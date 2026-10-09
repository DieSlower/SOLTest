/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic shield-then-health damage model for targets (SDD 7).
namespace SOLDamageMath
{
    // A target's shield, health and time since it was last hit
    struct FDamageState
    {
        double Shield = 0.0;
        double Health = 0.0;
        double SecondsSinceLastHit = 0.0;
        bool bDead = false;
    };

    // Returns a fresh state with the given shield and health
    SOLTEST_API FDamageState MakeState(double shield, double health);

    // Returns the state after damage: shield first, overflow to health; a dead state or an invalid damage is unchanged
    SOLTEST_API FDamageState ApplyDamage(const FDamageState& state, double damage);

    // Returns the state after dt seconds, regenerating the shield once the regen delay has passed
    SOLTEST_API FDamageState TickRegen(const FDamageState& state, double dt, double regenDelaySeconds,
        double regenPerSecond, double maxShield);

    // Returns the damage beyond what was needed to reach exactly zero health (0 when not lethal)
    SOLTEST_API double OverkillDamage(const FDamageState& before, double damage);
}
