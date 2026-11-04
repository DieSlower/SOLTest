/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloRadial.h"

#include "SOLConstants.h"
#include "UI/SOLHoloFrameBuild.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns whether the anim is opening or open (a menu is up as far as the anim knows)
    bool HoloRadialIsUp(const FSOLHoloMenuAnim& anim)
    {
        return anim.Phase == ESOLHoloMenuPhase::Opening || anim.Phase == ESOLHoloMenuPhase::Open;
    }

    //////////////////////////////////////////////////////////////////////////
    // Puts the anim back to Closed at once, keeping the hover cue's rate limit
    void HoloRadialSnapClosed(FSOLHoloMenuAnim& anim)
    {
        const double lastHoverCueS = anim.LastHoverCueS;
        anim = FSOLHoloMenuAnim();
        anim.LastHoverCueS = lastHoverCueS;
    }
}

//////////////////////////////////////////////////////////////////////////
// Updates the radial's anim and cached state for one HUD frame and returns the cues to play
FSOLHoloRadialCues SOLHoloRadial::Update(FSOLHoloRadialState& state, const FSOLRadialMenuState& live,
    const FSOLActionList& liveActions, const int32 openCount, const int32 confirmCount, const bool bShown,
    const bool bContinuous, const double nowS, const float s)
{
    FSOLHoloRadialCues cues;
    const bool bIsVisible = live.bOpen && bShown;

    // First update: sync the counters only; a menu already open shows whole (nothing to animate from)
    if (state.SeenOpenCount == INDEX_NONE || state.SeenConfirmCount == INDEX_NONE)
    {
        state.SeenOpenCount = openCount;
        state.SeenConfirmCount = confirmCount;
        HoloRadialSnapClosed(state.Anim);
        if (bIsVisible)
        {
            state.Radial = live;
            state.Actions = liveActions;
            state.Anim.Phase = ESOLHoloMenuPhase::Open;
            state.Anim.PhaseStartS = nowS;
            state.Anim.PhaseStartProgress = 1.0f;
            SOLHoloMenu::SetHovered(state.Anim, live.Highlighted, nowS);
        }
        return cues;
    }

    // Events since the last update, from the interaction's counters
    const bool bIsNewOpen = openCount != state.SeenOpenCount;
    cues.bOpen = bIsNewOpen;
    cues.bSelect = confirmCount != state.SeenConfirmCount;
    state.SeenOpenCount = openCount;
    state.SeenConfirmCount = confirmCount;

    // Open: cache the live menu every frame (the close draws from the last copy); a new menu always draws in from
    // Closed, even over the previous menu's close (never reversing from the old menu's progress)
    if (bIsVisible)
    {
        if (bIsNewOpen)
        {
            HoloRadialSnapClosed(state.Anim);
        }
        SOLHoloMenu::BeginOpen(state.Anim, nowS, s);
        state.Radial = live;
        state.Actions = liveActions;
    }
    else if (HoloRadialIsUp(state.Anim))
    {
        // Closed (or hidden): play the close from the cached state, unless frames were skipped (then at once). The
        // close keeps the last hovered sector and its glow time (the kit's BeginClose clears them), so the glow fades
        // with the fill instead of dropping in one frame (SDD 5: backwards from the last drawn state)
        if (bContinuous)
        {
            const int32 hoveredItem = state.Anim.HoveredItem;
            const double hoverStartS = state.Anim.HoverStartS;
            SOLHoloMenu::BeginClose(state.Anim, nowS, s);
            state.Anim.HoveredItem = hoveredItem;
            state.Anim.HoverStartS = hoverStartS;
        }
        else
        {
            HoloRadialSnapClosed(state.Anim);
        }
    }
    else if (!bContinuous && state.Anim.Phase == ESOLHoloMenuPhase::Closing)
    {
        HoloRadialSnapClosed(state.Anim);
    }
    SOLHoloMenu::Advance(state.Anim, nowS, s);

    // The highlighted sector is the hovered item; a change while Open asks for the (rate limited) hover cue, never on
    // the opening update (at strength 0 the anim is Open there already; the press plays the open cue only, D21)
    if (bIsVisible)
    {
        const bool bIsHoverChanged = SOLHoloMenu::SetHovered(state.Anim, live.Highlighted, nowS);
        cues.bHover = !bIsNewOpen && SOLHoloMenu::ShouldPlayHoverCue(state.Anim, bIsHoverChanged, nowS);
    }
    return cues;
}

//////////////////////////////////////////////////////////////////////////
// Returns how one sector draws from the menu's look: draw-in aperture, edge and fill glow, colour and glow pad
FSOLHoloRadialSectorLook SOLHoloRadial::SectorLook(const float halfApertureRad, const bool bHighlighted,
    const bool bEnabled, const FSOLHoloMenuLook& look, const float s)
{
    FSOLHoloRadialSectorLook sector;
    SOLHoloMenu::RadialArcDrawIn(halfApertureRad, look.FrameFraction, sector.HalfApertureRad);
    const float fillAlpha = FadeAlpha(look.FillAlpha);
    const float baseIntensity = !bEnabled ? SOL::Interact::RADIAL_DISABLED_INTENSITY
        : bHighlighted ? SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY : SOL::Interact::RADIAL_INTENSITY;
    const float glowScale = bEnabled ? 1.0f : SOL::HoloMenu::RADIAL_DISABLED_HOVER_GLOW_SCALE;
    sector.EdgeIntensity = baseIntensity
        + (bHighlighted ? FMath::Max(look.HoverIntensity, 0.0f) * fillAlpha * glowScale : 0.0f);
    sector.FillIntensity = bHighlighted && bEnabled ? SOL::Interact::RADIAL_FILL_INTENSITY * fillAlpha : 0.0f;
    sector.Color = bHighlighted ? SOL::HudPalette::SECONDARY : SOL::HudPalette::PRIMARY;
    sector.PadPx = SOLHoloFrameBuild::LinePad(s);
    return sector;
}

//////////////////////////////////////////////////////////////////////////
// Returns a sector label's colour, its alpha faded by the fill
FLinearColor SOLHoloRadial::LabelColor(const bool bHighlighted, const bool bEnabled, const float fillAlpha)
{
    return Faded(bHighlighted ? SOL::HudPalette::SECONDARY
        : bEnabled ? SOL::HudPalette::PRIMARY
        : SOL::HudPalette::PRIMARY * SOL::Interact::DISABLED_TEXT_SCALE, fillAlpha);
}

//////////////////////////////////////////////////////////////////////////
// Returns a fill alpha clamped to [0, 1], NaN as 0
float SOLHoloRadial::FadeAlpha(const float fillAlpha)
{
    return FMath::IsNaN(fillAlpha) ? 0.0f : FMath::Clamp(fillAlpha, 0.0f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Returns a colour with its alpha times the clamped fill alpha
FLinearColor SOLHoloRadial::Faded(const FLinearColor& color, const float fillAlpha)
{
    FLinearColor faded = color;
    faded.A *= FadeAlpha(fillAlpha);
    return faded;
}
