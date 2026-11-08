/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

struct FSOLHoloMenuAnim;

// One hull's shape in the widget's local space (origin top-left, x right, y down, the rect [0, w] x [0, h]); UMG units
// (SDD 46 sections 5.2 and 8.1). The title step raises the top edge left of TitleStepWidth by TitleStepHeight over the
// rest (a 45-degree flank), the chin lowers the bottom edge left of ChinWidth by ChinHeight (the same flank), and the
// notches cut a 45-degree trapezoid NotchDepth deep and NotchHeight tall into both sides, centred on NotchY
struct FSOLSolidHull
{
    float ChamferTL = 0.0f;         // 45-degree corner cuts, >= 0
    float ChamferTR = 0.0f;         // (top-right sits on the stepped-down top edge)
    float ChamferBR = 0.0f;         // (bottom-right sits on the raised bottom edge right of the chin)
    float ChamferBL = 0.0f;
    float TitleStepWidth = 0.0f;    // 0 = no stepped top edge
    float TitleStepHeight = 0.0f;
    float NotchY = -1.0f;           // Notch centre; < 0 = no side notches
    float NotchDepth = 0.0f;
    float NotchHeight = 0.0f;
    float ChinWidth = 0.0f;         // 0 = no chin
    float ChinHeight = 0.0f;
    bool bRecess = false;           // A sunken recess (inverted bevel) instead of a raised hull
};

// A menu button's highlight state (SDD 46 section 5.3)
enum class ESOLSolidButtonState : uint8
{
    Normal,
    Hovered,    // Mouse hover or keyboard focus (one highlight)
    Pressed,
    Selected,   // Active tab, page or start body
    Disabled,
};

// What a button draws this frame (SDD 46 section 8.2): five animated amounts in [0, 1], the glow halo and the flags
struct FSOLSolidButtonLook
{
    float Lift = 0.0f;              // Hover lift, scaled by HOVER_LIFT_PX
    float Edge = 0.0f;              // Amber accent edge
    float Led = 0.0f;               // Amber LED
    float Underline = 0.0f;         // Amber underline
    float Trace = 0.0f;             // The hovered button's extending circuit trace
    float Glow = 0.0f;              // Glow halo: s x Edge, in [0, 2] (doubled at strength 2, SDD 46 section 7)
    bool bPressedHull = false;      // The dark inset pressed hull
    bool bSelectedMark = false;     // The cyan selected underline and LED
    bool bDisabled = false;         // Dimmed hull and label (the tint's alpha)
};

// The pause side panel's look at a frame (46g, SOLSolidMenu::EvaluatePausePanel)
struct FSOLPausePanelLook
{
    float Progress = 0.0f;          // Open progress, [0, 1]
    float SlideX = 0.0f;            // The side panel's x offset (never positive)
    float DimAlpha = 0.0f;          // The live view's dim
    bool bShown = false;            // On screen (anything but Closed)
    bool bInteractive = false;      // Takes input (Opening or Open)
};

/**
 * Pure, renderer-free logic of the solid menus (SDD 46 section 8): the hull spec clamp and its SDF, the button look and
 * its brush-tint encoding, the panel and pause slides, the trace power-up, the idle pulse and trace polylines. `s` is
 * the effects strength (HudEffectsStrength, Q1), NaN reading 0 and clamped to the HUD effects range [0, 2] by every
 * function itself (the same rule as FSOLHoloMenuParamCache::SanitizeStrength). Times are real application seconds
 * passed in (no clock inside); NaN or negative elapsed times read 0.
 */
namespace SOLSolidMenu
{
    // (33d-ii) Returns the effects strength NaN-safe (NaN reads 0) and clamped to the HUD effects range (Q1); the rule
    // FSOLSolidMenuParamCache::SanitizeStrength applies, here for pure callers
    SOLTEST_API float SanitizeStrength(float s);

    // --- Hull (section 8.1) ---

    // The hull made valid for a size: negative or NaN values read 0; the chamfers scaled by one common factor so every
    // edge's pair sums to at most HULL_MAX_CHAMFER_FRACTION of it; the title step, notches and chin clamped inside the
    // rect, clear of the chamfers (heights and depth at most HULL_MAX_FEATURE_FRACTION); a feature with a zero size
    // removed (a removed notch reads NotchY -1); every value 0 for a size with a component <= 0 or non-finite
    SOLTEST_API FSOLSolidHull ClampHull(const FSOLSolidHull& hull, FVector2f size);

