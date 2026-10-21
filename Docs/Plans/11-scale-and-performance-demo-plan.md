<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 11 — Scale and performance demo (NPC ships toward 1,000,000)

Design: [`Docs/SDDs/11-scale-and-performance-demo.md`](../SDDs/11-scale-and-performance-demo.md).

Labels: this is roadmap Part 10 (issue #11). Its sub-parts are labelled **10a-10g** in docs and code comments; commits
use `Issue #11: Part 10x - ...`.

Before each sub-part, check the SDD's open questions it is blocked on and get the user's answer first.

- [x] **10a — Pure logic, test-first.** Done 2026-10-15: 36/36 `SOLTest.NpcScaleMath` pass. One implementation
  reading settled by the tests: `SetOption` on a bool takes the raw value's truth (`-3` is on), not the clamped value.
  `Source/SOLTest/Npc/SOLNpcScaleMath.{h,cpp}` and the `SOL::NPC_*` constants
  in `SOLConstants.h`, per the contract below. Tests: `Source/SOLTest/Tests/NpcScaleMathTest.cpp` (written
  2026-10-15 against this contract, before any production code). Make them pass without editing them. If a test
  contradicts the contract, stop and ask.
- [x] **10b — Mass NPC archetype and movement at scale (spike, measured).** Done 2026-10-15, SDD 11 Amendment 1
  (motion seam, temporal cull added after the first ramp, `-corelimit=16` confirmed: 15 TaskGraph workers; 1M NPC update
  0.88 ms mean / 1.07 ms p95). The flags, ramp and `sol.Npc.Count/Ramp/Stop/FaceBattle` console commands were pulled
  forward from 10e.
  - `Npc/` fragments, tags and the const-shared class fragment, `USOLNpcSubsystem` (batch spawn and despawn from
    `MakeSpawn`, the per-population rail buffer, explicit processor runs on `OnUniverseUpdated`, `OnNpcsUpdated`).
  - `USOLNpcTierProcessor` (group cull with `DistanceToLaneM` / `DistanceToShellM`, rails for candidates,
    `BudgetedRadiusM` + `SelectAiTier`, tag changes through the command buffer) and `USOLNpcMidProcessor` (rail plus
    damped deviation). No AI or combat yet: Full ships behave like Mid.
  - `-SOLNpcCount=` and `-SOLNpcSeed=` (`ParseNpcCount`), `TRACE_CPUPROFILER_EVENT_SCOPE` on every phase, Insights
    counters for the tier counts.
  - Measure at 10k / 100k / 250k / 500k / 1M with `-corelimit=16`: spawn time, per-frame game-thread cost per phase,
    memory. Confirm `-corelimit` limits the TaskGraph workers. Record as SDD 11 Amendment 1.
- [x] **10c — Rendering tiers.** Done 2026-10-15, SDD 11 Amendment 2. Deviation: the point layers are a
  material-driven point-block ISM (`M_SOLNpcPoints`, `Tools/Npc/create_npc_assets.py`), not Niagara (no Niagara
  authoring path this session). 1M: +0.77 ms GPU, final ramp p95 6.17 ms at 1M (PASS).
  - `NS_SOLNpcFar` (GPU sim, rail parameters uploaded once per population, rails and far-depth compression evaluated
    in the shader, Mid-radius hide), the CPU-fed point system for Mid ships, the instanced tier with per-instance
    previous transforms, the mesh pool, all in `ASOLNpcVisuals`; `SelectRenderTier` + `BudgetedRadiusM` for the CPU
    ships. Asset scripts under `Tools/Npc/`, paths in `SOL::Paths`.
  - Screenshots of each tier and a lane from afar; `ProfileGPU` at 1080p with 1M.
- [x] **10d — AI tiers and NPC combat.** Built and measured 2026-10-15 (SDD 11 Amendment 3); 32/32
  `SOLTest.NpcAiMath` pass. Combat model answered 2026-10-20: **hybrid** (`SOL::NPC_COMBAT_DEFAULT_MODEL` = 2; SDD 11
  Amendment 4, with the beyond-20-km check). Not committed.
  - Write the 10d contract (steering, avoidance, target selection, statistical hit model, respawn rules) and have a
    separate agent write its tests first.
  - `USOLNpcFullAiProcessor`, the NPC bolt pool and its Niagara renderer, pooled NPC explosion effects.
  - Measure option A (bolts) at the full Full-tier budget, then decide A / B / hybrid (SDD 11 section 3.6) and show
    the user the numbers. Record as an amendment.
- [x] **10e — Demo menu, console, overlay, Ctrl+F1.** Done 2026-10-20 (SDD 11 Amendment 5). Tests from the 10e contract
  (separate agent) and the touched groups 135/135; `-SOLMenuSmoke=all` 50/0. Not committed. Not done: the PIE-viewport
  check of Ctrl+F1 (verified in standalone with real key events instead).
  - Demo page in `USOLMainMenuWidget` built from `OptionDescriptors` (steppers, presets, Start demo, Reset), the
    `sol.Npc.*` console commands, `-SOLNpcRamp`, `USOLNpcStatsWidget` (4 Hz), with a test-first contract for the
    overlay line formatting.
  - **Menu hotkey (Ctrl+F1):** fixed chord in the ship's mapping context, the pause and main menus' key handlers,
    F1 reserved in `SOLKeyBindings` (extend `SOLKeyBindingsTest.cpp`), and, only if open question 1 picks "straight to
    the main menu", a new `SOLMenuState` event (extend `SOLMenuStateTest.cpp`). Update `SOLMenuText::CONTROL_ROWS`,
    the Info prose, `GAME_MECHANICS.md` section 1 and section 8. PIE check that the editor does not swallow Ctrl+F1.
  - Extend `-SOLMenuSmoke` with the Demo page and Ctrl+F1.
- [ ] **10f — Profiling and hot-spot fixes toward 1M at 60 fps.**
  - Run the full ramp (`-SOLNpcRamp -SOLNpcCount=1M -corelimit=16`, 1080p, vsync off) and Insights captures at each
    failing stage. Fix hot spots until every stage passes `MeetsFrameTarget`. Revisit the existing tech-debt items
    that scale with entity count if they show up (combat minor-body snapshot, belt per-frame ISM upload, minor-body
    Kepler solve, ring-rock pool solve).
  - One packaged Development confirmation run at 1M. Record the per-stage table as an amendment.
- [ ] **10g — Docs, review, verification.**
  - `ARCHITECTURE.md` (the `Npc/` folder, the frame order, the flags, section 8's NPC note), `GAME_MECHANICS.md`
    (the Demo page, console commands, NPC numbers), `CLAUDE.md` tech debt for anything deferred.
  - Adversarial opus performance and style review, fixes, full suite, `graphify update .`, then commit and push.

### Progress notes (resume here)

- 2026-10-21 (10f review fixes done, SDD 11 Amendment 7; not committed): all 17 adversarial-review findings fixed
  (spawn-slice chunk cull, dirty clearing, hull range/shrink, mesh-pool motion vectors, lane wake bound, Mid-band double
  draw, Mid budget >= Full and Full radius <= Mid, Mid leash 3 km, GPU rail re-epoch, respawn on leaving Full,
  deterministic moves, nearest-first bolt cap plus a dropped counter, no per-frame region allocations, session-only
  combat model and overlay hidden on Stop, style, docs, tests). Tests: Npc*, CombatGrid, MenuState, InfoText,
  KeyBindings 146/146. Default-budget 1M ramp PASS (p95 8.40 ms). Demo option maxima kept (user decision).
  **Next:** the full suite in the editor, the packaged Development confirmation run at 1M, root-cause the
  render-thread stalls, then commit 10f.

- 2026-10-20 (10f, part 1 done, SDD 11 Amendment 6; not committed): `TRANSVERSE_ERROR_FRACTION` = 0.15 (parity with
  bolts measured; 0.1 overshot). Fixed: budget selection, hull ISM bounds (`USOLNpcHullInstances`), idle target-search
  throttle, open-addressed `SOLCombatGrid` + concurrent grid builds + parallel respawn pass, parallel visuals placement,
  tier passes over recorded chunk views + per-chunk wake minimum (`FSOLNpcChunkWakeFragment`) + 512 KB chunks, parallel
  Collect / gather / scatter, chunk-based despawn + time-sliced spawn (25k entities per frame). Tests: NpcScaleMath (one
  new large-array selection case), NpcAiMath, CombatGrid, NpcStatsText 89/89. W1 ramp PASS at every stage (1M p95 8.75
  ms); W2 (all options at maximum) FAILS from 250k (1M p95 20.4 ms). Soak (W1, 5.5 min): no leak (memory plateau ~3.9 GB),
  NPC update steady; frame spikes come from GC (~60 s) and render-thread stalls, not the NPCs.
  **Next:** W2 to 60 fps (GPU-driven hulls instead of CPU ISM transform updates; Full-AI step cost at 20k ships;
  serial ship-grid build ~1.3 ms), root-cause the 40-160 ms render-thread stalls (`LaunchVisibilityTasks`,
  `DeleteRHIResources`), the packaged Development confirmation run at 1M, then 10g. Scenario commands are in Amendment 6
  (W2 = every `sol.Npc.Option` at its maximum via `-ExecCmds`).

- 2026-10-20 (10d closed, 10e done): combat model = hybrid (Amendment 4). Beyond 20 km the statistical model is ~4x
  less lethal per shot than bolts (11% vs 48%); `TRANSVERSE_ERROR_FRACTION` ~0.1 would match, left for the user (it is
  a 10d contract value). Next: **10f** profiling (`sol.Npc.Option` now drives the worst-case scenarios from the
  console / `-ExecCmds`).

- 2026-10-15 (10d): built and measured, see Amendment 3. Waiting on the user's combat-model choice (open question 11).
  Follow-ups for 10f: 4,000 instanced hulls plus the Full AI raised frame time to ~7.3 ms at 1M (from ~5.5 ms); the
  Full AI gather / scatter are serial chunk walks (~0.3-0.4 ms). Classes' gun arcs were widened after the first run.

- 2026-10-15: 10a-10c done, not committed (per the session's instruction). Next: **10d** — write the 10d contract and
  have a separate agent write its tests first; SDD open question 11 (combat model) is answered only after the 10d
  measurements are shown to the user. Follow-ups for 10f found in 10b: the tier passes still walk every chunk (~0.7 ms
  at 1M even with no candidate cluster): per-chunk awake counts, a separate wake-key fragment, per-group queries.
  Worst-case scenarios (full Mid / instanced budgets) need the 10e option console to drive. The const shared class
  fragment (`FSOLNpcClassFragment`, acceleration / turn limits, weapons) was not needed by 10b and moves to 10d.
- The adversarial opus performance/style review of 10a-10c has not run yet (planned for 10g; may be run earlier).

## 10e contract

Pure logic for the Demo page, the menu hotkeys and the stats overlay. No UObjects, no world access, no global mutable
state. Tests: extend `Tests/SOLMenuStateTest.cpp` (`SOLTest.MenuState.Demo*`), `Tests/SOLKeyBindingsTest.cpp`
(`SOLTest.KeyBindings.*`), `Tests/SOLInfoTextTest.cpp` (`SOLTest.InfoText.*`), and a new `Tests/NpcStatsTextTest.cpp`
(`SOLTest.NpcStatsText.<Case>`). Written by a separate agent from this contract only.

### Menu state (`Menu/SOLMenuState.h`, namespace `SOLMenuState`; the header includes `Npc/SOLNpcScaleMath.h`)

`EMenuTab` gains `Demo` between `Settings` and `About`: `{ Resume, Controls, Info, Settings, Demo, About, Quit }`.
`SelectTab` treats it like any other tab (no other state-machine change; Start demo from the main menu is `PlayPressed`).

```cpp
enum class EDemoExtraRow : uint8 { CombatModel, Seed, Ramp, Count };   // rows after the EDemoOption rows
enum class EDemoPreset : uint8 { Showcase, Benchmark, Ramp, Count };
struct FDemoPage
{
    SOLNpcScale::FDemoOptions Options;                     // default-constructed = the descriptor defaults
    int32 CombatModel = SOL::NPC_COMBAT_DEFAULT_MODEL;      // SOLNpcAi::ECombatModel as int (0 bolts, 1 stat, 2 hybrid)
    bool bRamp = false;                                     // Start runs the stress ramp up to Options.NpcCount
};
```

Rows: row `r` in `[0, EDemoOption::Count)` is the option `EDemoOption(r)`; row `EDemoOption::Count + e` is
`EDemoExtraRow(e)`. `SOL::NPC_COMBAT_DEFAULT_MODEL` is 2 (hybrid).

| Function | Contract |
|---|---|
| `int32 DemoRowCount()` | `EDemoOption::Count + EDemoExtraRow::Count` (24). |
| `int32 DemoExtraRow(EDemoExtraRow extra)` | `EDemoOption::Count + extra`. |
| `FDemoPage DefaultDemoPage()` | `Options = SOLNpcScale::DefaultOptions()`, `CombatModel = SOL::NPC_COMBAT_DEFAULT_MODEL`, `bRamp = false`. |
| `FDemoPage StepDemoRow(const FDemoPage& page, int32 row, int32 direction)` | `direction == 0` or a row outside `[0, DemoRowCount())`: unchanged. Option rows: `Options = SOLNpcScale::StepOption(Options, EDemoOption(row), direction)`. CombatModel: the current value clamped to `[0, 2]`, then +1 (`direction > 0`) or -1 (`< 0`), clamped to `[0, 2]` (no wrap). Seed: `Seed + 1` or `Seed - 1` with `uint32` wraparound. Ramp: toggles. Every other field unchanged. |
| `FDemoPage ApplyDemoPreset(const FDemoPage& page, EDemoPreset preset)` | Any other preset value: unchanged. Otherwise, through `SOLNpcScale::SetOption` in this order: **Showcase** `NpcCount = 250000`, `BattleShipCount = 2000`, `ShowStats = 1`, and `bRamp = false`; **Benchmark** `NpcCount = 1000000`, `ShowStats = 1`, `bRamp = false`; **Ramp** `NpcCount = 1000000`, `ShowStats = 1`, `bRamp = true`. Every other field (other options, seed, combat model) unchanged. |
| `FString FormatDemoValue(const FDemoPage& page, int32 row)` | Empty for a row outside `[0, DemoRowCount())`. Bool options and Ramp: `On` / `Off`. Distance options (`BattleRadiusM`, `BattleMinDistanceM`, `BattleMaxDistanceM`, `FullAiRadiusM`, `MidAiRadiusM`, `MeshRadiusM`, `InstancedRadiusM`): `k = value / 1000`; `k < 10` gives `Printf("%.1f km", k)`, else `FormatGroupedInteger(round(k)) + " km"` (`3000` -> `3.0 km`, `500` -> `0.5 km`, `20000` -> `20 km`, `2000000` -> `2,000 km`). Integer options (`NpcCount`, `BattleShipCount`, `BattleCount`, `FullAiBudget`, `MidAiBudget`, `MeshBudget`, `InstancedBudget`): `FormatGroupedInteger` (`1,000,000`). `LaneFraction`, `AiHysteresis`, `RenderHysteresis`: `Printf("%.2f")`. `RampSettleS`, `RampMeasureS`: `Printf("%.0f s")`. CombatModel: `Bolts`, `Statistical`, `Hybrid` for 0, 1, 2 (any other value as `SOLNpcAi::CombatModelFromInt`, i.e. `Bolts`). Seed: `Printf("%u")` (no grouping). |

### Key bindings (`Menu/SOLKeyBindings.h`)

| Function | Contract |
|---|---|
| `bool IsMenuHotkey(const FString& keyName, bool bControlDown)` | True exactly when `bControlDown` and `keyName` is `F1` (case-insensitive). |
| `bool IsStatsHotkey(const FString& keyName, bool bControlDown)` | True exactly when `bControlDown` and `keyName` is `F2` (case-insensitive). |
| `IsReservedKey` | Now also true for `F1` and `F2` (so `CanBindKey`, `CanBindKeyTo`, `Rebind` and `ResolveKeys` refuse them like Escape); `F3` / `F4` stay bindable. |

`SOLMenuText::CONTROL_ROWS` gains `{ "Menu", "LeftControl+F1", ... }` (Ctrl+F1 opens the pause menu / resumes, like
Esc) and `{ "Demo", "LeftControl+F2", ... }` (show or hide the NPC stats overlay); `FormatBindings` shows them as
`Ctrl+F1` and `Ctrl+F2`. `SOLMenuText::INFO_PROSE` gains a paragraph that names `Ctrl+F1` and the `Demo` page.

### Overlay text (`Npc/SOLNpcStatsText.h/.cpp`, namespace `SOLNpcStatsText`, includes `Npc/SOLNpcScaleMath.h`)

```cpp
struct FOverlayInput
{
    double FrameMs = 0.0; double FrameP95Ms = 0.0; double GameMs = 0.0; double RenderMs = 0.0; double GpuMs = 0.0;
    double NpcMs = 0.0;
    int32 Total = 0; int32 Far = 0; int32 Mid = 0; int32 Full = 0;
    int32 Point = 0; int32 Instanced = 0; int32 Mesh = 0;
    int32 Awake = 0; int32 TierChanges = 0;
    bool bCombat = true; int32 CombatModel = 0; int32 CombatShips = 0; int32 Alive = 0; int32 LiveBolts = 0;
    int32 RealBolts = 0;
    double UsedMemoryMB = 0.0;
    SOLNpcScale::ERampPhase RampPhase = SOLNpcScale::ERampPhase::Idle; int32 RampStageIndex = INDEX_NONE;
    int32 RampStageCount = 0; int32 RampStageNpcCount = 0; double RampPhaseElapsedS = 0.0; double RampPhaseLengthS = 0.0;
    FString RampResult;    // the last finished stage (FormatRampResult), empty if none
};
```

A non-finite double input counts as 0 everywhere below.

| Function | Contract |
|---|---|
| `FString FormatGroupedInteger(int64 value)` | Decimal digits with `,` between groups of three from the right, `-` first for negatives (`0`, `999`, `1,000`, `-1,234,567`, `9,223,372,036,854,775,807`; `INT64_MIN` gives `-9,223,372,036,854,775,808`). |
| `FString FormatRampResult(int32 npcCount, double p95Ms, bool bPass)` | `Printf("%s ships p95 %.2f ms %s", FormatGroupedInteger(npcCount), p95Ms, bPass ? "PASS" : "FAIL")`. |
| `FString CombatModelName(int32 model)` | `bolts`, `statistical`, `hybrid` for 0, 1, 2; anything else `bolts`. |
| `TArray<FString> FormatOverlayLines(const FOverlayInput& in)` | Exactly 8 lines (`G` = `FormatGroupedInteger`): (1) `Printf("FPS %.0f  frame %.2f ms  p95 %.2f ms", fps, FrameMs, FrameP95Ms)` with `fps = 1000 / FrameMs` when `FrameMs > 0`, else 0; (2) `Printf("game %.2f  render %.2f  GPU %.2f  NPC %.2f ms", ...)`; (3) `"NPCs " + G(Total) + "  far " + G(Far) + "  mid " + G(Mid) + "  full " + G(Full)`; (4) `"render  point " + G(Point) + "  instanced " + G(Instanced) + "  mesh " + G(Mesh)`; (5) `"awake " + G(Awake) + "  tier changes " + G(TierChanges)`; (6) `"combat off"` when `!bCombat`, else `"combat " + CombatModelName(CombatModel) + "  alive " + G(Alive) + "/" + G(CombatShips) + "  bolts " + G(LiveBolts) + " (" + G(RealBolts) + " real)"`; (7) `"memory " + G(round(UsedMemoryMB)) + " MB"`; (8) the ramp line: Idle `"ramp idle"`; Done `"ramp done"`; Settling / Measuring `Printf("ramp stage %d/%d  %s ships  %s %.0f/%.0f s", RampStageIndex + 1, RampStageCount, G(RampStageNpcCount), "settling" or "measuring", RampPhaseElapsedS, RampPhaseLengthS)`; in every phase, `"  last " + RampResult` is appended when `RampResult` is not empty. |

## 10d contract

**Files.** Production: `Source/SOLTest/Npc/SOLNpcAiMath.h/.cpp`, `namespace SOLNpcAi`, `SOLTEST_API` free functions on
plain types (`double`, `int32`, `FVector3d`, `TConstArrayView`) and the plain structs below. No UObjects, no world
access, no global mutable state, no allocation. Tests: `Source/SOLTest/Tests/NpcAiMathTest.cpp`, paths
`SOLTest.NpcAiMath.<Case>`. Header includes `CoreMinimal.h` only. The gun cadence reuses the already-tested
`SOLFireCadence::Step` (issue #7) and is not part of this contract.

**Conventions.** Metres, seconds, radians, m/s. `EPS = SOLNpcAi::STEER_EPSILON` (`1e-6`): a vector or length below it
counts as zero. "Non-finite" means NaN or ±infinity. `normalize(v)` is `v / |v|`. A non-finite vector input that the
row does not mention counts as zero.

### Types and constants (in `SOLNpcAi`)

```cpp
inline constexpr double STEER_EPSILON = 1e-6;
inline constexpr double LEASH_SOFT_FRACTION = 0.8;       // the leash starts pulling at 80% of its length
inline constexpr double TRANSVERSE_ERROR_FRACTION = 0.15; // statistical model: miss from the target's sideways motion (0.5 until 2026-10-20, SDD 11 Amendment 6)

enum class ECombatModel : uint8 { Bolts = 0, Statistical = 1, Hybrid = 2 };

struct FClassParams              // one ship class (the const shared class fragment's payload)
{
    double MaxSpeedMps = 0.0;        // speed limit of the deviation velocity (relative to the rail)
    double MaxAccelMps2 = 0.0;
    double MaxTurnRateRadPerS = 0.0;
    double LeashM = 0.0;             // deviation length the ship may wander from its rail
    double HitRadiusM = 0.0;
    double MaxHealth = 0.0;
    double FireRatePerS = 0.0;
    double MuzzleSpeedMps = 0.0;
    double BoltDamage = 0.0;
    double BoltLifetimeS = 0.0;
    double EngageRangeM = 0.0;       // target acquisition range
    double FireRangeM = 0.0;         // the gun fires only inside this distance
    double FireConeRad = 0.0;        // half-angle between heading and aim inside which the gun fires
    double StandoffM = 0.0;          // preferred distance to the target
    double SeparationRadiusM = 0.0;  // avoidance radius
    double AimSpreadRad = 0.0;       // statistical model: aim error (1 sigma)
    double RespawnDelayS = 0.0;
};

struct FTargetCandidate { int32 Index = INDEX_NONE; FVector3d PositionM = FVector3d::ZeroVector; int32 Faction = 0;
    bool bAlive = true; };
struct FCombatState { double Health = 0.0; double RespawnS = 0.0; bool bAlive = true; };
struct FDamageResult { FCombatState State; bool bKilled = false; };
struct FRespawnStep { FCombatState State; bool bRespawned = false; };
```

### Classes

| Function | Contract |
|---|---|
| `int32 ClassCount()` | At least 2. |
| `const FClassParams& GetClass(int32 classIndex)` | The class at `classIndex` clamped to `[0, ClassCount() - 1]`. Every class: every field finite; every field `> 0` except `AimSpreadRad >= 0`; `FireConeRad <= PI`; `StandoffM < FireRangeM <= EngageRangeM`; `FireRangeM <= MuzzleSpeedMps * BoltLifetimeS`; `SeparationRadiusM >= 2 * HitRadiusM`; `BoltDamage < MaxHealth`; `MaxSpeedMps < MuzzleSpeedMps`. At least two classes differ in `MaxSpeedMps`. |
| `int32 PickClass(double unit)` | `floor(u * ClassCount())`, with `u` = `unit` clamped to `[0, 1)` (NaN counts as 0); always in `[0, ClassCount())`. |

### Steering

| Function | Contract |
|---|---|
| `FVector3d LimitSteering(const FVector3d& currentVelocityMps, const FVector3d& desiredVelocityMps, double maxSpeedMps, double maxAccelMps2, double maxTurnRateRadPerS, double dt)` | A non-finite or `<= 0` `dt` returns `currentVelocityMps` unchanged. A non-finite or negative limit counts as 0. (1) `d` = desired clamped to length `maxSpeed` (direction kept). (2) Turn limit: when `|current| >= EPS` and `|d| >= EPS` and the angle between them exceeds `theta = maxTurn * dt`, `d` is replaced by the vector of length `|d|` whose direction is `current`'s direction rotated by exactly `theta` toward `d`'s direction, in the plane of the two (for exactly opposite directions, any plane containing `current`). (3) Acceleration limit: `dv = d - current`, shortened to length `maxAccel * dt` if longer. Returns `current + dv`. So: the result equals the clamped desired whenever both limits allow it; `|result - current| <= maxAccel * dt`; the angle between the result and `current` is at most `theta` when both are non-zero; `|result| <= max(|current|, maxSpeed)`. |
| `FVector3d PursuitVelocity(const FVector3d& selfPositionM, const FVector3d& targetPositionM, const FVector3d& targetVelocityMps, double maxSpeedMps, double standoffM, double leadTimeS, const FVector3d& strafeAxis)` | `lead` = `leadTimeS`, 0 if non-finite or negative. `p = target + targetVelocity * lead`, `d = p - self`. Returns zero when `|d| < EPS` or `maxSpeed` is non-finite or `<= 0`. `dir = normalize(d)`; `k = clamp((|d| - standoff) / standoff, -1, 1)` for a finite `standoff > 0`, else `k = 1`. `t = normalize(dir x strafeAxis)`, or zero when that cross product is shorter than `EPS`. Returns `maxSpeed * (dir * k + t * (1 - |k|))` (so its length never exceeds `maxSpeed`: full speed in when far, out when too close, sideways at the standoff). |
| `FVector3d SeparationVelocity(const FVector3d& selfPositionM, TConstArrayView<FVector3d> neighbourPositionsM, double radiusM, double maxSpeedMps)` | Zero when `radius` or `maxSpeed` is non-finite or `<= 0`. `s` = sum over neighbours with a finite position and `EPS <= d < radius` (`d = |self - n|`; coincident ones, the ship itself included, are skipped) of `(self - n) / d * (1 - d / radius)`. Returns `s * maxSpeed`, shortened to length `maxSpeed` if longer. |
| `FVector3d LeashVelocity(const FVector3d& desiredVelocityMps, const FVector3d& deviationM, double leashM, double maxSpeedMps)` | `m = |deviation|`. Returns `desired` unchanged when `deviation` is non-finite or `m < EPS`. `w = clamp((m - F * L) / ((1 - F) * L), 0, 1)` with `F = LEASH_SOFT_FRACTION`, `L = leashM`; `w = 1` when `L` is non-finite or `<= 0`. `s` = `maxSpeed`, 0 if non-finite or negative. Returns `desired * (1 - w) - deviation / m * s * w` (pure pull back toward the rail at or beyond the leash). |

### Targets and guns

| Function | Contract |
|---|---|
| `int32 SelectTarget(int32 selfIndex, const FVector3d& selfPositionM, int32 selfFaction, int32 currentTarget, TConstArrayView<FTargetCandidate> candidates, double engageRangeM, double keepRangeScale)` | A valid enemy is a candidate with `Index != selfIndex`, `Index != INDEX_NONE`, `Faction != selfFaction`, `bAlive` and a finite position. `keep` = `keepRangeScale`, 1 if non-finite or `< 1`. A non-finite or `<= 0` `engageRangeM` gives `INDEX_NONE`. If `currentTarget != INDEX_NONE` and some candidate with that `Index` is a valid enemy closer than `engageRange * keep`, returns `currentTarget` (sticky). Otherwise returns the `Index` of the nearest valid enemy closer than `engageRange` (ties: the lower `Index`), else `INDEX_NONE`. Candidate order does not matter. |
| `FVector3d LeadAimDirection(const FVector3d& shooterPositionM, const FVector3d& shooterVelocityMps, const FVector3d& targetPositionM, const FVector3d& targetVelocityMps, double muzzleSpeedMps)` | The bolt leaves with `shooterVelocity + aim * muzzleSpeed` (as `SOLBoltMath::MuzzleVelocity`). `r = target - shooter`, `w = targetVelocity - shooterVelocity`. Zero vector when `|r| < EPS`. If `muzzleSpeed` is non-finite or `<= 0`, or `|r + w t| = muzzleSpeed * t` has no root `t > 0`, returns `normalize(r)`. Otherwise, with `t` the smallest positive root, returns `normalize(r + w t)` (a bolt fired along it meets a target moving at constant velocity). Always unit length or zero. |
| `bool ShouldFire(const FVector3d& headingDirection, const FVector3d& aimDirection, double distanceM, double fireRangeM, double fireConeRad)` | False when `distanceM` is non-finite or `<= 0`, `fireRangeM` is non-finite, `distanceM > fireRangeM`, or `aimDirection` is non-finite or shorter than `EPS`. Otherwise true when `headingDirection` is shorter than `EPS` (a ship at rest may fire), else when the angle between heading and aim is at most `fireConeRad` (a non-finite or negative cone counts as 0; within `1e-9` rad tolerance). |

### Damage, death and respawn

| Function | Contract |
|---|---|
| `FCombatState MakeAlive(double maxHealth)` | `{maxHealth, 0, true}`; a non-finite or `<= 0` `maxHealth` gives health 1. |
| `FDamageResult ApplyDamage(const FCombatState& state, double damage, double respawnDelayS)` | A dead state, or a non-finite or `<= 0` damage: unchanged, `bKilled` false. Otherwise `Health -= damage`; if then `<= 0`: `Health = 0`, `bAlive = false`, `RespawnS = respawnDelayS` (0 if non-finite or negative), `bKilled = true`. Otherwise still alive, `RespawnS` unchanged. |
| `FRespawnStep StepRespawn(const FCombatState& state, double dt, double maxHealth)` | An alive state, or a non-finite or negative `dt`: unchanged, `bRespawned` false. Otherwise `RespawnS -= dt`; once `<= 0` the state becomes `MakeAlive(maxHealth)` and `bRespawned` is true (a dead state with `RespawnS <= 0` respawns on any valid `dt`, 0 included), else it stays dead with the reduced timer. |

### Statistical hit model and model selection

| Function | Contract |
|---|---|
| `double TransverseSpeedMps(const FVector3d& relativePositionM, const FVector3d& relativeVelocityMps)` | The length of the velocity's component perpendicular to the position; `|velocity|` when `|position| < EPS`; 0 for any non-finite input. |
| `double StatisticalHitProbability(double distanceM, double transverseSpeedMps, double hitRadiusM, double aimSpreadRad, double muzzleSpeedMps, double fireRangeM)` | 0 when any input is non-finite, `distanceM < 0`, `distanceM > fireRangeM`, `hitRadiusM <= 0` or `muzzleSpeedMps <= 0`. A negative spread counts as 0, the transverse speed by its absolute value. With `tf = distance / muzzleSpeed` and `miss^2 = (spread * distance)^2 + (TRANSVERSE_ERROR_FRACTION * |vT| * tf)^2`, returns `R^2 / (R^2 + miss^2)` (`R = hitRadiusM`): 1 at distance 0, in `[0, 1]`, non-increasing in distance, spread and transverse speed. |
| `bool RollHit(double unit, double probability)` | `unit < clamp(probability, 0, 1)` (a NaN probability counts as 0); a NaN `unit` gives false. |
| `ECombatModel CombatModelFromInt(int32 value)` | 0 Bolts, 1 Statistical, 2 Hybrid; any other value Bolts. |
| `bool UsesRealBolts(ECombatModel model, double shooterDistanceToCameraM, double visualRadiusM)` | Bolts: true. Statistical: false. Hybrid: true when the distance is finite (a negative one counts as 0) and strictly less than a finite, positive `visualRadiusM`, else false. Any other enum value: false. |

## 10a contract

**Files.**

- Production: `Source/SOLTest/Npc/SOLNpcScaleMath.h/.cpp`, `namespace SOLNpcScale`, `SOLTEST_API` free functions on
  plain types (`double`, `int32`, `uint32`, `FVector2d`, `FVector3d`, `FName`, `FString`, `TArray`,
  `TConstArrayView`) plus the plain types below. No `USTRUCT`, no UObjects, no world access, no global mutable state.
  The only allocations are the returned arrays and strings and the documented use of caller-owned arrays.
- Constants: `SOLConstants.h`, namespace `SOL` (values are part of the contract and are hard-coded in the tests):
  - `NPC_MAX_COUNT = 1000000` (`int32`)
  - `NPC_RAMP_STAGE_COUNTS[] = { 10000, 100000, 250000, 500000, 1000000 }` (`inline constexpr int32` array)
  - `NPC_COUNT_PRESETS[] = { 0, 1000, 10000, 100000, 250000, 500000, 1000000 }`
  - `NPC_TARGET_FPS = 60.0`
  - `NPC_DEFAULT_SEED` (any fixed `uint32`)
- Tests: `Source/SOLTest/Tests/NpcScaleMathTest.cpp`, paths `SOLTest.NpcScaleMath.<Case>`.
- Includes for the header: `CoreMinimal.h`, `SOLConstants.h`. `ENUM_CLASS_FLAGS(EOptionIssue)` is declared in the
  header, inside the namespace.

**Conventions.** Units are metres, seconds and radians. "Non-finite" means NaN or ±infinity. `h'` for a hysteresis
fraction `h` means `h` when finite and `>= 0`, else 0.

### Types (in `SOLNpcScale`)

```cpp
enum class EAiTier : uint8 { Full = 0, Mid = 1, Far = 2 };            // nearest first; value = tier index
enum class ERenderTier : uint8 { Mesh = 0, Instanced = 1, Point = 2 }; // CPU-simulated ships only
enum class EPlacementKind : uint8 { Battle, Lane, Cluster };
enum class ERampPhase : uint8 { Idle, Settling, Measuring, Done };

struct FLaneDef { FName BodyA; FName BodyB; double RadiusM = 0.0; double Weight = 0.0; };
struct FClusterDef { FName Body; double InnerRadiusM = 0.0; double OuterRadiusM = 0.0; double MaxInclinationRad = 0.0;
    double Weight = 0.0; };
struct FLayout { TArray<FLaneDef> Lanes; TArray<FClusterDef> Clusters; double LaneMinSpeedMps = 0.0;
    double LaneMaxSpeedMps = 0.0; };
struct FBattleParams { int32 BattleCount = 1; double RadiusM = 0.0; double MinDistanceM = 0.0;
    double MaxDistanceM = 0.0; };
struct FCounts { int32 Battle = 0; int32 Lane = 0; int32 Cluster = 0;
    int32 Total() const { return Battle + Lane + Cluster; } };
struct FPlacementSlot { EPlacementKind Kind = EPlacementKind::Cluster; int32 LocalIndex = INDEX_NONE; };

struct FSpawn
{
    EPlacementKind Kind = EPlacementKind::Cluster;
    int32 GroupIndex = INDEX_NONE;               // lane, cluster or battle index
    int32 Faction = 0;                           // 0 or 1
    double LaneStartParam = 0.0;                 // lane only
    double LaneDirection = 1.0;                  // lane only: +1 or -1
    double LaneSpeedMps = 0.0;                   // lane only
    FVector2d LaneLateralOffsetM = FVector2d::ZeroVector;   // lane only
    double OrbitRadiusM = 0.0;                   // cluster only
    double OrbitInclinationRad = 0.0;            // cluster only
    double OrbitNodeRad = 0.0;                   // cluster only
    double OrbitPhaseRad = 0.0;                  // cluster only
    FVector3d BattleSlotOffsetM = FVector3d::ZeroVector;    // battle only
};

struct FRampState { ERampPhase Phase = ERampPhase::Idle; int32 StageIndex = INDEX_NONE; double PhaseElapsedS = 0.0; };
struct FRampStep { FRampState State; bool bRecordFrame = false; bool bStageFinished = false;
    int32 FinishedStageIndex = INDEX_NONE; };
struct FFrameStats { int32 Count = 0; double MeanMs = 0.0; double MinMs = 0.0; double P50Ms = 0.0; double P95Ms = 0.0;
    double P99Ms = 0.0; double MaxMs = 0.0; };

enum class EDemoOption : uint8
{
    NpcCount, LaneFraction, BattleShipCount, BattleCount, BattleRadiusM, BattleMinDistanceM, BattleMaxDistanceM,
    FullAiRadiusM, MidAiRadiusM, FullAiBudget, MidAiBudget, AiHysteresis, MeshRadiusM, InstancedRadiusM, MeshBudget,
    InstancedBudget, RenderHysteresis, NpcCombat, ShowStats, RampSettleS, RampMeasureS,
    Count
};

struct FDemoOptions
{
    int32 NpcCount; double LaneFraction; int32 BattleShipCount; int32 BattleCount; double BattleRadiusM;
    double BattleMinDistanceM; double BattleMaxDistanceM; double FullAiRadiusM; double MidAiRadiusM;
    int32 FullAiBudget; int32 MidAiBudget; double AiHysteresis; double MeshRadiusM; double InstancedRadiusM;
    int32 MeshBudget; int32 InstancedBudget; double RenderHysteresis; bool bNpcCombat; bool bShowStats;
    double RampSettleS; double RampMeasureS;
    uint32 Seed;                                 // not a menu option; untouched by SetOption / SanitizeOptions
};   // member initializers = the descriptor defaults (table below) and SOL::NPC_DEFAULT_SEED

struct FOptionDesc { EDemoOption Id; double Min; double Max; double Step; double Default; bool bInteger; bool bBool; };

enum class EOptionIssue : uint32
{
    None = 0,
    ValueClamped = 1 << 0,                // a field was non-finite or outside its descriptor range
    MidRadiusRaised = 1 << 1,
    InstancedRadiusLowered = 1 << 2,
    MeshRadiusLowered = 1 << 3,
    BattleShipsLimited = 1 << 4,          // BattleShipCount > NpcCount
    BattleDistanceRaised = 1 << 5,        // BattleMaxDistanceM < BattleMinDistanceM
    BattleShipsOverFullBudget = 1 << 6,   // warning only, nothing changed
    BattlesBeyondFullAi = 1 << 7,         // warning only: BattleMaxDistanceM + BattleRadiusM > FullAiRadiusM
};
struct FSanitizeResult { FDemoOptions Options; EOptionIssue Issues = EOptionIssue::None; };
```

### Tiers

| Function | Contract |
|---|---|
| `int32 SelectTierIndex(int32 currentTier, double distanceM, TConstArrayView<double> promoteRadiiM, double hysteresisFraction)` | With `N = promoteRadiiM.Num()`, returns a tier in `[0, N]` (`N` = beyond every radius). A non-finite `distanceM` gives `N`; a negative one counts as 0. For `k = 0 .. N-1` in order, the keep radius is `R_k * (1 + h')` when `0 <= currentTier <= k`, else `R_k`; the first `k` with `distanceM < keepRadius` is returned, else `N`. A `currentTier` outside `[0, N]` counts as "no tier" (only the plain radii apply). A NaN or non-positive radius never contains a ship; `+inf` contains every finite distance. Radii need not be ascending (first match wins). |
| `EAiTier SelectAiTier(EAiTier current, double distanceM, double fullRadiusM, double midRadiusM, double hysteresisFraction)` | `SelectTierIndex` with radii `{full, mid}`; 0 Full, 1 Mid, 2 Far. |
| `ERenderTier SelectRenderTier(ERenderTier current, double distanceM, double meshRadiusM, double instancedRadiusM, double hysteresisFraction)` | `SelectTierIndex` with radii `{mesh, instanced}`; 0 Mesh, 1 Instanced, 2 Point. |
| `double BudgetedRadiusM(TConstArrayView<double> distancesM, double nominalRadiusM, double hysteresisFraction, int32 budget, TArray<double>& scratch)` | Valid distances are the finite ones, a negative one counting as 0 (non-finite entries are ignored). `b = max(budget, 0)`. If at most `b` valid distances are `< nominalRadiusM * (1 + h')`, it returns `nominalRadiusM` unchanged. Otherwise it returns `min(nominalRadiusM, s[b] / (1 + h'))`, where `s` is the valid distances sorted ascending. So at most `b` valid distances lie strictly inside the returned radius times `(1 + h')`. `scratch` is caller-owned working storage; its contents afterwards are unspecified. |

### Distribution and rails

| Function | Contract |
|---|---|
| `double HashUnit(uint32 seed, uint32 index, uint32 stream)` | Deterministic, in `[0, 1)`. Statistically uniform over consecutive indices, and statistically independent across `seed` and `stream` (no simple relation between neighbouring indices, seeds or streams). |
| `int32 PickWeighted(double unit, TConstArrayView<double> weights)` | Non-finite or non-positive weights count as 0. With total `W > 0`, returns the smallest `i` with `u * W < cumulative[i]`, where `u` is `unit` clamped to `[0, 1)` (NaN counts as 0); a zero-weight entry is never returned. `W == 0` (or empty) gives `INDEX_NONE`. |
| `FCounts SplitCounts(int32 total, int32 battleShips, double laneFraction)` | `T = max(total, 0)`; `Battle = clamp(battleShips, 0, T)`; `rest = T - Battle`; `Lane = floor(rest * f + 0.5)` with `f` = `laneFraction` clamped to `[0, 1]` (NaN counts as 0); `Cluster = rest - Lane`. |
| `FPlacementSlot ClassifyIndex(int32 npcIndex, const FCounts& counts)` | Indices `[0, Battle)` are Battle (local index = `npcIndex`), then `[Battle, Battle + Lane)` Lane (local = `npcIndex - Battle`), then Cluster up to `Total()`. Outside `[0, Total())` the local index is `INDEX_NONE` (kind unspecified). |
| `FSpawn MakeSpawn(uint32 seed, int32 npcIndex, const FCounts& counts, const FLayout& layout, const FBattleParams& battle)` | Deterministic (same arguments, bit-identical result). Out-of-range index: a default `FSpawn`. In range, `Kind` is always `ClassifyIndex`'s kind, and fields of other kinds keep their defaults. **Battle:** `n = max(battle.BattleCount, 1)`, `GroupIndex = local % n`, `Faction = (local / n) % 2` (so each battle's factions differ in size by at most one), `|BattleSlotOffsetM| <= battle.RadiusM` (zero vector if the radius is non-finite or `<= 0`), uniform over the ball's volume. **Lane:** `GroupIndex = PickWeighted(hash, lane weights)`; if that is `INDEX_NONE`, every other field (faction included) keeps its default. Otherwise `Faction` in {0, 1} from the hash (about half each), `LaneStartParam` uniform in `[0, 1)`, `LaneDirection` exactly +1 or -1 (about half each), `LaneSpeedMps` uniform in `[min(LaneMin, LaneMax), max(LaneMin, LaneMax)]`, `LaneLateralOffsetM` uniform over the disc of radius `lane.RadiusM` by area (zero if the radius is non-positive). **Cluster:** `GroupIndex = PickWeighted(hash, cluster weights)`; if `INDEX_NONE`, every other field (faction included) keeps its default. Otherwise `Faction` in {0, 1} (about half each), `OrbitRadiusM` uniform in `[min(inner, outer), max(inner, outer)]`, `OrbitInclinationRad` uniform in `[0, MaxInclinationRad]` (exactly 0 if that is negative), node and phase uniform in `[0, 2*PI)`. Every hashed choice uses `HashUnit(seed, npcIndex, <stream>)` with a different stream per field. |
| `FVector3d BattleCenterOffsetM(uint32 seed, int32 battleIndex, const FBattleParams& battle)` | Deterministic. Length in `[min(dMin, dMax), max(dMin, dMax)]`, where a non-finite or negative distance counts as 0. Direction uniform over the sphere; different battle indices give different offsets. |
| `double TriangleWave01(double x)` | `m = x - 2 * floor(x / 2)`; returns `m` if `m <= 1`, else `2 - m`. Period 2, range `[0, 1]`, continuous. Non-finite gives 0. |
| `double LaneParamAt(const FSpawn& spawn, double nominalLaneLengthM, double timeS)` | `TriangleWave01(LaneStartParam + LaneDirection * LaneSpeedMps * timeS / L)`. A non-finite or non-positive `L`, or a non-finite time, gives `TriangleWave01(LaneStartParam)`. |
| `void LaneBasis(const FVector3d& axis, FVector3d& outU, FVector3d& outV)` | `a = normalize(axis)`. `outU` and `outV` are unit, perpendicular to `a` and each other, deterministic, right-handed (`outU x outV = a`). A zero or non-finite axis gives `outU = +X`, `outV = +Y`. |
| `FVector3d LanePositionM(const FVector3d& endpointAM, const FVector3d& endpointBM, double laneParam, const FVector2d& lateralOffsetM)` | `A + (B - A) * s + U * off.X + V * off.Y`, with `(U, V) = LaneBasis(B - A)`. `s` is not clamped. |
| `FVector3d ClusterOffsetM(const FSpawn& spawn, double bodyGM, double timeS)` | Circular orbit relative to the body. `r = OrbitRadiusM`; `r` non-finite or `<= 0` gives zero. `n = sqrt(GM / r^3)` for finite `GM > 0`, else 0; a non-finite time counts as 0. `theta = phase + n * t`. With `i` and `Om` the inclination and node, `P = (cos Om, sin Om, 0)`, `Q = (-cos i sin Om, cos i cos Om, sin i)`, it returns `r * (cos theta * P + sin theta * Q)`. So the orbit normal is `P x Q = (sin i sin Om, -sin i cos Om, cos i)` and `i = 0` is prograde in the XY plane. |
| `double DistancePointToSegmentM(const FVector3d& p, const FVector3d& a, const FVector3d& b)` | Euclidean distance from `p` to the closed segment (to `a` when `a == b`). |
| `double DistanceToLaneM(const FVector3d& p, const FVector3d& a, const FVector3d& b, double laneRadiusM)` | `max(0, DistancePointToSegmentM - max(radius, 0))`; a non-finite radius counts as 0. |
| `double DistanceToShellM(const FVector3d& p, const FVector3d& center, double innerRadiusM, double outerRadiusM)` | With `d = |p - center|`, `lo = min(inner, outer)`, `hi = max(inner, outer)`: `lo - d` if `d < lo`, `d - hi` if `d > hi`, else 0. |
| `FLayout DefaultLayout()` | At least one lane and one cluster. Every lane: two different, non-`None` body names, radius `> 0`, weight `> 0`. Every cluster: non-`None` body, `0 < inner < outer`, `0 <= MaxInclinationRad <= PI`, weight `> 0`. Includes the belt: a cluster on `"Sun"` with `inner >= 2.0 AU` and `outer <= 3.5 AU`. `0 < LaneMinSpeedMps <= LaneMaxSpeedMps`. |

### Ramp and frame statistics

| Function | Contract |
|---|---|
| `TArray<int32> BuildRampStages(int32 maxCount)` | `m = min(maxCount, NPC_MAX_COUNT)`. `m <= 0` gives an empty array. Otherwise every `NPC_RAMP_STAGE_COUNTS` entry strictly below `m`, in order, then `m`. |
| `FRampState StartRamp(int32 stageCount)` | `stageCount <= 0`: `{Done, INDEX_NONE, 0}`. Otherwise `{Settling, 0, 0}`. |
| `FRampStep StepRamp(const FRampState& state, int32 stageCount, double settleS, double measureS, double realDtS)` | `dt` is valid when finite and `> 0`; an invalid `dt` returns the state unchanged with no flags (no transition either). A non-finite or negative `settleS` / `measureS` counts as 0. Idle, Done, or a stage index outside `[0, stageCount)`: the state is returned unchanged with no flags. **Settling:** `elapsed += dt`; if `elapsed >= settle`, it becomes Measuring with `elapsed = 0` (no carry-over), else it stays Settling. `bRecordFrame` is false. **Measuring:** `bRecordFrame = dt valid`; `elapsed += dt`; if `elapsed >= measure`: `bStageFinished = true`, `FinishedStageIndex` = this stage, and the state becomes `{Settling, index + 1, 0}`, or `{Done, INDEX_NONE, 0}` after the last stage. At most one transition per step. |
| `int32 StageNpcCount(const FRampState& state, TConstArrayView<int32> stages)` | `stages[StageIndex]` while Settling or Measuring with a valid index, else `INDEX_NONE` (keep the current count). |
| `double PercentileNearestRank(TConstArrayView<double> sortedAscending, double fraction)` | Empty gives 0. A NaN or `<= 0` fraction gives the first element, `>= 1` the last. Otherwise the element at `clamp(ceil(fraction * N - 1e-9), 1, N) - 1`. |
| `FFrameStats SummarizeFrameTimes(TArray<double>& inOutFrameTimesMs)` | Removes non-finite and negative entries, then sorts the rest ascending in place. `Count` = kept entries; if 0, everything is 0. Otherwise `MeanMs` = arithmetic mean, `Min` / `Max`, and `P50` / `P95` / `P99` from `PercentileNearestRank` with 0.50 / 0.95 / 0.99. |
| `bool MeetsFrameTarget(const FFrameStats& stats, double targetFps)` | `Count > 0` and a finite `targetFps > 0` and `P95Ms <= 1000 / targetFps`. |
| `FString FormatRampStageLine(int32 npcCount, const FFrameStats& stats, double usedPhysicalMB, bool bPass)` | Exactly `FString::Printf(TEXT("NpcRamp stage=%d frames=%d meanMs=%.2f p50Ms=%.2f p95Ms=%.2f p99Ms=%.2f maxMs=%.2f fps=%.1f memMB=%.0f result=%s"), ...)`, where `fps = 1000 / MeanMs` when `MeanMs > 0`, else 0, and the result is `PASS` or `FAIL`. |

### Demo options

| Function | Contract |
|---|---|
| `TConstArrayView<FOptionDesc> OptionDescriptors()` | One entry per `EDemoOption` before `Count`, in enum order (`[i].Id == i`). Each has `Min <= Default <= Max`, and `Step > 0` except for `NpcCount` (presets, `Step` 0) and the two bools (`bBool`, range 0-1, step 1). Integer options (`bInteger`) have integral min, max and default. Values are the table in SDD 11 section 3.8, in metres and seconds (NpcCount 0..1,000,000, default 1,000,000). |
| `FDemoOptions DefaultOptions()` | Every option at its descriptor default, `Seed = NPC_DEFAULT_SEED`. Equal to a default-constructed `FDemoOptions`. `SanitizeOptions(DefaultOptions())` reports no issue. |
| `double GetOption(const FDemoOptions& options, EDemoOption id)` | The field's value as a double (bools 0 or 1). An invalid id gives 0. |
| `FSanitizeResult SanitizeOptions(const FDemoOptions& options)` | In this order: (1) each option field that is non-finite is reset to its default, and one outside `[Min, Max]` is clamped (both set `ValueClamped`); (2) `MidAiRadiusM = max(Mid, Full)` (`MidRadiusRaised` if changed); (3) `InstancedRadiusM = min(Instanced, Mid)` (`InstancedRadiusLowered`); (4) `MeshRadiusM = min(Mesh, Instanced)` (`MeshRadiusLowered`); (5) `BattleShipCount = min(BattleShips, NpcCount)` (`BattleShipsLimited`); (6) `BattleMaxDistanceM = max(Max, Min)` (`BattleDistanceRaised`). Then the warnings, with nothing changed: `BattleShipsOverFullBudget` when `BattleShipCount > FullAiBudget`, `BattlesBeyondFullAi` when `BattleMaxDistanceM + BattleRadiusM > FullAiRadiusM`. `Seed` is untouched. Idempotent: sanitizing the result changes nothing (warnings may repeat). |
| `FDemoOptions SetOption(const FDemoOptions& options, EDemoOption id, double value)` | An invalid id or NaN value returns the options unchanged. Otherwise the value is clamped to `[Min, Max]` (`±inf` to the bound); integer options round to nearest (`floor(v + 0.5)`); bools become `v != 0`. Then the result goes through `SanitizeOptions` (its `Options` are returned). |
| `FDemoOptions StepOption(const FDemoOptions& options, EDemoOption id, int32 direction)` | `direction == 0` or an invalid id: unchanged. Only the sign of `direction` matters. The new value always goes through `SetOption` (so the cross-option rules apply). Bools toggle. `NpcCount` moves to the nearest `NPC_COUNT_PRESETS` entry strictly above (`> 0`) or strictly below (`< 0`) the current count, unchanged at the ends. Every other option: `value + sign * Step`. |
| `int32 ParseNpcCount(const FString& text)` | Trims whitespace. Accepts one or more digits with at most one `.` among them, then an optional suffix `k` / `K` (x1,000) or `m` / `M` (x1,000,000). The result must be a whole number in `[0, NPC_MAX_COUNT]`, otherwise `INDEX_NONE`. So `"250000"`, `"250k"`, `"1M"`, `"1.5k"` and `" 10k "` parse, while `""`, `"abc"`, `"-5"`, `"1.5"`, `"1.2345k"`, `"2M"`, `"10kk"`, `"1e6"` and `"1,000"` give `INDEX_NONE`. |
