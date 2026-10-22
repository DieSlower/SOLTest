/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLNewtonianFlight.h"
#include "SOLConstants.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #18 (18a) Newtonian flight core, written against the 18a contract in
// Docs/Plans/18-newtonian-flight-core-plan.md before any production code existed.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Tolerance for exact formulas
    constexpr double NFM_EPS = 1.0e-9;

    // The player's substep length (SOL::SHIP_MAX_SUBSTEP_S), restated so the tests stay independent
    constexpr double NFM_DT = 1.0 / 30.0;

    // Seed for the randomized property cases
    constexpr int32 NFM_SEED = 20261021;

    // Half the speed of light (the hard speed clamp)
    constexpr double NFM_HALF_C = 149896229.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double NfmNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the invalid-input cases
    double NfmInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a uniform random double in [minValue, maxValue)
    double NfmRange(const FRandomStream& stream, const double minValue, const double maxValue)
    {
        return minValue + (maxValue - minValue) * static_cast<double>(stream.FRand());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a vector whose components are all NaN
    FVector3d NfmNaNVector()
    {
        return FVector3d(NfmNaN(), NfmNaN(), NfmNaN());
    }

    // Standard gravity, restated so the tests stay independent
    constexpr double NFM_G0 = 9.80665;

    //////////////////////////////////////////////////////////////////////////
    // Returns an explicit mechanics envelope (round numbers, no total cap) for the integrator, allocation and
    // guidance cases; every field is set so default changes do not break the tests
    FSOLThrustEnvelope NfmEnvelope()
    {
        FSOLThrustEnvelope env;
        env.ForwardAccelMps2 = 50.0;
        env.ReverseAccelMps2 = 25.0;
        env.LateralAccelMps2 = 30.0;
        env.VerticalAccelMps2 = 30.0;
        env.BoostMultiplier = 3.0;
        env.MaxTotalAccelMps2 = 0.0;
        env.MaxAngularAccelRadPerS2 = FVector3d(4.0, 3.0, 3.0);
        env.MaxAngularRateRadPerS = FVector3d(2.0943951023931953, 1.5707963267948966, 1.5707963267948966);
        env.MaxSpinRateRadPerS = 2.0 * UE_DOUBLE_PI;
        env.MaxSpeedMps = NFM_HALF_C;
        return env;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the SDD 18 player envelope in G (3 / 1.5 / 2 / 2 G, boost x3, 9 G total cap), set field by field
    FSOLThrustEnvelope NfmPlayerEnvelope()
    {
        FSOLThrustEnvelope env = NfmEnvelope();
        env.ForwardAccelMps2 = 3.0 * NFM_G0;
        env.ReverseAccelMps2 = 1.5 * NFM_G0;
        env.LateralAccelMps2 = 2.0 * NFM_G0;
        env.VerticalAccelMps2 = 2.0 * NFM_G0;
        env.BoostMultiplier = 3.0;
        env.MaxTotalAccelMps2 = 9.0 * NFM_G0;
        return env;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the SDD 18 baseline pilot G limits, set field by field
    FSOLPilotGLimits NfmLimits()
    {
        FSOLPilotGLimits limits;
        limits.SustainedG = 1.5;
        limits.MaxG = 9.0;
        limits.TimeToEmptyAtMaxGS = 10.0;
        limits.DrainExponent = 2.0;
        limits.RecoverPerS = 0.05;
        limits.BlackoutDurationS = 5.0;
        limits.WakeMeter = 0.2;
        limits.CriticalMeter = 0.25;
        limits.VisionStartMeter = 0.4;
        limits.AutopilotRecoveryG = 1.5;
        limits.UnconsciousRecoverPerS = 0.02;
        limits.LethalWarningS = 30.0;
        return limits;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a G meter state with the given fields
    FSOLGMeterState NfmMeter(const double meter, const bool bBlackout = false, const double remainingS = 0.0)
    {
        FSOLGMeterState state;
        state.Meter = meter;
        state.bBlackout = bBlackout;
        state.BlackoutRemainingS = remainingS;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a G meter state with the given fields and depleted time
    FSOLGMeterState NfmDepletedMeter(const double meter, const bool bBlackout, const double remainingS,
        const double depletedS)
    {
        FSOLGMeterState state = NfmMeter(meter, bBlackout, remainingS);
        state.DepletedS = depletedS;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two meter states match within a tolerance (blackout flags exactly)
    bool NfmMeterNear(const FSOLGMeterState& a, const FSOLGMeterState& b, const double tolerance)
    {
        return a.bBlackout == b.bBlackout && FMath::Abs(a.Meter - b.Meter) <= tolerance
            && FMath::Abs(a.BlackoutRemainingS - b.BlackoutRemainingS) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns guidance params with the given budgets and the contract's default arrival values
    FSOLGuidanceParams NfmGuidance(const double planAccelMps2, const double brakeAccelMps2, const double flipTimeS)
    {
        FSOLGuidanceParams params;
        params.PlanAccelMps2 = planAccelMps2;
        params.BrakeAccelMps2 = brakeAccelMps2;
        params.FlipTimeS = flipTimeS;
        params.MaxCruiseSpeedMps = NFM_HALF_C;
        params.ArrivalRadiusM = 10.0;
        params.ArrivalSpeedMps = 1.0;
        params.VelocityGainPerS = 2.0;
        return params;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two vectors match within an absolute tolerance
    bool NfmVecNear(const FVector3d& actual, const FVector3d& expected, const double tolerance)
    {
        return SOLTestHelpers::IsFiniteVector(actual) && (actual - expected).Size() <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the angle in radians between two non-zero vectors (atan2 form, accurate near 0 and PI)
    double NfmAngle(const FVector3d& a, const FVector3d& b)
    {
        return FMath::Atan2(FVector3d::CrossProduct(a, b).Size(), FVector3d::DotProduct(a, b));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship's nose (local +X) in world axes
    FVector3d NfmForward(const FQuat4d& q)
    {
        return q.RotateVector(FVector3d(1.0, 0.0, 0.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship's right (local +Y) in world axes
    FVector3d NfmRight(const FQuat4d& q)
    {
        return q.RotateVector(FVector3d(0.0, 1.0, 0.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship's up (local +Z) in world axes
    FVector3d NfmUp(const FQuat4d& q)
    {
        return q.RotateVector(FVector3d(0.0, 0.0, 1.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every field of a state is finite
    bool NfmStateFinite(const FSOLShipState& s)
    {
        return SOLTestHelpers::IsFiniteVector(s.PositionM) && SOLTestHelpers::IsFiniteVector(s.VelocityMps)
            && SOLTestHelpers::IsFiniteVector(s.AngularVelocityRadPerS) && FMath::IsFinite(s.Orientation.X)
            && FMath::IsFinite(s.Orientation.Y) && FMath::IsFinite(s.Orientation.Z) && FMath::IsFinite(s.Orientation.W);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two states are bit-identical in every field
    bool NfmStatesIdentical(const FSOLShipState& a, const FSOLShipState& b)
    {
        return a.PositionM == b.PositionM && a.VelocityMps == b.VelocityMps
            && a.AngularVelocityRadPerS == b.AngularVelocityRadPerS && a.Orientation.X == b.Orientation.X
            && a.Orientation.Y == b.Orientation.Y && a.Orientation.Z == b.Orientation.Z
            && a.Orientation.W == b.Orientation.W;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a command with the given thrust and torque fractions
    FSOLNewtonianCommand NfmCommand(const FVector3d& thrust, const FVector3d& torque, const bool bBoost = false)
    {
        FSOLNewtonianCommand command;
        command.ThrustFraction = thrust;
        command.AngularAccelFraction = torque;
        command.bBoost = bBoost;
        return command;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the world rotation (axis * angle) that takes orientation a to orientation b, with the angle in [0, PI]
    FVector3d NfmWorldRotationVector(const FQuat4d& a, const FQuat4d& b)
    {
        FQuat4d delta = b * a.Inverse();
        if (delta.W < 0.0)
        {
            delta = FQuat4d(-delta.X, -delta.Y, -delta.Z, -delta.W);
        }
        FVector3d axis;
        double angle = 0.0;
        delta.GetNormalized().ToAxisAndAngle(axis, angle);
        return axis * angle;
    }

    // Outcome of one closed-loop point-mass guidance run
    struct FNfmPointRun
    {
        bool bArrived = false;
        double ArrivalTimeS = 0.0;
        double MaxOvershootM = 0.0;          // Largest distance the ship got past the target along the initial line
        double PeakClosingMps = 0.0;
        double BrakeStartTraveledM = -1.0;   // Distance travelled along the initial line when Brake first showed
        double MaxShipRadiusM = 0.0;         // Largest |ship position| (for runs around the origin)
        bool bBudgetOk = true;
        bool bFinite = true;
        bool bSawCruise = false;
        bool bSawAccelerate = false;
        double InitialEtaS = -1.0;           // EtaS of the first evaluation
        double MinEtaPlusTimeS = TNumericLimits<double>::Max();   // min over the run of (t + EtaS)
        double MaxEtaPlusTimeS = -1.0;                            // max over the run of (t + EtaS)
        FVector3d FinalShipVelocityMps = FVector3d::ZeroVector;
        FVector3d FinalTargetVelocityMps = FVector3d::ZeroVector;
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies a point mass with the guidance's desired net acceleration (semi-implicit Euler, no gravity) toward a
    // target that moves with constant acceleration; stops at the first Arrived phase or after maxTimeS
    FNfmPointRun NfmRunPointMass(FVector3d shipPositionM, FVector3d shipVelocityMps, FVector3d targetPositionM,
        FVector3d targetVelocityMps, const FVector3d& targetAccelMps2, const FSOLGuidanceParams& params,
        const double dt, const double maxTimeS)
    {
        FNfmPointRun run;
        const FVector3d axis = (targetPositionM - shipPositionM).GetSafeNormal();
        const FVector3d startM = shipPositionM;
        const double brakeBudget = FMath::Max(params.PlanAccelMps2, params.BrakeAccelMps2);
        const int64 maxSteps = static_cast<int64>(FMath::CeilToDouble(maxTimeS / dt));
        for (int64 step = 0; step <= maxSteps; ++step)
        {
            const FSOLGuidanceOutput out = SOLNewtonian::ComputeRendezvousGuidance(targetPositionM - shipPositionM,
                shipVelocityMps - targetVelocityMps, targetAccelMps2, FVector3d::ZeroVector, params, dt);
            if (!SOLTestHelpers::IsFiniteVector(out.DesiredNetAccelMps2)
                || !SOLTestHelpers::IsFiniteVector(out.DesiredForward) || !FMath::IsFinite(out.ClosingSpeedMps))
            {
                run.bFinite = false;
                break;
            }

            // Budget guarantees: the relative acceleration never exceeds the phase's budget
            const double relAccel = (out.DesiredNetAccelMps2 - targetAccelMps2).Size();
            const double budget = out.Phase == ESOLGuidancePhase::Brake ? brakeBudget : params.PlanAccelMps2;
            if (relAccel > budget * (1.0 + 1.0e-9) + 1.0e-9)
            {
                run.bBudgetOk = false;
            }

            if (out.Phase == ESOLGuidancePhase::Arrived)
            {
                run.bArrived = true;
                run.ArrivalTimeS = step * dt;
                break;
            }

            // ETA bookkeeping: t + EtaS should stay close to the real arrival time
            if (run.InitialEtaS < 0.0)
            {
                run.InitialEtaS = out.EtaS;
            }
            run.MinEtaPlusTimeS = FMath::Min(run.MinEtaPlusTimeS, step * dt + out.EtaS);
            run.MaxEtaPlusTimeS = FMath::Max(run.MaxEtaPlusTimeS, step * dt + out.EtaS);
            if (out.Phase == ESOLGuidancePhase::Brake && run.BrakeStartTraveledM < 0.0)
            {
                run.BrakeStartTraveledM = FVector3d::DotProduct(shipPositionM - startM, axis);
            }
            run.bSawCruise |= out.Phase == ESOLGuidancePhase::Cruise;
            run.bSawAccelerate |= out.Phase == ESOLGuidancePhase::Accelerate;
            run.PeakClosingMps = FMath::Max(run.PeakClosingMps, out.ClosingSpeedMps);

            // Semi-implicit Euler for both the ship and the target
            shipVelocityMps += out.DesiredNetAccelMps2 * dt;
            shipPositionM += shipVelocityMps * dt;
            targetVelocityMps += targetAccelMps2 * dt;
            targetPositionM += targetVelocityMps * dt;

            run.MaxOvershootM = FMath::Max(run.MaxOvershootM,
                FVector3d::DotProduct(shipPositionM - targetPositionM, axis));
            run.MaxShipRadiusM = FMath::Max(run.MaxShipRadiusM, shipPositionM.Size());
        }
        run.FinalShipVelocityMps = shipVelocityMps;
        run.FinalTargetVelocityMps = targetVelocityMps;
        return run;
    }

    // Outcome of one closed-loop full-ship (guidance, attitude, allocation, step) run
    struct FNfmShipRun
    {
        bool bArrived = false;
        double ArrivalTimeS = 0.0;
        double MaxOvershootM = 0.0;
        double MinForwardDot = 1.0;          // Smallest dot(nose, initial line of sight): < -0.9 means it flipped
        double MaxGLoad = 0.0;               // Largest G-load of the achieved thrust (proper acceleration)
        bool bFinite = true;
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies a full 6-DOF ship from rest at the origin (nose +X) to a static target with the whole controller chain;
    // the desired thrust is clamped to pilotLimitMps2 before allocation (the autopilot never exceeds the pilot)
    FNfmShipRun NfmRunFullShip(const FSOLThrustEnvelope& env, const FSOLGuidanceParams& params,
        const FVector3d& targetM, const FVector3d& gravityMps2, const double dt, const double maxTimeS,
        const double pilotLimitMps2)
    {
        FNfmShipRun run;
        const FVector3d axis = targetM.GetSafeNormal();
        FSOLShipState state;
        const int64 maxSteps = static_cast<int64>(FMath::CeilToDouble(maxTimeS / dt));
        for (int64 step = 0; step <= maxSteps; ++step)
        {
            const FSOLGuidanceOutput out = SOLNewtonian::ComputeRendezvousGuidance(targetM - state.PositionM,
                state.VelocityMps, FVector3d::ZeroVector, gravityMps2, params, dt);
            if (out.Phase == ESOLGuidancePhase::Arrived)
            {
                run.bArrived = true;
                run.ArrivalTimeS = step * dt;
                break;
            }

            // Thrust from the allocation, rotation from the attitude law through the rate command
            const FSOLThrustAllocation allocation = SOLNewtonian::AllocateThrust(
                SOLNewtonian::ClampToPilotLimit(out.DesiredThrustAccelMps2, pilotLimitMps2), state.Orientation, env,
                false);
            run.MaxGLoad = FMath::Max(run.MaxGLoad, SOLNewtonian::GLoadG(allocation.AchievedAccelWorldMps2));
            const FVector3d rateCommand = SOLNewtonian::AttitudeRateCommand(state.Orientation, out.DesiredForward,
                FVector3d::ZeroVector, env);
            const FVector3d torque = SOLNewtonian::RateCommandAngularFraction(state.AngularVelocityRadPerS,
                rateCommand, env, dt);
            state = SOLNewtonian::StepNewtonian(state, NfmCommand(allocation.ThrustFraction, torque), env,
                gravityMps2, dt);
            if (!NfmStateFinite(state))
            {
                run.bFinite = false;
                break;
            }
            run.MinForwardDot = FMath::Min(run.MinForwardDot,
                FVector3d::DotProduct(NfmForward(state.Orientation), axis));
            run.MaxOvershootM = FMath::Max(run.MaxOvershootM, FVector3d::DotProduct(state.PositionM - targetM, axis));
        }
        return run;
    }

    //////////////////////////////////////////////////////////////////////////
    // Turns a ship to a desired forward / up with attitude + rate command + step; returns the seconds until both
    // errors are below toleranceRad, or -1 if not reached within maxTimeS (peak |rate| per axis into outPeakRate)
    double NfmTurnUntilAligned(const FSOLThrustEnvelope& env, FSOLShipState& state, const FVector3d& desiredForward,
        const FVector3d& desiredUp, const double toleranceRad, const double maxTimeS, FVector3d& outPeakRate)
    {
        outPeakRate = FVector3d::ZeroVector;
        const int64 maxSteps = static_cast<int64>(FMath::CeilToDouble(maxTimeS / NFM_DT));
        for (int64 step = 0; step <= maxSteps; ++step)
        {
            const bool bForwardOk = NfmAngle(NfmForward(state.Orientation), desiredForward) <= toleranceRad;
            const bool bUpOk = desiredUp.IsZero() || NfmAngle(NfmUp(state.Orientation), desiredUp) <= toleranceRad;
            if (bForwardOk && bUpOk)
            {
                return step * NFM_DT;
            }
            const FVector3d rate = SOLNewtonian::AttitudeRateCommand(state.Orientation, desiredForward, desiredUp,
                env);
            const FVector3d torque = SOLNewtonian::RateCommandAngularFraction(state.AngularVelocityRadPerS, rate,
                env, NFM_DT);
            state = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d::ZeroVector, torque), env,
                FVector3d::ZeroVector, NFM_DT);
            outPeakRate.X = FMath::Max(outPeakRate.X, FMath::Abs(state.AngularVelocityRadPerS.X));
            outPeakRate.Y = FMath::Max(outPeakRate.Y, FMath::Abs(state.AngularVelocityRadPerS.Y));
            outPeakRate.Z = FMath::Max(outPeakRate.Z, FMath::Abs(state.AngularVelocityRadPerS.Z));
        }
        return -1.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Constants
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathConstantsTest, "SOLTest.NewtonianFlightMath.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Checks the contract's tuning constants
bool FSOLNewtonianFlightMathConstantsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNewtonian;
    TestEqual(TEXT("EPSILON"), EPSILON, 1.0e-9);
    TestEqual(TEXT("ATTITUDE_GAIN_PER_S"), ATTITUDE_GAIN_PER_S, 4.0);
    TestEqual(TEXT("ATTITUDE_BRAKE_FRACTION"), ATTITUDE_BRAKE_FRACTION, 0.8);
    TestEqual(TEXT("GUIDANCE_ACCEL_MARGIN"), GUIDANCE_ACCEL_MARGIN, 0.9);
    TestEqual(TEXT("GUIDANCE_MIN_ACCEL_FRACTION"), GUIDANCE_MIN_ACCEL_FRACTION, 0.1);
    TestEqual(TEXT("MIN_TIME_CONSTANT_S"), MIN_TIME_CONSTANT_S, 1.0e-3);
    TestEqual(TEXT("FORWARD_HOLD_FRACTION"), FORWARD_HOLD_FRACTION, 0.05);
    TestEqual(TEXT("STANDARD_GRAVITY_MPS2"), STANDARD_GRAVITY_MPS2, 9.80665);
    TestEqual(TEXT("HUMAN_MAX_G"), HUMAN_MAX_G, 9.0);
    TestEqual(TEXT("G_LOAD_TOLERANCE_G"), G_LOAD_TOLERANCE_G, 1.0e-9);

    // Review-fix (SDD 18 Amendment 7) defaults: the unconscious recovery floor, the lethal warning time, the
    // depleted-time accumulator and the Lethal warning appended last
    const FSOLPilotGLimits limits;
    TestEqual(TEXT("UnconsciousRecoverPerS default"), limits.UnconsciousRecoverPerS, 0.02);
    TestEqual(TEXT("LethalWarningS default"), limits.LethalWarningS, 30.0);
    TestEqual(TEXT("DepletedS default"), FSOLGMeterState().DepletedS, 0.0);
    TestEqual(TEXT("ESOLGWarning order: None"), static_cast<int32>(ESOLGWarning::None), 0);
    TestEqual(TEXT("ESOLGWarning order: OverSustained"), static_cast<int32>(ESOLGWarning::OverSustained), 1);
    TestEqual(TEXT("ESOLGWarning order: Critical"), static_cast<int32>(ESOLGWarning::Critical), 2);
    TestEqual(TEXT("ESOLGWarning order: Blackout"), static_cast<int32>(ESOLGWarning::Blackout), 3);
    TestEqual(TEXT("ESOLGWarning order: Lethal appended last"), static_cast<int32>(ESOLGWarning::Lethal), 4);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Thrust envelope and the integrator
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathLocalThrustAxesTest,
    "SOLTest.NewtonianFlightMath.LocalThrustAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The envelope is a per-axis box with separate forward and reverse limits, clamped and boost-scaled
bool FSOLNewtonianFlightMathLocalThrustAxesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    TestTrue(TEXT("Full positive box corner"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 1.0, 1.0), env, false),
        FVector3d(50.0, 30.0, 30.0), NFM_EPS));
    TestTrue(TEXT("Full negative corner uses reverse"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(-1.0, -1.0, -1.0), env, false), FVector3d(-25.0, -30.0, -30.0),
        NFM_EPS));
    TestTrue(TEXT("Partial fractions scale linearly"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(0.5, -0.25, 0.1), env, false), FVector3d(25.0, -7.5, 3.0),
        NFM_EPS));
    TestTrue(TEXT("Partial reverse"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(-0.4, 0.0, 0.0), env, false), FVector3d(-10.0, 0.0, 0.0),
        NFM_EPS));
    TestTrue(TEXT("Out-of-range fractions are clamped per axis (no unit-length clamp)"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(5.0, -7.0, 0.5), env, false), FVector3d(50.0, -30.0, 15.0),
        NFM_EPS));
    TestTrue(TEXT("Boost multiplies every axis"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, -1.0, 0.5), env, true), FVector3d(150.0, -90.0, 45.0),
        NFM_EPS));
    TestTrue(TEXT("Boosted reverse"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(-1.0, 0.0, 0.0), env, true), FVector3d(-75.0, 0.0, 0.0),
        NFM_EPS));
    TestTrue(TEXT("NaN component counts as zero"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(NfmNaN(), 1.0, -1.0), env, false), FVector3d(0.0, 30.0, -30.0),
        NFM_EPS));
    TestTrue(TEXT("Infinite component counts as zero"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(NfmInf(), 0.0, 0.0), env, false), FVector3d::ZeroVector,
        NFM_EPS));
    TestTrue(TEXT("Zero fractions give zero"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d::ZeroVector, env, true), FVector3d::ZeroVector, NFM_EPS));

    // Total cap: a box result longer than MaxTotalAccelMps2 is scaled to it, direction kept
    FSOLThrustEnvelope capped = env;
    capped.MaxTotalAccelMps2 = 40.0;
    const FVector3d corner = SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 1.0, 0.0), capped, false);
    TestTrue(TEXT("Capped corner length"), FMath::Abs(corner.Size() - 40.0) < 1.0e-9);
    TestTrue(TEXT("Capped corner direction"), NfmAngle(corner, FVector3d(50.0, 30.0, 0.0)) < 1.0e-12);
    TestTrue(TEXT("Under the cap is untouched"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(0.5, 0.0, 0.0), capped, false), FVector3d(25.0, 0.0, 0.0),
        NFM_EPS));
    capped.MaxTotalAccelMps2 = NfmNaN();
    TestTrue(TEXT("NaN cap means no cap"), NfmVecNear(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 1.0, 0.0), capped, false), FVector3d(50.0, 30.0, 0.0),
        NFM_EPS));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathStepInvalidDtTest,
    "SOLTest.NewtonianFlightMath.StepInvalidDtUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero, negative or non-finite dt leaves the state bit-identical
bool FSOLNewtonianFlightMathStepInvalidDtTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    state.PositionM = FVector3d(1.0e9, -2.0e8, 3.0e7);
    state.VelocityMps = FVector3d(30000.0, 10.0, -5.0);
    state.Orientation = FQuat4d(FVector3d(0.0, 0.0, 1.0), 0.7);
    state.AngularVelocityRadPerS = FVector3d(0.1, -0.2, 0.3);
    const FSOLNewtonianCommand command = NfmCommand(FVector3d(1.0, 0.5, -0.5), FVector3d(1.0, 1.0, 1.0), true);
    const FVector3d gravity(0.0, 0.0, -9.81);
    const double badDts[] = { 0.0, -0.1, NfmNaN(), NfmInf(), -NfmInf() };
    for (const double dt : badDts)
    {
        const FSOLShipState after = SOLNewtonian::StepNewtonian(state, command, env, gravity, dt);
        TestTrue(FString::Printf(TEXT("dt %f leaves the state unchanged"), dt), NfmStatesIdentical(after, state));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathMomentumConservationTest,
    "SOLTest.NewtonianFlightMath.MomentumConservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With no thrust, torque or gravity the velocity is exactly constant (no drag) and the position is linear
bool FSOLNewtonianFlightMathMomentumConservationTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    state.PositionM = FVector3d(1.0e11, 2.0e10, -3.0e9);
    state.VelocityMps = FVector3d(29780.0, -1234.5, 17.25);
    state.Orientation = FQuat4d(FVector3d(1.0, 2.0, 3.0).GetSafeNormal(), 1.1);
    const FSOLShipState start = state;
    constexpr int32 steps = 1000;
    for (int32 i = 0; i < steps; ++i)
    {
        state = SOLNewtonian::StepNewtonian(state, FSOLNewtonianCommand(), env, FVector3d::ZeroVector, NFM_DT);
    }
    TestTrue(TEXT("Velocity is exactly unchanged"), state.VelocityMps == start.VelocityMps);
    TestTrue(TEXT("Angular velocity stays zero"), state.AngularVelocityRadPerS == FVector3d::ZeroVector);
    const FVector3d expected = start.PositionM + start.VelocityMps * (steps * NFM_DT);
    TestTrue(TEXT("Position advanced linearly"), NfmVecNear(state.PositionM, expected, 1.0e-6 * (steps * NFM_DT)
        * start.VelocityMps.Size() + 1.0e-3));
    TestTrue(TEXT("Orientation unchanged"), NfmAngle(NfmForward(state.Orientation), NfmForward(start.Orientation))
        < 1.0e-12 && NfmAngle(NfmUp(state.Orientation), NfmUp(start.Orientation)) < 1.0e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathThrustImpulseTest, "SOLTest.NewtonianFlightMath.ThrustImpulse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Constant thrust gives delta-v = a * t exactly along the rotated axis, and the semi-implicit position sum
bool FSOLNewtonianFlightMathThrustImpulseTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    const FQuat4d yawed(FVector3d(0.0, 0.0, 1.0), 0.5 * UE_DOUBLE_PI);
    constexpr int32 steps = 60;
    const double timeS = steps * NFM_DT;

    // Forward, reverse and boosted forward, each from the same start
    struct FCase { double Fraction; bool bBoost; double ExpectedAccel; const TCHAR* Name; };
    const FCase cases[] = {
        { 1.0, false, 50.0, TEXT("forward") },
        { -1.0, false, -25.0, TEXT("reverse") },
        { 1.0, true, 150.0, TEXT("boosted forward") },
    };
    for (const FCase& c : cases)
    {
        FSOLShipState state;
        state.PositionM = FVector3d(10.0, 20.0, 30.0);
        state.VelocityMps = FVector3d(1.0, 2.0, 3.0);
        state.Orientation = yawed;
        const FSOLShipState start = state;
        for (int32 i = 0; i < steps; ++i)
        {
            state = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d(c.Fraction, 0.0, 0.0),
                FVector3d::ZeroVector, c.bBoost), env, FVector3d::ZeroVector, NFM_DT);
        }
        const FVector3d accel = NfmForward(yawed) * c.ExpectedAccel;
        TestTrue(FString::Printf(TEXT("%s: delta-v = a t"), c.Name),
            NfmVecNear(state.VelocityMps - start.VelocityMps, accel * timeS, 1.0e-9 * FMath::Abs(c.ExpectedAccel)
                * timeS + 1.0e-9));
        const FVector3d expectedPosition = start.PositionM + start.VelocityMps * timeS
            + accel * (NFM_DT * NFM_DT * steps * (steps + 1) * 0.5);
        TestTrue(FString::Printf(TEXT("%s: semi-implicit position"), c.Name),
            NfmVecNear(state.PositionM, expectedPosition, 1.0e-9 * expectedPosition.Size() + 1.0e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathStartOrientationTest,
    "SOLTest.NewtonianFlightMath.ThrustUsesStartOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The thrust of a step points along the orientation at the start of that step, even while spinning
bool FSOLNewtonianFlightMathStartOrientationTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    state.AngularVelocityRadPerS = FVector3d(0.0, 0.0, 1.5);
    const FSOLShipState after = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d(1.0, 0.0, 0.0),
        FVector3d::ZeroVector), env, FVector3d::ZeroVector, 0.1);
    TestTrue(TEXT("Delta-v along the start nose"), NfmVecNear(after.VelocityMps, FVector3d(5.0, 0.0, 0.0), 1.0e-12));
    TestTrue(TEXT("The orientation did turn this step"),
        NfmAngle(NfmForward(after.Orientation), FVector3d(1.0, 0.0, 0.0)) > 0.1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGravityOnlyTest, "SOLTest.NewtonianFlightMath.GravityOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Gravity is applied in every step as given, with no thrust needed
bool FSOLNewtonianFlightMathGravityOnlyTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    state.VelocityMps = FVector3d(100.0, 0.0, 0.0);
    const FVector3d gravity(0.0, -3.0, -9.81);
    const FSOLShipState after = SOLNewtonian::StepNewtonian(state, FSOLNewtonianCommand(), env, gravity, 0.5);
    TestTrue(TEXT("v += g dt"), NfmVecNear(after.VelocityMps, FVector3d(100.0, -1.5, -4.905), 1.0e-12));
    TestTrue(TEXT("x += v_new dt"), NfmVecNear(after.PositionM, FVector3d(50.0, -0.75, -2.4525), 1.0e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathSpeedClampTest, "SOLTest.NewtonianFlightMath.SpeedClampAndHuge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// |v| never exceeds the 0.5c clamp (direction kept) and huge inputs stay finite
bool FSOLNewtonianFlightMathSpeedClampTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    state.VelocityMps = FVector3d(NFM_HALF_C, 0.0, 0.0) * 0.999999;
    for (int32 i = 0; i < 100; ++i)
    {
        state = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d(1.0, 0.0, 0.0), FVector3d::ZeroVector, true),
            env, FVector3d::ZeroVector, 10.0);
        if (state.VelocityMps.Size() > NFM_HALF_C * (1.0 + 1.0e-12))
        {
            AddError(TEXT("Speed went above 0.5c"));
            break;
        }
    }
    TestTrue(TEXT("Speed sits at the clamp"), FMath::Abs(state.VelocityMps.Size() - NFM_HALF_C) <= NFM_HALF_C * 1.0e-9);
    TestTrue(TEXT("Clamp keeps the direction"), NfmAngle(state.VelocityMps, FVector3d(1.0, 0.0, 0.0)) < 1.0e-12);

    // A velocity far above the clamp is scaled down along its own direction
    FSOLShipState fast;
    fast.VelocityMps = FVector3d(3.0, -4.0, 12.0) * 1.0e8;
    const FSOLShipState clamped = SOLNewtonian::StepNewtonian(fast, FSOLNewtonianCommand(), env,
        FVector3d::ZeroVector, NFM_DT);
    TestTrue(TEXT("Over-speed scaled to the clamp"),
        FMath::Abs(clamped.VelocityMps.Size() - NFM_HALF_C) <= NFM_HALF_C * 1.0e-9);
    TestTrue(TEXT("Over-speed direction kept"), NfmAngle(clamped.VelocityMps, fast.VelocityMps) < 1.0e-9);

    // Huge dt, huge fractions and huge gravity stay finite
    FSOLShipState huge;
    huge.PositionM = FVector3d(1.0e13, -1.0e13, 1.0e12);
    huge = SOLNewtonian::StepNewtonian(huge, NfmCommand(FVector3d(1.0e12, -1.0e12, 1.0e300),
        FVector3d(1.0e12, 1.0e12, -1.0e12), true), env, FVector3d(1.0e9, 0.0, 0.0), 1.0e7);
    TestTrue(TEXT("Huge step is finite"), NfmStateFinite(huge));
    TestTrue(TEXT("Huge step respects the clamp"), huge.VelocityMps.Size() <= NFM_HALF_C * (1.0 + 1.0e-9));
    TestTrue(TEXT("Huge torque respects the spin clamp"),
        huge.AngularVelocityRadPerS.GetAbsMax() <= env.MaxSpinRateRadPerS * (1.0 + 1.0e-12));
    TestTrue(TEXT("Orientation stays unit length"), FMath::Abs(huge.Orientation.Size() - 1.0) < 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathStepNonFiniteTest, "SOLTest.NewtonianFlightMath.StepNonFinite",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-finite command components and gravity count as zero, so the step equals a coast
bool FSOLNewtonianFlightMathStepNonFiniteTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    state.PositionM = FVector3d(5.0, 6.0, 7.0);
    state.VelocityMps = FVector3d(10.0, -20.0, 30.0);
    state.AngularVelocityRadPerS = FVector3d(0.1, 0.2, -0.3);
    const FSOLShipState coast = SOLNewtonian::StepNewtonian(state, FSOLNewtonianCommand(), env, FVector3d::ZeroVector,
        NFM_DT);
    const FSOLShipState bad = SOLNewtonian::StepNewtonian(state, NfmCommand(NfmNaNVector(),
        FVector3d(NfmNaN(), NfmInf(), -NfmInf())), env, NfmNaNVector(), NFM_DT);
    TestTrue(TEXT("Result is finite"), NfmStateFinite(bad));
    TestTrue(TEXT("Velocity equals the coast"), NfmVecNear(bad.VelocityMps, coast.VelocityMps, 1.0e-12));
    TestTrue(TEXT("Position equals the coast"), NfmVecNear(bad.PositionM, coast.PositionM, 1.0e-12));
    TestTrue(TEXT("Angular velocity equals the coast"),
        NfmVecNear(bad.AngularVelocityRadPerS, coast.AngularVelocityRadPerS, 1.0e-12));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Rotation: torque, inertia, rate command, stop spin
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathTorqueInertiaTest, "SOLTest.NewtonianFlightMath.TorqueInertia",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Torque integrates to angular velocity at the axis's angular acceleration, and the spin persists afterwards
bool FSOLNewtonianFlightMathTorqueInertiaTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    for (int32 i = 0; i < 15; ++i)
    {
        state = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d::ZeroVector, FVector3d(0.0, 1.0, 0.0)), env,
            FVector3d::ZeroVector, NFM_DT);
    }
    TestTrue(TEXT("Pitch rate = 3 rad/s^2 * 0.5 s"),
        NfmVecNear(state.AngularVelocityRadPerS, FVector3d(0.0, 1.5, 0.0), 1.0e-12));

    // Half torque on roll and negative torque on yaw
    FSOLShipState mixed;
    mixed = SOLNewtonian::StepNewtonian(mixed, NfmCommand(FVector3d::ZeroVector, FVector3d(0.5, 0.0, -1.0)), env,
        FVector3d::ZeroVector, 0.25);
    TestTrue(TEXT("Per-axis angular acceleration"),
        NfmVecNear(mixed.AngularVelocityRadPerS, FVector3d(0.5, 0.0, -0.75), 1.0e-12));

    // Released: the rate is kept exactly (no damping) and the nose keeps rising
    const FVector3d spin = state.AngularVelocityRadPerS;
    const double noseZBefore = NfmForward(state.Orientation).Z;
    for (int32 i = 0; i < 10; ++i)
    {
        state = SOLNewtonian::StepNewtonian(state, FSOLNewtonianCommand(), env, FVector3d::ZeroVector, NFM_DT);
    }
    TestTrue(TEXT("Spin persists exactly without torque"), state.AngularVelocityRadPerS == spin);
    TestTrue(TEXT("Positive pitch raises the nose"), NfmForward(state.Orientation).Z > noseZBefore);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathSpinCapTest, "SOLTest.NewtonianFlightMath.SpinCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every angular-velocity component is clamped to the spin cap however long the torque is held
bool FSOLNewtonianFlightMathSpinCapTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    for (int32 i = 0; i < 300; ++i)
    {
        state = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d::ZeroVector, FVector3d(1.0, -1.0, 1.0)), env,
            FVector3d::ZeroVector, NFM_DT);
        if (state.AngularVelocityRadPerS.GetAbsMax() > env.MaxSpinRateRadPerS * (1.0 + 1.0e-12))
        {
            AddError(TEXT("Spin exceeded the cap"));
            break;
        }
    }
    const double cap = env.MaxSpinRateRadPerS;
    TestTrue(TEXT("All axes sit at the cap"), NfmVecNear(state.AngularVelocityRadPerS, FVector3d(cap, -cap, cap),
        1.0e-9));
    TestTrue(TEXT("Orientation stays unit length"), FMath::Abs(state.Orientation.Size() - 1.0) < 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAngularMomentumTest,
    "SOLTest.NewtonianFlightMath.AngularMomentumConservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A torque-free spin keeps its world axis and its rate (spherical inertia), and the quaternion stays normalized
bool FSOLNewtonianFlightMathAngularMomentumTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    state.Orientation = FQuat4d(FVector3d(0.3, -0.5, 0.8).GetSafeNormal(), 0.9);
    state.AngularVelocityRadPerS = FVector3d(0.3, -0.7, 1.1);
    const FSOLShipState first = SOLNewtonian::StepNewtonian(state, FSOLNewtonianCommand(), env, FVector3d::ZeroVector,
        NFM_DT);
    const FVector3d referenceRotation = NfmWorldRotationVector(state.Orientation, first.Orientation);
    TestTrue(TEXT("Per-step angle equals |w| dt"),
        FMath::Abs(referenceRotation.Size() - state.AngularVelocityRadPerS.Size() * NFM_DT) < 1.0e-9);

    FSOLShipState previous = first;
    double worstAxisError = 0.0;
    double worstNormError = 0.0;
    for (int32 i = 0; i < 3000; ++i)
    {
        const FSOLShipState next = SOLNewtonian::StepNewtonian(previous, FSOLNewtonianCommand(), env,
            FVector3d::ZeroVector, NFM_DT);
        worstAxisError = FMath::Max(worstAxisError,
            (NfmWorldRotationVector(previous.Orientation, next.Orientation) - referenceRotation).Size());
        worstNormError = FMath::Max(worstNormError, FMath::Abs(next.Orientation.Size() - 1.0));
        previous = next;
    }
    TestTrue(TEXT("World spin axis and rate constant"), worstAxisError < 1.0e-9);
    TestTrue(TEXT("Quaternion stays normalized"), worstNormError < 1.0e-12);
    TestTrue(TEXT("Body rates unchanged"), previous.AngularVelocityRadPerS == state.AngularVelocityRadPerS);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathSignConventionsTest,
    "SOLTest.NewtonianFlightMath.SignConventions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Positive pitch raises the nose, positive yaw turns right, positive roll drops the right wing (SDD 2)
bool FSOLNewtonianFlightMathSignConventionsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState pitch;
    pitch.AngularVelocityRadPerS = FVector3d(0.0, 0.2, 0.0);
    pitch = SOLNewtonian::StepNewtonian(pitch, FSOLNewtonianCommand(), env, FVector3d::ZeroVector, 0.5);
    TestTrue(TEXT("Pitch: forward gains +up"), NfmForward(pitch.Orientation).Z > 0.09);
    TestTrue(TEXT("Pitch: 0.1 rad turned"), FMath::Abs(NfmAngle(NfmForward(pitch.Orientation),
        FVector3d(1.0, 0.0, 0.0)) - 0.1) < 1.0e-9);

    FSOLShipState yaw;
    yaw.AngularVelocityRadPerS = FVector3d(0.0, 0.0, 0.2);
    yaw = SOLNewtonian::StepNewtonian(yaw, FSOLNewtonianCommand(), env, FVector3d::ZeroVector, 0.5);
    TestTrue(TEXT("Yaw: forward gains +right"), NfmForward(yaw.Orientation).Y > 0.09);

    FSOLShipState roll;
    roll.AngularVelocityRadPerS = FVector3d(0.2, 0.0, 0.0);
    roll = SOLNewtonian::StepNewtonian(roll, FSOLNewtonianCommand(), env, FVector3d::ZeroVector, 0.5);
    TestTrue(TEXT("Roll: up gains +right"), NfmUp(roll.Orientation).Y > 0.09);
    TestTrue(TEXT("Roll: nose unchanged"), NfmAngle(NfmForward(roll.Orientation), FVector3d(1.0, 0.0, 0.0)) < 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathRateCommandTest, "SOLTest.NewtonianFlightMath.RateCommand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The rate command is deadbeat, bounded by the angular acceleration, clamped to the rate limits and safe on bad input
bool FSOLNewtonianFlightMathRateCommandTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();

    // Within one step's reach: half torque lands exactly on the command
    const FVector3d small = SOLNewtonian::RateCommandAngularFraction(FVector3d::ZeroVector,
        FVector3d(0.0, 0.05, 0.0), env, NFM_DT);
    TestTrue(TEXT("Fraction = delta / (alpha dt)"), NfmVecNear(small, FVector3d(0.0, 0.5, 0.0), 1.0e-12));
    FSOLShipState state;
    state = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d::ZeroVector, small), env, FVector3d::ZeroVector,
        NFM_DT);
    TestTrue(TEXT("Lands exactly on the command"),
        NfmVecNear(state.AngularVelocityRadPerS, FVector3d(0.0, 0.05, 0.0), 1.0e-12));

    // Out of reach: saturates at +-1
    TestTrue(TEXT("Saturates toward a far command"), NfmVecNear(SOLNewtonian::RateCommandAngularFraction(
        FVector3d(0.0, 0.0, 1.0), FVector3d(1.0, 1.0, -1.0), env, NFM_DT), FVector3d(1.0, 1.0, -1.0), 1.0e-12));

    // The command is clamped to the rate limits: driving for 2 s ends exactly at the pitch limit, never above it
    FSOLShipState climb;
    double peak = 0.0;
    for (int32 i = 0; i < 60; ++i)
    {
        const FVector3d torque = SOLNewtonian::RateCommandAngularFraction(climb.AngularVelocityRadPerS,
            FVector3d(0.0, 10.0, 0.0), env, NFM_DT);
        climb = SOLNewtonian::StepNewtonian(climb, NfmCommand(FVector3d::ZeroVector, torque), env,
            FVector3d::ZeroVector, NFM_DT);
        peak = FMath::Max(peak, climb.AngularVelocityRadPerS.Y);
    }
    TestTrue(TEXT("Reaches the pitch rate limit"),
        FMath::Abs(climb.AngularVelocityRadPerS.Y - env.MaxAngularRateRadPerS.Y) < 1.0e-12);
    TestTrue(TEXT("Never exceeds the pitch rate limit"), peak <= env.MaxAngularRateRadPerS.Y + 1.0e-12);

    // An axis with no angular acceleration gets no torque
    FSOLThrustEnvelope noYaw = env;
    noYaw.MaxAngularAccelRadPerS2.Z = 0.0;
    TestTrue(TEXT("Zero alpha axis gives 0"), FMath::Abs(SOLNewtonian::RateCommandAngularFraction(
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, 1.0), noYaw, NFM_DT).Z) == 0.0);

    // Invalid inputs give zero
    TestTrue(TEXT("dt 0 gives zero"), SOLNewtonian::RateCommandAngularFraction(FVector3d(1.0, 1.0, 1.0),
        FVector3d::ZeroVector, env, 0.0).IsZero());
    TestTrue(TEXT("Negative dt gives zero"), SOLNewtonian::RateCommandAngularFraction(FVector3d(1.0, 1.0, 1.0),
        FVector3d::ZeroVector, env, -1.0).IsZero());
    TestTrue(TEXT("NaN dt gives zero"), SOLNewtonian::RateCommandAngularFraction(FVector3d(1.0, 1.0, 1.0),
        FVector3d::ZeroVector, env, NfmNaN()).IsZero());
    TestTrue(TEXT("NaN rate gives zero"), SOLNewtonian::RateCommandAngularFraction(NfmNaNVector(),
        FVector3d::ZeroVector, env, NFM_DT).IsZero());
    TestTrue(TEXT("NaN command gives zero"), SOLNewtonian::RateCommandAngularFraction(FVector3d::ZeroVector,
        NfmNaNVector(), env, NFM_DT).IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathStopSpinTest, "SOLTest.NewtonianFlightMath.StopSpin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stop spin (rate command zero) cancels every axis monotonically, without a sign change, in |w| / alpha
bool FSOLNewtonianFlightMathStopSpinTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    state.AngularVelocityRadPerS = FVector3d(2.0, -1.5, 0.75);
    state.Orientation = FQuat4d(FVector3d(0.0, 1.0, 1.0).GetSafeNormal(), 2.0);
    const FVector3d start = state.AngularVelocityRadPerS;
    bool bMonotone = true;
    bool bSignKept = true;
    int32 stepsToStop = -1;
    for (int32 i = 0; i < 60; ++i)
    {
        const FVector3d before = state.AngularVelocityRadPerS;
        const FVector3d torque = SOLNewtonian::RateCommandAngularFraction(before, FVector3d::ZeroVector, env, NFM_DT);
        state = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d::ZeroVector, torque), env,
            FVector3d::ZeroVector, NFM_DT);
        for (int32 axis = 0; axis < 3; ++axis)
        {
            bMonotone &= FMath::Abs(state.AngularVelocityRadPerS[axis]) <= FMath::Abs(before[axis]) + 1.0e-15;
            bSignKept &= state.AngularVelocityRadPerS[axis] * start[axis] >= -1.0e-15;
        }
        if (stepsToStop < 0 && state.AngularVelocityRadPerS.GetAbsMax() < 1.0e-12)
        {
            stepsToStop = i + 1;
        }
    }
    TestTrue(TEXT("Each axis decreases monotonically"), bMonotone);
    TestTrue(TEXT("No axis reverses"), bSignKept);
    TestTrue(TEXT("Spin fully cancelled"), stepsToStop > 0);

    // Slowest axis: 2.0 / 4.0 = 1.5 / 3.0 = 0.5 s = 15 steps, plus at most one rounding step
    TestTrue(TEXT("Stops in max(|w_i| / alpha_i)"), stepsToStop >= 15 && stepsToStop <= 16);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathRotationTimeTest, "SOLTest.NewtonianFlightMath.RotationTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RotationTimeS covers the trapezoid and triangle profiles and the invalid inputs
bool FSOLNewtonianFlightMathRotationTimeTest::RunTest(const FString& /*parameters*/)
{
    const double maxD = TNumericLimits<double>::Max();

    // Trapezoid: PI at 1.5708 rad/s and 3 rad/s^2 (rate^2 / alpha = 0.822 <= PI)
    const double rate = 0.5 * UE_DOUBLE_PI;
    TestTrue(TEXT("Trapezoid"), FMath::Abs(SOLNewtonian::RotationTimeS(UE_DOUBLE_PI, rate, 3.0)
        - (UE_DOUBLE_PI / rate + rate / 3.0)) < NFM_EPS);
    TestTrue(TEXT("Negative angle uses |angle|"), FMath::Abs(SOLNewtonian::RotationTimeS(-UE_DOUBLE_PI, rate, 3.0)
        - (UE_DOUBLE_PI / rate + rate / 3.0)) < NFM_EPS);

    // Triangle: 0.1 rad never reaches the rate
    TestTrue(TEXT("Triangle"), FMath::Abs(SOLNewtonian::RotationTimeS(0.1, rate, 3.0)
        - 2.0 * FMath::Sqrt(0.1 / 3.0)) < NFM_EPS);

    // Boundary theta = rate^2 / alpha: both formulas agree
    const double boundary = rate * rate / 3.0;
    TestTrue(TEXT("Continuous at the boundary"), FMath::Abs(SOLNewtonian::RotationTimeS(boundary, rate, 3.0)
        - 2.0 * FMath::Sqrt(boundary / 3.0)) < 1.0e-9);

    TestTrue(TEXT("Infinite rate is a triangle"), FMath::Abs(SOLNewtonian::RotationTimeS(UE_DOUBLE_PI, NfmInf(), 3.0)
        - 2.0 * FMath::Sqrt(UE_DOUBLE_PI / 3.0)) < NFM_EPS);
    TestEqual(TEXT("Zero angle"), SOLNewtonian::RotationTimeS(0.0, rate, 3.0), 0.0);
    TestEqual(TEXT("Zero alpha"), SOLNewtonian::RotationTimeS(1.0, rate, 0.0), maxD);
    TestEqual(TEXT("Negative alpha"), SOLNewtonian::RotationTimeS(1.0, rate, -1.0), maxD);
    TestEqual(TEXT("Zero rate"), SOLNewtonian::RotationTimeS(1.0, 0.0, 3.0), maxD);
    TestEqual(TEXT("NaN rate"), SOLNewtonian::RotationTimeS(1.0, NfmNaN(), 3.0), maxD);
    TestEqual(TEXT("NaN angle"), SOLNewtonian::RotationTimeS(NfmNaN(), rate, 3.0), maxD);
    TestEqual(TEXT("Infinite angle"), SOLNewtonian::RotationTimeS(NfmInf(), rate, 3.0), maxD);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Attitude pointing
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAttitudeSignsTest, "SOLTest.NewtonianFlightMath.AttitudeSigns",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The attitude law commands the right axis with the right sign, nothing when aligned, and pitch for a flip
bool FSOLNewtonianFlightMathAttitudeSignsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    const FQuat4d identity = FQuat4d::Identity;

    TestTrue(TEXT("Aligned gives zero"), SOLNewtonian::AttitudeRateCommand(identity, FVector3d(1.0, 0.0, 0.0),
        FVector3d(0.0, 0.0, 1.0), env).Size() < 1.0e-9);

    const FVector3d right = SOLNewtonian::AttitudeRateCommand(identity, FVector3d(0.0, 1.0, 0.0),
        FVector3d::ZeroVector, env);
    TestTrue(TEXT("Target to the right: positive yaw at the limit"),
        FMath::Abs(right.Z - env.MaxAngularRateRadPerS.Z) < 1.0e-9);
    TestTrue(TEXT("Target to the right: no pitch or roll"),
        FMath::Abs(right.X) < 1.0e-9 && FMath::Abs(right.Y) < 1.0e-9);

    const FVector3d up = SOLNewtonian::AttitudeRateCommand(identity, FVector3d(0.0, 0.0, 1.0), FVector3d::ZeroVector,
        env);
    TestTrue(TEXT("Target above: positive pitch"),
        up.Y > 1.0 && FMath::Abs(up.Z) < 1.0e-9 && FMath::Abs(up.X) < 1.0e-9);

    const FVector3d left = SOLNewtonian::AttitudeRateCommand(identity, FVector3d(0.0, -1.0, 0.0),
        FVector3d::ZeroVector, env);
    TestTrue(TEXT("Target to the left: negative yaw"), left.Z < -1.0);

    const FVector3d rollRight = SOLNewtonian::AttitudeRateCommand(identity, FVector3d(1.0, 0.0, 0.0),
        FVector3d(0.0, 1.0, 0.0), env);
    TestTrue(TEXT("Desired up to the right: positive roll"), rollRight.X > 1.0);
    TestTrue(TEXT("Roll only"), FMath::Abs(rollRight.Y) < 1.0e-9 && FMath::Abs(rollRight.Z) < 1.0e-9);

    const FVector3d flip = SOLNewtonian::AttitudeRateCommand(identity, FVector3d(-1.0, 0.0, 0.0),
        FVector3d::ZeroVector, env);
    TestTrue(TEXT("Antiparallel: positive pitch at the limit"),
        FMath::Abs(flip.Y - env.MaxAngularRateRadPerS.Y) < 1.0e-9);
    TestTrue(TEXT("Antiparallel: no roll or yaw"), FMath::Abs(flip.X) < 1.0e-9 && FMath::Abs(flip.Z) < 1.0e-9);

    // Works in a rotated frame too: a nose yawed 90 deg left of +X, target +X, means a right turn
    const FQuat4d yawedLeft(FVector3d(0.0, 0.0, 1.0), -0.5 * UE_DOUBLE_PI);
    TestTrue(TEXT("Rotated frame: positive yaw"), SOLNewtonian::AttitudeRateCommand(yawedLeft,
        FVector3d(1.0, 0.0, 0.0), FVector3d::ZeroVector, env).Z > 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAttitudeRateLawTest,
    "SOLTest.NewtonianFlightMath.AttitudeRateLaw",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Per-axis rate = min(limit, sqrt(2 * 0.8 * alpha * e), 4 * e), and zero alpha means no rate
bool FSOLNewtonianFlightMathAttitudeRateLawTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    const FQuat4d identity = FQuat4d::Identity;

    // Yaw errors of 0.5 (square-root term), 0.1 (linear term) and 1.2 rad (rate limit), alpha 3
    struct FCase { double ErrorRad; double Expected; };
    const FCase cases[] = {
        { 0.5, FMath::Sqrt(2.0 * 0.8 * 3.0 * 0.5) },
        { 0.1, 4.0 * 0.1 },
        { 1.2, env.MaxAngularRateRadPerS.Z },
    };
    for (const FCase& c : cases)
    {
        const FVector3d desired(FMath::Cos(c.ErrorRad), FMath::Sin(c.ErrorRad), 0.0);
        const FVector3d rate = SOLNewtonian::AttitudeRateCommand(identity, desired, FVector3d::ZeroVector, env);
        TestTrue(FString::Printf(TEXT("Yaw rate for %.2f rad"), c.ErrorRad), FMath::Abs(rate.Z - c.Expected) < 1.0e-9);
        TestTrue(FString::Printf(TEXT("No pitch for %.2f rad"), c.ErrorRad), FMath::Abs(rate.Y) < 1.0e-9);

        // Pitch error of the same size (nose down target gives negative pitch)
        const FVector3d below(FMath::Cos(c.ErrorRad), 0.0, -FMath::Sin(c.ErrorRad));
        const FVector3d pitch = SOLNewtonian::AttitudeRateCommand(identity, below, FVector3d::ZeroVector, env);
        const double expectedPitch = FMath::Min(c.Expected, env.MaxAngularRateRadPerS.Y);
        TestTrue(FString::Printf(TEXT("Pitch rate for -%.2f rad"), c.ErrorRad),
            FMath::Abs(pitch.Y + expectedPitch) < 1.0e-9);
    }

    // Roll error 0.5 rad, alpha 4: sqrt(2 * 0.8 * 4 * 0.5)
    const FVector3d desiredUp(0.0, FMath::Sin(0.5), FMath::Cos(0.5));
    const FVector3d roll = SOLNewtonian::AttitudeRateCommand(identity, FVector3d(1.0, 0.0, 0.0), desiredUp, env);
    TestTrue(TEXT("Roll rate law"), FMath::Abs(roll.X - FMath::Sqrt(2.0 * 0.8 * 4.0 * 0.5)) < 1.0e-9);

    // Zero alpha on an axis: no rate on that axis
    FSOLThrustEnvelope noYaw = env;
    noYaw.MaxAngularAccelRadPerS2.Z = 0.0;
    TestTrue(TEXT("Zero alpha axis gives 0"), FMath::Abs(SOLNewtonian::AttitudeRateCommand(identity,
        FVector3d(0.0, 1.0, 0.0), FVector3d::ZeroVector, noYaw).Z) < 1.0e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAttitudeInvalidTest,
    "SOLTest.NewtonianFlightMath.AttitudeInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or non-finite desired forward gives zero; a degenerate desired up leaves roll alone
bool FSOLNewtonianFlightMathAttitudeInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    const FQuat4d identity = FQuat4d::Identity;
    TestTrue(TEXT("Zero forward"), SOLNewtonian::AttitudeRateCommand(identity, FVector3d::ZeroVector,
        FVector3d(0.0, 1.0, 0.0), env).IsZero());
    TestTrue(TEXT("NaN forward"), SOLNewtonian::AttitudeRateCommand(identity, NfmNaNVector(),
        FVector3d(0.0, 0.0, 1.0), env).IsZero());
    TestTrue(TEXT("NaN orientation"), SOLNewtonian::AttitudeRateCommand(FQuat4d(NfmNaN(), 0.0, 0.0, 1.0),
        FVector3d(1.0, 0.0, 0.0), FVector3d::ZeroVector, env).IsZero());

    // Up along the nose (no perpendicular component) or NaN: roll error 0
    const FVector3d upAlongNose = SOLNewtonian::AttitudeRateCommand(identity, FVector3d(1.0, 0.0, 0.0),
        FVector3d(5.0, 0.0, 0.0), env);
    TestTrue(TEXT("Up parallel to the nose: no roll"), upAlongNose.Size() < 1.0e-9);
    const FVector3d nanUp = SOLNewtonian::AttitudeRateCommand(identity, FVector3d(1.0, 0.0, 0.0), NfmNaNVector(), env);
    TestTrue(TEXT("NaN up: no roll"), SOLTestHelpers::IsFiniteVector(nanUp) && nanUp.Size() < 1.0e-9);

    // A non-normalized forward is normalized
    TestTrue(TEXT("Forward length does not matter"), NfmVecNear(SOLNewtonian::AttitudeRateCommand(identity,
        FVector3d(0.0, 1000.0, 0.0), FVector3d::ZeroVector, env), SOLNewtonian::AttitudeRateCommand(identity,
        FVector3d(0.0, 1.0, 0.0), FVector3d::ZeroVector, env), 1.0e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathFlipClosedLoopTest,
    "SOLTest.NewtonianFlightMath.AttitudeFlipClosedLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 180 deg flip through attitude + rate command + step settles in a bounded time, as a pure pitch, inside the limits
bool FSOLNewtonianFlightMathFlipClosedLoopTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FSOLShipState state;
    FVector3d peak;
    const double ideal = SOLNewtonian::RotationTimeS(UE_DOUBLE_PI, env.MaxAngularRateRadPerS.Y,
        0.8 * env.MaxAngularAccelRadPerS2.Y);
    const double timeS = NfmTurnUntilAligned(env, state, FVector3d(-1.0, 0.0, 0.0), FVector3d::ZeroVector, 1.0e-3,
        20.0, peak);
    TestTrue(TEXT("Flip completes"), timeS > 0.0);
    TestTrue(TEXT("Flip takes at least the physical minimum"), timeS >= SOLNewtonian::RotationTimeS(UE_DOUBLE_PI,
        env.MaxAngularRateRadPerS.Y, env.MaxAngularAccelRadPerS2.Y) - NFM_DT);
    TestTrue(TEXT("Flip within 1.5x the planned time + 1 s"), timeS <= 1.5 * ideal + 1.0);
    TestTrue(TEXT("Pitch rate within the limit"), peak.Y <= env.MaxAngularRateRadPerS.Y + 1.0e-9);
    TestTrue(TEXT("Pure pitch: no roll or yaw"), peak.X < 1.0e-6 && peak.Z < 1.0e-6);

    // Once aligned with stop-spin-like settling, holding the attitude keeps it
    for (int32 i = 0; i < 90; ++i)
    {
        const FVector3d rate = SOLNewtonian::AttitudeRateCommand(state.Orientation, FVector3d(-1.0, 0.0, 0.0),
            FVector3d::ZeroVector, env);
        const FVector3d torque = SOLNewtonian::RateCommandAngularFraction(state.AngularVelocityRadPerS, rate, env,
            NFM_DT);
        state = SOLNewtonian::StepNewtonian(state, NfmCommand(FVector3d::ZeroVector, torque), env,
            FVector3d::ZeroVector, NFM_DT);
    }
    TestTrue(TEXT("Holds the new attitude"), NfmAngle(NfmForward(state.Orientation), FVector3d(-1.0, 0.0, 0.0))
        < 1.0e-3);
    TestTrue(TEXT("Comes to rest"), state.AngularVelocityRadPerS.Size() < 1.0e-2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAttitudeRandomTest,
    "SOLTest.NewtonianFlightMath.AttitudeRandomClosedLoop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From random orientations and spins, pointing the nose and the up vector converges inside the rate limits
bool FSOLNewtonianFlightMathAttitudeRandomTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    FRandomStream stream(NFM_SEED);
    for (int32 trial = 0; trial < 20; ++trial)
    {
        FSOLShipState state;
        state.Orientation = FQuat4d(FVector3d(stream.VRand()).GetSafeNormal(), NfmRange(stream, 0.0, UE_DOUBLE_PI));
        state.AngularVelocityRadPerS = FVector3d(NfmRange(stream, -1.0, 1.0), NfmRange(stream, -1.0, 1.0),
            NfmRange(stream, -1.0, 1.0));
        const FVector3d forward = FVector3d(stream.VRand()).GetSafeNormal();
        FVector3d up = FVector3d(stream.VRand()).GetSafeNormal();
        up = (up - forward * FVector3d::DotProduct(up, forward)).GetSafeNormal();
        FVector3d peak;
        const double timeS = NfmTurnUntilAligned(env, state, forward, up, 2.0e-3, 15.0, peak);
        TestTrue(FString::Printf(TEXT("Trial %d converges"), trial), timeS >= 0.0);
        TestTrue(FString::Printf(TEXT("Trial %d stays finite"), trial), NfmStateFinite(state));

        // A start spin above a rate limit is only braked, never sped up beyond max(start, limit)
        TestTrue(FString::Printf(TEXT("Trial %d respects the rate limits"), trial),
            peak.X <= FMath::Max(env.MaxAngularRateRadPerS.X, 1.0) + 1.0e-9
            && peak.Y <= FMath::Max(env.MaxAngularRateRadPerS.Y, 1.0) + 1.0e-9
            && peak.Z <= FMath::Max(env.MaxAngularRateRadPerS.Z, 1.0) + 1.0e-9);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Thrust allocation and the assist controller
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAllocateInsideTest,
    "SOLTest.NewtonianFlightMath.AllocateInsideEnvelope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A request inside the envelope is delivered exactly, unsaturated, with the matching fractions
bool FSOLNewtonianFlightMathAllocateInsideTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    const FSOLThrustAllocation a = SOLNewtonian::AllocateThrust(FVector3d(40.0, -15.0, 6.0), FQuat4d::Identity, env,
        false);
    TestTrue(TEXT("Fractions"), NfmVecNear(a.ThrustFraction, FVector3d(0.8, -0.5, 0.2), 1.0e-12));
    TestTrue(TEXT("Achieved = desired"), NfmVecNear(a.AchievedAccelWorldMps2, FVector3d(40.0, -15.0, 6.0), 1.0e-9));
    TestFalse(TEXT("Not saturated"), a.bSaturated);

    const FSOLThrustAllocation reverse = SOLNewtonian::AllocateThrust(FVector3d(-20.0, 0.0, 0.0), FQuat4d::Identity,
        env, false);
    TestTrue(TEXT("Reverse fraction uses the reverse limit"),
        NfmVecNear(reverse.ThrustFraction, FVector3d(-0.8, 0.0, 0.0), 1.0e-12));
    TestFalse(TEXT("Reverse not saturated"), reverse.bSaturated);

    const FSOLThrustAllocation boosted = SOLNewtonian::AllocateThrust(FVector3d(100.0, 0.0, 0.0), FQuat4d::Identity,
        env, true);
    TestTrue(TEXT("Boost raises the limit"), NfmVecNear(boosted.ThrustFraction, FVector3d(100.0 / 150.0, 0.0, 0.0),
        1.0e-12));
    TestFalse(TEXT("Boosted not saturated"), boosted.bSaturated);

    // A yawed ship: world +Y is its nose, so 40 m/s^2 along world +Y is a 0.8 forward fraction
    const FQuat4d yawed(FVector3d(0.0, 0.0, 1.0), 0.5 * UE_DOUBLE_PI);
    const FVector3d worldRequest = yawed.RotateVector(FVector3d(40.0, 0.0, 0.0));
    const FSOLThrustAllocation rotated = SOLNewtonian::AllocateThrust(worldRequest, yawed, env, false);
    TestTrue(TEXT("Rotated fractions"), NfmVecNear(rotated.ThrustFraction, FVector3d(0.8, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Rotated achieved"), NfmVecNear(rotated.AchievedAccelWorldMps2, worldRequest, 1.0e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAllocateSaturatedTest,
    "SOLTest.NewtonianFlightMath.AllocatePreservesDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An over-limit request is scaled down as a whole (direction kept) until its tightest axis fits
bool FSOLNewtonianFlightMathAllocateSaturatedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    struct FCase { FVector3d Desired; FVector3d Fraction; FVector3d Achieved; };
    const FCase cases[] = {
        { FVector3d(-40.0, 0.0, 0.0), FVector3d(-1.0, 0.0, 0.0), FVector3d(-25.0, 0.0, 0.0) },
        { FVector3d(100.0, 60.0, 0.0), FVector3d(1.0, 1.0, 0.0), FVector3d(50.0, 30.0, 0.0) },
        { FVector3d(40.0, 0.0, 45.0), FVector3d(40.0 / 45.0 * 30.0 / 50.0, 0.0, 1.0),
            FVector3d(40.0 * 30.0 / 45.0, 0.0, 30.0) },
        { FVector3d(-50.0, -10.0, 10.0), FVector3d(-1.0, -0.2 * 25.0 / 30.0, 0.2 * 25.0 / 30.0),
            FVector3d(-25.0, -5.0, 5.0) },
    };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(cases)); ++i)
    {
        const FCase& c = cases[i];
        const FSOLThrustAllocation a = SOLNewtonian::AllocateThrust(c.Desired, FQuat4d::Identity, env, false);
        TestTrue(FString::Printf(TEXT("Case %d fractions"), i), NfmVecNear(a.ThrustFraction, c.Fraction, 1.0e-9));
        TestTrue(FString::Printf(TEXT("Case %d achieved"), i), NfmVecNear(a.AchievedAccelWorldMps2, c.Achieved,
            1.0e-9));
        TestTrue(FString::Printf(TEXT("Case %d direction kept"), i),
            NfmAngle(a.AchievedAccelWorldMps2, c.Desired) < 1.0e-9);
        TestTrue(FString::Printf(TEXT("Case %d saturated"), i), a.bSaturated);
        TestTrue(FString::Printf(TEXT("Case %d fractions inside the box"), i), a.ThrustFraction.GetAbsMax()
            <= 1.0 + 1.0e-12);
    }

    // An axis with no thrust at all cannot produce any of a request that needs it: everything is scaled to zero
    FSOLThrustEnvelope noLateral = env;
    noLateral.LateralAccelMps2 = 0.0;
    const FSOLThrustAllocation none = SOLNewtonian::AllocateThrust(FVector3d(10.0, 5.0, 0.0), FQuat4d::Identity,
        noLateral, false);
    TestTrue(TEXT("Missing axis gives zero"), none.ThrustFraction.IsZero() && none.AchievedAccelWorldMps2.IsZero());
    TestTrue(TEXT("Missing axis is saturated"), none.bSaturated);

    // The total cap also limits the request, after the per-axis limits
    FSOLThrustEnvelope capped = env;
    capped.MaxTotalAccelMps2 = 20.0;
    const FSOLThrustAllocation totalCapped = SOLNewtonian::AllocateThrust(FVector3d(30.0, 0.0, 40.0),
        FQuat4d::Identity, capped, false);
    TestTrue(TEXT("Total cap: achieved length"), FMath::Abs(totalCapped.AchievedAccelWorldMps2.Size() - 20.0)
        < 1.0e-9);
    TestTrue(TEXT("Total cap: direction kept"), NfmAngle(totalCapped.AchievedAccelWorldMps2,
        FVector3d(30.0, 0.0, 40.0)) < 1.0e-9);
    TestTrue(TEXT("Total cap: fractions"), NfmVecNear(totalCapped.ThrustFraction, FVector3d(12.0 / 50.0, 0.0,
        16.0 / 30.0), 1.0e-9));
    TestTrue(TEXT("Total cap: saturated"), totalCapped.bSaturated);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAllocateInvalidTest,
    "SOLTest.NewtonianFlightMath.AllocateInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Zero and non-finite requests or orientations give zero thrust, not saturated
bool FSOLNewtonianFlightMathAllocateInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    const FSOLThrustAllocation zero = SOLNewtonian::AllocateThrust(FVector3d::ZeroVector, FQuat4d::Identity, env,
        false);
    TestTrue(TEXT("Zero request"), zero.ThrustFraction.IsZero() && zero.AchievedAccelWorldMps2.IsZero()
        && !zero.bSaturated);
    const FSOLThrustAllocation nan = SOLNewtonian::AllocateThrust(NfmNaNVector(), FQuat4d::Identity, env, false);
    TestTrue(TEXT("NaN request"),
        nan.ThrustFraction.IsZero() && nan.AchievedAccelWorldMps2.IsZero() && !nan.bSaturated);
    const FSOLThrustAllocation inf = SOLNewtonian::AllocateThrust(FVector3d(NfmInf(), 0.0, 0.0), FQuat4d::Identity,
        env, false);
    TestTrue(TEXT("Infinite request"), inf.ThrustFraction.IsZero() && !inf.bSaturated);
    const FSOLThrustAllocation badQ = SOLNewtonian::AllocateThrust(FVector3d(1.0, 0.0, 0.0),
        FQuat4d(NfmNaN(), 0.0, 0.0, 1.0), env, false);
    TestTrue(TEXT("NaN orientation"), badQ.ThrustFraction.IsZero() && !badQ.bSaturated);

    // A huge finite request is scaled into the box, finite
    const FSOLThrustAllocation huge = SOLNewtonian::AllocateThrust(FVector3d(1.0e150, -1.0e150, 1.0e150),
        FQuat4d::Identity, env, false);
    TestTrue(TEXT("Huge request finite"), SOLTestHelpers::IsFiniteVector(huge.AchievedAccelWorldMps2)
        && huge.ThrustFraction.GetAbsMax() <= 1.0 + 1.0e-12 && huge.bSaturated);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathVelocityHoldTest, "SOLTest.NewtonianFlightMath.VelocityHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// VelocityHoldAccelMps2 = (target - v) / max(tau, dt, 1 ms) - g, and zero on bad vectors
bool FSOLNewtonianFlightMathVelocityHoldTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d v(10.0, 0.0, 0.0);
    const FVector3d g(0.0, 0.0, -2.0);
    TestTrue(TEXT("tau sets the divisor"), NfmVecNear(SOLNewtonian::VelocityHoldAccelMps2(v, FVector3d::ZeroVector, g,
        0.5, NFM_DT), FVector3d(-20.0, 0.0, 2.0), 1.0e-12));
    TestTrue(TEXT("dt floors tau"), NfmVecNear(SOLNewtonian::VelocityHoldAccelMps2(v, FVector3d::ZeroVector, g, 0.0,
        0.1), FVector3d(-100.0, 0.0, 2.0), 1.0e-9));
    TestTrue(TEXT("1 ms floor"), NfmVecNear(SOLNewtonian::VelocityHoldAccelMps2(v, FVector3d::ZeroVector, g, 0.0, 0.0),
        FVector3d(-10000.0, 0.0, 2.0), 1.0e-6));
    TestTrue(TEXT("Negative tau counts as 0"), NfmVecNear(SOLNewtonian::VelocityHoldAccelMps2(v, FVector3d::ZeroVector,
        g, -5.0, 0.1), FVector3d(-100.0, 0.0, 2.0), 1.0e-9));
    TestTrue(TEXT("NaN tau counts as 0"), NfmVecNear(SOLNewtonian::VelocityHoldAccelMps2(v, FVector3d::ZeroVector,
        g, NfmNaN(), 0.1), FVector3d(-100.0, 0.0, 2.0), 1.0e-9));
    TestTrue(TEXT("At the target: pure gravity compensation"), NfmVecNear(SOLNewtonian::VelocityHoldAccelMps2(v, v, g,
        0.75, NFM_DT), FVector3d(0.0, 0.0, 2.0), 1.0e-12));
    TestTrue(TEXT("NaN velocity"), SOLNewtonian::VelocityHoldAccelMps2(NfmNaNVector(), v, g, 0.5, NFM_DT).IsZero());
    TestTrue(TEXT("NaN target"), SOLNewtonian::VelocityHoldAccelMps2(v, NfmNaNVector(), g, 0.5, NFM_DT).IsZero());
    TestTrue(TEXT("NaN gravity"), SOLNewtonian::VelocityHoldAccelMps2(v, v, NfmNaNVector(), 0.5, NFM_DT).IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGravityHoverTest,
    "SOLTest.NewtonianFlightMath.GravityCompensationHover",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Hovering spends envelope on gravity; gravity beyond the envelope saturates; a closed-loop hover holds still
bool FSOLNewtonianFlightMathGravityHoverTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    const FVector3d earthG(0.0, 0.0, -9.81);
    const FVector3d hold = SOLNewtonian::VelocityHoldAccelMps2(FVector3d::ZeroVector, FVector3d::ZeroVector, earthG,
        0.75, NFM_DT);
    const FSOLThrustAllocation hover = SOLNewtonian::AllocateThrust(hold, FQuat4d::Identity, env, false);
    TestTrue(TEXT("Vertical fraction = g / vertical limit"),
        NfmVecNear(hover.ThrustFraction, FVector3d(0.0, 0.0, 9.81 / 30.0), 1.0e-12));
    TestTrue(TEXT("Net acceleration zero"), NfmVecNear(hover.AchievedAccelWorldMps2 + earthG, FVector3d::ZeroVector,
        1.0e-9));
    TestFalse(TEXT("Hover not saturated"), hover.bSaturated);

    const FVector3d strongG(0.0, 0.0, -100.0);
    const FSOLThrustAllocation sink = SOLNewtonian::AllocateThrust(SOLNewtonian::VelocityHoldAccelMps2(
        FVector3d::ZeroVector, FVector3d::ZeroVector, strongG, 0.75, NFM_DT), FQuat4d::Identity, env, false);
    TestTrue(TEXT("Gravity beyond the envelope saturates"), sink.bSaturated);
    TestTrue(TEXT("Full vertical thrust"), NfmVecNear(sink.AchievedAccelWorldMps2, FVector3d(0.0, 0.0, 30.0), 1.0e-9));

    // Closed loop for 10 s: the ship stays put to well under a metre
    FSOLShipState state;
    for (int32 i = 0; i < 300; ++i)
    {
        const FVector3d desired = SOLNewtonian::VelocityHoldAccelMps2(state.VelocityMps, FVector3d::ZeroVector,
            earthG, 0.75, NFM_DT);
        const FSOLThrustAllocation a = SOLNewtonian::AllocateThrust(desired, state.Orientation, env, false);
        state = SOLNewtonian::StepNewtonian(state, NfmCommand(a.ThrustFraction, FVector3d::ZeroVector), env, earthG,
            NFM_DT);
    }
    TestTrue(TEXT("Closed-loop hover holds position"), state.PositionM.Size() < 1.0e-6);

    // Gravity compensation counts toward the pilot limit: hovering in 2.5 G needs 2.5 G of thrust, so the assisted
    // request clamped at the 1.5 G sustained limit cannot hold altitude
    const FVector3d jupiterG(0.0, 0.0, -2.5 * NFM_G0);
    const double sustained = SOLNewtonian::PilotSustainedAccelMps2(NfmLimits());
    const FVector3d clamped = SOLNewtonian::ClampToPilotLimit(SOLNewtonian::VelocityHoldAccelMps2(
        FVector3d::ZeroVector, FVector3d::ZeroVector, jupiterG, 0.75, NFM_DT), sustained);
    const FSOLThrustAllocation weak = SOLNewtonian::AllocateThrust(clamped, FQuat4d::Identity, NfmPlayerEnvelope(),
        false);
    TestTrue(TEXT("Pilot-limited hover thrust is 1.5 G"), NfmVecNear(weak.AchievedAccelWorldMps2,
        FVector3d(0.0, 0.0, 1.5 * NFM_G0), 1.0e-9));
    TestTrue(TEXT("Pilot-limited hover sinks"), (weak.AchievedAccelWorldMps2 + jupiterG).Z < 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAssistPilotLimitTest,
    "SOLTest.NewtonianFlightMath.AssistRespectsPilotLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Assisted flight (velocity hold -> pilot clamp -> allocation) never exceeds the 1.5 G sustained limit even though
// the ship can do 3 G: 1000 m/s takes ~68 s, the G meter never drains
bool FSOLNewtonianFlightMathAssistPilotLimitTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmPlayerEnvelope();
    const FSOLPilotGLimits limits = NfmLimits();
    const double sustained = SOLNewtonian::PilotSustainedAccelMps2(limits);
    TestTrue(TEXT("Sustained limit = 1.5 G"), FMath::Abs(sustained - 1.5 * NFM_G0) < 1.0e-12);
    const FVector3d target(1000.0, 0.0, 0.0);
    FSOLShipState state;
    FSOLGMeterState meter = NfmMeter(1.0);
    double settledS = -1.0;
    double maxG = 0.0;
    bool bWithinLimit = true;
    for (int32 i = 0; i < 2700; ++i)
    {
        const FVector3d before = state.VelocityMps;
        const FVector3d desired = SOLNewtonian::ClampToPilotLimit(SOLNewtonian::VelocityHoldAccelMps2(
            state.VelocityMps, target, FVector3d::ZeroVector, 0.75, NFM_DT), sustained);
        const FSOLThrustAllocation a = SOLNewtonian::AllocateThrust(desired, state.Orientation, env, false);
        state = SOLNewtonian::StepNewtonian(state, NfmCommand(a.ThrustFraction, FVector3d::ZeroVector), env,
            FVector3d::ZeroVector, NFM_DT);
        const double gLoad = SOLNewtonian::GLoadG(a.AchievedAccelWorldMps2);
        maxG = FMath::Max(maxG, gLoad);
        meter = SOLNewtonian::StepGMeter(meter, gLoad, limits, NFM_DT);
        bWithinLimit &= (state.VelocityMps - before).Size() <= sustained * NFM_DT * (1.0 + 1.0e-9);
        if (settledS < 0.0 && (state.VelocityMps - target).Size() < 1.0)
        {
            settledS = (i + 1) * NFM_DT;
        }
    }
    TestTrue(TEXT("Every step within 1.5 G"), bWithinLimit);
    TestTrue(TEXT("Peak G-load at most 1.5"), maxG <= 1.5 * (1.0 + 1.0e-9));
    TestTrue(TEXT("Peak G-load reaches 1.5"), maxG >= 1.5 * (1.0 - 1.0e-9));
    TestTrue(TEXT("Meter never drains"), meter.Meter == 1.0 && !meter.bBlackout);
    TestTrue(TEXT("Settles"), settledS > 0.0);
    TestTrue(TEXT("Not faster than 1.5 G allows"), settledS >= (1000.0 - 1.0) / sustained - NFM_DT);
    TestTrue(TEXT("Linear ramp then a short tau tail"), settledS <= 72.5);
    TestTrue(TEXT("No overshoot"), state.VelocityMps.X <= 1000.0 + 1.0e-6);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Braking curve
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathStoppingDistanceTest,
    "SOLTest.NewtonianFlightMath.StoppingDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// StoppingDistanceM = v * tf + v^2 / (2a), with |v|, a negative flip time as 0, and DBL_MAX when it cannot stop
bool FSOLNewtonianFlightMathStoppingDistanceTest::RunTest(const FString& /*parameters*/)
{
    const double maxD = TNumericLimits<double>::Max();
    TestTrue(TEXT("No flip"), FMath::Abs(SOLNewtonian::StoppingDistanceM(100.0, 20.0, 0.0) - 250.0) < NFM_EPS);
    TestTrue(TEXT("With flip"), FMath::Abs(SOLNewtonian::StoppingDistanceM(100.0, 20.0, 3.0) - 550.0) < NFM_EPS);
    TestTrue(TEXT("Negative speed uses |v|"), FMath::Abs(SOLNewtonian::StoppingDistanceM(-100.0, 20.0, 3.0) - 550.0)
        < NFM_EPS);
    TestTrue(TEXT("Negative flip time counts as 0"),
        FMath::Abs(SOLNewtonian::StoppingDistanceM(100.0, 20.0, -3.0) - 250.0) < NFM_EPS);
    TestEqual(TEXT("At rest"), SOLNewtonian::StoppingDistanceM(0.0, 20.0, 3.0), 0.0);
    TestEqual(TEXT("At rest without brakes"), SOLNewtonian::StoppingDistanceM(0.0, 0.0, 3.0), 0.0);
    TestEqual(TEXT("No brakes"), SOLNewtonian::StoppingDistanceM(1.0, 0.0, 0.0), maxD);
    TestEqual(TEXT("Negative brakes"), SOLNewtonian::StoppingDistanceM(1.0, -5.0, 0.0), maxD);
    TestEqual(TEXT("NaN speed"), SOLNewtonian::StoppingDistanceM(NfmNaN(), 20.0, 0.0), maxD);
    TestEqual(TEXT("NaN flip time"), SOLNewtonian::StoppingDistanceM(10.0, 20.0, NfmNaN()), maxD);
    TestEqual(TEXT("NaN brakes"), SOLNewtonian::StoppingDistanceM(10.0, NfmNaN(), 0.0), maxD);

    // 0.5c at 45 m/s^2: finite and enormous (about 3,000 AU... 2.5e14 m)
    const double halfC = SOLNewtonian::StoppingDistanceM(NFM_HALF_C, 45.0, 3.0);
    TestTrue(TEXT("0.5c finite"), FMath::IsFinite(halfC) && FMath::Abs(halfC - (NFM_HALF_C * 3.0
        + NFM_HALF_C * NFM_HALF_C / 90.0)) <= halfC * 1.0e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathBrakingLimitTest,
    "SOLTest.NewtonianFlightMath.BrakingSpeedLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BrakingSpeedLimitMps inverts StoppingDistanceM, is cancellation-free for long flips, and is 0 on bad input
bool FSOLNewtonianFlightMathBrakingLimitTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("No flip: sqrt(2 a d)"), FMath::Abs(SOLNewtonian::BrakingSpeedLimitMps(250.0, 20.0, 0.0) - 100.0)
        < 1.0e-9);
    TestTrue(TEXT("With flip"), FMath::Abs(SOLNewtonian::BrakingSpeedLimitMps(550.0, 20.0, 3.0) - 100.0) < 1.0e-9);

    // Inverse property over random values spanning metres to tens of AU
    FRandomStream stream(NFM_SEED + 1);
    for (int32 i = 0; i < 200; ++i)
    {
        const double distance = FMath::Pow(10.0, NfmRange(stream, 0.0, 13.0));
        const double accel = FMath::Pow(10.0, NfmRange(stream, -1.0, 2.5));
        const double flip = stream.FRand() < 0.3 ? 0.0 : FMath::Pow(10.0, NfmRange(stream, -2.0, 2.0));
        const double v = SOLNewtonian::BrakingSpeedLimitMps(distance, accel, flip);
        const double back = SOLNewtonian::StoppingDistanceM(v, accel, flip);
        if (!(FMath::IsFinite(v) && v > 0.0 && FMath::Abs(back - distance) <= distance * 1.0e-9))
        {
            AddError(FString::Printf(TEXT("Inverse fails: d %g a %g tf %g -> v %g -> d %g"), distance, accel, flip, v,
                back));
            break;
        }
    }

    // Monotone in distance
    TestTrue(TEXT("Increasing in distance"), SOLNewtonian::BrakingSpeedLimitMps(1000.0, 10.0, 2.0)
        < SOLNewtonian::BrakingSpeedLimitMps(1001.0, 10.0, 2.0));

    // Long flip, short distance: about d / tf, with no cancellation
    const double tiny = SOLNewtonian::BrakingSpeedLimitMps(1.0, 10.0, 1.0e6);
    TestTrue(TEXT("Cancellation-free"), FMath::Abs(tiny - 1.0e-6) <= 1.0e-6 * 1.0e-6);

    // A huge distance stays finite
    TestTrue(TEXT("Huge distance finite"), FMath::IsFinite(SOLNewtonian::BrakingSpeedLimitMps(1.0e300, 50.0, 0.0)));

    TestEqual(TEXT("Zero distance"), SOLNewtonian::BrakingSpeedLimitMps(0.0, 20.0, 1.0), 0.0);
    TestEqual(TEXT("Negative distance"), SOLNewtonian::BrakingSpeedLimitMps(-5.0, 20.0, 1.0), 0.0);
    TestEqual(TEXT("NaN distance"), SOLNewtonian::BrakingSpeedLimitMps(NfmNaN(), 20.0, 1.0), 0.0);
    TestEqual(TEXT("Zero accel"), SOLNewtonian::BrakingSpeedLimitMps(100.0, 0.0, 1.0), 0.0);
    TestEqual(TEXT("NaN accel"), SOLNewtonian::BrakingSpeedLimitMps(100.0, NfmNaN(), 1.0), 0.0);
    TestEqual(TEXT("NaN flip"), SOLNewtonian::BrakingSpeedLimitMps(100.0, 20.0, NfmNaN()), 0.0);
    TestEqual(TEXT("Infinite flip"), SOLNewtonian::BrakingSpeedLimitMps(100.0, 20.0, NfmInf()), 0.0);
    TestTrue(TEXT("Negative flip counts as 0"), FMath::Abs(SOLNewtonian::BrakingSpeedLimitMps(250.0, 20.0, -4.0)
        - 100.0) < 1.0e-9);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Rendezvous guidance: single evaluations
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGuidanceArrivedTest,
    "SOLTest.NewtonianFlightMath.GuidanceArrived",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Inside the arrival radius and speed the phase is Arrived, the nose is held, and station-keeping stays in budget
bool FSOLNewtonianFlightMathGuidanceArrivedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(20.0, 20.0, 1.0);
    const FVector3d targetAccel(0.0, 0.0, 1.0);
    const FSOLGuidanceOutput atTarget = SOLNewtonian::ComputeRendezvousGuidance(FVector3d::ZeroVector,
        FVector3d::ZeroVector, targetAccel, FVector3d::ZeroVector, params, NFM_DT);
    TestTrue(TEXT("Exactly at the target: Arrived"), atTarget.Phase == ESOLGuidancePhase::Arrived);
    TestTrue(TEXT("Exactly at the target: net = target accel"),
        NfmVecNear(atTarget.DesiredNetAccelMps2, targetAccel, 1.0e-12));
    TestTrue(TEXT("Exactly at the target: hold attitude"), atTarget.DesiredForward.IsZero());
    TestEqual(TEXT("Exactly at the target: distance"), atTarget.DistanceM, 0.0);

    const FSOLGuidanceOutput near = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(3.0, 0.0, 0.0),
        FVector3d(0.0, 0.5, 0.0), targetAccel, FVector3d(0.0, 0.0, -9.81), params, NFM_DT);
    TestTrue(TEXT("Near: Arrived"), near.Phase == ESOLGuidancePhase::Arrived);
    TestTrue(TEXT("Near: hold attitude"), near.DesiredForward.IsZero());
    TestTrue(TEXT("Near: station-keeping in budget"),
        (near.DesiredNetAccelMps2 - targetAccel).Size() <= params.PlanAccelMps2 + 1.0e-9);
    TestTrue(TEXT("Near: thrust = net - gravity"), NfmVecNear(near.DesiredThrustAccelMps2,
        near.DesiredNetAccelMps2 - FVector3d(0.0, 0.0, -9.81), 1.0e-12));
    TestTrue(TEXT("Near: distance reported"), FMath::Abs(near.DistanceM - 3.0) < 1.0e-12);

    const FSOLGuidanceOutput tooFast = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(3.0, 0.0, 0.0),
        FVector3d(0.0, 5.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT);
    TestTrue(TEXT("Inside the radius but too fast: not Arrived"), tooFast.Phase != ESOLGuidancePhase::Arrived);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGuidanceInvalidTest,
    "SOLTest.NewtonianFlightMath.GuidanceInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Any non-finite input gives the default (all zero, Arrived) output; a zero budget gives zero acceleration
bool FSOLNewtonianFlightMathGuidanceInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(20.0, 20.0, 1.0);
    const FVector3d r(1000.0, 0.0, 0.0);

    // Helper check: the output is the default
    auto isDefault = [](const FSOLGuidanceOutput& out)
    {
        return out.DesiredNetAccelMps2.IsZero() && out.DesiredThrustAccelMps2.IsZero() && out.DesiredForward.IsZero()
            && out.Phase == ESOLGuidancePhase::Arrived && out.DistanceM == 0.0 && out.ClosingSpeedMps == 0.0
            && out.SpeedLimitMps == 0.0 && out.EtaS == 0.0;
    };
    TestTrue(TEXT("NaN position"), isDefault(SOLNewtonian::ComputeRendezvousGuidance(NfmNaNVector(),
        FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT)));
    TestTrue(TEXT("NaN velocity"), isDefault(SOLNewtonian::ComputeRendezvousGuidance(r, NfmNaNVector(),
        FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT)));
    TestTrue(TEXT("NaN target accel"), isDefault(SOLNewtonian::ComputeRendezvousGuidance(r, FVector3d::ZeroVector,
        NfmNaNVector(), FVector3d::ZeroVector, params, NFM_DT)));
    TestTrue(TEXT("Infinite gravity"), isDefault(SOLNewtonian::ComputeRendezvousGuidance(r, FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d(0.0, 0.0, -NfmInf()), params, NFM_DT)));
    TestTrue(TEXT("NaN dt"), isDefault(SOLNewtonian::ComputeRendezvousGuidance(r, FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, params, NfmNaN())));
    TestTrue(TEXT("Negative dt"), isDefault(SOLNewtonian::ComputeRendezvousGuidance(r, FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, params, -1.0)));
    FSOLGuidanceParams badParams = params;
    badParams.BrakeAccelMps2 = NfmNaN();
    TestTrue(TEXT("NaN params"), isDefault(SOLNewtonian::ComputeRendezvousGuidance(r, FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, badParams, NFM_DT)));

    FSOLGuidanceParams noBudget = params;
    noBudget.PlanAccelMps2 = 0.0;
    noBudget.BrakeAccelMps2 = 0.0;
    const FSOLGuidanceOutput still = SOLNewtonian::ComputeRendezvousGuidance(r, FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, noBudget, NFM_DT);
    TestTrue(TEXT("Zero budget: zero acceleration"), still.DesiredNetAccelMps2.IsZero());
    TestTrue(TEXT("Zero budget: distance still computed"), FMath::Abs(still.DistanceM - 1000.0) < 1.0e-9);

    // dt 0 is valid (not negative): output finite
    const FSOLGuidanceOutput zeroDt = SOLNewtonian::ComputeRendezvousGuidance(r, FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, params, 0.0);
    TestTrue(TEXT("dt 0 finite"), SOLTestHelpers::IsFiniteVector(zeroDt.DesiredNetAccelMps2)
        && zeroDt.Phase == ESOLGuidancePhase::Accelerate);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGuidancePhasesTest,
    "SOLTest.NewtonianFlightMath.GuidancePhases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Accelerate / Brake / Cruise are classified per the contract, with the matching acceleration and nose direction
bool FSOLNewtonianFlightMathGuidancePhasesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(20.0, 20.0, 0.0);

    // Far away at rest: full acceleration toward the target, nose toward it
    const FSOLGuidanceOutput accel = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(1.0e6, 0.0, 0.0),
        FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT);
    TestTrue(TEXT("At rest far away: Accelerate"), accel.Phase == ESOLGuidancePhase::Accelerate);
    TestTrue(TEXT("Accelerate: full budget toward the target"),
        NfmVecNear(accel.DesiredNetAccelMps2, FVector3d(20.0, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Accelerate: nose toward the target"), NfmVecNear(accel.DesiredForward, FVector3d(1.0, 0.0, 0.0),
        1.0e-9));
    TestTrue(TEXT("Speed limit is the braking curve (dt as flip)"), FMath::Abs(accel.SpeedLimitMps
        - SOLNewtonian::BrakingSpeedLimitMps(1.0e6, 20.0, NFM_DT)) < 1.0e-9);

    // Closing far too fast: Brake, full budget backwards, nose retrograde
    const FSOLGuidanceOutput brake = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(1.0e4, 0.0, 0.0),
        FVector3d(2000.0, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT);
    TestTrue(TEXT("Too fast: Brake"), brake.Phase == ESOLGuidancePhase::Brake);
    TestTrue(TEXT("Brake: full budget away from the target"),
        NfmVecNear(brake.DesiredNetAccelMps2, FVector3d(-20.0, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Brake: nose retrograde"), NfmVecNear(brake.DesiredForward, FVector3d(-1.0, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Closing speed reported"), FMath::Abs(brake.ClosingSpeedMps - 2000.0) < 1.0e-9);

    // At the cruise cap far away: Cruise, coasting, nose still prograde
    FSOLGuidanceParams capped = params;
    capped.MaxCruiseSpeedMps = 100.0;
    const FSOLGuidanceOutput cruise = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(1.0e7, 0.0, 0.0),
        FVector3d(100.0, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, capped, NFM_DT);
    TestTrue(TEXT("At the cap: Cruise"), cruise.Phase == ESOLGuidancePhase::Cruise);
    TestTrue(TEXT("Cruise: coasting"), cruise.DesiredNetAccelMps2.Size() < 1.0e-9);
    TestTrue(TEXT("Cruise far: nose prograde"), NfmVecNear(cruise.DesiredForward, FVector3d(1.0, 0.0, 0.0), 1.0e-9));
    TestTrue(TEXT("Cruise: speed limit is the cap"), FMath::Abs(cruise.SpeedLimitMps - 100.0) < 1.0e-12);

    // Cruising inside twice-the-flip stopping distance (but above the curve's reach): nose turns retrograde early
    FSOLGuidanceParams flip = capped;
    flip.FlipTimeS = 3.0;
    const FSOLGuidanceOutput preFlip = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(700.0, 0.0, 0.0),
        FVector3d(100.0, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, flip, NFM_DT);
    TestTrue(TEXT("Pre-flip: still Cruise"), preFlip.Phase == ESOLGuidancePhase::Cruise);
    TestTrue(TEXT("Pre-flip: nose retrograde"), NfmVecNear(preFlip.DesiredForward, FVector3d(-1.0, 0.0, 0.0), 1.0e-9));

    // Sideways drift is nulled: the desired acceleration has a component against it
    const FSOLGuidanceOutput drift = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(1.0e6, 0.0, 0.0),
        FVector3d(0.0, 50.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT);
    TestTrue(TEXT("Lateral drift opposed"), drift.DesiredNetAccelMps2.Y < 0.0);
    TestTrue(TEXT("Lateral drift: still toward the target"), drift.DesiredNetAccelMps2.X > 0.0);

    // Gravity shifts the thrust and the nose, not the net acceleration
    const FVector3d g(0.0, 0.0, -10.0);
    const FSOLGuidanceOutput withG = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(1.0e6, 0.0, 0.0),
        FVector3d::ZeroVector, FVector3d::ZeroVector, g, params, NFM_DT);
    TestTrue(TEXT("Gravity: same net"), NfmVecNear(withG.DesiredNetAccelMps2, accel.DesiredNetAccelMps2, 1.0e-9));
    TestTrue(TEXT("Gravity: thrust = net - g"), NfmVecNear(withG.DesiredThrustAccelMps2, FVector3d(20.0, 0.0, 10.0),
        1.0e-9));
    TestTrue(TEXT("Gravity: nose along the thrust"), NfmVecNear(withG.DesiredForward,
        FVector3d(20.0, 0.0, 10.0).GetSafeNormal(), 1.0e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGuidanceBudgetTest,
    "SOLTest.NewtonianFlightMath.GuidanceBudgetProperty",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Over random states: the budget holds, thrust = net - gravity, and the nose is a unit vector or zero
bool FSOLNewtonianFlightMathGuidanceBudgetTest::RunTest(const FString& /*parameters*/)
{
    FRandomStream stream(NFM_SEED + 2);
    for (int32 i = 0; i < 500; ++i)
    {
        const double plan = NfmRange(stream, 1.0, 150.0);
        const double brake = NfmRange(stream, 1.0, 150.0);
        FSOLGuidanceParams params = NfmGuidance(plan, brake, NfmRange(stream, 0.0, 5.0));
        if (stream.FRand() < 0.3)
        {
            params.MaxCruiseSpeedMps = NfmRange(stream, 10.0, 1.0e5);
        }
        const FVector3d r = FVector3d(stream.VRand()) * FMath::Pow(10.0, NfmRange(stream, -1.0, 12.0));
        const FVector3d v = FVector3d(stream.VRand()) * FMath::Pow(10.0, NfmRange(stream, -2.0, 7.0));
        const FVector3d targetAccel = FVector3d(stream.VRand()) * NfmRange(stream, 0.0, 5.0);
        const FVector3d g = FVector3d(stream.VRand()) * NfmRange(stream, 0.0, 30.0);
        const double dt = stream.FRand() < 0.5 ? NFM_DT : NfmRange(stream, 0.0, 60.0);
        const FSOLGuidanceOutput out = SOLNewtonian::ComputeRendezvousGuidance(r, v, targetAccel, g, params, dt);

        const double relAccel = (out.DesiredNetAccelMps2 - targetAccel).Size();
        const double budget = out.Phase == ESOLGuidancePhase::Brake ? FMath::Max(plan, brake) : plan;
        const bool bBudget = relAccel <= budget * (1.0 + 1.0e-9) + 1.0e-9;
        const bool bThrust = NfmVecNear(out.DesiredThrustAccelMps2, out.DesiredNetAccelMps2 - g,
            1.0e-9 * (out.DesiredNetAccelMps2.Size() + g.Size()) + 1.0e-12);
        const double forwardLength = out.DesiredForward.Size();
        const bool bForward = forwardLength == 0.0 || FMath::Abs(forwardLength - 1.0) < 1.0e-9;
        const bool bFinite = SOLTestHelpers::IsFiniteVector(out.DesiredNetAccelMps2)
            && FMath::IsFinite(out.SpeedLimitMps) && FMath::IsFinite(out.ClosingSpeedMps);
        const bool bLimit = out.SpeedLimitMps >= 0.0 && out.SpeedLimitMps <= params.MaxCruiseSpeedMps + 1.0e-9;
        if (!(bBudget && bThrust && bForward && bFinite && bLimit))
        {
            AddError(FString::Printf(TEXT("Sample %d: budget %d thrust %d forward %d finite %d limit %d"), i,
                static_cast<int32>(bBudget), static_cast<int32>(bThrust), static_cast<int32>(bForward),
                static_cast<int32>(bFinite), static_cast<int32>(bLimit)));
            break;
        }
    }

    // Huge values stay finite and inside the budget
    const FSOLGuidanceParams params = NfmGuidance(50.0, 50.0, 3.0);
    const FSOLGuidanceOutput huge = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(1.0e13, 0.0, 0.0),
        FVector3d(1.0e8, -1.0e8, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, 60.0);
    TestTrue(TEXT("Huge values finite"), SOLTestHelpers::IsFiniteVector(huge.DesiredNetAccelMps2)
        && huge.DesiredNetAccelMps2.Size() <= 50.0 + 1.0e-9);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Rendezvous guidance: closed-loop flights
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathFlipMidpointTest,
    "SOLTest.NewtonianFlightMath.FlipAndBurnMidpoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Equal budgets: brake at the midpoint, peak sqrt(a d), arrive in about 2 sqrt(d / a) without overshoot
bool FSOLNewtonianFlightMathFlipMidpointTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(10.0, 10.0, 0.0);
    const double distance = 1.0e5;
    const FNfmPointRun run = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
        FVector3d(distance, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT, 600.0);
    const double ideal = 2.0 * FMath::Sqrt(distance / 10.0);
    TestTrue(TEXT("Finite"), run.bFinite);
    TestTrue(TEXT("Budget respected"), run.bBudgetOk);
    TestTrue(TEXT("Arrives"), run.bArrived);
    TestTrue(TEXT("Not faster than the ideal"), run.ArrivalTimeS >= ideal * 0.999);
    TestTrue(TEXT("Within 10% + 5 s of the ideal"), run.ArrivalTimeS <= ideal * 1.1 + 5.0);
    TestTrue(TEXT("No overshoot"), run.MaxOvershootM <= params.ArrivalRadiusM);
    TestTrue(TEXT("Brake starts near the midpoint"), run.BrakeStartTraveledM >= 0.45 * distance
        && run.BrakeStartTraveledM <= 0.55 * distance);
    TestTrue(TEXT("Peak speed about sqrt(a d)"), FMath::Abs(run.PeakClosingMps - FMath::Sqrt(10.0 * distance))
        <= 0.05 * FMath::Sqrt(10.0 * distance));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathBrakingPointTest,
    "SOLTest.NewtonianFlightMath.FlipAndBurnUnequalBrakingPoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Weaker braking: the braking point moves to d * a_b / (a_a + a_b) and the trip time matches the two-phase ideal
bool FSOLNewtonianFlightMathBrakingPointTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(40.0, 10.0, 0.0);
    const double distance = 1.0e5;
    const FNfmPointRun run = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
        FVector3d(distance, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT, 600.0);
    const double brakingPoint = distance * 10.0 / (40.0 + 10.0);
    const double ideal = FMath::Sqrt(2.0 * brakingPoint / 40.0) + FMath::Sqrt(2.0 * (distance - brakingPoint) / 10.0);
    TestTrue(TEXT("Budget respected"), run.bBudgetOk);
    TestTrue(TEXT("Arrives"), run.bArrived);
    TestTrue(TEXT("Braking point within 5%"), FMath::Abs(run.BrakeStartTraveledM - brakingPoint)
        <= 0.05 * brakingPoint);
    TestTrue(TEXT("Not faster than the ideal"), run.ArrivalTimeS >= ideal * 0.999);
    TestTrue(TEXT("Within 15% + 5 s of the ideal"), run.ArrivalTimeS <= ideal * 1.15 + 5.0);
    TestTrue(TEXT("No overshoot"), run.MaxOvershootM <= params.ArrivalRadiusM);

    // Stronger braking than acceleration: the braking point moves past the midpoint
    const FSOLGuidanceParams strongBrake = NfmGuidance(10.0, 40.0, 0.0);
    const FNfmPointRun late = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
        FVector3d(distance, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, strongBrake, NFM_DT, 600.0);
    const double latePoint = distance * 40.0 / (10.0 + 40.0);
    TestTrue(TEXT("Strong brake: arrives"), late.bArrived && late.bBudgetOk);
    TestTrue(TEXT("Strong brake: braking point within 5%"), FMath::Abs(late.BrakeStartTraveledM - latePoint)
        <= 0.05 * latePoint);
    TestTrue(TEXT("Strong brake: no overshoot"), late.MaxOvershootM <= strongBrake.ArrivalRadiusM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathBrakingDistanceTest,
    "SOLTest.NewtonianFlightMath.BrakingDistanceCorrectness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Starting exactly at the stopping distance brakes at once and stops on target; starting too close overshoots by
// the unavoidable amount and then comes back
bool FSOLNewtonianFlightMathBrakingDistanceTest::RunTest(const FString& /*parameters*/)
{
    const double speed = 1000.0;
    const FSOLGuidanceParams params = NfmGuidance(20.0, 20.0, 2.0);
    const double exactDistance = SOLNewtonian::StoppingDistanceM(speed, 20.0, 2.0 + NFM_DT);

    const FSOLGuidanceOutput first = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(exactDistance, 0.0, 0.0),
        FVector3d(speed, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT);
    TestTrue(TEXT("On the curve: Brake at once"), first.Phase == ESOLGuidancePhase::Brake);

    const FNfmPointRun onCurve = NfmRunPointMass(FVector3d::ZeroVector, FVector3d(speed, 0.0, 0.0),
        FVector3d(exactDistance, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT, 300.0);
    TestTrue(TEXT("On the curve: arrives"), onCurve.bArrived && onCurve.bBudgetOk);
    TestTrue(TEXT("On the curve: no overshoot"), onCurve.MaxOvershootM <= params.ArrivalRadiusM);

    // Half the no-flip stopping distance away: the overshoot is about the missing half
    const FSOLGuidanceParams noFlip = NfmGuidance(20.0, 20.0, 0.0);
    const double stopNoFlip = SOLNewtonian::StoppingDistanceM(speed, 20.0, 0.0);
    const double shortDistance = 0.5 * stopNoFlip;
    const FNfmPointRun tooClose = NfmRunPointMass(FVector3d::ZeroVector, FVector3d(speed, 0.0, 0.0),
        FVector3d(shortDistance, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, noFlip, NFM_DT, 600.0);
    const double unavoidable = stopNoFlip - shortDistance;
    TestTrue(TEXT("Too close: overshoots by the unavoidable amount"),
        FMath::Abs(tooClose.MaxOvershootM - unavoidable) <= 0.05 * unavoidable);
    TestTrue(TEXT("Too close: comes back and arrives"), tooClose.bArrived && tooClose.bBudgetOk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathMovingTargetTest,
    "SOLTest.NewtonianFlightMath.MovingTargetIntercept",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A target moving sideways at constant velocity is met with matched velocity inside the budget
bool FSOLNewtonianFlightMathMovingTargetTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(20.0, 20.0, 1.0);
    const FVector3d targetVelocity(0.0, 500.0, 0.0);
    const FNfmPointRun run = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
        FVector3d(5.0e4, 0.0, 0.0), targetVelocity, FVector3d::ZeroVector, params, NFM_DT, 600.0);
    TestTrue(TEXT("Finite"), run.bFinite);
    TestTrue(TEXT("Budget respected"), run.bBudgetOk);
    TestTrue(TEXT("Arrives"), run.bArrived);
    TestTrue(TEXT("Within 400 s"), run.ArrivalTimeS <= 400.0);
    TestTrue(TEXT("Velocity matched"), (run.FinalShipVelocityMps - run.FinalTargetVelocityMps).Size()
        <= params.ArrivalSpeedMps + 1.0e-9);

    // Target approaching head-on fast: still rendezvous (the ship must reverse to match)
    const FNfmPointRun headOn = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
        FVector3d(2.0e5, 0.0, 0.0), FVector3d(-300.0, 0.0, 0.0), FVector3d::ZeroVector, params, NFM_DT, 900.0);
    TestTrue(TEXT("Head-on: arrives"), headOn.bArrived && headOn.bBudgetOk);
    TestTrue(TEXT("Head-on: matched"), (headOn.FinalShipVelocityMps - headOn.FinalTargetVelocityMps).Size()
        <= params.ArrivalSpeedMps + 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathAcceleratingTargetTest,
    "SOLTest.NewtonianFlightMath.AcceleratingTargetIntercept",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A target with its own constant acceleration (feedforward) is met with matched velocity
bool FSOLNewtonianFlightMathAcceleratingTargetTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(20.0, 20.0, 1.0);
    const FVector3d targetAccel(0.0, 1.0, -0.5);
    const FNfmPointRun run = NfmRunPointMass(FVector3d::ZeroVector, FVector3d(0.0, 0.0, 50.0),
        FVector3d(5.0e4, 0.0, 0.0), FVector3d(0.0, 200.0, 0.0), targetAccel, params, NFM_DT, 600.0);
    TestTrue(TEXT("Finite"), run.bFinite);
    TestTrue(TEXT("Budget respected (relative to the target's acceleration)"), run.bBudgetOk);
    TestTrue(TEXT("Arrives"), run.bArrived);
    TestTrue(TEXT("Within 400 s"), run.ArrivalTimeS <= 400.0);
    TestTrue(TEXT("Velocity matched"), (run.FinalShipVelocityMps - run.FinalTargetVelocityMps).Size()
        <= params.ArrivalSpeedMps + 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathCruiseCapTest, "SOLTest.NewtonianFlightMath.CruiseCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The closing speed never exceeds the cruise cap; the leg shows Accelerate, Cruise and arrives without overshoot
bool FSOLNewtonianFlightMathCruiseCapTest::RunTest(const FString& /*parameters*/)
{
    FSOLGuidanceParams params = NfmGuidance(20.0, 20.0, 1.0);
    params.MaxCruiseSpeedMps = 300.0;
    const double distance = 2.0e5;
    const FNfmPointRun run = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
        FVector3d(distance, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT, 1500.0);
    const double ideal = distance / 300.0 + 300.0 / 20.0;
    TestTrue(TEXT("Arrives"), run.bArrived && run.bBudgetOk);
    TestTrue(TEXT("Never above the cap"), run.PeakClosingMps <= 300.0 * (1.0 + 1.0e-6) + 1.0e-6);
    TestTrue(TEXT("Reaches the cap"), run.PeakClosingMps >= 300.0 * 0.99);
    TestTrue(TEXT("Saw Accelerate and Cruise"), run.bSawAccelerate && run.bSawCruise);
    TestTrue(TEXT("Trip time near the ideal"), run.ArrivalTimeS >= ideal * 0.999 && run.ArrivalTimeS
        <= ideal * 1.1 + 10.0);
    TestTrue(TEXT("No overshoot"), run.MaxOvershootM <= params.ArrivalRadiusM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathLongRangeTest,
    "SOLTest.NewtonianFlightMath.LongRangeDtInvariance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 1 AU leg at the 1.5 G pilot plan (13.24 m/s^2) arrives in about the brachistochrone time whether stepped at 1,
// 10, 60 or 300 s (the sim-time substeps of user-chosen warp)
bool FSOLNewtonianFlightMathLongRangeTest::RunTest(const FString& /*parameters*/)
{
    const double plan = 0.9 * 1.5 * NFM_G0;
    FSOLGuidanceParams params = NfmGuidance(plan, plan, 3.0);
    params.ArrivalRadiusM = 1000.0;
    params.ArrivalSpeedMps = 10.0;
    const double distance = SOLTestHelpers::AU_M;
    const double ideal = 2.0 * FMath::Sqrt(distance / plan);
    const double dts[] = { 1.0, 10.0, 60.0, 300.0 };
    double times[UE_ARRAY_COUNT(dts)] = {};
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(dts)); ++i)
    {
        const FNfmPointRun run = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
            FVector3d(0.0, distance, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, dts[i], 2.0 * ideal);
        times[i] = run.ArrivalTimeS;
        TestTrue(FString::Printf(TEXT("dt %.0f: finite, in budget"), dts[i]), run.bFinite && run.bBudgetOk);
        TestTrue(FString::Printf(TEXT("dt %.0f: arrives"), dts[i]), run.bArrived);
        TestTrue(FString::Printf(TEXT("dt %.0f: not faster than the ideal"), dts[i]),
            run.ArrivalTimeS >= ideal * 0.999);
        TestTrue(FString::Printf(TEXT("dt %.0f: within 5%% + 2000 s"), dts[i]), run.ArrivalTimeS <= ideal * 1.05
            + 2000.0);
        TestTrue(FString::Printf(TEXT("dt %.0f: far below 0.5c"), dts[i]),
            run.PeakClosingMps < 0.01 * NFM_HALF_C);
        TestTrue(FString::Printf(TEXT("dt %.0f: overshoot bounded"), dts[i]),
            run.MaxOvershootM <= 1.0e-6 * distance);
    }
    for (int32 i = 1; i < static_cast<int32>(UE_ARRAY_COUNT(dts)); ++i)
    {
        TestTrue(FString::Printf(TEXT("dt %.0f matches dt 1 within 2%%"), dts[i]),
            FMath::Abs(times[i] - times[0]) <= 0.02 * times[0]);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathFeedforwardFadeTest,
    "SOLTest.NewtonianFlightMath.GuidanceFeedforwardFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Brake feedforward fades with k = min(1, (1/gain) / tau, clamp(2 (1 - dt * gain), 0, 1)): a 3 km stop arrives
// without ringing at every step length from 1/60 s to 1 s, and on the braking curve the net acceleration is exactly the
// faded feedforward (at 1/60 s only 1/30 below the old k = 1)
bool FSOLNewtonianFlightMathFeedforwardFadeTest::RunTest(const FString& /*parameters*/)
{
    constexpr double accel = 35.3;
    constexpr double cap = 250.0;
    FSOLGuidanceParams params = NfmGuidance(accel, accel, 0.0);
    params.MaxCruiseSpeedMps = cap;
    params.ArrivalRadiusM = 1.0;
    params.ArrivalSpeedMps = 0.5;
    params.VelocityGainPerS = 2.0;

    // Closed loop: arrives within 1.5 x the no-cap flip-and-burn time + 2 s, never past the arrival radius
    const double distance = 3000.0;
    const double bound = 1.5 * 2.0 * FMath::Sqrt(distance / accel) + 2.0;
    const double dts[] = { 1.0 / 60.0, 0.1, 0.25, 0.5, 1.0 };
    for (const double dt : dts)
    {
        const FNfmPointRun run = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
            FVector3d(distance, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, dt, bound);
        TestTrue(FString::Printf(TEXT("dt %g: finite, in budget"), dt), run.bFinite && run.bBudgetOk);
        TestTrue(FString::Printf(TEXT("dt %g: arrives (at %.3f s, bound %.3f s)"), dt, run.ArrivalTimeS, bound),
            run.bArrived && run.ArrivalTimeS <= bound + 1.0e-9);
        TestTrue(FString::Printf(TEXT("dt %g: no overshoot past the arrival radius"), dt),
            run.MaxOvershootM <= params.ArrivalRadiusM);
        if (dt < 0.02)
        {
            // At the player's frame rates the fade changes nothing visible: still the capped ideal (accelerate to
            // the cap, cruise, brake) within 2% + 0.2 s
            const double ideal = cap / accel + distance / cap;
            TestTrue(FString::Printf(TEXT("dt 1/60: %.3f s near the capped ideal %.3f s"), run.ArrivalTimeS, ideal),
                run.ArrivalTimeS <= ideal * 1.02 + 0.2);
        }
    }

    // Single evaluations exactly on the braking curve (so the velocity correction is zero): net = -k vc / sqrt(tf^2 +
    // 2 d / a), with tf = FlipTimeS + dt
    struct FCase { double Dt; double Gain; double K; };
    const FCase cases[] = {
        { 1.0 / 60.0, 2.0, 1.0 },                // real-time steps keep the full feedforward (as the old rule)
        { 0.1, 2.0, 1.0 },
        { 0.25, 2.0, 1.0 },                     // dt = 0.5 / gain: the fade starts here
        { 0.375, 2.0, 0.5 },
        { 0.5, 2.0, 0.0 },                      // dt = 1 / gain: fully faded (the old rule's period-2 ringing case)
        { 0.75, 2.0, 0.0 },
        { 0.75, 0.5, 1.0 },                     // gain 0.5: the fade starts at dt = 1 s
        { 1.0 / 60.0, 0.0, 1.0 },               // no gain: k = 1
    };
    const double d = 500.0;
    for (const FCase& c : cases)
    {
        FSOLGuidanceParams p = params;
        p.VelocityGainPerS = c.Gain;
        const double tf = 0.0 + c.Dt;
        const double curve = SOLNewtonian::BrakingSpeedLimitMps(d, accel, tf);
        const FSOLGuidanceOutput out = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(d, 0.0, 0.0),
            FVector3d(curve, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, p, c.Dt);
        const double feedforward = curve / FMath::Sqrt(tf * tf + 2.0 * d / accel);
        const FVector3d expected(-c.K * feedforward, 0.0, 0.0);
        TestTrue(FString::Printf(TEXT("dt %g gain %g: Brake"), c.Dt, c.Gain), out.Phase == ESOLGuidancePhase::Brake);
        TestTrue(FString::Printf(TEXT("dt %g gain %g: net %.12f, expected %.12f (k %g)"), c.Dt, c.Gain,
            out.DesiredNetAccelMps2.X, expected.X, c.K), NfmVecNear(out.DesiredNetAccelMps2, expected, 1.0e-9 * accel));
        if (c.Dt < 0.02 && c.Gain > 0.0)
        {
            TestTrue(TEXT("dt 1/60: the full feedforward of the old k = 1"),
                FMath::Abs(out.DesiredNetAccelMps2.X - (-feedforward)) <= 1.0e-9 * accel);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGuidanceNoEtaTest,
    "SOLTest.NewtonianFlightMath.GuidanceNoEtaVariant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The bComputeEta overload matches the plain call in every field; false only zeroes EtaS
bool FSOLNewtonianFlightMathGuidanceNoEtaTest::RunTest(const FString& /*parameters*/)
{
    // Helper check: every field except EtaS is bit-identical
    auto sameButEta = [](const FSOLGuidanceOutput& a, const FSOLGuidanceOutput& b)
    {
        return a.DesiredNetAccelMps2 == b.DesiredNetAccelMps2 && a.DesiredThrustAccelMps2 == b.DesiredThrustAccelMps2
            && a.DesiredForward == b.DesiredForward && a.Phase == b.Phase && a.DistanceM == b.DistanceM
            && a.ClosingSpeedMps == b.ClosingSpeedMps && a.SpeedLimitMps == b.SpeedLimitMps;
    };

    FRandomStream stream(NFM_SEED + 7);
    for (int32 i = 0; i < 500; ++i)
    {
        FSOLGuidanceParams params = NfmGuidance(NfmRange(stream, 1.0, 150.0), NfmRange(stream, 1.0, 150.0),
            NfmRange(stream, 0.0, 5.0));
        if (stream.FRand() < 0.3)
        {
            params.MaxCruiseSpeedMps = NfmRange(stream, 10.0, 1.0e5);
        }
        const FVector3d r = FVector3d(stream.VRand()) * FMath::Pow(10.0, NfmRange(stream, -1.0, 12.0));
        const FVector3d v = FVector3d(stream.VRand()) * FMath::Pow(10.0, NfmRange(stream, -2.0, 7.0));
        const FVector3d targetAccel = FVector3d(stream.VRand()) * NfmRange(stream, 0.0, 5.0);
        const FVector3d g = FVector3d(stream.VRand()) * NfmRange(stream, 0.0, 30.0);
        const double dt = stream.FRand() < 0.5 ? NFM_DT : NfmRange(stream, 0.0, 60.0);

        const FSOLGuidanceOutput plain = SOLNewtonian::ComputeRendezvousGuidance(r, v, targetAccel, g, params, dt);
        const FSOLGuidanceOutput withEta = SOLNewtonian::ComputeRendezvousGuidance(r, v, targetAccel, g, params, dt,
            true);
        const FSOLGuidanceOutput noEta = SOLNewtonian::ComputeRendezvousGuidance(r, v, targetAccel, g, params, dt,
            false);
        const bool bWithEta = sameButEta(withEta, plain) && withEta.EtaS == plain.EtaS;
        const bool bNoEta = sameButEta(noEta, plain) && noEta.EtaS == 0.0;
        if (!(bWithEta && bNoEta))
        {
            AddError(FString::Printf(TEXT("Sample %d: with ETA matches %d, without ETA matches %d"), i,
                static_cast<int32>(bWithEta), static_cast<int32>(bNoEta)));
            break;
        }
    }

    // Invalid input: both give the default output
    const FSOLGuidanceOutput invalid = SOLNewtonian::ComputeRendezvousGuidance(NfmNaNVector(), FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, NfmGuidance(10.0, 10.0, 0.0), NFM_DT, false);
    TestTrue(TEXT("Invalid: default output"), invalid.Phase == ESOLGuidancePhase::Arrived && invalid.EtaS == 0.0
        && invalid.DesiredNetAccelMps2.IsZero() && invalid.DistanceM == 0.0);

    // A real leg: the plain call reports an ETA, the no-ETA call reports 0
    const FSOLGuidanceOutput leg = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(1.0e5, 0.0, 0.0),
        FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector, NfmGuidance(10.0, 10.0, 0.0), NFM_DT,
        false);
    TestTrue(TEXT("Leg without ETA: accelerating, EtaS 0"), leg.Phase == ESOLGuidancePhase::Accelerate
        && leg.EtaS == 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathFullShipTest,
    "SOLTest.NewtonianFlightMath.FullShipFlipAndBurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The whole autopilot chain (guidance at the pilot's sustained limit, pilot clamp, attitude, rate command,
// allocation, step) flies the player ship 100 km: it flips, stops on target, never pulls more than 1.5 G, also under
// uniform gravity
bool FSOLNewtonianFlightMathFullShipTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmPlayerEnvelope();
    const FSOLPilotGLimits limits = NfmLimits();
    const FVector3d target(1.0e5, 0.0, 0.0);

    struct FCase { FVector3d Gravity; const TCHAR* Name; };
    const FCase cases[] = {
        { FVector3d::ZeroVector, TEXT("no gravity") },
        { FVector3d(0.0, 0.0, -5.0), TEXT("uniform gravity") },
    };
    for (const FCase& c : cases)
    {
        const FSOLGMeterState fullMeter = NfmMeter(1.0);
        const double pilotLimit = SOLNewtonian::AutopilotAccelLimitMps2(limits, fullMeter);
        FSOLGuidanceParams params = SOLNewtonian::MakeGuidanceParams(env, pilotLimit, c.Gravity.Size(), true, false);
        params.ArrivalRadiusM = 50.0;
        params.ArrivalSpeedMps = 1.0;
        const double ideal = 2.0 * FMath::Sqrt(target.Size() / params.PlanAccelMps2) + params.FlipTimeS;
        const FNfmShipRun run = NfmRunFullShip(env, params, target, c.Gravity, NFM_DT, 3.0 * ideal, pilotLimit);
        TestTrue(FString::Printf(TEXT("%s: never above the sustained 1.5 G"), c.Name),
            run.MaxGLoad <= 1.5 * (1.0 + 1.0e-9));
        TestTrue(FString::Printf(TEXT("%s: finite"), c.Name), run.bFinite);
        TestTrue(FString::Printf(TEXT("%s: arrives"), c.Name), run.bArrived);
        TestTrue(FString::Printf(TEXT("%s: within 1.3x ideal + 10 s"), c.Name), run.ArrivalTimeS <= 1.3 * ideal
            + 10.0);
        // Braking thrust also cancels gravity, so under gravity the nose can only reach -plan / |(plan, g)| along the
        // line of sight (-0.855 for 8.24 m/s^2 against 5 m/s^2); 90% of that still proves the flip
        const double gravitySize = c.Gravity.Size();
        const double flipDot = gravitySize > 0.0 ? -0.9 * params.PlanAccelMps2
            / FMath::Sqrt(params.PlanAccelMps2 * params.PlanAccelMps2 + gravitySize * gravitySize) : -0.9;
        TestTrue(FString::Printf(TEXT("%s: flipped to brake on the main engine"), c.Name), run.MinForwardDot < flipDot);
        TestTrue(FString::Printf(TEXT("%s: overshoot under 100 m"), c.Name), run.MaxOvershootM <= 100.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathMakeGuidanceParamsTest,
    "SOLTest.NewtonianFlightMath.MakeGuidanceParams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Budgets are 90% of min(thrust * boost, pilot limit, total cap) minus gravity (floored at 10%); braking flips or
// uses reverse; flip time per contract
bool FSOLNewtonianFlightMathMakeGuidanceParamsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();   // 50 / 25 / 30 / 30, no total cap
    const double noLimit = NfmInf();
    const double rate = env.MaxAngularRateRadPerS.Y;
    const double alpha = 0.8 * env.MaxAngularAccelRadPerS2.Y;
    const double expectedFlip = (UE_DOUBLE_PI >= rate * rate / alpha
        ? UE_DOUBLE_PI / rate + rate / alpha : 2.0 * FMath::Sqrt(UE_DOUBLE_PI / alpha)) + 1.0 / 4.0;

    // No pilot limit (an AI ship): thrust capability only
    const FSOLGuidanceParams flip = SOLNewtonian::MakeGuidanceParams(env, noLimit, 0.0, true, false);
    TestTrue(TEXT("Flip: plan 45"), FMath::Abs(flip.PlanAccelMps2 - 45.0) < 1.0e-12);
    TestTrue(TEXT("Flip: brake = plan"), FMath::Abs(flip.BrakeAccelMps2 - 45.0) < 1.0e-12);
    TestTrue(TEXT("Flip: flip time"), FMath::Abs(flip.FlipTimeS - expectedFlip) < 1.0e-9);

    const FSOLGuidanceParams noFlip = SOLNewtonian::MakeGuidanceParams(env, noLimit, 0.0, false, false);
    TestTrue(TEXT("No flip: brake on reverse (22.5)"), FMath::Abs(noFlip.BrakeAccelMps2 - 22.5) < 1.0e-12);
    TestEqual(TEXT("No flip: no flip time"), noFlip.FlipTimeS, 0.0);

    const FSOLGuidanceParams gravity = SOLNewtonian::MakeGuidanceParams(env, noLimit, 10.0, true, false);
    TestTrue(TEXT("Gravity eats budget: 35"), FMath::Abs(gravity.PlanAccelMps2 - 35.0) < 1.0e-12);
    const FSOLGuidanceParams gravityRev = SOLNewtonian::MakeGuidanceParams(env, noLimit, 10.0, false, false);
    TestTrue(TEXT("Gravity on reverse: 12.5"), FMath::Abs(gravityRev.BrakeAccelMps2 - 12.5) < 1.0e-12);

    const FSOLGuidanceParams crushing = SOLNewtonian::MakeGuidanceParams(env, noLimit, 100.0, false, false);
    TestTrue(TEXT("Floor: 10% of forward"), FMath::Abs(crushing.PlanAccelMps2 - 5.0) < 1.0e-12);
    TestTrue(TEXT("Floor: 10% of reverse"), FMath::Abs(crushing.BrakeAccelMps2 - 2.5) < 1.0e-12);

    const FSOLGuidanceParams boost = SOLNewtonian::MakeGuidanceParams(env, noLimit, 0.0, false, true);
    TestTrue(TEXT("Boost: plan 135"), FMath::Abs(boost.PlanAccelMps2 - 135.0) < 1.0e-12);
    TestTrue(TEXT("Boost: reverse 67.5"), FMath::Abs(boost.BrakeAccelMps2 - 67.5) < 1.0e-12);

    const FSOLGuidanceParams badGravity = SOLNewtonian::MakeGuidanceParams(env, noLimit, NfmNaN(), true, false);
    TestTrue(TEXT("NaN gravity counts as 0"), FMath::Abs(badGravity.PlanAccelMps2 - 45.0) < 1.0e-12);
    const FSOLGuidanceParams negGravity = SOLNewtonian::MakeGuidanceParams(env, noLimit, -20.0, true, false);
    TestTrue(TEXT("Negative gravity counts as 0"), FMath::Abs(negGravity.PlanAccelMps2 - 45.0) < 1.0e-12);

    // Pilot limit: the player at the 1.5 G sustained limit plans 0.9 * 1.5 G whatever the ship or boost can do
    const FSOLThrustEnvelope player = NfmPlayerEnvelope();
    const double sustained = 1.5 * NFM_G0;
    const FSOLGuidanceParams pilot = SOLNewtonian::MakeGuidanceParams(player, sustained, 0.0, true, false);
    TestTrue(TEXT("Pilot: plan 0.9 * 1.5 G"), FMath::Abs(pilot.PlanAccelMps2 - 0.9 * sustained) < 1.0e-12);
    const FSOLGuidanceParams pilotBoost = SOLNewtonian::MakeGuidanceParams(player, sustained, 0.0, true, true);
    TestTrue(TEXT("Pilot: boost does not raise the plan"),
        FMath::Abs(pilotBoost.PlanAccelMps2 - 0.9 * sustained) < 1.0e-12);
    const FSOLGuidanceParams pilotRev = SOLNewtonian::MakeGuidanceParams(player, sustained, 0.0, false, false);
    TestTrue(TEXT("Pilot: reverse 1.5 G also limited"), FMath::Abs(pilotRev.BrakeAccelMps2 - 0.9 * sustained)
        < 1.0e-12);
    const FSOLGuidanceParams pilotG = SOLNewtonian::MakeGuidanceParams(player, sustained, 10.0, true, false);
    TestTrue(TEXT("Pilot: gravity spends the pilot budget"),
        FMath::Abs(pilotG.PlanAccelMps2 - (0.9 * sustained - 10.0)) < 1.0e-12);
    const FSOLGuidanceParams pilotFloor = SOLNewtonian::MakeGuidanceParams(player, sustained, 100.0, true, false);
    TestTrue(TEXT("Pilot: floor is 10% of the limited forward"),
        FMath::Abs(pilotFloor.PlanAccelMps2 - 0.1 * sustained) < 1.0e-12);

    // Total cap: an AI ship with no pilot limit is held by the envelope's total cap (9 G), even when boosted
    const FSOLGuidanceParams capped = SOLNewtonian::MakeGuidanceParams(player, noLimit, 0.0, true, true);
    TestTrue(TEXT("Total cap limits a boosted plan"), FMath::Abs(capped.PlanAccelMps2 - 0.9 * 9.0 * NFM_G0)
        < 1.0e-9);
    const FSOLGuidanceParams nanLimit = SOLNewtonian::MakeGuidanceParams(player, NfmNaN(), 0.0, true, false);
    TestTrue(TEXT("NaN pilot limit counts as none"), FMath::Abs(nanLimit.PlanAccelMps2 - 0.9 * 3.0 * NFM_G0)
        < 1.0e-9);
    const FSOLGuidanceParams zeroLimit = SOLNewtonian::MakeGuidanceParams(player, 0.0, 0.0, true, false);
    TestTrue(TEXT("Zero pilot limit counts as none"), FMath::Abs(zeroLimit.PlanAccelMps2 - 0.9 * 3.0 * NFM_G0)
        < 1.0e-9);

    // Every other field keeps the struct default
    const FSOLGuidanceParams defaults;
    TestEqual(TEXT("Default cruise cap"), flip.MaxCruiseSpeedMps, defaults.MaxCruiseSpeedMps);
    TestEqual(TEXT("Default arrival radius"), flip.ArrivalRadiusM, defaults.ArrivalRadiusM);
    TestEqual(TEXT("Default arrival speed"), flip.ArrivalSpeedMps, defaults.ArrivalSpeedMps);
    TestEqual(TEXT("Default gain"), flip.VelocityGainPerS, defaults.VelocityGainPerS);
    TestEqual(TEXT("Contract default arrival radius"), defaults.ArrivalRadiusM, 10.0);
    TestEqual(TEXT("Contract default arrival speed"), defaults.ArrivalSpeedMps, 1.0);
    TestEqual(TEXT("Contract default gain"), defaults.VelocityGainPerS, 2.0);
    TestEqual(TEXT("Contract default cruise cap"), defaults.MaxCruiseSpeedMps, SOL::MAX_SPEED_CAP_MPS);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Conservation and frame properties
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathOrbitEnergyTest,
    "SOLTest.NewtonianFlightMath.OrbitEnergyConservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A coasting circular orbit over 10 periods keeps its energy (symplectic integration, no drag) and its radius
bool FSOLNewtonianFlightMathOrbitEnergyTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    constexpr double gm = 4.0e6;
    constexpr double radius = 1000.0;
    FSOLShipState state;
    state.PositionM = FVector3d(radius, 0.0, 0.0);
    state.VelocityMps = FVector3d(0.0, FMath::Sqrt(gm / radius), 0.0);
    const double period = UE_DOUBLE_TWO_PI * FMath::Sqrt(radius * radius * radius / gm);
    const int32 steps = static_cast<int32>(10.0 * period / NFM_DT);

    // Specific orbital energy, computed independently of the code under test
    auto energy = [gm](const FSOLShipState& s)
    {
        return 0.5 * s.VelocityMps.SizeSquared() - gm / s.PositionM.Size();
    };
    const double e0 = energy(state);
    double worstEnergy = 0.0;
    double minR = radius;
    double maxR = radius;
    for (int32 i = 0; i < steps; ++i)
    {
        const double r = state.PositionM.Size();
        const FVector3d g = -state.PositionM * (gm / (r * r * r));
        state = SOLNewtonian::StepNewtonian(state, FSOLNewtonianCommand(), env, g, NFM_DT);
        worstEnergy = FMath::Max(worstEnergy, FMath::Abs(energy(state) - e0) / FMath::Abs(e0));
        minR = FMath::Min(minR, state.PositionM.Size());
        maxR = FMath::Max(maxR, state.PositionM.Size());
    }
    TestTrue(TEXT("Energy bounded over 10 orbits (no secular drift)"), worstEnergy < 5.0e-3);
    TestTrue(TEXT("Radius within 1%"), minR >= 0.99 * radius && maxR <= 1.01 * radius);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGalileanTest, "SOLTest.NewtonianFlightMath.GalileanInvariance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Two ships that differ only by a constant velocity, flown with the same commands, stay apart by exactly u * t
// (the property that lets NPCs fly in their rail's translating frame)
bool FSOLNewtonianFlightMathGalileanTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmEnvelope();
    const FVector3d u(1234.5, -678.0, 90.25);
    const FVector3d gravity(0.0, 0.0, -3.0);
    FSOLShipState a;
    a.PositionM = FVector3d(100.0, 200.0, 300.0);
    a.VelocityMps = FVector3d(10.0, 0.0, 0.0);
    FSOLShipState b = a;
    b.VelocityMps += u;
    FRandomStream stream(NFM_SEED + 3);
    constexpr int32 steps = 300;
    for (int32 i = 0; i < steps; ++i)
    {
        const FSOLNewtonianCommand command = NfmCommand(FVector3d(NfmRange(stream, -1.0, 1.0),
            NfmRange(stream, -1.0, 1.0), NfmRange(stream, -1.0, 1.0)), FVector3d(NfmRange(stream, -1.0, 1.0),
            NfmRange(stream, -1.0, 1.0), NfmRange(stream, -1.0, 1.0)), stream.FRand() < 0.2);
        a = SOLNewtonian::StepNewtonian(a, command, env, gravity, NFM_DT);
        b = SOLNewtonian::StepNewtonian(b, command, env, gravity, NFM_DT);
    }
    const double t = steps * NFM_DT;
    TestTrue(TEXT("Velocities differ by u"), NfmVecNear(b.VelocityMps - a.VelocityMps, u, 1.0e-9 * u.Size()));
    TestTrue(TEXT("Positions differ by u t"), NfmVecNear(b.PositionM - a.PositionM, u * t, 1.0e-9 * u.Size() * t));
    TestTrue(TEXT("Same attitude"), NfmAngle(NfmForward(a.Orientation), NfmForward(b.Orientation)) < 1.0e-12
        && NfmAngle(NfmUp(a.Orientation), NfmUp(b.Orientation)) < 1.0e-12);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// NPC rail hand-off
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathHandoffOrientationTest,
    "SOLTest.NewtonianFlightMath.HandoffOrientation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// OrientationFromForward puts the nose on forward, up on the hint's perpendicular part, with the fallback chain
bool FSOLNewtonianFlightMathHandoffOrientationTest::RunTest(const FString& /*parameters*/)
{
    // Checks one result: orthonormal frame with the expected nose and up
    auto checkFrame = [this](const TCHAR* name, const FQuat4d& q, const FVector3d& expectedForward,
        const FVector3d& expectedUp)
    {
        TestTrue(FString::Printf(TEXT("%s: unit"), name), FMath::Abs(q.Size() - 1.0) < 1.0e-9);
        TestTrue(FString::Printf(TEXT("%s: nose"), name), NfmVecNear(NfmForward(q), expectedForward, 1.0e-9));
        TestTrue(FString::Printf(TEXT("%s: up"), name), NfmVecNear(NfmUp(q), expectedUp, 1.0e-9));
        TestTrue(FString::Printf(TEXT("%s: right = up x forward"), name), NfmVecNear(NfmRight(q),
            FVector3d::CrossProduct(expectedUp, expectedForward), 1.0e-9));
    };

    const FVector3d diagonal = FVector3d(1.0, 1.0, 0.0).GetSafeNormal();
    checkFrame(TEXT("Level diagonal"), SOLNewtonian::OrientationFromForward(FVector3d(3.0, 3.0, 0.0),
        FVector3d(0.0, 0.0, 1.0), FQuat4d::Identity), diagonal, FVector3d(0.0, 0.0, 1.0));

    // A tilted hint keeps only its part perpendicular to the nose
    checkFrame(TEXT("Tilted hint"), SOLNewtonian::OrientationFromForward(FVector3d(0.0, 0.0, 2.0),
        FVector3d(1.0, 1.0, 1.0), FQuat4d::Identity), FVector3d(0.0, 0.0, 1.0), diagonal);

    // Degenerate hints fall back to world +Z, then world +X
    checkFrame(TEXT("Zero hint uses +Z"), SOLNewtonian::OrientationFromForward(FVector3d(0.0, 5.0, 0.0),
        FVector3d::ZeroVector, FQuat4d::Identity), FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 0.0, 1.0));
    checkFrame(TEXT("NaN hint uses +Z"), SOLNewtonian::OrientationFromForward(FVector3d(0.0, 5.0, 0.0),
        NfmNaNVector(), FQuat4d::Identity), FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 0.0, 1.0));
    checkFrame(TEXT("Hint along the nose and nose along +Z uses +X"), SOLNewtonian::OrientationFromForward(
        FVector3d(0.0, 0.0, 5.0), FVector3d(0.0, 0.0, 1.0), FQuat4d::Identity), FVector3d(0.0, 0.0, 1.0),
        FVector3d(1.0, 0.0, 0.0));

    // A bad forward returns the normalized fallback
    const FQuat4d fallback(0.0, 0.0, 0.5, 0.5);
    const FQuat4d fromNaN = SOLNewtonian::OrientationFromForward(NfmNaNVector(), FVector3d(0.0, 0.0, 1.0), fallback);
    const FQuat4d expected = fallback.GetNormalized();
    TestTrue(TEXT("NaN forward: fallback"), FMath::Abs(FMath::Abs(fromNaN | expected) - 1.0) < 1.0e-12);
    const FQuat4d fromZero = SOLNewtonian::OrientationFromForward(FVector3d::ZeroVector, FVector3d(0.0, 0.0, 1.0),
        fallback);
    TestTrue(TEXT("Zero forward: fallback"), FMath::Abs(FMath::Abs(fromZero | expected) - 1.0) < 1.0e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathHandoffNoPopTest, "SOLTest.NewtonianFlightMath.HandoffNoPop",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ship promoted onto its rail (deviation 0, rail frame, no gravity input) stays exactly on the rail when idle,
// and its hand-off nose follows the rail velocity
bool FSOLNewtonianFlightMathHandoffNoPopTest::RunTest(const FString& /*parameters*/)
{
    FSOLThrustEnvelope npc = NfmEnvelope();
    npc.ForwardAccelMps2 = 80.0;
    npc.ReverseAccelMps2 = 40.0;
    npc.LateralAccelMps2 = 40.0;
    npc.VerticalAccelMps2 = 40.0;
    const FVector3d railVelocity(-3000.0, 4000.0, 0.0);
    FSOLShipState deviation;
    deviation.Orientation = SOLNewtonian::OrientationFromForward(railVelocity, FVector3d(0.0, 0.0, 1.0),
        FQuat4d::Identity);
    TestTrue(TEXT("Nose along the rail velocity"), NfmVecNear(NfmForward(deviation.Orientation),
        railVelocity.GetSafeNormal(), 1.0e-9));
    for (int32 i = 0; i < 300; ++i)
    {
        deviation = SOLNewtonian::StepNewtonian(deviation, FSOLNewtonianCommand(), npc, FVector3d::ZeroVector, NFM_DT);
    }
    TestTrue(TEXT("Deviation stays exactly zero"), deviation.PositionM.IsZero() && deviation.VelocityMps.IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathRailReturnTest, "SOLTest.NewtonianFlightMath.RailReturn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Mid ship flies its deviation back to the rail (rendezvous with the rail point) without running away
bool FSOLNewtonianFlightMathRailReturnTest::RunTest(const FString& /*parameters*/)
{
    FSOLGuidanceParams params = NfmGuidance(30.0, 30.0, 0.0);
    params.ArrivalRadiusM = 5.0;
    params.ArrivalSpeedMps = 0.5;
    const FVector3d deviation(2000.0, -500.0, 300.0);
    const FVector3d deviationVelocity(0.0, 80.0, 0.0);
    const FNfmPointRun run = NfmRunPointMass(deviation, deviationVelocity, FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT, 300.0);
    TestTrue(TEXT("Returns to the rail"), run.bArrived && run.bBudgetOk);
    TestTrue(TEXT("Within 120 s"), run.ArrivalTimeS <= 120.0);
    TestTrue(TEXT("Excursion bounded by the stopping distance"), run.MaxShipRadiusM <= deviation.Size()
        + SOLNewtonian::StoppingDistanceM(deviationVelocity.Size(), 30.0, NFM_DT) + 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathSnapInvisibleTest,
    "SOLTest.NewtonianFlightMath.HandoffSnapInvisible",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A demotion snap is invisible when it is at most half a pixel at the viewer's distance
bool FSOLNewtonianFlightMathSnapInvisibleTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("Exactly half a pixel"), SOLNewtonian::IsHandoffSnapInvisible(0.5, 1000.0, 1.0e-3));
    TestFalse(TEXT("Just above half a pixel"), SOLNewtonian::IsHandoffSnapInvisible(0.51, 1000.0, 1.0e-3));
    TestTrue(TEXT("1 km snap at 2.5 million km"),
        SOLNewtonian::IsHandoffSnapInvisible(1000.0, 2.5e9, 8.2e-4));
    TestFalse(TEXT("3 km snap at 2,000 km"), SOLNewtonian::IsHandoffSnapInvisible(3000.0, 2.0e6, 8.2e-4));
    TestTrue(TEXT("Zero offset"), SOLNewtonian::IsHandoffSnapInvisible(0.0, 0.0, 0.0));
    TestFalse(TEXT("NaN offset"), SOLNewtonian::IsHandoffSnapInvisible(NfmNaN(), 1000.0, 1.0e-3));
    TestFalse(TEXT("NaN distance"), SOLNewtonian::IsHandoffSnapInvisible(0.1, NfmNaN(), 1.0e-3));
    TestFalse(TEXT("Infinite pixel"), SOLNewtonian::IsHandoffSnapInvisible(0.1, 1000.0, NfmInf()));
    TestFalse(TEXT("Negative offset"), SOLNewtonian::IsHandoffSnapInvisible(-0.1, 1000.0, 1.0e-3));
    TestFalse(TEXT("Negative distance"), SOLNewtonian::IsHandoffSnapInvisible(0.1, -1000.0, 1.0e-3));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Pilot G model
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGLoadTest, "SOLTest.NewtonianFlightMath.GLoad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// G-load is |proper acceleration| / g0: a 1 G hover reads 1, coasting reads 0, bad input reads 0
bool FSOLNewtonianFlightMathGLoadTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("One g0 is 1 G"), FMath::Abs(SOLNewtonian::GLoadG(FVector3d(0.0, 0.0, NFM_G0)) - 1.0) < 1.0e-12);
    TestTrue(TEXT("Direction does not matter"),
        FMath::Abs(SOLNewtonian::GLoadG(FVector3d(-3.0, 4.0, 0.0) * NFM_G0 / 5.0 * 9.0) - 9.0) < 1.0e-12);
    TestEqual(TEXT("Coasting (no thrust, gravity not counted) is 0 G"), SOLNewtonian::GLoadG(FVector3d::ZeroVector),
        0.0);
    TestEqual(TEXT("NaN is 0"), SOLNewtonian::GLoadG(NfmNaNVector()), 0.0);
    TestEqual(TEXT("Infinite is 0"), SOLNewtonian::GLoadG(FVector3d(NfmInf(), 0.0, 0.0)), 0.0);

    // An assisted hover over Earth: the thrust that cancels 9.81 m/s^2 of gravity reads 1.0003 G
    const FSOLThrustAllocation hover = SOLNewtonian::AllocateThrust(SOLNewtonian::VelocityHoldAccelMps2(
        FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d(0.0, 0.0, -9.81), 0.75, NFM_DT), FQuat4d::Identity,
        NfmPlayerEnvelope(), false);
    TestTrue(TEXT("Hover over Earth feels 1 G"), FMath::Abs(SOLNewtonian::GLoadG(hover.AchievedAccelWorldMps2)
        - 9.81 / NFM_G0) < 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGDrainTest, "SOLTest.NewtonianFlightMath.GDrainRate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Drain = ((G - sustained) / (max - sustained))^p / timeToEmpty above the sustained limit, 0 at or below it
bool FSOLNewtonianFlightMathGDrainTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();
    TestEqual(TEXT("Below sustained"), SOLNewtonian::GDrainPerS(1.0, limits), 0.0);
    TestEqual(TEXT("At sustained"), SOLNewtonian::GDrainPerS(1.5, limits), 0.0);
    TestEqual(TEXT("Inside the 1e-9 G tolerance"), SOLNewtonian::GDrainPerS(1.5 + 5.0e-10, limits), 0.0);
    TestEqual(TEXT("At the tolerance"), SOLNewtonian::GDrainPerS(1.5 + 1.0e-9, limits), 0.0);
    TestTrue(TEXT("Past the tolerance drains"), SOLNewtonian::GDrainPerS(1.5 + 1.0e-6, limits) > 0.0);
    TestTrue(TEXT("9 G empties in 10 s"), FMath::Abs(SOLNewtonian::GDrainPerS(9.0, limits) - 0.1) < 1.0e-12);
    TestTrue(TEXT("6 G"), FMath::Abs(SOLNewtonian::GDrainPerS(6.0, limits) - 0.036) < 1.0e-12);
    TestTrue(TEXT("4.5 G"), FMath::Abs(SOLNewtonian::GDrainPerS(4.5, limits) - 0.016) < 1.0e-12);
    TestTrue(TEXT("3 G"), FMath::Abs(SOLNewtonian::GDrainPerS(3.0, limits) - 0.004) < 1.0e-12);
    TestTrue(TEXT("Above max keeps growing"), SOLNewtonian::GDrainPerS(12.0, limits) > 0.1);
    TestTrue(TEXT("Monotone"), SOLNewtonian::GDrainPerS(5.0, limits) < SOLNewtonian::GDrainPerS(5.1, limits));
    TestEqual(TEXT("NaN"), SOLNewtonian::GDrainPerS(NfmNaN(), limits), 0.0);
    TestTrue(TEXT("+inf G drains infinitely fast"), SOLNewtonian::GDrainPerS(NfmInf(), limits) == NfmInf());

    // A tech upgrade (longer time to empty, higher sustained limit) slows the drain
    FSOLPilotGLimits upgraded = limits;
    upgraded.SustainedG = 3.0;
    upgraded.TimeToEmptyAtMaxGS = 20.0;
    TestEqual(TEXT("Upgraded: 3 G is sustained"), SOLNewtonian::GDrainPerS(3.0, upgraded), 0.0);
    TestTrue(TEXT("Upgraded: 9 G empties in 20 s"), FMath::Abs(SOLNewtonian::GDrainPerS(9.0, upgraded) - 0.05)
        < 1.0e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGMeterBlackoutTest,
    "SOLTest.NewtonianFlightMath.GMeterDrainToBlackout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Above the sustained limit the meter drains to zero and the pilot blacks out at the exact crossing instant
bool FSOLNewtonianFlightMathGMeterBlackoutTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();

    const FSOLGMeterState almost = SOLNewtonian::StepGMeter(NfmMeter(1.0), 9.0, limits, 9.9);
    TestTrue(TEXT("9.9 s at 9 G: meter 0.01, still conscious"), NfmMeterNear(almost, NfmMeter(0.01), 1.0e-12));
    TestTrue(TEXT("9.9 s at 9 G: has control"), SOLNewtonian::PilotHasControl(almost));

    // Blacked out at 10 s; in G-LOC the meter recovers at the 0.02/s unconscious floor even under the load
    const FSOLGMeterState out = SOLNewtonian::StepGMeter(NfmMeter(1.0), 9.0, limits, 10.5);
    TestTrue(TEXT("10.5 s at 9 G: blacked out 0.5 s ago, meter 0.01"), NfmMeterNear(out, NfmMeter(0.01, true, 4.5),
        1.0e-12));
    TestFalse(TEXT("G-LOC: no control"), SOLNewtonian::PilotHasControl(out));

    // Small steps black out within one step of 10 s
    FSOLGMeterState meter = NfmMeter(1.0);
    int32 blackoutStep = -1;
    for (int32 i = 0; i < 400 && blackoutStep < 0; ++i)
    {
        meter = SOLNewtonian::StepGMeter(meter, 9.0, limits, NFM_DT);
        if (meter.bBlackout)
        {
            blackoutStep = i + 1;
        }
    }
    TestTrue(TEXT("Small steps: blackout at 10 s"), blackoutStep >= 300 && blackoutStep <= 301);

    // At or below the sustained limit nothing drains
    TestTrue(TEXT("1.5 G for an hour: full"), NfmMeterNear(SOLNewtonian::StepGMeter(NfmMeter(1.0), 1.5, limits,
        3600.0), NfmMeter(1.0), 0.0));

    // An empty meter under load blacks out at once (then 0.1 s at the unconscious floor)
    TestTrue(TEXT("Empty meter at 2 G: immediate blackout"), NfmMeterNear(SOLNewtonian::StepGMeter(NfmMeter(0.0),
        2.0, limits, 0.1), NfmMeter(0.002, true, 4.9), 1.0e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGMeterWakeTest, "SOLTest.NewtonianFlightMath.GMeterWake",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// G-LOC ends at the later of the timer and the meter reaching the wake level; under load the meter still recovers at
// the unconscious floor, so it always ends
bool FSOLNewtonianFlightMathGMeterWakeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();
    FSOLGMeterState meter = NfmMeter(0.0, true, 5.0);

    meter = SOLNewtonian::StepGMeter(meter, 1.0, limits, 3.0);
    TestTrue(TEXT("3 s: still out, recovering"), NfmMeterNear(meter, NfmMeter(0.15, true, 2.0), 1.0e-12));
    meter = SOLNewtonian::StepGMeter(meter, 1.0, limits, 1.0);
    TestTrue(TEXT("4 s: meter at wake level, timer not done"), NfmMeterNear(meter, NfmMeter(0.2, true, 1.0),
        1.0e-12));
    meter = SOLNewtonian::StepGMeter(meter, 1.0, limits, 1.5);
    TestTrue(TEXT("5.5 s: woke at 5 s, then 0.5 s of recovery"), NfmMeterNear(meter, NfmMeter(0.275), 1.0e-12));
    TestTrue(TEXT("Awake: has control"), SOLNewtonian::PilotHasControl(meter));

    // Meter-limited wake: a slow recovery wakes later than the timer
    FSOLPilotGLimits slow = limits;
    slow.RecoverPerS = 0.02;
    const FSOLGMeterState late = SOLNewtonian::StepGMeter(NfmMeter(0.0, true, 5.0), 0.0, slow, 9.0);
    TestTrue(TEXT("Slow recovery: still out at 9 s"), late.bBlackout && FMath::Abs(late.Meter - 0.18) < 1.0e-12);
    const FSOLGMeterState woke = SOLNewtonian::StepGMeter(late, 0.0, slow, 2.0);
    TestTrue(TEXT("Slow recovery: woke at 10 s, then 1 s more"), NfmMeterNear(woke, NfmMeter(0.22), 1.0e-12));

    // Still under load after the timer: the meter recovers at the 0.02/s floor, out until it reaches 0.2 (10 s)
    const FSOLGMeterState loaded = SOLNewtonian::StepGMeter(NfmMeter(0.0, true, 0.5), 9.0, limits, 2.0);
    TestTrue(TEXT("Under load: still out, meter at the floor rate"), NfmMeterNear(loaded, NfmMeter(0.04, true, 0.0),
        1.0e-12));
    const FSOLGMeterState loadedWake = SOLNewtonian::StepGMeter(loaded, 9.0, limits, 9.0);
    TestTrue(TEXT("Under load: wakes 8 s later with 0.2, then 1 s of 9 G drain"), NfmMeterNear(loadedWake,
        NfmMeter(0.1), 1.0e-12));

    // A recovery rate above the floor is used as is in G-LOC (max of the two)
    const FSOLGMeterState fast = SOLNewtonian::StepGMeter(NfmMeter(0.0, true, 5.0), 0.0, limits, 2.0);
    TestTrue(TEXT("Unloaded G-LOC recovers at RecoverPerS (0.05 > 0.02)"), NfmMeterNear(fast, NfmMeter(0.1, true, 3.0),
        1.0e-12));

    // Recovery is clamped at full
    TestTrue(TEXT("Clamped at 1"), NfmMeterNear(SOLNewtonian::StepGMeter(NfmMeter(0.9), 0.0, limits, 100.0),
        NfmMeter(1.0), 1.0e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGMeterDtTest, "SOLTest.NewtonianFlightMath.GMeterDtInvariance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The meter is exact piecewise-linear: one long step equals many short (or uneven) steps, and runs are deterministic
bool FSOLNewtonianFlightMathGMeterDtTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();

    // 12 s at 9 G (blackout at 10 s, then 2 s at the 0.02/s unconscious floor: 0.04), then 6 s at 1 G (0.05/s: the
    // meter reaches 0.2 after 3.2 s, later than the 3 s timer, then 2.8 s of recovery: 0.34)
    const FSOLGMeterState bigAfter9 = SOLNewtonian::StepGMeter(NfmMeter(1.0), 9.0, limits, 12.0);
    const FSOLGMeterState bigEnd = SOLNewtonian::StepGMeter(bigAfter9, 1.0, limits, 6.0);
    TestTrue(TEXT("One step: blacked out after phase 1"), NfmMeterNear(bigAfter9, NfmMeter(0.04, true, 3.0),
        1.0e-12));
    TestTrue(TEXT("One step: awake at 0.34 after phase 2"), NfmMeterNear(bigEnd, NfmMeter(0.34), 1.0e-12));

    // The same with 1/30 s steps
    FSOLGMeterState small = NfmMeter(1.0);
    for (int32 i = 0; i < 360; ++i)
    {
        small = SOLNewtonian::StepGMeter(small, 9.0, limits, NFM_DT);
    }
    TestTrue(TEXT("Small steps: phase 1 matches"), NfmMeterNear(small, bigAfter9, 1.0e-9));
    for (int32 i = 0; i < 180; ++i)
    {
        small = SOLNewtonian::StepGMeter(small, 1.0, limits, NFM_DT);
    }
    TestTrue(TEXT("Small steps: phase 2 matches"), NfmMeterNear(small, bigEnd, 1.0e-9));

    // Uneven random steps covering the same phases, run twice: equal to each other bit for bit
    auto runUneven = [&limits](const int32 seed)
    {
        FRandomStream stream(seed);
        FSOLGMeterState state = NfmMeter(1.0);
        const double phases[2][2] = { { 9.0, 12.0 }, { 1.0, 6.0 } };
        for (const auto& phase : phases)
        {
            double left = phase[1];
            while (left > 0.0)
            {
                const double step = FMath::Min(left, NfmRange(stream, 0.001, 0.7));
                state = SOLNewtonian::StepGMeter(state, phase[0], limits, step);
                left -= step;
            }
        }
        return state;
    };
    const FSOLGMeterState unevenA = runUneven(NFM_SEED + 4);
    const FSOLGMeterState unevenB = runUneven(NFM_SEED + 4);
    TestTrue(TEXT("Uneven steps match the single step"), NfmMeterNear(unevenA, bigEnd, 1.0e-9));
    TestTrue(TEXT("Deterministic"), unevenA.Meter == unevenB.Meter && unevenA.bBlackout == unevenB.bBlackout
        && unevenA.BlackoutRemainingS == unevenB.BlackoutRemainingS);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGMeterEdgeTest, "SOLTest.NewtonianFlightMath.GMeterEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Bad dt or NaN G leave the meter unchanged; negative G recovers; infinite G blacks out at once
bool FSOLNewtonianFlightMathGMeterEdgeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();
    const FSOLGMeterState start = NfmMeter(0.6, false, 0.0);
    const double badDts[] = { 0.0, -1.0, NfmNaN(), NfmInf() };
    for (const double dt : badDts)
    {
        TestTrue(FString::Printf(TEXT("dt %f unchanged"), dt), NfmMeterNear(SOLNewtonian::StepGMeter(start, 9.0,
            limits, dt), start, 0.0));
    }
    TestTrue(TEXT("NaN G unchanged"), NfmMeterNear(SOLNewtonian::StepGMeter(start, NfmNaN(), limits, 1.0), start,
        0.0));

    // The depleted time is part of the state: bad input leaves it unchanged too
    const FSOLGMeterState depleted = NfmDepletedMeter(0.1, false, 0.0, 12.0);
    TestEqual(TEXT("Bad dt: DepletedS unchanged"), SOLNewtonian::StepGMeter(depleted, 9.0, limits, NfmNaN()).DepletedS,
        12.0);
    TestEqual(TEXT("NaN G: DepletedS unchanged"), SOLNewtonian::StepGMeter(depleted, NfmNaN(), limits, 1.0).DepletedS,
        12.0);

    TestTrue(TEXT("Negative G recovers"), NfmMeterNear(SOLNewtonian::StepGMeter(start, -3.0, limits, 2.0),
        NfmMeter(0.7), 1.0e-12));
    // Infinite G: blackout at once, then 0.5 s at the unconscious floor
    const FSOLGMeterState inf = SOLNewtonian::StepGMeter(NfmMeter(1.0), NfmInf(), limits, 0.5);
    TestTrue(TEXT("Infinite G: immediate blackout"), NfmMeterNear(inf, NfmMeter(0.01, true, 4.5), 1.0e-12));
    TestTrue(TEXT("Infinite G: finite state"), FMath::IsFinite(inf.Meter) && FMath::IsFinite(inf.BlackoutRemainingS));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGWarningTest, "SOLTest.NewtonianFlightMath.GWarningLevels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Warning levels: None, OverSustained (above 1.5 G), Critical (meter <= 0.25), Blackout, in that priority
bool FSOLNewtonianFlightMathGWarningTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();
    TestTrue(TEXT("1 G, full: None"), SOLNewtonian::GWarningLevel(NfmMeter(1.0), 1.0, limits) == ESOLGWarning::None);
    TestTrue(TEXT("Exactly 1.5 G: None"),
        SOLNewtonian::GWarningLevel(NfmMeter(1.0), 1.5, limits) == ESOLGWarning::None);
    TestTrue(TEXT("1.6 G: OverSustained"),
        SOLNewtonian::GWarningLevel(NfmMeter(1.0), 1.6, limits) == ESOLGWarning::OverSustained);
    TestTrue(TEXT("Meter 0.26 at 2 G: OverSustained"),
        SOLNewtonian::GWarningLevel(NfmMeter(0.26), 2.0, limits) == ESOLGWarning::OverSustained);
    TestTrue(TEXT("Meter 0.25 at 1 G: Critical"),
        SOLNewtonian::GWarningLevel(NfmMeter(0.25), 1.0, limits) == ESOLGWarning::Critical);
    TestTrue(TEXT("Meter 0.1 at 9 G: Critical"),
        SOLNewtonian::GWarningLevel(NfmMeter(0.1), 9.0, limits) == ESOLGWarning::Critical);
    TestTrue(TEXT("G-LOC: Blackout"),
        SOLNewtonian::GWarningLevel(NfmMeter(0.5, true, 2.0), 0.0, limits) == ESOLGWarning::Blackout);
    TestTrue(TEXT("NaN G, full: None"),
        SOLNewtonian::GWarningLevel(NfmMeter(1.0), NfmNaN(), limits) == ESOLGWarning::None);

    // "Above sustained" carries the 1e-9 G tolerance
    TestTrue(TEXT("1.5 G + 5e-10: None"),
        SOLNewtonian::GWarningLevel(NfmMeter(1.0), 1.5 + 5.0e-10, limits) == ESOLGWarning::None);
    TestTrue(TEXT("1.5 G + 1e-9: None"),
        SOLNewtonian::GWarningLevel(NfmMeter(1.0), 1.5 + 1.0e-9, limits) == ESOLGWarning::None);
    TestTrue(TEXT("1.5 G + 1e-7: OverSustained"),
        SOLNewtonian::GWarningLevel(NfmMeter(1.0), 1.5 + 1.0e-7, limits) == ESOLGWarning::OverSustained);

    // Lethal comes first once DepletedS reaches LethalWarningS, whatever the rest of the state and the load
    TestTrue(TEXT("Depleted 30 s in G-LOC: Lethal"),
        SOLNewtonian::GWarningLevel(NfmDepletedMeter(0.05, true, 2.0, 30.0), 9.0, limits) == ESOLGWarning::Lethal);
    TestTrue(TEXT("Depleted 45 s, critical: Lethal"),
        SOLNewtonian::GWarningLevel(NfmDepletedMeter(0.2, false, 0.0, 45.0), 9.0, limits) == ESOLGWarning::Lethal);
    TestTrue(TEXT("Depleted 30 s, full meter, no load: Lethal (state only)"),
        SOLNewtonian::GWarningLevel(NfmDepletedMeter(1.0, false, 0.0, 30.0), 0.0, limits) == ESOLGWarning::Lethal);
    TestTrue(TEXT("Depleted 29.9 s in G-LOC: Blackout"),
        SOLNewtonian::GWarningLevel(NfmDepletedMeter(0.05, true, 2.0, 29.9), 9.0, limits) == ESOLGWarning::Blackout);
    TestTrue(TEXT("Depleted 29.9 s, critical: Critical"),
        SOLNewtonian::GWarningLevel(NfmDepletedMeter(0.2, false, 0.0, 29.9), 9.0, limits) == ESOLGWarning::Critical);
    FSOLPilotGLimits shortFuse = limits;
    shortFuse.LethalWarningS = 5.0;
    TestTrue(TEXT("LethalWarningS is a tunable"),
        SOLNewtonian::GWarningLevel(NfmDepletedMeter(0.05, true, 2.0, 5.0), 9.0, shortFuse) == ESOLGWarning::Lethal);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGVisionTest, "SOLTest.NewtonianFlightMath.GVisionLoss",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Vision tunnels in below a 0.4 meter, is total at 0 and during G-LOC
bool FSOLNewtonianFlightMathGVisionTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();
    TestEqual(TEXT("Full meter"), SOLNewtonian::GVisionLoss(NfmMeter(1.0), limits), 0.0);
    TestEqual(TEXT("At the start level"), SOLNewtonian::GVisionLoss(NfmMeter(0.4), limits), 0.0);
    TestTrue(TEXT("Half way"), FMath::Abs(SOLNewtonian::GVisionLoss(NfmMeter(0.2), limits) - 0.5) < 1.0e-12);
    TestEqual(TEXT("Empty"), SOLNewtonian::GVisionLoss(NfmMeter(0.0), limits), 1.0);
    TestEqual(TEXT("G-LOC is total whatever the meter"), SOLNewtonian::GVisionLoss(NfmMeter(0.5, true, 1.0), limits),
        1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathPilotLimitTest, "SOLTest.NewtonianFlightMath.PilotLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sustained and autopilot limits in m/s^2, and the clamp that assisted flight and the autopilot apply
bool FSOLNewtonianFlightMathPilotLimitTest::RunTest(const FString& /*parameters*/)
{
    FSOLPilotGLimits limits = NfmLimits();
    TestTrue(TEXT("Sustained 1.5 G"), FMath::Abs(SOLNewtonian::PilotSustainedAccelMps2(limits) - 1.5 * NFM_G0)
        < 1.0e-12);
    FSOLPilotGLimits bad = limits;
    bad.SustainedG = NfmNaN();
    TestEqual(TEXT("NaN sustained gives 0"), SOLNewtonian::PilotSustainedAccelMps2(bad), 0.0);
    bad.SustainedG = -1.0;
    TestEqual(TEXT("Negative sustained gives 0"), SOLNewtonian::PilotSustainedAccelMps2(bad), 0.0);

    // Autopilot: sustained normally; the recovery limit while critical or blacked out
    limits.AutopilotRecoveryG = 1.0;
    TestTrue(TEXT("Autopilot, full meter"), FMath::Abs(SOLNewtonian::AutopilotAccelLimitMps2(limits, NfmMeter(1.0))
        - 1.5 * NFM_G0) < 1.0e-12);
    TestTrue(TEXT("Autopilot, meter 0.3"), FMath::Abs(SOLNewtonian::AutopilotAccelLimitMps2(limits, NfmMeter(0.3))
        - 1.5 * NFM_G0) < 1.0e-12);
    TestTrue(TEXT("Autopilot, critical"), FMath::Abs(SOLNewtonian::AutopilotAccelLimitMps2(limits, NfmMeter(0.25))
        - 1.0 * NFM_G0) < 1.0e-12);
    TestTrue(TEXT("Autopilot, G-LOC"), FMath::Abs(SOLNewtonian::AutopilotAccelLimitMps2(limits,
        NfmMeter(0.9, true, 1.0)) - 1.0 * NFM_G0) < 1.0e-12);
    limits.AutopilotRecoveryG = 1.5;
    limits.SustainedG = 3.0;
    TestTrue(TEXT("Upgraded pilot: autopilot at 3 G"), FMath::Abs(SOLNewtonian::AutopilotAccelLimitMps2(limits,
        NfmMeter(1.0)) - 3.0 * NFM_G0) < 1.0e-12);
    TestTrue(TEXT("Upgraded pilot out of meter: back to 1.5 G"), FMath::Abs(SOLNewtonian::AutopilotAccelLimitMps2(
        limits, NfmMeter(0.1)) - 1.5 * NFM_G0) < 1.0e-12);

    // Clamp
    const double limit = 1.5 * NFM_G0;
    TestTrue(TEXT("Over the limit is clamped"), NfmVecNear(SOLNewtonian::ClampToPilotLimit(FVector3d(30.0, 0.0, 0.0),
        limit), FVector3d(limit, 0.0, 0.0), 1.0e-12));
    TestTrue(TEXT("Clamp keeps direction"), NfmAngle(SOLNewtonian::ClampToPilotLimit(FVector3d(30.0, -40.0, 10.0),
        limit), FVector3d(30.0, -40.0, 10.0)) < 1.0e-12);
    TestTrue(TEXT("Under the limit unchanged"), NfmVecNear(SOLNewtonian::ClampToPilotLimit(FVector3d(3.0, 4.0, 0.0),
        limit), FVector3d(3.0, 4.0, 0.0), 0.0));
    TestTrue(TEXT("Infinite limit unchanged"), NfmVecNear(SOLNewtonian::ClampToPilotLimit(FVector3d(300.0, 0.0, 0.0),
        NfmInf()), FVector3d(300.0, 0.0, 0.0), 0.0));
    TestTrue(TEXT("Zero limit gives zero"), SOLNewtonian::ClampToPilotLimit(FVector3d(3.0, 0.0, 0.0), 0.0).IsZero());
    TestTrue(TEXT("NaN limit gives zero"),
        SOLNewtonian::ClampToPilotLimit(FVector3d(3.0, 0.0, 0.0), NfmNaN()).IsZero());
    TestTrue(TEXT("NaN request gives zero"), SOLNewtonian::ClampToPilotLimit(NfmNaNVector(), limit).IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGLoadToleranceTest,
    "SOLTest.NewtonianFlightMath.GLoadTolerance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A thrust clamped to exactly the sustained limit (whatever rounding the clamp and the G conversion add) never drains
// or warns, and the meter recovers under it; a load just past the 1e-9 G tolerance drains and warns
bool FSOLNewtonianFlightMathGLoadToleranceTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();
    const FSOLThrustEnvelope env = NfmPlayerEnvelope();
    const double sustained = SOLNewtonian::PilotSustainedAccelMps2(limits);
    FRandomStream stream(NFM_SEED + 6);
    for (int32 i = 0; i < 300; ++i)
    {
        // A request well above the limit in a random direction, clamped, then allocated in a random orientation
        const FVector3d request = FVector3d(stream.VRand()) * FMath::Pow(10.0, NfmRange(stream, 1.5, 7.0));
        const FVector3d clamped = SOLNewtonian::ClampToPilotLimit(request, sustained);
        const FQuat4d q = FQuat4d(FVector3d(stream.VRand()), NfmRange(stream, -UE_DOUBLE_PI, UE_DOUBLE_PI));
        const FSOLThrustAllocation allocation = SOLNewtonian::AllocateThrust(clamped, q, env, false);
        const double loads[] = { SOLNewtonian::GLoadG(clamped),
            SOLNewtonian::GLoadG(allocation.AchievedAccelWorldMps2) };
        for (const double gLoad : loads)
        {
            const FSOLGMeterState stepped = SOLNewtonian::StepGMeter(NfmMeter(0.5), gLoad, limits, 1.0);
            const bool bNoDrain = SOLNewtonian::GDrainPerS(gLoad, limits) == 0.0;
            const bool bNoWarning = SOLNewtonian::GWarningLevel(NfmMeter(1.0), gLoad, limits) == ESOLGWarning::None;
            const bool bRecovers = FMath::Abs(stepped.Meter - 0.55) < 1.0e-12 && !stepped.bBlackout
                && stepped.DepletedS == 0.0;
            if (!(bNoDrain && bNoWarning && bRecovers))
            {
                AddError(FString::Printf(TEXT("Sample %d: load %.17g G: no drain %d, no warning %d, recovers %d"), i,
                    gLoad, static_cast<int32>(bNoDrain), static_cast<int32>(bNoWarning),
                    static_cast<int32>(bRecovers)));
                return true;
            }
        }
    }

    // An hour at exactly the sustained limit keeps a full meter
    const double atLimit = SOLNewtonian::GLoadG(SOLNewtonian::ClampToPilotLimit(FVector3d(0.0, 1.0e6, 0.0),
        sustained));
    TestTrue(TEXT("An hour at the clamped limit: full"), NfmMeterNear(SOLNewtonian::StepGMeter(NfmMeter(1.0), atLimit,
        limits, 3600.0), NfmMeter(1.0), 0.0));

    // Just past the tolerance: drains and warns
    const double justOver = 1.5 + 1.0e-9 + 1.0e-7;
    TestTrue(TEXT("Just over: drains"), SOLNewtonian::GDrainPerS(justOver, limits) > 0.0);
    TestTrue(TEXT("Just over: OverSustained"),
        SOLNewtonian::GWarningLevel(NfmMeter(1.0), justOver, limits) == ESOLGWarning::OverSustained);
    TestTrue(TEXT("1e-3 G over: the meter goes down"),
        SOLNewtonian::StepGMeter(NfmMeter(0.5), 1.5 + 1.0e-9 + 1.0e-3, limits, 1.0).Meter < 0.5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGLocUnderLoadTest,
    "SOLTest.NewtonianFlightMath.GLocUnderLoadEnds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Under a constant load above sustained the pilot cycles: drain, G-LOC for max(BlackoutDurationS, WakeMeter /
// UnconsciousRecoverPerS), wake, drain again, G-LOC again; one long step equals many short ones
bool FSOLNewtonianFlightMathGLocUnderLoadTest::RunTest(const FString& /*parameters*/)
{
    FSOLPilotGLimits timerLimited = NfmLimits();
    timerLimited.UnconsciousRecoverPerS = 0.1;        // meter reaches 0.2 after 2 s; the 5 s timer decides

    struct FCase { double LoadG; FSOLPilotGLimits Limits; const TCHAR* Name; };
    const FCase cases[] = {
        { 9.0, NfmLimits(), TEXT("9 G, meter-limited wake") },
        { 3.0, NfmLimits(), TEXT("3 G, meter-limited wake") },
        { 9.0, timerLimited, TEXT("9 G, timer-limited wake") },
    };
    for (const FCase& c : cases)
    {
        const FSOLPilotGLimits& limits = c.Limits;
        const double drain = SOLNewtonian::GDrainPerS(c.LoadG, limits);
        const double entryS = 1.0 / drain;                                         // full meter to empty
        const double outS = FMath::Max(limits.BlackoutDurationS, limits.WakeMeter / limits.UnconsciousRecoverPerS);
        const double wakeMeter = limits.UnconsciousRecoverPerS * outS;             // meter at the wake instant
        const double redrainS = wakeMeter / drain;                                 // wake to the second G-LOC

        // One long step into each phase of the cycle
        const FSOLGMeterState inside = SOLNewtonian::StepGMeter(NfmMeter(1.0), c.LoadG, limits, entryS + 0.5 * outS);
        TestTrue(FString::Printf(TEXT("%s: out half way, recovering at the floor"), c.Name), NfmMeterNear(inside,
            NfmMeter(0.5 * outS * limits.UnconsciousRecoverPerS, true,
            FMath::Max(0.0, limits.BlackoutDurationS - 0.5 * outS)), 1.0e-9));
        const FSOLGMeterState awake = SOLNewtonian::StepGMeter(NfmMeter(1.0), c.LoadG, limits,
            entryS + outS + 0.5 * redrainS);
        TestTrue(FString::Printf(TEXT("%s: awake again, draining"), c.Name), NfmMeterNear(awake,
            NfmMeter(0.5 * wakeMeter), 1.0e-9));
        const double cycleEndS = entryS + outS + redrainS + 1.0;
        const FSOLGMeterState again = SOLNewtonian::StepGMeter(NfmMeter(1.0), c.LoadG, limits, cycleEndS);
        TestTrue(FString::Printf(TEXT("%s: second G-LOC, 1 s in"), c.Name), NfmMeterNear(again,
            NfmMeter(limits.UnconsciousRecoverPerS, true, limits.BlackoutDurationS - 1.0), 1.0e-9));

        // Short steps: the transitions land within a step of the predicted instants and the end state matches
        constexpr double dt = 0.05;
        const int32 steps = FMath::RoundToInt32(cycleEndS / dt);
        FSOLGMeterState meter = NfmMeter(1.0);
        double firstOutS = -1.0;
        double wakeS = -1.0;
        double secondOutS = -1.0;
        for (int32 i = 0; i < steps; ++i)
        {
            const bool bWasOut = meter.bBlackout;
            meter = SOLNewtonian::StepGMeter(meter, c.LoadG, limits, dt);
            const double t = (i + 1) * dt;
            if (!bWasOut && meter.bBlackout && firstOutS < 0.0)
            {
                firstOutS = t;
            }
            else if (!bWasOut && meter.bBlackout && secondOutS < 0.0)
            {
                secondOutS = t;
            }
            if (bWasOut && !meter.bBlackout && wakeS < 0.0)
            {
                wakeS = t;
            }
        }
        TestTrue(FString::Printf(TEXT("%s: G-LOC at %.3f s (expected %.3f)"), c.Name, firstOutS, entryS),
            FMath::Abs(firstOutS - entryS) <= dt + 1.0e-9);
        TestTrue(FString::Printf(TEXT("%s: wakes at %.3f s (expected %.3f)"), c.Name, wakeS, entryS + outS),
            FMath::Abs(wakeS - (entryS + outS)) <= dt + 1.0e-9);
        TestTrue(FString::Printf(TEXT("%s: G-LOC again at %.3f s (expected %.3f)"), c.Name, secondOutS,
            entryS + outS + redrainS), FMath::Abs(secondOutS - (entryS + outS + redrainS)) <= dt + 1.0e-9);
        const FSOLGMeterState big = SOLNewtonian::StepGMeter(NfmMeter(1.0), c.LoadG, limits, steps * dt);
        TestTrue(FString::Printf(TEXT("%s: one long step equals the short steps"), c.Name), NfmMeterNear(meter, big,
            1.0e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathDepletedLethalTest,
    "SOLTest.NewtonianFlightMath.DepletedAndLethal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DepletedS grows by dt while the load is above sustained and the pilot is out or critical, and resets otherwise;
// LethalWarningS of it shows Lethal, and dropping the load falls back to the lower warnings
bool FSOLNewtonianFlightMathDepletedLethalTest::RunTest(const FString& /*parameters*/)
{
    const FSOLPilotGLimits limits = NfmLimits();

    // The rule on single steps
    TestEqual(TEXT("Above sustained, meter high: 0"),
        SOLNewtonian::StepGMeter(NfmDepletedMeter(1.0, false, 0.0, 0.0), 3.0, limits, 1.0).DepletedS, 0.0);
    TestEqual(TEXT("Above sustained, high again: reset"),
        SOLNewtonian::StepGMeter(NfmDepletedMeter(0.9, false, 0.0, 7.0), 3.0, limits, 1.0).DepletedS, 0.0);
    TestEqual(TEXT("Critical at exactly sustained: reset"),
        SOLNewtonian::StepGMeter(NfmDepletedMeter(0.1, false, 0.0, 7.0), 1.5, limits, 1.0).DepletedS, 0.0);
    TestEqual(TEXT("Critical inside the tolerance: reset"),
        SOLNewtonian::StepGMeter(NfmDepletedMeter(0.1, false, 0.0, 7.0), 1.5 + 5.0e-10, limits, 1.0).DepletedS, 0.0);
    TestTrue(TEXT("Becomes critical under load: + dt"), FMath::Abs(SOLNewtonian::StepGMeter(
        NfmDepletedMeter(0.26, false, 0.0, 0.0), 9.0, limits, 0.5).DepletedS - 0.5) < 1.0e-12);
    TestTrue(TEXT("G-LOC under load: + dt"), FMath::Abs(SOLNewtonian::StepGMeter(
        NfmDepletedMeter(0.1, true, 3.0, 4.0), 9.0, limits, 2.0).DepletedS - 6.0) < 1.0e-12);
    TestEqual(TEXT("G-LOC without load: reset"),
        SOLNewtonian::StepGMeter(NfmDepletedMeter(0.1, true, 3.0, 4.0), 0.0, limits, 2.0).DepletedS, 0.0);

    // A constant 9 G: the meter is critical from 7.5 s on (then cycles through G-LOC, never above 0.25 again), so
    // Lethal shows LethalWarningS later; every step follows the rule
    constexpr double dt = 0.1;
    FSOLGMeterState meter = NfmMeter(1.0);
    double expectedDepleted = 0.0;
    double worstError = 0.0;
    double lethalS = -1.0;
    bool bLethalMatchesDepleted = true;
    for (int32 i = 0; i < 400; ++i)
    {
        meter = SOLNewtonian::StepGMeter(meter, 9.0, limits, dt);
        expectedDepleted = (meter.bBlackout || meter.Meter <= limits.CriticalMeter) ? expectedDepleted + dt : 0.0;
        worstError = FMath::Max(worstError, FMath::Abs(meter.DepletedS - expectedDepleted));
        const bool bLethal = SOLNewtonian::GWarningLevel(meter, 9.0, limits) == ESOLGWarning::Lethal;
        bLethalMatchesDepleted &= bLethal == (meter.DepletedS >= limits.LethalWarningS);
        if (bLethal && lethalS < 0.0)
        {
            lethalS = (i + 1) * dt;
        }
    }
    TestTrue(FString::Printf(TEXT("DepletedS follows the rule (worst %.3e s)"), worstError), worstError <= 1.0e-9);
    TestTrue(TEXT("Lethal exactly when DepletedS >= LethalWarningS"), bLethalMatchesDepleted);
    TestTrue(FString::Printf(TEXT("Lethal at %.2f s, about 7.5 + 30 s"), lethalS),
        FMath::Abs(lethalS - (7.5 + limits.LethalWarningS)) <= 2.0 * dt + 1.0e-9);

    // At 40 s the pilot is in G-LOC (34 s to 44 s); dropping the load to 1 G resets DepletedS: Blackout, then Critical
    // after the wake, then None once the meter is above 0.25
    TestTrue(TEXT("At 40 s: out"), meter.bBlackout);
    meter = SOLNewtonian::StepGMeter(meter, 1.0, limits, dt);
    TestEqual(TEXT("Load dropped: DepletedS reset"), meter.DepletedS, 0.0);
    TestTrue(TEXT("Load dropped: Blackout"), SOLNewtonian::GWarningLevel(meter, 1.0, limits) == ESOLGWarning::Blackout);
    bool bSawCritical = false;
    bool bSawLethal = false;
    for (int32 i = 0; i < 300; ++i)
    {
        meter = SOLNewtonian::StepGMeter(meter, 1.0, limits, dt);
        const ESOLGWarning warning = SOLNewtonian::GWarningLevel(meter, 1.0, limits);
        bSawCritical |= warning == ESOLGWarning::Critical;
        bSawLethal |= warning == ESOLGWarning::Lethal;
    }
    TestTrue(TEXT("Recovering: Critical after the wake"), bSawCritical);
    TestFalse(TEXT("Recovering: never Lethal"), bSawLethal);
    TestTrue(TEXT("Recovered: None"), SOLNewtonian::GWarningLevel(meter, 1.0, limits) == ESOLGWarning::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathManualBurstTest,
    "SOLTest.NewtonianFlightMath.ManualNineGBurst",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Manual flight may exceed the sustained limit: 3 G main engine warns, the boosted 9 G burst (and a capped boosted
// diagonal) empties the meter in 10 s and blacks the pilot out
bool FSOLNewtonianFlightMathManualBurstTest::RunTest(const FString& /*parameters*/)
{
    const FSOLThrustEnvelope env = NfmPlayerEnvelope();
    const FSOLPilotGLimits limits = NfmLimits();
    const double mainG = SOLNewtonian::GLoadG(SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 0.0, 0.0), env,
        false));
    TestTrue(TEXT("Main engine is 3 G"), FMath::Abs(mainG - 3.0) < 1.0e-12);
    TestTrue(TEXT("3 G warns"), SOLNewtonian::GWarningLevel(NfmMeter(1.0), mainG, limits)
        == ESOLGWarning::OverSustained);
    const double burstG = SOLNewtonian::GLoadG(SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 0.0, 0.0), env,
        true));
    TestTrue(TEXT("Boosted main engine is 9 G"), FMath::Abs(burstG - 9.0) < 1.0e-12);
    const double diagonalG = SOLNewtonian::GLoadG(SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 1.0, 1.0), env,
        true));
    TestTrue(TEXT("Boosted diagonal is capped at 9 G"), FMath::Abs(diagonalG - 9.0) < 1.0e-9);
    TestTrue(TEXT("Unboosted lateral is 2 G"), FMath::Abs(SOLNewtonian::GLoadG(SOLNewtonian::LocalThrustAccelMps2(
        FVector3d(0.0, -1.0, 0.0), env, false)) - 2.0) < 1.0e-12);

    // Hold the burst with the ship stepping too: blackout after 10 s, control lost, the ship keeps flying
    FSOLShipState state;
    FSOLGMeterState meter = NfmMeter(1.0);
    double blackoutS = -1.0;
    for (int32 i = 0; i < 330; ++i)
    {
        const FSOLNewtonianCommand command = SOLNewtonian::PilotHasControl(meter)
            ? NfmCommand(FVector3d(1.0, 0.0, 0.0), FVector3d::ZeroVector, true) : FSOLNewtonianCommand();
        const double gLoad = SOLNewtonian::GLoadG(state.Orientation.RotateVector(
            SOLNewtonian::LocalThrustAccelMps2(command.ThrustFraction, env, command.bBoost)));
        state = SOLNewtonian::StepNewtonian(state, command, env, FVector3d::ZeroVector, NFM_DT);
        meter = SOLNewtonian::StepGMeter(meter, gLoad, limits, NFM_DT);
        if (blackoutS < 0.0 && meter.bBlackout)
        {
            blackoutS = (i + 1) * NFM_DT;
        }
    }
    TestTrue(TEXT("Blackout after about 10 s"), FMath::Abs(blackoutS - 10.0) <= NFM_DT + 1.0e-9);
    TestTrue(TEXT("Blacked out: Blackout warning"), SOLNewtonian::GWarningLevel(meter, 0.0, limits)
        == ESOLGWarning::Blackout);
    TestTrue(TEXT("Blacked out: vision gone"), SOLNewtonian::GVisionLoss(meter, limits) == 1.0);
    TestTrue(TEXT("Coasting at the burst's final speed"), FMath::Abs(state.VelocityMps.X - 9.0 * NFM_G0 * blackoutS)
        <= 9.0 * NFM_G0 * NFM_DT * 1.001);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathNpcPilotCapsTest, "SOLTest.NewtonianFlightMath.NpcPilotCaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Human NPC classes are capped at the human 9 G maximum; AI ships only by their structural cap
bool FSOLNewtonianFlightMathNpcPilotCapsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Human 6 G class"), SOLNewtonian::EffectiveGCapG(ESOLPilotKind::Human, 6.0), 6.0);
    TestEqual(TEXT("Human 12 G class is held to 9"), SOLNewtonian::EffectiveGCapG(ESOLPilotKind::Human, 12.0), 9.0);
    TestEqual(TEXT("Human infinite is 9"), SOLNewtonian::EffectiveGCapG(ESOLPilotKind::Human, NfmInf()), 9.0);
    TestEqual(TEXT("AI 20 G structural"), SOLNewtonian::EffectiveGCapG(ESOLPilotKind::AI, 20.0), 20.0);
    TestTrue(TEXT("AI infinite"), SOLNewtonian::EffectiveGCapG(ESOLPilotKind::AI, NfmInf()) == NfmInf());
    TestEqual(TEXT("NaN"), SOLNewtonian::EffectiveGCapG(ESOLPilotKind::AI, NfmNaN()), 0.0);
    TestEqual(TEXT("Negative"), SOLNewtonian::EffectiveGCapG(ESOLPilotKind::Human, -1.0), 0.0);

    // WithGCap lowers (never raises) the envelope's total cap
    FSOLThrustEnvelope fighter = NfmEnvelope();
    fighter.ForwardAccelMps2 = 8.0 * NFM_G0;
    fighter.MaxTotalAccelMps2 = 0.0;
    const FSOLThrustEnvelope humanFighter = SOLNewtonian::WithGCap(fighter,
        SOLNewtonian::EffectiveGCapG(ESOLPilotKind::Human, 6.0));
    TestTrue(TEXT("Uncapped envelope gets the cap"), FMath::Abs(humanFighter.MaxTotalAccelMps2 - 6.0 * NFM_G0)
        < 1.0e-12);
    TestTrue(TEXT("Human fighter's 8 G engine pulls 6 G"), FMath::Abs(SOLNewtonian::GLoadG(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 0.0, 0.0), humanFighter, false)) - 6.0) < 1.0e-12);

    FSOLThrustEnvelope drone = NfmEnvelope();
    drone.ForwardAccelMps2 = 15.0 * NFM_G0;
    drone.MaxTotalAccelMps2 = 0.0;
    const FSOLThrustEnvelope aiDrone = SOLNewtonian::WithGCap(drone,
        SOLNewtonian::EffectiveGCapG(ESOLPilotKind::AI, 20.0));
    TestTrue(TEXT("AI drone's 15 G engine pulls 15 G"), FMath::Abs(SOLNewtonian::GLoadG(
        SOLNewtonian::LocalThrustAccelMps2(FVector3d(1.0, 0.0, 0.0), aiDrone, false)) - 15.0) < 1.0e-12);

    FSOLThrustEnvelope tight = NfmPlayerEnvelope();   // already capped at 9 G
    TestTrue(TEXT("A higher cap does not raise an existing one"), FMath::Abs(SOLNewtonian::WithGCap(tight, 20.0)
        .MaxTotalAccelMps2 - 9.0 * NFM_G0) < 1.0e-12);
    TestTrue(TEXT("NaN cap leaves it unchanged"), SOLNewtonian::WithGCap(tight, NfmNaN()).MaxTotalAccelMps2
        == tight.MaxTotalAccelMps2);
    TestTrue(TEXT("Zero cap leaves it unchanged"), SOLNewtonian::WithGCap(tight, 0.0).MaxTotalAccelMps2
        == tight.MaxTotalAccelMps2);
    TestTrue(TEXT("Other fields untouched"), SOLNewtonian::WithGCap(tight, 6.0).ForwardAccelMps2
        == tight.ForwardAccelMps2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// ETA and trajectory
//////////////////////////////////////////////////////////////////////////

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathEtaFormulaTest, "SOLTest.NewtonianFlightMath.EtaFormula",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// EstimateTimeToArrivalS follows the 1-D braking-curve plan in each of its cases
bool FSOLNewtonianFlightMathEtaFormulaTest::RunTest(const FString& /*parameters*/)
{
    const double cap = NFM_HALF_C;

    TestTrue(TEXT("From rest, equal budgets: 2 sqrt(d / a)"),
        FMath::Abs(SOLNewtonian::EstimateTimeToArrivalS(1.0e5, 0.0, 10.0, 10.0, 0.0, cap) - 200.0) < 1.0e-9);

    const double d1 = 1.0e5 * 10.0 / 50.0;
    const double unequal = FMath::Sqrt(2.0 * d1 / 40.0) + FMath::Sqrt(2.0 * (1.0e5 - d1) / 10.0);
    TestTrue(TEXT("From rest, unequal budgets"),
        FMath::Abs(SOLNewtonian::EstimateTimeToArrivalS(1.0e5, 0.0, 40.0, 10.0, 0.0, cap) - unequal) < 1.0e-9);

    // With a flip coast: t = 2 v / a + tf, where v covers d = v^2 / a + v tf
    const double withFlip = SOLNewtonian::EstimateTimeToArrivalS(1.0e5, 0.0, 10.0, 10.0, 3.0, cap);
    const double peak = 0.5 * 10.0 * (withFlip - 3.0);
    TestTrue(TEXT("Flip: distance consistent"), FMath::Abs(peak * peak / 10.0 + peak * 3.0 - 1.0e5) < 1.0e-6);

    // Cruise at a cap: accelerate, cruise, coast tf, brake
    const double cruiseExpected = 300.0 / 20.0 + (2.0e5 - 2250.0 - 300.0 - 2250.0) / 300.0 + 1.0 + 300.0 / 20.0;
    TestTrue(TEXT("Cruise at the cap"), FMath::Abs(SOLNewtonian::EstimateTimeToArrivalS(2.0e5, 0.0, 20.0, 20.0, 1.0,
        300.0) - cruiseExpected) < 1.0e-9);

    // Just under the braking curve: almost only the coast and the burn remain
    const double curve = SOLNewtonian::BrakingSpeedLimitMps(27000.0, 20.0, 2.0) * 0.999999;
    TestTrue(TEXT("On the curve: tf + v / a_b"), FMath::Abs(SOLNewtonian::EstimateTimeToArrivalS(27000.0, curve,
        20.0, 20.0, 2.0, cap) - (2.0 + curve / 20.0)) < 0.01);

    // Too fast: brake, overshoot, come back from rest
    TestTrue(TEXT("Too fast"), FMath::Abs(SOLNewtonian::EstimateTimeToArrivalS(12500.0, 1000.0, 20.0, 20.0, 0.0, cap)
        - 100.0) < 1.0e-9);

    // Receding: stop first, then from rest over the longer distance
    const double receding = 5.0 + 2.0 * FMath::Sqrt(10250.0 / 20.0);
    TestTrue(TEXT("Receding"), FMath::Abs(SOLNewtonian::EstimateTimeToArrivalS(1.0e4, -100.0, 20.0, 20.0, 0.0, cap)
        - receding) < 1.0e-9);

    // Above the cap: slow to the cap, cruise, brake
    TestTrue(TEXT("Above the cap"), FMath::Abs(SOLNewtonian::EstimateTimeToArrivalS(1.0e6, 400.0, 20.0, 20.0, 0.0,
        300.0) - (5.0 + (1.0e6 - 1750.0 - 2250.0) / 300.0 + 15.0)) < 1.0e-9);

    // 30 AU at the 1.5 G pilot plan: finite, 2 sqrt(d / a)
    const double plan = 0.9 * 1.5 * NFM_G0;
    const double far = 30.0 * SOLTestHelpers::AU_M;
    TestTrue(TEXT("30 AU"), SOLTestHelpers::RelativeError(SOLNewtonian::EstimateTimeToArrivalS(far, 0.0, plan, plan,
        0.0, cap), 2.0 * FMath::Sqrt(far / plan)) < 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathEtaEdgeTest, "SOLTest.NewtonianFlightMath.EtaEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At the target the ETA is 0; invalid inputs give DBL_MAX
bool FSOLNewtonianFlightMathEtaEdgeTest::RunTest(const FString& /*parameters*/)
{
    const double maxD = TNumericLimits<double>::Max();
    TestEqual(TEXT("At the target at rest"), SOLNewtonian::EstimateTimeToArrivalS(0.0, 0.0, 10.0, 10.0, 3.0, 100.0),
        0.0);
    TestEqual(TEXT("NaN distance"), SOLNewtonian::EstimateTimeToArrivalS(NfmNaN(), 0.0, 10.0, 10.0, 0.0, 100.0), maxD);
    TestEqual(TEXT("NaN speed"), SOLNewtonian::EstimateTimeToArrivalS(10.0, NfmNaN(), 10.0, 10.0, 0.0, 100.0), maxD);
    TestEqual(TEXT("Negative distance"), SOLNewtonian::EstimateTimeToArrivalS(-1.0, 0.0, 10.0, 10.0, 0.0, 100.0),
        maxD);
    TestEqual(TEXT("Zero acceleration"), SOLNewtonian::EstimateTimeToArrivalS(10.0, 0.0, 0.0, 10.0, 0.0, 100.0), maxD);
    TestEqual(TEXT("Zero braking"), SOLNewtonian::EstimateTimeToArrivalS(10.0, 0.0, 10.0, 0.0, 0.0, 100.0), maxD);
    TestEqual(TEXT("Zero cap"), SOLNewtonian::EstimateTimeToArrivalS(10.0, 0.0, 10.0, 10.0, 0.0, 0.0), maxD);
    TestEqual(TEXT("Infinite flip"), SOLNewtonian::EstimateTimeToArrivalS(10.0, 0.0, 10.0, 10.0, NfmInf(), 100.0),
        maxD);
    TestTrue(TEXT("Negative flip counts as 0"), FMath::Abs(SOLNewtonian::EstimateTimeToArrivalS(1.0e5, 0.0, 10.0,
        10.0, -5.0, NFM_HALF_C) - 200.0) < 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathGuidanceEtaTest, "SOLTest.NewtonianFlightMath.GuidanceEta",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The guidance's EtaS is the ETA function of its own distance and closing speed with the plan's flip time (not
// FlipTimeS + dt: SDD 18 Amendment 7, L6), and 0 once Arrived
bool FSOLNewtonianFlightMathGuidanceEtaTest::RunTest(const FString& /*parameters*/)
{
    FSOLGuidanceParams params = NfmGuidance(20.0, 15.0, 2.0);
    params.MaxCruiseSpeedMps = 500.0;
    FRandomStream stream(NFM_SEED + 5);
    const double dts[] = { NFM_DT, 0.5, 10.0, 0.0 };
    for (int32 i = 0; i < 200; ++i)
    {
        const FVector3d r = FVector3d(stream.VRand()) * FMath::Pow(10.0, NfmRange(stream, 2.0, 8.0));
        const FVector3d v = FVector3d(stream.VRand()) * NfmRange(stream, 0.0, 800.0);
        const double dt = dts[i % static_cast<int32>(UE_ARRAY_COUNT(dts))];
        const FSOLGuidanceOutput out = SOLNewtonian::ComputeRendezvousGuidance(r, v, FVector3d::ZeroVector,
            FVector3d::ZeroVector, params, dt);
        const double expected = SOLNewtonian::EstimateTimeToArrivalS(out.DistanceM, out.ClosingSpeedMps, 20.0, 15.0,
            2.0, 500.0);
        if (out.Phase == ESOLGuidancePhase::Arrived || FMath::Abs(out.EtaS - expected) > 1.0e-9 * expected + 1.0e-9)
        {
            AddError(FString::Printf(TEXT("Sample %d (dt %g): EtaS %g, expected %g"), i, dt, out.EtaS, expected));
            break;
        }
    }

    // A negative flip time counts as 0 inside the ETA's flip term too (EstimateTimeToArrivalS clamps it)
    FSOLGuidanceParams noFlip = params;
    noFlip.FlipTimeS = -3.0;
    const FSOLGuidanceOutput negative = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(1.0e5, 0.0, 0.0),
        FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector, noFlip, 0.5);
    TestTrue(TEXT("Negative flip time: tf = 0"), FMath::Abs(negative.EtaS - SOLNewtonian::EstimateTimeToArrivalS(
        1.0e5, 0.0, 20.0, 15.0, 0.0, 500.0)) <= 1.0e-9 * negative.EtaS);
    const FSOLGuidanceOutput arrived = SOLNewtonian::ComputeRendezvousGuidance(FVector3d(2.0, 0.0, 0.0),
        FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT);
    TestTrue(TEXT("Arrived: ETA 0"), arrived.Phase == ESOLGuidancePhase::Arrived && arrived.EtaS == 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathEtaCountdownTest, "SOLTest.NewtonianFlightMath.EtaCountdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Flying the plan, the ETA counts down in step with real time: t + EtaS stays within 5% (+2 s) of the actual arrival
bool FSOLNewtonianFlightMathEtaCountdownTest::RunTest(const FString& /*parameters*/)
{
    struct FCase { double Plan; double Brake; double Flip; double Cap; double Distance; const TCHAR* Name; };
    const FCase cases[] = {
        { 10.0, 10.0, 0.0, NFM_HALF_C, 1.0e5, TEXT("flip-and-burn") },
        { 40.0, 10.0, 0.0, NFM_HALF_C, 1.0e5, TEXT("unequal budgets") },
        { 20.0, 20.0, 1.0, 300.0, 2.0e5, TEXT("cruise") },
    };
    for (const FCase& c : cases)
    {
        FSOLGuidanceParams params = NfmGuidance(c.Plan, c.Brake, c.Flip);
        params.MaxCruiseSpeedMps = c.Cap;
        const FNfmPointRun run = NfmRunPointMass(FVector3d::ZeroVector, FVector3d::ZeroVector,
            FVector3d(c.Distance, 0.0, 0.0), FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT, 2000.0);
        const double tolerance = 0.05 * run.ArrivalTimeS + 2.0;
        TestTrue(FString::Printf(TEXT("%s: arrives"), c.Name), run.bArrived);
        TestTrue(FString::Printf(TEXT("%s: initial ETA"), c.Name),
            FMath::Abs(run.InitialEtaS - run.ArrivalTimeS) <= tolerance);
        TestTrue(FString::Printf(TEXT("%s: countdown never early"), c.Name),
            run.MinEtaPlusTimeS >= run.ArrivalTimeS - tolerance);
        TestTrue(FString::Printf(TEXT("%s: countdown never late"), c.Name),
            run.MaxEtaPlusTimeS <= run.ArrivalTimeS + tolerance);
    }
    return true;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Independent re-statement of the contract's trajectory sampler loop (used to check the production sampler)
    int32 NfmSampleReference(const FVector3d& relPosM, const FVector3d& relVelMps, const FSOLGuidanceParams& params,
        const double intervalS, const int32 substeps, const int32 capacity, TArray<FVector3d>& out)
    {
        out.Reset();
        FVector3d p = -relPosM;
        FVector3d v = relVelMps;
        const double h = intervalS / substeps;
        out.Add(p);
        while (out.Num() < capacity)
        {
            for (int32 s = 0; s < substeps; ++s)
            {
                const FSOLGuidanceOutput o = SOLNewtonian::ComputeRendezvousGuidance(-p, v, FVector3d::ZeroVector,
                    FVector3d::ZeroVector, params, h);
                if (o.Phase == ESOLGuidancePhase::Arrived)
                {
                    if (p != out.Last())
                    {
                        out.Add(p);
                    }
                    return out.Num();
                }
                v += o.DesiredNetAccelMps2 * h;
                p += v * h;
            }
            out.Add(p);
        }
        return out.Num();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathTrajectoryInvalidTest,
    "SOLTest.NewtonianFlightMath.TrajectoryInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The sampler writes nothing on bad input, only the start point when already arrived, and never past the view
bool FSOLNewtonianFlightMathTrajectoryInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(10.0, 10.0, 0.0);
    TArray<FVector3d> buffer;
    buffer.SetNumZeroed(8);
    const TArrayView<FVector3d> view(buffer);
    const FVector3d r(1.0e4, 0.0, 0.0);
    TestEqual(TEXT("NaN position"), SOLNewtonian::SampleRendezvousTrajectory(NfmNaNVector(), FVector3d::ZeroVector,
        params, 1.0, 4, view), 0);
    TestEqual(TEXT("NaN velocity"), SOLNewtonian::SampleRendezvousTrajectory(r, NfmNaNVector(), params, 1.0, 4,
        view), 0);
    TestEqual(TEXT("Zero interval"), SOLNewtonian::SampleRendezvousTrajectory(r, FVector3d::ZeroVector, params, 0.0,
        4, view), 0);
    TestEqual(TEXT("NaN interval"), SOLNewtonian::SampleRendezvousTrajectory(r, FVector3d::ZeroVector, params,
        NfmNaN(), 4, view), 0);
    TestEqual(TEXT("Zero substeps"), SOLNewtonian::SampleRendezvousTrajectory(r, FVector3d::ZeroVector, params, 1.0,
        0, view), 0);
    TestEqual(TEXT("Empty view"), SOLNewtonian::SampleRendezvousTrajectory(r, FVector3d::ZeroVector, params, 1.0, 4,
        TArrayView<FVector3d>()), 0);
    FSOLGuidanceParams badParams = params;
    badParams.PlanAccelMps2 = NfmNaN();
    TestEqual(TEXT("NaN params"), SOLNewtonian::SampleRendezvousTrajectory(r, FVector3d::ZeroVector, badParams, 1.0,
        4, view), 0);

    // Already arrived: just the start point
    TestEqual(TEXT("Already arrived"), SOLNewtonian::SampleRendezvousTrajectory(FVector3d(1.0, 0.0, 0.0),
        FVector3d::ZeroVector, params, 1.0, 4, view), 1);
    TestTrue(TEXT("Already arrived: start point"), buffer[0] == FVector3d(-1.0, 0.0, 0.0));

    // A small view is filled exactly, no further
    TArray<FVector3d> small;
    small.Init(FVector3d(7.0, 7.0, 7.0), 6);
    const int32 written = SOLNewtonian::SampleRendezvousTrajectory(r, FVector3d::ZeroVector, params, 1.0, 4,
        TArrayView<FVector3d>(small.GetData(), 4));
    TestEqual(TEXT("Small view filled"), written, 4);
    TestTrue(TEXT("Nothing written past the view"), small[4] == FVector3d(7.0, 7.0, 7.0)
        && small[5] == FVector3d(7.0, 7.0, 7.0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathTrajectoryReferenceTest,
    "SOLTest.NewtonianFlightMath.TrajectoryMatchesGuidance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The sampled path is the guidance law flown on a point mass: it matches an independent restatement point for point
bool FSOLNewtonianFlightMathTrajectoryReferenceTest::RunTest(const FString& /*parameters*/)
{
    FSOLGuidanceParams params = NfmGuidance(20.0, 15.0, 2.0);
    params.MaxCruiseSpeedMps = 400.0;
    const FVector3d r(3.0e4, -1.0e4, 5.0e3);
    const FVector3d v(50.0, 120.0, -30.0);
    constexpr int32 capacity = 64;
    TArray<FVector3d> produced;
    produced.SetNumZeroed(capacity);
    const int32 count = SOLNewtonian::SampleRendezvousTrajectory(r, v, params, 2.0, 4, TArrayView<FVector3d>(produced));
    TArray<FVector3d> reference;
    const int32 expectedCount = NfmSampleReference(r, v, params, 2.0, 4, capacity, reference);
    TestEqual(TEXT("Same number of points"), count, expectedCount);
    TestTrue(TEXT("Start point is ship minus target"), produced[0] == -r);
    bool bAllMatch = count == expectedCount;
    for (int32 i = 0; bAllMatch && i < count; ++i)
    {
        bAllMatch = NfmVecNear(produced[i], reference[i], 1.0e-9 * FMath::Max(1.0, reference[i].Size()));
    }
    TestTrue(TEXT("Every point matches"), bAllMatch);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNewtonianFlightMathTrajectoryArrivalTest,
    "SOLTest.NewtonianFlightMath.TrajectoryReachesTarget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sampled at ETA / 400, a straight leg from rest ends inside the arrival radius, on the line, monotonically closer
bool FSOLNewtonianFlightMathTrajectoryArrivalTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams params = NfmGuidance(10.0, 10.0, 0.0);
    const FVector3d r(1.0e5, 0.0, 0.0);
    const FSOLGuidanceOutput first = SOLNewtonian::ComputeRendezvousGuidance(r, FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, params, NFM_DT);
    TArray<FVector3d> path;
    path.SetNumZeroed(512);
    const int32 count = SOLNewtonian::SampleRendezvousTrajectory(r, FVector3d::ZeroVector, params, first.EtaS / 400.0,
        8, TArrayView<FVector3d>(path));
    TestTrue(TEXT("Arrives before the view is full"), count > 300 && count < 512);
    TestTrue(TEXT("Last point inside the arrival radius"), path[count - 1].Size() <= params.ArrivalRadiusM);
    bool bOnLine = true;
    bool bCloser = true;
    for (int32 i = 1; i < count; ++i)
    {
        bOnLine &= FMath::Abs(path[i].Y) < 1.0e-9 && FMath::Abs(path[i].Z) < 1.0e-9;
        bCloser &= path[i].Size() <= path[i - 1].Size() + 1.0e-9;
    }
    TestTrue(TEXT("Stays on the line"), bOnLine);
    TestTrue(TEXT("Monotonically closer"), bCloser);
    return true;
}

#endif
