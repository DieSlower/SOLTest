/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyboardDiagram.h"

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"

#include "Brushes/SlateColorBrush.h"
#include "Fonts/FontCache.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Types/WidgetActiveTimerDelegate.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Shapes a text at a scale (null for an empty text or without Slate)
    FShapedGlyphSequencePtr KeyboardDiagramShape(const FSlateFontCache* cache, const FStringView text,
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
    float KeyboardDiagramWidth(const FShapedGlyphSequencePtr& text, const float scale)
    {
        return text.IsValid() && scale > 0.0f ? text->GetMeasuredWidth() / scale : 0.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a text range's measured width at scale 1 ([start, end) of text), or its length as a fallback width of
    // one unit a character without a measure service
    float KeyboardDiagramMeasure(const FSlateFontMeasure* measure, const FStringView text, const int32 start,
        const int32 end, const FSlateFontInfo& font)
    {
        if (end <= start)
        {
            return 0.0f;
        }
        if (measure == nullptr)
        {
            return static_cast<float>(end - start);
        }
        return static_cast<float>(measure->Measure(text, start, end, font, false, 1.0f).X);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a colour with its alpha scaled
    FLinearColor KeyboardDiagramFade(FLinearColor color, const float alpha)
    {
        color.A *= alpha;
        return color;
    }

    // A key cap's top line span in local units (D35, FitKeyText's left and right)
    struct FKeyboardDiagramSpan
    {
        float Left = 0.0f;
        float Right = 0.0f;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a key cap's top line span: inside the pad (D38: the diagram's keys have no LED), left of the lock mark
    // (when the key is locked)
    FKeyboardDiagramSpan KeyboardDiagramLegendSpan(const FBox2f& box, const bool bLocked)
    {
        using namespace SOL::Help;
        FKeyboardDiagramSpan span;
        span.Left = box.Min.X + DIAGRAM_KEY_PAD;    // D38: the diagram's keys have no LED
        span.Right = box.Max.X - DIAGRAM_KEY_PAD - (bLocked ? DIAGRAM_MARK_SIZE + DIAGRAM_MARK_GAP : 0.0f);
        return span;
    }

    //////////////////////////////////////////////////////////////////////////
    // Wraps hover strip lines in a strip width at scale 1 (D36: the full width, the cap is on its own row above): a
    // keys column (the widest keys text, at most DIAGRAM_DETAIL_KEYS_MAX_FRACTION of the width), then each action
    // broken at spaces into visual rows, calling visit(line, bFirst, start, end) per row. Returns the keys column's
    // width. Without a measure service each action stays one row
    template <typename FVisit>
    float KeyboardDiagramWrapDetail(const FSlateFontMeasure* measure, const FSOLKeyboardDiagramFonts& fonts,
        const TArray<FSOLDiagramDetailLine>& lines, const float stripWidth, FVisit&& visit)
    {
        using namespace SOL::Help;
        float keysWidth = 0.0f;
        for (const FSOLDiagramDetailLine& line : lines)
        {
            keysWidth = FMath::Max(keysWidth, KeyboardDiagramMeasure(measure, line.Keys, 0, line.Keys.Len(),
                fonts.Strong));
        }
        keysWidth = FMath::Min(keysWidth, stripWidth * DIAGRAM_DETAIL_KEYS_MAX_FRACTION);
        const float textWidth = FMath::Max(stripWidth - DIAGRAM_LEGEND_SWATCH - 2.0f * DIAGRAM_TEXT_GAP - keysWidth,
            1.0f);

        // Greedy word wrap: each row takes words while they fit (a word wider than the row takes a row of its own)
        for (int32 lineIndex = 0; lineIndex < lines.Num(); ++lineIndex)
        {
            const FString& action = lines[lineIndex].Action;
            const int32 length = action.Len();
            int32 start = 0;
            bool bFirst = true;
            do
            {
                while (start < length && action[start] == TEXT(' '))
                {
                    ++start;
                }
                if (start >= length && !bFirst)
                {
                    break;
                }
                int32 rowEnd = measure != nullptr ? INDEX_NONE : length;
                for (int32 position = start; rowEnd != length && measure != nullptr;)
                {
                    int32 wordEnd = position;
                    while (wordEnd < length && action[wordEnd] != TEXT(' '))
                    {
                        ++wordEnd;
                    }
                    const bool bFits = KeyboardDiagramMeasure(measure, action, start, wordEnd, fonts.Text) <= textWidth;
                    if (rowEnd != INDEX_NONE && !bFits)
                    {
                        break;
                    }
                    rowEnd = wordEnd;
                    if (wordEnd >= length || !bFits)
                    {
                        break;
                    }
                    position = wordEnd + 1;
                }
                rowEnd = FMath::Max(rowEnd, start);
                visit(lineIndex, bFirst, start, rowEnd);
                bFirst = false;
                start = rowEnd + 1;
            }
            while (start < length);
        }
        return keysWidth;
    }
}

//////////////////////////////////////////////////////////////////////////
// Makes the leaf tickless and hit-testable (it reports mouse moves), keeps each layout key's main key and listens for
// Slate's invalidate-all
void SSOLKeyboardDiagram::Construct(const FArguments& /*args*/)
{
    SetCanTick(false);
    SetVisibility(EVisibility::Visible);
    mFlatBrush = FSlateColorBrush(FLinearColor::White);
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    mMainOf.SetNumUninitialized(layout.Num());
    for (int32 index = 0; index < layout.Num(); ++index)
    {
        const int32 main = layout[index].bMirror ? SOLKeyboardLayout::FindKey(layout[index].KeyName) : INDEX_NONE;
        mMainOf[index] = main != INDEX_NONE ? main : index;
    }
    if (FSlateApplicationBase::IsInitialized())
    {
        mInvalidateAllHandle = FSlateApplicationBase::Get().OnInvalidateAllWidgets().AddSP(this,
            &SSOLKeyboardDiagram::HandleInvalidateAll);
    }
}

//////////////////////////////////////////////////////////////////////////
// Stops listening for Slate's invalidate-all
SSOLKeyboardDiagram::~SSOLKeyboardDiagram()
{
    if (mInvalidateAllHandle.IsValid() && FSlateApplicationBase::IsInitialized())
    {
        FSlateApplicationBase::Get().OnInvalidateAllWidgets().Remove(mInvalidateAllHandle);
    }
}

//////////////////////////////////////////////////////////////////////////
// Slate invalidated every widget: every shaped text is rebuilt at the next paint
void SSOLKeyboardDiagram::HandleInvalidateAll(const bool /*bClearResourcesImmediately*/)
{
    mShapedScale = -1.0f;
    mShapedDetailScale = -1.0f;
}

//////////////////////////////////////////////////////////////////////////
// Sets the strip cap's brush, the keys' brush (D38: no LED) and the fonts; the strip is wrapped again and every text
// reshaped
void SSOLKeyboardDiagram::SetStyle(const FSlateBrush& capBrush, const FSlateBrush& keyBrush,
    const FSOLKeyboardDiagramFonts& fonts)
{
    mKeyBrush = keyBrush;
    mCapBrush = capBrush;
    mFonts = fonts;
    mHasStyle = true;
    mShapedScale = -1.0f;
    WrapDetail();
    Invalidate(EInvalidateWidgetReason::Layout);
}

//////////////////////////////////////////////////////////////////////////
// Sets the model: the badge texts and the unbound line are rebuilt and the key texts reshaped
void SSOLKeyboardDiagram::SetModel(const FSOLKeyDiagramModel& model)
{
    mModel = model;
    BuildModelTexts();
    mShapedScale = -1.0f;
    Invalidate(EInvalidateWidgetReason::Paint);
}

//////////////////////////////////////////////////////////////////////////
// Sets the panel; the legends are fitted again when the key unit changed and the strip text is wrapped again only when
// its width changed
void SSOLKeyboardDiagram::SetPanel(const FSOLDiagramPanel& panel)
{
    if (panel.Scale.UnitPx != mPanel.Scale.UnitPx)
    {
        mShapedScale = -1.0f;
    }
    mPanel = panel;
    if (mPanel.Detail.GetSize().X != mWrappedWidth)
    {
        WrapDetail();
    }
    Invalidate(EInvalidateWidgetReason::Layout);
}

//////////////////////////////////////////////////////////////////////////
// Sets the hovered key and its strip lines; a change of key starts the ramp of the gaining and the losing key from the
// looks they show now (a reversal mid-ramp continues without a pop)
void SSOLKeyboardDiagram::SetHover(const int32 key, const TArray<FSOLDiagramDetailLine>& lines,
    const float effectsStrength)
{
    mStrength = effectsStrength;
    if (key != mHoveredKey)
    {
        const double nowS = FPlatformTime::Seconds();
        const FSOLSolidButtonLook leavingLook = LookOf(mHoveredKey, nowS);
        const FSOLSolidButtonLook gainingLook = LookOf(key, nowS);
        mLeavingKey = mHoveredKey;
        mLeavingFrom = leavingLook;
        mHoveredKey = key;
        mHoveredFrom = gainingLook;
        mHoverChangeS = nowS;
        if (IsRamping(nowS) && !mRampTimer.IsValid())
        {
            mRampTimer = RegisterActiveTimer(0.0f, FWidgetActiveTimerDelegate::CreateSP(this,
                &SSOLKeyboardDiagram::HandleRampTimer));
        }
    }
    mDetailLines = lines;
    WrapDetail();
    Invalidate(EInvalidateWidgetReason::Paint);
}

//////////////////////////////////////////////////////////////////////////
// Sets what a mouse move and a mouse leave call
void SSOLKeyboardDiagram::SetHoverHandlers(TFunction<void(const FVector2f&)> onMove, TFunction<void()> onLeave)
{
    mOnMove = MoveTemp(onMove);
    mOnLeave = MoveTemp(onLeave);
}

//////////////////////////////////////////////////////////////////////////
// Returns the font measure service, or null without Slate or before the style is set
const FSlateFontMeasure* SSOLKeyboardDiagram::GetMeasure() const
{
    return FSlateApplication::IsInitialized() && mHasStyle
        ? &FSlateApplication::Get().GetRenderer()->GetFontMeasureService().Get() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the "+N" texts, the Other keys' shown names and the unbound line from the model (on a model change only)
void SSOLKeyboardDiagram::BuildModelTexts()
{
    mBadgeTexts.SetNum(mModel.Keys.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < mModel.Keys.Num(); ++index)
    {
        const int32 extra = mModel.Keys[index].ExtraCount;
        mBadgeTexts[index] = extra > 0 ? FString::Format(SOLMenuText::HELP_EXTRA_BADGE_FORMAT, { extra }) : FString();
    }
    mOverflowLabels.SetNum(mModel.Overflow.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < mModel.Overflow.Num(); ++index)
    {
        SOLBindingLabel::AssignKeyDisplayName(mModel.Overflow[index].KeyName, mOverflowLabels[index]);
    }

    // The unbound actions' labels, joined by the separator
    mUnboundText.Reset();
    for (const int32 binding : mModel.Unbound)
    {
        if (!mUnboundText.IsEmpty())
        {
            mUnboundText += SOLMenuText::HELP_UNBOUND_SEPARATOR;
        }
        mUnboundText += SOLKeyBindings::GetDef(binding).Label;
    }
}

//////////////////////////////////////////////////////////////////////////
// Wraps the strip's lines in its full width at scale 1 (D36: the hovered key's cap is measured for its own row above
// them; KeyboardDiagramWrapDetail); the hint alone with no lines
void SSOLKeyboardDiagram::WrapDetail()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SSOLKeyboardDiagram::WrapDetail);
    using namespace SOL::Help;
    const float width = mPanel.Detail.GetSize().X;
    mWrappedWidth = width;
    mShapedDetailScale = -1.0f;
    mDetailRows.Reset();
    mDetailCapWidth = 0.0f;
    if (mDetailLines.IsEmpty())
    {
        FDetailRow& hint = mDetailRows.AddDefaulted_GetRef();
        hint.Text = SOLMenuText::HELP_DETAIL_HINT;
        mDetailKeysWidth = 0.0f;
        return;
    }
    const FSlateFontMeasure* measure = GetMeasure();

    // The hovered key's cap: rebuilt only when the hovered key changed (its strings keep their buffers), measured on
    // every wrap (a style or width change re-measures it)
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    if (layout.IsValidIndex(mHoveredKey))
    {
        if (mHoveredKey != mDetailCapKey)
        {
            mDetailCapKey = mHoveredKey;
            mDetailCap.KeyName = layout[mHoveredKey].KeyName;
            SOLBindingLabel::AssignKeyDisplayName(mDetailCap.KeyName, mDetailCap.Label);
            mDetailCap.Shape = SOLKeyCaps::WidthClass(mDetailCap.KeyName);
            mDetailCap.Mouse = SOLKeyCaps::MouseButtonOf(mDetailCap.KeyName);
            mDetailCap.WidthUnits = SOLKeyCaps::WidthUnits(mDetailCap.KeyName);
            mDetailCap.bUnbound = false;
        }
        const FStringView drawn = SOLKeyCaps::DrawnLegend(mDetailCap);
        mDetailCapWidth = SOLKeyCaps::DrawnCapWidth(mDetailCap, KeyboardDiagramMeasure(measure, drawn, 0, drawn.Len(),
            mFonts.Cap));
    }

    // The lines at the full width, one row per wrapped visual line
    mDetailKeysWidth = KeyboardDiagramWrapDetail(measure, mFonts, mDetailLines, width,
        [this](const int32 line, const bool bFirst, const int32 start, const int32 end)
        {
            FDetailRow& row = mDetailRows.AddDefaulted_GetRef();
            row.Line = line;
            row.bFirst = bFirst;
            row.Text = mDetailLines[line].Action.Mid(start, end - start);
        });
}

//////////////////////////////////////////////////////////////////////////
// Returns the visual rows a set of strip lines wraps into at a strip width (the strip's own wrap, counting only)
int32 SSOLKeyboardDiagram::CountDetailRows(const TArray<FSOLDiagramDetailLine>& lines, const float stripWidth) const
{
    int32 rows = 0;
    KeyboardDiagramWrapDetail(GetMeasure(), mFonts, lines, stripWidth,
        [&rows](const int32 /*line*/, const bool /*bFirst*/, const int32 /*start*/, const int32 /*end*/) { ++rows; });
    return rows;
}

//////////////////////////////////////////////////////////////////////////
// Shapes the key legends, short labels and badges, the area names and the strips' texts at a paint scale, and fits
// each key's legend and badge (D35). The clip technique: a legend wider than its span is cut here, once per model,
// unit or scale change, to its longest whole-glyph prefix that fits, and that prefix is what gets shaped and drawn.
// The paint then draws plain shaped texts at cached offsets: no per-key PushClip / PopClip (each would add a clipping
// state and break the element batching across the ~110 keys) and no allocation or measuring in paint
void SSOLKeyboardDiagram::ReshapeStatic(const float scale) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SSOLKeyboardDiagram::ReshapeStatic);
    const FSlateFontCache* cache = FSlateApplication::IsInitialized()
        ? &FSlateApplication::Get().GetRenderer()->GetFontCache().Get() : nullptr;
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    mShapedLegends.SetNum(layout.Num(), EAllowShrinking::No);
    mLegendFits.SetNum(layout.Num(), EAllowShrinking::No);
    mLegendAbbreviated.SetNum(layout.Num(), EAllowShrinking::No);
    mShapedLabels.SetNum(layout.Num(), EAllowShrinking::No);
    mShapedBadges.SetNum(layout.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < layout.Num(); ++index)
    {
        const bool bHasKey = mModel.Keys.IsValidIndex(index);
        mShapedLegends[index] = KeyboardDiagramShape(cache, layout[index].Legend, mFonts.Key, scale);
        mShapedLabels[index] = bHasKey ? KeyboardDiagramShape(cache, mModel.Keys[index].ShortLabel, mFonts.Label,
            scale) : nullptr;
        mShapedBadges[index] = bHasKey && mBadgeTexts.IsValidIndex(index)
            ? KeyboardDiagramShape(cache, mBadgeTexts[index], mFonts.Badge, scale) : nullptr;

        // D35: fit the legend and the badge in the top line's span (the lock mark taken off; D38: no LED)
        const FBox2f box = KeyBox(index);
        const FKeyboardDiagramSpan span = KeyboardDiagramLegendSpan(box, bHasKey && mModel.Keys[index].bLocked);
        const float badgeWidth = KeyboardDiagramWidth(mShapedBadges[index], scale);
        FSOLKeyTextFit fit = SOLKeyboardLayout::FitKeyText(span.Left, span.Right,
            KeyboardDiagramWidth(mShapedLegends[index], scale), badgeWidth);
        const TCHAR* abbreviation = fit.bClipped ? SOLKeyDiagram::LegendAbbreviation(layout[index].KeyName) : nullptr;
        mLegendAbbreviated[index] = abbreviation != nullptr;
        if (abbreviation != nullptr)
        {
            // D38: a legend that does not fit uses its abbreviation (then clipped like any other if even that does not)
            mShapedLegends[index] = KeyboardDiagramShape(cache, abbreviation, mFonts.Key, scale);
            fit = SOLKeyboardLayout::FitKeyText(span.Left, span.Right, KeyboardDiagramWidth(mShapedLegends[index],
                scale), badgeWidth);
        }
        if (fit.bClipped)
        {
            // Clipped: the longest prefix whose shaped width fits the span (legends are a few characters)
            const FStringView legend(abbreviation != nullptr ? abbreviation : layout[index].Legend);
            const float spanWidth = FMath::Max(span.Right - span.Left, 0.0f);
            FShapedGlyphSequencePtr prefix;
            for (int32 length = legend.Len() - 1; length > 0; --length)
            {
                prefix = KeyboardDiagramShape(cache, legend.Left(length), mFonts.Key, scale);
                if (KeyboardDiagramWidth(prefix, scale) <= spanWidth + KINDA_SMALL_NUMBER)
                {
                    break;
                }
                prefix.Reset();
            }
            mShapedLegends[index] = prefix;
            fit.LegendWidth = KeyboardDiagramWidth(prefix, scale);
        }
        fit.LegendX -= box.Min.X;
        mLegendFits[index] = fit;
    }
    mShapedAreas.SetNum(SOL::Help::AREA_COUNT, EAllowShrinking::No);
    for (int32 area = 0; area < SOL::Help::AREA_COUNT; ++area)
    {
        mShapedAreas[area] = KeyboardDiagramShape(cache, SOLKeyDiagram::AreaName(static_cast<ESOLControlArea>(area)),
            mFonts.Text, scale);
    }
    mShapedOverflow.SetNum(mModel.Overflow.Num(), EAllowShrinking::No);
    mShapedOverflowLabels.SetNum(mModel.Overflow.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < mModel.Overflow.Num(); ++index)
    {
        mShapedOverflow[index] = mOverflowLabels.IsValidIndex(index)
            ? KeyboardDiagramShape(cache, mOverflowLabels[index], mFonts.Key, scale) : nullptr;
        mShapedOverflowLabels[index] = KeyboardDiagramShape(cache, mModel.Overflow[index].ShortLabel, mFonts.Label,
            scale);
    }
    mShapedOtherCaption = KeyboardDiagramShape(cache, SOLMenuText::HELP_OTHER_KEYS, mFonts.Text, scale);
    mShapedUnboundCaption = KeyboardDiagramShape(cache, SOLMenuText::HELP_UNBOUND, mFonts.Text, scale);
    mShapedUnbound = KeyboardDiagramShape(cache, mUnboundText, mFonts.Text, scale);
    mShapedScale = scale;
}

