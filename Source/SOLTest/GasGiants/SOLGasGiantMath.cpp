/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "GasGiants/SOLGasGiantMath.h"

#include "SOLConstants.h"

#include "Math/RandomStream.h"

#include <cmath>

namespace
{
    constexpr double GAS_GIANT_TWO_PI = 2.0 * UE_DOUBLE_PI;

    //////////////////////////////////////////////////////////////////////////
    // Returns the fractional part of a finite value in [0, 1), never exactly 1
    double GasGiantFracTurns(const double value)
    {
        const double frac = value - std::floor(value);
        return (frac >= 1.0 || frac < 0.0) ? 0.0 : frac;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an angle wrapped into [0, 2PI), or 0 if it is non-finite
    double GasGiantWrapAngleRad(const double angleRad)
    {
        if (!std::isfinite(angleRad))
        {
            return 0.0;
        }
        double wrapped = std::fmod(angleRad, GAS_GIANT_TWO_PI);
        if (wrapped < 0.0)
        {
            wrapped += GAS_GIANT_TWO_PI;
        }
        return (wrapped >= GAS_GIANT_TWO_PI) ? 0.0 : wrapped;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the great-circle angle (rad) between two points on the unit sphere (haversine, stable for small angles)
    double GasGiantGreatCircleRad(const double lat1, const double lon1, const double lat2, const double lon2)
    {
        const double sLat = std::sin(0.5 * (lat2 - lat1));
        const double sLon = std::sin(0.5 * (lon2 - lon1));
        const double h = sLat * sLat + std::cos(lat1) * std::cos(lat2) * sLon * sLon;
        return 2.0 * std::asin(std::sqrt(FMath::Clamp(h, 0.0, 1.0)));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the band drift rate relative to the body's spin at a latitude (sin^2 differential-rotation profile)
double SOLGasGiant::DriftRateRadPerS(const double latitudeRad, const double equatorDriftRadPerS,
    const double polarDriftRadPerS)
{
    const double eq = std::isfinite(equatorDriftRadPerS) ? equatorDriftRadPerS : 0.0;
    const double polar = std::isfinite(polarDriftRadPerS) ? polarDriftRadPerS : 0.0;
    const double lat = std::isnan(latitudeRad) ? 0.0 : FMath::Clamp(latitudeRad, -0.5 * UE_DOUBLE_PI,
        0.5 * UE_DOUBLE_PI);
    const double s = std::sin(lat);
    return eq + (polar - eq) * s * s;
}

//////////////////////////////////////////////////////////////////////////
// Returns frac(simSeconds * rate * speedFactor / 2PI) in [0, 1); 0 for a non-positive factor or non-finite input
double SOLGasGiant::PhaseTurns(const double simSeconds, const double rateRadPerS, const double speedFactor)
{
    if (!std::isfinite(simSeconds) || !std::isfinite(rateRadPerS) || !std::isfinite(speedFactor) ||
        speedFactor <= 0.0)
    {
        return 0.0;
    }
    const double turns = simSeconds * (rateRadPerS * speedFactor) / GAS_GIANT_TWO_PI;
    return std::isfinite(turns) ? GasGiantFracTurns(turns) : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the phase of the band at a latitude
double SOLGasGiant::BandPhaseTurns(const double simSeconds, const double latitudeRad,
    const double equatorDriftRadPerS, const double polarDriftRadPerS, const double speedFactor)
{
    return PhaseTurns(simSeconds, DriftRateRadPerS(latitudeRad, equatorDriftRadPerS, polarDriftRadPerS),
        speedFactor);
}

//////////////////////////////////////////////////////////////////////////
// Returns a phase wrapped into float [0, 1) for a material parameter
float SOLGasGiant::PhaseToShader(const double phaseTurns)
{
    if (!std::isfinite(phaseTurns))
    {
        return 0.0f;
    }
    const float phase = static_cast<float>(GasGiantFracTurns(phaseTurns));
    return (phase >= 1.0f || phase < 0.0f) ? 0.0f : phase;
}

//////////////////////////////////////////////////////////////////////////
// Returns the latitude of a band sample, evenly spaced from the south to the north pole
double SOLGasGiant::BandSampleLatitudeRad(const int32 index, const int32 count)
{
    if (count <= 1)
    {
        return 0.0;
    }
    const int32 clamped = FMath::Clamp(index, 0, count - 1);
    return -0.5 * UE_DOUBLE_PI + UE_DOUBLE_PI * static_cast<double>(clamped) / static_cast<double>(count - 1);
}

//////////////////////////////////////////////////////////////////////////
// Fills every slot with the shader phase of its band sample latitude
void SOLGasGiant::ComputeBandPhases(const double simSeconds, const double equatorDriftRadPerS,
    const double polarDriftRadPerS, const double speedFactor, TArrayView<float> outPhases)
{
    const int32 count = outPhases.Num();
    for (int32 index = 0; index < count; ++index)
    {
        outPhases[index] = PhaseToShader(BandPhaseTurns(simSeconds, BandSampleLatitudeRad(index, count),
            equatorDriftRadPerS, polarDriftRadPerS, speedFactor));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the two band samples a latitude draws from and the upper one's cross-fade weight
SOLGasGiant::FBandBlend SOLGasGiant::BandBlendAt(const double latitudeRad, const int32 count,
    const double blendHalfWidth)
{
    FBandBlend blend;
    if (count < 2)
    {
        return blend;
    }

    // Position between samples: sample i sits at -PI/2 + PI i / (count - 1)
    const double lat = std::isnan(latitudeRad) ? 0.0 : FMath::Clamp(latitudeRad, -0.5 * UE_DOUBLE_PI,
        0.5 * UE_DOUBLE_PI);
    const double sampleF = (lat / UE_DOUBLE_PI + 0.5) * static_cast<double>(count - 1);
    blend.Lower = FMath::Clamp(static_cast<int32>(std::floor(sampleF)), 0, count - 2);
    blend.Upper = blend.Lower + 1;
    const double w = FMath::Clamp(sampleF - static_cast<double>(blend.Lower), 0.0, 1.0);

    // Cross-fade over the middle of the gap; each sample alone near itself
    const double h = std::isfinite(blendHalfWidth) ? FMath::Clamp(blendHalfWidth, 0.0, 0.5) : 0.0;
    if (h <= 0.0)
    {
        blend.UpperWeight = w >= 0.5 ? 1.0 : 0.0;
        return blend;
    }
    const double x = FMath::Clamp((w - (0.5 - h)) / (2.0 * h), 0.0, 1.0);
    blend.UpperWeight = x * x * (3.0 - 2.0 * x);
    return blend;
}

//////////////////////////////////////////////////////////////////////////
// Returns the haze shell's opacity scale for a near-layer blend
double SOLGasGiant::HazeFadeForNearBlend(const double nearBlend)
{
    return std::isfinite(nearBlend) ? 1.0 - FMath::Clamp(nearBlend, 0.0, 1.0) : 1.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the sim time step capped to realDelta * maxTimeWarp in magnitude (drift anti-aliasing at high warp)
double SOLGasGiant::CappedDriftStepSeconds(const double simDeltaSeconds, const double realDeltaSeconds,
    const double maxTimeWarp)
{
    if (!std::isfinite(simDeltaSeconds) || !std::isfinite(realDeltaSeconds) || realDeltaSeconds < 0.0)
    {
        return 0.0;
    }
    if (!std::isfinite(maxTimeWarp) || maxTimeWarp <= 0.0)
    {
        return simDeltaSeconds;
    }
    const double limit = realDeltaSeconds * maxTimeWarp;
    return FMath::Clamp(simDeltaSeconds, -limit, limit);
}

//////////////////////////////////////////////////////////////////////////
// Returns true if a sphere is inside, or overlaps, the camera's view cone
bool SOLGasGiant::IsSphereInViewCone(const FVector3d& cameraLocation, const FVector3d& cameraForward,
    const double halfAngleRad, const FVector3d& sphereCenter, const double sphereRadius)
{
    const FVector3d toCenter = sphereCenter - cameraLocation;
    const double distance = toCenter.Length();
    const double forwardLength = cameraForward.Length();
    if (!std::isfinite(distance) || !std::isfinite(forwardLength) || !std::isfinite(halfAngleRad) ||
        !std::isfinite(sphereRadius) || forwardLength <= 0.0)
    {
        return true;
    }
    if (distance <= FMath::Max(sphereRadius, 0.0))
    {
        return true;
    }

    // Angle between the forward axis and the centre, against the cone half-angle widened by the angular radius
    const double cosAngle = FMath::Clamp(toCenter.Dot(cameraForward) / (distance * forwardLength), -1.0, 1.0);
    const double angularRadius = FMath::Asin(FMath::Clamp(FMath::Max(sphereRadius, 0.0) / distance, 0.0, 1.0));
    return FMath::Acos(cosAngle) <= FMath::Max(halfAngleRad, 0.0) + angularRadius;
}

//////////////////////////////////////////////////////////////////////////
// Replaces outStorms with deterministic, non-overlapping, prefix-stable storms
void SOLGasGiant::GenerateStorms(const int32 seed, const int32 count, const double maxAbsLatitudeRad,
    const double minRadiusRad, const double maxRadiusRad, TArray<FStorm>& outStorms)
{
    outStorms.Reset();

    // Sanitize the inputs; an unusable radius range gives no storms
    if (!std::isfinite(minRadiusRad) || !std::isfinite(maxRadiusRad))
    {
        return;
    }
    double minRadius = minRadiusRad;
    double maxRadius = maxRadiusRad;
    if (minRadius > maxRadius)
    {
        Swap(minRadius, maxRadius);
    }
    minRadius = FMath::Max(minRadius, 0.0);
    if (maxRadius <= 0.0)
    {
        return;
    }
    const double maxLat = std::isnan(maxAbsLatitudeRad) ? 0.0 : FMath::Clamp(maxAbsLatitudeRad, 0.0,
        0.5 * UE_DOUBLE_PI);
    const int32 wanted = FMath::Clamp(count, 0, SOL::GAS_GIANT_MAX_STORMS);
    outStorms.Reserve(wanted);

    // One private stream: each storm's draws depend only on the storms before it, so a shorter count is a prefix
    FRandomStream stream(seed);
    for (int32 stormIndex = 0; stormIndex < wanted; ++stormIndex)
    {
        for (int32 attempt = 0; attempt < SOL::GAS_GIANT_STORM_PLACEMENT_ATTEMPTS; ++attempt)
        {
            FStorm candidate;
            candidate.LatitudeRad = FMath::Clamp(maxLat * (2.0 * stream.GetFraction() - 1.0), -maxLat, maxLat);
            candidate.LongitudeRad = GasGiantWrapAngleRad(GAS_GIANT_TWO_PI * stream.GetFraction());
            candidate.RadiusRad = FMath::Clamp(minRadius + (maxRadius - minRadius) * stream.GetFraction(), minRadius,
                maxRadius);

            // Reject a candidate that overlaps any storm already placed
            bool bFits = true;
            for (const FStorm& placed : outStorms)
            {
                if (GasGiantGreatCircleRad(candidate.LatitudeRad, candidate.LongitudeRad, placed.LatitudeRad,
                    placed.LongitudeRad) < candidate.RadiusRad + placed.RadiusRad)
                {
                    bFits = false;
                    break;
                }
            }
            if (bFits)
            {
                outStorms.Add(candidate);
                break;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a storm's body-frame longitude at a sim time: its authored longitude carried by its band's drift
double SOLGasGiant::StormLongitudeRad(const FStorm& storm, const double simSeconds, const double equatorDriftRadPerS,
    const double polarDriftRadPerS, const double speedFactor)
{
    const double phase = BandPhaseTurns(simSeconds, storm.LatitudeRad, equatorDriftRadPerS, polarDriftRadPerS,
        speedFactor);
    return GasGiantWrapAngleRad(GasGiantWrapAngleRad(storm.LongitudeRad) + GAS_GIANT_TWO_PI * phase);
}

//////////////////////////////////////////////////////////////////////////
// Returns the near layer's blend from the camera distance to the planet centre, in planet radii
double SOLGasGiant::NearLayerBlend(const double distanceToCenterM, const double planetRadiusM, const double fadeInRadii,
    const double fadeOutRadii)
{
    if (!std::isfinite(distanceToCenterM) || !std::isfinite(planetRadiusM) || planetRadiusM <= 0.0)
    {
        return 0.0;
    }
    const double x = distanceToCenterM / planetRadiusM;
    if (x <= fadeInRadii)
    {
        return 1.0;
    }
    if (!(fadeOutRadii > fadeInRadii) || x >= fadeOutRadii)
    {
        return 0.0;
    }
    const double t = (x - fadeInRadii) / (fadeOutRadii - fadeInRadii);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

//////////////////////////////////////////////////////////////////////////
// Returns true if a blend shows the near layer
bool SOLGasGiant::IsNearLayerVisible(const double blend)
{
    return std::isfinite(blend) && blend > 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Advances the near layer's residency by one real frame and returns the resource action to take
SOLGasGiant::ENearLayerAction SOLGasGiant::StepNearLayer(FNearLayerState& state, const double blend,
    const double realDeltaSeconds, const double releaseDelaySeconds)
{
    if (IsNearLayerVisible(blend))
    {
        state.HiddenSeconds = 0.0;
        if (!state.bActive)
        {
            state.bActive = true;
            return ENearLayerAction::Activate;
        }
        return ENearLayerAction::None;
    }
    if (!state.bActive)
    {
        state.HiddenSeconds = 0.0;
        return ENearLayerAction::None;
    }

    // Hidden and active: count real time toward the release (bad deltas count as 0)
    const double dt = (std::isfinite(realDeltaSeconds) && realDeltaSeconds > 0.0) ? realDeltaSeconds : 0.0;
    state.HiddenSeconds += dt;
    if (!(releaseDelaySeconds > 0.0) || state.HiddenSeconds >= releaseDelaySeconds)
    {
        state.bActive = false;
        state.HiddenSeconds = 0.0;
        return ENearLayerAction::Release;
    }
    return ENearLayerAction::None;
}
