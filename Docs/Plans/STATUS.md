<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Project status: tickets and remaining work

Living document. **Update it whenever a ticket is added, started, finished or re-scoped** (rule in `CLAUDE.md`).
Last updated: 2026-11-13.

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
| #33 | Player documentation: a Help page (main and pause menus) with a live keyboard and mouse diagram tinted by area and a HUD glossary drawn with the HUD's own shape code; key caps on the Controls page; a generated `Docs/PLAYER_GUIDE.md` (`-SOLWritePlayerGuide`) with a stale check; no schema or key-layout change (2026-11-10). **33a done (2026-11-07):** pure logic, tables and tests (key caps, keyboard layout and diagram model, glossary, player guide text, HUD symbol builder), profile baselines for Controls and Settings Key bindings; **33b done (2026-11-07):** the key cap widget (`USOLKeyCapRow`) on the Controls page and the Settings > Key bindings rows, suite 1627, within the profile budget; O14 answered (D32, modifier marker, built in 33c); **33c done (2026-11-08):** the live keyboard diagram widget (`USOLKeyboardDiagram`: stacked at the 658-unit shell width, D34; legends clipped / badges hidden, D35; fixed hover strip, D36; wide screens only, D37; LED-free diagram caps and three legend abbreviations, D38), not yet on a page until 33d-ii, within the profile budget (three-run medians, worst +0.153 ms CPU / +0.107 ms GPU against the main Settings baseline); **33d-i done (2026-11-08):** the flight HUD draws its symbols through the shared `SOLHudSymbols` builders (same calls and order, proven by a same-frame A/B check; HUD cost unchanged), every glossary symbol builds, the shape layer's retain mode; the five map rows are description-only with screenshot crops planned (D39, new part 33e-ii), the bright planet icon only (D40), a 2 px glossary feature floor (D42); **33d-ii done (2026-11-08):** the Help page in both menus (Keyboard tab with the live diagram, four HUD glossary tabs drawn through `USOLHudSymbolView` and one shared `M_SOLHudShape` MID, rail group captions, 8-entry scrolling stacks), the Info HUD paragraph shortened to a pointer to Help; suite 1714 / 0, `-SOLSmokeSolidMenu` 257 / 257, six new profile passes inside the budget (worst +0.101 ms GPU, +0.084 ms CPU against the main Settings baseline), review fixes done; **33d-iii done (2026-11-08):** the user answered both layout questions (SDD 33 D43, D44): the Keyboard tab's hover strip sits above the diagram under the area legend, in view at the tab's top at 1280x720 and 1920x1080; long glossary text samples get a 200-unit-wide box as tall as their lines, drawn at the HUD's own size and wrapped rather than shrunk (smallest scale 1.0), short ones (MANUAL / ASSISTED / AUTO) unchanged. **33e done (2026-11-08):** `Tools\WritePlayerGuide.bat` runs the game with `-SOLWritePlayerGuide` at 1920x1080: it opens Help, captures one image per Help tab plus the Keyboard tab with a key hovered, writes the generated `Docs/PLAYER_GUIDE.md` and puts the images in `Docs/Images/`; `SOLTest.PlayerGuide.UpToDate` fails while the committed guide is stale; **33e-iii done (2026-11-08):** the Keyboard tab's hover detail is a popup over the page beside the hovered key (D45: the reserved strip is gone, the diagram starts under the legend and the hint), and the guide shows each Help tab in scrolled window shots until the whole tab is covered (D46: 15 tab shots plus the hover shot); **33e-ii done (2026-11-08):** the five map glossary rows (orbit lines, route legs, waypoints, map destination pick, map grid) show 176x176 crops of the real game in the Help page and the guide (D47; capture flag `-SOLWriteGlossaryCrops`, textures under `/Game/SOL/UI/Glossary`, cooked by `DirectoriesToAlwaysCook`), the Waypoints and Map grid texts corrected, and the guide writer's end-of-tab measurement fixed (pages 2, 6, 3, 5, 3; 20 Help images plus 5 crops); **33e-iv done (2026-11-09):** two TECH_DEBT fixes: `-SOLSmokeHoloMenu` passes 86 / 86 at 1920x1080 too (the card steps wait on HUD frames, not a wall-clock margin), and `Tools\WritePlayerGuide.bat` keeps unchanged Help images byte for byte (one shared image compare with the crops; the scene behind the menu held still for the shots); **33f done (2026-11-09):** the one-eye XR-4 and 4K profile view runs now cover the Help page of both menus (12 screens, three runs each, two eyes skipped): verdict PASS (menu GPU at most 0.59 ms at 4K and 1.00 ms at one eye under the 1.2 ms ceiling, at most +0.11 ms over Settings); at the eye's near-square aspect the Help layout clips (outside D37, recorded in TECH_DEBT); the user deferred it to the VR menu presentation work, with no VR work before procedural terrain (D48); **33g done (2026-11-09):** the whole-ticket review fixes (player text made true against the code, one shared key cap painter, Shipping guards for the writer switches, stale comments and references) and the keyboard diagram contrast (D49, user: bright key texts over the kept area tint, 3:1 floor, worst idle face Demo 3.22, free keys 3.12), guide regenerated; **33h done (2026-11-10):** final truth check of the in-game text and docs against the bindings and constants (no text error found, guide unchanged), STATUS, SDD and plan closed, README gallery shots of the finished Help page and the Controls key caps. Design: SDD 33 D1-D49, parts 33a-33h | 33 |
| #48 | Window-open sound replaced (2026-11-10): `S_SOLHoloOpen` (`CUES[0]`, same path, volume 0.8) is now Shapeforms' "FUI Hologram Ping Tone Echoed-2" from the Sonniss GDC 2023 bundle, cut to 0.60 s with a fade-out and levelled to the Select cue in SoundCraft, re-imported with the sibling cues' settings; Sonniss licence (no attribution, redistribution allowed, not CC0) recorded in SDD 42 section 7.1 and the README; new test `SOLTest.HoloMenuKit.CueAssets`; no schema change | 42 (section 7.1) |
| #36 | Radar manual zoom floor (2026-11-10): `-` / `=` step the radar range through powers of ten between 1 km and 1 AU with or without a target (from AUTO with no target: 1,000 km, 100 km, 10 km, 1 km); MANUAL keeps its mode and range when a target is selected or cleared; AUTO unchanged (10,000 km, 1 km with a target); no setting, persistence or schema change. Suite 1774 / 0, `-SOLSmokeHud` 30 / 30 at 1280x720 and 1920x1080, Help text and player guide updated | 36 |
| #37 | HUD glow setting (2026-11-11): a Settings "HUD glow" slider (Gameplay, 0-200% in 5% steps, default 100%, save schema 12, a v11 save migrates to 100%) scales the flight HUD's glow (lines, text, radar, route halo, card readouts, Help glossary pictures, the mouse pointer and a live sample under the slider) by g = slider x 0.3, so the default is 30% of the issue #31 glow (the user's pick from screenshots) and 0% leaves a crisp core; menus and the cards' corner marks keep their look. Parts 37a-37c; suite 1800 / 0; `-SOLSmokeHud` 45 / 45 at 1280x720 and 1920x1080; profile +0.001 to +0.003 ms GPU at 1920x1080, flight HUD 0.32 ms at one XR-4 eye | 37, 21 (Amendment 5) |
| #38 | NPC lane lines (2026-11-11): each lane with ships is drawn as two thin dashed hologram lines between its two body centres (outbound teal-white, inbound cyan, keep right), width and brightness by the lane's ship count, the dashes fixed to the lane and flowing each way on a real-time clock that never stops (coarser dashes, down to 4 per lane, on lanes shorter than about 1,536 px on screen; no dimmer "shadow" copy; since a second follow-up the same day one continuous "accordion" scale per lane keeps a dash period about 24 px near the screen centre (spaced by angle, longer toward the edges at a wide field of view) with no size pop, the dashes sliding as the lane's angle on screen changes), faded out within 100-400 lane radii where the lane ships show as points again; HUD glow, planet occlusion, shown on the map; a Lanes row in the Overlay Menu (default on, per profile, save schema 13, key layout 10 unchanged), `sol.NpcLanes`, `-SOLSmokeNpcLanes` (and `-SOLSmokeNpcLanesProfile`). Parts 38a-38c; suite 1843 / 0; `-SOLSmokeNpcLanes` 35 / 35 at 1280x720 and 1920x1080, `-SOLSmokeOverlay` 50 / 50; 1080p budgets held (lane draw 0.027 ms GPU, redraw +0.003 ms), the 1M ramp PASS | 38 |
| #49 | Lane guidance tunnel (2026-11-13): within 100-50 lane radii of an NPC lane its dashed lines hand over to a tunnel of HUD-cyan rings to fly through (12 o'clock to the ecliptic north), every lane near the player, width 1x-3x the lane radius by ship count (empty lanes kept), one continuous "accordion" ring spacing (rings near the player fixed to the lane), end rings one tunnel radius outside each body (three since #50 part 50a, D14, so the lane U-turns clear the planet; never faded below 0.5, exempt from the vanishing-point fade), keep right shown by filled wall chevrons (fading out at 13-30 tunnel radii, like the mock-up) and green / red guide dots (world-sized near), the lane's ships shown as points again inside it; one wall label per shown tunnel (lane, distance to each end ring, ships; a compact one-line form where space is short; clear of the world-sized dots and the HUD panels); Lanes row, HUD glow, planet occlusion, hidden on the map; `sol.NpcTunnel`, `-SOLSmokeNpcTunnel` (and `-SOLSmokeNpcTunnelProfile`); no schema change (save 13, key layout 10). Parts 49a-49c; suite 1903 / 0; `-SOLSmokeNpcTunnel` 39 / 39 and `-SOLSmokeNpcLanes` 35 / 35 at 1280x720 and 1920x1080; 1080p: tunnel draw 0.034-0.039 ms GPU, CPU on vs off 0.016-0.022 ms at the 0.02 ms budget (judged against the tunnels-off baseline, D20); the 1M ramp PASS | 49 |

