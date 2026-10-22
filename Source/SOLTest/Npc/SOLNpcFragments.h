/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcRail.h"

#include "CoreMinimal.h"
#include "Mass/EntityElementTypes.h"

#include "SOLNpcFragments.generated.h"

/** Immutable rail of one NPC ship (SDD 11 section 3.3), written at spawn. */
USTRUCT()
struct FSOLNpcRailFragment : public FMassFragment
{
    GENERATED_BODY()

    FSOLNpcRail Rail;
};

/**
 * Per-frame state of one NPC ship. Only awake ships in candidate groups (or already Mid / Full) are evaluated each
 * frame; a Far ship's values are stale and unused.
 */
USTRUCT()
struct FSOLNpcStateFragment : public FMassFragment
{
    GENERATED_BODY()

    FVector3d PositionM = FVector3d::ZeroVector;      // Rail plus deviation, universe metres (ecliptic)
    FVector3d PrevPositionM = FVector3d::ZeroVector;  // Last frame's position, for the hull heading
    double WakeKeyM = 0.0;                            // A Far ship sleeps while this exceeds its group's wake clock
                                                      // (USOLNpcTierProcessor: temporal cull, SDD 11 Amendment 1)
    float DistanceM = 0.0f;                           // From the viewpoint this frame
    uint8 AiTier = 2;                                 // SOLNpcScale::EAiTier (Far)
    uint8 RenderTier = 2;                             // SOLNpcScale::ERenderTier (Point)
    uint8 bHasPrevPosition = 0;                       // PrevPositionM is valid (CPU-simulated last frame too)
};

/**
 * Per-chunk temporal cull (SDD 11 Amendment 6): the smallest WakeKeyM of the chunk's Far ships, written by the tier
 * processor's Evaluate pass. While it exceeds the group's wake clock every ship of the chunk sleeps, so both tier
 * passes skip the chunk without touching its ships. A new chunk starts at 0 (visited once).
 */
USTRUCT()
struct FSOLNpcChunkWakeFragment : public FMassChunkFragment
{
    GENERATED_BODY()

    double MinWakeKeyM = 0.0;
};

/**
 * Deviation from the rail (SDD 11 section 3.1): the ship's position and velocity in its rail's frame, integrated in
 * real time by the Mid tier (point mass) and the Full tier (6-DOF, SDD 18 section 3.7); zero when Far.
 */
USTRUCT()
struct FSOLNpcDeviationFragment : public FMassFragment
{
    GENERATED_BODY()

    FVector3d DeviationM = FVector3d::ZeroVector;
    FVector3d DeviationVelocityMps = FVector3d::ZeroVector;
};

/**
 * Attitude of a Full-tier ship (SDD 18 section 3.7): added on promotion to Full and removed on demotion. The zero
 * quaternion means "not set yet": the Full AI's gather sets it from the rail plus deviation velocity
 * (SOLNpcAi::HandoffOrientation), so the nose starts where the Mid hull was drawn heading.
 */
USTRUCT()
struct FSOLNpcAttitudeFragment : public FMassFragment
{
    GENERATED_BODY()

    FQuat4d Orientation = FQuat4d(0.0, 0.0, 0.0, 0.0);     // Ship-local -> universe axes; zero until set
    FVector3d AngularVelocityRadPerS = FVector3d::ZeroVector; // Ship-local axes: X roll, Y pitch, Z yaw
};

/** The ship's group (lane, cluster or battle) as a const shared fragment, so chunks are grouped and culled by group. */
USTRUCT()
struct FSOLNpcGroupFragment : public FMassConstSharedFragment
{
    GENERATED_BODY()

    // Returns a fragment for a global group index
    static FSOLNpcGroupFragment Make(const int32 groupIndex)
    {
        FSOLNpcGroupFragment fragment;
        fragment.GroupIndex = groupIndex;
        return fragment;
    }

    /** Global group index (FSOLNpcGroupTable order). */
    UPROPERTY()
    int32 GroupIndex = 0;
};

/**
 * Combat state of one NPC ship (SDD 11 section 3.6, Part 10d). Only Full-tier ships fight; a dead ship that leaves the
 * Full tier respawns at once (USOLNpcTierProcessor's Select pass), so Mid and Far ships are always alive.
 */
USTRUCT()
struct FSOLNpcCombatFragment : public FMassFragment
{
    GENERATED_BODY()

    int32 NpcIndex = INDEX_NONE;          // Population index (stable for the population's life; targets refer to it)
    int32 TargetNpc = INDEX_NONE;         // Population index of the current target, or none
    float Health = 0.0f;
    float RespawnS = 0.0f;                // Seconds until a dead ship respawns
    float CooldownS = 0.0f;               // Gun cooldown (SOLFireCadence)
    uint8 bAlive = 1;
};

/** The ship's class (SOLNpcAi::GetClass) as a const shared fragment: acceleration and turn limits, leash, weapons. */
USTRUCT()
struct FSOLNpcClassFragment : public FMassConstSharedFragment
{
    GENERATED_BODY()

    // Returns a fragment for a class index
    static FSOLNpcClassFragment Make(const int32 classIndex)
    {
        FSOLNpcClassFragment fragment;
        fragment.ClassIndex = classIndex;
        return fragment;
    }

    /** SOLNpcAi class index (the payload is the class table entry, so equal classes share one fragment). */
    UPROPERTY()
    int32 ClassIndex = 0;
};

/** Marks an NPC ship in the Mid AI tier (CPU-simulated, point-mass return to the rail). */
USTRUCT()
struct FSOLNpcMidAiTag : public FMassTag
{
    GENERATED_BODY()
};

/** Marks an NPC ship in the Full AI tier (CPU-simulated: steering, avoidance and combat, USOLNpcFullAiProcessor). */
USTRUCT()
struct FSOLNpcFullAiTag : public FMassTag
{
    GENERATED_BODY()
};
