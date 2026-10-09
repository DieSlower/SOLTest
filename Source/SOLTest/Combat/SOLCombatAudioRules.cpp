/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Combat/SOLCombatAudioRules.h"

//////////////////////////////////////////////////////////////////////////
// Returns the sound an event plays; bTargetShielded is whether the hit target still has shield after the hit
ESOLCombatSound SOLCombatAudioRules::SoundForEvent(const ESOLCombatEventType type, const bool bTargetShielded)
{
    switch (type)
    {
    case ESOLCombatEventType::BoltFired:
        return ESOLCombatSound::Shot;
    case ESOLCombatEventType::TargetHit:
        return bTargetShielded ? ESOLCombatSound::ShieldHit : ESOLCombatSound::HullHit;
    case ESOLCombatEventType::ShieldBroken:
        return ESOLCombatSound::ShieldBreak;
    case ESOLCombatEventType::TargetDestroyed:
        return ESOLCombatSound::Explosion;
    case ESOLCombatEventType::BoltAbsorbed:
        return ESOLCombatSound::Spark;
    case ESOLCombatEventType::TargetDropped:
        return ESOLCombatSound::TargetDrop;
    case ESOLCombatEventType::TargetEvicted:
    default:
        // An eviction quietly makes room for a new drop, whose own sound covers it
        return ESOLCombatSound::None;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true when a sound last played at lastPlayS may play again at nowS (at least minIntervalS later)
bool SOLCombatAudioRules::MayPlay(const double lastPlayS, const double nowS, const double minIntervalS)
{
    return nowS < lastPlayS || nowS - lastPlayS >= minIntervalS;
}
