# Graph Report - .  (2026-09-28)

## Corpus Check
- Corpus is ~14,500 words - fits in a single context window. You may not need a graph.

## Summary
- 91 nodes · 128 edges · 18 communities (12 shown, 6 thin omitted)
- Extraction: 76% EXTRACTED · 24% INFERRED · 0% AMBIGUOUS · INFERRED: 31 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- [[_COMMUNITY_Style Guide & Perf Review|Style Guide & Perf Review]]
- [[_COMMUNITY_Kepler Orbits & Sim Clock|Kepler Orbits & Sim Clock]]
- [[_COMMUNITY_Project Rules & Architecture SDD|Project Rules & Architecture SDD]]
- [[_COMMUNITY_UE Module & Build Config|UE Module & Build Config]]
- [[_COMMUNITY_Scale, Rebasing & Jump Map|Scale, Rebasing & Jump Map]]
- [[_COMMUNITY_Testing & Flight Model|Testing & Flight Model]]
- [[_COMMUNITY_Combat, Surface-Lock & Roadmap|Combat, Surface-Lock & Roadmap]]
- [[_COMMUNITY_Star Field Research|Star Field Research]]
- [[_COMMUNITY_SOLTypes Namespace|SOLTypes Namespace]]
- [[_COMMUNITY_Local Claude Settings|Local Claude Settings]]
- [[_COMMUNITY_unreal-mcp Config|unreal-mcp Config]]
- [[_COMMUNITY_Git Rule|Git Rule]]
- [[_COMMUNITY_Persistence Rule|Persistence Rule]]
- [[_COMMUNITY_Subagent Model Policy|Subagent Model Policy]]

## God Nodes (most connected - your core abstractions)
1. `SDD 1: Solar-system architecture decisions` - 20 edges
2. `SOLTest C++ Style Guide (draft)` - 13 edges
3. `CLAUDE.md project guidance` - 10 edges
4. `UE5 C++ performance guidelines (Epic + Looman)` - 9 edges
5. `Solar-System Architecture Roadmap Plan` - 7 edges
6. `SOLTest` - 6 edges
7. `Origin rebasing with anchor hysteresis` - 6 edges
8. `Analytic Keplerian orbits, no perturbations` - 6 edges
9. `FSOLKeplerElements` - 5 edges
10. `FSOLState` - 5 edges

## Surprising Connections (you probably didn't know these)
- `SOLTest C++ Style Guide (draft)` --references--> `SOL namespace physical constants`  [EXTRACTED]
  Docs/STYLE_GUIDE.md → Source/SOLTest/Universe/SOLTypes.h
- `Module and file layout (feature folders, Build.cs rules)` --references--> `SOLTest`  [EXTRACTED]
  Docs/STYLE_GUIDE.md → Source/SOLTest/SOLTest.Build.cs
- `CLAUDE.md project guidance` --references--> `unreal-mcp server (HTTP 127.0.0.1:33445)`  [EXTRACTED]
  CLAUDE.md → .mcp.json
- `Tech debt: untested SOLTypes.h and SOLKepler.h` --references--> `FSOLKeplerElements`  [EXTRACTED]
  CLAUDE.md → Source/SOLTest/Universe/SOLKepler.h
- `FSOLSecularElements` --implements--> `Analytic Keplerian orbits, no perturbations`  [INFERRED]
  Source/SOLTest/Universe/SOLKepler.h → Docs/SDDs/1-solar-system-architecture.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **SOLTest build configuration (targets plus module rules plus module impl)** — source_soltest_target_soltesttarget, source_soltesteditor_target_soltesteditortarget, soltest_soltest_build_soltest, soltest_soltest_primary_game_module [INFERRED 0.85]
- **Kepler orbit evaluation pipeline (secular elements to state vector)** — universe_solkepler_fsolsecularelements, universe_solkepler_fsolkeplerelements, universe_solkepler_solkepler_solveeccentricanomaly, universe_solkepler_solkepler_elementstostate, universe_soltypes_fsolstate [INFERRED 0.85]
- **Design governance document set (CLAUDE.md, SDD, plan, style guide, performance research)** — claude_project_guidance, sdds_1_solar_system_architecture, plans_1_solar_system_architecture_plan, docs_style_guide, research_ue_performance_guidelines [INFERRED 0.85]

## Communities (18 total, 6 thin omitted)

### Community 0 - "Style Guide & Perf Review"
Cohesion: 0.19
Nodes (14): Adversarial opus performance and style review, SOLTest C++ Style Guide (draft), File banner, function comment and separator rules, Asset paths live in one place (SOLConstants.h / data assets), Data-oriented Mass guidance (fragments, processors, no hot-loop allocation), TrueReality house style origin, UE reflection macro and UPROPERTY rules, UE5 C++ performance guidelines (Epic + Looman) (+6 more)

