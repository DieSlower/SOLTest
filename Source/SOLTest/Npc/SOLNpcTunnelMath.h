/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcLaneMath.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include "CoreMinimal.h"

// A lane tunnel's orthonormal frame (SDD 49 section 4.3): the lane basis, which moved to Traffic/ (SDD 50 section
// 4.10); the #49 name stays for the tunnel code
using FSOLNpcTunnelBasis = FSOLLaneBasis;

// The inputs of a ring, dot or chevron slot (SDD 49 sections 4.5 and 4.7), all relative to the camera's foot point on
// the lane's axis line
struct FSOLNpcTunnelSlotInput
{
    double RadialM = 0.0;           // Camera distance from the axis line (h)
    double TunnelRadiusM = 0.0;     // Tunnel radius (Rt)
    double Phase = 0.0;             // The lane's ring phase in [0, 1)
    double EndArcAM = 0.0;          // End ring A's arc minus the foot arc
    double EndArcBM = 0.0;          // End ring B's arc minus the foot arc
    double FocalRefPx = 0.0;        // Focal length in 1080p-reference px (SDD 52 D8); no slot draws while not positive
};

// One ring, dot or chevron a slot returns: its warp coordinate, axial offset from the foot point and alpha (cap, end,
// near, pixel-size and gap fades, SDD 52 section 4.4; the lane's draw fade is not included)
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
    double Fade = 0.0;                          // Label strength: the old 100 -> 50 lane radii band (TunnelFade, SDD
                                                // 52 D7), row 1 .w, also read by the lane and point materials (Q5;
                                                // far lane ships stay hidden, D6)
    double DrawFade = 0.0;                      // The tunnel's draw fade (TunnelDrawFade, row 3 .z; every distance, D7)
    double SegmentDistanceM = 0.0;              // The viewpoint's distance to the lane segment (LaneSegmentDistanceM,
                                                // computed once per lane per frame; not written to the texture)
    FVector3d Up = FVector3d::ZeroVector;       // Basis up
    double Phase = 0.0;                         // Ring phase, RingLatticePhase of the foot arc
    double EndArcAM = 0.0;                      // End ring A's arc minus the foot arc
    double EndArcBM = 0.0;                      // End ring B's arc minus the foot arc
};

// The tunnel material's constant vector parameters (SOL::NpcTunnelMaterialParams, set once at BeginPlay from
// SOLConstants.h; SDD 52 D16: the far-field constants ride the components the #49 accordion and fades freed, plus
// Layout4, added by the M_SOLNpcTunnel.uasset rebuild the user approved). The layout is documented in
// Shaders/Private/SOLNpcTunnel.ush
struct FSOLNpcTunnelMaterialVectors
{
    FVector4f Style0 = FVector4f(0.0f);     // (ring width px, end ring width px, dot size px, reference height px)
    FVector4f Style1 = FVector4f(0.0f);     // (emissive, chevron alpha, glow pad px, dot radius scale)
    FVector4f Shape = FVector4f(0.0f);      // (chevron radius scale, half width, half length radii, radius gone px)
    FVector4f Style2 = FVector4f(0.0f);     // (chevron notch, detail near fade start and full radii, dot diameter)
    FVector4f Style3 = FVector4f(0.0f);     // (ring gap gone px, chevron far fade start and end radii, chevrons/gap)
    FVector4f Layout0 = FVector4f(0.0f);    // (base spacing radii, ring gap full px, ring and gap slots per side)
    FVector4f Layout1 = FVector4f(0.0f);    // (end fade spacings, radius fade solid px, knee px, knee alpha)
    FVector4f Layout2 = FVector4f(0.0f);    // (near fade start, near fade full, radial full, radial none; radii)
    FVector4f Layout3 = FVector4f(0.0f);    // (detail gap gone px, detail gap full px, dots per gap, dot rows)
    FVector4f Layout4 = FVector4f(0.0f);    // (end ring min drawn px, ribbon inner max radius fraction, 0, 0); .z is
                                            // the QualityPreset scale the component pushes (53d; 0 reads as 1)
};

