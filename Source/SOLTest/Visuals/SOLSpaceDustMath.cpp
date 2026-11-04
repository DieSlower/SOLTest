/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLSpaceDustMath.h"

#include <cmath>
#include <limits>

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the smoothstep of x between e0 and e1; a degenerate range (e1 <= e0) is a step at e0 (0 at or below)
    double SmoothStep(const double e0, const double e1, const double x)
    {
        if (e1 <= e0)
        {
            return x > e0 ? 1.0 : 0.0;
        }
        const double t = FMath::Clamp((x - e0) / (e1 - e0), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether all three components of a vector are finite
    bool IsFiniteVector(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one offset component wrapped into [0, boxCm); non-finite input gives 0
    double WrapComponent(const double value, const double boxCm)
    {
        if (!FMath::IsFinite(value))
        {
            return 0.0;
        }
        double r = std::fmod(value, boxCm);
        if (r < 0.0)
        {
            r += boxCm;
        }
        // A tiny negative value can round r + box up to exactly box
        return (r >= boxCm || r < 0.0) ? 0.0 : r + 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the particle count for an effects quality level (0 Low, 1 Medium, 2 and above High)
int32 SOLSpaceDust::ParticleCountForQuality(const int32 effectsQuality)
{
    if (effectsQuality <= 0)
    {
        return SOL::SpaceDust::PARTICLE_COUNT_LOW;
    }
    return effectsQuality == 1 ? SOL::SpaceDust::PARTICLE_COUNT_MEDIUM : SOL::SpaceDust::PARTICLE_COUNT;
}

//////////////////////////////////////////////////////////////////////////
// Returns the apparent speed (m/s): 0 in the dead zone, else Amax (1 - exp(-(v - v0) / Amax)), strictly below Amax
double SOLSpaceDust::ApparentSpeedMps(const double relativeSpeedMps, const FSOLSpaceDustParams& params)
{
    if (!FMath::IsFinite(relativeSpeedMps) || relativeSpeedMps <= params.DeadZoneMps || params.ApparentMaxMps <= 0.0)
    {
        return 0.0;
    }
    const double aMax = params.ApparentMaxMps;
    const double apparent = aMax * -std::expm1(-(relativeSpeedMps - params.DeadZoneMps) / aMax);
    // Rounding can reach Amax exactly at relativistic speeds; keep it strictly below
    return apparent >= aMax ? std::nextafter(aMax, 0.0) : apparent;
}

//////////////////////////////////////////////////////////////////////////
// Returns the dust opacity: smoothstep(v0, v1, v), 0 for non-finite input
double SOLSpaceDust::Opacity(const double relativeSpeedMps, const FSOLSpaceDustParams& params)
{
    if (!FMath::IsFinite(relativeSpeedMps))
    {
        return 0.0;
    }
    return SmoothStep(params.DeadZoneMps, params.FullOpacityMps, relativeSpeedMps);
}

//////////////////////////////////////////////////////////////////////////
// Returns the dust's apparent velocity (cm/s) along the relative velocity, its length below Amax x 100
FVector3d SOLSpaceDust::ApparentVelocityCmPerS(const FVector3d& relativeVelocityMps, const FSOLSpaceDustParams& params)
{
    if (!IsFiniteVector(relativeVelocityMps))
    {
        return FVector3d::ZeroVector;
    }
    // Scale by the largest component first so squaring cannot overflow
    const double maxComponent = relativeVelocityMps.GetAbsMax();
    if (maxComponent <= 0.0)
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d unscaled = relativeVelocityMps / maxComponent;
    const double unscaledLength = unscaled.Size();
    const double speed = maxComponent * unscaledLength;
    const double apparent = ApparentSpeedMps(speed, params);
    if (apparent <= 0.0)
    {
        return FVector3d::ZeroVector;
    }
    FVector3d result = unscaled / unscaledLength * (apparent * 100.0);

    // The normalize and scale roundings can land on the cap; pull the result just under it
    const double cap = params.ApparentMaxMps * 100.0;
    const double size = result.Size();
    if (size >= cap)
    {
        result *= cap * (1.0 - 1.0e-12) / size;
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the streak length (cm): clamp(P + max(0, A - As) x 100 x Ts, P, Smax); P for non-finite or non-positive input
double SOLSpaceDust::StreakLengthCm(const double apparentSpeedMps, const FSOLSpaceDustParams& params)
{
    if (!FMath::IsFinite(apparentSpeedMps) || apparentSpeedMps <= 0.0)
    {
        return params.PointSizeCm;
    }
    const double length = params.PointSizeCm
        + FMath::Max(0.0, apparentSpeedMps - params.StreakStartMps) * 100.0 * params.StreakTimeS;
    return FMath::Clamp(length, params.PointSizeCm, FMath::Max(params.StreakMaxCm, params.PointSizeCm));
}

//////////////////////////////////////////////////////////////////////////
// Returns the streak alpha: max(Amin, sqrt(P / S)), 1 for S <= P or non-finite
double SOLSpaceDust::StreakAlpha(const double streakLengthCm, const FSOLSpaceDustParams& params)
{
    if (!FMath::IsFinite(streakLengthCm) || streakLengthCm <= params.PointSizeCm)
    {
        return 1.0;
    }
    return FMath::Min(1.0, FMath::Max(params.StreakMinAlpha, FMath::Sqrt(params.PointSizeCm / streakLengthCm)));
}

//////////////////////////////////////////////////////////////////////////
// Returns current moved toward target by 1 - exp(-dt / tau); invalid input follows the SDD 43 section 7 rules
double SOLSpaceDust::SmoothScalar(const double current, const double target, const double dtS, const double tauS)
{
    if (!FMath::IsFinite(target) || !FMath::IsFinite(dtS) || dtS <= 0.0)
    {
        return current;
    }
    if (!(tauS > 0.0))
    {
        return target;
    }
    return current + (target - current) * -std::expm1(-dtS / tauS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the vector current moved toward target per component; a non-finite target component leaves it unchanged
FVector3d SOLSpaceDust::SmoothVector(const FVector3d& current, const FVector3d& target, const double dtS,
    const double tauS)
{
    if (!IsFiniteVector(target))
    {
        return current;
    }
    return FVector3d(SmoothScalar(current.X, target.X, dtS, tauS), SmoothScalar(current.Y, target.Y, dtS, tauS),
        SmoothScalar(current.Z, target.Z, dtS, tauS));
}

//////////////////////////////////////////////////////////////////////////
// Returns the offset advanced by velocity x dt and wrapped into [0, boxCm); invalid dt or velocity components only wrap
FVector3d SOLSpaceDust::AdvanceOffsetCm(const FVector3d& offsetCm, const FVector3d& velocityCmPerS, const double dtS,
    const double boxCm)
{
    if (!FMath::IsFinite(dtS) || dtS <= 0.0)
    {
        return WrapOffsetCm(offsetCm, boxCm);
    }
    const FVector3d moved(
        FMath::IsFinite(velocityCmPerS.X) ? offsetCm.X + velocityCmPerS.X * dtS : offsetCm.X,
        FMath::IsFinite(velocityCmPerS.Y) ? offsetCm.Y + velocityCmPerS.Y * dtS : offsetCm.Y,
        FMath::IsFinite(velocityCmPerS.Z) ? offsetCm.Z + velocityCmPerS.Z * dtS : offsetCm.Z);
    return WrapOffsetCm(moved, boxCm);
}

//////////////////////////////////////////////////////////////////////////
// Returns the offset wrapped into [0, boxCm) per component; a non-finite component gives 0, a bad box the zero vector
FVector3d SOLSpaceDust::WrapOffsetCm(const FVector3d& offsetCm, const double boxCm)
{
    if (!FMath::IsFinite(boxCm) || boxCm <= 0.0)
    {
        return FVector3d::ZeroVector;
    }
    return FVector3d(WrapComponent(offsetCm.X, boxCm), WrapComponent(offsetCm.Y, boxCm),
        WrapComponent(offsetCm.Z, boxCm));
}

//////////////////////////////////////////////////////////////////////////
// Returns the shader's frac((anchor - offset) / box) x box - box / 2 per component, kept inside [-box / 2, box / 2)
FVector3f SOLSpaceDust::WrappedLocalPositionCm(const FVector3f& anchorCm, const FVector3f& offsetCm, const float boxCm)
{
    if (!(boxCm > 0.0f) || !FMath::IsFinite(boxCm))
    {
        return FVector3f::ZeroVector;
    }
    const double box = boxCm;
    const float half = 0.5f * boxCm;
    const float belowHalf = std::nextafter(half, -std::numeric_limits<float>::infinity());

    // Wrap one axis in double, then make sure the float cast did not round up onto +box / 2
    auto wrapAxis = [box, half, belowHalf](const float anchor, const float offset)
    {
        const double x = (static_cast<double>(anchor) - static_cast<double>(offset)) / box;
        if (!FMath::IsFinite(x))
        {
            return 0.0f;
        }
        const double frac = x - std::floor(x);
        const float r = static_cast<float>(frac * box - 0.5 * box);
        return FMath::Clamp(r, -half, belowHalf);
    };
    return FVector3f(wrapAxis(anchorCm.X, offsetCm.X), wrapAxis(anchorCm.Y, offsetCm.Y),
        wrapAxis(anchorCm.Z, offsetCm.Z));
}

//////////////////////////////////////////////////////////////////////////
// Returns the particle fade: (1 - smoothstep(f R, R, |local|)) x smoothstep(nearStart, nearEnd, |local - camera|)
float SOLSpaceDust::DustFade(const FVector3f& localCm, const FVector3f& cameraLocalCm,
    const FSOLSpaceDustParams& params)
{
    const double radius = 0.5 * params.BoxSizeCm;
    const double radial = 1.0 - SmoothStep(params.RadialFadeStartFraction * radius, radius,
        static_cast<double>(localCm.Size()));
    const double nearFade = SmoothStep(params.NearFadeStartCm, params.NearFadeEndCm,
        static_cast<double>((localCm - cameraLocalCm).Size()));
    return static_cast<float>(FMath::Clamp(radial * nearFade, 0.0, 1.0));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the effect runs: ship, setting and cvar on, finite warp within MaxTimeWarp, no jump warp, map, pause
bool SOLSpaceDust::ShouldRun(const FSOLSpaceDustGate& gate, const FSOLSpaceDustParams& params)
{
    return gate.bHasShip && gate.bSettingEnabled && gate.bCvarEnabled && FMath::IsFinite(gate.TimeWarpFactor)
        && gate.TimeWarpFactor <= params.MaxTimeWarp && !gate.bJumpWarping && !gate.bMapOpen && !gate.bPaused;
}
