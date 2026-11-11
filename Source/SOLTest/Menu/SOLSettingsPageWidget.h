/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLSettingsTabs.h"
#include "Menu/SOLSolidTabRail.h"
#include "Menu/SOLSolidTraceLayer.h"

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"
#include "InputCoreTypes.h"

#include "SOLSettingsPageWidget.generated.h"

class IInputProcessor;
class UCheckBox;
class UHorizontalBox;
class UScrollBox;
class USizeBox;
class USOLHudGlowSample;
class USOLKeyCapRow;
class USOLMenuButton;
class USOLOverlayMenuWidget;
class USOLSettingsSubsystem;
class USOLSolidPanel;
class USOLSolidSlider;
class UTextBlock;
class UWidgetSwitcher;

// Sliders of the Settings page, in page order
enum class ESOLSettingsSlider : uint8
{
    MasterVolume,
    SfxVolume,
    MusicVolume,
    MouseSensitivity,
    HudScale,
    HudEffects,         // HUD hologram effects strength (SDD 21)
    HudGlow,            // HUD glow, 0-200% (SDD 37 D2); under the HUD effects slider (D5)
    HoloMenuEffects,    // Hologram menu effects strength (SDD 42 section 8); under the HUD glow slider
    ChaseSafetyMargin,  // Chase Down meter floor, a percent of a full meter (SDD 32); after Mouse steering at start
    Count,
};

// Checkboxes of the Settings page, in page order
enum class ESOLSettingsToggle : uint8
{
    Vsync,
    SpaceDust,          // Space dust speed cues, global (SDD 34 section 9); Graphics, after VSync
    InvertY,
    HudVisible,
    DefaultAssist,
    MouseSteerAtStart,  // Mouse steering toggle on at flight start (SDD 19 Amendment 5)
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
 * (sensitivity slider, invert Y), gameplay (HUD scale, HUD hologram effects and hologram menu effects sliders, HUD
 * visible, flight assist at launch, the Chase Down safety margin slider) and key bindings (one row per SOLKeyBindings
 * action with its pending key as key caps (33b, D20) and a Rebind button, and Reset keys to defaults), above a footer
 * with the dirty indicator, a status line, Revert and Apply. The solid look (SDD 46 section 6.3): the six
 * sections are six tabs in a vertical tab column (a tab rail recess, Tab buttons whose hover extends a trace into the
 * rail; the shown tab carries the selected mark) left of a content recess showing the chosen tab's rows in its own
 * scroll box (USOLSolidSlider sliders with the cyan fill, SOLSolidStyle toggles and Small buttons: amber edge and
 * underline on hover, no trace); the footer stays under the content on every tab. RefreshFromSettings shows the first
 * tab (Graphics) with every tab scrolled to its top every time the page opens (no last-tab or scroll memory, Q8);
 * ScrollToBinding first shows the Key bindings tab. Narrow views (46i, 1000x800): the Key bindings and Gameplay tabs
 * narrow their label column (the labels wrap, a wrapped label grows its row) and Key bindings its Rebind buttons, so
 * the key names and the sliders keep their room (FitNarrowRows); wide views keep the wide columns. One highlight (46e
 * follow-up): while the mouse is over a slider, a toggle or an embedded Overlay row (controls that take no focus on
 * hover) and the last input was a real mouse move, the focused button shows no hover look (the owning menus pass the
 * same rule to their own buttons); a key press gives the focus its highlight back until the mouse moves again (last
 * input wins). Every control only calls
 * USOLSettingsSubsystem, which previews audio, mouse and HUD values at once and applies the rest on Apply; the public
 * setters below are the same paths the controls use (the menu smoke script drives them).
 *
 * Rebind: the button puts the page in listening mode; the next key or mouse button is the new key, Esc cancels, and a
 * key that cannot be bound is refused with a message. The key is taken by a Slate input pre-processor (registered while
 * the page is constructed) before any widget sees it, whatever has keyboard focus, and its release is swallowed too, so
 * it can never click a hovered or focused button or reach the menu; menu buttons do not take focus on hover meanwhile
 * (USOLSettingsSubsystem::IsKeyCaptureActive). A key another action already uses is bound anyway and both rows turn the
 * warning colour. The owning menu calls RefreshFromSettings when it shows the page and NotifyHidden when it leaves it
 * (which ends the settings visit: unapplied changes are reverted and the save is written once if an Apply changed
 * something).
 */
UCLASS()
class SOLTEST_API USOLSettingsPageWidget : public UUserWidget
{
    GENERATED_BODY()

public:

