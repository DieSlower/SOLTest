/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLSceneExposure.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the scene exposure math (SDD 13 decision 2, Amendment 1). Expected values are hand-computed from the
// definitions in the SDD and the header, not from the implementation
namespace
{
    constexpr double EXPOSURE_TOL = 1.0e-9;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSceneExposureSolidAngleTest, "SOLTest.SceneExposure.SolidAngles",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A 90 deg x 58.7 deg (16:9) view is 4 asin(sin 45 deg sin 29.36 deg) = 1.41606 sr; an ever wider view tends to a
// hemisphere (2 PI); a disc of 0.1 rad is 2 PI (1 - cos 0.1) = 0.0313898 sr, a disc of PI / 2 or more a hemisphere
bool FSOLSceneExposureSolidAngleTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("90 deg 16:9 view"), SOLSceneExposure::ViewSolidAngleSr(1.0, 1.0 / (16.0 / 9.0)),
        1.4160618427686762, EXPOSURE_TOL);
    TestEqual(TEXT("Very wide view: hemisphere"), SOLSceneExposure::ViewSolidAngleSr(1.0e9, 1.0e9),
        2.0 * UE_DOUBLE_PI, 1e-6);
    TestEqual(TEXT("Degenerate view: 0"), SOLSceneExposure::ViewSolidAngleSr(0.0, 1.0), 0.0);
    TestEqual(TEXT("Negative tangent: 0"), SOLSceneExposure::ViewSolidAngleSr(1.0, -1.0), 0.0);
    TestEqual(TEXT("0.1 rad disc"), SOLSceneExposure::DiscSolidAngleSr(0.1), 0.031389755322205774, EXPOSURE_TOL);
    TestEqual(TEXT("PI / 2 disc: hemisphere"), SOLSceneExposure::DiscSolidAngleSr(0.5 * UE_DOUBLE_PI),
        2.0 * UE_DOUBLE_PI, EXPOSURE_TOL);
    TestEqual(TEXT("Disc past PI / 2 clamps"), SOLSceneExposure::DiscSolidAngleSr(3.0), 2.0 * UE_DOUBLE_PI,
        EXPOSURE_TOL);
    TestEqual(TEXT("Negative radius: 0"), SOLSceneExposure::DiscSolidAngleSr(-0.1), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSceneExposureCoverageTest, "SOLTest.SceneExposure.DiscViewCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A small centered disc covers its own solid angle over the view's; a disc larger than the view covers all of it; a
// disc far outside the view covers nothing; a disc centered on the view-cone edge counts half
bool FSOLSceneExposureCoverageTest::RunTest(const FString& /*parameters*/)
{
    constexpr double viewSr = 1.4160618427686762;
    TestEqual(TEXT("Centered 0.1 rad disc"), SOLSceneExposure::DiscViewCoverage(0.1, 0.0, viewSr),
        0.031389755322205774 / viewSr, EXPOSURE_TOL);
    TestEqual(TEXT("Disc bigger than the view: 1"), SOLSceneExposure::DiscViewCoverage(1.2, 0.0, viewSr), 1.0,
        EXPOSURE_TOL);
    TestEqual(TEXT("Disc behind the camera: 0"), SOLSceneExposure::DiscViewCoverage(0.1, UE_DOUBLE_PI, viewSr), 0.0);

    // Cone of the view's solid angle: acos(1 - sr / 2 PI); a disc centered on its edge is half inside
    const double viewConeRad = FMath::Acos(1.0 - viewSr / (2.0 * UE_DOUBLE_PI));
    TestEqual(TEXT("Disc on the view edge: half"), SOLSceneExposure::DiscViewCoverage(0.1, viewConeRad, viewSr),
        0.5 * 0.031389755322205774 / viewSr, EXPOSURE_TOL);
    TestEqual(TEXT("Zero radius: 0"), SOLSceneExposure::DiscViewCoverage(0.0, 0.0, viewSr), 0.0);
    TestEqual(TEXT("Degenerate view: 0"), SOLSceneExposure::DiscViewCoverage(0.1, 0.0, 0.0), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSceneExposureRadianceTest, "SOLTest.SceneExposure.BodyRadiance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Lambert disc mean: 2/3 at full phase, 2 / (3 PI) = 0.212207 at quarter phase, 0 at new phase. Radiance: albedo 0.3
// under 3 lux at full phase is 0.3 * 3 / PI * 2/3 = 0.190986; an emissive body adds its own emission
bool FSOLSceneExposureRadianceTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Full phase: 2/3"), SOLSceneExposure::LambertDiscMeanLight(0.0), 2.0 / 3.0, EXPOSURE_TOL);
    TestEqual(TEXT("Quarter phase: 2 / (3 PI)"), SOLSceneExposure::LambertDiscMeanLight(0.5 * UE_DOUBLE_PI),
        0.21220659078919377, EXPOSURE_TOL);
    TestEqual(TEXT("New phase: 0"), SOLSceneExposure::LambertDiscMeanLight(UE_DOUBLE_PI), 0.0, EXPOSURE_TOL);
    TestEqual(TEXT("Past new phase clamps: 0"), SOLSceneExposure::LambertDiscMeanLight(4.0), 0.0, EXPOSURE_TOL);
    TestEqual(TEXT("Albedo 0.3, 3 lux, full phase"), SOLSceneExposure::BodyMeanRadiance(0.0, 0.3, 3.0, 0.0),
        0.3 * 3.0 / UE_DOUBLE_PI * 2.0 / 3.0, EXPOSURE_TOL);
    TestEqual(TEXT("Emissive body unlit side"), SOLSceneExposure::BodyMeanRadiance(49.0, 0.5, 3.0, UE_DOUBLE_PI),
        49.0, EXPOSURE_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSceneExposureTargetTest, "SOLTest.SceneExposure.TargetEv",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Dark sky (mean at or below the key) stays at the minimum; 4x the key is 2 EV up; far above clamps to the maximum;
// the bias is minimum - EV (0 in dark sky, -2 at the cap)
bool FSOLSceneExposureTargetTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Black: min"), SOLSceneExposure::TargetEv100(0.0, 0.05, 0.0, 2.0), 0.0);
    TestEqual(TEXT("At the key: min"), SOLSceneExposure::TargetEv100(0.05, 0.05, 0.0, 2.0), 0.0);
    TestEqual(TEXT("2x the key: 1 EV"), SOLSceneExposure::TargetEv100(0.1, 0.05, 0.0, 2.0), 1.0, EXPOSURE_TOL);
    TestEqual(TEXT("4x the key, max 3: 2 EV"), SOLSceneExposure::TargetEv100(0.2, 0.05, 0.0, 3.0), 2.0,
        EXPOSURE_TOL);
    TestEqual(TEXT("1000x the key: clamps at max"), SOLSceneExposure::TargetEv100(50.0, 0.05, 0.0, 2.0), 2.0,
        EXPOSURE_TOL);
    TestEqual(TEXT("Offset range"), SOLSceneExposure::TargetEv100(0.1, 0.05, 1.0, 4.0), 2.0, EXPOSURE_TOL);
    TestEqual(TEXT("Zero key: min"), SOLSceneExposure::TargetEv100(1.0, 0.0, 0.0, 2.0), 0.0);
    TestEqual(TEXT("Bias in dark sky: 0"), SOLSceneExposure::ExposureBias(0.0, 0.0), 0.0);
    TestEqual(TEXT("Bias at 2 EV: -2"), SOLSceneExposure::ExposureBias(2.0, 0.0), -2.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLSceneExposureSmoothingTest, "SOLTest.SceneExposure.Smoothing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// From 1 toward 3 with tau 0.4 s: after 0.5 s 3 - 2 e^-1.25 = 2.42699; two 0.25 s steps equal one 0.5 s step (frame-
// rate independent); dt 0 keeps the value; tau 0 jumps to the target; a negative dt does not move away from it
bool FSOLSceneExposureSmoothingTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("0.5 s at tau 0.4"), SOLSceneExposure::SmoothEv100(1.0, 3.0, 0.5, 0.4),
        3.0 - 2.0 * FMath::Exp(-1.25), EXPOSURE_TOL);
    const double twoSteps = SOLSceneExposure::SmoothEv100(SOLSceneExposure::SmoothEv100(1.0, 3.0, 0.25, 0.4), 3.0,
        0.25, 0.4);
    TestEqual(TEXT("Two half steps = one step"), twoSteps, SOLSceneExposure::SmoothEv100(1.0, 3.0, 0.5, 0.4),
        EXPOSURE_TOL);
    TestEqual(TEXT("No time: unchanged"), SOLSceneExposure::SmoothEv100(1.0, 3.0, 0.0, 0.4), 1.0, EXPOSURE_TOL);
    TestEqual(TEXT("No time constant: target"), SOLSceneExposure::SmoothEv100(1.0, 3.0, 0.016, 0.0), 3.0);
    TestEqual(TEXT("Negative dt: unchanged"), SOLSceneExposure::SmoothEv100(1.0, 3.0, -1.0, 0.4), 1.0, EXPOSURE_TOL);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
