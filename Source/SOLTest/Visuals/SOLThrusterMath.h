/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

struct FSOLLaneThrustState;

// The plume's two colours at a colour scalar c (SOLThruster::PlumeColour): the rim (outer) and the white-hot core
struct FSOLPlumeColour
{
    FLinearColor Rim = FLinearColor::White;
    FLinearColor Core = FLinearColor::White;
};

// One shock-diamond knot on the plume's centre line (SOLThruster::ShockKnots)
struct FSOLShockKnot
{
    double PositionM = 0.0;     // Distance from the nozzle exit along the plume
    double Strength = 0.0;      // SHOCK_DECAY^(k-1) for knot k
};

// The player's thrust over one step (SDD 51 section 4.2, Q8): the time-weighted mean of the substeps' hull-frame
// thrust, m/s^2 in, G out; plain data, no allocation
struct SOLTEST_API FSOLThrustMean
{
    // Adds one substep's hull-frame thrust (m/s^2) held for dtS; ignored for dt <= 0 or a non-finite dt or vector
    void Add(const FVector3d& thrustLocalMps2, double dtS);

    // Returns the mean thrust in G over the added substeps; zero with none
    FVector3f MeanG() const;

    // Forgets every added substep
    void Reset();

private:

    FVector3d mSumMps2S = FVector3d::ZeroVector;    // Sum of thrust x dt
    double mSumS = 0.0;                             // Sum of dt
};

// A hull-frame thrust vector split into the main plume and the five gas-burst axes (G, each >= 0)
struct FSOLThrustAxes
{
    float Main = 0.0f;          // max(0, X): the main plume
    float Reverse = 0.0f;       // max(0, -X)
    float Right = 0.0f;         // max(0, Y)
    float Left = 0.0f;          // max(0, -Y)
    float Up = 0.0f;            // max(0, Z)
    float Down = 0.0f;          // max(0, -Z)
};

// A gas burst's thrust axis (SDD 51 section 4.6); the gas leaves opposite to the thrust
enum class ESOLBurst : uint8
{
    Reverse,                    // Thrust -X, gas +X at the bow ports
    Right,                      // Thrust +Y, gas -Y on the left side
    Left,                       // Thrust -Y, gas +Y on the right side
    Up,                         // Thrust +Z, gas -Z on the bottom
    Down                        // Thrust -Z, gas +Z on the top
};

// One nozzle pose of a flame's history (SDD 51 R10-4, R10-8): non-rotating world-aligned axes relative to the ship
struct FSOLPlumeSample
{
    double EmitTimeS = 0.0;                         // Emission time (game time)
    FVector3f OffsetM = FVector3f::ZeroVector;      // Nozzle exit relative to the ship's centre at emission
    FVector3f ExhaustDir = FVector3f::ZeroVector;   // Unit exhaust direction at emission
    float Brightness = 0.0f;                        // b at emission (0 once the spool-down has ended)
};

// A flame's pose history: a fixed ring of FLAME_HISTORY_SAMPLES poses, newest first by age, no allocation
class SOLTEST_API FSOLPlumeHistory
{
public:

    // Empties the history
    void Reset();

    // Stores a pose as the newest (overwriting the oldest when full); false, nothing stored, if any field is
    // non-finite
    bool Push(const FSOLPlumeSample& sample);

    // Returns the number of poses held (at most FLAME_HISTORY_SAMPLES)
    int32 Num() const;

    // Returns the pose of age rank i (0 = newest, Num() - 1 = oldest); i must lie in [0, Num())
    const FSOLPlumeSample& ByAge(int32 i) const;

    // Returns whether a pose is due: always when empty, else once nowS is more than FLAME_SAMPLE_INTERVAL_S past the
    // newest pose's emission
    bool ShouldSample(double nowS) const;

private:

    FSOLPlumeSample mSamples[SOL::Thruster::FLAME_HISTORY_SAMPLES]; // Ring storage
    int32 mNewest = 0;                                          // Slot of the newest sample
    int32 mCount = 0;                                           // Samples held
};

