/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Screen-size thresholds and projection inputs for the map's per-body mesh/icon LOD (SDD 3, Appendix G)
struct FSOLBodyLodParams
{
    double IconFullBelowPx = 4.0;                 // apparent diameter at/under this: icon only, alpha = 1
    double MeshFullAbovePx = 32.0;                // apparent diameter at/over this: mesh only, alpha = 0
    double ViewportHeightPx = 720.0;              // caller's current viewport height, for the projection
    double VerticalFovRad = 1.0471975511965976;   // 60 deg, matches the map camera's FOV
};

// Per-body LOD decision: apparent size and the icon/mesh cross-fade weight
struct FSOLBodyLodResult
{
    double ApparentDiameterPx = 0.0;
    double IconAlpha = 0.0;   // 0 = mesh only, 1 = icon only, linearly interpolated between the two thresholds
};

namespace SOLMapBodyLod
{
    // Apparent diameter in pixels of a sphere of radiusM at distanceM from the camera, given the params' viewport
    // height and vertical FOV:
    // ApparentDiameterPx = (2*radiusM/distanceM) * (ViewportHeightPx / (2*tan(VerticalFovRad/2))).
    // distanceM <= 0 or radiusM <= 0 returns 0 (no valid apparent size).
    SOLTEST_API double ComputeApparentDiameterPx(double radiusM, double distanceM, const FSOLBodyLodParams& params);

    // IconAlpha = 0 at/above MeshFullAbovePx, 1 at/below IconFullBelowPx, linearly interpolated in between
    // (clamped to [0,1] outside that range, e.g. for a degenerate ApparentDiameterPx of 0 -> alpha = 1).
    SOLTEST_API FSOLBodyLodResult Evaluate(double radiusM, double distanceM, const FSOLBodyLodParams& params);
}
