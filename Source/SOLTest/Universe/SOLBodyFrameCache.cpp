/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLBodyFrameCache.h"

#include "Universe/SOLBodyRegistry.h"
#include "Universe/SOLRenderPlacement.h"

//////////////////////////////////////////////////////////////////////////
// Sizes the arrays once for the registry's bodies and copies the constant radii and GMs
void FSOLBodyFrameCache::Init(const FSOLBodyRegistry& registry)
{
    const int32 count = registry.Num();
    PositionsM.SetNumZeroed(count);
    VelocitiesMps.SetNumZeroed(count);
    RadiiM.SetNumUninitialized(count);
    GMs.SetNumUninitialized(count);
    for (int32 index = 0; index < count; ++index)
    {
        RadiiM[index] = registry.GetRadiusM(index);
        GMs[index] = registry.GetGM(index);
    }
    Refresh(registry);
}

//////////////////////////////////////////////////////////////////////////
// Copies the registry's current positions and velocities into the Unreal-handed frame; allocates nothing
void FSOLBodyFrameCache::Refresh(const FSOLBodyRegistry& registry)
{
    const TConstArrayView<FVector3d> positions = registry.GetPositionsM();
    const TConstArrayView<FVector3d> velocities = registry.GetVelocitiesMps();
    const int32 count = FMath::Min(PositionsM.Num(), positions.Num());
    for (int32 index = 0; index < count; ++index)
    {
        PositionsM[index] = SOLRender::EclipticToUnreal(positions[index]);
        VelocitiesMps[index] = SOLRender::EclipticToUnreal(velocities[index]);
    }
}
