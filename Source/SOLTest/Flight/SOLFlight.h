/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLNewtonianFlight.h"
#include "Flight/SOLShipState.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"

// Tunable flight-model parameters of one ship class (SDD 18: thrust envelope in G plus the pilot's G limits)
struct FSOLFlightParams
{
    double AssistTimeConstantS = 0.75;                      // assisted velocity hold
    FSOLThrustEnvelope Envelope;                            // thrust, rotation and speed capability (player defaults)
    FSOLPilotGLimits PilotLimits;                           // pilot G tolerance (untrained baseline)
    double BoostSpeedMultiplier = 2.0;                      // assisted: boost doubles the target speed
    double MaxSpeedMps = SOL::MAX_SPEED_CAP_MPS;            // 0.5c: assisted target and speed-cap clamp
    double MinSpeedCapMps = SOL::MIN_SPEED_CAP_MPS;         // 1 m/s
    double ShipRadiusM = 10.0;
};

// Player (or AI) control input for one flight step
struct FSOLShipControl
{
    FVector3d Thrust = FVector3d::ZeroVector;       // local X forward, Y right, Z up, each in [-1, 1]
    FVector3d Rotation = FVector3d::ZeroVector;     // X roll, Y pitch, Z yaw in [-1, 1]: rate (assisted) or torque
                                                    // (manual)
    bool bBoost = false;
    bool bFlightAssist = true;                      // true = assisted, false = manual
    bool bStopSpin = false;                         // cancel all rotation (Z)
    double SpeedCapMps = 1000.0;                    // relative to the reference frame
    FVector3d ReferenceVelocityMps = FVector3d::ZeroVector;
    FVector3d AttitudeUpWorld = FVector3d::ZeroVector;  // assisted attitude target's up (surface-lock); zero = none
};

// What one flight step did besides moving the ship
struct FSOLFlightStepOutput
{
    FVector3d ThrustAccelWorldMps2 = FVector3d::ZeroVector;    // achieved thrust acceleration (the G-load source)
};

namespace SOLFlight
{
    constexpr double SpeedCapStepsPerDecade = 10.0;

    // Returns the assisted thrust request: gravity compensation first, then as much velocity hold as the limit allows
    SOLTEST_API FVector3d AssistThrustAccelMps2(const FVector3d& velocityMps, const FVector3d& targetVelocityMps,
        const FVector3d& gravityAccelMps2, double timeConstantS, double dt, double limitMps2);

    // Advances the ship by dt in assisted (rate command, velocity hold at the pilot limit) or manual (torque, thrust)
    // mode on the Newtonian core, gravity included in both
    SOLTEST_API FSOLShipState Step(const FSOLShipState& state, const FSOLShipControl& control,
        const FSOLFlightParams& params, const FVector3d& gravityAccelMps2, double dt,
        FSOLFlightStepOutput* outStep = nullptr);

    // Returns the ship's forward projected level to the plane perpendicular to up (the forward itself if degenerate)
    SOLTEST_API FVector3d LevelForward(const FQuat4d& orientation, const FVector3d& upWorld);

    // Returns the contact (normal) acceleration of a ship resting on a surface: what presses it into the ground
    SOLTEST_API FVector3d ContactAccelMps2(const FVector3d& outwardNormal, const FVector3d& bodyGravityAccelMps2,
        const FVector3d& thrustAccelWorldMps2);

    // Returns the number of sim-time substeps the autopilot uses for one frame of sim time (0 for invalid input)
    SOLTEST_API int32 AutopilotSubstepCount(double frameSimS);

    // Advances the ship by dt under the rendezvous autopilot (flip-and-burn at the pilot's autopilot limit). A route
    // fly-through (SDD 20) passes its pass speed and capture radius into the guidance (both 0: stop at the target)
    SOLTEST_API FSOLShipState AutopilotStep(const FSOLShipState& state, const FVector3d& targetPositionM,
        const FVector3d& targetVelocityMps, const FVector3d& targetAccelMps2, const FVector3d& gravityAccelMps2,
        const FSOLFlightParams& params, const FSOLGMeterState& meter, double dt,
        FSOLFlightStepOutput* outStep = nullptr, FSOLGuidanceOutput* outGuidance = nullptr,
        double targetGravityMps2 = 0.0, double passSpeedMps = 0.0, double captureRadiusM = 0.0);

    // Returns the autopilot's arrival distance from a body's centre: the geometric standoff, moved out until the
    // body's gravity there is at most AUTOPILOT_GRAVITY_STANDOFF_FRACTION of the autopilot limit
    SOLTEST_API double AutopilotStandoffM(double bodyRadiusM, double bodyGM, double accelLimitMps2);

    // Returns true when the autopilot flies under a warp too high for precise gravity near a body (the HUD hint)
    SOLTEST_API bool IsAutopilotPrecisionReduced(double warpFactor, double distanceToBodyCenterM, double bodyRadiusM);

    // Returns the summed gravitational acceleration toward every body, with r clamped to at least the body radius
    SOLTEST_API FVector3d GravityAcceleration(const FVector3d& shipPositionM, TConstArrayView<FVector3d> bodyPositionsM,
        TConstArrayView<double> bodyGMs, TConstArrayView<double> bodyRadiiM);

    // Returns currentCap * 10^(wheelSteps / SpeedCapStepsPerDecade), clamped to [MinSpeedCapMps, MaxSpeedMps]
    SOLTEST_API double StepSpeedCap(double currentCapMps, int32 wheelSteps, const FSOLFlightParams& params);

    // Maps a virtual-joystick offset (X right, Y up) to (X = yaw, Y = pitch) with a dead zone and response exponent
    SOLTEST_API FVector2d JoystickToRotation(const FVector2d& offsetNorm, double deadZone, double exponent);

    // Pushes an overlapping ship out of a body sphere and removes its inward relative velocity; returns true on contact
    SOLTEST_API bool ResolveSphereCollision(FSOLShipState& inOutState, const FVector3d& bodyPositionM,
        const FVector3d& bodyVelocityMps, double bodyRadiusM, double shipRadiusM);

    // Returns a ship state on a circular orbit at the given altitude, on the sun-facing side of the body
    SOLTEST_API FSOLShipState MakeCircularOrbitState(const FVector3d& bodyPositionM, const FVector3d& bodyVelocityMps,
        double bodyGM, double bodyRadiusM, double altitudeM, const FVector3d& towardSunDir);

    // Returns the extra displacement from the reference frame moving more than refVelocity * realDt (time-warp carry)
    SOLTEST_API FVector3d FrameCarryDisplacement(const FVector3d& refPositionPrevM, const FVector3d& refPositionNowM,
        const FVector3d& refVelocityMps, double realDt);

    // Returns a body's position at a substep: endPosition - velocity * warpFactor * remainingRealSeconds
    SOLTEST_API FVector3d BodyPositionAtSubstep(const FVector3d& endPositionM, const FVector3d& velocityMps,
        double warpFactor, double remainingRealSeconds);

    // Swept sphere collision in the body's frame; places a tunnelling or entering ship at the entry point; true on contact
    SOLTEST_API bool ResolveSweptSphereCollision(const FSOLShipState& before, FSOLShipState& inOutAfter,
        const FVector3d& bodyPositionBeforeM, const FVector3d& bodyPositionAfterM, const FVector3d& bodyVelocityMps,
        double bodyRadiusM, double shipRadiusM);
}
