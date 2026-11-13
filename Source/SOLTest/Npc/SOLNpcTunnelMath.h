/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcLaneMath.h"

#include "CoreMinimal.h"

// A lane tunnel's orthonormal frame (SDD 49 section 4.3), universe ecliptic directions: the outbound axis A -> B, up
// (12 o'clock) from the ecliptic north pole projected off the axis, right (3 o'clock, the outbound side) = axis x up
struct FSOLNpcTunnelBasis
{
    FVector3d Axis = FVector3d::ZeroVector;     // Outbound direction A -> B
    FVector3d Up = FVector3d::ZeroVector;       // 12 o'clock
    FVector3d Right = FVector3d::ZeroVector;    // 3 o'clock (outbound half of the tube)
    bool bValid = false;                        // False (and zero vectors) for A == B or a non-finite input
};

// The inputs of a ring, dot or chevron slot (SDD 49 sections 4.5 and 4.7), all relative to the camera's foot point on
// the lane's axis line
struct FSOLNpcTunnelSlotInput
{
    double RadialM = 0.0;           // Camera distance from the axis line (h)
    double TunnelRadiusM = 0.0;     // Tunnel radius (Rt)
    double Phase = 0.0;             // The lane's ring phase in [0, 1)
    double EndArcAM = 0.0;          // End ring A's arc minus the foot arc
    double EndArcBM = 0.0;          // End ring B's arc minus the foot arc
};

// One ring, dot or chevron a slot returns: its warp coordinate, axial offset from the foot point and alpha (cap, end,
// depth, near and, for dots and chevrons, detail fades; the lane's tunnel fade is not included)
struct FSOLNpcTunnelRing
{
    double W = 0.0;                 // Warp coordinate (SDD 49 section 4.5)
    double AxialOffsetM = 0.0;      // Signed offset along the axis from the camera's foot point
    double Alpha = 0.0;             // Product of the element's fades
};

// One shown tunnel's renderer feed (SDD 49 section 4.7): the values WriteTunnelTexels packs into the lane's column
struct FSOLNpcTunnelLaneData
{
    int32 Lane = INDEX_NONE;                    // Lane (group texture column)
    FVector3d FootRelM = FVector3d::ZeroVector; // Foot point on the axis line relative to the viewpoint (ecliptic m)
    double TunnelRadiusM = 0.0;                 // Rt
    FVector3d Axis = FVector3d::ZeroVector;     // Basis axis
    double Fade = 0.0;                          // Tunnel fade F (also read by the lane and point materials, Q5)
    FVector3d Up = FVector3d::ZeroVector;       // Basis up
    double Phase = 0.0;                         // Ring phase
    double EndArcAM = 0.0;                      // End ring A's arc minus the foot arc
    double EndArcBM = 0.0;                      // End ring B's arc minus the foot arc
};

// The viewpoint's foot on a lane's axis line (SDD 49 sections 4.3 and 4.4), not clamped to the segment
struct FSOLNpcTunnelFoot
{
    double ArcM = 0.0;                              // Arc from node A (below 0 before A, above L past B)
    FVector3d PointRelM = FVector3d::ZeroVector;    // Foot point relative to the viewpoint (ecliptic m)
    double RadialM = 0.0;                           // Viewpoint distance from the axis line (h = |PointRelM|)
    bool bValid = false;                            // False (and zeros) for A == B or a non-finite input
};

// Pure math of the NPC lane guidance tunnel (SDD 49 sections 4.2-4.9, contract section 6 cases 1-16): the trigger
// fade, radius, basis and keep-right rows, the nearest lane point, the continuous "accordion" ring spacing and warp,
// the ring phase, the fades, the ring / dot / chevron slots, the end rings, ring samples, the label lanes and the
// renderer feed. Universe ecliptic metres, double. The tunnel material's HLSL (SOLNpcTunnel.ush) mirrors these
// functions; a change to one must change the other
namespace SOLNpcTunnel
{
    // Returns the tunnel fade: 0 at and beyond NPC_TUNNEL_FADE_START_RADII x R, 1 at and inside
    // NPC_TUNNEL_FADE_FULL_RADII x R (also d <= 0), linear in log d between; 0 for a NaN distance or a NaN,
    // non-positive or infinite lane radius (no tunnel)
    SOLTEST_API double TunnelFade(double distanceM, double laneRadiusM);