    // The page was shown: shows the opening tab (Q8) and re-reads every control from the settings subsystem
    void RefreshFromSettings();

    // Shows a tab as a click on its tab button does (SOLSettingsTabs::SelectTab: an invalid tab keeps the current one)
    void SelectTab(ESOLSettingsTab tab);

    // Returns the tab showing
    ESOLSettingsTab GetSelectedTab() const { return mTab; }

    // Returns the content switcher's shown index (verification: equals the selected tab), or INDEX_NONE
    int32 GetShownContentIndex() const;

    // Returns a tab's button in the tab column, or null (verification)
    USOLMenuButton* GetTabButton(ESOLSettingsTab tab) const;

    // Returns the tab rail (its scroll state and ScrollToEnd; verification)
    FSOLSolidTabRail& GetRail() { return Rail; }

    // Returns the HUD glow sample under the HUD glow row (SDD 37 D16), or null (verification)
    USOLHudGlowSample* GetGlowSample() const { return GlowSample; }

    // Returns a tab's scroll box, or null for an invalid tab (verification)
    const UScrollBox* GetTabScroll(ESOLSettingsTab tab) const;

    // Returns a stepper's back (side 0) or forward (side 1) button, or null (verification)
    USOLMenuButton* GetStepperButton(ESOLSettingsStepper stepper, int32 side) const;

    // Returns the Revert and Apply buttons, or null (verification)
    USOLMenuButton* GetRevertButton() const { return RevertButton; }
    USOLMenuButton* GetApplyButton() const { return ApplyButton; }

    // Returns a slider or a checkbox, or null (verification)
    const USOLSolidSlider* GetSlider(ESOLSettingsSlider slider) const;
    const UCheckBox* GetToggle(ESOLSettingsToggle toggle) const;

    // Returns whether the tab rail draws a tab's hover trace (verification)
    bool IsTabHoverTraceShown(ESOLSettingsTab tab) const;

    // Returns whether the tab rail draws any hover trace (verification: a row's hover draws none, Q15)
    bool IsAnyTabHoverTraceShown() const;

    // Returns whether a binding's key row lies inside the Key bindings tab's scroll box as last laid out (verification)
    bool IsBindingRowInView(int32 binding) const;

    // Returns a tab's scroll offset (local units; 0 at the top), or -1 for an invalid tab (verification)
    float GetTabScrollOffset(ESOLSettingsTab tab) const;

    // Returns whether the last tick found the mouse over a slider, a toggle or an embedded Overlay row, so no focused
    // button showed its hover look (verification)
    bool IsHoverElsewhere() const { return mIsHoverElsewhere; }

    // Returns whether hover wins over the keyboard focus now: the last input was a real mouse move (not a key) and
    // the mouse is over a slider, a toggle or an embedded Overlay tab or row of the shown tab, controls that take no
    // focus on hover (the owning menus pass it to their own buttons while the page shows)
    bool IsHoverOnFocuslessControl() const;

    // A real mouse move (a non-zero cursor delta) or a mouse press reached Slate: hover wins again
    void NotifyMouseInput() { mIsMouseLastInput = true; }

    // A key press reached Slate: the keyboard focus shows its highlight until the mouse moves again
    void NotifyKeyInput() { mIsMouseLastInput = false; }

    // Returns a stepper's value text, or null (verification)
    const UTextBlock* GetStepperValueText(ESOLSettingsStepper stepper) const;

    // Returns how many rows have a label (steppers, toggles, sliders and key rows, in build order) (verification)
    int32 GetRowLabelCount() const { return RowLabels.Num(); }

    // Returns a row's label and its tab, or null for an invalid index (verification)
    const UTextBlock* GetRowLabel(int32 index, ESOLSettingsTab& outTab) const;

    // (46i) Returns a row's label column box (RowLabels order), or null for an invalid index (verification: the
    // applied column width)
    const USizeBox* GetRowLabelBox(int32 index) const
    {
        return RowLabelBoxes.IsValidIndex(index) ? RowLabelBoxes[index].Get() : nullptr;
    }

    // (46i) Returns a row's row box (RowLabels order), or null for an invalid index (verification: a wrapped label
    // inside its row, the fixed or growing height)
    const USizeBox* GetRowBox(int32 index) const
    {
        return RowBoxes.IsValidIndex(index) ? RowBoxes[index].Get() : nullptr;
    }

