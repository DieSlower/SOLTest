/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloFrameBuild.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #42 part 42e Overlay Menu frame layout (Docs/SDDs/42-hologram-in-flight-menus.md sections 5,
// 6.3 and 6.4 cases 19-26): SOLHoloFrameBuild::MakeOverlayLayout (title step, dashed column divider open beside the
// selected tab, tab markers, hint divider, the written-fields rule) and SOLHoloFrameBuild::TextItemRect (the rect a
// row's or tab's hover mark goes around)
namespace
{
    constexpr float HOLOLAYOUTTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float HOLOLAYOUTTEST_INF = std::numeric_limits<float>::infinity();
    constexpr float HOLOLAYOUTTEST_TOLERANCE = 1.0e-5f;
    constexpr float HOLOLAYOUTTEST_SHAPE_TOLERANCE = 1.0e-3f;

    // A typical panel in local units: tabs at x 0-100, rows at x 118-400, both from y 30 to 330, the hint at y 340
    constexpr float HOLOLAYOUTTEST_TITLE = 260.0f;
    constexpr float HOLOLAYOUTTEST_TAB_RIGHT = 100.0f;
    constexpr float HOLOLAYOUTTEST_ROW_LEFT = 118.0f;
    constexpr float HOLOLAYOUTTEST_ROW_RIGHT = 400.0f;
    constexpr float HOLOLAYOUTTEST_TOP = 30.0f;
    constexpr float HOLOLAYOUTTEST_TABS_BOTTOM = 160.0f;
    constexpr float HOLOLAYOUTTEST_BOTTOM = 330.0f;
    constexpr float HOLOLAYOUTTEST_HINT_TOP = 340.0f;
    constexpr float HOLOLAYOUTTEST_TAB_HEIGHT = 26.0f;

    // Worked input O of SDD 42 section 6.4 has five tabs, so it relies on FRAME_MAX_TABS being 5
    static_assert(SOL::HoloMenu::FRAME_MAX_TABS == 5, "Worked input O assumes FRAME_MAX_TABS == 5");
    constexpr int32 HOLOLAYOUTTEST_O_TABS = 5;

