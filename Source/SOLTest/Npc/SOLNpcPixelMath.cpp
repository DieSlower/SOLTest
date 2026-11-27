/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcPixelMath.h"

#include "SOLConstants.h"

// The double reference of the sub-pixel ship rule (SDD 54 sections 4.1-4.6). Shaders/Private/SOLNpcPoint.ush and its
// float copy Npc/SOLNpcPointMirror.h follow the same rules; every guard here is an explicit comparison or IsFinite,
// never a clamp, so a NaN never turns into a drawn ship.

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns true for a value that is greater than 0 and finite (false for NaN and infinities)
    bool NpcPixelIsPositiveFinite(const double value)
    {
        return value > 0.0 && FMath::IsFinite(value);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship's true size in reference px, focal x diameter / max(distance, 1); 0 for any bad input
double SOLNpcPixel::ShipSizePx(const double diameterM, const double distanceM, const double focalRefPx)
{
    if (!NpcPixelIsPositiveFinite(diameterM) || !NpcPixelIsPositiveFinite(focalRefPx) || !(distanceM >= 0.0))
    {
        return 0.0;
    }

    // An infinite distance gives 0 through the division
    return focalRefPx * diameterM / FMath::Max(distanceM, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the dot brightness factor: 0 at and below hidePx (and NaN), linear to 1 at fullPx, 1 at and above it
double SOLNpcPixel::DotAlpha(const double sizePx, const double hidePx, const double fullPx)
{
    if (!FMath::IsFinite(hidePx) || !FMath::IsFinite(fullPx) || !(sizePx > hidePx))
    {
        return 0.0;
    }
    if (sizePx >= fullPx)
    {
        return 1.0;
    }

    // hidePx < sizePx < fullPx here, so the ratio lies in (0, 1)
    return (sizePx - hidePx) / (fullPx - hidePx);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the dot is drawn at all, DotAlpha > 0
bool SOLNpcPixel::IsDotDrawn(const double sizePx, const double hidePx, const double fullPx)
{
    return DotAlpha(sizePx, hidePx, fullPx) > 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the distance at which a ship of diameterM is exactly thresholdPx across; 0 (no ship qualifies) for bad input
double SOLNpcPixel::PixelRadiusM(const double diameterM, const double focalRefPx, const double thresholdPx)
{
    if (!NpcPixelIsPositiveFinite(diameterM) || !NpcPixelIsPositiveFinite(focalRefPx)
        || !NpcPixelIsPositiveFinite(thresholdPx))
    {
        return 0.0;
    }
    return diameterM * focalRefPx / thresholdPx;
}

//////////////////////////////////////////////////////////////////////////
// Returns the bounding sphere diameter of an axis-aligned box (its diagonal); 0 for a negative or non-finite size
double SOLNpcPixel::BoundingDiameterM(const FVector3d& boxSizeM)
{
    for (int32 axis = 0; axis < 3; ++axis)
    {
        const double component = boxSizeM[axis];
        if (!(component >= 0.0) || !FMath::IsFinite(component))
        {
            return 0.0;
        }
    }
    return FMath::Sqrt(boxSizeM.X * boxSizeM.X + boxSizeM.Y * boxSizeM.Y + boxSizeM.Z * boxSizeM.Z);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a ship at or beyond nearestDistanceM can be drawn (more than hidePx across, the IsDotDrawn rule);
// true on any bad input so the shader decides
bool SOLNpcPixel::FarLayerCanDraw(const double diameterM, const double focalRefPx, const double nearestDistanceM,
    const double hidePx)
{
    if (!NpcPixelIsPositiveFinite(diameterM) || !NpcPixelIsPositiveFinite(focalRefPx) || !(nearestDistanceM > 0.0)
        || !FMath::IsFinite(hidePx))
    {
        return true;
    }

    // The nearest ship is the largest; strictly above the hide size, as DotAlpha (exactly hidePx is not drawn)
    return ShipSizePx(diameterM, nearestDistanceM, focalRefPx) > hidePx;
}

//////////////////////////////////////////////////////////////////////////
// Returns the distance at which a ship is thresholdPx x scale across (scale read as at least 1), capped at capM
double SOLNpcPixel::TierRadiusM(const double diameterM, const double focalRefPx, const double thresholdPx,
    const double qualityScale, const double capM)
{
    if (!(capM >= 0.0))
    {
        return 0.0;
    }

    // A quality scale only ever raises the thresholds (Epic and Cinematic are 1); a bad threshold gives 0 through
    // PixelRadiusM
    const double scale = (FMath::IsFinite(qualityScale) && qualityScale > 1.0) ? qualityScale : 1.0;
    return FMath::Min(PixelRadiusM(diameterM, focalRefPx, thresholdPx * scale), capM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the farther of the hull radius and the hidePx distance (a bad hull radius reads as 0)
double SOLNpcPixel::HandOverStartM(const double hullRadiusM, const double diameterM, const double focalRefPx,
    const double hidePx)
{
    const double hullM = (hullRadiusM > 0.0 && FMath::IsFinite(hullRadiusM)) ? hullRadiusM : 0.0;
    return FMath::Max(hullM, PixelRadiusM(diameterM, focalRefPx, hidePx));
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the far layer is shown: on the jump map always, else when its nearest ship can pass the cutoff
bool SOLNpcPixel::FarLayerShown(const bool bMapView, const double focalRefPx, const double midRadiusM)
{
    return bMapView || FarLayerCanDraw(SOL::NPC_HULL_BOUNDING_DIAMETER_M, focalRefPx, midRadiusM,
        SOL::NPC_SHIP_HIDE_PX);
}

//////////////////////////////////////////////////////////////////////////
// Returns the point material's NpcParams2 from the constants
FLinearColor SOLNpcPixel::PointParams2()
{
    return FLinearColor(static_cast<float>(SOL::NPC_POINT_RASTER_MIN_DEVICE_PX),
        static_cast<float>(SOL::NPC_HULL_BOUNDING_DIAMETER_M * 0.5),
        static_cast<float>(SOL::NPC_POINT_BRIGHTNESS_REF_M), static_cast<float>(SOL::NPC_POINT_BRIGHTNESS_MIN));
}

//////////////////////////////////////////////////////////////////////////
// Returns the point material's NpcParams5 from the constants and the map flag
FLinearColor SOLNpcPixel::PointParams5(const bool bMapView)
{
    return FLinearColor(static_cast<float>(SOL::NPC_SHIP_HIDE_PX), static_cast<float>(SOL::NPC_SHIP_DOT_FULL_PX),
        static_cast<float>(SOL::NPC_POINT_REFERENCE_HEIGHT_PX), bMapView ? 1.0f : 0.0f);
}

//////////////////////////////////////////////////////////////////////////
// Returns the point material's NpcParams6 (the dot cap) from the constants
FLinearColor SOLNpcPixel::PointParams6()
{
    return FLinearColor(static_cast<float>(SOL::NPC_POINT_DOT_MAX_PX), 0.0f, 0.0f, 0.0f);
}
