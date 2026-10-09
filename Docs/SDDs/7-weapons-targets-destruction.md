<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 7 — Weapons, target drops, destruction VFX and audio

Ticket `#7` (roadmap Part 6). Plan of record: [`Docs/Plans/7-weapons-targets-destruction-plan.md`](../Plans/7-weapons-targets-destruction-plan.md).
Refines the "Combat" row of [SDD 1](1-solar-system-architecture.md) and must not contradict it. All decisions below were
resolved with the user in a `grill-me` session on 2026-10-09.

## 1. Problem

The ship can fly but cannot shoot. Add a forward-firing gun, targets the player drops, health/shield damage, destruction
effects and sound, at a scale that does not block the ~1M-entity goal (Mass for everything, no Actor per datum).

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Weapon | Fixed forward-firing pulse guns, aimed at a crosshair point along the camera ray (bolts converge on it); left mouse fires. No turret, no lock-on or homing (later part). |
| 2 | Numbers (starting values) | 8 shots/s, 1.5 km/s muzzle speed added to the ship's velocity (velocity inherited), 10 damage, bolt lifetime 3.5 s. Target: 100 shield + 100 health, shield regenerates after 3 s without hits. All constants in `SOLConstants.h`, listed in `GAME_MECHANICS.md`. Tunable. |
| 3 | Bolts | Mass entities (fragments: position/velocity in universe metres as doubles, remaining lifetime, owner); one processor integrates and sweeps each bolt segment (previous to current position, a swept sphere) against targets through a spatial grid, never physics line traces. Rendered by one Niagara system fed from a packed array (GPU bolt renderer), not an Actor or component per bolt. |
| 4 | Targets | The player drops a target with a key press; it spawns a set distance ahead of the ship, at rest in the current reference frame. Targets are Mass entities (health and shield fragments, position, radius), rendered as instanced meshes; pooled, with a cap. |
| 5 | Damage and hit feedback | Shield absorbs first, then health. A hit flashes the target (per-instance custom data) and spawns a pooled Niagara impact effect. |
| 6 | Destruction | When health reaches 0: pooled Niagara explosion plus randomised debris mesh particles (random every time). No Chaos Geometry Collection (cost per kill); a hybrid by distance is a possible later upgrade. |
| 7 | Asteroids and rings | Bolts also collide with belt asteroids and ring rocks (sweep against the minor-body positions via the same spatial grid), but are only absorbed with a spark puff; asteroids are on rails and unharmed. Ship-vs-asteroid collision and destructible asteroids stay in [`Docs/ToDo/asteroid-ring-collision.md`](../ToDo/asteroid-ring-collision.md). |
| 8 | Audio | Placeholder synthesized sounds (MetaSound or generated wave assets) for shot, hit, shield break and explosion, played via pooled one-shot audio; real audio arrives in the Part 8 art and audio pass. Asset paths in `SOLConstants.h`. |
| 9 | Origin rebasing | Bolts, targets and the spatial grid live in universe coordinates (double), converted to render cm with the frame's origin snapshot like the other visuals; no float truncation of universe positions. |

## 3. Design

### 3.1 Pure logic (test-first, `Source/SOLTest/Combat/`)

- `SOLBoltMath`: advance a bolt (position + (shipVelocity + muzzleVelocity) * dt), muzzle velocity from aim direction,
  crosshair convergence direction, lifetime expiry, swept-sphere vs sphere hit (earliest time of impact in [0,1],
  tunnelling-safe at 1.5 km/s plus warp).
- `SOLDamageMath`: shield-first damage, shield regeneration delay and rate, death on health <= 0, overkill clamp,
  NaN/negative damage rejected.
- `SOLCombatGrid`: uniform spatial hash over double-precision positions (cell insert/query, swept-segment cell walk),
  so a bolt tests only nearby candidates.
- `SOLTargetDrop`: drop position ahead of the ship, cap and oldest-eviction policy.

### 3.2 Mass and rendering

Fragments in `SOLCombatFragments.h`; `USOLBoltProcessor` (move, sweep, apply damage via the command buffer), a combat
subsystem owning pools and the grid, `ASOLTargetVisuals` (instanced target meshes, hit-flash custom data), a Niagara
bolt/impact/explosion/debris set under `/Game/SOL/Combat/`, and a firing input action in the Enhanced Input mapping.
Per-frame order slots after the ship flight processor and before the visuals update (documented in `ARCHITECTURE.md`).

### 3.3 Verification

Automation tests for all pure logic (written by a separate agent from the contracts), a PIE smoke check (drop a target, fire,
shield and health fall, explosion, bolts absorbed by an asteroid) with screenshots, `stat` evidence for 10,000 live bolts and
1,000 targets, then the adversarial opus review.

## 4. Open questions

