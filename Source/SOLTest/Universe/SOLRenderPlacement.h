/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

#include "SOLConstants.h"

// Render location and radius relative to the observer (camera), ecliptic axes
struct SOLTEST_API FSOLRenderPlacement
{
    FVector3d LocationCm = FVector3d::ZeroVector;
    double RadiusCm = 0.0;
};

namespace SOLRender
{
    constexpr double MetersToCm = SOL::METERS_TO_CM;

    // Depth compression beyond range: d' = Max * (1 + FarDepthLogScale * ln(d / Max))
    constexpr double FarDepthLogScale = 0.1;

    // Places a body 1:1 within range, otherwise log-compressed in depth along the same direction with angular size kept
    SOLTEST_API FSOLRenderPlacement ComputePlacement(const FVector3d& relativeToObserverM, double radiusM,
        double maxRenderDistanceCm);

    // Converts right-handed ecliptic axes to Unreal's left-handed axes: (X, -Y, Z)
    SOLTEST_API FVector3d EclipticToUnreal(const FVector3d& ecliptic);
}
