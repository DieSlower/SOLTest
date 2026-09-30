/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapBodyLod.h"

//////////////////////////////////////////////////////////////////////////
// Projects a sphere's radius at a camera distance to its on-screen diameter in pixels (0 for invalid inputs)
double SOLMapBodyLod::ComputeApparentDiameterPx(const double radiusM, const double distanceM,
    const FSOLBodyLodParams& params)
{
    if (distanceM <= 0.0 || radiusM <= 0.0)
    {
        return 0.0;
    }

    const double focalPx = params.ViewportHeightPx / (2.0 * FMath::Tan(params.VerticalFovRad * 0.5));
    return (2.0 * radiusM / distanceM) * focalPx;
}

//////////////////////////////////////////////////////////////////////////
// Computes the apparent diameter and the icon/mesh cross-fade weight for one body
FSOLBodyLodResult SOLMapBodyLod::Evaluate(const double radiusM, const double distanceM,
    const FSOLBodyLodParams& params)
{
    FSOLBodyLodResult result;
    result.ApparentDiameterPx = ComputeApparentDiameterPx(radiusM, distanceM, params);

    const double sizePx = result.ApparentDiameterPx;
    if (params.MeshFullAbovePx <= params.IconFullBelowPx)
    {
        // Inverted or zero-width band: IconFullBelowPx acts as a single hard cutoff
        result.IconAlpha = sizePx < params.IconFullBelowPx ? 1.0 : 0.0;
    }
    else if (sizePx <= params.IconFullBelowPx)
    {
        result.IconAlpha = 1.0;
    }
    else if (sizePx >= params.MeshFullAbovePx)
    {
        result.IconAlpha = 0.0;
    }
    else
    {
        const double spanPx = params.MeshFullAbovePx - params.IconFullBelowPx;
        result.IconAlpha = FMath::Clamp((params.MeshFullAbovePx - sizePx) / spanPx, 0.0, 1.0);
    }
    return result;
}
