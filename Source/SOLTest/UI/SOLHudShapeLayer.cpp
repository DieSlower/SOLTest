/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudShapeLayer.h"

#include "SOLConstants.h"

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
void SSOLHudShapeLayer::Construct(const FArguments& /*args*/)
{
    SetVisibility(EVisibility::HitTestInvisible);

    // Its content changes every frame without invalidating: volatile, so an invalidation panel never caches it
    ForceVolatile(true);
    mBatch.Initialize(SOL::Hologram::HUD_MAX_SHAPES);
    mTexts.Initialize(SOL::Hologram::HUD_MAX_TEXTS);
    mBackings.Reserve(SOL::Hologram::HUD_MAX_PANELS);
    mPaintVertices.Reserve(SOL::Hologram::HUD_MAX_SHAPES * SOL::Hologram::HUD_VERTICES_PER_SHAPE);

    // A solid white box tinted per backing
    mBackingBrush = FSlateBrush();
    mBackingBrush.DrawAs = ESlateBrushDrawType::Box;
    mBackingBrush.TintColor = FSlateColor(FLinearColor::White);
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
// Empties the batch, the text lines and the panel backings for a new frame
void SSOLHudShapeLayer::ResetFrame()
{
    mBatch.Reset();
    mTexts.Reset();
    mBackings.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Adds a dark translucent backing under a hologram panel; false when the backing buffer is full
bool SSOLHudShapeLayer::AddBacking(const FBox2f& rect)
{
    if (!rect.bIsValid || mBackings.Num() >= SOL::Hologram::HUD_MAX_PANELS)
    {
        return false;
    }
    mBackings.Add(rect);
    return true;
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

    // Only what the HUD filled this frame or the last (Slate may paint before or after DrawHUD in a frame)
    if (GFrameCounter > mBatchFrame + 1 || !FSlateApplication::IsInitialized())
    {
        return layerId;
    }
    const float inverseScale = allottedGeometry.Scale > 0.0f ? 1.0f / allottedGeometry.Scale : 1.0f;

    // Dark backings under the panels (alpha-blended), so text stays readable over a bright planet
    for (const FBox2f& rect : mBackings)
    {
        FSlateDrawElement::MakeBox(outDrawElements, layerId, allottedGeometry.ToPaintGeometry(rect.GetSize()
            * inverseScale, FSlateLayoutTransform(rect.Min * inverseScale)), &mBackingBrush, ESlateDrawEffect::None,
            SOL::Hologram::HUD_PANEL_BACKING_COLOR);
    }
    mLastPaintedBackingCount = mBackings.Num();

    // Shapes: canvas pixels -> widget local units -> window space (the accumulated transform includes the DPI scale)
    const TArray<FSlateVertex>& vertices = mBatch.GetVertices();
    if (!vertices.IsEmpty() && mBrush.GetResourceObject() != nullptr)
    {
        if (!mResourceHandle.IsValid())
        {
            mResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(mBrush);
        }
        if (mResourceHandle.IsValid())
        {
            const FSlateRenderTransform& renderTransform = allottedGeometry.GetAccumulatedRenderTransform();
            mPaintVertices.Reset();
            mPaintVertices.Append(vertices);
            for (FSlateVertex& vertex : mPaintVertices)
            {
                vertex.Position = renderTransform.TransformPoint(vertex.Position * inverseScale);
            }
            FSlateDrawElement::MakeCustomVerts(outDrawElements, layerId + 1, mResourceHandle, mPaintVertices,
                mBatch.GetIndices(), nullptr, 0, 0);
            mLastPaintedShapeCount = mBatch.GetShapeCount();
        }
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
        FSlateFontInfo glowFont = baseFont;
        glowFont.OutlineSettings.OutlineSize = HUD_TEXT_GLOW_OUTLINE;
        glowFont.OutlineSettings.OutlineColor = FLinearColor(item.Color.R, item.Color.G, item.Color.B,
            HUD_TEXT_GLOW_ALPHA);
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