    //////////////////////////////////////////////////////////////////////////
    // Returns the box (x0, y0)-(x1, y1), flagged valid as FBox2f's two-corner constructor does
    FBox2f HoloLayoutBox(const float x0, const float y0, const float x1, const float y1)
    {
        return FBox2f(FVector2f(x0, y0), FVector2f(x1, y1));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the typical panel's layout input with five tabs and the third one selected
    FSOLHoloOverlayLayoutInput HoloLayoutTypicalInput()
    {
        FSOLHoloOverlayLayoutInput in;
        in.TitleWidth = HOLOLAYOUTTEST_TITLE;
        in.TabColumn = FBox2f(FVector2f(0.0f, HOLOLAYOUTTEST_TOP), FVector2f(HOLOLAYOUTTEST_TAB_RIGHT,
            HOLOLAYOUTTEST_TABS_BOTTOM));
        in.RowColumn = FBox2f(FVector2f(HOLOLAYOUTTEST_ROW_LEFT, HOLOLAYOUTTEST_TOP),
            FVector2f(HOLOLAYOUTTEST_ROW_RIGHT, HOLOLAYOUTTEST_BOTTOM));
        in.TabCount = SOL::HoloMenu::FRAME_MAX_TABS;
        for (int32 tab = 0; tab < in.TabCount; ++tab)
        {
            const float top = HOLOLAYOUTTEST_TOP + HOLOLAYOUTTEST_TAB_HEIGHT * static_cast<float>(tab);
            in.TabRects[tab] = FBox2f(FVector2f(0.0f, top), FVector2f(HOLOLAYOUTTEST_TAB_RIGHT,
                top + HOLOLAYOUTTEST_TAB_HEIGHT));
        }
        in.SelectedTab = 2;
        in.HintTopY = HOLOLAYOUTTEST_HINT_TOP;
        return in;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns worked input O (SDD 42 section 6.4): title 150, tabs (10,50)-(110,200), rows (130,40)-(380,260), tab i
    // at (10, 50 + 30i)-(110, 75 + 30i), tab 2 selected, the hint at y 280
    FSOLHoloOverlayLayoutInput HoloLayoutWorkedInput()
    {
        FSOLHoloOverlayLayoutInput in;
        in.TitleWidth = 150.0f;
        in.TabColumn = HoloLayoutBox(10.0f, 50.0f, 110.0f, 200.0f);
        in.RowColumn = HoloLayoutBox(130.0f, 40.0f, 380.0f, 260.0f);
        in.TabCount = HOLOLAYOUTTEST_O_TABS;
        for (int32 tab = 0; tab < HOLOLAYOUTTEST_O_TABS; ++tab)
        {
            const float offset = 30.0f * static_cast<float>(tab);
            in.TabRects[tab] = HoloLayoutBox(10.0f, 50.0f + offset, 110.0f, 75.0f + offset);
        }
        in.SelectedTab = 2;
        in.HintTopY = 280.0f;
        return in;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the SDD's dirty spec: other values in every written field (a column divider elsewhere, 5 other valid
    // tab rects, SelectedTab 4, a stepped broken divider) and the frame's own Rect, UiScale and hover fields
    FSOLHoloFrameSpec HoloLayoutDirtySpec()
    {
        FSOLHoloFrameSpec spec;
        spec.Rect = HoloLayoutBox(0.0f, 0.0f, 400.0f, 300.0f);
        spec.TitleStepWidthPx = 999.0f;
        spec.UiScale = 2.0f;
        spec.bColumnDivider = true;
        spec.DividerTop = FVector2f(55.0f, 7.0f);
        spec.DividerBottomY = 123.0f;
        spec.OpenTopY = 20.0f;
        spec.OpenBottomY = 44.0f;
        spec.bBrokenDivider = true;
        spec.BrokenStart = FVector2f(3.0f, 290.0f);
        spec.BrokenEndX = 222.0f;
        spec.BrokenStepAtPx = 17.0f;
        spec.BrokenStepPx = 9.0f;
        spec.TabCount = 2;
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            const float offset = 7.0f * static_cast<float>(tab);
            spec.TabRects[tab] = HoloLayoutBox(200.0f + offset, 5.0f, 250.0f + offset, 15.0f);
        }
        spec.SelectedTab = 4;
        spec.TabDividerX = 77.0f;
        spec.HoverItemRect = HoloLayoutBox(1.0f, 2.0f, 3.0f, 4.0f);
        spec.HoverMark = ESOLHoloHoverMark::CentreTicks;
        return spec;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two floats are the same value, NaN matching NaN
    bool HoloLayoutSameFloat(const float a, const float b)
    {
        return a == b || (FMath::IsNaN(a) && FMath::IsNaN(b));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two boxes have the same corners (NaN matching NaN) and the same valid flag
    bool HoloLayoutSameBox(const FBox2f& a, const FBox2f& b)
    {
        return a.bIsValid == b.bIsValid && HoloLayoutSameFloat(a.Min.X, b.Min.X)
            && HoloLayoutSameFloat(a.Min.Y, b.Min.Y) && HoloLayoutSameFloat(a.Max.X, b.Max.X)
            && HoloLayoutSameFloat(a.Max.Y, b.Max.Y);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that every written field of the dirty run equals the fresh run's (the written-fields rule)
    void HoloLayoutCheckSameWritten(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& fresh,
        const FSOLHoloFrameSpec& dirty)
    {
        // Reports one field's comparison under the case label
        auto expectSame = [&test, &label](const TCHAR* field, const bool bSame)
        {
            test.TestTrue(*FString::Printf(TEXT("%s: dirty spec %s as a fresh one"), *label, field), bSame);
        };
        expectSame(TEXT("TitleStepWidthPx"), HoloLayoutSameFloat(dirty.TitleStepWidthPx, fresh.TitleStepWidthPx));
        expectSame(TEXT("bColumnDivider"), dirty.bColumnDivider == fresh.bColumnDivider);
        expectSame(TEXT("DividerTop.X"), HoloLayoutSameFloat(dirty.DividerTop.X, fresh.DividerTop.X));
        expectSame(TEXT("DividerTop.Y"), HoloLayoutSameFloat(dirty.DividerTop.Y, fresh.DividerTop.Y));
        expectSame(TEXT("DividerBottomY"), HoloLayoutSameFloat(dirty.DividerBottomY, fresh.DividerBottomY));
        expectSame(TEXT("OpenTopY"), HoloLayoutSameFloat(dirty.OpenTopY, fresh.OpenTopY));
        expectSame(TEXT("OpenBottomY"), HoloLayoutSameFloat(dirty.OpenBottomY, fresh.OpenBottomY));
        expectSame(TEXT("TabDividerX"), HoloLayoutSameFloat(dirty.TabDividerX, fresh.TabDividerX));
        expectSame(TEXT("TabCount"), dirty.TabCount == fresh.TabCount);
        expectSame(TEXT("SelectedTab"), dirty.SelectedTab == fresh.SelectedTab);
        expectSame(TEXT("bBrokenDivider"), dirty.bBrokenDivider == fresh.bBrokenDivider);
        expectSame(TEXT("BrokenStart.X"), HoloLayoutSameFloat(dirty.BrokenStart.X, fresh.BrokenStart.X));
        expectSame(TEXT("BrokenStart.Y"), HoloLayoutSameFloat(dirty.BrokenStart.Y, fresh.BrokenStart.Y));
        expectSame(TEXT("BrokenEndX"), HoloLayoutSameFloat(dirty.BrokenEndX, fresh.BrokenEndX));
        expectSame(TEXT("BrokenStepAtPx"), HoloLayoutSameFloat(dirty.BrokenStepAtPx, fresh.BrokenStepAtPx));
        expectSame(TEXT("BrokenStepPx"), HoloLayoutSameFloat(dirty.BrokenStepPx, fresh.BrokenStepPx));
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            test.TestTrue(*FString::Printf(TEXT("%s: dirty spec TabRects[%d] as a fresh one"), *label, tab),
                HoloLayoutSameBox(dirty.TabRects[tab], fresh.TabRects[tab]));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that the frame's own fields (Rect, UiScale, HoverItemRect, HoverMark) still hold the dirty spec's values
    void HoloLayoutCheckFrameFieldsKept(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out)
    {
        const FSOLHoloFrameSpec dirty = HoloLayoutDirtySpec();
        test.TestTrue(*FString::Printf(TEXT("%s: Rect kept"), *label), HoloLayoutSameBox(out.Rect, dirty.Rect));
        test.TestEqual(*FString::Printf(TEXT("%s: UiScale kept"), *label), out.UiScale, dirty.UiScale);
        test.TestTrue(*FString::Printf(TEXT("%s: HoverItemRect kept"), *label),
            HoloLayoutSameBox(out.HoverItemRect, dirty.HoverItemRect));
        test.TestTrue(*FString::Printf(TEXT("%s: HoverMark kept"), *label), out.HoverMark == dirty.HoverMark);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs MakeOverlayLayout into a fresh spec and into a dirty one, checks the dirty run wrote the same fields and
    // kept the frame's own, and returns the fresh run's output
    FSOLHoloFrameSpec HoloLayoutRun(FAutomationTestBase& test, const FString& label,
        const FSOLHoloOverlayLayoutInput& in)
    {
        FSOLHoloFrameSpec fresh;
        SOLHoloFrameBuild::MakeOverlayLayout(in, fresh);
        FSOLHoloFrameSpec dirty = HoloLayoutDirtySpec();
        SOLHoloFrameBuild::MakeOverlayLayout(in, dirty);
        HoloLayoutCheckSameWritten(test, label, fresh, dirty);
        HoloLayoutCheckFrameFieldsKept(test, label, dirty);
        return fresh;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the column divider is present at x xd from topY to bottomY, the tab markers running into it
    void HoloLayoutExpectColumnDivider(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out,
        const float xd, const float topY, const float bottomY)
    {
        test.TestTrue(*FString::Printf(TEXT("%s: column divider"), *label), out.bColumnDivider);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerTop.X"), *label), out.DividerTop.X, xd,
            HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerTop.Y"), *label), out.DividerTop.Y, topY,
            HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerBottomY"), *label), out.DividerBottomY, bottomY,
            HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: TabDividerX"), *label), out.TabDividerX, xd,
            HOLOLAYOUTTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the column divider's absent values: off, DividerTop (0, 0), DividerBottomY 0, TabDividerX 0
    void HoloLayoutExpectNoColumnDivider(FAutomationTestBase& test, const FString& label,
        const FSOLHoloFrameSpec& out)
    {
        test.TestFalse(*FString::Printf(TEXT("%s: no column divider"), *label), out.bColumnDivider);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerTop.X 0"), *label), out.DividerTop.X, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerTop.Y 0"), *label), out.DividerTop.Y, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerBottomY 0"), *label), out.DividerBottomY, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: TabDividerX 0"), *label), out.TabDividerX, 0.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the opening beside the selected tab runs from topY to bottomY
    void HoloLayoutExpectOpening(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out,
        const float topY, const float bottomY)
    {
        test.TestEqual(*FString::Printf(TEXT("%s: OpenTopY"), *label), out.OpenTopY, topY, HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: OpenBottomY"), *label), out.OpenBottomY, bottomY,
            HOLOLAYOUTTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks there is no opening: OpenTopY 1, OpenBottomY 0
    void HoloLayoutExpectNoOpening(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out)
    {
        test.TestEqual(*FString::Printf(TEXT("%s: no opening, OpenTopY 1"), *label), out.OpenTopY, 1.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: no opening, OpenBottomY 0"), *label), out.OpenBottomY, 0.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the hint divider is present from (x, y) to endX and never steps
    void HoloLayoutExpectHint(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out,
        const float x, const float y, const float endX)
    {
        test.TestTrue(*FString::Printf(TEXT("%s: hint divider"), *label), out.bBrokenDivider);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStart.X"), *label), out.BrokenStart.X, x,
            HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStart.Y"), *label), out.BrokenStart.Y, y,
            HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenEndX"), *label), out.BrokenEndX, endX,
            HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStepAtPx 0"), *label), out.BrokenStepAtPx, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStepPx 0"), *label), out.BrokenStepPx, 0.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the hint divider's absent values: off, BrokenStart (0, 0), BrokenEndX 0, both step fields 0
    void HoloLayoutExpectNoHint(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out)
    {
        test.TestFalse(*FString::Printf(TEXT("%s: no hint divider"), *label), out.bBrokenDivider);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStart.X 0"), *label), out.BrokenStart.X, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStart.Y 0"), *label), out.BrokenStart.Y, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenEndX 0"), *label), out.BrokenEndX, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStepAtPx 0"), *label), out.BrokenStepAtPx, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStepPx 0"), *label), out.BrokenStepPx, 0.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks case 19's column divider and opening on worked input O: x 120 from y 40 to 260, open 110-135
    void HoloLayoutExpectWorkedColumn(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out)
    {
        HoloLayoutExpectColumnDivider(test, label, out, 120.0f, 40.0f, 260.0f);
        HoloLayoutExpectOpening(test, label, out, 110.0f, 135.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a TextItemRect result is a valid box (x0, y0)-(x1, y1)
    void HoloLayoutExpectBox(FAutomationTestBase& test, const FString& label, const FBox2f& box, const float x0,
        const float y0, const float x1, const float y1)
    {
        test.TestTrue(*FString::Printf(TEXT("%s: valid"), *label), box.bIsValid);
        test.TestEqual(*FString::Printf(TEXT("%s: Min.X"), *label), box.Min.X, x0, HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: Min.Y"), *label), box.Min.Y, y0, HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: Max.X"), *label), box.Max.X, x1, HOLOLAYOUTTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: Max.Y"), *label), box.Max.Y, y1, HOLOLAYOUTTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the fully open look at strength 1 (FrameFraction 1, FillAlpha 1), nothing hovered
    FSOLHoloMenuLook HoloLayoutOpenLook()
    {
        FSOLHoloMenuLook look;
        look.bVisible = true;
        look.Progress = 1.0f;
        look.FrameFraction = 1.0f;
        look.FillAlpha = 1.0f;
        look.SweepIntensity = SOLHoloMenu::SweepIntensity(1.0f);
        look.BackdropAlpha = SOLHoloMenu::BackdropAlpha(1.0f, 1.0f);
        return look;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutTypicalTest, "SOLTest.HoloOverlayLayout.Typical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The typical panel: title step, divider halfway between the columns over their full height, open at the selected
// tab, the tab markers running into it, the hint divider halfway to the hint across both columns
bool FSOLHoloOverlayLayoutTypicalTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloOverlayLayoutInput in = HoloLayoutTypicalInput();
    const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("typical"), in);
    const float dividerX = 0.5f * (HOLOLAYOUTTEST_TAB_RIGHT + HOLOLAYOUTTEST_ROW_LEFT);
    TestEqual(TEXT("title step is the title width"), out.TitleStepWidthPx, HOLOLAYOUTTEST_TITLE);
    HoloLayoutExpectColumnDivider(*this, TEXT("typical"), out, dividerX, HOLOLAYOUTTEST_TOP, HOLOLAYOUTTEST_BOTTOM);
    TestEqual(TEXT("tab count"), out.TabCount, SOL::HoloMenu::FRAME_MAX_TABS);
    TestEqual(TEXT("selected tab"), out.SelectedTab, 2);
    for (int32 tab = 0; tab < out.TabCount; ++tab)
    {
        TestTrue(*FString::Printf(TEXT("tab %d rect copied"), tab),
            HoloLayoutSameBox(out.TabRects[tab], in.TabRects[tab]));
    }
    HoloLayoutExpectOpening(*this, TEXT("typical"), out, in.TabRects[2].Min.Y, in.TabRects[2].Max.Y);
    HoloLayoutExpectHint(*this, TEXT("typical"), out, 0.0f, 0.5f * (HOLOLAYOUTTEST_BOTTOM + HOLOLAYOUTTEST_HINT_TOP),
        HOLOLAYOUTTEST_ROW_RIGHT);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutKeepsFrameFieldsTest,
    "SOLTest.HoloOverlayLayout.KeepsFrameFields", EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rect, UI scale and hover fields are the frame's own and are left as they were; a dirty spec gets the same
// written fields as a fresh one (case 19, and the typical panel)
bool FSOLHoloOverlayLayoutKeepsFrameFieldsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloOverlayLayoutInput inputs[] = { HoloLayoutTypicalInput(), HoloLayoutWorkedInput() };
    const TCHAR* labels[] = { TEXT("typical"), TEXT("worked input O") };
    static_assert(UE_ARRAY_COUNT(inputs) == UE_ARRAY_COUNT(labels), "One label per input");
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(inputs)); ++index)
    {
        FSOLHoloFrameSpec fresh;
        SOLHoloFrameBuild::MakeOverlayLayout(inputs[index], fresh);
        FSOLHoloFrameSpec dirty = HoloLayoutDirtySpec();
        SOLHoloFrameBuild::MakeOverlayLayout(inputs[index], dirty);
        HoloLayoutCheckSameWritten(*this, labels[index], fresh, dirty);
        HoloLayoutCheckFrameFieldsKept(*this, labels[index], dirty);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutMissingPiecesTest, "SOLTest.HoloOverlayLayout.MissingPieces",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// On the typical panel: a missing or overlapping column, no hint, a bad selection, too few or too many tabs
bool FSOLHoloOverlayLayoutMissingPiecesTest::RunTest(const FString& /*parameters*/)
{
    // A missing row column: no column divider and no opening, the tab markers end at the tabs; the hint divider
    // spans the tab column
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutTypicalInput();
        in.RowColumn = FBox2f(ForceInit);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("no row column"), in);
        HoloLayoutExpectNoColumnDivider(*this, TEXT("no row column"), out);
        HoloLayoutExpectNoOpening(*this, TEXT("no row column"), out);
        HoloLayoutExpectHint(*this, TEXT("no row column"), out, 0.0f,
            0.5f * (HOLOLAYOUTTEST_TABS_BOTTOM + HOLOLAYOUTTEST_HINT_TOP), HOLOLAYOUTTEST_TAB_RIGHT);
    }

    // Overlapping columns: no column divider
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutTypicalInput();
        in.RowColumn.Min.X = HOLOLAYOUTTEST_TAB_RIGHT - 1.0f;
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("overlapping columns"), in);
        HoloLayoutExpectNoColumnDivider(*this, TEXT("overlapping columns"), out);
        HoloLayoutExpectNoOpening(*this, TEXT("overlapping columns"), out);
    }

    // No hint, or a hint not below the columns: no hint divider
    const float badHints[] = { HOLOLAYOUTTEST_NAN, HOLOLAYOUTTEST_BOTTOM, HOLOLAYOUTTEST_TOP };
    for (const float hint : badHints)
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutTypicalInput();
        in.HintTopY = hint;
        const FString label = FString::Printf(TEXT("hint top %f"), hint);
        HoloLayoutExpectNoHint(*this, label, HoloLayoutRun(*this, label, in));
    }

    // A selection outside the tabs: none, and no opening
    const int32 badSelections[] = { INDEX_NONE, -3, SOL::HoloMenu::FRAME_MAX_TABS };
    for (const int32 selected : badSelections)
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutTypicalInput();
        in.SelectedTab = selected;
        const FString label = FString::Printf(TEXT("selection %d"), selected);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, label, in);
        TestEqual(*FString::Printf(TEXT("%s: none"), *label), out.SelectedTab, static_cast<int32>(INDEX_NONE));
        HoloLayoutExpectNoOpening(*this, label, out);
    }

    // Fewer tabs: the rest emptied, a selection past them is none; more than FRAME_MAX_TABS reads the maximum
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutTypicalInput();
        in.TabCount = 2;
        FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("two tabs"), in);
        TestEqual(TEXT("two tabs"), out.TabCount, 2);
        TestFalse(TEXT("third tab rect emptied"), out.TabRects[2].bIsValid);
        TestEqual(TEXT("selection past the tabs: none"), out.SelectedTab, static_cast<int32>(INDEX_NONE));
        in.TabCount = SOL::HoloMenu::FRAME_MAX_TABS + 3;
        out = HoloLayoutRun(*this, TEXT("too many tabs"), in);
        TestEqual(TEXT("too many tabs reads the maximum"), out.TabCount, SOL::HoloMenu::FRAME_MAX_TABS);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutWorkedInputTest, "SOLTest.HoloOverlayLayout.WorkedInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 19: every written field on worked input O, fresh and into a dirty spec (frame fields kept)
bool FSOLHoloOverlayLayoutWorkedInputTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
    const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("O"), in);
    TestEqual(TEXT("O: TitleStepWidthPx 150"), out.TitleStepWidthPx, 150.0f);
    TestEqual(TEXT("O: TabCount 5"), out.TabCount, 5);
    for (int32 tab = 0; tab < HOLOLAYOUTTEST_O_TABS; ++tab)
    {
        TestTrue(*FString::Printf(TEXT("O: TabRects[%d] equal to the input's"), tab),
            HoloLayoutSameBox(out.TabRects[tab], in.TabRects[tab]));
    }
    TestEqual(TEXT("O: SelectedTab 2"), out.SelectedTab, 2);
    HoloLayoutExpectWorkedColumn(*this, TEXT("O"), out);
    HoloLayoutExpectHint(*this, TEXT("O"), out, 10.0f, 270.0f, 380.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutTitleRuleTest, "SOLTest.HoloOverlayLayout.TitleRule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 20: a title width of 0, -5, NaN, +inf or -inf gives no title step; 150 gives 150 (the raw width)
bool FSOLHoloOverlayLayoutTitleRuleTest::RunTest(const FString& /*parameters*/)
{
    const float badTitles[] = { 0.0f, -5.0f, HOLOLAYOUTTEST_NAN, HOLOLAYOUTTEST_INF, -HOLOLAYOUTTEST_INF };
    for (const float title : badTitles)
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
        in.TitleWidth = title;
        const FString label = FString::Printf(TEXT("title width %f"), title);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, label, in);
        TestEqual(*FString::Printf(TEXT("%s: no step"), *label), out.TitleStepWidthPx, 0.0f);
    }
    FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
    in.TitleWidth = 150.0f;
    TestEqual(TEXT("title width 150: step 150"), HoloLayoutRun(*this, TEXT("title 150"), in).TitleStepWidthPx,
        150.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutTabsTest, "SOLTest.HoloOverlayLayout.Tabs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 21: the tab count clamps to [0, FRAME_MAX_TABS], rects past it are invalid, a selection outside the tabs is
// none with no opening while the column divider stays
bool FSOLHoloOverlayLayoutTabsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloOverlayLayoutInput worked = HoloLayoutWorkedInput();

    // TabCount 7 reads 5, rects 0-4 copied
    {
        FSOLHoloOverlayLayoutInput in = worked;
        in.TabCount = 7;
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("TabCount 7"), in);
        TestEqual(TEXT("TabCount 7: reads 5"), out.TabCount, 5);
        for (int32 tab = 0; tab < HOLOLAYOUTTEST_O_TABS; ++tab)
        {
            TestTrue(*FString::Printf(TEXT("TabCount 7: TabRects[%d] copied"), tab),
                HoloLayoutSameBox(out.TabRects[tab], worked.TabRects[tab]));
        }
        TestEqual(TEXT("TabCount 7: SelectedTab 2"), out.SelectedTab, 2);
    }

    // TabCount 3 copies rects 0-2 and leaves 3 and 4 invalid although the input's are valid
    {
        FSOLHoloOverlayLayoutInput in = worked;
        in.TabCount = 3;
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("TabCount 3"), in);
        TestEqual(TEXT("TabCount 3: 3"), out.TabCount, 3);
        for (int32 tab = 0; tab < 3; ++tab)
        {
            TestTrue(*FString::Printf(TEXT("TabCount 3: TabRects[%d] copied"), tab),
                HoloLayoutSameBox(out.TabRects[tab], worked.TabRects[tab]));
        }
        TestFalse(TEXT("TabCount 3: TabRects[3] invalid"), out.TabRects[3].bIsValid);
        TestFalse(TEXT("TabCount 3: TabRects[4] invalid"), out.TabRects[4].bIsValid);
        TestEqual(TEXT("TabCount 3: SelectedTab 2"), out.SelectedTab, 2);
        HoloLayoutExpectOpening(*this, TEXT("TabCount 3"), out, 110.0f, 135.0f);
    }

    // TabCount -1 gives 0, every rect invalid, no selection, no opening; the column divider stays
    {
        FSOLHoloOverlayLayoutInput in = worked;
        in.TabCount = -1;
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("TabCount -1"), in);
        TestEqual(TEXT("TabCount -1: 0"), out.TabCount, 0);
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            TestFalse(*FString::Printf(TEXT("TabCount -1: TabRects[%d] invalid"), tab), out.TabRects[tab].bIsValid);
        }
        TestEqual(TEXT("TabCount -1: SelectedTab none"), out.SelectedTab, static_cast<int32>(INDEX_NONE));
        HoloLayoutExpectNoOpening(*this, TEXT("TabCount -1"), out);
        HoloLayoutExpectColumnDivider(*this, TEXT("TabCount -1"), out, 120.0f, 40.0f, 260.0f);
    }

    // SelectedTab 5 (with 5 tabs), -1, or 3 with TabCount 3: none, no opening, the column divider still at 120
    const int32 selections[] = { 5, -1, 3 };
    const int32 counts[] = { 5, 5, 3 };
    static_assert(UE_ARRAY_COUNT(selections) == UE_ARRAY_COUNT(counts), "One tab count per selection");
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(selections)); ++index)
    {
        FSOLHoloOverlayLayoutInput in = worked;
        in.SelectedTab = selections[index];
        in.TabCount = counts[index];
        const FString label = FString::Printf(TEXT("SelectedTab %d of %d"), selections[index], counts[index]);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, label, in);
        TestEqual(*FString::Printf(TEXT("%s: none"), *label), out.SelectedTab, static_cast<int32>(INDEX_NONE));
        HoloLayoutExpectNoOpening(*this, label, out);
        HoloLayoutExpectColumnDivider(*this, label, out, 120.0f, 40.0f, 260.0f);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutInvalidSelectedRectTest,
    "SOLTest.HoloOverlayLayout.InvalidSelectedRect", EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 22: a valid selection whose tab rect is invalid (unset, zero height, NaN) keeps SelectedTab, gives no opening
