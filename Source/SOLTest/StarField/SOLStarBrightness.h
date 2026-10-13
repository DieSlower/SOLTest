/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

/**
 * Star-field brightness and screen-size math (SDD 13): the magnitude-to-display mapping, and the pixel-sized,
 * flux-conserving point-star kernel shared by the bright-star sprites (M_SOLStarSprite) and the faint-star cube's
 * point reconstruction (M_SOLStarSky). The materials evaluate the same formulas on the GPU; these functions are their
 * reference definition and are unit-tested in SOLTest.StarBrightness.*. Pure: no engine state.
 *
 * Units. Data flux is relative to the bake's reference magnitude (1.0 at FluxReferenceMagnitude). On screen a star's
 * "pixel flux" is the sum of its pixels' emissive values (exposure 1, EV100 0, the dark-sky exposure, before
 * tonemapping); 1.0 is one fully white pixel. A star's pixel flux is its data flux times the brightness scale at every
 * FOV, resolution and screen percentage, and it is always drawn as the same kernel saturate(1 - (d / R)^2)^2 of
 * diameter 2R output pixels, so its peak pixel value never changes either: zooming changes neither size nor
 * brightness of a point star, as with the naked eye (SDD 13 Amendment 1).
 */
namespace SOLStarBrightness
{
    // Returns the flux of a star of this magnitude relative to one of referenceMagnitude (Pogson: 10^(-0.4 dV))
    double MagnitudeToRelativeFlux(double magnitude, double referenceMagnitude);

    // Returns the multiplier on baked flux (relative to fluxReferenceMagnitude) that makes a star of whiteMagnitude
    // sum to a pixel flux of exactly 1.0 (one display-white pixel's worth): 10^(-0.4 (fluxReferenceMagnitude -
    // whiteMagnitude))
    double BrightnessScaleForWhiteMagnitude(double whiteMagnitude, double fluxReferenceMagnitude);

    // Returns the angular size (rad) of one output pixel at the view center for a horizontal half-FOV tangent and the
    // output (post-upscale) width in pixels: 2 * tanHalfFovX / outputWidthPx; 0 for a non-positive width
    double PixelAngularSizeRad(double tanHalfFovX, double outputWidthPx);

    // Returns the angular diameter (rad) of a star kernel drawn diameterPx output pixels across at this FOV and width
    double KernelAngularDiameterRad(double diameterPx, double tanHalfFovX, double outputWidthPx);

    // Returns the area in pixels of the unit-peak kernel saturate(1 - (d / R)^2)^2 of diameter 2R = diameterPx, i.e.
    // its pixel sum at peak 1: PI R^2 / 3
    double PointKernelAreaPx(double diameterPx);

    // Returns a point star's peak pixel value per unit data flux: brightnessScale / PointKernelAreaPx(diameterPx), so
    // its pixel flux is exactly flux * brightnessScale whatever the FOV or resolution
    double PointPeakPerFlux(double brightnessScale, double diameterPx);

    // Returns the factor from a cube texel value (flux per face-center texel solid angle) to pixel flux for a pixel of
    // this angular size: pixelAngularSizeRad^2 / cubeTexelSolidAngleSr (filtered cube and the point mode's glow)
    double CubeTexelToPixelFlux(double pixelAngularSizeRad, double cubeTexelSolidAngleSr);

    // Returns how much of the faint-star cube shows as reconstructed point stars rather than filtered texels at a
    // pixel: 0 while the pixel is at least one texel across (texels minified or 1:1), 1 once it is 0.8 texel or
    // smaller (texels magnified: a narrow FOV or a high resolution), linear in texel / pixel between:
    // saturate((t / p - 1) / 0.25); 1 for a non-positive pixel size, 0 for a non-positive texel size.
    // From 0.8 texel down, the point kernel's radius (1.25 px at 2.5 px) stays within the one texel the
    // reconstruction searches around the pixel
    double CubePointBlend(double texelAngularSizeRad, double pixelAngularSizeRad);
}
