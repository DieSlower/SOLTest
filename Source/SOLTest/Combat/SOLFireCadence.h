/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic gun cadence (SDD 7): shots fire on a continuous timeline at a fixed rate while the trigger is held, so the
// number and timing of shots never depend on how the held time is split into frames.
namespace SOLFireCadence
{
    // Result of one cadence step
    struct FStep
    {
        int32 Shots = 0;              // Shots fired during this step
        double CooldownS = 0.0;       // Seconds until the gun may fire again, measured from the end of this step (>= 0)
        double FirstShotAgeS = 0.0;   // Seconds between the first shot of this step and the end of the step
    };

    // Advances the gun by dt seconds and returns the shots fired; the cooldown also runs out while the trigger is up
    SOLTEST_API FStep Step(double cooldownS, double dt, double shotsPerSecond, bool bTriggerHeld, int32 maxShots);

    // Returns how long before the end of the step shot shotIndex was fired (never negative)
    SOLTEST_API double ShotAgeS(const FStep& step, int32 shotIndex, double shotsPerSecond);
}
