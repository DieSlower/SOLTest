/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingPatch.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Simple Saturn-scale synthetic ring (meters from the planet's center)
    constexpr double RINGPATCH_INNER_M = 70000.0e3;
    constexpr double RINGPATCH_OUTER_M = 140000.0e3;

    // Saturn-like GM (m^3/s^2), typed independently for the rock-orbit cases
    constexpr double RINGPATCH_PLANET_GM = 3.7931187e16;

    // Activation margin and generous gameplay half-thickness used by the ComputeActiveCell cases
    constexpr double RINGPATCH_MARGIN_M = 5000.0;
    constexpr double RINGPATCH_HALF_THICKNESS_M = 500.0;

    // Radial cell index used by the GenerateCellRocks cases (well inside the ring, margin 0)
    constexpr int32 RINGPATCH_GEN_RADIAL_INDEX = 1000;

    // Fixed seeds for the determinism/randomness cases
    constexpr int32 RINGPATCH_SEED_A = 12345;
    constexpr int32 RINGPATCH_SEED_B = 67890;

    // Slack for positions reconstructed through StateToElements/ElementsToState at ~1e8 m
    constexpr double RINGPATCH_POSITION_SLACK_M = 1.0e-2;

    // A realistic non-zero sim instant (~0.32 Julian centuries past J2000, near this project's current sim time), and
    // the J2000 epoch itself; the generation/placement cases run at both
    constexpr double RINGPATCH_NOW_SECONDS = 1.0e9;
    constexpr double RINGPATCH_GEN_TIMES_SECONDS[] = { 0.0, RINGPATCH_NOW_SECONDS };

    // Seconds in one Julian century, typed independently of SOLConstants.h
    constexpr double RINGPATCH_SECONDS_PER_JULIAN_CENTURY = 36525.0 * 86400.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns the synthetic ring definition shared by every case
    FSOLPlanetRingDef RingPatchMakeRing()
    {
        FSOLPlanetRingDef ring;
        ring.PlanetName = FName(TEXT("TestPlanet"));
        ring.InnerRadiusM = RINGPATCH_INNER_M;
        ring.OuterRadiusM = RINGPATCH_OUTER_M;
        return ring;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the 45-degree-tilted unit ring normal used by the tilted cases (rotated from +Z toward +Y about +X)
    FVector3d RingPatchTiltedNormal()
    {
        return FVector3d(0.0, 1.0, 1.0).GetSafeNormal();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a unit in-plane direction for the tilted normal that has a non-zero world-Z component
    FVector3d RingPatchTiltedInPlaneWithZ()
    {
        return FVector3d(0.0, -1.0, 1.0).GetSafeNormal();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the radius at the radial center of a radial cell index (indices count from ringInnerRadiusM itself)
    double RingPatchRadialCellCenterM(const double ringInnerRadiusM, const int32 radialIndex)
    {
        return ringInnerRadiusM + (static_cast<double>(radialIndex) + 0.5) * SOLRingPatch::CELL_SIZE_M;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the co-rotating frame's rotation (rad, unwrapped) at a radial band's center radius after secondsSinceJ2000
    double RingPatchFramePhaseRad(const double ringInnerRadiusM, const int32 radialIndex, const double secondsSinceJ2000)
    {
        const double referenceRadiusM = RingPatchRadialCellCenterM(ringInnerRadiusM, radialIndex);
        return FMath::Sqrt(RINGPATCH_PLANET_GM / (referenceRadiusM * referenceRadiusM * referenceRadiusM))
            * secondsSinceJ2000;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a rock's planet-relative position secondsSinceJ2000 after J2000, via the already-correct Kepler oracle
    // (its Elements are J2000-epoch values, so they must be evaluated at that instant to see where it actually is)
    FVector3d RingPatchRockPositionM(const FSOLRingRockDef& rock, const double planetGM, const double secondsSinceJ2000)
    {
        const double centuries = secondsSinceJ2000 / RINGPATCH_SECONDS_PER_JULIAN_CENTURY;
        return SOLKepler::ElementsToState(rock.Elements.AtCenturies(centuries), planetGM, 0.0).PositionM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a planet-relative position's radius projected into the ring plane (normal component removed)
    double RingPatchInPlaneRadiusM(const FVector3d& relativePositionM, const FVector3d& ringNormal)
    {
        return (relativePositionM - ringNormal * FVector3d::DotProduct(relativePositionM, ringNormal)).Size();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every field of two rocks' elements and radius is bit-identical
    bool RingPatchRocksIdentical(const FSOLRingRockDef& a, const FSOLRingRockDef& b)
    {
        const FSOLSecularElements& ea = a.Elements;
        const FSOLSecularElements& eb = b.Elements;
        return ea.A0AU == eb.A0AU && ea.ADotAUPerCy == eb.ADotAUPerCy && ea.E0 == eb.E0 && ea.EDotPerCy == eb.EDotPerCy
            && ea.I0Deg == eb.I0Deg && ea.IDotDegPerCy == eb.IDotDegPerCy && ea.L0Deg == eb.L0Deg
            && ea.LDotDegPerCy == eb.LDotDegPerCy && ea.LongPeri0Deg == eb.LongPeri0Deg
            && ea.LongPeriDotDegPerCy == eb.LongPeriDotDegPerCy && ea.LongNode0Deg == eb.LongNode0Deg
            && ea.LongNodeDotDegPerCy == eb.LongNodeDotDegPerCy && a.RadiusM == b.RadiusM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the generation cell at RINGPATCH_GEN_RADIAL_INDEX (margin 0) along inPlaneDir at secondsSinceJ2000, via
    // ComputeActiveCell
    TOptional<FSOLRingCell> RingPatchGenerationCell(const FSOLPlanetRingDef& ring, const FVector3d& ringNormal,
        const FVector3d& inPlaneDir, const double secondsSinceJ2000)
    {
        const double radiusM = RingPatchRadialCellCenterM(ring.InnerRadiusM, RINGPATCH_GEN_RADIAL_INDEX);
        return SOLRingPatch::ComputeActiveCell(inPlaneDir * radiusM, FVector3d::ZeroVector, ringNormal, ring, 0.0,
            RINGPATCH_HALF_THICKNESS_M, RINGPATCH_PLANET_GM, secondsSinceJ2000);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns ComputeActiveCell's result for the test planet's GM at RINGPATCH_NOW_SECONDS (the presence/margin/
    // indexing/tilt cases, whose admission and radial-index checks do not depend on the instant)
    TOptional<FSOLRingCell> RingPatchActiveCellNow(const FVector3d& worldPositionM, const FVector3d& planetPositionM,
        const FVector3d& ringNormal, const FSOLPlanetRingDef& ring, const double marginM, const double halfThicknessM)
    {
        return SOLRingPatch::ComputeActiveCell(worldPositionM, planetPositionM, ringNormal, ring, marginM,
            halfThicknessM, RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchConstantsTest, "SOLTest.RingPatch.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CELL_SIZE_M and ROCKS_PER_CELL hold their documented values
bool FSOLRingPatchConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("CELL_SIZE_M is 1000 m"), SOLRingPatch::CELL_SIZE_M, 1000.0);
    TestEqual(TEXT("ROCKS_PER_CELL is 100"), SOLRingPatch::ROCKS_PER_CELL, 100);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchActiveCellPresenceTest, "SOLTest.RingPatch.ActiveCellPresence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// In-band in-plane positions are SET; positions outside the radial band or beyond halfThicknessM are UNSET
bool FSOLRingPatchActiveCellPresenceTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FVector3d normal(0.0, 0.0, 1.0);
    const FVector3d planetM(1.0e9, -2.0e9, 3.0e8);
    const FVector3d inPlane(1.0, 0.0, 0.0);
    const double midRadiusM = 0.5 * (RINGPATCH_INNER_M + RINGPATCH_OUTER_M);

    // In the ring plane, mid-band
    TestTrue(TEXT("Mid-band in-plane position is SET"), RingPatchActiveCellNow(planetM + inPlane * midRadiusM,
        planetM, normal, ring, RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());

    // Far outside the radial band (2x outer), and well inside the inner edge
    TestFalse(TEXT("2x outer radius is UNSET"), RingPatchActiveCellNow(
        planetM + inPlane * (2.0 * RINGPATCH_OUTER_M), planetM, normal, ring, RINGPATCH_MARGIN_M,
        RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestFalse(TEXT("Half the inner radius is UNSET"), RingPatchActiveCellNow(
        planetM + inPlane * (0.5 * RINGPATCH_INNER_M), planetM, normal, ring, RINGPATCH_MARGIN_M,
        RINGPATCH_HALF_THICKNESS_M).IsSet());

    // Offset along the normal beyond / within halfThicknessM (both sides of the plane)
    TestFalse(TEXT("Above the plane beyond halfThickness is UNSET"), RingPatchActiveCellNow(
        planetM + inPlane * midRadiusM + normal * (2.0 * RINGPATCH_HALF_THICKNESS_M), planetM, normal, ring,
        RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestFalse(TEXT("Below the plane beyond halfThickness is UNSET"), RingPatchActiveCellNow(
        planetM + inPlane * midRadiusM - normal * (2.0 * RINGPATCH_HALF_THICKNESS_M), planetM, normal, ring,
        RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestTrue(TEXT("Above the plane within halfThickness is SET"), RingPatchActiveCellNow(
        planetM + inPlane * midRadiusM + normal * (0.5 * RINGPATCH_HALF_THICKNESS_M), planetM, normal, ring,
        RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestTrue(TEXT("Below the plane within halfThickness is SET"), RingPatchActiveCellNow(
        planetM + inPlane * midRadiusM - normal * (0.5 * RINGPATCH_HALF_THICKNESS_M), planetM, normal, ring,
        RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());

    // NaN anywhere in the inputs is rejected, never admitted
    const double nan = std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN position is UNSET"), RingPatchActiveCellNow(FVector3d(nan, 0.0, 0.0), planetM, normal, ring,
        RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestFalse(TEXT("NaN halfThickness is UNSET"), RingPatchActiveCellNow(planetM + inPlane * midRadiusM, planetM,
        normal, ring, RINGPATCH_MARGIN_M, nan).IsSet());
    TestFalse(TEXT("NaN margin is UNSET"), RingPatchActiveCellNow(planetM + inPlane * midRadiusM, planetM, normal,
        ring, nan, RINGPATCH_HALF_THICKNESS_M).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchActiveCellMarginTest, "SOLTest.RingPatch.ActiveCellMarginBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The activation band is closed at InnerRadiusM - marginM and OuterRadiusM + marginM; just past either is UNSET
bool FSOLRingPatchActiveCellMarginTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FVector3d normal(0.0, 0.0, 1.0);
    const FVector3d inPlane(1.0, 0.0, 0.0);
    const double innerEdgeM = RINGPATCH_INNER_M - RINGPATCH_MARGIN_M;
    const double outerEdgeM = RINGPATCH_OUTER_M + RINGPATCH_MARGIN_M;

    const TOptional<FSOLRingCell> atInner = RingPatchActiveCellNow(inPlane * innerEdgeM,
        FVector3d::ZeroVector, normal, ring, RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M);
    if (TestTrue(TEXT("Exactly at inner - margin is SET"), atInner.IsSet()))
    {
        // RadialIndex is anchored at InnerRadiusM itself, independent of marginM (so GenerateCellRocks, which has no
        // marginM of its own, can still reconstruct a cell's absolute span from RadialIndex alone) - a point exactly
        // at the margin boundary (InnerRadiusM - marginM) is -marginM/CELL_SIZE_M cells inward of index 0, not 0
        const int32 expectedRadialIndexAtMargin = -static_cast<int32>(RINGPATCH_MARGIN_M / SOLRingPatch::CELL_SIZE_M);
        TestEqual(TEXT("Inner activation edge is marginM/CELL_SIZE_M cells inward of index 0"),
            atInner.GetValue().RadialIndex, expectedRadialIndexAtMargin);
    }
    TestFalse(TEXT("1 m inside inner - margin is UNSET"), RingPatchActiveCellNow(
        inPlane * (innerEdgeM - 1.0), FVector3d::ZeroVector, normal, ring, RINGPATCH_MARGIN_M,
        RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestTrue(TEXT("Inside the margin but below InnerRadiusM is SET"), RingPatchActiveCellNow(
        inPlane * (RINGPATCH_INNER_M - 0.5 * RINGPATCH_MARGIN_M), FVector3d::ZeroVector, normal, ring,
        RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());

    TestTrue(TEXT("Exactly at outer + margin is SET"), RingPatchActiveCellNow(inPlane * outerEdgeM,
        FVector3d::ZeroVector, normal, ring, RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestFalse(TEXT("1 m beyond outer + margin is UNSET"), RingPatchActiveCellNow(
        inPlane * (outerEdgeM + 1.0), FVector3d::ZeroVector, normal, ring, RINGPATCH_MARGIN_M,
        RINGPATCH_HALF_THICKNESS_M).IsSet());

    // Zero margin: InnerRadiusM itself is the edge
    TestTrue(TEXT("Zero margin: InnerRadiusM is SET"), RingPatchActiveCellNow(inPlane * RINGPATCH_INNER_M,
        FVector3d::ZeroVector, normal, ring, 0.0, RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestFalse(TEXT("Zero margin: 1 m inside InnerRadiusM is UNSET"), RingPatchActiveCellNow(
        inPlane * (RINGPATCH_INNER_M - 1.0), FVector3d::ZeroVector, normal, ring, 0.0,
        RINGPATCH_HALF_THICKNESS_M).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchActiveCellIndexingTest, "SOLTest.RingPatch.ActiveCellIndexing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Cells are deterministic, RadialIndex counts CELL_SIZE_M steps from InnerRadiusM itself (independent of marginM),
// nearby points share a cell and points more than a cell apart radially or tangentially land in different cells
bool FSOLRingPatchActiveCellIndexingTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FVector3d normal(0.0, 0.0, 1.0);
    const FVector3d planetM(-4.0e9, 1.0e9, 0.0);
    const FVector3d inPlane(1.0, 0.0, 0.0);
    const FVector3d tangent(0.0, 1.0, 0.0);
    const int32 radialIndex = 20000;
    const double radiusM = RingPatchRadialCellCenterM(RINGPATCH_INNER_M, radialIndex);
    const FVector3d baseM = planetM + inPlane * radiusM;

    // Lambda: the cell for a world position with this case's fixed margin/thickness, at one fixed instant
    auto cellAt = [&](const FVector3d& positionM)
    {
        return RingPatchActiveCellNow(positionM, planetM, normal, ring, RINGPATCH_MARGIN_M,
            RINGPATCH_HALF_THICKNESS_M);
    };

    const TOptional<FSOLRingCell> base = cellAt(baseM);
    if (!TestTrue(TEXT("Base position is SET"), base.IsSet()))
    {
        return false;
    }

    // Determinism
    const TOptional<FSOLRingCell> baseAgain = cellAt(baseM);
    TestTrue(TEXT("Same inputs give the same cell"), baseAgain.IsSet() && baseAgain.GetValue() == base.GetValue());

    // RadialIndex counts CELL_SIZE_M steps outward from InnerRadiusM itself (this case's non-zero margin is ignored)
    TestEqual(TEXT("Radial index at a radial cell center"), base.GetValue().RadialIndex, radialIndex);

    // Small in-cell radial and out-of-plane offsets (base is at the radial center) keep the same cell
    const TOptional<FSOLRingCell> radialNudge = cellAt(baseM + inPlane * (0.25 * SOLRingPatch::CELL_SIZE_M));
    TestTrue(TEXT("Quarter-cell radial nudge keeps the radial index"),
        radialNudge.IsSet() && radialNudge.GetValue().RadialIndex == radialIndex);
    const TOptional<FSOLRingCell> verticalNudge = cellAt(baseM + normal * (0.5 * RINGPATCH_HALF_THICKNESS_M));
    TestTrue(TEXT("Offset along the normal keeps the same cell"),
        verticalNudge.IsSet() && verticalNudge.GetValue() == base.GetValue());

    // A 10 m tangential nudge either way: at most one cell boundary can lie in that 20 m span, so the base shares a
    // cell with at least one side, and both keep the radial index
    const double nudgeM = 10.0;
    const TOptional<FSOLRingCell> plus = cellAt(baseM + tangent * nudgeM);
    const TOptional<FSOLRingCell> minus = cellAt(baseM - tangent * nudgeM);
    if (TestTrue(TEXT("Tangential nudges are SET"), plus.IsSet() && minus.IsSet()))
    {
        TestTrue(TEXT("Base shares a cell with a nearby tangential neighbor"),
            plus.GetValue() == base.GetValue() || minus.GetValue() == base.GetValue());
        TestEqual(TEXT("+nudge keeps the radial index"), plus.GetValue().RadialIndex, radialIndex);
        TestEqual(TEXT("-nudge keeps the radial index"), minus.GetValue().RadialIndex, radialIndex);
    }

    // A full cell-width displacement from the radial CENTER of cell radialIndex lands exactly on the center of the
    // next/previous cell - unambiguous, unlike a 1.5-cell displacement, which would land exactly ON a cell boundary
    // (center is already +0.5 cells from the low edge, so +1.5 more lands on an edge, not inside the next cell)
    const TOptional<FSOLRingCell> outward = cellAt(baseM + inPlane * SOLRingPatch::CELL_SIZE_M);
    const TOptional<FSOLRingCell> inward = cellAt(baseM - inPlane * SOLRingPatch::CELL_SIZE_M);
    if (TestTrue(TEXT("Radially displaced positions are SET"), outward.IsSet() && inward.IsSet()))
    {
        TestNotEqual(TEXT("1 cell outward changes the radial index"), outward.GetValue().RadialIndex, radialIndex);
        TestNotEqual(TEXT("1 cell inward changes the radial index"), inward.GetValue().RadialIndex, radialIndex);
        TestEqual(TEXT("1 cell outward is the next radial index"), outward.GetValue().RadialIndex, radialIndex + 1);
        TestEqual(TEXT("1 cell inward is the previous radial index"), inward.GetValue().RadialIndex, radialIndex - 1);
    }

    // Several cells apart along the circle (same radius) gives a different angular index, same radial index
    const double arcM = 3.0 * SOLRingPatch::CELL_SIZE_M;
    const double angleRad = arcM / radiusM;
    const FVector3d alongArcM = planetM + (inPlane * FMath::Cos(angleRad) + tangent * FMath::Sin(angleRad)) * radiusM;
    const TOptional<FSOLRingCell> alongArc = cellAt(alongArcM);
    if (TestTrue(TEXT("Along-arc position is SET"), alongArc.IsSet()))
    {
        TestEqual(TEXT("Along-arc keeps the radial index"), alongArc.GetValue().RadialIndex, radialIndex);
        TestNotEqual(TEXT("3 cells along the arc changes the angular index"), alongArc.GetValue().AngularIndex,
            base.GetValue().AngularIndex);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchActiveCellTiltedTest, "SOLTest.RingPatch.ActiveCellTiltedNormal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With a 45-degree-tilted normal, displacement along that normal (not world Z) decides plane distance
bool FSOLRingPatchActiveCellTiltedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FVector3d normal = RingPatchTiltedNormal();
    const FVector3d planetM(2.0e9, 5.0e8, -7.0e8);
    const FVector3d inPlaneX(1.0, 0.0, 0.0);
    const FVector3d inPlaneWithZ = RingPatchTiltedInPlaneWithZ();
    const double midRadiusM = 0.5 * (RINGPATCH_INNER_M + RINGPATCH_OUTER_M);

    // Sanity on the fixture itself: both directions really lie in the tilted plane
    TestEqual(TEXT("Fixture: X is in the tilted plane"), FVector3d::DotProduct(inPlaneX, normal), 0.0, 1e-12);
    TestEqual(TEXT("Fixture: tilted in-plane dir is in the plane"), FVector3d::DotProduct(inPlaneWithZ, normal), 0.0,
        1e-12);

    // In the tilted plane: SET along both in-plane directions
    TestTrue(TEXT("Tilted: in-plane along X is SET"), RingPatchActiveCellNow(planetM + inPlaneX * midRadiusM,
        planetM, normal, ring, RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestTrue(TEXT("Tilted: in-plane with a large world-Z component is SET"), RingPatchActiveCellNow(
        planetM + inPlaneWithZ * midRadiusM, planetM, normal, ring, RINGPATCH_MARGIN_M,
        RINGPATCH_HALF_THICKNESS_M).IsSet());

    // Displaced purely along the tilted normal beyond halfThicknessM: UNSET
    TestFalse(TEXT("Tilted: 2x halfThickness along the tilted normal is UNSET"), RingPatchActiveCellNow(
        planetM + inPlaneX * midRadiusM + normal * (2.0 * RINGPATCH_HALF_THICKNESS_M), planetM, normal, ring,
        RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());
    TestTrue(TEXT("Tilted: 0.5x halfThickness along the tilted normal is SET"), RingPatchActiveCellNow(
        planetM + inPlaneX * midRadiusM + normal * (0.5 * RINGPATCH_HALF_THICKNESS_M), planetM, normal, ring,
        RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M).IsSet());

    // A displacement with a world-Z offset well beyond halfThicknessM that stays in the tilted plane is still SET,
    // proving the classification is not hardcoded to world Z
    const FVector3d inPlaneOffsetM = inPlaneWithZ * (4.0 * RINGPATCH_HALF_THICKNESS_M);
    TestTrue(TEXT("Fixture: offset's world-Z exceeds halfThickness"), inPlaneOffsetM.Z > RINGPATCH_HALF_THICKNESS_M);
    TestTrue(TEXT("Tilted: world-Z offset inside the tilted plane is SET"), RingPatchActiveCellNow(
        planetM + inPlaneX * midRadiusM + inPlaneOffsetM, planetM, normal, ring, RINGPATCH_MARGIN_M,
        RINGPATCH_HALF_THICKNESS_M).IsSet());

    // Radial band is measured in the tilted plane too: 2x outer along the in-plane-with-Z direction is UNSET
    TestFalse(TEXT("Tilted: 2x outer radius is UNSET"), RingPatchActiveCellNow(
        planetM + inPlaneWithZ * (2.0 * RINGPATCH_OUTER_M), planetM, normal, ring, RINGPATCH_MARGIN_M,
        RINGPATCH_HALF_THICKNESS_M).IsSet());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchActiveCellCoRotatingTest, "SOLTest.RingPatch.ActiveCellCoRotating",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AngularIndex is measured in a frame co-rotating at the band's circular mean motion: a point carried around at that
// rate keeps its cell at any instant, while a point fixed in the planet's frame changes cell within seconds
bool FSOLRingPatchActiveCellCoRotatingTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FVector3d normal(0.0, 0.0, 1.0);
    const FVector3d planetM(3.0e9, -1.0e9, 2.0e8);
    const int32 radialIndex = 20000;
    const int32 angularIndex = 1234;
    const double radiusM = RingPatchRadialCellCenterM(RINGPATCH_INNER_M, radialIndex);
    const double angularCount = FMath::FloorToDouble(UE_DOUBLE_TWO_PI * radiusM / SOLRingPatch::CELL_SIZE_M);
    const double coRotatingAngleRad = (angularIndex + 0.5) * UE_DOUBLE_TWO_PI / angularCount;

    // Lambda: the in-plane world position at radiusM and the given world angle
    auto pointAt = [&](const double worldAngleRad)
    {
        return planetM + FVector3d(FMath::Cos(worldAngleRad), FMath::Sin(worldAngleRad), 0.0) * radiusM;
    };
    // Lambda: the cell for a world position at the given instant
    auto cellAt = [&](const FVector3d& positionM, const double secondsSinceJ2000)
    {
        return SOLRingPatch::ComputeActiveCell(positionM, planetM, normal, ring, RINGPATCH_MARGIN_M,
            RINGPATCH_HALF_THICKNESS_M, RINGPATCH_PLANET_GM, secondsSinceJ2000);
    };

    // At J2000 the co-rotating and world frames coincide
    const TOptional<FSOLRingCell> atEpoch = cellAt(pointAt(coRotatingAngleRad), 0.0);
    if (TestTrue(TEXT("J2000: position is SET"), atEpoch.IsSet()))
    {
        TestEqual(TEXT("J2000: radial index"), atEpoch.GetValue().RadialIndex, radialIndex);
        TestEqual(TEXT("J2000: angular index is the world-angle cell"), atEpoch.GetValue().AngularIndex, angularIndex);
    }

    // A point carried forward at the band's mean motion keeps its cell, at a nearby instant and at a much later one
    const double laterSeconds[] = { 10.0, RINGPATCH_NOW_SECONDS, RINGPATCH_NOW_SECONDS + 10.0 };
    for (const double seconds : laterSeconds)
    {
        const double phaseRad = RingPatchFramePhaseRad(RINGPATCH_INNER_M, radialIndex, seconds);
        const TOptional<FSOLRingCell> carried = cellAt(pointAt(coRotatingAngleRad + phaseRad), seconds);
        if (TestTrue(*FString::Printf(TEXT("t=%.1f s: carried position is SET"), seconds), carried.IsSet()))
        {
            TestEqual(*FString::Printf(TEXT("t=%.1f s: carried radial index"), seconds),
                carried.GetValue().RadialIndex, radialIndex);
            TestEqual(*FString::Printf(TEXT("t=%.1f s: carried point keeps its angular index"), seconds),
                carried.GetValue().AngularIndex, angularIndex);
        }
    }

    // A point fixed in the planet's frame changes cell 10 s later (the band sweeps ~200 km past it)
    const FVector3d fixedM = pointAt(coRotatingAngleRad + RingPatchFramePhaseRad(RINGPATCH_INNER_M, radialIndex,
        RINGPATCH_NOW_SECONDS));
    const TOptional<FSOLRingCell> fixedNow = cellAt(fixedM, RINGPATCH_NOW_SECONDS);
    const TOptional<FSOLRingCell> fixedLater = cellAt(fixedM, RINGPATCH_NOW_SECONDS + 10.0);
    if (TestTrue(TEXT("Fixed point is SET at both instants"), fixedNow.IsSet() && fixedLater.IsSet()))
    {
        TestEqual(TEXT("Fixed point keeps its radial index"), fixedLater.GetValue().RadialIndex, radialIndex);
        TestNotEqual(TEXT("Fixed point changes angular index 10 s later"), fixedLater.GetValue().AngularIndex,
            fixedNow.GetValue().AngularIndex);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchGenerateDeterminismTest, "SOLTest.RingPatch.GenerateDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Same inputs give identical rocks; a different seed gives a different set
bool FSOLRingPatchGenerateDeterminismTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FVector3d normal(0.0, 0.0, 1.0);
    const TOptional<FSOLRingCell> cell = RingPatchGenerationCell(ring, normal, FVector3d(1.0, 0.0, 0.0),
        RINGPATCH_NOW_SECONDS);
    if (!TestTrue(TEXT("Generation cell is SET"), cell.IsSet()))
    {
        return false;
    }

    // Lambda: this cell's rocks for a seed, at RINGPATCH_NOW_SECONDS
    auto generate = [&](const int32 seed)
    {
        return SOLRingPatch::GenerateCellRocks(ring, cell.GetValue(), normal, RINGPATCH_PLANET_GM,
            RINGPATCH_HALF_THICKNESS_M, TConstArrayView<FSOLRadiusBandM>(), seed, RINGPATCH_NOW_SECONDS);
    };

    const TArray<FSOLRingRockDef> first = generate(RINGPATCH_SEED_A);
    const TArray<FSOLRingRockDef> second = generate(RINGPATCH_SEED_A);
    if (!TestTrue(TEXT("Generation produced rocks"), first.Num() > 0))
    {
        return false;
    }
    if (TestEqual(TEXT("Same inputs, same count"), second.Num(), first.Num()))
    {
        bool bAllIdentical = true;
        for (int32 index = 0; index < first.Num(); ++index)
        {
            bAllIdentical &= RingPatchRocksIdentical(first[index], second[index]);
        }
        TestTrue(TEXT("Same inputs, identical rocks in the same order"), bAllIdentical);
    }

    // A different seed changes at least one rock's position
    const TArray<FSOLRingRockDef> other = generate(RINGPATCH_SEED_B);
    bool bAnyDiffers = other.Num() != first.Num();
    for (int32 index = 0; !bAnyDiffers && index < first.Num(); ++index)
    {
        bAnyDiffers = !RingPatchRockPositionM(first[index], RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS).Equals(
            RingPatchRockPositionM(other[index], RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS), 1.0e-3);
    }
    TestTrue(TEXT("Different seed gives a different rock set"), bAnyDiffers);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchGenerateGapCountTest, "SOLTest.RingPatch.GenerateGapCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Rock count is round(ROCKS_PER_CELL * (1 - gapFraction)) over the cell's radial span, and no rock lands in a gap
bool FSOLRingPatchGenerateGapCountTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FVector3d normal(0.0, 0.0, 1.0);
    const TOptional<FSOLRingCell> cell = RingPatchGenerationCell(ring, normal, FVector3d(1.0, 0.0, 0.0),
        RINGPATCH_NOW_SECONDS);
    if (!TestTrue(TEXT("Generation cell is SET"), cell.IsSet()))
    {
        return false;
    }

    // The cell's radial span (RadialIndex counts from InnerRadiusM itself)
    const double cellSize = SOLRingPatch::CELL_SIZE_M;
    const double spanLowM = RINGPATCH_INNER_M + RINGPATCH_GEN_RADIAL_INDEX * cellSize;
    const double spanHighM = spanLowM + cellSize;

    // Lambda: generate this cell's rocks against a gap band list, at RINGPATCH_NOW_SECONDS
    auto generate = [&](TConstArrayView<FSOLRadiusBandM> gaps)
    {
        return SOLRingPatch::GenerateCellRocks(ring, cell.GetValue(), normal, RINGPATCH_PLANET_GM,
            RINGPATCH_HALF_THICKNESS_M, gaps, RINGPATCH_SEED_A, RINGPATCH_NOW_SECONDS);
    };

    // No gaps, and gaps that miss the cell entirely: full count
    TestEqual(TEXT("No gap bands: ROCKS_PER_CELL"), generate(TConstArrayView<FSOLRadiusBandM>()).Num(),
        SOLRingPatch::ROCKS_PER_CELL);
    const FSOLRadiusBandM missing[] = { { spanLowM - 50.0 * cellSize, spanLowM - 10.0 * cellSize },
        { spanHighM, spanHighM + 20.0 * cellSize } };
    TestEqual(TEXT("Non-overlapping gap bands: ROCKS_PER_CELL"), generate(missing).Num(), SOLRingPatch::ROCKS_PER_CELL);

    // A gap band covering the whole radial span: empty
    const FSOLRadiusBandM covering[] = { { spanLowM - cellSize, spanHighM + cellSize } };
    TestEqual(TEXT("Fully covering gap: no rocks"), generate(covering).Num(), 0);

    // Half the span (inner half): gapFraction 0.5 -> round(100 * 0.5) = 50
    const FSOLRadiusBandM innerHalf[] = { { spanLowM - cellSize, spanLowM + 0.5 * cellSize } };
    const int32 expectedHalf = FMath::RoundToInt(SOLRingPatch::ROCKS_PER_CELL * (1.0 - 0.5));
    const TArray<FSOLRingRockDef> halfRocks = generate(innerHalf);
    TestEqual(TEXT("Half-covering gap: round(ROCKS_PER_CELL * 0.5)"), halfRocks.Num(), expectedHalf);

    // A quarter of the span (outer quarter): gapFraction 0.25 -> round(100 * 0.75) = 75
    const FSOLRadiusBandM outerQuarter[] = { { spanLowM + 0.75 * cellSize, spanHighM + cellSize } };
    const int32 expectedQuarter = FMath::RoundToInt(SOLRingPatch::ROCKS_PER_CELL * (1.0 - 0.25));
    const TArray<FSOLRingRockDef> quarterRocks = generate(outerQuarter);
    TestEqual(TEXT("Quarter-covering gap: round(ROCKS_PER_CELL * 0.75)"), quarterRocks.Num(), expectedQuarter);

    // Two overlapping bands covering the same 30% of the span count once: round(100 * 0.7) = 70
    const FSOLRadiusBandM overlapping[] = { { spanLowM + 0.1 * cellSize, spanLowM + 0.3 * cellSize },
        { spanLowM + 0.2 * cellSize, spanLowM + 0.4 * cellSize } };
    const int32 expectedOverlap = FMath::RoundToInt(SOLRingPatch::ROCKS_PER_CELL * (1.0 - 0.3));
    TestEqual(TEXT("Overlapping gap bands count once"), generate(overlapping).Num(), expectedOverlap);

    // No rock lands inside a gap band (shrunk by a small slack for orbit-reconstruction error), evaluated at the
    // instant the rocks were generated for
    // Lambda: true when any rock's in-plane radius lies strictly inside the given band (minus slack)
    auto anyRockInBand = [&](const TArray<FSOLRingRockDef>& rocks, const FSOLRadiusBandM& band)
    {
        return rocks.ContainsByPredicate([&](const FSOLRingRockDef& rock)
        {
            const double radiusM = RingPatchInPlaneRadiusM(RingPatchRockPositionM(rock, RINGPATCH_PLANET_GM,
                RINGPATCH_NOW_SECONDS), normal);
            return radiusM > band.LowM + RINGPATCH_POSITION_SLACK_M && radiusM < band.HighM - RINGPATCH_POSITION_SLACK_M;
        });
    };
    TestFalse(TEXT("No rock inside the half-covering gap"), anyRockInBand(halfRocks, innerHalf[0]));
    TestFalse(TEXT("No rock inside the quarter-covering gap"), anyRockInBand(quarterRocks, outerQuarter[0]));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchGeneratePlacementTest, "SOLTest.RingPatch.GeneratePlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every rock, evaluated at the instant it was generated for, sits inside its cell's radial span and thickness, maps
// back to the same cell, is near-circular with a positive radius, and advances at its orbit's own mean motion;
// checked for untilted and tilted ring normals, at J2000 and at a realistic later instant
bool FSOLRingPatchGeneratePlacementTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const double cellSize = SOLRingPatch::CELL_SIZE_M;
    const double spanLowM = RINGPATCH_INNER_M + RINGPATCH_GEN_RADIAL_INDEX * cellSize;
    const double spanHighM = spanLowM + cellSize;

    struct FRingPatchPlacementCase
    {
        const TCHAR* Label;
        FVector3d Normal;
        FVector3d InPlaneDir;
    };
    const FRingPatchPlacementCase cases[] =
    {
        { TEXT("Untilted"), FVector3d(0.0, 0.0, 1.0), FVector3d(1.0, 0.0, 0.0) },
        { TEXT("Tilted"), RingPatchTiltedNormal(), RingPatchTiltedInPlaneWithZ() },
    };

    for (const FRingPatchPlacementCase& placement : cases)
    {
        for (const double seconds : RINGPATCH_GEN_TIMES_SECONDS)
        {
            const FString label = FString::Printf(TEXT("%s t=%.0f s"), placement.Label, seconds);
            const TOptional<FSOLRingCell> cell = RingPatchGenerationCell(ring, placement.Normal, placement.InPlaneDir,
                seconds);
            if (!TestTrue(*FString::Printf(TEXT("%s: generation cell is SET"), *label), cell.IsSet()))
            {
                continue;
            }
            const TArray<FSOLRingRockDef> rocks = SOLRingPatch::GenerateCellRocks(ring, cell.GetValue(),
                placement.Normal, RINGPATCH_PLANET_GM, RINGPATCH_HALF_THICKNESS_M, TConstArrayView<FSOLRadiusBandM>(),
                RINGPATCH_SEED_A, seconds);
            TestEqual(*FString::Printf(TEXT("%s: full rock count"), *label), rocks.Num(), SOLRingPatch::ROCKS_PER_CELL);

            // Per-rock checks, each reported once per case (first failure) to keep the log readable
            for (int32 index = 0; index < rocks.Num(); ++index)
            {
                const FSOLRingRockDef& rock = rocks[index];
                const FVector3d positionM = RingPatchRockPositionM(rock, RINGPATCH_PLANET_GM, seconds);
                const double inPlaneRadiusM = RingPatchInPlaneRadiusM(positionM, placement.Normal);
                const double planeDistanceM = FMath::Abs(FVector3d::DotProduct(positionM, placement.Normal));
                const TOptional<FSOLRingCell> rockCell = SOLRingPatch::ComputeActiveCell(positionM,
                    FVector3d::ZeroVector, placement.Normal, ring, 0.0,
                    RINGPATCH_HALF_THICKNESS_M + RINGPATCH_POSITION_SLACK_M, RINGPATCH_PLANET_GM, seconds);
                const double semiMajorAxisM = rock.Elements.A0AU * SOLTestHelpers::AU_M;
                const double expectedMeanMotion = SOLKepler::MeanMotionDegPerCy(semiMajorAxisM, RINGPATCH_PLANET_GM);

                const bool bInSpan = inPlaneRadiusM >= spanLowM - RINGPATCH_POSITION_SLACK_M
                    && inPlaneRadiusM <= spanHighM + RINGPATCH_POSITION_SLACK_M;
                const bool bInThickness = planeDistanceM <= RINGPATCH_HALF_THICKNESS_M + RINGPATCH_POSITION_SLACK_M;
                const bool bSameCell = rockCell.IsSet() && rockCell.GetValue() == cell.GetValue();
                const bool bNearCircular = rock.Elements.E0 >= 0.0 && rock.Elements.E0 < 0.05;
                const bool bPositiveRadius = rock.RadiusM > 0.0;
                const bool bMeanMotion = rock.Elements.LDotDegPerCy > 0.0
                    && FMath::Abs(rock.Elements.LDotDegPerCy - expectedMeanMotion) <= 1.0e-3 * expectedMeanMotion;

                if (!bInSpan || !bInThickness || !bSameCell || !bNearCircular || !bPositiveRadius || !bMeanMotion)
                {
                    TestTrue(*FString::Printf(TEXT("%s rock %d: in-plane radius %.3f within [%.3f, %.3f]"), *label,
                        index, inPlaneRadiusM, spanLowM, spanHighM), bInSpan);
                    TestTrue(*FString::Printf(TEXT("%s rock %d: plane distance %.3f within halfThickness"), *label,
                        index, planeDistanceM), bInThickness);
                    TestTrue(*FString::Printf(TEXT("%s rock %d: maps back to its own cell"), *label, index),
                        bSameCell);
                    TestTrue(*FString::Printf(TEXT("%s rock %d: eccentricity %.6f near 0"), *label, index,
                        rock.Elements.E0), bNearCircular);
                    TestTrue(*FString::Printf(TEXT("%s rock %d: radius %.3f positive"), *label, index,
                        rock.RadiusM), bPositiveRadius);
                    TestTrue(*FString::Printf(TEXT("%s rock %d: mean motion %.6f matches %.6f"), *label, index,
                        rock.Elements.LDotDegPerCy, expectedMeanMotion), bMeanMotion);
                    break;
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchGenerateEpochRollbackTest, "SOLTest.RingPatch.GenerateEpochRollback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Rocks generated at a non-zero instant carry J2000-epoch elements: L0Deg + LDotDegPerCy * centuries (the mean
// longitude AtCenturies reproduces at the generation instant) lands back inside the cell's co-rotating angular span,
// and the rock's position there agrees with that longitude; L0Deg alone (the un-rolled-back reading) does not
bool FSOLRingPatchGenerateEpochRollbackTest::RunTest(const FString& /*parameters*/)
{
    // Untilted ring: its in-plane angle is measured from +X, the same axis ecliptic longitudes are measured from
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FVector3d normal(0.0, 0.0, 1.0);
    const double seconds = RINGPATCH_NOW_SECONDS;
    const double centuries = seconds / RINGPATCH_SECONDS_PER_JULIAN_CENTURY;
    const TOptional<FSOLRingCell> cell = RingPatchGenerationCell(ring, normal, FVector3d(1.0, 0.0, 0.0), seconds);
    if (!TestTrue(TEXT("Generation cell is SET"), cell.IsSet()))
    {
        return false;
    }
    const TArray<FSOLRingRockDef> rocks = SOLRingPatch::GenerateCellRocks(ring, cell.GetValue(), normal,
        RINGPATCH_PLANET_GM, RINGPATCH_HALF_THICKNESS_M, TConstArrayView<FSOLRadiusBandM>(), RINGPATCH_SEED_A, seconds);
    if (!TestTrue(TEXT("Generation produced rocks"), rocks.Num() > 0))
    {
        return false;
    }

    // The cell's co-rotating angular span, rebuilt independently from the band's center radius
    const double referenceRadiusM = RingPatchRadialCellCenterM(RINGPATCH_INNER_M, cell.GetValue().RadialIndex);
    const double angularCount = FMath::FloorToDouble(UE_DOUBLE_TWO_PI * referenceRadiusM / SOLRingPatch::CELL_SIZE_M);
    const double cellAngleRad = UE_DOUBLE_TWO_PI / angularCount;
    const double spanCenterRad = (cell.GetValue().AngularIndex + 0.5) * cellAngleRad;
    const double phaseRad = RingPatchFramePhaseRad(RINGPATCH_INNER_M, cell.GetValue().RadialIndex, seconds);

    // Lambda: how far (rad) a world angle's co-rotating equivalent lies from the cell's angular center, mod 2PI
    auto offsetFromSpanCenterRad = [&](const double worldAngleRad)
    {
        return FMath::Abs(SOLTestHelpers::AngleDiffRad(worldAngleRad - phaseRad - spanCenterRad));
    };

    // Angular slack: a few mm of orbit-reconstruction error at ~7e7 m
    const double angleSlackRad = RINGPATCH_POSITION_SLACK_M / referenceRadiusM;
    bool bAnyUnrolledOutside = false;
    for (int32 index = 0; index < rocks.Num(); ++index)
    {
        const FSOLSecularElements& elements = rocks[index].Elements;
        const double rolledLongitudeRad = FMath::DegreesToRadians(SOLKepler::WrapDegrees(elements.L0Deg
            + SOLKepler::WrapDegrees(elements.LDotDegPerCy * centuries)));
        const FVector3d positionM = RingPatchRockPositionM(rocks[index], RINGPATCH_PLANET_GM, seconds);
        const double positionAngleRad = FMath::Atan2(positionM.Y, positionM.X);

        const bool bLongitudeInCell = offsetFromSpanCenterRad(rolledLongitudeRad) <= 0.5 * cellAngleRad + angleSlackRad;
        const bool bPositionMatchesLongitude = FMath::Abs(SOLTestHelpers::AngleDiffRad(positionAngleRad
            - rolledLongitudeRad)) <= angleSlackRad;
        bAnyUnrolledOutside |= offsetFromSpanCenterRad(FMath::DegreesToRadians(elements.L0Deg)) > cellAngleRad;
        if (!bLongitudeInCell || !bPositionMatchesLongitude)
        {
            TestTrue(*FString::Printf(TEXT("Rock %d: mean longitude at the generation instant is in the cell"),
                index), bLongitudeInCell);
            TestTrue(*FString::Printf(TEXT("Rock %d: position angle %.12f matches mean longitude %.12f"), index,
                positionAngleRad, rolledLongitudeRad), bPositionMatchesLongitude);
            break;
        }
    }
    TestTrue(TEXT("L0Deg alone (not advanced to the generation instant) is outside the cell"), bAnyUnrolledOutside);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchNearFieldAlphaTest, "SOLTest.RingPatch.NearFieldAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Alpha is 1 inside, 0 beyond the band, linear between; a non-positive band is a hard cut
bool FSOLRingPatchNearFieldAlphaTest::RunTest(const FString& /*parameters*/)
{
    const double bandM = 2000.0;

    // Inside or at the ring volume
    TestEqual(TEXT("At the volume edge is exactly 1"), SOLRingPatch::ComputeNearFieldAlpha(0.0, bandM), 1.0);
    TestEqual(TEXT("Inside the volume is exactly 1"), SOLRingPatch::ComputeNearFieldAlpha(-500.0, bandM), 1.0);

    // At or beyond the transition band
    TestEqual(TEXT("At the band's far edge is exactly 0"), SOLRingPatch::ComputeNearFieldAlpha(bandM, bandM), 0.0);
    TestEqual(TEXT("Beyond the band is exactly 0"), SOLRingPatch::ComputeNearFieldAlpha(3.0 * bandM, bandM), 0.0);

    // Linear in between
    TestEqual(TEXT("Midpoint is 0.5"), SOLRingPatch::ComputeNearFieldAlpha(0.5 * bandM, bandM), 0.5, 1e-9);
    TestEqual(TEXT("Quarter-way is 0.75"), SOLRingPatch::ComputeNearFieldAlpha(0.25 * bandM, bandM), 0.75, 1e-9);
    TestEqual(TEXT("Three-quarter-way is 0.25"), SOLRingPatch::ComputeNearFieldAlpha(0.75 * bandM, bandM), 0.25, 1e-9);

    // Hard cut for a zero or negative band
    const double hardBands[] = { 0.0, -100.0 };
    for (const double hardBandM : hardBands)
    {
        TestEqual(*FString::Printf(TEXT("Band %.1f: at the volume edge is 1"), hardBandM),
            SOLRingPatch::ComputeNearFieldAlpha(0.0, hardBandM), 1.0);
        TestEqual(*FString::Printf(TEXT("Band %.1f: inside is 1"), hardBandM),
            SOLRingPatch::ComputeNearFieldAlpha(-10.0, hardBandM), 1.0);
        TestEqual(*FString::Printf(TEXT("Band %.1f: just outside is 0"), hardBandM),
            SOLRingPatch::ComputeNearFieldAlpha(1.0e-3, hardBandM), 0.0);
        TestEqual(*FString::Printf(TEXT("Band %.1f: far outside is 0"), hardBandM),
            SOLRingPatch::ComputeNearFieldAlpha(1.0e6, hardBandM), 0.0);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
