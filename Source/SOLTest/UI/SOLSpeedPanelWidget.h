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

class UScrollBox;
class UTextBlock;
class USOLBodyRegistrySubsystem;

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

protected:

    // Builds the widget tree (no widget blueprint) and makes the panel focusable
    virtual void NativeOnInitialized() override;

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
    bool mIsUnitHighlighted = false;                    // True when the highlight is on the unit field
    bool mIsListFocused = false;                        // True when Up/Down/Enter act on the body list
};
