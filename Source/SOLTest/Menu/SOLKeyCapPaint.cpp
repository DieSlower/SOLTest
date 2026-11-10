/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyCapPaint.h"

#include "Menu/SOLKeyCaps.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"

#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Layout/Geometry.h"
#include "Rendering/DrawElements.h"

//////////////////////////////////////////////////////////////////////////
// Returns a text range's width in local units at a paint scale, 0 for an empty range or without a measure service
float SOLKeyCapPaint::MeasureRange(const FSlateFontMeasure* measure, const FStringView text, const int32 start,
    const int32 end, const FSlateFontInfo& font, const float scale)
{
    if (measure == nullptr || end <= start)
    {
        return 0.0f;
    }
    const float paintScale = scale > 0.0f ? scale : 1.0f;
    return static_cast<float>(measure->Measure(text, start, end, font, false, paintScale).X) / paintScale;
}

//////////////////////////////////////////////////////////////////////////
// Returns a whole text's width in local units at a paint scale, 0 for an empty text or without a measure service
float SOLKeyCapPaint::Measure(const FSlateFontMeasure* measure, const FStringView text, const FSlateFontInfo& font,
    const float scale)
{
    return MeasureRange(measure, text, 0, text.Len(), font, scale);
}

//////////////////////////////////////////////////////////////////////////
// Shapes a text at a scale (null for an empty text or without a font cache)
FShapedGlyphSequencePtr SOLKeyCapPaint::Shape(const FSlateFontCache* cache, const FStringView text,
    const FSlateFontInfo& font, const float scale)
{
    if (cache == nullptr || text.IsEmpty())
    {
        return nullptr;
    }
    return cache->ShapeBidirectionalText(text.GetData(), 0, text.Len(), font, scale,
        TextBiDi::ETextDirection::LeftToRight, ETextShapingMethod::Auto);
}

//////////////////////////////////////////////////////////////////////////
// Returns a shaped text's width in local units (0 for none or a non-positive scale)
float SOLKeyCapPaint::ShapedWidth(const FShapedGlyphSequencePtr& text, const float scale)
{
    return text.IsValid() && scale > 0.0f ? text->GetMeasuredWidth() / scale : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Paints a shaped text at a local x, vertically centred in a line (shaped at the paint scale); returns its width
float SOLKeyCapPaint::PaintText(const FShapedGlyphSequencePtr& text, const float x, const float lineTop,
    const float lineHeight, const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId,
    const FLinearColor& color, const ESlateDrawEffect effects)
{
    if (!text.IsValid() || !(geometry.Scale > 0.0f))
    {
        return 0.0f;
    }
    const float inverse = 1.0f / geometry.Scale;
    const FVector2f size(text->GetMeasuredWidth() * inverse, text->GetMaxTextHeight() * inverse);
    const FVector2f topLeft(x, lineTop + 0.5f * (lineHeight - size.Y));
    FSlateDrawElement::MakeShapedText(out, layerId, geometry.ToPaintGeometry(size, FSlateLayoutTransform(topLeft)),
        text.ToSharedRef(), effects, color, FLinearColor::Transparent);
    return size.X;
}

//////////////////////////////////////////////////////////////////////////
// Draws a filled box in local units
void SOLKeyCapPaint::Fill(const FBox2f& box, const FGeometry& geometry, FSlateWindowElementList& out,
    const int32 layerId, const FSlateBrush* brush, const ESlateDrawEffect effects, const FLinearColor& color)
{
    FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(box.GetSize(), FSlateLayoutTransform(box.Min)),
        brush, effects, color);
}

//////////////////////////////////////////////////////////////////////////
// Draws a box outline of a line width (four flat boxes: top, bottom, left, right) in local units
void SOLKeyCapPaint::Outline(const FBox2f& box, const float line, const FGeometry& geometry,
    FSlateWindowElementList& out, const int32 layerId, const FSlateBrush* brush, const ESlateDrawEffect effects,
    const FLinearColor& color)
{
    const FVector2f size = box.GetSize();
    const FVector2f pieces[4][2] = {
        { box.Min, FVector2f(size.X, line) },
        { FVector2f(box.Min.X, box.Max.Y - line), FVector2f(size.X, line) },
        { box.Min, FVector2f(line, size.Y) },
        { FVector2f(box.Max.X - line, box.Min.Y), FVector2f(line, size.Y) },
    };
    for (const FVector2f (&piece)[2] : pieces)
    {
        FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(piece[1], FSlateLayoutTransform(piece[0])),
            brush, effects, color);
    }
}

