/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudShapeBatch.h"

#include "SOLConstants.h"

namespace
{
    constexpr int32 HUDBATCH_VERTICES_PER_SHAPE = SOL::Hologram::HUD_VERTICES_PER_SHAPE;
    constexpr int32 HUDBATCH_INDICES_PER_SHAPE = SOL::Hologram::HUD_INDICES_PER_SHAPE;
    constexpr float HUDBATCH_MIN_LINE_LENGTH_PX = 1.0e-4f;     // Shorter lines use +X as their direction
    constexpr float HUDBATCH_ALPHA_MAX = 255.0f;

    //////////////////////////////////////////////////////////////////////////
    // Returns sRGB(colour x intensity) with the given alpha byte
    FColor HudBatchColor(const FLinearColor& color, const float intensity, const float alpha01)
    {
        FColor result = FLinearColor(color.R * intensity, color.G * intensity, color.B * intensity, 1.0f)
            .ToFColorSRGB();
        result.A = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(alpha01, 0.0f, 1.0f) * HUDBATCH_ALPHA_MAX));
        return result;
    }
}

//////////////////////////////////////////////////////////////////////////
// Reserves room for maxShapes quads (the only allocation) and empties the batch
void FSOLHudShapeBatch::Initialize(const int32 maxShapes)
{
    mMaxShapes = FMath::Max(maxShapes, 0);
    mVertices.Empty(mMaxShapes * HUDBATCH_VERTICES_PER_SHAPE);
    mIndices.Empty(mMaxShapes * HUDBATCH_INDICES_PER_SHAPE);
    mDroppedCount = 0;
}

//////////////////////////////////////////////////////////////////////////
// Empties the batch, keeping its capacity
void FSOLHudShapeBatch::Reset()
{
    mVertices.Reset();
    mIndices.Reset();
    mDroppedCount = 0;
}