// One shown NPC flame offered to the renderer split (SOLThruster::SplitRibbonRenderers, part 51e)
struct FSOLRibbonShown
{
    int32 NpcIndex = INDEX_NONE;
    double RibbonPx = 0.0;      // Flame length on screen, reference px (SOLThruster::RibbonPixelLength)
};

// One flame spine point as both renderers draw it (SOLThruster::BuildFlamePoints, R10)
struct FSOLRibbonPoint
{
    FVector3f PositionM = FVector3f::ZeroVector;    // Position relative to the frame anchor
    float Alpha = 0.0f;                             // Emission brightness x along-arc fade x pixel fade
    float WidthM = 0.0f;                            // Unfloored full width (each renderer applies the 1 px floor)
};

// The plume floats of one hull-drawn ship (SOLThruster::PlumeInstance; SOL::ThrusterMaterialParams 0-2): the glow reads
// c and b; float 2 is read by no section since R10 (layout kept, compaction left to 51f)
struct FSOLPlumeInstance
{
    float Colour = 0.0f;        // c in [0, 1]
    float Brightness = 0.0f;    // b in [0, 1]; 0 = no glow
    float LengthM = 0.0f;       // True plume length L_p (the true hull length's, never the drawn size, Q14)
};

// The player's plume floats and hull light of one frame (SOLThruster::PlayerPlume, part 51d)
struct FSOLPlayerPlume
{
    FSOLPlumeInstance Plume;                        // Both nozzles' floats (they share c, b and L_p)
    float LightIntensityCd = 0.0f;                  // Hull light intensity; 0 while hidden
    FLinearColor LightColour = FLinearColor::Black; // The ramp's rim colour at c while visible
    bool bLightVisible = false;                     // Enabled and b > 0
};

// The hulls with a nozzle table (SOLThruster::HullThrusters)
enum class ESOLHullKind : uint8
{
    NpcFaction0,                // Placeholder cone
    NpcFaction1,                // Placeholder box
    Player
};

// One main-engine nozzle in the hull frame (X nose, Y right, Z top), metres
struct FSOLNozzle
{
    FVector3f PositionM = FVector3f::ZeroVector;    // Nozzle exit centre
    FVector3f ExhaustDir = FVector3f::ZeroVector;   // Unit exhaust axis
    float RadiusM = 0.0f;                           // Nozzle radius r_n
};

// A hull's main-engine table (SDD 51 section 4.9): true hull length and up to two nozzles
struct FSOLHullThrusters
{
    float HullLengthM = 0.0f;
    int32 NozzleCount = 0;
    FSOLNozzle Nozzles[2];
};

// Pure math of the issue #51 thruster plume (SDD 51 sections 4.2-4.9, 11.2, contract
// Docs/Plans/51-thruster-math-contract.md): the plume scalars and colour ramp, shock diamonds, the hull-frame thrust and
// its burst axes, the pixel-size rule, the one-piece flame (profile, gas-parcel spine, pixel envelope), quality scaling,
// the player hull light and the per-hull nozzle table. Constants in SOL::Thruster. The flame materials' HLSL mirror
// (Shaders/Private/SOLThruster.ush, SOLThrusterMirror.h) must follow any change to PlumeColour, PlumeBrightness,
// ShockKnots or the Flame* envelope
namespace SOLThruster
{
    // Returns x = clamp(G / G_REF, 0, 1); 0 under PLUME_MIN_G and for a non-finite G
    SOLTEST_API double PlumeFraction(double gMain);

    // Returns the cone length L_hull x^LENGTH_EXPONENT (the hull length at and above G_REF); 0 when off, for a bad G or
    // a non-finite or non-positive hull length
    SOLTEST_API double PlumeLengthM(double hullLengthM, double gMain);

    // Returns the brightness b = x^BRIGHTNESS_EXPONENT; 0 when off or for a bad G
    SOLTEST_API double PlumeBrightness(double gMain);

