/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapPickOverlay.h"

#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLMapPicking.h"
#include "SOLConstants.h"
#include "Universe/SOLAnchorSubsystem.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GlobalRenderResources.h"

namespace
{
    // Disc tessellation (rim points around the reference in the ecliptic plane)
    constexpr int32 MAP_PICK_DISC_SEGMENTS = 64;

    // Projection: points closer than this in front of the camera (cm) are not drawn, nor points projecting farther
    // than this many pixels from the screen (keeps canvas coordinates sane at grazing angles)
    constexpr double MAP_PICK_NEAR_PLANE_CM = 10.0;
    constexpr double MAP_PICK_MAX_SCREEN_PX = 1.0e5;

    // Sizes at a 720 px canvas (scaled with the HUD's UI scale): reference ring radius, XY point half-size, guide
    // tip tick half-length, destination marker half-diagonal and the gap/length of its crosshair arms
    constexpr double MAP_PICK_REFERENCE_RING_PX = 11.0;
    constexpr double MAP_PICK_POINT_HALF_PX = 3.0;
    constexpr double MAP_PICK_GUIDE_TICK_PX = 6.0;
    constexpr double MAP_PICK_MARKER_HALF_PX = 9.0;
    constexpr double MAP_PICK_MARKER_ARM_GAP_PX = 3.0;
    constexpr double MAP_PICK_MARKER_ARM_PX = 6.0;
    constexpr int32 MAP_PICK_RING_SEGMENTS_STEP = 2;    // The screen ring uses every other disc point (32 segments)

    // Line thickness (pixels at a 720 px canvas)
    constexpr float MAP_PICK_LINE_PX = 1.5f;
    constexpr float MAP_PICK_MARKER_LINE_PX = 2.0f;
    constexpr float MAP_PICK_RIM_LINE_PX = 1.0f;