## To do (ordered)

| Order | Issue | What | Size | Depends on |
|---|---|---|---|---|
| 1c | #42 | **Done.** Hologram in-flight menus: radial menu, Ctrl+O Overlay Menu with profile tabs, F3 speed panel, target/info cards (the HUD itself unchanged; full screens are #46). Glow-line outline frames on the HUD renderer, faint backdrop tint, open/close draw-in, hover glow and flicker, idle sweep and jitter, open/hover/select cues, a separate hologram-menu effects slider. SDD 42 and plan done, every question answered (2026-11-03). Parts built: (hologram menu maths, profiling baseline), 42b (settings schema 11, `HoloMenuEffects`, Settings > Gameplay slider, the hologram cursor follows it), 42c (frame kit, menu layer, menu subsystem), 42d (radial), 42e (Overlay Menu), 42f (speed panel), 42g (the four frameless cards and the map list menu) and 42h (cue assets) done; 42i (final profile within budget, docs and in-game text audit, README screenshots, final review) done. Q13 (no hover glow on a disabled radial sector) and Q16 (text glow kept at strength 0) answered 2026-11-04; Q14 and Q15 likewise | M-L | #22 |
| 2 | #33 | **Done.** Player documentation: Help page, keyboard diagram, HUD glossary, key caps, generated `Docs/PLAYER_GUIDE.md` (see Done) | M | #22, #46 |
| 3b2 | #37 | **Done** (2026-11-11). Settings "HUD glow" slider (0-200%, save schema 12) with a lower default glow (see Done) | S-M | none |
| 3b3 | #38 | **Done** (2026-11-11). NPC lanes drawn as two flowing dashed hologram lines per lane with a Lanes row in the Overlay Menu (save schema 13; see Done) | M | #22 |
| 3b4 | #49 | **Done** (2026-11-13). Lane guidance tunnel near NPC lanes: cyan ring tunnels with keep-right chevrons and guide dots, wall labels, no schema change (see Done) | M | #38 |
| 3b5 | #50 | **In progress** (grill-me done 2026-11-13, D1-D12; SDD 50 and plan drafted; Q1-Q8 answered and folded in as D13-D20, one open question Q9 for part 50d: how AUTO is told to fly around lanes). Lane traffic flow: keep-right closed-form rails (outbound right, inbound left, U-turn outside each end ring), flip-and-burn along the lane at 1.65 G with 50-100 % effort capped at 300 km/s, fastest on the axis and 20 km/s at the wall, 25 % halo traffic outside the wall, a HUD merge readout (traffic speed here, TOO FAST / TOO SLOW / MATCHED), AUTO merging and holding the traffic speed in lanes; parts 50a-50e; no schema change planned | M-L | #49 |
| 3b5b | #51 | Placeholder main thruster effect for the player ship and all NPC ships (consumes #50's per-ship thrust state); needs grill-me | S-M | #50 |
| 3b6 | #39 | NPC ships vanish at distance (realistic visibility, no pixel or brightness floor) | M | none |
| 3b7 | #40 | NPC ships selectable with T / R / F, only inside the radar range (also enables the ship row of Chase Down, greyed since #32) | M | #22 |
| 3c | #35 | Bolts follow normal perspective (today a constant ~3 px on screen past 100 m) | S-M | none |
| 3d | #44 | Space dust look: subtler, debris-like particles (user feedback on #43: too coarse, uniform lines or spheres); needs grill-me | S-M | #43 |
| 3e | #45 | Key rebinding conflict prevention: Settings warns and refuses a binding that conflicts with another bound key (also covers the RShift+O cycle key case in TECH_DEBT); needs grill-me | S-M | none |
| 3f | #47 | Review all ship movement control bindings (move Thrust down off Left Ctrl, which is also the Ctrl+F1 / Ctrl+O / Ctrl+RMB / Ctrl+F2 modifier): decide a coherent default layout for thrust, strafe, roll, pitch/yaw, throttle, boost, free look, recenter and steer toggle; needs grill-me first, follows #33; bumping `KEY_BINDING_LAYOUT_VERSION` (10) with migration needs explicit user approval; then update Controls/Info text, GAME_MECHANICS.md, Help keyboard diagram, glossary and PLAYER_GUIDE.md | M | #33 |
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

About 12 tickets (#50, #51, #39, #40, #35, #44, #45, #47, #23, #27, #26, #24 decisions) before the two large roadmap items (#10, #9),
plus whatever #24 adopts. The ToDo list above is the backlog beyond that.
