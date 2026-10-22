/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// SOLFlight::Step tests, re-validated on 2026-10-21 against the issue #18 (18b) contract in
// Docs/Plans/18-newtonian-flight-core-plan.md: assisted = velocity hold with gravity priority at the pilot's
// sustained G plus a rate command; manual = thrust and torque fractions straight into the Newtonian integrator.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Standard gravity, restated so the tests stay independent
    constexpr double FLIGHT_STEP_G0 = 9.80665;

    // Half the speed of light (the hard speed clamp)
    constexpr double FLIGHT_STEP_HALF_C = 149896229.0;

    // The player's substep length (SOL::SHIP_MAX_SUBSTEP_S), restated
    constexpr double FLIGHT_STEP_DT = 1.0 / 30.0;

    // The baseline pilot's sustained acceleration (1.5 G), the assisted thrust ceiling
    constexpr double FLIGHT_STEP_SUSTAINED_MPS2 = 1.5 * FLIGHT_STEP_G0;

    // Relative slack on "never above" checks (pure rounding)
    constexpr double FLIGHT_STEP_ROUNDING = 1.0e-9;

    // Local rotation axes as used by the contract (X roll, Y pitch, Z yaw)
    enum class EFlightStepAxis : uint8
    {
        Roll,
        Pitch,
        Yaw,
    };

    // Forward, right and up vectors of an orientation in the universe frame
    struct FFlightStepAxes
    {
        FVector3d Forward;
        FVector3d Right;
        FVector3d Up;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double FlightStepNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns explicit flight params (every field set, SDD 18 player values) so default drift cannot break the tests
    FSOLFlightParams FlightStepParams()
    {
        FSOLFlightParams params;
        params.AssistTimeConstantS = 0.75;
        params.BoostSpeedMultiplier = 2.0;
        params.MaxSpeedMps = FLIGHT_STEP_HALF_C;
        params.MinSpeedCapMps = 1.0;
        params.ShipRadiusM = 10.0;

        FSOLThrustEnvelope& env = params.Envelope;
        env.ForwardAccelMps2 = 3.0 * FLIGHT_STEP_G0;
        env.ReverseAccelMps2 = 1.5 * FLIGHT_STEP_G0;
        env.LateralAccelMps2 = 2.0 * FLIGHT_STEP_G0;
        env.VerticalAccelMps2 = 2.0 * FLIGHT_STEP_G0;
        env.BoostMultiplier = 3.0;
        env.MaxTotalAccelMps2 = 9.0 * FLIGHT_STEP_G0;
        env.MaxAngularAccelRadPerS2 = FVector3d(4.0, 3.0, 3.0);
        env.MaxAngularRateRadPerS = FVector3d(2.0943951023931953, 1.5707963267948966, 1.5707963267948966);
        env.MaxSpinRateRadPerS = 2.0 * UE_DOUBLE_PI;
        env.MaxSpeedMps = FLIGHT_STEP_HALF_C;

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
        return params;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a control with the given mode and everything else at rest
    FSOLShipControl FlightStepControl(const bool bAssist)
    {
        FSOLShipControl control;
        control.bFlightAssist = bAssist;
        control.Thrust = FVector3d::ZeroVector;
        control.Rotation = FVector3d::ZeroVector;
        control.bBoost = false;
        control.bStopSpin = false;
        control.SpeedCapMps = 1000.0;
        control.ReferenceVelocityMps = FVector3d::ZeroVector;
        control.AttitudeUpWorld = FVector3d::ZeroVector;
        return control;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the forward/right/up vectors of an orientation (local +X, +Y, +Z rotated into the universe frame)
    FFlightStepAxes FlightStepAxesOf(const FQuat4d& orientation)
    {
        return { orientation.RotateVector(FVector3d(1.0, 0.0, 0.0)), orientation.RotateVector(FVector3d(0.0, 1.0, 0.0)),
            orientation.RotateVector(FVector3d(0.0, 0.0, 1.0)) };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the axes expected after a local rotation by angleRad under the contract's sign conventions
    FFlightStepAxes FlightStepRotatedAxes(const FFlightStepAxes& axes, const EFlightStepAxis axis, const double angleRad)
    {
        const double c = FMath::Cos(angleRad);
        const double s = FMath::Sin(angleRad);
        FFlightStepAxes result = axes;
        switch (axis)
        {
        case EFlightStepAxis::Yaw:      // forward gains +right
            result.Forward = axes.Forward * c + axes.Right * s;
            result.Right = axes.Right * c - axes.Forward * s;
            break;
        case EFlightStepAxis::Pitch:    // forward gains +up
            result.Forward = axes.Forward * c + axes.Up * s;
            result.Up = axes.Up * c - axes.Forward * s;
            break;
        case EFlightStepAxis::Roll:     // up gains +right
            result.Up = axes.Up * c + axes.Right * s;
            result.Right = axes.Right * c - axes.Up * s;
            break;
        }
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when all three axes match within an absolute tolerance
    bool FlightStepAxesNear(const FFlightStepAxes& actual, const FFlightStepAxes& expected, const double tolerance)
    {
        return (actual.Forward - expected.Forward).Size() <= tolerance
            && (actual.Right - expected.Right).Size() <= tolerance
            && (actual.Up - expected.Up).Size() <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the angle between two vectors via atan2 (accurate for small and large angles)
    double FlightStepAngleBetween(const FVector3d& a, const FVector3d& b)
    {
        return FMath::Atan2(FVector3d::CrossProduct(a, b).Size(), FVector3d::DotProduct(a, b));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an orientation pitched nose-up by pitchRad from identity: forward (cos p, 0, sin p), right +Y
    FQuat4d FlightStepPitchedOrientation(const double pitchRad)
    {
        return FQuat4d(FVector3d(0.0, 1.0, 0.0), -pitchRad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an orientation rolled right by rollRad from identity: up (0, sin r, cos r), forward +X
    FQuat4d FlightStepRolledOrientation(const double rollRad)
    {
        return FQuat4d(FVector3d(1.0, 0.0, 0.0), -rollRad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the commanded local angular velocity for an assisted rotation input (input times the max rates)
    FVector3d FlightStepCommandedRate(const FVector3d& rotationInput, const FSOLFlightParams& params)
    {
        const FVector3d& rate = params.Envelope.MaxAngularRateRadPerS;
        return FVector3d(rotationInput.X * rate.X, rotationInput.Y * rate.Y, rotationInput.Z * rate.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Steps once with the angular velocity already at the commanded rate, so the turn is exactly rate * dt: assisted
    // holds the commanded rate (zero torque), manual coasts with no input (torque-free spin)
    FSOLShipState FlightStepSteadyRotate(FSOLShipState state, const FVector3d& rotationInput, const double dt,
        const bool bAssist)
    {
        const FSOLFlightParams params = FlightStepParams();
        FSOLShipControl control = FlightStepControl(bAssist);
        control.Rotation = bAssist ? rotationInput : FVector3d::ZeroVector;
        state.AngularVelocityRadPerS = FlightStepCommandedRate(rotationInput, params);
        return SOLFlight::Step(state, control, params, FVector3d::ZeroVector, dt);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rotation input vector that drives only one axis at full deflection
    FVector3d FlightStepAxisInput(const EFlightStepAxis axis, const double sign)
    {
        switch (axis)
        {
        case EFlightStepAxis::Roll:
            return FVector3d(sign, 0.0, 0.0);
        case EFlightStepAxis::Pitch:
            return FVector3d(0.0, sign, 0.0);
        default:
            return FVector3d(0.0, 0.0, sign);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the max angular acceleration of one local axis
    double FlightStepAlpha(const FSOLFlightParams& params, const EFlightStepAxis axis)
    {
        const FVector3d& alpha = params.Envelope.MaxAngularAccelRadPerS2;
        switch (axis)
        {
        case EFlightStepAxis::Roll:
            return alpha.X;
        case EFlightStepAxis::Pitch:
            return alpha.Y;
        default:
            return alpha.Z;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship state with a non-trivial position and the given velocity and orientation
    FSOLShipState FlightStepMakeState(const FVector3d& velocity, const FQuat4d& orientation)
    {
        FSOLShipState state;
        state.PositionM = FVector3d(1.0e7, -2.0e7, 3.0e6);
        state.VelocityMps = velocity;
        state.Orientation = orientation;
        state.AngularVelocityRadPerS = FVector3d::ZeroVector;
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs a number of identical steps and returns the final state; outMaxThrust gets the largest achieved thrust
    FSOLShipState FlightStepRun(FSOLShipState state, const FSOLShipControl& control, const FSOLFlightParams& params,
        const FVector3d& gravity, const double dt, const int32 steps, double& outMaxThrust)
    {
        outMaxThrust = 0.0;
        for (int32 step = 0; step < steps; ++step)
        {
            FSOLFlightStepOutput output;
            state = SOLFlight::Step(state, control, params, gravity, dt, &output);
            outMaxThrust = FMath::Max(outMaxThrust, output.ThrustAccelWorldMps2.Size());
        }
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a uniform random double in [minValue, maxValue)
    double FlightStepRange(const FRandomStream& stream, const double minValue, const double maxValue)
    {
        return minValue + (maxValue - minValue) * static_cast<double>(stream.FRand());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a random vector with components in [-extent, extent)
    FVector3d FlightStepRandomVector(const FRandomStream& stream, const double extent)
    {
        return FVector3d(FlightStepRange(stream, -extent, extent), FlightStepRange(stream, -extent, extent),
            FlightStepRange(stream, -extent, extent));
    }

    // A yaw of +90 degrees from identity: forward is +Y, right is -X, up is +Z
    const FQuat4d FLIGHT_STEP_YAW90(FVector3d(0.0, 0.0, 1.0), UE_DOUBLE_HALF_PI);
}

// --- Params and common behavior ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepDefaultsTest, "SOLTest.FlightStep.DefaultParamsMatchContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The default FSOLFlightParams carry the 18b values: 0.75 s assist, player envelope, baseline pilot, boost x2 speed
bool FSOLFlightStepDefaultsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    TestEqual(TEXT("AssistTimeConstantS"), params.AssistTimeConstantS, 0.75, 1e-15);
    TestEqual(TEXT("BoostSpeedMultiplier"), params.BoostSpeedMultiplier, 2.0, 1e-15);
    TestEqual(TEXT("MaxSpeedMps is 0.5c"), params.MaxSpeedMps, FLIGHT_STEP_HALF_C, 1e-6);
    TestEqual(TEXT("MinSpeedCapMps"), params.MinSpeedCapMps, 1.0, 1e-15);
    TestEqual(TEXT("ShipRadiusM"), params.ShipRadiusM, 10.0, 1e-15);
    TestEqual(TEXT("Forward 3 G"), params.Envelope.ForwardAccelMps2, 3.0 * FLIGHT_STEP_G0, 1e-12);
    TestEqual(TEXT("Reverse 1.5 G"), params.Envelope.ReverseAccelMps2, 1.5 * FLIGHT_STEP_G0, 1e-12);
    TestEqual(TEXT("Lateral 2 G"), params.Envelope.LateralAccelMps2, 2.0 * FLIGHT_STEP_G0, 1e-12);
    TestEqual(TEXT("Vertical 2 G"), params.Envelope.VerticalAccelMps2, 2.0 * FLIGHT_STEP_G0, 1e-12);
    TestEqual(TEXT("Boost x3"), params.Envelope.BoostMultiplier, 3.0, 1e-15);
    TestEqual(TEXT("Total cap 9 G"), params.Envelope.MaxTotalAccelMps2, 9.0 * FLIGHT_STEP_G0, 1e-12);
    TestEqual(TEXT("Pilot sustained 1.5 G"), params.PilotLimits.SustainedG, 1.5, 1e-15);
    TestEqual(TEXT("Pilot max 9 G"), params.PilotLimits.MaxG, 9.0, 1e-15);

    // The control defaults: assisted, no stop spin, no attitude target
    const FSOLShipControl control;
    TestTrue(TEXT("Assisted by default"), control.bFlightAssist);
    TestFalse(TEXT("No stop spin by default"), control.bStopSpin);
    TestTrue(TEXT("No attitude up by default"), control.AttitudeUpWorld.IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepZeroDtTest, "SOLTest.FlightStep.ZeroDtLeavesStateUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero, negative or non-finite time step changes nothing in either mode and zeroes the output
bool FSOLFlightStepZeroDtTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipState start = FlightStepMakeState(FVector3d(12.0, -34.0, 56.0), FQuat4d(FVector3d(1.0, 2.0, 3.0)
        .GetSafeNormal(), 0.8));
    start.AngularVelocityRadPerS = FVector3d(0.1, -0.2, 0.3);
    const double dts[] = { 0.0, -0.1, FlightStepNaN(), std::numeric_limits<double>::infinity() };
    for (const bool bAssist : { true, false })
    {
        for (const double dt : dts)
        {
            FSOLShipControl control = FlightStepControl(bAssist);
            control.Thrust = FVector3d(1.0, 0.5, -0.5);
            control.Rotation = FVector3d(1.0, -1.0, 0.5);
            control.bBoost = true;
            control.AttitudeUpWorld = FVector3d(0.0, 0.0, 1.0);
            FSOLFlightStepOutput output;
            output.ThrustAccelWorldMps2 = FVector3d(1.0, 2.0, 3.0);     // must be reset
            const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d(0.0, 0.0, -9.81), dt,
                &output);
            const FString label = FString::Printf(TEXT("%s, dt %f"), bAssist ? TEXT("Assist") : TEXT("Manual"), dt);
            TestTrue(*FString::Printf(TEXT("%s: position unchanged"), *label), result.PositionM == start.PositionM);
            TestTrue(*FString::Printf(TEXT("%s: velocity unchanged"), *label),
                (result.VelocityMps - start.VelocityMps).Size() <= 1e-12);
            TestTrue(*FString::Printf(TEXT("%s: orientation unchanged"), *label),
                result.Orientation.Equals(start.Orientation, 1e-12));
            TestTrue(*FString::Printf(TEXT("%s: angular velocity unchanged"), *label),
                (result.AngularVelocityRadPerS - start.AngularVelocityRadPerS).Size() <= 1e-12);
            TestTrue(*FString::Printf(TEXT("%s: output thrust zero"), *label),
                output.ThrustAccelWorldMps2.IsZero());
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepSemiImplicitTest, "SOLTest.FlightStep.PositionUsesUpdatedVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Position advances by the post-update velocity times dt (semi-implicit Euler) in both modes
bool FSOLFlightStepSemiImplicitTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const double dt = 0.25;
    for (const bool bAssist : { true, false })
    {
        FSOLShipControl control = FlightStepControl(bAssist);
        control.Thrust = FVector3d(1.0, 0.0, 0.0);
        const FSOLShipState start = FlightStepMakeState(FVector3d(100.0, -20.0, 5.0), FQuat4d::Identity);
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d(0.0, 0.0, -2.0), dt);
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Manual");
        TestFalse(*FString::Printf(TEXT("%s: velocity changed"), mode),
            (result.VelocityMps - start.VelocityMps).IsNearlyZero(1e-6));
        TestTrue(*FString::Printf(TEXT("%s: position == start + new velocity * dt"), mode),
            (result.PositionM - (start.PositionM + result.VelocityMps * dt)).Size() <= 1e-6);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepThrustPlusGravityTest,
    "SOLTest.FlightStep.VelocityChangeIsThrustPlusGravityBothModes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// In both modes the velocity change per step is (reported thrust + gravity) * dt: gravity is always summed, no damping
bool FSOLFlightStepThrustPlusGravityTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FRandomStream stream(20261021);
    for (const bool bAssist : { true, false })
    {
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Manual");
        FSOLShipState state = FlightStepMakeState(FVector3d(300.0, -200.0, 50.0), FQuat4d::Identity);
        double worstError = 0.0;
        for (int32 step = 0; step < 600; ++step)
        {
            // Random inputs and gravity each step (speeds stay far below the 0.5c clamp)
            FSOLShipControl control = FlightStepControl(bAssist);
            control.Thrust = FlightStepRandomVector(stream, 1.0);
            control.Rotation = FlightStepRandomVector(stream, 1.0);
            control.bBoost = stream.FRand() < 0.3f;
            control.SpeedCapMps = FlightStepRange(stream, 1.0, 2000.0);
            control.ReferenceVelocityMps = FlightStepRandomVector(stream, 500.0);
            const FVector3d gravity = FlightStepRandomVector(stream, 2.0 * FLIGHT_STEP_G0);
            FSOLFlightStepOutput output;
            const FSOLShipState next = SOLFlight::Step(state, control, params, gravity, FLIGHT_STEP_DT, &output);
            const FVector3d expectedDv = (output.ThrustAccelWorldMps2 + gravity) * FLIGHT_STEP_DT;
            worstError = FMath::Max(worstError, ((next.VelocityMps - state.VelocityMps) - expectedDv).Size());
            state = next;
        }
        TestTrue(*FString::Printf(TEXT("%s: dv == (thrust + g) * dt, worst error %.3e m/s"), mode, worstError),
            worstError <= 1e-6);
    }

    // With no thrust and no gravity the velocity persists in both modes (assisted at its reference: no drag)
    for (const bool bAssist : { true, false })
    {
        FSOLShipControl control = FlightStepControl(bAssist);
        control.ReferenceVelocityMps = FVector3d(1234.0, -567.0, 89.0);
        const FSOLShipState start = FlightStepMakeState(control.ReferenceVelocityMps, FLIGHT_STEP_YAW90);
        double maxThrust = 0.0;
        const FSOLShipState coasting = FlightStepRun(start, control, params, FVector3d::ZeroVector, 0.1, 1000,
            maxThrust);
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Manual");
        TestTrue(*FString::Printf(TEXT("%s: velocity persists"), mode),
            (coasting.VelocityMps - start.VelocityMps).Size() <= 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: no thrust used"), mode), maxThrust <= 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: position advanced by v * 100 s"), mode),
            (coasting.PositionM - (start.PositionM + start.VelocityMps * 100.0)).Size() <= 1e-5);
    }
    return true;
}

// --- Manual ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualAxesTest, "SOLTest.FlightStep.ManualAccelerationsAlongShipAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Manual thrust gives 3 G forward, 1.5 G reverse and 2 G lateral/vertical along the ship's axes (ship yawed 90 deg)
bool FSOLFlightStepManualAxesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const double dt = 2.0;
    const double g = FLIGHT_STEP_G0;
    const FVector3d v0(7.0, -3.0, 1.0);
    const FSOLShipState start = FlightStepMakeState(v0, FLIGHT_STEP_YAW90);

    // After the yaw: forward +Y, right -X, up +Z
    struct FFlightStepManualCase
    {
        FVector3d Thrust;
        FVector3d ExpectedAccel;
    };
    const FFlightStepManualCase cases[] =
    {
        { FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 3.0 * g, 0.0) },
        { FVector3d(-1.0, 0.0, 0.0), FVector3d(0.0, -1.5 * g, 0.0) },
        { FVector3d(0.5, 0.0, 0.0), FVector3d(0.0, 1.5 * g, 0.0) },
        { FVector3d(0.0, 1.0, 0.0), FVector3d(-2.0 * g, 0.0, 0.0) },
        { FVector3d(0.0, -1.0, 0.0), FVector3d(2.0 * g, 0.0, 0.0) },
        { FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, 2.0 * g) },
        { FVector3d(0.0, 0.0, -0.5), FVector3d(0.0, 0.0, -1.0 * g) },
        { FVector3d(2.0, 0.0, 0.0), FVector3d(0.0, 3.0 * g, 0.0) },          // inputs clamp to [-1, 1]
    };
    for (const FFlightStepManualCase& testCase : cases)
    {
        FSOLShipControl control = FlightStepControl(false);
        control.Thrust = testCase.Thrust;
        FSOLFlightStepOutput output;
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt, &output);
        const FVector3d expectedVelocity = v0 + testCase.ExpectedAccel * dt;
        const FString label = FString::Printf(TEXT("Thrust (%.1f, %.1f, %.1f)"), testCase.Thrust.X, testCase.Thrust.Y,
            testCase.Thrust.Z);
        TestTrue(*FString::Printf(TEXT("%s: velocity (%.6f, %.6f, %.6f) == (%.6f, %.6f, %.6f)"), *label,
            result.VelocityMps.X, result.VelocityMps.Y, result.VelocityMps.Z, expectedVelocity.X, expectedVelocity.Y,
            expectedVelocity.Z), (result.VelocityMps - expectedVelocity).Size() <= 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: output is the world thrust"), *label),
            (output.ThrustAccelWorldMps2 - testCase.ExpectedAccel).Size() <= 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: position semi-implicit"), *label),
            (result.PositionM - (start.PositionM + expectedVelocity * dt)).Size() <= 1e-6);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualDiagonalTest,
    "SOLTest.FlightStep.ManualDiagonalThrustNotUnitClamped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A manual diagonal burn fires several thrusters at full (no unit-length clamp), so it beats any single axis
bool FSOLFlightStepManualDiagonalTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const double g = FLIGHT_STEP_G0;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    // (1, 1, 0): 3 G forward + 2 G right = sqrt(13) G ~ 3.61 G (a unit clamp would give at most 3 G)
    FSOLShipControl control = FlightStepControl(false);
    control.Thrust = FVector3d(1.0, 1.0, 0.0);
    FSOLFlightStepOutput output;
    SOLFlight::Step(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT, &output);
    TestTrue(TEXT("(1, 1, 0) gives (3 G, 2 G, 0)"),
        (output.ThrustAccelWorldMps2 - FVector3d(3.0 * g, 2.0 * g, 0.0)).Size() <= 1e-9);

    // (1, 1, 1): sqrt(17) G ~ 4.12 G, under the 9 G cap
    control.Thrust = FVector3d(1.0, 1.0, 1.0);
    SOLFlight::Step(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT, &output);
    TestTrue(TEXT("(1, 1, 1) gives (3 G, 2 G, 2 G)"),
        (output.ThrustAccelWorldMps2 - FVector3d(3.0 * g, 2.0 * g, 2.0 * g)).Size() <= 1e-9);
    TestTrue(TEXT("Diagonal is longer than the forward axis"), output.ThrustAccelWorldMps2.Size() > 3.0 * g);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualBoostTest,
    "SOLTest.FlightStep.ManualBoostTriplesThrustWithin9GCap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Boost triples every manual axis (9 G forward = 88.26 m/s^2), and the 9 G total cap scales longer diagonals down
bool FSOLFlightStepManualBoostTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const double g = FLIGHT_STEP_G0;
    const double cap = 9.0 * g;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    FSOLShipControl control = FlightStepControl(false);
    control.bBoost = true;

    struct FFlightStepBoostCase
    {
        FVector3d Thrust;
        FVector3d ExpectedAccel;
    };
    const FVector3d boostedDiagonal = FVector3d(9.0, 6.0, 6.0).GetSafeNormal() * cap;     // |(9, 6, 6)| G > 9 G
    const FFlightStepBoostCase cases[] =
    {
        { FVector3d(1.0, 0.0, 0.0), FVector3d(cap, 0.0, 0.0) },             // exactly at the cap: 88.25985 m/s^2
        { FVector3d(-1.0, 0.0, 0.0), FVector3d(-4.5 * g, 0.0, 0.0) },
        { FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 6.0 * g, 0.0) },
        { FVector3d(0.0, 0.0, -1.0), FVector3d(0.0, 0.0, -6.0 * g) },
        { FVector3d(1.0, 1.0, 1.0), boostedDiagonal },
    };
    for (const FFlightStepBoostCase& testCase : cases)
    {
        control.Thrust = testCase.Thrust;
        FSOLFlightStepOutput output;
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, 1.0, &output);
        const FString label = FString::Printf(TEXT("Boosted (%.0f, %.0f, %.0f)"), testCase.Thrust.X, testCase.Thrust.Y,
            testCase.Thrust.Z);
        TestTrue(*FString::Printf(TEXT("%s: thrust (%.6f, %.6f, %.6f)"), *label, output.ThrustAccelWorldMps2.X,
            output.ThrustAccelWorldMps2.Y, output.ThrustAccelWorldMps2.Z),
            (output.ThrustAccelWorldMps2 - testCase.ExpectedAccel).Size() <= 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: velocity after 1 s"), *label),
            (result.VelocityMps - testCase.ExpectedAccel).Size() <= 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: never above 9 G"), *label),
            output.ThrustAccelWorldMps2.Size() <= cap * (1.0 + FLIGHT_STEP_ROUNDING));
    }
    TestEqual(TEXT("9 G forward is 88.26 m/s^2"), cap, 88.25985, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualGravityTest, "SOLTest.FlightStep.ManualAddsGravityNoDamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Gravity adds to the velocity in manual mode, the cap and reference are ignored, and nothing damps the velocity
bool FSOLFlightStepManualGravityTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipControl control = FlightStepControl(false);
    control.SpeedCapMps = 10.0;                     // cap and reference are ignored in manual mode
    control.ReferenceVelocityMps = FVector3d(1.0e4, 0.0, 0.0);
    const FVector3d v0(1234.0, -567.0, 89.0);
    const FSOLShipState start = FlightStepMakeState(v0, FLIGHT_STEP_YAW90);

    // Gravity alone
    const FVector3d gravity(1.0, 2.0, -3.0);
    const FSOLShipState falling = SOLFlight::Step(start, control, params, gravity, 2.0);
    TestTrue(TEXT("Gravity adds g * dt"), (falling.VelocityMps - (v0 + gravity * 2.0)).Size() <= 1e-9);

    // Gravity plus forward thrust (forward is +Y)
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    const FSOLShipState thrusting = SOLFlight::Step(start, control, params, gravity, 2.0);
    TestTrue(TEXT("Thrust and gravity add"), (thrusting.VelocityMps
        - (v0 + (gravity + FVector3d(0.0, 3.0 * FLIGHT_STEP_G0, 0.0)) * 2.0)).Size() <= 1e-9);

    // No damping over many steps
    control.Thrust = FVector3d::ZeroVector;
    double maxThrust = 0.0;
    const FSOLShipState coasting = FlightStepRun(start, control, params, FVector3d::ZeroVector, 0.1, 1000, maxThrust);
    TestTrue(TEXT("Velocity persists with zero thrust"), (coasting.VelocityMps - v0).Size() <= 1e-9);
    TestTrue(TEXT("Position advanced by v0 * 100 s"), (coasting.PositionM - (start.PositionM + v0 * 100.0)).Size()
        <= 1e-5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualMaxSpeedTest, "SOLTest.FlightStep.ManualClampsToMaxSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Manual velocity above 0.5c is scaled down to the max speed with its direction kept
bool FSOLFlightStepManualMaxSpeedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipControl control = FlightStepControl(false);
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    control.bBoost = true;

    // Just below the limit, then one boosted second (+88 m/s) pushes past it
    const FSOLShipState nearLimit = FlightStepMakeState(FVector3d(FLIGHT_STEP_HALF_C - 10.0, 0.0, 0.0),
        FQuat4d::Identity);
    const FSOLShipState result = SOLFlight::Step(nearLimit, control, params, FVector3d::ZeroVector, 1.0);
    TestTrue(*FString::Printf(TEXT("Clamped to the max speed: %.6f"), result.VelocityMps.X),
        FMath::Abs(result.VelocityMps.Size() - FLIGHT_STEP_HALF_C) <= 1e-6);
    TestTrue(TEXT("Direction kept"), SOLTestHelpers::VectorsNear(result.VelocityMps.GetSafeNormal(),
        FVector3d(1.0, 0.0, 0.0), 1e-12));

    // An over-limit diagonal velocity is scaled down along its own direction
    control.Thrust = FVector3d::ZeroVector;
    const FVector3d overDir = FVector3d(1.0, -2.0, 2.0) / 3.0;
    const FSOLShipState over = FlightStepMakeState(overDir * (2.0 * FLIGHT_STEP_HALF_C), FQuat4d::Identity);
    const FSOLShipState scaled = SOLFlight::Step(over, control, params, FVector3d::ZeroVector, 0.1);
    TestTrue(TEXT("Over-limit velocity scaled to the max speed along its direction"),
        SOLTestHelpers::VectorsNear(scaled.VelocityMps, overDir * FLIGHT_STEP_HALF_C, 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualOrientTest, "SOLTest.FlightStep.ManualThrustFollowsRotatedShip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After the ship yaws 90 degrees right through Step, forward thrust accelerates along the new forward (+Y)
bool FSOLFlightStepManualOrientTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    // Coast at 90 deg/s of yaw for 10 steps of 0.1 s = 90 degrees (manual, no input: torque-free spin)
    for (int32 step = 0; step < 10; ++step)
    {
        state = FlightStepSteadyRotate(state, FVector3d(0.0, 0.0, 1.0), 0.1, false);
    }
    TestTrue(TEXT("Forward is +Y after the yaw"), SOLTestHelpers::VectorsNear(
        state.Orientation.RotateVector(FVector3d(1.0, 0.0, 0.0)), FVector3d(0.0, 1.0, 0.0), 1e-9));

    // Stop rotating, then thrust forward for one second
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    FSOLShipControl control = FlightStepControl(false);
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    const FSOLShipState result = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 1.0);
    TestTrue(*FString::Printf(TEXT("Velocity (%.6f, %.6f, %.6f) == (0, 3 G, 0)"), result.VelocityMps.X,
        result.VelocityMps.Y, result.VelocityMps.Z),
        (result.VelocityMps - FVector3d(0.0, 3.0 * FLIGHT_STEP_G0, 0.0)).Size() <= 1e-6);
    return true;
}

// --- Assisted ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistSustainedTest, "SOLTest.FlightStep.AssistNeverExceedsSustainedG",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Whatever the inputs (boost, big caps, strong gravity), the assisted achieved thrust never exceeds 1.5 G
bool FSOLFlightStepAssistSustainedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FRandomStream stream(18018);
    FSOLShipState state = FlightStepMakeState(FVector3d(5000.0, 0.0, 0.0), FQuat4d(FVector3d(0.3, -0.4, 0.8)
        .GetSafeNormal(), 1.1));
    double maxThrust = 0.0;
    bool bFinite = true;
    for (int32 step = 0; step < 3000; ++step)
    {
        // Change the inputs every 30 steps, so the hold both saturates and settles
        FSOLShipControl control = FlightStepControl(true);
        FRandomStream inputStream(step / 30);
        control.Thrust = FlightStepRandomVector(inputStream, 1.0);
        control.Rotation = FlightStepRandomVector(inputStream, 1.0);
        control.bBoost = inputStream.FRand() < 0.5f;
        control.SpeedCapMps = FlightStepRange(inputStream, 1.0, 1.0e5);
        control.ReferenceVelocityMps = FlightStepRandomVector(inputStream, 1.0e4);
        control.AttitudeUpWorld = inputStream.FRand() < 0.5f ? FVector3d(0.0, 0.0, 1.0) : FVector3d::ZeroVector;
        const FVector3d gravity = FlightStepRandomVector(stream, 3.0 * FLIGHT_STEP_G0);
        FSOLFlightStepOutput output;
        state = SOLFlight::Step(state, control, params, gravity, FLIGHT_STEP_DT, &output);
        maxThrust = FMath::Max(maxThrust, output.ThrustAccelWorldMps2.Size());
        bFinite = bFinite && SOLTestHelpers::IsFiniteVector(state.VelocityMps)
            && SOLTestHelpers::IsFiniteVector(output.ThrustAccelWorldMps2);
    }
    TestTrue(TEXT("State and output stay finite"), bFinite);
    TestTrue(*FString::Printf(TEXT("Max assisted thrust %.9f G <= 1.5 G"), maxThrust / FLIGHT_STEP_G0),
        maxThrust <= FLIGHT_STEP_SUSTAINED_MPS2 * (1.0 + FLIGHT_STEP_ROUNDING));
    TestTrue(TEXT("The assist actually used its full 1.5 G at some point"),
        maxThrust >= FLIGHT_STEP_SUSTAINED_MPS2 * (1.0 - 1e-6));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistBrakeTest,
    "SOLTest.FlightStep.AssistBrakesToReferenceAtSustainedG",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With no input, 1000 m/s relative to the reference brakes at exactly 1.5 G (~68 s), then settles on the reference
bool FSOLFlightStepAssistBrakeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipControl control = FlightStepControl(true);
    control.ReferenceVelocityMps = FVector3d(5.0, -3.0, 2.0);
    const FVector3d relativeDir = FVector3d(3.0, -4.0, 12.0) / 13.0;
    const double relative0 = 1000.0;
    FSOLShipState state = FlightStepMakeState(control.ReferenceVelocityMps + relativeDir * relative0,
        FLIGHT_STEP_YAW90);

    // While the hold demand (relative / 0.75 s) exceeds 1.5 G, every step removes exactly 1.5 G * dt along the
    // relative velocity; the linear phase lasts until relative <= 1.5 G * 0.75 s ~ 11 m/s, i.e. ~67 s
    double firstBelowOneS = -1.0;
    bool bBoundedDecel = true;
    bool bStraight = true;
    const int32 steps = FMath::RoundToInt32(80.0 / FLIGHT_STEP_DT);
    for (int32 step = 1; step <= steps; ++step)
    {
        const FSOLShipState before = state;
        FSOLFlightStepOutput output;
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT, &output);
        bBoundedDecel = bBoundedDecel && (state.VelocityMps - before.VelocityMps).Size()
            <= FLIGHT_STEP_SUSTAINED_MPS2 * FLIGHT_STEP_DT * (1.0 + FLIGHT_STEP_ROUNDING);
        const FVector3d relative = state.VelocityMps - control.ReferenceVelocityMps;
        bStraight = bStraight && FVector3d::CrossProduct(relative, relativeDir).Size() <= 1e-9 * relative0;
        if (step == FMath::RoundToInt32(60.0 / FLIGHT_STEP_DT))
        {
            const double expected = relative0 - 60.0 * FLIGHT_STEP_SUSTAINED_MPS2;   // 117.4015 m/s
            TestTrue(*FString::Printf(TEXT("After 60 s: relative %.6f == %.6f"), relative.Size(), expected),
                FMath::Abs(relative.Size() - expected) <= 1e-6);
        }
        if (firstBelowOneS < 0.0 && relative.Size() < 1.0)
        {
            firstBelowOneS = step * FLIGHT_STEP_DT;
        }
    }
    TestTrue(TEXT("Each step changes the velocity by at most 1.5 G * dt"), bBoundedDecel);
    TestTrue(TEXT("Braking stays along the relative velocity"), bStraight);

    // Physical floor: 999 m/s at 1.5 G is 67.9 s; the tail from ~11 m/s to 1 m/s at tau 0.75 s adds ~1.8 s
    TestTrue(*FString::Printf(TEXT("Below 1 m/s after %.3f s, within [67.9, 71] s"), firstBelowOneS),
        firstBelowOneS >= 67.9 && firstBelowOneS <= 71.0);
    TestTrue(TEXT("Settled on the reference after 80 s"),
        (state.VelocityMps - control.ReferenceVelocityMps).Size() <= 1e-3);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistThrustTest,
    "SOLTest.FlightStep.AssistThrustTargetsCapAlongShipAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Assisted thrust targets reference + cap * input along the ship's axes: proportional below 1.5 G, saturated above
bool FSOLFlightStepAssistThrustTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const double tau = params.AssistTimeConstantS;
    const double dt = 0.1;      // shorter than tau, so tau sets the hold's divisor
    const FVector3d reference(10.0, 20.0, 30.0);
    const FSOLShipState start = FlightStepMakeState(reference, FLIGHT_STEP_YAW90);

    // Small cap (1 m/s): the hold demand cap / tau ~ 1.33 m/s^2 is under 1.5 G, so dv = (vT - v) * dt / tau exactly.
    // After the yaw: forward +Y, right -X, up +Z
    struct FFlightStepAssistCase
    {
        FVector3d Thrust;
        FVector3d Direction;
    };
    const FFlightStepAssistCase cases[] =
    {
        { FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0) },
        { FVector3d(-1.0, 0.0, 0.0), FVector3d(0.0, -1.0, 0.0) },
        { FVector3d(0.0, 1.0, 0.0), FVector3d(-1.0, 0.0, 0.0) },
        { FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, 1.0) },
        { FVector3d(0.0, 0.0, -0.5), FVector3d(0.0, 0.0, -0.5) },
    };
    for (const FFlightStepAssistCase& testCase : cases)
    {
        FSOLShipControl control = FlightStepControl(true);
        control.Thrust = testCase.Thrust;
        control.SpeedCapMps = 1.0;
        control.ReferenceVelocityMps = reference;
        FSOLFlightStepOutput output;
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt, &output);
        const FVector3d target = reference + testCase.Direction * control.SpeedCapMps;
        const FVector3d expectedAccel = (target - start.VelocityMps) / tau;
        TestTrue(*FString::Printf(TEXT("Thrust (%.1f, %.1f, %.1f): proportional hold along the ship axes"),
            testCase.Thrust.X, testCase.Thrust.Y, testCase.Thrust.Z),
            (result.VelocityMps - (start.VelocityMps + expectedAccel * dt)).Size() <= 1e-9);
        TestTrue(TEXT("Output matches the hold"), (output.ThrustAccelWorldMps2 - expectedAccel).Size() <= 1e-9);
    }

    // Big cap (1000 m/s): the demand saturates, so the ship accelerates along forward (+Y) at exactly 1.5 G
    FSOLShipControl control = FlightStepControl(true);
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    control.SpeedCapMps = 1000.0;
    control.ReferenceVelocityMps = reference;
    double maxThrust = 0.0;
    const FSOLShipState after30 = FlightStepRun(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT,
        FMath::RoundToInt32(30.0 / FLIGHT_STEP_DT), maxThrust);
    const FVector3d expected30 = reference + FVector3d(0.0, 30.0 * FLIGHT_STEP_SUSTAINED_MPS2, 0.0);
    TestTrue(*FString::Printf(TEXT("After 30 s: (%.6f, %.6f, %.6f) == reference + 30 s * 1.5 G forward"),
        after30.VelocityMps.X, after30.VelocityMps.Y, after30.VelocityMps.Z),
        (after30.VelocityMps - expected30).Size() <= 1e-6);
    TestTrue(TEXT("Never above 1.5 G"), maxThrust <= FLIGHT_STEP_SUSTAINED_MPS2 * (1.0 + FLIGHT_STEP_ROUNDING));

    // Long run converges on reference + cap * forward (1000 m/s at 1.5 G is ~68 s, plus the settling tail)
    const FSOLShipState after100 = FlightStepRun(after30, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT,
        FMath::RoundToInt32(70.0 / FLIGHT_STEP_DT), maxThrust);
    TestTrue(TEXT("Converges to reference + cap * forward"),
        (after100.VelocityMps - (reference + FVector3d(0.0, 1000.0, 0.0))).Size() <= 1e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistClampTest, "SOLTest.FlightStep.AssistThrustClampedToUnitLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An assisted diagonal input is clamped to unit length, so its target speed is the cap; shorter inputs are kept
bool FSOLFlightStepAssistClampTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipControl control = FlightStepControl(true);
    control.SpeedCapMps = 50.0;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    // 50 m/s at 1.5 G takes ~3.4 s; 30 s leaves the 0.75 s settling tail far below 1e-6
    const int32 steps = FMath::RoundToInt32(30.0 / FLIGHT_STEP_DT);
    double maxThrust = 0.0;

    // (1, 1, 1) and (1, 1, 0) end at exactly the cap along the normalized direction, not cap * sqrt(3 or 2)
    const FVector3d diagonals[] = { FVector3d(1.0, 1.0, 1.0), FVector3d(1.0, 1.0, 0.0) };
    for (const FVector3d& diagonal : diagonals)
    {
        control.Thrust = diagonal;
        const FSOLShipState result = FlightStepRun(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT,
            steps, maxThrust);
        TestTrue(*FString::Printf(TEXT("Diagonal (%.0f, %.0f, %.0f): steady velocity is cap * normalized input"),
            diagonal.X, diagonal.Y, diagonal.Z),
            (result.VelocityMps - diagonal.GetSafeNormal() * control.SpeedCapMps).Size() <= 1e-6);
    }

    // An input shorter than unit length is not scaled up
    control.Thrust = FVector3d(0.5, 0.0, 0.0);
    const FSOLShipState half = FlightStepRun(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT, steps,
        maxThrust);
    TestTrue(TEXT("Half thrust targets half the cap"),
        (half.VelocityMps - FVector3d(0.5 * control.SpeedCapMps, 0.0, 0.0)).Size() <= 1e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistBoostTest, "SOLTest.FlightStep.AssistBoostDoublesTargetSpeedOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Assisted boost doubles the target speed but not the acceleration: the hold still stops at 1.5 G
bool FSOLFlightStepAssistBoostTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipControl control = FlightStepControl(true);
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    control.SpeedCapMps = 100.0;
    control.bBoost = true;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    // First second: saturated at 1.5 G, same as unboosted
    double maxThrust = 0.0;
    const FSOLShipState boosted1 = FlightStepRun(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT, 30,
        maxThrust);
    TestTrue(*FString::Printf(TEXT("Boosted speed after 1 s %.6f == 1.5 G * 1 s"), boosted1.VelocityMps.X),
        FMath::Abs(boosted1.VelocityMps.X - FLIGHT_STEP_SUSTAINED_MPS2) <= 1e-9);
    TestTrue(TEXT("Boost does not raise the assisted thrust above 1.5 G"),
        maxThrust <= FLIGHT_STEP_SUSTAINED_MPS2 * (1.0 + FLIGHT_STEP_ROUNDING));

    // Steady state: twice the cap with boost (200 m/s at 1.5 G is ~14 s), the cap without
    const int32 steps = FMath::RoundToInt32(40.0 / FLIGHT_STEP_DT);
    const FSOLShipState boostedSteady = FlightStepRun(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT,
        steps, maxThrust);
    TestTrue(*FString::Printf(TEXT("Boosted steady speed %.6f is twice the cap"), boostedSteady.VelocityMps.X),
        FMath::Abs(boostedSteady.VelocityMps.X - 2.0 * control.SpeedCapMps) <= 1e-6);
    control.bBoost = false;
    const FSOLShipState plainSteady = FlightStepRun(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT,
        steps, maxThrust);
    TestTrue(*FString::Printf(TEXT("Unboosted steady speed %.6f is the cap"), plainSteady.VelocityMps.X),
        FMath::Abs(plainSteady.VelocityMps.X - control.SpeedCapMps) <= 1e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistMaxSpeedTest, "SOLTest.FlightStep.AssistClampsToMaxSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The assisted relative target is clamped to 0.5c, and the absolute speed never exceeds 0.5c
bool FSOLFlightStepAssistMaxSpeedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipControl control = FlightStepControl(true);
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    control.SpeedCapMps = 1.0e9;
    control.bBoost = true;

    // Reference -1e8 along X and the ship already at reference + 0.5c: the clamped target is exactly here, so the
    // hold asks for nothing (unclamped it would be reference + 2e9 and the ship would accelerate)
    control.ReferenceVelocityMps = FVector3d(-1.0e8, 0.0, 0.0);
    const FSOLShipState atTarget = FlightStepMakeState(control.ReferenceVelocityMps
        + FVector3d(FLIGHT_STEP_HALF_C, 0.0, 0.0), FQuat4d::Identity);
    FSOLFlightStepOutput output;
    const FSOLShipState held = SOLFlight::Step(atTarget, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT,
        &output);
    TestTrue(*FString::Printf(TEXT("Relative target clamped: velocity %.6f unchanged"), held.VelocityMps.X),
        (held.VelocityMps - atTarget.VelocityMps).Size() <= 1e-6);
    TestTrue(TEXT("No thrust requested at the clamped target"), output.ThrustAccelWorldMps2.Size() <= 1e-6);

    // A fast reference plus full thrust: the speed reaches 0.5c and is clamped there in absolute terms
    control.ReferenceVelocityMps = FVector3d(1.0e8, 0.0, 0.0);
    FSOLShipState fast = FlightStepMakeState(FVector3d(FLIGHT_STEP_HALF_C - 5.0, 0.0, 0.0), FQuat4d::Identity);
    bool bNeverExceeded = true;
    for (int32 step = 0; step < 60; ++step)
    {
        fast = SOLFlight::Step(fast, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        bNeverExceeded = bNeverExceeded && fast.VelocityMps.Size() <= FLIGHT_STEP_HALF_C * (1.0 + 1e-12);
    }
    TestTrue(TEXT("Speed never exceeds 0.5c"), bNeverExceeded);
    TestTrue(*FString::Printf(TEXT("Absolute speed %.3f clamped to 0.5c"), fast.VelocityMps.Size()),
        fast.VelocityMps.Size() >= FLIGHT_STEP_HALF_C * (1.0 - 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistHoverTest, "SOLTest.FlightStep.AssistHoverUnderOneGHoldsVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Under 1 G (below the 1.5 G sustained limit) the assist cancels gravity: the velocity holds in any attitude
bool FSOLFlightStepAssistHoverTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FVector3d gravity(0.0, 0.0, -FLIGHT_STEP_G0);
    const FQuat4d orientations[] =
    {
        FQuat4d::Identity,
        FlightStepRolledOrientation(FMath::DegreesToRadians(30.0)),
        FlightStepRolledOrientation(UE_DOUBLE_PI),                  // upside down: the down-facing thrusters lift
        FlightStepPitchedOrientation(UE_DOUBLE_HALF_PI),            // nose up: the main engine lifts
    };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(orientations)); ++index)
    {
        FSOLShipControl control = FlightStepControl(true);
        control.ReferenceVelocityMps = FVector3d(5.0, -2.0, 0.0);
        const FSOLShipState start = FlightStepMakeState(control.ReferenceVelocityMps, orientations[index]);
        double maxThrust = 0.0;
        const FSOLShipState result = FlightStepRun(start, control, params, gravity, FLIGHT_STEP_DT, 600, maxThrust);
        TestTrue(*FString::Printf(TEXT("Attitude %d: velocity held for 20 s (drift %.3e m/s)"), index,
            (result.VelocityMps - start.VelocityMps).Size()), (result.VelocityMps - start.VelocityMps).Size() <= 1e-6);
        TestTrue(*FString::Printf(TEXT("Attitude %d: thrust is 1 G (%.9f)"), index, maxThrust),
            FMath::Abs(maxThrust - FLIGHT_STEP_G0) <= 1e-6);
        TestTrue(*FString::Printf(TEXT("Attitude %d: attitude unchanged"), index),
            result.Orientation.Equals(start.Orientation, 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistNoHoverTest, "SOLTest.FlightStep.AssistCannotHoverAboveSustainedG",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Under 2.5 G the assist can only push back with 1.5 G (all of it against gravity), so the ship falls at 1 G
bool FSOLFlightStepAssistNoHoverTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FVector3d gravity(0.0, 0.0, -2.5 * FLIGHT_STEP_G0);
    FSOLShipControl control = FlightStepControl(true);
    control.Thrust = FVector3d(1.0, 0.0, 0.0);      // asks to fly forward too: gravity has priority
    control.SpeedCapMps = 1000.0;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    FSOLShipState state = start;
    bool bAllUp = true;
    for (int32 step = 0; step < 30; ++step)
    {
        FSOLFlightStepOutput output;
        state = SOLFlight::Step(state, control, params, gravity, FLIGHT_STEP_DT, &output);
        bAllUp = bAllUp && (output.ThrustAccelWorldMps2 - FVector3d(0.0, 0.0, FLIGHT_STEP_SUSTAINED_MPS2)).Size()
            <= 1e-9;
    }
    TestTrue(TEXT("Every step thrusts exactly 1.5 G straight up"), bAllUp);
    TestTrue(*FString::Printf(TEXT("After 1 s the ship falls at 1 G (vz %.9f)"), state.VelocityMps.Z),
        (state.VelocityMps - FVector3d(0.0, 0.0, -FLIGHT_STEP_G0)).Size() <= 1e-9);
    return true;
}

// --- Rotation ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepRotSignsTest, "SOLTest.FlightStep.RotationSignConventions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From identity: +pitch raises the nose, +yaw turns right, +roll drops the right wing (up gains +right), both modes
bool FSOLFlightStepRotSignsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    const double dt = 0.1;
    for (const bool bAssist : { true, false })
    {
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Manual");
        FSOLShipControl control = FlightStepControl(bAssist);

        // Pitch up: forward gains +Z, right unchanged
        control.Rotation = FVector3d(0.0, 1.0, 0.0);
        const FFlightStepAxes pitch = FlightStepAxesOf(SOLFlight::Step(start, control, params, FVector3d::ZeroVector,
            dt).Orientation);
        TestTrue(*FString::Printf(TEXT("%s: pitch raises the nose (forward.Z = %.6f)"), mode, pitch.Forward.Z),
            pitch.Forward.Z > 1e-3);
        TestTrue(*FString::Printf(TEXT("%s: pitch keeps right"), mode),
            SOLTestHelpers::VectorsNear(pitch.Right, FVector3d(0.0, 1.0, 0.0), 1e-9));

        // Yaw right: forward gains +Y, up unchanged
        control.Rotation = FVector3d(0.0, 0.0, 1.0);
        const FFlightStepAxes yaw = FlightStepAxesOf(SOLFlight::Step(start, control, params, FVector3d::ZeroVector,
            dt).Orientation);
        TestTrue(*FString::Printf(TEXT("%s: yaw turns right (forward.Y = %.6f)"), mode, yaw.Forward.Y),
            yaw.Forward.Y > 1e-3);
        TestTrue(*FString::Printf(TEXT("%s: yaw keeps up"), mode),
            SOLTestHelpers::VectorsNear(yaw.Up, FVector3d(0.0, 0.0, 1.0), 1e-9));

        // Roll right: up gains +Y, right gains -Z, forward unchanged
        control.Rotation = FVector3d(1.0, 0.0, 0.0);
        const FFlightStepAxes roll = FlightStepAxesOf(SOLFlight::Step(start, control, params, FVector3d::ZeroVector,
            dt).Orientation);
        TestTrue(*FString::Printf(TEXT("%s: roll tilts up toward +right (up.Y = %.6f)"), mode, roll.Up.Y),
            roll.Up.Y > 1e-3);
        TestTrue(*FString::Printf(TEXT("%s: roll drops the right wing (right.Z = %.6f)"), mode, roll.Right.Z),
            roll.Right.Z < -1e-3);
        TestTrue(*FString::Printf(TEXT("%s: roll keeps forward"), mode),
            SOLTestHelpers::VectorsNear(roll.Forward, FVector3d(1.0, 0.0, 0.0), 1e-9));

        // Negative inputs reverse the direction
        control.Rotation = FVector3d(0.0, -1.0, -1.0);
        const FFlightStepAxes negative = FlightStepAxesOf(SOLFlight::Step(start, control, params,
            FVector3d::ZeroVector, dt).Orientation);
        TestTrue(*FString::Printf(TEXT("%s: negative pitch/yaw lowers the nose and turns left"), mode),
            negative.Forward.Z < -1e-3 && negative.Forward.Y < -1e-3);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepRotFirstStepTest,
    "SOLTest.FlightStep.RotationFirstStepUsesMaxAngularAccel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From rest a full input spins up by alpha * dt (both modes), then the orientation turns by that new rate * dt
bool FSOLFlightStepRotFirstStepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    const double dt = 0.1;      // alpha * dt (0.3 to 0.4 rad/s) is below every max rate, so assisted saturates too
    const EFlightStepAxis axes[] = { EFlightStepAxis::Roll, EFlightStepAxis::Pitch, EFlightStepAxis::Yaw };
    for (const bool bAssist : { true, false })
    {
        for (const EFlightStepAxis axis : axes)
        {
            FSOLShipControl control = FlightStepControl(bAssist);
            control.Rotation = FlightStepAxisInput(axis, 1.0);
            const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
            const FVector3d expectedRate = control.Rotation * FlightStepAlpha(params, axis) * dt;
            const double angle = expectedRate.Size() * dt;
            const FFlightStepAxes expected = FlightStepRotatedAxes(FlightStepAxesOf(FQuat4d::Identity), axis, angle);
            const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Manual");
            TestTrue(*FString::Printf(TEXT("%s axis %d: rate is alpha * dt"), mode, static_cast<int32>(axis)),
                (result.AngularVelocityRadPerS - expectedRate).Size() <= 1e-12);
            TestTrue(*FString::Printf(TEXT("%s axis %d: turned by the new rate * dt (%.9f rad)"), mode,
                static_cast<int32>(axis), angle), FlightStepAxesNear(FlightStepAxesOf(result.Orientation), expected,
                1e-9));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistRateTest, "SOLTest.FlightStep.AssistRateCommandHoldsRate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Assisted rotation input commands a rate: it ramps at alpha to input * max rate, holds it, and releases to zero
bool FSOLFlightStepAssistRateTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const double alphaPitch = params.Envelope.MaxAngularAccelRadPerS2.Y;
    const double ratePitch = params.Envelope.MaxAngularRateRadPerS.Y;
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    FSOLShipControl control = FlightStepControl(true);
    control.Rotation = FVector3d(0.0, 1.0, 0.0);

    // Pitch 1.5708 rad/s at 3 rad/s^2 takes 0.52 s; each step changes the rate by at most alpha * dt
    bool bBounded = true;
    for (int32 step = 0; step < 30; ++step)
    {
        const FSOLShipState before = state;
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        bBounded = bBounded && FMath::Abs(state.AngularVelocityRadPerS.Y - before.AngularVelocityRadPerS.Y)
            <= alphaPitch * FLIGHT_STEP_DT * (1.0 + FLIGHT_STEP_ROUNDING);
    }
    TestTrue(TEXT("Spin-up bounded by alpha * dt"), bBounded);
    TestTrue(*FString::Printf(TEXT("Holds the max pitch rate (%.12f)"), state.AngularVelocityRadPerS.Y),
        FMath::Abs(state.AngularVelocityRadPerS.Y - ratePitch) <= 1e-12);
    TestTrue(TEXT("Other axes stay still"), FMath::Abs(state.AngularVelocityRadPerS.X)
        + FMath::Abs(state.AngularVelocityRadPerS.Z) <= 1e-12);

    // A half input holds half the rate
    control.Rotation = FVector3d(0.0, 0.5, 0.0);
    for (int32 step = 0; step < 30; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
    }
    TestTrue(TEXT("Half input holds half the rate"),
        FMath::Abs(state.AngularVelocityRadPerS.Y - 0.5 * ratePitch) <= 1e-12);

    // Releasing the input brings the rate back to zero (the assist is a rate command, zero input = stop)
    control.Rotation = FVector3d::ZeroVector;
    for (int32 step = 0; step < 30; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
    }
    TestTrue(TEXT("Released input stops the rotation"), state.AngularVelocityRadPerS.Size() <= 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualTorqueTest,
    "SOLTest.FlightStep.ManualTorqueAccumulatesToSpinCapNoDamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Manual rotation input is torque: the rate grows at alpha past the assisted max rate up to the spin cap, and persists
bool FSOLFlightStepManualTorqueTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const double alphaPitch = params.Envelope.MaxAngularAccelRadPerS2.Y;
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    FSOLShipControl control = FlightStepControl(false);
    control.Rotation = FVector3d(0.0, 1.0, 0.0);

    // 0.5 s of full torque: 1.5 rad/s
    for (int32 step = 0; step < 5; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 0.1);
    }
    TestTrue(TEXT("Rate is alpha * 0.5 s"), FMath::Abs(state.AngularVelocityRadPerS.Y - alphaPitch * 0.5) <= 1e-12);

    // 3 s more would give 10.5 rad/s, but the spin cap holds it at 2 PI (past the 1.57 rad/s assisted rate)
    for (int32 step = 0; step < 30; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 0.1);
    }
    TestTrue(*FString::Printf(TEXT("Clamped at the spin cap (%.9f)"), state.AngularVelocityRadPerS.Y),
        FMath::Abs(state.AngularVelocityRadPerS.Y - params.Envelope.MaxSpinRateRadPerS) <= 1e-12);

    // Releasing the input keeps the spin (no damping)
    control.Rotation = FVector3d::ZeroVector;
    const FVector3d spin = state.AngularVelocityRadPerS;
    for (int32 step = 0; step < 100; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 0.1);
    }
    TestTrue(TEXT("Spin persists with no input"), (state.AngularVelocityRadPerS - spin).Size() <= 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualStopSpinTest, "SOLTest.FlightStep.StopSpinManualStopsSpin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Manual stop spin overrides the torque input and brakes each axis at no more than alpha, down to zero
bool FSOLFlightStepManualStopSpinTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FVector3d alpha = params.Envelope.MaxAngularAccelRadPerS2;
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d(FVector3d(0.2, 0.5, -0.4)
        .GetSafeNormal(), 0.7));
    state.AngularVelocityRadPerS = FVector3d(3.0, -2.0, 1.0);
    FSOLShipControl control = FlightStepControl(false);
    control.bStopSpin = true;
    control.Rotation = FVector3d(1.0, 1.0, 1.0);        // ignored while stop spin is held

    // Slowest axis: roll 3 rad/s at 4 rad/s^2 = 0.75 s; 1 s is enough for all three
    bool bBounded = true;
    bool bNoReversal = true;
    for (int32 step = 0; step < 30; ++step)
    {
        const FVector3d before = state.AngularVelocityRadPerS;
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        const FVector3d after = state.AngularVelocityRadPerS;
        for (int32 axis = 0; axis < 3; ++axis)
        {
            bBounded = bBounded && FMath::Abs(after[axis] - before[axis])
                <= alpha[axis] * FLIGHT_STEP_DT * (1.0 + FLIGHT_STEP_ROUNDING);
            // The final step lands on zero up to rounding, which may leave a ~1e-16 residue of either sign
            bNoReversal = bNoReversal && after[axis] * before[axis] >= -1e-12;
        }
    }
    TestTrue(TEXT("Braking bounded by alpha * dt per axis"), bBounded);
    TestTrue(TEXT("No axis overshoots through zero"), bNoReversal);
    TestTrue(*FString::Printf(TEXT("Spin stopped after 1 s (|w| = %.3e)"), state.AngularVelocityRadPerS.Size()),
        state.AngularVelocityRadPerS.Size() <= 1e-12);

    // Holding it longer keeps the ship still
    const FQuat4d stopped = state.Orientation;
    for (int32 step = 0; step < 30; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
    }
    TestTrue(TEXT("Stays stopped"), state.AngularVelocityRadPerS.Size() <= 1e-12
        && state.Orientation.Equals(stopped, 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistStopSpinTest,
    "SOLTest.FlightStep.StopSpinAssistOverridesRotationInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Assisted stop spin commands zero rate on every axis, overriding both the rotation input and the attitude target
bool FSOLFlightStepAssistStopSpinTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FVector3d alpha = params.Envelope.MaxAngularAccelRadPerS2;
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FlightStepRolledOrientation(0.5));
    state.AngularVelocityRadPerS = FVector3d(1.5, -1.0, 0.8);
    FSOLShipControl control = FlightStepControl(true);
    control.bStopSpin = true;
    control.Rotation = FVector3d(1.0, 1.0, 1.0);
    control.AttitudeUpWorld = FVector3d(0.0, 0.0, 1.0);     // would level the roll, but stop spin wins

    bool bBounded = true;
    for (int32 step = 0; step < 30; ++step)
    {
        const FVector3d before = state.AngularVelocityRadPerS;
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        for (int32 axis = 0; axis < 3; ++axis)
        {
            bBounded = bBounded && FMath::Abs(state.AngularVelocityRadPerS[axis] - before[axis])
                <= alpha[axis] * FLIGHT_STEP_DT * (1.0 + FLIGHT_STEP_ROUNDING);
        }
    }
    TestTrue(TEXT("Braking bounded by alpha * dt per axis"), bBounded);
    TestTrue(TEXT("Spin stopped after 1 s despite full input"), state.AngularVelocityRadPerS.Size() <= 1e-12);

    // Still held: the orientation stays put (no leveling toward the attitude up)
    const FQuat4d stopped = state.Orientation;
    for (int32 step = 0; step < 60; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
    }
    TestTrue(TEXT("Orientation frozen while stop spin is held"), state.Orientation.Equals(stopped, 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAttitudeLevelTest, "SOLTest.FlightStep.AssistAttitudeUpLevelsShip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With an assisted attitude up and no rotation input, a pitched and rolled ship levels: nose horizontal, up along up
bool FSOLFlightStepAttitudeLevelTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FVector3d up(0.0, 0.0, 1.0);
    const int32 steps = FMath::RoundToInt32(6.0 / FLIGHT_STEP_DT);     // rotation ~1 s, then exp. settling at 4 /s

    // Pitched 0.3 rad and rolled 0.4 rad
    {
        FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FlightStepPitchedOrientation(0.3)
            * FlightStepRolledOrientation(0.4));
        FSOLShipControl control = FlightStepControl(true);
        control.AttitudeUpWorld = up * 7.0;             // not unit length: only its direction matters
        for (int32 step = 0; step < steps; ++step)
        {
            state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        }
        const FFlightStepAxes axes = FlightStepAxesOf(state.Orientation);
        TestTrue(*FString::Printf(TEXT("Nose level (forward.Z %.3e)"), axes.Forward.Z),
            FMath::Abs(axes.Forward.Z) <= 1e-4);
        TestTrue(*FString::Printf(TEXT("Up along the attitude up (angle %.3e rad)"),
            FlightStepAngleBetween(axes.Up, up)), FlightStepAngleBetween(axes.Up, up) <= 1e-4);
        TestTrue(TEXT("Rotation settled"), state.AngularVelocityRadPerS.Size() <= 1e-3);
    }

    // Pure pitch: levels about the pitch axis only, so the heading (forward in the XZ plane) is kept
    {
        FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FlightStepPitchedOrientation(0.3));
        FSOLShipControl control = FlightStepControl(true);
        control.AttitudeUpWorld = up;
        for (int32 step = 0; step < steps; ++step)
        {
            state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        }
        const FFlightStepAxes axes = FlightStepAxesOf(state.Orientation);
        TestTrue(TEXT("Pitched ship levels to forward +X"),
            SOLTestHelpers::VectorsNear(axes.Forward, FVector3d(1.0, 0.0, 0.0), 1e-4));
        TestTrue(TEXT("Heading kept (no yaw)"), FMath::Abs(axes.Forward.Y) <= 1e-9);
    }

    // An already level ship stays exactly where it is
    {
        const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FLIGHT_STEP_YAW90);
        FSOLShipControl control = FlightStepControl(true);
        control.AttitudeUpWorld = up;
        double maxThrust = 0.0;
        const FSOLShipState result = FlightStepRun(start, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT, 60,
            maxThrust);
        TestTrue(TEXT("Level ship unchanged"), result.Orientation.Equals(start.Orientation, 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAttitudeInputTest,
    "SOLTest.FlightStep.AssistAttitudeUpOnlyOnAxesWithoutInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An axis with rotation input follows the input, not the attitude target; the other axes still level
bool FSOLFlightStepAttitudeInputTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FVector3d up(0.0, 0.0, 1.0);

    // A level ship rolling on input: the attitude adds nothing on pitch/yaw (nose already level), so the run is the
    // same as with no attitude target at all
    {
        FSOLShipControl withUp = FlightStepControl(true);
        withUp.Rotation = FVector3d(1.0, 0.0, 0.0);
        withUp.AttitudeUpWorld = up;
        FSOLShipControl withoutUp = withUp;
        withoutUp.AttitudeUpWorld = FVector3d::ZeroVector;
        FSOLShipState a = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
        FSOLShipState b = a;
        for (int32 step = 0; step < 60; ++step)
        {
            a = SOLFlight::Step(a, withUp, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
            b = SOLFlight::Step(b, withoutUp, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        }
        TestTrue(TEXT("Roll input wins over the attitude roll"), a.Orientation.Equals(b.Orientation, 1e-9)
            && (a.AngularVelocityRadPerS - b.AngularVelocityRadPerS).Size() <= 1e-9);
        TestTrue(*FString::Printf(TEXT("Rolling at the max roll rate (%.9f)"), a.AngularVelocityRadPerS.X),
            FMath::Abs(a.AngularVelocityRadPerS.X - params.Envelope.MaxAngularRateRadPerS.X) <= 1e-9);
    }

    // A rolled ship with yaw input: yaw follows the input while the roll still levels
    {
        FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FlightStepRolledOrientation(0.4));
        FSOLShipControl control = FlightStepControl(true);
        control.Rotation = FVector3d(0.0, 0.0, 0.5);
        control.AttitudeUpWorld = up;
        for (int32 step = 0; step < FMath::RoundToInt32(4.0 / FLIGHT_STEP_DT); ++step)
        {
            state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        }
        TestTrue(*FString::Printf(TEXT("Yaw holds the commanded half rate (%.9f)"), state.AngularVelocityRadPerS.Z),
            FMath::Abs(state.AngularVelocityRadPerS.Z - 0.5 * params.Envelope.MaxAngularRateRadPerS.Z) <= 1e-6);
        const FFlightStepAxes axes = FlightStepAxesOf(state.Orientation);
        TestTrue(*FString::Printf(TEXT("Roll levelled while yawing (up.Z %.9f)"), axes.Up.Z),
            FlightStepAngleBetween(axes.Up, up) <= 1e-2);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepManualAttitudeTest, "SOLTest.FlightStep.ManualIgnoresAttitudeUp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Manual mode ignores the attitude up: a tilted ship with no input stays tilted, identical to no attitude target
bool FSOLFlightStepManualAttitudeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FSOLShipState start = FlightStepMakeState(FVector3d(10.0, 0.0, 0.0), FlightStepPitchedOrientation(0.3)
        * FlightStepRolledOrientation(0.4));
    FSOLShipControl withUp = FlightStepControl(false);
    withUp.AttitudeUpWorld = FVector3d(0.0, 0.0, 1.0);
    withUp.Rotation = FVector3d(0.0, 0.2, 0.0);
    FSOLShipControl withoutUp = withUp;
    withoutUp.AttitudeUpWorld = FVector3d::ZeroVector;
    FSOLShipState a = start;
    FSOLShipState b = start;
    for (int32 step = 0; step < 60; ++step)
    {
        a = SOLFlight::Step(a, withUp, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
        b = SOLFlight::Step(b, withoutUp, params, FVector3d::ZeroVector, FLIGHT_STEP_DT);
    }
    TestTrue(TEXT("Identical orientation with and without the attitude up"),
        a.Orientation.Equals(b.Orientation, 1e-15));
    TestTrue(TEXT("Identical angular velocity"), (a.AngularVelocityRadPerS - b.AngularVelocityRadPerS).Size() <= 1e-15);

    // And with no input the tilted ship does not move at all
    FSOLShipControl idle = FlightStepControl(false);
    idle.AttitudeUpWorld = FVector3d(0.0, 0.0, 1.0);
    double maxThrust = 0.0;
    const FSOLShipState still = FlightStepRun(start, idle, params, FVector3d::ZeroVector, FLIGHT_STEP_DT, 60,
        maxThrust);
    TestTrue(TEXT("No input: orientation unchanged"), still.Orientation.Equals(start.Orientation, 1e-12));
    TestTrue(TEXT("No input: no rotation"), still.AngularVelocityRadPerS.IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepNormalizedTest, "SOLTest.FlightStep.OrientationStaysNormalized",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After 100000 steps of varying rotation input the orientation quaternion is still unit length, in both modes
bool FSOLFlightStepNormalizedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    for (const bool bAssist : { true, false })
    {
        FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
        FSOLShipControl control = FlightStepControl(bAssist);
        for (int32 step = 0; step < 100000; ++step)
        {
            // Slowly varying inputs on all three axes
            const double t = step * 0.001;
            control.Rotation = FVector3d(FMath::Sin(t * 1.3), FMath::Cos(t * 0.7), FMath::Sin(t * 2.9 + 0.4));
            state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 1.0 / 60.0);
        }
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Manual");
        TestTrue(*FString::Printf(TEXT("%s: |q| = %.15f"), mode, state.Orientation.Size()),
            FMath::Abs(state.Orientation.Size() - 1.0) <= 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: orientation is finite"), mode), FMath::IsFinite(state.Orientation.X)
            && FMath::IsFinite(state.Orientation.Y) && FMath::IsFinite(state.Orientation.Z)
            && FMath::IsFinite(state.Orientation.W));

        // The ship actually rotated (guards against a do-nothing step)
        TestFalse(*FString::Printf(TEXT("%s: orientation changed"), mode),
            state.Orientation.Equals(FQuat4d::Identity, 1e-3));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepGimbalPitch90Test, "SOLTest.FlightStep.NoGimbalLockYawAfterPitch90",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After pitching the nose straight up, pure yaw still turns the ship about its local up axis by the commanded angle
bool FSOLFlightStepGimbalPitch90Test::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    // Pitch up 90 degrees: 10 steady steps of 0.1 s at 90 deg/s
    for (int32 step = 0; step < 10; ++step)
    {
        state = FlightStepSteadyRotate(state, FVector3d(0.0, 1.0, 0.0), 0.1, true);
    }
    const FFlightStepAxes nosedUp = FlightStepAxesOf(state.Orientation);
    TestTrue(TEXT("Nose points straight up"), SOLTestHelpers::VectorsNear(nosedUp.Forward, FVector3d(0.0, 0.0, 1.0),
        1e-9));
    TestTrue(TEXT("Up points backward (-X)"), SOLTestHelpers::VectorsNear(nosedUp.Up, FVector3d(-1.0, 0.0, 0.0), 1e-9));
    TestTrue(TEXT("Right unchanged (+Y)"), SOLTestHelpers::VectorsNear(nosedUp.Right, FVector3d(0.0, 1.0, 0.0), 1e-9));

    // Yaw 10 steady steps: each turns forward by exactly rate * dt about the local up
    const double stepAngle = params.Envelope.MaxAngularRateRadPerS.Z * 0.1;
    for (int32 step = 0; step < 10; ++step)
    {
        const FFlightStepAxes before = FlightStepAxesOf(state.Orientation);
        state = FlightStepSteadyRotate(state, FVector3d(0.0, 0.0, 1.0), 0.1, true);
        const FFlightStepAxes after = FlightStepAxesOf(state.Orientation);
        const double turned = FlightStepAngleBetween(before.Forward, after.Forward);
        TestTrue(*FString::Printf(TEXT("Yaw step %d: forward turned %.12f == %.12f rad"), step, turned, stepAngle),
            FMath::Abs(turned - stepAngle) <= 1e-9);
        TestTrue(*FString::Printf(TEXT("Yaw step %d: axes as expected"), step), FlightStepAxesNear(after,
            FlightStepRotatedAxes(before, EFlightStepAxis::Yaw, stepAngle), 1e-9));
    }

    // 90 degrees of yaw from nose-up points the nose along the old right (+Y); up is still -X
    const FFlightStepAxes yawed = FlightStepAxesOf(state.Orientation);
    TestTrue(TEXT("Nose now points along +Y"), SOLTestHelpers::VectorsNear(yawed.Forward, FVector3d(0.0, 1.0, 0.0),
        1e-9));
    TestTrue(TEXT("Up still -X"), SOLTestHelpers::VectorsNear(yawed.Up, FVector3d(-1.0, 0.0, 0.0), 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepGimbalRateTest,
    "SOLTest.FlightStep.NoGimbalLockYawRateIndependentOfPitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The yaw response at 89.9, 90 and 90.1 degrees of pitch has the same magnitude as at 0 degrees, in both modes
bool FSOLFlightStepGimbalRateTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const double pitchesDeg[] = { 0.0, 89.9, 90.0, 90.1, -90.0, 179.0 };
    const double dt = 0.05;
    const double fromRestAngle = params.Envelope.MaxAngularAccelRadPerS2.Z * dt * dt;     // spin up alpha * dt
    const double steadyAngle = params.Envelope.MaxAngularRateRadPerS.Z * dt;
    for (const bool bAssist : { true, false })
    {
        FSOLShipControl control = FlightStepControl(bAssist);
        control.Rotation = FVector3d(0.0, 0.0, 1.0);
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Manual");
        for (const double pitchDeg : pitchesDeg)
        {
            const FQuat4d orientation = FlightStepPitchedOrientation(FMath::DegreesToRadians(pitchDeg));
            const FFlightStepAxes before = FlightStepAxesOf(orientation);
            const FVector3d expectedForward(FMath::Cos(FMath::DegreesToRadians(pitchDeg)), 0.0,
                FMath::Sin(FMath::DegreesToRadians(pitchDeg)));
            TestTrue(*FString::Printf(TEXT("Pitch %.1f: test orientation built correctly"), pitchDeg),
                SOLTestHelpers::VectorsNear(before.Forward, expectedForward, 1e-12));

            // From rest (the rate spins up by alpha * dt first)
            const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, orientation);
            const FSOLShipState fromRest = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
            const FFlightStepAxes afterRest = FlightStepAxesOf(fromRest.Orientation);
            const double turnedRest = FlightStepAngleBetween(before.Forward, afterRest.Forward);
            TestTrue(*FString::Printf(TEXT("%s pitch %.1f: yaw from rest %.12f == %.12f rad"), mode, pitchDeg,
                turnedRest, fromRestAngle), FMath::Abs(turnedRest - fromRestAngle) <= 1e-10);
            TestTrue(*FString::Printf(TEXT("%s pitch %.1f: yaw keeps the local up"), mode, pitchDeg),
                SOLTestHelpers::VectorsNear(afterRest.Up, before.Up, 1e-10));

            // At the steady rate
            const FSOLShipState steady = FlightStepSteadyRotate(start, control.Rotation, dt, bAssist);
            const FFlightStepAxes afterSteady = FlightStepAxesOf(steady.Orientation);
            TestTrue(*FString::Printf(TEXT("%s pitch %.1f: steady yaw axes as expected"), mode, pitchDeg),
                FlightStepAxesNear(afterSteady, FlightStepRotatedAxes(before, EFlightStepAxis::Yaw, steadyAngle),
                1e-10));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepGimbalSequenceTest,
    "SOLTest.FlightStep.NoGimbalLockIndependentAxesAnywhere",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Roll, then pitch, then yaw are three independent local rotations at any starting orientation, including nose-up
bool FSOLFlightStepGimbalSequenceTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FQuat4d starts[] =
    {
        FQuat4d::Identity,
        FlightStepPitchedOrientation(UE_DOUBLE_HALF_PI),
        FlightStepPitchedOrientation(-UE_DOUBLE_HALF_PI),
        FQuat4d(FVector3d(1.0, 2.0, 3.0).GetSafeNormal(), 1.234),
        FQuat4d(FVector3d(-0.3, 0.9, 0.1).GetSafeNormal(), 2.9),
    };
    const EFlightStepAxis sequence[] = { EFlightStepAxis::Roll, EFlightStepAxis::Pitch, EFlightStepAxis::Yaw };
    const double dt = 0.2;
    for (int32 startIndex = 0; startIndex < static_cast<int32>(UE_ARRAY_COUNT(starts)); ++startIndex)
    {
        FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, starts[startIndex]);
        FFlightStepAxes expected = FlightStepAxesOf(starts[startIndex]);
        for (const EFlightStepAxis axis : sequence)
        {
            // Each single-axis step rotates only the two axes it should, by rate * dt (assisted and manual alternate)
            const FFlightStepAxes before = FlightStepAxesOf(state.Orientation);
            const FVector3d input = FlightStepAxisInput(axis, 1.0);
            const double angle = FlightStepCommandedRate(input, params).Size() * dt;
            state = FlightStepSteadyRotate(state, input, dt, (startIndex % 2) == 0);
            const FFlightStepAxes after = FlightStepAxesOf(state.Orientation);
            TestTrue(*FString::Printf(TEXT("Start %d, axis %d: local rotation by %.6f rad"), startIndex,
                static_cast<int32>(axis), angle), FlightStepAxesNear(after,
                FlightStepRotatedAxes(before, axis, angle), 1e-9));
            expected = FlightStepRotatedAxes(expected, axis, angle);
        }
        TestTrue(*FString::Printf(TEXT("Start %d: composed result as expected"), startIndex),
            FlightStepAxesNear(FlightStepAxesOf(state.Orientation), expected, 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepFullTurnTest, "SOLTest.FlightStep.FullTurnReturnsToStart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A full 360 degree roll, pitch or yaw returns the ship to its starting orientation
bool FSOLFlightStepFullTurnTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params = FlightStepParams();
    const FQuat4d startOrientation(FVector3d(0.2, -0.5, 0.8).GetSafeNormal(), 0.9);
    const FFlightStepAxes startAxes = FlightStepAxesOf(startOrientation);
    const EFlightStepAxis axes[] = { EFlightStepAxis::Roll, EFlightStepAxis::Pitch, EFlightStepAxis::Yaw };
    const double dt = 0.1;
    for (const EFlightStepAxis axis : axes)
    {
        const FVector3d input = FlightStepAxisInput(axis, 1.0);
        const double rate = FlightStepCommandedRate(input, params).Size();
        const int32 steps = FMath::RoundToInt32(UE_DOUBLE_TWO_PI / (rate * dt));
        FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, startOrientation);
        for (int32 step = 0; step < steps; ++step)
        {
            state = FlightStepSteadyRotate(state, input, dt, true);

            // Halfway round the rotated axes are reversed
            if (step == steps / 2 - 1)
            {
                const FFlightStepAxes half = FlightStepAxesOf(state.Orientation);
                TestTrue(*FString::Printf(TEXT("Axis %d: half turn is 180 degrees"), static_cast<int32>(axis)),
                    FlightStepAxesNear(half, FlightStepRotatedAxes(startAxes, axis, UE_DOUBLE_PI), 1e-9));
            }
        }
        TestTrue(*FString::Printf(TEXT("Axis %d: %d steps make a full turn"), static_cast<int32>(axis), steps),
            FMath::Abs(steps * rate * dt - UE_DOUBLE_TWO_PI) <= 1e-9);
        TestTrue(*FString::Printf(TEXT("Axis %d: back to the start after 360 degrees"), static_cast<int32>(axis)),
            FlightStepAxesNear(FlightStepAxesOf(state.Orientation), startAxes, 1e-9));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