    // Returns the colour scalar c in [0, 1]: 0 up to COLOUR_BLUE_END_G, COLOUR_YELLOW_C x smoothstep to
    // COLOUR_YELLOW_G, linear to 1 at G_REF, 1 above; 1 at any G under War power; 0 for NaN
    SOLTEST_API double PlumeColourScalar(double gMain, bool bWarPower);

    // Returns the ramp's rim and core colours at c (linear between the blue, yellow and red keys, c clamped to [0, 1],
    // NaN as 0), alpha 1
    SOLTEST_API FSOLPlumeColour PlumeColour(double c);

    // Returns the knot spacing SHOCK_SPACING_FACTOR r_n (1 + SHOCK_BRIGHTNESS_GAIN b), b clamped to [0, 1] (NaN 0); 0
    // for a non-finite or non-positive nozzle radius
    SOLTEST_API double ShockSpacingM(double nozzleRadiusM, double brightness);

    // Writes the knots k S (k = 1, 2, ...) with k S <= SHOCK_ZONE L_p and strength SHOCK_DECAY^(k-1) into out, at
    // most min(out.Num(), SHOCK_MAX_KNOTS), and returns the count; 0 for a plume length or spacing that is not positive
    SOLTEST_API int32 ShockKnots(double plumeLengthM, double nozzleRadiusM, double brightness,
        TArrayView<FSOLShockKnot> out);

    // Returns a lane ship's hull-frame thrust in G: AccelMps2 x Direction / g0 on (forward, right = top x forward,
    // top); zero for Coast and Flip and for a non-finite input. All vectors in one frame: pass Unreal axes so +Y is
    // the hull's right
    SOLTEST_API FVector3f LaneThrustLocalG(const FSOLLaneThrustState& state, const FVector3d& hullForward,
        const FVector3d& hullTop);

    // Returns a thrust acceleration (m/s^2) in G on a hull basis (forward, right = top x forward, top), the forward
    // normalised and the top made orthogonal to it and normalised first (an NPC's drawn top is not yet orthogonal);
    // zero for a non-finite input, a zero forward, or a top that is zero or parallel to the forward. All vectors in one
    // frame: pass Unreal axes so +Y is the hull's right (part 51c)
    SOLTEST_API FVector3f HullThrustLocalG(const FVector3d& thrustMps2, const FVector3d& hullForward,
        const FVector3d& hullTop);

    // Returns the plume floats of a hull-drawn ship (part 51c, R10): PlumeColourScalar, PlumeBrightness and PlumeLengthM
    // of the true hull length; all zero when the plume is off, when the nozzle glow's true diameter
    // GLOW_DIAMETER_NOZZLE_RADII r_n is under GLOW_FADE_LOW_PX on screen (PixelLength; the glow's alpha is 0 there) and
    // for a non-finite or non-positive nozzle radius, distance or focal length
    SOLTEST_API FSOLPlumeInstance PlumeInstance(double gMain, bool bWarPower, double hullLengthM, double nozzleRadiusM,
        double distanceM, double focalRefPx);

    // Writes a ship's per-ship floats in the SOL::ThrusterMaterialParams layout into out (at most out.Num()): c, b and
    // the true plume length, the three burst floats zero (gas bursts are part 51g)
    SOLTEST_API void PlumeCustomData(const FSOLPlumeInstance& plume, TArrayView<float> out);

    // Returns the main and burst axes of a hull-frame thrust in G; a non-finite component zeroes its own two axes
    SOLTEST_API FSOLThrustAxes ThrustAxes(const FVector3f& thrustLocalG);

    // Returns the unit hull-frame direction a burst's gas leaves in (opposite to the thrust)
    SOLTEST_API FVector3f BurstExhaustDir(ESOLBurst burst);

    // Returns a burst's strength clamp((g - minG) / (refG - minG), 0, 1); 0 for NaN or a non-finite threshold, and
    // for refG <= minG
    SOLTEST_API float BurstStrength(float componentG, float minG, float refG);

