<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Project status: tickets and remaining work

Living document. **Update it whenever a ticket is added, started, finished or re-scoped** (rule in `CLAUDE.md`).
Last updated: 2026-11-04.

Sizes are rough: S = a day of agent work or less, M = a few sessions, L = a large multi-session feature.

## Done

| Issue | What | SDD |
|---|---|---|
| #2 | Part 1: foundations and flight scaffold | 1, 2 |
| #3 | Part 2: jump map | 3 |
| #4 | Part 3: surface-lock | 4 |
| #5 | Part 4: star field (AT-HYG) | 5 |
| #6 | Part 5: moons, dwarf planets, asteroid belt, rings | 6 |
| #7 | Part 6: weapons, target drops, destruction | 7 |
| #8 | Part 7: HUD, menus, controls, info, settings | 8 |
| #11 | Part 10: scale and performance demo (1M NPC ships) | 11 |
| #12 | Planet axial rotation | 12 |
| #13 | Star field quality | 13 |
| #14 | Gas giant fidelity | 14 |
| #15 | Dropped target blur | 7 (amendment) |
| #16 | Ring shadows | 16 |
| #17 | Orbit lines | 17 |
| #18 | Newtonian flight core, G-force pilot model, autopilot, NPC motion | 18 |
| #19 | Three flight modes and controls | 19 |
| #20 | Hologram map, Move/Patrol routes, route overlay, map hologram look | 20 |
| #28 | HUD phase A: event bus, batched Slate renderer, shape material, effects slider | 21 |
| #29 | HUD phase B: every flight HUD element on the glow renderer, hologram panels, glow text | 21 |
| #30 | HUD phase C: 3D hologram radar (ship-horizontal plane, above/below colours, sonar rings) | 21 |
| #21 | HUD hologram rendering (umbrella, phases A-C) | 21 |
| #25 | Map alignment and shimmer (incl. date-dependent smoke fix) | 25 |
| #31 | HUD polish: half glow, Alt yellow circles, borderless panels, steering start setting and hint, stick recenter on release | 19, 21 (amendments) |
| #41 | Project metrics tracking: generated effort page (time, tokens, notional price, agents by model and effort), README summary, update rule | 41 |
| #22 | Overlay icons, Overlay Menu, O keys, interaction menus, hologram cursor (Part 1: icons, O keys, Overlay Menu, cursor; Part 2: radial and list menus, action table) | 22 |
| #32 | Chase Down autopilot (2026-11-03): G bursts above 1.5 G planned against the pilot G model, meter-floor safety margin setting (save schema 8), radial / list menu entry, Esc cancel, HUD CHASE marker; Jump To ends the chase. The NPC ship row stays greyed until #40 | 32 |
| #43 | Space dust speed cues (2026-11-03): GPU Niagara dust in a 400 m wrap volume around the ship, saturating speed map with a dead zone, points to streaks, hidden under warp / jump / map / pause; Space Dust row in the Overlay Menu, not governed by the master O (save schema 9) | 43 |
| #34 | Overlay profiles (2026-11-04): 5 profiles as vertical tabs in the Ctrl+O menu (enable / disable, first enable copies Profile 1, re-enable restores), per-profile master O and Alt+O orbit lines, RShift+O cycles with a HUD tag, darkening a menu row only, space dust a global Settings > Graphics option (save schema 10, key layout 10) | 34 |

## In progress

| Issue | What | Notes |
|---|---|---|
| - | (none) | - |

## To do (ordered)