// and keeps the column divider; the rect is copied as given
bool FSOLHoloOverlayLayoutInvalidSelectedRectTest::RunTest(const FString& /*parameters*/)
{
    const FBox2f badRects[] = {
        FBox2f(ForceInit), HoloLayoutBox(10.0f, 110.0f, 110.0f, 110.0f),
        HoloLayoutBox(10.0f, HOLOLAYOUTTEST_NAN, 110.0f, 135.0f) };
    const TCHAR* labels[] = { TEXT("unset tab 2 rect"), TEXT("zero-height tab 2 rect"), TEXT("NaN tab 2 rect") };
    static_assert(UE_ARRAY_COUNT(badRects) == UE_ARRAY_COUNT(labels), "One label per rect");
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(badRects)); ++index)
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
        in.TabRects[2] = badRects[index];
        const FString label = labels[index];
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, label, in);
        TestEqual(*FString::Printf(TEXT("%s: SelectedTab stays 2"), *label), out.SelectedTab, 2);
        HoloLayoutExpectNoOpening(*this, label, out);
        HoloLayoutExpectColumnDivider(*this, label, out, 120.0f, 40.0f, 260.0f);
        TestTrue(*FString::Printf(TEXT("%s: TabRects[2] copied as given"), *label),
            HoloLayoutSameBox(out.TabRects[2], badRects[index]));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutColumnsTest, "SOLTest.HoloOverlayLayout.Columns",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 23: a missing or overlapping column gives no column divider (the hint divider from the valid columns);
