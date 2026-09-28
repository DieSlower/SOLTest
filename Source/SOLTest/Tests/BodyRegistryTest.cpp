/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLBodyRegistry.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS


// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr int32 REGISTRY_BODY_COUNT = 9;
    constexpr int32 REGISTRY_EARTH = 3;

    // Contract reference data for one body: name, GM (m^3/s^2), mean radius (m), Standish a (AU) and e at J2000
    struct FSOLRegistryExpectedBody
    {
        const TCHAR* Name;
        double GM;
        double RadiusM;
        double SemiMajorAxisAU;
        double Eccentricity;
    };

    const FSOLRegistryExpectedBody REGISTRY_EXPECTED[REGISTRY_BODY_COUNT] =
    {
        { TEXT("Sun"), 1.32712440018e20, 6.957e8, 0.0, 0.0 },
        { TEXT("Mercury"), 2.2032e13, 2.4397e6, 0.38709927, 0.20563593 },
        { TEXT("Venus"), 3.24859e14, 6.0518e6, 0.72333566, 0.00677672 },
        { TEXT("Earth"), 3.986004418e14, 6.371e6, 1.00000261, 0.01671123 },
        { TEXT("Mars"), 4.282837e13, 3.3895e6, 1.52371034, 0.09339410 },
        { TEXT("Jupiter"), 1.26686534e17, 6.9911e7, 5.20288700, 0.04838624 },
        { TEXT("Saturn"), 3.7931187e16, 5.8232e7, 9.53667594, 0.05386179 },
        { TEXT("Uranus"), 5.793939e15, 2.5362e7, 19.18916464, 0.04725744 },
        { TEXT("Neptune"), 6.836529e15, 2.4622e7, 30.06992276, 0.00859048 },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns true when the registry holds the nine solar-system bodies (logs a failure otherwise)
    bool RegistryHasSolarSystem(FAutomationTestBase& test, const FSOLBodyRegistry& registry)
    {
        return test.TestEqual(TEXT("PopulateSolarSystem body count"), registry.Num(), REGISTRY_BODY_COUNT);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the heliocentric ecliptic longitude of a position in degrees, in [0, 360)
    double RegistryLongitudeDeg(const FVector3d& position)
    {
        double degrees = FMath::RadiansToDegrees(FMath::Atan2(position.Y, position.X));
        if (degrees < 0.0)
        {
            degrees += 360.0;
        }
        return degrees;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the signed angle in the XY plane swept from one position to the next, in radians
    double RegistrySweptAngleRad(const FVector3d& from, const FVector3d& to)
    {
        const double cross = from.X * to.Y - from.Y * to.X;
        const double dot = from.X * to.X + from.Y * to.Y;
        return FMath::Atan2(cross, dot);
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryNamesTest, "SOLTest.BodyRegistry.PopulateNamesAndOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PopulateSolarSystem adds Sun..Neptune in order with Sun as the only root and every planet parented to the Sun
bool FSOLBodyRegistryNamesTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    for (int32 index = 0; index < REGISTRY_BODY_COUNT; ++index)
    {
        const FName expectedName(REGISTRY_EXPECTED[index].Name);
        TestEqual(*FString::Printf(TEXT("Name of body %d"), index), registry.GetName(index).ToString(),
            FString(REGISTRY_EXPECTED[index].Name));
        TestEqual(*FString::Printf(TEXT("FindByName(%s)"), REGISTRY_EXPECTED[index].Name),
            registry.FindByName(expectedName), index);
        TestEqual(*FString::Printf(TEXT("Parent of %s"), REGISTRY_EXPECTED[index].Name), registry.GetParent(index),
            index == 0 ? INDEX_NONE : 0);
    }
    TestEqual(TEXT("Positions view size"), registry.GetPositionsM().Num(), REGISTRY_BODY_COUNT);
    TestEqual(TEXT("Velocities view size"), registry.GetVelocitiesMps().Num(), REGISTRY_BODY_COUNT);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryFindUnknownTest, "SOLTest.BodyRegistry.FindByNameUnknown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FindByName returns INDEX_NONE for names that are not registered, and finds registered ones
bool FSOLBodyRegistryFindUnknownTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry empty;
    TestEqual(TEXT("Empty registry: Earth absent"), empty.FindByName(FName(TEXT("Earth"))), INDEX_NONE);
    TestEqual(TEXT("Empty registry has no bodies"), empty.Num(), 0);

    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    TestEqual(TEXT("Pluto absent"), registry.FindByName(FName(TEXT("Pluto"))), INDEX_NONE);
    TestEqual(TEXT("Moon absent"), registry.FindByName(FName(TEXT("Moon"))), INDEX_NONE);
    TestEqual(TEXT("NAME_None absent"), registry.FindByName(NAME_None), INDEX_NONE);
    TestEqual(TEXT("Earth present at 3"), registry.FindByName(FName(TEXT("Earth"))), REGISTRY_EARTH);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryConstantsTest, "SOLTest.BodyRegistry.PhysicalConstants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each body's GM and mean radius equal the contract reference constants
bool FSOLBodyRegistryConstantsTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    for (int32 index = 0; index < REGISTRY_BODY_COUNT; ++index)
    {
        const FSOLRegistryExpectedBody& expected = REGISTRY_EXPECTED[index];
        TestTrue(*FString::Printf(TEXT("%s GM %.9e == %.9e"), expected.Name, registry.GetGM(index), expected.GM),
            SOLTestHelpers::RelativeError(registry.GetGM(index), expected.GM) < 1e-12);
        TestTrue(*FString::Printf(TEXT("%s radius %.6e == %.6e"), expected.Name, registry.GetRadiusM(index),
            expected.RadiusM), SOLTestHelpers::RelativeError(registry.GetRadiusM(index), expected.RadiusM) < 1e-12);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryPopulateClearsTest, "SOLTest.BodyRegistry.PopulateClearsExisting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PopulateSolarSystem clears previously added bodies and is repeatable
bool FSOLBodyRegistryPopulateClearsTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    FSOLBodyDef extra;
    extra.Name = FName(TEXT("Extra"));
    extra.RadiusM = 1.0;
    extra.GM = 1.0;
    registry.AddBody(extra);
    TestEqual(TEXT("One body before populate"), registry.Num(), 1);

    registry.PopulateSolarSystem();
    TestEqual(TEXT("Populate replaces the extra body"), registry.Num(), REGISTRY_BODY_COUNT);
    TestEqual(TEXT("Extra body is gone"), registry.FindByName(FName(TEXT("Extra"))), INDEX_NONE);
    TestEqual(TEXT("Sun is index 0 after populate"), registry.FindByName(FName(TEXT("Sun"))), 0);

    registry.PopulateSolarSystem();
    TestEqual(TEXT("Populating twice still gives nine bodies"), registry.Num(), REGISTRY_BODY_COUNT);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryAddBodyTest, "SOLTest.BodyRegistry.AddBodyReturnsIndex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AddBody returns sequential indices and stores the definition's name, radius, GM and parent
bool FSOLBodyRegistryAddBodyTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    FSOLBodyDef star;
    star.Name = FName(TEXT("TestStar"));
    star.RadiusM = 5.0e8;
    star.GM = 1.0e20;
    TestEqual(TEXT("First AddBody returns 0"), registry.AddBody(star), 0);

    FSOLBodyDef planet;
    planet.Name = FName(TEXT("TestPlanet"));
    planet.RadiusM = 4.0e6;
    planet.GM = 2.0e13;
    planet.ParentIndex = 0;
    planet.Elements.A0AU = 1.0;
    TestEqual(TEXT("Second AddBody returns 1"), registry.AddBody(planet), 1);

    TestEqual(TEXT("Num == 2"), registry.Num(), 2);
    if (registry.Num() != 2)
    {
        return false;
    }
    TestEqual(TEXT("Star name"), registry.GetName(0).ToString(), FString(TEXT("TestStar")));
    TestEqual(TEXT("Planet name"), registry.GetName(1).ToString(), FString(TEXT("TestPlanet")));
    TestEqual(TEXT("Star parent"), registry.GetParent(0), INDEX_NONE);
    TestEqual(TEXT("Planet parent"), registry.GetParent(1), 0);
    TestEqual(TEXT("Planet radius"), registry.GetRadiusM(1), 4.0e6, 0.0);
    TestEqual(TEXT("Planet GM"), registry.GetGM(1), 2.0e13, 0.0);
    TestEqual(TEXT("FindByName(TestPlanet)"), registry.FindByName(FName(TEXT("TestPlanet"))), 1);

    // A 1 AU circular orbit around a GM=1e20 star after Update
    registry.Update(0.0);
    TestTrue(TEXT("Root body stays at the origin"), registry.GetPositionM(0).IsNearlyZero(0.0));
    TestTrue(TEXT("Planet at 1 AU from the star"), SOLTestHelpers::RelativeError(registry.GetPositionM(1).Size(), SOLTestHelpers::AU_M) < 1e-9);
    TestTrue(TEXT("Planet speed sqrt(GM_parent/a) (parent GM is the central body)"),
        SOLTestHelpers::RelativeError(registry.GetVelocityMps(1).Size(), FMath::Sqrt(1.0e20 / SOLTestHelpers::AU_M)) < 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistrySunFixedTest, "SOLTest.BodyRegistry.SunFixedAtOrigin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Sun stays at the origin with zero velocity while planets are placed away from it
bool FSOLBodyRegistrySunFixedTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    const double times[] = { 0.0, 1.0e7, -3.0e8, 1.5e9 };
    for (const double time : times)
    {
        registry.Update(time);
        TestTrue(*FString::Printf(TEXT("t=%.0f Sun at origin"), time), registry.GetPositionM(0).IsNearlyZero(0.0));
        TestTrue(*FString::Printf(TEXT("t=%.0f Sun at rest"), time), registry.GetVelocityMps(0).IsNearlyZero(0.0));
        TestTrue(*FString::Printf(TEXT("t=%.0f Earth away from the Sun"), time),
            registry.GetPositionM(REGISTRY_EARTH).Size() > 0.9 * SOLTestHelpers::AU_M);

        // Views agree with the per-index getters
        const TConstArrayView<FVector3d> positions = registry.GetPositionsM();
        const TConstArrayView<FVector3d> velocities = registry.GetVelocitiesMps();
        if (positions.Num() == REGISTRY_BODY_COUNT && velocities.Num() == REGISTRY_BODY_COUNT)
        {
            for (int32 index = 0; index < REGISTRY_BODY_COUNT; ++index)
            {
                TestTrue(TEXT("Position view matches getter"), positions[index] == registry.GetPositionM(index));
                TestTrue(TEXT("Velocity view matches getter"), velocities[index] == registry.GetVelocityMps(index));
            }
        }
        else
        {
            AddError(TEXT("Position/velocity views do not hold nine bodies"));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryEarthJ2000Test, "SOLTest.BodyRegistry.EarthAtJ2000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At J2000 Earth is ~0.9833 AU from the Sun at ecliptic longitude ~100.4 deg moving at ~30.3 km/s
bool FSOLBodyRegistryEarthJ2000Test::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    registry.Update(0.0);
    const FVector3d earth = registry.GetPositionM(REGISTRY_EARTH);
    TestEqual(TEXT("Earth distance (AU)"), earth.Size() / SOLTestHelpers::AU_M, 0.9833, 0.001);
    TestEqual(TEXT("Earth ecliptic longitude (deg)"), RegistryLongitudeDeg(earth), 100.4, 0.5);
    TestEqual(TEXT("Earth speed (km/s)"), registry.GetVelocityMps(REGISTRY_EARTH).Size() / 1000.0, 30.3, 0.3);
    TestTrue(TEXT("Earth near the ecliptic plane"), FMath::Abs(earth.Z) < 1.0e-4 * earth.Size());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryEarthQuarterTest, "SOLTest.BodyRegistry.EarthQuarterYear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A quarter year after J2000 Earth has moved about 90 degrees counter-clockwise (seen from +Z)
bool FSOLBodyRegistryEarthQuarterTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    registry.Update(0.0);
    const FVector3d start = registry.GetPositionM(REGISTRY_EARTH);
    registry.Update(91.3125 * SOLTestHelpers::SECONDS_PER_DAY);
    const FVector3d quarter = registry.GetPositionM(REGISTRY_EARTH);
    const double sweptDeg = FMath::RadiansToDegrees(RegistrySweptAngleRad(start, quarter));
    TestEqual(TEXT("Earth sweeps ~90 deg in a quarter year"), sweptDeg, 90.0, 3.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryApsidesTest, "SOLTest.BodyRegistry.PlanetDistancesWithinApsides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every planet's Sun distance stays within [a(1-e), a(1+e)] over a year of sampled times
bool FSOLBodyRegistryApsidesTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }

    // Sample every 5 days over one year, plus a few years away from J2000
    TArray<double> times;
    for (double day = 0.0; day <= 365.25; day += 5.0)
    {
        times.Add(day * SOLTestHelpers::SECONDS_PER_DAY);
    }
    times.Add(-10.0 * 365.25 * SOLTestHelpers::SECONDS_PER_DAY);
    times.Add(20.0 * 365.25 * SOLTestHelpers::SECONDS_PER_DAY);

    for (int32 index = 1; index < REGISTRY_BODY_COUNT; ++index)
    {
        const FSOLRegistryExpectedBody& expected = REGISTRY_EXPECTED[index];
        const double a = expected.SemiMajorAxisAU * SOLTestHelpers::AU_M;
        const double slack = 0.002 * a;
        for (const double time : times)
        {
            registry.Update(time);
            const double r = registry.GetPositionM(index).Size();
            if (!TestTrue(*FString::Printf(TEXT("%s at t=%.0f: r=%.6f AU within [%.6f, %.6f] AU"), expected.Name, time,
                r / SOLTestHelpers::AU_M, a * (1.0 - expected.Eccentricity) / SOLTestHelpers::AU_M, a * (1.0 + expected.Eccentricity) / SOLTestHelpers::AU_M),
                r >= a * (1.0 - expected.Eccentricity) - slack && r <= a * (1.0 + expected.Eccentricity) + slack))
            {
                break;
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryEarthPeriodTest, "SOLTest.BodyRegistry.EarthPeriodFromPositions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Earth returns to its starting direction after about 365.25 days (+-0.1%)
bool FSOLBodyRegistryEarthPeriodTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }

    // Accumulate the swept angle day by day until it passes a full turn, then interpolate the crossing
    registry.Update(0.0);
    FVector3d previous = registry.GetPositionM(REGISTRY_EARTH);
    double swept = 0.0;
    double periodDays = -1.0;
    for (int32 day = 1; day <= 400; ++day)
    {
        registry.Update(day * SOLTestHelpers::SECONDS_PER_DAY);
        const FVector3d current = registry.GetPositionM(REGISTRY_EARTH);
        const double step = RegistrySweptAngleRad(previous, current);
        if (swept + step >= UE_DOUBLE_TWO_PI && step > 0.0)
        {
            periodDays = (day - 1) + (UE_DOUBLE_TWO_PI - swept) / step;
            break;
        }
        swept += step;
        previous = current;
    }
    TestTrue(TEXT("Earth completed a revolution within 400 days"), periodDays > 0.0);
    TestEqual(TEXT("Earth period from positions (days)"), periodDays, 365.25, 365.25 * 0.001);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryVelocityDerivativeTest,
    "SOLTest.BodyRegistry.VelocityMatchesPositionDerivative",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each planet's velocity matches the central finite difference of its position
bool FSOLBodyRegistryVelocityDerivativeTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    const double dt = 10.0;
    const double times[] = { 0.0, 1.234e8, -4.0e8 };
    for (const double time : times)
    {
        for (int32 index = 1; index < REGISTRY_BODY_COUNT; ++index)
        {
            registry.Update(time + dt);
            const FVector3d ahead = registry.GetPositionM(index);
            registry.Update(time - dt);
            const FVector3d behind = registry.GetPositionM(index);
            registry.Update(time);
            const FVector3d velocity = registry.GetVelocityMps(index);
            const FVector3d numeric = (ahead - behind) / (2.0 * dt);

            // Earth is held to 1e-4; others to 1e-3 because a Sun-only GM mean motion may differ from Standish rates
            const double tolerance = index == REGISTRY_EARTH ? 1e-4 : 1e-3;
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: |v|=%.3f vs numeric %.3f"), REGISTRY_EXPECTED[index].Name, time,
                velocity.Size(), numeric.Size()), numeric.Size() > 1000.0 && SOLTestHelpers::VectorsNear(velocity, numeric, tolerance));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryProgradeTest, "SOLTest.BodyRegistry.PlanetsOrbitPrograde",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every planet orbits counter-clockwise seen from the north ecliptic pole (angular momentum Z > 0)
bool FSOLBodyRegistryProgradeTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    registry.Update(0.0);
    for (int32 index = 1; index < REGISTRY_BODY_COUNT; ++index)
    {
        const FVector3d h = registry.GetPositionM(index) ^ registry.GetVelocityMps(index);
        TestTrue(*FString::Printf(TEXT("%s is prograde"), REGISTRY_EXPECTED[index].Name),
            h.Z > 0.9 * h.Size() && h.Size() > 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryChildBodyTest, "SOLTest.BodyRegistry.ChildBodyIsParentRelative",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A synthetic moon parented to Earth sits at Earth's position plus its own orbit, with Earth's GM as the central body
bool FSOLBodyRegistryChildBodyTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry reference;
    reference.PopulateSolarSystem();
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }

    // Circular moon orbit, 384,400 km, with negligible own GM
    const double moonA = 3.844e8;
    FSOLBodyDef moon;
    moon.Name = FName(TEXT("TestMoon"));
    moon.RadiusM = 1.7374e6;
    moon.GM = 1.0;
    moon.ParentIndex = REGISTRY_EARTH;
    moon.Elements.A0AU = moonA / SOLTestHelpers::AU_M;
    moon.Elements.I0Deg = 5.0;
    moon.Elements.L0Deg = 40.0;
    moon.Elements.LDotDegPerCy = 481267.88;
    moon.Elements.LongNode0Deg = 10.0;
    const int32 moonIndex = registry.AddBody(moon);
    TestEqual(TEXT("Moon index"), moonIndex, REGISTRY_BODY_COUNT);
    TestEqual(TEXT("Moon parent"), registry.GetParent(moonIndex), REGISTRY_EARTH);
    TestEqual(TEXT("Num includes the moon"), registry.Num(), REGISTRY_BODY_COUNT + 1);
    if (registry.Num() != REGISTRY_BODY_COUNT + 1)
    {
        return false;
    }

    const double times[] = { 0.0, 1.0e6, 3.3e7 };
    for (const double time : times)
    {
        registry.Update(time);
        reference.Update(time);
        const FVector3d earthPos = registry.GetPositionM(REGISTRY_EARTH);
        const FVector3d relativePos = registry.GetPositionM(moonIndex) - earthPos;
        const FVector3d relativeVel = registry.GetVelocityMps(moonIndex) - registry.GetVelocityMps(REGISTRY_EARTH);
        TestTrue(*FString::Printf(TEXT("t=%.0f adding a moon does not move Earth"), time),
            SOLTestHelpers::VectorsNear(earthPos, reference.GetPositionM(REGISTRY_EARTH), 1e-12));
        TestTrue(*FString::Printf(TEXT("t=%.0f moon distance from Earth %.3f == a"), time, relativePos.Size()),
            SOLTestHelpers::RelativeError(relativePos.Size(), moonA) < 1e-6);
        TestTrue(*FString::Printf(TEXT("t=%.0f moon relative speed %.6f == sqrt(GM_earth/a)"), time,
            relativeVel.Size()), SOLTestHelpers::RelativeError(relativeVel.Size(), FMath::Sqrt(SOLTestHelpers::EARTH_GM / moonA)) < 1e-6);
        TestTrue(*FString::Printf(TEXT("t=%.0f moon relative velocity perpendicular to radius"), time),
            FMath::Abs(relativePos.GetSafeNormal() | relativeVel.GetSafeNormal()) < 1e-6);
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
