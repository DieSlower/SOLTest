/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLBodyRegistry.h"
#include "Universe/SOLBodyRotation.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS


// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Sun + 8 planets (indices 0-8, REGISTRY_EXPECTED), then 5 dwarf planets and 22 moons (REGISTRY_EXPECTED_CHILDREN)
    constexpr int32 REGISTRY_PLANET_COUNT = 9;
    constexpr int32 REGISTRY_CHILD_COUNT = 27;
    constexpr int32 REGISTRY_BODY_COUNT = REGISTRY_PLANET_COUNT + REGISTRY_CHILD_COUNT;    // 36 since issue #6 Part 5d
    constexpr int32 REGISTRY_EARTH = 3;

    constexpr int32 REGISTRY_VENUS = 2;
    constexpr int32 REGISTRY_MARS_HOST = 4;
    constexpr int32 REGISTRY_JUPITER_HOST = 5;
    constexpr int32 REGISTRY_SATURN_HOST = 6;
    constexpr int32 REGISTRY_URANUS_HOST = 7;
    constexpr int32 REGISTRY_NEPTUNE_HOST = 8;

    // Contract reference data for one body: name, GM (m^3/s^2), mean radius (m), Standish a (AU) and e at J2000,
    // then sidereal rotation period (h), axial tilt (deg, 0-180) and prime-meridian angle at J2000 (deg) per SDD 12
    struct FSOLRegistryExpectedBody
    {
        const TCHAR* Name;
        double GM;
        double RadiusM;
        double SemiMajorAxisAU;
        double Eccentricity;
        double RotationPeriodH;
        double AxialTiltDeg;
        double W0Deg;
    };

    const FSOLRegistryExpectedBody REGISTRY_EXPECTED[REGISTRY_PLANET_COUNT] =
    {
        { TEXT("Sun"), 1.32712440018e20, 6.957e8, 0.0, 0.0, 601.2, 7.25, 0.0 },
        { TEXT("Mercury"), 2.2032e13, 2.4397e6, 0.38709927, 0.20563593, 1407.6, 0.034, 0.0 },
        { TEXT("Venus"), 3.24859e14, 6.0518e6, 0.72333566, 0.00677672, 5832.6, 177.36, 0.0 },
        { TEXT("Earth"), 3.986004418e14, 6.371e6, 1.00000261, 0.01671123, 23.9345, 23.44, 0.0 },
        { TEXT("Mars"), 4.282837e13, 3.3895e6, 1.52371034, 0.09339410, 24.6229, 25.19, 0.0 },
        { TEXT("Jupiter"), 1.26686534e17, 6.9911e7, 5.20288700, 0.04838624, 9.9250, 3.13, 0.0 },
        { TEXT("Saturn"), 3.7931187e16, 5.8232e7, 9.53667594, 0.05386179, 10.656, 26.73, 0.0 },
        { TEXT("Uranus"), 5.793939e15, 2.5362e7, 19.18916464, 0.04725744, 17.24, 97.77, 0.0 },
        { TEXT("Neptune"), 6.836529e15, 2.4622e7, 30.06992276, 0.00859048, 16.11, 28.32, 0.0 },
    };

    // Contract reference data for one dwarf planet or moon (issue #6): name, parent index, whether its elements are
    // written in the host's equator frame (tilted by the host's AxialTiltDeg) or the ecliptic, GM (m^3/s^2), mean
    // radius (m), epoch elements (a AU, e, i deg, L deg, L rate deg/cy, longitude of periapsis deg, node deg;
    // every other rate is 0), real sidereal orbital period (days), rotation period (h), axial tilt (deg) and W0 (deg), and whether
    // the orbit is retrograde about the host's spin pole
    struct FSOLRegistryExpectedChild
    {
        const TCHAR* Name;
        int32 ParentIndex;
        bool bHostEquator;
        double GM;
        double RadiusM;
        double SemiMajorAxisAU;
        double Eccentricity;
        double InclinationDeg;
        double MeanLongitudeDeg;
        double MeanLongitudeRateDegPerCy;
        double LongPeriDeg;
        double LongNodeDeg;
        double OrbitalPeriodDays;
        double RotationPeriodH;
        double AxialTiltDeg;
        double W0Deg;
        bool bRetrograde;
    };

    // Same sourcing as the production table (JPL SSD satellite mean elements/physical parameters, JPL Horizons J2000
    // osculating angles, NASA/JPL periods), typed independently; L rate = 13,149,000 / period for moons
    const FSOLRegistryExpectedChild REGISTRY_EXPECTED_CHILDREN[REGISTRY_CHILD_COUNT] =
    {
        { TEXT("Ceres"), 0, false, 6.262736e10, 4.697e5,
            2.76649602, 0.07837563, 10.58336046, 160.593875, 7823.466439, 154.41722, 80.49435747, 1680.71,
            9.07417, 4.0, 0.0, false },
        { TEXT("Pluto"), 0, false, 8.696e11, 1.1883e6,
            39.4874155, 0.24897636, 17.14055931, 238.927469, 145.176864, 224.07872, 110.30125386, 90572.28,
            153.29352, 122.53, 0.0, false },
        { TEXT("Eris"), 0, false, 1.09899e12, 1.163e6,
            67.83513506, 0.43843, 43.99285596, 21.040722, 64.476795, 187.197725, 35.97652907, 203933.84,
            378.864, 0.0, 0.0, false },
        { TEXT("Makemake"), 0, false, 2.07e11, 7.15e5,
            45.4988931, 0.16038663, 29.00199284, 155.615831, 117.377332, 15.511621, 79.44279338, 112023.33,
            22.8266, 0.0, 0.0, false },
        { TEXT("Haumea"), 0, false, 2.67372e11, 7.98e5,
            43.1028675, 0.19500944, 28.20492617, 192.319956, 127.299364, 1.894889, 121.94764449, 103291.95,
            3.915341, 0.0, 0.0, false },
        { TEXT("Moon"), REGISTRY_EARTH, false, 4.9028e12, 1.7374e6,
            0.0025695553, 0.0549, 5.145396, 218.3164477, 481266.494, 83.3532465, 125.0445479, 27.321661,
            655.719864, 1.5424, 0.0, false },
        { TEXT("Phobos"), REGISTRY_MARS_HOST, true, 7.087e5, 1.108e4,
            0.000062668, 0.0151, 1.08, 215.511, 41231038.5904, 25.6886, 41.8325, 0.31891023,
            7.653846, 25.19, 0.0, false },
        { TEXT("Deimos"), REGISTRY_MARS_HOST, true, 9.62e4, 6.2e3,
            0.0001568004, 0.00033, 2.2279, 258.8668, 10415544.5011, 253.7731, 28.8065, 1.26244,
            30.29856, 25.19, 0.0, false },
        { TEXT("Io"), REGISTRY_JUPITER_HOST, true, 5.95991547e12, 1.82149e6,
            0.0028195588, 0.0041, 0.0375, 19.9404, 7432434.0953, 44.7872, 243.1625, 1.769137786,
            42.459307, 3.13, 0.0, false },
        { TEXT("Europa"), REGISTRY_JUPITER_HOST, true, 3.2027121e12, 1.5608e6,
            0.0044860264, 0.009, 0.4622, 214.4592, 3702711.8584, 229.0481, 180.0999, 3.551181,
            85.228344, 3.13, 0.0, false },
        { TEXT("Ganymede"), REGISTRY_JUPITER_HOST, true, 9.88783275e12, 2.6312e6,
            0.0071551821, 0.0013, 0.2069, 221.7946, 1837850.6768, 304.7455, 72.9129, 7.15455296,
            171.709271, 3.13, 0.0, false },
        { TEXT("Callisto"), REGISTRY_JUPITER_HOST, true, 7.1792834e12, 2.4103e6,
            0.0125850722, 0.0074, 0.1996, 80.9574, 787883.3665, 355.8387, 158.3252, 16.6890184,
            400.536442, 3.13, 0.0, false },
        { TEXT("Mimas"), REGISTRY_SATURN_HOST, true, 2.50349e9, 1.982e5,
            0.0012433332, 0.0196, 1.5708, 188.3322, 13952348.3111, 150.9342, 172.9984, 0.942422,
            22.618128, 26.73, 0.0, false },
        { TEXT("Enceladus"), REGISTRY_SATURN_HOST, true, 7.21037e9, 2.521e5,
            0.0015936056, 0.0047, 0.0098, 182.3835, 9596283.2192, 175.4301, 309.079, 1.370218,
            32.885232, 26.73, 0.0, false },
        { TEXT("Tethys"), REGISTRY_SATURN_HOST, true, 4.121353e10, 5.311e5,
            0.0019719532, 0.0001, 1.093, 187.0501, 6965243.177, 196.6672, 259.7693, 1.887802,
            45.307248, 26.73, 0.0, false },
        { TEXT("Dione"), REGISTRY_SATURN_HOST, true, 7.311607e10, 5.614e5,
            0.0025247686, 0.0022, 0.029, 176.9069, 4804314.3466, 204.8502, 288.141, 2.736915,
            65.68596, 26.73, 0.0, false },
        { TEXT("Rhea"), REGISTRY_SATURN_HOST, true, 1.5394175e11, 7.635e5,
            0.0035241143, 0.0012583, 0.3186, 52.1704, 2910222.0082, 205.2675, 346.167, 4.518212,
            108.437088, 26.73, 0.0, false },
        { TEXT("Titan"), REGISTRY_SATURN_HOST, true, 8.9781371e12, 2.57476e6,
            0.008167897, 0.0288, 0.36, 7.556, 824625.452, 204.1198, 241.8359, 15.945421,
            382.690104, 26.73, 0.0, false },
        { TEXT("Iapetus"), REGISTRY_SATURN_HOST, true, 1.2051511e11, 7.343e5,
            0.023808494, 0.0276812, 15.4701, 89.8956, 165768.4234, 241.8776, 253.5208, 79.3215,
            1903.716, 26.73, 0.0, false },
        // Ring shepherds (issue #6 Part 5d): a 133,584 / 136,505 km, P 0.575050718 / 0.5940798 d (Jacobson et al. 2008),
        // mean e taken as 0, GM = G * mass
        { TEXT("Pan"), REGISTRY_SATURN_HOST, true, 3.30378e5, 1.41e4,
            0.0008929539, 0.0, 0.0009, 146.5849, 22865809.2033, 146.6508, 247.4223, 0.575050718,
            13.8, 26.73, 0.0, false },
        { TEXT("Daphnis"), REGISTRY_SATURN_HOST, true, 5.139211e3, 3.8e3,
            0.0009124796, 0.0, 0.0017, 153.5761, 22133390.1607, 153.4587, 121.2292, 0.5940798,
            14.256, 26.73, 0.0, false },
        { TEXT("Miranda"), REGISTRY_URANUS_HOST, true, 4.3e9, 2.358e5,
            0.0008679669, 0.0013, 4.4278, 147.5636, 9302578.9559, 85.5017, 280.8287, 1.413479,
            33.923496, 97.77, 0.0, false },
        { TEXT("Ariel"), REGISTRY_URANUS_HOST, true, 8.35e10, 5.789e5,
            0.0012762815, 0.0012, 0.0027, 23.2099, 5217072.5117, 230.4155, 29.3786, 2.520379,
            60.489096, 97.77, 0.0, false },
        { TEXT("Umbriel"), REGISTRY_URANUS_HOST, true, 8.51e10, 5.847e5,
            0.0017780066, 0.0039, 0.0564, 71.2348, 3172885.7141, 160.0114, 13.6513, 4.144177,
            99.460248, 97.77, 0.0, false },
        { TEXT("Titania"), REGISTRY_URANUS_HOST, true, 2.269e11, 7.889e5,
            0.002916472, 0.0011, 0.1007, 101.5961, 1510360.4247, 27.1793, 168.5636, 8.705869,
            208.940856, 97.77, 0.0, false },
        { TEXT("Oberon"), REGISTRY_URANUS_HOST, true, 2.053e11, 7.614e5,
            0.0039005301, 0.0014, 0.188, 172.5469, 976659.8426, 79.0506, 220.0438, 13.463234,
            323.117616, 97.77, 0.0, false },
        // Triton orbits retrograde: ~157 deg from Neptune's spin pole
        { TEXT("Triton"), REGISTRY_NEPTUNE_HOST, true, 1.42849546e12, 1.3526e6,
            0.0023716915, 0.000016, 156.8279, 236.4627, 2237421.5864, 253.002, 177.8669, 5.876854,
            141.044496, 180.0, 0.0, true },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a child body's reference epoch elements in the frame its table row is written in
    FSOLSecularElements RegistryChildElements(const FSOLRegistryExpectedChild& expected)
    {
        FSOLSecularElements elements;
        elements.A0AU = expected.SemiMajorAxisAU;
        elements.E0 = expected.Eccentricity;
        elements.I0Deg = expected.InclinationDeg;
        elements.L0Deg = expected.MeanLongitudeDeg;
        elements.LDotDegPerCy = expected.MeanLongitudeRateDegPerCy;
        elements.LongPeri0Deg = expected.LongPeriDeg;
        elements.LongNode0Deg = expected.LongNodeDeg;
        return elements;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rotation taking a child's table frame to the ecliptic: the host's tilt about +Y, or identity
    FQuat4d RegistryChildFrame(const FSOLRegistryExpectedChild& expected)
    {
        if (!expected.bHostEquator)
        {
            return FQuat4d::Identity;
        }
        const double tiltRad = FMath::DegreesToRadians(REGISTRY_EXPECTED[expected.ParentIndex].AxialTiltDeg);
        return FQuat4d(FVector3d(0.0, 1.0, 0.0), tiltRad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the GM that reproduces a child's own tabulated mean motion exactly (mu = n^2 a^3, Kepler's third law
    // inverted), written independently of production's ChildOrbitGM so this reference does not just mirror the
    // implementation under test; a close moon's real mean motion need not match its parent's measured point-mass GM
    // (e.g. oblateness), so the production code uses this per-body value instead of the parent's own GM
    double RegistryChildOrbitGM(const FSOLRegistryExpectedChild& expected)
    {
        const double secondsPerJulianCentury = SOLTestHelpers::SECONDS_PER_DAY * 36525.0;
        const double meanMotionRadPerSec = FMath::DegreesToRadians(expected.MeanLongitudeRateDegPerCy)
            / secondsPerJulianCentury;
        const double semiMajorAxisM = expected.SemiMajorAxisAU * SOLTestHelpers::AU_M;
        return FMath::Square(meanMotionRadPerSec) * (semiMajorAxisM * semiMajorAxisM * semiMajorAxisM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected parent-relative ecliptic state of a child body at a time, independent of the registry
    FSOLOrbitState RegistryExpectedChildState(const FSOLRegistryExpectedChild& expected, const double secondsSinceJ2000)
    {
        const double centuries = secondsSinceJ2000 / (SOLTestHelpers::SECONDS_PER_DAY * 36525.0);
        const FSOLKeplerElements elements = RegistryChildElements(expected).AtCenturies(centuries);
        const FSOLOrbitState inFrame = SOLKepler::ElementsToState(elements, RegistryChildOrbitGM(expected), 0.0);
        const FQuat4d frame = RegistryChildFrame(expected);
        FSOLOrbitState state;
        state.PositionM = frame.RotateVector(inFrame.PositionM);
        state.VelocityMps = frame.RotateVector(inFrame.VelocityMps);
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the orientation the pure rotation function gives for one reference-table body at a time
    FQuat4d RegistryExpectedOrientation(const FSOLRegistryExpectedBody& expected, const double secondsSinceJ2000)
    {
        return SOLBodyRotation::ComputeOrientation(FMath::DegreesToRadians(expected.AxialTiltDeg),
            FMath::DegreesToRadians(expected.W0Deg), expected.RotationPeriodH, secondsSinceJ2000);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two quaternions rotate a set of probe vectors identically within a relative tolerance
    bool RegistrySameRotation(const FQuat4d& actual, const FQuat4d& expected, const double tolerance)
    {
        const FVector3d probes[] =
        {
            FVector3d(1.0, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), FVector3d(0.0, 0.0, 1.0), FVector3d(0.3, -0.7, 0.2),
        };
        for (const FVector3d& probe : probes)
        {
            if (!SOLTestHelpers::VectorsNear(actual.RotateVector(probe), expected.RotateVector(probe), tolerance))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when the registry holds all 36 bodies: Sun, planets, dwarf planets, moons (logs a failure otherwise)
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
    for (int32 index = 0; index < REGISTRY_PLANET_COUNT; ++index)
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
    // Pluto and the Moon joined the table in issue #6; Charon (out of scope) and a fictional body stay absent
    TestEqual(TEXT("Charon absent"), registry.FindByName(FName(TEXT("Charon"))), INDEX_NONE);
    TestEqual(TEXT("Vulcan absent"), registry.FindByName(FName(TEXT("Vulcan"))), INDEX_NONE);
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
    for (int32 index = 0; index < REGISTRY_PLANET_COUNT; ++index)
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
    TestEqual(TEXT("Populating twice still gives all 36 bodies (was nine before issue #6)"), registry.Num(),
        REGISTRY_BODY_COUNT);
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
            AddError(TEXT("Position/velocity views do not hold all 36 bodies"));
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

    for (int32 index = 1; index < REGISTRY_PLANET_COUNT; ++index)
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
        for (int32 index = 1; index < REGISTRY_PLANET_COUNT; ++index)
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
    for (int32 index = 1; index < REGISTRY_PLANET_COUNT; ++index)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryPoleDataTest, "SOLTest.BodyRegistry.RotationPoleFromTiltData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every populated body's orientation sends body-north to (sin tilt, 0, cos tilt) from the reference tilt, at any time
bool FSOLBodyRegistryPoleDataTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    const double times[] = { 0.0, 1.234e7, -4.0e8 };
    for (const double time : times)
    {
        registry.Update(time);
        for (int32 index = 0; index < REGISTRY_PLANET_COUNT; ++index)
        {
            const FSOLRegistryExpectedBody& expected = REGISTRY_EXPECTED[index];
            const double tiltRad = FMath::DegreesToRadians(expected.AxialTiltDeg);
            const FVector3d expectedPole(FMath::Sin(tiltRad), 0.0, FMath::Cos(tiltRad));
            const FVector3d pole = registry.GetOrientation(index).RotateVector(FVector3d(0.0, 0.0, 1.0));
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: pole (%.9f, %.9f, %.9f) from tilt %.3f deg"), expected.Name, time,
                pole.X, pole.Y, pole.Z, expected.AxialTiltDeg), SOLTestHelpers::VectorsNear(pole, expectedPole, 1e-9));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryOrientationDataTest, "SOLTest.BodyRegistry.OrientationMatchesRotationData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every populated body's orientation equals ComputeOrientation built from the reference period, tilt and W0 columns
bool FSOLBodyRegistryOrientationDataTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }

    // Non-epoch times make the period column matter, not just tilt and W0
    const double times[] = { 0.0, 3600.0, 1.0e6, 1.234e7, -4.0e8 };
    for (const double time : times)
    {
        registry.Update(time);
        for (int32 index = 0; index < REGISTRY_PLANET_COUNT; ++index)
        {
            const FSOLRegistryExpectedBody& expected = REGISTRY_EXPECTED[index];
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: orientation matches P=%.4f h, tilt=%.3f deg, W0=%.1f deg"),
                expected.Name, time, expected.RotationPeriodH, expected.AxialTiltDeg, expected.W0Deg),
                RegistrySameRotation(registry.GetOrientation(index), RegistryExpectedOrientation(expected, time), 1e-9));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryEpochOrientationTest, "SOLTest.BodyRegistry.OrientationAtJ2000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At J2000 Earth and Venus orientations equal ComputeOrientation(tilt, W0, period, 0) from the reference data
bool FSOLBodyRegistryEpochOrientationTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    registry.Update(0.0);
    const int32 bodies[] = { REGISTRY_EARTH, REGISTRY_VENUS };
    for (const int32 index : bodies)
    {
        const FSOLRegistryExpectedBody& expected = REGISTRY_EXPECTED[index];
        const FQuat4d orientation = registry.GetOrientation(index);
        TestTrue(*FString::Printf(TEXT("%s at J2000 matches ComputeOrientation"), expected.Name),
            RegistrySameRotation(orientation, RegistryExpectedOrientation(expected, 0.0), 1e-12));
        TestTrue(*FString::Printf(TEXT("%s at J2000 is a unit quaternion"), expected.Name),
            FMath::Abs(orientation.Size() - 1.0) < 1e-12);
    }

    // Venus's near-180 deg tilt points its pole mostly south
    const FVector3d venusPole = registry.GetOrientation(REGISTRY_VENUS).RotateVector(FVector3d(0.0, 0.0, 1.0));
    TestTrue(*FString::Printf(TEXT("Venus pole z = %.6f is mostly south"), venusPole.Z), venusPole.Z < -0.99);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryEarthSiderealDayTest, "SOLTest.BodyRegistry.EarthSiderealDayFullTurn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One Earth sidereal day after J2000 Earth's orientation is back to its epoch orientation; half a day it is not
bool FSOLBodyRegistryEarthSiderealDayTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    const double siderealDayS = REGISTRY_EXPECTED[REGISTRY_EARTH].RotationPeriodH * 3600.0;
    registry.Update(0.0);
    const FQuat4d epoch = registry.GetOrientation(REGISTRY_EARTH);

    registry.Update(0.5 * siderealDayS);
    TestFalse(TEXT("Half a sidereal day later Earth has turned"),
        RegistrySameRotation(registry.GetOrientation(REGISTRY_EARTH), epoch, 1e-3));

    registry.Update(siderealDayS);
    TestTrue(TEXT("One sidereal day later Earth is back to its epoch orientation"),
        RegistrySameRotation(registry.GetOrientation(REGISTRY_EARTH), epoch, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryNoRotationDataTest, "SOLTest.BodyRegistry.BodyWithoutRotationDataIsIdentity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FSOLBodyDef rotation fields default to 0, and a body added without them has an identity orientation after Update
bool FSOLBodyRegistryNoRotationDataTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyDef defaults;
    TestEqual(TEXT("RotationPeriodH defaults to 0"), defaults.RotationPeriodH, 0.0, 0.0);
    TestEqual(TEXT("AxialTiltDeg defaults to 0"), defaults.AxialTiltDeg, 0.0, 0.0);
    TestEqual(TEXT("W0Deg defaults to 0"), defaults.W0Deg, 0.0, 0.0);

    FSOLBodyRegistry registry;
    FSOLBodyDef star;
    star.Name = FName(TEXT("TestStar"));
    star.RadiusM = 5.0e8;
    star.GM = 1.0e20;
    registry.AddBody(star);

    FSOLBodyDef planet;
    planet.Name = FName(TEXT("TestPlanet"));
    planet.RadiusM = 4.0e6;
    planet.GM = 2.0e13;
    planet.ParentIndex = 0;
    planet.Elements.A0AU = 1.0;
    registry.AddBody(planet);

    const double times[] = { 0.0, 1.0e6, -3.0e8 };
    for (const double time : times)
    {
        registry.Update(time);
        for (int32 index = 0; index < registry.Num(); ++index)
        {
            const FQuat4d orientation = registry.GetOrientation(index);
            TestTrue(*FString::Printf(TEXT("Body %d t=%.0f: finite"), index, time), FMath::IsFinite(orientation.X)
                && FMath::IsFinite(orientation.Y) && FMath::IsFinite(orientation.Z) && FMath::IsFinite(orientation.W));
            TestTrue(*FString::Printf(TEXT("Body %d t=%.0f: identity orientation"), index, time),
                RegistrySameRotation(orientation, FQuat4d::Identity, 1e-15));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryAddBodyRotationTest, "SOLTest.BodyRegistry.AddBodyStoresRotationData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A body added with rotation data gets ComputeOrientation(tilt, W0, period, t) with degrees converted to radians
bool FSOLBodyRegistryAddBodyRotationTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    FSOLBodyDef star;
    star.Name = FName(TEXT("TestStar"));
    star.RadiusM = 5.0e8;
    star.GM = 1.0e20;
    star.RotationPeriodH = 100.0;
    star.AxialTiltDeg = 12.0;
    star.W0Deg = 45.0;
    TestEqual(TEXT("AddBody returns 0"), registry.AddBody(star), 0);

    const double times[] = { 0.0, 7200.0, 5.0e6 };
    for (const double time : times)
    {
        registry.Update(time);
        const FQuat4d expected = SOLBodyRotation::ComputeOrientation(FMath::DegreesToRadians(12.0),
            FMath::DegreesToRadians(45.0), 100.0, time);
        TestTrue(*FString::Printf(TEXT("t=%.0f: orientation matches ComputeOrientation from the def"), time),
            RegistrySameRotation(registry.GetOrientation(0), expected, 1e-12));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryOrientationBeforeUpdateTest,
    "SOLTest.BodyRegistry.OrientationIsIdentityBeforeUpdate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A freshly added body reads as the identity orientation before the first Update, whatever its rotation data
bool FSOLBodyRegistryOrientationBeforeUpdateTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    FSOLBodyDef plain;
    plain.Name = FName(TEXT("TestPlain"));
    plain.RadiusM = 1.0;
    plain.GM = 1.0;
    FSOLBodyDef rotating;
    rotating.Name = FName(TEXT("TestRotating"));
    rotating.RadiusM = 1.0;
    rotating.GM = 1.0;
    rotating.RotationPeriodH = 10.0;
    rotating.AxialTiltDeg = 30.0;
    rotating.W0Deg = 60.0;
    const int32 indices[] = { registry.AddBody(plain), registry.AddBody(rotating) };

    for (const int32 index : indices)
    {
        const FQuat4d orientation = registry.GetOrientation(index);
        TestTrue(*FString::Printf(TEXT("Body %d before Update: components are identity"), index),
            orientation.Equals(FQuat4d::Identity, 0.0));
        TestTrue(*FString::Printf(TEXT("Body %d before Update: rotates like identity"), index),
            RegistrySameRotation(orientation, FQuat4d::Identity, 0.0));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryChildNamesTest, "SOLTest.BodyRegistry.ChildNamesParentsAndOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After the nine original bodies come the dwarf planets then the moons, each found by name and parented to its host
bool FSOLBodyRegistryChildNamesTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    for (int32 child = 0; child < REGISTRY_CHILD_COUNT; ++child)
    {
        const FSOLRegistryExpectedChild& expected = REGISTRY_EXPECTED_CHILDREN[child];
        const int32 index = REGISTRY_PLANET_COUNT + child;
        TestEqual(*FString::Printf(TEXT("Name of body %d"), index), registry.GetName(index).ToString(),
            FString(expected.Name));
        TestEqual(*FString::Printf(TEXT("FindByName(%s)"), expected.Name), registry.FindByName(FName(expected.Name)),
            index);
        TestEqual(*FString::Printf(TEXT("Parent of %s"), expected.Name), registry.GetParent(index),
            expected.ParentIndex);
        TestTrue(*FString::Printf(TEXT("%s's parent precedes it"), expected.Name), registry.GetParent(index) < index);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryChildConstantsTest, "SOLTest.BodyRegistry.ChildPhysicalConstants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each dwarf planet's and moon's GM and mean radius equal the contract reference constants
bool FSOLBodyRegistryChildConstantsTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    for (int32 child = 0; child < REGISTRY_CHILD_COUNT; ++child)
    {
        const FSOLRegistryExpectedChild& expected = REGISTRY_EXPECTED_CHILDREN[child];
        const int32 index = REGISTRY_PLANET_COUNT + child;
        TestTrue(*FString::Printf(TEXT("%s GM %.9e == %.9e"), expected.Name, registry.GetGM(index), expected.GM),
            SOLTestHelpers::RelativeError(registry.GetGM(index), expected.GM) < 1e-12);
        TestTrue(*FString::Printf(TEXT("%s radius %.6e == %.6e"), expected.Name, registry.GetRadiusM(index),
            expected.RadiusM), SOLTestHelpers::RelativeError(registry.GetRadiusM(index), expected.RadiusM) < 1e-12);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryChildElementsTest, "SOLTest.BodyRegistry.ChildElementsMatchReference",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stored elements keep the reference a, e and L rate with all other rates 0; ecliptic rows are stored as written
bool FSOLBodyRegistryChildElementsTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    for (int32 child = 0; child < REGISTRY_CHILD_COUNT; ++child)
    {
        const FSOLRegistryExpectedChild& expected = REGISTRY_EXPECTED_CHILDREN[child];
        const FSOLSecularElements& actual = registry.GetElements(REGISTRY_PLANET_COUNT + child);
        TestTrue(*FString::Printf(TEXT("%s a %.10f == %.10f AU"), expected.Name, actual.A0AU, expected.SemiMajorAxisAU),
            SOLTestHelpers::RelativeError(actual.A0AU, expected.SemiMajorAxisAU) < 1e-12);
        TestEqual(*FString::Printf(TEXT("%s e"), expected.Name), actual.E0, expected.Eccentricity, 1e-12);
        TestTrue(*FString::Printf(TEXT("%s L rate %.4f == %.4f deg/cy"), expected.Name, actual.LDotDegPerCy,
            expected.MeanLongitudeRateDegPerCy),
            SOLTestHelpers::RelativeError(actual.LDotDegPerCy, expected.MeanLongitudeRateDegPerCy) < 1e-12);
        TestTrue(*FString::Printf(TEXT("%s has no secular drift besides L"), expected.Name), actual.ADotAUPerCy == 0.0
            && actual.EDotPerCy == 0.0 && actual.IDotDegPerCy == 0.0 && actual.LongPeriDotDegPerCy == 0.0
            && actual.LongNodeDotDegPerCy == 0.0);

        // Host-equator rows are re-expressed in the ecliptic; their angles are checked through the state test instead
        if (!expected.bHostEquator)
        {
            TestEqual(*FString::Printf(TEXT("%s i"), expected.Name), actual.I0Deg, expected.InclinationDeg, 1e-12);
            TestEqual(*FString::Printf(TEXT("%s L0"), expected.Name), actual.L0Deg, expected.MeanLongitudeDeg, 1e-12);
            TestEqual(*FString::Printf(TEXT("%s LongPeri"), expected.Name), actual.LongPeri0Deg, expected.LongPeriDeg,
                1e-12);
            TestEqual(*FString::Printf(TEXT("%s LongNode"), expected.Name), actual.LongNode0Deg, expected.LongNodeDeg,
                1e-12);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryChildStateTest,
    "SOLTest.BodyRegistry.ChildStateIsTiltedReferenceOrbit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each child's state relative to its parent equals its reference orbit (host-equator rows tilted by the host's tilt)
bool FSOLBodyRegistryChildStateTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    const double times[] = { 0.0, 1.0e6, 1.234e7, -3.0e8, 1.5e9 };
    for (const double time : times)
    {
        registry.Update(time);
        for (int32 child = 0; child < REGISTRY_CHILD_COUNT; ++child)
        {
            const FSOLRegistryExpectedChild& expected = REGISTRY_EXPECTED_CHILDREN[child];
            const int32 index = REGISTRY_PLANET_COUNT + child;
            const FSOLOrbitState reference = RegistryExpectedChildState(expected, time);
            const FVector3d relativePos = registry.GetPositionM(index) - registry.GetPositionM(expected.ParentIndex);
            const FVector3d relativeVel = registry.GetVelocityMps(index)
                - registry.GetVelocityMps(expected.ParentIndex);
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: relative position matches the reference orbit"), expected.Name,
                time), SOLTestHelpers::VectorsNear(relativePos, reference.PositionM, 1e-8));
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: relative velocity matches the reference orbit"), expected.Name,
                time), SOLTestHelpers::VectorsNear(relativeVel, reference.VelocityMps, 1e-8));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryChildOrientationTest,
    "SOLTest.BodyRegistry.ChildOrientationMatchesRotationData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each child's orientation equals ComputeOrientation built from the reference period, tilt and W0 columns
bool FSOLBodyRegistryChildOrientationTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    const double times[] = { 0.0, 3600.0, 1.0e6, 1.234e7, -4.0e8 };
    for (const double time : times)
    {
        registry.Update(time);
        for (int32 child = 0; child < REGISTRY_CHILD_COUNT; ++child)
        {
            const FSOLRegistryExpectedChild& expected = REGISTRY_EXPECTED_CHILDREN[child];
            const FQuat4d reference = SOLBodyRotation::ComputeOrientation(
                FMath::DegreesToRadians(expected.AxialTiltDeg), FMath::DegreesToRadians(expected.W0Deg),
                expected.RotationPeriodH, time);
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: orientation matches P=%.4f h, tilt=%.3f deg, W0=%.1f deg"),
                expected.Name, time, expected.RotationPeriodH, expected.AxialTiltDeg, expected.W0Deg),
                RegistrySameRotation(registry.GetOrientation(REGISTRY_PLANET_COUNT + child), reference, 1e-9));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryChildPeriodTest, "SOLTest.BodyRegistry.ChildOrbitalPeriods",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each child's L rate, Kepler period from (a, parent GM) and position after one real period agree with that period (1%)
bool FSOLBodyRegistryChildPeriodTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    for (int32 child = 0; child < REGISTRY_CHILD_COUNT; ++child)
    {
        const FSOLRegistryExpectedChild& expected = REGISTRY_EXPECTED_CHILDREN[child];
        const int32 index = REGISTRY_PLANET_COUNT + child;
        const int32 parent = registry.GetParent(index);
        const FSOLSecularElements& elements = registry.GetElements(index);

        // Mean motion: 360 deg * 36525 days per century / L rate
        const double fromRateDays = 360.0 * 36525.0 / elements.LDotDegPerCy;
        TestTrue(*FString::Printf(TEXT("%s period from L rate %.6f d vs real %.6f d"), expected.Name, fromRateDays,
            expected.OrbitalPeriodDays),
            SOLTestHelpers::RelativeError(fromRateDays, expected.OrbitalPeriodDays) < 0.01);

        // Kepler's third law from the stored a and the parent's GM catches a or GM data-entry slips
        const double aM = elements.A0AU * SOLTestHelpers::AU_M;
        const double keplerDays = UE_DOUBLE_TWO_PI * FMath::Sqrt(aM * aM * aM / registry.GetGM(parent))
            / SOLTestHelpers::SECONDS_PER_DAY;
        TestTrue(*FString::Printf(TEXT("%s Kepler period %.6f d vs real %.6f d"), expected.Name, keplerDays,
            expected.OrbitalPeriodDays), SOLTestHelpers::RelativeError(keplerDays, expected.OrbitalPeriodDays) < 0.01);

        // One real period later the body is back where it started relative to its parent; half a period it is not
        const double periodS = expected.OrbitalPeriodDays * SOLTestHelpers::SECONDS_PER_DAY;
        registry.Update(0.0);
        const FVector3d start = registry.GetPositionM(index) - registry.GetPositionM(parent);
        registry.Update(0.5 * periodS);
        const FVector3d half = registry.GetPositionM(index) - registry.GetPositionM(parent);
        registry.Update(periodS);
        const FVector3d full = registry.GetPositionM(index) - registry.GetPositionM(parent);
        const double circumference = UE_DOUBLE_TWO_PI * aM;
        TestTrue(*FString::Printf(TEXT("%s returns to its start after one period"), expected.Name),
            (full - start).Size() < 0.01 * circumference);
        TestTrue(*FString::Printf(TEXT("%s is across its orbit after half a period"), expected.Name),
            (half - start).Size() > aM);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryChildOrbitSenseTest, "SOLTest.BodyRegistry.ChildOrbitSense",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Moons orbit their host's spin pole prograde except retrograde Triton; dwarf planets orbit the Sun prograde
bool FSOLBodyRegistryChildOrbitSenseTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    registry.Update(0.0);
    int32 retrogradeCount = 0;
    for (int32 child = 0; child < REGISTRY_CHILD_COUNT; ++child)
    {
        const FSOLRegistryExpectedChild& expected = REGISTRY_EXPECTED_CHILDREN[child];
        const int32 index = REGISTRY_PLANET_COUNT + child;
        const int32 parent = expected.ParentIndex;
        const FVector3d h = (registry.GetPositionM(index) - registry.GetPositionM(parent))
            ^ (registry.GetVelocityMps(index) - registry.GetVelocityMps(parent));

        // Dwarf planets are judged against ecliptic-north (like the planets), moons against their host's spin pole
        const FVector3d pole = parent == 0 ? FVector3d(0.0, 0.0, 1.0)
            : registry.GetOrientation(parent).RotateVector(FVector3d(0.0, 0.0, 1.0));
        const double cosine = (h.GetSafeNormal() | pole);
        if (expected.bRetrograde)
        {
            ++retrogradeCount;
            TestTrue(*FString::Printf(TEXT("%s is retrograde (cos %.4f)"), expected.Name, cosine), cosine < -0.5);
        }
        else
        {
            TestTrue(*FString::Printf(TEXT("%s is prograde (cos %.4f)"), expected.Name, cosine), cosine > 0.5);
        }
    }
    TestEqual(TEXT("Exactly one retrograde child (Triton)"), retrogradeCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBodyRegistryHierarchyTest,
    "SOLTest.BodyRegistry.MoonSunFramePositionIsHierarchical",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A real moon's Sun-frame state is its planet's own Sun-relative orbit plus the moon's planet-relative orbit
bool FSOLBodyRegistryHierarchyTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyRegistry registry;
    registry.PopulateSolarSystem();
    if (!RegistryHasSolarSystem(*this, registry))
    {
        return false;
    }
    const TCHAR* moons[] = { TEXT("Moon"), TEXT("Titan"), TEXT("Triton") };
    const double times[] = { 0.0, 2.0e6, -5.0e8 };
    for (const TCHAR* moonName : moons)
    {
        const int32 index = registry.FindByName(FName(moonName));
        if (!TestTrue(*FString::Printf(TEXT("%s registered"), moonName), index >= REGISTRY_PLANET_COUNT))
        {
            continue;
        }
        const FSOLRegistryExpectedChild& expected = REGISTRY_EXPECTED_CHILDREN[index - REGISTRY_PLANET_COUNT];
        const int32 planet = expected.ParentIndex;
        for (const double time : times)
        {
            registry.Update(time);
            const double centuries = time / (SOLTestHelpers::SECONDS_PER_DAY * 36525.0);

            // Planet: Sun at the origin plus the planet's own heliocentric orbit
            const FSOLOrbitState planetState = SOLKepler::ElementsToState(
                registry.GetElements(planet).AtCenturies(centuries), SOLTestHelpers::SUN_GM, 0.0);
            const FSOLOrbitState moonState = RegistryExpectedChildState(expected, time);
            const FVector3d expectedPos = planetState.PositionM + moonState.PositionM;
            const FVector3d expectedVel = planetState.VelocityMps + moonState.VelocityMps;
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: planet sits on its heliocentric orbit"), moonName, time),
                SOLTestHelpers::VectorsNear(registry.GetPositionM(planet), planetState.PositionM, 1e-12));
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: Sun-frame position = planet + moon orbit"), moonName, time),
                (registry.GetPositionM(index) - expectedPos).Size() < 1e-8 * moonState.PositionM.Size());
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: Sun-frame velocity = planet + moon orbit"), moonName, time),
                (registry.GetVelocityMps(index) - expectedVel).Size() < 1e-8 * moonState.VelocityMps.Size());

            // The moon stays within its own apsides of the planet
            const double a = expected.SemiMajorAxisAU * SOLTestHelpers::AU_M;
            const double r = (registry.GetPositionM(index) - registry.GetPositionM(planet)).Size();
            TestTrue(*FString::Printf(TEXT("%s t=%.0f: %.0f m from its planet within apsides"), moonName, time, r),
                r >= a * (1.0 - expected.Eccentricity) * (1.0 - 1e-9)
                && r <= a * (1.0 + expected.Eccentricity) * (1.0 + 1e-9));
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