//////////////////////////////////////////////////////////////////////////
// Shapes the strip's rows at a paint scale: the keys on each line's first row, every row's text
void SSOLKeyboardDiagram::ReshapeDetail(const float scale) const
{
    const FSlateFontCache* cache = FSlateApplication::IsInitialized()
        ? &FSlateApplication::Get().GetRenderer()->GetFontCache().Get() : nullptr;
    mShapedDetailKeys.SetNum(mDetailRows.Num(), EAllowShrinking::No);
    mShapedDetailText.SetNum(mDetailRows.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < mDetailRows.Num(); ++index)
    {
        const FDetailRow& row = mDetailRows[index];
        mShapedDetailKeys[index] = row.bFirst && mDetailLines.IsValidIndex(row.Line)
            ? KeyboardDiagramShape(cache, mDetailLines[row.Line].Keys, mFonts.Strong, scale) : nullptr;
        mShapedDetailText[index] = KeyboardDiagramShape(cache, row.Text, mFonts.Text, scale);
    }
    mShapedDetailCap = mDetailCapWidth > 0.0f
        ? KeyboardDiagramShape(cache, SOLKeyCaps::DrawnLegend(mDetailCap), mFonts.Cap, scale) : nullptr;
    mShapedDetailScale = scale;
}

//////////////////////////////////////////////////////////////////////////
// Returns a key's hover look now: the gaining key ramps to hovered, the losing key back to normal, others rest
FSOLSolidButtonLook SSOLKeyboardDiagram::LookOf(const int32 key, const double nowS) const
{
    if (key == INDEX_NONE)
    {
        return FSOLSolidButtonLook();
    }
    const double sinceS = nowS - mHoverChangeS;
    if (key == mHoveredKey)
    {
        return SOLSolidMenu::EvaluateButtonFrom(ESOLSolidButtonState::Hovered, mHoveredFrom, sinceS, mStrength);
    }
    if (key == mLeavingKey)
    {
        return SOLSolidMenu::EvaluateButtonFrom(ESOLSolidButtonState::Normal, mLeavingFrom, sinceS, mStrength);
    }
    return FSOLSolidButtonLook();
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a hover ramp is still in progress (never at strength 0: the look changes at once)
bool SSOLKeyboardDiagram::IsRamping(const double nowS) const
{
    return (mHoveredKey != INDEX_NONE || mLeavingKey != INDEX_NONE) && mStrength > 0.0f
        && nowS - mHoverChangeS < SOL::SolidMenu::HOVER_RAMP_S;
}

//////////////////////////////////////////////////////////////////////////
// Repaints while the ramp runs; one last repaint and the timer stops once it settles
EActiveTimerReturnType SSOLKeyboardDiagram::HandleRampTimer(const double /*currentTime*/, const float /*deltaTime*/)
{
    Invalidate(EInvalidateWidgetReason::Paint);
    if (IsRamping(FPlatformTime::Seconds()))
    {
        return EActiveTimerReturnType::Continue;
    }
    mRampTimer.Reset();
    return EActiveTimerReturnType::Stop;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the last paint drew every layout key's hull with the keys' own hull material (D38: not the strip
// cap's), every legend at the paint scale (D35: fitted, a clipped one as its prefix) and every strip row that fits (the
// hint, or the panel's DetailLines)
bool SSOLKeyboardDiagram::WasPaintedWhole() const
{
    const UObject* keyResource = mKeyBrush.GetResourceObject();
    if (!mHasStyle || !(mPaintedScale > 0.0f) || mCapBrush.GetResourceObject() == nullptr || keyResource == nullptr
        || keyResource == mCapBrush.GetResourceObject() || mShapedScale != mPaintedScale
        || mShapedDetailScale != mPaintedScale)
    {
        return false;
    }
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    int32 legends = 0;
    for (const FSOLLayoutKey& key : layout)
    {
        legends += FStringView(key.Legend).IsEmpty() ? 0 : 1;
    }
    const int32 rowsThatFit = FMath::Min(mDetailRows.Num(), mDetailLines.IsEmpty() ? 1 : mPanel.DetailLines);
    return mPaintedKeys == layout.Num() && mPaintedLegends == legends && mPaintedDetailRows == rowsThatFit
        && mPaintedDetailCap == (mDetailCapWidth > 0.0f);
}

//////////////////////////////////////////////////////////////////////////
// Paints the panel: the legend and the strips, the keys (hulls, then tints, then texts and marks) and the hover strip
int32 SSOLKeyboardDiagram::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& widgetStyle, const bool bParentEnabled) const
{
    mPaintedKeys = 0;
    mPaintedLegends = 0;
    mPaintedDetailRows = 0;
    mPaintedDetailCap = false;
    mPaintedWideLegends = 0;
    mPaintedHiddenBadges = 0;
    if (!mHasStyle)
    {
        return layerId;
    }
    const float scale = allottedGeometry.Scale;
    mPaintedScale = scale;
    if (scale != mShapedScale)
    {
        ReshapeStatic(scale);
    }
    if (scale != mShapedDetailScale)
    {
        ReshapeDetail(scale);
    }
    const ESlateDrawEffect effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None
        : ESlateDrawEffect::DisabledEffect;
    const FLinearColor styleTint = widgetStyle.GetColorAndOpacityTint();
    const double nowS = FPlatformTime::Seconds();

    PaintSections(allottedGeometry, outDrawElements, layerId, styleTint, effects);
    const int32 keyCount = SOLKeyboardLayout::KeyCount();
    for (int32 index = 0; index < keyCount; ++index)
    {
        PaintKey(index, allottedGeometry, outDrawElements, layerId, styleTint, effects, nowS);
    }
    PaintDetail(allottedGeometry, outDrawElements, layerId, styleTint, effects);
    return layerId + 2;
}

//////////////////////////////////////////////////////////////////////////
// Returns a layout key's hull box at rest: its layout rect in the panel's scale, inset (empty when nothing is left)
FBox2f SSOLKeyboardDiagram::KeyBox(const int32 index) const
{
    const FBox2f units = SOLKeyboardLayout::KeyRect(index, mPanel.Scale.Placement);
    const FVector2f inset(SOL::Help::DIAGRAM_KEY_INSET, SOL::Help::DIAGRAM_KEY_INSET);
    const FBox2f box(SOLKeyboardLayout::FromUnits(units.Min, mPanel.Scale) + inset,
        SOLKeyboardLayout::FromUnits(units.Max, mPanel.Scale) - inset);
    return box.Max.X > box.Min.X && box.Max.Y > box.Min.Y ? box : FBox2f(ForceInit);
}

//////////////////////////////////////////////////////////////////////////
// Paints one layout key: the LED-less Small hull (D38; lifted and amber-edged by its hover look; a free key dimmed, a
// modifier key with no action of its own at full alpha), the area tint and bar, the legend (D35: fitted inside the
// pad, abbreviated (D38) or clipped to the key) over the short label, the lock mark in the top-right corner with the
// "+N" badge left of it (hidden when it would overlap the legend) and the modifier caret in the bottom-right
void SSOLKeyboardDiagram::PaintKey(const int32 index, const FGeometry& geometry, FSlateWindowElementList& out,
    const int32 layerId, const FLinearColor& styleTint, const ESlateDrawEffect effects, const double nowS) const
{
    using namespace SOL::Help;
    FBox2f box = KeyBox(index);
    if (!box.bIsValid)
    {
        return;
    }
    const FSOLSolidButtonLook look = LookOf(mMainOf.IsValidIndex(index) ? mMainOf[index] : index, nowS);
    box = box.ShiftBy(FVector2f(0.0f, SOLSolidMenu::LiftOffset(look)));
    const FSOLDiagramKey* key = mModel.Keys.IsValidIndex(index) ? &mModel.Keys[index] : nullptr;
    const bool bAssigned = key != nullptr && key->Primary != INDEX_NONE;
    const bool bModifier = key != nullptr && key->bModifier;
    const float fade = bAssigned || bModifier ? 1.0f : DIAGRAM_FREE_ALPHA;

    // The hull, its look in the tint (the alpha a continuous dim for a free key)
    FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(box.GetSize(), FSlateLayoutTransform(box.Min)),
        &mKeyBrush, effects, KeyboardDiagramFade(SOLSolidStyle::BrushTint(look) * styleTint, fade));
    ++mPaintedKeys;

    // The area tint over the face and the area bar along the lower edge
    const FLinearColor areaColor = bAssigned ? SOLSolidStyle::Linear(SOLKeyDiagram::AreaColor(key->Area)) * styleTint
        : FLinearColor::Transparent;
    if (bAssigned)
    {
        PaintBox(box, geometry, out, layerId + 1, KeyboardDiagramFade(areaColor, DIAGRAM_TINT_ALPHA), effects);
        PaintBox(FBox2f(FVector2f(box.Min.X, box.Max.Y - DIAGRAM_AREA_BAR), box.Max), geometry, out, layerId + 1,
            areaColor, effects);
    }

    // The content box and the corner marks' room: the lock top-right with the badge left of it (the top line's span,
    // fitted at shape time), the modifier caret bottom-right
    const float scale = geometry.Scale;
    const float left = box.Min.X + DIAGRAM_KEY_PAD;
    const float right = box.Max.X - DIAGRAM_KEY_PAD;
    const float top = box.Min.Y + DIAGRAM_KEY_PAD;
    const float bottom = box.Max.Y - DIAGRAM_KEY_PAD - (bAssigned ? DIAGRAM_AREA_BAR : 0.0f);
    const bool bLocked = key != nullptr && key->bLocked;
    const FKeyboardDiagramSpan span = KeyboardDiagramLegendSpan(box, bLocked);
    const FSOLKeyTextFit fit = mLegendFits.IsValidIndex(index) ? mLegendFits[index] : FSOLKeyTextFit();
    const FShapedGlyphSequencePtr* badge = mShapedBadges.IsValidIndex(index) && mShapedBadges[index].IsValid()
        ? &mShapedBadges[index] : nullptr;
    if (badge != nullptr && fit.bBadge)
    {
        PaintText(*badge, span.Right - KeyboardDiagramWidth(*badge, scale), top,
            DIAGRAM_MARK_SIZE + DIAGRAM_BADGE_LINE_EXTRA, geometry, out, layerId + 2,
            SOLSolidStyle::TextColor() * styleTint, effects);
    }
    mPaintedHiddenBadges += badge != nullptr && !fit.bBadge ? 1 : 0;
    const FLinearColor markColor = SOLSolidStyle::BrightTextColor() * styleTint;
    if (bLocked)
    {
        // A padlock: the shackle's outline over a filled body
        const float m = DIAGRAM_MARK_SIZE;
        const float line = DIAGRAM_MARK_LINE;
        const FVector2f origin(right - m, top);
        const float bodyTop = origin.Y + DIAGRAM_LOCK_BODY_TOP_FRACTION * m;
        const float shackleLeft = origin.X + DIAGRAM_LOCK_SHACKLE_INSET_FRACTION * m;
        const float shackleRight = origin.X + (1.0f - DIAGRAM_LOCK_SHACKLE_INSET_FRACTION) * m;
        const FLinearColor lockColor = SOLSolidStyle::DimTextColor() * styleTint;
        PaintBox(FBox2f(FVector2f(origin.X, bodyTop), FVector2f(origin.X + m, origin.Y + m)), geometry, out,
            layerId + 2, lockColor, effects);
        PaintBox(FBox2f(FVector2f(shackleLeft, origin.Y), FVector2f(shackleRight, origin.Y + line)), geometry, out,
            layerId + 2, lockColor, effects);
        PaintBox(FBox2f(FVector2f(shackleLeft, origin.Y), FVector2f(shackleLeft + line, bodyTop)), geometry, out,
            layerId + 2, lockColor, effects);
        PaintBox(FBox2f(FVector2f(shackleRight - line, origin.Y), FVector2f(shackleRight, bodyTop)), geometry, out,
            layerId + 2, lockColor, effects);
    }
    float bottomRight = right;
    if (bModifier)
    {
        // The modifier caret (D32): an upward chevron in the bright text colour
        const float m = DIAGRAM_MARK_SIZE;
        const FVector2f tip(right - 0.5f * m, bottom - DIAGRAM_CARET_HEIGHT_FRACTION * m);
        PaintLine(FVector2f(right - m, bottom), tip, DIAGRAM_MARK_LINE, geometry, out, layerId + 2, markColor, effects);
        PaintLine(tip, FVector2f(right, bottom), DIAGRAM_MARK_LINE, geometry, out, layerId + 2, markColor, effects);
        bottomRight -= m + DIAGRAM_MARK_GAP;
    }

    // The legend over the short label (labels hidden below DIAGRAM_MIN_UNIT or when one does not fit the key). The
    // legend is the fitted one (D35): a clipped legend was shaped as its prefix that fits, possibly none at all
    const FShapedGlyphSequencePtr* legend = mShapedLegends.IsValidIndex(index) && mShapedLegends[index].IsValid()
        ? &mShapedLegends[index] : nullptr;
    const FShapedGlyphSequencePtr* label = mShapedLabels.IsValidIndex(index) && mShapedLabels[index].IsValid()
        ? &mShapedLabels[index] : nullptr;
    const float labelWidth = label != nullptr ? KeyboardDiagramWidth(*label, scale) : 0.0f;
    const bool bShowLabel = label != nullptr && mPanel.Scale.bShortLabels && labelWidth <= bottomRight - left;
    const float height = FMath::Max(bottom - top, 0.0f);
    const float legendHeight = bShowLabel ? 0.5f * height : height;
    if (legend != nullptr)
    {
        const float legendWidth = KeyboardDiagramWidth(*legend, scale);
        const FLinearColor legendColor = KeyboardDiagramFade(SOLSolidStyle::LabelColor(look) * styleTint, fade);
        PaintText(*legend, box.Min.X + fit.LegendX, top, legendHeight, geometry, out, layerId + 2, legendColor,
            effects);
        mPaintedWideLegends += legendWidth > FMath::Max(span.Right - span.Left, 0.0f) + KINDA_SMALL_NUMBER ? 1 : 0;
    }
    mPaintedLegends += legend != nullptr || fit.bClipped ? 1 : 0;   // A legend clipped to nothing counts as drawn
    if (bShowLabel)
    {
        PaintText(*label, left + FMath::Max(0.0f, 0.5f * (bottomRight - left - labelWidth)), top + legendHeight,
            height - legendHeight, geometry, out, layerId + 2, areaColor, effects);
    }
}

//////////////////////////////////////////////////////////////////////////
// Paints the area legend (swatch and name per area), the "Other keys" strip (caption, then a tinted cap per key off the
// layout) and the "Unbound" line (caption, then the actions' names)
void SSOLKeyboardDiagram::PaintSections(const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId,
    const FLinearColor& styleTint, const ESlateDrawEffect effects) const
{
    using namespace SOL::Help;
    const float scale = geometry.Scale;
    const FLinearColor text = SOLSolidStyle::TextColor() * styleTint;
    const FLinearColor dim = SOLSolidStyle::DimTextColor() * styleTint;

    // The area legend
    for (int32 area = 0; area < AREA_COUNT; ++area)
    {
        const FBox2f item = SOLKeyboardLayout::LegendItemRect(mPanel, area);
        const float swatchTop = item.Min.Y + 0.5f * (item.GetSize().Y - DIAGRAM_LEGEND_SWATCH);
        PaintBox(FBox2f(FVector2f(item.Min.X, swatchTop), FVector2f(item.Min.X + DIAGRAM_LEGEND_SWATCH, swatchTop
            + DIAGRAM_LEGEND_SWATCH)), geometry, out, layerId + 1, SOLSolidStyle::Linear(SOLKeyDiagram::AreaColor(
            static_cast<ESOLControlArea>(area))) * styleTint, effects);
        if (mShapedAreas.IsValidIndex(area))
        {
            PaintText(mShapedAreas[area], item.Min.X + DIAGRAM_LEGEND_SWATCH + DIAGRAM_TEXT_GAP, item.Min.Y,
                item.GetSize().Y, geometry, out, layerId + 1, text, effects);
        }
    }

    // The Other keys strip: a Small hull cap per key with its tint, bar, name and short label
    const FBox2f other = mPanel.Other;
    if (other.GetSize().Y > 0.0f)
    {
        float x = other.Min.X + PaintText(mShapedOtherCaption, other.Min.X, other.Min.Y, other.GetSize().Y, geometry,
            out, layerId + 1, dim, effects) + DIAGRAM_TEXT_GAP;
        const float leftPad = DIAGRAM_KEY_PAD;      // D38: the diagram's caps have no LED, only the pad
        for (int32 index = 0; index < mModel.Overflow.Num() && index < mShapedOverflow.Num(); ++index)
        {
            const FSOLDiagramKey& key = mModel.Overflow[index];
            const float nameWidth = KeyboardDiagramWidth(mShapedOverflow[index], scale);
            const float labelWidth = KeyboardDiagramWidth(mShapedOverflowLabels[index], scale);
            const FBox2f cap(FVector2f(x, other.Min.Y + DIAGRAM_KEY_INSET), FVector2f(x + leftPad
                + FMath::Max(nameWidth, labelWidth) + DIAGRAM_KEY_PAD * 2.0f, other.Max.Y - DIAGRAM_KEY_INSET));
            FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(cap.GetSize(),
                FSlateLayoutTransform(cap.Min)), &mKeyBrush, effects,
                SOLSolidStyle::BrushTint(FSOLSolidButtonLook()) * styleTint);
            const FLinearColor areaColor = SOLSolidStyle::Linear(SOLKeyDiagram::AreaColor(key.Area)) * styleTint;
            PaintBox(cap, geometry, out, layerId + 1, KeyboardDiagramFade(areaColor, DIAGRAM_TINT_ALPHA), effects);
            PaintBox(FBox2f(FVector2f(cap.Min.X, cap.Max.Y - DIAGRAM_AREA_BAR), cap.Max), geometry, out, layerId + 1,
                areaColor, effects);
            const float half = 0.5f * (cap.GetSize().Y - DIAGRAM_AREA_BAR);
            PaintText(mShapedOverflow[index], cap.Min.X + leftPad, cap.Min.Y, half, geometry, out, layerId + 1, text,
                effects);
            PaintText(mShapedOverflowLabels[index], cap.Min.X + leftPad, cap.Min.Y + half, half, geometry, out,
                layerId + 1, areaColor, effects);
            x = cap.Max.X + DIAGRAM_TEXT_GAP;
        }
    }

    // The Unbound line
    const FBox2f unbound = mPanel.Unbound;
    if (unbound.GetSize().Y > 0.0f)
    {
        const float captionWidth = PaintText(mShapedUnboundCaption, unbound.Min.X, unbound.Min.Y,
            unbound.GetSize().Y, geometry, out, layerId + 1, SOLSolidStyle::WarningColor() * styleTint, effects);
        PaintText(mShapedUnbound, unbound.Min.X + captionWidth + DIAGRAM_TEXT_GAP, unbound.Min.Y, unbound.GetSize().Y,
            geometry, out, layerId + 1, text, effects);
    }
}

