/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLDamageMath.h"

//////////////////////////////////////////////////////////////////////////
// Returns a fresh state
SOLDamageMath::FDamageState SOLDamageMath::MakeState(double shield, double health)
{
    FDamageState state;
    state.Shield = shield;
    state.Health = health;
    state.bDead = !(health > 0.0);
    return state;
}

//////////////////////////////////////////////////////////////////////////
// Applies damage to the shield, then the health
SOLDamageMath::FDamageState SOLDamageMath::ApplyDamage(const FDamageState& state, double damage)
{
    if (state.bDead || !(damage > 0.0))
    {
        return state;
    }
    FDamageState result = state;
    const double absorbed = FMath::Min(result.Shield, damage);
    result.Shield -= absorbed;
    result.Health = FMath::Max(result.Health - (damage - absorbed), 0.0);
    result.SecondsSinceLastHit = 0.0;
    result.bDead = !(result.Health > 0.0);
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Advances the hit timer and regenerates the shield past the delay
SOLDamageMath::FDamageState SOLDamageMath::TickRegen(const FDamageState& state, double dt, double regenDelaySeconds,
    double regenPerSecond, double maxShield)
{
    if (state.bDead || !(dt > 0.0) || !FMath::IsFinite(dt))
    {
        return state;
    }
    FDamageState result = state;
    const double before = state.SecondsSinceLastHit;
    result.SecondsSinceLastHit = before + dt;

    // Only the part of this step past the delay regenerates
    const double regenSeconds = FMath::Max(result.SecondsSinceLastHit - FMath::Max(before, regenDelaySeconds), 0.0);
    if (regenSeconds > 0.0 && regenPerSecond > 0.0)
    {
        result.Shield = FMath::Max(FMath::Min(result.Shield + regenPerSecond * regenSeconds, maxShield), result.Shield);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the damage that was not needed to kill
double SOLDamageMath::OverkillDamage(const FDamageState& before, double damage)
{
    if (before.bDead || !(damage > 0.0))
    {
        return 0.0;
    }
    return FMath::Max(damage - before.Shield - before.Health, 0.0);
}
