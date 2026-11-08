/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLGlossary.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLPageTabs.h"
#include "Menu/SOLSolidTabRail.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #46 Part 46f tab columns of the read-only pages (Docs/SDDs/46-solid-menus.md sections 6.3 and
// 6.5, Q6, Q8), written against the header contracts before the production code existed. They pin the pure,
// widget-free tab logic of the Controls, Info and About pages (Menu/SOLPageTabs.h) and the pure tab rail geometry
// shared with Settings (namespace SOLSolidTabRail in Menu/SOLSolidTabRail.h); the widget tree, the buttons and the
// trace layer are checked by the menu smokes.
//
// Contract under test:
//
//   enum class ESOLReadOnlyPage : uint8 { Controls, Info, About, Count };
//
//   namespace SOLPageTabs
//   {
//       int32 OpeningTab();                                  // 0, every page, every time
//       int32 TabCount(ESOLReadOnlyPage page);               // 0 for Count and past it
//       FString TabLabel(ESOLReadOnlyPage page, int32 tab);  // empty for an invalid page / tab
//       int32 SelectTab(int32 current, int32 requested, int32 count);
//       int32 ControlsTabOfRow(int32 row);                   // INDEX_NONE outside SOLMenuText::CONTROL_ROWS
//       float LeftColumnWidth(ESOLReadOnlyPage page, float rowWidth);   // (46i, written after the code)
//   }
//
//   namespace SOLSolidTabRail
//   {
//       FSOLSolidTrace TraceForTab(int32 index);             // two points, no ring terminal, negative = tab 0
//       float RailHeight(int32 tabCount);                    // padding both sides + each tab's height and gap
//   }
//
// (Issue #33 Part 33d-ii, SDD 33 D22 / D37, written against the 33d-ii contract before the code) The Help page is the
// fourth read-only page, and the rail can draw non-clickable group captions above some tabs:
//
//   enum class ESOLReadOnlyPage : uint8 { Controls, Info, About, Help, Count };
//   FString SOLPageTabs::TabCaption(ESOLReadOnlyPage page, int32 tab);  // Help: SOLGlossary::TabCaption, else ""
//   LeftColumnWidth(Help, any width) == SOL::Help::GLOSSARY_SYMBOL_BOX
//
//   float SOLSolidTabRail::TabTop(int32 index, TConstArrayView<bool> captioned);
//   float SOLSolidTabRail::CaptionTop(int32 index, TConstArrayView<bool> captioned);   // -1 when no caption
//   float SOLSolidTabRail::RailHeight(int32 tabCount, TConstArrayView<bool> captioned);
//   FSOLSolidTrace SOLSolidTabRail::TraceForTab(int32 index, TConstArrayView<bool> captioned);
namespace
{
    constexpr int32 SOLIDPAGESTEST_INFO_TABS = 4;               // Flight, Time and travel, Combat, How it works
    constexpr int32 SOLIDPAGESTEST_ABOUT_TABS = 2;              // Game, Credits
    constexpr int32 SOLIDPAGESTEST_HELP_TABS = 5;               // Keyboard, Flight HUD, Targets, Overlays, Map, menus
    constexpr uint8 SOLIDPAGESTEST_BAD_PAGE = 200;              // A page value well past Count
    constexpr float SOLIDPAGESTEST_TOLERANCE = 1.0e-3f;         // Geometry comparisons (rail local units)

    // The four pages with a tab column
    constexpr ESOLReadOnlyPage SOLIDPAGESTEST_PAGES[] = { ESOLReadOnlyPage::Controls, ESOLReadOnlyPage::Info,
        ESOLReadOnlyPage::About, ESOLReadOnlyPage::Help };

