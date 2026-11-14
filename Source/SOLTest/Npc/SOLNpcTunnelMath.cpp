/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcTunnelMath.h"

#include "SOLConstants.h"

#include "Containers/StaticArray.h"

#include <cmath>
#include <limits>

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns true for a finite, positive radius or spacing
    bool TunnelIsPositive(const double value)
    {
        return FMath::IsFinite(value) && value > 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of a vector is finite
    bool TunnelIsFinite(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns frac(x) in [0, 1) (a negative x wraps up; a result rounded up to 1 reads as 0)
    double TunnelFrac(const double x)
    {
        const double fraction = x - FMath::FloorToDouble(x);
        return fraction >= 1.0 ? 0.0 : fraction;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a phase wrapped into [0, 1); a non-finite phase reads as 0
    double TunnelWrapPhase(const double phase)
    {
        return FMath::IsFinite(phase) ? TunnelFrac(phase) : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the descending smoothstep of a value: 1 at and below low, 0 at and above high
    double TunnelSmoothFall(const double value, const double low, const double high)
    {
        const double t = FMath::Clamp((value - low) / (high - low), 0.0, 1.0);
        return 1.0 - t * t * (3.0 - 2.0 * t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the constant ring spacing near the camera, S0 = NPC_TUNNEL_RING_BASE_SPACING_RADII x Rt
    double TunnelBaseSpacingM(const double tunnelRadiusM)
    {
        return SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII * tunnelRadiusM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the half length x0 = sqrt(max(0, D0^2 - h^2)) of the constant-spacing zone on the foot point, with
    // D0 = S0 / rho
    double TunnelConstantZoneM(const double radialM, const double tunnelRadiusM)
    {
        const double d0 = TunnelBaseSpacingM(tunnelRadiusM) / SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE;
        return FMath::Sqrt(FMath::Max(0.0, d0 * d0 - radialM * radialM));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the warp coordinate of ring slot k (ahead: k + 1 - phase; behind: -((k - PER_SIDE) + phase))
    double TunnelRingSlotW(const int32 slot, const double phase)
    {
        const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
        return slot < perSide ? static_cast<double>(slot + 1) - phase
            : -(static_cast<double>(slot - perSide) + phase);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the warp coordinate of gap slot k's ring (ahead: k - phase; behind: -(k - PER_SIDE + 1) - phase)
    double TunnelGapSlotW(const int32 slot, const double phase)
    {
        const int32 perSide = SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE;
        return slot < perSide ? static_cast<double>(slot) - phase
            : -static_cast<double>(slot - perSide + 1) - phase;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a slot input can lay out rings (a finite radial distance, end arcs, a positive tunnel radius;
    // the phase is wrapped by the caller)
    bool TunnelIsInputValid(const FSOLNpcTunnelSlotInput& input)
    {
        return TunnelIsPositive(input.TunnelRadiusM) && FMath::IsFinite(input.RadialM) && !FMath::IsNaN(input.EndArcAM)
            && !FMath::IsNaN(input.EndArcBM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the dot rows' clock directions (cos, sin of each row's clock angle), computed once: the same values
    // WallDirection takes, so a row written from the table matches one written through it
    const FVector2D* TunnelDotRowClocks()
    {
        static const TStaticArray<FVector2D, SOL::NPC_TUNNEL_DOT_ROWS> clocks = []()
        {
            TStaticArray<FVector2D, SOL::NPC_TUNNEL_DOT_ROWS> table;
            for (int32 row = 0; row < SOL::NPC_TUNNEL_DOT_ROWS; ++row)
            {
                const double angle = FMath::DegreesToRadians(SOLNpcTunnel::DotRowAngleDeg(row));
                table[row] = FVector2D(FMath::Cos(angle), FMath::Sin(angle));
            }
            return table;
        }();
        return clocks.GetData();
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the axial range (from, to) every dot row of a tunnel shares: max(end arc A, -X) to min(end arc B, X), X
    // where the dots end at NPC_TUNNEL_DETAIL_FADE_END_RADII x Rt of true distance; false when it is empty or an input
    // is bad
    bool TunnelDotRowRangeM(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis, const double tunnelRadiusM,
        const double endArcAM, const double endArcBM, FVector2D& outRangeM)
    {
        if (!basis.bValid || !TunnelIsPositive(tunnelRadiusM) || !TunnelIsFinite(footRelM) || FMath::IsNaN(endArcAM)
            || FMath::IsNaN(endArcBM))
        {
            return false;
        }
        const double radialM = footRelM.Size();
        const double reachM = SOL::NPC_TUNNEL_DETAIL_FADE_END_RADII * tunnelRadiusM;
        const double halfM = FMath::Sqrt(FMath::Max(0.0, reachM * reachM - radialM * radialM));
        outRangeM = FVector2D(FMath::Max(endArcAM, -halfM), FMath::Min(endArcBM, halfM));
        return outRangeM.X < outRangeM.Y;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes dot row k's segment over a shared axial range, on the wall at NPC_TUNNEL_DOT_RADIUS_SCALE x Rt
    void TunnelWriteDotRow(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis, const double tunnelRadiusM,
        const FVector2D& rangeM, const int32 row, FVector3d& outStartM, FVector3d& outEndM)
    {
        const FVector2D clock = TunnelDotRowClocks()[row];
        const FVector3d wallM = (basis.Up * clock.X + basis.Right * clock.Y) * (SOL::NPC_TUNNEL_DOT_RADIUS_SCALE
            * tunnelRadiusM);
        outStartM = footRelM + basis.Axis * rangeM.X + wallM;
        outEndM = footRelM + basis.Axis * rangeM.Y + wallM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills an element at warp coordinate w (cap fade given) with its axial offset and alpha; true when it lies
    // strictly between the end rings with a non-zero alpha. Detail elements also take the detail fade
    bool TunnelPlaceElement(const double w, const double capFade, const bool bDetail,
        const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outElement)
    {
        const double rt = input.TunnelRadiusM;
        const double axialM = SOLNpcTunnel::RingWarpInverse(w, input.RadialM, rt);
        outElement.W = w;
        outElement.AxialOffsetM = axialM;
        outElement.Alpha = 0.0;
        if (!(axialM > input.EndArcAM && axialM < input.EndArcBM))
        {
            return false;
        }

        // Cap, end, depth and near fades (and the detail fade), the depth from the camera to the axis point
        const double distanceM = FMath::Sqrt(axialM * axialM + input.RadialM * input.RadialM);
        const double distanceToEndM = FMath::Min(axialM - input.EndArcAM, input.EndArcBM - axialM);
        double alpha = capFade * SOLNpcTunnel::TunnelEndFade(distanceToEndM,
            SOLNpcTunnel::RingSpacingM(axialM, input.RadialM, rt)) * SOLNpcTunnel::TunnelDepthFade(distanceM, rt)
            * SOLNpcTunnel::TunnelNearFadeAt(axialM, input.RadialM, rt);
        if (bDetail)
        {
            alpha *= SOLNpcTunnel::TunnelDetailFade(distanceM, rt);
        }
        outElement.Alpha = alpha;
        return alpha > 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when label candidate a ranks before b: higher fade, then smaller distance, then lower index
    bool TunnelLabelRanksBefore(const double fadeA, const double distanceA, const int32 laneA, const double fadeB,
        const double distanceB, const int32 laneB)
    {
        if (fadeA != fadeB)
        {
            return fadeA > fadeB;
        }
        if (distanceA != distanceB)
        {
            return distanceA < distanceB;
        }
        return laneA < laneB;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the tunnel fade: 1 inside FULL x R, 0 beyond START x R, linear in log d between; bad input shows none (0)
double SOLNpcTunnel::TunnelFade(const double distanceM, const double laneRadiusM)
{
    if (FMath::IsNaN(distanceM) || !TunnelIsPositive(laneRadiusM))
    {
        return 0.0;
    }
    const double fullM = SOL::NPC_TUNNEL_FADE_FULL_RADII * laneRadiusM;
    if (distanceM <= fullM)
    {
        return 1.0;
    }
    const double startM = SOL::NPC_TUNNEL_FADE_START_RADII * laneRadiusM;
    if (distanceM >= startM)
    {
        return 0.0;
    }
    const double fade = FMath::Loge(startM / distanceM)
        / FMath::Loge(SOL::NPC_TUNNEL_FADE_START_RADII / SOL::NPC_TUNNEL_FADE_FULL_RADII);
    return FMath::Clamp(fade, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the tunnel radius from the lane radius and its traffic (the minimum scale for an empty lane)
double SOLNpcTunnel::TunnelRadiusM(const double laneRadiusM, const int32 shipCount)
{
    if (!TunnelIsPositive(laneRadiusM))
    {
        return 0.0;
    }
    return laneRadiusM * FMath::Lerp(SOL::NPC_TUNNEL_RADIUS_MIN_SCALE, SOL::NPC_TUNNEL_RADIUS_MAX_SCALE,
        SOLNpcLane::LaneTrafficWeight(shipCount));
}

//////////////////////////////////////////////////////////////////////////
// Returns a dot row's side: +1 outbound (right half), -1 inbound, 0 out of range
int32 SOLNpcTunnel::DotRowSide(const int32 row)
{
    if (row < 0 || row >= SOL::NPC_TUNNEL_DOT_ROWS)
    {
        return 0;
    }
    return row < SOL::NPC_TUNNEL_DOT_ROWS / 2 ? 1 : -1;
}

//////////////////////////////////////////////////////////////////////////
// Returns a dot row's clock angle in degrees, clockwise from 12 o'clock
double SOLNpcTunnel::DotRowAngleDeg(const int32 row)
{
    return (static_cast<double>(row) + 0.5) * 360.0 / static_cast<double>(SOL::NPC_TUNNEL_DOT_ROWS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the wall direction at a clock angle from the basis
FVector3d SOLNpcTunnel::WallDirection(const FSOLNpcTunnelBasis& basis, const double clockDeg)
{
    const double angle = FMath::DegreesToRadians(clockDeg);
    return basis.Up * FMath::Cos(angle) + basis.Right * FMath::Sin(angle);
}

//////////////////////////////////////////////////////////////////////////
// Returns the segment parameter of the point nearest the origin, clamped to [0, 1]
double SOLNpcTunnel::LaneNearestParam(const FVector3d& aRelM, const FVector3d& bRelM)
{
    if (!TunnelIsFinite(aRelM) || !TunnelIsFinite(bRelM))
    {
        return 0.0;
    }
    const FVector3d ab = bRelM - aRelM;
    const double lengthSq = ab.SizeSquared();
    if (!(lengthSq > 0.0) || !FMath::IsFinite(lengthSq))
    {
        return 0.0;
    }
    return FMath::Clamp(-FVector3d::DotProduct(aRelM, ab) / lengthSq, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the distance from the origin to the segment's nearest point (+inf for a non-finite input)
double SOLNpcTunnel::LaneSegmentDistanceM(const FVector3d& aRelM, const FVector3d& bRelM)
{
    if (!TunnelIsFinite(aRelM) || !TunnelIsFinite(bRelM))
    {
        return std::numeric_limits<double>::infinity();
    }
    const double param = LaneNearestParam(aRelM, bRelM);
    return (aRelM + (bRelM - aRelM) * param).Size();
}

//////////////////////////////////////////////////////////////////////////
// Returns the local ring spacing: the base spacing near the camera, rho x distance beyond
double SOLNpcTunnel::RingSpacingM(const double axialM, const double radialM, const double tunnelRadiusM)
{
    if (!TunnelIsPositive(tunnelRadiusM) || FMath::IsNaN(axialM) || FMath::IsNaN(radialM))
    {
        return 0.0;
    }
    return FMath::Max(TunnelBaseSpacingM(tunnelRadiusM),
        SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE * FMath::Sqrt(axialM * axialM + radialM * radialM));
}

//////////////////////////////////////////////////////////////////////////
// Returns the ring warp: x / S0 inside the constant zone, the logarithmic (asinh) growth beyond
double SOLNpcTunnel::RingWarp(const double axialM, const double radialM, const double tunnelRadiusM)
{
    if (!TunnelIsPositive(tunnelRadiusM) || FMath::IsNaN(axialM) || FMath::IsNaN(radialM))
    {
        return 0.0;
    }
    const double s0 = TunnelBaseSpacingM(tunnelRadiusM);
    const double x0 = TunnelConstantZoneM(radialM, tunnelRadiusM);
    const double u = FMath::Abs(axialM);
    if (u <= x0)
    {
        return axialM / s0;
    }

    // Beyond the zone: the integral of 1 / (rho sqrt(x^2 + h^2)), in the asinh form (well defined at h = 0, x0 = 0)
    const double h = FMath::Abs(radialM);
    const double logPart = h > 0.0 ? std::asinh(u / h) - std::asinh(x0 / h) : FMath::Loge(u / x0);
    const double magnitude = x0 / s0 + logPart / SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE;
    return axialM < 0.0 ? -magnitude : magnitude;
}

//////////////////////////////////////////////////////////////////////////
// Returns the axial offset of a warp coordinate: w S0 inside the zone, the closed-form inverse beyond
double SOLNpcTunnel::RingWarpInverse(const double w, const double radialM, const double tunnelRadiusM)
{
    if (!TunnelIsPositive(tunnelRadiusM) || FMath::IsNaN(w) || FMath::IsNaN(radialM))
    {
        return 0.0;
    }
    const double s0 = TunnelBaseSpacingM(tunnelRadiusM);
    const double x0 = TunnelConstantZoneM(radialM, tunnelRadiusM);
    const double magnitude = FMath::Abs(w);
    if (magnitude <= x0 / s0)
    {
        return w * s0;
    }

    // u = (v^2 - h^2) / (2 v) with v = c e^t, written as (c expm1(2t) / 2 + x0) e^-t so it does not cancel
    const double c = x0 + FMath::Sqrt(x0 * x0 + radialM * radialM);
    const double t = SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE * (magnitude - x0 / s0);
    const double u = (0.5 * c * std::expm1(2.0 * t) + x0) * std::exp(-t);
    return w < 0.0 ? -u : u;
}

//////////////////////////////////////////////////////////////////////////
// Returns the ring phase advanced by an arc change over the spacing at the foot point (bad input keeps it, wrapped)
double SOLNpcTunnel::AdvanceRingPhase(const double phase, const double deltaArcM, const double radialM,
    const double tunnelRadiusM)
{
    const double wrapped = TunnelWrapPhase(phase);
    const double spacingM = RingSpacingM(0.0, radialM, tunnelRadiusM);
    if (!FMath::IsFinite(deltaArcM) || !(spacingM > 0.0))
    {
        return wrapped;
    }
    return TunnelFrac(wrapped + deltaArcM / spacingM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the deterministic phase of a lane entering range from its foot arc
double SOLNpcTunnel::InitialRingPhase(const double footArcM, const double radialM, const double tunnelRadiusM)
{
    const double spacingM = RingSpacingM(0.0, radialM, tunnelRadiusM);
    if (!FMath::IsFinite(footArcM) || !(spacingM > 0.0))
    {
        return 0.0;
    }
    return TunnelFrac(footArcM / spacingM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the depth fade: 1 near, log-linear to the floor, the floor far away (and for bad input)
double SOLNpcTunnel::TunnelDepthFade(const double distanceM, const double tunnelRadiusM)
{
    const double floorAlpha = SOL::NPC_TUNNEL_DEPTH_FADE_FLOOR;
    if (FMath::IsNaN(distanceM) || !TunnelIsPositive(tunnelRadiusM))
    {
        return floorAlpha;
    }
    const double startM = SOL::NPC_TUNNEL_DEPTH_FADE_START_RADII * tunnelRadiusM;
    if (distanceM <= startM)
    {
        return 1.0;
    }
    if (distanceM >= SOL::NPC_TUNNEL_DEPTH_FADE_END_RADII * tunnelRadiusM)
    {
        return floorAlpha;
    }
    const double t = FMath::Loge(distanceM / startM)
        / FMath::Loge(SOL::NPC_TUNNEL_DEPTH_FADE_END_RADII / SOL::NPC_TUNNEL_DEPTH_FADE_START_RADII);
    return FMath::Clamp(1.0 - (1.0 - floorAlpha) * t, floorAlpha, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the near fade of an element by its axial offset (smoothstep from the start to the full radii)
double SOLNpcTunnel::TunnelNearFade(const double axialOffsetM, const double tunnelRadiusM)
{
    if (FMath::IsNaN(axialOffsetM) || !TunnelIsPositive(tunnelRadiusM))
    {
        return 0.0;
    }
    const double startM = SOL::NPC_TUNNEL_NEAR_FADE_START_RADII * tunnelRadiusM;
    const double widthM = (SOL::NPC_TUNNEL_NEAR_FADE_FULL_RADII - SOL::NPC_TUNNEL_NEAR_FADE_START_RADII)
        * tunnelRadiusM;
    const double t = FMath::Clamp((FMath::Abs(axialOffsetM) - startM) / widthM, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

//////////////////////////////////////////////////////////////////////////
// Returns how much the near fade acts at a radial distance: 1 close to the axis, 0 well outside the tube
double SOLNpcTunnel::TunnelNearFadeRadialWeight(const double radialM, const double tunnelRadiusM)
{
    if (FMath::IsNaN(radialM) || !TunnelIsPositive(tunnelRadiusM))
    {
        return 1.0;
    }
    return TunnelSmoothFall(FMath::Abs(radialM), SOL::NPC_TUNNEL_NEAR_FADE_RADIAL_FULL_RADII * tunnelRadiusM,
        SOL::NPC_TUNNEL_NEAR_FADE_RADIAL_NONE_RADII * tunnelRadiusM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the near fade weighted by the camera's distance from the axis (0 for bad input)
double SOLNpcTunnel::TunnelNearFadeAt(const double axialOffsetM, const double radialM, const double tunnelRadiusM)
{
    if (FMath::IsNaN(axialOffsetM) || FMath::IsNaN(radialM) || !TunnelIsPositive(tunnelRadiusM))
    {
        return 0.0;
    }
    return 1.0 - TunnelNearFadeRadialWeight(radialM, tunnelRadiusM)
        * (1.0 - TunnelNearFade(axialOffsetM, tunnelRadiusM));
}

//////////////////////////////////////////////////////////////////////////
// Returns the dot and chevron fade: 1 inside the start radii, 0 past the end radii, log-linear between
double SOLNpcTunnel::TunnelDetailFade(const double distanceM, const double tunnelRadiusM)
{
    if (FMath::IsNaN(distanceM) || !TunnelIsPositive(tunnelRadiusM))
    {
        return 0.0;
    }
    const double startM = SOL::NPC_TUNNEL_DETAIL_FADE_START_RADII * tunnelRadiusM;
    if (distanceM <= startM)
    {
        return 1.0;
    }
    if (distanceM >= SOL::NPC_TUNNEL_DETAIL_FADE_END_RADII * tunnelRadiusM)
    {
        return 0.0;
    }
    const double t = FMath::Loge(distanceM / startM)
        / FMath::Loge(SOL::NPC_TUNNEL_DETAIL_FADE_END_RADII / SOL::NPC_TUNNEL_DETAIL_FADE_START_RADII);
    return FMath::Clamp(1.0 - t, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the dot and chevron near fade by the element's own distance (smoothstep from the start to the full radius)
double SOLNpcTunnel::TunnelDetailNearFade(const double elementDistanceM, const double tunnelRadiusM)
{
    if (FMath::IsNaN(elementDistanceM) || !TunnelIsPositive(tunnelRadiusM))
    {
        return 0.0;
    }
    const double startM = SOL::NPC_TUNNEL_DETAIL_NEAR_FADE_START_RADII * tunnelRadiusM;
    const double fullM = SOL::NPC_TUNNEL_DETAIL_NEAR_FADE_FULL_RADII * tunnelRadiusM;
    const double t = FMath::Clamp((elementDistanceM - startM) / (fullM - startM), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

//////////////////////////////////////////////////////////////////////////
// Returns clamp(projected radius / (SCREEN_FADE_HALF_EXTENTS x ribbon half extent), 0, 1)
double SOLNpcTunnel::TunnelScreenSizeFade(const double projectedRadiusPx, const double halfExtentPx)
{
    if (FMath::IsNaN(projectedRadiusPx) || !(halfExtentPx > 0.0))
    {
        return 0.0;
    }
    return FMath::Clamp(projectedRadiusPx / (SOL::NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS * halfExtentPx), 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns a ring's screen-size fade: 1 for an end ring (exempt), else of Rt x the pixels per metre at its centre
double SOLNpcTunnel::TunnelRingScreenFade(const bool bEndRing, const double tunnelRadiusM, const double pxPerMAtCentre,
    const double halfExtentPx)
{
    if (bEndRing)
    {
        return 1.0;
    }
    return TunnelScreenSizeFade(tunnelRadiusM * pxPerMAtCentre, halfExtentPx);
}

//////////////////////////////////////////////////////////////////////////
// Returns the chevrons' far fade: 1 inside the start radii, 0 from the end radii, a falling smoothstep between
double SOLNpcTunnel::TunnelChevronFarFade(const double distanceM, const double tunnelRadiusM)
{
    if (FMath::IsNaN(distanceM) || !TunnelIsPositive(tunnelRadiusM))
    {
        return 0.0;
    }
    const double startM = SOL::NPC_TUNNEL_CHEVRON_FADE_START_RADII * tunnelRadiusM;
    const double endM = SOL::NPC_TUNNEL_CHEVRON_FADE_END_RADII * tunnelRadiusM;
    const double t = FMath::Clamp((distanceM - startM) / (endM - startM), 0.0, 1.0);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

//////////////////////////////////////////////////////////////////////////
// Returns the signed distance from the filled dart (negative inside)
double SOLNpcTunnel::ChevronDartDistance(const double x, const double y, const double notchFraction)
{
    // Front edges: tip (0, 1) to the wings (+-1, -1); rear edges: the notch (0, -n) to the wings
    const double ax = FMath::Abs(x);
    const double front = (2.0 * ax + y - 1.0) / FMath::Sqrt(5.0);
    const double rearSlope = 1.0 - notchFraction;
    const double rear = (-rearSlope * ax - y - notchFraction) / FMath::Sqrt(rearSlope * rearSlope + 1.0);
    return FMath::Max(front, rear);
}

//////////////////////////////////////////////////////////////////////////
// Returns the end fade over half a spacing inside an end ring (0 at it and outside, 0 for bad input)
double SOLNpcTunnel::TunnelEndFade(const double distanceToEndM, const double spacingM)
{
    if (FMath::IsNaN(distanceToEndM) || !TunnelIsPositive(spacingM))
    {
        return 0.0;
    }
    return FMath::Clamp(distanceToEndM / (SOL::NPC_TUNNEL_END_FADE_SPACINGS * spacingM), 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the ring cap fade at the far end of the slot range
double SOLNpcTunnel::RingCapFade(const double w)
{
    if (FMath::IsNaN(w))
    {
        return 0.0;
    }
    return FMath::Clamp(static_cast<double>(SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE) - FMath::Abs(w), 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the dot and chevron cap fade: 0 from one gap before the far end of the gap slots
double SOLNpcTunnel::DetailCapFade(const double w)
{
    if (FMath::IsNaN(w))
    {
        return 0.0;
    }
    return FMath::Clamp(static_cast<double>(SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE - 1) - FMath::Abs(w), 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of ring slots (ahead and behind)
int32 SOLNpcTunnel::RingSlotCount()
{
    return 2 * SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
}

//////////////////////////////////////////////////////////////////////////
// Returns ring slot k's ring when it is visible between the end rings
bool SOLNpcTunnel::RingSlot(const int32 slot, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outRing)
{
    if (slot < 0 || slot >= RingSlotCount() || !TunnelIsInputValid(input))
    {
        return false;
    }
    const double w = TunnelRingSlotW(slot, TunnelWrapPhase(input.Phase));
    return TunnelPlaceElement(w, RingCapFade(w), false, input, outRing);
}

//////////////////////////////////////////////////////////////////////////
// Returns ring slot k's axial offset in float arithmetic, as the tunnel shader computes it
float SOLNpcTunnel::RingSlotOffsetM(const int32 slot, const float phase, const float radialM,
    const float tunnelRadiusM)
{
    if (slot < 0 || slot >= RingSlotCount() || !FMath::IsFinite(phase) || !FMath::IsFinite(radialM)
        || !FMath::IsFinite(tunnelRadiusM) || tunnelRadiusM <= 0.0f)
    {
        return 0.0f;
    }

    // The slot's warp coordinate (the phase wrapped into [0, 1)), then the inverse warp, all in float
    const float wrapped = phase - FMath::FloorToFloat(phase);
    const float slotPhase = wrapped >= 1.0f ? 0.0f : wrapped;
    const int32 perSide = SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    const float w = slot < perSide ? static_cast<float>(slot + 1) - slotPhase
        : -(static_cast<float>(slot - perSide) + slotPhase);
    const float rho = static_cast<float>(SOL::NPC_TUNNEL_RING_SPACING_PER_DISTANCE);
    const float s0 = static_cast<float>(SOL::NPC_TUNNEL_RING_BASE_SPACING_RADII) * tunnelRadiusM;
    const float d0 = s0 / rho;
    const float x0 = FMath::Sqrt(FMath::Max(0.0f, d0 * d0 - radialM * radialM));
    const float magnitude = FMath::Abs(w);
    if (magnitude <= x0 / s0)
    {
        return w * s0;
    }
    const float c = x0 + FMath::Sqrt(x0 * x0 + radialM * radialM);
    const float t = rho * (magnitude - x0 / s0);
    const float u = (0.5f * c * (FMath::Exp(2.0f * t) - 1.0f) + x0) * FMath::Exp(-t);
    return w < 0.0f ? -u : u;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of dot and chevron gap slots (ahead and behind)
int32 SOLNpcTunnel::DetailSlotCount()
{
    return 2 * SOL::NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE;
}

//////////////////////////////////////////////////////////////////////////
// Returns a gap slot's dot when it is visible between the end rings
bool SOLNpcTunnel::DotSlot(const int32 slot, const int32 dot, const FSOLNpcTunnelSlotInput& input,
    FSOLNpcTunnelRing& outDot)
{
    if (slot < 0 || slot >= DetailSlotCount() || dot < 0 || dot >= SOL::NPC_TUNNEL_DOTS_PER_GAP
        || !TunnelIsInputValid(input))
    {
        return false;
    }
    const double offset = (static_cast<double>(dot) + 0.5) / static_cast<double>(SOL::NPC_TUNNEL_DOTS_PER_GAP);
    const double w = TunnelGapSlotW(slot, TunnelWrapPhase(input.Phase)) + offset;
    return TunnelPlaceElement(w, DetailCapFade(w), true, input, outDot);
}

//////////////////////////////////////////////////////////////////////////
// Returns a gap slot's chevron (mid-gap) when it is visible between the end rings
bool SOLNpcTunnel::ChevronSlot(const int32 slot, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outChevron)
{
    if (slot < 0 || slot >= DetailSlotCount() || !TunnelIsInputValid(input))
    {
        return false;
    }
    const double w = TunnelGapSlotW(slot, TunnelWrapPhase(input.Phase)) + 0.5;
    if (!TunnelPlaceElement(w, DetailCapFade(w), true, input, outChevron))
    {
        return false;
    }

    // The chevrons' own far fade (D22), by the same camera-to-axis-point distance as the detail fade
    const double axialM = outChevron.AxialOffsetM;
    outChevron.Alpha *= TunnelChevronFarFade(FMath::Sqrt(axialM * axialM + input.RadialM * input.RadialM),
        input.TunnelRadiusM);
    return outChevron.Alpha > 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the end rings' arcs just outside each body, or (0, 0) when the tunnel cannot be drawn
FVector2D SOLNpcTunnel::TunnelEndArcsM(const double laneLengthM, const double bodyRadiusAM, const double bodyRadiusBM,
    const double tunnelRadiusM)
{
    if (!IsTunnelDrawable(laneLengthM, bodyRadiusAM, bodyRadiusBM, tunnelRadiusM))
    {
        return FVector2D::ZeroVector;
    }
    const double marginM = SOL::NPC_TUNNEL_END_MARGIN_RADII * tunnelRadiusM;
    return FVector2D(bodyRadiusAM + marginM, laneLengthM - bodyRadiusBM - marginM);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the end rings are in order (A before B) for finite input and a positive tunnel radius
bool SOLNpcTunnel::IsTunnelDrawable(const double laneLengthM, const double bodyRadiusAM, const double bodyRadiusBM,
    const double tunnelRadiusM)
{
    if (!FMath::IsFinite(laneLengthM) || !FMath::IsFinite(bodyRadiusAM) || !FMath::IsFinite(bodyRadiusBM)
        || !TunnelIsPositive(tunnelRadiusM))
    {
        return false;
    }
    const double marginM = SOL::NPC_TUNNEL_END_MARGIN_RADII * tunnelRadiusM;
    return bodyRadiusAM + marginM < laneLengthM - bodyRadiusBM - marginM;
}

//////////////////////////////////////////////////////////////////////////
// Returns ring sample k on the circle round the centre, clockwise from 12 o'clock
FVector3d SOLNpcTunnel::RingSamplePointM(const FSOLNpcTunnelBasis& basis, const FVector3d& centreM,
    const double radiusM, const int32 sample, const int32 segments)
{
    if (segments <= 0)
    {
        return centreM;
    }
    const double angle = 2.0 * UE_DOUBLE_PI * static_cast<double>(sample) / static_cast<double>(segments);
    return centreM + (basis.Up * FMath::Cos(angle) + basis.Right * FMath::Sin(angle)) * radiusM;
}

//////////////////////////////////////////////////////////////////////////
// Writes the label lanes strongest first by repeated selection (at most the label cap, no allocation)
int32 SOLNpcTunnel::SelectLabelLanes(const TConstArrayView<double> fades, const TConstArrayView<double> distanceRadii,
    const TArrayView<int32> outLanes)
{
    const int32 laneCount = FMath::Min(fades.Num(), distanceRadii.Num());
    const int32 maxCount = FMath::Min(static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_COUNT), outLanes.Num());
    int32 count = 0;
    while (count < maxCount)
    {
        // The best candidate ranked after the previous pick (the ranking is total, so each lane is picked once)
        int32 best = INDEX_NONE;
        for (int32 lane = 0; lane < laneCount; ++lane)
        {
            const double fade = fades[lane];
            const double distance = distanceRadii[lane];
            if (!(fade > 0.0) || FMath::IsNaN(distance))
            {
                continue;
            }
            if (count > 0)
            {
                const int32 previous = outLanes[count - 1];
                if (!TunnelLabelRanksBefore(fades[previous], distanceRadii[previous], previous, fade, distance, lane))
                {
                    continue;
                }
            }
            if (best == INDEX_NONE
                || TunnelLabelRanksBefore(fade, distance, lane, fades[best], distanceRadii[best], best))
            {
                best = lane;
            }
        }
        if (best == INDEX_NONE)
        {
            break;
        }
        outLanes[count++] = best;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns a label's alpha: the fade for the strongest tunnel, dimmed by the side alpha for every other
double SOLNpcTunnel::LaneLabelAlpha(const double fade, const bool bStrongest)
{
    const double clamped = FMath::IsNaN(fade) ? 0.0 : FMath::Clamp(fade, 0.0, 1.0);
    return bStrongest ? clamped : clamped * SOL::NPC_TUNNEL_LABEL_SIDE_ALPHA;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a label of this alpha is placed
bool SOLNpcTunnel::IsLabelPlaced(const double alpha)
{
    return alpha >= SOL::NPC_TUNNEL_LABEL_MIN_ALPHA;
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship's straight-line distances to the two end rings' centres (computed relative to node A)
FVector2D SOLNpcTunnel::LaneEndDistancesM(const FVector3d& shipM, const FSOLNpcLaneEntry& lane,
    const FSOLNpcTunnelBasis& basis, const FVector2D endArcsM)
{
    const FVector3d shipRelM = shipM - lane.A;
    return FVector2D((shipRelM - basis.Axis * endArcsM.X).Size(), (shipRelM - basis.Axis * endArcsM.Y).Size());
}

//////////////////////////////////////////////////////////////////////////
// Writes the 4-row tunnel texture: zeros, then each shown lane's column
void SOLNpcTunnel::WriteTunnelTexels(const TConstArrayView<FSOLNpcTunnelLaneData> lanes,
    const TArrayView<FVector4f> texels)
{
    constexpr int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    if (texels.Num() < 4 * width)
    {
        return;
    }
    for (int32 index = 0; index < 4 * width; ++index)
    {
        texels[index] = FVector4f(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // Each shown lane's column: (foot, Rt), (axis, fade), (up, phase), (end arcs, 0, 0)
    for (const FSOLNpcTunnelLaneData& lane : lanes)
    {
        if (lane.Lane < 0 || lane.Lane >= width)
        {
            continue;
        }
        const int32 column = lane.Lane;
        texels[column] = FVector4f(static_cast<float>(lane.FootRelM.X), static_cast<float>(lane.FootRelM.Y),
            static_cast<float>(lane.FootRelM.Z), static_cast<float>(lane.TunnelRadiusM));
        texels[width + column] = FVector4f(static_cast<float>(lane.Axis.X), static_cast<float>(lane.Axis.Y),
            static_cast<float>(lane.Axis.Z), static_cast<float>(lane.Fade));
        texels[2 * width + column] = FVector4f(static_cast<float>(lane.Up.X), static_cast<float>(lane.Up.Y),
            static_cast<float>(lane.Up.Z), static_cast<float>(lane.Phase));
        texels[3 * width + column] = FVector4f(static_cast<float>(lane.EndArcAM), static_cast<float>(lane.EndArcBM),
            0.0f, 0.0f);
    }
}

//////////////////////////////////////////////////////////////////////////
// Rewrites the shown-lane list when its set changed (index order, allocation kept unless it grows)
bool SOLNpcTunnel::CollectTunnelLanes(const TConstArrayView<double> fades, TArray<int32>& inOutLanes)
{
    // Compare the current set against the list without writing
    int32 matched = 0;
    bool bChanged = false;
    for (int32 lane = 0; lane < fades.Num() && !bChanged; ++lane)
    {
        if (fades[lane] > 0.0)
        {
            bChanged = matched >= inOutLanes.Num() || inOutLanes[matched] != lane;
            ++matched;
        }
    }
    bChanged = bChanged || matched != inOutLanes.Num();
    if (!bChanged)
    {
        return false;
    }

    // Rebuild in index order
    inOutLanes.Reset();
    for (int32 lane = 0; lane < fades.Num(); ++lane)
    {
        if (fades[lane] > 0.0)
        {
            inOutLanes.Add(lane);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the tunnels show: lanes shown, the cvar on and the jump map closed
bool SOLNpcTunnel::AreTunnelsShown(const bool bLanesShown, const bool bCvarOn, const bool bMapOpen)
{
    return bLanesShown && bCvarOn && !bMapOpen;
}

//////////////////////////////////////////////////////////////////////////
// Returns the viewpoint's foot on the axis line of A-B, its arc from A not clamped
FSOLNpcTunnelFoot SOLNpcTunnel::LaneFoot(const FVector3d& aRelM, const FVector3d& bRelM)
{
    FSOLNpcTunnelFoot foot;
    if (!TunnelIsFinite(aRelM) || !TunnelIsFinite(bRelM))
    {
        return foot;
    }
    const FVector3d ab = bRelM - aRelM;
    const double lengthSq = ab.SizeSquared();
    if (!(lengthSq > 0.0) || !FMath::IsFinite(lengthSq))
    {
        return foot;
    }
    const FVector3d axis = ab / FMath::Sqrt(lengthSq);
    foot.ArcM = -FVector3d::DotProduct(aRelM, axis);
    foot.PointRelM = aRelM + axis * foot.ArcM;
    foot.RadialM = foot.PointRelM.Size();
    foot.bValid = true;
    return foot;
}

//////////////////////////////////////////////////////////////////////////
// Returns an end ring's alpha: the depth fade with its floor raised to the end-ring minimum, times the near fade
double SOLNpcTunnel::EndRingAlpha(const double axialOffsetM, const double radialM, const double tunnelRadiusM)
{
    if (FMath::IsNaN(axialOffsetM) || FMath::IsNaN(radialM) || !TunnelIsPositive(tunnelRadiusM))
    {
        return 0.0;
    }
    const double distanceM = FMath::Sqrt(axialOffsetM * axialOffsetM + radialM * radialM);
    return FMath::Max(TunnelDepthFade(distanceM, tunnelRadiusM), SOL::NPC_TUNNEL_END_RING_MIN_ALPHA)
        * TunnelNearFadeAt(axialOffsetM, radialM, tunnelRadiusM);
}

//////////////////////////////////////////////////////////////////////////
// Returns end ring A or B at its arc when the end arcs are in order and its alpha is above 0
bool SOLNpcTunnel::EndRingSlot(const int32 end, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outRing)
{
    if ((end != 0 && end != 1) || !TunnelIsInputValid(input) || !(input.EndArcAM < input.EndArcBM))
    {
        return false;
    }
    const double axialM = end == 0 ? input.EndArcAM : input.EndArcBM;
    outRing.AxialOffsetM = axialM;
    outRing.W = RingWarp(axialM, input.RadialM, input.TunnelRadiusM);
    outRing.Alpha = EndRingAlpha(axialM, input.RadialM, input.TunnelRadiusM);
    return outRing.Alpha > 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the label's side of the foot point, flipping a held side only past the dead band
int32 SOLNpcTunnel::LabelAheadSign(const double forwardDotAxis, const int32 previousSign)
{
    const double band = SOL::NPC_TUNNEL_LABEL_FLIP_HYSTERESIS;
    if (previousSign == 0)
    {
        return forwardDotAxis < 0.0 ? -1 : 1;
    }
    if (previousSign > 0)
    {
        return forwardDotAxis < -band ? -1 : 1;
    }
    return forwardDotAxis > band ? 1 : -1;
}

//////////////////////////////////////////////////////////////////////////
// Returns a label anchor on the tube wall at a clock angle, ahead of the foot point on the given side
FVector3d SOLNpcTunnel::LaneLabelAnchorRelM(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis,
    const double tunnelRadiusM, const int32 aheadSign, const double clockDeg)
{
    if (!basis.bValid || !TunnelIsPositive(tunnelRadiusM))
    {
        return footRelM;
    }
    const double sign = aheadSign < 0 ? -1.0 : 1.0;
    return footRelM + basis.Axis * (sign * SOL::NPC_TUNNEL_LABEL_AHEAD_RADII * tunnelRadiusM)
        + WallDirection(basis, clockDeg) * tunnelRadiusM;
}

//////////////////////////////////////////////////////////////////////////
// Writes a dot row's straight segment between the end rings, cut where the dots end
bool SOLNpcTunnel::TunnelDotRowSegmentRelM(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis,
    const double tunnelRadiusM, const double endArcAM, const double endArcBM, const int32 row, FVector3d& outStartM,
    FVector3d& outEndM)
{
    FVector2D rangeM;
    if (row < 0 || row >= SOL::NPC_TUNNEL_DOT_ROWS
        || !TunnelDotRowRangeM(footRelM, basis, tunnelRadiusM, endArcAM, endArcBM, rangeM))
    {
        return false;
    }
    TunnelWriteDotRow(footRelM, basis, tunnelRadiusM, rangeM, row, outStartM, outEndM);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Writes every dot row's segment of one tunnel (the shared range computed once); returns how many were written
int32 SOLNpcTunnel::TunnelDotRowSegmentsRelM(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis,
    const double tunnelRadiusM, const double endArcAM, const double endArcBM, const TArrayView<FVector3d> outStartsM,
    const TArrayView<FVector3d> outEndsM)
{
    FVector2D rangeM;
    if (!TunnelDotRowRangeM(footRelM, basis, tunnelRadiusM, endArcAM, endArcBM, rangeM))
    {
        return 0;
    }
    const int32 count = FMath::Min3(static_cast<int32>(SOL::NPC_TUNNEL_DOT_ROWS), outStartsM.Num(), outEndsM.Num());
    for (int32 row = 0; row < count; ++row)
    {
        TunnelWriteDotRow(footRelM, basis, tunnelRadiusM, rangeM, row, outStartsM[row], outEndsM[row]);
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Fills a lane's renderer feed and foot; true when the lane can have a tunnel
bool SOLNpcTunnel::BuildTunnelLaneData(const int32 laneIndex, const FSOLNpcLaneEntry& lane,
    const FVector3d& viewpointM, const double bodyRadiusAM, const double bodyRadiusBM,
    FSOLNpcTunnelLaneData& outData, FSOLNpcTunnelFoot& outFoot)
{
    outData = FSOLNpcTunnelLaneData();
    outData.Lane = laneIndex;
    outFoot = FSOLNpcTunnelFoot();
    if (!lane.bResolved)
    {
        return false;
    }
    const FVector3d aRelM = lane.A - viewpointM;
    const FVector3d bRelM = lane.B - viewpointM;
    const FSOLNpcTunnelBasis basis = TunnelBasis(lane.A, lane.B);
    outFoot = LaneFoot(aRelM, bRelM);
    const double tunnelRadiusM = TunnelRadiusM(lane.RadiusM, lane.ShipCount);
    const double laneLengthM = (lane.B - lane.A).Size();
    if (!basis.bValid || !outFoot.bValid
        || !IsTunnelDrawable(laneLengthM, bodyRadiusAM, bodyRadiusBM, tunnelRadiusM))
    {
        return false;
    }

    // The feed: foot point, radius, basis, fade from the segment distance, end arcs relative to the foot arc
    const FVector2D endArcsM = TunnelEndArcsM(laneLengthM, bodyRadiusAM, bodyRadiusBM, tunnelRadiusM);
    outData.FootRelM = outFoot.PointRelM;
    outData.TunnelRadiusM = tunnelRadiusM;
    outData.Axis = basis.Axis;
    outData.Up = basis.Up;
    outData.Fade = TunnelFade(LaneSegmentDistanceM(aRelM, bRelM), lane.RadiusM);
    outData.EndArcAM = endArcsM.X - outFoot.ArcM;
    outData.EndArcBM = endArcsM.Y - outFoot.ArcM;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the next ring phase: the initial phase on entering range, else advanced by the foot arc's change
double SOLNpcTunnel::StepRingPhase(const bool bWasInRange, const double phase, const double previousFootArcM,
    const double footArcM, const double radialM, const double tunnelRadiusM)
{
    if (!bWasInRange)
    {
        return InitialRingPhase(footArcM, radialM, tunnelRadiusM);
    }
    return AdvanceRingPhase(phase, footArcM - previousFootArcM, radialM, tunnelRadiusM);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the tunnels show, the verification override taking the place of the player's lanes
bool SOLNpcTunnel::ResolveTunnelsShown(const bool bOverride, const bool bOverrideShown, const bool bLanesShown,
    const bool bCvarOn, const bool bMapOpen)
{
    return AreTunnelsShown(bOverride ? bOverrideShown : bLanesShown, bCvarOn, bMapOpen);
}

//////////////////////////////////////////////////////////////////////////
// Returns the fade the renderer uses: 0 hidden, 1 forced, else the clamped fade
double SOLNpcTunnel::EffectiveTunnelFade(const double fade, const bool bShown, const bool bForceAllLanes)
{
    if (!bShown)
    {
        return 0.0;
    }
    if (bForceAllLanes)
    {
        return 1.0;
    }
    return FMath::IsNaN(fade) ? 0.0 : FMath::Clamp(fade, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the tunnel component draws: shown and a fade above 0
bool SOLNpcTunnel::ShouldDrawTunnels(const bool bShown, const TConstArrayView<double> fades)
{
    if (!bShown)
    {
        return false;
    }
    for (const double fade : fades)
    {
        if (fade > 0.0)
        {
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the lane overlay counts as drawn for the shared occluders
bool SOLNpcTunnel::IsLaneOverlayDrawn(const bool bLaneLinesDrawn, const bool bTunnelsDrawn)
{
    return bLaneLinesDrawn || bTunnelsDrawn;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the tunnel texture uploads: while a tunnel shows and once more after the last one goes
bool SOLNpcTunnel::ShouldUploadTunnelTexels(const bool bShownNow, const bool bShownLastFrame)
{
    return bShownNow || bShownLastFrame;
}

//////////////////////////////////////////////////////////////////////////
// Returns a ring slot's segment count by its index on its side
int32 SOLNpcTunnel::RingSegmentsForSlot(const int32 slot)
{
    if (slot < 0 || slot >= RingSlotCount())
    {
        return 0;
    }
    const int32 k = slot % SOL::NPC_TUNNEL_RING_SLOTS_PER_SIDE;
    if (k < SOL::NPC_TUNNEL_RING_NEAREST_SLOTS)
    {
        return SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST;
    }
    if (k < SOL::NPC_TUNNEL_RING_NEAR_SLOTS)
    {
        return SOL::NPC_TUNNEL_RING_SEGMENTS_NEAR;
    }
    return k < SOL::NPC_TUNNEL_RING_MID_SLOTS ? SOL::NPC_TUNNEL_RING_SEGMENTS_MID : SOL::NPC_TUNNEL_RING_SEGMENTS_FAR;
}

//////////////////////////////////////////////////////////////////////////
// Returns the slot mesh's vertex count: ring ribbons, two end rings, dot and chevron quads
int32 SOLNpcTunnel::TunnelSlotMeshVertexCount()
{
    int32 count = 2 * 2 * (SOL::NPC_TUNNEL_RING_SEGMENTS_NEAREST + 1);
    for (int32 slot = 0; slot < RingSlotCount(); ++slot)
    {
        count += 2 * (RingSegmentsForSlot(slot) + 1);
    }
    count += 4 * DetailSlotCount() * SOL::NPC_TUNNEL_DOTS_PER_GAP * SOL::NPC_TUNNEL_DOT_ROWS;
    count += 4 * DetailSlotCount() * 2;
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane's tunnel fade from row 1 .w of its column
double SOLNpcTunnel::ReadTunnelFadeTexel(const TConstArrayView<FVector4f> texels, const int32 lane)
{
    constexpr int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    if (lane < 0 || lane >= width || texels.Num() < 2 * width)
    {
        return 0.0;
    }
    return static_cast<double>(texels[width + lane].W);
}
