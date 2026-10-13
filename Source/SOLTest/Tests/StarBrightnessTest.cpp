/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "StarField/SOLStarBrightness.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the star-field brightness mapping and the pixel-sized point kernel (SDD 13 Amendment 1). Expected values
// are hand-computed from the definitions in the SDD, not from the implementation
namespace
{
    constexpr double STAR_REL_TOL = 1.0e-9;

    // Output widths and horizontal FOVs (deg) the FOV-invariance checks sweep: the chase camera's 90-110, the warp
    // peak 135, the map/menu 60 and narrow zooms down to 5
    constexpr double STAR_WIDTHS_PX[] = { 1280.0, 1920.0, 3840.0 };
    constexpr double STAR_FOVS_DEG[] = { 5.0, 20.0, 45.0, 60.0, 90.0, 110.0, 135.0 };

    //////////////////////////////////////////////////////////////////////////
    // Returns tan(fov / 2) for a horizontal FOV in degrees
    double TanHalfFov(const double fovDeg)
    {
        return FMath::Tan(FMath::DegreesToRadians(0.5 * fovDeg));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if a and b agree within a relative tolerance
    bool NearlyRel(const double a, const double b, const double relTol)
    {
        return FMath::Abs(a - b) <= relTol * FMath::Max(FMath::Abs(a), FMath::Abs(b));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarBrightnessPogsonTest, "SOLTest.StarBrightness.PogsonFlux",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Five magnitudes are a factor 100, one magnitude 10^0.4, equal magnitudes 1, and brighter (smaller) is more flux
bool FSOLStarBrightnessPogsonTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Same magnitude is flux 1"), SOLStarBrightness::MagnitudeToRelativeFlux(8.0, 8.0), 1.0, 1e-12);
    TestTrue(TEXT("Mag 3 is 100x mag 8"),
        NearlyRel(SOLStarBrightness::MagnitudeToRelativeFlux(3.0, 8.0), 100.0, STAR_REL_TOL));
    TestTrue(TEXT("Mag 13 is 1/100 of mag 8"),
        NearlyRel(SOLStarBrightness::MagnitudeToRelativeFlux(13.0, 8.0), 0.01, STAR_REL_TOL));
    TestTrue(TEXT("One magnitude is 10^0.4 = 2.5119"),
        NearlyRel(SOLStarBrightness::MagnitudeToRelativeFlux(0.0, 1.0), 2.51188643150958, STAR_REL_TOL));
    // Sirius (V = -1.46) against the mag-8 reference: 10^(0.4 * 9.46) = 6081.35
    TestTrue(TEXT("Sirius is ~6081x a mag-8 star"),
        NearlyRel(SOLStarBrightness::MagnitudeToRelativeFlux(-1.46, 8.0), 6081.35001278719, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarBrightnessWhiteScaleTest, "SOLTest.StarBrightness.WhiteMagnitudeScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The scale is 10^(-0.4 (reference - white)): 1 when the white magnitude is the reference (the SDD 5 mapping),
// 10^-2.8 for white 1 against reference 8, 10^-2.2 for the shipped white 2.5 against the bake's reference 8
bool FSOLStarBrightnessWhiteScaleTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("White = reference gives scale 1"),
        NearlyRel(SOLStarBrightness::BrightnessScaleForWhiteMagnitude(8.0, 8.0), 1.0, STAR_REL_TOL));
    TestTrue(TEXT("White mag 1, reference 8: 10^-2.8"),
        NearlyRel(SOLStarBrightness::BrightnessScaleForWhiteMagnitude(1.0, 8.0), 0.00158489319246, 1e-9));
    TestTrue(TEXT("White mag 2.5, reference 8: 10^-2.2"),
        NearlyRel(SOLStarBrightness::BrightnessScaleForWhiteMagnitude(2.5, 8.0), 0.00630957344480193, 1e-9));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarBrightnessCalibrationTest, "SOLTest.StarBrightness.Calibration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Pins the shipped look (SDD 13 Amendment 1, A1.2): white magnitude 2.5, bake reference 8, a 2.5 px kernel. A star's
// peak pixel (linear, dark-sky exposure) is 0.6112 * 10^(-0.4 (V - 2.5)): V 6.5 (naked-eye limit) 0.01535, V 8 (the
// sprite / cube split) 0.003856, V 2.0 0.9686 (saturation sets in just above V 2)
bool FSOLStarBrightnessCalibrationTest::RunTest(const FString& /*parameters*/)
{
    constexpr double whiteMagnitude = 2.5;
    constexpr double referenceMagnitude = 8.0;
    constexpr double diameterPx = 2.5;
    const double peakPerFlux = SOLStarBrightness::PointPeakPerFlux(
        SOLStarBrightness::BrightnessScaleForWhiteMagnitude(whiteMagnitude, referenceMagnitude), diameterPx);
    struct FPin
    {
        double Magnitude;
        double Peak;
    };
    for (const FPin& pin : { FPin{ 6.5, 0.015351519055112 }, FPin{ 8.0, 0.003856127241760 },
        FPin{ 2.0, 0.968615369675063 }, FPin{ 2.5, 0.611154981472878 } })
    {
        const double peak = peakPerFlux * SOLStarBrightness::MagnitudeToRelativeFlux(pin.Magnitude, referenceMagnitude);
        TestTrue(FString::Printf(TEXT("V %.1f peaks at %.6f"), pin.Magnitude, pin.Peak),
            NearlyRel(peak, pin.Peak, 1e-9));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarBrightnessPixelSizeTest, "SOLTest.StarBrightness.PixelAngularSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One center pixel subtends 2 tan(fov/2) / width: 90 deg over 2000 px is exactly 1e-3 rad; width <= 0 gives 0
bool FSOLStarBrightnessPixelSizeTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("90 deg over 2000 px is 1e-3 rad"),
        NearlyRel(SOLStarBrightness::PixelAngularSizeRad(1.0, 2000.0), 1.0e-3, STAR_REL_TOL));
    TestTrue(TEXT("Halving the width doubles the pixel"),
        NearlyRel(SOLStarBrightness::PixelAngularSizeRad(0.5, 1000.0), 1.0e-3, STAR_REL_TOL));
    TestEqual(TEXT("Zero width is 0"), SOLStarBrightness::PixelAngularSizeRad(1.0, 0.0), 0.0);
    TestEqual(TEXT("Negative width is 0"), SOLStarBrightness::PixelAngularSizeRad(1.0, -5.0), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarBrightnessFovInvariantSizeTest, "SOLTest.StarBrightness.FovInvariantPixelSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The kernel's angular diameter is diameterPx * 2 tan(fov / 2) / width, so it shrinks with zoom and stays the same
// number of pixels: 2.5 px at 90 deg over 1920 px is 2.5 * 2 / 1920 rad, at 20 deg 2.5 * 2 tan(10 deg) / 1920 rad,
// at 90 deg over 3840 px half the 1920 px value
bool FSOLStarBrightnessFovInvariantSizeTest::RunTest(const FString& /*parameters*/)
{
    constexpr double diameterPx = 2.5;
    TestTrue(TEXT("90 deg, 1920 px: 2.604e-3 rad"), NearlyRel(
        SOLStarBrightness::KernelAngularDiameterRad(diameterPx, 1.0, 1920.0), 2.5 * 2.0 / 1920.0, STAR_REL_TOL));
    TestTrue(TEXT("20 deg, 1920 px: 4.592e-4 rad"), NearlyRel(
        SOLStarBrightness::KernelAngularDiameterRad(diameterPx, TanHalfFov(20.0), 1920.0),
        2.5 * 2.0 * 0.176326980708465 / 1920.0, STAR_REL_TOL));
    TestTrue(TEXT("90 deg, 3840 px: half of 1920 px"), NearlyRel(
        SOLStarBrightness::KernelAngularDiameterRad(diameterPx, 1.0, 3840.0), 2.5 / 1920.0, STAR_REL_TOL));
    TestEqual(TEXT("Zero width: 0"), SOLStarBrightness::KernelAngularDiameterRad(diameterPx, 1.0, 0.0), 0.0);

    // Over the sweep the kernel always spans diameterPx center pixels
    for (const double width : STAR_WIDTHS_PX)
    {
        for (const double fovDeg : STAR_FOVS_DEG)
        {
            const double tanHalf = TanHalfFov(fovDeg);
            const double kernel = SOLStarBrightness::KernelAngularDiameterRad(diameterPx, tanHalf, width);
            TestTrue(FString::Printf(TEXT("FOV %.0f, width %.0f: kernel is %.1f px"), fovDeg, width, diameterPx),
                NearlyRel(kernel * width / (2.0 * tanHalf), diameterPx, STAR_REL_TOL));
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarBrightnessFluxConservingTest, "SOLTest.StarBrightness.FluxConservingKernel",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The kernel's pixel area is PI R^2 / 3 and a star's pixel flux (peak x area) is flux x scale, with no FOV or
// resolution input at all, so a star's size and brightness on screen never follow zoom; the cube's texel-to-pixel
// factor matches a texel-sized pixel to 1 and grows with the pixel's solid angle
bool FSOLStarBrightnessFluxConservingTest::RunTest(const FString& /*parameters*/)
{
    // Diameter 2 px: R = 1, PI / 3 = 1.0471975512 px
    TestTrue(TEXT("Kernel area is PI R^2 / 3"),
        NearlyRel(SOLStarBrightness::PointKernelAreaPx(2.0), 1.04719755119660, STAR_REL_TOL));
    // Diameter 2.5 px: area PI 1.5625 / 3 = 1.63625 px, so scale 0.004 peaks at 0.004 / 1.63625 = 0.00244462
    TestTrue(TEXT("2.5 px kernel area"),
        NearlyRel(SOLStarBrightness::PointKernelAreaPx(2.5), 1.636246173744684, STAR_REL_TOL));
    TestTrue(TEXT("Scale 0.004, 2.5 px: peak 0.00244462"),
        NearlyRel(SOLStarBrightness::PointPeakPerFlux(0.004, 2.5), 0.002444619925891512, STAR_REL_TOL));
    TestEqual(TEXT("Zero diameter has no peak (no division by zero)"),
        SOLStarBrightness::PointPeakPerFlux(1.0, 0.0), 0.0);

    // Texel sr (2 / 2048)^2: a pixel of 2 / 2048 rad is 1, twice that is 4, half is 1/4
    constexpr double texelRad = 2.0 / 2048.0;
    constexpr double texelSr = texelRad * texelRad;
    TestTrue(TEXT("Texel-sized pixel: 1"),
        NearlyRel(SOLStarBrightness::CubeTexelToPixelFlux(texelRad, texelSr), 1.0, STAR_REL_TOL));
    TestTrue(TEXT("Pixel two texels across: 4"),
        NearlyRel(SOLStarBrightness::CubeTexelToPixelFlux(2.0 * texelRad, texelSr), 4.0, STAR_REL_TOL));
    TestTrue(TEXT("Pixel half a texel: 1/4"),
        NearlyRel(SOLStarBrightness::CubeTexelToPixelFlux(0.5 * texelRad, texelSr), 0.25, STAR_REL_TOL));
    TestEqual(TEXT("Zero texel sr: 0"), SOLStarBrightness::CubeTexelToPixelFlux(texelRad, 0.0), 0.0);
    TestEqual(TEXT("Negative texel sr: 0"), SOLStarBrightness::CubeTexelToPixelFlux(texelRad, -texelSr), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLStarBrightnessCubeBlendTest, "SOLTest.StarBrightness.CubeModeBlend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Point blend: 0 at pixel >= texel, 1 at pixel <= 0.8 texel, 0.5 halfway in texel / pixel (pixel = texel / 1.125);
// a degenerate texel draws filtered, a degenerate pixel points
bool FSOLStarBrightnessCubeBlendTest::RunTest(const FString& /*parameters*/)
{
    constexpr double texel = 1.0e-3;
    TestEqual(TEXT("Pixel 2 texels: filtered"), SOLStarBrightness::CubePointBlend(texel, 2.0e-3), 0.0, 1e-12);
    TestEqual(TEXT("Pixel 1 texel: filtered"), SOLStarBrightness::CubePointBlend(texel, 1.0e-3), 0.0, 1e-12);
    TestEqual(TEXT("Pixel texel / 1.125: half"), SOLStarBrightness::CubePointBlend(texel, texel / 1.125), 0.5, 1e-9);
    TestEqual(TEXT("Pixel 0.8 texel: points"), SOLStarBrightness::CubePointBlend(texel, 0.8e-3), 1.0, 1e-9);
    TestEqual(TEXT("Pixel 1/10 texel: points"), SOLStarBrightness::CubePointBlend(texel, 1.0e-4), 1.0, 1e-12);
    TestEqual(TEXT("Zero texel: filtered"), SOLStarBrightness::CubePointBlend(0.0, 1.0e-3), 0.0, 1e-12);
    TestEqual(TEXT("Zero pixel: points"), SOLStarBrightness::CubePointBlend(texel, 0.0), 1.0, 1e-12);
    TestEqual(TEXT("Negative pixel: points"), SOLStarBrightness::CubePointBlend(texel, -1.0e-3), 1.0, 1e-12);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
