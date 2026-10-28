/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlight.h"
#include "Flight/SOLNewtonianFlight.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #20 route fly-through in the rendezvous guidance (pass speed and capture radius, SDD 20 section
// 2.3), written against the FSOLGuidanceParams / SOLFlight::AutopilotStep contract before the production code existed.
// relativePositionM is target minus ship; relativeVelocityMps is ship minus target velocity
namespace
{
    constexpr double ROUTE_GUIDE_DT = 0.02;
    constexpr double ROUTE_GUIDE_ACCEL = 20.0;
    constexpr double ROUTE_GUIDE_FLIP_S = 1.0;
    constexpr double ROUTE_GUIDE_CAP_MPS = 1.0e5;

    //////////////////////////////////////////////////////////////////////////
    // Returns guidance params with fixed budgets and the default arrival values (no pass speed, no capture)
    FSOLGuidanceParams RouteGuideParams()
    {
        FSOLGuidanceParams params;
        params.PlanAccelMps2 = ROUTE_GUIDE_ACCEL;
        params.BrakeAccelMps2 = ROUTE_GUIDE_ACCEL;
        params.FlipTimeS = ROUTE_GUIDE_FLIP_S;
        params.MaxCruiseSpeedMps = ROUTE_GUIDE_CAP_MPS;
        params.ArrivalRadiusM = 10.0;
        params.ArrivalSpeedMps = 1.0;
        params.VelocityGainPerS = 2.0;
        return params;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns guidance for a ship at relative position / velocity with no target accel or gravity
    FSOLGuidanceOutput RouteGuideEval(const FVector3d& relativePositionM, const FVector3d& relativeVelocityMps,
        const FSOLGuidanceParams& params, const double dt)
    {
        return SOLNewtonian::ComputeRendezvousGuidance(relativePositionM, relativeVelocityMps, FVector3d::ZeroVector,
            FVector3d::ZeroVector, params, dt);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two guidance outputs are identical in every field
    bool RouteGuideSameOutput(const FSOLGuidanceOutput& a, const FSOLGuidanceOutput& b)
    {
        return a.DesiredNetAccelMps2 == b.DesiredNetAccelMps2 && a.DesiredThrustAccelMps2 == b.DesiredThrustAccelMps2
            && a.DesiredForward == b.DesiredForward && a.Phase == b.Phase && a.DistanceM == b.DistanceM
            && a.ClosingSpeedMps == b.ClosingSpeedMps && a.SpeedLimitMps == b.SpeedLimitMps && a.EtaS == b.EtaS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the speed limit the contract expects with a pass speed: min(cap, sqrt(curve^2 + min(pass, cap)^2))
    double RouteGuideExpectedLimit(const double distanceM, const double passMps, const FSOLGuidanceParams& params,
        const double dt)
    {
        const double curve = SOLNewtonian::BrakingSpeedLimitMps(distanceM, params.BrakeAccelMps2,
            params.FlipTimeS + dt);
        const double pass = FMath::Min(passMps, params.MaxCruiseSpeedMps);
        return FMath::Min(params.MaxCruiseSpeedMps, FMath::Sqrt(curve * curve + pass * pass));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteGuidanceDefaultsTest, "SOLTest.RouteGuidance.DefaultsUnchanged",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The new fields default to 0, and explicit zeros give exactly the same output (far approach, braking, near arrival)
bool FSOLRouteGuidanceDefaultsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLGuidanceParams defaults;
    TestEqual(TEXT("pass speed defaults to 0"), defaults.PassSpeedMps, 0.0);
    TestEqual(TEXT("capture radius defaults to 0"), defaults.CaptureRadiusM, 0.0);

    const FSOLGuidanceParams implicitZero = RouteGuideParams();
    FSOLGuidanceParams explicitZero = RouteGuideParams();
    explicitZero.PassSpeedMps = 0.0;
    explicitZero.CaptureRadiusM = 0.0;

    struct FState
    {
        const TCHAR* Name;
        FVector3d RelativePositionM;
        FVector3d RelativeVelocityMps;
    };
    const FState states[] =
    {
        { TEXT("far approach at rest"), FVector3d(1.0e8, 0.0, 0.0), FVector3d::ZeroVector },
        { TEXT("braking"), FVector3d(1.0e4, 0.0, 0.0), FVector3d(2000.0, 0.0, 0.0) },
        { TEXT("near arrival"), FVector3d(3.0, 0.0, 0.0), FVector3d(0.0, 0.5, 0.0) },
        { TEXT("near but too fast"), FVector3d(3.0, 0.0, 0.0), FVector3d(0.0, 5.0, 0.0) },
    };
    for (const FState& state : states)
    {
        const FSOLGuidanceOutput a = RouteGuideEval(state.RelativePositionM, state.RelativeVelocityMps, implicitZero,
            ROUTE_GUIDE_DT);
        const FSOLGuidanceOutput b = RouteGuideEval(state.RelativePositionM, state.RelativeVelocityMps, explicitZero,
            ROUTE_GUIDE_DT);
        TestTrue(FString::Printf(TEXT("%s: identical output"), state.Name), RouteGuideSameOutput(a, b));
    }

    // The zero-pass speed limit is still the braking curve near the target
    const FSOLGuidanceOutput near = RouteGuideEval(FVector3d(1000.0, 0.0, 0.0), FVector3d::ZeroVector, implicitZero,
        ROUTE_GUIDE_DT);
    TestEqual(TEXT("zero pass: limit is the curve"), near.SpeedLimitMps, SOLNewtonian::BrakingSpeedLimitMps(1000.0,
        ROUTE_GUIDE_ACCEL, ROUTE_GUIDE_FLIP_S + ROUTE_GUIDE_DT), 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteGuidancePassSpeedTest, "SOLTest.RouteGuidance.PassSpeedLimit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With a pass speed the speed limit is min(cap, sqrt(curve^2 + pass^2)): the cap far away, above the zero-pass curve
// near the target, tending to the pass speed at the target; a pass speed above the cap is clamped to it
bool FSOLRouteGuidancePassSpeedTest::RunTest(const FString& /*parameters*/)
{
    constexpr double pass = 300.0;
    FSOLGuidanceParams params = RouteGuideParams();
    params.PassSpeedMps = pass;

    // Far: the curve is far above the cap
    const FSOLGuidanceOutput far = RouteGuideEval(FVector3d(1.0e10, 0.0, 0.0), FVector3d::ZeroVector, params,
        ROUTE_GUIDE_DT);
    TestEqual(TEXT("far: the cap"), far.SpeedLimitMps, ROUTE_GUIDE_CAP_MPS, 1.0e-6);

    // Near: above the zero-pass limit and per the formula
    const double distances[] = { 5.0e4, 1000.0, 100.0, 20.0 };
    for (const double d : distances)
    {
        const FSOLGuidanceOutput withPass = RouteGuideEval(FVector3d(d, 0.0, 0.0), FVector3d::ZeroVector, params,
            ROUTE_GUIDE_DT);
        const FSOLGuidanceOutput noPass = RouteGuideEval(FVector3d(d, 0.0, 0.0), FVector3d::ZeroVector,
            RouteGuideParams(), ROUTE_GUIDE_DT);
        const double expected = RouteGuideExpectedLimit(d, pass, params, ROUTE_GUIDE_DT);
        TestTrue(FString::Printf(TEXT("d %.0f: limit %.6f == %.6f"), d, withPass.SpeedLimitMps, expected),
            FMath::Abs(withPass.SpeedLimitMps - expected) <= 1.0e-9 * FMath::Max(1.0, expected));
        TestTrue(FString::Printf(TEXT("d %.0f: above the zero-pass limit %.6f"), d, noPass.SpeedLimitMps),
            withPass.SpeedLimitMps > noPass.SpeedLimitMps);
    }

    // Approaching the target the limit tends to the pass speed (a fast ship, so not the stop arrival)
    const FSOLGuidanceOutput atTarget = RouteGuideEval(FVector3d(1.0e-3, 0.0, 0.0), FVector3d(50.0, 0.0, 0.0), params,
        ROUTE_GUIDE_DT);
    const double expectedAtTarget = RouteGuideExpectedLimit(1.0e-3, pass, params, ROUTE_GUIDE_DT);
    TestTrue(FString::Printf(TEXT("at the target: limit %.6f == %.6f"), atTarget.SpeedLimitMps, expectedAtTarget),
        FMath::Abs(atTarget.SpeedLimitMps - expectedAtTarget) <= 1.0e-9 * expectedAtTarget);
    TestTrue(TEXT("at the target: about the pass speed"), FMath::Abs(atTarget.SpeedLimitMps - pass) < 0.5);

    // A pass speed above the cap is clamped to it
    FSOLGuidanceParams overCap = RouteGuideParams();
    overCap.MaxCruiseSpeedMps = 200.0;
    overCap.PassSpeedMps = 5000.0;
    const FSOLGuidanceOutput clamped = RouteGuideEval(FVector3d(1000.0, 0.0, 0.0), FVector3d::ZeroVector, overCap,
        ROUTE_GUIDE_DT);
    TestEqual(TEXT("pass above the cap: limit is the cap"), clamped.SpeedLimitMps, 200.0, 1.0e-9);
    const FSOLGuidanceOutput clampedNear = RouteGuideEval(FVector3d(1.0e-3, 0.0, 0.0), FVector3d(50.0, 0.0, 0.0),
        overCap, ROUTE_GUIDE_DT);
    TestEqual(TEXT("pass above the cap at the target: the cap"), clampedNear.SpeedLimitMps, 200.0, 1.0e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteGuidanceCaptureTest, "SOLTest.RouteGuidance.Capture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A capture radius arrives at any speed inside it or when this step's straight-line motion passes within it; without
// one the old rule (inside the arrival radius and slow) holds
bool FSOLRouteGuidanceCaptureTest::RunTest(const FString& /*parameters*/)
{
    constexpr double capture = 5000.0;
    FSOLGuidanceParams params = RouteGuideParams();
    params.PassSpeedMps = 300.0;
    params.CaptureRadiusM = capture;
    const auto isArrived = [](const FSOLGuidanceOutput& out)
    {
        return out.Phase == ESOLGuidancePhase::Arrived;
    };

    // Inside the radius at high speed, closing or sideways
    TestTrue(TEXT("inside, closing fast"), isArrived(RouteGuideEval(FVector3d(3000.0, 0.0, 0.0),
        FVector3d(2000.0, 0.0, 0.0), params, ROUTE_GUIDE_DT)));
    TestTrue(TEXT("inside, sideways fast"), isArrived(RouteGuideEval(FVector3d(3000.0, 0.0, 0.0),
        FVector3d(0.0, 2000.0, 0.0), params, ROUTE_GUIDE_DT)));
    TestTrue(TEXT("inside, receding fast"), isArrived(RouteGuideEval(FVector3d(3000.0, 0.0, 0.0),
        FVector3d(-2000.0, 0.0, 0.0), params, ROUTE_GUIDE_DT)));

    // Well outside: not arrived
    TestFalse(TEXT("far outside"), isArrived(RouteGuideEval(FVector3d(1.0e5, 0.0, 0.0),
        FVector3d(2000.0, 0.0, 0.0), params, ROUTE_GUIDE_DT)));

    // Swept: 30 m outside at 2000 m/s closing; 40 m in a 0.02 s step reaches the radius, 20 m in 0.01 s does not
    const FVector3d justOutside(capture + 30.0, 0.0, 0.0);
    const FVector3d closing(2000.0, 0.0, 0.0);
    TestTrue(TEXT("swept into the radius"), isArrived(RouteGuideEval(justOutside, closing, params, 0.02)));
    TestFalse(TEXT("too short a step to reach it"), isArrived(RouteGuideEval(justOutside, closing, params, 0.01)));

    // Swept past at an angle: the closest approach over the step is inside the radius although both ends are outside
    const FVector3d offsetStart(400.0, capture - 10.0, 0.0);   // |r| ~ 5006 m; closest approach 4990 m at X = 0
    const FVector3d crossing(40000.0, 0.0, 0.0);               // 800 m along X this step: ends at X = -400
    TestTrue(TEXT("start outside the radius"), offsetStart.Size() > capture);
    TestTrue(TEXT("end outside the radius"), (offsetStart - crossing * 0.02).Size() > capture);
    TestTrue(TEXT("swept past within the radius"), isArrived(RouteGuideEval(offsetStart, crossing, params, 0.02)));

    // Moving away outside the radius: not arrived
    TestFalse(TEXT("receding outside"), isArrived(RouteGuideEval(justOutside, -closing, params, 0.02)));

    // Without a capture radius the old rule holds
    FSOLGuidanceParams noCapture = params;
    noCapture.CaptureRadiusM = 0.0;
    TestFalse(TEXT("no capture: inside 5 km fast is not arrived"), isArrived(RouteGuideEval(FVector3d(3000.0, 0.0, 0.0),
        FVector3d(2000.0, 0.0, 0.0), noCapture, ROUTE_GUIDE_DT)));
    TestTrue(TEXT("no capture: inside the arrival radius and slow"), isArrived(RouteGuideEval(
        FVector3d(3.0, 0.0, 0.0), FVector3d(0.0, 0.5, 0.0), noCapture, ROUTE_GUIDE_DT)));
    TestFalse(TEXT("no capture: inside the arrival radius but fast"), isArrived(RouteGuideEval(
        FVector3d(3.0, 0.0, 0.0), FVector3d(0.0, 5.0, 0.0), noCapture, ROUTE_GUIDE_DT)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteGuidanceAutopilotTest, "SOLTest.RouteGuidance.AutopilotStepCapture",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AutopilotStep passes the pass speed and capture radius to the guidance: a fast ship inside the radius arrives, and
// the same state with the default (zero) arguments does not
bool FSOLRouteGuidanceAutopilotTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    const FSOLGMeterState meter;
    FSOLShipState ship;
    ship.PositionM = FVector3d(1.0e11, 2.0e10, 0.0);
    ship.VelocityMps = FVector3d(2000.0, 0.0, 0.0);
    const FVector3d targetM = ship.PositionM + FVector3d(3000.0, 0.0, 0.0);

    FSOLFlightStepOutput step;
    FSOLGuidanceOutput withCapture;
    SOLFlight::AutopilotStep(ship, targetM, FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector,
        params, meter, ROUTE_GUIDE_DT, &step, &withCapture, 0.0, 300.0, 5000.0);
    TestTrue(TEXT("inside the capture radius: Arrived"), withCapture.Phase == ESOLGuidancePhase::Arrived);

    FSOLGuidanceOutput withDefaults;
    SOLFlight::AutopilotStep(ship, targetM, FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector,
        params, meter, ROUTE_GUIDE_DT, &step, &withDefaults);
    TestTrue(TEXT("default arguments: not Arrived"), withDefaults.Phase != ESOLGuidancePhase::Arrived);

    FSOLGuidanceOutput explicitZero;
    SOLFlight::AutopilotStep(ship, targetM, FVector3d::ZeroVector, FVector3d::ZeroVector, FVector3d::ZeroVector,
        params, meter, ROUTE_GUIDE_DT, &step, &explicitZero, 0.0, 0.0, 0.0);
    TestTrue(TEXT("explicit zeros: same phase as the defaults"), explicitZero.Phase == withDefaults.Phase);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRouteGuidanceCaptureCurveTest, "SOLTest.RouteGuidance.CaptureCurve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With a capture radius the braking curve reaches the pass speed at the capture radius (the next leg starts there),
// i.e. it is the zero-capture curve shifted inward by the radius (adversarial review fix, SDD 20 Amendment 1)
bool FSOLRouteGuidanceCaptureCurveTest::RunTest(const FString& /*parameters*/)
{
    constexpr double capture = 2000.0;
    FSOLGuidanceParams withCapture = RouteGuideParams();
    withCapture.PassSpeedMps = 300.0;
    withCapture.CaptureRadiusM = capture;
    FSOLGuidanceParams noCapture = withCapture;
    noCapture.CaptureRadiusM = 0.0;
    const FVector3d closing(-100.0, 0.0, 0.0);
    for (const double distanceM : { 3000.0, 10000.0, 50000.0 })
    {
        const FSOLGuidanceOutput shifted = RouteGuideEval(FVector3d(distanceM, 0.0, 0.0), closing, withCapture,
            ROUTE_GUIDE_DT);
        const FSOLGuidanceOutput reference = RouteGuideEval(FVector3d(distanceM - capture, 0.0, 0.0), closing,
            noCapture, ROUTE_GUIDE_DT);
        TestEqual(FString::Printf(TEXT("speed limit at %.0f m equals the unshifted one at %.0f m"), distanceM,
            distanceM - capture), shifted.SpeedLimitMps, reference.SpeedLimitMps, 1.0e-6);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