    // Returns the tunnel radius R x lerp(NPC_TUNNEL_RADIUS_MIN_SCALE, NPC_TUNNEL_RADIUS_MAX_SCALE,
    // SOLNpcLane::LaneTrafficWeight(n)): R x MIN for an empty lane (never 0, D8); 0 for a bad lane radius
    SOLTEST_API double TunnelRadiusM(double laneRadiusM, int32 shipCount);

    // Returns the tunnel basis of the lane A -> B: up = the ecliptic north pole projected off the axis, right = axis x
    // up; within NPC_TUNNEL_UP_FALLBACK_DEG of the pole up is universe +X projected off the axis, and up to
    // NPC_TUNNEL_UP_BLEND_DEG it turns smoothly about the axis from that to north (continuous along the pole angle;
    // like any up field on a sphere it keeps a seam, here at the azimuth where the two ups are opposite inside the
    // blend band); invalid for A == B or a non-finite input
    SOLTEST_API FSOLNpcTunnelBasis TunnelBasis(const FVector3d& aM, const FVector3d& bM);

    // Returns a dot row's side: +1 for the outbound (right) half, rows 0 .. NPC_TUNNEL_DOT_ROWS / 2 - 1, -1 for the
    // inbound half, 0 for a row out of range
    SOLTEST_API int32 DotRowSide(int32 row);

    // Returns a dot row's clock angle (row + 0.5) x 360 / NPC_TUNNEL_DOT_ROWS degrees, clockwise from 12 o'clock as
    // seen looking from A toward B
    SOLTEST_API double DotRowAngleDeg(int32 row);

    // Returns the wall direction at a clock angle: cos(t) Up + sin(t) Right (0 = 12 o'clock, 90 = 3 o'clock)
    SOLTEST_API FVector3d WallDirection(const FSOLNpcTunnelBasis& basis, double clockDeg);

    // Returns the parameter in [0, 1] of the point of segment A-B (viewpoint at the origin) nearest the origin; 0 for a
    // degenerate segment or a non-finite input
    SOLTEST_API double LaneNearestParam(const FVector3d& aRelM, const FVector3d& bRelM);

    // Returns the distance from the origin to segment A-B (to the nearer end past it); +inf for a non-finite input
    SOLTEST_API double LaneSegmentDistanceM(const FVector3d& aRelM, const FVector3d& bRelM);

    // Returns the local ring spacing max(NPC_TUNNEL_RING_BASE_SPACING_RADII x Rt, rho x sqrt(x^2 + h^2)) for a ring at
    // axial offset x from the foot point seen from h off the axis; 0 for a NaN input or a bad tunnel radius
    SOLTEST_API double RingSpacingM(double axialM, double radialM, double tunnelRadiusM);

    // Returns the ring warp W(x, h), the integral of 1 / RingSpacingM from the foot point (odd, strictly increasing,
    // x / S0 inside the constant zone, the asinh form beyond); 0 for a NaN input or a bad tunnel radius
    SOLTEST_API double RingWarp(double axialM, double radialM, double tunnelRadiusM);

    // Returns the axial offset x with RingWarp(x, h, Rt) == w (closed form); 0 for a NaN input or a bad tunnel radius
    SOLTEST_API double RingWarpInverse(double w, double radialM, double tunnelRadiusM);

    // Returns frac(phase + deltaArc / RingSpacingM(0, h, Rt)) in [0, 1), in double (a negative delta wraps up); a NaN
    // or infinite delta or a bad tunnel radius keeps the phase (wrapped into [0, 1); a non-finite phase reads as 0)
    SOLTEST_API double AdvanceRingPhase(double phase, double deltaArcM, double radialM, double tunnelRadiusM);

    // Returns the phase of a lane entering range, frac(footArc / RingSpacingM(0, h, Rt)); 0 for a non-finite arc or a
    // bad tunnel radius
    SOLTEST_API double InitialRingPhase(double footArcM, double radialM, double tunnelRadiusM);

    // Returns the depth fade: 1 inside NPC_TUNNEL_DEPTH_FADE_START_RADII x Rt, log-linear down to
    // NPC_TUNNEL_DEPTH_FADE_FLOOR at NPC_TUNNEL_DEPTH_FADE_END_RADII x Rt, the floor beyond; the floor for a NaN
    // distance or a bad tunnel radius
    SOLTEST_API double TunnelDepthFade(double distanceM, double tunnelRadiusM);

