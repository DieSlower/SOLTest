/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapBodyOverlay.h"
#include "Map/SOLRouteOverlayMath.h"

#include "CoreMinimal.h"

class FCanvasTriangleItem;
class FSOLBodyRegistry;
class UCanvas;
class UFont;
class USOLAnchorSubsystem;

// What the route overlay draws this frame (filled by the HUD from the route and map-mode subsystems)
struct FSOLRouteOverlayInputs
{
    TConstArrayView<FSOLRouteWaypoint> RouteWaypoints;          // Active / paused / finished route (empty: none)
    FSOLRouteCursor RouteCursor;                                // Its sequencing cursor
    ESOLRouteKind RouteKind = ESOLRouteKind::Move;
    ESOLRouteDrawStyle RouteStyle = ESOLRouteDrawStyle::Active;
    TConstArrayView<FSOLRouteWaypoint> DraftWaypoints;          // Map draft (empty: none)
    ESOLRouteKind DraftKind = ESOLRouteKind::Move;
    FVector3d CursorPointM = FVector3d::ZeroVector;             // Live pick point (map drag), see bHasCursorPoint
    FVector3d ShipPositionM = FVector3d::ZeroVector;            // Player ship, see bHasShip
    FVector3d PlanePointM = FVector3d::ZeroVector;              // The ecliptic plane runs through here (ship / focus)
    bool bHasCursorPoint = false;                               // Draw the draft's segment to the cursor
    bool bHasShip = false;                                      // Draw the leg from the ship to the route's target
};

/**
 * Route overlay (SDD 20 section 2.4, plan step 5), drawn on the HUD canvas in the map and, while the route overlay is
 * on (P), in the flight view. It draws the active / paused / finished route and the map's draft as Primary-palette
 * glow lines (a bright core with a soft halo, additive) with the LT directional fade along each leg (dim at its start,
 * bright at its end), a ring at each waypoint (Primary with a centre dot above the ecliptic plane through the ship or map
 * focus, hollow Secondary below), direction arrows at each leg's middle (both ways for Ping-pong), a dashed closing leg
 * for a Patrol Loop, the leg from the ship to the current target, a PAUSED tag at the target of a paused route, and the
 * draft's segment to the cursor in Secondary. Alphas per style come from SOLRouteOverlayMath::StyleAlpha.
 *
 * Bounded for unlimited waypoints: legs are visited in priority order around the active (or last) leg, at most
 * SOL::Route::OVERLAY_MAX_LEGS_CONSIDERED projected and OVERLAY_MAX_LEGS_DRAWN drawn per route, and at most
 * OVERLAY_MAX_MARKERS_DRAWN markers, nearest the target first; each leg is clipped to the near plane and the screen
 * before it is tessellated, so only its visible part costs triangles. All geometry goes into one triangle batch of at
 * most OVERLAY_MAX_TRIANGLES, reserved once: the cursor segment first, the route within OVERLAY_ROUTE_TRIANGLE_SHARE,
 * the draft in the rest. Points are placed by their true offset from the render viewpoint, which projects exactly
 * where the bodies' compressed placement does (the compression is radial from the viewpoint) but keeps depths true.
 */
class SOLTEST_API FSOLMapRouteOverlay
{
public:

    // Declared here, defined in the .cpp where FCanvasTriangleItem is complete
    FSOLMapRouteOverlay();
    ~FSOLMapRouteOverlay();

    // Sizes the per-frame buffers and builds the circle table and the tag text once
    void Initialize();

    // Clears the last frame's draw state (counts, markers) for a frame that does not draw the overlay
    void ResetFrame();

    // Draws the route and the draft for this frame
    void Draw(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLAnchorSubsystem& anchor,
        const FSOLBodyRegistry& registry, const FSOLRouteOverlayInputs& inputs, UFont* font, float uiScale);

    // One waypoint marker drawn last frame: which route (draft or not), waypoint index and screen position
    struct FMarker
    {
        FVector2D ScreenPx = FVector2D::ZeroVector;
        int32 WaypointIndex = INDEX_NONE;
        bool bDraft = false;
        bool bBelowPlane = false;
    };

    // Returns the waypoint markers drawn in the last frame (verification)
    TConstArrayView<FMarker> GetLastMarkers() const { return mMarkers; }

    // Returns the number of legs (waypoint to waypoint, route and draft) drawn in the last frame (verification)
    int32 GetLastLegsDrawn() const { return mLastLegsDrawn; }

    // Returns the number of dashes of closing legs drawn in the last frame (verification)
    int32 GetLastDashesDrawn() const { return mLastDashesDrawn; }

    // Returns the number of direction arrowheads drawn in the last frame (verification)
    int32 GetLastArrowsDrawn() const { return mLastArrowsDrawn; }

    // Returns the number of triangles in the last frame's batch (verification)
    int32 GetLastTriangles() const { return mLastTriangles; }

    // Returns true when the last frame drew the leg from the ship to the route's target (verification)
    bool HasDrawnShipLeg() const { return mHasDrawnShipLeg; }

    // Returns true when the last frame drew the draft's segment to the cursor (verification)
    bool HasDrawnCursorSegment() const { return mHasDrawnCursorSegment; }

    // Returns true when the last frame drew the PAUSED tag (verification)
    bool HasDrawnPausedTag() const { return mHasDrawnPausedTag; }

