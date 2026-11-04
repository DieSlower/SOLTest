/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLSpeedPanelWidget.h"

#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLHoloFrame.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloFrameWidget.h"
#include "UI/SOLHudFormat.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Styling/CoreStyle.h"

namespace
{
    // Fonts (Slate's default font family) and sizes
    const TCHAR* const PANEL_FONT_REGULAR = TEXT("Regular");
    const TCHAR* const PANEL_FONT_MONO = TEXT("Mono");
    constexpr int32 PANEL_TITLE_FONT_SIZE = 18;
    constexpr int32 PANEL_DIGIT_FONT_SIZE = 34;
    constexpr int32 PANEL_TEXT_FONT_SIZE = 13;
    constexpr int32 PANEL_ROW_FONT_SIZE = 14;

    // Layout
    constexpr float PANEL_PADDING = 20.0f;
    constexpr float PANEL_SECTION_GAP = 10.0f;
    constexpr float PANEL_LIST_WIDTH = 440.0f;
    constexpr float PANEL_LIST_HEIGHT = 230.0f;

    // Room the hologram frame needs inside the panel (local units; the frame's pixel sizes are about 1.5 local units at
    // 720-1440 px high views, as in the Overlay Menu): below the title, so the steppers start under the frame's title
    // underline (34 px), on the right, clear of the bottom-right step-in, and between the stepper section and the list
    // section, room for the stepped divider (6 px) clear of the key hint
    constexpr float PANEL_FRAMED_TITLE_GAP = 14.0f;
    constexpr float PANEL_FRAMED_RIGHT_ROOM = 16.0f;
    constexpr float PANEL_FRAMED_SECTION_GAP = 26.0f;

    // A highlighted digit's or the unit field's hover rect is narrowed by this (reference px a side, so the frame's
    // HOVER_PAD_X_PX leaves 2 px around the glyph), keeping at least this half width (local units)
    constexpr float PANEL_DIGIT_HOVER_INSET_PX = 6.0f;
    constexpr float PANEL_DIGIT_HOVER_MIN_HALF_WIDTH = 1.0f;

    // Frames the hologram frame's layout is re-read after a change: the change is arranged in the paint of the frame
    // it happened in, so the next tick reads it (the second frame is a margin)
    constexpr int32 PANEL_LAYOUT_SETTLE_FRAMES = 2;

    // Colors: the LT HUD palette like the flight HUD (SDD 21 phase B): Primary-tinted text over the hologram frame's
    // faint tint, the highlight in LT Secondary; the screen dim behind the panel (kept from before the hologram look)
    const FLinearColor PANEL_SCREEN_DIM(0.0f, 0.0f, 0.0f, 0.35f);
    const FLinearColor PANEL_TEXT(0.75f, 0.9f, 1.0f, 1.0f);
    const FLinearColor PANEL_DIM_TEXT(0.35f, 0.47f, 0.58f, 1.0f);
    const FLinearColor& PANEL_HIGHLIGHT = SOL::HudPalette::SECONDARY;

    // Hundredths per unit: the odometer's least significant place is 10^MinDigit
    constexpr double PANEL_HUNDREDTHS_PER_UNIT = 100.0;

