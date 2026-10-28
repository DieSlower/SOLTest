/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapBodyOverlay.h"
#include "Map/SOLRoute.h"

#include "CoreMinimal.h"

class FCanvasTriangleItem;
class FSOLBodyRegistry;
class UCanvas;
class USOLAnchorSubsystem;

// What the hologram overlay draws this frame (filled by the HUD from the map and route subsystems)
struct FSOLHologramOverlayInputs
{
    FVector3d FocusM = FVector3d::ZeroVector;                   // Map camera focus: the grid plane runs through it
    double CameraDistanceM = 0.0;                               // Map camera distance to the focus (grid level)
    TConstArrayView<FSOLRouteWaypoint> RouteWaypoints;          // Active / paused / finished route (stems)
    TConstArrayView<FSOLRouteWaypoint> DraftWaypoints;          // Map draft (stems)
};

/**
 * Map hologram overlay (SDD 20 section 2.4, plan step 6, layer c), drawn on the HUD canvas under the body icons while
 * the map is open and sol.Hologram is on: the ecliptic grid plane through the camera focus in two scales
 * (SOLHologramMath::ComputeGridLevel; the fine lines fade out across a level, so a zoom never pops) with the LT
 * distance field fade around the focus (SOLHologramMath::GridFade), Primary at SOL::Hologram::GRID_ALPHA, and a
 * vertical stem from every body and route / draft waypoint to the plane with a small ring at its foot (Primary above
 * the plane, Secondary below, alpha SOLHologramMath::StemAlpha; stems shorter than STEM_MIN_HEIGHT_FRACTION of the
 * camera distance are skipped).
 *
 * Bounded: 2 x (2 x GRID_EXTENT_MAJOR_CELLS x GRID_SCALE_RATIO + 1) grid lines of GRID_PIECES_PER_LINE pieces each, at
 * most MAX_STEMS stems, one additive triangle batch of at most OVERLAY_MAX_TRIANGLES reserved once. Points are
 * projected like the route overlay (true offset from the render viewpoint; the far-field compression is radial from
 * it), each piece is clipped to the near plane and the screen.
 */
class SOLTEST_API FSOLMapHologramOverlay
{
public:

    // Declared here, defined in the .cpp where FCanvasTriangleItem is complete
    FSOLMapHologramOverlay();
    ~FSOLMapHologramOverlay();

    // Sizes the batch and builds the circle table once
    void Initialize();

    // Clears the last frame's counts for a frame that does not draw the overlay
    void ResetFrame();

    // Draws the grid and the stems for this frame
    void Draw(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLAnchorSubsystem& anchor,
        const FSOLBodyRegistry& registry, const FSOLHologramOverlayInputs& inputs, float uiScale);

    // Returns the number of grid lines with a visible piece last frame (verification)
    int32 GetLastGridLines() const { return mLastGridLines; }

    // Returns the number of stems drawn last frame (verification)
    int32 GetLastStems() const { return mLastStems; }

    // Returns the number of triangles in last frame's batch (verification)
    int32 GetLastTriangles() const { return mLastTriangles; }

    // Returns the grid's minor spacing last frame in meters, 0 when not drawn (verification)
    double GetLastMinorSpacingM() const { return mLastMinorSpacingM; }

private:

    // The frame's projection state
    struct FFrame
    {
        const FSOLMapOverlayView* View = nullptr;
        FVector2D RectMin = FVector2D::ZeroVector;
        FVector2D RectMax = FVector2D::ZeroVector;
        FVector3d ViewpointM = FVector3d::ZeroVector;
        FVector ViewpointOffsetCm = FVector::ZeroVector;
        float UiScale = 1.0f;
    };

    // Draws one grid line from a to b (universe meters) in pieces faded by their distance to the focus; true when any
    // piece was on screen
    bool DrawGridLine(const FFrame& frame, const FVector3d& fromM, const FVector3d& toM, const FVector3d& focusM,
        double majorSpacingM, double alpha);

    // Draws one stem from a point down (or up) to the plane; true when drawn
    bool DrawStem(const FFrame& frame, const FVector3d& pointM, const FSOLHologramOverlayInputs& inputs,
        double majorSpacingM);

    // Clips a universe segment to the near plane and the screen; false when nothing is visible
    static bool ProjectSegment(const FFrame& frame, const FVector3d& fromM, const FVector3d& toM, FVector2D& outA,
        FVector2D& outB);

    // Returns a universe point's true offset from the viewpoint in camera axes (X forward, cm)
    static FVector ToCameraLocal(const FFrame& frame, const FVector3d& pointM);

    // Clips a camera-space segment to the near plane and the screen; false when nothing is visible
    static bool ProjectLocalSegment(const FFrame& frame, const FVector& localA, const FVector& localB,
        FVector2D& outA, FVector2D& outB);

    // Appends a thin quad between two screen points with per-end colors
    void AppendLine(const FVector2D& a, const FVector2D& b, const FLinearColor& colorA, const FLinearColor& colorB,
        double halfWidthPx);

    // Appends a ring (annulus) between two radii
    void AppendRing(const FVector2D& center, double innerPx, double outerPx, const FLinearColor& color);

    // Appends one triangle with per-vertex colors
    void AppendTriangle(const FVector2D& a, const FVector2D& b, const FVector2D& c, const FLinearColor& colorA,
        const FLinearColor& colorB, const FLinearColor& colorC);

    // Returns true when count more triangles fit in the batch
    bool HasRoom(int32 count) const;

    TUniquePtr<FCanvasTriangleItem> mBatch;         // One additive triangle batch for grid and stems
    TArray<FVector2D> mUnitCircle;                  // Foot ring tessellation, built once
    double mLastMinorSpacingM = 0.0;                // Grid minor spacing last frame
    int32 mLastGridLines = 0;                       // Grid lines with a visible piece last frame
    int32 mLastStems = 0;                           // Stems drawn last frame
    int32 mLastTriangles = 0;                       // Triangles in last frame's batch
};
