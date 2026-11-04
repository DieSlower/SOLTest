/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "SOLConstants.h"

// One hologram menu's open / close phase (SDD 42 section 6.2)
enum class ESOLHoloMenuPhase : uint8
{
    Closed,
    Opening,
    Open,
    Closing,
};

// One menu's animation state: its phase, the hovered item and the hover cue's rate limit (SDD 42 section 6.2)
struct FSOLHoloMenuAnim
{
    ESOLHoloMenuPhase Phase = ESOLHoloMenuPhase::Closed;
    double PhaseStartS = 0.0;           // When the current phase started
    float PhaseStartProgress = 0.0f;    // Progress at that moment (a reversal starts mid-way)
    int32 HoveredItem = INDEX_NONE;
    double HoverStartS = 0.0;
    double LastHoverCueS = -1.0e9;
};

// What a menu draws this frame, from its anim state (SOLHoloMenu::Evaluate)
struct FSOLHoloMenuLook
{
    bool bVisible = false;          // Phase is not Closed
    float Progress = 0.0f;          // Open / close progress in [0, 1]
    float FrameFraction = 0.0f;     // Frame line draw-in in [0, 1]
    float FillAlpha = 0.0f;         // Fill, backdrop and text fade in [0, 1]
    float HoverIntensity = 0.0f;    // Hovered item's edge glow, 0 with no hovered item
    float SweepFraction = 0.0f;     // Idle sweep line position, 0 at the top of the frame, in [0, 1)
    float SweepIntensity = 0.0f;    // Idle sweep line glow (strength x fill)
    float BackdropAlpha = 0.0f;     // Faint dark tint behind the panel
};

// One straight line piece of a frame, drawn A -> B (SDD 42 section 6.3)
struct FSOLHoloSegment
{
    FVector2f A = FVector2f::ZeroVector;
    FVector2f B = FVector2f::ZeroVector;
};

// The mark on a menu's hovered item (SDD 42 section 6.3)
enum class ESOLHoloHoverMark : uint8
{
    BracketsOnly,   // Left and right brackets
    RowPointer,     // Brackets plus a pointer line with a dot to the right
    CentreTicks,    // Brackets plus a short tick above and below the centre
};

/**
 * Pure, renderer-free logic of the hologram in-flight menus (SDD 42 section 6): the open / close and hover curves, the
 * per-menu anim state, and the option (c) frame geometry. `s` is the effects strength, clamped to [0, 2] with NaN
 * reading 0, by every function itself. Times are real application seconds passed in (no clock inside). Geometry is in
 * screen px, y down; every geometry function writes into a caller-owned fixed-size array, never past its capacity,
 * and returns the count written. `uiScale` NaN, infinite or <= 0 reads 1, and is clamped to
 * [UI_SCALE_MIN, UI_SCALE_MAX]. A NaN time in BeginOpen / BeginClose finishes the transition at once.
 */
namespace SOLHoloMenu
{
    // --- Curves (section 6.1) ---

    // Open progress after elapsedS of durationS: clamp(elapsed / duration, 0, 1); 1 at s <= 0 or duration <= 0
    SOLTEST_API float OpenProgress(double elapsedS, float durationS, float s);

    // Frame line draw-in: SmoothStep(0, LINE_PHASE_END, progress)
    SOLTEST_API float FrameDrawIn(float progress);

    // Fill fade: SmoothStep(FILL_PHASE_START, 1, progress)
    SOLTEST_API float FillFade(float progress);

    // Hovered item's edge glow sinceHoverS after the hover began: ramp x flicker x HOVER_GLOW_INTENSITY; 0 before the
    // hover and at s <= 0
    SOLTEST_API float HoverGlow(double sinceHoverS, float s);

    // Idle sweep position: frac(fmod(ageS, HUD_TIME_WRAP_S) / IDLE_SWEEP_PERIOD_S), always in [0, 1)
    SOLTEST_API float SweepFraction(double ageS);

    // Idle sweep glow: IDLE_SWEEP_INTENSITY x s
    SOLTEST_API float SweepIntensity(float s);

    // Backdrop tint alpha: lerp(FLAT_PANEL_ALPHA, BACKDROP_ALPHA_PER_UNIT, s) up to s = 1, BACKDROP_ALPHA_PER_UNIT x s
    // above, times clamp(fillAlpha, 0, 1)
    SOLTEST_API float BackdropAlpha(float s, float fillAlpha);

    // Glow halo scale of outlines, ticks and sweep: clamp(s, 0, 1)
    SOLTEST_API float GlowScale(float s);

    // --- Anim state (section 6.2) ---

    // Current progress: Closed 0, Open 1, Opening / Closing from PhaseStartProgress at the OPEN_S / CLOSE_S rate;
    // at s <= 0 Opening reads 1 and Closing 0
    SOLTEST_API float Progress(const FSOLHoloMenuAnim& anim, double nowS, float s);

    // Starts an open (from 0 when Closed, from the current progress when Closing); true when it did (the open cue)
    SOLTEST_API bool BeginOpen(FSOLHoloMenuAnim& anim, double nowS, float s);