    //////////////////////////////////////////////////////////////////////////
    // Appends a speed with its auto-picked unit (m/s, km/s or c)
    void SpeedPanelAppendSpeed(FString& text, const double speedMps)
    {
        const ESOLSpeedUnit unit = SOLHudFormat::PickSpeedUnit(speedMps);
        const double value = SOLHudFormat::ToUnitValue(speedMps, unit);
        switch (unit)
        {
        case ESOLSpeedUnit::KilometersPerSecond:
            text.Appendf(TEXT("%.2f km/s"), value);
            break;
        case ESOLSpeedUnit::LightSpeed:
            text.Appendf(TEXT("%.4f c"), value);
            break;
        case ESOLSpeedUnit::MetersPerSecond:
        default:
            text.Appendf(TEXT("%.1f m/s"), value);
            break;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unit field's label
    const TCHAR* SpeedPanelUnitLabel(const ESOLSpeedUnit unit)
    {
        switch (unit)
        {
        case ESOLSpeedUnit::KilometersPerSecond:
            return TEXT("km/s");
        case ESOLSpeedUnit::LightSpeed:
            return TEXT("c");
        case ESOLSpeedUnit::MetersPerSecond:
        default:
            return TEXT("m/s");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a key has its own meaning inside the panel (steppers, list, unit)
    bool SpeedPanelIsOwnKey(const FKey& key)
    {
        return key == EKeys::Up || key == EKeys::Down || key == EKeys::Left || key == EKeys::Right
            || key == EKeys::Enter || key == EKeys::Tab || key == EKeys::U || key == EKeys::Escape;
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates a text block in the tree with a font face, size and color
    UTextBlock* SpeedPanelMakeText(UWidgetTree* tree, const TCHAR* text, const TCHAR* face, const int32 size,
        const FLinearColor& color)
    {
        UTextBlock* block = tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
        block->SetFont(FCoreStyle::GetDefaultFontStyle(face, size));
        block->SetColorAndOpacity(FSlateColor(color));
        block->SetText(FText::FromString(text));
        return block;
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds the widget tree (no widget blueprint) and makes the panel focusable
void USOLSpeedPanelWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetIsFocusable(true);
    BodyRegistry = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USOLBodyRegistrySubsystem>() : nullptr;

    // Full-screen dimmer (it also catches the wheel anywhere on screen) holding a centered panel; the hologram frame
    // sits behind the panel's content in an overlay and fills it
    RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Root"));
    RootBorder->SetBrushColor(PANEL_SCREEN_DIM);
    RootBorder->SetHorizontalAlignment(HAlign_Center);
    RootBorder->SetVerticalAlignment(VAlign_Center);
    WidgetTree->RootWidget = RootBorder;
    UOverlay* panelStack = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("PanelStack"));
    RootBorder->SetContent(panelStack);
    HoloFrame = WidgetTree->ConstructWidget<USOLHoloFrameWidget>(USOLHoloFrameWidget::StaticClass(),
        TEXT("HoloFrame"));
    UOverlaySlot* frameSlot = panelStack->AddChildToOverlay(HoloFrame);
    frameSlot->SetHorizontalAlignment(HAlign_Fill);
    frameSlot->SetVerticalAlignment(VAlign_Fill);
    PanelBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
    PanelBorder->SetBrushColor(FLinearColor::Transparent);
    PanelBorder->SetPadding(FMargin(PANEL_PADDING, PANEL_PADDING, PANEL_PADDING + PANEL_FRAMED_RIGHT_ROOM,
        PANEL_PADDING));
    UOverlaySlot* panelSlot = panelStack->AddChildToOverlay(PanelBorder);
    panelSlot->SetHorizontalAlignment(HAlign_Fill);
    panelSlot->SetVerticalAlignment(VAlign_Fill);
    UVerticalBox* column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
    PanelBorder->SetContent(column);
    TitleText = SpeedPanelMakeText(WidgetTree, TEXT(""), PANEL_FONT_REGULAR, PANEL_TITLE_FONT_SIZE, PANEL_TEXT);
    column->AddChildToVerticalBox(TitleText)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, PANEL_FRAMED_TITLE_GAP));

    // The stepper section above the frame's divider, the list section below it
    StepperSection = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("StepperSection"));
    column->AddChildToVerticalBox(StepperSection);
    ListSection = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ListSection"));
    column->AddChildToVerticalBox(ListSection)->SetPadding(FMargin(0.0f, PANEL_FRAMED_SECTION_GAP, 0.0f, 0.0f));

    // Steppers side by side: odometer digits (10^MaxDigit .. 10^MinDigit, point before the tenths) and the unit
    UHorizontalBox* steppers = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    StepperSection->AddChildToVerticalBox(steppers)->SetPadding(FMargin(0.0f, PANEL_SECTION_GAP));
    DigitTexts.Reset();
    for (int32 digit = SOLSpeedStepper::MaxDigit; digit >= SOLSpeedStepper::MinDigit; --digit)
    {
        if (digit == -1)
        {
            steppers->AddChildToHorizontalBox(SpeedPanelMakeText(WidgetTree, TEXT("."), PANEL_FONT_MONO,
                PANEL_DIGIT_FONT_SIZE, PANEL_TEXT));
        }
        UTextBlock* digitText = SpeedPanelMakeText(WidgetTree, TEXT("0"), PANEL_FONT_MONO, PANEL_DIGIT_FONT_SIZE,
            PANEL_TEXT);
        steppers->AddChildToHorizontalBox(digitText);
        DigitTexts.Add(digitText);
    }
    UnitText = SpeedPanelMakeText(WidgetTree, TEXT("km/s"), PANEL_FONT_MONO, PANEL_DIGIT_FONT_SIZE, PANEL_TEXT);
    steppers->AddChildToHorizontalBox(UnitText)->SetPadding(FMargin(PANEL_SECTION_GAP * 2.0f, 0.0f, 0.0f, 0.0f));
    CapText = SpeedPanelMakeText(WidgetTree, TEXT(""), PANEL_FONT_REGULAR, PANEL_TEXT_FONT_SIZE, PANEL_TEXT);
    StepperSection->AddChildToVerticalBox(CapText);
    StepperSection->AddChildToVerticalBox(SpeedPanelMakeText(WidgetTree, TEXT("Up/Down or wheel: step digit (unit "
        "field: cycle)   Left/Right: move highlight   U: cycle unit   Tab: body list"), PANEL_FONT_REGULAR,
        PANEL_TEXT_FONT_SIZE, PANEL_DIM_TEXT));

    // Scrollable body list: one row per registry body with its orbital speed
    ListSection->AddChildToVerticalBox(SpeedPanelMakeText(WidgetTree, TEXT("BODY ORBITAL SPEEDS  (Tab, Up/Down, "
        "Enter = set cap)"), PANEL_FONT_REGULAR, PANEL_TEXT_FONT_SIZE, PANEL_TEXT));
    USizeBox* listBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
    listBox->SetWidthOverride(PANEL_LIST_WIDTH);
    listBox->SetHeightOverride(PANEL_LIST_HEIGHT);
    ListSection->AddChildToVerticalBox(listBox);
    BodyList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("BodyList"));
    listBox->SetContent(BodyList);
    BodyRows.Reset();
    const int32 bodyCount = BodyRegistry != nullptr ? BodyRegistry->GetRegistry().Num() : 0;
    mBodySpeedsMps.SetNumZeroed(bodyCount);
    for (int32 index = 0; index < bodyCount; ++index)
    {
        UTextBlock* row = SpeedPanelMakeText(WidgetTree, TEXT(""), PANEL_FONT_MONO, PANEL_ROW_FONT_SIZE, PANEL_TEXT);
        BodyList->AddChild(row);
        BodyRows.Add(row);
    }
    mOpenVisibility = GetVisibility();
}

//////////////////////////////////////////////////////////////////////////
// Seeds the steppers from the current cap, refreshes the body speeds and remembers the owner's apply and close delegates
void USOLSpeedPanelWidget::Open(const FSOLSpeedPanelApplyCap& applyCap, const FSimpleDelegate& close,
    const double currentCapMps, const FKey& toggleKey, const FString& title)
{
    mApplyCap = applyCap;
    mClose = close;
    mToggleKey = toggleKey;
    if (TitleText != nullptr)
    {
        TitleText->SetText(FText::FromString(title));
    }
    mStepper = SOLSpeedStepper::FromSpeedMps(currentCapMps);
    mIsUnitHighlighted = false;
    mIsListFocused = false;
    RefreshBodySpeeds();
    RefreshStepperText();
    RefreshListText();
    if (HoloFrame == nullptr)
    {
        return;
    }

    // Takes input again (a reopen during the close) and opens the frame (or reverses its close from where it is); a
    // frame not built yet (the first open: its Slate widget is made when the panel is first arranged) opens from the
    // first tick, which runs before the content's first paint, so the content waits hidden until then
    mIsClosing = false;
    SetVisibility(mOpenVisibility);
    SetIsFocusable(true);
    mIsOpenPending = true;
    if (HoloFrame->GetFrame().IsValid())
    {
        StartFrameOpen();
    }
    else
    {
        ApplyContentOpacity(0.0f);
    }
    MarkLayoutDirty();
}

//////////////////////////////////////////////////////////////////////////
// Opens the built frame (or reverses its close) with the open cue; the content follows the frame's fill
void USOLSpeedPanelWidget::StartFrameOpen()
{
    mIsOpenPending = false;
    HoloFrame->BeginOpen();

    // The highlight is set while the anim still reads Opening (also at strength 0, where the next advance makes it
    // Open), so opening asks for the open cue only, never a hover cue (SDD 5: hover on a highlight move)
    UpdateFrameHover();
    mIsAnimating = true;
    ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
}

//////////////////////////////////////////////////////////////////////////
// Starts the non-blocking close: the owner gives input back at once, the frame's close plays, then the widget removes
// itself
void USOLSpeedPanelWidget::BeginClose()
{
    mIsClosing = true;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    SetIsFocusable(false);

    // The keyboard goes back to the game viewport now (the owner's input mode change lands later), so no key reaches
    // the panel while it closes
    if (HasAnyUserFocus() && FSlateApplication::IsInitialized())
    {
        FSlateApplication::Get().SetAllUserFocusToGameViewport();
    }
    if (HoloFrame == nullptr || !IsInViewport() || mIsOpenPending)
    {
        FinishClose();
        return;
    }
    HoloFrame->BeginClose();
    if (HoloFrame->AdvanceAnim() == ESOLHoloMenuPhase::Closed)
    {
        FinishClose();
        return;
    }
    mIsAnimating = true;
    ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
}

//////////////////////////////////////////////////////////////////////////
// Ends the close: removes the widget (the next Open adds it again)
void USOLSpeedPanelWidget::FinishClose()
{
    mIsAnimating = false;
    mIsOpenPending = false;
    ApplyContentOpacity(0.0f);
    RemoveFromParent();
}

//////////////////////////////////////////////////////////////////////////
// Removes the panel at once, without the close (teardown); the frame drops to Closed, so the next Open draws in again
// with its open cue
void USOLSpeedPanelWidget::Dismiss()
{
    mIsClosing = true;
    if (HoloFrame != nullptr)
    {
        HoloFrame->ResetClosed();
    }
    FinishClose();
}

//////////////////////////////////////////////////////////////////////////
// While the frame animates: advances it and follows its fill, removing the widget once closed; after a change (or a
// scroll of the body list): lays the frame out again. Nothing else while the panel is open and idle
void USOLSpeedPanelWidget::NativeTick(const FGeometry& geometry, const float deltaSeconds)
{
    Super::NativeTick(geometry, deltaSeconds);
    if (HoloFrame == nullptr)
    {
        return;
    }
    if (mIsOpenPending && HoloFrame->GetFrame().IsValid())
    {
        StartFrameOpen();
    }
    if (mIsAnimating)
    {
        const ESOLHoloMenuPhase phase = HoloFrame->AdvanceAnim();
        if (phase == ESOLHoloMenuPhase::Closed)
        {
            FinishClose();
            return;
        }
        ApplyContentOpacity(HoloFrame->GetLook().FillAlpha);
        mIsAnimating = phase != ESOLHoloMenuPhase::Open;
    }
    if (mIsClosing)
    {
        return;
    }

    // A wheel over the list scrolls it (the scroll box takes the wheel) and keeping the selection in view scrolls it
    // a frame later: the highlighted row moved, so the hover mark follows
    const float scrollOffset = BodyList != nullptr ? BodyList->GetScrollOffset() : 0.0f;
    if (scrollOffset != mLastScrollOffset)
    {
        mLastScrollOffset = scrollOffset;
        MarkLayoutDirty();
    }

    // A window resize or UI scale change moves the lengths given in reference px (divider step, digit inset)
    const float localPerPx = GetFrameLocalPerPx();
    if (localPerPx != mLastLocalPerPx)
    {
        mLastLocalPerPx = localPerPx;
        MarkLayoutDirty();
    }
    if (mLayoutDirtyFrames > 0)
    {
        UpdateFrameLayout();
    }
}

//////////////////////////////////////////////////////////////////////////
// Asks the frame layout to be rebuilt over the next frames
void USOLSpeedPanelWidget::MarkLayoutDirty()
{
    if (HoloFrame != nullptr)
    {
        mLayoutDirtyFrames = PANEL_LAYOUT_SETTLE_FRAMES;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the frame's layout and hover from the content as last arranged, in the frame's local units (the frame fills
// the panel, so the panel's top-left is the origin); waits while the frame has not been arranged yet
void USOLSpeedPanelWidget::UpdateFrameLayout()
{
    const FGeometry& frameGeometry = HoloFrame->GetCachedGeometry();
    const FVector2D frameSize = frameGeometry.GetLocalSize();
    if (frameSize.X <= 0.0 || frameSize.Y <= 0.0)
    {
        return;
    }

    FSOLHoloSpeedLayoutInput input;
    input.TitleWidth = static_cast<float>(TitleText->GetDesiredSize().X);
    input.StepperSection = HoloFrame->ToFrameLocal(StepperSection->GetCachedGeometry());
    input.ListSection = HoloFrame->ToFrameLocal(ListSection->GetCachedGeometry());
    input.LocalPerPx = GetFrameLocalPerPx();
    FSOLHoloFrameSpec layout;
    SOLHoloFrameBuild::MakeSpeedLayout(input, layout);
    HoloFrame->SetLayout(layout);
    UpdateFrameHover();
    --mLayoutDirtyFrames;
}

//////////////////////////////////////////////////////////////////////////
// Returns the item the hover mark is on: a digit's slot, the unit field (the digit count) or a body row
int32 USOLSpeedPanelWidget::GetHighlightedItem() const
{
    const int32 digitCount = DigitTexts.Num();
    if (mIsListFocused)
    {
        return digitCount + 1 + mListSelection;
    }
    return mIsUnitHighlighted ? digitCount : SOLSpeedStepper::MaxDigit - mStepper.HighlightedDigit;
}

//////////////////////////////////////////////////////////////////////////
// Sets the frame's hovered item around its text: a digit or the unit field (brackets with a tick above and below), or
// a body row (brackets and a pointer line); the frame asks for the hover cue when the item changed while open
void USOLSpeedPanelWidget::UpdateFrameHover()
{
    const int32 item = GetHighlightedItem();
    const UTextBlock* text = nullptr;
    ESOLHoloHoverMark mark = ESOLHoloHoverMark::CentreTicks;
    FBox2f rect(ForceInit);
    if (mIsListFocused)
    {
        // A body row's slot comes from the list (a row scrolled out of view keeps a stale geometry): out of view, the
        // same item with no rect, so no mark and no cue
        text = BodyRows.IsValidIndex(mListSelection) ? BodyRows[mListSelection].Get() : nullptr;
        mark = ESOLHoloHoverMark::RowPointer;
        if (text != nullptr)
        {
            rect = SOLHoloFrameBuild::TextItemRect(GetVisibleRowSlot(mListSelection),
                static_cast<float>(text->GetDesiredSize().X), 0.0f);
        }
    }
    else
    {
        const int32 slot = SOLSpeedStepper::MaxDigit - mStepper.HighlightedDigit;
        text = mIsUnitHighlighted ? UnitText.Get() : DigitTexts.IsValidIndex(slot) ? DigitTexts[slot].Get() : nullptr;
        if (text != nullptr)
        {
            rect = SOLHoloFrameBuild::TextItemRect(HoloFrame->ToFrameLocal(text->GetCachedGeometry()),
                static_cast<float>(text->GetDesiredSize().X), 0.0f);
        }
    }

    // A digit or the unit field sits between its neighbours: its rect is narrowed so the frame's hover pad leaves the
    // brackets hugging the glyph (as in the approved sample) rather than over the next digits
    if (mark == ESOLHoloHoverMark::CentreTicks && SOLHoloMenu::IsValidRect(rect))
    {
        const float inset = FMath::Min(PANEL_DIGIT_HOVER_INSET_PX * GetFrameLocalPerPx(),
            0.5f * (rect.Max.X - rect.Min.X) - PANEL_DIGIT_HOVER_MIN_HALF_WIDTH);
        if (inset > 0.0f)
        {
            rect.Min.X += inset;
            rect.Max.X -= inset;
        }
    }
    HoloFrame->SetHoveredItem(text != nullptr ? item : INDEX_NONE, rect, mark);
}

//////////////////////////////////////////////////////////////////////////
// Returns a body row's slot in the frame's local units from the list's geometry, the row height and the list's scroll
// offset (the rows are one line each in one font, so one height); invalid when the row's centre is outside the list
FBox2f USOLSpeedPanelWidget::GetVisibleRowSlot(const int32 index) const
{
    if (BodyList == nullptr || !BodyRows.IsValidIndex(index))
    {
        return FBox2f(ForceInit);
    }
    const FGeometry& listGeometry = BodyList->GetCachedGeometry();
    const FVector2f listSize(listGeometry.GetLocalSize());
    const float rowHeight = static_cast<float>(BodyRows[index]->GetDesiredSize().Y);
    const float rowTop = rowHeight * static_cast<float>(index) - BodyList->GetScrollOffset();
    const float rowCentre = rowTop + 0.5f * rowHeight;
    if (rowHeight <= 0.0f || rowCentre < 0.0f || rowCentre > listSize.Y)
    {
        return FBox2f(ForceInit);
    }
    return HoloFrame->ToFrameLocal(listGeometry.MakeChild(FVector2f(listSize.X, rowHeight),
        FSlateLayoutTransform(FVector2f(0.0f, rowTop))));
}

//////////////////////////////////////////////////////////////////////////
// Returns the body list's centre in absolute (screen) Slate space as last laid out, or zero
FVector2D USOLSpeedPanelWidget::GetListScreenCenter() const
{
    if (BodyList == nullptr)
    {
        return FVector2D::ZeroVector;
    }
    const FGeometry& geometry = BodyList->GetCachedGeometry();
    return FVector2D(geometry.LocalToAbsolute(geometry.GetLocalSize() * 0.5f));
}

//////////////////////////////////////////////////////////////////////////
// Returns the body list's scroll offset
float USOLSpeedPanelWidget::GetListScrollOffset() const
{
    return BodyList != nullptr ? BodyList->GetScrollOffset() : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns the frame's local units per pixel of the 720 px reference canvas: its UI scale (from its last paint) over its
// geometry scale, so lengths given in reference px keep their pixel size in the frame
float USOLSpeedPanelWidget::GetFrameLocalPerPx() const
{
    const TSharedPtr<SSOLHoloFrame> frame = HoloFrame != nullptr ? HoloFrame->GetFrame() : nullptr;
    const float uiScale = SOLHoloMenu::UiScale(frame.IsValid() ? frame->GetLastPixelSpec().UiScale : 1.0f);
    const float geometryScale = HoloFrame != nullptr ? HoloFrame->GetCachedGeometry().Scale : 0.0f;
    return geometryScale > 0.0f ? uiScale / geometryScale : 1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Sets the content's render opacity and the screen dim's alpha when the fill changed, so both fade with the frame
void USOLSpeedPanelWidget::ApplyContentOpacity(const float opacity)
{
    if (opacity == mContentOpacity || PanelBorder == nullptr)
    {
        return;
    }
    mContentOpacity = opacity;
    PanelBorder->SetRenderOpacity(opacity);
    RootBorder->SetBrushColor(FLinearColor(PANEL_SCREEN_DIM.R, PANEL_SCREEN_DIM.G, PANEL_SCREEN_DIM.B,
        PANEL_SCREEN_DIM.A * opacity));
}

//////////////////////////////////////////////////////////////////////////
// Requests the select cue (not while closing; at any strength, D21)
void USOLSpeedPanelWidget::RequestSelectCue()
{
    if (HoloFrame != nullptr && !mIsClosing)
    {
        HoloFrame->PlaySelectCue();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's speed as listed when the panel was opened (m/s, relative to its parent), or 0
double USOLSpeedPanelWidget::GetListedBodySpeedMps(const int32 bodyIndex) const
{
    return mBodySpeedsMps.IsValidIndex(bodyIndex) ? mBodySpeedsMps[bodyIndex] : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Handles one key press; returns true when the panel used it (verification scripts call it like Slate does)
bool USOLSpeedPanelWidget::HandlePanelKey(const FKey& key)
{
    // A closing panel takes no keys (the ship has the input back)
    if (mIsClosing)
    {
        return false;
    }

    // The panel's own keys keep their meaning even when the speed-panel binding was moved onto one of them
    const bool bToggleKey = key == mToggleKey && !SpeedPanelIsOwnKey(key);
    if (bToggleKey || key == EKeys::Escape || (key == EKeys::Enter && !mIsListFocused))
    {
        RequestClose();
        return true;
    }
    if (key == EKeys::Up || key == EKeys::Down)
    {
        HandleWheelSteps(key == EKeys::Up ? 1 : -1);
        return true;
    }
    if (key == EKeys::Enter)
    {
        ApplySelectedBody();
        return true;
    }
    if (key == EKeys::Tab)
    {
        mIsListFocused = !mIsListFocused;
        RefreshStepperText();
        RefreshListText();
        return true;
    }
    if (mIsListFocused)
    {
        return false;
    }
    if (key == EKeys::Left || key == EKeys::Right)
    {
        MoveHighlight(key == EKeys::Right);
        return true;
    }
    if (key == EKeys::U)
    {
        mStepper = SOLSpeedStepper::CycleUnit(mStepper, false);
        RefreshStepperText();
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Steps the highlighted digit (or the list selection) by wheel notches (positive = up)
void USOLSpeedPanelWidget::HandleWheelSteps(const int32 steps)
{
    const bool bDown = steps < 0;
    for (int32 step = 0; step < FMath::Abs(steps); ++step)
    {
        if (mIsListFocused)
        {
            // Up moves toward the top of the list
            mListSelection = FMath::Clamp(mListSelection + (bDown ? 1 : -1), 0, FMath::Max(0, BodyRows.Num() - 1));
        }
        else if (mIsUnitHighlighted)
        {
            mStepper = SOLSpeedStepper::CycleUnit(mStepper, bDown);
        }
        else
        {
            StepHighlightedDigit(bDown);
        }
    }
    RefreshStepperText();
    RefreshListText();
}

//////////////////////////////////////////////////////////////////////////
// Routes a key press to HandlePanelKey and consumes every key so none reaches the game viewport
FReply USOLSpeedPanelWidget::NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent)
{
    if (mIsClosing)
    {
        return Super::NativeOnKeyDown(geometry, keyEvent);
    }
    ++mSlateKeyCount;
    HandlePanelKey(keyEvent.GetKey());
    return FReply::Handled();
}

//////////////////////////////////////////////////////////////////////////
// Consumes key releases so they do not reach the game viewport either
FReply USOLSpeedPanelWidget::NativeOnKeyUp(const FGeometry& /*geometry*/, const FKeyEvent& /*keyEvent*/)
{
    return FReply::Handled();
}

//////////////////////////////////////////////////////////////////////////
// Routes the mouse wheel to HandleWheelSteps
FReply USOLSpeedPanelWidget::NativeOnMouseWheel(const FGeometry& geometry, const FPointerEvent& mouseEvent)
{
    if (mIsClosing)
    {
        return Super::NativeOnMouseWheel(geometry, mouseEvent);
    }
    const float delta = mouseEvent.GetWheelDelta();
    int32 steps = FMath::RoundToInt32(delta);
    if (steps == 0 && delta != 0.0f)
    {
        steps = delta > 0.0f ? 1 : -1;
    }
    HandleWheelSteps(steps);
    return FReply::Handled();
}

//////////////////////////////////////////////////////////////////////////
// Steps the highlighted digit up or down and applies the resulting cap
void USOLSpeedPanelWidget::StepHighlightedDigit(const bool bDown)
{
    ApplyCapMps(SOLSpeedStepper::StepDigit(mStepper, bDown));
}

//////////////////////////////////////////////////////////////////////////
// Moves the highlight one place left (more significant) or right (toward the unit field)
void USOLSpeedPanelWidget::MoveHighlight(const bool bRight)
{
    if (bRight)
    {
        if (mStepper.HighlightedDigit <= SOLSpeedStepper::MinDigit)
        {
            mIsUnitHighlighted = true;
        }
        else
        {
            --mStepper.HighlightedDigit;
        }
    }
    else if (mIsUnitHighlighted)
    {
        mIsUnitHighlighted = false;
        mStepper.HighlightedDigit = SOLSpeedStepper::MinDigit;
    }
    else
    {
        mStepper.HighlightedDigit = FMath::Min(mStepper.HighlightedDigit + 1, SOLSpeedStepper::MaxDigit);
    }
    mStepper.HighlightedDigit = FMath::Clamp(mStepper.HighlightedDigit, SOLSpeedStepper::MinDigit,
        SOLSpeedStepper::MaxDigit);
    RefreshStepperText();
}

//////////////////////////////////////////////////////////////////////////
// Sets the cap to the highlighted body's listed speed, in that speed's natural unit
void USOLSpeedPanelWidget::ApplySelectedBody()
{
    if (!mBodySpeedsMps.IsValidIndex(mListSelection))
    {
        return;
    }
    mStepper = SOLSpeedStepper::FromSpeedMps(mBodySpeedsMps[mListSelection]);
    mIsUnitHighlighted = false;
    mIsListFocused = false;
    ApplyCapMps(SOLHudFormat::FromUnitValue(mStepper.ValueInUnit, mStepper.Unit));
    if (BodyRegistry != nullptr)
    {
        UE_LOG(LogSOL, Log, TEXT("SpeedPanel: cap set from %s's speed %.3f m/s"),
            *BodyRegistry->GetRegistry().GetName(mListSelection).ToString(), mBodySpeedsMps[mListSelection]);
    }
    RefreshStepperText();
    RefreshListText();
}

//////////////////////////////////////////////////////////////////////////
// Sends a cap to the ship and re-expresses the steppers' value in their current unit
void USOLSpeedPanelWidget::ApplyCapMps(const double capMps)
{
    double appliedMps = capMps;
    if (mApplyCap.IsBound())
    {
        appliedMps = mApplyCap.Execute(capMps);
    }
    mStepper.ValueInUnit = SOLHudFormat::ToUnitValue(appliedMps, mStepper.Unit);
    UE_LOG(LogSOL, Log, TEXT("SpeedPanel: cap %.3f m/s (%.2f %s, digit %d)"), appliedMps, mStepper.ValueInUnit,
        SpeedPanelUnitLabel(mStepper.Unit), mStepper.HighlightedDigit);
    RequestSelectCue();
}

//////////////////////////////////////////////////////////////////////////
// Reads every body's speed relative to its parent into the reused list buffer
void USOLSpeedPanelWidget::RefreshBodySpeeds()
{
    if (BodyRegistry == nullptr)
    {
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const int32 bodyCount = FMath::Min(registry.Num(), mBodySpeedsMps.Num());
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const int32 parent = registry.GetParent(index);
        const FVector3d parentVelocity = parent == INDEX_NONE ? FVector3d::ZeroVector : registry.GetVelocityMps(parent);
        mBodySpeedsMps[index] = (registry.GetVelocityMps(index) - parentVelocity).Size();
    }
}

//////////////////////////////////////////////////////////////////////////
// Re-renders the digit, unit and cap texts from the stepper state
void USOLSpeedPanelWidget::RefreshStepperText()
{
    // Whole hundredths of the value, so each odometer place is an exact integer digit
    const int64 hundredths = FMath::RoundToInt64(mStepper.ValueInUnit * PANEL_HUNDREDTHS_PER_UNIT);
    const bool bStepperActive = !mIsListFocused;
    for (int32 slot = 0; slot < DigitTexts.Num(); ++slot)
    {
        const int32 digit = SOLSpeedStepper::MaxDigit - slot;
        int64 placeHundredths = 1;
        for (int32 power = SOLSpeedStepper::MinDigit; power < digit; ++power)
        {
            placeHundredths *= 10;
        }
        const int32 value = static_cast<int32>((hundredths / placeHundredths) % 10);
        const TCHAR character[2] = { static_cast<TCHAR>(TEXT('0') + value), TEXT('\0') };
        DigitTexts[slot]->SetText(FText::FromString(character));

        // Highlighted place bright, leading zeros dim
        const bool bHighlighted = bStepperActive && !mIsUnitHighlighted && digit == mStepper.HighlightedDigit;
        const bool bLeadingZero = digit > 0 && hundredths < placeHundredths;
        DigitTexts[slot]->SetColorAndOpacity(FSlateColor(bHighlighted ? PANEL_HIGHLIGHT
            : (bLeadingZero ? PANEL_DIM_TEXT : PANEL_TEXT)));
    }
    UnitText->SetText(FText::FromString(SpeedPanelUnitLabel(mStepper.Unit)));
    UnitText->SetColorAndOpacity(FSlateColor(bStepperActive && mIsUnitHighlighted ? PANEL_HIGHLIGHT : PANEL_TEXT));

    // The cap the ship now flies with, in its auto-picked unit
    FString capLine(TEXT("Ship speed cap: "));
    SpeedPanelAppendSpeed(capLine, SOLHudFormat::FromUnitValue(mStepper.ValueInUnit, mStepper.Unit));
    CapText->SetText(FText::FromString(capLine));

    // The highlight or the unit's width may have changed: the hover mark follows
    MarkLayoutDirty();
}

//////////////////////////////////////////////////////////////////////////
// Re-renders the body list rows and keeps the selection in view
void USOLSpeedPanelWidget::RefreshListText()
{
    if (BodyRegistry == nullptr)
    {
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    FString row;
    for (int32 index = 0; index < BodyRows.Num() && index < registry.Num(); ++index)
    {
        const bool bSelected = index == mListSelection;
        row.Reset();
        row.Append(bSelected && mIsListFocused ? TEXT("> ") : TEXT("  "));
        const FString name = registry.GetName(index).ToString();
        row.Appendf(TEXT("%-10s "), *name);
        SpeedPanelAppendSpeed(row, mBodySpeedsMps.IsValidIndex(index) ? mBodySpeedsMps[index] : 0.0);
        BodyRows[index]->SetText(FText::FromString(row));
        BodyRows[index]->SetColorAndOpacity(FSlateColor(bSelected && mIsListFocused ? PANEL_HIGHLIGHT
            : (mIsListFocused ? PANEL_TEXT : PANEL_DIM_TEXT)));
    }
    if (BodyRows.IsValidIndex(mListSelection))
    {
        BodyList->ScrollWidgetIntoView(BodyRows[mListSelection], false);
    }
}

//////////////////////////////////////////////////////////////////////////
// Closes the panel through its owner (the pawn, which restores ship input and starts the close); with no owner, plays
// the close itself
void USOLSpeedPanelWidget::RequestClose()
{
    if (mClose.IsBound())
    {
        mClose.Execute();
        return;
    }
    BeginClose();
}
