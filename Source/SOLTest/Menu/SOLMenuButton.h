/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidStyle.h"
#include "Menu/SOLSolidTraceLayer.h"

#include "Components/Button.h"
#include "CoreMinimal.h"

#include "SOLMenuButton.generated.h"

class UTextBlock;
class UWidgetTree;

// Fired when a menu button is clicked (mouse, or Enter / Space while it has keyboard focus), with its index
DECLARE_DELEGATE_OneParam(FSOLMenuButtonClicked, int32 /*index*/);

/**
 * A solid menu button's animated look (SDD 46 sections 5.3 and 8.2), pure and headless: the state it shows, the look
 * at its last change and the press linger. Update moves toward a target state with SOLSolidMenu::EvaluateButtonFrom:
 * a change of target starts a ramp from the look shown at that moment (no pop on a mid-ramp reversal); a press shows
 * Pressed at once and keeps it until PRESSED_LINGER_S after the press even when the target leaves Pressed sooner (the
 * ramp to the new target then starts at the linger's end); bSelected is orthogonal (the cyan mark, 46c amendment).
 * Non-finite times read as no time passed.
 */
struct SOLTEST_API FSOLSolidButtonAnim
{
    // Returns the state a button shows from its flags: Disabled, else Pressed, else Hovered (mouse hover takes keyboard
    // focus, so focus is the hover) unless bHoverElsewhere (the mouse is over another control that does not take the
    // focus, a slider, toggle or list row: hover wins, so one highlight shows), else Normal; selection is not a state
    // here (it is the orthogonal bSelected)
    static ESOLSolidButtonState ResolveState(bool bDisabled, bool bPressed, bool bFocused,
        bool bHoverElsewhere = false);

    // Returns whether two looks differ on screen: their 8-bit brush tints (SOLSolidStyle::BrushTint) or their lifts
    // (SOLSolidMenu::LiftOffset, by more than SOL::SolidMenu::LIFT_EPSILON_PX)
    static bool DiffersOnScreen(const FSOLSolidButtonLook& a, const FSOLSolidButtonLook& b);

    // Moves toward target at nowS (s the effects strength) and returns the look to show
    FSOLSolidButtonLook Update(ESOLSolidButtonState target, bool bSelected, double nowS, float s);

    // Returns whether the look no longer changes without a new target (no ramp or linger running at nowS)
    bool IsSettled(double nowS, float s) const;

    // Returns the state shown (Pressed during the linger)
    ESOLSolidButtonState GetShownState() const { return mShown; }

    // Returns the last look Update returned
    const FSOLSolidButtonLook& GetLook() const { return mLook; }

private:

    FSOLSolidButtonLook mLook;              // Last look returned
    FSOLSolidButtonLook mAtChange;          // Look shown when mShown last changed
    double mChangeS = 0.0;                  // When mShown last changed
    double mPressS = 0.0;                   // When the last press began
    double mTargetS = 0.0;                  // When the target last changed (a release ends the linger from here)
    ESOLSolidButtonState mShown = ESOLSolidButtonState::Normal;
    ESOLSolidButtonState mTarget = ESOLSolidButtonState::Normal;
    bool mIsSelected = false;
    bool mHasUpdated = false;
};

