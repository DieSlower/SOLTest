/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuButton.h"

#include "Menu/SOLSettingsSubsystem.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "SOLConstants.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Input/SButton.h"

namespace
{
    constexpr float MENU_BUTTON_MIN_STYLE_TINT = 1.0e-4f;   // A style tint channel below this is not divided out

    //////////////////////////////////////////////////////////////////////////
    // Returns the settled look of a state (long past every ramp and linger)
    FSOLSolidButtonLook MenuButtonSettled(const ESOLSolidButtonState state, const float s, const bool bSelected)
    {
        using namespace SOL::SolidMenu;
        const double settledS = static_cast<double>(HOVER_RAMP_S) + PRESSED_LINGER_S + 1.0;
        return SOLSolidMenu::EvaluateButton(state, state, settledS, s, bSelected);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two looks' animated amounts are equal
    bool MenuButtonSameAmounts(const FSOLSolidButtonLook& a, const FSOLSolidButtonLook& b)
    {
        return a.Lift == b.Lift && a.Edge == b.Edge && a.Led == b.Led && a.Underline == b.Underline
            && a.Trace == b.Trace;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a / b per channel, leaving a channel whose divisor is (near) zero undivided
    FLinearColor MenuButtonDivide(const FLinearColor& a, const FLinearColor& b)
    {
        // Divides one channel when its divisor is usable
        const auto divide = [](const float x, const float d)
        {
            return d > MENU_BUTTON_MIN_STYLE_TINT ? x / d : x;
        };
        return FLinearColor(divide(a.R, b.R), divide(a.G, b.G), divide(a.B, b.B), divide(a.A, b.A));
    }

    // Reaches SButton::SetIsFocusable, which SButton declares protected (UButton has no runtime setter, only
    // InitIsFocusable for the next rebuild): a pointer to the member formed through a derived class is a pointer to
    // SButton's member and may be called on any SButton. Never instantiated
    struct FSOLMenuButtonFocusAccess : SButton
    {
        //////////////////////////////////////////////////////////////////////////
        // Sets whether a built SButton can take keyboard focus
        static void Apply(SButton& button, const bool bFocusable)
        {
            void (SButton::*const setter)(bool) = &FSOLMenuButtonFocusAccess::SetIsFocusable;
            (button.*setter)(bFocusable);
        }
    };
}

//////////////////////////////////////////////////////////////////////////
// Returns the shown state from the button's flags: Disabled, else Pressed, else Hovered (focus), else Normal
ESOLSolidButtonState FSOLSolidButtonAnim::ResolveState(const bool bDisabled, const bool bPressed, const bool bFocused)
{
    return bDisabled ? ESOLSolidButtonState::Disabled
        : bPressed ? ESOLSolidButtonState::Pressed
        : bFocused ? ESOLSolidButtonState::Hovered
        : ESOLSolidButtonState::Normal;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether two looks differ on screen: their 8-bit tint codes or their lifts
bool FSOLSolidButtonAnim::DiffersOnScreen(const FSOLSolidButtonLook& a, const FSOLSolidButtonLook& b)
{
    // EncodeButtonTint is already quantized to 8 bits; half a level apart is a different level
    const FLinearColor ta = SOLSolidMenu::EncodeButtonTint(a);
    const FLinearColor tb = SOLSolidMenu::EncodeButtonTint(b);
    const float halfLevel = 0.5f / 255.0f;
    if (FMath::Abs(ta.R - tb.R) > halfLevel || FMath::Abs(ta.G - tb.G) > halfLevel
        || FMath::Abs(ta.B - tb.B) > halfLevel || FMath::Abs(ta.A - tb.A) > halfLevel)
    {
        return true;
    }
    return FMath::Abs(SOLSolidMenu::LiftOffset(a) - SOLSolidMenu::LiftOffset(b)) > SOL::SolidMenu::LIFT_EPSILON_PX;
}

//////////////////////////////////////////////////////////////////////////
// Moves toward a target state and returns the look to show
FSOLSolidButtonLook FSOLSolidButtonAnim::Update(const ESOLSolidButtonState target, const bool bSelected,
    const double nowS, const float s)
{
    using namespace SOL::SolidMenu;
    if (!FMath::IsFinite(nowS))
    {
        return mLook;
    }
    mIsSelected = bSelected;

    // The first update shows its state settled (a button appears at rest, not ramping in)
    if (!mHasUpdated)
    {
        mHasUpdated = true;
        mShown = target;
        mTarget = target;
        mTargetS = nowS;
        mChangeS = nowS;
        mPressS = nowS;
        mAtChange = MenuButtonSettled(target, s, bSelected);
        mLook = mAtChange;
        return mLook;
    }
    if (target != mTarget)
    {
        mTarget = target;
        mTargetS = nowS;
    }

    // A press lingers PRESSED_LINGER_S after it began, whatever the target does meanwhile (disabling ends it at once)
    const double lingerEndS = mPressS + PRESSED_LINGER_S;
    const bool bLingering = mShown == ESOLSolidButtonState::Pressed && target != ESOLSolidButtonState::Disabled
        && nowS < lingerEndS;
    const ESOLSolidButtonState desired = bLingering ? ESOLSolidButtonState::Pressed : target;
    if (desired != mShown)
    {
        // Leaving a press starts the ramp at the linger's end or at the release, whichever is later; any other change
        // starts now. The ramp starts from the look shown at that moment (no pop on a reversal)
        const bool bLeavingPress = mShown == ESOLSolidButtonState::Pressed && desired != ESOLSolidButtonState::Disabled;
        const double changeS = bLeavingPress ? FMath::Min(nowS, FMath::Max(lingerEndS, mTargetS)) : nowS;
        mAtChange = SOLSolidMenu::EvaluateButtonFrom(mShown, mAtChange, changeS - mChangeS, s, bSelected);
        mShown = desired;
        mChangeS = changeS;
        if (desired == ESOLSolidButtonState::Pressed)
        {
            mPressS = nowS;
        }
    }
    mLook = SOLSolidMenu::EvaluateButtonFrom(mShown, mAtChange, nowS - mChangeS, s, bSelected);
    return mLook;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the look no longer changes without a new target
bool FSOLSolidButtonAnim::IsSettled(const double nowS, const float s) const
{
    using namespace SOL::SolidMenu;
    if (!mHasUpdated || !FMath::IsFinite(nowS))
    {
        return true;
    }

    // A change still owed (the press linger holds the shown state) is not settled
    if (mTarget != mShown)
    {
        return false;
    }
    const double sinceS = nowS - mChangeS;
    if (FSOLSolidMenuParamCache::SanitizeStrength(s) <= 0.0f || sinceS >= HOVER_RAMP_S)
    {
        return true;
    }

    // Mid-ramp: settled only when the ramp has nothing left to move (it started at its end look)
    const FSOLSolidButtonLook now = SOLSolidMenu::EvaluateButtonFrom(mShown, mAtChange, sinceS, s, mIsSelected);
    const FSOLSolidButtonLook end = SOLSolidMenu::EvaluateButtonFrom(mShown, mAtChange, HOVER_RAMP_S, s, mIsSelected);
    return MenuButtonSameAmounts(now, end);
}

//////////////////////////////////////////////////////////////////////////
// Styles the button as a solid variant, creates its label in the owner's widget tree and routes clicks and hovers
void USOLMenuButton::Setup(UWidgetTree& tree, const FString& label, const int32 index, const int32 fontSize,
    const ESOLSolidButtonVariant variant)
{
    mIndex = index;
    mVariant = variant;
    const USOLSolidMenuSubsystem* solid = USOLSolidMenuSubsystem::Get(&tree);

    // Every state brush is the variant's hull with the Normal look; the background colour carries the animated look
    const FButtonStyle style = SOLSolidStyle::MakeButtonStyle(solid, variant);
    mStyleTint = style.Normal.TintColor.GetSpecifiedColor();
    mIsMaterialDrawn = style.Normal.GetResourceObject() != nullptr;
    SetStyle(style);

    // Stack and tab labels carry the stack letter spacing; small buttons none
    FSlateFontInfo font = SOLSolidStyle::MenuFont(solid, ESOLMenuFontWeight::SemiBold, fontSize);
    font.LetterSpacing = variant == ESOLSolidButtonVariant::Small ? 0 : SOL::SolidMenu::LETTER_SPACING_STACK;
    Label = tree.ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
    Label->SetFont(font);
    Label->SetText(FText::FromString(label));
    SetContent(Label);

    OnClicked.AddUniqueDynamic(this, &USOLMenuButton::HandleClicked);
    OnHovered.AddUniqueDynamic(this, &USOLMenuButton::HandleHovered);
    mAnim = FSOLSolidButtonAnim();
    mHasApplied = false;
    mWriteCount = 0;
    ApplyLook(FSOLSolidButtonLook());
}

//////////////////////////////////////////////////////////////////////////
// Gives or removes the disabled art and blocks input (hit-test invisible, not focusable, no hover focus, clicks
// swallowed)
void USOLMenuButton::SetSolidDisabled(const bool bDisabled)
{
    if (bDisabled != mIsSolidDisabled)
    {
        // Keyboard navigation skips a disabled button; enabling restores the focusability it had
        if (bDisabled)
        {
            mIsFocusableWhenEnabled = GetIsFocusable();
        }
        const bool bFocusable = !bDisabled && mIsFocusableWhenEnabled;
        InitIsFocusable(bFocusable);
        if (MyButton.IsValid())
        {
            FSOLMenuButtonFocusAccess::Apply(*MyButton, bFocusable);
        }
    }
    mIsSolidDisabled = bDisabled;
    const ESlateVisibility visibility = GetVisibility();
    if (bDisabled && visibility == ESlateVisibility::Visible)
    {
        SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    else if (!bDisabled && visibility == ESlateVisibility::HitTestInvisible)
    {
        SetVisibility(ESlateVisibility::Visible);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the trace layer and polyline the hover extends (Stack and Tab only); a new layer (the panel was rebuilt, as
// after a return from flight) gets the current look's trace at once, since a settled refresh writes nothing
void USOLMenuButton::SetHoverTrace(const TSharedPtr<SSOLSolidTraceLayer>& layer, const FSOLSolidTrace& trace)
{
    if (!SOLSolidStyle::HasHoverTrace(mVariant))
    {
        return;
    }
    const bool bNewLayer = mTraceLayer.Pin() != layer;
    mTraceLayer = layer;
    mHoverTrace = trace;
    if (bNewLayer && mHasApplied)
    {
        UpdateHoverTrace(mAppliedLook);
    }
}

//////////////////////////////////////////////////////////////////////////
// Replaces the label text
void USOLMenuButton::SetLabel(const FString& label)
{
    if (Label != nullptr)
    {
        Label->SetText(FText::FromString(label));
    }
}

//////////////////////////////////////////////////////////////////////////
// Advances the look for the resolved state (disabled, pressed, focused) at a time and strength
void USOLMenuButton::RefreshHighlightAt(const double nowS, const float s)
{
    RefreshHighlightFor(FSOLSolidButtonAnim::ResolveState(mIsSolidDisabled, IsPressed(), HasKeyboardFocus()), nowS,
        s);
}

//////////////////////////////////////////////////////////////////////////
// Advances the look toward a state at a time and strength and writes it only when it differs on screen from the look
// last written (a compare when settled)
void USOLMenuButton::RefreshHighlightFor(const ESOLSolidButtonState state, const double nowS, const float s)
{
    // Settled since the last update, with the same inputs: nothing can have changed
    if (mHasApplied && mIsAppliedSettled && state == mAppliedState && mIsSelected == mIsAppliedSelected
        && s == mAppliedStrength)
    {
        return;
    }
    mAppliedState = state;
    mIsAppliedSelected = mIsSelected;
    mAppliedStrength = s;
    const FSOLSolidButtonLook look = mAnim.Update(state, mIsSelected, nowS, s);
    mIsAppliedSettled = mAnim.IsSettled(nowS, s);
    mAppliedLook = look;

    // Compared with what was last written, not with the last refresh: small steps add up to a visible change
    if (mHasApplied && !FSOLSolidButtonAnim::DiffersOnScreen(look, mWrittenLook))
    {
        return;
    }
    ApplyLook(look);
}

//////////////////////////////////////////////////////////////////////////
// Applies a look: the hull tint (background colour x style tint = the tint the material decodes), the lift, the
// label colour and the hover trace, each only when it differs from what was last written
void USOLMenuButton::ApplyLook(const FSOLSolidButtonLook& look)
{
    const float writtenLift = mWrittenLook.Lift;
    const FLinearColor tint = mIsMaterialDrawn ? SOLSolidStyle::BrushTint(look)
        : SOLSolidStyle::FlatColor(SOLSolidStyle::ButtonStyleOf(mVariant), look);
    if (!mHasApplied || tint != mAppliedTint)
    {
        SetBackgroundColor(MenuButtonDivide(tint, mStyleTint));
        mAppliedTint = tint;
        ++mWriteCount;
    }
    const float lift = SOLSolidMenu::LiftOffset(look);
    const bool bWriteLift = !mHasApplied
        || FMath::Abs(lift - SOLSolidMenu::LiftOffset(mWrittenLook)) > SOL::SolidMenu::LIFT_EPSILON_PX;
    if (bWriteLift)
    {
        SetRenderTranslation(FVector2D(0.0f, lift));
        ++mWriteCount;
    }
    const FLinearColor labelColor = SOLSolidStyle::LabelColor(look);
    if (Label != nullptr && (!mHasApplied || labelColor != mAppliedLabelColor))
    {
        Label->SetColorAndOpacity(FSlateColor(labelColor));
        mAppliedLabelColor = labelColor;
        ++mWriteCount;
    }
    UpdateHoverTrace(look);
    mAppliedLook = look;

    // The written look keeps the lift actually on screen when this write left it alone
    mWrittenLook = look;
    if (!bWriteLift)
    {
        mWrittenLook.Lift = writtenLift;
    }
    mHasApplied = true;
}

//////////////////////////////////////////////////////////////////////////
// Feeds the trace layer this button's hover trace: while it shows hover, or while the layer still shows its trace
// (falling); a button whose trace another button replaced leaves the layer alone
void USOLMenuButton::UpdateHoverTrace(const FSOLSolidButtonLook& look)
{
    const TSharedPtr<SSOLSolidTraceLayer> layer = mTraceLayer.Pin();
    if (!layer.IsValid() || mHoverTrace.Count < 2)
    {
        return;
    }
    if (mAnim.GetShownState() == ESOLSolidButtonState::Hovered)
    {
        layer->SetHoverTrace(mHoverTrace, look.Trace);
    }
    else if (layer->IsShowingHoverTrace(mHoverTrace))
    {
        if (look.Trace > 0.0f)
        {
            layer->SetHoverTrace(mHoverTrace, look.Trace);
        }
        else
        {
            layer->ClearHoverTrace();
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// UButton click: forwards the index to OnMenuClicked (not while solid-disabled)
void USOLMenuButton::HandleClicked()
{
    if (!mIsSolidDisabled)
    {
        OnMenuClicked.ExecuteIfBound(mIndex);
    }
}

//////////////////////////////////////////////////////////////////////////
// Takes keyboard focus as a mouse hover does, unless a Settings page is listening for a rebind key or the button is
// solid-disabled
void USOLMenuButton::NotifyHovered()
{
    // While a rebind listens, the next key is the new binding: moving the focus would let it land on this button
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(this);
    if (mIsSolidDisabled || (settings != nullptr && settings->IsKeyCaptureActive()))
    {
        return;
    }
    SetKeyboardFocus();
}

//////////////////////////////////////////////////////////////////////////
// UButton hover: takes keyboard focus so the highlight follows the mouse (NotifyHovered)
void USOLMenuButton::HandleHovered()
{
    NotifyHovered();
}
