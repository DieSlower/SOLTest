/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic rules for dropping targets (SDD 7).
namespace SOLTargetDrop
{
    // Returns the drop position distanceM ahead of the ship along its forward direction
    SOLTEST_API FVector3d DropPosition(const FVector3d& shipPosition, const FVector3d& shipForward, double distanceM);

    // Returns the index of the oldest target to evict when the cap is reached, else INDEX_NONE
    SOLTEST_API int32 EvictionIndex(const TArray<double>& spawnTimes, int32 maxTargets);

    // Returns whether enough time has passed since the last drop
    SOLTEST_API bool ShouldAllowDrop(double secondsSinceLastDrop, double cooldownSeconds);
}
