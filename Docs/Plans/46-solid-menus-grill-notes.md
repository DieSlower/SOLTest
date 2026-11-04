<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->
# #46 solid console-style menus: grill-me notes

Status: grill done except per-screen layouts and the build details below. Next step: draft the SDD and plan (`Docs/SDDs/46-...`, `Docs/Plans/46-...-plan.md`). Earlier context and the sample boards are in `Plans/42-hologram-menus-grill-notes.md` and `Research/SOLID_MENU_STYLE_REFERENCES.md`.

## Decided (2026-11-03)

- **Scope:** main menu, settings, demo, Controls / Info / About and pause. The hologram stays only for the HUD, radial menu, Overlay Menu, speed panel and target / info cards (that is #42).
- **Look:** direction 1 palette (dark gunmetal brushed steel, cyan accent, dim text #A4AFB8) with the round 2 "C" shape language: asymmetric hull, inset recesses, vents, rivets and glowing circuit traces. Reference: `Docs/Images/menu-style-samples/round2-C.png`.
- **Textures and materials** are generated now with the MCP texture / material tools, not deferred to #9.
- **Font:** Rajdhani or a similar free OFL techno sans, bundled in the project. The HUD font is unchanged.
- **Pause:** a slide-in side panel over a dimmed, frozen flight view.
- **Animations:** panel-open, circuit-trace power-up and a slow idle pulse. They are scaled by the existing effects-intensity setting. No new saved settings.
- **Input unchanged:** mouse + keyboard + the #22 hologram cursor and hover glow. A keyboard / gamepad navigation redesign is a future idea, not part of #46.

## Still open

- Per-screen layouts (main menu, settings tabs, demo, Controls / Info / About, pause entries).
- Reuse (restyle in place) vs rewrite of the existing UMG widgets, and how non-rectangular shapes are built (9-slice vs SDF / UMG material).
- Exact asset list: tiles, bevels, vents, rivets, trace glow, button states; resolution and tiling.
- Asset creation pipeline: the MCP tools had no file read / write roots. Find out how exported textures reach `Content/`, or whether a `Tools/*.py` pipeline is needed.
- Hover glow colour on solid menus (cyan hologram vs direction accent): confirm if not already implied.
- SDD and plan drafting.
