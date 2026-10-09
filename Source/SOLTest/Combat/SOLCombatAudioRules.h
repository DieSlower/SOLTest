/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Combat/SOLCombatTypes.h"

#include "CoreMinimal.h"

// Placeholder combat sounds (SDD 7, Part 7d), one per kind; Count is the number of real kinds
enum class ESOLCombatSound : uint8
{
    Shot,           // A bolt left the player's gun
    ShieldHit,      // A bolt hit a target whose shield still holds
    HullHit,        // A bolt hit a target's hull (shield already down)
    ShieldBreak,    // A hit took a target's shield to zero
    Explosion,      // A target was destroyed
    Spark,          // A bolt was absorbed by a belt asteroid or ring rock
    TargetDrop,     // The player dropped a target
    Count,
    None = Count,   // The event plays no sound
};

// Pure-logic rules of the combat audio (SDD 7, Part 7d): which sound an event plays, and the per-kind minimum interval
// that keeps a burst of identical events (8 shots a second, a spray of hits) from stacking into one loud smear.
namespace SOLCombatAudioRules
{
    // Returns the sound an event plays; bTargetShielded is whether the hit target still has shield after the hit
    SOLTEST_API ESOLCombatSound SoundForEvent(ESOLCombatEventType type, bool bTargetShielded);

    // Returns true when a sound last played at lastPlayS may play again at nowS (at least minIntervalS later). A sound
    // that never played (lastPlayS = NEVER_PLAYED_S) or whose clock went backwards (a new world) always may play
    SOLTEST_API bool MayPlay(double lastPlayS, double nowS, double minIntervalS);

    // lastPlayS value of a sound that has not played yet
    inline constexpr double NEVER_PLAYED_S = -1.0e30;
}
