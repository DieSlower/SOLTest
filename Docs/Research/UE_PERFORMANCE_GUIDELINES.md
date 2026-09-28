<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Unreal Engine 5 C++ / Game-Code Performance Guidelines (Epic + Tom Looman)

Status: research only, compiled 2026-09-28. Guidance was gathered from Epic's UE 5.7/5.8 documentation pages and Tom Looman's public articles; re-check every rule against the UE 5.8 docs and source before enforcing it. CVar names and defaults change between releases.

Source labels: **[Epic]** = Epic documentation, **[Looman]** = tomlooman.com (public pages only; his paid course lessons are locked and were not read). Each rule points to a numbered source in the Sources list. Rules marked *(inference)* are a project-specific application of a sourced rule, not a quote.

Project lens: ~1,000,000 MassEntity ships and bodies, double-precision coordinates with origin rebasing, Niagara/GPU VFX, data-driven projectiles (no Actor per bolt), 1:1 planetary rendering.

---

## 1. Review checklist

### A. Tick and update cost

1. Set `PrimaryActorTick.bCanEverTick = false` in the constructor of every Actor/Component that does not need a per-frame update. Rationale: every ticking Actor costs a call and cache miss per frame even when idle; ticking should be opt-in. [Epic, S1 via search summary of the Actor Ticking page; S2]
2. Use `SetActorTickEnabled` / `SetComponentTickEnabled` to switch ticking on and off at runtime, instead of early-returning inside `Tick()`. Rationale: a disabled tick is not scheduled at all. [Epic, S2]
3. Give periodic work a `TickInterval` (or a timer) instead of every-frame ticking. Rationale: work that does not need frame accuracy should not run at frame rate. [Epic, S2; Looman, S3]
4. Choose the narrowest `TickGroup` that satisfies real data dependencies (for example `TG_DuringPhysics` for logic that does not read physics results, `TG_PostPhysics` only for things like traces that need this frame's physics). Rationale: wrong groups serialize work behind physics. [Epic, S2]
5. Prefer `AddTickPrerequisiteActor/Component` for one-off ordering rather than moving a whole group of Actors into a later tick group. Rationale: avoids forcing unrelated objects to wait. [Epic, S2]
6. Replace polling in `Tick()` with delegates/callbacks for value-change reactions (health, state). Rationale: events cost nothing on frames where nothing changed. [Looman, S4]
7. Audit ticking objects with `dumpticks` / `dumpticks grouped` and remove or disable anything that should not tick. Rationale: cheapest game-thread win. [Looman, S3]
8. Consider batched ticks (`tick.AllowBatchedTicks 1`, `ForEachNestedTick` on a tick function) for large groups of similar tickers. Rationale: reduces TaskGraph overhead on the game thread. Verify status in 5.8. [Looman, S5]
9. Do not run `Tick` on a per-bolt / per-ship Actor for the 1M-entity path; use Mass processors or a data-oriented subsystem with one tick. Rationale: Looman's sample handles thousands of projectiles as structs because a "full fat Actor design" does not scale. [Looman, S6]
10. For work that does not need frame-perfect execution (spawning, low-priority world updates), spread it over frames with a budgeted deferred-task scheduler. Rationale: keeps per-frame cost flat and avoids hitches. [Looman, S6]
11. Throttle non-critical gameplay logic by distance/importance using the Significance Manager (or Mass LOD, see section D). Rationale: the framework itself does not speed anything up; objects must reduce their own cost when significance drops. [Epic, S7; Looman, S3, S6]
12. Significance and post-significance functions must be thread-safe when the concurrent post-function type is used, and `Update` must be called manually about once per frame. Rationale: the manager evaluates objects in parallel. [Epic, S7]

### B. Memory, allocation and GC

13. Every `UObject*` member intended to keep an object alive must be `UPROPERTY()`. Rationale: only reflected properties are scanned by GC, and they get auto-nulled on destroy. [Looman, S4]
14. Declare UObject members in headers as `TObjectPtr<T>`. Rationale: required for incremental reachability write barriers and virtualized assets; identical cost in shipping. [Epic, S8; Looman, S4]
15. If incremental GC is used, set `gc.AllowIncrementalReachability 1`, `gc.AllowIncrementalGather 1`, `gc.IncrementalReachabilityTimeLimit 0.002`. Rationale: spreads reachability across frames to remove GC hitches. Epic recommends it only for single-threaded object access, because an object held only on a worker thread is not marked reachable during a scan. [Epic, S8]
16. Do not touch or hold raw UObject pointers on worker threads. If needed, use `TStrongObjectPtr` (thread-safe ref counting, 5.5+) or the `TWeakObjectPtr` pin API. Rationale: GC safety. [Looman, S5]
17. Avoid creating and destroying UObjects/Actors at runtime in hot paths; pool them (fixed number spawned during load, `AcquireFromPool` / `ReleaseToPool`). Rationale: spawn is expensive, pooling reduces fragmentation, allocations, destroy cost and GC load. [Looman, S3, S9]
18. Do not make each projectile, ship or body a UObject at all; keep them as plain structs / Mass fragments. Rationale: object count drives GC cost, and UE 5.8 adds a "UObject Count" Insights counter to watch it. *(inference)* [Looman, S6, S10]
19. Use `TWeakObjPtr` for non-owning references so nothing is kept alive artificially. [Looman, S4]
20. Avoid hard-reference chains (Blueprint casts to specific Blueprint classes pull all referenced assets into memory). Use soft references and async loading; check with the Size Map tool early. [Looman, S3, S4]
21. Reserve TArray capacity before bulk adds (`Reserve`/`Empty(Slack)`), use `Reset()` to reuse capacity, and `Shrink()` after mass removal. Rationale: avoids repeated reallocation. [Epic, S11]
22. Prefer `Emplace` over `Add` for non-trivial types; use `RemoveAtSwap`/`RemoveSwap` when order does not matter. Rationale: no temporaries; no element shuffling. [Epic, S11]
23. Use `AddUninitialized`/`InsertUninitialized` only for trivially copyable bulk data you will fully overwrite (Memcpy). Rationale: skips constructors; unsafe otherwise. [Epic, S11]
24. Never mark a return type `const`; do not use `const` on by-value params you intend to move. Use `MoveTemp` for containers and `FString`, which all have move operations. Rationale: const return inhibits move semantics. [Epic, S12]
25. Lambdas: use explicit captures, not `[&]`/`[=]`; never capture references or raw pointers for deferred/async execution. Rationale: dangling data, crashes. [Epic, S12]
26. Wrap string literals in `TEXT()`. Rationale: avoids conversion when constructing `FString`. [Epic, S12]
27. In loops, hoist common subexpressions out and do not repeat the same operation. [Epic, S12]
28. Use `FORCEINLINE` and `inline` sparingly (trivial accessors, or proven by profiling). Rationale: code bloat in callers. [Epic, S12]
29. In packaged builds use `memreport -full` and `obj list class=<Class>` to find unintended loads and expensive object populations. [Looman, S3]
30. Note UE 5.8 defaults: MallocBinned3 on Windows, max small bin 14KB. Do not assume older allocator behavior when benchmarking. [Looman, S13]

### C. Containers and data layout

31. Store hot simulation data as struct-of-arrays / Mass fragments, iterate linearly, and keep fragments small and single-purpose. Rationale: chunks store entities in SoA-like layout, sized around cache lines (Mass chunk sizing is based on 128-byte lines x 1024 lines per MassSample docs). [Epic, S14; Looman, S15 (MassSample repo is community, not Epic; see caveat)]
32. Use `TArray` ranged-for iteration for the default case. [Epic, S11]
33. Use heap functions (`HeapPush`/`HeapPop`) for priority queues; use `StableSort` when relative order of equivalent elements matters (`Sort` and `HeapSort` do not preserve it). [Epic, S11]
34. For coordinates, use `FVector` (double) by default in gameplay code, and `FVector3f` only where profiling/memory justifies single precision. Never assign a double component to a `float` local (precision loss at ~6 significant digits); use `FVector::FReal` or `double`. [Epic, S16, S17]
35. Keep GPU-side positions camera-relative/translated (float) and convert from double as early as possible; avoid LWC/DoubleFloat math in shaders and materials because it is expensive. Set material World Position Origin Type to Camera Relative. [Epic, S18]
36. Niagara particle positions use float within grid cells rather than raw doubles for performance. Do not feed raw double universe coordinates into per-particle data. [Epic, S16]
37. Audit `UFUNCTION`s exposing `float` parameters for precision loss under LWC. [Epic, S16]
38. `TArray` with `FNonshrinkingAllocator` (5.7) avoids automatic shrink reallocations for arrays that oscillate in size. [Looman, S19]

### D. Mass / ECS

39. Iterate with chunk-based APIs (`ForEachEntityChunk`, `ParallelForEachEntityChunk`), not per-entity lambdas, on hot queries. Rationale: fragment views are fetched once per chunk instead of once per entity; per-entity variants "can introduce a large overhead". [Epic-adjacent community article, S20; Epic, S14]
40. Processors must be stateless; declare exact fragment access (ReadOnly vs ReadWrite) and query requirements so the dependency solver can parallelize. Rationale: enables multi-core execution and correct ordering. [Epic, S14; Looman, S19]
41. Never change entity composition (add/remove fragments or tags, create/destroy) inline during iteration; use `FMassCommandBuffer` deferred commands. Rationale: archetype moves are deferred to the end of the batch. [Epic, S14, S15]
42. Minimize archetype churn: do not toggle tags/fragments on many entities per frame. Prefer tags for cheap filtering (bitset filtering, no data access) but avoid flipping them constantly. UE 5.8 adds sparse/virtual fragments to avoid costly archetype changes; verify before relying on them. [Epic, S14; Looman, S13, S15]
43. Batch-create entities (`BatchCreateEntities`, `BatchReserveEntities`, spawner with batches) instead of creating one entity per call. [Epic-adjacent community article, S20]
44. Mark a processor `bRequiresGameThreadExecution = true` only when it truly touches UObjects/game-thread-only state. Rationale: it prevents worker-thread execution. [Epic-adjacent, S15]
45. Enable parallelism deliberately: `mass.FullyParallel 1` for per-processor threading and `mass.AllowQueryParallelFor` plus `ParallelForEachEntityChunk` for per-query parallelism. Each parallel job gets its own command buffer. [Epic-adjacent, S15]
46. Use Mass LOD (High/Medium/Low/Off with per-level entity caps) to give simulation frequency variable rates by distance (Simulation LOD) and to cull visibility (Representation LOD). Rationale: load-balances entity calculation. [Epic, S21]
47. Represent far/many entities with ISM (or vertex animation), and only spawn Actors for the nearest few; enable representation Actor pooling. Rationale: Epic calls ISM "the cheapest way of representing the Actor". [Epic, S21]
48. For long-running processors, use time-slicing (`FMassEntityQuery::FExecutionLimiter`, UE 5.7) so work spans frames. [Looman, S19]
49. Batch observer calls (UE 5.7) rather than per-entity observers; observers that fire per entity per frame are a hitch source. [Looman, S19]
50. Use `EntityView` (safe random access) only for occasional cross-entity lookups, not in hot per-entity loops. Rationale: it is for entities outside the current batch. [Epic, S14]
51. Set processor `ExecutionOrder.ExecuteInGroup` and processing phase explicitly. Rationale: avoids unnecessary phase overhead and wrong ordering. [Epic-adjacent, S20]
52. Put shared per-chunk managerial data (for example LOD) in ChunkFragments, not duplicated per entity. [Epic, S14]
53. Mass has been reworked in 5.8 (lock-free off-game-thread entity creation, Mass Signals in core, better dependency resolution); read the 5.8 release notes before designing around 5.5-era assumptions. [Looman, S13]

### E. Threading and async

54. Prefer `UE::Tasks` (Launch/prerequisites/nested tasks/`FPipe`) for new async code; express ordering as prerequisites, not blocking waits. Rationale: "Waiting should be avoided if possible, as it limits scalability". [Epic, S22]
55. Never wait inside a task body or create circular waits; do not run long tasks inside an `FPipe`. [Epic, S22]
56. Use `ParallelFor` for large uniform loops over plain data; the calling thread participates and it blocks until done. Keep task bodies free of UObject access. Rationale: UObjects belong to the game thread. [Epic-adjacent, general knowledge from S22 context; UObject rule from Looman, S5]
57. Use async physics queries (for example `AsyncSweepByChannel`) for non-blocking traces when a one-frame delay is acceptable. [Looman, S6]
58. Marshal worker results back to the game thread instead of mutating game state from tasks. [Looman, S5]
59. On 5.7+ consider `Async.ParallelFor.DisableOversubscription` when cores are saturated; note the Windows thread priority change. [Looman, S19]
60. `TaskSyncManager` (experimental 5.6/5.7) can batch per-frame updates from worker threads. Treat as experimental. [Looman, S19]

### F. Rendering (Nanite, Niagara, draw calls, LOD, materials)

61. Nanite: opaque and masked only, no morph targets, rigid transforms only; use for high-density static geometry, not for large on-screen triangles (sky spheres), single unoccluded instances, forward/VR. Hard cap 16 million instances. [Epic, S23]
62. Nanite WPO splits meshes into small individually culled clusters; clamp WPO displacement. [Epic, S23]
63. Niagara: assign an Effect Type to every system so scalability, culling and instance limits apply. [Epic, S24]
64. Niagara: pool systems ("System as a Service") for rapid effects (projectiles, impacts), and use pooling via Spawn System at Location/Attached. Prime pools carefully. [Epic, S24]
65. Niagara: reduce emitter count (fixed VM overhead per emitter); use multiple renderers, spawn index and mesh arrays instead of extra emitters. [Epic, S24]
66. Niagara: choose GPU sim for large counts, CPU sim for very small counts (about 1-64 particles); verify by measuring. [Epic, S24]
67. Niagara: prefer fixed bounds; use dynamic bounds only for far-travelling small effects (bullet traces). [Epic, S24]
68. Niagara: leave sorting off by default, avoid warmup, spread big bursts over frames, disable "Require Current Frame Data" when unneeded, prefer attribute-reader data interface over particle events. [Epic, S24]
69. Niagara: consider lightweight (stateless) emitters for simple VFX and Data Channels for impact/spark effects instead of a component per instance. Both were Beta/Production as of 5.5 per Looman; confirm 5.8 status. [Looman, S5]
70. Meshes used by Niagara: strip collision, extra UVs, unneeded LOD shadows; set Distance Field Resolution Scale 0. [Epic, S24]
71. Do not create per-component User Parameter data interfaces (each creates a UObject per component). [Epic, S24]
72. Cull by distance: set `MaxDrawDistance` on translucent and small props, use Distance Cull Volumes, verify with `r.visualizeoccludedprimitives 1`. Rationale: cuts occlusion-query and overdraw cost. [Looman, S3]
73. Move Anim/Audio/Niagara components with `AutoManageAttachment` and `UseAttachParentBound`; keep `GenerateOverlaps` off on moving components; call `SetActorLocation` and `SetActorRotation` once per frame each (not repeatedly). [Looman, S3]
74. Limit overlapping stationary lights (over 4 forced to movable); set per-light `MaxDrawDistance`. [Looman, S3]
75. For Virtual Shadow Maps with Nanite, control shadow LOD with `r.Shadow.NaniteLODBias` rather than `r.ForceLODShadow`. [Looman, S3]
76. Use `ToggleForceDefaultMaterial`, shader complexity and quad overdraw views to isolate material cost. [Looman, S3; Epic, S25]
77. `stat initviews` visible section count is "the single most important stat with respect to rendering thread performance"; reduce draw calls by merging or instancing. [Epic, S26]

### G. Physics and collision

78. Default collision to `QueryOnly` unless simulation is needed; disable collision on unreachable components. [Looman, S3]
79. Profile with `stat physics` and `stat collision`; run collision queries asynchronously where possible (see rule 57). [Looman, S3, S6]
80. Chaos uses double precision natively; be explicit when narrowing to float. [Epic, S16]

### H. Animation

81. Move AnimGraph logic to the fast path; use Update Rate Optimization for distant meshes; use `VisibilityBasedAnimTickOption = OnlyTickPoseWhenRendered`. [Looman, S3]
82. Use the Animation Budget Allocator (`SkeletalMeshComponentBudgeted`, `a.Budget.BudgetMs` default 1.0 ms, `a.Budget.MaxTickRate` default 10) for many animated characters; set significance via `SetComponentSignificance`. [Epic, S27; Looman, S6]
83. Consider the ACL plugin for animation compression (Looman cites about 50% memory reduction). [Looman, S3]
84. For mass crowds, prefer vertex animation on ISM representation over skeletal meshes. [Epic, S21]

### I. Streaming and loading

85. Design level streaming in from the start; use `bShouldBeVisible = false` to hide but keep loaded; profile with `stat levels` and `Loadtimes.dumpreport`. [Looman, S3]
86. Use async loading APIs (`LoadAssetAsync`, `FSoftObjectPath::LoadAsync`) and never block on synchronous loads in gameplay. [Looman, S5]
87. Streaming hitches can be studied with the `WorldStreaming` trace channel and the Spatial Profiler (UE 5.8 experimental). [Looman, S13]
88. GC during streaming: 5.8 adds actor-pending-purge triggers (`s.ContinuouslyIncrementalGCWhileActorsPendingPurge`); tune to avoid hitches on unload. [Looman, S13]

### J. Profiling workflow rules

89. Profile in packaged Development/Test builds, not in-editor; disable vsync and frame smoothing (`r.vsync 0`, `t.maxfps 0`, `SmoothFrameRate = false`) first. [Looman, S3; Epic, S26]
90. Identify the bound thread (Game / Render / GPU) with `stat unit` before optimizing anything. [Epic, S26; Looman, S3]
91. Wrap hot code in stat scopes (`SCOPE_CYCLE_COUNTER`) and counters, but conservatively (only actionable metrics); named events add noticeable overhead. [Looman, S28]
92. Every new system in this project should expose at least one trace counter (entity count, projectile count, time per phase). *(inference)* [Looman, S28]

---

## 2. Profiling workflow

Order of operations (Looman, S3; Epic, S26):

1. Prepare: packaged Development build, lighting built, `r.vsync 0`, `t.maxfps 0`, `SmoothFrameRate` off, editor viewport realtime off if in editor.
2. Locate the bottleneck: `stat unit` (Frame / Game / Draw / GPU / RHIT / DynRes), `stat unitgraph` for a history graph, `pause` to freeze the game thread, `r.screenpercentage 20` to test GPU-boundness.
3. Drill down by area:

| Command | Use |
|---|---|
| `stat game` | Game-thread tick cost by category |
| `stat scenerendering`, `stat engine`, `stat gpu` | Rendering and GPU overview |
| `stat initviews` | Culling and visible sections (render-thread driver) |
| `stat physics`, `stat collision` | Physics and collision costs |
| `stat anim`, `stat component` | Skeletal animation, component movement |
| `stat significancemanager`, `stat uobjects`, `stat memory`, `stat levels` | Significance overhead, UObject counts, memory, streaming |
| `dumpticks`, `dumpticks grouped` | List every ticking Actor/Component |
| `listtimers`, `stat dumphitches` | Timer costs, expensive functions during hitches |
| `memreport -full`, `obj list class=X` | Packaged-build memory audits |
| `ProfileGPU` (`r.ProfileGPU.*`), `ToggleForceDefaultMaterial` | GPU pass breakdown, material isolation |
| `showflag.bounds 1`, `showflag.distanceculledprimitives 1`, `r.visualizeoccludedprimitives 1` | Culling visualizations |
| `ShowDebug SignificanceManager`, `a.Budget.Debug.Enabled 1` | Significance and animation budget overlays |
| `stat none` | Clear stats overlay |
| `stat startfile` / `stat stopfile` | Capture stats for Session Frontend Profiler |
| `snapshothitches -start/-stop` (5.8) | Capture traces and screenshots of hitches; needs an active stat group |

4. Capture with Unreal Insights: launch with `-trace=cpu,gpu,frame,loadtime,file,memory` (Looman list) or `-trace=default,memory` for memory (Epic). Defaults already include Gpu, Bookmark, Frame, Cpu, Log. Runtime control: `Trace.Status`, `Trace.Stop`, `Trace.SnapshotFile <name>`, `Trace.Bookmark <name>`, `Trace.Screenshot`, `Trace.RegionBegin/End` (5.5+). Use `-trace=memory_light` to avoid callstack overhead (5.5+). Memory tracing must start with the process (no late connect). Named events (`-statnamedevents`, `stat namedevents`) show asset names in traces but add overhead (Looman says roughly 20%).
5. Analyze in Timing Insights (CPU/GPU per frame), Memory Insights (allocations, leaks, short-lived allocations), and check the 5.8 "UObject Count" counter (`counters` channel) for GC impact.
6. Add your own instrumentation:
   - Insights counters: `TRACE_DECLARE_INT_COUNTER`, `TRACE_DECLARE_FLOAT_COUNTER`, `TRACE_COUNTER_SET/ADD/SUBTRACT` (needs `-trace=counters`).
   - Stats system: `DECLARE_STATS_GROUP`, `DECLARE_CYCLE_STAT`, `SCOPE_CYCLE_COUNTER`, `DECLARE_DWORD_ACCUMULATOR_STAT`, `INC_DWORD_STAT`.
   - Named events: `SCOPED_NAMED_EVENT`, `TRACE_BOOKMARK`.
   - Custom events: `UE_TRACE_EVENT_BEGIN/FIELD/END`, `UE_TRACE_CHANNEL`, `UE_TRACE_LOG` (Epic tracing developer guide).
7. Niagara-specific: read `Niagara Manager Tick [GT]`, `System Simulation Tick`, `Emitter Tick [CNC]`, `Compute Dispatch` (render thread) and `Get Dynamic Mesh Elements` in Insights; use the Niagara Debugger, shader complexity and quad overdraw view modes. Launch with `-StatNamedEvents` for asset names.
8. Mass-specific: use `UMassDebuggerSubsystem`/Mass Debugger; in 5.8 the `UE_TRACE_MASS_ENTITIES_MOVED` trace macro and archetype-move events show archetype churn (Looman 5.8 summary).
9. Change one thing, re-capture the same scenario, compare traces. Re-measure on the 1M-entity worst case, not a small test scene. *(inference)*

---

## 3. Caveats and gaps

- Many Epic pages were summarized by the fetch tool; exact wording, defaults and CVar names must be verified in the UE 5.8 docs and source before turning any rule into an enforced lint.
- Sources tagged "Epic-adjacent" (S15 MassSample, S20 NukeTheBees) are community sources, not Epic. Their Mass claims (`mass.FullyParallel`, `mass.AllowQueryParallelFor`, chunk-vs-entity overhead) should be confirmed against the Mass source in 5.8.
- Rule 56 (ParallelFor uniform loops, no UObject access) is a synthesis: Epic's Tasks page had no UObject guidance; the GC/thread caution comes from Looman's 5.5 notes.
- Not accessible: Tom Looman's paid course lessons (tick batching, GC settings, SceneComponent movement, Significance Manager, pooling lectures; content locked); `tomlooman.com/unreal-engine-cpp-performance-optimization/` (404); Epic Mass Entity landing page (nav only), Niagara Data Channels page (empty body), "Optimizing Niagara" landing page (empty). Epic coding standard was read from the `epic-cplusplus-coding-standard-for-unreal-engine` URL after the first URL returned only a table of contents.
- No Epic source was found for Mass at 1M-entity scale specifically, or for origin rebasing cost with Mass entity data. Treat that as a design/measurement task for this project.

---

## 4. Sources (pages actually read)

- S1 Epic, Actor Ticking web-search summary (bCanEverTick / TickInterval / SetActorTickEnabled wording came from search result text, not from the fetched page S2): https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine
- S2 Epic, Actor Ticking in Unreal Engine: https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-ticking-in-unreal-engine
- S3 Looman, Game Optimization on a Budget: https://tomlooman.com/unreal-engine-optimization-talk/
- S4 Looman, Unreal Engine C++ Complete Guide: https://tomlooman.com/unreal-engine-cpp-guide/
- S5 Looman, UE 5.5 Performance Highlights: https://tomlooman.com/unreal-engine-5-5-performance-highlights/
- S6 Looman, Project Orion sample game: https://tomlooman.com/unreal-engine-sample-game-action-roguelike
- S7 Epic, Significance Manager: https://dev.epicgames.com/documentation/en-us/unreal-engine/significance-manager-in-unreal-engine
- S8 Epic, Incremental Garbage Collection: https://dev.epicgames.com/documentation/en-us/unreal-engine/incremental-garbage-collection-in-unreal-engine
- S9 Looman, actor pooling (search result summary of https://courses.tomlooman.com/courses/1982675/lectures/56589891 and Orion page S6)
- S10 Looman, UE 5.8 Performance Highlights (same page as S13; UObject Count counter)
- S11 Epic, Array Containers: https://dev.epicgames.com/documentation/en-us/unreal-engine/array-containers-in-unreal-engine
- S12 Epic, C++ Coding Standard: https://dev.epicgames.com/documentation/en-us/unreal-engine/epic-cplusplus-coding-standard-for-unreal-engine
- S13 Looman, UE 5.8 Performance Highlights: https://tomlooman.com/unreal-engine-5-8-performance-highlights/
- S14 Epic, Overview of Mass Entity: https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-entity-in-unreal-engine
- S15 Community (Megafunk), MassSample: https://github.com/Megafunk/MassSample
- S16 Epic, Large World Coordinates in UE5: https://dev.epicgames.com/documentation/unreal-engine/large-world-coordinates-in-unreal-engine-5
- S17 Epic, LWC Project Conversion Guidelines: https://dev.epicgames.com/documentation/unreal-engine/large-world-coordinates-project-conversion-guidelines-in-unreal-engine-5
- S18 Epic, LWC Rendering: https://dev.epicgames.com/documentation/unreal-engine/large-world-coordinates-rendering-in-unreal-engine-5
- S19 Looman, UE 5.7 Performance Highlights: https://tomlooman.com/unreal-engine-5-7-performance-highlights/
- S20 Community (NukeTheBees), Simplified Mass Query API: https://nukethebees.com/unreal-mass-entity-simplified-query-api/
- S21 Epic, Overview of Mass Gameplay: https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-mass-gameplay-in-unreal-engine
- S22 Epic, Tasks Systems: https://dev.epicgames.com/documentation/unreal-engine/tasks-systems-in-unreal-engine
- S23 Epic, Nanite: https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine
- S24 Epic, Scalability and Best Practices for Niagara: https://dev.epicgames.com/documentation/en-us/unreal-engine/scalability-and-best-practices-for-niagara
- S25 Epic, Measuring Performance in Niagara: https://dev.epicgames.com/documentation/en-us/unreal-engine/measuring-performance-in-niagara
- S26 Epic, Stat Commands: https://dev.epicgames.com/documentation/en-us/unreal-engine/stat-commands-in-unreal-engine
- S27 Epic, Animation Budget Allocator: https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine
- S28 Looman, Adding Counters and Traces to Insights and Stats: https://tomlooman.com/unreal-engine-profiling-stat-commands/
- S29 Epic, Developer Guide to Tracing: https://dev.epicgames.com/documentation/en-us/unreal-engine/developer-guide-to-tracing-in-unreal-engine
- S30 Epic, Trace Quick Start: https://dev.epicgames.com/documentation/en-us/unreal-engine/trace-quick-start-guide-in-unreal-engine
- S31 Epic, Memory Insights: https://dev.epicgames.com/documentation/en-us/unreal-engine/memory-insights-in-unreal-engine
- S32 Looman, Optimization Tutorials index: https://tomlooman.com/unreal-engine-optimization-tutorials/
