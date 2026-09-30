/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// One body's on-screen pick target for the jump map's screen-space body pick (SDD 3, Appendix H)
struct FSOLScreenPickCandidate
{
    int32 BodyIndex = INDEX_NONE;
    FVector2D ScreenPositionPx = FVector2D::ZeroVector;
    double PickRadiusPx = 0.0;
};

namespace SOLMapScreenPick
{
    // Among candidates whose ScreenPositionPx is within PickRadiusPx of CursorPx (Euclidean, in pixels), returns the
    // one with the SMALLEST cursor-to-center distance (not the smallest PickRadiusPx, not any 3D/ray measure).
    // Returns INDEX_NONE if no candidate qualifies. A candidate with PickRadiusPx <= 0 never qualifies.
    SOLTEST_API int32 PickNearestOnScreen(TConstArrayView<FSOLScreenPickCandidate> Candidates,
        const FVector2D& CursorPx);
}