    // Returns true when the last frame ran out of triangle budget (verification)
    bool WasBudgetExhausted() const { return mIsBudgetExhausted; }

private:

    // The frame's projection state shared by the drawing helpers
    struct FFrame
    {
        const FSOLMapOverlayView* View = nullptr;
        const USOLAnchorSubsystem* Anchor = nullptr;
        FVector2D RectMin = FVector2D::ZeroVector;      // Screen rectangle legs are clipped to (canvas plus margin)
        FVector2D RectMax = FVector2D::ZeroVector;
        FVector3d ViewpointM = FVector3d::ZeroVector;   // Render viewpoint (universe meters)
        FVector ViewpointOffsetCm = FVector::ZeroVector; // Viewpoint's render location minus the camera's
        float UiScale = 1.0f;
    };

    // How one leg is drawn
    struct FLegStyle
    {
        FLinearColor Color = FLinearColor::White;       // Palette color (alpha folded into RGB when drawn)
        double Alpha = 1.0;
        bool bDashed = false;
        bool bReverseFade = false;                      // Fade toward the leg's start (Ping-pong travelling back)
        int32 Arrows = 0;                               // 0 none, 1 forward, 2 both ways
        double TrimStartPx = 0.0;                       // Stop this short of an on-screen start (its ring)
        double TrimEndPx = 0.0;                         // ...and of an on-screen end
    };

    // Draws one route's legs (priority order around the focus leg) and its waypoint markers
    void DrawRoute(const FFrame& frame, const FSOLBodyRegistry& registry, TConstArrayView<FSOLRouteWaypoint> waypoints,
        ESOLRouteKind kind, int32 focusLeg, const FLegStyle& baseStyle, bool bDraft, int32 highlightedWaypoint,
        const FVector3d& planePointM);

    // Draws one leg between two universe points; returns true when any part of it was on screen
    bool DrawLeg(const FFrame& frame, const FVector3d& fromM, const FVector3d& toM, const FLegStyle& style);

    // Draws a waypoint ring (with a centre dot above the plane) and records it; false when off screen or over budget
    bool DrawWaypointMarker(const FFrame& frame, const FVector3d& pointM, double alpha, bool bBelowPlane,
        bool bHighlighted, int32 waypointIndex, bool bDraft, FVector2D& outScreenPx);

    // Projects a universe point to camera-local render space (cm, X forward)
    static FVector ToLocal(const FFrame& frame, const FVector3d& pointM);

    // Projects a camera-local point to the screen (caller guarantees X > 0)
    static FVector2D LocalToScreen(const FFrame& frame, const FVector& local);

    // Appends one glow piece (core plus soft halo) between two screen points with their intensities
    void AppendGlowPiece(const FVector2D& a, const FVector2D& b, const FLinearColor& color, double intensityA,
        double intensityB, float uiScale);

    // Appends a filled arrowhead at a screen point pointing along a unit direction
    void AppendArrow(const FVector2D& tip, const FVector2D& direction, const FLinearColor& color, float uiScale);

    // Appends a ring (annulus) between two radii
    void AppendRing(const FVector2D& center, double innerPx, double outerPx, const FLinearColor& color);

    // Appends a filled disc
    void AppendDisc(const FVector2D& center, double radiusPx, const FLinearColor& color);

    // Returns true when count more triangles fit under the current limit; marks the limit hit otherwise
    bool Reserve(int32 count);

    // Sets the triangle count the next drawing may fill up to (at most the batch's budget) and clears the limit flag
    void SetBudgetLimit(int32 limit);

    // Returns a waypoint ring's outer radius on screen (the current target's is larger)
    static double MarkerRadiusPx(bool bHighlighted, float uiScale);

    // Appends one triangle with per-vertex colors
    void AppendTriangle(const FVector2D& a, const FVector2D& b, const FVector2D& c, const FLinearColor& colorA,
        const FLinearColor& colorB, const FLinearColor& colorC);

    TUniquePtr<FCanvasTriangleItem> mBatch;         // One additive triangle batch for the whole overlay
    TArray<FVector2D> mUnitCircle;                  // Marker ring tessellation, built once
    TArray<FVector2D> mDashes;                      // Dash intervals of one leg, sized once
    TArray<FMarker> mMarkers;                       // Markers drawn this frame, reserved once
    FText mPausedText;                              // "PAUSED", built once
    int32 mLastLegsDrawn = 0;                       // Legs drawn last frame
    int32 mLastDashesDrawn = 0;                     // Closing-leg dashes drawn last frame
    int32 mLastArrowsDrawn = 0;                     // Arrowheads drawn last frame
    int32 mLastTriangles = 0;                       // Triangles in last frame's batch
    int32 mLegsDrawnThisRoute = 0;                  // Legs drawn of the route being drawn (per-route cap)
    bool mHasDrawnShipLeg = false;                  // Ship-to-target leg drawn last frame
    bool mHasDrawnCursorSegment = false;            // Draft cursor segment drawn last frame
    bool mHasDrawnPausedTag = false;                // PAUSED tag drawn last frame
    int32 mBudgetLimit = 0;                         // Triangles the current part (route or draft) may fill up to
    bool mIsLimitHit = false;                       // The current part hit its limit (stops its loops)
    bool mIsBudgetExhausted = false;                // A triangle limit was hit last frame
};
