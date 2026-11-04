/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLSpeedStepper.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "SOLSpeedPanelWidget.generated.h"

// Applies a speed cap (m/s) to the ship and returns the cap actually applied (the pawn binds it; SDD 21 D4: no include
// of the pawn)
DECLARE_DELEGATE_RetVal_OneParam(double, FSOLSpeedPanelApplyCap, double);

class UBorder;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class USOLBodyRegistrySubsystem;
class USOLHoloFrameWidget;

/**
 * F3 speed-cap panel (SDD 2 section 6), a UMG widget tree built entirely in C++ (no widget blueprint). Two steppers
 * side by side, an odometer VALUE (digits 10^6 .. 10^-2) and a UNIT field (m/s, km/s, c), above a scrollable list of
 * every registry body with its current orbital speed (relative to its parent).
 *
 * Keys while open (the panel has keyboard focus in UI-only input mode, so it consumes every key and nothing reaches
 * the ship): Up/Down or the wheel step the highlighted digit (SOLSpeedStepper::StepDigit, with carry) or, on the unit
 * field, cycle the unit; Left/Right move the highlight (one step right of the hundredths is the unit field); U cycles
 * the unit; Tab moves between the steppers and the body list; in the list Up/Down select a body and Enter sets the cap
 * to its speed. Every digit step and every list Enter applies the cap to the ship at once through the same path
 * (the owner's apply-cap delegate, ASOLShipPawn::ApplySpeedCapMps). Enter on the steppers, the speed-panel key
 * (F3 by default, rebindable; ignored when it was moved onto one of the panel's own keys) or Esc close the panel and give input back to the ship.
 *
 * It is a hologram menu (SDD 42 part 42f): a USOLHoloFrameWidget behind the panel draws the option (c) frame with the
 * faint tint (D20), the title step sized to the title and the stepped broken divider between the stepper section and
 * the body list (SOLHoloFrameBuild::MakeSpeedLayout), and the hover mark on the highlight: brackets with a tick above
 * and below the highlighted digit or unit field, brackets with a pointer line on the highlighted body row (none while
 * that row is scrolled out of the list's view). The content's render opacity and the screen dim follow the frame's
 * fill. It opens with a draw-in and plays the open cue, the hover cue on each highlight move and the select cue on
 * each applied cap (a digit step, a list Enter). BeginClose gives input back at once: the widget turns hit-test
 * invisible and unfocusable, plays the close and removes itself from its NativeTick (SDD 4.5); Open during the close
 * reverses it. The NativeTick does nothing while the panel is open and idle but compare the list's scroll offset and
 * the frame's local units per px.
 */
UCLASS()
class SOLTEST_API USOLSpeedPanelWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Seeds the steppers from the current cap, refreshes the body speeds, remembers how to apply the cap and close the
    // panel (the owner's delegates) and the speed-panel key that also closes it, and shows the title (naming that key)
    void Open(const FSOLSpeedPanelApplyCap& applyCap, const FSimpleDelegate& close, double currentCapMps,
        const FKey& toggleKey, const FString& title);

    // Handles one key press; returns true when the panel used it (verification scripts call it like Slate does)
    bool HandlePanelKey(const FKey& key);

    // Steps the highlighted digit (or the list selection) by wheel notches (positive = up)
    void HandleWheelSteps(int32 steps);

    // Returns the steppers' state
    const FSOLSpeedStepperState& GetStepperState() const { return mStepper; }

    // Returns true while the body list (rather than the steppers) has the highlight
    bool IsListFocused() const { return mIsListFocused; }

    // Returns the highlighted body index in the list
    int32 GetListSelection() const { return mListSelection; }

    // Returns how many key presses arrived through Slate (NativeOnKeyDown), for verification
    int32 GetSlateKeyCount() const { return mSlateKeyCount; }

    // Returns a body's speed as listed when the panel was opened (m/s, relative to its parent), or 0
    double GetListedBodySpeedMps(int32 bodyIndex) const;

    // Starts the non-blocking close (SDD 4.5): hit-test invisible and unfocusable at once, the frame's close plays,
    // then the widget removes itself (at once when nothing is left to draw, e.g. at strength 0)
    void BeginClose();

    // Removes the panel at once without the close (teardown); the next Open draws in again with its open cue
    void Dismiss();

    // Returns the body list's centre in absolute (screen) Slate space as last laid out (verification)
    FVector2D GetListScreenCenter() const;

    // Returns the body list's scroll offset (verification)
    float GetListScrollOffset() const;

    // Returns whether the close is playing or done and the panel not reopened since
    bool IsClosing() const { return mIsClosing; }

    // Returns the hologram frame (verification)
    USOLHoloFrameWidget* GetHoloFrame() const { return HoloFrame; }

    // Returns the content's render opacity (the frame's fill alpha, verification)
    float GetContentOpacity() const { return mContentOpacity; }

    // Returns the item the hover mark is on: a digit's slot (0 = the most significant), the unit field (the digit
    // count) or a body row (the digit count + 1 + its index)
    int32 GetHighlightedItem() const;

