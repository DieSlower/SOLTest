/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLRoute.h"

#include "CoreMinimal.h"

// Issue #20 route overlay (SDD 20 section 2.4): which legs a route has and which to draw first, near-plane and screen
// clipping, the directional fade, fade tessellation, the dashed closing leg and the above/below-plane test.
// Pure logic: no UObjects, no world access, no allocation.

// How a route is drawn (sets its alpha)
enum class ESOLRouteDrawStyle : uint8
{
    Active,     // Flown by the autopilot: full brightness
    Draft,      // Being planned on the map: SOL::Route::OVERLAY_ALPHA_DRAFT
    Paused,     // Paused by manual input: SOL::Route::OVERLAY_ALPHA_PAUSED (plus a PAUSED tag)
    Finished,   // A Move that reached its end: SOL::Route::OVERLAY_ALPHA_FINISHED
};

// One drawn leg of a route: from one waypoint to the next; the closing leg of a Patrol Loop (last back to first) is
// drawn dashed
struct FSOLRouteLeg
{
    int32 FromIndex = INDEX_NONE;
    int32 ToIndex = INDEX_NONE;
    bool bClosing = false;
};

namespace SOLRouteOverlayMath
{
    // Returns the number of waypoint-to-waypoint legs a route draws: N - 1 for a Move or a Patrol Ping-pong, N for a
    // Patrol Loop of two or more waypoints (the extra one is the closing leg, last back to first); 0 with fewer than two
    // waypoints (also for a one-waypoint Loop) or a negative count
    SOLTEST_API int32 LegCount(ESOLRouteKind kind, int32 waypointCount);

    // Fills leg legIndex: leg i < N - 1 runs from waypoint i to i + 1 (not closing); a Loop's leg N - 1 runs from N - 1
    // to 0 (closing). Returns false (outLeg untouched) when legIndex is outside [0, LegCount)
    SOLTEST_API bool GetLeg(ESOLRouteKind kind, int32 waypointCount, int32 legIndex, FSOLRouteLeg& outLeg);

    // Returns the leg that ends at the cursor's target, travelled the way the cursor is going: Move and Ping-pong with
    // direction +1, leg target - 1; Ping-pong with direction -1, leg target (from target + 1 down to target); a Loop
    // flying back to waypoint 0 after at least one reached waypoint, the closing leg N - 1, otherwise leg target - 1.
    // INDEX_NONE when the cursor is finished, the target is out of range, there are fewer than two waypoints, or the
    // ship is still flying to the route's first waypoint (target 0 with no leg completed: that leg starts at the ship)
    SOLTEST_API int32 ActiveLegIndex(ESOLRouteKind kind, int32 waypointCount, const FSOLRouteCursor& cursor);

    // Returns the leg at drawing priority rank (0 = most important) around the focus leg: focus, focus + 1, focus - 1,
    // focus + 2, focus - 2, ... skipping indices outside [0, legCount); once one side runs out the rest continue on the
    // other side in order. The focus is clamped into [0, legCount - 1]. Every leg appears exactly once over ranks
    // 0 .. legCount - 1; INDEX_NONE for a rank outside that range or legCount <= 0
    SOLTEST_API int32 LegAtPriority(int32 legCount, int32 focusLeg, int32 rank);

    // Clips the segment a -> b (camera-local, X forward, any unit) to the half space X >= nearX. On success returns true
    // with the kept parameter range [outT0, outT1] within [0, 1] (0 = a, 1 = b; the clip point is where X == nearX on the
    // straight segment); false when both ends are behind the plane (X < nearX), or on non-finite input.
    // An end exactly on the plane counts as kept
    SOLTEST_API bool ClipToNearPlane(const FVector& localA, const FVector& localB, double nearX, double& outT0,
        double& outT1);

    // Clips the screen segment a -> b to the rectangle [rectMin, rectMax] (Liang-Barsky). Returns true with the kept
    // parameter range [outT0, outT1] within [0, 1] when any part of it (an end on the border included) lies inside;
    // false when it misses the rectangle, or on non-finite input or an empty rectangle (max < min). A zero-length
    // segment is kept (0, 1) when its point is inside
    SOLTEST_API bool ClipToRect(const FVector2D& a, const FVector2D& b, const FVector2D& rectMin,
        const FVector2D& rectMax, double& outT0, double& outT1);

    // Returns the LT directional fade exp(-SOL::Route::OVERLAY_FADE_RATE * (1 - t)) with t clamped into [0, 1]: 1 at
    // the leg's end (t = 1), exp(-2) at its start; non-finite t gives the start value
    SOLTEST_API double DirectionalFade(double t);

    // Returns how many pieces a leg of lengthPx on screen is tessellated into for its fade: ceil(lengthPx /
    // pxPerSubdivision) clamped into [1, maxSubdivisions]; 1 for a non-positive or non-finite length or step, and
    // never below 1 even if maxSubdivisions is
    SOLTEST_API int32 SubdivisionCount(double lengthPx, double pxPerSubdivision, int32 maxSubdivisions);

    // Fills the dashes along a screen line of lengthPx whose pattern (dashPx on, gapPx off, repeating) starts phasePx
    // before the line's start (phase taken modulo dashPx + gapPx, negative phases wrapped into range): each dash is
    // written as (t0, t1), fractions of lengthPx with 0 <= t0 < t1 <= 1, in increasing order, dashes cut at both ends of
    // the line. Writes at most outDashes.Num() and returns the count written. Returns 0 for a non-positive or non-finite
    // length, a non-positive dash or a negative gap; with a zero gap the whole line is one dash (0, 1)
    SOLTEST_API int32 ComputeDashes(double lengthPx, double dashPx, double gapPx, double phasePx,
        TArrayView<FVector2D> outDashes);

    // Returns whether a point lies below the ecliptic plane through planePointM (its Z is lower; on the plane is not
    // below)
    SOLTEST_API bool IsBelowPlane(const FVector3d& pointM, const FVector3d& planePointM);

    // Returns a draw style's alpha: SOL::Route::OVERLAY_ALPHA_ACTIVE, _DRAFT, _PAUSED or _FINISHED
    SOLTEST_API double StyleAlpha(ESOLRouteDrawStyle style);
}
