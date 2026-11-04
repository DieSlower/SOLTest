/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloFrameBuild.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #42 part 42f F3 speed panel frame layout (Docs/SDDs/42-hologram-in-flight-menus.md sections 5,
// 6.3, 6.4 cases 20 and 27-29, and 7): SOLHoloFrameBuild::MakeSpeedLayout (title step, the stepped broken divider
// between the steppers and the body list, no column divider and no tabs, the written-fields rule)
namespace
{
    constexpr float HOLOSPEEDTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float HOLOSPEEDTEST_INF = std::numeric_limits<float>::infinity();
    constexpr float HOLOSPEEDTEST_TOLERANCE = 1.0e-5f;

    // A typical panel in local units: steppers from x 20 to 380, y 60 to 150; the list from x 20 to 460, y 175 to 430
    constexpr float HOLOSPEEDTEST_TITLE = 300.0f;
    constexpr float HOLOSPEEDTEST_LEFT = 20.0f;
    constexpr float HOLOSPEEDTEST_STEPPER_RIGHT = 380.0f;
    constexpr float HOLOSPEEDTEST_LIST_RIGHT = 460.0f;
    constexpr float HOLOSPEEDTEST_STEPPER_TOP = 60.0f;
    constexpr float HOLOSPEEDTEST_STEPPER_BOTTOM = 150.0f;
    constexpr float HOLOSPEEDTEST_LIST_TOP = 175.0f;
    constexpr float HOLOSPEEDTEST_LIST_BOTTOM = 430.0f;
    constexpr float HOLOSPEEDTEST_LOCAL_PER_PX = 1.5f;

