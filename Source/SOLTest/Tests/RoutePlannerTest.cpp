/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLRoutePlanner.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #20 route planner (Docs/SDDs/20-hologram-map-and-move-orders.md section 2.3), written against the
// Map/SOLRoutePlanner.h contract before the production code existed
namespace
{
    constexpr double ROUTE_PLAN_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double ROUTE_PLAN_INF = std::numeric_limits<double>::infinity();
    constexpr double ROUTE_PLAN_G0 = 9.80665;                   // Standard gravity, restated for independence
    constexpr double ROUTE_PLAN_HUMAN_ACCEL = 1.5 * ROUTE_PLAN_G0;
    constexpr double ROUTE_PLAN_FAST_MPS = 1.0e6;               // A cap far above any corner speed here
    constexpr double ROUTE_PLAN_ANGLE_TOL = 1.0e-9;
    constexpr double ROUTE_PLAN_REL_TOL = 1.0e-9;

    //////////////////////////////////////////////////////////////////////////
    // Returns degrees in radians
    double RoutePlanRad(const double degrees)
    {
        return degrees * UE_DOUBLE_PI / 180.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a value is within a relative tolerance of the expected one (absolute near zero)
    bool RoutePlanNear(const double actual, const double expected, const double relTol)
    {
        return FMath::IsFinite(actual)
            && FMath::Abs(actual - expected) <= relTol * FMath::Max(1.0, FMath::Abs(expected));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a plan is exactly the stop plan
    bool RoutePlanIsStop(const FSOLCornerPlan& plan)
    {
        return plan.bStop && plan.PassSpeedMps == 0.0 && plan.CaptureRadiusM == 0.0;
    }
}

// --- TurnAngleRad ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerTurnAngleTest, "SOLTest.RoutePlanner.TurnAngle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The turn between the two legs: 0 straight, PI/2 right angle, PI reversal, 45 / 135 deg cases, at any scale
bool FSOLRoutePlannerTurnAngleTest::RunTest(const FString& /*parameters*/)
{
    // Checks the turn for one corner, relative to a base point
    const auto checkTurn = [this](const FVector3d& baseM, const double legScaleM, const TCHAR* scaleName,
                                  const double angleTol)
    {
        const FVector3d from = baseM;
        const FVector3d corner = baseM + FVector3d(legScaleM, 0.0, 0.0);
        struct FCase
        {
            const TCHAR* Name;
            FVector3d ToOffset;     // from the corner, in leg-scale units
            double ExpectedRad;
        };
        const FCase cases[] =
        {
            { TEXT("straight"), FVector3d(1.0, 0.0, 0.0), 0.0 },
            { TEXT("right angle"), FVector3d(0.0, 1.0, 0.0), UE_DOUBLE_PI / 2.0 },
            { TEXT("right angle up"), FVector3d(0.0, 0.0, 2.0), UE_DOUBLE_PI / 2.0 },
            { TEXT("reversal"), FVector3d(-1.0, 0.0, 0.0), UE_DOUBLE_PI },
            { TEXT("45 deg"), FVector3d(1.0, 1.0, 0.0), UE_DOUBLE_PI / 4.0 },
            { TEXT("135 deg"), FVector3d(-1.0, 1.0, 0.0), 3.0 * UE_DOUBLE_PI / 4.0 },
        };
        for (const FCase& entry : cases)
        {
            const double turn = SOLRoutePlanner::TurnAngleRad(from, corner, corner + entry.ToOffset * legScaleM);
            TestTrue(FString::Printf(TEXT("%s, %s: %.12f vs %.12f"), scaleName, entry.Name, turn, entry.ExpectedRad),
                FMath::IsFinite(turn) && FMath::Abs(turn - entry.ExpectedRad) <= angleTol);
        }
    };
    checkTurn(FVector3d::ZeroVector, 1000.0, TEXT("origin, 1 km legs"), ROUTE_PLAN_ANGLE_TOL);
    checkTurn(FVector3d(1.5e11, -2.0e10, 3.0e9), 5000.0, TEXT("1.5e11 m, 5 km legs"), 1.0e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerTurnAngleDegenerateTest, "SOLTest.RoutePlanner.TurnAngleDegenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A leg shorter than 1e-6 m or any non-finite input reads as a reversal (PI)
bool FSOLRoutePlannerTurnAngleDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d a(0.0, 0.0, 0.0);
    const FVector3d b(1000.0, 0.0, 0.0);
    const FVector3d c(2000.0, 0.0, 0.0);
    TestEqual(TEXT("from == corner"), SOLRoutePlanner::TurnAngleRad(a, a, c), UE_DOUBLE_PI);
    TestEqual(TEXT("corner == to"), SOLRoutePlanner::TurnAngleRad(a, b, b), UE_DOUBLE_PI);
    TestEqual(TEXT("all equal"), SOLRoutePlanner::TurnAngleRad(b, b, b), UE_DOUBLE_PI);
    TestEqual(TEXT("tiny in-leg"), SOLRoutePlanner::TurnAngleRad(b - FVector3d(1.0e-7, 0.0, 0.0), b, c),
        UE_DOUBLE_PI);
    TestEqual(TEXT("tiny out-leg"), SOLRoutePlanner::TurnAngleRad(a, b, b + FVector3d(0.0, 5.0e-7, 0.0)),
        UE_DOUBLE_PI);
    TestEqual(TEXT("NaN from"), SOLRoutePlanner::TurnAngleRad(FVector3d(ROUTE_PLAN_NAN, 0.0, 0.0), b, c),
        UE_DOUBLE_PI);
    TestEqual(TEXT("NaN corner"), SOLRoutePlanner::TurnAngleRad(a, FVector3d(1000.0, ROUTE_PLAN_NAN, 0.0), c),
        UE_DOUBLE_PI);
    TestEqual(TEXT("infinite to"), SOLRoutePlanner::TurnAngleRad(a, b, FVector3d(0.0, 0.0, ROUTE_PLAN_INF)),
        UE_DOUBLE_PI);

    // Legs of 1 mm (well above the 1e-6 m threshold) are real legs
    TestTrue(TEXT("1 mm legs are real"),
        FMath::Abs(SOLRoutePlanner::TurnAngleRad(a, FVector3d(1.0e-3, 0.0, 0.0), FVector3d(2.0e-3, 0.0, 0.0)))
        <= 1.0e-6);
    return true;
}

// --- PlanCorner ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerStopPlanTest, "SOLTest.RoutePlanner.StopPlan",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The stop plan; turns of 150 deg or more stop; invalid input stops; a custom stop angle is respected
bool FSOLRoutePlannerStopPlanTest::RunTest(const FString& /*parameters*/)
{
    using SOLRoutePlanner::PlanCorner;
    TestTrue(TEXT("MakeStopPlan"), RoutePlanIsStop(SOLRoutePlanner::MakeStopPlan()));
    TestTrue(TEXT("default plan is the stop plan"), RoutePlanIsStop(FSOLCornerPlan()));

    const FSOLRoutePlannerParams params;
    TestEqual(TEXT("default stop angle"), params.StopTurnAngleRad, RoutePlanRad(150.0), 1.0e-12);
    TestEqual(TEXT("default capture fraction"), params.CaptureLegFraction, 0.25);
    TestEqual(TEXT("default capture floor"), params.MinCaptureRadiusM, 1000.0);

    constexpr double legM = 1.0e5;
    TestTrue(TEXT("exactly 150 deg stops"), RoutePlanIsStop(PlanCorner(SOL::Route::STOP_TURN_ANGLE_RAD, legM, legM,
        ROUTE_PLAN_HUMAN_ACCEL, ROUTE_PLAN_FAST_MPS, params)));
    TestTrue(TEXT("170 deg stops"), RoutePlanIsStop(PlanCorner(RoutePlanRad(170.0), legM, legM,
        ROUTE_PLAN_HUMAN_ACCEL, ROUTE_PLAN_FAST_MPS, params)));
    TestTrue(TEXT("reversal stops"), RoutePlanIsStop(PlanCorner(UE_DOUBLE_PI, legM, legM, ROUTE_PLAN_HUMAN_ACCEL,
        ROUTE_PLAN_FAST_MPS, params)));
    TestFalse(TEXT("149 deg flies through"), PlanCorner(RoutePlanRad(149.0), legM, legM, ROUTE_PLAN_HUMAN_ACCEL,
        ROUTE_PLAN_FAST_MPS, params).bStop);

    // Invalid input
    TestTrue(TEXT("NaN angle"), RoutePlanIsStop(PlanCorner(ROUTE_PLAN_NAN, legM, legM, ROUTE_PLAN_HUMAN_ACCEL,
        ROUTE_PLAN_FAST_MPS, params)));
    TestTrue(TEXT("zero accel"), RoutePlanIsStop(PlanCorner(RoutePlanRad(90.0), legM, legM, 0.0,
        ROUTE_PLAN_FAST_MPS, params)));
    TestTrue(TEXT("negative accel"), RoutePlanIsStop(PlanCorner(RoutePlanRad(90.0), legM, legM, -1.0,
        ROUTE_PLAN_FAST_MPS, params)));
    TestTrue(TEXT("zero max speed"), RoutePlanIsStop(PlanCorner(RoutePlanRad(90.0), legM, legM,
        ROUTE_PLAN_HUMAN_ACCEL, 0.0, params)));
    TestTrue(TEXT("zero in-leg"), RoutePlanIsStop(PlanCorner(RoutePlanRad(90.0), 0.0, legM, ROUTE_PLAN_HUMAN_ACCEL,
        ROUTE_PLAN_FAST_MPS, params)));
    TestTrue(TEXT("negative out-leg"), RoutePlanIsStop(PlanCorner(RoutePlanRad(90.0), legM, -5.0,
        ROUTE_PLAN_HUMAN_ACCEL, ROUTE_PLAN_FAST_MPS, params)));
    TestTrue(TEXT("infinite leg"), RoutePlanIsStop(PlanCorner(RoutePlanRad(90.0), ROUTE_PLAN_INF, legM,
        ROUTE_PLAN_HUMAN_ACCEL, ROUTE_PLAN_FAST_MPS, params)));
    TestTrue(TEXT("NaN accel"), RoutePlanIsStop(PlanCorner(RoutePlanRad(90.0), legM, legM, ROUTE_PLAN_NAN,
        ROUTE_PLAN_FAST_MPS, params)));

    // A custom stop angle of 60 deg makes a 90 deg turn a stop
    FSOLRoutePlannerParams strict;
    strict.StopTurnAngleRad = RoutePlanRad(60.0);
    TestTrue(TEXT("custom stop angle"), RoutePlanIsStop(PlanCorner(RoutePlanRad(90.0), legM, legM,
        ROUTE_PLAN_HUMAN_ACCEL, ROUTE_PLAN_FAST_MPS, strict)));
    TestFalse(TEXT("custom stop angle: 45 deg still flies"), PlanCorner(RoutePlanRad(45.0), legM, legM,
        ROUTE_PLAN_HUMAN_ACCEL, ROUTE_PLAN_FAST_MPS, strict).bStop);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerRightAngleTest, "SOLTest.RoutePlanner.RightAngleCorner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 90 deg turn between 100 km and 40 km legs at 1.5 G: capture 10 km, pass sqrt(a * 10 km); a low cap and short legs
// clamp the speed and the radius
bool FSOLRoutePlannerRightAngleTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRoutePlannerParams params;
    const double a = ROUTE_PLAN_HUMAN_ACCEL;

    // captureMax = 0.25 * 40 km = 10 km, t = tan(45 deg) = 1
    const FSOLCornerPlan plan = SOLRoutePlanner::PlanCorner(RoutePlanRad(90.0), 1.0e5, 4.0e4, a, ROUTE_PLAN_FAST_MPS,
        params);
    TestFalse(TEXT("flies through"), plan.bStop);
    const double expectedPass = FMath::Sqrt(a * 1.0e4);
    TestTrue(FString::Printf(TEXT("pass %.6f == %.6f"), plan.PassSpeedMps, expectedPass),
        RoutePlanNear(plan.PassSpeedMps, expectedPass, ROUTE_PLAN_REL_TOL));
    TestTrue(FString::Printf(TEXT("capture %.6f == 10000"), plan.CaptureRadiusM),
        RoutePlanNear(plan.CaptureRadiusM, 1.0e4, ROUTE_PLAN_REL_TOL));

    // The legs swapped give the same corner
    const FSOLCornerPlan swapped = SOLRoutePlanner::PlanCorner(RoutePlanRad(90.0), 4.0e4, 1.0e5, a,
        ROUTE_PLAN_FAST_MPS, params);
    TestTrue(TEXT("symmetric in the legs"), RoutePlanNear(swapped.PassSpeedMps, plan.PassSpeedMps, ROUTE_PLAN_REL_TOL)
        && RoutePlanNear(swapped.CaptureRadiusM, plan.CaptureRadiusM, ROUTE_PLAN_REL_TOL));

    // A 50 m/s cap: pass 50, capture max(1000, 50^2 / a * 1) = 1000
    const FSOLCornerPlan capped = SOLRoutePlanner::PlanCorner(RoutePlanRad(90.0), 1.0e5, 4.0e4, a, 50.0, params);
    TestFalse(TEXT("capped: flies through"), capped.bStop);
    TestTrue(TEXT("capped: pass is the cap"), RoutePlanNear(capped.PassSpeedMps, 50.0, ROUTE_PLAN_REL_TOL));
    TestTrue(TEXT("capped: capture at the floor"), RoutePlanNear(capped.CaptureRadiusM,
        FMath::Max(1000.0, 50.0 * 50.0 / a), ROUTE_PLAN_REL_TOL));

    // A cap high enough to need a capture radius above the floor: pass 150, capture 150^2 / a
    const FSOLCornerPlan midCap = SOLRoutePlanner::PlanCorner(RoutePlanRad(90.0), 1.0e5, 4.0e4, a, 150.0, params);
    TestTrue(TEXT("mid cap: pass is the cap"), RoutePlanNear(midCap.PassSpeedMps, 150.0, ROUTE_PLAN_REL_TOL));
    TestTrue(TEXT("mid cap: capture from the cap"), RoutePlanNear(midCap.CaptureRadiusM, 150.0 * 150.0 / a,
        ROUTE_PLAN_REL_TOL));

    // Short legs: captureMax floors at 1000 m, so pass = sqrt(a * 1000)
    const FSOLCornerPlan shortLegs = SOLRoutePlanner::PlanCorner(RoutePlanRad(90.0), 100.0, 100.0, a,
        ROUTE_PLAN_FAST_MPS, params);
    TestFalse(TEXT("short legs: flies through"), shortLegs.bStop);
    TestTrue(TEXT("short legs: capture 1000"), RoutePlanNear(shortLegs.CaptureRadiusM, 1000.0, ROUTE_PLAN_REL_TOL));
    TestTrue(TEXT("short legs: pass from the floor"), RoutePlanNear(shortLegs.PassSpeedMps, FMath::Sqrt(a * 1000.0),
        ROUTE_PLAN_REL_TOL));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerStraightTest, "SOLTest.RoutePlanner.StraightCorner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A straight-through waypoint passes at the cap with a capture radius inside [floor, captureMax]
bool FSOLRoutePlannerStraightTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRoutePlannerParams params;
    const double caps[] = { 50.0, 3000.0, ROUTE_PLAN_FAST_MPS };
    for (const double cap : caps)
    {
        const FSOLCornerPlan plan = SOLRoutePlanner::PlanCorner(0.0, 1.0e5, 4.0e4, ROUTE_PLAN_HUMAN_ACCEL, cap,
            params);
        TestFalse(FString::Printf(TEXT("cap %.0f: flies through"), cap), plan.bStop);
        TestTrue(FString::Printf(TEXT("cap %.0f: pass is the cap"), cap),
            RoutePlanNear(plan.PassSpeedMps, cap, ROUTE_PLAN_REL_TOL));
        TestTrue(FString::Printf(TEXT("cap %.0f: capture %.3f in [1000, 10000]"), cap, plan.CaptureRadiusM),
            plan.CaptureRadiusM >= 1000.0 - 1.0e-9 && plan.CaptureRadiusM <= 1.0e4 + 1.0e-9);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerMonotonicTest, "SOLTest.RoutePlanner.CornerMonotonicAndInBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sharper turns never pass faster, more G never passes slower, the radius stays in range, and the lateral accel of
// the plan's own turn (pass^2 / (capture / t)) stays within the budget
bool FSOLRoutePlannerMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRoutePlannerParams params;
    const double legsM[][2] = { { 1.0e5, 4.0e4 }, { 3.0e3, 2.0e3 }, { 1.0e8, 5.0e7 }, { 500.0, 800.0 } };
    const double accels[] = { 1.0 * ROUTE_PLAN_G0, ROUTE_PLAN_HUMAN_ACCEL, 9.0 * ROUTE_PLAN_G0, 20.0 * ROUTE_PLAN_G0 };
    const double caps[] = { 200.0, 5.0e4, ROUTE_PLAN_FAST_MPS };
    for (const auto& legs : legsM)
    {
        const double captureMax = FMath::Max(1000.0, 0.25 * FMath::Min(legs[0], legs[1]));
        for (const double cap : caps)
        {
            // Sharper turns: pass speed non-increasing; every plan in range and in budget
            for (const double a : accels)
            {
                double previousPass = TNumericLimits<double>::Max();
                bool bMonotonic = true;
                bool bInRange = true;
                bool bInBudget = true;
                for (int32 degrees = 1; degrees < 150; degrees += 4)
                {
                    const double turn = RoutePlanRad(degrees);
                    const FSOLCornerPlan plan = SOLRoutePlanner::PlanCorner(turn, legs[0], legs[1], a, cap, params);
                    bInRange &= !plan.bStop && plan.PassSpeedMps > 0.0 && plan.PassSpeedMps <= cap * (1.0 + 1.0e-12)
                        && plan.CaptureRadiusM >= 1000.0 - 1.0e-9 && plan.CaptureRadiusM <= captureMax + 1.0e-9;
                    bMonotonic &= plan.PassSpeedMps <= previousPass * (1.0 + 1.0e-12);
                    previousPass = plan.PassSpeedMps;
                    if (plan.CaptureRadiusM > params.MinCaptureRadiusM * (1.0 + 1.0e-9))
                    {
                        const double t = FMath::Tan(turn / 2.0);
                        const double lateral = plan.PassSpeedMps * plan.PassSpeedMps / (plan.CaptureRadiusM / t);
                        bInBudget &= lateral <= a * (1.0 + 1.0e-9);
                    }
                }
                const FString label = FString::Printf(TEXT("legs %.0f/%.0f, cap %.0f, a %.2f"), legs[0], legs[1], cap,
                    a);
                TestTrue(label + TEXT(": sharper turn never faster"), bMonotonic);
                TestTrue(label + TEXT(": pass and capture in range"), bInRange);
                TestTrue(label + TEXT(": lateral accel within budget"), bInBudget);
            }

            // More G: pass speed non-decreasing, for a few turns
            const double turns[] = { 20.0, 90.0, 140.0 };
            for (const double degrees : turns)
            {
                double previousPass = 0.0;
                bool bMonotonic = true;
                for (const double a : accels)
                {
                    const FSOLCornerPlan plan = SOLRoutePlanner::PlanCorner(RoutePlanRad(degrees), legs[0], legs[1], a,
                        cap, params);
                    bMonotonic &= plan.PassSpeedMps >= previousPass * (1.0 - 1.0e-12);
                    previousPass = plan.PassSpeedMps;
                }
                TestTrue(FString::Printf(TEXT("legs %.0f/%.0f, cap %.0f, %.0f deg: more G never slower"), legs[0],
                    legs[1], cap, degrees), bMonotonic);
            }
        }
    }
    return true;
}

// --- CornerAccelBudgetMps2 ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerAccelBudgetTest, "SOLTest.RoutePlanner.CornerAccelBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A human corners at min(sustained G, hull cap), an AI at the hull cap; invalid limits give 0
bool FSOLRoutePlannerAccelBudgetTest::RunTest(const FString& /*parameters*/)
{
    using SOLRoutePlanner::CornerAccelBudgetMps2;
    constexpr double tol = 1.0e-9;
    TestEqual(TEXT("human, 1.5 G pilot, 20 G hull"), CornerAccelBudgetMps2(ESOLPilotKind::Human, 1.5, 20.0),
        1.5 * ROUTE_PLAN_G0, tol);
    TestEqual(TEXT("human, 1.5 G pilot, 1 G hull"), CornerAccelBudgetMps2(ESOLPilotKind::Human, 1.5, 1.0),
        1.0 * ROUTE_PLAN_G0, tol);
    TestEqual(TEXT("AI, 20 G hull"), CornerAccelBudgetMps2(ESOLPilotKind::AI, 1.5, 20.0), 20.0 * ROUTE_PLAN_G0, tol);
    TestEqual(TEXT("AI ignores the pilot"), CornerAccelBudgetMps2(ESOLPilotKind::AI, 9.0, 20.0),
        20.0 * ROUTE_PLAN_G0, tol);

    TestEqual(TEXT("human, NaN pilot"), CornerAccelBudgetMps2(ESOLPilotKind::Human, ROUTE_PLAN_NAN, 20.0), 0.0);
    TestEqual(TEXT("human, zero pilot"), CornerAccelBudgetMps2(ESOLPilotKind::Human, 0.0, 20.0), 0.0);
    TestEqual(TEXT("human, negative pilot"), CornerAccelBudgetMps2(ESOLPilotKind::Human, -1.5, 20.0), 0.0);
    TestEqual(TEXT("human, zero hull"), CornerAccelBudgetMps2(ESOLPilotKind::Human, 1.5, 0.0), 0.0);
    TestEqual(TEXT("human, infinite hull"), CornerAccelBudgetMps2(ESOLPilotKind::Human, 1.5, ROUTE_PLAN_INF), 0.0);
    TestEqual(TEXT("AI, NaN hull"), CornerAccelBudgetMps2(ESOLPilotKind::AI, 1.5, ROUTE_PLAN_NAN), 0.0);
    TestEqual(TEXT("AI, negative hull"), CornerAccelBudgetMps2(ESOLPilotKind::AI, 1.5, -20.0), 0.0);
    return true;
}

// --- LegTimeS ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerLegTimeTest, "SOLTest.RoutePlanner.LegTime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Triangle and trapezoid profiles, one-ramp legs, speed clamping, and invalid input
bool FSOLRoutePlannerLegTimeTest::RunTest(const FString& /*parameters*/)
{
    using SOLRoutePlanner::LegTimeS;
    constexpr double tol = 1.0e-9;
    constexpr double huge = 1.0e9;
    TestEqual(TEXT("zero distance"), LegTimeS(0.0, 0.0, 0.0, 10.0, 100.0), 0.0);

    // Rest to rest without a cap: triangle, 2 * sqrt(d / a)
    TestEqual(TEXT("triangle"), LegTimeS(1000.0, 0.0, 0.0, 10.0, huge), 20.0, tol);

    // Rest to rest with a cap: 10 s up (500 m), 10 s down (500 m), 9000 m at 100 m/s
    TestEqual(TEXT("trapezoid"), LegTimeS(1.0e4, 0.0, 0.0, 10.0, 100.0), 110.0, tol);

    // Moving entry to rest: 5 s up (375 m), 10 s down (500 m), 9125 m at 100 m/s
    TestEqual(TEXT("trapezoid from 50 m/s"), LegTimeS(1.0e4, 50.0, 0.0, 10.0, 100.0), 106.25, tol);

    // At the cap in and out: pure cruise
    TestEqual(TEXT("cruise"), LegTimeS(5000.0, 100.0, 100.0, 10.0, 100.0), 50.0, tol);

    // The exit speed cannot be reached: one acceleration ramp over the whole leg
    const double rampUp = (FMath::Sqrt(0.0 + 2.0 * 10.0 * 100.0) - 0.0) / 10.0;
    TestEqual(TEXT("exit unreachable"), LegTimeS(100.0, 0.0, 1000.0, 10.0, huge), rampUp, tol);

    // Cannot brake enough: one braking ramp over the whole leg
    const double rampDown = (1000.0 - FMath::Sqrt(1000.0 * 1000.0 - 2.0 * 10.0 * 100.0)) / 10.0;
    TestEqual(TEXT("cannot brake enough"), LegTimeS(100.0, 1000.0, 0.0, 10.0, huge), rampDown, tol);

    // Speeds above the cap are clamped to it; negative speeds to 0
    TestEqual(TEXT("entry above the cap"), LegTimeS(5000.0, 500.0, 100.0, 10.0, 100.0), 50.0, tol);
    TestEqual(TEXT("both above the cap"), LegTimeS(5000.0, 500.0, 700.0, 10.0, 100.0), 50.0, tol);
    TestEqual(TEXT("negative entry is rest"), LegTimeS(1000.0, -50.0, 0.0, 10.0, huge), 20.0, tol);
    TestEqual(TEXT("negative exit is rest"), LegTimeS(1000.0, 0.0, -50.0, 10.0, huge), 20.0, tol);

    // Invalid input
    const double maxTime = TNumericLimits<double>::Max();
    TestEqual(TEXT("NaN distance"), LegTimeS(ROUTE_PLAN_NAN, 0.0, 0.0, 10.0, 100.0), maxTime);
    TestEqual(TEXT("negative distance"), LegTimeS(-1.0, 0.0, 0.0, 10.0, 100.0), maxTime);
    TestEqual(TEXT("zero accel"), LegTimeS(1000.0, 0.0, 0.0, 0.0, 100.0), maxTime);
    TestEqual(TEXT("negative accel"), LegTimeS(1000.0, 0.0, 0.0, -10.0, 100.0), maxTime);
    TestEqual(TEXT("zero cap"), LegTimeS(1000.0, 0.0, 0.0, 10.0, 0.0), maxTime);
    TestEqual(TEXT("NaN entry"), LegTimeS(1000.0, ROUTE_PLAN_NAN, 0.0, 10.0, 100.0), maxTime);
    TestEqual(TEXT("infinite accel"), LegTimeS(1000.0, 0.0, 0.0, ROUTE_PLAN_INF, 100.0), maxTime);
    return true;
}

// --- Progress ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRoutePlannerProgressTest, "SOLTest.RoutePlanner.Progress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// LegFraction is 1 - remaining / length in [0, 1]; RouteProgress is (legs + fraction) / total in [0, 1]
bool FSOLRoutePlannerProgressTest::RunTest(const FString& /*parameters*/)
{
    using SOLRoutePlanner::LegFraction;
    using SOLRoutePlanner::RouteProgress;
    constexpr double tol = 1.0e-12;
    TestEqual(TEXT("quarter left"), LegFraction(25.0, 100.0), 0.75, tol);
    TestEqual(TEXT("leg start"), LegFraction(100.0, 100.0), 0.0, tol);
    TestEqual(TEXT("leg end"), LegFraction(0.0, 100.0), 1.0, tol);
    TestEqual(TEXT("remaining beyond the length"), LegFraction(150.0, 100.0), 0.0, tol);
    TestEqual(TEXT("overshot"), LegFraction(-5.0, 100.0), 1.0, tol);
    TestEqual(TEXT("zero-length leg"), LegFraction(50.0, 0.0), 1.0, tol);
    TestEqual(TEXT("zero-length leg, zero remaining"), LegFraction(0.0, 0.0), 1.0, tol);

    TestEqual(TEXT("1.5 of 4"), RouteProgress(1, 4, 0.5), 0.375, tol);
    TestEqual(TEXT("start"), RouteProgress(0, 4, 0.0), 0.0, tol);
    TestEqual(TEXT("done"), RouteProgress(4, 4, 0.0), 1.0, tol);
    TestEqual(TEXT("clamped above"), RouteProgress(5, 4, 0.5), 1.0, tol);
    TestEqual(TEXT("clamped below"), RouteProgress(-3, 4, 0.0), 0.0, tol);
    TestEqual(TEXT("no legs"), RouteProgress(1, 0, 0.5), 0.0, tol);
    TestEqual(TEXT("negative legs"), RouteProgress(1, -2, 0.5), 0.0, tol);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
