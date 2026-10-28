/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapHologramOverlay.h"

#include "Map/SOLRouteOverlayMath.h"
#include "SOLConstants.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistry.h"
#include "Universe/SOLRenderPlacement.h"
#include "Visuals/SOLHologramMath.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GlobalRenderResources.h"

namespace
{
    constexpr double HOLO_CLIP_MARGIN_PX = 4.0;             // Screen margin pieces are clipped to (720 px canvas)
    constexpr double HOLO_MIN_PIECE_PX = 1.0e-3;            // Shorter pieces are skipped
    constexpr double HOLO_STEM_HALF_WIDTH_PX = 0.6;         // Stem line half width (720 px canvas)
    constexpr double HOLO_FOOT_RING_PX = 1.0;               // Foot ring thickness (720 px canvas)
    constexpr double HOLO_MAJOR_LINE_TOLERANCE = 1.0e-6;    // |index / ratio - round| below this: a major line
    constexpr int32 HOLO_FOOT_SEGMENTS = 12;
    constexpr int32 HOLO_FOOT_TRIANGLES = 2 * HOLO_FOOT_SEGMENTS;

    //////////////////////////////////////////////////////////////////////////
    // Returns a palette color scaled by an intensity, alpha 1 (the batch is additive: RGB carries the brightness)
    FLinearColor HoloScaled(const FLinearColor& color, const double intensity)
    {
        const float scale = static_cast<float>(FMath::Max(0.0, intensity));
        return FLinearColor(color.R * scale, color.G * scale, color.B * scale, 1.0f);
    }
}

//////////////////////////////////////////////////////////////////////////
// Declared here, defined in the .cpp where FCanvasTriangleItem is complete
FSOLMapHologramOverlay::FSOLMapHologramOverlay() = default;

//////////////////////////////////////////////////////////////////////////
// Declared here, defined in the .cpp where FCanvasTriangleItem is complete
FSOLMapHologramOverlay::~FSOLMapHologramOverlay() = default;

//////////////////////////////////////////////////////////////////////////
// Sizes the batch and builds the circle table once
void FSOLMapHologramOverlay::Initialize()
{
    mUnitCircle.Reset(HOLO_FOOT_SEGMENTS);
    for (int32 segment = 0; segment < HOLO_FOOT_SEGMENTS; ++segment)
    {
        const double angle = UE_DOUBLE_TWO_PI * segment / HOLO_FOOT_SEGMENTS;
        mUnitCircle.Add(FVector2D(FMath::Cos(angle), FMath::Sin(angle)));
    }
    mBatch = MakeUnique<FCanvasTriangleItem>(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector,
        GWhiteTexture);
    mBatch->BlendMode = SE_BLEND_Additive;
    mBatch->TriangleList.Reset(SOL::Hologram::OVERLAY_MAX_TRIANGLES);
}

//////////////////////////////////////////////////////////////////////////
// Clears the last frame's counts for a frame that does not draw the overlay
void FSOLMapHologramOverlay::ResetFrame()
{
    mLastMinorSpacingM = 0.0;
    mLastGridLines = 0;
    mLastStems = 0;
    mLastTriangles = 0;
}