    // Returns the near fade of an element at axial offset x: 0 within NPC_TUNNEL_NEAR_FADE_START_RADII x Rt, 1 from
    // NPC_TUNNEL_NEAR_FADE_FULL_RADII x Rt, smoothstep between, symmetric; 0 for a NaN offset or a bad tunnel radius
    SOLTEST_API double TunnelNearFade(double axialOffsetM, double tunnelRadiusM);

    // Returns how much the near fade acts with the camera h off the axis: 1 within
    // NPC_TUNNEL_NEAR_FADE_RADIAL_FULL_RADII x Rt, 0 from NPC_TUNNEL_NEAR_FADE_RADIAL_NONE_RADII x Rt, smoothstep
    // between (seen from outside the tube the nearest ring stays); 1 for a NaN distance or a bad tunnel radius
    SOLTEST_API double TunnelNearFadeRadialWeight(double radialM, double tunnelRadiusM);

    // Returns the near fade the slots use: 1 - TunnelNearFadeRadialWeight(h) x (1 - TunnelNearFade(x)); 0 for a NaN
    // input or a bad tunnel radius
    SOLTEST_API double TunnelNearFadeAt(double axialOffsetM, double radialM, double tunnelRadiusM);

    // Returns the dot and chevron fade: 1 inside NPC_TUNNEL_DETAIL_FADE_START_RADII x Rt, 0 at and past
    // NPC_TUNNEL_DETAIL_FADE_END_RADII x Rt, log-linear between; 0 for a NaN distance or a bad tunnel radius
    SOLTEST_API double TunnelDetailFade(double distanceM, double tunnelRadiusM);

    // Returns the end fade clamp(dEnd / (NPC_TUNNEL_END_FADE_SPACINGS x spacing), 0, 1): 0 at an end ring and outside
    // the tunnel (negative dEnd); 0 for a NaN distance or a NaN, non-positive or infinite spacing
    SOLTEST_API double TunnelEndFade(double distanceToEndM, double spacingM);

    // Returns the ring cap fade clamp(NPC_TUNNEL_RING_SLOTS_PER_SIDE - |w|, 0, 1) (0 for NaN)
    SOLTEST_API double RingCapFade(double w);

    // Returns the dot and chevron cap fade clamp(NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE - 1 - |w|, 0, 1) (0 for NaN): an
    // element enters or leaves the gap slots only at alpha 0 (the detail fade has removed it well before)
    SOLTEST_API double DetailCapFade(double w);

    // Returns the ring slot count, 2 x NPC_TUNNEL_RING_SLOTS_PER_SIDE (ahead, then behind)
    SOLTEST_API int32 RingSlotCount();

    // Returns ring slot k's ring (k < PER_SIDE: w = k + 1 - phase ahead; else w = -((k - PER_SIDE) + phase) behind):
    // true with its warp coordinate, axial offset and alpha (cap, end, depth and TunnelNearFadeAt) when it lies
    // strictly between the end rings with a non-zero alpha; false for a slot out of range or a bad input. The phase is
    // wrapped into [0, 1) (a non-finite phase reads as 0)
    SOLTEST_API bool RingSlot(int32 slot, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outRing);

    // Returns ring slot k's axial offset from the foot point computed in float, the tunnel shader's path (the phase,
    // wrapped into [0, 1), the radial distance and the tunnel radius as the shader receives them); 0 for a slot out of
    // range or a bad input
    SOLTEST_API float RingSlotOffsetM(int32 slot, float phase, float radialM, float tunnelRadiusM);

    // Returns the dot and chevron gap slot count, 2 x NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE (ahead, then behind)
    SOLTEST_API int32 DetailSlotCount();

    // Returns a gap slot's dot (dot 0 .. NPC_TUNNEL_DOTS_PER_GAP - 1, at (dot + 0.5) / DOTS_PER_GAP of the gap whose
    // ring is at w = k - phase ahead or -(k - PER_SIDE + 1) - phase behind): true when it lies strictly between the
    // end rings with a non-zero alpha (DetailCapFade, end, depth, TunnelNearFadeAt and detail fades)
    SOLTEST_API bool DotSlot(int32 slot, int32 dot, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outDot);

    // Returns a gap slot's chevron (mid-gap, the same gaps as DotSlot): true when it lies strictly between the end
    // rings with a non-zero alpha
    SOLTEST_API bool ChevronSlot(int32 slot, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outChevron);

