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