/**
 * Menu button (SDD 8, 8c; restyled by SDD 46 section 4.2): a UButton with a solid hull variant (stack, tab or small),
 * a text label and an index, whose clicks go to one native delegate so a menu can route a whole list of buttons
 * through one handler. Hovering it with the mouse gives it keyboard focus (except while a Settings page listens for a
 * rebind key, or while it is solid-disabled, and only on a real mouse move: a list scrolled under a still cursor does
 * not take the focus, USOLSolidMenuSubsystem::HasRealMouseMoveThisFrame), so the mouse and the keyboard share one
 * highlight. The owning menu reads
 * the frame once per tick (USOLSolidMenuSubsystem::BeginFrame) and calls RefreshHighlightAt on each of its buttons: it
 * resolves the state (disabled, pressed, focused) and the orthogonal selected flag, advances the FSOLSolidButtonAnim
 * and, only when the look differs on screen from the one last written, sets the background colour to the look's brush
 * tint (SOLSolidStyle::BrushTint; the flat colour without the hull material), the render-transform lift and the label
 * colour (SOLSolidStyle::LabelColor), and feeds its hover trace (Stack and Tab, Q15). A settled button costs a state
 * compare per tick. Disabled art (SetSolidDisabled) never uses SetIsEnabled(false): Slate's disabled effect would
 * multiply the hull and the label alpha by 0.45 on top of the tint's DISABLED_ALPHA; the button instead becomes
 * hit-test invisible and not focusable, refuses hover focus and swallows clicks. (46g, Q11) The #42 cues: the hover cue
 * when the button gains the keyboard focus (RefreshHighlightAt; not on a press's release), the select cue on a click
 * (USOLSolidMenuSubsystem::PlayCue).
 */
UCLASS()
class SOLTEST_API USOLMenuButton : public UButton
{
    GENERATED_BODY()

public:

    // Styles the button as a solid variant (SOLSolidStyle::MakeButtonStyle), creates its label in the owner's widget
    // tree (the menu font at fontSize) and routes clicks and hovers
    void Setup(UWidgetTree& tree, const FString& label, int32 index, int32 fontSize,
        ESOLSolidButtonVariant variant = ESOLSolidButtonVariant::Stack);

    // Replaces the label text
    void SetLabel(const FString& label);

    // Replaces the label's letter spacing (Setup gives Stack and Tab LETTER_SPACING_STACK, Small none); the Settings
    // tab column uses a tighter one so its longer labels fit (SDD 46 section 6.3)
    void SetLabelLetterSpacing(int32 spacing);

    // Marks the button as the selected entry (active tab, page or start body): the cyan underline and LED, orthogonal
    // to hover, focus and press (a selected button that is hovered shows the amber hover look and the cyan mark)
    void SetSelected(bool bSelected) { mIsSelected = bSelected; }

    // Returns whether the button is the selected entry
    bool IsSelected() const { return mIsSelected; }

    // Gives or removes the disabled art (desaturated hull, no grain or edge highlight, LED off, no hover, the disabled
    // cue, DISABLED_TEXT_COLOR label) and blocks its input without Slate's disabled effect: hit-test invisible and not
    // focusable (keyboard navigation skips it) until enabled again, when its focusability is restored; a disabled
    // button that has keyboard focus keeps it until the owner moves it
    void SetSolidDisabled(bool bDisabled);

    // Returns whether the button has the disabled art and blocks input
    bool IsSolidDisabled() const { return mIsSolidDisabled; }

    // Returns the button's variant
    ESOLSolidButtonVariant GetVariant() const { return mVariant; }

    // Returns the button's index
    int32 GetIndex() const { return mIndex; }

    // Sets the panel trace layer and the polyline (layer local units) the hover extends; Stack and Tab only (Q15),
    // ignored for Small. A new layer gets the current look's trace at once (a settled button writes nothing later)
    void SetHoverTrace(const TSharedPtr<SSOLSolidTraceLayer>& layer, const FSOLSolidTrace& trace);

    // Advances the look for keyboard focus, press, selection and disabled at nowS (real application seconds) and the
    // effects strength s (the owner's USOLSolidMenuSubsystem::BeginFrame), and writes it only when it differs on screen
    // from the look last written (a state compare when settled); bHoverElsewhere: the owner's mouse is over another
    // control that keeps no focus (a slider, toggle or list row), so the focus shows no hover (ResolveState). Plays
    // the hover cue when the keyboard focus has arrived since the last call (not while solid-disabled)
    void RefreshHighlightAt(double nowS, float s, bool bHoverElsewhere = false);

    // RefreshHighlightAt with the state given instead of resolved from the button's focus and press, and no cue
    // (verification: drives hover and press headless, where no Slate widget has focus or a press)
    void RefreshHighlightFor(ESOLSolidButtonState state, double nowS, float s);

