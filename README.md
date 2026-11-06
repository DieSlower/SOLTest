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

The solid main menu (issue #46, part 46d): hull column with the start-location button, hazard stripe and the version recess.

![Solid Settings with the tab column](Docs/Images/46-settings-solid.png)

The solid Settings page (issue #46, part 46e and its follow-ups), on its Gameplay tab: the six sections as a vertical tab column beside the content recess, the solid sliders with their cyan fill up to the value, the toggles and the shared Revert / Apply footer.

## Development effort

<!-- METRICS:BEGIN -->
Measured from the Claude Code session logs by `Tools/Metrics/update_metrics.py`; the price is notional (the project runs on a subscription). Full breakdown by model, agent type and effort: [Docs/PROJECT_METRICS.md](Docs/PROJECT_METRICS.md).

| Metric | So far |
|---|---|
| Work time (wall-clock) | 85.5 h |
| Time spent answering questions | 7.7 h |
| Tokens | 4,687.4M |
| Notional API price | $1,834 |
| Subagents dispatched | 377 |
<!-- METRICS:END -->

## Documentation map

- [CLAUDE.md](CLAUDE.md): workflow rules, build commands and known hands-on checks.
- [Docs/TECH_DEBT.md](Docs/TECH_DEBT.md): known tech debt in existing code (imperfections and deferred cleanups).
- [Docs/ARCHITECTURE.md](Docs/ARCHITECTURE.md): modules, update order, coordinate frames, command-line flags.
- [Docs/GAME_MECHANICS.md](Docs/GAME_MECHANICS.md): every control, mechanic and tunable.
- [Docs/Plans/STATUS.md](Docs/Plans/STATUS.md): ticket status and roadmap; [Docs/SDDs/](Docs/SDDs/README.md): design documents; [Docs/Plans/](Docs/Plans/): implementation plans.
- [Docs/PROJECT_METRICS.md](Docs/PROJECT_METRICS.md): generated effort metrics (time, tokens, notional price, agents).
- [Docs/STYLE_GUIDE.md](Docs/STYLE_GUIDE.md): C++ style. [Docs/Research/](Docs/Research/): research notes, including the performance guidelines. [Docs/ToDo/](Docs/ToDo/README.md): unscheduled ideas.

## Licence

The repository carries a `LICENSE` file (CC0 1.0 Universal text, added in its latest commit). Baked star data derives from CC BY-SA 4.0 catalogues, so check that against the licence before redistributing. The licence choice has not been confirmed by Acid Rain Studios.
