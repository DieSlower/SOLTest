<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 11 — Scale and performance demo (NPC ships toward 1,000,000)

Ticket `#11` (roadmap Part 10). Plan of record: [`Docs/Plans/11-scale-and-performance-demo-plan.md`](../Plans/11-scale-and-performance-demo-plan.md).
Refines the "Scalability" row of [SDD 1](1-solar-system-architecture.md) ("full MassEntity for everything, including
ships", ~1M entities as the goal) and must not contradict it. Decisions 1-8 below were resolved with the user in a
`grill-me` session on 2026-10-15. Rows marked **(proposed)** are design choices made while writing this SDD that the
user has not yet ruled on; each one is also listed in [section 5](#5-open-questions) and can be overturned there.

## 1. Problem

SOLTest was built "for massive scale", but nothing yet proves it: the largest Mass population is ~19,000 minor bodies
(belt plus ring pool), and several tech-debt items in `CLAUDE.md` already show linear per-frame costs that would not
survive 1M (the combat minor-body snapshot at ~20 ns per entity, the belt's per-frame ISM upload, the full Kepler solve
of every minor body every frame). Part 10 adds a population of NPC ships, ramps it to 1,000,000, and holds 60 fps on the
baseline machine, fixing the hot spots the ramp finds. It also adds a Demo menu to drive it, and a temporary Ctrl+F1
menu hotkey because Esc stops Play-In-Editor before the game sees it.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | NPC ships | MassEntity archetypes with **full AI** (steering, avoidance, combat), scaled by **AI level-of-detail tiers**: **Full** AI for ships near the player (a few thousand, hard budget), **Mid** (simple steering) at mid range, **Far** (cheap data-driven / statistical behavior) everywhere else. |
| 2 | Rendering LOD | **Niagara GPU points** far, **instanced meshes** near, **real meshes** closest. |
| 3 | Performance bar | **Hard requirement: 60 fps at 1,000,000 NPC ships.** A stress ramp 10k / 100k / 250k / 500k / 1M logs frame time per stage; hot spots are profiled and fixed until 1M passes. |
| 4 | Interaction | Visual only for the player: player bolts do not hit NPCs and NPCs cannot shoot the player (a separate later ticket). NPCs fight each other near the player using the Full-tier combat. Combat model (existing grid/bolts vs a lighter statistical model) is proposed in section 3.6 and decided by measurement in 10d. |
| 5 | Distribution | Spread across the solar system: **lanes** between planets, **clusters** around bodies and the asteroid belt, plus **dense battles near the player**. |
| 6 | Activation | Command-line flag `-SOLNpcCount=<n>`, console commands to change and ramp the count, an on-screen stats overlay, **and a Demo page in the existing main menu** to select and tune every option and start the demo (options list in section 3.8). |
| 7 | Hardware baseline | RTX 2080 Ti, 1080p. Pretend 16 CPU cores and 64 GB RAM, but fit in 32 GB: benchmark with worker threads limited to 16 cores (`-corelimit=16`) and track memory per ramp stage. |
| 8 | Menu hotkey (small related item) | Esc stops PIE, so the player can never reach the game menu in the editor. Add **Ctrl+F1** as a temporary key combo that brings up the menu in game. Esc keeps its current behavior. Controls page, Info text and `GAME_MECHANICS.md` are updated. Exact behavior: open question 1. |
| 9 | Motion model (confirmed) | Every NPC is **rail + deviation**: its position is an analytic rail (lane, cluster orbit or battle slot, a closed-form function of sim time) plus a bounded deviation vector integrated in real time by Mid and Full AI. Far ships have zero deviation and no per-frame CPU state. No per-ship gravity integration. See section 3.1 and open question 3. |
| 10 | Tier selection (confirmed) | Tiers are chosen by distance from the observer with hysteresis (promote inside radius `R`, keep until `R * (1 + h)`), and each CPU tier has a hard entity budget enforced by shrinking its radius for that frame. The same rule picks the render tier of CPU-simulated ships. See section 3.2. |
| 11 | Who draws what (confirmed) | The GPU point layer draws **only Far-AI ships**, evaluating their rails on the GPU from per-ship parameters uploaded once per population. Mid and Full ships are drawn from CPU positions: real meshes (closest, budget), instanced meshes (near, budget), or a CPU-fed point layer (rest of the Mid tier). See section 3.4. |
| 12 | Measurement protocol (confirmed) | Standalone `-game` Development build at 1920x1080, `r.vsync 0`, `t.maxfps 0`, `-corelimit=16`; a stage passes when its **95th-percentile frame time is at most 16.67 ms** (open question 2). Memory = process used physical memory per stage, budget 32 GB. |

## 3. Design

### 3.1 Motion model: rails plus deviation

Simulating 1M ships with per-substep gravity (the player ship's `USOLShipFlightProcessor` runs up to 16 substeps of
gravity against every body) is far outside a 16 ms frame. Instead each NPC's position is

```
worldPosition(t) = Rail(spawnParams, simTime, bodyPositions) + Deviation
```

- **Lane rail:** a lane is a segment between two bodies' current positions. A ship has a lane parameter `s` moving back
  and forth along it at a fixed fraction-per-second (`TriangleWave01(s0 + direction * speed * t / nominalLength)`),
  plus a fixed lateral offset inside the lane's radius, in a basis perpendicular to the lane. Lanes are powered flight,
  so no gravity term is needed.
- **Cluster rail:** a circular Keplerian orbit around the cluster body (radius, inclination, node, phase from the spawn
  hash; angular rate `sqrt(GM / r^3)`), i.e. gravity-consistent by construction. The asteroid-belt cluster is a cluster
  on the Sun with radii between 2.1 and 3.3 AU and a small inclination cap.
- **Battle rail:** a fixed slot offset inside a sphere around a battle centre, and the battle centre is a fixed offset
  from the player's reference body, chosen at demo start (so battles co-move with the player's frame, like dropped
  targets do).
- **Deviation:** a double-precision vector (with its velocity) relative to the rail. Far ships have none. Mid ships
  steer it back toward zero (critically damped). Full ships drive it with their AI (pursuit, evasion, avoidance), and
  it is bounded by a leash. Rails advance with **sim time** (so time warp moves them like bodies), while deviations
  integrate in **real time**, matching SDD 1's rule that ships always fly in real time.

Promotion and demotion are therefore continuous: a ship promoted from Far starts with zero deviation, and a ship
demoted to Far snaps its deviation to zero. The snap happens only beyond the Mid radius (thousands of km, far below a
pixel), so it is invisible.

This replaces `ARCHITECTURE.md` section 8's earlier note that NPCs would reuse the ship archetype and
`USOLShipFlightProcessor`. That note is updated in 10g if open questions 3 and 4 are confirmed.

### 3.2 AI and render tiers

- **AI tiers** (`EAiTier`): Full, Mid, Far. Selection by distance from the observer (the render viewpoint's universe
  position) with hysteresis (`SelectTierIndex`): a ship already in tier `k` or nearer keeps tier `k` until
  `R_k * (1 + h)`; a farther ship is promoted only inside `R_k`.
- **Budgets:** Full and Mid each have a hard budget. Before selection, each tier radius is shrunk for the frame to
  `BudgetedRadiusM`: the largest radius for which at most `budget` candidates lie inside its keep radius, so the budget
  holds even with hysteresis. The battle sizes default below the Full budget, so battles run Full AI.
- **Render tiers** (`ERenderTier`) for CPU-simulated ships (Mid and Full): Mesh (closest), Instanced, Point. Same
  hysteresis and budget rule. The instanced radius is never larger than the Mid AI radius, so every instanced or meshed
  ship has a CPU position.
- **Group culling:** a Far ship can only be promoted if its whole group (one lane capsule or one cluster shell) comes
  within the Mid radius. The tier processor first checks each group's distance (`DistanceToLaneM`,
  `DistanceToShellM`, O(groups) per frame), and evaluates rails only for ships in candidate groups. With the player
  near nothing, the per-frame CPU cost of 1M Far ships is a few dozen group tests.
- Tier changes are archetype moves (tags `FSOLNpcMidAiTag`, `FSOLNpcFullAiTag`) through the Mass command buffer. They
  are rare thanks to hysteresis and budgets (UE_PERFORMANCE_GUIDELINES rules 41-42), and are counted in the stats
  overlay so churn is visible.

### 3.3 Mass layout (new `Source/SOLTest/Npc/` feature folder)

