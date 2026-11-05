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

    // The tab ScrollToBinding shows before it scrolls: KeyBindings for a binding in [0, SOLKeyBindings::BINDING_COUNT);
    // for any other binding the tab stays (current when valid, otherwise OpeningTab())
    SOLTEST_API ESOLSettingsTab TabForBinding(ESOLSettingsTab current, int32 binding);

    // The button variant of the tab column's buttons (Tab: the hover trace extends, Q15)
    SOLTEST_API ESOLSolidButtonVariant TabButtonVariant();

    // The button variant of the settings rows' buttons: steppers, Rebind, Reset keys and the footer (Small: amber edge
    // and underline, no trace, Q15)
    SOLTEST_API ESOLSolidButtonVariant RowButtonVariant();
}
