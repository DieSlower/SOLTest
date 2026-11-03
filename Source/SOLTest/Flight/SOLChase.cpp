/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLChase.h"

#include <limits>

namespace
{
    // MaxSafeGForStep spends this fraction less than the whole meter budget, so rounding never ends below the floor
    constexpr double CHASE_SAFE_STEP_MARGIN = 1.0e-9;

    // Meter and speed comparisons of the prediction treat values this close as equal
    constexpr double CHASE_EVENT_EPS = 1.0e-12;

    // A brake that starts this far (relative) beyond the braking curve is "too fast" (overshoot and come back)
    constexpr double CHASE_CURVE_TOLERANCE = 1.0e-9;

    // Lowest end speed of the braking-curve tail (its time has a log of the end speed)
    constexpr double CHASE_MIN_END_SPEED_MPS = 1.0e-3;

    // The brake drain integral is split at this many flip times above its low end (where the load changes fastest)
    constexpr double CHASE_QUAD_SPLIT_FLIP_TIMES = 10.0;

    // 6-point Gauss-Legendre nodes and weights on [-1, 1]
    constexpr int32 CHASE_QUAD_POINTS = 6;
    constexpr double CHASE_QUAD_NODES[CHASE_QUAD_POINTS] = { -0.9324695142031521, -0.6612093864662645,
        -0.2386191860831969, 0.2386191860831969, 0.6612093864662645, 0.9324695142031521 };
    constexpr double CHASE_QUAD_WEIGHTS[CHASE_QUAD_POINTS] = { 0.1713244923791704, 0.3607615730481386,
        0.4679139345726910, 0.4679139345726910, 0.3607615730481386, 0.1713244923791704 };

    // Grid of the planner: PLAN_G_LEVELS + 1 levels, pairs with BrakePlanG <= BurstG
    constexpr int32 CHASE_PLAN_LEVELS = SOL::Chase::PLAN_G_LEVELS + 1;
    constexpr int32 CHASE_PLAN_PAIRS = CHASE_PLAN_LEVELS * (CHASE_PLAN_LEVELS + 1) / 2;

    // A feasible plan's lowest meter may be this much under its bound (rounding)
    constexpr double CHASE_FEASIBLE_TOLERANCE = 1.0e-9;

    constexpr double CHASE_INFINITY = std::numeric_limits<double>::infinity();

