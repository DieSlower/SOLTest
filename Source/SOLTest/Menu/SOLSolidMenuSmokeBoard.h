/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "SOLSolidMenuSmokeBoard.generated.h"

class UCheckBox;
class USlider;
class USOLMenuButton;
class USOLSolidPanel;

// The comparison board's buttons (FSOLSolidMenuSmoke reads their looks)
enum class ESOLSolidSmokeButton : uint8
{
    StackOn,        // Stack button, enabled
    StackOff,       // ... solid-disabled
    TabA,           // Tab, enabled and selected
    TabOff,         // Tab, solid-disabled (selected: the disabled look ignores it)
    TabB,           // Tab, enabled, not selected (hover-only when focused)
    SmallOn,        // Small button, enabled
    SmallOff,       // ... solid-disabled
    RowOnPrev,      // Settings row stepper '<' / '>', enabled
    RowOnNext,
    RowOffPrev,     // ... disabled
    RowOffNext,
    Count,
};

/**
 * Verification-only comparison board of the solid menu kit (-SOLSmokeSolidMenu, SDD 46 sections 5.3 and 14): a hull
 * panel with its own material (a title step), rivets, vents and a static trace, holding one row per control type with
 * an enabled and a disabled instance side by side: stack button, tab (plus a second, unselected tab), small button, a
 * settings row (label and two steppers), slider and toggle (checked). The disabled buttons use SetSolidDisabled, the
 * disabled slider and toggle Slate's SetIsEnabled(false) with the disabled styles. Its tick refreshes the buttons'
 * highlights and the panel's trace look, as a menu screen does. Built in C++, no widget blueprint.
 */
UCLASS()
class SOLTEST_API USOLSolidMenuSmokeBoard : public UUserWidget
{
    GENERATED_BODY()

public:

    // Returns one of the board's buttons, or null
    USOLMenuButton* GetButton(ESOLSolidSmokeButton button) const;

    // Returns the board's hull panel
    USOLSolidPanel* GetPanel() const { return Panel; }

    // Gives the stack and tab buttons their hover traces in the panel's trace layer (once the layer is built)
    void BindHoverTraces();

protected:

    // Builds the board
    virtual void NativeOnInitialized() override;

    // Refreshes the buttons' highlights and the panel's trace look
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:

    UPROPERTY(Transient)
    TObjectPtr<USOLSolidPanel> Panel;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> Buttons;     // One per ESOLSolidSmokeButton

    UPROPERTY(Transient)
    TArray<TObjectPtr<USlider>> Sliders;            // Enabled, disabled

    UPROPERTY(Transient)
    TArray<TObjectPtr<UCheckBox>> Toggles;          // Enabled, disabled
};
