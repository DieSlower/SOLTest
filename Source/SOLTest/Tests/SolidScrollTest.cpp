/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidScroll.h"
#include "Menu/SOLSolidTabRail.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #46 Part 46g scrolling rails and stacks (Docs/SDDs/46-solid-menus.md D17), written against the
// Menu/SOLSolidScroll.h contract before the production code existed. A stack is count items of itemHeight, each
// followed by gap, inside padding at both ends; offsets are the content's scroll offset (0 = top, positive = scrolled
// down). The tolerance and the stack's visible entries come from SOL::SolidMenu; the stack geometry below is test
// input only (any positive values do)
namespace
{
    constexpr float SOLIDSCROLLTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float SOLIDSCROLLTEST_INF = std::numeric_limits<float>::infinity();
    constexpr float SOLIDSCROLLTEST_TOLERANCE = 1.0e-4f;

    // A generic test stack: items [PAD + i x 50, PAD + i x 50 + 40]
    constexpr float SOLIDSCROLLTEST_ITEM = 40.0f;
    constexpr float SOLIDSCROLLTEST_GAP = 10.0f;
    constexpr float SOLIDSCROLLTEST_PAD = 5.0f;
    constexpr int32 SOLIDSCROLLTEST_COUNT = 10;

    // A generic content and view for the offset maths
    constexpr float SOLIDSCROLLTEST_CONTENT = 1000.0f;
    constexpr float SOLIDSCROLLTEST_VIEW = 300.0f;
    constexpr float SOLIDSCROLLTEST_MAX = SOLIDSCROLLTEST_CONTENT - SOLIDSCROLLTEST_VIEW;

    //////////////////////////////////////////////////////////////////////////
    // Returns the test stack's item top, computed here (independent of production)
    float SolidScrollTestTop(const int32 index)
    {
        return SOLIDSCROLLTEST_PAD + index * (SOLIDSCROLLTEST_ITEM + SOLIDSCROLLTEST_GAP);
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Constants and offset limits
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollConstantsTest, "SOLTest.SolidScroll.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The fit tolerance is a small positive amount and the stack shows at least one entry
bool FSOLSolidScrollConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("SCROLL_FIT_TOLERANCE_PX > 0"), SOL::SolidMenu::SCROLL_FIT_TOLERANCE_PX > 0.0f);
    TestTrue(TEXT("SCROLL_FIT_TOLERANCE_PX below one item"),
        SOL::SolidMenu::SCROLL_FIT_TOLERANCE_PX < SOL::MenuStyle::TAB_HEIGHT);
    TestTrue(TEXT("STACK_MAX_VISIBLE_ENTRIES > 0"), SOL::SolidMenu::STACK_MAX_VISIBLE_ENTRIES > 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollMaxOffsetTest, "SOLTest.SolidScroll.MaxOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// max(0, content - view); a negative or non-finite height reads 0
bool FSOLSolidScrollMaxOffsetTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("taller content"), SOLSolidScroll::MaxOffset(500.0f, 300.0f), 200.0f, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("shorter content"), SOLSolidScroll::MaxOffset(300.0f, 500.0f), 0.0f, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("equal heights"), SOLSolidScroll::MaxOffset(300.0f, 300.0f), 0.0f, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("both 0"), SOLSolidScroll::MaxOffset(0.0f, 0.0f), 0.0f, SOLIDSCROLLTEST_TOLERANCE);

    // Bad content reads 0, so nothing scrolls
    TestEqual(TEXT("negative content"), SOLSolidScroll::MaxOffset(-50.0f, 300.0f), 0.0f, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("NaN content"), SOLSolidScroll::MaxOffset(SOLIDSCROLLTEST_NAN, 300.0f), 0.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("infinite content"), SOLSolidScroll::MaxOffset(SOLIDSCROLLTEST_INF, 300.0f), 0.0f,
        SOLIDSCROLLTEST_TOLERANCE);

