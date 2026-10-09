/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Shared plain data of the combat system (SDD 7): owner ids and the one-shot events USOLCombatSubsystem queues for the
// visuals (7c) and audio (7d). Bolts and targets themselves live in the subsystem's structure-of-arrays pools (see the
// USOLCombatSubsystem class comment for why they are not Mass entities).
namespace SOLCombat
{
    inline constexpr int32 NO_OWNER = INDEX_NONE;     // An event with no shooter (a drop or an eviction)
    inline constexpr int32 PLAYER_OWNER = 0;          // Bolts fired by the player's ship
}

// What a queued combat event reports
enum class ESOLCombatEventType : uint8
{
    BoltFired,          // A bolt left a muzzle (shot sound, muzzle flash)
    TargetHit,          // A bolt struck a target (impact effect, hit sound); the target also flashes
    ShieldBroken,       // A hit took a target's shield from above zero to zero (shield-break sound and effect)
    TargetDestroyed,    // A hit took a target's health to zero; the target is gone (explosion and debris)
    BoltAbsorbed,       // A bolt struck a belt asteroid or a ring rock and was absorbed (spark puff); the rock is unharmed
    TargetDropped,      // The player dropped a target
    TargetEvicted,      // The oldest target was removed to make room for a new drop (no explosion)
};

// One queued combat event. Positions and velocities are universe metres / m/s in the ecliptic frame, like every other
// universe position; convert with the frame's render-origin snapshot to draw.
//
// An effect or sound that lingers at the event must NOT extrapolate PositionM with a velocity in real seconds: under
// time warp the frame body moves warp times its velocity per real second. It recomputes the position every frame
// instead, as the frame body's current position plus FrameOffsetM, plus RelativeVelocityMps times the event's age in
// real seconds (SOLCombatRules::EventPositionNowM; USOLCombatSubsystem::GetFrameBodyPositionM) - exactly how the
// subsystem moves targets and bolts.
struct FSOLCombatEvent
{
    ESOLCombatEventType Type = ESOLCombatEventType::BoltFired;
    FVector3d PositionM = FVector3d::ZeroVector;      // Where it happened
    FVector3d VelocityMps = FVector3d::ZeroVector;    // The bolt's velocity for bolt events, else zero
    FVector3d FrameVelocityMps = FVector3d::ZeroVector; // Universe velocity of what the event happened on (the target,
                                                         // else the bolt's frame body), at the event; for directions
    int32 FrameBody = INDEX_NONE;                     // Body the event's position is held relative to (the target's or
                                                      // bolt's frame body: the reference body, else the anchor); none
                                                      // (INDEX_NONE) only without any body, when PositionM is fixed
    FVector3d FrameOffsetM = FVector3d::ZeroVector;   // PositionM minus FrameBody's position at the event
    FVector3d RelativeVelocityMps = FVector3d::ZeroVector; // Velocity of what it happened on relative to FrameBody (zero
                                                            // for a target at rest in a body frame)
    int32 TargetSlot = INDEX_NONE;                    // Target pool slot for target events, else INDEX_NONE
    int32 Owner = SOLCombat::NO_OWNER;                // Shooter for bolt events, else NO_OWNER
};
