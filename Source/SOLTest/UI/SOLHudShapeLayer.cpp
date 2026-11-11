/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudShapeLayer.h"

#include "SOLConstants.h"
#include "UI/SOLHudShapeMath.h"

#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

namespace
{
    // A text line's paint geometry size in its own scaled units (text is not clipped to it; any size that holds a
    // HUD line works)
    const FVector2f HUDLAYER_TEXT_LOCAL_SIZE(4096.0f, 256.0f);
    constexpr int32 HUDLAYER_LAYERS_USED = 4;          // Backings, shapes, text shadows, text
}

//////////////////////////////////////////////////////////////////////////
// Reserves the batch, text, backing and paint buffers once
void SSOLHudShapeLayer::Construct(const FArguments& args)
{
    SetVisibility(EVisibility::HitTestInvisible);

    // Its content changes every frame without invalidating: volatile, so an invalidation panel never caches it
    ForceVolatile(true);
    const int32 maxShapes = FMath::Max(args._MaxShapes, 0);
    mBatch.Initialize(maxShapes);
    mTexts.Initialize(FMath::Max(args._MaxTexts, 0));
    mMaxBackings = FMath::Max(args._MaxBackings, 0);
    mBackings.Reserve(mMaxBackings);
    mPaintScratch = args._PaintScratch;
    if (mPaintScratch == nullptr)
    {
        mPaintVertices.Reserve(maxShapes * SOL::Hologram::HUD_VERTICES_PER_SHAPE);
    }

    InitBackingBrush(mBackingBrush);
}

