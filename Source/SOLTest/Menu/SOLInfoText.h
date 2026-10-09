/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic formatting for the in-game Info screen (SDD 8): turns the real gameplay constants into display lines so
// the screen cannot go stale.
namespace SOLInfoText
{
    struct FInfoLine
    {
        FString Label;
        FString Value;
    };

    // Returns a speed as "x.x m/s", "x.xx km/s" or "x km/s" ("n/a" for NaN or infinity)
    SOLTEST_API FString FormatSpeed(double metersPerSecond);

    // Returns a rate as "<v> /s" with a trimmed trailing ".0" ("n/a" for NaN or infinity)
    SOLTEST_API FString FormatRate(double perSecond);

    // Returns whole numbers without decimals and other values with one ("n/a" for NaN or infinity)
    SOLTEST_API FString FormatPercentOrValue(double value);

    // Returns the six combat lines of the Info screen
    SOLTEST_API TArray<FInfoLine> BuildCombatLines(double boltSpeedMps, double damage, double shotsPerSecond,
        double targetShield, double targetHealth, double shieldRegenPerSecond, double shieldRegenDelaySeconds);
}
