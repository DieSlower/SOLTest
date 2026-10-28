/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLHudShapeBatch.h"

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SLeafWidget.h"

class UMaterialInterface;

/**
 * The flight HUD's batched glow-shape layer (SDD 21 phase A): a hit-test-invisible leaf widget over the game viewport
 * that draws one FSOLHudShapeBatch per frame as a single FSlateDrawElement::MakeCustomVerts element with
 * M_SOLHudShape (one draw call for every shape). ASOLFlightHud fills the batch in canvas pixels during DrawHUD and
 * stamps the frame; OnPaint maps the pixels into window space through the widget's geometry and draws only a batch
 * stamped this frame or the previous one, so a HUD that stopped drawing (menu, warp, HUD hidden) leaves nothing behind
 * after at most one frame (Slate may paint before DrawHUD in a frame, so the previous frame's batch must stay valid).
 * The owner keeps the material alive (a UPROPERTY); the widget holds the brush only.
 */
class SOLTEST_API SSOLHudShapeLayer : public SLeafWidget
{
public:

    SLATE_BEGIN_ARGS(SSOLHudShapeLayer) {}
    SLATE_END_ARGS()

    // Reserves the batch and paint buffers once (SOL::Hologram::HUD_MAX_SHAPES quads)
    void Construct(const FArguments& args);

    // Sets the material the shapes are drawn with (kept alive by the owner)
    void SetMaterial(UMaterialInterface* material);

    // Returns the batch the owner refills each frame
    FSOLHudShapeBatch& GetBatch() { return mBatch; }

    // Stamps the batch as this frame's (call after filling it)
    void MarkBatchFrame(uint64 frameNumber) { mBatchFrame = frameNumber; }

    // Returns the number of shapes drawn by the last paint (verification)
    int32 GetLastPaintedShapeCount() const { return mLastPaintedShapeCount; }

    // Draws the batch as one custom-vertex element
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // Takes no layout space (it fills the viewport overlay slot)
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

private:

    FSOLHudShapeBatch mBatch;                           // Filled by the HUD in canvas pixels
    FSlateBrush mBrush;                                 // Brush wrapping the shape material
    mutable FSlateResourceHandle mResourceHandle;       // Renderer handle of the brush, fetched once
    mutable TArray<FSlateVertex> mPaintVertices;        // The batch in window space, reused every paint
    uint64 mBatchFrame = 0;                             // Frame the batch was filled for
    mutable int32 mLastPaintedShapeCount = 0;           // Shapes drawn by the last paint
};
