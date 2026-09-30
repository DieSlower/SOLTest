/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLViewpointResolve.h"

//////////////////////////////////////////////////////////////////////////
// Picks the override or the observer as this frame's viewpoint, and forces a snap when the override toggled
FSOLViewpointResolveResult SOLViewpoint::Resolve(const bool bWasOverrideActiveLastFrame,
    const bool bIsOverrideActiveThisFrame, const FVector3d& OverridePositionM, const FVector3d& ObserverPositionM)
{
    FSOLViewpointResolveResult result;
    result.EffectivePositionM = bIsOverrideActiveThisFrame ? OverridePositionM : ObserverPositionM;
    result.bForceSnap = bWasOverrideActiveLastFrame != bIsOverrideActiveThisFrame;
    return result;
}
