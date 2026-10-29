<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 21: HUD hologram rendering

Umbrella issue #21; phases are issues #28 (A), #29 (B) and #30 (C). Depends on SDD 20 (`SOL::Hologram` shared style),
SDD 8 (HUD, menus, settings) and the Limit Theory research (`Docs/Research/LT_features_for_SOL.md` section C).
Status: done. Phase A built (Amendment 1), phase B built (Amendment 2), phase C built (Amendment 3); #21 closed.
Amendment 4: issue #31 polish (less glow, borderless panels, free-look colour).

## 1. Goal

Replace the Canvas-drawn flight HUD with a batched custom-vertex Slate element and one shape material, and give every
flight-HUD element the hologram look, with a 3D radar that reads clearly. Menus and the main menu stay as they are.

## 2. Decisions

| # | Decision |
|---|---|
| D1 | Renderer: one batched Slate custom-vertex element and one shape material (smooth antialiased lines, rings, arcs, filled panels), glow profile `0.8*e^(-2*max(0,d-0.5)) + 0.2*e^(-(0.2d)^0.75)` (LT), additive. |
| D2 | Hologram scope: glow-line shapes plus hologram **panels** (translucent scanline and fresnel-edge backgrounds, flicker) and **text in a glow style**. Uses `SOL::Hologram` from #20. In-world cockpit screens stay a ToDo (`Docs/ToDo/cockpit-3d-screens.md`). |
| D3 | Jitter: subtle, on by default; grain and scanline composite off by default; one strength slider in Settings (persisted, SAVE_VERSION bump). LT palette (`SOL::HudPalette`) and the blue grade (0.40, 0.50, 0.75) are applied. |
| D4 | Ship/UI include cycle is broken with an **event/delegate bus**: the pawn (and subsystems) broadcast HUD events, the HUD subscribes. The HUD must not include the pawn header and the pawn must not include the HUD header. Frame order is made explicit (one update tick per frame before drawing, not field-per-event churn: events carry coalesced snapshots). |
| D5 | Radar: stays 3D on a **ship-horizontal plane**; contacts above blue/cyan, below amber/orange drawn dashed or hollow; smooth glow lines, less pixelated. |
| D6 | Delivery: three issues, each built, tested, reviewed and pushed on its own: A #28 (bus, renderer, shape material, settings slider, proof element), B #29 (port every element, panels, text), C #30 (radar). |

## 3. Open questions (decide in the phase SDD notes with the user if they affect behaviour)

- Glow text implementation (signed-distance font atlas versus the engine font drawn as glow quads) is chosen in phase B
  after measuring readability at 1280x720. **Resolved in Amendment 2:** the engine font drawn by Slate with an outline
  glow.
- Bus payload shape (one coalesced snapshot struct versus several) is chosen in phase A; it must be allocation-free per
  frame.

## 4. Constraints

- No per-frame heap allocation in the HUD hot path; buffers sized once.
- GPU cost measured (`stat gpu`) and written in the SDD per phase.
- The in-game Controls/Info text and `Docs/GAME_MECHANICS.md` stay truthful (new Settings slider).
- Tests: pure-logic parts test-first, authored by a separate subagent from the contract; smokes for the HUD
  (`-SOLSmokeHud` stays green or is updated).

## Amendment 1: phase A as built (issue #28)

**Bus (D4; open question "payload shape" resolved: one coalesced snapshot plus a command signal).**
`HudEvents/SOLHudBus` (plain C++, unit-tested) inside the world subsystem `USOLHudEventSubsystem`:
- `FSOLHudPilotSnapshot` (stick offset and radius, steer icon, steering, free look): the pawn publishes it once per
  tick (`ASOLShipPawn::PublishHudPilot`, TG_PrePhysics, only while player-controlled) and clears it on unpossess and
  EndPlay; the HUD draws the joystick and steer icon from it in `DrawHUD` (after the world tick). A plain struct copy,
  no allocation.
