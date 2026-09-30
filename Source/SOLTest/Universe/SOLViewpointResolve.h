/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Per-frame render viewpoint decision (SDD 3, Appendix F): which universe position rendering uses, and whether the
// render origin must snap to it this frame
struct FSOLViewpointResolveResult
{
    FVector3d EffectivePositionM = FVector3d::ZeroVector;   // universe position rendering is centered on this frame
    bool bForceSnap = false;                                 // true when the viewpoint source changed this frame
};

namespace SOLViewpoint
{
    // EffectivePositionM = OverridePositionM if bIsOverrideActiveThisFrame, else ObserverPositionM. bForceSnap is true
    // exactly when the override's on/off state changed since last frame (bWasOverrideActiveLastFrame !=
    // bIsOverrideActiveThisFrame) — a still-active override moving frame to frame does NOT force a snap; that case
    // is left to FSOLRenderOrigin's own drift-distance snap rule, unchanged.
    SOLTEST_API FSOLViewpointResolveResult Resolve(bool bWasOverrideActiveLastFrame, bool bIsOverrideActiveThisFrame,
        const FVector3d& OverridePositionM, const FVector3d& ObserverPositionM);
}
