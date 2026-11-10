/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyboardDiagram.h"

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLKeyCapPaint.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidStyle.h"
#include "SOLConstants.h"

#include "Brushes/SlateColorBrush.h"
#include "CoreGlobals.h"
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
    // Returns a text range's measured width in local units at a paint scale ([start, end) of text; the shared cap
    // painter's measure), or its length as a fallback width of one unit a character without a measure service
    float KeyboardDiagramMeasure(const FSlateFontMeasure* measure, const FStringView text, const int32 start,
        const int32 end, const FSlateFontInfo& font, const float scale = 1.0f)
    {
        if (end <= start)
        {
            return 0.0f;
        }
        return measure != nullptr ? SOLKeyCapPaint::MeasureRange(measure, text, start, end, font, scale)
            : static_cast<float>(end - start);
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
    // Wraps hover popup lines in a width at a paint scale (D45: the popup's wrap width, the cap on its own row above):
    // a keys column (the widest keys text, at most DIAGRAM_DETAIL_KEYS_MAX_FRACTION of the width), then each action
    // broken at spaces into visual rows, calling visit(line, bFirst, start, end) per row; every measure is multiplied
    // by ratio (the shaped text's width over the measure's). Returns the keys column's width. Without a measure service
    // each action stays one row
    template <typename FVisit>
    float KeyboardDiagramWrapDetail(const FSlateFontMeasure* measure, const FSOLKeyboardDiagramFonts& fonts,
        const TArray<FSOLDiagramDetailLine>& lines, const float wrapWidth, const float scale, const float ratio,
        FVisit&& visit)
    {
        using namespace SOL::Help;
        float keysWidth = 0.0f;
        for (const FSOLDiagramDetailLine& line : lines)
        {
            keysWidth = FMath::Max(keysWidth, ratio * KeyboardDiagramMeasure(measure, line.Keys, 0, line.Keys.Len(),
                fonts.Strong, scale));
        }
        keysWidth = FMath::Min(keysWidth, wrapWidth * DIAGRAM_DETAIL_KEYS_MAX_FRACTION);
        const float textWidth = FMath::Max(wrapWidth - DIAGRAM_LEGEND_SWATCH - 2.0f * DIAGRAM_TEXT_GAP - keysWidth,
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
                    const bool bFits = ratio * KeyboardDiagramMeasure(measure, action, start, wordEnd, fonts.Text,
                        scale) <= textWidth;
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
// Sets the popup cap's brush, the keys' brush (D38: no LED), the popup's hull (D45) and the fonts; the popup is
// wrapped again and every text reshaped
void SSOLKeyboardDiagram::SetStyle(const FSlateBrush& capBrush, const FSlateBrush& keyBrush,
    const FSlateBrush& popupBrush, const FSOLKeyboardDiagramFonts& fonts)
{
    mKeyBrush = keyBrush;
    mCapBrush = capBrush;
    mPopupBrush = popupBrush;
    mFonts = fonts;
    mHasStyle = true;
    mShapedScale = -1.0f;
    WrapDetail(mWrapScale);
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
// Sets the panel; the legends are fitted again when the key unit changed (the popup's wrap does not depend on it)
void SSOLKeyboardDiagram::SetPanel(const FSOLDiagramPanel& panel)
{
    if (panel.Scale.UnitPx != mPanel.Scale.UnitPx)
    {
        mShapedScale = -1.0f;
    }
    mPanel = panel;
    Invalidate(EInvalidateWidgetReason::Layout);
}

//////////////////////////////////////////////////////////////////////////
// Sets the hovered key and its popup lines; a change of key starts the ramp of the gaining and the losing key from the
// looks they show now (a reversal mid-ramp continues without a pop)
void SSOLKeyboardDiagram::SetHover(const int32 key, const TArray<FSOLDiagramDetailLine>& lines,
    const float effectsStrength)
{
    mStrength = effectsStrength;
    if (key != mHoveredKey)
    {
        const double nowS = GetNowS();
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
    WrapDetail(mWrapScale);
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
// Sets the subsystem whose menu clock the hover ramp runs on (weakly held)
void SSOLKeyboardDiagram::SetClockSource(const USOLSolidMenuSubsystem* source)
{
    mClockSource = source;
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
// Wraps the popup's lines in its wrap width at a paint scale (D45: measured as drawn there, so the popup fits what it
// shows; the hovered key's cap is measured for its own row above them; KeyboardDiagramWrapDetail) and measures the
// popup's content width (the cap, or the keys column and the widest row); nothing with no lines. Runs on a hover or
// style change (at the last popup paint's scale) and when the popup paints at another scale, never per frame
void SSOLKeyboardDiagram::WrapDetail(const float scale) const
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SSOLKeyboardDiagram::WrapDetail);
    mWrapScale = scale > 0.0f ? scale : 1.0f;
    mShapedDetailScale = -1.0f;
    mDetailRows.Reset();
    mDetailCapWidth = 0.0f;
    mDetailKeysWidth = 0.0f;
    mPopupContentWidth = 0.0f;
    if (mDetailLines.IsEmpty())
    {
        return;
    }
    const FSlateFontMeasure* measure = GetMeasure();

    // The hovered key's cap: rebuilt only when the hovered key changed (its strings keep their buffers), measured on
    // every wrap (a style change re-measures it)
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
            mFonts.Cap, mWrapScale));
    }

    // Slate's shaped text draws a little wider than the font measure (about 3.5% for the menu font at the 1080 paint
    // scale): each line's action and keys are shaped once and the widest ratio scales every measure of the wrap
    float ratio = 1.0f;
    const FSlateFontCache* cache = FSlateApplication::IsInitialized()
        ? &FSlateApplication::Get().GetRenderer()->GetFontCache().Get() : nullptr;
    for (int32 line = 0; measure != nullptr && cache != nullptr && line < mDetailLines.Num(); ++line)
    {
        const FSOLDiagramDetailLine& detail = mDetailLines[line];
        for (const TPair<const FString*, const FSlateFontInfo*>& part : { TPair<const FString*, const FSlateFontInfo*>(
            &detail.Action, &mFonts.Text), TPair<const FString*, const FSlateFontInfo*>(&detail.Keys, &mFonts.Strong) })
        {
            const float measured = KeyboardDiagramMeasure(measure, *part.Key, 0, part.Key->Len(), *part.Value,
                mWrapScale);
            const float shaped = SOLKeyCapPaint::ShapedWidth(SOLKeyCapPaint::Shape(cache, *part.Key, *part.Value,
                mWrapScale), mWrapScale);
            ratio = measured > 0.0f ? FMath::Max(ratio, shaped / measured) : ratio;
        }
    }

    // The lines in the popup's wrap width, one row per wrapped visual line, the widest row measured for the size
    float widestText = 0.0f;
    mDetailKeysWidth = KeyboardDiagramWrapDetail(measure, mFonts, mDetailLines, SOLKeyboardLayout::PopupWrapWidth()
        - SOL::Help::DIAGRAM_POPUP_WRAP_SLACK, mWrapScale, ratio,
        [this, measure, ratio, &widestText](const int32 line, const bool bFirst, const int32 start, const int32 end)
        {
            FDetailRow& row = mDetailRows.AddDefaulted_GetRef();
            row.Line = line;
            row.bFirst = bFirst;
            row.Text = mDetailLines[line].Action.Mid(start, end - start);
            widestText = FMath::Max(widestText, ratio * KeyboardDiagramMeasure(measure, mDetailLines[line].Action,
                start, end, mFonts.Text, mWrapScale));
        });
    mPopupContentWidth = SOLKeyboardLayout::PopupContentWidth(mDetailCapWidth, mDetailKeysWidth, widestText);
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
        mShapedLegends[index] = SOLKeyCapPaint::Shape(cache, layout[index].Legend, mFonts.Key, scale);
        mShapedLabels[index] = bHasKey ? SOLKeyCapPaint::Shape(cache, mModel.Keys[index].ShortLabel, mFonts.Label,
            scale) : nullptr;
        mShapedBadges[index] = bHasKey && mBadgeTexts.IsValidIndex(index)
            ? SOLKeyCapPaint::Shape(cache, mBadgeTexts[index], mFonts.Badge, scale) : nullptr;

        // D35: fit the legend and the badge in the top line's span (the lock mark taken off; D38: no LED)
        const FBox2f box = KeyBox(index);
        const FKeyboardDiagramSpan span = KeyboardDiagramLegendSpan(box, bHasKey && mModel.Keys[index].bLocked);
        const float badgeWidth = SOLKeyCapPaint::ShapedWidth(mShapedBadges[index], scale);
        FSOLKeyTextFit fit = SOLKeyboardLayout::FitKeyText(span.Left, span.Right,
            SOLKeyCapPaint::ShapedWidth(mShapedLegends[index], scale), badgeWidth);
        const TCHAR* abbreviation = fit.bClipped ? SOLKeyDiagram::LegendAbbreviation(layout[index].KeyName) : nullptr;
        mLegendAbbreviated[index] = abbreviation != nullptr;
        if (abbreviation != nullptr)
        {
            // D38: a legend that does not fit uses its abbreviation (then clipped like any other if even that does not)
            mShapedLegends[index] = SOLKeyCapPaint::Shape(cache, abbreviation, mFonts.Key, scale);
            fit = SOLKeyboardLayout::FitKeyText(span.Left, span.Right,
                SOLKeyCapPaint::ShapedWidth(mShapedLegends[index], scale), badgeWidth);
        }
        if (fit.bClipped)
        {
            // Clipped: the longest prefix whose shaped width fits the span (legends are a few characters)
            const FStringView legend(abbreviation != nullptr ? abbreviation : layout[index].Legend);
            const float spanWidth = FMath::Max(span.Right - span.Left, 0.0f);
            FShapedGlyphSequencePtr prefix;
            for (int32 length = legend.Len() - 1; length > 0; --length)
            {
                prefix = SOLKeyCapPaint::Shape(cache, legend.Left(length), mFonts.Key, scale);
                if (SOLKeyCapPaint::ShapedWidth(prefix, scale) <= spanWidth + KINDA_SMALL_NUMBER)
                {
                    break;
                }
                prefix.Reset();
            }
            mShapedLegends[index] = prefix;
            fit.LegendWidth = SOLKeyCapPaint::ShapedWidth(prefix, scale);
        }
        fit.LegendX -= box.Min.X;
        mLegendFits[index] = fit;
    }
    mShapedAreas.SetNum(SOL::Help::AREA_COUNT, EAllowShrinking::No);
    for (int32 area = 0; area < SOL::Help::AREA_COUNT; ++area)
    {
        mShapedAreas[area] = SOLKeyCapPaint::Shape(cache, SOLKeyDiagram::AreaName(static_cast<ESOLControlArea>(area)),
            mFonts.Text, scale);
    }
    mShapedOverflow.SetNum(mModel.Overflow.Num(), EAllowShrinking::No);
    mShapedOverflowLabels.SetNum(mModel.Overflow.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < mModel.Overflow.Num(); ++index)
    {
        mShapedOverflow[index] = mOverflowLabels.IsValidIndex(index)
            ? SOLKeyCapPaint::Shape(cache, mOverflowLabels[index], mFonts.Key, scale) : nullptr;
        mShapedOverflowLabels[index] = SOLKeyCapPaint::Shape(cache, mModel.Overflow[index].ShortLabel, mFonts.Label,
            scale);
    }
    mShapedOtherCaption = SOLKeyCapPaint::Shape(cache, SOLMenuText::HELP_OTHER_KEYS, mFonts.Text, scale);
    mShapedUnboundCaption = SOLKeyCapPaint::Shape(cache, SOLMenuText::HELP_UNBOUND, mFonts.Text, scale);
    mShapedUnbound = SOLKeyCapPaint::Shape(cache, mUnboundText, mFonts.Text, scale);
    mShapedHint = SOLKeyCapPaint::Shape(cache, SOLMenuText::HELP_DETAIL_HINT, mFonts.Text, scale);
    mShapedScale = scale;
}

//////////////////////////////////////////////////////////////////////////
// Shapes the popup's rows at a paint scale: the keys on each line's first row, every row's text, the cap's legend
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
            ? SOLKeyCapPaint::Shape(cache, mDetailLines[row.Line].Keys, mFonts.Strong, scale) : nullptr;
        mShapedDetailText[index] = SOLKeyCapPaint::Shape(cache, row.Text, mFonts.Text, scale);
    }
    mShapedDetailCap = mDetailCapWidth > 0.0f
        ? SOLKeyCapPaint::Shape(cache, SOLKeyCaps::DrawnLegend(mDetailCap), mFonts.Cap, scale) : nullptr;

    // D45: the keys column and the content as shaped (what is drawn), so the popup's pad is exact
    float keysWidth = 0.0f;
    float widestText = 0.0f;
    for (int32 index = 0; index < mDetailRows.Num(); ++index)
    {
        keysWidth = FMath::Max(keysWidth, SOLKeyCapPaint::ShapedWidth(mShapedDetailKeys[index], scale));
        widestText = FMath::Max(widestText, SOLKeyCapPaint::ShapedWidth(mShapedDetailText[index], scale));
    }
    // The keys column capped as the wrap caps it (a long rebound chord would otherwise push the rows out of the hull)
    const bool bShaped = cache != nullptr && !mDetailRows.IsEmpty();
    const float keysCap = (SOLKeyboardLayout::PopupWrapWidth() - SOL::Help::DIAGRAM_POPUP_WRAP_SLACK)
        * SOL::Help::DIAGRAM_DETAIL_KEYS_MAX_FRACTION;
    mShapedKeysWidth = bShaped ? FMath::Min(keysWidth, keysCap) : mDetailKeysWidth;
    mShapedContentWidth = bShaped ? SOLKeyboardLayout::PopupContentWidth(mDetailCapWidth, mShapedKeysWidth,
        widestText) : mPopupContentWidth;
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
// Returns whether a hover ramp is still in progress (never at strength 0: the look changes at once; a clock that went
// back, a lost clock source falling back to real time, counts as settled)
bool SSOLKeyboardDiagram::IsRamping(const double nowS) const
{
    const double elapsedS = nowS - mHoverChangeS;
    return (mHoveredKey != INDEX_NONE || mLeavingKey != INDEX_NONE) && mStrength > 0.0f && elapsedS >= 0.0
        && elapsedS < SOL::SolidMenu::HOVER_RAMP_S;
}