| Order | Issue | What | Size | Depends on |
|---|---|---|---|---|
| 1c | #42 | **Done.** Hologram in-flight menus: radial menu, Ctrl+O Overlay Menu with profile tabs, F3 speed panel, target/info cards (the HUD itself unchanged; full screens are #46). Glow-line outline frames on the HUD renderer, faint backdrop tint, open/close draw-in, hover glow and flicker, idle sweep and jitter, open/hover/select cues, a separate hologram-menu effects slider. SDD 42 and plan done, every question answered (2026-11-03). Parts built: (hologram menu maths, profiling baseline), 42b (settings schema 11, `HoloMenuEffects`, Settings > Gameplay slider, the hologram cursor follows it), 42c (frame kit, menu layer, menu subsystem), 42d (radial), 42e (Overlay Menu), 42f (speed panel), 42g (the four frameless cards and the map list menu) and 42h (cue assets) done; 42i (final profile within budget, docs and in-game text audit, README screenshots, final review) done. Q13 (no hover glow on a disabled radial sector) and Q16 (text glow kept at strength 0) answered 2026-11-04; Q14 and Q15 likewise | M-L | #22 |
| 1d | #46 | Solid console-style menus: main menu, settings, demo, Controls / Info / About (full-screen read-only mechanics screens) and pause, as a solid brushed-metal console with textured panels, clearly different from the hologram; input unchanged (mouse + keyboard + #22 hologram cursor and hover glow); textures and materials generated now with the MCP texture/material tools, not waiting for #9. Grill done (2026-11-04): restyle in place with a shared solid-panel kit, SDF hull material, craft-tool PNGs imported by a Tools/ script, amber hover / cyan traces, left-stack main menu, tabbed Settings shell reused by Controls / Info / About, left slide-in pause, no schema change. **SDD 46 and plan drafted** (10 parts, 46a-46j); all 17 open questions (SDD 46 section 15) answered 2026-11-04 (setting `HudEffectsStrength`, version line `0.1.MMYY.<commits>` generated at build time), no gate left. **In progress:** 46a (solid menu maths), 46b (texture sources, Rajdhani, import script), 46c (hull material `M_SOLSolidHull` and its nine instances, the solid kit `Menu/SOLSolid*`, restyled buttons with selected-with-hover and disabled art, the profiling baseline) and 46d (solid main menu: hull column, page shell with Back, start-location recess list, build-time version line `0.1.YYMM.N`, panel animations, focus on Play; review fixed, full suite 1443) and 46e (solid Settings: the six sections as a vertical tab column with hover traces, content recess, solid sliders / toggles / steppers, shared footer, opens on Graphics every time, the embedded Overlay list in the solid look; full suite 1459) done, with its follow-ups (the cyan slider fill, one highlight: the mouse over a slider or Overlay row wins over a button's focus, every tab back at its top on each open, per-tab label columns so the stepper values fit at 1000x800 and no label runs into its control; full suite 1462) and 46f (solid Controls / Info / About as tab pages, Demo as recessed groups; full suite 1476) done; 46g (solid pause: left side panel sliding in over the dimmed live view, pages in the shell beside it, Esc / Back close a page before resuming, the #42 cues on every solid screen, scrolling tab rails and stacks (SDD D17; nothing scrolls at 1280x720); full suite 1506; the CPU profile is over the 0.1 ms budget on most screens, a user decision) done; next 46h (smoke completion and profile) | M | #42 (shared hover/cursor) |
| 2 | #33 | Player documentation: controls with keyboard pictures and a GUI glossary | M | #22 |
| 3b | #36 | Radar manual zoom floor of 1 km or 10 km without a target (today 10,000 km unless a target is selected) | S | none |
| 3b2 | #37 | Lower the HUD hologram glow again and add a Settings glow slider (0-200%) | S-M | none |
| 3b3 | #38 | NPC lanes drawn as one animated hologram line per lane, with an overlay-menu toggle | M | #22 |
| 3b4 | #39 | NPC ships vanish at distance (realistic visibility, no pixel or brightness floor) | M | none |
| 3b5 | #40 | NPC ships selectable with T / R / F, only inside the radar range (also enables the ship row of Chase Down, greyed since #32) | M | #22 |
| 3c | #35 | Bolts follow normal perspective (today a constant ~3 px on screen past 100 m) | S-M | none |
| 3d | #44 | Space dust look: subtler, debris-like particles (user feedback on #43: too coarse, uniform lines or spheres); needs grill-me | S-M | #43 |
| 3e | #45 | Key rebinding conflict prevention: Settings warns and refuses a binding that conflicts with another bound key (also covers the RShift+O cycle key case in TECH_DEBT); needs grill-me | S-M | none |
| 4 | #23 | Orbit line quality (noise where a line enters a planet) | S-M | none |
| 5 | #27 | Ship jitters slightly at rest | M | #18 |
| 6 | #26 | Anti-aliasing evaluation (TSR vs alternatives), report then change | M | none |
| 7 | #24 | Decide each of the 120 Limit Theory catalogue features, split adopted ones into tickets | M (user decisions) | none |
| 8 | #10 | Part 9: procedural terrain and atmospheres | L | #2, #4 |
| 9 | #9 | Part 8: art and audio pass | L | #2-#8 |
| - | #1 | Umbrella: architecture and roadmap, closes when the above is done | - | all |

Order is a proposal; the user can reorder at any time. #24 produces new tickets, so the total will grow.

## Not yet ticketed (`Docs/ToDo/`)

Picked up as tickets when scheduled; each needs grill-me first.

- `sun-glow-and-flare.md`: bigger Sun glow and lens flare
- `adaptive-body-tessellation.md`: finer sphere tessellation when close
- `minor-body-targeting.md`
- `ship-tech-upgrades.md`: G tolerance, thrust and other upgrades
- `damage-and-death.md`: damage system (G-LOC death, planet-impact G)
- `predicted-path.md`: ballistic path under gravity (P key now toggles the route overlay)
- `cockpit-3d-screens.md`: HUD on in-world cockpit screens
- `vr-performance-gate.md`: perf smoke at the Varjo XR-4 resolution, one eye 3840x3744 and two eyes 7680x3744, 90 Hz / 11.1 ms (p99 <= 10 / 9.5 ms), see `Docs/Research/VR_PERFORMANCE_TARGETS.md`
- `supercruise-travel.md`, `planetary-perturbations.md`, `surface-landing.md`, `star-diffraction-spikes.md`,
  `accurate-pole-directions.md`, `asteroid-ring-collision.md`

## Known follow-ups and checks (not tickets)

- Hands-on PIE checks the user needs to do: Left Shift vs Shift+M chord in Auto, shimmer gone in the map, free-cursor
  behaviour, map-close key repeat.
- Failing or unverified smokes: `-SOLMenuSmoke=all` 47/50 (stale cursor expectations after #31 and "Thrust flies the ship" at Mars, see TECH_DEBT), `-SOLSmokeJump` 13/14
  (arrives at Deimos not Mars).
- Tech debt list: `Docs/TECH_DEBT.md` (M2 Mid-to-Far pop, packaged 1M run, render-thread stalls,
  orbit-line memory, and others).

## How much is left

About 4 tickets (#23, #26, #27, #24 decisions) before the two large roadmap items (#10, #9),
plus whatever #24 adopts. The ToDo list above is the backlog beyond that.