    // A bad view reads 0, so the whole content is the scroll range
    TestEqual(TEXT("negative view"), SOLSolidScroll::MaxOffset(500.0f, -10.0f), 500.0f, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("NaN view"), SOLSolidScroll::MaxOffset(500.0f, SOLIDSCROLLTEST_NAN), 500.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("infinite view"), SOLSolidScroll::MaxOffset(500.0f, SOLIDSCROLLTEST_INF), 500.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollNeedsScrollTest, "SOLTest.SolidScroll.NeedsScroll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Content scrolls only when taller than the view by more than SCROLL_FIT_TOLERANCE_PX
bool FSOLSolidScrollNeedsScrollTest::RunTest(const FString& /*parameters*/)
{
    const float view = SOLIDSCROLLTEST_VIEW;
    const float tol = SOL::SolidMenu::SCROLL_FIT_TOLERANCE_PX;
    TestFalse(TEXT("shorter content fits"), SOLSolidScroll::NeedsScroll(view - 50.0f, view));
    TestFalse(TEXT("equal content fits"), SOLSolidScroll::NeedsScroll(view, view));
    TestFalse(TEXT("just below the tolerance fits"), SOLSolidScroll::NeedsScroll(view + 0.5f * tol, view));
    TestFalse(TEXT("exactly the tolerance fits (more than, not at)"), SOLSolidScroll::NeedsScroll(view + tol, view));
    TestTrue(TEXT("just above the tolerance scrolls"), SOLSolidScroll::NeedsScroll(view + 2.0f * tol, view));
    TestTrue(TEXT("much taller scrolls"), SOLSolidScroll::NeedsScroll(SOLIDSCROLLTEST_CONTENT, view));

    // Heights read as MaxOffset reads them
    TestFalse(TEXT("NaN content fits"), SOLSolidScroll::NeedsScroll(SOLIDSCROLLTEST_NAN, view));
    TestFalse(TEXT("negative content fits"), SOLSolidScroll::NeedsScroll(-100.0f, view));
    TestTrue(TEXT("NaN view reads 0: content scrolls"), SOLSolidScroll::NeedsScroll(view, SOLIDSCROLLTEST_NAN));
    TestFalse(TEXT("both 0 fits"), SOLSolidScroll::NeedsScroll(0.0f, 0.0f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollClampOffsetTest, "SOLTest.SolidScroll.ClampOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The offset clamped to [0, MaxOffset]; NaN reads 0
bool FSOLSolidScrollClampOffsetTest::RunTest(const FString& /*parameters*/)
{
    const float content = SOLIDSCROLLTEST_CONTENT;
    const float view = SOLIDSCROLLTEST_VIEW;
    TestEqual(TEXT("inside unchanged"), SOLSolidScroll::ClampOffset(250.0f, content, view), 250.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("0 unchanged"), SOLSolidScroll::ClampOffset(0.0f, content, view), 0.0f, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("max unchanged"), SOLSolidScroll::ClampOffset(SOLIDSCROLLTEST_MAX, content, view),
        SOLIDSCROLLTEST_MAX, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("negative to 0"), SOLSolidScroll::ClampOffset(-40.0f, content, view), 0.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("past the end to max"), SOLSolidScroll::ClampOffset(5000.0f, content, view), SOLIDSCROLLTEST_MAX,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("NaN to 0"), SOLSolidScroll::ClampOffset(SOLIDSCROLLTEST_NAN, content, view), 0.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("+inf to max"), SOLSolidScroll::ClampOffset(SOLIDSCROLLTEST_INF, content, view),
        SOLIDSCROLLTEST_MAX, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("-inf to 0"), SOLSolidScroll::ClampOffset(-SOLIDSCROLLTEST_INF, content, view), 0.0f,
        SOLIDSCROLLTEST_TOLERANCE);

    // Content that fits: every offset is 0
    const float offsets[] = { -10.0f, 0.0f, 25.0f, 1000.0f };
    for (const float offset : offsets)
    {
        TestEqual(FString::Printf(TEXT("fitting content, offset %f"), offset),
            SOLSolidScroll::ClampOffset(offset, view - 20.0f, view), 0.0f, SOLIDSCROLLTEST_TOLERANCE);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Scrolling an item into view
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollShowTest, "SOLTest.SolidScroll.OffsetToShow.Moves",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// An item already shown whole keeps the offset; one above scrolls up to its top; one below scrolls down to its bottom
bool FSOLSolidScrollShowTest::RunTest(const FString& /*parameters*/)
{
    const float content = SOLIDSCROLLTEST_CONTENT;
    const float view = SOLIDSCROLLTEST_VIEW;

    // Already whole in [100, 400]: unchanged, edges included
    TestEqual(TEXT("visible inside"), SOLSolidScroll::OffsetToShow(100.0f, 150.0f, 46.0f, content, view), 100.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("visible on the top edge"), SOLSolidScroll::OffsetToShow(100.0f, 100.0f, 46.0f, content, view),
        100.0f, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("visible on the bottom edge"), SOLSolidScroll::OffsetToShow(100.0f, 354.0f, 46.0f, content, view),
        100.0f, SOLIDSCROLLTEST_TOLERANCE);

    // Above (wholly or partly): its top
    TestEqual(TEXT("wholly above"), SOLSolidScroll::OffsetToShow(400.0f, 150.0f, 46.0f, content, view), 150.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("partly above"), SOLSolidScroll::OffsetToShow(170.0f, 150.0f, 46.0f, content, view), 150.0f,
        SOLIDSCROLLTEST_TOLERANCE);

    // Below (wholly or partly): its bottom at the view's bottom
    TestEqual(TEXT("wholly below"), SOLSolidScroll::OffsetToShow(0.0f, 400.0f, 46.0f, content, view),
        400.0f + 46.0f - view, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("partly below"), SOLSolidScroll::OffsetToShow(0.0f, 280.0f, 46.0f, content, view),
        280.0f + 46.0f - view, SOLIDSCROLLTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollShowEdgesTest, "SOLTest.SolidScroll.OffsetToShow.Limits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// An item taller than the view shows from its top; the result is always clamped to [0, MaxOffset]
bool FSOLSolidScrollShowEdgesTest::RunTest(const FString& /*parameters*/)
{
    const float content = SOLIDSCROLLTEST_CONTENT;
    const float view = SOLIDSCROLLTEST_VIEW;

    // Taller than the view: its top, from above, below or inside it
    TestEqual(TEXT("taller, from above"), SOLSolidScroll::OffsetToShow(0.0f, 400.0f, 350.0f, content, view), 400.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("taller, from inside"), SOLSolidScroll::OffsetToShow(450.0f, 400.0f, 350.0f, content, view),
        400.0f, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("taller, from below"), SOLSolidScroll::OffsetToShow(650.0f, 400.0f, 350.0f, content, view),
        400.0f, SOLIDSCROLLTEST_TOLERANCE);

    // At the content's end: clamped to MaxOffset
    TestEqual(TEXT("bottom past the content end"), SOLSolidScroll::OffsetToShow(0.0f, 980.0f, 46.0f, content, view),
        SOLIDSCROLLTEST_MAX, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("taller item near the end"), SOLSolidScroll::OffsetToShow(0.0f, 900.0f, 350.0f, content, view),
        SOLIDSCROLLTEST_MAX, SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("last item exactly at the end"), SOLSolidScroll::OffsetToShow(0.0f, 954.0f, 46.0f, content, view),
        SOLIDSCROLLTEST_MAX, SOLIDSCROLLTEST_TOLERANCE);

    // Above the content's start: clamped to 0
    TestEqual(TEXT("item above the content top"), SOLSolidScroll::OffsetToShow(200.0f, -30.0f, 20.0f, content, view),
        0.0f, SOLIDSCROLLTEST_TOLERANCE);

    // Out-of-range starting offset with the item shown: still clamped
    TestEqual(TEXT("negative offset clamped"), SOLSolidScroll::OffsetToShow(-20.0f, 50.0f, 46.0f, content, view),
        0.0f, SOLIDSCROLLTEST_TOLERANCE);

    // Content that fits: always 0
    TestEqual(TEXT("fitting content"), SOLSolidScroll::OffsetToShow(0.0f, 250.0f, 46.0f, 280.0f, view), 0.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollShowNonFiniteTest, "SOLTest.SolidScroll.OffsetToShow.NonFinite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A non-finite item top or height, or a negative height, returns ClampOffset(offset)
bool FSOLSolidScrollShowNonFiniteTest::RunTest(const FString& /*parameters*/)
{
    const float content = SOLIDSCROLLTEST_CONTENT;
    const float view = SOLIDSCROLLTEST_VIEW;
    const float offsets[] = { 250.0f, -40.0f, 900.0f, SOLIDSCROLLTEST_NAN };
    for (const float offset : offsets)
    {
        const float expected = SOLSolidScroll::ClampOffset(offset, content, view);
        TestEqual(FString::Printf(TEXT("NaN top, offset %f"), offset),
            SOLSolidScroll::OffsetToShow(offset, SOLIDSCROLLTEST_NAN, 46.0f, content, view), expected,
            SOLIDSCROLLTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("+inf top, offset %f"), offset),
            SOLSolidScroll::OffsetToShow(offset, SOLIDSCROLLTEST_INF, 46.0f, content, view), expected,
            SOLIDSCROLLTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("NaN height, offset %f"), offset),
            SOLSolidScroll::OffsetToShow(offset, 600.0f, SOLIDSCROLLTEST_NAN, content, view), expected,
            SOLIDSCROLLTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("+inf height, offset %f"), offset),
            SOLSolidScroll::OffsetToShow(offset, 600.0f, SOLIDSCROLLTEST_INF, content, view), expected,
            SOLIDSCROLLTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("negative height, offset %f"), offset),
            SOLSolidScroll::OffsetToShow(offset, 600.0f, -46.0f, content, view), expected, SOLIDSCROLLTEST_TOLERANCE);
    }

    // Whatever a NaN offset does with a valid item, the result is a finite offset in range
    const float fromNan = SOLSolidScroll::OffsetToShow(SOLIDSCROLLTEST_NAN, 400.0f, 46.0f, content, view);
    TestTrue(TEXT("NaN offset gives a finite offset in [0, max]"), FMath::IsFinite(fromNan) && fromNan >= 0.0f &&
        fromNan <= SOLIDSCROLLTEST_MAX);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Stack geometry
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollItemTopTest, "SOLTest.SolidScroll.Stack.ItemTop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// padding + index x (itemHeight + gap); a negative index reads 0 (the first item's top)
bool FSOLSolidScrollItemTopTest::RunTest(const FString& /*parameters*/)
{
    for (int32 index = 0; index < SOLIDSCROLLTEST_COUNT; ++index)
    {
        TestEqual(FString::Printf(TEXT("item %d top"), index), SOLSolidScroll::StackItemTop(index,
            SOLIDSCROLLTEST_ITEM, SOLIDSCROLLTEST_GAP, SOLIDSCROLLTEST_PAD), SolidScrollTestTop(index),
            SOLIDSCROLLTEST_TOLERANCE);
    }
    const int32 negatives[] = { -1, -7, INDEX_NONE, MIN_int32 };
    for (const int32 index : negatives)
    {
        TestEqual(FString::Printf(TEXT("index %d reads 0"), index), SOLSolidScroll::StackItemTop(index,
            SOLIDSCROLLTEST_ITEM, SOLIDSCROLLTEST_GAP, SOLIDSCROLLTEST_PAD), SOLIDSCROLLTEST_PAD,
            SOLIDSCROLLTEST_TOLERANCE);
    }
    TestEqual(TEXT("no padding, item 2"), SOLSolidScroll::StackItemTop(2, 30.0f, 4.0f, 0.0f), 68.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollContentHeightTest, "SOLTest.SolidScroll.Stack.ContentHeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 2 x padding + count x (itemHeight + gap); a negative count reads 0
bool FSOLSolidScrollContentHeightTest::RunTest(const FString& /*parameters*/)
{
    const float empty = 2.0f * SOLIDSCROLLTEST_PAD;
    TestEqual(TEXT("count 0"), SOLSolidScroll::StackContentHeight(0, SOLIDSCROLLTEST_ITEM, SOLIDSCROLLTEST_GAP,
        SOLIDSCROLLTEST_PAD), empty, SOLIDSCROLLTEST_TOLERANCE);
    for (int32 count = 1; count <= 12; ++count)
    {
        TestEqual(FString::Printf(TEXT("count %d"), count), SOLSolidScroll::StackContentHeight(count,
            SOLIDSCROLLTEST_ITEM, SOLIDSCROLLTEST_GAP, SOLIDSCROLLTEST_PAD),
            empty + count * (SOLIDSCROLLTEST_ITEM + SOLIDSCROLLTEST_GAP), SOLIDSCROLLTEST_TOLERANCE);
    }
    const int32 negatives[] = { -1, -12, INDEX_NONE, MIN_int32 };
    for (const int32 count : negatives)
    {
        TestEqual(FString::Printf(TEXT("count %d reads 0"), count), SOLSolidScroll::StackContentHeight(count,
            SOLIDSCROLLTEST_ITEM, SOLIDSCROLLTEST_GAP, SOLIDSCROLLTEST_PAD), empty, SOLIDSCROLLTEST_TOLERANCE);
    }

    // The last item's bottom plus its gap and the end padding is the content height
    const float lastBottom = SOLSolidScroll::StackItemTop(SOLIDSCROLLTEST_COUNT - 1, SOLIDSCROLLTEST_ITEM,
        SOLIDSCROLLTEST_GAP, SOLIDSCROLLTEST_PAD) + SOLIDSCROLLTEST_ITEM;
    TestEqual(TEXT("last item bottom + gap + padding"), SOLSolidScroll::StackContentHeight(SOLIDSCROLLTEST_COUNT,
        SOLIDSCROLLTEST_ITEM, SOLIDSCROLLTEST_GAP, SOLIDSCROLLTEST_PAD),
        lastBottom + SOLIDSCROLLTEST_GAP + SOLIDSCROLLTEST_PAD, SOLIDSCROLLTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollRailMatchTest, "SOLTest.SolidScroll.Stack.MatchesTabRail",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// With the tab rail's geometry the stack content height is SOLSolidTabRail::RailHeight, 10 tabs (the Controls page)
// included
bool FSOLSolidScrollRailMatchTest::RunTest(const FString& /*parameters*/)
{
    const float tab = SOL::MenuStyle::TAB_HEIGHT;
    const float gap = SOL::MenuStyle::BUTTON_GAP;
    const float pad = SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING;
    for (int32 count = 0; count <= 12; ++count)
    {
        TestEqual(FString::Printf(TEXT("%d tabs: StackContentHeight == RailHeight"), count),
            SOLSolidScroll::StackContentHeight(count, tab, gap, pad), SOLSolidTabRail::RailHeight(count),
            SOLIDSCROLLTEST_TOLERANCE);
    }
    TestEqual(TEXT("negative count matches RailHeight(-1)"), SOLSolidScroll::StackContentHeight(-1, tab, gap, pad),
        SOLSolidTabRail::RailHeight(-1), SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("the Controls page's 10 tabs"), SOLSolidScroll::StackContentHeight(10, tab, gap, pad),
        SOLSolidTabRail::RailHeight(10), SOLIDSCROLLTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollViewHeightTest, "SOLTest.SolidScroll.Stack.ViewHeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// min(content, cap); a cap <= 0 or non-finite is no cap
bool FSOLSolidScrollViewHeightTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("cap below the content"), SOLSolidScroll::StackViewHeight(500.0f, 300.0f), 300.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("cap above the content"), SOLSolidScroll::StackViewHeight(200.0f, 300.0f), 200.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    TestEqual(TEXT("cap equal to the content"), SOLSolidScroll::StackViewHeight(300.0f, 300.0f), 300.0f,
        SOLIDSCROLLTEST_TOLERANCE);
    const float noCaps[] = { 0.0f, -1.0f, -500.0f, SOLIDSCROLLTEST_NAN, SOLIDSCROLLTEST_INF, -SOLIDSCROLLTEST_INF };
    for (const float cap : noCaps)
    {
        TestEqual(FString::Printf(TEXT("cap %f is no cap"), cap), SOLSolidScroll::StackViewHeight(500.0f, cap),
            500.0f, SOLIDSCROLLTEST_TOLERANCE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollLineInViewTest, "SOLTest.SolidScroll.IsLineInView",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// viewTop <= y - offset <= viewTop + viewHeight, inclusive both ends; any NaN is false
bool FSOLSolidScrollLineInViewTest::RunTest(const FString& /*parameters*/)
{
    // View band [100, 300] panel local, content scrolled by 50
    TestTrue(TEXT("inside"), SOLSolidScroll::IsLineInView(250.0f, 50.0f, 100.0f, 200.0f));
    TestTrue(TEXT("on the top edge"), SOLSolidScroll::IsLineInView(150.0f, 50.0f, 100.0f, 200.0f));
    TestTrue(TEXT("on the bottom edge"), SOLSolidScroll::IsLineInView(350.0f, 50.0f, 100.0f, 200.0f));
    TestFalse(TEXT("just above"), SOLSolidScroll::IsLineInView(149.0f, 50.0f, 100.0f, 200.0f));
    TestFalse(TEXT("just below"), SOLSolidScroll::IsLineInView(351.0f, 50.0f, 100.0f, 200.0f));
    TestTrue(TEXT("unscrolled, inside"), SOLSolidScroll::IsLineInView(120.0f, 0.0f, 100.0f, 200.0f));
    TestFalse(TEXT("unscrolled, scrolled out"), SOLSolidScroll::IsLineInView(120.0f, 50.0f, 100.0f, 200.0f));

    // Any NaN
    const float nan = SOLIDSCROLLTEST_NAN;
    TestFalse(TEXT("NaN y"), SOLSolidScroll::IsLineInView(nan, 50.0f, 100.0f, 200.0f));
    TestFalse(TEXT("NaN offset"), SOLSolidScroll::IsLineInView(250.0f, nan, 100.0f, 200.0f));
    TestFalse(TEXT("NaN view top"), SOLSolidScroll::IsLineInView(250.0f, 50.0f, nan, 200.0f));
    TestFalse(TEXT("NaN view height"), SOLSolidScroll::IsLineInView(250.0f, 50.0f, 100.0f, nan));
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Scenario: a ten-entry stack under the visible-entries cap
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSolidScrollStackScenarioTest, "SOLTest.SolidScroll.Scenario.TenEntryStack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Ten entries in a view capped at STACK_MAX_VISIBLE_ENTRIES: the stack scrolls, and scrolling to the last entry makes
// it whole in view and puts its centre line in view (the first entry's centre scrolls out)
bool FSOLSolidScrollStackScenarioTest::RunTest(const FString& /*parameters*/)
{
    constexpr int32 count = 10;
    const int32 visible = SOL::SolidMenu::STACK_MAX_VISIBLE_ENTRIES;
    const float item = SOL::MenuStyle::TAB_HEIGHT;
    const float gap = SOL::MenuStyle::BUTTON_GAP;
    const float pad = SOL::SolidMenu::SETTINGS_TAB_RAIL_PADDING;
    const float viewTop = 120.0f;    // The stack's view band in the panel, panel local
    if (!TestTrue(TEXT("the cap is below ten entries"), visible < count))
    {
        return true;
    }

    const float content = SOLSolidScroll::StackContentHeight(count, item, gap, pad);
    const float cap = SOLSolidScroll::StackContentHeight(visible, item, gap, pad);
    const float view = SOLSolidScroll::StackViewHeight(content, cap);
    TestEqual(TEXT("the view is the cap"), view, cap, SOLIDSCROLLTEST_TOLERANCE);
    TestTrue(TEXT("ten entries need scrolling"), SOLSolidScroll::NeedsScroll(content, view));
    TestFalse(TEXT("the cap's own entries fit"), SOLSolidScroll::NeedsScroll(cap, SOLSolidScroll::StackViewHeight(cap,
        cap)));

    // Unscrolled: the last entry starts below the view
    const float lastTop = SOLSolidScroll::StackItemTop(count - 1, item, gap, pad);
    TestTrue(TEXT("unscrolled, the last entry is below the view"), lastTop >= view);

    // Scroll to show the last entry
    const float offset = SOLSolidScroll::OffsetToShow(0.0f, lastTop, item, content, view);
    TestEqual(TEXT("the offset puts the last entry's bottom on the view's bottom"), offset, lastTop + item - view,
        SOLIDSCROLLTEST_TOLERANCE);
    TestTrue(TEXT("the offset is within the scroll range"), offset >= 0.0f &&
        offset <= SOLSolidScroll::MaxOffset(content, view) + SOLIDSCROLLTEST_TOLERANCE);

    TestTrue(TEXT("scrolled, the last entry is whole in view"), lastTop >= offset - SOLIDSCROLLTEST_TOLERANCE
        && lastTop + item <= offset + view + SOLIDSCROLLTEST_TOLERANCE);

    // Centre lines, panel local and unscrolled
    const float lastCentre = viewTop + lastTop + 0.5f * item;
    const float firstCentre = viewTop + SOLSolidScroll::StackItemTop(0, item, gap, pad) + 0.5f * item;
    TestTrue(TEXT("the last entry's centre line is in view"),
        SOLSolidScroll::IsLineInView(lastCentre, offset, viewTop, view));
    TestFalse(TEXT("the first entry's centre line scrolled out"),
        SOLSolidScroll::IsLineInView(firstCentre, offset, viewTop, view));
    TestFalse(TEXT("unscrolled, the last entry's centre line is out"),
        SOLSolidScroll::IsLineInView(lastCentre, 0.0f, viewTop, view));

    // Showing it again changes nothing
    TestEqual(TEXT("showing the last entry again keeps the offset"),
        SOLSolidScroll::OffsetToShow(offset, lastTop, item, content, view), offset, SOLIDSCROLLTEST_TOLERANCE);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