    // (46i) Returns a key row's Rebind box (its width), or null for an invalid binding (verification)
    const USizeBox* GetRebindBox(int32 binding) const
    {
        return RebindBoxes.IsValidIndex(binding) ? RebindBoxes[binding].Get() : nullptr;
    }

    // (46i) Returns a key row's Rebind button (its desired width), or null for an invalid binding (verification)
    const USOLMenuButton* GetRebindButton(int32 binding) const;

    // (46i) Returns whether a tab's rows have the narrow layout as last fitted (SOLSettingsTabs::IsNarrowLayout)
    bool IsNarrowLayout(ESOLSettingsTab tab) const
    {
        return SOLSettingsTabs::IsValidTab(tab) && mIsNarrowTab[static_cast<int32>(tab)];
    }

    // (46i) Returns the row width (local units) the rows were last fitted to: the content switcher's width less a
    // scroll bar's room; 0 before the first layout (verification)
    float GetFitRowWidth() const { return mFitRowWidth; }

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

    // Shows the Key bindings tab and scrolls it so a binding's key row is in view (an invalid binding changes nothing)
    void ScrollToBinding(int32 binding);

    // Returns the status line's text (verification)
    FString GetStatusText() const;

    // Returns a key row's text (verification)
    FString GetKeyRowText(int32 binding) const;

    // (33b) Returns a key row's key cap cell, or null for an invalid binding (verification: its caps against its slot)
    const USOLKeyCapRow* GetKeyCell(int32 binding) const
    {
        return KeyCells.IsValidIndex(binding) ? KeyCells[binding].Get() : nullptr;
    }

    // (33b) Returns the width the key cap cells wrap in as last fitted (SOLSettingsTabs::KeyCellWidth; verification)
    float GetKeyCellWidth() const { return mKeyCellWidth; }

    // Returns the embedded Overlay Menu list, or null (verification)
    USOLOverlayMenuWidget* GetOverlayList() const { return OverlayList; }

    // A key or mouse press reached Slate's input pre-processor: captured as the new key (true) while listening
    bool HandleCaptureDown(const FKey& key);

    // A key or mouse release reached Slate's input pre-processor: swallowed (true) if it ends the captured press
    bool HandleCaptureUp(const FKey& key);

protected:

    // Builds the widget tree (no widget blueprint)
    virtual void NativeOnInitialized() override;

    // Registers the rebind key capture with Slate while the page is on screen and gives the tabs their hover traces
    virtual void NativeConstruct() override;

    // Stops listening and unregisters the rebind key capture
    virtual void NativeDestruct() override;

    // Refreshes the button highlights (focus follows the keyboard and the mouse) and fits the narrow rows (46i)
    virtual void NativeTick(const FGeometry& geometry, float deltaSeconds) override;

private:

    // Footer buttons (their indices)
    enum class EAction : uint8
    {
        Revert,
        Apply,
        ResetKeys,
    };

    // Builds the tab rail (the shared FSOLSolidTabRail): one Tab button per tab; returns the column widget
    USizeBox* BuildTabRail();

    // Creates a row of a tab with its label in the tab's label column (SOLSettingsTabs::LabelColumnWidth), ready for
    // the control(s)
    UHorizontalBox* MakeRow(ESOLSettingsTab tab, const FString& label);

    // Adds a row made by MakeRow to its tab's scroll box in a box SETTINGS_ROW_HEIGHT tall, with the row gap below it
    // (the scroll bar keeps its own gap, so a tab without a bar gives its rows the full width)
    void AddRow(ESOLSettingsTab tab, UHorizontalBox* row);

    // (46i) Fits the Key bindings and Gameplay rows to the rows' laid-out width (SOLSettingsTabs::IsNarrowLayout),
    // only when a tab's narrow state changes: the label column's width, the labels' wrap, the row boxes' height (fixed
    // when wide, at least SETTINGS_ROW_HEIGHT when narrow so a wrapped label grows its row) and the Rebind boxes' width
    // (fixed when wide, a minimum when narrow so the button's label is never cut)
    void FitNarrowRows();

    // Re-reads every control from the settings subsystem (the tab is left as it is)
    void RefreshControls();

    // Refreshes the highlights of the shown tab's buttons, the footer's and any hidden button not yet at rest
    // (USOLMenuButton::IsAtRest): the 38 Rebind buttons cost nothing while another tab shows
    void RefreshButtons(const FSOLSolidMenuFrame& frame);

