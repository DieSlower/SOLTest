/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// A targetable object's snapshot for selection (Unreal-handed universe frame, meters, m/s)
struct FSOLTargetInfo
{
    FName Name;
    FVector3d PositionM = FVector3d::ZeroVector;
    FVector3d VelocityMps = FVector3d::ZeroVector;
    double RadiusM = 0.0;
};

namespace SOLTargeting
{
    constexpr double DefaultConeHalfAngleRad = 0.17453292519943295;    // 10 deg

    // Returns the candidate with the smallest angular offset within the cone (ties: nearer), or INDEX_NONE
    SOLTEST_API int32 PickUnderReticle(TConstArrayView<FSOLTargetInfo> candidates, const FVector3d& shipPositionM,
        const FVector3d& forwardDir, double coneHalfAngleRad);

    // Steps through the candidates ordered by distance (nearest first, stable), wrapping; returns an index or INDEX_NONE
    SOLTEST_API int32 Cycle(TConstArrayView<FSOLTargetInfo> candidates, const FVector3d& shipPositionM,
        int32 currentIndex, int32 direction);

    // Returns the locked target's velocity when the lock is active and the target is set, else the anchor velocity
    SOLTEST_API FVector3d ResolveReferenceVelocity(bool bLockActive, const FSOLTargetInfo* lockedTarget,
        const FVector3d& anchorVelocityMps);
}
