/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcLaneMath.h"

#include "OrbitLines/SOLOrbitLineMath.h"
#include "SOLConstants.h"
#include "UI/SOLHudShapeMath.h"

namespace
{
    // A ray counts as parallel to the lane when the squared sine of their angle is below this: the closest-approach
    // parameter is then undefined and the segment point nearest the origin is taken
    constexpr double LANE_PARALLEL_SIN_SQ = 1.0e-20;

    //////////////////////////////////////////////////////////////////////////
    // Returns the viewport scale height / NPC_LANE_REFERENCE_HEIGHT_PX (1 for a non-positive or non-finite height)
    double LaneHeightScale(const double viewportHeightPx)
    {
        return FMath::IsFinite(viewportHeightPx) && viewportHeightPx > 0.0
            ? viewportHeightPx / SOL::NPC_LANE_REFERENCE_HEIGHT_PX : 1.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a traffic weight clamped to [0, 1] with NaN read as 0
    double LaneClampedWeight(const double weight)
    {
        return FMath::IsNaN(weight) ? 0.0 : FMath::Clamp(weight, 0.0, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the squared distance from a point to the ray from the origin along a unit direction (t >= 0)
    double LaneRayDistanceSq(const FVector3d& pointM, const FVector3d& unitDirection)
    {
        const double along = FMath::Max(0.0, FVector3d::DotProduct(pointM, unitDirection));
        return (pointM - unitDirection * along).SizeSquared();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the line's close-up fade: 0 inside NEAR x R, 1 past FAR x R, linear in log d between; bad input keeps it (1)
double SOLNpcLane::LaneLineFade(const double distanceM, const double laneRadiusM)
{
    if (FMath::IsNaN(distanceM) || !FMath::IsFinite(laneRadiusM) || laneRadiusM <= 0.0)
    {
        return 1.0;
    }
    const double nearM = SOL::NPC_LANE_FADE_NEAR_RADII * laneRadiusM;
    if (distanceM <= nearM)
    {
        return 0.0;
    }
    if (distanceM >= SOL::NPC_LANE_FADE_FAR_RADII * laneRadiusM)
    {
        return 1.0;
    }
    const double fade = FMath::Loge(distanceM / nearM)
        / FMath::Loge(SOL::NPC_LANE_FADE_FAR_RADII / SOL::NPC_LANE_FADE_NEAR_RADII);
    return FMath::Clamp(fade, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane ship's far-point alpha: the line fade's complement while the lanes are shown, else 1
double SOLNpcLane::LaneShipPointAlpha(const double distanceM, const double laneRadiusM, const bool bLanesShown)
{
    return bLanesShown ? 1.0 - LaneLineFade(distanceM, laneRadiusM) : 1.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the log-linear traffic weight of a ship count, clamped to [0, 1] (0 for no ships)
double SOLNpcLane::LaneTrafficWeight(const int32 shipCount)
{
    if (shipCount <= 0)
    {
        return 0.0;
    }
    const double low = static_cast<double>(SOL::NPC_LANE_TRAFFIC_LOW);
    const double high = static_cast<double>(SOL::NPC_LANE_TRAFFIC_HIGH);
    const double weight = FMath::Loge(static_cast<double>(shipCount) / low) / FMath::Loge(high / low);
    return FMath::Clamp(weight, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a lane with this ship count is drawn (at least one ship)
bool SOLNpcLane::IsLaneDrawn(const int32 shipCount)
{
    return shipCount >= 1;
}

//////////////////////////////////////////////////////////////////////////
// Returns each line's width in pixels from the traffic weight, scaled by the viewport height
double SOLNpcLane::LaneLineWidthPx(const double weight, const double viewportHeightPx)
{
    return FMath::Lerp(SOL::NPC_LANE_WIDTH_MIN_PX, SOL::NPC_LANE_WIDTH_MAX_PX, LaneClampedWeight(weight))
        * LaneHeightScale(viewportHeightPx);
}

//////////////////////////////////////////////////////////////////////////
// Returns the line brightness from the traffic weight
double SOLNpcLane::LaneLineBrightness(const double weight)
{
    return FMath::Lerp(SOL::NPC_LANE_BRIGHTNESS_MIN, SOL::NPC_LANE_BRIGHTNESS_MAX, LaneClampedWeight(weight));
}

//////////////////////////////////////////////////////////////////////////
// Returns each line's centre offset from the lane's centre line: half the width plus half the scaled gap
double SOLNpcLane::LaneLineCenterOffsetPx(const double widthPx, const double viewportHeightPx)
{
    return 0.5 * (widthPx + SOL::NPC_LANE_GAP_PX * LaneHeightScale(viewportHeightPx));
}

//////////////////////////////////////////////////////////////////////////
// Returns a line ribbon's half extent: its edge where the height-scaled glow has its reference-height pad value
double SOLNpcLane::LaneRibbonHalfExtentPx(const double widthPx, const double viewportHeightPx)
{
    const double halfWidth = FMath::Max(widthPx, 1.0) * 0.5;
    const double glowReach = (SOL::NPC_LANE_GLOW_PAD_PX + SOL::Hologram::HUD_GLOW_CORE_HALF_PX)
        * LaneHeightScale(viewportHeightPx);
    return FMath::Max(halfWidth, halfWidth - SOL::Hologram::HUD_GLOW_CORE_HALF_PX + glowReach);
}

//////////////////////////////////////////////////////////////////////////
// Returns the profile across one line: the HUD glow profile shifted out by the half width, its distance in reference
// pixels (D25), times min(width, 1)
double SOLNpcLane::LaneLineProfile(const double distanceFromCenterPx, const double widthPx, const double glowScale,
    const double viewportHeightPx)
{
    if (!FMath::IsFinite(distanceFromCenterPx) || !FMath::IsFinite(widthPx) || widthPx <= 0.0)
    {
        return 0.0;
    }
    const double halfCore = FMath::Max(widthPx, 1.0) * 0.5 - SOL::Hologram::HUD_GLOW_CORE_HALF_PX;
    const double shiftedRef = FMath::Max(0.0, FMath::Abs(distanceFromCenterPx) - halfCore)
        / LaneHeightScale(viewportHeightPx);
    const float g = SOLHudShapeMath::SanitizeGlowScale(static_cast<float>(glowScale));
    return static_cast<double>(SOLHudShapeMath::GlowProfile(static_cast<float>(shiftedRef), g))
        * FMath::Min(widthPx, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the wrapped dash flow phase frac(FLOW x t) in [0, 1); 0 for a NaN or infinite time
double SOLNpcLane::LaneDashFlowPhase(const double timeS)
{
    if (!FMath::IsFinite(timeS))
    {
        return 0.0;
    }
    const double periods = SOL::NPC_LANE_DASH_FLOW_PERIODS_PER_S * timeS;
    const double phase = periods - FMath::FloorToDouble(periods);

    // A tiny negative time rounds up to exactly 1, which is 0 modulo 1
    return phase < 1.0 ? phase : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns a level's flowing dash phase: s x BASE x 2^level - sign(direction) x flowPhase
double SOLNpcLane::LaneDashPhase(const double laneParam, const int32 level, const double flowPhase,
    const int32 direction)
{
    const int32 clampedLevel = FMath::Clamp(level, 0, SOL::ORBIT_LINE_DASH_MAX_LEVEL);
    const double levelScale = FMath::Exp2(static_cast<double>(clampedLevel));
    const double sign = direction < 0 ? -1.0 : 1.0;
    return laneParam * static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT) * levelScale - sign * flowPhase;
}

//////////////////////////////////////////////////////////////////////////
// Returns the multi-scale coverage of a flowing lane line (SOLOrbitLineMath::MultiScaleDashCoverage with the flow
// applied after each level's scaling, so the dashes move by the same fraction of their own period at every zoom)
double SOLNpcLane::LaneDashCoverage(const double laneParam, const double flowPhase, const int32 direction,
    const double phaseFootprint)
{
    if (!FMath::IsFinite(laneParam) || !FMath::IsFinite(flowPhase))
    {
        return 0.0;
    }
    if (!FMath::IsFinite(phaseFootprint) || phaseFootprint <= 0.0)
    {
        return SOLOrbitLineMath::DashCoverage(LaneDashPhase(laneParam, 0, flowPhase, direction),
            SOL::NPC_LANE_DASH_DUTY, phaseFootprint);
    }

    // Continuous level: how many halvings bring the on-screen period down to the orbit lines' minimum (as theirs)
    const int32 topLevel = FMath::Max(SOL::ORBIT_LINE_DASH_MAX_LEVEL, 0);
    const double periodPx = 1.0 / phaseFootprint;
    const double level = FMath::Clamp(FMath::Log2(periodPx / SOL::ORBIT_LINE_DASH_MIN_PERIOD_PX), 0.0,
        static_cast<double>(topLevel));
    const int32 lower = FMath::Min(static_cast<int32>(FMath::FloorToDouble(level)), topLevel);
    const double blend = level - static_cast<double>(lower);
    const double lowerScale = FMath::Exp2(static_cast<double>(lower));
    const double lowerCoverage = SOLOrbitLineMath::DashCoverage(LaneDashPhase(laneParam, lower, flowPhase, direction),
        SOL::NPC_LANE_DASH_DUTY, phaseFootprint * lowerScale);
    double coverage = lowerCoverage;
    if (blend > 0.0 && lower < topLevel)
    {
        const double upperCoverage = SOLOrbitLineMath::DashCoverage(LaneDashPhase(laneParam, lower + 1, flowPhase,
            direction), SOL::NPC_LANE_DASH_DUTY, phaseFootprint * 2.0 * lowerScale);
        coverage = FMath::Lerp(lowerCoverage, upperCoverage, blend);
    }

    // Past the last level the period keeps growing: fade to a solid line so it never sits in a gap
    const double remainingPeriodPx = periodPx / FMath::Exp2(level);
    double solid = 0.0;
    if (SOL::ORBIT_LINE_DASH_SOLID_END_PX > SOL::ORBIT_LINE_DASH_SOLID_START_PX)
    {
        solid = FMath::Clamp((remainingPeriodPx - SOL::ORBIT_LINE_DASH_SOLID_START_PX)
            / (SOL::ORBIT_LINE_DASH_SOLID_END_PX - SOL::ORBIT_LINE_DASH_SOLID_START_PX), 0.0, 1.0);
    }
    else
    {
        solid = remainingPeriodPx >= SOL::ORBIT_LINE_DASH_SOLID_START_PX ? 1.0 : 0.0;
    }
    return FMath::Lerp(coverage, 1.0, solid);
}

//////////////////////////////////////////////////////////////////////////
// Returns the parameter of the point on segment A-B closest to the view ray from the origin (bad input: 0)
double SOLNpcLane::ClosestLaneParam(const FVector3d& aRelM, const FVector3d& bRelM, const FVector3d& rayDirection)
{
    if (aRelM.ContainsNaN() || bRelM.ContainsNaN() || rayDirection.ContainsNaN())
    {
        return 0.0;
    }
    const double rayLength = rayDirection.Size();
    const FVector3d edge = bRelM - aRelM;
    const double edgeSq = edge.SizeSquared();
    if (!(rayLength > 0.0) || !(edgeSq > 0.0))
    {
        return 0.0;
    }
    const FVector3d direction = rayDirection / rayLength;

    // The segment point nearest the origin: the closest point where the ray's own closest point is its origin, and
    // the answer for a ray parallel to the lane
    const double nearest = FMath::Clamp(-FVector3d::DotProduct(aRelM, edge) / edgeSq, 0.0, 1.0);

    // The two lines' closest approach, solved in the plane across the ray (each vector minus its part along the ray)
    const FVector3d acrossA = aRelM - direction * FVector3d::DotProduct(aRelM, direction);
    const FVector3d acrossEdge = edge - direction * FVector3d::DotProduct(edge, direction);
    const double acrossSq = acrossEdge.SizeSquared();
    if (acrossSq <= edgeSq * LANE_PARALLEL_SIN_SQ)
    {
        return nearest;
    }
    const double onLine = FMath::Clamp(-FVector3d::DotProduct(acrossA, acrossEdge) / acrossSq, 0.0, 1.0);

    // The distance to the ray is convex along the segment, so its minimum is one of these candidates
    double best = onLine;
    double bestSq = LaneRayDistanceSq(aRelM + edge * onLine, direction);
    for (const double candidate : { nearest, 0.0, 1.0 })
    {
        const double distanceSq = LaneRayDistanceSq(aRelM + edge * candidate, direction);
        if (distanceSq < bestSq)
        {
            best = candidate;
            bestSq = distanceSq;
        }
    }
    return best;
}

//////////////////////////////////////////////////////////////////////////
// Returns the camera distance of the lane point at a parameter (clamped to [0, 1], NaN as 0)
double SOLNpcLane::LaneDistanceAtParamM(const FVector3d& aRelM, const FVector3d& bRelM, const double laneParam)
{
    const double param = FMath::IsNaN(laneParam) ? 0.0 : FMath::Clamp(laneParam, 0.0, 1.0);
    return (aRelM + (bRelM - aRelM) * param).Size();
}

//////////////////////////////////////////////////////////////////////////
// Returns the outbound line's screen side: the unit normal on the right of A -> B (x right, y down)
FVector2D SOLNpcLane::LaneOutboundSideNormal(const FVector2D& screenA, const FVector2D& screenB)
{
    const FVector2D travel = screenB - screenA;
    if (!FMath::IsFinite(travel.X) || !FMath::IsFinite(travel.Y))
    {
        return FVector2D::ZeroVector;
    }
    const double length = travel.Size();
    if (!(length > 0.0))
    {
        return FVector2D::ZeroVector;
    }
    return FVector2D(-travel.Y / length, travel.X / length);
}

//////////////////////////////////////////////////////////////////////////
// Counts the lane rails per lane (laneCount entries; other kinds and out-of-range groups ignored)
void SOLNpcLane::CountLaneShips(const TConstArrayView<FSOLNpcRail> rails, const int32 laneCount,
    TArray<int32>& outCounts)
{
    outCounts.Reset();
    if (laneCount <= 0)
    {
        return;
    }
    outCounts.SetNumZeroed(laneCount);
    const uint8 laneKind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
    for (const FSOLNpcRail& rail : rails)
    {
        const int32 group = static_cast<int32>(rail.Group);
        if (rail.Kind == laneKind && group < laneCount)
        {
            ++outCounts[group];
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Fills the per-frame lane entries from the group points, radii and counts (the array kept at an unchanged size)
void SOLNpcLane::BuildLaneFrame(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame,
    const TConstArrayView<int32> counts, FSOLNpcLaneFrame& outFrame)
{
    const int32 laneCount = FMath::Max(groups.LaneCount, 0);
    outFrame.Lanes.SetNum(laneCount, EAllowShrinking::No);
    const int32 framePoints = FMath::Min(frame.GroupPointAM.Num(), frame.GroupPointBM.Num());
    for (int32 lane = 0; lane < laneCount; ++lane)
    {
        // A lane past the frame arrays or the counts has no ships this frame
        FSOLNpcLaneEntry& entry = outFrame.Lanes[lane];
        const bool bInFrame = lane < framePoints;
        entry.A = bInFrame ? frame.GroupPointAM[lane] : FVector3d::ZeroVector;
        entry.B = bInFrame ? frame.GroupPointBM[lane] : FVector3d::ZeroVector;
        entry.RadiusM = groups.LaneRadiusM.IsValidIndex(lane) ? groups.LaneRadiusM[lane] : 0.0;
        entry.ShipCount = bInFrame && lane < counts.Num() ? counts[lane] : 0;
        entry.TrafficWeight = LaneTrafficWeight(entry.ShipCount);

        // Drawn with ships on it and both bodies resolved
        const bool bBodies = groups.LaneBodyA.IsValidIndex(lane) && groups.LaneBodyA[lane] != INDEX_NONE
            && groups.LaneBodyB.IsValidIndex(lane) && groups.LaneBodyB[lane] != INDEX_NONE;
        entry.bDrawn = bBodies && IsLaneDrawn(entry.ShipCount);
    }
}