    //////////////////////////////////////////////////////////////////////////
    // Returns the box (x0, y0)-(x1, y1), flagged valid as FBox2f's two-corner constructor does
    FBox2f HoloSpeedBox(const float x0, const float y0, const float x1, const float y1)
    {
        return FBox2f(FVector2f(x0, y0), FVector2f(x1, y1));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the typical panel's layout input
    FSOLHoloSpeedLayoutInput HoloSpeedTypicalInput()
    {
        FSOLHoloSpeedLayoutInput in;
        in.TitleWidth = HOLOSPEEDTEST_TITLE;
        in.StepperSection = FBox2f(FVector2f(HOLOSPEEDTEST_LEFT, HOLOSPEEDTEST_STEPPER_TOP),
            FVector2f(HOLOSPEEDTEST_STEPPER_RIGHT, HOLOSPEEDTEST_STEPPER_BOTTOM));
        in.ListSection = FBox2f(FVector2f(HOLOSPEEDTEST_LEFT, HOLOSPEEDTEST_LIST_TOP),
            FVector2f(HOLOSPEEDTEST_LIST_RIGHT, HOLOSPEEDTEST_LIST_BOTTOM));
        in.LocalPerPx = HOLOSPEEDTEST_LOCAL_PER_PX;
        return in;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns case 27's input (SDD 42 section 6.4): title 120, steppers (12,40)-(330,150), list (12,170)-(330,380),
    // 1 local unit per px
    FSOLHoloSpeedLayoutInput HoloSpeedWorkedInput()
    {
        FSOLHoloSpeedLayoutInput in;
        in.TitleWidth = 120.0f;
        in.StepperSection = HoloSpeedBox(12.0f, 40.0f, 330.0f, 150.0f);
        in.ListSection = HoloSpeedBox(12.0f, 170.0f, 330.0f, 380.0f);
        in.LocalPerPx = 1.0f;
        return in;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the SDD's dirty spec: other values in every written field (a column divider with an opening, 5 valid tab
    // rects, SelectedTab 4, a stepped broken divider elsewhere) and the frame's own Rect, UiScale and hover fields
    FSOLHoloFrameSpec HoloSpeedDirtySpec()
    {
        FSOLHoloFrameSpec spec;
        spec.Rect = HoloSpeedBox(0.0f, 0.0f, 400.0f, 300.0f);
        spec.TitleStepWidthPx = 999.0f;
        spec.UiScale = 2.0f;
        spec.bColumnDivider = true;
        spec.DividerTop = FVector2f(50.0f, 10.0f);
        spec.DividerBottomY = 300.0f;
        spec.OpenTopY = 20.0f;
        spec.OpenBottomY = 40.0f;
        spec.TabCount = SOL::HoloMenu::FRAME_MAX_TABS;
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            const float offset = 12.0f * static_cast<float>(tab);
            spec.TabRects[tab] = HoloSpeedBox(0.0f, offset, 10.0f, offset + 10.0f);
        }
        spec.SelectedTab = 4;
        spec.TabDividerX = 50.0f;
        spec.bBrokenDivider = true;
        spec.BrokenStart = FVector2f(1.0f, 2.0f);
        spec.BrokenEndX = 3.0f;
        spec.BrokenStepAtPx = 4.0f;
        spec.BrokenStepPx = 5.0f;
        spec.HoverItemRect = HoloSpeedBox(1.0f, 2.0f, 3.0f, 4.0f);
        spec.HoverMark = ESOLHoloHoverMark::CentreTicks;
        return spec;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two floats are the same value, NaN matching NaN
    bool HoloSpeedSameFloat(const float a, const float b)
    {
        return a == b || (FMath::IsNaN(a) && FMath::IsNaN(b));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two boxes have the same corners (NaN matching NaN) and the same valid flag
    bool HoloSpeedSameBox(const FBox2f& a, const FBox2f& b)
    {
        return a.bIsValid == b.bIsValid && HoloSpeedSameFloat(a.Min.X, b.Min.X)
            && HoloSpeedSameFloat(a.Min.Y, b.Min.Y) && HoloSpeedSameFloat(a.Max.X, b.Max.X)
            && HoloSpeedSameFloat(a.Max.Y, b.Max.Y);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that every written field of the dirty run equals the fresh run's (the written-fields rule)
    void HoloSpeedCheckSameWritten(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& fresh,
        const FSOLHoloFrameSpec& dirty)
    {
        // Reports one field's comparison under the case label
        auto expectSame = [&test, &label](const TCHAR* field, const bool bSame)
        {
            test.TestTrue(*FString::Printf(TEXT("%s: dirty spec %s as a fresh one"), *label, field), bSame);
        };
        expectSame(TEXT("TitleStepWidthPx"), HoloSpeedSameFloat(dirty.TitleStepWidthPx, fresh.TitleStepWidthPx));
        expectSame(TEXT("bColumnDivider"), dirty.bColumnDivider == fresh.bColumnDivider);
        expectSame(TEXT("DividerTop.X"), HoloSpeedSameFloat(dirty.DividerTop.X, fresh.DividerTop.X));
        expectSame(TEXT("DividerTop.Y"), HoloSpeedSameFloat(dirty.DividerTop.Y, fresh.DividerTop.Y));
        expectSame(TEXT("DividerBottomY"), HoloSpeedSameFloat(dirty.DividerBottomY, fresh.DividerBottomY));
        expectSame(TEXT("OpenTopY"), HoloSpeedSameFloat(dirty.OpenTopY, fresh.OpenTopY));
        expectSame(TEXT("OpenBottomY"), HoloSpeedSameFloat(dirty.OpenBottomY, fresh.OpenBottomY));
        expectSame(TEXT("TabDividerX"), HoloSpeedSameFloat(dirty.TabDividerX, fresh.TabDividerX));
        expectSame(TEXT("TabCount"), dirty.TabCount == fresh.TabCount);
        expectSame(TEXT("SelectedTab"), dirty.SelectedTab == fresh.SelectedTab);
        expectSame(TEXT("bBrokenDivider"), dirty.bBrokenDivider == fresh.bBrokenDivider);
        expectSame(TEXT("BrokenStart.X"), HoloSpeedSameFloat(dirty.BrokenStart.X, fresh.BrokenStart.X));
        expectSame(TEXT("BrokenStart.Y"), HoloSpeedSameFloat(dirty.BrokenStart.Y, fresh.BrokenStart.Y));
        expectSame(TEXT("BrokenEndX"), HoloSpeedSameFloat(dirty.BrokenEndX, fresh.BrokenEndX));
        expectSame(TEXT("BrokenStepAtPx"), HoloSpeedSameFloat(dirty.BrokenStepAtPx, fresh.BrokenStepAtPx));
        expectSame(TEXT("BrokenStepPx"), HoloSpeedSameFloat(dirty.BrokenStepPx, fresh.BrokenStepPx));
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            test.TestTrue(*FString::Printf(TEXT("%s: dirty spec TabRects[%d] as a fresh one"), *label, tab),
                HoloSpeedSameBox(dirty.TabRects[tab], fresh.TabRects[tab]));
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that the frame's own fields (Rect, UiScale, HoverItemRect, HoverMark) still hold the dirty spec's values
    void HoloSpeedCheckFrameFieldsKept(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out)
    {
        const FSOLHoloFrameSpec dirty = HoloSpeedDirtySpec();
        test.TestTrue(*FString::Printf(TEXT("%s: Rect kept"), *label), HoloSpeedSameBox(out.Rect, dirty.Rect));
        test.TestEqual(*FString::Printf(TEXT("%s: UiScale kept"), *label), out.UiScale, dirty.UiScale);
        test.TestTrue(*FString::Printf(TEXT("%s: HoverItemRect kept"), *label),
            HoloSpeedSameBox(out.HoverItemRect, dirty.HoverItemRect));
        test.TestTrue(*FString::Printf(TEXT("%s: HoverMark kept"), *label), out.HoverMark == dirty.HoverMark);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs MakeSpeedLayout into a fresh spec and into a dirty one, checks the dirty run wrote the same fields and kept
    // the frame's own, and returns the fresh run's output
    FSOLHoloFrameSpec HoloSpeedRun(FAutomationTestBase& test, const FString& label, const FSOLHoloSpeedLayoutInput& in)
    {
        FSOLHoloFrameSpec fresh;
        SOLHoloFrameBuild::MakeSpeedLayout(in, fresh);
        FSOLHoloFrameSpec dirty = HoloSpeedDirtySpec();
        SOLHoloFrameBuild::MakeSpeedLayout(in, dirty);
        HoloSpeedCheckSameWritten(test, label, fresh, dirty);
        HoloSpeedCheckFrameFieldsKept(test, label, dirty);
        return fresh;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the speed panel's fixed absent pieces: no column divider, no opening, no tabs
    void HoloSpeedExpectNoColumnNoTabs(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out)
    {
        test.TestFalse(*FString::Printf(TEXT("%s: no column divider"), *label), out.bColumnDivider);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerTop.X 0"), *label), out.DividerTop.X, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerTop.Y 0"), *label), out.DividerTop.Y, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: DividerBottomY 0"), *label), out.DividerBottomY, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: OpenTopY 1"), *label), out.OpenTopY, 1.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: OpenBottomY 0"), *label), out.OpenBottomY, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: TabDividerX 0"), *label), out.TabDividerX, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: TabCount 0"), *label), out.TabCount, 0);
        for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
        {
            test.TestFalse(*FString::Printf(TEXT("%s: TabRects[%d] invalid"), *label, tab),
                out.TabRects[tab].bIsValid);
        }
        test.TestEqual(*FString::Printf(TEXT("%s: SelectedTab none"), *label), out.SelectedTab,
            static_cast<int32>(INDEX_NONE));
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the stepped divider is present from (x, y) to endX, stepping stepPx at stepAtPx
    void HoloSpeedExpectDivider(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out,
        const float x, const float y, const float endX, const float stepAtPx, const float stepPx)
    {
        test.TestTrue(*FString::Printf(TEXT("%s: divider"), *label), out.bBrokenDivider);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStart.X"), *label), out.BrokenStart.X, x,
            HOLOSPEEDTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStart.Y"), *label), out.BrokenStart.Y, y,
            HOLOSPEEDTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenEndX"), *label), out.BrokenEndX, endX,
            HOLOSPEEDTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStepAtPx"), *label), out.BrokenStepAtPx, stepAtPx,
            HOLOSPEEDTEST_TOLERANCE);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStepPx"), *label), out.BrokenStepPx, stepPx,
            HOLOSPEEDTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks the divider's absent values: off, BrokenStart (0, 0), BrokenEndX 0, both step fields 0
    void HoloSpeedExpectNoDivider(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameSpec& out)
    {
        test.TestFalse(*FString::Printf(TEXT("%s: no divider"), *label), out.bBrokenDivider);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStart.X 0"), *label), out.BrokenStart.X, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStart.Y 0"), *label), out.BrokenStart.Y, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenEndX 0"), *label), out.BrokenEndX, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStepAtPx 0"), *label), out.BrokenStepAtPx, 0.0f);
        test.TestEqual(*FString::Printf(TEXT("%s: BrokenStepPx 0"), *label), out.BrokenStepPx, 0.0f);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloSpeedLayoutTypicalTest, "SOLTest.HoloSpeedLayout.Typical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The typical panel: title step, the stepped divider across both sections straddling the gap's midpoint, its step at
