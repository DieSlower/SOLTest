/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLMenuText.h"
#include "Menu/SOLPageTabs.h"
#include "Menu/SOLSolidTabRail.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

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
//   }
//
//   namespace SOLSolidTabRail
//   {
//       FSOLSolidTrace TraceForTab(int32 index);             // two points, no ring terminal, negative = tab 0
//       float RailHeight(int32 tabCount);                    // padding both sides + each tab's height and gap
//   }
namespace
{
    constexpr int32 SOLIDPAGESTEST_INFO_TABS = 4;               // Flight, Time and travel, Combat, How it works
    constexpr int32 SOLIDPAGESTEST_ABOUT_TABS = 2;              // Game, Credits
    constexpr uint8 SOLIDPAGESTEST_BAD_PAGE = 200;              // A page value well past Count
    constexpr float SOLIDPAGESTEST_TOLERANCE = 1.0e-3f;         // Geometry comparisons (rail local units)

    // The three pages with a tab column
    constexpr ESOLReadOnlyPage SOLIDPAGESTEST_PAGES[] = { ESOLReadOnlyPage::Controls, ESOLReadOnlyPage::Info,
        ESOLReadOnlyPage::About };

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
    for (int32 open = 0; open < 10; ++open)
    {
        TestEqual(FString::Printf(TEXT("open %d"), open), SOLPageTabs::OpeningTab(), 0);
    }
    for (const ESOLReadOnlyPage page : SOLIDPAGESTEST_PAGES)
    {
        TestTrue(FString::Printf(TEXT("%s's opening tab is one of its tabs"), SolidPagesTestName(page)),
            SOLPageTabs::OpeningTab() < SOLPageTabs::TabCount(page));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidPagesTabCountTest, "SOLTest.SolidPages.Tabs.Count",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Controls: one tab per area run of CONTROL_ROWS; Info: 4; About: 2; Count and past it: 0
bool FSOLSolidPagesTabCountTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("ESOLReadOnlyPage::Count is 3"), static_cast<int32>(ESOLReadOnlyPage::Count), 3);
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

#endif // WITH_DEV_AUTOMATION_TESTS