    // The CPU mirror of the hull material's SDF for a clamped hull: negative inside, 0 on the edge, positive outside,
    // exact on the straight edges and the chamfer diagonals; FLT_MAX for an invalid size or a non-finite point
    SOLTEST_API float HullDistance(FVector2f p, FVector2f size, const FSOLSolidHull& hull);

    // --- Button look (section 8.2) ---

    // The button's look sinceChangeS after it entered state from previous: Hovered ramps Lift, Edge, Led, Underline
    // and Trace linearly from the previous state's settled values to 1 over HOVER_RAMP_S (instant at s = 0), leaving
    // it ramps back over the same time; Pressed is the pressed hull with Lift 0 and Edge 1 at once, and lingers until
    // PRESSED_LINGER_S after the press, the ramp to the new state starting when the linger ends (so for a press longer
    // than the linger the caller passes now - max(releaseS - PRESSED_LINGER_S, pressS)); Selected is the mark with no
    // lift or amber; Disabled is all 0 and bDisabled
    // (46c amendment) bSelected is orthogonal to the state: with Normal, Hovered or Pressed it adds bSelectedMark and
    // changes nothing else, State Selected is Normal plus the mark (always), Disabled ignores bSelected
    SOLTEST_API FSOLSolidButtonLook EvaluateButton(ESOLSolidButtonState state, ESOLSolidButtonState previous,
        double sinceChangeS, float s, bool bSelected = false);

    // The same ramp from the look the button showed at the change (atChange's amounts, clamped to [0, 1]), so a hover
    // that reverses mid-ramp continues from where it was instead of popping; no press linger (the caller keeps the
    // state Pressed until PRESSED_LINGER_S after the press, then changes it at the release or the linger's end); the
    // bSelected rule is EvaluateButton's
    SOLTEST_API FSOLSolidButtonLook EvaluateButtonFrom(ESOLSolidButtonState state, const FSOLSolidButtonLook& atChange,
        double sinceChangeS, float s, bool bSelected = false);

    // The render-transform lift: -HOVER_LIFT_PX x Lift (up is negative Y)
    SOLTEST_API float LiftOffset(const FSOLSolidButtonLook& look);

    // --- Tint encoding (section 8.3) ---

    // The brush tint carrying a look: R Edge (also the LED and underline), G a three-level code (0 normal,
    // TINT_DISABLED_CODE disabled, 1 pressed hull), B selected mark, A 1 or DISABLED_ALPHA (a continuous dim only);
    // every channel quantized to 8 bits as the vertex colour is
    SOLTEST_API FLinearColor EncodeButtonTint(const FSOLSolidButtonLook& look);

    // The inverse the material implements: Edge, Led and Underline from R, the flags from G (pressed at or above
    // TINT_PRESSED_G_MIN, disabled from TINT_DISABLED_G_MIN up to it) and B (mark at or above TINT_MARK_B_MIN); A is
    // ignored (Lift, Trace and Glow are not carried and read 0). Slate multiplies the tint's alpha by the widget's
    // inherited opacity in fades, so A decides no flag and the material uses it only as a continuous dim factor. Slate
    // also sends RGB as sRGB 8-bit, so the material must not threshold R on the linear k / 255 grid
    SOLTEST_API FSOLSolidButtonLook DecodeButtonTint(const FLinearColor& tint);

    // --- Panels, traces, pause (section 8.4) ---

    // The panel-open offset: PANEL_SLIDE_PX x (1 - EaseOut(progress)), progress clamped to [0, 1], NaN 0
    SOLTEST_API float PanelSlide(float progress);

    // The pause side panel's x: -(panelWidth + PAUSE_SLIDE_MARGIN_PX) x (1 - EaseOut(progress)), never positive (a
    // negative, NaN or infinite width reads 0)
    SOLTEST_API float PauseSlideX(float progress, float panelWidth);

    // The traces' lit fraction: 0 before TRACE_POWER_DELAY_S, then linear to 1 over TRACE_POWER_S; 1 at s = 0
    SOLTEST_API float TracePower(double sinceOpenS, float s);

    // The traces' brightness factor: 1 + IDLE_PULSE_DEPTH x s x sin(2 pi nowS / IDLE_PULSE_PERIOD_S); exactly 1 at
    // s = 0 and for a non-finite nowS
    SOLTEST_API float IdlePulse(double nowS, float s);

    // (46d) Whether the index-th of count stack LEDs is lit by the power-up's blink: from TRACE_POWER_DELAY_S +
    // TRACE_POWER_S x index / count for LED_BLINK_S (stack order, ending off); never at s = 0 or for an index outside
    // [0, count)
    SOLTEST_API bool PowerUpLedLit(int32 index, int32 count, double sinceOpenS, float s);