// The viewpoint's foot on a lane's axis line (SDD 49 sections 4.3 and 4.4), not clamped to the segment
struct FSOLNpcTunnelFoot
{
    double ArcM = 0.0;                              // Arc from node A (below 0 before A, above L past B)
    FVector3d PointRelM = FVector3d::ZeroVector;    // Foot point relative to the viewpoint (ecliptic m)
    double RadialM = 0.0;                           // Viewpoint distance from the axis line (h = |PointRelM|)
    bool bValid = false;                            // False (and zeros) for A == B or a non-finite input
};

// Pure math of the NPC lane guidance tunnel (SDD 49 sections 4.2-4.9; far field SDD 52, contract
// Docs/Plans/52a-tunnel-far-field-contract.md): the label fade, radius, basis and keep-right rows, the nearest lane
// point, the flat ring lattice and its phase, the reference pixels, the pixel-size and gap fades, the ring / dot /
// chevron slots, the end rings, ring samples, the label lanes and the renderer feed. Universe ecliptic metres, double.
// The tunnel material's HLSL (SOLNpcTunnel.ush) mirrors these functions, and Npc/SOLNpcTunnelMirror.h is its float copy
// on the CPU; a change to one must change the others
namespace SOLNpcTunnel
{
    // Returns the tunnel fade: 0 at and beyond NPC_TUNNEL_FADE_START_RADII x R, 1 at and inside
    // NPC_TUNNEL_FADE_FULL_RADII x R (also d <= 0), linear in log d between; 0 for a NaN distance or a NaN,
    // non-positive or infinite lane radius (no tunnel). Since SDD 52 D7 the label strength only, no draw alpha
    SOLTEST_API double TunnelFade(double distanceM, double laneRadiusM);

    // Returns the label strength restated in reference px of the lane radius with the live focal (SDD 53 Q20, section
    // 7.2): r = f R / d; 0 at and below 9.6 px, 1 at and above 19.2 px, linear in log r between (the same as TunnelFade
    // at f = 960); d <= 0 gives 1; NaN distance, bad radius or a non-positive focal give 0. The QualityPreset scale is
    // applied by passing FadeFocalRefPx
    SOLTEST_API double TunnelFade(double distanceM, double laneRadiusM, double focalRefPx);

    // Returns the focal every pixel fade and band reads (SDD 53 7.1, 7.2, user decision: one QualityPreset factor for
    // all thresholds): the live focal over SOLLaneNetwork::LaneTunnelQualityScale, so each pixel threshold is
    // multiplied by the scale; a scale below 1, NaN or infinite reads as 1 (the shader's max(Layout4.z, 1))
    SOLTEST_API double FadeFocalRefPx(double focalRefPx, double qualityScale);

    // Returns a lane's tunnel open fade after realDeltaS real seconds (SDD 53 Q9, section 5.4): toward 0 while closed,
    // toward 1 while open, a whole 1 -> 0 or 0 -> 1 taking SOL::NPC_LANE_OPEN_FADE_REAL_S, kept in [0, 1]; written
    // into row 3 .z with the draw fade; linear in real time; a NaN fade starts from the far end, a NaN or
    // non-positive step leaves it unchanged (clamped)
    SOLTEST_API double StepLaneOpenFade(double fade, bool bOpen, double realDeltaS);

