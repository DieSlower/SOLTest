/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLRadarLayout.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Contacts are given in ship-local axes (X forward, Y right, Z up, as in SOLFlight's thrust convention); the scope maps
// ship-forward to ScreenOffsetUnit.Y and ship-right to ScreenOffsetUnit.X
namespace
{
    // Range used by the projection tests
    constexpr double RADAR_RANGE_M = 1.0e6;

    //////////////////////////////////////////////////////////////////////////
    // Returns a contact at a ship-relative position
    FSOLRadarContact RadarMakeContact(const FVector3d& relativePositionM, const TCHAR* name = TEXT("C"),
        const bool bSelected = false)
    {
        FSOLRadarContact contact;
        contact.RelativePositionM = relativePositionM;
        contact.Name = FName(name);
        contact.bSelected = bSelected;
        return contact;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a relative tolerance for comparing ranges of the given magnitude
    double RadarTol(const double expectedM)
    {
        return FMath::Max(1e-9, FMath::Abs(expectedM) * 1e-9);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the projected radial distance from the scope center of a contact dead ahead at the given distance
    double RadarAheadRadius(const double distanceM)
    {
        const FSOLRadarPoint point = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(distanceM, 0.0, 0.0)),
            RADAR_RANGE_M);
        return point.ScreenOffsetUnit.Size();
    }
}

// --- EffectiveFloorM ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarEffectiveFloorTest, "SOLTest.RadarLayout.EffectiveFloorByTargetState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The effective floor is 10,000 km with no target selected and 1 km with one selected
bool FSOLRadarEffectiveFloorTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("No target -> DefaultFloorM"), SOLRadar::EffectiveFloorM(false), SOLRadar::DefaultFloorM, 0.0);
    TestEqual(TEXT("No target -> 1e7 m"), SOLRadar::EffectiveFloorM(false), 1.0e7, 0.0);
    TestEqual(TEXT("Target -> TargetedFloorM"), SOLRadar::EffectiveFloorM(true), SOLRadar::TargetedFloorM, 0.0);
    TestEqual(TEXT("Target -> 1e3 m"), SOLRadar::EffectiveFloorM(true), 1.0e3, 0.0);
    return true;
}

