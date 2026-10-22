/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"
#include "SOLConstants.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #18 (18b) player front end helpers in SOLFlight (assist thrust with gravity priority, attitude
// leveling, contact acceleration, autopilot substeps and step), written on 2026-10-21 against the 18b contract in
// Docs/Plans/18-newtonian-flight-core-plan.md before the production code existed.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Standard gravity, restated so the tests stay independent
    constexpr double PLAYER_FLIGHT_G0 = 9.80665;

    // Half the speed of light (the hard speed clamp)
    constexpr double PLAYER_FLIGHT_HALF_C = 149896229.0;

    // The player's substep length (SOL::SHIP_MAX_SUBSTEP_S), restated
    constexpr double PLAYER_FLIGHT_DT = 1.0 / 30.0;

    // The baseline pilot's sustained acceleration (1.5 G)
    constexpr double PLAYER_FLIGHT_SUSTAINED_MPS2 = 1.5 * PLAYER_FLIGHT_G0;

    // Relative slack on "never above" checks (pure rounding)
    constexpr double PLAYER_FLIGHT_ROUNDING = 1.0e-9;

    // The autopilot constants of the contract, restated (SOL::AUTOPILOT_*)
    constexpr int32 PLAYER_FLIGHT_MAX_SUBSTEPS = 256;
    constexpr double PLAYER_FLIGHT_HINT_WARP = 86400.0;
    constexpr double PLAYER_FLIGHT_HINT_RADII = 10.0;

    // The guidance plan margin (SOLNewtonian::GUIDANCE_ACCEL_MARGIN) and attitude gain, restated
    constexpr double PLAYER_FLIGHT_PLAN_MARGIN = 0.9;
    constexpr double PLAYER_FLIGHT_ATTITUDE_GAIN = 4.0;
    constexpr double PLAYER_FLIGHT_BRAKE_FRACTION = 0.8;

    // The review-fix autopilot constants (SDD 18 Amendment 7), restated
    constexpr double PLAYER_FLIGHT_GRAVITY_STANDOFF_FRACTION = 0.9;
    constexpr double PLAYER_FLIGHT_KINEMATIC_ATTITUDE_S = 1.0;

    // Sun-, Earth- and Jupiter-like bodies (GM in m^3/s^2, radius in m)
    constexpr double PLAYER_FLIGHT_SUN_GM = 1.32712440018e20;
    constexpr double PLAYER_FLIGHT_SUN_RADIUS_M = 6.957e8;
    constexpr double PLAYER_FLIGHT_EARTH_GM = 3.986004418e14;
    constexpr double PLAYER_FLIGHT_EARTH_RADIUS_M = 6.371e6;
    constexpr double PLAYER_FLIGHT_JUPITER_GM = 1.26686534e17;
    constexpr double PLAYER_FLIGHT_JUPITER_RADIUS_M = 7.1492e7;

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double PlayerFlightNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the invalid-input cases
    double PlayerFlightInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns explicit flight params (every field set, SDD 18 player values) so default drift cannot break the tests
    FSOLFlightParams PlayerFlightParams()
    {
        FSOLFlightParams params;
        params.AssistTimeConstantS = 0.75;
        params.BoostSpeedMultiplier = 2.0;
        params.MaxSpeedMps = PLAYER_FLIGHT_HALF_C;
        params.MinSpeedCapMps = 1.0;
        params.ShipRadiusM = 10.0;

        FSOLThrustEnvelope& env = params.Envelope;
        env.ForwardAccelMps2 = 3.0 * PLAYER_FLIGHT_G0;
        env.ReverseAccelMps2 = 1.5 * PLAYER_FLIGHT_G0;
        env.LateralAccelMps2 = 2.0 * PLAYER_FLIGHT_G0;
        env.VerticalAccelMps2 = 2.0 * PLAYER_FLIGHT_G0;
        env.BoostMultiplier = 3.0;
        env.MaxTotalAccelMps2 = 9.0 * PLAYER_FLIGHT_G0;
        env.MaxAngularAccelRadPerS2 = FVector3d(4.0, 3.0, 3.0);
        env.MaxAngularRateRadPerS = FVector3d(2.0943951023931953, 1.5707963267948966, 1.5707963267948966);
        env.MaxSpinRateRadPerS = 2.0 * UE_DOUBLE_PI;
        env.MaxSpeedMps = PLAYER_FLIGHT_HALF_C;

        FSOLPilotGLimits& limits = params.PilotLimits;
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
        return params;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a G meter state with the given fields
    FSOLGMeterState PlayerFlightMeter(const double meter, const bool bBlackout = false, const double remainingS = 0.0)
    {
        FSOLGMeterState state;
        state.Meter = meter;
        state.bBlackout = bBlackout;
        state.BlackoutRemainingS = remainingS;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two vectors are finite and match within an absolute tolerance
    bool PlayerFlightVecNear(const FVector3d& actual, const FVector3d& expected, const double tolerance)
    {
        return SOLTestHelpers::IsFiniteVector(actual) && (actual - expected).Size() <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an orientation pitched nose-up by pitchRad from identity: forward (cos p, 0, sin p)
    FQuat4d PlayerFlightPitched(const double pitchRad)
    {
        return FQuat4d(FVector3d(0.0, 1.0, 0.0), -pitchRad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected flip time for the player params: a PI pitch at the max rate with 0.8 of alpha, plus 1/gain
    // (the rate-limited branch of RotationTimeS applies because PI >= rate^2 / (0.8 * alpha) = 1.03 rad)
    double PlayerFlightFlipTimeS(const FSOLFlightParams& params)
    {
        const double rate = params.Envelope.MaxAngularRateRadPerS.Y;
        const double alpha = PLAYER_FLIGHT_BRAKE_FRACTION * params.Envelope.MaxAngularAccelRadPerS2.Y;
        return UE_DOUBLE_PI / rate + rate / alpha + 1.0 / PLAYER_FLIGHT_ATTITUDE_GAIN;
    }

    // Result of an autopilot leg flown in the tests
    struct FPlayerFlightLeg
    {
        bool bArrived = false;
        double ArrivalTimeS = 0.0;
        double MaxThrustMps2 = 0.0;
        bool bFinite = true;
        FSOLShipState FinalState;
        TArray<FVector2d> EtaSamples;      // (time, ETA) every sample interval while not arrived
    };

    //////////////////////////////////////////////////////////////////////////
    // Flies AutopilotStep from a state to a static target with no gravity until it arrives or maxTimeS passes
    FPlayerFlightLeg PlayerFlightFlyLeg(const FSOLShipState& start, const FVector3d& targetM,
        const FSOLFlightParams& params, const FSOLGMeterState& meter, const double dt, const double maxTimeS,
        const int32 stepsPerEtaSample)
    {
        FPlayerFlightLeg leg;
        FSOLShipState state = start;
        const int32 maxSteps = FMath::CeilToInt32(maxTimeS / dt);
        for (int32 step = 0; step < maxSteps; ++step)
        {
            FSOLFlightStepOutput output;
            FSOLGuidanceOutput guidance;
            state = SOLFlight::AutopilotStep(state, targetM, FVector3d::ZeroVector, FVector3d::ZeroVector,
                FVector3d::ZeroVector, params, meter, dt, &output, &guidance);
            leg.MaxThrustMps2 = FMath::Max(leg.MaxThrustMps2, output.ThrustAccelWorldMps2.Size());
            leg.bFinite = leg.bFinite && SOLTestHelpers::IsFiniteVector(state.PositionM)
                && SOLTestHelpers::IsFiniteVector(state.VelocityMps);
            if (guidance.Phase == ESOLGuidancePhase::Arrived)
            {
                leg.bArrived = true;
                leg.ArrivalTimeS = step * dt;
                break;
            }
            if (stepsPerEtaSample > 0 && step % stepsPerEtaSample == 0)
            {
                leg.EtaSamples.Add(FVector2d(step * dt, guidance.EtaS));
            }
        }
        leg.FinalState = state;
        return leg;
    }
}

// --- AssistThrustAccelMps2 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAssistUnsaturatedTest,
    "SOLTest.PlayerFlight.AssistThrustUnsaturatedEqualsVelocityHold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Below the limit the assist request is the plain velocity hold: (vTarget - v) / max(tau, dt, 1 ms) - g
bool FSOLPlayerFlightAssistUnsaturatedTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d v(1.0, 2.0, 3.0);
    const FVector3d vTarget(2.0, 2.0, 2.0);
    const FVector3d g(0.0, 0.0, -1.0);
    const double limit = PLAYER_FLIGHT_SUSTAINED_MPS2;

    // tau sets the divisor (dt shorter)
    const FVector3d byTau = SOLFlight::AssistThrustAccelMps2(v, vTarget, g, 0.75, PLAYER_FLIGHT_DT, limit);
    TestTrue(TEXT("tau divisor: (vT - v) / 0.75 - g"), PlayerFlightVecNear(byTau, (vTarget - v) / 0.75 - g, 1e-12));
    TestTrue(TEXT("Equals SOLNewtonian::VelocityHoldAccelMps2"), PlayerFlightVecNear(byTau,
        SOLNewtonian::VelocityHoldAccelMps2(v, vTarget, g, 0.75, PLAYER_FLIGHT_DT), 1e-12));

    // dt longer than tau sets the divisor
    const FVector3d byDt = SOLFlight::AssistThrustAccelMps2(v, vTarget, g, 0.75, 2.0, limit);
    TestTrue(TEXT("dt divisor: (vT - v) / 2 - g"), PlayerFlightVecNear(byDt, (vTarget - v) / 2.0 - g, 1e-12));

    // A NaN tau counts as 0, so dt sets the divisor
    const FVector3d nanTau = SOLFlight::AssistThrustAccelMps2(v, vTarget, g, PlayerFlightNaN(), 0.5, limit);
    TestTrue(TEXT("NaN tau counts as 0"), PlayerFlightVecNear(nanTau, (vTarget - v) / 0.5 - g, 1e-12));

    // Negative tau and dt fall to the 1 ms floor (+inf limit so the large result is not clamped)
    const FVector3d floored = SOLFlight::AssistThrustAccelMps2(v, vTarget, g, -1.0, -1.0, PlayerFlightInf());
    TestTrue(TEXT("1 ms floor"), PlayerFlightVecNear(floored, (vTarget - v) / 1.0e-3 - g, 1e-9));

    // A hold larger than the limit that still fits once gravity compensation is added is not scaled (s = 1)
    const FVector3d upG(0.0, 0.0, -PLAYER_FLIGHT_G0);
    const FVector3d bigDown = SOLFlight::AssistThrustAccelMps2(FVector3d::ZeroVector, FVector3d(0.0, 0.0, -15.0 * 0.75),
        upG, 0.75, PLAYER_FLIGHT_DT, limit);
    TestTrue(TEXT("Hold 15 m/s^2 down plus 1 G up = 5.19 m/s^2 down, kept whole"),
        PlayerFlightVecNear(bigDown, FVector3d(0.0, 0.0, PLAYER_FLIGHT_G0 - 15.0), 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAssistPriorityTest, "SOLTest.PlayerFlight.AssistThrustGravityPriority",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Saturated: gravity compensation is kept whole and only the hold part is scaled, to exactly the limit's length
bool FSOLPlayerFlightAssistPriorityTest::RunTest(const FString& /*parameters*/)
{
    const double limit = PLAYER_FLIGHT_SUSTAINED_MPS2;
    const double tau = 0.75;

    // 1 G down, hold 1000 m/s along +X: |result| = limit, the upward part is exactly 1 G, the rest goes to +X
    const FVector3d g(0.0, 0.0, -PLAYER_FLIGHT_G0);
    const FVector3d sideways = SOLFlight::AssistThrustAccelMps2(FVector3d::ZeroVector, FVector3d(1000.0, 0.0, 0.0), g,
        tau, PLAYER_FLIGHT_DT, limit);
    const double expectedX = FMath::Sqrt(limit * limit - PLAYER_FLIGHT_G0 * PLAYER_FLIGHT_G0);   // ~10.96 m/s^2
    TestTrue(*FString::Printf(TEXT("Length %.12f == limit"), sideways.Size()),
        FMath::Abs(sideways.Size() - limit) <= 1e-9 * limit);
    TestTrue(*FString::Printf(TEXT("Component against gravity %.12f == |g|"), sideways.Z),
        FMath::Abs(sideways.Z - PLAYER_FLIGHT_G0) <= 1e-9);
    TestTrue(TEXT("The remainder goes along the hold"), PlayerFlightVecNear(sideways,
        FVector3d(expectedX, 0.0, PLAYER_FLIGHT_G0), 1e-9));

    // Randomized: |g| < limit and an oversized hold: result = -g + s * hold with s in [0, 1], length == limit
    FRandomStream stream(1818);
    for (int32 index = 0; index < 200; ++index)
    {
        const FVector3d gravity = FVector3d(stream.VRand()) * (limit * 0.95 * stream.FRand());
        const FVector3d v = FVector3d(stream.VRand()) * 50.0;
        const FVector3d vTarget = FVector3d(stream.VRand()) * (100.0 + 1000.0 * stream.FRand());
        const FVector3d hold = (vTarget - v) / tau;
        if ((hold - gravity).Size() <= limit * 1.01)
        {
            continue;       // fits: covered by the unsaturated test
        }
        const FVector3d result = SOLFlight::AssistThrustAccelMps2(v, vTarget, gravity, tau, PLAYER_FLIGHT_DT, limit);
        const FVector3d holdPart = result + gravity;      // = s * hold
        const double s = FVector3d::DotProduct(holdPart, hold) / hold.SizeSquared();
        TestTrue(*FString::Printf(TEXT("Case %d: length == limit"), index),
            FMath::Abs(result.Size() - limit) <= 1e-9 * limit);
        TestTrue(*FString::Printf(TEXT("Case %d: s = %.6f in [0, 1]"), index, s), s >= -1e-12 && s <= 1.0 + 1e-12);
        TestTrue(*FString::Printf(TEXT("Case %d: hold part parallel to the hold"), index),
            (holdPart - hold * s).Size() <= 1e-9 * limit);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAssistStrongGravityTest,
    "SOLTest.PlayerFlight.AssistThrustGravityAtOrAboveLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// When |g| >= limit the whole budget fights gravity: the result is -g scaled to the limit, whatever the hold asks
bool FSOLPlayerFlightAssistStrongGravityTest::RunTest(const FString& /*parameters*/)
{
    const double limit = PLAYER_FLIGHT_SUSTAINED_MPS2;
    const FVector3d g25(0.0, 0.0, -2.5 * PLAYER_FLIGHT_G0);
    const FVector3d holds[] = { FVector3d(1000.0, 0.0, 0.0), FVector3d(0.0, 0.0, 1000.0), FVector3d(0.0, 0.0, -1000.0),
        FVector3d::ZeroVector };
    for (const FVector3d& vTarget : holds)
    {
        const FVector3d result = SOLFlight::AssistThrustAccelMps2(FVector3d::ZeroVector, vTarget, g25, 0.75,
            PLAYER_FLIGHT_DT, limit);
        TestTrue(*FString::Printf(TEXT("2.5 G, target (%.0f, %.0f, %.0f): straight up at the limit"), vTarget.X,
            vTarget.Y, vTarget.Z), PlayerFlightVecNear(result, FVector3d(0.0, 0.0, limit), 1e-9));
    }

    // An oblique gravity of 50 m/s^2 along -(3, 4, 0) / 5
    const FVector3d oblique = SOLFlight::AssistThrustAccelMps2(FVector3d::ZeroVector, FVector3d(0.0, 0.0, 500.0),
        FVector3d(-30.0, -40.0, 0.0), 0.75, PLAYER_FLIGHT_DT, limit);
    TestTrue(TEXT("Oblique strong gravity: -g scaled to the limit"),
        PlayerFlightVecNear(oblique, FVector3d(0.6, 0.8, 0.0) * limit, 1e-9));

    // |g| exactly the limit: -g itself, even with a hold pointing the other way
    const FVector3d equal = SOLFlight::AssistThrustAccelMps2(FVector3d::ZeroVector, FVector3d(0.0, 0.0, -1000.0),
        FVector3d(0.0, 0.0, -limit), 0.75, PLAYER_FLIGHT_DT, limit);
    TestTrue(TEXT("|g| == limit: -g"), PlayerFlightVecNear(equal, FVector3d(0.0, 0.0, limit), 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAssistLimitsTest,
    "SOLTest.PlayerFlight.AssistThrustInfiniteAndInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A +inf limit returns -g + hold unscaled; non-finite vectors or a NaN / non-positive limit return zero
bool FSOLPlayerFlightAssistLimitsTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d v(10.0, -20.0, 30.0);
    const FVector3d vTarget(1.0e6, 0.0, 0.0);
    const FVector3d g(0.0, 0.0, -3.0 * PLAYER_FLIGHT_G0);
    const double limit = PLAYER_FLIGHT_SUSTAINED_MPS2;

    const FVector3d unlimited = SOLFlight::AssistThrustAccelMps2(v, vTarget, g, 0.75, PLAYER_FLIGHT_DT,
        PlayerFlightInf());
    TestTrue(TEXT("+inf limit: hold - g"), PlayerFlightVecNear(unlimited, (vTarget - v) / 0.75 - g, 1e-6));

    const FVector3d nanVector(PlayerFlightNaN(), 0.0, 0.0);
    const FVector3d infVector(0.0, PlayerFlightInf(), 0.0);
    struct FPlayerFlightInvalidCase
    {
        const TCHAR* Label;
        FVector3d V;
        FVector3d VTarget;
        FVector3d G;
        double Limit;
    };
    const FPlayerFlightInvalidCase cases[] =
    {
        { TEXT("NaN v"), nanVector, vTarget, g, limit },
        { TEXT("inf vTarget"), v, infVector, g, limit },
        { TEXT("NaN g"), v, vTarget, nanVector, limit },
        { TEXT("inf g"), v, vTarget, infVector, limit },
        { TEXT("NaN limit"), v, vTarget, g, PlayerFlightNaN() },
        { TEXT("zero limit"), v, vTarget, g, 0.0 },
        { TEXT("negative limit"), v, vTarget, g, -5.0 },
        { TEXT("-inf limit"), v, vTarget, g, -PlayerFlightInf() },
    };
    for (const FPlayerFlightInvalidCase& testCase : cases)
    {
        const FVector3d result = SOLFlight::AssistThrustAccelMps2(testCase.V, testCase.VTarget, testCase.G, 0.75,
            PLAYER_FLIGHT_DT, testCase.Limit);
        TestTrue(*FString::Printf(TEXT("%s: zero"), testCase.Label), result.IsZero());
    }
    return true;
}

// --- LevelForward ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightLevelForwardTest, "SOLTest.PlayerFlight.LevelForward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ship's forward projected on the plane perpendicular to up, normalized; the forward itself when degenerate
bool FSOLPlayerFlightLevelForwardTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d up(0.0, 0.0, 1.0);

    // A level ship: unchanged
    TestTrue(TEXT("Identity: +X"), PlayerFlightVecNear(SOLFlight::LevelForward(FQuat4d::Identity, up),
        FVector3d(1.0, 0.0, 0.0), 1e-12));
    const FQuat4d yaw40(FVector3d(0.0, 0.0, 1.0), FMath::DegreesToRadians(40.0));
    TestTrue(TEXT("Yawed level ship: its own forward"), PlayerFlightVecNear(SOLFlight::LevelForward(yaw40, up),
        yaw40.RotateVector(FVector3d(1.0, 0.0, 0.0)), 1e-12));

    // A pitched (and yawed) nose: projected onto the horizontal, heading kept, unit length
    TestTrue(TEXT("Pitched 0.3 rad: +X"), PlayerFlightVecNear(SOLFlight::LevelForward(PlayerFlightPitched(0.3), up),
        FVector3d(1.0, 0.0, 0.0), 1e-12));
    const FQuat4d yawPitch = yaw40 * PlayerFlightPitched(-0.7);
    const FVector3d expectedYawPitch(FMath::Cos(FMath::DegreesToRadians(40.0)),
        FMath::Sin(FMath::DegreesToRadians(40.0)), 0.0);
    TestTrue(TEXT("Yawed and pitched down: horizontal heading"), PlayerFlightVecNear(SOLFlight::LevelForward(yawPitch,
        up), expectedYawPitch, 1e-12));

    // A non-unit up only sets the direction; a tilted up projects accordingly
    TestTrue(TEXT("Up of length 5"), PlayerFlightVecNear(SOLFlight::LevelForward(PlayerFlightPitched(0.3),
        FVector3d(0.0, 0.0, 5.0)), FVector3d(1.0, 0.0, 0.0), 1e-12));
    TestTrue(TEXT("Up along +X: identity nose along up levels toward minus its own up"),
        PlayerFlightVecNear(SOLFlight::LevelForward(FQuat4d::Identity, FVector3d(1.0, 0.0, 0.0)),
        FVector3d(0.0, 0.0, -1.0), 1e-12));
    const FVector3d tiltedUp = FVector3d(1.0, 0.0, 1.0).GetSafeNormal();
    TestTrue(TEXT("Up tilted 45 deg toward +X: identity forward projects to (1, 0, -1) / sqrt 2"),
        PlayerFlightVecNear(SOLFlight::LevelForward(FQuat4d::Identity, tiltedUp),
        FVector3d(1.0, 0.0, -1.0).GetSafeNormal(), 1e-12));

    // Nose along up (straight up or down): the level heading the shortest pitch reaches, i.e. the ship's up
    // projected (minus it when the nose points up, plus it when down); both pitched ships level out toward +X
    const FQuat4d noseUp = PlayerFlightPitched(UE_DOUBLE_HALF_PI);
    TestTrue(TEXT("Nose straight up: pitch back down to +X"), PlayerFlightVecNear(SOLFlight::LevelForward(noseUp, up),
        FVector3d(1.0, 0.0, 0.0), 1e-12));
    const FQuat4d noseDown = PlayerFlightPitched(-UE_DOUBLE_HALF_PI);
    TestTrue(TEXT("Nose straight down: pitch back up to +X"), PlayerFlightVecNear(SOLFlight::LevelForward(noseDown, up),
        FVector3d(1.0, 0.0, 0.0), 1e-12));

    // Invalid up: the ship's forward unchanged
    const FQuat4d pitched = PlayerFlightPitched(0.3);
    const FVector3d pitchedForward = pitched.RotateVector(FVector3d(1.0, 0.0, 0.0));
    TestTrue(TEXT("Zero up: forward kept"), PlayerFlightVecNear(SOLFlight::LevelForward(pitched, FVector3d::ZeroVector),
        pitchedForward, 1e-12));
    TestTrue(TEXT("NaN up: forward kept"), PlayerFlightVecNear(SOLFlight::LevelForward(pitched,
        FVector3d(0.0, 0.0, PlayerFlightNaN())), pitchedForward, 1e-12));
    TestTrue(TEXT("inf up: forward kept"), PlayerFlightVecNear(SOLFlight::LevelForward(pitched,
        FVector3d(0.0, 0.0, PlayerFlightInf())), pitchedForward, 1e-12));
    return true;
}

// --- ContactAccelMps2 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightContactTest, "SOLTest.PlayerFlight.ContactAccel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The surface pushes back along the normal with whatever (gravity + thrust) presses into it, never pulls
bool FSOLPlayerFlightContactTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d n(0.0, 0.0, 1.0);
    const FVector3d earthG(0.0, 0.0, -PLAYER_FLIGHT_G0);

    // Resting on Earth without thrust: 1 G up
    const FVector3d rest = SOLFlight::ContactAccelMps2(n, earthG, FVector3d::ZeroVector);
    TestTrue(TEXT("Rest: 9.80665 m/s^2 up"), PlayerFlightVecNear(rest, FVector3d(0.0, 0.0, PLAYER_FLIGHT_G0), 1e-12));
    TestEqual(TEXT("Rest: G-load 1"), SOLNewtonian::GLoadG(rest), 1.0, 1e-12);
    TestTrue(TEXT("Non-unit normal gives the same"), PlayerFlightVecNear(SOLFlight::ContactAccelMps2(
        FVector3d(0.0, 0.0, 3.0), earthG, FVector3d::ZeroVector), rest, 1e-12));

    // Thrust pushing away reduces it, down to zero (no pull)
    TestTrue(TEXT("5 m/s^2 up thrust: 4.80665 up"), PlayerFlightVecNear(SOLFlight::ContactAccelMps2(n, earthG,
        FVector3d(0.0, 0.0, 5.0)), FVector3d(0.0, 0.0, PLAYER_FLIGHT_G0 - 5.0), 1e-12));
    TestTrue(TEXT("20 m/s^2 up thrust: zero"), SOLFlight::ContactAccelMps2(n, earthG, FVector3d(0.0, 0.0, 20.0))
        .IsZero());

    // Thrust into the surface increases it; lateral thrust adds nothing along the normal
    TestTrue(TEXT("5 m/s^2 down thrust: 14.80665 up"), PlayerFlightVecNear(SOLFlight::ContactAccelMps2(n, earthG,
        FVector3d(0.0, 0.0, -5.0)), FVector3d(0.0, 0.0, PLAYER_FLIGHT_G0 + 5.0), 1e-12));
    TestTrue(TEXT("Lateral thrust: still 1 G up"), PlayerFlightVecNear(SOLFlight::ContactAccelMps2(n, earthG,
        FVector3d(5.0, -3.0, 0.0)), FVector3d(0.0, 0.0, PLAYER_FLIGHT_G0), 1e-12));

    // An oblique normal: the push is along the normal with the pressing component
    const FVector3d slope = FVector3d(0.0, 1.0, 1.0).GetSafeNormal();
    TestTrue(TEXT("45 deg slope: g * cos 45 along the normal"), PlayerFlightVecNear(SOLFlight::ContactAccelMps2(slope,
        earthG, FVector3d::ZeroVector), slope * (PLAYER_FLIGHT_G0 * UE_DOUBLE_INV_SQRT_2), 1e-12));

    // Zero or non-finite input: zero
    const FVector3d nanVector(PlayerFlightNaN(), 0.0, 0.0);
    const FVector3d infVector(0.0, 0.0, PlayerFlightInf());
    TestTrue(TEXT("Zero normal"), SOLFlight::ContactAccelMps2(FVector3d::ZeroVector, earthG, FVector3d::ZeroVector)
        .IsZero());
    TestTrue(TEXT("NaN normal"), SOLFlight::ContactAccelMps2(nanVector, earthG, FVector3d::ZeroVector).IsZero());
    TestTrue(TEXT("inf gravity"), SOLFlight::ContactAccelMps2(n, infVector * -1.0, FVector3d::ZeroVector).IsZero());
    TestTrue(TEXT("NaN thrust"), SOLFlight::ContactAccelMps2(n, earthG, nanVector).IsZero());
    TestTrue(TEXT("Zero gravity and thrust"), SOLFlight::ContactAccelMps2(n, FVector3d::ZeroVector,
        FVector3d::ZeroVector).IsZero());
    return true;
}

// --- Autopilot ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightSubstepTest, "SOLTest.PlayerFlight.AutopilotSubstepCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Substeps of max(10 s, T / 256), at least 1 and at most 256; 0 for an invalid frame time
bool FSOLPlayerFlightSubstepTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("16.7 s -> 2"), SOLFlight::AutopilotSubstepCount(16.7), 2);
    TestEqual(TEXT("1 d/s at 60 fps (1440 s) -> 144"), SOLFlight::AutopilotSubstepCount(86400.0 / 60.0), 144);
    TestEqual(TEXT("30 d/s at 60 fps (43200 s) -> 256"), SOLFlight::AutopilotSubstepCount(30.0 * 86400.0 / 60.0),
        256);
    TestEqual(TEXT("10 s -> 1"), SOLFlight::AutopilotSubstepCount(10.0), 1);
    TestEqual(TEXT("5 s -> 1"), SOLFlight::AutopilotSubstepCount(5.0), 1);
    TestEqual(TEXT("1/60 s -> 1"), SOLFlight::AutopilotSubstepCount(1.0 / 60.0), 1);
    TestEqual(TEXT("1e12 s -> 256"), SOLFlight::AutopilotSubstepCount(1.0e12), PLAYER_FLIGHT_MAX_SUBSTEPS);

    TestEqual(TEXT("0 -> 0"), SOLFlight::AutopilotSubstepCount(0.0), 0);
    TestEqual(TEXT("negative -> 0"), SOLFlight::AutopilotSubstepCount(-5.0), 0);
    TestEqual(TEXT("NaN -> 0"), SOLFlight::AutopilotSubstepCount(PlayerFlightNaN()), 0);
    TestEqual(TEXT("inf -> 0"), SOLFlight::AutopilotSubstepCount(PlayerFlightInf()), 0);

    // Sweep: always within [1, 256], and the substep T / count is never longer than max(10 s, T / 256)
    bool bInRange = true;
    bool bShortEnough = true;
    for (double frameS = 0.001; frameS < 1.0e8; frameS *= 1.37)
    {
        const int32 count = SOLFlight::AutopilotSubstepCount(frameS);
        bInRange = bInRange && count >= 1 && count <= PLAYER_FLIGHT_MAX_SUBSTEPS;
        bShortEnough = bShortEnough && frameS / count <= FMath::Max(10.0, frameS / PLAYER_FLIGHT_MAX_SUBSTEPS)
            * (1.0 + 1e-12);
    }
    TestTrue(TEXT("Count always in [1, 256]"), bInRange);
    TestTrue(TEXT("Substep never longer than max(10 s, T / 256)"), bShortEnough);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAutopilotShortLegTest, "SOLTest.PlayerFlight.AutopilotFlies100Km",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From rest, 100 km to a static target at 1/30 s: arrives in time, never above 1.5 G, and the ETA counts down
bool FSOLPlayerFlightAutopilotShortLegTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = PlayerFlightParams();
    const double distance = 1.0e5;
    const FVector3d target(distance, 0.0, 0.0);
    FSOLShipState start;
    start.PositionM = FVector3d::ZeroVector;
    start.VelocityMps = FVector3d::ZeroVector;
    start.Orientation = FQuat4d::Identity;
    start.AngularVelocityRadPerS = FVector3d::ZeroVector;

    // Plan acceleration: 0.9 of the 1.5 G pilot limit (no gravity); flip: half a turn at the pitch limits
    const double plan = PLAYER_FLIGHT_PLAN_MARGIN * PLAYER_FLIGHT_SUSTAINED_MPS2;     // 13.24 m/s^2
    const double flip = PlayerFlightFlipTimeS(params);                                // ~2.90 s
    const double ideal = 2.0 * FMath::Sqrt(distance / plan);                          // ~173.8 s
    const double bound = 1.3 * (ideal + flip + 10.0);                                 // ~243 s

    const int32 stepsPerSample = FMath::RoundToInt32(10.0 / PLAYER_FLIGHT_DT);
    const FPlayerFlightLeg leg = PlayerFlightFlyLeg(start, target, params, PlayerFlightMeter(1.0), PLAYER_FLIGHT_DT,
        bound, stepsPerSample);
    TestTrue(TEXT("State stays finite"), leg.bFinite);
    TestTrue(*FString::Printf(TEXT("Arrived (at %.2f s, bound %.2f s)"), leg.ArrivalTimeS, bound), leg.bArrived);

    // The thrust never exceeds plan, so the trip cannot beat 2 sqrt(d / plan) (less a little for the 10 m / 1 m/s
    // arrival box); 170 s leaves ~4 s of slack for that
    TestTrue(*FString::Printf(TEXT("Not faster than physics allows (%.2f s >= 170 s)"), leg.ArrivalTimeS),
        leg.ArrivalTimeS >= 170.0);
    TestTrue(*FString::Printf(TEXT("Max thrust %.9f G <= 1.5 G"), leg.MaxThrustMps2 / PLAYER_FLIGHT_G0),
        leg.MaxThrustMps2 <= PLAYER_FLIGHT_SUSTAINED_MPS2 * (1.0 + PLAYER_FLIGHT_ROUNDING));

    // At arrival the ship is inside the arrival box, give or take one step of motion
    TestTrue(*FString::Printf(TEXT("Ends within ~10 m of the target (%.3f m)"),
        (leg.FinalState.PositionM - target).Size()), (leg.FinalState.PositionM - target).Size() <= 10.0 + 1.0);
    TestTrue(*FString::Printf(TEXT("Ends nearly stopped (%.3f m/s)"), leg.FinalState.VelocityMps.Size()),
        leg.FinalState.VelocityMps.Size() <= 1.0 + PLAYER_FLIGHT_SUSTAINED_MPS2 * PLAYER_FLIGHT_DT);

    // The first ETA is the braking-curve plan: between 2 sqrt(d / plan) and that plus the flip time
    if (TestTrue(TEXT("ETA sampled"), leg.EtaSamples.Num() >= 2))
    {
        const double eta0 = leg.EtaSamples[0].Y;
        TestTrue(*FString::Printf(TEXT("Initial ETA %.3f s in [%.3f, %.3f]"), eta0, ideal, ideal + flip),
            eta0 >= ideal - 1e-6 && eta0 <= ideal + flip + PLAYER_FLIGHT_DT + 1e-6);

        // Each 10 s of flight takes 7 to 13 s off the ETA while more than 30 s remain (near the end the braking
        // curve's flip-time term makes the estimate optimistic, so the tail is excluded)
        for (int32 index = 1; index < leg.EtaSamples.Num(); ++index)
        {
            const FVector2d previous = leg.EtaSamples[index - 1];
            const FVector2d current = leg.EtaSamples[index];
            if (current.Y <= 30.0)
            {
                break;
            }
            const double drop = previous.Y - current.Y;
            TestTrue(*FString::Printf(TEXT("t %.0f s: ETA dropped %.3f s in 10 s"), current.X, drop),
                drop >= 7.0 && drop <= 13.0);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAutopilotLongLegTest,
    "SOLTest.PlayerFlight.AutopilotFlies10000KmWith10SSubsteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 10,000 km leg at the 10 s warp substep still arrives, never above 1.5 G
bool FSOLPlayerFlightAutopilotLongLegTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = PlayerFlightParams();
    const double distance = 1.0e7;
    const FVector3d target = FVector3d(3.0, -4.0, 12.0) / 13.0 * distance;
    FSOLShipState start;
    start.PositionM = FVector3d::ZeroVector;
    start.VelocityMps = FVector3d::ZeroVector;
    start.Orientation = FQuat4d::Identity;
    start.AngularVelocityRadPerS = FVector3d::ZeroVector;

    // ~1738 s ideal; 10 s steps add the step to the guidance's flip lead, so the final approach is slower: allow 1.5x
    const double dt = 10.0;
    const double plan = PLAYER_FLIGHT_PLAN_MARGIN * PLAYER_FLIGHT_SUSTAINED_MPS2;
    const double ideal = 2.0 * FMath::Sqrt(distance / plan);
    const double bound = 1.5 * (ideal + PlayerFlightFlipTimeS(params) + 10.0);
    const FPlayerFlightLeg leg = PlayerFlightFlyLeg(start, target, params, PlayerFlightMeter(1.0), dt, bound, 0);
    TestTrue(TEXT("State stays finite"), leg.bFinite);
    TestTrue(*FString::Printf(TEXT("Arrived (at %.0f s, bound %.0f s)"), leg.ArrivalTimeS, bound), leg.bArrived);
    TestTrue(*FString::Printf(TEXT("Max thrust %.9f G <= 1.5 G"), leg.MaxThrustMps2 / PLAYER_FLIGHT_G0),
        leg.MaxThrustMps2 <= PLAYER_FLIGHT_SUSTAINED_MPS2 * (1.0 + PLAYER_FLIGHT_ROUNDING));
    // The final state is one 10 s step after the arrival check (inside 10 m at <= 1 m/s); that step's small
    // correction (<= ~0.2 m/s^2 this close) can add at most a few m/s, so allow 10 m + 3 m/s * 10 s
    TestTrue(*FString::Printf(TEXT("Ends near the target (%.3f m)"), (leg.FinalState.PositionM - target).Size()),
        (leg.FinalState.PositionM - target).Size() <= 10.0 + 3.0 * dt);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAutopilotMeterTest,
    "SOLTest.PlayerFlight.AutopilotRecoveryLimitWhenCritical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With a 3 G trained pilot the autopilot plans at 0.9 * 3 G, but a critical or blacked-out meter drops it to 1.5 G
bool FSOLPlayerFlightAutopilotMeterTest::RunTest(const FString& /*parameters*/)
{
    FSOLFlightParams params = PlayerFlightParams();
    params.PilotLimits.SustainedG = 3.0;
    params.PilotLimits.AutopilotRecoveryG = 1.5;
    params.PilotLimits.CriticalMeter = 0.25;

    // From rest, far target straight ahead: the first step accelerates at the full plan along +X
    FSOLShipState start;
    start.PositionM = FVector3d::ZeroVector;
    start.VelocityMps = FVector3d::ZeroVector;
    start.Orientation = FQuat4d::Identity;
    start.AngularVelocityRadPerS = FVector3d::ZeroVector;
    const FVector3d target(1.0e7, 0.0, 0.0);

    struct FPlayerFlightMeterCase
    {
        const TCHAR* Label;
        FSOLGMeterState Meter;
        double ExpectedLimitG;
    };
    const FPlayerFlightMeterCase cases[] =
    {
        { TEXT("Full meter"), PlayerFlightMeter(1.0), 3.0 },
        { TEXT("Just above critical"), PlayerFlightMeter(0.3), 3.0 },
        { TEXT("Critical meter"), PlayerFlightMeter(0.2), 1.5 },
        { TEXT("At the critical threshold"), PlayerFlightMeter(0.25), 1.5 },
        { TEXT("Blackout"), PlayerFlightMeter(0.5, true, 3.0), 1.5 },
    };
    for (const FPlayerFlightMeterCase& testCase : cases)
    {
        FSOLFlightStepOutput output;
        FSOLGuidanceOutput guidance;
        const FSOLShipState result = SOLFlight::AutopilotStep(start, target, FVector3d::ZeroVector,
            FVector3d::ZeroVector, FVector3d::ZeroVector, params, testCase.Meter, PLAYER_FLIGHT_DT, &output,
            &guidance);
        const double expected = PLAYER_FLIGHT_PLAN_MARGIN * testCase.ExpectedLimitG * PLAYER_FLIGHT_G0;
        TestTrue(*FString::Printf(TEXT("%s: thrust (%.9f, %.9f, %.9f) == %.9f along +X"), testCase.Label,
            output.ThrustAccelWorldMps2.X, output.ThrustAccelWorldMps2.Y, output.ThrustAccelWorldMps2.Z, expected),
            PlayerFlightVecNear(output.ThrustAccelWorldMps2, FVector3d(expected, 0.0, 0.0), 1e-9 * expected));
        TestTrue(*FString::Printf(TEXT("%s: velocity after the step"), testCase.Label),
            PlayerFlightVecNear(result.VelocityMps, FVector3d(expected * PLAYER_FLIGHT_DT, 0.0, 0.0), 1e-9));
        TestTrue(*FString::Printf(TEXT("%s: guidance accelerating"), testCase.Label),
            guidance.Phase == ESOLGuidancePhase::Accelerate);
        TestTrue(*FString::Printf(TEXT("%s: ETA reported"), testCase.Label),
            guidance.EtaS > 0.0 && FMath::IsFinite(guidance.EtaS));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAutopilotGravityTest, "SOLTest.PlayerFlight.AutopilotSumsGravity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The autopilot integrates with gravity: the velocity change is (achieved thrust + gravity) * dt, thrust <= 1.5 G
bool FSOLPlayerFlightAutopilotGravityTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = PlayerFlightParams();
    FSOLShipState state;
    state.PositionM = FVector3d(0.0, 0.0, 1000.0);
    state.VelocityMps = FVector3d(20.0, 0.0, 0.0);
    state.Orientation = FQuat4d::Identity;
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    const FVector3d gravity(0.0, 0.0, -0.5 * PLAYER_FLIGHT_G0);
    const FVector3d target(5.0e4, 0.0, 1000.0);
    double worstError = 0.0;
    double maxThrust = 0.0;
    for (int32 step = 0; step < 600; ++step)
    {
        FSOLFlightStepOutput output;
        const FSOLShipState next = SOLFlight::AutopilotStep(state, target, FVector3d::ZeroVector,
            FVector3d::ZeroVector, gravity, params, PlayerFlightMeter(1.0), PLAYER_FLIGHT_DT, &output, nullptr);
        worstError = FMath::Max(worstError, ((next.VelocityMps - state.VelocityMps)
            - (output.ThrustAccelWorldMps2 + gravity) * PLAYER_FLIGHT_DT).Size());
        maxThrust = FMath::Max(maxThrust, output.ThrustAccelWorldMps2.Size());
        state = next;
    }
    TestTrue(*FString::Printf(TEXT("dv == (thrust + g) * dt, worst %.3e m/s"), worstError), worstError <= 1e-9);
    TestTrue(*FString::Printf(TEXT("Max thrust %.9f G <= 1.5 G"), maxThrust / PLAYER_FLIGHT_G0),
        maxThrust <= PLAYER_FLIGHT_SUSTAINED_MPS2 * (1.0 + PLAYER_FLIGHT_ROUNDING));
    TestTrue(TEXT("Holds altitude against gravity (within 50 m after 20 s)"),
        FMath::Abs(state.PositionM.Z - 1000.0) <= 50.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAutopilotInvalidTest,
    "SOLTest.PlayerFlight.AutopilotInvalidDtUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero, negative or non-finite dt leaves the state unchanged and resets both outputs to their defaults
bool FSOLPlayerFlightAutopilotInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = PlayerFlightParams();
    FSOLShipState start;
    start.PositionM = FVector3d(1.0, 2.0, 3.0);
    start.VelocityMps = FVector3d(4.0, 5.0, 6.0);
    start.Orientation = FQuat4d(FVector3d(1.0, 1.0, 0.0).GetSafeNormal(), 0.4);
    start.AngularVelocityRadPerS = FVector3d(0.1, 0.2, 0.3);
    const double dts[] = { 0.0, -1.0, PlayerFlightNaN(), PlayerFlightInf() };
    for (const double dt : dts)
    {
        FSOLFlightStepOutput output;
        output.ThrustAccelWorldMps2 = FVector3d(1.0, 2.0, 3.0);
        FSOLGuidanceOutput guidance;
        guidance.Phase = ESOLGuidancePhase::Brake;
        guidance.EtaS = 5.0;
        guidance.DistanceM = 7.0;
        guidance.DesiredThrustAccelMps2 = FVector3d(1.0, 1.0, 1.0);
        const FSOLShipState result = SOLFlight::AutopilotStep(start, FVector3d(1.0e6, 0.0, 0.0), FVector3d::ZeroVector,
            FVector3d::ZeroVector, FVector3d(0.0, 0.0, -9.81), params, PlayerFlightMeter(1.0), dt, &output, &guidance);
        const FString label = FString::Printf(TEXT("dt %f"), dt);
        TestTrue(*FString::Printf(TEXT("%s: state unchanged"), *label), result.PositionM == start.PositionM
            && result.VelocityMps == start.VelocityMps && result.Orientation.Equals(start.Orientation, 0.0)
            && result.AngularVelocityRadPerS == start.AngularVelocityRadPerS);
        TestTrue(*FString::Printf(TEXT("%s: thrust output zero"), *label), output.ThrustAccelWorldMps2.IsZero());
        TestTrue(*FString::Printf(TEXT("%s: guidance output default"), *label),
            guidance.Phase == ESOLGuidancePhase::Arrived && guidance.EtaS == 0.0 && guidance.DistanceM == 0.0
            && guidance.DesiredThrustAccelMps2.IsZero() && guidance.DesiredForward.IsZero());
    }

    // Null outputs are allowed
    const FSOLShipState noOutputs = SOLFlight::AutopilotStep(start, FVector3d(1.0e6, 0.0, 0.0), FVector3d::ZeroVector,
        FVector3d::ZeroVector, FVector3d::ZeroVector, params, PlayerFlightMeter(1.0), PLAYER_FLIGHT_DT);
    TestTrue(TEXT("Null outputs: the step still runs"), SOLTestHelpers::IsFiniteVector(noOutputs.VelocityMps));
    return true;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the geometric autopilot standoff from a body's centre (contract: R * (1 + radii) + metres)
    double PlayerFlightGeometricStandoffM(const double radiusM)
    {
        return radiusM * (1.0 + SOL::AUTOPILOT_BODY_STANDOFF_RADII) + SOL::AUTOPILOT_BODY_STANDOFF_M;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance at which a body's gravity is 0.9 of the autopilot limit
    double PlayerFlightGravityStandoffM(const double gm, const double limitMps2)
    {
        return FMath::Sqrt(gm / (PLAYER_FLIGHT_GRAVITY_STANDOFF_FRACTION * limitMps2));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the point-mass gravity of a body at the origin: -GM r / |r|^3
    FVector3d PlayerFlightGravityAt(const FVector3d& positionM, const double gm)
    {
        const double r = positionM.Size();
        return positionM * (-gm / (r * r * r));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the angle in radians between two non-zero vectors (atan2 form, accurate near 0 and PI)
    double PlayerFlightAngle(const FVector3d& a, const FVector3d& b)
    {
        return FMath::Atan2(FVector3d::CrossProduct(a, b).Size(), FVector3d::DotProduct(a, b));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAutopilotStandoffTest, "SOLTest.PlayerFlight.AutopilotStandoff",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The arrival distance is the geometric standoff, moved out until the body's gravity there is at most 0.9 of the
// autopilot limit: Earth keeps the geometric term, the Sun and Jupiter get the gravity term; bad input per contract
bool FSOLPlayerFlightAutopilotStandoffTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("AUTOPILOT_GRAVITY_STANDOFF_FRACTION"), SOL::AUTOPILOT_GRAVITY_STANDOFF_FRACTION,
        PLAYER_FLIGHT_GRAVITY_STANDOFF_FRACTION);
    TestEqual(TEXT("AUTOPILOT_KINEMATIC_ATTITUDE_S"), SOL::AUTOPILOT_KINEMATIC_ATTITUDE_S,
        PLAYER_FLIGHT_KINEMATIC_ATTITUDE_S);
    const double limit = PLAYER_FLIGHT_SUSTAINED_MPS2;

    // Earth: 1.5 G is far above its gravity at 1.25 radii, so the geometric term wins
    const double earthGeometric = PlayerFlightGeometricStandoffM(PLAYER_FLIGHT_EARTH_RADIUS_M);
    TestTrue(TEXT("Earth: geometric term is the larger"), earthGeometric > PlayerFlightGravityStandoffM(
        PLAYER_FLIGHT_EARTH_GM, limit));
    const double earth = SOLFlight::AutopilotStandoffM(PLAYER_FLIGHT_EARTH_RADIUS_M, PLAYER_FLIGHT_EARTH_GM, limit);
    TestTrue(*FString::Printf(TEXT("Earth: %.3f m == geometric %.3f m"), earth, earthGeometric),
        FMath::Abs(earth - earthGeometric) <= 1e-12 * earthGeometric);

    // Sun: the gravity term wins by far, and gravity at the standoff is exactly 0.9 of the limit
    const double sun = SOLFlight::AutopilotStandoffM(PLAYER_FLIGHT_SUN_RADIUS_M, PLAYER_FLIGHT_SUN_GM, limit);
    TestTrue(*FString::Printf(TEXT("Sun: %.6e m beyond the geometric %.6e m"), sun,
        PlayerFlightGeometricStandoffM(PLAYER_FLIGHT_SUN_RADIUS_M)),
        sun > PlayerFlightGeometricStandoffM(PLAYER_FLIGHT_SUN_RADIUS_M));
    const double sunGravity = PLAYER_FLIGHT_SUN_GM / (sun * sun);
    TestTrue(*FString::Printf(TEXT("Sun: gravity at the standoff %.12f == 0.9 x limit %.12f"), sunGravity,
        PLAYER_FLIGHT_GRAVITY_STANDOFF_FRACTION * limit),
        FMath::Abs(sunGravity - PLAYER_FLIGHT_GRAVITY_STANDOFF_FRACTION * limit)
        <= 1e-9 * PLAYER_FLIGHT_GRAVITY_STANDOFF_FRACTION * limit);

    // Jupiter: its cloud-top gravity (~24.8 m/s^2) is above the 1.5 G limit, so the gravity term wins
    TestTrue(TEXT("Jupiter: heavier than the limit at its cloud tops"), PLAYER_FLIGHT_JUPITER_GM
        / (PLAYER_FLIGHT_JUPITER_RADIUS_M * PLAYER_FLIGHT_JUPITER_RADIUS_M) > limit);
    const double jupiter = SOLFlight::AutopilotStandoffM(PLAYER_FLIGHT_JUPITER_RADIUS_M, PLAYER_FLIGHT_JUPITER_GM,
        limit);
    const double jupiterGravityTerm = PlayerFlightGravityStandoffM(PLAYER_FLIGHT_JUPITER_GM, limit);
    TestTrue(TEXT("Jupiter: gravity term is the larger"), jupiterGravityTerm
        > PlayerFlightGeometricStandoffM(PLAYER_FLIGHT_JUPITER_RADIUS_M));
    TestTrue(*FString::Printf(TEXT("Jupiter: %.6e m == gravity term %.6e m"), jupiter, jupiterGravityTerm),
        FMath::Abs(jupiter - jupiterGravityTerm) <= 1e-9 * jupiterGravityTerm);

    // A non-finite or negative radius counts as 0
    const double badNaN = PlayerFlightNaN();
    const double badInf = PlayerFlightInf();
    TestEqual(TEXT("NaN radius, no GM: the metres only"), SOLFlight::AutopilotStandoffM(badNaN, 0.0, limit),
        SOL::AUTOPILOT_BODY_STANDOFF_M);
    TestEqual(TEXT("inf radius, no GM: the metres only"), SOLFlight::AutopilotStandoffM(badInf, 0.0, limit),
        SOL::AUTOPILOT_BODY_STANDOFF_M);
    TestEqual(TEXT("Negative radius, no GM: the metres only"), SOLFlight::AutopilotStandoffM(-5.0e6, 0.0, limit),
        SOL::AUTOPILOT_BODY_STANDOFF_M);
    const double nanRadiusSun = SOLFlight::AutopilotStandoffM(badNaN, PLAYER_FLIGHT_SUN_GM, limit);
    TestTrue(TEXT("NaN radius, Sun GM: the gravity term"), FMath::Abs(nanRadiusSun - PlayerFlightGravityStandoffM(
        PLAYER_FLIGHT_SUN_GM, limit)) <= 1e-9 * nanRadiusSun);

    // A non-finite or non-positive GM or limit: the geometric term only
    const double sunGeometric = PlayerFlightGeometricStandoffM(PLAYER_FLIGHT_SUN_RADIUS_M);
    const double badValues[] = { 0.0, -1.0, badNaN, badInf, -badInf };
    for (const double bad : badValues)
    {
        const double badGm = SOLFlight::AutopilotStandoffM(PLAYER_FLIGHT_SUN_RADIUS_M, bad, limit);
        TestTrue(*FString::Printf(TEXT("GM %f: geometric"), bad), FMath::Abs(badGm - sunGeometric)
            <= 1e-12 * sunGeometric);
        const double badLimit = SOLFlight::AutopilotStandoffM(PLAYER_FLIGHT_SUN_RADIUS_M, PLAYER_FLIGHT_SUN_GM, bad);
        TestTrue(*FString::Printf(TEXT("Limit %f: geometric"), bad), FMath::Abs(badLimit - sunGeometric)
            <= 1e-12 * sunGeometric);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightAutopilotHeavyBodyTest, "SOLTest.PlayerFlight.AutopilotHoldsHeavyBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Flying from rest at twice the Sun's standoff to the standoff point under the Sun's real gravity (default params, full
// meter), the autopilot arrives in bounded time at 1/30 s and at 10 s sim-time steps, never above the pilot limit;
// the geometric standoff could not be held at all (gravity there exceeds the limit)
bool FSOLPlayerFlightAutopilotHeavyBodyTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FSOLFlightParams();
    const FSOLGMeterState full = PlayerFlightMeter(1.0);
    const double limit = SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, full);
    TestTrue(*FString::Printf(TEXT("Default autopilot limit %.6f m/s^2 is positive"), limit), limit > 0.0);
    const double standoff = SOLFlight::AutopilotStandoffM(PLAYER_FLIGHT_SUN_RADIUS_M, PLAYER_FLIGHT_SUN_GM, limit);
    const FVector3d target(standoff, 0.0, 0.0);

    // Bound: twice a flip-and-burn over the whole distance at the guidance's 10% floor budget (gravity at the standoff
    // leaves only 0.1 of the limit for braking)
    const double floorPlan = 0.1 * limit;
    const double bound = 2.0 * (2.0 * FMath::Sqrt(standoff / floorPlan) + PlayerFlightFlipTimeS(params));

    const double dts[] = { PLAYER_FLIGHT_DT, 10.0 };
    for (const double dt : dts)
    {
        FSOLShipState state;
        state.PositionM = FVector3d(2.0 * standoff, 0.0, 0.0);
        state.VelocityMps = FVector3d::ZeroVector;
        state.Orientation = FQuat4d::Identity;
        state.AngularVelocityRadPerS = FVector3d::ZeroVector;
        bool bArrived = false;
        bool bFellIn = false;
        double arrivalS = 0.0;
        double maxThrust = 0.0;
        double closestM = state.PositionM.Size();
        const int64 maxSteps = static_cast<int64>(FMath::CeilToDouble(bound / dt));
        for (int64 step = 0; step < maxSteps; ++step)
        {
            FSOLFlightStepOutput output;
            FSOLGuidanceOutput guidance;
            const FVector3d gravity = PlayerFlightGravityAt(state.PositionM, PLAYER_FLIGHT_SUN_GM);
            state = SOLFlight::AutopilotStep(state, target, FVector3d::ZeroVector, FVector3d::ZeroVector, gravity,
                params, full, dt, &output, &guidance, PlayerFlightGravityAt(target, PLAYER_FLIGHT_SUN_GM).Size());
            maxThrust = FMath::Max(maxThrust, output.ThrustAccelWorldMps2.Size());
            if (guidance.Phase == ESOLGuidancePhase::Arrived)
            {
                bArrived = true;
                arrivalS = step * dt;
                break;
            }
            closestM = FMath::Min(closestM, state.PositionM.Size());
            if (!SOLTestHelpers::IsFiniteVector(state.PositionM) || state.PositionM.Size() < PLAYER_FLIGHT_SUN_RADIUS_M)
            {
                bFellIn = true;
                arrivalS = step * dt;
                break;
            }
        }
        TestFalse(*FString::Printf(TEXT("dt %g: did not fall into the body (at %.0f s)"), dt, arrivalS), bFellIn);
        TestTrue(*FString::Printf(TEXT("dt %g: arrived (at %.0f s, bound %.0f s, closest %.4f standoffs)"), dt,
            arrivalS, bound, closestM / standoff), bArrived);
        TestTrue(*FString::Printf(TEXT("dt %g: max thrust %.9f m/s^2 <= limit %.9f"), dt, maxThrust, limit),
            maxThrust <= limit * (1.0 + PLAYER_FLIGHT_ROUNDING));
    }

    // The geometric standoff is not holdable: the Sun's gravity there exceeds the whole autopilot limit
    const double geometric = PlayerFlightGeometricStandoffM(PLAYER_FLIGHT_SUN_RADIUS_M);
    const double gravityAtGeometric = PLAYER_FLIGHT_SUN_GM / (geometric * geometric);
    TestTrue(*FString::Printf(TEXT("Gravity at the geometric standoff %.3f m/s^2 > limit %.3f m/s^2"),
        gravityAtGeometric, limit), gravityAtGeometric > limit);
    TestTrue(TEXT("The standoff used is beyond the geometric one"), standoff > geometric);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightKinematicAttitudeTest,
    "SOLTest.PlayerFlight.AutopilotKinematicAttitude",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Above 1 s steps (sim-time substeps under warp) the autopilot sets the attitude kinematically: the nose is on the
// guidance's DesiredForward after one step and the spin is zero; at or below 1 s the turn stays rate-limited
bool FSOLPlayerFlightKinematicAttitudeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = PlayerFlightParams();
    FSOLShipState start;
    start.PositionM = FVector3d::ZeroVector;
    start.VelocityMps = FVector3d::ZeroVector;
    start.Orientation = FQuat4d::Identity;
    start.AngularVelocityRadPerS = FVector3d::ZeroVector;

    // Targets 90 degrees off the nose: to the right (a yaw) and straight up (a pitch)
    const FVector3d targets[] = { FVector3d(0.0, 1.0e7, 0.0), FVector3d(0.0, 0.0, 1.0e7) };
    for (const FVector3d& target : targets)
    {
        const FString name = FString::Printf(TEXT("Target (%.0f, %.0f, %.0f)"), target.X, target.Y, target.Z);

        // dt = 60 s: kinematic
        FSOLGuidanceOutput guidance;
        const FSOLShipState after = SOLFlight::AutopilotStep(start, target, FVector3d::ZeroVector,
            FVector3d::ZeroVector, FVector3d::ZeroVector, params, PlayerFlightMeter(1.0), 60.0, nullptr, &guidance);
        const FVector3d nose = after.Orientation.RotateVector(FVector3d(1.0, 0.0, 0.0));
        TestFalse(*FString::Printf(TEXT("%s, 60 s: the guidance wants a direction"), *name),
            guidance.DesiredForward.IsZero());
        TestTrue(*FString::Printf(TEXT("%s, 60 s: DesiredForward toward the target"), *name),
            PlayerFlightAngle(guidance.DesiredForward, target) <= FMath::DegreesToRadians(1.0));
        TestTrue(*FString::Printf(TEXT("%s, 60 s: nose on DesiredForward (%.4f deg)"), *name,
            FMath::RadiansToDegrees(PlayerFlightAngle(nose, guidance.DesiredForward))),
            PlayerFlightAngle(nose, guidance.DesiredForward) <= FMath::DegreesToRadians(1.0));
        TestTrue(*FString::Printf(TEXT("%s, 60 s: no spin left"), *name),
            after.AngularVelocityRadPerS.Size() <= 1e-12);
        const FQuat4d expected = SOLNewtonian::OrientationFromForward(guidance.DesiredForward,
            start.Orientation.GetUpVector(), start.Orientation);
        TestTrue(*FString::Printf(TEXT("%s, 60 s: orientation is OrientationFromForward(forward, up, q)"), *name),
            after.Orientation.AngularDistance(expected) <= 1e-9);

        // dt = 1/30 s: rate-limited, the nose cannot swing 90 degrees in one step
        FSOLGuidanceOutput realTimeGuidance;
        const FSOLShipState realTime = SOLFlight::AutopilotStep(start, target, FVector3d::ZeroVector,
            FVector3d::ZeroVector, FVector3d::ZeroVector, params, PlayerFlightMeter(1.0), PLAYER_FLIGHT_DT, nullptr,
            &realTimeGuidance);
        const double realTimeError = PlayerFlightAngle(realTime.Orientation.RotateVector(FVector3d(1.0, 0.0, 0.0)),
            realTimeGuidance.DesiredForward);
        TestTrue(*FString::Printf(TEXT("%s, 1/30 s: still %.2f deg off (no snap)"), *name,
            FMath::RadiansToDegrees(realTimeError)), realTimeError >= FMath::DegreesToRadians(80.0));
        TestTrue(*FString::Printf(TEXT("%s, 1/30 s: turning (spin non-zero)"), *name),
            realTime.AngularVelocityRadPerS.Size() > 0.0);

        // dt = 1 s exactly: still the rate-limited step (strictly above 1 s is kinematic), which leaves a spin
        const FSOLShipState atThreshold = SOLFlight::AutopilotStep(start, target, FVector3d::ZeroVector,
            FVector3d::ZeroVector, FVector3d::ZeroVector, params, PlayerFlightMeter(1.0),
            PLAYER_FLIGHT_KINEMATIC_ATTITUDE_S);
        TestTrue(*FString::Printf(TEXT("%s, 1 s: rate-limited (spin non-zero)"), *name),
            atThreshold.AngularVelocityRadPerS.Size() > 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlayerFlightPrecisionTest, "SOLTest.PlayerFlight.AutopilotPrecisionReduced",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The precision hint shows above 1 d/s of warp within 10 body radii; false for invalid input
bool FSOLPlayerFlightPrecisionTest::RunTest(const FString& /*parameters*/)
{
    const double radius = 6.371e6;
    const double warp = PLAYER_FLIGHT_HINT_WARP;
    const double hintDistance = PLAYER_FLIGHT_HINT_RADII * radius;      // (not "near": a Windows macro)
    TestTrue(TEXT("Fast warp, 5 radii"), SOLFlight::IsAutopilotPrecisionReduced(1.0e6, 5.0 * radius, radius));
    TestTrue(TEXT("Just above 1 d/s, just inside 10 radii"),
        SOLFlight::IsAutopilotPrecisionReduced(warp + 1.0, hintDistance * 0.999, radius));
    TestFalse(TEXT("Exactly 1 d/s (strict >)"), SOLFlight::IsAutopilotPrecisionReduced(warp, 5.0 * radius, radius));
    TestFalse(TEXT("Exactly 10 radii (strict <)"), SOLFlight::IsAutopilotPrecisionReduced(1.0e6, hintDistance,
        radius));
    TestFalse(TEXT("Far from the body"), SOLFlight::IsAutopilotPrecisionReduced(1.0e6, 100.0 * radius, radius));
    TestFalse(TEXT("Slow warp near the body"), SOLFlight::IsAutopilotPrecisionReduced(1000.0, 2.0 * radius, radius));

    TestFalse(TEXT("Zero radius"), SOLFlight::IsAutopilotPrecisionReduced(1.0e6, 0.0, 0.0));
    TestFalse(TEXT("Negative radius"), SOLFlight::IsAutopilotPrecisionReduced(1.0e6, 1.0, -radius));
    TestFalse(TEXT("NaN warp"), SOLFlight::IsAutopilotPrecisionReduced(PlayerFlightNaN(), radius, radius));
    TestFalse(TEXT("inf warp"), SOLFlight::IsAutopilotPrecisionReduced(PlayerFlightInf(), radius, radius));
    TestFalse(TEXT("NaN distance"), SOLFlight::IsAutopilotPrecisionReduced(1.0e6, PlayerFlightNaN(), radius));
    TestFalse(TEXT("NaN radius"), SOLFlight::IsAutopilotPrecisionReduced(1.0e6, radius, PlayerFlightNaN()));
    TestFalse(TEXT("inf radius"), SOLFlight::IsAutopilotPrecisionReduced(1.0e6, radius, PlayerFlightInf()));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
