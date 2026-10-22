/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLNewtonianFlight.h"

#include "CoreMinimal.h"

// Pure-logic NPC AI and NPC-vs-NPC combat rules (SDD 11, Part 10d, and SDD 18 section 3.7; contracts in the plans'
// "10d contract" and "18c contract"): ship classes with thrust envelopes in G and pilot kinds, the Full tier's 6-DOF
// step and nose choice, the Mid tier's point-mass return to the rail, the tier hand-off helpers, pursuit, separation,
// leash, target selection, lead aim, fire gating, the damage / death / respawn state machine, and the statistical hit
// model.
namespace SOLNpcAi
{
    inline constexpr double STEER_EPSILON = 1e-6;           // Vectors and lengths below this count as zero
    inline constexpr double LEASH_SOFT_FRACTION = 0.8;       // The leash starts pulling at 80% of its length
    inline constexpr double TRANSVERSE_ERROR_FRACTION = 0.15; // Statistical model: miss from the sideways motion
    inline constexpr double FULL_HOLD_TIME_CONSTANT_S = 0.5;  // Full tier: velocity-hold time constant
    inline constexpr double NOSE_ON_TARGET_RANGE_SCALE = 1.5; // Full tier: nose on the aim inside 1.5x fire range
    inline constexpr double HULL_HEADING_MIN_SPEED_MPS = 1.0; // Hull / nose follow a velocity above this speed
    inline constexpr double MID_ARRIVAL_RADIUS_M = 1.0;       // Mid tier: back on the rail within this distance...
    inline constexpr double MID_ARRIVAL_SPEED_MPS = 0.5;      // ...and below this speed (then at rest)
    inline constexpr double CLASS_SPIN_RATE_SCALE = 2.0;      // Spin clamp = this times the largest rate limit

    // How NPC shots are resolved (SDD 11 section 3.6; selected with sol.Npc.CombatModel)
    enum class ECombatModel : uint8 { Bolts = 0, Statistical = 1, Hybrid = 2 };

    // One ship class (the const shared class fragment's payload)
    struct FClassParams
    {
        double MaxSpeedMps = 0.0;           // The AI's desired-speed cap (deviation velocity, relative to the rail)
        ESOLPilotKind PilotKind = ESOLPilotKind::Human;
        double ForwardG = 0.0;              // Thrust capability per axis (G; SDD 18 section 3.7)
        double ReverseG = 0.0;
        double LateralG = 0.0;
        double VerticalG = 0.0;
        double ClassGCapG = 0.0;            // Total-acceleration cap (G; a Human pilot is further capped at 9 G)
        FVector3d MaxAngularAccelRadPerS2 = FVector3d::ZeroVector;  // X roll, Y pitch, Z yaw
        FVector3d MaxAngularRateRadPerS = FVector3d::ZeroVector;
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
        double GunGimbalRad = 0.0;          // How far the guns swivel off the nose (turrets: PI)
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

    // Result of StepMidReturn
    struct FMidReturnStep
    {
        FVector3d DeviationM = FVector3d::ZeroVector;
        FVector3d DeviationVelocityMps = FVector3d::ZeroVector;
        bool bAtRest = false;               // Back on the rail and stopped (the Mid processor then skips the ship)
    };

    // Returns a class's thrust envelope: the G box, its angular limits and the total cap of its pilot kind
    SOLTEST_API FSOLThrustEnvelope ClassEnvelope(const FClassParams& shipClass);

    // Returns the Mid tier's point-mass acceleration budget: the lateral thrust, at most the class cap
    SOLTEST_API double MidReturnAccelMps2(const FClassParams& shipClass);

    // Returns the guidance parameters of a Mid ship's return to its rail
    SOLTEST_API FSOLGuidanceParams MidReturnGuidanceParams(const FClassParams& shipClass);

    // Returns a Mid ship's deviation after one point-mass step back toward its rail (at rest once arrived)
    SOLTEST_API FMidReturnStep StepMidReturn(const FVector3d& deviationM, const FVector3d& deviationVelocityMps,
        const FSOLGuidanceParams& params, double dt);

    // Returns the unit hull heading for a velocity (a direction perpendicular to the up hint when nearly at rest)
    SOLTEST_API FVector3d HullForward(const FVector3d& velocityMps, const FVector3d& upHint);

    // Returns the orientation a ship is promoted to the Full tier with (its forward is HullForward)
    SOLTEST_API FQuat4d HandoffOrientation(const FVector3d& velocityMps, const FVector3d& upHint);

    // Returns where a Full ship points its nose: the aim near its target, else its desired or current velocity
    SOLTEST_API FVector3d FullShipDesiredForward(const FVector3d& aimDirection, double targetDistanceM,
        double fireRangeM, const FVector3d& desiredVelocityMps, const FVector3d& velocityMps,
        const FVector3d& currentForward);

    // Returns a Full ship's state after one 6-DOF step in its rail frame: velocity hold, thrust allocation, attitude
    SOLTEST_API FSOLShipState StepFullShip(const FSOLShipState& state, const FVector3d& desiredVelocityMps,
        const FVector3d& desiredForward, const FVector3d& desiredUp, const FSOLThrustEnvelope& env, double dt);

    // Returns the direction a shot leaves in: the aim, swivelled at most gimbalRad off the nose
    SOLTEST_API FVector3d GunDirection(const FVector3d& noseDirection, const FVector3d& aimDirection, double gimbalRad);

    // Returns whether a ship may drop to the Far tier (its deviation snaps to the rail): sub-pixel or budget-forced
    SOLTEST_API bool AllowFarDemotion(double deviationLengthM, double distanceM, double pixelAngleRad,
        bool bBudgetForced);

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