//////////////////////////////////////////////////////////////////////////
// Paints the hover strip (D36): the cap row on top (the hovered key's cap, or the hint (dim) with no hover), then per
// visual row at the full width, on a line's first row the area swatch and the keys (cyan), then the row's action text.
// Rows past the panel's DetailLines are not drawn
void SSOLKeyboardDiagram::PaintDetail(const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId,
    const FLinearColor& styleTint, const ESlateDrawEffect effects) const
{
    using namespace SOL::Help;
    const FBox2f strip = mPanel.Detail;
    if (mDetailCapWidth > 0.0f && !mDetailLines.IsEmpty())
    {
        PaintDetailCap(FVector2f(strip.Min.X, strip.Min.Y + 0.5f * (DIAGRAM_DETAIL_CAP_HEIGHT - KEYCAP_HEIGHT)),
            geometry, out, layerId, styleTint, effects);
    }
    const float linesTop = strip.Min.Y + DIAGRAM_DETAIL_CAP_HEIGHT;
    const float textLeft = strip.Min.X + DIAGRAM_LEGEND_SWATCH + 2.0f * DIAGRAM_TEXT_GAP + mDetailKeysWidth;

    // Each wrapped row that fits the strip's line budget: the hint in the cap row, or a line's row below it
    for (int32 index = 0; index < mDetailRows.Num() && index < mPanel.DetailLines; ++index)
    {
        const FDetailRow& row = mDetailRows[index];
        const float rowTop = linesTop + static_cast<float>(index) * DIAGRAM_DETAIL_LINE_HEIGHT;
        if (!mShapedDetailText.IsValidIndex(index))
        {
            break;
        }
        const FShapedGlyphSequencePtr& text = mShapedDetailText[index];
        if (!mDetailLines.IsValidIndex(row.Line))
        {
            // The hint, in the cap row
            PaintText(text, strip.Min.X, strip.Min.Y, DIAGRAM_DETAIL_CAP_HEIGHT, geometry, out, layerId + 1,
                SOLSolidStyle::DimTextColor() * styleTint, effects);
            ++mPaintedDetailRows;
            continue;
        }
        if (row.bFirst)
        {
            const FSOLDiagramDetailLine& line = mDetailLines[row.Line];
            const float swatchTop = rowTop + 0.5f * (DIAGRAM_DETAIL_LINE_HEIGHT - DIAGRAM_LEGEND_SWATCH);
            PaintBox(FBox2f(FVector2f(strip.Min.X, swatchTop), FVector2f(strip.Min.X + DIAGRAM_LEGEND_SWATCH,
                swatchTop + DIAGRAM_LEGEND_SWATCH)), geometry, out, layerId + 1,
                SOLSolidStyle::Linear(SOLKeyDiagram::AreaColor(line.Area)) * styleTint, effects);
            if (mShapedDetailKeys.IsValidIndex(index))
            {
                PaintText(mShapedDetailKeys[index], strip.Min.X + DIAGRAM_LEGEND_SWATCH + DIAGRAM_TEXT_GAP, rowTop,
                    DIAGRAM_DETAIL_LINE_HEIGHT, geometry, out, layerId + 1, SOLSolidStyle::ValueColor() * styleTint,
                    effects);
            }
        }
        PaintText(text, textLeft, rowTop, DIAGRAM_DETAIL_LINE_HEIGHT, geometry, out, layerId + 1,
            SOLSolidStyle::TextColor() * styleTint, effects);
        ++mPaintedDetailRows;
    }
}

