/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure analytic ring and planet shadow math (SDD 16): the test oracle and reference implementation that
// Shaders/Private/SOLRingShadow.ush mirrors function for function. Units are metres and radians. The ring frame is
// planet-centred with z along the ring normal (the planet's pole) and x and y any orthonormal pair in the ring plane:
// every result depends only on z and the in-plane radius, so handedness does not matter.
// "Toward the Sun" vectors need not be normalized. Lit (transmittance 1) is the safe default for bad input.
namespace SOLRingShadow
{
    // One band of a radial ring profile: normal optical depth Tau over [InnerM, OuterM)
    struct FTauBand
    {
        double InnerM = 0.0;
        double OuterM = 0.0;
        double Tau = 0.0;
    };

    // Umbra and penumbra radii of a planet's shadow at some distance behind it
    struct FPlanetShadowRadii
    {
        double UmbraRadiusM = 0.0;
        double PenumbraRadiusM = 0.0;
    };

    // Returns the Sun's angular radius asin(R_sun / d); PI/2 at or inside the Sun, 0 for NaN or +inf
    SOLTEST_API double SunAngularRadiusRad(double sunDistanceM);

    // Returns the penumbra half-width L tan(alpha) a receiver sees at distance L from an occluding edge (alpha clamped
    // to RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD); 0 for non-positive or non-finite input
    SOLTEST_API double PenumbraHalfWidthM(double distanceToOccluderM, double sunAngularRadiusRad);

    // Returns the radial half-extent w sqrt(1 + c^2 (1/mu^2 - 1)) of the Sun's footprint on the ring plane, with
    // mu = |sunElevationSin| clamped to [RING_SHADOW_MIN_SUN_ELEVATION_SIN, 1] and c clamped to [-1, 1]
    SOLTEST_API double FootprintRadialHalfWidthM(double penumbraHalfWidthM, double sunElevationSin,
        double radialSunAzimuthCos);

    // Returns the tau of the band containing radiusM (inner inclusive, outer exclusive), 0 outside every band
    SOLTEST_API double OpticalDepthAt(double radiusM, TConstArrayView<FTauBand> profile);

    // Returns the exact mean of exp(-tau(r) * slantFactor) over r in [c - h, c + h] (a radial box filter over the
    // piecewise-constant profile); a point sample for a bad window, 1 for a bad slant or centre
    SOLTEST_API double FilteredTransmittance(double centerRadiusM, double halfWidthM, double slantFactor,
        TConstArrayView<FTauBand> profile);

    // Returns 0.5 erfc(z / (sigma sqrt 2)): the fraction of a Gaussian layer between the receiver and the Sun (a step
    // for a zero, negative or non-finite thickness)
    SOLTEST_API double LayerFractionTowardSun(double heightTowardSunM, double layerHalfThicknessM);

    // Returns the ring's transmittance of sunlight to a receiver (ring frame), with a physically soft penumbra from the
    // Sun's angular radius and a Gaussian layer of the given 1-sigma half-thickness (0 = thin plane)
    SOLTEST_API double RingShadowTransmittance(const FVector3d& receiverRingFrameM, const FVector3d& towardSunRingFrame,
        double sunAngularRadiusRad, double layerHalfThicknessM, TConstArrayView<FTauBand> profile);

    // Returns the fraction of a uniform Sun disc (radius a) left uncovered by an occluder disc (radius b) whose centre
    // is |separation| away, using flat-angle disc geometry
    SOLTEST_API double SunVisibleFraction(double sunAngularRadiusRad, double occluderAngularRadiusRad,
        double separationRad);

    // Returns the fraction of the Sun a spherical planet leaves visible from a point (relative to the planet's
    // centre); 0 inside the planet, 1 for bad input, a hard cylinder for a zero or bad alpha
    SOLTEST_API double PlanetShadowTransmittance(const FVector3d& pointRelPlanetM, const FVector3d& towardSun,
        double planetRadiusM, double sunAngularRadiusRad);

