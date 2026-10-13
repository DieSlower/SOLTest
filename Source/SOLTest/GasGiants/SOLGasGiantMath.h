/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

/**
 * Gas giant animation and LOD math (SDD 14, plan "14a contract"): differential-rotation band phases, storm placement
 * and drift, and the near volumetric layer's blend and resource-release rule. Band and storm motion is a pure function
 * of sim time (never an accumulator), so it follows time warp, is pause-safe and deterministic; phases are reduced in
 * double on the CPU and handed to materials as small floats in [0, 1). Unit-tested in SOLTest.GasGiantMath.*. Pure:
 * no UObjects, no world access, no global state. Angles in radians, times in seconds, phases in turns.
 */
namespace SOLGasGiant
{
    // A storm vortex on the sphere: centre latitude, longitude in the body frame at t = 0, and angular radius
    struct FStorm
    {
        double LatitudeRad = 0.0;
        double LongitudeRad = 0.0;
        double RadiusRad = 0.0;
    };

    // Near volumetric layer residency: whether its resources are allocated, and how long it has been hidden
    struct FNearLayerState
    {
        bool bActive = false;
        double HiddenSeconds = 0.0;
    };

    // The two band samples a latitude draws from and the upper sample's cross-fade weight (SDD 14 Amendment 3)
    struct FBandBlend
    {
        int32 Lower = 0;
        int32 Upper = 0;
        double UpperWeight = 0.0;
    };

    // What the caller must do with the near layer's resources after a StepNearLayer call
    enum class ENearLayerAction : uint8
    {
        None,
        Activate,
        Release,
    };

    // Returns the band drift rate relative to the body's spin at a latitude: eq + (polar - eq) * sin^2(lat). Latitude
    // is clamped to [-PI/2, PI/2] (NaN = equator); a non-finite drift input counts as 0
    SOLTEST_API double DriftRateRadPerS(double latitudeRad, double equatorDriftRadPerS, double polarDriftRadPerS);

    // Returns frac(simSeconds * rate * speedFactor / 2PI) in [0, 1); 0 for speedFactor <= 0 or any non-finite input
    SOLTEST_API double PhaseTurns(double simSeconds, double rateRadPerS, double speedFactor);

    // Returns the phase (turns, [0, 1)) of the band at a latitude: PhaseTurns at that latitude's drift rate
    SOLTEST_API double BandPhaseTurns(double simSeconds, double latitudeRad, double equatorDriftRadPerS,
        double polarDriftRadPerS, double speedFactor);

    // Returns a phase wrapped into float [0, 1) for a material parameter; a value rounding to 1.0f or non-finite is 0
    SOLTEST_API float PhaseToShader(double phaseTurns);

    // Returns the latitude of band sample index (of count), evenly spaced from -PI/2 to +PI/2; 0 for count <= 1
    SOLTEST_API double BandSampleLatitudeRad(int32 index, int32 count);

    // Fills every slot with the shader phase of its band sample latitude (count = outPhases.Num()); no allocation
    SOLTEST_API void ComputeBandPhases(double simSeconds, double equatorDriftRadPerS, double polarDriftRadPerS,
        double speedFactor, TArrayView<float> outPhases);

    // Returns the wrap-safe band lookup at a latitude (mirrored by the gas giant materials): the latitude lies between
    // samples Lower and Upper = Lower + 1 (of count, south to north); each sample's phase is applied rigidly near it
    // and the two advected fields are cross-faded with UpperWeight = smoothstep(0.5 - h, 0.5 + h, w) over the middle
    // 2h of the gap (h = blendHalfWidth clamped to [0, 0.5]; h = 0 is a step at w = 0.5), so the result depends only
    // on each phase modulo one turn and never jumps when neighbouring phases drift more than half a turn apart.
    // count < 2 -> {0, 0, 0}; latitude clamped to [-PI/2, PI/2], NaN = equator
    SOLTEST_API FBandBlend BandBlendAt(double latitudeRad, int32 count, double blendHalfWidth);

    // Returns the haze shell's opacity scale while the near volumetric layer is blended in: 1 - blend clamped to
    // [0, 1] (the volumetric clouds replace the haze); a non-finite blend counts as 0 (haze fully shown)
    SOLTEST_API double HazeFadeForNearBlend(double nearBlend);

    // Returns how far the band/storm drift clock advances this frame (SDD 14 Amendment 4): the sim time step, capped
    // in magnitude to realDeltaSeconds * maxTimeWarp so drift above that warp runs at that warp's speed instead of
    // aliasing. Sign kept; non-finite or negative real delta -> 0; non-finite sim delta -> 0; maxTimeWarp <= 0 or
    // non-finite -> no cap
    SOLTEST_API double CappedDriftStepSeconds(double simDeltaSeconds, double realDeltaSeconds, double maxTimeWarp);

    // Returns true if a sphere may be on screen: inside it, or within halfAngleRad of the camera's forward axis plus
    // the sphere's angular radius (a cone test, conservative for a rectangular view when halfAngleRad covers its
    // corners).
    // A zero forward vector or a non-finite input counts as visible
    SOLTEST_API bool IsSphereInViewCone(const FVector3d& cameraLocation, const FVector3d& cameraForward,
        double halfAngleRad, const FVector3d& sphereCenter, double sphereRadius);

    // Replaces outStorms with up to count (clamped to SOL::GAS_GIANT_MAX_STORMS) deterministic, non-overlapping,
    // prefix-stable storms within |lat| <= maxAbsLatitudeRad and radius in [min, max]; unplaceable storms are skipped
    SOLTEST_API void GenerateStorms(int32 seed, int32 count, double maxAbsLatitudeRad, double minRadiusRad,
        double maxRadiusRad, TArray<FStorm>& outStorms);

    // Returns a storm's body-frame longitude at a sim time in [0, 2PI): it rides the band at its latitude
    SOLTEST_API double StormLongitudeRad(const FStorm& storm, double simSeconds, double equatorDriftRadPerS,
        double polarDriftRadPerS, double speedFactor);

    // Returns the near layer's blend from the camera distance to the planet centre: 1 within fadeIn radii, 0 beyond
    // fadeOut radii, 1 - smoothstep between; a hard step for fadeOut <= fadeIn; 0 for invalid distance or radius
    SOLTEST_API double NearLayerBlend(double distanceToCenterM, double planetRadiusM, double fadeInRadii,
        double fadeOutRadii);

    // Returns true if a blend shows the near layer (finite and > 0)
    SOLTEST_API bool IsNearLayerVisible(double blend);

    // Advances the near layer's residency by one frame of real (unwarped) time and returns what to do with its
    // resources: Activate when it first becomes visible, Release once hidden for releaseDelaySeconds
    SOLTEST_API ENearLayerAction StepNearLayer(FNearLayerState& state, double blend, double realDeltaSeconds,
        double releaseDelaySeconds);
}
