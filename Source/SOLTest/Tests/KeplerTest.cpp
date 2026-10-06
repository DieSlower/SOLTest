/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLKepler.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS


// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double KEPLER_TWO_PI = UE_DOUBLE_TWO_PI;

    //////////////////////////////////////////////////////////////////////////
    // Returns the analytic orbital period, independent of SOLKepler::PeriodSeconds
    double AnalyticPeriod(const double semiMajorAxisM, const double gm)
    {
        return KEPLER_TWO_PI * FMath::Sqrt(semiMajorAxisM * semiMajorAxisM * semiMajorAxisM / gm);
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds a Kepler element set from its six values
    FSOLKeplerElements MakeElements(const double a, const double e, const double i, const double node,
        const double argPeri, const double meanAnomaly)
    {
        FSOLKeplerElements elements;
        elements.SemiMajorAxisM = a;
        elements.Eccentricity = e;
        elements.InclinationRad = i;
        elements.LongitudeOfAscendingNodeRad = node;
        elements.ArgumentOfPeriapsisRad = argPeri;
        elements.MeanAnomalyRad = meanAnomaly;
        return elements;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the JPL Standish (1800-2050) Earth-Moon barycenter secular elements
    FSOLSecularElements MakeEarthSecular()
    {
        FSOLSecularElements earth;
        earth.A0AU = 1.00000261;
        earth.ADotAUPerCy = 0.00000562;
        earth.E0 = 0.01671123;
        earth.EDotPerCy = -0.00004392;
        earth.I0Deg = -0.00001531;
        earth.IDotDegPerCy = -0.01294668;
        earth.L0Deg = 100.46457166;
        earth.LDotDegPerCy = 35999.37244981;
        earth.LongPeri0Deg = 102.93768193;
        earth.LongPeriDotDegPerCy = 0.32327364;
        earth.LongNode0Deg = 0.0;
        earth.LongNodeDotDegPerCy = 0.0;
        return earth;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the JPL Standish (1800-2050) Mars secular elements
    FSOLSecularElements MakeMarsSecular()
    {
        FSOLSecularElements mars;
        mars.A0AU = 1.52371034;
        mars.ADotAUPerCy = 0.00001847;
        mars.E0 = 0.09339410;
        mars.EDotPerCy = 0.00007882;
        mars.I0Deg = 1.84969142;
        mars.IDotDegPerCy = -0.00813131;
        mars.L0Deg = -4.55343205;
        mars.LDotDegPerCy = 19140.30268499;
        mars.LongPeri0Deg = -23.94362959;
        mars.LongPeriDotDegPerCy = 0.44441088;
        mars.LongNode0Deg = 49.55953891;
        mars.LongNodeDotDegPerCy = -0.29257343;
        return mars;
    }

    // Name, e0 and e-dot per century of one planet from JPL Standish Table 1 (1800-2050)
    struct FSOLKeplerPlanetEccentricity
    {
        const TCHAR* Name;
        double E0;
        double EDotPerCy;
    };

    // Eccentricity terms for the eight planets (Earth is the Earth-Moon barycenter)
    const FSOLKeplerPlanetEccentricity KEPLER_PLANET_ECCENTRICITIES[] =
    {
        { TEXT("Mercury"), 0.20563593, 0.00001906 },
        { TEXT("Venus"), 0.00677672, -0.00004107 },
        { TEXT("Earth"), 0.01671123, -0.00004392 },
        { TEXT("Mars"), 0.09339410, 0.00007882 },
        { TEXT("Jupiter"), 0.04838624, -0.00013253 },
        { TEXT("Saturn"), 0.05386179, -0.00050991 },
        { TEXT("Uranus"), 0.04725744, -0.00004397 },
        { TEXT("Neptune"), 0.00859048, 0.00005105 },
    };

    //////////////////////////////////////////////////////////////////////////
    // Builds secular elements for a planet's eccentricity terms (other elements set to Earth-like values)
    FSOLSecularElements MakeSecularWithEccentricity(const double e0, const double eDotPerCy)
    {
        FSOLSecularElements elements = MakeEarthSecular();
        elements.E0 = e0;
        elements.EDotPerCy = eDotPerCy;
        return elements;
    }

    // Seconds in one Julian century (36525 days), kept independent of SOLConstants.h like the other test constants
    constexpr double KEPLER_SECONDS_PER_JULIAN_CENTURY = 36525.0 * SOLTestHelpers::SECONDS_PER_DAY;

    // Relative tolerance a StateToElements round trip must reproduce position and velocity to
    constexpr double KEPLER_ROUND_TRIP_TOLERANCE = 1e-11;

    // Looser tolerance for (near-)degenerate orbits, where i or e near 0 is numerically ill-conditioned
    constexpr double KEPLER_DEGENERATE_ROUND_TRIP_TOLERANCE = 1e-10;

    //////////////////////////////////////////////////////////////////////////
    // Round-trips the state reached elapsedSeconds after the given elements through StateToElements and back,
    // asserting position, velocity, a, e and i survive; bCheckAngles also asserts node, argPeri and M (non-degenerate only)
    void KeplerCheckStateToElementsRoundTrip(FAutomationTestBase& test, const TCHAR* label,
        const FSOLKeplerElements& elements, const double gm, const double elapsedSeconds, const bool bCheckAngles)
    {
        const FSOLOrbitState state = SOLKepler::ElementsToState(elements, gm, elapsedSeconds);
        const FSOLKeplerElements roundTripElements = SOLKepler::StateToElements(state, gm);
        const FSOLOrbitState roundTripState = SOLKepler::ElementsToState(roundTripElements, gm, 0.0);
        const double tolerance = bCheckAngles ? KEPLER_ROUND_TRIP_TOLERANCE : KEPLER_DEGENERATE_ROUND_TRIP_TOLERANCE;

        // The reconstructed elements must reproduce the exact state (relative errors reported for diagnosis)
        const double positionError = (roundTripState.PositionM - state.PositionM).Size() / state.PositionM.Size();
        const double velocityError = (roundTripState.VelocityMps - state.VelocityMps).Size()
            / state.VelocityMps.Size();
        test.TestTrue(FString::Printf(TEXT("%s: round-trip position matches (rel err %.3e)"), label, positionError),
            SOLTestHelpers::VectorsNear(roundTripState.PositionM, state.PositionM, tolerance));
        test.TestTrue(FString::Printf(TEXT("%s: round-trip velocity matches (rel err %.3e)"), label, velocityError),
            SOLTestHelpers::VectorsNear(roundTripState.VelocityMps, state.VelocityMps, tolerance));

        // Shape and tilt of the orbit are well defined even for degenerate orbits
        test.TestTrue(FString::Printf(TEXT("%s: a %.6f == %.6f"), label, roundTripElements.SemiMajorAxisM,
            elements.SemiMajorAxisM), SOLTestHelpers::RelativeError(roundTripElements.SemiMajorAxisM,
            elements.SemiMajorAxisM) < 1e-9);
        test.TestTrue(FString::Printf(TEXT("%s: e %.12f == %.12f"), label, roundTripElements.Eccentricity,
            elements.Eccentricity), FMath::Abs(roundTripElements.Eccentricity - elements.Eccentricity) < 1e-9);
        test.TestTrue(FString::Printf(TEXT("%s: e in [0, 1)"), label),
            roundTripElements.Eccentricity >= 0.0 && roundTripElements.Eccentricity < 1.0);
        test.TestTrue(FString::Printf(TEXT("%s: i %.12f == %.12f"), label, roundTripElements.InclinationRad,
            elements.InclinationRad), FMath::Abs(roundTripElements.InclinationRad - elements.InclinationRad) < tolerance);
        test.TestTrue(FString::Printf(TEXT("%s: i in [0, PI]"), label),
            roundTripElements.InclinationRad >= 0.0 && roundTripElements.InclinationRad <= UE_DOUBLE_PI);

        // Node, argument of periapsis and the advanced mean anomaly are only defined for non-degenerate orbits
        if (bCheckAngles)
        {
            const double meanMotion = FMath::Sqrt(gm / (elements.SemiMajorAxisM * elements.SemiMajorAxisM
                * elements.SemiMajorAxisM));
            const double expectedMeanAnomaly = elements.MeanAnomalyRad + meanMotion * elapsedSeconds;
            test.TestTrue(FString::Printf(TEXT("%s: node matches (mod 2PI)"), label), FMath::Abs(SOLTestHelpers::AngleDiffRad(
                roundTripElements.LongitudeOfAscendingNodeRad - elements.LongitudeOfAscendingNodeRad)) < 1e-9);
            test.TestTrue(FString::Printf(TEXT("%s: argPeri matches (mod 2PI)"), label), FMath::Abs(SOLTestHelpers::AngleDiffRad(
                roundTripElements.ArgumentOfPeriapsisRad - elements.ArgumentOfPeriapsisRad)) < 1e-9);
            test.TestTrue(FString::Printf(TEXT("%s: M == M0 + n*elapsed (mod 2PI)"), label), FMath::Abs(SOLTestHelpers::AngleDiffRad(
                roundTripElements.MeanAnomalyRad - expectedMeanAnomaly)) < 1e-8);
        }
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerWrapAngleKnownValuesTest, "SOLTest.Kepler.WrapAngleKnownValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// WrapAngleRad maps known angles to their [-PI, PI) equivalents
bool FSOLKeplerWrapAngleKnownValuesTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Wrap(0)"), SOLKepler::WrapAngleRad(0.0), 0.0, 1e-15);
    TestEqual(TEXT("Wrap(0.5)"), SOLKepler::WrapAngleRad(0.5), 0.5, 1e-15);
    TestEqual(TEXT("Wrap(-1)"), SOLKepler::WrapAngleRad(-1.0), -1.0, 1e-15);
    TestEqual(TEXT("Wrap(0.5 + 2PI)"), SOLKepler::WrapAngleRad(0.5 + KEPLER_TWO_PI), 0.5, 1e-12);
    TestEqual(TEXT("Wrap(-0.5 - 2PI)"), SOLKepler::WrapAngleRad(-0.5 - KEPLER_TWO_PI), -0.5, 1e-12);
    TestEqual(TEXT("Wrap(1 + 10*2PI)"), SOLKepler::WrapAngleRad(1.0 + 10.0 * KEPLER_TWO_PI), 1.0, 1e-11);
    TestEqual(TEXT("Wrap(-2 - 7*2PI)"), SOLKepler::WrapAngleRad(-2.0 - 7.0 * KEPLER_TWO_PI), -2.0, 1e-11);
    TestEqual(TEXT("Wrap(PI) is -PI (upper bound is open)"), SOLKepler::WrapAngleRad(UE_DOUBLE_PI), -UE_DOUBLE_PI, 1e-15);
    TestEqual(TEXT("Wrap(-PI) is -PI"), SOLKepler::WrapAngleRad(-UE_DOUBLE_PI), -UE_DOUBLE_PI, 1e-15);
    TestEqual(TEXT("Wrap(3PI) has magnitude PI"), FMath::Abs(SOLKepler::WrapAngleRad(3.0 * UE_DOUBLE_PI)),
        UE_DOUBLE_PI, 1e-12);
    TestEqual(TEXT("Wrap(100)"), SOLKepler::WrapAngleRad(100.0), SOLTestHelpers::AngleDiffRad(100.0), 1e-12);
    TestEqual(TEXT("Wrap(-100)"), SOLKepler::WrapAngleRad(-100.0), SOLTestHelpers::AngleDiffRad(-100.0), 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerWrapAngleRangeTest, "SOLTest.Kepler.WrapAngleRangeAndIdempotence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// WrapAngleRad always lands in [-PI, PI), preserves the angle mod 2PI, and is idempotent
bool FSOLKeplerWrapAngleRangeTest::RunTest(const FString& /*parameters*/)
{
    // Sweep a wide span of angles, including large magnitudes
    for (double angle = -1000.0; angle <= 1000.0; angle += 0.731)
    {
        const double wrapped = SOLKepler::WrapAngleRad(angle);
        const bool bInRange = wrapped >= -UE_DOUBLE_PI && wrapped < UE_DOUBLE_PI;
        const bool bSameAngle = FMath::Abs(SOLTestHelpers::AngleDiffRad(wrapped - angle)) < 1e-9;
        const bool bIdempotent = FMath::Abs(SOLKepler::WrapAngleRad(wrapped) - wrapped) < 1e-12;
        if (!TestTrue(FString::Printf(TEXT("Wrap(%.6f)=%.12f in [-PI,PI)"), angle, wrapped), bInRange)
            || !TestTrue(FString::Printf(TEXT("Wrap(%.6f)=%.12f equals the input mod 2PI"), angle, wrapped), bSameAngle)
            || !TestTrue(FString::Printf(TEXT("Wrap(Wrap(%.6f)) is idempotent"), angle), bIdempotent))
        {
            break;
        }
    }

    // Very large magnitude still wraps into range
    const double big = SOLKepler::WrapAngleRad(1.0e6);
    TestTrue(TEXT("Wrap(1e6) in range"), big >= -UE_DOUBLE_PI && big < UE_DOUBLE_PI);
    TestTrue(TEXT("Wrap(1e6) equals 1e6 mod 2PI"), FMath::Abs(SOLTestHelpers::AngleDiffRad(big - 1.0e6)) < 1e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerSolveResidualTest, "SOLTest.Kepler.SolveEccentricAnomalyResidual",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SolveEccentricAnomaly satisfies Kepler's equation to 1e-10 for many e and M, including multi-turn and negative M
bool FSOLKeplerSolveResidualTest::RunTest(const FString& /*parameters*/)
{
    const double eccentricities[] = { 0.0, 0.01, 0.2, 0.7, 0.95, 0.99 };
    for (const double e : eccentricities)
    {
        // M spans several turns in both directions; residual is compared mod 2PI
        bool bAllGood = true;
        for (double meanAnomaly = -20.0; meanAnomaly <= 20.0 && bAllGood; meanAnomaly += 0.0937)
        {
            const double eccAnomaly = SOLKepler::SolveEccentricAnomaly(meanAnomaly, e);
            const double residual = SOLTestHelpers::AngleDiffRad(meanAnomaly - (eccAnomaly - e * FMath::Sin(eccAnomaly)));
            bAllGood = TestTrue(FString::Printf(TEXT("e=%.2f M=%.4f E=%.12f residual=%.3e < 1e-10"),
                e, meanAnomaly, eccAnomaly, residual), FMath::Abs(residual) < 1e-10);
        }
    }

    // Points just either side of 0 and PI where high-e solvers struggle
    const double hardPoints[] = { 1e-9, -1e-9, UE_DOUBLE_PI - 1e-9, -UE_DOUBLE_PI + 1e-9, UE_DOUBLE_PI };
    for (const double meanAnomaly : hardPoints)
    {
        const double eccAnomaly = SOLKepler::SolveEccentricAnomaly(meanAnomaly, 0.99);
        const double residual = SOLTestHelpers::AngleDiffRad(meanAnomaly - (eccAnomaly - 0.99 * FMath::Sin(eccAnomaly)));
        TestTrue(FString::Printf(TEXT("e=0.99 M=%.12f residual=%.3e"), meanAnomaly, residual),
            FMath::Abs(residual) < 1e-10);
    }

    // A non-trivial value must not be zero (guards against a solver that ignores its input)
    const double eccAnomaly = SOLKepler::SolveEccentricAnomaly(1.0, 0.5);
    TestEqual(TEXT("E(M=1, e=0.5) satisfies E - e sin E = 1"), eccAnomaly - 0.5 * FMath::Sin(eccAnomaly), 1.0, 1e-10);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerSolveCircularTest, "SOLTest.Kepler.SolveEccentricAnomalyCircular",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// For e = 0 the eccentric anomaly equals the mean anomaly (mod 2PI)
bool FSOLKeplerSolveCircularTest::RunTest(const FString& /*parameters*/)
{
    const double samples[] = { 0.3, 1.7, -2.9, 3.0, 12.5, -40.0 };
    for (const double meanAnomaly : samples)
    {
        const double eccAnomaly = SOLKepler::SolveEccentricAnomaly(meanAnomaly, 0.0);
        TestTrue(FString::Printf(TEXT("e=0: E(%.3f)=%.12f equals M mod 2PI"), meanAnomaly, eccAnomaly),
            FMath::Abs(SOLTestHelpers::AngleDiffRad(eccAnomaly - meanAnomaly)) < 1e-12);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerPeriodTest, "SOLTest.Kepler.PeriodSeconds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PeriodSeconds follows 2*PI*sqrt(a^3/GM); Earth's is about 365.25 days
bool FSOLKeplerPeriodTest::RunTest(const FString& /*parameters*/)
{
    const double earthDays = SOLKepler::PeriodSeconds(SOLTestHelpers::AU_M, SOLTestHelpers::SUN_GM) / SOLTestHelpers::SECONDS_PER_DAY;
    TestEqual(TEXT("Earth period (days) ~365.25 within 0.1%"), earthDays, 365.25, 365.25 * 0.001);

    const double expected = KEPLER_TWO_PI * FMath::Sqrt(1.0e21 / 4.0e14);
    TestTrue(TEXT("Period(a=1e7, GM=4e14) matches the formula to 1e-12 relative"),
        SOLTestHelpers::RelativeError(SOLKepler::PeriodSeconds(1.0e7, 4.0e14), expected) < 1e-12);

    // Low Earth orbit sanity: ~92.4 minutes at 6778 km
    const double leoMinutes = SOLKepler::PeriodSeconds(6.778e6, SOLTestHelpers::EARTH_GM) / 60.0;
    TestEqual(TEXT("LEO period ~92.56 minutes"), leoMinutes, AnalyticPeriod(6.778e6, SOLTestHelpers::EARTH_GM) / 60.0, 1e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerCircularStateTest, "SOLTest.Kepler.CircularOrbitState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A circular orbit has radius a, speed sqrt(GM/a), and velocity perpendicular to the radius at all times
bool FSOLKeplerCircularStateTest::RunTest(const FString& /*parameters*/)
{
    const double a = 7.0e6;
    const FSOLKeplerElements elements = MakeElements(a, 0.0, 0.3, 1.0, 0.5, 0.7);
    const double expectedSpeed = FMath::Sqrt(SOLTestHelpers::EARTH_GM / a);
    const double period = AnalyticPeriod(a, SOLTestHelpers::EARTH_GM);

    // Sample across more than one period
    for (int32 step = 0; step <= 40; ++step)
    {
        const double elapsed = period * 1.3 * step / 40.0;
        const FSOLOrbitState state = SOLKepler::ElementsToState(elements, SOLTestHelpers::EARTH_GM, elapsed);
        const double radius = state.PositionM.Size();
        const double speed = state.VelocityMps.Size();
        const double cosAngle = state.PositionM.GetSafeNormal() | state.VelocityMps.GetSafeNormal();
        if (!TestTrue(FString::Printf(TEXT("t=%.1f radius %.6f == a"), elapsed, radius), SOLTestHelpers::RelativeError(radius, a) < 1e-9)
            || !TestTrue(FString::Printf(TEXT("t=%.1f speed %.9f == sqrt(GM/a) %.9f"), elapsed, speed, expectedSpeed),
                SOLTestHelpers::RelativeError(speed, expectedSpeed) < 1e-9)
            || !TestTrue(FString::Printf(TEXT("t=%.1f velocity perpendicular to radius (cos=%.3e)"), elapsed, cosAngle),
                FMath::Abs(cosAngle) < 1e-9))
        {
            break;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerApsidesTest, "SOLTest.Kepler.PeriapsisAndApoapsis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An elliptical orbit is at a(1-e) at M=0 and a(1+e) at M=PI, with vis-viva speeds
bool FSOLKeplerApsidesTest::RunTest(const FString& /*parameters*/)
{
    const double a = 1.0e11;
    const double e = 0.3;

    // Periapsis
    const FSOLOrbitState peri = SOLKepler::ElementsToState(MakeElements(a, e, 0.2, 0.4, 1.1, 0.0), SOLTestHelpers::SUN_GM, 0.0);
    const double rPeri = a * (1.0 - e);
    const double vPeri = FMath::Sqrt(SOLTestHelpers::SUN_GM * (2.0 / rPeri - 1.0 / a));
    TestTrue(TEXT("Periapsis radius a(1-e)"), SOLTestHelpers::RelativeError(peri.PositionM.Size(), rPeri) < 1e-9);
    TestTrue(TEXT("Periapsis speed from vis-viva"), SOLTestHelpers::RelativeError(peri.VelocityMps.Size(), vPeri) < 1e-9);
    TestTrue(TEXT("Periapsis velocity perpendicular to radius"),
        FMath::Abs(peri.PositionM.GetSafeNormal() | peri.VelocityMps.GetSafeNormal()) < 1e-9);

    // Apoapsis
    const FSOLOrbitState apo = SOLKepler::ElementsToState(MakeElements(a, e, 0.2, 0.4, 1.1, UE_DOUBLE_PI), SOLTestHelpers::SUN_GM, 0.0);
    const double rApo = a * (1.0 + e);
    const double vApo = FMath::Sqrt(SOLTestHelpers::SUN_GM * (2.0 / rApo - 1.0 / a));
    TestTrue(TEXT("Apoapsis radius a(1+e)"), SOLTestHelpers::RelativeError(apo.PositionM.Size(), rApo) < 1e-9);
    TestTrue(TEXT("Apoapsis speed from vis-viva"), SOLTestHelpers::RelativeError(apo.VelocityMps.Size(), vApo) < 1e-9);
    TestTrue(TEXT("Apoapsis is opposite periapsis"),
        SOLTestHelpers::VectorsNear(apo.PositionM.GetSafeNormal(), -peri.PositionM.GetSafeNormal(), 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerConservationTest, "SOLTest.Kepler.ConservesEnergyAndAngularMomentum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Specific energy (-GM/2a) and angular momentum (magnitude sqrt(GM a (1-e^2))) stay constant along the orbit
bool FSOLKeplerConservationTest::RunTest(const FString& /*parameters*/)
{
    const double a = 2.0e11;
    const double e = 0.6;
    const FSOLKeplerElements elements = MakeElements(a, e, 0.5, 2.0, -1.0, 0.3);
    const double period = AnalyticPeriod(a, SOLTestHelpers::SUN_GM);
    const double expectedEnergy = -SOLTestHelpers::SUN_GM / (2.0 * a);
    const double expectedH = FMath::Sqrt(SOLTestHelpers::SUN_GM * a * (1.0 - e * e));
    const FSOLOrbitState start = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM, 0.0);
    const FVector3d startH = start.PositionM ^ start.VelocityMps;

    TestTrue(TEXT("Start |h| == sqrt(GM a (1-e^2))"), SOLTestHelpers::RelativeError(startH.Size(), expectedH) < 1e-9);

    // Sample 200 instants over two periods
    for (int32 step = 0; step <= 200; ++step)
    {
        const double elapsed = 2.0 * period * step / 200.0;
        const FSOLOrbitState state = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM, elapsed);
        const double r = state.PositionM.Size();
        const double energy = 0.5 * state.VelocityMps.SizeSquared() - SOLTestHelpers::SUN_GM / FMath::Max(r, 1.0);
        const FVector3d h = state.PositionM ^ state.VelocityMps;
        if (!TestTrue(FString::Printf(TEXT("step %d energy %.9e == %.9e"), step, energy, expectedEnergy),
                SOLTestHelpers::RelativeError(energy, expectedEnergy) < 1e-9)
            || !TestTrue(FString::Printf(TEXT("step %d angular momentum vector constant"), step),
                SOLTestHelpers::VectorsNear(h, startH, 1e-9))
            || !TestTrue(FString::Printf(TEXT("step %d radius within [a(1-e), a(1+e)]"), step),
                r >= a * (1.0 - e) * (1.0 - 1e-9) && r <= a * (1.0 + e) * (1.0 + 1e-9)))
        {
            break;
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerInclinationTest, "SOLTest.Kepler.InclinationAndNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Inclination lifts the orbit out of the XY plane (max Z = a sin i); the node and normal follow the ecliptic convention
bool FSOLKeplerInclinationTest::RunTest(const FString& /*parameters*/)
{
    const double a = 1.0e9;
    const double gm = 1.0e17;
    const double inclination = 0.4;
    const double node = 0.3;
    const double period = AnalyticPeriod(a, gm);

    // Max |Z| over one period of a circular inclined orbit
    const FSOLKeplerElements inclined = MakeElements(a, 0.0, inclination, node, 0.0, 0.0);
    double maxZ = 0.0;
    for (int32 step = 0; step < 3600; ++step)
    {
        const FSOLOrbitState state = SOLKepler::ElementsToState(inclined, gm, period * step / 3600.0);
        maxZ = FMath::Max(maxZ, state.PositionM.Z);
    }
    TestTrue(FString::Printf(TEXT("Max Z %.3f ~ a sin i %.3f"), maxZ, a * FMath::Sin(inclination)),
        SOLTestHelpers::RelativeError(maxZ, a * FMath::Sin(inclination)) < 1e-4);

    // At argument of latitude 0 the body sits on the ascending node: a*(cos node, sin node, 0)
    const FSOLOrbitState atNode = SOLKepler::ElementsToState(inclined, gm, 0.0);
    TestTrue(TEXT("Position at ascending node is a*(cos node, sin node, 0)"),
        SOLTestHelpers::VectorsNear(atNode.PositionM, FVector3d(a * FMath::Cos(node), a * FMath::Sin(node), 0.0), 1e-9));
    TestTrue(TEXT("Body rises through the node (Vz > 0)"), atNode.VelocityMps.Z > 0.0);

    // Orbit normal (prograde) is (sin i sin node, -sin i cos node, cos i)
    const FVector3d normal = (atNode.PositionM ^ atNode.VelocityMps).GetSafeNormal();
    const FVector3d expectedNormal(FMath::Sin(inclination) * FMath::Sin(node),
        -FMath::Sin(inclination) * FMath::Cos(node), FMath::Cos(inclination));
    TestTrue(TEXT("Orbit normal matches the ecliptic convention"), SOLTestHelpers::VectorsNear(normal, expectedNormal, 1e-9));

    // Zero inclination stays in the XY plane and orbits counter-clockwise seen from +Z
    const FSOLKeplerElements planar = MakeElements(a, 0.4, 0.0, 1.2, 0.8, 0.0);
    bool bPlanar = true;
    for (int32 step = 0; step < 100 && bPlanar; ++step)
    {
        const FSOLOrbitState state = SOLKepler::ElementsToState(planar, gm, period * step / 100.0);
        bPlanar = TestTrue(FString::Printf(TEXT("i=0 step %d: Z=%.3e, Vz=%.3e stay zero"), step, state.PositionM.Z,
            state.VelocityMps.Z), FMath::Abs(state.PositionM.Z) < 1e-6 * a && FMath::Abs(state.VelocityMps.Z) < 1e-9
            && state.PositionM.Size() > 0.5 * a);
        bPlanar = bPlanar && TestTrue(TEXT("i=0 orbit is prograde (h.Z > 0)"),
            (state.PositionM ^ state.VelocityMps).Z > 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerPeriapsisDirectionTest, "SOLTest.Kepler.PeriapsisDirection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At M=0 the body lies along the periapsis unit vector given by the standard 3-1-3 rotation (node, i, argPeri)
bool FSOLKeplerPeriapsisDirectionTest::RunTest(const FString& /*parameters*/)
{
    const double a = 3.0e10;
    const double e = 0.5;
    const double i = 0.3;
    const double node = 1.1;
    const double w = 0.7;
    const FSOLOrbitState state = SOLKepler::ElementsToState(MakeElements(a, e, i, node, w, 0.0), SOLTestHelpers::SUN_GM, 0.0);

    const FVector3d expectedDir(
        FMath::Cos(node) * FMath::Cos(w) - FMath::Sin(node) * FMath::Sin(w) * FMath::Cos(i),
        FMath::Sin(node) * FMath::Cos(w) + FMath::Cos(node) * FMath::Sin(w) * FMath::Cos(i),
        FMath::Sin(w) * FMath::Sin(i));
    TestTrue(TEXT("Periapsis position == a(1-e) * P-hat"), SOLTestHelpers::VectorsNear(state.PositionM, expectedDir * a * (1.0 - e), 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerFullPeriodTest, "SOLTest.Kepler.FullPeriodReturnsToStart",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Advancing by exactly one period returns to the starting state
bool FSOLKeplerFullPeriodTest::RunTest(const FString& /*parameters*/)
{
    const double a = 1.5e11;
    const double e = 0.2;
    const FSOLKeplerElements elements = MakeElements(a, e, 0.1, 0.9, 2.2, -1.3);
    const double period = AnalyticPeriod(a, SOLTestHelpers::SUN_GM);
    const FSOLOrbitState start = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM, 0.0);
    const FSOLOrbitState half = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM, 0.5 * period);
    const FSOLOrbitState once = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM, period);
    const FSOLOrbitState thrice = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM, 3.0 * period);

    TestTrue(TEXT("Start state is on the orbit"),
        start.PositionM.Size() >= a * (1.0 - e) * 0.999 && start.PositionM.Size() <= a * (1.0 + e) * 1.001);
    TestFalse(TEXT("Half a period later the body has moved"), SOLTestHelpers::VectorsNear(half.PositionM, start.PositionM, 1e-3));
    TestTrue(TEXT("Position after 1 period"), SOLTestHelpers::VectorsNear(once.PositionM, start.PositionM, 1e-9));
    TestTrue(TEXT("Velocity after 1 period"), SOLTestHelpers::VectorsNear(once.VelocityMps, start.VelocityMps, 1e-9));
    TestTrue(TEXT("Position after 3 periods"), SOLTestHelpers::VectorsNear(thrice.PositionM, start.PositionM, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerElapsedTest, "SOLTest.Kepler.ElapsedAdvancesMeanAnomaly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// State after Elapsed equals the state at Elapsed=0 of elements whose M is advanced by sqrt(GM/a^3)*Elapsed
bool FSOLKeplerElapsedTest::RunTest(const FString& /*parameters*/)
{
    const double a = 2.3e11;
    const double e = 0.09;
    const double meanMotion = FMath::Sqrt(SOLTestHelpers::SUN_GM / (a * a * a));
    const FSOLKeplerElements elements = MakeElements(a, e, 0.03, 0.86, -0.42, 0.34);
    const double elapsedSamples[] = { 3600.0, 30.0 * SOLTestHelpers::SECONDS_PER_DAY, 400.0 * SOLTestHelpers::SECONDS_PER_DAY };
    for (const double elapsed : elapsedSamples)
    {
        FSOLKeplerElements shifted = elements;
        shifted.MeanAnomalyRad = elements.MeanAnomalyRad + meanMotion * elapsed;
        const FSOLOrbitState advanced = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM, elapsed);
        const FSOLOrbitState reference = SOLKepler::ElementsToState(shifted, SOLTestHelpers::SUN_GM, 0.0);
        TestTrue(FString::Printf(TEXT("Elapsed %.0f s: state is non-trivial"), elapsed),
            advanced.PositionM.Size() > 0.5 * a);
        TestTrue(FString::Printf(TEXT("Elapsed %.0f s: position matches shifted M"), elapsed),
            SOLTestHelpers::VectorsNear(advanced.PositionM, reference.PositionM, 1e-9));
        TestTrue(FString::Printf(TEXT("Elapsed %.0f s: velocity matches shifted M"), elapsed),
            SOLTestHelpers::VectorsNear(advanced.VelocityMps, reference.VelocityMps, 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerSecularEarthTest, "SOLTest.Kepler.SecularEarthAtJ2000",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AtCenturies(0) converts Standish Earth elements to meters/radians with ArgPeri = LongPeri - LongNode, M = L - LongPeri
bool FSOLKeplerSecularEarthTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeplerElements elements = MakeEarthSecular().AtCenturies(0.0);
    TestTrue(TEXT("a in meters = 1.00000261 AU"), SOLTestHelpers::RelativeError(elements.SemiMajorAxisM, 1.00000261 * SOLTestHelpers::AU_M) < 1e-12);
    TestEqual(TEXT("e"), elements.Eccentricity, 0.01671123, 1e-12);
    TestEqual(TEXT("i (rad)"), elements.InclinationRad, FMath::DegreesToRadians(-0.00001531), 1e-14);
    TestTrue(TEXT("Node (rad) == 0"), FMath::Abs(SOLTestHelpers::AngleDiffRad(elements.LongitudeOfAscendingNodeRad)) < 1e-12);
    TestTrue(TEXT("ArgPeri == LongPeri - LongNode = 102.93768193 deg"),
        FMath::Abs(SOLTestHelpers::AngleDiffRad(elements.ArgumentOfPeriapsisRad - FMath::DegreesToRadians(102.93768193))) < 1e-12);
    TestEqual(TEXT("M == wrap(L - LongPeri) = -2.47311027 deg"), elements.MeanAnomalyRad,
        FMath::DegreesToRadians(100.46457166 - 102.93768193), 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerSecularRatesTest, "SOLTest.Kepler.SecularRatesAndNode",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Element rates are applied linearly in T, M is wrapped to [-PI, PI), and ArgPeri subtracts a non-zero node
bool FSOLKeplerSecularRatesTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSecularElements earth = MakeEarthSecular();
    const FSOLKeplerElements atT1 = earth.AtCenturies(1.0);
    TestTrue(TEXT("T=1: a includes the rate"),
        SOLTestHelpers::RelativeError(atT1.SemiMajorAxisM, (earth.A0AU + earth.ADotAUPerCy) * SOLTestHelpers::AU_M) < 1e-12);
    TestEqual(TEXT("T=1: e includes the rate"), atT1.Eccentricity, earth.E0 + earth.EDotPerCy, 1e-12);
    TestEqual(TEXT("T=1: i includes the rate"), atT1.InclinationRad,
        FMath::DegreesToRadians(earth.I0Deg + earth.IDotDegPerCy), 1e-12);
    const double expectedM = FMath::DegreesToRadians((earth.L0Deg + earth.LDotDegPerCy)
        - (earth.LongPeri0Deg + earth.LongPeriDotDegPerCy));
    TestTrue(TEXT("T=1: M in [-PI, PI)"), atT1.MeanAnomalyRad >= -UE_DOUBLE_PI && atT1.MeanAnomalyRad < UE_DOUBLE_PI);
    TestTrue(TEXT("T=1: M == L - LongPeri (mod 2PI)"), FMath::Abs(SOLTestHelpers::AngleDiffRad(atT1.MeanAnomalyRad - expectedM)) < 1e-9);
    TestTrue(TEXT("T=1: M differs from T=0 (rates matter)"),
        FMath::Abs(SOLTestHelpers::AngleDiffRad(atT1.MeanAnomalyRad - earth.AtCenturies(0.0).MeanAnomalyRad)) > 1e-3);

    // Half a century, negative T
    const FSOLKeplerElements atMinusHalf = earth.AtCenturies(-0.5);
    TestEqual(TEXT("T=-0.5: e"), atMinusHalf.Eccentricity, earth.E0 - 0.5 * earth.EDotPerCy, 1e-12);
    TestTrue(TEXT("T=-0.5: M in [-PI, PI)"),
        atMinusHalf.MeanAnomalyRad >= -UE_DOUBLE_PI && atMinusHalf.MeanAnomalyRad < UE_DOUBLE_PI);

    // Mars has a non-zero ascending node, so ArgPeri must subtract it
    const FSOLKeplerElements mars = MakeMarsSecular().AtCenturies(0.0);
    TestTrue(TEXT("Mars a"), SOLTestHelpers::RelativeError(mars.SemiMajorAxisM, 1.52371034 * SOLTestHelpers::AU_M) < 1e-12);
    TestEqual(TEXT("Mars e"), mars.Eccentricity, 0.09339410, 1e-12);
    TestEqual(TEXT("Mars i"), mars.InclinationRad, FMath::DegreesToRadians(1.84969142), 1e-12);
    TestTrue(TEXT("Mars node == 49.55953891 deg"),
        FMath::Abs(SOLTestHelpers::AngleDiffRad(mars.LongitudeOfAscendingNodeRad - FMath::DegreesToRadians(49.55953891))) < 1e-12);
    TestTrue(TEXT("Mars ArgPeri == LongPeri - LongNode = -73.5031685 deg"),
        FMath::Abs(SOLTestHelpers::AngleDiffRad(mars.ArgumentOfPeriapsisRad - FMath::DegreesToRadians(-23.94362959 - 49.55953891)))
        < 1e-12);
    TestEqual(TEXT("Mars M == L - LongPeri = 19.39019754 deg"), mars.MeanAnomalyRad,
        FMath::DegreesToRadians(-4.55343205 + 23.94362959), 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerEccentricityClampTest, "SOLTest.Kepler.SecularEccentricityClamped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AtCenturies clamps every planet's eccentricity to [0, 0.99] for any T, clamping (not reflecting) the linear value
bool FSOLKeplerEccentricityClampTest::RunTest(const FString& /*parameters*/)
{
    const double centuries[] = { -200.0, -50.0, -2.0, 0.0, 0.5, 5.0, 50.0, 200.0 };
    bool bAnyClampedLow = false;
    for (const FSOLKeplerPlanetEccentricity& planet : KEPLER_PLANET_ECCENTRICITIES)
    {
        const FSOLSecularElements secular = MakeSecularWithEccentricity(planet.E0, planet.EDotPerCy);
        for (const double t : centuries)
        {
            const double e = secular.AtCenturies(t).Eccentricity;
            const double linear = planet.E0 + planet.EDotPerCy * t;
            const double expected = FMath::Clamp(linear, 0.0, 0.99);
            bAnyClampedLow = bAnyClampedLow || linear < 0.0;
            TestTrue(*FString::Printf(TEXT("%s T=%.1f: e %.9f in [0, 0.99]"), planet.Name, t, e), e >= 0.0 && e <= 0.99);
            TestEqual(*FString::Printf(TEXT("%s T=%.1f: e == clamp(e0 + edot*T = %.9f)"), planet.Name, t, linear), e,
                expected, 1e-12);
        }
    }
    TestTrue(TEXT("Guard: the sample includes a planet whose linear eccentricity goes negative"), bAnyClampedLow);

    // Upper bound: a synthetic set whose linear eccentricity exceeds 0.99
    const FSOLSecularElements runaway = MakeSecularWithEccentricity(0.5, 1.0);
    TestEqual(TEXT("Runaway e at T=5 clamps to 0.99"), runaway.AtCenturies(5.0).Eccentricity, 0.99, 0.0);
    TestEqual(TEXT("Runaway e at T=-5 clamps to 0"), runaway.AtCenturies(-5.0).Eccentricity, 0.0, 0.0);
    TestEqual(TEXT("Runaway e at T=0.25 is unclamped"), runaway.AtCenturies(0.25).Eccentricity, 0.75, 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerEccentricityWindowTest, "SOLTest.Kepler.SecularEccentricityUnchangedInWindow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Inside the 1800-2050 validity window (T in [-2, 0.5]) the eccentricity is the plain linear value
bool FSOLKeplerEccentricityWindowTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Earth e at T=0 stays 0.01671123"), MakeEarthSecular().AtCenturies(0.0).Eccentricity, 0.01671123,
        1e-15);

    const double centuries[] = { -2.0, -1.0, -0.25, 0.0, 0.26, 0.5 };
    for (const FSOLKeplerPlanetEccentricity& planet : KEPLER_PLANET_ECCENTRICITIES)
    {
        const FSOLSecularElements secular = MakeSecularWithEccentricity(planet.E0, planet.EDotPerCy);
        for (const double t : centuries)
        {
            const double linear = planet.E0 + planet.EDotPerCy * t;
            TestEqual(*FString::Printf(TEXT("%s T=%.2f: e unchanged"), planet.Name, t),
                secular.AtCenturies(t).Eccentricity, linear, 1e-12);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerStateToElementsRoundTripTest, "SOLTest.Kepler.StateToElementsRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// StateToElements inverts ElementsToState for circular, eccentric, inclined, retrograde and high-e orbits
bool FSOLKeplerStateToElementsRoundTripTest::RunTest(const FString& /*parameters*/)
{
    const double sunGm = SOLTestHelpers::SUN_GM;
    const double au = SOLTestHelpers::AU_M;

    // Circular equatorial (node/argPeri degenerate, so only the state, a, e and i are checked)
    const FSOLKeplerElements circularEquatorial = MakeElements(au, 0.0, 0.0, 0.0, 0.0, 0.4);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("Circular equatorial"), circularEquatorial, sunGm,
        0.13 * AnalyticPeriod(au, sunGm), false);

    // Eccentric equatorial (node degenerate)
    const FSOLKeplerElements eccentricEquatorial = MakeElements(1.5 * au, 0.3, 0.0, 0.0, 1.0, -0.6);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("Eccentric equatorial"), eccentricEquatorial, sunGm,
        0.37 * AnalyticPeriod(1.5 * au, sunGm), false);

    // Circular inclined (argPeri degenerate)
    const FSOLKeplerElements circularInclined = MakeElements(2.0 * au, 0.0, FMath::DegreesToRadians(45.0), 0.8, 0.0, 1.2);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("Circular inclined"), circularInclined, sunGm,
        0.21 * AnalyticPeriod(2.0 * au, sunGm), false);

    // Eccentric inclined with non-zero node and argument of periapsis (fully determined)
    const FSOLKeplerElements eccentricInclined = MakeElements(2.7 * au, 0.5, FMath::DegreesToRadians(60.0),
        FMath::DegreesToRadians(40.0), FMath::DegreesToRadians(70.0), 0.25);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("Eccentric inclined"), eccentricInclined, sunGm,
        0.29 * AnalyticPeriod(2.7 * au, sunGm), true);

    // Retrograde orbit (i > 90 deg)
    const FSOLKeplerElements retrograde = MakeElements(5.2 * au, 0.2, FMath::DegreesToRadians(150.0), 2.0, -1.0, 2.5);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("Retrograde"), retrograde, sunGm,
        0.41 * AnalyticPeriod(5.2 * au, sunGm), true);

    // High-eccentricity orbit, sampled away from and near periapsis
    const FSOLKeplerElements highEccentricity = MakeElements(3.0 * au, 0.9, 0.3, -2.5, 2.8, 0.1);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("High-e (e=0.9) mid-orbit"), highEccentricity, sunGm,
        0.33 * AnalyticPeriod(3.0 * au, sunGm), true);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("High-e (e=0.9) near periapsis"), highEccentricity, sunGm,
        0.97 * AnalyticPeriod(3.0 * au, sunGm), true);

    // A planet-scale orbit around Earth's GM (low Earth orbit)
    const FSOLKeplerElements leo = MakeElements(6.778e6, 0.01, 0.9, 1.3, 0.6, -2.0);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("Low Earth orbit"), leo, SOLTestHelpers::EARTH_GM,
        0.6 * AnalyticPeriod(6.778e6, SOLTestHelpers::EARTH_GM), true);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerStateToElementsNearCircularTest, "SOLTest.Kepler.StateToElementsNearCircular",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An orbit close to circular (e = 1e-9, ill-conditioned argPeri) still round-trips position and velocity. e = 1e-9 is
// far above StateToElements's 1e-12 fallback threshold, so this exercises the normal (non-fallback) path near the
// degenerate case; the exact-degenerate fallback itself is covered by StateToElementsExactDegenerate
bool FSOLKeplerStateToElementsNearCircularTest::RunTest(const FString& /*parameters*/)
{
    const double a = 1.2 * SOLTestHelpers::AU_M;
    const double period = AnalyticPeriod(a, SOLTestHelpers::SUN_GM);
    const FSOLKeplerElements inclined = MakeElements(a, 1e-9, 0.4, 1.1, 2.3, 0.5);
    const FSOLKeplerElements equatorial = MakeElements(a, 1e-9, 0.0, 0.0, 0.7, -1.4);

    // Sample several instants around the orbit
    const double fractions[] = { 0.0, 0.17, 0.5, 0.83 };
    for (const double fraction : fractions)
    {
        KeplerCheckStateToElementsRoundTrip(*this, *FString::Printf(TEXT("Near-circular inclined t=%.2fP"), fraction),
            inclined, SOLTestHelpers::SUN_GM, fraction * period, false);
        KeplerCheckStateToElementsRoundTrip(*this, *FString::Printf(TEXT("Near-circular equatorial t=%.2fP"), fraction),
            equatorial, SOLTestHelpers::SUN_GM, fraction * period, false);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerStateToElementsNearEquatorialTest, "SOLTest.Kepler.StateToElementsNearEquatorial",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An orbit close to equatorial (i = 1e-9 or PI - 1e-9, ill-conditioned node) still round-trips position and velocity.
// Its node vector is ~1e-9 of |h|, far above the 1e-12 fallback threshold, so this exercises the normal
// (non-fallback) path near the degenerate case; the fallback itself is covered by StateToElementsExactDegenerate
bool FSOLKeplerStateToElementsNearEquatorialTest::RunTest(const FString& /*parameters*/)
{
    const double a = 0.8 * SOLTestHelpers::AU_M;
    const double period = AnalyticPeriod(a, SOLTestHelpers::SUN_GM);
    const FSOLKeplerElements prograde = MakeElements(a, 0.25, 1e-9, 0.9, 1.6, 0.3);
    const FSOLKeplerElements retrograde = MakeElements(a, 0.25, UE_DOUBLE_PI - 1e-9, 0.9, 1.6, 0.3);

    // Sample several instants around the orbit
    const double fractions[] = { 0.0, 0.23, 0.5, 0.71 };
    for (const double fraction : fractions)
    {
        KeplerCheckStateToElementsRoundTrip(*this, *FString::Printf(TEXT("Near-equatorial prograde t=%.2fP"), fraction),
            prograde, SOLTestHelpers::SUN_GM, fraction * period, false);
        KeplerCheckStateToElementsRoundTrip(*this, *FString::Printf(TEXT("Near-equatorial retrograde t=%.2fP"), fraction),
            retrograde, SOLTestHelpers::SUN_GM, fraction * period, false);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerStateToElementsExactDegenerateTest,
    "SOLTest.Kepler.StateToElementsExactDegenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Exactly degenerate orbits take StateToElements's documented fallbacks: e = 0 places periapsis at the node
// (argPeri = 0), and i = 0 or i = PI (prograde/retrograde equatorial) takes the node along +X (node = 0); the state
// still round-trips
bool FSOLKeplerStateToElementsExactDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const double gm = SOLTestHelpers::SUN_GM;
    const double a = 1.3 * SOLTestHelpers::AU_M;
    const double period = AnalyticPeriod(a, gm);

    // Exactly circular, inclined: argPeri falls back to 0, so M is the argument of latitude
    const FSOLKeplerElements circular = MakeElements(a, 0.0, 0.5, 1.2, 0.0, 0.9);
    const FSOLKeplerElements circularResult = SOLKepler::StateToElements(
        SOLKepler::ElementsToState(circular, gm, 0.31 * period), gm);
    TestTrue(FString::Printf(TEXT("e = 0: e %.3e below the fallback threshold"), circularResult.Eccentricity),
        circularResult.Eccentricity < 1e-12);
    TestEqual(TEXT("e = 0: argPeri falls back to exactly 0"), circularResult.ArgumentOfPeriapsisRad, 0.0, 0.0);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("Exactly circular"), circular, gm, 0.31 * period, false);

    // Exactly equatorial, prograde (i = 0): the state has exactly zero Z, so h has no X/Y component
    const FSOLKeplerElements equatorial = MakeElements(a, 0.2, 0.0, 0.0, 0.8, -0.4);
    const FSOLOrbitState equatorialState = SOLKepler::ElementsToState(equatorial, gm, 0.47 * period);
    const FSOLKeplerElements equatorialResult = SOLKepler::StateToElements(equatorialState, gm);
    TestTrue(TEXT("i = 0: fixture state lies exactly in the XY plane"),
        equatorialState.PositionM.Z == 0.0 && equatorialState.VelocityMps.Z == 0.0);
    TestEqual(TEXT("i = 0: inclination is exactly 0"), equatorialResult.InclinationRad, 0.0, 0.0);
    TestEqual(TEXT("i = 0: node falls back to exactly 0"), equatorialResult.LongitudeOfAscendingNodeRad, 0.0, 0.0);
    KeplerCheckStateToElementsRoundTrip(*this, TEXT("Exactly equatorial prograde"), equatorial, gm, 0.47 * period,
        false);

    // Exactly equatorial, retrograde (i = PI): the same in-plane state with its velocity reversed
    FSOLOrbitState retrogradeState = equatorialState;
    retrogradeState.VelocityMps = -retrogradeState.VelocityMps;
    const FSOLKeplerElements retrogradeResult = SOLKepler::StateToElements(retrogradeState, gm);
    TestEqual(TEXT("i = PI: inclination is PI"), retrogradeResult.InclinationRad, UE_DOUBLE_PI, 1e-15);
    TestEqual(TEXT("i = PI: node falls back to exactly 0"), retrogradeResult.LongitudeOfAscendingNodeRad, 0.0, 0.0);
    const FSOLOrbitState retrogradeRoundTrip = SOLKepler::ElementsToState(retrogradeResult, gm, 0.0);
    TestTrue(TEXT("i = PI: round-trip position matches"), SOLTestHelpers::VectorsNear(retrogradeRoundTrip.PositionM,
        retrogradeState.PositionM, KEPLER_DEGENERATE_ROUND_TRIP_TOLERANCE));
    TestTrue(TEXT("i = PI: round-trip velocity matches"), SOLTestHelpers::VectorsNear(retrogradeRoundTrip.VelocityMps,
        retrogradeState.VelocityMps, KEPLER_DEGENERATE_ROUND_TRIP_TOLERANCE));
    TestTrue(FString::Printf(TEXT("i = PI: e %.12f matches 0.2"), retrogradeResult.Eccentricity),
        FMath::Abs(retrogradeResult.Eccentricity - 0.2) < 1e-9);

    // Exactly circular and equatorial: both fallbacks at once, so M is the true longitude atan2(y, x)
    const FSOLKeplerElements both = MakeElements(a, 0.0, 0.0, 0.0, 0.0, 2.1);
    const FSOLOrbitState bothState = SOLKepler::ElementsToState(both, gm, 0.66 * period);
    const FSOLKeplerElements bothResult = SOLKepler::StateToElements(bothState, gm);
    TestEqual(TEXT("e = 0, i = 0: node is exactly 0"), bothResult.LongitudeOfAscendingNodeRad, 0.0, 0.0);
    TestEqual(TEXT("e = 0, i = 0: argPeri is exactly 0"), bothResult.ArgumentOfPeriapsisRad, 0.0, 0.0);
    TestTrue(TEXT("e = 0, i = 0: M is the true longitude"), FMath::Abs(SOLTestHelpers::AngleDiffRad(
        bothResult.MeanAnomalyRad - FMath::Atan2(bothState.PositionM.Y, bothState.PositionM.X))) < 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerStateToElementsDeterministicTest, "SOLTest.Kepler.StateToElementsDeterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// StateToElements returns bit-identical elements when called twice with the same state
bool FSOLKeplerStateToElementsDeterministicTest::RunTest(const FString& /*parameters*/)
{
    const double a = 2.7 * SOLTestHelpers::AU_M;
    const FSOLKeplerElements elements = MakeElements(a, 0.5, FMath::DegreesToRadians(60.0),
        FMath::DegreesToRadians(40.0), FMath::DegreesToRadians(70.0), 0.25);
    const FSOLOrbitState state = SOLKepler::ElementsToState(elements, SOLTestHelpers::SUN_GM,
        0.29 * AnalyticPeriod(a, SOLTestHelpers::SUN_GM));
    const FSOLKeplerElements first = SOLKepler::StateToElements(state, SOLTestHelpers::SUN_GM);
    const FSOLKeplerElements second = SOLKepler::StateToElements(state, SOLTestHelpers::SUN_GM);

    TestTrue(TEXT("Guard: the result is non-trivial (a > 0)"), first.SemiMajorAxisM > 0.0);
    TestTrue(TEXT("a bit-identical"), first.SemiMajorAxisM == second.SemiMajorAxisM);
    TestTrue(TEXT("e bit-identical"), first.Eccentricity == second.Eccentricity);
    TestTrue(TEXT("i bit-identical"), first.InclinationRad == second.InclinationRad);
    TestTrue(TEXT("node bit-identical"), first.LongitudeOfAscendingNodeRad == second.LongitudeOfAscendingNodeRad);
    TestTrue(TEXT("argPeri bit-identical"), first.ArgumentOfPeriapsisRad == second.ArgumentOfPeriapsisRad);
    TestTrue(TEXT("M bit-identical"), first.MeanAnomalyRad == second.MeanAnomalyRad);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeplerMeanMotionDegPerCyTest, "SOLTest.Kepler.MeanMotionDegPerCy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MeanMotionDegPerCy equals sqrt(GM/a^3) converted from rad/s to degrees per Julian century
bool FSOLKeplerMeanMotionDegPerCyTest::RunTest(const FString& /*parameters*/)
{
    // (semi-major axis, GM) pairs: Earth around the Sun, an outer asteroid, low Earth orbit, a small close-in case
    const double cases[][2] =
    {
        { SOLTestHelpers::AU_M, SOLTestHelpers::SUN_GM },
        { 3.2 * SOLTestHelpers::AU_M, SOLTestHelpers::SUN_GM },
        { 6.778e6, SOLTestHelpers::EARTH_GM },
        { 1.0e7, 4.0e14 },
    };
    for (const auto& pair : cases)
    {
        const double a = pair[0];
        const double gm = pair[1];
        const double expected = FMath::RadiansToDegrees(FMath::Sqrt(gm / (a * a * a))) * KEPLER_SECONDS_PER_JULIAN_CENTURY;
        const double actual = SOLKepler::MeanMotionDegPerCy(a, gm);
        TestTrue(FString::Printf(TEXT("a=%.6e GM=%.6e: %.9f == %.9f deg/cy"), a, gm, actual, expected),
            SOLTestHelpers::RelativeError(actual, expected) < 1e-12);
    }

    // Sanity: Earth's mean motion is close to the Standish L-dot (~35999.37 deg/cy)
    const double earthMeanMotion = SOLKepler::MeanMotionDegPerCy(SOLTestHelpers::AU_M, SOLTestHelpers::SUN_GM);
    TestTrue(FString::Printf(TEXT("Earth mean motion %.4f deg/cy ~ 35999.37 within 0.01%%"), earthMeanMotion),
        SOLTestHelpers::RelativeError(earthMeanMotion, 35999.37244981) < 1e-4);

    // A tighter orbit around the same body moves faster (n scales as a^-1.5)
    const double inner = SOLKepler::MeanMotionDegPerCy(SOLTestHelpers::AU_M, SOLTestHelpers::SUN_GM);
    const double outer = SOLKepler::MeanMotionDegPerCy(4.0 * SOLTestHelpers::AU_M, SOLTestHelpers::SUN_GM);
    TestTrue(TEXT("Quadrupling a divides n by 8"), SOLTestHelpers::RelativeError(inner / FMath::Max(outer, 1e-300), 8.0) < 1e-12);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
