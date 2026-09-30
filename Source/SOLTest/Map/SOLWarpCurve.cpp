/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLWarpCurve.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Normalized warp progress u = clamp(elapsedS / durationS, 0, 1); a non-positive duration counts as complete
    double ComputeProgress(const double elapsedS, const double durationS)
    {
        if (durationS <= 0.0)
        {
            return 1.0;
        }
        return FMath::Clamp(elapsedS / durationS, 0.0, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // The shared sin(PI * u) pulse: 0 at both ends, 1 at the midpoint
    double ComputePulse(const double elapsedS, const double durationS)
    {
        return FMath::Sin(UE_DOUBLE_PI * ComputeProgress(elapsedS, durationS));
    }
}

namespace SOLWarpCurve
{
    //////////////////////////////////////////////////////////////////////////
    // Camera FOV at elapsedS: baseFovDeg + (peakFovDeg - baseFovDeg) * sin(PI * u)
    double ComputeFovDeg(const double elapsedS, const double durationS, const double baseFovDeg,
        const double peakFovDeg)
    {
        return baseFovDeg + (peakFovDeg - baseFovDeg) * ComputePulse(elapsedS, durationS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Radial-streak intensity at elapsedS: peakIntensity * sin(PI * u), clamped to >= 0
    double ComputeStreakIntensity(const double elapsedS, const double durationS, const double peakIntensity)
    {
        return FMath::Max(0.0, peakIntensity * ComputePulse(elapsedS, durationS));
    }
}
