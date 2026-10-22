/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLNewtonianFlight.h"

#include <limits>

namespace
{
    // Below this rotation angle (radians) the rotation-vector quaternion uses its small-angle limit sin(a/2)/a -> 1/2
    constexpr double NEWTONIAN_SMALL_ROTATION_ANGLE_RAD = 1.0e-8;

    // An attitude error larger than PI minus this (radians) counts as "target exactly behind" and turns by pure pitch
    constexpr double NEWTONIAN_BEHIND_ANGLE_MARGIN_RAD = 1.0e-6;

    // AllocateThrust reports saturation when its scale is below 1 minus this
    constexpr double NEWTONIAN_SATURATION_TOLERANCE = 1.0e-12;

    // Guidance Brake phase starts once the closing speed reaches the braking curve minus this relative margin
    constexpr double NEWTONIAN_BRAKE_CURVE_MARGIN = 1.0e-6;

    // Guidance Cruise phase starts once the closing speed reaches this fraction of the cruise cap
    constexpr double NEWTONIAN_CRUISE_FRACTION = 0.99;

    // OrientationFromForward treats a normalized up hint's perpendicular part at or below this length as degenerate
    constexpr double NEWTONIAN_DEGENERATE_HINT_LENGTH = 1.0e-6;

    // Upper bound on the G-meter phase changes (Normal -> G-LOC -> Normal) processed inside one step
    constexpr int32 NEWTONIAN_MAX_METER_PHASES = 256;

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of v is finite
    bool NewtonianIsFinite(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of q is finite
    bool NewtonianIsFinite(const FQuat4d& q)
    {
        return FMath::IsFinite(q.X) && FMath::IsFinite(q.Y) && FMath::IsFinite(q.Z) && FMath::IsFinite(q.W);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns value, or 0 when it is non-finite
    double NewtonianFiniteOrZero(const double value)
    {
        return FMath::IsFinite(value) ? value : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns v with every non-finite component replaced by 0
    FVector3d NewtonianFiniteOrZero(const FVector3d& v)
    {
        return FVector3d(NewtonianFiniteOrZero(v.X), NewtonianFiniteOrZero(v.Y), NewtonianFiniteOrZero(v.Z));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns v with non-finite components zeroed, then each component clamped to [-1, 1]
    FVector3d NewtonianSanitizeFraction(const FVector3d& v)
    {
        const FVector3d finite = NewtonianFiniteOrZero(v);
        return FVector3d(FMath::Clamp(finite.X, -1.0, 1.0), FMath::Clamp(finite.Y, -1.0, 1.0),
            FMath::Clamp(finite.Z, -1.0, 1.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns v scaled down to maxLength if it is longer, else v unchanged
    FVector3d NewtonianClampLength(const FVector3d& v, const double maxLength)
    {
        const double sizeSquared = v.SizeSquared();
        return sizeSquared > maxLength * maxLength ? v * (maxLength / FMath::Sqrt(sizeSquared)) : v;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the cap when it is finite and positive, else +inf (no cap)
    double NewtonianCapOrInfinity(const double cap)
    {
        return FMath::IsFinite(cap) && cap > 0.0 ? cap : std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the per-axis acceleration limits for a local demand (forward or reverse on X), times the boost
    FVector3d NewtonianAxisLimits(const FVector3d& localDemand, const FSOLThrustEnvelope& env, const bool bBoost)
    {
        const double multiplier = bBoost ? env.BoostMultiplier : 1.0;
        const double xLimit = localDemand.X >= 0.0 ? env.ForwardAccelMps2 : env.ReverseAccelMps2;
        return FVector3d(xLimit, env.LateralAccelMps2, env.VerticalAccelMps2) * multiplier;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the quaternion rotating by |rotationVector| radians about its direction (standard right-hand math)
    FQuat4d NewtonianQuatFromRotationVector(const FVector3d& rotationVector)
    {
        const double angle = rotationVector.Size();
        const double halfAngle = 0.5 * angle;
        const double sinHalfOverAngle = angle > NEWTONIAN_SMALL_ROTATION_ANGLE_RAD
            ? FMath::Sin(halfAngle) / angle : 0.5;
        return FQuat4d(rotationVector.X * sinHalfOverAngle, rotationVector.Y * sinHalfOverAngle,
            rotationVector.Z * sinHalfOverAngle, FMath::Cos(halfAngle));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rate command for one axis error: sign(e) * min(rate, sqrt(2 * 0.8 * alpha * |e|), 4 * |e|)
    double NewtonianAxisAttitudeRate(const double error, const double maxRate, const double alpha)
    {
        if (!(alpha > 0.0) || error == 0.0)
        {
            return 0.0;
        }
        const double magnitude = FMath::Abs(error);
        const double brakingRate = FMath::Sqrt(2.0 * SOLNewtonian::ATTITUDE_BRAKE_FRACTION * alpha * magnitude);
        const double rate = FMath::Min3(FMath::Max(maxRate, 0.0), brakingRate,
            SOLNewtonian::ATTITUDE_GAIN_PER_S * magnitude);
        return error > 0.0 ? rate : -rate;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the meter advanced at rate r for t seconds, clamped to [0, 1] (t <= 0 leaves it unchanged)
    double NewtonianAdvanceMeter(const double meter, const double rate, const double t)
    {
        return t > 0.0 ? FMath::Clamp(meter + rate * t, 0.0, 1.0) : meter;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every guidance param is finite
    bool NewtonianParamsFinite(const FSOLGuidanceParams& params)
    {
        return FMath::IsFinite(params.PlanAccelMps2) && FMath::IsFinite(params.BrakeAccelMps2)
            && FMath::IsFinite(params.FlipTimeS) && FMath::IsFinite(params.MaxCruiseSpeedMps)
            && FMath::IsFinite(params.ArrivalRadiusM) && FMath::IsFinite(params.ArrivalSpeedMps)
            && FMath::IsFinite(params.VelocityGainPerS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ETA from a valid, non-receding, not-too-fast state at or below the cap (contract cases e and f)
    double NewtonianPeakOrCruiseEta(const double distanceM, const double closingSpeedMps, const double accel,
        const double brake, const double flipTimeS, const double cap)
    {
        const double d = FMath::Max(distanceM, 0.0);
        const double vc = closingSpeedMps;

        // (e) Peak speed of the accelerate-coast-brake profile: v^2 * A + v * tf = C
        const double a = 1.0 / (2.0 * accel) + 1.0 / (2.0 * brake);
        const double c = d + vc * vc / (2.0 * accel);
        const double peak = 2.0 * c / (flipTimeS + FMath::Sqrt(flipTimeS * flipTimeS + 4.0 * a * c));
        if (peak <= cap)
        {
            return (peak - vc) / accel + flipTimeS + peak / brake;
        }

        // (f) Accelerate to the cap, cruise, coast the flip, brake
        const double cruiseDistance = d - (cap * cap - vc * vc) / (2.0 * accel) - cap * flipTimeS
            - cap * cap / (2.0 * brake);
        return (cap - vc) / accel + cruiseDistance / cap + flipTimeS + cap / brake;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the local thrust acceleration for the fractions (per-axis box, boost, then the total cap)
FVector3d SOLNewtonian::LocalThrustAccelMps2(const FVector3d& thrustFraction, const FSOLThrustEnvelope& env,
    const bool bBoost)
{
    const FVector3d fraction = NewtonianSanitizeFraction(thrustFraction);
    const double multiplier = bBoost ? env.BoostMultiplier : 1.0;
    const double x = fraction.X >= 0.0 ? fraction.X * env.ForwardAccelMps2 : fraction.X * env.ReverseAccelMps2;
    const FVector3d accel = FVector3d(x, fraction.Y * env.LateralAccelMps2, fraction.Z * env.VerticalAccelMps2)
        * multiplier;
    const double cap = env.MaxTotalAccelMps2;
    return FMath::IsFinite(cap) && cap > 0.0 ? NewtonianClampLength(accel, cap) : accel;
}

//////////////////////////////////////////////////////////////////////////
// Advances the state by dt with torque, thrust and gravity (semi-implicit Euler, no damping)
FSOLShipState SOLNewtonian::StepNewtonian(const FSOLShipState& state, const FSOLNewtonianCommand& command,
    const FSOLThrustEnvelope& env, const FVector3d& gravityAccelMps2, const double dt)
{
    if (!FMath::IsFinite(dt) || dt <= 0.0)
    {
        return state;
    }
    FSOLShipState result = state;

    // Angular velocity from the torque fraction, each component clamped to the spin limit
    const FVector3d torque = NewtonianSanitizeFraction(command.AngularAccelFraction);
    const FVector3d spin = state.AngularVelocityRadPerS + torque * env.MaxAngularAccelRadPerS2 * dt;
    const double spinLimit = FMath::Max(env.MaxSpinRateRadPerS, 0.0);
    result.AngularVelocityRadPerS = FVector3d(FMath::Clamp(spin.X, -spinLimit, spinLimit),
        FMath::Clamp(spin.Y, -spinLimit, spinLimit), FMath::Clamp(spin.Z, -spinLimit, spinLimit));

    // Velocity from thrust (start-of-step orientation) plus gravity, clamped to the speed limit; then position
    const FVector3d thrustWorld = state.Orientation.RotateVector(
        LocalThrustAccelMps2(command.ThrustFraction, env, command.bBoost));
    const FVector3d velocity = state.VelocityMps + (thrustWorld + NewtonianFiniteOrZero(gravityAccelMps2)) * dt;
    result.VelocityMps = NewtonianClampLength(velocity, NewtonianCapOrInfinity(env.MaxSpeedMps));
    result.PositionM = state.PositionM + result.VelocityMps * dt;

    // Orientation by the new rate. Standard quaternion math is right-handed, so roll and pitch are negated to get
    // the SDD 2 conventions (+pitch: forward gains +up, +roll: up gains +right); +yaw already turns toward +right.
    const FVector3d& w = result.AngularVelocityRadPerS;
    const FVector3d localRotation = FVector3d(-w.X, -w.Y, w.Z) * dt;
    result.Orientation = (state.Orientation * NewtonianQuatFromRotationVector(localRotation)).GetNormalized();
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the per-axis torque fraction that reaches the commanded rate this step (deadbeat, bounded)
FVector3d SOLNewtonian::RateCommandAngularFraction(const FVector3d& angularVelocityRadPerS,
    const FVector3d& commandRateRadPerS, const FSOLThrustEnvelope& env, const double dt)
{
    if (!FMath::IsFinite(dt) || dt <= 0.0 || !NewtonianIsFinite(angularVelocityRadPerS)
        || !NewtonianIsFinite(commandRateRadPerS))
    {
        return FVector3d::ZeroVector;
    }

    // Per axis: clamp the command to the rate limit, then the fraction that closes the gap in one step
    FVector3d fraction = FVector3d::ZeroVector;
    for (int32 axis = 0; axis < 3; ++axis)
    {
        const double limit = FMath::Max(env.MaxAngularRateRadPerS[axis], 0.0);
        const double command = FMath::Clamp(commandRateRadPerS[axis], -limit, limit);
        const double alpha = env.MaxAngularAccelRadPerS2[axis];
        fraction[axis] = alpha > 0.0
            ? FMath::Clamp((command - angularVelocityRadPerS[axis]) / (alpha * dt), -1.0, 1.0) : 0.0;
    }
    return fraction;
}

//////////////////////////////////////////////////////////////////////////
// Returns the rate command that turns the nose to the desired forward and the roll to the desired up
FVector3d SOLNewtonian::AttitudeRateCommand(const FQuat4d& orientation, const FVector3d& desiredForwardWorld,
    const FVector3d& desiredUpWorld, const FSOLThrustEnvelope& env)
{
    if (!NewtonianIsFinite(desiredForwardWorld) || !NewtonianIsFinite(orientation)
        || desiredForwardWorld.SizeSquared() <= 0.0)
    {
        return FVector3d::ZeroVector;
    }
    const FQuat4d q = orientation.GetNormalized();

    // Pitch and yaw from the shortest arc nose (+X) -> desired forward; cross(+X, f) = (0, -f.Z, f.Y) has no roll
    // part. In the SDD 2 conventions the standard rotation vector (0, -f.Z, f.Y) * angle / s maps to pitch +f.Z,
    // yaw +f.Y.
    const FVector3d f = q.UnrotateVector(desiredForwardWorld.GetUnsafeNormal());
    const double sinAngle = FMath::Sqrt(f.Y * f.Y + f.Z * f.Z);
    const double angle = FMath::Atan2(sinAngle, f.X);
    double pitchError = 0.0;
    double yawError = 0.0;
    if (angle > UE_DOUBLE_PI - NEWTONIAN_BEHIND_ANGLE_MARGIN_RAD)
    {
        pitchError = UE_DOUBLE_PI;
    }
    else if (sinAngle > 0.0)
    {
        pitchError = f.Z * angle / sinAngle;
        yawError = f.Y * angle / sinAngle;
    }

    // Roll: signed angle about the nose from local +Z to the up's perpendicular part (+roll moves up toward +Y)
    double rollError = 0.0;
    if (NewtonianIsFinite(desiredUpWorld) && desiredUpWorld.SizeSquared() > 0.0)
    {
        const FVector3d u = q.UnrotateVector(desiredUpWorld.GetUnsafeNormal());
        if (FMath::Sqrt(u.Y * u.Y + u.Z * u.Z) > EPSILON)
        {
            rollError = FMath::Atan2(u.Y, u.Z);
        }
    }

    return FVector3d(
        NewtonianAxisAttitudeRate(rollError, env.MaxAngularRateRadPerS.X, env.MaxAngularAccelRadPerS2.X),
        NewtonianAxisAttitudeRate(pitchError, env.MaxAngularRateRadPerS.Y, env.MaxAngularAccelRadPerS2.Y),
        NewtonianAxisAttitudeRate(yawError, env.MaxAngularRateRadPerS.Z, env.MaxAngularAccelRadPerS2.Z));
}

//////////////////////////////////////////////////////////////////////////
// Returns the rest-to-rest bang-bang time to turn by angle with a rate limit and angular acceleration
double SOLNewtonian::RotationTimeS(const double angleRad, const double maxRateRadPerS, const double alphaRadPerS2)
{
    const double theta = FMath::Abs(angleRad);
    if (theta == 0.0)
    {
        return 0.0;
    }
    if (!FMath::IsFinite(theta) || !FMath::IsFinite(alphaRadPerS2) || alphaRadPerS2 <= 0.0
        || FMath::IsNaN(maxRateRadPerS) || maxRateRadPerS <= 0.0)
    {
        return TNumericLimits<double>::Max();
    }

    // Trapezoid (reaches the rate limit) or triangle profile
    if (theta >= maxRateRadPerS * maxRateRadPerS / alphaRadPerS2)
    {
        return theta / maxRateRadPerS + maxRateRadPerS / alphaRadPerS2;
    }
    return 2.0 * FMath::Sqrt(theta / alphaRadPerS2);
}

//////////////////////////////////////////////////////////////////////////
// Returns the distance needed to stop from speed: coast for the flip time, then brake at a
double SOLNewtonian::StoppingDistanceM(const double speedMps, const double brakeAccelMps2, const double flipTimeS)
{
    if (!FMath::IsFinite(speedMps) || !FMath::IsFinite(flipTimeS))
    {
        return TNumericLimits<double>::Max();
    }
    const double v = FMath::Abs(speedMps);
    if (v == 0.0)
    {
        return 0.0;
    }
    if (!FMath::IsFinite(brakeAccelMps2) || brakeAccelMps2 <= 0.0)
    {
        return TNumericLimits<double>::Max();
    }
    return v * FMath::Max(flipTimeS, 0.0) + v * v / (2.0 * brakeAccelMps2);
}

//////////////////////////////////////////////////////////////////////////
// Returns the highest closing speed that can still stop within d after a flip time
double SOLNewtonian::BrakingSpeedLimitMps(const double distanceM, const double brakeAccelMps2, const double flipTimeS)
{
    if (!FMath::IsFinite(distanceM) || distanceM <= 0.0 || !FMath::IsFinite(brakeAccelMps2) || brakeAccelMps2 <= 0.0
        || !FMath::IsFinite(flipTimeS))
    {
        return 0.0;
    }
    const double tf = FMath::Max(flipTimeS, 0.0);
    return 2.0 * distanceM / (FMath::Sqrt(tf * tf + 2.0 * distanceM / brakeAccelMps2) + tf);
}

//////////////////////////////////////////////////////////////////////////
// Scales a desired world thrust acceleration down to fit the per-axis box and total cap, keeping its direction
FSOLThrustAllocation SOLNewtonian::AllocateThrust(const FVector3d& desiredAccelWorldMps2, const FQuat4d& orientation,
    const FSOLThrustEnvelope& env, const bool bBoost)
{
    FSOLThrustAllocation allocation;
    if (!NewtonianIsFinite(desiredAccelWorldMps2) || !NewtonianIsFinite(orientation)
        || desiredAccelWorldMps2.IsZero())
    {
        return allocation;
    }

    // One common scale so every axis fits its limit (an axis with no thruster blocks the whole request)
    const FVector3d local = orientation.UnrotateVector(desiredAccelWorldMps2);
    const FVector3d limits = NewtonianAxisLimits(local, env, bBoost);
    double scale = 1.0;
    for (int32 axis = 0; axis < 3; ++axis)
    {
        const double demand = FMath::Abs(local[axis]);
        if (demand > 0.0)
        {
            scale = limits[axis] > 0.0 ? FMath::Min(scale, limits[axis] / demand) : 0.0;
        }
    }

    // The total-acceleration cap, measured on the whole request
    const double cap = env.MaxTotalAccelMps2;
    if (FMath::IsFinite(cap) && cap > 0.0)
    {
        scale = FMath::Min(scale, cap / desiredAccelWorldMps2.Size());
    }

    for (int32 axis = 0; axis < 3; ++axis)
    {
        allocation.ThrustFraction[axis] = limits[axis] > 0.0 ? scale * local[axis] / limits[axis] : 0.0;
    }
    allocation.AchievedAccelWorldMps2 = orientation.RotateVector(
        LocalThrustAccelMps2(allocation.ThrustFraction, env, bBoost));
    allocation.bSaturated = scale < 1.0 - NEWTONIAN_SATURATION_TOLERANCE;
    return allocation;
}

//////////////////////////////////////////////////////////////////////////
// Returns the thrust acceleration that relaxes v toward vTarget with time constant tau, compensating gravity
FVector3d SOLNewtonian::VelocityHoldAccelMps2(const FVector3d& velocityMps, const FVector3d& targetVelocityMps,
    const FVector3d& gravityAccelMps2, const double timeConstantS, const double dt)
{
    if (!NewtonianIsFinite(velocityMps) || !NewtonianIsFinite(targetVelocityMps)
        || !NewtonianIsFinite(gravityAccelMps2))
    {
        return FVector3d::ZeroVector;
    }
    const double tau = FMath::IsFinite(timeConstantS) && timeConstantS > 0.0 ? timeConstantS : 0.0;
    const double step = FMath::IsFinite(dt) && dt > 0.0 ? dt : 0.0;
    return (targetVelocityMps - velocityMps) / FMath::Max3(tau, step, MIN_TIME_CONSTANT_S) - gravityAccelMps2;
}

//////////////////////////////////////////////////////////////////////////
// Returns the G-load of a proper (non-gravitational) acceleration
double SOLNewtonian::GLoadG(const FVector3d& properAccelMps2)
{
    return NewtonianIsFinite(properAccelMps2) ? properAccelMps2.Size() / STANDARD_GRAVITY_MPS2 : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the meter drain per second at a G-load (0 at or below the sustained limit)
double SOLNewtonian::GDrainPerS(const double gLoad, const FSOLPilotGLimits& limits)
{
    if (FMath::IsNaN(gLoad) || gLoad <= limits.SustainedG + G_LOAD_TOLERANCE_G)
    {
        return 0.0;
    }
    if (!FMath::IsFinite(gLoad))
    {
        return std::numeric_limits<double>::infinity();
    }
    const double normalized = (gLoad - limits.SustainedG) / FMath::Max(limits.MaxG - limits.SustainedG, EPSILON);
    return FMath::Pow(normalized, limits.DrainExponent) / FMath::Max(limits.TimeToEmptyAtMaxGS, EPSILON);
}

//////////////////////////////////////////////////////////////////////////
// Advances the G meter by dt at a constant G-load (exact piecewise-linear, including G-LOC entry and wake)
FSOLGMeterState SOLNewtonian::StepGMeter(const FSOLGMeterState& state, const double gLoad,
    const FSOLPilotGLimits& limits, const double dt)
{
    if (!FMath::IsFinite(dt) || dt <= 0.0 || FMath::IsNaN(gLoad))
    {
        return state;
    }
    const double load = FMath::Max(gLoad, 0.0);
    const bool bOverSustained = load > limits.SustainedG + G_LOAD_TOLERANCE_G;
    const double rate = bOverSustained ? -GDrainPerS(load, limits) : limits.RecoverPerS;

    // Unconscious, the meter always recovers (at least at the floor), so G-LOC ends even under a constant load
    const double unconsciousRate = FMath::Max(rate, limits.UnconsciousRecoverPerS);

    // Walk the step through its Normal / G-LOC phases; the meter is linear inside each
    FSOLGMeterState result = state;
    double remaining = dt;
    for (int32 phase = 0; phase < NEWTONIAN_MAX_METER_PHASES; ++phase)
    {
        if (!result.bBlackout)
        {
            // Normal: drain into G-LOC at the exact crossing instant, else move and stop
            if (rate < 0.0 && result.Meter + rate * remaining <= 0.0)
            {
                const double crossing = (!FMath::IsFinite(rate) || result.Meter <= 0.0) ? 0.0 : result.Meter / -rate;
                result.Meter = 0.0;
                result.bBlackout = true;
                result.BlackoutRemainingS = limits.BlackoutDurationS;
                remaining = FMath::Max(remaining - crossing, 0.0);
                continue;
            }
            result.Meter = NewtonianAdvanceMeter(result.Meter, rate, remaining);
            break;
        }

        // G-LOC: wake at the later of the timer and the meter reaching WakeMeter
        const double timerS = result.BlackoutRemainingS;
        const double meterS = result.Meter >= limits.WakeMeter ? 0.0 : (unconsciousRate > 0.0
            ? (limits.WakeMeter - result.Meter) / unconsciousRate : std::numeric_limits<double>::infinity());
        const double wakeS = FMath::Max(timerS, meterS);
        if (wakeS <= remaining)
        {
            result.Meter = NewtonianAdvanceMeter(result.Meter, unconsciousRate, wakeS);
            result.bBlackout = false;
            result.BlackoutRemainingS = 0.0;
            remaining = FMath::Max(remaining - wakeS, 0.0);
            continue;
        }
        result.Meter = NewtonianAdvanceMeter(result.Meter, unconsciousRate, remaining);
        result.BlackoutRemainingS = FMath::Max(0.0, timerS - remaining);
        break;
    }

    // Time spent depleted under load (the HUD's lethal-G warning)
    const bool bDepleted = result.bBlackout || result.Meter <= limits.CriticalMeter;
    result.DepletedS = bOverSustained && bDepleted ? state.DepletedS + dt : 0.0;
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD warning level for the meter and the current G-load
ESOLGWarning SOLNewtonian::GWarningLevel(const FSOLGMeterState& state, const double gLoad,
    const FSOLPilotGLimits& limits)
{
    if (state.DepletedS >= limits.LethalWarningS)
    {
        return ESOLGWarning::Lethal;
    }
    if (state.bBlackout)
    {
        return ESOLGWarning::Blackout;
    }
    if (state.Meter <= limits.CriticalMeter)
    {
        return ESOLGWarning::Critical;
    }
    return gLoad > limits.SustainedG + G_LOAD_TOLERANCE_G ? ESOLGWarning::OverSustained : ESOLGWarning::None;
}

//////////////////////////////////////////////////////////////////////////
// Returns the tunnel-vision / gray-out amount in [0, 1] (1 during G-LOC)
double SOLNewtonian::GVisionLoss(const FSOLGMeterState& state, const FSOLPilotGLimits& limits)
{
    if (state.bBlackout)
    {
        return 1.0;
    }
    return FMath::Clamp((limits.VisionStartMeter - state.Meter) / FMath::Max(limits.VisionStartMeter, EPSILON),
        0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the pilot is conscious
bool SOLNewtonian::PilotHasControl(const FSOLGMeterState& state)
{
    return !state.bBlackout;
}

//////////////////////////////////////////////////////////////////////////
// Returns the pilot's sustained acceleration limit in m/s^2
double SOLNewtonian::PilotSustainedAccelMps2(const FSOLPilotGLimits& limits)
{
    return FMath::IsFinite(limits.SustainedG) ? FMath::Max(limits.SustainedG, 0.0) * STANDARD_GRAVITY_MPS2 : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the autopilot's acceleration limit: sustained, or the recovery limit while critical or in G-LOC
double SOLNewtonian::AutopilotAccelLimitMps2(const FSOLPilotGLimits& limits, const FSOLGMeterState& state)
{
    const bool bRecovering = state.bBlackout || state.Meter <= limits.CriticalMeter;
    const double limitG = bRecovering ? FMath::Min(limits.SustainedG, limits.AutopilotRecoveryG) : limits.SustainedG;
    return FMath::Max(limitG * STANDARD_GRAVITY_MPS2, 0.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the desired thrust acceleration clamped to a length limit
FVector3d SOLNewtonian::ClampToPilotLimit(const FVector3d& desiredThrustAccelMps2, const double limitMps2)
{
    if (!NewtonianIsFinite(desiredThrustAccelMps2) || FMath::IsNaN(limitMps2) || limitMps2 <= 0.0)
    {
        return FVector3d::ZeroVector;
    }
    return FMath::IsFinite(limitMps2) ? NewtonianClampLength(desiredThrustAccelMps2, limitMps2)
        : desiredThrustAccelMps2;
}

//////////////////////////////////////////////////////////////////////////
// Returns an NPC class's effective G cap: humans are held to HUMAN_MAX_G, AIs use the structural cap
double SOLNewtonian::EffectiveGCapG(const ESOLPilotKind kind, const double classCapG)
{
    if (FMath::IsNaN(classCapG) || classCapG <= 0.0)
    {
        return 0.0;
    }
    return kind == ESOLPilotKind::Human ? FMath::Min(classCapG, HUMAN_MAX_G) : classCapG;
}

//////////////////////////////////////////////////////////////////////////
// Returns a copy of the envelope whose total-acceleration cap is at most capG
FSOLThrustEnvelope SOLNewtonian::WithGCap(const FSOLThrustEnvelope& env, const double capG)
{
    FSOLThrustEnvelope result = env;
    if (FMath::IsFinite(capG) && capG > 0.0)
    {
        result.MaxTotalAccelMps2 = FMath::Min(NewtonianCapOrInfinity(env.MaxTotalAccelMps2),
            capG * STANDARD_GRAVITY_MPS2);
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the rendezvous guidance (braking-curve law in the target's frame) for one step
FSOLGuidanceOutput SOLNewtonian::ComputeRendezvousGuidance(const FVector3d& relativePositionM,
    const FVector3d& relativeVelocityMps, const FVector3d& targetAccelMps2, const FVector3d& gravityAccelMps2,
    const FSOLGuidanceParams& params, const double dt)
{
    return ComputeRendezvousGuidance(relativePositionM, relativeVelocityMps, targetAccelMps2, gravityAccelMps2, params,
        dt, true);
}

//////////////////////////////////////////////////////////////////////////
// Returns the rendezvous guidance; with bComputeEta false EtaS stays 0
FSOLGuidanceOutput SOLNewtonian::ComputeRendezvousGuidance(const FVector3d& relativePositionM,
    const FVector3d& relativeVelocityMps, const FVector3d& targetAccelMps2, const FVector3d& gravityAccelMps2,
    const FSOLGuidanceParams& params, const double dt, const bool bComputeEta)
{
    FSOLGuidanceOutput out;
    if (!NewtonianIsFinite(relativePositionM) || !NewtonianIsFinite(relativeVelocityMps)
        || !NewtonianIsFinite(targetAccelMps2) || !NewtonianIsFinite(gravityAccelMps2)
        || !NewtonianParamsFinite(params) || !FMath::IsFinite(dt) || dt < 0.0)
    {
        return out;
    }

    // Geometry in the target's frame: distance, direction to the target, closing speed
    const double d = relativePositionM.Size();
    const FVector3d dir = d > 0.0 ? relativePositionM / d : FVector3d::ZeroVector;
    const FVector3d& v = relativeVelocityMps;
    const double vc = FVector3d::DotProduct(v, dir);
    const double flipTimeS = FMath::Max(params.FlipTimeS, 0.0);
    const double tfEff = flipTimeS + dt;
    const double cap = params.MaxCruiseSpeedMps;
    const double curve = BrakingSpeedLimitMps(d, params.BrakeAccelMps2, tfEff);
    const double speedLimit = FMath::Min(cap, curve);
    const FVector3d desiredVelocity = dir * speedLimit;

    // Phase
    if (d <= params.ArrivalRadiusM && v.Size() <= params.ArrivalSpeedMps)
    {
        out.Phase = ESOLGuidancePhase::Arrived;
    }
    else if (curve < cap && vc > 0.0 && vc >= curve * (1.0 - NEWTONIAN_BRAKE_CURVE_MARGIN))
    {
        out.Phase = ESOLGuidancePhase::Brake;
    }
    else if (vc >= cap * NEWTONIAN_CRUISE_FRACTION)
    {
        out.Phase = ESOLGuidancePhase::Cruise;
    }
    else
    {
        out.Phase = ESOLGuidancePhase::Accelerate;
    }

    // Velocity tracker toward the braking curve, plus the curve's own slope as feedforward while braking. A step
    // approaching the tracker's time constant is nearly deadbeat (it re-targets the curve every step), so the
    // feedforward fades continuously from 1 at dt = 0.5 / gain to 0 at dt = 1 / gain; counted twice it rings at period
    // 2 (SDD 18 Amendments 1 and 7). Real-time steps keep the full feedforward
    const double gainTimeS = params.VelocityGainPerS > 0.0 ? 1.0 / params.VelocityGainPerS : 0.0;
    const double trackerTimeS = FMath::Max3(gainTimeS, dt, MIN_TIME_CONSTANT_S);
    FVector3d correction = (desiredVelocity - v) / trackerTimeS;
    if (out.Phase == ESOLGuidancePhase::Brake && d > EPSILON && params.BrakeAccelMps2 > 0.0)
    {
        const double feedforwardScale = gainTimeS > 0.0 ? FMath::Min3(1.0, gainTimeS / trackerTimeS,
            FMath::Clamp(2.0 * (1.0 - FMath::Max(dt, 0.0) * params.VelocityGainPerS), 0.0, 1.0)) : 1.0;
        correction -= dir * (feedforwardScale * vc / FMath::Sqrt(tfEff * tfEff + 2.0 * d / params.BrakeAccelMps2));
    }

    // Clamp to the phase budget, then net and thrust accelerations
    const double budget = out.Phase == ESOLGuidancePhase::Brake
        ? FMath::Max(params.PlanAccelMps2, params.BrakeAccelMps2) : params.PlanAccelMps2;
    const FVector3d relativeAccel = budget > 0.0 ? NewtonianClampLength(correction, budget) : FVector3d::ZeroVector;
    out.DesiredNetAccelMps2 = targetAccelMps2 + relativeAccel;
    out.DesiredThrustAccelMps2 = out.DesiredNetAccelMps2 - gravityAccelMps2;

    // Nose: along the thrust, else flip early while coasting inside the stopping distance, else toward the target
    if (out.Phase != ESOLGuidancePhase::Arrived)
    {
        const double thrustSize = out.DesiredThrustAccelMps2.Size();
        if (thrustSize >= FORWARD_HOLD_FRACTION * params.PlanAccelMps2 && thrustSize > EPSILON)
        {
            out.DesiredForward = out.DesiredThrustAccelMps2 / thrustSize;
        }
        else if (vc > 0.0 && d <= StoppingDistanceM(vc, params.BrakeAccelMps2, 2.0 * tfEff))
        {
            out.DesiredForward = -dir;
        }
        else
        {
            out.DesiredForward = dir;
        }
    }

    out.DistanceM = d;
    out.ClosingSpeedMps = vc;
    out.SpeedLimitMps = speedLimit;
    out.EtaS = out.Phase == ESOLGuidancePhase::Arrived || !bComputeEta ? 0.0
        : EstimateTimeToArrivalS(d, vc, params.PlanAccelMps2, params.BrakeAccelMps2, params.FlipTimeS, cap);
    return out;
}

//////////////////////////////////////////////////////////////////////////
// Returns guidance budgets from the envelope, the pilot limit and gravity (flip-and-burn or reverse braking)
FSOLGuidanceParams SOLNewtonian::MakeGuidanceParams(const FSOLThrustEnvelope& env, const double pilotAccelLimitMps2,
    const double gravityMagnitudeMps2, const bool bFlipToBrake, const bool bBoost)
{
    const double multiplier = bBoost ? env.BoostMultiplier : 1.0;
    const double limit = FMath::Min(NewtonianCapOrInfinity(pilotAccelLimitMps2),
        NewtonianCapOrInfinity(env.MaxTotalAccelMps2));
    const double forward = FMath::Min(env.ForwardAccelMps2 * multiplier, limit);
    const double reverse = FMath::Min(env.ReverseAccelMps2 * multiplier, limit);
    const double gravity = FMath::IsFinite(gravityMagnitudeMps2) && gravityMagnitudeMps2 > 0.0
        ? gravityMagnitudeMps2 : 0.0;

    FSOLGuidanceParams params;
    params.PlanAccelMps2 = FMath::Max(GUIDANCE_MIN_ACCEL_FRACTION * forward, GUIDANCE_ACCEL_MARGIN * forward - gravity);
    params.BrakeAccelMps2 = bFlipToBrake ? params.PlanAccelMps2
        : FMath::Max(GUIDANCE_MIN_ACCEL_FRACTION * reverse, GUIDANCE_ACCEL_MARGIN * reverse - gravity);
    params.FlipTimeS = bFlipToBrake
        ? RotationTimeS(UE_DOUBLE_PI, env.MaxAngularRateRadPerS.Y,
            ATTITUDE_BRAKE_FRACTION * env.MaxAngularAccelRadPerS2.Y) + 1.0 / ATTITUDE_GAIN_PER_S
        : 0.0;
    return params;
}

//////////////////////////////////////////////////////////////////////////
// Returns the ETA of the 1-D braking-curve plan (DBL_MAX for invalid inputs)
double SOLNewtonian::EstimateTimeToArrivalS(const double distanceM, const double closingSpeedMps,
    const double accelMps2, const double brakeAccelMps2, const double flipTimeS, const double cruiseCapMps)
{
    if (!FMath::IsFinite(distanceM) || !FMath::IsFinite(closingSpeedMps) || !FMath::IsFinite(accelMps2)
        || !FMath::IsFinite(brakeAccelMps2) || !FMath::IsFinite(flipTimeS) || !FMath::IsFinite(cruiseCapMps)
        || accelMps2 <= 0.0 || brakeAccelMps2 <= 0.0 || cruiseCapMps <= 0.0 || distanceM < 0.0)
    {
        return TNumericLimits<double>::Max();
    }
    const double tf = FMath::Max(flipTimeS, 0.0);
    const double d = distanceM;
    const double vc = closingSpeedMps;

    // (a) Already there and at rest
    if (d <= EPSILON && FMath::Abs(vc) <= EPSILON)
    {
        return 0.0;
    }

    // (b) Receding: stop first, then fly the longer distance from rest
    if (vc < 0.0)
    {
        const double stopS = -vc / accelMps2;
        const double farther = d + vc * vc / (2.0 * accelMps2);
        return stopS + EstimateTimeToArrivalS(farther, 0.0, accelMps2, brakeAccelMps2, flipTimeS, cruiseCapMps);
    }

    // (c) Too fast to stop: brake, overshoot, come back from rest
    if (vc > BrakingSpeedLimitMps(d, brakeAccelMps2, tf))
    {
        const double stopS = tf + vc / brakeAccelMps2;
        const double overshoot = FMath::Max(StoppingDistanceM(vc, brakeAccelMps2, tf) - d, 0.0);
        return stopS + EstimateTimeToArrivalS(overshoot, 0.0, accelMps2, brakeAccelMps2, flipTimeS, cruiseCapMps);
    }

    // (d) Above the cruise cap: slow to it first
    if (vc > cruiseCapMps)
    {
        const double slowS = (vc - cruiseCapMps) / accelMps2;
        const double slowDistance = (vc * vc - cruiseCapMps * cruiseCapMps) / (2.0 * accelMps2);
        return slowS + NewtonianPeakOrCruiseEta(d - slowDistance, cruiseCapMps, accelMps2, brakeAccelMps2, tf,
            cruiseCapMps);
    }

    // (e) / (f)
    return NewtonianPeakOrCruiseEta(d, vc, accelMps2, brakeAccelMps2, tf, cruiseCapMps);
}

//////////////////////////////////////////////////////////////////////////
// Writes the predicted ship-minus-target path into the view; returns the number of points written
int32 SOLNewtonian::SampleRendezvousTrajectory(const FVector3d& relativePositionM,
    const FVector3d& relativeVelocityMps, const FSOLGuidanceParams& params, const double sampleIntervalS,
    const int32 substepsPerSample, TArrayView<FVector3d> outShipMinusTargetM)
{
    if (!NewtonianIsFinite(relativePositionM) || !NewtonianIsFinite(relativeVelocityMps)
        || !NewtonianParamsFinite(params) || !FMath::IsFinite(sampleIntervalS) || sampleIntervalS <= 0.0
        || substepsPerSample < 1 || outShipMinusTargetM.Num() == 0)
    {
        return 0;
    }

    // Point-mass run of the same guidance law in the (unaccelerated) target's frame, without gravity
    FVector3d position = -relativePositionM;
    FVector3d velocity = relativeVelocityMps;
    const double h = sampleIntervalS / substepsPerSample;
    outShipMinusTargetM[0] = position;
    int32 count = 1;
    while (count < outShipMinusTargetM.Num())
    {
        for (int32 substep = 0; substep < substepsPerSample; ++substep)
        {
            const FSOLGuidanceOutput out = ComputeRendezvousGuidance(-position, velocity, FVector3d::ZeroVector,
                FVector3d::ZeroVector, params, h, false);
            if (out.Phase == ESOLGuidancePhase::Arrived)
            {
                if (position != outShipMinusTargetM[count - 1])
                {
                    outShipMinusTargetM[count++] = position;
                }
                return count;
            }
            velocity += out.DesiredNetAccelMps2 * h;
            position += velocity * h;
        }
        outShipMinusTargetM[count++] = position;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns the orientation with its nose on forward and its up toward the hint (fallback for a bad forward)
FQuat4d SOLNewtonian::OrientationFromForward(const FVector3d& forward, const FVector3d& upHint,
    const FQuat4d& fallback)
{
    if (!NewtonianIsFinite(forward) || forward.SizeSquared() <= 0.0)
    {
        return fallback.GetNormalized();
    }
    const FVector3d x = forward.GetUnsafeNormal();

    // Returns the normalized part of a hint perpendicular to the nose, or zero when it is degenerate
    auto perpendicular = [&x](const FVector3d& hint)
    {
        if (!NewtonianIsFinite(hint) || hint.SizeSquared() <= 0.0)
        {
            return FVector3d::ZeroVector;
        }
        const FVector3d unit = hint.GetUnsafeNormal();
        const FVector3d part = unit - x * FVector3d::DotProduct(unit, x);
        const double size = part.Size();
        return size > NEWTONIAN_DEGENERATE_HINT_LENGTH ? part / size : FVector3d::ZeroVector;
    };

    // The hint, then world +Z, then world +X
    FVector3d z = perpendicular(upHint);
    if (z.IsZero())
    {
        z = perpendicular(FVector3d(0.0, 0.0, 1.0));
    }
    if (z.IsZero())
    {
        z = perpendicular(FVector3d(1.0, 0.0, 0.0));
    }
    const FVector3d y = FVector3d::CrossProduct(z, x);
    return FQuat4d(FMatrix44d(x, y, z, FVector3d::ZeroVector)).GetNormalized();
}

//////////////////////////////////////////////////////////////////////////
// Returns true when snapping by offset at distance moves at most half a pixel
bool SOLNewtonian::IsHandoffSnapInvisible(const double offsetM, const double distanceM, const double pixelAngleRad)
{
    if (!FMath::IsFinite(offsetM) || !FMath::IsFinite(distanceM) || !FMath::IsFinite(pixelAngleRad) || offsetM < 0.0
        || distanceM < 0.0 || pixelAngleRad < 0.0)
    {
        return false;
    }
    return offsetM <= 0.5 * pixelAngleRad * distanceM;
}