None deferred. Ship stats beyond the gun (energy, heat, ammo) are intentionally out of scope.

## 5. Amendments

### Amendment 1 — 7b implementation deviations (2026-10-09)

Flagged to the user because decision 3 chose Mass entities for bolts:

- **Bolts and targets are pooled structure-of-arrays inside `USOLCombatSubsystem`, not Mass entities** (no `USOLBoltProcessor`, no `SOLCombatFragments.h`; shared types are in `SOLCombatTypes.h`). Reasons: a bolt hit writes into a target (random-access cross-archetype writes from a processor), bolts spawn and die 8 times a second (archetype churn or a pool the processor must still walk), and the Niagara bolt renderer wants one packed position array, which these arrays already are. Still reserved once, allocation-free per frame, parallel sweep. Reversible if the user prefers Mass.
- **Drop target is G** (T is already "select target under the reticle").
- **Update order:** runs on `USOLAnchorSubsystem::OnUniverseUpdated`, after every `OnBodiesUpdated` listener, so order is guaranteed.
- **Frames:** bolts and targets are stored relative to the player's reference body at fire/drop time; collision sweeps in a ship-centred frame. Bolts pass through the GPU-only dense ring layer (no CPU positions); belt asteroids and the ring pool's active rocks absorb them.
- Values picked: shield regen 25/s, drop cooldown 0.25 s, target radius 10 m, bolt hit radius 0.5 m.

### Amendment 2 — 7c visuals findings (2026-10-09)

- **`ASOLCombatVisuals`** (one actor, no tick, redraws on `OnCombatUpdated`): `NS_SOLBolts` fed from the packed bolt arrays,
  one `UInstancedStaticMeshComponent` for all target slots (custom data: shield, health, hit flash), and fixed pools of
  pre-registered effect components (impact 32, shield break 8, spark 32, explosion 8). Verification hook `-SOLCombatDemo`.
- **Bolts run as a CPU sim, not GPU.** The GPU variant never drew anything in game; the CPU one does. Indexing the arrays by
  `ExecIndex()` inside the array-select dynamic input made the whole system fail to activate (no log line), so the bolt
  slot index is `Particles.UniqueID` (0..N-1 because all particles spawn in one burst). Cost of 20,000 always-simulated CPU
  particles is unmeasured; profile it in 7f, and revisit GPU (with persistent IDs) if it shows up.
- **Niagara user parameters:** a `User.*` read only from an HLSL expression, or linked straight into a `Particles.*`
  attribute in a Set Parameters module, reads as zero at runtime. Fix used: link the user parameters once into `Emitter.*`
  attributes in the emitter update script, and read those (bolts, impact sparks, spark). Linking `User.Tint` into an
  `Emitter.*` attribute in the impact's Flash emitter made the whole system fail to activate, so Flash reads `User.Tint`
  directly (valid because the Sparks emitter of the same system links it).
- **Bolt size:** a 70 cm streak is sub-pixel past ~100 m and nearly end-on from the chase camera, so sprite width and
  length are `max(base, camera distance * 0.004 / 0.05)` (`DistanceToCamera` dynamic input), about 3 px wide at any range.

### Amendment 3 — 7d audio (2026-10-09)

- **`ASOLCombatAudio`** (separate actor in `Combat/`, spawned next to the visuals, no tick, plays on `OnCombatUpdated`).
  Seven generated mono 44.1 kHz WAVs (Python-synthesized sweeps and noise bursts, 0.18-2 s) imported as `USoundWave`s under
  `/Game/SOL/Combat/Audio/` (`S_SOLShot`, `S_SOLShieldHit`, `S_SOLHullHit`, `S_SOLShieldBreak`, `S_SOLExplosion`, `S_SOLSpark`,
  `S_SOLTargetDrop`). No MetaSound, no attenuation or concurrency assets: both attenuations are transient objects built
  from `SOLConstants.h`, and concurrency is a fixed voice pool per kind (oldest reused, 21 voices total) plus a per-kind
  minimum interval (`SOLCombatAudioRules::MayPlay`, tested in `SOLTest.CombatAudio.*`). All tuning is the
  `SOL::COMBAT_SOUNDS` table. `TargetEvicted` is silent. A hit picks the shield ping or hull thud by the same
  shield-after-hit test as the impact tint.
- **3D placement:** each voice starts at the event's universe position (render-origin snapshot) and rides with the event's
  frame velocity, or the ship's own velocity for player shots, until its sound ends, so ship motion and origin rebases do
  not leave it behind.
- **Verified** with `-SOLCombatDemo -SOLSmokeShot=22` (4 cycles): shot 72 (2 dropped by the interval), shield hit 35,
  hull hit 33, shield break 3, explosion 3, target drop 4; spark 0 because the demo has no rock in the line of fire
  (covered by the event mapping test). No LogSOL or LogAudio warnings or errors. Audibility and mix not checked by ear.

