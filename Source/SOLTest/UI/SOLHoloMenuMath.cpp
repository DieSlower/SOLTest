/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloMenuMath.h"

#include "UI/SOLHudShapeMath.h"

namespace
{
    constexpr int32 HOLOMATH_MAX_GAPS = 8;                  // Merged gaps of one line (FRAME_GAPS / DIVIDER_GAPS)
    constexpr int32 HOLOMATH_MAX_PATH_POINTS = SOL::HoloMenu::FRAME_OUTLINE_POINTS + 1;  // The outline, closed
    constexpr int32 HOLOMATH_MAX_DASH_PASSES = SOL::HoloMenu::DIVIDER_MAX_DASHES + 2;  // Hard cap of DashedDivider's
                                                                                        // loop (dashes, skip, last)
    constexpr double HOLOMATH_SWEEPS_PER_WRAP =
        static_cast<double>(SOL::Hologram::HUD_TIME_WRAP_S) / SOL::HoloMenu::IDLE_SWEEP_PERIOD_S;

    static_assert(SOL::HoloMenu::FRAME_GAP_COUNT <= HOLOMATH_MAX_GAPS, "FRAME_GAPS outgrew the merged gap buffer");
    static_assert(SOL::HoloMenu::DIVIDER_GAP_COUNT <= HOLOMATH_MAX_GAPS, "DIVIDER_GAPS outgrew the merged gap buffer");
    static_assert(HOLOMATH_SWEEPS_PER_WRAP == static_cast<double>(static_cast<int64>(HOLOMATH_SWEEPS_PER_WRAP)),
        "SweepFraction is smooth across the HUD time wrap only when HUD_TIME_WRAP_S / IDLE_SWEEP_PERIOD_S is whole");

    // A polyline with its arc coordinate u at each point (u[0] = 0, u[last] = its length)
    struct FSOLHoloPath
    {
        FVector2f Points[HOLOMATH_MAX_PATH_POINTS];
        float U[HOLOMATH_MAX_PATH_POINTS];
        int32 NumPoints = 0;
    };

    // Gaps of a line merged into disjoint sorted [Start, End] intervals of u
    struct FSOLHoloMergedGaps
    {
        float Start[HOLOMATH_MAX_GAPS];
        float End[HOLOMATH_MAX_GAPS];
        int32 Count = 0;
    };

    // The option (c) outline of one rect: its closed path and the effective lengths of section 6.3
    struct FSOLHoloOutline
    {
        FSOLHoloPath Path;
        float K = 1.0f;         // Effective scale
        float Cut = 0.0f;       // c
        float Step = 0.0f;      // s
        float Title = 0.0f;     // T
    };

    // An output buffer of pieces with its capacity; Add skips degenerate pieces and never writes past the capacity
    struct FSOLHoloPieceWriter
    {
        FSOLHoloSegment* Out;
        int32 Capacity;
        int32 Count = 0;

