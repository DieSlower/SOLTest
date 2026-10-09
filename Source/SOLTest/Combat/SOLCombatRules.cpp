/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatRules.h"

//////////////////////////////////////////////////////////////////////////
// Returns whether a per-frame displacement is at most maxStepM long (false for NaN)
bool SOLCombatRules::IsSweepable(const FVector3d& startM, const FVector3d& endM, const double maxStepM)
{
    // A NaN length fails the comparison, so it is never swept
    return (endM - startM).SizeSquared() <= maxStepM * maxStepM;
}

//////////////////////////////////////////////////////////////////////////
// Returns the sweep-segment start: the previous position, or the current one for no history or a teleport
FVector3d SOLCombatRules::ResolveSweepStart(const FVector3d& startM, const FVector3d& endM, const bool bHasPrevious,
    const double maxStepM)
{
    return bHasPrevious && IsSweepable(startM, endM, maxStepM) ? startM : endM;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a minor body's snapshot entry is its previous position
bool SOLCombatRules::HasPreviousPosition(const bool bIsSnapshotFresh, const bool bIsSameEntity,
    const uint16 snapshotGeneration, const uint16 generation)
{
    return bIsSnapshotFresh && bIsSameEntity && snapshotGeneration == generation;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a bolt is within maxRangeM of the ship (false for NaN)
bool SOLCombatRules::IsBoltInRange(const FVector3d& shipRelativePositionM, const double maxRangeM)
{
    return shipRelativePositionM.SizeSquared() <= maxRangeM * maxRangeM;
}

//////////////////////////////////////////////////////////////////////////
// Applies one bolt's damage to a target that can still take it (active and not dead)
SOLCombatRules::FTargetHitOutcome SOLCombatRules::ResolveTargetHit(const bool bIsTargetActive,
    const SOLDamageMath::FDamageState& state, const double damage)
{
    FTargetHitOutcome outcome;
    outcome.After = state;
    if (!bIsTargetActive || state.bDead)
    {
        return outcome;
    }
    outcome.bApplied = true;
    outcome.After = SOLDamageMath::ApplyDamage(state, damage);
    outcome.bShieldBroken = state.Shield > 0.0 && outcome.After.Shield <= 0.0;
    outcome.bDestroyed = outcome.After.bDead;
    return outcome;
}

//////////////////////////////////////////////////////////////////////////
// Returns how many already-broadcast events to drop from the front of the queue
int32 SOLCombatRules::EventsToTrim(const int32 publishedCount, const int32 queuedCount)
{
    return FMath::Clamp(publishedCount, 0, FMath::Max(queuedCount, 0));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether one more event fits under the per-update cap
bool SOLCombatRules::MayQueueEvent(const int32 queuedCount, const int32 cap)
{
    return queuedCount < cap;
}

//////////////////////////////////////////////////////////////////////////
// Returns where an event is now, from its frame body's current position
FVector3d SOLCombatRules::EventPositionNowM(const FVector3d& frameBodyPositionM, const FVector3d& offsetM,
    const FVector3d& relativeVelocityMps, const double ageS)
{
    return frameBodyPositionM + offsetM + relativeVelocityMps * FMath::Max(ageS, 0.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns where a hit is at the end of the frame: the contact offset at the hit time on the object's end position
FVector3d SOLCombatRules::HitPointAtFrameEndM(const FVector3d& boltStartM, const FVector3d& boltEndM,
    const FVector3d& objectStartM, const FVector3d& objectEndM, const double hitTime, const FVector3d& sweepOriginEndM)
{
    const double t = FMath::Clamp(hitTime, 0.0, 1.0);
    const FVector3d contactOffsetM = FMath::Lerp(boltStartM, boltEndM, t) - FMath::Lerp(objectStartM, objectEndM, t);
    return objectEndM + sweepOriginEndM + contactOffsetM;
}

//////////////////////////////////////////////////////////////////////////
// Returns how many bolts the renderer draws
int32 SOLCombatRules::DrawnBoltCount(const int32 liveBoltCount, const int32 renderCap)
{
    return FMath::Clamp(liveBoltCount, 0, FMath::Max(renderCap, 0));
}
