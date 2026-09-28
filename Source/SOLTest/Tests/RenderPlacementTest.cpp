/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLRenderPlacement.h"

#include "SOLConstants.h"
#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the expected far depth d' = Max * (1 + FarDepthLogScale * ln(d / Max)), independent of the code under test
    double ExpectedFarDepthCm(const double distanceCm, const double maxCm)
    {
        return maxCm * (1.0 + 0.1 * FMath::Loge(distanceCm / maxCm));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderPlacementWithinRangeTest, "SOLTest.RenderPlacement.WithinRangeIsOneToOne",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Within MaxRenderDistanceCm the placement is exactly meters * 100 for both location and radius
bool FSOLRenderPlacementWithinRangeTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("MetersToCm"), SOLRender::MetersToCm, 100.0, 0.0);

    const FVector3d relative(1000.0, -2000.0, 3000.0);
    const FSOLRenderPlacement placement = SOLRender::ComputePlacement(relative, 50.0, 1.0e8);
    TestTrue(TEXT("Location is exactly relative * 100"), placement.LocationCm == FVector3d(1.0e5, -2.0e5, 3.0e5));
    TestEqual(TEXT("Radius is exactly radius * 100"), placement.RadiusCm, 5000.0, 0.0);

    // Near the edge of a large range: 9,000 km inside a 10,000 km range
    const FVector3d nearRelative(0.0, 9.0e6, 0.0);
    const FSOLRenderPlacement nearPlacement = SOLRender::ComputePlacement(nearRelative, 6.371e6, 1.0e9);
    TestTrue(TEXT("9,000 km inside 10,000 km range is 1:1"), nearPlacement.LocationCm == FVector3d(0.0, 9.0e8, 0.0));
    TestEqual(TEXT("Earth radius 1:1"), nearPlacement.RadiusCm, 6.371e8, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderPlacementBoundaryTest, "SOLTest.RenderPlacement.AtRangeBoundaryIsOneToOne",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Exactly at MaxRenderDistanceCm the placement is still 1:1 (the range is inclusive)
bool FSOLRenderPlacementBoundaryTest::RunTest(const FString& /*parameters*/)
{
    // (3, 4, 0) m is 500 cm
    const FSOLRenderPlacement placement = SOLRender::ComputePlacement(FVector3d(3.0, 4.0, 0.0), 2.0, 500.0);
    TestTrue(TEXT("Boundary location is 1:1"),
        SOLTestHelpers::VectorsNear(placement.LocationCm, FVector3d(300.0, 400.0, 0.0), 1e-15));
    TestEqual(TEXT("Boundary radius is 1:1"), placement.RadiusCm, 200.0, 1e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderPlacementFarDepthTest, "SOLTest.RenderPlacement.BeyondRangeLogDepth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Beyond range the body sits at d' = Max*(1 + 0.1*ln(d/Max)) along the same direction with the same angular size
bool FSOLRenderPlacementFarDepthTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("FarDepthLogScale is 0.1"), SOLRender::FarDepthLogScale, 0.1, 0.0);

    // Relative position, radius and max distance for each case
    struct FSOLPlacementCase
    {
        FVector3d RelativeM;
        double RadiusM;
        double MaxCm;
    };
    const FSOLPlacementCase cases[] =
    {
        { FVector3d(1.0, 2.0, -2.0) * (SOLTestHelpers::AU_M / 3.0), 6.371e6, 1.0e7 }, // Earth-distance, 100 km range
        { FVector3d(-4.5e12, 1.0e11, 3.0e10), 2.4622e7, 1.0e9 },                      // Neptune-distance
        { FVector3d(0.0, 0.0, -7.0e8), 6.957e8, 5.0e8 },                              // Sun from just above its surface
        { FVector3d(3.0, 4.0, 0.0), 2.0, 499.999 },                                   // just past the boundary
        { FVector3d(1.0e12, -2.0e12, 5.0e11), 6.9911e7, SOL::DEFAULT_MAX_RENDER_DISTANCE_CM }, // default range
    };
    for (const FSOLPlacementCase& testCase : cases)
    {
        const FSOLRenderPlacement placement = SOLRender::ComputePlacement(testCase.RelativeM, testCase.RadiusM,
            testCase.MaxCm);
        const double trueDistanceCm = testCase.RelativeM.Size() * 100.0;
        const double expectedDepthCm = ExpectedFarDepthCm(trueDistanceCm, testCase.MaxCm);
        const double distanceCm = placement.LocationCm.Size();
        const double expectedAngular = testCase.RadiusM / testCase.RelativeM.Size();
        const double angular = placement.RadiusCm / FMath::Max(distanceCm, UE_DOUBLE_SMALL_NUMBER);
        const FString label = FString::Printf(TEXT("rel=(%.3e, %.3e, %.3e) max=%.3f"), testCase.RelativeM.X,
            testCase.RelativeM.Y, testCase.RelativeM.Z, testCase.MaxCm);

        TestTrue(*FString::Printf(TEXT("%s: |Location| %.9f == d' %.9f"), *label, distanceCm, expectedDepthCm),
            SOLTestHelpers::RelativeError(distanceCm, expectedDepthCm) < 1e-12);
        TestTrue(*FString::Printf(TEXT("%s: |Location| %.9f >= max"), *label, distanceCm),
            distanceCm >= testCase.MaxCm);
        TestTrue(*FString::Printf(TEXT("%s: angular size %.15e == %.15e"), *label, angular, expectedAngular),
            SOLTestHelpers::RelativeError(angular, expectedAngular) < 1e-12);
        TestTrue(*FString::Printf(TEXT("%s: direction preserved"), *label),
            SOLTestHelpers::VectorsNear(placement.LocationCm.GetSafeNormal(), testCase.RelativeM.GetSafeNormal(),
                1e-12));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderPlacementContinuityTest, "SOLTest.RenderPlacement.ContinuousAtRangeBoundary",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Placements just below and just above MaxRenderDistanceCm agree, for both location and radius
bool FSOLRenderPlacementContinuityTest::RunTest(const FString& /*parameters*/)
{
    const double maxCms[] = { 500.0, 1.0e9, SOL::DEFAULT_MAX_RENDER_DISTANCE_CM };
    const FVector3d direction = FVector3d(2.0, -3.0, 6.0) / 7.0;
    for (const double maxCm : maxCms)
    {
        const double maxM = maxCm / 100.0;
        const double radiusM = maxM * 0.01;
        const FSOLRenderPlacement below = SOLRender::ComputePlacement(direction * (maxM * (1.0 - 1e-9)), radiusM, maxCm);
        const FSOLRenderPlacement above = SOLRender::ComputePlacement(direction * (maxM * (1.0 + 1e-9)), radiusM, maxCm);
        TestTrue(*FString::Printf(TEXT("max=%.3e: |Location| continuous (%.12e vs %.12e)"), maxCm,
            below.LocationCm.Size(), above.LocationCm.Size()),
            SOLTestHelpers::RelativeError(above.LocationCm.Size(), below.LocationCm.Size()) < 1e-8);
        TestTrue(*FString::Printf(TEXT("max=%.3e: location continuous"), maxCm),
            SOLTestHelpers::VectorsNear(above.LocationCm, below.LocationCm, 1e-8));
        TestTrue(*FString::Printf(TEXT("max=%.3e: radius continuous (%.12e vs %.12e)"), maxCm, below.RadiusCm,
            above.RadiusCm), SOLTestHelpers::RelativeError(above.RadiusCm, below.RadiusCm) < 1e-8);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderPlacementMonotonicTest, "SOLTest.RenderPlacement.FarDepthStrictlyMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Beyond range the placed distance strictly increases with the true distance from 1.0001*Max up to 1e6*Max
bool FSOLRenderPlacementMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const double maxCm = SOL::DEFAULT_MAX_RENDER_DISTANCE_CM;
    const FVector3d direction = FVector3d(-1.0, 4.0, 8.0) / 9.0;
    const double startCm = 1.0001 * maxCm;
    const double endCm = 1.0e6 * maxCm;
    constexpr int32 steps = 600;

    // Geometric sweep of true distances; each placed distance must exceed the previous one
    double previousCm = -1.0;
    for (int32 step = 0; step <= steps; ++step)
    {
        const double distanceCm = startCm * FMath::Pow(endCm / startCm, static_cast<double>(step) / steps);
        const FSOLRenderPlacement placement = SOLRender::ComputePlacement(direction * (distanceCm / 100.0), 1.0e6,
            maxCm);
        const double placedCm = placement.LocationCm.Size();
        if (!TestTrue(FString::Printf(TEXT("d=%.6e: placed %.12e >= max"), distanceCm, placedCm), placedCm >= maxCm)
            || !TestTrue(FString::Printf(TEXT("d=%.6e: placed %.12e > previous %.12e"), distanceCm, placedCm,
                previousCm), placedCm > previousCm))
        {
            break;
        }
        previousCm = placedCm;
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderPlacementDepthOrderTest, "SOLTest.RenderPlacement.NearSmallBodyNotSwallowed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A small near body on the same line of sight as a large far body stays in front of the far body's front surface
bool FSOLRenderPlacementDepthOrderTest::RunTest(const FString& /*parameters*/)
{
    const FVector3d direction = FVector3d(1.0, 2.0, -2.0) / 3.0;
    const double maxCms[] = { 1.0e9, SOL::DEFAULT_MAX_RENDER_DISTANCE_CM };
    for (const double maxCm : maxCms)
    {
        // Sun-like body at 1.5e11 m, Venus-like body at 4.2e10 m
        const FSOLRenderPlacement farBody = SOLRender::ComputePlacement(direction * 1.5e11, 6.957e8, maxCm);
        const FSOLRenderPlacement nearBody = SOLRender::ComputePlacement(direction * 4.2e10, 6.05e6, maxCm);
        const double farFrontCm = farBody.LocationCm.Size() - farBody.RadiusCm;
        const double nearCm = nearBody.LocationCm.Size();
        TestTrue(*FString::Printf(TEXT("max=%.3e: near body %.9e is in front of far front surface %.9e"), maxCm, nearCm,
            farFrontCm), nearCm < farFrontCm);
        TestTrue(*FString::Printf(TEXT("max=%.3e: near body placed nearer than far body"), maxCm),
            nearCm < farBody.LocationCm.Size());
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderPlacementZeroTest, "SOLTest.RenderPlacement.ZeroDistanceNoNaN",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A body at the observer's position produces finite values: location zero, radius 1:1
bool FSOLRenderPlacementZeroTest::RunTest(const FString& /*parameters*/)
{
    const FSOLRenderPlacement placement = SOLRender::ComputePlacement(FVector3d::ZeroVector, 10.0, 1.0e6);
    TestTrue(TEXT("Location is finite"), SOLTestHelpers::IsFiniteVector(placement.LocationCm));
    TestTrue(TEXT("Radius is finite"), FMath::IsFinite(placement.RadiusCm));
    TestTrue(TEXT("Location is zero"), placement.LocationCm.IsNearlyZero(0.0));
    TestEqual(TEXT("Radius is 1:1"), placement.RadiusCm, 1000.0, 0.0);

    // Zero range with zero distance is still inside the inclusive range
    const FSOLRenderPlacement zeroRange = SOLRender::ComputePlacement(FVector3d::ZeroVector, 3.0, 0.0);
    TestTrue(TEXT("Zero range: location finite"), SOLTestHelpers::IsFiniteVector(zeroRange.LocationCm));
    TestEqual(TEXT("Zero range: radius 1:1"), zeroRange.RadiusCm, 300.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRenderPlacementAxesTest, "SOLTest.RenderPlacement.EclipticToUnrealFlipsY",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// EclipticToUnreal negates Y only, and applying it twice is the identity
bool FSOLRenderPlacementAxesTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("(1, 2, 3) -> (1, -2, 3)"),
        SOLRender::EclipticToUnreal(FVector3d(1.0, 2.0, 3.0)) == FVector3d(1.0, -2.0, 3.0));
    const FVector3d big(-4.5, -1.0e12, 0.25);
    TestTrue(TEXT("(-4.5, -1e12, 0.25) -> (-4.5, 1e12, 0.25)"),
        SOLRender::EclipticToUnreal(big) == FVector3d(-4.5, 1.0e12, 0.25));
    TestTrue(TEXT("Applying twice is the identity"), SOLRender::EclipticToUnreal(SOLRender::EclipticToUnreal(big)) == big);
    TestTrue(TEXT("Zero stays zero"), SOLRender::EclipticToUnreal(FVector3d::ZeroVector).IsNearlyZero(0.0));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
