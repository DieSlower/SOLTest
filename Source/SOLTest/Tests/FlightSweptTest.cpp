/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlight.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Contract Earth radius (meters) and Earth's mean orbital speed (m/s)
    constexpr double FLIGHT_SWEPT_EARTH_RADIUS_M = 6.371e6;
    constexpr double FLIGHT_SWEPT_EARTH_SPEED_MPS = 29780.0;

    // Mercury radius (meters) and mean orbital speed (m/s)
    constexpr double FLIGHT_SWEPT_MERCURY_RADIUS_M = 2.4397e6;
    constexpr double FLIGHT_SWEPT_MERCURY_SPEED_MPS = 47360.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship state with distinctive non-default orientation and spin for collision tests
    FSOLShipState FlightSweptMakeShip(const FVector3d& positionM, const FVector3d& velocityMps)
    {
        FSOLShipState state;
        state.PositionM = positionM;
        state.VelocityMps = velocityMps;
        state.Orientation = FQuat4d(FVector3d(-0.2, 0.7, 0.4).GetSafeNormal(), 1.13);
        state.AngularVelocityRadPerS = FVector3d(-0.04, 0.05, 0.06);
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two ship states are bitwise identical field by field
    bool FlightSweptStatesIdentical(const FSOLShipState& a, const FSOLShipState& b)
    {
        return a.PositionM == b.PositionM && a.VelocityMps == b.VelocityMps
            && a.Orientation.X == b.Orientation.X && a.Orientation.Y == b.Orientation.Y
            && a.Orientation.Z == b.Orientation.Z && a.Orientation.W == b.Orientation.W
            && a.AngularVelocityRadPerS == b.AngularVelocityRadPerS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Reference solution: first point where the segment startRel -> endRel meets the origin-centered sphere
    FVector3d FlightSweptEntryPoint(const FVector3d& startRel, const FVector3d& endRel, const double radius)
    {
        const FVector3d delta = endRel - startRel;
        const double a = delta.SizeSquared();
        const double b = 2.0 * FVector3d::DotProduct(startRel, delta);
        const double c = startRel.SizeSquared() - radius * radius;
        const double disc = FMath::Max(b * b - 4.0 * a * c, 0.0);
        const double t = (-b - FMath::Sqrt(disc)) / (2.0 * a);
        return startRel + delta * t;
    }

    //////////////////////////////////////////////////////////////////////////
    // Reference solution: removes the inward (along -normal) part of the velocity relative to the body
    FVector3d FlightSweptExpectedVelocity(const FVector3d& shipVelocityMps, const FVector3d& bodyVelocityMps,
        const FVector3d& normal)
    {
        FVector3d relative = shipVelocityMps - bodyVelocityMps;
        const double inward = FVector3d::DotProduct(relative, normal);
        if (inward < 0.0)
        {
            relative -= normal * inward;
        }
        return bodyVelocityMps + relative;
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs the swept resolve on a copy of 'after' and checks the ship lands at the entry point with the entry-normal response
    void FlightSweptCheckEntry(FAutomationTestBase& test, const FString& label, const FSOLShipState& before,
        const FSOLShipState& after, const FVector3d& bodyBeforeM, const FVector3d& bodyAfterM, const FVector3d& bodyV,
        const double bodyR, const double shipR)
    {
        FSOLShipState state = after;
        const bool bContact = SOLFlight::ResolveSweptSphereCollision(before, state, bodyBeforeM, bodyAfterM, bodyV,
            bodyR, shipR);
        const double contactR = bodyR + shipR;
        const FVector3d entry = FlightSweptEntryPoint(before.PositionM - bodyBeforeM, after.PositionM - bodyAfterM,
            contactR);
        const FVector3d normal = entry.GetSafeNormal();
        const FVector3d expectedV = FlightSweptExpectedVelocity(after.VelocityMps, bodyV, normal);
        const FVector3d relPos = state.PositionM - bodyAfterM;
        const double velTolerance = 1e-6 * FMath::Max((after.VelocityMps - bodyV).Size(), 1.0);

        test.TestTrue(*FString::Printf(TEXT("%s: contact"), *label), bContact);
        test.TestTrue(*FString::Printf(TEXT("%s: position finite"), *label),
            SOLTestHelpers::IsFiniteVector(state.PositionM));
        test.TestTrue(*FString::Printf(TEXT("%s: velocity finite"), *label),
            SOLTestHelpers::IsFiniteVector(state.VelocityMps));
        test.TestTrue(*FString::Printf(TEXT("%s: body-frame position (%.6f, %.6f, %.6f) == entry (%.6f, %.6f, %.6f)"),
            *label, relPos.X, relPos.Y, relPos.Z, normal.X * contactR, normal.Y * contactR, normal.Z * contactR),
            SOLTestHelpers::VectorsNear(relPos, normal * contactR, 1e-6));
        test.TestTrue(*FString::Printf(TEXT("%s: distance %.9f == R + r %.9f"), *label, relPos.Size(), contactR),
            SOLTestHelpers::RelativeError(relPos.Size(), contactR) < 1e-9);
        test.TestTrue(*FString::Printf(TEXT("%s: velocity (%.6f, %.6f, %.6f) == (%.6f, %.6f, %.6f)"), *label,
            state.VelocityMps.X, state.VelocityMps.Y, state.VelocityMps.Z, expectedV.X, expectedV.Y, expectedV.Z),
            (state.VelocityMps - expectedV).Size() <= velTolerance);
        test.TestTrue(*FString::Printf(TEXT("%s: orientation untouched"), *label),
            state.Orientation.Equals(after.Orientation, 0.0));
        test.TestTrue(*FString::Printf(TEXT("%s: angular velocity untouched"), *label),
            state.AngularVelocityRadPerS == after.AngularVelocityRadPerS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs both the swept and the non-swept resolve and checks they give the same result
    void FlightSweptCheckMatchesNonSwept(FAutomationTestBase& test, const FString& label, const FSOLShipState& before,
        const FSOLShipState& after, const FVector3d& bodyBeforeM, const FVector3d& bodyAfterM, const FVector3d& bodyV,
        const double bodyR, const double shipR)
    {
        FSOLShipState swept = after;
        const bool bSwept = SOLFlight::ResolveSweptSphereCollision(before, swept, bodyBeforeM, bodyAfterM, bodyV, bodyR,
            shipR);
        FSOLShipState plain = after;
        const bool bPlain = SOLFlight::ResolveSphereCollision(plain, bodyAfterM, bodyV, bodyR, shipR);

        test.TestTrue(*FString::Printf(TEXT("%s: non-swept reports contact"), *label), bPlain);
        test.TestTrue(*FString::Printf(TEXT("%s: contact flag matches non-swept"), *label), bSwept == bPlain);
        test.TestTrue(*FString::Printf(TEXT("%s: position finite"), *label), SOLTestHelpers::IsFiniteVector(swept.PositionM));
        test.TestTrue(*FString::Printf(TEXT("%s: velocity finite"), *label),
            SOLTestHelpers::IsFiniteVector(swept.VelocityMps));
        test.TestTrue(*FString::Printf(TEXT("%s: position (%.6f, %.6f, %.6f) matches non-swept (%.6f, %.6f, %.6f)"),
            *label, swept.PositionM.X, swept.PositionM.Y, swept.PositionM.Z, plain.PositionM.X, plain.PositionM.Y,
            plain.PositionM.Z), (swept.PositionM - plain.PositionM).Size() <= 1e-6);
        test.TestTrue(*FString::Printf(TEXT("%s: velocity matches non-swept"), *label),
            (swept.VelocityMps - plain.VelocityMps).Size() <= 1e-9);
        test.TestTrue(*FString::Printf(TEXT("%s: orientation untouched"), *label),
            swept.Orientation.Equals(after.Orientation, 0.0));
    }
}

// --- FrameCarryDisplacement ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptCarryZeroTest, "SOLTest.FlightSwept.CarryZeroWhenFrameMatchesVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A stationary reference carries nothing; a reference moving exactly RefVelocity * RealDt (warp 1x) carries ~nothing
bool FSOLFlightSweptCarryZeroTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d refM(1.496e11, -3.0e9, 2.0e7);
    const double realDt = 1.0 / 60.0;

    const FVector3d stationary = SOLFlight::FrameCarryDisplacement(refM, refM, FVector3d::ZeroVector, realDt);
    TestTrue(TEXT("Stationary reference: zero"), stationary == FVector3d::ZeroVector);

    const FVector3d refV(FLIGHT_SWEPT_EARTH_SPEED_MPS, -1000.0, 50.0);
    const FVector3d realTime = SOLFlight::FrameCarryDisplacement(refM, refM + refV * realDt, refV, realDt);
    TestTrue(*FString::Printf(TEXT("Real-time frame: |carry| %.3e m ~ 0"), realTime.Size()), realTime.Size() <= 1e-3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptCarryWarpTest, "SOLTest.FlightSwept.CarryWarpGivesExtraDisplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At warp 3600 with RealDt 1/60 the reference moved 3600x further; the carry is exactly the extra displacement
bool FSOLFlightSweptCarryWarpTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d refPrevM(1.496e11, -3.0e9, 2.0e7);
    const FVector3d refV(FLIGHT_SWEPT_EARTH_SPEED_MPS, -1000.0, 50.0);
    const double realDt = 1.0 / 60.0;
    const double warp = 3600.0;
    const FVector3d refNowM = refPrevM + refV * (realDt * warp);
    const FVector3d carry = SOLFlight::FrameCarryDisplacement(refPrevM, refNowM, refV, realDt);
    const FVector3d expected = refV * (realDt * (warp - 1.0));
    TestTrue(*FString::Printf(TEXT("Carry (%.6f, %.6f, %.6f) == (%.6f, %.6f, %.6f)"), carry.X, carry.Y, carry.Z,
        expected.X, expected.Y, expected.Z), SOLTestHelpers::VectorsNear(carry, expected, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptCarryComponentsTest, "SOLTest.FlightSwept.CarrySignAndComponents",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each component is (Now - Prev) - RefVelocity * RealDt, with signs preserved; RealDt 0 gives (Now - Prev)
bool FSOLFlightSweptCarryComponentsTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d prevM(1000.0, 2000.0, 3000.0);
    const FVector3d nowM(-5000.0, 7000.0, 11000.0);
    const FVector3d refV(10.0, -20.0, 30.0);
    const FVector3d carry = SOLFlight::FrameCarryDisplacement(prevM, nowM, refV, 0.5);
    TestEqual(TEXT("X: (-5000 - 1000) - 10 * 0.5"), carry.X, -6005.0, 1e-9);
    TestEqual(TEXT("Y: (7000 - 2000) + 20 * 0.5"), carry.Y, 5010.0, 1e-9);
    TestEqual(TEXT("Z: (11000 - 3000) - 30 * 0.5"), carry.Z, 7985.0, 1e-9);

    // Reversed frame motion flips the sign of the displacement part
    const FVector3d reversed = SOLFlight::FrameCarryDisplacement(nowM, prevM, -refV, 0.5);
    TestTrue(TEXT("Swapping Prev/Now and negating velocity negates the carry"), (reversed + carry).Size() <= 1e-9);

    // RealDt 0: the whole frame displacement is carried
    const FVector3d zeroDt = SOLFlight::FrameCarryDisplacement(prevM, nowM, refV, 0.0);
    TestTrue(*FString::Printf(TEXT("RealDt 0: (%.3f, %.3f, %.3f) == Now - Prev"), zeroDt.X, zeroDt.Y, zeroDt.Z),
        zeroDt == nowM - prevM);
    return true;
}

// --- BodyPositionAtSubstep ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptSubstepEndTest, "SOLTest.FlightSwept.SubstepZeroRemainingIsEnd",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With no real time remaining the body is exactly at its end position; zero velocity never moves it
bool FSOLFlightSweptSubstepEndTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d endM(1.496e11, 2.0e9, -1.0e7);
    const FVector3d velocity(-400.0, FLIGHT_SWEPT_EARTH_SPEED_MPS, 3.0);
    TestTrue(TEXT("Remaining 0, warp 3600: exactly the end position"),
        SOLFlight::BodyPositionAtSubstep(endM, velocity, 3600.0, 0.0) == endM);
    TestTrue(TEXT("Remaining 0, warp 1: exactly the end position"),
        SOLFlight::BodyPositionAtSubstep(endM, velocity, 1.0, 0.0) == endM);
    TestTrue(TEXT("Zero velocity, remaining 1/60, warp 86400: end position"),
        SOLFlight::BodyPositionAtSubstep(endM, FVector3d::ZeroVector, 86400.0, 1.0 / 60.0) == endM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptSubstepOffsetTest, "SOLTest.FlightSwept.SubstepOffsetScalesWithWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The position is End - Velocity * Warp * Remaining: warp 1 subtracts Velocity * h and the offset grows linearly with warp
bool FSOLFlightSweptSubstepOffsetTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d endM(1.0e7, -2.0e7, 3.0e6);
    const FVector3d velocity(12000.0, -25000.0, 700.0);
    const double remaining = 1.0 / 120.0;

    const FVector3d atWarp1 = SOLFlight::BodyPositionAtSubstep(endM, velocity, 1.0, remaining);
    TestTrue(*FString::Printf(TEXT("Warp 1: (%.6f, %.6f, %.6f) == End - V * h"), atWarp1.X, atWarp1.Y, atWarp1.Z),
        SOLTestHelpers::VectorsNear(atWarp1, endM - velocity * remaining, 1e-12));

    for (const double warp : { 1.0, 10.0, 60.0, 3600.0, 86400.0 })
    {
        const FVector3d offset = endM - SOLFlight::BodyPositionAtSubstep(endM, velocity, warp, remaining);
        const FVector3d expected = velocity * (warp * remaining);
        TestTrue(*FString::Printf(TEXT("Warp %.0f: offset (%.6f, %.6f, %.6f) == V * warp * h"), warp, offset.X,
            offset.Y, offset.Z), SOLTestHelpers::VectorsNear(offset, expected, 1e-9));
    }

    // Half the remaining time halves the offset
    const FVector3d full = endM - SOLFlight::BodyPositionAtSubstep(endM, velocity, 100.0, 2.0 * remaining);
    const FVector3d half = endM - SOLFlight::BodyPositionAtSubstep(endM, velocity, 100.0, remaining);
    TestTrue(TEXT("Offset is linear in the remaining time"), SOLTestHelpers::VectorsNear(half * 2.0, full, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptSubstepEarthTest, "SOLTest.FlightSwept.SubstepEarthAtDayWarp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Earth at 29.78 km/s, warp 86400, 1/60 s left: the body sits 29.78e3 * 86400 / 60 m back along its velocity
bool FSOLFlightSweptSubstepEarthTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d endM(1.496e11, 2.0e9, -1.0e7);
    const FVector3d direction(0.6, -0.8, 0.0);
    const FVector3d velocity = direction * FLIGHT_SWEPT_EARTH_SPEED_MPS;
    const FVector3d offset = endM - SOLFlight::BodyPositionAtSubstep(endM, velocity, 86400.0, 1.0 / 60.0);
    const double distance = 29.78e3 * 86400.0 / 60.0;
    TestEqual(TEXT("X offset"), offset.X, direction.X * distance, 1e-3);
    TestEqual(TEXT("Y offset"), offset.Y, direction.Y * distance, 1e-3);
    TestEqual(TEXT("Z offset"), offset.Z, 0.0, 1e-3);
    return true;
}

// --- ResolveSweptSphereCollision ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptMissTest, "SOLTest.FlightSwept.MissLeavesStateUntouched",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (a) A segment that never touches the contact sphere and ends outside returns false and leaves the state untouched
bool FSOLFlightSweptMissTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyBeforeM(1.0e7, 2.0e7, 3.0e7);
    const FVector3d bodyV(100.0, -200.0, 300.0);
    const double dt = 1.0 / 30.0;
    const FVector3d bodyAfterM = bodyBeforeM + bodyV * dt;
    struct FSegment { FVector3d StartRel; FVector3d EndRel; };
    const FSegment segments[] =
    {
        { FVector3d(2000.0, 1000.0, 0.0), FVector3d(2000.0, -1000.0, 0.0) },    // passes by at 2000 m
        { FVector3d(0.0, 0.0, 3000.0), FVector3d(0.0, 0.0, 600.0) },            // approaches, stops short
        { FVector3d(0.0, 600.0, 0.0), FVector3d(0.0, 5000.0, 0.0) },            // moving away
        { FVector3d(-2000.0, 0.0, 520.0), FVector3d(2000.0, 0.0, 520.0) },      // passes 10 m above the contact sphere
    };
    for (const FSegment& segment : segments)
    {
        const FVector3d velocity = bodyV + (segment.EndRel - segment.StartRel) / dt;
        const FSOLShipState before = FlightSweptMakeShip(bodyBeforeM + segment.StartRel, velocity);
        const FSOLShipState original = FlightSweptMakeShip(bodyAfterM + segment.EndRel, velocity);
        FSOLShipState state = original;
        const bool bContact = SOLFlight::ResolveSweptSphereCollision(before, state, bodyBeforeM, bodyAfterM, bodyV,
            500.0, 10.0);
        const FString label = FString::Printf(TEXT("(%.0f, %.0f, %.0f) -> (%.0f, %.0f, %.0f)"), segment.StartRel.X,
            segment.StartRel.Y, segment.StartRel.Z, segment.EndRel.X, segment.EndRel.Y, segment.EndRel.Z);
        TestFalse(*FString::Printf(TEXT("%s: no contact"), *label), bContact);
        TestTrue(*FString::Printf(TEXT("%s: state untouched"), *label), FlightSweptStatesIdentical(state, original));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptEnterTest, "SOLTest.FlightSwept.EnteringPlacesAtEntryDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (b) End point inside after entering: the ship sits on R + r at the ENTRY direction (not the end-point direction)
bool FSOLFlightSweptEnterTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyBeforeM(1.0e7, 2.0e7, 3.0e7);
    const FVector3d bodyV(100.0, -200.0, 300.0);
    const double dt = 1.0 / 30.0;
    const FVector3d bodyAfterM = bodyBeforeM + bodyV * dt;
    const double bodyR = 500.0;
    const double shipR = 10.0;

    // Descends along x = 300: enters at z = sqrt(510^2 - 300^2), ends at (300, 0, 300) (inside)
    const FVector3d startRel(300.0, 0.0, 2000.0);
    const FVector3d endRel(300.0, 0.0, 300.0);
    const FVector3d velocity = bodyV + FVector3d(5.0, 3.0, -40.0);
    const FSOLShipState before = FlightSweptMakeShip(bodyBeforeM + startRel, velocity);
    const FSOLShipState after = FlightSweptMakeShip(bodyAfterM + endRel, velocity);
    FlightSweptCheckEntry(*this, TEXT("Oblique entry"), before, after, bodyBeforeM, bodyAfterM, bodyV, bodyR, shipR);

    // Explicit numbers for the entry direction, independent of the reference helper
    FSOLShipState state = after;
    SOLFlight::ResolveSweptSphereCollision(before, state, bodyBeforeM, bodyAfterM, bodyV, bodyR, shipR);
    const FVector3d entryDir = FVector3d(300.0, 0.0, FMath::Sqrt(510.0 * 510.0 - 300.0 * 300.0)) / 510.0;
    TestTrue(TEXT("Oblique entry: explicit entry point"),
        SOLTestHelpers::VectorsNear(state.PositionM - bodyAfterM, entryDir * 510.0, 1e-6));
    TestFalse(TEXT("Oblique entry: not the end-point direction"), SOLTestHelpers::VectorsNear(
        state.PositionM - bodyAfterM, endRel.GetSafeNormal() * 510.0, 1e-3));

    // Tangential relative velocity survives; normal relative velocity is zero
    const FVector3d relV = state.VelocityMps - bodyV;
    TestTrue(*FString::Printf(TEXT("Oblique entry: relative normal velocity %.9f == 0"),
        FVector3d::DotProduct(relV, entryDir)), FMath::Abs(FVector3d::DotProduct(relV, entryDir)) <= 1e-9);
    TestEqual(TEXT("Oblique entry: Y (tangential) relative velocity kept"), relV.Y, 3.0, 1e-9);

    // Straight radial dive from +Y: lands at (0, 510, 0) with the relative velocity fully removed
    const FSOLShipState radialBefore = FlightSweptMakeShip(bodyBeforeM + FVector3d(0.0, 5000.0, 0.0),
        bodyV + FVector3d(0.0, -9000.0, 0.0));
    const FSOLShipState radialAfter = FlightSweptMakeShip(bodyAfterM + FVector3d(0.0, 200.0, 0.0),
        bodyV + FVector3d(0.0, -9000.0, 0.0));
    FlightSweptCheckEntry(*this, TEXT("Radial dive"), radialBefore, radialAfter, bodyBeforeM, bodyAfterM, bodyV, bodyR,
        shipR);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptTunnelTest, "SOLTest.FlightSwept.TunnellingStopsAtNearSide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (c) 10 km before the near side to 10 km past the far side in one step at 1e6 m/s: contact, ship on the NEAR side
bool FSOLFlightSweptTunnelTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(-4.0e10, 1.0e11, 2.0e8);
    const FVector3d bodyV = FVector3d::ZeroVector;
    const double bodyR = FLIGHT_SWEPT_EARTH_RADIUS_M;
    const double shipR = 10.0;
    const double contactR = bodyR + shipR;
    const double speed = 1.0e6;

    for (const double sideOffset : { 0.0, 1.0e6, 5.0e6 })
    {
        const double halfChord = FMath::Sqrt(contactR * contactR - sideOffset * sideOffset);
        const FVector3d startRel(-(halfChord + 1.0e4), sideOffset, 0.0);
        const FVector3d endRel(halfChord + 1.0e4, sideOffset, 0.0);
        const FVector3d velocity(speed, 0.0, 0.0);
        const FSOLShipState before = FlightSweptMakeShip(bodyM + startRel, velocity);
        const FSOLShipState after = FlightSweptMakeShip(bodyM + endRel, velocity);
        const FString label = FString::Printf(TEXT("Tunnel, side offset %.0f m"), sideOffset);
        FlightSweptCheckEntry(*this, label, before, after, bodyM, bodyM, bodyV, bodyR, shipR);

        FSOLShipState state = after;
        SOLFlight::ResolveSweptSphereCollision(before, state, bodyM, bodyM, bodyV, bodyR, shipR);
        const FVector3d relPos = state.PositionM - bodyM;
        TestTrue(*FString::Printf(TEXT("%s: on the near side (x %.3f ~ %.3f)"), *label, relPos.X, -halfChord),
            FMath::Abs(relPos.X + halfChord) <= 1e-6 * contactR);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptGrazeTest, "SOLTest.FlightSwept.GrazingSegments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (d) Exact tangent: finite and never deflected; a 1 mm graze into the sphere collides at its entry; 1 mm outside misses
bool FSOLFlightSweptGrazeTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(1.0e6, -1.0e6, 2.0e6);
    const FVector3d bodyV = FVector3d::ZeroVector;
    const double bodyR = 500.0;
    const double shipR = 10.0;
    const FVector3d velocity(120000.0, 0.0, 0.0);

    // Exactly tangent at z = R + r: whether or not it counts as contact, the ship must not be moved off its path or slowed
    {
        const FSOLShipState before = FlightSweptMakeShip(bodyM + FVector3d(-2000.0, 0.0, 510.0), velocity);
        const FSOLShipState original = FlightSweptMakeShip(bodyM + FVector3d(2000.0, 0.0, 510.0), velocity);
        FSOLShipState state = original;
        const bool bContact = SOLFlight::ResolveSweptSphereCollision(before, state, bodyM, bodyM, bodyV, bodyR, shipR);
        TestTrue(TEXT("Tangent: position finite"), SOLTestHelpers::IsFiniteVector(state.PositionM));
        TestTrue(TEXT("Tangent: velocity finite"), SOLTestHelpers::IsFiniteVector(state.VelocityMps));
        TestTrue(TEXT("Tangent: velocity unchanged (no inward component)"),
            (state.VelocityMps - velocity).Size() <= 1e-9 * velocity.Size());
        if (bContact)
        {
            TestTrue(TEXT("Tangent (contact): placed at the touching point (0, 0, R + r)"),
                (state.PositionM - (bodyM + FVector3d(0.0, 0.0, 510.0))).Size() <= 1e-6);
        }
        else
        {
            TestTrue(TEXT("Tangent (no contact): state untouched"), FlightSweptStatesIdentical(state, original));
        }
    }

    // 1 mm inside the contact sphere: both ends outside, but the segment enters
    {
        const FSOLShipState before = FlightSweptMakeShip(bodyM + FVector3d(-2000.0, 0.0, 509.999), velocity);
        const FSOLShipState after = FlightSweptMakeShip(bodyM + FVector3d(2000.0, 0.0, 509.999), velocity);
        FlightSweptCheckEntry(*this, TEXT("1 mm graze"), before, after, bodyM, bodyM, bodyV, bodyR, shipR);
    }

    // 1 mm outside the contact sphere: no contact
    {
        const FSOLShipState before = FlightSweptMakeShip(bodyM + FVector3d(-2000.0, 0.0, 510.001), velocity);
        const FSOLShipState original = FlightSweptMakeShip(bodyM + FVector3d(2000.0, 0.0, 510.001), velocity);
        FSOLShipState state = original;
        TestFalse(TEXT("1 mm outside: no contact"),
            SOLFlight::ResolveSweptSphereCollision(before, state, bodyM, bodyM, bodyV, bodyR, shipR));
        TestTrue(TEXT("1 mm outside: state untouched"), FlightSweptStatesIdentical(state, original));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptStartInsideTest, "SOLTest.FlightSwept.StartInsideMatchesNonSwept",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (e) A segment starting inside (already overlapping) and ending inside behaves exactly like ResolveSphereCollision
bool FSOLFlightSweptStartInsideTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyBeforeM(1.0e7, 2.0e7, 3.0e7);
    const FVector3d bodyV(100.0, -200.0, 300.0);
    const double dt = 1.0 / 30.0;
    const FVector3d bodyAfterM = bodyBeforeM + bodyV * dt;

    // Sinking deeper, with a slide
    const FVector3d velocityA = bodyV + FVector3d(3.0, 0.0, -30.0);
    FlightSweptCheckMatchesNonSwept(*this, TEXT("Sinking"),
        FlightSweptMakeShip(bodyBeforeM + FVector3d(0.0, 0.0, 300.0), velocityA),
        FlightSweptMakeShip(bodyAfterM + FVector3d(100.0, 0.0, 250.0), velocityA), bodyBeforeM, bodyAfterM, bodyV,
        500.0, 10.0);

    // Diagonal, moving slightly outward but still inside
    const FVector3d velocityB = bodyV + FVector3d(6.0, 8.0, 1.0);
    FlightSweptCheckMatchesNonSwept(*this, TEXT("Rising inside"),
        FlightSweptMakeShip(bodyBeforeM + FVector3d(240.0, 320.0, 0.0), velocityB),
        FlightSweptMakeShip(bodyAfterM + FVector3d(270.0, 360.0, 5.0), velocityB), bodyBeforeM, bodyAfterM, bodyV,
        500.0, 10.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptMovingBodyTest, "SOLTest.FlightSwept.MovingBodyCarriesRestingShip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (f) A ship resting on a moving body (Earth-like and a much faster synthetic body) stays put in the body frame
bool FSOLFlightSweptMovingBodyTest::RunTest(const FString& /*parameters*/)
{
    struct FBodyCase { const TCHAR* Name; FVector3d PositionM; FVector3d VelocityMps; double RadiusM; };
    const FBodyCase bodies[] =
    {
        { TEXT("Earth-like"), FVector3d(1.496e11, -2.0e9, 1.0e7), FVector3d(-400.0, FLIGHT_SWEPT_EARTH_SPEED_MPS, 3.0),
            FLIGHT_SWEPT_EARTH_RADIUS_M },
        { TEXT("Fast synthetic"), FVector3d(5.0e10, -3.0e10, 1.0e9), FVector3d(3.0e6, -1.0e6, 2.0e5), 1.0e5 },
    };
    const double dt = 1.0 / 30.0;
    const double shipR = 10.0;
    const FVector3d normal(0.6, 0.0, 0.8);
    for (const FBodyCase& body : bodies)
    {
        const FVector3d bodyAfterM = body.PositionM + body.VelocityMps * dt;
        const double contactR = body.RadiusM + shipR;

        // Settled 1 cm into the contact sphere, co-moving: contact, pushed to the surface, zero relative velocity
        {
            const FSOLShipState before = FlightSweptMakeShip(body.PositionM + normal * (contactR - 0.01),
                body.VelocityMps);
            const FSOLShipState after = FlightSweptMakeShip(bodyAfterM + normal * (contactR - 0.01), body.VelocityMps);
            FSOLShipState state = after;
            const bool bContact = SOLFlight::ResolveSweptSphereCollision(before, state, body.PositionM, bodyAfterM,
                body.VelocityMps, body.RadiusM, shipR);
            TestTrue(*FString::Printf(TEXT("%s, settled: contact"), body.Name), bContact);
            TestTrue(*FString::Printf(TEXT("%s, settled: on the surface under the ship"), body.Name),
                (state.PositionM - (bodyAfterM + normal * contactR)).Size() <= 1e-3);
            TestTrue(*FString::Printf(TEXT("%s, settled: zero relative velocity"), body.Name),
                (state.VelocityMps - body.VelocityMps).Size() <= 1e-6);
        }

        // Exactly on the surface, co-moving: stays put with the body's velocity (no NaN)
        {
            const FSOLShipState before = FlightSweptMakeShip(body.PositionM + normal * contactR, body.VelocityMps);
            const FSOLShipState after = FlightSweptMakeShip(bodyAfterM + normal * contactR, body.VelocityMps);
            FSOLShipState state = after;
            SOLFlight::ResolveSweptSphereCollision(before, state, body.PositionM, bodyAfterM, body.VelocityMps,
                body.RadiusM, shipR);
            TestTrue(*FString::Printf(TEXT("%s, on surface: finite"), body.Name),
                SOLTestHelpers::IsFiniteVector(state.PositionM) && SOLTestHelpers::IsFiniteVector(state.VelocityMps));
            TestTrue(*FString::Printf(TEXT("%s, on surface: stays put"), body.Name),
                (state.PositionM - after.PositionM).Size() <= 1e-3);
            TestTrue(*FString::Printf(TEXT("%s, on surface: velocity == body velocity"), body.Name),
                (state.VelocityMps - body.VelocityMps).Size() <= 1e-6);
        }

        // Descending onto the moving body from 5 km in the body frame, ending 1 m inside: entry is computed in the body frame
        {
            const FVector3d relV = normal * -(5000.0 / dt) + FVector3d(0.0, 40.0, 0.0);
            const FSOLShipState before = FlightSweptMakeShip(body.PositionM + normal * (contactR + 4999.0),
                body.VelocityMps + relV);
            const FSOLShipState after = FlightSweptMakeShip(bodyAfterM + normal * (contactR - 1.0)
                + FVector3d(0.0, 40.0 * dt, 0.0), body.VelocityMps + relV);
            FlightSweptCheckEntry(*this, FString::Printf(TEXT("%s, landing"), body.Name), before, after,
                body.PositionM, bodyAfterM, body.VelocityMps, body.RadiusM, shipR);
        }
    }

    // The fast body sweeps through a ship at rest in the universe: in the body frame the ship tunnels in from +Y
    {
        const FVector3d bodyM(5.0e10, -3.0e10, 1.0e9);
        const FVector3d bodyV(0.0, 3.0e6, 0.0);
        const double bodyR = 1.0e4;
        const FVector3d bodyAfterM = bodyM + bodyV * dt;
        const FVector3d shipV = FVector3d::ZeroVector;
        const FSOLShipState before = FlightSweptMakeShip(bodyM + FVector3d(0.0, 5.0e4, 0.0), shipV);
        const FSOLShipState after = FlightSweptMakeShip(before.PositionM + shipV * dt, shipV);
        FlightSweptCheckEntry(*this, TEXT("Body sweeps into a stationary ship"), before, after, bodyM, bodyAfterM,
            bodyV, bodyR, shipR);
        FSOLShipState state = after;
        SOLFlight::ResolveSweptSphereCollision(before, state, bodyM, bodyAfterM, bodyV, bodyR, shipR);
        TestTrue(TEXT("Body sweeps into a stationary ship: ends on the +Y (leading) face"),
            (state.PositionM - bodyAfterM).Y > 0.999 * (bodyR + shipR));
        TestTrue(TEXT("Body sweeps into a stationary ship: picks up exactly the body velocity"),
            (state.VelocityMps - bodyV).Size() <= 1e-3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptDegenerateTest, "SOLTest.FlightSwept.DegenerateSegmentsNoNaN",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (g) Zero-length segments and a ship exactly at the body center give finite, contract-consistent results
bool FSOLFlightSweptDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(1.0e6, -1.0e6, 2.0e6);
    const FVector3d bodyV(10.0, 20.0, 30.0);
    const double bodyR = 1000.0;
    const double shipR = 10.0;

    // Zero-length outside: no contact, untouched
    {
        const FSOLShipState before = FlightSweptMakeShip(bodyM + FVector3d(0.0, 3000.0, 0.0), bodyV);
        FSOLShipState state = before;
        TestFalse(TEXT("Zero-length outside: no contact"),
            SOLFlight::ResolveSweptSphereCollision(before, state, bodyM, bodyM, bodyV, bodyR, shipR));
        TestTrue(TEXT("Zero-length outside: untouched"), FlightSweptStatesIdentical(state, before));
    }

    // Zero-length inside: like the non-swept resolve
    {
        const FSOLShipState ship = FlightSweptMakeShip(bodyM + FVector3d(300.0, -400.0, 0.0),
            bodyV + FVector3d(-3.0, 4.0, 7.0));
        FlightSweptCheckMatchesNonSwept(*this, TEXT("Zero-length inside"), ship, ship, bodyM, bodyM, bodyV, bodyR,
            shipR);
    }

    // Before and after exactly at the center: finite, pushed out along +Z like the non-swept resolve
    {
        const FSOLShipState ship = FlightSweptMakeShip(bodyM, bodyV + FVector3d(1.0, 2.0, -3.0));
        FlightSweptCheckMatchesNonSwept(*this, TEXT("At center"), ship, ship, bodyM, bodyM, bodyV, bodyR, shipR);
        FSOLShipState state = ship;
        SOLFlight::ResolveSweptSphereCollision(ship, state, bodyM, bodyM, bodyV, bodyR, shipR);
        TestTrue(TEXT("At center: pushed to R + r along +Z"),
            (state.PositionM - (bodyM + FVector3d(0.0, 0.0, bodyR + shipR))).Size() <= 1e-6);
    }

    // Diving from outside to exactly the center: enters from +Z, finite
    {
        const FVector3d velocity = bodyV + FVector3d(0.0, 0.0, -60000.0);
        const FSOLShipState before = FlightSweptMakeShip(bodyM + FVector3d(0.0, 0.0, 2000.0), velocity);
        const FSOLShipState after = FlightSweptMakeShip(bodyM, velocity);
        FlightSweptCheckEntry(*this, TEXT("Dive to center"), before, after, bodyM, bodyM, bodyV, bodyR, shipR);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptShipRadiusTest, "SOLTest.FlightSwept.EntryRespectsShipRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (h) A segment passing within R + r but outside R collides; with a zero ship radius it does not
bool FSOLFlightSweptShipRadiusTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(1.0e7, 2.0e7, 3.0e7);
    const FVector3d bodyV = FVector3d::ZeroVector;
    const double bodyR = 500.0;
    const FVector3d velocity(0.0, 90000.0, 0.0);

    // Closest approach 505 m: outside R = 500, inside R + r = 510
    const FSOLShipState before = FlightSweptMakeShip(bodyM + FVector3d(505.0, -1500.0, 0.0), velocity);
    const FSOLShipState after = FlightSweptMakeShip(bodyM + FVector3d(505.0, 1500.0, 0.0), velocity);
    FlightSweptCheckEntry(*this, TEXT("Ship radius 10"), before, after, bodyM, bodyM, bodyV, bodyR, 10.0);

    FSOLShipState state = after;
    TestFalse(TEXT("Ship radius 0: no contact"),
        SOLFlight::ResolveSweptSphereCollision(before, state, bodyM, bodyM, bodyV, bodyR, 0.0));
    TestTrue(TEXT("Ship radius 0: untouched"), FlightSweptStatesIdentical(state, after));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightSweptRelativisticTest, "SOLTest.FlightSwept.HalfLightSpeedAcrossMercury",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (i) At 0.5c a 1/30 s step jumps clean across a moving Mercury-sized body; it still collides on the near side
bool FSOLFlightSweptRelativisticTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FVector3d bodyM(5.79e10, -1.0e9, 3.0e8);
    const FVector3d bodyV(0.0, FLIGHT_SWEPT_MERCURY_SPEED_MPS, 0.0);
    const double dt = 1.0 / 30.0;
    const FVector3d bodyAfterM = bodyM + bodyV * dt;
    const double bodyR = FLIGHT_SWEPT_MERCURY_RADIUS_M;
    const double shipR = params.ShipRadiusM;
    const double contactR = bodyR + shipR;

    const FVector3d velocity(params.MaxSpeedMps, 0.0, 0.0);
    const FSOLShipState before = FlightSweptMakeShip(bodyM + FVector3d(-(contactR + 2.0e4), 1.0e5, 0.0), velocity);
    const FSOLShipState after = FlightSweptMakeShip(before.PositionM + velocity * dt, velocity);

    // Both end points are outside the contact sphere in the body frame
    TestTrue(TEXT("Setup: start outside"), (before.PositionM - bodyM).Size() > contactR);
    TestTrue(TEXT("Setup: end outside"), (after.PositionM - bodyAfterM).Size() > contactR);

    FlightSweptCheckEntry(*this, TEXT("0.5c across Mercury"), before, after, bodyM, bodyAfterM, bodyV, bodyR, shipR);
    FSOLShipState state = after;
    SOLFlight::ResolveSweptSphereCollision(before, state, bodyM, bodyAfterM, bodyV, bodyR, shipR);
    TestTrue(TEXT("0.5c across Mercury: on the near (-X) side"), (state.PositionM - bodyAfterM).X < 0.0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
