/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlight.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
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
    // Returns the commanded local angular velocity for a rotation input under the given params
    FVector3d FlightStepCommandedRate(const FVector3d& rotationInput, const FSOLFlightParams& params)
    {
        return FVector3d(rotationInput.X * params.MaxRollRateRadPerS, rotationInput.Y * params.MaxPitchYawRateRadPerS,
            rotationInput.Z * params.MaxPitchYawRateRadPerS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Steps once with the angular velocity already at the commanded rate, so the turn is exactly rate * dt
    FSOLShipState FlightStepSteadyRotate(FSOLShipState state, const FVector3d& rotationInput, const double dt,
        const bool bAssist)
    {
        const FSOLFlightParams params;
        FSOLShipControl control;
        control.Rotation = rotationInput;
        control.bFlightAssist = bAssist;
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
    // Returns the expected assist velocity after one exact exponential relaxation step
    FVector3d FlightStepExpectedAssist(const FVector3d& velocity, const FVector3d& target, const double dt,
        const double tau)
    {
        return target + (velocity - target) * FMath::Exp(-dt / tau);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship state with a non-trivial position and the given velocity and orientation
    FSOLShipState FlightStepMakeState(const FVector3d& velocity, const FQuat4d& orientation)
    {
        FSOLShipState state;
        state.PositionM = FVector3d(1.0e7, -2.0e7, 3.0e6);
        state.VelocityMps = velocity;
        state.Orientation = orientation;
        return state;
    }

    // A yaw of +90 degrees from identity: forward is +Y, right is -X, up is +Z
    const FQuat4d FLIGHT_STEP_YAW90(FVector3d(0.0, 0.0, 1.0), UE_DOUBLE_HALF_PI);
}

// --- Flight assist ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistBrakeTest, "SOLTest.FlightStep.AssistZeroThrustBrakesToReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With zero thrust, the velocity relaxes exponentially toward the reference velocity with the assist time constant
bool FSOLFlightStepAssistBrakeTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.ReferenceVelocityMps = FVector3d(5.0, -3.0, 2.0);
    const FVector3d relative0(100.0, 50.0, -20.0);
    const FSOLShipState start = FlightStepMakeState(control.ReferenceVelocityMps + relative0, FQuat4d::Identity);

    // One step of exactly one time constant leaves e^-1 of the relative velocity
    const FSOLShipState oneTau = SOLFlight::Step(start, control, params, FVector3d::ZeroVector,
        params.AssistTimeConstantS);
    const FVector3d expectedOneTau = control.ReferenceVelocityMps + relative0 * FMath::Exp(-1.0);
    TestTrue(*FString::Printf(TEXT("After tau: (%.9f, %.9f, %.9f) == (%.9f, %.9f, %.9f)"), oneTau.VelocityMps.X,
        oneTau.VelocityMps.Y, oneTau.VelocityMps.Z, expectedOneTau.X, expectedOneTau.Y, expectedOneTau.Z),
        (oneTau.VelocityMps - expectedOneTau).Size() <= 1e-9 * relative0.Size());

    // Ten steps of tau leave e^-10 (essentially stopped relative to the frame)
    FSOLShipState state = start;
    for (int32 step = 0; step < 10; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, params.AssistTimeConstantS);
    }
    const FVector3d relative10 = state.VelocityMps - control.ReferenceVelocityMps;
    TestTrue(TEXT("After 10 tau: relative velocity is e^-10 of the start"),
        (relative10 - relative0 * FMath::Exp(-10.0)).Size() <= 1e-9 * relative0.Size());
    TestTrue(TEXT("After 10 tau: relative speed below 1e-4 of the start"), relative10.Size() < 1e-4 * relative0.Size());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistScaleTest, "SOLTest.FlightStep.AssistRelaxationIsScaleInvariant",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The remaining fraction after a step is the same at 1 m/s and at 1e7 m/s relative speed
bool FSOLFlightStepAssistScaleTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLShipControl control;
    const double dt = 0.3;
    const double expectedFraction = FMath::Exp(-dt / params.AssistTimeConstantS);
    const double speeds[] = { 1.0, 1.0e3, 1.0e7 };
    for (const double speed : speeds)
    {
        const FSOLShipState start = FlightStepMakeState(FVector3d(0.0, speed, 0.0), FQuat4d::Identity);
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
        const double fraction = result.VelocityMps.Y / speed;
        TestTrue(*FString::Printf(TEXT("Speed %.0e: remaining fraction %.12f == %.12f"), speed, fraction,
            expectedFraction), FMath::Abs(fraction - expectedFraction) <= 1e-12);
        TestTrue(*FString::Printf(TEXT("Speed %.0e: no drift in other axes"), speed),
            FMath::Abs(result.VelocityMps.X) + FMath::Abs(result.VelocityMps.Z) <= 1e-9 * speed);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistThrustTest, "SOLTest.FlightStep.AssistThrustTargetsCapAlongForward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Forward thrust relaxes the velocity toward reference + SpeedCap * forward (ship yawed 90 degrees: forward is +Y)
bool FSOLFlightStepAssistThrustTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    control.SpeedCapMps = 1000.0;
    control.ReferenceVelocityMps = FVector3d(10.0, 20.0, 30.0);
    const FSOLShipState start = FlightStepMakeState(control.ReferenceVelocityMps, FLIGHT_STEP_YAW90);
    const FVector3d target = control.ReferenceVelocityMps + FVector3d(0.0, 1000.0, 0.0);

    // One exact step
    const double dt = 0.7;
    const FSOLShipState one = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
    const FVector3d expected = FlightStepExpectedAssist(start.VelocityMps, target, dt, params.AssistTimeConstantS);
    TestTrue(*FString::Printf(TEXT("One step: (%.6f, %.6f, %.6f) == (%.6f, %.6f, %.6f)"), one.VelocityMps.X,
        one.VelocityMps.Y, one.VelocityMps.Z, expected.X, expected.Y, expected.Z),
        (one.VelocityMps - expected).Size() <= 1e-9);

    // Long run converges on the target
    FSOLShipState state = start;
    for (int32 step = 0; step < 40; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 1.0);
    }
    TestTrue(TEXT("Converges to reference + cap * forward"), (state.VelocityMps - target).Size() <= 1e-6);

    // Backward and strafe inputs follow the ship's axes too (right is -X, up is +Z after the yaw)
    struct FFlightStepThrustCase
    {
        FVector3d Thrust;
        FVector3d Direction;
    };
    const FFlightStepThrustCase cases[] =
    {
        { FVector3d(-1.0, 0.0, 0.0), FVector3d(0.0, -1.0, 0.0) },
        { FVector3d(0.0, 1.0, 0.0), FVector3d(-1.0, 0.0, 0.0) },
        { FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, 1.0) },
        { FVector3d(0.0, 0.0, -0.5), FVector3d(0.0, 0.0, -0.5) },
    };
    for (const FFlightStepThrustCase& testCase : cases)
    {
        FSOLShipControl caseControl = control;
        caseControl.Thrust = testCase.Thrust;
        const FVector3d caseTarget = control.ReferenceVelocityMps + testCase.Direction * control.SpeedCapMps;
        const FSOLShipState result = SOLFlight::Step(start, caseControl, params, FVector3d::ZeroVector, dt);
        const FVector3d caseExpected = FlightStepExpectedAssist(start.VelocityMps, caseTarget, dt,
            params.AssistTimeConstantS);
        TestTrue(*FString::Printf(TEXT("Thrust (%.1f, %.1f, %.1f) follows the ship axes"), testCase.Thrust.X,
            testCase.Thrust.Y, testCase.Thrust.Z), (result.VelocityMps - caseExpected).Size() <= 1e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistClampTest, "SOLTest.FlightStep.AssistThrustClampedToUnitLength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A diagonal thrust input is clamped to unit length, so it is not faster than a single axis; shorter inputs are kept
bool FSOLFlightStepAssistClampTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.SpeedCapMps = 500.0;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    const double dt = 1.0;

    // Diagonal (1, 1, 1) targets cap * normalize(1, 1, 1)
    control.Thrust = FVector3d(1.0, 1.0, 1.0);
    const FSOLShipState diag = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
    const FVector3d diagTarget = FVector3d(1.0, 1.0, 1.0).GetSafeNormal() * control.SpeedCapMps;
    TestTrue(TEXT("Diagonal thrust targets the cap along the normalized direction"), (diag.VelocityMps
        - FlightStepExpectedAssist(FVector3d::ZeroVector, diagTarget, dt, params.AssistTimeConstantS)).Size() <= 1e-9);

    // A long run with diagonal thrust ends at exactly the cap, not cap * sqrt(2)
    control.Thrust = FVector3d(1.0, 1.0, 0.0);
    FSOLShipState state = start;
    for (int32 step = 0; step < 50; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 1.0);
    }
    TestTrue(*FString::Printf(TEXT("Diagonal steady speed %.6f == cap %.1f"), state.VelocityMps.Size(),
        control.SpeedCapMps), FMath::Abs(state.VelocityMps.Size() - control.SpeedCapMps) <= 1e-6);

    // An input shorter than unit length is not scaled up
    control.Thrust = FVector3d(0.5, 0.0, 0.0);
    const FSOLShipState half = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
    const FVector3d halfTarget(0.5 * control.SpeedCapMps, 0.0, 0.0);
    TestTrue(TEXT("Half thrust targets half the cap"), (half.VelocityMps
        - FlightStepExpectedAssist(FVector3d::ZeroVector, halfTarget, dt, params.AssistTimeConstantS)).Size() <= 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistBoostTest, "SOLTest.FlightStep.AssistBoostDoublesSpeedAndShortensTau",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Boost targets twice the cap and relaxes with a third of the time constant
bool FSOLFlightStepAssistBoostTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    control.SpeedCapMps = 2000.0;
    control.bBoost = true;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    // One boosted step of the boosted time constant reaches (1 - e^-1) of twice the cap
    const double boostedTau = params.AssistTimeConstantS / params.BoostAccelMultiplier;
    TestEqual(TEXT("Boosted tau is 0.5 s"), boostedTau, 0.5, 1e-15);
    const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, boostedTau);
    const double expected = 2.0 * control.SpeedCapMps * (1.0 - FMath::Exp(-1.0));
    TestTrue(*FString::Printf(TEXT("Boosted speed %.6f == %.6f"), result.VelocityMps.X, expected),
        FMath::Abs(result.VelocityMps.X - expected) <= 1e-9);

    // The same step without boost uses the full tau and the plain cap
    control.bBoost = false;
    const FSOLShipState plain = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, boostedTau);
    const double plainExpected = control.SpeedCapMps * (1.0 - FMath::Exp(-boostedTau / params.AssistTimeConstantS));
    TestTrue(*FString::Printf(TEXT("Unboosted speed %.6f == %.6f"), plain.VelocityMps.X, plainExpected),
        FMath::Abs(plain.VelocityMps.X - plainExpected) <= 1e-9);

    // Boosted steady state is twice the cap
    control.bBoost = true;
    FSOLShipState state = start;
    for (int32 step = 0; step < 40; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 1.0);
    }
    TestTrue(TEXT("Boosted steady speed is twice the cap"),
        FMath::Abs(state.VelocityMps.X - 2.0 * control.SpeedCapMps) <= 1e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistMaxSpeedTest, "SOLTest.FlightStep.AssistClampsToMaxSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A cap request above 0.5c never yields more than MaxSpeedMps, relative or absolute
bool FSOLFlightStepAssistMaxSpeedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    TestEqual(TEXT("MaxSpeedMps is 0.5c"), params.MaxSpeedMps, 149896229.0, 0.0);
    FSOLShipControl control;
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    control.SpeedCapMps = 1.0e9;
    control.bBoost = true;

    // Zero reference: the speed approaches MaxSpeed and never exceeds it
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    bool bNeverExceeded = true;
    for (int32 step = 0; step < 60; ++step)
    {
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 1.0);
        bNeverExceeded &= state.VelocityMps.Size() <= params.MaxSpeedMps * (1.0 + 1e-12);
    }
    TestTrue(TEXT("Speed never exceeds MaxSpeedMps"), bNeverExceeded);
    TestTrue(*FString::Printf(TEXT("Speed converges to MaxSpeedMps (%.3f)"), state.VelocityMps.Size()),
        FMath::Abs(state.VelocityMps.Size() - params.MaxSpeedMps) <= 1e-3);

    // The relative target is clamped: reference -1e8 along X, thrust +X targets -1e8 + MaxSpeed (not -1e8 + 2e9)
    control.ReferenceVelocityMps = FVector3d(-1.0e8, 0.0, 0.0);
    const FSOLShipState refStart = FlightStepMakeState(control.ReferenceVelocityMps, FQuat4d::Identity);
    const double dt = 2.0;
    const FSOLShipState refResult = SOLFlight::Step(refStart, control, params, FVector3d::ZeroVector, dt);
    const FVector3d refTarget = control.ReferenceVelocityMps + FVector3d(params.MaxSpeedMps, 0.0, 0.0);
    const FVector3d refExpected = FlightStepExpectedAssist(refStart.VelocityMps, refTarget, dt,
        params.AssistTimeConstantS / params.BoostAccelMultiplier);
    TestTrue(*FString::Printf(TEXT("Relative target clamped: %.3f == %.3f"), refResult.VelocityMps.X, refExpected.X),
        (refResult.VelocityMps - refExpected).Size() <= 1e-3);

    // A fast reference plus full thrust is still clamped in absolute terms
    control.ReferenceVelocityMps = FVector3d(1.0e8, 0.0, 0.0);
    FSOLShipState fast = FlightStepMakeState(control.ReferenceVelocityMps, FQuat4d::Identity);
    for (int32 step = 0; step < 60; ++step)
    {
        fast = SOLFlight::Step(fast, control, params, FVector3d::ZeroVector, 1.0);
    }
    TestTrue(*FString::Printf(TEXT("Absolute speed %.3f clamped to MaxSpeedMps"), fast.VelocityMps.Size()),
        fast.VelocityMps.Size() <= params.MaxSpeedMps * (1.0 + 1e-12)
        && fast.VelocityMps.Size() >= params.MaxSpeedMps * (1.0 - 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistGravityTest, "SOLTest.FlightStep.AssistIgnoresGravity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// In assist mode the gravity argument has no effect (thrusters cancel it)
bool FSOLFlightStepAssistGravityTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.Thrust = FVector3d(0.3, -0.2, 0.1);
    control.ReferenceVelocityMps = FVector3d(3.0, 4.0, 5.0);
    const FSOLShipState start = FlightStepMakeState(FVector3d(-7.0, 1.0, 2.0), FLIGHT_STEP_YAW90);
    const FSOLShipState withoutGravity = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, 0.5);
    const FSOLShipState withGravity = SOLFlight::Step(start, control, params, FVector3d(0.0, 0.0, -9.81), 0.5);
    TestTrue(TEXT("Velocity is identical with and without gravity"),
        (withGravity.VelocityMps - withoutGravity.VelocityMps).Size() <= 1e-12);
    TestTrue(TEXT("Position is identical with and without gravity"),
        (withGravity.PositionM - withoutGravity.PositionM).Size() <= 1e-9);

    // And the result actually moved toward the target (guards against a do-nothing step)
    const FVector3d target = control.ReferenceVelocityMps
        + FLIGHT_STEP_YAW90.RotateVector(control.Thrust) * control.SpeedCapMps;
    TestTrue(TEXT("Velocity follows the assist relaxation"), (withGravity.VelocityMps
        - FlightStepExpectedAssist(start.VelocityMps, target, 0.5, params.AssistTimeConstantS)).Size() <= 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepZeroDtTest, "SOLTest.FlightStep.ZeroDtLeavesStateUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero time step changes nothing in either mode, even with thrust, rotation and gravity
bool FSOLFlightStepZeroDtTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipState start = FlightStepMakeState(FVector3d(12.0, -34.0, 56.0), FQuat4d(FVector3d(1.0, 2.0, 3.0)
        .GetSafeNormal(), 0.8));
    start.AngularVelocityRadPerS = FVector3d(0.1, -0.2, 0.3);
    for (const bool bAssist : { true, false })
    {
        FSOLShipControl control;
        control.bFlightAssist = bAssist;
        control.Thrust = FVector3d(1.0, 0.5, -0.5);
        control.Rotation = FVector3d(1.0, -1.0, 0.5);
        control.bBoost = true;
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d(0.0, 0.0, -9.81), 0.0);
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Newtonian");
        TestTrue(*FString::Printf(TEXT("%s: position unchanged"), mode), result.PositionM == start.PositionM);
        TestTrue(*FString::Printf(TEXT("%s: velocity unchanged"), mode),
            (result.VelocityMps - start.VelocityMps).Size() <= 1e-12);
        TestTrue(*FString::Printf(TEXT("%s: orientation unchanged"), mode),
            result.Orientation.Equals(start.Orientation, 1e-12));
        TestTrue(*FString::Printf(TEXT("%s: angular velocity unchanged"), mode),
            (result.AngularVelocityRadPerS - start.AngularVelocityRadPerS).Size() <= 1e-12);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepAssistSubstepTest, "SOLTest.FlightStep.AssistSmallStepsMatchOneBigStep",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The exact exponential relaxation gives the same velocity for 1000 small steps as for one big step
bool FSOLFlightStepAssistSubstepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.Thrust = FVector3d(0.8, 0.3, -0.2);
    control.SpeedCapMps = 25000.0;
    control.ReferenceVelocityMps = FVector3d(29780.0, -500.0, 12.0);
    const FSOLShipState start = FlightStepMakeState(FVector3d(-400.0, 900.0, 3.0), FLIGHT_STEP_YAW90);

    const double totalTime = 3.0;
    const FSOLShipState big = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, totalTime);
    FSOLShipState small = start;
    for (int32 step = 0; step < 1000; ++step)
    {
        small = SOLFlight::Step(small, control, params, FVector3d::ZeroVector, totalTime / 1000.0);
    }
    TestTrue(*FString::Printf(TEXT("Small steps (%.6f, %.6f, %.6f) == big step (%.6f, %.6f, %.6f)"),
        small.VelocityMps.X, small.VelocityMps.Y, small.VelocityMps.Z, big.VelocityMps.X, big.VelocityMps.Y,
        big.VelocityMps.Z), (small.VelocityMps - big.VelocityMps).Size() <= 1e-9 * big.VelocityMps.Size());

    // And both equal the closed form
    const FVector3d target = control.ReferenceVelocityMps
        + FLIGHT_STEP_YAW90.RotateVector(control.Thrust) * control.SpeedCapMps;
    TestTrue(TEXT("Big step matches the closed form"), (big.VelocityMps - FlightStepExpectedAssist(start.VelocityMps,
        target, totalTime, params.AssistTimeConstantS)).Size() <= 1e-9 * big.VelocityMps.Size());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepSemiImplicitTest, "SOLTest.FlightStep.PositionUsesUpdatedVelocity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Position advances by the post-update velocity times dt (semi-implicit Euler) in both modes
bool FSOLFlightStepSemiImplicitTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const double dt = 0.25;
    for (const bool bAssist : { true, false })
    {
        FSOLShipControl control;
        control.bFlightAssist = bAssist;
        control.Thrust = FVector3d(1.0, 0.0, 0.0);
        const FSOLShipState start = FlightStepMakeState(FVector3d(100.0, -20.0, 5.0), FQuat4d::Identity);
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d(0.0, 0.0, -2.0), dt);
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Newtonian");
        TestFalse(*FString::Printf(TEXT("%s: velocity changed"), mode),
            (result.VelocityMps - start.VelocityMps).IsNearlyZero(1e-6));
        TestTrue(*FString::Printf(TEXT("%s: position == start + new velocity * dt"), mode),
            (result.PositionM - (start.PositionM + result.VelocityMps * dt)).Size() <= 1e-6);
    }
    return true;
}

