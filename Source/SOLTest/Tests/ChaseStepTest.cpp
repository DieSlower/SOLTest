/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLChase.h"
#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Flight/SOLShipState.h"
#include "SOLConstants.h"

#include "Tests/SOLTestHelpers.h"

#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#include <cfloat>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for SOLFlight::ChaseStep (issue #32 part 32c, SDD 32 sections 3.3, 3.4, 3.8 and Amendment 2a), written against
// the declaration and the SDD before reading any implementation. ChaseStep advances the ship state only; the G meter is
// stepped by the caller from the step's achieved thrust (as the processor does), so the meter cases do that here.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Tolerance for exact relations (limits, caps, the floor)
    constexpr double CST_EPS = 1.0e-9;

    // Seed for the randomized meter-floor property case
    constexpr int32 CST_SEED = 20261103;

    // Short real-time substep
    constexpr double CST_SHORT_DT = 0.02;

    // A far aim distance: the guidance is in its Accelerate phase from rest
    constexpr double CST_FAR_M = 1.0e8;

    // Non-trivial ship position used by the fixed cases
    const FVector3d CST_SHIP_M(1.0e7, -2.0e7, 3.0e6);

    // A yaw of +90 degrees from identity: forward is +Y, up is +Z
    const FQuat4d CST_YAW90(FVector3d(0.0, 0.0, 1.0), UE_DOUBLE_HALF_PI);

    // Everything one ChaseStep call returns or writes
    struct FCstResult
    {
        FSOLShipState State;
        FSOLFlightStepOutput Step;
        FSOLGuidanceOutput Guidance;
        FSOLChaseGLimit Limit;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns standard gravity as the production code names it
    double CstG0()
    {
        return SOLNewtonian::STANDARD_GRAVITY_MPS2;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double CstNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the invalid-input cases
    double CstInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a uniform random double in [minValue, maxValue)
    double CstRange(const FRandomStream& stream, const double minValue, const double maxValue)
    {
        return minValue + (maxValue - minValue) * static_cast<double>(stream.FRand());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a log-uniform random double in [minValue, maxValue) (both positive)
    double CstLogRange(const FRandomStream& stream, const double minValue, const double maxValue)
    {
        return minValue * FMath::Pow(maxValue / minValue, static_cast<double>(stream.FRand()));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a random unit vector
    FVector3d CstRandomDir(const FRandomStream& stream)
    {
        const FVector3d dir(CstRange(stream, -1.0, 1.0), CstRange(stream, -1.0, 1.0), CstRange(stream, -1.0, 1.0));
        return dir.SizeSquared() > 1.0e-6 ? dir.GetSafeNormal() : FVector3d(1.0, 0.0, 0.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship state at rest at the fixed position with the given orientation
    FSOLShipState CstMakeState(const FQuat4d& orientation)
    {
        FSOLShipState state;
        state.PositionM = CST_SHIP_M;
        state.VelocityMps = FVector3d::ZeroVector;
        state.Orientation = orientation;
        state.AngularVelocityRadPerS = FVector3d::ZeroVector;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a chase policy with the given burst and brake-plan levels and floor
    FSOLChasePolicy CstPolicy(const double burstG, const double brakePlanG, const double floorMeter)
    {
        FSOLChasePolicy policy;
        policy.BurstG = burstG;
        policy.BrakePlanG = brakePlanG;
        policy.FloorMeter = floorMeter;
        return policy;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a G meter state with the given meter (conscious)
    FSOLGMeterState CstMeter(const double meter)
    {
        FSOLGMeterState state;
        state.Meter = meter;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs one ChaseStep toward a target moving at targetVelocity, planning with the local gravity's magnitude
    FCstResult CstStep(const FSOLShipState& state, const FVector3d& aimPointM, const FVector3d& targetVelocityMps,
        const FVector3d& gravityAccelMps2, const FSOLFlightParams& params, const FSOLGMeterState& meter,
        const FSOLChasePolicy& policy, const bool bBoost, const double dt, FSOLChaseRunState& inOutRun)
    {
        FCstResult result;
        result.State = SOLFlight::ChaseStep(state, aimPointM, targetVelocityMps, FVector3d::ZeroVector,
            gravityAccelMps2, gravityAccelMps2.Size(), params, meter, policy, bBoost, dt, inOutRun, &result.Step,
            &result.Guidance, &result.Limit);
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship's nose (local +X) in the universe frame
    FVector3d CstForward(const FQuat4d& orientation)
    {
        return orientation.RotateVector(FVector3d(1.0, 0.0, 0.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship's up (local +Z) in the universe frame
    FVector3d CstUp(const FQuat4d& orientation)
    {
        return orientation.RotateVector(FVector3d(0.0, 0.0, 1.0));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseStepBadDtTest, "SOLTest.ChaseStep.BadDtLeavesStateUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero, negative or non-finite substep changes nothing and writes default outputs
bool FSOLChaseStepBadDtTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipState start = CstMakeState(FQuat4d(FVector3d(1.0, 2.0, 3.0).GetSafeNormal(), 0.8));
    start.VelocityMps = FVector3d(12.0, -34.0, 56.0);
    start.AngularVelocityRadPerS = FVector3d(0.1, -0.2, 0.3);
    const FVector3d aim = CST_SHIP_M + FVector3d(CST_FAR_M, 0.0, 0.0);
    const FSOLChasePolicy policy = CstPolicy(6.0, 4.0, SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT);
    const FSOLChaseGLimit defaultLimit;
    const FSOLGuidanceOutput defaultGuidance;

    const double dts[] = { 0.0, -0.5, CstNaN(), CstInf(), -CstInf() };
    for (const bool bBoost : { true, false })
    {
        for (const double dt : dts)
        {
            const FString label = FString::Printf(TEXT("%s, dt %f"), bBoost ? TEXT("Boost") : TEXT("No boost"), dt);
            FSOLChaseRunState run;
            FSOLFlightStepOutput step;
            step.ThrustAccelWorldMps2 = FVector3d(1.0, 2.0, 3.0);          // must be reset
            FSOLGuidanceOutput guidance;
            guidance.Phase = ESOLGuidancePhase::Brake;                      // must be reset
            guidance.DesiredThrustAccelMps2 = FVector3d(4.0, 5.0, 6.0);
            guidance.DesiredForward = FVector3d(0.0, 1.0, 0.0);
            FSOLChaseGLimit limit;
            limit.LimitG = 7.0;                                             // must be reset
            limit.Activity = ESOLChaseActivity::BrakeBurst;
            limit.ReserveMeter = 0.5;
            limit.ResumeMeter = 0.9;

            const FSOLShipState result = SOLFlight::ChaseStep(start, aim, FVector3d(100.0, 0.0, 0.0),
                FVector3d::ZeroVector, FVector3d(0.0, 0.0, -9.81), 9.81, params, CstMeter(1.0), policy, bBoost, dt,
                run, &step, &guidance, &limit);

            TestTrue(*FString::Printf(TEXT("%s: position unchanged"), *label), result.PositionM == start.PositionM);
            TestTrue(*FString::Printf(TEXT("%s: velocity unchanged"), *label),
                (result.VelocityMps - start.VelocityMps).Size() <= 1e-12);
            TestTrue(*FString::Printf(TEXT("%s: orientation unchanged"), *label),
                result.Orientation.Equals(start.Orientation, 1e-12));
            TestTrue(*FString::Printf(TEXT("%s: angular velocity unchanged"), *label),
                (result.AngularVelocityRadPerS - start.AngularVelocityRadPerS).Size() <= 1e-12);
            TestTrue(*FString::Printf(TEXT("%s: output thrust zero"), *label), step.ThrustAccelWorldMps2.IsZero());
            TestTrue(*FString::Printf(TEXT("%s: guidance output default"), *label),
                guidance.Phase == defaultGuidance.Phase && guidance.DesiredThrustAccelMps2.IsZero()
                && guidance.DesiredNetAccelMps2.IsZero() && guidance.DesiredForward.IsZero());
            TestTrue(*FString::Printf(TEXT("%s: limit output default"), *label),
                limit.LimitG == defaultLimit.LimitG && limit.Activity == defaultLimit.Activity
                && limit.ReserveMeter == defaultLimit.ReserveMeter && limit.ResumeMeter == defaultLimit.ResumeMeter);
        }
    }

    // Null outputs are allowed
    FSOLChaseRunState run;
    const FSOLShipState result = SOLFlight::ChaseStep(start, aim, FVector3d::ZeroVector, FVector3d::ZeroVector,
        FVector3d::ZeroVector, 0.0, params, CstMeter(1.0), policy, true, 0.0, run);
    TestTrue(TEXT("Null outputs: position unchanged"), result.PositionM == start.PositionM);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseStepBurstTest, "SOLTest.ChaseStep.FarTargetBurstsAtBurstG",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Full meter, far target, from rest (Accelerate phase): the substep bursts at the policy's BurstG
bool FSOLChaseStepBurstTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const double g0 = CstG0();
    const double burstG = 6.0;
    const FSOLChasePolicy policy = CstPolicy(burstG, 4.0, SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT);
    const FVector3d aim = CST_SHIP_M + FVector3d(CST_FAR_M, 0.0, 0.0);

    FSOLChaseRunState run;
    const FCstResult result = CstStep(CstMakeState(FQuat4d::Identity), aim, FVector3d::ZeroVector,
        FVector3d::ZeroVector, params, CstMeter(1.0), policy, true, CST_SHORT_DT, run);

    TestTrue(TEXT("Guidance phase Accelerate"), result.Guidance.Phase == ESOLGuidancePhase::Accelerate);
    TestTrue(TEXT("Activity Burst"), result.Limit.Activity == ESOLChaseActivity::Burst);
    TestNearlyEqual(TEXT("Limit at BurstG"), result.Limit.LimitG, burstG, CST_EPS);
    TestNearlyEqual(TEXT("Run state records the limit"), run.LastLimitG, result.Limit.LimitG, CST_EPS);
    TestTrue(TEXT("Run state records Burst"), run.LastActivity == ESOLChaseActivity::Burst);
    TestFalse(TEXT("Not cooling"), run.bCooling);

    // The achieved thrust is above sustained (a real burst), toward the target, and within the limit
    const double thrust = result.Step.ThrustAccelWorldMps2.Size();
    TestTrue(TEXT("Thrust above sustained"), thrust > params.PilotLimits.SustainedG * g0 * 1.01);
    TestTrue(TEXT("Thrust within LimitG x g0"), thrust <= result.Limit.LimitG * g0 * (1.0 + CST_EPS) + CST_EPS);
    TestTrue(TEXT("Thrust toward the target"),
        result.Step.ThrustAccelWorldMps2.GetSafeNormal().Dot(FVector3d(1.0, 0.0, 0.0)) > 0.999);
    TestTrue(TEXT("Velocity grows toward the target"), result.State.VelocityMps.X > 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseStepLimitTest, "SOLTest.ChaseStep.ThrustWithinLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The achieved thrust acceleration is at most LimitG x g0 over burst levels, boost settings, orientations and gravity
bool FSOLChaseStepLimitTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const double g0 = CstG0();
    const double S = params.PilotLimits.SustainedG;
    const FVector3d gravities[] = { FVector3d::ZeroVector, FVector3d(0.0, 0.0, -0.5 * g0),
        FVector3d(0.3 * g0, -0.2 * g0, 0.0) };
    const FQuat4d orientations[] = { FQuat4d::Identity, CST_YAW90,
        FQuat4d(FVector3d(1.0, -2.0, 0.5).GetSafeNormal(), 2.3) };
    const double burstLevels[] = { S, 3.0, 6.0, 9.0 };
    for (const bool bBoost : { true, false })
    {
        for (const double burstG : burstLevels)
        {
            for (const FQuat4d& orientation : orientations)
            {
                for (const FVector3d& gravity : gravities)
                {
                    FSOLChaseRunState run;
                    const FSOLChasePolicy policy = CstPolicy(burstG, S, SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT);
                    const FCstResult result = CstStep(CstMakeState(orientation),
                        CST_SHIP_M + FVector3d(CST_FAR_M, 0.5 * CST_FAR_M, 0.0), FVector3d::ZeroVector, gravity,
                        params, CstMeter(1.0), policy, bBoost, CST_SHORT_DT, run);
                    const double thrust = result.Step.ThrustAccelWorldMps2.Size();
                    const FString label = FString::Printf(TEXT("%s, BurstG %.2f, g %.2f"),
                        bBoost ? TEXT("Boost") : TEXT("No boost"), burstG, gravity.Size());
                    TestTrue(*FString::Printf(TEXT("%s: thrust %.4f within LimitG %.4f x g0"), *label, thrust / g0,
                        result.Limit.LimitG), thrust <= result.Limit.LimitG * g0 * (1.0 + CST_EPS) + CST_EPS);
                    TestTrue(*FString::Printf(TEXT("%s: limit at most BurstG"), *label),
                        result.Limit.LimitG <= burstG + CST_EPS);
                    TestTrue(*FString::Printf(TEXT("%s: state finite"), *label),
                        SOLTestHelpers::IsFiniteVector(result.State.PositionM)
                        && SOLTestHelpers::IsFiniteVector(result.State.VelocityMps));
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseStepBoostTest, "SOLTest.ChaseStep.BoostOnlyWithToggle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Boost off never exceeds the unboosted cap; boost on with a high BurstG goes past it
bool FSOLChaseStepBoostTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const double g0 = CstG0();
    const double unboostedCapG = SOLChase::ChaseMaxG(params.Envelope, params.PilotLimits, false);
    const double boostedCapG = SOLChase::ChaseMaxG(params.Envelope, params.PilotLimits, true);
    TestTrue(TEXT("Precondition: the boosted cap is above the unboosted one"), boostedCapG > unboostedCapG * 1.5);

    const FSOLChasePolicy policy = CstPolicy(boostedCapG, boostedCapG, SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT);
    const FVector3d aim = CST_SHIP_M + FVector3d(CST_FAR_M, 0.0, 0.0);

    // Boost off: the limit and the achieved thrust stay at or under the unboosted cap
    {
        FSOLChaseRunState run;
        const FCstResult result = CstStep(CstMakeState(FQuat4d::Identity), aim, FVector3d::ZeroVector,
            FVector3d::ZeroVector, params, CstMeter(1.0), policy, false, CST_SHORT_DT, run);
        const double thrust = result.Step.ThrustAccelWorldMps2.Size();
        TestTrue(TEXT("Boost off: limit at most the unboosted cap"), result.Limit.LimitG <= unboostedCapG + CST_EPS);
        TestTrue(*FString::Printf(TEXT("Boost off: thrust %.4f G within the unboosted cap %.4f G"), thrust / g0,
            unboostedCapG), thrust <= unboostedCapG * g0 * (1.0 + CST_EPS) + CST_EPS);
    }

    // Boost off over a few substeps from other orientations: still never above the unboosted cap
    for (const FQuat4d& orientation : { CST_YAW90, FQuat4d(FVector3d(0.0, 1.0, 0.0), -0.7) })
    {
        FSOLChaseRunState run;
        FSOLShipState state = CstMakeState(orientation);
        double maxThrust = 0.0;
        for (int32 i = 0; i < 50; ++i)
        {
            const FCstResult result = CstStep(state, aim, FVector3d::ZeroVector, FVector3d::ZeroVector, params,
                CstMeter(1.0), policy, false, CST_SHORT_DT, run);
            maxThrust = FMath::Max(maxThrust, result.Step.ThrustAccelWorldMps2.Size());
            state = result.State;
        }
        TestTrue(*FString::Printf(TEXT("Boost off, turning: max thrust %.4f G within the unboosted cap"),
            maxThrust / g0), maxThrust <= unboostedCapG * g0 * (1.0 + CST_EPS) + CST_EPS);
    }

    // Boost on: the same policy goes past the unboosted cap and stays within its own limit
    {
        FSOLChaseRunState run;
        const FCstResult result = CstStep(CstMakeState(FQuat4d::Identity), aim, FVector3d::ZeroVector,
            FVector3d::ZeroVector, params, CstMeter(1.0), policy, true, CST_SHORT_DT, run);
        const double thrust = result.Step.ThrustAccelWorldMps2.Size();
        TestTrue(TEXT("Boost on: limit above the unboosted cap"), result.Limit.LimitG > unboostedCapG * 1.01);
        TestTrue(*FString::Printf(TEXT("Boost on: thrust %.4f G above the unboosted cap %.4f G"), thrust / g0,
            unboostedCapG), thrust > unboostedCapG * g0 * 1.01);
        TestTrue(TEXT("Boost on: thrust within LimitG x g0"),
            thrust <= result.Limit.LimitG * g0 * (1.0 + CST_EPS) + CST_EPS);
        TestTrue(TEXT("Boost on: limit at most the boosted cap"), result.Limit.LimitG <= boostedCapG + CST_EPS);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseStepMeterFloorTest, "SOLTest.ChaseStep.MeterNeverBelowFloor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Chained substeps (dt 0.01-300 s), with the caller stepping the meter, never take it below the floor or into G-LOC
bool FSOLChaseStepMeterFloorTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLPilotGLimits& limits = params.PilotLimits;
    const double g0 = CstG0();
    const double S = limits.SustainedG;
    const FRandomStream stream(CST_SEED);
    constexpr int32 Runs = 40;
    constexpr int32 StepsPerRun = 150;

    int32 failures = 0;
    double lowestMarginOverFloor = DBL_MAX;
    int32 burstSteps = 0;
    for (int32 runIndex = 0; runIndex < Runs; ++runIndex)
    {
        // A random chase: policy, floor, boost, geometry, motion and gravity
        const bool bBoost = stream.FRand() < 0.5f;
        const double floorMeter = SOLChase::EffectiveMeterFloor(CstRange(stream, 0.0, 0.3));
        const double burstG = CstRange(stream, S, limits.MaxG);
        const double brakePlanG = CstRange(stream, S, burstG);
        const FSOLChasePolicy policy = CstPolicy(burstG, brakePlanG, floorMeter);
        const double capG = SOLChase::ChaseMaxG(params.Envelope, limits, bBoost);

        FSOLShipState state = CstMakeState(FQuat4d(CstRandomDir(stream), CstRange(stream, 0.0, UE_DOUBLE_PI)));
        state.VelocityMps = CstRandomDir(stream) * CstRange(stream, 0.0, 2000.0);
        state.AngularVelocityRadPerS = CstRandomDir(stream) * CstRange(stream, 0.0, 0.5);
        FVector3d aim = state.PositionM + CstRandomDir(stream) * CstLogRange(stream, 1.0e3, 1.0e9);
        const FVector3d targetVelocity = CstRandomDir(stream) * CstRange(stream, 0.0, 2000.0);
        const FVector3d gravity = CstRandomDir(stream) * CstRange(stream, 0.0, 0.5 * g0);
        FSOLGMeterState meter = CstMeter(CstRange(stream, floorMeter, 1.0));
        FSOLChaseRunState run;

        for (int32 stepIndex = 0; stepIndex < StepsPerRun; ++stepIndex)
        {
            const double dt = CstLogRange(stream, 0.01, 300.0);
            const FCstResult result = CstStep(state, aim, targetVelocity, gravity, params, meter, policy, bBoost, dt,
                run);
            const double thrust = result.Step.ThrustAccelWorldMps2.Size();

            // The caller's meter step: the achieved thrust is the G-load source
            meter = SOLNewtonian::StepGMeter(meter, SOLNewtonian::GLoadG(result.Step.ThrustAccelWorldMps2), limits,
                dt);
            if (result.Limit.Activity == ESOLChaseActivity::Burst || result.Limit.Activity
                == ESOLChaseActivity::BrakeBurst)
            {
                ++burstSteps;
            }
            lowestMarginOverFloor = FMath::Min(lowestMarginOverFloor, meter.Meter - floorMeter);

            const bool bFloorHeld = meter.Meter >= floorMeter - CST_EPS;
            const bool bConscious = !meter.bBlackout;
            const bool bWithinLimit = thrust <= result.Limit.LimitG * g0 * (1.0 + CST_EPS) + CST_EPS;
            const bool bWithinCap = result.Limit.LimitG <= capG + CST_EPS;
            const bool bFinite = SOLTestHelpers::IsFiniteVector(result.State.PositionM)
                && SOLTestHelpers::IsFiniteVector(result.State.VelocityMps)
                && SOLTestHelpers::IsFiniteVector(result.State.AngularVelocityRadPerS);
            if (!(bFloorHeld && bConscious && bWithinLimit && bWithinCap && bFinite))
            {
                // Report only the first few failures so a regression does not flood the log
                if (failures < 10)
                {
                    AddError(FString::Printf(TEXT("Run %d step %d (dt %.3f s, %s, BurstG %.3f, BrakePlanG %.3f, "
                        "floor %.3f): meter %.6f blackout %d, thrust %.4f G, LimitG %.4f, cap %.4f, finite %d"),
                        runIndex, stepIndex, dt, bBoost ? TEXT("boost") : TEXT("no boost"), burstG, brakePlanG,
                        floorMeter, meter.Meter, meter.bBlackout ? 1 : 0, thrust / g0, result.Limit.LimitG, capG,
                        bFinite ? 1 : 0));
                }
                ++failures;
            }

            state = result.State;
            aim += targetVelocity * dt;
        }
    }

    TestEqual(TEXT("No substep broke the floor, G-LOC, the limit or the cap"), failures, 0);
    TestTrue(TEXT("The random chases did burst (the case is not vacuous)"), burstSteps > 0);
    AddInfo(FString::Printf(TEXT("Lowest meter over the floor: %.6f; burst substeps: %d"), lowestMarginOverFloor,
        burstSteps));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLChaseStepKinematicNoseTest, "SOLTest.ChaseStep.LongSubstepKinematicNose",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A substep beyond AUTOPILOT_KINEMATIC_ATTITUDE_S puts the nose straight on the guidance's forward (here the target)
// with zero spin, keeping the current up (SDD 18 Amendment 7 M4, shared with the autopilot per SDD 32 Amendment 2a)
bool FSOLChaseStepKinematicNoseTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const double threshold = SOL::AUTOPILOT_KINEMATIC_ATTITUDE_S;
    const FSOLChasePolicy policy = CstPolicy(6.0, 4.0, SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT);
    const double dts[] = { 2.0 * threshold, 10.0 * threshold, 60.0, 300.0 };

    // Target along +X, ship yawed to +Y and spinning: the nose snaps to +X and the up stays +Z
    for (const bool bBoost : { true, false })
    {
        for (const double dt : dts)
        {
            const FString label = FString::Printf(TEXT("+X, %s, dt %.1f s"),
                bBoost ? TEXT("boost") : TEXT("no boost"), dt);
            FSOLShipState start = CstMakeState(CST_YAW90);
            start.AngularVelocityRadPerS = FVector3d(0.3, -0.2, 0.5);
            FSOLChaseRunState run;
            const FCstResult result = CstStep(start, CST_SHIP_M + FVector3d(1.0e10, 0.0, 0.0), FVector3d::ZeroVector,
                FVector3d::ZeroVector, params, CstMeter(1.0), policy, bBoost, dt, run);
            const FVector3d forward = CstForward(result.State.Orientation);
            TestTrue(*FString::Printf(TEXT("%s: nose on the target (dot %.9f)"), *label,
                forward.Dot(FVector3d(1.0, 0.0, 0.0))), forward.Dot(FVector3d(1.0, 0.0, 0.0)) > 1.0 - 1.0e-6);
            TestTrue(*FString::Printf(TEXT("%s: nose on the guidance's forward"), *label),
                forward.Dot(result.Guidance.DesiredForward.GetSafeNormal()) > 1.0 - 1.0e-6);
            TestTrue(*FString::Printf(TEXT("%s: up kept"), *label),
                CstUp(result.State.Orientation).Dot(FVector3d(0.0, 0.0, 1.0)) > 1.0 - 1.0e-6);
            TestTrue(*FString::Printf(TEXT("%s: zero spin"), *label),
                result.State.AngularVelocityRadPerS.IsNearlyZero(1.0e-12));
        }
    }

    // An off-axis target from a tumbling start: the nose snaps straight to it
    const FVector3d targetDir = FVector3d(1.0, 2.0, 0.5).GetSafeNormal();
    for (const double dt : dts)
    {
        const FString label = FString::Printf(TEXT("Off-axis, dt %.1f s"), dt);
        FSOLShipState start = CstMakeState(FQuat4d(FVector3d(-0.4, 0.3, 0.8).GetSafeNormal(), 2.6));
        start.AngularVelocityRadPerS = FVector3d(-0.6, 0.4, 0.2);
        FSOLChaseRunState run;
        const FCstResult result = CstStep(start, CST_SHIP_M + targetDir * 1.0e10, FVector3d::ZeroVector,
            FVector3d::ZeroVector, params, CstMeter(1.0), policy, true, dt, run);
        const FVector3d forward = CstForward(result.State.Orientation);
        TestTrue(*FString::Printf(TEXT("%s: nose on the target (dot %.9f)"), *label, forward.Dot(targetDir)),
            forward.Dot(targetDir) > 1.0 - 1.0e-6);
        TestTrue(*FString::Printf(TEXT("%s: nose on the guidance's forward"), *label),
            forward.Dot(result.Guidance.DesiredForward.GetSafeNormal()) > 1.0 - 1.0e-6);
        TestTrue(*FString::Printf(TEXT("%s: zero spin"), *label),
            result.State.AngularVelocityRadPerS.IsNearlyZero(1.0e-12));
        TestTrue(*FString::Printf(TEXT("%s: orientation normalized"), *label),
            FMath::IsNearlyEqual(result.State.Orientation.Size(), 1.0, 1.0e-9));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
