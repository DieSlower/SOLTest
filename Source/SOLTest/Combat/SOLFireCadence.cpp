/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLFireCadence.h"

//////////////////////////////////////////////////////////////////////////
// Advances the cooldown by dt and fires every shot whose time on the continuous timeline falls inside the step
SOLFireCadence::FStep SOLFireCadence::Step(double cooldownS, double dt, double shotsPerSecond, bool bTriggerHeld,
    int32 maxShots)
{
    FStep result;
    const double cooldown = FMath::IsFinite(cooldownS) ? FMath::Max(cooldownS, 0.0) : 0.0;
    result.CooldownS = cooldown;
    if (!(dt > 0.0) || !FMath::IsFinite(dt))
    {
        return result;
    }

    // Trigger up, an unusable rate or a zero cap: no shots, but the cooldown still runs out (never banked below zero)
    const bool bCanFire = bTriggerHeld && shotsPerSecond > 0.0 && FMath::IsFinite(shotsPerSecond) && maxShots > 0;
    if (!bCanFire || cooldown > dt)
    {
        result.CooldownS = FMath::Max(cooldown - dt, 0.0);
        return result;
    }

    // The first shot fires once the cooldown has run out (cooldown seconds into the step), then one per interval
    const double intervalS = 1.0 / shotsPerSecond;
    const double shotsInStep = FMath::FloorToDouble((dt - cooldown) / intervalS) + 1.0;
    result.Shots = static_cast<int32>(FMath::Min(shotsInStep, static_cast<double>(maxShots)));
    result.FirstShotAgeS = dt - cooldown;
    result.CooldownS = FMath::Max(cooldown + static_cast<double>(result.Shots) * intervalS - dt, 0.0);
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the first shot's age minus one interval per later shot
double SOLFireCadence::ShotAgeS(const FStep& step, int32 shotIndex, double shotsPerSecond)
{
    if (!(shotsPerSecond > 0.0) || !FMath::IsFinite(shotsPerSecond))
    {
        return FMath::Max(step.FirstShotAgeS, 0.0);
    }
    return FMath::Max(step.FirstShotAgeS - static_cast<double>(FMath::Max(shotIndex, 0)) / shotsPerSecond, 0.0);
}
