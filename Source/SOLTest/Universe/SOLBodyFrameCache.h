/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

class FSOLBodyRegistry;

// Body data in the ship's Unreal-handed universe frame (EclipticToUnreal applied), as flat arrays for hot loops
struct SOLTEST_API FSOLBodyFrameCache
{
    TArray<FVector3d> PositionsM;       // Unreal-handed universe positions, indexed like the registry
    TArray<FVector3d> VelocitiesMps;    // Unreal-handed universe velocities
    TArray<double> RadiiM;              // Mean radii (frame-independent)
    TArray<double> GMs;                 // Gravitational parameters (frame-independent)

    // Sizes the arrays once for the registry's bodies and copies the constant radii and GMs
    void Init(const FSOLBodyRegistry& registry);

    // Copies the registry's current positions and velocities into the Unreal-handed frame; allocates nothing
    void Refresh(const FSOLBodyRegistry& registry);

    // Returns the number of cached bodies
    int32 Num() const { return PositionsM.Num(); }
};