    // Steps every group column's tunnel open fade (SDD 53 Q9, 4.5, 5.4): a column whose end points differ from the ones
    // its fade belongs to (reused for another lane) restarts from 0 and takes the new ones; the target is the column's
    // open state (an empty columnOpen reads every column as open: no lane network); bSnap (the first update, a time
    // jump) jumps every fade to its target, else StepLaneOpenFade; writes per column whether its fade is above 0 and it
    // has an end point (the slot hold). Runs over inOutFades' length; the other outputs must be as long, end points
    // past laneBodyA / B read as none
    SOLTEST_API void StepColumnOpenFades(TConstArrayView<int32> laneBodyA, TConstArrayView<int32> laneBodyB,
        TConstArrayView<uint8> columnOpen, double realDeltaS, bool bSnap, TArrayView<double> inOutFades,
        TArrayView<int32> inOutFadeBodyA, TArrayView<int32> inOutFadeBodyB, TArrayView<uint8> outVisible);

    // Returns the tunnel radius R x lerp(NPC_TUNNEL_RADIUS_MIN_SCALE, NPC_TUNNEL_RADIUS_MAX_SCALE,
    // SOLNpcLane::LaneTrafficWeight(n)): R x MIN for an empty lane (never 0, D8); 0 for a bad lane radius
    SOLTEST_API double TunnelRadiusM(double laneRadiusM, int32 shipCount);

    // Returns the tunnel basis of the lane A -> B, forwarding to SOLLaneTraffic::LaneBasis (SDD 50 section 4.10): up =
    // the ecliptic north pole projected off the axis, right = axis x up; within NPC_TUNNEL_UP_FALLBACK_DEG of the pole
    // up is universe +X projected off the axis, and up to NPC_TUNNEL_UP_BLEND_DEG it turns smoothly about the axis from
    // that to north (continuous along the pole angle; like any up field on a sphere it keeps a seam, here at the
    // azimuth where the two ups are opposite inside the blend band); invalid for A == B or a non-finite input
    inline FSOLNpcTunnelBasis TunnelBasis(const FVector3d& aM, const FVector3d& bM)
    {
        return SOLLaneTraffic::LaneBasis(aM, bM);
    }

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

    // Returns the focal length in 1080p-reference px, (W / 2) / tan(fov / 2) x NPC_LANE_REFERENCE_HEIGHT_PX / H (the
    // same at every resolution of one aspect, SDD 52 D8); 0 for a non-finite input, W or H <= 0, or a fov outside
    // (0, 180)
    SOLTEST_API double FocalLengthRefPx(double horizontalFovDeg, double viewportWidthPx, double viewportHeightPx);

    // Returns a viewport pixel size in 1080p-reference px, px x NPC_LANE_REFERENCE_HEIGHT_PX / H; 0 for a NaN size or a
    // non-finite or non-positive height
    SOLTEST_API double ToReferencePx(double viewportPx, double viewportHeightPx);

    // Returns the flat ring spacing S0 = NPC_TUNNEL_RING_BASE_SPACING_RADII x Rt (SDD 52 D2); 0 for a NaN, infinite or
    // non-positive tunnel radius
    SOLTEST_API double RingLatticeSpacingM(double tunnelRadiusM);

    // Returns the lane's ring phase frac(footArc / S0) in [0, 1), computed directly from the unclamped foot arc (no
    // history; a negative arc wraps up, a result rounded to 1 reads 0); 0 for a non-finite arc or a bad tunnel radius
    SOLTEST_API double RingLatticePhase(double footArcM, double tunnelRadiusM);

    // Returns a ring's radius on screen in reference px, f Rt / d (+inf for d <= 0); 0 for a NaN distance, a bad tunnel
    // radius or a bad focal length
    SOLTEST_API double RingRadiusPx(double distanceM, double tunnelRadiusM, double focalRefPx);

    // Returns a chevron's length on screen in reference px, f x 2 x NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII x Rt / d (+inf
    // for d <= 0); 0 for a NaN distance, a bad tunnel radius or a bad focal length
    SOLTEST_API double ChevronLengthPx(double distanceM, double tunnelRadiusM, double focalRefPx);

    // Returns the size a guide dot is drawn at in reference px: max(f x NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII x Rt / d,
    // NPC_TUNNEL_DOT_SIZE_PX); the floor for any bad input or d <= 0
    SOLTEST_API double DotDrawnSizePx(double distanceM, double tunnelRadiusM, double focalRefPx);

