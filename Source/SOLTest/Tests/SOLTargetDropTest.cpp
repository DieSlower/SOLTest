/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLTargetDrop.h"

#include "Misc/AutomationTest.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Illustrative drop distance (m) and cooldown (s), typed independently of production code
    constexpr double TARGETDROP_DISTANCE_M = 200.0;
    constexpr double TARGETDROP_COOLDOWN_S = 0.5;

    // Tight tolerance for small arithmetic
    constexpr double TARGETDROP_TIGHT_TOL = 1.0e-9;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double TargetDropNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of a matches b within tolerance
    bool TargetDropVecNear(const FVector3d& a, const FVector3d& b, const double tolerance)
    {
        return std::abs(a.X - b.X) <= tolerance && std::abs(a.Y - b.Y) <= tolerance && std::abs(a.Z - b.Z) <= tolerance;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetDropPositionTest, "SOLTest.TargetDrop.Position",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DropPosition places the target distanceM along the normalised ship forward
bool FSOLTargetDropPositionTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d ship(1.0, 2.0, 3.0);
    TestTrue(TEXT("Unnormalised forward (0,0,5) puts the drop 200 m up +Z"),
        TargetDropVecNear(SOLTargetDrop::DropPosition(ship, FVector3d(0.0, 0.0, 5.0), TARGETDROP_DISTANCE_M),
            FVector3d(1.0, 2.0, 203.0), TARGETDROP_TIGHT_TOL));
    TestTrue(TEXT("Forward (3,-4,0) at 50 m gives (+30,-40,0)"),
        TargetDropVecNear(SOLTargetDrop::DropPosition(ship, FVector3d(3.0, -4.0, 0.0), 50.0),
            FVector3d(31.0, -38.0, 3.0), TARGETDROP_TIGHT_TOL));
    TestTrue(TEXT("Zero distance gives the ship position"),
        TargetDropVecNear(SOLTargetDrop::DropPosition(ship, FVector3d(1.0, 0.0, 0.0), 0.0), ship, 0.0));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetDropPositionDegenerateTest, "SOLTest.TargetDrop.PositionDegenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or NaN forward falls back to the (1,0,0) direction rather than NaN or the ship position
bool FSOLTargetDropPositionDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d ship(1.0, 2.0, 3.0);
    const FVector3d expected(201.0, 2.0, 3.0);
    TestTrue(TEXT("Zero forward uses (1,0,0)"),
        TargetDropVecNear(SOLTargetDrop::DropPosition(ship, FVector3d::ZeroVector, TARGETDROP_DISTANCE_M), expected,
            TARGETDROP_TIGHT_TOL));
    TestTrue(TEXT("NaN forward uses (1,0,0)"),
        TargetDropVecNear(SOLTargetDrop::DropPosition(ship, FVector3d(TargetDropNaN(), 1.0, 0.0),
            TARGETDROP_DISTANCE_M), expected, TARGETDROP_TIGHT_TOL));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetDropPositionHugeTest, "SOLTest.TargetDrop.PositionHuge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At universe scale (1e12 m) the drop offset survives in double precision
bool FSOLTargetDropPositionHugeTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d ship(1.0e12, -2.0e11, 7.0e11);
    const FVector3d drop = SOLTargetDrop::DropPosition(ship, FVector3d(0.0, 1.0, 0.0), TARGETDROP_DISTANCE_M);
    TestNearlyEqual(TEXT("X unchanged"), drop.X, ship.X, 0.0);
    TestNearlyEqual(TEXT("Y offset is 200 m"), drop.Y - ship.Y, TARGETDROP_DISTANCE_M, 1.0e-3);
    TestNearlyEqual(TEXT("Z unchanged"), drop.Z, ship.Z, 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetDropEvictionUnderCapTest, "SOLTest.TargetDrop.EvictionUnderCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Below the cap nothing is evicted; an empty pool never evicts, whatever the cap
bool FSOLTargetDropEvictionUnderCapTest::RunTest(const FString& /*parameters*/)
{
    const TArray<double> two = { 5.0, 1.0 };
    TestEqual(TEXT("2 of 3: no eviction"), SOLTargetDrop::EvictionIndex(two, 3), INDEX_NONE);

    const TArray<double> none;
    TestEqual(TEXT("Empty, cap 3: no eviction"), SOLTargetDrop::EvictionIndex(none, 3), INDEX_NONE);
    TestEqual(TEXT("Empty, cap 0: no eviction"), SOLTargetDrop::EvictionIndex(none, 0), INDEX_NONE);
    TestEqual(TEXT("Empty, cap -1: no eviction"), SOLTargetDrop::EvictionIndex(none, -1), INDEX_NONE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetDropEvictionOldestTest, "SOLTest.TargetDrop.EvictionOldest",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At or over the cap the oldest (smallest spawn time) is evicted, ties going to the lowest index
bool FSOLTargetDropEvictionOldestTest::RunTest(const FString& /*parameters*/)
{
    const TArray<double> full = { 5.0, 1.0, 3.0 };
    TestEqual(TEXT("3 of 3: oldest is index 1"), SOLTargetDrop::EvictionIndex(full, 3), 1);

    const TArray<double> over = { 9.0, 8.0, 7.0, 6.0, 10.0 };
    TestEqual(TEXT("5 over cap 3: oldest is index 3"), SOLTargetDrop::EvictionIndex(over, 3), 3);

    const TArray<double> ties = { 4.0, 2.0, 7.0, 2.0 };
    TestEqual(TEXT("Tie at 2.0: lowest index 1"), SOLTargetDrop::EvictionIndex(ties, 4), 1);

    const TArray<double> allSame = { 3.0, 3.0, 3.0 };
    TestEqual(TEXT("All equal: index 0"), SOLTargetDrop::EvictionIndex(allSame, 3), 0);

    const TArray<double> single = { 42.0 };
    TestEqual(TEXT("Cap 1 with one target: index 0"), SOLTargetDrop::EvictionIndex(single, 1), 0);

    const TArray<double> negativeTimes = { -1.0, -5.0, 0.0 };
    TestEqual(TEXT("Negative spawn times: smallest is index 1"), SOLTargetDrop::EvictionIndex(negativeTimes, 3), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetDropCooldownTest, "SOLTest.TargetDrop.Cooldown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ShouldAllowDrop is true once the time since the last drop reaches the cooldown (inclusive), false for NaN
bool FSOLTargetDropCooldownTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("Exactly at cooldown allows"), SOLTargetDrop::ShouldAllowDrop(TARGETDROP_COOLDOWN_S,
        TARGETDROP_COOLDOWN_S));
    TestTrue(TEXT("Past cooldown allows"), SOLTargetDrop::ShouldAllowDrop(1.0, TARGETDROP_COOLDOWN_S));
    TestFalse(TEXT("Just before cooldown blocks"), SOLTargetDrop::ShouldAllowDrop(0.49, TARGETDROP_COOLDOWN_S));
    TestFalse(TEXT("Zero elapsed blocks"), SOLTargetDrop::ShouldAllowDrop(0.0, TARGETDROP_COOLDOWN_S));
    TestTrue(TEXT("Zero cooldown always allows"), SOLTargetDrop::ShouldAllowDrop(0.0, 0.0));
    TestTrue(TEXT("Infinite elapsed (never dropped) allows"),
        SOLTargetDrop::ShouldAllowDrop(std::numeric_limits<double>::infinity(), TARGETDROP_COOLDOWN_S));
    TestFalse(TEXT("NaN elapsed blocks"), SOLTargetDrop::ShouldAllowDrop(TargetDropNaN(), TARGETDROP_COOLDOWN_S));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLTargetDropDeterminismTest, "SOLTest.TargetDrop.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Identical inputs give bit-identical drop positions and eviction choices
bool FSOLTargetDropDeterminismTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d ship(123.456, -789.012, 3.5e9);
    const FVector3d forward(0.3, -0.8, 0.1);
    const FVector3d a = SOLTargetDrop::DropPosition(ship, forward, 321.0);
    const FVector3d b = SOLTargetDrop::DropPosition(ship, forward, 321.0);
    TestTrue(TEXT("DropPosition repeats exactly"), a.X == b.X && a.Y == b.Y && a.Z == b.Z);
    TestNearlyEqual(TEXT("Drop distance is 321 m"), (a - ship).Size(), 321.0, 1.0e-5);

    const TArray<double> times = { 0.7, 0.2, 0.9, 0.2, 0.1, 0.5 };
    TestEqual(TEXT("EvictionIndex repeats"), SOLTargetDrop::EvictionIndex(times, 6),
        SOLTargetDrop::EvictionIndex(times, 6));
    TestEqual(TEXT("EvictionIndex picks index 4"), SOLTargetDrop::EvictionIndex(times, 6), 4);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