// SPEED_DIVIDER_STEP_AT_PX by SPEED_DIVIDER_STEP_PX times the local units per px; no column divider and no tabs
bool FSOLHoloSpeedLayoutTypicalTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHoloFrameSpec out = HoloSpeedRun(*this, TEXT("typical"), HoloSpeedTypicalInput());
    const float stepLocal = SOL::HoloMenu::SPEED_DIVIDER_STEP_PX * HOLOSPEEDTEST_LOCAL_PER_PX;
    const float midY = 0.5f * (HOLOSPEEDTEST_STEPPER_BOTTOM + HOLOSPEEDTEST_LIST_TOP);
    TestEqual(TEXT("title step is the title width"), out.TitleStepWidthPx, HOLOSPEEDTEST_TITLE);
    HoloSpeedExpectDivider(*this, TEXT("typical"), out, HOLOSPEEDTEST_LEFT, midY + 0.5f * stepLocal,
        HOLOSPEEDTEST_LIST_RIGHT, SOL::HoloMenu::SPEED_DIVIDER_STEP_AT_PX * HOLOSPEEDTEST_LOCAL_PER_PX, stepLocal);
    TestEqual(TEXT("upper level half a step above the midpoint"), out.BrokenStart.Y - out.BrokenStepPx,
        midY - 0.5f * stepLocal, HOLOSPEEDTEST_TOLERANCE);
    HoloSpeedExpectNoColumnNoTabs(*this, TEXT("typical"), out);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloSpeedLayoutScaleTest, "SOLTest.HoloSpeedLayout.LocalPerPx",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The step scales with the local units per px; NaN, infinite, zero and negative read 1
