/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidTraceLayer.h"

#include "Menu/SOLSolidMenuMath.h"
#include "Menu/SOLSolidMenuSubsystem.h"
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
    for (const FSOLSolidTrace& trace : traces)
    {
        if (mTraceCount >= MAX_TRACES)
        {
            break;
        }
        if (TraceIsDrawable(trace))
        {
            mTraces[mTraceCount++] = trace;
        }
    }
    Invalidate(EInvalidateWidgetReason::Paint);
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
// Returns whether the shown hover trace is this one (a button leaving hover clears only its own)
bool SSOLSolidTraceLayer::IsShowingHoverTrace(const FSOLSolidTrace& trace) const
{
    return mHoverAmount > 0.0f && TraceEquals(trace, mHoverTrace);
}

//////////////////////////////////////////////////////////////////////////
// Returns the glow pass alpha: TRACE_GLOW_ALPHA x min(s, 2) x pulse in [0, 1], 0 at s <= 0
float SSOLSolidTraceLayer::GlowPassAlpha(const float strength, const float pulse)
{
    if (FMath::IsNaN(strength) || strength <= 0.0f)
    {
        return 0.0f;
    }
    const float factor = FMath::IsFinite(pulse) ? pulse : 1.0f;
    return FMath::Clamp(SOL::SolidMenu::TRACE_GLOW_ALPHA * FMath::Min(strength, 2.0f) * factor, 0.0f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Returns the core pass alpha: the pulse clamped to [0, 1] (non-finite reads 1)
float SSOLSolidTraceLayer::CorePassAlpha(const float pulse)
{
    return FMath::IsFinite(pulse) ? FMath::Clamp(pulse, 0.0f, 1.0f) : 1.0f;
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

    const FLinearColor cyan = SOLSolidStyle::Linear(CYAN_COLOR);
    const float coreAlpha = CorePassAlpha(mPulse);
    const float glowAlpha = GlowPassAlpha(mStrength, mPulse);
    for (int32 index = 0; index < mTraceCount; ++index)
    {
        drawTrace(mTraces[index], mPower, cyan, coreAlpha, glowAlpha);
    }
    if (mHoverAmount > 0.0f)
    {
        drawTrace(mHoverTrace, mHoverAmount, SOLSolidStyle::Linear(AMBER_COLOR), 1.0f, GlowPassAlpha(mStrength, 1.0f));
    }
    return layerId + 1;
}

//////////////////////////////////////////////////////////////////////////
// A layer takes no room of its own
FVector2D SSOLSolidTraceLayer::ComputeDesiredSize(const float /*layoutScaleMultiplier*/) const
{
    return FVector2D::ZeroVector;
}
