/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLAsteroidBelt.h"
#include "MinorBodies/SOLMinorBodyOrbit.h"
#include "Universe/SOLKepler.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS


// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double MINOR_ORBIT_SECONDS_PER_CENTURY = 36525.0 * SOLTestHelpers::SECONDS_PER_DAY;
    constexpr double MINOR_ORBIT_REL_TOLERANCE = 1e-12;

    // Sample times (seconds since J2000): J2000 itself, ~3.2 years after, ~16 years before
    constexpr double MINOR_ORBIT_TIMES_S[] = { 0.0, 1.0e8, -5.0e8 };

    //////////////////////////////////////////////////////////////////////////
    // Returns a belt-like Sun-relative element set (a 2.77 AU, e 0.08, i 10.6 deg) with a mean-motion rate
    FSOLSecularElements MakeMinorOrbitBeltElements()
    {
        FSOLSecularElements elements;
        elements.A0AU = 2.77;
        elements.E0 = 0.0785;
        elements.I0Deg = 10.6;
        elements.L0Deg = 153.2;
        elements.LDotDegPerCy = 7820.0;
        elements.LongPeri0Deg = 153.9;
        elements.LongNode0Deg = 80.3;
        return elements;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the parent-relative position straight from SOLKepler, independent of the code under test's composition
    FVector3d MinorOrbitDirectKeplerPositionM(const FSOLSecularElements& elements, const double secondsSinceJ2000)
    {
        return SOLKepler::ElementsToState(elements.AtCenturies(secondsSinceJ2000 / MINOR_ORBIT_SECONDS_PER_CENTURY),
            SOLTestHelpers::SUN_GM, 0.0).PositionM;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMinorBodyOrbitOriginParentTest, "SOLTest.MinorBodyOrbit.OriginParentMatchesKepler",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With the parent at the origin, the position equals SOLKepler::ElementsToState's at the same instant
bool FSOLMinorBodyOrbitOriginParentTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSecularElements elements = MakeMinorOrbitBeltElements();
    for (const double timeS : MINOR_ORBIT_TIMES_S)
    {
        const FVector3d actual = SOLMinorBodyOrbit::ComputePositionM(elements, FVector3d::ZeroVector,
            SOLTestHelpers::SUN_GM, timeS);
        const FVector3d expected = MinorOrbitDirectKeplerPositionM(elements, timeS);
        TestTrue(FString::Printf(TEXT("t=%.0f s matches ElementsToState"), timeS),
            SOLTestHelpers::VectorsNear(actual, expected, MINOR_ORBIT_REL_TOLERANCE));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMinorBodyOrbitParentOffsetTest, "SOLTest.MinorBodyOrbit.ParentOffsetShiftsResult",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A parent away from the origin shifts the result by exactly the parent's position
bool FSOLMinorBodyOrbitParentOffsetTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSecularElements elements = MakeMinorOrbitBeltElements();
    const FVector3d parentM(7.4e11, -2.1e11, 3.0e9);
    for (const double timeS : MINOR_ORBIT_TIMES_S)
    {
        const FVector3d atOrigin = SOLMinorBodyOrbit::ComputePositionM(elements, FVector3d::ZeroVector,
            SOLTestHelpers::SUN_GM, timeS);
        const FVector3d offset = SOLMinorBodyOrbit::ComputePositionM(elements, parentM, SOLTestHelpers::SUN_GM, timeS);
        TestTrue(FString::Printf(TEXT("t=%.0f s shifted by the parent"), timeS),
            SOLTestHelpers::VectorsNear(offset - atOrigin, parentM, MINOR_ORBIT_REL_TOLERANCE * 1e3));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMinorBodyOrbitGmIndependentTest, "SOLTest.MinorBodyOrbit.PositionIndependentOfGm",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The position does not depend on the GM passed (only the discarded velocity does)
bool FSOLMinorBodyOrbitGmIndependentTest::RunTest(const FString& /*parameters*/)
{
    const FSOLSecularElements elements = MakeMinorOrbitBeltElements();
    for (const double timeS : MINOR_ORBIT_TIMES_S)
    {
        const FVector3d withSunGm = SOLMinorBodyOrbit::ComputePositionM(elements, FVector3d::ZeroVector,
            SOLTestHelpers::SUN_GM, timeS);
        const FVector3d withEarthGm = SOLMinorBodyOrbit::ComputePositionM(elements, FVector3d::ZeroVector,
            SOLTestHelpers::EARTH_GM, timeS);
        TestTrue(FString::Printf(TEXT("t=%.0f s same position for any GM"), timeS),
            SOLTestHelpers::VectorsNear(withEarthGm, withSunGm, MINOR_ORBIT_REL_TOLERANCE));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMinorBodyOrbitRealAsteroidTest, "SOLTest.MinorBodyOrbit.RealAsteroidSanePosition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A real catalog asteroid lands at a finite Sun distance between its perihelion and aphelion
bool FSOLMinorBodyOrbitRealAsteroidTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLAsteroidDef> asteroids = SOLAsteroidBelt::RealAsteroids();
    if (!TestTrue(TEXT("catalog is not empty"), asteroids.Num() > 0))
    {
        return false;
    }
    const FSOLSecularElements& elements = asteroids[0].Elements;
    const double semiMajorAxisM = elements.A0AU * SOLTestHelpers::AU_M;
    for (const double timeS : MINOR_ORBIT_TIMES_S)
    {
        const FVector3d positionM = SOLMinorBodyOrbit::ComputePositionM(elements, FVector3d::ZeroVector,
            SOLTestHelpers::SUN_GM, timeS);
        const double distanceM = positionM.Size();
        TestTrue(FString::Printf(TEXT("t=%.0f s position is finite"), timeS),
            SOLTestHelpers::IsFiniteVector(positionM));
        TestTrue(FString::Printf(TEXT("t=%.0f s distance %.4g m within [a(1-e), a(1+e)]"), timeS, distanceM),
            distanceM >= semiMajorAxisM * (1.0 - elements.E0) * (1.0 - 1e-9)
            && distanceM <= semiMajorAxisM * (1.0 + elements.E0) * (1.0 + 1e-9));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