//////////////////////////////////////////////////////////////////////////
// Paints one key cap: its hull, then its mouse glyph and its legend centred in it right of the hull's LED
bool SOLKeyCapPaint::PaintCap(const FSOLKeyCap& cap, const FVector2f& topLeft, const float width,
    const FShapedGlyphSequencePtr* legend, const FGeometry& geometry, FSlateWindowElementList& out,
    const int32 layerId, const FSOLKeyCapPaintStyle& style)
{
    using namespace SOL::Help;
    if (style.Hull == nullptr)
    {
        return false;
    }
    FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(FVector2f(width, KEYCAP_HEIGHT),
        FSlateLayoutTransform(topLeft)), style.Hull, style.Effects, style.HullTint);

    // The content (the glyph, then the legend as shaped at this scale) centred in the cap right of the hull's LED (a
    // recess cap has none: SOLKeyCaps::ContentLeft)
    const bool bHasLegend = legend != nullptr && legend->IsValid();
    const float legendWidth = bHasLegend ? ShapedWidth(*legend, geometry.Scale) : 0.0f;
    const float content = SOLKeyCaps::ContentWidth(cap, legendWidth);
    const float left = SOLKeyCaps::ContentLeft(cap);
    float x = topLeft.X + left + 0.5f * (width - left - KEYCAP_PADDING - content);
    if (cap.Shape == ESOLKeyCapShape::Mouse && style.Line != nullptr)
    {
        // The glyph: its outline, wheel and button divider dim, the used part lit in cyan
        const FVector2f glyphSize(KEYCAP_MOUSE_GLYPH_WIDTH, KEYCAP_MOUSE_GLYPH_HEIGHT);
        const FSOLMouseGlyph glyph = SOLKeyCaps::MouseGlyph(FVector2f(x, topLeft.Y + 0.5f * (KEYCAP_HEIGHT
            - glyphSize.Y)), glyphSize, cap.Mouse);
        const FLinearColor dim = SOLSolidStyle::DimTextColor() * style.StyleTint;
        const FLinearColor lit = SOLSolidStyle::ValueColor() * style.StyleTint;
        const float line = KEYCAP_MOUSE_LINE_PX / FMath::Max(geometry.Scale, UE_SMALL_NUMBER);
        const FBox2f* litBox = glyph.Lit == ESOLMouseGlyphPart::LeftButton ? &glyph.LeftButton
            : glyph.Lit == ESOLMouseGlyphPart::RightButton ? &glyph.RightButton
            : glyph.Lit == ESOLMouseGlyphPart::Wheel ? &glyph.Wheel : nullptr;
        if (litBox != nullptr)
        {
            Fill(*litBox, geometry, out, layerId + 1, style.Line, style.Effects, lit);
        }
        if (glyph.Lit != ESOLMouseGlyphPart::Wheel)
        {
            Outline(glyph.Wheel, line, geometry, out, layerId + 1, style.Line, style.Effects, dim);
        }
        Outline(glyph.Body, line, geometry, out, layerId + 1, style.Line, style.Effects,
            glyph.Lit == ESOLMouseGlyphPart::Body ? lit : dim);
        const FBox2f divider(FVector2f(glyph.LeftButton.Max.X - 0.5f * line, glyph.Body.Min.Y),
            FVector2f(glyph.LeftButton.Max.X + 0.5f * line, glyph.LeftButton.Max.Y));
        const FBox2f base(FVector2f(glyph.Body.Min.X, glyph.LeftButton.Max.Y - 0.5f * line),
            FVector2f(glyph.Body.Max.X, glyph.LeftButton.Max.Y + 0.5f * line));
        Fill(divider, geometry, out, layerId + 1, style.Line, style.Effects, dim);
        Fill(base, geometry, out, layerId + 1, style.Line, style.Effects, dim);
        x += KEYCAP_MOUSE_GLYPH_WIDTH + (legendWidth > 0.0f ? KEYCAP_GAP : 0.0f);
    }
    if (bHasLegend)
    {
        PaintText(*legend, x, topLeft.Y, KEYCAP_HEIGHT, geometry, out, layerId + 1, style.LegendColor, style.Effects);
    }
    return bHasLegend;
}
