/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLNewtonianFlight.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"

#include <cfloat>

// Issue #32 Chase Down autopilot (SDD 32 sections 3.2-3.6): the G cap, the meter floor, the per-substep G policy, the
// burst / cool planner and the tail aim point. Pure logic: no UObjects, no world access, no allocation. Frames and
// units as SDD 18 (Unreal-handed universe frame, metres, m/s, seconds); every G value is in units of standard gravity.

// What the chase does in one substep (HUD and run state)
enum class ESOLChaseActivity : uint8
{
    Burst,          // Accelerate / Cruise phase at BurstG, the meter draining
    Cool,           // Accelerate / Cruise phase at sustained, the meter recovering
    BrakeBurst,     // Brake phase at BurstG
    BrakeCool,      // Brake phase at sustained
    Hold            // On the tail (guidance Arrived)
};

// A chase plan's G levels and its meter floor
struct FSOLChasePolicy
{
    double BurstG = SOL::PILOT_SUSTAINED_G;            // Level of every burst (accelerating and braking)
    double BrakePlanG = SOL::PILOT_SUSTAINED_G;        // Average deceleration the braking curve is designed for
    double FloorMeter = SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT;     // The meter never planned below this
    double ResumeHysteresis = SOL::Chase::COOL_HYSTERESIS_METER;        // A cool off ends this far above the switch
};

// Per-ship chase state carried from substep to substep
struct FSOLChaseRunState
{
    bool bCooling = false;
    double LastLimitG = 0.0;
    ESOLChaseActivity LastActivity = ESOLChaseActivity::Burst;
};

// One substep's G limit and the thresholds it was decided with
struct FSOLChaseGLimit
{
    double LimitG = SOL::PILOT_SUSTAINED_G;
    ESOLChaseActivity Activity = ESOLChaseActivity::Cool;
    double ReserveMeter = 0.0;      // Meter the rest of the brake needs (BrakeReserveMeter)
    double ResumeMeter = 0.0;       // A cool off ends at this meter (floor + reserve + hysteresis)
};

// What the planner sees: the 1-D chase along the line of sight in the target's frame
struct FSOLChasePlanInput
{
    double DistanceM = 0.0;                         // To the aim point
    double ClosingSpeedMps = 0.0;                   // > 0 closing, < 0 receding
    double GravityMps2 = 0.0;                       // Magnitude of the local gravity (the budgets subtract it)
    double CruiseCapMps = SOL::MAX_SPEED_CAP_MPS;
    double ArrivalRadiusM = SOL::AUTOPILOT_ARRIVAL_RADIUS_M;
    double ArrivalSpeedMps = SOL::AUTOPILOT_ARRIVAL_SPEED_MPS;
    FSOLThrustEnvelope Envelope;
    FSOLPilotGLimits Limits;
    FSOLGMeterState Meter;
    double FloorMeter = SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT;    // EffectiveMeterFloor of the setting
    bool bBoost = true;                             // The player's Tab boost toggle (SDD 32 Q10)
};

// Predicted outcome of one policy
struct FSOLChasePrediction
{
    bool bIntercepts = false;
    double TimeS = DBL_MAX;         // Predicted time to intercept (DBL_MAX when not intercepting)
    double MinMeter = 0.0;          // Lowest predicted meter
    double EndMeter = 0.0;          // Meter at the intercept
};

// The planner's choice
struct FSOLChasePlan
{
    bool bValid = false;
    FSOLChasePolicy Policy;
    FSOLChasePrediction Prediction;
};

/*
 * Per-substep use (the ship processor, part 32c), in this order:
 * 1. capG = ChaseMaxG(env, limits, bBoostToggle); the aim point from ChaseAimPointM.
 * 2. The guidance phase for ChaseGLimit comes from a phase probe: ComputeRendezvousGuidance (bComputeEta false) with
 *    MakeChasePhaseProbeParams(env, policy, gravity, bBoost) and the order's cruise cap and arrival tolerances. The
 *    probe's Phase and ClosingSpeedMps are what ChaseGLimit takes. (The guidance phase depends only on the brake
 *    budget, the flip time, the cruise cap and the tolerances, never on the plan budget, so the probe's phase is the
 *    phase of the step's real guidance call too.)
 * 3. limit = ChaseGLimit(policy, run, meter, probe.Phase, probe.ClosingSpeedMps,
 *    MakeChasePhaseProbeParams(...).BrakeAccelMps2, capG, limits, dt).
 * 4. The step's guidance with MakeChaseGuidanceParams(env, limit.LimitG, policy.BrakePlanG, gravity, bBoost) (same
 *    cruise cap and tolerances), the thrust through ClampToPilotLimit(thrust, limit.LimitG * g0), then the allocation,
 *    StepNewtonian and StepGMeter as for the autopilot.
 */