//////////////////////////////////////////////////////////////////////////
// Draws the grid and the stems for this frame
void FSOLMapHologramOverlay::Draw(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLAnchorSubsystem& anchor,
    const FSOLBodyRegistry& registry, const FSOLHologramOverlayInputs& inputs, const float uiScale)
{
    ResetFrame();
    FSOLHologramGridLevel level;
    if (!mBatch.IsValid() || !SOLHologramMath::ComputeGridLevel(inputs.CameraDistanceM, level))
    {
        return;
    }
    mBatch->Texture = GWhiteTexture;
    mBatch->TriangleList.Reset();
    mLastMinorSpacingM = level.MinorSpacingM;

    FFrame frame;
    frame.View = &view;
    frame.UiScale = uiScale;
    frame.ViewpointM = anchor.GetViewpointM();
    frame.ViewpointOffsetCm = anchor.ComputePointRenderLocationCm(frame.ViewpointM) - view.LocationCm;
    const double marginPx = HOLO_CLIP_MARGIN_PX * uiScale;
    frame.RectMin = FVector2D(-marginPx, -marginPx);
    frame.RectMax = FVector2D(canvas.ClipX + marginPx, canvas.ClipY + marginPx);

    // Grid: lines on multiples of the minor spacing out to the extent around the focus, in both ecliptic axes; every
    // GRID_SCALE_RATIO-th line is a major line (full alpha), the rest fade with the level
    const FVector3d& focusM = inputs.FocusM;
    const double extentM = SOL::Hologram::GRID_EXTENT_MAJOR_CELLS * level.MajorSpacingM;
    const FVector2d startM = SOLHologramMath::SnapToGrid(FVector2d(focusM.X - extentM, focusM.Y - extentM),
        level.MinorSpacingM);
    const int32 lineCount = FMath::CeilToInt32(2.0 * extentM / level.MinorSpacingM) + 1;
    const double majorAlpha = SOL::Hologram::GRID_ALPHA;
    const double minorAlpha = SOL::Hologram::GRID_ALPHA * SOL::Hologram::GRID_MINOR_ALPHA_SCALE * level.MinorAlpha;
    for (int32 axis = 0; axis < 2; ++axis)
    {
        for (int32 line = 0; line < lineCount; ++line)
        {
            const double coordinateM = (axis == 0 ? startM.Y : startM.X) + line * level.MinorSpacingM;
            const double majorIndex = coordinateM / level.MajorSpacingM;
            const bool bMajor = FMath::Abs(majorIndex - FMath::RoundToDouble(majorIndex)) < HOLO_MAJOR_LINE_TOLERANCE;
            const double alpha = bMajor ? majorAlpha : minorAlpha;
            if (alpha <= 0.0)
            {
                continue;
            }
            // Axis 0: lines along ecliptic X at a fixed Y; axis 1: along Y at a fixed X
            const FVector3d fromM = axis == 0 ? FVector3d(focusM.X - extentM, coordinateM, focusM.Z)
                : FVector3d(coordinateM, focusM.Y - extentM, focusM.Z);
            const FVector3d toM = axis == 0 ? FVector3d(focusM.X + extentM, coordinateM, focusM.Z)
                : FVector3d(coordinateM, focusM.Y + extentM, focusM.Z);
            mLastGridLines += DrawGridLine(frame, fromM, toM, focusM, level.MajorSpacingM, alpha) ? 1 : 0;
        }
    }

    // Stems: every body, then the route's and the draft's waypoints, up to MAX_STEMS
    const int32 bodyCount = registry.Num();
    for (int32 index = 0; index < bodyCount && mLastStems < SOL::Hologram::MAX_STEMS; ++index)
    {
        mLastStems += DrawStem(frame, registry.GetPositionM(index), inputs, level.MajorSpacingM) ? 1 : 0;
    }
    for (const TConstArrayView<FSOLRouteWaypoint>& waypoints : { inputs.RouteWaypoints, inputs.DraftWaypoints })
    {
        for (const FSOLRouteWaypoint& waypoint : waypoints)
        {
            if (mLastStems >= SOL::Hologram::MAX_STEMS)
            {
                break;
            }
            if (waypoint.ReferenceBodyIndex >= 0 && waypoint.ReferenceBodyIndex < bodyCount)
            {
                const FVector3d pointM = SOLRoute::ResolveWaypointM(waypoint,
                    registry.GetPositionM(waypoint.ReferenceBodyIndex));
                mLastStems += DrawStem(frame, pointM, inputs, level.MajorSpacingM) ? 1 : 0;
            }
        }
    }

    mLastTriangles = mBatch->TriangleList.Num();
    if (mLastTriangles > 0)
    {
        canvas.DrawItem(*mBatch);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws one grid line in world-space pieces faded by their distance to the focus; true when any piece was on screen
bool FSOLMapHologramOverlay::DrawGridLine(const FFrame& frame, const FVector3d& fromM, const FVector3d& toM,
    const FVector3d& focusM, const double majorSpacingM, const double alpha)
{
    const int32 pieces = SOL::Hologram::GRID_PIECES_PER_LINE;
    const double halfWidthPx = SOL::Hologram::GLOW_CORE_HALF_PX * frame.UiScale;
    bool bAnyVisible = false;
    // Each piece's end point is transformed once and carried over as the next piece's start
    FVector localStart = ToCameraLocal(frame, fromM);
    double fadeStart = SOLHologramMath::GridFade(FVector2d(fromM.X - focusM.X, fromM.Y - focusM.Y).Size()
        / majorSpacingM);
    for (int32 piece = 1; piece <= pieces; ++piece)
    {
        const FVector3d pieceEndM = FMath::Lerp(fromM, toM, static_cast<double>(piece) / pieces);
        const double fadeEnd = SOLHologramMath::GridFade(FVector2d(pieceEndM.X - focusM.X, pieceEndM.Y - focusM.Y)
            .Size() / majorSpacingM);
        const FVector localEnd = ToCameraLocal(frame, pieceEndM);
        FVector2D screenA;
        FVector2D screenB;
        if ((fadeStart > 0.0 || fadeEnd > 0.0) && ProjectLocalSegment(frame, localStart, localEnd, screenA, screenB))
        {
            if (!HasRoom(2))
            {
                return bAnyVisible;
            }
            AppendLine(screenA, screenB, HoloScaled(SOL::HudPalette::PRIMARY, alpha * fadeStart),
                HoloScaled(SOL::HudPalette::PRIMARY, alpha * fadeEnd), halfWidthPx);
            bAnyVisible = true;
        }
        localStart = localEnd;
        fadeStart = fadeEnd;
    }
    return bAnyVisible;
}

//////////////////////////////////////////////////////////////////////////
// Draws one stem from a point to the plane (with a ring at its foot); true when drawn
bool FSOLMapHologramOverlay::DrawStem(const FFrame& frame, const FVector3d& pointM,
    const FSOLHologramOverlayInputs& inputs, const double majorSpacingM)
{
    const double heightM = pointM.Z - inputs.FocusM.Z;
    if (!SOLHologramMath::IsStemVisible(heightM, inputs.CameraDistanceM))
    {
        return false;
    }
    const FVector3d footM(pointM.X, pointM.Y, inputs.FocusM.Z);
    const double footDistance = FVector2d(footM.X - inputs.FocusM.X, footM.Y - inputs.FocusM.Y).Size()
        / majorSpacingM;
    const FLinearColor color = HoloScaled(heightM < 0.0 ? SOL::HudPalette::SECONDARY : SOL::HudPalette::PRIMARY,
        SOLHologramMath::StemAlpha(footDistance));
    FVector2D screenA;
    FVector2D screenB;
    if (!ProjectSegment(frame, pointM, footM, screenA, screenB) || !HasRoom(2 + HOLO_FOOT_TRIANGLES))
    {
        return false;
    }
    AppendLine(screenA, screenB, color, color, HOLO_STEM_HALF_WIDTH_PX * frame.UiScale);

    // Foot ring where the stem meets the plane, when the foot itself is on screen
    FVector2D footA;
    FVector2D footB;
    if (ProjectSegment(frame, footM, footM, footA, footB))
    {
        const double outerPx = SOL::Hologram::STEM_FOOT_RADIUS_PX * frame.UiScale;
        AppendRing(footA, outerPx - HOLO_FOOT_RING_PX * frame.UiScale, outerPx, color);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Clips a universe segment to the near plane and the screen; false when nothing is visible
bool FSOLMapHologramOverlay::ProjectSegment(const FFrame& frame, const FVector3d& fromM, const FVector3d& toM,
    FVector2D& outA, FVector2D& outB)
{
    return ProjectLocalSegment(frame, ToCameraLocal(frame, fromM), ToCameraLocal(frame, toM), outA, outB);
}

//////////////////////////////////////////////////////////////////////////
// Returns a universe point's true (uncompressed) offset from the viewpoint in camera axes (X forward, cm), as the
// route overlay projects it
FVector FSOLMapHologramOverlay::ToCameraLocal(const FFrame& frame, const FVector3d& pointM)
{
    return frame.View->Rotation.UnrotateVector(SOLRender::EclipticToUnreal(pointM - frame.ViewpointM)
        * SOLRender::MetersToCm + frame.ViewpointOffsetCm);
}

//////////////////////////////////////////////////////////////////////////
// Clips a camera-space segment to the near plane and the screen; false when nothing is visible
bool FSOLMapHologramOverlay::ProjectLocalSegment(const FFrame& frame, const FVector& localA, const FVector& localB,
    FVector2D& outA, FVector2D& outB)
{
    const FSOLMapOverlayView& view = *frame.View;
    double t0 = 0.0;
    double t1 = 1.0;
    if (!SOLRouteOverlayMath::ClipToNearPlane(localA, localB, SOL::MAP_OVERLAY_NEAR_PLANE_CM, t0, t1))
    {
        return false;
    }
    const FVector nearA = FMath::Lerp(localA, localB, t0);
    const FVector nearB = FMath::Lerp(localA, localB, t1);
    const FVector2D screenA(view.Center.X + view.FocalPx * (nearA.Y / nearA.X),
        view.Center.Y - view.FocalPx * (nearA.Z / nearA.X));
    const FVector2D screenB(view.Center.X + view.FocalPx * (nearB.Y / nearB.X),
        view.Center.Y - view.FocalPx * (nearB.Z / nearB.X));
    double u0 = 0.0;
    double u1 = 1.0;
    if (!SOLRouteOverlayMath::ClipToRect(screenA, screenB, frame.RectMin, frame.RectMax, u0, u1))
    {
        return false;
    }
    outA = FMath::Lerp(screenA, screenB, u0);
    outB = FMath::Lerp(screenA, screenB, u1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Appends a thin quad between two screen points with per-end colors
void FSOLMapHologramOverlay::AppendLine(const FVector2D& a, const FVector2D& b, const FLinearColor& colorA,
    const FLinearColor& colorB, const double halfWidthPx)
{
    const FVector2D delta = b - a;
    const double length = delta.Size();
    if (length < HOLO_MIN_PIECE_PX)
    {
        return;
    }
    const FVector2D side = FVector2D(-delta.Y, delta.X) * (halfWidthPx / length);
    AppendTriangle(a - side, a + side, b + side, colorA, colorA, colorB);
    AppendTriangle(a - side, b + side, b - side, colorA, colorB, colorB);
}

//////////////////////////////////////////////////////////////////////////
// Appends a ring (annulus) between two radii
void FSOLMapHologramOverlay::AppendRing(const FVector2D& center, const double innerPx, const double outerPx,
    const FLinearColor& color)
{
    const int32 segments = mUnitCircle.Num();
    for (int32 segment = 0; segment < segments; ++segment)
    {
        const FVector2D& unitA = mUnitCircle[segment];
        const FVector2D& unitB = mUnitCircle[(segment + 1) % segments];
        AppendTriangle(center + unitA * innerPx, center + unitA * outerPx, center + unitB * outerPx, color, color,
            color);
        AppendTriangle(center + unitA * innerPx, center + unitB * outerPx, center + unitB * innerPx, color, color,
            color);
    }
}

//////////////////////////////////////////////////////////////////////////
// Appends one triangle with per-vertex colors
void FSOLMapHologramOverlay::AppendTriangle(const FVector2D& a, const FVector2D& b, const FVector2D& c,
    const FLinearColor& colorA, const FLinearColor& colorB, const FLinearColor& colorC)
{
    FCanvasUVTri& triangle = mBatch->TriangleList.AddDefaulted_GetRef();
    triangle.V0_Pos = a;
    triangle.V1_Pos = b;
    triangle.V2_Pos = c;
    triangle.V0_Color = colorA;
    triangle.V1_Color = colorB;
    triangle.V2_Color = colorC;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when count more triangles fit in the batch
bool FSOLMapHologramOverlay::HasRoom(const int32 count) const
{
    return mBatch->TriangleList.Num() + count <= SOL::Hologram::OVERLAY_MAX_TRIANGLES;
}
