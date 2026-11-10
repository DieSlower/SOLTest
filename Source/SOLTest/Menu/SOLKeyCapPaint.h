/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Fonts/ShapedTextFwd.h"
#include "Rendering/RenderingCommon.h"

class FSlateFontCache;
class FSlateFontMeasure;
class FSlateWindowElementList;
struct FGeometry;
struct FSlateBrush;
struct FSlateFontInfo;
struct FSOLKeyCap;

// How one key cap is painted (SOLKeyCapPaint::PaintCap): the colours already carry the widget's style tint
struct FSOLKeyCapPaintStyle
{
    const FSlateBrush* Hull = nullptr;              // The cap's hull (a key cap or the unbound recess)
    FLinearColor HullTint = FLinearColor::White;    // The hull's tint
    const FSlateBrush* Line = nullptr;              // A flat white box: the mouse glyph's lines and lit part
    FLinearColor LegendColor = FLinearColor::White; // The legend's colour
    FLinearColor StyleTint = FLinearColor::White;   // The widget's tint (the glyph's dim and lit colours)
    ESlateDrawEffect Effects = ESlateDrawEffect::None;  // The widget's draw effects
};

/**
 * The key cap painter and the paint helpers shared by the key cap row (SSOLKeyCapRow) and the keyboard diagram's
 * hover popup (SSOLKeyboardDiagram), so a cap looks the same everywhere (SDD 33 D3). Paint allocates nothing: the
 * texts come shaped, and the boxes are plain draw elements.
 */
namespace SOLKeyCapPaint
{
    // Returns a text range's width in local units ([start, end) measured at the paint scale, as it is drawn, then
    // divided by it); 0 for an empty range or without a measure service
    float MeasureRange(const FSlateFontMeasure* measure, FStringView text, int32 start, int32 end,
        const FSlateFontInfo& font, float scale = 1.0f);

    // Returns a whole text's width in local units at a paint scale; 0 for an empty text or without a measure service
    float Measure(const FSlateFontMeasure* measure, FStringView text, const FSlateFontInfo& font, float scale = 1.0f);

    // Shapes a text at a scale (null for an empty text or without a font cache)
    FShapedGlyphSequencePtr Shape(const FSlateFontCache* cache, FStringView text, const FSlateFontInfo& font,
        float scale);

    // Returns a shaped text's width in local units (0 for none or a non-positive scale)
    float ShapedWidth(const FShapedGlyphSequencePtr& text, float scale);

    // Paints a shaped text at a local x, vertically centred in a line (shaped at the paint scale); returns its width
    float PaintText(const FShapedGlyphSequencePtr& text, float x, float lineTop, float lineHeight,
        const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId, const FLinearColor& color,
        ESlateDrawEffect effects);

    // Draws a filled box in local units
    void Fill(const FBox2f& box, const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId,
        const FSlateBrush* brush, ESlateDrawEffect effects, const FLinearColor& color);

    // Draws a box outline of a line width (four flat boxes) in local units
    void Outline(const FBox2f& box, float line, const FGeometry& geometry, FSlateWindowElementList& out,
        int32 layerId, const FSlateBrush* brush, ESlateDrawEffect effects, const FLinearColor& color);

    // Paints one key cap of a width, KEYCAP_HEIGHT tall, at a local top-left: its hull on layerId, then on layerId + 1
    // the mouse glyph (outline, wheel and divider dim, the used part lit) and the legend, centred right of the hull's
    // LED. Returns whether a legend was drawn (a valid shaped legend; null or empty draws none). Paints nothing
    // without a hull brush and no mouse glyph without a line brush
    bool PaintCap(const FSOLKeyCap& cap, const FVector2f& topLeft, float width, const FShapedGlyphSequencePtr* legend,
        const FGeometry& geometry, FSlateWindowElementList& out, int32 layerId, const FSOLKeyCapPaintStyle& style);
}
