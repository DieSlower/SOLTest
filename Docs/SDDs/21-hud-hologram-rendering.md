<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 21: HUD hologram rendering

Umbrella issue #21; phases are issues #28 (A), #29 (B) and #30 (C). Depends on SDD 20 (`SOL::Hologram` shared style),
SDD 8 (HUD, menus, settings) and the Limit Theory research (`Docs/Research/LT_features_for_SOL.md` section C).
Status: design agreed (grill-me complete), not implemented.

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
