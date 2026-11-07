/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyCapRow.h"

#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidStyle.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns whether two parsed cap models show the same caps in the same alternatives
    bool KeyCapRowSameModel(const FSOLKeyCapModel& a, const FSOLKeyCapModel& b)
    {
        if (a.Caps.Num() != b.Caps.Num() || a.Chords.Num() != b.Chords.Num())
        {
            return false;
        }
        for (int32 index = 0; index < a.Caps.Num(); ++index)
        {
            const FSOLKeyCap& capA = a.Caps[index];
            const FSOLKeyCap& capB = b.Caps[index];
            if (capA.Shape != capB.Shape || capA.Mouse != capB.Mouse || capA.WidthUnits != capB.WidthUnits
                || capA.bUnbound != capB.bUnbound || !capA.KeyName.Equals(capB.KeyName, ESearchCase::CaseSensitive)
                || !capA.Label.Equals(capB.Label, ESearchCase::CaseSensitive))
            {
                return false;
            }
        }
        for (int32 index = 0; index < a.Chords.Num(); ++index)
        {
            const FSOLKeyCapChord& chordA = a.Chords[index];
            const FSOLKeyCapChord& chordB = b.Chords[index];
            if (chordA.FirstCap != chordB.FirstCap || chordA.CapCount != chordB.CapCount
                || chordA.Binding != chordB.Binding)
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a text's measured width at scale 1 (local units), 0 for an empty text or without Slate
    float KeyCapRowMeasure(const FSlateFontMeasure* measure, const FStringView text, const FSlateFontInfo& font)
    {
        if (measure == nullptr || text.IsEmpty())
        {
            return 0.0f;
        }
        return static_cast<float>(measure->Measure(text, font, 1.0f).X);
    }

    //////////////////////////////////////////////////////////////////////////
    // Shapes a text at a scale (null for an empty text or without Slate)
    FShapedGlyphSequencePtr KeyCapRowShape(const FSlateFontCache* cache, const FStringView text,
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
    // Draws a box outline of a line width (four flat boxes) in local units
    void KeyCapRowOutline(const FBox2f& box, const float line, const FGeometry& geometry, FSlateWindowElementList& out,
        const int32 layerId, const FSlateBrush* brush, const ESlateDrawEffect effects, const FLinearColor& color)
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
            FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(piece[1],
                FSlateLayoutTransform(piece[0])), brush, effects, color);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Draws a filled box in local units
    void KeyCapRowFill(const FBox2f& box, const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId,
        const FSlateBrush* brush, const ESlateDrawEffect effects, const FLinearColor& color)
    {
        FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(box.GetSize(),
            FSlateLayoutTransform(box.Min)), brush, effects, color);
    }
}

//////////////////////////////////////////////////////////////////////////
// Makes the row hit-test invisible and tickless, gives it the flat line brush and listens for Slate's invalidate-all
// (a font cache flush unloads the faces the kept shaped texts point at)
void SSOLKeyCapRow::Construct(const FArguments& /*args*/)
{
    SetVisibility(EVisibility::HitTestInvisible);
    SetCanTick(false);
    mLineBrush = FSlateColorBrush(FLinearColor::White);
    mLegendColor = SOLSolidStyle::TextColor();
    if (FSlateApplicationBase::IsInitialized())
    {
        mInvalidateAllHandle = FSlateApplicationBase::Get().OnInvalidateAllWidgets().AddSP(this,
            &SSOLKeyCapRow::HandleInvalidateAll);
    }
}

//////////////////////////////////////////////////////////////////////////
// Stops listening for Slate's invalidate-all
SSOLKeyCapRow::~SSOLKeyCapRow()
{
    if (mInvalidateAllHandle.IsValid() && FSlateApplicationBase::IsInitialized())
    {
        FSlateApplicationBase::Get().OnInvalidateAllWidgets().Remove(mInvalidateAllHandle);
    }
}

