/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapBodyLod.h"

#include "CoreMinimal.h"

class FCanvasTriangleItem;
class FSOLBodyRegistry;
class UCanvas;
class UFont;
class USOLAnchorSubsystem;
class USOLMapModeSubsystem;

// The camera the overlay projects with: render-space location and rotation, screen center and focal length in pixels
struct FSOLMapOverlayView
{
    FVector LocationCm = FVector::ZeroVector;
    FQuat Rotation = FQuat::Identity;
    FVector2D Center = FVector2D::ZeroVector;
    double FocalPx = 1.0;
};

/**
 * Jump-map body icons and name labels (SDD 3, sub-part 2c), drawn on the HUD canvas while the map is open. Every frame
 * each body's apparent size from the map camera goes through SOLMapBodyLod::Evaluate: at IconAlpha 0 nothing is drawn
 * (the real mesh shows as in normal play); above 0 a camera-facing disc in the body's own material color is drawn over
 * the mesh with that alpha, never smaller than the mesh, so the mesh fades out under a solid disc as it shrinks
 * instead of popping; a thin dark ring just outside the disc keeps it readable without darkening the blend. An icon
 * whose center lies inside a nearer body's apparent disc is hidden (that body's mesh or icon wins), and the icons are
 * drawn back to front. Every visible body gets its name, placed beside the disc (right, left, above or below, the
 * first spot free of other discs and labels, bigger bodies first) or skipped when none is free; icons that are nearly
 * transparent neither get a label nor block label space. It also keeps each body's map pick radius for the frame
 * (SOLMapPickRadius) for destination picking. All buffers are sized once; a frame only resets and refills them
 * (Canvas's own text layout is the only allocation left).
 */
class SOLTEST_API FSOLMapBodyOverlay
{
public:

    // Declared here, defined in the .cpp where FCanvasTriangleItem is complete
    FSOLMapBodyOverlay();
    ~FSOLMapBodyOverlay();

    // Caches each body's icon color, label text and label priority, and sizes the per-frame buffers
    void Initialize(const FSOLBodyRegistry& registry);

    // Evaluates every body's LOD from the map camera and draws its icon and label on the canvas
    void Draw(UCanvas& canvas, const FSOLMapOverlayView& view, const USOLMapModeSubsystem& mapMode,
        const USOLAnchorSubsystem& anchor, const FSOLBodyRegistry& registry, UFont* font,
        const FLinearColor& labelColor, float uiScale);

    // Returns each body's icon alpha of the last frame, indexed like the registry (verification)
    TConstArrayView<double> GetIconAlphas() const { return mIconAlphas; }

    // Returns each body's map pick radius (meters) of the last frame, indexed like the registry
    TConstArrayView<double> GetPickRadiiM() const { return mPickRadiiM; }

    // Returns each body's drawn disc radius (pixels) of the last frame, 0 when not drawn (verification)
    TConstArrayView<double> GetIconRadiiPx() const { return mIconRadiusPx; }

    // Returns the number of icons drawn in the last frame (verification)
    int32 GetLastIconsDrawn() const { return mLastIconsDrawn; }

    // Returns the number of labels drawn in the last frame (verification)
    int32 GetLastLabelsDrawn() const { return mLastLabelsDrawn; }

private:

    // Returns true when a body's screen center lies inside the apparent disc of a nearer projected body
    bool IsOccluded(int32 index, int32 bodyCount) const;

    // Appends a filled disc to the icon triangle batch
    void AppendDisc(const FVector2D& center, double radiusPx, const FLinearColor& color);

    // Appends a ring (annulus) between two radii to the icon triangle batch
    void AppendRing(const FVector2D& center, double innerRadiusPx, double outerRadiusPx, const FLinearColor& color);

    // Returns true when a rectangle overlaps none of the occupied rectangles
    bool IsFree(const FBox2D& rect) const;

    TUniquePtr<FCanvasTriangleItem> mIconBatch;     // One translucent triangle batch for all icon discs
    TArray<FVector2D> mUnitCircle;                  // Unit-circle points of the disc tessellation, built once
    TArray<FLinearColor> mIconColors;               // Per body: icon color (base color at full brightness)
    TArray<FString> mLabelSources;                  // Per body: name, for measuring
    TArray<FText> mLabels;                          // Per body: name, built once
    TArray<FVector2D> mLabelSizesPx;                // Per body: label size at scale 1 (measured on the first draw)
    TArray<int32> mLabelOrder;                      // Body indices by descending radius (label priority)
    TArray<double> mIconAlphas;                     // Per body: icon alpha of the frame
    TArray<double> mPickRadiiM;                     // Per body: pick radius of the frame
    TArray<double> mDistancesM;                     // Per body: distance from the map camera of the frame
    TArray<FVector2D> mScreenPx;                    // Per body: screen position of the frame
    TArray<double> mApparentRadiusPx;               // Per body: apparent (mesh) radius on screen, 0 when not projected
    TArray<double> mIconRadiusPx;                   // Per body: drawn disc radius, 0 when not drawn
    TArray<int32> mDrawOrder;                       // Bodies with an icon this frame, sorted back to front
    TArray<FBox2D> mOccupied;                       // Discs and labels placed this frame
    FSOLBodyLodParams mLodParams;                   // Thresholds (defaults) and the frame's projection inputs
    int32 mLastIconsDrawn = 0;                      // Icons drawn last frame
    int32 mLastLabelsDrawn = 0;                     // Labels drawn last frame
    bool mHasLabelSizes = false;                    // True once the label sizes were measured
};
