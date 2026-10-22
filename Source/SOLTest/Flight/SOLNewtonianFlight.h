/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLShipState.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"

// Issue #18 Newtonian flight core: thrust-limited integrator, rotation control, thrust allocation, pilot G model,
// autopilot guidance and NPC hand-off helpers. Pure logic: no UObjects, no world access, no allocation.
namespace SOLNewtonian
{
    inline constexpr double EPSILON = 1.0e-9;
    inline constexpr double STANDARD_GRAVITY_MPS2 = 9.80665;
    inline constexpr double HUMAN_MAX_G = 9.0;
    inline constexpr double ATTITUDE_GAIN_PER_S = 4.0;
    inline constexpr double ATTITUDE_BRAKE_FRACTION = 0.8;
    inline constexpr double GUIDANCE_ACCEL_MARGIN = 0.9;
    inline constexpr double GUIDANCE_MIN_ACCEL_FRACTION = 0.1;
    inline constexpr double MIN_TIME_CONSTANT_S = 1.0e-3;
    inline constexpr double FORWARD_HOLD_FRACTION = 0.05;
    inline constexpr double G_LOAD_TOLERANCE_G = 1.0e-9;    // "Above sustained" means above SustainedG plus this
}

// Thrust capability of a hull and its engines (defaults: the player ship, SDD 18 section 3.2)
struct FSOLThrustEnvelope
{
    double ForwardAccelMps2 = SOL::PLAYER_THRUST_FORWARD_G * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    double ReverseAccelMps2 = SOL::PLAYER_THRUST_REVERSE_G * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    double LateralAccelMps2 = SOL::PLAYER_THRUST_LATERAL_G * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    double VerticalAccelMps2 = SOL::PLAYER_THRUST_VERTICAL_G * SOLNewtonian::STANDARD_GRAVITY_MPS2;
    double BoostMultiplier = SOL::PLAYER_THRUST_BOOST_MULTIPLIER;             // every translation axis
    double MaxTotalAccelMps2 = SOL::PLAYER_THRUST_TOTAL_CAP_G * SOLNewtonian::STANDARD_GRAVITY_MPS2; // <= 0 = none
    FVector3d MaxAngularAccelRadPerS2 = FVector3d(SOL::PLAYER_ANGULAR_ACCEL_ROLL_RADPS2,  // X roll, Y pitch, Z yaw
        SOL::PLAYER_ANGULAR_ACCEL_PITCH_YAW_RADPS2, SOL::PLAYER_ANGULAR_ACCEL_PITCH_YAW_RADPS2);
    FVector3d MaxAngularRateRadPerS = FVector3d(SOL::PLAYER_ANGULAR_RATE_ROLL_RADPS,
        SOL::PLAYER_ANGULAR_RATE_PITCH_YAW_RADPS, SOL::PLAYER_ANGULAR_RATE_PITCH_YAW_RADPS);
    double MaxSpinRateRadPerS = SOL::PLAYER_SPIN_CLAMP_RADPS;
    double MaxSpeedMps = SOL::MAX_SPEED_CAP_MPS;
};

// The integrator's only input: local thrust and torque fractions in [-1, 1], plus boost
struct FSOLNewtonianCommand
{
    FVector3d ThrustFraction = FVector3d::ZeroVector;
    FVector3d AngularAccelFraction = FVector3d::ZeroVector;
    bool bBoost = false;
};

// Result of fitting a desired world thrust acceleration into the thrust envelope
struct FSOLThrustAllocation
{
    FVector3d ThrustFraction = FVector3d::ZeroVector;
    FVector3d AchievedAccelWorldMps2 = FVector3d::ZeroVector;
    bool bSaturated = false;
};

// Budgets and tolerances of the rendezvous (flip-and-burn) guidance law
struct FSOLGuidanceParams
{
    double PlanAccelMps2 = 0.0;
    double BrakeAccelMps2 = 0.0;
    double FlipTimeS = 0.0;
    double MaxCruiseSpeedMps = SOL::MAX_SPEED_CAP_MPS;
    double ArrivalRadiusM = 10.0;
    double ArrivalSpeedMps = 1.0;
    double VelocityGainPerS = 2.0;
};

// Phase of the rendezvous guidance
enum class ESOLGuidancePhase : uint8
{
    Accelerate,
    Cruise,
    Brake,
    Arrived
};

