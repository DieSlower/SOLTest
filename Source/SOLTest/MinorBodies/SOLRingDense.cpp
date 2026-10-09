/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingDense.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Returns positionCm modulo the tile size 2 * windowRadiusCm, always in [0, 2W); 0 for a non-positive window
double SOLRingDense::WindowPhaseCm(double positionCm, double windowRadiusCm)
{
    if (windowRadiusCm <= 0.0)
    {
        return 0.0;
    }
    const double tileCm = 2.0 * windowRadiusCm;
    double phaseCm = FMath::Fmod(positionCm, tileCm);
    if (phaseCm < 0.0)
    {
        phaseCm += tileCm;
    }
    // A tiny negative remainder can round the sum up to exactly the tile size; keep the half-open range
    return phaseCm >= tileCm ? 0.0 : phaseCm;
}

//////////////////////////////////////////////////////////////////////////
// Advances the ring's co-rotation angle by the Keplerian rate over deltaSeconds, normalized into [0, 2*PI)
double SOLRingDense::AdvanceSpinAngle(double angleRad, double planetGM, double planeRadiusM, double deltaSeconds)
{
    double advancedRad = angleRad;
    if (planeRadiusM > 0.0 && planetGM > 0.0)
    {
        advancedRad += FMath::Sqrt(planetGM / (planeRadiusM * planeRadiusM * planeRadiusM)) * deltaSeconds;
    }
    constexpr double twoPi = 2.0 * UE_DOUBLE_PI;
    if (!FMath::IsFinite(advancedRad))
    {
        // A NaN/inf input (bad GM, position or time step) must not poison the integrated angle for the whole session
        advancedRad = FMath::IsFinite(angleRad) ? angleRad : 0.0;
    }
    double normalizedRad = FMath::Fmod(advancedRad, twoPi);
    if (normalizedRad < 0.0)
    {
        normalizedRad += twoPi;
    }
    return normalizedRad >= twoPi ? 0.0 : normalizedRad;
}

//////////////////////////////////////////////////////////////////////////
// Returns the dense layer's weight: 1 near the ring, 0 far from it, smoothstep between
double SOLRingDense::ComputeDenseAlpha(double distanceOutsideRingVolumeM, double fadeInDistanceM, double fadeOutDistanceM)
{
    if (distanceOutsideRingVolumeM <= fadeInDistanceM)
    {
        return 1.0;
    }
    if (fadeOutDistanceM <= fadeInDistanceM || distanceOutsideRingVolumeM >= fadeOutDistanceM)
    {
        return 0.0;
    }
    const double t = (distanceOutsideRingVolumeM - fadeInDistanceM) / (fadeOutDistanceM - fadeInDistanceM);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

//////////////////////////////////////////////////////////////////////////
// Returns the camera's position in the ring's co-rotating frame, in centimeters
FVector3d SOLRingDense::RingFramePositionCm(const FVector3d& relativeM, const FVector3d& ringAxisE1,
    const FVector3d& ringAxisE2, const FVector3d& ringNormal, double spinAngleRad)
{
    const double x = FVector3d::DotProduct(relativeM, ringAxisE1);
    const double y = FVector3d::DotProduct(relativeM, ringAxisE2);
    double sinAngle = 0.0;
    double cosAngle = 1.0;
    FMath::SinCos(&sinAngle, &cosAngle, spinAngleRad);
    // Rotation by -angle: the carpet turns with the ring, so the camera moves backwards through it
    return FVector3d(x * cosAngle + y * sinAngle, -x * sinAngle + y * cosAngle,
        FVector3d::DotProduct(relativeM, ringNormal)) * SOL::METERS_TO_CM;
}

//////////////////////////////////////////////////////////////////////////
// Converts a ring-frame position to the Niagara component's mirrored-Y local axes
FVector3d SOLRingDense::RingFrameToNiagaraLocalCm(const FVector3d& ringFrameCm)
{
    return FVector3d(ringFrameCm.X, -ringFrameCm.Y, ringFrameCm.Z);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the dense layer should draw: it has weight and the time warp is not too high
bool SOLRingDense::ShouldShowDenseTier(double denseAlpha, double timeWarpFactor, double maxTimeWarpFactor)
{
    return denseAlpha > 0.0 && maxTimeWarpFactor > 0.0 && timeWarpFactor <= maxTimeWarpFactor;
}

//////////////////////////////////////////////////////////////////////////
// Returns the fraction of dense rocks to show for a ring of the given relative density (NaN shows them all)
double SOLRingDense::DenseFillFraction(double ringDensity)
{
    return FMath::IsNaN(ringDensity) ? 1.0 : FMath::Clamp(ringDensity, 0.0, 1.0);
}
