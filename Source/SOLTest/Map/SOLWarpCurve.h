/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Warp-effect curves for the jump sequence (SDD 3 Appendix I): a sin(PI * u) pulse over the warp duration
namespace SOLWarpCurve
{
    // Normalized progress u = clamp(ElapsedS / DurationS, 0, 1) (DurationS <= 0 treated as already complete, u = 1).
    // Both curves use sin(PI * u): 0 at u=0 and u=1, peaking at u=0.5. FOV: BaseFovDeg + (PeakFovDeg - BaseFovDeg) *
    // sin(PI*u). Streak: PeakIntensity * sin(PI*u), clamped to >= 0 (sin(PI*u) is already >= 0 for u in [0,1]).
    SOLTEST_API double ComputeFovDeg(double elapsedS, double durationS, double baseFovDeg, double peakFovDeg);

    // Radial-streak post-process intensity at elapsedS: PeakIntensity * sin(PI * u), clamped to >= 0
    SOLTEST_API double ComputeStreakIntensity(double elapsedS, double durationS, double peakIntensity);
}
