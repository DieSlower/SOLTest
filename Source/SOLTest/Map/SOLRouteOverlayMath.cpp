/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRouteOverlayMath.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Returns the number of waypoint-to-waypoint legs a route draws (a Loop adds its closing leg)
int32 SOLRouteOverlayMath::LegCount(const ESOLRouteKind kind, const int32 waypointCount)
{
    if (waypointCount < 2)
    {
        return 0;
    }
    return kind == ESOLRouteKind::PatrolLoop ? waypointCount : waypointCount - 1;
}

//////////////////////////////////////////////////////////////////////////
// Fills leg legIndex (i -> i + 1, or a Loop's closing leg N - 1 -> 0); false when out of range
bool SOLRouteOverlayMath::GetLeg(const ESOLRouteKind kind, const int32 waypointCount, const int32 legIndex,
    FSOLRouteLeg& outLeg)
{
    if (legIndex < 0 || legIndex >= LegCount(kind, waypointCount))
    {
        return false;
    }
    const bool bClosing = legIndex == waypointCount - 1;
    outLeg.FromIndex = legIndex;
    outLeg.ToIndex = bClosing ? 0 : legIndex + 1;
    outLeg.bClosing = bClosing;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the leg that ends at the cursor's target, travelled the way the cursor is going, or INDEX_NONE
int32 SOLRouteOverlayMath::ActiveLegIndex(const ESOLRouteKind kind, const int32 waypointCount,
    const FSOLRouteCursor& cursor)
{
    const int32 target = cursor.TargetIndex;
    if (cursor.bFinished || waypointCount < 2 || target < 0 || target >= waypointCount)
    {
        return INDEX_NONE;
    }
    if (target == 0 && cursor.LegsCompleted == 0)
    {
        return INDEX_NONE;      // Still flying from the ship to the first waypoint
    }
    switch (kind)
    {
    case ESOLRouteKind::PatrolLoop:
        return target == 0 ? waypointCount - 1 : target - 1;
    case ESOLRouteKind::PatrolPingPong:
        if (cursor.Direction < 0)
        {
            return target + 1 < waypointCount ? target : INDEX_NONE;
        }
        return target > 0 ? target - 1 : INDEX_NONE;
    case ESOLRouteKind::Move:
    default:
        return target > 0 ? target - 1 : INDEX_NONE;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the leg at a drawing priority rank around the focus leg (alternating ahead and behind), or INDEX_NONE
int32 SOLRouteOverlayMath::LegAtPriority(const int32 legCount, const int32 focusLeg, const int32 rank)
{
    if (legCount <= 0 || rank < 0 || rank >= legCount)
    {
        return INDEX_NONE;
    }
    const int32 focus = FMath::Clamp(focusLeg, 0, legCount - 1);
    if (rank == 0)
    {
        return focus;
    }

    // Alternate +1, -1, +2, -2 while both sides have legs, then continue on the longer side
    const int32 below = focus;
    const int32 above = legCount - 1 - focus;
    const int32 both = FMath::Min(below, above);
    if (rank <= 2 * both)
    {
        const int32 step = (rank + 1) / 2;
        return (rank % 2) == 1 ? focus + step : focus - step;
    }
    const int32 extra = rank - both;
    return above > below ? focus + extra : focus - extra;
}

//////////////////////////////////////////////////////////////////////////
// Clips a camera-local segment to X >= nearX; false when it lies wholly behind
bool SOLRouteOverlayMath::ClipToNearPlane(const FVector& localA, const FVector& localB, const double nearX,
    double& outT0, double& outT1)
{
    const double xa = localA.X;
    const double xb = localB.X;
    if (!FMath::IsFinite(xa) || !FMath::IsFinite(xb) || !FMath::IsFinite(nearX) || localA.ContainsNaN()
        || localB.ContainsNaN())
    {
        return false;
    }
    const bool bKeepA = xa >= nearX;
    const bool bKeepB = xb >= nearX;
    if (!bKeepA && !bKeepB)
    {
        return false;
    }
    outT0 = 0.0;
    outT1 = 1.0;
    if (bKeepA && bKeepB)
    {
        return true;
    }
    const double tPlane = FMath::Clamp((nearX - xa) / (xb - xa), 0.0, 1.0);
    if (bKeepA)
    {
        outT1 = tPlane;
    }
    else
    {
        outT0 = tPlane;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Clips a screen segment to a rectangle (Liang-Barsky); false when it misses
bool SOLRouteOverlayMath::ClipToRect(const FVector2D& a, const FVector2D& b, const FVector2D& rectMin,
    const FVector2D& rectMax, double& outT0, double& outT1)
{
    if (a.ContainsNaN() || b.ContainsNaN() || rectMin.ContainsNaN() || rectMax.ContainsNaN()
        || !FMath::IsFinite(a.X) || !FMath::IsFinite(a.Y) || !FMath::IsFinite(b.X) || !FMath::IsFinite(b.Y)
        || !FMath::IsFinite(rectMin.X) || !FMath::IsFinite(rectMin.Y) || !FMath::IsFinite(rectMax.X)
        || !FMath::IsFinite(rectMax.Y) || rectMax.X < rectMin.X || rectMax.Y < rectMin.Y)
    {
        return false;
    }
    const FVector2D delta = b - a;
    const double p[4] = { -delta.X, delta.X, -delta.Y, delta.Y };
    const double q[4] = { a.X - rectMin.X, rectMax.X - a.X, a.Y - rectMin.Y, rectMax.Y - a.Y };
    double t0 = 0.0;
    double t1 = 1.0;
    for (int32 edge = 0; edge < 4; ++edge)
    {
        if (p[edge] == 0.0)
        {
            // Parallel to this edge: outside it means no part is inside
            if (q[edge] < 0.0)
            {
                return false;
            }
            continue;
        }
        const double t = q[edge] / p[edge];
        if (p[edge] < 0.0)
        {
            t0 = FMath::Max(t0, t);
        }
        else
        {
            t1 = FMath::Min(t1, t);
        }
        if (t0 > t1)
        {
            return false;
        }
    }
    outT0 = t0;
    outT1 = t1;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the LT directional fade along a leg (1 at its end, exp(-rate) at its start)
double SOLRouteOverlayMath::DirectionalFade(const double t)
{
    const double clamped = FMath::IsFinite(t) ? FMath::Clamp(t, 0.0, 1.0) : 0.0;
    return FMath::Exp(-SOL::Route::OVERLAY_FADE_RATE * (1.0 - clamped));
}

//////////////////////////////////////////////////////////////////////////
// Returns how many pieces a leg's fade is tessellated into for its length on screen
int32 SOLRouteOverlayMath::SubdivisionCount(const double lengthPx, const double pxPerSubdivision,
    const int32 maxSubdivisions)
{
    const int32 maxCount = FMath::Max(1, maxSubdivisions);
    if (!FMath::IsFinite(lengthPx) || !FMath::IsFinite(pxPerSubdivision) || lengthPx <= 0.0 || pxPerSubdivision <= 0.0)
    {
        return 1;
    }
    const double pieces = FMath::CeilToDouble(lengthPx / pxPerSubdivision);
    return pieces >= maxCount ? maxCount : FMath::Max(1, static_cast<int32>(pieces));
}

//////////////////////////////////////////////////////////////////////////
// Fills the dash intervals (fractions of the line) of a dash/gap pattern shifted by a phase; returns the count
int32 SOLRouteOverlayMath::ComputeDashes(const double lengthPx, const double dashPx, const double gapPx,
    const double phasePx, TArrayView<FVector2D> outDashes)
{
    if (!FMath::IsFinite(lengthPx) || !FMath::IsFinite(dashPx) || !FMath::IsFinite(gapPx) || lengthPx <= 0.0
        || dashPx <= 0.0 || gapPx < 0.0 || outDashes.Num() == 0)
    {
        return 0;
    }
    if (gapPx == 0.0)
    {
        outDashes[0] = FVector2D(0.0, 1.0);
        return 1;
    }

    // Position of the line's start inside the pattern, then walk the dashes from the one covering (or after) it
    const double period = dashPx + gapPx;
    double phase = FMath::IsFinite(phasePx) ? FMath::Fmod(phasePx, period) : 0.0;
    if (phase < 0.0)
    {
        phase += period;
    }
    double dashStartPx = -phase;
    int32 count = 0;
    while (dashStartPx < lengthPx && count < outDashes.Num())
    {
        const double startPx = FMath::Max(dashStartPx, 0.0);
        const double endPx = FMath::Min(dashStartPx + dashPx, lengthPx);
        if (endPx > startPx)
        {
            outDashes[count++] = FVector2D(startPx / lengthPx, endPx / lengthPx);
        }
        dashStartPx += period;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a point lies below the ecliptic plane through a plane point
bool SOLRouteOverlayMath::IsBelowPlane(const FVector3d& pointM, const FVector3d& planePointM)
{
    return pointM.Z < planePointM.Z;
}

//////////////////////////////////////////////////////////////////////////
// Returns a draw style's alpha
double SOLRouteOverlayMath::StyleAlpha(const ESOLRouteDrawStyle style)
{
    switch (style)
    {
    case ESOLRouteDrawStyle::Draft:
        return SOL::Route::OVERLAY_ALPHA_DRAFT;
    case ESOLRouteDrawStyle::Paused:
        return SOL::Route::OVERLAY_ALPHA_PAUSED;
    case ESOLRouteDrawStyle::Finished:
        return SOL::Route::OVERLAY_ALPHA_FINISHED;
    case ESOLRouteDrawStyle::Active:
    default:
        return SOL::Route::OVERLAY_ALPHA_ACTIVE;
    }
}