//////////////////////////////////////////////////////////////////////////
// Paints the hover strip's cap of the hovered key like a 33b key cap (SSOLKeyCapRow): the Small hull KEYCAP_HEIGHT
// tall, then the mouse glyph (its outline and divider dim, the used part lit cyan) and the legend, centred right of
// the hull's LED
void SSOLKeyboardDiagram::PaintDetailCap(const FVector2f& topLeft, const FGeometry& geometry,
    FSlateWindowElementList& out, const int32 layerId, const FLinearColor& styleTint,
    const ESlateDrawEffect effects) const
{
    using namespace SOL::Help;
    FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(FVector2f(mDetailCapWidth, KEYCAP_HEIGHT),
        FSlateLayoutTransform(topLeft)), &mCapBrush, effects, SOLSolidStyle::BrushTint(FSOLSolidButtonLook())
        * styleTint);
    const float legendWidth = KeyboardDiagramWidth(mShapedDetailCap, geometry.Scale);
    const float content = SOLKeyCaps::ContentWidth(mDetailCap, legendWidth);
    const float left = SOLKeyCaps::ContentLeft(mDetailCap);
    float x = topLeft.X + left + 0.5f * (mDetailCapWidth - left - KEYCAP_PADDING - content);
    if (mDetailCap.Shape == ESOLKeyCapShape::Mouse)
    {
        // The glyph: the lit part filled, the body outlined (lit for the mouse move), the button divider and base
        const FVector2f glyphSize(KEYCAP_MOUSE_GLYPH_WIDTH, KEYCAP_MOUSE_GLYPH_HEIGHT);
        const FSOLMouseGlyph glyph = SOLKeyCaps::MouseGlyph(FVector2f(x, topLeft.Y + 0.5f * (KEYCAP_HEIGHT
            - glyphSize.Y)), glyphSize, mDetailCap.Mouse);
        const FLinearColor dim = SOLSolidStyle::DimTextColor() * styleTint;
        const FLinearColor lit = SOLSolidStyle::ValueColor() * styleTint;
        const float line = KEYCAP_MOUSE_LINE_PX / FMath::Max(geometry.Scale, UE_SMALL_NUMBER);
        const FBox2f* litBox = glyph.Lit == ESOLMouseGlyphPart::LeftButton ? &glyph.LeftButton
            : glyph.Lit == ESOLMouseGlyphPart::RightButton ? &glyph.RightButton
            : glyph.Lit == ESOLMouseGlyphPart::Wheel ? &glyph.Wheel : nullptr;
        if (litBox != nullptr)
        {
            PaintBox(*litBox, geometry, out, layerId + 1, lit, effects);
        }
        const FBox2f body = glyph.Body;
        const FLinearColor bodyColor = glyph.Lit == ESOLMouseGlyphPart::Body ? lit : dim;
        PaintBox(FBox2f(body.Min, FVector2f(body.Max.X, body.Min.Y + line)), geometry, out, layerId + 1, bodyColor,
            effects);
        PaintBox(FBox2f(FVector2f(body.Min.X, body.Max.Y - line), body.Max), geometry, out, layerId + 1, bodyColor,
            effects);
        PaintBox(FBox2f(body.Min, FVector2f(body.Min.X + line, body.Max.Y)), geometry, out, layerId + 1, bodyColor,
            effects);
        PaintBox(FBox2f(FVector2f(body.Max.X - line, body.Min.Y), body.Max), geometry, out, layerId + 1, bodyColor,
            effects);
        PaintBox(FBox2f(FVector2f(glyph.LeftButton.Max.X - 0.5f * line, body.Min.Y), FVector2f(glyph.LeftButton.Max.X
            + 0.5f * line, glyph.LeftButton.Max.Y)), geometry, out, layerId + 1, dim, effects);
        PaintBox(FBox2f(FVector2f(body.Min.X, glyph.LeftButton.Max.Y - 0.5f * line), FVector2f(body.Max.X,
            glyph.LeftButton.Max.Y + 0.5f * line)), geometry, out, layerId + 1, dim, effects);
        x += KEYCAP_MOUSE_GLYPH_WIDTH + (legendWidth > 0.0f ? KEYCAP_GAP : 0.0f);
    }
    PaintText(mShapedDetailCap, x, topLeft.Y, KEYCAP_HEIGHT, geometry, out, layerId + 1,
        SOLSolidStyle::TextColor() * styleTint, effects);
    mPaintedDetailCap = mCapBrush.GetResourceObject() != nullptr
        && (mShapedDetailCap.IsValid() || SOLKeyCaps::DrawnLegend(mDetailCap).IsEmpty());
}