    // Returns a burst's length L_hull / BURST_LENGTH_HULL_DIVISOR x strength, strength clamped to [0, 1]; 0 for a NaN
    // strength or a non-finite or non-positive hull length
    SOLTEST_API double BurstLengthM(double hullLengthM, float strength);

    // Returns the focal length in 1080p-reference px, (NPC_LANE_REFERENCE_HEIGHT_PX x aspect / 2) / tan(fov / 2); 0
    // for a non-finite input, a fov outside (0, 180) or a non-positive aspect
    SOLTEST_API double FocalRefPx(double hFovDeg, double aspect);

    // Returns a length's size on screen f L / d in reference px; 0 for d <= 0 and any non-finite or non-positive input
    SOLTEST_API double PixelLength(double lengthM, double distanceM, double focalRefPx);

    // Returns the fade clamp((px - low) / (high - low), 0, 1); for low >= high a step, 1 at and above high; 0 for NaN
    SOLTEST_API double LodAlpha(double px, double lowPx, double highPx);

    // Returns the distance at which the longest plume (the hull length) shrinks to lowPx, f L_hull / lowPx; 0 for
    // lowPx <= 0 and any non-finite or non-positive input
    SOLTEST_API double PlumeMaxDistanceM(double hullLengthM, double focalRefPx, double lowPx);

    // Returns the size a shown glow is drawn at, max(truePx, floorPx) (the floor for NaN)
    SOLTEST_API double GlowDrawnPx(double truePx, double floorPx);

    // Returns the flame length L_f = FLAME_LENGTH_FACTOR L_p (R10-1); 0 for a non-finite or non-positive plume length
    SOLTEST_API double FlameLengthM(double plumeLengthM);

    // Returns the flame's full width (diameter) at arc coordinate s, 2 r_n [1 - (1 - FLAME_TAIL_WIDTH_RATIO) S(s / L_p)]
    // with S the smoothstep on s / L_p clamped to [0, 1] (R10-2): widest at the nozzle, no kink at L_p, constant past it;
    // a negative s reads as 0; 0 for a NaN s or a non-finite or non-positive L_p or r_n. Unfloored
    SOLTEST_API double FlameWidthM(double arcM, double plumeLengthM, double nozzleRadiusM);

    // Returns the along-arc alpha clamp(1 - s / L_f, 0, 1) (R10-3); 0 for a NaN s or a non-finite or non-positive L_f
    SOLTEST_API double FlameAlongAlpha(double arcM, double flameLengthM);

    // Returns the lateral offset (a + s) sin(phi) - a phi cos(phi), phi = omega s / v_e, of the parcel at arc coordinate
    // s from the current nozzle axis in a steady turn about the ship's centre with the nozzle a behind it, positive
    // toward the side the nose turns to (R10-5); 0 for a negative or NaN s, a non-finite or non-positive v_e, or a
    // non-finite omega or a
    SOLTEST_API double FlameTurnOffsetM(double arcM, double turnRateRadS, double exhaustSpeedMps, double nozzleOffsetM);

    // Writes a flame's spine relative to anchorM (R10-4, contract case 21): point j at s_j = j L_f / (FLAME_POINTS - 1)
    // is the gas parcel of age s_j / v_e, its pose interpolated between the bracketing poses of (head, the history
    // newest first), at shipPositionNow + offset + (v_e exhaust + nozzle velocity) tau; alpha clamp01(b) x
    // FlameAlongAlpha x clamp01(fade) (NaN as 0), 0 for a parcel older than the oldest pose (not yet emitted); width
    // FlameWidthM, unfloored. head.EmitTimeS is now. Returns the points written, min(FLAME_POINTS, outPoints.Num()); 0
    // and nothing written for an empty view, L_f 0 or a v_e that is not finite and positive
    SOLTEST_API int32 BuildFlamePoints(const FSOLPlumeHistory& history, const FSOLPlumeSample& head,
        const FVector3d& shipPositionNow, double plumeLengthM, double nozzleRadiusM, double exhaustSpeedMps,
        double fade, const FVector3d& anchorM, TArrayView<FSOLRibbonPoint> outPoints);

