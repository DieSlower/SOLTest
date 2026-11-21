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

// One ribbon history sample (SDD 51 section 4.8, Q4): non-rotating world-aligned axes relative to the ship
struct FSOLPlumeSample
{
    double EmitTimeS = 0.0;                         // Emission time
    FVector3f OffsetM = FVector3f::ZeroVector;      // Nozzle exit relative to the ship's centre at emission
    FVector3f ExhaustDir = FVector3f::ZeroVector;   // Unit exhaust direction at emission
    float DriftMps = 0.0f;                          // u = RIBBON_LENGTH_FACTOR L_p / RIBBON_MAX_AGE_S
    float Brightness = 0.0f;                        // b at emission
};

// A plume's ribbon history: a fixed ring of RIBBON_SAMPLES samples, newest first by age, no allocation
class SOLTEST_API FSOLPlumeHistory
{
public:

    // Empties the history
    void Reset();

    // Stores a sample as the newest (overwriting the oldest when full); false, nothing stored, if any field is
    // non-finite
    bool Push(const FSOLPlumeSample& sample);

    // Returns the number of samples held (at most RIBBON_SAMPLES)
    int32 Num() const;

    // Returns the sample of age rank i (0 = newest, Num() - 1 = oldest); i must lie in [0, Num())
    const FSOLPlumeSample& ByAge(int32 i) const;

    // Returns whether a sample is due: always when empty, else once nowS is at least RIBBON_MAX_AGE_S /
    // RIBBON_SAMPLES past the newest sample's emission
    bool ShouldSample(double nowS) const;

private:

    FSOLPlumeSample mSamples[SOL::Thruster::RIBBON_SAMPLES];    // Ring storage
    int32 mNewest = 0;                                          // Slot of the newest sample
    int32 mCount = 0;                                           // Samples held
};

// One ship offered to the ribbon selection (SOLThruster::SelectRibbonShips)
struct FSOLRibbonCandidate
{
    int32 NpcIndex = INDEX_NONE;
    double DistanceM = 0.0;     // Distance from the camera
    double RibbonPx = 0.0;      // Ribbon length on screen, reference px (the ranking key)
    bool bHasPlume = false;     // The ship shows a plume now
    bool bRibbonFading = false; // The ship still has ribbon samples ageing out
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

// Pure math of the issue #51 thruster plume (SDD 51 sections 4.2-4.9, contract
// Docs/Plans/51-thruster-math-contract.md): the plume scalars and colour ramp, shock diamonds, the hull-frame thrust and
// its burst axes, the pixel-size rule, the ribbon history and selection, quality scaling, the player hull light and the
// per-hull nozzle table. Constants in SOL::Thruster. The plume material's HLSL mirror (part 51b) must follow any change
// to PlumeColour, PlumeBrightness or ShockKnots
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

    // Returns a ribbon sample's drift speed RIBBON_LENGTH_FACTOR L_p / RIBBON_MAX_AGE_S; 0 for a non-finite or
    // non-positive plume length
    SOLTEST_API float RibbonDriftMps(double plumeLengthM);

    // Returns where a ribbon sample is drawn: ship now + offset + exhaust x drift x age (age = now - emission, a
    // negative or NaN age read as 0)
    SOLTEST_API FVector3d RibbonPoint(const FVector3d& shipPositionNow, const FSOLPlumeSample& sample, double nowS);

    // Returns a ribbon sample's alpha (1 - age / RIBBON_MAX_AGE_S) x b, age clamped to [0, A] (negative as 0), b
    // clamped to [0, 1]; 0 for a NaN age or brightness
    SOLTEST_API float RibbonAlpha(double ageS, float brightness);

    // Returns a ribbon's width r_n (1 + age / RIBBON_MAX_AGE_S), age clamped to [0, A] (NaN as 0); 0 for a non-finite
    // or non-positive nozzle radius
    SOLTEST_API float RibbonWidthM(double ageS, double nozzleRadiusM);

    // Writes the ribbon ships into outNpcIndices (cleared first): candidates within radiusM (inclusive) with a plume or
    // a fading ribbon, ranked by ribbon px descending (NaN as 0), ties by the lower NPC index, at most maxCount (none
    // for maxCount <= 0); a partial sort in the output array, which allocates only to grow
    SOLTEST_API void SelectRibbonShips(TConstArrayView<FSOLRibbonCandidate> candidates, double radiusM, int32 maxCount,
        TArray<int32>& outNpcIndices);

    // Returns the ribbon ship count of a QualityPreset (RIBBON_COUNTS_BY_QUALITY, the preset clamped to Low ..
    // Cinematic; Cinematic is RIBBON_COUNT_UNLIMITED)
    SOLTEST_API int32 RibbonCountForQuality(int32 qualityPreset);

    // Returns whether the player's plume has a ribbon: at every preset above Low
    SOLTEST_API bool PlayerHasRibbon(int32 qualityPreset);

    // Returns the player hull light's intensity b x maxIntensity, b clamped to [0, 1] (NaN 0); 0 for a non-finite or
    // negative maximum
    SOLTEST_API float PlumeLightIntensity(float brightness, float maxIntensity);

    // Returns the player hull light's colour, the ramp's rim colour at c
    SOLTEST_API FLinearColor PlumeLightColour(double c);

    // Returns a hull's nozzle table (SOL::Thruster); an unknown kind reads as NpcFaction0
    SOLTEST_API const FSOLHullThrusters& HullThrusters(ESOLHullKind kind);

    // Returns whether a ship may show a plume and gas bursts: only when drawn with a hull mesh, never as a dot (Q27)
    SOLTEST_API bool PlumeAllowed(bool bHullDrawn);

    // Returns whether the player hull light is on (Q28): always in VR, else at every preset above Low
    SOLTEST_API bool PlayerLightEnabled(int32 qualityPreset, bool bVR);
}
