/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLChase.h"
#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Flight/SOLShipState.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Issue #50 Part 50d regression (SDD 50 D19, R8 and contract case 14): AUTO with no lane leg must stay bit-identical to
// today's AUTO. Three fixed multi-substep runs of the existing pure steps (a body stop and a route fly-through through
// SOLFlight::AutopilotStep, a Chase Down through SOLFlight::ChaseStep, the G meter stepped by the caller as the flight
// processor does) are sampled at four checkpoints that cover the accelerate, brake and arrival (or capture) phases and
// compared bit for bit with values captured from the pre-50d build (HEAD b8f7f38, captured 2026-11-17). This file uses
// only API that exists before part 50d, so it builds on the old code alone. Part 50d must leave
// SOLFlight::AutopilotStep and SOLFlight::ChaseStep (their default arguments included) bit-identical.
//
// Capture protocol (once, before part 50d changes any flight code): build the pre-50d code with only this file added,
// run `Tools\RunTests.bat -Filter SOLTest.AutopilotRegression`, paste the logged rows over the rows below and set
// AUTOPILOTREG_CAPTURED to true. Until then the test fails on purpose.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // True once the rows below hold the values captured from the pre-50d build
    constexpr bool AUTOPILOTREG_CAPTURED = true;

    // Values recorded per checkpoint: position (3), velocity (3), orientation x y z w (4), angular velocity (3), the
    // last guidance's ETA, distance, speed limit and phase, and the last step's achieved thrust acceleration magnitude
    constexpr int32 AUTOPILOTREG_VALUES = 18;

    // Checkpoints per run (substeps done when sampled) and the substep length (sim seconds)
    constexpr int32 AUTOPILOTREG_CHECKPOINTS = 4;
    constexpr int32 AUTOPILOTREG_CHECKPOINT_STEPS[AUTOPILOTREG_CHECKPOINTS] = { 100, 200, 300, 400 };
    constexpr double AUTOPILOTREG_DT_S = 5.0;

    // One run's captured checkpoints
    struct FAutopilotRegRun
    {
        const TCHAR* Name;
        double V[AUTOPILOTREG_CHECKPOINTS][AUTOPILOTREG_VALUES];
    };

    // Captured body stop (AutopilotStep, no pass speed, no capture radius): accelerate, brake, arrived, arrived
    const FAutopilotRegRun AUTOPILOTREG_BODY = { TEXT("Body"), {
        { 149599544847.30023, -25243544.776849195, 3090148.3726451471, 6558.7720770280366, -1051.3787858626697,
            397.01930705688244, 0.17086882509482915, -0.038480154851948396, 0.033757824346483688, 0.98396317593123117,
            0, 0, 0, 595.41768966522409, 2335277.9848858202, 7758.8734110644773, 0, 13.238878809537708 },
        { 149601556824.47296, -25281243.031495657, 3226406.8392608115, 979.01800452735654, 835.10154831668513,
            99.901423040481603, -0.032914945878683974, -0.03727555407009342, 0.98854349764981642, 0.14250927219250756,
            0, 0, 0, 107.54859765873053, 72418.844973329164, 1283.9479523860657, 2, 12.552060861635173 },
        { 149601495000, -24700000, 3245000, -250, 1200, 30, -0.032910392211287005, -0.037276118773267711,
            0.98854409501058216, 0.14250603241059628, 0, 0, 0, 0, 0, 0, 3, 0.0020615528128088293 },
        { 149601370000, -24100000, 3260000, -250, 1200, 30, -0.032910392211287005, -0.037276118773267711,
            0.98854409501058216, 0.14250603241059628, 0, 0, 0, 0, 0, 0, 3, 0.0020615528128088293 } } };

    // Captured route fly-through (AutopilotStep with a pass speed, a capture radius and a target gravity):
    // accelerate, then captured at speed
    const FAutopilotRegRun AUTOPILOTREG_ROUTE = { TEXT("Route"), {
        { 149599546024.13428, -25236725.63028742, 3090846.4367690622, 6559.9204659789439, -964.13398417416397,
            404.7958126837417, 0.16236706143568941, -0.02772418591458169, 0.1566836705411202, 0.97381647873810118, 0, 0,
            0, 595.58913914660195, 2335955.8653421928, 7308.5092173990615, 0, 13.23772810196861 },
        { 149601453999.03534, -25249951.437663767, 3220625.685876044, 2140.0872589137593, 479.39918059914692,
            164.97167798071953, -0.031604111249014147, -0.0292833278385587, 0.98851327430494307, 0.14486260175000357, 0,
            0, 0, 0, 186134.83703031606, 2500, 3, 0.0020615521371256731 },
        { 149601382330.41943, -24666030.615951128, 3238637.3861344429, -1909.2108124391666, 1700.2447781297196,
            -63.6980298765911, -0.031604111249014147, -0.0292833278385587, 0.98851327430494307, 0.14486260175000357, 0,
            0, 0, 0, 109173.34303724986, 2500, 3, 13.238033690805091 },
        { 149601580509.55792, -24163467.707763519, 3271887.7786200102, -264.1834708956884, 1204.2762543135082,
            29.19903890666782, -0.031604111249014147, -0.0292833278385587, 0.98851327430494307, 0.14486260175000357, 0,
            0, 0, 0, 220264.42179574771, 2500, 3, 13.233921310863247 } } };

    // Captured Chase Down (ChaseStep with a burst policy and boost on): braking after the bursts, then on the
    // tail
    const FAutopilotRegRun AUTOPILOTREG_CHASE = { TEXT("Chase"), {
        { 149596190313.18466, -21071559.713060472, 2746717.6179251075, -4449.1477063465927, 10659.911178709046,
            -682.75341109478177, 0.095849631345183969, -0.095163426919187249, -0.52473634015443726, 0.84048113819961223,
            0, 0, 0, 0, 2195212.0683334507, 12173.560579447754, 2, 34.758551233262189 },
        { 149595670000, -19300000, 2640000, 800, -299.99999999952871, 40, 0.095850712577120231, -0.095161685325375001,
            -0.52473599480953903, 0.84048142769232936, 0, 0, 0, 0, 3.7252902984619141e-09, 4.7128737045544823e-10, 3,
            0.0022360678931928361 },
        { 149596070000, -19450000, 2660000, 800, -300, 40, 0.095850712577120231, -0.095161685325375001,
            -0.52473599480953903, 0.84048142769232936, 0, 0, 0, 0, 0, 0, 3, 0.0022360679774997907 },
        { 149596470000, -19600000, 2680000, 800, -300, 40, 0.095850712577120231, -0.095161685325375001,
            -0.52473599480953903, 0.84048142769232936, 0, 0, 0, 0, 0, 0, 3, 0.0022360679774997907 } } };

    //////////////////////////////////////////////////////////////////////////
    // Returns the fixed start state of every run: off-origin, moving, turned and spinning a little
    FSOLShipState AutopilotRegStart()
    {
        FSOLShipState state;
        state.PositionM = FVector3d(1.4959787e11, -2.5e7, 3.0e6);
        state.VelocityMps = FVector3d(120.0, 340.0, -15.0);
        state.Orientation = FQuat4d(FVector3d(0.3, -0.5, 0.8).GetSafeNormal(), 0.7);
        state.AngularVelocityRadPerS = FVector3d(0.01, -0.02, 0.005);
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes one checkpoint: the state, the last guidance and the last achieved thrust
    void AutopilotRegRecord(const FSOLShipState& state, const FSOLGuidanceOutput& guidance,
        const FSOLFlightStepOutput& step, double (&outValues)[AUTOPILOTREG_VALUES])
    {
        const double values[AUTOPILOTREG_VALUES] = { state.PositionM.X, state.PositionM.Y, state.PositionM.Z,
            state.VelocityMps.X, state.VelocityMps.Y, state.VelocityMps.Z, state.Orientation.X, state.Orientation.Y,
            state.Orientation.Z, state.Orientation.W, state.AngularVelocityRadPerS.X, state.AngularVelocityRadPerS.Y,
            state.AngularVelocityRadPerS.Z, guidance.EtaS, guidance.DistanceM, guidance.SpeedLimitMps,
            static_cast<double>(guidance.Phase), step.ThrustAccelWorldMps2.Size() };
        for (int32 index = 0; index < AUTOPILOTREG_VALUES; ++index)
        {
            outValues[index] = values[index];
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs AutopilotStep toward a target moving at constant velocity, the meter stepped from the achieved thrust
    FAutopilotRegRun AutopilotRegRunAutopilot(const TCHAR* name, const double targetGravityMps2,
        const double passSpeedMps, const double captureRadiusM)
    {
        const FSOLFlightParams params;
        FAutopilotRegRun run = { name, {} };
        FSOLShipState state = AutopilotRegStart();
        FSOLGMeterState meter;
        const FVector3d targetStartM = state.PositionM + FVector3d(4.0e6, -1.5e6, 2.0e5);
        const FVector3d targetVelocityMps(-250.0, 1200.0, 30.0);
        const FVector3d gravityMps2(-2.0e-3, 5.0e-4, 0.0);
        FSOLGuidanceOutput guidance;
        FSOLFlightStepOutput step;
        int32 checkpoint = 0;
        for (int32 index = 0; index < AUTOPILOTREG_CHECKPOINT_STEPS[AUTOPILOTREG_CHECKPOINTS - 1]; ++index)
        {
            const FVector3d targetM = targetStartM + targetVelocityMps * (AUTOPILOTREG_DT_S * index);
            state = SOLFlight::AutopilotStep(state, targetM, targetVelocityMps, FVector3d::ZeroVector, gravityMps2,
                params, meter, AUTOPILOTREG_DT_S, &step, &guidance, targetGravityMps2, passSpeedMps, captureRadiusM);
            meter = SOLNewtonian::StepGMeter(meter, SOLNewtonian::GLoadG(step.ThrustAccelWorldMps2),
                params.PilotLimits, AUTOPILOTREG_DT_S);
            if (index + 1 == AUTOPILOTREG_CHECKPOINT_STEPS[checkpoint])
            {
                AutopilotRegRecord(state, guidance, step, run.V[checkpoint]);
                ++checkpoint;
            }
        }
        return run;
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs ChaseStep toward a moving target with a burst policy, the meter stepped from the achieved thrust
    FAutopilotRegRun AutopilotRegRunChase()
    {
        const FSOLFlightParams params;
        FAutopilotRegRun run = { TEXT("Chase"), {} };
        FSOLShipState state = AutopilotRegStart();
        FSOLGMeterState meter;
        FSOLChasePolicy policy;
        policy.BurstG = 6.0;
        policy.BrakePlanG = 4.0;
        policy.FloorMeter = SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT;
        FSOLChaseRunState chaseRun;
        const FVector3d aimStartM = state.PositionM + FVector3d(-3.0e6, 6.0e6, -4.0e5);
        const FVector3d targetVelocityMps(800.0, -300.0, 40.0);
        const FVector3d gravityMps2(1.0e-3, -2.0e-3, 0.0);
        FSOLGuidanceOutput guidance;
        FSOLFlightStepOutput step;
        int32 checkpoint = 0;
        for (int32 index = 0; index < AUTOPILOTREG_CHECKPOINT_STEPS[AUTOPILOTREG_CHECKPOINTS - 1]; ++index)
        {
            const FVector3d aimM = aimStartM + targetVelocityMps * (AUTOPILOTREG_DT_S * index);
            state = SOLFlight::ChaseStep(state, aimM, targetVelocityMps, FVector3d::ZeroVector, gravityMps2,
                gravityMps2.Size(), params, meter, policy, true, AUTOPILOTREG_DT_S, chaseRun, &step, &guidance);
            meter = SOLNewtonian::StepGMeter(meter, SOLNewtonian::GLoadG(step.ThrustAccelWorldMps2),
                params.PilotLimits, AUTOPILOTREG_DT_S);
            if (index + 1 == AUTOPILOTREG_CHECKPOINT_STEPS[checkpoint])
            {
                AutopilotRegRecord(state, guidance, step, run.V[checkpoint]);
                ++checkpoint;
            }
        }
        return run;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one checkpoint's values as a C++ initializer list (17 significant digits, so a pasted value round-trips
    // bit for bit)
    FString AutopilotRegLine(const double (&values)[AUTOPILOTREG_VALUES])
    {
        FString line = TEXT("{ ");
        for (int32 index = 0; index < AUTOPILOTREG_VALUES; ++index)
        {
            line += FString::Printf(TEXT("%.17g%s"), values[index], index + 1 < AUTOPILOTREG_VALUES ? TEXT(", ")
                : TEXT(" },"));
        }
        return line;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLAutopilotRegressionTest, "SOLTest.AutopilotRegression.NoLaneBitIdentical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AUTO with no lane leg (a body stop, a route fly-through and a Chase Down) is bit-identical to the pre-50d build at
// every checkpoint (contract case 14 regression, D19); before the capture the test logs the rows to paste and fails
bool FSOLAutopilotRegressionTest::RunTest(const FString& /*parameters*/)
{
    struct FRegCase
    {
        const FAutopilotRegRun* Expected;
        FAutopilotRegRun Actual;
    };
    const FRegCase cases[] = {
        { &AUTOPILOTREG_BODY, AutopilotRegRunAutopilot(TEXT("Body"), 0.0, 0.0, 0.0) },
        { &AUTOPILOTREG_ROUTE, AutopilotRegRunAutopilot(TEXT("Route"), 3.0e-3, 2500.0, 5.0e5) },
        { &AUTOPILOTREG_CHASE, AutopilotRegRunChase() },
    };
    for (const FRegCase& regCase : cases)
    {
        // Before the capture: log the current values in paste-ready form
        if (!AUTOPILOTREG_CAPTURED)
        {
            AddInfo(FString::Printf(TEXT("%s rows to paste:"), regCase.Expected->Name));
            for (int32 checkpoint = 0; checkpoint < AUTOPILOTREG_CHECKPOINTS; ++checkpoint)
            {
                AddInfo(AutopilotRegLine(regCase.Actual.V[checkpoint]));
            }
            continue;
        }

        // After the capture: bit-identical (exact ==, no tolerance)
        for (int32 checkpoint = 0; checkpoint < AUTOPILOTREG_CHECKPOINTS; ++checkpoint)
        {
            for (int32 index = 0; index < AUTOPILOTREG_VALUES; ++index)
            {
                const double actual = regCase.Actual.V[checkpoint][index];
                const double expected = regCase.Expected->V[checkpoint][index];
                TestTrue(FString::Printf(TEXT("%s step %d value %d bit-identical (%.17g, captured %.17g)"),
                    regCase.Expected->Name, AUTOPILOTREG_CHECKPOINT_STEPS[checkpoint], index, actual, expected),
                    actual == expected);
            }
        }
    }
    TestTrue(TEXT("Regression values captured from the pre-50d build (see the capture protocol at the top)"),
        AUTOPILOTREG_CAPTURED);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
