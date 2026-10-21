/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure-logic NPC AI and NPC-vs-NPC combat rules (SDD 11, Part 10d; contract in the plan's "10d contract"): ship
// classes, point-mass steering with acceleration and turn limits, pursuit, separation, leash, target selection, lead
// aim, fire gating, the damage / death / respawn state machine, and the statistical hit model.
namespace SOLNpcAi
{
    inline constexpr double STEER_EPSILON = 1e-6;           // Vectors and lengths below this count as zero
    inline constexpr double LEASH_SOFT_FRACTION = 0.8;       // The leash starts pulling at 80% of its length
    inline constexpr double TRANSVERSE_ERROR_FRACTION = 0.15; // Statistical model: miss from the sideways motion

    // How NPC shots are resolved (SDD 11 section 3.6; selected with sol.Npc.CombatModel)
    enum class ECombatModel : uint8 { Bolts = 0, Statistical = 1, Hybrid = 2 };

    // One ship class (the const shared class fragment's payload)
    struct FClassParams
    {
        double MaxSpeedMps = 0.0;           // Speed limit of the deviation velocity (relative to the rail)
        double MaxAccelMps2 = 0.0;
        double MaxTurnRateRadPerS = 0.0;
        double LeashM = 0.0;                // Deviation length the ship may wander from its rail
        double HitRadiusM = 0.0;
        double MaxHealth = 0.0;
        double FireRatePerS = 0.0;
        double MuzzleSpeedMps = 0.0;
        double BoltDamage = 0.0;
        double BoltLifetimeS = 0.0;
        double EngageRangeM = 0.0;          // Target acquisition range
        double FireRangeM = 0.0;            // The gun fires only inside this distance
        double FireConeRad = 0.0;           // Half-angle between heading and aim inside which the gun fires
        double StandoffM = 0.0;             // Preferred distance to the target
        double SeparationRadiusM = 0.0;     // Avoidance radius
        double AimSpreadRad = 0.0;          // Statistical model: aim error (1 sigma)
        double RespawnDelayS = 0.0;
    };

    // One ship a target search may pick
    struct FTargetCandidate
    {
        int32 Index = INDEX_NONE;
        FVector3d PositionM = FVector3d::ZeroVector;
        int32 Faction = 0;
        bool bAlive = true;
    };

    // A ship's health and death state
    struct FCombatState
    {
        double Health = 0.0;
        double RespawnS = 0.0;
        bool bAlive = true;
    };

    // Result of ApplyDamage
    struct FDamageResult
    {
        FCombatState State;
        bool bKilled = false;
    };

    // Result of StepRespawn
    struct FRespawnStep
    {
        FCombatState State;
        bool bRespawned = false;
    };

    // Returns the number of ship classes (at least 2)
    SOLTEST_API int32 ClassCount();

    // Returns the class at classIndex clamped to the valid range
    SOLTEST_API const FClassParams& GetClass(int32 classIndex);

    // Returns the class picked by a unit value in [0, 1)
    SOLTEST_API int32 PickClass(double unit);

    // Returns the velocity after steering from current toward desired under speed, acceleration and turn-rate limits
    SOLTEST_API FVector3d LimitSteering(const FVector3d& currentVelocityMps, const FVector3d& desiredVelocityMps,
        double maxSpeedMps, double maxAccelMps2, double maxTurnRateRadPerS, double dt);

    // Returns the desired velocity to close to the standoff distance of a led target, strafing at the standoff
    SOLTEST_API FVector3d PursuitVelocity(const FVector3d& selfPositionM, const FVector3d& targetPositionM,
        const FVector3d& targetVelocityMps, double maxSpeedMps, double standoffM, double leadTimeS,
        const FVector3d& strafeAxis);

    // Returns the velocity pushing a ship away from the neighbours inside the avoidance radius
    SOLTEST_API FVector3d SeparationVelocity(const FVector3d& selfPositionM,
        TConstArrayView<FVector3d> neighbourPositionsM, double radiusM, double maxSpeedMps);

    // Returns the desired velocity blended toward a pull back to the rail as the deviation nears the leash
    SOLTEST_API FVector3d LeashVelocity(const FVector3d& desiredVelocityMps, const FVector3d& deviationM,
        double leashM, double maxSpeedMps);

    // Returns the target index to engage (the sticky current target, else the nearest enemy in range, else none)
    SOLTEST_API int32 SelectTarget(int32 selfIndex, const FVector3d& selfPositionM, int32 selfFaction,
        int32 currentTarget, TConstArrayView<FTargetCandidate> candidates, double engageRangeM, double keepRangeScale);

    // Returns the unit aim direction that makes a bolt meet a target moving at constant velocity
    SOLTEST_API FVector3d LeadAimDirection(const FVector3d& shooterPositionM, const FVector3d& shooterVelocityMps,
        const FVector3d& targetPositionM, const FVector3d& targetVelocityMps, double muzzleSpeedMps);

    // Returns whether the gun may fire: target within range and the aim inside the cone around the heading
    SOLTEST_API bool ShouldFire(const FVector3d& headingDirection, const FVector3d& aimDirection, double distanceM,
        double fireRangeM, double fireConeRad);

    // Returns a fresh, alive state with full health
    SOLTEST_API FCombatState MakeAlive(double maxHealth);

    // Returns the state after damage; a lethal hit starts the respawn timer
    SOLTEST_API FDamageResult ApplyDamage(const FCombatState& state, double damage, double respawnDelayS);

    // Returns the state after dt seconds of the respawn timer; it respawns with full health once the timer runs out
    SOLTEST_API FRespawnStep StepRespawn(const FCombatState& state, double dt, double maxHealth);

    // Returns the speed component perpendicular to the line of sight
    SOLTEST_API double TransverseSpeedMps(const FVector3d& relativePositionM, const FVector3d& relativeVelocityMps);

    // Returns the probability that one shot hits under the statistical model
    SOLTEST_API double StatisticalHitProbability(double distanceM, double transverseSpeedMps, double hitRadiusM,
        double aimSpreadRad, double muzzleSpeedMps, double fireRangeM);

    // Returns whether a shot with this probability hits, given a unit random value
    SOLTEST_API bool RollHit(double unit, double probability);

    // Returns the combat model for an integer (console variable) value
    SOLTEST_API ECombatModel CombatModelFromInt(int32 value);

    // Returns whether a shooter at this distance from the camera fires real (swept) bolts under the model
    SOLTEST_API bool UsesRealBolts(ECombatModel model, double shooterDistanceToCameraM, double visualRadiusM);
}
