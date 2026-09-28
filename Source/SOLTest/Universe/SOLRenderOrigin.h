/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

#include "SOLConstants.h"
#include "Universe/SOLRenderPlacement.h"

// Floating render origin: maps double-precision universe meters to Unreal-space centimeters around the observer
struct SOLTEST_API FSOLRenderOrigin
{
    static constexpr double SnapDistanceM = SOL::RENDER_REBASE_DISTANCE_M;     // 10 km

    FVector3d OriginM = FVector3d::ZeroVector;        // universe point drawn at Unreal (0,0,0)

    // Sets the origin to the observer's universe position
    void Reset(const FVector3d& observerM);

    // Snaps the origin to the observer if forced or once it drifted SnapDistanceM or more; returns true if it moved
    bool Update(const FVector3d& observerM, bool bForceSnap);

    // Converts a universe position to Unreal-space cm: EclipticToUnreal(UniverseM - OriginM) * MetersToCm
    FVector3d UniverseToRenderCm(const FVector3d& universeM) const;

    // Converts an Unreal-space cm position back to universe meters (exact inverse of UniverseToRenderCm)
    FVector3d RenderCmToUniverseM(const FVector3d& renderCm) const;

    // Returns a body's Unreal-space placement: ComputePlacement from the observer, offset by its render location
    FSOLRenderPlacement BodyPlacement(const FVector3d& bodyM, const FVector3d& observerM, double radiusM,
        double maxRenderDistanceCm) const;
};
