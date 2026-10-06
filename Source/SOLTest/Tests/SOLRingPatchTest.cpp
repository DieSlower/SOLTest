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

// Helpers for the GetAngularCellCount/ActiveCellWindow cases, unique-named for unity builds
namespace
{
    // Radial index deep inside the Saturn-scale fixture (far from both edges) used by the window cases
    constexpr int32 RINGPATCH_WINDOW_RADIAL_INDEX = 20000;

    // Angular index used by the window-size cases (far from the band's wrap boundary)
    constexpr int32 RINGPATCH_WINDOW_ANGULAR_INDEX = 1234;

    // Tiny synthetic ring starting at the planet's center, so its innermost bands have only a handful of angular
    // cells (band 0's center circle is 0.5 * CELL_SIZE_M in radius, ~3 cells around) - used by the dedup case only
    constexpr double RINGPATCH_TINY_INNER_M = 0.0;
    constexpr double RINGPATCH_TINY_OUTER_M = 10000.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns the tiny synthetic ring whose innermost bands have very few angular cells
    FSOLPlanetRingDef RingPatchMakeTinyRing()
    {
        FSOLPlanetRingDef ring;
        ring.PlanetName = FName(TEXT("TinyTestPlanet"));
        ring.InnerRadiusM = RINGPATCH_TINY_INNER_M;
        ring.OuterRadiusM = RINGPATCH_TINY_OUTER_M;
        return ring;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a cell array as a set (duplicates collapse, so Num() differs from the array's when any exist)
    TSet<FSOLRingCell> RingPatchCellSet(const TArray<FSOLRingCell>& cells)
    {
        TSet<FSOLRingCell> result;
        result.Reserve(cells.Num());
        for (const FSOLRingCell& cell : cells)
        {
            result.Add(cell);
        }
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two cell sets hold exactly the same cells
    bool RingPatchSetsEqual(const TSet<FSOLRingCell>& a, const TSet<FSOLRingCell>& b)
    {
        return a.Num() == b.Num() && a.Includes(b);
    }

    // Sim instants the position-based window cases run at: RINGPATCH_NOW_SECONDS plus two more, so neighboring bands'
    // co-rotating frames have drifted apart by several radians (thousands of cells) at every one of them
    constexpr double RINGPATCH_WINDOW_TIMES_SECONDS[] = { RINGPATCH_NOW_SECONDS, 8.44e8, 3.0e9 };

    // Angular nudge (in cell arc-lengths at the nudged position's own band) guaranteed to stay inside an
    // angularRadius-1 window: the player can sit up to half a cell from its cell's center, and the window covers
    // angularRadius cells either side of that center, so any nudge below angularRadius - 0.5 must be covered
    constexpr double RINGPATCH_WINDOW_NUDGE_SLACK_CELLS = 0.05;

    //////////////////////////////////////////////////////////////////////////
    // Returns the planet position used by the position-based window cases (away from the origin, ring normal +Z)
    FVector3d RingPatchWindowPlanetM()
    {
        return FVector3d(1.0e9, -2.0e9, 3.0e8);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the world position at a planet-relative in-plane radius and angle (about +Z) for the window cases
    FVector3d RingPatchWindowPositionM(const double radiusM, const double angleRad)
    {
        return RingPatchWindowPlanetM() + FVector3d(radiusM * FMath::Cos(angleRad), radiusM * FMath::Sin(angleRad), 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns ComputeActiveCell's cell for the in-plane point at (radiusM, angleRad) at secondsSinceJ2000 - the
    // never-buggy co-rotating-frame oracle the window cases verify ActiveCellWindow against
    TOptional<FSOLRingCell> RingPatchWindowCellAt(const FSOLPlanetRingDef& ring, const double radiusM,
        const double angleRad, const double secondsSinceJ2000)
    {
        return SOLRingPatch::ComputeActiveCell(RingPatchWindowPositionM(radiusM, angleRad), RingPatchWindowPlanetM(),
            FVector3d(0.0, 0.0, 1.0), ring, RINGPATCH_MARGIN_M, RINGPATCH_HALF_THICKNESS_M, RINGPATCH_PLANET_GM,
            secondsSinceJ2000);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an in-plane angle (about +Z) at radialIndex's center radius whose ComputeActiveCell AngularIndex is
    // targetIndex at secondsSinceJ2000: starts from an arbitrary angle and steps whole cell widths (exact in the
    // band's co-rotating frame, which is a pure rotation of world angle)
    double RingPatchAngleForAngularIndex(const FSOLPlanetRingDef& ring, const int32 radialIndex,
        const int32 targetIndex, const double secondsSinceJ2000)
    {
        const double startAngleRad = 0.5;
        const double radiusM = RingPatchRadialCellCenterM(ring.InnerRadiusM, radialIndex);
        const TOptional<FSOLRingCell> start = RingPatchWindowCellAt(ring, radiusM, startAngleRad, secondsSinceJ2000);
        const int32 startIndex = start.IsSet() ? start.GetValue().AngularIndex : 0;
        const double cellAngleRad = UE_DOUBLE_TWO_PI / SOLRingPatch::GetAngularCellCount(ring, radialIndex);
        return startAngleRad + static_cast<double>(targetIndex - startIndex) * cellAngleRad;
    }

    //////////////////////////////////////////////////////////////////////////
    // Places a player at (centerRadialIndex's center radius, playerAngleRad), takes its real cell from
    // ComputeActiveCell, then checks that every nearby real position - each band within radialRadius, nudged
    // angularly by 0 and +/- (angularRadius - 0.5 - slack) cell arc-lengths - maps (again via ComputeActiveCell) to a
    // cell contained in ActiveCellWindow(playerCell, radialRadius, angularRadius). Reports the first miss only.
    // Returns the player's cell (unset if the fixture position was not admitted)
    TOptional<FSOLRingCell> RingPatchCheckRealNeighborsInWindow(FAutomationTestBase& test, const FSOLPlanetRingDef& ring,
        const int32 centerRadialIndex, const double playerAngleRad, const double secondsSinceJ2000,
        const int32 radialRadius, const int32 angularRadius, const TCHAR* label)
    {
        const double playerRadiusM = RingPatchRadialCellCenterM(ring.InnerRadiusM, centerRadialIndex);
        const TOptional<FSOLRingCell> playerCell = RingPatchWindowCellAt(ring, playerRadiusM, playerAngleRad,
            secondsSinceJ2000);
        if (!test.TestTrue(*FString::Printf(TEXT("%s: fixture player position is inside the ring"), label),
            playerCell.IsSet() && playerCell.GetValue().RadialIndex == centerRadialIndex))
        {
            return playerCell;
        }

        const TArray<FSOLRingCell> window = SOLRingPatch::ActiveCellWindow(ring, playerCell.GetValue(), radialRadius,
            angularRadius, RINGPATCH_PLANET_GM, secondsSinceJ2000);
        const double nudgeCells = static_cast<double>(angularRadius) - 0.5 - RINGPATCH_WINDOW_NUDGE_SLACK_CELLS;

        // Every (band, nudge) neighbor: its real cell must be in the window
        for (int32 radialOffset = -radialRadius; radialOffset <= radialRadius; ++radialOffset)
        {
            const int32 bandIndex = centerRadialIndex + radialOffset;
            const double bandRadiusM = RingPatchRadialCellCenterM(ring.InnerRadiusM, bandIndex);
            const double bandCellAngleRad = UE_DOUBLE_TWO_PI / SOLRingPatch::GetAngularCellCount(ring, bandIndex);
            const double nudges[] = { -nudgeCells, 0.0, nudgeCells };
            for (const double nudge : nudges)
            {
                const TOptional<FSOLRingCell> neighbor = RingPatchWindowCellAt(ring, bandRadiusM,
                    playerAngleRad + nudge * bandCellAngleRad, secondsSinceJ2000);
                if (!neighbor.IsSet() || neighbor.GetValue().RadialIndex != bandIndex
                    || !window.Contains(neighbor.GetValue()))
                {
                    test.AddError(FString::Printf(TEXT("%s: player cell {%d,%d}; neighbor at band offset %d, nudge %.2f "
                        "cells maps to {%d,%d}, which is not in the %dx%d window"), label,
                        playerCell.GetValue().RadialIndex, playerCell.GetValue().AngularIndex, radialOffset, nudge,
                        neighbor.IsSet() ? neighbor.GetValue().RadialIndex : -1,
                        neighbor.IsSet() ? neighbor.GetValue().AngularIndex : -1, radialRadius * 2 + 1,
                        angularRadius * 2 + 1));
                    return playerCell;
                }
            }
        }
        return playerCell;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the circular distance between two angular indices in a band of count cells
    int32 RingPatchCircularIndexDistance(const int32 a, const int32 b, const int32 count)
    {
        const int32 forward = ((a - b) % count + count) % count;
        return FMath::Min(forward, count - forward);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the window's cells that lie in one radial band, as a set
    TSet<FSOLRingCell> RingPatchBandCells(const TArray<FSOLRingCell>& window, const int32 radialIndex)
    {
        TSet<FSOLRingCell> result;
        for (const FSOLRingCell& cell : window)
        {
            if (cell.RadialIndex == radialIndex)
            {
                result.Add(cell);
            }
        }
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a cell built from its two indices
    FSOLRingCell RingPatchCell(const int32 radialIndex, const int32 angularIndex)
    {
        FSOLRingCell cell;
        cell.RadialIndex = radialIndex;
        cell.AngularIndex = angularIndex;
        return cell;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchAngularCountMonotonicTest, "SOLTest.RingPatch.GetAngularCellCountMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Angular cell count is at least 1 and never decreases moving outward across the ring's radial bands
bool FSOLRingPatchAngularCountMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const int32 bandCount = static_cast<int32>((RINGPATCH_OUTER_M - RINGPATCH_INNER_M) / SOLRingPatch::CELL_SIZE_M);

    // Walk every band of the Saturn-scale fixture, reporting only the first failure
    int32 previousCount = SOLRingPatch::GetAngularCellCount(ring, 0);
    TestTrue(TEXT("Band 0 has at least 1 angular cell"), previousCount >= 1);
    for (int32 radialIndex = 1; radialIndex <= bandCount; ++radialIndex)
    {
        const int32 count = SOLRingPatch::GetAngularCellCount(ring, radialIndex);
        if (count < 1 || count < previousCount)
        {
            TestTrue(*FString::Printf(TEXT("Band %d: count %d is at least 1"), radialIndex, count), count >= 1);
            TestTrue(*FString::Printf(TEXT("Band %d: count %d >= previous band's %d"), radialIndex, count,
                previousCount), count >= previousCount);
            break;
        }
        previousCount = count;
    }

    // Meaningfully growing over the ring's width (outer circumference is twice the inner one here)
    TestTrue(TEXT("Outermost band has more cells than the innermost"),
        SOLRingPatch::GetAngularCellCount(ring, bandCount) > SOLRingPatch::GetAngularCellCount(ring, 0));
    TestTrue(TEXT("Saturn-scale band has tens of thousands of cells"),
        SOLRingPatch::GetAngularCellCount(ring, RINGPATCH_WINDOW_RADIAL_INDEX) > 10000);

    // The tiny ring's innermost bands still have at least 1 cell and stay non-decreasing
    const FSOLPlanetRingDef tinyRing = RingPatchMakeTinyRing();
    int32 previousTiny = SOLRingPatch::GetAngularCellCount(tinyRing, 0);
    TestTrue(TEXT("Tiny ring band 0 has at least 1 angular cell"), previousTiny >= 1);
    for (int32 radialIndex = 1; radialIndex < 10; ++radialIndex)
    {
        const int32 count = SOLRingPatch::GetAngularCellCount(tinyRing, radialIndex);
        TestTrue(*FString::Printf(TEXT("Tiny ring band %d: count %d >= max(1, previous %d)"), radialIndex, count,
            previousTiny), count >= 1 && count >= previousTiny);
        previousTiny = count;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchWindowSizeTest, "SOLTest.RingPatch.ActiveCellWindowSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A deep-interior window has (2R+1)*(2A+1) distinct cells and always includes the center cell
bool FSOLRingPatchWindowSizeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FSOLRingCell center = RingPatchCell(RINGPATCH_WINDOW_RADIAL_INDEX, RINGPATCH_WINDOW_ANGULAR_INDEX);

    // 3x3
    const TArray<FSOLRingCell> square = SOLRingPatch::ActiveCellWindow(ring, center, 1, 1,
        RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
    TestEqual(TEXT("3x3: 9 entries"), square.Num(), 9);
    TestEqual(TEXT("3x3: 9 distinct cells"), RingPatchCellSet(square).Num(), 9);
    TestTrue(TEXT("3x3: contains the center cell"), square.Contains(center));

    // Radial line: 5 cells, one per band r-2..r+2
    const TArray<FSOLRingCell> radialLine = SOLRingPatch::ActiveCellWindow(ring, center, 2, 0,
        RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
    TestEqual(TEXT("Radial line: 5 entries"), radialLine.Num(), 5);
    TestEqual(TEXT("Radial line: 5 distinct cells"), RingPatchCellSet(radialLine).Num(), 5);
    TestTrue(TEXT("Radial line: contains the center cell"), radialLine.Contains(center));
    for (int32 radialOffset = -2; radialOffset <= 2; ++radialOffset)
    {
        TestEqual(*FString::Printf(TEXT("Radial line: exactly one cell at radial offset %d"), radialOffset),
            RingPatchBandCells(radialLine, center.RadialIndex + radialOffset).Num(), 1);
    }

    // Angular line: 5 cells, all in the center's own band, at A-2..A+2
    const TArray<FSOLRingCell> angularLine = SOLRingPatch::ActiveCellWindow(ring, center, 0, 2,
        RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
    TestEqual(TEXT("Angular line: 5 entries"), angularLine.Num(), 5);
    TSet<FSOLRingCell> expectedAngular;
    for (int32 angularOffset = -2; angularOffset <= 2; ++angularOffset)
    {
        expectedAngular.Add(RingPatchCell(center.RadialIndex, center.AngularIndex + angularOffset));
    }
    TestTrue(TEXT("Angular line: exactly A-2..A+2 in the center's band"),
        RingPatchSetsEqual(RingPatchCellSet(angularLine), expectedAngular));

    // Degenerate 1x1 window is just the center
    const TArray<FSOLRingCell> single = SOLRingPatch::ActiveCellWindow(ring, center, 0, 0,
        RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
    TestTrue(TEXT("1x1: only the center cell"), single.Num() == 1 && single[0] == center);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchWindowRadialIndicesTest, "SOLTest.RingPatch.ActiveCellWindowRadialIndices",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 3x3 window spans exactly radial bands r-1, r, r+1, three cells each
bool FSOLRingPatchWindowRadialIndicesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const FSOLRingCell center = RingPatchCell(RINGPATCH_WINDOW_RADIAL_INDEX, RINGPATCH_WINDOW_ANGULAR_INDEX);
    const TArray<FSOLRingCell> window = SOLRingPatch::ActiveCellWindow(ring, center, 1, 1,
        RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);

    TSet<int32> radialIndices;
    for (const FSOLRingCell& cell : window)
    {
        radialIndices.Add(cell.RadialIndex);
    }
    TSet<int32> expectedRadial;
    expectedRadial.Add(center.RadialIndex - 1);
    expectedRadial.Add(center.RadialIndex);
    expectedRadial.Add(center.RadialIndex + 1);
    TestTrue(TEXT("Radial indices are exactly {r-1, r, r+1}"),
        radialIndices.Num() == expectedRadial.Num() && radialIndices.Includes(expectedRadial));

    for (int32 radialOffset = -1; radialOffset <= 1; ++radialOffset)
    {
        TestEqual(*FString::Printf(TEXT("Three cells at radial offset %d"), radialOffset),
            RingPatchBandCells(window, center.RadialIndex + radialOffset).Num(), 3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchWindowCrossBandTest, "SOLTest.RingPatch.ActiveCellWindowCrossBandAlignment",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// For players at many angles (including both sides of the world-angle +/-PI seam) and several sim instants, every
// real position one band in or out and up to ~half a cell along the orbit maps (via ComputeActiveCell) to a cell the
// 3x3 window contains; likewise for a 5x5 window out to two bands and ~1.5 cells. Verified against ComputeActiveCell
// itself rather than a parallel re-derivation of the alignment formula
bool FSOLRingPatchWindowCrossBandTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const int32 radialIndex = RINGPATCH_WINDOW_RADIAL_INDEX;

    // Fixture sanity: neighbor bands really have different counts
    const int32 centerCount = SOLRingPatch::GetAngularCellCount(ring, radialIndex);
    TestTrue(TEXT("Fixture: outer neighbor band has more cells"),
        SOLRingPatch::GetAngularCellCount(ring, radialIndex + 1) > centerCount);
    TestTrue(TEXT("Fixture: inner neighbor band has fewer cells"),
        SOLRingPatch::GetAngularCellCount(ring, radialIndex - 1) < centerCount);

    // Seven spread-out angles plus both sides of the Atan2 seam at +/-PI
    TArray<double> playerAngles;
    for (int32 step = 0; step < 7; ++step)
    {
        playerAngles.Add(0.123 + step * UE_DOUBLE_TWO_PI / 7.0);
    }
    playerAngles.Add(UE_DOUBLE_PI - 1.0e-7);
    playerAngles.Add(-UE_DOUBLE_PI + 1.0e-7);

    for (const double seconds : RINGPATCH_WINDOW_TIMES_SECONDS)
    {
        for (const double angleRad : playerAngles)
        {
            RingPatchCheckRealNeighborsInWindow(*this, ring, radialIndex, angleRad, seconds, 1, 1,
                *FString::Printf(TEXT("3x3 t=%.3g angle=%.6f"), seconds, angleRad));
            RingPatchCheckRealNeighborsInWindow(*this, ring, radialIndex, angleRad, seconds, 2, 2,
                *FString::Printf(TEXT("5x5 t=%.3g angle=%.6f"), seconds, angleRad));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchWindowRealNeighborTest, "SOLTest.RingPatch.ActiveCellWindowContainsRealNeighbor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Regression test for SDD 6 Amendment 9: neighboring bands' co-rotating frames have different phases, so their
// AngularIndex 0 point at different world angles. The real cell a position one band out maps to must be in the
// window, and the fixture confirms it is far (many cells) from where a phase-blind "fraction of its own band's circle"
// re-derivation would have put that band's window - so the old formula cannot pass this case
bool FSOLRingPatchWindowRealNeighborTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const int32 radialIndex = RINGPATCH_WINDOW_RADIAL_INDEX;
    const double angleRad = 1.0;

    for (const double seconds : RINGPATCH_WINDOW_TIMES_SECONDS)
    {
        const FString label = FString::Printf(TEXT("t=%.3g"), seconds);
        const TOptional<FSOLRingCell> playerCell = RingPatchCheckRealNeighborsInWindow(*this, ring, radialIndex,
            angleRad, seconds, 1, 1, *label);
        if (!playerCell.IsSet())
        {
            continue;
        }

        // Both neighboring bands: the real neighbor cell is in the window, and far from the phase-blind index
        const TArray<FSOLRingCell> window = SOLRingPatch::ActiveCellWindow(ring, playerCell.GetValue(), 1, 1,
            RINGPATCH_PLANET_GM, seconds);
        const int32 centerCount = SOLRingPatch::GetAngularCellCount(ring, radialIndex);
        const int32 bandOffsets[] = { -1, 1 };
        for (const int32 bandOffset : bandOffsets)
        {
            const int32 bandIndex = radialIndex + bandOffset;
            const int32 bandCount = SOLRingPatch::GetAngularCellCount(ring, bandIndex);
            const TOptional<FSOLRingCell> neighbor = RingPatchWindowCellAt(ring,
                RingPatchRadialCellCenterM(ring.InnerRadiusM, bandIndex), angleRad, seconds);
            if (!TestTrue(*FString::Printf(TEXT("%s band %+d: fixture neighbor is admitted in that band"), *label,
                bandOffset), neighbor.IsSet() && neighbor.GetValue().RadialIndex == bandIndex))
            {
                continue;
            }
            TestTrue(*FString::Printf(TEXT("%s band %+d: window contains the real neighbor cell {%d,%d}"), *label,
                bandOffset, neighbor.GetValue().RadialIndex, neighbor.GetValue().AngularIndex),
                window.Contains(neighbor.GetValue()));

            // The phase-blind index the pre-Amendment-9 code used, and its +/-1 window, would miss the real cell
            const double fraction = (playerCell.GetValue().AngularIndex + 0.5) / static_cast<double>(centerCount);
            const int32 phaseBlindIndex = FMath::Clamp(FMath::FloorToInt32(fraction * bandCount), 0, bandCount - 1);
            TestTrue(*FString::Printf(TEXT("%s band %+d: fixture - real neighbor index %d is far from phase-blind "
                "index %d"), *label, bandOffset, neighbor.GetValue().AngularIndex, phaseBlindIndex),
                RingPatchCircularIndexDistance(neighbor.GetValue().AngularIndex, phaseBlindIndex, bandCount) > 10);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchWindowWrapTest, "SOLTest.RingPatch.ActiveCellWindowAngularWrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AngularIndex wraps within each band's own count at both ends of the circle, never going negative or out of range,
// and a player in a band's first/last co-rotating cell still has every real neighbor (including the one across the
// wrap) inside its window
bool FSOLRingPatchWindowWrapTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const int32 radialIndex = RINGPATCH_WINDOW_RADIAL_INDEX;
    const int32 count = SOLRingPatch::GetAngularCellCount(ring, radialIndex);
    const double seconds = RINGPATCH_NOW_SECONDS;

    // Lambda: true when every cell's AngularIndex is within [0, its own band's count)
    auto allInRange = [&](const TArray<FSOLRingCell>& window)
    {
        return !window.ContainsByPredicate([&](const FSOLRingCell& cell)
        {
            return cell.AngularIndex < 0 || cell.AngularIndex >= SOLRingPatch::GetAngularCellCount(ring,
                cell.RadialIndex);
        });
    };

    // Low end (index-only): index 0 wraps back to count - 1 in its own band
    const FSOLRingCell low = RingPatchCell(radialIndex, 0);
    const TArray<FSOLRingCell> lowLine = SOLRingPatch::ActiveCellWindow(ring, low, 0, 1, RINGPATCH_PLANET_GM, seconds);
    TSet<FSOLRingCell> expectedLowLine;
    expectedLowLine.Add(RingPatchCell(radialIndex, count - 1));
    expectedLowLine.Add(RingPatchCell(radialIndex, 0));
    expectedLowLine.Add(RingPatchCell(radialIndex, 1));
    TestTrue(TEXT("Low end: {count-1, 0, 1}"), RingPatchSetsEqual(RingPatchCellSet(lowLine), expectedLowLine));
    TestTrue(TEXT("Low end: all indices in range"), allInRange(lowLine));

    // High end (index-only): count - 1 wraps forward to 0
    const FSOLRingCell high = RingPatchCell(radialIndex, count - 1);
    const TArray<FSOLRingCell> highLine = SOLRingPatch::ActiveCellWindow(ring, high, 0, 1, RINGPATCH_PLANET_GM,
        seconds);
    TSet<FSOLRingCell> expectedHighLine;
    expectedHighLine.Add(RingPatchCell(radialIndex, count - 2));
    expectedHighLine.Add(RingPatchCell(radialIndex, count - 1));
    expectedHighLine.Add(RingPatchCell(radialIndex, 0));
    TestTrue(TEXT("High end: {count-2, count-1, 0}"), RingPatchSetsEqual(RingPatchCellSet(highLine), expectedHighLine));
    TestTrue(TEXT("High end: all indices in range"), allInRange(highLine));

    // Position-based at both ends: a real player in cell 0 / count-1, its real neighbors (one full cell along the
    // orbit lands exactly in the wrapped cell of its own band) and the neighbor bands' real cells are all covered
    const double cellAngleRad = UE_DOUBLE_TWO_PI / count;
    const double radiusM = RingPatchRadialCellCenterM(ring.InnerRadiusM, radialIndex);
    struct FWrapCase
    {
        int32 PlayerIndex;
        double StepCells;
        int32 WrappedIndex;
    };
    const FWrapCase cases[] = { { 0, -1.0, count - 1 }, { count - 1, 1.0, 0 } };
    for (const FWrapCase& wrapCase : cases)
    {
        const double angleRad = RingPatchAngleForAngularIndex(ring, radialIndex, wrapCase.PlayerIndex, seconds);
        const FString label = FString::Printf(TEXT("Player in cell %d"), wrapCase.PlayerIndex);
        const TOptional<FSOLRingCell> playerCell = RingPatchCheckRealNeighborsInWindow(*this, ring, radialIndex,
            angleRad, seconds, 1, 1, *label);
        if (!TestTrue(*FString::Printf(TEXT("%s: fixture lands in that cell"), *label),
            playerCell.IsSet() && playerCell.GetValue() == RingPatchCell(radialIndex, wrapCase.PlayerIndex)))
        {
            continue;
        }

        // One whole cell across the wrap, in the player's own band
        const TOptional<FSOLRingCell> across = RingPatchWindowCellAt(ring, radiusM,
            angleRad + wrapCase.StepCells * cellAngleRad, seconds);
        if (TestTrue(*FString::Printf(TEXT("%s: fixture - the cell across the wrap is %d"), *label,
            wrapCase.WrappedIndex), across.IsSet() && across.GetValue() == RingPatchCell(radialIndex,
            wrapCase.WrappedIndex)))
        {
            const TArray<FSOLRingCell> window = SOLRingPatch::ActiveCellWindow(ring, playerCell.GetValue(), 1, 1,
                RINGPATCH_PLANET_GM, seconds);
            TestTrue(*FString::Printf(TEXT("%s: window contains the real cell across the wrap"), *label),
                window.Contains(across.GetValue()));
            TestTrue(*FString::Printf(TEXT("%s: all indices in range"), *label), allInRange(window));
            TestEqual(*FString::Printf(TEXT("%s: 9 entries"), *label), window.Num(), 9);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchWindowDedupTest, "SOLTest.RingPatch.ActiveCellWindowDeduplication",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A band with fewer angular cells than angularRadius*2+1 yields each of its cells exactly once
bool FSOLRingPatchWindowDedupTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeTinyRing();
    const int32 angularRadius = 5;
    const int32 naiveWidth = 2 * angularRadius + 1;

    // Find the first non-negative band with at most 3 angular cells
    int32 smallBand = INDEX_NONE;
    for (int32 radialIndex = 0; radialIndex < 10 && smallBand == INDEX_NONE; ++radialIndex)
    {
        if (SOLRingPatch::GetAngularCellCount(ring, radialIndex) <= 3)
        {
            smallBand = radialIndex;
        }
    }
    if (!TestTrue(TEXT("Fixture: tiny ring has a band with at most 3 angular cells"), smallBand != INDEX_NONE))
    {
        return false;
    }
    const int32 smallCount = SOLRingPatch::GetAngularCellCount(ring, smallBand);

    // Angular line in the small band: every one of its cells exactly once
    const TArray<FSOLRingCell> line = SOLRingPatch::ActiveCellWindow(ring, RingPatchCell(smallBand, 0), 0,
        angularRadius, RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
    TSet<FSOLRingCell> wholeSmallBand;
    for (int32 angularIndex = 0; angularIndex < smallCount; ++angularIndex)
    {
        wholeSmallBand.Add(RingPatchCell(smallBand, angularIndex));
    }
    TestEqual(TEXT("Small band line: entry count equals the band's cell count"), line.Num(), smallCount);
    TestEqual(TEXT("Small band line: no duplicates"), RingPatchCellSet(line).Num(), line.Num());
    TestTrue(TEXT("Small band line: covers every cell of the band"),
        RingPatchSetsEqual(RingPatchCellSet(line), wholeSmallBand));

    // Multi-band window centered just outside the small band: every band capped at min(naiveWidth, its count), all
    // indices in range (which cells of a wider band are picked depends on alignment, covered by the position cases)
    const FSOLRingCell center = RingPatchCell(smallBand + 1, 0);
    const TArray<FSOLRingCell> window = SOLRingPatch::ActiveCellWindow(ring, center, 1, angularRadius,
        RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
    TestEqual(TEXT("Multi-band: no duplicates"), RingPatchCellSet(window).Num(), window.Num());
    TestTrue(TEXT("Multi-band: contains the center cell"), window.Contains(center));
    int32 naiveTotal = 0;
    for (int32 radialOffset = -1; radialOffset <= 1; ++radialOffset)
    {
        const int32 band = center.RadialIndex + radialOffset;
        const int32 bandCount = SOLRingPatch::GetAngularCellCount(ring, band);
        const TSet<FSOLRingCell> bandCells = RingPatchBandCells(window, band);
        TestEqual(*FString::Printf(TEXT("Multi-band: band %d has min(%d, count) cells"), band, naiveWidth),
            bandCells.Num(), FMath::Min(naiveWidth, bandCount));
        for (const FSOLRingCell& cell : bandCells)
        {
            TestTrue(*FString::Printf(TEXT("Multi-band: band %d index %d in range"), band, cell.AngularIndex),
                cell.AngularIndex >= 0 && cell.AngularIndex < bandCount);
        }
        naiveTotal += naiveWidth;
    }
    TestTrue(TEXT("Fixture: the naive un-deduplicated window would be larger"), naiveTotal > window.Num());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchWindowDeterminismTest, "SOLTest.RingPatch.ActiveCellWindowDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Same inputs give the same set of cells
bool FSOLRingPatchWindowDeterminismTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPlanetRingDef ring = RingPatchMakeRing();
    const int32 count = SOLRingPatch::GetAngularCellCount(ring, RINGPATCH_WINDOW_RADIAL_INDEX);
    const FSOLRingCell centers[] = { RingPatchCell(RINGPATCH_WINDOW_RADIAL_INDEX, RINGPATCH_WINDOW_ANGULAR_INDEX),
        RingPatchCell(RINGPATCH_WINDOW_RADIAL_INDEX, 0), RingPatchCell(RINGPATCH_WINDOW_RADIAL_INDEX, count - 1) };

    for (const FSOLRingCell& center : centers)
    {
        const TArray<FSOLRingCell> first = SOLRingPatch::ActiveCellWindow(ring, center, 2, 3,
        RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
        const TArray<FSOLRingCell> second = SOLRingPatch::ActiveCellWindow(ring, center, 2, 3,
        RINGPATCH_PLANET_GM, RINGPATCH_NOW_SECONDS);
        TestEqual(*FString::Printf(TEXT("Angular %d: same entry count"), center.AngularIndex), second.Num(),
            first.Num());
        TestTrue(*FString::Printf(TEXT("Angular %d: same set of cells"), center.AngularIndex),
            RingPatchSetsEqual(RingPatchCellSet(first), RingPatchCellSet(second)));
        TestEqual(*FString::Printf(TEXT("Angular %d: 5x7 window has 35 entries"), center.AngularIndex), first.Num(),
            35);
    }
    return true;
}

// Helpers for the ReassignPoolSlots cases, unique-named for unity builds
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns true when a pool slot is set and represents exactly the given cell
    bool RingPatchSlotIs(const TOptional<FSOLRingCell>& slot, const FSOLRingCell& cell)
    {
        return slot.IsSet() && slot.GetValue() == cell;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many of a slot array's entries are set
    int32 RingPatchSetSlotCount(const TArray<TOptional<FSOLRingCell>>& slots)
    {
        int32 count = 0;
        for (const TOptional<FSOLRingCell>& slot : slots)
        {
            count += slot.IsSet() ? 1 : 0;
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the cells of a slot array's set entries, in slot order (unset slots skipped, duplicates kept)
    TArray<FSOLRingCell> RingPatchSetSlotCells(const TArray<TOptional<FSOLRingCell>>& slots)
    {
        TArray<FSOLRingCell> cells;
        cells.Reserve(slots.Num());
        for (const TOptional<FSOLRingCell>& slot : slots)
        {
            if (slot.IsSet())
            {
                cells.Add(slot.GetValue());
            }
        }
        return cells;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two slot arrays have the same length and the same (set, cell) state at every index
    bool RingPatchSlotsIdentical(const TArray<TOptional<FSOLRingCell>>& a, const TArray<TOptional<FSOLRingCell>>& b)
    {
        if (a.Num() != b.Num())
        {
            return false;
        }
        for (int32 index = 0; index < a.Num(); ++index)
        {
            if (a[index].IsSet() != b[index].IsSet()
                || (a[index].IsSet() && !(a[index].GetValue() == b[index].GetValue())))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns n unset pool slots
    TArray<TOptional<FSOLRingCell>> RingPatchUnsetSlots(const int32 n)
    {
        TArray<TOptional<FSOLRingCell>> slots;
        slots.Init(TOptional<FSOLRingCell>(), n);
        return slots;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignUnchangedTest,
    "SOLTest.RingPatch.ReassignPoolSlotsNoChangeWhenWindowUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// When every slot's cell is still desired, every slot keeps its exact cell at its exact index
bool FSOLRingPatchReassignUnchangedTest::RunTest(const FString& /*parameters*/)
{
    const TArray<TOptional<FSOLRingCell>> current = { RingPatchCell(0, 0), RingPatchCell(0, 1), RingPatchCell(1, 0) };

    // Same cells, deliberately in a different order from the slots
    const TArray<FSOLRingCell> desired = { RingPatchCell(1, 0), RingPatchCell(0, 0), RingPatchCell(0, 1) };

    const TArray<TOptional<FSOLRingCell>> result = SOLRingPatch::ReassignPoolSlots(current, desired);
    if (!TestEqual(TEXT("Slot count preserved"), result.Num(), current.Num()))
    {
        return false;
    }
    for (int32 index = 0; index < current.Num(); ++index)
    {
        TestTrue(*FString::Printf(TEXT("Slot %d keeps its cell at the same index"), index),
            RingPatchSlotIs(result[index], current[index].GetValue()));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignAllUnsetTest, "SOLTest.RingPatch.ReassignPoolSlotsAllSlotsUnsetInitially",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Unset slots are all assigned, together covering every desired cell exactly once
bool FSOLRingPatchReassignAllUnsetTest::RunTest(const FString& /*parameters*/)
{
    const TArray<TOptional<FSOLRingCell>> current = RingPatchUnsetSlots(3);
    const TArray<FSOLRingCell> desired = { RingPatchCell(0, 0), RingPatchCell(0, 1), RingPatchCell(1, 0) };

    const TArray<TOptional<FSOLRingCell>> result = SOLRingPatch::ReassignPoolSlots(current, desired);
    if (!TestEqual(TEXT("Slot count preserved"), result.Num(), current.Num()))
    {
        return false;
    }
    TestEqual(TEXT("Every slot becomes set"), RingPatchSetSlotCount(result), 3);

    // Exactly once each: no duplicates among the set slots, and the set matches the desired cells
    const TArray<FSOLRingCell> covered = RingPatchSetSlotCells(result);
    TestEqual(TEXT("No desired cell covered twice"), RingPatchCellSet(covered).Num(), covered.Num());
    TestTrue(TEXT("Slots cover exactly the desired cells"),
        RingPatchSetsEqual(RingPatchCellSet(covered), RingPatchCellSet(desired)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignStableTest,
    "SOLTest.RingPatch.ReassignPoolSlotsStableSlotsKeepTheirCellWhenStillDesired",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Slots whose cells stay desired are untouched; the one dropped slot moves to the one newly desired cell
bool FSOLRingPatchReassignStableTest::RunTest(const FString& /*parameters*/)
{
    const TArray<TOptional<FSOLRingCell>> current = { RingPatchCell(0, 0), RingPatchCell(0, 1), RingPatchCell(0, 2) };
    const TArray<FSOLRingCell> desired = { RingPatchCell(0, 1), RingPatchCell(0, 2), RingPatchCell(0, 3) };

    const TArray<TOptional<FSOLRingCell>> result = SOLRingPatch::ReassignPoolSlots(current, desired);
    if (!TestEqual(TEXT("Slot count preserved"), result.Num(), current.Num()))
    {
        return false;
    }
    TestTrue(TEXT("Slot 1 keeps {0,1}"), RingPatchSlotIs(result[1], RingPatchCell(0, 1)));
    TestTrue(TEXT("Slot 2 keeps {0,2}"), RingPatchSlotIs(result[2], RingPatchCell(0, 2)));

    // The only candidate and the only uncovered desired cell: fully determined
    TestTrue(TEXT("Slot 0 (no longer desired) moves to {0,3}"), RingPatchSlotIs(result[0], RingPatchCell(0, 3)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignParkExtrasTest,
    "SOLTest.RingPatch.ReassignPoolSlotsMoreSlotsThanDesiredCellsParksExtras",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With more candidate slots than desired cells, the desired cells are each covered once and the rest are unset
bool FSOLRingPatchReassignParkExtrasTest::RunTest(const FString& /*parameters*/)
{
    // Two slots on stale (no longer desired) cells, three already unset: all five are candidates
    TArray<TOptional<FSOLRingCell>> current = RingPatchUnsetSlots(5);
    current[1] = RingPatchCell(5, 5);
    current[3] = RingPatchCell(6, 6);
    const TArray<FSOLRingCell> desired = { RingPatchCell(0, 0), RingPatchCell(0, 1) };

    const TArray<TOptional<FSOLRingCell>> result = SOLRingPatch::ReassignPoolSlots(current, desired);
    if (!TestEqual(TEXT("Slot count preserved"), result.Num(), current.Num()))
    {
        return false;
    }
    TestEqual(TEXT("Exactly 2 slots set"), RingPatchSetSlotCount(result), 2);
    TestEqual(TEXT("Exactly 3 slots unset"), result.Num() - RingPatchSetSlotCount(result), 3);

    const TArray<FSOLRingCell> covered = RingPatchSetSlotCells(result);
    TestEqual(TEXT("No desired cell covered twice"), RingPatchCellSet(covered).Num(), covered.Num());
    TestTrue(TEXT("Set slots cover exactly the desired cells"),
        RingPatchSetsEqual(RingPatchCellSet(covered), RingPatchCellSet(desired)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignUncoveredTest,
    "SOLTest.RingPatch.ReassignPoolSlotsFewerSlotsThanDesiredCellsLeavesSomeCellsUncovered",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With fewer slots than desired cells, every slot is used on a distinct desired cell and the rest stay uncovered
bool FSOLRingPatchReassignUncoveredTest::RunTest(const FString& /*parameters*/)
{
    const TArray<TOptional<FSOLRingCell>> current = RingPatchUnsetSlots(2);
    const TArray<FSOLRingCell> desired = { RingPatchCell(0, 0), RingPatchCell(0, 1), RingPatchCell(1, 0),
        RingPatchCell(1, 1) };

    const TArray<TOptional<FSOLRingCell>> result = SOLRingPatch::ReassignPoolSlots(current, desired);
    if (!TestEqual(TEXT("Slot count preserved"), result.Num(), current.Num()))
    {
        return false;
    }
    TestEqual(TEXT("Both slots set"), RingPatchSetSlotCount(result), 2);

    // Which two cells are covered is unspecified; only that they are distinct desired cells
    const TArray<FSOLRingCell> covered = RingPatchSetSlotCells(result);
    TestEqual(TEXT("Covered cells are distinct"), RingPatchCellSet(covered).Num(), covered.Num());
    for (const FSOLRingCell& cell : covered)
    {
        TestTrue(*FString::Printf(TEXT("Covered cell {%d,%d} is desired"), cell.RadialIndex, cell.AngularIndex),
            desired.Contains(cell));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignEmptyWindowTest,
    "SOLTest.RingPatch.ReassignPoolSlotsEmptyDesiredWindowParksEverySlot",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An empty desired window parks every slot, set or not
bool FSOLRingPatchReassignEmptyWindowTest::RunTest(const FString& /*parameters*/)
{
    TArray<TOptional<FSOLRingCell>> current = RingPatchUnsetSlots(3);
    current[0] = RingPatchCell(2, 7);
    current[2] = RingPatchCell(-1, 3);

    const TArray<TOptional<FSOLRingCell>> result = SOLRingPatch::ReassignPoolSlots(current,
        TConstArrayView<FSOLRingCell>());
    if (!TestEqual(TEXT("Slot count preserved"), result.Num(), current.Num()))
    {
        return false;
    }
    for (int32 index = 0; index < result.Num(); ++index)
    {
        TestFalse(*FString::Printf(TEXT("Slot %d is unset"), index), result[index].IsSet());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignDuplicateSlotsTest,
    "SOLTest.RingPatch.ReassignPoolSlotsStableSlotsAreNotDoubleCountedAsCandidates",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Two slots already on the same desired cell: the first stays, the second is freed to cover the other desired cell.
// This is a degenerate input the header does not explicitly pin down; the expected outcome is the natural one
// (first matching slot is stable, a later duplicate becomes a reassignment candidate) and keeps the documented
// "every desired cell covered by exactly one slot" guarantee
bool FSOLRingPatchReassignDuplicateSlotsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<TOptional<FSOLRingCell>> current = { RingPatchCell(0, 0), RingPatchCell(0, 0) };
    const TArray<FSOLRingCell> desired = { RingPatchCell(0, 0), RingPatchCell(0, 1) };

    const TArray<TOptional<FSOLRingCell>> result = SOLRingPatch::ReassignPoolSlots(current, desired);
    if (!TestEqual(TEXT("Slot count preserved"), result.Num(), current.Num()))
    {
        return false;
    }
    TestTrue(TEXT("First slot stays on {0,0}"), RingPatchSlotIs(result[0], RingPatchCell(0, 0)));
    TestTrue(TEXT("Duplicate second slot moves to {0,1}"), RingPatchSlotIs(result[1], RingPatchCell(0, 1)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignDuplicateDesiredTest,
    "SOLTest.RingPatch.ReassignPoolSlotsDuplicateDesiredCell",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PRECONDITION VIOLATION, not a guaranteed contract: desiredWindow must be duplicate-free, and this feeds it {X, X}.
// It pins down what the implementation currently does with that malformed input (the stable slot keeps X, and the
// spare slot is also assigned X, so X ends up covered twice) so the behavior cannot silently change unnoticed
bool FSOLRingPatchReassignDuplicateDesiredTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRingCell cellX = RingPatchCell(4, 2);
    TArray<TOptional<FSOLRingCell>> current = RingPatchUnsetSlots(2);
    current[0] = cellX;
    const TArray<FSOLRingCell> desired = { cellX, cellX };

    const TArray<TOptional<FSOLRingCell>> result = SOLRingPatch::ReassignPoolSlots(current, desired);
    if (!TestEqual(TEXT("Slot count preserved"), result.Num(), current.Num()))
    {
        return false;
    }
    TestTrue(TEXT("Slot 0 (already on X) keeps X"), RingPatchSlotIs(result[0], cellX));
    TestTrue(TEXT("Slot 1 (unset) is assigned the duplicate X entry too"), RingPatchSlotIs(result[1], cellX));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignDeterminismTest, "SOLTest.RingPatch.ReassignPoolSlotsDeterminism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The same inputs give the identical slot assignment, including where the contract allows several valid answers
bool FSOLRingPatchReassignDeterminismTest::RunTest(const FString& /*parameters*/)
{
    // Underdetermined: 2 unset slots, 4 desired cells
    const TArray<TOptional<FSOLRingCell>> unsetSlots = RingPatchUnsetSlots(2);
    const TArray<FSOLRingCell> fourCells = { RingPatchCell(0, 0), RingPatchCell(0, 1), RingPatchCell(1, 0),
        RingPatchCell(1, 1) };
    TestTrue(TEXT("Fewer slots than cells: identical results"), RingPatchSlotsIdentical(
        SOLRingPatch::ReassignPoolSlots(unsetSlots, fourCells), SOLRingPatch::ReassignPoolSlots(unsetSlots, fourCells)));

    // Mixed: stable, stale and unset slots against a partly shifted window
    TArray<TOptional<FSOLRingCell>> mixed = RingPatchUnsetSlots(6);
    mixed[0] = RingPatchCell(3, 3);
    mixed[2] = RingPatchCell(0, 1);
    mixed[4] = RingPatchCell(9, 9);
    const TArray<FSOLRingCell> shifted = { RingPatchCell(0, 1), RingPatchCell(0, 2), RingPatchCell(1, 1),
        RingPatchCell(1, 2) };
    TestTrue(TEXT("Mixed slots: identical results"), RingPatchSlotsIdentical(
        SOLRingPatch::ReassignPoolSlots(mixed, shifted), SOLRingPatch::ReassignPoolSlots(mixed, shifted)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingPatchReassignSlotCountTest, "SOLTest.RingPatch.ReassignPoolSlotsPreservesSlotCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The result always has exactly one entry per input slot, whatever the desired window's size
bool FSOLRingPatchReassignSlotCountTest::RunTest(const FString& /*parameters*/)
{
    TArray<FSOLRingCell> manyCells;
    for (int32 angularIndex = 0; angularIndex < 10; ++angularIndex)
    {
        manyCells.Add(RingPatchCell(0, angularIndex));
    }
    TArray<TOptional<FSOLRingCell>> mixed = RingPatchUnsetSlots(4);
    mixed[1] = RingPatchCell(0, 2);
    mixed[3] = RingPatchCell(7, 7);

    const int32 slotCounts[] = { 0, 1, 4 };
    const int32 windowSizes[] = { 0, 1, 4, 10 };
    for (const int32 slotCount : slotCounts)
    {
        for (const int32 windowSize : windowSizes)
        {
            const TConstArrayView<FSOLRingCell> window(manyCells.GetData(), windowSize);
            TestEqual(*FString::Printf(TEXT("%d unset slots, %d desired cells"), slotCount, windowSize),
                SOLRingPatch::ReassignPoolSlots(RingPatchUnsetSlots(slotCount), window).Num(), slotCount);
            TestEqual(*FString::Printf(TEXT("4 mixed slots, %d desired cells"), windowSize),
                SOLRingPatch::ReassignPoolSlots(mixed, window).Num(), mixed.Num());
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
