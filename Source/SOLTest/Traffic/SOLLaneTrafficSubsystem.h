/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Traffic/SOLLaneTrafficMath.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLLaneTrafficSubsystem.generated.h"

// One lane of the NPC population's lane table (SDD 50 section 4.10): its end bodies and its frozen profile
struct FSOLLaneTrafficEntry
{
    int32 BodyA = INDEX_NONE;       // Registry index of body A (INDEX_NONE if unknown)
    int32 BodyB = INDEX_NONE;       // Registry index of body B
    FSOLLaneProfile Profile;        // Frozen at spawn (wall radius, nominal leg, end arcs, U-turn)
};

/**
 * The NPC population's lane table (SDD 50 section 4.10, part 50c): written by the NPC subsystem at spawn and cleared at
 * despawn, read by the HUD's merge readout (and later the AUTO lane rules) without a UI -> Npc or Ship -> Npc edge. The
 * entry index is the lane (group) index of the NPC population and of the HUD's tunnel labels. No per-frame data.
 */
UCLASS()
class SOLTEST_API USOLLaneTrafficSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Replaces the lane table (one entry per lane, in lane order; the shortest list sets the count)
    void SetLanes(TConstArrayView<int32> bodyA, TConstArrayView<int32> bodyB,
        TConstArrayView<FSOLLaneProfile> profiles);

    // Empties the lane table (no population)
    void ClearLanes();

    // Returns the lane table (empty without a population)
    TConstArrayView<FSOLLaneTrafficEntry> GetLanes() const { return mLanes; }

    // Verification only (-SOLSmokeLaneMerge): shows the merge readout whatever the Lanes overlay row says, so no player
    // setting is read or written
    void SetReadoutVerificationOverride(bool bOn) { mIsReadoutOverride = bOn; }

    // Returns whether the readout's verification override is on
    bool IsReadoutVerificationOverride() const { return mIsReadoutOverride; }

protected:

    // Exists in game and PIE worlds only
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    TArray<FSOLLaneTrafficEntry> mLanes;    // Per lane, in the population's lane order
    bool mIsReadoutOverride = false;
};
