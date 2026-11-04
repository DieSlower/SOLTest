/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudTextList.h"

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
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
 *
 * Phase B (SDD 21 Amendment 2) adds, in paint order: dark translucent panel backings (one solid box per hologram
 * panel, for contrast under the additive glow), the shapes, then the glow text (FSOLHudTextList, engine fonts drawn by
 * Slate: a soft drop shadow, then the text with a hot core and an outline in its own colour). Every buffer is reserved
 * once in Construct.
 */
class SOLTEST_API SSOLHudShapeLayer : public SLeafWidget
{
public:

    SLATE_BEGIN_ARGS(SSOLHudShapeLayer)
        : _MaxShapes(SOL::Hologram::HUD_MAX_SHAPES)
    {}
        // Shape capacity of the batch and the paint buffer (the HUD's by default; the menu layer asks for less)
        SLATE_ARGUMENT(int32, MaxShapes)
    SLATE_END_ARGS()

    // Reserves the batch and paint buffers once (args.MaxShapes quads)
    void Construct(const FArguments& args);

    // Sets the material the shapes are drawn with (kept alive by the owner)
    void SetMaterial(UMaterialInterface* material);

    // Returns the batch the owner refills each frame
    FSOLHudShapeBatch& GetBatch() { return mBatch; }

    // Returns the glow-text lines the owner refills each frame (same frame stamp as the batch)
    FSOLHudTextList& GetTexts() { return mTexts; }

    // Empties the batch, the text lines and the panel backings for a new frame
    void ResetFrame();

    // Adds a soft dark backing under a hologram panel (canvas pixels, rounded with cornerPx and fading in over
    // featherPx from its edge; SOLHudShapeMath::BackingLayer), its darkness scaled by opacity (0-1, so a fading panel
    // fades its backing too), in colour's RGB (the HUD backing colour by default; the alpha is always the HUD backing
    // alpha x opacity); false when the backing buffer is full
    bool AddBacking(const FBox2f& rect, float cornerPx, float featherPx, float opacity = 1.0f,
        const FLinearColor& color = SOL::Hologram::HUD_PANEL_BACKING_COLOR);

    // Sets the font a text line of a given ESOLHudFont is drawn with (the engine fonts' legacy Slate font info)
    void SetFont(ESOLHudFont font, const FSlateFontInfo& fontInfo);

    // Turns the glow-text style on (hot core and coloured outline) or off (plain text over the drop shadow)
    void SetTextGlow(bool bGlow) { mIsTextGlow = bGlow; }

    // Returns the number of text lines drawn by the last paint (verification)
    int32 GetLastPaintedTextCount() const { return mLastPaintedTextCount; }

    // Returns the number of panel backings drawn by the last paint (verification)
    int32 GetLastPaintedBackingCount() const { return mLastPaintedBackingCount; }

    // Stamps the batch as this frame's (call after filling it)
    void MarkBatchFrame(uint64 frameNumber) { mBatchFrame = frameNumber; }

    // Returns the number of shapes drawn by the last paint (verification)
    int32 GetLastPaintedShapeCount() const { return mLastPaintedShapeCount; }

    // Returns the frame the batch was last stamped for (verification)
    uint64 GetBatchFrame() const { return mBatchFrame; }

    // Returns the material the shapes are drawn with, or null (verification)
    UObject* GetMaterial() const { return mBrush.GetResourceObject(); }

    // Returns the number of panel backings queued this frame
    int32 GetBackingCount() const { return mBackings.Num(); }

    // Returns a queued backing's rectangle in canvas pixels, or an invalid box for an index out of range (verification)
    FBox2f GetBackingRect(const int32 index) const
    {
        return mBackings.IsValidIndex(index) ? mBackings[index].Rect : FBox2f(ForceInit);
    }

    // Sets a brush up as the white, outline-free rounded box the backings are drawn with (tinted per layer)
    static void InitBackingBrush(FSlateBrush& brush);

    // Paints one soft dark backing (pixels, SOLHudShapeMath::BackingLayer's stack of rounded boxes) on layerId in
    // colour's RGB at the HUD backing alpha x opacity; the brush's corner radius is set per box (Slate copies it into
    // each element)
    static void PaintBacking(const FGeometry& allottedGeometry, FSlateWindowElementList& outDrawElements,
        int32 layerId, FSlateBrush& backingBrush, const FBox2f& rect, float cornerPx, float featherPx, float opacity,
        const FLinearColor& color, float inverseScale);

    // Paints a batch (pixels relative to the geometry's origin) as one custom-vertex element with the brush's
    // material on layerId, through a reused vertex buffer; fetches the resource handle once; true when drawn
    static bool PaintBatch(const FGeometry& allottedGeometry, FSlateWindowElementList& outDrawElements, int32 layerId,
        const FSOLHudShapeBatch& batch, const FSlateBrush& brush, FSlateResourceHandle& resourceHandle,
        TArray<FSlateVertex>& paintVertices, float inverseScale);

    // Draws the batch as one custom-vertex element
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // Takes no layout space (it fills the viewport overlay slot)
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

private:

    // Draws the text lines (drop shadow, then the text) on two layers above the shapes
    void PaintTexts(const FGeometry& allottedGeometry, FSlateWindowElementList& outDrawElements, int32 layerId,
        float inverseScale) const;

    FSOLHudShapeBatch mBatch;                           // Filled by the HUD in canvas pixels
    FSOLHudTextList mTexts;                             // Glow-text lines, canvas pixels
    // One panel backing: its rectangle, corner radius and edge feather in canvas pixels, its opacity and colour (RGB)
    struct FSOLHudBacking
    {
        FBox2f Rect;
        float CornerPx = 0.0f;
        float FeatherPx = 0.0f;
        float Opacity = 1.0f;
        FLinearColor Color = FLinearColor::Black;
    };

    TArray<FSOLHudBacking> mBackings;                   // Panel backings, canvas pixels (reserved once)
    FSlateFontInfo mFonts[static_cast<int32>(ESOLHudFont::Count)];     // Fonts per ESOLHudFont
    mutable FSlateBrush mBackingBrush;                  // White rounded box tinted per layer; its corner radius is set
                                                        // per layer in OnPaint (Slate copies it into each element)
    bool mIsTextGlow = true;                            // Glow-text style (false: plain text over the shadow)
    mutable int32 mLastPaintedTextCount = 0;            // Text lines drawn by the last paint
    mutable int32 mLastPaintedBackingCount = 0;         // Panel backings drawn by the last paint
    FSlateBrush mBrush;                                 // Brush wrapping the shape material
    mutable FSlateResourceHandle mResourceHandle;       // Renderer handle of the brush, fetched once
    mutable TArray<FSlateVertex> mPaintVertices;        // The batch in window space, reused every paint
    uint64 mBatchFrame = 0;                             // Frame the batch was filled for
    mutable int32 mLastPaintedShapeCount = 0;           // Shapes drawn by the last paint
};
