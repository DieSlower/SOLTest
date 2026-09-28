/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLTargeting.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Ship position and an oblique forward direction shared by the reticle tests
    const FVector3d TARGETING_SHIP_M(1000.0, 2000.0, 3000.0);
    const FVector3d TARGETING_FORWARD = FVector3d(1.0, 1.0, 0.0).GetSafeNormal();

    // A unit vector perpendicular to TARGETING_FORWARD, used to build off-axis directions
    const FVector3d TARGETING_SIDE = FVector3d(0.0, 0.0, 1.0);

    //////////////////////////////////////////////////////////////////////////
    // Returns a candidate at the given angle off the forward axis and distance from the ship, with a radius
    FSOLTargetInfo TargetingMakeOffAxis(const TCHAR* name, const double offsetRad, const double distanceM,
        const double radiusM)
    {
        const FVector3d direction = TARGETING_FORWARD * FMath::Cos(offsetRad) + TARGETING_SIDE * FMath::Sin(offsetRad);
        FSOLTargetInfo info;
        info.Name = FName(name);
        info.PositionM = TARGETING_SHIP_M + direction * distanceM;
        info.VelocityMps = FVector3d(1.0, 2.0, 3.0);
        info.RadiusM = radiusM;
        return info;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a point candidate at a given distance from the ship along an arbitrary direction
    FSOLTargetInfo TargetingMakeAtDistance(const TCHAR* name, const FVector3d& direction, const double distanceM)
    {
        FSOLTargetInfo info;
        info.Name = FName(name);
        info.PositionM = TARGETING_SHIP_M + direction.GetSafeNormal() * distanceM;
        return info;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a degrees value in radians (test-local, independent of the code under test)
    double TargetingDeg(const double degrees)
    {
        return degrees * UE_DOUBLE_PI / 180.0;
    }
}

// --- PickUnderReticle ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingPickNoneTest, "SOLTest.Targeting.PickNothingInCone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Nothing inside the cone, or no candidates at all, returns INDEX_NONE
bool FSOLTargetingPickNoneTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Default cone half angle is 10 degrees"), SOLTargeting::DefaultConeHalfAngleRad, TargetingDeg(10.0),
        1e-15);
    const FSOLTargetInfo candidates[] =
    {
        TargetingMakeOffAxis(TEXT("A"), TargetingDeg(30.0), 1000.0, 0.0),
        TargetingMakeOffAxis(TEXT("B"), TargetingDeg(-15.0), 500.0, 10.0),
        TargetingMakeOffAxis(TEXT("Behind"), TargetingDeg(180.0), 100.0, 0.0),
    };
    TestEqual(TEXT("Nothing in the cone"), SOLTargeting::PickUnderReticle(candidates, TARGETING_SHIP_M,
        TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), INDEX_NONE);
    TestEqual(TEXT("No candidates"), SOLTargeting::PickUnderReticle(TConstArrayView<FSOLTargetInfo>(),
        TARGETING_SHIP_M, TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingPickAheadTest, "SOLTest.Targeting.PickDeadAheadAndSmallestOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A point dead ahead is picked, and among several in the cone the smallest angular offset wins regardless of distance
bool FSOLTargetingPickAheadTest::RunTest(const FString& /*parameters*/)
{
    const FSOLTargetInfo ahead[] =
    {
        TargetingMakeOffAxis(TEXT("Off"), TargetingDeg(40.0), 100.0, 0.0),
        TargetingMakeOffAxis(TEXT("Ahead"), 0.0, 5000.0, 0.0),
    };
    TestEqual(TEXT("Dead-ahead point picked"), SOLTargeting::PickUnderReticle(ahead, TARGETING_SHIP_M,
        TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 1);

    // Near at 5 degrees, far at 2 degrees, nearest at 8 degrees: the 2-degree one wins
    const FSOLTargetInfo several[] =
    {
        TargetingMakeOffAxis(TEXT("Near5"), TargetingDeg(5.0), 200.0, 0.0),
        TargetingMakeOffAxis(TEXT("Nearest8"), TargetingDeg(-8.0), 50.0, 0.0),
        TargetingMakeOffAxis(TEXT("Far2"), TargetingDeg(-2.0), 1.0e9, 0.0),
        TargetingMakeOffAxis(TEXT("Outside"), TargetingDeg(1.0) + UE_DOUBLE_PI, 10.0, 0.0),
    };
    TestEqual(TEXT("Smallest offset wins"), SOLTargeting::PickUnderReticle(several, TARGETING_SHIP_M,
        TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 2);

    // A narrower cone that excludes the 2-degree target picks nothing
    TestEqual(TEXT("Cone of 1 degree: nothing"), SOLTargeting::PickUnderReticle(several, TARGETING_SHIP_M,
        TARGETING_FORWARD, TargetingDeg(1.0)), INDEX_NONE);

    // A different forward direction picks a different target
    const FVector3d towardNear5 = (several[0].PositionM - TARGETING_SHIP_M).GetSafeNormal();
    TestEqual(TEXT("Aiming at Near5 picks it"), SOLTargeting::PickUnderReticle(several, TARGETING_SHIP_M, towardNear5,
        SOLTargeting::DefaultConeHalfAngleRad), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingPickLimbTest, "SOLTest.Targeting.PickLargeBodyByLimb",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A large body whose center is outside the cone but whose limb is inside is pickable; its offset is angle - asin(R/d)
bool FSOLTargetingPickLimbTest::RunTest(const FString& /*parameters*/)
{
    // Center 20 degrees off, angular radius 15 degrees -> offset 5 degrees
    const double distance = 1.0e6;
    const double radius = distance * FMath::Sin(TargetingDeg(15.0));
    const FSOLTargetInfo bodyOnly[] = { TargetingMakeOffAxis(TEXT("Planet"), TargetingDeg(20.0), distance, radius) };
    TestEqual(TEXT("Limb inside the cone: picked"), SOLTargeting::PickUnderReticle(bodyOnly, TARGETING_SHIP_M,
        TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 0);

    // A point at 7 degrees loses to the body's 5-degree offset; a point at 3 degrees beats it
    const FSOLTargetInfo withPoint7[] =
    {
        TargetingMakeOffAxis(TEXT("Point7"), TargetingDeg(-7.0), 100.0, 0.0),
        bodyOnly[0],
    };
    TestEqual(TEXT("Body (5 deg offset) beats point at 7 deg"), SOLTargeting::PickUnderReticle(withPoint7,
        TARGETING_SHIP_M, TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 1);
    const FSOLTargetInfo withPoint3[] =
    {
        bodyOnly[0],
        TargetingMakeOffAxis(TEXT("Point3"), TargetingDeg(-3.0), 1.0e7, 0.0),
    };
    TestEqual(TEXT("Point at 3 deg beats body (5 deg offset)"), SOLTargeting::PickUnderReticle(withPoint3,
        TARGETING_SHIP_M, TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 1);

    // A smaller body at the same center angle (angular radius 5 degrees -> offset 15) is not picked
    const FSOLTargetInfo smallBody[] = { TargetingMakeOffAxis(TEXT("Moon"), TargetingDeg(20.0), distance,
        distance * FMath::Sin(TargetingDeg(5.0))) };
    TestEqual(TEXT("Small body outside the cone"), SOLTargeting::PickUnderReticle(smallBody, TARGETING_SHIP_M,
        TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingPickTieTest, "SOLTest.Targeting.PickTieGoesToNearer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Equal angular offsets are broken by distance: the nearer candidate wins, whatever the array order
bool FSOLTargetingPickTieTest::RunTest(const FString& /*parameters*/)
{
    // Two points exactly dead ahead
    const FSOLTargetInfo points[] =
    {
        TargetingMakeOffAxis(TEXT("Far"), 0.0, 500.0, 0.0),
        TargetingMakeOffAxis(TEXT("Near"), 0.0, 200.0, 0.0),
    };
    TestEqual(TEXT("Dead-ahead tie: nearer (index 1)"), SOLTargeting::PickUnderReticle(points, TARGETING_SHIP_M,
        TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 1);
    const FSOLTargetInfo pointsSwapped[] = { points[1], points[0] };
    TestEqual(TEXT("Dead-ahead tie swapped: nearer (index 0)"), SOLTargeting::PickUnderReticle(pointsSwapped,
        TARGETING_SHIP_M, TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 0);

    // Two large bodies that both cover the reticle (offset clamped to 0)
    const FSOLTargetInfo bodies[] =
    {
        TargetingMakeOffAxis(TEXT("BigFar"), TargetingDeg(3.0), 1.0e8, 1.0e8 * FMath::Sin(TargetingDeg(20.0))),
        TargetingMakeOffAxis(TEXT("BigNear"), TargetingDeg(-4.0), 1.0e7, 1.0e7 * FMath::Sin(TargetingDeg(10.0))),
        TargetingMakeOffAxis(TEXT("Point"), TargetingDeg(0.5), 10.0, 0.0),
    };
    TestEqual(TEXT("Both bodies cover the reticle: nearer body (index 1)"), SOLTargeting::PickUnderReticle(bodies,
        TARGETING_SHIP_M, TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingPickInsideTest, "SOLTest.Targeting.PickIgnoresBodyContainingShip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A body whose radius contains the ship (distance <= radius) is ignored
bool FSOLTargetingPickInsideTest::RunTest(const FString& /*parameters*/)
{
    const FSOLTargetInfo containing = TargetingMakeOffAxis(TEXT("Around"), 0.0, 100.0, 1.0e4);
    const FSOLTargetInfo onlyContaining[] = { containing };
    TestEqual(TEXT("Only a containing body: nothing"), SOLTargeting::PickUnderReticle(onlyContaining,
        TARGETING_SHIP_M, TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), INDEX_NONE);

    // Distance exactly equal to the radius also counts as inside
    FSOLTargetInfo onSurface = TargetingMakeAtDistance(TEXT("Touching"), FVector3d(0.0, 0.0, 1.0), 500.0);
    onSurface.RadiusM = 500.0;
    const FSOLTargetInfo onlySurface[] = { onSurface };
    TestEqual(TEXT("Distance == radius: ignored"), SOLTargeting::PickUnderReticle(onlySurface, TARGETING_SHIP_M,
        FVector3d(0.0, 0.0, 1.0), SOLTargeting::DefaultConeHalfAngleRad), INDEX_NONE);

    // With another target in the cone, that target is picked instead
    const FSOLTargetInfo mixed[] = { containing, TargetingMakeOffAxis(TEXT("Point8"), TargetingDeg(8.0), 1000.0, 0.0) };
    TestEqual(TEXT("Containing body skipped, point picked"), SOLTargeting::PickUnderReticle(mixed, TARGETING_SHIP_M,
        TARGETING_FORWARD, SOLTargeting::DefaultConeHalfAngleRad), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingPickBoundaryTest, "SOLTest.Targeting.PickConeBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An offset at the half angle (within rounding) is picked; just beyond it is not
bool FSOLTargetingPickBoundaryTest::RunTest(const FString& /*parameters*/)
{
    const double half = SOLTargeting::DefaultConeHalfAngleRad;
    const double margin = 1.0e-7;

    const FSOLTargetInfo justInside[] = { TargetingMakeOffAxis(TEXT("In"), half - margin, 1000.0, 0.0) };
    TestEqual(TEXT("Just inside the half angle: picked"), SOLTargeting::PickUnderReticle(justInside, TARGETING_SHIP_M,
        TARGETING_FORWARD, half), 0);

    const FSOLTargetInfo justOutside[] = { TargetingMakeOffAxis(TEXT("Out"), half + margin, 1000.0, 0.0) };
    TestEqual(TEXT("Just beyond the half angle: not picked"), SOLTargeting::PickUnderReticle(justOutside,
        TARGETING_SHIP_M, TARGETING_FORWARD, half), INDEX_NONE);

    // Axis-aligned case where the angle is exactly representable: 45 degrees off +X in the XY plane, cone 45 degrees
    FSOLTargetInfo diagonal;
    diagonal.Name = FName(TEXT("Diag"));
    diagonal.PositionM = TARGETING_SHIP_M + FVector3d(100.0, 100.0, 0.0);
    const FSOLTargetInfo exact[] = { diagonal };
    TestEqual(TEXT("Offset equal to the half angle (45 deg): picked"), SOLTargeting::PickUnderReticle(exact,
        TARGETING_SHIP_M, FVector3d(1.0, 0.0, 0.0), UE_DOUBLE_PI / 4.0 + 1e-12), 0);

    // Limb exactly reaching the boundary: center at half + 5 deg, angular radius 5 deg (+ tiny margin)
    const double distance = 1.0e5;
    const FSOLTargetInfo limb[] = { TargetingMakeOffAxis(TEXT("Limb"), half + TargetingDeg(5.0), distance,
        distance * FMath::Sin(TargetingDeg(5.0) + margin)) };
    TestEqual(TEXT("Limb at the boundary: picked"), SOLTargeting::PickUnderReticle(limb, TARGETING_SHIP_M,
        TARGETING_FORWARD, half), 0);
    return true;
}

// --- Cycle ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingCycleOrderTest, "SOLTest.Targeting.CycleDistanceOrderAndWrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Cycling walks candidates nearest-first by center distance, wraps both ways and returns original indices
bool FSOLTargetingCycleOrderTest::RunTest(const FString& /*parameters*/)
{
    // Distances 300, 100, 500, 200 in array order -> nearest-first order is 1, 3, 0, 2
    const FSOLTargetInfo candidates[] =
    {
        TargetingMakeAtDistance(TEXT("D300"), FVector3d(1.0, 0.0, 0.0), 300.0),
        TargetingMakeAtDistance(TEXT("D100"), FVector3d(0.0, -1.0, 0.0), 100.0),
        TargetingMakeAtDistance(TEXT("D500"), FVector3d(0.0, 0.0, 1.0), 500.0),
        TargetingMakeAtDistance(TEXT("D200"), FVector3d(-1.0, 1.0, 1.0), 200.0),
    };
    const int32 order[] = { 1, 3, 0, 2 };

    TestEqual(TEXT("+1 from none: nearest"), SOLTargeting::Cycle(candidates, TARGETING_SHIP_M, INDEX_NONE, 1), 1);
    TestEqual(TEXT("-1 from none: farthest"), SOLTargeting::Cycle(candidates, TARGETING_SHIP_M, INDEX_NONE, -1), 2);

    // Forward walk wraps from the farthest to the nearest; backward walk wraps from the nearest to the farthest
    const int32 count = static_cast<int32>(UE_ARRAY_COUNT(order));
    for (int32 position = 0; position < count; ++position)
    {
        const int32 current = order[position];
        const int32 next = order[(position + 1) % count];
        const int32 previous = order[(position + count - 1) % count];
        TestEqual(*FString::Printf(TEXT("+1 from %d"), current), SOLTargeting::Cycle(candidates, TARGETING_SHIP_M,
            current, 1), next);
        TestEqual(*FString::Printf(TEXT("-1 from %d"), current), SOLTargeting::Cycle(candidates, TARGETING_SHIP_M,
            current, -1), previous);
    }

    // A full forward loop visits every candidate once and returns to the start
    int32 index = INDEX_NONE;
    TArray<int32> visited;
    for (int32 step = 0; step < count; ++step)
    {
        index = SOLTargeting::Cycle(candidates, TARGETING_SHIP_M, index, 1);
        visited.Add(index);
    }
    TestTrue(TEXT("Forward loop visits 1, 3, 0, 2"), visited == TArray<int32>({ 1, 3, 0, 2 }));
    TestEqual(TEXT("Loop wraps back to the nearest"), SOLTargeting::Cycle(candidates, TARGETING_SHIP_M, index, 1), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingCycleCenterTest, "SOLTest.Targeting.CycleUsesCenterDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ordering uses center distance, not surface distance, and depends on the ship position
bool FSOLTargetingCycleCenterTest::RunTest(const FString& /*parameters*/)
{
    // Big body: center 1000 m, surface 100 m; small point at 500 m -> the point is nearer by center distance
    FSOLTargetInfo big = TargetingMakeAtDistance(TEXT("Big"), FVector3d(1.0, 0.0, 0.0), 1000.0);
    big.RadiusM = 900.0;
    const FSOLTargetInfo candidates[] = { big, TargetingMakeAtDistance(TEXT("Small"), FVector3d(-1.0, 0.0, 0.0), 500.0) };
    TestEqual(TEXT("Nearest by center distance is the point"), SOLTargeting::Cycle(candidates, TARGETING_SHIP_M,
        INDEX_NONE, 1), 1);

    // Moving the ship next to the big body flips the order
    const FVector3d movedShip = TARGETING_SHIP_M + FVector3d(900.0, 0.0, 0.0);
    TestEqual(TEXT("Moved ship: big body is nearest"), SOLTargeting::Cycle(candidates, movedShip, INDEX_NONE, 1), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingCycleTieTest, "SOLTest.Targeting.CycleStableForEqualDistances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Candidates at equal distance keep their array order in the cycle
bool FSOLTargetingCycleTieTest::RunTest(const FString& /*parameters*/)
{
    // Distances 100, 100, 50, 100 -> order 2, 0, 1, 3
    const FSOLTargetInfo candidates[] =
    {
        TargetingMakeAtDistance(TEXT("A100"), FVector3d(1.0, 0.0, 0.0), 100.0),
        TargetingMakeAtDistance(TEXT("B100"), FVector3d(0.0, 1.0, 0.0), 100.0),
        TargetingMakeAtDistance(TEXT("C50"), FVector3d(0.0, 0.0, 1.0), 50.0),
        TargetingMakeAtDistance(TEXT("D100"), FVector3d(-1.0, 0.0, 0.0), 100.0),
    };
    int32 index = INDEX_NONE;
    TArray<int32> forward;
    for (int32 step = 0; step < 5; ++step)
    {
        index = SOLTargeting::Cycle(candidates, TARGETING_SHIP_M, index, 1);
        forward.Add(index);
    }
    TestTrue(TEXT("Forward: 2, 0, 1, 3, 2"), forward == TArray<int32>({ 2, 0, 1, 3, 2 }));

    index = INDEX_NONE;
    TArray<int32> backward;
    for (int32 step = 0; step < 5; ++step)
    {
        index = SOLTargeting::Cycle(candidates, TARGETING_SHIP_M, index, -1);
        backward.Add(index);
    }
    TestTrue(TEXT("Backward: 3, 1, 0, 2, 3"), backward == TArray<int32>({ 3, 1, 0, 2, 3 }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingCycleEdgeTest, "SOLTest.Targeting.CycleEmptyAndSingle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Empty candidates give INDEX_NONE; a single candidate cycles to itself in both directions
bool FSOLTargetingCycleEdgeTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FSOLTargetInfo> empty;
    for (const int32 direction : { 1, -1 })
    {
        TestEqual(*FString::Printf(TEXT("Empty, direction %d, from none"), direction),
            SOLTargeting::Cycle(empty, TARGETING_SHIP_M, INDEX_NONE, direction), INDEX_NONE);
        TestEqual(*FString::Printf(TEXT("Empty, direction %d, from 0"), direction),
            SOLTargeting::Cycle(empty, TARGETING_SHIP_M, 0, direction), INDEX_NONE);
    }

    const FSOLTargetInfo single[] = { TargetingMakeAtDistance(TEXT("Only"), FVector3d(0.0, 1.0, 0.0), 42.0) };
    for (const int32 direction : { 1, -1 })
    {
        TestEqual(*FString::Printf(TEXT("Single, direction %d, from none"), direction),
            SOLTargeting::Cycle(single, TARGETING_SHIP_M, INDEX_NONE, direction), 0);
        TestEqual(*FString::Printf(TEXT("Single, direction %d, from itself"), direction),
            SOLTargeting::Cycle(single, TARGETING_SHIP_M, 0, direction), 0);
    }
    return true;
}

// --- ResolveReferenceVelocity ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetingReferenceTest, "SOLTest.Targeting.ResolveReferenceVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The locked target's velocity is used only when the lock is active and a target is given; otherwise the anchor's
bool FSOLTargetingReferenceTest::RunTest(const FString& /*parameters*/)
{
    FSOLTargetInfo target;
    target.Name = FName(TEXT("Mars"));
    target.VelocityMps = FVector3d(24000.0, -1000.0, 50.0);
    const FVector3d anchor(-29780.0, 300.0, -2.0);

    TestTrue(TEXT("Lock active with target: target velocity"),
        SOLTargeting::ResolveReferenceVelocity(true, &target, anchor) == target.VelocityMps);
    TestTrue(TEXT("Lock active without target: anchor velocity"),
        SOLTargeting::ResolveReferenceVelocity(true, nullptr, anchor) == anchor);
    TestTrue(TEXT("Lock inactive with target: anchor velocity"),
        SOLTargeting::ResolveReferenceVelocity(false, &target, anchor) == anchor);
    TestTrue(TEXT("Lock inactive without target: anchor velocity"),
        SOLTargeting::ResolveReferenceVelocity(false, nullptr, anchor) == anchor);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