protected:

    // Builds the widget tree (no widget blueprint) and makes the panel focusable
    virtual void NativeOnInitialized() override;

    // While the frame animates: advances it, follows its fill, removes the widget once closed; after a content,
    // highlight or list scroll change: lays the frame out from the arranged content. Nothing else while open and idle
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

    // Routes a key press to HandlePanelKey and consumes every key so none reaches the game viewport
    virtual FReply NativeOnKeyDown(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

    // Consumes key releases so they do not reach the game viewport either
    virtual FReply NativeOnKeyUp(const FGeometry& geometry, const FKeyEvent& keyEvent) override;

    // Routes the mouse wheel to HandleWheelSteps
    virtual FReply NativeOnMouseWheel(const FGeometry& geometry, const FPointerEvent& mouseEvent) override;

private:

    // Steps the highlighted digit up or down and applies the resulting cap
    void StepHighlightedDigit(bool bDown);

    // Moves the highlight one place left (more significant) or right (toward the unit field)
    void MoveHighlight(bool bRight);

    // Sets the cap to the highlighted body's listed speed, in that speed's natural unit
    void ApplySelectedBody();

    // Sends a cap to the ship and re-expresses the steppers' value in their current unit
    void ApplyCapMps(double capMps);

    // Reads every body's speed relative to its parent into the reused list buffer
    void RefreshBodySpeeds();

    // Re-renders the digit, unit and cap texts from the stepper state
    void RefreshStepperText();

    // Re-renders the body list rows and keeps the selection in view
    void RefreshListText();

    // Closes the panel through the pawn (which restores ship input)
    void RequestClose();

    // Asks the frame layout to be rebuilt from the arranged content over the next frames
    void MarkLayoutDirty();

    // Sets the frame's layout and hover mark from the content's arranged geometry (frame-local units)
    void UpdateFrameLayout();

    // Sets the frame's hovered item: the highlighted digit, unit field or body row
    void UpdateFrameHover();

    // Returns a body row's slot in the frame's local units from the list's geometry and scroll offset; invalid when
    // the row is scrolled out of the list's view
    FBox2f GetVisibleRowSlot(int32 index) const;

    // Returns the frame's local units per pixel of the 720 px reference canvas (its UI scale over its geometry scale)
    float GetFrameLocalPerPx() const;

    // Sets the content's render opacity and the screen dim's alpha from the frame's fill, when it changed
    void ApplyContentOpacity(float opacity);

    // Opens the built frame (or reverses its close) with the open cue
    void StartFrameOpen();

    // Ends the close: removes the widget
    void FinishClose();

    // Requests the select cue (not while closing; at any strength, D21)
    void RequestSelectCue();

    UPROPERTY(Transient)
    TObjectPtr<UBorder> RootBorder;                     // Full-screen dim, centring the panel

    UPROPERTY(Transient)
    TObjectPtr<UBorder> PanelBorder;                    // The panel's content box (clear over the frame)

    UPROPERTY(Transient)
    TObjectPtr<USOLHoloFrameWidget> HoloFrame;          // The hologram frame behind the panel

    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> StepperSection;            // The steppers, the cap line and the key hint

    UPROPERTY(Transient)
    TObjectPtr<UVerticalBox> ListSection;               // The list header and the body list

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> DigitTexts;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> TitleText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> UnitText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> CapText;

    UPROPERTY(Transient)
    TObjectPtr<UScrollBox> BodyList;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> BodyRows;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    FSOLSpeedPanelApplyCap mApplyCap;                   // Applies the cap to the ship (bound by the pawn)
    FSimpleDelegate mClose;                             // Closes the panel through its owner (the pawn)
    FKey mToggleKey = EKeys::F3;                        // The speed-panel binding's key (closes the panel too)
    TArray<double> mBodySpeedsMps;                      // Listed body speeds (relative to the parent)
    FSOLSpeedStepperState mStepper;                     // Odometer value, unit and highlighted digit
    int32 mListSelection = 0;                           // Highlighted body in the list
    int32 mSlateKeyCount = 0;                           // Key presses received through Slate
    int32 mLayoutDirtyFrames = 0;                       // Frames left to re-lay the frame out after a change
    float mContentOpacity = 1.0f;                       // The content's render opacity as last set
    float mLastScrollOffset = 0.0f;                     // The body list's scroll offset the layout last saw
    float mLastLocalPerPx = 0.0f;                       // The frame's local units per px the layout last saw
    ESlateVisibility mOpenVisibility = ESlateVisibility::Visible;  // The widget's visibility while open
    bool mIsUnitHighlighted = false;                    // True when the highlight is on the unit field
    bool mIsListFocused = false;                        // True when Up/Down/Enter act on the body list
    bool mIsClosing = false;                            // BeginClose ran and Open did not since
    bool mIsAnimating = false;                          // The frame is opening or closing
    bool mIsOpenPending = false;                        // Opened before the frame's Slate widget was built
};
