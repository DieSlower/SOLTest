/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLBoltMath.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Muzzle speed (m/s) and frame step (s) from SDD 7's starting numbers, typed independently of production code
    constexpr double BOLTMATH_MUZZLE_SPEED = 1500.0;
    constexpr double BOLTMATH_FRAME_DT = 1.0 / 60.0;

    // Tight tolerance for small, exactly-representable arithmetic
    constexpr double BOLTMATH_TIGHT_TOL = 1.0e-9;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double BoltMathNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of a matches b within tolerance
    bool BoltMathVecNear(const FVector3d& a, const FVector3d& b, const double tolerance)
    {
        return std::abs(a.X - b.X) <= tolerance && std::abs(a.Y - b.Y) <= tolerance && std::abs(a.Z - b.Z) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of v is finite
    bool BoltMathVecFinite(const FVector3d& v)
    {
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathMuzzleVelocityTest, "SOLTest.BoltMath.MuzzleVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MuzzleVelocity adds the normalised aim direction times muzzle speed to the inherited ship velocity
bool FSOLBoltMathMuzzleVelocityTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d ship(10.0, -20.0, 5.0);
    const FVector3d result = SOLBoltMath::MuzzleVelocity(ship, FVector3d(0.0, 2.0, 0.0), BOLTMATH_MUZZLE_SPEED);
    TestTrue(TEXT("Unnormalised aim (0,2,0) gives ship + (0,1500,0)"),
        BoltMathVecNear(result, FVector3d(10.0, 1480.0, 5.0), BOLTMATH_TIGHT_TOL));

    const FVector3d diagonal = SOLBoltMath::MuzzleVelocity(FVector3d::ZeroVector, FVector3d(3.0, 4.0, 0.0), 100.0);
    TestTrue(TEXT("Aim (3,4,0) at speed 100 from rest gives (60,80,0)"),
        BoltMathVecNear(diagonal, FVector3d(60.0, 80.0, 0.0), BOLTMATH_TIGHT_TOL));

    const FVector3d atRest = SOLBoltMath::MuzzleVelocity(FVector3d::ZeroVector, FVector3d(1.0, 0.0, 0.0), 0.0);
    TestTrue(TEXT("Zero muzzle speed returns the ship velocity"), BoltMathVecNear(atRest, FVector3d::ZeroVector,
        BOLTMATH_TIGHT_TOL));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathMuzzleVelocityDegenerateTest, "SOLTest.BoltMath.MuzzleVelocityDegenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or NaN aim direction returns the ship velocity unchanged rather than NaN or a guessed direction
bool FSOLBoltMathMuzzleVelocityDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d ship(7.0, 8.0, 9.0);
    const FVector3d zeroAim = SOLBoltMath::MuzzleVelocity(ship, FVector3d::ZeroVector, BOLTMATH_MUZZLE_SPEED);
    TestTrue(TEXT("Zero aim returns ship velocity"), BoltMathVecNear(zeroAim, ship, 0.0));

    const FVector3d nanAim = SOLBoltMath::MuzzleVelocity(ship, FVector3d(BoltMathNaN(), 0.0, 0.0),
        BOLTMATH_MUZZLE_SPEED);
    TestTrue(TEXT("NaN aim returns ship velocity"), BoltMathVecNear(nanAim, ship, 0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathConvergedAimTest, "SOLTest.BoltMath.ConvergedAim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ConvergedAimDirection points from the muzzle at the crosshair point along the (normalised) camera ray
bool FSOLBoltMathConvergedAimTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d muzzle(0.0, 1.0, 0.0);
    const FVector3d camera(0.0, 0.0, 0.0);
    const FVector3d result = SOLBoltMath::ConvergedAimDirection(muzzle, camera, FVector3d(2.0, 0.0, 0.0), 100.0);
    const FVector3d expected = FVector3d(100.0, -1.0, 0.0) / std::sqrt(100.0 * 100.0 + 1.0);
    TestTrue(TEXT("Direction from (0,1,0) to (100,0,0)"), BoltMathVecNear(result, expected, BOLTMATH_TIGHT_TOL));
    TestNearlyEqual(TEXT("Result is unit length"), result.Size(), 1.0, BOLTMATH_TIGHT_TOL);

    // Muzzle on the camera ray: aim is exactly the camera forward
    const FVector3d onRay = SOLBoltMath::ConvergedAimDirection(FVector3d(0.0, 0.0, 5.0), FVector3d::ZeroVector,
        FVector3d(0.0, 0.0, 1.0), 1000.0);
    TestTrue(TEXT("Muzzle on the ray aims along forward"), BoltMathVecNear(onRay, FVector3d(0.0, 0.0, 1.0),
        BOLTMATH_TIGHT_TOL));

    // Two muzzles either side of the camera converge on the same point
    const FVector3d left = SOLBoltMath::ConvergedAimDirection(FVector3d(0.0, -2.0, 0.0), FVector3d::ZeroVector,
        FVector3d(1.0, 0.0, 0.0), 500.0);
    const FVector3d right = SOLBoltMath::ConvergedAimDirection(FVector3d(0.0, 2.0, 0.0), FVector3d::ZeroVector,
        FVector3d(1.0, 0.0, 0.0), 500.0);
    TestTrue(TEXT("Left muzzle toes inward (+Y)"), left.Y > 0.0);
    TestTrue(TEXT("Right muzzle toes inward (-Y)"), right.Y < 0.0);
    TestNearlyEqual(TEXT("Toe-in is symmetric"), left.Y, -right.Y, BOLTMATH_TIGHT_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathConvergedAimDegenerateTest, "SOLTest.BoltMath.ConvergedAimDegenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Degenerate convergence falls back to normalised camera forward, then to (1,0,0); never NaN, always unit length
bool FSOLBoltMathConvergedAimDegenerateTest::RunTest(const FString& /*parameters*/)
{
    // Convergence point coincides with the muzzle
    const FVector3d samePoint = SOLBoltMath::ConvergedAimDirection(FVector3d(0.0, 100.0, 0.0), FVector3d::ZeroVector,
        FVector3d(0.0, 3.0, 0.0), 100.0);
    TestTrue(TEXT("Point == muzzle returns normalised forward"), BoltMathVecNear(samePoint, FVector3d(0.0, 1.0, 0.0),
        BOLTMATH_TIGHT_TOL));

    // Non-positive distances
    const FVector3d zeroDistance = SOLBoltMath::ConvergedAimDirection(FVector3d(0.0, 1.0, 0.0), FVector3d::ZeroVector,
        FVector3d(0.0, 0.0, 4.0), 0.0);
    TestTrue(TEXT("Zero distance returns normalised forward"), BoltMathVecNear(zeroDistance, FVector3d(0.0, 0.0, 1.0),
        BOLTMATH_TIGHT_TOL));
    const FVector3d negativeDistance = SOLBoltMath::ConvergedAimDirection(FVector3d(0.0, 1.0, 0.0),
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, 4.0), -50.0);
    TestTrue(TEXT("Negative distance returns normalised forward"), BoltMathVecNear(negativeDistance,
        FVector3d(0.0, 0.0, 1.0), BOLTMATH_TIGHT_TOL));

    // Zero forward: forward is degenerate too, so (1,0,0)
    const FVector3d zeroForward = SOLBoltMath::ConvergedAimDirection(FVector3d(0.0, 1.0, 0.0), FVector3d::ZeroVector,
        FVector3d::ZeroVector, 100.0);
    TestTrue(TEXT("Zero forward returns (1,0,0)"), BoltMathVecNear(zeroForward, FVector3d(1.0, 0.0, 0.0),
        BOLTMATH_TIGHT_TOL));

    // NaN forward must not leak NaN
    const FVector3d nanForward = SOLBoltMath::ConvergedAimDirection(FVector3d(0.0, 1.0, 0.0), FVector3d::ZeroVector,
        FVector3d(BoltMathNaN(), 0.0, 0.0), 100.0);
    TestTrue(TEXT("NaN forward gives a finite result"), BoltMathVecFinite(nanForward));
    TestNearlyEqual(TEXT("NaN forward gives a unit result"), nanForward.Size(), 1.0, BOLTMATH_TIGHT_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathAdvancePositionTest, "SOLTest.BoltMath.AdvancePosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AdvancePosition integrates position + velocity * dt in doubles, and ignores negative or NaN dt
bool FSOLBoltMathAdvancePositionTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d start(1.0, 2.0, 3.0);
    const FVector3d velocity(BOLTMATH_MUZZLE_SPEED, 0.0, -60.0);
    TestTrue(TEXT("One 1/60 s frame at 1.5 km/s moves 25 m"),
        BoltMathVecNear(SOLBoltMath::AdvancePosition(start, velocity, BOLTMATH_FRAME_DT), FVector3d(26.0, 2.0, 2.0),
            BOLTMATH_TIGHT_TOL));
    TestTrue(TEXT("dt 0 leaves position unchanged"),
        BoltMathVecNear(SOLBoltMath::AdvancePosition(start, velocity, 0.0), start, 0.0));
    TestTrue(TEXT("Negative dt leaves position unchanged"),
        BoltMathVecNear(SOLBoltMath::AdvancePosition(start, velocity, -1.0), start, 0.0));
    TestTrue(TEXT("NaN dt leaves position unchanged"),
        BoltMathVecNear(SOLBoltMath::AdvancePosition(start, velocity, BoltMathNaN()), start, 0.0));

    // Universe-scale position keeps sub-metre precision (doubles, no float truncation)
    const FVector3d far(1.0e12, -1.0e12, 5.0e11);
    const FVector3d moved = SOLBoltMath::AdvancePosition(far, FVector3d(BOLTMATH_MUZZLE_SPEED, 0.0, 0.0),
        BOLTMATH_FRAME_DT);
    TestNearlyEqual(TEXT("1e12 m X advances by 25 m"), moved.X - far.X, 25.0, 1.0e-3);
    TestNearlyEqual(TEXT("1e12 m Y unchanged"), moved.Y, far.Y, 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathLifetimeTest, "SOLTest.BoltMath.Lifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// IsExpired is true at or below zero and for NaN; AdvanceLifetime subtracts dt and never yields NaN
bool FSOLBoltMathLifetimeTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("0 is expired"), SOLBoltMath::IsExpired(0.0));
    TestTrue(TEXT("-1 is expired"), SOLBoltMath::IsExpired(-1.0));
    TestTrue(TEXT("NaN is expired"), SOLBoltMath::IsExpired(BoltMathNaN()));
    TestFalse(TEXT("1e-9 is alive"), SOLBoltMath::IsExpired(1.0e-9));
    TestFalse(TEXT("3.5 is alive"), SOLBoltMath::IsExpired(3.5));
    TestFalse(TEXT("+inf is alive"), SOLBoltMath::IsExpired(std::numeric_limits<double>::infinity()));

    TestNearlyEqual(TEXT("3.5 - 0.5 = 3.0"), SOLBoltMath::AdvanceLifetime(3.5, 0.5), 3.0, BOLTMATH_TIGHT_TOL);
    TestNearlyEqual(TEXT("0.25 - 1 = -0.75"), SOLBoltMath::AdvanceLifetime(0.25, 1.0), -0.75, BOLTMATH_TIGHT_TOL);
    TestEqual(TEXT("NaN remaining gives 0"), SOLBoltMath::AdvanceLifetime(BoltMathNaN(), BOLTMATH_FRAME_DT), 0.0);
    TestFalse(TEXT("NaN dt never gives NaN"), std::isnan(SOLBoltMath::AdvanceLifetime(3.5, BoltMathNaN())));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathLifetimeFramesTest, "SOLTest.BoltMath.LifetimeFrames",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 3.5 s bolt stepped at 1/60 s is alive after 209 frames and expired after 211 (210 is the rounding boundary)
bool FSOLBoltMathLifetimeFramesTest::RunTest(const FString& /*parameters*/)
{
    double remaining = 3.5;
    for (int32 frame = 0; frame < 209; ++frame)
    {
        remaining = SOLBoltMath::AdvanceLifetime(remaining, BOLTMATH_FRAME_DT);
    }
    TestFalse(TEXT("Alive after 209 frames"), SOLBoltMath::IsExpired(remaining));

    for (int32 frame = 0; frame < 2; ++frame)
    {
        remaining = SOLBoltMath::AdvanceLifetime(remaining, BOLTMATH_FRAME_DT);
    }
    TestTrue(TEXT("Expired after 211 frames"), SOLBoltMath::IsExpired(remaining));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathSweptSphereBasicTest, "SOLTest.BoltMath.SweptSphereBasic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SweptSphereHit returns the earliest entry time along the segment and misses spheres off, behind or beyond it
bool FSOLBoltMathSweptSphereBasicTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d start(0.0, 0.0, 0.0);
    const FVector3d end(100.0, 0.0, 0.0);

    const SOLBoltMath::FSweepHit centred = SOLBoltMath::SweptSphereHit(start, end, FVector3d(50.0, 0.0, 0.0), 10.0);
    TestTrue(TEXT("Sphere on the segment is hit"), centred.bHit);
    TestNearlyEqual(TEXT("Earliest entry at t = 0.4"), centred.Time, 0.4, BOLTMATH_TIGHT_TOL);

    // Off-axis by 6 with radius 10: entry where (x-50)^2 + 36 = 100 -> x = 42
    const SOLBoltMath::FSweepHit offAxis = SOLBoltMath::SweptSphereHit(start, end, FVector3d(50.0, 6.0, 0.0), 10.0);
    TestTrue(TEXT("Off-axis sphere within radius is hit"), offAxis.bHit);
    TestNearlyEqual(TEXT("Off-axis entry at t = 0.42"), offAxis.Time, 0.42, BOLTMATH_TIGHT_TOL);

    TestFalse(TEXT("Sphere beside the segment (distance 11 > 10) is missed"),
        SOLBoltMath::SweptSphereHit(start, end, FVector3d(50.0, 11.0, 0.0), 10.0).bHit);
    TestFalse(TEXT("Sphere behind the start is missed"),
        SOLBoltMath::SweptSphereHit(start, end, FVector3d(-20.0, 0.0, 0.0), 10.0).bHit);
    TestFalse(TEXT("Sphere beyond the end is missed"),
        SOLBoltMath::SweptSphereHit(start, end, FVector3d(120.0, 0.0, 0.0), 10.0).bHit);

    // Segment ends exactly on the surface (inclusive) versus just short of it
    const SOLBoltMath::FSweepHit touch = SOLBoltMath::SweptSphereHit(start, FVector3d(40.0, 0.0, 0.0),
        FVector3d(50.0, 0.0, 0.0), 10.0);
    TestTrue(TEXT("Segment ending exactly on the surface hits"), touch.bHit);
    TestNearlyEqual(TEXT("Surface touch at t = 1"), touch.Time, 1.0, BOLTMATH_TIGHT_TOL);
    TestFalse(TEXT("Segment ending 0.1 m short misses"),
        SOLBoltMath::SweptSphereHit(start, FVector3d(39.9, 0.0, 0.0), FVector3d(50.0, 0.0, 0.0), 10.0).bHit);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathSweptSphereEdgeTest, "SOLTest.BoltMath.SweptSphereEdge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Start inside hits at t=0, a zero-length segment tests containment only, and bad radii or NaN never hit
bool FSOLBoltMathSweptSphereEdgeTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d centre(0.0, 0.0, 0.0);
    const SOLBoltMath::FSweepHit inside = SOLBoltMath::SweptSphereHit(FVector3d(1.0, 0.0, 0.0),
        FVector3d(100.0, 0.0, 0.0), centre, 5.0);
    TestTrue(TEXT("Start inside is a hit"), inside.bHit);
    TestEqual(TEXT("Start inside reports t = 0"), inside.Time, 0.0);

    const SOLBoltMath::FSweepHit pointIn = SOLBoltMath::SweptSphereHit(FVector3d(1.0, 1.0, 1.0),
        FVector3d(1.0, 1.0, 1.0), centre, 5.0);
    TestTrue(TEXT("Zero-length segment inside is a hit"), pointIn.bHit);
    TestEqual(TEXT("Zero-length inside reports t = 0"), pointIn.Time, 0.0);
    TestFalse(TEXT("Zero-length segment outside is a miss"),
        SOLBoltMath::SweptSphereHit(FVector3d(10.0, 0.0, 0.0), FVector3d(10.0, 0.0, 0.0), centre, 5.0).bHit);

    const FVector3d start(-100.0, 0.0, 0.0);
    const FVector3d end(100.0, 0.0, 0.0);
    TestFalse(TEXT("Zero radius never hits"), SOLBoltMath::SweptSphereHit(start, end, centre, 0.0).bHit);
    TestFalse(TEXT("Negative radius never hits"), SOLBoltMath::SweptSphereHit(start, end, centre, -5.0).bHit);
    TestFalse(TEXT("NaN radius never hits"), SOLBoltMath::SweptSphereHit(start, end, centre, BoltMathNaN()).bHit);
    TestFalse(TEXT("NaN centre never hits"),
        SOLBoltMath::SweptSphereHit(start, end, FVector3d(BoltMathNaN(), 0.0, 0.0), 5.0).bHit);
    TestFalse(TEXT("NaN segment start never hits"),
        SOLBoltMath::SweptSphereHit(FVector3d(BoltMathNaN(), 0.0, 0.0), end, centre, 5.0).bHit);
    TestFalse(TEXT("NaN segment end never hits"),
        SOLBoltMath::SweptSphereHit(start, FVector3d(0.0, BoltMathNaN(), 0.0), centre, 5.0).bHit);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathSweptSphereTunnellingTest, "SOLTest.BoltMath.SweptSphereTunnelling",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 1.5 km/s bolt cannot tunnel through a small sphere, either with a 25 m (1/60 s) step or a 1500 m (1 s) step
bool FSOLBoltMathSweptSphereTunnellingTest::RunTest(const FString& /*parameters*/)
{
    // 25 m segment straddling a 1 m sphere off-axis by 0.5 m: neither endpoint is inside it
    const FVector3d start(0.0, 0.0, 0.0);
    const FVector3d velocity(BOLTMATH_MUZZLE_SPEED, 0.0, 0.0);
    const FVector3d end = SOLBoltMath::AdvancePosition(start, velocity, BOLTMATH_FRAME_DT);
    const SOLBoltMath::FSweepHit small = SOLBoltMath::SweptSphereHit(start, end, FVector3d(12.5, 0.5, 0.0), 1.0);
    TestTrue(TEXT("25 m step hits a 1 m sphere mid-segment"), small.bHit);
    TestTrue(TEXT("25 m step hit time inside (0,1)"), small.Time > 0.0 && small.Time < 1.0);

    // A 1 s step (1500 m) through a 0.5 m sphere at 750 m
    const FVector3d bigEnd = SOLBoltMath::AdvancePosition(start, velocity, 1.0);
    const SOLBoltMath::FSweepHit big = SOLBoltMath::SweptSphereHit(start, bigEnd, FVector3d(750.0, 0.0, 0.0), 0.5);
    TestTrue(TEXT("1500 m step hits a 0.5 m sphere"), big.bHit);
    TestNearlyEqual(TEXT("1500 m step entry at (750-0.5)/1500"), big.Time, 749.5 / 1500.0, BOLTMATH_TIGHT_TOL);

    // The same at universe-scale coordinates (doubles keep the geometry intact)
    const FVector3d farStart(1.0e12, 2.0e11, -3.0e11);
    const FVector3d farEnd = farStart + FVector3d(25.0, 0.0, 0.0);
    const SOLBoltMath::FSweepHit far = SOLBoltMath::SweptSphereHit(farStart, farEnd,
        farStart + FVector3d(12.5, 0.5, 0.0), 1.0);
    TestTrue(TEXT("25 m step at 1e12 m hits a 1 m sphere"), far.bHit);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathSweptSphereMovingTest, "SOLTest.BoltMath.SweptSphereMoving",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SweptSphereHitMoving tests relative motion: a head-on closing sphere is hit earlier, a co-moving one is not reached
bool FSOLBoltMathSweptSphereMovingTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d start(0.0, 0.0, 0.0);
    const FVector3d end(100.0, 0.0, 0.0);

    // Head-on: relative position 200t - 100, within 10 at t = 0.45
    const SOLBoltMath::FSweepHit headOn = SOLBoltMath::SweptSphereHitMoving(start, end, FVector3d(100.0, 0.0, 0.0),
        FVector3d(0.0, 0.0, 0.0), 10.0);
    TestTrue(TEXT("Head-on closing sphere is hit"), headOn.bHit);
    TestNearlyEqual(TEXT("Head-on entry at t = 0.45"), headOn.Time, 0.45, BOLTMATH_TIGHT_TOL);

    // Sphere moving away at the bolt's speed stays 50 m ahead: a static test at its start position would hit, this must not
    TestFalse(TEXT("Co-moving sphere 50 m ahead is never reached"),
        SOLBoltMath::SweptSphereHitMoving(start, end, FVector3d(50.0, 0.0, 0.0), FVector3d(150.0, 0.0, 0.0), 10.0).bHit);

    // Stationary sphere: same answer as SweptSphereHit
    const SOLBoltMath::FSweepHit still = SOLBoltMath::SweptSphereHitMoving(start, end, FVector3d(50.0, 0.0, 0.0),
        FVector3d(50.0, 0.0, 0.0), 10.0);
    TestTrue(TEXT("Stationary moving-variant hits"), still.bHit);
    TestNearlyEqual(TEXT("Stationary moving-variant matches static t = 0.4"), still.Time, 0.4, BOLTMATH_TIGHT_TOL);

    // Fast crossing target: target sweeps across the bolt's path within one frame (both ~25 m moves, 1 m radius)
    const FVector3d boltEnd(25.0, 0.0, 0.0);
    const SOLBoltMath::FSweepHit crossing = SOLBoltMath::SweptSphereHitMoving(start, boltEnd,
        FVector3d(12.5, -12.5, 0.0), FVector3d(12.5, 12.5, 0.0), 1.0);
    TestTrue(TEXT("Perpendicular crossing at the same instant is hit"), crossing.bHit);
    TestTrue(TEXT("Crossing hit time inside (0,1)"), crossing.Time > 0.0 && crossing.Time < 1.0);

    // Invalid radius and NaN never hit
    TestFalse(TEXT("Moving: zero radius never hits"), SOLBoltMath::SweptSphereHitMoving(start, end,
        FVector3d(100.0, 0.0, 0.0), FVector3d::ZeroVector, 0.0).bHit);
    TestFalse(TEXT("Moving: NaN centre never hits"), SOLBoltMath::SweptSphereHitMoving(start, end,
        FVector3d(BoltMathNaN(), 0.0, 0.0), FVector3d::ZeroVector, 10.0).bHit);

    // Start overlapping: t = 0
    const SOLBoltMath::FSweepHit overlap = SOLBoltMath::SweptSphereHitMoving(start, end, FVector3d(1.0, 0.0, 0.0),
        FVector3d(200.0, 0.0, 0.0), 5.0);
    TestTrue(TEXT("Moving: start inside is a hit"), overlap.bHit);
    TestEqual(TEXT("Moving: start inside reports t = 0"), overlap.Time, 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBoltMathDeterminismTest, "SOLTest.BoltMath.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Identical inputs produce bit-identical outputs across repeated calls
bool FSOLBoltMathDeterminismTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d a = SOLBoltMath::ConvergedAimDirection(FVector3d(1.3, -0.7, 0.2), FVector3d(0.1, 0.2, 0.3),
        FVector3d(0.3, 0.9, -0.1), 1234.5);
    const FVector3d b = SOLBoltMath::ConvergedAimDirection(FVector3d(1.3, -0.7, 0.2), FVector3d(0.1, 0.2, 0.3),
        FVector3d(0.3, 0.9, -0.1), 1234.5);
    TestTrue(TEXT("ConvergedAimDirection repeats exactly"), a.X == b.X && a.Y == b.Y && a.Z == b.Z);

    const SOLBoltMath::FSweepHit h1 = SOLBoltMath::SweptSphereHitMoving(FVector3d(0.1, 0.2, 0.3),
        FVector3d(25.1, 3.2, -1.3), FVector3d(13.0, 1.0, 0.0), FVector3d(12.0, 2.0, -1.0), 2.5);
    const SOLBoltMath::FSweepHit h2 = SOLBoltMath::SweptSphereHitMoving(FVector3d(0.1, 0.2, 0.3),
        FVector3d(25.1, 3.2, -1.3), FVector3d(13.0, 1.0, 0.0), FVector3d(12.0, 2.0, -1.0), 2.5);
    TestEqual(TEXT("Sweep hit flag repeats"), h1.bHit, h2.bHit);
    TestEqual(TEXT("Sweep hit time repeats exactly"), h1.Time, h2.Time);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
