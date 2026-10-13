/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

/**
 * Scene exposure math (SDD 13 decision 2, Amendment 1): the camera's exposure is set by ASOLBodyVisuals from what is
 * known to be in view, not measured from the frame. The engine's eye adaptation is not used: the render-origin rebase
 * resets the previous view every frame at speed (bPrevTransformsReset), which makes it snap to its target instead of
 * adapting. Each frame the view's mean radiance (emissive at the dark-sky exposure, EV100 0) is estimated from every
 * body's disc in view (its share of the view times its mean radiance seen at its phase angle), turned into a target
 * EV100 clamped to [min, max] (min = the dark-sky exposure, which equals the old fixed exposure), and the exposure
 * follows that target with a frame-rate independent exponential. Pure: no engine state; tested in
 * SOLTest.SceneExposure.*.
 */
namespace SOLSceneExposure
{
    // Returns the solid angle (sr) of a rectilinear view with these half-angle tangents: 4 asin(sin a sin b), where
    // a and b are the horizontal and vertical half angles; 0 if either tangent is not positive
    double ViewSolidAngleSr(double tanHalfFovX, double tanHalfFovY);

    // Returns the solid angle (sr) of a disc of this angular radius (rad, clamped to [0, PI / 2]): 2 PI (1 - cos r)
    double DiscSolidAngleSr(double angularRadiusRad);

    // Returns the share (0..1) of a view of this solid angle covered by a disc of this angular radius whose center is
    // offAxisRad from the view axis. The view is treated as the cone of the same solid angle; the disc counts in
    // proportion to how much of its diameter overlaps that cone, so it fades out as it leaves the view
    double DiscViewCoverage(double angularRadiusRad, double offAxisRad, double viewSolidAngleSr);

    // Returns the mean of saturate(N . L) over the visible disc of a Lambertian sphere at this phase angle (rad, the
    // angle Sun-body-camera): (2 / 3) (sin a + (PI - a) cos a) / PI; 2/3 at full phase, 0 at new phase
    double LambertDiscMeanLight(double phaseAngleRad);

    // Returns a body's mean radiance seen at this phase angle: its own emission plus its albedo lit by the Sun,
    // emissiveLuminance + albedoLuminance * sunIlluminanceLux / PI * LambertDiscMeanLight (as M_SOLBody shades it,
    // ambient term left out)
    double BodyMeanRadiance(double emissiveLuminance, double albedoLuminance, double sunIlluminanceLux,
        double phaseAngleRad);

    // Returns the EV100 the exposure aims for: minEv100 while the view's mean radiance is at or below keyRadiance,
    // minEv100 + log2(mean / key) above it, clamped to [minEv100, maxEv100]
    double TargetEv100(double meanRadiance, double keyRadiance, double minEv100, double maxEv100);

    // Returns the exposure after dt seconds of exponential approach to the target with this time constant (s):
    // target + (current - target) * exp(-dt / tau); frame-rate independent, the target itself for tau <= 0
    double SmoothEv100(double currentEv100, double targetEv100, double deltaSeconds, double timeConstantSeconds);

    // Returns the manual exposure bias (EV) that applies this EV100 relative to the dark-sky exposure: minEv100 -
    // ev100, so 0 in dark sky (the old fixed exposure) and negative as the scene brightens (pixel values times
    // 2^bias)
    double ExposureBias(double ev100, double minEv100);
}