// touching columns still give one
bool FSOLHoloOverlayLayoutColumnsTest::RunTest(const FString& /*parameters*/)
{
    // Tab column missing: no column divider or opening (the selection stays), hint divider from the rows alone
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
        in.TabColumn = FBox2f(ForceInit);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("no tab column"), in);
        HoloLayoutExpectNoColumnDivider(*this, TEXT("no tab column"), out);
        HoloLayoutExpectNoOpening(*this, TEXT("no tab column"), out);
        TestEqual(TEXT("no tab column: SelectedTab stays 2"), out.SelectedTab, 2);
        HoloLayoutExpectHint(*this, TEXT("no tab column"), out, 130.0f, 270.0f, 380.0f);
    }

    // Row column missing: no column divider, hint divider from the tabs alone
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
        in.RowColumn = FBox2f(ForceInit);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("no row column"), in);
        HoloLayoutExpectNoColumnDivider(*this, TEXT("no row column"), out);
        HoloLayoutExpectNoOpening(*this, TEXT("no row column"), out);
        HoloLayoutExpectHint(*this, TEXT("no row column"), out, 10.0f, 240.0f, 110.0f);
    }

    // Overlapping columns: no column divider or opening; the hint divider across both
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
        in.RowColumn = HoloLayoutBox(100.0f, 40.0f, 380.0f, 260.0f);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("overlapping columns"), in);
        HoloLayoutExpectNoColumnDivider(*this, TEXT("overlapping columns"), out);
        HoloLayoutExpectNoOpening(*this, TEXT("overlapping columns"), out);
        HoloLayoutExpectHint(*this, TEXT("overlapping columns"), out, 10.0f, 270.0f, 380.0f);
    }

    // Touching columns: a column divider at x 110, open 110-135
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
        in.RowColumn = HoloLayoutBox(110.0f, 40.0f, 380.0f, 260.0f);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("touching columns"), in);
        HoloLayoutExpectColumnDivider(*this, TEXT("touching columns"), out, 110.0f, 40.0f, 260.0f);
        HoloLayoutExpectOpening(*this, TEXT("touching columns"), out, 110.0f, 135.0f);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutNoHintTest, "SOLTest.HoloOverlayLayout.NoHint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 24: a non-finite hint top, or one at or above the columns' bottom, gives no hint divider and leaves the
