/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcRail.h"

#include "CoreMinimal.h"

// One lane of the per-frame lane frame (SDD 38 section 4.2): its endpoints (the two body centres, universe metres),
// radius, ship count, traffic weight and whether it is drawn (at least one ship and both bodies resolved)
struct FSOLNpcLaneEntry
{
    FVector3d A = FVector3d::ZeroVector;     // Body A's centre (outbound traffic leaves it)
    FVector3d B = FVector3d::ZeroVector;     // Body B's centre
    double RadiusM = 0.0;                    // Lane radius (FSOLNpcGroupTable::LaneRadiusM)
    int32 ShipCount = 0;                     // Lane rails of the population
    double TrafficWeight = 0.0;              // SOLNpcLane::LaneTrafficWeight(ShipCount)
    bool bDrawn = false;                     // Ships on it and both bodies resolved
};

// The lanes of one frame, in lane order (global groups 0 .. LaneCount - 1). Built each frame by
// SOLNpcLane::BuildLaneFrame without allocating once sized; the extension point for issue #49 (SDD 38 section 4.10)
struct FSOLNpcLaneFrame
{
    TArray<FSOLNpcLaneEntry> Lanes;
};

// Pure math of the NPC lane lines (SDD 38 sections 4.2-4.7, contract section 6): the close-up fade and the lane ships'
// point crossfade, width and brightness from the ship count, the two lines' screen offsets and glow profile, the
// flowing world-anchored dashes, the analytic lane parameter under a pixel, the "keep right" side rule, the ship
// histogram and the per-frame lane frame. The lane and point materials' HLSL mirrors these functions; a change to one
// must change the other
namespace SOLNpcLane
{
    // Returns the line's close-up fade: 0 at and inside NPC_LANE_FADE_NEAR_RADII x R (also for d <= 0), 1 at and past
    // NPC_LANE_FADE_FAR_RADII x R, linear in log d between; a NaN distance or a NaN, non-positive or infinite radius
    // returns 1 (no close-up band, the line stays)
    SOLTEST_API double LaneLineFade(double distanceM, double laneRadiusM);

    // Returns a lane ship's far-point alpha: 1 - LaneLineFade(d, R) while the lanes are shown (they sum to 1), else 1
    SOLTEST_API double LaneShipPointAlpha(double distanceM, double laneRadiusM, bool bLanesShown);

    // Returns the traffic weight of a lane's ship count: log-linear from NPC_LANE_TRAFFIC_LOW (0) to
    // NPC_LANE_TRAFFIC_HIGH (1), clamped; 0 for a count <= 0
    SOLTEST_API double LaneTrafficWeight(int32 shipCount);

    // Returns whether a lane with this ship count is drawn (at least one ship)
    SOLTEST_API bool IsLaneDrawn(int32 shipCount);

    // Returns each line's width: lerp(NPC_LANE_WIDTH_MIN_PX, NPC_LANE_WIDTH_MAX_PX, clamped weight, NaN as 0) scaled by
    // the viewport height against NPC_LANE_REFERENCE_HEIGHT_PX (a non-positive or non-finite height reads as it)
    SOLTEST_API double LaneLineWidthPx(double weight, double viewportHeightPx);

    // Returns the line brightness: lerp(NPC_LANE_BRIGHTNESS_MIN, NPC_LANE_BRIGHTNESS_MAX, clamped weight, NaN as 0)
    SOLTEST_API double LaneLineBrightness(double weight);

    // Returns each line's centre offset from the lane's projected centre line: 0.5 x (width + scaled NPC_LANE_GAP_PX),
    // so the two cores never overlap (a bad height reads as the reference)
    SOLTEST_API double LaneLineCenterOffsetPx(double widthPx, double viewportHeightPx);

    // Returns a line ribbon's half extent (SDD 38 D25): its edge where the height-scaled glow has its reference-height
    // pad value, max(w, 1) / 2 - HUD_GLOW_CORE_HALF_PX + (NPC_LANE_GLOW_PAD_PX + HUD_GLOW_CORE_HALF_PX) x scale, never
    // less than max(w, 1) / 2 (max(w, 1) / 2 + NPC_LANE_GLOW_PAD_PX at the reference; a bad height reads as it)
    SOLTEST_API double LaneRibbonHalfExtentPx(double widthPx, double viewportHeightPx);

