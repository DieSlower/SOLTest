/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "StarField/SOLStarBrightness.h"

namespace
{
    constexpr double POGSON_EXPONENT = -0.4; // log10 flux per magnitude
}

//////////////////////////////////////////////////////////////////////////
// Returns the flux of a star of this magnitude relative to one of referenceMagnitude (Pogson: 10^(-0.4 dV))
double SOLStarBrightness::MagnitudeToRelativeFlux(const double magnitude, const double referenceMagnitude)
{
    return FMath::Pow(10.0, POGSON_EXPONENT * (magnitude - referenceMagnitude));
}

//////////////////////////////////////////////////////////////////////////
// Returns the multiplier on baked flux that makes a star of whiteMagnitude scale to exactly 1.0
double SOLStarBrightness::BrightnessScaleForWhiteMagnitude(const double whiteMagnitude,
    const double fluxReferenceMagnitude)
{
    return 1.0 / MagnitudeToRelativeFlux(whiteMagnitude, fluxReferenceMagnitude);
}

//////////////////////////////////////////////////////////////////////////
// Returns the angular size (rad) of one output pixel at the view center: 2 * tanHalfFovX / outputWidthPx
double SOLStarBrightness::PixelAngularSizeRad(const double tanHalfFovX, const double outputWidthPx)
{
    return outputWidthPx > 0.0 ? 2.0 * tanHalfFovX / outputWidthPx : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the angular diameter (rad) of a star kernel drawn diameterPx output pixels across at this FOV and width
double SOLStarBrightness::KernelAngularDiameterRad(const double diameterPx, const double tanHalfFovX,
    const double outputWidthPx)
{
    return diameterPx * PixelAngularSizeRad(tanHalfFovX, outputWidthPx);
}

//////////////////////////////////////////////////////////////////////////
// Returns the pixel area (pixel sum at peak 1) of the kernel saturate(1 - (d / R)^2)^2, 2R = diameterPx: PI R^2 / 3
double SOLStarBrightness::PointKernelAreaPx(const double diameterPx)
{
    const double radiusPx = 0.5 * diameterPx;
    return UE_DOUBLE_PI * radiusPx * radiusPx / 3.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns a point star's peak pixel value per unit data flux: brightnessScale / PointKernelAreaPx(diameterPx)
double SOLStarBrightness::PointPeakPerFlux(const double brightnessScale, const double diameterPx)
{
    const double areaPx = PointKernelAreaPx(diameterPx);
    return areaPx > 0.0 ? brightnessScale / areaPx : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the factor from a cube texel value to pixel flux for a pixel of this angular size: pixel^2 / texel sr
double SOLStarBrightness::CubeTexelToPixelFlux(const double pixelAngularSizeRad, const double cubeTexelSolidAngleSr)
{
    return cubeTexelSolidAngleSr > 0.0 ? pixelAngularSizeRad * pixelAngularSizeRad / cubeTexelSolidAngleSr : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the point-star share of the cube: 0 at pixel >= one texel, 1 at pixel <= 0.8 texel, linear in texel/pixel
double SOLStarBrightness::CubePointBlend(const double texelAngularSizeRad, const double pixelAngularSizeRad)
{
    constexpr double FULL_POINT_RAMP = 0.25; // texel / pixel - 1 at which the cube is all point stars
    if (pixelAngularSizeRad <= 0.0)
    {
        return 1.0;
    }
    return FMath::Clamp((texelAngularSizeRad / pixelAngularSizeRad - 1.0) / FULL_POINT_RAMP, 0.0, 1.0);
}