// column divider as case 19; no valid column gives neither divider
bool FSOLHoloOverlayLayoutNoHintTest::RunTest(const FString& /*parameters*/)
{
    const float badHints[] = { HOLOLAYOUTTEST_NAN, HOLOLAYOUTTEST_INF, -HOLOLAYOUTTEST_INF, 260.0f, 200.0f };
    for (const float hint : badHints)
    {
        FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
        in.HintTopY = hint;
        const FString label = FString::Printf(TEXT("HintTopY %f"), hint);
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, label, in);
        HoloLayoutExpectNoHint(*this, label, out);
        HoloLayoutExpectWorkedColumn(*this, label, out);
    }

    // Both columns invalid with the hint at 280: no column divider and no hint divider
    FSOLHoloOverlayLayoutInput in = HoloLayoutWorkedInput();
    in.TabColumn = FBox2f(ForceInit);
    in.RowColumn = FBox2f(ForceInit);
    const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("no columns"), in);
    HoloLayoutExpectNoColumnDivider(*this, TEXT("no columns"), out);
    HoloLayoutExpectNoOpening(*this, TEXT("no columns"), out);
    HoloLayoutExpectNoHint(*this, TEXT("no columns"), out);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutDefaultInputTest, "SOLTest.HoloOverlayLayout.DefaultInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 24: a default-constructed input gives every absent value; HintTopY left at its default gives no hint divider
