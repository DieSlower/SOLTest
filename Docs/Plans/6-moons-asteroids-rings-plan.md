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
- [ ] **5c — Asteroid belt Mass architecture and rendering.**
  `FSOLMinorBodyOrbitFragment`/`FSOLMinorBodyAppearanceFragment`,
  `USOLMinorBodyOrbitProcessor` (SDD 6 §3.2 — mirrors `USOLShipFlightProcessor`'s
  chunk-parallel structure, far simpler: no gravity/collision/control), and
  `ASOLAsteroidBeltVisuals` (ISM, bulk per-frame transform updates from the
  processor's output — this project's first large *moving* GPU-instanced
  population). Screenshot verification. Adversarial review with explicit focus on
  per-frame bulk-transform-update cost at ~51,000 instances (the performance
  checklist's new concern this part introduces), fix, commit.
- [ ] **5d — Ring pure logic.** `Source/SOLTest/MinorBodies/SOLPlanetRing.h/.cpp` per
  SDD 6 §3.3: per-ring parameters for all four gas giants, gap-carving against every
  moon orbit that falls within a ring's radius range, reusing 5b's gap-exclusion
  math. Unit-tested (TDD), adversarial review, fix, commit.
- [ ] **5e — Ring rendering (near/far LOD).** `ASOLRingVisuals` per ringed planet:
  near-field ISM (shares 5c's Mass population/rendering approach, `ParentBodyIndex`
  set to the host planet), far-field Niagara (aggregate visual only, no per-particle
  Mass simulation), cross-fading by distance (adapt `Map/SOLMapBodyLod.h`'s pattern).
  Screenshot verification at multiple distances per ring (confirm gaps are visible,
  the near/far cross-fade isn't jarring). Adversarial review, fix, commit.

## Cross-cutting

- [ ] `Docs/ARCHITECTURE.md`: new `MinorBodies/` module entry, the new Mass
  archetype/processor in the frame-order section (where it runs relative to the
  existing ship/body update), key types.
- [ ] No `GAME_MECHANICS.md` entry needed unless implementation adds player-facing
  controls (none currently anticipated — purely environmental/visual).
