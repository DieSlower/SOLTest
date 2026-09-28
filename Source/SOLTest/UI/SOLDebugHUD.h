/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"

#include "SOLDebugHUD.generated.h"

/**
 * TEMPORARY debug text readout for sub-part 1a verification: sim date and warp, anchor, nearest body and altitude,
 * observer speed and universe position. Replaced by the real flight HUD in sub-part 1c.
 */
UCLASS()
class SOLTEST_API ASOLDebugHUD : public AHUD
{
    GENERATED_BODY()

public:

    // Draws the debug readout lines
    virtual void DrawHUD() override;
};