    // Returns the flame pixel's core weight at a screen-radial fraction r, (sqrt(1 - r^2))^PLUME_CORE_POWER, r clamped
    // to [0, 1]; 0 for NaN (R10-3; SOLTH_FlameCoreWeight mirrors it)
    SOLTEST_API double FlameCoreWeight(double radialFraction);

    // Returns the shock-diamond brightness at arc coordinate s and radial fraction r: the nearest ShockKnots knot's
    // rhombus (half-length SHOCK_KNOT_HALF_LENGTH S along, half-width SHOCK_KNOT_HALF_WIDTH across) times its strength;
    // 0 outside every knot (R10-3; SOLTH_ShockIntensity mirrors it)
    SOLTEST_API double FlameKnotIntensity(double arcM, double radialFraction, double plumeLengthM, double nozzleRadiusM,
        double brightness);

    // Returns the streak depth lerp(body, tail, S((s - 0.5 L_p) / L_p)), S the FlameWidthM smoothstep (R10-3); the body
    // depth for a NaN s or a non-finite or non-positive L_p (SOLTH_FlameStreakDepth mirrors it)
    SOLTEST_API double FlameStreakDepth(double arcM, double plumeLengthM, double bodyDepth, double tailDepth);

    // Returns the brightness a flame emits t seconds after its thrust cut out (R10-Q4): clamp01(b) x clamp(1 - t /
    // FLAME_SHUTDOWN_S, 0, 1), a negative t read as 0; 0 for a NaN b or t
    SOLTEST_API double FlameShutdownBrightness(double lastBurningBrightness, double secondsSinceCutoff);

    // Returns whether a ship shows a flame (SDD 51 section 11.1, R2, R4): it burns or still has gas in its flame, and
    // the flame's on-screen length is at least the vanish length; false for a NaN length or vanish length
    SOLTEST_API bool RibbonShown(double ribbonPx, double vanishPx, bool bHasPlume, bool bRibbonFading);

    // Returns the ribbon vanish length V of a QualityPreset (RIBBON_VANISH_PX_BY_QUALITY, the preset clamped to Low ..
    // Cinematic; R3)
    SOLTEST_API double RibbonVanishPx(int32 qualityPreset);

    // Returns a flame's on-screen length in reference px, PixelLength(FLAME_LENGTH_FACTOR L_p, d, f)
    SOLTEST_API double RibbonPixelLength(double plumeLengthM, double distanceM, double focalRefPx);

    // Returns a ribbon's alpha factor from its on-screen length (R2): 0 at and below V, rising linearly to 1 at
    // RIBBON_FADE_START_FACTOR x V, 1 above; 0 for a NaN length or vanish length and for V <= 0
    SOLTEST_API double RibbonPixelFade(double ribbonPx, double vanishPx);

    // Returns the width a ribbon point is drawn at, max(widthM, minPx d / f) (R1, f in the pixels of the floor); a NaN or
    // negative width reads as 0, the floor term is 0 for a non-finite or non-positive d, f or minPx. The ribbon
    // material's vertex shader mirrors it (SOLTH_FlameVertex, device pixels)
    SOLTEST_API double RibbonDrawnWidthM(double widthM, double distanceM, double focalPx, double minPx);

    // Returns a flame point's thin-flame shading weight (R10, the Niagara feeder's copy of SOLTH_FlameSharpness):
    // clamp01(trueWidthM / drawnWidthM) x clamp01((drawnPx - FLAME_SHARP_LOW_PX) / (FLAME_SHARP_HIGH_PX -
    // FLAME_SHARP_LOW_PX)) with drawnPx = drawnWidthM x focalPx / distanceM; a negative true width reads as 0; 1 when
    // the drawn width, distance or focal length is not positive (or NaN)
    SOLTEST_API double FlameSharpness(double trueWidthM, double drawnWidthM, double distanceM, double focalPx);