    // Returns the radius an end ring is drawn at in reference px, max(r, NPC_TUNNEL_END_RING_MIN_DRAWN_PX); the floor
    // for NaN (SDD 52 D14)
    SOLTEST_API double EndRingDrawnRadiusPx(double radiusRefPx);

    // Returns how far a ring ribbon's inner half reaches toward the ring's centre in px: min(the ribbon's half extent,
    // NPC_TUNNEL_RIBBON_INNER_MAX_RADIUS_FRACTION x the ring's on-screen radius), so the inner edge always stays
    // short of the centre and a sub-pixel ring cannot fold its quads into overlapping bowties; the half extent
    // unchanged when the ring is larger than the ribbon; 0 for a NaN or non-positive radius or half extent (both in
    // the same px, actual or reference)
    SOLTEST_API double RingRibbonInnerExtentPx(double halfExtentPx, double ringRadiusPx);

    // Returns the pixel-size fade (SDD 52 D9): 1 from NPC_TUNNEL_RADIUS_FADE_SOLID_PX, KNEE_ALPHA at KNEE_PX, 0 at and
    // below GONE_PX, linear in log px between the knots; 0 for NaN, 1 for +inf
    SOLTEST_API double RingRadiusFade(double radiusRefPx);

    // Returns the on-screen gap in reference px between the ring at axial offset x (camera h off the axis) and its
    // neighbour one S0 farther from the camera: f x max(the angle between the rays to their centres, the difference of
    // their angular radii Rt / d); +inf at the camera (d = 0); 0 for a NaN input, a bad tunnel radius or focal length
    SOLTEST_API double RingGapPx(double axialM, double radialM, double tunnelRadiusM, double focalRefPx);

    // Returns the ring gap fade (SDD 52 D11): 0 at and below NPC_TUNNEL_RING_GAP_FADE_GONE_PX, 1 from _FULL_PX, linear
    // in log px between; 0 for NaN, 1 for +inf
    SOLTEST_API double RingGapFade(double gapRefPx);

    // Returns the dot and chevron gap gate (SDD 52 D4, D11): 0 at and below NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX, 1 from
    // _FULL_PX, linear in log px between; 0 for NaN, 1 for +inf
    SOLTEST_API double DetailGapFade(double gapRefPx);

    // Returns the lane's draw fade (SDD 52 D7): 1 while the tunnels are shown, 0 while hidden; no distance input
    SOLTEST_API double TunnelDrawFade(bool bShown);

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

    // Returns the dot and chevron near fade by the element's own distance from the camera (49c, fill cost near a
    // wall): 0 inside NPC_TUNNEL_DETAIL_NEAR_FADE_START_RADII x Rt, 1 from NPC_TUNNEL_DETAIL_NEAR_FADE_FULL_RADII x Rt,
    // smoothstep between; 0 for a NaN distance or a bad tunnel radius
    SOLTEST_API double TunnelDetailNearFade(double elementDistanceM, double tunnelRadiusM);

    // Returns the chevrons' far fade (D22, like the mock-up): 1 inside NPC_TUNNEL_CHEVRON_FADE_START_RADII x Rt, 0 from
    // NPC_TUNNEL_CHEVRON_FADE_END_RADII x Rt, a falling smoothstep between (distance from the camera to the axis
    // point); 0 for a NaN distance or a bad tunnel radius
    SOLTEST_API double TunnelChevronFarFade(double distanceM, double tunnelRadiusM);

    // Returns the signed distance of a chevron quad point (corner units: x across in [-1, 1], y along its pointing
    // direction in [-1, 1]) from the filled dart: tip (0, 1), wings (+-1, -1), rear notch (0, -notch); negative
    // inside, the larger of the front and rear edge distances (the mock-up's arrowhead)
    SOLTEST_API double ChevronDartDistance(double x, double y, double notchFraction);