//////////////////////////////////////////////////////////////////////////
// Returns the menu clock now: the clock source's (pinned during a guide capture), else the real application time
double SSOLKeyboardDiagram::GetNowS() const
{
    const USOLSolidMenuSubsystem* source = mClockSource.Get();
    return source != nullptr ? source->GetClockS() : FPlatformTime::Seconds();
}

//////////////////////////////////////////////////////////////////////////
// Repaints while the ramp runs; one last repaint and the timer stops once it settles
EActiveTimerReturnType SSOLKeyboardDiagram::HandleRampTimer(const double /*currentTime*/, const float /*deltaTime*/)
{
    Invalidate(EInvalidateWidgetReason::Paint);
    if (IsRamping(GetNowS()))
    {
        return EActiveTimerReturnType::Continue;
    }
    mRampTimer.Reset();
    return EActiveTimerReturnType::Stop;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the last paint drew every layout key's hull with the keys' own hull material (D38: not the popup
// cap's), every legend at the paint scale (D35: fitted, a clipped one as its prefix) and the hint
bool SSOLKeyboardDiagram::WasPaintedWhole() const
{
    const UObject* keyResource = mKeyBrush.GetResourceObject();
    if (!mHasStyle || !(mPaintedScale > 0.0f) || mCapBrush.GetResourceObject() == nullptr || keyResource == nullptr
        || keyResource == mCapBrush.GetResourceObject() || mShapedScale != mPaintedScale)
    {
        return false;
    }
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    int32 legends = 0;
    for (const FSOLLayoutKey& key : layout)
    {
        legends += FStringView(key.Legend).IsEmpty() ? 0 : 1;
    }
    return mPaintedKeys == layout.Num() && mPaintedLegends == legends && mPaintedHint;
}

//////////////////////////////////////////////////////////////////////////
// Paints the panel: the legend, the hint and the strips, then the keys (hulls, then tints, then texts and marks). D45:
// keeps the geometry and the frame for the popup layer, which paints the hover popup after this leaf
int32 SSOLKeyboardDiagram::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& widgetStyle, const bool bParentEnabled) const
{
    mPaintedKeys = 0;
    mPaintedLegends = 0;
    mPaintedHint = false;
    mPaintedWideLegends = 0;
    mPaintedHiddenBadges = 0;
    mPaintGeometry = allottedGeometry;
    mPaintFrame = GFrameCounter;
    mPopupPending = true;
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
    const ESlateDrawEffect effects = ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None
        : ESlateDrawEffect::DisabledEffect;
    const FLinearColor styleTint = widgetStyle.GetColorAndOpacityTint();
    const double nowS = GetNowS();

    PaintSections(allottedGeometry, outDrawElements, layerId, styleTint, effects);
    const int32 keyCount = SOLKeyboardLayout::KeyCount();
    for (int32 index = 0; index < keyCount; ++index)
    {
        PaintKey(index, allottedGeometry, outDrawElements, layerId, styleTint, effects, nowS);
    }
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
// "+N" badge left of it (hidden when it would overlap the legend) and the modifier caret in the bottom-right, every
// text and mark in the bright key text colour (D49)
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

    // D49: every text and mark on the key in the bright key text colour (the area colour stays in the tint and bar)
    const FLinearColor keyText = SOLSolidStyle::Linear(DIAGRAM_KEY_TEXT_COLOR) * styleTint;

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
        PaintText(*badge, span.Right - SOLKeyCapPaint::ShapedWidth(*badge, scale), top,
            DIAGRAM_MARK_SIZE + DIAGRAM_BADGE_LINE_EXTRA, geometry, out, layerId + 2, keyText, effects);
    }
    mPaintedHiddenBadges += badge != nullptr && !fit.bBadge ? 1 : 0;
    if (bLocked)
    {
        // A padlock: the shackle's outline over a filled body
        const float m = DIAGRAM_MARK_SIZE;
        const float line = DIAGRAM_MARK_LINE;
        const FVector2f origin(right - m, top);
        const float bodyTop = origin.Y + DIAGRAM_LOCK_BODY_TOP_FRACTION * m;
        const float shackleLeft = origin.X + DIAGRAM_LOCK_SHACKLE_INSET_FRACTION * m;
        const float shackleRight = origin.X + (1.0f - DIAGRAM_LOCK_SHACKLE_INSET_FRACTION) * m;
        PaintBox(FBox2f(FVector2f(origin.X, bodyTop), FVector2f(origin.X + m, origin.Y + m)), geometry, out,
            layerId + 2, keyText, effects);
        PaintBox(FBox2f(FVector2f(shackleLeft, origin.Y), FVector2f(shackleRight, origin.Y + line)), geometry, out,
            layerId + 2, keyText, effects);
        PaintBox(FBox2f(FVector2f(shackleLeft, origin.Y), FVector2f(shackleLeft + line, bodyTop)), geometry, out,
            layerId + 2, keyText, effects);
        PaintBox(FBox2f(FVector2f(shackleRight - line, origin.Y), FVector2f(shackleRight, bodyTop)), geometry, out,
            layerId + 2, keyText, effects);
    }
    float bottomRight = right;
    if (bModifier)
    {
        // The modifier caret (D32): an upward chevron in the bright text colour
        const float m = DIAGRAM_MARK_SIZE;
        const FVector2f tip(right - 0.5f * m, bottom - DIAGRAM_CARET_HEIGHT_FRACTION * m);
        PaintLine(FVector2f(right - m, bottom), tip, DIAGRAM_MARK_LINE, geometry, out, layerId + 2, keyText, effects);
        PaintLine(tip, FVector2f(right, bottom), DIAGRAM_MARK_LINE, geometry, out, layerId + 2, keyText, effects);
        bottomRight -= m + DIAGRAM_MARK_GAP;
    }

    // The legend over the short label (labels hidden below DIAGRAM_MIN_UNIT or when one does not fit the key). The
    // legend is the fitted one (D35): a clipped legend was shaped as its prefix that fits, possibly none at all
    const FShapedGlyphSequencePtr* legend = mShapedLegends.IsValidIndex(index) && mShapedLegends[index].IsValid()
        ? &mShapedLegends[index] : nullptr;
    const FShapedGlyphSequencePtr* label = mShapedLabels.IsValidIndex(index) && mShapedLabels[index].IsValid()
        ? &mShapedLabels[index] : nullptr;
    const float labelWidth = label != nullptr ? SOLKeyCapPaint::ShapedWidth(*label, scale) : 0.0f;
    const bool bShowLabel = label != nullptr && mPanel.Scale.bShortLabels && labelWidth <= bottomRight - left;
    const float height = FMath::Max(bottom - top, 0.0f);
    const float legendHeight = bShowLabel ? 0.5f * height : height;
    if (legend != nullptr)
    {
        const float legendWidth = SOLKeyCapPaint::ShapedWidth(*legend, scale);
        const FLinearColor legendColor = KeyboardDiagramFade(keyText, fade);
        PaintText(*legend, box.Min.X + fit.LegendX, top, legendHeight, geometry, out, layerId + 2, legendColor,
            effects);
        mPaintedWideLegends += legendWidth > FMath::Max(span.Right - span.Left, 0.0f) + KINDA_SMALL_NUMBER ? 1 : 0;
    }
    mPaintedLegends += legend != nullptr || fit.bClipped ? 1 : 0;   // A legend clipped to nothing counts as drawn
    if (bShowLabel)
    {
        PaintText(*label, left + FMath::Max(0.0f, 0.5f * (bottomRight - left - labelWidth)), top + legendHeight,
            height - legendHeight, geometry, out, layerId + 2, keyText, effects);
    }
}

