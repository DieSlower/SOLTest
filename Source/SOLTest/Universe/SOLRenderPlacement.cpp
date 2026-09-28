/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLRenderPlacement.h"

namespace SOLRender
{
    //////////////////////////////////////////////////////////////////////////
    // Places a body 1:1 within range, otherwise log-compressed in depth along the same direction with angular size kept
    FSOLRenderPlacement ComputePlacement(const FVector3d& relativeToObserverM, const double radiusM,
        const double maxRenderDistanceCm)
    {
        FSOLRenderPlacement placement;
        placement.LocationCm = relativeToObserverM * MetersToCm;
        placement.RadiusCm = radiusM * MetersToCm;

        // Beyond range: d' = Max * (1 + k * ln(d / Max)) keeps depth order; location and radius scale by d'/d
        const double distanceSqCm = placement.LocationCm.SizeSquared();
        if (distanceSqCm > maxRenderDistanceCm * maxRenderDistanceCm)
        {
            const double distanceCm = FMath::Sqrt(distanceSqCm);
            const double placedCm = maxRenderDistanceCm
                * (1.0 + FarDepthLogScale * FMath::Loge(distanceCm / maxRenderDistanceCm));
            const double scale = placedCm / distanceCm;
            placement.LocationCm *= scale;
            placement.RadiusCm *= scale;
        }
        return placement;
    }

    //////////////////////////////////////////////////////////////////////////
    // Converts right-handed ecliptic axes to Unreal's left-handed axes: (X, -Y, Z)
    FVector3d EclipticToUnreal(const FVector3d& ecliptic)
    {
        return FVector3d(ecliptic.X, -ecliptic.Y, ecliptic.Z);
    }
}