//////////////////////////////////////////////////////////////////////////
// Slate invalidated every widget (a font cache flush among the causes): the shaped texts are rebuilt at the next paint
void SSOLKeyCapRow::HandleInvalidateAll(const bool /*bClearResourcesImmediately*/)
{
    mShapedScale = -1.0f;
    mShapedMessageScale = -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Sets the cap brushes and the fonts, then measures and lays out (the one measure of a freshly built row)
void SSOLKeyCapRow::SetStyle(const FSlateBrush& capBrush, const FSlateBrush& unboundBrush,
    const FSlateFontInfo& legendFont, const FSlateFontInfo& messageFont)
{
    mCapBrush = capBrush;
    mUnboundBrush = unboundBrush;
    mLegendFont = legendFont;
    mMessageFont = messageFont;
    mHasStyle = true;
    MeasureLegends();
    MeasureMessage();
    Layout();
}

//////////////////////////////////////////////////////////////////////////
// Sets the caps and ends a message, then measures their legends and lays them out
void SSOLKeyCapRow::SetModel(const FSOLKeyCapModel& model)
{
    mModel = model;
    mIsMessage = false;
    MeasureLegends();
    Layout();
}

//////////////////////////////////////////////////////////////////////////
// Shows a text in place of the caps, measuring only the text (the caps' measures and shaped legends are kept)
void SSOLKeyCapRow::SetMessage(const FString& message)
{
    mMessage = message;
    mIsMessage = true;
    MeasureMessage();
    Invalidate(EInvalidateWidgetReason::Layout);
}

//////////////////////////////////////////////////////////////////////////
// Sets the wrap width, re-laying the caps out (no measure, no reshape) only on a change
void SSOLKeyCapRow::SetWrapWidth(const float width)
{
    if (width == mWrapWidth)
    {
        return;
    }
    mWrapWidth = width;
    Layout();
}

//////////////////////////////////////////////////////////////////////////
// Sets the legend colour, repainting only on a change
void SSOLKeyCapRow::SetLegendColor(const FLinearColor& color)
{
    if (color == mLegendColor)
    {
        return;
    }
    mLegendColor = color;
    Invalidate(EInvalidateWidgetReason::Paint);
}

//////////////////////////////////////////////////////////////////////////
// Returns the laid-out width (the message's while one shows)
float SSOLKeyCapRow::GetLayoutWidth() const
{
    return mIsMessage ? mMessageWidth : mLayout.Width;
}

//////////////////////////////////////////////////////////////////////////
// Returns the laid-out height (at least one cap tall)
float SSOLKeyCapRow::GetLayoutHeight() const
{
    const float cap = SOL::Help::KEYCAP_HEIGHT;
    return mIsMessage ? FMath::Max(cap, mMessageHeight) : FMath::Max(cap, mLayout.Height);
}

//////////////////////////////////////////////////////////////////////////
// Returns the font measure service, or null without Slate or before the style is set
const FSlateFontMeasure* SSOLKeyCapRow::GetMeasure() const
{
    return FSlateApplication::IsInitialized() && mHasStyle
        ? &FSlateApplication::Get().GetRenderer()->GetFontMeasureService().Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Measures every drawn legend and the "+" and "/" joints at scale 1; the shaped legends go stale
void SSOLKeyCapRow::MeasureLegends()
{
    const FSlateFontMeasure* measure = GetMeasure();
    mLegendWidths.SetNumUninitialized(mModel.Caps.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < mModel.Caps.Num(); ++index)
    {
        mLegendWidths[index] = KeyCapRowMeasure(measure, SOLKeyCaps::DrawnLegend(mModel.Caps[index]), mLegendFont);
    }
    mPlusWidth = KeyCapRowMeasure(measure, SOLMenuText::KEYCAP_CHORD_PLUS, mLegendFont);
    mSlashWidth = KeyCapRowMeasure(measure, SOLMenuText::KEYCAP_ALTERNATIVE_SLASH, mLegendFont);
    mShapedScale = -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Measures the message at scale 1 (none without one); the shaped message goes stale
void SSOLKeyCapRow::MeasureMessage()
{
    const FSlateFontMeasure* measure = mIsMessage ? GetMeasure() : nullptr;
    mMessageWidth = KeyCapRowMeasure(measure, mMessage, mMessageFont);
    mMessageHeight = measure != nullptr ? static_cast<float>(measure->GetMaxCharacterHeight(mMessageFont, 1.0f))
        : 0.0f;
    mShapedMessageScale = -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Lays the caps out in the wrap width from the measured legends (SOLKeyCaps::DrawnCapWidth)
void SSOLKeyCapRow::Layout()
{
    mCapWidths.SetNumUninitialized(mModel.Caps.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < mModel.Caps.Num(); ++index)
    {
        const float legend = mLegendWidths.IsValidIndex(index) ? mLegendWidths[index] : 0.0f;
        mCapWidths[index] = SOLKeyCaps::DrawnCapWidth(mModel.Caps[index], legend);
    }
    FSOLKeyCapMetrics metrics;
    metrics.PlusWidth = mPlusWidth;
    metrics.SeparatorWidth = mSlashWidth;
    SOLKeyCaps::LayoutRow(mModel, mCapWidths, metrics, mWrapWidth, mLayout);
    Invalidate(EInvalidateWidgetReason::Layout);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the last paint drew every cap (hull material brush) and every drawn legend, or the message
bool SSOLKeyCapRow::WasPaintedWhole() const
{
    if (!mHasStyle || !(mPaintedScale > 0.0f) || mCapBrush.GetResourceObject() == nullptr)
    {
        return false;
    }
    if (mIsMessage)
    {
        return mPaintedLegends == 1 && mShapedMessageScale == mPaintedScale;
    }
    int32 legends = 0;
    for (const FSOLKeyCap& cap : mModel.Caps)
    {
        legends += SOLKeyCaps::DrawnLegend(cap).IsEmpty() ? 0 : 1;
    }
    return mPaintedCaps == mModel.Caps.Num() && mPaintedLegends == legends && mShapedScale == mPaintedScale;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether every drawn legend and the joints were measured wider than 0
bool SSOLKeyCapRow::AreLegendsMeasured() const
{
    if (!mHasStyle || !(mPlusWidth > 0.0f) || !(mSlashWidth > 0.0f) || mLegendWidths.Num() != mModel.Caps.Num())
    {
        return false;
    }
    for (int32 index = 0; index < mModel.Caps.Num(); ++index)
    {
        if (!SOLKeyCaps::DrawnLegend(mModel.Caps[index]).IsEmpty() && !(mLegendWidths[index] > 0.0f))
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Shapes the stale drawn texts at the paint scale: the legends and the joints, or the message while one shows
void SSOLKeyCapRow::Reshape(const float scale) const
{
    const FSlateFontCache* cache = FSlateApplication::IsInitialized()
        ? &FSlateApplication::Get().GetRenderer()->GetFontCache().Get() : nullptr;
    if (mIsMessage)
    {
        mShapedMessage = KeyCapRowShape(cache, mMessage, mMessageFont, scale);
        mShapedMessageScale = scale;
        return;
    }
    mShapedLegends.SetNum(mModel.Caps.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < mModel.Caps.Num(); ++index)
    {
        mShapedLegends[index] = KeyCapRowShape(cache, SOLKeyCaps::DrawnLegend(mModel.Caps[index]), mLegendFont,
            scale);
    }
    mShapedPlus = KeyCapRowShape(cache, SOLMenuText::KEYCAP_CHORD_PLUS, mLegendFont, scale);
    mShapedSlash = KeyCapRowShape(cache, SOLMenuText::KEYCAP_ALTERNATIVE_SLASH, mLegendFont, scale);
    mShapedScale = scale;
}

//////////////////////////////////////////////////////////////////////////
// Paints the caps in their laid-out places (hulls first, then glyphs and texts above them), or the message
int32 SSOLKeyCapRow::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& widgetStyle, const bool bParentEnabled) const
{
    mPaintedCaps = 0;
    mPaintedLegends = 0;
    if (!mHasStyle)
    {
        return layerId;
    }
    const float scale = allottedGeometry.Scale;
    mPaintedScale = scale;
    if (scale != (mIsMessage ? mShapedMessageScale : mShapedScale))
    {
        Reshape(scale);
    }
    const ESlateDrawEffect effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None
        : ESlateDrawEffect::DisabledEffect;
    const FLinearColor styleTint = widgetStyle.GetColorAndOpacityTint();
    const float lineHeight = SOL::Help::KEYCAP_HEIGHT;
    if (mIsMessage)
    {
        const float top = 0.5f * (allottedGeometry.GetLocalSize().Y - lineHeight);
        PaintText(mShapedMessage, 0.0f, top, lineHeight, allottedGeometry, outDrawElements, layerId,
            SOLSolidStyle::TextColor() * styleTint, effects);
        mPaintedLegends += mShapedMessage.IsValid() ? 1 : 0;
        return layerId;
    }

    // The caps' hulls on the first layer, their glyphs, legends and the joints on the next
    const FLinearColor dim = SOLSolidStyle::DimTextColor() * styleTint;
    for (const FSOLKeyCapLayoutItem& item : mLayout.Items)
    {
        if (item.Kind == ESOLKeyCapItem::Cap)
        {
            PaintCap(item, allottedGeometry, outDrawElements, layerId, styleTint, effects);
            continue;
        }
        const FShapedGlyphSequencePtr& joint = item.Kind == ESOLKeyCapItem::Plus ? mShapedPlus : mShapedSlash;
        const float jointWidth = joint.IsValid() ? joint->GetMeasuredWidth() / scale : 0.0f;
        PaintText(joint, item.X + 0.5f * (item.Width - jointWidth), item.Y, lineHeight, allottedGeometry,
            outDrawElements, layerId + 1, dim, effects);
    }
    return layerId + 1;
}

//////////////////////////////////////////////////////////////////////////
// Paints one cap: its hull, then its mouse glyph and its legend centred in it
int32 SSOLKeyCapRow::PaintCap(const FSOLKeyCapLayoutItem& item, const FGeometry& geometry,
    FSlateWindowElementList& out, const int32 layerId, const FLinearColor& styleTint,
    const ESlateDrawEffect effects) const
{
    using namespace SOL::Help;
    if (!mModel.Caps.IsValidIndex(item.Cap))
    {
        return layerId;
    }
    const FSOLKeyCap& cap = mModel.Caps[item.Cap];
    const FSlateBrush* hull = cap.bUnbound ? &mUnboundBrush : &mCapBrush;
    FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(FVector2f(item.Width, KEYCAP_HEIGHT),
        FSlateLayoutTransform(FVector2f(item.X, item.Y))), hull, effects, hull->GetTint(FWidgetStyle()) * styleTint);
    ++mPaintedCaps;

    // The content (the glyph, then the legend as shaped at this scale) centred in the cap right of the hull's LED (a
    // recess cap has none: SOLKeyCaps::ContentLeft)
    const FShapedGlyphSequencePtr* legend = mShapedLegends.IsValidIndex(item.Cap) ? &mShapedLegends[item.Cap] : nullptr;
    const float legendWidth = legend != nullptr && legend->IsValid() && geometry.Scale > 0.0f
        ? (*legend)->GetMeasuredWidth() / geometry.Scale : 0.0f;
    const float content = SOLKeyCaps::ContentWidth(cap, legendWidth);
    const float left = SOLKeyCaps::ContentLeft(cap);
    float x = item.X + left + 0.5f * (item.Width - left - KEYCAP_PADDING - content);
    if (cap.Shape == ESOLKeyCapShape::Mouse)
    {
        // The glyph: its outline and button divider dim, the used part lit in cyan
        const FVector2f glyphSize(KEYCAP_MOUSE_GLYPH_WIDTH, KEYCAP_MOUSE_GLYPH_HEIGHT);
        const FSOLMouseGlyph glyph = SOLKeyCaps::MouseGlyph(FVector2f(x, item.Y + 0.5f * (KEYCAP_HEIGHT
            - glyphSize.Y)), glyphSize, cap.Mouse);
        const FLinearColor dim = SOLSolidStyle::DimTextColor() * styleTint;
        const FLinearColor lit = SOLSolidStyle::ValueColor() * styleTint;
        const float line = KEYCAP_MOUSE_LINE_PX / FMath::Max(geometry.Scale, UE_SMALL_NUMBER);
        const FBox2f* litBox = glyph.Lit == ESOLMouseGlyphPart::LeftButton ? &glyph.LeftButton
            : glyph.Lit == ESOLMouseGlyphPart::RightButton ? &glyph.RightButton
            : glyph.Lit == ESOLMouseGlyphPart::Wheel ? &glyph.Wheel : nullptr;
        if (litBox != nullptr)
        {
            KeyCapRowFill(*litBox, geometry, out, layerId + 1, &mLineBrush, effects, lit);
        }
        if (glyph.Lit != ESOLMouseGlyphPart::Wheel)
        {
            KeyCapRowOutline(glyph.Wheel, line, geometry, out, layerId + 1, &mLineBrush, effects, dim);
        }
        KeyCapRowOutline(glyph.Body, line, geometry, out, layerId + 1, &mLineBrush, effects,
            glyph.Lit == ESOLMouseGlyphPart::Body ? lit : dim);
        const FBox2f divider(FVector2f(glyph.LeftButton.Max.X - 0.5f * line, glyph.Body.Min.Y),
            FVector2f(glyph.LeftButton.Max.X + 0.5f * line, glyph.LeftButton.Max.Y));
        const FBox2f base(FVector2f(glyph.Body.Min.X, glyph.LeftButton.Max.Y - 0.5f * line),
            FVector2f(glyph.Body.Max.X, glyph.LeftButton.Max.Y + 0.5f * line));
        KeyCapRowFill(divider, geometry, out, layerId + 1, &mLineBrush, effects, dim);
        KeyCapRowFill(base, geometry, out, layerId + 1, &mLineBrush, effects, dim);
        x += KEYCAP_MOUSE_GLYPH_WIDTH + (legendWidth > 0.0f ? KEYCAP_GAP : 0.0f);
    }
    if (legend != nullptr && legend->IsValid())
    {
        const FLinearColor color = (cap.bUnbound ? SOLSolidStyle::DimTextColor() : mLegendColor) * styleTint;
        PaintText(*legend, x, item.Y, KEYCAP_HEIGHT, geometry, out, layerId + 1, color, effects);
        ++mPaintedLegends;
    }
    return layerId + 1;
}

//////////////////////////////////////////////////////////////////////////
// Paints a shaped text at a local x, vertically centred in a line (the glyphs were shaped at the paint scale)
void SSOLKeyCapRow::PaintText(const FShapedGlyphSequencePtr& text, const float x, const float lineTop,
    const float lineHeight, const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId,
    const FLinearColor& color, const ESlateDrawEffect effects) const
{
    if (!text.IsValid() || !(geometry.Scale > 0.0f))
    {
        return;
    }
    const float inverse = 1.0f / geometry.Scale;
    const FVector2f size(text->GetMeasuredWidth() * inverse, text->GetMaxTextHeight() * inverse);
    const FVector2f topLeft(x, lineTop + 0.5f * (lineHeight - size.Y));
    FSlateDrawElement::MakeShapedText(out, layerId, geometry.ToPaintGeometry(size, FSlateLayoutTransform(topLeft)),
        text.ToSharedRef(), effects, color, FLinearColor::Transparent);
}

//////////////////////////////////////////////////////////////////////////
// The laid-out size
FVector2D SSOLKeyCapRow::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    return FVector2D(GetLayoutWidth(), GetLayoutHeight());
}

//////////////////////////////////////////////////////////////////////////
// Starts with the solid text colour as the legend colour
USOLKeyCapRow::USOLKeyCapRow()
    : mLegendColor(SOLSolidStyle::TextColor())
{
    SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

//////////////////////////////////////////////////////////////////////////
// Shows a bindings cell's caps (parsed into the scratch, taken only when they differ)
void USOLKeyCapRow::SetCell(const TCHAR* cell, const TArray<FString>& liveKeys)
{
    SOLKeyCaps::ParseCell(cell, liveKeys, mScratch);
    TakeScratch();
}

//////////////////////////////////////////////////////////////////////////
// Shows one binding's caps (parsed into the scratch, taken only when they differ)
void USOLKeyCapRow::SetBinding(const int32 binding, const TArray<FString>& liveKeys)
{
    SOLKeyCaps::ParseBinding(binding, liveKeys, mScratch);
    TakeScratch();
}

//////////////////////////////////////////////////////////////////////////
// Takes the scratch as the caps when they differ or a message showed, and hands them to the leaf
void USOLKeyCapRow::TakeScratch()
{
    if (!mIsShowingMessage && KeyCapRowSameModel(mScratch, mModel))
    {
        return;
    }
    Swap(mModel, mScratch);
    mIsShowingMessage = false;
    if (mRow.IsValid())
    {
        mRow->SetModel(mModel);
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows a text in place of the caps (kept for the next SetCell / SetBinding)
void USOLKeyCapRow::SetMessage(const FString& message)
{
    if (mIsShowingMessage && message.Equals(mMessage, ESearchCase::CaseSensitive))
    {
        return;
    }
    mMessage = message;
    mIsShowingMessage = true;
    if (mRow.IsValid())
    {
        mRow->SetMessage(mMessage);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the message while one shows, else the caps' text form
FString USOLKeyCapRow::GetText() const
{
    return mIsShowingMessage ? mMessage : SOLKeyCaps::ModelText(mModel);
}

//////////////////////////////////////////////////////////////////////////
// Sets the wrap width (handed to the leaf, which re-lays out only on a change)
void USOLKeyCapRow::SetWrapWidth(const float width)
{
    mWrapWidth = width;
    if (mRow.IsValid())
    {
        mRow->SetWrapWidth(width);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the legend colour of bound caps (the leaf repaints only on a change)
void USOLKeyCapRow::SetLegendColor(const FLinearColor& color)
{
    mLegendColor = color;
    if (mRow.IsValid())
    {
        mRow->SetLegendColor(color);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the leaf's laid-out width, -1 before it is built
float USOLKeyCapRow::GetLaidOutWidth() const
{
    return mRow.IsValid() ? mRow->GetLayoutWidth() : -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns the leaf's laid-out height, -1 before it is built
float USOLKeyCapRow::GetLaidOutHeight() const
{
    return mRow.IsValid() ? mRow->GetLayoutHeight() : -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the leaf's last paint drew every cap's hull with the hull material and every drawn legend shaped at
// the paint scale (or the message), false before it is built or painted
bool USOLKeyCapRow::WasPaintedWhole() const
{
    return mRow.IsValid() && mRow->WasPaintedWhole();
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the leaf measured every drawn legend and joint (false before it is built)
bool USOLKeyCapRow::AreLegendsMeasured() const
{
    return mRow.IsValid() && mRow->AreLegendsMeasured();
}

//////////////////////////////////////////////////////////////////////////
// Returns the leaf's line count, 0 before it is built
int32 USOLKeyCapRow::GetLaidOutLines() const
{
    return mRow.IsValid() ? mRow->GetLineCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Drops the leaf
void USOLKeyCapRow::ReleaseSlateResources(const bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    mRow.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Builds the leaf with the solid kit's cap brushes (Small button hull, an unbound cap the recess) and fonts (the legend
// Rajdhani SemiBold KEYCAP_FONT_SIZE, the message the body role) and hands it the state
TSharedRef<SWidget> USOLKeyCapRow::RebuildWidget()
{
    const USOLSolidMenuSubsystem* subsystem = USOLSolidMenuSubsystem::Get(this);
    const FVector2f capSize(SOL::Help::KEYCAP_MIN_WIDTH, SOL::Help::KEYCAP_HEIGHT);
    // The state first (nothing is measured before the style), then the style: the row measures and lays out once
    mRow = SNew(SSOLKeyCapRow);
    mRow->SetWrapWidth(mWrapWidth);
    mRow->SetLegendColor(mLegendColor);
    mRow->SetModel(mModel);
    if (mIsShowingMessage)
    {
        mRow->SetMessage(mMessage);
    }
    mRow->SetStyle(SOLSolidStyle::MakeHullBrush(subsystem, ESOLSolidStyle::SmallButton, capSize,
        FSOLSolidButtonLook()), SOLSolidStyle::MakeHullBrush(subsystem, ESOLSolidStyle::Recess, capSize,
        FSOLSolidButtonLook()), SOLSolidStyle::MenuFont(subsystem, ESOLMenuFontWeight::SemiBold,
        SOL::Help::KEYCAP_FONT_SIZE), SOLSolidStyle::MenuFont(subsystem, ESOLMenuFontWeight::Bold,
        SOL::MenuStyle::BODY_FONT_SIZE));
    return mRow.ToSharedRef();
}
