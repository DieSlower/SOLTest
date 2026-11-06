/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSettingsPageWidget.h"
#include "Menu/SOLSettingsTabs.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #46 Part 46e Settings tab column (Docs/SDDs/46-solid-menus.md section 6 Settings row, D13, Q8,
// Q15), written against the spec before the production code existed. They pin the pure, widget-free logic the
// Settings page uses to pick and show its tabs; the widget tree, the solid sliders / toggles / steppers, the shared
// footer and the embedded Overlay list are checked by the menu smokes (-SOLSmokeSolidMenu, -SOLMenuSmoke).
//
// Contract under test (new header Menu/SOLSettingsTabs.h, implemented in Menu/SOLSettingsTabs.cpp; the slider,
// toggle, stepper and button variant enums may be opaque-declared there, they are defined in SOLSettingsPageWidget.h
// and SOLSolidStyle.h):
//
//   // The Settings tabs, in tab-column order (D13: the six existing sections)
//   enum class ESOLSettingsTab : uint8 { Graphics, Audio, Mouse, Gameplay, Overlays, KeyBindings, Count };
//
//   namespace SOLSettingsTabs
//   {
//       // The tab Settings opens on, every time it opens (Q8: the first tab, Graphics; no last-tab memory)
//       SOLTEST_API ESOLSettingsTab OpeningTab();
//       // Whether a tab is one of the six (Count and anything past it are not)
//       SOLTEST_API bool IsValidTab(ESOLSettingsTab tab);
//       // The tab of a tab button's index (its click index, the tab's position in the column); Count out of range
//       SOLTEST_API ESOLSettingsTab TabFromIndex(int32 index);
//       // The tab's label: the section heading it replaces (SOLMenuText::SETTINGS_GRAPHICS ... SETTINGS_KEYS);
//       // an empty string, never null, for an invalid tab
//       SOLTEST_API const TCHAR* TabLabel(ESOLSettingsTab tab);
//       // The tab that shows a slider's / toggle's / stepper's row (its section today); Count for an invalid control
//       SOLTEST_API ESOLSettingsTab TabOfSlider(ESOLSettingsSlider slider);
//       SOLTEST_API ESOLSettingsTab TabOfToggle(ESOLSettingsToggle toggle);
//       SOLTEST_API ESOLSettingsTab TabOfStepper(ESOLSettingsStepper stepper);
//       // The tab that shows after a click on requested while current shows: requested when valid, otherwise current
//       // when valid, otherwise OpeningTab()
//       SOLTEST_API ESOLSettingsTab SelectTab(ESOLSettingsTab current, ESOLSettingsTab requested);
//       // The tab ScrollToBinding shows before it scrolls: KeyBindings for a binding in [0, SOLKeyBindings::
//       // BINDING_COUNT); for any other binding nothing scrolls and the tab stays (current when valid, otherwise
//       // OpeningTab())
//       SOLTEST_API ESOLSettingsTab TabForBinding(ESOLSettingsTab current, int32 binding);
//       // The button variant of the tab column's buttons (Tab: the hover trace extends, Q15)
//       SOLTEST_API ESOLSolidButtonVariant TabButtonVariant();
//       // The button variant of the settings rows' buttons: steppers, Rebind, Reset keys and the footer (Small: amber
//       // edge and underline, no trace, Q15)
//       SOLTEST_API ESOLSolidButtonVariant RowButtonVariant();
//   }
namespace
{
    constexpr int32 SOLIDSETTINGSTEST_TAB_COUNT = 6;                    // D13: six sections, six tabs
    constexpr uint8 SOLIDSETTINGSTEST_BAD_TAB = 200;                    // A tab value well past Count