    // The float settings constants are rounded to this many steps per unit before use as doubles
    constexpr double CHASE_SETTING_SCALE = 1.0e6;

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of v is finite
    bool ChaseIsFinite(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the cap when it is finite and positive, else +inf (no cap)
    double ChaseCapOrInfinity(const double cap)
    {
        return FMath::IsFinite(cap) && cap > 0.0 ? cap : CHASE_INFINITY;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a float setting constant as the double it stands for (0.30f is 0.3000000119; settings step in 1e-6)
    double ChaseSettingValue(const float value)
    {
        return FMath::RoundToDouble(static_cast<double>(value) * CHASE_SETTING_SCALE) / CHASE_SETTING_SCALE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a gravity magnitude usable by the budgets (0 for a bad or negative value)
    double ChaseGravity(const double gravityMps2)
    {
        return FMath::IsFinite(gravityMps2) && gravityMps2 > 0.0 ? gravityMps2 : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a policy floor usable by the thresholds (NaN -> the default margin's floor, else clamped to [0, 1])
    double ChaseSanitizedFloor(const double floorMeter)
    {
        return FMath::IsNaN(floorMeter) ? SOLChase::EffectiveMeterFloor(floorMeter)
            : FMath::Clamp(floorMeter, 0.0, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a policy hysteresis usable by the thresholds (bad -> COOL_HYSTERESIS_METER)
    double ChaseSanitizedHysteresis(const double hysteresis)
    {
        return FMath::IsFinite(hysteresis) && hysteresis >= 0.0 ? hysteresis : SOL::Chase::COOL_HYSTERESIS_METER;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the guidance budget of one G level: max(10%, 90% of the thrust - gravity), thrust capped by the envelope
    double ChaseBudgetMps2(const FSOLThrustEnvelope& env, const double levelG, const double gravityMps2,
        const bool bBoost)
    {
        const double multiplier = bBoost ? env.BoostMultiplier : 1.0;
        const double pilot = FMath::IsNaN(levelG) || levelG <= 0.0 ? 0.0
            : levelG * SOLNewtonian::STANDARD_GRAVITY_MPS2;
        const double forward = env.ForwardAccelMps2 * multiplier;
        const double accel = FMath::Min3(pilot, FMath::IsNaN(forward) ? 0.0 : forward,
            ChaseCapOrInfinity(env.MaxTotalAccelMps2));
        if (!(accel > 0.0))
        {
            return 0.0;
        }
        return FMath::Max(SOLNewtonian::GUIDANCE_MIN_ACCEL_FRACTION * accel,
            SOLNewtonian::GUIDANCE_ACCEL_MARGIN * accel - ChaseGravity(gravityMps2));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the G-load flown at a level whose budget is accelMps2 (gravity assumed to oppose; never above the level)
    double ChaseFlownLoadG(const double levelG, const double accelMps2, const double gravityMps2)
    {
        return FMath::Min(levelG, (accelMps2 + gravityMps2) / SOLNewtonian::STANDARD_GRAVITY_MPS2);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the signed meter drain per second at a load (negative = recovery at RecoverPerS)
    double ChaseNetDrainPerS(const double loadG, const FSOLPilotGLimits& limits)
    {
        return loadG > limits.SustainedG + SOLNewtonian::G_LOAD_TOLERANCE_G ? SOLNewtonian::GDrainPerS(loadG, limits)
            : -limits.RecoverPerS;
    }

    // Everything one prediction keeps constant
    struct FSOLChasePredictModel
    {
        double S = 0.0;                 // Sustained G
        double R = 0.0;                 // Recovery per second
        double BurstG = 0.0;
        double BrakePlanG = 0.0;
        double Floor = 0.0;
        double Hysteresis = 0.0;
        double Gravity = 0.0;
        double BurstAccel = 0.0;        // Plan budget while bursting
        double CoolAccel = 0.0;         // Plan budget while cooling (sustained)
        double BrakeAccel = 0.0;        // a_bp of the braking curve
        double FlipS = 0.0;
        double BurstDrain = 0.0;        // Signed meter drain per second while bursting
        double ReservePerMps = 0.0;     // BrakeReserveMeter per m/s of closing speed (the reserve is linear in v)
        double CruiseCap = 0.0;
    };

    // The predicted ship along the line of sight
    struct FSOLChasePredictState
    {
        double TimeS = 0.0;
        double X = 0.0;                 // Distance flown toward the aim point
        double V = 0.0;                 // Closing speed
        double Meter = 1.0;
        double MinMeter = 1.0;
        bool bCooling = false;
        int32 Segments = 0;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns true when the state has reached the braking curve (stopping distance with the flip at least the rest)
    bool ChaseAtBrakePoint(const FSOLChasePredictModel& model, const FSOLChasePredictState& state,
        const double distanceM)
    {
        const double v = state.V;
        return v > 0.0 && v * model.FlipS + v * v / (2.0 * model.BrakeAccel) >= distanceM - state.X;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the seconds of constant acceleration a until the state meets the braking curve
    double ChaseTimeToBrakePoint(const FSOLChasePredictModel& model, const FSOLChasePredictState& state,
        const double distanceM, const double accel)
    {
        // Peak speed of accelerate-then-curve: v^2 (1 / 2a + 1 / 2 a_bp) + v tf = d - x + v0^2 / 2a
        const double tf = model.FlipS;
        const double a2 = 1.0 / (2.0 * accel) + 1.0 / (2.0 * model.BrakeAccel);
        const double c = distanceM - state.X + state.V * state.V / (2.0 * accel);
        const double peak = 2.0 * c / (tf + FMath::Sqrt(tf * tf + 4.0 * a2 * c));
        return FMath::Max((peak - state.V) / accel, 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the meter the braking curve drains while its load is above sustained, from speed v down to vLow
    double ChaseBrakeDrain(const FSOLChasePredictModel& model, const FSOLPilotGLimits& limits, const double v,
        const double vLow)
    {
        // On the curve v = a (u - tf) and the deceleration is a (1 - tf / u); dt = u / (u - tf) du
        const double a = model.BrakeAccel;
        const double tf = model.FlipS;
        const double uHigh = v / a + tf;
        const double uLow = vLow / a + tf;
        const double uSplit = FMath::Min(uHigh, uLow + CHASE_QUAD_SPLIT_FLIP_TIMES * tf);
        const double bounds[3] = { uLow, uSplit, uHigh };
        double drain = 0.0;

        // Gauss-Legendre on [uLow, uSplit] and [uSplit, uHigh]
        for (int32 piece = 0; piece < 2; ++piece)
        {
            const double lo = bounds[piece];
            const double hi = bounds[piece + 1];
            if (hi <= lo)
            {
                continue;
            }
            const double half = 0.5 * (hi - lo);
            const double centre = 0.5 * (hi + lo);
            double sum = 0.0;
            for (int32 point = 0; point < CHASE_QUAD_POINTS; ++point)
            {
                const double u = centre + half * CHASE_QUAD_NODES[point];
                const double load = ChaseFlownLoadG(model.BurstG, a * (1.0 - tf / u), model.Gravity);
                sum += CHASE_QUAD_WEIGHTS[point] * SOLNewtonian::GDrainPerS(load, limits) * u / (u - tf);
            }
            drain += sum * half;
        }
        return drain;
    }

    //////////////////////////////////////////////////////////////////////////
    // Skips whole steady burst / cool cycles (no brake reserve) that end before the brake point; true if any skipped
    bool ChaseSkipSteadyCycles(const FSOLChasePredictModel& model, FSOLChasePredictState& state, const double distanceM)
    {
        // One cycle: burst from the resume level down to the switch level, then cool back up
        const double swing = model.Hysteresis - SOL::Chase::COOL_SWITCH_TOLERANCE_METER;
        const double burstS = swing / model.BurstDrain;
        const double coolS = swing / model.R;
        const double periodS = burstS + coolS;
        const double dv = model.BurstAccel * burstS + model.CoolAccel * coolS;
        const double cycleDistance = 0.5 * model.BurstAccel * burstS * burstS + model.BurstAccel * burstS * coolS
            + 0.5 * model.CoolAccel * coolS * coolS;
        if (!(periodS > 0.0) || !(dv > 0.0))
        {
            return false;
        }

        // g(n) = (stopping distance - remaining distance) after n cycles, a quadratic in n; skip the cycles with g < 0
        const double v = state.V;
        const double tf = model.FlipS;
        const double aBp = model.BrakeAccel;
        const double alpha = dv * dv / (2.0 * aBp) + 0.5 * periodS * dv;
        const double beta = tf * dv + v * dv / aBp + v * periodS - 0.5 * periodS * dv + cycleDistance;
        const double gamma = tf * v + v * v / (2.0 * aBp) + state.X - distanceM;
        const double disc = beta * beta - 4.0 * alpha * gamma;
        if (!(alpha > 0.0) || !(disc >= 0.0))
        {
            return false;
        }
        double cycles = FMath::FloorToDouble((-beta + FMath::Sqrt(disc)) / (2.0 * alpha)) - 1.0;
        cycles = FMath::Min(cycles, FMath::FloorToDouble((model.CruiseCap - v) / dv) - 1.0);
        if (!(cycles >= 1.0))
        {
            return false;
        }
        state.X += cycles * v * periodS + periodS * dv * cycles * (cycles - 1.0) * 0.5 + cycles * cycleDistance;
        state.V += cycles * dv;
        state.TimeS += cycles * periodS;
        state.MinMeter = FMath::Min(state.MinMeter, model.Floor + SOL::Chase::COOL_SWITCH_TOLERANCE_METER);
        return true;
    }

    // Outcome of one accelerate run
    enum class ESOLChaseRunEnd : uint8
    {
        BrakePoint,
        Failed
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies the accelerate / cruise phase segment by segment (burst / cool cycles) until the braking curve
    ESOLChaseRunEnd ChaseRunAccelerate(const FSOLChasePredictModel& model, FSOLChasePredictState& state,
        const double distanceM)
    {
        while (true)
        {
            if (++state.Segments > SOL::Chase::PLAN_MAX_SEGMENTS)
            {
                return ESOLChaseRunEnd::Failed;
            }

            // Thresholds of ChaseGLimit at this speed (the reserve only counts while closing)
            const double reservePerMps = state.V >= 0.0 ? model.ReservePerMps : 0.0;
            const double switchMeter = model.Floor + reservePerMps * FMath::Max(state.V, 0.0);
            const double resumeMeter = switchMeter + model.Hysteresis;
            const double coolAt = switchMeter + SOL::Chase::COOL_SWITCH_TOLERANCE_METER;
            if (resumeMeter > 1.0)
            {
                state.bCooling = true;
            }
            else if (state.bCooling && state.Meter >= resumeMeter - CHASE_EVENT_EPS)
            {
                state.bCooling = false;
            }
            else if (!state.bCooling && state.Meter <= coolAt + CHASE_EVENT_EPS)
            {
                state.bCooling = true;
            }

            if (ChaseAtBrakePoint(model, state, distanceM))
            {
                return ESOLChaseRunEnd::BrakePoint;
            }

            // At the cruise cap: coast (recovering) to the braking curve
            const double remaining = distanceM - state.X;
            if (state.V >= model.CruiseCap)
            {
                const double v = state.V;
                const double coastS = FMath::Max((remaining - (v * model.FlipS + v * v / (2.0 * model.BrakeAccel)))
                    / v, 0.0);
                state.Meter = FMath::Min(1.0, state.Meter + model.R * coastS);
                state.TimeS += coastS;
                state.X += v * coastS;
                return ESOLChaseRunEnd::BrakePoint;
            }

            const double accel = state.bCooling ? model.CoolAccel : model.BurstAccel;
            const double rate = state.bCooling ? model.R : -model.BurstDrain;
            if (!(accel > 0.0))
            {
                return ESOLChaseRunEnd::Failed;
            }

            // Events: the braking curve, the cruise cap, zero speed, a full meter, the switch / resume / force levels
            double eventS = ChaseTimeToBrakePoint(model, state, distanceM, accel);
            bool bBrakePoint = true;
            enum class ESOLChaseEvent : uint8
            {
                Other,
                Switch,
                Resume,
                Full,
                Force
            };
            ESOLChaseEvent kind = ESOLChaseEvent::Other;
            // Keeps the earliest event (the brake point stays the event unless something comes first)
            auto take = [&eventS, &bBrakePoint, &kind](const double candidateS, const ESOLChaseEvent candidateKind)
            {
                if (candidateS < eventS)
                {
                    eventS = candidateS;
                    bBrakePoint = false;
                    kind = candidateKind;
                }
            };
            take((model.CruiseCap - state.V) / accel, ESOLChaseEvent::Other);
            if (state.V < 0.0)
            {
                take(-state.V / accel, ESOLChaseEvent::Other);
            }
            if (rate > 0.0 && state.Meter < 1.0)
            {
                take((1.0 - state.Meter) / rate, ESOLChaseEvent::Full);
            }
            if (!state.bCooling)
            {
                const double closingRate = -rate + reservePerMps * accel;
                if (closingRate > 0.0)
                {
                    take(FMath::Max((state.Meter - coolAt) / closingRate, 0.0), ESOLChaseEvent::Switch);
                }
                if (reservePerMps > 0.0)
                {
                    take(FMath::Max((1.0 - model.Floor - model.Hysteresis - reservePerMps * state.V)
                        / (reservePerMps * accel), 0.0), ESOLChaseEvent::Force);
                }
            }
            else
            {
                const double closingRate = model.R - reservePerMps * accel;
                if (closingRate > 0.0 && resumeMeter <= 1.0)
                {
                    take(FMath::Max((resumeMeter - state.Meter) / closingRate, 0.0), ESOLChaseEvent::Resume);
                }
            }

            // Steady cycles with no reserve repeat exactly: skip the whole ones before the brake point
            if (kind == ESOLChaseEvent::Switch && model.ReservePerMps == 0.0 && model.BurstDrain > 0.0
                && FMath::Abs(state.Meter - (model.Floor + model.Hysteresis)) < CHASE_EVENT_EPS
                && model.Floor + model.Hysteresis <= 1.0 && ChaseSkipSteadyCycles(model, state, distanceM))
            {
                continue;
            }

            // Advance to the event; the meter lands exactly on the level it crossed
            state.X += state.V * eventS + 0.5 * accel * eventS * eventS;
            state.V += accel * eventS;
            state.TimeS += eventS;
            const double reserveNow = reservePerMps * FMath::Max(state.V, 0.0);
            switch (kind)
            {
                case ESOLChaseEvent::Switch:
                    state.Meter = model.Floor + reserveNow + SOL::Chase::COOL_SWITCH_TOLERANCE_METER;
                    state.bCooling = true;
                    break;
                case ESOLChaseEvent::Resume:
                    state.Meter = model.Floor + reserveNow + model.Hysteresis;
                    state.bCooling = false;
                    break;
                case ESOLChaseEvent::Full:
                    state.Meter = 1.0;
                    break;
                default:
                    state.Meter = FMath::Min(1.0, state.Meter + rate * eventS);
                    state.bCooling |= kind == ESOLChaseEvent::Force;
                    break;
            }
            state.MinMeter = FMath::Min(state.MinMeter, state.Meter);
            if (bBrakePoint)
            {
                return ESOLChaseRunEnd::BrakePoint;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the chase G cap: min(MaxG, forward thrust (boosted only with bBoost) / g0, total cap / g0), at least S
double SOLChase::ChaseMaxG(const FSOLThrustEnvelope& env, const FSOLPilotGLimits& limits, const bool bBoost)
{
    const double sustained = limits.SustainedG;
    const double multiplier = bBoost ? env.BoostMultiplier : 1.0;
    const double forwardG = env.ForwardAccelMps2 * multiplier / SOLNewtonian::STANDARD_GRAVITY_MPS2;
    const double totalG = ChaseCapOrInfinity(env.MaxTotalAccelMps2) / SOLNewtonian::STANDARD_GRAVITY_MPS2;
    double capG = FMath::IsNaN(limits.MaxG) ? sustained : limits.MaxG;
    capG = FMath::IsNaN(forwardG) ? capG : FMath::Min(capG, forwardG);
    capG = FMath::Min(capG, totalG);
    return FMath::IsNaN(capG) || capG < sustained ? sustained : capG;
}

//////////////////////////////////////////////////////////////////////////
// Returns the meter floor of a safety margin: clamped to the setting range, at least MIN_METER_FLOOR, NaN -> default
double SOLChase::EffectiveMeterFloor(const double margin)
{
    const double value = FMath::IsNaN(margin) ? ChaseSettingValue(SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT)
        : margin;
    const double clamped = FMath::Clamp(value, ChaseSettingValue(SOL::Settings::CHASE_SAFETY_MARGIN_MIN),
        ChaseSettingValue(SOL::Settings::CHASE_SAFETY_MARGIN_MAX));
    return FMath::Max(clamped, SOL::Chase::MIN_METER_FLOOR);
}

//////////////////////////////////////////////////////////////////////////
// Returns the highest constant G for dt that leaves the meter at or above the floor (S at or below it, in G-LOC or
// for a bad dt); uncapped, the caller caps it
double SOLChase::MaxSafeGForStep(const FSOLGMeterState& meter, const double floorMeter,
    const FSOLPilotGLimits& limits, const double dt)
{
    const double sustained = limits.SustainedG;
    const double exponent = limits.DrainExponent;
    if (meter.bBlackout || !FMath::IsFinite(dt) || dt <= 0.0 || !(meter.Meter > floorMeter)
        || !FMath::IsFinite(exponent) || exponent <= 0.0)
    {
        return sustained;
    }

    // Invert GDrainPerS: the drain over dt equals the budget above the floor
    const double budget = (FMath::Min(meter.Meter, 1.0) - floorMeter) * (1.0 - CHASE_SAFE_STEP_MARGIN);
    const double drainPerS = budget / dt;
    const double normalized = FMath::Pow(drainPerS * FMath::Max(limits.TimeToEmptyAtMaxGS, SOLNewtonian::EPSILON),
        1.0 / exponent);
    const double safeG = sustained + FMath::Max(limits.MaxG - sustained, SOLNewtonian::EPSILON) * normalized;
    return FMath::IsNaN(safeG) ? sustained : FMath::Max(safeG, sustained);
}

//////////////////////////////////////////////////////////////////////////
// Returns the meter the rest of the brake uses net of recovery (bursts at burstG averaging brakePlanG)
double SOLChase::BrakeReserveMeter(const double closingSpeedMps, const double burstG, const double brakePlanG,
    const double brakeAccelMps2, const FSOLPilotGLimits& limits)
{
    const double sustained = limits.SustainedG;
    const double fraction = burstG > sustained && !FMath::IsNaN(brakePlanG)
        ? FMath::Clamp((brakePlanG - sustained) / (burstG - sustained), 0.0, 1.0) : 0.0;
    const double netRate = fraction * SOLNewtonian::GDrainPerS(burstG, limits) - (1.0 - fraction) * limits.RecoverPerS;
    const double speed = FMath::IsNaN(closingSpeedMps) ? 0.0 : FMath::Max(closingSpeedMps, 0.0);
    if (!(netRate > 0.0) || speed <= 0.0)
    {
        return 0.0;
    }
    if (!(brakeAccelMps2 > 0.0))
    {
        return CHASE_INFINITY;
    }
    return netRate * speed / brakeAccelMps2;
}

//////////////////////////////////////////////////////////////////////////
// Returns the guidance budgets of a chase step: plan from limitG, brake from brakePlanG, flip to brake
FSOLGuidanceParams SOLChase::MakeChaseGuidanceParams(const FSOLThrustEnvelope& env, const double limitG,
    const double brakePlanG, const double gravityMps2, const bool bBoost)
{
    FSOLGuidanceParams params;
    params.PlanAccelMps2 = ChaseBudgetMps2(env, limitG, gravityMps2, bBoost);
    params.BrakeAccelMps2 = ChaseBudgetMps2(env, brakePlanG, gravityMps2, bBoost);
    params.FlipTimeS = SOLNewtonian::MakeGuidanceParams(env, params.PlanAccelMps2, ChaseGravity(gravityMps2), true,
        bBoost).FlipTimeS;
    return params;
}

//////////////////////////////////////////////////////////////////////////
// Returns the guidance params of the phase probe that feeds ChaseGLimit (the policy's BurstG and BrakePlanG)
FSOLGuidanceParams SOLChase::MakeChasePhaseProbeParams(const FSOLThrustEnvelope& env, const FSOLChasePolicy& policy,
    const double gravityMps2, const bool bBoost)
{
    return MakeChaseGuidanceParams(env, policy.BurstG, policy.BrakePlanG, gravityMps2, bBoost);
}

//////////////////////////////////////////////////////////////////////////
// Returns one substep's G limit and activity (burst / cool hysteresis, then the cap and the per-step safety clamp)
FSOLChaseGLimit SOLChase::ChaseGLimit(const FSOLChasePolicy& policy, FSOLChaseRunState& inOutRun,
    const FSOLGMeterState& meter, const ESOLGuidancePhase phase, const double closingSpeedMps,
    const double brakeAccelMps2, const double capG, const FSOLPilotGLimits& limits, const double dt)
{
    const double sustained = limits.SustainedG;
    const double floorMeter = ChaseSanitizedFloor(policy.FloorMeter);
    FSOLChaseGLimit result;
    result.ReserveMeter = BrakeReserveMeter(closingSpeedMps, policy.BurstG, policy.BrakePlanG, brakeAccelMps2, limits);
    const double switchMeter = floorMeter + result.ReserveMeter;
    result.ResumeMeter = switchMeter + ChaseSanitizedHysteresis(policy.ResumeHysteresis);

    // G-LOC (an outside load): fly the autopilot's recovery limit and cool off once awake
    if (meter.bBlackout)
    {
        inOutRun.bCooling = true;
        result.LimitG = limits.AutopilotRecoveryG;
        result.Activity = ESOLChaseActivity::Cool;
        inOutRun.LastLimitG = result.LimitG;
        inOutRun.LastActivity = result.Activity;
        return result;
    }

    // Burst / cool hysteresis on the meter; no burst at all when the resume level is out of reach
    if (result.ResumeMeter > 1.0)
    {
        inOutRun.bCooling = true;
    }
    else if (inOutRun.bCooling)
    {
        inOutRun.bCooling = meter.Meter < result.ResumeMeter;
    }
    else
    {
        inOutRun.bCooling = meter.Meter <= switchMeter + SOL::Chase::COOL_SWITCH_TOLERANCE_METER;
    }

    // Activity from the guidance phase (the hold never cools: its clamp keeps it at sustained when low)
    switch (phase)
    {
        case ESOLGuidancePhase::Arrived:
            result.Activity = ESOLChaseActivity::Hold;
            break;
        case ESOLGuidancePhase::Brake:
            result.Activity = inOutRun.bCooling ? ESOLChaseActivity::BrakeCool : ESOLChaseActivity::BrakeBurst;
            break;
        default:
            result.Activity = inOutRun.bCooling ? ESOLChaseActivity::Cool : ESOLChaseActivity::Burst;
            break;
    }

    // Level, the cap, then the per-step safety clamp (bursts and the hold keep the reserve, brake bursts spend it)
    const bool bBursting = result.Activity == ESOLChaseActivity::Burst || result.Activity == ESOLChaseActivity::Hold
        || result.Activity == ESOLChaseActivity::BrakeBurst;
    double limitG = bBursting ? policy.BurstG : sustained;
    limitG = FMath::IsNaN(capG) ? limitG : FMath::Min(limitG, capG);
    if (result.Activity == ESOLChaseActivity::BrakeBurst)
    {
        limitG = FMath::Min(limitG, MaxSafeGForStep(meter, floorMeter, limits, dt));
    }
    else if (bBursting)
    {
        limitG = FMath::Min(limitG, MaxSafeGForStep(meter, switchMeter, limits, dt));
    }
    result.LimitG = FMath::IsNaN(limitG) ? sustained : FMath::Max(limitG, sustained);
    inOutRun.LastLimitG = result.LimitG;
    inOutRun.LastActivity = result.Activity;
    return result;
}

//////////////////////////////////////////////////////////////////////////
// Returns the closed-form, segment-by-segment prediction of a policy's time to intercept and meter
FSOLChasePrediction SOLChase::PredictChase(const FSOLChasePlanInput& input, const FSOLChasePolicy& policy)
{
    FSOLChasePrediction prediction;
    const FSOLPilotGLimits& limits = input.Limits;
    const double startMeter = FMath::IsFinite(input.Meter.Meter) ? FMath::Clamp(input.Meter.Meter, 0.0, 1.0) : 0.0;
    prediction.MinMeter = startMeter;
    prediction.EndMeter = startMeter;
    if (!FMath::IsFinite(input.DistanceM) || input.DistanceM < 0.0 || !FMath::IsFinite(input.ClosingSpeedMps)
        || !FMath::IsFinite(input.Meter.Meter) || FMath::IsNaN(policy.BurstG) || FMath::IsNaN(policy.BrakePlanG))
    {
        return prediction;
    }

    // Already on the tail
    if (input.DistanceM <= input.ArrivalRadiusM && FMath::Abs(input.ClosingSpeedMps) <= input.ArrivalSpeedMps)
    {
        prediction.bIntercepts = true;
        prediction.TimeS = 0.0;
        return prediction;
    }

    // The constants of this policy: levels within [S, cap], budgets, the burst drain and the linear brake reserve
    FSOLChasePredictModel model;
    model.S = limits.SustainedG;
    model.R = limits.RecoverPerS;
    const double capG = ChaseMaxG(input.Envelope, limits, input.bBoost);
    model.BurstG = FMath::Clamp(policy.BurstG, model.S, capG);
    model.BrakePlanG = FMath::Clamp(policy.BrakePlanG, model.S, model.BurstG);
    model.Floor = ChaseSanitizedFloor(policy.FloorMeter);
    model.Hysteresis = ChaseSanitizedHysteresis(policy.ResumeHysteresis);
    model.Gravity = ChaseGravity(input.GravityMps2);
    const FSOLGuidanceParams burst = MakeChaseGuidanceParams(input.Envelope, model.BurstG, model.BrakePlanG,
        model.Gravity, input.bBoost);
    model.BurstAccel = burst.PlanAccelMps2;
    model.BrakeAccel = burst.BrakeAccelMps2;
    model.FlipS = FMath::Max(burst.FlipTimeS, 0.0);
    model.CoolAccel = ChaseBudgetMps2(input.Envelope, model.S, model.Gravity, input.bBoost);
    model.BurstDrain = ChaseNetDrainPerS(ChaseFlownLoadG(model.BurstG, model.BurstAccel, model.Gravity), limits);
    model.ReservePerMps = BrakeReserveMeter(1.0, model.BurstG, model.BrakePlanG, model.BrakeAccel, limits);
    model.CruiseCap = input.CruiseCapMps;
    if (!(model.BrakeAccel > 0.0) || !FMath::IsFinite(model.FlipS) || !(model.CruiseCap > 0.0)
        || !FMath::IsFinite(model.ReservePerMps) || !(model.R >= 0.0))
    {
        return prediction;
    }

    FSOLChasePredictState state;
    state.V = input.ClosingSpeedMps;
    state.Meter = startMeter;
    state.MinMeter = startMeter;
    state.bCooling = input.Meter.bBlackout;
    double distanceM = input.DistanceM;
    const double endSpeed = FMath::Max(FMath::Min(input.ArrivalSpeedMps,
        SOLNewtonian::BrakingSpeedLimitMps(input.ArrivalRadiusM, model.BrakeAccel, model.FlipS)),
        CHASE_MIN_END_SPEED_MPS);

    while (true)
    {
        if (ChaseRunAccelerate(model, state, distanceM) == ESOLChaseRunEnd::Failed)
        {
            return prediction;
        }

        // Faster than the braking curve allows (an initial state): stop at the full budget, overshoot, come back
        const double v = state.V;
        const double remaining = distanceM - state.X;
        const double aBp = model.BrakeAccel;
        if (v * model.FlipS + v * v / (2.0 * aBp) > remaining * (1.0 + CHASE_CURVE_TOLERANCE) + CHASE_CURVE_TOLERANCE)
        {
            const double stopAccel = FMath::Max(model.BurstAccel, aBp);
            const double overshoot = v * v / (2.0 * stopAccel) - remaining;
            if (overshoot > input.ArrivalRadiusM)
            {
                const double stopS = v / stopAccel;
                const double rate = ChaseNetDrainPerS(ChaseFlownLoadG(model.BurstG, stopAccel, model.Gravity), limits);
                state.Meter = FMath::Clamp(state.Meter - rate * stopS, 0.0, 1.0);
                state.MinMeter = FMath::Min(state.MinMeter, state.Meter);
                state.TimeS += stopS;
                distanceM = overshoot;
                state.X = 0.0;
                state.V = 0.0;
                continue;
            }
        }
        if (v <= endSpeed)
        {
            break;
        }

        // The braking curve v = a (u - tf): it takes (v - vEnd) / a + tf ln(v / vEnd) (SDD 18 guidance tail)
        const double tf = model.FlipS;
        const double brakeS = (v - endSpeed) / aBp + tf * FMath::Loge(v / endSpeed);

        // Part of the curve with the load above sustained (deceleration a (1 - tf / u) + gravity above S g0)
        const double threshold = model.S * SOLNewtonian::STANDARD_GRAVITY_MPS2 - model.Gravity;
        double highS = 0.0;
        double drain = 0.0;
        if (threshold < aBp)
        {
            const double uSustained = threshold > 0.0 ? tf / (1.0 - threshold / aBp) : tf;
            const double vSustained = FMath::Max(aBp * (uSustained - tf), endSpeed);
            if (v > vSustained)
            {
                highS = (v - vSustained) / aBp + tf * FMath::Loge(v / vSustained);
                drain = ChaseBrakeDrain(model, limits, v, vSustained);
            }
        }

        // The meter through the brake: the curve's drain, or the duty cycle around the floor when that runs out, or
        // an unfundable brake (the meter below the reserve it needs) falling below the floor
        double meter = state.Meter - drain;
        const double reserve = model.ReservePerMps * v;
        if (reserve > 0.0 && state.Meter < model.Floor + reserve - CHASE_EVENT_EPS)
        {
            meter = FMath::Max(state.Meter - reserve, 0.0);
            state.MinMeter = FMath::Min(state.MinMeter, meter);
        }
        else if (meter >= model.Floor - CHASE_EVENT_EPS)
        {
            state.MinMeter = FMath::Min(state.MinMeter, meter);
        }
        else if (reserve <= 0.0 && model.ReservePerMps == 0.0)
        {
            state.MinMeter = FMath::Min(state.MinMeter, FMath::Min(state.Meter, model.Floor));
            meter = state.Meter >= model.Floor ? FMath::Min(1.0, model.Floor + 0.5 * model.Hysteresis) : state.Meter;
        }
        else
        {
            meter = FMath::Max(meter, 0.0);
            state.MinMeter = FMath::Min(state.MinMeter, meter);
        }
        state.Meter = FMath::Min(1.0, meter + model.R * FMath::Max(brakeS - highS, 0.0));
        state.TimeS += brakeS;
        break;
    }

    prediction.bIntercepts = true;
    prediction.TimeS = state.TimeS;
    prediction.MinMeter = FMath::Max(state.MinMeter, 0.0);
    prediction.EndMeter = FMath::Clamp(state.Meter, 0.0, 1.0);
    return prediction;
}

//////////////////////////////////////////////////////////////////////////
// Returns the fastest feasible policy of the BurstG x BrakePlanG grid (ties to the gentler levels)
FSOLChasePlan SOLChase::PlanChase(const FSOLChasePlanInput& input)
{
    const FSOLPilotGLimits& limits = input.Limits;
    const double sustained = limits.SustainedG;
    FSOLChasePlan plan;
    plan.Policy.BurstG = sustained;
    plan.Policy.BrakePlanG = sustained;
    plan.Policy.FloorMeter = ChaseSanitizedFloor(input.FloorMeter);
    plan.Policy.ResumeHysteresis = SOL::Chase::COOL_HYSTERESIS_METER;
    if (!FMath::IsFinite(input.DistanceM) || input.DistanceM < 0.0 || !FMath::IsFinite(input.ClosingSpeedMps)
        || !FMath::IsFinite(input.Meter.Meter))
    {
        return plan;
    }

    // Predict every pair on the grid S + k (cap - S) / 8 with BrakePlanG <= BurstG (a fixed array on the stack)
    const double capG = ChaseMaxG(input.Envelope, limits, input.bBoost);
    const double stepG = (capG - sustained) / static_cast<double>(SOL::Chase::PLAN_G_LEVELS);
    const double meterBound = FMath::Min(plan.Policy.FloorMeter, FMath::Clamp(input.Meter.Meter, 0.0, 1.0))
        - CHASE_FEASIBLE_TOLERANCE;
    FSOLChasePrediction predictions[CHASE_PLAN_PAIRS];
    bool feasible[CHASE_PLAN_PAIRS];
    double bestTimeS = DBL_MAX;
    int32 pair = 0;
    FSOLChasePolicy candidate = plan.Policy;
    for (int32 burstIndex = 0; burstIndex < CHASE_PLAN_LEVELS; ++burstIndex)
    {
        candidate.BurstG = burstIndex == SOL::Chase::PLAN_G_LEVELS ? capG : sustained + burstIndex * stepG;
        for (int32 brakeIndex = 0; brakeIndex <= burstIndex; ++brakeIndex, ++pair)
        {
            candidate.BrakePlanG = brakeIndex == burstIndex ? candidate.BurstG : sustained + brakeIndex * stepG;
            predictions[pair] = PredictChase(input, candidate);
            feasible[pair] = predictions[pair].bIntercepts && predictions[pair].MinMeter >= meterBound;
            if (feasible[pair])
            {
                bestTimeS = FMath::Min(bestTimeS, predictions[pair].TimeS);
            }
        }
    }
    if (bestTimeS == DBL_MAX)
    {
        plan.Prediction = predictions[0];
        return plan;
    }

    // The gentlest pair (lowest BurstG, then lowest BrakePlanG) within the tie fraction of the fastest
    const double tieTimeS = bestTimeS * (1.0 + SOL::Chase::PLAN_TIE_FRACTION);
    pair = 0;
    for (int32 burstIndex = 0; burstIndex < CHASE_PLAN_LEVELS; ++burstIndex)
    {
        for (int32 brakeIndex = 0; brakeIndex <= burstIndex; ++brakeIndex, ++pair)
        {
            if (feasible[pair] && predictions[pair].TimeS <= tieTimeS)
            {
                plan.bValid = true;
                plan.Policy.BurstG = burstIndex == SOL::Chase::PLAN_G_LEVELS ? capG : sustained + burstIndex * stepG;
                plan.Policy.BrakePlanG = brakeIndex == burstIndex ? plan.Policy.BurstG
                    : sustained + brakeIndex * stepG;
                plan.Prediction = predictions[pair];
                return plan;
            }
        }
    }
    return plan;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the candidate plan should replace the current one
bool SOLChase::ShouldReplacePlan(const FSOLChasePlan& current, const FSOLChasePlan& candidate)
{
    if (!candidate.bValid || !candidate.Prediction.bIntercepts)
    {
        return false;
    }
    if (!current.bValid || !current.Prediction.bIntercepts)
    {
        return true;
    }
    return candidate.Prediction.TimeS < current.Prediction.TimeS * (1.0 - SOL::Chase::REPLAN_IMPROVEMENT_FRACTION);
}

//////////////////////////////////////////////////////////////////////////
// Returns the tail point: behind a moving target along its frame-relative velocity, else on the approach side
FVector3d SOLChase::ChaseAimPointM(const FVector3d& targetM, const FVector3d& targetRelFrameVelocityMps,
    const FVector3d& approachDir, const double trailM)
{
    const double trail = FMath::IsFinite(trailM) && trailM > 0.0 ? trailM : 0.0;
    if (trail == 0.0)
    {
        return targetM;
    }

    // Moving in its frame: trail behind along the motion
    const double speed = ChaseIsFinite(targetRelFrameVelocityMps) ? targetRelFrameVelocityMps.Size() : 0.0;
    if (speed > SOL::Chase::TAIL_MIN_SPEED_MPS)
    {
        return targetM - targetRelFrameVelocityMps / speed * trail;
    }

    // At rest: out along the approach direction (+Z when it is unusable)
    const double length = ChaseIsFinite(approachDir) ? approachDir.Size() : 0.0;
    const FVector3d direction = length > SOLNewtonian::EPSILON && FMath::IsFinite(length) ? approachDir / length
        : FVector3d::UnitZ();
    return targetM + direction * trail;
}

//////////////////////////////////////////////////////////////////////////
// Returns the trail distance max(minTrailM, k x radius); a bad radius or k gives minTrailM
double SOLChase::ChaseTrailM(const double radiusM, const double minTrailM, const double k)
{
    if (!(radiusM >= 0.0) || FMath::IsNaN(k))
    {
        return minTrailM;
    }
    const double scaled = k * radiusM;
    return FMath::IsNaN(scaled) ? minTrailM : FMath::Max(minTrailM, scaled);
}

//////////////////////////////////////////////////////////////////////////
// Returns the low-pass target acceleration estimate from a velocity change, its length clamped to capMps2
FVector3d SOLChase::EstimateTargetAccelMps2(const FVector3d& prevVelocityMps, const FVector3d& velocityMps,
    const FVector3d& prevEstimateMps2, const double dt, const double capMps2)
{
    if (!FMath::IsFinite(dt) || dt <= 0.0 || !ChaseIsFinite(prevVelocityMps) || !ChaseIsFinite(velocityMps))
    {
        return prevEstimateMps2;
    }

    // Exponential blend toward the measured acceleration, time constant TARGET_ACCEL_FILTER_S
    const FVector3d measured = (velocityMps - prevVelocityMps) / dt;
    const double blend = 1.0 - FMath::Exp(-dt / SOL::Chase::TARGET_ACCEL_FILTER_S);
    const FVector3d estimate = prevEstimateMps2 + (measured - prevEstimateMps2) * blend;
    if (!ChaseIsFinite(estimate))
    {
        return prevEstimateMps2;
    }
    const double size = estimate.Size();
    return FMath::IsFinite(capMps2) && capMps2 >= 0.0 && size > capMps2 ? estimate * (capMps2 / size) : estimate;
}
