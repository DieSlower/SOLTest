/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLInteractMenus.h"
#include "UI/SOLHoloMenuMath.h"

#include "CoreMinimal.h"

// The radial menu as the HUD last drew it (SDD 42 section 4.5): its anim, a copy of the menu state and actions (value
// types, no allocation) to draw the close animation from after the menu closed, and the interaction counters last
// seen (open and confirm), which tell the per-frame update a menu opened or an action was confirmed
struct FSOLHoloRadialState
{
    FSOLHoloMenuAnim Anim;
    FSOLRadialMenuState Radial;             // The last drawn menu state (kept while it closes)
    FSOLActionList Actions;                 // Its actions
    int32 SeenOpenCount = INDEX_NONE;       // The interaction's radial open count at the last update (none: unsynced)
    int32 SeenConfirmCount = INDEX_NONE;    // ...and its confirm count
};

// The cues one update asks for (each played through the hologram menu subsystem, whatever the strength, D21)
struct FSOLHoloRadialCues
{
    bool bOpen = false;     // A menu opened (the opening press)
    bool bHover = false;    // The highlighted sector changed while Open (rate limited by the anim)
    bool bSelect = false;   // An enabled action was confirmed (never for a cancel)
};

// How one radial sector draws this frame (SDD 42 section 5, the radial row)
struct FSOLHoloRadialSectorLook
{
    float HalfApertureRad = 0.0f;   // Rim arcs' half aperture, grown from the sector's middle (RadialArcDrawIn)
    float EdgeIntensity = 0.0f;     // Rim arcs' glow: today's (enabled / highlighted / disabled) plus the hover glow
    float FillIntensity = 0.0f;     // The highlighted enabled sector's fill arc, faded by FillAlpha; 0 otherwise
    FLinearColor Color = FLinearColor::Transparent;     // Secondary when highlighted (also at strength 0), else Primary
    float PadPx = 0.0f;             // Glow pad of every arc (SOLHoloFrameBuild::LinePad: flat at strength 0)
};

/**
 * Pure, renderer-free logic of the hologram radial menu (SDD 42 sections 4.5 and 5): the per-frame update of its anim
 * and cues from the interaction's state and counters, and how each sector and label draws from the anim's look.
 * `s` is the hologram menu strength (clamped by the SOLHoloMenu functions); times are real seconds passed in.
 */
namespace SOLHoloRadial
{
    // Updates the anim and the cached state for one HUD frame and returns the cues to play. live / liveActions are
    // the interaction's menu now; openCount / confirmCount its radial open and confirm counters; bShown false when
    // the radial cannot show (the map is open); bContinuous false when HUD frames were skipped since the last update
    // (a menu that closed meanwhile then closes at once, never playing a stale close later). Rules: the first update
    // only syncs the counters (no cues; an open menu shows at once, Open). A changed open count asks for the open cue,
    // a changed confirm count for the select cue. A shown open menu is cached every update and opens the anim (a new
    // menu always restarts it from Closed, also over a close still playing, keeping the hover cue's rate limit); a
    // closed or hidden one starts the close (from the cached state, keeping the hovered sector so its glow fades with
    // the fill). Then Advance; while shown and open, the highlighted sector is the anim's hovered item and a change
    // asks for the hover cue by ShouldPlayHoverCue, never on the update that saw the open (D21)
    SOLTEST_API FSOLHoloRadialCues Update(FSOLHoloRadialState& state, const FSOLRadialMenuState& live,
        const FSOLActionList& liveActions, int32 openCount, int32 confirmCount, bool bShown, bool bContinuous,
        double nowS, float s);

    // Returns how one sector draws: its rim arcs grown by look.FrameFraction, today's edge intensity (disabled,
    // highlighted, else normal) plus look.HoverIntensity x look.FillAlpha on the highlighted sector (times
    // RADIAL_DISABLED_HOVER_GLOW_SCALE when it is disabled; FillAlpha through FadeAlpha), the highlighted
    // enabled sector's fill at RADIAL_FILL_INTENSITY x FillAlpha, Secondary when highlighted, and LinePad(s)
    SOLTEST_API FSOLHoloRadialSectorLook SectorLook(float halfApertureRad, bool bHighlighted, bool bEnabled,
        const FSOLHoloMenuLook& look, float s);

    // Returns a sector label's colour: Secondary when highlighted, Primary when enabled, else Primary dimmed by
    // DISABLED_TEXT_SCALE (as before), its alpha times look's FillAlpha (clamped to [0, 1])
    SOLTEST_API FLinearColor LabelColor(bool bHighlighted, bool bEnabled, float fillAlpha);

    // Returns a fill alpha clamped to [0, 1], NaN as 0 (every fade of the radial goes through it)
    SOLTEST_API float FadeAlpha(float fillAlpha);

    // Returns the colour with its alpha times FadeAlpha(fillAlpha) (the title and the reason line)
    SOLTEST_API FLinearColor Faded(const FLinearColor& color, float fillAlpha);
}