    //////////////////////////////////////////////////////////////////////////
    // Returns a tab's position as an int (TestEqual on enum class values)
    int32 SolidSettingsTestIndex(const ESOLSettingsTab tab)
    {
        return static_cast<int32>(tab);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a tab's name for assertion messages (independent of the production labels)
    const TCHAR* SolidSettingsTestName(const ESOLSettingsTab tab)
    {
        switch (tab)
        {
        case ESOLSettingsTab::Graphics:
            return TEXT("Graphics");
        case ESOLSettingsTab::Audio:
            return TEXT("Audio");
        case ESOLSettingsTab::Mouse:
            return TEXT("Mouse");
        case ESOLSettingsTab::Gameplay:
            return TEXT("Gameplay");
        case ESOLSettingsTab::Overlays:
            return TEXT("Overlays");
        case ESOLSettingsTab::KeyBindings:
            return TEXT("KeyBindings");
        default:
            return TEXT("<invalid>");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a tab equals the expected one, naming both in the message
    void SolidSettingsTestTab(FAutomationTestBase& test, const FString& label, const ESOLSettingsTab actual,
        const ESOLSettingsTab expected)
    {
        test.TestEqual(FString::Printf(TEXT("%s (got %s, want %s)"), *label, SolidSettingsTestName(actual),
            SolidSettingsTestName(expected)), SolidSettingsTestIndex(actual), SolidSettingsTestIndex(expected));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an out-of-range tab value (a corrupt index cast to the enum)
    ESOLSettingsTab SolidSettingsTestBadTab()
    {
        return static_cast<ESOLSettingsTab>(SOLIDSETTINGSTEST_BAD_TAB);
    }

    // The six tabs in the expected column order (D13)
    constexpr ESOLSettingsTab SOLIDSETTINGSTEST_TABS[] = { ESOLSettingsTab::Graphics, ESOLSettingsTab::Audio,
        ESOLSettingsTab::Mouse, ESOLSettingsTab::Gameplay, ESOLSettingsTab::Overlays, ESOLSettingsTab::KeyBindings };
}

// ---------------------------------------------------------------------------------------------------------------------
// Tab set and order (D13)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsTabCountTest, "SOLTest.SolidSettings.Tabs.Count",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// There are exactly six tabs, one per existing Settings section
bool FSOLSolidSettingsTabCountTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("ESOLSettingsTab::Count is 6"), static_cast<int32>(ESOLSettingsTab::Count),
        SOLIDSETTINGSTEST_TAB_COUNT);
    TestEqual(TEXT("the expected order lists 6 tabs"), static_cast<int32>(UE_ARRAY_COUNT(SOLIDSETTINGSTEST_TABS)),
        SOLIDSETTINGSTEST_TAB_COUNT);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsTabOrderTest, "SOLTest.SolidSettings.Tabs.Order",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The column runs Graphics, Audio, Mouse, Gameplay, Overlays, Key bindings (the page's section order), and a tab
// button's index is its position in the column
bool FSOLSolidSettingsTabOrderTest::RunTest(const FString& /*parameters*/)
{
    for (int32 index = 0; index < SOLIDSETTINGSTEST_TAB_COUNT; ++index)
    {
        const ESOLSettingsTab expected = SOLIDSETTINGSTEST_TABS[index];
        TestEqual(FString::Printf(TEXT("%s sits at column position %d"), SolidSettingsTestName(expected), index),
            SolidSettingsTestIndex(expected), index);
        SolidSettingsTestTab(*this, FString::Printf(TEXT("TabFromIndex(%d)"), index),
            SOLSettingsTabs::TabFromIndex(index), expected);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsTabFromIndexRangeTest, "SOLTest.SolidSettings.Tabs.FromIndexRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// An index outside the column gives Count (no tab), never a wrapped or clamped tab
bool FSOLSolidSettingsTabFromIndexRangeTest::RunTest(const FString& /*parameters*/)
{
    const int32 badIndices[] = { -1, -100, SOLIDSETTINGSTEST_TAB_COUNT, SOLIDSETTINGSTEST_TAB_COUNT + 1, 255, 256,
        MAX_int32, MIN_int32, INDEX_NONE };
    for (const int32 index : badIndices)
    {
        SolidSettingsTestTab(*this, FString::Printf(TEXT("TabFromIndex(%d)"), index),
            SOLSettingsTabs::TabFromIndex(index), ESOLSettingsTab::Count);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsTabValidTest, "SOLTest.SolidSettings.Tabs.Valid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The six tabs are valid; Count and any value past it are not
bool FSOLSolidSettingsTabValidTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLSettingsTab tab : SOLIDSETTINGSTEST_TABS)
    {
        TestTrue(FString::Printf(TEXT("%s is valid"), SolidSettingsTestName(tab)), SOLSettingsTabs::IsValidTab(tab));
    }
    TestFalse(TEXT("Count is not a tab"), SOLSettingsTabs::IsValidTab(ESOLSettingsTab::Count));
    TestFalse(TEXT("Count + 1 is not a tab"), SOLSettingsTabs::IsValidTab(
        static_cast<ESOLSettingsTab>(SOLIDSETTINGSTEST_TAB_COUNT + 1)));
    TestFalse(TEXT("a value well past Count is not a tab"), SOLSettingsTabs::IsValidTab(SolidSettingsTestBadTab()));
    TestFalse(TEXT("255 is not a tab"), SOLSettingsTabs::IsValidTab(static_cast<ESOLSettingsTab>(255)));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Labels: each tab carries the heading of the section it replaces
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsTabLabelTest, "SOLTest.SolidSettings.Tabs.Labels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every tab's label is its section's SOLMenuText heading; an invalid tab has an empty (non-null) label
bool FSOLSolidSettingsTabLabelTest::RunTest(const FString& /*parameters*/)
{
    const TCHAR* const expected[] = { SOLMenuText::SETTINGS_GRAPHICS, SOLMenuText::SETTINGS_AUDIO,
        SOLMenuText::SETTINGS_MOUSE, SOLMenuText::SETTINGS_GAMEPLAY, SOLMenuText::SETTINGS_OVERLAYS,
        SOLMenuText::SETTINGS_KEYS };
    static_assert(UE_ARRAY_COUNT(expected) == SOLIDSETTINGSTEST_TAB_COUNT, "one label per tab");
    for (int32 index = 0; index < SOLIDSETTINGSTEST_TAB_COUNT; ++index)
    {
        const ESOLSettingsTab tab = SOLIDSETTINGSTEST_TABS[index];
        const TCHAR* const label = SOLSettingsTabs::TabLabel(tab);
        if (!TestNotNull(FString::Printf(TEXT("%s label not null"), SolidSettingsTestName(tab)), label))
        {
            continue;
        }
        TestEqual(FString::Printf(TEXT("%s label"), SolidSettingsTestName(tab)), FString(label),
            FString(expected[index]));
    }

    // Labels are distinct (a tab is told apart by its text)
    for (int32 a = 0; a < SOLIDSETTINGSTEST_TAB_COUNT; ++a)
    {
        for (int32 b = a + 1; b < SOLIDSETTINGSTEST_TAB_COUNT; ++b)
        {
            const TCHAR* const la = SOLSettingsTabs::TabLabel(SOLIDSETTINGSTEST_TABS[a]);
            const TCHAR* const lb = SOLSettingsTabs::TabLabel(SOLIDSETTINGSTEST_TABS[b]);
            TestTrue(FString::Printf(TEXT("labels of %s and %s differ"),
                SolidSettingsTestName(SOLIDSETTINGSTEST_TABS[a]), SolidSettingsTestName(SOLIDSETTINGSTEST_TABS[b])),
                la != nullptr && lb != nullptr && FString(la) != FString(lb));
        }
    }

    // Invalid tabs: an empty string, never null
    const ESOLSettingsTab invalid[] = { ESOLSettingsTab::Count, SolidSettingsTestBadTab() };
    for (const ESOLSettingsTab tab : invalid)
    {
        const TCHAR* const label = SOLSettingsTabs::TabLabel(tab);
        if (TestNotNull(FString::Printf(TEXT("invalid tab %d label not null"), SolidSettingsTestIndex(tab)), label))
        {
            TestTrue(FString::Printf(TEXT("invalid tab %d label empty"), SolidSettingsTestIndex(tab)),
                FString(label).IsEmpty());
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Opening tab (Q8): Settings always opens on Graphics, no last-tab memory
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsOpeningTabTest, "SOLTest.SolidSettings.Open.FirstTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The opening tab is the first tab, Graphics, and is valid
bool FSOLSolidSettingsOpeningTabTest::RunTest(const FString& /*parameters*/)
{
    SolidSettingsTestTab(*this, TEXT("OpeningTab is Graphics"), SOLSettingsTabs::OpeningTab(),
        ESOLSettingsTab::Graphics);
    SolidSettingsTestTab(*this, TEXT("OpeningTab is the first column entry"), SOLSettingsTabs::OpeningTab(),
        SOLSettingsTabs::TabFromIndex(0));
    TestTrue(TEXT("OpeningTab is valid"), SOLSettingsTabs::IsValidTab(SOLSettingsTabs::OpeningTab()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsNoLastTabMemoryTest, "SOLTest.SolidSettings.Open.NoLastTabMemory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Whatever tab was left showing (each of the six), the next open shows Graphics, and repeated opens never drift
bool FSOLSolidSettingsNoLastTabMemoryTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLSettingsTab left : SOLIDSETTINGSTEST_TABS)
    {
        // Open, choose a tab, leave; then open again
        ESOLSettingsTab current = SOLSettingsTabs::OpeningTab();
        current = SOLSettingsTabs::SelectTab(current, left);
        SolidSettingsTestTab(*this, FString::Printf(TEXT("visit chose %s"), SolidSettingsTestName(left)), current,
            left);
        current = SOLSettingsTabs::OpeningTab();
        SolidSettingsTestTab(*this, FString::Printf(TEXT("reopen after leaving %s"), SolidSettingsTestName(left)),
            current, ESOLSettingsTab::Graphics);
    }

    // A visit that ended on Key bindings through ScrollToBinding also reopens on Graphics
    ESOLSettingsTab current = SOLSettingsTabs::TabForBinding(SOLSettingsTabs::OpeningTab(), 0);
    SolidSettingsTestTab(*this, TEXT("ScrollToBinding visit on Key bindings"), current, ESOLSettingsTab::KeyBindings);
    current = SOLSettingsTabs::OpeningTab();
    SolidSettingsTestTab(*this, TEXT("reopen after a ScrollToBinding visit"), current, ESOLSettingsTab::Graphics);

    // Many opens in a row give the same tab
    for (int32 open = 0; open < 10; ++open)
    {
        SolidSettingsTestTab(*this, FString::Printf(TEXT("open %d"), open), SOLSettingsTabs::OpeningTab(),
            ESOLSettingsTab::Graphics);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Selecting a tab
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsSelectValidTest, "SOLTest.SolidSettings.Select.Valid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A click on any tab shows that tab, from any tab (including the one already showing)
bool FSOLSolidSettingsSelectValidTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLSettingsTab current : SOLIDSETTINGSTEST_TABS)
    {
        for (const ESOLSettingsTab requested : SOLIDSETTINGSTEST_TABS)
        {
            SolidSettingsTestTab(*this, FString::Printf(TEXT("SelectTab(%s, %s)"), SolidSettingsTestName(current),
                SolidSettingsTestName(requested)), SOLSettingsTabs::SelectTab(current, requested), requested);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsSelectInvalidTest, "SOLTest.SolidSettings.Select.Invalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// An invalid request keeps the current tab; with no valid current tab either, the opening tab shows; a valid request
// from an invalid current tab still shows the request
bool FSOLSolidSettingsSelectInvalidTest::RunTest(const FString& /*parameters*/)
{
    const ESOLSettingsTab invalid[] = { ESOLSettingsTab::Count, SolidSettingsTestBadTab() };
    for (const ESOLSettingsTab current : SOLIDSETTINGSTEST_TABS)
    {
        for (const ESOLSettingsTab requested : invalid)
        {
            SolidSettingsTestTab(*this, FString::Printf(TEXT("SelectTab(%s, invalid %d) keeps the tab"),
                SolidSettingsTestName(current), SolidSettingsTestIndex(requested)),
                SOLSettingsTabs::SelectTab(current, requested), current);
        }
    }
    for (const ESOLSettingsTab current : invalid)
    {
        for (const ESOLSettingsTab requested : invalid)
        {
            SolidSettingsTestTab(*this, FString::Printf(TEXT("SelectTab(invalid %d, invalid %d) is the opening tab"),
                SolidSettingsTestIndex(current), SolidSettingsTestIndex(requested)),
                SOLSettingsTabs::SelectTab(current, requested), SOLSettingsTabs::OpeningTab());
        }
        for (const ESOLSettingsTab requested : SOLIDSETTINGSTEST_TABS)
        {
            SolidSettingsTestTab(*this, FString::Printf(TEXT("SelectTab(invalid %d, %s)"),
                SolidSettingsTestIndex(current), SolidSettingsTestName(requested)),
                SOLSettingsTabs::SelectTab(current, requested), requested);
        }
    }

    // A click index out of range, through TabFromIndex, changes nothing
    SolidSettingsTestTab(*this, TEXT("click on index 6 from Audio"),
        SOLSettingsTabs::SelectTab(ESOLSettingsTab::Audio, SOLSettingsTabs::TabFromIndex(SOLIDSETTINGSTEST_TAB_COUNT)),
        ESOLSettingsTab::Audio);
    SolidSettingsTestTab(*this, TEXT("click on index -1 from Overlays"),
        SOLSettingsTabs::SelectTab(ESOLSettingsTab::Overlays, SOLSettingsTabs::TabFromIndex(-1)),
        ESOLSettingsTab::Overlays);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsIsTabChangeTest, "SOLTest.SolidSettings.Select.IsTabChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A click changes the tab only when it names a different valid tab: a re-click on the shown tab is a no-op, an invalid
// request changes nothing
bool FSOLSolidSettingsIsTabChangeTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLSettingsTab current : SOLIDSETTINGSTEST_TABS)
    {
        for (const ESOLSettingsTab requested : SOLIDSETTINGSTEST_TABS)
        {
            TestEqual(FString::Printf(TEXT("IsTabChange(%s, %s)"), SolidSettingsTestName(current),
                SolidSettingsTestName(requested)), SOLSettingsTabs::IsTabChange(current, requested),
                requested != current);
        }
        TestFalse(TEXT("IsTabChange to Count"), SOLSettingsTabs::IsTabChange(current, ESOLSettingsTab::Count));
        TestFalse(TEXT("IsTabChange to a bad tab"), SOLSettingsTabs::IsTabChange(current, SolidSettingsTestBadTab()));
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// ScrollToBinding switches to Key bindings first
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsBindingTabTest,
    "SOLTest.SolidSettings.ScrollToBinding.KeyBindingsTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every valid binding, from every tab (Key bindings included), shows the Key bindings tab
bool FSOLSolidSettingsBindingTabTest::RunTest(const FString& /*parameters*/)
{
    if (!TestTrue(TEXT("there are bindings to scroll to"), SOLKeyBindings::BINDING_COUNT > 0))
    {
        return true;
    }
    int32 failures = 0;
    FString first;
    for (const ESOLSettingsTab current : SOLIDSETTINGSTEST_TABS)
    {
        for (int32 binding = 0; binding < SOLKeyBindings::BINDING_COUNT; ++binding)
        {
            const ESOLSettingsTab shown = SOLSettingsTabs::TabForBinding(current, binding);
            if (shown != ESOLSettingsTab::KeyBindings)
            {
                if (failures == 0)
                {
                    first = FString::Printf(TEXT("TabForBinding(%s, %d) gave %s"), SolidSettingsTestName(current),
                        binding, SolidSettingsTestName(shown));
                }
                ++failures;
            }
        }
    }
    TestEqual(FString::Printf(TEXT("every valid binding shows Key bindings (first failure: %s)"), *first), failures,
        0);

    // The first and the last binding explicitly (the range ends)
    SolidSettingsTestTab(*this, TEXT("first binding from Graphics"),
        SOLSettingsTabs::TabForBinding(ESOLSettingsTab::Graphics, 0), ESOLSettingsTab::KeyBindings);
    SolidSettingsTestTab(*this, TEXT("last binding from Gameplay"),
        SOLSettingsTabs::TabForBinding(ESOLSettingsTab::Gameplay, SOLKeyBindings::BINDING_COUNT - 1),
        ESOLSettingsTab::KeyBindings);

    // From an invalid current tab a valid binding still shows Key bindings
    SolidSettingsTestTab(*this, TEXT("valid binding from Count"),
        SOLSettingsTabs::TabForBinding(ESOLSettingsTab::Count, 0), ESOLSettingsTab::KeyBindings);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsBindingInvalidTest,
    "SOLTest.SolidSettings.ScrollToBinding.InvalidKeepsTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A binding outside [0, BINDING_COUNT) scrolls nowhere and leaves the tab as it is (the opening tab when the current
// tab is invalid)
bool FSOLSolidSettingsBindingInvalidTest::RunTest(const FString& /*parameters*/)
{
    const int32 badBindings[] = { INDEX_NONE, -2, SOLKeyBindings::BINDING_COUNT, SOLKeyBindings::BINDING_COUNT + 1,
        MAX_int32, MIN_int32 };
    for (const ESOLSettingsTab current : SOLIDSETTINGSTEST_TABS)
    {
        for (const int32 binding : badBindings)
        {
            SolidSettingsTestTab(*this, FString::Printf(TEXT("TabForBinding(%s, %d) keeps the tab"),
                SolidSettingsTestName(current), binding), SOLSettingsTabs::TabForBinding(current, binding), current);
        }
    }
    for (const int32 binding : badBindings)
    {
        SolidSettingsTestTab(*this, FString::Printf(TEXT("TabForBinding(Count, %d) is the opening tab"), binding),
            SOLSettingsTabs::TabForBinding(ESOLSettingsTab::Count, binding), SOLSettingsTabs::OpeningTab());
        SolidSettingsTestTab(*this, FString::Printf(TEXT("TabForBinding(bad tab, %d) is the opening tab"), binding),
            SOLSettingsTabs::TabForBinding(SolidSettingsTestBadTab(), binding), SOLSettingsTabs::OpeningTab());
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Control to tab mapping: each tab shows its section's rows (SDD 46 section 6, Settings row)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsSliderTabTest, "SOLTest.SolidSettings.Map.Sliders",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Volumes on Audio, sensitivity on Mouse, HUD scale / HUD effects / menu effects / Chase margin on Gameplay
bool FSOLSolidSettingsSliderTabTest::RunTest(const FString& /*parameters*/)
{
    struct FCase
    {
        ESOLSettingsSlider Slider;
        ESOLSettingsTab Tab;
        const TCHAR* Name;
    };
    const FCase cases[] = {
        { ESOLSettingsSlider::MasterVolume, ESOLSettingsTab::Audio, TEXT("MasterVolume") },
        { ESOLSettingsSlider::SfxVolume, ESOLSettingsTab::Audio, TEXT("SfxVolume") },
        { ESOLSettingsSlider::MusicVolume, ESOLSettingsTab::Audio, TEXT("MusicVolume") },
        { ESOLSettingsSlider::MouseSensitivity, ESOLSettingsTab::Mouse, TEXT("MouseSensitivity") },
        { ESOLSettingsSlider::HudScale, ESOLSettingsTab::Gameplay, TEXT("HudScale") },
        { ESOLSettingsSlider::HudEffects, ESOLSettingsTab::Gameplay, TEXT("HudEffects") },
        { ESOLSettingsSlider::HoloMenuEffects, ESOLSettingsTab::Gameplay, TEXT("HoloMenuEffects") },
        { ESOLSettingsSlider::ChaseSafetyMargin, ESOLSettingsTab::Gameplay, TEXT("ChaseSafetyMargin") },
    };
    TestEqual(TEXT("one case per slider"), static_cast<int32>(UE_ARRAY_COUNT(cases)),
        static_cast<int32>(ESOLSettingsSlider::Count));
    for (const FCase& c : cases)
    {
        SolidSettingsTestTab(*this, FString::Printf(TEXT("slider %s"), c.Name), SOLSettingsTabs::TabOfSlider(c.Slider),
            c.Tab);
    }
    SolidSettingsTestTab(*this, TEXT("slider Count"), SOLSettingsTabs::TabOfSlider(ESOLSettingsSlider::Count),
        ESOLSettingsTab::Count);
    SolidSettingsTestTab(*this, TEXT("slider 200"), SOLSettingsTabs::TabOfSlider(static_cast<ESOLSettingsSlider>(200)),
        ESOLSettingsTab::Count);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsToggleTabTest, "SOLTest.SolidSettings.Map.Toggles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// VSync and space dust on Graphics, invert Y on Mouse, HUD visible / assist / mouse steering at start on Gameplay
bool FSOLSolidSettingsToggleTabTest::RunTest(const FString& /*parameters*/)
{
    struct FCase
    {
        ESOLSettingsToggle Toggle;
        ESOLSettingsTab Tab;
        const TCHAR* Name;
    };
    const FCase cases[] = {
        { ESOLSettingsToggle::Vsync, ESOLSettingsTab::Graphics, TEXT("Vsync") },
        { ESOLSettingsToggle::SpaceDust, ESOLSettingsTab::Graphics, TEXT("SpaceDust") },
        { ESOLSettingsToggle::InvertY, ESOLSettingsTab::Mouse, TEXT("InvertY") },
        { ESOLSettingsToggle::HudVisible, ESOLSettingsTab::Gameplay, TEXT("HudVisible") },
        { ESOLSettingsToggle::DefaultAssist, ESOLSettingsTab::Gameplay, TEXT("DefaultAssist") },
        { ESOLSettingsToggle::MouseSteerAtStart, ESOLSettingsTab::Gameplay, TEXT("MouseSteerAtStart") },
    };
    TestEqual(TEXT("one case per toggle"), static_cast<int32>(UE_ARRAY_COUNT(cases)),
        static_cast<int32>(ESOLSettingsToggle::Count));
    for (const FCase& c : cases)
    {
        SolidSettingsTestTab(*this, FString::Printf(TEXT("toggle %s"), c.Name), SOLSettingsTabs::TabOfToggle(c.Toggle),
            c.Tab);
    }
    SolidSettingsTestTab(*this, TEXT("toggle Count"), SOLSettingsTabs::TabOfToggle(ESOLSettingsToggle::Count),
        ESOLSettingsTab::Count);
    SolidSettingsTestTab(*this, TEXT("toggle 200"), SOLSettingsTabs::TabOfToggle(static_cast<ESOLSettingsToggle>(200)),
        ESOLSettingsTab::Count);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsStepperTabTest, "SOLTest.SolidSettings.Map.Steppers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Resolution, window mode and quality all on Graphics
bool FSOLSolidSettingsStepperTabTest::RunTest(const FString& /*parameters*/)
{
    struct FCase
    {
        ESOLSettingsStepper Stepper;
        const TCHAR* Name;
    };
    const FCase cases[] = {
        { ESOLSettingsStepper::Resolution, TEXT("Resolution") },
        { ESOLSettingsStepper::WindowMode, TEXT("WindowMode") },
        { ESOLSettingsStepper::Quality, TEXT("Quality") },
    };
    TestEqual(TEXT("one case per stepper"), static_cast<int32>(UE_ARRAY_COUNT(cases)),
        static_cast<int32>(ESOLSettingsStepper::Count));
    for (const FCase& c : cases)
    {
        SolidSettingsTestTab(*this, FString::Printf(TEXT("stepper %s"), c.Name),
            SOLSettingsTabs::TabOfStepper(c.Stepper), ESOLSettingsTab::Graphics);
    }
    SolidSettingsTestTab(*this, TEXT("stepper Count"), SOLSettingsTabs::TabOfStepper(ESOLSettingsStepper::Count),
        ESOLSettingsTab::Count);
    SolidSettingsTestTab(*this, TEXT("stepper 200"),
        SOLSettingsTabs::TabOfStepper(static_cast<ESOLSettingsStepper>(200)), ESOLSettingsTab::Count);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsMapCoverageTest, "SOLTest.SolidSettings.Map.Coverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every control lands on a valid tab; Graphics, Audio, Mouse and Gameplay each show at least one slider, toggle or
// stepper; Overlays (the embedded list) and Key bindings (the key rows) show none
bool FSOLSolidSettingsMapCoverageTest::RunTest(const FString& /*parameters*/)
{
    int32 perTab[SOLIDSETTINGSTEST_TAB_COUNT] = {};

    // Counts a control's tab, failing for an invalid one
    const auto count = [this, &perTab](const ESOLSettingsTab tab, const FString& what)
    {
        if (TestTrue(FString::Printf(TEXT("%s is on a valid tab"), *what), SOLSettingsTabs::IsValidTab(tab)))
        {
            ++perTab[SolidSettingsTestIndex(tab)];
        }
    };
    for (int32 i = 0; i < static_cast<int32>(ESOLSettingsSlider::Count); ++i)
    {
        count(SOLSettingsTabs::TabOfSlider(static_cast<ESOLSettingsSlider>(i)), FString::Printf(TEXT("slider %d"), i));
    }
    for (int32 i = 0; i < static_cast<int32>(ESOLSettingsToggle::Count); ++i)
    {
        count(SOLSettingsTabs::TabOfToggle(static_cast<ESOLSettingsToggle>(i)), FString::Printf(TEXT("toggle %d"), i));
    }
    for (int32 i = 0; i < static_cast<int32>(ESOLSettingsStepper::Count); ++i)
    {
        count(SOLSettingsTabs::TabOfStepper(static_cast<ESOLSettingsStepper>(i)),
            FString::Printf(TEXT("stepper %d"), i));
    }

    TestEqual(TEXT("Graphics rows (3 steppers + 2 toggles)"), perTab[SolidSettingsTestIndex(ESOLSettingsTab::Graphics)],
        5);
    TestEqual(TEXT("Audio rows (3 sliders)"), perTab[SolidSettingsTestIndex(ESOLSettingsTab::Audio)], 3);
    TestEqual(TEXT("Mouse rows (1 slider + 1 toggle)"), perTab[SolidSettingsTestIndex(ESOLSettingsTab::Mouse)], 2);
    TestEqual(TEXT("Gameplay rows (4 sliders + 3 toggles)"), perTab[SolidSettingsTestIndex(ESOLSettingsTab::Gameplay)],
        7);
    TestEqual(TEXT("Overlays has no slider, toggle or stepper"),
        perTab[SolidSettingsTestIndex(ESOLSettingsTab::Overlays)], 0);
    TestEqual(TEXT("Key bindings has no slider, toggle or stepper"),
        perTab[SolidSettingsTestIndex(ESOLSettingsTab::KeyBindings)], 0);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Hover trace (Q15): only the tab column's buttons extend a trace, the settings rows do not
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsVariantTest, "SOLTest.SolidSettings.Hover.TraceOnlyOnTabs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The tab buttons are the Tab variant (with a hover trace); the row buttons are Small (edge and underline, no trace)
bool FSOLSolidSettingsVariantTest::RunTest(const FString& /*parameters*/)
{
    const ESOLSolidButtonVariant tab = SOLSettingsTabs::TabButtonVariant();
    const ESOLSolidButtonVariant row = SOLSettingsTabs::RowButtonVariant();
    TestEqual(TEXT("tab buttons are the Tab variant"), static_cast<int32>(tab),
        static_cast<int32>(ESOLSolidButtonVariant::Tab));
    TestEqual(TEXT("row buttons are the Small variant"), static_cast<int32>(row),
        static_cast<int32>(ESOLSolidButtonVariant::Small));
    TestTrue(TEXT("the tab column's hover extends a trace"), SOLSolidStyle::HasHoverTrace(tab));
    TestFalse(TEXT("a settings row's hover has no trace"), SOLSolidStyle::HasHoverTrace(row));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Label column (46e follow-up): the Graphics tab's narrower column, Gameplay's wider one, the shared one elsewhere
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidSettingsLabelColumnTest, "SOLTest.SolidSettings.Layout.LabelColumnWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// LabelColumnWidth: SETTINGS_GRAPHICS_LABEL_WIDTH on Graphics, SETTINGS_GAMEPLAY_LABEL_WIDTH on Gameplay,
// SETTINGS_LABEL_WIDTH on every other tab and an invalid one; every stepper sits under the narrower column (the
// steppers' values are what needs the room)
bool FSOLSolidSettingsLabelColumnTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Graphics: the narrower column"), SOLSettingsTabs::LabelColumnWidth(ESOLSettingsTab::Graphics),
        SOL::SolidMenu::SETTINGS_GRAPHICS_LABEL_WIDTH);
    TestEqual(TEXT("Gameplay: the wider column"), SOLSettingsTabs::LabelColumnWidth(ESOLSettingsTab::Gameplay),
        SOL::SolidMenu::SETTINGS_GAMEPLAY_LABEL_WIDTH);
    const ESOLSettingsTab sharedTabs[] = { ESOLSettingsTab::Audio, ESOLSettingsTab::Mouse, ESOLSettingsTab::Overlays,
        ESOLSettingsTab::KeyBindings };
    for (const ESOLSettingsTab tab : sharedTabs)
    {
        TestEqual(FString::Printf(TEXT("tab %d: the shared column"), static_cast<int32>(tab)),
            SOLSettingsTabs::LabelColumnWidth(tab), SOL::MenuStyle::SETTINGS_LABEL_WIDTH);
    }
    TestEqual(TEXT("an invalid tab: the shared column"), SOLSettingsTabs::LabelColumnWidth(ESOLSettingsTab::Count),
        SOL::MenuStyle::SETTINGS_LABEL_WIDTH);
    TestTrue(TEXT("Graphics' column is the narrower one"),
        SOL::SolidMenu::SETTINGS_GRAPHICS_LABEL_WIDTH < SOL::MenuStyle::SETTINGS_LABEL_WIDTH);
    TestTrue(TEXT("Gameplay's column is the wider one"),
        SOL::SolidMenu::SETTINGS_GAMEPLAY_LABEL_WIDTH > SOL::MenuStyle::SETTINGS_LABEL_WIDTH);
    for (int32 stepper = 0; stepper < static_cast<int32>(ESOLSettingsStepper::Count); ++stepper)
    {
        TestEqual(FString::Printf(TEXT("stepper %d: under the narrower column"), stepper),
            SOLSettingsTabs::LabelColumnWidth(SOLSettingsTabs::TabOfStepper(static_cast<ESOLSettingsStepper>(stepper))),
            SOL::SolidMenu::SETTINGS_GRAPHICS_LABEL_WIDTH);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
