<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->
# #46 solid console-style menus: grill-me notes

Status: **grill done (2026-11-04); SDD and plan drafted:** [`Docs/SDDs/46-solid-menus.md`](../SDDs/46-solid-menus.md) and [`46-solid-menus-plan.md`](46-solid-menus-plan.md). The decisions below are recorded there as D1-D16; what they leave open is listed as Q1-Q17 in SDD section 15 and waits for the user. Earlier context and the sample boards are in `Plans/42-hologram-menus-grill-notes.md` and `Research/SOLID_MENU_STYLE_REFERENCES.md`.

## Decided (2026-11-03)

- **Scope:** main menu, settings, demo, Controls / Info / About and pause. The hologram stays only for the HUD, radial menu, Overlay Menu, speed panel and target / info cards (that is #42).
- **Look:** direction 1 palette (dark gunmetal brushed steel, cyan accent, dim text #A4AFB8) with the round 2 "C" shape language: asymmetric hull, inset recesses, vents, rivets and glowing circuit traces. Reference: `Docs/Images/menu-style-samples/round2-C.png`.
- **Textures and materials** are generated now with the MCP texture / material tools, not deferred to #9.
- **Font:** Rajdhani or a similar free OFL techno sans, bundled in the project. The HUD font is unchanged.
- **Pause:** a slide-in side panel over a dimmed, frozen flight view.
- **Animations:** panel-open, circuit-trace power-up and a slow idle pulse. They are scaled by the existing effects-intensity setting. No new saved settings.
- **Input unchanged:** mouse + keyboard + the #22 hologram cursor and hover glow. A keyboard / gamepad navigation redesign is a future idea, not part of #46.

## Decided (2026-11-04)

- **Rebuild:** restyle in place; each screen keeps its logic and widgets; a shared solid-panel kit (frame, buttons, tabs, sliders, toggles).
- **Hull shapes:** SDF in a UMG material, not 9-slice.
- **Asset pipeline:** PNG sources exported from the craft MCP tools into `Docs/Art/`, imported into `Content/` by an editor Python script under `Tools/`, asset paths fixed in `SOLConstants.h`. VectorCraft takes absolute paths; PhotoCraft needs `--automation-write-root`.
- **Colours:** hover / selected glow amber #FFA628; hull traces cyan #28D2F0; text #A4AFB8.
- **Hover:** lift 3 px, accent edge, lit LED, underline, a trace that extends into the panel (sample `hover-compare.png` panel B, deleted once recorded).
- **Layouts:** main menu = left button stack on a hull panel (title on top, version / credits in a small bottom-left recess); Settings = large hull panel with a vertical tab column on the left, content right; Controls / Info / About reuse the Settings shell with read-only content; pause = slide-in side panel on the left over a dimmed, frozen flight view (layout samples `layout-1` to `layout-4`, deleted once recorded).
- **No schema change:** no new saved settings; input unchanged.

## Still open

See SDD 46 section 15 (Q1-Q17): which effects setting scales the animations, strength 0 / 2, "frozen" pause vs the running simulation, pause and main-menu page placement, Controls / Info / About and Demo in the shell, Settings tab details, the selected look, the version line, sounds, text / cyan / warning colours, where the hover trace extends, the starting values and the performance budget.
