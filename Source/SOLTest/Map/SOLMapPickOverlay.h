/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapBodyOverlay.h"

#include "CoreMinimal.h"

class FCanvasTriangleItem;
class UCanvas;
class USOLAnchorSubsystem;
class USOLMapModeSubsystem;

/**
 * Jump-map destination-pick visuals (SDD 3 section 3, sub-part 2d), drawn on the HUD canvas while the map is open,
 * over the body icons. From USOLMapModeSubsystem's pick state it draws, in cyan: a ring around the reference (the
 * picked body or the ship); while the left button drags, a translucent disc in the ecliptic plane through the
 * reference, centered on it with the drag distance as radius, plus the radius line to the XY point ("direction and
 * range"); after the planar lock the disc stays as a dim outline; during the Shift height preview (and after its lock)
 * a vertical guide line from the locked XY point to the height, with a tick at its tip. The destination marker is a
 * magenta diamond with four crosshair arms (unlike any round body icon), dim after the planar lock and bright once the
 * height is locked too. Every point is projected through the same render placement as the bodies, so it lines up
 * with them at any zoom. Buffers are sized once at Initialize; a frame only resets and refills them.
 */
class SOLTEST_API FSOLMapPickOverlay
{
public:

    // Declared here, defined in the .cpp where FCanvasTriangleItem is complete
    FSOLMapPickOverlay();
    ~FSOLMapPickOverlay();

    // Builds the disc tessellation table and sizes the per-frame buffers
    void Initialize();

    // Draws the reference ring, the disc, the guide line and the destination marker for the current pick
    void Draw(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLMapModeSubsystem& mapMode,
        const USOLAnchorSubsystem& anchor, float uiScale);

    // Returns the number of disc rim segments drawn in the last frame (verification)
    int32 GetLastDiscSegmentsDrawn() const { return mLastDiscSegmentsDrawn; }

    // Returns true when the height guide line was drawn in the last frame (verification)
    bool HasDrawnGuide() const { return mHasDrawnGuide; }

    // Returns true when the destination marker was drawn in the last frame (verification)
    bool HasDrawnMarker() const { return mHasDrawnMarker; }

    // Returns true when the destination marker of the last frame was the bright (both locks) one (verification)
    bool HasDrawnFinalMarker() const { return mHasDrawnFinalMarker; }

private:

    // Projects a universe point (meters) to the screen through the bodies' render placement; false when behind the
    // camera or absurdly far off-screen
    static bool ProjectPoint(const FSOLMapOverlayView& view, const USOLAnchorSubsystem& anchor,
        const FVector3d& pointM, FVector2D& outScreen);

    // Draws one screen-space line
    static void DrawLine(UCanvas& canvas, const FVector2D& a, const FVector2D& b, const FLinearColor& color,
        float thickness);

    // Draws the disc of the planar offset around the reference: rim outline, plus the translucent fill when filled
    void DrawDisc(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLAnchorSubsystem& anchor,
        const FVector3d& referenceM, const FVector2D& referenceScreen, bool bHasReferenceScreen, double radiusM,
        bool bFilled, const FLinearColor& outlineColor, float rimThickness);

    // Draws a screen-space circle outline from the unit-circle table
    void DrawScreenCircle(UCanvas& canvas, const FVector2D& center, double radiusPx, const FLinearColor& color,
        float thickness) const;

    // Draws the destination marker: a diamond with four crosshair arms
    static void DrawMarker(UCanvas& canvas, const FVector2D& center, double halfPx, const FLinearColor& color,
        float thickness);

    TUniquePtr<FCanvasTriangleItem> mDiscBatch;     // Translucent triangle fan of the disc fill
    TArray<FVector2D> mUnitCircle;                  // Unit-circle points of the disc tessellation, built once
    TArray<FVector2D> mRimPx;                       // Disc rim points on screen this frame
    TArray<bool> mRimValid;                         // Per rim point: projected this frame
    int32 mLastDiscSegmentsDrawn = 0;               // Disc rim segments drawn last frame
    bool mHasDrawnGuide = false;                    // Height guide line drawn last frame
    bool mHasDrawnMarker = false;                   // Destination marker drawn last frame
    bool mHasDrawnFinalMarker = false;              // The marker drawn last frame was the both-locks one
};
