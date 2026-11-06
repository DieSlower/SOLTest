/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLSolidMenuMath.h"

#include "SOLConstants.h"
#include "UI/SOLHoloMenuMath.h"

namespace
{
    constexpr float SOLIDMATH_INV_SQRT2 = 0.70710678118654752f;    // Unit normal component of a 45-degree line
    constexpr float SOLIDMATH_8BIT_STEPS = 255.0f;                  // Vertex colour channel steps

    static_assert(SOL::Settings::HUD_EFFECTS_MIN == SOL::Settings::HOLO_MENU_EFFECTS_MIN
        && SOL::Settings::HUD_EFFECTS_MAX == SOL::Settings::HOLO_MENU_EFFECTS_MAX,
        "The solid menus sanitize the strength as FSOLHoloMenuParamCache::SanitizeStrength does (SDD 46 section 8)");
    static_assert(2.0f * SOL::SolidMenu::HULL_MAX_FEATURE_FRACTION + SOL::SolidMenu::HULL_MAX_CHAMFER_FRACTION < 1.0f,
        "The title step, the chin and the right edge's chamfers must leave part of the right edge straight");
    static_assert(2.0f * SOL::SolidMenu::HULL_MAX_FEATURE_FRACTION < 1.0f, "The two side notches must never meet");

