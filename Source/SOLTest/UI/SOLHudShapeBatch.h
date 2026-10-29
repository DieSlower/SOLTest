/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"
#include "UI/SOLHudShapeMath.h"

#include "CoreMinimal.h"
#include "Rendering/RenderingCommon.h"

/**
 * One frame's HUD shapes as Slate custom vertices (SDD 21 phase A): every shape is one quad (4 vertices, 6 indices,
 * triangles 0-1-2 and 0-2-3 of the quad) in canvas pixel space, drawn by SSOLHudShapeLayer with M_SOLHudShape. Per
 * vertex: TexCoords[0..1] = the pixel position in the shape's local frame, TexCoords[2] = the first size parameter,
 * TexCoords[3] = SOLHudShapeMath::EncodeKind(kind, second size parameter), Color = sRGB(colour x intensity) with alpha
 * = the shape's extra parameter (line: 255, arc: half aperture / pi, panel: fill; 8 bits, so an arc's ends sit within
 * pi / 510 rad of the asked aperture, about 3 px at a 500 px radius). Buffers are reserved once by
 * Initialize; a shape that would exceed the capacity is dropped (counted), never reallocated.
 */
struct SOLTEST_API FSOLHudShapeBatch
{
    // Reserves room for maxShapes quads (the only allocation) and empties the batch
    void Initialize(int32 maxShapes);

    // Empties the batch, keeping its capacity
    void Reset();

    // Adds a glow line from a to b (local X along a -> b, origin at the midpoint); false if dropped
    bool AddLine(const FVector2f& a, const FVector2f& b, float halfWidthPx, const FLinearColor& color, float intensity,
        float padPx);

    // Adds an arc around a centre whose middle points at midAngleRad (screen angle, +X toward +Y = down) and spans
    // +-halfApertureRad (clamped to [0, pi]); false if dropped
    bool AddArc(const FVector2f& center, float radiusPx, float halfWidthPx, float midAngleRad, float halfApertureRad,
        const FLinearColor& color, float intensity, float padPx);

    // Adds a full ring (an arc with a half aperture of pi); false if dropped
    bool AddRing(const FVector2f& center, float radiusPx, float halfWidthPx, const FLinearColor& color, float intensity,
        float padPx);

    // Adds a dashed glow line from a to b (local X along a -> b, origin at the midpoint, dashes starting at a):
    // size 1 = half length, kind param = halfWidthPx, alpha = round(periodPx clamped to [1, HUD_DASH_MAX_PERIOD_PX])
    // / 255 (the period travels as whole pixels), quad as AddLine's; false if dropped
    bool AddDashedLine(const FVector2f& a, const FVector2f& b, float halfWidthPx, float periodPx,
        const FLinearColor& color, float intensity, float padPx);

    // Adds an axis-aligned ellipse ring (radiusYPx clamped to [0, radiusXPx]): local X and Y along screen X and Y,
    // size 1 = radiusXPx, kind param = halfWidthPx, alpha = radiusYPx / radiusXPx (8 bits, so the GPU's ratio is within
    // 1/510 of it, e.g. 0.5 draws as 128/255; 1 for radiusXPx <= 0), quad
    // half extents (radiusX + halfWidth + pad, radiusY + halfWidth + pad); false if dropped
    bool AddEllipse(const FVector2f& center, float radiusXPx, float radiusYPx, float halfWidthPx,
        const FLinearColor& color, float intensity, float padPx);

    // Adds an axis-aligned borderless hologram panel: a fill (0..1) in the colour x intensity that fades in over
    // HUD_PANEL_FEATHER_PX from its edge (issue #31); false if dropped
    bool AddPanel(const FVector2f& center, const FVector2f& halfSizePx, const FLinearColor& color, float intensity,
        float fill, float padPx);

    // Returns the vertices in canvas pixel space
    const TArray<FSlateVertex>& GetVertices() const { return mVertices; }

    // Returns the triangle indices
    const TArray<SlateIndex>& GetIndices() const { return mIndices; }

    // Returns the number of shapes added since the last Reset
    int32 GetShapeCount() const { return mVertices.Num() / SOL::Hologram::HUD_VERTICES_PER_SHAPE; }

    // Returns the capacity set by Initialize
    int32 GetMaxShapes() const { return mMaxShapes; }

    // Returns the number of shapes dropped since the last Reset because the batch was full
    int32 GetDroppedCount() const { return mDroppedCount; }

private:

    // Appends one quad given its local-frame half extents, frame axes and the vertex parameters; false if full
    bool AddQuad(const FVector2f& origin, const FVector2f& axisX, const FVector2f& axisY, const FVector2f& halfExtent,
        float size1, float encodedKind, const FColor& color);

    TArray<FSlateVertex> mVertices;     // 4 per shape, canvas pixels
    TArray<SlateIndex> mIndices;        // 6 per shape
    int32 mMaxShapes = 0;               // Capacity reserved by Initialize
    int32 mDroppedCount = 0;            // Shapes dropped this frame (batch full)
};
