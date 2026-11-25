/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Traffic/SOLLaneNetworkSchedule.h"
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
    int32 Slot = INDEX_NONE;        // Lane network slot (SDD 53 4.5), INDEX_NONE without one
    bool bOpenNow = true;           // Inside its slot's open window now (Q30; always true without a slot)
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

    // Updates the existing entries' end points and profiles in place (a slot column refreshed at a block move, SDD 53
    // 4.5), keeping each entry's slot and open flag; entries past the shortest list are left as they are
    void UpdateLaneEnds(TConstArrayView<int32> bodyA, TConstArrayView<int32> bodyB,
        TConstArrayView<FSOLLaneProfile> profiles);

    // Empties the lane table (no population)
    void ClearLanes();

    // Returns the lane table (empty without a population)
    TConstArrayView<FSOLLaneTrafficEntry> GetLanes() const { return mLanes; }

    // Sets up the lane network (SDD 53, part 53c) for a population; it is built on the next AdvanceNetwork
    void ConfigureNetwork(const FSOLLaneNetworkConfig& config, FSOLLaneNodePositionsFn positionsFn);

    // Forgets the lane network (no population)
    void ClearNetwork();

    // Brings the lane network to the epoch of a sim time (seconds since J2000): steps or rebuilds
    ESOLLaneNetworkAdvance AdvanceNetwork(double secondsSinceJ2000);

    // Makes the next AdvanceNetwork a time jump (a clock set)
    void MarkNetworkTimeJump();

    // Sets each lane table entry's network slot (by entry index; INDEX_NONE or past the list: no slot, always open);
    // AdvanceNetwork then refreshes each slotted entry's bOpenNow
    void SetLaneSlots(TConstArrayView<int32> slotByEntry);

    // Marks a network slot held (SDD 53 4.5: a slot whose ships still fly keeps its column after its lane leaves)
    void SetNetworkSlotHeld(int32 slot, bool bHeld);

    // Returns the lane network's schedule (the daily set, slots, windows and the route query)
    const FSOLLaneNetworkSchedule& GetNetwork() const { return mNetwork; }

    // Returns the fastest route at startS over the slot lanes open now (hop Lane = slot; SDD 53 6.3)
    FSOLLaneRoute FindNetworkRoute(int32 sourceNode, int32 targetNode, double startS,
        TConstArrayView<FSOLLaneProfile> slotProfiles, double wallSpeedMps);

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
    FSOLLaneNetworkSchedule mNetwork;       // The lane network's per-epoch state (not saved)
    bool mIsReadoutOverride = false;
};