//////////////////////////////////////////////////////////////////////////
// Paints the area legend (swatch and name per area), the hint (dim, D45), the "Other keys" strip (caption, then a
// tinted cap per key off the layout) and the "Unbound" line (caption, then the actions' names)
void SSOLKeyboardDiagram::PaintSections(const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId,
    const FLinearColor& styleTint, const ESlateDrawEffect effects) const
{
    using namespace SOL::Help;
    const float scale = geometry.Scale;
    const FLinearColor text = SOLSolidStyle::TextColor() * styleTint;
    const FLinearColor dim = SOLSolidStyle::DimTextColor() * styleTint;
    const FLinearColor keyText = SOLSolidStyle::Linear(DIAGRAM_KEY_TEXT_COLOR) * styleTint;   // D49

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

    // D45: the hint line, always shown (the hover detail is the popup)
    if (mShapedHint.IsValid())
    {
        PaintText(mShapedHint, mPanel.Hint.Min.X, mPanel.Hint.Min.Y, mPanel.Hint.GetSize().Y, geometry, out,
            layerId + 1, dim, effects);
        mPaintedHint = true;
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
            const float nameWidth = SOLKeyCapPaint::ShapedWidth(mShapedOverflow[index], scale);
            const float labelWidth = SOLKeyCapPaint::ShapedWidth(mShapedOverflowLabels[index], scale);
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
            PaintText(mShapedOverflow[index], cap.Min.X + leftPad, cap.Min.Y, half, geometry, out, layerId + 1, keyText,
                effects);
            PaintText(mShapedOverflowLabels[index], cap.Min.X + leftPad, cap.Min.Y + half, half, geometry, out,
                layerId + 1, keyText, effects);
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
// D45: paints the hovered key's popup in the popup layer's geometry: the key's box and the layer's bounds (less the
// edge pad) in the layer's space, the popup sized and placed (PopupSize, PlacePopup), then its hull panel and content.
// Shapes the popup's texts only when the hover or the scale changed; nothing is allocated otherwise
int32 SSOLKeyboardDiagram::PaintPopup(const FGeometry& layerGeometry, FSlateWindowElementList& out,
    const int32 layerId, const FWidgetStyle& widgetStyle, const bool bParentEnabled) const
{
    using namespace SOL::Help;
    mPaintedDetailRows = 0;
    mPaintedDetailCap = false;
    // Only right after this leaf's own paint in the same frame (a second Slate paint in one engine frame, or a frame
    // the leaf was not painted in, such as another tab, shows nothing)
    const bool bPending = mPopupPending;
    mPopupPending = false;
    if (!bPending || !mHasStyle || mDetailLines.IsEmpty() || mPaintFrame != GFrameCounter
        || !(layerGeometry.Scale > 0.0f))
    {
        return layerId;
    }
    const bool bAnchor = mMainOf.IsValidIndex(mHoverAnchor) && mMainOf[mHoverAnchor] == mHoveredKey;
    const FBox2f keyLocal = KeyBox(bAnchor ? mHoverAnchor : mHoveredKey);
    if (!keyLocal.bIsValid)
    {
        return layerId;
    }
    if (layerGeometry.Scale != mWrapScale)
    {
        WrapDetail(layerGeometry.Scale);
    }
    if (layerGeometry.Scale != mShapedDetailScale)
    {
        ReshapeDetail(layerGeometry.Scale);
    }

    // The key's box and the bounds in the layer's space (through absolute space: the scroll offset is in the leaf's)
    const FVector2f keyMin(layerGeometry.AbsoluteToLocal(mPaintGeometry.LocalToAbsolute(FVector2D(keyLocal.Min))));
    const FVector2f keyMax(layerGeometry.AbsoluteToLocal(mPaintGeometry.LocalToAbsolute(FVector2D(keyLocal.Max))));
    const FVector2f edge(DIAGRAM_POPUP_EDGE_PAD, DIAGRAM_POPUP_EDGE_PAD);
    const FBox2f bounds(edge, FVector2f(layerGeometry.GetLocalSize()) - edge);
    const FSOLPopupPlacement placement = SOLKeyboardLayout::PlacePopup(FBox2f(keyMin, keyMax),
        SOLKeyboardLayout::PopupSize(mShapedContentWidth, mDetailRows.Num()), bounds, DIAGRAM_POPUP_GAP);
    if (!placement.Rect.bIsValid)
    {
        return layerId;
    }

    // The hull panel, then the content inside its pad (the cap's hull a layer above the panel, the texts above that)
    const ESlateDrawEffect effects = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
    const FLinearColor styleTint = widgetStyle.GetColorAndOpacityTint();
    const FBox2f rect = placement.Rect;
    FSlateDrawElement::MakeBox(out, layerId, layerGeometry.ToPaintGeometry(rect.GetSize(),
        FSlateLayoutTransform(rect.Min)), &mPopupBrush, effects, SOLSolidStyle::BrushTint(FSOLSolidButtonLook())
        * styleTint);
    const FVector2f pad(DIAGRAM_POPUP_PAD, DIAGRAM_POPUP_PAD);
    PaintDetail(FBox2f(rect.Min + pad, rect.Max - pad), layerGeometry, out, layerId + 1, styleTint, effects);

    // The verification record, in the layer's local space (paint space is the window's, not the desktop's)
    mPopupFrame = GFrameCounter;
    mPopupSide = placement.Side;
    mPopupRect = rect;
    mPopupKeyRect = FBox2f(keyMin, keyMax);
    mPopupBounds = bounds;
    return layerId + 3;
}

//////////////////////////////////////////////////////////////////////////
// Paints the popup's content in a box (D36 / D45): the cap row on top (the hovered key's cap), then per visual row, on
// a line's first row the area swatch and the keys (cyan), then the row's action text. Rows past DIAGRAM_DETAIL_LINES
// are not drawn (PopupSize sizes the popup for that many at most)
void SSOLKeyboardDiagram::PaintDetail(const FBox2f& content, const FGeometry& geometry, FSlateWindowElementList& out,
    const int32 layerId, const FLinearColor& styleTint, const ESlateDrawEffect effects) const
{
    using namespace SOL::Help;
    if (mDetailCapWidth > 0.0f)
    {
        PaintDetailCap(FVector2f(content.Min.X, content.Min.Y + 0.5f * (DIAGRAM_DETAIL_CAP_HEIGHT - KEYCAP_HEIGHT)),
            geometry, out, layerId, styleTint, effects);
    }
    const float linesTop = content.Min.Y + DIAGRAM_DETAIL_CAP_HEIGHT;
    const float textLeft = content.Min.X + DIAGRAM_LEGEND_SWATCH + 2.0f * DIAGRAM_TEXT_GAP + mShapedKeysWidth;

    // Each wrapped row up to the line cap: the swatch and keys on a line's first row, then its text
    for (int32 index = 0; index < mDetailRows.Num() && index < DIAGRAM_DETAIL_LINES; ++index)
    {
        const FDetailRow& row = mDetailRows[index];
        const float rowTop = linesTop + static_cast<float>(index) * DIAGRAM_DETAIL_LINE_HEIGHT;
        if (!mShapedDetailText.IsValidIndex(index) || !mDetailLines.IsValidIndex(row.Line))
        {
            break;
        }
        if (row.bFirst)
        {
            const FSOLDiagramDetailLine& line = mDetailLines[row.Line];
            const float swatchTop = rowTop + 0.5f * (DIAGRAM_DETAIL_LINE_HEIGHT - DIAGRAM_LEGEND_SWATCH);
            PaintBox(FBox2f(FVector2f(content.Min.X, swatchTop), FVector2f(content.Min.X + DIAGRAM_LEGEND_SWATCH,
                swatchTop + DIAGRAM_LEGEND_SWATCH)), geometry, out, layerId + 1,
                SOLSolidStyle::Linear(SOLKeyDiagram::AreaColor(line.Area)) * styleTint, effects);
            if (mShapedDetailKeys.IsValidIndex(index))
            {
                PaintText(mShapedDetailKeys[index], content.Min.X + DIAGRAM_LEGEND_SWATCH + DIAGRAM_TEXT_GAP, rowTop,
                    DIAGRAM_DETAIL_LINE_HEIGHT, geometry, out, layerId + 1, SOLSolidStyle::ValueColor() * styleTint,
                    effects);
            }
        }
        PaintText(mShapedDetailText[index], textLeft, rowTop, DIAGRAM_DETAIL_LINE_HEIGHT, geometry, out, layerId + 1,
            SOLSolidStyle::TextColor() * styleTint, effects);
        ++mPaintedDetailRows;
    }
}

//////////////////////////////////////////////////////////////////////////
// Paints the hover popup's cap of the hovered key with the shared cap painter (SOLKeyCapPaint::PaintCap, so it looks
// like every other key cap, D3): the Small hull KEYCAP_HEIGHT tall, then the mouse glyph and the legend, centred right
// of the hull's LED
void SSOLKeyboardDiagram::PaintDetailCap(const FVector2f& topLeft, const FGeometry& geometry,
    FSlateWindowElementList& out, const int32 layerId, const FLinearColor& styleTint,
    const ESlateDrawEffect effects) const
{
    FSOLKeyCapPaintStyle style;
    style.Hull = &mCapBrush;
    style.HullTint = SOLSolidStyle::BrushTint(FSOLSolidButtonLook()) * styleTint;
    style.Line = &mFlatBrush;
    style.LegendColor = SOLSolidStyle::TextColor() * styleTint;
    style.StyleTint = styleTint;
    style.Effects = effects;
    SOLKeyCapPaint::PaintCap(mDetailCap, topLeft, mDetailCapWidth, &mShapedDetailCap, geometry, out, layerId, style);
    mPaintedDetailCap = mCapBrush.GetResourceObject() != nullptr
        && (mShapedDetailCap.IsValid() || SOLKeyCaps::DrawnLegend(mDetailCap).IsEmpty());
}

//////////////////////////////////////////////////////////////////////////
// Paints a shaped text at a local x, vertically centred in a line (SOLKeyCapPaint::PaintText); returns its width
float SSOLKeyboardDiagram::PaintText(const FShapedGlyphSequencePtr& text, const float x, const float lineTop,
    const float lineHeight, const FGeometry& geometry, FSlateWindowElementList& out, const int32 layerId,
    const FLinearColor& color, const ESlateDrawEffect effects) const
{
    return SOLKeyCapPaint::PaintText(text, x, lineTop, lineHeight, geometry, out, layerId, color, effects);
}

//////////////////////////////////////////////////////////////////////////
// Draws a filled box in local units with the flat brush
void SSOLKeyboardDiagram::PaintBox(const FBox2f& box, const FGeometry& geometry, FSlateWindowElementList& out,
    const int32 layerId, const FLinearColor& color, const ESlateDrawEffect effects) const
{
    SOLKeyCapPaint::Fill(box, geometry, out, layerId, &mFlatBrush, effects, color);
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
    const float width = FMath::Max3(mPanel.Legend.Max.X, mPanel.Diagram.Max.X, mPanel.Hint.Max.X);
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
// Lays the empty panel out at width 0
USOLKeyboardDiagram::USOLKeyboardDiagram()
    : mPanel(SOLKeyboardLayout::LayoutPanel(0.0f, false, false))
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
// Lays the panel out with the model's Other keys and Unbound sections (on a model or width change, never on a hover)
// and hands it to the leaf
void USOLKeyboardDiagram::LayoutPanel()
{
    mPanel = SOLKeyboardLayout::LayoutPanel(mLayoutWidth, mModel.Overflow.Num() > 0, mModel.Unbound.Num() > 0);
    if (mDiagram.IsValid())
    {
        mDiagram->SetPanel(mPanel);
    }
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
    // D45: the popup goes by the cap given (the numpad Enter, a mirror, is not the main Enter's cap)
    const int32 anchor = hovered != INDEX_NONE ? key : INDEX_NONE;
    if (anchor != mHoverAnchor)
    {
        mHoverAnchor = anchor;
        if (mDiagram.IsValid())
        {
            mDiagram->SetHoverAnchor(mHoverAnchor);
        }
    }
    if (hovered == mHoveredKey)
    {
        return;
    }
    mHoveredKey = hovered;
    RefreshHover();
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the hovered key's popup lines (empty with no hover) and hands the hover to the leaf
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
// Returns the leaf's wrapped hover popup rows (0 without the leaf)
int32 USOLKeyboardDiagram::GetDetailRowCount() const
{
    return mDiagram.IsValid() ? mDiagram->GetDetailRowCount() : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the leaf's hover popup cap width (0 without the leaf)
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
// the texts the Small role), the popup's hull panel (D45) and hands it the model, the panel, the hover and the mouse
// handlers
TSharedRef<SWidget> USOLKeyboardDiagram::RebuildWidget()
{
    using namespace SOL::Help;
    USOLSolidMenuSubsystem* subsystem = USOLSolidMenuSubsystem::Get(this);
    mDiagram = SNew(SSOLKeyboardDiagram);
    mDiagram->SetClockSource(subsystem);
    mDiagram->SetModel(mModel);
    mDiagram->SetPanel(mPanel);
    mDiagram->SetHover(mHoveredKey, mDetailLines, GetEffectsStrength());
    mDiagram->SetHoverAnchor(mHoverAnchor);

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

    // The fonts: key legends Rajdhani SemiBold, short labels Medium, the badge, the texts' Small role and the 33b cap
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
    const FSlateBrush popupBrush = SOLSolidStyle::MakeHullBrush(subsystem, ESOLSolidStyle::Panel,
        FVector2f(DIAGRAM_POPUP_MAX_WIDTH, DIAGRAM_DETAIL_CAP_HEIGHT), FSOLSolidButtonLook());
    mDiagram->SetStyle(capBrush, keyBrush, popupBrush, fonts);
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

//////////////////////////////////////////////////////////////////////////
// Makes the popup layer tickless, hit-test invisible (it never takes hover from the keys) and volatile
void SSOLKeyboardDiagramPopupLayer::Construct(const FArguments& /*args*/)
{
    SetCanTick(false);
    SetVisibility(EVisibility::HitTestInvisible);
    ForceVolatile(true);    // Painted every frame even under an invalidation panel: the hover lives in the diagram
}

//////////////////////////////////////////////////////////////////////////
// Sets the diagram whose popup the layer paints
void SSOLKeyboardDiagramPopupLayer::SetDiagram(USOLKeyboardDiagram* diagram)
{
    mDiagram = diagram;
}

//////////////////////////////////////////////////////////////////////////
// Paints the diagram leaf's popup in this layer's geometry (nothing without a diagram or its leaf)
int32 SSOLKeyboardDiagramPopupLayer::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& widgetStyle, const bool bParentEnabled) const
{
    const USOLKeyboardDiagram* diagram = mDiagram.Get();
    const SSOLKeyboardDiagram* leaf = diagram != nullptr ? diagram->GetLeaf().Get() : nullptr;
    return leaf != nullptr ? leaf->PaintPopup(allottedGeometry, outDrawElements, layerId, widgetStyle,
        ShouldBeEnabled(bParentEnabled)) : layerId;
}

//////////////////////////////////////////////////////////////////////////
// The layer takes no room in the page's layout
FVector2D SSOLKeyboardDiagramPopupLayer::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    return FVector2D::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Makes the widget hit-test invisible
USOLKeyboardDiagramPopup::USOLKeyboardDiagramPopup()
{
    SetVisibilityInternal(ESlateVisibility::HitTestInvisible);
}

//////////////////////////////////////////////////////////////////////////
// Sets the diagram whose popup the layer paints (handed to a built layer at once)
void USOLKeyboardDiagramPopup::SetDiagram(USOLKeyboardDiagram* diagram)
{
    Diagram = diagram;
    if (mLayer.IsValid())
    {
        mLayer->SetDiagram(Diagram);
    }
}

//////////////////////////////////////////////////////////////////////////
// Drops the layer
void USOLKeyboardDiagramPopup::ReleaseSlateResources(const bool bReleaseChildren)
{
    Super::ReleaseSlateResources(bReleaseChildren);
    mLayer.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Builds the layer and hands it the diagram
TSharedRef<SWidget> USOLKeyboardDiagramPopup::RebuildWidget()
{
    mLayer = SNew(SSOLKeyboardDiagramPopupLayer);
    mLayer->SetDiagram(Diagram);
    return mLayer.ToSharedRef();
}
