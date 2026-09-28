# Graph Report - SOLTest  (2026-09-28)

## Corpus Check
- 57 files · ~38,321 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 492 nodes · 598 edges · 52 communities (36 shown, 16 thin omitted)
- Extraction: 94% EXTRACTED · 6% INFERRED · 0% AMBIGUOUS · INFERRED: 38 edges (avg confidence: 0.84)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `d102e51f`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_Style Guide & Perf Review|Style Guide & Perf Review]]
- [[_COMMUNITY_Kepler Orbits & Sim Clock|Kepler Orbits & Sim Clock]]
- [[_COMMUNITY_Project Rules & Architecture SDD|Project Rules & Architecture SDD]]
- [[_COMMUNITY_UE Module & Build Config|UE Module & Build Config]]
- [[_COMMUNITY_Scale, Rebasing & Jump Map|Scale, Rebasing & Jump Map]]
- [[_COMMUNITY_Star Field Research|Star Field Research]]
- [[_COMMUNITY_SOLTypes Namespace|SOLTypes Namespace]]
- [[_COMMUNITY_Local Claude Settings|Local Claude Settings]]
- [[_COMMUNITY_unreal-mcp Config|unreal-mcp Config]]
- [[_COMMUNITY_Git Rule|Git Rule]]
- [[_COMMUNITY_Persistence Rule|Persistence Rule]]
- [[_COMMUNITY_Subagent Model Policy|Subagent Model Policy]]
- [[_COMMUNITY_Community 18|Community 18]]
- [[_COMMUNITY_Community 19|Community 19]]
- [[_COMMUNITY_Community 20|Community 20]]
- [[_COMMUNITY_Community 21|Community 21]]
- [[_COMMUNITY_Community 22|Community 22]]
- [[_COMMUNITY_Community 23|Community 23]]
- [[_COMMUNITY_Community 24|Community 24]]
- [[_COMMUNITY_Community 25|Community 25]]
- [[_COMMUNITY_Community 26|Community 26]]
- [[_COMMUNITY_Community 27|Community 27]]
- [[_COMMUNITY_Community 28|Community 28]]
- [[_COMMUNITY_Community 29|Community 29]]
- [[_COMMUNITY_Community 30|Community 30]]
- [[_COMMUNITY_Community 31|Community 31]]
- [[_COMMUNITY_Community 32|Community 32]]
- [[_COMMUNITY_Community 33|Community 33]]
- [[_COMMUNITY_Community 34|Community 34]]
- [[_COMMUNITY_Community 35|Community 35]]
- [[_COMMUNITY_Community 36|Community 36]]
- [[_COMMUNITY_Community 37|Community 37]]
- [[_COMMUNITY_Community 39|Community 39]]
- [[_COMMUNITY_Community 40|Community 40]]
- [[_COMMUNITY_Community 41|Community 41]]
- [[_COMMUNITY_Community 42|Community 42]]
- [[_COMMUNITY_Community 43|Community 43]]
- [[_COMMUNITY_Community 44|Community 44]]
- [[_COMMUNITY_Community 45|Community 45]]
- [[_COMMUNITY_Community 46|Community 46]]
- [[_COMMUNITY_Community 47|Community 47]]
- [[_COMMUNITY_Community 48|Community 48]]
- [[_COMMUNITY_Community 49|Community 49]]
- [[_COMMUNITY_Community 50|Community 50]]
- [[_COMMUNITY_Community 51|Community 51]]

## God Nodes (most connected - your core abstractions)
1. `SOLTest — C++ Code Style Guide (DRAFT)` - 18 edges
2. `FSOLBodyAppearance` - 16 edges
3. `GetName()` - 11 edges
4. `1. Review checklist` - 11 edges
5. `CLAUDE.md project guidance` - 10 edges
6. `FSOLClockDateCase` - 9 edges
7. `Appendix A — Part 1a API contract` - 9 edges
8. `build_material()` - 8 edges
9. `SOLTest — Game Mechanics, Controls & Tunables` - 8 edges
10. `CreateAction()` - 7 edges

## Surprising Connections (you probably didn't know these)
- `Module and file layout (feature folders, Build.cs rules)` --references--> `SOLTest`  [EXTRACTED]
  Docs/STYLE_GUIDE.md → Source/SOLTest/SOLTest.Build.cs
- `Tech debt: untested SOLTypes.h and SOLKepler.h` --references--> `FSOLKeplerElements()`  [EXTRACTED]
  CLAUDE.md → Source/SOLTest/Universe/SOLKepler.h
- `Part 1a: universe types, Kepler solver, clock, registry (paused)` --references--> `FSOLKeplerElements()`  [EXTRACTED]
  Docs/Plans/1-solar-system-architecture-plan.md → Source/SOLTest/Universe/SOLKepler.h
- `FSOLSecularElements()` --implements--> `Analytic Keplerian orbits, no perturbations`  [INFERRED]
  Source/SOLTest/Universe/SOLKepler.h → Docs/SDDs/1-solar-system-architecture.md