//////////////////////////////////////////////////////////////////////////
// Sets a brush up as a white rounded box (no outline) tinted per backing layer
void SSOLHudShapeLayer::InitBackingBrush(FSlateBrush& brush)
{
    brush = FSlateBrush();
    brush.DrawAs = ESlateBrushDrawType::RoundedBox;
    brush.TintColor = FSlateColor(FLinearColor::White);
    brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
    brush.OutlineSettings.Width = 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Paints one soft dark backing: a stack of rounded boxes inset step by step, so it fades in from its edge
void SSOLHudShapeLayer::PaintBacking(const FGeometry& allottedGeometry, FSlateWindowElementList& outDrawElements,
    const int32 layerId, FSlateBrush& backingBrush, const FBox2f& rect, const float cornerPx, const float featherPx,
    const float opacity, const FLinearColor& color, const float inverseScale)
{
    using namespace SOL::Hologram;
    for (int32 layer = 0; layer < HUD_PANEL_BACKING_LAYERS; ++layer)
    {
        FBox2f layerRect;
        float corner = 0.0f;
        float alpha = 0.0f;
        if (!SOLHudShapeMath::BackingLayer(rect, cornerPx, featherPx, HUD_PANEL_BACKING_COLOR.A * opacity, layer,
            HUD_PANEL_BACKING_LAYERS, layerRect, corner, alpha))
        {
            break;
        }
        const float localCorner = corner * inverseScale;
        backingBrush.OutlineSettings.CornerRadii = FVector4(localCorner, localCorner, localCorner, localCorner);
        FSlateDrawElement::MakeBox(outDrawElements, layerId, allottedGeometry.ToPaintGeometry(layerRect.GetSize()
            * inverseScale, FSlateLayoutTransform(layerRect.Min * inverseScale)), &backingBrush,
            ESlateDrawEffect::None, FLinearColor(color.R, color.G, color.B, alpha));
    }
}

//////////////////////////////////////////////////////////////////////////
// Paints a batch as one custom-vertex element: pixels -> widget local units -> window space (the accumulated
// transform includes the DPI scale); true when drawn
bool SSOLHudShapeLayer::PaintBatch(const FGeometry& allottedGeometry, FSlateWindowElementList& outDrawElements,
    const int32 layerId, const FSOLHudShapeBatch& batch, const FSlateBrush& brush,
    FSlateResourceHandle& resourceHandle, TArray<FSlateVertex>& paintVertices, const float inverseScale)
{
    const TArray<FSlateVertex>& vertices = batch.GetVertices();
    if (vertices.IsEmpty() || brush.GetResourceObject() == nullptr)
    {
        return false;
    }
    if (!resourceHandle.IsValid())
    {
        resourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(brush);
    }
    if (!resourceHandle.IsValid())
    {
        return false;
    }
    const FSlateRenderTransform& renderTransform = allottedGeometry.GetAccumulatedRenderTransform();
    paintVertices.Reset();
    paintVertices.Append(vertices);
    for (FSlateVertex& vertex : paintVertices)
    {
        vertex.Position = renderTransform.TransformPoint(vertex.Position * inverseScale);
    }
    FSlateDrawElement::MakeCustomVerts(outDrawElements, layerId, resourceHandle, paintVertices, batch.GetIndices(),
        nullptr, 0, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the material the shapes are drawn with
void SSOLHudShapeLayer::SetMaterial(UMaterialInterface* material)
{
    mBrush = FSlateBrush();
    mBrush.SetResourceObject(material);
    mBrush.ImageSize = FVector2D(1.0, 1.0);
    mResourceHandle = FSlateResourceHandle();
}

//////////////////////////////////////////////////////////////////////////
// Empties the batch, the text lines and the panel backings for a new frame (clearing any glow override left set)
void SSOLHudShapeLayer::ResetFrame()
{
    mBatch.Reset();
    mTexts.Reset();
    mBackings.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Sets retain mode: a retained picture may be cached by an invalidation root (not volatile), the HUD's is volatile
void SSOLHudShapeLayer::SetRetainBatch(const bool bRetain)
{
    mIsRetainBatch = bRetain;
    ForceVolatile(!bRetain);
}

//////////////////////////////////////////////////////////////////////////
// Retain mode: the owner refilled the batch, so the widget's cached paint is invalidated
void SSOLHudShapeLayer::MarkBatchChanged()
{
    Invalidate(EInvalidateWidgetReason::Paint);
}

//////////////////////////////////////////////////////////////////////////
// Adds a soft dark backing under a hologram panel (its darkness scaled by opacity, in a colour's RGB); false when the
// buffer is full
bool SSOLHudShapeLayer::AddBacking(const FBox2f& rect, const float cornerPx, const float featherPx, const float opacity,
    const FLinearColor& color)
{
    if (!rect.bIsValid || mBackings.Num() >= mMaxBackings)
    {
        return false;
    }
    mBackings.Add({ rect, cornerPx, featherPx, FMath::Clamp(opacity, 0.0f, 1.0f), color });
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the glow text's outline alpha (clamped, NaN -> the default); a retained picture repaints when it changes
void SSOLHudShapeLayer::SetTextGlowAlpha(const float alpha)
{
    const float sanitized = FMath::IsNaN(alpha) ? SOL::Hologram::HUD_TEXT_GLOW_ALPHA : FMath::Clamp(alpha, 0.0f, 1.0f);
    if (sanitized == mTextGlowAlpha)
    {
        return;
    }
    mTextGlowAlpha = sanitized;
    if (mIsRetainBatch)
    {
        MarkBatchChanged();
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the font a text line of a given ESOLHudFont is drawn with
void SSOLHudShapeLayer::SetFont(const ESOLHudFont font, const FSlateFontInfo& fontInfo)
{
    const int32 index = static_cast<int32>(font);
    if (index >= 0 && index < static_cast<int32>(ESOLHudFont::Count))
    {
        mFonts[index] = fontInfo;
    }
}

//////////////////////////////////////////////////////////////////////////
// Draws the backings, the batch as one custom-vertex element and the text lines
int32 SSOLHudShapeLayer::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& /*widgetStyle*/, const bool /*bParentEnabled*/) const
{
    mLastPaintedShapeCount = 0;
    mLastPaintedTextCount = 0;
    mLastPaintedBackingCount = 0;

    // Only what the HUD filled this frame or the last (Slate may paint before or after DrawHUD in a frame); a retained
    // picture every paint
    if (!ShouldPaintBatch(mIsRetainBatch, mBatchFrame, GFrameCounter) || !FSlateApplication::IsInitialized())
    {
        return layerId;
    }
    const float inverseScale = allottedGeometry.Scale > 0.0f ? 1.0f / allottedGeometry.Scale : 1.0f;
    mLastPaintOrigin = allottedGeometry.GetAccumulatedRenderTransform().TransformPoint(FVector2f::ZeroVector);

    // Dark backings under the panels (alpha-blended), so text stays readable over a bright planet: each is a stack of
    // rounded boxes inset step by step, so it fades in from its edge and never shows a box outline (issue #31)
    for (const FSOLHudBacking& backing : mBackings)
    {
        PaintBacking(allottedGeometry, outDrawElements, layerId, mBackingBrush, backing.Rect, backing.CornerPx,
            backing.FeatherPx, backing.Opacity, backing.Color, inverseScale);
    }
    mLastPaintedBackingCount = mBackings.Num();

    // Shapes: canvas pixels -> widget local units -> window space, one custom-vertex element
    TArray<FSlateVertex>& paintVertices = mPaintScratch != nullptr ? *mPaintScratch : mPaintVertices;
    if (PaintBatch(allottedGeometry, outDrawElements, layerId + 1, mBatch, mBrush, mResourceHandle, paintVertices,
        inverseScale))
    {
        mLastPaintedShapeCount = mBatch.GetShapeCount();
    }
    PaintTexts(allottedGeometry, outDrawElements, layerId + 2, inverseScale);
    return layerId + HUDLAYER_LAYERS_USED;
}

//////////////////////////////////////////////////////////////////////////
// Draws the text lines (drop shadow, then the text) on two layers above the shapes
void SSOLHudShapeLayer::PaintTexts(const FGeometry& allottedGeometry, FSlateWindowElementList& outDrawElements,
    const int32 layerId, const float inverseScale) const
{
    using namespace SOL::Hologram;
    const FLinearColor shadowColor(0.0f, 0.0f, 0.0f, HUD_TEXT_SHADOW_ALPHA);
    for (const FSOLHudTextItem& item : mTexts.GetItems())
    {
        const FSlateFontInfo& baseFont = mFonts[static_cast<int32>(item.Font)];
        if (baseFont.FontObject == nullptr && !baseFont.CompositeFont.IsValid())
        {
            continue;
        }
        ++mLastPaintedTextCount;

        // The line's font units -> canvas pixels (item scale) -> widget local units
        const float localScale = item.Scale * inverseScale;
        const FVector2f origin = item.Position * inverseScale;
        const FVector2f shadowOffset(HUD_TEXT_SHADOW_PX * FMath::Max(item.Scale, 1.0f) * inverseScale);
        FSlateDrawElement::MakeText(outDrawElements, layerId, allottedGeometry.ToPaintGeometry(
            HUDLAYER_TEXT_LOCAL_SIZE, FSlateLayoutTransform(localScale, origin + shadowOffset)), item.Text, baseFont,
            ESlateDrawEffect::None, FLinearColor(shadowColor.R, shadowColor.G, shadowColor.B,
                shadowColor.A * item.Color.A));

        // Glow style: a hot core (the colour toward white) inside an outline in the colour itself (a copy of the font
        // info: reference counts only, no allocation)
        const FPaintGeometry textGeometry = allottedGeometry.ToPaintGeometry(HUDLAYER_TEXT_LOCAL_SIZE,
            FSlateLayoutTransform(localScale, origin));
        if (!mIsTextGlow)
        {
            FSlateDrawElement::MakeText(outDrawElements, layerId + 1, textGeometry, item.Text, baseFont,
                ESlateDrawEffect::None, item.Color);
            continue;
        }
        // (the line's own outline alpha if it has one, e.g. a card readout, else the layer's; 0, HUD glow 0%, drops
        // the outline: SDD 37 D9 and section 4.6)
        const float glowAlpha = item.GlowAlpha >= 0.0f ? item.GlowAlpha : mTextGlowAlpha;
        FSlateFontInfo glowFont = baseFont;
        glowFont.OutlineSettings.OutlineSize = glowAlpha > 0.0f ? HUD_TEXT_GLOW_OUTLINE : 0;
        glowFont.OutlineSettings.OutlineColor = FLinearColor(item.Color.R, item.Color.G, item.Color.B, glowAlpha);
        FLinearColor core = FMath::Lerp(item.Color, FLinearColor::White, HUD_TEXT_CORE_WHITEN);
        core.A = item.Color.A;
        FSlateDrawElement::MakeText(outDrawElements, layerId + 1, textGeometry, item.Text, glowFont,
            ESlateDrawEffect::None, core);
    }
}

//////////////////////////////////////////////////////////////////////////
// Takes no layout space
FVector2D SSOLHudShapeLayer::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    return FVector2D::ZeroVector;
}