    // Returns the end fade clamp(dEnd / (NPC_TUNNEL_END_FADE_SPACINGS x spacing), 0, 1): 0 at an end ring and outside
    // the tunnel (negative dEnd); 0 for a NaN distance or a NaN, non-positive or infinite spacing
    SOLTEST_API double TunnelEndFade(double distanceToEndM, double spacingM);

    // Returns the ring cap fade clamp(NPC_TUNNEL_RING_SLOTS_PER_SIDE - |w|, 0, 1) (0 for NaN)
    SOLTEST_API double RingCapFade(double w);

    // Returns the dot and chevron cap fade clamp(NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE - 1 - |w|, 0, 1) (0 for NaN): an
    // element enters or leaves the gap slots only at alpha 0 (the gap gate has removed it well before)
    SOLTEST_API double DetailCapFade(double w);

    // Returns the ring slot count, 2 x NPC_TUNNEL_RING_SLOTS_PER_SIDE (ahead, then behind)
    SOLTEST_API int32 RingSlotCount();

    // Returns ring slot k's ring (k < PER_SIDE: w = k + 1 - phase ahead; else w = -((k - PER_SIDE) + phase) behind) on
    // the flat lattice at x = w S0: true with its warp coordinate, axial offset and alpha (RingCapFade x TunnelEndFade
    // over S0 x TunnelNearFadeAt x RingRadiusFade(RingRadiusPx) x RingGapFade(RingGapPx), SDD 52 section 4.4) when it
    // lies strictly between the end rings with a non-zero alpha; false for a slot out of range or a bad input (also a
    // bad focal length). The phase is wrapped into [0, 1) (a non-finite phase reads as 0)
    SOLTEST_API bool RingSlot(int32 slot, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outRing);

    // Returns ring slot k's axial offset from the foot point computed in float, the tunnel shader's path: w x S0 with
    // the phase wrapped into [0, 1) (a non-finite phase reads as 0); 0 for a slot out of range or a bad tunnel radius
    SOLTEST_API float RingSlotOffsetM(int32 slot, float phase, float tunnelRadiusM);

    // Returns the dot and chevron gap slot count, 2 x NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE (ahead, then behind)
    SOLTEST_API int32 DetailSlotCount();

    // Returns a gap slot's dot (dot 0 .. NPC_TUNNEL_DOTS_PER_GAP - 1, at dot / DOTS_PER_GAP of the gap whose ring is at
    // w = k - phase ahead or -(k - PER_SIDE + 1) - phase behind: on the kept ring and halfway to the next, guide
    // thinning): true when it lies strictly between the end rings with a non-zero alpha (DetailCapFade, end and
    // TunnelNearFadeAt fades, RingRadiusFade of its ring's radius and the DetailGapFade gate, SDD 52 D12). Not the full
    // rendered alpha: the shader also applies TunnelDetailNearFade and draws it at DotDrawnSizePx
    SOLTEST_API bool DotSlot(int32 slot, int32 dot, const FSOLNpcTunnelSlotInput& input, FSOLNpcTunnelRing& outDot);

    // Returns a gap slot's chevron (chevron 0 .. NPC_TUNNEL_CHEVRONS_PER_GAP - 1, at chevron / CHEVRONS_PER_GAP of the
    // same gaps as DotSlot: on the kept ring and halfway): true when it lies strictly between the end rings with a
    // non-zero alpha (DotSlot's fades with RingRadiusFade of ChevronLengthPx in place of the ring radius, times
    // TunnelChevronFarFade); false for a chevron index out of range. Not the full rendered alpha: the shader also
    // applies TunnelDetailNearFade and NPC_TUNNEL_CHEVRON_ALPHA
    SOLTEST_API bool ChevronSlot(int32 slot, int32 chevron, const FSOLNpcTunnelSlotInput& input,
        FSOLNpcTunnelRing& outChevron);

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
    // point, Rt), row 1 = (axis, label strength Fade), row 2 = (up, phase), row 3 = (end arc A, end arc B, draw fade,
    // 0; SDD 52 D7; the end-ring floor is the material's Layout4.x since the D16 rebuild); every other column zero
    // (nothing drawn, strength 0), so an empty list writes all zeros; a buffer shorter than 4 rows is left alone
    SOLTEST_API void WriteTunnelTexels(TConstArrayView<FSOLNpcTunnelLaneData> lanes, TArrayView<FVector4f> texels);

