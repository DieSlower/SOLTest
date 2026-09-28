/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLAnchor.h"

namespace
{
    // Squared switch ratio, so the hysteresis test can compare squared distances without square roots
    constexpr double ANCHOR_SWITCH_RATIO_SQUARED = FSOLAnchorSelector::SwitchRatio * FSOLAnchorSelector::SwitchRatio;
}

//////////////////////////////////////////////////////////////////////////
// Returns the current anchor index, or INDEX_NONE before the first Update
int32 FSOLAnchorSelector::GetAnchorIndex() const
{
    return mAnchorIndex;
}

//////////////////////////////////////////////////////////////////////////
// Updates the anchor (nearest on first call, then switch only below SwitchRatio) and returns it
int32 FSOLAnchorSelector::Update(TConstArrayView<FVector3d> bodyPositionsM, const FVector3d& observerPositionM)
{
    const int32 bodyCount = bodyPositionsM.Num();
    if (bodyCount == 0)
    {
        return mAnchorIndex;
    }

    // A missing or stale anchor (body list shrank) is re-picked as the plain nearest body
    const bool bHasAnchor = mAnchorIndex >= 0 && mAnchorIndex < bodyCount;

    // Nearest body other than the current anchor (or the nearest overall when there is none)
    int32 candidateIndex = INDEX_NONE;
    double candidateDistSq = TNumericLimits<double>::Max();
    for (int32 index = 0; index < bodyCount; ++index)
    {
        if (bHasAnchor && index == mAnchorIndex)
        {
            continue;
        }
        const double distSq = FVector3d::DistSquared(bodyPositionsM[index], observerPositionM);
        if (distSq < candidateDistSq)
        {
            candidateDistSq = distSq;
            candidateIndex = index;
        }
    }

    if (!bHasAnchor)
    {
        mAnchorIndex = candidateIndex;
    }
    else if (candidateIndex != INDEX_NONE)
    {
        const double anchorDistSq = FVector3d::DistSquared(bodyPositionsM[mAnchorIndex], observerPositionM);
        if (candidateDistSq < ANCHOR_SWITCH_RATIO_SQUARED * anchorDistSq)
        {
            mAnchorIndex = candidateIndex;
        }
    }
    return mAnchorIndex;
}
