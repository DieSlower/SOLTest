/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcHullInstances.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Returns bounds covering the whole render space, without walking the instances
FBoxSphereBounds USOLNpcHullInstances::CalcBounds(const FTransform& /*localToWorld*/) const
{
    const FVector extent(SOL::NPC_RENDER_SPACE_BOUNDS_CM);
    return FBoxSphereBounds(FVector::ZeroVector, extent, extent.Size());
}
