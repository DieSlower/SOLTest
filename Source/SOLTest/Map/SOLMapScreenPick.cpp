/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapScreenPick.h"

//////////////////////////////////////////////////////////////////////////
// Returns the body of the covering candidate nearest the cursor on screen (inclusive radius), or INDEX_NONE
int32 SOLMapScreenPick::PickNearestOnScreen(const TConstArrayView<FSOLScreenPickCandidate> Candidates,
    const FVector2D& CursorPx)
{
    int32 bestBodyIndex = INDEX_NONE;
    double bestDistanceSq = TNumericLimits<double>::Max();

    // Single O(N) scan comparing squared distances (no sqrt); ties keep the earlier candidate
    for (const FSOLScreenPickCandidate& candidate : Candidates)
    {
        if (candidate.PickRadiusPx <= 0.0)
        {
            continue;
        }

        const double distanceSq = FVector2D::DistSquared(candidate.ScreenPositionPx, CursorPx);
        if (distanceSq <= candidate.PickRadiusPx * candidate.PickRadiusPx && distanceSq < bestDistanceSq)
        {
            bestDistanceSq = distanceSq;
            bestBodyIndex = candidate.BodyIndex;
        }
    }

    return bestBodyIndex;
}
