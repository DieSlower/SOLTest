/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLTargeting.h"

namespace
{
    // Angular offsets closer than this (radians) count as a tie, which the nearer candidate wins
    constexpr double TARGETING_OFFSET_TIE_RAD = 1.0e-9;

    //////////////////////////////////////////////////////////////////////////
    // Returns true when candidate a comes before candidate b in the cycle order (distance, then array index)
    bool TargetingCycleLess(const double distSqA, const int32 indexA, const double distSqB, const int32 indexB)
    {
        return distSqA < distSqB || (distSqA == distSqB && indexA < indexB);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the candidate with the smallest angular offset within the cone (ties: nearer), or INDEX_NONE
int32 SOLTargeting::PickUnderReticle(TConstArrayView<FSOLTargetInfo> candidates, const FVector3d& shipPositionM,
    const FVector3d& forwardDir, const double coneHalfAngleRad)
{
    int32 bestIndex = INDEX_NONE;
    double bestOffset = 0.0;
    double bestDistance = 0.0;
    const int32 count = candidates.Num();
    for (int32 index = 0; index < count; ++index)
    {
        const FSOLTargetInfo& candidate = candidates[index];
        const FVector3d toCandidate = candidate.PositionM - shipPositionM;
        const double distance = toCandidate.Size();

        // A candidate that contains the ship has no meaningful direction
        if (distance <= candidate.RadiusM)
        {
            continue;
        }

        // Offset = angle to the center minus the angular radius, floored at zero (the limb may cover the reticle)
        const double centerAngle = FMath::Atan2(FVector3d::CrossProduct(forwardDir, toCandidate).Size(),
            FVector3d::DotProduct(forwardDir, toCandidate));
        const double angularRadius = FMath::Asin(FMath::Min(1.0, candidate.RadiusM / distance));
        const double offset = FMath::Max(0.0, centerAngle - angularRadius);
        if (offset > coneHalfAngleRad)
        {
            continue;
        }

        // Keep the smallest offset; offsets within the tie tolerance go to the nearer candidate
        const bool bTie = bestIndex != INDEX_NONE && FMath::Abs(offset - bestOffset) <= TARGETING_OFFSET_TIE_RAD;
        const bool bBetter = bestIndex == INDEX_NONE || (bTie ? distance < bestDistance : offset < bestOffset);
        if (bBetter)
        {
            bestIndex = index;
            bestOffset = offset;
            bestDistance = distance;
        }
    }
    return bestIndex;
}

//////////////////////////////////////////////////////////////////////////
// Steps through the candidates ordered by distance (nearest first, stable), wrapping; returns an index or INDEX_NONE
int32 SOLTargeting::Cycle(TConstArrayView<FSOLTargetInfo> candidates, const FVector3d& shipPositionM,
    const int32 currentIndex, const int32 direction)
{
    const int32 count = candidates.Num();
    if (count == 0)
    {
        return INDEX_NONE;
    }
    const bool bHasCurrent = currentIndex >= 0 && currentIndex < count;
    if (direction == 0)
    {
        return bHasCurrent ? currentIndex : INDEX_NONE;
    }
    const bool bForward = direction > 0;
    const double currentDistSq = bHasCurrent
        ? FVector3d::DistSquared(candidates[currentIndex].PositionM, shipPositionM) : 0.0;

    // One linear pass without sorting: track the neighbor of the current entry in the (distance, index) order,
    // plus the wrap-around entry (nearest when moving forward, farthest when moving backward)
    int32 neighborIndex = INDEX_NONE;
    double neighborDistSq = 0.0;
    int32 wrapIndex = INDEX_NONE;
    double wrapDistSq = 0.0;
    for (int32 index = 0; index < count; ++index)
    {
        const double distSq = FVector3d::DistSquared(candidates[index].PositionM, shipPositionM);

        // Wrap-around entry: the first entry of the order when moving forward, the last when moving backward
        const bool bWrap = wrapIndex == INDEX_NONE || (bForward
            ? TargetingCycleLess(distSq, index, wrapDistSq, wrapIndex)
            : TargetingCycleLess(wrapDistSq, wrapIndex, distSq, index));
        if (bWrap)
        {
            wrapIndex = index;
            wrapDistSq = distSq;
        }

        // Neighbor: the closest entry strictly after (forward) or before (backward) the current one
        if (bHasCurrent)
        {
            const bool bBeyond = bForward
                ? TargetingCycleLess(currentDistSq, currentIndex, distSq, index)
                : TargetingCycleLess(distSq, index, currentDistSq, currentIndex);
            const bool bCloser = neighborIndex == INDEX_NONE || (bForward
                ? TargetingCycleLess(distSq, index, neighborDistSq, neighborIndex)
                : TargetingCycleLess(neighborDistSq, neighborIndex, distSq, index));
            if (bBeyond && bCloser)
            {
                neighborIndex = index;
                neighborDistSq = distSq;
            }
        }
    }
    return neighborIndex != INDEX_NONE ? neighborIndex : wrapIndex;
}

//////////////////////////////////////////////////////////////////////////
// Returns the locked target's velocity when the lock is active and the target is set, else the anchor velocity
FVector3d SOLTargeting::ResolveReferenceVelocity(const bool bLockActive, const FSOLTargetInfo* lockedTarget,
    const FVector3d& anchorVelocityMps)
{
    return (bLockActive && lockedTarget != nullptr) ? lockedTarget->VelocityMps : anchorVelocityMps;
}