bool FSOLHoloSpeedLayoutScaleTest::RunTest(const FString& /*parameters*/)
{
    const float invalid[] = { HOLOSPEEDTEST_NAN, HOLOSPEEDTEST_INF, 0.0f, -2.0f };
    for (const float localPerPx : invalid)
    {
        FSOLHoloSpeedLayoutInput in = HoloSpeedTypicalInput();
        in.LocalPerPx = localPerPx;
        const FString label = FString::Printf(TEXT("local per px %f"), localPerPx);
        const FSOLHoloFrameSpec out = HoloSpeedRun(*this, label, in);
        TestEqual(*FString::Printf(TEXT("%s: step at reads 1"), *label), out.BrokenStepAtPx,
            SOL::HoloMenu::SPEED_DIVIDER_STEP_AT_PX, HOLOSPEEDTEST_TOLERANCE);
        TestEqual(*FString::Printf(TEXT("%s: step height reads 1"), *label), out.BrokenStepPx,
            SOL::HoloMenu::SPEED_DIVIDER_STEP_PX, HOLOSPEEDTEST_TOLERANCE);
    }
    FSOLHoloSpeedLayoutInput in = HoloSpeedTypicalInput();
    in.LocalPerPx = 2.0f;
    const FSOLHoloFrameSpec out = HoloSpeedRun(*this, TEXT("local per px 2"), in);
    TestEqual(TEXT("step height doubles at 2"), out.BrokenStepPx, 2.0f * SOL::HoloMenu::SPEED_DIVIDER_STEP_PX,
        HOLOSPEEDTEST_TOLERANCE);
    TestEqual(TEXT("divider x unscaled"), out.BrokenStart.X, HOLOSPEEDTEST_LEFT, HOLOSPEEDTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloSpeedLayoutMissingTest, "SOLTest.HoloSpeedLayout.Missing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 29: a missing section (unset or with a NaN coordinate), or sections that touch or overlap vertically, give the
// divider's absent values, fresh and into a dirty spec; the title step is still set
bool FSOLHoloSpeedLayoutMissingTest::RunTest(const FString& /*parameters*/)
{
    // Each case and its label
    FSOLHoloSpeedLayoutInput noStepper = HoloSpeedWorkedInput();
    noStepper.StepperSection = FBox2f(ForceInit);
    FSOLHoloSpeedLayoutInput noList = HoloSpeedWorkedInput();
    noList.ListSection = FBox2f(ForceInit);
    FSOLHoloSpeedLayoutInput touching = HoloSpeedWorkedInput();
    touching.ListSection.Min.Y = 150.0f;
    FSOLHoloSpeedLayoutInput overlapping = HoloSpeedWorkedInput();
    overlapping.ListSection.Min.Y = 140.0f;
    FSOLHoloSpeedLayoutInput nanList = HoloSpeedWorkedInput();
    nanList.ListSection.Min.Y = HOLOSPEEDTEST_NAN;
    const FSOLHoloSpeedLayoutInput* cases[] = { &noStepper, &noList, &touching, &overlapping, &nanList };
    const TCHAR* labels[] = { TEXT("no stepper section"), TEXT("no list section"), TEXT("touching sections"),
        TEXT("overlapping sections"), TEXT("NaN list top") };
    static_assert(UE_ARRAY_COUNT(cases) == UE_ARRAY_COUNT(labels), "One label per case");
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(cases)); ++index)
    {
        const FString label = labels[index];
        const FSOLHoloFrameSpec out = HoloSpeedRun(*this, label, *cases[index]);
        HoloSpeedExpectNoDivider(*this, label, out);
        TestEqual(*FString::Printf(TEXT("%s: title step still 120"), *label), out.TitleStepWidthPx, 120.0f);
        HoloSpeedExpectNoColumnNoTabs(*this, label, out);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloSpeedLayoutWorkedInputTest, "SOLTest.HoloSpeedLayout.WorkedInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 27: the exact divider at 1 and 0.5 local units per px, bad scales reading 1, the outer edges of sections of
// different widths, and a gap narrower than the step
bool FSOLHoloSpeedLayoutWorkedInputTest::RunTest(const FString& /*parameters*/)
{
    // LocalPerPx 1: left level 163, right level 157, straddling 160
    {
        const FSOLHoloFrameSpec out = HoloSpeedRun(*this, TEXT("scale 1"), HoloSpeedWorkedInput());
        TestEqual(TEXT("scale 1: TitleStepWidthPx 120"), out.TitleStepWidthPx, 120.0f);
        HoloSpeedExpectDivider(*this, TEXT("scale 1"), out, 12.0f, 163.0f, 330.0f, 104.0f, 6.0f);
        TestEqual(TEXT("scale 1: right level 157"), out.BrokenStart.Y - out.BrokenStepPx, 157.0f,
            HOLOSPEEDTEST_TOLERANCE);
    }

    // LocalPerPx 0.5
    {
        FSOLHoloSpeedLayoutInput in = HoloSpeedWorkedInput();
        in.LocalPerPx = 0.5f;
        const FSOLHoloFrameSpec out = HoloSpeedRun(*this, TEXT("scale 0.5"), in);
        HoloSpeedExpectDivider(*this, TEXT("scale 0.5"), out, 12.0f, 161.5f, 330.0f, 52.0f, 3.0f);
    }

    // LocalPerPx 0, -1, NaN and +inf behave as 1
    const float badScales[] = { 0.0f, -1.0f, HOLOSPEEDTEST_NAN, HOLOSPEEDTEST_INF };
    for (const float scale : badScales)
    {
        FSOLHoloSpeedLayoutInput in = HoloSpeedWorkedInput();
        in.LocalPerPx = scale;
        const FString label = FString::Printf(TEXT("scale %f"), scale);
        HoloSpeedExpectDivider(*this, label, HoloSpeedRun(*this, label, in), 12.0f, 163.0f, 330.0f, 104.0f, 6.0f);
    }

    // Sections of different widths: the divider spans the outer edges of both
    {
        FSOLHoloSpeedLayoutInput in = HoloSpeedWorkedInput();
        in.StepperSection = HoloSpeedBox(20.0f, 40.0f, 300.0f, 150.0f);
        const FSOLHoloFrameSpec out = HoloSpeedRun(*this, TEXT("different widths"), in);
        HoloSpeedExpectDivider(*this, TEXT("different widths"), out, 12.0f, 163.0f, 330.0f, 104.0f, 6.0f);
    }

    // A gap (150-154) narrower than the step: still present, the levels not clamped to the gap
    {
        FSOLHoloSpeedLayoutInput in = HoloSpeedWorkedInput();
        in.ListSection.Min.Y = 154.0f;
        const FSOLHoloFrameSpec out = HoloSpeedRun(*this, TEXT("narrow gap"), in);
        HoloSpeedExpectDivider(*this, TEXT("narrow gap"), out, 12.0f, 155.0f, 330.0f, 104.0f, 6.0f);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloSpeedLayoutDirtySpecTest, "SOLTest.HoloSpeedLayout.DirtySpec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 28: case 27's input into a dirty spec clears the column divider and tabs, writes the divider as case 27 and
// leaves the frame's own fields
bool FSOLHoloSpeedLayoutDirtySpecTest::RunTest(const FString& /*parameters*/)
{
    FSOLHoloFrameSpec out = HoloSpeedDirtySpec();
    SOLHoloFrameBuild::MakeSpeedLayout(HoloSpeedWorkedInput(), out);
    TestEqual(TEXT("dirty: TitleStepWidthPx 120"), out.TitleStepWidthPx, 120.0f);
    HoloSpeedExpectNoColumnNoTabs(*this, TEXT("dirty"), out);
    HoloSpeedExpectDivider(*this, TEXT("dirty"), out, 12.0f, 163.0f, 330.0f, 104.0f, 6.0f);
    HoloSpeedCheckFrameFieldsKept(*this, TEXT("dirty"), out);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloSpeedLayoutTitleRuleTest, "SOLTest.HoloSpeedLayout.TitleRule",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 20: a title width of 0, -5, NaN, +inf or -inf gives no title step (the divider unaffected); 150 gives 150
bool FSOLHoloSpeedLayoutTitleRuleTest::RunTest(const FString& /*parameters*/)
{
    const float titles[] = { 0.0f, -5.0f, HOLOSPEEDTEST_NAN, HOLOSPEEDTEST_INF, -HOLOSPEEDTEST_INF };
    for (const float title : titles)
    {
        FSOLHoloSpeedLayoutInput in = HoloSpeedWorkedInput();
        in.TitleWidth = title;
        const FString label = FString::Printf(TEXT("title %f"), title);
        const FSOLHoloFrameSpec out = HoloSpeedRun(*this, label, in);
        TestEqual(*FString::Printf(TEXT("%s: no step"), *label), out.TitleStepWidthPx, 0.0f);
        TestTrue(*FString::Printf(TEXT("%s: divider still there"), *label), out.bBrokenDivider);
    }
    FSOLHoloSpeedLayoutInput in = HoloSpeedWorkedInput();
    in.TitleWidth = 150.0f;
    TestEqual(TEXT("title 150: step 150"), HoloSpeedRun(*this, TEXT("title 150"), in).TitleStepWidthPx, 150.0f);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