- `ESOLHudCommand` (orbit lines, radar zoom in/out, radar AUTO) broadcast synchronously when the pawn handles the key;
  the HUD subscribes in BeginPlay and unsubscribes in EndPlay. UE multicast delegates promise no order between
  subscribers (the HUD is the only one); the contract and its test say so.
- The HUD publishes its jump-map body overlay pointer for the pawn's map picking (cleared in EndPlay).
- The F3 speed panel no longer holds the pawn: it gets an apply-cap delegate and a close delegate from the pawn.
- Result: `SOLFlightHud.*` and `SOLSpeedPanelWidget.*` do not include `Ship/SOLShipPawn.h`, `SOLShipPawn.*` does not
  include `UI/SOLFlightHud.h` (grep, 2026-10-28); only the verification smoke scripts include both (the pawn header
  includes `UI/SOLHudSmoke.h`). The CLAUDE.md tech-debt item now names only that smoke-harness coupling. The HUD draws
  the pilot only while the snapshot is at most `HUD_PILOT_MAX_FRAME_LAG` (2) frames old. The HUD still reads ship state from `USOLShipSubsystem`, and the pawn still includes
  the panel and `SOLHudFormat` (one direction each).

**Renderer (D1).** `FSOLHudShapeBatch` (pure, unit-tested) builds one quad per shape as `FSlateVertex` in canvas
pixels, reserved once (`SOL::Hologram::HUD_MAX_SHAPES` = 4096; a full batch drops and counts, never reallocates).
Vertex contract: TexCoords[0..1] local pixel position in the shape frame, TexCoords[2] size 1,
TexCoords[3] = kind x 8192 + size 2, colour sRGB(colour x intensity), alpha = arc half aperture / pi or panel fill.
`SSOLHudShapeLayer` (SLeafWidget, hit-test invisible, in the game viewport at z 0) maps the pixels into window space
and emits one `FSlateDrawElement::MakeCustomVerts` with `M_SOLHudShape` (MD_UI, additive, built by
`Tools/UI/create_hud_shape_material.py`, defaults from `SOLConstants.h`). It draws only a batch stamped this frame or
the previous one, so a HUD that stopped drawing leaves nothing. Glow: LT's profile on the signed distance (capsule,
iq arc, rounded box), colour x2, tone curve `1 - e^(-2c^1.25)`, blue grade Bezier (0, .4, .5, .75, 1); LT's maths
runs in display space (the vertex colour is taken back to display space and the result to linear, since Slate's
material path works in linear and gamma-corrects after), and the halo fades out over the outer half of the 32 px
padding so quad edges never show. Panels: fill x (0.4 + 0.6 e^(d/6)) x 1 px scanlines plus a glowing outline.
One-frame latency is possible when Slate paints before `DrawHUD` in a frame (not visible on the reticle), and for the
same reason a HUD that stops drawing leaves its last batch for at most one frame. The layer is volatile (never cached
by an invalidation panel). The arc aperture travels in 8 bits (ends within pi/510 rad). Every number in the HLSL comes
from `SOL::Hologram`; the noise lattice wraps at 289 and time at 1000 s in both the C++ mirror and the HLSL so GPU
`sin` keeps its precision. The panel fill, pad fade and tone/grade have no C++ mirror yet (phase B adds them with the
panels).

