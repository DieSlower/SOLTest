/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "Menu/SOLMenuState.h"

#include "SOLDemoPageWidget.generated.h"

class UScrollBox;
class USOLMenuButton;
class USOLMenuSubsystem;
class UTextBlock;

/**
 * Demo page (SDD 11 section 3.8, Part 10e), shared by the main menu and the pause menu's Demo tab: a UMG widget tree
 * built in C++ with the three presets (Showcase, Benchmark, Ramp), one '<' value '>' stepper per option (every
 * SOLNpcScale::EDemoOption, then the combat model, seed and stress-ramp rows of SOLMenuState's Demo page model) in
 * sections, the cross-option warnings (SOLNpcScale::SanitizeOptions) and a footer with Reset and Start demo (main
 * menu) or Stop NPCs and Apply (pause menu). The options live in USOLMenuSubsystem (session-only); every control edits
 * them through the pure SOLMenuState functions, and the public methods below are the same paths the buttons use (the
 * menu smoke script drives them).
 */
UCLASS()
class SOLTEST_API USOLDemoPageWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Sets the page's mode: in flight (pause menu: Stop NPCs and Apply) or not (main menu: Start demo)
    void SetInFlight(bool bInFlight);

    // Re-reads every value from the menu subsystem (the page was shown)
    void RefreshFromMenu();

    // Steps a row back (direction < 0) or forward (direction > 0) as its buttons would
    void Step(int32 row, int32 direction);

    // Applies a preset as its button would
    void ApplyPreset(SOLMenuState::EDemoPreset preset);

    // Puts every option back to its default (Reset button)
    void Reset();

    // Starts the demo (Start demo / Apply button)
    void Start();

    // Returns a row's value text (verification)
    FString GetValueText(int32 row) const;

    // Returns the warnings text (verification)
    FString GetWarningsText() const;

protected:

    // Builds the widget tree (no widget blueprint)
    virtual void NativeOnInitialized() override;

    // Refreshes the button highlights (focus follows the keyboard and the mouse)
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:

    // Footer and preset buttons (their indices start after the stepper buttons)
    enum class EAction : uint8
    {
        PresetShowcase,
        PresetBenchmark,
        PresetRamp,
        Reset,
        Stop,
        Start,
        Count
    };

    // Rewrites every value text and the warnings from the menu subsystem's Demo page
    void RefreshTexts();

    // Stores an edited page in the menu subsystem and refreshes the texts
    void SetPage(const SOLMenuState::FDemoPage& page);

    // A stepper button was clicked (index = row * 2 + 0 back / 1 forward)
    void HandleStepClicked(int32 index);

    // A preset or footer button was clicked (index = stepper button count + EAction)
    void HandleActionClicked(int32 index);

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> ValueTexts;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> Buttons;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuButton> StopButton;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuButton> StartButton;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> HintText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> WarningsText;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    bool mIsInFlight = false;    // Pause menu tab (Apply, Stop NPCs) rather than the main menu page (Start demo)
};