    // Returns the farthest distance at which the longest flame (FLAME_LENGTH_FACTOR x the hull length) is still V
    // long on screen, f x FLAME_LENGTH_FACTOR x L_hull / V; 0 for V <= 0 and any non-finite or non-positive input
    SOLTEST_API double RibbonReachM(double hullLengthM, double focalRefPx, double vanishPx);

    // Returns the Collect pass's NPC thrust gate (R4, R9, R10-7): max(glow reach f GLOW_DIAMETER_NOZZLE_RADII r_n /
    // GLOW_FADE_LOW_PX, burst reach f BurstLengthM(L_hull, 1) / CONE_FADE_LOW_PX, flame reach RibbonReachM(L_hull, f, V)
    // while the flames are on); each term 0 for its own bad input
    SOLTEST_API double NpcThrustGateM(double hullLengthM, double nozzleRadiusM, double focalRefPx, bool bFlames,
        double vanishPx);

    // Returns the on-screen ribbon length from which an NPC ribbon is drawn by Niagara (SDD 51 R7),
    // RIBBON_NIAGARA_PX_FACTOR x RibbonVanishPx of the preset
    SOLTEST_API double RibbonNiagaraMinPx(int32 qualityPreset);

    // Splits the shown NPC ribbons between the renderers by pixel size (R7): those at least niagaraMinPx long, at most
    // max(0, niagaraFreeTrails) of them by highest px (ties lower index), go to outNiagaraNpcIndices px descending;
    // every other entry goes to outPoolNpcIndices in input order. NaN px never qualifies; both outputs are cleared
    // first and allocate only to grow
    SOLTEST_API void SplitRibbonRenderers(TConstArrayView<FSOLRibbonShown> shown, double niagaraMinPx,
        int32 niagaraFreeTrails, TArray<int32>& outNiagaraNpcIndices, TArray<int32>& outPoolNpcIndices);

    // Returns a flame end cap's depth (51e3, contract case 25): max(FLAME_CAP_DEPTH_NOZZLE_RADII x r_n,
    // FLAME_CAP_MIN_DEPTH_M); the minimum for a NaN, infinite or non-positive r_n
    SOLTEST_API double FlameCapDepthM(double nozzleRadiusM);

    // Writes the drawn flame (51e3, contract case 25): a nozzle cap point, the spine (at most FLAME_POINTS points), a
    // tail cap point. Each cap lies FlameCapDepthM(r_n) out from its end point along the end segment (nozzle: from
    // spine 1 through spine 0; tail: from spine n - 2 through spine n - 1), on the end point itself for a one-point spine
    // or an end segment shorter than 1e-6 m; its alpha is its end point's, its width 0. Returns n + 2 (n the spine
    // points used); 0 and nothing written for an empty spine or an output shorter than n + 2. The spine may already sit
    // at outDrawn[1..n] (built in place); no other overlap is allowed
    SOLTEST_API int32 AddFlameCaps(TConstArrayView<FSOLRibbonPoint> spine, double nozzleRadiusM,
        TArrayView<FSOLRibbonPoint> outDrawn);

    // Writes a flame's strip-pool texels (R7, R10, contract case 18, caps 51e3): FLAME_DRAWN_POINTS (x, y, z, alpha)
    // texels of the drawn points (AddFlameCaps) padded with the last position at alpha 0 (zero with no points), then
    // the header (c, b, L_p, r_n), then the unfloored widths of drawn points 2..25 four per texel (0 past
    // points.Num(); drawn point 1, the nozzle, is 2 r_n from the header, the caps are 0); writes nothing unless
    // outTexels holds exactly RIBBON_TEXELS_PER_TRAIL texels
    SOLTEST_API void PackRibbonTexels(TConstArrayView<FSOLRibbonPoint> points, float colourScalar, float brightness,
        float plumeLengthM, float nozzleRadiusM, TArrayView<FVector4f> outTexels);