    // (46d) Whether the power-up (traces and LED blinks) is still running sinceOpenS after the open; false at s = 0
    SOLTEST_API bool IsPowerUpRunning(double sinceOpenS, float s);

    // (46d) The idle pulse dot's place along the longest trace: the phase of nowS in IDLE_PULSE_PERIOD_S, [0, 1);
    // 0 for a non-finite nowS
    SOLTEST_API float PulseDotFraction(double nowS);

    // (46d) The idle pulse dot's alpha: PULSE_DOT_ALPHA x s (s capped at 2), at most 1; 0 at s = 0 (no dot)
    SOLTEST_API float PulseDotAlpha(float s);

    // The first fraction (clamped to [0, 1]) of a polyline's length: its whole segments, then the partial last one;
    // returns the points written (at least 2, or 0 for a fraction <= 0 or NaN, fewer than 2 points, no length or an
    // outCapacity below 2); never writes past outCapacity
    SOLTEST_API int32 TracePolyline(const FVector2f* points, int32 count, float fraction, FVector2f* out,
        int32 outCapacity);

    // The pause dim's alpha: PAUSE_DIM_ALPHA x SOLHoloMenu::FillFade(progress); PAUSE_DIM_ALPHA at once at s = 0 (the
    // dim is not an effect), so the caller hides the dim once the panel is Closed
    SOLTEST_API float DimAlpha(float progress, float s);

    // --- Panel open / close state (46d, SDD 4.5 and 4.6): the #42 FSOLHoloMenuAnim phases with the solid timings ---

    // The panel's open progress: Closed 0, Open 1, Opening / Closing from PhaseStartProgress at the PANEL_OPEN_S /
    // CLOSE_S rate (a time before the phase start reads the start progress); at s <= 0 or a NaN nowS Opening reads 1
    // and Closing 0
    SOLTEST_API float PanelProgress(const FSOLHoloMenuAnim& anim, double nowS, float s);

    // Starts an open (from 0 when Closed, from the current progress when Closing, so a reopen never pops); true when
    // it did, false (no change) when Opening or Open
    SOLTEST_API bool BeginPanelOpen(FSOLHoloMenuAnim& anim, double nowS, float s);

    // Starts a close from the current progress when Opening / Open; true when it did, false (no change) otherwise
    SOLTEST_API bool BeginPanelClose(FSOLHoloMenuAnim& anim, double nowS, float s);

    // Moves a finished Opening to Open and a finished Closing to Closed (PanelProgress at nowS); idempotent, leaves
    // every other state alone
    SOLTEST_API void AdvancePanel(FSOLHoloMenuAnim& anim, double nowS, float s);

    // (46i) The four panel phase functions above at an explicit open / close duration (the forms above pass
    // PANEL_OPEN_S / CLOSE_S, the pause side panel PAUSE_SLIDE_S / CLOSE_S); every call on one anim passes the same
    // pair
    SOLTEST_API float PanelProgress(const FSOLHoloMenuAnim& anim, double nowS, float s, double openS, double closeS);

    // Starts an open at an explicit open / close duration (see BeginPanelOpen)
    SOLTEST_API bool BeginPanelOpen(FSOLHoloMenuAnim& anim, double nowS, float s, double openS, double closeS);

    // Starts a close at an explicit open / close duration (see BeginPanelClose)
    SOLTEST_API bool BeginPanelClose(FSOLHoloMenuAnim& anim, double nowS, float s, double openS, double closeS);

    // Finishes an open or close at an explicit open / close duration (see AdvancePanel)
    SOLTEST_API void AdvancePanel(FSOLHoloMenuAnim& anim, double nowS, float s, double openS, double closeS);

    // (46g, SDD 4.6) The pause side panel's look at a frame: Progress = PanelProgress at PAUSE_SLIDE_S / CLOSE_S;
    // SlideX = PauseSlideX(Progress, panelWidth); DimAlpha = DimAlpha(Progress, s), but 0 when Closed, and 0 when
    // Closing at s <= 0 (a close at strength 0 is instant); bShown unless Closed; bInteractive only while Opening or
    // Open (a closing panel takes no input: the input is already back, section 4.6)
    SOLTEST_API FSOLPausePanelLook EvaluatePausePanel(const FSOLHoloMenuAnim& anim, double nowS, float s,
        float panelWidth);
}
