/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLChase.h"
#include "Flight/SOLNewtonianFlight.h"
#include "SOLConstants.h"

#include "Tests/SOLTestHelpers.h"

#include "HAL/PlatformTime.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#include <cfloat>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #32 (32a) Chase Down pure math, written against the "Chase contract" section of
// Docs/Plans/32-chase-down-autopilot-plan.md (and SDD 32 sections 3.2-3.6) before any production code existed.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Tolerance for exact formulas
    constexpr double CHS_EPS = 1.0e-9;

    // Seed for the randomized property cases
    constexpr int32 CHS_SEED = 20261102;

    // The 1-D execution simulation's step (contract item 7)
    constexpr double CHS_SIM_DT = 0.02;

    // Generous simulated-time cap of one execution run, so a bug cannot hang the test
    constexpr double CHS_SIM_MAX_TIME_S = 4000.0;

    // Short substep used for the per-substep G limit cases
    constexpr double CHS_SUBSTEP_DT = 0.02;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double ChsNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the invalid-input cases
    double ChsInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns standard gravity as the production code names it
    double ChsG0()
    {
        return SOLNewtonian::STANDARD_GRAVITY_MPS2;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a uniform random double in [minValue, maxValue)
    double ChsRange(const FRandomStream& stream, const double minValue, const double maxValue)
    {
        return minValue + (maxValue - minValue) * static_cast<double>(stream.FRand());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a log-uniform random double in [minValue, maxValue) (both positive)
    double ChsLogRange(const FRandomStream& stream, const double minValue, const double maxValue)
    {
        return minValue * FMath::Pow(maxValue / minValue, static_cast<double>(stream.FRand()));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a value is within an absolute tolerance of the expected one (and finite)
    bool ChsNear(const double actual, const double expected, const double tolerance)
    {
        return FMath::IsFinite(actual) && FMath::Abs(actual - expected) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a G meter state with the given meter (conscious unless bBlackout)
    FSOLGMeterState ChsMeter(const double meter, const bool bBlackout = false, const double remainingS = 0.0)
    {
        FSOLGMeterState state;
        state.Meter = meter;
        state.bBlackout = bBlackout;
        state.BlackoutRemainingS = remainingS;
        state.DepletedS = 0.0;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a chase policy with every field set (hysteresis = the contract constant)
    FSOLChasePolicy ChsPolicy(const double burstG, const double brakePlanG, const double floorMeter)
    {
        FSOLChasePolicy policy;
        policy.BurstG = burstG;
        policy.BrakePlanG = brakePlanG;
        policy.FloorMeter = floorMeter;
        policy.ResumeHysteresis = SOL::Chase::COOL_HYSTERESIS_METER;
        return policy;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the sustained-only policy (S, S) for the default pilot and a floor
    FSOLChasePolicy ChsSustainedPolicy(const FSOLPilotGLimits& limits, const double floorMeter)
    {
        return ChsPolicy(limits.SustainedG, limits.SustainedG, floorMeter);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a plan input with every field set: the default player envelope and pilot, no gravity, boost on
    FSOLChasePlanInput ChsInput(const double distanceM, const double closingSpeedMps, const double meter,
        const double floorMeter)
    {
        FSOLChasePlanInput input;
        input.DistanceM = distanceM;
        input.ClosingSpeedMps = closingSpeedMps;
        input.GravityMps2 = 0.0;
        input.CruiseCapMps = SOL::MAX_SPEED_CAP_MPS;
        input.ArrivalRadiusM = 10.0;
        input.ArrivalSpeedMps = 1.0;
        input.Envelope = FSOLThrustEnvelope();
        input.Limits = FSOLPilotGLimits();
        input.Meter = ChsMeter(meter);
        input.FloorMeter = floorMeter;
        input.bBoost = true;
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a plan with every field set (for the replace-rule cases)
    FSOLChasePlan ChsPlan(const bool bValid, const bool bIntercepts, const double timeS)
    {
        FSOLChasePlan plan;
        plan.bValid = bValid;
        plan.Policy = ChsPolicy(1.5, 1.5, 0.25);
        plan.Prediction.bIntercepts = bIntercepts;
        plan.Prediction.TimeS = timeS;
        plan.Prediction.MinMeter = 0.5;
        plan.Prediction.EndMeter = 0.5;
        return plan;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two plans are bit-identical field by field
    bool ChsPlansEqual(const FSOLChasePlan& a, const FSOLChasePlan& b)
    {
        return a.bValid == b.bValid && a.Policy.BurstG == b.Policy.BurstG
            && a.Policy.BrakePlanG == b.Policy.BrakePlanG && a.Policy.FloorMeter == b.Policy.FloorMeter
            && a.Policy.ResumeHysteresis == b.Policy.ResumeHysteresis
            && a.Prediction.bIntercepts == b.Prediction.bIntercepts && a.Prediction.TimeS == b.Prediction.TimeS
            && a.Prediction.MinMeter == b.Prediction.MinMeter && a.Prediction.EndMeter == b.Prediction.EndMeter;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the brake budget a_bp of a policy (the policy's BrakePlanG through the chase guidance params)
    double ChsBrakeAccel(const FSOLThrustEnvelope& env, const FSOLChasePolicy& policy, const double gravityMps2,
        const bool bBoost)
    {
        return SOLChase::MakeChaseGuidanceParams(env, policy.BrakePlanG, policy.BrakePlanG, gravityMps2, bBoost)
            .BrakeAccelMps2;
    }

    // Outcome of one 1-D execution run of a chase policy
    struct FChsSimRun
    {
        bool bArrived = false;
        double TimeS = 0.0;
        double EndMeter = 1.0;
        double MinMeter = 1.0;
        bool bEverBlackout = false;
        bool bFinite = true;
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies a chase policy in 1-D (x axis, no gravity, target at rest) with ChaseGLimit, MakeChaseGuidanceParams,
    // ComputeRendezvousGuidance, ClampToPilotLimit and StepGMeter at a fixed step until the guidance reports Arrived
    FChsSimRun ChsSimulateChase1D(const FSOLChasePlanInput& input, const FSOLChasePolicy& policy, const double dt,
        const double maxTimeS)
    {
        FChsSimRun run;
        const FSOLThrustEnvelope& env = input.Envelope;
        const FSOLPilotGLimits& limits = input.Limits;
        const double capG = SOLChase::ChaseMaxG(env, limits, input.bBoost);
        const double brakeAccel = ChsBrakeAccel(env, policy, 0.0, input.bBoost);
        FSOLChaseRunState chaseRun;
        FSOLGMeterState meter = input.Meter;
        run.EndMeter = meter.Meter;
        run.MinMeter = meter.Meter;

        // Ship at the origin moving at the closing speed along +X, the (aim point of the) target at rest at +distance
        double shipX = 0.0;
        double shipV = input.ClosingSpeedMps;
        const double targetX = input.DistanceM;

        // A probe of the phase with the policy's burst budget, so ChaseGLimit sees this step's guidance phase
        FSOLGuidanceParams probeParams = SOLChase::MakeChaseGuidanceParams(env, policy.BurstG, policy.BrakePlanG, 0.0,
            input.bBoost);
        probeParams.MaxCruiseSpeedMps = input.CruiseCapMps;
        probeParams.ArrivalRadiusM = input.ArrivalRadiusM;
        probeParams.ArrivalSpeedMps = input.ArrivalSpeedMps;

        const int64 maxSteps = static_cast<int64>(FMath::CeilToDouble(maxTimeS / dt));
        for (int64 step = 0; step <= maxSteps; ++step)
        {
            const FVector3d relPos(targetX - shipX, 0.0, 0.0);
            const FVector3d relVel(shipV, 0.0, 0.0);
            const FSOLGuidanceOutput probe = SOLNewtonian::ComputeRendezvousGuidance(relPos, relVel,
                FVector3d::ZeroVector, FVector3d::ZeroVector, probeParams, dt, false);
            if (probe.Phase == ESOLGuidancePhase::Arrived)
            {
                run.bArrived = true;
                run.TimeS = static_cast<double>(step) * dt;
                break;
            }

            // The substep's G limit, then the guidance with the chase budgets for that limit
            const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(policy, chaseRun, meter, probe.Phase,
                probe.ClosingSpeedMps, brakeAccel, capG, limits, dt);
            FSOLGuidanceParams params = SOLChase::MakeChaseGuidanceParams(env, limit.LimitG, policy.BrakePlanG, 0.0,
                input.bBoost);
            params.MaxCruiseSpeedMps = input.CruiseCapMps;
            params.ArrivalRadiusM = input.ArrivalRadiusM;
            params.ArrivalSpeedMps = input.ArrivalSpeedMps;
            const FSOLGuidanceOutput out = SOLNewtonian::ComputeRendezvousGuidance(relPos, relVel,
                FVector3d::ZeroVector, FVector3d::ZeroVector, params, dt, false);
            if (out.Phase == ESOLGuidancePhase::Arrived)
            {
                run.bArrived = true;
                run.TimeS = static_cast<double>(step) * dt;
                break;
            }

            // Thrust clamped to the pilot limit drives the meter and the point mass (semi-implicit Euler)
            const FVector3d thrust = SOLNewtonian::ClampToPilotLimit(out.DesiredThrustAccelMps2,
                limit.LimitG * ChsG0());
            meter = SOLNewtonian::StepGMeter(meter, SOLNewtonian::GLoadG(thrust), limits, dt);
            shipV += thrust.X * dt;
            shipX += shipV * dt;
            if (!FMath::IsFinite(shipX) || !FMath::IsFinite(shipV) || !FMath::IsFinite(meter.Meter))
            {
                run.bFinite = false;
                break;
            }
            run.MinMeter = FMath::Min(run.MinMeter, meter.Meter);
            run.bEverBlackout |= meter.bBlackout;
        }
        run.EndMeter = meter.Meter;
        return run;
    }

    //////////////////////////////////////////////////////////////////////////
    // Flies the plain sustained autopilot in 1-D (x axis, no gravity, target at rest): MakeGuidanceParams at the
    // sustained limit with flip-to-brake, ComputeRendezvousGuidance, ClampToPilotLimit at SustainedG and StepGMeter
    FChsSimRun ChsSimulateSustained1D(const FSOLChasePlanInput& input, const double dt, const double maxTimeS)
    {
        FChsSimRun run;
        const FSOLPilotGLimits& limits = input.Limits;
        const double sustainedMps2 = limits.SustainedG * SOLNewtonian::STANDARD_GRAVITY_MPS2;
        FSOLGuidanceParams params = SOLNewtonian::MakeGuidanceParams(input.Envelope, sustainedMps2, 0.0, true,
            input.bBoost);
        params.MaxCruiseSpeedMps = input.CruiseCapMps;
        params.ArrivalRadiusM = input.ArrivalRadiusM;
        params.ArrivalSpeedMps = input.ArrivalSpeedMps;
        FSOLGMeterState meter = input.Meter;
        run.MinMeter = meter.Meter;

        // Ship at the origin moving at the closing speed along +X, the target at rest at +distance
        double shipX = 0.0;
        double shipV = input.ClosingSpeedMps;
        const double targetX = input.DistanceM;
        const int64 maxSteps = static_cast<int64>(FMath::CeilToDouble(maxTimeS / dt));
        for (int64 step = 0; step <= maxSteps; ++step)
        {
            const FSOLGuidanceOutput out = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(targetX - shipX, 0.0, 0.0),
                FVector3d(shipV, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, dt, false);
            if (out.Phase == ESOLGuidancePhase::Arrived)
            {
                run.bArrived = true;
                run.TimeS = static_cast<double>(step) * dt;
                break;
            }

            // Thrust clamped to sustained drives the meter and the point mass (semi-implicit Euler)
            const FVector3d thrust = SOLNewtonian::ClampToPilotLimit(out.DesiredThrustAccelMps2, sustainedMps2);
            meter = SOLNewtonian::StepGMeter(meter, SOLNewtonian::GLoadG(thrust), limits, dt);
            shipV += thrust.X * dt;
            shipX += shipV * dt;
            if (!FMath::IsFinite(shipX) || !FMath::IsFinite(shipV) || !FMath::IsFinite(meter.Meter))
            {
                run.bFinite = false;
                break;
            }
            run.MinMeter = FMath::Min(run.MinMeter, meter.Meter);
            run.bEverBlackout |= meter.bBlackout;
        }
        run.EndMeter = meter.Meter;
        return run;
    }
}

//////////////////////////////////////////////////////////////////////////
// Constants and type defaults
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseConstantsTest, "SOLTest.Chase.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Checks the contract's SOL::Chase tuning constants and the Chase Down safety margin setting range
bool FSOLChaseConstantsTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("MIN_METER_FLOOR 0.01"), ChsNear(SOL::Chase::MIN_METER_FLOOR, 0.01, CHS_EPS));
    TestTrue(TEXT("COOL_HYSTERESIS_METER 0.15"), ChsNear(SOL::Chase::COOL_HYSTERESIS_METER, 0.15, CHS_EPS));
    TestTrue(TEXT("COOL_SWITCH_TOLERANCE_METER 0.01"),
        ChsNear(SOL::Chase::COOL_SWITCH_TOLERANCE_METER, 0.01, CHS_EPS));
    TestEqual(TEXT("PLAN_G_LEVELS 8"), static_cast<int32>(SOL::Chase::PLAN_G_LEVELS), 8);
    TestTrue(TEXT("PLAN_TIE_FRACTION 0.005"), ChsNear(SOL::Chase::PLAN_TIE_FRACTION, 0.005, CHS_EPS));
    TestTrue(TEXT("REPLAN_INTERVAL_S 0.25"), ChsNear(SOL::Chase::REPLAN_INTERVAL_S, 0.25, CHS_EPS));
    TestTrue(TEXT("REPLAN_METER_DELTA 0.05"), ChsNear(SOL::Chase::REPLAN_METER_DELTA, 0.05, CHS_EPS));
    TestTrue(TEXT("REPLAN_TARGET_DV_FRACTION 0.1"), ChsNear(SOL::Chase::REPLAN_TARGET_DV_FRACTION, 0.1, CHS_EPS));
    TestTrue(TEXT("REPLAN_IMPROVEMENT_FRACTION 0.02"),
        ChsNear(SOL::Chase::REPLAN_IMPROVEMENT_FRACTION, 0.02, CHS_EPS));
    TestTrue(TEXT("TAIL_MIN_SPEED_MPS 1"), ChsNear(SOL::Chase::TAIL_MIN_SPEED_MPS, 1.0, CHS_EPS));
    TestTrue(TEXT("TARGET_TRAIL_M 200"), ChsNear(SOL::Chase::TARGET_TRAIL_M, 200.0, CHS_EPS));
    TestTrue(TEXT("TARGET_TRAIL_RADIUS_FACTOR 3"), ChsNear(SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR, 3.0, CHS_EPS));
    TestTrue(TEXT("REPLAN_MAX_SIM_S 30"), ChsNear(SOL::Chase::REPLAN_MAX_SIM_S, 30.0, CHS_EPS));
    TestTrue(TEXT("TARGET_ACCEL_FILTER_S 0.5"), ChsNear(SOL::Chase::TARGET_ACCEL_FILTER_S, 0.5, CHS_EPS));
    TestEqual(TEXT("PLAN_MAX_SEGMENTS 4096"), static_cast<int32>(SOL::Chase::PLAN_MAX_SEGMENTS), 4096);

    // The settings range (float or double storage, so compared loosely)
    TestTrue(TEXT("CHASE_SAFETY_MARGIN_DEFAULT 0.25"),
        ChsNear(static_cast<double>(SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT), 0.25, 1.0e-6));
    TestTrue(TEXT("CHASE_SAFETY_MARGIN_MIN 0"),
        ChsNear(static_cast<double>(SOL::Settings::CHASE_SAFETY_MARGIN_MIN), 0.0, 1.0e-6));
    TestTrue(TEXT("CHASE_SAFETY_MARGIN_MAX 0.30"),
        ChsNear(static_cast<double>(SOL::Settings::CHASE_SAFETY_MARGIN_MAX), 0.30, 1.0e-6));
    TestTrue(TEXT("CHASE_SAFETY_MARGIN_STEP 0.01"),
        ChsNear(static_cast<double>(SOL::Settings::CHASE_SAFETY_MARGIN_STEP), 0.01, 1.0e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseTypeDefaultsTest, "SOLTest.Chase.TypeDefaults",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Checks the default member values the contract states for the chase types
bool FSOLChaseTypeDefaultsTest::RunTest(const FString& Parameters)
{
    // Non-const: the contract gives no initializers for the other fields, so these are only read where it does
    FSOLChasePolicy policy;
    TestTrue(TEXT("Policy hysteresis defaults to COOL_HYSTERESIS_METER"),
        ChsNear(policy.ResumeHysteresis, SOL::Chase::COOL_HYSTERESIS_METER, CHS_EPS));

    FSOLChaseRunState run;
    TestFalse(TEXT("Run state: not cooling"), run.bCooling);
    TestTrue(TEXT("Run state: last limit 0"), ChsNear(run.LastLimitG, 0.0, CHS_EPS));
    TestTrue(TEXT("Run state: last activity Burst"), run.LastActivity == ESOLChaseActivity::Burst);

    FSOLChasePlanInput input;
    TestTrue(TEXT("Plan input: arrival radius 10 m"), ChsNear(input.ArrivalRadiusM, 10.0, CHS_EPS));
    TestTrue(TEXT("Plan input: arrival speed 1 m/s"), ChsNear(input.ArrivalSpeedMps, 1.0, CHS_EPS));
    TestTrue(TEXT("Plan input: boost on by default"), input.bBoost);

    // The five activities exist and are distinct
    TestTrue(TEXT("Activities distinct"), ESOLChaseActivity::Burst != ESOLChaseActivity::Cool
        && ESOLChaseActivity::Cool != ESOLChaseActivity::BrakeBurst
        && ESOLChaseActivity::BrakeBurst != ESOLChaseActivity::BrakeCool
        && ESOLChaseActivity::BrakeCool != ESOLChaseActivity::Hold);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 1. ChaseMaxG
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseMaxGTest, "SOLTest.Chase.ChaseMaxG",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The cap is min(pilot max, forward thrust (boosted only with boost on), total cap when > 0), at least sustained
bool FSOLChaseMaxGTest::RunTest(const FString& Parameters)
{
    const double g0 = ChsG0();

    // Default player: expected values derived from the envelope and limit fields, never a literal
    const FSOLThrustEnvelope env;
    const FSOLPilotGLimits limits;
    const double boostedForwardG = env.ForwardAccelMps2 * env.BoostMultiplier / g0;
    const double totalCapG = env.MaxTotalAccelMps2 / g0;
    const double expectedBoostOn = FMath::Max(limits.SustainedG,
        FMath::Min(limits.MaxG, FMath::Min(boostedForwardG, totalCapG)));
    const double expectedBoostOff = FMath::Max(limits.SustainedG,
        FMath::Min(limits.MaxG, FMath::Min(env.ForwardAccelMps2 / g0, totalCapG)));
    TestTrue(TEXT("Default player, boost on: min of the fields"),
        ChsNear(SOLChase::ChaseMaxG(env, limits, true), expectedBoostOn, CHS_EPS));
    TestTrue(TEXT("Default player, boost on: equals the pilot's MaxG (all three agree)"),
        ChsNear(SOLChase::ChaseMaxG(env, limits, true), limits.MaxG, CHS_EPS));
    TestTrue(TEXT("Default player, boost off: the unboosted forward thrust"),
        ChsNear(SOLChase::ChaseMaxG(env, limits, false), expectedBoostOff, CHS_EPS));
    TestTrue(TEXT("Default player, boost off: equals forward / g0"),
        ChsNear(SOLChase::ChaseMaxG(env, limits, false), env.ForwardAccelMps2 / g0, CHS_EPS));

    // An upgraded pilot (M 12) with forward 5 G x3 and a 20 G total cap
    FSOLPilotGLimits limits12 = limits;
    limits12.MaxG = 12.0;
    FSOLThrustEnvelope env5 = env;
    env5.ForwardAccelMps2 = 5.0 * g0;
    env5.BoostMultiplier = 3.0;
    env5.MaxTotalAccelMps2 = 20.0 * g0;
    TestTrue(TEXT("M 12, forward 5 G x3, cap 20 G, boost on: 12"),
        ChsNear(SOLChase::ChaseMaxG(env5, limits12, true), 12.0, CHS_EPS));
    TestTrue(TEXT("M 12, forward 5 G x3, cap 20 G, boost off: 5"),
        ChsNear(SOLChase::ChaseMaxG(env5, limits12, false), 5.0, CHS_EPS));

    // The total cap binds
    FSOLThrustEnvelope envCap10 = env5;
    envCap10.MaxTotalAccelMps2 = 10.0 * g0;
    TestTrue(TEXT("M 12, cap 10 G, boost on: 10"),
        ChsNear(SOLChase::ChaseMaxG(envCap10, limits12, true), 10.0, CHS_EPS));

    // A total cap <= 0 means none
    FSOLPilotGLimits limits20 = limits;
    limits20.MaxG = 20.0;
    FSOLThrustEnvelope envNoCap = env5;
    envNoCap.MaxTotalAccelMps2 = 0.0;
    TestTrue(TEXT("Cap 0 ignored: forward 5 G x3 binds at 15"),
        ChsNear(SOLChase::ChaseMaxG(envNoCap, limits20, true), 15.0, CHS_EPS));
    envNoCap.MaxTotalAccelMps2 = -1.0;
    TestTrue(TEXT("Negative cap ignored: forward 5 G x3 binds at 15"),
        ChsNear(SOLChase::ChaseMaxG(envNoCap, limits20, true), 15.0, CHS_EPS));
    envNoCap.MaxTotalAccelMps2 = 0.0;
    TestTrue(TEXT("Cap 0 ignored: M 12 binds"),
        ChsNear(SOLChase::ChaseMaxG(envNoCap, limits12, true), 12.0, CHS_EPS));

    // No boost hardware: boost on changes nothing
    FSOLThrustEnvelope envNoBoost = env;
    envNoBoost.BoostMultiplier = 1.0;
    envNoBoost.ForwardAccelMps2 = 3.0 * g0;
    TestTrue(TEXT("BoostMultiplier 1, forward 3 G, boost on: 3"),
        ChsNear(SOLChase::ChaseMaxG(envNoBoost, limits, true), 3.0, CHS_EPS));

    // Never below sustained
    FSOLPilotGLimits limitsLowM = limits;
    limitsLowM.MaxG = limits.SustainedG - 0.5;
    TestTrue(TEXT("M below S: S"),
        ChsNear(SOLChase::ChaseMaxG(env, limitsLowM, true), limitsLowM.SustainedG, CHS_EPS));
    FSOLPilotGLimits limitsNaNM = limits;
    limitsNaNM.MaxG = ChsNaN();
    TestTrue(TEXT("NaN M: S"),
        ChsNear(SOLChase::ChaseMaxG(env, limitsNaNM, true), limitsNaNM.SustainedG, CHS_EPS));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 2. EffectiveMeterFloor
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseEffectiveMeterFloorTest, "SOLTest.Chase.EffectiveMeterFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The margin is clamped to [0, 0.3], kept at least the 1% technical floor, and NaN gives the 0.25 default
bool FSOLChaseEffectiveMeterFloorTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("0 -> 0.01"), ChsNear(SOLChase::EffectiveMeterFloor(0.0), 0.01, CHS_EPS));
    TestTrue(TEXT("0.1 -> 0.1"), ChsNear(SOLChase::EffectiveMeterFloor(0.1), 0.1, CHS_EPS));
    TestTrue(TEXT("0.25 -> 0.25"), ChsNear(SOLChase::EffectiveMeterFloor(0.25), 0.25, CHS_EPS));
    TestTrue(TEXT("0.3 -> 0.3"), ChsNear(SOLChase::EffectiveMeterFloor(0.3), 0.3, CHS_EPS));
    TestTrue(TEXT("0.5 -> 0.3"), ChsNear(SOLChase::EffectiveMeterFloor(0.5), 0.3, CHS_EPS));
    TestTrue(TEXT("-1 -> 0.01"), ChsNear(SOLChase::EffectiveMeterFloor(-1.0), 0.01, CHS_EPS));
    TestTrue(TEXT("NaN -> 0.25"), ChsNear(SOLChase::EffectiveMeterFloor(ChsNaN()), 0.25, CHS_EPS));
    TestTrue(TEXT("Technical floor is MIN_METER_FLOOR"),
        ChsNear(SOLChase::EffectiveMeterFloor(0.0), SOL::Chase::MIN_METER_FLOOR, CHS_EPS));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 3. MaxSafeGForStep
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseMaxSafeGForStepTest, "SOLTest.Chase.MaxSafeGForStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The largest constant G whose drain over dt leaves the meter at the floor; S at or below the floor or for bad input
bool FSOLChaseMaxSafeGForStepTest::RunTest(const FString& Parameters)
{
    const FSOLPilotGLimits limits;
    const double S = limits.SustainedG;
    const FSOLGMeterState full = ChsMeter(1.0);

    // Worked values for the default pilot (full meter, floor 0.1)
    TestTrue(TEXT("Full, floor 0.1, dt 10 -> 8.615"),
        ChsNear(SOLChase::MaxSafeGForStep(full, 0.1, limits, 10.0), 8.615, 1.0e-3));
    const double oneSecond = SOLChase::MaxSafeGForStep(full, 0.1, limits, 1.0);
    TestTrue(TEXT("Full, floor 0.1, dt 1 -> 24 (uncapped)"), ChsNear(oneSecond, 24.0, 1.0e-3));
    TestTrue(TEXT("dt 1 result is above MaxG (the caller caps)"), oneSecond > limits.MaxG);
    TestTrue(TEXT("Full, floor 0.1, dt 169 -> 3.23"),
        ChsNear(SOLChase::MaxSafeGForStep(full, 0.1, limits, 169.0), 3.23, 0.01));

    // At or below the floor, in G-LOC and for bad dt: sustained
    TestTrue(TEXT("Meter = floor -> S"), ChsNear(SOLChase::MaxSafeGForStep(ChsMeter(0.25), 0.25, limits, 1.0),
        S, CHS_EPS));
    TestTrue(TEXT("Meter below floor -> S"), ChsNear(SOLChase::MaxSafeGForStep(ChsMeter(0.1), 0.25, limits, 1.0),
        S, CHS_EPS));
    TestTrue(TEXT("G-LOC -> S"), ChsNear(SOLChase::MaxSafeGForStep(ChsMeter(0.0, true, 3.0), 0.1, limits, 1.0),
        S, CHS_EPS));
    TestTrue(TEXT("G-LOC with a recovering meter -> S"),
        ChsNear(SOLChase::MaxSafeGForStep(ChsMeter(0.15, true, 1.0), 0.1, limits, 1.0), S, CHS_EPS));
    TestTrue(TEXT("dt 0 -> S"), ChsNear(SOLChase::MaxSafeGForStep(full, 0.1, limits, 0.0), S, CHS_EPS));
    TestTrue(TEXT("dt negative -> S"), ChsNear(SOLChase::MaxSafeGForStep(full, 0.1, limits, -1.0), S, CHS_EPS));
    TestTrue(TEXT("dt NaN -> S"), ChsNear(SOLChase::MaxSafeGForStep(full, 0.1, limits, ChsNaN()), S, CHS_EPS));
    TestTrue(TEXT("dt infinite -> S"), ChsNear(SOLChase::MaxSafeGForStep(full, 0.1, limits, ChsInf()), S, CHS_EPS));

    // Round trip over a grid: stepping the meter at the result never ends below the floor (from at or above it)
    // and never enters G-LOC; the result is never below S
    const double meters[] = { 0.05, 0.1, 0.2, 0.35, 0.5, 0.75, 0.9, 1.0 };
    const double floors[] = { 0.01, 0.05, 0.1, 0.2, 0.25, 0.3 };
    const double dts[] = { 0.01, 0.02, 0.1, 1.0, 10.0, 60.0, 169.0, 300.0 };
    int32 failures = 0;
    int32 belowS = 0;
    for (const double meter : meters)
    {
        for (const double floorMeter : floors)
        {
            for (const double dt : dts)
            {
                const FSOLGMeterState start = ChsMeter(meter);
                const double safeG = SOLChase::MaxSafeGForStep(start, floorMeter, limits, dt);
                if (!FMath::IsFinite(safeG) || safeG < S - CHS_EPS)
                {
                    ++belowS;
                    continue;
                }
                const FSOLGMeterState end = SOLNewtonian::StepGMeter(start, safeG, limits, dt);
                // From under the floor the pilot only has to not go lower
                const double bound = meter >= floorMeter ? floorMeter : meter;
                if (end.bBlackout || end.Meter < bound - CHS_EPS)
                {
                    ++failures;
                    AddInfo(FString::Printf(TEXT("Round trip: meter %.2f floor %.2f dt %.2f -> G %.4f, end %.12f"),
                        meter, floorMeter, dt, safeG, end.Meter));
                }
            }
        }
    }
    TestEqual(TEXT("Round trip never below the floor nor G-LOC"), failures, 0);
    TestEqual(TEXT("Never below S"), belowS, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 4. BrakeReserveMeter
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseBrakeReserveMeterTest, "SOLTest.Chase.BrakeReserveMeter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The meter the rest of the brake uses net of recovery, from the duty cycle fraction and the brake duration
bool FSOLChaseBrakeReserveMeterTest::RunTest(const FString& Parameters)
{
    const FSOLPilotGLimits limits;
    const double S = limits.SustainedG;
    const double g0 = ChsG0();

    TestTrue(TEXT("Burst 9, plan 9, 300 m/s, a_bp 79.434 -> 0.3777"),
        ChsNear(SOLChase::BrakeReserveMeter(300.0, 9.0, 9.0, 79.434, limits), 0.3777, 1.0e-3));
    TestTrue(TEXT("Burst 9, plan 5.25, 300 m/s, a_bp 46.337 -> 0.1619"),
        ChsNear(SOLChase::BrakeReserveMeter(300.0, 9.0, 5.25, 46.337, limits), 0.1619, 1.0e-3));
    TestTrue(TEXT("Burst 9, plan 3.0 (sustainable duty) -> 0"),
        ChsNear(SOLChase::BrakeReserveMeter(300.0, 9.0, 3.0, 0.9 * 3.0 * g0, limits), 0.0, CHS_EPS));
    TestTrue(TEXT("Plan S -> 0"),
        ChsNear(SOLChase::BrakeReserveMeter(300.0, 9.0, S, 0.9 * S * g0, limits), 0.0, CHS_EPS));
    TestTrue(TEXT("Receding (v < 0) -> 0"),
        ChsNear(SOLChase::BrakeReserveMeter(-300.0, 9.0, 9.0, 79.434, limits), 0.0, CHS_EPS));
    TestTrue(TEXT("At rest (v 0) -> 0"),
        ChsNear(SOLChase::BrakeReserveMeter(0.0, 9.0, 9.0, 79.434, limits), 0.0, CHS_EPS));
    TestTrue(TEXT("Burst at S (f = 0) -> 0"),
        ChsNear(SOLChase::BrakeReserveMeter(300.0, S, S, 0.9 * S * g0, limits), 0.0, CHS_EPS));
    TestTrue(TEXT("Burst below S (f = 0) -> 0"),
        ChsNear(SOLChase::BrakeReserveMeter(300.0, S - 0.5, S, 10.0, limits), 0.0, CHS_EPS));

    // f is clamped to 1 when the plan is above the burst
    TestTrue(TEXT("Plan above burst: f clamped to 1"),
        ChsNear(SOLChase::BrakeReserveMeter(300.0, 9.0, 12.0, 79.434, limits),
            SOLChase::BrakeReserveMeter(300.0, 9.0, 9.0, 79.434, limits), CHS_EPS));

    // The formula itself, against the production meter drain
    const double f = (5.25 - S) / (9.0 - S);
    const double expected = FMath::Max(0.0, f * SOLNewtonian::GDrainPerS(9.0, limits) - (1.0 - f) * limits.RecoverPerS)
        * 300.0 / 46.337;
    TestTrue(TEXT("Matches the contract formula"),
        ChsNear(SOLChase::BrakeReserveMeter(300.0, 9.0, 5.25, 46.337, limits), expected, CHS_EPS));

    // No brake budget with a positive net drain: an unfundable brake
    const double noBrake = SOLChase::BrakeReserveMeter(300.0, 9.0, 9.0, 0.0, limits);
    TestTrue(TEXT("a_bp 0 with a positive rate -> +inf"), noBrake == ChsInf());
    const double negBrake = SOLChase::BrakeReserveMeter(300.0, 9.0, 9.0, -5.0, limits);
    TestTrue(TEXT("a_bp negative with a positive rate -> +inf"), negBrake == ChsInf());
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 5. MakeChaseGuidanceParams
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseMakeGuidanceParamsTest, "SOLTest.Chase.MakeChaseGuidanceParams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The plan and brake budgets come from the two G levels (capped by the envelope), the flip from MakeGuidanceParams
bool FSOLChaseMakeGuidanceParamsTest::RunTest(const FString& Parameters)
{
    const double g0 = ChsG0();
    const FSOLThrustEnvelope env;

    // Sustained levels match the plain autopilot's budgets and flip time
    const FSOLGuidanceParams reference = SOLNewtonian::MakeGuidanceParams(env, 1.5 * g0, 0.0, true, true);
    const FSOLGuidanceParams sustained = SOLChase::MakeChaseGuidanceParams(env, 1.5, 1.5, 0.0, true);
    TestTrue(TEXT("1.5 G: plan equals MakeGuidanceParams"),
        ChsNear(sustained.PlanAccelMps2, reference.PlanAccelMps2, CHS_EPS));
    TestTrue(TEXT("1.5 G: brake equals MakeGuidanceParams' plan"),
        ChsNear(sustained.BrakeAccelMps2, reference.PlanAccelMps2, CHS_EPS));
    TestTrue(TEXT("1.5 G: flip time equals MakeGuidanceParams (flip to brake)"),
        ChsNear(sustained.FlipTimeS, reference.FlipTimeS, CHS_EPS));

    // Other fields keep their defaults
    const FSOLGuidanceParams defaults;
    TestTrue(TEXT("Cruise cap default"), sustained.MaxCruiseSpeedMps == defaults.MaxCruiseSpeedMps);
    TestTrue(TEXT("Arrival radius default"), sustained.ArrivalRadiusM == defaults.ArrivalRadiusM);
    TestTrue(TEXT("Arrival speed default"), sustained.ArrivalSpeedMps == defaults.ArrivalSpeedMps);
    TestTrue(TEXT("Velocity gain default"), sustained.VelocityGainPerS == defaults.VelocityGainPerS);
    TestTrue(TEXT("Pass speed default"), sustained.PassSpeedMps == defaults.PassSpeedMps);
    TestTrue(TEXT("Capture radius default"), sustained.CaptureRadiusM == defaults.CaptureRadiusM);

    // 9 G boosted: 0.9 x 9 g0 (the contract's 88.26 is 9 g0 rounded)
    const FSOLGuidanceParams nine = SOLChase::MakeChaseGuidanceParams(env, 9.0, 9.0, 0.0, true);
    TestTrue(TEXT("Limit 9 G, boost on: plan 0.9 x 9 g0"), ChsNear(nine.PlanAccelMps2, 0.9 * 9.0 * g0, 1.0e-6));
    TestTrue(TEXT("Limit 9 G, boost on: brake 0.9 x 9 g0"), ChsNear(nine.BrakeAccelMps2, 0.9 * 9.0 * g0, 1.0e-6));

    // Boost off: the unboosted forward thrust caps it
    const FSOLGuidanceParams nineNoBoost = SOLChase::MakeChaseGuidanceParams(env, 9.0, 9.0, 0.0, false);
    TestTrue(TEXT("Limit 9 G, boost off: plan capped at 0.9 x forward"),
        ChsNear(nineNoBoost.PlanAccelMps2, 0.9 * env.ForwardAccelMps2, 1.0e-6));
    TestTrue(TEXT("Limit 9 G, boost off: brake capped at 0.9 x forward"),
        ChsNear(nineNoBoost.BrakeAccelMps2, 0.9 * env.ForwardAccelMps2, 1.0e-6));
    TestTrue(TEXT("Boost off flip equals MakeGuidanceParams boost off"),
        ChsNear(nineNoBoost.FlipTimeS, SOLNewtonian::MakeGuidanceParams(env, 9.0 * g0, 0.0, true, false).FlipTimeS,
            CHS_EPS));

    // Gravity above 90% of the budget: the 10% floor
    const double gravity = 0.95 * 1.5 * g0;
    const FSOLGuidanceParams heavy = SOLChase::MakeChaseGuidanceParams(env, 1.5, 1.5, gravity, true);
    TestTrue(TEXT("Gravity above 0.9 a: plan at 10%"), ChsNear(heavy.PlanAccelMps2, 0.1 * 1.5 * g0, 1.0e-9));
    TestTrue(TEXT("Gravity above 0.9 a: brake at 10%"), ChsNear(heavy.BrakeAccelMps2, 0.1 * 1.5 * g0, 1.0e-9));

    // Gravity below 90%: subtracted
    const FSOLGuidanceParams light = SOLChase::MakeChaseGuidanceParams(env, 3.0, 3.0, 2.0, true);
    TestTrue(TEXT("Moderate gravity subtracted"), ChsNear(light.PlanAccelMps2, 0.9 * 3.0 * g0 - 2.0, 1.0e-9));

    // The two levels give two budgets
    const FSOLGuidanceParams split = SOLChase::MakeChaseGuidanceParams(env, 9.0, 3.0, 0.0, true);
    TestTrue(TEXT("Plan from the limit"), ChsNear(split.PlanAccelMps2, 0.9 * 9.0 * g0, 1.0e-6));
    TestTrue(TEXT("Brake from the brake plan level"), ChsNear(split.BrakeAccelMps2, 0.9 * 3.0 * g0, 1.0e-6));
    TestTrue(TEXT("Brake differs from plan when the levels differ"),
        FMath::Abs(split.PlanAccelMps2 - split.BrakeAccelMps2) > 1.0);

    // Above the envelope: capped by boosted forward and the total cap
    const double envelopeCap = FMath::Min(env.ForwardAccelMps2 * env.BoostMultiplier, env.MaxTotalAccelMps2);
    const FSOLGuidanceParams twenty = SOLChase::MakeChaseGuidanceParams(env, 20.0, 20.0, 0.0, true);
    TestTrue(TEXT("Limit 20 G, boost on: capped at the envelope (9 G)"),
        ChsNear(twenty.PlanAccelMps2, 0.9 * envelopeCap, 1.0e-6));
    TestTrue(TEXT("Limit 20 G, boost on: brake capped at the envelope"),
        ChsNear(twenty.BrakeAccelMps2, 0.9 * envelopeCap, 1.0e-6));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 6. ChaseGLimit
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseGLimitTest, "SOLTest.Chase.ChaseGLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Burst / cool switching with hysteresis, activities per phase, the caps and the per-step safety clamp
bool FSOLChaseGLimitTest::RunTest(const FString& Parameters)
{
    const FSOLPilotGLimits limits;
    const FSOLThrustEnvelope env;
    const double S = limits.SustainedG;
    const double capG = SOLChase::ChaseMaxG(env, limits, true);
    const double dt = CHS_SUBSTEP_DT;
    const FSOLChasePolicy policy = ChsPolicy(9.0, 9.0, 0.1);
    const double aBp = 79.434;

    // Full meter, accelerating, at rest relative to the target (no reserve): Burst at BurstG
    {
        FSOLChaseRunState run;
        const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(policy, run, ChsMeter(1.0), ESOLGuidancePhase::Accelerate,
            0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("Full: Burst"), limit.Activity == ESOLChaseActivity::Burst);
        TestTrue(TEXT("Full: BurstG"), ChsNear(limit.LimitG, policy.BurstG, CHS_EPS));
        TestFalse(TEXT("Full: not cooling"), run.bCooling);
        TestTrue(TEXT("Full: reserve 0 at rest"), ChsNear(limit.ReserveMeter, 0.0, CHS_EPS));
        TestTrue(TEXT("Full: resume = floor + hysteresis"),
            ChsNear(limit.ResumeMeter, policy.FloorMeter + SOL::Chase::COOL_HYSTERESIS_METER, CHS_EPS));
        TestTrue(TEXT("Full: run state records the limit"), ChsNear(run.LastLimitG, limit.LimitG, CHS_EPS));
        TestTrue(TEXT("Full: run state records the activity"), run.LastActivity == limit.Activity);
    }

    // Cruise is treated as the accelerate phase
    {
        FSOLChaseRunState run;
        const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(policy, run, ChsMeter(1.0), ESOLGuidancePhase::Cruise,
            0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("Cruise, full: Burst at BurstG"), limit.Activity == ESOLChaseActivity::Burst
            && ChsNear(limit.LimitG, policy.BurstG, CHS_EPS));
    }

    // Meter at floor + reserve (reserve 0): start cooling
    {
        FSOLChaseRunState run;
        const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(policy, run, ChsMeter(policy.FloorMeter),
            ESOLGuidancePhase::Accelerate, 0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("At floor: Cool"), limit.Activity == ESOLChaseActivity::Cool);
        TestTrue(TEXT("At floor: S"), ChsNear(limit.LimitG, S, CHS_EPS));
        TestTrue(TEXT("At floor: cooling set"), run.bCooling);
        TestTrue(TEXT("At floor: run state records Cool"), run.LastActivity == ESOLChaseActivity::Cool
            && ChsNear(run.LastLimitG, S, CHS_EPS));
    }

    // Within the switch tolerance above floor + reserve (reserve 0) while bursting: start cooling
    {
        FSOLChaseRunState run;
        const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(policy, run, ChsMeter(policy.FloorMeter + 0.005),
            ESOLGuidancePhase::Accelerate, 0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("Floor + 0.005 while bursting: Cool at S"),
            limit.Activity == ESOLChaseActivity::Cool && ChsNear(limit.LimitG, S, CHS_EPS));
        TestTrue(TEXT("Floor + 0.005 while bursting: cooling set"), run.bCooling);
    }

    // With a non-zero reserve: the switch is at floor + reserve
    {
        const FSOLChasePolicy split = ChsPolicy(9.0, 5.25, 0.1);
        const double reserve = SOLChase::BrakeReserveMeter(300.0, split.BurstG, split.BrakePlanG, 46.337, limits);
        FSOLChaseRunState run;
        const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(split, run, ChsMeter(0.25),
            ESOLGuidancePhase::Accelerate, 300.0, 46.337, capG, limits, dt);
        TestTrue(TEXT("Reserve: equals BrakeReserveMeter"), ChsNear(limit.ReserveMeter, reserve, CHS_EPS));
        TestTrue(TEXT("Reserve: resume = floor + reserve + hysteresis"), ChsNear(limit.ResumeMeter,
            split.FloorMeter + reserve + SOL::Chase::COOL_HYSTERESIS_METER, CHS_EPS));
        TestTrue(TEXT("Reserve: meter 0.25 <= floor + 0.162 -> Cool at S"),
            limit.Activity == ESOLChaseActivity::Cool && ChsNear(limit.LimitG, S, CHS_EPS) && run.bCooling);
    }

    // Cooling below resume keeps cooling; reaching resume bursts again
    {
        FSOLChaseRunState run;
        run.bCooling = true;
        const FSOLChaseGLimit below = SOLChase::ChaseGLimit(policy, run, ChsMeter(0.2),
            ESOLGuidancePhase::Accelerate, 0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("Cooling below resume: Cool at S"),
            below.Activity == ESOLChaseActivity::Cool && ChsNear(below.LimitG, S, CHS_EPS));
        TestTrue(TEXT("Cooling below resume: still cooling"), run.bCooling);

        const double resume = policy.FloorMeter + SOL::Chase::COOL_HYSTERESIS_METER;
        const FSOLChaseGLimit reached = SOLChase::ChaseGLimit(policy, run, ChsMeter(resume + 1.0e-9),
            ESOLGuidancePhase::Accelerate, 0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("Reaching resume: Burst"), reached.Activity == ESOLChaseActivity::Burst);
        TestTrue(TEXT("Reaching resume: BurstG"), ChsNear(reached.LimitG, policy.BurstG, CHS_EPS));
        TestFalse(TEXT("Reaching resume: cooling cleared"), run.bCooling);
    }

    // Brake phase: BrakeBurst when not cooling, BrakeCool when cooling
    {
        FSOLChaseRunState run;
        const FSOLChaseGLimit burst = SOLChase::ChaseGLimit(policy, run, ChsMeter(1.0), ESOLGuidancePhase::Brake,
            0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("Brake, not cooling: BrakeBurst at BurstG"),
            burst.Activity == ESOLChaseActivity::BrakeBurst && ChsNear(burst.LimitG, policy.BurstG, CHS_EPS));

        FSOLChaseRunState cooling;
        cooling.bCooling = true;
        const FSOLChaseGLimit cool = SOLChase::ChaseGLimit(policy, cooling, ChsMeter(0.2), ESOLGuidancePhase::Brake,
            0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("Brake, cooling: BrakeCool at S"),
            cool.Activity == ESOLChaseActivity::BrakeCool && ChsNear(cool.LimitG, S, CHS_EPS));
    }

    // Arrived: Hold at BurstG
    {
        FSOLChaseRunState run;
        const FSOLChaseGLimit hold = SOLChase::ChaseGLimit(policy, run, ChsMeter(1.0), ESOLGuidancePhase::Arrived,
            0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("Arrived: Hold"), hold.Activity == ESOLChaseActivity::Hold);
        TestTrue(TEXT("Arrived: BurstG"), ChsNear(hold.LimitG, policy.BurstG, CHS_EPS));
    }

    // Meter below the floor: S in every phase, cooling or not
    {
        const ESOLGuidancePhase phases[] = { ESOLGuidancePhase::Accelerate, ESOLGuidancePhase::Cruise,
            ESOLGuidancePhase::Brake, ESOLGuidancePhase::Arrived };
        int32 notSustained = 0;
        for (const ESOLGuidancePhase phase : phases)
        {
            for (int32 cooling = 0; cooling < 2; ++cooling)
            {
                FSOLChaseRunState run;
                run.bCooling = cooling != 0;
                const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(policy, run, ChsMeter(0.05), phase, 0.0, aBp,
                    capG, limits, dt);
                notSustained += ChsNear(limit.LimitG, S, CHS_EPS) ? 0 : 1;
            }
        }
        TestEqual(TEXT("Below the floor: S in every activity"), notSustained, 0);
    }

    // BurstG above the cap: the cap
    {
        const double lowCap = 6.0;
        FSOLChaseRunState run;
        const FSOLChaseGLimit burst = SOLChase::ChaseGLimit(policy, run, ChsMeter(1.0),
            ESOLGuidancePhase::Accelerate, 0.0, aBp, lowCap, limits, dt);
        TestTrue(TEXT("BurstG above the cap: Burst at the cap"),
            burst.Activity == ESOLChaseActivity::Burst && ChsNear(burst.LimitG, lowCap, CHS_EPS));
        FSOLChaseRunState brakeRun;
        const FSOLChaseGLimit brake = SOLChase::ChaseGLimit(policy, brakeRun, ChsMeter(1.0),
            ESOLGuidancePhase::Brake, 0.0, aBp, lowCap, limits, dt);
        TestTrue(TEXT("BurstG above the cap: BrakeBurst at the cap"), ChsNear(brake.LimitG, lowCap, CHS_EPS));
    }

    // floor + reserve + hysteresis > 1: no burst possible, Cool at S from a full meter
    {
        const FSOLChasePolicy highFloor = ChsPolicy(9.0, 9.0, 0.3);
        const double speed = 6.0 * aBp;   // reserve 0.1/s x 6 s = 0.6: floor + reserve 0.9, resume 1.05
        FSOLChaseRunState run;
        const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(highFloor, run, ChsMeter(1.0),
            ESOLGuidancePhase::Accelerate, speed, aBp, capG, limits, dt);
        TestTrue(TEXT("Resume above 1: reserve as stated"), ChsNear(limit.ReserveMeter, 0.6, 1.0e-3));
        TestTrue(TEXT("Resume above 1: resume > 1"), limit.ResumeMeter > 1.0);
        TestTrue(TEXT("Resume above 1: Cool"), limit.Activity == ESOLChaseActivity::Cool);
        TestTrue(TEXT("Resume above 1: S"), ChsNear(limit.LimitG, S, CHS_EPS));
    }

    // The per-step safety clamp: Burst against floor + reserve, BrakeBurst against the floor
    {
        FSOLChaseRunState run;
        const FSOLChaseGLimit longStep = SOLChase::ChaseGLimit(policy, run, ChsMeter(1.0),
            ESOLGuidancePhase::Accelerate, 0.0, aBp, capG, limits, 10.0);
        TestTrue(TEXT("Burst, dt 10, no reserve: MaxSafeGForStep(1, floor)"), ChsNear(longStep.LimitG,
            SOLChase::MaxSafeGForStep(ChsMeter(1.0), policy.FloorMeter, limits, 10.0), CHS_EPS));

        const double reserve = SOLChase::BrakeReserveMeter(300.0, policy.BurstG, policy.BrakePlanG, aBp, limits);
        FSOLChaseRunState burstRun;
        const FSOLChaseGLimit burst = SOLChase::ChaseGLimit(policy, burstRun, ChsMeter(1.0),
            ESOLGuidancePhase::Accelerate, 300.0, aBp, capG, limits, 10.0);
        TestTrue(TEXT("Burst, dt 10, reserve: clamped against floor + reserve"), ChsNear(burst.LimitG,
            SOLChase::MaxSafeGForStep(ChsMeter(1.0), policy.FloorMeter + reserve, limits, 10.0), CHS_EPS));

        FSOLChaseRunState holdRun;
        const FSOLChaseGLimit hold = SOLChase::ChaseGLimit(policy, holdRun, ChsMeter(1.0),
            ESOLGuidancePhase::Arrived, 300.0, aBp, capG, limits, 10.0);
        TestTrue(TEXT("Hold, dt 10, reserve: clamped against floor + reserve"), ChsNear(hold.LimitG,
            SOLChase::MaxSafeGForStep(ChsMeter(1.0), policy.FloorMeter + reserve, limits, 10.0), CHS_EPS));

        FSOLChaseRunState brakeRun;
        const FSOLChaseGLimit brake = SOLChase::ChaseGLimit(policy, brakeRun, ChsMeter(1.0),
            ESOLGuidancePhase::Brake, 300.0, aBp, capG, limits, 10.0);
        TestTrue(TEXT("BrakeBurst, dt 10: clamped against the floor only"), ChsNear(brake.LimitG,
            SOLChase::MaxSafeGForStep(ChsMeter(1.0), policy.FloorMeter, limits, 10.0), CHS_EPS));
        TestTrue(TEXT("BrakeBurst allows more than Burst with a reserve"), brake.LimitG > burst.LimitG);
    }

    // G-LOC: the autopilot recovery G, activity Cool
    {
        FSOLChaseRunState run;
        const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(policy, run, ChsMeter(0.0, true, 3.0),
            ESOLGuidancePhase::Accelerate, 0.0, aBp, capG, limits, dt);
        TestTrue(TEXT("G-LOC: AutopilotRecoveryG"), ChsNear(limit.LimitG, limits.AutopilotRecoveryG, CHS_EPS));
        TestTrue(TEXT("G-LOC: Cool"), limit.Activity == ESOLChaseActivity::Cool);
        TestTrue(TEXT("G-LOC: run state records it"), ChsNear(run.LastLimitG, limits.AutopilotRecoveryG, CHS_EPS)
            && run.LastActivity == ESOLChaseActivity::Cool);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseGLimitSafetyTest, "SOLTest.Chase.ChaseGLimitSafety",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Safety property: 2,000 random substeps from a meter at or above the floor never end below it nor in G-LOC
bool FSOLChaseGLimitSafetyTest::RunTest(const FString& Parameters)
{
    const FSOLPilotGLimits limits;
    const FSOLThrustEnvelope env;
    const double S = limits.SustainedG;
    const double capG = SOLChase::ChaseMaxG(env, limits, true);
    const ESOLGuidancePhase phases[] = { ESOLGuidancePhase::Accelerate, ESOLGuidancePhase::Cruise,
        ESOLGuidancePhase::Brake, ESOLGuidancePhase::Arrived };
    FRandomStream stream(CHS_SEED);

    constexpr int32 RUNS = 40;
    constexpr int32 STEPS_PER_RUN = 50;
    int32 belowFloor = 0;
    int32 glocs = 0;
    int32 belowS = 0;
    int32 runStateMismatch = 0;
    int32 steps = 0;
    for (int32 runIndex = 0; runIndex < RUNS; ++runIndex)
    {
        // A random policy within [S, cap], a floor and a start meter at or above it
        const double floorMeter = ChsRange(stream, 0.01, 0.3);
        const FSOLChasePolicy policy = ChsPolicy(ChsRange(stream, S, capG), ChsRange(stream, S, capG), floorMeter);
        FSOLGMeterState meter = ChsMeter(ChsRange(stream, floorMeter, 1.0));
        FSOLChaseRunState run;
        for (int32 step = 0; step < STEPS_PER_RUN; ++step, ++steps)
        {
            // Random substep: dt log-uniform 0.01-300 s, phase, closing speed and brake budget
            const double dt = ChsLogRange(stream, 0.01, 300.0);
            const ESOLGuidancePhase phase = phases[stream.RandRange(0, 3)];
            const double closing = ChsRange(stream, -2000.0, 20000.0);
            const double brakeAccel = ChsRange(stream, 0.5, 100.0);
            const FSOLChaseGLimit limit = SOLChase::ChaseGLimit(policy, run, meter, phase, closing, brakeAccel, capG,
                limits, dt);
            if (!FMath::IsFinite(limit.LimitG) || limit.LimitG < S - CHS_EPS)
            {
                ++belowS;
            }
            if (run.LastLimitG != limit.LimitG || run.LastActivity != limit.Activity)
            {
                ++runStateMismatch;
            }
            meter = SOLNewtonian::StepGMeter(meter, limit.LimitG, limits, dt);
            if (meter.bBlackout)
            {
                ++glocs;
                break;
            }
            if (meter.Meter < floorMeter - CHS_EPS)
            {
                ++belowFloor;
                AddInfo(FString::Printf(TEXT("Run %d step %d: dt %.3f G %.4f meter %.12f floor %.4f"), runIndex,
                    step, dt, limit.LimitG, meter.Meter, floorMeter));
                break;
            }
        }
    }
    TestEqual(TEXT("2,000 steps taken"), steps, RUNS * STEPS_PER_RUN);
    TestEqual(TEXT("Never below the floor"), belowFloor, 0);
    TestEqual(TEXT("Never G-LOC"), glocs, 0);
    TestEqual(TEXT("Limit never below S"), belowS, 0);
    TestEqual(TEXT("Run state records every limit and activity"), runStateMismatch, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 7. PredictChase
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChasePredictTest, "SOLTest.Chase.PredictChase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The closed-form prediction: the sustained case matches the SDD 18 ETA, edge cases, meter bookkeeping
bool FSOLChasePredictTest::RunTest(const FString& Parameters)
{
    const FSOLPilotGLimits limits;
    const FSOLThrustEnvelope env;
    const double g0 = ChsG0();
    const FSOLChasePolicy sustained = ChsSustainedPolicy(limits, 0.1);

    // (S, S) from rest at 100 km: within 1% of the flown plain sustained autopilot, and never below the ideal ETA
    // (the SDD 18 braking curve fades near arrival, so the uniform-brake ETA is a lower bound)
    {
        const FSOLChasePlanInput input = ChsInput(1.0e5, 0.0, 1.0, 0.1);
        const FSOLGuidanceParams plan = SOLNewtonian::MakeGuidanceParams(env, limits.SustainedG * g0, 0.0, true,
            true);
        const double eta = SOLNewtonian::EstimateTimeToArrivalS(1.0e5, 0.0, plan.PlanAccelMps2, plan.PlanAccelMps2,
            plan.FlipTimeS, input.CruiseCapMps);
        const FChsSimRun flown = ChsSimulateSustained1D(input, CHS_SIM_DT, CHS_SIM_MAX_TIME_S);
        const FSOLChasePrediction prediction = SOLChase::PredictChase(input, sustained);
        AddInfo(FString::Printf(TEXT("(S, S) 100 km: predicted %.3f s, flown %.3f s, ideal ETA %.3f s"),
            prediction.TimeS, flown.TimeS, eta));
        TestTrue(TEXT("(S, S) 100 km: sustained simulation arrived"), flown.bArrived && flown.bFinite);
        TestTrue(TEXT("(S, S) 100 km: intercepts"), prediction.bIntercepts);
        TestTrue(TEXT("(S, S) 100 km: within 1% of the flown sustained autopilot"), flown.bArrived
            && FMath::IsFinite(prediction.TimeS)
            && SOLTestHelpers::RelativeError(prediction.TimeS, flown.TimeS) <= 0.01);
        TestTrue(TEXT("(S, S) 100 km: not below EstimateTimeToArrivalS"),
            FMath::IsFinite(prediction.TimeS) && prediction.TimeS >= eta * (1.0 - CHS_EPS));
        TestTrue(TEXT("(S, S) 100 km: full meter stays full"), ChsNear(prediction.EndMeter, 1.0, 1.0e-9)
            && ChsNear(prediction.MinMeter, 1.0, 1.0e-9));
    }

    // Already there: 0 s, the meter unchanged
    {
        const FSOLChasePrediction there = SOLChase::PredictChase(ChsInput(5.0, 0.5, 0.7, 0.1), sustained);
        TestTrue(TEXT("Already there: intercepts"), there.bIntercepts);
        TestTrue(TEXT("Already there: 0 s"), ChsNear(there.TimeS, 0.0, CHS_EPS));
        TestTrue(TEXT("Already there: EndMeter = start"), ChsNear(there.EndMeter, 0.7, CHS_EPS));
        const FSOLChasePrediction edge = SOLChase::PredictChase(ChsInput(10.0, 1.0, 0.7, 0.1), sustained);
        TestTrue(TEXT("Already there at the tolerance edge (10 m, 1 m/s): 0 s"),
            edge.bIntercepts && ChsNear(edge.TimeS, 0.0, CHS_EPS) && ChsNear(edge.EndMeter, 0.7, CHS_EPS));
        const FSOLChasePrediction edgeReceding = SOLChase::PredictChase(ChsInput(10.0, -1.0, 0.7, 0.1), sustained);
        TestTrue(TEXT("Already there receding at 1 m/s: 0 s"),
            edgeReceding.bIntercepts && ChsNear(edgeReceding.TimeS, 0.0, CHS_EPS));
    }

    // A receding target takes longer than the same distance from rest
    {
        const FSOLChasePrediction rest = SOLChase::PredictChase(ChsInput(1.0e5, 0.0, 1.0, 0.1), sustained);
        const FSOLChasePrediction receding = SOLChase::PredictChase(ChsInput(1.0e5, -200.0, 1.0, 0.1), sustained);
        TestTrue(TEXT("Receding: intercepts, finite"), receding.bIntercepts && FMath::IsFinite(receding.TimeS));
        TestTrue(TEXT("Receding: longer than from rest"), receding.TimeS > rest.TimeS);
    }

    // (S, S) never changes the meter except recovery
    {
        const FSOLChasePrediction low = SOLChase::PredictChase(ChsInput(200.0, 0.0, 0.2, 0.1), sustained);
        TestTrue(TEXT("(S, S) from 0.2: intercepts"), low.bIntercepts && FMath::IsFinite(low.TimeS));
        TestTrue(TEXT("(S, S) from 0.2: MinMeter is the start"), ChsNear(low.MinMeter, 0.2, 1.0e-9));
        TestTrue(TEXT("(S, S) from 0.2: EndMeter = start + recovery"),
            ChsNear(low.EndMeter, FMath::Min(1.0, 0.2 + limits.RecoverPerS * low.TimeS), 1.0e-6));
        const FSOLChasePrediction mid = SOLChase::PredictChase(ChsInput(1.0e5, 0.0, 0.5, 0.1), sustained);
        TestTrue(TEXT("(S, S) from 0.5 over 100 km: never below the start"), mid.MinMeter >= 0.5 - 1.0e-9);
        TestTrue(TEXT("(S, S) from 0.5 over 100 km: recovers to full"), ChsNear(mid.EndMeter, 1.0, 1.0e-9));
    }

    // Burst 9 / plan 9 at 1e9 m: an unfundable brake predicts a meter below the floor (still predicted)
    {
        const FSOLChasePrediction unfunded = SOLChase::PredictChase(ChsInput(1.0e9, 0.0, 1.0, 0.1),
            ChsPolicy(9.0, 9.0, 0.1));
        TestTrue(TEXT("9 / 9 at 1e9 m: still predicts"), unfunded.bIntercepts && FMath::IsFinite(unfunded.TimeS));
        TestTrue(TEXT("9 / 9 at 1e9 m: MinMeter below the floor"), unfunded.MinMeter < 0.1);
    }

    // Invalid input: not intercepted, DBL_MAX
    {
        const FSOLChasePrediction nanDistance = SOLChase::PredictChase(ChsInput(ChsNaN(), 0.0, 1.0, 0.1), sustained);
        TestTrue(TEXT("NaN distance: no intercept, DBL_MAX"),
            !nanDistance.bIntercepts && nanDistance.TimeS == DBL_MAX);
        const FSOLChasePrediction negative = SOLChase::PredictChase(ChsInput(-100.0, 0.0, 1.0, 0.1), sustained);
        TestTrue(TEXT("Negative distance: no intercept, DBL_MAX"), !negative.bIntercepts && negative.TimeS == DBL_MAX);
        const FSOLChasePrediction nanSpeed = SOLChase::PredictChase(ChsInput(1.0e5, ChsNaN(), 1.0, 0.1), sustained);
        TestTrue(TEXT("NaN closing speed: no intercept, DBL_MAX"),
            !nanSpeed.bIntercepts && nanSpeed.TimeS == DBL_MAX);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChasePredictMatchesExecutionTest, "SOLTest.Chase.PredictMatchesExecution",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Consistency with execution: the plan's prediction matches a 0.02 s-step 1-D flight of the same policy
bool FSOLChasePredictMatchesExecutionTest::RunTest(const FString& Parameters)
{
    const double distances[] = { 2000.0, 2.0e5 };
    for (const double distanceM : distances)
    {
        const FSOLChasePlanInput input = ChsInput(distanceM, 0.0, 1.0, 0.1);
        const FSOLChasePlan plan = SOLChase::PlanChase(input);
        const FString label = FString::Printf(TEXT("%.0f m"), distanceM);
        TestTrue(*(label + TEXT(": plan valid")), plan.bValid && plan.Prediction.bIntercepts);
        if (!plan.bValid || !plan.Prediction.bIntercepts)
        {
            continue;
        }

        // Fly the planned policy and compare time and end meter
        const FChsSimRun sim = ChsSimulateChase1D(input, plan.Policy, CHS_SIM_DT, CHS_SIM_MAX_TIME_S);
        AddInfo(FString::Printf(TEXT("%s: policy %.3f / %.3f G, predicted %.2f s end %.4f min %.4f; flown %.2f s end "
            "%.4f min %.4f"), *label, plan.Policy.BurstG, plan.Policy.BrakePlanG, plan.Prediction.TimeS,
            plan.Prediction.EndMeter, plan.Prediction.MinMeter, sim.TimeS, sim.EndMeter, sim.MinMeter));
        TestTrue(*(label + TEXT(": simulation finite")), sim.bFinite);
        TestTrue(*(label + TEXT(": simulation arrived")), sim.bArrived);
        if (!sim.bArrived)
        {
            continue;
        }
        TestTrue(*(label + TEXT(": TimeS within 10% of execution")),
            FMath::Abs(plan.Prediction.TimeS - sim.TimeS) <= 0.1 * sim.TimeS);
        TestTrue(*(label + TEXT(": EndMeter within 0.1 of execution")),
            FMath::Abs(plan.Prediction.EndMeter - sim.EndMeter) <= 0.1);
        TestFalse(*(label + TEXT(": execution never G-LOC")), sim.bEverBlackout);
        TestTrue(*(label + TEXT(": execution never below the floor")), sim.MinMeter >= input.FloorMeter - 1.0e-6);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 8. PlanChase
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChasePlanTest, "SOLTest.Chase.PlanChase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The grid search: never slower than sustained, short and long chase gains, floors, invalid input, determinism
bool FSOLChasePlanTest::RunTest(const FString& Parameters)
{
    const FSOLPilotGLimits limits;
    const FSOLThrustEnvelope env;
    const double S = limits.SustainedG;
    const double capG = SOLChase::ChaseMaxG(env, limits, true);

    // Never slower than the (S, S) prediction, over several scenarios
    {
        const FSOLChasePlanInput scenarios[] = { ChsInput(2000.0, 0.0, 1.0, 0.1), ChsInput(2.0e5, 0.0, 1.0, 0.1),
            ChsInput(1.0e9, 0.0, 1.0, 0.1), ChsInput(2.0e5, -300.0, 1.0, 0.25), ChsInput(2.0e5, 500.0, 0.6, 0.25),
            ChsInput(2.0e5, 0.0, 0.05, 0.1) };
        int32 slower = 0;
        int32 invalid = 0;
        for (const FSOLChasePlanInput& input : scenarios)
        {
            const FSOLChasePlan plan = SOLChase::PlanChase(input);
            const FSOLChasePrediction ss = SOLChase::PredictChase(input, ChsSustainedPolicy(limits,
                input.FloorMeter));
            invalid += plan.bValid && plan.Prediction.bIntercepts ? 0 : 1;
            slower += plan.Prediction.TimeS <= ss.TimeS * (1.0 + CHS_EPS) ? 0 : 1;
        }
        TestEqual(TEXT("All scenario plans valid"), invalid, 0);
        TestEqual(TEXT("Never slower than (S, S)"), slower, 0);
    }

    // Short chase: one burst above sustained, at most 0.75 of the sustained time
    {
        const FSOLChasePlanInput input = ChsInput(2000.0, 0.0, 1.0, 0.1);
        const FSOLChasePlan plan = SOLChase::PlanChase(input);
        const FSOLChasePrediction ss = SOLChase::PredictChase(input, ChsSustainedPolicy(limits, 0.1));
        TestTrue(TEXT("Short: valid"), plan.bValid);
        TestTrue(TEXT("Short: BurstG > S"), plan.Policy.BurstG > S + CHS_EPS);
        TestTrue(TEXT("Short: time <= 0.75 x (S, S)"), plan.Prediction.TimeS <= 0.75 * ss.TimeS);
        TestTrue(TEXT("Short: BrakePlanG <= BurstG"), plan.Policy.BrakePlanG <= plan.Policy.BurstG + CHS_EPS);
        TestTrue(TEXT("Short: feasible (MinMeter >= floor)"), plan.Prediction.MinMeter >= 0.1 - CHS_EPS);
    }

    // Long chase: a duty cycle below the cap, at most 0.7 of the sustained time
    {
        const FSOLChasePlanInput input = ChsInput(1.0e9, 0.0, 1.0, 0.1);
        const FSOLChasePlan plan = SOLChase::PlanChase(input);
        const FSOLChasePrediction ss = SOLChase::PredictChase(input, ChsSustainedPolicy(limits, 0.1));
        TestTrue(TEXT("Long: valid"), plan.bValid);
        TestTrue(TEXT("Long: time <= 0.7 x (S, S)"), plan.Prediction.TimeS <= 0.7 * ss.TimeS);
        TestTrue(TEXT("Long: BurstG below the cap"), plan.Policy.BurstG < capG - CHS_EPS);
        TestTrue(TEXT("Long: feasible (MinMeter >= floor)"), plan.Prediction.MinMeter >= 0.1 - CHS_EPS);
    }

    // Grid levels: both levels are on the 9-level grid S + k (cap - S) / 8
    {
        const FSOLChasePlan plan = SOLChase::PlanChase(ChsInput(2.0e5, 0.0, 1.0, 0.1));
        const double stepG = (capG - S) / static_cast<double>(SOL::Chase::PLAN_G_LEVELS);
        const double burstK = (plan.Policy.BurstG - S) / stepG;
        const double brakeK = (plan.Policy.BrakePlanG - S) / stepG;
        TestTrue(TEXT("Grid: BurstG on a grid level"),
            ChsNear(burstK, FMath::RoundToDouble(burstK), 1.0e-6) && burstK > -1.0e-6 && burstK < 8.0 + 1.0e-6);
        TestTrue(TEXT("Grid: BrakePlanG on a grid level"),
            ChsNear(brakeK, FMath::RoundToDouble(brakeK), 1.0e-6) && brakeK > -1.0e-6 && brakeK <= burstK + 1.0e-6);
    }

    // Meter below the floor: a valid plan whose first substep is at sustained
    {
        const FSOLChasePlanInput input = ChsInput(2000.0, 0.0, 0.05, 0.1);
        const FSOLChasePlan plan = SOLChase::PlanChase(input);
        TestTrue(TEXT("Under the floor: valid"), plan.bValid);
        TestTrue(TEXT("Under the floor: never lower than the start"), plan.Prediction.MinMeter >= 0.05 - CHS_EPS);
        FSOLChaseRunState run;
        const FSOLChaseGLimit first = SOLChase::ChaseGLimit(plan.Policy, run, input.Meter,
            ESOLGuidancePhase::Accelerate, input.ClosingSpeedMps, ChsBrakeAccel(env, plan.Policy, 0.0, true), capG,
            limits, CHS_SUBSTEP_DT);
        TestTrue(TEXT("Under the floor: first ChaseGLimit is S"), ChsNear(first.LimitG, S, CHS_EPS));
    }

    // Floor 0.3: the meter stays above it and the chase is not faster than at floor 0.1 (ties excepted)
    {
        const double distances[] = { 2000.0, 2.0e5 };
        for (const double distanceM : distances)
        {
            const FSOLChasePlan high = SOLChase::PlanChase(ChsInput(distanceM, 0.0, 1.0, 0.3));
            const FSOLChasePlan low = SOLChase::PlanChase(ChsInput(distanceM, 0.0, 1.0, 0.1));
            const FString label = FString::Printf(TEXT("Floor 0.3 at %.0f m"), distanceM);
            TestTrue(*(label + TEXT(": valid")), high.bValid);
            TestTrue(*(label + TEXT(": MinMeter >= 0.3")), high.Prediction.MinMeter >= 0.3 - CHS_EPS);
            TestTrue(*(label + TEXT(": time >= the floor-0.1 time")),
                high.Prediction.TimeS >= low.Prediction.TimeS * (1.0 - SOL::Chase::PLAN_TIE_FRACTION) - CHS_EPS);
        }
    }

    // M = S: only sustained is possible
    {
        FSOLChasePlanInput input = ChsInput(2.0e5, 0.0, 1.0, 0.1);
        input.Limits.MaxG = input.Limits.SustainedG;
        const FSOLChasePlan plan = SOLChase::PlanChase(input);
        TestTrue(TEXT("M = S: valid"), plan.bValid);
        TestTrue(TEXT("M = S: policy (S, S)"), ChsNear(plan.Policy.BurstG, input.Limits.SustainedG, CHS_EPS)
            && ChsNear(plan.Policy.BrakePlanG, input.Limits.SustainedG, CHS_EPS));
    }

    // Invalid input: not valid, policy (S, S)
    {
        const FSOLChasePlanInput bad[] = { ChsInput(ChsNaN(), 0.0, 1.0, 0.1), ChsInput(-50.0, 0.0, 1.0, 0.1) };
        for (const FSOLChasePlanInput& input : bad)
        {
            const FSOLChasePlan plan = SOLChase::PlanChase(input);
            TestFalse(TEXT("Invalid input: bValid false"), plan.bValid);
            TestTrue(TEXT("Invalid input: policy (S, S)"), ChsNear(plan.Policy.BurstG, S, CHS_EPS)
                && ChsNear(plan.Policy.BrakePlanG, S, CHS_EPS));
        }
    }

    // Deterministic: two calls give equal plans
    {
        const FSOLChasePlanInput inputs[] = { ChsInput(2000.0, 0.0, 1.0, 0.1), ChsInput(1.0e9, 0.0, 0.7, 0.25) };
        for (const FSOLChasePlanInput& input : inputs)
        {
            TestTrue(TEXT("Deterministic"), ChsPlansEqual(SOLChase::PlanChase(input), SOLChase::PlanChase(input)));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChasePlanMonotonicityTest, "SOLTest.Chase.PlanMonotonicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Upgrade monotonicity: raising MaxG (with an envelope that allows it), TimeToEmpty or SustainedG never slows a chase
bool FSOLChasePlanMonotonicityTest::RunTest(const FString& Parameters)
{
    const double g0 = ChsG0();
    const double distances[] = { 2000.0, 2.0e5, 1.0e9 };

    // An envelope with headroom above 9 G (forward 5 G x3, total cap 15 G)
    FSOLThrustEnvelope roomy;
    roomy.ForwardAccelMps2 = 5.0 * g0;
    roomy.BoostMultiplier = 3.0;
    roomy.MaxTotalAccelMps2 = 15.0 * g0;

    int32 slowerM = 0;
    int32 slowerT = 0;
    int32 slowerS = 0;
    int32 invalid = 0;
    for (const double distanceM : distances)
    {
        // Raise MaxG 9 -> 12 on the roomy envelope
        FSOLChasePlanInput baseM = ChsInput(distanceM, 0.0, 1.0, 0.1);
        baseM.Envelope = roomy;
        FSOLChasePlanInput upM = baseM;
        upM.Limits.MaxG = 12.0;
        const FSOLChasePlan planBaseM = SOLChase::PlanChase(baseM);
        const FSOLChasePlan planUpM = SOLChase::PlanChase(upM);

        // Raise TimeToEmptyAtMaxGS 10 -> 20 s on the default envelope
        const FSOLChasePlanInput baseT = ChsInput(distanceM, 0.0, 1.0, 0.1);
        FSOLChasePlanInput upT = baseT;
        upT.Limits.TimeToEmptyAtMaxGS = 2.0 * baseT.Limits.TimeToEmptyAtMaxGS;
        const FSOLChasePlan planBaseT = SOLChase::PlanChase(baseT);
        const FSOLChasePlan planUpT = SOLChase::PlanChase(upT);

        // Raise SustainedG 1.5 -> 2 on the default envelope
        FSOLChasePlanInput upS = baseT;
        upS.Limits.SustainedG = baseT.Limits.SustainedG + 0.5;
        const FSOLChasePlan planUpS = SOLChase::PlanChase(upS);

        invalid += planBaseM.bValid && planUpM.bValid && planBaseT.bValid && planUpT.bValid && planUpS.bValid ? 0 : 1;

        // "Never longer", up to the planner's own tie fraction (ties pick the gentler level)
        const double slack = 1.0 + SOL::Chase::PLAN_TIE_FRACTION;
        if (planUpM.Prediction.TimeS > planBaseM.Prediction.TimeS * slack + CHS_EPS)
        {
            ++slowerM;
            AddInfo(FString::Printf(TEXT("M 12 at %.0f m: %.3f s vs %.3f s"), distanceM, planUpM.Prediction.TimeS,
                planBaseM.Prediction.TimeS));
        }
        if (planUpT.Prediction.TimeS > planBaseT.Prediction.TimeS * slack + CHS_EPS)
        {
            ++slowerT;
            AddInfo(FString::Printf(TEXT("T 20 at %.0f m: %.3f s vs %.3f s"), distanceM, planUpT.Prediction.TimeS,
                planBaseT.Prediction.TimeS));
        }
        if (planUpS.Prediction.TimeS > planBaseT.Prediction.TimeS * slack + CHS_EPS)
        {
            ++slowerS;
            AddInfo(FString::Printf(TEXT("S 2 at %.0f m: %.3f s vs %.3f s"), distanceM, planUpS.Prediction.TimeS,
                planBaseT.Prediction.TimeS));
        }
    }
    TestEqual(TEXT("All plans valid"), invalid, 0);
    TestEqual(TEXT("Raising MaxG never slower"), slowerM, 0);
    TestEqual(TEXT("Raising TimeToEmpty never slower"), slowerT, 0);
    TestEqual(TEXT("Raising SustainedG never slower"), slowerS, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChasePlanTimingTest, "SOLTest.Chase.PlanTiming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Logs the mean cost of one PlanChase per scenario (budget 0.1 ms, checked by a human); asserts only validity
bool FSOLChasePlanTimingTest::RunTest(const FString& Parameters)
{
    constexpr int32 ITERATIONS = 200;
    const FSOLChasePlanInput scenarios[] = { ChsInput(2000.0, 0.0, 1.0, 0.1), ChsInput(2.0e5, 0.0, 1.0, 0.1),
        ChsInput(1.0e9, 0.0, 1.0, 0.1), ChsInput(2.0e5, 0.0, 0.05, 0.1) };
    const TCHAR* names[] = { TEXT("short 2 km"), TEXT("medium 200 km"), TEXT("long 1e9 m"),
        TEXT("under-floor 200 km") };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(scenarios)); ++index)
    {
        // Time the loop; the accumulated time keeps the calls from being optimized away
        double timeSum = 0.0;
        bool bAllValid = true;
        const double startS = FPlatformTime::Seconds();
        for (int32 iteration = 0; iteration < ITERATIONS; ++iteration)
        {
            const FSOLChasePlan plan = SOLChase::PlanChase(scenarios[index]);
            timeSum += plan.Prediction.TimeS;
            bAllValid &= plan.bValid && plan.Prediction.bIntercepts;
        }
        const double meanUs = (FPlatformTime::Seconds() - startS) * 1.0e6 / ITERATIONS;
        AddInfo(FString::Printf(TEXT("PlanChase %s: %.2f us mean over %d calls (budget 100 us)"), names[index], meanUs,
            ITERATIONS));
        TestTrue(FString::Printf(TEXT("%s: result finite"), names[index]), FMath::IsFinite(timeSum));
        TestTrue(FString::Printf(TEXT("%s: plans valid"), names[index]), bAllValid);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 9. ShouldReplacePlan
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseShouldReplacePlanTest, "SOLTest.Chase.ShouldReplacePlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A new plan replaces a feasible one only when more than 2% faster, and always replaces an infeasible one
bool FSOLChaseShouldReplacePlanTest::RunTest(const FString& Parameters)
{
    const FSOLChasePlan current = ChsPlan(true, true, 100.0);
    TestFalse(TEXT("1.5% faster: keep"), SOLChase::ShouldReplacePlan(current, ChsPlan(true, true, 98.5)));
    TestTrue(TEXT("3% faster: replace"), SOLChase::ShouldReplacePlan(current, ChsPlan(true, true, 97.0)));
    TestFalse(TEXT("Equal: keep"), SOLChase::ShouldReplacePlan(current, ChsPlan(true, true, 100.0)));
    TestFalse(TEXT("Slower: keep"), SOLChase::ShouldReplacePlan(current, ChsPlan(true, true, 120.0)));
    TestTrue(TEXT("Current invalid: replace"),
        SOLChase::ShouldReplacePlan(ChsPlan(false, true, 50.0), ChsPlan(true, true, 100.0)));
    TestTrue(TEXT("Current not intercepting: replace"),
        SOLChase::ShouldReplacePlan(ChsPlan(true, false, DBL_MAX), ChsPlan(true, true, 100.0)));
    TestFalse(TEXT("Candidate invalid: keep"), SOLChase::ShouldReplacePlan(current, ChsPlan(false, true, 10.0)));
    TestFalse(TEXT("Both invalid: keep"),
        SOLChase::ShouldReplacePlan(ChsPlan(false, false, DBL_MAX), ChsPlan(false, false, DBL_MAX)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 10. ChaseAimPointM
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseAimPointTest, "SOLTest.Chase.AimPoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The tail: behind a moving target along its relative velocity, else out along the approach direction
bool FSOLChaseAimPointTest::RunTest(const FString& Parameters)
{
    const FVector3d target(1.0e6, -2.0e5, 3.0e4);
    const double trail = 250.0;
    const FVector3d approach(0.0, 3.0, 0.0);

    // Moving faster than 1 m/s relative to its frame: trail behind along the relative velocity
    const FVector3d moving = SOLChase::ChaseAimPointM(target, FVector3d(40.0, 0.0, 30.0), approach, trail);
    TestTrue(TEXT("Moving: behind along the velocity"),
        SOLTestHelpers::VectorsNear(moving, target - FVector3d(0.8, 0.0, 0.6) * trail, 1.0e-12));
    const FVector3d justAbove = SOLChase::ChaseAimPointM(target, FVector3d(0.0, -1.5, 0.0), approach, trail);
    TestTrue(TEXT("1.5 m/s: behind along the velocity"),
        SOLTestHelpers::VectorsNear(justAbove, target + FVector3d(0.0, trail, 0.0), 1.0e-12));

    // At rest in its frame (and exactly at 1 m/s): out along the approach side
    const FVector3d still = SOLChase::ChaseAimPointM(target, FVector3d::ZeroVector, approach, trail);
    TestTrue(TEXT("At rest: approach side"),
        SOLTestHelpers::VectorsNear(still, target + FVector3d(0.0, trail, 0.0), 1.0e-12));
    const FVector3d slow = SOLChase::ChaseAimPointM(target, FVector3d(0.0, 0.0, 0.5), approach, trail);
    TestTrue(TEXT("0.5 m/s: approach side"),
        SOLTestHelpers::VectorsNear(slow, target + FVector3d(0.0, trail, 0.0), 1.0e-12));
    const FVector3d boundary = SOLChase::ChaseAimPointM(target, FVector3d(1.0, 0.0, 0.0), approach, trail);
    TestTrue(TEXT("Exactly 1 m/s: approach side"),
        SOLTestHelpers::VectorsNear(boundary, target + FVector3d(0.0, trail, 0.0), 1.0e-12));

    // Fallbacks: a zero or non-finite approach direction -> +Z
    const FVector3d zeroDir = SOLChase::ChaseAimPointM(target, FVector3d::ZeroVector, FVector3d::ZeroVector, trail);
    TestTrue(TEXT("Zero approach: +Z"),
        SOLTestHelpers::VectorsNear(zeroDir, target + FVector3d(0.0, 0.0, trail), 1.0e-12));
    const FVector3d nanDir = SOLChase::ChaseAimPointM(target, FVector3d::ZeroVector,
        FVector3d(ChsNaN(), 0.0, 1.0), trail);
    TestTrue(TEXT("NaN approach: +Z"),
        SOLTestHelpers::VectorsNear(nanDir, target + FVector3d(0.0, 0.0, trail), 1.0e-12));
    const FVector3d infDir = SOLChase::ChaseAimPointM(target, FVector3d::ZeroVector,
        FVector3d(ChsInf(), 0.0, 0.0), trail);
    TestTrue(TEXT("Infinite approach: +Z"),
        SOLTestHelpers::VectorsNear(infDir, target + FVector3d(0.0, 0.0, trail), 1.0e-12));

    // A negative or NaN trail: the target itself
    TestTrue(TEXT("Negative trail: the target"), SOLTestHelpers::VectorsNear(
        SOLChase::ChaseAimPointM(target, FVector3d(40.0, 0.0, 30.0), approach, -10.0), target, 1.0e-15));
    TestTrue(TEXT("NaN trail: the target"), SOLTestHelpers::VectorsNear(
        SOLChase::ChaseAimPointM(target, FVector3d::ZeroVector, approach, ChsNaN()), target, 1.0e-15));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 12. ChaseTrailM
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseTrailDistanceTest, "SOLTest.Chase.TrailDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The trail is max(minimum, k x radius); bad radius or k falls back to the minimum
bool FSOLChaseTrailDistanceTest::RunTest(const FString& Parameters)
{
    const double minTrail = SOL::Chase::TARGET_TRAIL_M;
    const double k = SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR;
    TestTrue(TEXT("Radius 10 m -> 200"), ChsNear(SOLChase::ChaseTrailM(10.0, minTrail, k), 200.0, CHS_EPS));
    TestTrue(TEXT("Radius 5 km, k 3 -> 15 km"), ChsNear(SOLChase::ChaseTrailM(5000.0, minTrail, 3.0), 15000.0,
        1.0e-6));
    TestTrue(TEXT("Boundary radius 200/3 -> 200"), ChsNear(SOLChase::ChaseTrailM(200.0 / 3.0, minTrail, 3.0), 200.0,
        1.0e-9));
    TestTrue(TEXT("NaN radius -> minimum"), ChsNear(SOLChase::ChaseTrailM(ChsNaN(), minTrail, k), minTrail, CHS_EPS));
    TestTrue(TEXT("Negative radius -> minimum"), ChsNear(SOLChase::ChaseTrailM(-500.0, minTrail, k), minTrail,
        CHS_EPS));
    TestTrue(TEXT("NaN k -> minimum"), ChsNear(SOLChase::ChaseTrailM(5000.0, minTrail, ChsNaN()), minTrail, CHS_EPS));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// 11. EstimateTargetAccelMps2
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseTargetAccelEstimateTest, "SOLTest.Chase.TargetAccelEstimate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The low-pass acceleration estimate: decays at constant velocity, converges to a steady change, clamps to the cap
bool FSOLChaseTargetAccelEstimateTest::RunTest(const FString& Parameters)
{
    const double dt = 1.0 / 60.0;
    const double capMps2 = 2.0 * 9.0 * ChsG0();

    // Constant velocity: one step decays by exp(-dt / tau); 5 s decays toward 0
    {
        const FVector3d velocity(100.0, -50.0, 20.0);
        const FVector3d prev(3.0, 0.0, -4.0);
        const FVector3d one = SOLChase::EstimateTargetAccelMps2(velocity, velocity, prev, dt, capMps2);
        TestTrue(TEXT("Constant velocity: one step decays by exp(-dt / tau)"), SOLTestHelpers::VectorsNear(one,
            prev * FMath::Exp(-dt / SOL::Chase::TARGET_ACCEL_FILTER_S), 1.0e-9));
        FVector3d estimate = prev;
        for (int32 step = 0; step < 300; ++step)
        {
            estimate = SOLChase::EstimateTargetAccelMps2(velocity, velocity, estimate, dt, capMps2);
        }
        TestTrue(TEXT("Constant velocity: decays toward 0 after 5 s"), estimate.Size() < 0.01 * prev.Size());
    }

    // A steady 5 m/s^2 change: 5 within 1% after 5 s of 1/60 s steps
    {
        FVector3d estimate = FVector3d::ZeroVector;
        FVector3d velocity(10.0, 0.0, 0.0);
        const FVector3d accel(0.0, 3.0, 4.0);
        for (int32 step = 0; step < 300; ++step)
        {
            const FVector3d next = velocity + accel * dt;
            estimate = SOLChase::EstimateTargetAccelMps2(velocity, next, estimate, dt, capMps2);
            velocity = next;
        }
        TestTrue(TEXT("Steady 5 m/s^2: length within 1%"), SOLTestHelpers::RelativeError(estimate.Size(), 5.0) <= 0.01);
        TestTrue(TEXT("Steady 5 m/s^2: direction"), SOLTestHelpers::VectorsNear(estimate, accel, 0.01));
    }

    // A teleport-sized velocity jump: clamped to the cap
    {
        const FVector3d jump = SOLChase::EstimateTargetAccelMps2(FVector3d::ZeroVector, FVector3d(0.0, 0.0, 1.0e6),
            FVector3d::ZeroVector, dt, capMps2);
        TestTrue(TEXT("Teleport: length = cap"), ChsNear(jump.Size(), capMps2, 1.0e-9 * capMps2));
        TestTrue(TEXT("Teleport: along the jump"), jump.Z > 0.0);
    }

    // Bad dt: the previous estimate
    {
        const FVector3d prev(1.0, 2.0, 3.0);
        const FVector3d a(0.0, 0.0, 0.0);
        const FVector3d b(100.0, 0.0, 0.0);
        TestTrue(TEXT("dt 0: previous"), SOLChase::EstimateTargetAccelMps2(a, b, prev, 0.0, capMps2) == prev);
        TestTrue(TEXT("dt negative: previous"), SOLChase::EstimateTargetAccelMps2(a, b, prev, -dt, capMps2) == prev);
        TestTrue(TEXT("dt NaN: previous"), SOLChase::EstimateTargetAccelMps2(a, b, prev, ChsNaN(), capMps2) == prev);
        TestTrue(TEXT("dt infinite: previous"),
            SOLChase::EstimateTargetAccelMps2(a, b, prev, ChsInf(), capMps2) == prev);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