    // Returns the profile across one line: SOLHudShapeMath::GlowProfile(d', g) x min(width, 1) with
    // d' = max(0, |d| - (max(width, 1) / 2 - HUD_GLOW_CORE_HALF_PX)) / scale (the HUD glow shifted out by the half
    // width, its distance in reference pixels so it looks the same at every height, D25; g sanitized like the HUD's; a
    // bad height reads as the reference); 0 for a non-finite distance or a non-finite or non-positive width
    SOLTEST_API double LaneLineProfile(double distanceFromCenterPx, double widthPx, double glowScale,
        double viewportHeightPx);

    // Returns the dash flow phase frac(NPC_LANE_DASH_FLOW_PERIODS_PER_S x t) in [0, 1), computed in double (a negative
    // time wraps up); 0 for a NaN or infinite time
    SOLTEST_API double LaneDashFlowPhase(double timeS);

    // Returns a level's dash phase: s x NPC_LANE_DASH_BASE_COUNT x 2^level - sign(direction) x flowPhase (level clamped
    // to [NPC_LANE_DASH_MIN_LEVEL, ORBIT_LINE_DASH_MAX_LEVEL], direction 0 as +1), so outbound dashes move toward B and
    // inbound ones toward A
    SOLTEST_API double LaneDashPhase(double laneParam, int32 level, double flowPhase, int32 direction);

    // Returns the dash coverage of a flowing lane line at a single whole level: the orbit lines' continuous level
    // (ORBIT_LINE_DASH_MIN_PERIOD_PX, ORBIT_LINE_DASH_MAX_LEVEL) rounded down to the coarser size, box-filtered at that
    // level only (no blend toward the finer size, so no dimmer dash copy; the dashes jump to double or half length as
    // the on-screen period crosses a size step), the flow applied after the level's scaling, then the orbit lines' fade
    // to solid past the last level. Where the level-0 period is under the minimum the levels go coarser, down to
    // NPC_LANE_DASH_MIN_LEVEL, so a lane short on screen still shows dashes. The footprint is one pixel in level-0 dash
    // periods, and a NaN or non-positive one is a level-0 point sample
    SOLTEST_API double LaneDashCoverage(double laneParam, double flowPhase, int32 direction, double phaseFootprint);

    // Returns the parameter in [0, 1] of the point on segment A-B (camera at the origin) closest to the ray from the
    // origin along rayDirection (normalized inside); a ray parallel to A-B takes the segment point nearest the origin;
    // 0 for a degenerate segment, a zero ray or a non-finite input
    SOLTEST_API double ClosestLaneParam(const FVector3d& aRelM, const FVector3d& bRelM, const FVector3d& rayDirection);

    // Returns |A + s (B - A)| with s clamped to [0, 1] (NaN as 0): the camera distance of a lane point
    SOLTEST_API double LaneDistanceAtParamM(const FVector3d& aRelM, const FVector3d& bRelM, double laneParam);

    // Returns the outbound line's side on screen (x right, y down): the unit normal on the right of the travel
    // direction A -> B ("keep right"; the inbound line uses the negated normal); (0, 0) for A == B or a non-finite
    // input
    SOLTEST_API FVector2D LaneOutboundSideNormal(const FVector2D& screenA, const FVector2D& screenB);

    // Returns the lane ribbon's side direction (unit; endpoints relative to the camera, the camera's right, up and
    // forward in the same axes): the normal of the plane through the camera and the lane, so it is perpendicular to
    // every view ray of the lane, signed so that moving a lane point along it moves it on screen to the right of the
    // travel A -> B (LaneOutboundSideNormal, "keep right"; the outbound line is offset along it, the inbound one
    // against it). Zero when the lane is wholly behind the camera, seen end-on, degenerate or not finite. The lane
    // material's SOLNL_OutboundSide mirrors it
    SOLTEST_API FVector3d LaneOutboundSide(const FVector3d& aCamM, const FVector3d& bCamM, const FVector3d& right,
        const FVector3d& up, const FVector3d& forward);

