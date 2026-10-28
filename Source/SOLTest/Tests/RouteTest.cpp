/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRoute.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #20 route waypoint model and sequencing (Docs/SDDs/20-hologram-map-and-move-orders.md section
// 2.2), written against the Map/SOLRoute.h contract before the production code existed
namespace
{
    constexpr double ROUTE_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double ROUTE_INF = std::numeric_limits<double>::infinity();

    // Absolute tolerance (meters) for a round trip at ~1.5e11 m (double ulp there is ~3e-5 m)
    constexpr double ROUTE_LARGE_ABS_TOL_M = 1.0e-4;

    const ESOLRouteKind ROUTE_ALL_KINDS[] = { ESOLRouteKind::Move, ESOLRouteKind::PatrolLoop,
        ESOLRouteKind::PatrolPingPong };

    //////////////////////////////////////////////////////////////////////////
    // Returns a short name of a route kind for test messages
    const TCHAR* RouteKindName(const ESOLRouteKind kind)
    {
        switch (kind)
        {
        case ESOLRouteKind::Move:
            return TEXT("Move");
        case ESOLRouteKind::PatrolLoop:
            return TEXT("Loop");
        case ESOLRouteKind::PatrolPingPong:
            return TEXT("PingPong");
        }
        return TEXT("?");
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the target indices visited from the start cursor: the start target, then the target after each Advance
    // that returned true, for at most maxAdvances advances
    TArray<int32> RouteVisitSequence(const ESOLRouteKind kind, const int32 count, const int32 maxAdvances)
    {
        TArray<int32> visited;
        FSOLRouteCursor cursor = SOLRoute::MakeStartCursor(count);
        if (cursor.bFinished)
        {
            return visited;
        }
        visited.Add(cursor.TargetIndex);
        for (int32 step = 0; step < maxAdvances; ++step)
        {
            if (!SOLRoute::Advance(kind, count, cursor))
            {
                break;
            }
            visited.Add(cursor.TargetIndex);
        }
        return visited;
    }

    //////////////////////////////////////////////////////////////////////////
    // Formats an index sequence as "0,1,2" for test messages
    FString RouteSequenceText(const TArray<int32>& sequence)
    {
        FString text;
        for (int32 index = 0; index < sequence.Num(); ++index)
        {
            text += (index == 0 ? FString() : FString(TEXT(","))) + FString::FromInt(sequence[index]);
        }
        return text;
    }
}

// --- Waypoints ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteMakeWaypointTest, "SOLTest.Route.MakeWaypoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MakeWaypoint stores the body and the (planar X, planar Y, height) offset; MakeWaypointAtPoint stores point - body
bool FSOLRouteMakeWaypointTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRouteWaypoint waypoint = SOLRoute::MakeWaypoint(3, 1200.5, -340.25, 75.0);
    TestEqual(TEXT("body index"), waypoint.ReferenceBodyIndex, 3);
    TestTrue(TEXT("offset"), waypoint.OffsetM == FVector3d(1200.5, -340.25, 75.0));

    const FSOLRouteWaypoint defaulted;
    TestEqual(TEXT("default waypoint has no body"), defaulted.ReferenceBodyIndex, static_cast<int32>(INDEX_NONE));
    TestTrue(TEXT("default waypoint has no offset"), defaulted.OffsetM.IsZero());