// for columns at y >= 0
bool FSOLHoloOverlayLayoutDefaultInputTest::RunTest(const FString& /*parameters*/)
{
    {
        const FSOLHoloOverlayLayoutInput in;
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("default input"), in);
        TestEqual(TEXT("default input: TitleStepWidthPx 0"), out.TitleStepWidthPx, 0.0f);
        TestEqual(TEXT("default input: TabCount 0"), out.TabCount, 0);
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            TestFalse(*FString::Printf(TEXT("default input: TabRects[%d] invalid"), tab),
                out.TabRects[tab].bIsValid);
        }
        TestEqual(TEXT("default input: SelectedTab none"), out.SelectedTab, static_cast<int32>(INDEX_NONE));
        HoloLayoutExpectNoColumnDivider(*this, TEXT("default input"), out);
        HoloLayoutExpectNoOpening(*this, TEXT("default input"), out);
        HoloLayoutExpectNoHint(*this, TEXT("default input"), out);
    }

    // O with every field set except HintTopY, left at its default: the column divider as case 19, no hint divider
    {
        const FSOLHoloOverlayLayoutInput worked = HoloLayoutWorkedInput();
        FSOLHoloOverlayLayoutInput in;
        in.TitleWidth = worked.TitleWidth;
        in.TabColumn = worked.TabColumn;
        in.RowColumn = worked.RowColumn;
        in.TabCount = worked.TabCount;
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            in.TabRects[tab] = worked.TabRects[tab];
        }
        in.SelectedTab = worked.SelectedTab;
        const FSOLHoloFrameSpec out = HoloLayoutRun(*this, TEXT("default hint"), in);
        HoloLayoutExpectNoHint(*this, TEXT("default hint"), out);
        HoloLayoutExpectWorkedColumn(*this, TEXT("default hint"), out);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutWithFrameBuilderTest,
    "SOLTest.HoloOverlayLayout.WithFrameBuilder", EAutomationTestFlags::EditorContext
    | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 25: case 19's spec through BuildFrame (open look, s 1): the column divider dashes stay out of the opening at
