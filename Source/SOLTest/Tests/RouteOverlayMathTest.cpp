/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRouteOverlayMath.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #20 route overlay math (Docs/SDDs/20-hologram-map-and-move-orders.md section 2.4), written
// against the Map/SOLRouteOverlayMath.h contract before the production code existed
namespace
{
    constexpr double ROM_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double ROM_INF = std::numeric_limits<double>::infinity();
    constexpr double ROM_TOL = 1e-9;

    const ESOLRouteKind ROM_KINDS[] = { ESOLRouteKind::Move, ESOLRouteKind::PatrolLoop,
        ESOLRouteKind::PatrolPingPong };

    //////////////////////////////////////////////////////////////////////////
    // Returns a cursor with the given target, direction and reached count
    FSOLRouteCursor RouteOverlayMakeCursor(const int32 target, const int32 direction, const int32 legsCompleted,
        const bool bFinished = false)
    {
        FSOLRouteCursor cursor;
        cursor.TargetIndex = target;
        cursor.Direction = direction;
        cursor.LegsCompleted = legsCompleted;
        cursor.bFinished = bFinished;
        return cursor;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that GetLeg returns the expected leg
    void RouteOverlayExpectLeg(FAutomationTestBase& test, const FString& what, const ESOLRouteKind kind,
        const int32 count, const int32 legIndex, const int32 from, const int32 to, const bool bClosing)
    {
        FSOLRouteLeg leg;
        test.TestTrue(what + TEXT(" returns true"), SOLRouteOverlayMath::GetLeg(kind, count, legIndex, leg));
        test.TestEqual(what + TEXT(" from"), leg.FromIndex, from);
        test.TestEqual(what + TEXT(" to"), leg.ToIndex, to);
        test.TestEqual(what + TEXT(" closing"), leg.bClosing, bClosing);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that GetLeg rejects a leg index and leaves the output untouched
    void RouteOverlayExpectNoLeg(FAutomationTestBase& test, const FString& what, const ESOLRouteKind kind,
        const int32 count, const int32 legIndex)
    {
        FSOLRouteLeg leg;
        leg.FromIndex = 77;
        leg.ToIndex = 88;
        leg.bClosing = true;
        test.TestFalse(what + TEXT(" returns false"), SOLRouteOverlayMath::GetLeg(kind, count, legIndex, leg));
        test.TestTrue(what + TEXT(" leaves out untouched"), leg.FromIndex == 77 && leg.ToIndex == 88 && leg.bClosing);
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks LegAtPriority's full order for one legCount and focus
    void RouteOverlayExpectOrder(FAutomationTestBase& test, const int32 legCount, const int32 focus,
        const TArray<int32>& expected)
    {
        for (int32 rank = 0; rank < expected.Num(); ++rank)
        {
            test.TestEqual(FString::Printf(TEXT("legCount %d focus %d rank %d"), legCount, focus, rank),
                SOLRouteOverlayMath::LegAtPriority(legCount, focus, rank), expected[rank]);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a clip result's success and kept parameter range
    void RouteOverlayExpectRange(FAutomationTestBase& test, const FString& what, const bool bResult,
        const double t0, const double t1, const double expectedT0, const double expectedT1)
    {
        test.TestTrue(what + TEXT(" kept"), bResult);
        test.TestEqual(what + TEXT(" t0"), t0, expectedT0, ROM_TOL);
        test.TestEqual(what + TEXT(" t1"), t1, expectedT1, ROM_TOL);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs ComputeDashes into a 64-slot buffer and checks the exact dashes written
    void RouteOverlayExpectDashes(FAutomationTestBase& test, const FString& what, const double lengthPx,
        const double dashPx, const double gapPx, const double phasePx, const TArray<FVector2D>& expected)
    {
        FVector2D buffer[64];
        const int32 count = SOLRouteOverlayMath::ComputeDashes(lengthPx, dashPx, gapPx, phasePx,
            TArrayView<FVector2D>(buffer, 64));
        test.TestEqual(what + TEXT(" count"), count, expected.Num());
        for (int32 index = 0; index < FMath::Min(count, expected.Num()); ++index)
        {
            test.TestEqual(FString::Printf(TEXT("%s dash %d t0"), *what, index), buffer[index].X, expected[index].X,
                ROM_TOL);
            test.TestEqual(FString::Printf(TEXT("%s dash %d t1"), *what, index), buffer[index].Y, expected[index].Y,
                ROM_TOL);
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayLegCountTest, "SOLTest.RouteOverlayMath.LegCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Move and Ping-pong draw N - 1 legs, a Loop N (two or more waypoints); 0 below two waypoints or for a negative count
bool FSOLRouteOverlayLegCountTest::RunTest(const FString& /*parameters*/)
{
    const int32 counts[] = { -1, 0, 1, 2, 3, 100 };
    const int32 openExpected[] = { 0, 0, 0, 1, 2, 99 };
    const int32 loopExpected[] = { 0, 0, 0, 2, 3, 100 };
    for (int32 index = 0; index < UE_ARRAY_COUNT(counts); ++index)
    {
        const int32 count = counts[index];
        TestEqual(FString::Printf(TEXT("Move %d"), count),
            SOLRouteOverlayMath::LegCount(ESOLRouteKind::Move, count), openExpected[index]);
        TestEqual(FString::Printf(TEXT("PingPong %d"), count),
            SOLRouteOverlayMath::LegCount(ESOLRouteKind::PatrolPingPong, count), openExpected[index]);
        TestEqual(FString::Printf(TEXT("Loop %d"), count),
            SOLRouteOverlayMath::LegCount(ESOLRouteKind::PatrolLoop, count), loopExpected[index]);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayGetLegTest, "SOLTest.RouteOverlayMath.GetLeg",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Legs run i -> i + 1; a Loop's last leg closes back to 0; out-of-range indices are rejected without touching out
bool FSOLRouteOverlayGetLegTest::RunTest(const FString& /*parameters*/)
{
    RouteOverlayExpectLeg(*this, TEXT("Move leg 0"), ESOLRouteKind::Move, 3, 0, 0, 1, false);
    RouteOverlayExpectLeg(*this, TEXT("Move leg 1"), ESOLRouteKind::Move, 3, 1, 1, 2, false);
    RouteOverlayExpectNoLeg(*this, TEXT("Move leg 2"), ESOLRouteKind::Move, 3, 2);
    RouteOverlayExpectLeg(*this, TEXT("PingPong leg 1"), ESOLRouteKind::PatrolPingPong, 3, 1, 1, 2, false);
    RouteOverlayExpectNoLeg(*this, TEXT("PingPong leg 2"), ESOLRouteKind::PatrolPingPong, 3, 2);
    RouteOverlayExpectLeg(*this, TEXT("Loop leg 0"), ESOLRouteKind::PatrolLoop, 3, 0, 0, 1, false);
    RouteOverlayExpectLeg(*this, TEXT("Loop leg 1"), ESOLRouteKind::PatrolLoop, 3, 1, 1, 2, false);
    RouteOverlayExpectLeg(*this, TEXT("Loop closing leg"), ESOLRouteKind::PatrolLoop, 3, 2, 2, 0, true);
    RouteOverlayExpectLeg(*this, TEXT("Loop of two closing leg"), ESOLRouteKind::PatrolLoop, 2, 1, 1, 0, true);
    RouteOverlayExpectNoLeg(*this, TEXT("Loop leg 3"), ESOLRouteKind::PatrolLoop, 3, 3);
    for (const ESOLRouteKind kind : ROM_KINDS)
    {
        const int32 kindIndex = static_cast<int32>(kind);
        RouteOverlayExpectNoLeg(*this, FString::Printf(TEXT("kind %d negative leg"), kindIndex), kind, 3, -1);
        RouteOverlayExpectNoLeg(*this, FString::Printf(TEXT("kind %d one waypoint"), kindIndex), kind, 1, 0);
        RouteOverlayExpectNoLeg(*this, FString::Printf(TEXT("kind %d no waypoints"), kindIndex), kind, 0, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayActiveLegTest, "SOLTest.RouteOverlayMath.ActiveLegIndex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The active leg ends at the cursor's target in the direction of travel; none at the start, when finished or invalid
bool FSOLRouteOverlayActiveLegTest::RunTest(const FString& /*parameters*/)
{
    using SOLRouteOverlayMath::ActiveLegIndex;

    // Move: flying to waypoint 0 from the ship has no drawn leg; target t is leg t - 1; finished has none
    TestEqual(TEXT("Move start"), ActiveLegIndex(ESOLRouteKind::Move, 4, RouteOverlayMakeCursor(0, 1, 0)),
        INDEX_NONE);
    TestEqual(TEXT("Move target 1"), ActiveLegIndex(ESOLRouteKind::Move, 4, RouteOverlayMakeCursor(1, 1, 1)), 0);
    TestEqual(TEXT("Move target 3"), ActiveLegIndex(ESOLRouteKind::Move, 4, RouteOverlayMakeCursor(3, 1, 3)), 2);
    TestEqual(TEXT("Move finished"), ActiveLegIndex(ESOLRouteKind::Move, 4, RouteOverlayMakeCursor(3, 1, 4, true)),
        INDEX_NONE);

    // Loop: back to 0 after reaching waypoints is the closing leg; the very first target 0 is not
    TestEqual(TEXT("Loop start"), ActiveLegIndex(ESOLRouteKind::PatrolLoop, 3, RouteOverlayMakeCursor(0, 1, 0)),
        INDEX_NONE);
    TestEqual(TEXT("Loop target 2"), ActiveLegIndex(ESOLRouteKind::PatrolLoop, 3, RouteOverlayMakeCursor(2, 1, 2)), 1);
    TestEqual(TEXT("Loop wrap to 0"), ActiveLegIndex(ESOLRouteKind::PatrolLoop, 3, RouteOverlayMakeCursor(0, 1, 3)),
        2);
    TestEqual(TEXT("Loop second lap target 1"),
        ActiveLegIndex(ESOLRouteKind::PatrolLoop, 3, RouteOverlayMakeCursor(1, 1, 4)), 0);

    // Ping-pong: forward target t is leg t - 1, backward target t is leg t (from t + 1 down to t), both ends
    TestEqual(TEXT("PingPong start"),
        ActiveLegIndex(ESOLRouteKind::PatrolPingPong, 3, RouteOverlayMakeCursor(0, 1, 0)), INDEX_NONE);
    TestEqual(TEXT("PingPong forward to far end"),
        ActiveLegIndex(ESOLRouteKind::PatrolPingPong, 3, RouteOverlayMakeCursor(2, 1, 2)), 1);
    TestEqual(TEXT("PingPong backward to middle"),
        ActiveLegIndex(ESOLRouteKind::PatrolPingPong, 3, RouteOverlayMakeCursor(1, -1, 3)), 1);
    TestEqual(TEXT("PingPong backward to near end"),
        ActiveLegIndex(ESOLRouteKind::PatrolPingPong, 3, RouteOverlayMakeCursor(0, -1, 4)), 0);
    TestEqual(TEXT("PingPong forward again"),
        ActiveLegIndex(ESOLRouteKind::PatrolPingPong, 3, RouteOverlayMakeCursor(1, 1, 5)), 0);

    // Invalid: out-of-range targets, fewer than two waypoints
    for (const ESOLRouteKind kind : ROM_KINDS)
    {
        const int32 kindIndex = static_cast<int32>(kind);
        TestEqual(FString::Printf(TEXT("kind %d target past end"), kindIndex),
            ActiveLegIndex(kind, 3, RouteOverlayMakeCursor(3, 1, 3)), INDEX_NONE);
        TestEqual(FString::Printf(TEXT("kind %d negative target"), kindIndex),
            ActiveLegIndex(kind, 3, RouteOverlayMakeCursor(-1, 1, 3)), INDEX_NONE);
        TestEqual(FString::Printf(TEXT("kind %d one waypoint"), kindIndex),
            ActiveLegIndex(kind, 1, RouteOverlayMakeCursor(0, 1, 1)), INDEX_NONE);
        TestEqual(FString::Printf(TEXT("kind %d no waypoints"), kindIndex),
            ActiveLegIndex(kind, 0, RouteOverlayMakeCursor(0, 1, 0)), INDEX_NONE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayPriorityTest, "SOLTest.RouteOverlayMath.LegAtPriority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Priority alternates outward from the focus leg, is a permutation of all legs, and clamps the focus
bool FSOLRouteOverlayPriorityTest::RunTest(const FString& /*parameters*/)
{
    RouteOverlayExpectOrder(*this, 5, 2, { 2, 3, 1, 4, 0 });
    RouteOverlayExpectOrder(*this, 5, 0, { 0, 1, 2, 3, 4 });
    RouteOverlayExpectOrder(*this, 5, 4, { 4, 3, 2, 1, 0 });
    RouteOverlayExpectOrder(*this, 6, 1, { 1, 2, 0, 3, 4, 5 });
    RouteOverlayExpectOrder(*this, 1, 0, { 0 });
    RouteOverlayExpectOrder(*this, 5, -3, { 0, 1, 2, 3, 4 });
    RouteOverlayExpectOrder(*this, 5, 99, { 4, 3, 2, 1, 0 });

    // Every leg exactly once over ranks 0 .. legCount - 1, for many counts and focuses (in and out of range)
    for (int32 legCount = 1; legCount <= 12; ++legCount)
    {
        for (int32 focus = -2; focus <= legCount + 1; ++focus)
        {
            TArray<int32> seen;
            seen.Init(0, legCount);
            bool bAllValid = true;
            for (int32 rank = 0; rank < legCount; ++rank)
            {
                const int32 leg = SOLRouteOverlayMath::LegAtPriority(legCount, focus, rank);
                if (leg < 0 || leg >= legCount)
                {
                    bAllValid = false;
                    continue;
                }
                ++seen[leg];
            }
            bool bEachOnce = bAllValid;
            for (const int32 hits : seen)
            {
                bEachOnce = bEachOnce && hits == 1;
            }
            const int32 clampedFocus = FMath::Clamp(focus, 0, legCount - 1);
            TestTrue(FString::Printf(TEXT("permutation legCount %d focus %d"), legCount, focus), bEachOnce);
            TestEqual(FString::Printf(TEXT("rank 0 is focus, legCount %d focus %d"), legCount, focus),
                SOLRouteOverlayMath::LegAtPriority(legCount, focus, 0), clampedFocus);
        }
    }

    // Invalid rank or count
    TestEqual(TEXT("rank == legCount"), SOLRouteOverlayMath::LegAtPriority(5, 2, 5), INDEX_NONE);
    TestEqual(TEXT("negative rank"), SOLRouteOverlayMath::LegAtPriority(5, 2, -1), INDEX_NONE);
    TestEqual(TEXT("legCount 0"), SOLRouteOverlayMath::LegAtPriority(0, 0, 0), INDEX_NONE);
    TestEqual(TEXT("negative legCount"), SOLRouteOverlayMath::LegAtPriority(-4, 0, 0), INDEX_NONE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayNearClipTest, "SOLTest.RouteOverlayMath.ClipToNearPlane",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Keeps the part of a segment with X >= nearX, with the exact crossing parameter; rejects behind and non-finite
bool FSOLRouteOverlayNearClipTest::RunTest(const FString& /*parameters*/)
{
    double t0 = -5.0;
    double t1 = -5.0;
    bool bKept = SOLRouteOverlayMath::ClipToNearPlane(FVector(5, 1, 2), FVector(10, -3, 4), 1.0, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("both in front"), bKept, t0, t1, 0.0, 1.0);

    TestFalse(TEXT("both behind"),
        SOLRouteOverlayMath::ClipToNearPlane(FVector(-5, 1, 2), FVector(0.5, 3, 4), 1.0, t0, t1));

    bKept = SOLRouteOverlayMath::ClipToNearPlane(FVector(-1, 0, 0), FVector(3, 8, 0), 1.0, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("behind to front"), bKept, t0, t1, 0.5, 1.0);

    bKept = SOLRouteOverlayMath::ClipToNearPlane(FVector(3, 0, 0), FVector(-1, 0, 9), 1.0, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("front to behind"), bKept, t0, t1, 0.0, 0.5);

    bKept = SOLRouteOverlayMath::ClipToNearPlane(FVector(-9, 0, 0), FVector(1, 0, 0), 0.0, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("quarter crossing"), bKept, t0, t1, 0.9, 1.0);

    bKept = SOLRouteOverlayMath::ClipToNearPlane(FVector(1, 0, 0), FVector(-2, 0, 0), 1.0, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("start on plane, end behind"), bKept, t0, t1, 0.0, 0.0);

    bKept = SOLRouteOverlayMath::ClipToNearPlane(FVector(-2, 0, 0), FVector(1, 0, 0), 1.0, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("end on plane, start behind"), bKept, t0, t1, 1.0, 1.0);

    TestFalse(TEXT("NaN point"),
        SOLRouteOverlayMath::ClipToNearPlane(FVector(ROM_NAN, 0, 0), FVector(5, 0, 0), 1.0, t0, t1));
    TestFalse(TEXT("infinite point"),
        SOLRouteOverlayMath::ClipToNearPlane(FVector(5, 0, 0), FVector(ROM_INF, 0, 0), 1.0, t0, t1));
    TestFalse(TEXT("NaN nearX"),
        SOLRouteOverlayMath::ClipToNearPlane(FVector(5, 0, 0), FVector(6, 0, 0), ROM_NAN, t0, t1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayRectClipTest, "SOLTest.RouteOverlayMath.ClipToRect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Liang-Barsky clipping to the screen rectangle with exact parameters, degenerate segments and invalid input
bool FSOLRouteOverlayRectClipTest::RunTest(const FString& /*parameters*/)
{
    using SOLRouteOverlayMath::ClipToRect;
    const FVector2D rectMin(0.0, 0.0);
    const FVector2D rectMax(10.0, 10.0);
    double t0 = -5.0;
    double t1 = -5.0;

    bool bKept = ClipToRect(FVector2D(1, 2), FVector2D(9, 8), rectMin, rectMax, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("inside"), bKept, t0, t1, 0.0, 1.0);

    TestFalse(TEXT("outside left"), ClipToRect(FVector2D(-5, -5), FVector2D(-1, 20), rectMin, rectMax, t0, t1));
    TestFalse(TEXT("outside above"), ClipToRect(FVector2D(-5, 12), FVector2D(15, 11), rectMin, rectMax, t0, t1));

    bKept = ClipToRect(FVector2D(-5, 5), FVector2D(5, 5), rectMin, rectMax, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("enters left edge"), bKept, t0, t1, 0.5, 1.0);

    bKept = ClipToRect(FVector2D(5, 5), FVector2D(5, 25), rectMin, rectMax, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("leaves top edge"), bKept, t0, t1, 0.0, 0.25);

    bKept = ClipToRect(FVector2D(-5, 5), FVector2D(15, 5), rectMin, rectMax, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("crosses both sides"), bKept, t0, t1, 0.25, 0.75);

    bKept = ClipToRect(FVector2D(-2, 4), FVector2D(8, 14), rectMin, rectMax, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("crosses left then top"), bKept, t0, t1, 0.2, 0.6);

    bKept = ClipToRect(FVector2D(-5, 5), FVector2D(0, 5), rectMin, rectMax, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("end on border"), bKept, t0, t1, 1.0, 1.0);

    bKept = ClipToRect(FVector2D(3, 3), FVector2D(3, 3), rectMin, rectMax, t0, t1);
    RouteOverlayExpectRange(*this, TEXT("point inside"), bKept, t0, t1, 0.0, 1.0);
    TestFalse(TEXT("point outside"), ClipToRect(FVector2D(20, 3), FVector2D(20, 3), rectMin, rectMax, t0, t1));

    TestFalse(TEXT("empty rect"),
        ClipToRect(FVector2D(1, 1), FVector2D(2, 2), FVector2D(10, 10), FVector2D(0, 0), t0, t1));
    TestFalse(TEXT("NaN point"), ClipToRect(FVector2D(ROM_NAN, 1), FVector2D(2, 2), rectMin, rectMax, t0, t1));
    TestFalse(TEXT("NaN rect"),
        ClipToRect(FVector2D(1, 1), FVector2D(2, 2), rectMin, FVector2D(ROM_NAN, 10), t0, t1));
    TestFalse(TEXT("infinite point"), ClipToRect(FVector2D(1, 1), FVector2D(ROM_INF, 2), rectMin, rectMax, t0, t1));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayFadeTest, "SOLTest.RouteOverlayMath.DirectionalFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// exp(-rate (1 - t)): 1 at the end, dimmest at the start, clamped, monotonic, start value for non-finite t
bool FSOLRouteOverlayFadeTest::RunTest(const FString& /*parameters*/)
{
    using SOLRouteOverlayMath::DirectionalFade;
    const double startValue = FMath::Exp(-SOL::Route::OVERLAY_FADE_RATE);
    TestEqual(TEXT("t = 1"), DirectionalFade(1.0), 1.0, ROM_TOL);
    TestEqual(TEXT("t = 0"), DirectionalFade(0.0), startValue, ROM_TOL);
    TestEqual(TEXT("t = 0.5"), DirectionalFade(0.5), FMath::Exp(-SOL::Route::OVERLAY_FADE_RATE * 0.5), ROM_TOL);
    TestEqual(TEXT("t = 0.25"), DirectionalFade(0.25), FMath::Exp(-SOL::Route::OVERLAY_FADE_RATE * 0.75), ROM_TOL);
    TestEqual(TEXT("t above 1 clamps"), DirectionalFade(3.0), 1.0, ROM_TOL);
    TestEqual(TEXT("t below 0 clamps"), DirectionalFade(-2.0), startValue, ROM_TOL);
    TestEqual(TEXT("NaN gives start"), DirectionalFade(ROM_NAN), startValue, ROM_TOL);
    TestEqual(TEXT("+inf gives start"), DirectionalFade(ROM_INF), startValue, ROM_TOL);

    bool bMonotonic = true;
    double previous = DirectionalFade(0.0);
    for (int32 step = 1; step <= 100; ++step)
    {
        const double value = DirectionalFade(step / 100.0);
        bMonotonic = bMonotonic && value > previous;
        previous = value;
    }
    TestTrue(TEXT("strictly increasing along the leg"), bMonotonic);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlaySubdivisionTest, "SOLTest.RouteOverlayMath.SubdivisionCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ceil(length / step) clamped into [1, max]; 1 for any invalid input
bool FSOLRouteOverlaySubdivisionTest::RunTest(const FString& /*parameters*/)
{
    using SOLRouteOverlayMath::SubdivisionCount;
    TestEqual(TEXT("exact multiple"), SubdivisionCount(64.0, 32.0, 16), 2);
    TestEqual(TEXT("exact single"), SubdivisionCount(32.0, 32.0, 16), 1);
    TestEqual(TEXT("ceil"), SubdivisionCount(65.0, 32.0, 16), 3);
    TestEqual(TEXT("short leg"), SubdivisionCount(10.0, 32.0, 16), 1);
    TestEqual(TEXT("clamped to max"), SubdivisionCount(1000.0, 32.0, 16), 16);
    TestEqual(TEXT("exactly max"), SubdivisionCount(512.0, 32.0, 16), 16);
    TestEqual(TEXT("zero length"), SubdivisionCount(0.0, 32.0, 16), 1);
    TestEqual(TEXT("negative length"), SubdivisionCount(-50.0, 32.0, 16), 1);
    TestEqual(TEXT("NaN length"), SubdivisionCount(ROM_NAN, 32.0, 16), 1);
    TestEqual(TEXT("infinite length"), SubdivisionCount(ROM_INF, 32.0, 16), 1);
    TestEqual(TEXT("zero step"), SubdivisionCount(100.0, 0.0, 16), 1);
    TestEqual(TEXT("negative step"), SubdivisionCount(100.0, -32.0, 16), 1);
    TestEqual(TEXT("NaN step"), SubdivisionCount(100.0, ROM_NAN, 16), 1);
    TestEqual(TEXT("max zero"), SubdivisionCount(100.0, 32.0, 0), 1);
    TestEqual(TEXT("max negative"), SubdivisionCount(100.0, 32.0, -3), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayDashesTest, "SOLTest.RouteOverlayMath.ComputeDashes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Dash patterns: exact positions, phase shifts and wrapping, cut ends, output limit, zero gap and invalid input
bool FSOLRouteOverlayDashesTest::RunTest(const FString& /*parameters*/)
{
    // Pattern 10 on, 7 off (period 17)
    RouteOverlayExpectDashes(*this, TEXT("phase 0, length 34"), 34.0, 10.0, 7.0, 0.0,
        { FVector2D(0.0, 10.0 / 34.0), FVector2D(0.5, 27.0 / 34.0) });
    RouteOverlayExpectDashes(*this, TEXT("phase 0, last dash cut"), 20.0, 10.0, 7.0, 0.0,
        { FVector2D(0.0, 0.5), FVector2D(17.0 / 20.0, 1.0) });
    RouteOverlayExpectDashes(*this, TEXT("phase 5, partial first dash"), 30.0, 10.0, 7.0, 5.0,
        { FVector2D(0.0, 5.0 / 30.0), FVector2D(12.0 / 30.0, 22.0 / 30.0), FVector2D(29.0 / 30.0, 1.0) });
    RouteOverlayExpectDashes(*this, TEXT("phase 12, starts in a gap"), 30.0, 10.0, 7.0, 12.0,
        { FVector2D(5.0 / 30.0, 15.0 / 30.0), FVector2D(22.0 / 30.0, 1.0) });
    RouteOverlayExpectDashes(*this, TEXT("phase 10, no zero-length dash at start"), 30.0, 10.0, 7.0, 10.0,
        { FVector2D(7.0 / 30.0, 17.0 / 30.0), FVector2D(24.0 / 30.0, 1.0) });
    RouteOverlayExpectDashes(*this, TEXT("phase 22 wraps to 5"), 30.0, 10.0, 7.0, 22.0,
        { FVector2D(0.0, 5.0 / 30.0), FVector2D(12.0 / 30.0, 22.0 / 30.0), FVector2D(29.0 / 30.0, 1.0) });
    RouteOverlayExpectDashes(*this, TEXT("phase -12 wraps to 5"), 30.0, 10.0, 7.0, -12.0,
        { FVector2D(0.0, 5.0 / 30.0), FVector2D(12.0 / 30.0, 22.0 / 30.0), FVector2D(29.0 / 30.0, 1.0) });
    RouteOverlayExpectDashes(*this, TEXT("zero gap is one dash"), 50.0, 10.0, 0.0, 3.0, { FVector2D(0.0, 1.0) });

    // Output limit respected
    FVector2D small[2];
    TestEqual(TEXT("limited to 2"), SOLRouteOverlayMath::ComputeDashes(1000.0, 10.0, 7.0, 0.0,
        TArrayView<FVector2D>(small, 2)), 2);
    TestEqual(TEXT("limited first dash"), small[0].Y, 10.0 / 1000.0, ROM_TOL);
    TestEqual(TEXT("empty output"), SOLRouteOverlayMath::ComputeDashes(1000.0, 10.0, 7.0, 0.0,
        TArrayView<FVector2D>()), 0);

    // Invalid input
    FVector2D buffer[16];
    const TArrayView<FVector2D> view(buffer, 16);
    TestEqual(TEXT("zero length"), SOLRouteOverlayMath::ComputeDashes(0.0, 10.0, 7.0, 0.0, view), 0);
    TestEqual(TEXT("negative length"), SOLRouteOverlayMath::ComputeDashes(-5.0, 10.0, 7.0, 0.0, view), 0);
    TestEqual(TEXT("NaN length"), SOLRouteOverlayMath::ComputeDashes(ROM_NAN, 10.0, 7.0, 0.0, view), 0);
    TestEqual(TEXT("infinite length"), SOLRouteOverlayMath::ComputeDashes(ROM_INF, 10.0, 7.0, 0.0, view), 0);
    TestEqual(TEXT("zero dash"), SOLRouteOverlayMath::ComputeDashes(50.0, 0.0, 7.0, 0.0, view), 0);
    TestEqual(TEXT("negative gap"), SOLRouteOverlayMath::ComputeDashes(50.0, 10.0, -1.0, 0.0, view), 0);

    // Invariants over many lengths and phases: increasing, non-overlapping, inside [0, 1], t0 < t1
    FVector2D many[SOL::Route::OVERLAY_MAX_DASHES];
    bool bValid = true;
    for (int32 lengthStep = 1; lengthStep <= 40; ++lengthStep)
    {
        const double lengthPx = 7.3 * lengthStep;
        for (int32 phaseStep = -20; phaseStep <= 40; phaseStep += 3)
        {
            const int32 count = SOLRouteOverlayMath::ComputeDashes(lengthPx, SOL::Route::OVERLAY_DASH_PX,
                SOL::Route::OVERLAY_GAP_PX, 1.7 * phaseStep, TArrayView<FVector2D>(many, UE_ARRAY_COUNT(many)));
            bValid = bValid && count >= 1;
            double previousEnd = -1.0;
            for (int32 index = 0; index < count; ++index)
            {
                bValid = bValid && many[index].X >= 0.0 && many[index].Y <= 1.0 && many[index].X < many[index].Y
                    && many[index].X > previousEnd;
                previousEnd = many[index].Y;
            }
        }
    }
    TestTrue(TEXT("dash intervals well formed"), bValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteOverlayPlaneAndStyleTest, "SOLTest.RouteOverlayMath.PlaneAndStyle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Below-plane test compares Z only (on the plane is not below); style alphas come from SOL::Route
bool FSOLRouteOverlayPlaneAndStyleTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d planePoint(5.0e9, -2.0e9, 100.0);
    TestTrue(TEXT("below"), SOLRouteOverlayMath::IsBelowPlane(FVector3d(0.0, 0.0, 99.0), planePoint));
    TestFalse(TEXT("on plane"), SOLRouteOverlayMath::IsBelowPlane(FVector3d(-7.0e11, 3.0e10, 100.0), planePoint));
    TestFalse(TEXT("above"), SOLRouteOverlayMath::IsBelowPlane(FVector3d(5.0e9, -2.0e9, 101.0), planePoint));
    TestTrue(TEXT("far below"), SOLRouteOverlayMath::IsBelowPlane(FVector3d(1.0e12, 1.0e12, -1.0e9), planePoint));

    TestEqual(TEXT("Active"), SOLRouteOverlayMath::StyleAlpha(ESOLRouteDrawStyle::Active),
        SOL::Route::OVERLAY_ALPHA_ACTIVE, ROM_TOL);
    TestEqual(TEXT("Draft"), SOLRouteOverlayMath::StyleAlpha(ESOLRouteDrawStyle::Draft),
        SOL::Route::OVERLAY_ALPHA_DRAFT, ROM_TOL);
    TestEqual(TEXT("Paused"), SOLRouteOverlayMath::StyleAlpha(ESOLRouteDrawStyle::Paused),
        SOL::Route::OVERLAY_ALPHA_PAUSED, ROM_TOL);
    TestEqual(TEXT("Finished"), SOLRouteOverlayMath::StyleAlpha(ESOLRouteDrawStyle::Finished),
        SOL::Route::OVERLAY_ALPHA_FINISHED, ROM_TOL);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