namespace SOLChase
{
    // Returns the chase G cap: min(MaxG, forward thrust (boosted only with bBoost) / g0, total cap / g0), at least S
    SOLTEST_API double ChaseMaxG(const FSOLThrustEnvelope& env, const FSOLPilotGLimits& limits, bool bBoost);

    // Returns the meter floor of a margin: clamped to the setting range, at least MIN_METER_FLOOR, NaN -> default
    SOLTEST_API double EffectiveMeterFloor(double margin);

    // Returns the highest constant G for dt that leaves the meter at or above the floor (S at or below it, in G-LOC
    // or for a bad dt); uncapped, the caller caps it
    SOLTEST_API double MaxSafeGForStep(const FSOLGMeterState& meter, double floorMeter, const FSOLPilotGLimits& limits,
        double dt);

    // Returns the meter the rest of the brake uses net of recovery (bursts at burstG averaging brakePlanG)
    SOLTEST_API double BrakeReserveMeter(double closingSpeedMps, double burstG, double brakePlanG,
        double brakeAccelMps2, const FSOLPilotGLimits& limits);

    // Returns the guidance budgets of a chase step: plan from limitG, brake from brakePlanG, flip to brake
    SOLTEST_API FSOLGuidanceParams MakeChaseGuidanceParams(const FSOLThrustEnvelope& env, double limitG,
        double brakePlanG, double gravityMps2, bool bBoost);

    // Returns the guidance params of the phase probe that feeds ChaseGLimit (the policy's BurstG and BrakePlanG)
    SOLTEST_API FSOLGuidanceParams MakeChasePhaseProbeParams(const FSOLThrustEnvelope& env,
        const FSOLChasePolicy& policy, double gravityMps2, bool bBoost);

    // Returns one substep's G limit and activity (burst / cool hysteresis, then the cap and the per-step safety clamp)
    SOLTEST_API FSOLChaseGLimit ChaseGLimit(const FSOLChasePolicy& policy, FSOLChaseRunState& inOutRun,
        const FSOLGMeterState& meter, ESOLGuidancePhase phase, double closingSpeedMps, double brakeAccelMps2,
        double capG, const FSOLPilotGLimits& limits, double dt);

    // Returns the closed-form, segment-by-segment prediction of a policy's time to intercept and meter
    SOLTEST_API FSOLChasePrediction PredictChase(const FSOLChasePlanInput& input, const FSOLChasePolicy& policy);

    // Returns the fastest feasible policy of the BurstG x BrakePlanG grid (ties to the gentler levels)
    SOLTEST_API FSOLChasePlan PlanChase(const FSOLChasePlanInput& input);

    // Returns true when the candidate plan should replace the current one
    SOLTEST_API bool ShouldReplacePlan(const FSOLChasePlan& current, const FSOLChasePlan& candidate);

    // Returns the tail point: behind a moving target along its frame-relative velocity, else on the approach side
    SOLTEST_API FVector3d ChaseAimPointM(const FVector3d& targetM, const FVector3d& targetRelFrameVelocityMps,
        const FVector3d& approachDir, double trailM);

    // Returns the trail distance max(minTrailM, k x radius); a bad radius or k gives minTrailM
    SOLTEST_API double ChaseTrailM(double radiusM, double minTrailM, double k);

    // Returns the low-pass target acceleration estimate from a velocity change, its length clamped to capMps2
    SOLTEST_API FVector3d EstimateTargetAccelMps2(const FVector3d& prevVelocityMps, const FVector3d& velocityMps,
        const FVector3d& prevEstimateMps2, double dt, double capMps2);
}
