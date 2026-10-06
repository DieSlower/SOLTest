<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Moons, Dwarf Planets, Asteroid Belt, Rings — Implementation Plan

**Goal:** Deliver Part 5 (issue #6): real moons/dwarf planets in the existing body
registry, a realistically-sparse-but-clustered Mass-native asteroid belt (~1,000 real
+ family-clustered procedural fill), and all four gas giants' rings with real
moon-carved gaps, rendered with a near-ISM/far-Niagara LOD split.

**SDD of record:** [`Docs/SDDs/6-moons-asteroids-rings.md`](../SDDs/6-moons-asteroids-rings.md).
Keep it in sync if the design shifts.

## Global constraints

- Follow `CLAUDE.md` and `Docs/STYLE_GUIDE.md`.
- Same loop as prior parts: tests-first for pure logic (a separate subagent, from the
  contract only), implement to green, adversarial review, fix, re-verify, docs,
  commit (`Issue #6: ...`), no push.
- The Unreal Editor must be closed while building; reopen for the screenshot checks.
- This is a large ticket (new Mass architecture, two procedural generators, dual-LOD
  rendering) — each sub-part gets its own dispatch, kept small enough to verify
  independently, per the project's usual rhythm.

## Steps

- [x] **5a — Moons and dwarf planets.** Pure data addition to `FSOLBodyRegistry`'s
  solar-system table (SDD 6 §3.1): real orbital elements, radii, and rotation data
  (reusing issue #12) for the Moon, Phobos/Deimos, the Galilean moons, Saturn's major
  moons (at least Titan/Enceladus/Mimas, needed by 5d's ring gaps), Uranus's and
  Neptune's major moons, and the dwarf planets. No new C++ types — extends
  `BodyRegistryTest.cpp`'s existing table-driven tests. Adversarial review (data
  fidelity, registry/visuals behave correctly with a larger, deeper parent-child
  hierarchy), commit.
  **Built:** 5 dwarf planets (Ceres, Pluto, Eris, Makemake, Haumea) + 20 moons added,
  registry total 9 → 34. Adversarial review found two real issues, fixed before commit
  and documented in SDD 6 Amendment 1: (1) moon elements needed a host-equator→ecliptic
  tilt conversion (new file-local `ESOLElementFrame`/`HostEquatorToEcliptic()` in
  `SOLBodyRegistry.cpp`, not a public API change); (2) child-body orbital velocity needed
  its own Kepler-consistent GM, decoupled from the parent's physical GM
  (`FSOLBodyDef::OrbitGM`, `ChildOrbitGM()` — fixes a ~72 m/s/0.5% speed error at Mimas
  that fed ship spawn/jump-arrival/collision code). 340 automation tests passing (was
  332). `Docs/ARCHITECTURE.md` and `Docs/ToDo/accurate-pole-directions.md` updated.
- [x] **5b — Asteroid belt pure logic.** `Source/SOLTest/MinorBodies/SOLAsteroidBelt.h/.cpp`
  per SDD 6 §3.2 (Amendment 2): the real ~500-asteroid table (fetched from a real public
  catalog by a dispatched subagent, baked into a compiled C++ array literal — not
  hand-typed, not a UDataTable), and the deterministic family-cluster generator (cluster
  placement across real belt element ranges minus Kirkwood gap bands, per-cluster
  procedural member scatter). Fully unit-tested: TDD as usual, tests from the contract
  alone. Adversarial review (generation correctness, determinism, gap exclusions
  actually exclude, real-data fidelity of the fetched table), fix, commit.
  **Built:** real data fetched live from JPL SBDB (500 of the largest main-belt
  asteroids, Ceres excluded since it's already a registry dwarf planet), real names
  folded with their catalog number (e.g. "52 Europa") to avoid colliding with existing
  moon names. Family members get an exact (not approximate) cohesion guarantee: each
  shares its cluster center's semi-major axis and mean motion exactly, via a
  state-vector-to-orbital-elements conversion, so separation is an exact bounded
  periodic function of time. Adversarial review found no correctness bugs; fixed
  documentation overclaims, the real-asteroid name collisions, a duplicated J2000
  constant, and skewed the cluster eccentricity/inclination draw toward low values to
  better match the real belt (was uniform). 15 new automation tests, 355 total (was
  340). `SOLBodyRegistry.cpp`'s Sun-GM literal duplication noted as tech debt in
  `CLAUDE.md` rather than fixed here (cross-file cleanup, not blocking).
- [x] **5c — Asteroid belt Mass architecture and rendering.**
  `FSOLMinorBodyOrbitFragment`/`FSOLMinorBodyAppearanceFragment`,
  `USOLMinorBodyOrbitProcessor` (SDD 6 §3.2 — mirrors `USOLShipFlightProcessor`'s
  chunk-parallel structure, far simpler: no gravity/collision/control), and
  `ASOLAsteroidBeltVisuals` (ISM, bulk per-frame transform updates from the
  processor's output — this project's first large *moving* GPU-instanced
  population). Screenshot verification. Adversarial review with explicit focus on
  per-frame bulk-transform-update cost at ~51,000 instances (the performance
  checklist's new concern this part introduces), fix, commit.
  **Built:** actual belt is ~9,268 instances (500 real + ~8,768 fill), not ~51,000 —
  see SDD 6 Amendment 3. Two adversarial-review rounds found and fixed: a real
  performance bug (`bMarkRenderStateDirty=true` rebuilding both ISM scene proxies
  from scratch every frame — fixed to `false`), a pre-existing latent teardown-crash
  hazard found in the already-shipped `USOLShipSubsystem::Deinitialize` (fixed there
  too, same pattern as the new subsystem's `PreDeinitialize`), a data-layout split
  (`FSOLMinorBodyOrbitFragment` vs. the new `FSOLMinorBodyRenderFragment`), and a
  style-guide UObject-access-in-loop fix. 360 automation tests passing (was 355).
  Headless `-SOLSmokeFlight` re-verified clean (9/9, no crash) after every fix round.
  **PIE/profiling verification done (SDD 6 Amendment 5)**: 87.75 FPS average over
  2,239 frames / 25.51s with the belt's Mass processor and ISM bulk update running the
  whole time, 0% missed syncs at 30/60 FPS, 0 hitches/min. Also found and fixed a real
  bug while there: `M_SOLBody` (the material the belt's ISM components render with) was
  missing its "Used with Instanced Static Meshes" flag, so the belt had been silently
  rendering with the engine's default material since 5c shipped — fixed and resaved.
  5c is now fully verified.
- [x] **5d — Ring pure logic.** `Source/SOLTest/MinorBodies/SOLPlanetRing.h/.cpp` per
  SDD 6 §3.3: per-ring parameters for all four gas giants, gap-carving against every
  moon orbit that falls within a ring's radius range, reusing 5b's gap-exclusion
  math. Unit-tested (TDD), adversarial review, fix, commit.
  **Built:** scope extension (user-directed, SDD 6 Amendment 4) — added Saturn's real
  shepherd moons Pan and Daphnis to the registry (36 bodies total, was 34) since no
  previously-registered moon actually orbits within any ring system; built a hybrid
  gap mechanism (fixed cited data for Saturn's resonance-based Cassini Division, a
  generic per-moon-semi-major-axis band exclusion for Pan/Daphnis's real Encke/Keeler
  gaps, with real per-moon gap widths after review caught a uniform-width bug making
  Daphnis's gap ~7x too wide). 5 new `SOLTest.PlanetRing` tests + extended
  `BodyRegistry` tests, 365 automation tests passing (was 360).
- [ ] **5e — Ring rendering (near/far LOD).** Grilled before implementation (SDD 6
  Amendment 6) — scope grew from the original two-sentence sketch into a genuinely
  bigger feature: Saturn's ring alone spans ~70,000 km radially, so a whole-ring
  flyable-density Mass population is infeasible; the near field is a **player-streamed
  local patch** (re-centers as the player moves) rather than a static whole-ring spawn
  like the belt. Collision stays deferred (decision 9 re-confirmed; `Docs/ToDo/
  asteroid-ring-collision.md` updated). Sub-steps, each independently verifiable per
  the project's usual rhythm:
  - [x] **5e-i — Shared math + pure logic.** Promoted `SOLAsteroidBelt.cpp`'s file-local
    `BeltStateToElements` to `SOLKepler::StateToElements(state, gm)` (the documented
    inverse of `ElementsToState`) and `BeltMeanMotionDegPerCy` to
    `SOLKepler::MeanMotionDegPerCy`; re-pointed 5b's belt code at the shared versions
    (its existing tests pass unchanged) and added direct tests for both. New
    `Source/SOLTest/MinorBodies/SOLRingPatch.h/.cpp`: cell-grid indexing
    (`ComputeActiveCell`), deterministic per-cell rock generation respecting
    `SOLPlanetRing::AllGapBandsM` (`GenerateCellRocks`), and the near/far cross-fade
    alpha (`ComputeNearFieldAlpha`, `SOLMapBodyLod`-style). Full contract-only TDD, a
    dispatched subagent wrote the tests from the header alone.
    **Built:** adversarial review (hand-traced the RV2COE math and the gap/interval
    logic, not just "tests are green") found two real design bugs before any caller
    existed — see SDD 6 Amendment 7: (1) generated rocks stored their "right now" mean
    longitude directly as the J2000-epoch value, which would place them at an
    essentially arbitrary point on their orbit once the sim clock advances past J2000;
    (2) the cell grid was fixed in a non-rotating frame, but ring rocks orbit at tens of
    km/s, so cells would need reassigning dozens of times per second even with a
    stationary player, directly contradicting the "holds its place" design intent.
    Fixed with a co-rotating reference frame (`ComputeActiveCell`/`GenerateCellRocks`
    both gained `planetGM`/`secondsSinceJ2000` parameters) and an epoch-rollback
    correction on each rock's `L0Deg`; a new `GenerateEpochRollback` test explicitly
    proves the un-rolled-back value would land outside the cell. Also fixed: a
    duplicated element-packing helper (promoted `SOLKepler::KeplerElementsToSecular`,
    shared by the belt and the ring patch), weak per-cell seed mixing (finalized through
    `MurmurFinalize32`), a NaN-admission hazard in `ComputeActiveCell`, stale test
    comments from a now-superseded indexing convention, overclaimed degenerate-fallback
    test coverage (added true exact-degenerate cases), and tightened round-trip
    tolerances (1e-8/1e-7 → 1e-11/1e-10, verified empirically). One genuine test
    arithmetic bug was also found and fixed during the main session's own RED/GREEN
    verification (an exact-cell-boundary coincidence in a "1.5 cells away" displacement
    test). `GenerateCellRocks`'s per-call heap allocation and the ring-normal basis's
    tie-sensitivity were deliberately deferred to 5e-ii's design rather than guessed at
    now. 382 automation tests passing (was 365 before 5e).
  - [x] **5e-ii — Pooled Mass architecture.** New `USOLRingSubsystem` (mirrors
    `USOLMinorBodySubsystem`'s shape) owning a **fixed-size entity pool per ring**
    (reserved once at world begin-play, grouped into `ROCKS_PER_CELL`-sized chunks, one
    per cell the pool can represent at once — no runtime Mass create/destroy, per
    CLAUDE.md's "pooled, not spawn-destroy" guidance). Every `OnShipsStepped` it
    recomputes the desired cell window and reassigns whichever groups the window no
    longer covers (not gated behind an "active cell changed" check — the co-rotating
    window can drift even when it doesn't, see SDD 6 Amendment 9), rewriting
    `FSOLMinorBodyOrbitFragment`/`RenderFragment` data in place rather than
    creating/destroying entities. Reuses `USOLMinorBodyOrbitProcessor`/the existing
    fragments unchanged (matches by composition, not creator) — deliberately runs no
    orbit-processor instance of its own, to avoid double-processing the belt's entities
    too.
    **Built:** two adversarial review rounds (the cell-windowing pure logic, then the
    subsystem itself) found and fixed two more real design bugs beyond 5e-i's two
    (tracked as SDD 6 Amendments 9 and 10): `ActiveCellWindow` had a second
    co-rotating-frame bug (re-deriving a neighboring band's angular index as a
    same-band-circle-fraction, which implicitly assumed a shared origin across bands
    that doesn't exist — the existing tests couldn't catch it because they checked the
    implementation against a copy of the same flawed formula); and cells beyond a
    ring's real inner/outer edge were getting full-density rocks (fixed with two
    synthetic gap bands). Also fixed: the ring update moved from `OnBodiesUpdated` to
    `USOLShipSubsystem::OnShipsStepped` for a more current ship position, the ring
    plane normal is now cached instead of recomputed every call, an early-out avoids
    allocating on no-op frames, and a real Mass-architecture hazard was caught before
    it shipped (ring entities get no `FSOLMinorBodyAppearanceFragment` at all, since
    giving them one would make the belt's already-shipped visuals actor pick them up
    and render them into its own ISM arrays). Two performance questions (idle-ring
    processor cost, per-reassignment allocation rate) were explicitly deferred to
    profiling rather than fixed or ignored, recorded in `CLAUDE.md`'s tech debt with
    the review's own rate estimates. `SOL::RING_PATCH_WINDOW_RADIAL_RADIUS`/
    `_ANGULAR_RADIUS` raised from 1 to 2 after discovering a radius-R window only
    guarantees real coverage within (R - 0.5) cells, not R. Verified via a clean
    66-second headless `-SOLSmokeFlight` run (all 4 rings' pools spawned, zero errors)
    rather than a dedicated unit test, consistent with `USOLMinorBodySubsystem`'s own
    precedent — this subsystem's correctness rests on the already-tested `SOLRingPatch`
    pure functions it calls. 400 automation tests passing (was 382 after 5e-i; the 18
    new ones — `GetAngularCellCount`/`ActiveCellWindow`/`ReassignPoolSlots` coverage —
    extend `SOLRingPatchTest.cpp`, written as prerequisite pure-logic work for this
    sub-part even though that file itself belongs to 5e-i).
  - [ ] **5e-iii — `ASOLRingVisuals` + Niagara far field.** New actor per ringed planet:
    owns the pool's ISM component (bulk-transform update, same pattern as
    `ASOLAsteroidBeltVisuals`) and a `UNiagaraComponent` for the far-field annulus
    emitter (one shared, parameterized Niagara system across all 4 rings, not four
    near-duplicate assets). The Niagara asset is authored by the main session directly
    through the editor's Python/Niagara-scripting API (needs the live editor
    connection established this session; not delegated to a subagent). Cross-fades
    ISM/Niagara visibility via `ComputeNearFieldAlpha` from
    `OnUniverseUpdated`, same event the belt renders from.
  - [ ] **5e-iv — Integration and verification.** All four gas giants get rings
    (decision 8). Adversarial review (entity-pool reuse correctness, cell-boundary
    hysteresis, Niagara system cost, style-guide conformance), fix, re-verify. Real PIE
    screenshot verification at multiple distances per ring (confirm real gaps are
    visible in both tiers, the cross-fade isn't jarring, rapid cell-crossing at warp
    speed doesn't pop/crash). Commit.

## Cross-cutting

- [ ] `Docs/ARCHITECTURE.md`: new `MinorBodies/` module entry, the new Mass
  archetype/processor in the frame-order section (where it runs relative to the
  existing ship/body update), key types.
- [ ] No `GAME_MECHANICS.md` entry needed unless implementation adds player-facing
  controls (none currently anticipated — purely environmental/visual).
