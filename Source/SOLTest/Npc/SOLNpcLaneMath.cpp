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

    // A lane counts as seen end-on (no side direction) when the sine of the angle between it and the view ray to its
    // front point is below this (the lane material's SOLNL_OutboundSide uses the same value)
    constexpr double LANE_END_ON_SIN = 1.0e-6;

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
    const int32 clampedLevel = FMath::Clamp(level, SOL::NPC_LANE_DASH_MIN_LEVEL, SOL::ORBIT_LINE_DASH_MAX_LEVEL);
    const double levelScale = FMath::Exp2(static_cast<double>(clampedLevel));
    const double sign = direction < 0 ? -1.0 : 1.0;
    return laneParam * static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT) * levelScale - sign * flowPhase;
}

//////////////////////////////////////////////////////////////////////////
// Returns the multi-scale coverage of a flowing lane line (SOLOrbitLineMath::MultiScaleDashCoverage with the flow
// applied after each level's scaling, so the dashes move by the same fraction of their own period at every zoom, and
// with coarser levels down to NPC_LANE_DASH_MIN_LEVEL where the level-0 period is under the minimum)
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

    // Continuous level: how many halvings bring the on-screen period down to the orbit lines' minimum (as theirs), or
    // for a short lane how many doublings bring it up to it (negative, down to the coarsest level)
    const int32 topLevel = FMath::Max(SOL::ORBIT_LINE_DASH_MAX_LEVEL, 0);
    const double periodPx = 1.0 / phaseFootprint;
    const double level = FMath::Clamp(FMath::Log2(periodPx / SOL::ORBIT_LINE_DASH_MIN_PERIOD_PX),
        static_cast<double>(FMath::Min(SOL::NPC_LANE_DASH_MIN_LEVEL, 0)), static_cast<double>(topLevel));
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
// Returns the ribbon side direction: the camera-lane plane's unit normal, signed to the screen right of A -> B
FVector3d SOLNpcLane::LaneOutboundSide(const FVector3d& aCamM, const FVector3d& bCamM, const FVector3d& right,
    const FVector3d& up, const FVector3d& forward)
{
    if (aCamM.ContainsNaN() || bCamM.ContainsNaN() || right.ContainsNaN() || up.ContainsNaN()
        || forward.ContainsNaN())
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d edge = bCamM - aCamM;
    const double edgeLength = edge.Size();
    if (!(edgeLength > 0.0) || !FMath::IsFinite(edgeLength))
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d travel = edge / edgeLength;

    // A point of the lane in front of the camera if there is one: the endpoint farther along the view direction
    const FVector3d& front = FVector3d::DotProduct(aCamM, forward) >= FVector3d::DotProduct(bCamM, forward)
        ? aCamM : bCamM;
    const double depth = FVector3d::DotProduct(front, forward);
    const FVector3d normal = FVector3d::CrossProduct(front, travel);
    const double normalLength = normal.Size();
    if (!(depth > 0.0) || !(normalLength > LANE_END_ON_SIN * front.Size()))
    {
        return FVector3d::ZeroVector;
    }

    // The travel direction on screen (right, up) at that point and its right-hand normal (y, -x); moving a point along
    // +normal moves it on screen toward +(normal . right, normal . up), the screen line's own normal
    const double travelForward = FVector3d::DotProduct(travel, forward);
    const FVector2D screenTravel(
        FVector3d::DotProduct(travel, right) - FVector3d::DotProduct(front, right) / depth * travelForward,
        FVector3d::DotProduct(travel, up) - FVector3d::DotProduct(front, up) / depth * travelForward);
    const FVector2D rightOfTravel(screenTravel.Y, -screenTravel.X);
    const double side = FVector3d::DotProduct(normal, right) * rightOfTravel.X
        + FVector3d::DotProduct(normal, up) * rightOfTravel.Y;
    return normal * ((side >= 0.0 ? 1.0 : -1.0) / normalLength);
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

//////////////////////////////////////////////////////////////////////////
// Returns whether the lanes are shown: the cvar on and, with settings, the Lanes row shown
bool SOLNpcLane::AreLanesShown(const bool bHasSettings, const bool bRowShown, const bool bCvarOn)
{
    return bCvarOn && (!bHasSettings || bRowShown);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the lane component is visible: lanes shown and at least one drawn lane
bool SOLNpcLane::ShouldDrawLaneLines(const bool bLanesShown, const FSOLNpcLaneFrame& frame)
{
    if (!bLanesShown)
    {
        return false;
    }
    return frame.Lanes.ContainsByPredicate([](const FSOLNpcLaneEntry& entry)
    {
        return entry.bDrawn;
    });
}

//////////////////////////////////////////////////////////////////////////
// Returns the far-point material's crossfade vector (lanes shown, fade near and far radii, 0)
FLinearColor SOLNpcLane::PointLaneParams(const bool bLanesShown)
{
    return FLinearColor(bLanesShown ? 1.0f : 0.0f, static_cast<float>(SOL::NPC_LANE_FADE_NEAR_RADII),
        static_cast<float>(SOL::NPC_LANE_FADE_FAR_RADII), 0.0f);
}

//////////////////////////////////////////////////////////////////////////
// Returns the lane material's per-frame vector (flow phase, 0 unused, fade near and far radii)
FLinearColor SOLNpcLane::LaneMaterialParams(const double flowPhase)
{
    return FLinearColor(FMath::IsFinite(flowPhase) ? static_cast<float>(flowPhase) : 0.0f, 0.0f,
        static_cast<float>(SOL::NPC_LANE_FADE_NEAR_RADII), static_cast<float>(SOL::NPC_LANE_FADE_FAR_RADII));
}

//////////////////////////////////////////////////////////////////////////
// Writes each lane's radius (row 0) and weight or -1 (row 1) into the group texels' .w; other columns get 0
void SOLNpcLane::WriteLaneGroupTexelW(const FSOLNpcLaneFrame& frame, const TArrayView<FVector4f> groupTexels)
{
    constexpr int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    if (groupTexels.Num() < 2 * width)
    {
        return;
    }
    const int32 laneCount = FMath::Min(frame.Lanes.Num(), width);
    for (int32 column = 0; column < width; ++column)
    {
        float radius = 0.0f;
        float weight = 0.0f;
        if (column < laneCount)
        {
            const FSOLNpcLaneEntry& entry = frame.Lanes[column];
            radius = static_cast<float>(entry.RadiusM);
            weight = entry.bDrawn ? static_cast<float>(entry.TrafficWeight) : -1.0f;
        }
        groupTexels[column].W = radius;
        groupTexels[width + column].W = weight;
    }
}

//////////////////////////////////////////////////////////////////////////
// Adds the lane rails of a slice to the per-lane counts (groups past the counts ignored, no reset)
void SOLNpcLane::AccumulateLaneShips(const TConstArrayView<FSOLNpcRail> rails, const TArrayView<int32> inOutCounts)
{
    const int32 laneCount = inOutCounts.Num();
    if (laneCount <= 0)
    {
        return;
    }

    // Counted on the stack (the texture's group columns), added to the caller's counts once at the end; a group past
    // the texture width (none in a real layout) is counted in place
    constexpr int32 localWidth = SOL::NPC_GROUP_TEXTURE_WIDTH;
    int32 localCounts[localWidth] = {};
    const uint8 laneKind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
    for (const FSOLNpcRail& rail : rails)
    {
        const int32 group = static_cast<int32>(rail.Group);
        if (rail.Kind == laneKind && group < laneCount)
        {
            if (group < localWidth)
            {
                ++localCounts[group];
            }
            else
            {
                ++inOutCounts[group];
            }
        }
    }
    const int32 localLanes = FMath::Min(laneCount, localWidth);
    for (int32 lane = 0; lane < localLanes; ++lane)
    {
        inOutCounts[lane] += localCounts[lane];
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the flow clock advanced by a real frame time (held only for a bad delta; a bad clock restarts)
double SOLNpcLane::AdvanceLaneFlowClockS(const double clockS, const double realDeltaS)
{
    const double base = FMath::IsFinite(clockS) ? clockS : 0.0;
    if (!FMath::IsFinite(realDeltaS) || realDeltaS <= 0.0)
    {
        return base;
    }
    return base + realDeltaS;
}

//////////////////////////////////////////////////////////////////////////
// Returns the ribbon vertex count of a number of lanes (two lines, two sides per sample)
int32 SOLNpcLane::LaneRibbonVertexCount(const int32 laneCount)
{
    return laneCount > 0 ? laneCount * 2 * (SOL::NPC_LANE_SEGMENTS + 1) * 2 : 0;
}

//////////////////////////////////////////////////////////////////////////
// Lays out the ribbon strips of every lane with ships: UVs per vertex and two triangles per segment
void SOLNpcLane::BuildLaneRibbonLayout(const TConstArrayView<int32> laneShipCounts, TArray<FVector2f>& outUv0,
    TArray<FVector2f>& outUv1, TArray<uint32>& outIndices)
{
    constexpr int32 segments = SOL::NPC_LANE_SEGMENTS;
    int32 drawnLanes = 0;
    for (const int32 count : laneShipCounts)
    {
        drawnLanes += IsLaneDrawn(count) ? 1 : 0;
    }
    outUv0.Reset(LaneRibbonVertexCount(drawnLanes));
    outUv1.Reset(LaneRibbonVertexCount(drawnLanes));
    outIndices.Reset(drawnLanes * 2 * segments * 6);

    // One strip per line: samples 0..segments, side -1 then +1 at each
    for (int32 lane = 0; lane < laneShipCounts.Num(); ++lane)
    {
        if (!IsLaneDrawn(laneShipCounts[lane]))
        {
            continue;
        }
        for (int32 line = 0; line < 2; ++line)
        {
            const uint32 first = static_cast<uint32>(outUv0.Num());
            const FVector2f strip(static_cast<float>(lane), static_cast<float>(line));
            for (int32 sample = 0; sample <= segments; ++sample)
            {
                const float u = static_cast<float>(sample) / static_cast<float>(segments);
                outUv0.Add(FVector2f(u, -1.0f));
                outUv0.Add(FVector2f(u, 1.0f));
                outUv1.Add(strip);
                outUv1.Add(strip);
            }
            for (uint32 segment = 0; segment < static_cast<uint32>(segments); ++segment)
            {
                const uint32 v0 = first + 2 * segment;
                outIndices.Append({ v0, v0 + 2, v0 + 1, v0 + 1, v0 + 2, v0 + 3 });
            }
        }
    }
}
