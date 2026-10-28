/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRoute.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns whether every component of a vector is finite
    bool RouteIsFinite(const FVector3d& value)
    {
        return FMath::IsFinite(value.X) && FMath::IsFinite(value.Y) && FMath::IsFinite(value.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the waypoint after the cursor's target and the direction it is reached in, or INDEX_NONE
    int32 RouteNextIndex(const ESOLRouteKind kind, const int32 count, const FSOLRouteCursor& cursor,
        int32& outDirection)
    {
        outDirection = cursor.Direction;
        if (cursor.bFinished || count <= 1 || cursor.TargetIndex < 0 || cursor.TargetIndex >= count)
        {
            return INDEX_NONE;
        }
        switch (kind)
        {
        case ESOLRouteKind::PatrolLoop:
            outDirection = 1;
            return (cursor.TargetIndex + 1) % count;
        case ESOLRouteKind::PatrolPingPong:
        {
            // Turn around at either end
            const int32 direction = cursor.Direction >= 0 ? 1 : -1;
            const int32 next = cursor.TargetIndex + direction;
            outDirection = next >= 0 && next < count ? direction : -direction;
            return cursor.TargetIndex + outDirection;
        }
        case ESOLRouteKind::Move:
        default:
            outDirection = 1;
            return cursor.TargetIndex + 1 < count ? cursor.TargetIndex + 1 : INDEX_NONE;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a waypoint following a body at an ecliptic offset
FSOLRouteWaypoint SOLRoute::MakeWaypoint(const int32 bodyIndex, const double planarXM, const double planarYM,
    const double heightM)
{
    FSOLRouteWaypoint waypoint;
    waypoint.ReferenceBodyIndex = bodyIndex;
    waypoint.OffsetM = FVector3d(planarXM, planarYM, heightM);
    return waypoint;
}

//////////////////////////////////////////////////////////////////////////
// Returns a waypoint at a universe point, re-expressed as an offset from a body
FSOLRouteWaypoint SOLRoute::MakeWaypointAtPoint(const int32 bodyIndex, const FVector3d& pointM,
    const FVector3d& bodyPositionM)
{
    FSOLRouteWaypoint waypoint;
    waypoint.ReferenceBodyIndex = bodyIndex;
    waypoint.OffsetM = pointM - bodyPositionM;
    return waypoint;
}

//////////////////////////////////////////////////////////////////////////
// Returns the waypoint's universe position for its reference body's current position
FVector3d SOLRoute::ResolveWaypointM(const FSOLRouteWaypoint& waypoint, const FVector3d& referencePositionM)
{
    if (!RouteIsFinite(waypoint.OffsetM) || !RouteIsFinite(referencePositionM))
    {
        return FVector3d::ZeroVector;
    }
    return referencePositionM + waypoint.OffsetM;
}

//////////////////////////////////////////////////////////////////////////
// Returns the start cursor; finished when there are no waypoints
FSOLRouteCursor SOLRoute::MakeStartCursor(const int32 waypointCount)
{
    FSOLRouteCursor cursor;
    cursor.bFinished = waypointCount <= 0;
    return cursor;
}

//////////////////////////////////////////////////////////////////////////
// Returns the waypoint after the target in the sequence, or INDEX_NONE
int32 SOLRoute::PeekNextIndex(const ESOLRouteKind kind, const int32 waypointCount, const FSOLRouteCursor& cursor)
{
    int32 direction = 1;
    return RouteNextIndex(kind, waypointCount, cursor, direction);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the ship must stop at the target
bool SOLRoute::IsStopWaypoint(const ESOLRouteKind kind, const int32 waypointCount, const FSOLRouteCursor& cursor)
{
    return !cursor.bFinished && cursor.TargetIndex >= 0 && cursor.TargetIndex < waypointCount
        && PeekNextIndex(kind, waypointCount, cursor) == INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// The target was reached: counts the leg and moves on, or finishes the route; false once finished
bool SOLRoute::Advance(const ESOLRouteKind kind, const int32 waypointCount, FSOLRouteCursor& cursor)
{
    if (cursor.bFinished)
    {
        return false;
    }
    ++cursor.LegsCompleted;
    int32 direction = 1;
    const int32 next = RouteNextIndex(kind, waypointCount, cursor, direction);
    if (next == INDEX_NONE)
    {
        cursor.bFinished = true;
        return false;
    }
    cursor.TargetIndex = next;
    cursor.Direction = direction;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of legs a Move has from its start; 0 for a patrol or no waypoints
int32 SOLRoute::TotalLegs(const ESOLRouteKind kind, const int32 waypointCount)
{
    return kind == ESOLRouteKind::Move ? FMath::Max(waypointCount, 0) : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the kind is a patrol
bool SOLRoute::IsPatrol(const ESOLRouteKind kind)
{
    return kind == ESOLRouteKind::PatrolLoop || kind == ESOLRouteKind::PatrolPingPong;
}