// One guidance evaluation: the acceleration and nose demands plus the HUD values
struct FSOLGuidanceOutput
{
    FVector3d DesiredNetAccelMps2 = FVector3d::ZeroVector;
    FVector3d DesiredThrustAccelMps2 = FVector3d::ZeroVector;
    FVector3d DesiredForward = FVector3d::ZeroVector;
    ESOLGuidancePhase Phase = ESOLGuidancePhase::Arrived;
    double DistanceM = 0.0;
    double ClosingSpeedMps = 0.0;
    double SpeedLimitMps = 0.0;
    double EtaS = 0.0;                                      // estimated seconds to arrival; 0 when Arrived
};

// Human pilot G tolerance (upgradeable); the defaults are the untrained baseline
struct FSOLPilotGLimits
{
    double SustainedG = SOL::PILOT_SUSTAINED_G;
    double MaxG = SOL::PILOT_MAX_G;
    double TimeToEmptyAtMaxGS = SOL::PILOT_TIME_TO_EMPTY_AT_MAX_G_S;
    double DrainExponent = SOL::PILOT_DRAIN_EXPONENT;
    double RecoverPerS = SOL::PILOT_RECOVER_PER_S;
    double BlackoutDurationS = SOL::PILOT_BLACKOUT_DURATION_S;
    double WakeMeter = SOL::PILOT_WAKE_METER;
    double CriticalMeter = SOL::PILOT_CRITICAL_METER;
    double VisionStartMeter = SOL::PILOT_VISION_START_METER;
    double AutopilotRecoveryG = SOL::PILOT_AUTOPILOT_RECOVERY_G;
    double UnconsciousRecoverPerS = SOL::PILOT_UNCONSCIOUS_RECOVER_PER_S;   // G-LOC recovery floor, whatever the load
    double LethalWarningS = SOL::PILOT_LETHAL_WARNING_S;   // Depleted this long under load: the "lethal G" warning
};

// Per-pilot G-tolerance meter (1 = full) and G-LOC state
struct FSOLGMeterState
{
    double Meter = 1.0;
    bool bBlackout = false;
    double BlackoutRemainingS = 0.0;
    double DepletedS = 0.0;     // Seconds in a row with the load above sustained and the meter critical or in G-LOC
};

// HUD warning level of the pilot G model
enum class ESOLGWarning : uint8
{
    None,
    OverSustained,
    Critical,
    Blackout,
    Lethal          // Depleted for LethalWarningS under load (HUD only: no death until a damage system exists)
};

// Who flies an NPC class: a human (G-limited) or an AI (structural limit only)
enum class ESOLPilotKind : uint8
{
    Human,
    AI
};

namespace SOLNewtonian
{
    // Returns the local thrust acceleration for the fractions (per-axis box, boost, then the total cap)
    SOLTEST_API FVector3d LocalThrustAccelMps2(const FVector3d& thrustFraction, const FSOLThrustEnvelope& env,
        bool bBoost);

    // Advances the state by dt with torque, thrust and gravity (semi-implicit Euler, no damping)
    SOLTEST_API FSOLShipState StepNewtonian(const FSOLShipState& state, const FSOLNewtonianCommand& command,
        const FSOLThrustEnvelope& env, const FVector3d& gravityAccelMps2, double dt);

    // Returns the per-axis torque fraction that reaches the commanded rate this step (deadbeat, bounded)
    SOLTEST_API FVector3d RateCommandAngularFraction(const FVector3d& angularVelocityRadPerS,
        const FVector3d& commandRateRadPerS, const FSOLThrustEnvelope& env, double dt);

    // Returns the rate command that turns the nose to the desired forward and the roll to the desired up
    SOLTEST_API FVector3d AttitudeRateCommand(const FQuat4d& orientation, const FVector3d& desiredForwardWorld,
        const FVector3d& desiredUpWorld, const FSOLThrustEnvelope& env);

    // Returns the rest-to-rest bang-bang time to turn by angle with a rate limit and angular acceleration
    SOLTEST_API double RotationTimeS(double angleRad, double maxRateRadPerS, double alphaRadPerS2);

    // Returns the distance needed to stop from speed: coast for the flip time, then brake at a
    SOLTEST_API double StoppingDistanceM(double speedMps, double brakeAccelMps2, double flipTimeS);

    // Returns the highest closing speed that can still stop within d after a flip time
    SOLTEST_API double BrakingSpeedLimitMps(double distanceM, double brakeAccelMps2, double flipTimeS);

    // Scales a desired world thrust acceleration down to fit the per-axis box and total cap, keeping its direction
    SOLTEST_API FSOLThrustAllocation AllocateThrust(const FVector3d& desiredAccelWorldMps2, const FQuat4d& orientation,
        const FSOLThrustEnvelope& env, bool bBoost);

