/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSettingsTabs.h"

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSolidStyle.h"

namespace
{
    // Tab labels in ESOLSettingsTab order (the section headings the tabs replace)
    const TCHAR* const SETTINGS_TAB_LABELS[] = { SOLMenuText::SETTINGS_GRAPHICS, SOLMenuText::SETTINGS_AUDIO,
        SOLMenuText::SETTINGS_MOUSE, SOLMenuText::SETTINGS_GAMEPLAY, SOLMenuText::SETTINGS_OVERLAYS,
        SOLMenuText::SETTINGS_KEYS };
    static_assert(UE_ARRAY_COUNT(SETTINGS_TAB_LABELS) == static_cast<int32>(ESOLSettingsTab::Count),
        "one label per tab");

    // The tab of each slider in ESOLSettingsSlider order
    constexpr ESOLSettingsTab SETTINGS_SLIDER_TABS[] = { ESOLSettingsTab::Audio, ESOLSettingsTab::Audio,
        ESOLSettingsTab::Audio, ESOLSettingsTab::Mouse, ESOLSettingsTab::Gameplay, ESOLSettingsTab::Gameplay,
        ESOLSettingsTab::Gameplay, ESOLSettingsTab::Gameplay };
    static_assert(UE_ARRAY_COUNT(SETTINGS_SLIDER_TABS) == static_cast<int32>(ESOLSettingsSlider::Count),
        "one tab per slider");

    // The tab of each toggle in ESOLSettingsToggle order
    constexpr ESOLSettingsTab SETTINGS_TOGGLE_TABS[] = { ESOLSettingsTab::Graphics, ESOLSettingsTab::Graphics,
        ESOLSettingsTab::Mouse, ESOLSettingsTab::Gameplay, ESOLSettingsTab::Gameplay, ESOLSettingsTab::Gameplay };
    static_assert(UE_ARRAY_COUNT(SETTINGS_TOGGLE_TABS) == static_cast<int32>(ESOLSettingsToggle::Count),
        "one tab per toggle");
}

//////////////////////////////////////////////////////////////////////////
// The tab Settings opens on, every time (Q8: the first tab)
ESOLSettingsTab SOLSettingsTabs::OpeningTab()
{
    return ESOLSettingsTab::Graphics;
}

//////////////////////////////////////////////////////////////////////////
// Whether a tab is one of the six
bool SOLSettingsTabs::IsValidTab(const ESOLSettingsTab tab)
{
    return static_cast<uint8>(tab) < static_cast<uint8>(ESOLSettingsTab::Count);
}

//////////////////////////////////////////////////////////////////////////
// The tab at a column position; Count out of range
ESOLSettingsTab SOLSettingsTabs::TabFromIndex(const int32 index)
{
    return index >= 0 && index < static_cast<int32>(ESOLSettingsTab::Count) ? static_cast<ESOLSettingsTab>(index)
        : ESOLSettingsTab::Count;
}

//////////////////////////////////////////////////////////////////////////
// The tab's label; empty for an invalid tab
const TCHAR* SOLSettingsTabs::TabLabel(const ESOLSettingsTab tab)
{
    return IsValidTab(tab) ? SETTINGS_TAB_LABELS[static_cast<int32>(tab)] : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// The tab of a slider's row; Count for an invalid slider
ESOLSettingsTab SOLSettingsTabs::TabOfSlider(const ESOLSettingsSlider slider)
{
    const int32 index = static_cast<int32>(slider);
    return index < static_cast<int32>(ESOLSettingsSlider::Count) ? SETTINGS_SLIDER_TABS[index] : ESOLSettingsTab::Count;
}

//////////////////////////////////////////////////////////////////////////
// The tab of a toggle's row; Count for an invalid toggle
ESOLSettingsTab SOLSettingsTabs::TabOfToggle(const ESOLSettingsToggle toggle)
{
    const int32 index = static_cast<int32>(toggle);
    return index < static_cast<int32>(ESOLSettingsToggle::Count) ? SETTINGS_TOGGLE_TABS[index] : ESOLSettingsTab::Count;
}

//////////////////////////////////////////////////////////////////////////
// The tab of a stepper's row (every stepper is a Graphics row); Count for an invalid stepper
ESOLSettingsTab SOLSettingsTabs::TabOfStepper(const ESOLSettingsStepper stepper)
{
    return static_cast<int32>(stepper) < static_cast<int32>(ESOLSettingsStepper::Count) ? ESOLSettingsTab::Graphics
        : ESOLSettingsTab::Count;
}

//////////////////////////////////////////////////////////////////////////
// The tab after a click: the request when valid, else the current tab when valid, else the opening tab
ESOLSettingsTab SOLSettingsTabs::SelectTab(const ESOLSettingsTab current, const ESOLSettingsTab requested)
{
    if (IsValidTab(requested))
    {
        return requested;
    }
    return IsValidTab(current) ? current : OpeningTab();
}

//////////////////////////////////////////////////////////////////////////
// The tab ScrollToBinding shows: Key bindings for a valid binding, else the tab stays
ESOLSettingsTab SOLSettingsTabs::TabForBinding(const ESOLSettingsTab current, const int32 binding)
{
    const bool bValid = binding >= 0 && binding < SOLKeyBindings::BINDING_COUNT;
    return SelectTab(current, bValid ? ESOLSettingsTab::KeyBindings : ESOLSettingsTab::Count);
}

//////////////////////////////////////////////////////////////////////////
// The tab column's button variant (the hover trace extends, Q15)
ESOLSolidButtonVariant SOLSettingsTabs::TabButtonVariant()
{
    return ESOLSolidButtonVariant::Tab;
}

//////////////////////////////////////////////////////////////////////////
// The settings rows' button variant (edge and underline, no trace, Q15)
ESOLSolidButtonVariant SOLSettingsTabs::RowButtonVariant()
{
    return ESOLSolidButtonVariant::Small;
}
