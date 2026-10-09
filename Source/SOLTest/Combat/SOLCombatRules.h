/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Combat/SOLDamageMath.h"

#include "CoreMinimal.h"

// Pure decision rules of USOLCombatSubsystem's frame update and of the effects and sounds that follow its events (SDD 7
// Amendment 4): the sweep-segment and teleport rule, the bolt range rule, hit resolution with its double-kill guard, the
// event queue's trim and cap, and where an event is now. Kept apart from the subsystem so they are unit-tested.
namespace SOLCombatRules
{
    // Returns whether a per-frame displacement is at most maxStepM long (false for NaN), i.e. real motion to sweep
    SOLTEST_API bool IsSweepable(const FVector3d& startM, const FVector3d& endM, double maxStepM);

    // Returns the sweep-segment start for something now at endM that was at startM last frame: startM, unless it has no
    // valid previous position or moved farther than maxStepM (a teleport), when it is tested where it is now (endM)
    SOLTEST_API FVector3d ResolveSweepStart(const FVector3d& startM, const FVector3d& endM, bool bHasPrevious,
        double maxStepM);

    // Returns whether a minor body's snapshot entry is its previous position: the snapshot was taken at the previous
    // update, the same entity sat at that index, and it has not been reassigned (moved to another ring cell) since
    SOLTEST_API bool HasPreviousPosition(bool bIsSnapshotFresh, bool bIsSameEntity, uint16 snapshotGeneration,
        uint16 generation);

    // Returns whether a bolt (its position relative to the ship) is within maxRangeM of the ship; false for NaN
    SOLTEST_API bool IsBoltInRange(const FVector3d& shipRelativePositionM, double maxRangeM);

    // What one bolt hitting one target did
    struct FTargetHitOutcome
    {
        bool bApplied = false;                 // False: the target was gone or dead, so the bolt flies on
        SOLDamageMath::FDamageState After;     // The target's state after the hit (the input state when not applied)
        bool bShieldBroken = false;            // The hit took the shield from above zero to zero
        bool bDestroyed = false;               // The hit took the health to zero
    };

    // Applies one bolt's damage to a target that can still take it (active and not dead); a target an earlier bolt
    // destroyed this frame does not, so that bolt flies on instead of scoring a second kill
    SOLTEST_API FTargetHitOutcome ResolveTargetHit(bool bIsTargetActive, const SOLDamageMath::FDamageState& state,
        double damage);

    // Returns how many events to drop from the front of the queue when an update starts: those already broadcast,
    // never more than are queued and never negative
    SOLTEST_API int32 EventsToTrim(int32 publishedCount, int32 queuedCount);

    // Returns whether one more event fits under the per-update cap
    SOLTEST_API bool MayQueueEvent(int32 queuedCount, int32 cap);

    // Returns where an event is now: its frame body's current position plus the event's offset from that body, moved by
    // its own velocity relative to the body for ageS real seconds (how targets and bolts move in their frames)
    SOLTEST_API FVector3d EventPositionNowM(const FVector3d& frameBodyPositionM, const FVector3d& offsetM,
        const FVector3d& relativeVelocityMps, double ageS);

    // Returns where a hit is at the END of the frame (universe m): the contact offset from the struck object at the hit
    // time t in [0,1] (bolt point minus object point, both on their ship-centred sweep segments), applied to the
    // object's end-of-frame position (its sweep end plus the frame-end sweep origin). Not the bolt's universe point at
    // t: the reference frame keeps moving for the rest of the frame (30 km/s for Earth), which would leave an effect
    // tens of metres off its target
    SOLTEST_API FVector3d HitPointAtFrameEndM(const FVector3d& boltStartM, const FVector3d& boltEndM,
        const FVector3d& objectStartM, const FVector3d& objectEndM, double hitTime, const FVector3d& sweepOriginEndM);

    // Returns how many bolts the renderer draws: the live count clamped to [0, renderCap]
    SOLTEST_API int32 DrawnBoltCount(int32 liveBoltCount, int32 renderCap);
}