    // Returns the end rings' arcs from node A, (R_A + MARGIN x Rt, L - R_B - MARGIN x Rt); (0, 0) when they cross or
    // an input is not finite (IsTunnelDrawable false)
    SOLTEST_API FVector2D TunnelEndArcsM(double laneLengthM, double bodyRadiusAM, double bodyRadiusBM,
        double tunnelRadiusM);

    // Returns whether a tunnel can be drawn: every input finite, a positive tunnel radius and end ring A before end
    // ring B
    SOLTEST_API bool IsTunnelDrawable(double laneLengthM, double bodyRadiusAM, double bodyRadiusBM,
        double tunnelRadiusM);

    // Returns sample k of a ring of a radius round a centre, clockwise from 12 o'clock as seen along the axis:
    // centre + radius (cos(2 pi k / n) Up + sin(2 pi k / n) Right); the centre for segments <= 0
    SOLTEST_API FVector3d RingSamplePointM(const FSOLNpcTunnelBasis& basis, const FVector3d& centreM, double radiusM,
        int32 sample, int32 segments);

    // Writes the lanes with a fade above 0 into outLanes, strongest first (highest fade; ties to the smaller distance
    // in lane radii, then the lower index), at most min(NPC_TUNNEL_LABEL_MAX_COUNT, outLanes.Num()), and returns the
    // count; the shorter of the two inputs is used and a lane with a NaN entry is ignored
    SOLTEST_API int32 SelectLabelLanes(TConstArrayView<double> fades, TConstArrayView<double> distanceRadii,
        TArrayView<int32> outLanes);

    // Returns a label's alpha: the fade (clamped to [0, 1], NaN as 0) for the strongest tunnel's label, the fade x
    // NPC_TUNNEL_LABEL_SIDE_ALPHA for every other
    SOLTEST_API double LaneLabelAlpha(double fade, bool bStrongest);

    // Returns whether a label of this alpha is placed (alpha >= NPC_TUNNEL_LABEL_MIN_ALPHA; NaN is not)
    SOLTEST_API bool IsLabelPlaced(double alpha);

    // Returns the straight-line distances from the ship to end ring A's and end ring B's centres (A + arc x axis)
    SOLTEST_API FVector2D LaneEndDistancesM(const FVector3d& shipM, const FSOLNpcLaneEntry& lane,
        const FSOLNpcTunnelBasis& basis, FVector2D endArcsM);

    // Writes the tunnel texture (4 rows of NPC_GROUP_TEXTURE_WIDTH texels): per shown lane's column row 0 = (foot
    // point, Rt), row 1 = (axis, fade), row 2 = (up, phase), row 3 = (end arc A, end arc B, 0, 0); every other column
    // zero (fade 0), so an empty list writes all zeros; a buffer shorter than 4 rows is left alone
    SOLTEST_API void WriteTunnelTexels(TConstArrayView<FSOLNpcTunnelLaneData> lanes, TArrayView<FVector4f> texels);

    // Rewrites inOutLanes to the lanes with a fade above 0, in index order, and returns true only when that set
    // changed; the array's allocation is kept when the count does not grow
    SOLTEST_API bool CollectTunnelLanes(TConstArrayView<double> fades, TArray<int32>& inOutLanes);

    // Returns whether the tunnels show: the lanes shown, sol.NpcTunnel on and the jump map closed (Q6)
    SOLTEST_API bool AreTunnelsShown(bool bLanesShown, bool bCvarOn, bool bMapOpen);

    // Returns the viewpoint's (the origin's) foot on the axis line of A-B (relative to the viewpoint): its arc from A,
    // not clamped, the foot point and its distance h; invalid (zeros) for A == B or a non-finite input
    SOLTEST_API FSOLNpcTunnelFoot LaneFoot(const FVector3d& aRelM, const FVector3d& bRelM);

    // Returns an end ring's alpha max(TunnelDepthFade(sqrt(x^2 + h^2), Rt), NPC_TUNNEL_END_RING_MIN_ALPHA) x
    // TunnelNearFadeAt(x, h, Rt) (no cap, end or detail fade; the tunnel fade is the caller's); 0 for a NaN input or a
    // bad tunnel radius
    SOLTEST_API double EndRingAlpha(double axialOffsetM, double radialM, double tunnelRadiusM);

    // Returns end ring A (end 0) or B (end 1) at its arc with its warp coordinate and EndRingAlpha: true when the end
    // arcs are in order and the alpha is above 0 (beyond the ring cap too); false for a bad index or input
    SOLTEST_API bool EndRingSlot(int32 end, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outRing);