    // Returns whether the tunnels show: the lanes shown, sol.NpcTunnel on and the jump map closed (Q6)
    SOLTEST_API bool AreTunnelsShown(bool bLanesShown, bool bCvarOn, bool bMapOpen);

    // Returns the viewpoint's (the origin's) foot on the axis line of A-B (relative to the viewpoint): its arc from A,
    // not clamped, the foot point and its distance h; invalid (zeros) for A == B or a non-finite input
    SOLTEST_API FSOLNpcTunnelFoot LaneFoot(const FVector3d& aRelM, const FVector3d& bRelM);

    // Returns an end ring's alpha TunnelNearFadeAt(x, h, Rt) x RingRadiusFade(RingRadiusPx(sqrt(x^2 + h^2))) (SDD 52
    // D14: the pixel curve, gone at NPC_TUNNEL_RADIUS_FADE_GONE_PX like every ring, user 2026-11-18; no cap, end or gap
    // fade; drawn at EndRingDrawnRadiusPx; the lane's draw fade is the caller's); 0 for a NaN input, a bad tunnel
    // radius or a bad focal length
    SOLTEST_API double EndRingAlpha(double axialOffsetM, double radialM, double tunnelRadiusM, double focalRefPx);

    // Returns end ring A (end 0) or B (end 1) at its arc with w = x / S0 and EndRingAlpha (the input's focal length):
    // true when the end arcs are in order and the alpha is above 0 (beyond the ring cap too); false for a bad index or
    // input (also a bad focal length)
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
    // WallDirection(DotRowAngleDeg(row)) for x from max(end arc A, -X) to min(end arc B, X), X where the gap gate's
    // reach ends: NPC_TUNNEL_REFERENCE_FOCAL_PX x S0 / NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX of true distance (RingGapPx
    // never exceeds f S0 / d); false for an empty range, a bad row, basis or radius
    SOLTEST_API bool TunnelDotRowSegmentRelM(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis,
        double tunnelRadiusM, double endArcAM, double endArcBM, int32 row, FVector3d& outStartM, FVector3d& outEndM);

    // Writes every dot row's segment of one tunnel (TunnelDotRowSegmentRelM for rows 0 .. NPC_TUNNEL_DOT_ROWS - 1, the
    // range they share computed once, the rows' clock directions from a table): returns how many were written (all
    // rows, capped by the outputs' sizes; 0 when the range is empty or an input is bad)
    SOLTEST_API int32 TunnelDotRowSegmentsRelM(const FVector3d& footRelM, const FSOLNpcTunnelBasis& basis,
        double tunnelRadiusM, double endArcAM, double endArcBM, TArrayView<FVector3d> outStartsM,
        TArrayView<FVector3d> outEndsM);

    // Returns whether a drawn tunnel's dot rows can be visible, so the labels must keep clear of them: the viewpoint's
    // distance to the lane segment below the gap gate's reach, NPC_TUNNEL_REFERENCE_FOCAL_PX x S0 /
    // NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX (the end of TunnelDotRowSegmentRelM's range); false for a NaN distance or a
    // bad tunnel radius
    SOLTEST_API bool IsDotRowInReach(double segmentDistanceM, double tunnelRadiusM);

