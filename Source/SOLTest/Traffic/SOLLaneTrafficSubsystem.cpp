/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneTrafficSubsystem.h"
#include "Traffic/SOLLaneNetworkMath.h"

//////////////////////////////////////////////////////////////////////////
// Replaces the lane table from per-lane arrays (the shortest list sets the lane count)
void USOLLaneTrafficSubsystem::SetLanes(const TConstArrayView<int32> bodyA, const TConstArrayView<int32> bodyB,
    const TConstArrayView<FSOLLaneProfile> profiles)
{
    const int32 count = FMath::Min3(bodyA.Num(), bodyB.Num(), profiles.Num());
    mLanes.Reset(count);
    for (int32 lane = 0; lane < count; ++lane)
    {
        FSOLLaneTrafficEntry& entry = mLanes.AddDefaulted_GetRef();
        entry.BodyA = bodyA[lane];
        entry.BodyB = bodyB[lane];
        entry.Profile = profiles[lane];
    }
}

//////////////////////////////////////////////////////////////////////////
// Empties the lane table
void USOLLaneTrafficSubsystem::ClearLanes()
{
    mLanes.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Sets up the lane network for a population
void USOLLaneTrafficSubsystem::ConfigureNetwork(const FSOLLaneNetworkConfig& config,
    FSOLLaneNodePositionsFn positionsFn)
{
    mNetwork.Configure(config, MoveTemp(positionsFn));
}

//////////////////////////////////////////////////////////////////////////
// Forgets the lane network
void USOLLaneTrafficSubsystem::ClearNetwork()
{
    mNetwork.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Brings the lane network to the epoch of a sim time
ESOLLaneNetworkAdvance USOLLaneTrafficSubsystem::AdvanceNetwork(const double secondsSinceJ2000)
{
    const ESOLLaneNetworkAdvance advance = mNetwork.Advance(secondsSinceJ2000);

    // Each slotted entry's open flag at this time (Q30): open while the time is inside its slot's window
    for (FSOLLaneTrafficEntry& entry : mLanes)
    {
        if (entry.Slot == INDEX_NONE)
        {
            continue;
        }
        double openStartS = 0.0;
        double closeS = 0.0;
        entry.bOpenNow = mNetwork.GetSlotWindow(entry.Slot, openStartS, closeS)
            && SOLLaneNetwork::LaneOpenAt(secondsSinceJ2000, openStartS, closeS);
    }
    return advance;
}

//////////////////////////////////////////////////////////////////////////
// Sets each lane table entry's network slot (INDEX_NONE: no slot, always open)
void USOLLaneTrafficSubsystem::SetLaneSlots(const TConstArrayView<int32> slotByEntry)
{
    for (int32 lane = 0; lane < mLanes.Num(); ++lane)
    {
        mLanes[lane].Slot = slotByEntry.IsValidIndex(lane) ? slotByEntry[lane] : INDEX_NONE;
        mLanes[lane].bOpenNow = true;
    }
}

//////////////////////////////////////////////////////////////////////////
// Marks a network slot held (its lane keeps the slot after leaving the set)
void USOLLaneTrafficSubsystem::SetNetworkSlotHeld(const int32 slot, const bool bHeld)
{
    mNetwork.SetSlotHeld(slot, bHeld);
}

//////////////////////////////////////////////////////////////////////////
// Makes the next advance a time jump
void USOLLaneTrafficSubsystem::MarkNetworkTimeJump()
{
    mNetwork.MarkTimeJump();
}

//////////////////////////////////////////////////////////////////////////
// Returns the fastest route over the slot lanes open now, with the routing transfer penalty
FSOLLaneRoute USOLLaneTrafficSubsystem::FindNetworkRoute(const int32 sourceNode, const int32 targetNode,
    const double startS, const TConstArrayView<FSOLLaneProfile> slotProfiles, const double wallSpeedMps)
{
    return mNetwork.FindRoute(sourceNode, targetNode, startS, slotProfiles, wallSpeedMps,
        SOL::NPC_LANE_TRANSFER_PENALTY_S);
}

//////////////////////////////////////////////////////////////////////////
// Exists in game and PIE worlds only
bool USOLLaneTrafficSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}
