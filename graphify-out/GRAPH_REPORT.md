# Graph Report - SOLTest  (2026-09-28)

## Corpus Check
- 83 files · ~74,565 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 839 nodes · 1235 edges · 82 communities (57 shown, 25 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 51 edges (avg confidence: 0.83)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `bce54157`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- [[_COMMUNITY_Style Guide & Perf Review|Style Guide & Perf Review]]
- [[_COMMUNITY_Kepler Orbits & Sim Clock|Kepler Orbits & Sim Clock]]
- [[_COMMUNITY_Project Rules & Architecture SDD|Project Rules & Architecture SDD]]
- [[_COMMUNITY_UE Module & Build Config|UE Module & Build Config]]
- [[_COMMUNITY_Scale, Rebasing & Jump Map|Scale, Rebasing & Jump Map]]
- [[_COMMUNITY_Community 6|Community 6]]
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
- [[_COMMUNITY_Community 38|Community 38]]
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
- [[_COMMUNITY_Community 54|Community 54]]
- [[_COMMUNITY_Community 55|Community 55]]
- [[_COMMUNITY_Community 56|Community 56]]
- [[_COMMUNITY_Community 57|Community 57]]
- [[_COMMUNITY_Community 58|Community 58]]
- [[_COMMUNITY_Community 59|Community 59]]
- [[_COMMUNITY_Community 60|Community 60]]
- [[_COMMUNITY_Community 61|Community 61]]
- [[_COMMUNITY_Community 62|Community 62]]
- [[_COMMUNITY_Community 63|Community 63]]
- [[_COMMUNITY_Community 64|Community 64]]
- [[_COMMUNITY_Community 65|Community 65]]
- [[_COMMUNITY_Community 66|Community 66]]
- [[_COMMUNITY_Community 67|Community 67]]
- [[_COMMUNITY_Community 68|Community 68]]
- [[_COMMUNITY_Community 69|Community 69]]
- [[_COMMUNITY_Community 70|Community 70]]
- [[_COMMUNITY_Community 73|Community 73]]
- [[_COMMUNITY_Community 74|Community 74]]
- [[_COMMUNITY_Community 75|Community 75]]
- [[_COMMUNITY_Community 76|Community 76]]
- [[_COMMUNITY_Community 77|Community 77]]
- [[_COMMUNITY_Community 78|Community 78]]
- [[_COMMUNITY_Community 79|Community 79]]
- [[_COMMUNITY_Community 80|Community 80]]
- [[_COMMUNITY_Community 81|Community 81]]

## God Nodes (most connected - your core abstractions)
1. `FInputActionValue` - 20 edges
2. `GetName()` - 20 edges
3. `SOLTest — C++ Code Style Guide (DRAFT)` - 18 edges
4. `FSOLBodyAppearance` - 17 edges
5. `RunTest()` - 15 edges
6. `Update()` - 12 edges
7. `HasPlayerShip()` - 12 edges
8. `BeginPhase()` - 11 edges
9. `1. Review checklist` - 11 edges
10. `FVector3d` - 10 edges

## Surprising Connections (you probably didn't know these)
- `Module and file layout (feature folders, Build.cs rules)` --references--> `SOLTest`  [EXTRACTED]
  Docs/STYLE_GUIDE.md → Source/SOLTest/SOLTest.Build.cs
- `Tech debt: untested SOLTypes.h and SOLKepler.h` --references--> `FSOLKeplerElements()`  [EXTRACTED]
  CLAUDE.md → Source/SOLTest/Universe/SOLKepler.h
- `FSOLSecularElements()` --implements--> `Analytic Keplerian orbits, no perturbations`  [INFERRED]
  Source/SOLTest/Universe/SOLKepler.h → Docs/SDDs/1-solar-system-architecture.md
- `CLAUDE.md project guidance` --references--> `unreal-mcp server (HTTP 127.0.0.1:33445)`  [EXTRACTED]
  CLAUDE.md → .mcp.json
- `SOLKepler::ElementsToState` --implements--> `Analytic Keplerian orbits, no perturbations`  [INFERRED]
  Source/SOLTest/Universe/SOLKepler.h → Docs/SDDs/1-solar-system-architecture.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **SOLTest build configuration (targets plus module rules plus module impl)** — source_soltest_target_soltesttarget, source_soltesteditor_target_soltesteditortarget, soltest_soltest_build_soltest, soltest_soltest_primary_game_module [INFERRED 0.85]
- **Kepler orbit evaluation pipeline (secular elements to state vector)** — universe_solkepler_fsolsecularelements, universe_solkepler_fsolkeplerelements, universe_solkepler_solkepler_solveeccentricanomaly, universe_solkepler_solkepler_elementstostate, universe_soltypes_fsolstate [INFERRED 0.85]
- **Design governance document set (CLAUDE.md, SDD, plan, style guide, performance research)** — claude_project_guidance, sdds_1_solar_system_architecture, plans_1_solar_system_architecture_plan, docs_style_guide, research_ue_performance_guidelines [INFERRED 0.85]

## Communities (82 total, 25 thin omitted)

### Community 0 - "Style Guide & Perf Review"
Cohesion: 0.19
Nodes (12): Adversarial opus performance and style review, File banner, function comment and separator rules, Asset paths live in one place (SOLConstants.h / data assets), Data-oriented Mass guidance (fragments, processors, no hot-loop allocation), Naming conventions (SOL infix, m prefix, UPPER_SNAKE constants), TrueReality house style origin, UE reflection macro and UPROPERTY rules, Mass ECS performance rules (+4 more)

### Community 1 - "Kepler Orbits & Sim Clock"
Cohesion: 0.17
Nodes (13): Tech debt: untested SOLTypes.h and SOLKepler.h, Sim clock and time-warp, Part 1a: universe types, Kepler solver, clock, registry (paused), Analytic Keplerian orbits, no perturbations, Time decision: real date start, time-warp on bodies only, namespace, Planetary perturbations / N-body integration, FSOLKeplerElements() (+5 more)

### Community 2 - "Project Rules & Architecture SDD"
Cohesion: 0.43
Nodes (7): FSOLRenderPlacement, FVector3d, BodyPlacement(), RenderCmToUniverseM(), Reset(), UniverseToRenderCm(), Update()

### Community 3 - "UE Module & Build Config"
Cohesion: 0.17
Nodes (10): TDD with tests authored by separate subagent, UE Automation Framework testing (SOLTest.<Topic>.<Case>), Module and file layout (feature folders, Build.cs rules), ModuleRules, SOLTest, SOLTest primary game module (FDefaultGameModuleImpl), SOLTestTarget, SOLTestEditorTarget (+2 more)

### Community 4 - "Scale, Rebasing & Jump Map"
Cohesion: 0.48
Nodes (6): FSOLRenderOrigin, FString, FVector3d, ExpectedRenderOffsetCm(), MakeRenderOriginAt(), RunTest()

### Community 6 - "Community 6"
Cohesion: 0.19
Nodes (20): BodyPositionAtSubstep(), FlightClampLength(), FlightClampUnitComponents(), FlightQuatFromRotationVector(), FlightRemainingFraction(), FrameCarryDisplacement(), GravityAcceleration(), MakeCircularOrbitState() (+12 more)

### Community 7 - "Star Field Research"
Cohesion: 0.28
Nodes (8): AT-HYG star catalog (2.55M stars), CelestialVault engine plugin (beta, Earth-centric), Decision (SDD 1), Hybrid star renderer: bright sprites plus faint HDR cubemap, Reusable CelestialVault Milky Way texture and material functions, Star Field: CelestialVault plugin vs. AT-HYG — Research Notes, What CelestialVault is, Why not use it directly with AT-HYG

### Community 8 - "SOLTypes Namespace"
Cohesion: 0.11
Nodes (25): AActor, FVector, BeginPlay(), FSOLRenderPlacement, FSubsystemCollectionBase, FVector, FVector3d, int32 (+17 more)

### Community 18 - "Community 18"
Cohesion: 0.10
Nodes (24): FLinearColor, FLinearColor, FName, TCHAR, Type, UMaterialInstanceDynamic, UMaterialInstanceDynamic, ApplyAppearance() (+16 more)

### Community 19 - "Community 19"
Cohesion: 0.08
Nodes (36): EInputActionValueType, FInputActionValue, AddMappingContext(), CreateAction(), MapPressedKey(), RemoveMappingContext(), CreateAction(), CreateInputObjects() (+28 more)

### Community 20 - "Community 20"
Cohesion: 0.09
Nodes (32): FSOLBodyDef, Initialize(), FSubsystemCollectionBase, FName, FSOLSecularElements, FVector3d, int32, TCHAR (+24 more)

### Community 21 - "Community 21"
Cohesion: 0.09
Nodes (22): 10. Class layout, 11. Enums, 12. Templates, 13. Error handling & logging, 14.1 Reflection macros, 14.2 Module & file layout, 14.3 `Build.cs` / `Target.cs`, 14.4 Other UE rules (+14 more)

### Community 22 - "Community 22"
Cohesion: 0.11
Nodes (17): Architecture, Build & Run, Conventions, Game mechanics catalog & in-game info, Git, graphify, Known tech debt to revisit, Performance review — required after code changes (+9 more)

### Community 23 - "Community 23"
Cohesion: 0.15
Nodes (16): FSOLBodyRegistry, FAutomationTestBase, FSOLBodyRegistry, FString, FVector3d, TCHAR, FSOLRegistryExpectedBody, Eccentricity (+8 more)

### Community 24 - "Community 24"
Cohesion: 0.13
Nodes (15): 1. Review checklist, 2. Profiling workflow, 3. Caveats and gaps, 4. Sources (pages actually read), A. Tick and update cost, B. Memory, allocation and GC, C. Containers and data layout, D. Mass / ECS (+7 more)

### Community 25 - "Community 25"
Cohesion: 0.09
Nodes (22): 1. Problem, 2. Decisions (user-resolved), 3. Design detail (Claude's choices within the decisions above), 4. Open questions, 5. Revision history, Amendment 1 — post-review changes (2026-09-28, 1a adversarial review), Amendment 2 clarifications (settled after the test author's ambiguity report), Appendix A — Part 1a API contract (+14 more)

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
Cohesion: 0.13
Nodes (11): AController, FRotator, EndPlay(), FinishRestartPlayer(), GetDefaultPawnClassForController_Implementation(), IsSOLGameWorld(), FRotator, Type (+3 more)

### Community 30 - "Community 30"
Cohesion: 0.53
Nodes (8): binary(), build_material(), connect(), lerp(), node(), scalar(), unary(), vector()

### Community 31 - "Community 31"
Cohesion: 0.20
Nodes (10): 1.1 Debug and verification switches, 1.1 Debug spectator (sub-part 1a, temporary until the ship lands in 1b), 1. Controls, 2. Flight, 3. Time, 4. Jump map (J), 5. Surface-lock (L), 6. Origin anchoring (+2 more)

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
Cohesion: 0.52
Nodes (5): FString, DrawHUD(), FormatDistance(), FormatSpeed(), FormatWarp()

### Community 37 - "Community 37"
Cohesion: 0.40
Nodes (5): FVector3d, int32, TConstArrayView, GetAnchorIndex(), Update()

### Community 38 - "Community 38"
Cohesion: 0.21
Nodes (22): EFlightStepAxis, FQuat4d, FQuat4d, FSOLFlightParams, FSOLShipState, FString, FVector3d, FFlightStepAxes (+14 more)

### Community 39 - "Community 39"
Cohesion: 0.50
Nodes (4): FSOLRenderPlacement, FVector3d, ComputePlacement(), EclipticToUnreal()

### Community 40 - "Community 40"
Cohesion: 0.40
Nodes (3): class, class, FSOLBodyRegistry()

### Community 45 - "Community 45"
Cohesion: 0.67
Nodes (3): FString, ExpectedFarDepthCm(), RunTest()

### Community 54 - "Community 54"
Cohesion: 0.22
Nodes (10): grill-me before new features workflow rule, Combat mechanics (bolts, targets), Surface-lock (L) mechanic, Roadmap Parts 2-10 (jump map, level, star field, moons, weapons, HUD, art, terrain, scale demo), Niagara and rendering best practices, Data-driven projectile combat decision, Level / surface-lock decision, Ship pulled by body gravity; bodies on rails (+2 more)

### Community 55 - "Community 55"
Cohesion: 0.42
Nodes (8): FSOLTargetInfo, FString, FVector3d, TCHAR, RunTest(), TargetingDeg(), TargetingMakeAtDistance(), TargetingMakeOffAxis()

### Community 56 - "Community 56"
Cohesion: 0.17
Nodes (9): CLAUDE.md project guidance, unreal-mcp server (HTTP 127.0.0.1:33445), SOLTest, Naming, Software Design Documents (SDDs), Items, Lifecycle, Naming (+1 more)

### Community 57 - "Community 57"
Cohesion: 0.25
Nodes (7): Flight-assist vs Newtonian flight model, Cross-cutting tooling, Global constraints, Part 1b: Mass ship entity, Pawn proxy, flight model, Roadmap, Solar-System Architecture — Roadmap Plan, Flight model decision (assist default, Newtonian toggle)

### Community 58 - "Community 58"
Cohesion: 0.05
Nodes (76): ESOLShipPartLook, ESOLShipPartShape, AddShipPart(), BeginPlay(), BuildShipMesh(), CreateHullMaterial(), CreateInputObjects(), EndPlay() (+68 more)

### Community 59 - "Community 59"
Cohesion: 0.44
Nodes (8): Cycle(), PickUnderReticle(), ResolveReferenceVelocity(), TargetingCycleLess(), FSOLTargetInfo, FVector3d, int32, TConstArrayView

### Community 60 - "Community 60"
Cohesion: 0.40
Nodes (5): 1. Problem, 2. Decisions, 3. Open questions, 4. Revision history, SDD 1 — Solar-system space sim: architecture and cross-cutting decisions

### Community 63 - "Community 63"
Cohesion: 0.13
Nodes (28): FSOLShipFrameInputs, ComputeFrameInputs(), DoesSupportWorldType(), FindNearestBody(), GetActiveReferenceBody(), GetContactBodyIndex(), GetControl(), GetFlightParams() (+20 more)

### Community 64 - "Community 64"
Cohesion: 0.35
Nodes (17): ESOLSmokePhase, FSOLSmokeSample, BeginPhase(), EndPhase(), GetPhaseDuration(), GetPhaseName(), LogSample(), Sample() (+9 more)

### Community 65 - "Community 65"
Cohesion: 0.12
Nodes (24): ISOLTargetable, FName, FSOLTargetInfo, FSubsystemCollectionBase, FVector3d, int32, Type, UObject (+16 more)

### Community 66 - "Community 66"
Cohesion: 0.25
Nodes (10): FMassEntityManager, FMassExecutionContext, FSOLBodyFrameCache, ConfigureQueries(), Execute(), PrepareSubstepBodyPositions(), ProcessChunk(), SetBodyCache() (+2 more)

### Community 67 - "Community 67"
Cohesion: 0.83
Nodes (3): FSOLBodyRegistry, Init(), Refresh()

### Community 73 - "Community 73"
Cohesion: 0.22
Nodes (23): ASOLShipPawn, ESOLInputPhase, Check(), EnterPhase(), ExitPhase(), FindBody(), GetCandidateName(), GetPhaseDuration() (+15 more)

### Community 79 - "Community 79"
Cohesion: 0.24
Nodes (14): JoystickToRotation(), FVector2d, FVector2d, FAutomationTestBase, FSOLShipState, FString, FVector3d, TCHAR (+6 more)

### Community 80 - "Community 80"
Cohesion: 0.42
Nodes (11): FAutomationTestBase, FSOLShipState, FString, FVector3d, FlightSweptCheckEntry(), FlightSweptCheckMatchesNonSwept(), FlightSweptEntryPoint(), FlightSweptExpectedVelocity() (+3 more)

### Community 81 - "Community 81"
Cohesion: 0.32
Nodes (7): Jump map (J) mechanic, Origin anchor 25% hysteresis, Double precision and camera-relative rendering (LWC) rules, Jump map decision (2-4 s warp, velocity-matched arrival), Origin rebasing with anchor hysteresis, True 1:1 scale with double-precision universe coordinates, Supercruise-style long travel

## Knowledge Gaps
- **263 isolated node(s):** `enabledMcpjsonServers`, `unreal-mcp`, `FSOLShipControl`, `TConstArrayView`, `int32` (+258 more)
  These have ≤1 connection - possible missing edges or undocumented components.
- **25 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `GetName()` connect `Community 20` to `Community 65`, `SOLTypes Namespace`, `Community 18`, `Community 19`, `Community 58`, `Community 28`, `Community 63`?**
  _High betweenness centrality (0.084) - this node is a cross-community bridge._
- **Why does `BeginPlay()` connect `Community 18` to `Community 20`?**
  _High betweenness centrality (0.016) - this node is a cross-community bridge._
- **Why does `SpawnPlayerShip()` connect `Community 63` to `Community 20`?**
  _High betweenness centrality (0.013) - this node is a cross-community bridge._
- **Are the 17 inferred relationships involving `GetName()` (e.g. with `BeginPlay()` and `SetupPlayerInputComponent()`) actually correct?**
  _`GetName()` has 17 INFERRED edges - model-reasoned connections that need verification._
- **What connects `enabledMcpjsonServers`, `unreal-mcp`, `FSOLShipControl` to the rest of the system?**
  _269 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `SOLTypes Namespace` be split into smaller, more focused modules?**
  _Cohesion score 0.10541310541310542 - nodes in this community are weakly interconnected._
- **Should `Community 18` be split into smaller, more focused modules?**
  _Cohesion score 0.10461538461538461 - nodes in this community are weakly interconnected._