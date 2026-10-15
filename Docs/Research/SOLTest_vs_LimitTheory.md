<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->
# SOLTest vs Limit Theory: Comparison, Feature Gaps and Unreal 5.8 Feasibility

Research comparing **SOLTest** (Acid Rain Studios LLC, Unreal Engine 5.8, C++) with **Limit Theory** (LT, Josh Parnell / Procedural Reality, cancelled 2018). Snapshot date 2026-10-14, SOLTest HEAD `7efe35c`.

LT facts and feature IDs (F-001 to F-370) come from [`SimResearch.md`](SimResearch.md), which is the evidence base for everything said about LT here. SOLTest facts come from `CLAUDE.md`, [`../ARCHITECTURE.md`](../ARCHITECTURE.md), [`../GAME_MECHANICS.md`](../GAME_MECHANICS.md), [`../SDDs/`](../SDDs/), [`../Plans/`](../Plans/), [`../ToDo/`](../ToDo/) and `git log`. Difficulty ratings and effort judgements in this document are **estimates**, not measurements.

---

## Table of contents

1. [Purpose and how to read](#1-purpose-and-how-to-read)
2. [SOLTest in brief](#2-soltest-in-brief)
3. [Side-by-side comparison by domain](#3-side-by-side-comparison-by-domain)
4. [Feature gap list: game features](#4-feature-gap-list-game-features)
5. [Feature gap list: editor and tooling features](#5-feature-gap-list-editor-and-tooling-features)
6. [Can all of LT's features be added in Unreal? Assessment and adoption order](#6-can-all-of-lts-features-be-added-in-unreal-assessment-and-adoption-order)
7. [Open questions, assumptions and unverified claims](#7-open-questions-assumptions-and-unverified-claims)

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
| What it is | A **scale-first, 1:1 real solar-system flight and visual simulation**: real ephemerides, real star catalogue, physically based rings and shadows, Mass entities | A **procedural economy and AI sandbox**: planner-driven NPCs, order-book markets, factions, research, all in a procedurally generated, stylised universe |
| Strongest layer | Universe/numerics, rendering, test and review discipline | Simulation, economy, AI, tooling for iteration |
| Weakest layer | No game loop yet: no economy, AI, factions, missions, stations, save | Performance at scale, visual popping, repeated rewrites, never shipped |
| Engine | Unreal Engine 5.8 (stock), single C++ module | Home-grown LTE (C++/LTSL), then libphx (C) plus Lua |
| Development span | ~18 days (2026-09-27 to 2026-10-14), 66 commits | ~6 years (2012 to 2018), cancelled |
| Content model | Hand-sourced real data (JPL, AT-HYG), procedural fill only for belt and ring rocks | Everything procedural (universe, ships, stations, items, tech, names) |

Reading guide: chapter 3 gives the domain overview; chapters 4 and 5 are the detailed gap tables; chapter 6 is the plain-language answer and roadmap; chapter 7 lists what the owner needs to decide.

### 1.3 Column conventions used in chapters 4 and 5

| Column | Values |
|---|---|
| Present in SOLTest? | **absent** (nothing built or planned), **partial** (some related code exists), **planned** (in the roadmap or a `Docs/ToDo/` item), **n/a** (deliberately out of scope by SOLTest's design) |
| UE 5.8 feasibility | **Direct** (stock engine features cover most of it), **Needs custom work** (engine gives building blocks, game-specific systems must be written), **Poor fit** (conflicts with SOLTest's design or is not worth building in UE) |
| Difficulty | **S** (days), **M** (1 to 3 weeks), **L** (1 to 3 months), **XL** (multi-month, open-ended). Calibrated loosely against SOLTest's own pace (a roadmap part took 1 to 5 days with AI assistance), so these are rough estimates |

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
| Minor bodies | 9,268 belt asteroids and 10,000 pooled ring rocks as Mass entities (~19,269 entities in total today); flown through without collision |
| Rendering | Procedural planet material, gas giants with volumetric clouds, real star sky, ring optical depth and ring/planet shadows, orbit lines, game-driven exposure |
| Combat | Two forward pulse guns, up to 20,000 live bolts, up to 1,000 passive targets with shield and health; nobody fights back |
| UI | Canvas HUD, radar, speed panel, menus and settings all in C++ UMG |
| Persistence | Settings only (versioned `USaveGame`, async, dirty-checked) |
| Quality process | 48 automation test files, scripted `-SOL*` smoke runs, adversarial performance and style review per part |
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
| Scale blockers for ~1M | Belt ISM per-frame CPU transform upload; idle ring rocks still Kepler-solved; constant orbital bases recomputed per frame; `BuildMinorBodyGrid` full scan (~0.4 ms at 19k, ~20 ms extrapolated at 1M) |
| Ordering and coupling | `Ship/`-`UI/` include cycle; unordered `OnUniverseUpdated` and `OnBodiesUpdated` listeners |
| Rendering cost or correctness | Dense ring layer ~3.3 ms GPU; belt mesh without LOD; TSR/motion-blur smear on render-space objects without motion vectors; orbit-line memory (~55 MB) and game-thread rebuilds; ring-carpet vs disc profile mismatch; private copy of the ring table in `M_SOLRingFar`; spherical-planet shadow assumption |
| Small CPU waste | Focal length, tilt quaternion, Sun GM literal duplication, ring reassignment allocation |
| Tooling | Headless `RunTests.ps1` stalls on this machine |
| Latent bug | Non-body targetable frame carry for bolts and targets |
| Docs drift | Hand-written menu prose and fixed keys in `SOLMenuText.h` |

---

## 3. Side-by-side comparison by domain

| Domain | SOLTest today | Limit Theory | Gap / advantage |
|---|---|---|---|
| Flight model | 6DoF, assist vs Newtonian, rate-command rotation, boost, speed cap to 0.5c, gravity, frame lock to any body | Freelancer-style third person, mouse aim, roll, cruise mode with power trade-offs, free vs locked camera, power distribution, dynamic mass (F-001 to F-006, F-018 to F-020) | Comparable core; **SOLTest ahead** on physical fidelity and frame handling; **LT ahead** on ship systems (power, cruise trade-offs, mass from cargo) |
| Universe and scale | Real Solar System at 1:1, analytic orbits, 36 bodies, ~9.3k real and fill asteroids, 4 ring systems, time warp to 30 d/s, double precision with custom rebasing | Infinite procedural universe of regions and systems linked by wormholes; scale 10x stylised, realistic scale "possible as a mod" (F-206, F-208, F-209) | **SOLTest far ahead** on realism and numerics (LT's new code even regressed to floats). **LT ahead** on breadth: many systems, procedural variety, zones |
| Travel | Jump map with picked destination and 3 s warp; supercruise is a ToDo | Warp lanes, then warp rails with lane allocation; jump holes between systems (F-026 to F-029) | Different answers to the same problem. LT's rails are a world feature AI also uses; SOLTest's jump is player-only |
| Rendering and effects | Niagara, ~1M GPU ring rocks, volumetric gas-giant clouds, AT-HYG sky, analytic ring/planet shadows, GPU orbit lines, TSR | Custom deferred PBR, procedural nebulae, volumetric dust, lens flares, impostors, atmospheres, procedural ships and stations (F-288 to F-319) | **SOLTest ahead** on physical correctness and particle scale; **LT ahead** on procedural art (nebulae, dust, ships, stations) and art direction breadth |
| Combat | Pulse guns, 20k-bolt SoA pool, swept hits on moving targets and minor bodies, shields and health on passive targets | Pulse, beam, rail, missiles, turrets, hull-conforming shields, subsystem damage, lead indicator, aim assist, AI dogfighting, fleet battles (F-049 to F-076) | **LT far ahead** on gameplay (enemies, weapon variety, damage model). **SOLTest ahead** on projectile scale and hit-test robustness |
| Economy | None | Credits, commodities, bid/ask order books, population demand, emergent prices, NPC pricing, storage (F-077 to F-102) | **Entire layer missing** in SOLTest |
| AI / NPCs | None (Part 10 plans behaviour-less NPC ships for a scale demo) | Escort/patrol AI, task-based professions (miner, trader, pirate), leaders and projects, personalities; 100 to 128+ NPCs per system in videos, 2,000+ at PAX 2018 (F-134 to F-162) | **Entire layer missing**. SOLTest's Mass base is a better substrate for scale than LT ever had |
| Factions | None | Factions as economic hierarchies, named factions, pirates, hostility by action; no real diplomacy (F-163 to F-179) | Missing; LT itself was thin here |
| Missions | None | Procedural missions v1, NPC-posted contracts, continuous reward-pool missions, data proofs (F-175 to F-178, F-145, F-154) | Missing |
| Industry, mining, stations | None (asteroids indestructible on rails) | Scanner, prospecting drones, beam mining, production modules, Tech/Production Labs, stations, asteroid bases, colonies (F-103 to F-133) | Missing. SOLTest has real asteroid data to build on |
| Research / tech | None | Procedural blueprint derivation, assembly chips, physical technology (F-180 to F-196) | Missing |
| UI / HUD | Canvas HUD, radar, speed panel, jump map, menus, settings, live key labels | Many UI generations: HUD, command (RTS) view, ship viewer, market, projects, research, map, radial menus, curved HUD (F-223 to F-287) | **LT ahead** on breadth (strategic screens); **SOLTest ahead** on polish of what exists (live rebinding, truthful info pages) |
| Persistence | Settings save only; universe deterministic from sim time but sim time not saved | No universe save in any code base | Both weak. SOLTest's determinism makes a future save small |
| Tools / editor | Python asset scripts, unreal-mcp, smoke flags, automation tests, adversarial reviews | Reflective data model, LT Data Editor (demoed, never in any repo), LTSL with hot reload, test beds, dev panel (F-336, F-341 to F-350) | **LT ahead** on in-game data tooling; **SOLTest ahead** on automated testing and the UE editor it gets for free |
| Audio | 7 synthesized placeholder combat sounds, pooled 3D voices, sound classes | Contracted weapon SFX, tension-driven music, gameplay-informative scanner and mining audio (F-320 to F-326) | **LT ahead**; SOLTest's Part 8 covers part of it |
| Modding / scripting | None | Data-level modding, LTSL scripts for UI, generators and behaviours; no mod manager (F-359 to F-370) | **LT ahead** on intent; neither had a mod loader |
| Testing / QA | ~700 test macros, smoke runs, review per part, measured perf numbers | Test-bed apps, AI combat statistics; no automated test suite recorded | **SOLTest far ahead** |

---

## 4. Feature gap list: game features

Rows list every LT feature group SOLTest lacks. Minor features are grouped. LT features SOLTest already matches or exceeds are listed in 4.12.

### 4.1 Flight, ship systems and travel

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Cruise mode with power trade-offs (F-004) | absent (supercruise ToDo is a different idea) | Direct | New mode in `FSOLFlightParams` and `USOLShipFlightProcessor`; HUD state | S: one more flight mode in existing pure logic | Must stay compatible with frame carry under warp |
| Free vs locked camera, turrets track aim (F-005, F-006) | partial (Alt free-look exists) | Direct | Spring-arm camera modes in `ASOLShipPawn` | S: camera code only | Turret tracking depends on turrets (4.2) |
| Power distribution, overcharge, presets, boost capacitor (F-018, F-019, F-235) | absent | Needs custom work | Power fragment on the ship entity; GameplayAbilities attributes are an option but heavy for Mass entities | M: rules plus UI plus effect on weapons, shields, thrust | Keep as plain fragments so NPC ships at scale do not need GAS components |
| Dynamic mass from cargo (F-020) | absent | Direct | Mass term in flight params from a cargo fragment | S: once cargo exists | Depends on cargo (4.4) |
| Hull and subsystem damage, repair, death and restart (F-021, F-022, F-037, F-061) | absent (collision has no damage) | Direct | Health/subsystem fragments; reuse `SOLDamageMath`; respawn flow in a game-mode subsystem | M: damage model plus collision damage plus death UX | Collision at 0.5c needs a damage curve that stays sane |
| Multiple ships, ship classes, buy/switch (F-013 to F-017) | absent (ARCHITECTURE §8: shared params fragment per class is the extension point) | Direct | Data Assets per ship class feeding a const shared Mass fragment; pawn re-possess | M: class data plus switch flow; L with buying | Needs real ship art (Part 8) to be meaningful |
| Hardpoints, fitting, fire groups (F-023 to F-025, F-232) | absent | Needs custom work | Socket list in a ship-class Data Asset, item instances as `FInstancedStruct`, CommonUI fitting screen | L: item model, fitting rules, UI | Item model is shared with economy and research, so design once |
| Docking and undocking, hangar view (F-009, F-010, F-238) | absent | Needs custom work | Docking as a state in the ship entity; station-relative frame lock (exists for bodies) | M: approach, lock and UI; stations must exist first | Stations orbit bodies, so docking happens in a moving frame (already solved for bodies by frame lock) |
| Warp lanes / warp rails with lane allocation (F-026 to F-028, F-159, F-160) | absent (jump map instead) | Needs custom work | Rails defined in body-relative frames, evaluated analytically per frame; ZoneGraph is a poor fit because its lanes are static world geometry | L: rail geometry on moving bodies plus AI use plus lanes | Under time warp rails between orbiting bodies change shape; must decide if they exist at all |
| Jump holes / inter-system travel, jump gates, capital jump drives (F-029 to F-031) | n/a | Poor fit | Could be reinterpreted as Lagrange-point gates, but there is one real system | n/a | Contradicts the single-real-system design |
| Planet docking, colony landing (F-032, F-034) | planned (surface-landing ToDo, Part 9) | Needs custom work | Level streaming or a sub-level for colony scenes; or seamless landing on Part 9 terrain | L: depends on Part 9 terrain choice | LT cut seamless landing; SOLTest's scale makes it harder but Part 9 plans terrain |
| Asteroid landing and hiding (F-008), rigid-body collision with asteroids (F-007) | planned (asteroid-ring-collision ToDo) | Needs custom work | Analytic swept sphere vs minor-body grid (already exists for bolts) | M: reuse `SOLCombatGrid`; tech debt on the grid scan applies | Chaos physics is a poor fit in rebasing render space; keep analytic |
| Discovery gating of markers (F-039), journals and bookmarks (F-044) | absent | Direct | Discovered-flag bitset per entity/body; bookmarks in the save | S each | Needs save/load |
| Targeting tracker on reticle (F-040) | partial (bracket and edge arrow exist) | Direct | HUD addition | S | |
| Asset naming (F-038), headlights (F-045), dev teleport (F-048) | absent (jump map covers teleport-like travel) | Direct | Trivial UI/state; a spot light on the pawn | S | |
| Player/NPC symmetry (F-047) | absent | Needs custom work | Design rule: player ship is just a Mass entity with `FSOLPlayerShipTag` (already true) and NPCs use the same actions | XL as a principle, not a feature | Already favoured by SOLTest's architecture |

### 4.2 Combat and weapons

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Weapon variety: missiles, guided missiles, railguns, beams (F-050 to F-053, F-313) | partial (pulse bolts only) | Direct | Extend the SoA bolt pool with kinds; beams as instant swept traces; missiles as a small Mass archetype with guidance; Niagara visuals | M per weapon family | Guided missiles need a guidance step inside the frame-carry scheme |
| Cooldowns, magazines, ammo in cargo (F-054, F-095) | partial (fixed fire cadence) | Direct | Fragment fields; `SOLFireCadence` extension | S | Ammo needs cargo |
| Two-axis turrets (F-055) | absent | Direct | Turret transform math in pure logic; ISM or skeletal sockets on the ship mesh | M | Needs ship art with sockets |
| Hull-conforming hit-only shields, multiple shields, shield tightness (F-056 to F-060, F-314) | partial (sphere targets with shield and regen) | Direct | Shield material driven by hit positions (material parameter or Niagara), shield values in fragments | M | |
| Lead indicator, aim assist (F-064, F-065) | absent | Direct | Intercept solve in pure logic, HUD marker | S | Must use frame-relative velocities |
| Friend/foe colours, friendly fire, hostility (F-066, F-067, F-173) | absent | Direct | Faction id fragment, relation lookup | S once factions exist | |
| Enemy AI combat: kiting, dogfighting, AI combat test bed (F-069, F-072, F-075) | absent | Needs custom work | StateTree (MassAI StateTree processor) for tactics; headless test maps for AI-vs-AI stats | L: AI combat is a tuning sink | MassAI/StateTree not yet a SOLTest dependency (Build.cs uses only MassEntity/MassCore) |
| Fleet battles (F-076), long-range bombardment (F-063) | absent | Needs custom work | Mass archetype per ship class, Mass LOD tiers, existing combat grid | L to XL at scale | Combat grid scan is a known 1M blocker |
| Heat detection, star masking (F-070, F-071) | absent | Needs custom work | Signature model in the scanner (4.5); star-direction check uses registry data | M | Real Sun position makes star masking physically meaningful |
| Size immunity, retaliation (F-073, F-074) | absent | Direct | Damage rules; faction response hook | S each | |

### 4.3 Economy and trade

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Credits, selling, rewards (F-077, F-080, F-081) | absent | Direct | Wallet fragment per agent; player wallet in the save | S | |
| Commodities and ore types (F-083) | absent | Direct | Data Table or Data Assets of commodities, Gameplay Tags for categories | S | |
| Equipment market, ship dealer (F-078, F-079) | absent | Direct | Station inventory as SoA arrays in a market subsystem; CommonUI screens | M | Needs stations and the item model |
| Bid/ask order books, emergent prices, average pricing, proceeds to producers (F-085 to F-089, F-091, F-092) | absent | Needs custom work | Market subsystem with per-station, per-commodity order books in SoA; matching on a low-frequency tick or event; pure-logic tests | L: matching is easy, stability is not | LT's new matcher was broken and unbounded orders stalled the game: test-first and bound outstanding orders |
| Player orders (F-090, F-101) | absent | Direct once books exist | UI over the order book | M | |
| Population demand (F-084) | absent | Needs custom work | Demand model per body/station (real populations are zero off Earth, so this is a design choice) | M | Real Solar System has no populations beyond Earth: fiction needed |
| NPC pricing (F-093) | absent | Needs custom work | EMA price history per book, valuation per agent | M | |
| Cargo by mass (F-094) | absent | Direct | Cargo fragment | S | |
| Reward-pool missions, data redemption (F-097, F-098) | absent | Needs custom work | Mission records in a subsystem, data items as cargo | M | |
| Tech licensing (F-099), personality data market (F-100) | absent | Needs custom work | Depends on research and personalities | M | |
| Economy stability controls and diagnostics (section 8.4 of SimResearch) | absent | Needs custom work | Gameplay Debugger category, Visual Logger, CSV/Insights traces, soak-test smoke flag | M | LT's own failures show this is not optional |
| Time-warp interaction | n/a in LT (its time acceleration was a dev hack, F-036) | Needs custom work | Economy ticks in sim time, NPC travel resolved abstractly at warp | L | SOLTest's 30 d/s warp means agents cannot fly in real time during warp: biggest SOLTest-specific economy risk |

### 4.4 Industry: mining, scanning, production, stations, colonies

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Ore-bearing asteroids, deposits, access points (F-103, F-104, F-217) | absent (asteroids are inert, on rails) | Direct | Ore composition fragment on belt entities, seeded from the asteroid id; real spectral types (C/S/M) are available in JPL data for named ones | M | Deposit depletion is persistent state, needs save |
| Transfer-unit beam mining, overflow, stress tell, blind mining, audio (F-105 to F-109, F-315, F-325) | absent | Direct | Beam as a Niagara ribbon; rate rules in pure logic; MetaSounds parameter for mass flow | M | |
| Drone bay and prospecting drones (F-110 to F-112) | absent | Needs custom work | Drones as a small Mass archetype attached to asteroid entities | M | Drones ride an asteroid moving at ~17 km/s heliocentric; must be stored relative to the asteroid |
| Scanner 1.0/2.0/3.0, signature types, catalogue (F-113 to F-118, F-116) | absent | Needs custom work | Signature evaluation in a subsystem over bodies, minor bodies and ships; UMG/Slate custom-drawn spectrum widget | L: design-heavy, LT itself iterated 3 times | Good fit with real data (star brightness, body temperatures) |
| Industrial modules, construction module, loop production, munition loading (F-119 to F-123) | absent | Needs custom work | Recipe Data Assets; production queues as SoA in a production subsystem | M | |
| Tech Lab, Production Lab, raw material requirements (F-124 to F-126) | absent | Needs custom work | Same production subsystem; blueprint items | M after research model | |
| Stations, asteroid bases, station services (F-127 to F-129) | absent | Needs custom work | Station archetype: Kepler elements relative to a parent body (reuse `SOLKepler`), Nanite meshes, a frame-lockable `ISOLTargetable`; services as data | L: first world objects with gameplay | Must ride the anchor/rebasing scheme; stations near the player become Actors, far ones are data |
| Planetary colonies, colony types and transforms (F-130, F-131) | absent | Needs custom work | Colony records per body; icons via the existing map overlay | M | Real bodies, so colonies are fiction placed on real planets/moons |
| Player-built infrastructure (F-132) | absent | Needs custom work | Construction jobs that spawn stations into the registry; persistent | L | Save format must hold dynamic bodies |
| Mining-ship class (F-133) | absent | Direct once ship classes exist | Ship-class Data Asset | S | |

### 4.5 AI and simulation

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| NPC ship entities with movement (prerequisite for all below) | planned (Part 10, without behaviour) | Direct | Reuse ship archetype and `USOLShipFlightProcessor` (ARCHITECTURE §8) | M | Part 10's design should include an AI control fragment, not only a stress test |
| Escort, follow, wingman, patrol, leader AI (F-134, F-135) | absent | Direct | MassAI StateTree per entity writing the ship control fragment; formation offsets in the leader's frame | M | First user of MassAI/StateTree in the project |
| Abstract low-LOD simulation, multi-level abstraction (F-136, F-137, F-334) | absent | Needs custom work | Mass LOD (`MassLOD`) for tiers near the player; a separate aggregate simulator (statistical, per body region) for the rest | XL: the core enabling tech for 1M agents | Essential for the 1M goal and for time warp |
| Master planner, fluid goals, emergent professions (F-140, F-141, F-143) | absent | Needs custom work | Utility scoring or GOAP written in C++ over a small action set; StateTree for execution; LT itself replaced its planner with a task system | L to XL | A general planner per agent at 1M is not affordable; plan only for near or "important" agents |
| Reactive AI (F-142), hypothetical-simulation reasoning (F-139) | absent | Needs custom work | Opportunity events via Mass Signals; "what-if" queries on the aggregate simulator | XL | F-139 was a concept even in LT |
| Delegation, NPC-posted contracts, AI-run missions (F-145, F-154) | absent | Needs custom work | Contract board subsystem shared by player and NPCs | L | |
| Profit-driven professions, miner/pirate equilibrium (F-149, F-152) | absent | Needs custom work | Job selection by expected payout (LT's new code sampled 100 random jobs); Mass processors at low frequency | L | |
| Faction leaders, business-strategy AI, meta projects (F-150, F-151, F-153) | absent | Needs custom work | Leader agents as plain C++ objects in a faction subsystem, ticking on sim-time intervals | L | |
| Personality traits, birth at colonies, birthplace inference (F-156 to F-158) | absent | Direct | 7-float fragment; seeded from colony culture | S to M | |
| History generation (F-138) | absent | Needs custom work | Offline or boot-time coarse simulation over the aggregate model | L | Never built in LT |
| Population scale (F-147) | partial (Mass base exists; ~19k inert entities today) | Needs custom work | Mass tiers: full AI near the player, cheap movement mid-range, aggregate far | XL toward 1M | LT reached ~128 per system in the videos; SOLTest's goal is ~4 orders of magnitude larger, so a tiered model is the only plausible path (estimate) |
| AI keeps running on script errors (F-162), leader brain view (F-161) | absent | Direct | Gameplay Debugger category and Visual Logger for AI introspection | M | |

### 4.6 Factions and missions

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Factions as hierarchies, named factions, pirate faction (F-164, F-170, F-172) | absent | Direct | Faction Data Assets plus a faction subsystem; faction id fragment on agents | M | |
| Projects with P/L, player projects, empire management (F-165 to F-169, F-268) | absent | Needs custom work | Project records in the faction subsystem; CommonUI projects screen | L | |
| Security, bounty hunters (F-171) | absent | Needs custom work | Region security values (per body sphere of influence) | M | |
| Zone claims (F-174) | absent | Needs custom work | Ownership per body or region | M | |
| Diplomacy beyond hostility (F-179) | absent | Needs custom work | Relation matrix plus events | M to L | LT never built it, so no reference design |
| Missions v1 procedural (F-175, F-240) | absent | Direct | Mission templates in Data Assets; mission board UI | M | |
| Continuous reward-pool missions, data proofs, player-posted missions (F-176 to F-178, F-271) | absent | Needs custom work | Contract board subsystem shared with AI (4.5) | M | |

### 4.7 Research and procedural technology

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Procedural items balanced by value, generated names (F-180, F-211 items) | absent | Needs custom work | Item generator in pure logic seeded per item; stats as `FInstancedStruct` modifiers | M | |
| Authored tech tree (F-182, F-194, F-193) | absent | Direct | Data Assets with prerequisites; a CommonUI graph view | M | |
| Procedural modifier tech, blueprint derivation, generations, attributes (F-183 to F-189, F-192) | absent | Needs custom work | Blueprint = base item + modifier list; derivation as seeded random walk in pure logic; validation tests against runaway growth | L | Balancing is open-ended; LT showed it only as a demo |
| Assembly chips, physical technology (F-190, F-191) | absent | Needs custom work | Items in cargo; theft via combat drops | M after cargo and research | |
| Research colonies and projects (F-196) | absent | Needs custom work | Depends on colonies and projects | M | |

### 4.8 Universe and procedural generation

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Procedural nebulae, dusty vs clear systems (F-197, F-216) | n/a (real Milky Way sky from catalogue) | Poor fit | — | n/a | Would contradict the real-sky design |
| Procedural asteroids with craters, ice asteroids, ice subsurface (F-199, F-203, F-302) | partial (placeholder grey sphere ISM, 2 variants) | Direct | Offline-generated Nanite rock meshes (Modeling Tools or Houdini), a few dozen variants, subsurface material for icy bodies | M | Belt mesh has no LOD today (tech debt) |
| Infinite streamed fields, density modulation, Zone Tech (F-200 to F-202) | partial (belt and streamed ring rocks are the SOLTest equivalent) | Direct | Extend the ring-patch streaming design to belt clusters | M | Real belt density is very low; dense fields are fiction unless placed in rings |
| Procedural planets, planet surfaces, water (F-204, F-205, F-301) | planned (Part 9) | Needs custom work | Part 9 cube-sphere terrain (PCG/Nanite or virtual heightfield) | XL | Already the hardest roadmap part |
| Planetary rings (F-207) | **present and ahead** | — | — | — | See 4.12 |
| Universe hierarchy, infinite universe, system layout (F-208 to F-210) | n/a | Poor fit | — | n/a | Single real system by design |
| Name generators (F-211) | absent | Direct | Grammar tables in Data Tables | S | For stations, NPCs, factions, ships |
| Procedural ships, stations, turrets (F-212, F-213, F-221) | absent (placeholder primitive ship) | Needs custom work | Either authored kitbash modules plus PCG/Geometry Script assembly, or authored meshes (Part 8) | L | Real art (Part 8) may make procedural ships unnecessary |
| Spatial coherence, colony cultures (F-214, F-215) | absent | Direct | Seeded noise over body/region | S | |
| History-driven placement (F-138, F-222) | absent | Needs custom work | See 4.5 | L | |
| Procedural icons, procedural UI layout (F-219, F-220) | absent | Direct | Material-based icon generator or render target | S | |

### 4.9 UI and UX

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| New-game setup, name generator (F-012, F-223) | partial (start-location picker exists) | Direct | Extend main menu | S | Needs save slots |
| Top bar / player menu, personal interface (F-225, F-267) | absent | Direct | CommonUI activatable widget stack | M | Introduces CommonUI (SOLTest uses plain UMG in C++) |
| Command interface (RTS), asset panel, leader-follower groups (F-226 to F-230) | absent | Needs custom work | Reuse the jump-map camera and overlay for a tactical view; selection and orders over Mass entities | L | Map already renders at any scale, a strong starting point |
| Ship viewer, inventory, cargo, power UI (F-231 to F-235) | absent | Direct | UMG/CommonUI, a render-target ship preview | M | |
| Ships menu, job board, contract board, missions, database screens (F-239, F-240, F-254, F-271) | absent | Direct | CommonUI lists bound via MVVM (ModelViewViewModel plugin, present in 5.8) | M each | |
| HUD v1 widgets, radial subsystem rings, mining widgets, minimap (F-241, F-242, F-262, F-282) | partial (Canvas HUD and 3D radar exist) | Direct | Extend `ASOLFlightHud` | S to M | `Ship/`-`UI/` include cycle should be broken first |
| Commodity market screen and trading charts (F-264, F-269, F-281) | absent | Direct | Slate custom-drawn chart widget; CommonUI market screen | M | |
| System map, hierarchical map (F-250, F-251) | partial (jump map is a live system map) | Direct | Add trade-route, ownership and NPC layers to the map | M | SOLTest's map is already ahead in scale range |
| Split panels, workspaces, pop-outs, categorical filters (F-252, F-253, F-258, F-259) | absent | Direct | CommonUI/UMG layout | M | |
| Cylindrical/curved HUD, glass HUD, cockpit mode, UI compositing (F-255 to F-257, F-280, F-307) | absent | Direct | Widget Component on a curved mesh, or a post-process distortion of the UI layer | M | Purely presentational |
| Radial context menu, object info window, world-space overlays (F-260, F-276, F-277, F-278) | absent | Direct | UMG with world-to-screen projection (HUD already projects targets) | M | |
| UI colour picker, opacity (F-236, F-237) | absent | Direct | Settings entries | S | Persist via the settings save |
| UI-to-texture (F-285) | absent | Direct | Widget Component / render target | S | |
| Gamepad and joystick (F-248) | absent (keyboard and mouse only) | Direct | Enhanced Input contexts, CommonUI input routing | M | |
| VR (F-249) | absent | Direct (OpenXR) | OpenXR plugin | L | Never shown in LT either |

### 4.10 Rendering and effects

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Lens flares (F-299), god rays (F-295) | planned (Sun glow and flare ToDo) | Direct | Post-process lens flare, custom flare material, light-shaft settings | S to M | Game-driven exposure must stay in control |
| Space dust, volumetric dust, dust transitions, motion flecks (F-294, F-296, F-297, F-303) | absent | Direct | Niagara camera-relative particles; Heterogeneous Volumes or volumetric fog for dust | M | Real interplanetary space has no visible dust; flecks only as a speed cue |
| Dynamic lights from weapons and thrusters (F-298) | partial (combat VFX are emissive; ship/targets lit by a CPU sunlight light) | Direct | Niagara light renderer / Megalights (verify availability in 5.8) | S to M | |
| Engine trails (F-309), cruise glow | absent | Direct | Niagara ribbons in ship frame | S | Trails in render space hit the known TSR smear issue |
| Impostors, popping fix (F-310, F-311) | partial (log-depth placement, body icon LOD) | Direct | Nanite plus HLOD or octahedral impostors for stations and fleets | M | |
| Atmospheric scattering, clouds (F-300) | planned (Part 9); gas giants already volumetric | Direct | SkyAtmosphere per rocky planet (SOLTest already uses SkyAtmosphere as a carrier for gas giants) | M to L | One SkyAtmosphere per scene is the engine norm; multiple planets need a switching scheme |
| Holographic look, glitch effect, procedural plating (F-290, F-308, F-263) | absent | Direct | Materials | S to M | |
| Chromatic aberration, colour grading, bloom, HDR, PBR, AA (F-291 to F-293, F-304 to F-306) | present via UE (TSR, post-process); jump effect uses chromatic aberration | — | — | — | Engine-provided |
| Mining, shield, warp VFX (F-314 to F-316) | partial (shield-break and warp effects exist) | Direct | Niagara | S each | |

### 4.11 Audio

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Real weapon and explosion SFX (F-320, F-326) | partial (7 synthesized placeholders) | Direct | Licensed or authored assets; MetaSounds | S to M | Part 8 |
| Dynamic tension music, composer soundtrack (F-321, F-322) | absent (music sound class exists, empty) | Direct | MetaSounds with a tension parameter, Quartz for musical timing, Audio Modulation | M | |
| Scanner, mining, zone ambience audio (F-323 to F-325) | absent | Direct | MetaSounds driven by game parameters | S each | Depends on scanner and mining |

### 4.12 LT features SOLTest already matches or exceeds (for completeness)

| LT feature (F-IDs) | SOLTest equivalent |
|---|---|
| Third-person flight, controls, roll (F-001 to F-003) | 6DoF with chase cam, rate-command quaternion rotation |
| Rigid-body collision against large bodies (F-007) | Analytic swept sphere, no tunnelling at 0.5c, never disabled |
| Time acceleration (F-036) | First-class warp ladder to 30 d/s with frame carry |
| Double precision, dynamic loading (F-332, F-333) | `FVector3d` universe, rebasing, streamed ring rocks |
| Starfield (F-198) | Real AT-HYG sky with physical magnitudes |
| Planetary rings (F-207) | Real optical-depth profile, 1M-rock layer, analytic shadows |
| Pulse lasers, explosions (F-049, F-312) | Pulse bolts at 20k scale, explosion, debris and shield-break effects |
| Settings, key rebinding (F-224, F-369) | 29 rebindable actions with conflict checks, live key labels |
| Unicode text (F-337) | Engine-native |
| Profiling (F-339) | `stat`, Unreal Insights, measured numbers per part |

---

## 5. Feature gap list: editor and tooling features

SOLTest's editor side today: Python scripts that build materials and assets headlessly, a generated shader include, one Data Asset type (`USOLStarFieldData`), one editor-only function library, unreal-mcp for driving the editor, `-SOL*` smoke flags, automation tests and two A/B console variables. There is no custom editor module.

Unlike LT, SOLTest starts from the Unreal Editor, which already provides most of what LT built by hand (reflection, a generic property inspector, asset browser, undo/redo, source control, packaging).

| LT feature (F-IDs) | Present in SOLTest? | UE 5.8 feasibility | Suggested UE mechanism | Difficulty | Risk / notes |
|---|---|---|---|---|---|
| Reflective data model (F-336) | present via UE reflection (`UCLASS`/`USTRUCT`/`UPROPERTY`) | Direct | Already available | — | LT built this from scratch; UE gives it free |
| LT Data Editor: generic inspector, live editing, no special-case code (F-350 to F-352, F-356) | partial (Details panel works on any asset; SOLTest keeps tunables as C++ constants in `SOLConstants.h`) | Direct | Move tunables that designers need into Data Assets / Data Tables / `UDeveloperSettings`; Property Matrix for bulk editing; PIE edits of Data Assets apply live if code reads them each use | S to M: mostly migration of constants | Constants are deliberately compile-time today; a mix is likely best (open question) |
| Graph layouts where shape = type (F-353 to F-355) | absent | Needs custom work | Custom `EdGraph`-based asset editor, or an Editor Utility Widget that draws a graph | M to L | Novelty value; Details panel plus Reference Viewer covers most practical needs |
| Data-editor operations LT never had: undo/redo, save, validation, search, dependencies, versioning (SimResearch 18.5) | present via UE editor (transactions, asset save, Reference Viewer, source control) | Direct | Add Data Validation plugin rules (`UEditorValidatorBase`) for content | S to M | |
| In-game data inspection (F-357) | absent | Direct | Gameplay Debugger categories; console commands; Mass debugger in the editor | S to M | |
| LTSL scripting language (F-341, F-362, F-363) | absent | Poor fit (as a language) | Blueprints for designer logic, StateTree for behaviour, Data Assets for generator parameters, C++ for systems, Python for editor automation. Verse is not available to C++ UE projects (UEFN only, as far as known) | n/a | A custom language would duplicate UE; LT's reasons (no editor, no reflection) do not apply |
| Hot reload (F-342) | partial (Live Coding exists; CLAUDE.md does not rely on it and closes the editor to build) | Direct | Live Coding for C++ body changes; Blueprint and StateTree recompile in PIE; Data Assets live | S to adopt | Live Coding cannot change reflected layouts safely; full builds remain needed |
| Soft compile failure, AI keeps running (F-343, F-162) | absent | Poor fit | Blueprint compile errors already isolate assets; C++ has no equivalent | n/a | Not worth emulating |
| Compiler errors in game (F-364) | n/a | Direct | Message Log | — | |
| Launcher architecture, apps as scripts (F-344) | partial (`-SOL*` scenario flags select behaviours) | Direct | Command-line flags (existing), separate test maps, commandlets | S | |
| Test beds: AI combat stats, market, rail, UI, HUD (F-345, F-075) | partial (smoke runs: flight, input, HUD, map, jump, level, menus, combat demo and stress, stars) | Direct | Dedicated test maps plus Functional Tests or `-SOL*` smoke flags; Gauntlet for long soak runs | S to M per bed | Headless runner currently stalls (tech debt), which blocks unattended AI soak tests |
| 3D model test bed for generators (F-346) | partial (Python scripts regenerate assets; Modeling Tools plugin enabled) | Direct | Editor Utility Widget that reruns a Geometry Script or PCG generator with parameters in a preview viewport | M | Only needed if procedural ships/stations are chosen |
| Procedural model editor with symmetry (old-code `ModelEditor`) | absent | Direct | Modeling Tools / Geometry Script | M | |
| Dev tools menu: steal asset, transfer, jump to object (F-347) | partial (jump map; console smoke flags) | Direct | `UCheatManager` subclass, console commands, an Editor Utility Widget or in-game debug UMG panel | S | |
| Dev region viewer, system view with AI on rails (F-243, F-251 debug) | partial (jump map shows all bodies at any zoom) | Direct | Add debug layers to the map (agent positions, routes, trade flows) | M | |
| Tech-tree viewer and editor (F-193, F-194) | absent | Needs custom work | EdGraph asset editor for authored trees; runtime CommonUI graph for players | M to L | Only if research is adopted |
| AI introspection: goal inspector, decision trace, leader brain view (F-161, SimResearch 10.4) | absent | Direct | Gameplay Debugger, Visual Logger, StateTree debugger, Rewind Debugger, Mass debugger | S to M to wire up | Requires AI to exist |
| Economy diagnostics: price spikes, flow visualisation (SimResearch 8.4) | absent | Needs custom work | Visual Logger, Gameplay Debugger, CSV export, Insights trace channels, map overlay | M | |
| Seed capture and replay, deterministic scenarios (SimResearch 10.4, 13.3) | partial (universe is deterministic from sim time; `-SOLSimDate`, `-SOLStart` scenario flags) | Direct | Seed and sim-time in save files; deterministic automation tests | S to M | Deterministic economy requires fixed-order processing (Mass parallel chunks need care) |
| Profiling (F-339) | present (`stat`, Insights, measured per part) | Direct | Add Insights trace channels for AI and economy | S | |
| Data-level modding, script modding, moddable UI, mod sharing (F-359 to F-361, F-366, F-370) | absent | Needs custom work | Game Feature plugins or pak/IoStore mounting for content mods; Data Asset overrides; mod.io or a UGC plugin; Blueprint-only mods via a mod SDK project | L to XL | UE modding of a C++ game is possible but heavy; decide early whether it is a goal |
| Unicode text, fonts (F-337, F-367) | present via UE | — | — | — | |
| Capture and recording (F-348) | partial (`-SOLSmokeShot` screenshots) | Direct | Movie Render Queue, Take Recorder | S | |
| Patcher / updater (old code) | absent | Direct | Platform store patching or UE chunk patching | M | Only for distribution |

---

## 6. Can all of LT's features be added in Unreal? Assessment and adoption order

### 6.1 Short answer

**Yes, almost all of them can be built in Unreal 5.8, but very few come for free, and the hard part is not the engine.** LT's gameplay systems (economy, AI, factions, research, missions) are game logic. No engine provides them; LT built them by hand on a custom engine and still never finished. In UE they would also be hand-written, but on a far better base: reflection, an editor, Mass, StateTree, Smart Objects, Niagara, CommonUI, MVVM, Data Assets, SaveGame and Insights all exist.

A minority of LT's features are **poor fits** for SOLTest specifically, because they contradict its design (single real Solar System, real sky) or because UE already solves the problem LT was working around (LTSL).

### 6.2 Classification

| Category | LT features | Why |
|---|---|---|
| **Fits directly** (stock UE plus modest game code) | Cruise mode, camera modes, weapon variety, turrets, shields, lead indicator, damage and repair, ship classes, cargo by mass, credits, commodities, missions v1, name generators, personalities, UI screens (market, cargo, ship viewer, mission board), HUD additions, lens flares, dust, trails, impostors, audio and music, dev menus, AI introspection tools, live data editing via Data Assets | UE has the building blocks; SOLTest's pure-logic-plus-glue pattern applies unchanged |
| **Needs substantial custom work** | Stations orbiting bodies, docking in moving frames, mining with persistent deposits, scanner, order-book economy, NPC professions, faction leaders and projects, contracts shared by player and AI, production chains, procedural blueprint research, fleet command view, abstract off-screen simulation, Mass AI at scale, save/load of a living universe, modding | Engine gives primitives only; the systems are the game. Most must also be adapted to double-precision universe coordinates, Keplerian frames and time warp |
| **Poor fit or not worth it** | Procedural nebulae and procedural universe hierarchy, jump holes between systems, LTSL as a custom language, soft compile failure, history generation (low value for a real system), a fully emergent per-agent economy at ~1M agents | Contradicts the real-Solar-System design, duplicates UE features, or is computationally implausible (see 6.3) |

### 6.3 Specific judgements

- **LTSL vs Blueprints, C++ and StateTree.** LTSL existed because LT had no editor, no reflection and no hot reload. UE provides all three. The LTSL roles map onto: StateTree (behaviour logic), Data Assets (generator parameters), Blueprints (designer scripting, if wanted), C++ with Live Coding (systems), Python (editor automation, already used). Recommendation: do not build a language. If modding with logic becomes a goal, expose a curated Blueprint API.
- **Procedural tech tree.** Feasible as pure logic (blueprint = base item plus modifier list, derivation as a seeded random walk). The engineering is moderate; the balancing is open-ended. LT itself only demoed it. Treat as late, optional depth.
- **Fully emergent economy at scale.** LT ran about 100 to 128 economically active NPCs per system in the videos (2,000+ ships at PAX 2018 in the new code). SOLTest's ~1M-entity goal is roughly four orders of magnitude more. A per-agent planner or order-book participant for a million agents every frame is not plausible (estimate, not measured). A plausible design is tiered: full StateTree agents near the player (hundreds to low thousands), cheap scripted movement mid-range (Mass LOD), and an aggregate statistical simulation (flows between bodies) for everything else, with agents materialising from and dissolving into the aggregate. LT called this "abstract simulation" (F-136) and never finished it.
- **Time warp is SOLTest's unique complication.** At 30 days per second an NPC trader cannot fly its route in real time. Economy and travel must resolve in sim time (legs with departure and arrival times, positions computed analytically), not in real-time physics. This is consistent with SOLTest's existing approach (bodies are analytic, O(1) at any time) and argues for analytic NPC transit when warp is above some threshold.
- **Floating origin and stations.** Stations and NPC ships must live in universe coordinates (`FVector3d`, body-relative Kepler elements for stations), with Actors only near the viewpoint, exactly like bodies today. Engine systems that assume static world positions (ZoneGraph lanes, NavMesh, Smart Object placement in level space, Chaos scenes spanning large areas) need wrapping or are avoided. Smart Objects can still be used as data (slots on a station) if their world transforms are updated with the anchor.
- **Mass for everything.** NPC ships fit the existing ship archetype (ARCHITECTURE §8). Markets, factions and contracts are better as SoA data in subsystems than as entities, the same deviation SDD 7 already accepted for bolts and targets. That deviation should be written down in an SDD rather than repeated case by case.

### 6.4 Overall difficulty (estimate)

| Scope | Estimate | Comment |
|---|---|---|
| LT's pilot loop (ship systems, weapons, enemies, damage, stations, docking, market with simple prices, missions v1, save/load) | **L** overall (a few months at SOLTest's current pace) | Mostly "fits directly" items; the main new pieces are stations and save/load |
| LT's living world (NPC professions, order-book economy, factions and projects, contracts, mining and scanning) | **XL** | The core of LT; balancing and debugging dominate |
| LT's depth layer (production, procedural research, colonies, fleet command, abstract simulation) | **XL** | Open-ended |
| All of the above at ~1M entities | **XL+**, research-grade | Requires the aggregate simulator and closing the scale tech debt; no reference implementation exists in LT |
| Tooling (Data Assets migration, debug views, test beds) | **M** | UE removes most of LT's tooling work |

### 6.5 Dependencies

```
Save/load (sim time, player, persistent changes)
   └─ needed by everything with state: deposits, wallets, factions, research

NPC ship entities (Part 10 archetype + AI control fragment + MassAI/StateTree)
   ├─ escort/patrol/combat AI ──── enemies, fleet battles, fleet command view
   └─ professions (miner, trader, pirate)
          └─ needs: stations, commodities, cargo, mining, markets

Ship damage + weapon variety ── enemies that shoot back ── piracy, security

Stations (Kepler-bound, targetable, dockable)
   ├─ docking ── station services ── equipment market, ship dealer, repair
   ├─ markets (fixed price first, then order books)
   └─ contracts/mission board ── factions and projects

Asteroid composition + collision + targeting (ToDo items)
   └─ scanner, drones ── beam mining ── commodities flow into markets

Item model (FInstancedStruct items, cargo)
   └─ fitting, loot, production, blueprints, research

Aggregate (abstract) simulation + Mass LOD tiers
   └─ economy at scale, time warp with a living economy, the 1M goal
```

### 6.6 Recommended adoption order (phases), mapped onto the roadmap

The order follows LT's own lesson (SimResearch 20.2): build the gameplay vertical slice before more engine polish, and keep phases strictly ordered.

| Phase | Content | Maps onto | Prerequisites | Size (estimate) |
|---|---|---|---|---|
| **A. Foundations for a game** | Save/load (sim time, player state, schema-versioned per CLAUDE.md persistence rules); ship damage, death and respawn; minor-body collision and targeting (existing ToDo items); close the combat-grid scan tech debt; break the `Ship/`-`UI/` cycle | New tickets; two existing ToDo items | none | M to L |
| **B. NPC ships with behaviour** | Redesign Part 10 so the NPC archetype carries an AI control fragment; adopt MassAI StateTree; escort, patrol, attack; enemies with pulse guns; lead indicator; Gameplay Debugger and Visual Logger categories | Part 10 (#11), reshaped from "stress demo" to "NPC foundation plus stress demo" | A | L |
| **C. Places and items** | Stations as Kepler-bound targetable objects with Nanite meshes, docking, item model and cargo, credits, fixed-price station market, ship classes as Data Assets, mission board v1 | New tickets; Part 8 art for ships and stations | A, B | L |
| **D. Industry loop** | Asteroid composition, scanner, prospecting drones, beam mining, commodities feeding station markets | New tickets; builds on Part 5's belt data | C | L |
| **E. Living economy** | NPC miners, haulers, traders, pirates choosing jobs by profit; order books with bounded orders; price history; sim-time-based transit for warp; economy diagnostics; aggregate simulation for far agents | New tickets; extends Part 10's scale goal | B, C, D | XL |
| **F. Factions and contracts** | Factions, hostility, projects with P/L, NPC-posted contracts, reward-pool missions, security | New tickets | E | L to XL |
| **G. Depth** | Production chains, procedural research and blueprints, colonies, fleet command view on the jump-map camera, personalities | New tickets | F | XL |
| **Parallel tracks** | Part 8 (art, audio, music with a tension parameter), Part 9 (terrain and atmospheres, enabling colony landing), tooling (Data Asset migration of tunables, test maps, validation rules), presentation features (lens flare, dust flecks, trails) | Parts 8 and 9, ToDo items | independent | per part |

Phases A to C give a playable pilot loop comparable to LT's 2013 prototype. Phases D to F are LT's core identity. Phase G is where LT itself stalled.

---

## 7. Open questions, assumptions and unverified claims

### 7.1 Open questions for the project owner

1. **Is a game loop the goal at all?** SOLTest so far is a technology and visual foundation. Should it grow into an LT-like sandbox, a combat game, an exploration sim, or stay a scale showcase?
2. **Single real Solar System as a hard constraint?** If yes, LT's procedural universe, jump holes and nebulae stay out. If not, a hybrid (real Sol plus fictional systems) changes chapter 4.8 a lot.
3. **Realism vs fiction for the economy.** The real Solar System has no populations beyond Earth. Where do markets, colonies and stations come from (near-future fiction, a chosen year)?
4. **Time warp vs living economy.** Should warp be limited while NPC activity is near, or should NPC travel become analytic at high warp (recommended in 6.3)?
5. **What does "~1M entities" mean for AI?** One million moving bodies, one million AI agents, or one million things the economy tracks? This decides whether the aggregate simulator is required.
6. **Should Part 10 be reshaped** from a behaviour-less stress demo into the NPC foundation (phase B)?
7. **Mass-for-everything scope.** Are markets, factions and contracts allowed to be subsystem SoA data rather than Mass entities (as bolts already are under SDD 7 Amendment 1)?
8. **Tunables in C++ constants vs Data Assets.** Should designer-facing values move to Data Assets (live editing, LT Data Editor equivalent), accepting less compile-time checking?
9. **Modding.** Is modding a goal? It changes data format decisions early (Data Assets and Game Feature plugins).
10. **Procedural ships and stations vs authored art** for Part 8?
11. **UI framework.** Adopt CommonUI and MVVM before the strategic screens multiply (LT rewrote its UI five times)?
12. **Multiplayer.** Assumed out of scope (LT never planned it); Mass and the custom rebasing would make it hard later.

### 7.2 Assumptions made in this document

- Difficulty ratings (S/M/L/XL) assume SOLTest's current workflow (AI-assisted parts, TDD, review per part) and are rough estimates, not measurements or schedules.
- "Direct" feasibility assumes the stock UE 5.8 plugins found in the local engine install: MassGameplay, MassAI, StateTree, GameplayStateTree, SmartObjects, ZoneGraph, GameplayAbilities, CommonUI, ModelViewViewModel, PCG, GameFeatures, ModularGameplay, DataRegistry, Chooser and Rewind Debugger are present under `C:\Program Files\Epic Games\UE_5.8\Engine\Plugins`. Their maturity level (experimental, beta, production) in 5.8 was **not checked**.
- SOLTest currently depends only on `MassEntity` and `MassCore` among the Mass modules (`SOLTest.Build.cs`); MassAI and StateTree would be new dependencies.
- LT feature status is taken from `SimResearch.md` as written, including its "shown", "prototype" and "planned" labels. LT never shipped, so "shown" never means "shipped".

### 7.3 Unverified claims and items to check before relying on them

| Claim | Status |
|---|---|
| A tiered model is required for AI at ~1M entities; a per-agent planner at that count is not affordable | Engineering judgement, not measured. Part 10 profiling would settle it |
| ~20 ms per frame for `BuildMinorBodyGrid` at 1M entities | Linear extrapolation from a measured ~0.4 ms at 19,268 entities (SDD 7 Amendment 4), not measured at 1M |
| Verse is unavailable to C++ UE projects outside UEFN | Based on general knowledge, not checked against 5.8 |
| Megalights availability and Heterogeneous Volumes suitability for dust in 5.8 | Not checked |
| One SkyAtmosphere per scene as a practical limit for multiple rocky planets | Engine-norm assumption; SDD 14 uses SkyAtmosphere only as a carrier, so verify in Part 9 |
| Smart Objects usable on moving stations if transforms are updated with the anchor | Untested design idea |
| Live Coding usable for SOLTest iteration | CLAUDE.md says it is "not relied on"; not tested for this project |
| LT feature details (scanner version in code, hot-reload path in LTSL, double precision in old code) | Marked uncertain in SimResearch chapter 22.2 itself |
| Real spectral types available for SOLTest's 500 named asteroids in JPL SBDB | Likely for many, not checked for the specific list |