// --- Newtonian ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepNewtonMainTest, "SOLTest.FlightStep.NewtonianAccelerationsAlongShipAxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Forward/back thrust gives 50 m/s^2 and strafe/vertical 30 m/s^2 along the ship's axes (ship yawed 90 degrees)
bool FSOLFlightStepNewtonMainTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const double dt = 2.0;
    const FVector3d v0(7.0, -3.0, 1.0);
    const FSOLShipState start = FlightStepMakeState(v0, FLIGHT_STEP_YAW90);

    // After the yaw: forward +Y, right -X, up +Z
    struct FFlightStepNewtonCase
    {
        FVector3d Thrust;
        FVector3d ExpectedAccel;
    };
    const FFlightStepNewtonCase cases[] =
    {
        { FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 50.0, 0.0) },
        { FVector3d(-1.0, 0.0, 0.0), FVector3d(0.0, -50.0, 0.0) },
        { FVector3d(0.0, 1.0, 0.0), FVector3d(-30.0, 0.0, 0.0) },
        { FVector3d(0.0, -1.0, 0.0), FVector3d(30.0, 0.0, 0.0) },
        { FVector3d(0.0, 0.0, 1.0), FVector3d(0.0, 0.0, 30.0) },
        { FVector3d(0.0, 0.0, -0.5), FVector3d(0.0, 0.0, -15.0) },
        { FVector3d(1.0, 1.0, 0.0), FVector3d(-30.0 * UE_DOUBLE_INV_SQRT_2, 50.0 * UE_DOUBLE_INV_SQRT_2, 0.0) },
    };
    for (const FFlightStepNewtonCase& testCase : cases)
    {
        FSOLShipControl control;
        control.bFlightAssist = false;
        control.Thrust = testCase.Thrust;
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
        const FVector3d expectedVelocity = v0 + testCase.ExpectedAccel * dt;
        TestTrue(*FString::Printf(TEXT("Thrust (%.1f, %.1f, %.1f): velocity (%.6f, %.6f, %.6f) == (%.6f, %.6f, %.6f)"),
            testCase.Thrust.X, testCase.Thrust.Y, testCase.Thrust.Z, result.VelocityMps.X, result.VelocityMps.Y,
            result.VelocityMps.Z, expectedVelocity.X, expectedVelocity.Y, expectedVelocity.Z),
            (result.VelocityMps - expectedVelocity).Size() <= 1e-9);
        TestTrue(*FString::Printf(TEXT("Thrust (%.1f, %.1f, %.1f): position semi-implicit"), testCase.Thrust.X,
            testCase.Thrust.Y, testCase.Thrust.Z),
            (result.PositionM - (start.PositionM + expectedVelocity * dt)).Size() <= 1e-6);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepNewtonBoostTest, "SOLTest.FlightStep.NewtonianBoostTriplesThrust",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Boost multiplies the Newtonian acceleration by 3 (150 m/s^2 forward, 90 m/s^2 strafe)
bool FSOLFlightStepNewtonBoostTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.bFlightAssist = false;
    control.bBoost = true;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    const FSOLShipState forward = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, 1.0);
    TestTrue(TEXT("Boosted forward: 150 m/s after 1 s"),
        (forward.VelocityMps - FVector3d(150.0, 0.0, 0.0)).Size() <= 1e-9);

    control.Thrust = FVector3d(0.0, 1.0, 0.0);
    const FSOLShipState strafe = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, 1.0);
    TestTrue(TEXT("Boosted strafe: 90 m/s after 1 s"), (strafe.VelocityMps - FVector3d(0.0, 90.0, 0.0)).Size() <= 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepNewtonGravityTest, "SOLTest.FlightStep.NewtonianAddsGravityNoDamping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Gravity adds to the velocity in Newtonian mode; with no thrust and no gravity the velocity persists (no damping)
bool FSOLFlightStepNewtonGravityTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.bFlightAssist = false;
    control.SpeedCapMps = 10.0;                     // cap and reference are ignored in Newtonian mode
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
    TestTrue(TEXT("Thrust and gravity add"),
        (thrusting.VelocityMps - (v0 + (gravity + FVector3d(0.0, 50.0, 0.0)) * 2.0)).Size() <= 1e-9);

    // No damping over many steps
    control.Thrust = FVector3d::ZeroVector;
    FSOLShipState coasting = start;
    for (int32 step = 0; step < 1000; ++step)
    {
        coasting = SOLFlight::Step(coasting, control, params, FVector3d::ZeroVector, 0.1);
    }
    TestTrue(TEXT("Velocity persists with zero thrust"), (coasting.VelocityMps - v0).Size() <= 1e-9);
    TestTrue(TEXT("Position advanced by v0 * 100 s"), (coasting.PositionM - (start.PositionM + v0 * 100.0)).Size()
        <= 1e-5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepNewtonMaxSpeedTest, "SOLTest.FlightStep.NewtonianClampsToMaxSpeed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Newtonian velocity above 0.5c is scaled down to MaxSpeedMps with its direction kept
bool FSOLFlightStepNewtonMaxSpeedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.bFlightAssist = false;
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    control.bBoost = true;

    // Just below the limit, then one boosted second pushes past it
    const FSOLShipState nearLimit = FlightStepMakeState(FVector3d(params.MaxSpeedMps - 10.0, 0.0, 0.0),
        FQuat4d::Identity);
    const FSOLShipState result = SOLFlight::Step(nearLimit, control, params, FVector3d::ZeroVector, 1.0);
    TestTrue(*FString::Printf(TEXT("Clamped to MaxSpeedMps: %.6f"), result.VelocityMps.X),
        FMath::Abs(result.VelocityMps.Size() - params.MaxSpeedMps) <= 1e-6);
    TestTrue(TEXT("Direction kept"), SOLTestHelpers::VectorsNear(result.VelocityMps.GetSafeNormal(),
        FVector3d(1.0, 0.0, 0.0), 1e-12));

    // An over-limit diagonal velocity is scaled down along its own direction
    control.Thrust = FVector3d::ZeroVector;
    const FVector3d overDir = FVector3d(1.0, -2.0, 2.0) / 3.0;
    const FSOLShipState over = FlightStepMakeState(overDir * (2.0 * params.MaxSpeedMps), FQuat4d::Identity);
    const FSOLShipState scaled = SOLFlight::Step(over, control, params, FVector3d::ZeroVector, 0.1);
    TestTrue(TEXT("Over-limit velocity scaled to MaxSpeedMps along its direction"),
        SOLTestHelpers::VectorsNear(scaled.VelocityMps, overDir * params.MaxSpeedMps, 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepNewtonOrientTest, "SOLTest.FlightStep.NewtonianThrustFollowsRotatedShip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After the ship yaws 90 degrees right through Step, forward thrust accelerates along the new forward (+Y)
bool FSOLFlightStepNewtonOrientTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);

    // 10 steady steps of 0.1 s at 90 deg/s yaw = 90 degrees
    for (int32 step = 0; step < 10; ++step)
    {
        state = FlightStepSteadyRotate(state, FVector3d(0.0, 0.0, 1.0), 0.1, false);
    }
    TestTrue(TEXT("Forward is +Y after the yaw"), SOLTestHelpers::VectorsNear(
        state.Orientation.RotateVector(FVector3d(1.0, 0.0, 0.0)), FVector3d(0.0, 1.0, 0.0), 1e-9));

    // Stop rotating, then thrust forward for one second
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    FSOLShipControl control;
    control.bFlightAssist = false;
    control.Thrust = FVector3d(1.0, 0.0, 0.0);
    const FSOLShipState result = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 1.0);
    TestTrue(*FString::Printf(TEXT("Velocity (%.6f, %.6f, %.6f) == (0, 50, 0)"), result.VelocityMps.X,
        result.VelocityMps.Y, result.VelocityMps.Z),
        (result.VelocityMps - FVector3d(0.0, 50.0, 0.0)).Size() <= 1e-6);
    return true;
}

// --- Rotation ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepRotSignsTest, "SOLTest.FlightStep.RotationSignConventions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From identity: +pitch raises the nose, +yaw turns right, +roll drops the right wing (up gains +right)
bool FSOLFlightStepRotSignsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    const double dt = 0.1;
    for (const bool bAssist : { true, false })
    {
        const TCHAR* mode = bAssist ? TEXT("Assist") : TEXT("Newtonian");
        FSOLShipControl control;
        control.bFlightAssist = bAssist;

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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepRotFirstStepTest, "SOLTest.FlightStep.RotationFirstStepUsesRelaxedRate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From rest the stored rate relaxes first, then the orientation turns by the new rate * dt about the local axis
bool FSOLFlightStepRotFirstStepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    const double dt = 0.1;
    const double fraction = 1.0 - FMath::Exp(-dt / params.AngularResponseTimeS);
    const EFlightStepAxis axes[] = { EFlightStepAxis::Roll, EFlightStepAxis::Pitch, EFlightStepAxis::Yaw };
    for (const EFlightStepAxis axis : axes)
    {
        FSOLShipControl control;
        control.Rotation = FlightStepAxisInput(axis, 1.0);
        const FSOLShipState result = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
        const FVector3d expectedRate = FlightStepCommandedRate(control.Rotation, params) * fraction;
        const double angle = expectedRate.Size() * dt;
        const FFlightStepAxes expected = FlightStepRotatedAxes(FlightStepAxesOf(FQuat4d::Identity), axis, angle);
        TestTrue(*FString::Printf(TEXT("Axis %d: stored rate relaxed"), static_cast<int32>(axis)),
            (result.AngularVelocityRadPerS - expectedRate).Size() <= 1e-12);
        TestTrue(*FString::Printf(TEXT("Axis %d: turned by the relaxed rate * dt (%.9f rad)"), static_cast<int32>(axis),
            angle), FlightStepAxesNear(FlightStepAxesOf(result.Orientation), expected, 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepRotRelaxTest, "SOLTest.FlightStep.AngularVelocityRelaxesWithResponseTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After one AngularResponseTimeS the stored rate is (1 - e^-1) ~ 63% of the commanded rate, in one or many steps
bool FSOLFlightStepRotRelaxTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    FSOLShipControl control;
    control.Rotation = FVector3d(1.0, -0.5, 0.25);
    const FVector3d commanded = FlightStepCommandedRate(control.Rotation, params);
    const FVector3d expected = commanded * (1.0 - FMath::Exp(-1.0));

    // One step of exactly the response time
    const FSOLShipState one = SOLFlight::Step(start, control, params, FVector3d::ZeroVector,
        params.AngularResponseTimeS);
    TestTrue(*FString::Printf(TEXT("One step: rate (%.9f, %.9f, %.9f) == (%.9f, %.9f, %.9f)"),
        one.AngularVelocityRadPerS.X, one.AngularVelocityRadPerS.Y, one.AngularVelocityRadPerS.Z, expected.X,
        expected.Y, expected.Z), (one.AngularVelocityRadPerS - expected).Size() <= 1e-12);

    // Fifteen steps of a fifteenth give the same (exact exponential)
    FSOLShipState many = start;
    for (int32 step = 0; step < 15; ++step)
    {
        many = SOLFlight::Step(many, control, params, FVector3d::ZeroVector, params.AngularResponseTimeS / 15.0);
    }
    TestTrue(TEXT("Fifteen steps match one step"), (many.AngularVelocityRadPerS - expected).Size() <= 1e-12);

    // Releasing the input decays the rate toward zero
    FSOLShipControl released;
    const FSOLShipState decayed = SOLFlight::Step(one, released, params, FVector3d::ZeroVector,
        params.AngularResponseTimeS);
    TestTrue(TEXT("Released input decays by e^-1"),
        (decayed.AngularVelocityRadPerS - expected * FMath::Exp(-1.0)).Size() <= 1e-12);

    // Both flight modes give the same rotation
    FSOLShipControl newtonian = control;
    newtonian.bFlightAssist = false;
    const FSOLShipState oneNewton = SOLFlight::Step(start, newtonian, params, FVector3d::ZeroVector,
        params.AngularResponseTimeS);
    TestTrue(TEXT("Newtonian rate matches assist"),
        (oneNewton.AngularVelocityRadPerS - one.AngularVelocityRadPerS).Size() <= 1e-15);
    TestTrue(TEXT("Newtonian orientation matches assist"), oneNewton.Orientation.Equals(one.Orientation, 1e-15));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepNormalizedTest, "SOLTest.FlightStep.OrientationStaysNormalized",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After 100000 steps of varying rotation input the orientation quaternion is still unit length
bool FSOLFlightStepNormalizedTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    FSOLShipState state = FlightStepMakeState(FVector3d::ZeroVector, FQuat4d::Identity);
    FSOLShipControl control;
    for (int32 step = 0; step < 100000; ++step)
    {
        // Slowly varying inputs on all three axes
        const double t = step * 0.001;
        control.Rotation = FVector3d(FMath::Sin(t * 1.3), FMath::Cos(t * 0.7), FMath::Sin(t * 2.9 + 0.4));
        state = SOLFlight::Step(state, control, params, FVector3d::ZeroVector, 1.0 / 60.0);
    }
    TestTrue(*FString::Printf(TEXT("|q| = %.15f"), state.Orientation.Size()),
        FMath::Abs(state.Orientation.Size() - 1.0) <= 1e-9);
    TestTrue(TEXT("Orientation is finite"), FMath::IsFinite(state.Orientation.X) && FMath::IsFinite(state.Orientation.Y)
        && FMath::IsFinite(state.Orientation.Z) && FMath::IsFinite(state.Orientation.W));

    // The ship actually rotated (guards against a do-nothing step)
    TestFalse(TEXT("Orientation changed"), state.Orientation.Equals(FQuat4d::Identity, 1e-3));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepGimbalPitch90Test, "SOLTest.FlightStep.NoGimbalLockYawAfterPitch90",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After pitching the nose straight up, pure yaw still turns the ship about its local up axis by the commanded angle
bool FSOLFlightStepGimbalPitch90Test::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
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
    const double stepAngle = params.MaxPitchYawRateRadPerS * 0.1;
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepGimbalRateTest, "SOLTest.FlightStep.NoGimbalLockYawRateIndependentOfPitch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The yaw response at 89.9, 90 and 90.1 degrees of pitch has the same magnitude as at 0 degrees
bool FSOLFlightStepGimbalRateTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const double pitchesDeg[] = { 0.0, 89.9, 90.0, 90.1, -90.0, 179.0 };
    const double dt = 0.05;
    FSOLShipControl control;
    control.Rotation = FVector3d(0.0, 0.0, 1.0);
    const double fromRestAngle = params.MaxPitchYawRateRadPerS
        * (1.0 - FMath::Exp(-dt / params.AngularResponseTimeS)) * dt;
    const double steadyAngle = params.MaxPitchYawRateRadPerS * dt;
    for (const double pitchDeg : pitchesDeg)
    {
        const FQuat4d orientation = FlightStepPitchedOrientation(FMath::DegreesToRadians(pitchDeg));
        const FFlightStepAxes before = FlightStepAxesOf(orientation);
        const FVector3d expectedForward(FMath::Cos(FMath::DegreesToRadians(pitchDeg)), 0.0,
            FMath::Sin(FMath::DegreesToRadians(pitchDeg)));
        TestTrue(*FString::Printf(TEXT("Pitch %.1f: test orientation built correctly"), pitchDeg),
            SOLTestHelpers::VectorsNear(before.Forward, expectedForward, 1e-12));

        // From rest (the rate relaxes first)
        const FSOLShipState start = FlightStepMakeState(FVector3d::ZeroVector, orientation);
        const FSOLShipState fromRest = SOLFlight::Step(start, control, params, FVector3d::ZeroVector, dt);
        const FFlightStepAxes afterRest = FlightStepAxesOf(fromRest.Orientation);
        const double turnedRest = FlightStepAngleBetween(before.Forward, afterRest.Forward);
        TestTrue(*FString::Printf(TEXT("Pitch %.1f: yaw from rest %.12f == %.12f rad"), pitchDeg, turnedRest,
            fromRestAngle), FMath::Abs(turnedRest - fromRestAngle) <= 1e-10);
        TestTrue(*FString::Printf(TEXT("Pitch %.1f: yaw keeps the local up"), pitchDeg),
            SOLTestHelpers::VectorsNear(afterRest.Up, before.Up, 1e-10));

        // At the steady rate
        const FSOLShipState steady = FlightStepSteadyRotate(start, control.Rotation, dt, true);
        const FFlightStepAxes afterSteady = FlightStepAxesOf(steady.Orientation);
        TestTrue(*FString::Printf(TEXT("Pitch %.1f: steady yaw axes as expected"), pitchDeg), FlightStepAxesNear(
            afterSteady, FlightStepRotatedAxes(before, EFlightStepAxis::Yaw, steadyAngle), 1e-10));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightStepGimbalSequenceTest, "SOLTest.FlightStep.NoGimbalLockIndependentAxesAnywhere",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Roll, then pitch, then yaw are three independent local rotations at any starting orientation, including nose-up
bool FSOLFlightStepGimbalSequenceTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
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
            // Each single-axis step rotates only the two axes it should, by rate * dt
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
    const FSOLFlightParams params;
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