// x 120, the selected tab's marker runs into the divider, exactly one tab dot
bool FSOLHoloOverlayLayoutWithFrameBuilderTest::RunTest(const FString& /*parameters*/)
{
    FSOLHoloFrameSpec spec;
    SOLHoloFrameBuild::MakeOverlayLayout(HoloLayoutWorkedInput(), spec);
    spec.Rect = HoloLayoutBox(0.0f, 0.0f, 400.0f, 300.0f);
    spec.UiScale = 1.0f;
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    SOLHoloFrameBuild::BuildFrame(spec, HoloLayoutOpenLook(), 1.0f, *out);

    int32 dividerCount = 0;
    int32 tabDotCount = 0;
    bool bTopEdge = false;
    bool bBottomEdge = false;
    const float tol = HOLOLAYOUTTEST_SHAPE_TOLERANCE;
    for (int32 index = 0; index < out->ShapeCount; ++index)
    {
        // Each column divider dash: at x 120, its y range clear of the opening [110, 135] (ends included)
        const FSOLHoloShape& shape = out->Shapes[index];
        if (shape.Part == ESOLHoloPart::ColumnDivider)
        {
            ++dividerCount;
            const float minY = FMath::Min(shape.A.Y, shape.B.Y);
            const float maxY = FMath::Max(shape.A.Y, shape.B.Y);
            TestTrue(*FString::Printf(TEXT("divider dash %d at x 120"), index),
                FMath::IsNearlyEqual(shape.A.X, 120.0f, tol) && FMath::IsNearlyEqual(shape.B.X, 120.0f, tol));
            TestFalse(*FString::Printf(TEXT("divider dash %d (y %f-%f) clear of the opening"), index, minY, maxY),
                maxY >= 110.0f && minY <= 135.0f);
        }
        else if (shape.Part == ESOLHoloPart::Tab)
        {
            bTopEdge = bTopEdge || (shape.A.Equals(FVector2f(120.0f, 110.0f), tol)
                && shape.B.Equals(FVector2f(16.0f, 110.0f), tol));
            bBottomEdge = bBottomEdge || (shape.A.Equals(FVector2f(10.0f, 135.0f), tol)
                && shape.B.Equals(FVector2f(120.0f, 135.0f), tol));
        }
        else if (shape.Part == ESOLHoloPart::TabDot)
        {
            ++tabDotCount;
        }
    }
    TestTrue(TEXT("the column divider has dashes"), dividerCount > 0);
    TestTrue(TEXT("selected tab piece (120,110)->(16,110)"), bTopEdge);
    TestTrue(TEXT("selected tab piece (10,135)->(120,135)"), bBottomEdge);
    TestEqual(TEXT("exactly one tab dot"), tabDotCount, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloOverlayLayoutTextItemRectTest, "SOLTest.HoloOverlayLayout.TextItemRect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 26: a text item's hover rect is the slot's left plus the indent to the text's width (at most the slot's), the
// slot's full height; invalid when nothing is left of the text or the slot or the text width is bad
bool FSOLHoloOverlayLayoutTextItemRectTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloFrameBuild::TextItemRect;
    const FBox2f slot = HoloLayoutBox(40.0f, 100.0f, 240.0f, 120.0f);

    HoloLayoutExpectBox(*this, TEXT("text 90"), TextItemRect(slot, 90.0f, 0.0f), 40.0f, 100.0f, 130.0f, 120.0f);
    HoloLayoutExpectBox(*this, TEXT("text 120 indent 34"), TextItemRect(slot, 120.0f, 34.0f), 74.0f, 100.0f, 160.0f,
        120.0f);
    HoloLayoutExpectBox(*this, TEXT("text 1000 clamped to the slot"), TextItemRect(slot, 1000.0f, 0.0f), 40.0f,
        100.0f, 240.0f, 120.0f);
    HoloLayoutExpectBox(*this, TEXT("NaN indent reads 0"), TextItemRect(slot, 90.0f, HOLOLAYOUTTEST_NAN), 40.0f,
        100.0f, 130.0f, 120.0f);
    HoloLayoutExpectBox(*this, TEXT("indent -5 reads 0"), TextItemRect(slot, 90.0f, -5.0f), 40.0f, 100.0f, 130.0f,
        120.0f);

    // Invalid boxes: no text left, a bad text width, a bad slot
    TestFalse(TEXT("text 34 indent 34: invalid"), TextItemRect(slot, 34.0f, 34.0f).bIsValid);
    TestFalse(TEXT("text 0: invalid"), TextItemRect(slot, 0.0f, 0.0f).bIsValid);
    TestFalse(TEXT("text -10: invalid"), TextItemRect(slot, -10.0f, 0.0f).bIsValid);
    TestFalse(TEXT("text NaN: invalid"), TextItemRect(slot, HOLOLAYOUTTEST_NAN, 0.0f).bIsValid);
    TestFalse(TEXT("text +inf: invalid"), TextItemRect(slot, HOLOLAYOUTTEST_INF, 0.0f).bIsValid);
    TestFalse(TEXT("indent 250 past the slot: invalid"), TextItemRect(slot, 1000.0f, 250.0f).bIsValid);
    TestFalse(TEXT("unset slot: invalid"), TextItemRect(FBox2f(ForceInit), 90.0f, 0.0f).bIsValid);
    TestFalse(TEXT("zero-width slot: invalid"), TextItemRect(HoloLayoutBox(5.0f, 5.0f, 5.0f, 20.0f), 90.0f,
        0.0f).bIsValid);
    TestFalse(TEXT("NaN slot: invalid"), TextItemRect(HoloLayoutBox(40.0f, HOLOLAYOUTTEST_NAN, 240.0f, 120.0f),
        90.0f, 0.0f).bIsValid);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
