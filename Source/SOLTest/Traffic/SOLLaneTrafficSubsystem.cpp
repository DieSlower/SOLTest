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
// Exists in game and PIE worlds only
bool USOLLaneTrafficSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}