    // The Help rail's caption flags (a caption above Keyboard and above Flight HUD, D22)
    constexpr bool SOLIDPAGESTEST_HELP_CAPTIONED[] = { true, true, false, false, false };

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of rows in SOLMenuText::CONTROL_ROWS
    int32 SolidPagesTestRowCount()
    {
        return static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two area names are the same text
    bool SolidPagesTestSameArea(const TCHAR* a, const TCHAR* b)
    {
        return FCString::Strcmp(a, b) == 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of Controls tabs the table implies: one per area change between consecutive rows, plus one
    int32 SolidPagesTestControlsTabCount()
    {
        const int32 rows = SolidPagesTestRowCount();
        if (rows == 0)
        {
            return 0;
        }
        int32 tabs = 1;
        for (int32 row = 1; row < rows; ++row)
        {
            if (!SolidPagesTestSameArea(SOLMenuText::CONTROL_ROWS[row - 1].Area, SOLMenuText::CONTROL_ROWS[row].Area))
            {
                ++tabs;
            }
        }
        return tabs;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected Controls tab of each row (the running count of area changes), independent of production
    TArray<int32> SolidPagesTestExpectedTabOfRows()
    {
        TArray<int32> tabs;
        tabs.Reserve(SolidPagesTestRowCount());
        int32 tab = 0;
        for (int32 row = 0; row < SolidPagesTestRowCount(); ++row)
        {
            if (row > 0 && !SolidPagesTestSameArea(SOLMenuText::CONTROL_ROWS[row - 1].Area,
                SOLMenuText::CONTROL_ROWS[row].Area))
            {
                ++tab;
            }
            tabs.Add(tab);
        }
        return tabs;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a page's name for assertion messages (independent of the production labels)
    const TCHAR* SolidPagesTestName(const ESOLReadOnlyPage page)
    {
        switch (page)
        {
        case ESOLReadOnlyPage::Controls:
            return TEXT("Controls");
        case ESOLReadOnlyPage::Info:
            return TEXT("Info");
        case ESOLReadOnlyPage::About:
            return TEXT("About");
        case ESOLReadOnlyPage::Help:
            return TEXT("Help");
        default:
            return TEXT("<invalid>");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an out-of-range page value (a corrupt index cast to the enum)
    ESOLReadOnlyPage SolidPagesTestBadPage()
    {
        return static_cast<ESOLReadOnlyPage>(SOLIDPAGESTEST_BAD_PAGE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected tab count of a valid page (Controls derived from the table)
    int32 SolidPagesTestExpectedCount(const ESOLReadOnlyPage page)
    {
        switch (page)
        {
        case ESOLReadOnlyPage::Controls:
            return SolidPagesTestControlsTabCount();
        case ESOLReadOnlyPage::Info:
            return SOLIDPAGESTEST_INFO_TABS;
        case ESOLReadOnlyPage::About:
            return SOLIDPAGESTEST_ABOUT_TABS;
        case ESOLReadOnlyPage::Help:
            return static_cast<int32>(ESOLHelpTab::Count);
        default:
            return 0;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the vertical distance between two neighbouring tabs on the rail
    float SolidPagesTestTabStep()
    {
        return SOL::MenuStyle::TAB_HEIGHT + SOL::MenuStyle::BUTTON_GAP;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the height a caption row adds above its tab: the caption and the gap after it
    float SolidPagesTestCaptionStep()
    {
        return SOL::SolidMenu::TAB_RAIL_CAPTION_HEIGHT + SOL::MenuStyle::BUTTON_GAP;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a caption sits above tab index (false outside the view)
    bool SolidPagesTestCaptioned(const int32 index, const TConstArrayView<bool> captioned)
    {
        return index >= 0 && index < captioned.Num() && captioned[index];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of caption flags set among the first count entries of the view (count clamped to the view)
    int32 SolidPagesTestFlagsBefore(const int32 count, const TConstArrayView<bool> captioned)
    {
        int32 flags = 0;
        for (int32 i = 0; i < FMath::Min(count, captioned.Num()); ++i)
        {
            flags += captioned[i] ? 1 : 0;
        }
        return flags;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected top of a non-negative tab index: padding, the tabs above it, the caption rows up to and
    // including its own (independent of production)
    float SolidPagesTestExpectedTabTop(const int32 index, const TConstArrayView<bool> captioned)
    {
        return SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING + index * SolidPagesTestTabStep() +
            SolidPagesTestFlagsBefore(index + 1, captioned) * SolidPagesTestCaptionStep();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected rail height of tabCount tabs (a negative count reads 0)
    float SolidPagesTestExpectedRailHeight(const int32 tabCount, const TConstArrayView<bool> captioned)
    {
        const int32 count = FMath::Max(tabCount, 0);
        return 2.0f * SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING + count * SolidPagesTestTabStep() +
            SolidPagesTestFlagsBefore(count, captioned) * SolidPagesTestCaptionStep();
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Opening tab and tab counts (Q6, Q8)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesOpeningTabTest, "SOLTest.SolidPages.Tabs.OpeningTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every page opens on its first tab, 0, and repeated opens never drift (no last-tab memory)
bool FSOLSolidPagesOpeningTabTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("OpeningTab is 0"), SOLPageTabs::OpeningTab(), 0);
    for (const ESOLReadOnlyPage page : SOLIDPAGESTEST_PAGES)
    {
        TestTrue(FString::Printf(TEXT("%s's opening tab is one of its tabs"), SolidPagesTestName(page)),
            SOLPageTabs::OpeningTab() < SOLPageTabs::TabCount(page));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesTabCountTest, "SOLTest.SolidPages.Tabs.Count",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Controls: one tab per area run of CONTROL_ROWS; Info: 4; About: 2; Help: the Help tab list (5); Count and past it: 0
bool FSOLSolidPagesTabCountTest::RunTest(const FString& /*parameters*/)
{
    // (33d-ii) Help is appended before Count, so Count is 4
    TestEqual(TEXT("ESOLReadOnlyPage::Count is 4"), static_cast<int32>(ESOLReadOnlyPage::Count), 4);
    TestEqual(TEXT("Help follows About"), static_cast<int32>(ESOLReadOnlyPage::Help),
        static_cast<int32>(ESOLReadOnlyPage::About) + 1);
    TestEqual(TEXT("TabCount(Help) is SOLGlossary::TabCount()"), SOLPageTabs::TabCount(ESOLReadOnlyPage::Help),
        SOLGlossary::TabCount());
    TestEqual(TEXT("TabCount(Help) is 5 (today's data)"), SOLPageTabs::TabCount(ESOLReadOnlyPage::Help),
        SOLIDPAGESTEST_HELP_TABS);
    TestTrue(TEXT("CONTROL_ROWS is not empty"), SolidPagesTestRowCount() > 0);
    TestTrue(TEXT("Controls has more than one area"), SolidPagesTestControlsTabCount() > 1);
    for (const ESOLReadOnlyPage page : SOLIDPAGESTEST_PAGES)
    {
        TestEqual(FString::Printf(TEXT("TabCount(%s)"), SolidPagesTestName(page)), SOLPageTabs::TabCount(page),
            SolidPagesTestExpectedCount(page));
    }
    TestEqual(TEXT("TabCount(Count)"), SOLPageTabs::TabCount(ESOLReadOnlyPage::Count), 0);
    TestEqual(TEXT("TabCount(Count + 1)"), SOLPageTabs::TabCount(
        static_cast<ESOLReadOnlyPage>(static_cast<uint8>(ESOLReadOnlyPage::Count) + 1)), 0);
    TestEqual(TEXT("TabCount(bad page)"), SOLPageTabs::TabCount(SolidPagesTestBadPage()), 0);
    TestEqual(TEXT("TabCount(255)"), SOLPageTabs::TabCount(static_cast<ESOLReadOnlyPage>(255)), 0);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Labels
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesLabelRangeTest, "SOLTest.SolidPages.Labels.Range",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every tab in [0, TabCount) of a valid page has a non-empty label; a tab past the end, a negative tab and any tab of
// an invalid page have an empty label
bool FSOLSolidPagesLabelRangeTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLReadOnlyPage page : SOLIDPAGESTEST_PAGES)
    {
        const int32 count = SOLPageTabs::TabCount(page);
        for (int32 tab = 0; tab < count; ++tab)
        {
            TestFalse(FString::Printf(TEXT("%s tab %d has a label"), SolidPagesTestName(page), tab),
                SOLPageTabs::TabLabel(page, tab).IsEmpty());
        }
        const int32 badTabs[] = { count, count + 1, -1, -100, INDEX_NONE, MAX_int32, MIN_int32 };
        for (const int32 tab : badTabs)
        {
            TestTrue(FString::Printf(TEXT("%s tab %d: empty label"), SolidPagesTestName(page), tab),
                SOLPageTabs::TabLabel(page, tab).IsEmpty());
        }
    }

    // Invalid pages: empty for every tab, including tab 0
    const ESOLReadOnlyPage invalid[] = { ESOLReadOnlyPage::Count, SolidPagesTestBadPage() };
    const int32 anyTabs[] = { 0, 1, 3, -1 };
    for (const ESOLReadOnlyPage page : invalid)
    {
        for (const int32 tab : anyTabs)
        {
            TestTrue(FString::Printf(TEXT("invalid page %d tab %d: empty label"), static_cast<int32>(page), tab),
                SOLPageTabs::TabLabel(page, tab).IsEmpty());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesControlsLabelsTest, "SOLTest.SolidPages.Labels.Controls",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Each Controls tab's label is the area of the first row of its run, in table order, and the labels are unique
bool FSOLSolidPagesControlsLabelsTest::RunTest(const FString& /*parameters*/)
{
    // The expected labels: the area of each run's first row, in table order
    TArray<FString> expected;
    for (int32 row = 0; row < SolidPagesTestRowCount(); ++row)
    {
        if (row == 0 || !SolidPagesTestSameArea(SOLMenuText::CONTROL_ROWS[row - 1].Area,
            SOLMenuText::CONTROL_ROWS[row].Area))
        {
            expected.Add(FString(SOLMenuText::CONTROL_ROWS[row].Area));
        }
    }
    const int32 count = SOLPageTabs::TabCount(ESOLReadOnlyPage::Controls);
    if (!TestEqual(TEXT("one Controls tab per area run"), count, expected.Num()))
    {
        return true;
    }
    for (int32 tab = 0; tab < count; ++tab)
    {
        TestEqual(FString::Printf(TEXT("Controls tab %d label"), tab),
            SOLPageTabs::TabLabel(ESOLReadOnlyPage::Controls, tab), expected[tab]);
    }

    // Unique: the rows of one area are contiguous, so no area starts two tabs
    for (int32 a = 0; a < count; ++a)
    {
        for (int32 b = a + 1; b < count; ++b)
        {
            TestTrue(FString::Printf(TEXT("Controls tabs %d and %d differ"), a, b),
                SOLPageTabs::TabLabel(ESOLReadOnlyPage::Controls, a) !=
                SOLPageTabs::TabLabel(ESOLReadOnlyPage::Controls, b));
        }
    }

    // The first tab is the first row's area, the last tab the last row's
    TestEqual(TEXT("first Controls tab is the first row's area"),
        SOLPageTabs::TabLabel(ESOLReadOnlyPage::Controls, 0), FString(SOLMenuText::CONTROL_ROWS[0].Area));
    TestEqual(TEXT("last Controls tab is the last row's area"),
        SOLPageTabs::TabLabel(ESOLReadOnlyPage::Controls, count - 1),
        FString(SOLMenuText::CONTROL_ROWS[SolidPagesTestRowCount() - 1].Area));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesInfoLabelsTest, "SOLTest.SolidPages.Labels.Info",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The Info tabs are Flight, Time and travel, Combat, How it works (the SOLMenuText headings), in that order
bool FSOLSolidPagesInfoLabelsTest::RunTest(const FString& /*parameters*/)
{
    const TCHAR* const expected[] = { SOLMenuText::INFO_FLIGHT, SOLMenuText::INFO_TIME_TAB, SOLMenuText::INFO_COMBAT,
        SOLMenuText::INFO_HOW_IT_WORKS };
    static_assert(UE_ARRAY_COUNT(expected) == SOLIDPAGESTEST_INFO_TABS, "one label per Info tab");
    for (int32 tab = 0; tab < SOLIDPAGESTEST_INFO_TABS; ++tab)
    {
        TestEqual(FString::Printf(TEXT("Info tab %d label"), tab), SOLPageTabs::TabLabel(ESOLReadOnlyPage::Info, tab),
            FString(expected[tab]));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesAboutLabelsTest, "SOLTest.SolidPages.Labels.About",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The About tabs are Game and Credits (SOLMenuText::ABOUT_TAB_GAME, ABOUT_CREDITS_TITLE), in that order
bool FSOLSolidPagesAboutLabelsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("About tab 0 label"), SOLPageTabs::TabLabel(ESOLReadOnlyPage::About, 0),
        FString(SOLMenuText::ABOUT_TAB_GAME));
    TestEqual(TEXT("About tab 1 label"), SOLPageTabs::TabLabel(ESOLReadOnlyPage::About, 1),
        FString(SOLMenuText::ABOUT_CREDITS_TITLE));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesHelpLabelsTest, "SOLTest.SolidPages.Labels.Help",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// (33d-ii, D22) The Help tabs are SOLMenuText::HELP_TAB_LABELS in order (Keyboard, Flight HUD, Targets, Overlays,
// Map, menus); every out-of-range tab has an empty label
bool FSOLSolidPagesHelpLabelsTest::RunTest(const FString& /*parameters*/)
{
    const int32 tableCount = static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::HELP_TAB_LABELS));
    TestEqual(TEXT("one Help tab per HELP_TAB_LABELS entry"), SOLPageTabs::TabCount(ESOLReadOnlyPage::Help),
        tableCount);
    for (int32 tab = 0; tab < tableCount; ++tab)
    {
        TestEqual(FString::Printf(TEXT("Help tab %d label"), tab), SOLPageTabs::TabLabel(ESOLReadOnlyPage::Help, tab),
            FString(SOLMenuText::HELP_TAB_LABELS[tab]));
    }

    // Today's labels, as a cross-check of the table
    const TCHAR* const expected[] = { TEXT("Keyboard"), TEXT("Flight HUD"), TEXT("Targets"), TEXT("Overlays"),
        TEXT("Map, menus") };
    static_assert(UE_ARRAY_COUNT(expected) == SOLIDPAGESTEST_HELP_TABS, "one label per Help tab");
    for (int32 tab = 0; tab < SOLIDPAGESTEST_HELP_TABS; ++tab)
    {
        TestEqual(FString::Printf(TEXT("Help tab %d is \"%s\""), tab, expected[tab]),
            SOLPageTabs::TabLabel(ESOLReadOnlyPage::Help, tab), FString(expected[tab]));
    }

    const int32 badTabs[] = { -1, tableCount, tableCount + 1, INDEX_NONE, MIN_int32, MAX_int32 };
    for (const int32 tab : badTabs)
    {
        TestTrue(FString::Printf(TEXT("Help tab %d: empty label"), tab),
            SOLPageTabs::TabLabel(ESOLReadOnlyPage::Help, tab).IsEmpty());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesCaptionsTest, "SOLTest.SolidPages.Captions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// (33d-ii, D22) Only the Help page has group captions: KEYS above Keyboard, HUD GLOSSARY above Flight HUD, none above
// the other tabs; every tab of Controls, Info and About, every invalid page and an out-of-range Help tab have none
bool FSOLSolidPagesCaptionsTest::RunTest(const FString& /*parameters*/)
{
    // Help: the SOLMenuText captions on tabs 0 and 1, empty on the rest
    TestEqual(TEXT("Help tab 0 caption is HELP_CAPTION_KEYS"), SOLPageTabs::TabCaption(ESOLReadOnlyPage::Help, 0),
        FString(SOLMenuText::HELP_CAPTION_KEYS));
    TestEqual(TEXT("Help tab 1 caption is HELP_CAPTION_GLOSSARY"), SOLPageTabs::TabCaption(ESOLReadOnlyPage::Help, 1),
        FString(SOLMenuText::HELP_CAPTION_GLOSSARY));
    TestEqual(TEXT("Help tab 0 caption is \"KEYS\""), SOLPageTabs::TabCaption(ESOLReadOnlyPage::Help, 0),
        FString(TEXT("KEYS")));
    TestEqual(TEXT("Help tab 1 caption is \"HUD GLOSSARY\""), SOLPageTabs::TabCaption(ESOLReadOnlyPage::Help, 1),
        FString(TEXT("HUD GLOSSARY")));
    const int32 helpCount = SOLPageTabs::TabCount(ESOLReadOnlyPage::Help);
    for (int32 tab = 0; tab < helpCount; ++tab)
    {
        TestEqual(FString::Printf(TEXT("Help tab %d caption equals SOLGlossary::TabCaption"), tab),
            SOLPageTabs::TabCaption(ESOLReadOnlyPage::Help, tab),
            FString(SOLGlossary::TabCaption(static_cast<ESOLHelpTab>(tab))));
        TestTrue(FString::Printf(TEXT("Help tab %d captioned flag"), tab),
            !SOLPageTabs::TabCaption(ESOLReadOnlyPage::Help, tab).IsEmpty() ==
            SolidPagesTestCaptioned(tab, MakeArrayView(SOLIDPAGESTEST_HELP_CAPTIONED)));
    }
    const int32 badHelpTabs[] = { -1, helpCount, helpCount + 1, INDEX_NONE, MIN_int32, MAX_int32 };
    for (const int32 tab : badHelpTabs)
    {
        TestTrue(FString::Printf(TEXT("Help tab %d: no caption"), tab),
            SOLPageTabs::TabCaption(ESOLReadOnlyPage::Help, tab).IsEmpty());
    }

    // The other pages: no caption on any tab, in range or not
    const ESOLReadOnlyPage plainPages[] = { ESOLReadOnlyPage::Controls, ESOLReadOnlyPage::Info,
        ESOLReadOnlyPage::About };
    for (const ESOLReadOnlyPage page : plainPages)
    {
        const int32 count = SOLPageTabs::TabCount(page);
        for (int32 tab = -1; tab <= count; ++tab)
        {
            TestTrue(FString::Printf(TEXT("%s tab %d: no caption"), SolidPagesTestName(page), tab),
                SOLPageTabs::TabCaption(page, tab).IsEmpty());
        }
    }

    // Invalid pages: no caption, even on tabs 0 and 1
    const ESOLReadOnlyPage invalid[] = { ESOLReadOnlyPage::Count,
        static_cast<ESOLReadOnlyPage>(static_cast<uint8>(ESOLReadOnlyPage::Count) + 1), SolidPagesTestBadPage(),
        static_cast<ESOLReadOnlyPage>(255) };
    const int32 anyTabs[] = { 0, 1, 2, -1 };
    for (const ESOLReadOnlyPage page : invalid)
    {
        for (const int32 tab : anyTabs)
        {
            TestTrue(FString::Printf(TEXT("invalid page %d tab %d: no caption"), static_cast<int32>(page), tab),
                SOLPageTabs::TabCaption(page, tab).IsEmpty());
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Selecting a tab
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesSelectValidTest, "SOLTest.SolidPages.Select.Valid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A click on any tab in range shows that tab, from any tab (the one showing included) and from an invalid current tab
bool FSOLSolidPagesSelectValidTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLReadOnlyPage page : SOLIDPAGESTEST_PAGES)
    {
        const int32 count = SOLPageTabs::TabCount(page);
        for (int32 current = 0; current < count; ++current)
        {
            for (int32 requested = 0; requested < count; ++requested)
            {
                TestEqual(FString::Printf(TEXT("%s SelectTab(%d, %d, %d)"), SolidPagesTestName(page), current,
                    requested, count), SOLPageTabs::SelectTab(current, requested, count), requested);
            }
        }
        const int32 badCurrents[] = { -1, count, INDEX_NONE, MAX_int32 };
        for (const int32 current : badCurrents)
        {
            TestEqual(FString::Printf(TEXT("%s SelectTab(bad %d, last, %d)"), SolidPagesTestName(page), current,
                count), SOLPageTabs::SelectTab(current, count - 1, count), count - 1);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesSelectInvalidTest, "SOLTest.SolidPages.Select.Invalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// An out-of-range request keeps a valid current tab; with both out of range the opening tab shows; with no tabs at
// all nothing is in range, so the opening tab is returned
bool FSOLSolidPagesSelectInvalidTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLReadOnlyPage page : SOLIDPAGESTEST_PAGES)
    {
        const int32 count = SOLPageTabs::TabCount(page);
        const int32 badTabs[] = { -1, -100, count, count + 1, INDEX_NONE, MAX_int32, MIN_int32 };
        for (int32 current = 0; current < count; ++current)
        {
            for (const int32 requested : badTabs)
            {
                TestEqual(FString::Printf(TEXT("%s SelectTab(%d, bad %d, %d) keeps the tab"),
                    SolidPagesTestName(page), current, requested, count),
                    SOLPageTabs::SelectTab(current, requested, count), current);
            }
        }
        for (const int32 current : badTabs)
        {
            for (const int32 requested : badTabs)
            {
                TestEqual(FString::Printf(TEXT("%s SelectTab(bad %d, bad %d, %d) is the opening tab"),
                    SolidPagesTestName(page), current, requested, count),
                    SOLPageTabs::SelectTab(current, requested, count), SOLPageTabs::OpeningTab());
            }
        }
    }

    // Count 0: no tab is in range, whatever current and requested are
    const int32 anyTabs[] = { 0, 1, -1, 5 };
    for (const int32 current : anyTabs)
    {
        for (const int32 requested : anyTabs)
        {
            TestEqual(FString::Printf(TEXT("SelectTab(%d, %d, 0) is the opening tab"), current, requested),
                SOLPageTabs::SelectTab(current, requested, 0), SOLPageTabs::OpeningTab());
        }
    }

    // Count 1: only tab 0 is in range
    TestEqual(TEXT("SelectTab(0, 1, 1) keeps 0"), SOLPageTabs::SelectTab(0, 1, 1), 0);
    TestEqual(TEXT("SelectTab(1, 0, 1) shows 0"), SOLPageTabs::SelectTab(1, 0, 1), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesIsTabChangeTest, "SOLTest.SolidPages.Select.IsTabChange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A click changes the tab only when it names a different tab in range: a re-click on the shown tab is a no-op (the page
// keeps its scroll), and an out-of-range request changes nothing either
bool FSOLSolidPagesIsTabChangeTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLReadOnlyPage page : SOLIDPAGESTEST_PAGES)
    {
        const int32 count = SOLPageTabs::TabCount(page);
        for (int32 current = 0; current < count; ++current)
        {
            for (int32 requested = 0; requested < count; ++requested)
            {
                TestEqual(FString::Printf(TEXT("%s IsTabChange(%d, %d, %d)"), SolidPagesTestName(page), current,
                    requested, count), SOLPageTabs::IsTabChange(current, requested, count), requested != current);
            }
            const int32 badTabs[] = { -1, count, INDEX_NONE, MAX_int32, MIN_int32 };
            for (const int32 requested : badTabs)
            {
                TestFalse(FString::Printf(TEXT("%s IsTabChange(%d, bad %d, %d)"), SolidPagesTestName(page), current,
                    requested, count), SOLPageTabs::IsTabChange(current, requested, count));
            }
        }
    }
    TestFalse(TEXT("IsTabChange with no tabs"), SOLPageTabs::IsTabChange(0, 0, 0));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Controls row to tab
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesRowEndsTest, "SOLTest.SolidPages.ControlsRow.Ends",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The first row is on the first tab, the last row on the last tab
bool FSOLSolidPagesRowEndsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("first row on tab 0"), SOLPageTabs::ControlsTabOfRow(0), 0);
    TestEqual(TEXT("last row on the last tab"), SOLPageTabs::ControlsTabOfRow(SolidPagesTestRowCount() - 1),
        SolidPagesTestControlsTabCount() - 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesRowMappingTest, "SOLTest.SolidPages.ControlsRow.Mapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Every row lands on a valid Controls tab whose label is the row's area; the tab follows the table's area runs and
// never decreases as the row grows
bool FSOLSolidPagesRowMappingTest::RunTest(const FString& /*parameters*/)
{
    const TArray<int32> expected = SolidPagesTestExpectedTabOfRows();
    const int32 count = SOLPageTabs::TabCount(ESOLReadOnlyPage::Controls);
    int32 previous = 0;
    for (int32 row = 0; row < SolidPagesTestRowCount(); ++row)
    {
        const int32 tab = SOLPageTabs::ControlsTabOfRow(row);
        if (!TestTrue(FString::Printf(TEXT("row %d on a valid tab (got %d)"), row, tab), tab >= 0 && tab < count))
        {
            continue;
        }
        TestEqual(FString::Printf(TEXT("row %d tab"), row), tab, expected[row]);
        TestEqual(FString::Printf(TEXT("row %d tab label is its area"), row),
            SOLPageTabs::TabLabel(ESOLReadOnlyPage::Controls, tab), FString(SOLMenuText::CONTROL_ROWS[row].Area));
        TestTrue(FString::Printf(TEXT("row %d tab %d not before row %d's tab %d"), row, tab, row - 1, previous),
            tab >= previous);
        TestTrue(FString::Printf(TEXT("row %d tab %d at most one past the previous row's"), row, tab),
            tab <= previous + 1);
        previous = tab;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesRowRangeTest, "SOLTest.SolidPages.ControlsRow.OutOfRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A row outside CONTROL_ROWS has no tab (INDEX_NONE)
bool FSOLSolidPagesRowRangeTest::RunTest(const FString& /*parameters*/)
{
    const int32 rows = SolidPagesTestRowCount();
    const int32 badRows[] = { -1, -2, INDEX_NONE, rows, rows + 1, MAX_int32, MIN_int32 };
    for (const int32 row : badRows)
    {
        TestEqual(FString::Printf(TEXT("ControlsTabOfRow(%d)"), row), SOLPageTabs::ControlsTabOfRow(row),
            static_cast<int32>(INDEX_NONE));
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Tab rail geometry (SDD 46 section 6.3)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesTraceShapeTest, "SOLTest.SolidPages.Rail.TraceShape",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A tab's hover trace is a straight horizontal run of two points from the tab's right end to the rail's edge, with no
// ring terminal, and lies within its tab's band
bool FSOLSolidPagesTraceShapeTest::RunTest(const FString& /*parameters*/)
{
    const float startX = SOL::SolidMenu::TAB_COLUMN_WIDTH - SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING -
        SOL::SolidMenu::SETTINGS_TAB_TRACE_PX;
    const float endX = SOL::SolidMenu::TAB_COLUMN_WIDTH - SOL::SolidMenu::SETTINGS_TAB_TRACE_END_PX;
    for (int32 index = 0; index < 8; ++index)
    {
        const FSOLSolidTrace trace = SOLSolidTabRail::TraceForTab(index);
        if (!TestEqual(FString::Printf(TEXT("tab %d trace has two points"), index), trace.Count, 2))
        {
            continue;
        }
        TestFalse(FString::Printf(TEXT("tab %d trace has no ring terminal"), index), trace.bRingTerminal);
        TestEqual(FString::Printf(TEXT("tab %d trace starts at the tab's right end"), index), trace.Points[0].X,
            startX, SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("tab %d trace ends inside the rail's edge"), index), trace.Points[1].X, endX,
            SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("tab %d trace is horizontal"), index), trace.Points[1].Y, trace.Points[0].Y,
            SOLIDPAGESTEST_TOLERANCE);

        // The centre line lies inside the tab: below the tab's top, above its bottom
        const float tabTop = SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING + index * SolidPagesTestTabStep();
        TestTrue(FString::Printf(TEXT("tab %d trace (y %f) within its tab [%f, %f]"), index, trace.Points[0].Y,
            tabTop, tabTop + SOL::MenuStyle::TAB_HEIGHT), trace.Points[0].Y > tabTop &&
            trace.Points[0].Y < tabTop + SOL::MenuStyle::TAB_HEIGHT);
    }
    TestTrue(TEXT("the trace runs rightwards"), endX > startX);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesTraceSpacingTest, "SOLTest.SolidPages.Rail.TraceSpacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Neighbouring tabs' traces are TAB_HEIGHT + BUTTON_GAP apart, at the same x; a negative index reads as tab 0
bool FSOLSolidPagesTraceSpacingTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSolidTrace first = SOLSolidTabRail::TraceForTab(0);
    for (int32 index = 1; index < 8; ++index)
    {
        const FSOLSolidTrace previous = SOLSolidTabRail::TraceForTab(index - 1);
        const FSOLSolidTrace trace = SOLSolidTabRail::TraceForTab(index);
        TestEqual(FString::Printf(TEXT("tab %d trace one step below tab %d's"), index, index - 1),
            trace.Points[0].Y - previous.Points[0].Y, SolidPagesTestTabStep(), SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("tab %d trace %d steps below tab 0's"), index, index),
            trace.Points[0].Y - first.Points[0].Y, index * SolidPagesTestTabStep(), SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("tab %d start x equals tab 0's"), index), trace.Points[0].X, first.Points[0].X,
            SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("tab %d end x equals tab 0's"), index), trace.Points[1].X, first.Points[1].X,
            SOLIDPAGESTEST_TOLERANCE);
    }

    // A negative index reads as tab 0 (same points, count and terminal)
    const int32 negatives[] = { -1, -5, INDEX_NONE, MIN_int32 };
    for (const int32 index : negatives)
    {
        const FSOLSolidTrace trace = SOLSolidTabRail::TraceForTab(index);
        TestEqual(FString::Printf(TEXT("tab %d count equals tab 0's"), index), trace.Count, first.Count);
        TestTrue(FString::Printf(TEXT("tab %d terminal equals tab 0's"), index),
            trace.bRingTerminal == first.bRingTerminal);
        for (int32 point = 0; point < 2; ++point)
        {
            TestEqual(FString::Printf(TEXT("tab %d point %d x equals tab 0's"), index, point), trace.Points[point].X,
                first.Points[point].X, SOLIDPAGESTEST_TOLERANCE);
            TestEqual(FString::Printf(TEXT("tab %d point %d y equals tab 0's"), index, point), trace.Points[point].Y,
                first.Points[point].Y, SOLIDPAGESTEST_TOLERANCE);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesRailHeightTest, "SOLTest.SolidPages.Rail.Height",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// An empty rail is the padding on both sides; each tab adds TAB_HEIGHT + BUTTON_GAP; a negative count reads as 0, and
// every page's rail holds all of its tabs' traces
bool FSOLSolidPagesRailHeightTest::RunTest(const FString& /*parameters*/)
{
    const float empty = 2.0f * SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING;
    TestEqual(TEXT("RailHeight(0) is the padding on both sides"), SOLSolidTabRail::RailHeight(0), empty,
        SOLIDPAGESTEST_TOLERANCE);
    for (int32 tabs = 1; tabs <= 12; ++tabs)
    {
        TestEqual(FString::Printf(TEXT("RailHeight(%d)"), tabs), SOLSolidTabRail::RailHeight(tabs),
            empty + tabs * SolidPagesTestTabStep(), SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("RailHeight(%d) grows one step from %d"), tabs, tabs - 1),
            SOLSolidTabRail::RailHeight(tabs) - SOLSolidTabRail::RailHeight(tabs - 1), SolidPagesTestTabStep(),
            SOLIDPAGESTEST_TOLERANCE);
    }
    const int32 negatives[] = { -1, -10, INDEX_NONE, MIN_int32 };
    for (const int32 tabs : negatives)
    {
        TestEqual(FString::Printf(TEXT("RailHeight(%d) reads as 0 tabs"), tabs), SOLSolidTabRail::RailHeight(tabs),
            empty, SOLIDPAGESTEST_TOLERANCE);
    }

    // Each page's last tab trace fits inside its rail
    for (const ESOLReadOnlyPage page : SOLIDPAGESTEST_PAGES)
    {
        const int32 count = SOLPageTabs::TabCount(page);
        const FSOLSolidTrace last = SOLSolidTabRail::TraceForTab(count - 1);
        TestTrue(FString::Printf(TEXT("%s's last trace inside its rail"), SolidPagesTestName(page)),
            last.Points[0].Y < SOLSolidTabRail::RailHeight(count));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesLeftColumnTest, "SOLTest.SolidPages.LeftColumn.Width",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// (46i) The left column: Controls keeps KEY_COLUMN_WIDTH at every row width; Info keeps LABEL_COLUMN_WIDTH while the
// value column keeps VALUE_COLUMN_MIN_WIDTH after the gap and narrows to LABEL_COLUMN_NARROW_WIDTH below that (the
// 1000x800 rows), the full column for a row not laid out yet (0, negative, NaN, infinite); About has none
bool FSOLSolidPagesLeftColumnTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::MenuStyle;
    const float threshold = LABEL_COLUMN_WIDTH + BUTTON_GAP + VALUE_COLUMN_MIN_WIDTH;
    const float rows[] = { 0.0f, -50.0f, 200.0f, 423.0f, threshold - 1.0f, threshold, 676.0f, 2000.0f,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() };
    for (const float row : rows)
    {
        TestEqual(FString::Printf(TEXT("Controls at %.0f"), row),
            SOLPageTabs::LeftColumnWidth(ESOLReadOnlyPage::Controls, row), KEY_COLUMN_WIDTH, SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("About at %.0f"), row), SOLPageTabs::LeftColumnWidth(ESOLReadOnlyPage::About,
            row), 0.0f, SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("Count at %.0f"), row), SOLPageTabs::LeftColumnWidth(ESOLReadOnlyPage::Count,
            row), 0.0f, SOLIDPAGESTEST_TOLERANCE);
        const bool bNarrow = row > 0.0f && row < threshold;
        TestEqual(FString::Printf(TEXT("Info at %.0f"), row), SOLPageTabs::LeftColumnWidth(ESOLReadOnlyPage::Info, row),
            bNarrow ? LABEL_COLUMN_NARROW_WIDTH : LABEL_COLUMN_WIDTH, SOLIDPAGESTEST_TOLERANCE);
    }

    TestTrue(TEXT("the narrow column is narrower"), LABEL_COLUMN_NARROW_WIDTH < LABEL_COLUMN_WIDTH);

    // (33d-ii, D37) Help: the glossary's symbol box at every row width, no narrow variant
    const float helpRows[] = { 0.0f, -1.0f, -50.0f, 100.0f, 500.0f, 1.0e6f, threshold - 1.0f, threshold,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity() };
    for (const float row : helpRows)
    {
        TestEqual(FString::Printf(TEXT("Help at %.0f"), row), SOLPageTabs::LeftColumnWidth(ESOLReadOnlyPage::Help,
            row), SOL::Help::GLOSSARY_SYMBOL_BOX, SOLIDPAGESTEST_TOLERANCE);
    }
    TestEqual(TEXT("GLOSSARY_SYMBOL_BOX is 96 (today's value)"), SOL::Help::GLOSSARY_SYMBOL_BOX, 96.0f,
        SOLIDPAGESTEST_TOLERANCE);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// (33d-ii) Tab rail geometry with group captions (SDD 33 D22)
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesCaptionTabTopTest, "SOLTest.SolidPages.Rail.CaptionTabTop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A tab's top is the padding, one tab step per tab above it and one caption step per caption row up to and including
// its own; with no captions it is the caption-less layout; an index past the view counts every flag of the view
bool FSOLSolidPagesCaptionTabTopTest::RunTest(const FString& /*parameters*/)
{
    const bool none[] = { false, false, false, false };
    const bool all[] = { true, true, true, true };
    const bool alternate[] = { false, true, false, true, false };
    const bool lastOnly[] = { false, false, true };
    const TConstArrayView<bool> views[] = { TConstArrayView<bool>(), MakeArrayView(none), MakeArrayView(all),
        MakeArrayView(alternate), MakeArrayView(lastOnly), MakeArrayView(SOLIDPAGESTEST_HELP_CAPTIONED) };
    for (int32 v = 0; v < static_cast<int32>(UE_ARRAY_COUNT(views)); ++v)
    {
        for (int32 index = 0; index < 9; ++index)
        {
            TestEqual(FString::Printf(TEXT("view %d TabTop(%d)"), v, index), SOLSolidTabRail::TabTop(index, views[v]),
                SolidPagesTestExpectedTabTop(index, views[v]), SOLIDPAGESTEST_TOLERANCE);
        }
    }

    // With an empty view or all-false flags it is the caption-less layout, P + i x (T + G)
    for (int32 index = 0; index < 9; ++index)
    {
        const float plain = SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING + index * SolidPagesTestTabStep();
        TestEqual(FString::Printf(TEXT("TabTop(%d, {})"), index), SOLSolidTabRail::TabTop(index,
            TConstArrayView<bool>()), plain, SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("TabTop(%d, all false)"), index), SOLSolidTabRail::TabTop(index,
            MakeArrayView(none)), plain, SOLIDPAGESTEST_TOLERANCE);
    }

    // Past the view: every flag of the view counts (all four captioned, index 6 = P + 6 steps + 4 captions)
    TestEqual(TEXT("TabTop(6, all four captioned)"), SOLSolidTabRail::TabTop(6, MakeArrayView(all)),
        SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING + 6 * SolidPagesTestTabStep() + 4 * SolidPagesTestCaptionStep(),
        SOLIDPAGESTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesCaptionTopTest, "SOLTest.SolidPages.Rail.CaptionTop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A caption row sits one caption step above its tab and right after the previous tab's gap (no overlap); a tab
// without a caption, and an index outside the view, has none (-1)
bool FSOLSolidPagesCaptionTopTest::RunTest(const FString& /*parameters*/)
{
    const bool all[] = { true, true, true, true };
    const bool alternate[] = { false, true, false, true, false };
    const TConstArrayView<bool> views[] = { MakeArrayView(all), MakeArrayView(alternate),
        MakeArrayView(SOLIDPAGESTEST_HELP_CAPTIONED) };
    for (int32 v = 0; v < static_cast<int32>(UE_ARRAY_COUNT(views)); ++v)
    {
        const TConstArrayView<bool> view = views[v];
        for (int32 index = 0; index < view.Num(); ++index)
        {
            const float caption = SOLSolidTabRail::CaptionTop(index, view);
            if (!view[index])
            {
                TestEqual(FString::Printf(TEXT("view %d tab %d has no caption"), v, index), caption, -1.0f,
                    SOLIDPAGESTEST_TOLERANCE);
                continue;
            }
            TestEqual(FString::Printf(TEXT("view %d CaptionTop(%d) is one caption step above its tab"), v, index),
                caption, SolidPagesTestExpectedTabTop(index, view) - SolidPagesTestCaptionStep(),
                SOLIDPAGESTEST_TOLERANCE);
            TestEqual(FString::Printf(TEXT("view %d caption %d ends at its tab"), v, index),
                caption + SolidPagesTestCaptionStep(), SOLSolidTabRail::TabTop(index, view), SOLIDPAGESTEST_TOLERANCE);
            if (index >= 1)
            {
                TestEqual(FString::Printf(TEXT("view %d caption %d starts after tab %d's gap"), v, index, index - 1),
                    SOLSolidTabRail::TabTop(index - 1, view) + SolidPagesTestTabStep(), caption,
                    SOLIDPAGESTEST_TOLERANCE);
            }
            else
            {
                TestEqual(FString::Printf(TEXT("view %d caption 0 starts at the padding"), v), caption,
                    SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING, SOLIDPAGESTEST_TOLERANCE);
            }
        }

        // Outside the view: no caption
        const int32 outside[] = { -1, view.Num(), view.Num() + 3, INDEX_NONE, MIN_int32, MAX_int32 };
        for (const int32 index : outside)
        {
            TestEqual(FString::Printf(TEXT("view %d CaptionTop(%d) outside the view"), v, index),
                SOLSolidTabRail::CaptionTop(index, view), -1.0f, SOLIDPAGESTEST_TOLERANCE);
        }
    }

    // An empty view has no caption anywhere
    for (int32 index = -1; index < 4; ++index)
    {
        TestEqual(FString::Printf(TEXT("CaptionTop(%d, {})"), index),
            SOLSolidTabRail::CaptionTop(index, TConstArrayView<bool>()), -1.0f, SOLIDPAGESTEST_TOLERANCE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesCaptionHeightTest, "SOLTest.SolidPages.Rail.CaptionHeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The rail height is the caption-less height plus one caption step per flag among the first tabCount tabs; a negative
// count reads 0 (the padding on both sides); with no captions it equals RailHeight(n)
bool FSOLSolidPagesCaptionHeightTest::RunTest(const FString& /*parameters*/)
{
    const bool none[] = { false, false, false };
    const bool all[] = { true, true, true, true };
    const bool alternate[] = { false, true, false, true, false };
    const TConstArrayView<bool> views[] = { TConstArrayView<bool>(), MakeArrayView(none), MakeArrayView(all),
        MakeArrayView(alternate), MakeArrayView(SOLIDPAGESTEST_HELP_CAPTIONED) };
    for (int32 v = 0; v < static_cast<int32>(UE_ARRAY_COUNT(views)); ++v)
    {
        for (int32 tabs = 0; tabs <= 8; ++tabs)
        {
            TestEqual(FString::Printf(TEXT("view %d RailHeight(%d)"), v, tabs),
                SOLSolidTabRail::RailHeight(tabs, views[v]), SolidPagesTestExpectedRailHeight(tabs, views[v]),
                SOLIDPAGESTEST_TOLERANCE);
        }
        const int32 negatives[] = { -1, -10, INDEX_NONE, MIN_int32 };
        for (const int32 tabs : negatives)
        {
            TestEqual(FString::Printf(TEXT("view %d RailHeight(%d) reads as 0 tabs"), v, tabs),
                SOLSolidTabRail::RailHeight(tabs, views[v]), 2.0f * SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING,
                SOLIDPAGESTEST_TOLERANCE);
        }
    }

    // No captions: the caption-less overload
    for (int32 tabs = -1; tabs <= 8; ++tabs)
    {
        TestEqual(FString::Printf(TEXT("RailHeight(%d, {}) == RailHeight(%d)"), tabs, tabs),
            SOLSolidTabRail::RailHeight(tabs, TConstArrayView<bool>()), SOLSolidTabRail::RailHeight(tabs),
            SOLIDPAGESTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("RailHeight(%d, all false) == RailHeight(%d)"), tabs, tabs),
            SOLSolidTabRail::RailHeight(tabs, MakeArrayView(none)), SOLSolidTabRail::RailHeight(tabs),
            SOLIDPAGESTEST_TOLERANCE);
    }

    // Only the first tabCount flags count: one tab of a fully captioned view adds one caption step
    TestEqual(TEXT("RailHeight(1, all captioned)"), SOLSolidTabRail::RailHeight(1, MakeArrayView(all)),
        SOLSolidTabRail::RailHeight(1) + SolidPagesTestCaptionStep(), SOLIDPAGESTEST_TOLERANCE);

    // Every tab and caption lies inside its rail
    for (int32 v = 0; v < static_cast<int32>(UE_ARRAY_COUNT(views)); ++v)
    {
        const int32 count = FMath::Max(views[v].Num(), 1);
        TestTrue(FString::Printf(TEXT("view %d last tab inside its rail"), v),
            SOLSolidTabRail::TabTop(count - 1, views[v]) + SOL::MenuStyle::TAB_HEIGHT <=
            SOLSolidTabRail::RailHeight(count, views[v]) - SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING +
            SOLIDPAGESTEST_TOLERANCE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesCaptionTraceTest, "SOLTest.SolidPages.Rail.CaptionTrace",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A tab's hover trace on a captioned rail is the caption-less trace moved to the tab's centre line (same x start and
// end, two points, no ring terminal); with no captions it equals the caption-less trace
bool FSOLSolidPagesCaptionTraceTest::RunTest(const FString& /*parameters*/)
{
    const bool none[] = { false, false, false, false };
    const bool all[] = { true, true, true, true };
    const bool alternate[] = { false, true, false, true, false };
    const TConstArrayView<bool> views[] = { TConstArrayView<bool>(), MakeArrayView(none), MakeArrayView(all),
        MakeArrayView(alternate), MakeArrayView(SOLIDPAGESTEST_HELP_CAPTIONED) };
    const int32 indices[] = { 0, 1, 2, 3, 4, 5, 7, -1, INDEX_NONE };
    for (int32 v = 0; v < static_cast<int32>(UE_ARRAY_COUNT(views)); ++v)
    {
        for (const int32 index : indices)
        {
            const FSOLSolidTrace plain = SOLSolidTabRail::TraceForTab(index);
            const FSOLSolidTrace trace = SOLSolidTabRail::TraceForTab(index, views[v]);
            if (!TestEqual(FString::Printf(TEXT("view %d tab %d trace has two points"), v, index), trace.Count, 2))
            {
                continue;
            }
            TestFalse(FString::Printf(TEXT("view %d tab %d trace has no ring terminal"), v, index),
                trace.bRingTerminal);
            const float centre = SOLSolidTabRail::TabTop(index, views[v]) + 0.5f * SOL::MenuStyle::TAB_HEIGHT;
            for (int32 point = 0; point < 2; ++point)
            {
                TestEqual(FString::Printf(TEXT("view %d tab %d point %d x unchanged"), v, index, point),
                    trace.Points[point].X, plain.Points[point].X, SOLIDPAGESTEST_TOLERANCE);
                TestEqual(FString::Printf(TEXT("view %d tab %d point %d y on the tab's centre line"), v, index, point),
                    trace.Points[point].Y, centre, SOLIDPAGESTEST_TOLERANCE);
            }
        }
    }

    // No captions: exactly the caption-less trace
    for (int32 index = 0; index < 8; ++index)
    {
        const FSOLSolidTrace plain = SOLSolidTabRail::TraceForTab(index);
        const FSOLSolidTrace trace = SOLSolidTabRail::TraceForTab(index, TConstArrayView<bool>());
        TestEqual(FString::Printf(TEXT("TraceForTab(%d, {}) count"), index), trace.Count, plain.Count);
        TestTrue(FString::Printf(TEXT("TraceForTab(%d, {}) terminal"), index),
            trace.bRingTerminal == plain.bRingTerminal);
        for (int32 point = 0; point < 2; ++point)
        {
            TestEqual(FString::Printf(TEXT("TraceForTab(%d, {}) point %d y"), index, point), trace.Points[point].Y,
                plain.Points[point].Y, SOLIDPAGESTEST_TOLERANCE);
            TestEqual(FString::Printf(TEXT("TraceForTab(%d, {}) point %d x"), index, point), trace.Points[point].X,
                plain.Points[point].X, SOLIDPAGESTEST_TOLERANCE);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesHelpRailTest, "SOLTest.SolidPages.Rail.HelpCaptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The Help rail (captions from SOLPageTabs::TabCaption): tab tops 42, 132, 188, 244, 300, caption tops 8 and 98, height
// 364 with today's constants, and no caption row overlaps a tab
bool FSOLSolidPagesHelpRailTest::RunTest(const FString& /*parameters*/)
{
    // The flags as the page derives them: a caption row above every tab with a non-empty caption
    const int32 count = SOLPageTabs::TabCount(ESOLReadOnlyPage::Help);
    TArray<bool> captioned;
    for (int32 tab = 0; tab < count; ++tab)
    {
        captioned.Add(!SOLPageTabs::TabCaption(ESOLReadOnlyPage::Help, tab).IsEmpty());
    }
    if (!TestEqual(TEXT("Help flags match {true, true, false, false, false}"), captioned.Num(),
        static_cast<int32>(UE_ARRAY_COUNT(SOLIDPAGESTEST_HELP_CAPTIONED))))
    {
        return true;
    }
    for (int32 tab = 0; tab < count; ++tab)
    {
        TestTrue(FString::Printf(TEXT("Help tab %d captioned"), tab),
            captioned[tab] == SOLIDPAGESTEST_HELP_CAPTIONED[tab]);
    }

    // Formula against the constants
    for (int32 tab = 0; tab < count; ++tab)
    {
        TestEqual(FString::Printf(TEXT("Help TabTop(%d)"), tab), SOLSolidTabRail::TabTop(tab, captioned),
            SolidPagesTestExpectedTabTop(tab, captioned), SOLIDPAGESTEST_TOLERANCE);
    }
    TestEqual(TEXT("Help RailHeight"), SOLSolidTabRail::RailHeight(count, captioned),
        SolidPagesTestExpectedRailHeight(count, captioned), SOLIDPAGESTEST_TOLERANCE);

    // Today's numbers (P 8, T 46, G 10, C 24)
    TestEqual(TEXT("TAB_RAIL_CAPTION_HEIGHT is 24 (today's value)"), SOL::SolidMenu::TAB_RAIL_CAPTION_HEIGHT, 24.0f,
        SOLIDPAGESTEST_TOLERANCE);
    const float tops[] = { 42.0f, 132.0f, 188.0f, 244.0f, 300.0f };
    static_assert(UE_ARRAY_COUNT(tops) == SOLIDPAGESTEST_HELP_TABS, "one top per Help tab");
    for (int32 tab = 0; tab < SOLIDPAGESTEST_HELP_TABS; ++tab)
    {
        TestEqual(FString::Printf(TEXT("Help TabTop(%d) is %.0f"), tab, tops[tab]),
            SOLSolidTabRail::TabTop(tab, captioned), tops[tab], SOLIDPAGESTEST_TOLERANCE);
    }
    TestEqual(TEXT("Help CaptionTop(0) is 8"), SOLSolidTabRail::CaptionTop(0, captioned), 8.0f,
        SOLIDPAGESTEST_TOLERANCE);
    TestEqual(TEXT("Help CaptionTop(1) is 98"), SOLSolidTabRail::CaptionTop(1, captioned), 98.0f,
        SOLIDPAGESTEST_TOLERANCE);
    for (int32 tab = 2; tab < SOLIDPAGESTEST_HELP_TABS; ++tab)
    {
        TestEqual(FString::Printf(TEXT("Help CaptionTop(%d) is -1"), tab), SOLSolidTabRail::CaptionTop(tab, captioned),
            -1.0f, SOLIDPAGESTEST_TOLERANCE);
    }
    TestEqual(TEXT("Help RailHeight is 364"), SOLSolidTabRail::RailHeight(count, captioned), 364.0f,
        SOLIDPAGESTEST_TOLERANCE);

    // No caption row overlaps a tab, and every trace sits inside its own tab
    for (int32 tab = 0; tab < count; ++tab)
    {
        const float top = SOLSolidTabRail::TabTop(tab, captioned);
        const FSOLSolidTrace trace = SOLSolidTabRail::TraceForTab(tab, captioned);
        TestTrue(FString::Printf(TEXT("Help tab %d trace inside its tab"), tab),
            trace.Points[0].Y > top && trace.Points[0].Y < top + SOL::MenuStyle::TAB_HEIGHT);
        if (!captioned[tab])
        {
            continue;
        }
        const float caption = SOLSolidTabRail::CaptionTop(tab, captioned);
        TestEqual(FString::Printf(TEXT("Help caption %d + C + G is its tab's top"), tab),
            caption + SolidPagesTestCaptionStep(), top, SOLIDPAGESTEST_TOLERANCE);
        if (tab >= 1)
        {
            TestEqual(FString::Printf(TEXT("Help tab %d + T + G is caption %d's top"), tab - 1, tab),
                SOLSolidTabRail::TabTop(tab - 1, captioned) + SolidPagesTestTabStep(), caption,
                SOLIDPAGESTEST_TOLERANCE);
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
