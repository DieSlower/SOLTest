<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->
# Limit Theory: Feature Inventory, Video Catalogue and Code-Base Provenance Audit

Research on **Limit Theory (LT)**, the cancelled procedural space simulation by Josh Parnell (Procedural Reality). It merges four evidence streams into one reference:

1. The 20 monthly development-update videos (Jan 2013 to Nov 2014) and the official prototype tutorial.
2. The official site, FAQ, press and the community Limit Theory Redux wiki.
3. A read-only source audit of the three public repositories: `JoshParnell/ltheory-old` (C++ / LTE / LTSL), `JoshParnell/ltheory` (C + Lua game code) and `JoshParnell/libphx` (the engine under the new code).
4. The earlier version of this document: its requirement lists, development sequence, risks and research plan are kept and updated here.

It catalogues every feature seen in the videos (IDs F-001 to F-370), records which code base (if any) contains it, and turns the findings into a prioritised checklist for building comparable software.

---

## Table of contents

1. [Executive summary](#1-executive-summary)
2. [Scope, evidence labels and limitations](#2-scope-evidence-labels-and-limitations)
3. [Project timeline and history](#3-project-timeline-and-history)
4. [Strengths and weaknesses](#4-strengths-and-weaknesses)
5. [Code-base architecture, engine vs game, and licences](#5-code-base-architecture-engine-vs-game-and-licences)
6. [Core gameplay and flight](#6-core-gameplay-and-flight)
7. [Combat, weapons and fleets](#7-combat-weapons-and-fleets)
8. [Economy and trade](#8-economy-and-trade)
9. [Industry: mining, scanning, production, stations and colonies](#9-industry-mining-scanning-production-stations-and-colonies)
10. [AI and simulation](#10-ai-and-simulation)
11. [Factions, diplomacy and projects](#11-factions-diplomacy-and-projects)
12. [Research, tech trees and procedural technology](#12-research-tech-trees-and-procedural-technology)
13. [Universe and procedural generation](#13-universe-and-procedural-generation)
14. [User interface and user experience](#14-user-interface-and-user-experience)
15. [Graphics, rendering and the visual-effects catalogue](#15-graphics-rendering-and-the-visual-effects-catalogue)
16. [Audio](#16-audio)
17. [Engine, tools and backend](#17-engine-tools-and-backend)
18. [Data editor, LTSL and modding](#18-data-editor-ltsl-and-modding)
19. [Feature provenance matrix](#19-feature-provenance-matrix)
20. [Building comparable software: checklist and development sequence](#20-building-comparable-software-checklist-and-development-sequence)
21. [Key risks](#21-key-risks)
22. [Follow-up source research plan](#22-follow-up-source-research-plan)
23. [Contradictions and unresolved gaps](#23-contradictions-and-unresolved-gaps)
24. [Sources and evidence](#24-sources-and-evidence)
25. [Visual reference](#25-visual-reference)

---

## 1. Executive summary

### 1.1 The videos

From January 2013 to November 2014 Josh Parnell, then the solo developer of Limit Theory, published monthly "Development Update" videos after a successful Kickstarter (launched 20 Nov 2012, $50k goal, about $187.9k raised from 5,449 backers). There are 20 published updates (#1 to #21; #4 was never made, because April 2013 went into shipping the backer prototype). Each one runs 10 to 37 minutes of narrated, live in-engine footage. A typical video covers a few threads at once: a graphics technique, a gameplay system, the AI or simulation, and the UI. Most were recorded in a development build, so crashes, debug tools and placeholders are visible, and Josh often calls features "rough" or "placeholder". A separate official video, the **Prototype Tutorial**, walks through the build that $75+ backers received on 28 Apr 2013.

### 1.2 The software (all videos treated as one product)

LT is a **single-player, open-ended, fully procedural space simulation** with no story and no multiplayer. The player starts as one pilot in a small fighter and can grow into an industrial and military power with fleets, stations and research. Across the videos it amounts to:

- **Flight and combat.** Third-person, Freelancer-inspired flight with mouse aim, roll and a cruise mode. Power is distributed across subsystems. Ships are fitted through typed hardpoints (weapons, thrusters, sensors, shields, industrial modules, drone bays). Shields conform to the hull and only show when hit. Weapons include turrets, missiles, railguns, beams and pulse lasers. Fleets are commanded through an RTS-style command interface.
- **A living, AI-driven universe.** NPCs are goal-driven planners, not scripts. They mine, trade, research and pirate, and they post contracts and missions for each other. They are organised into faction hierarchies whose leaders run **projects**. Player and NPC are **symmetric**: they have the same actions and use the same markets.
- **A real economy.** Bid/ask order-book markets with emergent prices, population-driven demand at planets, commodity charts (candles, EMAs, MACD) and station storage.
- **Industry and procedural technology.** Prospecting with a frequency-spectrum scanner and drones, beam mining with a "transfer unit", cargo measured in mass, and production from blueprints. Research yields **new procedural blueprints**, each a parent design with trade-off modifiers. Technology exists as physical items (blueprints, assembly chips) that can be sold, stolen or destroyed.
- **An infinite procedural universe.** Regions, sub-regions and systems are linked by jump holes or wormholes. Systems contain procedural nebulae, planets with atmospheric scattering, colonies, rings, asteroid and ice zones streamed on demand, and warp lanes (later warp rails).
- **A home-grown engine and toolchain.** Double precision, dynamic entity loading, dynamic lights, volumetric dust, impostors, a node-based UI, the **LT Data Editor**, and **LTSL**, a scripting language with Lisp-like semantics, Python-style blocks and hot reload. By the last videos the UI, the ship generator and the application itself were all scripts.

### 1.3 The released code

All three repositories were published under the **Unlicense** on 24 Jul 2022, after the project was cancelled (28 Sep 2018). Each is a single squashed commit, so per-feature history cannot be recovered from git.

| Repository | Era | What it is | Gameplay depth |
|---|---|---|---|
| `ltheory-old` @ `0535d46` | 2012-2015, the era of the videos | C++ engine (LTE) and game in one shared library `lt`, plus LTSL scripts (141 `.lts`), 169 `.jsl` shaders, about 75k lines of C++ | The richest: order-book markets with EMA history, task-based AI economy, mining, prospecting drones, Tech Lab research with derived blueprints, assembly chips, production labs, colonies, warp rails, scanner, full HUD and market UI in LTSL |
| `ltheory` @ `21d4504` | 2016-2018 rewrite | Lua game code (about 22k lines) over libphx, with a 35-line C++ launcher and the GLSL shader library | Thin: one procedural system, AI mine and transport jobs, pulse combat, an RTS command view, a debug window. The market matcher is broken and factory orders are disabled. No research, factions, save/load or data editor |
| `libphx` @ `0f804f2` | 2016-2018 | A flat C engine library (about 19.5k lines) bound to LuaJIT through FFI: GL 2.1, Bullet 2.87, FMOD Core, SDL2, FreeType, BSP, Surface Nets SDF meshing, GPU AO baking, HmGui | Engine only; no game logic |

**Headline provenance findings.** Most of what the videos show is in `ltheory-old`, at varying maturity. The **LT Data Editor shown in Update 10 is in none of the three repos**. LTSL is only in the old code. The new code base is a technology rewrite that never caught up on gameplay. Game save/load, diplomacy beyond a hostility scalar, and networking are absent everywhere.

---

## 2. Scope, evidence labels and limitations

### 2.1 Evidence labels

| Label | Meaning |
|---|---|
| **[Video Ux]** | Seen working in Development Update #x (or **T**, the prototype tutorial), even if rough. |
| **[Design]** | Stated in official prose (site, FAQ, Kickstarter, Road to Beta) or by Josh in a video as intended, but not shown. |
| **[Code: Old]** | Found in `ltheory-old` by the source audit; a path is given. |
| **[Code: New]** | Found in `ltheory` by the source audit. |
| **[Engine]** | Found in `libphx`, or in the LTE engine layer of `ltheory-old`. Engine support is not proof that the game uses it. |
| **[Required]** | Needed to build comparable software. Not a claim that LT had it. |
| **[Unverified]** | Plausible or claimed by a secondary source, but not confirmed by video or code. |

**Video status values** used in the catalogue tables: **shown** (working on screen), **prototype** (shown, but explicitly a mock-up, hack or dev tool), **planned** (stated, not shown), **cut** (dropped or replaced). The status is as of the last video that mentions the feature. LT was never released, so "shown" never means "shipped".

**Code maturity values** used in the provenance matrix: **present** (a working implementation is there), **partial** (real code, but incomplete or partly disabled), **stub** (data holders, empty functions or TODOs), **absent** (nothing found), **demo** (only in a test app).

### 2.2 Source keys

- **U1 ... U21**: Development Update #N. **U4 does not exist.**
- **T**: the official Limit Theory Prototype Tutorial (April 2013 build).
- **W**: the official site, FAQ, wiki, Redux wiki and press. Used for history and design intent only, not for what the videos show.

### 2.3 Audit boundary and limitations

- Video notes come from auto-generated captions, read in full and paraphrased. Proper nouns heard in captions may be misspelt (e.g. "Verad 9", "Vialo", "Garson", "Palala").
- The repository audit was static and read-only: nothing was built or run. Maturity judgements come from reading code, TODOs and call sites. Some cross-checks during this merge were keyword searches only, and are marked as such.
- The `ltheory` clone did not have its `libphx` submodule checked out. libphx was audited separately at the exact pinned commit (`0f804f2`), so the engine-side claims for the new code are cross-checked, not inferred.
- All three repositories are squashed single commits, so the date a feature was written cannot be recovered.
- The rules from the earlier version of this document still apply: an engine feature is not automatically a game feature; a demo, folder or commit message is not proof of production readiness; and a feature counts as implemented only when an exact path is recorded.

---

## 3. Project timeline and history

### 3.1 Pre-video history

- **2012.** Josh Parnell, a student developer in Louisiana, builds LT as a solo project. Early gameplay demos (Gameplay Demo 1-5) and the Kickstarter video precede the update series.
- **20 Nov 2012.** Kickstarter launches with a $50k goal. It is funded in about 6 days and ends at about $187,865 from 5,449 backers. Stretch goals add OS X and Linux. Release is promised for early 2014.

### 3.2 The development-update series

| Update | Month (covered) | Headline topics |
|---|---|---|
| U1 | Jan 2013 | Procedural nebula breakthrough; detailed procedural asteroids; ship lighting and metal; new physics/collision engine; capital ships at 50x; two-axis turrets; hull-conforming hit-only shields; first command interface (holographic, action menu, follow/attack); explosions; new GUI engine; space dust |
| U2 | Feb 2013 | First SFX; top bar; ship viewer; inventory; drag-and-drop fitting; missiles; OP railgun; command interface overhaul (class icons, colour, solid mode); power distribution with overcharge; UI opacity; tension-driven dynamic music |
| U3 | Mar 2013 | First semi-playable loop: hangar, market of procedural weapons, ship buying/switching, job board (Assassination/Distress/Conflict), HUD widgets, cruise mode, fire-group toggles, AI kiting |
| (U4) | Apr 2013 | **Not published.** The backer prototype shipped on 28 Apr 2013 (covered by tutorial **T**) |
| U5 | May 2013 | Prototype patch; nebula breakthrough; rewritten starfield; rebuilt planets with atmospheric scattering; warp lanes; free camera; jump holes; region/sub-region universe generation and region viewer |
| U6 | Jun 2013 | Ship plating texture, HDR, bloom, metal reflections; dust and god rays; battleship; production with industrial hardpoint, construction module and blueprints (Firefly missiles); guided missiles; Linux port |
| U7 | Jul 2013 | Mac port; first planet surface (ground-to-space scattering, water, trees); planet docking; abstract simulation; planned history generation |
| U8 | Aug 2013 | Dynamic entity loading and infinite hierarchical asteroid fields; asteroid-free warp lanes; placeholder station; double precision 50%; UI refresh; composer music; hypothetical-simulation AI concept |
| U9 | Sep 2013 | Volumetric dust; dynamic lighting; generic anamorphic lens flares; master planning AI (units of functionality, emergent mining) |
| U10 | Oct 2013 | Bloom; double precision done (planets and distances 10x); research system mock-up (procedural modifier tech tree); **LT Data Editor** |
| U11 | Nov 2013 | Gamma-correct rendering and PBR metal; AI rewritten from scratch; "AI-side gameplay first"; node-based 3D UI engine; hierarchical universe map; gamepad; seeking missiles; loot hack |
| U12 | Dec 2013 | One year full-time; HDR backgrounds; metal and environment maps; motion effects; lighting rewrite; holographic ship viewer; split-screen panels; categorical filtering; system map; AI delegation and contract board; release slips to mid-2014 |
| U13 | Jan 2014 | SMAA, chromatic aberration, bloom; cylindrical 3D HUD, cockpit mode, glass HUD, workspaces, pop-out widgets; scanner 1.0; world-space holographic overlays; performance numbers |
| U14 | Feb 2014 | Ice fields; JJ Abrams-style lens flares; new meshing tech; scanner 2.0 (frequency spectrum); prospecting drones; transfer-unit mining 1.0; dynamic mass; boost capacitor |
| U15 | Mar 2014 | 100+ NPC living economy; population demand; bid/ask auction; miner/trader equilibrium; faction leaders ("Captains") and projects; commodity market graphs |
| U16 | Apr 2014 | Graphics pass; NPCs mine for real; pirates; new nodal UI style; personal interface; projects interface; advanced commodities interface (candles, EMA, MACD, order book); player bids/asks |
| U17 | May 2014 | Nebula breakthrough and colour grading; procedural research implemented (Tech Lab, blueprints, assembly chips, Production Lab); continuous missions with reward pools; "data" proof items; meta project AI |
| U18 | Jun 2014 | Zone Tech (bounded procedural zones, rings); curved engine trails; planetary colonies and landing; data language becomes a programming language (compiler); dogfighting; release slips to early 2015 |
| U19 | Jul 2014 | Impostors (60 fps); **LTSL**; ship generator rewritten as a live-editable script; stack/layer UI; live-coded procedural icons |
| U20 | Aug + Sep 2014 | New unified UI engine written 100% in LTSL; targeting tracker, radial menus, scanner 3.0; bloom math; dust transitions; UI compositing and grading; colony cultures and 7 AI personality traits; live coding; launcher and test beds |
| U21 | Oct + Nov 2014 (delivered early Dec) | Warp nodes and warp rails (lanes, eject anywhere, about 9 km/s); new market UI with real order-book pricing; storage locker; 3D model and rail test beds; planetary rings; native Unicode text rendering; UI-to-texture |

### 3.3 After the videos

| Date | Event (W) |
|---|---|
| Dec 2014 | Video updates stop because of the strain. Josh moves to text "Road to Beta" updates, but only chapters 1-2 appear (HUD, hardpoint control groups, HUD customisation, object overview, cargo management, UI finalisation). |
| 2015 | Silence. Burnout and mental-health problems. |
| Sep 2015 | Full-time work resumes in a rented office at the Louisiana Tech Park. |
| Nov 2015 | The engine is judged too complicated; its components are separated. |
| Feb-Mar 2016 | Engine rewrite roughly complete: this is **libphx**. |
| Apr 2016 | New game/mod scripting language announced as Python 2.7 (per the Redux wiki). Lua was eventually used (see chapter 23). |
| Feb 2017 | Josh says performance problems took about 2 years to solve. Bi-weekly devlogs; a community manager is named. |
| May 2017 | New site and a "1.0 feature list" roadmap to curb scope creep. |
| Jul 2017 | The team triples with two programmers. |
| Nov 2017 | An artist/programmer joins to replace the "blocky" ships. |
| Jan 2018 | PAX South demo: planetary system, asteroid field, combat with allied ships, in-game ship redesign and spawning, 2,000+ simulated ships with projectiles and full AI. |
| 28 Sep 2018 | The project is cancelled, citing financial, emotional and physical exhaustion. |
| 24 Jul 2022 | Source released under the Unlicense: `ltheory`, `libphx` and `ltheory-old`, plus the 2013 prototype binary. (The `ltheory` repo's only commit is dated 6 Aug 2022: "remove potentially-copyrighted font; fix HmGui demo gamma space".) |
| 2022 onward | The community continues the new code as **Limit Theory Redux** (Lua on libphx). |

### 3.4 Release-date slips

Early 2014 (Kickstarter), then mid-2014 (U12), then early 2015 with a beta in late 2014 (U18). Never released.

---

## 4. Strengths and weaknesses

### 4.1 Strengths and weaknesses seen in the videos

**Strengths**

- **One coherent simulation, shared by player and NPC.** The same actions (mine, sell, dock, research, post or take missions) drive both, so content emerges from the simulation instead of being authored. Examples: NPCs discovered the mining loop from the single goal "make money" (U9); contracts are real NPC needs, not random missions (U12); supply/demand cycles and the value of labour emerge from bid/ask markets (U15).
- **Planner-based AI with layered abstraction.** There is a master planner over "units of functionality" (U9), rewritten for responsiveness to interruptions (U11). On top of it sit delegation (U12), faction leaders and projects (U15), and meta-projects (U17). An abstract low-LOD simulation lets large regions run at 60 fps (U7, U8).
- **Physical technology.** Research creates real items: blueprints, then assembly chips, then products. So tech can be traded, licensed, stolen and destroyed, which gives economic and combat stakes (U17). Infinite procedural trade-off research avoids runaway power creep (U10, U17).
- **Strong procedural visuals for a solo developer.** Nebulae, volumetric dust, dynamic lights, lens flares, HDR and bloom, PBR metal, atmospheric scattering, rings, ice. Each was iterated repeatedly (U1, U5, U9, U12, U13, U17, U20).
- **Fast-iteration toolchain.** A generic reflective data model and the LT Data Editor (U10), a node UI built from the editor code (U11), LTSL with hot reload and soft compile failure (U19, U20), a launcher plus isolated test beds (U20, U21), and a 3D model test bed for generators (U21). Josh credits these for building a system map in about 25 minutes (U11) and a new scanner widget in about 8 minutes (U20).
- **Scale engineering.** Double-precision positions (U10), hierarchical on-demand entity streaming (U8), bounded deterministic zones (U18), impostors (U19), and lane allocation on warp rails for 128 NPCs at about 9 km/s (U21).
- **Distinctive information gameplay.** Scanning is a first-class activity (U13, U14, U20): frequency-space signatures with pink noise, saturation and star masking, prospecting drones, discovery-gated markers, and inference from public birthplace to hidden AI personality (U20).
- **Moddability.** This went from "basically not gonna happen" to data-level modding (U10), then a scripting language with an engine-call API (U18, U19), then the whole UI and app in script (U20).

**Weaknesses, open problems, slips and cuts**

- **Gameplay lagged far behind tech.** Josh repeatedly says the game is "not fun yet" (U14). Months went to engine, UI or graphics with "no new gameplay" (U1, U7, U19). Most HUD content was placeholder (U13), item info widgets were empty (U21), and player docking, ship buying and storage were missing for long stretches (U12, U15, U16).
- **Repeated rewrites.** The AI was rewritten (U11). The UI went through panels (U2, U6), then the node UI (U11), the new nodal style (U16), the stack/layer UI (U19) and finally the unified LTSL UI engine (U20). The scanner went 1.0 radial (U13), then 2.0 spectrum (U14), then 3.0 amplitude (U20). Fast travel went from warp lanes (U5) to warp rails (U21). After the videos the whole engine was rewritten (2015-2016, LibPHX, W).
- **Performance with many NPCs.** There was lag in fleet battles (U3). Collision was disabled for 100+ NPCs (U15, U16) and again after the zone work (U18). The 128+ NPC system stuttered (U21). Optimisation was repeatedly deferred, and Josh later said solving performance took about 2 years (W).
- **Persistent visual open problems.** Asteroid popping (U10, U18; "conceptually solved" by impostors in U19 but not implemented). Zones and dust don't read from afar (U8, U18, U20). Ships stayed "Lego/box ships" until the meshing tech, and some generated ships were still lackluster (U5, U14, U19, U21). God rays were too strong (U6), lens flares possibly obnoxious (U14), and the interface glitch effect too strong (U14).
- **Economy and AI balance.** Prices were too volatile (U15), NPCs priced badly (U16), piracy was rampant and made projects unprofitable without security (U16), and the market was easy to game (U15). Retaliation was disabled (U17), mission content was sparse ("damage the enemy" is too easy, U17), and dogfighting was dull or too hard (U18).
- **Release slips.** Early 2014 (Kickstarter), then mid-2014 (U12), then early 2015 with beta late 2014 (U18). The update cadence broke down ("failed catastrophically", U21).
- **Cut or scoped out.**
  - Seamless landing and free roaming on planets were ruled out (U7, U18, W).
  - Menu-based planet locations were replaced by spatial colonies (U18).
  - Point-to-point jump fast travel was rejected (U10).
  - Realistic scale is not the default; it can be done as a mod (U10).
  - Code modding was originally off the table (later reversed).
  - Multiplayer was never planned (W).
  - Oculus Rift support was planned (U11) but never shown.
- **Project history (W).**
  - After the videos ended (Dec 2014), Josh switched to text "Road to Beta" updates (only 2 chapters).
  - 2015 brought burnout and silence. He resumed in September 2015.
  - The engine was judged too complicated and rewritten (LibPHX, roughly Feb-Mar 2016). The scripting language changed to Python, then Lua.
  - In 2017 the team grew. At PAX South in January 2018 he showed a demo with 2,000+ simulated ships.
  - The project was cancelled on 28 Sep 2018, citing financial, emotional and physical exhaustion.
  - The source was released under the Unlicense on 24 Jul 2022. The community continues it as Limit Theory Redux.

### 4.2 Strengths and weaknesses seen in the code

**Strengths**

- **The old code is gameplay-rich.** `ltheory-old` actually implements most of the U14-U21 systems: order books with EMA history, 19 AI task types, a project/manager AI, mining and prospecting drones, Tech Lab research with derived blueprints, assembly chips, production labs, colonies, warp nodes and rails, a scanner, and a full LTSL widget library (87 widget files).
- **A clean engine/script boundary.** LTE exposes engine types through reflection macros (`AutoClass`, `DefineFunction`, `FreeFunction`), generated by Python meta scripts. libphx exposes a flat C API that is bound to LuaJIT FFI by a generator (`tool/genffi.lua`). Both give modders the same API the game uses.
- **The procedural art pipelines are complete.** The old code has SDF CSG with 21 operators, a plate-mesh hull generator and GPU nebula/planet generators. The new code has ShapeLib (warps, joints, UV unwrap) with libphx GPU AO baking, a BSP and Surface Nets asteroid meshing.
- **Rendering breadth.** Deferred PBR, SMAA, SSAO (old), bloom, colour grading, lens flares, impostors (old) and atmospheric scattering are all in source.
- **Liberal licence.** All original code is public domain (Unlicense).

**Weaknesses**

- **The new code regressed on gameplay.** Only one system per seed. AI traders and planets are switched off in the main app. The market matcher never matches orders because of a field-name mismatch (`marketData` vs `data`). Factory trade orders are disabled because unbounded orders stalled the game. Research, construction, factions, missions, save/load and the data editor are all absent.
- **Nothing persists.** Neither code base saves universe state. The old code has only a reflective binary serializer used for asset caching. The new code does not even save settings.
- **No game-data editor in any repo**, despite the U10 demo.
- **Precision regressed.** The videos claim full double precision (U10), but libphx physics, meshes and rigid bodies are float only and the new game uses `Vec3f`.
- **Engine debt in libphx:** a disabled shader cache, a leaking collision-shape cache, an O(N²) GPU AO bake, one GL texture per font glyph, a stub HmGui slider, a `Trigger_SetPos` bug and about 90 TODOs.
- **Licence hazards in assets:** see section 5.5.
- **A small code bug found during the merge:** in `ltheory-old/src/liblt/AI/Traits.h` the X-macro lists 7 traits but declares `XSIZE 6`.

---

## 5. Code-base architecture, engine vs game, and licences

### 5.1 Old implementation: `JoshParnell/ltheory-old`

- [Code: Old] The C++ implementation from 2012-2015, the era of the videos. Three layers: the **Limit Theory Engine (LTE)**, the game layer, and **LTSL** scripts (`.lts`).
- [Code: Old] The README describes it as dated compared with the later C/Lua code but "arguably meatier in gameplay implementation".
- [Code: Old] One built executable, `src/launch/launch.cpp`: `launch.exe <app>` runs `resource/script/App/<app>.lts`. There are 31 top-level apps, including `ltheory`, `war` (AI skirmish), `dogfight`, `sandbox`, `universe`, `map`, `market`, `colony`, `observatory`, `hud`, `rails`, `model`, `platemesh`, `loading`, `launcher` and experiments (`Life/`, `brain`, `hnn`, `prime`). The README says many apps are broken or incomplete but some work well enough to fly around a system. The game's default app is `ltheory.lts`.
- [Code: Old] Build: CMake 3.0 plus `configure.py [build|clean|run <app>]`. Only the Win32 build was restored; the Linux path is present but unmaintained. Git LFS is needed (293 LFS files, hydrated in the audited checkout). One submodule: `ext/SFML`.
- [Code: Old] No Lua and no libphx. Scripting is LTSL only.
- [Code: Old] Assets: about 40 Google font families, about 130 sound effects, FMOD Designer music banks (`.fev`/`.fsb`) and a few textures. No meshes or art textures, since geometry and most textures are procedural.
- [Code: Old] Legacy tools under `src/old/` are not built: the old native game main, a launcher, `ltconsole`, `codegen`, `soundstudio`, an `updater` and `makepatch` (a binary-diff patcher for the 2014 "LT Prototype").
- [Code: Old] Size: 468 `.h` + 390 `.cpp` (about 75k lines in `src/liblt`), 141 `.lts` (about 9.9k lines), 169 `.jsl` shaders (about 6.4k lines), 90 game-data `.txt` files.

### 5.2 New implementation: `JoshParnell/ltheory`

- [Code: New] Second-generation game code: C plus Lua. `src/Main.cpp` (35 lines) calls `Engine_Init`, then `Lua_Create`, sets `__app__` from argv, and runs `script/Main.lua`. That loads the namespaces (`Util`, `UI`, `WIP`, `Gen`, `Game`) and runs `App.<name>`, by default `LTheory`.
- [Code: New] Layout: `script/App` (the game plus 12 test/demo apps), `script/Game` (entities, components, actions, jobs, controls, GUI), `script/Gen` (procedural generation), `script/UI` (a retained-mode Lua widget toolkit), `script/env` (stdlib extensions), `script/phx` (the Lua side of the engine binding), and `res/` (GLSL shaders, fonts, meshes, textures, sounds, grammar lists).
- [Code: New] Build: CMake plus `configure.py`. Links `phx` through `add_subdirectory(libphx)`. Needs Git LFS.
- [Code: New] Size: about 22.3k lines of Lua excluding `script/jit/` (stock LuaJIT tooling, vendored).
- [Code: New] The main app runs one procedural system with a player fighter, 100 escorts, one station and one asteroid field. AI traders (`spawnAI` loop `1, 0`) and planets are off.

### 5.3 Engine dependency: `JoshParnell/libphx`

- [Engine] A lightweight, mostly C engine built *as a library*, so game code keeps control of the frame. It is designed to be driven from LuaJIT through FFI. The README calls it abandoned and mostly undocumented.
- [Engine] A flat `PHX_API` C interface over opaque structs, 113 `.cpp` and 129 `.h` (about 19.5k lines). About 120 generated FFI binding files (`script/ffi`), plus hand-written metatype extensions (`script/ffiext`). One tool: `tool/genffi.lua`.
- [Engine] OpenGL through GLEW. The game requests a **GL 2.1 compatibility context** through SDL2. There is no Vulkan or D3D. **There are no shaders in libphx**: every GLSL file lives in the game repo.
- [Engine] Vendored: FMOD Core + Studio 1.10.01, Bullet 2.87, SDL2, GLEW, LuaJIT 2.x, FreeType 2.8, LZ4 1.7, stb, `windirent.h`. OpenVR is referenced only inside `#if 0`.

### 5.4 Which features are engine and which are game, per code base

| Concern | ltheory-old | ltheory (new) + libphx |
|---|---|---|
| Where the engine lives | `src/liblt/LTE` (about 40k lines), `UI/`, `Module/`, `Audio/`, `Volume/`, `CodeGen/`, `Strukt/`, `ThirdParty/`, `Network/`, all in the one `lt` library | `libphx` (separate repo, a C library) |
| Where game logic lives | C++ `Game/` (about 17k lines), `Component/` (about 4.3k lines, ECS-style `AutoComponent` mixins), `AI/` | Lua `script/Game`, `script/Gen` |
| Script layer | LTSL: apps, all UI widgets, ship and station generators, object behaviours | Lua: everything above the engine, including the frame loop |
| Render pipeline (deferred, lighting, post) | Engine (LTE `Renderer`, `RenderPass/*`) and game render passes (`Game/RenderPass/*`) | **Game** (`script/phx/util/Renderer.lua` + `res/shader`). libphx supplies only GL primitives, render targets and shader loading |
| UI framework | Engine `UI/` (widget, glyph, compositor) plus LTSL widgets | **Game** Lua toolkit `script/UI`. libphx's HmGui and its own ImGui are used only by demos |
| Physics | Engine `Module/PhysicsEngine.cpp` (custom, 155 lines) | Engine (Bullet 2.87 wrapper) |
| Procedural meshing | Engine (`LTE/SDF*`, `PlateMesh`, `Volume/` marching cubes) driven by LTSL generators | Game ShapeLib (Lua) for topology. Engine for `Mesh`, `BoxMesh`, GPU AO, BSP and Surface Nets SDF |
| Noise and texture generators | Game (`Game/Graphics/Generator/*`) plus `.jsl` shaders | Game GLSL. libphx has no noise in C, only `TexCube_Generate` and `TexCube_GenIRMap` helpers |
| Audio | Engine `Module/SoundEngine` (FMOD Ex, with a Null fallback) and `MusicEngine` (FMOD Designer banks) | Engine (FMOD Core). The game only initialises audio and sets the listener |
| Scripting runtime | Engine (LTSL compiler and runtime in `LTE/`) | Engine (LuaJIT host, `Lua.cpp`) plus game-side reflection (`phx/util/CStruct.lua`) |
| AI, economy, research, factions | Game (C++) | Game (Lua), where present |

### 5.5 Licence and asset caveats

| Item | Where | Licence / risk |
|---|---|---|
| All original code | all three repos | **Unlicense** (public-domain dedication). It only covers what Josh owned. |
| FMOD Ex / Event binaries and headers | `ltheory-old/extbin`, `extlib`, `include` | **Proprietary.** Treat as non-redistributable. A shipping product needs its own FMOD licence. |
| FMOD Core + Studio 1.10.01 | `libphx/ext` | **Proprietary FMOD EULA.** The Studio DLLs are linked but never called. |
| `res/sound/*.mp3` (cantina, Duel of the Fates, Yoda laugh, Imperial March and others) | `ltheory/res/sound` | **Almost certainly copyrighted Star Wars audio**, not covered by the Unlicense. Used only by the `FMODTest` demo. Do not reuse. |
| Test meshes (`dragon.obj`, `teapot*.obj`, `minicooper.obj`, `alfa147.obj`) | `ltheory/res/mesh` | Unknown provenance. |
| About 130 sound effects and the music banks | `ltheory-old/resource/sound`, `resource/music` | Made by a contracted sound designer and a composer ("Mr. France", eonsounds.com, U8). Whether the Unlicense covers them is **unverified**. Treat as all rights reserved. |
| Fonts | `ltheory-old/resource/font` (Google Fonts), `ltheory/res/font` (Apache 2.0, OFL, UFL; `fa-solid-900.ttf` is Font Awesome) | Permissive but with their own terms. One "potentially-copyrighted font" was removed in the last commit. |
| FreeType | both | FTL (BSD-like with attribution) or GPLv2. |
| SFML, Bullet, SDL2, LuaJIT, GLEW, LZ4, stb, SMAA tables, enet headers | various | Permissive (zlib, MIT, BSD, public domain). |
| Oculus OVR SDK headers | `ltheory-old/include/OVR` | Vendored but unused; Oculus SDK licence. |
| SDL controller DB | `ltheory/res/gamecontrollerdb_205.txt` | zlib. |
| Video footage and screenshots | YouTube | Copyright Josh Parnell / Procedural Reality. Use only as internal research reference (see chapter 25). |

**Practical consequence for a clean-room comparable product:** the Unlicensed code can be studied and reused, but audio, FMOD and fonts must be replaced or licensed separately.

---

## 6. Core gameplay and flight

### 6.1 Product definition

- [Design] An open-ended procedural space simulation with no required linear story ("the player makes their own narrative"; the freedom is inspired by Morrowind). [W]
- [Design] Effectively unlimited exploration in a procedurally generated universe, with a new universe per new game.
- [Video U9, U15, U17] Progression from an individual pilot to owning an organisation, through projects and fleets.
- [Video U9, U15] Autonomous AI activity and a living economy. The player influences the universe but is not its centre ("a little gear in a bigger machine", U7).
- [Design] Stylised rather than realistic. A "space simulator" feel, but fun beats realism. No multiplayer planned. Seamless space-to-planet transitions out of scope for 1.0. [W]

### 6.2 Player activities, and where each is evidenced

| Activity | Video evidence | Code evidence |
|---|---|---|
| Explore | U5, U8, U20 (scanner, discovery gating) | Old: universe, regions, wormholes |
| Mine and scan resources | U13, U14, U16 | Old: present. New: AI only |
| Trade and haul | U15, U16, U21 | Old: present. New: AI transport jobs |
| Fight; raid or counter piracy | U1-U3, U16, U18, T | Old and new: present (new is thinner) |
| Build stations and factories | U6, U17 (production modules only; station building never shown) | Old: production labs; construction drone thin. New: absent |
| Build planetary structures | Not shown | Absent |
| Research technology | U10 (mock-up), U17 (implemented) | Old: partial. New: absent |
| Colonise | Colonies shown as places (U18, U20); player colonisation not shown | Old: colony objects. New: absent |
| Own multiple ships, command fleets | U1-U3, U16, T | Old: assets and tasks. New: command view |
| Influence factions and systems; expand territory | U16-U17 (hostility, projects); territory not shown | Thin in both |

### 6.3 Feature catalogue: core gameplay and flight

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-001 | Third-person flight | Fly a ship in third person among asteroids, stations and NPCs; first-person also cited by W | U1-U21, T | shown |
| F-002 | Flight controls | Mouse yaw/pitch, Q/E roll (rebindable), forward thrust only; strafe/reverse keys exist but no prototype ship has those thrusters | T | shown |
| F-003 | Roll for dodging | Roll stressed as the key evasive manoeuvre | T | shown |
| F-004 | Cruise mode | Shift+W toggles all power to thrusters; much faster; weapons and shields stop recharging, sensors may drop targets, and power takes time to re-route after exit; orange (later blue) cruise-thruster glow | U3, U9, U12, T | shown |
| F-005 | Free camera mode | Camera decoupled from ship orientation; ship turns toward the aim point as fast as it can while turrets track the aim; makes capital-ship combat workable | U5 (prototype patch) | shown |
| F-006 | Locked camera mode | Original ship-oriented camera kept as an option | U5 | shown |
| F-007 | Rigid-body collision | Ships bounce off solid asteroid surfaces; in the real game a crash would destroy the ship; disabled for 100+ NPCs and after zone changes; physics on again in U20 | U1, U15, U16, U18, U20 | shown (intermittently disabled) |
| F-008 | Asteroid landing and hiding | Set down on asteroids, hide in craters (cannot leave the ship) | U1 | shown |
| F-009 | Station docking | Dock inside stations (asteroid base hangar deck); player docking temporarily unavailable in U12/U16 builds | U3, U9, U12, U16, T | shown |
| F-010 | Undock | Undock button bottom right of the docked screen | T | shown |
| F-011 | Start state | Docked in an asteroid base, basic fighter (4 thrusters, 1 sensor, no cargo, empty hardpoints), 1,000 credits | U3, T | shown |
| F-012 | New game setup | Choose 1 of 4 starting systems (no inter-system travel in the prototype); type a name or generate one procedurally | T | shown |
| F-013 | Own and switch ships | Buy ships (owned ships change colour), "pilot ship" switches into one, the old ship stays manageable | U3, T | shown |
| F-014 | Any ship is pilotable | Any ship you see can be bought and flown, including very large mining ships; in U16 ships were stolen via the dev menu because buying wasn't wired | U16, U17, U18 | shown / planned (buying) |
| F-015 | Ship transfer rules | Switching ship requires docking or a carrier in the real game | U16 | planned |
| F-016 | Ship class ladder | Fighter, then heavy/military fighter, bomber, frigate, destroyer, battleship; capitals at 50x fighter scale, target ~10,000x | U1, U3, U6, U10, T | shown (50x) / planned (10,000x) |
| F-017 | Capital-ship piloting | Fly a Battleship with a bomber escort; big ships accelerate slowly but have many thrusters | U6, U17 | shown |
| F-018 | Power distribution | Relative per-subsystem power sliders; overcharge (e.g. pulse weapons at 240%) with diminishing returns; presets (shields, thrusters, weapons); cruise overrides and settings reapply on exit; missiles are unpowered | U2, U3, T | shown |
| F-019 | Boost capacitor | Afterburner-like battery that boosts the entire power grid, including weapons and transfer unit | U14 | shown |
| F-020 | Dynamic mass | Top speed falls as cargo mass grows (full hold ~30-40% slower); mass is also a ship and tech stat | U10, U14 | shown |
| F-021 | Hull and subsystem damage | Hull = base health, zero means destroyed; subsystems (thrusters, armour, shields...) take damage individually | U3, T | shown |
| F-022 | Repair service | Paid repair when docked, cost shown in the ships menu | U3, T | shown |
| F-023 | Fitting by drag and drop | Drag items from cargo onto hardpoints; refitting requires docking in the real game (in-space fitting was a demo cheat) | U2, U3, U6, T | shown |
| F-024 | Hardpoint classes | Weapon/turret, thruster, sensor/scanner, generator, shield, drone bay, and industrial ("IND"; big ships, stations and planets only) | U2, U6, U10, U12, U14, U17 | shown |
| F-025 | Fire groups | Per-weapon on/off toggles; hotkeys planned; "hardpoint control groups" in Road to Beta (W) | U3, W | shown (toggles) / planned (hotkeys) |
| F-026 | Warp lanes | Intra-system fast-travel lanes you fly into; cleared of asteroids ("cleanup crew"); the lane graph defines an in-system distance metric; superseded by warp rails | U5, U8, U10 | cut (replaced by F-027) |
| F-027 | Warp nodes and warp rails | Orb-like warp nodes launch ships onto invisible "subspace" energy rails (no tube, so scenery stays visible); linear or curved (through rings); bidirectional; enter at any node, eject anywhere; ~9 km/s in the test bed | U21 | shown |
| F-028 | Rail speed falloff and lanes | Speed falls inversely with distance from the rail centre; big ships pushed to outer, slower lanes, fighters ride inside | U21 | shown |
| F-029 | Jump holes / wormholes | Inter-system travel through anomalies ("a jump gate minus the gate"); instant cut transition; called wormholes in U20 with strong scanner signals | U5, U8, U20 | shown |
| F-030 | Jump gates (structures) | Gate structures around jump holes | U5 | planned |
| F-031 | Capital jump drives | Capital-only, very expensive drives exploiting the gap between physical and jump-network distance | U5 | planned |
| F-032 | Planet docking (fade) | Shift+D docks with a planet like a station, with fade in/out | U7 | shown |
| F-033 | Menu-based planet locations | Each planet has a list of locations you fade between | U7 | cut (replaced by F-034) |
| F-034 | Colony landing | Approach a colony icon, enter its zone (e.g. "Vialo"), dock, loading screen, generated surface view, menu interaction | U18 | shown |
| F-035 | Seamless landing / surface flight | Free roaming or seamless descent | U7, U18, W | cut |
| F-036 | Time acceleration | Speed up time (dev feature); used for travel and economy demos | U12, U15, U16, U17 | prototype |
| F-037 | Death and restart | Game restarts after the player dies | U18 | shown |
| F-038 | Asset naming | Name any owned asset freely (e.g. "secret science vessel") | U17 | shown |
| F-039 | Discovery gating | Undiscovered objects (e.g. wormholes) are not drawn in world-space markers | U20 | shown |
| F-040 | Targeting with tracker | Hover and press a hotkey to target; a tracker on the reticle follows the target while manoeuvring | U20 | shown |
| F-041 | Scan another ship | View a scanned ship's cargo/inventory live (ore count rising) | U11, U13 | prototype |
| F-042 | Cargo loot scooping | Kills drop cargo containers the player can pick up | U11 | prototype ("not real gameplay") |
| F-043 | Station storage locker | Buy while near (not docked at) a station; goods go to a per-player locker; "transfer to" cargo when docked | U16 (missing), U21 | shown |
| F-044 | Journals and bookmarks | Mark good ore locations; bookmark scanner signals | U13, U20 | planned |
| F-045 | Headlights | Possible ship headlights for dark systems (enabled by dynamic lighting) | U9 | planned (idea) |
| F-046 | Sandbox framing | Single-player, no story; the player is "a little gear in a bigger machine"; procedural narrative (W: Morrowind-inspired freedom) | U7, U8, W | shown (design) |
| F-047 | Player/NPC symmetry | Player and AI have the same actions, markets and missions; the player can be overlord or subordinate | U9, U15, U17 | shown |
| F-048 | Dev teleport | Jump straight to any object (can drop you inside an asteroid) | U20 | prototype (dev tool) |

### 6.4 Prototype-tutorial controls

**Controls from the prototype tutorial (T).** Mouse = yaw/pitch, Q/E = roll, W = thrust (strafe/reverse bound but unusable), Shift+W = cruise toggle, Space = top bar, Y = command interface, Tab = holographic/realistic toggle, F = follow selection, right-drag = orbit camera, scroll = zoom, WASD = detach camera, double-click = focus, drag = box select, right-click = context order. Shift+D docks with a planet (U7); targeting is hover plus a hotkey (U20). All keys are rebindable.

---

## 7. Combat, weapons and fleets

### 7.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-049 | Pulse lasers | Small and big pulse energy weapons; projectiles emit dynamic light; muzzle flashes | U2, U3, U9, U12 | shown |
| F-050 | Missiles (unguided) | Ammo-based, long range, unpowered (not on the power grid), ammo uses cargo; Josh's favourite weapon | U2, U3 | shown |
| F-051 | Guided / seeking missiles | Guided missiles (bug: settling into stable orbits around targets); seeking missiles later | U6, U11 | shown |
| F-052 | Railguns | First an overpowered "instantaneous missile" demo, then a first-class weapon: very fast projectile, short range | U2, U3 | shown |
| F-053 | Beam weapons | Instant-hit beams; with rails, recommended against fast fighters | U3, T | shown |
| F-054 | Weapon cooldown and magazines | Per-weapon cooldown; magazine weapons reload longer when empty | T | shown |
| F-055 | Two-axis turrets | Swivelling base plus pitching barrel; track the aim point while the ship rolls | U1, U5 | shown |
| F-056 | Hit-only conforming shields | Invisible until hit; impact area lights up; shape conforms to the hull | U1 | shown |
| F-057 | Under-the-shield tactic | Small craft fly inside a capital's shield so shots hit the hull; AI meant to use it | U1 | shown / planned (AI use) |
| F-058 | Shield tightness trade-off | Tight shields weaker; loose "bubbly" shields stronger but easier to get inside | U1 | planned |
| F-059 | Multiple shields per ship | One shield per generator hardpoint | U1 | planned |
| F-060 | Shield regen and cooldown | Regenerating armour layer; cooldown before it returns after full depletion; recommended first purchase | T | shown |
| F-061 | Destructible hardpoints | Individual turrets and hardpoints can be destroyed | U1 | shown |
| F-062 | Engagement range | Weapon ranges matter: at long range only missiles reach; AI fires only weapons in range | U2, U3 | shown |
| F-063 | Long-range bombardment | Fleet-vs-fleet missile salvos collapsing shields and killing capitals | U2 | shown |
| F-064 | Target lead indicator | Red cross showing where to aim, appears in range | T | shown |
| F-065 | Aim assistance | On by default; turrets lock onto the lead indicator when the cursor is near; toggle in settings | T | shown |
| F-066 | Friend/foe colours | Green allies, red enemies, grey dead; attacking a faction turns it red | U3, U17 | shown |
| F-067 | Friendly fire | Allies can be targeted and damaged | U6 | shown |
| F-068 | Combat tactics guidance | Don't charge head-on; approach from the side, orbit, roll, shift power to thrusters or shields | T | shown |
| F-069 | Kiting and range AI | AI keeps its range advantage, "bobs and weaves"; an all-missile ship outran ~20 pursuers | U3 | shown |
| F-070 | Heat detection | Scanner detects heat-emitting enemies and cruising ships | U13 | shown |
| F-071 | Star masking | Hide your signature in the star's spectral band by positioning between star and enemy | U14 | shown |
| F-072 | Dogfighting | Dogfights in planetary belts; AI very accurate; hide behind asteroids | U18 | shown |
| F-073 | Size immunity | Small pirates can't hurt big ships, so big miners ignore them | U16 | shown |
| F-074 | Retaliation | Factions retaliating against attackers | U17 | planned (disabled) |
| F-075 | AI combat test bed | Pit AIs against each other headless and read statistics to tune dogfight and fleet algorithms | U20 | shown |
| F-076 | Fleet battles | Conflict / Major Conflict missions with 10-30 ships per side; caused lag | U3, T | shown |

Fleet command screens (command interface, asset panel, leader-follower groups) are catalogued in chapter 14 (F-226 to F-230); escort, wingman and patrol AI in chapter 10 (F-134, F-135).

### 7.2 Corrections to the earlier version of this document

- "Battles involving hundreds of ships" was not shown in the update videos. The largest shown were Conflict missions with 10-30 ships per side (U3, T). The 2,000+ ship figure is from the PAX South 2018 demo of the new code [W].
- "Ambushes", "full-system assaults", "faction wars", "treaties", "planets can be overrun" and "systems can change hands" appear in **no video and in no code**. They are downgraded to **[Unverified]**: claimed only in secondary or official prose.
- Shields were listed as [Unverified]; they were **shown in U1** (hit-only, hull-conforming) and are in `ltheory-old` (`Game/Object/Shield.cpp`, `shield*` shaders).

### 7.3 Required combat systems

- [Required] Targeting; weapon range; beam and projectile weapons; a damage model; an armour, shield or subsystem model.
- [Required] AI targeting; threat assessment; formation management; tactical orders; escort, patrol and retreat behaviour.
- [Required] Resupply and repair; fleet tasking; performance scaling for large battles; debris, salvage, disabled and destroyed-ship states.

---

## 8. Economy and trade

### 8.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-077 | Credits | Single currency; start with 1,000; "credits transferred" notifications | U3, T | shown |
| F-078 | Equipment market (v1) | List of procedural weapons sorted by price, stats balanced to value (range vs rate of fire supports sniper or "chain gun" styles) | U3, T | shown |
| F-079 | Ship dealer | Ships for sale shown in orange (Destroyer 69,000 cr, Bomber 6,300 cr); big ships rare | U3, T | shown |
| F-080 | Selling | Selling items and ships mirrors buying | U3 | shown |
| F-081 | Mission rewards | Assassination 1,000; Distress 4,600; Conflict 2,600; a 120,000 cr mission against destroyers; occasional item rewards | U3, T | shown |
| F-082 | Contract board as market | NPC-posted requests with prices (research units, ore); Josh notes item requests are effectively buy orders | U12 | shown |
| F-083 | Commodities | Procedural commodities: veridium, alite, kite; ore contains commodities (composition visible in data editor) | U14, U15, U16 | shown |
| F-084 | Population demand | A planet's orbital station population demands a commodity with hunger-like growth; price capped (~100 cr/unit at peak need) | U15, U16 | shown |
| F-085 | Bid/ask auction | Seller sets a minimum, buyer a maximum; overlap trades at the overlap point; ask volume = supply, bid volume = demand | U15 | shown |
| F-086 | Emergent supply/demand cycles | Oversupply crashes price (e.g. 2,000 units listed), miners stop, price recovers; oscillation around equilibrium; volatility exaggerated | U15, U16 | shown |
| F-087 | Trading-outpost intermediary | Outpost ("Garson") near fields: miners drop ore, traders haul to demand; most volatile market | U15 | shown |
| F-088 | Emergent labour value | Prices settle so everyone earns a reasonable return; any change (distance, new station) re-equilibrates | U15 | shown |
| F-089 | Real order book | Lists of actual NPC bids and asks; big speculative spreads (e.g. NPC asking 4,000) | U16, U21 | shown |
| F-090 | Player orders | Player places bids/asks; own listings in green; goods stored at station; a lost-order bug seen | U16 | shown |
| F-091 | Order-book average pricing | Buying more climbs through pricier listings, so average price per unit rises (e.g. 2/unit up to 6 units, changes at 7) | U21 | shown |
| F-092 | Proceeds to producers | Money paid goes directly to the factions that mined and sold the goods | U21 | shown |
| F-093 | AI pricing | NPCs set their own sale prices; over/under-pricing seen; improving valuation reduced chaos | U16 | shown |
| F-094 | Cargo by mass | Capacity measured in mass, not volume ("volumetric compression"); percent of capacity shown | U14 | shown |
| F-095 | Ammo logistics | Missile ammo consumes cargo (trade-off vs energy weapons) | U2 | shown |
| F-096 | Contraband flavour | Asteroid-field bases to "manufacture illegal goods" | U8 | planned (flavour) |
| F-097 | Reward-pool missions | Poster funds a pool; payout proportional to work until the pool runs out; poster replenishes to keep the job alive | U17 | shown |
| F-098 | Data redemption | Collectible "data" items prove actions and are redeemed with the poster for payment | U17 | shown |
| F-099 | Tech licensing | Blueprint owner controls supply by minting assembly chips (licensing, not giving away the blueprint) | U17 | shown (mechanic) |
| F-100 | Personality data market | Buy an AI player's personality profile, or learn it by working with them | U20 | planned |
| F-101 | Buy/sell buttons (player) | Direct player buy/sell was missing in U15, partly via orders in U16, full buy panel in U21 | U15, U16, U21 | shown (U21) |
| F-102 | Scripted market stock | Weapons injected into the U21 market by script because AI didn't yet sell all item types | U21 | prototype |

### 8.2 Claims from the earlier document, checked

| Earlier claim | Status now |
|---|---|
| Dynamic supply-and-demand economy, dynamic prices | **Confirmed** [Video U15, U16, U21] and [Code: Old] `Component/Market`, `Component/Economy` |
| AI takes part in the economy | **Confirmed** [Video U9, U15, U16] |
| Mining, trade, production and faction behaviour are interconnected | **Confirmed** [Video U15-U17] |
| Player activity influences local systems | Partly shown (player bids and asks, U16; NPC reaction is design intent, U9) |
| Raids, blockades and factional takeovers move prices | Piracy shown making projects unprofitable (U16). Blockades and takeovers: **[Unverified]** |

### 8.3 Required economy components

- [Required] Goods: raw and refined resources, components, construction materials, equipment, weapons; commodity classification; legality and rarity rules.
- [Required] Markets: inventories, buy and sell orders, supply and demand calculations, production and consumption rates, price history, trend display, shortage and glut alerts.
- [Required] Logistics: trade-route planning, freight contracts, cargo capacity, shipping-risk calculation, scheduling, player-owned logistics assets, NPC miners/haulers/traders/producers, aggregate off-screen logistics simulation.
- [Required] Diagnostics: economy diagnostics and stability controls.

### 8.4 Economy stability controls

LT's own videos show why these matter: prices were too volatile (U15), NPCs mispriced goods (U16), piracy made projects unprofitable (U16), and in the new code unbounded factory orders stalled the game.

- [Required] Anti-inflation and anti-deflation controls.
- [Required] Recovery mechanisms for damaged faction economies.
- [Required] Failsafes for deadlocked production chains, and bounds on outstanding orders.
- [Required] Replacement rules for NPC traders, haulers, producers and miners.
- [Required] Shortage detection; commodity-flow visualisation; market-health reporting.
- [Required] Developer diagnostics for price spikes, halted logistics and unreachable routes.

---

## 9. Industry: mining, scanning, production, stations and colonies

### 9.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-103 | Ore-bearing asteroids | Most asteroids are plain rock; some hold minable ore in deposits | U9, U13, U14 | shown |
| F-104 | Deposit access points | Deposits have surface access points of varying density (yield); thin surfaces are best | U14 | shown |
| F-105 | Transfer unit | Mining beam equipment waved over an asteroid; drains ore at a rate proportional to access-point density; audio rises with mass flow (placeholder sphere model in U12) | U12, U14, U16 | shown |
| F-106 | Overflow destroys matter | At 100% capacity the beam keeps draining and destroys ore ("be careful"; feature vs bug undecided) | U14 | shown |
| F-107 | Beam stress tell | Transfer beam glows red as the hold fills, a visual cue for watching pirates | U14 | shown |
| F-108 | Blind mining | Mine without drones by finding thin entry points, guided by the transfer-unit sound | U14, U15 | shown |
| F-109 | Mining kept fast | Extraction deliberately quick; the challenge is finding ore (coarse scan, then drones) | U14 | shown (design) |
| F-110 | Drone bay | Ship hardpoint that launches drones | U14 | shown |
| F-111 | Prospecting drones | Drone sticks to an asteroid and reports via HUD widget: blank (no ore), rotating square (weak), or deposit type, units (~400) and density (19%, 63%, ~70%) | U14 | shown |
| F-112 | More drone types | Drill drones for unattended long-term mining; recallable or cheap expendable prospectors | U14 | planned |
| F-113 | Scanner 1.0 (radial) | Active scanner: radial time-history signal plot around a centre aperture circle; adjustable cone (wide sweep, then narrow); finds jump holes, ore, cruising NPCs; audio cue | U13 | cut (replaced by F-114) |
| F-114 | Scanner 2.0 (spectrum) | Linear frequency spectrum from Hz to ~10^21 Hz; signals have frequency, width and shape; pink (1/f) noise; strong signals saturate | U14 | cut (replaced by F-115) |
| F-115 | Scanner 3.0 (amplitude) | Signal-amplitude display aimed with the camera; picks up star, ore, colonies, wormholes; saturates near wormholes; drawn entirely in LTSL | U19, U20 | shown |
| F-116 | Isolated signature widget | Object-info widget showing an object's "naked" scanner signature; built live in ~8 minutes | U20 | prototype |
| F-117 | Signal catalogue / auto-match | Scanner matches signals ("95% match"), learn signal shapes; noisier with distance | U14, U20 | planned |
| F-118 | Signature types | Distinct types (heat, radioactive/nuclear, ore, jump hole) | U13 | planned |
| F-119 | Industrial hardpoint modules | Heavy modules for ore refining, research and construction on industrial hardpoints | U6 | shown (construction) / planned (refining) |
| F-120 | Construction module + blueprint | Drop a blueprint into the module's slot to see materials per batch, units per batch (16) and time per batch (20 s); produce button | U6 | shown |
| F-121 | Loop production | Repeat batches continuously (mass-produced "Firefly" missiles) | U6 | shown |
| F-122 | Munition loading | Drop munitions into a launcher's munition slot to chamber them; item leaves inventory | U6 | shown |
| F-123 | One-ship production chain | Mine, refine, process and build aboard a single big ship | U6 | planned |
| F-124 | Tech Lab | Large internal module (very large ships or stations only) running research and assembly-chip tasks | U17 | shown |
| F-125 | Production Lab | Module that runs production tasks from an assembly chip plus raw materials (produced 30, then 60 transfer units) | U17 | shown |
| F-126 | Raw material requirements | Inputs per item/batch (simplified in demos; not yet displayed in U17 UI) | U6, U17 | shown (partial) |
| F-127 | Station meshes | Placeholder radial-primitive station ("bicycle wheels"), then new meshing tech | U8, U14 | prototype |
| F-128 | Asteroid bases | Stations built into large asteroids, with hangars | U3, U5, T | shown |
| F-129 | Station services | Station node with inventory (grid with 3D item models), contract board, market, storage | U12, U21 | shown |
| F-130 | Planetary colonies | Several colonies per planet, icons visible from space; act like stations (market, mission board, storage) | U18, U20 | shown |
| F-131 | Colony types and transforms | Farming, military, research colonies with type-specific input/output transforms; logic runs but isn't wired into the economy | U18 | prototype |
| F-132 | Player-built infrastructure | Build trade outposts or production plants elsewhere to reshape the economy | U15 | planned |
| F-133 | Mining-ship class | Very large mining ships / barges actively mining | U16, U18 | shown |

### 9.2 Notes

- The earlier document's "radial scanner menu" is scanner **1.0's radial signal plot** (U13). The scanner went through three versions (F-113 to F-115), and the bottom-screen spectrum display is version 2.0 (U14).
- "Vein triangulation using multiple drone readings" was stated as the intended use of prospecting drones (U14). Each drone shows its own reading; automatic triangulation was not shown.
- "Buildings on planetary surfaces" and player colonisation were not shown. Colonies exist as generated places you visit (U18, U20).

### 9.3 Required systems: exploration, scanning and mining

- [Required] Scanning: scan range, sensor field of view, signal quality, detection confidence, resource signatures.
- [Required] Deposits: asteroid composition model, ore-vein model, deposit depletion.
- [Required] Drones: inventory, launch and recovery, telemetry, loss and failure states.
- [Required] Beam: range, efficiency, energy and heat model.
- [Required] Downstream: cargo transfer, resource processing and refining, a mining risk model.

### 9.4 Required systems: stations, factories, colonies and construction

- [Required] Construction materials, build queues, construction duration, station modules, factory recipes.
- [Required] Storage, input/output logistics, maintenance, repairs, upgrades.
- [Required] Ownership, asset permissions, defences, a vulnerability and damage model.
- [Required] Colony outputs, demand and development; an asset-management dashboard.

---

## 10. AI and simulation

### 10.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-134 | Escort and follow AI | Escorts follow a target positioned by a lead-time parameter (ahead, beside, behind) | U1, T | shown |
| F-135 | Wingman, patrol and leader AI | Wingmen auto-engage (hostility shared per owner); patrols roam and attack hostiles; leaders use cruise so groups arrive together | T | shown |
| F-136 | Abstract simulation | A lower-LOD abstract model of the world and of gameplay interactions, hooked together so large regions are simulated at once; runs at 60 fps | U7, U8 | shown (internal) |
| F-137 | Multi-level abstraction | The engine "thinks" at several levels of abstraction; the universe is infinite and can't all be simulated at full detail | U7 | shown (concept) |
| F-138 | History generation | A centuries-long coarse pre-game simulation decides where stations and factions are, for a "web of coherence" | U7, U8 | planned |
| F-139 | Hypothetical-simulation reasoning | NPCs ask the abstract simulator "if I do this, what happens?"; the same architecture as the world simulation ("symmetric") | U8 | shown (concept) |
| F-140 | Master planning algorithm | Chains "units of functionality" (actions with preconditions such as extract ore, sell, navigate, dock, buy, mount) to meet goals; no scripts, state machines or trigger transitions | U9 | shown |
| F-141 | Emergent professions | Given only "make money", NPCs worked out the buy-mining-gear, mine, sell loop | U9 | shown |
| F-142 | Reactive AI | Player actions (trading, destroying ships, building stations) create opportunities the AI folds into plans | U9 | shown / planned |
| F-143 | AI rewrite (fluid goals) | New AI handles short-, medium- and long-term goals on one timeframe and adapts to interruptions; faster and lighter than the plan-then-follow model | U11 | shown |
| F-144 | AI-side gameplay first | Mechanics built for the AI with no graphics/UI first, to test behaviour patterns (NPCs mined invisibly) | U11 | shown (process) |
| F-145 | Delegation and contracts | NPCs split plans into sub-tasks and post them as real contracts at stations; can encode arbitrary world conditions ("I wish this person were dead", "repair me") | U12 | shown |
| F-146 | NPC research | NPCs dock to research and request "research units" | U12 | shown |
| F-147 | Population scale | ~9 NPCs (U11), then 100+ per system (U15/U16), then 128+ from 8 factions (U21); W: 2,000+ ships at PAX 2018 | U11, U15, U16, U21, W | shown |
| F-148 | Time-compression hack | Actions complete after travelling a percentage of the distance (~100x faster economy, for observation); removed in U16 | U15 | prototype |
| F-149 | Profit-driven job choice | NPCs choose miner or trader by expected profit, not fixed ratios; traders and miners flow in opposite directions | U15 | shown |
| F-150 | Faction leaders ("Captains") | Leaders ("overlords") sit in a spherical shell outside the system, evaluate market and assets and allocate them to projects; can't manage and act at once yet | U15 | shown |
| F-151 | Business-strategy AI | High-level AI thinks like a CEO: creates profitable projects and allocates owned ships, composing low-level actions | U16 | shown |
| F-152 | Miner/pirate equilibrium | Pirates prey on miners; NPC miners use real transfer units and blind mining with the player's logic | U16 | shown |
| F-153 | Meta project AI | Top AI layer improves projects by eliminating competition, catching up or expanding, through missions; "all AI layers in place" | U17 | shown |
| F-154 | AI-posted, AI-run missions | AI posts missions only when they benefit it; other AI take them (two NPCs killed a StarTek ship for the bounty) | U17 | shown |
| F-155 | Continuous processes | Simulation built around ongoing processes (projects, continuous missions) rather than one-shots | U16, U17 | shown (design) |
| F-156 | Personality traits | 7 traits on every AI player, shown as meters: aggressive, creative, explorative, greedy, intellectual, lawless, sociable; they drive decisions and profession | U20 | shown |
| F-157 | AI birth at colonies | AIs are born at colonies, inheriting the colony culture plus a random offset | U20 | shown |
| F-158 | Public birthplace inference | Personality hidden but birthplace public; infer it (born at aggressive/lawless "Verad 9", probably a pirate) | U20 | shown (design) |
| F-159 | Rail-exploiting AI | AI enters rails at any node and ejects when that is faster ("exploit that maximally") | U21 | shown |
| F-160 | Warp node controller | Splits the space around rails into per-ship lanes (wedge / annular-sector allocation); 128 NPCs, no head-on collisions | U21 | shown |
| F-161 | Leader brain view | UI to look inside a leader's reasoning | U15 | planned |
| F-162 | AI keeps running on script errors | AI continues while broken LTSL is cut out | U20 | shown |

### 10.2 How the AI evolved

1. **Scripted behaviours (U1-U3, T):** escort with lead time, wingmen, patrols, kiting.
2. **Abstract simulation (U7-U8):** a low-LOD model of the world and its interactions, hooked together so large regions run at once. A coarse "history" pre-simulation was planned but not shown.
3. **Master planner (U9):** chains "units of functionality" (actions with preconditions) to meet a goal. NPCs worked out the mining profession from the single goal "make money".
4. **Rewrite with fluid goals (U11):** short-, medium- and long-term goals on one timeframe, responsive to interruptions. Mechanics are built AI-side first, with no graphics.
5. **Delegation (U12):** plans are split into sub-tasks posted as real contracts.
6. **Leaders and projects (U15-U16):** faction leaders ("Captains") allocate assets to profitable projects; a business-strategy layer.
7. **Meta-projects (U17):** the top layer improves projects through missions. Josh says "all AI layers" are now in place.
8. **Personalities (U20):** 7 traits inherited from the colony an AI is born at.

**In code.** `ltheory-old` has 19 pooled task types (`Game/Task/*.cpp`: Buy, Custom, Destroy, Dock, Drill, Goto, LOD, Manage, Mine, Mint, Patrol, Pirate, Play, Produce, Research, Sell, Spawn, Transport, Wait), the `Manage` owner/asset-manager AI, the 7-trait `AI/Traits.h`, and an experimental NLP event phraser. The Action/Condition folders hold one or a few entries each, so a general precondition planner is a **stub**. `ltheory` has an action stack per entity, Mine and Transport jobs, and a `Think` action that samples 100 random jobs per idle asset and takes the best payout. Pathfinding and capital expenditure are TODOs.

### 10.3 Required AI systems

- [Required] Goal selection; opportunity detection; threat detection; multi-step planning.
- [Required] Navigation, docking, mining, hauling, trading, combat.
- [Required] Fleet coordination; retreat and resupply behaviour; recovery from task failure.
- [Required] Faction strategic planning; faction research priorities; economic planning.

### 10.4 Required debugging and QA tools

LT's videos show how much these tools paid off: a system-view map with AI on rails (U21), a headless AI combat test bed with statistics (U20), and the data editor used instead of console prints (U10). In code, the old repo has a DevPanel (apps, clock, performance, status, tasks). The new repo has a debug window with a read-only reflective inspector, settings sliders, and time-acceleration and instant-jobs flags.

- [Required] Agent goal inspector; current-task inspector; decision trace.
- [Required] Path and target visualisation; economy-flow and production-chain visualisation.
- [Required] Faction-relation timeline; territory-change log; event log.
- [Required] Pause, single-step, slow motion, fast-forward; replay tools.
- [Required] Seed capture for bug reports; deterministic test scenarios; long-duration soak tests.
- [Required] CPU, GPU, memory and AI profiling.

---

## 11. Factions, diplomacy and projects

### 11.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-163 | Factions at war (goal) | Factions go about their business and wage war; strategic station placement | U7 | planned |
| F-164 | Factions as economic hierarchies | Leaders plus hired workers; orders from superiors = accepted missions | U15 | shown |
| F-165 | Projects | Project = task + scale + allocation; player can be the overlord assigning NPCs or hire out as a subordinate | U15, U16 | shown |
| F-166 | Player mining project | Create a project, allocate ships ("you're allocated to that project"); it runs automatically (earned 7,000) | U16 | shown |
| F-167 | Project P/L tracking | Each project tracked as profit/loss (an NPC project earned 43,000; the pirate faction lost money) | U16 | shown |
| F-168 | More project types | Production, research, construction; output controls (sell within a price band or store for other projects) | U15, U16 | planned |
| F-169 | Empire management | Run an empire through projects without micromanagement | U16 | shown (early) / planned |
| F-170 | Pirate faction | Pirates as a real faction with finances | U16, U21 | shown |
| F-171 | Security and bounty hunters | System piracy rate; risky project owners hire bounty hunters | U16 | planned |
| F-172 | Named factions | e.g. StarTek Corporation; 8 factions in one system | U17, U21 | shown |
| F-173 | Hostility by action | Attacking a faction turns it hostile ("choose your friends and enemies"); hostility shared across one owner's ships | U17, T | shown |
| F-174 | Zone claims | Unregistered zones show as "unknown" ("Wild West"), implying claimed zones | U18 | shown (label) |
| F-175 | Missions v1 | Procedural Assassination, Distress Call, Conflict/Major Conflict; value indicates difficulty; objectives near-identical | U3, T | shown (later replaced) |
| F-176 | Missions v2 (continuous) | Repeatable jobs with scalable goals and reward pools; "Damage the enemy" pays per damage, capped by the pool (demo 2,000 cr); some one-shot | U17 | shown |
| F-177 | Data proof categories | Proof types: damaged, destroyed, stolen, discovered, explored, patrolled | U17 | planned (damaged shown) |
| F-178 | Player-posted missions | Player posts missions for AI | U17 | planned |
| F-179 | Diplomacy beyond hostility | Treaties, alliances, territory change, planets overrun | none | not shown in any video (only hostility toggling and zone claims) |

### 11.2 What exists, and what does not

- **Shown:** factions as economic hierarchies with leaders and workers; projects with profit/loss; a pirate faction with real finances; named factions (StarTek, 8 per system in U21); hostility caused by attacking; "unknown" (unclaimed) zone labels.
- **Not shown in any video:** treaties, alliances, ceasefires, embargoes, territory change, planets overrun, systems changing hands. "Dynamic AI factions" appears only in a secondary source (Grokipedia) [Unverified].
- **Code:** old, a faction is a `Player`, named from the `$faction` grammar. `Component/Opinion.h` is a per-object opinion map clamped to [-1, 1] that changes when something is destroyed (with a "TODO: opinions are permanent"). New, `Components/Dispositions.lua` is a per-target scalar with friendly and hostile thresholds and a HUD colour. **No diplomacy logic exists in either code base.**

### 11.3 Required faction and diplomacy systems

- [Required] Faction identity, territory, economy, industry, fleet strength.
- [Required] Reputation; alliances; treaties; neutrality; war; embargoes; ceasefires.
- [Required] Faction goals; expansion, defence and research priorities.
- [Required] Territory-change history; conflict history.

---

## 12. Research, tech trees and procedural technology

### 12.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-180 | Procedural items by value | Procedural weapons/equipment with stats balanced to price and generated names ("Alpha Photon Blades", "Hilbert's Tachyon Cannon") | U3, U21, T | shown |
| F-181 | Research on industrial hardpoints | Research is a planned function of industrial modules | U6 | planned (became F-186) |
| F-182 | Base tech tree | Traditional base nodes (e.g. Fighter; Extraction branch) | U10 | prototype (mock-up) |
| F-183 | Fan-out discovery | Select any node to push; over time there's a chance to unlock new nodes fanning out; speed depends on research modules owned | U10 | prototype (mock-up) |
| F-184 | Procedural modifier sets | Discovered nodes are modifier sets on a base tech: Heavy Fighter (+20% integrity, +25% mass); Military Fighter (+1 turret, +25% mass); Heavy Military Fighter (+1 turret, +25% mass, +20% health, -1 thruster); Military Bomber (many trade-offs) | U10 | prototype (mock-up) |
| F-185 | Stacking specialisation | Research a discovered node and later discoveries stack modifiers; clusters form where the player invests; mostly trade-offs, rare flat gains | U10, U17 | shown (U17 as blueprints) |
| F-186 | Blueprint research (implemented) | Tech Lab research task advances a chosen blueprint in the background and yields breakthroughs = derived blueprints | U17 | shown |
| F-187 | Derived blueprints and generations | Each derived blueprint is slightly better and differently specialised; research derivatives for 3rd-generation and beyond, in any direction | U17 | shown |
| F-188 | Blueprint attributes | Base blueprint attributes all 1; e.g. transfer unit: transfer rate vs compactness (heavier, bigger hardpoint, more space), power efficiency vs compactness | U17 | shown |
| F-189 | Grab-bag research vs certain production | Research outcomes deliberately uncertain; production deterministic | U17 | shown (design) |
| F-190 | Assembly chips | Second Tech Lab task mints chips from a blueprint; the Production Lab consumes a chip per production run | U17 | shown |
| F-191 | Physical technology | Blueprints and chips are cargo: tradeable, stealable (convoys), destroyable, so tech can be lost or gained | U17 | shown (design) / planned (theft) |
| F-192 | Invent new items | Players invent, produce and use entirely new procedural items | U17 | shown |
| F-193 | Visible tech tree view | Tree built from the blueprints you own | U17 | planned |
| F-194 | Tech tree widget | Node tree with hover tooltips listing modifiers; bugs: negatives not red, contradictory modifiers | U10 | prototype |
| F-195 | Blueprint UI polish | Icons, distinct names, "better than parent" display | U17 | planned |
| F-196 | Research colonies and projects | Research colony type; research as a project type | U15, U16, U18 | planned |

### 12.2 What was and was not shown

**What was and was not shown (research).** Shown and working (U17): the Tech Lab, background research tasks on a blueprint, breakthroughs producing derived blueprints with attribute trade-offs, multi-generation derivation, assembly-chip minting, Production Lab runs (30 and 60 transfer units) and use of the invented item. **Mock-up only** (U10): the node-graph tech tree with fan-out probability, research speed scaling with research modules, and the specific named modifier examples. **Not shown in any video:** the U17 system as a visible tree, research speed or cost numbers, authored prerequisites or categories, faction-exclusive tech, blueprint theft in action, research colonies, research projects, refining modules. The "procedural modifier tech tree" survives as the blueprint-derivation mechanic, not as a separate tree screen.

### 12.3 What the code contains

- **`ltheory-old`:** `Game/Task/Research.cpp` randomly "discovers" derived blueprints (`Item_Blueprint_Derived`), which matches the U17 demo. `Game/Object/TechLab.cpp`, `Game/Item/Blueprint.*` and `Game/Item/AssemblyChip.*` implement the Tech Lab, blueprints and chips. Production runs through `ProductionLab` and `Task/Produce`. The data-driven tree in `resource/gamedata/technology/{root,structural_engineering,subsystem_engineering}.txt` (name, advanceTime, level, children) is **not loaded by any code**, so the authored tech tree is a stub. There is a "TODO: Output of research?" comment.
- **`ltheory`:** **absent**. "ResearchControl" is mentioned only in `App/Todo.lua` and a comment in `MasterControl.lua`.
- **`libphx`:** nothing research-specific (as expected for an engine).

### 12.4 Required research model

- [Required] Technology IDs, categories, procedural names, descriptions.
- [Required] Prerequisites, costs, research time, research capacity, facility and resource requirements.
- [Required] Unlock effects, trade-offs, rarity, faction restrictions, parent/child relationships.
- [Required] Research queue, obsolescence and replacement rules.
- [Required] Validation against dead ends, loops, duplicates and runaway power growth. LT's answer to runaway growth was trade-off modifiers with rare flat gains (U10, U17).

### 12.5 Recommended technology categories

Exploration, scanning, mining, refining, trade, logistics, propulsion, weapons, defence, fleet command, industrial production, station construction, colony development, information systems, AI automation, faction-specific technology. [Required]

### 12.6 Required research UI

- [Required] A zoomable, pannable graph; node detail panel; search; filters.
- [Required] Research progress; cost and time display; facility/resource requirement display; prerequisite display.
- [Required] Unlock and trade-off previews; locked/available/queued/researched states; faction-exclusive and undiscovered states; a "why is this locked?" explanation.
- [Required] Links to affected ships, factories, modules, recipes, stations and fleets.
- [Required] Breadcrumbs, history, bookmarks, keyboard navigation, colour-safe accessibility.

---

## 13. Universe and procedural generation

### 13.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-197 | Procedural nebulae | Unique background nebula per system in every universe; repeated breakthroughs; HDR (emit light, shine through dust); "centerpiece" | U1, U5, U12, U16, U17 | shown |
| F-198 | Starfield | Rewritten "real stars" (U5); crisp stars; stars come out when leaving dust | U5, U8, U18 | shown |
| F-199 | Procedural asteroids | Highly detailed fully 3D (volumetric) procedural asteroid models with craters | U1, U10 | shown |
| F-200 | Infinite asteroid fields | Streamed hierarchically around the player with big scale variation (huge rocks next to debris) | U8 | shown (superseded by zones) |
| F-201 | Density modulation | Spatial density fields: pockets, whole-system fields, empty systems, zero density in lanes | U8 | shown |
| F-202 | Zone Tech | Bounded, mathematically shaped (spheres, rings) zones that auto-populate deterministically; named zones | U18, U19 | shown |
| F-203 | Ice fields | Ice asteroid fields with subsurface light transmission ("Ice 1.0") | U14 | shown |
| F-204 | Procedural planets | Rebuilt with atmospheric scattering, better surfaces, shading and clouds; Earth-like only; gas giants not done | U5, U16 | shown |
| F-205 | Planet surfaces | Stylised terrain engine (2 days), water, dot trees (U7); per-colony surfaces generated on landing (U18) | U7, U18 | prototype |
| F-206 | Scale | Double precision made planets 10x (~10,000x ship size) and distances 10x; realistic scale possible via mod; scale increased again U16 | U10, U16 | shown |
| F-207 | Planetary rings | Orbital asteroid ring (U17), ring zone you fly through (U18), ring shader (U21) | U17, U18, U21 | shown |
| F-208 | Universe hierarchy | Universe, clusters, regions, sub-regions, systems; recursive subdivision; irregular jump-hole graph (no grid, no jump-anywhere); Freelancer-like | U5, U11 | shown |
| F-209 | Infinite universe | Effectively infinite procedural universe | U5, U7, U20 | planned / partial |
| F-210 | System layout generation | Placement of planets, bases and fields; awkward early (objects too close); scriptable | U5, U19 | shown (rough) |
| F-211 | Name generators | Regions ("Arcax"), stations ("Garson"), planets ("Palala"), zones ("Vialo"), colonies ("Verad 9"), commodities, items; "funky", untouched since prototype | U3, U5, U15, U18, U21 | shown (weak) |
| F-212 | Procedural ships | "Lego/box ships", plating texture (U6), new meshing tech (U14), whole generator as an LTSL script with seeds and symmetry edits (U19); some lackluster | U1, U5, U6, U14, U19, U21 | shown |
| F-213 | Procedural stations | Radial-primitive placeholder, then meshing tech; scriptable | U8, U14, U19, U21 | prototype |
| F-214 | Spatial coherence | Generation correlated with distance: colonies on one planet similar, neighbouring systems similar, fading over several regions | U20 | shown |
| F-215 | Colony cultures | Generated culture profiles (the 7 traits) per colony | U20 | shown |
| F-216 | Dusty vs clear systems | Systems vary from dusty/colourful to "clear black" | U6, U17 | shown |
| F-217 | Resource distribution | Ore presence per asteroid; deposits with access points | U13, U14 | shown |
| F-218 | Procedural missions/contracts | Random missions (U3), replaced by real NPC-posted contracts (U12, U17) | U3, U12, U17 | shown |
| F-219 | Procedural UI layout | UIs laid out automatically from data ("what procedural is all about") | U11 | shown |
| F-220 | Procedural icons | ~12-line generator: random lines, horizontal symmetry, lit endpoints; 1000+ instantly | U19 | shown |
| F-221 | Procedural turrets and thrusters | Generators for turret and thruster meshes, iterated in the 3D model test bed | U21 | shown |
| F-222 | History-driven placement | Placement explained by simulated history | U7 | planned |

### 13.2 Confirmed procedural content (summary)

| Content | Video | Code |
|---|---|---|
| Star systems and universe hierarchy | U5, U11 | Old: Universe, Region, System (the generator loops over only 1 region). New: one system per seed |
| Asteroid and ice fields | U1, U8, U14, U18 | Old: marching-cubes volumes, Asteroid and Ice renderables. New: Surface Nets SDF asteroids, clustered fields |
| Planets, rings, atmospheres | U5, U7, U17, U21 | Old: present (`planetring.jsl`, scattering). New: planet shader present but spawning disabled |
| Nebulae and starfield | U1, U5, U12, U17 | Both: GPU-generated cubemap nebulae |
| Ships | U1-U21 | Old: SDF CSG, plate mesh, LTSL generator. New: ShapeLib fighters (capital generators exist, unused) |
| Stations | U8, U14 | Old: LTSL generator. New: `Gen.StationOld` in game |
| Weapons and items | U3, U21 | Old: procedural item types and derived blueprints. New: 4 hard-coded items |
| Commodities and ore types | U14-U16 | Old: per-universe ore types. New: one T1 ore |
| Factions and AI pilots | U15, U20, U21 | Old: grammar-named players, 7-trait AIs. New: none |
| Missions | U3, U12, U17 | Old: stub. New: absent |
| Technology | U10, U17 | Old: derived blueprints. New: absent |
| Names | U3-U21 | Old: grammar engine with 49 word lists. New: inline consonant/vowel generator (the grammar lists are carried over but unused) |

### 13.3 Required procedural systems

- [Required] Stable random seeds; deterministic system regeneration; persistent world modifications.
- [Required] Procedural naming; content rarity; faction identity rules; constraint-based generation; validation of generated content.
- [Required] Asset previews (cf. LT's 3D model test bed, U21); system streaming; cached generated assets (cf. `ltheory-old` caching ship types through its serializer, and the new code's baked `asteroid.bin` and `station.bin`).
- [Required] Simulation and rendering level of detail.
- [Required] Debug seed replay; regression tests for generated content.

---

## 14. User interface and user experience

### 14.1 Feature catalogue (every screen and widget shown)

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-223 | Start and new-game menus | Start menu; new game screen with 4 systems and a name field with generator | T | shown |
| F-224 | Settings and controls menu | Full control list, key rebinding, aim-assist toggle; graphics control panel on the U1 GUI engine; bloom and particles toggles | U1, U10, T | shown |
| F-225 | Top bar / player menu | Space bar opens the launcher bar for all interfaces (ship viewer, command...); "most important menu"; icons made to stand out in U20 | U2, U20, T | shown |
| F-226 | Command interface (RTS) | Holographic overview of the local system: selection, action menu ("Swiss army knife"), follow lead-time slider, attack; class icons (dots = fighters, "wheel"); custom colour; Tab toggles holographic/solid | U1, U2, U8, T | shown |
| F-227 | Command interface controls | Open with Y (undocked only); right-drag orbit; scroll zoom; WASD detaches the camera; double-click focus; drag-select owned units; F follow; Tab view toggle; right-click unit for orders | T | shown |
| F-228 | Command view planned extras | Sensor-range-limited visibility, icons for tiny units, formation editor | U1 | planned |
| F-229 | Asset panel | List of owned ships with dropdowns (status, activity, health, subsystems) and contextual orders (undock, follow, patrol, attack) | T | shown |
| F-230 | Leader-follower fleet groups | Select all but the leader, follow the leader, order the leader; any number of groups | T | shown |
| F-231 | Ship viewer v1 | 3D ship with hardpoint boxes, attached components | U2, T | shown |
| F-232 | Ship viewer v2 (folders) | Side-sliding folder layout (3D view, cargo, power, hardpoints); first-class scrollable hardpoint widgets with per-item config sub-widgets, compatibility highlighting, type labels ("IND") | U6 | shown |
| F-233 | Holographic ship viewer | Node-UI viewer with holographic hardpoints placed where they physically are on the ship; "your one true ship viewer" | U12, U13 | shown |
| F-234 | Inventory / cargo view | Scrollable expandable list (U2); node inventory (U11); cargo hold (U17); "transfer to" | U2, U6, U11, U17, U21 | shown |
| F-235 | Power distribution UI | Per-subsystem icon, slider, percentage readout; presets | U2, T | shown |
| F-236 | UI opacity | Glassy-to-solid opacity control | U2, U12 | shown |
| F-237 | UI colour picker | Choose the interface tint (blue default, "hot pink" possible) | U2, U8, U16 | shown |
| F-238 | Hangar view | Docked hangar-deck scene; view the station from any docked ship's point of view | U3 | shown |
| F-239 | Ships menu | Docked ship list; orange for sale, owned colour; repair button with cost; buy, pilot ship | U3, T | shown |
| F-240 | Job / mission board | List sorted by value with rewards and objectives; take mission; objectives display top left | U3, T | shown |
| F-241 | HUD v1 | Target markers; bottom-left weapon widgets (power, reload); bottom-right subsystem widgets (power, integrity); credit notifications | U3 | shown |
| F-242 | Radial subsystem rings | Per-subsystem rings: inner = health, second = power (glows when overpowered), third = cooldown; hull widget | T | shown |
| F-243 | Dev region viewer | Systems as nodes, jump-hole lines, sub-region partitions | U5 | prototype (dev tool) |
| F-244 | Prompts | Dock prompts for stations and planets (Shift+D), land prompt in colony zones | U7, U18 | shown |
| F-245 | UI visual refresh | Rounded edges, animated "electric" panel backdrop, rewritten crisper text, crisper icons, new font | U8 | shown |
| F-246 | Node-based UI engine | Hierarchical structural navigation built from the data-editor code; drives inventory, object menus and maps; "learn one interface, know them all" | U11, U12 | cut (evolved into F-260/F-262) |
| F-247 | 3D tilted UI | Whole interface tilts with depth | U11 | shown |
| F-248 | Radial layouts and input | Radial layout algorithm; mouse/keyboard primary, gamepad, joystick, axis navigation | U11, U12 | shown |
| F-249 | VR support | Oculus Rift | U11 | planned (never shown) |
| F-250 | Hierarchical universe map | One widget serves as universe/cluster/region/system map; child system maps visible from above | U11 | prototype |
| F-251 | System map | Nodes = objects; drill in to see 3D models; shows trade routes; later "system view map" debug tool showing AI on rails | U12, U15, U19, U21 | shown |
| F-252 | Split-screen panels | Replaces windowing; split repeatedly, any interface per panel; vertical (horizontal planned); enables drag-and-drop transfer | U12 | shown |
| F-253 | Categorical filtering | Automatic filter groups from node categories (ships, stations, asteroids, jump nodes; thrusters, scanner, generator, weapons) | U12 | shown |
| F-254 | Contract board screen | Lists NPC requests (still in code text, not English) | U12, U13 | prototype |
| F-255 | Cylindrical 3D HUD | HUD painted on a cylindrical surface for immersion, with a dedicated node-UI renderer | U13 | shown |
| F-256 | Cockpit mode | Blurred letterbox bars outside the HUD cylinder (optional) | U13 | shown |
| F-257 | Glass HUD | Etched-glass compositing on panels and overlays | U13 | shown |
| F-258 | Pop-out widgets | HUD widgets expand to full screen (ship display to full ship viewer) | U13 | shown |
| F-259 | Workspaces | Multiple desktops/modes (three dots at top), e.g. scanner workspace | U13 | shown |
| F-260 | World-space overlays | Holographic node UI projected in the world (NPC hardpoints/cargo, station contract board); node dragging (debug) | U13 | prototype |
| F-261 | HUD reticle and graph | Radial centre reticle; "gratuitous" graph widget | U13 | shown |
| F-262 | Mining HUD widgets | Prospecting drone readout; capacity %; boost capacitor battery icon; top speed | U14 | shown |
| F-263 | Interface glitch effect | Glitchy UI distortion (too strong) | U14 | shown (bug noted) |
| F-264 | Commodity market screen | Per-commodity, per-station graphs of last price, ask volume, bid volume over time | U15 | shown |
| F-265 | Recursive UI nesting | Interfaces nested inside each other "for fun" | U15 | prototype |
| F-266 | New nodal style | Restyled node UI ("really nailed the visual style") | U16 | shown |
| F-267 | Personal interface | Player hub ("Jean Luc Picard"): assets, projects, dev tools, inventory | U16 | shown |
| F-268 | Projects screen | Project list, metrics (earnings, duration), create project, allocate ships ("pretty shabby") | U16 | shown |
| F-269 | Commodities trading screen | Candles, EMAs, MACD, volume bars, order book, trade list, bid/ask entry | U16 | shown |
| F-270 | Research and production UIs | Tech Lab node with task buttons and blueprint picker; attribute display; production task; hardpoints panel | U17 | shown (unpolished) |
| F-271 | Missions and Database screens | Missions list with reward pool; Database of collected data entries for redemption | U17 | shown |
| F-272 | Zone indicator | Top-left current zone name ("unknown") | U18 | shown |
| F-273 | Loading screen | Placeholder hang during surface generation; test bed in U20 | U18, U20 | prototype |
| F-274 | Stack/layer UI | Each activated node opens a layer (ship, then cargo, then item); cycle layers, parents stay visible for context | U19 | cut (evolved into F-275) |
| F-275 | Unified LTSL UI engine | One UI engine mixing node ideas and standard layouts, 100% LTSL: windows, lists, menus, reticle; edges blend into the world | U20, U21 | shown |
| F-276 | Radial context menu | Right-click world objects for info and actions | U20, U21 | shown |
| F-277 | Object info window | Per-object info (components to become full widgets) | U20 | shown (partial) |
| F-278 | World-space markers | Object markers filtered by discovery | U20 | shown |
| F-279 | Colony info menu | Lists a planet's colonies with 7 trait meters | U20 | shown |
| F-280 | Curved UI projection | Screen-space curvature tunable by one number (flat, curved, cuspy); picking still works | U20 | shown |
| F-281 | Market UI (summary) | Category tabs; item list; detail panel (item information widget); search filter; shift-click split-screen comparison; buy panel with quantity slider and average price; transfer-to | U21 | shown |
| F-282 | Minimap | HUD minimap (pirate seen on it) | U21 | shown |
| F-283 | Sortable lists | Sorting in market and other lists | U21 | planned |
| F-284 | Reusable management UIs | Cargo, fleet and asset management UIs from the market components | U21 | planned |
| F-285 | UI-to-texture | Render a whole UI to a texture for display on 3D in-game objects | U21 | shown (capability) |
| F-286 | Road to Beta UI items | HUD customisation, object overview, cargo management, UI finalisation (text updates) | W | planned / text-only |
| F-287 | Tension slider | Debug slider driving dynamic music | U2 | prototype |

### 14.2 The UI rewrites

| Generation | Updates | Character |
|---|---|---|
| Panel GUI engine | U1-U3, U6, U8 | Menus, top bar, folder ship viewer, opacity and colour |
| Node-based UI | U11-U15 | Built from the data-editor code; hierarchical navigation; 3D tilt; split screens; categorical filters; cylindrical HUD |
| New nodal style | U16-U17 | Restyle; personal interface; projects and commodities screens |
| Stack/layer UI | U19 | One layer per activated node |
| Unified LTSL UI engine | U20-U21 | 100% script; radial menus; curved, composited projection; market UI |

**In code.** `ltheory-old` holds the last generation: the engine `UI/` framework plus 87 LTSL widget files (Window, Button, Slider, ScrollFrame, Tab, TextField, Grid, RadialList, Tooltip, ContextMenu, GameMenu, Settings, SplashScreen, Map, Scanner, Messages, Observatory, ObjectInfo, ItemInfo, Browser, the Market panels), and HUD widgets (Minimap, Miniscanner, Log, MenuBar, Targets, world-object badges, reticle). `Widget/Object/{Database,Sockets,Tasks}.lts` are 4-line stubs. `ltheory` has a separate retained-mode Lua toolkit (`script/UI`) with gamepad-friendly navigation groups, plus a HUD, a 623-line RTS command view and a single-system map. HmGui (libphx) appears only in demo apps.

### 14.3 Required flight and local-space UI

- [Required] Ship condition, energy, heat, fuel, speed, cargo, weapon status.
- [Required] Targeting reticle, navigation indicators, threat warnings.
- [Required] Scanner controls, mining-drone controls, docking controls.
- [Required] Communications, context-sensitive interactions, an alert feed.

### 14.4 Required strategic UI

- [Required] Universe, system, resource, trade, faction and conflict maps.
- [Required] Fleet command, asset ownership, station/factory, production-chain, market, research, diplomacy and mission screens.
- [Required] Event history and notifications.

### 14.5 UX controls

- [Required] Progressive disclosure; separate tactical, operational and strategic UI.
- [Required] Explain AI decisions, economic changes, faction conflict and supply-chain failures (cf. LT's planned "leader brain view", F-161).
- [Required] Preserve context during map and graph navigation (cf. LT's stack/layer UI, F-274).
- [Required] Keep visual effects from hiding tactical information (LT's glitch effect and god rays were judged too strong).
- [Required] Filters and alert prioritisation; support growth from one ship to a large organisation.

---

## 15. Graphics, rendering and the visual-effects catalogue

### 15.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-288 | Custom renderer and model pipeline | Engine overhaul for models and procedural model generation | U1 | shown |
| F-289 | Ship lighting, shadows, metal | Lighting, shadowing and metal shading; "pretty much where I want it" | U1 | shown |
| F-290 | Procedural plating texture | Reworked hull plating texture | U6 | shown |
| F-291 | HDR rendering | HDR scene; HDR-generated backgrounds that emit light | U6, U12 | shown |
| F-292 | Bloom | Bloom (U6), Skyrim-inspired soft bloom, toggleable (U10), improved (U13), new maths (U20) | U6, U10, U13, U20 | shown |
| F-293 | Metal reflections / PBR | Sun glints (U6); gamma-correct rendering and physically based metal BRDFs (U11); new metal model with HDR environment-map reflections (U12) | U6, U11, U12, U13 | shown |
| F-294 | Space dust | Dust particles and dust fields in asteroid fields | U1, U6 | shown |
| F-295 | God rays | Light shafts in dust (too aggressive) | U6 | shown |
| F-296 | Volumetric dust | Participating media visible from anywhere, arbitrary distributions, sun-lit, fast | U9 | shown |
| F-297 | Dust/zone transitions | Dust fades and stars appear when leaving a zone; the bright background shines through dust | U18, U20 | shown |
| F-298 | Dynamic lighting | Many moving lights (thrusters, pulses, muzzle flashes, cruise, explosions) lighting asteroids, ships and stations; rewritten for speed and light count | U9, U12, U13 | shown |
| F-299 | Lens flares | Generic, slightly anamorphic, geometry-occluded flares on all lights (U9); "JJ Abrams style" (U14) | U9, U14, U17 | shown |
| F-300 | Atmospheric scattering | Space-to-ground (U5), ground-to-space (U7) | U5, U7, U16 | shown |
| F-301 | Water rendering | First water on a planet surface | U7 | prototype |
| F-302 | Ice subsurface | Light diffusing through ice asteroids | U14 | shown |
| F-303 | Motion effects | Dust flecks and clouds streaming past to sell speed | U12 | shown |
| F-304 | Chromatic aberration | Subtle CA | U13 | shown |
| F-305 | SMAA | Anti-aliasing (credits Jimenez et al.) | U13 | shown |
| F-306 | Colour grading | "Advanced colour" artistic post-processing; toggleable; UI also graded | U17, U18, U20 | shown |
| F-307 | UI compositing | UI and world colours influence each other; windows blend at edges | U20 | shown |
| F-308 | Holographic rendering | Holographic look in the command view, ship viewer and world overlays | U1, U2, U12, U13 | shown |
| F-309 | Engine trails | Trails scaling with engine power (U2); trails following curved paths (U18) | U2, U18 | shown |
| F-310 | Impostors | Distant objects drawn as bitmap stand-ins; 60 fps with no visible change | U19 | shown |
| F-311 | Zone LOD / popping fix | Zones visible from afar; no asteroid popping | U10, U18, U19, U20 | planned |
| F-312 | Explosions | Shield-collapse plasma burst, hull explosion, improved explosions | U1, U11, U16, U20 | shown |
| F-313 | Weapon VFX | Pulse bolts, missile launch/trail/impact, railgun shot, large laser beams, muzzle flashes | U2, U3, U6, U9 | shown |
| F-314 | Shield hit VFX | Localised blue highlight following the hull-conforming shield | U1 | shown |
| F-315 | Mining VFX | Transfer beam, red stress shift, drone stick | U14, U16 | shown |
| F-316 | Warp VFX | Warp lane visual ("scratch work"); jump-hole anomaly; warp node orb; rail line | U5, U10, U21 | shown |
| F-317 | Procedural icons | Generated line icons with glowing endpoints | U19 | shown |
| F-318 | Native text effects | Text shadow; glow and outline planned | U21 | shown / planned |
| F-319 | Graphics pass | Asteroids, metal, planets, atmospheres, stars, nebulae ("darn near" final) | U16 | shown |

### 15.2 Visual-effects catalogue

| Effect type | What it is | Example / where shown | Status |
|---|---|---|---|
| Background nebula | Procedural, per-system HDR skybox nebula | U1, U5 (breakthrough), U12 (HDR), U17 (clear system breakthrough), U20 (bright) | shown |
| Starfield | Point stars in skybox | U5 rewrite, U18 stars come out | shown |
| Space dust (particles) | Dust motes in fields | U1, U6 | shown |
| Volumetric dust | Participating media, sun-lit, visible from outside | U9 (clear pocket looking back), U20 (bright parts through dust) | shown |
| God rays | Light shafts through dust | U6 (too strong) | shown |
| Motion flecks and clouds | Speed cues streaming past | U12 | shown |
| HDR + bloom | Glow and colour bleed | U6, U10 (on/off compare), U13, U20 | shown |
| Lens flares | Anamorphic, occluded, on every light | U9 (muzzle, sun, cruise), U14 (JJ Abrams), U17 | shown |
| Chromatic aberration | Edge colour fringing | U13 | shown |
| SMAA | Post-process anti-aliasing | U13 | shown |
| Colour grading | Artistic post colour | U17, U18, U20 (UI too) | shown |
| Dynamic lights | Moving point lights from thrusters, weapons, explosions | U9 (asteroid lit by thrusters), U12 (pulses on station), U13 (exploding ship "torch") | shown |
| PBR metal / env reflections | Gamma-correct BRDF, HDR env maps | U11, U12 | shown |
| Ship plating | Procedural hull texture | U6 | shown |
| Atmospheric scattering | Planet atmospheres both directions | U5, U7 | shown |
| Clouds | Planet cloud layer | U5 | shown |
| Water | Planet water | U7 | prototype |
| Terrain | Stylised planet surfaces | U7, U18 | prototype |
| Ice subsurface | Translucent ice asteroids | U14 | shown |
| Planetary rings | Ring shader and ring zones | U17, U18, U21 | shown |
| Engine trails | Power-scaled and path-following trails | U2, U18 | shown |
| Cruise glow | Orange/blue cruise thrusters | U9, T | shown |
| Pulse / laser / rail / missile | Projectile, beam and trail VFX | U2, U3, U6, U9 | shown |
| Shield hit and collapse | Local blue highlight; blue plasma burst | U1 | shown |
| Explosions | Hull explosions, improved later | U1, U11, U16, U20 | shown |
| Transfer beam | Mining beam that turns red under stress | U14, U16 | shown |
| Warp lane / jump hole | Lane visual; "strange anomaly" | U5, U10 | shown (lanes cut) |
| Warp node / rail | Energy orb; tube-less rail line | U21 | shown (entry animation missing) |
| Holographic render | Hologram look for UI and models | U1, U2, U12, U13 | shown |
| Cylindrical HUD / cockpit blur | HUD on cylinder, letterbox blur | U13 | shown |
| Glass HUD | Etched-glass compositing | U13 | shown |
| UI electric panel / glitch | Animated panel backdrop; glitch distortion | U8, U14 | shown |
| UI compositing / curvature | UI colour blending, curved projection | U20 | shown |
| Impostors | Billboard LOD for distant objects | U19 | shown |
| Procedural icons | Line-and-light icon generator | U19 | shown |
| Text shadow | Native text effect | U21 | shown |

### 15.3 Items the earlier document listed as unverified, resolved

| Earlier [Unverified] item | Resolution |
|---|---|
| Engine trails | **Confirmed**: U2 (power-scaled), U18 (curved paths). Code: old `Game/Object/Trail.cpp`, `thruster_trail` shader |
| Full ship explosion system | **Confirmed**: U1, U11, U16, U20. Code: old `Game/Object/Explosion.cpp`; new `Entities/Explosion.lua` |
| Shield system | **Confirmed**: U1 (hit-only, hull-conforming). Code: old `Game/Object/Shield.cpp`, `shield*` shaders |
| Bloom | **Confirmed**: U6, U10 (on/off comparison), U13, U20. Code: both |
| Ocean / water rendering | **Prototype**: U7 (first water on a planet surface) |
| Volumetric nebulae | **Corrected**: nebulae are procedural skybox cubemaps. The volumetric effect is **dust** (U9) |
| Motion blur | **Code only**: old `LTE/RenderPass/MotionBlur.cpp`. U12's "motion effects" are dust flecks and clouds streaming past, not blur |
| Depth of field | Not shown; not found in either audit |
| Atmospheric flight, weather | Not shown. Seamless atmospheric flight was cut (U7, U18, W) |
| Damage decals | Not shown; not found |
| VR | **Planned only** (Oculus Rift, U11). Old: OVR headers vendored but never referenced. libphx: `VR.cpp` entirely `#if 0` |
| Cinematic camera | Not shown. A free camera (U5) and dev free-fly exist |

### 15.4 Rendering in code

- **`ltheory-old`:** OpenGL via GLEW. Deferred G-buffer with global and local lighting passes, atmospheric scattering, planets and rings, impostors, starfield and nebula skybox. Post-processing: SMAA, SSAO, bloom, motion and radial blur, lens flares with a dirt texture, dust clouds. A CPU particle system with soft particles; thruster trails and glare. GPU generators for nebula cubemaps, planet surfaces, normal maps and irradiance maps.
- **`ltheory`:** deferred PBR with environment and irradiance maps, an alpha pass, a UI pass and supersampling. Filters: bloom, two tonemappers, colour grade, vignette, chromatic aberration, sharpen, radial blur, glitch, dither, fade. SMAA lookup textures are present. A path-tracer experiment (`ptracer.glsl`). No general particle system; effects are billboard shaders.
- **`libphx`:** GL 2.1 primitives only (render targets, textures, the shader loader with `#include` and `#autovar`, immediate-mode `Draw_*`, procedural cubemap generation, irradiance convolution). The pipeline itself is game code.

### 15.5 Required effects for comparable software

- **Space and navigation [Required]:** starfield; stellar and planet lighting; planet shadows; depth cues in asteroid fields; engine and manoeuvring effects; docking effects; station traffic; travel/jump effects.
- **Mining [Required]:** scan pulses; spectral overlays; resource highlighting; mining-beam effects; extraction particles; drone launch and impact effects; drone telemetry indicators; deposit/vein visualisation.
- **Combat [Required]:** energy beams; projectiles; weapon charging and cooldown effects; impacts; explosions; debris; shield effects; target-lock effects; fleet-order effects; LOD effects for large battles.
- **Industry and economy [Required]:** cargo transfer; factory activity; construction progress; a faction colour and marking system; blockade, raid, distress and war indicators on strategic maps.

---

## 16. Audio

### 16.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-320 | Weapon SFX | First sound effects from a contracted sound designer: pulse "pew pew", missiles, railgun | U2, U3 | shown |
| F-321 | Dynamic tension music | One continuously mixed track driven by a "tension" value (calm, approaching, combat), modelled on Freelancer | U2 | prototype |
| F-322 | Composer soundtrack | Preview track by "Mr. France" (eonsounds.com); earlier music by Josh | U8 | shown |
| F-323 | Scanner audio | Scanner signal has an audible strength component | U13 | shown |
| F-324 | Zone ambience | Ice-field ambience and music cue matched to visuals | U14 | shown |
| F-325 | Mining audio | Transfer-beam sound rises with mass flow; usable for blind mining | U14, U15 | shown |
| F-326 | Explosion audio | Explosion sounds (small-ship sound wrongly used for big ships) | U16 | shown (bug) |

Beyond these, the videos have music only in intros and outros. No spatial audio, UI sounds or voice were discussed.

### 16.2 Audio in code

- **`ltheory-old`:** `Module/SoundEngine` with FMOD Ex and a Null fallback for 3D SFX; `Module/MusicEngine` for adaptive music through FMOD Designer event banks (`resource/music/*.fev/.fsb`, with `MusicEngine/LtheoryTest01.h`). This matches the U2 tension-driven music. `Game/Object/SoundEmitter.cpp` provides positional emitters. About 130 sound effects. An experimental DSP synthesis graph (`src/liblt/Audio/*`: envelopes, filters, signal generators) and an unbuilt `soundstudio` harness. These never appeared in the videos.
- **`ltheory`:** initialises audio and sets the 3D listener only. All playback is in the `FMODTest` demo, which uses copyrighted Star Wars clips (see section 5.5).
- **`libphx`:** an FMOD **Core** wrapper (3D listener, doppler and rolloff, sound load and async load, play/pause, 3D position, pan, pitch, volume). FMOD Studio is linked but unused; no buses, mixer or DSP abstraction.

### 16.3 Required audio components

- [Required] Engine, manoeuvring, scanner, mining-beam, mining-drone, weapon, impact and explosion audio.
- [Required] Docking, station, factory and trade-hub ambience.
- [Required] UI confirmation, warning and error sounds.
- [Required] Spatial audio, distance attenuation, volume controls, accessibility-oriented alerts.
- [Required] Gameplay-informative audio, as LT did: scanner signal strength (U13) and a mining beam whose sound rises with mass flow, used for blind mining (U14).
- [Required] An audio-asset pipeline with licences you control (neither public repo's audio is safely reusable).

---

## 17. Engine, tools and backend

### 17.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-327 | Custom engine | Built from scratch (C++ era LTE; later C/Lua/OpenGL per W) | U1, W | shown |
| F-328 | Physics and collision engine | In-house; solid surfaces; struggles with massive objects (caps scale) | U1, U20 | shown |
| F-329 | GUI engine | New GUI engine for menus | U1 | shown |
| F-330 | Linux port | Easier than expected; engine multiplatform; Linux later the native dev client | U6, U16 | shown |
| F-331 | Mac port | Harder than Linux; all three OSes (stretch goal met) | U7 | shown |
| F-332 | Double precision | All positions in doubles (50% in U8, done in U10) enabling large scale | U8, U10 | shown |
| F-333 | Dynamic entity loading | Procedural objects computed and loaded on the fly near the player, hierarchically | U8 | shown |
| F-334 | Abstract simulation tools | Multi-level abstraction framework | U7, U8 | shown (internal) |
| F-335 | Dynamic light framework | Arbitrary moving lights; rewritten for many lights | U9, U12 | shown |
| F-336 | Reflective data model | All engine data introspectable and editable generically | U10 | shown |
| F-337 | Text renderer | Rewritten crisp text (U8); native engine text replacing an external library: faster, effects, Unicode/UTF-8 (Japanese, Chinese, Russian, Korean shown) | U8, U21 | shown |
| F-338 | Node-UI renderer | Dedicated renderer drawing much more UI faster | U13 | shown |
| F-339 | Performance figures | ~80 fps on GTX 560, ~30 fps on Intel HD 4000 (U13); 60 fps with impostors (U19); lag in big battles (U3); stutter with 128+ NPCs (U21) | U3, U13, U19, U21 | shown |
| F-340 | Collision disabled at scale | Turned off for 100+ NPC systems (clipping, miner lodged in asteroid) | U15, U16, U18 | shown (limitation) |
| F-341 | LTSL compiler and runtime | Data language turned into a functional language with compiler and execution component (U18), named LTSL (U19) | U18, U19 | shown |
| F-342 | Hot reload | Edit script, game recompiles and regenerates on the fly | U19, U20 | shown |
| F-343 | Soft compile failure | Broken code is isolated and cut out; dependents stop, the rest keeps running; errors to console | U20 | shown |
| F-344 | Launcher architecture | The executable is a lightweight launcher running an LTSL script that builds the app by calling engine functions selectively | U20 | shown |
| F-345 | Test beds | Isolated LTSL apps: UI, AI combat stats, HUD, Mandelbrot benchmark, drawing app, loading screen (U20); market, warp rail (128 NPCs), 3D model (U21) | U20, U21 | shown |
| F-346 | 3D model test bed | One object in its own world, rerunning a generator with parameters (ships, stations, planets, asteroids, turrets, thrusters); ring shader iterated about once a minute | U21 | shown |
| F-347 | Dev tools menu | Steal asset, transfer to ship, jump to object | U16, U20 | prototype |
| F-348 | Capture and recording | New video capture technique on Linux; time speed-up freezes during recording | U16, U17 | shown |
| F-349 | Optimisation pass | Rigorous pass promised before release | U3, U21 | planned |

### 17.2 Confirmed architecture

- [Code: Old] C++ with LTE and LTSL, built into one shared library (`lt.dll`) plus `launch.exe`. Flags `/fp:fast /arch:SSE2`.
- [Code: New] C and Lua. The main executable launches Lua applications.
- [Engine] libphx is mostly C and exposes everything through LuaJIT FFI.
- [Code: Old/New] Both build with CMake and Python helpers. Both are incomplete; libphx is abandoned and mostly undocumented.
- [Code: Old] Reflection-driven binary serializer (`LTE/Serializer`, versioned), packages and archives, hash-grid and uniform-grid spatial partitions, sphere trees, a KD-tree, thread/lock/job wrappers, a scheduler module, a scoped hierarchical frame profiler (`AUTO_FRAME`, used in about 45 files), composable button bindings, a settings registry.
- [Engine] libphx: SDL2 window and input with a unified button enum and the SDL controller DB; Bullet rigid bodies, compounds, triggers and ray/shape casts; a mesh BSP for exact ray and sphere queries; KD-tree, octree, box tree and hash grid (unused by the game); a seeded 64-bit RNG with many distributions and child RNGs; `Bytes` buffers with LZ4; a profiler writing Chrome trace JSON; MIDI input; raw UDP/TCP sockets; thin thread wrappers (the game is single-threaded in practice).

### 17.3 Required runtime systems

- [Required] An entity/component model or equivalent; spatial indexing; scene management.
- [Required] Asset loading; resource lifetime management; input, audio and rendering abstractions.
- [Required] A configuration system; error logging; crash diagnostics; a cross-platform build pipeline.

### 17.4 Required simulation systems

- [Required] Persistent world state and **save/load**: absent in every LT code base, so this must be designed in from the start.
- [Required] A simulation clock and event system.
- [Required] High-fidelity simulation near the player and aggregate simulation of the distant world (cf. LT's abstract simulation, F-136).
- [Required] A deterministic test/replay mode; save migration; seed-integrity and world-state integrity checks.

### 17.5 Required data systems

- [Required] Definitions for commodities, ships, modules, weapons, stations, factions, technologies, missions and AI behaviour.
- [Required] Schema versioning; mod dependency handling; mod load order; data validation; conflict detection; diagnostics.

### 17.6 Required performance systems

- [Required] Spatial partitioning; rendering and simulation LOD; object pooling; async loading; a job system or safe multithreading.
- [Required] CPU, GPU, memory, AI and simulation profiling.
- [Required] Stress tests: dense asteroid fields, trade hubs, fleet battles. LT's measured failures were lag in fleet battles (U3), collision turned off at 100+ NPCs (U15), and stutter at 128+ NPCs (U21).

---

## 18. Data editor, LTSL and modding

### 18.1 Feature catalogue

| ID | Feature | Description | Shown in | Status |
|---|---|---|---|---|
| F-350 | LT Data Editor | Generic visual graph editor that can inspect any engine data (ship, hardpoints, thruster, inventory, weapon type, munition branch) | U10 | prototype (days old, crashed on camera) |
| F-351 | No special-case code | Works on all data generically (no "set munition colour" function) | U10 | shown |
| F-352 | Live editing | Change values in real time; e.g. munition colour XYZ mapped to RGB, green to red to orange; projectiles and their lights update instantly | U10 | shown |
| F-353 | Automatic layouts | Runtime-switchable graph layouts: recursive octagons, plant ("data tree"), fern, rectilinear (best for reading types) | U10 | shown |
| F-354 | Shape = type | Data types are recognisable by their sub-graph shape | U10 | shown |
| F-355 | Navigation | Mouse or keyboard navigation of the graph | U10 | shown |
| F-356 | Debug use | Replaces console prints and text dumps for development | U10 | shown |
| F-357 | Data inspection in game | Used to show ore composition (veridium content); demo failed | U15 | prototype |
| F-358 | Editor-to-UI lineage | Node UI engine shares the editor's code ("the exact same code") | U11 | shown |
| F-359 | Data-level modding | Externalised data for generation (system layouts, skyboxes, scales); reversal of "basically not gonna happen" | U10 | planned |
| F-360 | Realistic-scale mod | Community could mod realistic scale | U10 | planned |
| F-361 | Script modding | External scripts call into the engine and manipulate objects; LT moddable "to quite some extent" | U18 | shown |
| F-362 | LTSL language | Lisp-like functional semantics, Python-like block syntax; generating scripts and update scripts (logic for ships, colonies, stations, AI) | U19 | shown |
| F-363 | Generator scripts | Entire ship algorithm in one script; stations, asteroids, system layouts and zone shapes scriptable ("every facet") | U19 | shown / planned (zones) |
| F-364 | Compiler errors | Type errors reported (e.g. adding 3D positions to a 2D line); console now, in-game messages planned | U19, U20 | shown / planned |
| F-365 | In-game script editor | Possible, not promised (W lists one, but no video shows it) | U19 | planned |
| F-366 | Moddable UI | Whole UI in LTSL; custom UI nodes; modders can make their own scanners and widgets | U19, U20 | shown |
| F-367 | Unicode and fonts for mods | LTSL loads Unicode fonts and UTF-8 strings (localisation, symbols) | U21 | shown |
| F-368 | UI-on-objects for mods | UI-to-texture intended as a modding avenue | U21 | shown (capability) |
| F-369 | Key rebinding | All controls rebindable | T | shown |
| F-370 | Mod sharing | Modders share modifications; forums for LTSL questions | U19 | planned |

**What the videos do *not* show for the editor and modding:** undo/redo, saving edits to disk, schema validation, search, dependency views, mod packaging or load order. Scripts were edited in Josh's own external code editor. The editor's role as a modding front end gave way to LTSL from U18 on.

### 18.2 The LT Data Editor (U10)

What the video shows: a generic, reflective graph editor, days old and crashing on camera. It can open any engine object (a ship, its hardpoints, a thruster, the inventory, a weapon type, a munition branch) without special-case code. Values change live; for example, the munition's colour vector is mapped to RGB and the projectiles and their lights recolour instantly. Layout algorithms can be switched at runtime (recursive octagons, "plant" data tree, fern, rectilinear), so a data type is recognisable by the shape of its sub-graph. Navigation works by mouse or keyboard. Josh uses it to replace console prints. A month later the node UI engine was built from "the exact same code" (U11). In U15 it was used to show ore composition, but the demo failed.

**What the code shows: the editor is in none of the three repositories.**

- `ltheory-old` has the reflection substrate it would have sat on (`AutoClass` and reflection macros, `Strukt/`, `CodeGen/`), and in-game *tool* widgets written in LTSL: a procedural **model editor** with X/Y/Z symmetry toggles and preview (`Widget/ModelEditor*.lts`, `App/model.lts`), a read-only line-numbered **text viewer** (`Widget/TextEditor.lts`), an **image editor/previewer** that saves through `Data_SaveTo`, a font previewer, a file browser, and a **developer panel** (`Widget/DevPanel*.lts`). A search for the editor's layout names (octagon, fern, rectilinear) found nothing. Game-data editing in practice meant editing `.lts` and `.txt` files.
- `ltheory` has none. The closest pieces are the read-only `DebugInspector`, `Settings` sliders in the debug window (not saved), and the standalone `GenTex2D` app. `Config.game.timeScaleShipEditor` is a leftover with no code behind it.
- `libphx` has no editor or authoring tool. Its only tool is the FFI generator.

### 18.3 LTSL (Limit Theory Scripting Language)

| Aspect | Videos (U18-U21) | Code (`ltheory-old`) |
|---|---|---|
| Origin | U18: the data language "became a programming language", with a compiler and an execution component; named LTSL in U19 | `LTE/{LTSL,Script,ScriptFunction,ScriptType,Expression,Function,Type,Tokenizer,StackFrame,DataStack}.*` |
| Syntax and semantics | Lisp-like, functional semantics with Python-like block syntax | Indentation-based and statically typed. Compiles to expression trees (25 node types including If, For, While, Switch and DynamicDispatch), with methods and `address`/`deref`/`ref` |
| Uses | Generator scripts (the whole ship algorithm; stations, asteroids, system layouts, zone shapes) and update scripts (logic for ships, colonies, stations, AI); the whole UI (U20); the application itself (launcher, U20) | 141 `.lts` files: apps, UI widgets, `Item/ShipType/Generate.lts`, `Item/StationType/Generate.lts`, object behaviours (`Object/Colony.lts`, `WarpNode.lts`, `WarpRail.lts`) |
| Engine binding | Scripts call engine functions | `*/ScriptAPI/*.cpp` bindings via the `AutoClass`/`FreeFunction` macros, with doc strings; generated by `script/meta/*.py` |
| Hot reload and soft failure | Edit, recompile and regenerate live; broken code is cut out and the rest keeps running; errors go to the console (U19, U20) | Not confirmed by the audit. The runtime is there, but the reload path was not traced |
| Errors | Type errors reported (e.g. a 3D position added to a 2D line) | Typed compiler confirmed |
| Script editor | Scripts edited in Josh's external editor. An in-game editor was "possible, not promised" (U19) | Only a read-only text viewer |

LTSL **does not exist in the new code base**. `ltheory` uses Lua over LuaJIT FFI; the README says LT and all mods were to be "many Lua scripts".

### 18.4 Modding, per code base

| Aspect | ltheory-old | ltheory |
|---|---|---|
| Intended model (README) | The whole game and all mods as LTSL scripts on top of engine functions | The whole game and all mods as Lua scripts calling engine functions |
| Data formats | `.lts`, `.jsl`, paren-delimited key=value `.txt` trees under `resource/gamedata` (grammar lists, technology, `item/thruster.txt`) | Lua tables; `Game/Content.lua` is explicitly a "placeholder for ... mod format" |
| Resource resolution | `Location_*` helpers, `Package`/`Archive` readers | libphx `Resource_AddPath` search paths |
| Mod manager, manifest, load order | Absent | Absent |

### 18.5 Data-editor functional requirements (for comparable software)

**Editable data categories [Required]:** technologies, prerequisites and unlock effects; ships and ship modules; weapons; stations and station modules; commodities; production recipes; factions; AI agent templates; mission templates; procedural-generation rules; research and economic rules; UI layouts; visual-effect definitions; audio definitions; localisation strings.

**Operations [Required]:** create, inspect, modify, duplicate, archive and delete records; search, filter, tag and saved queries; dependency and reverse-dependency views; detection of circular references and orphaned records; schema, range and constraint validation; version comparison; undo/redo; version history; import/export; mod packaging; sandbox testing of edits; previews of generated assets; simulation previews before committing changes.

**Views [Required]:** node graph; property inspector; table/list; hierarchy/tree; asset browser; preview pane; validation/error pane; dependency map; change history; script console or diagnostics.

**UI semantics [Required]:** node shape identifies data type (LT did this in U10); node colour identifies category or state; edge style identifies relationship type; tooltips explain briefly and detail panels list every property; graph layout is stable enough for spatial memory; pan, zoom, search, filter, bookmarks and navigation history; edits show affected dependencies before confirmation; developer-only tools are kept apart from player UI.

None of the operations beyond live value editing, automatic layout and navigation were shown in any video. Undo/redo, saving, validation, search, dependency views and mod packaging are all absent from the videos and the code.

---

## 19. Feature provenance matrix

One row per feature group, or per F-ID where it matters. Paths are relative to each repository root. In the old-repo column, `G/` = `src/liblt/Game/`, `C/` = `src/liblt/Component/`, `L/` = `src/liblt/LTE/`, `S/` = `resource/script/`. In the new-repo column, `s/` = `script/`. "kw" means the cell rests on a keyword search done during this merge, not on the full audit. "n/a" means the layer is not applicable.

### 19.1 Gameplay, flight and combat

| Feature | Shown in videos | ltheory-old | ltheory (new) | Engine | Notes |
|---|---|---|---|---|---|
| Third-person flight, thrusters (F-001, F-002) | U1-U21, T | present: `C/{MotionControl,Pilotable,Motion,Orientation}`, `G/Object/Thruster.cpp`, `S/App/handling.lts` | present: `s/Game/Components/{RigidBody,ThrustController}.lua`, `Entities/Thruster.lua` | LTE: custom physics module. libphx: Bullet 2.87 rigid bodies | |
| Cruise mode (F-004) | U3, U9, U12, T | partial (kw): cruise references in `G/Object/Thruster.cpp`, `G/Task/Goto.cpp`, `G/Widget/HUD.cpp` | partial (kw): references in `ThrustController.lua`, `ShipBindings.lua`, `Controls/HUD.lua` | n/a | Depth not audited |
| Free and locked camera (F-005, F-006) | U5 | not audited | present: `s/Util/Camera{,Chase,Orbit}.lua` | n/a | Free-aim turret behaviour unconfirmed |
| Rigid-body collision (F-007) | U1, U15-U20 | present: `Module/PhysicsEngine.cpp` (155 lines), `C/Collidable` | present: Bullet via `RigidBody.lua` | libphx Physics (float only, shape cache leaks) | Disabled at scale in videos |
| Docking and undocking (F-009, F-010) | U3, U9, U12, U16, T | present: `C/{Dockable,Interior}`, `G/Task/Dock.cpp` | partial: `Components/Dockable.lua`, `Actions/{DockAt,Undock}.lua`; docked UI is only an undock prompt | libphx Trigger | |
| Ship ownership and switching (F-013, F-014) | U3, U16, T | partial: `G/Player`, `C/Account`, asset lists (UI path not traced) | partial: `Components/Assets.lua`, `Entities/Player.lua` | n/a | |
| Power distribution, boost capacitor (F-018, F-019) | U2, U3, U14, T | partial: `G/Object/PowerGenerator.cpp`, boost references (kw) | partial: `Components/Capacitor.lua`; energy grid unchecked in `App/Todo.lua` | n/a | No slider UI found in either |
| Dynamic mass, cargo by mass (F-020, F-094) | U10, U14 | present: `G/Attribute/Mass.h`, `C/Cargo` | present: mass-based `Components/Inventory.lua` | n/a | |
| Hull and subsystem damage, repair (F-021, F-022) | U3, T | present: `C/{Integrity,Damager,Explodable}`, `G/Event/Damage.cpp` | present: `Components/Health.lua`, `Explodable.lua` | n/a | Repair service not traced |
| Hardpoints and fitting (F-023, F-024) | U2, U3, U6, U12, T | partial: `C/{Sockets,Attachable,Pluggable}`; `S/Widget/Object/Sockets.lts` is a 4-line stub | partial: `Components/Sockets.lua` (thrusters, turrets) | libphx compound rigid-body attach | No drag-and-drop fitting UI found |
| Fire groups (F-025) | U3, W | not found | absent (unchecked in `App/Todo.lua`) | n/a | |
| Warp lanes (F-026) | U5, U8, U10 (cut) | not found | absent | n/a | Replaced by rails |
| Warp nodes and rails, lane allocation (F-027, F-028, F-159, F-160) | U21 | present: `G/Object/{WarpNode,WarpRail}.cpp`, `S/Object/{WarpNode,WarpRail}.lts`, `S/App/rails.lts`, `warprail` shader | absent | n/a | Lane-allocation controller not traced to a file |
| Jump holes / wormholes (F-029) | U5, U8, U20 | present: `G/Object/Wormhole.cpp`, `wormhole` shader | absent (single system) | n/a | |
| Jump gates, capital jump drives (F-030, F-031) | planned U5 | not found | absent | n/a | |
| Planet docking, colony landing (F-032, F-034) | U7, U18 | partial: `G/Object/Colony.cpp`, `S/Object/Colony.lts`, `S/App/colony.lts`, `S/App/loading.lts` | absent | n/a | Surface view generation not traced |
| Time acceleration (F-036) | U12-U17 (dev) | not audited | present: debug flag | n/a | |
| Discovery gating, targeting tracker (F-039, F-040) | U20 | partial: `C/Detectable`, `S/Widget/HUD/Targets`, `S/Widget/Reticle/Default.lts` | partial: target brackets and lock indicator in `Controls/HUD.lua`; no discovery | n/a | |
| Pulse, beam, rail, missiles, turrets (F-049 to F-055) | U1-U6, U9, U11, T | present: `G/Object/{Weapon,Turret,Missile,Pulse,Rail,Payload}.cpp`, `G/Beam.*`, `G/Item/{WeaponType,TurretType}.cpp` | partial: `Entities/{Turret,Pulse}.lua` (pulses as C structs); `beam` shader only; no missiles or munitions | libphx `Physics_RayCast`, BSP | |
| Hit-only conforming shields (F-056 to F-060) | U1, T | present: `G/Object/Shield.cpp`, `G/Item/ShieldType.cpp`, `shield*` shaders | absent | n/a | |
| Lead indicator, aim assist (F-064, F-065) | T | not traced | partial: turret lead via `Math.Impact` | libphx `Math.Impact` | |
| Friend/foe colours, hostility (F-066, F-173) | U3, U17, T | partial: `C/Opinion.h` | partial: `Components/Dispositions.lua` (HUD colour) | n/a | |
| AI kiting, dogfighting, combat test bed (F-069, F-072, F-075) | U3, U18, U20 | present: `S/App/{war,dogfight}.lts`, `G/Task/Destroy.cpp` | partial: `Actions/Attack.lua` (randomised orbit-strafing) | n/a | |
| Fleet battles (F-076) | U3, T | present: `war` app | partial: 100 escorts in `App/LTheory.lua` | n/a | PAX 2018: 2,000+ ships (W) |

### 19.2 Economy, industry and research

| Feature | Shown in videos | ltheory-old | ltheory (new) | Engine | Notes |
|---|---|---|---|---|---|
| Credits, currency (F-077) | U3, T | present: `C/Account`; universal currency in `G/Universe.cpp` | present: `Components/Credits.lua` | n/a | |
| Equipment market v1, ship dealer (F-078, F-079) | U3, T | partial: market UI handles all items | absent | n/a | |
| Bid/ask order book, emergent prices, player orders (F-085 to F-091) | U15, U16, U21 | present: `C/Market`, `G/Order`, `MarketEMA` (27-deep EMA), trade history | partial / broken: `Components/Trader.lua` works; `Components/Market.lua` never matches (`marketData` vs `data`) | n/a | The most developed old system |
| Population demand (F-084) | U15, U16 | partial: colony population and workers (`G/Item/{ColonyType,Worker}.cpp`) | absent | n/a | A demand curve was not confirmed |
| AI pricing (F-093) | U16 | present: `G/Project.cpp` prices from EMA plus a variance term | partial: `computeTrade` profit estimate | n/a | |
| Commodity charts: candles, EMA, MACD (F-264, F-269) | U15, U16 | partial: EMA history and market panels (`S/Widget/Market/*`); no candle or MACD strings found (kw) | absent (generic `UI/Graph`) | n/a | Uncertain whether the charts survive |
| Storage locker (F-043) | U21 | partial: `C/Storage` | absent | n/a | |
| Contract board, missions v1/v2, data proofs (F-081, F-097, F-098, F-175 to F-177) | U3, U12, U17, T | stub: `G/Mission.cpp`, `C/{Missions,MissionBoard}.h` (about 120 lines) | absent | n/a | Video systems are richer than the released code |
| Ore asteroids, deposits, access points (F-103, F-104) | U9, U13, U14 | present: `G/Object/Asteroid.cpp`, `C/{Mineable,Resources}`, `G/Item/OreType.cpp`, `G/Event/Deposit.cpp` | partial: `Components/Yield.lua` (one T1 ore) | LTE `Volume/` marching cubes; libphx SDF | |
| Transfer-unit beam mining (F-105 to F-108) | U12, U14, U16 | present: `G/Object/TransferUnit.cpp`, `G/Task/{Mine,Drill}.cpp`, `transferbeam.jsl` | partial: AI `Actions/MineAt.lua`, `Jobs/Mine.lua`; no player mining UI | n/a | |
| Drone bay, prospecting drones (F-110, F-111) | U14 | present: `G/Object/DroneBay.cpp`, `G/Object/Drone/Prospecting.cpp` | absent | n/a | |
| Scanner 1.0 / 2.0 / 3.0 (F-113 to F-118) | U13, U14, U19, U20 | present: `G/Object/Scanner.cpp`, `S/Widget/Scanner.lts`, `S/Widget/HUD/Miniscanner` | absent | LTE spatial signatures | Which scanner version the code matches is unverified (probably 3.0) |
| Construction module, loop production (F-119 to F-122) | U6 | partial: `G/Task/{Produce,Spawn}.cpp`, `G/Object/Drone/Construction.cpp` (52 lines) | partial: `Components/Factory.lua`; trade orders disabled; 2 recipes | n/a | |
| Tech Lab, Production Lab (F-124, F-125) | U17 | present: `G/Object/{TechLab,ProductionLab}.cpp`, `G/Item/ProductionLabType.cpp` | absent | n/a | |
| Stations, asteroid bases, services (F-127 to F-129) | U3, U8, U12, U14, U21, T | present: `G/Object/Station.cpp`, `S/Item/StationType/Generate.lts` | partial: `Entities/Station.lua` uses `Gen.StationOld` | libphx `BoxMesh`, GPU AO | |
| Colonies, types and transforms (F-130, F-131) | U18, U20 | partial: `G/Object/Colony.cpp`, `G/Item/ColonyType.cpp` | absent | n/a | |
| Player-built infrastructure (F-132) | planned U15 | thin: construction drone | absent (`shipBuildTime` unused) | n/a | |
| Procedural items by value (F-180) | U3, U21, T | present: procedural item types | stub: 4 items in `Game/Item.lua` | n/a | |
| Tech-tree mock-up, fan-out discovery (F-182 to F-185, F-194) | U10 | stub: `resource/gamedata/technology/*.txt` exists but is never loaded | absent | n/a | |
| Blueprint research, derived blueprints, generations (F-186 to F-188) | U17 | partial: `G/Task/Research.cpp` (`Item_Blueprint_Derived`), `G/Item/Blueprint.*` | absent | n/a | |
| Assembly chips, tech licensing (F-190, F-099) | U17 | present: `G/Item/AssemblyChip.*`, `G/Task/Mint.cpp` | absent | n/a | |

### 19.3 AI, factions and simulation

| Feature | Shown in videos | ltheory-old | ltheory (new) | Engine | Notes |
|---|---|---|---|---|---|
| Escort, wingman, patrol (F-134, F-135) | U1, T | present: `G/Task/{Patrol,Goto}.cpp` | present: `Actions/Escort.lua` with formation offsets | n/a | |
| Abstract simulation, multi-level LOD (F-136, F-137) | U7, U8 | partial (uncertain): `G/Task/LOD.cpp` exists; no aggregate simulator confirmed | absent | n/a | |
| History generation (F-138) | planned | absent | absent | n/a | |
| Master planner, units of functionality (F-140, F-141) | U9 | stub: `G/Action/Mine.cpp`, `G/Condition/Nearby.cpp` (one or a few entries) | partial: an older "flow pressure" planner, overridden by `if true` | n/a | Task-based AI replaced general planning |
| Fluid-goal AI, task system (F-143) | U11 | present: 19 pooled task types, `C/Tasks`, `Module/Scheduler.cpp` | partial: action stack, `Jobs/{Mine,Transport}.lua` | n/a | |
| Delegation, AI-posted missions (F-145, F-154) | U12, U17 | stub: mission holders only | absent | n/a | |
| Profit-driven professions, miners/traders/pirates (F-149, F-152) | U15, U16 | present: `G/Task/{Mine,Transport,Pirate,Buy,Sell}.cpp` | partial: `Actions/Think.lua` best-payout sampling; traders off in the main app | n/a | |
| Faction leaders, projects, P/L, meta-projects (F-150, F-151, F-153, F-165 to F-169) | U15-U17 | partial: `G/Project.cpp`, `C/Projects`, `G/Task/Manage.cpp` (198 lines) | stub: capital expenditure empty | n/a | |
| Personality traits, colony cultures, birth at colonies (F-156, F-157, F-215) | U20 | partial: `src/liblt/AI/Traits.h` (7 traits; `XSIZE 6` mismatch), `S/Object/Colony.lts` | absent | n/a | |
| Population scale (F-147) | U11-U21; W 2,000+ | n/a | 100 escorts in the main app | n/a | |
| Factions, opinions, hostility (F-164, F-170, F-172, F-173) | U15-U17, U21 | partial / thin: `G/Player`, `C/Opinion.h`, faction grammar | stub: `Components/Dispositions.lua`, `Owner.lua` | n/a | |
| Treaties, alliances, territory (F-179) | **none** | absent | absent | n/a | Only in prose |
| Security, bounty hunters, retaliation (F-171, F-074) | planned or disabled | not found (kw "Bounty": 0) | absent | n/a | |
| NLP event messages and logs | not in videos | partial / experimental: `AI/NLP*`, `G/NLP.cpp`, `G/Messages.cpp`, `S/Widget/HUD/Log.lts` | absent | n/a | Code-only feature |
| Artificial-life and neural-net experiments | not in videos | experimental: `S/App/{Life/*,brain,hnn}.lts` | absent | n/a | Code-only |

### 19.4 Universe, procedural generation and graphics

| Feature | Shown in videos | ltheory-old | ltheory (new) | Engine | Notes |
|---|---|---|---|---|---|
| Universe hierarchy, regions, systems (F-208, F-209) | U5, U11 | partial: `G/Universe.cpp` (loops over 1 region), `G/Object/{Region,System}.cpp` | partial: one system per seed (`Entities/System.lua`, `Gen/SystemGenerator.lua`) | n/a | |
| Zone Tech, rings (F-202, F-207) | U17, U18, U21 | present: `G/Object/Zone.cpp`, `C/Zoned`, `planetring.jsl` | partial: `Entities/Zone.lua` (trigger only) | libphx Trigger | |
| Infinite streamed asteroid fields (F-200, F-201, F-333) | U8 | partial: `C/{Zoned,Seeded}`, `G/Task/LOD.cpp` | partial: clustered static fields (`Gen/System/AsteroidField.lua`) | n/a | Streaming depth not traced |
| Procedural asteroids, ice (F-199, F-203) | U1, U10, U14 | present: `src/liblt/Volume/*`, `G/Renderable/{Asteroid,Ice}.cpp` | present: `Gen/Asteroid.lua` | libphx `SDF` Surface Nets, `LodMesh`, `Mesh_ComputeAO` | |
| Planets, atmospheres, surfaces, water (F-204, F-205, F-300, F-301) | U5, U7, U16, U18 | present: `G/Object/Planet.cpp`, `G/Graphics/Generator/{PlanetSurface,PlanetSkybox}.cpp`, scattering shaders | partial: `Entities/Planet.lua`, planet and atmosphere shaders; spawning disabled | libphx `TexCube_Generate` | Surface terrain path not traced |
| Nebulae, starfield (F-197, F-198) | U1, U5, U12, U17 | present: `G/Graphics/Generator/Nebula.cpp`, `G/Renderable/Starfield.cpp` | present: `Gen/Nebula/*`, `Gen/Starfield.lua` | libphx `TexCube_Generate`, `TexCube_GenIRMap` | Signature look in both |
| Procedural ships (F-212, F-221) | U1-U21 | present: `L/{SDF,SDFMesh,PlateMesh}`, 21 SDF operators, `S/Item/ShipType/Generate.lts`, `S/App/platemesh.lts` | present (fighters), partial (capitals): `Gen/Ship*.lua`, `Gen/ShipLib/*`, `Gen/ShapeLib/*` | libphx `Mesh`, `BoxMesh`, GPU AO, BSP | |
| Procedural stations (F-213) | U8, U14 | present: `S/Item/StationType/Generate.lts` | partial: `Gen/StationOld.lua` in game; `Gen/Station.lua` experimental | libphx `BoxMesh` | |
| Name generators (F-211) | U3-U21 | present: `L/Grammar.cpp` plus 49 word lists | partial: inline generator in `System.lua`; grammar lists unused | n/a | |
| Spatial coherence (F-214) | U20 | not traced | absent | n/a | |
| Procedural icons (F-220, F-317) | U19 | partial: `S/Icons.lts`, `G/Icons.cpp` | partial: `App/TestIcon.lua` | n/a | |
| Deferred renderer, PBR metal, HDR (F-288 to F-293) | U1, U6, U11-U13 | present: `L/Renderer*`, `G/{ShadingModels,Materials,Light}`, `G/RenderPass/{GBuffer,GlobalLighting,LocalLighting}.cpp` | present: `s/phx/util/Renderer.lua`, `res/shader/include/{deferred,lighting,pbr}.glsl` | libphx: GL 2.1 primitives only | |
| Bloom, SMAA, chromatic aberration, grading (F-292, F-304 to F-306) | U6, U10, U13, U17-U20 | present: `L/RenderPass/Bloom*.cpp`, `G/RenderPass/SMAA.cpp`, `ThirdParty/SMAA_*`; grading via `CustomFilter` (uncertain) | present: `res/shader/fragment/filter/*` (bloom, colorgrade, aberration, vignette, glitch) | none | |
| Lens flares (F-299) | U9, U14, U17 | present: `G/RenderPass/LensFlares.cpp`, `texture/lensdirt.jpg` | not found | none | |
| Space dust, volumetric dust, god rays, motion flecks (F-294 to F-297, F-303) | U1, U6, U9, U12, U18, U20 | present: `G/RenderPass/DustClouds.cpp`, `G/Object/DustFlecks.cpp` | present: `Entities/Dust.lua`, `effect/dustcloud*.glsl`, `dustfleck.glsl` | none | God-ray pass not identified |
| Dynamic lighting (F-298, F-335) | U9, U12, U13 | present: local lighting pass | present: `light/point.glsl` | none | |
| Engine trails (F-309) | U2, U18 | present: `G/Object/Trail.cpp`, `trail`/`thruster_trail` shaders | partial: `Entities/Thruster.lua` effects | none | |
| Impostors (F-310) | U19 | present: `G/Renderable/Imposter.cpp` | not found | none | |
| Explosions, weapon and shield VFX (F-312 to F-314) | U1-U3, U6, U9, U11, U16, U20 | present: `G/Object/Explosion.cpp`, `L/ParticleSystem`, `G/Particles` | present: `Entities/Explosion.lua`, `effect/{explosion,pulsehead,pulsetail,beam}.glsl` | libphx: no particle system | |
| SSAO, motion blur, radial blur | not in videos | present: `G/RenderPass/SSAO.cpp`, `L/RenderPass/{MotionBlur,RadialBlur}.cpp` | radial blur filter | none | Code-only |

### 19.5 UI, audio, engine, tools and modding

| Feature | Shown in videos | ltheory-old | ltheory (new) | Engine | Notes |
|---|---|---|---|---|---|
| UI framework (F-246, F-274, F-275) | U1-U21 | present: `src/liblt/UI/*` plus 87 LTSL widgets (`S/Widget/*`) | present: `s/UI/*` retained Lua toolkit | libphx `HmGui` (early; slider stub), own `ImGui`, `UIRenderer`; demo-only | The videos' node UI is not identifiable as a separate code path |
| Start, settings, game menu (F-223, F-224) | U1, U10, T | present: `S/Widget/{GameMenu,Settings,SplashScreen}.lts`, `Module/Settings` | partial: settings in debug window, not saved | n/a | |
| Command interface / RTS (F-226 to F-230) | U1, U2, U8, T | not found as a dedicated widget (`S/Widget/Map.lts`, HUD targets, `war` app only) | present: `Game/Controls/CommandControl.lua` (623 lines) | libphx `Intersect_RectRect` | |
| Ship viewer, inventory, cargo (F-231 to F-234) | U2, U6, U11-U13, U17, U21 | partial: `S/Widget/Object/*` (overview with cargo, assets, market); `Sockets` stub | absent | n/a | |
| HUD: reticle, minimap, log, targets (F-241, F-242, F-261, F-282) | U3, U13, U21, T | present: `G/Widget/HUD.cpp`, `S/Widget/HUD/{Minimap,Miniscanner,Log,MenuBar,Targets}`, `S/Widget/Reticle/Default.lts` | partial: `Controls/HUD.lua` | n/a | |
| Cylindrical HUD, glass, workspaces, split panels (F-252, F-255 to F-259) | U12, U13 | partial (kw): "cylind" references in 13 old files; workspaces and split panels not traced | absent | n/a | Uncertain |
| System and universe maps (F-250, F-251) | U11, U12, U15, U19, U21 | present: `S/Widget/Map.lts`, `S/App/map.lts`, `S/App/universe.lts` | partial: `Game/SystemMap.lua` (single system) | n/a | |
| Market UI (F-264, F-269, F-281) | U15, U16, U21 | present: `S/Widget/Market/*` (left, mid, right and transaction panels), `S/App/market.lts` | absent | n/a | |
| Research, production, missions, database screens (F-270, F-271) | U17 | stub: `S/Widget/Object/{Database,Tasks}.lts` (4 lines each) | absent | n/a | |
| Radial context menu, object info (F-276, F-277) | U20, U21 | present: `S/Widget/{RadialList,RadialButton,ObjectInfo,ItemInfo}.lts` | partial: command-view context menu | n/a | |
| Colony info with trait meters (F-279) | U20 | partial: `S/Object/Widget/Colony.lts` | absent | n/a | |
| Gamepad, joystick (F-248) | U11, U12 | present: `L/Joystick` via SFML; `Button/{And,Or}` bindings | partial: `WIP/Control.lua`, `WIP/Gamepads.lua`, gamepad glyphs | libphx SDL2 gamepad and controller DB | |
| Key rebinding (F-369) | T | partial: composable bindings plus settings widget (rebinding UI not confirmed) | absent (hard-coded) | libphx `InputBindings` partial, unused | |
| VR (F-249) | planned U11 | absent (OVR headers unused) | absent | libphx `VR.cpp` `#if 0` | |
| Native Unicode text (F-337, F-367) | U8, U21 | present: `src/liblt/UI/Glyph/*`, UTF8 headers | partial: libphx `Font` | libphx FreeType, one texture per glyph | |
| UI-to-texture (F-285) | U21 | not traced | not found | libphx `RenderTarget` capable | |
| Weapon SFX, explosion audio (F-320, F-326) | U2, U3, U16 | present: `Module/SoundEngine` (FMOD Ex), about 130 SFX | demo only: `App/FMODTest.lua` | libphx FMOD Core | Audio licence caveats (5.5) |
| Dynamic tension music, soundtrack (F-321, F-322) | U2, U8 | present: `Module/MusicEngine` plus FMOD Designer banks | absent | n/a | |
| Mining and scanner audio cues (F-323, F-325) | U13, U14 | not traced | absent | n/a | |
| Procedural audio synthesis | not in videos | experimental: `src/liblt/Audio/*` | absent | none | Code-only |
| Double precision (F-332) | U8, U10 | not confirmed by the audit | absent: float `Vec3f`; `Config.gen.origin` precision test | libphx has `Vec3d`/`Box3d`, but physics, mesh and rigid body are float | Regression in the new code |
| Linux and Mac ports (F-330, F-331) | U6, U7 | partial: Linux path present but unmaintained; only Win32 restored | Windows/MSVC | libphx Linux link line TODO | |
| Reflective data model (F-336) | U10 | present: `L/AutoClass*`, `L/DeclareFunction.h`, `script/meta/*.py` | partial: `phx/util/{CType,CStruct,Type}.lua` | libphx FFI generator | |
| LT Data Editor (F-350 to F-358) | U10, U11, U15 | **absent** (tool widgets only: model, image, text and font editors; DevPanel) | **absent** (read-only `DebugInspector`) | **absent** | The biggest video-to-code gap |
| LTSL compiler and runtime (F-341, F-362) | U18-U21 | present: `L/{LTSL,Script,Expression,...}`, `L/Expression/*` (25 node types) | absent (Lua instead) | LTE | |
| Hot reload, soft compile failure (F-342, F-343) | U19, U20 | not confirmed | absent | libphx: no hot reload | |
| Launcher plus test beds (F-344 to F-346) | U20, U21 | present: `src/launch/launch.cpp`, 31 apps | present: `src/Main.cpp`, 12 test apps (BSPTest, PhysicsTest, TestEcon, TestHmGui, ...) | n/a | |
| Dev tools menu, dev panel (F-347) | U16, U20 | present: `S/Widget/DevPanel*` | present: `Game/Controls/DebugControl.lua`, `Game/GUI/{DebugWindow,DebugInspector}.lua` | libphx Profiler, Metric | |
| Profiling (F-339) | U13, U19, U21 | present: `L/Profiler` (`AUTO_FRAME`), `Module/FrameTimer` | present: profiler zones, `script/jit/loom.lua` | libphx Chrome-trace output | |
| Moddable UI and generators (F-363, F-366) | U19, U20 | present: UI and generators in LTSL | present in principle: all in Lua | n/a | No mod loader in either |
| Mod sharing, load order (F-370) | planned | absent | absent | n/a | |
| Save / load | not shown | infrastructure only: `L/Serializer`, `L/ScriptAPI/Data.cpp`; no universe save | absent | libphx `Bytes` (assets only) | |
| Networking | never planned (W) | stub: `src/liblt/Network/Client.h` empty | stub: `phx/util/Server.lua` (buggy) | libphx raw sockets | |
| Patcher, updater | prototype distribution (2013) | legacy, unbuilt: `src/old/{updater,makepatch,launcher}`, `L/{Diff,Patch}.cpp` | absent | n/a | |

### 19.6 Where the audits disagree or are uncertain

- **Era fit.** `ltheory-old` matches the U17-U21 videos closely (warp rails, Tech Lab, assembly chips, colonies, 7 traits, the LTSL UI). It does **not** obviously hold the earlier node-UI generation (U11-U16), the data editor (U10) or the commodity chart widgets (U16). These were probably overwritten by the U19-U20 UI rewrite before the snapshot.
- **The new code postdates the videos.** It was never shown in an update video. Its only public showing was the PAX South 2018 demo (W). Any "shown in videos" claim for it is a match by feature, not by code.
- **Keyword-only cells** (marked kw) need a file-level read before being cited as implemented.
- **The `ltheory` audit inferred engine behaviour** from Lua call sites because the submodule was empty. The libphx audit at the pinned commit confirms the main points: HmGui is demo-only, FMOD Core only, Bullet float physics, no editor.

---

## 20. Building comparable software: checklist and development sequence

### 20.1 Prioritised checklist (P0 to P3)

P0 = foundation that everything else needs. P1 = the core loop. P2 = depth. P3 = polish and extensibility. Feature IDs point to the catalogue.

**P0: engine foundation**
- [ ] Double-precision world positions plus a floating render origin (F-332, F-206)
- [ ] On-demand, deterministic, seeded streaming of procedural content in bounded zones (F-200 to F-202, F-333)
- [ ] Physics and collision that survives hundreds of ships; plan broadphase and sleeping early, since LT had to disable collision (F-007, F-340)
- [ ] Render LOD and impostors from day one, so there is no asteroid popping and zones read from afar (F-310, F-311)
- [ ] A reflective data model with a generic inspector/editor (F-336, F-350 to F-356)
- [ ] A scripting layer with hot reload, soft failure on errors, and isolated test-bed apps (F-341 to F-346)
- [ ] An abstract low-LOD simulation layer for off-screen regions (F-136, F-137, F-334)

**P1: core loop (pilot)**
- [ ] Third-person flight: mouse aim, roll, cruise mode with its trade-offs, free vs locked camera (F-001 to F-006)
- [ ] Hardpoint fitting with typed slots and per-item config, docking-gated (F-023 to F-025, F-232)
- [ ] Power distribution with overcharge and diminishing returns; boost capacitor (F-018, F-019)
- [ ] Weapons: pulse, beam, rail, missiles (ammo in cargo), guided missiles; turrets; lead indicator; aim assist (F-049 to F-055, F-064, F-065)
- [ ] Shields (hit-only, conforming) plus hull and subsystem damage, repair (F-056 to F-061, F-021, F-022)
- [ ] Docking, station services, market, ship dealer, storage locker (F-009, F-078, F-079, F-043, F-129)
- [ ] HUD: reticle with target tracker, subsystem rings, weapon widgets, markers filtered by discovery, minimap, notifications (F-040, F-241, F-242, F-278, F-282)
- [ ] In-system fast travel: rails with lanes, or lanes cleared of hazards (F-026 to F-028); inter-system jump holes (F-029)

**P1: core loop (world)**
- [ ] Goal-driven planner AI over action units with preconditions, responsive to interruptions (F-140, F-143)
- [ ] Commodities, ore deposits, scanner (frequency signatures, noise, saturation), prospecting drones, beam mining, cargo by mass, dynamic mass (F-083, F-103 to F-117, F-094, F-020)
- [ ] Bid/ask order-book markets with population demand; emergent prices; NPC pricing (F-084 to F-093)
- [ ] Profit-driven NPC professions: miners, traders, pirates (F-149, F-152)

**P2: depth**
- [ ] Delegation: NPCs post real contracts and missions; continuous reward-pool missions; data proof items (F-145, F-154, F-176, F-097, F-098)
- [ ] Faction hierarchies, leaders and projects with P/L; player as overlord or subordinate (F-150, F-151, F-164 to F-169)
- [ ] Production chain: construction/production modules, blueprints, batches, loop production (F-119 to F-125)
- [ ] Procedural research: blueprint derivation with trade-off modifiers, generations, assembly chips, physical tech that can be stolen or destroyed (F-184 to F-192)
- [ ] Colonies with cultures and input/output transforms; AI personalities born from colonies; spatial coherence (F-130, F-131, F-156 to F-158, F-214, F-215)
- [ ] Fleet command: RTS view, selection, follow/patrol/attack, leader-follower groups, asset panel (F-226 to F-230, F-134, F-135)
- [ ] Security and retaliation, bounty hunters, hostility (F-171, F-074, F-173)
- [ ] Universe hierarchy, maps (system, region, universe), dev viewers (F-208, F-250, F-251, F-243)

**P3: polish and extensibility**
- [ ] Procedural nebulae, volumetric dust with transitions, dynamic lights, lens flares, HDR/bloom, grading, SMAA, PBR metal, atmospheres, rings, ice (F-197, F-296 to F-306, F-207)
- [ ] One unified data-driven UI framework (layout from data, categorical filters, split panels or comparison, curved/composited HUD) (F-275, F-253, F-252, F-280, F-307)
- [ ] Advanced trading screens: charts, order book, summary market with search and compare (F-264, F-269, F-281)
- [ ] Projects, research, missions and database screens (F-268, F-270, F-271)
- [ ] Dynamic tension music, scanner and mining audio cues (F-321, F-323, F-325)
- [ ] Moddable UI and generators, Unicode text, UI-to-texture (F-363, F-366 to F-368)
- [ ] History simulation for coherent placement (F-138, F-222)
- [ ] Planet surfaces via colonies (no seamless landing) (F-034, F-205)

**Additions from the code audit and the earlier document**

- [ ] **P0:** persistent world state, save/load and save migration, designed in from the start. No LT code base ever had a universe save.
- [ ] **P0:** a deterministic seed and replay harness, with regression tests for generated content.
- [ ] **P0:** licence-clean audio middleware and assets. Do not inherit FMOD binaries or the repos' audio.
- [ ] **P1:** bounded order emission and failsafes in the economy (the new code stalled on unbounded factory orders), plus an order-matching test suite (the new matcher was broken).
- [ ] **P1:** a mission and contract generator backed by real NPC needs (a stub in the old code, absent in the new).
- [ ] **P2:** a data-driven tech and content format that is actually loaded (the old `technology/*.txt` files never were), plus a mod manifest and load order.
- [ ] **P2:** diplomacy beyond a hostility scalar (treaties, territory), if wanted. LT never built it.
- [ ] **P3:** the data editor's missing operations: undo/redo, saving, validation, search, dependency views, packaging (section 18.5).
- [ ] **P3:** AI decision explanation tools ("leader brain view", decision traces).

**Process lessons:** do the gameplay vertical slice before engine polish; avoid serial UI rewrites by fixing the UI framework early; budget optimisation continuously; keep scope at "1.0 list" discipline (W).

### 20.2 Recommended development sequence

**Phase 1: local-space vertical slice.** One generated star system; one player ship; movement and navigation; an asteroid field; the mining scanner, drones and beam; cargo management; one market; a small NPC population; basic save/load.

**Phase 2: living economy.** Dynamic prices; NPC miners, haulers and traders; a resource-to-production chain; trade routes; a player-owned logistics asset; an economy dashboard; commodity-flow and shortage diagnostics.

**Phase 3: factions and conflict.** Multiple factions; reputation; territorial ownership; patrols; piracy; raids; blockades; basic fleet command and diplomacy; faction economic and military objectives.

**Phase 4: industry and research.** Stations; factories; production queues; construction; a colony abstraction; research capacity; the technology tree; strategic UI; data-editing and validation tools.

**Phase 5: advanced procedural systems.** Procedural ships, stations and technology graphs; missions driven by live economic and faction conditions; advanced faction AI; full mod support; scriptable game logic; a mature data editor; large-scale fleet-combat optimisation.

**Lesson from LT's history:** it did Phase 5 technology (scripting, UI engines, rendering) before finishing Phases 1-2, and rewrote the engine twice. Keep the phases strictly ordered, and keep the reflective data model, the scripting layer and the test-bed harness (P0) as the only "engine-first" investments.

---

## 21. Key risks

1. **Incomplete baselines.** Both released LT code bases are incomplete and must not be treated as finished production baselines. The new one is less complete than the old.
2. **Procedural generation needs strict constraints and validation**, or it produces uninteresting or unusable output (LT: "lackluster" generated ships, U21; awkward early system layouts, U5).
3. **AI must work in arbitrary generated environments**, which makes debugging and balancing hard. Josh noted that telling correct behaviour from subtly wrong behaviour was the hardest part (W, PCGamesN).
4. **Cascading simulation failures** between economy, AI, logistics, research, production and faction warfare (LT: volatile prices, rampant piracy, stalled factory orders).
5. **Performance at scale.** Large asteroid fields, fleets, procedural assets and persistent agents are demanding. LT disabled collision at 100+ NPCs and later said performance took about 2 years to solve.
6. **Complex strategic systems need strong UI**, diagnostics and progressive disclosure. LT rewrote its UI five times.
7. **Engine vs game confusion.** Game-layer functionality must be kept apart from generic engine support during source analysis (e.g. libphx HmGui is not the game UI).
8. **The data editor is a major toolchain requirement**, not late polish. LT's editor was demoed and then never shipped in any code base.
9. **Evidence discipline.** A repository feature counts as implemented only once exact source evidence is recorded.
10. **Scope and burnout.** The engine-first approach and unbounded scope outran a solo developer (cancelled 2018). Fix the scope with a "1.0 list" early.
11. **Licence contamination.** Copyrighted audio in `ltheory/res/sound`, proprietary FMOD binaries, and unclear status of the old sound effects and music.
12. **Precision.** Large-world precision (doubles plus a floating origin) is easy to lose in a rewrite, as the new LT code did.

---

## 22. Follow-up source research plan

### 22.1 Status of the earlier plan

The earlier version of this document planned a four-batch source review. Its status now:

| Batch | Goal | Status |
|---|---|---|
| 1. Repository structure | File inventories and commit ids for all three repos; READMEs, CMake, `configure.py`, `.gitmodules` | **Done.** Commits: `ltheory-old` `0535d46`, `ltheory` `21d4504`, `libphx` `0f804f2` (chapter 24) |
| 2. Game application scripts | All of `ltheory-old/resource/script/App/` and `ltheory/script/App/` | **Done** at inventory level: 31 old apps, 13 new apps classified |
| 3. Engine and bindings | Lua/LuaJIT init, FFI, API registration, GUI, rendering, assets, input, audio, serialisation, profiling | **Done** for libphx and the LTE layer |
| 4. Data, assets and tools | Tech, commodity, faction, ship/station and procgen definitions; UI layouts; shaders; editor source; validation; tests | **Done** at inventory level. Result: there is no editor source, the tech data is unused, and content is hard-coded |

### 22.2 Remaining work

Each item closes a cell in chapter 19 that is marked uncertain, kw or "not traced".

1. **Trace hot reload in LTSL** (`ltheory-old/src/liblt/LTE/Script*`, `launch.cpp`): is there a file watcher, and how are compile failures isolated (F-342, F-343)?
2. **Identify the scanner version** in `Game/Object/Scanner.cpp` and `Widget/Scanner.lts` (F-113 to F-115).
3. **Check double precision** in LTE math and positions (`LTE/Vec*`, `Component/Motion`) (F-332).
4. **Read cruise, power and boost** (`Game/Object/{Thruster,PowerGenerator}.cpp`, `Widget/HUD.cpp`) (F-004, F-018, F-019).
5. **Look for chart widgets** (candles, MACD) in `Widget/Market/*` (F-269).
6. **Look for the warp-rail lane allocator** (wedge/annular-sector) in `WarpRail.cpp` or `WarpRail.lts` (F-160).
7. **Look for the cylindrical HUD, workspaces and split panels** (the "cylind" hits) (F-252, F-255 to F-259).
8. **Check whether `Task/LOD.cpp` is an abstract simulator** (F-136).
9. **Build and run** `ltheory-old` (Win32) and `ltheory` with libphx to confirm runtime maturity. The audits were static only.
10. **Analyse the unwatched videos** (Gameplay Demos 1-5, the Kickstarter video and trailer) for pre-2013 features.
11. **Fill the Visual Reference images** (chapter 25).

### 22.3 Search keywords (kept from the earlier plan)

| Area | Keywords |
|---|---|
| Gameplay scripts | war, ai, agent, faction, diplomacy, economy, market, trade, mining, scanner, drone, ship, station, factory, production, research, tech, tree, weapon, combat, fleet, mission |
| World | galaxy, system, planet, asteroid, procedural, generate, Universe |
| UI | gui, ui, hud, map, HmGui, Widget |
| Engine | Render, Shader, Texture, Mesh, Model, Particle, Effect, Audio, Sound, Input, Camera, Physics, Collision, Lua, FFI, Script, Asset, Resource, Serialize, File, Thread, Job, Profiler, Debug |
| Data editor | DataEditor, Editor, Edit, Graph, Node, Tree, TechTree, Technology, Schema, Property, Inspector, Tool, Mod, Content, Database, Object, AssetBrowser, Preview, Validation, Dependency, Serialization |

For each candidate file, decide:

1. Is it game UI, development tooling, mod tooling or generic engine support?
2. Is it implemented, prototype-only, demo-only or incomplete?
3. Does it edit data, display data, or only provide generic widgets?
4. Does it support node graphs, property inspection, schema validation, undo/redo, versioning, dependency analysis or live simulation preview?
5. Which domains does it affect: technologies, ships, stations, commodities, factions, AI, missions or procedural generation?

### 22.4 Inventory commands (reproducible)

```bash
git -C ltheory-old rev-parse HEAD   # 0535d46d04db78ecb2bcf532f79a8b366757bb3d
git -C ltheory-old ls-tree -r --name-only HEAD > ltheory-old-files.txt
git -C ltheory rev-parse HEAD       # 21d450406b2a742b2773692198e7a908a9978ec4
git -C ltheory ls-tree -r --name-only HEAD > ltheory-files.txt
git -C libphx rev-parse HEAD        # 0f804f26aa02f2bb5993c7678357713ea12dd6d8
git -C libphx ls-tree -r --name-only HEAD > libphx-files.txt
```

`ltheory-old` needs `git lfs pull` for its assets. `ltheory` needs `git submodule update --init` to fetch libphx.

---

## 23. Contradictions and unresolved gaps

### 23.1 Contradictions found

| # | Topic | Statement A | Statement B | Resolution |
|---|---|---|---|---|
| 1 | Scripting language | Videos U18-U21: LTSL is the game and mod language | FAQ: engine "from scratch in C, Lua, OpenGL". Redux wiki: Apr 2016 announcement of **Python 2.7** | Chronological. LTSL (2014, old code) → Python 2.7 announced (2016) → **Lua** in the released new code and libphx. The FAQ text is later than the videos. Python left no trace in any repo |
| 2 | LTSL character | U19: Lisp-like functional semantics with Python-style blocks | Old code: indentation-based, statically typed, with imperative For/While/Switch nodes and pointer-like `address`/`deref`/`ref` | Compatible on syntax (indentation). "Functional" overstates it; the implementation is a typed, mixed-paradigm language |
| 3 | Data editor | U10: the LT Data Editor is demonstrated; W lists it (Oct 2013) | No repository contains it | It was shown but not released. Probably superseded by the node UI (U11) and then the LTSL UI (U20) |
| 4 | In-game script editor | W lists one (Jul 2014) | U19: "possible, not promised". No video shows one. Old code has only a read-only text viewer | Treat as unverified or planned |
| 5 | Number of videos | W: "21 videos, Jan 2013 to Dec 2014" | 20 numbered updates exist (#4 missing) plus the tutorial; U21 covers Oct-Nov 2014 and was delivered early Dec 2014 | Both counts can be right if the tutorial is counted |
| 6 | Treaties, overrun planets, systems changing hands | The earlier version of this document listed them as Design Confirmed | No video, no code | Downgraded to [Unverified] |
| 7 | "Volumetric nebulae" | Implied by the earlier document | U9: the volumetric effect is dust; nebulae are skybox cubemaps | Corrected (section 15.3) |
| 8 | "Radial scanner menu" | The earlier document | U13: a radial time-history signal plot (scanner 1.0) | Corrected (section 9.2) |
| 9 | Double precision | U10: "done" | libphx and new game are float; the old audit did not confirm it | Regression in the new code; old code still to check (22.2 item 3) |
| 10 | Platforms | U6, U7, U16: Linux, Mac and Windows; Linux was the native dev client | Old README: only Win32 restored; Linux "on your own" | Released snapshot state, not the 2014 state |
| 11 | AI traits | U20: 7 traits | `AI/Traits.h`: 7 names, `XSIZE 6` | Likely a code bug in the snapshot |
| 12 | Genre | Wikipedia: RTS-flavoured | FAQ: a space simulator | Both hold: flight sim plus RTS-style command |
| 13 | Prototype year | Prototype shipped 28 Apr 2013 | Old updater registry key and patches say "2014 LT Prototype" | Probably later patch builds; unresolved |
| 14 | HmGui | Earlier document: "HmGui demo indicator" | libphx: HmGui is early (slider stub) and used only by demos; the game uses the Lua `UI` toolkit | Resolved |
| 15 | Default app (old) | Earlier document: unknown | `resource/script/App/ltheory.lts`, launched by `launch.exe <app>` | Resolved |

### 23.2 Unresolved gaps

- **Video publication dates.** Only the month each update covers is recorded. Exact upload dates were not captured.
- **Unanalysed videos.** Gameplay Demo 1-5, the Kickstarter full video and the trailer were listed but not transcribed.
- **Unreachable web sources.** The forum feature list (forums.ltheory.com, t=6134) returned HTTP 500; Kickstarter pages returned 403; the subreddit and Steam were not reached. Faction, tech-tree and audio specifics from these are missing.
- **Factions.** No primary source describes faction design beyond what the videos show.
- **Runtime behaviour.** No code base was built or run.
- **Feature dates in code.** Squashed histories make it impossible to date features or to match code to a specific video.
- **Which build PAX 2018 used.** It is unverified whether the released `ltheory` snapshot is the PAX South 2018 build. The released main app runs 100 escorts, not 2,000+ ships.
- **Old audio licence.** Whether the contracted sound effects and the composer's music are covered by the Unlicense.
- **Proper nouns from captions** ("Verad 9", "Vialo", "Garson", "Palala", "Arcax", "Mr. France") may be misspelt.

---

## 24. Sources and evidence

### 24.1 Development videos (YouTube, Josh Parnell)

URL form: `https://www.youtube.com/watch?v=<ID>`. "Covers" is the development month; upload dates were not captured.

| Key | Title | Video ID | Length | Covers |
|---|---|---|---|---|
| U1 | Limit Theory Development Update #1 | `53CQpjfM01w` | 15:47 | Jan 2013 |
| U2 | Limit Theory Development Update #2 | `-ThoPpBw144` | 15:07 | Feb 2013 |
| U3 | Limit Theory Development Update #3 | `PoZ4MOeyH0M` | 16:03 | Mar 2013 |
| T | [OFFICIAL] Limit Theory Prototype Tutorial | `tUPKTTsseHQ` | 23:01 | Apr 2013 prototype |
| U5 | Limit Theory Development Update #5 | `t6vVziWb00U` | 10:57 | May 2013 |
| U6 | Limit Theory Development Update #6 | `IqT1Lr6dnLs` | 17:37 | Jun 2013 |
| U7 | Limit Theory Development Update #7 | `mrFoesWWT30` | 10:18 | Jul 2013 |
| U8 | Limit Theory Development Update #8 | `tZK2VWhjVGg` | 15:59 | Aug 2013 |
| U9 | Limit Theory Development Update #9 | `Fq4gih_AX44` | 15:12 | Sep 2013 |
| U10 | Limit Theory Development Update #10 | `KN3tKT0E0t8` | 22:43 | Oct 2013 |
| U11 | Limit Theory Development Update #11 | `Ekq56VZqQvs` | 15:11 | Nov 2013 |
| U12 | Limit Theory Development Update #12 | `ADcfqusFfaM` | 19:42 | Dec 2013 |
| U13 | Limit Theory Development Update #13 | `YBf1odVtlWk` | 19:15 | Jan 2014 |
| U14 | Limit Theory Development Update #14 | `e8SOZxCXokg` | 18:40 | Feb 2014 |
| U15 | Limit Theory Development Update #15 | `k4PVSIShRgM` | 22:25 | Mar 2014 |
| U16 | Limit Theory Development Update #16 | `A2albJYS-wI` | 26:06 | Apr 2014 |
| U17 | Limit Theory Development Update #17 | `VshxojiNqJk` | 22:52 | May 2014 |
| U18 | Limit Theory Development Update #18 | `4r6N7_HAsZo` | 18:10 | Jun 2014 |
| U19 | Limit Theory Development Update #19 | `UGP7S2Oq0M8` | 20:05 | Jul 2014 |
| U20 | Limit Theory Development Update #20 | `MumoTz8CKtE` | 36:42 | Aug + Sep 2014 |
| U21 | Limit Theory Development Update #21 | `0tPdbLe3zx0` | 37:01 | Oct + Nov 2014 (delivered early Dec) |

**Listed but not analysed:** Limit Theory Gameplay Demo 1 (`hNU02rDSSvI`, 3:24), Demo 2 (`qzHrpptj5vQ`, 8:39), Demo 3 (`s4FPq9nfcps`, 5:05), Demo 4 (`nBtZKio_J20`, 4:34), Demo 5 (`KLINRbq3QKM`, 12:30), Kickstarter Full Video (`o3KDeQOEwzE`, 7:52), Kickstarter Trailer (`V7suG__lR40`, 2:00).

**Method:** auto-generated English captions were downloaded and read in full for every analysed video. The notes paraphrase them, with short quotes only.

### 24.2 Repositories

| Repository | URL | Commit (only commit, squashed) | Date | Message |
|---|---|---|---|---|
| ltheory-old | https://github.com/JoshParnell/ltheory-old | `0535d46d04db78ecb2bcf532f79a8b366757bb3d` | 2022-07-24 | readme: move LFS warning |
| ltheory | https://github.com/JoshParnell/ltheory | `21d450406b2a742b2773692198e7a908a9978ec4` | 2022-08-06 | remove potentially-copyrighted font; fix HmGui demo gamma space |
| libphx | https://github.com/JoshParnell/libphx | `0f804f26aa02f2bb5993c7678357713ea12dd6d8` | 2022-07-24 | add license (matches the `ltheory` submodule pin) |
| SFML (old submodule) | https://github.com/SFML/SFML | as pinned by `ltheory-old/.gitmodules` | | |
| Limit Theory Redux (community continuation) | https://github.com/Limit-Theory-Redux/ltheory | not audited | | |

### 24.3 Web sources

| Source | URL | Used for |
|---|---|---|
| Wikipedia: Limit Theory | https://en.wikipedia.org/wiki/Limit_Theory | Kickstarter figures, genre, release of the source, burnout coverage |
| Official news page | https://ltheory.com/news.html | Prototype date, 2017-2018 milestones, PAX South 2018 demo |
| Official FAQ | https://ltheory.com/faq.html | Design philosophy, engine language claim, no multiplayer, planet scope |
| Kickstarter cancellation post | https://www.kickstarter.com/projects/joshparnell/limit-theory-an-infinite-procedural-space-game/posts/2270873 | Cancellation, 28 Sep 2018 |
| Kickstarter post 1107942 (search snippet only) | https://www.kickstarter.com/projects/joshparnell/limit-theory-an-infinite-procedural-space-game/posts/1107942 | LTSL-driven HUD widgets |
| Redux wiki: dev overview | https://github.com/Limit-Theory-Redux/wiki/blob/main/dev-overview.md | Update chronology, Road to Beta, 2015-2016 history, Python 2.7 announcement |
| Redux CONTRIBUTING | https://github.com/Limit-Theory-Redux/ltheory/blob/main/CONTRIBUTING.md | 2016 libphx engine |
| PC Gamer | https://www.pcgamer.com/limit-theory-dev-releases-source-code-of-the-cancelled-space-sim-officially-ends-project/ | Source release |
| PCGamesN | https://www.pcgamesn.com/indie/limit-theory-ai-rebuilt-scratch | AI rebuild, AI debugging difficulty |
| Grokipedia (secondary, unverified) | https://grokipedia.com/page/limit_theory | "Dynamic AI factions" claim |

**Unreachable:** forums.ltheory.com feature list (t=6134, HTTP 500), Kickstarter project pages (403), `github.com/lttheory` (404).

### 24.4 Earlier source categories (kept)

Official LT project material; development-video coverage; repository documentation for the old, new and engine code; technical material on procedural ship and station geometry; archived forum and community references to development updates.

---

## 25. Visual reference

Planned frame captures from the update videos, one per high-value moment, so the catalogue can be read alongside images. Timestamps are to be found when capturing. Images will be stored next to this document and linked in the Image column. **These frames are copyrighted footage, for internal research reference only.**

| # | Update | Video ID | Moment | What it shows | Feature IDs | Image |
|---|---|---|---|---|---|---|
| 1 | U1 | `53CQpjfM01w` | Holographic command interface with action menu | First RTS view, selection, follow lead-time slider | F-226, F-308 | [view](SimResearchImages/row-01-command-interface-action-menu.png) |
| 2 | U1 | `53CQpjfM01w` | Fighter under a capital ship's shield | Hull-conforming hit-only shield highlight, 50x scale | F-056, F-057, F-016 | [view](SimResearchImages/row-02-capital-ship-shield-hit.png) |
| 3 | U2 | `-ThoPpBw144` | Power distribution at 240% | Power UI, overcharge, trails scaling with power | F-018, F-235, F-309 | [view](SimResearchImages/row-03-power-distribution-overcharge.png) |
| 4 | U2 | `-ThoPpBw144` | Command interface, solid vs holographic, recoloured | Class icons, colour picker, Tab mode toggle | F-226, F-237 | [view](SimResearchImages/row-04-command-interface-recolour.png) |
| 5 | U3 | `PoZ4MOeyH0M` | Ships menu with Destroyer 69,000 / Bomber 6,300 | Ship dealer colours, hangar deck | F-079, F-238, F-239 | [view](SimResearchImages/row-05-ships-menu-destroyer.png) |
| 6 | U3 | `PoZ4MOeyH0M` | HUD with weapon and subsystem widgets in a Distress mission | HUD v1, green/red/grey friend-foe | F-241, F-066 | [view](SimResearchImages/row-06-hud-distress-mission.png) |
| 7 | T | `tUPKTTsseHQ` | Bottom HUD radial rings and the red-cross lead indicator | Subsystem rings, aim assist | F-242, F-064, F-065 | [view](SimResearchImages/row-07-hud-rings-lead-indicator.png) |
| 8 | U5 | `t6vVziWb00U` | Planet with atmospheric scattering and clouds, new starfield | Planet rendering, nebula | F-204, F-300, F-198 | [view](SimResearchImages/row-08-planet-scattering-clouds.png) |
| 9 | U5 | `t6vVziWb00U` | Dev region viewer | Universe topology: nodes, jump-hole lines, sub-regions | F-243, F-208 | [view](SimResearchImages/row-09-dev-region-viewer.png) |
| 10 | U6 | `IqT1Lr6dnLs` | Construction module with blueprint: 16 units / 20 s | Production UI, folder ship viewer, IND hardpoint | F-120, F-232, F-024 | [view](SimResearchImages/row-10-construction-module-blueprint.png) |
| 11 | U6 | `IqT1Lr6dnLs` | Dusty asteroid field with god rays; plating with HDR glint | God rays, HDR/bloom, metal | F-295, F-291, F-290 | [view](SimResearchImages/row-11-dusty-field-god-rays.png) |
| 12 | U7 | `mrFoesWWT30` | Planet surface with ground-to-space scattering and water | Terrain, water, scattering | F-205, F-301, F-300 | [view](SimResearchImages/row-12-planet-surface-water-scattering.png) |
| 13 | U8 | `tZK2VWhjVGg` | Inside the infinite asteroid field next to a huge rock | Hierarchical streaming, scale variation | F-200, F-333 | [view](SimResearchImages/row-13-infinite-field-huge-rock.png) |
| 14 | U9 | `Fq4gih_AX44` | Looking back from a clear pocket into sunlit dust | Volumetric dust | F-296 | [view](SimResearchImages/row-14-volumetric-dust-pocket.png) |
| 15 | U9 | `Fq4gih_AX44` | Muzzle flashes and thrusters lighting an asteroid; anamorphic flares | Dynamic lighting, lens flares | F-298, F-299 | [view](SimResearchImages/row-15-dynamic-lighting-muzzle-flash.png) |
| 16 | U10 | `KN3tKT0E0t8` | Bloom off vs on | Bloom comparison | F-292 | [view](SimResearchImages/row-16-bloom-off-vs-on.png) |
| 17 | U10 | `KN3tKT0E0t8` | Tech-tree mock-up tooltip "Heavy Fighter +20% / +25% mass" | Procedural modifier tech tree | F-184, F-194 | [view](SimResearchImages/row-17-tech-tree-heavy-fighter-tooltip.png) |
| 18 | U10 | `KN3tKT0E0t8` | LT Data Editor recolouring the munition (octagon, plant, fern layouts) | Data editor, live editing | F-350, F-352, F-353 | [view](SimResearchImages/row-18-data-editor-munition-colour.png) |
| 19 | U11 | `Ekq56VZqQvs` | Tilted 3D node UI; universe map region of 5 systems | Node UI, hierarchical map | F-246, F-247, F-250 | [view](SimResearchImages/row-19-tilted-node-ui.png) |
| 20 | U12 | `ADcfqusFfaM` | Holographic ship viewer and split-screen panels with filters | Holographic UI, categorical filtering | F-233, F-252, F-253 | [view](SimResearchImages/row-20-holographic-split-panels.png) |
| 21 | U13 | `YBf1odVtlWk` | Cylindrical HUD in cockpit mode; glass HUD; workspace dots | HUD technology | F-255 to F-259 | [view](SimResearchImages/row-21-cylindrical-hud-cockpit-mode.png) |
| 22 | U14 | `e8SOZxCXokg` | Ice field with subsurface glow | Ice rendering | F-203, F-302 | [view](SimResearchImages/row-22-ice-field-subsurface.png) |
| 23 | U14 | `e8SOZxCXokg` | Scanner 2.0 spectrum with star peak, prospecting-drone widget "63% density", red transfer beam | Prospecting and mining loop | F-114, F-111, F-107 | [view](SimResearchImages/row-23-scanner-spectrum-star-peak.png) |
| 24 | U15 | `k4PVSIShRgM` | 100+ NPCs streaming between fields and station; commodity graph capped near 100 cr | Living economy | F-147, F-084, F-264 | [view](SimResearchImages/row-24-npc-traffic-streams.png) |
| 25 | U16 | `A2albJYS-wI` | Commodities screen (candles, EMA, MACD, order book with a 4,000 ask) | Advanced trading UI | F-269, F-089 | [view](SimResearchImages/row-25-commodity-price-history.png) |
| 26 | U16 | `A2albJYS-wI` | Personal interface and project metrics (43,000) | Projects and empire management | F-267, F-268, F-167 | [view](SimResearchImages/row-26-project-metrics.png) |
| 27 | U17 | `VshxojiNqJk` | Tech Lab breakthrough and blueprint attributes; Production Lab 60 transfer units | Implemented research chain | F-124, F-186, F-188, F-125 | [view](SimResearchImages/row-27-blueprint-attributes.png) |
| 28 | U17 | `VshxojiNqJk` | Missions screen "Damage the enemy" and Database redemption (2,000 cr) | Missions and data system | F-176, F-098, F-271 | [view](SimResearchImages/row-28-mission-claim-reward.png) |
| 29 | U18 | `4r6N7_HAsZo` | Leaving the planetary ring zone: dust fades, stars appear; curved engine trails | Zone Tech, trails | F-202, F-297, F-309 | [view](SimResearchImages/row-29-leaving-ring-zone.png) |
| 30 | U18 | `4r6N7_HAsZo` | Colony icon on a planet, then the "Vialo" colony surface | Colonies and landing | F-130, F-034 | [view](SimResearchImages/row-30-vialo-colony-surface.png) |
| 31 | U19 | `UGP7S2Oq0M8` | LTSL ship-generator script beside the live regenerated ship; procedural icon grid | Scripting, hot reload | F-362, F-363, F-342, F-220 | [view](SimResearchImages/row-31-ltsl-ship-generator-script.png) |
| 32 | U20 | `MumoTz8CKtE` | Colony info with 7 trait meters; radial context menu; curved vs flat UI | Cultures, new UI engine | F-279, F-276, F-280 | [view](SimResearchImages/row-32-colony-cultural-traits.png) |
| 33 | U21 | `0tPdbLe3zx0` | Riding a curved warp rail through planetary rings with oncoming traffic | Warp rails, rings | F-027, F-028, F-207 | [view](SimResearchImages/row-33-warp-rail-through-rings.png) |
| 34 | U21 | `0tPdbLe3zx0` | Market UI with shift-click comparison and buy slider (price change at 7 units) | Final market UI | F-281, F-091 | [view](SimResearchImages/row-34-market-buy-slider.png) |
| 35 | U21 | `0tPdbLe3zx0` | Native text rendering in Japanese, Chinese, Russian and Korean with shadow | Text engine | F-337, F-318, F-367 | [view](SimResearchImages/row-35-native-unicode-text.png) |