### Amendment 4 — 7f adversarial-review fixes and load measurements (2026-10-09)

Labels: sub-parts are 7a-7f everywhere (the plan's labels); the pushed commits say "Part 6x" (same letters).

- **Effects and sounds follow their frame under warp.** `FSOLCombatEvent` now carries `FrameBody`, `FrameOffsetM` and
  `RelativeVelocityMps`; effects and voices recompute their position every update as the body's current position plus
  the offset (plus relative motion for their age, `SOLCombatRules::EventPositionNowM`) instead of extrapolating with a
  velocity in real seconds (which drifted by the warp factor). The player's own shot sounds keep their offset from the
  ship. Both age with the subsystem's clamped delta (`GetLastUpdateDeltaS`), not the world delta.
- **Hit point at frame end (found during verification).** A hit was placed at the bolt's universe point at the hit time
  t, but the reference frame keeps moving for the rest of the frame (Earth: 30 km/s), so impacts landed 15-20 m in front
  of the target. `SOLCombatRules::HitPointAtFrameEndM` now takes the contact offset at t and applies it to the struck
  object's end-of-frame position; measured distance from the target centre is now exactly 10.5 m (radius + bolt radius).
- **Bolt renderer cost.** Burst sized by `COMBAT_BOLT_RENDER_CAP` = 2,048 (player keeps ~28 alive; `COMBAT_MAX_BOLTS`
  stays 20,000 for the simulation). The component is paused and hidden while no bolt is live (not deactivated: the
  slot index relies on the burst's particle ids). Bolts past the cap fly and hit undrawn, logged once.
- **Targets ISM** gets instances only up to the high-water mark of used slots (geometric growth, never per frame) and
  is hidden while no target is active.
- **Ring reassignment** is detected exactly: `FSOLMinorBodyRenderFragment::Generation` (uint16, fragment stays 16 bytes)
  is bumped by `USOLRingSubsystem::AssignGroup`; the combat snapshot stores it and treats a change as a teleport.
- **Range and first segment.** Bolts more than `COMBAT_BOLT_MAX_RANGE_M` (20 km) from the ship expire (keeps the
  broad-phase box small after a jump). New shots are fired before the sweep and swept from the muzzle in their first
  frame. A bolt whose target an earlier bolt destroyed is re-swept on the game thread (sweeps skip inactive targets).
- **Pure rules extracted** to `SOLCombatRules` (sweep start/teleport, previous-position validity, bolt range, target-hit
  resolution with the double-kill guard, event trim/cap, hit point, event position, drawn bolt count), tested in
  `SOLTest.CombatRules.*`; `SOLTest.CombatGrid.FallbackKeepsBuffer` covers the grid's all-ids fallback, which now appends
  into the caller's reserved scratch instead of copy-assigning (that could reallocate on a worker thread); sweep
  scratch is reserved to the grid's reserved entry count.
- **Not fixed (CLAUDE.md tech debt):** the minor-body snapshot still scans every minor body each frame a bolt is live
  (measured below); a non-body M-lock reference gives bolts/targets the anchor's warp carry (no `ISOLTargetable`
  implementer exists, so it cannot happen today; documented in `GAME_MECHANICS.md`).
- **Demo hook:** `-SOLCombatDemo` tracks its own drop's slot (`GetLastDroppedTargetSlot`/`GetTargetDropCount`), tells a
  kill from a loss (`GetTargetsDestroyedCount`) and gives up after 20 s. New `-SOLCombatStress=<bolts>` keeps that many
  extra bolts alive in a 25° forward cone.

**Measurements** (standalone `-game`, 1280x720, D3D12, ~230 fps; temporary `FPlatformTime` instrumentation averaged over
240 frames, since removed; GPU from `ProfileGPU`):

| Scenario | Combat update (sim) | Visuals + audio | GPU frame |
|---|---|---|---|
| Idle, no bolts | 0.003 ms | 0.006 ms | — |
| Demo, 1-3 live bolts | ~0.42 ms | ~0.06 ms | 2.77 ms |
| `-SOLCombatStress=10000`, ~9,700 live bolts (2,048 drawn) | ~1.0 ms | ~0.10 ms | 2.84 ms |

The ~0.4 ms floor whenever any bolt is live is the minor-body snapshot scan of 19,268 entities (belt 9,268 + ring pool
10,000), about 20 ns each; it is linear in the minor-body count (≈20 ms at 1M), hence the tech-debt entry. The bolts
themselves cost ~0.6 ms for ~9,700 (advance, parallel sweep, compaction). Drawing 2,048 bolts adds ~0.07 ms GPU.
Screenshot diff (stress vs baseline at the same moment) confirmed the bolt cloud renders after the pause/unpause.

