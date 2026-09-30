/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapBodyLod.h"

#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the jump-map body LOD contract (Docs/SDDs/3-jump-map.md, Appendix G). Radii and distances are meters;
// apparent sizes are pixels
namespace
{
    constexpr double MAP_LOD_EARTH_RADIUS_M = 6.371e6;

    // Relative tolerance for results that are a handful of double operations away from the reference formula
    constexpr double MAP_LOD_REL_TOL = 1.0e-12;

    // Absolute tolerance for IconAlpha when the apparent size is reconstructed through a radius/distance round trip
    constexpr double MAP_LOD_ALPHA_TOL = 1.0e-9;

    // Camera distance used when constructing a body of a chosen apparent size
    constexpr double MAP_LOD_PROBE_DISTANCE_M = 1.0e9;

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's reference apparent diameter, computed independently of the code under test
    double MapLodReferenceDiameterPx(const double radiusM, const double distanceM, const FSOLBodyLodParams& params)
    {
        const double focalPx = params.ViewportHeightPx / (2.0 * FMath::Tan(params.VerticalFovRad * 0.5));
        return (2.0 * radiusM / distanceM) * focalPx;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the radius that makes a sphere at MAP_LOD_PROBE_DISTANCE_M appear targetPx wide under the given params
    double MapLodRadiusForDiameterPx(const double targetPx, const FSOLBodyLodParams& params)
    {
        const double focalPx = params.ViewportHeightPx / (2.0 * FMath::Tan(params.VerticalFovRad * 0.5));
        return targetPx * MAP_LOD_PROBE_DISTANCE_M / (2.0 * focalPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns params whose viewport height equals 2*tan(FOV/2), making the projection's focal length exactly 1.0 px
    // (x / x), so a radius r at distance 1 m projects to exactly 2*r px with no rounding
    FSOLBodyLodParams MapLodUnitFocalParams(const double iconFullBelowPx, const double meshFullAbovePx)
    {
        FSOLBodyLodParams params;
        params.IconFullBelowPx = iconFullBelowPx;
        params.MeshFullAbovePx = meshFullAbovePx;
        params.ViewportHeightPx = 2.0 * FMath::Tan(params.VerticalFovRad * 0.5);
        return params;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a value is finite and inside [0, 1]
    bool MapLodIsUnitInterval(const double value)
    {
        return FMath::IsFinite(value) && value >= 0.0 && value <= 1.0;
    }
}

// --- ComputeApparentDiameterPx ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodEarthKnownCaseTest, "SOLTest.MapBodyLod.DiameterEarthKnownCase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Earth at 1e10 m with the default 720 px / 60 deg projection matches the closed-form value (~0.79451 px)
bool FSOLMapBodyLodEarthKnownCaseTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    const double actual = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e10, params);

    // focal = 720 / (2 * tan(30 deg)) = 623.5382907247958 px; diameter = 2 * 6.371e6 / 1e10 * focal
    const double expected = 2.0 * 6.371e6 / 1.0e10 * (720.0 / (2.0 * FMath::Tan(UE_DOUBLE_PI / 6.0)));
    TestTrue(TEXT("Earth apparent diameter matches the formula"),
        SOLTestHelpers::RelativeError(actual, expected) <= MAP_LOD_REL_TOL);
    TestTrue(TEXT("Earth apparent diameter is ~0.79451 px"), FMath::Abs(actual - 0.7945125) <= 1.0e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodDistanceScalingTest, "SOLTest.MapBodyLod.DiameterInverseInDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Doubling the distance halves the apparent diameter
bool FSOLMapBodyLodDistanceScalingTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    const double nearPx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, params);
    const double farPx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 2.0e9, params);
    TestTrue(TEXT("Near result is positive"), nearPx > 0.0);
    TestTrue(TEXT("Double distance gives half the diameter"),
        SOLTestHelpers::RelativeError(farPx, 0.5 * nearPx) <= MAP_LOD_REL_TOL);
    TestTrue(TEXT("Near result matches the formula"),
        SOLTestHelpers::RelativeError(nearPx, MapLodReferenceDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, params))
            <= MAP_LOD_REL_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodRadiusScalingTest, "SOLTest.MapBodyLod.DiameterLinearInRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Doubling the radius doubles the apparent diameter
bool FSOLMapBodyLodRadiusScalingTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    const double smallPx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 5.0e9, params);
    const double bigPx = SOLMapBodyLod::ComputeApparentDiameterPx(2.0 * MAP_LOD_EARTH_RADIUS_M, 5.0e9, params);
    TestTrue(TEXT("Small result is positive"), smallPx > 0.0);
    TestTrue(TEXT("Double radius gives double the diameter"),
        SOLTestHelpers::RelativeError(bigPx, 2.0 * smallPx) <= MAP_LOD_REL_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodInvalidDistanceTest,
    "SOLTest.MapBodyLod.DiameterNonPositiveDistanceIsZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or negative distance has no valid apparent size and returns exactly 0
bool FSOLMapBodyLodInvalidDistanceTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    TestEqual(TEXT("DistanceM = 0 returns 0"),
        SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 0.0, params), 0.0, 0.0);
    TestEqual(TEXT("DistanceM < 0 returns 0"),
        SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, -1.0e9, params), 0.0, 0.0);
    TestEqual(TEXT("Tiny negative DistanceM returns 0"),
        SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, -1.0e-9, params), 0.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodInvalidRadiusTest, "SOLTest.MapBodyLod.DiameterNonPositiveRadiusIsZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero or negative radius has no valid apparent size and returns exactly 0 (never a negative diameter)