    // Returns the strip pool texel (column, row) of a trail's first texel, mirroring SOLTH_RibbonTexel:
    // ((t % RIBBON_TRAILS_PER_BLOCK) x RIBBON_TEXELS_PER_TRAIL, t / RIBBON_TRAILS_PER_BLOCK); (-1, -1) for t < 0
    SOLTEST_API FIntPoint RibbonTexelOrigin(int32 trail);

    // Returns the strip blocks (ISM instances and texture rows) that hold a trail count, ceil(trails /
    // RIBBON_TRAILS_PER_BLOCK); 0 for a count <= 0
    SOLTEST_API int32 RibbonBlockCount(int32 trails);

    // Returns the ribbon frame anchor for a camera in render-space cm: each component rounded to whole metres, so the
    // anchor is exact in float (within 2^24 cm) and both renderers use the same value; zero for a non-finite input
    SOLTEST_API FVector3d RibbonAnchorCm(const FVector3d& cameraRenderCm);

    // Returns this frame's ribbon anchor with the hold (51e3, contract case 24): the held anchor while bHasHeld, the
    // held anchor is finite and the camera lies within RIBBON_ANCHOR_HOLD_M of it (straight-line distance, the bound
    // included), else RibbonAnchorCm(camera)
    SOLTEST_API FVector3d RibbonAnchorHoldCm(const FVector3d& heldAnchorCm, bool bHasHeld,
        const FVector3d& cameraRenderCm);

    // Returns the device focal length in device pixels, projectionScaleY x viewHeightPx / 2 (ViewToClip[1][1] and
    // the unscaled view height, as SOLTH_FlameVertex floors); 0 for a non-finite or non-positive input
    SOLTEST_API double RibbonDeviceFocalPx(double projectionScaleY, double viewHeightPx);

    // Returns whether a history slot of the shared flame store may be released this frame (contract case 24): it holds
    // no pose, its newest pose with b > 0 is at least flameLifeS old (NaN age too: all its gas has left the flame), or
    // it was not fed this frame and its last pixel fade was 0 (NaN as 0)
    SOLTEST_API bool RibbonTrailReleasable(bool bFedThisFrame, int32 sampleCount, double newestBurningAgeS,
        double flameLifeS, double pixelFade);

    // Returns the player hull light's intensity b x maxIntensity, b clamped to [0, 1] (NaN 0); 0 for a non-finite or
    // negative maximum
    SOLTEST_API float PlumeLightIntensity(float brightness, float maxIntensity);

    // Returns the player hull light's colour, the ramp's rim colour at c
    SOLTEST_API FLinearColor PlumeLightColour(double c);

    // Returns a hull's nozzle table (SOL::Thruster); an unknown kind reads as NpcFaction0
    SOLTEST_API const FSOLHullThrusters& HullThrusters(ESOLHullKind kind);

    // Returns whether the player hull light is on (Q28): always in VR, else at every preset above Low
    SOLTEST_API bool PlayerLightEnabled(int32 qualityPreset, bool bVR);

    // Returns a world thrust (m/s^2) in the ship frame of an attitude (ship-local -> universe, Unreal-handed: X nose,
    // Y right, Z top); zero for a non-finite input (part 51d: each flight substep's thrust, then FSOLThrustMean)
    SOLTEST_API FVector3d ShipThrustLocalMps2(const FQuat4d& orientation, const FVector3d& thrustWorldMps2);

    // Returns the player's plume floats (PlumeInstance of the forward G on the PLAYER_HULL_LENGTH_M hull) and hull
    // light (visible when bLightEnabled and PlumeBrightness of the forward G > 0, PlumeLightIntensity of it with
    // PLAYER_LIGHT_MAX_INTENSITY_CD, PlumeLightColour of the plume's c; not cut by the pixel size) (part 51d)
    SOLTEST_API FSOLPlayerPlume PlayerPlume(const FVector3f& thrustLocalG, bool bWarPower, double distanceM,
        double focalRefPx, bool bLightEnabled);
}
