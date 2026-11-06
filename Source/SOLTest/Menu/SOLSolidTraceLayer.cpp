/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidTraceLayer.h"

#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "Menu/SOLSolidScroll.h"
#include "Menu/SOLSolidStyle.h"

#include "Rendering/DrawElements.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns whether a trace has a drawable point count
    bool TraceIsDrawable(const FSOLSolidTrace& trace)
    {
        return trace.Count >= 2 && trace.Count <= FSOLSolidTrace::MAX_POINTS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two traces have the same points and terminal
    bool TraceEquals(const FSOLSolidTrace& a, const FSOLSolidTrace& b)
    {
        if (a.Count != b.Count || a.bRingTerminal != b.bRingTerminal)
        {
            return false;
        }
        for (int32 i = 0; i < a.Count && i < FSOLSolidTrace::MAX_POINTS; ++i)
        {
            if (!a.Points[i].Equals(b.Points[i], 0.0f))
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a trace's polyline length (0 when it is not drawable)
    float TraceLength(const FSOLSolidTrace& trace)
    {
        float length = 0.0f;
        for (int32 i = 1; TraceIsDrawable(trace) && i < trace.Count; ++i)
        {
            length += FVector2f::Distance(trace.Points[i - 1], trace.Points[i]);
        }
        return length;
    }
}

//////////////////////////////////////////////////////////////////////////
// Computes the ring terminal's octagon offsets once and makes the layer hit-test invisible
void SSOLSolidTraceLayer::Construct(const FArguments& /*args*/)
{
    for (int32 point = 0; point < RING_POINTS; ++point)
    {
        const float angle = UE_TWO_PI * static_cast<float>(point) / static_cast<float>(RING_SEGMENTS);
        mRingOffsets[point] = SOL::SolidMenu::LED_RADIUS_PX * FVector2f(FMath::Cos(angle), FMath::Sin(angle));
    }
    SetVisibility(EVisibility::HitTestInvisible);
}

//////////////////////////////////////////////////////////////////////////
// Replaces the static traces (at most MAX_TRACES drawable ones)
void SSOLSolidTraceLayer::SetTraces(TConstArrayView<FSOLSolidTrace> traces)
{
    mTraceCount = 0;
    mLongestTrace = INDEX_NONE;
    float longest = 0.0f;
    for (const FSOLSolidTrace& trace : traces)
    {
        if (mTraceCount >= MAX_TRACES)
        {
            break;
        }
        if (TraceIsDrawable(trace))
        {
            // The pulse dot's trace is the longest one (the first of equals)
            const float length = TraceLength(trace);
            if (length > longest)
            {
                longest = length;
                mLongestTrace = mTraceCount;
            }
            mTraces[mTraceCount++] = trace;
        }
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

//////////////////////////////////////////////////////////////////////////
// Replaces the stack LEDs' centres (at most MAX_LEDS)
void SSOLSolidTraceLayer::SetLeds(TConstArrayView<FVector2f> centres)
{
    mLedCount = FMath::Min(centres.Num(), MAX_LEDS);
    for (int32 index = 0; index < mLedCount; ++index)
    {
        mLeds[index] = centres[index];
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

//////////////////////////////////////////////////////////////////////////
// Sets the LEDs the power-up blink lights, repainting only on a change
void SSOLSolidTraceLayer::SetLitLeds(const uint32 mask)
{
    if (mask != mLitLeds)
    {
        mLitLeds = mask;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the pulse dot's place and alpha; a hidden dot ignores the place, so it never repaints while hidden
void SSOLSolidTraceLayer::SetPulseDot(const float fraction, const float alpha)
{
    const float value = FMath::IsFinite(alpha) ? FMath::Clamp(alpha, 0.0f, 1.0f) : 0.0f;
    const float place = value > 0.0f && FMath::IsFinite(fraction) ? FMath::Clamp(fraction, 0.0f, 1.0f)
        : mDotFraction;
    if (value != mDotAlpha || place != mDotFraction)
    {
        mDotAlpha = value;
        mDotFraction = place;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the static traces' lit fraction (NaN reads 0), repainting only on a change
void SSOLSolidTraceLayer::SetPower(const float power)
{
    const float value = FMath::IsNaN(power) ? 0.0f : FMath::Clamp(power, 0.0f, 1.0f);
    if (value != mPower)
    {
        mPower = value;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the static traces' brightness factor (non-finite reads 1), repainting only on a change
void SSOLSolidTraceLayer::SetPulse(const float pulse)
{
    const float value = FMath::IsFinite(pulse) ? pulse : 1.0f;
    if (value != mPulse)
    {
        mPulse = value;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the sanitized effects strength, repainting only on a change
void SSOLSolidTraceLayer::SetStrength(const float strength)
{
    const float value = FSOLSolidMenuParamCache::SanitizeStrength(strength);
    if (value != mStrength)
    {
        mStrength = value;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets the hovered button's trace and amount, repainting only on a change
void SSOLSolidTraceLayer::SetHoverTrace(const FSOLSolidTrace& trace, const float amount)
{
    const float value = TraceIsDrawable(trace) && !FMath::IsNaN(amount) ? FMath::Clamp(amount, 0.0f, 1.0f) : 0.0f;
    if (value == mHoverAmount && (value == 0.0f || TraceEquals(trace, mHoverTrace)))
    {
        return;
    }
    mHoverAmount = value;
    if (value > 0.0f)
    {
        mHoverTrace = trace;
    }
    Invalidate(EInvalidateWidgetReason::Paint);
}

//////////////////////////////////////////////////////////////////////////
// Hides the hover trace
void SSOLSolidTraceLayer::ClearHoverTrace()
{
    if (mHoverAmount != 0.0f)
    {
        mHoverAmount = 0.0f;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a stack line shows at the stack's scroll (always without a view band)
bool SSOLSolidTraceLayer::IsStackLineShown(const float y) const
{
    return mViewHeight <= 0.0f || SOLSolidScroll::IsLineInView(y, mScrollOffset, mViewTop, mViewHeight);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the paint draws the hover trace (set, and its line in the view band)
bool SSOLSolidTraceLayer::IsHoverTraceDrawn() const
{
    return mHoverAmount > 0.0f && IsStackLineShown(mHoverTrace.Points[0].Y);
}

//////////////////////////////////////////////////////////////////////////
// Sets the hover trace's and LEDs' stack scroll and view band (non-finite reads 0), repainting only on a change
void SSOLSolidTraceLayer::SetScroll(const float offsetY, const float viewTop, const float viewHeight)
{
    const float offset = FMath::IsFinite(offsetY) ? offsetY : 0.0f;
    const float top = FMath::IsFinite(viewTop) ? viewTop : 0.0f;
    const float height = FMath::IsFinite(viewHeight) ? FMath::Max(viewHeight, 0.0f) : 0.0f;
    if (offset != mScrollOffset || top != mViewTop || height != mViewHeight)
    {
        mScrollOffset = offset;
        mViewTop = top;
        mViewHeight = height;
        Invalidate(EInvalidateWidgetReason::Paint);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the shown hover trace is this one (a button leaving hover clears only its own)
bool SSOLSolidTraceLayer::IsShowingHoverTrace(const FSOLSolidTrace& trace) const
{
    return mHoverAmount > 0.0f && TraceEquals(trace, mHoverTrace);
}

//////////////////////////////////////////////////////////////////////////
// Returns the glow pass alpha: TRACE_GLOW_ALPHA x min(s, HUD_EFFECTS_MAX) x pulse in [0, 1], 0 at s <= 0
float SSOLSolidTraceLayer::GlowPassAlpha(const float strength, const float pulse)
{
    if (FMath::IsNaN(strength) || strength <= 0.0f)
    {
        return 0.0f;
    }
    const float factor = FMath::IsFinite(pulse) ? pulse : 1.0f;
    return FMath::Clamp(SOL::SolidMenu::TRACE_GLOW_ALPHA * FMath::Min(strength, SOL::Settings::HUD_EFFECTS_MAX)
        * factor, 0.0f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Returns the core pass alpha: (1 + pulse - IDLE_PULSE_DEPTH x s) / 2 at a strength above 0, the pulse itself at 0,
// clamped to [0, 1] (non-finite reads 1)
float SSOLSolidTraceLayer::CorePassAlpha(const float pulse, const float strength)
{
    if (!FMath::IsFinite(pulse))
    {
        return 1.0f;
    }
    if (FMath::IsNaN(strength) || strength <= 0.0f)
    {
        return FMath::Clamp(pulse, 0.0f, 1.0f);
    }
    const float s = FMath::Min(strength, SOL::Settings::HUD_EFFECTS_MAX);
    return FMath::Clamp(0.5f * (1.0f + pulse - SOL::SolidMenu::IDLE_PULSE_DEPTH * s), 0.0f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Draws the glow and core passes of every lit trace and the hover trace
int32 SSOLSolidTraceLayer::OnPaint(const FPaintArgs& /*args*/, const FGeometry& allottedGeometry,
    const FSlateRect& /*cullingRect*/, FSlateWindowElementList& outDrawElements, const int32 layerId,
    const FWidgetStyle& widgetStyle, const bool bParentEnabled) const
{
    using namespace SOL::SolidMenu;
    const ESlateDrawEffect effect = bParentEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;
    const FPaintGeometry geometry = allottedGeometry.ToPaintGeometry();
    const float opacity = widgetStyle.GetColorAndOpacityTint().A;

    // Draws one trace's lit part (and its ring terminal) as the glow and core passes; each line element owns its
    // points, so each gets one exact-size array, moved in (MakeLines takes the array by value)
    const auto drawTrace = [&](const FSOLSolidTrace& trace, const float fraction, const FLinearColor& color,
        const float coreAlpha, const float glowAlpha)
    {
        FVector2f lit[FSOLSolidTrace::MAX_POINTS];
        const int32 count = SOLSolidMenu::TracePolyline(trace.Points, trace.Count, fraction, lit,
            FSOLSolidTrace::MAX_POINTS);
        if (count < 2)
        {
            return;
        }
        if (glowAlpha > 0.0f)
        {
            FSlateDrawElement::MakeLines(outDrawElements, layerId, geometry, TArray<FVector2f>(lit, count), effect,
                color.CopyWithNewOpacity(glowAlpha * opacity), true, TRACE_GLOW_WIDTH_PX);
        }
        FSlateDrawElement::MakeLines(outDrawElements, layerId + 1, geometry, TArray<FVector2f>(lit, count), effect,
            color.CopyWithNewOpacity(coreAlpha * opacity), true, TRACE_CORE_WIDTH_PX);
        if (!trace.bRingTerminal)
        {
            return;
        }

        // The ring terminal at the trace's first point: the precomputed octagon offset to it
        TArray<FVector2f> ring;
        ring.SetNumUninitialized(RING_POINTS);
        for (int32 point = 0; point < RING_POINTS; ++point)
        {
            ring[point] = trace.Points[0] + mRingOffsets[point];
        }
        FSlateDrawElement::MakeLines(outDrawElements, layerId + 1, geometry, MoveTemp(ring), effect,
            color.CopyWithNewOpacity(coreAlpha * opacity), true, TRACE_CORE_WIDTH_PX);
    };

    // Draws a filled dot of LED_RADIUS_PX at a point as two concentric octagons in one polyline at the core width (an
    // inner one around the centre, an outer one filling the rest), so it batches with the core lines (the same line
    // thickness) instead of costing a draw of its own
    const auto drawDot = [&](const FVector2f& centre, const FLinearColor& color, const float alpha)
    {
        const float inner = 0.5f * TRACE_CORE_WIDTH_PX / LED_RADIUS_PX;
        const float outer = (LED_RADIUS_PX - 0.5f * TRACE_CORE_WIDTH_PX) / LED_RADIUS_PX;
        TArray<FVector2f> dot;
        dot.SetNumUninitialized(2 * RING_POINTS);
        for (int32 point = 0; point < RING_POINTS; ++point)
        {
            dot[point] = centre + inner * mRingOffsets[point];
            dot[RING_POINTS + point] = centre + outer * mRingOffsets[point];
        }
        FSlateDrawElement::MakeLines(outDrawElements, layerId + 1, geometry, MoveTemp(dot), effect,
            color.CopyWithNewOpacity(alpha * opacity), true, TRACE_CORE_WIDTH_PX);
    };

    const FLinearColor cyan = SOLSolidStyle::Linear(CYAN_COLOR);
    const float coreAlpha = CorePassAlpha(mPulse, mStrength);
    const float glowAlpha = GlowPassAlpha(mStrength, mPulse);
    for (int32 index = 0; index < mTraceCount; ++index)
    {
        drawTrace(mTraces[index], mPower, cyan, coreAlpha, glowAlpha);
    }

    // The idle pulse dot rides the longest trace once it is fully lit: the end point of its first fraction
    if (mDotAlpha > 0.0f && mPower >= 1.0f && mLongestTrace != INDEX_NONE
        && mLongestTrace < mTraceCount)
    {
        const FSOLSolidTrace& trace = mTraces[mLongestTrace];
        FVector2f lit[FSOLSolidTrace::MAX_POINTS];
        const int32 count = SOLSolidMenu::TracePolyline(trace.Points, trace.Count, mDotFraction, lit,
            FSOLSolidTrace::MAX_POINTS);
        drawDot(count >= 2 ? lit[count - 1] : trace.Points[0], cyan, mDotAlpha * coreAlpha);
    }

    // The stack's hover trace and LEDs are clipped to its view band while one is set, as its items are (a half
    // scrolled-out item's trace and LED end at the view's edge); the static traces above are not
    const bool bClipToBand = mViewHeight > 0.0f;
    if (bClipToBand)
    {
        const FGeometry band = allottedGeometry.MakeChild(FVector2f(allottedGeometry.GetLocalSize().X, mViewHeight),
            FSlateLayoutTransform(FVector2f(0.0f, mViewTop)));
        outDrawElements.PushClip(FSlateClippingZone(band));
    }

    // The hover trace, shifted with its stack's scroll (a copy on the stack, no heap)
    const FLinearColor amber = SOLSolidStyle::Linear(AMBER_COLOR);
    if (IsHoverTraceDrawn())
    {
        FSOLSolidTrace shifted = mHoverTrace;
        for (int32 point = 0; point < shifted.Count && point < FSOLSolidTrace::MAX_POINTS; ++point)
        {
            shifted.Points[point].Y -= mScrollOffset;
        }
        drawTrace(shifted, mHoverAmount, amber, 1.0f, GlowPassAlpha(mStrength, 1.0f));
    }

    // The power-up blink: each lit stack LED in view as a cyan dot (user, 2026-11-06: amber stays for hover and
    // focus), shifted with the stack's scroll
    for (int32 index = 0; index < mLedCount; ++index)
    {
        if ((mLitLeds & (1u << index)) != 0 && IsStackLineShown(mLeds[index].Y))
        {
            drawDot(mLeds[index] - FVector2f(0.0f, mScrollOffset), cyan, 1.0f);
        }
    }
    if (bClipToBand)
    {
        outDrawElements.PopClip();
    }
    return layerId + 1;
}

//////////////////////////////////////////////////////////////////////////
// A layer takes no room of its own
FVector2D SSOLSolidTraceLayer::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    return FVector2D::ZeroVector;
}
