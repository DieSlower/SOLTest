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

//////////////////////////////////////////////////////////////////////////
// Reserves the batch and paint buffers once
void SSOLHudShapeLayer::Construct(const FArguments& /*args*/)
{
    SetVisibility(EVisibility::HitTestInvisible);

    // Its content changes every frame without invalidating: volatile, so an invalidation panel never caches it
    ForceVolatile(true);
    mBatch.Initialize(SOL::Hologram::HUD_MAX_SHAPES);
    mPaintVertices.Reserve(SOL::Hologram::HUD_MAX_SHAPES * 4);
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
// Draws the batch as one custom-vertex element
int32 SSOLHudShapeLayer::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& /*widgetStyle*/, const bool /*bParentEnabled*/) const
{
    mLastPaintedShapeCount = 0;

    // Only a batch the HUD filled this frame or the last (Slate may paint before or after DrawHUD in a frame)
    const TArray<FSlateVertex>& vertices = mBatch.GetVertices();
    if (vertices.IsEmpty() || GFrameCounter > mBatchFrame + 1 || mBrush.GetResourceObject() == nullptr
        || !FSlateApplication::IsInitialized())
    {
        return layerId;
    }
    if (!mResourceHandle.IsValid())
    {
        mResourceHandle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(mBrush);
        if (!mResourceHandle.IsValid())
        {
            return layerId;
        }
    }

    // Canvas pixels -> widget local units -> window space (the accumulated transform includes the DPI scale)
    const FSlateRenderTransform& renderTransform = allottedGeometry.GetAccumulatedRenderTransform();
    const float inverseScale = allottedGeometry.Scale > 0.0f ? 1.0f / allottedGeometry.Scale : 1.0f;
    mPaintVertices.Reset();
    mPaintVertices.Append(vertices);
    for (FSlateVertex& vertex : mPaintVertices)
    {
        vertex.Position = renderTransform.TransformPoint(vertex.Position * inverseScale);
    }
    FSlateDrawElement::MakeCustomVerts(outDrawElements, layerId, mResourceHandle, mPaintVertices, mBatch.GetIndices(),
        nullptr, 0, 0);
    mLastPaintedShapeCount = mBatch.GetShapeCount();
    return layerId + 1;
}

//////////////////////////////////////////////////////////////////////////
// Takes no layout space
FVector2D SSOLHudShapeLayer::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    return FVector2D::ZeroVector;
}