    // Colors (canvas lines ignore alpha, so the dim variants are dimmed in RGB); the disc fill is translucent
    const FLinearColor MAP_PICK_COLOR(0.35f, 0.85f, 1.0f, 1.0f);
    const FLinearColor MAP_PICK_DIM_COLOR(0.17f, 0.42f, 0.5f, 1.0f);
    const FLinearColor MAP_PICK_DISC_FILL(0.35f, 0.85f, 1.0f, 0.14f);
    const FLinearColor MAP_PICK_MARKER_COLOR(1.0f, 0.35f, 0.85f, 1.0f);
    const FLinearColor MAP_PICK_MARKER_DIM_COLOR(0.55f, 0.2f, 0.47f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Declared here, defined in the .cpp where FCanvasTriangleItem is complete
FSOLMapPickOverlay::FSOLMapPickOverlay() = default;

//////////////////////////////////////////////////////////////////////////
// Declared here, defined in the .cpp where FCanvasTriangleItem is complete
FSOLMapPickOverlay::~FSOLMapPickOverlay() = default;

//////////////////////////////////////////////////////////////////////////
// Builds the disc tessellation table and sizes the per-frame buffers
void FSOLMapPickOverlay::Initialize()
{
    mUnitCircle.Reset(MAP_PICK_DISC_SEGMENTS);
    for (int32 segment = 0; segment < MAP_PICK_DISC_SEGMENTS; ++segment)
    {
        const double angle = UE_DOUBLE_TWO_PI * segment / MAP_PICK_DISC_SEGMENTS;
        mUnitCircle.Add(FVector2D(FMath::Cos(angle), FMath::Sin(angle)));
    }
    mRimPx.Init(FVector2D::ZeroVector, MAP_PICK_DISC_SEGMENTS);
    mRimValid.Init(false, MAP_PICK_DISC_SEGMENTS);
    mDiscBatch = MakeUnique<FCanvasTriangleItem>(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector,
        GWhiteTexture);
    mDiscBatch->BlendMode = SE_BLEND_Translucent;
    mDiscBatch->TriangleList.Reset(MAP_PICK_DISC_SEGMENTS);
}

//////////////////////////////////////////////////////////////////////////
// Draws the reference ring, the disc, the guide line and the destination marker for the current pick
void FSOLMapPickOverlay::Draw(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLMapModeSubsystem& mapMode,
    const USOLAnchorSubsystem& anchor, const float uiScale)
{
    mLastDiscSegmentsDrawn = 0;
    mHasDrawnGuide = false;
    mHasDrawnMarker = false;
    mHasDrawnFinalMarker = false;
    const FSOLMapPickState& pick = mapMode.GetPickState();
    if (!mDiscBatch.IsValid() || !pick.bHasReference)
    {
        return;
    }
    const float lineThickness = MAP_PICK_LINE_PX * uiScale;
    const FVector3d& referenceM = mapMode.GetPickReferencePositionM();
    FVector2D referenceScreen;
    const bool bHasReferenceScreen = ProjectPoint(view, anchor, referenceM, referenceScreen);

    // The reference (picked body or ship) gets a ring
    if (bHasReferenceScreen)
    {
        DrawScreenCircle(canvas, referenceScreen, MAP_PICK_REFERENCE_RING_PX * uiScale, MAP_PICK_COLOR, lineThickness);
    }

    // Planar offset: the disc and the radius line to the XY point, bright and filled while dragging, dim after
    const FVector2D planarM = mapMode.GetPickPlanarOffsetM();
    const bool bDragging = mapMode.IsPickDragging();
    const FLinearColor& planarColor = bDragging ? MAP_PICK_COLOR : MAP_PICK_DIM_COLOR;
    const FVector3d planarPointM = SOLMapPicking::ComposeDestination(referenceM, planarM.X, planarM.Y, 0.0,
        SOL::MAP_PICK_ECLIPTIC_X, SOL::MAP_PICK_ECLIPTIC_Y, SOL::MAP_PICK_UP);
    FVector2D planarScreen;
    const bool bHasPlanarScreen = ProjectPoint(view, anchor, planarPointM, planarScreen);
    if (bDragging || pick.bPlanarLocked)
    {
        DrawDisc(canvas, view, anchor, referenceM, referenceScreen, bHasReferenceScreen, planarM.Size(), bDragging,
            planarColor, MAP_PICK_RIM_LINE_PX * uiScale);
        if (bHasReferenceScreen && bHasPlanarScreen)
        {
            DrawLine(canvas, referenceScreen, planarScreen, planarColor, lineThickness);
        }
        if (bHasPlanarScreen)
        {
            const double half = MAP_PICK_POINT_HALF_PX * uiScale;
            canvas.K2_DrawBox(planarScreen - FVector2D(half), FVector2D(2.0 * half), lineThickness, MAP_PICK_COLOR);
        }
    }

    // Height: the vertical guide line from the locked XY point to the previewed (or locked) height, tick at the tip
    if (pick.bPlanarLocked && (mapMode.IsHeightPreviewing() || pick.bHeightLocked))
    {
        const FVector3d tipM = planarPointM + SOL::MAP_PICK_UP * mapMode.GetPickHeightOffsetM();
        FVector2D tipScreen;
        if (bHasPlanarScreen && ProjectPoint(view, anchor, tipM, tipScreen))
        {
            const FLinearColor& guideColor = pick.bHeightLocked ? MAP_PICK_DIM_COLOR : MAP_PICK_COLOR;
            const FVector2D tick(MAP_PICK_GUIDE_TICK_PX * uiScale, 0.0);
            DrawLine(canvas, planarScreen, tipScreen, guideColor, lineThickness);
            DrawLine(canvas, tipScreen - tick, tipScreen + tick, guideColor, lineThickness);
            mHasDrawnGuide = true;
        }
    }

    // Destination marker at the live destination: dim with only the planar lock, bright once the height is locked
    FVector3d destinationM;
    FVector2D destinationScreen;
    if (mapMode.GetLiveDestinationM(destinationM) && ProjectPoint(view, anchor, destinationM, destinationScreen))
    {
        DrawMarker(canvas, destinationScreen, MAP_PICK_MARKER_HALF_PX * uiScale, pick.bHeightLocked
            ? MAP_PICK_MARKER_COLOR : MAP_PICK_MARKER_DIM_COLOR, MAP_PICK_MARKER_LINE_PX * uiScale);
        mHasDrawnMarker = true;
        mHasDrawnFinalMarker = pick.bHeightLocked;
    }
}

//////////////////////////////////////////////////////////////////////////
// Projects a universe point (meters) to the screen through the bodies' render placement; false when behind the camera
// or absurdly far off-screen
bool FSOLMapPickOverlay::ProjectPoint(const FSOLMapOverlayView& view, const USOLAnchorSubsystem& anchor,
    const FVector3d& pointM, FVector2D& outScreen)
{
    const FVector local = view.Rotation.UnrotateVector(anchor.ComputePointRenderLocationCm(pointM) - view.LocationCm);
    if (local.X < MAP_PICK_NEAR_PLANE_CM)
    {
        return false;
    }
    outScreen = FVector2D(view.Center.X + view.FocalPx * (local.Y / local.X),
        view.Center.Y - view.FocalPx * (local.Z / local.X));
    return FMath::Abs(outScreen.X) <= MAP_PICK_MAX_SCREEN_PX && FMath::Abs(outScreen.Y) <= MAP_PICK_MAX_SCREEN_PX;
}

//////////////////////////////////////////////////////////////////////////
// Draws one screen-space line
void FSOLMapPickOverlay::DrawLine(UCanvas& canvas, const FVector2D& a, const FVector2D& b, const FLinearColor& color,
    const float thickness)
{
    FCanvasLineItem line(a, b);
    line.SetColor(color);
    line.LineThickness = thickness;
    canvas.DrawItem(line);
}

//////////////////////////////////////////////////////////////////////////
// Draws the disc of the planar offset around the reference: rim outline, plus the translucent fill when filled
void FSOLMapPickOverlay::DrawDisc(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLAnchorSubsystem& anchor,
    const FVector3d& referenceM, const FVector2D& referenceScreen, const bool bHasReferenceScreen, const double radiusM,
    const bool bFilled, const FLinearColor& outlineColor, const float rimThickness)
{
    if (!(radiusM > 0.0))
    {
        return;
    }

    // Rim points in the ecliptic plane through the reference, each projected along its own direction (so the disc
    // is correct in perspective and under the far-field depth compression)
    const int32 segments = mUnitCircle.Num();
    for (int32 segment = 0; segment < segments; ++segment)
    {
        const FVector2D& unit = mUnitCircle[segment];
        const FVector3d rimM = SOLMapPicking::ComposeDestination(referenceM, radiusM * unit.X, radiusM * unit.Y, 0.0,
            SOL::MAP_PICK_ECLIPTIC_X, SOL::MAP_PICK_ECLIPTIC_Y, SOL::MAP_PICK_UP);
        mRimValid[segment] = ProjectPoint(view, anchor, rimM, mRimPx[segment]);
    }

    // Fill: one translucent triangle fan from the reference, only between rim points that both projected
    if (bFilled && bHasReferenceScreen)
    {
        mDiscBatch->Texture = GWhiteTexture;
        mDiscBatch->TriangleList.Reset();
        for (int32 segment = 0; segment < segments; ++segment)
        {
            const int32 next = (segment + 1) % segments;
            if (!mRimValid[segment] || !mRimValid[next])
            {
                continue;
            }
            FCanvasUVTri& triangle = mDiscBatch->TriangleList.AddDefaulted_GetRef();
            triangle.V0_Pos = referenceScreen;
            triangle.V1_Pos = mRimPx[segment];
            triangle.V2_Pos = mRimPx[next];
            triangle.V0_Color = MAP_PICK_DISC_FILL;
            triangle.V1_Color = MAP_PICK_DISC_FILL;
            triangle.V2_Color = MAP_PICK_DISC_FILL;
        }
        if (mDiscBatch->TriangleList.Num() > 0)
        {
            canvas.DrawItem(*mDiscBatch);
        }
    }

    // Rim outline
    for (int32 segment = 0; segment < segments; ++segment)
    {
        const int32 next = (segment + 1) % segments;
        if (mRimValid[segment] && mRimValid[next])
        {
            DrawLine(canvas, mRimPx[segment], mRimPx[next], outlineColor, rimThickness);
            ++mLastDiscSegmentsDrawn;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws a screen-space circle outline from the unit-circle table
void FSOLMapPickOverlay::DrawScreenCircle(UCanvas& canvas, const FVector2D& center, const double radiusPx,
    const FLinearColor& color, const float thickness) const
{
    const int32 segments = mUnitCircle.Num();
    for (int32 segment = 0; segment < segments; segment += MAP_PICK_RING_SEGMENTS_STEP)
    {
        const int32 next = (segment + MAP_PICK_RING_SEGMENTS_STEP) % segments;
        DrawLine(canvas, center + radiusPx * mUnitCircle[segment], center + radiusPx * mUnitCircle[next], color,
            thickness);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the destination marker: a diamond with four crosshair arms
void FSOLMapPickOverlay::DrawMarker(UCanvas& canvas, const FVector2D& center, const double halfPx,
    const FLinearColor& color, const float thickness)
{
    // Diamond
    const FVector2D top = center + FVector2D(0.0, -halfPx);
    const FVector2D right = center + FVector2D(halfPx, 0.0);
    const FVector2D bottom = center + FVector2D(0.0, halfPx);
    const FVector2D left = center + FVector2D(-halfPx, 0.0);
    DrawLine(canvas, top, right, color, thickness);
    DrawLine(canvas, right, bottom, color, thickness);
    DrawLine(canvas, bottom, left, color, thickness);
    DrawLine(canvas, left, top, color, thickness);

    // Crosshair arms outside the diamond's corners, and a center dot
    const double scale = halfPx / MAP_PICK_MARKER_HALF_PX;
    const double armStart = halfPx + MAP_PICK_MARKER_ARM_GAP_PX * scale;
    const double armEnd = armStart + MAP_PICK_MARKER_ARM_PX * scale;
    const FVector2D directions[] = { FVector2D(0.0, -1.0), FVector2D(1.0, 0.0), FVector2D(0.0, 1.0),
        FVector2D(-1.0, 0.0) };
    for (const FVector2D& direction : directions)
    {
        DrawLine(canvas, center + direction * armStart, center + direction * armEnd, color, thickness);
    }
    const FVector2D dot(thickness);
    canvas.K2_DrawBox(center - 0.5 * dot, dot, thickness, color);
}
