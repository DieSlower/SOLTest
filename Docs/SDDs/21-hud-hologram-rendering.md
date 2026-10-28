<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 21: HUD hologram rendering

Umbrella issue #21; phases are issues #28 (A), #29 (B) and #30 (C). Depends on SDD 20 (`SOL::Hologram` shared style),
SDD 8 (HUD, menus, settings) and the Limit Theory research (`Docs/Research/LT_features_for_SOL.md` section C).
Status: design agreed (grill-me complete); phase A built (Amendment 1), B and C open.

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
  after measuring readability at 1280x720.
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