    // Returns whether the button had the keyboard focus at its last RefreshHighlightAt (a scrolling stack follows it
    // without querying the focus again)
    bool WasFocusedAtRefresh() const { return mIsFocusedAtRefresh; }

    // Returns the look of the last refresh (verification: the smoke compares it with SOLSolidMenu::EvaluateButton)
    const FSOLSolidButtonLook& GetAppliedLook() const { return mAppliedLook; }

    // Returns how many Slate writes (background colour, lift, label colour) the button made since Setup (verification:
    // a settled refresh makes none)
    int32 GetWriteCount() const { return mWriteCount; }

    // Returns the hull tint last applied, as the material (or the flat fallback) receives it: the button's background
    // colour times its style brush's tint (SButton multiplies them), SOLSolidStyle::BrushTint of the look
    FLinearColor GetAppliedTint() const { return mAppliedTint; }

    // Returns the label colour last applied (SOLSolidStyle::LabelColor of the look)
    FLinearColor GetAppliedLabelColor() const { return mAppliedLabelColor; }

    // Returns whether the button draws with the hull material (false: the flat fallback)
    bool IsHullMaterialDrawn() const { return mIsMaterialDrawn; }

    // Takes keyboard focus as a mouse hover does, unless a Settings page is listening for a rebind key or the button
    // is solid-disabled (the hover handler calls it; verification scripts call it to simulate a hover)
    void NotifyHovered();

    // Clicks, with the button's index
    FSOLMenuButtonClicked OnMenuClicked;

private:

    // UButton click: plays the select cue and forwards the index to OnMenuClicked (neither while solid-disabled)
    UFUNCTION()
    void HandleClicked();

    // UButton hover: takes keyboard focus so the highlight follows the mouse, only on a real mouse move
    UFUNCTION()
    void HandleHovered();

    // Applies a look: the hull tint through the background colour, the lift, the label colour and the hover trace
    void ApplyLook(const FSOLSolidButtonLook& look);

    // Feeds the panel's trace layer this button's hover trace (Stack and Tab only)
    void UpdateHoverTrace(const FSOLSolidButtonLook& look);

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> Label;

    FSOLSolidButtonAnim mAnim;
    FSOLSolidButtonLook mAppliedLook;                  // Look of the last refresh
    FSOLSolidButtonLook mWrittenLook;                  // Look on screen: last written tint and label, the lift as
                                                       // last set (the on-screen compare's base)
    FLinearColor mAppliedTint = FLinearColor::White;   // Hull tint last written
    FLinearColor mAppliedLabelColor = FLinearColor::White;
    FLinearColor mStyleTint = FLinearColor::White;     // The style brushes' tint, divided out of the background colour
    FSOLSolidTrace mHoverTrace;
    TWeakPtr<SSOLSolidTraceLayer> mTraceLayer;
    ESOLSolidButtonState mAppliedState = ESOLSolidButtonState::Normal;  // Target of the last refresh
    bool mIsAppliedSelected = false;                   // Selection of the last refresh
    float mAppliedStrength = -1.0f;                    // Strength of the last refresh
    int32 mWriteCount = 0;                             // Slate writes since Setup (verification)
    bool mIsAppliedSettled = false;                    // The last refresh's look was the settled one
    bool mIsFocusedAtRefresh = false;                  // Keyboard focus at the last RefreshHighlightAt (hover cue)
    bool mIsMaterialDrawn = false;
    bool mIsFocusableWhenEnabled = true;               // Focusability restored when the disabled art is removed
    int32 mIndex = INDEX_NONE;          // Index handed to OnMenuClicked
    ESOLSolidButtonVariant mVariant = ESOLSolidButtonVariant::Stack;
    bool mIsSelected = false;           // Selected entry of its menu
    bool mIsSolidDisabled = false;      // Disabled art, input blocked
    bool mHasApplied = false;           // A look was applied since Setup
};