| Type | Kind | Content |
|---|---|---|
| `FSOLNpcRailFragment` | fragment, all ships | Compact spawn parameters: placement kind, group index, faction, lane `s0` / direction / rate / lateral offset or orbit radius / inclination / node / phase or battle slot offset (written at spawn, read by the tier processor and the GPU upload). |
| `FSOLNpcTierFragment` | fragment, all ships | AI tier, render tier, render slot index. |
| `FSOLNpcMotionFragment` | fragment, all ships | Deviation and its velocity (double), last computed world position (Mid and Full only). |
| `FSOLNpcCombatFragment` | fragment, all ships | Health, gun cooldown, current target (entity index), respawn timer. |
| `FSOLNpcClassFragment` | const shared | Acceleration and turn limits, leash, weapon numbers, hit radius. |
| `FSOLNpcMidAiTag`, `FSOLNpcFullAiTag` | tags | Archetype-level tier filter for the processors' queries. |
| `USOLNpcTierProcessor` | processor | Group cull, rail evaluation for candidates, budgeted tier selection, tag changes via the command buffer. |
| `USOLNpcMidProcessor` | processor | Rail plus damped deviation for Mid ships (parallel chunks). |
| `USOLNpcFullAiProcessor` | processor | Full AI: target selection, steering, avoidance through a spatial grid, gun requests (parallel chunks). |
| `USOLNpcSubsystem` | world subsystem | Owns the options, spawns and despawns populations in batches (`BatchCreateEntities`), runs the processors explicitly (not phase-registered, like the minor-body processor), runs the ramp, collects stats, broadcasts `OnNpcsUpdated`. |
| `USOLNpcCombatSubsystem` (or inside the NPC subsystem) | world subsystem | NPC bolt pool (structure of arrays, like `USOLCombatSubsystem`, SDD 7 Amendment 1), sweeps against Full-tier NPCs only. |
| `ASOLNpcVisuals` | actor | The render tiers (section 3.4), redrawn on `OnNpcsUpdated`. |
| `USOLNpcStatsWidget` | C++ UMG | The stats overlay (section 3.8), refreshed at 4 Hz, not per frame. |

Memory estimate at 1M: about 200 bytes of Mass data per ship (~200 MB) plus the GPU parameter buffer (~48 MB of VRAM).
Measured per ramp stage, against the 32 GB budget.

