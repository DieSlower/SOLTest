/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapRouteOverlay.h"

#include "SOLConstants.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistry.h"
#include "Universe/SOLRenderPlacement.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "GlobalRenderResources.h"

namespace
{
    // Sizes at a 720 px canvas (scaled with the HUD's UI scale): the screen margin legs are clipped to (so a halo at
    // the edge is not cut), marker rings, centre dot, arrowheads and the tag offset
    constexpr double ROUTE_CLIP_MARGIN_PX = 8.0;
    constexpr double ROUTE_MARKER_RADIUS_PX = 6.0;
    constexpr double ROUTE_MARKER_TARGET_RADIUS_PX = 9.0;
    constexpr double ROUTE_MARKER_RING_PX = 1.6;
    constexpr double ROUTE_MARKER_DOT_PX = 2.2;
    constexpr double ROUTE_ARROW_LENGTH_PX = 9.0;
    constexpr double ROUTE_ARROW_HALF_WIDTH_PX = 4.5;
    constexpr double ROUTE_ARROW_PAIR_GAP_PX = 7.0;         // Ping-pong: each arrowhead this far from the middle
    constexpr double ROUTE_ARROW_MIN_LEG_PX = 48.0;         // No arrow on a leg shorter than this on screen
    constexpr double ROUTE_TAG_OFFSET_PX = 12.0;
    constexpr double ROUTE_MIN_PIECE_PX = 1.0e-3;           // Shorter glow pieces are skipped

    // The draft's segment to the cursor: Secondary at this alpha
    constexpr double ROUTE_CURSOR_ALPHA = 0.8;

    // Above this many pixels the clipped segment's start is too far for a stable dash phase (it then starts at 0)
    constexpr double ROUTE_MAX_DASH_PHASE_PX = 1.0e6;

    // Tessellation and triangle counts
    constexpr int32 ROUTE_MARKER_SEGMENTS = 12;
    constexpr int32 ROUTE_GLOW_PIECE_TRIANGLES = 6;
    constexpr int32 ROUTE_MARKER_RECORD_CAP = 2 * SOL::Route::OVERLAY_MAX_MARKERS_DRAWN;   // Route plus draft
    const FLinearColor ROUTE_TRANSPARENT(0.0f, 0.0f, 0.0f, 0.0f);