    // A tab button was clicked (index = the tab's column position)
    void HandleTabClicked(int32 index);

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

    // HUD glow slider moved (SDD 37)
    UFUNCTION()
    void HandleHudGlowChanged(float value);

    // Hologram menu effects slider moved (SDD 42)
    UFUNCTION()
    void HandleHoloMenuEffectsChanged(float value);

    // Chase Down safety margin slider moved (SDD 32)
    UFUNCTION()
    void HandleChaseSafetyMarginChanged(float value);

    // VSync checkbox clicked
    UFUNCTION()
    void HandleVsyncChanged(bool bChecked);

    // Space dust checkbox clicked
    UFUNCTION()
    void HandleSpaceDustChanged(bool bChecked);

    // Invert Y checkbox clicked
    UFUNCTION()
    void HandleInvertYChanged(bool bChecked);

    // HUD visible checkbox clicked
    UFUNCTION()
    void HandleHudVisibleChanged(bool bChecked);

    // Flight assist at launch checkbox clicked
    UFUNCTION()
    void HandleDefaultAssistChanged(bool bChecked);

    // Mouse steering at start checkbox clicked
    UFUNCTION()
    void HandleMouseSteerAtStartChanged(bool bChecked);

    // A stepper button was clicked (index = stepper * 2 + 0 back / 1 forward)
    void HandleStepClicked(int32 index);

    // A footer button was clicked (index = EAction)
    void HandleActionClicked(int32 action);

    // A Rebind button was clicked (index = binding)
    void HandleRebindClicked(int32 binding);

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLSolidSlider>> Sliders;

    UPROPERTY(Transient)
    TObjectPtr<USOLOverlayMenuWidget> OverlayList;          // The Overlay Menu's list, embedded (SDD 22)

    UPROPERTY(Transient)
    TObjectPtr<USOLHudGlowSample> GlowSample;               // The HUD glow sample under its slider (SDD 37 D16)

    UPROPERTY(Transient)
    TObjectPtr<USizeBox> GlowSampleIndent;                  // Its indent, the Gameplay label column's width

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> SliderValueTexts;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UCheckBox>> Toggles;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> StepperValueTexts;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLMenuButton>> Buttons;

    UPROPERTY(Transient)
    TArray<TObjectPtr<USOLKeyCapRow>> KeyCells;             // Each key row's key caps, in binding order (33b)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UTextBlock>> RowLabels;               // Every row's label, in build order (verification)

    UPROPERTY(Transient)
    TArray<TObjectPtr<USizeBox>> RowLabelBoxes;             // Each RowLabels entry's column box (46i)

    UPROPERTY(Transient)
    TArray<TObjectPtr<USizeBox>> RowBoxes;                  // Each RowLabels entry's row box, its height (46i)

    UPROPERTY(Transient)
    TArray<TObjectPtr<USizeBox>> RebindBoxes;               // Each key row's Rebind box, in binding order (46i)

    UPROPERTY(Transient)
    FSOLSolidTabRail Rail;                                  // The tab column (46f: the shared kit piece)

    UPROPERTY(Transient)
    TArray<TObjectPtr<UScrollBox>> TabScrolls;              // Each tab's rows, in ESOLSettingsTab order

    UPROPERTY(Transient)
    TObjectPtr<UWidgetSwitcher> TabSwitcher;                // Shows the selected tab's scroll box

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuButton> RevertButton;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuButton> ApplyButton;

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
    TArray<ESOLSettingsTab> mRowLabelTabs;          // The tab of each RowLabels entry
    TArray<ESOLSettingsTab> mButtonTabs;            // The tab of each Buttons entry (Count: the footer, every tab)
    bool mIsHoverElsewhere = false;                 // Last tick: the mouse over a focusless control (one highlight)
    bool mIsMouseLastInput = true;                  // The last input was a real mouse move or press, not a key
    bool mIsNarrowTab[static_cast<int32>(ESOLSettingsTab::Count)] = {};    // Each tab's narrow layout, as fitted
    float mFitRowWidth = 0.0f;                      // The row width last fitted to (0 before the first layout)
    float mKeyCellWidth = -1.0f;                    // The key cap cells' wrap width last set (33b; -1 before)
    ESOLSettingsTab mTab = ESOLSettingsTab::Graphics;   // The tab showing
};
