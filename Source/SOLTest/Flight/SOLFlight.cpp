/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlight.h"

namespace
{
    // Below this squared length the orbit tangent cross(+Z, towardSun) counts as parallel and falls back to +Y
    constexpr double FLIGHT_PARALLEL_CROSS_SIZE_SQUARED = 1.0e-24;

    // Below this squared relative travel (m^2, i.e. 0.1 mm) a swept collision degenerates to the end-point test
    constexpr double FLIGHT_MIN_SWEEP_TRAVEL_SQUARED = 1.0e-8;

    // LevelForward keeps the current forward when its level projection is at most this long (nose along the up)
    constexpr double FLIGHT_MIN_LEVEL_PROJECTION = 1.0e-6;

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of v is finite
    bool FlightIsFinite(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns v scaled down to maxLength if it is longer, else v unchanged
    FVector3d FlightClampLength(const FVector3d& v, const double maxLength)
    {
        const double sizeSquared = v.SizeSquared();
        return sizeSquared > maxLength * maxLength ? v * (maxLength / FMath::Sqrt(sizeSquared)) : v;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns each component of v clamped to [-1, 1], with non-finite components as 0
    FVector3d FlightClampUnitComponents(const FVector3d& v)
    {
        // Returns one component clamped to [-1, 1], or 0 when non-finite
        auto clampOne = [](const double value)
        {
            return FMath::IsFinite(value) ? FMath::Clamp(value, -1.0, 1.0) : 0.0;
        };
        return FVector3d(clampOne(v.X), clampOne(v.Y), clampOne(v.Z));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the assisted thrust request: gravity compensation first, then as much velocity hold as the limit allows
FVector3d SOLFlight::AssistThrustAccelMps2(const FVector3d& velocityMps, const FVector3d& targetVelocityMps,
    const FVector3d& gravityAccelMps2, const double timeConstantS, const double dt, const double limitMps2)
{
    if (!FlightIsFinite(velocityMps) || !FlightIsFinite(targetVelocityMps) || !FlightIsFinite(gravityAccelMps2)
        || FMath::IsNaN(limitMps2) || limitMps2 <= 0.0)
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d hold = SOLNewtonian::VelocityHoldAccelMps2(velocityMps, targetVelocityMps, FVector3d::ZeroVector,
        timeConstantS, dt);
    const FVector3d compensation = -gravityAccelMps2;
    if (!FMath::IsFinite(limitMps2))
    {
        return compensation + hold;
    }
    const double compensationSize = compensation.Size();
    if (compensationSize >= limitMps2)
    {
        return compensation * (limitMps2 / compensationSize);
    }

    // Largest s in [0, 1] with |c + s h| <= L: the positive root of |h|^2 s^2 + 2 (c.h) s + |c|^2 - L^2 = 0
    if ((compensation + hold).SizeSquared() <= limitMps2 * limitMps2)
    {
        return compensation + hold;
    }
    const double a = hold.SizeSquared();
    const double halfB = FVector3d::DotProduct(compensation, hold);
    const double c = (compensationSize - limitMps2) * (compensationSize + limitMps2);
    const double root = (-halfB + FMath::Sqrt(FMath::Max(halfB * halfB - a * c, 0.0))) / a;
    return compensation + hold * FMath::Clamp(root, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Advances the ship by dt in assisted (rate command, velocity hold at the pilot limit) or manual (torque, thrust)
// mode on the Newtonian core, gravity included in both
FSOLShipState SOLFlight::Step(const FSOLShipState& state, const FSOLShipControl& control,
    const FSOLFlightParams& params, const FVector3d& gravityAccelMps2, const double dt,
    FSOLFlightStepOutput* outStep)
{
    if (outStep != nullptr)
    {
        *outStep = FSOLFlightStepOutput();
    }
    if (!FMath::IsFinite(dt) || dt <= 0.0)
    {
        return state;
    }
    const FSOLThrustEnvelope& env = params.Envelope;
    const FVector3d rotation = FlightClampUnitComponents(control.Rotation);
    const FQuat4d& q = state.Orientation;

    FSOLNewtonianCommand command;
    command.bBoost = control.bBoost;
    if (control.bFlightAssist)
    {
        // Assisted rotation: rate command per axis; the attitude target (surface-lock) flies the axes left free
        FVector3d rateCommand = rotation * env.MaxAngularRateRadPerS;
        if (FlightIsFinite(control.AttitudeUpWorld) && !control.AttitudeUpWorld.IsZero())
        {
            const FVector3d attitude = SOLNewtonian::AttitudeRateCommand(q,
                LevelForward(q, control.AttitudeUpWorld), control.AttitudeUpWorld, env);
            for (int32 axis = 0; axis < 3; ++axis)
            {
                rateCommand[axis] = rotation[axis] == 0.0 ? attitude[axis] : rateCommand[axis];
            }
        }
        if (control.bStopSpin)
        {
            rateCommand = FVector3d::ZeroVector;
        }
        command.AngularAccelFraction = SOLNewtonian::RateCommandAngularFraction(state.AngularVelocityRadPerS,
            rateCommand, env, dt);

        // Assisted translation: hold reference + thrust * cap, spending at most the pilot's sustained limit
        const FVector3d thrust = FlightClampLength(FlightClampUnitComponents(control.Thrust), 1.0);
        const double boostSpeed = control.bBoost ? params.BoostSpeedMultiplier : 1.0;
        const FVector3d relativeTarget = FlightClampLength(q.RotateVector(thrust) * (control.SpeedCapMps * boostSpeed),
            params.MaxSpeedMps);
        const FVector3d request = AssistThrustAccelMps2(state.VelocityMps,
            control.ReferenceVelocityMps + relativeTarget, gravityAccelMps2, params.AssistTimeConstantS, dt,
            SOLNewtonian::PilotSustainedAccelMps2(params.PilotLimits));
        command.ThrustFraction = SOLNewtonian::AllocateThrust(request, q, env, control.bBoost).ThrustFraction;
    }
    else
    {
        // Manual: torque and thrust fractions straight to the thrusters; stop spin brakes the rotation
        command.AngularAccelFraction = control.bStopSpin
            ? SOLNewtonian::RateCommandAngularFraction(state.AngularVelocityRadPerS, FVector3d::ZeroVector, env, dt)
            : rotation;
        command.ThrustFraction = FlightClampUnitComponents(control.Thrust);
    }

    if (outStep != nullptr)
    {
        outStep->ThrustAccelWorldMps2 = q.RotateVector(
            SOLNewtonian::LocalThrustAccelMps2(command.ThrustFraction, env, command.bBoost));
    }
    return SOLNewtonian::StepNewtonian(state, command, env, gravityAccelMps2, dt);
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship's forward projected level to the plane perpendicular to up (the forward itself if degenerate)
FVector3d SOLFlight::LevelForward(const FQuat4d& orientation, const FVector3d& upWorld)
{
    const FVector3d forward = orientation.GetForwardVector();
    if (!FlightIsFinite(upWorld) || upWorld.IsZero())
    {
        return forward;
    }
    const FVector3d up = upWorld.GetUnsafeNormal();
    const double forwardAlongUp = FVector3d::DotProduct(forward, up);
    const FVector3d level = forward - up * forwardAlongUp;
    const double size = level.Size();
    if (size > FLIGHT_MIN_LEVEL_PROJECTION)
    {
        return level / size;
    }

    // Nose along the up: level out by the shortest pitch, toward minus the ship's up (nose up) or plus it (nose down)
    const FVector3d shipUp = orientation.GetUpVector() * (forwardAlongUp > 0.0 ? -1.0 : 1.0);
    const FVector3d pitchLevel = shipUp - up * FVector3d::DotProduct(shipUp, up);
    const double pitchSize = pitchLevel.Size();
    return pitchSize > FLIGHT_MIN_LEVEL_PROJECTION ? pitchLevel / pitchSize : forward;
}

//////////////////////////////////////////////////////////////////////////
// Returns the contact (normal) acceleration of a ship resting on a surface: what presses it into the ground
FVector3d SOLFlight::ContactAccelMps2(const FVector3d& outwardNormal, const FVector3d& bodyGravityAccelMps2,
    const FVector3d& thrustAccelWorldMps2)
{
    if (!FlightIsFinite(outwardNormal) || !FlightIsFinite(bodyGravityAccelMps2) || !FlightIsFinite(thrustAccelWorldMps2)
        || outwardNormal.IsZero())
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d normal = outwardNormal.GetUnsafeNormal();
    const double press = -FVector3d::DotProduct(bodyGravityAccelMps2 + thrustAccelWorldMps2, normal);
    return normal * FMath::Max(press, 0.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of sim-time substeps the autopilot uses for one frame of sim time (0 for invalid input)
int32 SOLFlight::AutopilotSubstepCount(const double frameSimS)
{
    if (!FMath::IsFinite(frameSimS) || frameSimS <= 0.0)
    {
        return 0;
    }
    const double substepS = FMath::Max(SOL::AUTOPILOT_MIN_SUBSTEP_S, frameSimS / SOL::AUTOPILOT_MAX_SUBSTEPS);
    return FMath::Clamp(FMath::CeilToInt32(frameSimS / substepS), 1, SOL::AUTOPILOT_MAX_SUBSTEPS);
}

//////////////////////////////////////////////////////////////////////////
// Advances the ship by dt under the rendezvous autopilot (flip-and-burn at the pilot's autopilot limit)
FSOLShipState SOLFlight::AutopilotStep(const FSOLShipState& state, const FVector3d& targetPositionM,
    const FVector3d& targetVelocityMps, const FVector3d& targetAccelMps2, const FVector3d& gravityAccelMps2,
    const FSOLFlightParams& params, const FSOLGMeterState& meter, const double dt, FSOLFlightStepOutput* outStep,
    FSOLGuidanceOutput* outGuidance, const double targetGravityMps2, const double passSpeedMps,
    const double captureRadiusM)
{
    if (outStep != nullptr)
    {
        *outStep = FSOLFlightStepOutput();
    }
    if (outGuidance != nullptr)
    {
        *outGuidance = FSOLGuidanceOutput();
    }
    if (!FMath::IsFinite(dt) || dt <= 0.0)
    {
        return state;
    }
    const FSOLThrustEnvelope& env = params.Envelope;

    // Guidance planned at the pilot's autopilot limit (sustained, or the recovery limit while the meter is out), with
    // the stronger of the gravity here and at the target, so braking into a deep well is planned for the well's
    // bottom (SDD 18 Amendment 7)
    const double limitMps2 = SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, meter);
    const double planGravityMps2 = FMath::Max(gravityAccelMps2.Size(),
        FMath::IsFinite(targetGravityMps2) ? targetGravityMps2 : 0.0);
    FSOLGuidanceParams guidanceParams = SOLNewtonian::MakeGuidanceParams(env, limitMps2, planGravityMps2, true, false);
    guidanceParams.PassSpeedMps = passSpeedMps;
    guidanceParams.CaptureRadiusM = captureRadiusM;
    const FSOLGuidanceOutput guidance = SOLNewtonian::ComputeRendezvousGuidance(targetPositionM - state.PositionM,
        state.VelocityMps - targetVelocityMps, targetAccelMps2, gravityAccelMps2, guidanceParams, dt);

    // Long sim-time substeps (user warp) cannot integrate a rate-limited turn: the nose is set straight to the
    // guidance's forward with no spin; otherwise it follows the guidance through the rate command
    FSOLShipState start = state;
    FSOLNewtonianCommand command;
    if (dt > SOL::AUTOPILOT_KINEMATIC_ATTITUDE_S && !guidance.DesiredForward.IsZero())
    {
        start.Orientation = SOLNewtonian::OrientationFromForward(guidance.DesiredForward,
            state.Orientation.GetUpVector(), state.Orientation);
        start.AngularVelocityRadPerS = FVector3d::ZeroVector;
    }
    else
    {
        const FVector3d rateCommand = SOLNewtonian::AttitudeRateCommand(state.Orientation, guidance.DesiredForward,
            FVector3d::ZeroVector, env);
        command.AngularAccelFraction = SOLNewtonian::RateCommandAngularFraction(state.AngularVelocityRadPerS,
            rateCommand, env, dt);
    }

    // Thrust within the pilot limit and the envelope
    command.ThrustFraction = SOLNewtonian::AllocateThrust(
        SOLNewtonian::ClampToPilotLimit(guidance.DesiredThrustAccelMps2, limitMps2), start.Orientation, env, false)
        .ThrustFraction;

    if (outStep != nullptr)
    {
        outStep->ThrustAccelWorldMps2 = start.Orientation.RotateVector(
            SOLNewtonian::LocalThrustAccelMps2(command.ThrustFraction, env, false));
    }
    if (outGuidance != nullptr)
    {
        *outGuidance = guidance;
    }
    return SOLNewtonian::StepNewtonian(start, command, env, gravityAccelMps2, dt);
}

//////////////////////////////////////////////////////////////////////////
// Returns the autopilot's arrival distance from a body's centre: the geometric standoff, moved out until the body's
// gravity there is at most AUTOPILOT_GRAVITY_STANDOFF_FRACTION of the autopilot limit (so the arrival can be held)
double SOLFlight::AutopilotStandoffM(const double bodyRadiusM, const double bodyGM, const double accelLimitMps2)
{
    const double radiusM = FMath::IsFinite(bodyRadiusM) && bodyRadiusM > 0.0 ? bodyRadiusM : 0.0;
    const double geometricM = radiusM * (1.0 + SOL::AUTOPILOT_BODY_STANDOFF_RADII) + SOL::AUTOPILOT_BODY_STANDOFF_M;
    if (!FMath::IsFinite(bodyGM) || bodyGM <= 0.0 || !FMath::IsFinite(accelLimitMps2) || accelLimitMps2 <= 0.0)
    {
        return geometricM;
    }
    return FMath::Max(geometricM, FMath::Sqrt(bodyGM / (SOL::AUTOPILOT_GRAVITY_STANDOFF_FRACTION * accelLimitMps2)));
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the autopilot flies under a warp too high for precise gravity near a body (the HUD hint)
bool SOLFlight::IsAutopilotPrecisionReduced(const double warpFactor, const double distanceToBodyCenterM,
    const double bodyRadiusM)
{
    if (!FMath::IsFinite(warpFactor) || !FMath::IsFinite(distanceToBodyCenterM) || !FMath::IsFinite(bodyRadiusM)
        || bodyRadiusM <= 0.0)
    {
        return false;
    }
    return warpFactor > SOL::AUTOPILOT_HINT_WARP && distanceToBodyCenterM < SOL::AUTOPILOT_HINT_RADII * bodyRadiusM;
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
