/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLRadarLayout.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <limits>

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

// --- Hologram radar helpers (SDD 21 phase C) ---

namespace
{
    // Tolerances of the hologram radar tests
    constexpr double RADAR_VEC_TOLERANCE = 1e-9;        // Ship-frame vectors of unit-scale offsets
    constexpr double RADAR_PX_TOLERANCE = 1e-9;         // Screen pixels and dash spans
    constexpr double RADAR_LARGE_TOLERANCE = 1e-3;      // Ship-frame vectors of offsets near 1e11 m
    constexpr double RADAR_SONAR_TOLERANCE = 1e-9;      // Sonar intensities

    // Sentinel written to DashSegment outputs to detect an untouched value
    constexpr double RADAR_DASH_SENTINEL = -12345.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship orientation from pitch, yaw and roll in degrees
    FQuat4d RadarOrientation(const double pitchDeg, const double yawDeg, const double rollDeg)
    {
        return FQuat4d(FRotator3d(pitchDeg, yawDeg, rollDeg));
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that two vectors agree componentwise within a tolerance
    void RadarCheckVector(FAutomationTestBase& test, const FString& label, const FVector3d& actual,
        const FVector3d& expected, const double tolerance)
    {
        test.TestEqual(label + TEXT(": X"), actual.X, expected.X, tolerance);
        test.TestEqual(label + TEXT(": Y"), actual.Y, expected.Y, tolerance);
        test.TestEqual(label + TEXT(": Z"), actual.Z, expected.Z, tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that two 2D points agree componentwise within a tolerance
    void RadarCheckPoint(FAutomationTestBase& test, const FString& label, const FVector2D& actual,
        const FVector2D& expected, const double tolerance)
    {
        test.TestEqual(label + TEXT(": X"), actual.X, expected.X, tolerance);
        test.TestEqual(label + TEXT(": Y"), actual.Y, expected.Y, tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the dash count by walking the dash starts (the reference for DashCount)
    int32 RadarReferenceDashCount(const double lengthPx, const double dashPx, const double gapPx)
    {
        if (lengthPx <= 0.0 || dashPx <= 0.0)
        {
            return 0;
        }
        const double period = dashPx + FMath::Max(gapPx, 0.0);
        int32 count = 0;
        for (double start = 0.0; start < lengthPx; start += period)
        {
            ++count;
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the sonar intensity from the SOLRadar constants (the reference formula)
    double RadarReferenceSonar(const double ringFraction, const double timeS, const double strength)
    {
        const double s = FMath::Clamp(strength, 0.0, SOLRadar::SonarMaxStrength);
        const double wave = FMath::Abs(FMath::Sin(UE_DOUBLE_PI * (timeS * SOLRadar::SonarRingsPerSecond - ringFraction)));
        return SOLRadar::SonarBaseIntensity + SOLRadar::SonarPulseIntensity * s
            * FMath::Pow(wave, SOLRadar::SonarSharpness);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contacts of the SelectContacts tests (distances 50, 10, 30 sel, 10, 5 sel, 20; 1 and 3 tie at 10)
    TArray<FSOLRadarContact> RadarSelectionContacts()
    {
        TArray<FSOLRadarContact> contacts;
        contacts.Add(RadarMakeContact(FVector3d(50.0, 0.0, 0.0), TEXT("C0")));
        contacts.Add(RadarMakeContact(FVector3d(10.0, 0.0, 0.0), TEXT("C1")));
        contacts.Add(RadarMakeContact(FVector3d(0.0, 0.0, 30.0), TEXT("C2"), true));
        contacts.Add(RadarMakeContact(FVector3d(0.0, -10.0, 0.0), TEXT("C3")));
        contacts.Add(RadarMakeContact(FVector3d(-5.0, 0.0, 0.0), TEXT("C4"), true));
        contacts.Add(RadarMakeContact(FVector3d(0.0, 0.0, -20.0), TEXT("C5")));
        return contacts;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that an index order equals the expected one
    void RadarCheckOrder(FAutomationTestBase& test, const FString& label, const TArray<int32>& actual,
        const TArray<int32>& expected)
    {
        if (!test.TestEqual(label + TEXT(": count"), actual.Num(), expected.Num()))
        {
            return;
        }
        for (int32 i = 0; i < expected.Num(); ++i)
        {
            test.TestEqual(FString::Printf(TEXT("%s: slot %d"), *label, i), actual[i], expected[i]);
        }
    }
}

// --- ToShipFrame ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarToShipFrameIdentityTest, "SOLTest.RadarLayout.ToShipFrameIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With an identity orientation the ship frame is the world offset contact - ship
bool FSOLRadarToShipFrameIdentityTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d ship(100.0, -200.0, 300.0);
    const FVector3d contact(150.0, -180.0, 290.0);
    RadarCheckVector(*this, TEXT("Identity"), SOLRadar::ToShipFrame(contact, ship, FQuat4d::Identity),
        FVector3d(50.0, 20.0, -10.0), RADAR_VEC_TOLERANCE);
    RadarCheckVector(*this, TEXT("Contact at the ship"), SOLRadar::ToShipFrame(ship, ship, FQuat4d::Identity),
        FVector3d::ZeroVector, RADAR_VEC_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarToShipFrameYawTest, "SOLTest.RadarLayout.ToShipFrameYaw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ship yawed 90 deg faces world +Y (ahead) with its right side toward world -X
bool FSOLRadarToShipFrameYawTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d yawed = RadarOrientation(0.0, 90.0, 0.0);
    const FVector3d ship(10.0, 20.0, 30.0);
    RadarCheckVector(*this, TEXT("World +Y is ahead"), SOLRadar::ToShipFrame(ship + FVector3d(0.0, 7.0, 0.0), ship,
        yawed), FVector3d(7.0, 0.0, 0.0), RADAR_VEC_TOLERANCE);
    RadarCheckVector(*this, TEXT("World -X is to the right"), SOLRadar::ToShipFrame(ship + FVector3d(-4.0, 0.0, 0.0),
        ship, yawed), FVector3d(0.0, 4.0, 0.0), RADAR_VEC_TOLERANCE);
    RadarCheckVector(*this, TEXT("World +X is to the left"), SOLRadar::ToShipFrame(ship + FVector3d(4.0, 0.0, 0.0),
        ship, yawed), FVector3d(0.0, -4.0, 0.0), RADAR_VEC_TOLERANCE);
    RadarCheckVector(*this, TEXT("World up stays up"), SOLRadar::ToShipFrame(ship + FVector3d(0.0, 0.0, 3.0), ship,
        yawed), FVector3d(0.0, 0.0, 3.0), RADAR_VEC_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarToShipFramePitchTest, "SOLTest.RadarLayout.ToShipFramePitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ship pitched up 90 deg sees a world-up contact dead ahead, and its world-forward contact below the plane
bool FSOLRadarToShipFramePitchTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d pitched = RadarOrientation(90.0, 0.0, 0.0);
    const FVector3d ship(-1000.0, 0.0, 5.0);
    RadarCheckVector(*this, TEXT("World up is ahead"), SOLRadar::ToShipFrame(ship + FVector3d(0.0, 0.0, 9.0), ship,
        pitched), FVector3d(9.0, 0.0, 0.0), RADAR_VEC_TOLERANCE);
    const FVector3d worldForward = SOLRadar::ToShipFrame(ship + FVector3d(9.0, 0.0, 0.0), ship, pitched);
    RadarCheckVector(*this, TEXT("World forward is below the ship plane"), worldForward, FVector3d(0.0, 0.0, -9.0),
        RADAR_VEC_TOLERANCE);
    RadarCheckVector(*this, TEXT("World right stays right"), SOLRadar::ToShipFrame(ship + FVector3d(0.0, 2.0, 0.0), ship,
        pitched), FVector3d(0.0, 2.0, 0.0), RADAR_VEC_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarToShipFrameRollTest, "SOLTest.RadarLayout.ToShipFrameRoll",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ship rolled 90 deg has world up on its side: no height, no forward part, magnitude preserved, and the side matches
// FQuat4d's own unrotation
bool FSOLRadarToShipFrameRollTest::RunTest(const FString& /*parameters*/)
{
    const FQuat4d rolled = RadarOrientation(0.0, 0.0, 90.0);
    const FVector3d ship(3.0, 4.0, 5.0);
    const FVector3d worldOffset(0.0, 0.0, 6.0);
    const FVector3d local = SOLRadar::ToShipFrame(ship + worldOffset, ship, rolled);
    TestEqual(TEXT("World up has no ship-up height"), local.Z, 0.0, RADAR_VEC_TOLERANCE);
    TestEqual(TEXT("World up has no forward part"), local.X, 0.0, RADAR_VEC_TOLERANCE);
    TestEqual(TEXT("World up lies fully to one side"), FMath::Abs(local.Y), 6.0, RADAR_VEC_TOLERANCE);
    TestEqual(TEXT("Magnitude preserved"), local.Size(), worldOffset.Size(), RADAR_VEC_TOLERANCE);
    RadarCheckVector(*this, TEXT("Matches the quaternion's inverse rotation"), local,
        rolled.Inverse().RotateVector(worldOffset), RADAR_VEC_TOLERANCE);

    // A combined attitude preserves magnitude too
    const FQuat4d combined = RadarOrientation(30.0, -70.0, 45.0);
    const FVector3d oblique(12.0, -7.0, 3.5);
    const FVector3d obliqueLocal = SOLRadar::ToShipFrame(ship + oblique, ship, combined);
    TestEqual(TEXT("Combined attitude: magnitude preserved"), obliqueLocal.Size(), oblique.Size(), RADAR_VEC_TOLERANCE);
    RadarCheckVector(*this, TEXT("Combined attitude: matches the inverse rotation"), obliqueLocal,
        combined.Inverse().RotateVector(oblique), RADAR_VEC_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarToShipFrameLargeTest, "SOLTest.RadarLayout.ToShipFrameLargeOffsets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ship and contact ~1e11 m from the origin: the small offset between them survives at double precision
bool FSOLRadarToShipFrameLargeTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d ship(1.0e11, -2.0e11, 3.0e10);
    const FVector3d offset(1.5, 2.25, -0.5);
    RadarCheckVector(*this, TEXT("Identity, 1e11 m from the origin"), SOLRadar::ToShipFrame(ship + offset, ship,
        FQuat4d::Identity), offset, RADAR_LARGE_TOLERANCE);
    RadarCheckVector(*this, TEXT("Yawed 90 deg, 1e11 m from the origin"), SOLRadar::ToShipFrame(ship + offset, ship,
        RadarOrientation(0.0, 90.0, 0.0)), FVector3d(2.25, -1.5, -0.5), RADAR_LARGE_TOLERANCE);

    // An offset of 1e11 m itself keeps its value
    const FVector3d farContact = ship + FVector3d(1.0e11, 0.0, 0.0);
    RadarCheckVector(*this, TEXT("A 1e11 m offset"), SOLRadar::ToShipFrame(farContact, ship, FQuat4d::Identity),
        FVector3d(1.0e11, 0.0, 0.0), RADAR_LARGE_TOLERANCE);
    return true;
}

// --- ClassifySide and ContactStyle ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarClassifySideTest, "SOLTest.RadarLayout.ClassifySide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Heights >= 0 (and NaN) are Above, negative heights are Below
bool FSOLRadarClassifySideTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("Positive is Above"), SOLRadar::ClassifySide(0.5) == ESOLRadarSide::Above);
    TestTrue(TEXT("1 is Above"), SOLRadar::ClassifySide(1.0) == ESOLRadarSide::Above);
    TestTrue(TEXT("Tiny positive is Above"), SOLRadar::ClassifySide(1e-300) == ESOLRadarSide::Above);
    TestTrue(TEXT("0 is Above (on the plane)"), SOLRadar::ClassifySide(0.0) == ESOLRadarSide::Above);
    TestTrue(TEXT("-0.0 is Above (not < 0)"), SOLRadar::ClassifySide(-0.0) == ESOLRadarSide::Above);
    TestTrue(TEXT("Negative is Below"), SOLRadar::ClassifySide(-0.5) == ESOLRadarSide::Below);
    TestTrue(TEXT("-1 is Below"), SOLRadar::ClassifySide(-1.0) == ESOLRadarSide::Below);
    TestTrue(TEXT("Tiny negative is Below"), SOLRadar::ClassifySide(-1e-300) == ESOLRadarSide::Below);
    TestTrue(TEXT("NaN is Above"), SOLRadar::ClassifySide(std::numeric_limits<double>::quiet_NaN())
        == ESOLRadarSide::Above);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarContactStyleTest, "SOLTest.RadarLayout.ContactStyle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Below the plane: dashed stem, hollow blip; above or on it: solid and filled; bLarge mirrors bSelected
bool FSOLRadarContactStyleTest::RunTest(const FString& /*parameters*/)
{
    const double heights[] = { 0.7, 0.0, -0.3 };
    for (const double height : heights)
    {
        for (const bool bSelected : { false, true })
        {
            FSOLRadarPoint point;
            point.HeightStalkUnit = height;
            point.bSelected = bSelected;
            const FSOLRadarStyle style = SOLRadar::ContactStyle(point);
            const bool bBelow = height < 0.0;
            const FString label = FString::Printf(TEXT("height %.1f, %s"), height,
                bSelected ? TEXT("selected") : TEXT("unselected"));
            TestTrue(label + TEXT(": side"), style.Side == (bBelow ? ESOLRadarSide::Below : ESOLRadarSide::Above));
            TestTrue(label + TEXT(": dashed stem"), style.bDashedStem == bBelow);
            TestTrue(label + TEXT(": hollow blip"), style.bHollowBlip == bBelow);
            TestTrue(label + TEXT(": large"), style.bLarge == bSelected);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarContactStyleProjectedTest, "SOLTest.RadarLayout.ContactStyleOfProjection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A projected contact below the ship plane gets the Below style, one above gets the Above style
bool FSOLRadarContactStyleProjectedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRadarStyle below = SOLRadar::ContactStyle(SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(0.2 * RADAR_RANGE_M, 0.1 * RADAR_RANGE_M, -0.3 * RADAR_RANGE_M), TEXT("B"), true),
        RADAR_RANGE_M));
    TestTrue(TEXT("Z < 0: Below"), below.Side == ESOLRadarSide::Below);
    TestTrue(TEXT("Z < 0: dashed stem"), below.bDashedStem);
    TestTrue(TEXT("Z < 0: hollow blip"), below.bHollowBlip);
    TestTrue(TEXT("Selected: large"), below.bLarge);

    const FSOLRadarStyle above = SOLRadar::ContactStyle(SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(-0.2 * RADAR_RANGE_M, 0.1 * RADAR_RANGE_M, 0.3 * RADAR_RANGE_M)), RADAR_RANGE_M));
    TestTrue(TEXT("Z > 0: Above"), above.Side == ESOLRadarSide::Above);
    TestFalse(TEXT("Z > 0: solid stem"), above.bDashedStem);
    TestFalse(TEXT("Z > 0: filled blip"), above.bHollowBlip);
    TestFalse(TEXT("Unselected: not large"), above.bLarge);

    const FSOLRadarStyle onPlane = SOLRadar::ContactStyle(SOLRadar::ProjectContact(
        RadarMakeContact(FVector3d(0.4 * RADAR_RANGE_M, 0.0, 0.0)), RADAR_RANGE_M));
    TestTrue(TEXT("Z == 0: Above"), onPlane.Side == ESOLRadarSide::Above);
    TestFalse(TEXT("Z == 0: solid stem"), onPlane.bDashedStem);
    return true;
}

// --- DashCount / DashSegment ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarDashExactTest, "SOLTest.RadarLayout.DashExactAndClipped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Length 10 with dash 3 / gap 2 is two dashes [0,3], [5,8]; length 11 adds a third dash clipped to [10,11]
bool FSOLRadarDashExactTest::RunTest(const FString& /*parameters*/)
{
    double start = RADAR_DASH_SENTINEL;
    double end = RADAR_DASH_SENTINEL;

    TestEqual(TEXT("Length 10: 2 dashes"), SOLRadar::DashCount(10.0, 3.0, 2.0), 2);
    TestTrue(TEXT("Length 10: dash 0"), SOLRadar::DashSegment(10.0, 3.0, 2.0, 0, start, end));
    TestEqual(TEXT("Length 10: dash 0 start"), start, 0.0, RADAR_PX_TOLERANCE);
    TestEqual(TEXT("Length 10: dash 0 end"), end, 3.0, RADAR_PX_TOLERANCE);
    TestTrue(TEXT("Length 10: dash 1"), SOLRadar::DashSegment(10.0, 3.0, 2.0, 1, start, end));
    TestEqual(TEXT("Length 10: dash 1 start"), start, 5.0, RADAR_PX_TOLERANCE);
    TestEqual(TEXT("Length 10: dash 1 end"), end, 8.0, RADAR_PX_TOLERANCE);
    TestFalse(TEXT("Length 10: no dash 2 (its start is the length)"), SOLRadar::DashSegment(10.0, 3.0, 2.0, 2, start,
        end));

    TestEqual(TEXT("Length 11: 3 dashes"), SOLRadar::DashCount(11.0, 3.0, 2.0), 3);
    TestTrue(TEXT("Length 11: dash 2"), SOLRadar::DashSegment(11.0, 3.0, 2.0, 2, start, end));
    TestEqual(TEXT("Length 11: dash 2 start"), start, 10.0, RADAR_PX_TOLERANCE);
    TestEqual(TEXT("Length 11: dash 2 clipped to the length"), end, 11.0, RADAR_PX_TOLERANCE);

    // A stem shorter than one dash is one clipped dash
    TestEqual(TEXT("Length 1.5: 1 dash"), SOLRadar::DashCount(1.5, 3.0, 2.0), 1);
    TestTrue(TEXT("Length 1.5: dash 0"), SOLRadar::DashSegment(1.5, 3.0, 2.0, 0, start, end));
    TestEqual(TEXT("Length 1.5: dash 0 start"), start, 0.0, RADAR_PX_TOLERANCE);
    TestEqual(TEXT("Length 1.5: dash 0 clipped"), end, 1.5, RADAR_PX_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarDashDegenerateTest, "SOLTest.RadarLayout.DashDegenerateInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No dashes for a non-positive length or dash; a negative gap counts as 0 (contiguous dashes)
bool FSOLRadarDashDegenerateTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Length 0"), SOLRadar::DashCount(0.0, 3.0, 2.0), 0);
    TestEqual(TEXT("Negative length"), SOLRadar::DashCount(-10.0, 3.0, 2.0), 0);
    TestEqual(TEXT("Dash 0"), SOLRadar::DashCount(10.0, 0.0, 2.0), 0);
    TestEqual(TEXT("Negative dash"), SOLRadar::DashCount(10.0, -3.0, 2.0), 0);

    double start = RADAR_DASH_SENTINEL;
    double end = RADAR_DASH_SENTINEL;
    TestFalse(TEXT("Length 0: no dash 0"), SOLRadar::DashSegment(0.0, 3.0, 2.0, 0, start, end));
    TestFalse(TEXT("Dash 0: no dash 0"), SOLRadar::DashSegment(10.0, 0.0, 2.0, 0, start, end));
    TestEqual(TEXT("Degenerate: start untouched"), start, RADAR_DASH_SENTINEL);
    TestEqual(TEXT("Degenerate: end untouched"), end, RADAR_DASH_SENTINEL);

    // Gap 0 and a negative gap both give contiguous dashes: [0,3], [3,6], [6,9], [9,10]
    for (const double gap : { 0.0, -5.0 })
    {
        const FString label = FString::Printf(TEXT("Gap %.1f"), gap);
        if (!TestEqual(label + TEXT(": 4 dashes"), SOLRadar::DashCount(10.0, 3.0, gap), 4))
        {
            continue;
        }
        for (int32 i = 0; i < 4; ++i)
        {
            TestTrue(FString::Printf(TEXT("%s: dash %d"), *label, i), SOLRadar::DashSegment(10.0, 3.0, gap, i, start,
                end));
            TestEqual(FString::Printf(TEXT("%s: dash %d start"), *label, i), start, 3.0 * i, RADAR_PX_TOLERANCE);
            TestEqual(FString::Printf(TEXT("%s: dash %d end"), *label, i), end, FMath::Min(3.0 * i + 3.0, 10.0),
                RADAR_PX_TOLERANCE);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarDashOutOfRangeTest, "SOLTest.RadarLayout.DashSegmentOutOfRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An index outside [0, DashCount) returns false and leaves both outputs untouched
bool FSOLRadarDashOutOfRangeTest::RunTest(const FString& /*parameters*/)
{
    const int32 count = SOLRadar::DashCount(11.0, 3.0, 2.0);
    const int32 badIndices[] = { -1, -100, count, count + 5, MAX_int32 };
    for (const int32 index : badIndices)
    {
        double start = RADAR_DASH_SENTINEL;
        double end = RADAR_DASH_SENTINEL;
        const FString label = FString::Printf(TEXT("Index %d"), index);
        TestFalse(label + TEXT(": false"), SOLRadar::DashSegment(11.0, 3.0, 2.0, index, start, end));
        TestEqual(label + TEXT(": start untouched"), start, RADAR_DASH_SENTINEL);
        TestEqual(label + TEXT(": end untouched"), end, RADAR_DASH_SENTINEL);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarDashLayoutTest, "SOLTest.RadarLayout.DashSegmentsWithinStem",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Over many stems, the count matches the reference walk and segments follow the period, stay inside and never overlap
bool FSOLRadarDashLayoutTest::RunTest(const FString& /*parameters*/)
{
    const double lengths[] = { 0.5, 3.0, 4.9, 5.0, 5.1, 17.3, 60.0, 61.0, 250.75 };
    const double dashGaps[][2] = { { SOLRadar::StemDashPx, SOLRadar::StemGapPx }, { 3.0, 2.0 }, { 1.0, 4.0 },
        { 7.5, 0.5 }, { 2.0, 0.0 } };
    for (const double length : lengths)
    {
        for (const auto& dashGap : dashGaps)
        {
            const double dash = dashGap[0];
            const double gap = dashGap[1];
            const FString label = FString::Printf(TEXT("length %.2f dash %.1f gap %.1f"), length, dash, gap);
            const int32 count = SOLRadar::DashCount(length, dash, gap);
            TestEqual(label + TEXT(": count"), count, RadarReferenceDashCount(length, dash, gap));
            double previousEnd = 0.0;
            for (int32 i = 0; i < count; ++i)
            {
                double start = RADAR_DASH_SENTINEL;
                double end = RADAR_DASH_SENTINEL;
                if (!TestTrue(FString::Printf(TEXT("%s: dash %d exists"), *label, i),
                    SOLRadar::DashSegment(length, dash, gap, i, start, end)))
                {
                    break;
                }
                const double expectedStart = i * (dash + gap);
                TestEqual(FString::Printf(TEXT("%s: dash %d start"), *label, i), start, expectedStart, 1e-9);
                TestEqual(FString::Printf(TEXT("%s: dash %d end"), *label, i), end,
                    FMath::Min(expectedStart + dash, length), 1e-9);
                TestTrue(FString::Printf(TEXT("%s: dash %d start < length"), *label, i), start < length);
                TestTrue(FString::Printf(TEXT("%s: dash %d end <= length"), *label, i), end <= length + 1e-12);
                TestTrue(FString::Printf(TEXT("%s: dash %d non-empty"), *label, i), end > start);
                TestTrue(FString::Printf(TEXT("%s: dash %d does not overlap the previous"), *label, i),
                    start >= previousEnd - 1e-12);
                previousEnd = end;
            }
        }
    }

    // The default stem pattern over a 60 px stem: one dash per (dash + gap) period, the last one ending inside
    const double period = SOLRadar::StemDashPx + SOLRadar::StemGapPx;
    const int32 defaultCount = SOLRadar::DashCount(60.0, SOLRadar::StemDashPx, SOLRadar::StemGapPx);
    TestEqual(TEXT("60 px default stem: count"), defaultCount, FMath::CeilToInt32(60.0 / period));
    double start = RADAR_DASH_SENTINEL;
    double end = RADAR_DASH_SENTINEL;
    TestTrue(TEXT("60 px default stem: last dash"), SOLRadar::DashSegment(60.0, SOLRadar::StemDashPx,
        SOLRadar::StemGapPx, defaultCount - 1, start, end));
    TestEqual(TEXT("60 px default stem: last dash start"), start, (defaultCount - 1) * period, RADAR_PX_TOLERANCE);
    TestTrue(TEXT("60 px default stem: last dash within the stem"), end <= 60.0);
    return true;
}

// --- ScopeToScreen / StemTop ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarScopeToScreenTest, "SOLTest.RadarLayout.ScopeToScreen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The scope maps to a squashed ellipse: forward up the screen, right to the right, centre to centre
bool FSOLRadarScopeToScreenTest::RunTest(const FString& /*parameters*/)
{
    const FVector2D center(400.0, 300.0);
    constexpr double radius = 100.0;
    constexpr double squash = 0.5;
    RadarCheckPoint(*this, TEXT("Centre"), SOLRadar::ScopeToScreen(center, radius, squash, FVector2D::ZeroVector),
        center, RADAR_PX_TOLERANCE);
    RadarCheckPoint(*this, TEXT("Forward goes up"), SOLRadar::ScopeToScreen(center, radius, squash, FVector2D(0.0, 1.0)),
        FVector2D(400.0, 250.0), RADAR_PX_TOLERANCE);
    RadarCheckPoint(*this, TEXT("Astern goes down"), SOLRadar::ScopeToScreen(center, radius, squash,
        FVector2D(0.0, -1.0)), FVector2D(400.0, 350.0), RADAR_PX_TOLERANCE);
    RadarCheckPoint(*this, TEXT("Right goes right (unsquashed)"), SOLRadar::ScopeToScreen(center, radius, squash,
        FVector2D(1.0, 0.0)), FVector2D(500.0, 300.0), RADAR_PX_TOLERANCE);
    RadarCheckPoint(*this, TEXT("Left goes left"), SOLRadar::ScopeToScreen(center, radius, squash,
        FVector2D(-1.0, 0.0)), FVector2D(300.0, 300.0), RADAR_PX_TOLERANCE);
    RadarCheckPoint(*this, TEXT("General point"), SOLRadar::ScopeToScreen(center, radius, squash,
        FVector2D(0.3, -0.4)), FVector2D(430.0, 320.0), RADAR_PX_TOLERANCE);
    RadarCheckPoint(*this, TEXT("Squash 1 is a circle"), SOLRadar::ScopeToScreen(center, radius, 1.0,
        FVector2D(0.6, 0.8)), FVector2D(460.0, 220.0), RADAR_PX_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarStemTopTest, "SOLTest.RadarLayout.StemTop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The stem top sits straight above the foot by height x radius x stalk scale; negative heights go down
bool FSOLRadarStemTopTest::RunTest(const FString& /*parameters*/)
{
    const FVector2D foot(10.0, 20.0);
    constexpr double radius = 100.0;
    constexpr double stalkScale = 0.8;
    RadarCheckPoint(*this, TEXT("Positive height goes up"), SOLRadar::StemTop(foot, 0.5, radius, stalkScale),
        FVector2D(10.0, -20.0), RADAR_PX_TOLERANCE);
    RadarCheckPoint(*this, TEXT("Negative height goes down"), SOLRadar::StemTop(foot, -0.25, radius, stalkScale),
        FVector2D(10.0, 40.0), RADAR_PX_TOLERANCE);
    RadarCheckPoint(*this, TEXT("Zero height is the foot"), SOLRadar::StemTop(foot, 0.0, radius, stalkScale), foot,
        RADAR_PX_TOLERANCE);
    return true;
}

// --- AzimuthToScope / ShipAzimuth ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarAzimuthTest, "SOLTest.RadarLayout.Azimuth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Azimuth 0 is forward (+Y on the scope) and +pi/2 right (+X); ShipAzimuth is atan2(Y, X), 0 with no lateral part
bool FSOLRadarAzimuthTest::RunTest(const FString& /*parameters*/)
{
    const double halfPi = UE_DOUBLE_HALF_PI;
    RadarCheckPoint(*this, TEXT("0 -> forward"), SOLRadar::AzimuthToScope(0.0), FVector2D(0.0, 1.0),
        RADAR_VEC_TOLERANCE);
    RadarCheckPoint(*this, TEXT("pi/2 -> right"), SOLRadar::AzimuthToScope(halfPi), FVector2D(1.0, 0.0),
        RADAR_VEC_TOLERANCE);
    RadarCheckPoint(*this, TEXT("pi -> astern"), SOLRadar::AzimuthToScope(UE_DOUBLE_PI), FVector2D(0.0, -1.0),
        RADAR_VEC_TOLERANCE);
    RadarCheckPoint(*this, TEXT("-pi/2 -> left"), SOLRadar::AzimuthToScope(-halfPi), FVector2D(-1.0, 0.0),
        RADAR_VEC_TOLERANCE);

    TestEqual(TEXT("Forward -> 0"), SOLRadar::ShipAzimuth(FVector3d(5.0, 0.0, 0.0)), 0.0, RADAR_VEC_TOLERANCE);
    TestEqual(TEXT("Right -> pi/2"), SOLRadar::ShipAzimuth(FVector3d(0.0, 5.0, 0.0)), halfPi, RADAR_VEC_TOLERANCE);
    TestEqual(TEXT("Left -> -pi/2"), SOLRadar::ShipAzimuth(FVector3d(0.0, -5.0, 0.0)), -halfPi, RADAR_VEC_TOLERANCE);
    TestEqual(TEXT("Astern -> |pi|"), FMath::Abs(SOLRadar::ShipAzimuth(FVector3d(-5.0, 0.0, 0.0))), UE_DOUBLE_PI,
        RADAR_VEC_TOLERANCE);
    TestEqual(TEXT("Height ignored: (1, 1, 100) -> pi/4"), SOLRadar::ShipAzimuth(FVector3d(1.0, 1.0, 100.0)),
        UE_DOUBLE_PI * 0.25, RADAR_VEC_TOLERANCE);
    TestEqual(TEXT("Straight up -> 0"), SOLRadar::ShipAzimuth(FVector3d(0.0, 0.0, 7.0)), 0.0);
    TestEqual(TEXT("Zero vector -> 0"), SOLRadar::ShipAzimuth(FVector3d::ZeroVector), 0.0);
    TestFalse(TEXT("Zero vector: not NaN"), FMath::IsNaN(SOLRadar::ShipAzimuth(FVector3d::ZeroVector)));

    // Round trip: the scope direction of a ship-frame vector is its normalized (right, forward) lateral part
    const FVector3d samples[] = { FVector3d(3.0, 4.0, 0.0), FVector3d(-3.0, 4.0, 9.0), FVector3d(-1.0, -2.0, -5.0),
        FVector3d(2.0, -7.0, 0.5), FVector3d(1.0e9, 3.0e8, 0.0) };
    for (const FVector3d& v : samples)
    {
        const double lateral = FMath::Sqrt(v.X * v.X + v.Y * v.Y);
        RadarCheckPoint(*this, FString::Printf(TEXT("Round trip (%.1f, %.1f, %.1f)"), v.X, v.Y, v.Z),
            SOLRadar::AzimuthToScope(SOLRadar::ShipAzimuth(v)), FVector2D(v.Y / lateral, v.X / lateral),
            RADAR_VEC_TOLERANCE);
    }
    return true;
}

// --- SelectContacts ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarSelectOrderTest, "SOLTest.RadarLayout.SelectContactsOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Selected contacts come first by index, then the rest nearest first with ties broken by the lower index
bool FSOLRadarSelectOrderTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLRadarContact> contacts = RadarSelectionContacts();
    TArray<int32> order;
    TestEqual(TEXT("Full budget: returns all"), SOLRadar::SelectContacts(contacts, contacts.Num(), order),
        contacts.Num());
    RadarCheckOrder(*this, TEXT("Full budget"), order, { 2, 4, 1, 3, 5, 0 });

    TestEqual(TEXT("Budget above N: returns N"), SOLRadar::SelectContacts(contacts, 100, order), contacts.Num());
    RadarCheckOrder(*this, TEXT("Budget above N"), order, { 2, 4, 1, 3, 5, 0 });

    TestEqual(TEXT("Budget 3"), SOLRadar::SelectContacts(contacts, 3, order), 3);
    RadarCheckOrder(*this, TEXT("Budget 3"), order, { 2, 4, 1 });

    TestEqual(TEXT("Budget 1: only the first selected"), SOLRadar::SelectContacts(contacts, 1, order), 1);
    RadarCheckOrder(*this, TEXT("Budget 1"), order, { 2 });

    // No selection: purely nearest first (1 and 3 tie at 10 m)
    TArray<FSOLRadarContact> unselected = contacts;
    for (FSOLRadarContact& contact : unselected)
    {
        contact.bSelected = false;
    }
    TestEqual(TEXT("No selection"), SOLRadar::SelectContacts(unselected, unselected.Num(), order), unselected.Num());
    RadarCheckOrder(*this, TEXT("No selection"), order, { 4, 1, 3, 5, 2, 0 });

    // A tie listed in reverse index order still comes out by lower index
    TArray<FSOLRadarContact> ties;
    ties.Add(RadarMakeContact(FVector3d(0.0, 0.0, 8.0), TEXT("T0")));
    ties.Add(RadarMakeContact(FVector3d(8.0, 0.0, 0.0), TEXT("T1")));
    ties.Add(RadarMakeContact(FVector3d(0.0, -8.0, 0.0), TEXT("T2")));
    ties.Add(RadarMakeContact(FVector3d(1.0, 0.0, 0.0), TEXT("T3")));
    SOLRadar::SelectContacts(ties, ties.Num(), order);
    RadarCheckOrder(*this, TEXT("Ties"), order, { 3, 0, 1, 2 });
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarSelectBudgetTest, "SOLTest.RadarLayout.SelectContactsBudgetAndReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or negative budget and an empty input give nothing; outOrder is always reset first
bool FSOLRadarSelectBudgetTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLRadarContact> contacts = RadarSelectionContacts();
    TArray<int32> order = { 99, 98, 97, 96, 95, 94, 93, 92, 91 };
    TestEqual(TEXT("Budget 0: returns 0"), SOLRadar::SelectContacts(contacts, 0, order), 0);
    TestEqual(TEXT("Budget 0: stale entries cleared"), order.Num(), 0);

    order = { 99, 98 };
    TestEqual(TEXT("Negative budget: returns 0"), SOLRadar::SelectContacts(contacts, -5, order), 0);
    TestEqual(TEXT("Negative budget: stale entries cleared"), order.Num(), 0);

    order = { 99, 98, 97, 96, 95, 94, 93, 92, 91 };
    TestEqual(TEXT("Stale longer list: returns 2"), SOLRadar::SelectContacts(contacts, 2, order), 2);
    RadarCheckOrder(*this, TEXT("Stale longer list"), order, { 2, 4 });

    const TArray<FSOLRadarContact> empty;
    order = { 7 };
    TestEqual(TEXT("Empty input: returns 0"), SOLRadar::SelectContacts(empty, SOLRadar::MaxDrawnContacts, order), 0);
    TestEqual(TEXT("Empty input: empty output"), order.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarSelectNoAllocTest, "SOLTest.RadarLayout.SelectContactsNoAllocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With outOrder reserved to the contact count, selecting never reallocates (same buffer, same capacity)
bool FSOLRadarSelectNoAllocTest::RunTest(const FString& /*parameters*/)
{
    // More contacts than the budget, at distinct distances
    constexpr int32 contactCount = 3 * SOLRadar::MaxDrawnContacts;
    TArray<FSOLRadarContact> contacts;
    contacts.Reserve(contactCount);
    for (int32 i = 0; i < contactCount; ++i)
    {
        const double distance = 1000.0 + 37.0 * ((i * 7919) % contactCount);
        contacts.Add(RadarMakeContact(FVector3d(distance, 0.0, 0.0), TEXT("N"), i == contactCount / 2));
    }

    TArray<int32> order;
    order.Reserve(contacts.Num());
    const int32* dataBefore = order.GetData();
    const int32 maxBefore = order.Max();
    for (const int32 budget : { SOLRadar::MaxDrawnContacts, contactCount, 0, 5 })
    {
        const FString label = FString::Printf(TEXT("Budget %d"), budget);
        TestEqual(label + TEXT(": count"), SOLRadar::SelectContacts(contacts, budget, order),
            FMath::Min(contactCount, budget));
        TestTrue(label + TEXT(": same buffer"), order.GetData() == dataBefore);
        TestEqual(label + TEXT(": same capacity"), order.Max(), maxBefore);
    }

    // The budgeted order: the selected one, then strictly non-decreasing distance
    SOLRadar::SelectContacts(contacts, SOLRadar::MaxDrawnContacts, order);
    if (TestEqual(TEXT("Budgeted: MaxDrawnContacts entries"), order.Num(), SOLRadar::MaxDrawnContacts))
    {
        TestEqual(TEXT("Budgeted: selected first"), order[0], contactCount / 2);
        bool bAscending = true;
        for (int32 i = 2; i < order.Num(); ++i)
        {
            bAscending &= contacts[order[i - 1]].RelativePositionM.Size() <= contacts[order[i]].RelativePositionM.Size();
        }
        TestTrue(TEXT("Budgeted: the rest nearest first"), bAscending);
    }
    return true;
}

// --- SonarRingIntensity ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarSonarFormulaTest, "SOLTest.RadarLayout.SonarFormula",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ring glow matches base + pulse s |sin(pi (t rate - r))|^sharpness, with its crest and trough where predicted
bool FSOLRadarSonarFormulaTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadar::SonarRingIntensity;
    const double rate = SOLRadar::SonarRingsPerSecond;
    const double base = SOLRadar::SonarBaseIntensity;
    const double pulse = SOLRadar::SonarPulseIntensity;

    // Formula over a grid
    const double fractions[] = { 0.0, 0.1, 0.25, 0.5, 0.8, 1.0 };
    const double times[] = { 0.0, 0.3, 1.0, 2.7, 10.0 };
    const double strengths[] = { 0.25, 1.0, 1.5, SOLRadar::SonarMaxStrength };
    for (const double r : fractions)
    {
        for (const double t : times)
        {
            for (const double s : strengths)
            {
                TestEqual(FString::Printf(TEXT("r %.2f t %.1f s %.2f"), r, t, s), SonarRingIntensity(r, t, s),
                    RadarReferenceSonar(r, t, s), RADAR_SONAR_TOLERANCE);
            }
        }
    }

    // Crest where t rate - r is a half-integer (|sin| = 1): base + pulse s
    TestEqual(TEXT("Crest at r 0.25, t rate - r = 0.5"), SonarRingIntensity(0.25, 0.75 / rate, 1.0), base + pulse,
        RADAR_SONAR_TOLERANCE);
    TestEqual(TEXT("Crest at r 0.6, t rate - r = 1.5, s 1.5"), SonarRingIntensity(0.6, 2.1 / rate, 1.5),
        base + pulse * 1.5, RADAR_SONAR_TOLERANCE);

    // Trough where t rate - r is an integer (sin = 0): base
    TestEqual(TEXT("Trough at r 0.5, t rate - r = 0"), SonarRingIntensity(0.5, 0.5 / rate, 1.0), base,
        RADAR_SONAR_TOLERANCE);
    TestEqual(TEXT("Trough at r 0.2, t rate - r = 2"), SonarRingIntensity(0.2, 2.2 / rate, 2.0), base,
        RADAR_SONAR_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarSonarStrengthTest, "SOLTest.RadarLayout.SonarStrengthClamp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Strength 0 (and below) is the steady base glow; strength above SonarMaxStrength behaves as SonarMaxStrength
bool FSOLRadarSonarStrengthTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadar::SonarRingIntensity;
    const double rate = SOLRadar::SonarRingsPerSecond;
    bool bSteadyAtZero = true;
    bool bNegativeIsZero = true;
    bool bOverIsMax = true;
    bool bBounded = true;
    for (int32 ri = 0; ri <= 20; ++ri)
    {
        const double r = ri / 20.0;
        for (int32 ti = 0; ti <= 50; ++ti)
        {
            const double t = ti * 0.137;
            bSteadyAtZero &= SonarRingIntensity(r, t, 0.0) == SOLRadar::SonarBaseIntensity;
            bNegativeIsZero &= FMath::IsNearlyEqual(SonarRingIntensity(r, t, -3.0), SOLRadar::SonarBaseIntensity,
                RADAR_SONAR_TOLERANCE);
            bOverIsMax &= FMath::IsNearlyEqual(SonarRingIntensity(r, t, 5.0),
                SonarRingIntensity(r, t, SOLRadar::SonarMaxStrength), RADAR_SONAR_TOLERANCE);
            const double value = SonarRingIntensity(r, t, 1.0);
            bBounded &= value >= SOLRadar::SonarBaseIntensity - RADAR_SONAR_TOLERANCE
                && value <= SOLRadar::SonarBaseIntensity + SOLRadar::SonarPulseIntensity + RADAR_SONAR_TOLERANCE;
        }
    }
    TestTrue(TEXT("Strength 0: exactly the base glow everywhere"), bSteadyAtZero);
    TestTrue(TEXT("Negative strength behaves as 0"), bNegativeIsZero);
    TestTrue(TEXT("Strength 5 behaves as SonarMaxStrength"), bOverIsMax);
    TestTrue(TEXT("Strength 1: within [base, base + pulse]"), bBounded);
    TestEqual(TEXT("Strength 5 crest is capped at base + pulse x max"), SonarRingIntensity(0.25, 0.75 / rate, 5.0),
        SOLRadar::SonarBaseIntensity + SOLRadar::SonarPulseIntensity * SOLRadar::SonarMaxStrength,
        RADAR_SONAR_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarSonarOutwardTest, "SOLTest.RadarLayout.SonarCrestMovesOutward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Over a small time step the crest moves outward: just outside it the glow rises, just inside it falls
bool FSOLRadarSonarOutwardTest::RunTest(const FString& /*parameters*/)
{
    using SOLRadar::SonarRingIntensity;
    const double rate = SOLRadar::SonarRingsPerSecond;
    constexpr double crestFraction = 0.3;
    constexpr double probeOffset = 0.05;
    constexpr double crestStep = 0.02;                    // Crest travel over dt, in ring fractions
    const double t0 = (crestFraction + 0.5) / rate;     // Crest at crestFraction
    const double dt = crestStep / rate;

    const double crestValue = SOLRadar::SonarBaseIntensity + SOLRadar::SonarPulseIntensity;
    TestEqual(TEXT("Crest at crestFraction at t0"), SonarRingIntensity(crestFraction, t0, 1.0), crestValue,
        RADAR_SONAR_TOLERANCE);
    TestEqual(TEXT("Crest one step further out at t0 + dt"), SonarRingIntensity(crestFraction + crestStep, t0 + dt,
        1.0), crestValue, RADAR_SONAR_TOLERANCE);

    const double outside = crestFraction + probeOffset;
    TestTrue(TEXT("Just outside the crest: brighter after dt"), SonarRingIntensity(outside, t0 + dt, 1.0)
        > SonarRingIntensity(outside, t0, 1.0));
    const double inside = crestFraction - probeOffset;
    TestTrue(TEXT("Just inside the crest: dimmer after dt"), SonarRingIntensity(inside, t0 + dt, 1.0)
        < SonarRingIntensity(inside, t0, 1.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// IsCrowded: inside or on the radius of any drawn blip is crowded, outside all of them is not, no blips or a
// non-positive radius is never crowded
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRadarIsCrowdedTest, "SOLTest.RadarLayout.IsCrowded",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FSOLRadarIsCrowdedTest::RunTest(const FString& Parameters)
{
    const FVector2D drawn[] = { FVector2D(10.0, 10.0), FVector2D(100.0, 50.0) };
    const TConstArrayView<FVector2D> view(drawn, 2);
    TestFalse(TEXT("No drawn blips"), SOLRadar::IsCrowded(TConstArrayView<FVector2D>(), FVector2D(10.0, 10.0), 6.0));
    TestTrue(TEXT("Same point"), SOLRadar::IsCrowded(view, FVector2D(10.0, 10.0), 6.0));
    TestTrue(TEXT("Inside the second blip's radius"), SOLRadar::IsCrowded(view, FVector2D(103.0, 54.0), 6.0));
    TestTrue(TEXT("Exactly on the radius"), SOLRadar::IsCrowded(view, FVector2D(16.0, 10.0), 6.0));
    TestFalse(TEXT("Just outside every radius"), SOLRadar::IsCrowded(view, FVector2D(16.01, 10.0), 6.0));
    TestFalse(TEXT("Zero radius"), SOLRadar::IsCrowded(view, FVector2D(10.0, 10.0), 0.0));
    TestFalse(TEXT("Negative radius"), SOLRadar::IsCrowded(view, FVector2D(10.0, 10.0), -1.0));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
