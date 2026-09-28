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
    // Contract Earth radius used by the gravity and orbit tests (meters)
    constexpr double FLIGHT_UTIL_EARTH_RADIUS_M = 6.371e6;

    //////////////////////////////////////////////////////////////////////////
    // Returns the gravity of a single body, computed through the arrays API
    FVector3d FlightUtilSingleGravity(const FVector3d& shipM, const FVector3d& bodyM, const double gm,
        const double radiusM)
    {
        const FVector3d positions[] = { bodyM };
        const double gms[] = { gm };
        const double radii[] = { radiusM };
        return SOLFlight::GravityAcceleration(shipM, positions, gms, radii);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ship state with distinctive non-default fields for collision tests
    FSOLShipState FlightUtilMakeShip(const FVector3d& positionM, const FVector3d& velocityMps)
    {
        FSOLShipState state;
        state.PositionM = positionM;
        state.VelocityMps = velocityMps;
        state.Orientation = FQuat4d(FVector3d(0.3, -0.4, 0.5).GetSafeNormal(), 0.77);
        state.AngularVelocityRadPerS = FVector3d(0.01, -0.02, 0.03);
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two ship states are bitwise identical field by field
    bool FlightUtilStatesIdentical(const FSOLShipState& a, const FSOLShipState& b)
    {
        return a.PositionM == b.PositionM && a.VelocityMps == b.VelocityMps
            && a.Orientation.X == b.Orientation.X && a.Orientation.Y == b.Orientation.Y
            && a.Orientation.Z == b.Orientation.Z && a.Orientation.W == b.Orientation.W
            && a.AngularVelocityRadPerS == b.AngularVelocityRadPerS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected circular-orbit tangent direction: normalize(cross(+Z, towardSun)), or +Y if parallel
    FVector3d FlightUtilExpectedTangent(const FVector3d& towardSun)
    {
        const FVector3d cross = FVector3d::CrossProduct(FVector3d(0.0, 0.0, 1.0), towardSun);
        return cross.SizeSquared() < 1e-24 ? FVector3d(0.0, 1.0, 0.0) : cross.GetSafeNormal();
    }
}

// --- GravityAcceleration ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilGravitySingleTest, "SOLTest.FlightUtil.GravitySingleBodyInverseSquare",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One body pulls with GM / r^2 toward its center, at several distances and directions
bool FSOLFlightUtilGravitySingleTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(1.0e9, -2.0e9, 3.0e8);
    const FVector3d offsets[] =
    {
        FVector3d(1.0e7, 0.0, 0.0),
        FVector3d(0.0, -3.0e7, 4.0e7),
        FVector3d(-2.0e8, 1.0e8, -5.0e7),
        FVector3d(1.5e11, 0.0, 0.0),
    };
    for (const FVector3d& offset : offsets)
    {
        const FVector3d shipM = bodyM + offset;
        const FVector3d accel = FlightUtilSingleGravity(shipM, bodyM, SOLTestHelpers::EARTH_GM,
            FLIGHT_UTIL_EARTH_RADIUS_M);
        const double r = offset.Size();
        const FVector3d expected = -offset / r * (SOLTestHelpers::EARTH_GM / (r * r));
        TestTrue(*FString::Printf(TEXT("r = %.3e: accel (%.6e, %.6e, %.6e) == (%.6e, %.6e, %.6e)"), r, accel.X,
            accel.Y, accel.Z, expected.X, expected.Y, expected.Z), SOLTestHelpers::VectorsNear(accel, expected, 1e-12));
    }

    // The Sun at 1 AU pulls about 5.93e-3 m/s^2
    const FVector3d sunPull = FlightUtilSingleGravity(FVector3d(SOLTestHelpers::AU_M, 0.0, 0.0), FVector3d::ZeroVector,
        SOLTestHelpers::SUN_GM, 6.957e8);
    TestTrue(*FString::Printf(TEXT("Sun at 1 AU: %.6e m/s^2 toward -X"), sunPull.X),
        SOLTestHelpers::RelativeError(-sunPull.X, 5.93e-3) < 1e-3 && FMath::Abs(sunPull.Y) + FMath::Abs(sunPull.Z) == 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilGravityEarthTest, "SOLTest.FlightUtil.GravityEarthSurface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With the contract Earth GM and radius, surface gravity is about 9.82 m/s^2 toward the center
bool FSOLFlightUtilGravityEarthTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d accel = FlightUtilSingleGravity(FVector3d(0.0, 0.0, FLIGHT_UTIL_EARTH_RADIUS_M),
        FVector3d::ZeroVector, SOLTestHelpers::EARTH_GM, FLIGHT_UTIL_EARTH_RADIUS_M);
    const double expected = SOLTestHelpers::EARTH_GM / (FLIGHT_UTIL_EARTH_RADIUS_M * FLIGHT_UTIL_EARTH_RADIUS_M);
    TestTrue(*FString::Printf(TEXT("Surface gravity %.6f ~ 9.82"), -accel.Z), FMath::Abs(-accel.Z - 9.82) < 0.01);
    TestTrue(TEXT("Surface gravity equals GM / R^2 downward"),
        SOLTestHelpers::VectorsNear(accel, FVector3d(0.0, 0.0, -expected), 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilGravitySumTest, "SOLTest.FlightUtil.GravitySuperposes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Several bodies add up; two equal bodies placed symmetrically cancel
bool FSOLFlightUtilGravitySumTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d shipM(3.0e8, 1.0e8, -2.0e7);
    const FVector3d positions[] = { FVector3d::ZeroVector, FVector3d(3.84e8, 0.0, 0.0), FVector3d(0.0, 5.0e8, 1.0e8) };
    const double gms[] = { SOLTestHelpers::EARTH_GM, 4.9048695e12, 1.0e15 };
    const double radii[] = { FLIGHT_UTIL_EARTH_RADIUS_M, 1.7374e6, 1.0e6 };
    const FVector3d total = SOLFlight::GravityAcceleration(shipM, positions, gms, radii);
    FVector3d expected = FVector3d::ZeroVector;
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(positions)); ++index)
    {
        expected += FlightUtilSingleGravity(shipM, positions[index], gms[index], radii[index]);
    }
    TestTrue(TEXT("Sum equals the sum of single-body results"), SOLTestHelpers::VectorsNear(total, expected, 1e-12));
    TestFalse(TEXT("Sum is non-zero"), total.IsNearlyZero(1e-12));

    // Symmetric pair around the ship cancels
    const FVector3d pairPositions[] = { shipM + FVector3d(1.0e7, 2.0e7, 0.0), shipM - FVector3d(1.0e7, 2.0e7, 0.0) };
    const double pairGms[] = { 1.0e14, 1.0e14 };
    const double pairRadii[] = { 1.0e6, 1.0e6 };
    const FVector3d pair = SOLFlight::GravityAcceleration(shipM, pairPositions, pairGms, pairRadii);
    const double single = 1.0e14 / FVector3d(1.0e7, 2.0e7, 0.0).SizeSquared();
    TestTrue(TEXT("Symmetric pair cancels"), pair.Size() <= 1e-12 * single);

    // Unequal pair: the net pull points to the heavier body
    const double unequalGms[] = { 3.0e14, 1.0e14 };
    const FVector3d unequal = SOLFlight::GravityAcceleration(shipM, pairPositions, unequalGms, pairRadii);
    TestTrue(TEXT("Unequal pair: net pull 2 * single toward the heavier body"), SOLTestHelpers::VectorsNear(unequal,
        FVector3d(1.0e7, 2.0e7, 0.0).GetSafeNormal() * (2.0 * single), 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilGravityInsideTest, "SOLTest.FlightUtil.GravityClampedInsideBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Inside the body radius the acceleration stays finite and never exceeds the surface value GM / R^2
bool FSOLFlightUtilGravityInsideTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(5.0e10, 5.0e10, 0.0);
    const double gm = SOLTestHelpers::EARTH_GM;
    const double radius = FLIGHT_UTIL_EARTH_RADIUS_M;
    const double surface = gm / (radius * radius);

    // At the surface: exactly GM / R^2
    const FVector3d atSurface = FlightUtilSingleGravity(bodyM + FVector3d(radius, 0.0, 0.0), bodyM, gm, radius);
    TestTrue(TEXT("At the surface: GM / R^2 toward the center"),
        SOLTestHelpers::VectorsNear(atSurface, FVector3d(-surface, 0.0, 0.0), 1e-12));

    // Inside: finite, toward the center, not stronger than at the surface
    const double fractions[] = { 0.999, 0.5, 1.0e-3, 1.0e-12 };
    for (const double fraction : fractions)
    {
        // Expected direction comes from the actual double positions passed in (tiny offsets round at this magnitude)
        const FVector3d shipM = bodyM + FVector3d(0.0, 0.6, -0.8) * (radius * fraction);
        const FVector3d offset = shipM - bodyM;
        const FVector3d accel = FlightUtilSingleGravity(shipM, bodyM, gm, radius);
        TestTrue(*FString::Printf(TEXT("Inside at %.3e R: finite"), fraction), SOLTestHelpers::IsFiniteVector(accel));
        TestTrue(*FString::Printf(TEXT("Inside at %.3e R: |a| = %.6f <= GM/R^2 = %.6f"), fraction, accel.Size(),
            surface), accel.Size() <= surface * (1.0 + 1e-12));
        TestTrue(*FString::Printf(TEXT("Inside at %.3e R: accel (%.6e, %.6e, %.6e) points toward the center, offset "
            "(%.6e, %.6e, %.6e)"), fraction, accel.X, accel.Y, accel.Z, offset.X, offset.Y, offset.Z),
            accel.IsNearlyZero(0.0)
            || FVector3d::DotProduct(accel / accel.Size(), -offset / offset.Size()) > 0.999999);
    }

    // Just inside the surface the pull is (nearly) the surface value, not a spike
    const FVector3d justInside = FlightUtilSingleGravity(bodyM + FVector3d(0.0, 0.0, radius * 0.999), bodyM, gm, radius);
    TestTrue(*FString::Printf(TEXT("Just inside: %.6f within 0.2%% of the surface value"), justInside.Size()),
        SOLTestHelpers::RelativeError(justInside.Size(), surface) < 2e-3);

    // Exactly at the center: finite (no NaN)
    const FVector3d atCenter = FlightUtilSingleGravity(bodyM, bodyM, gm, radius);
    TestTrue(TEXT("At the center: finite"), SOLTestHelpers::IsFiniteVector(atCenter));
    TestTrue(TEXT("At the center: not stronger than the surface"), atCenter.Size() <= surface * (1.0 + 1e-12));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilGravityEmptyTest, "SOLTest.FlightUtil.GravityNoBodiesIsZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Empty arrays give zero acceleration
bool FSOLFlightUtilGravityEmptyTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d accel = SOLFlight::GravityAcceleration(FVector3d(1.0, 2.0, 3.0), TConstArrayView<FVector3d>(),
        TConstArrayView<double>(), TConstArrayView<double>());
    TestTrue(TEXT("Zero with no bodies"), accel == FVector3d::ZeroVector);
    return true;
}

// --- StepSpeedCap ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilSpeedCapStepTest, "SOLTest.FlightUtil.SpeedCapLogSteps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Ten wheel steps multiply by 10, minus ten divide by 10, one step is 10^0.1, zero steps is identity
bool FSOLFlightUtilSpeedCapStepTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    TestEqual(TEXT("SpeedCapStepsPerDecade is 10"), SOLFlight::SpeedCapStepsPerDecade, 10.0, 0.0);
    TestTrue(TEXT("+10 steps: 1000 -> 10000"),
        SOLTestHelpers::RelativeError(SOLFlight::StepSpeedCap(1000.0, 10, params), 10000.0) < 1e-12);
    TestTrue(TEXT("-10 steps: 1000 -> 100"),
        SOLTestHelpers::RelativeError(SOLFlight::StepSpeedCap(1000.0, -10, params), 100.0) < 1e-12);
    TestTrue(TEXT("+1 step: 1000 -> 1000 * 10^0.1"),
        SOLTestHelpers::RelativeError(SOLFlight::StepSpeedCap(1000.0, 1, params), 1000.0 * FMath::Pow(10.0, 0.1))
        < 1e-12);
    TestTrue(TEXT("-3 steps: 1000 -> 1000 * 10^-0.3"),
        SOLTestHelpers::RelativeError(SOLFlight::StepSpeedCap(1000.0, -3, params), 1000.0 * FMath::Pow(10.0, -0.3))
        < 1e-12);
    TestTrue(TEXT("+25 steps: 3 -> 3 * 10^2.5"),
        SOLTestHelpers::RelativeError(SOLFlight::StepSpeedCap(3.0, 25, params), 3.0 * FMath::Pow(10.0, 2.5)) < 1e-12);
    TestEqual(TEXT("0 steps is identity"), SOLFlight::StepSpeedCap(1234.5, 0, params), 1234.5, 1e-9);

    // Up then down returns to the start
    const double upDown = SOLFlight::StepSpeedCap(SOLFlight::StepSpeedCap(777.0, 7, params), -7, params);
    TestTrue(TEXT("+7 then -7 returns to the start"), SOLTestHelpers::RelativeError(upDown, 777.0) < 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilSpeedCapClampTest, "SOLTest.FlightUtil.SpeedCapClampsToRange",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The cap is clamped to [MinSpeedCapMps, MaxSpeedMps] in both directions
bool FSOLFlightUtilSpeedCapClampTest::RunTest(const FString& /*parameters*/)
{
    const FSOLFlightParams params;
    TestEqual(TEXT("MinSpeedCapMps is 1 m/s"), params.MinSpeedCapMps, 1.0, 0.0);
    TestEqual(TEXT("Below min: 2 m/s - 10 steps -> 1 m/s"), SOLFlight::StepSpeedCap(2.0, -10, params), 1.0, 1e-12);
    TestEqual(TEXT("At min: -1 step stays 1 m/s"), SOLFlight::StepSpeedCap(1.0, -1, params), 1.0, 1e-12);
    TestEqual(TEXT("Above max: 1e8 + 10 steps -> 0.5c"), SOLFlight::StepSpeedCap(1.0e8, 10, params),
        params.MaxSpeedMps, 1e-6);
    TestEqual(TEXT("At max: +1 step stays 0.5c"), SOLFlight::StepSpeedCap(params.MaxSpeedMps, 1, params),
        params.MaxSpeedMps, 1e-6);
    TestEqual(TEXT("Huge step count clamps to 0.5c"), SOLFlight::StepSpeedCap(10.0, 1000, params), params.MaxSpeedMps,
        1e-6);
    TestEqual(TEXT("Huge negative step count clamps to 1 m/s"), SOLFlight::StepSpeedCap(1.0e6, -1000, params), 1.0,
        1e-12);

    // Out-of-range inputs are clamped even with 0 steps
    TestEqual(TEXT("Cap above max with 0 steps -> 0.5c"), SOLFlight::StepSpeedCap(1.0e9, 0, params),
        params.MaxSpeedMps, 1e-6);
    TestEqual(TEXT("Cap below min with 0 steps -> 1 m/s"), SOLFlight::StepSpeedCap(0.25, 0, params), 1.0, 1e-12);

    // Custom limits are honored
    FSOLFlightParams custom;
    custom.MinSpeedCapMps = 5.0;
    custom.MaxSpeedMps = 5000.0;
    TestEqual(TEXT("Custom min"), SOLFlight::StepSpeedCap(10.0, -10, custom), 5.0, 1e-12);
    TestEqual(TEXT("Custom max"), SOLFlight::StepSpeedCap(1000.0, 10, custom), 5000.0, 1e-9);
    return true;
}

// --- JoystickToRotation ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilJoystickDeadZoneTest, "SOLTest.FlightUtil.JoystickDeadZone",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Offsets inside or exactly on the dead zone give (0, 0); just outside gives a small non-zero value
bool FSOLFlightUtilJoystickDeadZoneTest::RunTest(const FString& /*parameters*/)
{
    const double deadZone = 0.1;
    const FVector2d inside[] =
    {
        FVector2d(0.0, 0.0),
        FVector2d(0.05, 0.0),
        FVector2d(-0.03, 0.04),
        FVector2d(0.1, 0.0),
        FVector2d(0.0, -0.1),
    };
    for (const FVector2d& offset : inside)
    {
        const FVector2d result = SOLFlight::JoystickToRotation(offset, deadZone, 1.0);
        TestTrue(*FString::Printf(TEXT("(%.3f, %.3f) -> (0, 0)"), offset.X, offset.Y), result == FVector2d::ZeroVector);
    }

    // Just beyond the dead zone
    const FVector2d outside = SOLFlight::JoystickToRotation(FVector2d(0.11, 0.0), deadZone, 1.0);
    TestTrue(*FString::Printf(TEXT("(0.11, 0) -> (%.6f, %.6f) == (0.01/0.9, 0)"), outside.X, outside.Y),
        FMath::Abs(outside.X - 0.01 / 0.9) < 1e-12 && outside.Y == 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilJoystickCurveTest, "SOLTest.FlightUtil.JoystickCurveAndDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Full deflection is unit magnitude, direction is preserved, and the exponent shapes the curve
bool FSOLFlightUtilJoystickCurveTest::RunTest(const FString& /*parameters*/)
{
    const double deadZone = 0.1;

    // Full deflection in several directions, including signs (right -> +yaw, up -> +pitch)
    const FVector2d edges[] =
    {
        FVector2d(1.0, 0.0),
        FVector2d(-1.0, 0.0),
        FVector2d(0.0, 1.0),
        FVector2d(0.0, -1.0),
        FVector2d(0.6, 0.8),
        FVector2d(-0.8, 0.6),
    };
    for (const FVector2d& edge : edges)
    {
        for (const double exponent : { 1.0, 2.0, 3.0 })
        {
            const FVector2d result = SOLFlight::JoystickToRotation(edge, deadZone, exponent);
            TestTrue(*FString::Printf(TEXT("Edge (%.1f, %.1f), exp %.0f -> (%.6f, %.6f)"), edge.X, edge.Y, exponent,
                result.X, result.Y), (result - edge).Size() < 1e-12);
        }
    }

    // Mid deflection: m = 0.5, direction (0.6, 0.8); s = (0.4 / 0.9)^exponent
    const FVector2d mid(0.3, 0.4);
    const FVector2d dir(0.6, 0.8);
    const double base = 0.4 / 0.9;
    const FVector2d linear = SOLFlight::JoystickToRotation(mid, deadZone, 1.0);
    TestTrue(*FString::Printf(TEXT("Linear mid (%.9f, %.9f)"), linear.X, linear.Y), (linear - dir * base).Size() < 1e-12);
    const FVector2d quadratic = SOLFlight::JoystickToRotation(mid, deadZone, 2.0);
    TestTrue(*FString::Printf(TEXT("Quadratic mid (%.9f, %.9f)"), quadratic.X, quadratic.Y),
        (quadratic - dir * (base * base)).Size() < 1e-12);

    // Negative quadrant keeps signs
    const FVector2d negative = SOLFlight::JoystickToRotation(FVector2d(-0.3, -0.4), deadZone, 1.0);
    TestTrue(TEXT("Negative offset keeps its direction"), (negative + dir * base).Size() < 1e-12);

    // Linear response is linear in m between the dead zone and the edge
    for (int32 index = 1; index <= 9; ++index)
    {
        const double m = deadZone + (1.0 - deadZone) * index / 10.0;
        const FVector2d result = SOLFlight::JoystickToRotation(FVector2d(0.0, m), deadZone, 1.0);
        TestTrue(*FString::Printf(TEXT("Linear at m = %.3f -> %.6f"), m, result.Y),
            FMath::Abs(result.Y - index / 10.0) < 1e-12 && result.X == 0.0);
    }

    // Zero dead zone with exponent 1 is the identity inside the unit circle
    const FVector2d identity = SOLFlight::JoystickToRotation(FVector2d(0.25, -0.5), 0.0, 1.0);
    TestTrue(TEXT("No dead zone, exponent 1: identity"), (identity - FVector2d(0.25, -0.5)).Size() < 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilJoystickClampTest, "SOLTest.FlightUtil.JoystickClampsBeyondUnit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Offsets longer than unit length are clamped to unit magnitude in the same direction
bool FSOLFlightUtilJoystickClampTest::RunTest(const FString& /*parameters*/)
{
    const FVector2d offsets[] = { FVector2d(3.0, 4.0), FVector2d(-2.0, 0.0), FVector2d(1.0, 1.0) };
    for (const FVector2d& offset : offsets)
    {
        const FVector2d result = SOLFlight::JoystickToRotation(offset, 0.1, 2.0);
        const FVector2d expected = offset.GetSafeNormal();
        TestTrue(*FString::Printf(TEXT("(%.1f, %.1f) -> (%.6f, %.6f) == (%.6f, %.6f)"), offset.X, offset.Y, result.X,
            result.Y, expected.X, expected.Y), (result - expected).Size() < 1e-12);
    }
    return true;
}

// --- ResolveSphereCollision ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilCollisionNoneTest, "SOLTest.FlightUtil.CollisionNoContactUntouched",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Without overlap (including exactly touching) the function returns false and leaves the state untouched
bool FSOLFlightUtilCollisionNoneTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(1.0e7, 2.0e7, 3.0e7);
    const FVector3d bodyV(100.0, -200.0, 300.0);
    const FVector3d offsets[] =
    {
        FVector3d(0.0, 0.0, 1000.0),
        FVector3d(0.0, 0.0, 510.0),         // exactly touching: not an overlap (distance < R + r is required)
        FVector3d(-3000.0, 4000.0, 0.0),
    };
    for (const FVector3d& offset : offsets)
    {
        const FSOLShipState original = FlightUtilMakeShip(bodyM + offset, bodyV + FVector3d(0.0, 0.0, -50.0));
        FSOLShipState state = original;
        const bool bContact = SOLFlight::ResolveSphereCollision(state, bodyM, bodyV, 500.0, 10.0);
        TestFalse(*FString::Printf(TEXT("Offset (%.0f, %.0f, %.0f): no contact"), offset.X, offset.Y, offset.Z),
            bContact);
        TestTrue(*FString::Printf(TEXT("Offset (%.0f, %.0f, %.0f): state untouched"), offset.X, offset.Y, offset.Z),
            FlightUtilStatesIdentical(state, original));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilCollisionPushTest, "SOLTest.FlightUtil.CollisionPushesOutAndSlides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A penetrating ship is moved to R + r along the outward direction; inward relative velocity is removed, tangential kept
bool FSOLFlightUtilCollisionPushTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(1.0e7, 2.0e7, 3.0e7);
    const FVector3d bodyV(100.0, -200.0, 300.0);
    const double bodyR = 500.0;
    const double shipR = 10.0;

    // Straight above, sinking with some sideways slide
    const FSOLShipState original = FlightUtilMakeShip(bodyM + FVector3d(0.0, 0.0, 400.0),
        bodyV + FVector3d(5.0, -6.0, -20.0));
    FSOLShipState state = original;
    TestTrue(TEXT("Above: contact"), SOLFlight::ResolveSphereCollision(state, bodyM, bodyV, bodyR, shipR));
    TestTrue(TEXT("Above: moved to R + r"), SOLTestHelpers::VectorsNear(state.PositionM,
        bodyM + FVector3d(0.0, 0.0, 510.0), 1e-15));
    TestTrue(*FString::Printf(TEXT("Above: velocity (%.6f, %.6f, %.6f) keeps the slide"), state.VelocityMps.X,
        state.VelocityMps.Y, state.VelocityMps.Z), (state.VelocityMps - (bodyV + FVector3d(5.0, -6.0, 0.0))).Size()
        <= 1e-9);
    TestTrue(TEXT("Above: orientation untouched"), state.Orientation.Equals(original.Orientation, 0.0));
    TestTrue(TEXT("Above: angular velocity untouched"), state.AngularVelocityRadPerS == original.AngularVelocityRadPerS);

    // Diagonal: (300, 400, 0) * 0.5 is 250 m out; normal (0.6, 0.8, 0)
    const FVector3d normal(0.6, 0.8, 0.0);
    const FVector3d tangent(-0.8, 0.6, 0.0);
    FSOLShipState diagonal = FlightUtilMakeShip(bodyM + normal * 250.0, bodyV + normal * -7.0 + tangent * 3.0
        + FVector3d(0.0, 0.0, 2.0));
    TestTrue(TEXT("Diagonal: contact"), SOLFlight::ResolveSphereCollision(diagonal, bodyM, bodyV, bodyR, shipR));
    TestTrue(TEXT("Diagonal: moved to R + r along the normal"),
        (diagonal.PositionM - (bodyM + normal * 510.0)).Size() <= 1e-6);
    TestTrue(TEXT("Diagonal: inward removed, tangential kept"),
        (diagonal.VelocityMps - (bodyV + tangent * 3.0 + FVector3d(0.0, 0.0, 2.0))).Size() <= 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilCollisionMovingTest, "SOLTest.FlightUtil.CollisionRelativeToMovingBody",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ship resting on a moving body keeps the body's velocity; an outward-moving ship keeps its own velocity
bool FSOLFlightUtilCollisionMovingTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(-4.0e10, 1.0e11, 0.0);
    const FVector3d bodyV(29780.0, 1000.0, -5.0);
    const double bodyR = FLIGHT_UTIL_EARTH_RADIUS_M;

    // Co-moving with the body, slightly penetrating
    FSOLShipState resting = FlightUtilMakeShip(bodyM + FVector3d(0.0, -(bodyR + 5.0), 0.0), bodyV);
    TestTrue(TEXT("Resting: contact"), SOLFlight::ResolveSphereCollision(resting, bodyM, bodyV, bodyR, 10.0));
    TestTrue(TEXT("Resting: keeps the body velocity"), (resting.VelocityMps - bodyV).Size() <= 1e-9);
    TestTrue(TEXT("Resting: moved out to R + r"),
        (resting.PositionM - (bodyM + FVector3d(0.0, -(bodyR + 10.0), 0.0))).Size() <= 1e-3);

    // Moving outward relative to the body, but absolutely moving inward: relative velocity decides, so it is kept
    const FVector3d outward = bodyV + FVector3d(0.0, -3.0, 0.0);
    FSOLShipState leaving = FlightUtilMakeShip(bodyM + FVector3d(0.0, -(bodyR + 5.0), 0.0), outward);
    TestTrue(TEXT("Leaving: contact"), SOLFlight::ResolveSphereCollision(leaving, bodyM, bodyV, bodyR, 10.0));
    TestTrue(TEXT("Leaving: keeps its velocity"), (leaving.VelocityMps - outward).Size() <= 1e-9);

    // Absolutely at rest but the body sweeps into it: the relative inward part is removed (ship picks up the body's
    // normal velocity, keeps its relative tangential velocity)
    // (ship on the +Y side; the body moves +1000 m/s along Y into it, so the relative normal velocity is -1000 m/s)
    FSOLShipState swept = FlightUtilMakeShip(bodyM + FVector3d(0.0, bodyR + 5.0, 0.0), FVector3d::ZeroVector);
    TestTrue(TEXT("Swept: contact"), SOLFlight::ResolveSphereCollision(swept, bodyM, bodyV, bodyR, 10.0));
    TestTrue(*FString::Printf(TEXT("Swept: velocity (%.6f, %.6f, %.6f) == (0, 1000, 0)"), swept.VelocityMps.X,
        swept.VelocityMps.Y, swept.VelocityMps.Z), (swept.VelocityMps - FVector3d(0.0, 1000.0, 0.0)).Size() <= 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilCollisionCenterTest, "SOLTest.FlightUtil.CollisionAtCenterPushesUpZ",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A ship exactly at the body's center is pushed out along +Z with no NaN
bool FSOLFlightUtilCollisionCenterTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(1.0e6, -1.0e6, 2.0e6);
    const FVector3d bodyV(10.0, 20.0, 30.0);
    FSOLShipState state = FlightUtilMakeShip(bodyM, bodyV + FVector3d(1.0, 2.0, -3.0));
    TestTrue(TEXT("Contact"), SOLFlight::ResolveSphereCollision(state, bodyM, bodyV, 1000.0, 10.0));
    TestTrue(TEXT("Position finite"), SOLTestHelpers::IsFiniteVector(state.PositionM));
    TestTrue(TEXT("Velocity finite"), SOLTestHelpers::IsFiniteVector(state.VelocityMps));
    TestTrue(TEXT("Pushed to R + r along +Z"),
        SOLTestHelpers::VectorsNear(state.PositionM, bodyM + FVector3d(0.0, 0.0, 1010.0), 1e-15));
    TestTrue(TEXT("Inward (-Z) relative velocity removed, tangential kept"),
        (state.VelocityMps - (bodyV + FVector3d(1.0, 2.0, 0.0))).Size() <= 1e-9);
    return true;
}

// --- MakeCircularOrbitState ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilOrbitStateTest, "SOLTest.FlightUtil.CircularOrbitStateGeometry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Position, speed, direction and orientation of the spawn state follow the contract
bool FSOLFlightUtilOrbitStateTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM(1.496e11, -2.0e9, 1.0e7);
    const FVector3d bodyV(-400.0, 29780.0, 3.0);
    const double gm = SOLTestHelpers::EARTH_GM;
    const double radius = FLIGHT_UTIL_EARTH_RADIUS_M;
    const double altitude = 1.0e7;
    const double r = radius + altitude;
    const double speed = FMath::Sqrt(gm / r);
    const FVector3d towardSuns[] =
    {
        FVector3d(-1.0, 0.0, 0.0),
        -bodyM.GetSafeNormal(),
        FVector3d(0.3, -0.5, 0.2).GetSafeNormal(),
        FVector3d(0.0, 0.0, 1.0),
        FVector3d(0.0, 0.0, -1.0),
    };
    for (const FVector3d& towardSun : towardSuns)
    {
        const FSOLShipState state = SOLFlight::MakeCircularOrbitState(bodyM, bodyV, gm, radius, altitude, towardSun);
        const FString label = FString::Printf(TEXT("TowardSun (%.3f, %.3f, %.3f)"), towardSun.X, towardSun.Y,
            towardSun.Z);
        const FVector3d radial = state.PositionM - bodyM;
        const FVector3d relativeV = state.VelocityMps - bodyV;
        const FVector3d tangent = FlightUtilExpectedTangent(towardSun);

        TestTrue(*FString::Printf(TEXT("%s: distance %.3f == R + h"), *label, radial.Size()),
            SOLTestHelpers::RelativeError(radial.Size(), r) < 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: on the sun-facing side"), *label),
            SOLTestHelpers::VectorsNear(radial, towardSun * r, 1e-9));
        TestTrue(*FString::Printf(TEXT("%s: relative speed %.6f == sqrt(GM/r) %.6f"), *label, relativeV.Size(), speed),
            SOLTestHelpers::RelativeError(relativeV.Size(), speed) < 1e-12);
        TestTrue(*FString::Printf(TEXT("%s: velocity perpendicular to the radius"), *label),
            FMath::Abs(FVector3d::DotProduct(relativeV.GetSafeNormal(), radial.GetSafeNormal())) < 1e-9);
        TestTrue(*FString::Printf(TEXT("%s: velocity along normalize(cross(+Z, TowardSun))"), *label),
            SOLTestHelpers::VectorsNear(relativeV, tangent * speed, 1e-12));
        TestTrue(*FString::Printf(TEXT("%s: forward == tangent"), *label), SOLTestHelpers::VectorsNear(
            state.Orientation.RotateVector(FVector3d(1.0, 0.0, 0.0)), tangent, 1e-9));
        TestTrue(*FString::Printf(TEXT("%s: up == radially outward"), *label), SOLTestHelpers::VectorsNear(
            state.Orientation.RotateVector(FVector3d(0.0, 0.0, 1.0)), towardSun, 1e-9));
        TestTrue(*FString::Printf(TEXT("%s: orientation normalized"), *label),
            FMath::Abs(state.Orientation.Size() - 1.0) < 1e-12);
        TestTrue(*FString::Printf(TEXT("%s: no angular velocity"), *label),
            state.AngularVelocityRadPerS.IsNearlyZero(0.0));
    }

    // Explicit expectations: TowardSun -X gives tangent -Y; TowardSun +/-Z gives +Y
    TestTrue(TEXT("cross(+Z, -X) is -Y"), SOLTestHelpers::VectorsNear(FlightUtilExpectedTangent(
        FVector3d(-1.0, 0.0, 0.0)), FVector3d(0.0, -1.0, 0.0), 1e-15));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilOrbitIntegrateTest, "SOLTest.FlightUtil.CircularOrbitStaysCircular",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Integrating the spawn state in Newtonian mode under the body's gravity keeps the radius within 0.5% for a quarter orbit
bool FSOLFlightUtilOrbitIntegrateTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d bodyM0(-1.0e11, 1.1e11, 0.0);
    const FVector3d bodyV(-21000.0, -19000.0, 0.0);
    const double gm = SOLTestHelpers::EARTH_GM;
    const double radius = FLIGHT_UTIL_EARTH_RADIUS_M;
    const double altitude = 4.0e5;
    const double r = radius + altitude;
    const FVector3d towardSun = -bodyM0.GetSafeNormal();

    FSOLShipState state = SOLFlight::MakeCircularOrbitState(bodyM0, bodyV, gm, radius, altitude, towardSun);
    const FSOLFlightParams params;
    FSOLShipControl control;
    control.bFlightAssist = false;

    // Step for a quarter period with the body moving uniformly
    const double period = UE_DOUBLE_TWO_PI * FMath::Sqrt(r * r * r / gm);
    const double dt = 0.5;
    const int32 steps = FMath::CeilToInt32(0.25 * period / dt);
    double maxError = 0.0;
    for (int32 step = 0; step < steps; ++step)
    {
        const FVector3d bodyNow = bodyM0 + bodyV * (step * dt);
        const FVector3d gravity = FlightUtilSingleGravity(state.PositionM, bodyNow, gm, radius);
        state = SOLFlight::Step(state, control, params, gravity, dt);
        const FVector3d bodyAfter = bodyM0 + bodyV * ((step + 1) * dt);
        maxError = FMath::Max(maxError, SOLTestHelpers::RelativeError((state.PositionM - bodyAfter).Size(), r));
    }
    TestTrue(*FString::Printf(TEXT("Max radius error over a quarter orbit %.6f%% < 0.5%%"), maxError * 100.0),
        maxError < 5e-3);

    // After a quarter orbit the ship has swept ~90 degrees around the body
    const FVector3d bodyEnd = bodyM0 + bodyV * (steps * dt);
    const FVector3d radialEnd = (state.PositionM - bodyEnd).GetSafeNormal();
    TestTrue(*FString::Printf(TEXT("Swept about 90 degrees (dot with start radial %.4f)"),
        FVector3d::DotProduct(radialEnd, towardSun)), FMath::Abs(FVector3d::DotProduct(radialEnd, towardSun)) < 0.02);
    return true;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Integrates a Newtonian circular orbit at dt = 1/30 s and checks radius and speed stay within 0.1% at every minute
    void FlightUtilCheckLongOrbit(FAutomationTestBase& test, const TCHAR* label, const double altitudeM,
        const double durationS, const bool bCheckSpeed)
    {
        const FVector3d bodyM(2.0e8, -3.0e8, 1.0e7);
        const double gm = SOLTestHelpers::EARTH_GM;
        const double radius = FLIGHT_UTIL_EARTH_RADIUS_M;
        const double r0 = radius + altitudeM;
        const double circularSpeed = FMath::Sqrt(gm / r0);

        FSOLShipState state = SOLFlight::MakeCircularOrbitState(bodyM, FVector3d::ZeroVector, gm, radius, altitudeM,
            FVector3d(-1.0, 0.0, 0.0));
        const FSOLFlightParams params;
        FSOLShipControl control;
        control.bFlightAssist = false;

        const double dt = 1.0 / 30.0;
        const int32 stepsPerMinute = 1800;
        const int32 minutes = FMath::RoundToInt32(durationS / 60.0);
        double maxRadiusError = 0.0;
        double maxSpeedError = 0.0;
        bool bFinite = true;
        for (int32 minute = 0; minute < minutes; ++minute)
        {
            for (int32 step = 0; step < stepsPerMinute; ++step)
            {
                const FVector3d gravity = FlightUtilSingleGravity(state.PositionM, bodyM, gm, radius);
                state = SOLFlight::Step(state, control, params, gravity, dt);
            }
            bFinite = bFinite && SOLTestHelpers::IsFiniteVector(state.PositionM)
                && SOLTestHelpers::IsFiniteVector(state.VelocityMps);
            maxRadiusError = FMath::Max(maxRadiusError,
                SOLTestHelpers::RelativeError((state.PositionM - bodyM).Size(), r0));
            maxSpeedError = FMath::Max(maxSpeedError, SOLTestHelpers::RelativeError(state.VelocityMps.Size(),
                circularSpeed));
        }
        test.TestTrue(*FString::Printf(TEXT("%s: state stays finite"), label), bFinite);
        test.TestTrue(*FString::Printf(TEXT("%s: max radius error %.6f%% < 0.1%%"), label, maxRadiusError * 100.0),
            maxRadiusError < 1e-3);
        if (bCheckSpeed)
        {
            test.TestTrue(*FString::Printf(TEXT("%s: max speed error %.6f%% < 0.1%%"), label, maxSpeedError * 100.0),
                maxSpeedError < 1e-3);
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilLongOrbitHighTest, "SOLTest.FlightUtil.LongOrbitHighStable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One hour at dt = 1/30 s on a 10,000 km-altitude circular Earth orbit keeps radius and speed within 0.1%
bool FSOLFlightUtilLongOrbitHighTest::RunTest(const FString& /*parameters*/)
{
    FlightUtilCheckLongOrbit(*this, TEXT("10,000 km for 1 h"), 1.0e7, 3600.0, true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLFlightUtilLongOrbitLowTest, "SOLTest.FlightUtil.LongOrbitLowStable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Thirty minutes at dt = 1/30 s on a 200 km-altitude circular Earth orbit keeps the radius within 0.1%
bool FSOLFlightUtilLongOrbitLowTest::RunTest(const FString& /*parameters*/)
{
    FlightUtilCheckLongOrbit(*this, TEXT("200 km for 30 min"), 2.0e5, 1800.0, false);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
