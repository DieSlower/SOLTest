/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLTargetDrop.h"

//////////////////////////////////////////////////////////////////////////
// Returns the position ahead of the ship
FVector3d SOLTargetDrop::DropPosition(const FVector3d& shipPosition, const FVector3d& shipForward, double distanceM)
{
    const bool bUsable = FMath::IsFinite(shipForward.X) && FMath::IsFinite(shipForward.Y) && FMath::IsFinite(shipForward.Z)
        && shipForward.SizeSquared() > 1.0e-24;
    return shipPosition + (bUsable ? shipForward.GetSafeNormal() : FVector3d(1.0, 0.0, 0.0)) * distanceM;
}

//////////////////////////////////////////////////////////////////////////
// Returns the oldest target's index once the cap is reached
int32 SOLTargetDrop::EvictionIndex(const TArray<double>& spawnTimes, int32 maxTargets)
{
    if (maxTargets <= 0 || spawnTimes.Num() < maxTargets)
    {
        return INDEX_NONE;
    }
    int32 oldest = 0;
    for (int32 i = 1; i < spawnTimes.Num(); ++i)
    {
        if (spawnTimes[i] < spawnTimes[oldest])
        {
            oldest = i;
        }
    }
    return oldest;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the drop cooldown has elapsed
bool SOLTargetDrop::ShouldAllowDrop(double secondsSinceLastDrop, double cooldownSeconds)
{
    return secondsSinceLastDrop >= cooldownSeconds;
}