**Effects (D3).** Row jitter on by default: `SOLHudShapeMath::JitterOffsetFraction` (mirrored in the HLSL), max
0.001 x strength of the width (strength 2 = LT's 0.002), applied as a screen-x shift moved into each shape's local
frame through `ddx`. Grain (0.10) and scanlines (0.05) only with `sol.HudComposite 1` (default 0), scaled by the same
strength. Settings: "HUD hologram effects" slider 0-2, step 0.05, default 1, `SAVE_VERSION` 5 (v4 saves migrate to
the default). The HUD pushes the material parameters only when they change.

**Proof element (D6 A).** The centre reticle (4 arms, 1 px glow lines, and a dimmer 1 px dot, LT Primary) is drawn
by the renderer; `sol.HudRenderer 0` falls back to the old Canvas reticle. Everything else is unchanged.

**Verification.** Tests: HudShapes 14, HudEventBus 5, settings 4 new (HudEffects sanitize and dirty, v4 -> v5 migration, v5 kept); suite 1037 tests. `-SOLSmokeHud`
20/20 at 1280x720 and 1000x800 (new checks `i-hud-bus-pilot`, `i-shape-renderer`, `i-canvas-fallback`; window
screenshots with the UI `HudSmoke_Glow.png` and `HudSmoke_Canvas.png`, since HighResShot does not capture Slate
layers). The reticle sits on the screen centre at both aspect ratios.

**Cost (ProfileGPU with `r.ProfileGPU.ShowUI 1`, Earth view, 1280x720, editor build `-game`).** The shape batch is one
Slate draw: `Slate Material: MID_M_SOLHudShape_0` 0.011 ms GPU (10 triangles, 20 vertices); the Slate pass was 0.040 ms
with it and 0.012 ms with `sol.HudRenderer 0` (single-frame captures, so within noise of each other at this size).
Game thread: 5 quads written into reserved arrays in `DrawHUD` and one 20-vertex transform copy in `OnPaint`, too
small to show in `stat` (sub-microsecond); `MakeCustomVerts` copies the arrays inside Slate (engine-side, like
Canvas's own batching). Phase B re-measures with every element ported.

## Amendment 2: phase B as built (issue #29)

**Scope.** Every flight-HUD element is drawn by the hologram renderer: reticle, joystick and stick, steer icon, the
speed / frame / absolute / cap readouts and the cap bar, the flight-mode and boost line, the G meter and its bar, the G
warning and blackout / lethal lines, the autopilot, cancel-hint and warp-precision lines, the route marker, the target
readout, bracket, label and edge arrow, the prograde / retrograde markers, the key hints, the controls-changed notice,
the date / anchor / nearest / surface-lock lines and the jump warp streaks. Not ported: the radar (phase C, #30; Canvas
in the LT palette, its two labels already glow text) and the jump map's overlays (body icons, pick, route and grid;
SDD 20's own batches). `sol.HudRenderer 0` still draws everything with Canvas (same layout and palette); `ASOLFlightHud`
routes each primitive through `GlowLine` / `GlowRing` / `GlowBox` / `DrawBar` / `DrawBuffer`, which pick the batch or
Canvas, so the fallback costs one branch per primitive.

**Palette.** LT palette everywhere (`SOL::HudPalette`): Primary for readouts and lines, Secondary for the target, the
locked frame, warnings and the over-cap bar, Good prograde, Hostile retrograde, Bad for G critical / blackout / lethal.
A tone is a colour and an intensity (glow intensity, Canvas RGB scale, or text alpha 0.7 for dim lines). Tuned from the
screenshots: target bracket 0.6 (its corners overlap and add), stick dot 0.6, joystick circles 0.18, bar fill 0.45 and
track 0.1. The F3 panel (UMG) takes the palette too (Primary-tinted text on dark blue, Secondary highlight); its layout is
unchanged.

**Panels (D2).** Each text block gets one panel around the union of its line rectangles (padding
`HUD_PANEL_PADDING_PX` 6 x UI scale): the top-left info lines, the bottom-left speed + G block, the top-right target
readout, the top-centre autopilot + route block, the G warning (Secondary or Bad), the controls notice and the target's
in-world label. A panel is the phase-A panel shape (`HUD_PANEL_FILL` 0.06, outline `HUD_PANEL_EDGE_INTENSITY` 0.35,
fresnel edge fill) plus, new, a dark alpha-blended backing box (`HUD_PANEL_BACKING_COLOR` (0, 0.01, 0.03, 0.45)) under
it for contrast, since the additive glow can only brighten. The panel scanlines now scale with the effects strength
(`HUD_PANEL_SCANLINE` x strength) and every panel flickers: brightness x (1 - `HUD_PANEL_FLICKER` 0.08 x strength x
SmoothNoise(age x 12 Hz + 41)), one global noise (all panels flicker together, like LT's global flicker). The key hints
have no panel (dim help text, often wider than the screen at 720p); the info panel pushes them down by 8 px.

**Glow text (open question resolved).** Options measured: (a) the engine font (GEngine Small / Medium / Large, their
legacy Slate font info, so glyphs and layout match Canvas's `StrLen`) drawn by Slate in the shape layer with an outline
glow, (b) plain Slate text over a drop shadow (`sol.HudTextGlow 0`), (c) the old Canvas text, and the SDF route. UE 5.8
does have Slate SDF / MSDF text (the device profile sets `UI.SlateSDFText.RasterizationMode Msdf`), but it is enabled
per font face asset and would need our own font assets for a glow that (a) already gives; rejected for now. Chosen: (a).
Each line is a drop shadow (black, alpha 0.7, 1 px x UI scale down-right) then the text with its fill moved 30% toward
white (a hot core) and a 1-unit outline in its own colour at alpha 0.35 (the glow). Slate rasterises at the final size,
so the 1.8x speed readout is crisp (Canvas stretched a bitmap). Readability, WCAG contrast of the text (95th-percentile
luminance) against its background (median) in the smoke's Earth view, info / speed / cap / target regions:

| Variant | 1280x720 | 1000x800 |
|---|---|---|
| (a) glow text on panels | 7.4 / 6.7 / 7.7 / 7.5 : 1 | 7.9 / 7.4 / 8.2 / 8.3 : 1 |
| (b) plain text on panels | 7.7 / 8.2 / 8.1 / 6.4 : 1 | 8.2 / 8.6 / 8.7 / 6.9 : 1 |
| (c) Canvas, no panels | 11.1 / 6.1 / 11.4 / 8.3 : 1 | 6.2 / 6.2 / 6.8 / 4.9 : 1 |

All panel variants clear WCAG AA (4.5:1) everywhere and vary little with what is behind (the backing), while Canvas
text depends on the scene (4.9:1 for the target over Earth at 1000x800). The glow costs (b) at most 1.5 contrast points
for the look D2 asks for. Text does not take the row jitter (Slate draws it, not our material); accepted.

**Text and backings in the layer.** `FSOLHudTextList` (pure, `UI/SOLHudTextList`): `HUD_MAX_TEXTS` 64 lines reserved
once; each line keeps a reference-counted copy of the HUD's cached per-line `FText` (no allocation), position, scale,
colour and font slot; a full list drops and counts. `SSOLHudShapeLayer` now paints, in order, the backings
(`HUD_MAX_PANELS` 32, `MakeBox` with a white box brush tinted), the shapes (one custom-vertex draw), the text shadows
and the text (`MakeText`, layer ids +0..+3); everything follows the same frame stamp (this frame or the last). Slate's
own text path copies each string into its draw element (engine-side, like Canvas's own batching).

**Phase-A leftovers.** C++ mirrors of the material's panel fill (`PanelFill`), pad fade (`PadFade`), flicker
(`PanelFlicker`), tone curve (`ToneCurve`), blue grade (`GradeBlue`) and the panel rectangle (`PanelAroundRect`) in
`SOLHudShapeMath`, test-first (`SOLTest.HudPanels.*`, 10 tests written from the contract by a separate agent; red on
stubs, then green).

**Verification.** Suite 1047 tests (1037 + 10); 1048 after the review fixes (+ `BoxCapsule`). `-SOLSmokeHud` 21/21 at 1280x720 and 1000x800, glow and
`sol.HudTextGlow 0` (new check `i-glow-text-panels`: at least 10 text lines and 3 panels; `i-canvas-fallback` also
requires no text lines). Screenshots looked at (`HudSmoke_Glow.png`, `HudSmoke_Canvas.png`).

**Cost (`-game -SOLNoMenu` start view, 1280x720, editor build, ProfileGPU with `r.ProfileGPU.ShowUI 1` and
`stat dumpave -num=240`).** GPU: the shape batch `Slate Material: MID_M_SOLHudShape_0` 0.020 ms (24 shapes, 48
triangles); the game window's whole Slate UI pass 0.046 ms (5 draws, including the backings and all text). Game thread:
`HUD Time` (DrawHUD and the Canvas flush) 0.37 ms with the renderer against 0.53 ms with `sol.HudRenderer 0` (the
Canvas text no longer draws there); `Game UI Paint` 0.071 ms against 0.055 ms (the layer's text moved into Slate's
paint). Measured in the start view only; the heaviest case (map open with target, autopilot, route, controls notice and
G warning panels all showing) was not profiled.

**Adversarial review fixes.** Each line's measured size now lives in its line cache (`MeasureLine`), measured again
only when its text or font changes, so unchanged lines no longer go through `StrLen` every frame (its measure cache is
keyed by the string, so a miss copied the string), and the centred, autopilot and route lines are no longer measured
twice. A bar's box-to-capsule maths moved to `SOLHudShapeMath::BoxCapsule` (tested, `SOLTest.HudPanels.BoxCapsule`)
with the half width clamped to half the box's length, so a nearly empty bar no longer draws a dot wider than its fill
that pokes out of the bar's start. The smoke's `i-glow-text-panels` now checks the text lines and backings the layer
actually painted (a line whose font is missing is no longer counted). The HUD logs one warning if a frame overflows the
shape, text or backing capacity. The Canvas fallback is not pixel-identical to phase A: the bar lost its top and bottom
border lines and the key hints sit 8 px lower (the panel gap), although the fallback draws no panels; accepted as
cosmetic.

## Amendment 3: phase C as built (issue #30)

**Decisions (grill-me, user).** The radar stays 3D on the **ship-horizontal plane** (the ship's forward/right plane,
so it follows pitch and roll), not the ecliptic; contacts above the plane in LT Primary, below in LT Secondary drawn
dashed and hollow; smooth glow lines through the batched renderer; the shared hologram style (panel, flicker, strength
slider); zoom decades, AUTO and the contact set unchanged; no per-frame heap allocation; a contact budget.

**Pure logic (`UI/SOLRadarLayout`, test-first).** `ToShipFrame` (the orientation's UnrotateVector of the offset),
`ClassifySide` (height < 0 is Below; 0 and NaN Above), `ContactStyle` (below: dashed stem, hollow blip; the selected
one large), `DashCount` / `DashSegment` (dashes anchored at the plane, the last clipped), `ScopeToScreen` / `StemTop`
(the plane drawn as an ellipse squashed 0.5, forward up), `AzimuthToScope` / `ShipAzimuth`, `SelectContacts` (the
selected first, then by distance, ties by index, into a reused index array), `SonarRingIntensity` (`0.18 + 0.3 s
|sin(pi (0.5 t - r))|^8`, s the effects strength clamped to 0-2) and `IsCrowded`. The projection's range compression
(whole-vector scaling onto the range sphere, square-root collapse inside 1%) is unchanged. 21 + 3 tests written from
the contract by a separate agent (red on stubs, then green); `IsCrowded` was added after the first screenshots
(test-after, 1 test).

**Two new shape kinds (`M_SOLHudShape`, C++ mirrors in `SOLHudShapeMath`, test-first, 4 + 2 tests).** `Ellipse`
(3): size 1 = X radius, kind param = half width, alpha = Y / X ratio (8 bits: the GPU draws 0.5 as 128/255);
distance `|k0 (k0 - 1) / k1| - w` with k0 = |p/r|, k1 = |p/r^2| (exact on the axes; the shorter radius at the centre;
radii clamped to 1e-3). `DashedLine` (4): a capsule whose distance along the line is measured to the nearest existing
dash (dashes from the start every period, `HUD_DASH_FRACTION` 0.5 of it, none starting exactly at the far end),
period in whole pixels in the alpha. A dashed stem must be one shape: drawn as separate dash quads each dash added the
whole line's halo and a 60 px stem became a white column (first screenshot). The panel-only paths (fill, flicker) are
now gated on the panel kind alone.

**Drawing (`ASOLFlightHud::DrawRadar`).** Same place and size as before (radius 105 px x UI scale, squash 0.5, stem
scale 0.6), on one hologram panel around the disc and range label plus a second dark backing under the disc (the
additive glow can only brighten; over a sunlit Earth the orange contacts washed out to white). Four ellipse range
rings at 25/50/75/100% with the sonar pulse (outer x1.3), faint axes, side and aft ticks, a heading tick ahead, the
camera's horizontal view cone (from the view's focal length; hidden when the camera looks within ~6 degrees of
ship-up or ship-down, where the heading is undefined), a ship chevron. Each contact: a foot tick, a stem (solid or one
dashed shape) and a blip (dot, or a ring of the blip's size below the plane); the selected one larger with a highlight
ring outside the blip and its name in its side's colour. Brightness: blips 0.6, stems 0.45, foot 0.35; non-selected
contacts x0.7 behind the ship and x0.45 beyond the range (pulled onto the rim). Tuned from the screenshots: at 1 km
AUTO with a target every contact sits on the rim and their halos added up to white blots, so a blip within 14 px x UI
scale of one already drawn is skipped (moons on their planet, bunched rim contacts) and does not use the budget;
drawing stops at `MaxDrawnContacts` (64) drawn blips. `sol.HudRenderer 0` draws the same with Canvas (ellipses as
segments, `DashSegment` dashes). Known look issue: a selected contact near the centre overlaps the chevron, cone and
axes, which glow together.

**Smoke.** `-SOLSmokeHud` adds `j-radar-above-below` (ship pitched 30 and rolled 40 degrees from facing Earth, radar
zoomed out to 1 AU: contacts drawn on both sides of the plane, within the budget) and the screenshot
`HudSmoke_Radar.png`; 22/22 at 1280x720 and 1000x800 (5 above, 5 below drawn of 36 contacts). Screenshots looked at
(Glow at 1 km AUTO, Radar at 1 AU, Canvas fallback).

**Cost (`-game -SOLNoMenu` start view, 1280x720, editor build, ProfileGPU with `r.ProfileGPU.ShowUI 1`, `stat
dumpframe`; single frames).** GPU: the shape batch `MID_M_SOLHudShape_0` 0.034 ms (69 shapes, 138 triangles; phase B
0.020 ms for 24), the whole Slate UI pass 0.065 ms (6 draws; phase B 0.046 ms). Game thread: `HUD Time` 0.066 ms and
`Game UI Paint` 0.073 ms in that frame (phase B's 0.37 / 0.071 ms were 240-frame averages, so not comparable
one-to-one; both are well under a tenth of a millisecond here). Per frame the radar adds about 14 shapes plus 3 per
drawn contact (at most ~207 of the 4096 capacity) and 2 backings; no heap allocation (the contact and order arrays
are reserved at BeginPlay, the crowding list is a 64-entry stack array).

**Adversarial review.** Blocker: the radar row in `GAME_MECHANICS.md` still had the pre-tuning numbers, fixed. Fixed
Mediums: the selected below-plane contact's highlight ring sat on its blip ring (now outside the blip's real radius);
crowded skips used up the budget (now the whole order is walked until 64 are drawn); the glow dash fraction silently
assumed dash == gap (now a `static_assert`). Fixed Lows: ellipse radius clamp (C++ and HLSL), the cone flip near
ship-up/down, comments on the 8-bit ratio and whole-pixel dash period, long lines, a parenthesised ternary. Not fixed,
in `CLAUDE.md` tech debt: `SelectContacts` sorts every candidate each frame (fine at ~100 candidates; a bounded heap or
partial selection when NPCs or minor bodies become targetable).

## Amendment 4: issue #31 polish (glow, panels, free-look colour)

**Decisions (grill-me, user).** Halve the glow of every HUD overlay, keeping text legible; while Left Alt free-look
is held the joystick circles turn yellow; no visible rectangle around the radar or any panel (soft, borderless panels
with fading edges, text backing kept).

**Glow.** `HUD_GLOW_HALO_WEIGHT` 0.2 -> 0.1 and `HUD_GLOW_CORE_FALLOFF` 2 -> 3 (about half the energy outside the 1 px
core); `HUD_GLOW_CORE_WEIGHT` 0.8 -> 0.9 so the core still peaks at 1 (`SOLTest.HudShapes.GlowProfile` unchanged).
Glow-text outline alpha `HUD_TEXT_GLOW_ALPHA` 0.35 -> 0.18. The map and route overlays' Canvas halo
(`GLOW_HALO_INTENSITY`, SDD 20) 0.3 -> 0.15. Side effect seen in the screenshots: a thin orange line over the blue
Earth now reads whitish (additive orange + blue), where the wider halo used to tint it; accepted.

**Borderless panels.** The panel kind of `M_SOLHudShape` no longer glows along its outline: it is only its fill,
`fill x smoothstep(0, HUD_PANEL_FEATHER_PX (8), -d)` x scanlines x flicker (`SOLHudShapeMath::PanelFill`, test-first).
`HUD_PANEL_FILL_BASE` and `HUD_PANEL_EDGE_FALLOFF_PX` are gone; `HUD_PANEL_EDGE_INTENSITY` became `HUD_PANEL_INTENSITY`
(the fill's colour scale); corner 4 -> 6 px, padding 6 -> 8 px. The dark backing is no longer one square box (the
CLAUDE.md "square backing corners" item): `SSOLHudShapeLayer` draws each backing as `HUD_PANEL_BACKING_LAYERS` (6)
rounded Slate boxes inset step by step over `HUD_PANEL_BACKING_FEATHER_PX` (8 x UI scale), rounder outside, each with
alpha `1 - (1 - 0.45)^(1/6)` so the stack reaches 0.45 inside (`SOLHudShapeMath::BackingLayer`, test-first). The
radar lost its rectangular panel: a soft stadium backing under the disc (grown 25% of the squashed radius, feathered
over twice that, corner = half its height) and a small panel behind the range label. Cost: at most 32 x 6 = 192
rounded boxes in one Slate layer (one brush, batched), no allocation (backings reserved once). The GPU cost is about 6x alpha-blended overdraw over each backing's area (largest for the radar stadium); not
profiled, accepted at HUD sizes. Panel quads are padded only `HUD_PANEL_QUAD_PAD_PX` (2 px, for the row jitter)
instead of the 32 px glow pad, since a borderless panel draws nothing outside itself.

**Adversarial review (fixed).** Controls rows for SteerHold / SteerToggle still said "on at every flight start";
GAME_MECHANICS still quoted the LT glow numbers; stale "glowing outline" comments and the `edgeIntensity` parameter
name; the panel quad pad; smoke member naming; `i-steer-hint` now compares with the snapshot the HUD drew from; long
lines. `-SOLSmokeMap`'s close checks now expect the cursor shown when the steer toggle is off (the new default).

**Free-look colour.** `SOL::HudPalette::FREE_LOOK` (1, 0.82, 0.25): `SOLHudFormat::PilotCircleColor` gives it to the
joystick circles, stick line and dot while `FSOLHudPilotSnapshot::bIsFreeLooking`, else LT Primary.

**Steer hint (SDD 19 Amendment 5).** The pilot snapshot carries `bShowSteerHint`; the HUD draws
`FHudKeyTexts::SteerHint` ("Hold Space to steer", the SteerHold key's label) centred at 0.8 of the screen height on a
panel, dim Primary, until the player first steers in the session.

**Verification.** Screenshots before (`HudSmoke_Glow.png` from #30) and after at 1280x720 and 1000x800 looked at: no
rectangle around any panel or the radar, thinner glow, text readable, yellow circles in `HudSmoke_FreeLook.png`.
`-SOLSmokeHud` 24/24 at both sizes (new `i-steer-hint`, `k-free-look-yellow`). Tests: HudPanels PanelFill rewritten,
BackingLayer 3 new; HudFormat SteerHint and PilotCircleColor new (written from the contract by a separate agent; red on
stubs, then green).
