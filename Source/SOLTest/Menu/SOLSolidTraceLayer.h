/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"

// One circuit trace (SDD 46 sections 5.2 and 7): a polyline in the layer's local UMG units, lit from its first point
// (a ring terminal there when bRingTerminal) toward its last; fixed storage, no heap
struct FSOLSolidTrace
{
    static constexpr int32 MAX_POINTS = 8;

    FVector2f Points[MAX_POINTS];
    int32 Count = 0;                // Points used, 0 or 2..MAX_POINTS
    bool bRingTerminal = true;
};

/**
 * A panel's circuit traces (SDD 46 sections 4.2, 5.2 and 7): a hit-test-invisible leaf widget over a USOLSolidPanel's
 * content. It draws the panel's static traces (power-up fraction from SOLSolidMenu::TracePower, brightness from
 * SOLSolidMenu::IdlePulse) and the hovered button's extending trace (its amount from the button's look, Trace), each
 * as FSlateDrawElement::MakeLines in CYAN_COLOR (the hover trace in AMBER_COLOR): a wide faint glow pass
 * (TRACE_GLOW_WIDTH_PX, only at strength above 0) and a thin core pass (TRACE_CORE_WIDTH_PX), the partial polylines
 * cut by SOLSolidMenu::TracePolyline into fixed buffers, plus a ring terminal per trace (its octagon offsets computed
 * once in Construct). No tick, no timers. Setters invalidate paint only when a value changes, which saves work only
 * where the layer is cached (an invalidation panel or global invalidation; the menus use neither, so Slate repaints
 * the layer every frame like its other widgets, at strength 0 too, the content then being unchanged). Each line
 * element owns its points (FSlateDrawElement::MakeLines takes them by value), so a paint makes one exact-size array
 * per line it draws (glow, core and ring: at most three per trace), moved into the draw list without a further copy.
 */
class SOLTEST_API SSOLSolidTraceLayer : public SLeafWidget
{
public:

    static constexpr int32 MAX_TRACES = 8;
    static constexpr int32 RING_SEGMENTS = 8;                   // A ring terminal is an octagon
    static constexpr int32 RING_POINTS = RING_SEGMENTS + 1;     // Closed: the first point repeated

    SLATE_BEGIN_ARGS(SSOLSolidTraceLayer) {}
    SLATE_END_ARGS()

    // Computes the ring terminal's offsets once and makes the layer hit-test invisible
    void Construct(const FArguments& args);

    // Replaces the static traces (copied; at most MAX_TRACES, the rest ignored; a trace with fewer than 2 points is
    // skipped)
    void SetTraces(TConstArrayView<FSOLSolidTrace> traces);

    // Sets the static traces' lit fraction, [0, 1] (SOLSolidMenu::TracePower); NaN reads 0
    void SetPower(float power);

    // Sets the static traces' brightness factor (SOLSolidMenu::IdlePulse); non-finite reads 1
    void SetPulse(float pulse);

    // Sets the effects strength (sanitized as FSOLSolidMenuParamCache::SanitizeStrength): 0 draws the core pass only
    void SetStrength(float strength);

    // Sets the hovered button's trace and its amount in [0, 1] (the button look's Trace; 0 hides it)
    void SetHoverTrace(const FSOLSolidTrace& trace, float amount);

    // Hides the hover trace
    void ClearHoverTrace();

    // Returns how many static traces are set
    int32 GetTraceCount() const { return mTraceCount; }

    // Returns the static traces' lit fraction
    float GetPower() const { return mPower; }

    // Returns the hover trace's amount (0: hidden)
    float GetHoverAmount() const { return mHoverAmount; }

    // Returns whether the shown hover trace is this one (a button leaving hover clears only its own)
    bool IsShowingHoverTrace(const FSOLSolidTrace& trace) const;

    // Returns the glow pass alpha for a strength and pulse: TRACE_GLOW_ALPHA x min(s, 2) x pulse, 0 at s <= 0, in
    // [0, 1] (doubled at strength 2, section 7)
    static float GlowPassAlpha(float strength, float pulse);

    // Returns the core pass alpha for a pulse: the pulse clamped to [0, 1] (non-finite reads 1)
    static float CorePassAlpha(float pulse);

    // Draws the glow and core passes of every lit trace and the hover trace
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // A layer takes no room of its own (it overlays its panel)
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

private:

    FSOLSolidTrace mTraces[MAX_TRACES];
    FSOLSolidTrace mHoverTrace;
    FVector2f mRingOffsets[RING_POINTS];        // LED_RADIUS_PX octagon around a trace's first point
    int32 mTraceCount = 0;
    float mPower = 1.0f;
    float mPulse = 1.0f;
    float mStrength = SOL::Settings::HUD_EFFECTS_DEFAULT;
    float mHoverAmount = 0.0f;
};