### Community 1 - "Kepler Orbits & Sim Clock"
Cohesion: 0.21
Nodes (14): Tech debt: untested SOLTypes.h and SOLKepler.h, Sim clock and time-warp, Naming conventions (SOL infix, m prefix, UPPER_SNAKE constants), Part 1a: universe types, Kepler solver, clock, registry (paused), Analytic Keplerian orbits, no perturbations, Time decision: real date start, time-warp on bodies only, ToDo: planetary perturbations / N-body integration, FSOLKeplerElements (+6 more)

### Community 2 - "Project Rules & Architecture SDD"
Cohesion: 0.27
Nodes (10): grill-me before new features workflow rule, CLAUDE.md project guidance, GAME_MECHANICS.md catalog, unreal-mcp server (HTTP 127.0.0.1:33445), README (SOLTest title stub), SDD 1: Solar-system architecture decisions, Ship pulled by body gravity; bodies on rails, Placeholder content and sphere surfaces first (+2 more)

### Community 3 - "UE Module & Build Config"
Cohesion: 0.22
Nodes (7): Module and file layout (feature folders, Build.cs rules), ModuleRules, SOLTest, SOLTest primary game module (FDefaultGameModuleImpl), SOLTestTarget, SOLTestEditorTarget, TargetRules

### Community 4 - "Scale, Rebasing & Jump Map"
Cohesion: 0.31
Nodes (9): Jump map (J) mechanic, Origin anchor 25% hysteresis, Double precision and camera-relative rendering (LWC) rules, Jump map decision (2-4 s warp, velocity-matched arrival), Origin rebasing with anchor hysteresis, True 1:1 scale with double-precision universe coordinates, ToDo: supercruise-style long travel, FSOLPosition / FSOLVelocity (FVector3d aliases) (+1 more)

### Community 5 - "Testing & Flight Model"
Cohesion: 0.29
Nodes (7): TDD with tests authored by separate subagent, UE Automation Framework testing (SOLTest.<Topic>.<Case>), Flight-assist vs Newtonian flight model, Solar-System Architecture Roadmap Plan, Part 1b: Mass ship entity, Pawn proxy, flight model, Flight model decision (assist default, Newtonian toggle), RunTests.ps1 headless automation test runner

### Community 6 - "Combat, Surface-Lock & Roadmap"
Cohesion: 0.29
Nodes (7): Combat mechanics (bolts, targets), Surface-lock (L) mechanic, Roadmap Parts 2-10 (jump map, level, star field, moons, weapons, HUD, art, terrain, scale demo), Niagara and rendering best practices, Data-driven projectile combat decision, Level / surface-lock decision, Custom ship-centric star field renderer (AT-HYG)

### Community 7 - "Star Field Research"
Cohesion: 0.60
Nodes (5): Star Field: CelestialVault vs AT-HYG research, AT-HYG star catalog (2.55M stars), CelestialVault engine plugin (beta, Earth-centric), Hybrid star renderer: bright sprites plus faint HDR cubemap, Reusable CelestialVault Milky Way texture and material functions

## Knowledge Gaps
- **19 isolated node(s):** `enabledMcpjsonServers`, `unreal-mcp`, `namespace`, `unreal-mcp server (HTTP 127.0.0.1:33445)`, `SOLTest primary game module (FDefaultGameModuleImpl)` (+14 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **6 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `SDD 1: Solar-system architecture decisions` connect `Project Rules & Architecture SDD` to `Style Guide & Perf Review`, `Kepler Orbits & Sim Clock`, `Scale, Rebasing & Jump Map`, `Testing & Flight Model`, `Combat, Surface-Lock & Roadmap`, `Star Field Research`?**
  _High betweenness centrality (0.335) - this node is a cross-community bridge._
- **Why does `SOLTest C++ Style Guide (draft)` connect `Style Guide & Perf Review` to `Kepler Orbits & Sim Clock`, `Project Rules & Architecture SDD`, `UE Module & Build Config`, `Testing & Flight Model`?**
  _High betweenness centrality (0.244) - this node is a cross-community bridge._
- **Why does `Solar-System Architecture Roadmap Plan` connect `Testing & Flight Model` to `Style Guide & Perf Review`, `Kepler Orbits & Sim Clock`, `Project Rules & Architecture SDD`, `Combat, Surface-Lock & Roadmap`?**
  _High betweenness centrality (0.119) - this node is a cross-community bridge._
- **What connects `enabledMcpjsonServers`, `unreal-mcp`, `namespace` to the rest of the system?**
  _25 weakly-connected nodes found - possible documentation gaps or missing edges._