    // Starts a close from the current progress when Opening / Open; true when it did
    SOLTEST_API bool BeginClose(FSOLHoloMenuAnim& anim, double nowS, float s);

    // Moves Opening at progress 1 to Open and Closing at progress 0 to Closed; idempotent
    SOLTEST_API void Advance(FSOLHoloMenuAnim& anim, double nowS, float s);

    // Sets the hovered item; true only when it changed to a real item (not INDEX_NONE)
    SOLTEST_API bool SetHovered(FSOLHoloMenuAnim& anim, int32 item, double nowS);

    // Whether a hover change plays the hover cue: only when Open and HOVER_CUE_MIN_INTERVAL_S after the last one
    SOLTEST_API bool ShouldPlayHoverCue(FSOLHoloMenuAnim& anim, bool bHoverChanged, double nowS);

    // The menu's look this frame from its anim state
    SOLTEST_API FSOLHoloMenuLook Evaluate(const FSOLHoloMenuAnim& anim, double nowS, float s);

    // --- Frame geometry (section 6.3) ---

    // The stepped outline's 11 points P0-P10 (closed back to P0); FRAME_OUTLINE_POINTS, or 0 for an invalid rect
    SOLTEST_API int32 FrameOutline(const FBox2f& rect, float titleStepWidthPx, float uiScale,
        FVector2f outPoints[SOL::HoloMenu::FRAME_OUTLINE_POINTS]);

    // The stepped outline's perimeter L; 0 for an invalid rect
    SOLTEST_API float FramePerimeter(const FBox2f& rect, float titleStepWidthPx, float uiScale);

    // The visible outline at a draw-in fraction (two strokes from P0, minus the gaps), split at every outline point and
    // at u = L / 2, ordered by u; 0 at fraction <= 0, NaN or an invalid rect
    SOLTEST_API int32 FrameDrawSegments(const FBox2f& rect, float titleStepWidthPx, float uiScale, float fraction,
        FSOLHoloSegment outSegments[SOL::HoloMenu::FRAME_MAX_SEGMENTS]);

    // The gap end dots the strokes have reached at a draw-in fraction, ordered by u; 0 at fraction <= 0, NaN or an
    // invalid rect
    SOLTEST_API int32 FrameGapDots(const FBox2f& rect, float titleStepWidthPx, float uiScale, float fraction,
        FVector2f outDots[SOL::HoloMenu::FRAME_MAX_DOTS]);

    // The left-edge ruler ticks from half the height down, stopping past the left edge's straight part
    SOLTEST_API int32 FrameRulerTicks(const FBox2f& rect, float uiScale,
        FSOLHoloSegment outSegments[SOL::HoloMenu::FRAME_RULER_TICK_COUNT]);

    // The two inner corner accents and the broken title underline (pieces with no length or the wrong way skipped)
    SOLTEST_API int32 FrameAccents(const FBox2f& rect, float titleStepWidthPx, float uiScale,
        FSOLHoloSegment outSegments[SOL::HoloMenu::FRAME_ACCENT_COUNT]);

    // The hovered item's brackets, plus a pointer line and dot (RowPointer) or centre ticks (CentreTicks)
    SOLTEST_API int32 HoverMark(const FBox2f& rect, ESOLHoloHoverMark mark, float uiScale,
        FSOLHoloSegment outSegments[SOL::HoloMenu::HOVER_MAX_SEGMENTS], FVector2f& outDot, int32& outDotCount);

    // The Overlay Menu's profile tab marker: a dim bar, or (selected) an outline into the column divider with a dot
    SOLTEST_API int32 TabMarker(const FBox2f& tabRect, float dividerX, bool bSelected, float uiScale,
        FSOLHoloSegment outSegments[SOL::HoloMenu::TAB_MAX_SEGMENTS], FVector2f& outDot, int32& outDotCount);

    // The dim dashed vertical column divider from top down to bottomY, open where it overlaps [openTopY, openBottomY]
    SOLTEST_API int32 DashedDivider(FVector2f top, float bottomY, float openTopY, float openBottomY, float uiScale,
        FSOLHoloSegment outSegments[SOL::HoloMenu::DIVIDER_MAX_DASHES]);

    // A dim horizontal divider to endX with the DIVIDER_GAPS gaps and an optional 45-degree step up of stepPx
    SOLTEST_API int32 BrokenDivider(FVector2f start, float endX, float stepAtPx, float stepPx, float uiScale,
        FSOLHoloSegment outSegments[SOL::HoloMenu::DIVIDER_MAX_SEGMENTS]);

    // A radial sector's rim arc growing from its middle: halfApertureRad x clamp(frameFraction, 0, 1)
    SOLTEST_API void RadialArcDrawIn(float halfApertureRad, float frameFraction, float& outHalfApertureRad);

    // The frameless card's corner ticks: two per corner along its edges, tickPx (clamped to half the shorter side)
    // x the fraction long; 8, or 0 for an invalid rect or a zero length
    SOLTEST_API int32 CardCornerTicks(const FBox2f& rect, float tickPx, float fraction,
        FSOLHoloSegment outSegments[SOL::HoloMenu::CARD_TICK_SEGMENTS]);
}
