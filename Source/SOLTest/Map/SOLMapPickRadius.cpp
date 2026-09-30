/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapPickRadius.h"

#include "Map/SOLMapBodyLod.h"

//////////////////////////////////////////////////////////////////////////
// Returns the larger of the body's real radius and the world radius that spans minPickPx on screen at distanceM
double SOLMapPickRadius::ComputeMapPickRadiusM(const double bodyRadiusM, const double distanceM, const double minPickPx,
    const double viewportHeightPx, const double verticalFovRad)
{
    const double realRadiusM = FMath::Max(bodyRadiusM, 0.0);
    if (!(distanceM > 0.0) || !(minPickPx > 0.0) || !(viewportHeightPx > 0.0) || !(verticalFovRad > 0.0)
        || verticalFovRad >= UE_DOUBLE_PI)
    {
        return realRadiusM;
    }

    // The LOD projection is linear in the radius: half the apparent diameter of a 1 m radius sphere is the number of
    // screen pixels one meter spans at that distance
    FSOLBodyLodParams params;
    params.ViewportHeightPx = viewportHeightPx;
    params.VerticalFovRad = verticalFovRad;
    const double pixelsPerMeter = SOLMapBodyLod::ComputeApparentDiameterPx(1.0, distanceM, params) * 0.5;
    if (!(pixelsPerMeter > 0.0))
    {
        return realRadiusM;
    }
    return FMath::Max(realRadiusM, minPickPx / pixelsPerMeter);
}
