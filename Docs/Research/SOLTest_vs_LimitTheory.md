<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->
# SOLTest vs Limit Theory: Comparison, Feature Gaps and Unreal 5.8 Feasibility

Research comparing **SOLTest** (Acid Rain Studios LLC, Unreal Engine 5.8, C++) with **Limit Theory** (LT, Josh Parnell / Procedural Reality, cancelled 2018). First written 2026-10-14, revised 2026-10-15 (see [chapter 10](#10-changes-in-this-revision)). SOLTest facts reflect HEAD `c4c94ee` (this document itself was first committed as `0933adf`).

LT facts and feature IDs (F-001 to F-370) come from [`SimResearch.md`](SimResearch.md), the evidence base for what LT showed in its videos and posts. For this revision, LT's three public repositories (`ltheory-old` C++/LTSL, `ltheory` Lua, `libphx` C engine, all Unlicense) were also read at source level, without building or running them (chapters 7 and 8). SOLTest facts come from `CLAUDE.md`, [`../ARCHITECTURE.md`](../ARCHITECTURE.md), [`../GAME_MECHANICS.md`](../GAME_MECHANICS.md), [`../SDDs/`](../SDDs/), [`../Plans/`](../Plans/), [`../ToDo/`](../ToDo/), the source tree and `git log`. Engine facts come from the local UE 5.8 install. Difficulty ratings, person-week ranges and costs in this document are **estimates**, not measurements or schedules. LT text is paraphrased, never quoted.

---

## Table of contents

1. [Purpose and how to read](#1-purpose-and-how-to-read)
2. [SOLTest in brief](#2-soltest-in-brief)
3. [Side-by-side comparison by domain](#3-side-by-side-comparison-by-domain)
4. [Feature gap list: game features](#4-feature-gap-list-game-features)
5. [Feature gap list: editor and tooling features](#5-feature-gap-list-editor-and-tooling-features)
6. [Can all of LT's features be added in Unreal? Assessment and adoption order](#6-can-all-of-lts-features-be-added-in-unreal-assessment-and-adoption-order)
7. [Limit Theory AI: source review and adoption for SOL](#7-limit-theory-ai-source-review-and-adoption-for-sol)
8. [Poor-fit features: source review and revised assessment](#8-poor-fit-features-source-review-and-revised-assessment)
9. [Open questions, assumptions and unverified claims](#9-open-questions-assumptions-and-unverified-claims)
10. [Changes in this revision](#10-changes-in-this-revision)

---

## 1. Purpose and how to read

### 1.1 Purpose

This document answers three questions for the project owner:

1. Where does SOLTest stand against Limit Theory, domain by domain?
2. Which LT features does SOLTest lack, and how hard would each be to build in Unreal 5.8 inside SOLTest's architecture (double-precision universe, custom render-origin rebasing, Keplerian rails, "Mass for everything", ~1M-entity goal)?
3. Can everything LT had be added, and in what order would it make sense?

It is a research input, not an SDD. Nothing here is a decision: any feature picked from it still goes through `grill-me`, an SDD and a plan, per `CLAUDE.md`.

### 1.2 The two projects have different centres of gravity

| | SOLTest | Limit Theory |
|---|---|---|
| What it is | A **scale-first, 1:1 real solar-system flight and visual simulation**: real ephemerides, real star catalogue, physically based rings and shadows, Mass entities | A **procedural economy and AI sandbox** as shown in its videos: planner-driven NPCs, order-book markets, factions, research, all in a procedurally generated, stylised universe. The public code implements much less of this than the videos show (chapters 7 and 8) |
| Strongest layer | Universe/numerics, rendering, test and review discipline | Simulation and AI *design ideas*, tooling for iteration |
| Weakest layer | No game loop yet: no economy, AI, factions, missions, stations, save | Performance at scale, visual popping, repeated rewrites, buggy and partly disabled economy code, never shipped |
| Engine | Unreal Engine 5.8 (stock), single C++ module | Home-grown LTE (C++/LTSL), then libphx (C) plus Lua |
| Development span | ~18 calendar days (2026-09-27 to 2026-10-14), 67 commits up to `7efe35c` (66 excluding the initial commit) | ~6 years (2012 to 2018), cancelled |
| Content model | Hand-sourced real data (JPL, AT-HYG), procedural fill only for belt and ring rocks | Everything procedural (universe, ships, stations, items, tech, names) |

Reading guide: chapter 3 gives the domain overview; chapters 4 and 5 are the detailed gap tables; chapter 6 is the plain-language answer, the cost model and the roadmap; chapters 7 and 8 are source-level reviews of LT's AI and of the features rated poor fit; chapter 9 lists what the owner needs to decide.

### 1.3 Column conventions and estimate scale

| Column | Values |
|---|---|
| Present in SOLTest? | **absent** (nothing built or planned), **partial** (some related code exists), **planned** (in the roadmap or a `Docs/ToDo/` item), **n/a** (deliberately out of scope by SOLTest's design) |
| UE 5.8 feasibility | **Direct** (stock engine features cover most of it), **Needs custom work** (engine gives building blocks, game-specific systems must be written), **Poor fit** (conflicts with SOLTest's design or is not worth building in UE). Engine features marked *(Experimental)* or *(Beta)* carry that flag in their 5.8 `.uplugin` (see 6.8) |
| Difficulty | **S** (up to ~1 person-week), **M** (1 to 3 person-weeks), **L** (4 to 12 person-weeks), **XL** (more than 12 person-weeks, or open-ended) |

**Estimate assumptions** (apply to every rating and range in this document):

- One developer (the owner) directing AI subagents under the current `CLAUDE.md` workflow (grill-me, SDD, plan, TDD with a separate test author, adversarial review, docs sync), including the pause at 70% of the weekly usage limit.
- One person-week is about 5 **active working days** of the owner. Calibration: a roadmap part took 1 to 5 active commit days; calendar time is longer (Part 5 ran 10 calendar days, 5 of them with commits, with 16+ SDD amendments). Usage-limit pauses stretch calendar time further.
- Art, music and sound content are excluded (bought or outsourced); content-bound rows say so.
- Gameplay systems (AI behaviour, economy stability, combat balance) include a **+50 to 100% tuning allowance**, because they need human playtest loops that the "Claude verifies, the user tests only the finished product" workflow does not provide.
- Small "S each" items each pay roughly 0.5 to 1 day of process overhead unless bundled into one ticket.

**Confidence legend** (used in 6.5 and 6.7):

| Confidence | Meaning |
|---|---|
| **High** | Technically specified work similar to parts already built; Claude can verify it alone |
| **Med** | Depends on an engine spike, an open design decision, or an unfixed blocker (for example the headless test runner) |
| **Low** | Dominated by gameplay tuning, research-grade design or content; no reference implementation exists, LT's included |

---

## 2. SOLTest in brief

### 2.1 History (dated timeline)

| Date | Issue / Part | Delivered |
|---|---|---|
| 2026-09-27 | setup, #1 | UE 5.8 project, architecture SDD 1 and roadmap Plan 1 |
| 2026-09-28 | #2 Part 1a | Double-precision J2000 ecliptic universe, Kepler solver (JPL elements), sim clock and warp ladder, SoA body registry, anchor with hysteresis, render-origin rebasing, log-depth far placement |
| 2026-09-28 | #2 Part 1b | Ship as a Mass entity, flight-assist and Newtonian modes, gravity, analytic swept-sphere collision, pawn proxy, targeting and frame lock |
| 2026-09-29 | #2 Part 1c | Canvas HUD, 3D radar, speed panel |
| 2026-09-29 to 30 | #3 Part 2 | Jump map: map camera, body LOD, picking gesture, 3 s warp jump with velocity-matched arrival |
| 2026-09-30 | #4 Part 3, #12 | Surface-lock (orientation alignment near bodies); planet axial rotation |
| 2026-09-30 | #5 Part 4 | Star field from AT-HYG (2.55M stars, bright ones as sprites, faint ones baked to a cubemap) |
| 2026-10-01 to 10-09 | #6 Part 5 | Moons and dwarf planets (36 bodies), asteroid belt (9,268 Mass entities), ring systems with a three-tier renderer (far disc, ~1M-rock Niagara layer, streamed Mass rocks) |
| 2026-10-09 | #7 Part 6 | Pulse guns, swept-sphere hit detection through spatial grids, droppable shielded targets, combat VFX and placeholder audio |
| 2026-10-09 to 10-12 | #8 Part 7 | Main and pause menus, Controls/Info/About, settings, key rebinding, settings persistence |
| 2026-10-12 | #15 | Motion-blur fix for dropped targets |
| 2026-10-13 | #13, #14 | Star-field quality (point stars, physical brightness, smoothed exposure); gas-giant band shells, storms and volumetric cloud layer |
| 2026-10-14 | #16, #17 | Analytic ring and planet shadows; dashed GPU orbit lines |

### 2.2 What is done (summary)

| Area | State |
|---|---|
| Universe | True 1:1 scale, `FVector3d` metres, analytic Kepler orbits for 36 bodies, rotation, time warp up to 30 days/s, custom render-origin rebasing every 10 km |
| Flight | 6DoF with assist and Newtonian modes, speed cap to 0.5c, gravity, no-tunnelling swept collision against bodies, surface-lock, jump map |
| Minor bodies | 9,268 belt asteroids and 10,000 pooled ring rocks as Mass entities: 19,268 minor bodies, ~19,269 Mass entities counting the player ship; flown through without collision |
| Rendering | Procedural planet material, gas giants with volumetric clouds, real star sky, ring optical depth and ring/planet shadows, orbit lines, game-driven exposure |
| Combat | Two forward pulse guns, up to 20,000 live bolts, up to 1,000 passive targets with shield and health; nobody fights back |
| UI | Canvas flight HUD and radar (`ASOLFlightHud`, an `AHUD`), plus UMG menus, settings and speed panel built in C++ |
| Persistence | Settings only (versioned `USaveGame`, async, dirty-checked); no sim time and no player state |
| Quality process | 51 automation test files (plus a shared `SOLTestHelpers.h`) with 707 test macros, scripted `-SOL*` smoke runs, adversarial performance and style review per part |
| Editor side | Python asset-building scripts, unreal-mcp; no custom editor module |

### 2.3 Todo list

**Roadmap parts not started** (Plan 1):

| Part | Issue | Content |
|---|---|---|
| 8 | #9 | Art and audio pass: real ship model, planet textures, real sounds |
| 9 | #10 | Procedural terrain and atmospheres (PCG/Nanite) |
| 10 | #11 | Scale and performance demo: NPC ships in Mass, stress test toward 1M. The only planned NPC work, and it has no behaviour design |

**`Docs/ToDo/` items:** supercruise travel, planetary perturbations, surface landing, accurate pole directions, star diffraction spikes, Sun glow and flare, adaptive body tessellation, asteroid/ring collision, minor-body targeting.

**Known tech debt, grouped** (22 items in `CLAUDE.md`):

| Group | Items |
|---|---|
| Scale blockers for ~1M | Belt ISM per-frame CPU transform upload; idle ring rocks still Kepler-solved; constant orbital bases recomputed per frame; `BuildMinorBodyGrid` full scan (~0.4 ms at 19,268 minor bodies, ~20 ms extrapolated at 1M) |
| Ordering and coupling | `Ship/`-`UI/` include cycle; unordered `OnUniverseUpdated` and `OnBodiesUpdated` listeners |
| Rendering cost or correctness | Dense ring layer ~3.3 ms GPU; belt mesh without LOD; TSR/motion-blur smear on render-space objects without motion vectors; orbit-line memory (~55 MB) and game-thread rebuilds; ring-carpet vs disc profile mismatch; private copy of the ring table in `M_SOLRingFar`; spherical-planet shadow assumption |
| Small CPU waste | Focal length, tilt quaternion, Sun GM literal duplication, ring reassignment allocation |
| Tooling | Headless `RunTests.ps1` stalls on this machine (root cause unknown) |
| Latent bug | Non-body targetable frame carry for bolts and targets |
| Docs drift | Hand-written menu prose and fixed keys in `SOLMenuText.h` |

---

## 3. Side-by-side comparison by domain

| Domain | SOLTest today | Limit Theory | Gap / advantage |
|---|---|---|---|
| Flight model | 6DoF, assist vs Newtonian, rate-command rotation, boost, speed cap to 0.5c, gravity, frame lock to any body | Freelancer-style third person, mouse aim, roll, cruise mode with power trade-offs, free vs locked camera, power distribution, dynamic mass (F-001 to F-006, F-018 to F-020) | Comparable core; **SOLTest ahead** on physical fidelity and frame handling; **LT ahead** on ship systems (power, cruise trade-offs, mass from cargo) |
| Universe and scale | Real Solar System at 1:1, analytic orbits, 36 bodies, ~9.3k real and fill asteroids, 4 ring systems, time warp to 30 d/s, double precision with custom rebasing | Designed as an infinite procedural universe of regions and systems linked by wormholes (F-209 is "planned / partial"); the videos showed a few systems; the old code builds one region with a handful of systems, the new code one system per seed. Scale 10x stylised, realistic scale "possible as a mod" (F-206, F-208, F-360) | **SOLTest far ahead** on realism and numerics (LT's new code even regressed to floats). **LT ahead** on *designed* breadth: many systems, procedural variety, zones |
| Travel | Jump map with picked destination and 3 s warp; supercruise is a ToDo | Warp lanes, then warp rails with lane allocation; jump holes between systems (F-026 to F-029) | Different answers to the same problem. LT's rails are a world feature AI also uses; SOLTest's jump is player-only (rails reviewed in 8.4) |
| Rendering and effects | Niagara, ~1M GPU ring rocks, volumetric gas-giant clouds, AT-HYG sky, analytic ring/planet shadows, GPU orbit lines, TSR | Custom deferred PBR, procedural nebulae, volumetric dust, lens flares, impostors, atmospheres, procedural ships and stations (F-288 to F-319) | **SOLTest ahead** on physical correctness and particle scale; **LT ahead** on procedural art (nebulae, dust, ships, stations) and art direction breadth |
| Combat | Pulse guns, 20k-bolt SoA pool, swept hits on moving targets and minor bodies, shields and health on passive targets | Pulse, beam, rail, missiles, turrets, hull-conforming shields, subsystem damage, lead indicator, aim assist, AI dogfighting, fleet battles (F-049 to F-076) | **LT far ahead** on gameplay (enemies, weapon variety, damage model). **SOLTest ahead** on projectile scale and hit-test robustness |
| Economy | None | Credits, commodities, bid/ask order books, population demand, emergent prices, NPC pricing, storage (F-077 to F-102). In code: an escrowed order book with a credit-creating refund bug (old), and a broken matcher plus disabled factory orders (new) (8.6) | **Entire layer missing** in SOLTest; LT's code is a set of ideas, not a working reference |
| AI / NPCs | None (Part 10 plans behaviour-less NPC ships for a scale demo) | Videos: escort/patrol AI, task-based professions, leaders and projects, personalities; ~100 to 128 NPCs per system, and 2,000+ ships reported for the PAX South 2018 demo build (not in the released repo). Code: a task stack with random job sampling (old) and best-of-100 job sampling that is idle in the shipped config (new) (F-134 to F-162, chapter 7) | **Entire layer missing**. SOLTest's Mass base is a better substrate for scale than LT ever had, but LT's AI must be redesigned, not ported |
| Factions | None | Factions as economic hierarchies, named factions, pirates, hostility by action; no real diplomacy (F-163 to F-179) | Missing; LT itself was thin here |
| Missions | None | Procedural missions v1, NPC-posted contracts, continuous reward-pool missions, data proofs (F-175 to F-178, F-145, F-154). In code the AI posts one kind of mission that nothing consumes | Missing |
| Industry, mining, stations | None (asteroids indestructible on rails) | Scanner, prospecting drones, beam mining, production modules, Tech/Production Labs, stations, asteroid bases, colonies (F-103 to F-133) | Missing. SOLTest has real asteroid data to build on |
| Research / tech | None | Procedural blueprint derivation, assembly chips, physical technology (F-180 to F-196) | Missing (LT's code version reviewed in 8.7) |
| UI / HUD | Canvas HUD, radar, speed panel, jump map, UMG menus, settings, live key labels | Many UI generations: HUD, command (RTS) view, ship viewer, market, projects, research, map, radial menus, curved HUD (F-223 to F-287) | **LT ahead** on breadth (strategic screens); **SOLTest ahead** on polish of what exists (live rebinding, truthful info pages) |
| Persistence | Settings save only; universe deterministic from sim time but sim time not saved | No universe save in any code base | Both weak. SOLTest's determinism keeps a future save small **for the universe and the player**; it grows with every dynamic system (deposits, stations, NPCs, economy) |
| Tools / editor | Python asset scripts, unreal-mcp, smoke flags, automation tests, adversarial reviews | Reflective data model, LT Data Editor (demoed, never in any repo), LTSL with hot reload, test beds, dev panel (F-336, F-341 to F-350) | **LT ahead** on in-game data tooling; **SOLTest ahead** on automated testing and the UE editor it gets for free |
| Audio | 7 synthesized placeholder combat sounds, pooled 3D voices, sound classes | Contracted weapon SFX, tension-driven music, gameplay-informative scanner and mining audio (F-320 to F-326) | **LT ahead**; SOLTest's Part 8 covers part of it |
| Modding / scripting | None | Data-level modding, LTSL scripts for UI, generators and behaviours; no mod manager (F-359 to F-370) | **LT ahead** on intent; neither had a mod loader |
| Platforms | Windows only | Windows, Linux and Mac ports shown (F-330, F-331) | **LT ahead** (see 4.13) |
| Testing / QA | 707 test macros in 51 files, smoke runs, review per part, measured perf numbers | Test-bed apps, AI combat statistics; no automated tests in any repo | **SOLTest far ahead** |

---

## 4. Feature gap list: game features

Rows list the LT feature groups SOLTest lacks; minor features are grouped. The list was checked against all 370 catalogue IDs for this revision and is believed complete for gameplay-relevant groups. LT features SOLTest already matches or exceeds are listed in 4.12. Ratings use the scale and assumptions in 1.3.

### 4.1 Flight, ship systems and travel

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Cruise mode with power trade-offs (F-004) | absent (supercruise ToDo is a different idea) | Direct | New mode in `FSOLFlightParams` and `USOLShipFlightProcessor`; HUD state | S as a speed mode; M with power trade-offs (needs the power system) | SOLTest already allows 0.5c, so "cruise" needs a design reason; overlaps the supercruise ToDo. Must stay compatible with frame carry under warp |
| Free vs locked camera, aim-steering, turrets track aim (F-005, F-006) | partial (Alt free-look exists) | Direct | Spring-arm camera modes in `ASOLShipPawn`; ship steers toward the camera aim point | S to M: a flight-control change (virtual-joystick scheme), new bindings and GAME_MECHANICS/info-screen sync, not camera code only | Turret tracking depends on turrets (4.2) |
| Power distribution, overcharge, presets, boost capacitor (F-018, F-019, F-235) | absent | Needs custom work | Power fragment on the ship entity; GameplayAbilities attributes are an option but heavy for Mass entities | M: rules plus UI plus effect on weapons, shields, thrust | Keep as plain fragments so NPC ships at scale do not need GAS components |
| Dynamic mass from cargo (F-020) | absent | Direct | Per-entity mass fragment (mass lives in the shared per-class params fragment today and must move) | S after cargo | Depends on cargo (4.3) |
| Hull and subsystem damage, repair, death and restart (F-021, F-022, F-037, F-061) | absent (collision has no damage) | Needs custom work | Health/subsystem fragments; reuse `SOLDamageMath`; respawn flow in a game-mode subsystem; bolts must also sweep against moving ships | M to L: damage model, collision damage, bolt-vs-ship hits, death UX | Death and respawn interact with jump, surface-lock, frame lock and save; collision at 0.5c needs a sane damage curve |
| Multiple ships, ship classes, buy/switch (F-013 to F-017) | absent (ARCHITECTURE §8: shared params fragment per class is the extension point) | Direct | Data Assets per ship class feeding a const shared Mass fragment; pawn re-possess keeping the pawn/entity bridge | M: class data plus switch flow; L with buying | Content-bound: needs real ship art (Part 8) |
| Hardpoints, fitting, fire groups (F-023 to F-025, F-232) | absent | Needs custom work | Socket list in a ship-class Data Asset, item instances as `FInstancedStruct` (engine core, no plugin), fitting screen | L: item model, fitting rules, UI | Item model is shared with economy and research, so design once |
| Docking and undocking, hangar view (F-009, F-010, F-238) | absent | Needs custom work | Docking as a state in the ship entity; station-relative frame lock (exists for bodies) | M: approach, lock and UI; stations must exist first | Docking happens in a moving frame (already solved for bodies by frame lock); slot allocation can reuse LT's lane allocator (8.4) |
| Dock and land prompts (F-244) | absent | Direct | HUD prompt when in range of a dockable or a colony zone | S after docking | |
| Warp lanes / warp rails with lane allocation (F-026 to F-028, F-159, F-160) | absent (jump map instead) | Needs custom work | Analytic rails between moving endpoints as a Mass fragment plus processor (8.4); ZoneGraph *(Experimental)* is a poor fit because its lanes are static world geometry | M for the player plus a few hundred riders; L with NPC AI and warp abstraction | Product decision first: rails mainly serve NPC traffic lanes, since the jump map covers player fast travel |
| Jump holes / inter-system travel, jump gates, capital jump drives (F-029 to F-031) | n/a | Poor fit as specified | Reinterpretation: charted jump-map edges, Lagrange-point gates, drive-class range limits (8.3) | M for the reinterpretation, after stations | Inter-system wormholes contradict the single-real-system design |
| Planet docking, colony landing (F-032, F-034) | planned (surface-landing ToDo, Part 9) | Needs custom work | Level streaming or a sub-level for colony scenes; or seamless landing on Part 9 terrain | L: depends on Part 9 terrain choice | LT cut seamless landing; SOLTest's scale makes it harder but Part 9 plans terrain |
| Asteroid landing and hiding (F-008), ships bouncing off asteroids (F-007) | planned (asteroid-ring-collision ToDo) | Needs custom work | Analytic swept sphere vs the minor-body grid (already exists for bolts) | M: reuse `SOLCombatGrid`; tech debt on the grid scan applies | Chaos physics is a poor fit in rebasing render space; keep analytic. Collision against large bodies is already done (4.12) |
| Discovery gating of markers (F-039), journals and bookmarks (F-044) | absent | Direct | Discovered-flag bitset per entity/body; bookmarks in the save | S each, **after save/load** | |
| Targeting tracker on reticle (F-040) | partial (bracket and edge arrow exist) | Direct | HUD addition | S | |
| Scan another ship's cargo (F-041) | absent | Needs custom work | Scanner query on a ship's cargo fragment; info panel | S after scanner and cargo | Needs NPC ships |
| Asset naming (F-038), headlights (F-045), dev teleport (F-048) | absent (jump map covers teleport-like travel) | Direct | Trivial UI/state; a spot light on the pawn | S | |
| Player/NPC symmetry (F-047) | absent (favoured by the architecture) | n/a (design rule, not a feature) | Player ship is just a Mass entity with `FSOLPlayerShipTag` (already true) and NPCs use the same actions | not sized; add about +20 to 30% to every action, market and mission feature that honours it | A principle cannot be rated as a feature |

### 4.2 Combat and weapons

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Weapon variety: missiles, guided missiles, railguns, beams (F-050 to F-053, F-313) | partial (pulse bolts only) | Direct | Extend the SoA bolt pool with kinds; beams as instant swept traces; missiles in the same SoA pool rather than a Mass archetype (SDD 7 Amendment 1's reasons apply); Niagara visuals | M for beams and rails; M to L for guided missiles | Guidance under frame carry and warp; every render-space visual inherits the TSR smear debt; balancing |
| Cooldowns, magazines, ammo in cargo (F-054, F-095) | partial (fixed fire cadence) | Direct | Fragment fields; `SOLFireCadence` extension | S after cargo | |
| Engagement range (F-062) | absent | Direct | Per-weapon range in data; AI fires only in range | S | Needs weapon variety to matter |
| Two-axis turrets (F-055) | absent | Direct | Turret transform math in pure logic; sockets on the ship mesh | M for the player; L at NPC scale (aim solved in a processor, no skeletal sockets per Mass entity) | Needs ship art with sockets |
| Hull-conforming hit-only shields, multiple shields, shield tightness (F-056 to F-060, F-314) | partial (sphere targets with shield and regen) | Direct | Shield material driven by hit positions (per-instance custom data on ISM ships, or Niagara), shield values in fragments | M | |
| Lead indicator, aim assist (F-064, F-065) | absent | Direct | Intercept solve in pure logic, HUD marker; aim assist as a setting with info text | S | Must use frame-relative velocities |
| Friend/foe colours, friendly fire, hostility (F-066, F-067, F-173) | absent | Direct | Faction id fragment, relation lookup | S; a **minimal** faction id is needed in phase B for enemies | Full factions stay in phase F |
| Cargo loot scooping (F-042) | absent | Needs custom work | Drop containers as pooled SoA records (like targets) carrying cargo; pickup by swept proximity | M after cargo and damage | Core to a pirate loop; drops ride the frame-carry scheme like targets |
| Enemy AI combat: kiting, dogfighting, AI combat test bed (F-069, F-072, F-075) | absent | Needs custom work | Near-tier behaviour (chapter 7) driving the player's flight controls; headless test maps for AI-vs-AI stats | L plus tuning | Depends on bolt-vs-ship hits and the MassAI/StateTree spike (6.8); the AI test bed is blocked by the headless runner stall |
| Fleet battles (F-076), long-range bombardment (F-063) | absent | Needs custom work | Mass archetype per ship class, tiered LOD (chapter 7), existing combat grid | L to XL at scale | Combat grid scan is a known 1M blocker |
| Heat detection, star masking (F-070, F-071) | absent | Needs custom work | Signature model in the scanner (4.4); star-direction check uses registry data | M | Real Sun position makes star masking physically meaningful |
| Size immunity, retaliation (F-073, F-074) | absent | Direct | Damage rules; faction response hook | S each | |

### 4.3 Economy and trade

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Credits, selling, rewards (F-077, F-080, F-081) | absent | Direct | Wallet fragment per agent (or per faction); player wallet in the save; single ledger functions that conserve money | S after save | LT's code created money in two places (8.6): conservation tests from day one |
| Commodities and ore types (F-083) | absent | Direct | Data Table or Data Assets of commodities, Gameplay Tags for categories | S | |
| Equipment market, ship dealer (F-078, F-079) | absent | Direct | Station inventory as SoA arrays in a market subsystem; CommonUI screens | M to L | Needs stations, the item model and the first CommonUI adoption, which must coexist with SOLTest's custom UMG focus handling and custom key rebinding (SDD 8) |
| Station storage locker (F-043) | absent | Direct | Per-player, per-station inventory record in the save | S after stations and cargo | |
| Bid/ask order books, emergent prices, average pricing, proceeds to producers (F-085 to F-089, F-091, F-092) | absent | Needs custom work | Market subsystem with per-station, per-commodity books aggregated by price level, matched on insert or a low-frequency sim-time tick; EMA price ladder; pure-logic tests (8.6) | L: matching is easy, stability is not | Test against LT's two concrete failures: the partial-fill refund leak and the 0.5% undercut ratchet. Bound outstanding orders |
| Player orders (F-090, F-101) | absent | Direct once books exist | UI over the order book | M | |
| Contract board as market (F-082), contraband (F-096) | absent | Needs custom work | Item requests as buy orders on the contract board (7.5); contraband as a commodity tag plus security rules | M each, after books and security | |
| Population demand (F-084) | absent | Needs custom work | Demand sinks per body/station (real populations are zero off Earth, so this is a design choice) | M | Fiction needed |
| NPC pricing (F-093) | absent | Needs custom work | EMA price history per book, volume-damped exploratory pricing, seeded RNG (8.6) | M | |
| Cargo by mass (F-094) | absent | Direct | Cargo fragment | S | |
| Reward-pool missions, data redemption (F-097, F-098) | absent | Needs custom work | Contract records in a subsystem, data items as cargo | M | |
| Tech licensing (F-099), personality data market (F-100) | absent | Needs custom work | Depends on research and personalities | M | |
| Economy stability controls and diagnostics (section 8.4 of SimResearch) | absent | Needs custom work | MassGameplayDebug / Gameplay Debugger category, Visual Logger, CSV/Insights traces, soak-test smoke flag | M | LT's own failures show this is not optional |
| Time-warp interaction (LT: F-036 dev time acceleration; F-148 the time-compression hack, removed later) | n/a in LT | Needs custom work | Explicit warp policy per system: carry, analytic transit (legs with departure and arrival times) or aggregate flows (7.8) | **L to XL, decided in phase B**, not E | Biggest SOLTest-specific risk: needed the moment the first NPC moves |

### 4.4 Industry: mining, scanning, production, stations, colonies

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Ore-bearing asteroids, deposits, access points (F-103, F-104, F-217) | absent (asteroids are inert, on rails) | Direct | Ore composition fragment on belt entities, seeded from the asteroid id with real spectral classes as the parent distribution (8.1) | M | Spectral data for the 500 named asteroids needs fetching and checking; depletion is persistent state, needs save |
| Transfer-unit beam mining, overflow, stress tell, blind mining, audio (F-105 to F-109, F-315, F-325) | absent | Direct | Beam as a Niagara ribbon; rate rules in pure logic (dt-invariant, unlike LT's per-frame extraction); MetaSounds parameter for mass flow | M | |
| Drone bay and prospecting drones (F-110 to F-112) | absent | Needs custom work | Drones as a small Mass archetype attached to asteroid entities | M | Drones ride an asteroid moving at ~17 km/s heliocentric; store them relative to it |
| Scanner 1.0/2.0/3.0, signature types, catalogue (F-113 to F-118) | absent | Needs custom work | Signature evaluation in a subsystem over bodies, minor bodies and ships; custom-drawn spectrum widget | L: design-heavy, LT itself iterated 3 times | Good fit with real data (star brightness, body temperatures) |
| Industrial modules, construction module, loop production, munition loading (F-119 to F-123) | absent | Needs custom work | Recipe Data Assets; factory state (idle, blocked, active, uptime) as SoA records in a low-frequency production subsystem (8.6) | M | |
| Tech Lab, Production Lab, raw material requirements (F-124 to F-126) | absent | Needs custom work | Same production subsystem; blueprint items | M after the research model | |
| Stations, asteroid bases, station services (F-127 to F-129) | absent | Needs custom work | A new dynamic-object store (the body registry is a fixed SoA table) with Kepler elements relative to a parent body (reuse `SOLKepler`), Nanite meshes, `ISOLTargetable`, radar, map icons, log-depth placement, save | L plus art | Must ride the anchor/rebasing scheme; stations near the player become Actors, far ones are data |
| Planetary colonies, colony types and transforms (F-130, F-131) | absent | Needs custom work | Colony records per body; icons via the existing map overlay | M | Colonies are fiction placed on real planets and moons |
| Player-built infrastructure (F-132) | absent | Needs custom work | Construction jobs that spawn stations into the dynamic store; persistent | L | Save format must hold dynamic bodies |
| Mining-ship class (F-133) | absent | Direct once ship classes exist | Ship-class Data Asset | S plus art | |

### 4.5 AI and simulation

Chapter 7 reviews LT's actual AI code and proposes a tiered Mass design; the rows below are rated against that design.

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| NPC ship entities with movement (prerequisite for all below) | planned (Part 10, without behaviour) | Needs custom work | Reuse the ship archetype and `USOLShipFlightProcessor` (ARCHITECTURE §8), plus ISM rendering with previous transforms and LOD, per-NPC gravity and swept collision cost, per-NPC frame carry under warp, `ISOLTargetable` and radar registration | L | The anchor is player-centric; every NPC must pass the 1M review bar. Part 10's design should include an AI control fragment, not only a stress test |
| Escort, follow, wingman, patrol, leader AI (F-134, F-135) | absent | Needs custom work | Near-tier behaviour writing the ship control fragment, either MassAI StateTree *(Experimental)* driven by hand or a custom C++ state machine in a SOLTest processor; formation slots in the leader's frame | M to L, including an integration spike | MassAI pulls in ZoneGraph, NavCorridor and PoseSearch, and its StateTree processors expect phase ticking that SOLTest does not use (6.8) |
| Abstract low-LOD simulation, multi-level abstraction (F-136, F-137, F-334) | absent | Needs custom work | Tiered model (7.7): near, local analytic legs, far event queue, aggregate flows; Mass tags per tier | XL (research): the core enabling tech for 1M agents | Essential for the 1M goal and for time warp. LT wrote a wrapper for this (`Task_LOD`) but never called it |
| Master planner, fluid goals, emergent professions (F-140, F-141, F-143) | absent | Needs custom work | K-sample utility choice over an opportunity table (7.5), not a precondition-chaining planner; LT's code never had one | L to XL | A general planner per agent at 1M is not affordable |
| Reactive AI (F-142) | absent | Needs custom work | Opportunity events via Mass signals or SOLTest's own event subsystem | L | |
| Hypothetical-simulation reasoning (F-139) | absent | Needs custom work | Optional: run the aggregate model forward on a copy, faction level only (7.5) | research / optional; not scheduled | A concept even in LT, never coded |
| Delegation, NPC-posted contracts, AI-run missions (F-145, F-154) | absent | Needs custom work | Contract board subsystem shared by player and NPCs | L | |
| Profit-driven professions, miner/pirate equilibrium (F-149, F-152) | absent | Needs custom work | Job choice by expected profit per unit time with reservations (LT's new code sampled 100 random jobs and ignored travel time); low-frequency processors | L | |
| NPC research (F-146), continuous processes (F-155) | absent | Needs custom work | NPC labs as producers in the production subsystem; continuous processes as the default shape of projects and contracts | M each, after research and contracts | |
| Faction leaders, business-strategy AI, meta projects (F-150, F-151, F-153) | absent | Needs custom work | Leader logic as plain C++ in a faction subsystem, ticking on sim-time intervals | L | LT's code split budgets four ways but only one branch did anything |
| Personality traits, birth at colonies, birthplace inference (F-156 to F-158) | absent | Direct | 7 x uint8 fragment or per-faction shared fragment plus offset; used as filters and utility weights | S to M | |
| History generation (F-138) | absent | Poor fit as a standalone system | Boot-time or offline seeded fast-forward of the aggregate economy (8.11) | S to M **after** the aggregate simulator (an XL prerequisite) | Never built in LT |
| Population scale (F-147) | partial (Mass base exists; ~19k inert entities today) | Needs custom work | Mass tiers: full AI near the player, analytic legs mid-range, aggregate far | XL toward 1M | LT's code tops out around 10^3 to 10^4 agents even if fixed (7.3); SOLTest's goal is far beyond |
| AI keeps running on script errors (F-162), leader brain view (F-161) | absent | Direct | MassGameplayDebug and MassInsights *(Beta)* for Mass agents; Gameplay Debugger and Visual Logger are Actor-oriented | M | |

### 4.6 Factions and missions

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Factions as hierarchies, named factions, pirate faction (F-164, F-170, F-172) | absent | Direct | Faction Data Assets plus a faction subsystem; faction id fragment on agents | M (data); a minimal id lands in phase B | |
| Projects with P/L, player projects, empire management (F-165 to F-169, F-268) | absent | Needs custom work | Project records with P/L metrics in the faction subsystem, updated on job-completion events; projects screen | L | |
| Security, bounty hunters (F-171) | absent | Needs custom work | Region security values (per body sphere of influence) | M | |
| Zone claims (F-174) | absent | Needs custom work | Ownership per body or region | M | |
| Diplomacy beyond hostility (F-179) | absent | Needs custom work | Relation matrix plus events | M to L | LT never built it, so no reference design |
| Missions v1 procedural (F-175, F-240) | absent | Direct | Mission templates in Data Assets; mission board UI | M, after phases B and C | Assassination, distress and conflict missions need enemy NPCs, stations and rewards |
| Continuous reward-pool missions, data proofs, player-posted missions (F-176 to F-178, F-271) | absent | Needs custom work | Contract board subsystem shared with AI (7.5) | M | |

### 4.7 Research and procedural technology

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Procedural items balanced by value, generated names (F-180, F-211 items) | absent | Needs custom work | Value-to-stats pure functions (8.7); stats as `FInstancedStruct` modifiers | M | |
| Authored tech tree (F-182, F-194, F-193) | absent | Direct | Data Assets with prerequisites; a runtime graph view needs custom drawing | M to L | |
| Procedural modifier tech, blueprint derivation, generations, attributes (F-183 to F-189, F-192) | absent | Needs custom work | Base item plus modifier list; product-preserving two-attribute mutation from a seeded stream; lineage saved, not stats; bounded walk with tests (8.7) | M for derivation; L with labs, costed research and a tree UI | LT's code version is an unbounded, unseeded random walk; balancing is open-ended |
| Assembly chips, physical technology (F-190, F-191) | absent | Needs custom work | Items in cargo; theft via combat drops | M after cargo and research | |
| Research colonies and projects (F-196) | absent | Needs custom work | Depends on colonies and projects | M | |

### 4.8 Universe and procedural generation

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Procedural nebulae, dusty vs clear systems (F-197, F-216) | n/a (real Milky Way sky from catalogue) | Poor fit (drop) | Spin-off: irradiance from SOLTest's real sky; optional zodiacal light (8.2) | S to M for the spin-offs | A fictional nebula contradicts the real-sky design |
| Procedural asteroids with craters, ice asteroids, ice subsurface (F-199, F-203, F-302) | partial (placeholder sphere ISM, 2 variants) | Direct | Offline-generated Nanite rock meshes (Modeling Tools *(Beta)* or Houdini), a few dozen variants, subsurface material for icy bodies | M | Plus the belt LOD debt, the per-frame ISM upload debt and collision-shape consistency |
| Infinite streamed fields, density modulation, Zone Tech (F-200 to F-202) | partial (belt and streamed ring rocks are the SOLTest equivalent) | Needs custom work | Extend the ring-patch streaming design to belt clusters | M to L | The ring-patch streaming took most of Part 5e with 16+ amendments; real belt density is very low |
| Procedural planets, planet surfaces, water (F-204, F-205, F-301) | planned (Part 9) | Needs custom work | Part 9 cube-sphere terrain (PCG/Nanite or virtual heightfield) | XL | Already the hardest roadmap part |
| Planetary rings (F-207) | **present and ahead** | — | — | — | See 4.12 |
| Universe hierarchy, infinite universe, system layout (F-208 to F-210) | n/a | Poor fit (drop) | Reusable kernels only: jittered double-MST sparse graph, seeded hierarchical distributions (8.1) | S for the kernels | Single real system by design |
| Name generators (F-211) | absent | Direct | Grammar tables in Data Tables | S | For stations, NPCs, factions, ships |
| Procedural ships, stations, turrets (F-212, F-213, F-221) | absent (placeholder primitive ship) | Needs custom work | Offline Geometry Script generator baking Nanite variants per type; raycast socket bake; PCG for greebles and station modules (8.8) | L for a generator; S for the socket bake; M to L for stations | Runtime generation is a poor fit; Part 8 art may make it unnecessary |
| Spatial coherence, colony cultures (F-214, F-215) | absent | Direct | Seeded noise over body/region | S after colonies | |
| History-driven placement (F-222) | absent | Poor fit as a standalone system | Fast-forward of the aggregate economy (8.11) | S to M after the aggregate simulator | |
| Procedural icons (F-219) | absent | Direct | Material-based icon generator or render target | S | |
| Procedural UI layout from data (F-220) | absent | Needs custom work | Data-driven layout of whole screens | M to L | Low value next to CommonUI |

### 4.9 UI and UX

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| New-game setup, name generator (F-012, F-223) | partial (start-location picker exists) | Direct | Extend main menu | S after save slots | |
| Loading screen (F-273) | absent | Direct | Movie/loading-screen module or a UMG overlay during boot and level loads | S | Becomes needed once boot-time simulation (8.11) or terrain generation exists |
| Top bar / player menu, personal interface (F-225, F-267) | absent | Direct | CommonUI activatable widget stack | M to L | First CommonUI adoption: must coexist with the C++ UMG menus, custom focus logic and custom rebinding |
| Command interface (RTS), asset panel, leader-follower groups (F-226 to F-230) | absent | Needs custom work | Reuse the jump-map camera and overlay for a tactical view; selection and orders over Mass entities | L | Map already renders at any scale, a strong starting point (8.9) |
| Ship viewer, inventory, cargo, power UI (F-231 to F-235) | absent | Direct | UMG/CommonUI, a scene-capture ship preview kept away from the rebasing gameplay scene | M | |
| Research and production UIs (F-270) | absent | Direct | CommonUI screens over the production and research subsystems | M after those subsystems | |
| Ships menu, job board, contract board, missions, database screens (F-239, F-240, F-254, F-271) | absent | Direct | CommonUI lists bound via MVVM *(Beta)* | M each | |
| Sortable lists (F-283) | absent | Direct | List view sorting in CommonUI/MVVM | S | |
| HUD v1 widgets, radial subsystem rings, mining widgets, minimap (F-241, F-242, F-262, F-282) | partial (Canvas HUD and 3D radar exist) | Direct | Extend `ASOLFlightHud` | S to M | `Ship/`-`UI/` include cycle should be broken first |
| Zone indicator (F-272) | absent | Direct | HUD text from the anchor body or region | S | |
| HUD customisation, object overview, UI finalisation (F-286) | absent | Direct | Settings entries plus layout data | M | The HUD is Canvas, so per-element layout needs a data layer |
| Commodity market screen and trading charts (F-264, F-269, F-281) | absent | Direct | Slate custom-drawn chart widget; CommonUI market screen | M | |
| System map, hierarchical map (F-250, F-251) | partial (jump map is a live system map) | Direct | Add trade-route, ownership and NPC layers to the map | M | SOLTest's map is already ahead in scale range |
| Colony info menu (F-279) | absent | Direct | Panel over colony records | S after colonies | |
| Split panels, workspaces, pop-outs, categorical filters (F-252, F-253, F-258, F-259) | absent | Direct | CommonUI/UMG layout | M | |
| Stack/layer UI (F-274) | absent | Direct | CommonUI activatable stack (parents visible) | S to M with the top-bar work | LT itself cut it |
| Cylindrical/curved HUD, glass HUD, cockpit mode, UI compositing (F-255 to F-257, F-280, F-307), 3D tilted UI (F-247) | absent | Direct | Widget Component on a curved mesh, or a post-process distortion of the UI layer | M to L | The flight HUD is Canvas (`AHUD`), so a curved or tilted HUD means rebuilding it as widgets |
| Radial context menu, object info window, world-space overlays (F-260, F-276, F-277, F-278) | absent | Direct | UMG with world-to-screen projection from universe positions (the HUD already projects targets) | M | |
| UI colour picker, opacity (F-236, F-237) | absent | Direct | Settings entries | S | Persist via the settings save |
| UI-to-texture (F-285) | absent | Direct | Widget Component / render target | S | |
| Gamepad and joystick (F-248) | absent (keyboard and mouse only) | Needs custom work | Extend `SOLKeyBindings` to gamepad keys, an analogue flight scheme beside the mouse virtual joystick, menu focus for gamepad | M to L | SOLTest's rebinding is custom, not Enhanced Input user settings, and SDD 8 says gamepad keys are not handled specially |
| VR (F-249) | absent | Poor fit | OpenXR exists, but stereo needs rework of the render-origin and log-depth placement, the camera-locked star renderer, the Canvas HUD, warp FOV effects and TSR assumptions | XL | Never shown in LT either |

### 4.10 Rendering and effects

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Lens flares (F-299), god rays (F-295) | planned (Sun glow and flare ToDo) | Direct | Post-process lens flare, custom flare material, light-shaft settings | S to M | Game-driven exposure must stay in control |
| Space dust, volumetric dust, dust transitions, motion flecks (F-294, F-296, F-297, F-303) | absent | Direct | Niagara camera-relative particles; Heterogeneous Volumes (present, `r.HeterogeneousVolumes` defaults on) for dust, suitability unverified | M | Real interplanetary space has no visible dust; flecks only as a speed cue |
| Dynamic lights from weapons and thrusters (F-298) | partial (combat VFX are emissive; ship/targets lit by a CPU sunlight light) | Direct | Niagara light renderer for a capped number of bolts; MegaLights exists but is experimental, needs hardware ray tracing and is off by default | S to M | With up to 20k bolts, cap the lit ones |
| Engine trails (F-309), cruise glow | absent | Direct | Niagara ribbons in ship frame | S to M | Trails in render space hit the open TSR smear debt; needs previous-position work |
| Impostors, popping fix (F-310, F-311) | partial (log-depth placement, body icon LOD) | Direct | Nanite plus HLOD or octahedral impostors for stations and fleets | M | |
| Atmospheric scattering, clouds (F-300) | planned (Part 9); gas giants already volumetric | Needs custom work | Custom atmosphere shells for rocky planets | L, as part of Part 9's XL | **Known constraint**, not an assumption: the renderer supports one atmosphere and one cloud layer per scene (ARCHITECTURE, SDD 14), and SOLTest already uses the one SkyAtmosphere as the gas-giant cloud carrier |
| Holographic look, glitch effect, procedural plating (F-290, F-308, F-263) | absent | Direct | Materials (LT's hologram recipe reviewed in 8.9) | S to M | |
| Native text effects (F-318) | absent | Direct | UMG/Slate font outline and shadow | S | |
| Chromatic aberration, colour grading, bloom, HDR, PBR, AA (F-291 to F-293, F-304 to F-306) | present via UE (TSR, post-process); jump effect uses chromatic aberration | — | — | — | Engine-provided |
| Mining, shield, warp VFX (F-314 to F-316) | partial (shield-break and warp effects exist) | Direct | Niagara | S each | |

### 4.11 Audio

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Real weapon and explosion SFX (F-320, F-326) | partial (7 synthesized placeholders) | Direct | Licensed or authored assets; MetaSounds | S to M (content) | Part 8 |
| Dynamic tension music, composer soundtrack (F-321, F-322), tension debug slider (F-287) | absent (music sound class exists, empty) | Direct | MetaSounds with a tension parameter, Quartz for musical timing, Audio Modulation; a console variable or debug slider for tension | M code plus content (a composer) | |
| Scanner, mining, zone ambience audio (F-323 to F-325) | absent | Direct | MetaSounds driven by game parameters | S each | Depends on scanner and mining |

### 4.12 LT features SOLTest already matches or exceeds (for completeness)

| LT feature (F-IDs) | SOLTest equivalent |
|---|---|
| Third-person flight, controls, roll (F-001 to F-003) | 6DoF with chase cam, rate-command quaternion rotation |
| Collision against large bodies (part of F-007; bouncing off asteroids is a gap, see 4.1) | Analytic swept sphere against bodies, no tunnelling at 0.5c, never disabled |
| Time acceleration (F-036) | First-class warp ladder to 30 d/s with frame carry |
| Scale (F-206) and realistic scale as a mod (F-360) | True 1:1 scale is the base design |
| Double precision, dynamic loading (F-332, F-333) | `FVector3d` universe, rebasing, streamed ring rocks |
| Starfield (F-198) | Real AT-HYG sky with physical magnitudes |
| Planetary rings (F-207) | Real optical-depth profile, 1M-rock layer, analytic shadows |
| Pulse lasers, explosions (F-049, F-312): **partially** | Pulse bolts at 20k scale, explosion, debris and shield-break effects; bolts do not emit dynamic light (see 4.10) |
| Settings, key rebinding (F-224, F-369) | 29 rebindable actions with conflict checks, live key labels |
| Unicode text (F-337) | Engine-native |
| Profiling (F-339) | `stat`, Unreal Insights, measured numbers per part |

### 4.13 Platforms

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Linux and Mac ports (F-330, F-331) | absent (Windows only) | Needs custom work | UE's Linux and Mac targets; verify custom shaders (`Shaders/Private/*.ush`), Niagara GPU simulations, Python bake tools and the test runner on each platform | M (Linux) to L (Mac, Metal shader and GPU-feature differences) | Not checked on either platform; volumetric clouds and the 1M-particle ring layer may need per-platform scalability settings |

---

## 5. Feature gap list: editor and tooling features

SOLTest's editor side today: Python scripts that build materials and assets headlessly, a generated shader include, one Data Asset type (`USOLStarFieldData`), one editor-only function library, unreal-mcp for driving the editor, `-SOL*` smoke flags, automation tests and two A/B console variables (`sol.RingShadows`, `sol.OrbitLines`). There is no custom editor module. Note that PythonScriptPlugin and EditorScriptingUtilities, which this tooling already relies on, are *(Beta)* in 5.8.

Unlike LT, SOLTest starts from the Unreal Editor, which already provides most of what LT built by hand (reflection, a generic property inspector, asset browser, undo/redo, source control, packaging). Chapter 8.10 confirms from source that LT's Data Editor is not in any repo.

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Reflective data model (F-336) | present via UE reflection (`UCLASS`/`USTRUCT`/`UPROPERTY`) | Direct | Already available | — | LT built this from scratch; UE gives it free |
| LT Data Editor: generic inspector, live editing, no special-case code (F-350 to F-352, F-356) | partial (Details panel works on any asset; SOLTest keeps tunables as C++ constants in `SOLConstants.h`) | Direct | Move designer-facing tunables into Data Assets / Data Tables / `UDeveloperSettings`; Property Matrix for bulk editing | M: the Info screen is generated from `SOLConstants.h` and `FSOLFlightParams` (SDD 8), tests reference the constants, and Mass worker threads must read cached copies rather than "read each use" | A mix is likely best (open question 9.1) |
| Graph layouts where shape = type (F-353 to F-355) | absent | Needs custom work | Custom `EdGraph`-based asset editor, or an Editor Utility Widget that draws a graph | M to L, low value | Keep only for data that really is a graph (tech tree, factions, trade network) (8.10) |
| Data-editor operations LT never had: undo/redo, save, validation, search, dependencies, versioning (SimResearch 18.5) | present via UE editor (transactions, asset save, Reference Viewer, source control) | Direct | Add Data Validation rules (`UEditorValidatorBase`, plugin enabled by default) for content | S to M | |
| In-game data inspection (F-357) | absent | Direct | Gameplay Debugger categories; console commands; Mass debugger in the editor | S to M | |
| LTSL scripting language (F-341, F-362, F-363) | absent | Poor fit (as a language) | StateTree for behaviour, Data Assets for generator parameters, Blueprints for designer logic, C++ for systems, Python for editor automation (8.5). Verse compiler infrastructure ships in 5.8, but its support for non-UEFN projects was not verified: do not plan on it | n/a | A custom language would duplicate UE; LT's reasons (no editor, no reflection) do not apply |
| In-game script editor (F-365) | absent | Poor fit | — | n/a | Follows from dropping a custom language |
| Hot reload (F-342) | partial (Live Coding exists; CLAUDE.md does not rely on it and closes the editor to build) | Direct | Live Coding for C++ body changes; Blueprint and StateTree recompile in PIE; Data Assets live; regenerate-on-edit delegates for generators (8.5) | S to adopt (where safe) | Live Coding cannot change reflected layouts safely; full builds remain needed |
| Soft compile failure, AI keeps running (F-343, F-162) | absent | Poor fit | Blueprint compile errors already isolate assets; C++ has no equivalent | n/a | Not worth emulating; LT's own version would crash on some paths (8.5) |
| Compiler errors in game (F-364) | n/a | n/a | Message Log covers editor-side errors | — | |
| Launcher architecture, apps as scripts (F-344) | partial (`-SOL*` scenario flags select behaviours) | Direct | Command-line flags (existing), separate test maps, commandlets | S | |
| Test beds: AI combat stats, market, rail, UI, HUD (F-345, F-075) | partial (smoke runs: flight, input, HUD, map, jump, level, menus, combat demo and stress, stars) | Direct | Dedicated test maps plus Functional Tests or `-SOL*` smoke flags; Gauntlet *(Beta)* for long soak runs | S to M per bed, **plus fixing the headless runner first** (0.5 to 2 person-weeks, root cause unknown) | Unattended AI and economy soak tests are blocked until the runner works |
| 3D model test bed for generators (F-346) | partial (Python scripts regenerate assets; Modeling Tools enabled) | Direct | Editor Utility Widget that reruns a Geometry Script or PCG generator with parameters in a preview viewport | M | Only needed if procedural ships/stations are chosen |
| Procedural model editor with symmetry (old-code `ModelEditor`) | absent | Direct | Modeling Tools *(Beta)* / Geometry Script | M | |
| Dev tools menu: steal asset, transfer, jump to object (F-347) | partial (jump map; console smoke flags) | Direct | `UCheatManager` subclass, console commands, an in-game debug UMG panel | S | |
| Dev region viewer, system view with AI on rails (F-243, F-251 debug) | partial (jump map shows all bodies at any zoom) | Direct | Add debug layers to the map (agent positions, routes, trade flows) | M | |
| Tech-tree viewer and editor (F-193, F-194) | absent | Needs custom work | EdGraph asset editor for authored trees; runtime graph view for players | M to L | Only if research is adopted |
| AI introspection: goal inspector, decision trace, leader brain view (F-161, SimResearch 10.4) | absent | Direct | MassGameplayDebug, MassAIDebug, MassInsights *(Beta)*, StateTree debugger, Rewind Debugger (part of GameplayInsights), Visual Logger | S to M to wire up | Requires AI to exist |
| Economy diagnostics: price spikes, flow visualisation (SimResearch 8.4) | absent | Needs custom work | Visual Logger, Gameplay Debugger, CSV export, Insights trace channels, map overlay | M | |
| Seed capture and replay, deterministic scenarios (SimResearch 10.4, 13.3) | partial (universe is deterministic from sim time; `-SOLSimDate`, `-SOLStart` scenario flags) | Needs custom work | Seed and sim time in save files; seeded per-agent RNG; fixed-order claim resolution | M to L | Player flight uses frame-rate-dependent substeps, and Mass parallel chunks plus float non-associativity break bitwise replay; only the analytic universe is deterministic for free |
| Optimisation pass (F-349) | present as a process (adversarial performance review per part, measured numbers) | Direct | Continue per part; Insights trace channels for AI and economy | — | |
| Profiling (F-339) | present (`stat`, Insights, measured per part) | Direct | Add Insights trace channels for AI and economy; MassInsights *(Beta)* | S | |
| Data-level modding, script modding, moddable UI, mod sharing (F-359 to F-361, F-366, F-370) | absent | Needs custom work | Game Feature plugins *(Beta)* or pak/IoStore mounting for content mods; Data Asset overrides; a UGC plugin; Blueprint-only mods via a mod SDK project | L to XL | Decide early whether it is a goal |
| Unicode text, fonts (F-337, F-367) | present via UE | — | — | — | |
| Capture and recording (F-348) | partial (`-SOLSmokeShot` screenshots) | Direct | Movie Render Queue, Take Recorder | S | |
| Patcher / updater (old code) | absent | Direct | Platform store patching or UE chunk patching | M | Only for distribution |

---

## 6. Can all of LT's features be added in Unreal? Assessment and adoption order

### 6.1 Short answer

**Yes, almost all of them can be built in Unreal 5.8 as designs, but very few come for free, LT's code is not a reference to port, and the work is larger than the first version of this document said.**

- **The hard part is not the engine.** LT's gameplay systems (economy, AI, factions, research, missions) are game logic. No engine provides them. In UE they would be hand-written on a far better base: reflection, an editor, Mass, StateTree, Smart Objects, Niagara, CommonUI, Data Assets, SaveGame and Insights all exist.
- **LT's code is much smaller and weaker than its videos** (chapters 7 and 8). The planner, hypothetical simulation, abstract simulation, AI-to-AI missions and the data editor are not in any public repo. What is there is prototype quality, polls every agent every frame, and contains money-creating and crash bugs. SOLTest should adopt LT's **ideas** (task descriptors, K-sample utility choice, P/L projects, reward-pool contracts, EMA price ladders, flow-pressure aggregation, lane allocation) and **redesign** everything else around tiers and time warp.
- **The engine's own AI stack is less ready than assumed.** MassGameplay, MassAI and ZoneGraph are *(Experimental)* in 5.8, MassAI pulls in a chain of plugins (6.8), and its StateTree processors assume Mass phase ticking that SOLTest deliberately does not use. Near-tier AI therefore starts with a spike, not "Direct".
- **Poor fits are fewer and narrower** after the source review (chapter 8). Truly dropped: the procedural universe hierarchy, fictional nebulae, inter-system wormholes, LTSL as a language, a custom graph data editor, and a per-agent order-book economy at ~1M agents. Most of the others have small reusable kernels.
- **Size, as estimates (6.5):** the pilot loop (phases A to C2) is about **38 to 74 person-weeks** (roughly 9 to 17 months for one developer under the 1.3 assumptions), not "a few months". Everything through phase F is roughly **87 to 169 person-weeks** (about 2 to 3.5+ years). Confidence falls sharply after phase C.

### 6.2 Classification

| Category | LT features | Why |
|---|---|---|
| **Fits directly** (stock UE plus modest game code) | Cruise as a speed mode, weapon variety, player turrets, shields, lead indicator, engagement range, ship classes, cargo by mass, credits, commodities, missions v1 (after B and C), name generators, personalities, UI screens (market, cargo, ship viewer, mission board), HUD additions, lens flares, dust, impostors, audio and music, dev menus, AI introspection tools | UE has the building blocks; SOLTest's pure-logic-plus-glue pattern applies, still paying the cross-cutting costs in 6.4 |
| **Needs substantial custom work** | NPC ship entities, escort and combat AI (with an engine spike), damage with bolt-vs-ship hits, stations and a dynamic object store, docking in moving frames, mining with persistent deposits, scanner, order-book economy, NPC professions, faction leaders and projects, contracts shared by player and AI, production chains, procedural blueprint research, fleet command view, tiered abstract simulation, time-warp policy, save/load of a living universe, gamepad, rocky-planet atmospheres, seed replay, analytic warp rails, Linux/Mac, modding | Engine gives primitives only; the systems are the game. Most must be adapted to double-precision universe coordinates, Keplerian frames, render-origin rebasing and time warp |
| **Poor fit or not worth it** | Procedural nebulae and procedural universe hierarchy, inter-system jump holes, LTSL and an in-game script editor, soft compile failure, a custom graph data editor, a generic node-graph game UI, VR, runtime procedural ship meshes, history generation **as a standalone system**, a fully emergent per-agent economy at ~1M agents | Contradicts the real-Solar-System design, duplicates UE features, or is computationally implausible. History generation is cheap later as a fast-forward of the aggregate simulator (8.11), and several dropped features leave reusable kernels (chapter 8) |

### 6.3 Specific judgements

- **LTSL vs Blueprints, C++ and StateTree.** LTSL existed because LT had no editor, no reflection and no hot reload. UE provides all three. The LTSL roles map onto StateTree (behaviour logic), Data Assets (generator parameters), Blueprints (designer scripting, if wanted), C++ with Live Coding (systems) and Python (editor automation, already used). Do not build a language. If modding with logic becomes a goal, expose a curated Blueprint API. Details in 8.5.
- **Procedural tech tree.** Feasible as pure logic (base item plus modifier list, seeded and bounded derivation). LT's code version is unseeded and unbounded and has no research cost. Treat as late, optional depth (8.7).
- **Fully emergent economy at scale.** LT's videos show about 100 to 128 economically active NPCs per system, and 2,000+ ships were reported for the PAX South 2018 demo build, which is not in the released repo. The released new code's economic AI is idle in its shipped configuration (7.4). SOLTest's goal is a future "~1,000,000 animated ships or characters" (SDD 1). A per-agent planner or order-book participant at that count is not plausible (estimate). The plausible design is tiered (7.7): full behaviour near the player, analytic legs mid-range, an event queue far away and aggregate flows for everything at high warp.
- **Time warp is SOLTest's unique complication**, and it bites at the first NPC, not at the economy phase. At 30 days per second, one job per agent per game-hour for 1M agents is about 7.2 x 10^8 events per real second (7.8). Every NPC, missile, drone, station interaction and economy tick needs an explicit warp policy (carry, analytic transit, aggregate flow, or suspend). This is now in phase B.
- **Floating origin and engine systems.** Stations and NPC ships live in universe coordinates with Actors only near the viewpoint, like bodies today. Engine systems that cache world positions (ZoneGraph, NavCorridor, MassMovement, the MassLOD viewer info, the Smart Object spatial index, Chaos scenes) go stale at every 10 km rebase, which happens every frame at high speed, and the universe exceeds LWC's ~±88 million km. They need wrapping or must be avoided. Smart Objects can still be used as data (slots on a station).
- **Mass for everything.** NPC ships fit the existing ship archetype (ARCHITECTURE §8). Markets, factions and contracts are better as SoA data in subsystems than as entities, the same deviation SDD 7 Amendment 1 accepted for bolts and targets. That deviation should be written down once in an SDD rather than repeated case by case.

### 6.4 Cross-cutting cost factors the row ratings do not show

These apply on top of individual rows; the person-week ranges in 6.5 and 6.7 include them.

| Factor | Effect |
|---|---|
| Time warp | Every moving or ticking gameplay system needs a warp policy and tests at warp; first needed in phase B |
| Render-origin and motion-vector smear | Every new render-space visual (NPC ships, missiles, beams, trails, drones, dust) inherits the open TSR/motion-blur smear debt (fixed only for the target ISM). An "S" visual is really "S plus previous-position work" |
| Save/load | Every stateful row depends on it. CLAUDE.md requires a versioned schema with migration, writes only at lifecycle boundaries, async saves and dirty checks. A living world saved only at boundaries can lose a lot of play on a crash: a design tension to resolve in phase A |
| 1M review bar | Every system must pass the adversarial "scales to ~1M at frame rate" review, so even UI-adjacent systems (markets, contracts) are SoA design work |
| Process overhead | Already in the observed pace, but each "S" ticket pays ~0.5 to 1 day unless bundled |
| Tuning | Gameplay rows carry +50 to 100% (1.3) because they need human playtest loops |
| Content | Ship classes, stations, turrets, music, SFX, mission text, item data and icons are content-bound; AI assistance speeds code, not art |
| Headless test runner | Broken with unknown root cause; blocks unattended soak tests for AI and economy. Fixed in phase A |
| Engine maturity | Experimental and Beta plugins (6.8) bring API churn across engine versions, made costlier by the "no deprecated APIs" rule |

### 6.5 Overall difficulty (estimate)

| Scope | Size | Person-weeks (estimate) | Confidence | Comment |
|---|---|---|---|---|
| LT's pilot loop (phases A, B, C, C2: save, damage, NPC enemies with warp policy, stations, docking, items, fixed-price market, ship classes, missions v1, ship systems and weapons) | XL | **38 to 74** (~9 to 17 months) | Med to Low | Not "L, a few months": its own phase sizes add up to far more. Still less than LT's 2013 prototype in places (no RTS command view or fleet battles) |
| LT's living world (phases D, E, F: mining and scanning, NPC professions, order books, aggregate simulation, factions, contracts) | XL | **49 to 95+** | Low | The core of LT; balancing and debugging dominate |
| LT's depth layer (phase G: production, procedural research, colonies, fleet command, personalities) | XL | **26 to 52+** | Low | Open-ended |
| All of the above at ~1M entities | XL+ | not estimable | Low | Research-grade: requires the aggregate simulator and closing the scale tech debt; no reference implementation exists, LT's included |
| Tooling (Data Asset migration, debug views, test beds, validation) | M to L | **3 to 8** | Med | Plus the headless runner fix counted in phase A |

### 6.6 Dependencies

```
Save/load (sim time, player, persistent changes)          [phase A]
   └─ needed by everything with state: discovery, wallets, deposits, stations, factions, research, NPCs

Headless test runner fix                                  [phase A]
   └─ unattended AI and economy soak tests

NPC ship entities (Part 10 archetype + AI control fragment)            [phase B]
   ├─ warp policy (carry / analytic legs / aggregate) ── decided here, used by everything that moves
   ├─ near-tier behaviour (MassAI StateTree spike vs custom processor)
   ├─ escort/patrol/combat AI ──── enemies, fleet battles, fleet command view
   └─ professions (miner, trader, pirate)
          └─ needs: stations, commodities, cargo, mining, markets

Ship damage + bolt-vs-ship hits + minimal faction id ── enemies that shoot back ── piracy, security

Stations (dynamic object store, Kepler-bound, targetable, dockable)    [phase C]
   ├─ docking ── station services ── equipment market, ship dealer, repair
   ├─ markets (fixed price first, then order books)
   └─ contracts/mission board ── factions and projects

Item model (FInstancedStruct items, cargo)                [phase C]
   └─ fitting, power, turrets, loot, production, blueprints, research   [C2, D, G]

Asteroid composition + collision + targeting (ToDo items)
   └─ scanner, drones ── beam mining ── commodities flow into markets   [phase D]

Pure-logic economy core + opportunity table (AI stages B, C in 7.9)
   └─ tiered agents (far, local, near) ── economy at scale, warp with a living economy, the 1M goal   [phase E]
```

### 6.7 Recommended adoption order (phases), mapped onto the roadmap

The order follows LT's own lesson (SimResearch 20.2): build the gameplay vertical slice before more engine polish, keep phases strictly ordered, and decide the hardest SOLTest-specific constraint (warp) before NPCs exist. All ranges are **estimates** under the assumptions in 1.3 (one developer with AI subagents, content excluded, tuning allowance included); they are not schedules.

| Phase | Content | Maps onto | Prerequisites | Size | Person-weeks | Confidence (why) |
|---|---|---|---|---|---|---|
| **A. Foundations for a game** | Save v1 (sim time, player state, schema-versioned per CLAUDE.md persistence rules) 1-2; ship damage, death and respawn 1-2; minor-body collision and targeting (existing ToDo items) 1-2; close the combat-grid scan tech debt 1-2; break the `Ship/`-`UI/` cycle ~0.5; **fix the headless test runner** 0.5-2 | New tickets; two ToDo items | none | L | **4 to 8** | Med: technical work like past parts, but the runner's root cause is unknown |
| **B. NPC ships with behaviour** | AI SDD with tiers and warp policy first (7.9 stage A); NPC archetype and rendering with previous transforms 2-3; **warp policy and analytic transit 2-4** (moved from E); near-tier behaviour spike: MassAI StateTree driven by hand vs a custom C++ state machine 1-3; escort, patrol, attack 2-3; bolt-vs-ship hits 1-2; **minimal faction id and hostility** 0.5-1; lead indicator; Mass debugging categories 1; tuning 2-4 | Part 10 (#11), reshaped from "stress demo" to "NPC foundation plus stress demo" | A | L to XL | **12 to 20** | Low to Med: engine spike outcome unknown; combat AI needs tuning |
| **C. Places and items** | Stations and dynamic object store 3-5; docking and UI 2-3; item model, cargo, minimal fitting 3-5; credits, fixed-price station market and CommonUI adoption 3-5; ship classes as Data Assets 1-2; mission board v1 2-4; save schema growth 1-2 | New tickets; Part 8 art for ships and stations | A, B | XL | **15 to 26** | Med: well-understood systems, but first CommonUI adoption and art dependency |
| **C2. Ship systems and weapons** | Power distribution 1-3; cruise mode with power trade-offs 0.5-1; free/locked camera with aim-steering 0.5-1.5; player turrets 1-3; hull-conforming shields 1-3; weapon variety (beams, rails 1-3; guided missiles 2-5); repair (with damage from A) | New tickets | A, C (item model, fitting) | L | **7 to 20** | Med: player-side systems similar to Part 6, balancing adds spread |
| **D. Industry loop** | Asteroid composition data 1-2; scanner 4-8; prospecting drones 2-3; beam mining 2-3; deposits and save 1; market hook 1 | New tickets; builds on Part 5's belt data | C | L to XL | **11 to 19** | Med to Low: scanner design is open-ended (LT iterated three times) |
| **E. Living economy** | Pure-logic economy core with conservation tests and the opportunity table (7.9 stages B, C); far and local tiers with a 100k/1M headless benchmark (stages D, E); NPC miners, haulers, traders, pirates choosing jobs by profit per time; order books with bounded orders; price history; economy diagnostics | New tickets; extends Part 10's scale goal | B, C, D | XL | **26 to 52+** | Low: research-grade aggregate simulation; stability tuning dominates |
| **F. Factions and contracts** | Full factions, projects with P/L, NPC-posted contracts and reward-pool missions shared with the player, security, zone claims | New tickets | E | L to XL | **12 to 24** | Low: depends on E's stability |
| **G. Depth** | Production chains, procedural research and blueprints, colonies, fleet command view on the jump-map camera, personalities in utility | New tickets | F | XL | **26 to 52+** | Low: open-ended balancing |
| **Parallel tracks** | Part 8 (art, audio, music with a tension parameter; content excluded from these ranges); Part 9 (terrain and atmospheres, enabling colony landing) 12-30; tooling (Data Asset migration, test maps, validation rules) 3-8; presentation features (lens flare, dust flecks, trails) | Parts 8 and 9, ToDo items | independent | per part | see left | Part 9 Low (multi-atmosphere constraint); tooling Med |

Phases A to C2 give a playable pilot loop: flying, fighting NPCs that fight back, docking, trading at fixed prices, fitting a ship and taking simple missions. That covers much of LT's 2013 prototype but not all of it (the prototype also showed the RTS command view and fleet battles, which arrive in G and later). Phases D to F are LT's core identity. Phase G is where LT itself stalled.

**Alternative ordering.** The AI source review (7.9) suggests a simulation-first path: build the pure-logic economy core and the far tiers (benchmarked at 1M) before the near-tier behaviour. That proves the 1M goal early and makes near-tier AI a view onto an existing simulation, at the cost of a later playable loop. The table above keeps a near-tier-first order but moves the AI SDD (tiers, warp policy, conservation) to the start of phase B so the near tier is designed against the tiers. Which order to use is open question 9.1.

### 6.8 Plugin maturity and dependencies (UE 5.8 local install)

Maturity comes from the `IsBetaVersion` / `IsExperimentalVersion` flags in each `.uplugin`; "none" means neither flag is set.

| System | Location in the 5.8 install | Maturity | Used in this document for |
|---|---|---|---|
| MassEntity, MassCore, MassSignals | engine modules (`Runtime/MassEntity`, `Runtime/Mass/*`) | engine | Already used (MassEntity, MassCore) |
| MassGameplay (MassLOD, MassSimulation, MassSpawner, MassRepresentation, MassMovement, MassCommon, MassActors, MassSmartObjects) | `Plugins/Runtime/MassGameplay` | **Experimental** | Tier LOD, spawning, representation |
| MassAI (MassAIBehavior with the StateTree processors, MassNavigation, MassZoneGraphNavigation) | `Plugins/AI/MassAI` | **Experimental** | Near-tier behaviour |
| MassCrowd | `Plugins/AI/MassCrowd` | Experimental | not proposed |
| MassInsights | `Plugins/MassInsights` | Beta | Profiling Mass agents |
| StateTree, GameplayStateTree | `Plugins/Runtime/` | none | Behaviour logic |
| SmartObjects | `Plugins/Runtime/SmartObjects` | none | Station slots as data |
| ZoneGraph | `Plugins/Runtime/ZoneGraph` | **Experimental** | Rejected for rails (static lanes) |
| GameplayAbilities | `Plugins/Runtime/GameplayAbilities` | none | Not recommended for Mass agents |
| CommonUI | `Plugins/Runtime/CommonUI` | none (not enabled by default) | Strategic screens |
| ModelViewViewModel (MVVM) | `Plugins/Runtime/ModelViewViewModel` | **Beta** | List and screen binding |
| PCG | `Plugins/PCG` | none (enabled by default); PCGGeometryScriptInterop Beta | Greebles, station modules, terrain |
| GameFeatures, ModularGameplay, DataRegistry | `Plugins/Runtime/` | **Beta** | Modding |
| Gauntlet | `Plugins/Experimental/Gauntlet` | **Beta** | Soak runs |
| ModelingToolsEditorMode; GeometryScripting | `Plugins/Editor/`; `Plugins/Runtime/` | **Beta**; none | Procedural meshes offline |
| PythonScriptPlugin, EditorScriptingUtilities | `Plugins/Experimental/`, `Plugins/Editor/` | **Beta** | Already relied on by SOLTest tooling |
| Rewind Debugger | inside GameplayInsights (not a standalone plugin) | none | AI introspection |
| DataValidation (`UEditorValidatorBase`) | `Plugins/Editor/DataValidation` | none (enabled by default) | Content validation |
| OpenXR, MetaSounds, AudioModulation, Movie Render Queue, Take Recorder (Takes), Chooser | various | none | VR, audio, capture |
| StructUtils plugin | `Plugins/Experimental/StructUtils` | Experimental | **Not needed**: `FInstancedStruct` is in CoreUObject |
| MegaLights | renderer, `r.MegaLights.EnableForProject` (default off) | experimental per its help text; needs hardware ray tracing, no directional lights | Weapon lights (capped) |
| Heterogeneous Volumes | renderer, `r.HeterogeneousVolumes` (default on) | not readable from source | Dust (suitability unverified) |
| Verse | `Runtime/VerseCompiler`, Verse-capable test plugin descriptors | n/a | Not planned on: support for non-UEFN projects unverified |

**What enabling MassAI pulls in.** SOLTest today enables only ModelingToolsEditorMode, ModelContextProtocol and AllToolsets in the `.uproject`, and links only MassEntity and MassCore among the Mass modules. Phase B with the engine's AI stack would need (estimate from the module dependency lists):

- `.uproject`: enable MassGameplay and MassAI, which transitively enable StateTree, ZoneGraph, ZoneGraphAnnotations, SmartObjects, NavCorridor, **PoseSearch** (an animation plugin) and DataValidation.
- `SOLTest.Build.cs`: add MassCommon, MassSimulation, MassSpawner, MassLOD, MassSignals, MassRepresentation (only if its path is used), StateTreeModule, MassAIBehavior and GameplayTags.

**The StateTree vs manually run processors clash.** SOLTest's processors set `bAutoRegisterWithProcessingPhases = false` and are run through `UE::Mass::Executor::Run` from its own subsystems (for example the ship and minor-body subsystems). That is what fixes the frame order in ARCHITECTURE §3. `UMassStateTreeProcessor` and the MassGameplay LOD, representation and spawner processors expect `UMassSimulationSubsystem` phase ticking and Mass signals. Using them means either moving SOLTest onto phase ticking (a frame-order redesign) or driving the StateTree processors by hand together with their signal and subsystem dependencies. Either way it is a spike with an unknown outcome, which is why escort AI is rated "Needs custom work, M to L" and phase B carries a spike line. A custom C++ state machine in a SOLTest processor is the fallback.

**World-space assumptions and churn.** MassMovement, the MassLOD viewer info, the Smart Object spatial index, ZoneGraph and NavCorridor all work in UE world space, which for SOLTest is rebased render space (6.3). Experimental status also means API churn between engine versions, which the "no deprecated APIs" rule makes more expensive.

---

## 7. Limit Theory AI: source review and adoption for SOL

Source-level review of LT's AI in the old C++ code (2012 to 2015, with LTSL scripts) and the new Lua code (~2017 to 2018); `libphx` contains no AI code. Nothing was built or run, so runtime figures are estimates or come from the videos. Costs and budgets in 7.7 and 7.8 are **estimates**.

### 7.1 What exists in code vs what the videos show

| Shown in videos (U7 to U21) | In the public code |
|---|---|
| Master planner that chains preconditions, fluid goals | **No.** Old: a task stack plus one random job sampled per frame and accepted if profitable. New: best payout of 100 random job samples |
| Hypothetical-outcome simulation (F-139) | No |
| Abstract/LOD world simulation (F-136) | Old has a wrapper that simulates a task as input-to-output cycles without movement (`Task_LOD`), but **nothing ever uses it**. New: none |
| AI-posted missions taken by other AIs (F-145, F-154) | Old posts one "damage the enemy" mission funded from a project wallet; **no code takes or pays missions** |
| Faction leaders, 7-trait personalities | Traits exist; personality is used in one decision (a lawlessness gate), and AI players are created with all-zero traits |
| Later fluid-goal rewrite, captains, contracts, rail AI (U11 to U21) | **Not in either repo** |

So "adopting LT's AI" means designing it, not porting it.

### 7.2 How it actually works

**Old (C++).**
- An AI "player" is an object assembled from mixin components (account, assets, opinions, orders, projects, missions, tasks). It is ticked by the ship it pilots, once per render frame with a variable time step; a declared 60 Hz simulation frequency is never used.
- **Task descriptors** are the core abstraction: shared (flyweight) objects declaring inputs, outputs, duration, value, alignment and capability factors, with a small per-agent instance holding execution state. Execution is a **pushdown task stack**: only the top task updates, tasks push children (mine pushes go-to), and buy/sell tasks decorate an inner task with docking and cargo steps. 19 task types exist.
- **Decision layer** (`Task_Play`): each frame, take one random job from the system's job list, reject it if the traits forbid it, wrap its inputs and outputs in buy/sell steps at random markets, value it from market EMA prices, adopt it if profit is at least 1 and fewer than one project exists. Idle assets are allocated with a constant score; ships are bought while under 16 assets.
- **Projects** carry P/L metrics (expense, revenue, losses, cycles) and a wallet fed by a share of revenue; they refresh market orders on a random (Poisson) timer and dispatch idle assets.
- **Meta-projects** split the wallet four ways; only "monopolize" does anything (posting the unconsumed mission above).
- **Job generation** rebuilds the full job list every frame, including four transport jobs per market pair per item.
- **Markets** are escrowed bid/ask books per station and item, re-sorted and crossed at the midpoint every frame, with a 27-level EMA price cascade.
- **Navigation** is Dijkstra over a complete graph of all navigable objects in the system per path request. **Combat** orbits the target and fires on an intercept solution.

**New (Lua).**
- Entities are Lua tables with per-event handler lists. Each frame every AI player, the system and every child entity receives an update event, then physics steps.
- An **action stack** where every stacked action gets a passive update and the top one an active update (so weapons keep firing while another action steers).
- Two **job** types, mine and transport, each a scripted multi-step sequence with a payout function.
- **`Think`**, the whole decision maker: for each idle asset, draw 100 random jobs and take the best positive payout. Payout ignores travel time and competition. An older flow-pressure planner is disabled by a constant condition.
- Job generation rebuilds every (yield x market) and (trader x item x trader) job every frame. Traders hold **one array entry per unit** of each order. Factory order posting is disabled because unbounded per-unit orders stalled the game. A separate market matcher is unused and broken.
- Steering runs per agent per frame in Lua and is self-flagged as the main bottleneck. A debug setting replaces flight with randomised teleport steps, which is the "time-compression hack" of the videos (F-148).

### 7.3 Scalability evidence

| Hot spot | Cost per frame (from code) | Consequence |
|---|---|---|
| Every object updated every frame | O(entities), no sleeping, no distance LOD for logic | No headroom for idle agents |
| Job list rebuild | O(markets^2 x items) with allocations, every frame, in both versions | E.g. 50 stations x 30 items gives ~147k task allocations per frame in the old code (estimate from the formula) |
| Order-book matching | Full sort of both books per item per market every frame; old trade log grows forever; new books are per unit | The documented stall; memory leak in long sessions |
| Decision busy-wait (new) | An idle asset with no positive job re-samples 100 jobs every frame | In the shipped config every asset is in this state |
| Pathfinding (old) | O(nodes^2) graph build and Dijkstra per request | Grows with stations and rail nodes |
| Parallelism, event-driven work, far-agent LOD | None in either version | Single-threaded polling |

Estimated memory: 0.5 to 2 KB per ship in the old code, 1 to 5 KB in the new, scattered on the heap. Estimated headroom even with every bug fixed: roughly 10^3 to 10^4 agents at 60 fps (1 to 10 µs per agent per frame in C++, 10 to 100 µs in Lua). The videos agree: ~9 NPCs (U11), 100+ per system (U15/U16), 128 from 8 factions (U21), and 2,000+ ships reported for the PAX 2018 build. **What scales is the decision shape**: K-sample utility choice costs O(K) per decision regardless of world size, and task descriptors with inputs, outputs and duration support simulation without movement.

Not establishable without running: real frame times and memory, LuaJIT GC pressure, whether EMA prices converge or oscillate, whether the old economy stays stable with the refund bug, and the later AI shown in U11 to U21.

### 7.4 Code quality and bugs (condensed)

Neither version is deterministic (global or wall-clock-seeded randomness, variable frame step, per-frame mining increments), and neither has automated tests. Coupling is heavy in the old code (tasks reach into arbitrary components, type-erased state blobs, static scratch vectors that are not reentrant); the new code stores job state on the entity because job objects are shared. Maturity: prototype, rewritten more than once.

| # | Version | Defect | Effect |
|---|---|---|---|
| B1 | Old | Market refund on a partial fill uses the remaining volume before it is reduced | **Creates credits** on every partial fill below the bid |
| B2 | Old | Two tasks misspell the virtual output method, so they do not override it | Pirate and produce jobs are valued at zero or less and never adopted |
| B3 | Old | Mining reads one element past the end of an offsets array | Out-of-bounds read every mining frame |
| B4 | Old | Trait vector declares 6 slots for 7 traits | One trait silently dropped from vector math |
| B5 | Old | Meta-project loop has no break and uses the project's own order as the mission target | A mission per competitor ask, aimed at the poster itself |
| B6, B7 | Old | Pirate never clears a dead target; pirate and mine pick random objects from the whole system | Pirates stall forever; random busy-wait |
| B8 | Old | Transport's withdraw and deposit are commented out | Transport moves no cargo; agents ping-pong |
| B10 | Old | AI players created with all-zero traits | The one trait-driven branch always rejects hostile jobs |
| B11 | New | Market matcher writes one field name and reads another | Would throw on first use (latent, unused) |
| B12 | New | Traders pay sellers without a balance check; bids never escrow | Money creation, negative balances |
| B13 | New | Factory orders disabled, so no station ever bids | **Every payout is 0; the economic AI is idle** in the shipped config |
| B14 | New | Payout ignores travel time and competition | Herding onto one job; far jobs look as good as near ones |

(B9, B15 and B16 are minor: an unused LOD wrapper with null nodes, a missing clone method, and self-documented float drift in a disabled planner.)

### 7.5 Ideas to adopt, mapped to UE 5.8

| LT idea | Keep? | SOL / UE mapping |
|---|---|---|
| Task descriptor with inputs, outputs, duration, value, capability factors | **Yes** | Data-only job rows (Data Asset or table, paths in `SOLConstants.h`); one descriptor drives execution (near tier), valuation and aggregate simulation (far tiers) |
| Flyweight task plus per-agent instance | **Yes** | Shared job table plus a small per-entity job fragment; no per-agent heap state |
| Task/action stack with a passive layer | Near tier only | StateTree states (MoveTo, Dock, Mine, Attack, Escort, Flee) or a custom processor; passive layer as a parallel branch or a separate weapons processor (subject to the 6.8 spike) |
| K-sample best-payout choice | **Yes, core** | A decision processor over agents tagged as needing a decision; utility per unit time with trait weights; K ~16 to 32 from a precomputed opportunity table; event-driven, not per frame |
| Job list rebuilt per frame | **No** | An opportunity subsystem keeping a bounded top-N per commodity and region, updated on change or at 0.2 to 1 Hz in sim time |
| Per-unit or per-frame-sorted order books | **No** | Station x commodity stock plus price curve as SoA; books aggregated by price level only where needed (8.6) |
| EMA price cascade | Yes, cheap | A few EMAs per station x commodity on a fixed sim-time interval |
| Projects with P/L and wallet feedback | Yes, later | Faction-level project records updated on job-completion events |
| AI-posted reward-pool missions | Yes, later | Contracts (poster, pool, price per unit of proof, constraints) listed in the same opportunity table, so NPCs and the player take them symmetrically |
| Personality traits | Yes | 7 x uint8, per agent or per faction plus offset; hard filters and utility weights |
| Capability algebra | Yes | Per-hull capability shared fragment (cargo, acceleration, mining rate, attack) |
| Dijkstra on a complete graph | **No** | Orbital legs between bodies and stations with analytic transfer-time estimates, cached per origin-destination pair per sim hour |
| Abstract task simulation (unused in LT) | **Yes, as the principle** | Far tiers simulate jobs as input-to-output cycles without positions |
| Hypothetical simulation (F-139) | Later, optional | Run the aggregate model forward a few steps on a copy, faction level only, budgeted |

Smart Objects fit station slots (docking bays, trade terminals, mining slots); claimable slots fix LT's herding bug and give a reservation count for the utility. EQS does not suit Mass scale; SOLTest's existing combat grid covers "nearest hostile" queries.

### 7.6 What must be redesigned

1. **Tiered simulation**: fine per-agent simulation only near the player, aggregate elsewhere. LT never shipped this.
2. **Event-driven everything off the near tier**: decisions on job completion, interruption or a relevant price change, never polling.
3. **Time warp as the hard constraint** (7.8).
4. **Deterministic, seeded, fixed-step**: per-agent random streams from a hash of seed, agent id and decision counter; sim-time steps from the sim clock, so promotion between tiers reproduces state and saves stay small.
5. **Consistent tier transitions**: promotion derives position, velocity and cargo from the agent's leg record; demotion folds it back. LT never solved this.
6. **Conservation by construction**: single ledger functions for money and goods with tests (the opposite of B1 and B12).
7. **Reservations and congestion** in the utility, to prevent herding.

### 7.7 Proposed 4-tier Mass architecture (sketch; all costs are estimates)

Tier assignment runs at 1 to 2 Hz real time with hysteresis, like the anchor's.

| Tier | Who | State | Update | Cost per agent (estimate) | Budget example (estimate) |
|---|---|---|---|---|---|
| **T0 Near** | Within ~100 to 1,000 km of the player, or visible or targeted | Full Mass ship (same fragments as the player ship), behaviour, weapons | Every frame through the existing flight-processor path; decisions event-driven | 1 to 3 µs flight plus 1 to 5 µs behaviour | up to 500 to 2,000 agents, about 2 to 5 ms |
| **T1 Local** | Same anchor region (planet or moon system), not near | Leg record plus analytic position, no physics | Evaluated at 2 to 10 Hz or on query (targeting, map, ISM upload) | ~20 to 50 ns per evaluation (compare the measured ~20 ns per entity minor-body scan, SDD 7 Amendment 4) | 50k agents at 5 Hz, about 0.1 to 0.2 ms per frame |
| **T2 Far** | Elsewhere in the solar system | Leg record only (from, to, start and end sim time, cargo) | Discrete events in a time-ordered queue (leg end, job end) | ~0.2 to 1 µs per event | 1M agents at one event per game hour, at 1x warp ~280 events/s, under 0.5 ms/s; **only valid up to ~10 to 100x warp** |
| **T3 Aggregate** | Everything at high warp, and all station and faction economics always | Station x commodity stock, price and flows; faction budgets; route flows per origin, destination and commodity | Fixed sim-time step (e.g. one game hour), sub-stepped under warp up to a cap | ~5 to 20 ns per cell | 500 stations x 30 commodities = 15k cells, about 0.1 to 0.3 ms per step |

At high warp, T2 agents freeze as "assigned to route R at phase p" and act only through the route's flow in T3; when warp drops, phases are re-derived deterministically from the route schedule. The player never sees T2 or T3 agents directly, so statistical consistency suffices there; T0 and T1 must be exact.

**Fragments (proposed names):** agent id (id, faction, archetype); traits (7 x uint8, or a per-faction shared fragment plus offset); job (job index, opportunity id, phase, claimed slot, cargo); **leg** (origin, destination, start and end as double sim seconds, profile; the single source of truth for T1 and T2 position); wallet (or faction-level); per-hull capability shared fragment; tags for each tier, for "needs decision" and for "in combat".

**Processors and subsystems (proposed):**
- Agent LOD processor: tier assignment, tags changed through the command buffer only on transitions, spawning and despawning T0 representations.
- Decision processor: agents tagged as needing a decision; utility over K samples; slot claim; writes job and first leg. Parallel over chunks, with claims resolved serially in a stable order (like the combat subsystem's parallel sweep and serial apply).
- Agent event subsystem: the T2 time-ordered queue (binary heap or calendar queue keyed by sim time); applies job outputs to T3 stocks and re-tags the agent.
- Leg processor: T1 analytic double-precision positions for agents that need them this tick; feeds ISM or Niagara like the belt.
- Economy subsystem: T3 SoA stocks, prices, EMAs and flows on a fixed sim-time step, with conserving ledger functions.
- Opportunity subsystem: top-N opportunities per region and commodity, refreshed incrementally; also holds contracts.
- Faction subsystem (later): projects with P/L, budgets, contract posting.
- T0 behaviour drives **the same flight control fragment the player uses** (player/NPC symmetry), with bolts through the existing combat subsystem.

**Frame order** (extends ARCHITECTURE §3's single driver): clock, registry, ship and T0 flight, T1 legs (budgeted), T2 event drain up to the current sim time (budgeted, with spillover), T3 step if due, decisions for tagged agents (budgeted N per frame), LOD transitions, visuals, combat. Every non-T0 stage has a per-frame work cap, so warp spikes turn into latency (decisions a few frames late) instead of hitches.

**Utility (sketch):** a hard trait gate on the job's alignment, times (expected revenue minus expected cost minus a regional risk cost minus a congestion cost from current claims), divided by total time (travel to source, job duration, travel to destination), times trait weights. Choose the best of K samples drawn in proportion to a cached base score. Unlike LT, this scores profit per unit time and penalises herding.

### 7.8 The time-warp event-rate risk

At 30 days per second of warp, 720 game hours pass per real second. A per-agent discrete-event model with one job event per agent per game hour then produces 1M x 720 = **~7.2 x 10^8 events per real second**, which is impossible (estimate). Above a warp threshold, the far tiers must switch to closed-form flow integration (stock changes by production minus consumption plus net transport flow, times the time step). The threshold (for example around 1 game hour per second) is an open question. This switch has to be designed first, in the phase B AI SDD, not retrofitted; it is the highest risk in the AI design.

### 7.9 Staged adoption path

| Stage | Content | Size | Fits phase |
|---|---|---|---|
| A | SDD and plan (grill-me first): tiers, warp policy, conservation rules, data model; decide StateTree vs custom for T0 after a spike | S | Start of B |
| B | Pure-logic economy core (no Unreal types beyond containers): commodities, station stock and price curves, conserving ledger, T3 fixed-step flow integration; tests for conservation, price response, warp sub-stepping | M | C or E |
| C | Opportunity table, utility, K-sampling and reservations as pure logic; tests for seeded determinism, herding avoidance, per-time scoring | M | E |
| D | Mass agents in T2: fragments, event subsystem, decision processor, headless benchmark at 100k and 1M agents with `stat`/Insights evidence | L | E (needs the headless runner) |
| E | T1 analytic legs and ISM rendering (reuse the belt patterns, double coordinates, rebasing), map display, targeting hook | L | E |
| F | T0: promotion and demotion with exact state transfer, AI flight through the player's flight model, docking and mining slots, behaviours, AI combat through the combat subsystem; PIE smoke checks | L to XL | B (initial), E (full) |
| G | Factions, projects with P/L, budgets, reward-pool contracts taken by NPCs and the player | L | F |
| H | Optional: hypothetical forward simulation for faction strategy, pre-game history (F-138, F-139) | XL | G or later |

Stages B and C are where LT's best ideas land cheaply; stage D is where the 1M goal is proven or not.

### 7.10 Risks and open questions

**Risks:** (1) time warp vs per-agent simulation (7.8), the highest; (2) tier-transition consistency (popping, duplicated cargo or money if the leg record is not the single source of truth); (3) economy stability (price curves plus profit-seeking agents can oscillate or collapse; damping and tests before content); (4) herding at 1M even with K-sampling; (5) StateTree per-entity cost at T0 and Smart Object claims under Mass, both unmeasured in 5.8; (6) determinism under parallel decisions (stable claim order); (7) scope creep from the videos; (8) licensing is not a risk (Unlicense), but nothing is worth copying verbatim.

**Open questions** (also listed in 9.1): what an NPC does in the real solar system (belt and ring mining, trade between moon stations, piracy, patrols); whether stations come before Part 10 and are Mass entities or registry records; the T2-to-T3 warp threshold and whether NPC traffic is visible on the jump map at high warp; whether the economy is saved or regenerated from seed plus elapsed time; full player flight model or a cheaper kinematic controller at T0; symmetric player-style factions or authored factions with AI budgets; how ~1M splits between AI agents and passive bodies.

---

## 8. Poor-fit features: source review and revised assessment

Source-level review of the 11 features that the first version of this document rated poor fit, put in the "not worth it" bucket, or treated as hard to adopt. No LT code was built or run; claims that only a run could settle are listed in 8.14. Sizes are **estimates** on the 1.3 scale.

**SOL constraints used in every verdict:** (C1) one real system with real ephemerides, bodies, rings and sky; (C2) real scale, double precision, rebasing every 10 km, bodies on Kepler rails, so any "static" infrastructure between bodies moves and changes shape; (C3) time warp up to 30 days per second, so anything simulated in real-time physics must resolve analytically in sim time; (C4) Mass for everything with a 1M goal, SoA subsystem data accepted for non-entity data; (C5) an engine with an editor.

### 8.1 Procedural universe hierarchy, infinite universe, system layout (F-208 to F-210)

- **How LT does it.** Old code only (the new code generates one system). A seeded universe creates resource types (with a fixed ore seed, so every universe has the same ores), then a single top-level region. Regions subdivide recursively by rejection-sampling child discs, but the fan-out is pinned to 1, so in practice a handful of systems exist. Connectivity is the union of two jittered minimum spanning trees over each region's children, each new edge becoming a wormhole pair. A system gets a star, a nebula, a starfield and dust; its layout script places one hard-named planet, two asteroid zones at fixed angles, an outpost per zone and scripted near-invulnerable pirates and patrols.
- **Quality and scalability.** Mostly seed-deterministic; the "infinite universe" is not implemented (no lazy region expansion); the MST is quadratic per region, fine for tens of children. The layout is a test scaffold.
- **Reusable.** The **double jittered MST** for an irregular, connected, sparse graph: NPC trade-route candidates between stations, charted lanes between Lagrange points or stations, patrol networks. Use orbit-averaged distances, because bodies move (C2). **Seeded hierarchical resource distributions** for per-region composition, with real spectral classes and family membership as the parent distribution. Seeded, cached generation matches SOLTest's offline bakes.
- **Drop.** The universe/region/system hierarchy and layout generation (C1). A future multi-system extension would be a separate design with Keplerian elements, multiple anchors and a dynamic-body save.
- **UE mapping.** Pure C++ Kruskal or Prim over `TArray`, unit-tested, run at bake time or boot.
- **Revised rating.** Poor fit, not needed. Kernels **S**. Hypothetical multi-system extension **XL**.

### 8.2 Procedural nebulae, dusty vs clear systems (F-197, F-216)

- **How LT does it.** A cubemap rendered once per system by a fragment shader: a 4D fractal (Kaliset style, 30 iterations) ray-marched at 128 steps per texel, with absorption and emission and a star glow added before absorption so dust occludes it. Cached to disk by seed; a blurred copy and an irradiance map are derived for lighting. "Dusty vs clear" has no separate code path; it falls out of random parameters (inferred).
- **Quality and scalability.** LT's signature look; runtime cost nil (once per system, cached); no physical realism.
- **Reusable.** The **lighting idea**: convolve SOLTest's real sky (AT-HYG plus the Milky Way) into a faint irradiance or reflection capture so ship metal picks up a Milky Way tint (a SkyLight with a specified cubemap, baked by the existing Python tools, kept tiny relative to sunlight; **S to M**). Absorption-plus-emission ray marching is an idea check for SOLTest's gas-giant volumetric cloud layer and possible ring forward-scattering glow, not a port. Midpoint-displacement colour LUTs could tint asteroid materials per family (**S**, optional).
- **Drop.** A fictional nebula skybox (C1, SDD 13). Real "dust" in the solar system is zodiacal light and the gegenschein: if ever wanted, a Sun-angle-driven sky or post-process term (**S to M**, low priority).
- **Revised rating.** Poor fit (drop). Spin-offs **S to M**.

### 8.3 Jump holes, jump gates, capital jump drives (F-029 to F-031)

- **How LT does it.** Old code only: a wormhole is an object linked to its twin, placed far out along the direction between the two systems; traversal is an **instant teleport** into the destination system. Links come from the region MST (8.1). Gates and capital drives have no code anywhere; the new code has no jump code at all.
- **Quality.** Trivial and deterministic.
- **Reusable.** SOLTest's jump map already does more (any destination, real flight time compressed). The reusable idea is a **sparse travel graph as a gameplay constraint**, creating chokepoints for piracy and security: fictional Lagrange-point or sphere-of-influence gates, or a licensing/fuel rule allowing jumps only along charted edges (built with 8.1's MST), or jump range by drive class (the F-031 idea).
- **Drop.** Inter-system wormholes (C1); an in-system teleport would be worse than the jump map and break sim-time consistency under warp (C3).
- **UE mapping.** Gate positions analytic from `SOLKepler`; charted edges in a Data Asset; the jump-map picker filters by edge; gates as Actors near the viewpoint and icons on the map.
- **Revised rating.** Poor fit as specified (drop). Reinterpretation **M**, after stations.

### 8.4 Warp lanes, warp rails and lane allocation (F-026 to F-028, F-159, F-160)

- **How LT does it.** Old code only. Rails are chains of nodes dropped at fixed spacing along straight segments or arcs; the rail itself has no mesh (CPU particles drawn when visible). One shared controller owns all passengers on a connected chain. **Lane allocation** is the interesting part: the rail cross-section is an annulus packed as an **adaptive polar quadtree**; allocation picks the smallest free slot that fits the ship, binary-searches the minimal angular and radial span, splits into four children, and freeing merges siblings back. Riders are moved by large per-frame forces toward their slot on the next node plus a lane-locking force.
- **Quality and scalability.** The allocator is sound but uses linear scans and fails silently when no slot fits. Force-driven motion is frame-rate sensitive. Node positions are static. The whole system was dropped in the new code.
- **SOL conflicts.** C2: endpoints move (Earth-Mars distance swings between ~0.5 and ~2.5 AU), and fixed node spacing is meaningless at AU scale. C3: a force-driven ride cannot run at 30 days per second. C4: per-frame CPU particles. ZoneGraph is a poor fit (static, world-space lanes).
- **Reusable.** **Rails as analytic paths in a moving frame**: an endpoint pair plus a parametric curve (straight line in a chosen frame, a transfer arc, or the jump-map transfer), with rider state of rail id, lane slot and progress; position evaluated each frame from the endpoints' current Kepler positions, O(1) per rider and warp-safe, as a Mass fragment plus processor. The **polar-quadtree lane allocator** as pure, tested C++ with free lists per depth, useful for rails, **docking queues and parking slots at stations**, and formation slots. Rail visuals as a Niagara ribbon or GPU spline (with the TSR smear caveat).
- **Drop.** Static node chains, force steering, per-node controller ownership, CPU particles. Under warp, riders resolve abstractly (arrival = departure + length / speed).
- **Revised rating.** Concept fine, LT implementation a poor fit. Analytic rails plus allocator plus visuals **M** for the player and a few hundred riders; **L** with NPC AI and warp abstraction; allocator alone **S**. Product decision first: rails mostly serve NPC traffic lanes.

### 8.5 LTSL, hot reload, soft compile failure (F-341 to F-343, F-362, F-363)

- **How LT does it.** Old code only (the new code switched to LuaJIT). An indentation-based list syntax rewritten into prefix calls (all operators left-associative, including power, a semantic trap), compiled by trying interpretations in sequence until one works, and executed by a **tree-walking interpreter** over LTE's reflection, with engine functions bound by generated macros. **Hot reload** is on for every function load: the script file is read and hashed from disk each time and recompiled on change, with transitive dependencies; some call sites read a file twice per generated ship type. **Soft failure** means a failed expression drops the function; but at least one caller invokes the result without a null check, so a broken generator would likely crash (inferred). The new code has an error handler around the app and reloads only shaders and materials.
- **Quality.** A tree-walking interpreter is typically 10 to 100x slower than native code (general estimate); error reporting has no line numbers. LT's famous iteration speed came from reload plus reflection-bound API plus test-bed apps, not from the language.
- **UE mapping (what replaces each role).** Behaviour scripts: StateTree with C++ tasks. Generators: Data Assets plus C++ or Geometry Script, PCG for placement. UI scripts: UMG/CommonUI with MVVM. Hot reload: Live Coding for C++ bodies, Blueprint/StateTree/PCG recompile in PIE, live Data Asset edits. Reflection-bound docs: `UFUNCTION` with tooltips, Python for editor automation. Soft failure: asset-isolated Blueprint/StateTree errors, `ensure`/`checkf` in C++. Test beds: `-SOL*` flags, test maps, Functional Tests, Gauntlet.
- **Transferable lesson.** Dependency-tracked regeneration: a generator Data Asset broadcasts a "regenerate" delegate from `PostEditChangeProperty` that previews and PIE instances listen to (**S** per generator type).
- **Drop.** The language, compiler, interpreter and any custom VM. Verse ships compiler infrastructure in 5.8, but its support for non-UEFN projects was not verified, so do not plan on it. For logic modding, a curated Blueprint API in a mod SDK.
- **Revised rating.** Poor fit as a language (drop). Adopting the iteration workflow **S to M**.

### 8.6 Emergent economy: markets, price formation, production, factories (F-085 to F-093, F-119 to F-126)

- **How LT does it (old, the most complete).** Integer quantities and prices. Escrow on placement (asks remove goods, bids remove credits). Every frame, for every item at every market, **both books are fully re-sorted**, crossed at the **midpoint price**, and each trade is appended to a log that is never trimmed. A **27-level cascade of EMAs** (periods 1, 2, 4, ... intervals) gives a multi-timescale price memory in constant space. NPC valuation reads the EMAs; sell prices get a stochastic markup whose variance shrinks with traded volume. Projects post orders on a random timer and track gross profit per unit time. Production consumes an assembly chip plus requirements; chips are minted from blueprints as a per-frame random process. The economy aggregator rebuilds all jobs every frame, quadratic in markets.
- **How LT does it (new, less complete).** Four items and two recipes as placeholder content. A **flow-pressure** objective (a production is attractive if it moves a location's net flow of each good toward zero) is a clean idea. Factories post **one order per unit**, undercutting the best ask by 0.5% each time with no cost floor; this posting is disabled because it stalled the game. Traders hold per-unit price arrays and do not escrow bids. The market matcher is broken by a field-name mismatch.
- **Quality and scalability.** Matching is O(n log n) per book per frame; memory grows without bound; no order bounds; **neither version conserves money** (the old refund over-credits partial fills, e.g. a resting bid of 10 at 100 filled 4 at 90 refunds 640 instead of 40; the new bids never escrow); the new pricing is **unstable by construction** (a geometric race to the bottom); quadratic aggregation; not deterministic; ~100 to 128 active NPCs per system in the videos.
- **Reusable.** (1) The escrowed limit-order-book data model with integer prices and the 27-EMA ladder, rebuilt as a market subsystem with SoA, fixed-capacity books aggregated by price level, matching on insert/cancel or a low-frequency sim-time tick, and conservation tests. (2) Volume-damped exploratory pricing from the EMAs, seeded per agent and tick. (3) **The flow-pressure objective as the currency of the aggregate simulator**: regions hold net production and consumption rates; aggregate haulers move flow along graph edges to minimise squared imbalance; prices follow imbalance; this steps analytically under warp (rates times dt) and lets Mass agents materialise near the player. (4) Recipes as Data Assets with Gameplay Tags; factory state as tiny SoA records in a low-frequency processor (not Mass entities, per SDD 7's precedent). (5) Project P/L for faction diagnostics and AI scoring. (6) Diagnostics as a requirement: a market debugger category (book depth, EMA ladder, flow pressure), Visual Logger events per trade, CSV export, an Insights channel.
- **Drop or redesign.** Per-unit orders; per-frame sorting and job regeneration (use incremental top-K spreads); undercut pricing (use cost-plus floors and EMA bands); unbounded logs (ring buffers, caps per owner); unseeded randomness (seeded streams, fixed iteration order); per-agent book participation at 1M (only near or important agents trade through books); Euclidean travel time (sim-time legs). Populations off Earth are a fiction choice.
- **Revised rating.** Fixed-price station market: Direct, **S to M**. Order book with level aggregation, EMA ladder and conservation tests: **M**. Recipes and production chains: **M**. NPC pricing, valuation and project P/L: **M**. Diagnostics: Direct, **M**. Aggregate flow simulator with materialise/dissolve, warp-safe: research-grade **XL**. A fully emergent per-agent economy at ~1M: **poor fit (drop)**, replaced by the tiered model. The first version's "L: matching is easy, stability is not" stands for the order-book row, now with two concrete bugs to test against.

### 8.7 Procedural technology tree and research (F-183 to F-189, F-192)

- **How LT does it.** Old code only. A blueprint stores a reflected copy of an item's constructor arguments plus a parent link. **Derivation** picks two distinct float attributes at random and multiplies one by a random factor in (0, 1] while dividing the other by it, so **their product is preserved** (a value-neutral trade-off), then re-instantiates the item, re-deriving stats and even the mesh from its seed. Item stats are pure functions of value and attribute multipliers, so derived blueprints are balanced by value. **Research** is a per-frame random process with a mean of ~10 s per research-capability unit, with no input cost. The authored tech-tree data files are loaded by no code; the fan-out discovery UI was a mock-up.
- **Quality.** Not deterministic; repeated derivation is an **unbounded multiplicative random walk**, so attributes can diverge to extremes; no generation naming or dedup; the per-frame Bernoulli trial saturates at large time steps (and so under SOL's warp).
- **Reusable.** The invariant-preserving mutation over a base item plus modifier list, with a seeded stream keyed by parent and derivation index, so blueprints are reproducible from their **lineage** (save the lineage, not the stats). **Bounding** by clamping log-attributes or a mean-reverting walk, with automation tests deriving 10k generations. Value-to-stats as a small set of pure functions. Research as a **Poisson process in sim time** (sample Poisson(rate x dt) per step, warp-safe). An authored tree as Data Assets with prerequisites and a custom-drawn graph view.
- **Drop.** Unseeded randomness, unbounded walks, free research, and mutation by field position in a struct (fragile under schema change; use explicit attribute ids such as Gameplay Tags). Under C1's near-future fiction, keep procedural tech modest (variants of real-ish equipment).
- **Revised rating.** Needs custom work, a good fit as pure logic. Seeded, bounded, lineage-saved derivation with tests **M**; with research labs, costed research and a tree UI **L**. Balancing open-ended.

### 8.8 Procedural ship and station generation (F-212, F-213, F-221)

- **How LT does it.** Old: a value budget split between hull, thrusters and generator, stats from value, scale from mass, then a script that accretes mirrored boxes on a plate mesh, applies warps and bakes AO. Thruster and turret sockets are found by **ray casting against the generated hull** (with a line-of-sight check for turrets) and mirrored. New: a Lua shape grammar (prism or ellipsoid hull, two wing families, bevel), sockets by ray casting against a per-type BSP, per-vertex AO that is quadratic in vertices. One generator variant is unreachable because the type picker never produces its index; capital-ship generators exist but are not spawned; stations are bevelled box clusters.
- **Quality.** Seeded and deterministic per seed; a recognisable look but a small variety space; no textures beyond basic UVs; cost per ship type, not per ship. Strong point: sockets sit on real surfaces and stats derive from value, so procedural ships slot into the economy and research loop.
- **Reusable.** Value-to-stats plus **socket finding by ray cast**, as an offline editor bake that writes Static Mesh sockets (Python or Geometry Script), useful even for authored Part 8 meshes. If procedural hulls are wanted: Geometry Script reproduces both approaches (box append plus mirror, extrude plus bevel, displacement, AO baked with the Modeling Tools bakers), run **offline** to produce Nanite Static Mesh variants per class; PCG for greebles and station module placement; an Editor Utility Widget preview with seed and parameter sliders. At fleet scale, variety is per type, not per instance.
- **Drop.** Runtime script generation, per-type BSP (use UE collision plus SOLTest's analytic swept spheres), quadratic AO, runtime mesh creation at game start (it loses Nanite and instancing).
- **Revised rating.** Fine as an offline generator, poor fit at runtime. Offline generator with preview and Nanite output **L** (art direction dominates). Socket bake **S**. Value-to-stats **S**. Procedural stations **M to L**.

### 8.9 3D / holographic node UI (F-226, F-233, F-260, F-266, F-308, F-358)

- **How LT does it.** **The node UI (U11 to U16) is not in either repo.** The old code holds the later stack/layer widget engine; the new code a retained Lua toolkit plus an early immediate-mode GUI used only in demos. What remains of the holographic look is a short shader building alpha from a normal-discontinuity edge term, three Fresnel-like bands and screen-space noise flicker, drawn additively, plus a 3D-model-in-a-widget pass with its own camera (the holographic ship viewer). The map widget is a flat top-down projection.
- **Quality.** Presentational and cheap.
- **Reusable (UE mapping).** The **hologram material** as an unlit additive material (Fresnel bands, edge glow from baked curvature or a screen-space edge post, time-plus-screen-noise flicker), as an overlay in the command view or on a proxy mesh in a ship viewer: **S**. 3D model in a widget via a scene capture to a render target, kept away from the rebasing gameplay scene: **M**. Holographic command view on the jump map (selection and orders over Mass): **L**. World-space overlays projected from universe positions: **M**.
- **Drop.** "The UI engine is the data-editor engine" (F-358): UE's Slate editor and UMG game UI are separate, mature stacks. The flat map (SOLTest's is already 3D and log-scaled). A generic node-graph game UI adds little over CommonUI lists; keep graphs for the tech tree and faction or trade networks.
- **Revised rating.** Look and 3D viewers Direct. Hologram material **S**; ship viewer **M**; holographic command view **L**; generic node-graph UI **M to L**, not recommended.

### 8.10 LT Data Editor with graph layouts where shape = type (F-350 to F-356)

- **How LT does it.** **No data-editor code exists in any of the three repos.** The old code has tool widgets (procedural model editor with symmetry toggles, a read-only text viewer, image and font previews, a browser, a dev panel) and the reflection it would have been built on (generated class macros, field iteration, a reflection-driven serializer). The new code has a short **read-only** reflective inspector. The layout engine shown in the video is lost; the video catalogue says it was days old and crashed on camera.
- **Reusable.** UE already is the data editor: reflection, Details panel and Property Matrix, live Data Asset edits in PIE (code must read values at use time or refresh cached copies), `UDeveloperSettings`, console variables, Gameplay Debugger and Mass debugger, `UCheatManager`, transactions, validation, source control. The Reference Viewer is the nearest "graph" view.
- **Drop.** A custom graph-based editor as a general tool. Keep at most a specialised graph view where the data is a graph.
- **Real SOL work.** Migrating designer- and player-felt tunables from `SOLConstants.h` into Data Assets or developer settings, keeping physics constants compile-time (open question 9.1).
- **Revised rating.** Direct (UE provides it). Tunable migration plus validation rules **S to M** (rated **M** in chapter 5 because of the generated Info screen and Mass thread rules). Optional reflected-struct graph viewer **M**.

### 8.11 History generation and history-driven placement (F-138, F-222)

- **How LT does it.** **Not implemented.** The old "History" component is a per-object event log for UI, not a world-history simulator; placement is direct random placement (8.1); the new code has nothing. The catalogue marks F-138 as planned.
- **SOL mapping.** The idea survives in a narrower form: once the aggregate flow simulator (8.6) exists, run it **fast-forward at boot** (a few simulated decades at coarse time steps, seeded) to give coherent initial prices, stockpiles, faction holdings and stations at resource-rich real locations (real geography is a strength here). Same code as the far-tier economy, so the marginal cost is low. Run it on a worker thread behind the main menu (with a loading screen, F-273), or as an offline commandlet that bakes the start state. Narrative history is a separate content problem.
- **Revised rating.** Poor fit as a standalone system. **S to M** as a fast-forward of the aggregate simulator once that exists (an XL prerequisite). This is now the single rating used in 4.5, 4.8 and 6.2.

### 8.12 Summary (revised, all estimates)

| # | Feature | LT source reality | Verdict for SOL | Reusable kernel | Size |
|---|---|---|---|---|---|
| 8.1 | Procedural universe, system layout | Recursive disc subdivision, double-MST wormhole graph, one region in practice; test-scaffold layout | Drop (C1) | Jittered double-MST graph; seeded hierarchical distributions | S |
| 8.2 | Procedural nebulae | Ray-marched fractal cubemap, disk cached, drives irradiance | Drop (real sky) | Irradiance from the real sky; colour LUTs; optional zodiacal light | S to M |
| 8.3 | Jump holes, gates, capital drives | Wormhole pair plus instant teleport; gates and drives absent | Drop; reinterpret | Charted jump-map edges, L-point gates, drive-class range | M (after stations) |
| 8.4 | Warp rails and lanes | Static node chains, force-driven riders, polar-quadtree lane allocator | Concept fine; implementation poor fit (C2, C3) | Analytic moving-frame rails; allocator for rails, docking, formations | S (allocator), M to L (rails) |
| 8.5 | LTSL, hot reload, soft failure | Rewrite plus backtracking compiler, tree-walking interpreter, reload rehashes files, soft failure partial | Drop the language | Live Coding, live Data Assets, regenerate-on-edit delegates, test maps | S to M |
| 8.6 | Emergent economy and markets | Escrowed books, midpoint fills, 27-EMA ladder, refund leak (old); per-unit orders, undercut ratchet, broken matcher (new) | Rebuild as SoA subsystem plus tiered aggregate; drop per-agent at 1M | Book model, EMA ladder, volume-damped pricing, flow pressure, project P/L, recipes | M per piece; XL aggregate |
| 8.7 | Procedural tech and research | Product-preserving two-attribute mutation; unseeded; free Poisson research; tree files unused | Needs custom work (good fit as pure logic) | Seeded, bounded, lineage-saved derivation; Poisson research in sim time | M to L |
| 8.8 | Procedural ships and stations | Box accretion (old), shape grammar (new); raycast sockets; value-budget stats; quadratic AO | Offline only, optional after Part 8 | Value-to-stats, socket bake, Geometry Script to Nanite | S (sockets), L (generator) |
| 8.9 | Holographic / node UI | Node UI absent; hologram shader and 3D-in-widget pass exist | Look Direct; node-graph UI low value | Hologram material, scene-capture viewer, holographic command view | S / M / L |
| 8.10 | Data editor | Absent; reflection and serializer exist; read-only inspector (new) | UE provides it | Tunable migration, validation rules | S to M |
| 8.11 | History generation | Absent (an event log only) | Only as fast-forward of the aggregate simulator | Seeded boot-time or offline fast-forward | S to M after 8.6 |

### 8.13 LT defects usable as SOL test cases

Each is a concrete failure that a SOLTest automation test should rule out in the equivalent system:

1. **Partial-fill refund over-credit** (old market): credit-conservation test across partial fills below the bid.
2. **Undercut ratchet** (new factory pricing, no cost floor): price-floor and convergence soak test.
3. **Matcher never runs** (new market, field-name mismatch): any unit test of matching catches it immediately.
4. **Unbounded growth** (old trade log never trimmed; new per-unit orders): caps and ring-buffer tests.
5. **Per-frame full re-sort and quadratic opportunity regeneration** (both versions): a performance test at target station and commodity counts.
6. **Unseeded randomness** in market cadence, research, minting and mutation: seeded replay test.
7. **Per-frame Bernoulli trials for research and minting** that saturate at large time steps: a warp test that compares Poisson(rate x dt) sampling at 1x and at maximum warp.
8. **Unreachable generator variant** (new ship-type picker): a test that every declared variant is reachable.
9. **Missing null check on a failed script load** (old ship-type generation): a test that a broken generator asset fails safely.
10. **Hot reload re-reads and hashes files on every load**: a test or review rule that reload checks are cached.
11. **Silent lane-allocation failure and linear scans** (old rails): allocator tests for full-rail behaviour and complexity.

The AI review adds more to the same list: credit creation in new traders (B12), the idle shipped economy (B13), herding from payout without travel time (B14), and the stalled pirate target (B6) (7.4).

### 8.14 What can only be confirmed by running LT's code

- Whether old LT's universe and layout generation reproduces bit-identically across machines and builds.
- Actual nebula generation time at 1024^2 x 6 on a modern GPU.
- Whether old LT's market prices converge or oscillate over long runs, and how large the refund leak is in practice.
- Rail-rider stability at low or variable frame rates.
- LTSL interpreter throughput relative to native code.
- Whether old ship-type generation actually crashes on a broken generator script.
- From the AI review: real frame times and memory per agent, LuaJIT GC pressure, whether new agents ever take a job in any shipped configuration, and the later AI shown in U11 to U21 (not in the repos).

---

## 9. Open questions, assumptions and unverified claims

### 9.1 Open questions for the project owner

1. **Is a game loop the goal at all?** SOLTest so far is a technology and visual foundation. Should it grow into an LT-like sandbox, a combat game, an exploration sim, or stay a scale showcase?
2. **Single real Solar System as a hard constraint?** If yes, LT's procedural universe, jump holes and nebulae stay out (chapter 8 keeps only kernels). If not, a hybrid (real Sol plus fictional systems) changes 4.8 and 8.1 a lot.
3. **Realism vs fiction for the economy.** The real Solar System has no populations beyond Earth. Where do markets, colonies and stations come from (near-future fiction, a chosen year)? What does an NPC actually do: belt and ring mining, trade between moon stations, piracy, patrols?
4. **Warp threshold for NPC simulation.** Above which warp rate do far agents freeze into aggregate flows (7.8; for example around one game hour per second)? Is NPC traffic visible on the jump map at high warp? Should warp ever be limited while NPCs are near?
5. **What does "~1M entities" mean for AI?** SDD 1 already says "a future goal of ~1,000,000 animated ships or characters". The remaining question is how that splits between full AI agents, analytic movers and aggregate flows, and how it relates to the ~19k passive minor bodies.
6. **Should Part 10 be reshaped** from a behaviour-less stress demo into the NPC foundation (phase B)?
7. **Near-tier-first or simulation-first?** Phase order in 6.7 (playable sooner) vs the AI review's order in 7.9 (1M proven sooner).
8. **Engine AI stack or custom?** Adopt MassAI StateTree (Experimental, pulls in ZoneGraph, NavCorridor and PoseSearch, clashes with manually run processors) after a spike, or write a custom near-tier state machine processor? And should NPCs fly the exact player flight model or a cheaper kinematic controller at T0?
9. **Mass-for-everything scope.** Are markets, factions, contracts and factories allowed to be subsystem SoA data rather than Mass entities (as bolts already are under SDD 7 Amendment 1)? Are stations Mass entities or records in a new dynamic object store?
10. **Persistence of a living world.** Is the economy saved or regenerated from seed plus elapsed time? How much play may be lost on a crash, given the "write only at lifecycle boundaries" rule?
11. **Tunables in C++ constants vs Data Assets.** Should designer-facing values move to Data Assets (live editing), accepting less compile-time checking and the Info-screen and Mass-thread work?
12. **Modding.** Is modding a goal? It changes data format decisions early (Data Assets and Game Feature plugins).
13. **Procedural ships and stations vs authored art** for Part 8 (8.8 recommends offline generation at most)?
14. **UI framework.** Adopt CommonUI and MVVM (Beta) before the strategic screens multiply (LT rewrote its UI five times), and how do they coexist with the custom rebinding and focus logic?
15. **Faction model.** Symmetric player-style factions (LT) or designer-authored factions with AI budgets?
16. **Multiplayer.** Assumed out of scope (LT never planned it); Mass and the custom rebasing would make it hard later.
17. **Platforms.** Is Linux or Mac support wanted (4.13)?

### 9.2 Assumptions made in this document

- Estimates follow the assumptions in 1.3: one developer directing AI subagents under the current workflow and usage pauses, person-weeks of active working days, content excluded, +50 to 100% tuning on gameplay systems. They are rough ranges, not schedules.
- Engine plugin maturity was read from the 5.8 `.uplugin` flags (6.8). Maturity can change between engine versions.
- SOLTest currently depends only on MassEntity and MassCore among the Mass modules (`SOLTest.Build.cs`); the full list of new dependencies for phase B is in 6.8.
- LT feature status is taken from `SimResearch.md` as written ("shown", "prototype", "planned"); LT never shipped, so "shown" never means "shipped". The source reviews in chapters 7 and 8 read code only; nothing was built or run.

### 9.3 Known constraints (previously listed as unverified)

| Constraint | Source |
|---|---|
| The renderer supports one atmosphere and one cloud layer per scene; SOLTest already uses SkyAtmosphere as the gas-giant cloud carrier | ARCHITECTURE (gas-giant visuals), SDD 14 Amendment 1 |
| MegaLights is present but experimental, needs hardware ray tracing, has no directional lights and is off by default | 5.8 renderer console-variable help text |
| MassGameplay, MassAI and ZoneGraph are Experimental; MVVM, GameFeatures, DataRegistry, Gauntlet are Beta | 5.8 `.uplugin` flags |
| SOLTest's Mass processors are not auto-registered with processing phases | `SOLShipFlightProcessor.cpp`, `SOLMinorBodyOrbitProcessor.cpp` |

### 9.4 Unverified claims and items to check before relying on them

| Claim | Status |
|---|---|
| A tiered model is required for AI at ~1M entities; a per-agent planner at that count is not affordable | Engineering judgement; the 7.7 tier costs are estimates. Stage D's 100k/1M benchmark (7.9) would settle it |
| ~20 ms per frame for `BuildMinorBodyGrid` at 1M entities | Linear extrapolation from a measured ~0.4 ms at 19,268 minor bodies (SDD 7 Amendment 4), not measured at 1M |
| Driving MassAI's StateTree processors by hand outside phase ticking is workable | Not tested; needs the phase B spike |
| StateTree per-entity cost at T0 and Smart Object claim cost under Mass in 5.8 | Not measured |
| Verse is usable by non-UEFN C++ projects in 5.8 | Compiler infrastructure ships; support not verified. Do not plan on it |
| Heterogeneous Volumes suitability for space dust | Present and on by default; suitability not checked |
| Smart Objects usable on moving stations if transforms are updated with the anchor | Untested design idea |
| Live Coding usable for SOLTest iteration | CLAUDE.md says it is "not relied on"; not tested for this project |
| Linux and Mac builds of SOLTest's custom shaders, Niagara systems and tools | Not checked |
| LT feature details (scanner version in code, hot-reload path in LTSL, double precision in old code) | Marked uncertain in SimResearch chapter 22.2; partly clarified by 8.5 |
| Real spectral types available for SOLTest's 500 named asteroids in JPL SBDB | Likely for many, not checked for the specific list |
| LT runtime behaviour (frame times, price convergence, generation determinism, crash paths) | Needs running LT's code; see 8.14 |

---

## 10. Changes in this revision

Revision of 2026-10-15, integrating three reviews: an adversarial review of this document, a source-level review of LT's AI, and a source-level review of the poor-fit features.

- **Factual corrections:** 51 test files (not 48) with 707 test macros; 67 commits including the initial commit; SOLTest facts pinned to HEAD `c4c94ee`; "1 to 5 days" defined as active commit days with Part 5's calendar span noted; 19,268 minor bodies vs ~19,269 entities explained; the flight HUD described as Canvas, not UMG; "infinite procedural universe" reworded to what was designed vs shown vs coded; the PAX 2,000+ ships attributed to the demo build, not the released code; F-148 added to the time-warp row; F-049 marked partially matched; the F-007 contradiction resolved (bodies matched, asteroid bounce a gap); duplicated F-116 removed; Verse wording corrected against the install; Rewind Debugger noted as part of GameplayInsights; `FInstancedStruct` noted as engine core (no StructUtils plugin needed); MegaLights and the one-atmosphere limit moved from unverified to known constraints; the determinism-makes-save-small claim qualified; one source-note claim corrected (SOLTest's gas giants use a volumetric cloud layer, not Heterogeneous Volumes).
- **Coverage:** 21 rows added for 22 missing LT feature IDs (F-041, F-042, F-043, F-062, F-082, F-096, F-146, F-155, F-244, F-247, F-270, F-272, F-273, F-274, F-279, F-283, F-286, F-287, F-318, F-330/F-331 in a new 4.13 Platforms section, F-349, F-365); F-206 and F-360 added to 4.12.
- **Engine maturity:** Experimental/Beta labels in the mechanism cells and a new plugin maturity and dependency table (6.8), including what enabling MassAI pulls in (PoseSearch, ZoneGraph, NavCorridor and more) and the clash between MassAI's StateTree processors and SOLTest's manually run processors.
- **Re-ratings:** NPC ship entities (Direct M to Needs custom L), escort AI (Direct M to Needs custom M to L with a spike), VR (Direct L to Poor fit XL), atmospheres (Direct M to L to Needs custom L, known constraint), time-warp interaction (L to L to XL, moved to phase B), damage, free camera, weapon variety, turrets, equipment market, stations, missions v1, tech tree, streamed fields, procedural UI layout, top bar, curved HUD, gamepad, engine trails, Data Asset migration, seed replay and test beds raised; player/NPC symmetry re-framed as a design rule; F-139 split off as research; compiler-errors row made consistent; warp rails, jump holes, LTSL, procedural ships and history generation re-rated from the source review.
- **Estimates:** S/M/L/XL mapped to person-weeks with stated assumptions; cross-cutting cost factors (6.4); person-week ranges and a confidence column per phase (6.5, 6.7); the pilot-loop total corrected from "L, a few months" to ~38 to 74 person-weeks.
- **Consistency:** power, cruise, camera, turrets, shields, weapon variety and repair assigned to a new phase C2; a minimal faction id and the warp policy moved into phase B; the headless runner fix added to phase A; history generation rated one way everywhere; the "comparable to LT's 2013 prototype" claim softened; open question 5 now cites SDD 1.
- **New chapters:** 7 (LT AI source review, scalability, bugs, ideas, 4-tier Mass sketch, warp event-rate risk, staged path) and 8 (11 poor-fit features with revised ratings, LT defects as SOL test cases, items that need a run). Open questions and unverified claims moved to chapter 9 and extended.
