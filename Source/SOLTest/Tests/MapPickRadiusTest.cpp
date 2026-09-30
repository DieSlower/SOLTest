/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapBodyLod.h"
#include "Map/SOLMapPickRadius.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the jump-map pick radius (Docs/SDDs/3-jump-map.md, Appendix G): max(real radius, the world radius that
// spans minPickPx on screen at the camera distance). Radii and distances are meters; screen sizes are pixels
namespace
{
    constexpr double MAP_PICK_EARTH_RADIUS_M = 6.371e6;
    constexpr double MAP_PICK_VIEWPORT_PX = 720.0;
    constexpr double MAP_PICK_MIN_PX = 12.0;
    constexpr double MAP_PICK_REL_TOL = 1.0e-12;

    // A 1 m body, so the screen-size term dominates at every distance used below
    constexpr double MAP_PICK_TINY_RADIUS_M = 1.0;

    //////////////////////////////////////////////////////////////////////////
    // Returns the default map vertical FOV used by these tests (60 deg)
    double MapPickDefaultFovRad()
    {
        return FMath::DegreesToRadians(60.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's reference pick radius, computed independently of the code under test
    double MapPickReferenceRadiusM(const double radiusM, const double distanceM, const double minPickPx,
        const double viewportPx, const double fovRad)
    {
        const double focalPx = viewportPx / (2.0 * FMath::Tan(0.5 * fovRad));
        return FMath::Max(radiusM, minPickPx * distanceM / focalPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Calls the code under test with the default viewport and FOV
    double MapPickDefault(const double radiusM, const double distanceM, const double minPickPx)
    {
        return SOLMapPickRadius::ComputeMapPickRadiusM(radiusM, distanceM, minPickPx, MAP_PICK_VIEWPORT_PX,
            MapPickDefaultFovRad());
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickRadiusKnownCaseTest, "SOLTest.MapPickRadius.KnownCase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Earth at 1e10 m, 12 px, 720 px / 60 deg: 12 px span 12 * 1e10 / 623.538 = ~1.92450e8 m, larger than Earth
bool FSOLMapPickRadiusKnownCaseTest::RunTest(const FString& /*parameters*/)
{
    const double actual = MapPickDefault(MAP_PICK_EARTH_RADIUS_M, 1.0e10, MAP_PICK_MIN_PX);

    // focal = 720 / (2 * tan(30 deg)) = 623.5382907247958 px; pick = 12 * 1e10 / focal
    const double expected = 12.0 * 1.0e10 / (720.0 / (2.0 * FMath::Tan(UE_DOUBLE_PI / 6.0)));
    TestTrue(TEXT("Pick radius matches the formula"),
        SOLTestHelpers::RelativeError(actual, expected) <= MAP_PICK_REL_TOL);
    TestTrue(TEXT("Pick radius is ~1.9245009e8 m"), FMath::Abs(actual - 1.9245009e8) <= 1.0e2);
    TestTrue(TEXT("Pick radius exceeds Earth's radius"), actual > MAP_PICK_EARTH_RADIUS_M);

    // The same Earth at 1e7 m spans hundreds of px: the real radius wins, exactly
    TestEqual(TEXT("Close Earth pick radius is its real radius"),
        MapPickDefault(MAP_PICK_EARTH_RADIUS_M, 1.0e7, MAP_PICK_MIN_PX), MAP_PICK_EARTH_RADIUS_M, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickRadiusNeverBelowRealTest, "SOLTest.MapPickRadius.NeverBelowRealRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Across radii, distances and pixel sizes the pick radius is never smaller than the body and always matches the formula
bool FSOLMapPickRadiusNeverBelowRealTest::RunTest(const FString& /*parameters*/)
{
    const double radiiM[] = { 1.0, 1.0e4, MAP_PICK_EARTH_RADIUS_M, 6.9634e8 };
    const double distancesM[] = { 1.0e3, 1.0e6, 1.0e8, 1.0e10, 1.0e13 };
    const double pixels[] = { 0.5, MAP_PICK_MIN_PX, 64.0 };
    for (const double radiusM : radiiM)
    {
        for (const double distanceM : distancesM)
        {
            for (const double minPickPx : pixels)
            {
                const double actual = MapPickDefault(radiusM, distanceM, minPickPx);
                const double expected = MapPickReferenceRadiusM(radiusM, distanceM, minPickPx, MAP_PICK_VIEWPORT_PX,
                    MapPickDefaultFovRad());
                TestTrue(*FString::Printf(TEXT("r %g m, d %g m, %g px: pick >= real radius"), radiusM, distanceM,
                    minPickPx), actual >= radiusM);
                TestTrue(*FString::Printf(TEXT("r %g m, d %g m, %g px: matches the formula"), radiusM, distanceM,
                    minPickPx), SOLTestHelpers::RelativeError(actual, expected) <= MAP_PICK_REL_TOL);
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickRadiusInvalidInputsTest, "SOLTest.MapPickRadius.InvalidInputsFallBack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A non-positive distance (or pixel size, viewport or FOV, or FOV >= PI) returns the real radius; never negative
bool FSOLMapPickRadiusInvalidInputsTest::RunTest(const FString& /*parameters*/)
{
    const double fovRad = MapPickDefaultFovRad();
    const double r = MAP_PICK_EARTH_RADIUS_M;
    TestEqual(TEXT("DistanceM = 0 returns the real radius"),
        SOLMapPickRadius::ComputeMapPickRadiusM(r, 0.0, MAP_PICK_MIN_PX, MAP_PICK_VIEWPORT_PX, fovRad), r, 0.0);
    TestEqual(TEXT("DistanceM < 0 returns the real radius"),
        SOLMapPickRadius::ComputeMapPickRadiusM(r, -1.0e10, MAP_PICK_MIN_PX, MAP_PICK_VIEWPORT_PX, fovRad), r, 0.0);
    TestEqual(TEXT("minPickPx = 0 returns the real radius"),
        SOLMapPickRadius::ComputeMapPickRadiusM(r, 1.0e10, 0.0, MAP_PICK_VIEWPORT_PX, fovRad), r, 0.0);
    TestEqual(TEXT("Viewport height 0 returns the real radius"),
        SOLMapPickRadius::ComputeMapPickRadiusM(r, 1.0e10, MAP_PICK_MIN_PX, 0.0, fovRad), r, 0.0);
    TestEqual(TEXT("FOV 0 returns the real radius"),
        SOLMapPickRadius::ComputeMapPickRadiusM(r, 1.0e10, MAP_PICK_MIN_PX, MAP_PICK_VIEWPORT_PX, 0.0), r, 0.0);
    TestEqual(TEXT("FOV = PI returns the real radius"),
        SOLMapPickRadius::ComputeMapPickRadiusM(r, 1.0e10, MAP_PICK_MIN_PX, MAP_PICK_VIEWPORT_PX, UE_DOUBLE_PI), r,
        0.0);
    TestEqual(TEXT("Negative radius with an invalid distance returns 0"),
        SOLMapPickRadius::ComputeMapPickRadiusM(-r, 0.0, MAP_PICK_MIN_PX, MAP_PICK_VIEWPORT_PX, fovRad), 0.0, 0.0);
    TestTrue(TEXT("Negative radius with valid inputs is the positive screen-size radius"),
        SOLMapPickRadius::ComputeMapPickRadiusM(-r, 1.0e10, MAP_PICK_MIN_PX, MAP_PICK_VIEWPORT_PX, fovRad) > 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickRadiusDistanceTest, "SOLTest.MapPickRadius.ShrinksWithDistanceToRealRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Moving closer shrinks the pick radius linearly with distance until it reaches the real radius, never below it
bool FSOLMapPickRadiusDistanceTest::RunTest(const FString& /*parameters*/)
{
    // Screen-size regime: halving the distance halves the pick radius
    const double farM = MapPickDefault(MAP_PICK_EARTH_RADIUS_M, 1.0e11, MAP_PICK_MIN_PX);
    const double halfM = MapPickDefault(MAP_PICK_EARTH_RADIUS_M, 0.5e11, MAP_PICK_MIN_PX);
    TestTrue(TEXT("Far pick radius exceeds the real radius"), farM > MAP_PICK_EARTH_RADIUS_M);
    TestTrue(TEXT("Half the distance gives half the pick radius"),
        SOLTestHelpers::RelativeError(halfM, 0.5 * farM) <= MAP_PICK_REL_TOL);

    // Sweep inward by factors of 2: non-increasing, never below the real radius, and ends at exactly the real radius
    double previousM = farM;
    for (double distanceM = 1.0e11; distanceM >= 1.0e5; distanceM *= 0.5)
    {
        const double pickM = MapPickDefault(MAP_PICK_EARTH_RADIUS_M, distanceM, MAP_PICK_MIN_PX);
        TestTrue(*FString::Printf(TEXT("Non-increasing at %g m"), distanceM), pickM <= previousM);
        TestTrue(*FString::Printf(TEXT("Never below the real radius at %g m"), distanceM),
            pickM >= MAP_PICK_EARTH_RADIUS_M);
        previousM = pickM;
    }
    TestEqual(TEXT("Close in the pick radius is exactly the real radius"), previousM, MAP_PICK_EARTH_RADIUS_M, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickRadiusPixelsTest, "SOLTest.MapPickRadius.LinearInMinPickPx",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// In the screen-size regime the pick radius is proportional to minPickPx
bool FSOLMapPickRadiusPixelsTest::RunTest(const FString& /*parameters*/)
{
    const double baseM = MapPickDefault(MAP_PICK_TINY_RADIUS_M, 1.0e9, MAP_PICK_MIN_PX);
    const double doubleM = MapPickDefault(MAP_PICK_TINY_RADIUS_M, 1.0e9, 2.0 * MAP_PICK_MIN_PX);
    const double tripleM = MapPickDefault(MAP_PICK_TINY_RADIUS_M, 1.0e9, 3.0 * MAP_PICK_MIN_PX);
    TestTrue(TEXT("Base result is in the screen-size regime"), baseM > MAP_PICK_TINY_RADIUS_M);
    TestTrue(TEXT("Double minPickPx gives double the radius"),
        SOLTestHelpers::RelativeError(doubleM, 2.0 * baseM) <= MAP_PICK_REL_TOL);
    TestTrue(TEXT("Triple minPickPx gives triple the radius"),
        SOLTestHelpers::RelativeError(tripleM, 3.0 * baseM) <= MAP_PICK_REL_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapPickRadiusProjectionTest, "SOLTest.MapPickRadius.FollowsViewportAndFov",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The inverse of SOLMapBodyLod's projection: a taller viewport shrinks the pick radius, a wider FOV grows it by the
// ratio of tan(FOV / 2)
bool FSOLMapPickRadiusProjectionTest::RunTest(const FString& /*parameters*/)
{
    const double distanceM = 1.0e9;
    const double baseFov = MapPickDefaultFovRad();
    const double baseM = SOLMapPickRadius::ComputeMapPickRadiusM(MAP_PICK_TINY_RADIUS_M, distanceM, MAP_PICK_MIN_PX,
        MAP_PICK_VIEWPORT_PX, baseFov);
    const double tallM = SOLMapPickRadius::ComputeMapPickRadiusM(MAP_PICK_TINY_RADIUS_M, distanceM, MAP_PICK_MIN_PX,
        2.0 * MAP_PICK_VIEWPORT_PX, baseFov);
    const double wideM = SOLMapPickRadius::ComputeMapPickRadiusM(MAP_PICK_TINY_RADIUS_M, distanceM, MAP_PICK_MIN_PX,
        MAP_PICK_VIEWPORT_PX, UE_DOUBLE_HALF_PI);
    const double narrowM = SOLMapPickRadius::ComputeMapPickRadiusM(MAP_PICK_TINY_RADIUS_M, distanceM, MAP_PICK_MIN_PX,
        MAP_PICK_VIEWPORT_PX, FMath::DegreesToRadians(20.0));

    TestTrue(TEXT("Double viewport height halves the pick radius"),
        SOLTestHelpers::RelativeError(tallM, 0.5 * baseM) <= MAP_PICK_REL_TOL);
    const double expectedWideRatio = FMath::Tan(0.5 * UE_DOUBLE_HALF_PI) / FMath::Tan(0.5 * baseFov);
    TestTrue(TEXT("Wide/base ratio follows tan(FOV/2)"),
        SOLTestHelpers::RelativeError(wideM / baseM, expectedWideRatio) <= MAP_PICK_REL_TOL);
    TestTrue(TEXT("Wider FOV grows the pick radius"), wideM > baseM);
    TestTrue(TEXT("Narrower FOV shrinks the pick radius"), narrowM < baseM);

    // Round trip through the LOD projection: the pick radius projects to exactly minPickPx (radius, not diameter)
    FSOLBodyLodParams params;
    params.ViewportHeightPx = MAP_PICK_VIEWPORT_PX;
    params.VerticalFovRad = baseFov;
    const double projectedRadiusPx = 0.5 * SOLMapBodyLod::ComputeApparentDiameterPx(baseM, distanceM, params);
    TestTrue(TEXT("Pick radius projects to minPickPx through SOLMapBodyLod"),
        SOLTestHelpers::RelativeError(projectedRadiusPx, MAP_PICK_MIN_PX) <= 1.0e-9);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