    // Counts the lane rails per lane: outCounts is reset to laneCount zeros (empty for laneCount <= 0) and each rail of
    // kind Lane with a group below laneCount adds one to its group; cluster and battle rails are ignored
    SOLTEST_API void CountLaneShips(TConstArrayView<FSOLNpcRail> rails, int32 laneCount, TArray<int32>& outCounts);

    // Fills one entry per lane (groups.LaneCount) from the frame's group points, the lane radii and the ship counts;
    // a lane past the end of the counts or of the frame arrays has count 0 and is not drawn. Keeps outFrame's array
    // when its size does not change (no allocation per frame)
    SOLTEST_API void BuildLaneFrame(const FSOLNpcGroupTable& groups, const FSOLNpcRailFrame& frame,
        TConstArrayView<int32> counts, FSOLNpcLaneFrame& outFrame);

    // Part 38b, the renderer's pure feed (SDD 38 sections 4.2, 4.5, 4.6 and 4.8)

    // Returns whether the lanes are shown: sol.NpcLanes on, and the Lanes row shown (IsOverlayShown, which folds in
    // the master O) when there is a settings subsystem; without one they are on
    SOLTEST_API bool AreLanesShown(bool bHasSettings, bool bRowShown, bool bCvarOn);

    // Returns the lane component's visibility: the lanes shown and at least one lane of the frame drawn
    SOLTEST_API bool ShouldDrawLaneLines(bool bLanesShown, const FSOLNpcLaneFrame& frame);

    // Returns the far-point material's NpcParams4: (lanes shown 0 / 1, fade near radii, fade far radii, 0), the inputs
    // of the lane ships' crossfade (LaneShipPointAlpha)
    SOLTEST_API FLinearColor PointLaneParams(bool bLanesShown);

    // Returns the lane material's NpcLaneParams0: (flow phase, 0 unused, fade near radii, fade far radii); a non-finite
    // phase reads as 0. The shader scales by its own view height (SDD 38 section 4.12), so no viewport scale is sent
    SOLTEST_API FLinearColor LaneMaterialParams(double flowPhase);

    // Writes the lane data into the group texels' free .w channels (row 0 = the first NPC_GROUP_TEXTURE_WIDTH texels,
    // row 1 the next): for a lane column, row 0 .w = its radius (m) and row 1 .w = its traffic weight, or -1 when it is
    // not drawn; every other column 0. x, y and z are not touched; a buffer shorter than two rows is left alone
    SOLTEST_API void WriteLaneGroupTexelW(const FSOLNpcLaneFrame& frame, TArrayView<FVector4f> groupTexels);

    // Adds one per lane rail whose group is below inOutCounts.Num() (no reset): one rail-fill task's share of the lane
    // histogram, so per-task counts over any partition of the rails, merged, equal CountLaneShips. Counts on the stack
    // and writes inOutCounts once at the end, so tasks whose count arrays share a cache line do not contend
    SOLTEST_API void AccumulateLaneShips(TConstArrayView<FSOLNpcRail> rails, TArrayView<int32> inOutCounts);

    // Returns the dash flow's real-time clock advanced by a real (warp-free) frame time; it never stops (the pause menu
    // and time warp included), only a zero, negative or non-finite delta leaves it as it is; a non-finite clock
    // restarts from 0
    SOLTEST_API double AdvanceLaneFlowClockS(double clockS, double realDeltaS);

    // Returns the lane ribbon mesh's vertex count: lanes x 2 lines x (NPC_LANE_SEGMENTS + 1) samples x 2 sides (0 for
    // no lanes)
    SOLTEST_API int32 LaneRibbonVertexCount(int32 laneCount);

    // Resets the outputs and lays out one strip per line (0 outbound, 1 inbound) of every lane with ships (IsLaneDrawn
    // of its count): per sample k two vertices (side -1, then +1) with UV0 = (k / NPC_LANE_SEGMENTS, side) and UV1 =
    // (lane group index, line index), and two triangles per segment
    SOLTEST_API void BuildLaneRibbonLayout(TConstArrayView<int32> laneShipCounts, TArray<FVector2f>& outUv0,
        TArray<FVector2f>& outUv1, TArray<uint32>& outIndices);
}