    //////////////////////////////////////////////////////////////////////////
    // Returns a palette color scaled by an intensity, alpha 1 (the batch is additive: RGB carries the brightness)
    FLinearColor RouteScaled(const FLinearColor& color, const double intensity)
    {
        const float scale = static_cast<float>(FMath::Max(0.0, intensity));
        return FLinearColor(color.R * scale, color.G * scale, color.B * scale, 1.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Resolves a waypoint's live universe position from its body; false when the body index does not resolve
    bool RouteResolveWaypoint(const FSOLBodyRegistry& registry, const FSOLRouteWaypoint& waypoint, FVector3d& outM)
    {
        if (waypoint.ReferenceBodyIndex < 0 || waypoint.ReferenceBodyIndex >= registry.Num())
        {
            return false;
        }
        outM = SOLRoute::ResolveWaypointM(waypoint, registry.GetPositionM(waypoint.ReferenceBodyIndex));
        return true;
    }
}

//////////////////////////////////////////////////////////////////////////
// Declared here, defined in the .cpp where FCanvasTriangleItem is complete
FSOLMapRouteOverlay::FSOLMapRouteOverlay() = default;

//////////////////////////////////////////////////////////////////////////
// Declared here, defined in the .cpp where FCanvasTriangleItem is complete
FSOLMapRouteOverlay::~FSOLMapRouteOverlay() = default;

//////////////////////////////////////////////////////////////////////////
// Sizes the per-frame buffers and builds the circle table and the tag text once
void FSOLMapRouteOverlay::Initialize()
{
    mUnitCircle.Reset(ROUTE_MARKER_SEGMENTS);
    for (int32 segment = 0; segment < ROUTE_MARKER_SEGMENTS; ++segment)
    {
        const double angle = UE_DOUBLE_TWO_PI * segment / ROUTE_MARKER_SEGMENTS;
        mUnitCircle.Add(FVector2D(FMath::Cos(angle), FMath::Sin(angle)));
    }
    mDashes.Init(FVector2D::ZeroVector, SOL::Route::OVERLAY_MAX_DASHES);

    // Markers: the route's and the draft's
    mMarkers.Reset(ROUTE_MARKER_RECORD_CAP);
    mBatch = MakeUnique<FCanvasTriangleItem>(FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector,
        GWhiteTexture);
    mBatch->BlendMode = SE_BLEND_Additive;
    mBatch->TriangleList.Reset(SOL::Route::OVERLAY_MAX_TRIANGLES);
    mPausedText = FText::FromString(TEXT("PAUSED"));
}

//////////////////////////////////////////////////////////////////////////
// Clears the last frame's draw state (counts, markers) for a frame that does not draw the overlay
void FSOLMapRouteOverlay::ResetFrame()
{
    mLastLegsDrawn = 0;
    mLastDashesDrawn = 0;
    mLastArrowsDrawn = 0;
    mLastTriangles = 0;
    mHasDrawnShipLeg = false;
    mHasDrawnCursorSegment = false;
    mHasDrawnPausedTag = false;
    mIsBudgetExhausted = false;
    mIsLimitHit = false;
    mBudgetLimit = SOL::Route::OVERLAY_MAX_TRIANGLES;
    mMarkers.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Draws the route and the draft for this frame
void FSOLMapRouteOverlay::Draw(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLAnchorSubsystem& anchor,
    const FSOLBodyRegistry& registry, const FSOLRouteOverlayInputs& inputs, UFont* font, const float uiScale)
{
    ResetFrame();
    if (!mBatch.IsValid())
    {
        return;
    }
    mBatch->Texture = GWhiteTexture;
    mBatch->TriangleList.Reset();

    FFrame frame;
    frame.View = &view;
    frame.Anchor = &anchor;
    frame.UiScale = uiScale;
    frame.ViewpointM = anchor.GetViewpointM();
    frame.ViewpointOffsetCm = anchor.ComputePointRenderLocationCm(frame.ViewpointM) - view.LocationCm;
    const double marginPx = ROUTE_CLIP_MARGIN_PX * uiScale;
    frame.RectMin = FVector2D(-marginPx, -marginPx);
    frame.RectMax = FVector2D(canvas.ClipX + marginPx, canvas.ClipY + marginPx);

    // The draft's segment to the cursor first: it is what the player is editing right now
    const int32 draftCount = inputs.DraftWaypoints.Num();
    if (inputs.bHasCursorPoint)
    {
        FVector3d fromM = inputs.ShipPositionM;
        bool bHasFrom = inputs.bHasShip;
        FLegStyle cursorStyle;
        cursorStyle.Color = SOL::HudPalette::SECONDARY;
        cursorStyle.Alpha = ROUTE_CURSOR_ALPHA;
        if (draftCount > 0)
        {
            bHasFrom = RouteResolveWaypoint(registry, inputs.DraftWaypoints[draftCount - 1], fromM);
            cursorStyle.TrimStartPx = ROUTE_MARKER_RADIUS_PX * uiScale;
        }
        if (bHasFrom)
        {
            mHasDrawnCursorSegment = DrawLeg(frame, fromM, inputs.CursorPointM, cursorStyle);
        }
    }

    // Active, paused or finished route, within its share of the triangles: the ship's leg to the target first, then
    // the legs around the active one
    SetBudgetLimit(mBatch->TriangleList.Num() + static_cast<int32>(SOL::Route::OVERLAY_ROUTE_TRIANGLE_SHARE
        * SOL::Route::OVERLAY_MAX_TRIANGLES));
    const int32 routeCount = inputs.RouteWaypoints.Num();
    int32 highlighted = INDEX_NONE;
    if (routeCount > 0)
    {
        const FSOLRouteCursor& cursor = inputs.RouteCursor;
        FLegStyle style;
        style.Color = SOL::HudPalette::PRIMARY;
        style.Alpha = SOLRouteOverlayMath::StyleAlpha(inputs.RouteStyle);
        style.Arrows = inputs.RouteKind == ESOLRouteKind::PatrolPingPong ? 2 : 1;
        style.bReverseFade = inputs.RouteKind == ESOLRouteKind::PatrolPingPong && cursor.Direction < 0;
        const bool bFinished = cursor.bFinished || inputs.RouteStyle == ESOLRouteDrawStyle::Finished;
        highlighted = bFinished || !inputs.RouteWaypoints.IsValidIndex(cursor.TargetIndex) ? INDEX_NONE
            : cursor.TargetIndex;
        FVector3d targetM;
        if (inputs.bHasShip && highlighted != INDEX_NONE
            && RouteResolveWaypoint(registry, inputs.RouteWaypoints[highlighted], targetM))
        {
            FLegStyle shipStyle = style;
            shipStyle.bReverseFade = false;
            shipStyle.Arrows = 1;
            shipStyle.TrimEndPx = ROUTE_MARKER_TARGET_RADIUS_PX * uiScale;
            mHasDrawnShipLeg = DrawLeg(frame, inputs.ShipPositionM, targetM, shipStyle);
        }
        const int32 legCount = SOLRouteOverlayMath::LegCount(inputs.RouteKind, routeCount);
        const int32 activeLeg = SOLRouteOverlayMath::ActiveLegIndex(inputs.RouteKind, routeCount, cursor);
        const int32 focusLeg = activeLeg != INDEX_NONE ? activeLeg : (bFinished ? legCount - 1 : 0);
        DrawRoute(frame, registry, inputs.RouteWaypoints, inputs.RouteKind, focusLeg, style, false, highlighted,
            inputs.PlanePointM);
    }

    // Draft (map planning): dim, focused on its newest leg, with the rest of the triangles
    SetBudgetLimit(SOL::Route::OVERLAY_MAX_TRIANGLES);
    if (draftCount > 0)
    {
        FLegStyle style;
        style.Color = SOL::HudPalette::PRIMARY;
        style.Alpha = SOLRouteOverlayMath::StyleAlpha(ESOLRouteDrawStyle::Draft);
        style.Arrows = inputs.DraftKind == ESOLRouteKind::PatrolPingPong ? 2 : 1;
        const int32 legCount = SOLRouteOverlayMath::LegCount(inputs.DraftKind, draftCount);
        DrawRoute(frame, registry, inputs.DraftWaypoints, inputs.DraftKind, legCount - 1, style, true, INDEX_NONE,
            inputs.PlanePointM);
    }
    mLastTriangles = mBatch->TriangleList.Num();
    if (mLastTriangles > 0)
    {
        canvas.DrawItem(*mBatch);
    }

    // PAUSED tag beside the paused route's target marker
    if (inputs.RouteStyle == ESOLRouteDrawStyle::Paused && highlighted != INDEX_NONE && font != nullptr)
    {
        for (const FMarker& marker : mMarkers)
        {
            if (!marker.bDraft && marker.WaypointIndex == highlighted)
            {
                const double offsetPx = ROUTE_TAG_OFFSET_PX * uiScale;
                FCanvasTextItem tag(marker.ScreenPx + FVector2D(offsetPx, -offsetPx), mPausedText, font,
                    SOL::HudPalette::SECONDARY);
                tag.Scale = FVector2D(uiScale, uiScale);
                canvas.DrawItem(tag);
                mHasDrawnPausedTag = true;
                break;
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws one route's legs (priority order around the focus leg) and its waypoint markers
void FSOLMapRouteOverlay::DrawRoute(const FFrame& frame, const FSOLBodyRegistry& registry,
    const TConstArrayView<FSOLRouteWaypoint> waypoints, const ESOLRouteKind kind, const int32 focusLeg,
    const FLegStyle& baseStyle, const bool bDraft, const int32 highlightedWaypoint, const FVector3d& planePointM)
{
    const int32 waypointCount = waypoints.Num();
    const int32 legCount = SOLRouteOverlayMath::LegCount(kind, waypointCount);
    const int32 considered = FMath::Min(legCount, SOL::Route::OVERLAY_MAX_LEGS_CONSIDERED);
    int32 minLeg = legCount;
    int32 maxLeg = -1;
    int32 legsDrawn = 0;

    // Legs nearest the focus first; ranks around one leg form a contiguous index range, so the markers below can
    // follow that range
    for (int32 rank = 0; rank < considered; ++rank)
    {
        if (legsDrawn >= SOL::Route::OVERLAY_MAX_LEGS_DRAWN || mIsLimitHit)
        {
            break;
        }
        FSOLRouteLeg leg;
        if (!SOLRouteOverlayMath::GetLeg(kind, waypointCount, SOLRouteOverlayMath::LegAtPriority(legCount, focusLeg,
            rank), leg))
        {
            continue;
        }
        const int32 legIndex = leg.FromIndex;
        minLeg = FMath::Min(minLeg, legIndex);
        maxLeg = FMath::Max(maxLeg, legIndex);
        FVector3d fromM;
        FVector3d toM;
        if (!RouteResolveWaypoint(registry, waypoints[leg.FromIndex], fromM)
            || !RouteResolveWaypoint(registry, waypoints[leg.ToIndex], toM))
        {
            continue;
        }
        FLegStyle style = baseStyle;
        style.bDashed = leg.bClosing;
        style.TrimStartPx = MarkerRadiusPx(leg.FromIndex == highlightedWaypoint, frame.UiScale);
        style.TrimEndPx = MarkerRadiusPx(leg.ToIndex == highlightedWaypoint, frame.UiScale);
        if (DrawLeg(frame, fromM, toM, style))
        {
            ++legsDrawn;
            ++mLastLegsDrawn;
        }
    }

    // Waypoint markers over the legs looked at (a route without legs has its single waypoint), nearest the target
    // (or the focus leg's end) first, capped
    const int32 firstWaypoint = legCount > 0 ? minLeg : 0;
    const int32 lastWaypoint = legCount > 0 ? FMath::Min(maxLeg + 1, waypointCount - 1)
        : FMath::Min(waypointCount, 1) - 1;
    const int32 markerRange = lastWaypoint - firstWaypoint + 1;
    const int32 focusWaypoint = highlightedWaypoint != INDEX_NONE ? highlightedWaypoint
        : FMath::Clamp(focusLeg + 1, 0, waypointCount - 1);
    const int32 markerCount = FMath::Min(markerRange, SOL::Route::OVERLAY_MAX_MARKERS_DRAWN);
    for (int32 rank = 0; rank < markerCount && !mIsLimitHit; ++rank)
    {
        const int32 index = firstWaypoint + SOLRouteOverlayMath::LegAtPriority(markerRange,
            focusWaypoint - firstWaypoint, rank);
        FVector3d pointM;
        if (!RouteResolveWaypoint(registry, waypoints[index], pointM))
        {
            continue;
        }
        FVector2D screenPx;
        DrawWaypointMarker(frame, pointM, baseStyle.Alpha, SOLRouteOverlayMath::IsBelowPlane(pointM, planePointM),
            index == highlightedWaypoint, index, bDraft, screenPx);
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws one leg between two universe points; returns true when any part of it was on screen
bool FSOLMapRouteOverlay::DrawLeg(const FFrame& frame, const FVector3d& fromM, const FVector3d& toM,
    const FLegStyle& style)
{
    // Near-plane clip in camera space (the depth compression is radial from the viewpoint, so the clipped straight
    // segment still projects onto the leg's true screen line), then the screen clip
    const FVector localA = ToLocal(frame, fromM);
    const FVector localB = ToLocal(frame, toM);
    double t0 = 0.0;
    double t1 = 1.0;
    if (!SOLRouteOverlayMath::ClipToNearPlane(localA, localB, SOL::MAP_OVERLAY_NEAR_PLANE_CM, t0, t1))
    {
        return false;
    }
    const FVector nearA = FMath::Lerp(localA, localB, t0);
    const FVector nearB = FMath::Lerp(localA, localB, t1);
    const FVector2D screenA = LocalToScreen(frame, nearA);
    const FVector2D screenB = LocalToScreen(frame, nearB);
    double u0 = 0.0;
    double u1 = 1.0;
    if (!SOLRouteOverlayMath::ClipToRect(screenA, screenB, frame.RectMin, frame.RectMax, u0, u1))
    {
        return false;
    }

    // Stop short of the waypoint rings at ends that are the leg's own (unclipped) ends, so a hollow ring stays hollow
    const double fullPx = FVector2D::Distance(screenA, screenB);
    if (fullPx > ROUTE_MIN_PIECE_PX)
    {
        if (t0 == 0.0 && u0 == 0.0)
        {
            u0 = FMath::Min(u1, style.TrimStartPx / fullPx);
        }
        if (t1 == 1.0 && u1 == 1.0)
        {
            u1 = FMath::Max(u0, 1.0 - style.TrimEndPx / fullPx);
        }
    }
    const FVector2D drawA = FMath::Lerp(screenA, screenB, u0);
    const FVector2D drawB = FMath::Lerp(screenA, screenB, u1);
    const double lengthPx = FVector2D::Distance(drawA, drawB);
    if (!(lengthPx > ROUTE_MIN_PIECE_PX))
    {
        return true;
    }

    // Leg parameter (0 at fromM, 1 at toM) of a screen parameter along screenA -> screenB: perspective-correct
    // interpolation of the clipped depths, then mapped into the near-clipped range
    const double depthA = nearA.X;
    const double depthB = nearB.X;
    const auto legParam = [depthA, depthB, t0, t1](const double u)
    {
        const double denominator = (1.0 - u) * depthB + u * depthA;
        const double s = denominator > 0.0 ? u * depthA / denominator : u;
        return t0 + (t1 - t0) * s;
    };
    const auto intensityAt = [&style](const double legT)
    {
        return style.Alpha * SOLRouteOverlayMath::DirectionalFade(style.bReverseFade ? 1.0 - legT : legT);
    };

    // Glow pieces: dashes for a closing leg, otherwise a fade tessellation of the visible part
    const float uiScale = frame.UiScale;
    if (style.bDashed)
    {
        const double startOffsetPx = fullPx * u0;
        const double phasePx = startOffsetPx < ROUTE_MAX_DASH_PHASE_PX ? startOffsetPx : 0.0;
        const int32 dashCount = SOLRouteOverlayMath::ComputeDashes(lengthPx, SOL::Route::OVERLAY_DASH_PX * uiScale,
            SOL::Route::OVERLAY_GAP_PX * uiScale, phasePx, mDashes);
        for (int32 dash = 0; dash < dashCount && !mIsLimitHit; ++dash)
        {
            const FVector2D& interval = mDashes[dash];
            const double uA = FMath::Lerp(u0, u1, interval.X);
            const double uB = FMath::Lerp(u0, u1, interval.Y);
            AppendGlowPiece(FMath::Lerp(drawA, drawB, interval.X), FMath::Lerp(drawA, drawB, interval.Y), style.Color,
                intensityAt(legParam(uA)), intensityAt(legParam(uB)), uiScale);
            ++mLastDashesDrawn;
        }
    }
    else
    {
        const int32 pieces = SOLRouteOverlayMath::SubdivisionCount(lengthPx,
            SOL::Route::OVERLAY_PX_PER_SUBDIVISION * uiScale, SOL::Route::OVERLAY_MAX_SUBDIVISIONS);
        for (int32 piece = 0; piece < pieces && !mIsLimitHit; ++piece)
        {
            const double fA = static_cast<double>(piece) / pieces;
            const double fB = static_cast<double>(piece + 1) / pieces;
            AppendGlowPiece(FMath::Lerp(drawA, drawB, fA), FMath::Lerp(drawA, drawB, fB), style.Color,
                intensityAt(legParam(FMath::Lerp(u0, u1, fA))), intensityAt(legParam(FMath::Lerp(u0, u1, fB))),
                uiScale);
        }
    }

    // Direction arrows at the leg's true middle when it is on screen (forward; both ways for a Ping-pong)
    if (style.Arrows > 0 && lengthPx >= ROUTE_ARROW_MIN_LEG_PX * uiScale && t0 <= 0.5 && t1 >= 0.5)
    {
        const FVector midLocal = FMath::Lerp(localA, localB, 0.5);
        const FVector2D middle = LocalToScreen(frame, midLocal);
        const FVector2D direction = (screenB - screenA).GetSafeNormal();
        const bool bOnScreen = middle.X >= frame.RectMin.X && middle.X <= frame.RectMax.X
            && middle.Y >= frame.RectMin.Y && middle.Y <= frame.RectMax.Y;
        if (bOnScreen && !direction.IsNearlyZero())
        {
            const FLinearColor arrowColor = RouteScaled(style.Color, style.Alpha);
            if (style.Arrows >= 2)
            {
                const FVector2D offset = direction * (ROUTE_ARROW_PAIR_GAP_PX * uiScale);
                AppendArrow(middle + offset, direction, arrowColor, uiScale);
                AppendArrow(middle - offset, -direction, arrowColor, uiScale);
                mLastArrowsDrawn += 2;
            }
            else
            {
                AppendArrow(middle, direction, arrowColor, uiScale);
                ++mLastArrowsDrawn;
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Draws a waypoint ring (with a centre dot above the plane) and records it; false when off screen or over budget
bool FSOLMapRouteOverlay::DrawWaypointMarker(const FFrame& frame, const FVector3d& pointM, const double alpha,
    const bool bBelowPlane, const bool bHighlighted, const int32 waypointIndex, const bool bDraft,
    FVector2D& outScreenPx)
{
    const FVector local = ToLocal(frame, pointM);
    if (local.X < SOL::MAP_OVERLAY_NEAR_PLANE_CM)
    {
        return false;
    }
    outScreenPx = LocalToScreen(frame, local);
    if (outScreenPx.X < frame.RectMin.X || outScreenPx.X > frame.RectMax.X || outScreenPx.Y < frame.RectMin.Y
        || outScreenPx.Y > frame.RectMax.Y)
    {
        return false;
    }
    const float uiScale = frame.UiScale;
    const double outerPx = MarkerRadiusPx(bHighlighted, uiScale);
    const FLinearColor color = RouteScaled(bBelowPlane ? SOL::HudPalette::SECONDARY : SOL::HudPalette::PRIMARY, alpha);
    const int32 triangles = 2 * mUnitCircle.Num() + (bBelowPlane ? 0 : mUnitCircle.Num());
    if (!Reserve(triangles))
    {
        return false;
    }
    AppendRing(outScreenPx, outerPx - ROUTE_MARKER_RING_PX * uiScale, outerPx, color);
    if (!bBelowPlane)
    {
        AppendDisc(outScreenPx, ROUTE_MARKER_DOT_PX * uiScale, color);
    }
    if (mMarkers.Num() < ROUTE_MARKER_RECORD_CAP)
    {
        FMarker& marker = mMarkers.AddDefaulted_GetRef();
        marker.ScreenPx = outScreenPx;
        marker.WaypointIndex = waypointIndex;
        marker.bDraft = bDraft;
        marker.bBelowPlane = bBelowPlane;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Projects a universe point to camera-local render space (cm, X forward)
FVector FSOLMapRouteOverlay::ToLocal(const FFrame& frame, const FVector3d& pointM)
{
    // True (uncompressed) offset from the viewpoint: the far-field compression is radial from the viewpoint, so the
    // direction (and screen position) is the same as the bodies' placement, while depths and clip parameters stay true
    const FVector offsetCm = SOLRender::EclipticToUnreal(pointM - frame.ViewpointM) * SOLRender::MetersToCm;
    return frame.View->Rotation.UnrotateVector(offsetCm + frame.ViewpointOffsetCm);
}

//////////////////////////////////////////////////////////////////////////
// Projects a camera-local point to the screen (caller guarantees X > 0)
FVector2D FSOLMapRouteOverlay::LocalToScreen(const FFrame& frame, const FVector& local)
{
    const FSOLMapOverlayView& view = *frame.View;
    return FVector2D(view.Center.X + view.FocalPx * (local.Y / local.X),
        view.Center.Y - view.FocalPx * (local.Z / local.X));
}

//////////////////////////////////////////////////////////////////////////
// Appends one glow piece (core plus soft halo) between two screen points with their intensities
void FSOLMapRouteOverlay::AppendGlowPiece(const FVector2D& a, const FVector2D& b, const FLinearColor& color,
    const double intensityA, const double intensityB, const float uiScale)
{
    const FVector2D delta = b - a;
    const double length = delta.Size();
    if (length < ROUTE_MIN_PIECE_PX || !Reserve(ROUTE_GLOW_PIECE_TRIANGLES))
    {
        return;
    }
    const FVector2D normal = FVector2D(-delta.Y, delta.X) / length;
    const FVector2D core = normal * (SOL::Hologram::GLOW_CORE_HALF_PX * uiScale);
    const FVector2D halo = normal * (SOL::Hologram::GLOW_HALO_HALF_PX * uiScale);
    const FLinearColor coreA = RouteScaled(color, intensityA);
    const FLinearColor coreB = RouteScaled(color, intensityB);
    const FLinearColor haloA = RouteScaled(color, intensityA * SOL::Hologram::GLOW_HALO_INTENSITY);
    const FLinearColor haloB = RouteScaled(color, intensityB * SOL::Hologram::GLOW_HALO_INTENSITY);

    // Core quad
    AppendTriangle(a - core, a + core, b + core, coreA, coreA, coreB);
    AppendTriangle(a - core, b + core, b - core, coreA, coreB, coreB);

    // Halo quads on both sides, fading to nothing at the outer edge
    AppendTriangle(a - halo, a - core, b - core, ROUTE_TRANSPARENT, haloA, haloB);
    AppendTriangle(a - halo, b - core, b - halo, ROUTE_TRANSPARENT, haloB, ROUTE_TRANSPARENT);
    AppendTriangle(a + core, a + halo, b + halo, haloA, ROUTE_TRANSPARENT, ROUTE_TRANSPARENT);
    AppendTriangle(a + core, b + halo, b + core, haloA, ROUTE_TRANSPARENT, haloB);
}

//////////////////////////////////////////////////////////////////////////
// Appends a filled arrowhead centred on a screen point, pointing along a unit direction
void FSOLMapRouteOverlay::AppendArrow(const FVector2D& center, const FVector2D& direction, const FLinearColor& color,
    const float uiScale)
{
    if (!Reserve(1))
    {
        return;
    }
    const FVector2D along = direction * (0.5 * ROUTE_ARROW_LENGTH_PX * uiScale);
    const FVector2D side = FVector2D(-direction.Y, direction.X) * (ROUTE_ARROW_HALF_WIDTH_PX * uiScale);
    AppendTriangle(center + along, center - along + side, center - along - side, color, color, color);
}

//////////////////////////////////////////////////////////////////////////
// Appends a ring (annulus) between two radii
void FSOLMapRouteOverlay::AppendRing(const FVector2D& center, const double innerPx, const double outerPx,
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
// Appends a filled disc
void FSOLMapRouteOverlay::AppendDisc(const FVector2D& center, const double radiusPx, const FLinearColor& color)
{
    const int32 segments = mUnitCircle.Num();
    for (int32 segment = 0; segment < segments; ++segment)
    {
        AppendTriangle(center, center + mUnitCircle[segment] * radiusPx,
            center + mUnitCircle[(segment + 1) % segments] * radiusPx, color, color, color);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true when count more triangles fit; marks the budget exhausted otherwise
bool FSOLMapRouteOverlay::Reserve(const int32 count)
{
    if (mIsLimitHit || mBatch->TriangleList.Num() + count > mBudgetLimit)
    {
        mIsBudgetExhausted = true;
        mIsLimitHit = true;
        return false;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the triangle count the next drawing may fill up to (at most the batch's budget) and clears the limit flag
void FSOLMapRouteOverlay::SetBudgetLimit(const int32 limit)
{
    mBudgetLimit = FMath::Min(limit, SOL::Route::OVERLAY_MAX_TRIANGLES);
    mIsLimitHit = false;
}

//////////////////////////////////////////////////////////////////////////
// Returns a waypoint ring's outer radius on screen (the current target's is larger)
double FSOLMapRouteOverlay::MarkerRadiusPx(const bool bHighlighted, const float uiScale)
{
    return (bHighlighted ? ROUTE_MARKER_TARGET_RADIUS_PX : ROUTE_MARKER_RADIUS_PX) * uiScale;
}

//////////////////////////////////////////////////////////////////////////
// Appends one triangle with per-vertex colors
void FSOLMapRouteOverlay::AppendTriangle(const FVector2D& a, const FVector2D& b, const FVector2D& c,
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