    // Returns the side of the foot point a label sits on: with no previous sign (0) the sign of the dot of the camera
    // forward and the axis (+1 at 0 and for NaN); a held sign flips only once the dot passes
    // NPC_TUNNEL_LABEL_FLIP_HYSTERESIS the other way (NaN keeps it); another previous value reads by its sign
    SOLTEST_API int32 LabelAheadSign(double forwardDotAxis, int32 previousSign);

    // Returns a label anchor relative to the viewpoint: foot + sign x NPC_TUNNEL_LABEL_AHEAD_RADII x Rt x Axis + Rt x
    // WallDirection(clock) (sign 0 reads as +1); the foot point for an invalid basis or a bad tunnel radius
    SOLTEST_API FVector3d LaneLabelAnchorRelM(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis,
        double tunnelRadiusM, int32 aheadSign, double clockDeg);

    // Writes a dot row's straight segment relative to the viewpoint: foot + x Axis + NPC_TUNNEL_DOT_RADIUS_SCALE x Rt x
    // WallDirection(DotRowAngleDeg(row)) for x from max(end arc A, -X) to min(end arc B, X), X where the dots end at
    // NPC_TUNNEL_DETAIL_FADE_END_RADII x Rt of true distance; false for an empty range, a bad row, basis or radius
    SOLTEST_API bool TunnelDotRowSegmentRelM(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis,
        double tunnelRadiusM, double endArcAM, double endArcBM, int32 row, FVector3d& outStartM, FVector3d& outEndM);

    // Fills a lane's renderer feed (phase 0, left to the caller) and its foot; returns whether the lane can have a
    // tunnel (both bodies resolved, a valid basis, drawable end rings), whatever its fade (0 when it cannot)
    SOLTEST_API bool BuildTunnelLaneData(int32 laneIndex, const FSOLNpcLaneEntry& lane, const FVector3d& viewpointM,
        double bodyRadiusAM, double bodyRadiusBM, FSOLNpcTunnelLaneData& outData, FSOLNpcTunnelFoot& outFoot);

    // Returns the next ring phase: InitialRingPhase(foot arc) for a lane entering range, else the phase advanced by
    // the foot arc's change (AdvanceRingPhase; a NaN change keeps it)
    SOLTEST_API double StepRingPhase(bool bWasInRange, double phase, double previousFootArcM, double footArcM,
        double radialM, double tunnelRadiusM);

    // Returns whether the tunnels show: the verification override's choice under sol.NpcTunnel and the map when it is
    // active, else AreTunnelsShown
    SOLTEST_API bool ResolveTunnelsShown(bool bOverride, bool bOverrideShown, bool bLanesShown, bool bCvarOn,
        bool bMapOpen);

    // Returns the fade the renderer uses: 0 while hidden, 1 when forced (the all-lanes budget case), else the fade
    // clamped to [0, 1] (NaN as 0)
    SOLTEST_API double EffectiveTunnelFade(double fade, bool bShown, bool bForceAllLanes);

    // Returns whether the tunnel component draws: shown and at least one fade above 0
    SOLTEST_API bool ShouldDrawTunnels(bool bShown, TConstArrayView<double> fades);

    // Returns whether the lane overlay counts as drawn for the shared occluders: lane lines or tunnels drawn
    SOLTEST_API bool IsLaneOverlayDrawn(bool bLaneLinesDrawn, bool bTunnelsDrawn);

    // Returns whether the tunnel texture uploads this frame: a tunnel shows now or showed last frame (the zeros once)
    SOLTEST_API bool ShouldUploadTunnelTexels(bool bShownNow, bool bShownLastFrame);

    // Returns ring slot k's segment count (level of detail by slot, behind slots like the ahead slot of the same
    // index): NPC_TUNNEL_RING_SEGMENTS_NEAR / MID / FAR; 0 out of range
    SOLTEST_API int32 RingSegmentsForSlot(int32 slot);

    // Returns the slot mesh's vertex count: ring ribbons ((N + 1) samples x 2 sides), two end rings at the near count,
    // a quad per dot and per chevron
    SOLTEST_API int32 TunnelSlotMeshVertexCount();

    // Returns a lane's tunnel fade where the lane and point materials read it (row 1 .w of its column); 0 out of range
    // or for a buffer shorter than two rows
    SOLTEST_API double ReadTunnelFadeTexel(TConstArrayView<FVector4f> texels, int32 lane);
}