    // Returns the thrust acceleration that relaxes v toward vTarget with time constant tau, compensating gravity
    SOLTEST_API FVector3d VelocityHoldAccelMps2(const FVector3d& velocityMps, const FVector3d& targetVelocityMps,
        const FVector3d& gravityAccelMps2, double timeConstantS, double dt);

    // Returns the G-load of a proper (non-gravitational) acceleration
    SOLTEST_API double GLoadG(const FVector3d& properAccelMps2);

    // Returns the meter drain per second at a G-load (0 at or below the sustained limit)
    SOLTEST_API double GDrainPerS(double gLoad, const FSOLPilotGLimits& limits);

    // Advances the G meter by dt at a constant G-load (exact piecewise-linear, including G-LOC entry and wake)
    SOLTEST_API FSOLGMeterState StepGMeter(const FSOLGMeterState& state, double gLoad, const FSOLPilotGLimits& limits,
        double dt);

    // Returns the HUD warning level for the meter and the current G-load
    SOLTEST_API ESOLGWarning GWarningLevel(const FSOLGMeterState& state, double gLoad, const FSOLPilotGLimits& limits);

    // Returns the tunnel-vision / gray-out amount in [0, 1] (1 during G-LOC)
    SOLTEST_API double GVisionLoss(const FSOLGMeterState& state, const FSOLPilotGLimits& limits);

    // Returns true while the pilot is conscious
    SOLTEST_API bool PilotHasControl(const FSOLGMeterState& state);

    // Returns the pilot's sustained acceleration limit in m/s^2
    SOLTEST_API double PilotSustainedAccelMps2(const FSOLPilotGLimits& limits);

    // Returns the autopilot's acceleration limit: sustained, or the recovery limit while critical or in G-LOC
    SOLTEST_API double AutopilotAccelLimitMps2(const FSOLPilotGLimits& limits, const FSOLGMeterState& state);

    // Returns the desired thrust acceleration clamped to a length limit
    SOLTEST_API FVector3d ClampToPilotLimit(const FVector3d& desiredThrustAccelMps2, double limitMps2);

    // Returns an NPC class's effective G cap: humans are held to HUMAN_MAX_G, AIs use the structural cap
    SOLTEST_API double EffectiveGCapG(ESOLPilotKind kind, double classCapG);

    // Returns a copy of the envelope whose total-acceleration cap is at most capG
    SOLTEST_API FSOLThrustEnvelope WithGCap(const FSOLThrustEnvelope& env, double capG);

    // Returns the rendezvous guidance (braking-curve law in the target's frame) for one step
    SOLTEST_API FSOLGuidanceOutput ComputeRendezvousGuidance(const FVector3d& relativePositionM,
        const FVector3d& relativeVelocityMps, const FVector3d& targetAccelMps2, const FVector3d& gravityAccelMps2,
        const FSOLGuidanceParams& params, double dt);

    // As above; with bComputeEta false EtaS stays 0 (hot loops that never show an ETA skip its cost)
    SOLTEST_API FSOLGuidanceOutput ComputeRendezvousGuidance(const FVector3d& relativePositionM,
        const FVector3d& relativeVelocityMps, const FVector3d& targetAccelMps2, const FVector3d& gravityAccelMps2,
        const FSOLGuidanceParams& params, double dt, bool bComputeEta);

    // Returns guidance budgets from the envelope, the pilot limit and gravity (flip-and-burn or reverse braking)
    SOLTEST_API FSOLGuidanceParams MakeGuidanceParams(const FSOLThrustEnvelope& env, double pilotAccelLimitMps2,
        double gravityMagnitudeMps2, bool bFlipToBrake, bool bBoost);

    // Returns the ETA of the 1-D braking-curve plan (DBL_MAX for invalid inputs)
    SOLTEST_API double EstimateTimeToArrivalS(double distanceM, double closingSpeedMps, double accelMps2,
        double brakeAccelMps2, double flipTimeS, double cruiseCapMps);

    // Writes the predicted ship-minus-target path into the view; returns the number of points written
    SOLTEST_API int32 SampleRendezvousTrajectory(const FVector3d& relativePositionM,
        const FVector3d& relativeVelocityMps, const FSOLGuidanceParams& params, double sampleIntervalS, int32 substepsPerSample,
        TArrayView<FVector3d> outShipMinusTargetM);

    // Returns the orientation with its nose on forward and its up toward the hint (fallback for a bad forward)
    SOLTEST_API FQuat4d OrientationFromForward(const FVector3d& forward, const FVector3d& upHint,
        const FQuat4d& fallback);

    // Returns true when snapping by offset at distance moves at most half a pixel
    SOLTEST_API bool IsHandoffSnapInvisible(double offsetM, double distanceM, double pixelAngleRad);
}