//////////////////////////////////////////////////////////////////////////
// Paints a shaped text at a local x, vertically centred in a line (shaped at the paint scale); returns its width
float SSOLKeyboardDiagram::PaintText(const FShapedGlyphSequencePtr& text, const float x, const float lineTop,
    const float lineHeight, const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId,
    const FLinearColor& color, const ESlateDrawEffect effects) const
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
void SSOLKeyboardDiagram::PaintBox(const FBox2f& box, const FGeometry& geometry, FSlateWindowElementList& out,
    const int32 layerId, const FLinearColor& color, const ESlateDrawEffect effects) const
{
    FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(box.GetSize(), FSlateLayoutTransform(box.Min)),
        &mFlatBrush, effects, color);
}

//////////////////////////////////////////////////////////////////////////
// Draws a line between two local points as a box rotated about its centre (no point array, nothing allocated)
void SSOLKeyboardDiagram::PaintLine(const FVector2f& from, const FVector2f& to, const float thickness,
    const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId, const FLinearColor& color,
    const ESlateDrawEffect effects) const
{
    const FVector2f delta = to - from;
    const float length = delta.Size();
    if (!(length > 0.0f))
    {
        return;
    }
    const FVector2f size(length, thickness);
    const FVector2f centre = 0.5f * (from + to);
    const float angle = FMath::Atan2(delta.Y, delta.X);
    FSlateDrawElement::MakeBox(out, layerId, geometry.ToPaintGeometry(size, FSlateLayoutTransform(centre - 0.5f * size),
        FSlateRenderTransform(FQuat2f(angle)), FVector2f(0.5f, 0.5f)), &mFlatBrush, effects, color);
}

