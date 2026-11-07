/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Defined in Menu/SOLSettingsPageWidget.h and Menu/SOLSolidStyle.h
enum class ESOLSettingsSlider : uint8;
enum class ESOLSettingsToggle : uint8;
enum class ESOLSettingsStepper : uint8;
enum class ESOLSolidButtonVariant : uint8;

// The Settings tabs, in tab-column order (SDD 46 D13: the six existing sections)
enum class ESOLSettingsTab : uint8
{
    Graphics,
    Audio,
    Mouse,
    Gameplay,
    Overlays,
    KeyBindings,
    Count,
};

/**
 * The Settings page's tab logic (SDD 46 section 6, D13, Q8, Q15), pure and widget-free: which tab shows when the page
 * opens (always the first, no last-tab memory), after a tab click and before ScrollToBinding scrolls, which tab each
 * control's row sits on, the tabs' labels and the button variants of the tab column and of the rows.
 */
namespace SOLSettingsTabs
{
    // The tab Settings opens on, every time it opens (Q8: the first tab, Graphics)
    SOLTEST_API ESOLSettingsTab OpeningTab();

    // Whether a tab is one of the six (Count and anything past it are not)
    SOLTEST_API bool IsValidTab(ESOLSettingsTab tab);

    // The tab of a tab button's index (its position in the column); Count out of range
    SOLTEST_API ESOLSettingsTab TabFromIndex(int32 index);

    // The tab's label, the section heading it replaces (SOLMenuText::SETTINGS_GRAPHICS ... SETTINGS_KEYS); an empty
    // string, never null, for an invalid tab
    SOLTEST_API const TCHAR* TabLabel(ESOLSettingsTab tab);

    // The tab that shows a slider's / toggle's / stepper's row; Count for an invalid control
    SOLTEST_API ESOLSettingsTab TabOfSlider(ESOLSettingsSlider slider);
    SOLTEST_API ESOLSettingsTab TabOfToggle(ESOLSettingsToggle toggle);
    SOLTEST_API ESOLSettingsTab TabOfStepper(ESOLSettingsStepper stepper);

    // The tab that shows after a click on requested while current shows: requested when valid, otherwise current when
    // valid, otherwise OpeningTab()
    SOLTEST_API ESOLSettingsTab SelectTab(ESOLSettingsTab current, ESOLSettingsTab requested);

    // Whether a click on requested while current shows changes the tab: true only when requested is valid and is not
    // current (a re-click on the shown tab is a no-op)
    SOLTEST_API bool IsTabChange(ESOLSettingsTab current, ESOLSettingsTab requested);

    // The tab ScrollToBinding shows before it scrolls: KeyBindings for a binding in [0, SOLKeyBindings::BINDING_COUNT);
    // for any other binding the tab stays (current when valid, otherwise OpeningTab())
    SOLTEST_API ESOLSettingsTab TabForBinding(ESOLSettingsTab current, int32 binding);

    // The button variant of the tab column's buttons (Tab: the hover trace extends, Q15)
    SOLTEST_API ESOLSolidButtonVariant TabButtonVariant();

    // The button variant of the settings rows' buttons: steppers, Rebind, Reset keys and the footer (Small: amber edge
    // and underline, no trace, Q15)
    SOLTEST_API ESOLSolidButtonVariant RowButtonVariant();

    // The width of a tab's row label column (46e follow-up): SOL::SolidMenu::SETTINGS_GRAPHICS_LABEL_WIDTH on Graphics
    // (short labels; the steppers' values need the room at 1000x800), SETTINGS_GAMEPLAY_LABEL_WIDTH on Gameplay (its
    // longest label), SOL::MenuStyle::SETTINGS_LABEL_WIDTH on every other tab, and on an invalid one
    SOLTEST_API float LabelColumnWidth(ESOLSettingsTab tab);

    // (46i) Whether a tab's rows take the narrow layout in a row of rowWidth local units: Key bindings when the key
    // text would keep less than SOL::SolidMenu::SETTINGS_KEY_TEXT_MIN_WIDTH beside the wide column and Rebind,
    // Gameplay when a slider would keep less than SETTINGS_SLIDER_MIN_WIDTH beside the wide column and the value
    // readout; never another tab, and never a row not laid out yet (rowWidth not a positive number)
    SOLTEST_API bool IsNarrowLayout(ESOLSettingsTab tab, float rowWidth);

    // (46i) The width of a tab's label column in a row of rowWidth units: SETTINGS_KEYS_NARROW_LABEL_WIDTH /
    // SETTINGS_GAMEPLAY_NARROW_LABEL_WIDTH when IsNarrowLayout, otherwise LabelColumnWidth(tab)
    SOLTEST_API float LabelColumnWidth(ESOLSettingsTab tab, float rowWidth);

    // (46i) The width of a Rebind button in a row of rowWidth units: SETTINGS_REBIND_NARROW_WIDTH when Key bindings is
    // narrow (the page applies it as a minimum), otherwise SOL::MenuStyle::SETTINGS_REBIND_WIDTH
    SOLTEST_API float RebindWidth(float rowWidth);

    // (33b, D20) The width of a Key bindings row's key cap cell in a row of rowWidth units: what the row leaves beside
    // its label column, BUTTON_GAP and its Rebind width, clamped to [0, SOL::Help::SETTINGS_KEYCAP_CELL_WIDTH]; a row
    // not laid out yet (rowWidth not a positive number) gives SETTINGS_KEYCAP_CELL_WIDTH
    SOLTEST_API float KeyCellWidth(float rowWidth);
}
