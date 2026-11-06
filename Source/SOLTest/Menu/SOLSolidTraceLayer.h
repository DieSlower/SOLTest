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

    // (46g) Returns a trace through up to MAX_POINTS of the given points (the rest ignored), in the layer's local units
    static FSOLSolidTrace FromPoints(std::initializer_list<FVector2f> points, const bool bRingTerminal)
    {
        FSOLSolidTrace trace;
        for (const FVector2f& point : points)
        {
            if (trace.Count < MAX_POINTS)
            {
                trace.Points[trace.Count++] = point;
            }
        }
        trace.bRingTerminal = bRingTerminal;
        return trace;
    }
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
 * (46d) It also draws the stack LEDs' power-up blink (cyan dots, 46i, at the LED centres it is given, the lit ones a bit
 * mask, SOLSolidMenu::PowerUpLedLit) and the idle pulse dot travelling along the longest static trace once the
 * traces are fully lit (SOLSolidMenu::PulseDotFraction / PulseDotAlpha): one more line array per dot drawn.
 * (46g) A scrolled stack under the layer (SetScroll): the hover trace and the LEDs are drawn shifted up by the scroll
 * offset, skipped when their line is outside the view band (SOLSolidScroll::IsLineInView) and clipped to the band
 * (a pushed clipping zone, so a half-hidden item's trace and LED end at the view's edge); the static traces are
 * neither scrolled nor clipped.
 */
class SOLTEST_API SSOLSolidTraceLayer : public SLeafWidget
{
public:

    static constexpr int32 MAX_TRACES = 8;
    static constexpr int32 MAX_LEDS = 8;                        // Stack LEDs the power-up blink can light
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

    // Sets the stack LEDs' centres for the power-up blink (copied; at most MAX_LEDS, the rest ignored)
    void SetLeds(TConstArrayView<FVector2f> centres);

    // Sets which LEDs the power-up blink lights (bit i: LED i; 0 none), repainting only on a change
    void SetLitLeds(uint32 mask);

    // Sets the idle pulse dot: its place along the longest trace, [0, 1], and its alpha (0 hides it and ignores the
    // place, so a hidden dot never repaints)
    void SetPulseDot(float fraction, float alpha);

    // Sets the scroll of the stack the hover trace and the LEDs belong to: they draw shifted up by offsetY, only when
    // their line is in the view band [viewTop, viewTop + viewHeight] (layer local), and clipped to that band; a
    // viewHeight <= 0 is no band (everything drawn, unclipped). Non-finite values read 0. Repaints only on a change
    void SetScroll(float offsetY, float viewTop, float viewHeight);

    // Returns the scroll offset last set (verification)
    float GetScrollOffset() const { return mScrollOffset; }

    // Returns whether the paint draws the hover trace: it is set with an amount above 0 and its line is in the view
    // band at the scroll (verification)
    bool IsHoverTraceDrawn() const;

    // Returns the y the hover trace's line is drawn at: its first point shifted up by the scroll (verification)
    float GetDrawnHoverTraceY() const { return mHoverTrace.Points[0].Y - mScrollOffset; }

    // Returns the LEDs the power-up blink lights (verification)
    uint32 GetLitLeds() const { return mLitLeds; }

    // Returns the idle pulse dot's alpha (0: hidden; verification)
    float GetPulseDotAlpha() const { return mDotAlpha; }

    // Returns the index of the longest static trace (the pulse dot's), or INDEX_NONE without traces
    int32 GetLongestTrace() const { return mLongestTrace; }

    // Returns how many static traces are set
    int32 GetTraceCount() const { return mTraceCount; }

    // Returns the static traces' lit fraction
    float GetPower() const { return mPower; }

    // Returns the hover trace's amount (0: hidden)
    float GetHoverAmount() const { return mHoverAmount; }

    // Returns whether the shown hover trace is this one (a button leaving hover clears only its own)
    bool IsShowingHoverTrace(const FSOLSolidTrace& trace) const;

    // Returns the glow pass alpha for a strength and pulse: TRACE_GLOW_ALPHA x min(s, HUD_EFFECTS_MAX) x pulse, 0 at
    // s <= 0, in [0, 1] (doubled at strength 2, section 7)
    static float GlowPassAlpha(float strength, float pulse);

    // Returns the core pass alpha for a pulse at a strength s = min(s, HUD_EFFECTS_MAX) above 0: (1 + pulse -
    // IDLE_PULSE_DEPTH x s) / 2, so over the pulse's swing [1 - depth x s, 1 + depth x s] the core line swings smoothly
    // in [1 - depth x s, 1] and reaches 1 at the pulse's top (section 7); at s <= 0 (or NaN) the pulse itself; clamped
    // to [0, 1]; a non-finite pulse reads 1
    static float CorePassAlpha(float pulse, float strength);

    // Draws the glow and core passes of every lit trace and the hover trace
    virtual int32 OnPaint(const FPaintArgs& args, const FGeometry& allottedGeometry, const FSlateRect& cullingRect,
        FSlateWindowElementList& outDrawElements, int32 layerId, const FWidgetStyle& widgetStyle,
        bool bParentEnabled) const override;

    // A layer takes no room of its own (it overlays its panel)
    virtual FVector2D ComputeDesiredSize(float layoutScaleMultiplier) const override;

private:

    // Returns whether a stack line (the hover trace's first point, an LED) shows at the stack's scroll: always without
    // a view band
    bool IsStackLineShown(float y) const;

    FSOLSolidTrace mTraces[MAX_TRACES];
    FSOLSolidTrace mHoverTrace;
    FVector2f mRingOffsets[RING_POINTS];        // LED_RADIUS_PX octagon around a trace's first point
    FVector2f mLeds[MAX_LEDS];                  // Stack LED centres (layer local)
    int32 mTraceCount = 0;
    int32 mLedCount = 0;
    int32 mLongestTrace = INDEX_NONE;           // The pulse dot's trace
    uint32 mLitLeds = 0;                        // Power-up blink: bit i lights mLeds[i]
    float mDotFraction = 0.0f;                  // Pulse dot: place along the longest trace
    float mDotAlpha = 0.0f;                     // ... and alpha (0: hidden)
    float mPower = 1.0f;
    float mPulse = 1.0f;
    float mStrength = SOL::Settings::HUD_EFFECTS_DEFAULT;
    float mHoverAmount = 0.0f;
    float mScrollOffset = 0.0f;                 // The hover trace's and LEDs' stack scroll (SetScroll)
    float mViewTop = 0.0f;                      // ... its view band's top (layer local)
    float mViewHeight = 0.0f;                   // ... and height (<= 0: no band)
};
