/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Per-body pick radius for the jump map (SDD 3 Appendix G): keeps icon-sized bodies clickable
namespace SOLMapPickRadius
{
    // World-space pick radius (meters) of a body for SOLMapPicking::PickBodyUnderRay: the larger of the body's real
    // radius and the radius that spans minPickPx on screen at distanceM, using the same projection as
    // SOLMapBodyLod::ComputeApparentDiameterPx (viewport height and vertical FOV). Invalid inputs (distance, pixel
    // size, viewport height or FOV not positive, FOV >= PI) fall back to the body's real radius (never negative).
    SOLTEST_API double ComputeMapPickRadiusM(double bodyRadiusM, double distanceM, double minPickPx,
        double viewportHeightPx, double verticalFovRad);
}
