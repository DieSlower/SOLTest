/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

// Kinematic state of the ship in the Unreal-handed universe frame (meters, m/s)
struct FSOLShipState
{
    FVector3d PositionM = FVector3d::ZeroVector;
    FVector3d VelocityMps = FVector3d::ZeroVector;
    FQuat4d Orientation = FQuat4d::Identity;                    // ship-local -> universe frame
    FVector3d AngularVelocityRadPerS = FVector3d::ZeroVector;   // ship-local axes: X roll, Y pitch, Z yaw
};

// Tunable flight-model parameters
struct FSOLFlightParams
{
    double AssistTimeConstantS = 1.5;
    double NewtonianMainAccelMps2 = 50.0;                   // forward/back
    double NewtonianStrafeAccelMps2 = 30.0;                 // right/left and up/down
    double BoostAccelMultiplier = 3.0;
    double BoostSpeedMultiplier = 2.0;
    double MaxPitchYawRateRadPerS = 1.5707963267948966;     // 90 deg/s
    double MaxRollRateRadPerS = 2.0943951023931953;         // 120 deg/s
    double AngularResponseTimeS = 0.15;
    double MaxSpeedMps = SOL::MAX_SPEED_CAP_MPS;            // 0.5c
    double MinSpeedCapMps = SOL::MIN_SPEED_CAP_MPS;         // 1 m/s
    double ShipRadiusM = 10.0;
    double AlignTimeConstantS = SOL::SURFACE_LOCK_ALIGN_TIME_CONSTANT_S;    // Surface-lock up alignment (real seconds)
};

// Player (or AI) control input for one flight step
struct FSOLShipControl
{
    FVector3d Thrust = FVector3d::ZeroVector;       // local X forward, Y right, Z up, each in [-1, 1]; clamped to unit length
    FVector3d Rotation = FVector3d::ZeroVector;     // X roll, Y pitch, Z yaw, each in [-1, 1]
    bool bBoost = false;
    bool bFlightAssist = true;
    double SpeedCapMps = 1000.0;                    // relative to the reference frame
    FVector3d ReferenceVelocityMps = FVector3d::ZeroVector;
};

namespace SOLFlight
{
    constexpr double SpeedCapStepsPerDecade = 10.0;

    // Advances the ship by dt seconds with quaternion rotation and assist or Newtonian translation
    SOLTEST_API FSOLShipState Step(const FSOLShipState& state, const FSOLShipControl& control,
        const FSOLFlightParams& params, const FVector3d& gravityAccelMps2, double dt);

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
