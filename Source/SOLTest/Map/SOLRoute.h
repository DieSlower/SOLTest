/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Issue #20 routes (SDD 20 section 2.2): the waypoint model and the Move / Patrol Loop / Patrol Ping-pong sequencing.
// Pure logic: no UObjects, no world access, no allocation.

// The shape of a route
enum class ESOLRouteKind : uint8
{
    Move,               // A, B, C, then stop at C
    PatrolLoop,         // A, B, C, A, B, C, ... until cancelled
    PatrolPingPong,     // A, B, C, B, A, B, ... until cancelled
};

// One waypoint: a point that follows its reference body (offset in ecliptic axes, meters, from the body's centre)
struct FSOLRouteWaypoint
{
    int32 ReferenceBodyIndex = INDEX_NONE;          // Body registry index the waypoint moves with
    FVector3d OffsetM = FVector3d::ZeroVector;      // X, Y planar (ecliptic), Z height above the ecliptic plane
};

// Where a route's sequence is: the waypoint being flown to, the ping-pong direction and the count of reached ones
struct FSOLRouteCursor
{
    int32 TargetIndex = 0;          // Waypoint being flown to
    int32 Direction = 1;            // +1 or -1 (ping-pong only; always +1 otherwise)
    int32 LegsCompleted = 0;        // Waypoints reached since the start
    bool bFinished = false;         // The route ended (a Move reached its last waypoint, or there are no waypoints)
};

namespace SOLRoute
{
    // Returns a waypoint following a body at an ecliptic offset (dx, dy planar, dz height)
    SOLTEST_API FSOLRouteWaypoint MakeWaypoint(int32 bodyIndex, double planarXM, double planarYM, double heightM);

    // Returns a waypoint at a universe point, re-expressed as an offset from a body (pointM - bodyPositionM)
    SOLTEST_API FSOLRouteWaypoint MakeWaypointAtPoint(int32 bodyIndex, const FVector3d& pointM,
        const FVector3d& bodyPositionM);

    // Returns the waypoint's universe position for its reference body's current position (reference + offset); the
    // zero vector on non-finite input
    SOLTEST_API FVector3d ResolveWaypointM(const FSOLRouteWaypoint& waypoint, const FVector3d& referencePositionM);

    // Returns the start cursor (target 0, direction +1, nothing reached); finished when there are no waypoints
    SOLTEST_API FSOLRouteCursor MakeStartCursor(int32 waypointCount);

    // Returns the waypoint after the target in the sequence, or INDEX_NONE when the target is the last one (a Move's
    // last waypoint, any route of one waypoint) or the cursor is finished or out of range
    SOLTEST_API int32 PeekNextIndex(ESOLRouteKind kind, int32 waypointCount, const FSOLRouteCursor& cursor);

    // Returns whether the ship must stop at the target: unfinished and no waypoint follows it
    SOLTEST_API bool IsStopWaypoint(ESOLRouteKind kind, int32 waypointCount, const FSOLRouteCursor& cursor);

    // The target was reached: counts the leg and moves to the next waypoint (ping-pong turns around at either end),
    // or finishes the route when none follows. Returns false once the route is finished (also when it already was)
    SOLTEST_API bool Advance(ESOLRouteKind kind, int32 waypointCount, FSOLRouteCursor& cursor);

    // Returns the number of legs a Move has from its start (one per waypoint); 0 for a patrol (endless) or no waypoints
    SOLTEST_API int32 TotalLegs(ESOLRouteKind kind, int32 waypointCount);

    // Returns whether the kind is a patrol (Loop or Ping-pong)
    SOLTEST_API bool IsPatrol(ESOLRouteKind kind);
}
