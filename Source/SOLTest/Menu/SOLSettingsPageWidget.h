/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "SOLSettingsPageWidget.generated.h"

class IInputProcessor;
class UCheckBox;
class UScrollBox;
class USlider;
class USOLMenuButton;
class USOLSettingsSubsystem;
class UTextBlock;

// Sliders of the Settings page, in page order
enum class ESOLSettingsSlider : uint8
{
    MasterVolume,
    SfxVolume,
    MusicVolume,
    MouseSensitivity,
    HudScale,
    HudEffects,     // HUD hologram effects strength (SDD 21)
    Count,
};

// Checkboxes of the Settings page, in page order
enum class ESOLSettingsToggle : uint8
{
    Vsync,
    InvertY,
    HudVisible,
    DefaultAssist,
    OrbitLines,
    Count,
};

// Steppers ('<' value '>') of the Settings page, in page order
enum class ESOLSettingsStepper : uint8
{
    Resolution,
    WindowMode,
    Quality,
    Count,
};

/**
 * Settings page (SDD 8, 8d), shared by the main menu and the pause menu: a UMG widget tree built in C++ with graphics
 * (resolution, window mode and quality steppers, vsync), audio (master, effects and music volume sliders), mouse
 * (sensitivity slider, invert Y), gameplay (HUD scale slider, HUD visible, flight assist at launch) and key bindings
 * (one row per SOLKeyBindings action with its pending key and a Rebind button, and Reset keys to defaults), above a
 * footer with the dirty indicator, a status line, Revert and Apply. Every control only calls USOLSettingsSubsystem,
 * which previews audio, mouse and HUD values at once and applies the rest on Apply; the public setters below are the
 * same paths the controls use (the menu smoke script drives them).
 *
 * Rebind: the button puts the page in listening mode; the next key or mouse button is the new key, Esc cancels, and a
 * key that cannot be bound is refused with a message. The key is taken by a Slate input pre-processor (registered while
 * the page is constructed) before any widget sees it, whatever has keyboard focus, and its release is swallowed too, so
 * it can never click a hovered or focused button or reach the menu; menu buttons do not take focus on hover meanwhile
 * (USOLSettingsSubsystem::IsKeyCaptureActive). A key another action already uses is bound anyway and both rows turn the warning colour. The owning menu
 * calls RefreshFromSettings when it shows the page and NotifyHidden when it leaves it (which ends the settings visit:
 * unapplied changes are reverted and the save is written once if an Apply changed something).
 */
UCLASS()
class SOLTEST_API USOLSettingsPageWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // Re-reads every control from the settings subsystem (the page was shown)
    void RefreshFromSettings();

    // The page was hidden: stops listening and ends the settings visit
    void NotifyHidden();

    // Sets a slider's value (snapped to its step) as dragging it would
    void SetSliderValue(ESOLSettingsSlider slider, float value);

    // Sets a checkbox as clicking it would
    void SetToggle(ESOLSettingsToggle toggle, bool bChecked);

    // Steps a stepper back (direction < 0) or forward (direction > 0) as its buttons would
    void Step(ESOLSettingsStepper stepper, int32 direction);

    // Applies the pending settings (Apply button)
    void Apply();

    // Discards the pending settings (Revert button)
    void Revert();

    // Sets every pending key back to its default (Reset keys button)
    void ResetKeys();

    // Starts listening for a binding's new key (its Rebind button)
    void BeginRebind(int32 binding);

    // Takes a key while listening: Esc cancels, anything else is offered to the settings as the new key
    void CaptureKey(const FKey& key);

    // Returns true while waiting for a rebind key
    bool IsListening() const { return mListeningBinding != INDEX_NONE; }

    // Scrolls the page so a binding's key row is in view
    void ScrollToBinding(int32 binding);

    // Returns the status line's text (verification)
    FString GetStatusText() const;

    // Returns a key row's text (verification)
    FString GetKeyRowText(int32 binding) const;

    // A key or mouse press reached Slate's input pre-processor: captured as the new key (true) while listening
    bool HandleCaptureDown(const FKey& key);

    // A key or mouse release reached Slate's input pre-processor: swallowed (true) if it ends the captured press
    bool HandleCaptureUp(const FKey& key);

protected:

    // Builds the widget tree (no widget blueprint)
    virtual void NativeOnInitialized() override;

    // Registers the rebind key capture with Slate while the page is on screen
    virtual void NativeConstruct() override;

    // Stops listening and unregisters the rebind key capture
    virtual void NativeDestruct() override;

    // Refreshes the button highlights (focus follows the keyboard and the mouse)
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:

    // Footer buttons (their indices)
    enum class EAction : uint8
    {
        Revert,
        Apply,
        ResetKeys,
    };

    // Applies one slider value to the pending settings (no slider position change)
    void ApplySliderValue(ESOLSettingsSlider slider, float value);

    // Rewrites every value text, stepper, key row and the status from the settings
    void RefreshTexts();

    // Sets the transient status message (empty clears it) and refreshes the status lines
    void SetStatus(const FString& message);

    // Sets the listening binding (INDEX_NONE stops) and tells the settings whether a key capture is active
    void SetListening(int32 binding);

    // Master volume slider moved
    UFUNCTION()
    void HandleMasterVolumeChanged(float value);

    // Effects volume slider moved
    UFUNCTION()
    void HandleSfxVolumeChanged(float value);

    // Music volume slider moved
    UFUNCTION()
    void HandleMusicVolumeChanged(float value);

    // Mouse sensitivity slider moved
    UFUNCTION()
    void HandleMouseSensitivityChanged(float value);

    // HUD scale slider moved
    UFUNCTION()
    void HandleHudScaleChanged(float value);

    // HUD hologram effects slider moved (SDD 21)
    UFUNCTION()
    void HandleHudEffectsChanged(float value);

    // VSync checkbox clicked
    UFUNCTION()
    void HandleVsyncChanged(bool bChecked);

    // Invert Y checkbox clicked
    UFUNCTION()
    void HandleInvertYChanged(bool bChecked);

    // HUD visible checkbox clicked
    UFUNCTION()
    void HandleHudVisibleChanged(bool bChecked);

    // Flight assist at launch checkbox clicked
    UFUNCTION()
    void HandleDefaultAssistChanged(bool bChecked);

    // Show orbit lines checkbox clicked
    UFUNCTION()
    void HandleOrbitLinesChanged(bool bChecked);

    // A stepper button was clicked (index = stepper * 2 + 0 back / 1 forward)
    void HandleStepClicked(int32 index);

    // A footer button was clicked (index = EAction)
    void HandleActionClicked(int32 action);

    // A Rebind button was clicked (index = binding)
    void HandleRebindClicked(int32 binding);

    UPROPERTY(Transient)
    TArray<TObjectPtr<USlider>> Sliders;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> SliderValueTexts;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UCheckBox>> Toggles;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> StepperValueTexts;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> Buttons;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> KeyTexts;

    UPROPERTY(Transient)
    TObjectPtr<UScrollBox> Scroll;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> DirtyText;

    UPROPERTY(Transient)
    TObjectPtr<UTextBlock> StatusText;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    TSharedPtr<IInputProcessor> mInputProcessor;    // Slate pre-processor taking the rebind key (while constructed)
    FString mStatusMessage;                         // Last rebind or reset message (cleared by the next action)
    FKey mSwallowedRelease;                         // Release of the captured key, still to be swallowed
    int32 mListeningBinding = INDEX_NONE;           // Binding waiting for its new key
};
