<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Project status: tickets and remaining work

Living document. **Update it whenever a ticket is added, started, finished or re-scoped** (rule in `CLAUDE.md`).
Last updated: 2026-11-06.

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
| #46 | Solid console-style menus (2026-11-06): main menu, Settings (tab column), Demo, Controls / Info / About (tab pages) and the pause side panel as a solid brushed-metal console (SDF hull material, shared kit `Menu/SOLSolid*`, amber hover / cyan traces, Rajdhani font, build-time version line), scaled by `HudEffectsStrength`, the #42 cues, scrolling rails, narrow 1000x800 layouts; no schema change; parts 46a-46j, full suite 1537, `-SOLSmokeSolidMenu` 193/193; the whole-frame VR proxy is missed (scene-bound, project-level VR work) | 46 |

## In progress

| Issue | What | Notes |
|---|---|---|
| #33 | Player documentation: a Help page (main and pause menus) with a live keyboard and mouse diagram tinted by area and a HUD glossary drawn with the HUD's own shape code; key caps on the Controls page; a generated `Docs/PLAYER_GUIDE.md` (`-SOLWritePlayerGuide`) with a stale check; no schema or key-layout change | **33a done (2026-11-07):** pure logic, tables and tests (key caps, keyboard layout and diagram model, glossary, player guide text, HUD symbol builder), profile baselines for Controls and Settings Key bindings; **33b done (2026-11-07):** the key cap widget (`USOLKeyCapRow`) on the Controls page and the Settings > Key bindings rows, suite 1627, within the profile budget; O14 answered (D32, modifier marker, built in 33c); **33c done (2026-11-08):** the live keyboard diagram widget (`USOLKeyboardDiagram`: stacked at the 658-unit shell width, D34; legends clipped / badges hidden, D35; fixed hover strip, D36; wide screens only, D37; LED-free diagram caps and three legend abbreviations, D38), not yet on a page until 33d-ii, within the profile budget (three-run medians, worst +0.153 ms CPU / +0.107 ms GPU against the main Settings baseline); **33d-i done (2026-11-08):** the flight HUD draws its symbols through the shared `SOLHudSymbols` builders (same calls and order, proven by a same-frame A/B check; HUD cost unchanged), every glossary symbol builds, the shape layer's retain mode; the five map rows are description-only with screenshot crops planned (D39, new part 33e-ii), the bright planet icon only (D40), a 2 px glossary feature floor (D42); **33d-ii done (2026-11-08):** the Help page in both menus (Keyboard tab with the live diagram, four HUD glossary tabs drawn through `USOLHudSymbolView` and one shared `M_SOLHudShape` MID, rail group captions, 8-entry scrolling stacks), the Info HUD paragraph shortened to a pointer to Help; suite 1714 / 0, `-SOLSmokeSolidMenu` 257 / 257, six new profile passes inside the budget (worst +0.101 ms GPU, +0.084 ms CPU against the main Settings baseline), review fixes done; **33d-iii done (2026-11-08):** the user answered both layout questions (SDD 33 D43, D44): the Keyboard tab's hover strip sits above the diagram under the area legend, in view at the tab's top at 1280x720 and 1920x1080; long glossary text samples get a 200-unit-wide box as tall as their lines, drawn at the HUD's own size and wrapped rather than shrunk (smallest scale 1.0), short ones (MANUAL / ASSISTED / AUTO) unchanged; **33e next**. Design: SDD 33 D1-D44, parts 33a-33h and 33e-ii |

## To do (ordered)

| Order | Issue | What | Size | Depends on |
|---|---|---|---|---|
| 1c | #42 | **Done.** Hologram in-flight menus: radial menu, Ctrl+O Overlay Menu with profile tabs, F3 speed panel, target/info cards (the HUD itself unchanged; full screens are #46). Glow-line outline frames on the HUD renderer, faint backdrop tint, open/close draw-in, hover glow and flicker, idle sweep and jitter, open/hover/select cues, a separate hologram-menu effects slider. SDD 42 and plan done, every question answered (2026-11-03). Parts built: (hologram menu maths, profiling baseline), 42b (settings schema 11, `HoloMenuEffects`, Settings > Gameplay slider, the hologram cursor follows it), 42c (frame kit, menu layer, menu subsystem), 42d (radial), 42e (Overlay Menu), 42f (speed panel), 42g (the four frameless cards and the map list menu) and 42h (cue assets) done; 42i (final profile within budget, docs and in-game text audit, README screenshots, final review) done. Q13 (no hover glow on a disabled radial sector) and Q16 (text glow kept at strength 0) answered 2026-11-04; Q14 and Q15 likewise | M-L | #22 |
| 2 | #33 | **In progress** (see above, 33d-iii done, 33e next): player documentation, controls with keyboard pictures and a GUI glossary | M | #22, #46 |
| 3b | #36 | Radar manual zoom floor of 1 km or 10 km without a target (today 10,000 km unless a target is selected) | S | none |
| 3b2 | #37 | Lower the HUD hologram glow again and add a Settings glow slider (0-200%) | S-M | none |
| 3b3 | #38 | NPC lanes drawn as one animated hologram line per lane, with an overlay-menu toggle | M | #22 |
| 3b4 | #39 | NPC ships vanish at distance (realistic visibility, no pixel or brightness floor) | M | none |
| 3b5 | #40 | NPC ships selectable with T / R / F, only inside the radar range (also enables the ship row of Chase Down, greyed since #32) | M | #22 |
| 3c | #35 | Bolts follow normal perspective (today a constant ~3 px on screen past 100 m) | S-M | none |
| 3d | #44 | Space dust look: subtler, debris-like particles (user feedback on #43: too coarse, uniform lines or spheres); needs grill-me | S-M | #43 |
| 3e | #45 | Key rebinding conflict prevention: Settings warns and refuses a binding that conflicts with another bound key (also covers the RShift+O cycle key case in TECH_DEBT); needs grill-me | S-M | none |
| 3f | #47 | Review all ship movement control bindings (move Thrust down off Left Ctrl, which is also the Ctrl+F1 / Ctrl+O / Ctrl+RMB / Ctrl+F2 modifier): decide a coherent default layout for thrust, strafe, roll, pitch/yaw, throttle, boost, free look, recenter and steer toggle; needs grill-me first, follows #33; bumping `KEY_BINDING_LAYOUT_VERSION` (10) with migration needs explicit user approval; then update Controls/Info text, GAME_MECHANICS.md, Help keyboard diagram, glossary and PLAYER_GUIDE.md | M | #33 |
| 3g | #48 | Replace the window-open sound cue (`S_SOLHoloOpen`, `CUES[0]`) with the user-chosen Sonniss Shapeforms 'FUI Hologram Ping Tone Echoed-2' (trimmed to ~0.6 s with fade, normalised like the other cues); needs the editor open, so run when no tests are running; do after #33; no schema change; licence line goes in SDD 42 section 7.1 | S | #33 |
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
- `map-overlays-shape-renderer.md`: the map's route, waypoint, pick and grid overlays on the HUD glow-shape renderer (issue #33, D39)
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
