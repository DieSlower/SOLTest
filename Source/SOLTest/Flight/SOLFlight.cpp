/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlight.h"

namespace
{
    // Below this rotation angle (radians) the rotation-vector quaternion uses its small-angle limit sin(a/2)/a -> 1/2
    constexpr double FLIGHT_SMALL_ROTATION_ANGLE_RAD = 1.0e-8;

    // Guard for divisions by a time constant that a caller may set to zero
    constexpr double FLIGHT_MIN_TIME_CONSTANT_S = UE_DOUBLE_SMALL_NUMBER;

    // Below this squared length the orbit tangent cross(+Z, towardSun) counts as parallel and falls back to +Y
    constexpr double FLIGHT_PARALLEL_CROSS_SIZE_SQUARED = 1.0e-24;

    // Below this squared relative travel (m^2, i.e. 0.1 mm) a swept collision degenerates to the end-point test
    constexpr double FLIGHT_MIN_SWEEP_TRAVEL_SQUARED = 1.0e-8;

    //////////////////////////////////////////////////////////////////////////
    // Returns the fraction of the gap that remains after dt of exponential relaxation with time constant tau
    double FlightRemainingFraction(const double dt, const double tau)
    {
        return FMath::Exp(-dt / FMath::Max(tau, FLIGHT_MIN_TIME_CONSTANT_S));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns v scaled down to maxLength if it is longer, else v unchanged
    FVector3d FlightClampLength(const FVector3d& v, const double maxLength)
    {
        const double sizeSquared = v.SizeSquared();
        return sizeSquared > maxLength * maxLength ? v * (maxLength / FMath::Sqrt(sizeSquared)) : v;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns each component of v clamped to [-1, 1]
    FVector3d FlightClampUnitComponents(const FVector3d& v)
    {
        return FVector3d(FMath::Clamp(v.X, -1.0, 1.0), FMath::Clamp(v.Y, -1.0, 1.0), FMath::Clamp(v.Z, -1.0, 1.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the quaternion rotating by |rotationVector| radians about its direction (standard right-hand quaternion math)
    FQuat4d FlightQuatFromRotationVector(const FVector3d& rotationVector)
    {
        const double angle = rotationVector.Size();
        const double halfAngle = 0.5 * angle;
        const double sinHalfOverAngle = angle > FLIGHT_SMALL_ROTATION_ANGLE_RAD ? FMath::Sin(halfAngle) / angle : 0.5;
        return FQuat4d(rotationVector.X * sinHalfOverAngle, rotationVector.Y * sinHalfOverAngle,
            rotationVector.Z * sinHalfOverAngle, FMath::Cos(halfAngle));
    }
}

//////////////////////////////////////////////////////////////////////////
// Advances the ship by dt seconds with quaternion rotation and assist or Newtonian translation
FSOLShipState SOLFlight::Step(const FSOLShipState& state, const FSOLShipControl& control,
    const FSOLFlightParams& params, const FVector3d& gravityAccelMps2, const double dt)
{
    FSOLShipState result = state;

    // Sanitize the inputs: each component in [-1, 1], thrust also clamped to unit length overall
    const FVector3d thrust = FlightClampLength(FlightClampUnitComponents(control.Thrust), 1.0);
    const FVector3d rotation = FlightClampUnitComponents(control.Rotation);

    // Rotation (both modes): relax the local rate toward the command, then turn by rate * dt about the local axes.
    // Standard quaternion math is right-handed, so roll and pitch are negated to get Unreal's left-handed conventions
    // (+pitch: forward gains +up, +roll: up gains +right); +yaw about +Z already turns forward toward +right.
    const FVector3d commandedRate(rotation.X * params.MaxRollRateRadPerS, rotation.Y * params.MaxPitchYawRateRadPerS,
        rotation.Z * params.MaxPitchYawRateRadPerS);
    const double rateRemaining = FlightRemainingFraction(dt, params.AngularResponseTimeS);
    const FVector3d rate = commandedRate + (state.AngularVelocityRadPerS - commandedRate) * rateRemaining;
    const FVector3d localRotation = FVector3d(-rate.X, -rate.Y, rate.Z) * dt;
    result.AngularVelocityRadPerS = rate;
    result.Orientation = (state.Orientation * FlightQuatFromRotationVector(localRotation)).GetNormalized();

    // Translation uses the orientation at the start of the step
    const double boostSpeed = control.bBoost ? params.BoostSpeedMultiplier : 1.0;
    const double boostAccel = control.bBoost ? params.BoostAccelMultiplier : 1.0;
    FVector3d velocity;
    if (control.bFlightAssist)
    {
        // Assist: exact exponential relaxation toward reference + thrust * cap (relative target clamped to max speed)
        const FVector3d relativeTarget = FlightClampLength(
            state.Orientation.RotateVector(thrust) * (control.SpeedCapMps * boostSpeed), params.MaxSpeedMps);
        const FVector3d target = control.ReferenceVelocityMps + relativeTarget;
        const double remaining = FlightRemainingFraction(dt, params.AssistTimeConstantS / boostAccel);
        velocity = target + (state.VelocityMps - target) * remaining;
    }
    else
    {
        // Newtonian: fixed thruster accelerations along the ship axes plus gravity, no damping
        const FVector3d localAccel = FVector3d(thrust.X * params.NewtonianMainAccelMps2,
            thrust.Y * params.NewtonianStrafeAccelMps2, thrust.Z * params.NewtonianStrafeAccelMps2) * boostAccel;
        velocity = state.VelocityMps + (state.Orientation.RotateVector(localAccel) + gravityAccelMps2) * dt;
    }

    // Absolute speed limit, then semi-implicit Euler position update
    result.VelocityMps = FlightClampLength(velocity, params.MaxSpeedMps);
    result.PositionM = state.PositionM + result.VelocityMps * dt;
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the summed gravitational acceleration toward every body, with r clamped to at least the body radius
FVector3d SOLFlight::GravityAcceleration(const FVector3d& shipPositionM, TConstArrayView<FVector3d> bodyPositionsM,
    TConstArrayView<double> bodyGMs, TConstArrayView<double> bodyRadiiM)
{
    const int32 bodyCount = bodyPositionsM.Num();
    checkSlow(bodyGMs.Num() == bodyCount && bodyRadiiM.Num() == bodyCount);

    // GM / max(r, R)^2 along the unit vector toward each body; a ship exactly at a center gets nothing from that body
    FVector3d total = FVector3d::ZeroVector;
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const FVector3d delta = bodyPositionsM[index] - shipPositionM;
        const double distance = delta.Size();
        const double clampedDistance = FMath::Max(distance, bodyRadiiM[index]);
        const double scale = distance > 0.0 ? bodyGMs[index] / (clampedDistance * clampedDistance * distance) : 0.0;
        total += delta * scale;
    }
    return total;
}

//////////////////////////////////////////////////////////////////////////
// Returns currentCap * 10^(wheelSteps / SpeedCapStepsPerDecade), clamped to [MinSpeedCapMps, MaxSpeedMps]
double SOLFlight::StepSpeedCap(const double currentCapMps, const int32 wheelSteps, const FSOLFlightParams& params)
{
    const double stepped = currentCapMps * FMath::Pow(10.0, wheelSteps / SpeedCapStepsPerDecade);
    return FMath::Clamp(stepped, params.MinSpeedCapMps, params.MaxSpeedMps);
}

//////////////////////////////////////////////////////////////////////////
// Maps a virtual-joystick offset (X right, Y up) to (X = yaw, Y = pitch) with a dead zone and response exponent
FVector2d SOLFlight::JoystickToRotation(const FVector2d& offsetNorm, const double deadZone, const double exponent)
{
    const double length = offsetNorm.Size();
    const double magnitude = FMath::Min(length, 1.0);
    if (magnitude <= deadZone)
    {
        return FVector2d::ZeroVector;
    }
    const double shaped = FMath::Pow((magnitude - deadZone) / (1.0 - deadZone), exponent);
    return offsetNorm * (shaped / length);
}

//////////////////////////////////////////////////////////////////////////
// Pushes an overlapping ship out of a body sphere and removes its inward relative velocity; returns true on contact
bool SOLFlight::ResolveSphereCollision(FSOLShipState& inOutState, const FVector3d& bodyPositionM,
    const FVector3d& bodyVelocityMps, const double bodyRadiusM, const double shipRadiusM)
{
    const FVector3d offset = inOutState.PositionM - bodyPositionM;
    const double contactDistance = bodyRadiusM + shipRadiusM;
    const double distanceSquared = offset.SizeSquared();
    if (distanceSquared >= contactDistance * contactDistance)
    {
        return false;
    }

    // Outward normal (+Z for a ship exactly at the center), then clamp the position to the contact distance
    const double distance = FMath::Sqrt(distanceSquared);
    const FVector3d normal = distance > 0.0 ? offset / distance : FVector3d(0.0, 0.0, 1.0);
    inOutState.PositionM = bodyPositionM + normal * contactDistance;

    // Remove only the inward part of the velocity relative to the body, so the ship keeps sliding tangentially
    const double normalSpeed = FVector3d::DotProduct(inOutState.VelocityMps - bodyVelocityMps, normal);
    inOutState.VelocityMps -= normal * FMath::Min(normalSpeed, 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns a ship state on a circular orbit at the given altitude, on the sun-facing side of the body
FSOLShipState SOLFlight::MakeCircularOrbitState(const FVector3d& bodyPositionM, const FVector3d& bodyVelocityMps,
    const double bodyGM, const double bodyRadiusM, const double altitudeM, const FVector3d& towardSunDir)
{
    const FVector3d radialDir = towardSunDir.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d(1.0, 0.0, 0.0));
    const double orbitRadius = bodyRadiusM + altitudeM;

    // Tangent along cross(+Z, radial), or +Y when the radial direction is (anti)parallel to +Z
    const FVector3d cross = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), radialDir);
    const FVector3d tangent = cross.SizeSquared() < FLIGHT_PARALLEL_CROSS_SIZE_SQUARED
        ? FVector3d(0.0, 1.0, 0.0) : cross.GetUnsafeNormal();

    // Orientation from forward = tangent, up = radial, right = up x forward (Unreal's left-handed basis)
    const FVector3d right = FVector3d::CrossProduct(radialDir, tangent);
    const FMatrix44d basis(tangent, right, radialDir, FVector3d::ZeroVector);

    FSOLShipState state;
    state.PositionM = bodyPositionM + radialDir * orbitRadius;
    state.VelocityMps = bodyVelocityMps + tangent * FMath::Sqrt(bodyGM / orbitRadius);
    state.Orientation = FQuat4d(basis).GetNormalized();
    return state;
}

//////////////////////////////////////////////////////////////////////////
// Returns the extra displacement from the reference frame moving more than refVelocity * realDt (time-warp carry)
FVector3d SOLFlight::FrameCarryDisplacement(const FVector3d& refPositionPrevM, const FVector3d& refPositionNowM,
    const FVector3d& refVelocityMps, const double realDt)
{
    return (refPositionNowM - refPositionPrevM) - refVelocityMps * realDt;
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's position at a substep: endPosition - velocity * warpFactor * remainingRealSeconds
FVector3d SOLFlight::BodyPositionAtSubstep(const FVector3d& endPositionM, const FVector3d& velocityMps,
    const double warpFactor, const double remainingRealSeconds)
{
    return endPositionM - velocityMps * (warpFactor * remainingRealSeconds);
}

//////////////////////////////////////////////////////////////////////////
// Swept sphere collision in the body's frame; places a tunnelling or entering ship at the entry point; true on contact
bool SOLFlight::ResolveSweptSphereCollision(const FSOLShipState& before, FSOLShipState& inOutAfter,
    const FVector3d& bodyPositionBeforeM, const FVector3d& bodyPositionAfterM, const FVector3d& bodyVelocityMps,
    const double bodyRadiusM, const double shipRadiusM)
{
    // The segment in the body's frame, from small relative vectors (never from the huge absolute coordinates)
    const FVector3d startRel = before.PositionM - bodyPositionBeforeM;
    const FVector3d endRel = inOutAfter.PositionM - bodyPositionAfterM;
    const FVector3d delta = endRel - startRel;
    const double contactRadius = bodyRadiusM + shipRadiusM;
    const double startDistance = startRel.Size();
    const double travelSquared = delta.SizeSquared();

    // Started inside, or (almost) no relative travel: nothing to sweep, resolve the end point like the plain version
    if (startDistance < contactRadius || travelSquared <= FLIGHT_MIN_SWEEP_TRAVEL_SQUARED)
    {
        return ResolveSphereCollision(inOutAfter, bodyPositionAfterM, bodyVelocityMps, bodyRadiusM, shipRadiusM);
    }

    // Entry needs the segment heading inward and passing strictly closer than the contact radius
    const double halfB = FVector3d::DotProduct(startRel, delta);
    const FVector3d closestOffset = startRel - delta * (halfB / travelSquared);
    const double closestDistance = closestOffset.Size();
    if (halfB >= 0.0 || closestDistance >= contactRadius)
    {
        return ResolveSphereCollision(inOutAfter, bodyPositionAfterM, bodyVelocityMps, bodyRadiusM, shipRadiusM);
    }

    // Smaller root of |startRel + delta t| = contactRadius in the cancellation-free form t = c / q, where
    // c = (|s| - R)(|s| + R) >= 0 and q = -halfB + sqrt(travel^2 * (R - h)(R + h)) > 0
    const double c = (startDistance - contactRadius) * (startDistance + contactRadius);
    const double rootDiscriminant = FMath::Sqrt(travelSquared
        * ((contactRadius - closestDistance) * (contactRadius + closestDistance)));
    const double entryT = c / (-halfB + rootDiscriminant);
    if (entryT > 1.0)
    {
        return false;
    }

    // Place the ship on the contact sphere at the entry direction, around where the body ends the step
    const FVector3d entryRel = startRel + delta * entryT;
    const double entryDistance = entryRel.Size();
    const FVector3d normal = entryDistance > 0.0 ? entryRel / entryDistance : FVector3d(0.0, 0.0, 1.0);
    inOutAfter.PositionM = bodyPositionAfterM + normal * contactRadius;

    // Remove only the inward part of the post-step velocity relative to the body, keeping the tangential slide
    const double normalSpeed = FVector3d::DotProduct(inOutAfter.VelocityMps - bodyVelocityMps, normal);
    inOutAfter.VelocityMps -= normal * FMath::Min(normalSpeed, 0.0);
    return true;
}