- `CLAUDE.md project guidance` --references--> `unreal-mcp server (HTTP 127.0.0.1:33445)`  [EXTRACTED]
  CLAUDE.md → .mcp.json

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **SOLTest build configuration (targets plus module rules plus module impl)** — source_soltest_target_soltesttarget, source_soltesteditor_target_soltesteditortarget, soltest_soltest_build_soltest, soltest_soltest_primary_game_module [INFERRED 0.85]
- **Kepler orbit evaluation pipeline (secular elements to state vector)** — universe_solkepler_fsolsecularelements, universe_solkepler_fsolkeplerelements, universe_solkepler_solkepler_solveeccentricanomaly, universe_solkepler_solkepler_elementstostate, universe_soltypes_fsolstate [INFERRED 0.85]
- **Design governance document set (CLAUDE.md, SDD, plan, style guide, performance research)** — claude_project_guidance, sdds_1_solar_system_architecture, plans_1_solar_system_architecture_plan, docs_style_guide, research_ue_performance_guidelines [INFERRED 0.85]

## Communities (52 total, 16 thin omitted)

### Community 0 - "Style Guide & Perf Review"
Cohesion: 0.05
Nodes (47): grill-me before new features workflow rule, Adversarial opus performance and style review, CLAUDE.md project guidance, Combat mechanics (bolts, targets), Flight-assist vs Newtonian flight model, Jump map (J) mechanic, Origin anchor 25% hysteresis, Surface-lock (L) mechanic (+39 more)

### Community 1 - "Kepler Orbits & Sim Clock"
Cohesion: 0.12
Nodes (16): Tech debt: untested SOLTypes.h and SOLKepler.h, Sim clock and time-warp, Analytic Keplerian orbits, no perturbations, Time decision: real date start, time-warp on bodies only, namespace, Planetary perturbations / N-body integration, Items, Lifecycle (+8 more)

### Community 2 - "Project Rules & Architecture SDD"
Cohesion: 0.43
Nodes (7): FSOLRenderPlacement, FVector3d, BodyPlacement(), RenderCmToUniverseM(), Reset(), UniverseToRenderCm(), Update()

### Community 3 - "UE Module & Build Config"
Cohesion: 0.17
Nodes (10): TDD with tests authored by separate subagent, UE Automation Framework testing (SOLTest.<Topic>.<Case>), Module and file layout (feature folders, Build.cs rules), ModuleRules, SOLTest, SOLTest primary game module (FDefaultGameModuleImpl), SOLTestTarget, SOLTestEditorTarget (+2 more)

### Community 4 - "Scale, Rebasing & Jump Map"
Cohesion: 0.48
Nodes (6): FSOLRenderOrigin, FString, FVector3d, ExpectedRenderOffsetCm(), MakeRenderOriginAt(), RunTest()

### Community 7 - "Star Field Research"
Cohesion: 0.28
Nodes (8): AT-HYG star catalog (2.55M stars), CelestialVault engine plugin (beta, Earth-centric), Decision (SDD 1), Hybrid star renderer: bright sprites plus faint HDR cubemap, Reusable CelestialVault Milky Way texture and material functions, Star Field: CelestialVault plugin vs. AT-HYG — Research Notes, What CelestialVault is, Why not use it directly with AT-HYG

### Community 8 - "SOLTypes Namespace"
Cohesion: 0.11
Nodes (23): AActor, FVector, FSOLRenderPlacement, FSubsystemCollectionBase, FVector3d, int32, Type, UObject (+15 more)

### Community 18 - "Community 18"
Cohesion: 0.11
Nodes (22): FLinearColor, FName, TCHAR, Type, UMaterialInstanceDynamic, ApplyAppearance(), BeginPlay(), EndPlay() (+14 more)

### Community 19 - "Community 19"
Cohesion: 0.11
Nodes (25): EInputActionValueType, FInputActionValue, BeginPlay(), CreateAction(), CreateInputObjects(), EndPlay(), GetSpeedCapMps(), HandleLook() (+17 more)

### Community 20 - "Community 20"
Cohesion: 0.09
Nodes (30): FSOLBodyDef, FName, FSOLSecularElements, FVector3d, int32, TCHAR, TConstArrayView, FSubsystemCollectionBase (+22 more)

### Community 21 - "Community 21"
Cohesion: 0.09
Nodes (22): 10. Class layout, 11. Enums, 12. Templates, 13. Error handling & logging, 14.1 Reflection macros, 14.2 Module & file layout, 14.3 `Build.cs` / `Target.cs`, 14.4 Other UE rules (+14 more)

### Community 22 - "Community 22"
Cohesion: 0.11
Nodes (17): Architecture, Build & Run, Conventions, Game mechanics catalog & in-game info, Git, graphify, Known tech debt to revisit, Performance review — required after code changes (+9 more)

### Community 23 - "Community 23"
Cohesion: 0.16
Nodes (15): FSOLBodyRegistry, FAutomationTestBase, FString, FVector3d, TCHAR, FSOLRegistryExpectedBody, Eccentricity, GM (+7 more)

