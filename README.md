<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SOLTest

A solar-system space simulation by **Acid Rain Studios LLC**, built with **Unreal Engine 5.8** in C++ (Windows). You fly a small ship in third person across a **1:1-scale solar system** with real orbits, jump between regions from a hologram map, level to planet surfaces, and shoot targets. Copyright © 2026 Acid Rain Studios LLC.

## Contents

- [Why it exists](#why-it-exists)
- [Features and status](#features-and-status)
- [How to play](#how-to-play)
- [Getting the project and building it](#getting-the-project-and-building-it)
- [Dependencies](#dependencies)
- [Generated content and Tools](#generated-content-and-tools)
- [Tests and smoke runs](#tests-and-smoke-runs)
- [Gallery](#gallery)
- [Development effort](#development-effort)
- [Documentation map](#documentation-map)
- [Licence](#licence)

## Why it exists

The goal is a game at true astronomical scale built for massive entity counts: Mass Entity for everything (about 1M entities is the long-term goal), using current Unreal features (Mass, Niagara, Nanite, PCG, Chaos, Enhanced Input) and no deprecated APIs. It is also a testbed for the cross-cutting problems of scale: double-precision universe coordinates, origin rebasing, Keplerian orbits and a flight model that works from metres per second to a fraction of light speed. The reasoning is in [SDD 1](Docs/SDDs/1-solar-system-architecture.md).

## Features and status

Built (see [Docs/Plans/STATUS.md](Docs/Plans/STATUS.md) for tickets and the roadmap):

- 1:1-scale solar system with Keplerian orbits, planet axial rotation, moons, dwarf planets, and origin rebasing for double-precision positions.
- Newtonian, thrust-limited flight with a G-force pilot model (boost to 9 G, blackout), and three flight modes: assisted, manual and autopilot (AUTO).
- Jump map and hologram map with Jump, Move and Patrol (loop / ping-pong) routes, plus a route overlay in the flight view.
- Orbit lines, a star field baked from the AT-HYG catalogue, gas giants with band shells and storms, rings with planet shadows, and an asteroid belt of about 9,000 bodies.
- Overlay icons and a Ctrl+O Overlay Menu with up to five overlay profiles (vertical tabs, per-profile master O and Alt+O orbit lines), cycled with RShift+O.
- Surface-lock, pulse guns, droppable targets, destruction and debris.
- A scale demo with up to 1M NPC ships (Mass), with a Full / reduced-detail budget.
- A hologram HUD on a batched glow-shape renderer with a 3D radar, menus, settings, rebindable keys, and in-game Controls / Info / About screens.
- Hologram in-flight menus (radial, Ctrl+O Overlay Menu, F3 speed panel, map list menu, info / target / speed / date cards) with draw-in and close animations, hover glow, sound cues and their own effects-strength slider.

Placeholder or not built yet:

- **Art and audio** are placeholder (engine meshes, generated materials, a few combat sounds, no music): ticket #9.
- **Procedural terrain and atmospheres** are not built: ticket #10 (surface-lock only holds the ship near a body).
- **Overlay icons, the Overlay Menu and radial menu** are built (SDD 22) and now hologram-styled (SDD 42); the **Chase Down autopilot mode** and player documentation are queued.

## How to play

Defaults below; every ship key can be rebound in Settings. The complete list with exact numbers is [Docs/GAME_MECHANICS.md](Docs/GAME_MECHANICS.md), and the game shows live bindings on its Controls page.

| Group | Input | Action |
|---|---|---|
| Flight | Space (hold) / wheel click | Steer with the mouse (hold, or toggle) |
| | W S A D, Left Shift / Left Ctrl | Thrust forward / back, strafe, up / down |
| | Q / E, Z (hold), B | Roll, stop spin, recentre the stick |
| | Tab | Boost on / off (x3 thrust, 9 G cap) |
| | Mouse wheel | Step the speed cap (1 m/s to 0.5c); F3 opens a speed panel |
| | Left Alt (hold) | Free-look camera |
| | Left mouse (hold) | Fire pulse guns |
| | L | Surface-lock on / off |
| Flight modes | C | Assisted <-> manual; takes control back from AUTO |
| | V | Resume a paused route |
| Map | Shift+M / Shift+J | Open the map (last mode) / in Jump mode |
| | J / M / P (in map) | Jump / Move / Patrol mode |
| | Left click, Shift (height), Enter | Pick a destination, set height, start the jump or route |
| | Right drag, middle drag, wheel | Orbit, pan, zoom |
| | Ctrl+right click, Delete, X | Undo a waypoint, clear route, clear pick |
| Overlays | O / Alt+O / RShift+O / P | Overlays on / off, orbit lines, cycle overlay profiles, route overlay; `-` `=` Home change radar zoom |
| Targets | T / R / F / X | Select nearest the reticle / next farther / next nearer / clear |
| | G | Drop a target 200 m ahead |
| | M | Lock the reference frame to the target |
| Time warp | `[` / `]` / Backspace | Warp down / up / reset to 1x |
| Menu | Esc (or Ctrl+F1 in PIE) | Pause menu |

## Getting the project and building it

```
git clone https://github.com/DieSlower/SOLTest.git
cd SOLTest
git lfs pull
```

Then, with the Unreal Editor closed (a running editor locks the module DLL):

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" SOLTestEditor Win64 Development -Project="<repo>\SOLTest.uproject" -WaitMutex
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "<repo>\SOLTest.uproject"
```

Or open `SOLTest.sln` / `SOLTest.slnx` in Rider or Visual Studio and run the editor target. Play with PIE or `-game`. There is no packaged build or published release yet. The game starts in the main menu unless another `-SOL*` switch is given (`-SOLNoMenu` skips it); the debug and verification flags are tabled in [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md).

## Dependencies

- **Unreal Engine 5.8** (`EngineAssociation` 5.8), installed at `C:\Program Files\Epic Games\UE_5.8` (the scripts and docs assume that path).
- **Windows 11** with **Visual Studio** (or Rider) and the C++ game-development workload: the MSVC toolchain and a Windows SDK, as the engine's setup requires. The exact component versions are not pinned in this repo.
- **Git LFS**: `.gitattributes` stores `.uasset`, `.umap`, textures, meshes and audio in LFS.
- **Plugins** enabled in `SOLTest.uproject`: ModelingToolsEditorMode (editor only), ModelContextProtocol, AllToolsets; Fab is disabled. The game module uses MassEntity, MassCore, Niagara, EnhancedInput and UMG, all engine modules.
- **CelestialVault** (engine plugin) is only needed to re-import the star field; the needed assets are already copied into `Content/SOL/StarField/CelestialVault`.
- **Python 3** (with numpy) for the star-field bake; the Unreal Python plugin for the editor scripts.
- **External data**: the AT-HYG v4.0 and HYG v4.4 star catalogues (CC BY-SA 4.0, David Nash / astronexus), downloaded by a script and **not committed** (`Tools/StarField/data/` is git-ignored). Its baked results are committed as assets. Attribution text is in `fetch_athyg.py`.
- **Hardware**: not documented or measured on a spec list. It is a modern-GPU project (Nanite, Niagara GPU particles, TSR); the 1M-NPC demo and the ring dense layer are GPU-heavy, so use a current dedicated GPU. Performance numbers are in the SDDs.

## Generated content and Tools

Committed assets in `Content/` mean a fresh clone already runs. The scripts under `Tools/` exist to recreate or rebuild generated content (materials, parameter collections, star data); each is idempotent and its header documents how to run it, usually headless: `UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript -script="<abs path>" -unattended` with the editor closed. `SOL_REBUILD` / `-SOLRebuild` rebuilds in place. Order matters only for the star field; the rest are independent.

| Script | Creates |
|---|---|
| `Tools/CreateSOLContent.py` | Base body material and first assets (run first) |
| `Tools/CreateSOLAudioClasses.py` | Master, SFX and music sound classes |
| `Tools/StarField/fetch_athyg.py` | Downloads and verifies the AT-HYG and HYG catalogues (step 1) |
| `Tools/StarField/bake_star_field.py` | Bakes bright-star list and faint-star cubemap (step 2) |
| `Tools/StarField/import_star_field.py` | Imports the bake and copies CelestialVault assets (step 3, editor closed) |
| `Tools/StarField/create_star_field_materials.py` | Star sprite and sky materials (step 4) |
| `Tools/GasGiants/create_gas_giant_materials.py` | Gas giant cloud-deck materials |
| `Tools/Rings/create_ring_shadow_assets.py` | Ring rock material and shadow parameter collection |
| `Tools/OrbitLines/create_orbit_line_assets.py` | Orbit line material and parameter collection |
| `Tools/Map/create_hologram_assets.py` | Map hologram materials |
| `Tools/Npc/create_npc_assets.py` | NPC point and hull renderer assets |
| `Tools/UI/create_hud_shape_material.py` | HUD glow-shape material |
| `Tools/WritePlayerGuide.bat` | `Docs/PLAYER_GUIDE.md` and its Help screenshots (a 1920x1080 game run, editor closed) |

Asset paths are centralised in `Source/SOLTest/SOLConstants.h`.

## Tests and smoke runs

Tests use the UE Automation Framework (`Source/SOLTest/Tests/`, names `SOLTest.<Topic>.<Case>`):

```
Tools\RunTests.bat [-Filter SOLTest.<Topic>] [-Build] [-List] [-ShowLog] [-Rhi]
```

It runs `UnrealEditor-Cmd` headless, prints a summary, and exits 0 (pass), 1 (failures) or 2 (could not run); results are in `Saved/AutomationReports/index.json`. It refuses to run while the editor has the project open. **Known problem:** the headless path currently stalls on the author's machine, so tests are being run from the open editor instead (details in [CLAUDE.md](CLAUDE.md), "Known tech debt").

Scripted smoke runs launch the game with `-SOLSmoke*` flags (flight, input, HUD, overlay and profiles, map, map pick, route, jump, menu, level, space dust) and take screenshots; the flags are in [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md).

## Gallery

![Hologram radial menu](Docs/Images/42-radial-hologram.png)

The hologram radial menu (issue #42) on a moon's icon: the rim arcs draw in, and the hovered Info sector glows and flickers briefly.

![Hologram Overlay Menu](Docs/Images/42-overlay-menu-hologram.png)

The Ctrl+O Overlay Menu as a hologram: a stepped broken-line glow frame over a faint dark tint, with brackets and a pointer on the highlighted row.

![Hologram cards](Docs/Images/42-hologram-cards.png)

The four frameless hologram cards: date and anchor (top left), target and info (top right), speed (bottom left), each with glowing corner ticks and a faint tint.

![Chase Down in the radial menu](Docs/Images/chase-down-radial-menu.png)

Chase Down in the radial menu: the autopilot runs down a dropped target with G bursts.

![Chase Down burst](Docs/Images/chase-down-hud-burst.png)

The CHASE marker during a burst: range, closing speed, time to intercept and the G load.

![Chase Down cool-off](Docs/Images/chase-down-hud-cool.png)

The cool-off between bursts: the marker shows the G floor and the tolerance at which the next burst resumes.

![Space dust at slow speed](Docs/Images/43-dust-slow.png)

Slow drift: sparse soft dust points give a sense of motion.

![Space dust streaks in fast flight](Docs/Images/43-dust-fast.png)

Fast flight: the dust stretches into streaks along the direction of motion.

![Space Dust row in the Overlay Menu](Docs/Images/43-dust-menu.jpg)

Ctrl+O Overlay Menu as of issue #43: the Space Dust row, checked, with the dust behind it (since issue #34 the dust is a Settings option, shown below).

![Overlay Menu profile tabs](Docs/Images/34-overlay-profile-tabs.png)

Ctrl+O Overlay Menu with the five profile tabs: Profile 3 is active, Profiles 2, 4 and 5 are off, and the orbit kinds are indented under Orbit Lines.

![Overlay profile tag](Docs/Images/34-overlay-profile-tag.png)

Right Shift+O cycles to the next profile and the HUD shows an OVERLAYS: PROFILE N tag.

![Space dust option in Settings](Docs/Images/34-settings-space-dust.png)

Settings: the Space dust option under Graphics.

![Solid main menu](Docs/Images/46-main-menu-solid.png)

The solid main menu (issue #46): the hull column with Play focused (amber, its trace lit), Demo, the start-location button, Controls, Settings, About and Quit, the hazard stripe, the key hints, the circuit traces and the build-time version recess.

![Solid Settings with the tab column](Docs/Images/46-settings-solid.png)

The solid Settings page (issue #46, part 46e and its follow-ups), on its Gameplay tab: the six sections as a vertical tab column beside the content recess, the solid sliders with their cyan fill up to the value, the toggles and the shared Revert / Apply footer.

![Solid Controls page with the tab column](Docs/Images/46-controls-solid.png)

The solid Controls page (issue #46, part 46f): each area of the controls is a tab in the vertical tab column, with the key and action rows in the content recess; Info and About use the same shell.

![Solid Scale demo page](Docs/Images/46-demo-solid.png)

The solid Scale demo page (issue #46, part 46f): the presets row and the option groups (Population, Battles and the rest) as recesses in one scroll, with the warnings line, Reset and Start demo.

![Solid pause menu with the Controls page open](Docs/Images/46-pause-solid.png)

The solid pause menu (issue #46, parts 46g-46i): the side panel slid in from the left over the dimmed, still-running flight (Resume, Controls, Info, Settings, Demo, About, Quit, the note that the simulation keeps running and the key hints), with the Controls page open in the shell beside it.

![Help page: keyboard and mouse diagram with a hovered key](Docs/Images/33-help-keyboard-hover.png)

The Help page (issue #33): the live keyboard and mouse diagram, each key tinted by area with a "+N" badge for further actions, and the popup beside a hovered key listing what it does (here O and its chords). The keys follow the player's own bindings.

![Help page: Flight HUD glossary](Docs/Images/33-help-glossary-flight-hud.png)

The Help page's HUD glossary (issue #33): each HUD symbol drawn with the HUD's own shape code beside its name and a description that names the live keys. `Docs/PLAYER_GUIDE.md` is generated from the same tables.

![Controls page with key caps](Docs/Images/33-controls-key-caps.png)

The Controls page with drawn key caps (issue #33, part 33b): each binding shown as caps, with chords and alternatives, instead of plain text.

![Radar zoomed in to 1 km with no target](Docs/Images/36-radar-manual-1km-no-target.png)

The radar zoomed in to `RADAR 1.0 km (MANUAL)` with no target selected (issue #36): `-` and `=` step through powers of ten from 1 AU down to 1 km whatever the target state, here with a 3,000-ship NPC population whose nearest battle ships (5-20 km away) sit beyond the range on the radar's rim.

![Help page: the radar scope's zoom text](Docs/Images/33-help-glossary-targets-radar-2.png)

The Help page's Targets and radar tab (issue #36 text): the AUTO range, the 1 km manual floor with or without a target, and MANUAL keeping its range when you change targets.

![HUD glow at 0%, 100% and 200%](Docs/Images/37-hud-glow-compare.png)

The HUD glow slider (issue #37) at 0%, 100% and 200%, over the lit Earth: the speed card's readouts, the target card and reticle, and the radar. 0% leaves crisp lines and text with no glow; 100% is the new default, 30% of the glow before the change; the cards' corner marks keep their own look.

![Flight HUD at the default HUD glow](Docs/Images/37-hud-glow-default.png)

The whole flight HUD at the new default glow (issue #37) with Earth targeted.

![NPC lane lines of the inner solar system](Docs/Images/38-npc-lanes-network.png)

The NPC lane lines (issue #38) seen from 3 AU above the Sun with a million NPC ships out: each lane is a pair of thin dashed hologram lines, teal-white for traffic leaving the inner body and cyan for traffic coming back, the dashes flowing each way. The short Mercury-Venus and Venus-Earth lanes keep a few longer dashes so their flow still shows.

![Lanes converging on Venus](Docs/Images/38-npc-lanes-venus.png)

0.15 AU off Venus: the Mercury-Venus and Venus-Earth lanes meet at the planet. The dashes stay fixed to the lane as the camera moves and regroup into finer or coarser dashes with the zoom.

![Lane ships beside Earth at 200% HUD glow](Docs/Images/38-npc-lanes-crossfade-glow.png)

40,000 km beside Earth at 200% HUD glow: the Earth-Moon lane leaves from behind Earth's disc (the planet hides it), its ships show one by one close to Earth where the lines have faded out, and the lines take over farther along the lane; the lines glow with the HUD glow setting.

## Development effort

<!-- METRICS:BEGIN -->
Measured from the Claude Code session logs by `Tools/Metrics/update_metrics.py`; the price is notional (the project runs on a subscription). Full breakdown by model, agent type and effort: [Docs/PROJECT_METRICS.md](Docs/PROJECT_METRICS.md).

| Metric | So far |
|---|---|
| Work time (wall-clock) | 125.8 h |
| Time spent answering questions | 9.9 h |
| Tokens | 7,187.7M |
| Notional API price | $2,836 |
| Subagents dispatched | 560 |
<!-- METRICS:END -->

## Documentation map

- [CLAUDE.md](CLAUDE.md): workflow rules, build commands and known hands-on checks.
- [Docs/PLAYER_GUIDE.md](Docs/PLAYER_GUIDE.md): player guide: every control and HUD element, with the Help page's screenshots (generated).
- [Docs/TECH_DEBT.md](Docs/TECH_DEBT.md): known tech debt in existing code (imperfections and deferred cleanups).
- [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md): modules, update order, coordinate frames, command-line flags.
- [Docs/GAME_MECHANICS.md](Docs/GAME_MECHANICS.md): every control, mechanic and tunable.
- [Docs/Plans/STATUS.md](Docs/Plans/STATUS.md): ticket status and roadmap; [Docs/SDDs/](Docs/SDDs/README.md): design documents; [Docs/Plans/](Docs/Plans/): implementation plans.
- [Docs/PROJECT_METRICS.md](Docs/PROJECT_METRICS.md): generated effort metrics (time, tokens, notional price, agents).
- [Docs/STYLE_GUIDE.md](Docs/STYLE_GUIDE.md): C++ style. [Docs/Research/](Docs/Research/): research notes, including the performance guidelines. [Docs/ToDo/](Docs/ToDo/README.md): unscheduled ideas.

## Licence

The repository carries a `LICENSE` file (CC0 1.0 Universal text, added in its latest commit). Baked star data derives from CC BY-SA 4.0 catalogues, so check that against the licence before redistributing. The window-open sound (`Docs/Art/ui-sounds/S_SOLHoloOpen.wav`, its source under `Audio Files/` and `Content/SOL/UI/Audio/S_SOLHoloOpen.uasset`) is "FUI Hologram Ping Tone Echoed-2" by Shapeforms Audio from the Sonniss GDC 2023 Game Audio Bundle: it is under the Sonniss licence, not CC0 (free use and redistribution, no attribution required, not to be sold on its own; see SDD 42 section 7.1). The licence choice has not been confirmed by Acid Rain Studios.
