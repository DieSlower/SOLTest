/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLSceneExposure.h"

//////////////////////////////////////////////////////////////////////////
// Returns the solid angle (sr) of a rectilinear view with these half-angle tangents: 4 asin(sin a sin b)
double SOLSceneExposure::ViewSolidAngleSr(const double tanHalfFovX, const double tanHalfFovY)
{
    if (tanHalfFovX <= 0.0 || tanHalfFovY <= 0.0)
    {
        return 0.0;
    }
    const double sinA = tanHalfFovX / FMath::Sqrt(1.0 + tanHalfFovX * tanHalfFovX);
    const double sinB = tanHalfFovY / FMath::Sqrt(1.0 + tanHalfFovY * tanHalfFovY);
    return 4.0 * FMath::Asin(sinA * sinB);
}

//////////////////////////////////////////////////////////////////////////
// Returns the solid angle (sr) of a disc of this angular radius (clamped to [0, PI / 2]): 2 PI (1 - cos r)
double SOLSceneExposure::DiscSolidAngleSr(const double angularRadiusRad)
{
    const double radius = FMath::Clamp(angularRadiusRad, 0.0, 0.5 * UE_DOUBLE_PI);
    return 2.0 * UE_DOUBLE_PI * (1.0 - FMath::Cos(radius));
}

//////////////////////////////////////////////////////////////////////////
// Returns the share of the view covered by a disc offAxisRad from the view axis (view treated as an equal-sr cone)
double SOLSceneExposure::DiscViewCoverage(const double angularRadiusRad, const double offAxisRad,
    const double viewSolidAngleSr)
{
    if (angularRadiusRad <= 0.0 || viewSolidAngleSr <= 0.0)
    {
        return 0.0;
    }

    // Half angle of the cone with the view's solid angle, and the share of the disc's diameter inside it
    const double viewConeRad = FMath::Acos(FMath::Clamp(1.0 - viewSolidAngleSr / (2.0 * UE_DOUBLE_PI), -1.0, 1.0));
    const double inside = FMath::Clamp((viewConeRad + angularRadiusRad - FMath::Abs(offAxisRad))
        / (2.0 * angularRadiusRad), 0.0, 1.0);
    return FMath::Min(1.0, DiscSolidAngleSr(angularRadiusRad) * inside / viewSolidAngleSr);
}

//////////////////////////////////////////////////////////////////////////
// Returns the mean of saturate(N . L) over a Lambertian sphere's visible disc: (2 / 3) (sin a + (PI - a) cos a) / PI
double SOLSceneExposure::LambertDiscMeanLight(const double phaseAngleRad)
{
    const double phase = FMath::Clamp(phaseAngleRad, 0.0, UE_DOUBLE_PI);
    return 2.0 / 3.0 * (FMath::Sin(phase) + (UE_DOUBLE_PI - phase) * FMath::Cos(phase)) / UE_DOUBLE_PI;
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's mean radiance at this phase: emission plus albedo * E / PI * the Lambert disc mean
double SOLSceneExposure::BodyMeanRadiance(const double emissiveLuminance, const double albedoLuminance,
    const double sunIlluminanceLux, const double phaseAngleRad)
{
    return emissiveLuminance
        + albedoLuminance * sunIlluminanceLux / UE_DOUBLE_PI * LambertDiscMeanLight(phaseAngleRad);
}

//////////////////////////////////////////////////////////////////////////
// Returns the target EV100: minEv100 + log2(mean / key) above the key, clamped to [minEv100, maxEv100]
double SOLSceneExposure::TargetEv100(const double meanRadiance, const double keyRadiance, const double minEv100,
    const double maxEv100)
{
    const double upper = FMath::Max(minEv100, maxEv100);
    if (keyRadiance <= 0.0 || meanRadiance <= keyRadiance)
    {
        return minEv100;
    }
    return FMath::Clamp(minEv100 + FMath::Log2(meanRadiance / keyRadiance), minEv100, upper);
}

//////////////////////////////////////////////////////////////////////////
// Returns the exposure after dt of exponential approach to the target: target + (current - target) exp(-dt / tau)
double SOLSceneExposure::SmoothEv100(const double currentEv100, const double targetEv100, const double deltaSeconds,
    const double timeConstantSeconds)
{
    if (timeConstantSeconds <= 0.0)
    {
        return targetEv100;
    }
    const double keep = FMath::Exp(-FMath::Max(deltaSeconds, 0.0) / timeConstantSeconds);
    return targetEv100 + (currentEv100 - targetEv100) * keep;
}

//////////////////////////////////////////////////////////////////////////
// Returns the manual exposure bias (EV) for this EV100 relative to the dark-sky exposure: minEv100 - ev100
double SOLSceneExposure::ExposureBias(const double ev100, const double minEv100)
{
    return minEv100 - ev100;
}