        // Writes a -> b when it is at least SOL::HoloMenu::MIN_PIECE_PX long and there is room
        void Add(const FVector2f& a, const FVector2f& b)
        {
            if (Count < Capacity
                && FVector2f::DistSquared(a, b) >= SOL::HoloMenu::MIN_PIECE_PX * SOL::HoloMenu::MIN_PIECE_PX)
            {
                Out[Count].A = a;
                Out[Count].B = b;
                ++Count;
            }
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the effects strength clamped to the slider's range [0, 2], NaN reading 0
    float HoloMathStrength(const float s)
    {
        return FMath::IsNaN(s) ? 0.0f
            : FMath::Clamp(s, SOL::Settings::HOLO_MENU_EFFECTS_MIN, SOL::Settings::HOLO_MENU_EFFECTS_MAX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a value clamped to [0, 1], NaN reading 0
    float HoloMathUnit(const float value)
    {
        return FMath::IsNaN(value) ? 0.0f : FMath::Clamp(value, 0.0f, 1.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the UI scale clamped to [UI_SCALE_MIN, UI_SCALE_MAX], NaN, infinite or <= 0 reading 1
    float HoloMathScale(const float uiScale)
    {
        using namespace SOL::HoloMenu;
        return FMath::IsFinite(uiScale) && uiScale > 0.0f ? FMath::Clamp(uiScale, UI_SCALE_MIN, UI_SCALE_MAX) : 1.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a rect has four finite coordinates and a positive width and height
    bool HoloMathIsValidRect(const FBox2f& rect)
    {
        return FMath::IsFinite(rect.Min.X) && FMath::IsFinite(rect.Min.Y) && FMath::IsFinite(rect.Max.X)
            && FMath::IsFinite(rect.Max.Y) && rect.Max.X - rect.Min.X > 0.0f && rect.Max.Y - rect.Min.Y > 0.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the outline's effective scale k = min(uiScale, w / FRAME_MIN_W_PX, h / FRAME_MIN_H_PX)
    float HoloMathFrameScale(const FBox2f& rect, const float uiScale)
    {
        using namespace SOL::HoloMenu;
        const float w = rect.Max.X - rect.Min.X;
        const float h = rect.Max.Y - rect.Min.Y;
        return FMath::Min3(HoloMathScale(uiScale), w / FRAME_MIN_W_PX, h / FRAME_MIN_H_PX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a point to a path, extending its arc coordinate
    void HoloMathPathAdd(FSOLHoloPath& path, const FVector2f& point)
    {
        check(path.NumPoints < HOLOMATH_MAX_PATH_POINTS);
        path.Points[path.NumPoints] = point;
        path.U[path.NumPoints] = path.NumPoints == 0 ? 0.0f
            : path.U[path.NumPoints - 1] + FVector2f::Distance(path.Points[path.NumPoints - 1], point);
        ++path.NumPoints;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the path's total length
    float HoloMathPathLength(const FSOLHoloPath& path)
    {
        return path.NumPoints > 0 ? path.U[path.NumPoints - 1] : 0.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the point at u on edge e of a path (u clamped to the edge)
    FVector2f HoloMathPointOnEdge(const FSOLHoloPath& path, const int32 e, const float u)
    {
        const float length = path.U[e + 1] - path.U[e];
        const float t = length > 0.0f ? FMath::Clamp((u - path.U[e]) / length, 0.0f, 1.0f) : 0.0f;
        return FMath::Lerp(path.Points[e], path.Points[e + 1], t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the point at arc coordinate u on a path (the first edge that reaches u)
    FVector2f HoloMathPointAtU(const FSOLHoloPath& path, const float u)
    {
        for (int32 e = 0; e + 1 < path.NumPoints; ++e)
        {
            if (u <= path.U[e + 1])
            {
                return HoloMathPointOnEdge(path, e, u);
            }
        }
        return path.Points[path.NumPoints - 1];
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the part [a, b] of a path, one piece per edge it crosses, clockwise (A at the smaller u)
    void HoloMathEmitSpan(const FSOLHoloPath& path, const float a, const float b, FSOLHoloPieceWriter& writer)
    {
        for (int32 e = 0; e + 1 < path.NumPoints; ++e)
        {
            const float lo = FMath::Max(a, path.U[e]);
            const float hi = FMath::Min(b, path.U[e + 1]);
            if (hi - lo >= SOL::HoloMenu::MIN_PIECE_PX)
            {
                writer.Add(HoloMathPointOnEdge(path, e, lo), HoloMathPointOnEdge(path, e, hi));
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the part [a, b] of a path minus the merged gaps
    void HoloMathEmitVisible(const FSOLHoloPath& path, const FSOLHoloMergedGaps& gaps, const float a, const float b,
        FSOLHoloPieceWriter& writer)
    {
        // Walk the sorted gaps, writing the stretch before each one that overlaps [a, b], then what is left after them
        float cursor = a;
        for (int32 g = 0; g < gaps.Count; ++g)
        {
            if (gaps.End[g] <= cursor)
            {
                continue;
            }
            if (gaps.Start[g] >= b)
            {
                break;
            }
            if (gaps.Start[g] > cursor)
            {
                HoloMathEmitSpan(path, cursor, gaps.Start[g], writer);
            }
            cursor = FMath::Max(cursor, gaps.End[g]);
        }
        if (cursor < b)
        {
            HoloMathEmitSpan(path, cursor, b, writer);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Places a sorted gap table on a line of the given length (start at the fraction, length x scale, clipped at the
    // end) and merges gaps that overlap or touch
    FSOLHoloMergedGaps HoloMathMergeGaps(const SOL::FSOLHoloGap* table, const int32 count, const float lineLength,
        const float scale)
    {
        // Walk the table in order: an empty gap (clipped away at the end) is dropped, one that starts inside or at the
        // end of the last merged gap extends it, any other opens a new merged gap
        FSOLHoloMergedGaps merged;
        for (int32 i = 0; i < count && i < HOLOMATH_MAX_GAPS; ++i)
        {
            const float start = table[i].AtFraction * lineLength;
            const float end = FMath::Min(start + table[i].LengthPx * scale, lineLength);
            if (end <= start)
            {
                continue;
            }
            if (merged.Count > 0 && start <= merged.End[merged.Count - 1])
            {
                merged.End[merged.Count - 1] = FMath::Max(merged.End[merged.Count - 1], end);
            }
            else
            {
                merged.Start[merged.Count] = start;
                merged.End[merged.Count] = end;
                ++merged.Count;
            }
        }
        return merged;
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds the stepped outline P0-P10 of a valid rect, closed back to P0, with its effective lengths
    FSOLHoloOutline HoloMathBuildOutline(const FBox2f& rect, const float titleStepWidthPx, const float uiScale)
    {
        using namespace SOL::HoloMenu;
        FSOLHoloOutline outline;
        const float x0 = rect.Min.X;
        const float y0 = rect.Min.Y;
        const float x1 = rect.Max.X;
        const float y1 = rect.Max.Y;
        const float w = x1 - x0;
        const float k = HoloMathFrameScale(rect, uiScale);
        const float a = FRAME_CUT_TL_PX * k;
        const float step = FRAME_STEP_PX * k;
        const float c = FRAME_CUT_PX * k;
        const float n = FRAME_NOTCH_PX * k;
        const float t = FMath::IsNaN(titleStepWidthPx) ? a
            : FMath::Clamp(titleStepWidthPx, a, FMath::Max(a, w - step - c));
        outline.K = k;
        outline.Cut = c;
        outline.Step = step;
        outline.Title = t;

        // Clockwise on screen from the title corner (SDD 42 section 6.3 table)
        const FVector2f points[FRAME_OUTLINE_POINTS] = {
            FVector2f(x0, y0 + a), FVector2f(x0 + a, y0), FVector2f(x0 + t, y0), FVector2f(x0 + t + step, y0 + step),
            FVector2f(x1 - c, y0 + step), FVector2f(x1, y0 + step + c), FVector2f(x1, y1 - n),
            FVector2f(x1 - step, y1 - n + step), FVector2f(x1 - step, y1), FVector2f(x0 + c, y1),
            FVector2f(x0, y1 - c) };
        for (const FVector2f& point : points)
        {
            HoloMathPathAdd(outline.Path, point);
        }
        HoloMathPathAdd(outline.Path, points[0]);
        return outline;
    }
}

//////////////////////////////////////////////////////////////////////////
// Open progress: the clamped elapsed fraction, instant at strength 0 or a zero duration
float SOLHoloMenu::OpenProgress(const double elapsedS, const float durationS, const float s)
{
    if (HoloMathStrength(s) <= 0.0f || !(durationS > 0.0f))
    {
        return 1.0f;
    }
    if (FMath::IsNaN(elapsedS))
    {
        return 0.0f;
    }
    return static_cast<float>(FMath::Clamp(elapsedS / static_cast<double>(durationS), 0.0, 1.0));
}

//////////////////////////////////////////////////////////////////////////
// Frame line draw-in over the first LINE_PHASE_END of the progress
float SOLHoloMenu::FrameDrawIn(const float progress)
{
    return FMath::SmoothStep(0.0f, SOL::HoloMenu::LINE_PHASE_END, HoloMathUnit(progress));
}

//////////////////////////////////////////////////////////////////////////
// Fill fade over the progress from FILL_PHASE_START
float SOLHoloMenu::FillFade(const float progress)
{
    return FMath::SmoothStep(SOL::HoloMenu::FILL_PHASE_START, 1.0f, HoloMathUnit(progress));
}

//////////////////////////////////////////////////////////////////////////
// Hover edge glow: a smoothstep ramp times a brief noise flicker, exactly HOVER_GLOW_INTENSITY once both are over
float SOLHoloMenu::HoverGlow(const double sinceHoverS, const float s)
{
    using namespace SOL::HoloMenu;
    const float strength = HoloMathStrength(s);
    if (strength <= 0.0f || !(sinceHoverS >= 0.0))
    {
        return 0.0f;
    }
    const float ramp = FMath::SmoothStep(0.0f, static_cast<float>(HOVER_RAMP_S), static_cast<float>(sinceHoverS));
    float flicker = 1.0f;
    if (sinceHoverS < HOVER_FLICKER_S)
    {
        const float noiseAt = static_cast<float>(sinceHoverS * HOVER_FLICKER_RATE_HZ + HOVER_FLICKER_OFFSET);
        flicker = 1.0f - HOVER_FLICKER_DEPTH * strength * SOLHudShapeMath::SmoothNoise(noiseAt);
    }
    return HOVER_GLOW_INTENSITY * ramp * flicker;
}

//////////////////////////////////////////////////////////////////////////
// Idle sweep position, wrapped like the HUD time, always in [0, 1)
float SOLHoloMenu::SweepFraction(const double ageS)
{
    if (!FMath::IsFinite(ageS))
    {
        return 0.0f;
    }
    const double cycles = FMath::Fmod(ageS, static_cast<double>(SOL::Hologram::HUD_TIME_WRAP_S))
        / SOL::HoloMenu::IDLE_SWEEP_PERIOD_S;
    const float fraction = static_cast<float>(cycles - FMath::FloorToDouble(cycles));
    // A fraction just below 1 (a tiny negative age) rounds to 1.0f in float; that is the top again
    return fraction >= 1.0f || fraction < 0.0f ? 0.0f : fraction;
}

//////////////////////////////////////////////////////////////////////////
// Idle sweep glow per unit of strength
float SOLHoloMenu::SweepIntensity(const float s)
{
    return SOL::HoloMenu::IDLE_SWEEP_INTENSITY * HoloMathStrength(s);
}

//////////////////////////////////////////////////////////////////////////
// Backdrop alpha: the flat panel at 0, the faint tint at 1, rising per unit above, times the fill
float SOLHoloMenu::BackdropAlpha(const float s, const float fillAlpha)
{
    using namespace SOL::HoloMenu;
    const float strength = HoloMathStrength(s);
    const float base = strength <= 1.0f ? FMath::Lerp(FLAT_PANEL_ALPHA, BACKDROP_ALPHA_PER_UNIT, strength)
        : BACKDROP_ALPHA_PER_UNIT * strength;
    return base * HoloMathUnit(fillAlpha);
}

//////////////////////////////////////////////////////////////////////////
// Glow halo scale: the strength clamped to [0, 1]
float SOLHoloMenu::GlowScale(const float s)
{
    return FMath::Min(HoloMathStrength(s), 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Current progress of the anim: fixed in Closed / Open, moving from the phase start at the open / close rate
float SOLHoloMenu::Progress(const FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    using namespace SOL::HoloMenu;
    const bool bInstant = HoloMathStrength(s) <= 0.0f;
    const double elapsedS = FMath::Max(nowS - anim.PhaseStartS, 0.0);
    switch (anim.Phase)
    {
    case ESOLHoloMenuPhase::Opening:
        return bInstant ? 1.0f : static_cast<float>(FMath::Clamp(
            static_cast<double>(anim.PhaseStartProgress) + elapsedS / OPEN_S, 0.0, 1.0));
    case ESOLHoloMenuPhase::Closing:
        return bInstant ? 0.0f : static_cast<float>(FMath::Clamp(
            static_cast<double>(anim.PhaseStartProgress) - elapsedS / CLOSE_S, 0.0, 1.0));
    case ESOLHoloMenuPhase::Open:
        return 1.0f;
    case ESOLHoloMenuPhase::Closed:
    default:
        return 0.0f;
    }
}

//////////////////////////////////////////////////////////////////////////
// Starts an open from Closed (at 0) or Closing (from the current progress), clearing the hover
bool SOLHoloMenu::BeginOpen(FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    if (anim.Phase != ESOLHoloMenuPhase::Closed && anim.Phase != ESOLHoloMenuPhase::Closing)
    {
        return false;
    }
    anim.HoveredItem = INDEX_NONE;
    if (FMath::IsNaN(nowS))
    {
        // No usable time: open at once rather than stick in Opening (a NaN start never elapses)
        anim.Phase = ESOLHoloMenuPhase::Open;
        anim.PhaseStartProgress = 1.0f;
        return true;
    }
    anim.PhaseStartProgress = anim.Phase == ESOLHoloMenuPhase::Closed ? 0.0f : Progress(anim, nowS, s);
    anim.Phase = ESOLHoloMenuPhase::Opening;
    anim.PhaseStartS = nowS;
    anim.HoverStartS = nowS;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Starts a close from Opening / Open at the current progress, clearing the hover
bool SOLHoloMenu::BeginClose(FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    if (anim.Phase != ESOLHoloMenuPhase::Opening && anim.Phase != ESOLHoloMenuPhase::Open)
    {
        return false;
    }
    anim.HoveredItem = INDEX_NONE;
    if (FMath::IsNaN(nowS))
    {
        // No usable time: close at once rather than stick in Closing (a NaN start never elapses)
        anim.Phase = ESOLHoloMenuPhase::Closed;
        anim.PhaseStartProgress = 0.0f;
        return true;
    }
    anim.PhaseStartProgress = Progress(anim, nowS, s);
    anim.Phase = ESOLHoloMenuPhase::Closing;
    anim.PhaseStartS = nowS;
    anim.HoverStartS = nowS;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Finishes an open or a close whose progress has reached its end
void SOLHoloMenu::Advance(FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    if (anim.Phase == ESOLHoloMenuPhase::Opening && Progress(anim, nowS, s) >= 1.0f)
    {
        anim.Phase = ESOLHoloMenuPhase::Open;
        anim.PhaseStartS = nowS;
        anim.PhaseStartProgress = 1.0f;
    }
    else if (anim.Phase == ESOLHoloMenuPhase::Closing && Progress(anim, nowS, s) <= 0.0f)
    {
        anim.Phase = ESOLHoloMenuPhase::Closed;
        anim.PhaseStartS = nowS;
        anim.PhaseStartProgress = 0.0f;
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves the hover to an item, restarting its glow; true for a change to a real item
bool SOLHoloMenu::SetHovered(FSOLHoloMenuAnim& anim, const int32 item, const double nowS)
{
    if (item == anim.HoveredItem)
    {
        return false;
    }
    anim.HoveredItem = item;
    anim.HoverStartS = nowS;
    return item != INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Rate-limited hover cue rule, only while Open; records the cue time when it plays
bool SOLHoloMenu::ShouldPlayHoverCue(FSOLHoloMenuAnim& anim, const bool bHoverChanged, const double nowS)
{
    if (!bHoverChanged || anim.Phase != ESOLHoloMenuPhase::Open
        || !(nowS - anim.LastHoverCueS >= SOL::HoloMenu::HOVER_CUE_MIN_INTERVAL_S))
    {
        return false;
    }
    anim.LastHoverCueS = nowS;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// The menu's look this frame: progress, the draw-in and fill curves, hover glow, idle sweep and backdrop
FSOLHoloMenuLook SOLHoloMenu::Evaluate(const FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    FSOLHoloMenuLook look;
    look.bVisible = anim.Phase != ESOLHoloMenuPhase::Closed;
    look.Progress = Progress(anim, nowS, s);
    look.FrameFraction = FrameDrawIn(look.Progress);
    look.FillAlpha = FillFade(look.Progress);
    look.HoverIntensity = anim.HoveredItem != INDEX_NONE ? HoverGlow(nowS - anim.HoverStartS, s) : 0.0f;
    // The sweep runs on the absolute time on purpose, not the phase time: every open menu sweeps in step, and there is
    // no jump when Opening turns into Open
    look.SweepFraction = SweepFraction(nowS);
    look.SweepIntensity = SweepIntensity(s) * look.FillAlpha;
    look.BackdropAlpha = BackdropAlpha(s, look.FillAlpha);
    return look;
}

//////////////////////////////////////////////////////////////////////////
// The stepped outline's points P0-P10
int32 SOLHoloMenu::FrameOutline(const FBox2f& rect, const float titleStepWidthPx, const float uiScale,
    FVector2f outPoints[SOL::HoloMenu::FRAME_OUTLINE_POINTS])
{
    if (!HoloMathIsValidRect(rect))
    {
        return 0;
    }
    const FSOLHoloOutline outline = HoloMathBuildOutline(rect, titleStepWidthPx, uiScale);
    for (int32 i = 0; i < SOL::HoloMenu::FRAME_OUTLINE_POINTS; ++i)
    {
        outPoints[i] = outline.Path.Points[i];
    }
    return SOL::HoloMenu::FRAME_OUTLINE_POINTS;
}

//////////////////////////////////////////////////////////////////////////
// The stepped outline's perimeter
float SOLHoloMenu::FramePerimeter(const FBox2f& rect, const float titleStepWidthPx, const float uiScale)
{
    return HoloMathIsValidRect(rect) ? HoloMathPathLength(HoloMathBuildOutline(rect, titleStepWidthPx, uiScale).Path)
        : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// The visible outline pieces at a draw-in fraction
int32 SOLHoloMenu::FrameDrawSegments(const FBox2f& rect, const float titleStepWidthPx, const float uiScale,
    const float fraction, FSOLHoloSegment outSegments[SOL::HoloMenu::FRAME_MAX_SEGMENTS])
{
    using namespace SOL::HoloMenu;
    const float f = HoloMathUnit(fraction);
    if (f <= 0.0f || !HoloMathIsValidRect(rect))
    {
        return 0;
    }
    const FSOLHoloOutline outline = HoloMathBuildOutline(rect, titleStepWidthPx, uiScale);
    const float length = HoloMathPathLength(outline.Path);
    const FSOLHoloMergedGaps gaps = HoloMathMergeGaps(FRAME_GAPS, FRAME_GAP_COUNT, length, outline.K);

    // The clockwise stroke covers [0, f L / 2] and the counter-clockwise one [L - f L / 2, L]; each is cut on its own,
    // so at f = 1 a piece ends and the next starts at u = L / 2
    const float reach = 0.5f * f * length;
    FSOLHoloPieceWriter writer{ outSegments, FRAME_MAX_SEGMENTS };
    HoloMathEmitVisible(outline.Path, gaps, 0.0f, reach, writer);
    HoloMathEmitVisible(outline.Path, gaps, length - reach, length, writer);
    return writer.Count;
}

//////////////////////////////////////////////////////////////////////////
// The gap end dots the strokes have reached at a draw-in fraction
int32 SOLHoloMenu::FrameGapDots(const FBox2f& rect, const float titleStepWidthPx, const float uiScale,
    const float fraction, FVector2f outDots[SOL::HoloMenu::FRAME_MAX_DOTS])
{
    using namespace SOL::HoloMenu;
    const float f = HoloMathUnit(fraction);
    if (f <= 0.0f || !HoloMathIsValidRect(rect))
    {
        return 0;
    }
    const FSOLHoloOutline outline = HoloMathBuildOutline(rect, titleStepWidthPx, uiScale);
    const float length = HoloMathPathLength(outline.Path);
    const FSOLHoloMergedGaps gaps = HoloMathMergeGaps(FRAME_GAPS, FRAME_GAP_COUNT, length, outline.K);
    const float reach = 0.5f * f * length;

    // Each merged gap's start then end, in increasing u, kept when a stroke has reached it (ends included)
    int32 count = 0;
    for (int32 g = 0; g < gaps.Count; ++g)
    {
        const float ends[2] = { gaps.Start[g], gaps.End[g] };
        for (const float u : ends)
        {
            if (count < FRAME_MAX_DOTS && (u <= reach || u >= length - reach))
            {
                outDots[count++] = HoloMathPointAtU(outline.Path, u);
            }
        }
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// The left-edge ruler ticks
int32 SOLHoloMenu::FrameRulerTicks(const FBox2f& rect, const float uiScale,
    FSOLHoloSegment outSegments[SOL::HoloMenu::FRAME_RULER_TICK_COUNT])
{
    using namespace SOL::HoloMenu;
    if (!HoloMathIsValidRect(rect))
    {
        return 0;
    }
    const float k = HoloMathFrameScale(rect, uiScale);
    const float x0 = rect.Min.X;
    const float lastY = rect.Max.Y - FRAME_CUT_PX * k;
    const float firstY = rect.Min.Y + FRAME_RULER_START * (rect.Max.Y - rect.Min.Y);
    FSOLHoloPieceWriter writer{ outSegments, FRAME_RULER_TICK_COUNT };
    for (int32 i = 0; i < FRAME_RULER_TICK_COUNT; ++i)
    {
        const float y = firstY + static_cast<float>(i) * FRAME_RULER_SPACING_PX * k;
        if (y > lastY)
        {
            break;
        }
        writer.Add(FVector2f(x0, y), FVector2f(x0 + FRAME_RULER_TICK_PX * k, y));
    }
    return writer.Count;
}

//////////////////////////////////////////////////////////////////////////
// The inner corner accents and the broken title underline
int32 SOLHoloMenu::FrameAccents(const FBox2f& rect, const float titleStepWidthPx, const float uiScale,
    FSOLHoloSegment outSegments[SOL::HoloMenu::FRAME_ACCENT_COUNT])
{
    using namespace SOL::HoloMenu;
    if (!HoloMathIsValidRect(rect))
    {
        return 0;
    }
    const FSOLHoloOutline outline = HoloMathBuildOutline(rect, titleStepWidthPx, uiScale);
    const float k = outline.K;
    const float x0 = rect.Min.X;
    const float y0 = rect.Min.Y;
    const float x1 = rect.Max.X;
    const float y1 = rect.Max.Y;
    const float t = outline.Title;
    FSOLHoloPieceWriter writer{ outSegments, FRAME_ACCENT_COUNT };

    // (1) Top-left inner accent, downward
    const float leftX = x0 + FRAME_ACCENT_INSET_PX * k;
    const float leftTop = y0 + FRAME_ACCENT_LEFT_TOP_PX * k;
    const float leftBottom = FMath::Min(y0 + FRAME_ACCENT_LEFT_BOTTOM_PX * k, y1 - outline.Cut);
    if (leftBottom - leftTop >= SOL::HoloMenu::MIN_PIECE_PX)
    {
        writer.Add(FVector2f(leftX, leftTop), FVector2f(leftX, leftBottom));
    }

    // (2) Bottom-right inner accent, leftward
    const float bottomY = y1 - FRAME_ACCENT_INSET_PX * k;
    const float bottomStart = x1 - outline.Step - FRAME_ACCENT_INSET_PX * k;
    const float bottomEnd = FMath::Max(x1 - outline.Step - (FRAME_ACCENT_INSET_PX + FRAME_ACCENT_BOTTOM_PX) * k,
        x0 + outline.Cut);
    if (bottomStart - bottomEnd >= SOL::HoloMenu::MIN_PIECE_PX)
    {
        writer.Add(FVector2f(bottomStart, bottomY), FVector2f(bottomEnd, bottomY));
    }

    // (3) Title underline and (4) its short dash, rightward; both left out when the underline has no length
    const float underlineY = y0 + FRAME_TITLE_UNDERLINE_Y_PX * k;
    const float underlineStart = x0 + FRAME_TITLE_UNDERLINE_START_PX * k;
    const float underlineEnd = x0 + t - FRAME_TITLE_UNDERLINE_END_PX * k;
    if (underlineEnd - underlineStart >= SOL::HoloMenu::MIN_PIECE_PX)
    {
        writer.Add(FVector2f(underlineStart, underlineY), FVector2f(underlineEnd, underlineY));
        const float dashStart = x0 + t - FRAME_TITLE_DASH_START_PX * k;
        const float dashEnd = x0 + t - FRAME_TITLE_DASH_END_PX * k;
        if (dashEnd - dashStart >= SOL::HoloMenu::MIN_PIECE_PX)
        {
            writer.Add(FVector2f(dashStart, underlineY), FVector2f(dashEnd, underlineY));
        }
    }
    return writer.Count;
}

//////////////////////////////////////////////////////////////////////////
// The hovered item's brackets and its pointer line or centre ticks
int32 SOLHoloMenu::HoverMark(const FBox2f& rect, const ESOLHoloHoverMark mark, const float uiScale,
    FSOLHoloSegment outSegments[SOL::HoloMenu::HOVER_MAX_SEGMENTS], FVector2f& outDot, int32& outDotCount)
{
    using namespace SOL::HoloMenu;
    outDotCount = 0;
    if (!HoloMathIsValidRect(rect))
    {
        return 0;
    }
    const float scale = HoloMathScale(uiScale);
    const float x0 = rect.Min.X;
    const float y0 = rect.Min.Y;
    const float x1 = rect.Max.X;
    const float y1 = rect.Max.Y;
    const float arm = FMath::Min(HOVER_BRACKET_ARM_PX * scale, 0.5f * (x1 - x0));
    FSOLHoloPieceWriter writer{ outSegments, HOVER_MAX_SEGMENTS };

    // Left bracket, then the mirrored right bracket
    writer.Add(FVector2f(x0 + arm, y0), FVector2f(x0, y0));
    writer.Add(FVector2f(x0, y0), FVector2f(x0, y1));
    writer.Add(FVector2f(x0, y1), FVector2f(x0 + arm, y1));
    writer.Add(FVector2f(x1 - arm, y0), FVector2f(x1, y0));
    writer.Add(FVector2f(x1, y0), FVector2f(x1, y1));
    writer.Add(FVector2f(x1, y1), FVector2f(x1 - arm, y1));

    if (mark == ESOLHoloHoverMark::RowPointer)
    {
        // A straight run right from the middle of the right bracket, a 45-degree rise, and a dot beyond its end
        const float midY = 0.5f * (y0 + y1);
        const float run = HOVER_POINTER_RUN_PX * scale;
        const float rise = HOVER_POINTER_RISE_PX * scale;
        const float dotOffset = HOVER_POINTER_DOT_PX * scale;
        writer.Add(FVector2f(x1, midY), FVector2f(x1 + run, midY));
        writer.Add(FVector2f(x1 + run, midY), FVector2f(x1 + run + rise, midY - rise));
        outDot = FVector2f(x1 + run + rise + dotOffset, midY - rise - dotOffset);
        outDotCount = 1;
    }
    else if (mark == ESOLHoloHoverMark::CentreTicks)
    {
        const float midX = 0.5f * (x0 + x1);
        const float tick = HOVER_CENTRE_TICK_PX * scale;
        writer.Add(FVector2f(midX, y0), FVector2f(midX, y0 - tick));
        writer.Add(FVector2f(midX, y1), FVector2f(midX, y1 + tick));
    }
    return writer.Count;
}

//////////////////////////////////////////////////////////////////////////
// The profile tab marker: a dim bar, or the selected tab's outline into the divider with a dot
int32 SOLHoloMenu::TabMarker(const FBox2f& tabRect, const float dividerX, const bool bSelected, const float uiScale,
    FSOLHoloSegment outSegments[SOL::HoloMenu::TAB_MAX_SEGMENTS], FVector2f& outDot, int32& outDotCount)
{
    using namespace SOL::HoloMenu;
    outDotCount = 0;
    if (!HoloMathIsValidRect(tabRect))
    {
        return 0;
    }
    const float scale = HoloMathScale(uiScale);
    const float x0 = tabRect.Min.X;
    const float y0 = tabRect.Min.Y;
    const float x1 = tabRect.Max.X;
    const float y1 = tabRect.Max.Y;
    const float w = x1 - x0;
    const float h = y1 - y0;
    FSOLHoloPieceWriter writer{ outSegments, TAB_MAX_SEGMENTS };
    if (!bSelected)
    {
        // A left bar with short arms, inset from the top and bottom
        const float arm = FMath::Min(TAB_BRACKET_ARM_PX * scale, w);
        const float inset = FMath::Min(TAB_BRACKET_INSET_PX * scale, 0.5f * h);
        writer.Add(FVector2f(x0 + arm, y0 + inset), FVector2f(x0, y0 + inset));
        writer.Add(FVector2f(x0, y0 + inset), FVector2f(x0, y1 - inset));
        writer.Add(FVector2f(x0, y1 - inset), FVector2f(x0 + arm, y1 - inset));
        return writer.Count;
    }

    // Selected: open on the right, running into the column divider, with a cut top-left corner and a dot to the left
    const float divider = FMath::IsFinite(dividerX) ? FMath::Max(dividerX, x1) : x1;
    const float cut = FMath::Min3(TAB_CUT_PX * scale, w, 0.5f * h);
    writer.Add(FVector2f(divider, y0), FVector2f(x0 + cut, y0));
    writer.Add(FVector2f(x0 + cut, y0), FVector2f(x0, y0 + cut));
    writer.Add(FVector2f(x0, y0 + cut), FVector2f(x0, y1));
    writer.Add(FVector2f(x0, y1), FVector2f(divider, y1));
    outDot = FVector2f(x0 - TAB_DOT_OFFSET_PX * scale, 0.5f * (y0 + y1));
    outDotCount = 1;
    return writer.Count;
}

//////////////////////////////////////////////////////////////////////////
// The dashed vertical column divider, open beside the selected tab
int32 SOLHoloMenu::DashedDivider(const FVector2f top, const float bottomY, const float openTopY,
    const float openBottomY, const float uiScale, FSOLHoloSegment outSegments[SOL::HoloMenu::DIVIDER_MAX_DASHES])
{
    using namespace SOL::HoloMenu;
    if (!FMath::IsFinite(top.X) || !FMath::IsFinite(top.Y) || !FMath::IsFinite(bottomY) || bottomY <= top.Y)
    {
        return 0;
    }
    const float scale = HoloMathScale(uiScale);
    const float period = DIVIDER_PERIOD_PX * scale;
    const float dash = DIVIDER_DASH_PX * scale;
    const bool bHasOpening = !FMath::IsNaN(openTopY) && !FMath::IsNaN(openBottomY) && openTopY <= openBottomY;

    // Dash slots top to bottom; the dashes touching the opening are left out whole by jumping to the first slot below
    // it, so the loop writes, skips the opening once and stops (hard-capped at HOLOMATH_MAX_DASH_PASSES)
    FSOLHoloPieceWriter writer{ outSegments, DIVIDER_MAX_DASHES };
    double slot = 0.0;
    for (int32 pass = 0; pass < HOLOMATH_MAX_DASH_PASSES && writer.Count < DIVIDER_MAX_DASHES; ++pass)
    {
        const float startY = static_cast<float>(static_cast<double>(top.Y) + slot * static_cast<double>(period));
        if (startY >= bottomY)
        {
            break;
        }
        const float endY = FMath::Min(startY + dash, bottomY);
        if (bHasOpening && startY <= openBottomY && endY >= openTopY)
        {
            const double firstBelow = FMath::FloorToDouble(
                (static_cast<double>(openBottomY) - static_cast<double>(top.Y)) / static_cast<double>(period)) + 1.0;
            slot = FMath::Max(slot + 1.0, firstBelow);
            continue;
        }
        writer.Add(FVector2f(top.X, startY), FVector2f(top.X, endY));
        slot += 1.0;
    }
    return writer.Count;
}

//////////////////////////////////////////////////////////////////////////
// The broken horizontal divider with its fixed gaps and an optional step up
int32 SOLHoloMenu::BrokenDivider(const FVector2f start, const float endX, const float stepAtPx, const float stepPx,
    const float uiScale, FSOLHoloSegment outSegments[SOL::HoloMenu::DIVIDER_MAX_SEGMENTS])
{
    using namespace SOL::HoloMenu;
    if (!FMath::IsFinite(start.X) || !FMath::IsFinite(start.Y) || !FMath::IsFinite(endX) || !FMath::IsFinite(stepAtPx)
        || endX <= start.X)
    {
        return 0;
    }
    const float width = endX - start.X;

    // The path: along start.Y, a 45-degree rise of the step (no wider than the divider), then along the raised line
    const float step = FMath::IsFinite(stepPx) && stepPx > 0.0f ? FMath::Min(stepPx, width) : 0.0f;
    FSOLHoloPath path;
    HoloMathPathAdd(path, start);
    if (step > 0.0f)
    {
        const float stepX = start.X + FMath::Clamp(stepAtPx, 0.0f, width - step);
        HoloMathPathAdd(path, FVector2f(stepX, start.Y));
        HoloMathPathAdd(path, FVector2f(stepX + step, start.Y - step));
        HoloMathPathAdd(path, FVector2f(endX, start.Y - step));
    }
    else
    {
        HoloMathPathAdd(path, FVector2f(endX, start.Y));
    }

    const float length = HoloMathPathLength(path);
    const FSOLHoloMergedGaps gaps = HoloMathMergeGaps(DIVIDER_GAPS, DIVIDER_GAP_COUNT, length, HoloMathScale(uiScale));
    FSOLHoloPieceWriter writer{ outSegments, DIVIDER_MAX_SEGMENTS };
    HoloMathEmitVisible(path, gaps, 0.0f, length, writer);
    return writer.Count;
}

//////////////////////////////////////////////////////////////////////////
// A radial sector's rim arc grown from its middle by the frame fraction
void SOLHoloMenu::RadialArcDrawIn(const float halfApertureRad, const float frameFraction, float& outHalfApertureRad)
{
    outHalfApertureRad = halfApertureRad * HoloMathUnit(frameFraction);
}

//////////////////////////////////////////////////////////////////////////
// The frameless card's eight corner ticks
int32 SOLHoloMenu::CardCornerTicks(const FBox2f& rect, const float tickPx, const float fraction,
    FSOLHoloSegment outSegments[SOL::HoloMenu::CARD_TICK_SEGMENTS])
{
    if (!HoloMathIsValidRect(rect))
    {
        return 0;
    }
    const float x0 = rect.Min.X;
    const float y0 = rect.Min.Y;
    const float x1 = rect.Max.X;
    const float y1 = rect.Max.Y;
    const float maxTick = 0.5f * FMath::Min(x1 - x0, y1 - y0);
    const float tick = FMath::IsNaN(tickPx) ? 0.0f : FMath::Clamp(tickPx, 0.0f, maxTick);
    const float length = tick * HoloMathUnit(fraction);
    if (length < SOL::HoloMenu::MIN_PIECE_PX)
    {
        return 0;
    }

    // Two ticks per corner, each from the corner along one of its edges (top-left, top-right, bottom-right,
    // bottom-left)
    FSOLHoloPieceWriter writer{ outSegments, SOL::HoloMenu::CARD_TICK_SEGMENTS };
    writer.Add(FVector2f(x0, y0), FVector2f(x0 + length, y0));
    writer.Add(FVector2f(x0, y0), FVector2f(x0, y0 + length));
    writer.Add(FVector2f(x1, y0), FVector2f(x1 - length, y0));
    writer.Add(FVector2f(x1, y0), FVector2f(x1, y0 + length));
    writer.Add(FVector2f(x1, y1), FVector2f(x1 - length, y1));
    writer.Add(FVector2f(x1, y1), FVector2f(x1, y1 - length));
    writer.Add(FVector2f(x0, y1), FVector2f(x0 + length, y1));
    writer.Add(FVector2f(x0, y1), FVector2f(x0, y1 - length));
    return writer.Count;
}