### Community 24 - "Community 24"
Cohesion: 0.13
Nodes (15): 1. Review checklist, 2. Profiling workflow, 3. Caveats and gaps, 4. Sources (pages actually read), A. Tick and update cost, B. Memory, allocation and GC, C. Containers and data layout, D. Mass / ECS (+7 more)

### Community 25 - "Community 25"
Cohesion: 0.12
Nodes (15): 1. Problem, 2. Decisions (user-resolved), 3. Design detail (Claude's choices within the decisions above), 4. Open questions, 5. Revision history, Amendment 1 — post-review changes (2026-09-28, 1a adversarial review), Appendix A — Part 1a API contract, Contract clarifications (settled after the test author's ambiguity report) (+7 more)

### Community 26 - "Community 26"
Cohesion: 0.14
Nodes (5): int32, TConstArrayView, GetWarpIndex(), JulianDateFromUtc(), WarpFactors()

### Community 27 - "Community 27"
Cohesion: 0.17
Nodes (11): FString, int32, FSOLClockDateCase, Day, ExpectedJd, Hour, Minute, Month (+3 more)

### Community 28 - "Community 28"
Cohesion: 0.15
Nodes (8): FDateTime, FSubsystemCollectionBase, Type, UObject, DoesSupportWorldType(), GetUtcDateTime(), Initialize(), ShouldCreateSubsystem()

### Community 29 - "Community 29"
Cohesion: 0.17
Nodes (7): AController, FRotator, EndPlay(), FinishRestartPlayer(), IsSOLGameWorld(), Type, UWorld

### Community 30 - "Community 30"
Cohesion: 0.53
Nodes (8): binary(), build_material(), connect(), lerp(), node(), scalar(), unary(), vector()

### Community 31 - "Community 31"
Cohesion: 0.22
Nodes (9): 1.1 Debug spectator (sub-part 1a, temporary until the ship lands in 1b), 1. Controls, 2. Flight, 3. Time, 4. Jump map (J), 5. Surface-lock (L), 6. Origin anchoring, 7. Combat (+1 more)

### Community 32 - "Community 32"
Cohesion: 0.36
Nodes (8): FSOLAnchorSelector, FAutomationTestBase, FString, FVector3d, TArray, MakeAnchoredToFirst(), MakeAnchorPair(), RunTest()

### Community 33 - "Community 33"
Cohesion: 0.21
Nodes (14): FSOLKeplerElements, FSOLSecularElements, FString, TCHAR, AnalyticPeriod(), FSOLKeplerPlanetEccentricity, E0, EDotPerCy (+6 more)

### Community 34 - "Community 34"
Cohesion: 0.43
Nodes (6): FSOLOrbitState, FSOLKeplerElements, AtCenturies(), ElementsToState(), SolveEccentricAnomaly(), WrapAngleRad()

### Community 35 - "Community 35"
Cohesion: 0.29
Nodes (6): 1a — Foundations, 1b — Ship flight, 1c — HUD, Foundations and Flight Scaffold — Implementation Plan, Global constraints, Steps

### Community 36 - "Community 36"
Cohesion: 0.67
Nodes (5): FString, DrawHUD(), FormatDistance(), FormatSpeed(), FormatWarp()

### Community 37 - "Community 37"
Cohesion: 0.40
Nodes (5): FVector3d, int32, TConstArrayView, GetAnchorIndex(), Update()

### Community 39 - "Community 39"
Cohesion: 0.50
Nodes (4): FSOLRenderPlacement, FVector3d, ComputePlacement(), EclipticToUnreal()

### Community 45 - "Community 45"
Cohesion: 0.67
Nodes (3): FString, ExpectedFarDepthCm(), RunTest()

## Knowledge Gaps
- **200 isolated node(s):** `enabledMcpjsonServers`, `unreal-mcp`, `UWorld`, `Type`, `AController` (+195 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **16 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `GetName()` connect `Community 20` to `SOLTypes Namespace`, `Community 18`, `Community 19`, `Community 28`?**
  _High betweenness centrality (0.043) - this node is a cross-community bridge._
- **Why does `SOLTest — C++ Code Style Guide (DRAFT)` connect `Community 21` to `Style Guide & Perf Review`?**
  _High betweenness centrality (0.023) - this node is a cross-community bridge._
- **Why does `CLAUDE.md project guidance` connect `Style Guide & Perf Review` to `Kepler Orbits & Sim Clock`, `Star Field Research`?**
  _High betweenness centrality (0.020) - this node is a cross-community bridge._
- **Are the 8 inferred relationships involving `GetName()` (e.g. with `BeginPlay()` and `SetupPlayerInputComponent()`) actually correct?**
  _`GetName()` has 8 INFERRED edges - model-reasoned connections that need verification._
- **What connects `enabledMcpjsonServers`, `unreal-mcp`, `UWorld` to the rest of the system?**
  _206 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Style Guide & Perf Review` be split into smaller, more focused modules?**
  _Cohesion score 0.05387205387205387 - nodes in this community are weakly interconnected._
- **Should `Kepler Orbits & Sim Clock` be split into smaller, more focused modules?**
  _Cohesion score 0.12105263157894737 - nodes in this community are weakly interconnected._