    // Returns a lane's tunnel radius Rt: the frozen wall radius of its profile when there is one (SDD 50 D13), else
    // TunnelRadiusM of its lane radius and ship count (the class step reads it before any frame is built, SDD 53 7.1)
    SOLTEST_API double LaneTunnelRadiusM(const FSOLNpcLaneEntry& lane);

    // Fills a lane's renderer feed (phase 0, left to the caller: RingLatticePhase of the foot arc; the segment distance
    // that gave the label strength kept in SegmentDistanceM) and its foot;
    // returns whether the lane can have a tunnel (both bodies resolved, a valid basis, drawable end rings), whatever
    // its fade (0 when it cannot). The label strength is TunnelFade at labelFocalRefPx (the live focal over the quality
    // scale, FadeFocalRefPx; the reference focal reproduces the old lane-radii band)
    SOLTEST_API bool BuildTunnelLaneData(int32 laneIndex, const FSOLNpcLaneEntry& lane, const FVector3d& viewpointM,
        double bodyRadiusAM, double bodyRadiusBM, FSOLNpcTunnelLaneData& outData, FSOLNpcTunnelFoot& outFoot,
        double labelFocalRefPx = SOL::NPC_TUNNEL_REFERENCE_FOCAL_PX);

    // Returns whether the tunnels show: the verification override's choice under sol.NpcTunnel and the map when it is
    // active, else AreTunnelsShown
    SOLTEST_API bool ResolveTunnelsShown(bool bOverride, bool bOverrideShown, bool bLanesShown, bool bCvarOn,
        bool bMapOpen);

    // Returns the fade the renderer uses: 0 while hidden, 1 when forced (the all-lanes budget case), else the fade
    // clamped to [0, 1] (NaN as 0)
    SOLTEST_API double EffectiveTunnelFade(double fade, bool bShown, bool bForceAllLanes);

    // Returns whether the lane overlay counts as drawn for the shared occluders: lane lines or tunnels drawn
    SOLTEST_API bool IsLaneOverlayDrawn(bool bLaneLinesDrawn, bool bTunnelsDrawn);

    // Returns whether the tunnel texture uploads this frame: a tunnel shows now or showed last frame (the zeros once)
    SOLTEST_API bool ShouldUploadTunnelTexels(bool bShownNow, bool bShownLastFrame);

    // Returns ring slot k's segment count (level of detail by slot, behind slots like the ahead slot of the same
    // index): NPC_TUNNEL_RING_SEGMENTS_NEAREST / NEAR / MID / FAR / FARTHEST; 0 out of range
    SOLTEST_API int32 RingSegmentsForSlot(int32 slot);

    // Returns the slot mesh's vertex count: ring ribbons ((N + 1) samples x 2 sides), two end rings at the nearest
    // count, a quad per dot (rows x dots per gap x gap slots) and per chevron (2 walls x chevrons per gap x gap slots)
    SOLTEST_API int32 TunnelSlotMeshVertexCount();

    // Returns the Reduced class mesh's vertex count (SDD 53 section 7.1): the ring ribbons and the two end rings of
    // the slot mesh, no dot or chevron quads (its first vertices, in the same order)
    SOLTEST_API int32 TunnelReducedMeshVertexCount();

    // Returns a lane's label strength where the lane and point materials read it (row 1 .w of its column); 0 out of
    // range or for a buffer shorter than two rows
    SOLTEST_API double ReadTunnelFadeTexel(TConstArrayView<FVector4f> texels, int32 lane);

    // Returns a lane's draw fade where the tunnel shader reads it (row 3 .z of its column); 0 out of range or for a
    // buffer shorter than four rows
    SOLTEST_API double ReadTunnelDrawFadeTexel(TConstArrayView<FVector4f> texels, int32 lane);

    // Returns the tunnel material's constant vectors from SOLConstants.h (the component sets them once; the float
    // mirror's test reads the same packing)
    SOLTEST_API FSOLNpcTunnelMaterialVectors TunnelMaterialVectors();
}