    const FVector3d bodyM(1.496e11, -2.3e10, 4.0e8);
    const FVector3d pointM = bodyM + FVector3d(5.0e7, 1.25e6, -3.0e5);
    const FSOLRouteWaypoint atPoint = SOLRoute::MakeWaypointAtPoint(7, pointM, bodyM);
    TestEqual(TEXT("at point: body index"), atPoint.ReferenceBodyIndex, 7);
    TestTrue(TEXT("at point: offset = point - body"), atPoint.OffsetM == pointM - bodyM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteResolveWaypointTest, "SOLTest.Route.ResolveWaypoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ResolveWaypointM is reference + offset exactly, keeps small offsets at 1.5e11 m, round-trips MakeWaypointAtPoint,
// and gives the zero vector on non-finite input
bool FSOLRouteResolveWaypointTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d referenceM(1.5e11, -2.0e10, 1.0e9);
    const FVector3d offsetM(1234.5, -6.25, 100.0);
    const FSOLRouteWaypoint waypoint = SOLRoute::MakeWaypoint(2, offsetM.X, offsetM.Y, offsetM.Z);
    const FVector3d resolved = SOLRoute::ResolveWaypointM(waypoint, referenceM);
    TestTrue(TEXT("reference + offset exactly"), resolved == referenceM + offsetM);
    TestTrue(TEXT("small offset kept at 1.5e11 m"),
        ((resolved - referenceM) - offsetM).Size() < ROUTE_LARGE_ABS_TOL_M);

    // The waypoint follows its body: a moved reference moves the resolved point by the same amount
    const FVector3d movedM = referenceM + FVector3d(3.0e7, -1.0e6, 0.0);
    TestTrue(TEXT("follows the body"), SOLRoute::ResolveWaypointM(waypoint, movedM) == movedM + offsetM);

    // Round trip through a body-relative offset
    const FVector3d bodyM(-7.4e11, 3.3e11, -1.2e10);
    const FVector3d pointM(-7.4e11 + 8.0e8, 3.3e11 - 2.5e7, -1.2e10 + 4.0e6);
    const FSOLRouteWaypoint atPoint = SOLRoute::MakeWaypointAtPoint(5, pointM, bodyM);
    TestTrue(TEXT("round trip gives the point"),
        (SOLRoute::ResolveWaypointM(atPoint, bodyM) - pointM).Size() < ROUTE_LARGE_ABS_TOL_M);

    // Non-finite input
    TestTrue(TEXT("NaN reference"), SOLRoute::ResolveWaypointM(waypoint, FVector3d(ROUTE_NAN, 0.0, 0.0)).IsZero());
    TestTrue(TEXT("infinite reference"),
        SOLRoute::ResolveWaypointM(waypoint, FVector3d(0.0, ROUTE_INF, 0.0)).IsZero());
    const FSOLRouteWaypoint badOffset = SOLRoute::MakeWaypoint(2, 0.0, 0.0, ROUTE_NAN);
    TestTrue(TEXT("NaN offset"), SOLRoute::ResolveWaypointM(badOffset, referenceM).IsZero());
    const FSOLRouteWaypoint infOffset = SOLRoute::MakeWaypoint(2, -ROUTE_INF, 0.0, 0.0);
    TestTrue(TEXT("infinite offset"), SOLRoute::ResolveWaypointM(infOffset, referenceM).IsZero());
    return true;
}

// --- Sequencing ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteMoveSequenceTest, "SOLTest.Route.MoveSequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Move of three visits 0, 1, 2, stops at 2 and finishes after it; a finished cursor stays finished
bool FSOLRouteMoveSequenceTest::RunTest(const FString& /*parameters*/)
{
    constexpr ESOLRouteKind kind = ESOLRouteKind::Move;
    FSOLRouteCursor cursor = SOLRoute::MakeStartCursor(3);
    TestEqual(TEXT("start target"), cursor.TargetIndex, 0);
    TestEqual(TEXT("start direction"), cursor.Direction, 1);
    TestEqual(TEXT("start legs"), cursor.LegsCompleted, 0);
    TestFalse(TEXT("start not finished"), cursor.bFinished);
    TestEqual(TEXT("next after 0"), SOLRoute::PeekNextIndex(kind, 3, cursor), 1);
    TestFalse(TEXT("0 is not a stop"), SOLRoute::IsStopWaypoint(kind, 3, cursor));

    TestTrue(TEXT("advance to 1"), SOLRoute::Advance(kind, 3, cursor));
    TestEqual(TEXT("target 1"), cursor.TargetIndex, 1);
    TestEqual(TEXT("one leg"), cursor.LegsCompleted, 1);
    TestEqual(TEXT("next after 1"), SOLRoute::PeekNextIndex(kind, 3, cursor), 2);
    TestFalse(TEXT("1 is not a stop"), SOLRoute::IsStopWaypoint(kind, 3, cursor));

    TestTrue(TEXT("advance to 2"), SOLRoute::Advance(kind, 3, cursor));
    TestEqual(TEXT("target 2"), cursor.TargetIndex, 2);
    TestEqual(TEXT("two legs"), cursor.LegsCompleted, 2);
    TestEqual(TEXT("nothing after the last"), SOLRoute::PeekNextIndex(kind, 3, cursor), INDEX_NONE);
    TestTrue(TEXT("2 is the stop"), SOLRoute::IsStopWaypoint(kind, 3, cursor));
    TestFalse(TEXT("not finished before reaching 2"), cursor.bFinished);

    TestFalse(TEXT("reaching 2 finishes"), SOLRoute::Advance(kind, 3, cursor));
    TestTrue(TEXT("finished"), cursor.bFinished);
    TestEqual(TEXT("three legs"), cursor.LegsCompleted, 3);
    TestEqual(TEXT("finished: no next"), SOLRoute::PeekNextIndex(kind, 3, cursor), INDEX_NONE);
    TestFalse(TEXT("finished: no stop"), SOLRoute::IsStopWaypoint(kind, 3, cursor));

    // Advance on a finished cursor changes nothing
    TestFalse(TEXT("advance after finish"), SOLRoute::Advance(kind, 3, cursor));
    TestEqual(TEXT("legs unchanged after finish"), cursor.LegsCompleted, 3);
    TestTrue(TEXT("still finished"), cursor.bFinished);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePatrolSequenceTest, "SOLTest.Route.PatrolSequence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Loop repeats 0,1,2,0,...; Ping-pong turns at the ends 0,1,2,1,0,...; two-waypoint ping-pong alternates; neither
// ever finishes or stops
bool FSOLRoutePatrolSequenceTest::RunTest(const FString& /*parameters*/)
{
    const TArray<int32> loop = RouteVisitSequence(ESOLRouteKind::PatrolLoop, 3, 9);
    TestEqual(TEXT("loop of three"), RouteSequenceText(loop), FString(TEXT("0,1,2,0,1,2,0,1,2,0")));

    const TArray<int32> pingPong = RouteVisitSequence(ESOLRouteKind::PatrolPingPong, 3, 9);
    TestEqual(TEXT("ping-pong of three"), RouteSequenceText(pingPong), FString(TEXT("0,1,2,1,0,1,2,1,0,1")));

    const TArray<int32> pingPongTwo = RouteVisitSequence(ESOLRouteKind::PatrolPingPong, 2, 5);
    TestEqual(TEXT("ping-pong of two"), RouteSequenceText(pingPongTwo), FString(TEXT("0,1,0,1,0,1")));

    const TArray<int32> loopTwo = RouteVisitSequence(ESOLRouteKind::PatrolLoop, 2, 5);
    TestEqual(TEXT("loop of two"), RouteSequenceText(loopTwo), FString(TEXT("0,1,0,1,0,1")));

    // Long runs: never finished, never a stop, a leg counted per advance; ping-pong direction is the travel direction
    const ESOLRouteKind patrols[] = { ESOLRouteKind::PatrolLoop, ESOLRouteKind::PatrolPingPong };
    for (const ESOLRouteKind kind : patrols)
    {
        FSOLRouteCursor cursor = SOLRoute::MakeStartCursor(3);
        bool bAllAdvanced = true;
        bool bNeverStop = !SOLRoute::IsStopWaypoint(kind, 3, cursor);
        bool bDirectionOk = true;
        int32 previous = cursor.TargetIndex;
        for (int32 step = 0; step < 100; ++step)
        {
            bAllAdvanced &= SOLRoute::Advance(kind, 3, cursor);
            bNeverStop &= !SOLRoute::IsStopWaypoint(kind, 3, cursor);
            if (kind == ESOLRouteKind::PatrolPingPong && cursor.TargetIndex == 1)
            {
                // At the middle waypoint the direction is the way the ship is travelling
                bDirectionOk &= cursor.Direction == (previous == 0 ? 1 : -1);
            }
            if (kind == ESOLRouteKind::PatrolLoop)
            {
                bDirectionOk &= cursor.Direction == 1;
            }
            previous = cursor.TargetIndex;
        }
        TestTrue(FString::Printf(TEXT("%s: every advance continues"), RouteKindName(kind)), bAllAdvanced);
        TestFalse(FString::Printf(TEXT("%s: never finished"), RouteKindName(kind)), cursor.bFinished);
        TestTrue(FString::Printf(TEXT("%s: never a stop waypoint"), RouteKindName(kind)), bNeverStop);
        TestEqual(FString::Printf(TEXT("%s: legs counted"), RouteKindName(kind)), cursor.LegsCompleted, 100);
        TestTrue(FString::Printf(TEXT("%s: direction"), RouteKindName(kind)), bDirectionOk);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteSingleAndEmptyTest, "SOLTest.Route.SingleAndEmpty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One waypoint (any kind) is a stop and finishes when reached; no waypoints starts finished and never advances
bool FSOLRouteSingleAndEmptyTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLRouteKind kind : ROUTE_ALL_KINDS)
    {
        const TCHAR* name = RouteKindName(kind);
        FSOLRouteCursor single = SOLRoute::MakeStartCursor(1);
        TestFalse(FString::Printf(TEXT("%s single: not finished"), name), single.bFinished);
        TestEqual(FString::Printf(TEXT("%s single: target 0"), name), single.TargetIndex, 0);
        TestTrue(FString::Printf(TEXT("%s single: stop"), name), SOLRoute::IsStopWaypoint(kind, 1, single));
        TestEqual(FString::Printf(TEXT("%s single: no next"), name), SOLRoute::PeekNextIndex(kind, 1, single),
            INDEX_NONE);
        TestFalse(FString::Printf(TEXT("%s single: advance finishes"), name), SOLRoute::Advance(kind, 1, single));
        TestTrue(FString::Printf(TEXT("%s single: finished"), name), single.bFinished);
        TestEqual(FString::Printf(TEXT("%s single: one leg"), name), single.LegsCompleted, 1);

        FSOLRouteCursor empty = SOLRoute::MakeStartCursor(0);
        TestTrue(FString::Printf(TEXT("%s empty: finished"), name), empty.bFinished);
        TestEqual(FString::Printf(TEXT("%s empty: no next"), name), SOLRoute::PeekNextIndex(kind, 0, empty),
            INDEX_NONE);
        TestFalse(FString::Printf(TEXT("%s empty: no stop"), name), SOLRoute::IsStopWaypoint(kind, 0, empty));
        TestFalse(FString::Printf(TEXT("%s empty: advance"), name), SOLRoute::Advance(kind, 0, empty));
        TestEqual(FString::Printf(TEXT("%s empty: no legs"), name), empty.LegsCompleted, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePeekAgreesTest, "SOLTest.Route.PeekAgreesWithAdvance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// For every kind and 2..5 waypoints, PeekNextIndex names the target Advance moves to, and INDEX_NONE exactly when
// Advance finishes; out-of-range targets have no next waypoint
bool FSOLRoutePeekAgreesTest::RunTest(const FString& /*parameters*/)
{
    for (const ESOLRouteKind kind : ROUTE_ALL_KINDS)
    {
        for (int32 count = 2; count <= 5; ++count)
        {
            FSOLRouteCursor cursor = SOLRoute::MakeStartCursor(count);
            bool bAgrees = true;
            for (int32 step = 0; step < 4 * count && !cursor.bFinished; ++step)
            {
                const int32 next = SOLRoute::PeekNextIndex(kind, count, cursor);
                const bool bStop = SOLRoute::IsStopWaypoint(kind, count, cursor);
                const bool bAdvanced = SOLRoute::Advance(kind, count, cursor);
                bAgrees &= bStop == (next == INDEX_NONE);
                const bool bInRange = cursor.TargetIndex >= 0 && cursor.TargetIndex < count;
                bAgrees &= next == INDEX_NONE ? (!bAdvanced && cursor.bFinished)
                    : (bAdvanced && cursor.TargetIndex == next && bInRange);
            }
            TestTrue(FString::Printf(TEXT("%s, %d waypoints: peek agrees with advance"), RouteKindName(kind), count),
                bAgrees);
            TestEqual(FString::Printf(TEXT("%s, %d waypoints: finished only for Move"), RouteKindName(kind), count),
                cursor.bFinished, kind == ESOLRouteKind::Move);

            // A target past the end (or negative) has no next waypoint
            FSOLRouteCursor outOfRange = SOLRoute::MakeStartCursor(count);
            outOfRange.TargetIndex = count;
            TestEqual(FString::Printf(TEXT("%s, %d: target == count"), RouteKindName(kind), count),
                SOLRoute::PeekNextIndex(kind, count, outOfRange), INDEX_NONE);
            outOfRange.TargetIndex = -1;
            TestEqual(FString::Printf(TEXT("%s, %d: negative target"), RouteKindName(kind), count),
                SOLRoute::PeekNextIndex(kind, count, outOfRange), INDEX_NONE);
        }
    }
    return true;
}

// --- Totals ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteTotalsTest, "SOLTest.Route.TotalLegsAndKinds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Move has one leg per waypoint, a patrol none (endless), no waypoints none; IsPatrol names the two patrols
bool FSOLRouteTotalsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Move of 3"), SOLRoute::TotalLegs(ESOLRouteKind::Move, 3), 3);
    TestEqual(TEXT("Move of 1"), SOLRoute::TotalLegs(ESOLRouteKind::Move, 1), 1);
    TestEqual(TEXT("Move of 0"), SOLRoute::TotalLegs(ESOLRouteKind::Move, 0), 0);
    TestEqual(TEXT("Loop of 3"), SOLRoute::TotalLegs(ESOLRouteKind::PatrolLoop, 3), 0);
    TestEqual(TEXT("Ping-pong of 3"), SOLRoute::TotalLegs(ESOLRouteKind::PatrolPingPong, 3), 0);
    TestEqual(TEXT("Loop of 0"), SOLRoute::TotalLegs(ESOLRouteKind::PatrolLoop, 0), 0);

    TestFalse(TEXT("Move is not a patrol"), SOLRoute::IsPatrol(ESOLRouteKind::Move));
    TestTrue(TEXT("Loop is a patrol"), SOLRoute::IsPatrol(ESOLRouteKind::PatrolLoop));
    TestTrue(TEXT("Ping-pong is a patrol"), SOLRoute::IsPatrol(ESOLRouteKind::PatrolPingPong));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
