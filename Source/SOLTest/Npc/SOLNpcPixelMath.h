/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Pure math of the NPC sub-pixel ship rule (SDD 54 sections 4.1-4.6): a ship's true size on screen in 1080p reference
// px, the 1-2 px dot fade, the pixel radii of the render tiers and the far layer skip. Double precision, no UObjects,
// no world access, no global state; the contract is Docs/Plans/54-hide-subpixel-ships-contract.md.
namespace SOLNpcPixel
{
    // True size on screen in reference px, focalRefPx x diameterM / max(distanceM, 1)
    SOLTEST_API double ShipSizePx(double diameterM, double distanceM, double focalRefPx);

    // Dot brightness factor: 0 at and below hidePx (and for NaN), linear to 1 at fullPx, 1 above
    SOLTEST_API double DotAlpha(double sizePx, double hidePx, double fullPx);

    // Whether the dot is drawn at all (DotAlpha > 0)
    SOLTEST_API bool IsDotDrawn(double sizePx, double hidePx, double fullPx);

    // Distance at which a ship of diameterM is exactly thresholdPx across, diameterM x focalRefPx / thresholdPx
    SOLTEST_API double PixelRadiusM(double diameterM, double focalRefPx, double thresholdPx);

    // Bounding sphere diameter of an axis-aligned box of the given full size (its diagonal)
    SOLTEST_API double BoundingDiameterM(const FVector3d& boxSizeM);

    // Whether any ship at or beyond nearestDistanceM can be more than hidePx across, the IsDotDrawn rule (the far layer
    // skip); true on any bad input
    SOLTEST_API bool FarLayerCanDraw(double diameterM, double focalRefPx, double nearestDistanceM, double hidePx);

    // Nominal render tier radius (SDD 54 4.5, Q12): the distance at which a ship of diameterM is thresholdPx x
    // qualityScale across (a NaN, infinite or below-1 scale reads as 1), capped at capM (the Mid radius; a NaN or
    // negative cap gives 0). Distance is only the derived figure; the budgets shrink it afterwards
    SOLTEST_API double TierRadiusM(double diameterM, double focalRefPx, double thresholdPx, double qualityScale,
        double capM);

    // Distance where the lane hand-over blend reaches the double position (SDD 54 4.5): the hull radius or the 1 px
    // distance (hidePx, never quality-scaled), whichever is farther, so every drawn dot sits at its double position
    SOLTEST_API double HandOverStartM(double hullRadiusM, double diameterM, double focalRefPx, double hidePx);

    // Whether the GPU far layer component is shown this frame (SDD 54 4.6, Q9, Q10): always while the jump map keeps
    // today's dots, else exactly FarLayerCanDraw of the hull bounding diameter at the Mid radius and the 1 px cutoff
    SOLTEST_API bool FarLayerShown(bool bMapView, double focalRefPx, double midRadiusM);

    // NpcParams2 of M_SOLNpcPoints (SDD 54 4.4): (raster floor in device px, half the bounding diameter m, distance
    // brightness reference m, distance brightness floor)
    SOLTEST_API FLinearColor PointParams2();

    // NpcParams5 of M_SOLNpcPoints (SDD 54 4.4, Q9): (hide px, full px, reference height px, map flag 1 / 0)
    SOLTEST_API FLinearColor PointParams5(bool bMapView);

    // NpcParams6 of M_SOLNpcPoints (SDD 54 4.3, Q13): (dot cap in reference px, 0, 0, 0)
    SOLTEST_API FLinearColor PointParams6();
}