//////////////////////////////////////////////////////////////////////////
// Adds a glow line from a to b (local X along a -> b, origin at the midpoint)
bool FSOLHudShapeBatch::AddLine(const FVector2f& a, const FVector2f& b, const float halfWidthPx,
    const FLinearColor& color, const float intensity, const float padPx)
{
    const FVector2f delta = b - a;
    const float length = delta.Size();
    const FVector2f dir = length > HUDBATCH_MIN_LINE_LENGTH_PX ? delta / length : FVector2f(1.0f, 0.0f);
    const float halfLength = length > HUDBATCH_MIN_LINE_LENGTH_PX ? 0.5f * length : 0.0f;
    return AddQuad((a + b) * 0.5f, dir, FVector2f(-dir.Y, dir.X),
        FVector2f(halfLength + halfWidthPx + padPx, halfWidthPx + padPx), halfLength,
        SOLHudShapeMath::EncodeKind(ESOLHudShapeKind::Line, halfWidthPx), HudBatchColor(color, intensity, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Adds an arc around a centre whose middle points at midAngleRad and spans +-halfApertureRad
bool FSOLHudShapeBatch::AddArc(const FVector2f& center, const float radiusPx, const float halfWidthPx,
    const float midAngleRad, const float halfApertureRad, const FLinearColor& color, const float intensity,
    const float padPx)
{
    float sinMid = 0.0f;
    float cosMid = 0.0f;
    FMath::SinCos(&sinMid, &cosMid, midAngleRad);
    const FVector2f axisY(cosMid, sinMid);
    const float extent = radiusPx + halfWidthPx + padPx;
    const float aperture01 = FMath::Clamp(halfApertureRad, 0.0f, UE_PI) / UE_PI;
    return AddQuad(center, FVector2f(axisY.Y, -axisY.X), axisY, FVector2f(extent, extent), radiusPx,
        SOLHudShapeMath::EncodeKind(ESOLHudShapeKind::Arc, halfWidthPx), HudBatchColor(color, intensity, aperture01));
}

//////////////////////////////////////////////////////////////////////////
// Adds a full ring (an arc with a half aperture of pi)
bool FSOLHudShapeBatch::AddRing(const FVector2f& center, const float radiusPx, const float halfWidthPx,
    const FLinearColor& color, const float intensity, const float padPx)
{
    return AddArc(center, radiusPx, halfWidthPx, 0.0f, UE_PI, color, intensity, padPx);
}

//////////////////////////////////////////////////////////////////////////
// Adds a dashed glow line from a to b (dashes start at a)
bool FSOLHudShapeBatch::AddDashedLine(const FVector2f& a, const FVector2f& b, const float halfWidthPx,
    const float periodPx, const FLinearColor& color, const float intensity, const float padPx)
{
    const FVector2f delta = b - a;
    const float length = delta.Size();
    const FVector2f dir = length > HUDBATCH_MIN_LINE_LENGTH_PX ? delta / length : FVector2f(1.0f, 0.0f);
    const float halfLength = length > HUDBATCH_MIN_LINE_LENGTH_PX ? 0.5f * length : 0.0f;
    const float period = FMath::RoundToFloat(FMath::Clamp(periodPx, 1.0f, SOL::Hologram::HUD_DASH_MAX_PERIOD_PX));
    return AddQuad((a + b) * 0.5f, dir, FVector2f(-dir.Y, dir.X),
        FVector2f(halfLength + halfWidthPx + padPx, halfWidthPx + padPx), halfLength,
        SOLHudShapeMath::EncodeKind(ESOLHudShapeKind::DashedLine, halfWidthPx),
        HudBatchColor(color, intensity, period / HUDBATCH_ALPHA_MAX));
}

//////////////////////////////////////////////////////////////////////////
// Adds an axis-aligned ellipse ring (radius Y clamped to [0, radius X])
bool FSOLHudShapeBatch::AddEllipse(const FVector2f& center, const float radiusXPx, const float radiusYPx,
    const float halfWidthPx, const FLinearColor& color, const float intensity, const float padPx)
{
    const float radiusY = FMath::Clamp(radiusYPx, 0.0f, FMath::Max(radiusXPx, 0.0f));
    const float ratio = radiusXPx > 0.0f ? radiusY / radiusXPx : 1.0f;
    return AddQuad(center, FVector2f(1.0f, 0.0f), FVector2f(0.0f, 1.0f),
        FVector2f(radiusXPx + halfWidthPx + padPx, radiusY + halfWidthPx + padPx), radiusXPx,
        SOLHudShapeMath::EncodeKind(ESOLHudShapeKind::Ellipse, halfWidthPx), HudBatchColor(color, intensity, ratio));
}

//////////////////////////////////////////////////////////////////////////
// Adds an axis-aligned hologram panel: a fill and a glowing outline
bool FSOLHudShapeBatch::AddPanel(const FVector2f& center, const FVector2f& halfSizePx, const FLinearColor& color,
    const float edgeIntensity, const float fill, const float padPx)
{
    return AddQuad(center, FVector2f(1.0f, 0.0f), FVector2f(0.0f, 1.0f),
        FVector2f(halfSizePx.X + padPx, halfSizePx.Y + padPx), halfSizePx.X,
        SOLHudShapeMath::EncodeKind(ESOLHudShapeKind::Panel, halfSizePx.Y), HudBatchColor(color, edgeIntensity, fill));
}

//////////////////////////////////////////////////////////////////////////
// Appends one quad given its local-frame half extents, frame axes and the vertex parameters; false if full
bool FSOLHudShapeBatch::AddQuad(const FVector2f& origin, const FVector2f& axisX, const FVector2f& axisY,
    const FVector2f& halfExtent, const float size1, const float encodedKind, const FColor& color)
{
    if (GetShapeCount() >= mMaxShapes)
    {
        ++mDroppedCount;
        return false;
    }

    // Corners counter-clockwise from (-x, -y) in the local frame; triangles 0-1-2 and 0-2-3
    const FVector2f corners[HUDBATCH_VERTICES_PER_SHAPE] =
    {
        FVector2f(-halfExtent.X, -halfExtent.Y),
        FVector2f(halfExtent.X, -halfExtent.Y),
        FVector2f(halfExtent.X, halfExtent.Y),
        FVector2f(-halfExtent.X, halfExtent.Y),
    };
    const SlateIndex base = static_cast<SlateIndex>(mVertices.Num());
    for (const FVector2f& local : corners)
    {
        FSlateVertex& vertex = mVertices.AddDefaulted_GetRef();
        vertex.Position = origin + axisX * local.X + axisY * local.Y;
        vertex.TexCoords[0] = local.X;
        vertex.TexCoords[1] = local.Y;
        vertex.TexCoords[2] = size1;
        vertex.TexCoords[3] = encodedKind;
        vertex.MaterialTexCoords = FVector2f::ZeroVector;
        vertex.Color = color;
        vertex.SecondaryColor = FColor::Transparent;
        vertex.PixelSize[0] = 0;
        vertex.PixelSize[1] = 0;
    }
    const SlateIndex quadIndices[HUDBATCH_INDICES_PER_SHAPE] = { 0, 1, 2, 0, 2, 3 };
    for (const SlateIndex index : quadIndices)
    {
        mIndices.Add(base + index);
    }
    return true;
}