bool FSOLMapBodyLodInvalidRadiusTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    TestEqual(TEXT("RadiusM = 0 returns 0"), SOLMapBodyLod::ComputeApparentDiameterPx(0.0, 1.0e9, params), 0.0, 0.0);
    TestEqual(TEXT("RadiusM < 0 returns 0"),
        SOLMapBodyLod::ComputeApparentDiameterPx(-MAP_LOD_EARTH_RADIUS_M, 1.0e9, params), 0.0, 0.0);
    TestEqual(TEXT("RadiusM < 0 and DistanceM < 0 returns 0"),
        SOLMapBodyLod::ComputeApparentDiameterPx(-MAP_LOD_EARTH_RADIUS_M, -1.0e9, params), 0.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodViewportScalingTest, "SOLTest.MapBodyLod.DiameterLinearInViewportHeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Doubling the viewport height doubles the apparent diameter
bool FSOLMapBodyLodViewportScalingTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams baseParams;
    FSOLBodyLodParams tallParams;
    tallParams.ViewportHeightPx = 2.0 * baseParams.ViewportHeightPx;

    const double basePx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, baseParams);
    const double tallPx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, tallParams);
    TestTrue(TEXT("Base result is positive"), basePx > 0.0);
    TestTrue(TEXT("Double viewport height gives double the diameter"),
        SOLTestHelpers::RelativeError(tallPx, 2.0 * basePx) <= MAP_LOD_REL_TOL);
    TestTrue(TEXT("Tall result matches the formula"),
        SOLTestHelpers::RelativeError(tallPx, MapLodReferenceDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, tallParams))
            <= MAP_LOD_REL_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodFovTest, "SOLTest.MapBodyLod.DiameterFollowsFovTangent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Changing the vertical FOV scales the apparent diameter by the inverse ratio of tan(FOV / 2)
bool FSOLMapBodyLodFovTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams baseParams;
    FSOLBodyLodParams wideParams;
    wideParams.VerticalFovRad = UE_DOUBLE_HALF_PI;   // 90 deg: tan(45 deg) = 1
    FSOLBodyLodParams narrowParams;
    narrowParams.VerticalFovRad = FMath::DegreesToRadians(20.0);

    const double basePx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, baseParams);
    const double widePx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, wideParams);
    const double narrowPx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, narrowParams);

    TestTrue(TEXT("Base result is positive"), basePx > 0.0);
    TestTrue(TEXT("Wide FOV matches the formula"),
        SOLTestHelpers::RelativeError(widePx, MapLodReferenceDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, wideParams))
            <= MAP_LOD_REL_TOL);
    TestTrue(TEXT("Narrow FOV matches the formula"),
        SOLTestHelpers::RelativeError(narrowPx,
            MapLodReferenceDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e9, narrowParams)) <= MAP_LOD_REL_TOL);

    // Ratio check: wide / base = tan(30 deg) / tan(45 deg)
    const double expectedRatio =
        FMath::Tan(baseParams.VerticalFovRad * 0.5) / FMath::Tan(wideParams.VerticalFovRad * 0.5);
    TestTrue(TEXT("Wide/base ratio follows tan(FOV/2)"),
        SOLTestHelpers::RelativeError(widePx / basePx, expectedRatio) <= MAP_LOD_REL_TOL);
    TestTrue(TEXT("Wider FOV makes the body smaller"), widePx < basePx);
    TestTrue(TEXT("Narrower FOV makes the body larger"), narrowPx > basePx);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodSolarScaleTest, "SOLTest.MapBodyLod.DiameterSolarScaleStaysPrecise",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At ~1e13 m (map default view) Earth is sub-pixel yet finite, nonzero and still exact to the formula
bool FSOLMapBodyLodSolarScaleTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    const double farPx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e13, params);
    TestTrue(TEXT("Result is finite"), FMath::IsFinite(farPx));
    TestTrue(TEXT("Result is nonzero"), farPx > 0.0);
    TestTrue(TEXT("Result is sub-pixel"), farPx < 1.0e-3);
    TestTrue(TEXT("Result matches the formula"),
        SOLTestHelpers::RelativeError(farPx, MapLodReferenceDiameterPx(MAP_LOD_EARTH_RADIUS_M, 1.0e13, params))
            <= MAP_LOD_REL_TOL);

    // A small moon at 100 AU: still positive and precise
    const double moonPx = SOLMapBodyLod::ComputeApparentDiameterPx(1.0e4, 100.0 * SOLTestHelpers::AU_M, params);
    TestTrue(TEXT("Tiny far body is positive and finite"), moonPx > 0.0 && FMath::IsFinite(moonPx));
    TestTrue(TEXT("Tiny far body matches the formula"),
        SOLTestHelpers::RelativeError(moonPx, MapLodReferenceDiameterPx(1.0e4, 100.0 * SOLTestHelpers::AU_M, params))
            <= MAP_LOD_REL_TOL);
    return true;
}

// --- Evaluate ---

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodEvaluateDiameterTest, "SOLTest.MapBodyLod.EvaluateDiameterAgrees",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Evaluate's ApparentDiameterPx equals ComputeApparentDiameterPx (and the formula) for the same inputs
bool FSOLMapBodyLodEvaluateDiameterTest::RunTest(const FString& /*parameters*/)
{
    FSOLBodyLodParams customParams;
    customParams.ViewportHeightPx = 1080.0;
    customParams.VerticalFovRad = FMath::DegreesToRadians(75.0);

    const FSOLBodyLodParams paramSets[] = { FSOLBodyLodParams(), customParams };
    const double distancesM[] = { 1.0e7, 1.0e9, 1.0e11, 1.0e13 };
    for (const FSOLBodyLodParams& params : paramSets)
    {
        for (const double distanceM : distancesM)
        {
            const double computedPx = SOLMapBodyLod::ComputeApparentDiameterPx(MAP_LOD_EARTH_RADIUS_M, distanceM,
                params);
            const FSOLBodyLodResult result = SOLMapBodyLod::Evaluate(MAP_LOD_EARTH_RADIUS_M, distanceM, params);
            const double referencePx = MapLodReferenceDiameterPx(MAP_LOD_EARTH_RADIUS_M, distanceM, params);
            TestTrue(*FString::Printf(TEXT("Evaluate diameter matches Compute at %g m"), distanceM),
                SOLTestHelpers::RelativeError(result.ApparentDiameterPx, computedPx) <= MAP_LOD_REL_TOL);
            TestTrue(*FString::Printf(TEXT("Evaluate diameter matches the formula at %g m"), distanceM),
                SOLTestHelpers::RelativeError(result.ApparentDiameterPx, referencePx) <= MAP_LOD_REL_TOL);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodMeshOnlyTest, "SOLTest.MapBodyLod.EvaluateMeshOnlyAboveThreshold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At or above MeshFullAbovePx the body is mesh only: IconAlpha is exactly 0
bool FSOLMapBodyLodMeshOnlyTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    const double sizesPx[] = { 2.0 * params.MeshFullAbovePx, 1000.0, 1.0e6 };
    for (const double sizePx : sizesPx)
    {
        const FSOLBodyLodResult result = SOLMapBodyLod::Evaluate(MapLodRadiusForDiameterPx(sizePx, params),
            MAP_LOD_PROBE_DISTANCE_M, params);
        TestTrue(*FString::Printf(TEXT("Apparent size is ~%g px"), sizePx),
            SOLTestHelpers::RelativeError(result.ApparentDiameterPx, sizePx) <= 1.0e-9);
        TestEqual(*FString::Printf(TEXT("IconAlpha is exactly 0 at %g px"), sizePx), result.IconAlpha, 0.0, 0.0);
    }

    // Exactly at the threshold (to within round-trip error): alpha is 0 to within that error
    const FSOLBodyLodResult atThreshold = SOLMapBodyLod::Evaluate(
        MapLodRadiusForDiameterPx(params.MeshFullAbovePx, params), MAP_LOD_PROBE_DISTANCE_M, params);
    TestTrue(TEXT("Apparent size is ~MeshFullAbovePx"),
        SOLTestHelpers::RelativeError(atThreshold.ApparentDiameterPx, params.MeshFullAbovePx) <= 1.0e-9);
    TestTrue(TEXT("IconAlpha ~0 at MeshFullAbovePx"), FMath::Abs(atThreshold.IconAlpha) <= MAP_LOD_ALPHA_TOL);

    // Real-world case: Earth at 1e7 m (hundreds of px) is mesh only
    const FSOLBodyLodResult nearEarth = SOLMapBodyLod::Evaluate(MAP_LOD_EARTH_RADIUS_M, 1.0e7, params);
    TestTrue(TEXT("Near Earth is well above the mesh threshold"),
        nearEarth.ApparentDiameterPx > params.MeshFullAbovePx);
    TestEqual(TEXT("Near Earth IconAlpha is exactly 0"), nearEarth.IconAlpha, 0.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodIconOnlyTest, "SOLTest.MapBodyLod.EvaluateIconOnlyBelowThreshold",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At or below IconFullBelowPx the body is icon only: IconAlpha is exactly 1
bool FSOLMapBodyLodIconOnlyTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    const double sizesPx[] = { 0.5 * params.IconFullBelowPx, 1.0, 1.0e-4 };
    for (const double sizePx : sizesPx)
    {
        const FSOLBodyLodResult result = SOLMapBodyLod::Evaluate(MapLodRadiusForDiameterPx(sizePx, params),
            MAP_LOD_PROBE_DISTANCE_M, params);
        TestTrue(*FString::Printf(TEXT("Apparent size is ~%g px"), sizePx),
            SOLTestHelpers::RelativeError(result.ApparentDiameterPx, sizePx) <= 1.0e-9);
        TestEqual(*FString::Printf(TEXT("IconAlpha is exactly 1 at %g px"), sizePx), result.IconAlpha, 1.0, 0.0);
    }

    // Exactly at the threshold (to within round-trip error): alpha is 1 to within that error
    const FSOLBodyLodResult atThreshold = SOLMapBodyLod::Evaluate(
        MapLodRadiusForDiameterPx(params.IconFullBelowPx, params), MAP_LOD_PROBE_DISTANCE_M, params);
    TestTrue(TEXT("IconAlpha ~1 at IconFullBelowPx"), FMath::Abs(atThreshold.IconAlpha - 1.0) <= MAP_LOD_ALPHA_TOL);

    // Real-world case from the 2b-2 smoke test: Earth at the 1e13 m default view is icon only
    const FSOLBodyLodResult farEarth = SOLMapBodyLod::Evaluate(MAP_LOD_EARTH_RADIUS_M, 1.0e13, params);
    TestEqual(TEXT("Far Earth IconAlpha is exactly 1"), farEarth.IconAlpha, 1.0, 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodMidpointTest, "SOLTest.MapBodyLod.EvaluateMidpointIsHalf",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Halfway between the two thresholds IconAlpha is 0.5
bool FSOLMapBodyLodMidpointTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    const double midPx = 0.5 * (params.IconFullBelowPx + params.MeshFullAbovePx);   // 18 px with defaults
    const FSOLBodyLodResult result = SOLMapBodyLod::Evaluate(MapLodRadiusForDiameterPx(midPx, params),
        MAP_LOD_PROBE_DISTANCE_M, params);
    TestTrue(TEXT("Apparent size is the midpoint"),
        SOLTestHelpers::RelativeError(result.ApparentDiameterPx, midPx) <= 1.0e-9);
    TestTrue(TEXT("IconAlpha is 0.5 at the midpoint"), FMath::Abs(result.IconAlpha - 0.5) <= MAP_LOD_ALPHA_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodLinearTest, "SOLTest.MapBodyLod.EvaluateLinearBetweenThresholds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Between the thresholds IconAlpha = (MeshFullAbovePx - size) / (MeshFullAbovePx - IconFullBelowPx)
bool FSOLMapBodyLodLinearTest::RunTest(const FString& /*parameters*/)
{
    // Default thresholds, then custom thresholds and projection so a hard-coded 4/32 implementation fails
    FSOLBodyLodParams customParams;
    customParams.IconFullBelowPx = 10.0;
    customParams.MeshFullAbovePx = 110.0;
    customParams.ViewportHeightPx = 1440.0;
    customParams.VerticalFovRad = FMath::DegreesToRadians(45.0);

    const FSOLBodyLodParams paramSets[] = { FSOLBodyLodParams(), customParams };
    const double fractions[] = { 0.1, 0.25, 0.75, 0.9 };
    for (const FSOLBodyLodParams& params : paramSets)
    {
        const double spanPx = params.MeshFullAbovePx - params.IconFullBelowPx;
        for (const double fraction : fractions)
        {
            // fraction of the way from IconFullBelowPx up to MeshFullAbovePx -> alpha = 1 - fraction
            const double sizePx = params.IconFullBelowPx + fraction * spanPx;
            const FSOLBodyLodResult result = SOLMapBodyLod::Evaluate(MapLodRadiusForDiameterPx(sizePx, params),
                MAP_LOD_PROBE_DISTANCE_M, params);
            const double expectedAlpha = (params.MeshFullAbovePx - result.ApparentDiameterPx) / spanPx;
            TestTrue(*FString::Printf(TEXT("Apparent size ~%g px"), sizePx),
                SOLTestHelpers::RelativeError(result.ApparentDiameterPx, sizePx) <= 1.0e-9);
            TestTrue(*FString::Printf(TEXT("IconAlpha follows the linear formula at %g px"), sizePx),
                FMath::Abs(result.IconAlpha - expectedAlpha) <= MAP_LOD_ALPHA_TOL);
            TestTrue(*FString::Printf(TEXT("IconAlpha ~%g at fraction %g"), 1.0 - fraction, fraction),
                FMath::Abs(result.IconAlpha - (1.0 - fraction)) <= MAP_LOD_ALPHA_TOL);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodDegenerateTest, "SOLTest.MapBodyLod.EvaluateDegenerateIsIconOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Invalid radius or distance gives ApparentDiameterPx 0, which is below IconFullBelowPx, so IconAlpha is exactly 1
bool FSOLMapBodyLodDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const FSOLBodyLodParams params;
    struct FDegenerateCase
    {
        const TCHAR* Label;
        double RadiusM;
        double DistanceM;
    };
    const FDegenerateCase cases[] = {
        { TEXT("DistanceM = 0"), MAP_LOD_EARTH_RADIUS_M, 0.0 },
        { TEXT("DistanceM < 0"), MAP_LOD_EARTH_RADIUS_M, -1.0e9 },
        { TEXT("RadiusM = 0"), 0.0, 1.0e9 },
        { TEXT("RadiusM < 0"), -MAP_LOD_EARTH_RADIUS_M, 1.0e9 },
    };
    for (const FDegenerateCase& degenerate : cases)
    {
        const FSOLBodyLodResult result = SOLMapBodyLod::Evaluate(degenerate.RadiusM, degenerate.DistanceM, params);
        TestEqual(*FString::Printf(TEXT("%s: ApparentDiameterPx is 0"), degenerate.Label),
            result.ApparentDiameterPx, 0.0, 0.0);
        TestEqual(*FString::Printf(TEXT("%s: IconAlpha is exactly 1"), degenerate.Label), result.IconAlpha, 1.0, 0.0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLMapBodyLodInvertedThresholdsTest,
    "SOLTest.MapBodyLod.EvaluateInvertedThresholdsClamped",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Out-of-order (MeshFullAbovePx < IconFullBelowPx) or equal thresholds use IconFullBelowPx as a single hard cutoff:
// IconAlpha = ApparentDiameterPx < IconFullBelowPx ? 1 : 0 (SDD 3, Appendix G clarifications and addendum). Inputs
// use a unit focal length so every apparent diameter is exact and the boundary values are pinned exactly
bool FSOLMapBodyLodInvertedThresholdsTest::RunTest(const FString& /*parameters*/)
{
    // Inverted: MeshFullAbovePx = 4, IconFullBelowPx = 32 (the defaults swapped)
    const FSOLBodyLodParams invertedParams = MapLodUnitFocalParams(32.0, 4.0);
    struct FCutoffCase
    {
        double SizePx;
        double ExpectedAlpha;
    };
    const FCutoffCase invertedCases[] = {
        { 1.0, 1.0 },    // below both thresholds
        { 4.0, 1.0 },    // at MeshFullAbovePx, still below the 32 px cutoff
        { 16.0, 1.0 },   // between the swapped thresholds, below the cutoff: icon only (no interpolation)
        { 32.0, 0.0 },   // exactly at the cutoff: strict '<' makes it mesh only
        { 64.0, 0.0 },   // above the cutoff: mesh only
    };
    for (const FCutoffCase& cutoffCase : invertedCases)
    {
        // Radius r at 1 m with focal 1 px projects to exactly 2r px
        const FSOLBodyLodResult result = SOLMapBodyLod::Evaluate(0.5 * cutoffCase.SizePx, 1.0, invertedParams);
        TestEqual(*FString::Printf(TEXT("Inverted: apparent size is exactly %g px"), cutoffCase.SizePx),
            result.ApparentDiameterPx, cutoffCase.SizePx, 0.0);
        TestEqual(*FString::Printf(TEXT("Inverted: IconAlpha is exactly %g at %g px"), cutoffCase.ExpectedAlpha,
            cutoffCase.SizePx), result.IconAlpha, cutoffCase.ExpectedAlpha, 0.0);
    }

    // Equal thresholds (zero-width band): no division by zero; exactly at the shared value IconAlpha is 0 (addendum)
    const FSOLBodyLodParams equalParams = MapLodUnitFocalParams(10.0, 10.0);
    const FCutoffCase equalCases[] = {
        { 5.0, 1.0 },    // below the band: icon only
        { 10.0, 0.0 },   // exactly at IconFullBelowPx == MeshFullAbovePx: mesh only, not 1.0
        { 20.0, 0.0 },   // above the band: mesh only
    };
    for (const FCutoffCase& cutoffCase : equalCases)
    {
        const FSOLBodyLodResult result = SOLMapBodyLod::Evaluate(0.5 * cutoffCase.SizePx, 1.0, equalParams);
        TestEqual(*FString::Printf(TEXT("Equal thresholds: apparent size is exactly %g px"), cutoffCase.SizePx),
            result.ApparentDiameterPx, cutoffCase.SizePx, 0.0);
        TestEqual(*FString::Printf(TEXT("Equal thresholds: IconAlpha is exactly %g at %g px"),
            cutoffCase.ExpectedAlpha, cutoffCase.SizePx), result.IconAlpha, cutoffCase.ExpectedAlpha, 0.0);
    }

    // Degenerate diameter (0) with equal thresholds is still finite and icon only
    const FSOLBodyLodResult degenerate = SOLMapBodyLod::Evaluate(0.0, 1.0, equalParams);
    TestTrue(TEXT("Equal thresholds: degenerate IconAlpha is in [0,1]"), MapLodIsUnitInterval(degenerate.IconAlpha));
    TestEqual(TEXT("Equal thresholds: degenerate IconAlpha is exactly 1"), degenerate.IconAlpha, 1.0, 0.0);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
