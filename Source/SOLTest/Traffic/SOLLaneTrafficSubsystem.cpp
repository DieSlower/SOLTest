/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Traffic/SOLLaneTrafficSubsystem.h"

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
    return mNetwork.Advance(secondsSinceJ2000);
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