//////////////////////////////////////////////////////////////////////////
// The panel's laid-out size: its widest section by its height
FVector2D SSOLKeyboardDiagram::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    const float width = FMath::Max3(mPanel.Legend.Max.X, mPanel.Diagram.Max.X, mPanel.Detail.Max.X);
    return FVector2D(FMath::Max(width, 0.0f), FMath::Max(mPanel.Height, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Reports the mouse's local point (menu units) to the owner; the event goes on unhandled
FReply SSOLKeyboardDiagram::OnMouseMove(const FGeometry& myGeometry, const FPointerEvent& mouseEvent)
{
    if (mOnMove)
    {
        mOnMove(FVector2f(myGeometry.AbsoluteToLocal(mouseEvent.GetScreenSpacePosition())));
    }
    return FReply::Unhandled();
}

//////////////////////////////////////////////////////////////////////////
// Reports the mouse leaving to the owner
void SSOLKeyboardDiagram::OnMouseLeave(const FPointerEvent& mouseEvent)
{
    SLeafWidget::OnMouseLeave(mouseEvent);
    if (mOnLeave)
    {
        mOnLeave();
    }
}

//////////////////////////////////////////////////////////////////////////
// Lays the empty panel out at width 0 with a one-line hover strip
USOLKeyboardDiagram::USOLKeyboardDiagram()
    : mPanel(SOLKeyboardLayout::LayoutPanel(0.0f, false, false, 1))
{
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the model when the keys differ from the last ones set (always on the first call), re-lays the panel out and
// rebuilds the hovered key's lines
bool USOLKeyboardDiagram::SetKeys(const TArray<FString>& liveKeys)
{
    if (mHasKeys && liveKeys == mLiveKeys)
    {
        return false;
    }
    mLiveKeys = liveKeys;
    mHasKeys = true;
    SOLKeyDiagram::Build(mLiveKeys, mModel);
    if (mDiagram.IsValid())
    {
        mDiagram->SetModel(mModel);
    }
    LayoutPanel();
    RefreshHover();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the layout width, re-laying the panel out on a change of more than DIAGRAM_WIDTH_TOLERANCE (not per frame)
void USOLKeyboardDiagram::SetLayoutWidth(const float width)
{
    if (FMath::IsNearlyEqual(width, mLayoutWidth, SOL::Help::DIAGRAM_WIDTH_TOLERANCE))
    {
        return;
    }
    mLayoutWidth = width;
    LayoutPanel();
}

//////////////////////////////////////////////////////////////////////////
// Sizes the hover strip for the model and the width (D36: here only, on a model, width or leaf change, never on a
// hover), lays the panel out with the model's Other keys and Unbound sections and hands it to the leaf
void USOLKeyboardDiagram::LayoutPanel()
{
    mDetailLineBudget = ComputeDetailLineBudget();
    mPanel = SOLKeyboardLayout::LayoutPanel(mLayoutWidth, mModel.Overflow.Num() > 0, mModel.Unbound.Num() > 0,
        mDetailLineBudget);
    if (mDiagram.IsValid())
    {
        mDiagram->SetPanel(mPanel);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the hover strip's line budget: the busiest key's line count (headless) or wrapped row count at the strip's
// width (with a leaf that measures), at least 1 and at most DIAGRAM_DETAIL_LINES
int32 USOLKeyboardDiagram::ComputeDetailLineBudget()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLKeyboardDiagram::ComputeDetailLineBudget);
    using namespace SOL::Help;
    const int32 headless = FMath::Clamp(FMath::Max(1, SOLKeyDiagram::MaxDetailCount(mModel)), 1,
        DIAGRAM_DETAIL_LINES);
    if (!mDiagram.IsValid() || !mDiagram->CanMeasure())
    {
        return headless;
    }

    // Every key's lines wrapped as the strip wraps them (the strip is the panel's full width), the most rows kept;
    // a mirror key shows its main key's lines and is skipped
    const float stripWidth = FMath::IsFinite(mLayoutWidth) && mLayoutWidth > 0.0f ? mLayoutWidth : 0.0f;
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    int32 rows = headless;
    for (int32 key = 0; key < mModel.Keys.Num() && rows < DIAGRAM_DETAIL_LINES; ++key)
    {
        if (layout.IsValidIndex(key) && layout[key].bMirror)
        {
            continue;
        }
        const int32 count = SOLKeyDiagram::DetailAssignments(mModel, key, mDetailAssignments);
        if (count == 0)
        {
            continue;
        }
        mBudgetLines.SetNum(count, EAllowShrinking::No);
        for (int32 index = 0; index < count; ++index)
        {
            SOLKeyDiagram::DetailLine(mModel, mDetailAssignments[index], mLiveKeys, mBudgetLines[index]);
        }
        rows = FMath::Max(rows, mDiagram->CountDetailRows(mBudgetLines, stripWidth));
    }
    return FMath::Clamp(rows, 1, DIAGRAM_DETAIL_LINES);
}

//////////////////////////////////////////////////////////////////////////
// Hovers the key under a local point (none off every key)
int32 USOLKeyboardDiagram::HoverAt(const FVector2f& localPoint)
{
    const FVector2f units = SOLKeyboardLayout::ToUnits(localPoint, mPanel.Scale);
    SetHoveredKey(SOLKeyboardLayout::KeyAt(units, mPanel.Scale.Placement));
    return mHoveredKey;
}

//////////////////////////////////////////////////////////////////////////
// Hovers a key (a mirror hovers its main key; INDEX_NONE or out of range clears), rebuilding the lines on a change
void USOLKeyboardDiagram::SetHoveredKey(const int32 key)
{
    int32 hovered = mModel.Keys.IsValidIndex(key) ? key : INDEX_NONE;
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    if (hovered != INDEX_NONE && layout.IsValidIndex(hovered) && layout[hovered].bMirror)
    {
        const int32 main = SOLKeyboardLayout::FindKey(layout[hovered].KeyName);
        hovered = main != INDEX_NONE ? main : hovered;
    }
    if (hovered == mHoveredKey)
    {
        return;
    }
    mHoveredKey = hovered;
    RefreshHover();
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the hovered key's strip lines (empty with no hover) and hands the hover to the leaf
void USOLKeyboardDiagram::RefreshHover()
{
    if (mHoveredKey == INDEX_NONE)
    {
        mDetailLines.Reset();
    }
    else
    {
        const int32 count = SOLKeyDiagram::DetailAssignments(mModel, mHoveredKey, mDetailAssignments);
        mDetailLines.SetNum(count, EAllowShrinking::No);
        for (int32 index = 0; index < count; ++index)
        {
            SOLKeyDiagram::DetailLine(mModel, mDetailAssignments[index], mLiveKeys, mDetailLines[index]);
        }
    }
    if (mDiagram.IsValid())
    {
        mDiagram->SetHover(mHoveredKey, mDetailLines, GetEffectsStrength());
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the live effects strength (the default without the solid menu subsystem)
float USOLKeyboardDiagram::GetEffectsStrength() const
{
    const USOLSolidMenuSubsystem* subsystem = USOLSolidMenuSubsystem::Get(this);
    return subsystem != nullptr ? subsystem->GetStrength() : SOL::Settings::HUD_EFFECTS_DEFAULT;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the leaf's last paint drew the whole diagram (false without the leaf)
bool USOLKeyboardDiagram::WasPaintedWhole() const
{
    return mDiagram.IsValid() && mDiagram->WasPaintedWhole();
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of key hulls the leaf's last paint drew (0 without the leaf)
int32 USOLKeyboardDiagram::GetPaintedKeyCount() const
{
    return mDiagram.IsValid() ? mDiagram->GetPaintedKeyCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the leaf's wrapped hover strip rows (0 without the leaf)
int32 USOLKeyboardDiagram::GetDetailRowCount() const
{
    return mDiagram.IsValid() ? mDiagram->GetDetailRowCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the leaf's hover strip cap width (0 without the leaf)
float USOLKeyboardDiagram::GetDetailCapWidth() const
{
    return mDiagram.IsValid() ? mDiagram->GetDetailCapWidth() : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns the leaf's legends wider than their key in the last paint (0 without the leaf)
int32 USOLKeyboardDiagram::GetWideLegendCount() const
{
    return mDiagram.IsValid() ? mDiagram->GetWideLegendCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Drops the leaf
void USOLKeyboardDiagram::ReleaseSlateResources(const bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    mDiagram.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Builds the leaf with the solid kit's Small hull brush and fonts (key legends Rajdhani SemiBold, short labels Medium,
// the strips the Small role) and hands it the model, the panel, the hover and the mouse handlers, then lays the panel
// out again with the leaf's wrapped hover strip budget
TSharedRef<SWidget> USOLKeyboardDiagram::RebuildWidget()
{
    using namespace SOL::Help;
    USOLSolidMenuSubsystem* subsystem = USOLSolidMenuSubsystem::Get(this);
    mDiagram = SNew(SSOLKeyboardDiagram);
    mDiagram->SetModel(mModel);
    mDiagram->SetPanel(mPanel);
    mDiagram->SetHover(mHoveredKey, mDetailLines, GetEffectsStrength());

    // The leaf's mouse moves hover the key under the point and a leave clears it (weak: the leaf may outlive this)
    const TWeakObjectPtr<USOLKeyboardDiagram> weakThis(this);
    mDiagram->SetHoverHandlers([weakThis](const FVector2f& point)
    {
        if (USOLKeyboardDiagram* self = weakThis.Get())
        {
            self->HoverAt(point);
        }
    }, [weakThis]()
    {
        if (USOLKeyboardDiagram* self = weakThis.Get())
        {
            self->SetHoveredKey(INDEX_NONE);
        }
    });

    // The fonts: key legends Rajdhani SemiBold, short labels Medium, the badge, the strips' Small role and the 33b cap
    FSOLKeyboardDiagramFonts fonts;
    fonts.Key = SOLSolidStyle::MenuFont(subsystem, ESOLMenuFontWeight::SemiBold, DIAGRAM_KEY_FONT_SIZE);
    fonts.Label = SOLSolidStyle::MenuFont(subsystem, ESOLMenuFontWeight::Medium, KEYCAP_LABEL_FONT_SIZE);
    fonts.Badge = SOLSolidStyle::MenuFont(subsystem, ESOLMenuFontWeight::SemiBold, DIAGRAM_BADGE_FONT_SIZE);
    fonts.Text = SOLSolidStyle::MenuFont(subsystem, ESOLMenuTextRole::Small, ESOLMenuFontWeight::Regular);
    fonts.Strong = SOLSolidStyle::MenuFont(subsystem, ESOLMenuTextRole::Small, ESOLMenuFontWeight::SemiBold);
    fonts.Cap = SOLSolidStyle::MenuFont(subsystem, ESOLMenuFontWeight::SemiBold, KEYCAP_FONT_SIZE);

    // D38: the keys' brush is the same Small hull with its LED off, in a material of the diagram's own (made once)
    const FSlateBrush capBrush = SOLSolidStyle::MakeHullBrush(subsystem, ESOLSolidStyle::SmallButton,
        FVector2f(KEYCAP_MIN_WIDTH, KEYCAP_HEIGHT), FSOLSolidButtonLook());
    if (KeyMaterial == nullptr && subsystem != nullptr)
    {
        KeyMaterial = subsystem->CreateUniqueMaterial(ESOLSolidStyle::SmallButton, this);
        if (KeyMaterial != nullptr)
        {
            KeyMaterial->SetScalarParameterValue(SOL::SolidMenuMaterialParams::LED_X, DIAGRAM_NO_LED_X);
        }
    }
    FSlateBrush keyBrush = capBrush;
    if (KeyMaterial != nullptr && capBrush.GetResourceObject() != nullptr)
    {
        keyBrush.SetResourceObject(KeyMaterial);
    }
    mDiagram->SetStyle(capBrush, keyBrush, fonts);

    // The leaf measures now: size the hover strip for its wrapped rows (D36)
    LayoutPanel();
    return mDiagram.ToSharedRef();
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a layout key's legend is drawn clipped (false without the leaf)
bool USOLKeyboardDiagram::IsLegendClipped(const int32 key) const
{
    return mDiagram.IsValid() && mDiagram->IsLegendClipped(key);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a layout key's legend is drawn as its D38 abbreviation (false without the leaf)
bool USOLKeyboardDiagram::IsLegendAbbreviated(const int32 key) const
{
    return mDiagram.IsValid() && mDiagram->IsLegendAbbreviated(key);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a layout key's "+N" badge is hidden by D35 (false without the leaf)
bool USOLKeyboardDiagram::IsBadgeHidden(const int32 key) const
{
    return mDiagram.IsValid() && mDiagram->IsBadgeHidden(key);
}

//////////////////////////////////////////////////////////////////////////
// Returns the badges the leaf's last paint hid (D35; 0 without the leaf)
int32 USOLKeyboardDiagram::GetPaintedHiddenBadges() const
{
    return mDiagram.IsValid() ? mDiagram->GetHiddenBadgeCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the keys' LED-less Small hull material (D38; null before the leaf is built with the subsystem)
const UMaterialInstanceDynamic* USOLKeyboardDiagram::GetKeyMaterial() const
{
    return KeyMaterial.Get();
}