    // Returns the sunlight fraction one body and its rings leave at a point (all vectors in one arbitrary frame):
    // PlanetShadowTransmittance times RingShadowTransmittance in a frame whose z is ringNormal (any length); the ring
    // term is 1 for an empty profile or a zero or non-finite normal. Used for the ship's and targets' sunlight (16d)
    SOLTEST_API double BodySunTransmittance(const FVector3d& pointRelBodyM, const FVector3d& towardSun,
        double bodyRadiusM, double sunAngularRadiusRad, const FVector3d& ringNormal, double layerHalfThicknessM,
        TConstArrayView<FTauBand> profile);

    // Returns whether the ship's sunlight is shadowed this frame given last frame's state and this frame's sunlight
    // fraction, with hysteresis: enter below SOL::SHIP_SUN_SHADOW_ENTER_BELOW, leave at SHIP_SUN_SHADOW_LEAVE_AT or
    // above; a NaN fraction keeps the previous state
    SOLTEST_API bool NextShipShadowedState(bool bWasShadowed, double sunTransmittance);

    // Returns the planet shadow's umbra and penumbra radii at distanceBehindM behind the planet
    SOLTEST_API FPlanetShadowRadii PlanetShadowRadiiM(double planetRadiusM, double distanceBehindM,
        double sunAngularRadiusRad);

    // Returns the real optical-depth profile of SOL::RING_PROFILE_INDEX_* (static data, no allocation); empty for any
    // other index. Ported from M_SOLRingFar's 52-band table; the single source of truth for the shaders' copy
    SOLTEST_API TConstArrayView<FTauBand> RealTauProfile(int32 profileIndex);

    // Returns the full text of Shaders/Private/SOLRingShadowProfile.ush (constants and every RealTauProfile band, in
    // km), generated so the shaders' copy cannot drift from the C++ table. Tooling only: allocates a string
    SOLTEST_API FString BuildShaderProfileSource();

    // Returns the binary search steps the shader needs (SOLRS_SEARCH_STEPS): the smallest n with 2^n >= the largest
    // profile's band count + 1, so the search always ends on the right band
    SOLTEST_API int32 ShaderSearchSteps();

    // Shadow state at a viewpoint near a ring, constant across the dense rock window (+-1.5 km, far below every
    // penumbra width), which materials combine with each pixel's own height for the layer's self-shadowing
    struct FViewpointShadowState
    {
        double PlanetShadow = 1.0;     // PlanetShadowTransmittance at the viewpoint
        double SlantOpticalDepth = 0.0; // tau(r) / |mu| at the viewpoint's ring radius (0 if the Sun is in the plane)
        double SunSide = 1.0;          // +1 if the Sun is on the ring normal's side of the plane, -1 otherwise
        double HeightM = 0.0;          // viewpoint height above the ring plane along the normal
    };

    // Returns the shadow state at a viewpoint (relative to the planet's centre, any frame) given the ring normal in
    // that frame; lit and unshadowed for degenerate input
    SOLTEST_API FViewpointShadowState ComputeViewpointShadowState(const FVector3d& viewpointRelPlanetM,
        const FVector3d& towardSun, const FVector3d& ringNormal, double planetRadiusM, double sunAngularRadiusRad,
        TConstArrayView<FTauBand> profile);

    // Returns the SOL::RING_PROFILE_INDEX_* of a ringed planet's name, or SOL::RING_PROFILE_INDEX_INFER (-1) otherwise
    SOLTEST_API int32 ProfileIndexForPlanet(FName planetName);

    // Returns the material input (unit Sun direction xyz, w = the Sun's angular radius) for a Sun vector given in the
    // receiver's local axes (any length) at sunDistanceM from the Sun; zero for a zero or non-finite vector
    SOLTEST_API FVector4d SunDirectionAndAngularRadius(const FVector3d& towardSunLocal, double sunDistanceM);

    // Returns the far ring disc's RingSunDirection: the unit Sun direction scaled by (1 + alpha), so the material reads
    // alpha back as its length - 1 without another Niagara parameter; zero stays zero (no host Sun)
    SOLTEST_API FVector3d EncodeSunDirectionWithAngularRadius(const FVector3d& towardSunUnit,
        double sunAngularRadiusRad);
}
