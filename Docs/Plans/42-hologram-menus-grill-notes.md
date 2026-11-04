<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->
# #42 hologram menus and #46 solid menus: grill-me notes (paused)

Status: grill paused. #42 was split on 2026-11-03: #42 keeps the hologram in-flight menus, the new #46 takes the full-screen menus. The agent texture/material tools (MaterialCraft, TextureCraft, PhotoCraft, VectorCraft) are now available and were used to make the #46 style samples.

## Decided

- **#42 = hologram in-flight menus only:** HUD, radial menu, Overlay Menu, speed panel, target/info cards (and similar panels used while flying).
- **#46 = solid console-style menus** (new ticket): main menu, settings, demo, Controls / Info / About and pause.
  - "Info" is the in-game Controls / Info / About mechanics-explainer screen: full-screen, read-only text. (The in-flight body info card is a hologram card and stays in #42.)
  - The Pause menu is in #46 (solid), even though it opens over live flight.
  - Look: a solid brushed-metal console with textured panels, clearly different from the hologram effect.
- **Input unchanged** for both: mouse and keyboard as today, plus the #22 hologram cursor and hover glow. A keyboard / gamepad navigation redesign is a future idea, not part of either ticket.
- **Textures and materials for #46 are generated now** with the MCP texture/material tools, not deferred to the #9 art pass. So #46 does not depend on #9.
- **The user cannot pick a palette without seeing samples.** Three sample boards were made for that:
  - `Docs/Images/menu-style-samples/direction-1.png` (gunmetal console, cyan accent)
  - `Docs/Images/menu-style-samples/direction-2.png` (brass industrial, amber accent)
  - `Docs/Images/menu-style-samples/direction-3.png` (cool light aluminium, signal orange + info blue)
  - Research and palettes: `Docs/Research/SOLID_MENU_STYLE_REFERENCES.md`.
- #46 depends on #42 for the shared hover glow and cursor.
- **Direction 1 is the base** (2026-11-03): dark gunmetal brushed steel with the cyan accent is the palette and material. The user's feedback on the round 1 boards: **too rectangular and not ornate enough**, so the menus need more ornate, non-rectangular shapes. Round 2 research (FreeSpace 2, Elite Dangerous, Homeworld 2 shape and effect vocabulary) and three new boards in direction 1's palette were made for that:
  - `Docs/Images/menu-style-samples/round2-A.png` (chamfered frames, corner brackets, etched lines; FreeSpace-style)
  - `Docs/Images/menu-style-samples/round2-B.png` (stepped, segmented panels, index cells, tech line work; Homeworld-2-style)
  - `Docs/Images/menu-style-samples/round2-C.png` (asymmetric hull, inset recesses, vents, rivets, circuit traces; hybrid)
  - Research: "Round 2: shapes and effects" in `Docs/Research/SOLID_MENU_STYLE_REFERENCES.md`.
  - The round 2 boards use dim text #A4AFB8 (5.15:1 on the panel) instead of round 1's #8E9AA4 (4.0:1). Adopting it is part of the shape pick below.

## Still open (do not decide without the user)

### #46 solid menus

- **Shape language pick** from round 2 (A, B, C, or a mix, e.g. A's chamfers and brackets with C's recesses and LEDs), or another round of samples.
- **How much ornament and motion:** which details to keep (rivets, vents, hazard stripes, circuit traces, etched lines, ruler ticks) and which effects (rollover glow / sweep / slide-out / brackets, panel-open draw-in, idle scan line or trace pulse); whether the effects-strength slider scales them.
- **How non-rectangular shapes are built:** 9-slice brushes (fine for chamfers, steps and brackets) vs bespoke panel art or an SDF / UMG material (needed for side notches and the asymmetric hull of C).
- **Hover glow colour:** the #22 hologram hover glow is cyan; the samples tint hover with each direction's accent (cyan, amber, blue). Keep the hologram cyan everywhere, or use the direction accent on solid menus?
- **Pause menu behaviour details:** whether it keeps today's pause behaviour unchanged; is the flight scene visible (dimmed / blurred) behind the console panel; which entries it has.
- **Fonts:** which typeface(s) for titles and body (the samples use Bahnschrift as a stand-in; Eurostile / Orbitron / DIN Condensed / Rajdhani-like options), licensing, minimum body size.
- **Per-screen layouts:** main menu, settings tabs, demo page, Controls / Info / About, pause.
- **Reuse of the existing UMG widgets** (restyle in place) vs new shared console widgets (button, toggle, slider, panel, title bar), and how the brushed-metal material is delivered (UMG material with a tiling texture vs 9-slice textures).
- Texture resolution, tiling and per-panel variation; whether the effects-strength slider applies to solid menus.

### #42 hologram in-flight menus

- The rest of the original grill: which shared hologram widgets to build from the #21/#22 renderer, how the effects-strength slider applies, and the exact list of in-flight panels to convert.