// --- StepManualRange ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarStepBasicTest, "SOLTest.RadarLayout.StepManualRangeInOut",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zooming in divides by ZoomStepFactor and zooming out multiplies by it, when the result stays inside the clamp
bool FSOLRadarStepBasicTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("No target: in from 1e9 -> 1e8"), SOLRadar::StepManualRange(1.0e9, true, false), 1.0e8, RadarTol(1.0e8));
    TestEqual(TEXT("No target: out from 1e9 -> 1e10"), SOLRadar::StepManualRange(1.0e9, false, false), 1.0e10,
        RadarTol(1.0e10));
    TestEqual(TEXT("Target: in from 1e5 -> 1e4"), SOLRadar::StepManualRange(1.0e5, true, true), 1.0e4, RadarTol(1.0e4));
    TestEqual(TEXT("Target: out from 1e5 -> 1e6"), SOLRadar::StepManualRange(1.0e5, false, true), 1.0e6, RadarTol(1.0e6));

    // Non-power-of-10 ranges step by the same factor (no snapping)
    TestEqual(TEXT("No target: in from 3e9 -> 3e8"), SOLRadar::StepManualRange(3.0e9, true, false), 3.0e8, RadarTol(3.0e8));
    TestEqual(TEXT("Target: out from 2.5e3 -> 2.5e4"), SOLRadar::StepManualRange(2.5e3, false, true), 2.5e4,
        RadarTol(2.5e4));
    TestEqual(TEXT("No target: in from MaxRangeM -> MaxRangeM / 10"), SOLRadar::StepManualRange(SOLRadar::MaxRangeM, true,
        false), SOLRadar::MaxRangeM / SOLRadar::ZoomStepFactor, RadarTol(SOLRadar::MaxRangeM / 10.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarStepClampTest, "SOLTest.RadarLayout.StepManualRangeClamps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Steps clamp exactly at the effective floor (in) and at MaxRangeM (out); the floor depends on the target state
bool FSOLRadarStepClampTest::RunTest(const FString& /*parameters*/)
{
    // Zoom-in floor, no target
    TestEqual(TEXT("No target: in from 5e7 -> DefaultFloorM exactly"), SOLRadar::StepManualRange(5.0e7, true, false),
        SOLRadar::DefaultFloorM, 0.0);
    TestEqual(TEXT("No target: in from DefaultFloorM stays at DefaultFloorM"),
        SOLRadar::StepManualRange(SOLRadar::DefaultFloorM, true, false), SOLRadar::DefaultFloorM, 0.0);

    // Zoom-in floor, target selected
    TestEqual(TEXT("Target: in from 5e3 -> TargetedFloorM exactly"), SOLRadar::StepManualRange(5.0e3, true, true),
        SOLRadar::TargetedFloorM, 0.0);
    TestEqual(TEXT("Target: in from TargetedFloorM stays at TargetedFloorM"),
        SOLRadar::StepManualRange(SOLRadar::TargetedFloorM, true, true), SOLRadar::TargetedFloorM, 0.0);

    // The same starting range zooms in further when a target is selected
    TestEqual(TEXT("From 1e7, no target -> stays 1e7"), SOLRadar::StepManualRange(1.0e7, true, false), 1.0e7, 0.0);
    TestEqual(TEXT("From 1e7, target -> 1e6"), SOLRadar::StepManualRange(1.0e7, true, true), 1.0e6, RadarTol(1.0e6));

    // Clearing the target re-clamps a below-floor manual range up to DefaultFloorM, whichever direction is stepped
    TestEqual(TEXT("No target: out from 1e3 -> DefaultFloorM (1e4 is below the floor)"),
        SOLRadar::StepManualRange(1.0e3, false, false), SOLRadar::DefaultFloorM, 0.0);
    TestEqual(TEXT("No target: in from 1e5 -> DefaultFloorM"), SOLRadar::StepManualRange(1.0e5, true, false),
        SOLRadar::DefaultFloorM, 0.0);

    // Zoom-out ceiling, both target states
    for (const bool bHasTarget : { false, true })
    {
        const FString state = bHasTarget ? TEXT("target") : TEXT("no target");
        TestEqual(FString::Printf(TEXT("%s: out from 1e11 -> MaxRangeM exactly (1e12 > 1 AU)"), *state),
            SOLRadar::StepManualRange(1.0e11, false, bHasTarget), SOLRadar::MaxRangeM, 0.0);
        TestEqual(FString::Printf(TEXT("%s: out from MaxRangeM stays at MaxRangeM"), *state),
            SOLRadar::StepManualRange(SOLRadar::MaxRangeM, false, bHasTarget), SOLRadar::MaxRangeM, 0.0);
        TestEqual(FString::Printf(TEXT("%s: out from 30 AU -> MaxRangeM"), *state),
            SOLRadar::StepManualRange(30.0 * SOLTestHelpers::AU_M, false, bHasTarget), SOLRadar::MaxRangeM, 0.0);
        TestEqual(FString::Printf(TEXT("%s: out from 1e10 -> 1e11 (still under 1 AU)"), *state),
            SOLRadar::StepManualRange(1.0e10, false, bHasTarget), 1.0e11, RadarTol(1.0e11));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarStepChainTest, "SOLTest.RadarLayout.StepManualRangeChained",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Chained steps land where repeated single-step application predicts, including a clamp mid-chain
bool FSOLRadarStepChainTest::RunTest(const FString& /*parameters*/)
{
    // No target: 1e9 -in-> 1e8 -in-> 1e7 -out-> 1e8
    double range = 1.0e9;
    range = SOLRadar::StepManualRange(range, true, false);
    TestEqual(TEXT("No target chain step 1 (in)"), range, 1.0e8, RadarTol(1.0e8));
    range = SOLRadar::StepManualRange(range, true, false);
    TestEqual(TEXT("No target chain step 2 (in)"), range, 1.0e7, RadarTol(1.0e7));
    range = SOLRadar::StepManualRange(range, false, false);
    TestEqual(TEXT("No target chain step 3 (out)"), range, 1.0e8, RadarTol(1.0e8));

    // No target, clamp mid-chain: 1e8 -in-> 1e7 -in-> 1e7 (floor) -out-> 1e8
    range = 1.0e8;
    range = SOLRadar::StepManualRange(range, true, false);
    range = SOLRadar::StepManualRange(range, true, false);
    TestEqual(TEXT("No target: second in clamps at the floor"), range, SOLRadar::DefaultFloorM, 0.0);
    range = SOLRadar::StepManualRange(range, false, false);
    TestEqual(TEXT("No target: out from the floor -> 1e8"), range, 1.0e8, RadarTol(1.0e8));

    // Target: 1e5 -in-> 1e4 -in-> 1e3 -in-> 1e3 (floor) -out-> 1e4
    range = 1.0e5;
    range = SOLRadar::StepManualRange(range, true, true);
    range = SOLRadar::StepManualRange(range, true, true);
    TestEqual(TEXT("Target chain: two ins -> 1e3"), range, 1.0e3, RadarTol(1.0e3));
    range = SOLRadar::StepManualRange(range, true, true);
    TestEqual(TEXT("Target chain: third in clamps at TargetedFloorM"), range, SOLRadar::TargetedFloorM, 0.0);
    range = SOLRadar::StepManualRange(range, false, true);
    TestEqual(TEXT("Target chain: out -> 1e4"), range, 1.0e4, RadarTol(1.0e4));

    // Out to the ceiling then back in: 1e10 -out-> 1e11 -out-> MaxRangeM -in-> MaxRangeM / 10
    range = 1.0e10;
    range = SOLRadar::StepManualRange(range, false, false);
    range = SOLRadar::StepManualRange(range, false, false);
    TestEqual(TEXT("Ceiling chain: clamps at MaxRangeM"), range, SOLRadar::MaxRangeM, 0.0);
    range = SOLRadar::StepManualRange(range, true, false);
    TestEqual(TEXT("Ceiling chain: in from MaxRangeM -> MaxRangeM / 10"), range, SOLRadar::MaxRangeM / 10.0,
        RadarTol(SOLRadar::MaxRangeM / 10.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarStepDegenerateTest, "SOLTest.RadarLayout.StepManualRangeDegenerateInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero or negative current ranges never yield NaN or a negative value; they clamp to the effective floor
bool FSOLRadarStepDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const double inputs[] = { 0.0, -0.0, -1.0, -5.0e8, -SOLRadar::MaxRangeM };
    for (const double input : inputs)
    {
        for (const bool bZoomIn : { true, false })
        {
            for (const bool bHasTarget : { false, true })
            {
                const double result = SOLRadar::StepManualRange(input, bZoomIn, bHasTarget);
                const double floor = bHasTarget ? SOLRadar::TargetedFloorM : SOLRadar::DefaultFloorM;
                const FString label = FString::Printf(TEXT("input %g, %s, %s"), input, bZoomIn ? TEXT("in") : TEXT("out"),
                    bHasTarget ? TEXT("target") : TEXT("no target"));
                TestFalse(FString::Printf(TEXT("%s: not NaN"), *label), FMath::IsNaN(result));
                TestTrue(FString::Printf(TEXT("%s: positive"), *label), result > 0.0);
                TestEqual(FString::Printf(TEXT("%s: clamps to the floor"), *label), result, floor, 0.0);
            }
        }
    }
    return true;
}

// --- ProjectContact ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarProjectAheadTest, "SOLTest.RadarLayout.ProjectAheadMapsToPlusY",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A contact dead ahead plots on +Y with no lateral X offset, is not behind, and has no height stalk
bool FSOLRadarProjectAheadTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRadarPoint point = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(0.5 * RADAR_RANGE_M, 0.0, 0.0)),
        RADAR_RANGE_M);
    TestTrue(TEXT("Y > 0 (ship-forward is up on the scope)"), point.ScreenOffsetUnit.Y > 0.0);
    TestTrue(TEXT("Radial distance strictly inside the unit disc"), point.ScreenOffsetUnit.Y < 1.0);
    TestEqual(TEXT("No lateral X offset"), point.ScreenOffsetUnit.X, 0.0, 1e-12);
    TestEqual(TEXT("No height stalk"), point.HeightStalkUnit, 0.0, 1e-12);
    TestFalse(TEXT("Not behind"), point.bBehind);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarProjectRightTest, "SOLTest.RadarLayout.ProjectRightMapsToPlusX",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A contact to starboard plots on +X; port on -X; a diagonal contact plots on the diagonal
bool FSOLRadarProjectRightTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRadarPoint right = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(0.0, 0.3 * RADAR_RANGE_M, 0.0)),
        RADAR_RANGE_M);
    TestTrue(TEXT("Right: X > 0"), right.ScreenOffsetUnit.X > 0.0);
    TestEqual(TEXT("Right: Y == 0"), right.ScreenOffsetUnit.Y, 0.0, 1e-12);
    TestFalse(TEXT("Right: forward component 0 is not behind"), right.bBehind);

    const FSOLRadarPoint left = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(0.0, -0.3 * RADAR_RANGE_M, 0.0)),
        RADAR_RANGE_M);
    TestTrue(TEXT("Left: X < 0"), left.ScreenOffsetUnit.X < 0.0);
    TestEqual(TEXT("Left mirrors right"), left.ScreenOffsetUnit.X, -right.ScreenOffsetUnit.X, 1e-12);

    const FSOLRadarPoint diagonal = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(0.2 * RADAR_RANGE_M, 0.2 * RADAR_RANGE_M, 0.0)), RADAR_RANGE_M);
    TestTrue(TEXT("Diagonal: X > 0"), diagonal.ScreenOffsetUnit.X > 0.0);
    TestEqual(TEXT("Diagonal: X == Y"), diagonal.ScreenOffsetUnit.X, diagonal.ScreenOffsetUnit.Y, 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarProjectBehindTest, "SOLTest.RadarLayout.ProjectBehindStillPlotted",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A contact astern is flagged bBehind, plotted on -Y (not zeroed), and mirrors the same contact ahead
bool FSOLRadarProjectBehindTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRadarPoint behind = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(-0.5 * RADAR_RANGE_M, 0.0, 0.0)),
        RADAR_RANGE_M);
    const FSOLRadarPoint ahead = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(0.5 * RADAR_RANGE_M, 0.0, 0.0)),
        RADAR_RANGE_M);
    TestTrue(TEXT("bBehind set"), behind.bBehind);
    TestTrue(TEXT("Plotted on -Y"), behind.ScreenOffsetUnit.Y < 0.0);
    TestTrue(TEXT("Not collapsed to the center"), behind.ScreenOffsetUnit.Size() > 0.0);
    TestEqual(TEXT("Same radius as the mirrored contact ahead"), behind.ScreenOffsetUnit.Size(),
        ahead.ScreenOffsetUnit.Size(), 1e-12);

    const FSOLRadarPoint slightlyBehind = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(-1.0, 0.4 * RADAR_RANGE_M, 0.0)), RADAR_RANGE_M);
    TestTrue(TEXT("Any negative forward component is behind"), slightlyBehind.bBehind);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarProjectHeightTest, "SOLTest.RadarLayout.ProjectHeightStalk",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ship-up becomes a positive stalk and ship-down a negative one, within [-1, 1]; beyond Range it keeps the 3D proportion
bool FSOLRadarProjectHeightTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRadarPoint above = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(0.0, 0.0, 0.5 * RADAR_RANGE_M)),
        RADAR_RANGE_M);
    TestTrue(TEXT("Above: stalk > 0"), above.HeightStalkUnit > 0.0);
    TestTrue(TEXT("Above: stalk <= 1"), above.HeightStalkUnit <= 1.0);
    TestEqual(TEXT("Above: no lateral X offset"), above.ScreenOffsetUnit.X, 0.0, 1e-12);
    TestFalse(TEXT("Above: not behind"), above.bBehind);

    const FSOLRadarPoint below = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(0.0, 0.0, -0.5 * RADAR_RANGE_M)),
        RADAR_RANGE_M);
    TestTrue(TEXT("Below: stalk < 0"), below.HeightStalkUnit < 0.0);
    TestEqual(TEXT("Below mirrors above"), below.HeightStalkUnit, -above.HeightStalkUnit, 1e-12);

    const FSOLRadarPoint farAbove = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(0.1 * RADAR_RANGE_M, 0.0, 10.0 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestTrue(TEXT("Far above: stalk saturates at <= 1"), farAbove.HeightStalkUnit > 0.0 && farAbove.HeightStalkUnit <= 1.0);

    // Pure up/down beyond Range: the whole-vector scale leaves the full stalk on the height axis and nothing laterally
    const FSOLRadarPoint pureAboveBeyond = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(0.0, 0.0, 5.0 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestEqual(TEXT("Pure above beyond Range: stalk 1"), pureAboveBeyond.HeightStalkUnit, 1.0, 1e-9);
    TestEqual(TEXT("Pure above beyond Range: no lateral offset"), pureAboveBeyond.ScreenOffsetUnit.Size(), 0.0, 1e-12);
    const FSOLRadarPoint pureBelowBeyond = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(0.0, 0.0, -5.0 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestEqual(TEXT("Pure below beyond Range: stalk -1"), pureBelowBeyond.HeightStalkUnit, -1.0, 1e-9);

    // Up-and-ahead beyond Range (3:4 forward:up at 10 Range): scaled as a whole to (0.6, 0.8), not (1, 1)
    const FSOLRadarPoint aheadAbove = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(6.0 * RADAR_RANGE_M, 0.0, 8.0 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestEqual(TEXT("3:4 ahead-above beyond Range: Y 0.6"), aheadAbove.ScreenOffsetUnit.Y, 0.6, 1e-9);
    TestEqual(TEXT("3:4 ahead-above beyond Range: X 0"), aheadAbove.ScreenOffsetUnit.X, 0.0, 1e-12);
    TestEqual(TEXT("3:4 ahead-above beyond Range: stalk 0.8"), aheadAbove.HeightStalkUnit, 0.8, 1e-9);
    TestEqual(TEXT("3:4 ahead-above beyond Range: stalk / lateral == up / forward"),
        aheadAbove.HeightStalkUnit / aheadAbove.ScreenOffsetUnit.Size(), 8.0 / 6.0, 1e-9);

    // Down-and-to-port beyond Range: signs kept, same 3:4 proportion
    const FSOLRadarPoint portBelow = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(0.0, -6.0 * RADAR_RANGE_M, -8.0 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestEqual(TEXT("3:4 port-below beyond Range: X -0.6"), portBelow.ScreenOffsetUnit.X, -0.6, 1e-9);
    TestEqual(TEXT("3:4 port-below beyond Range: stalk -0.8"), portBelow.HeightStalkUnit, -0.8, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarProjectEdgeTest, "SOLTest.RadarLayout.ProjectRangeEdgeAndSaturation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At exactly Range the radius is 1; beyond Range the whole 3D vector is scaled to Range, keeping its true direction
bool FSOLRadarProjectEdgeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRadarPoint edge = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(RADAR_RANGE_M, 0.0, 0.0)),
        RADAR_RANGE_M);
    TestEqual(TEXT("At Range: radius 1 on +Y"), edge.ScreenOffsetUnit.Y, 1.0, 1e-9);
    TestEqual(TEXT("At Range: X 0"), edge.ScreenOffsetUnit.X, 0.0, 1e-12);

    const FSOLRadarPoint beyond = SOLRadar::ProjectContact(RadarMakeContact(FVector3d(0.0, -3.0 * RADAR_RANGE_M, 0.0)),
        RADAR_RANGE_M);
    TestEqual(TEXT("Beyond Range: saturates at radius 1"), beyond.ScreenOffsetUnit.Size(), 1.0, 1e-9);
    TestEqual(TEXT("Beyond Range: on -X"), beyond.ScreenOffsetUnit.X, -1.0, 1e-9);

    const FSOLRadarPoint diagonalBeyond = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(-5.0 * RADAR_RANGE_M, 5.0 * RADAR_RANGE_M, 0.0)), RADAR_RANGE_M);
    TestEqual(TEXT("Diagonal beyond: radius 1"), diagonalBeyond.ScreenOffsetUnit.Size(), 1.0, 1e-9);
    TestTrue(TEXT("Diagonal beyond: each axis within [-1, 1]"),
        FMath::Abs(diagonalBeyond.ScreenOffsetUnit.X) <= 1.0 && FMath::Abs(diagonalBeyond.ScreenOffsetUnit.Y) <= 1.0);

    // 45 degrees between forward and up at 10 Range: the whole vector scales to length 1, so each axis is 1/sqrt(2)
    const double invSqrt2 = 1.0 / FMath::Sqrt(2.0);
    const FSOLRadarPoint diagonalUp = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(10.0 * RADAR_RANGE_M, 0.0, 10.0 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestEqual(TEXT("45 deg up beyond Range: Y 1/sqrt(2)"), diagonalUp.ScreenOffsetUnit.Y, invSqrt2, 1e-9);
    TestEqual(TEXT("45 deg up beyond Range: stalk 1/sqrt(2)"), diagonalUp.HeightStalkUnit, invSqrt2, 1e-9);
    TestEqual(TEXT("45 deg up beyond Range: combined (lateral, height) length 1"),
        FMath::Sqrt(diagonalUp.ScreenOffsetUnit.SizeSquared() + FMath::Square(diagonalUp.HeightStalkUnit)), 1.0, 1e-9);

    // Each component within Range but the whole vector beyond it (0.6, 0, 0.9): scaled by Range / |v|
    const double length = FMath::Sqrt(0.6 * 0.6 + 0.9 * 0.9);
    const FSOLRadarPoint justBeyond = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(0.6 * RADAR_RANGE_M, 0.0, 0.9 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestEqual(TEXT("Components inside, vector beyond: Y 0.6 / |v|"), justBeyond.ScreenOffsetUnit.Y, 0.6 / length, 1e-9);
    TestEqual(TEXT("Components inside, vector beyond: stalk 0.9 / |v|"), justBeyond.HeightStalkUnit, 0.9 / length, 1e-9);

    // Astern-and-above beyond Range: still flagged behind, plotted on -Y, proportional to the true direction
    const FSOLRadarPoint behindAbove = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(-6.0 * RADAR_RANGE_M, 0.0, 8.0 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestTrue(TEXT("Astern-above beyond Range: bBehind"), behindAbove.bBehind);
    TestEqual(TEXT("Astern-above beyond Range: Y -0.6"), behindAbove.ScreenOffsetUnit.Y, -0.6, 1e-9);
    TestEqual(TEXT("Astern-above beyond Range: stalk 0.8"), behindAbove.HeightStalkUnit, 0.8, 1e-9);

    // Full 3D direction (forward, right, up) = (2, 3, 6) * 10 Range: |v| = 70 Range, projected to (3/7, 2/7) and 6/7
    const FSOLRadarPoint oblique = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(20.0 * RADAR_RANGE_M, 30.0 * RADAR_RANGE_M, 60.0 * RADAR_RANGE_M)), RADAR_RANGE_M);
    TestEqual(TEXT("Oblique beyond Range: X 3/7"), oblique.ScreenOffsetUnit.X, 3.0 / 7.0, 1e-9);
    TestEqual(TEXT("Oblique beyond Range: Y 2/7"), oblique.ScreenOffsetUnit.Y, 2.0 / 7.0, 1e-9);
    TestEqual(TEXT("Oblique beyond Range: stalk 6/7"), oblique.HeightStalkUnit, 6.0 / 7.0, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarProjectZeroTest, "SOLTest.RadarLayout.ProjectZeroVectorNoNaN",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A contact exactly at the ship projects to the scope center with no stalk and no NaN
bool FSOLRadarProjectZeroTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRadarPoint point = SOLRadar::ProjectContact(RadarMakeContact(FVector3d::ZeroVector), RADAR_RANGE_M);
    TestFalse(TEXT("X not NaN"), FMath::IsNaN(point.ScreenOffsetUnit.X));
    TestFalse(TEXT("Y not NaN"), FMath::IsNaN(point.ScreenOffsetUnit.Y));
    TestFalse(TEXT("Stalk not NaN"), FMath::IsNaN(point.HeightStalkUnit));
    TestEqual(TEXT("At the center"), point.ScreenOffsetUnit.Size(), 0.0, 1e-12);
    TestEqual(TEXT("No stalk"), point.HeightStalkUnit, 0.0, 1e-12);
    TestFalse(TEXT("Not behind"), point.bBehind);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarProjectCollapseTest, "SOLTest.RadarLayout.ProjectCollapseAndMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Contacts inside Range*CollapseFraction are pushed out from the exact center; radius grows strictly with distance
bool FSOLRadarProjectCollapseTest::RunTest(const FString& /*parameters*/)
{
    const double innerM = 0.001 * RADAR_RANGE_M;
    const double innerRadius = RadarAheadRadius(innerM);
    TestTrue(TEXT("Inside the collapse zone: not at the exact center"), innerRadius > 0.0);
    TestTrue(TEXT("Inside the collapse zone: pulled outward beyond the linear radius"), innerRadius > innerM / RADAR_RANGE_M);

    // Radius increases strictly with distance up to Range
    const double distances[] = { 0.0001, 0.001, 0.005, 0.01, 0.02, 0.1, 0.25, 0.5, 0.75, 0.999 };
    double previous = 0.0;
    for (const double fraction : distances)
    {
        const double radius = RadarAheadRadius(fraction * RADAR_RANGE_M);
        TestTrue(FString::Printf(TEXT("Radius at %.4f Range (%.6f) > previous (%.6f)"), fraction, radius, previous),
            radius > previous);
        TestTrue(FString::Printf(TEXT("Radius at %.4f Range stays < 1"), fraction), radius < 1.0);
        previous = radius;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarProjectPassThroughTest, "SOLTest.RadarLayout.ProjectPassesNameAndSelection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Name and bSelected pass through the projection unchanged
bool FSOLRadarProjectPassThroughTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRadarPoint selected = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(1000.0, 2000.0, 3000.0), TEXT("Mars"), true), RADAR_RANGE_M);
    TestEqual(TEXT("Name passes through"), selected.Name, FName(TEXT("Mars")));
    TestTrue(TEXT("bSelected passes through (true)"), selected.bSelected);

    const FSOLRadarPoint unselected = SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(-1000.0, 2000.0, 3000.0), TEXT("Phobos"), false), RADAR_RANGE_M);
    TestEqual(TEXT("Name passes through"), unselected.Name, FName(TEXT("Phobos")));
    TestFalse(TEXT("bSelected passes through (false)"), unselected.bSelected);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