**Frame order.** The NPC subsystem updates on `USOLAnchorSubsystem::OnUniverseUpdated` (after every `OnBodiesUpdated`
listener, so this frame's ship, bodies and render origin are final), like `USOLCombatSubsystem`, and broadcasts its own
`OnNpcsUpdated` for the visuals and the overlay. NPCs and the player's combat never touch each other's state, so their
relative order on `OnUniverseUpdated` does not matter. `ARCHITECTURE.md` section 3 is updated in 10g.

### 3.4 Rendering tiers

- **Far: GPU points** (`NS_SOLNpcFar`, GPU sim). The per-ship rail parameters are uploaded once per population (a
  Niagara array or buffer, no per-frame upload of 1M positions, which is the belt's scaling problem in the tech-debt
  list). Each frame only the body positions relative to the viewpoint, sim time, the viewpoint and the current Mid
  radius are set. The shader evaluates every rail, applies the same far-depth compression as `SOLRender::ComputePlacement`,
  and hides ships inside the Mid radius (those are drawn from the CPU). Points have a minimum on-screen size and a
  faction tint. Float precision is fine here, since a Far ship is at least the Mid radius (~2,000 km) away.
- **Mid beyond the instanced radius: CPU-fed points** (a second, smaller Niagara system fed with packed positions, up
  to the Mid budget).
- **Instanced:** one `UInstancedStaticMeshComponent` per faction hull, instances up to the instanced budget, transforms
  pushed with `BatchUpdateInstancesTransforms` without a render-state rebuild, and **per-instance previous transforms**
  so ships write motion vectors (the issue #15 lesson: anything riding through render space without them smears under
  TSR).
- **Mesh:** a small pool of pre-registered static mesh components (budget ~32) for the closest ships, with the future
  real hull (Part 8 art pass). Placeholder hulls until then (open question 10).
- **NPC bolts:** their own Niagara bolt system, built like `NS_SOLBolts` (SDD 7 Amendments 2 and 4).

### 3.5 Distribution

- `SplitCounts(total, battleShips, laneFraction)`: battle ships first (capped by the total), then the rest is split
  between lanes and clusters by the lane fraction.
- The default layout (`DefaultLayout`) lists lanes between neighbouring planets and Earth-Moon / Earth-Mars, and
  clusters around each planet, the Moon and the belt, each with a weight, all by registry body name (resolved to
  indices at spawn).
- Each ship's parameters come from a stateless per-index hash (`HashUnit(seed, index, stream)`), so generating ship
  *i* needs no array and the GPU could reproduce it. The same seed and options give the same population.
- Battles: `BattleCount` battle centres at `[BattleMinDistance, BattleMaxDistance]` from the player, each ship in a
  ball of `BattleRadius`, factions alternating within each battle.

### 3.6 NPC combat (proposed; decided by measurement in 10d)

- **Option A, bolts:** a separate NPC bolt pool reusing `SOLBoltMath`, `SOLCombatGrid` and `SOLFireCadence` (tested in
  issue #7), sweeping against Full-tier NPCs only. Expected load: ~4,000 Full ships at ~2 shots/s with 3 s lifetimes is
  ~24,000 live bolts, against SDD 7's measured ~0.6 ms for ~9,700 bolts, i.e. ~1.5 ms. Visible, real projectiles.
- **Option B, statistical:** each engagement rolls hits at a rate derived from range and closing speed. Bolts are
  cosmetic tracers only (not swept). Much cheaper, and less faithful.
- **Proposal: hybrid.** Real bolts within a visual range of the camera (default 20 km, the player's bolt range), and
  the statistical model beyond it but still inside the Full tier. 10d measures A alone at the full budget first. If A
  fits its 1.5 ms budget, the hybrid is not needed.
- Destroyed NPCs explode (pooled NPC effects, separate from the player's combat pools) and respawn after a delay into
  their battle (open question 5), so the population count, and the benchmark, stays exact.

### 3.7 Performance budget at 1M (1080p, 16.67 ms frame)

| Item | Budget |
|---|---|
| Existing game (no NPCs) | ~5 ms GPU, ~2 ms game thread (SDD 7 measured ~4.3 ms frames at 720p) |
| NPC game thread: tier processor + group cull | 1.0 ms |
| NPC game thread: Mid + Full AI + combat | 2.0 ms (parallel; wall time on the game thread) |
| NPC GPU: far points + CPU points | 1.5 ms |
| NPC GPU: instanced + mesh + bolts | 3.0 ms |
| Headroom | ~4 ms |

Claims are checked with `stat unit`, `stat game`, `ProfileGPU` and Unreal Insights (with `TRACE_CPUPROFILER_EVENT_SCOPE`
around every NPC phase and Insights counters for tier counts), not estimates.

### 3.8 Activation: flag, console, overlay, Demo menu

- **Flags** (in `SOL::CommandLine`): `-SOLNpcCount=<n>` (accepts `250000`, `250k`, `1M`, `1.5k`, see `ParseNpcCount`),
  `-SOLNpcRamp` (run the ramp up to that count, log each stage, then quit), `-SOLNpcSeed=<n>`. Like every `-SOL*`
  switch, they skip the main menu.
- **Console:** `sol.Npc.Count <n>`, `sol.Npc.Ramp [max]`, `sol.Npc.Stop`, `sol.Npc.Stats 0|1`, `sol.Npc.Combat 0|1`.
- **Overlay:** total count, per-tier counts (Far / Mid / Full and Point / Instanced / Mesh), tier changes per second,
  live NPC bolts, frame / game / render / GPU ms, NPC phase ms, used physical memory, and the ramp stage and its
  progress.
- **Demo page** in the main menu (a new column entry "Demo" next to Play), built in C++ like the Settings page from the
  option descriptors (`OptionDescriptors`). It has a stepper per option, three presets (Showcase: 250k with battles;
  Benchmark: 1M; Ramp: 1M with the ramp on), "Start demo" (spawns the ship at the picked start body, exactly as Play
  does, then the population) and "Reset". Options:

| Option | Range | Default |
|---|---|---|
| NPC count | presets 0, 1k, 10k, 100k, 250k, 500k, 1M (any value 0..1M by flag or console) | 1,000,000 |
| Lane fraction (rest are clusters) | 0-1 | 0.4 |
| Battle ships near the player | 0-20,000 | 2,000 |
| Battles | 1-16 | 4 |
| Battle radius | 0.5-20 km | 3 km |
| Battle distance min / max | 1-200 km | 5 / 20 km |
| Full AI radius / budget | 5-500 km / 0-20,000 | 50 km / 4,000 |
| Mid AI radius / budget | 50-20,000 km / 0-500,000 | 2,000 km / 100,000 |
| AI hysteresis | 0-0.5 | 0.1 |
| Mesh radius / budget | 0.5-20 km / 0-256 | 2 km / 32 |
| Instanced radius / budget | 1-2,000 km / 0-200,000 | 200 km / 20,000 |
| Render hysteresis | 0-0.5 | 0.1 |
| NPC combat | on / off | on |
| Stats overlay | on / off | on |
| Ramp settle / measure time | 1-30 s / 2-60 s | 5 s / 10 s |

  Cross-option rules (`SanitizeOptions`): Mid radius at least the Full radius, instanced radius at most the Mid radius,
  mesh radius at most the instanced radius, battle ships at most the count, battle max distance at least the min.
  Warnings are shown (not corrected) when the battle ships exceed the Full budget or battles reach past the Full radius.
  Options are session-only (open question 6).

### 3.9 Ctrl+F1 menu hotkey

A fixed (not rebindable) chord, handled wherever Esc is handled today: the ship pawn's mapping context (a chord with
Left or Right Ctrl), the pause menu's and main menu's `NativeOnKeyDown`. F1 becomes a reserved key in `SOLKeyBindings`,
so a rebind cannot put an action on F1 and fire it with every Ctrl+F1. Controls table row, `GAME_MECHANICS.md` row and
the Info prose are updated. The PIE check confirms the editor does not swallow Ctrl+F1. Behavior per open question 1.

### 3.10 Pure logic (test-first, 10a)

`Source/SOLTest/Npc/SOLNpcScaleMath.{h,cpp}`, `namespace SOLNpcScale`, tested in `SOLTest.NpcScaleMath.*`: tier
selection with hysteresis and budgets, distribution (split, per-index hash, weighted pick, spawn parameters, rails,
group distances, default layout), ramp state machine and frame-time statistics, and the demo option model (descriptors,
get / set / step, sanitize, count parsing). The contract is in the plan. Later sub-parts write their own test-first
contracts for steering, avoidance, NPC combat rules and overlay formatting.

### 3.11 Verification

- Automation tests for all pure logic (10a and later contracts).
- 10b spike: spawn time and per-frame cost of the archetype and tier processor at every ramp stage, recorded in an
  amendment.
- The ramp itself (`-SOLNpcRamp -SOLNpcCount=1M -corelimit=16`) is the acceptance test: every stage logs one
  `NpcRamp stage=...` line, and 1M must pass.
- PIE and standalone screenshots of each render tier and a battle. Smoke checks for the Demo page and Ctrl+F1.
- Adversarial opus performance and style review (10g).

## 4. Non-unit-tested pieces

The Mass processors, the subsystems, `ASOLNpcVisuals`, the Niagara systems and their shader rail evaluation, the Demo
page widget, the overlay widget and the console commands. They are covered by the ramp run, the 10b measurements, PIE
screenshots and a `-SOLMenuSmoke` extension for the Demo page.

## 5. Open questions

**Resolved 2026-10-15** (user answers, recorded before 10a-10c started):

1. Ctrl+F1 behaves exactly like Esc (opens the pause menu in flight; resumes from the pause menu; closes the jump map).
   No new `SOLMenuState` event.
2. Pass metric: 95th-percentile frame time at most 16.67 ms per stage.
3. NPC motion: rail plus bounded deviation (section 3.1), no per-ship gravity. The user may later expand to per-ship
   gravity or change the player and AI motion models completely, so **the motion model must stay behind a clean,
   swappable seam**: the rail evaluation and the deviation integration are separate functions/processors with no
   rail-specific assumptions leaking into tier selection, rendering or combat (they consume only "world position and
   velocity of NPC i").
4. Full tier: simple point-mass steering with acceleration and turn-rate limits.
5. NPC deaths respawn after a delay, so counts stay constant.
6. Demo options are session-only (no settings schema bump).
7. Benchmark: standalone `-game` from the editor (Development) build for the per-stage numbers, plus one packaged
   Development run at 1M.
8. Worker-thread limit through a launch argument (`-corelimit=16` style), not a Demo option.
9. Every Far ship is drawn at any distance, with a minimum pixel size and brightness falling off with distance.
10. Two faction-tinted placeholder hulls until the Part 8 art pass.
11. NPC combat model: **hybrid** (answered 2026-10-20 after the Amendment 3 measurements): real swept bolts for
    shooters within 20 km of the camera, the statistical model beyond. `SOL::NPC_COMBAT_DEFAULT_MODEL` = 2. See
    Amendment 4.

The original questions and recommendations are kept below for reference.

1. **Ctrl+F1 behavior.** (Recommended) It acts exactly like Esc in every state: in flight it opens the pause menu
   (from which Quit to menu reaches the main menu and its Demo page), in the pause menu it resumes, in the jump map it
   closes the map. Alternative: Ctrl+F1 goes straight to the main menu (a Quit to menu, despawning the ship), which
   needs a new `SOLMenuState` event. *(Blocks 10e.)*
2. **Pass metric for "60 fps".** (Recommended) 95th-percentile frame time at most 16.67 ms per stage. Alternatives:
   the mean frame time, or the 99th percentile. *(The 10a contract uses P95.)*
3. **NPC motion model.** (Recommended) Rails plus bounded deviation (section 3.1), with no per-ship gravity
   integration. Alternative: integrate gravity per ship in the Mid and Full tiers. *(Blocks 10b.)*
4. **Full-tier flight model.** (Recommended) Point-mass steering with acceleration and turn-rate limits from a
   const-shared class fragment. Alternative: reuse `SOLFlight::Step` and the substepped `USOLShipFlightProcessor` for
   Full ships (more faithful, about 16x the cost per ship). *(Blocks 10d.)*
5. **NPC deaths.** (Recommended) Respawn after a delay into their battle, so counts stay constant. Alternative: stay
   dead (the population shrinks during a battle). *(Blocks 10d.)*
6. **Demo option persistence.** (Recommended) Session-only. Alternative: save them in `USOLSettingsSave` (schema 3,
   with a migration). *(Blocks 10e.)*
7. **Benchmark build.** (Recommended) Standalone `-game` Development editor build for the per-stage numbers, like SDD
   7's measurements, plus one packaged Development confirmation run at 1M in 10f. Alternative: packaged only.
8. **Worker-thread limit.** (Recommended) A launch argument (`-corelimit=16`, checked in 10b that TaskGraph worker counts
   follow it), not a Demo-page option, since it cannot change at runtime.
9. **Far points at extreme distance.** (Recommended) Every Far ship is drawn as a point at any distance, with a
   minimum pixel size and brightness falling off with distance, so whole lanes read as faint streams. Alternative: cull
   beyond a distance.
10. **NPC art.** (Recommended) Two faction-tinted low-poly placeholder hulls built from engine shapes until the Part 8
    art pass. Alternative: wait for real hulls.
11. **NPC combat model.** The proposal is the hybrid in section 3.6. The final choice is made from the 10d
    measurements and shown to the user before 10d is closed.

## Amendment 1 — 10b: Mass archetype, motion seam and tier spike (measured)

Built 2026-10-15 (Part 10b). Files: `Npc/SOLNpcRail.{h,cpp}` (the motion model's rail half), `Npc/SOLNpcFragments.h`,
`Npc/SOLNpcTierProcessor.{h,cpp}`, `Npc/SOLNpcMidProcessor.{h,cpp}` (the deviation half), `Npc/SOLNpcSubsystem.{h,cpp}`.

**What differs from section 3.3.**

- **Motion seam (answer 3).** `FSOLNpcRail` is a 32-byte packed rail (kind, faction, global group, 7 floats). The same
  floats are evaluated by `SOLNpcRail::EvaluateM` on the CPU and by the far-point shader on the GPU, so a ship crossing
  the Mid radius does not jump. Tier selection, the renderer and (later) combat consume only
  `FSOLNpcStateFragment::PositionM`, never rail parameters. Replacing the motion model means replacing `SOLNpcRail`,
  `USOLNpcMidProcessor` and the shader's rail block.
- **Fragments.** `FSOLNpcRailFragment` (32 B), `FSOLNpcStateFragment` (position, previous position, wake key,
  distance, tiers; 72 B), `FSOLNpcDeviationFragment` (48 B), the const shared `FSOLNpcGroupFragment` (so chunks are
  grouped by lane, cluster or battle) and the `FSOLNpcMidAiTag` / `FSOLNpcFullAiTag` tags. No combat fragment yet
  (10d). Rails run on a population epoch (`RailTimeS` = sim seconds since the spawn) to keep float precision.
- **Group cull** as designed: a chunk filter on the group fragment, or an existing Mid / Full tag.
- **Temporal cull (new).** The first ramp showed the Earth cluster (~89k ships at 1M, the player inside its shell)
  costing 3.25 ms per frame: rail evaluation, a serial distance gather and the budget sort of every candidate. Each
  group now keeps a monotonic **wake clock** = (the viewer's path length relative to the group's anchor point) + (a
  bound on its ships' speed relative to that point, x1.25) x (rail time elapsed). A Far ship evaluated beyond the Mid
  keep radius stores `WakeKeyM = clock + (distance - keepRadius)` and is skipped (one compare) until the clock passes
  it, since it cannot have come closer before then. Speed bounds: lanes = max lane speed + the endpoints' relative
  speed; clusters = circular speed at the inner radius; battles = 0 (fixed relative to their body).
- **Budget input.** Instead of gathering every candidate distance, the Evaluate pass appends only distances inside
  the nominal Mid keep radius (stack-staged, one atomic reservation per 256) into a preallocated array.
  `BudgetedRadiusM` gives the same result from that subset, since only distances below the keep radius can matter.
- **Budget semantics.** The Mid budget caps the ships inside the Mid radius (Full included), i.e. all CPU-simulated
  ships. The Full budget caps those inside the Full radius. Render radii are budgeted on the same distances and
  clamped to the budgeted Mid radius.
- **Activation pulled forward from 10e** (needed for the measurements): `-SOLNpcCount=`, `-SOLNpcSeed=`, `-SOLNpcRamp`
  (the ramp: one `NpcRamp` and one `NpcRampDetail` line per stage, then quit), and the console commands
  `sol.Npc.Count`, `sol.Npc.Ramp`, `sol.Npc.Stop`, plus `sol.Npc.FaceBattle <i>` (turns the ship toward a battle, for
  screenshots). A periodic `NpcStats` log line (every 2 s) stands in for the 10e overlay. A count change despawns and
  respawns the whole population.
- **Worker limit (answer 8).** `-corelimit=16` is honoured: the log shows `cores=16 logical=16 taskGraphWorkers=15` on
  the 32-thread machine (UE caps both core counts in `FGenericPlatformMisc`).

**Measurements.** Standalone `-game` from the Development editor build, 1920x1080 windowed, `r.vsync 0`, `t.maxfps 0`,
`-corelimit=16`, the ship at the default spawn 10,000 km above Earth, 5 s settle and 10 s measure per stage, RTX 2080 Ti.
"npc ms" is the whole NPC update on the game thread (`USOLNpcSubsystem::Update`).

| Stage | npc ms mean / p95, before the temporal cull | npc ms mean / p95, after | Awake candidates | Mid / Full | Spawn ms (incl. despawning the previous stage) |
|---|---|---|---|---|---|
| 10,000 | 0.30 / 0.38 | 0.25 / 0.31 | 2,001 | 1 / 2,000 | 3 |
| 100,000 | 0.60 / 0.76 | 0.34 / 0.41 | 2,008 | 8 / 2,000 | 29 |
| 250,000 | 1.13 / 1.39 | 0.44 / 0.54 | 2,021 | 18 / 2,000 | 95 |
| 500,000 | 1.76 / 2.11 | 0.59 / 0.76 | 2,036 | 27 / 2,000 | 215 |
| 1,000,000 | 3.25 / 3.73 | 0.88 / 1.07 | 2,079 | 66 / 2,000 | 447 |

At 1M the remaining cost is evaluate 0.32-0.40 ms, select 0.35-0.44 ms, Mid 0.05, collect 0.03 and budget 0.02. It
does not come from the 2,079 awake ships but from walking the ~1,100 chunks: the chunk filter runs on every chunk, and
the Earth cluster's ~89k sleeping ships each cost one compare in both passes. Even with no candidate cluster (spawned
0.15 AU from Venus) evaluate plus select stay ~0.7 ms. Candidates for 10f: a per-chunk awake count (a chunk fragment)
so Select skips all-asleep chunks, the wake key in its own 8-byte fragment, and one query per candidate group instead
of a chunk filter over every chunk. Tier churn was 0 changes per frame at rest.

**Memory.** Right after the 1M spawn the process had grown 130 MB over the 500k level it replaced (Mass entity data,
~152 B per ship, plus the 32 B rail array and the 8 B budget array), plus the 30.5 MB rail texture (10c). Whole
process at 1M: ~3.8 GB, against the 32 GB budget.

**Frame time with rendering (10c included):** every stage passes `MeetsFrameTarget` (p95 at most 16.67 ms) with a
wide margin; see Amendment 2.

## Amendment 2 — 10c: rendering tiers (measured)

Built 2026-10-15 (Part 10c). `Npc/SOLNpcVisuals.{h,cpp}` (`ASOLNpcVisuals`, spawned by `ASOLGameMode::StartPlay`),
`Tools/Npc/create_npc_assets.py` (`/Game/SOL/Npc/M_SOLNpcPoints`, `M_SOLNpcHull`, `T_SOLNpcFloatDefault`), paths in
`SOL::Paths::NPC_*`, tunables `SOL::NPC_POINT_*`, `NPC_HULL_*` and `NPC_FACTION_COLORS`.

**Deviation from section 3.4: no Niagara for the point layers.** The unreal-mcp Niagara toolset was not reachable in
this session, and Niagara graphs cannot be authored from editor Python, so both point layers are a
**material-driven instanced mesh** with the same contract (rails uploaded once, evaluated on the GPU, no per-frame
position upload):

- A runtime-built **point-block mesh** of 4,096 triangles (UV0 = billboard corner, UV1 = the ship's index in the block
  as (i % 64, i / 64), exact even in half-precision UVs), drawn as one ISM instance per block (245 at 1M) whose custom
  data is the block's first ship index. The mesh's bounds extensions cover all of render space, so blocks are never
  culled.
- **Far layer:** the rails go once per population into a 2048-wide `PF_A32B32G32R32F` texture (two texels per ship,
  30.5 MB at 1M). Per frame only a 64x2 group texture (group points relative to the viewpoint, float) and four vector
  parameters change. `M_SOLNpcPoints`'s vertex shader evaluates the rail (the same formulas as `SOLNpcRail`), applies
  the far-depth compression of `SOLRender::ComputePlacement`, hides ships inside the budgeted Mid radius (those are
  drawn from the CPU), and expands a camera-facing triangle of at least 2 px. Brightness falls off as
  `(2,000 km / d)^0.5`, floored at 4% (answer 9). Faction tints are blue and orange; the blend is additive.
- **CPU points:** Mid and Full ships in the Point render tier. Their positions relative to the viewpoint are written
  each frame into a 1024-wide float texture (grown in power-of-two rows) and drawn by the same material in CPU mode.
- **Instanced:** one ISM per faction placeholder hull (faction 0 an engine cone, faction 1 a flattened engine cube,
  30 m, `M_SOLNpcHull` tinted with a 60% tint glow so the night side reads), at least 3 px on screen, heading from the
  last frame's motion, with **per-instance previous transforms** (`SetHasPerInstancePrevTransforms`) so they write
  motion vectors (issue #15).
- **Mesh:** a pool of 256 static mesh components (alternating faction hulls) for ships in the Mesh tier.

**Measurements** (same setup as Amendment 1; `ProfileGPU` with 1 NPC against 1M, default spawn near Earth with ~89k
Earth-cluster ships and two lanes in view):

| | 1 NPC | 1M NPCs |
|---|---|---|
| GPU frame | 3.96 ms | 4.76 ms |
| Translucency (the point layers draw here) | 0.24 ms | 1.01 ms |
| BasePass (2,000 instanced battle hulls at 1M) | 0.24 ms | 0.24 ms |

So the far layer costs ~0.77 ms of GPU at 1M (budget 1.5 ms), and the instanced battle hulls are within noise.

**Final ramp** (all tiers drawing, `-corelimit=16`, 1080p):

| Stage | Mean ms | p50 | p95 | p99 | fps | Result |
|---|---|---|---|---|---|---|
| 10,000 | 4.70 | 4.61 | 5.43 | 6.32 | 212.8 | PASS |
| 100,000 | 4.94 | 4.85 | 5.75 | 6.30 | 202.3 | PASS |
| 250,000 | 4.95 | 4.85 | 5.65 | 6.22 | 202.0 | PASS |
| 500,000 | 5.05 | 4.97 | 5.73 | 6.18 | 198.0 | PASS |
| 1,000,000 | 5.46 | 5.37 | 6.17 | 7.01 | 183.1 | PASS |

With 1 NPC the same run gives mean 4.59 / p95 5.53 ms. The ceiling reached so far is the 1M cap at ~183 fps in this
scenario. 10d's Full AI and combat, and 10f's worst cases (dense regions, large budgets), are not in these numbers.

**Screenshots** (1280x720 `HighResShot`): the default spawn with 1M (Earth-cluster dots and lanes, with and without
orbit lines), a battle through `sol.Npc.FaceBattle 0` at FOV 25 (orange box and blue cone hulls), and a far view from
0.15 AU off Venus (lanes converging, the ecliptic band).

**Known issues and follow-ups.** The point layers are additive translucency with no velocity (Far ships are at least
the Mid radius away, so their parallax is tiny). The CPU-point upload goes through `UpdateTextureRegions` with a
3-deep buffer ring. No LOD on the hull meshes. The mesh pool uses the instanced placeholder look until the Part 8 art.
Worst-case scenarios (budgets full) need the 10e option commands to drive; not measured yet.

## Amendment 3 — 10d: Full AI, NPC combat, the three combat models (measured; model not chosen)

Built 2026-10-15 (Part 10d). Contract: the plan's "10d contract" (`SOLNpcAiMath`, tests `SOLTest.NpcAiMath.*`, 32
cases written first by a separate agent, all pass). Files: `Npc/SOLNpcAiMath.{h,cpp}`, `Npc/SOLNpcCombat.{h,cpp}`
(`FSOLNpcCombat`), `Npc/SOLNpcFullAiProcessor.{h,cpp}`, `Npc/SOLNpcCombatVisuals.{h,cpp}`, plus changes to the
fragments, the Mid and tier processors, the subsystem and the game mode.

**What differs from section 3.3 / 3.6.**

- **Class fragment.** `FSOLNpcClassFragment` (const shared) holds a class index into `SOLNpcAi::GetClass` (fighter,
  gunship, interceptor; picked per ship by `HashUnit`), not the numbers themselves, so equal classes share one
  fragment. Spawn batches by (group, class). `FSOLNpcCombatFragment` (all ships, 20 B): population index, target,
  health, respawn timer, gun cooldown, alive.
- **Full AI as gather / step / scatter.** `USOLNpcFullAiProcessor` gathers the Full ships (serial chunk walk) into a
  flat list in `FSOLNpcCombat`, which steers them in parallel (`ParallelForWithExistingTaskContext`, per-task scratch
  kept across frames), then scatters the new deviations back. A flat list lets every ship see every other through
  two `SOLCombatGrid`s (2 km cells for target search, 250 m cells for separation and bolt sweeps). The AI writes only
  the deviation, so the motion seam (Amendment 1) holds. Everything is relative to the battles' body, in real time.
  The Mid processor now runs Mid ships only (plus the respawn timer of a ship that died and was demoted).
- **Target search.** A ship keeps a live target within 1.2x the engage range; otherwise it searches a 1 km ring first
  (which already holds the nearest enemy if it finds one) and then the whole 4 km range. The first build re-picked
  every second over the whole range and showed 15-35 ms `npcMaxMs` spikes; with the ring and no periodic re-pick
  the worst frame is ~3 ms.
- **Gun arcs widened** after the first measurement: with the strafe at the standoff almost no ship had its target
  inside a 0.25-0.6 rad cone (2-8 shots per frame). Now fighter 1.6, gunship 3.1 (turrets), interceptor 1.0 rad.
- **Dead ships** are skipped by the Collect pass (not drawn); they respawn at their rail slot (deviation zero).
- **Renderer.** `ASOLNpcCombatVisuals` feeds the NPC bolts within 20 km of the viewpoint (cap 8,192) to its own
  `NS_SOLBolts` component (same asset as the player's bolts, so the same look, no faction tint), and runs pooled NPC
  explosions (16) and impacts (24) for events within 20 km. Hull headings follow the steering velocity.
- **Switch.** cvar `sol.Npc.CombatModel` (0 bolts, 1 statistical, 2 hybrid; default 2 hybrid since Amendment 4), cvar `sol.Npc.Combat 0|1`, command `sol.Npc.BattleShips <n>`, flags `-SOLNpcBattleShips=<n>` and
  `-SOLNpcFaceBattle=<i>`.

**Measurements.** Standalone `-game` from the Development editor build, 1920x1080 windowed, vsync off,
`-corelimit=16`, RTX 2080 Ti, 1M NPCs with 4,000 battle ships (= the Full budget: all 4,000 Full), 4 battles of 3 km
at 5-20 km, the ship facing battle 0. Means over 20 steady 2-s windows (spawn, `ProfileGPU` and screenshot windows
excluded). "Full AI" is gather + step + scatter on the game thread; "AI", "bolts" and "fire" are its parts. GPU is the
mean `RHIGetGPUFrameCycles`.

| Model | Frame ms | Game ms | GPU ms | NPC update ms | Full AI ms | AI ms | Bolts ms | Fire ms | Live bolts (swept) | Shots/frame | Hits/frame | Alive of 4,000 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Combat off (steering only) | 7.32 | 7.19 | 4.32 | 2.13 | 1.10 | 0.89 | 0 | 0 | 0 | 0 | 0 | 4,000 |
| 0 Bolts | 7.90 | 7.71 | 5.15 | 2.05 | 1.03 | 0.58 | 0.15 | 0.05 | 4,301 (4,301) | 13.3 | 9.3 | 2,185 |
| 1 Statistical | 7.79 | 7.63 | 4.83 | 2.16 | 1.15 | 0.78 | 0.07 | 0.05 | 3,728 (0) | 23.8 | 2.8 | 3,324 |
| 2 Hybrid | 7.93 | 7.74 | 5.18 | 2.08 | 1.05 | 0.68 | 0.12 | 0.06 | 3,298 (3,298) | 19.5 | 7.6 | 2,454 |

`ProfileGPU` (one frame each): BasePass 0.17 ms (off) / 0.32 (bolts) / 0.29 (statistical) / 0.41 (hybrid);
the bolt Niagara dispatch 0.01 ms; frame 4.3 / 5.1 / 5.2 / 5.5 ms.

**Reading.** All three fit the 2 ms "Mid + Full AI + combat" budget with room to spare; the combat models differ by
less than ~0.1 ms of CPU and ~0.3 ms of GPU, inside run-to-run noise. The swept bolts cost ~35 ns per bolt per frame
(0.15 ms for 4,300); at the SDD's original estimate of ~24,000 live bolts that extrapolates to ~0.8 ms (not measured:
the AI's gun arcs and the deaths keep the load far lower). The statistical model hits far less (2.8 vs 9.3 hits per
frame, so ~83% alive vs ~55%), because exact lead aim against slow-turning targets is more accurate than the miss
model; its tracers are cosmetic and pass through ships. In this scene the battles sit 5-20 km away, so the hybrid's
20 km radius covers most shooters and it behaves almost like bolts. The steering itself (~0.9-1.1 ms with 4,000 Full
ships) is the larger cost, not the combat. The frame and game-thread time grew from Amendment 2's ~5.5 ms to ~7.3 ms
mostly from 4,000 (instead of 2,000) instanced hulls and the Full AI; 10f profiles that.

**Recommendation (not final; open question 11 stays open for the user):** option A, **bolts** everywhere. It is the
faithful model (real projectiles that visibly hit), it costs about the same as the others at the Full-tier cap, and
the bolt pool and sweeps scale linearly with a measured ~35 ns per bolt, so even the original 24k-bolt estimate stays
under 1 ms. The hybrid only pays off if battles sit well beyond 20 km of the camera, where bolts are invisible anyway.

**Screenshots** (`Saved/Screenshots/WindowsEditor/HighresScreenshot00004.png` bolts, `00005` statistical,
`00006` hybrid, 1280x720): battle 0 from 5-20 km, a dense cloud of bolt streaks among the hulls.

## Amendment 4 — Combat model: hybrid (open question 11 answered)

Answered by the user on 2026-10-20: **hybrid**. `SOL::NPC_COMBAT_DEFAULT_MODEL` = 2: a shooter within
`NPC_COMBAT_VISUAL_RADIUS_M` (20 km) of the camera fires real swept bolts, a shooter beyond it rolls statistical hits
(no tracers, no effects: nothing is drawn there anyway). The bolts and statistical models stay selectable
(`sol.Npc.CombatModel`, the Demo page's combat-model row).

**Check beyond 20 km** (2026-10-20; standalone `-game`, 1080p, `-corelimit=16`, 1M NPCs, the default 2,000 battle ships
in 4 battles, the ship facing battle 0; means over 12 two-second `NpcCombat` windows after spawn; battle distances set
with the new `sol.Npc.Option`):

| Run | Battles at | Alive of 2,000 | Live bolts | Shots / frame (statistical) | Hits / frame | Hits per shot | Full AI ms | Frame ms |
|---|---|---|---|---|---|---|---|---|
| Hybrid | 5-20 km | 1,287 | 2,057 | 15.8 (4.2) | 6.1 | 39% | 0.61 | 6.8 |
| Hybrid | 25-40 km | 1,663 | 0 | 14.4 (14.4) | 1.6 | 11% | 0.59 | 6.6 |
| Bolts | 25-40 km | 1,144 | 2,602 | 8.8 (0) | 4.2 | 48% | 0.57 | 6.3 |

Beyond 20 km the hybrid runs fully statistical as designed: no bolts are kept (bolt sweep ~0 ms), the fights go on
(hits, kills and respawns every window), the cost is the same as near the camera, and a battle straddling 20 km mixes
both paths (the 5-20 km run: about a quarter of the shots statistical). Screenshot `Saved/Screenshots/Hybrid_far.png`:
the far battle as a cloud of faction hulls, no bolts. **Finding:** the statistical model is about 4x less lethal per
shot than lead-aimed bolts (11% vs 48% hits per shot; ~83% vs ~57% of ships alive), because its transverse-error term
(`TRANSVERSE_ERROR_FRACTION` = 0.5 of the target's sideways travel during the bolt's flight) dominates the miss distance,
while real bolts with exact lead aim hit steady-flying targets. So a battle's attrition changes as it crosses 20 km.
Not changed here (the fraction is part of the tested 10d contract): a fraction around 0.1 would roughly match the bolt
hit rate; left as a follow-up for the user to decide.
## Amendment 5 — 10e: Demo page, stats overlay, Ctrl+F1 / Ctrl+F2

Built 2026-10-20 (Part 10e). Contract: the plan's "10e contract" (tests written first by a separate agent:
`SOLTest.MenuState.Demo*`, `SOLTest.KeyBindings.MenuHotkeys` / `ReservedFunctionKeys` / `HotkeyControlRows`,
`SOLTest.InfoText.DemoParagraph`, `SOLTest.NpcStatsText.*`). Files: `Menu/SOLDemoPageWidget.{h,cpp}`,
`Npc/SOLNpcStatsText.{h,cpp}`, `Npc/SOLNpcStatsWidget.{h,cpp}`, plus changes to `SOLMenuState`, `SOLKeyBindings`,
`SOLMenuText`, the main and pause menus, the menu subsystem, the menu smoke, the ship pawn and `USOLNpcSubsystem`.

- **Demo page model** (`SOLMenuState::FDemoPage`): the 21 `EDemoOption`s plus three rows the 10a option model does not
  hold: the combat model (0-2, clamped), the seed (+/-1, wrapping) and "run the stress ramp". Presets Showcase (250k,
  2,000 battle ships), Benchmark (1M), Ramp (1M, ramp on); every preset turns the overlay option on. The model lives in
  `USOLMenuSubsystem` (session-only, answer 6), is edited only through the pure functions, and is **re-read from
  `USOLNpcSubsystem` whenever a menu opens from flight** (and at start), so `-SOLNpcSeed`, `-SOLNpcBattleShips`,
  `sol.Npc.Count`, `sol.Npc.BattleShips` and `sol.Npc.CombatModel` show on the page. Edits survive switching pages while
  the menu stays open.
- **Start.** Main menu: "Start demo" is Play plus `USOLNpcSubsystem::StartDemo(options, model, ramp)` right after the
  ship spawns (battles form around it). Pause menu (new Demo tab between Settings and About): "Apply" calls `StartDemo`
  at once (respawn around the ship, or ramp) and resumes; "Stop NPCs" calls `Stop`. Quit to menu now stops the NPCs
  (they would otherwise keep updating with no ship); their options stay.
- **Reconciling the flags.** Without `-SOLNpcCount` the NPC subsystem no longer calls `RequestNpcCount(0)` at start
  (which had clamped the option `NpcCount` and the battle ships to 0), so the page opens at the defaults. Flags and
  console commands keep working as before and still skip the menu.
- **Overlay** (`USOLNpcStatsWidget`, top right, z-order 5 below the menus, monospace): the eight lines of
  `SOLNpcStatsText::FormatOverlayLines`, refreshed at 4 Hz by the NPC subsystem from a 240-frame ring (mean and p95
  through `SummarizeFrameTimes`). Shown when a demo starts with its Stats option on; `sol.Npc.Stats [0|1]` and Ctrl+F2
  toggle it. It stays off for flag-started populations, so the benchmark runs are unchanged.
- **Ctrl+F1 / Ctrl+F2** (answer 1): F1 and F2 are mapped in the ship context (F1 also in the map context) to two new
  input actions whose handlers act only while Left or Right Ctrl is held: Ctrl+F1 closes the map or opens the pause menu
  (exactly the Esc paths), Ctrl+F2 toggles the overlay. The pause and main menus treat Ctrl+F1 like Esc in
  `NativeOnKeyDown` (`SOLKeyBindings::IsMenuHotkey`). F1 and F2 are reserved keys. Note: Left Ctrl is the default
  Thrust down key, so holding it for the hotkey also thrusts down for that moment. The engine's own non-shipping debug
  bindings put F1 on `viewmode wireframe` and F2 on `viewmode unlit` regardless of Ctrl (the first smoke run turned the
  scene into wireframe); `Config/DefaultInput.ini` re-adds both with `bIgnoreCtrl=True`. Checked in a standalone run
  with real OS key events: Ctrl+F1 paused and resumed, Ctrl+F2 toggled the overlay, the view stayed lit. Not checked
  inside the editor's PIE viewport (whether the editor swallows Ctrl+F1 there).
- **Console reconciliation.** New `sol.Npc.Option <name> <value>` sets any Demo option by its `EDemoOption` name
  (sanitized as on the page) and respawns a live population, so every page option is also reachable from the console
  and `-ExecCmds`; the page picks the change up the next time a menu opens.
- **Verification.** 135/135 tests in the touched groups (MenuState, KeyBindings, InfoText, NpcStatsText, NpcScaleMath,
  NpcAiMath, BindingLabel). `-SOLMenuSmoke=all`: 50 passed, 0 failed, with new phases for the main-menu Demo page
  (`04d_MainDemo`), the pause Demo tab (`09d_Pause_Demo`), Ctrl+F1 pause / resume (game mapping and Slate), a 100k demo
  applied from the pause menu with the overlay (`14_Demo_Overlay`), Ctrl+F2 and Stop NPCs.
## Amendment 6 — 10f (in progress): statistical retune, hot-spot fixes, worst-case ramps, soak

Measured 2026-10-20. Same setup as Amendment 1 (standalone `-game` from the Development editor build, 1920x1080
windowed, `r.vsync 0`, `t.maxfps 0`, `-corelimit=16`, RTX 2080 Ti, default spawn 10,000 km above Earth, orbit lines on).

**Statistical model retune (user decision: "~0.1, verify parity").** `SOLNpcAi::TRANSVERSE_ERROR_FRACTION` 0.5 -> **0.15**
(the pinned 10d tests `Constants` and `HitProbabilityFormula` updated to match; the plan's 10d contract too). Battles at
25-40 km (all statistical in the hybrid) vs bolts, 1M NPCs, 2,000 battle ships, means after spawn:

| Run | Hits per shot | Alive of 2,000 |
|---|---|---|
| Hybrid near (5-20 km, ~90% real bolts) | 46% | 1,096 |
| Bolts far (25-40 km) | 40-53% (two runs) | 1,134-1,213 |
| Statistical far, fraction 0.5 (before) | 13% | 1,671 |
| Statistical far, fraction 0.1 | 63% | 950 |
| **Statistical far, fraction 0.15 (chosen)** | **47%** | **1,129** |

0.1 overshot (more lethal than bolts); 0.15 matches bolts within run-to-run noise, so a battle's attrition no longer
changes as it crosses the 20 km hybrid radius.

**Worst cases.** W1 = 1M, 4,000 battle ships (= the Full budget), hybrid, facing battle 0, every other option at its
default. W2 = 1M with every Demo option at its maximum (Full radius 500 km / budget 20,000, Mid 20,000 km / 500,000,
instanced 2,000 km / 200,000, mesh 20 km / 256, hysteresis 0.5 / 0.5, 20,000 battle ships in 16 battles of 20 km),
battle distances at the default 5-20 km (real bolts).

**Hot spots found (Insights timer statistics, game thread, W2 baseline 24.0 ms frame / 23.4 ms game) and fixed:**

| Hot spot | Before (W2) | Fix |
|---|---|---|
| `BudgetedRadiusM` sorted ~60k distances 4x per frame | 3.3 ms | Count first (no copy when the budget holds), quickselect instead of a full sort, the two independent pairs of budgets on two threads: 0.9 ms |
| `UInstancedStaticMeshComponent::CalcBoundsImpl` on the hull ISMs (all instances, twice per frame) | 1.5 ms | `USOLNpcHullInstances`: ISM subclass with fixed render-space bounds (hulls are culled per instance on the GPU anyway): 0 |
| Idle Full ships (no target in a sparse battle) searched a 125-cell grid box every frame | in the 3.7 ms AI | A ship without a target searches every 8th frame, staggered by index (`NPC_AI_IDLE_SEARCH_INTERVAL_FRAMES`); one that just lost its target searches at once |
| Combat grids built serially through a `TMap` | 2.7 ms serial | Both grids built concurrently; `SOLCombatGrid` now uses an open-addressed cell table (contract and its 11 tests unchanged); respawn timers in a parallel pass |
| `ASOLNpcVisuals` placed 38k CPU ships serially | 2.4 ms | Serial slot pass, then the transforms / texels in a ParallelFor: 0.37 ms serial |
| Tier passes: Mass `ParallelForEachEntityChunk` allocated a job list and copied an execution context per chunk, over ~1,400 chunks | eval+select 2.3 ms | A serial chunk walk records the active chunks' raw arrays, then our own ParallelFor runs over them; Select's archetype moves are staged and pushed to the command buffer serially. 512 KB chunks (`NPC_CHUNK_MEMORY_BYTES`, ~350 chunks at 1M). **Per-chunk temporal cull** (`FSOLNpcChunkWakeFragment::MinWakeKeyM`): a Far chunk whose minimum wake key exceeds its group's clock is skipped by both passes without touching its ships; a demotion to Far marks the group so its chunks are re-evaluated once. Eval+select 1.6 ms at W2 (60k awake candidates), 0.4-0.9 at W1 |
| Collect, Full AI gather and scatter: serial chunk walks | 1.9 ms | Chunk views with fixed offsets (chunk order, so still deterministic), filled in a ParallelFor; dead ships are kept in the CPU list with `bHidden`: 0.7 ms |

**Spawn hitch.** Before: a 1M spawn after a 1M population was one ~800 ms frame (despawn 460 ms through 1M handle
lookups, entity creation 350 ms). Now the despawn destroys whole chunks of every NPC archetype
(`BatchDestroyEntityChunks`, ~5 ms), the per-index class hash runs in the rails ParallelFor, and the entities are
created over the next frames in slices of `NPC_SPAWN_ENTITIES_PER_FRAME` = 25,000 (`ContinueSpawn`, one
`FMassEntityView` per entity). The GPU far layer draws every rail from the first frame. Measured: 1M spawn = 40 slices,
max single frame 22 ms (fresh) / 55-56 ms (the ramp's 500k -> 1M transition, which also despawns 500k and builds 1M
rails in that first frame), 0.5-0.75 s until all entities exist.

**Worst-case ramps (`-SOLNpcRamp`, 5 s settle + 10 s measure per stage):**

| Run | Stage | Mean ms | p95 | p99 | max | fps | NPC mean / p95 ms | Full / Mid | Result |
|---|---|---|---|---|---|---|---|---|---|
| W1 | 10,000 | 6.75 | 8.09 | 9.19 | 13.97 | 148 | 1.16 / 1.45 | 4,000 / 1 | PASS |
| W1 | 100,000 | 6.83 | 8.14 | 9.11 | 19.03 | 146 | 1.22 / 1.49 | 4,000 / 7 | PASS |
| W1 | 250,000 | 6.94 | 8.24 | 9.05 | 10.17 | 144 | 1.29 / 1.58 | 4,000 / 13 | PASS |
| W1 | 500,000 | 7.02 | 8.27 | 9.25 | 10.55 | 142 | 1.41 / 1.70 | 4,000 / 26 | PASS |
| W1 | 1,000,000 | 7.39 | 8.75 | 10.05 | 11.25 | 135 | 1.64 / 1.99 | 4,000 / 66 | PASS |
| W2 | 10,000 | 9.52 | 11.22 | 11.99 | 12.74 | 105 | 2.56 / 3.13 | 10,000 / 0 | PASS |
| W2 | 100,000 | 13.85 | 16.36 | 17.66 | 19.81 | 72 | 4.84 / 5.97 | 20,000 / 1,473 | PASS |
| W2 | 250,000 | 16.77 | 19.14 | 20.41 | 21.31 | 60 | 5.37 / 6.27 | 20,000 / 4,403 | FAIL |
| W2 | 500,000 | 16.98 | 19.23 | 20.48 | 21.19 | 59 | 5.69 / 6.76 | 20,000 / 9,062 | FAIL |
| W2 | 1,000,000 | 18.09 | 20.41 | 21.93 | 23.27 | 55 | 6.57 / 7.93 | 20,000 / 18,684 | FAIL |

W1 (the 10d follow-up: 4,000 instanced hulls + Full AI, 7.3 ms) is now 7.4 ms mean at 1M with the chunk walk, budget and
bounds fixes absorbing the growth; memory 3.7 GB. **W2 does not yet meet 60 fps** from 250k up: at 1M the game thread is
~17 ms, of which the NPC update is ~6.6 ms (Full AI with 20,000 Full ships and ~23,000 live bolts ~4 ms), the NPC
visuals ~2.5 ms (the engine's `BatchUpdateInstancesTransforms` for ~20,000 hulls with previous transforms ~1 ms, not
ours to parallelize), the hull ISM proxy updates ~1.5 ms, and the rest of the game ~6 ms. GPU 5.2 ms, render thread
5.8 ms. W2 is 5x the default Full budget and 10x the default instanced budget.

**Soak** (W1, 1M, 5.5 min, 164 two-second windows): mean frame 7.36 ms; the NPC update stays 1.5-1.6 ms; alive ~2,160-
2,200 of 4,000 and ~3,300 live bolts throughout (respawn keeps the population steady). Memory grows 3.27 -> 3.89 GB over
the first ~160 s, then stays flat at 3.88-3.92 GB for the remaining ~3 min: a warm-up plateau, not a leak. **Spikes:**
32 of 161 windows hold one frame over 16.67 ms. None comes from the NPC update (its per-window phase maxima stay
<= 3 ms). An Insights capture of the same scene shows (a) four 28-33 ms frames every ~60 s: garbage collection
(`ConditionalCollectGarbage` up to 39 ms, ~15 ms of it the editor-only Python plugin's GC in this editor-build `-game`
run), and (b) occasional 40-160 ms frames where the game thread waits on the render thread
(`UpdateAllPrimitiveSceneInfos` -> `LaunchVisibilityTasks` waiting ~350 ms, plus `DeleteRHIResources` ~250 ms on the RHI
thread) while the workers are idle: an engine render-side stall, likely on-demand PSO / resource work (no PSO cache in an
editor-build `-game`); not yet root-caused, to be re-checked in the packaged run.

**Not done yet (10f continues):** W2 to 60 fps (next levers: GPU-driven hull drawing instead of per-instance CPU ISM
updates, a cheaper Full-AI step at 20k ships, the remaining serial combat-grid build ~1.3 ms), the render-thread spike
root cause, the belt ISM's own `CalcBoundsImpl` (~0.5 ms/frame, existing tech debt), the packaged Development
confirmation run, and the adversarial review of these changes (10g). Profiling tooling used: `-trace=cpu,frame` plus
`UnrealInsights.exe -NoUI -AutoQuit -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics <csv>
-threads=GameThread"` (and `ExportTimingEvents -threads=* -startTime= -endTime=` for single frames); `NpcPhaseMax` now
logs the period's longest frame (`frame=`).

## Amendment 7 — 10f: adversarial review fixes

An adversarial review (opus) of the 10f changes found the issues below; all are fixed. User decision: the Demo option
maxima stay as they are (the all-maximum worst case remains a documented miss, Amendment 6).

| # | Finding | Fix |
|---|---|---|
| 1 (High) | A spawn slice appended to a chunk whose `MinWakeKeyM` was already computed: the chunk cull could skip the new ships | `ContinueSpawn` marks every group it adds ships to dirty, so Evaluate visits all of the group's chunks once. The cull rule is now the pure `SOLNpcScale::IsFarChunkActive`, and the clearing rule is `ClearVisitedWakeDirty` (tests `NpcScaleMath.FarChunkCull`, including the spawn case) |
| 9 | The dirty flags were cleared for every group after Evaluate | Cleared only for the candidate groups Evaluate visited; a far group stays dirty until it becomes a candidate |
| 2 | The hull ISMs kept their size and rewrote every instance forever, even after Stop | `UploadHulls` writes only `[0, max(used, lastUsed))` and nothing when both are 0. After 300 frames using under a quarter of the instances, the ISM is rebuilt at twice the use (floor 1,024). With no population, the rail texture is released |
| 3 | A mesh-pool slot changing ship produced a motion vector from the old owner | `FSOLNpcCpuShip::NpcIndex` keys an owner per pool slot; on change the slot's scene velocity is reset |
| 4 | Lane wake bound too low once the endpoints drift (the rail's rate is per nominal length) | `SOLNpcScale::LaneShipSpeedBoundMps`: lane speed times current over nominal length, plus the endpoints' relative speed times (1 + lane radius / length). Re-evaluated every frame from the current endpoint positions and velocities, and the ships' possible travel is integrated as bound times the rail step (it only grows, so the wake clock stays a valid bound). Test `NpcScaleMath.LaneSpeedBound` checks it numerically against moving endpoints |
| 5 | A Mid ship in the hysteresis band (at or beyond the Mid radius) was drawn twice: by the GPU from its rail and by the CPU | The CPU point layer skips Point-tier CPU ships whose bare-rail distance is at or beyond the Mid radius (the GPU's own test; `FSOLNpcCpuShip::RailDistanceM`). Dead ships cannot be Mid or Far any more (finding 10), so the GPU never draws a dead ship |
| 6 | Options allowed Mid budget < Full budget, and the budgeted Full radius could exceed the budgeted Mid radius | `SanitizeOptions` rule: Mid budget raised to the Full budget (`EOptionIssue::MidBudgetRaised`); the subsystem clamps the budgeted Full radius to the budgeted Mid radius |
| 7 | A Full ship demoted to Mid was pulled to the 2 km Mid leash at once (class leash 3 km) | `NPC_MID_DEVIATION_LEASH_M` = 3,000 m (at least every class leash; test `NpcAiMath.MidLeashCoversClasses`), and the Mid clamp is `max(leash, |old deviation|)`, so only the spring pulls a ship in |
| 8 | The GPU evaluates rails in float from the population epoch: under warp, phases lose metres per ulp | `SOLNpcRail::AdvancedToEpoch` re-bases each rail's lane parameter or cluster phase in double and wraps it. `ASOLNpcVisuals` re-uploads the rail texture re-based to the current rail time once it runs `NPC_GPU_RAIL_EPOCH_S` = 10,000 sim s from the GPU epoch, at most every `NPC_GPU_RAIL_EPOCH_MIN_INTERVAL_S` = 2 real s (a parallel fill plus one texture upload). The shader receives the time since that epoch. Measured (test `NpcRail.GpuFloatPrecision`, low Earth orbit): 2,383 m float error 4e6 s after the epoch without re-basing, 6.6 m within one epoch window |
| 10 | A dead ship demoted to Far never ran its respawn timer | Only Full ships fight, so a dead ship leaving the Full tier respawns at once at its rail slot (Select pass). The Mid processor no longer has a respawn path |
| 11 | The archetype move order depended on the parallel chunk order (the Amendment 6 determinism claim was false) | Select sorts its moves by entity before writing them to the command buffer |
| 12 | Bolt render cap took the first 8,192 in pool order; pool overflow was silent | The nearest 8,192 bolts within 20 km are drawn (selection of the cap-th distance); shots dropped at the 65,536 pool cap are counted (`dropped=` in the `NpcCombat` log line) |
| 13 | A `new FUpdateTextureRegion2D` was allocated every frame for the group and CPU textures | One region per upload-ring slot, owned by the actor |
| 14 | Stop / Quit to menu left the overlay up, and `StartDemo` wrote `sol.Npc.CombatModel` (ECVF_SetByConsole), which persisted across PIE sessions | The Demo page's combat model lives in the subsystem (session-only). A console change of the variable still overrides it, detected by value change, and the game never writes the variable. `Stop` hides the overlay and resets the model to the console's value |
| 15 | Style | NPC constants regrouped under headings (population, renderer, statistics, Full AI and combat); lines over 120 columns rewrapped (changed files and the NPC tests); stale tier-processor comments fixed; Demo value strings moved to `SOLMenuText`; `PixelWorldCm` uses the viewport width (horizontal FOV, any aspect) instead of a 16:9 assumption |
| 16 | Docs | ARCHITECTURE: `Npc` row (10a-10f), `Npc` in the module graph with Menu -> Npc, a Part 10 note that rails are also evaluated in the shader (so a rail change touches both) |
| 17 | Tests | `CombatGrid.GrowthDuringBuild`, `ClearThenRegrow`, `DenseCellBlock` (rehash paths), `NpcScaleMath.FarChunkCull`, `SelectNth`, `LaneSpeedBound`, the Mid-budget sanitize case, `NpcRail.EpochMatches`, `NpcRail.GpuFloatPrecision` |

Verified: `SOLTest.Npc*`, `CombatGrid`, `MenuState`, `InfoText`, `KeyBindings` 146/146 in the editor. Default-budget
ramp (1M, 4,000 Full battle ships) after the fixes:

| Stage | Mean ms | p95 | p99 | max | fps | NPC mean / p95 ms | Result |
|---|---|---|---|---|---|---|---|
| 10,000 | 6.59 | 7.57 | 8.62 | 11.93 | 152 | 1.28 / 1.52 | PASS |
| 100,000 | 6.97 | 8.35 | 9.49 | 19.55 | 143 | 1.40 / 1.71 | PASS |
| 250,000 | 6.95 | 8.08 | 9.13 | 10.08 | 144 | 1.45 / 1.70 | PASS |
| 500,000 | 7.17 | 8.21 | 9.02 | 10.02 | 140 | 1.61 / 1.90 | PASS |
| 1,000,000 | 7.32 | 8.40 | 9.30 | 14.31 | 137 | 1.81 / 2.14 | PASS |

No regression against Amendment 6 (1M p95 8.75 ms then). The NPC update is ~0.17 ms slower at 1M: the sorted moves,
the respawn check in Select, and the bare-rail distance in Collect. The 1M spawn's largest slice is 45 ms (the ramp's
500k -> 1M step, which also despawns and builds rails), and no bolts were dropped.

## 6. Revision history

- 2026-10-15: initial SDD from the `grill-me` session (decisions 1-8), with design proposals 9-12 and open questions.
- 2026-10-15: open questions 1-10 answered by the user (section 5); proposals 9-12 confirmed. Question 11 stays open
  until the 10d measurements.
- 2026-10-15: Amendment 1 (10b: motion seam, temporal cull, measurements) and Amendment 2 (10c: material-driven
  point layers instead of Niagara, hull tiers, GPU measurements, final ramp).
- 2026-10-15: Amendment 3 (10d: Full AI, NPC combat, the three combat models measured; model choice pending).
- 2026-10-20: open question 11 answered (hybrid; Amendment 4). Amendment 5 (10e: Demo page, stats overlay, Ctrl+F1 /
  Ctrl+F2).
- 2026-10-20: Amendment 6 (10f in progress: statistical fraction 0.15 with parity measured, hot-spot fixes, time-sliced
  spawn, worst-case ramps W1 PASS / W2 FAIL from 250k, soak).
- 2026-10-21: Amendment 7 (10f adversarial review fixes; Demo option maxima kept by user decision).
