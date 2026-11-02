/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLOverlayModel.h"

#include "CoreMinimal.h"

struct FSOLHudShapeBatch;

// Pure logic of the overlay icons (SDD 22 section 2.1): which category an object is, how bright its icon is (fading
// out as the real object grows on screen, darker far away while distance darkening is on), where an icon or its
// edge arrow goes on screen, which objects of a crowded category get an icon (a hard budget, nearest first), and the
// vector shapes of each category's icon in the hologram renderer's batch.
namespace SOLOverlayIcons
{
    // Icon categories, one icon shape each
    enum class EIconCategory : uint8
    {
        Star,
        Planet,
        Moon,
        Dwarf,
        Asteroid,
        Ring,
        Ship,
        Target,
        Count,
    };

    // Returns the Overlay Menu row that toggles a category's icons (IconStars ... IconTargets; Count for Count)
    SOLTEST_API SOLOverlay::EOverlay CategoryOverlay(EIconCategory category);

    // Returns a registry body's category: no parent is the Star; a child of body 0 (the Sun) is a Planet up to
    // lastPlanetIndex and a Dwarf after it; any other parent makes a Moon
    SOLTEST_API EIconCategory ClassifyBody(int32 bodyIndex, int32 parentIndex, int32 lastPlanetIndex);

    // Returns the icon's fade from the object's apparent radius on screen: 1 up to SOL::Overlay::ICON_FADE_START_PX,
    // 0 from ICON_FADE_END_PX, a smoothstep between; a negative or NaN radius gives 1
    SOLTEST_API double SizeFade(double apparentRadiusPx);

    // Returns the distance darkening factor: 1 when bDarkening is off; with it on, 1 up to SOL::Overlay::DARKEN_NEAR_M,
    // DARKEN_FLOOR from DARKEN_FAR_M, linear in log10(distance) between; a NaN or non-positive distance gives 1
    SOLTEST_API double DistanceDarkening(double distanceM, bool bDarkening);

    // Returns SizeFade(apparentRadiusPx) x DistanceDarkening(distanceM, bDarkening)
    SOLTEST_API double IconBrightness(double apparentRadiusPx, double distanceM, bool bDarkening);

    // Where an icon goes: on screen at its projection, or pinned to the screen edge with the direction toward it
    struct FEdgePin
    {
        FVector2D PositionPx = FVector2D::ZeroVector;   // Icon centre, or the edge point an arrow points out from
        FVector2D Direction = FVector2D::ZeroVector;    // Unit screen direction toward the object (zero when on screen)
        bool bOnScreen = false;
    };

    // Places a camera-local point (X forward, Y right, Z up; any unit) on a screen of viewSizePx with its centre at
    // centerPx and focal length focalPx. On screen when X > 0 and the projection (centre + focal x (Y/X, -Z/X)) lies
    // inside the screen shrunk by marginPx on every side. Otherwise pinned: Direction = normalize(Y, -Z) ((0, 1) when
    // that is zero) and PositionPx = centre + Direction x reach, where reach is the largest distance along Direction
    // that stays inside the shrunk screen (min of (centre.X - margin) / |Dir.X| and (centre.Y - margin) / |Dir.Y|)
    SOLTEST_API FEdgePin PinToScreen(const FVector& cameraLocal, const FVector2D& centerPx, double focalPx,
        const FVector2D& viewSizePx, double marginPx);

    // Fills outIndices (Reset first, its capacity kept) with at most cap indices into distancesM: selectedIndex first
    // when it is valid with a finite distance, then the others nearest first (ties: lower index first). NaN and
    // infinite distances are skipped. Allocates nothing when outIndices already has capacity for cap entries
    SOLTEST_API void SelectBudgeted(TConstArrayView<double> distancesM, int32 selectedIndex, int32 cap,
        TArray<int32>& outIndices);

    // Returns a category's icon colour in the hologram palette (SOL::HudPalette)
    SOLTEST_API FLinearColor CategoryColor(EIconCategory category);

    // Adds a category's icon (vector shapes, at most SOL::Overlay::MAX_SHAPES_PER_ICON) centred on centerPx with the
    // given radius to the batch; returns the shapes added (0 for Count, a non-positive radius or a full batch)
    SOLTEST_API int32 AddIconShapes(FSOLHudShapeBatch& batch, EIconCategory category, const FVector2f& centerPx,
        float radiusPx, const FLinearColor& color, float intensity, float padPx);

    // Adds an edge arrow (a chevron of two lines whose tip is lengthPx beyond basePx along the unit direction);
    // returns the shapes added (2, or 0 for a zero direction, a non-positive length or a full batch)
    SOLTEST_API int32 AddEdgeArrow(FSOLHudShapeBatch& batch, const FVector2f& basePx, const FVector2f& direction,
        float lengthPx, const FLinearColor& color, float intensity, float padPx);
}

// The hologram mouse pointer's geometry (SDD 22 section 2.5): an arrow outline whose tip is the hot spot
namespace SOLHologramCursor
{
    // Fills the arrow's SOL::Overlay::CURSOR_SEGMENTS outline segments (outStart[i] to outEnd[i]) in pixels relative
    // to the hot spot (the tip at (0, 0); +X right, +Y down) at a UI scale (non-positive scales use 1). Every point
    // has X >= 0 and Y >= 0, at least one segment starts or ends at the tip, and the largest coordinate is
    // SOL::Overlay::CURSOR_LENGTH_PX x scale
    SOLTEST_API void ArrowSegments(float scale, FVector2f (&outStart)[SOL::Overlay::CURSOR_SEGMENTS],
        FVector2f (&outEnd)[SOL::Overlay::CURSOR_SEGMENTS]);

    // Returns the side of the square cursor widget, whose centre is the hot spot (Slate draws a software cursor
    // centred on the pointer): 2 x (CURSOR_LENGTH_PX x scale + padPx), non-positive scales use 1
    SOLTEST_API float WidgetSidePx(float scale, float padPx);
}