    //////////////////////////////////////////////////////////////////////////
    // Returns the effects strength NaN-safe and clamped to the HUD effects slider's range (Q1: HudEffectsStrength)
    float SolidMathStrength(const float s)
    {
        return FMath::IsNaN(s) ? 0.0f : FMath::Clamp(s, SOL::Settings::HUD_EFFECTS_MIN, SOL::Settings::HUD_EFFECTS_MAX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an elapsed time with NaN and negative values reading 0
    double SolidMathElapsed(const double seconds)
    {
        return seconds > 0.0 ? seconds : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a hull value with negative, NaN and infinite values reading 0
    float SolidMathLength(const float value)
    {
        return FMath::IsFinite(value) && value > 0.0f ? value : 0.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a value clamped to [0, 1], NaN reading 0
    float SolidMathUnit(const float value)
    {
        return FMath::IsNaN(value) ? 0.0f : FMath::Clamp(value, 0.0f, 1.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a value clamped to [0, 1] and rounded to the 8-bit grid k / 255
    float SolidMathQuantize(const float value)
    {
        return FMath::RoundToFloat(SolidMathUnit(value) * SOLIDMATH_8BIT_STEPS) / SOLIDMATH_8BIT_STEPS;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns 1 - EaseOut(progress) for the cubic ease-out 1 - (1 - p)^3, progress clamped to [0, 1], NaN reading 0
    float SolidMathSlideRemaining(const float progress)
    {
        const float rest = 1.0f - SolidMathUnit(progress);
        return rest * rest * rest;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a size has two finite, positive components
    bool SolidMathIsValidSize(const FVector2f size)
    {
        return FMath::IsFinite(size.X) && FMath::IsFinite(size.Y) && size.X > 0.0f && size.Y > 0.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the common chamfer factor one edge allows: 1, or the factor that makes its pair sum to the limit (the
    // pair summed in double, so two chamfers near FLT_MAX do not overflow)
    float SolidMathChamferFactor(const float chamferA, const float chamferB, const float edge)
    {
        const double pairSum = static_cast<double>(chamferA) + static_cast<double>(chamferB);
        const double limit = static_cast<double>(SOL::SolidMenu::HULL_MAX_CHAMFER_FRACTION) * edge;
        return pairSum > limit ? static_cast<float>(limit / pairSum) : 1.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the settled look of a state: Hovered all lit, Pressed lit with no lift, the others all 0 (amounts only)
    FSOLSolidButtonLook SolidMathSettled(const ESOLSolidButtonState state)
    {
        FSOLSolidButtonLook look;
        if (state == ESOLSolidButtonState::Hovered || state == ESOLSolidButtonState::Pressed)
        {
            look.Lift = state == ESOLSolidButtonState::Hovered ? 1.0f : 0.0f;
            look.Edge = 1.0f;
            look.Led = 1.0f;
            look.Underline = 1.0f;
            look.Trace = 1.0f;
        }
        return look;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the SDF of the region a side notch cuts, in side-local coordinates (q = the distance in from the side)
    float SolidMathNotchCut(const float q, const float y, const FSOLSolidHull& hull)
    {
        const float top = hull.NotchY - 0.5f * hull.NotchHeight;
        const float bottom = hull.NotchY + 0.5f * hull.NotchHeight;
        return FMath::Max3(q - hull.NotchDepth, (q - y + top) * SOLIDMATH_INV_SQRT2,
            (q + y - bottom) * SOLIDMATH_INV_SQRT2);
    }
}

//////////////////////////////////////////////////////////////////////////
// The hull made valid for a size: lengths sanitized, chamfers scaled together, step, chin and notches clamped clear
FSOLSolidHull SOLSolidMenu::ClampHull(const FSOLSolidHull& hull, const FVector2f size)
{
    using namespace SOL::SolidMenu;
    FSOLSolidHull out;
    out.bRecess = hull.bRecess;
    if (!SolidMathIsValidSize(size))
    {
        out.NotchY = 0.0f;
        return out;
    }
    const float w = size.X;
    const float h = size.Y;

    // Chamfers: one common factor, from the tightest edge
    const float tl = SolidMathLength(hull.ChamferTL);
    const float tr = SolidMathLength(hull.ChamferTR);
    const float br = SolidMathLength(hull.ChamferBR);
    const float bl = SolidMathLength(hull.ChamferBL);
    const float factor = FMath::Min(
        FMath::Min(SolidMathChamferFactor(tl, tr, w), SolidMathChamferFactor(tr, br, h)),
        FMath::Min(SolidMathChamferFactor(br, bl, w), SolidMathChamferFactor(bl, tl, h)));
    out.ChamferTL = tl * factor;
    out.ChamferTR = tr * factor;
    out.ChamferBR = br * factor;
    out.ChamferBL = bl * factor;

    // Title step: its flank starts past the top-left chamfer and ends before the top-right one; a step left with no
    // width is removed whole (a lone height would still lower the top-right chamfer)
    const float stepWidth = SolidMathLength(hull.TitleStepWidth);
    float stepHeight = SolidMathLength(hull.TitleStepHeight);
    if (stepWidth > 0.0f && stepHeight > 0.0f)
    {
        stepHeight = FMath::Min3(stepHeight, HULL_MAX_FEATURE_FRACTION * h, w - out.ChamferTL - out.ChamferTR);
        const float width = FMath::Clamp(stepWidth, out.ChamferTL, w - out.ChamferTR - stepHeight);
        if (width > 0.0f && stepHeight > 0.0f)
        {
            out.TitleStepHeight = stepHeight;
            out.TitleStepWidth = width;
        }
    }

    // Chin: its flank starts past the bottom-left chamfer and ends before the bottom-right one; removed whole when
    // left with no width
    const float chinWidth = SolidMathLength(hull.ChinWidth);
    float chinHeight = SolidMathLength(hull.ChinHeight);
    if (chinWidth > 0.0f && chinHeight > 0.0f)
    {
        chinHeight = FMath::Min3(chinHeight, HULL_MAX_FEATURE_FRACTION * h, w - out.ChamferBL - out.ChamferBR);
        const float width = FMath::Clamp(chinWidth, out.ChamferBL, w - out.ChamferBR - chinHeight);
        if (width > 0.0f && chinHeight > 0.0f)
        {
            out.ChinHeight = chinHeight;
            out.ChinWidth = width;
        }
    }

    // Notches: inside the straight part both side edges share, at most half as deep as tall (a V at the limit)
    const float notchDepth = SolidMathLength(hull.NotchDepth);
    float notchHeight = SolidMathLength(hull.NotchHeight);
    if (hull.NotchY >= 0.0f && notchDepth > 0.0f && notchHeight > 0.0f)
    {
        const float low = FMath::Max(out.ChamferTL, out.TitleStepHeight + out.ChamferTR);
        const float high = FMath::Min(h - out.ChamferBL, h - out.ChinHeight - out.ChamferBR);
        notchHeight = FMath::Min(notchHeight, high - low);
        if (notchHeight > 0.0f)
        {
            out.NotchHeight = notchHeight;
            out.NotchY = FMath::Clamp(hull.NotchY, low + 0.5f * notchHeight, high - 0.5f * notchHeight);
            out.NotchDepth = FMath::Min3(notchDepth, 0.5f * notchHeight, HULL_MAX_FEATURE_FRACTION * w);
        }
    }
    return out;
}

//////////////////////////////////////////////////////////////////////////
// The hull SDF: the chamfered rect's half-planes, minus the title step, chin and notch cuts
float SOLSolidMenu::HullDistance(const FVector2f p, const FVector2f size, const FSOLSolidHull& hull)
{
    if (!SolidMathIsValidSize(size) || !FMath::IsFinite(p.X) || !FMath::IsFinite(p.Y))
    {
        return TNumericLimits<float>::Max();
    }
    const float w = size.X;
    const float h = size.Y;

    // The convex body: the rect and the four chamfer lines (top-right on the stepped-down top edge, bottom-right on
    // the edge right of the chin)
    float distance = FMath::Max(FMath::Max(-p.X, p.X - w), FMath::Max(-p.Y, p.Y - h));
    distance = FMath::Max(distance, (hull.ChamferTL - p.X - p.Y) * SOLIDMATH_INV_SQRT2);
    distance = FMath::Max(distance, (hull.ChamferTR - w + p.X - p.Y + hull.TitleStepHeight) * SOLIDMATH_INV_SQRT2);
    distance = FMath::Max(distance,
        (hull.ChamferBR - w + p.X - h + hull.ChinHeight + p.Y) * SOLIDMATH_INV_SQRT2);
    distance = FMath::Max(distance, (hull.ChamferBL - p.X - h + p.Y) * SOLIDMATH_INV_SQRT2);

    // Title step: the top edge right of the step's 45-degree flank lies TitleStepHeight lower
    if (hull.TitleStepWidth > 0.0f && hull.TitleStepHeight > 0.0f)
    {
        const float cut = FMath::Max(p.Y - hull.TitleStepHeight,
            (hull.TitleStepWidth - p.X + p.Y) * SOLIDMATH_INV_SQRT2);
        distance = FMath::Max(distance, -cut);
    }

    // Chin: the bottom edge right of the chin's 45-degree flank lies ChinHeight higher
    if (hull.ChinWidth > 0.0f && hull.ChinHeight > 0.0f)
    {
        const float cut = FMath::Max(h - hull.ChinHeight - p.Y,
            (hull.ChinWidth + h - p.X - p.Y) * SOLIDMATH_INV_SQRT2);
        distance = FMath::Max(distance, -cut);
    }

    // Side notches: the same trapezoid cut into the left and the right edge
    if (hull.NotchY >= 0.0f && hull.NotchDepth > 0.0f && hull.NotchHeight > 0.0f)
    {
        distance = FMath::Max(distance, -SolidMathNotchCut(p.X, p.Y, hull));
        distance = FMath::Max(distance, -SolidMathNotchCut(w - p.X, p.Y, hull));
    }
    return distance;
}

//////////////////////////////////////////////////////////////////////////
// The button's look from its previous state: the press linger, then the ramp from that state's settled look
FSOLSolidButtonLook SOLSolidMenu::EvaluateButton(const ESOLSolidButtonState state,
    const ESOLSolidButtonState previous, const double sinceChangeS, const float s, const bool bSelected)
{
    using namespace SOL::SolidMenu;
    const double elapsedS = SolidMathElapsed(sinceChangeS);
    const bool bLeavingPress = previous == ESOLSolidButtonState::Pressed && state != ESOLSolidButtonState::Pressed
        && state != ESOLSolidButtonState::Disabled;
    if (bLeavingPress && elapsedS < PRESSED_LINGER_S)
    {
        // The linger shows the pressed hull; the mark follows the new state
        return EvaluateButtonFrom(ESOLSolidButtonState::Pressed, FSOLSolidButtonLook(), 0.0, s,
            bSelected || state == ESOLSolidButtonState::Selected);
    }
    const double rampS = bLeavingPress ? elapsedS - PRESSED_LINGER_S : elapsedS;
    return EvaluateButtonFrom(state, SolidMathSettled(previous), rampS, s, bSelected);
}

//////////////////////////////////////////////////////////////////////////
// The button's look from the amounts at the change: Disabled all 0, Pressed at once, else a linear ramp to the state
FSOLSolidButtonLook SOLSolidMenu::EvaluateButtonFrom(const ESOLSolidButtonState state,
    const FSOLSolidButtonLook& atChange, const double sinceChangeS, const float s, const bool bSelected)
{
    FSOLSolidButtonLook look;
    if (state == ESOLSolidButtonState::Disabled)
    {
        look.bDisabled = true;
        return look;
    }
    const float strength = SolidMathStrength(s);
    const FSOLSolidButtonLook to = SolidMathSettled(state);

    // Pressed shows at once; the other states ramp from the amounts at the change (instant at strength 0)
    const float alpha = state == ESOLSolidButtonState::Pressed || strength <= 0.0f ? 1.0f
        : static_cast<float>(FMath::Clamp(SolidMathElapsed(sinceChangeS)
            / static_cast<double>(SOL::SolidMenu::HOVER_RAMP_S), 0.0, 1.0));
    look.Lift = FMath::Lerp(SolidMathUnit(atChange.Lift), to.Lift, alpha);
    look.Edge = FMath::Lerp(SolidMathUnit(atChange.Edge), to.Edge, alpha);
    look.Led = FMath::Lerp(SolidMathUnit(atChange.Led), to.Led, alpha);
    look.Underline = FMath::Lerp(SolidMathUnit(atChange.Underline), to.Underline, alpha);
    look.Trace = FMath::Lerp(SolidMathUnit(atChange.Trace), to.Trace, alpha);
    look.Glow = strength * look.Edge;
    look.bPressedHull = state == ESOLSolidButtonState::Pressed;
    look.bSelectedMark = bSelected || state == ESOLSolidButtonState::Selected;
    return look;
}

//////////////////////////////////////////////////////////////////////////
// The render-transform lift of a look (up is negative Y)
float SOLSolidMenu::LiftOffset(const FSOLSolidButtonLook& look)
{
    return -SOL::SolidMenu::HOVER_LIFT_PX * look.Lift;
}

//////////////////////////////////////////////////////////////////////////
// The 8-bit brush tint of a look: R edge, G three-level code, B selected mark, A enabled or DISABLED_ALPHA
FLinearColor SOLSolidMenu::EncodeButtonTint(const FSOLSolidButtonLook& look)
{
    using namespace SOL::SolidMenu;
    const float code = look.bPressedHull ? 1.0f : (look.bDisabled ? TINT_DISABLED_CODE : 0.0f);
    return FLinearColor(SolidMathQuantize(look.Edge), SolidMathQuantize(code), look.bSelectedMark ? 1.0f : 0.0f,
        look.bDisabled ? SolidMathQuantize(DISABLED_ALPHA) : 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// The look a brush tint carries (what the hull material decodes)
FSOLSolidButtonLook SOLSolidMenu::DecodeButtonTint(const FLinearColor& tint)
{
    FSOLSolidButtonLook look;
    look.Edge = SolidMathQuantize(tint.R);
    look.Led = look.Edge;
    look.Underline = look.Edge;
    look.bPressedHull = tint.G >= SOL::SolidMenu::TINT_PRESSED_G_MIN;
    look.bDisabled = !look.bPressedHull && tint.G >= SOL::SolidMenu::TINT_DISABLED_G_MIN;
    look.bSelectedMark = tint.B >= SOL::SolidMenu::TINT_MARK_B_MIN;
    return look;
}

//////////////////////////////////////////////////////////////////////////
// The panel-open offset at an open progress
float SOLSolidMenu::PanelSlide(const float progress)
{
    return SOL::SolidMenu::PANEL_SLIDE_PX * SolidMathSlideRemaining(progress);
}

//////////////////////////////////////////////////////////////////////////
// The pause side panel's x at a slide progress
float SOLSolidMenu::PauseSlideX(const float progress, const float panelWidth)
{
    return -(SolidMathLength(panelWidth) + SOL::SolidMenu::PAUSE_SLIDE_MARGIN_PX) * SolidMathSlideRemaining(progress);
}

//////////////////////////////////////////////////////////////////////////
// The traces' lit fraction sinceOpenS after the panel opened
float SOLSolidMenu::TracePower(const double sinceOpenS, const float s)
{
    using namespace SOL::SolidMenu;
    if (SolidMathStrength(s) <= 0.0f)
    {
        return 1.0f;
    }
    const double litS = SolidMathElapsed(sinceOpenS) - TRACE_POWER_DELAY_S;
    return static_cast<float>(FMath::Clamp(litS / static_cast<double>(TRACE_POWER_S), 0.0, 1.0));
}

//////////////////////////////////////////////////////////////////////////
// The traces' brightness factor at real time nowS (the phase taken in double, so it stays exact late in a session)
float SOLSolidMenu::IdlePulse(const double nowS, const float s)
{
    using namespace SOL::SolidMenu;
    const float strength = SolidMathStrength(s);
    if (strength <= 0.0f || !FMath::IsFinite(nowS))
    {
        return 1.0f;
    }
    const double period = IDLE_PULSE_PERIOD_S;
    const double phase = FMath::Fmod(nowS, period) / period;
    return 1.0f + IDLE_PULSE_DEPTH * strength * static_cast<float>(FMath::Sin(UE_DOUBLE_TWO_PI * phase));
}

//////////////////////////////////////////////////////////////////////////
// Whether a stack LED is in its power-up blink: each one lights in stack order across the trace power-up
bool SOLSolidMenu::PowerUpLedLit(const int32 index, const int32 count, const double sinceOpenS, const float s)
{
    using namespace SOL::SolidMenu;
    if (SolidMathStrength(s) <= 0.0f || count <= 0 || index < 0 || index >= count)
    {
        return false;
    }
    const double onS = TRACE_POWER_DELAY_S + static_cast<double>(TRACE_POWER_S) * index / count;
    const double elapsedS = SolidMathElapsed(sinceOpenS);
    return elapsedS >= onS && elapsedS < onS + LED_BLINK_S;
}

//////////////////////////////////////////////////////////////////////////
// Whether the traces or a stack LED blink are still moving (the last LED goes off after the traces are lit)
bool SOLSolidMenu::IsPowerUpRunning(const double sinceOpenS, const float s)
{
    using namespace SOL::SolidMenu;
    const double endS = static_cast<double>(TRACE_POWER_DELAY_S) + TRACE_POWER_S + LED_BLINK_S;
    return SolidMathStrength(s) > 0.0f && SolidMathElapsed(sinceOpenS) < endS;
}

//////////////////////////////////////////////////////////////////////////
// The pulse dot's place along the longest trace: the phase of the pulse period (taken in double, exact late on)
float SOLSolidMenu::PulseDotFraction(const double nowS)
{
    using namespace SOL::SolidMenu;
    if (!FMath::IsFinite(nowS))
    {
        return 0.0f;
    }
    const double period = IDLE_PULSE_PERIOD_S;
    const double phase = FMath::Fmod(nowS, period) / period;
    const float fraction = static_cast<float>(phase < 0.0 ? phase + 1.0 : phase);
    return fraction < 1.0f ? fraction : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// The pulse dot's alpha: PULSE_DOT_ALPHA per unit of strength, at most 1, none at strength 0
float SOLSolidMenu::PulseDotAlpha(const float s)
{
    return FMath::Min(SOL::SolidMenu::PULSE_DOT_ALPHA * SolidMathStrength(s), 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// The first fraction of a polyline's length into out, never past outCapacity
int32 SOLSolidMenu::TracePolyline(const FVector2f* points, const int32 count, const float fraction, FVector2f* out,
    const int32 outCapacity)
{
    if (points == nullptr || out == nullptr || count < 2 || outCapacity < 2 || !(fraction > 0.0f))
    {
        return 0;
    }

    // The total length, then the length to cover
    float total = 0.0f;
    for (int32 index = 1; index < count; ++index)
    {
        total += FVector2f::Distance(points[index - 1], points[index]);
    }
    if (!(total > 0.0f))
    {
        return 0;
    }
    const float target = FMath::Min(fraction, 1.0f) * total;

    // Whole segments while they fit, then the partial last one
    out[0] = points[0];
    int32 written = 1;
    float covered = 0.0f;
    for (int32 index = 1; index < count && written < outCapacity; ++index)
    {
        const float segment = FVector2f::Distance(points[index - 1], points[index]);
        if (covered + segment >= target)
        {
            const float alpha = segment > 0.0f ? (target - covered) / segment : 1.0f;
            out[written++] = FMath::Lerp(points[index - 1], points[index], alpha);
            break;
        }
        covered += segment;
        out[written++] = points[index];
    }
    return written;
}

//////////////////////////////////////////////////////////////////////////
// The pause dim's alpha at a slide progress
float SOLSolidMenu::DimAlpha(const float progress, const float s)
{
    using namespace SOL::SolidMenu;
    return SolidMathStrength(s) <= 0.0f ? PAUSE_DIM_ALPHA : PAUSE_DIM_ALPHA * SOLHoloMenu::FillFade(progress);
}

//////////////////////////////////////////////////////////////////////////
// The panel's open progress: the #42 phases at the solid open / close rates, a NaN time reading the phase's end
float SOLSolidMenu::PanelProgress(const FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    return PanelProgress(anim, nowS, s, SOL::SolidMenu::PANEL_OPEN_S, SOL::SolidMenu::CLOSE_S);
}

//////////////////////////////////////////////////////////////////////////
// Starts a panel open (from 0 or from the current progress of a close)
bool SOLSolidMenu::BeginPanelOpen(FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    return BeginPanelOpen(anim, nowS, s, SOL::SolidMenu::PANEL_OPEN_S, SOL::SolidMenu::CLOSE_S);
}

//////////////////////////////////////////////////////////////////////////
// Starts a panel close from the current progress
bool SOLSolidMenu::BeginPanelClose(FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    return BeginPanelClose(anim, nowS, s, SOL::SolidMenu::PANEL_OPEN_S, SOL::SolidMenu::CLOSE_S);
}

//////////////////////////////////////////////////////////////////////////
// Finishes a panel open or close that has reached its end (at once for a NaN time, which reads the end)
void SOLSolidMenu::AdvancePanel(FSOLHoloMenuAnim& anim, const double nowS, const float s)
{
    AdvancePanel(anim, nowS, s, SOL::SolidMenu::PANEL_OPEN_S, SOL::SolidMenu::CLOSE_S);
}

//////////////////////////////////////////////////////////////////////////
// The panel's open progress at an open / close duration: the #42 phases, a NaN time reading the phase's end
float SOLSolidMenu::PanelProgress(const FSOLHoloMenuAnim& anim, const double nowS, const float s, const double openS,
    const double closeS)
{
    if (FMath::IsNaN(nowS))
    {
        return anim.Phase == ESOLHoloMenuPhase::Opening || anim.Phase == ESOLHoloMenuPhase::Open ? 1.0f : 0.0f;
    }
    return SOLHoloMenu::Progress(anim, nowS, s, openS, closeS);
}

//////////////////////////////////////////////////////////////////////////
// Starts a panel open at an open / close duration
bool SOLSolidMenu::BeginPanelOpen(FSOLHoloMenuAnim& anim, const double nowS, const float s, const double openS,
    const double closeS)
{
    return SOLHoloMenu::BeginOpen(anim, nowS, s, openS, closeS);
}

//////////////////////////////////////////////////////////////////////////
// Starts a panel close at an open / close duration
bool SOLSolidMenu::BeginPanelClose(FSOLHoloMenuAnim& anim, const double nowS, const float s, const double openS,
    const double closeS)
{
    return SOLHoloMenu::BeginClose(anim, nowS, s, openS, closeS);
}

//////////////////////////////////////////////////////////////////////////
// Finishes a panel open or close at an open / close duration (at once for a NaN time, which reads the end)
void SOLSolidMenu::AdvancePanel(FSOLHoloMenuAnim& anim, const double nowS, const float s, const double openS,
    const double closeS)
{
    if (!FMath::IsNaN(nowS))
    {
        SOLHoloMenu::Advance(anim, nowS, s, openS, closeS);
    }
    else if (anim.Phase == ESOLHoloMenuPhase::Opening)
    {
        anim.Phase = ESOLHoloMenuPhase::Open;
        anim.PhaseStartProgress = 1.0f;
    }
    else if (anim.Phase == ESOLHoloMenuPhase::Closing)
    {
        anim.Phase = ESOLHoloMenuPhase::Closed;
        anim.PhaseStartProgress = 0.0f;
    }
}

//////////////////////////////////////////////////////////////////////////
// The pause side panel's look at a frame: the composed open progress, slide and dim; input only while it opens or is
// open (SDD 4.6)
FSOLPausePanelLook SOLSolidMenu::EvaluatePausePanel(const FSOLHoloMenuAnim& anim, const double nowS, const float s,
    const float panelWidth)
{
    FSOLPausePanelLook look;
    look.Progress = PanelProgress(anim, nowS, s, SOL::SolidMenu::PAUSE_SLIDE_S, SOL::SolidMenu::CLOSE_S);
    look.SlideX = PauseSlideX(look.Progress, panelWidth);
    look.bShown = anim.Phase != ESOLHoloMenuPhase::Closed;
    look.bInteractive = anim.Phase == ESOLHoloMenuPhase::Opening || anim.Phase == ESOLHoloMenuPhase::Open;
    const bool bInstantClose = anim.Phase == ESOLHoloMenuPhase::Closing && !(s > 0.0f);
    look.DimAlpha = look.bShown && !bInstantClose ? DimAlpha(look.Progress, s) : 0.0f;
    return look;
}
