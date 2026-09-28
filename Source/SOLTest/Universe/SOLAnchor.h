/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Picks the anchor (nearest) body with hysteresis so the anchor does not flicker between bodies
struct SOLTEST_API FSOLAnchorSelector
{
public:

    static constexpr double SwitchRatio = 0.25;

    // Returns the current anchor index, or INDEX_NONE before the first Update
    int32 GetAnchorIndex() const;

    // Updates the anchor (nearest on first call, then switch only below SwitchRatio) and returns it
    int32 Update(TConstArrayView<FVector3d> bodyPositionsM, const FVector3d& observerPositionM);

private:

    int32 mAnchorIndex = INDEX_NONE; // Current anchor body
};
