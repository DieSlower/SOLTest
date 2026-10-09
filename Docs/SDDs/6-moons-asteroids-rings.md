<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 6 — Moons, dwarf planets, asteroid belt, planet rings

Ticket `#6` ("Part 5: Moons, dwarf planets, asteroid belt and rings"). Plan of record:
[`Docs/Plans/6-moons-asteroids-rings-plan.md`](../Plans/6-moons-asteroids-rings-plan.md).

Refines SDD 1 ("Asteroids and ring particles are Keplerian too... Full MassEntity for
everything... A data-oriented body registry holds the bodies"). Must not contradict
[`Docs/SDDs/1-solar-system-architecture.md`](1-solar-system-architecture.md). All
decisions below were resolved with the user during a `grill-me` session on 2026-09-30.

---

## 1. Problem

Populate the solar system beyond the Sun and 8 planets: real moons and dwarf planets
(a few dozen named, individually significant bodies), a realistically sparse but
visually worthwhile asteroid belt, and planet rings with real structural detail
(gaps where moons clear them) — at a scale (tens of thousands of bodies) the existing
one-actor-per-body `ASOLBodyVisuals` approach cannot handle.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Moons & dwarf planets | **No new architecture.** `FSOLBodyRegistry` already supports arbitrary parent-child hierarchies (`ParentIndex` isn't limited to "orbits the Sun") and `ASOLBodyVisuals` already renders any registry body generically. This is purely a data-addition task: real orbital elements, radii, and (reusing issue #12's work) rotation data for each moon/dwarf planet, sourced the same way the existing Standish planetary elements already are. |
| 2 | Asteroids/rings architecture | **A separate, Mass-native population, not registry entries.** SDD 1 explicitly calls for "Full MassEntity for everything" and says asteroids/ring particles "are Keplerian too" — a new Mass archetype with compact orbital-element fragments and a dedicated parallel Mass processor (mirroring `USOLShipFlightProcessor`'s chunk-parallel pattern), rendered via GPU instancing, never as individual `AActor`s. |
| 3 | Asteroid belt composition | **~500 real named asteroids** (the largest, by real catalog data — JPL Small-Body Database or equivalent — with true orbital elements/radii, individually meaningful/targetable) **plus a procedural fill in ~200-400 "family" clusters** (10-50 asteroids each, within ~20-100 km of the cluster center), the clusters themselves scattered at realistic sparse spacing across the belt's real semi-major-axis/eccentricity/inclination ranges. This reconciles "somewhat accurate, far apart" (most of the belt is genuinely empty, matching the real belt's actual sparsity — a uniform density high enough for constant local visibility would require an astronomically (literally) larger population than is remotely practical) with "Hollywood-like near one you can see others" (real asteroid families genuinely do cluster this way, from ancient collision events, giving the clustering a real astronomical basis rather than being an arbitrary gamey compromise). Revised down from an initial ~1,000 during the 5b grill (see Amendment 2) — even the 500th-largest real asteroid is still a substantial, Phobos-scale (~60-70 km diameter, confirmed as ~61 km by the actual fetched data in 5b) body, and the "Hollywood" local density near a cluster comes entirely from the procedural family fill, not the real-named count, so halving the real count doesn't affect that goal. |
| 4 | Kirkwood gaps | The procedural belt generation excludes semi-major-axis bands at the real Kirkwood gap resonances (3:1, 5:2, 2:1 with Jupiter), the same "exclude a band" technique as decision 6's ring gaps — consistent realism, cheap to add alongside that logic. |
| 5 | Asteroid/ring gravity | **None.** Asteroids and ring particles never enter `SOLFlight::GravityAcceleration`'s sum — their real mass is far too small to produce a perceptible pull (even Ceres, the largest asteroid, is negligible next to any planet), and feeding a large Mass population into the ship's per-frame gravity computation for a physically undetectable effect would be pure waste. |
| 6 | Ring structure | Rings get real gaps: a clearing carved out wherever a moon's orbit falls within the ring's radius range (matching real shepherd-moon gaps, e.g. Saturn's Cassini/Encke/Keeler divisions), using the same "exclude a band" generation technique as decision 4. |
| 7 | Ring rendering (near/far LOD) | **Near the ring: individually Mass-simulated rocks, rendered via ISM** (real presence, flyable-through). **From typical viewing distance (the common case): a cheaper Niagara-driven representation that does not track individual rock positions** — a visual-only haze/band — cross-fading between the two as distance changes. Mirrors this project's established LOD philosophy (the star field's bright-sprite/baked-cubemap split; the jump map's mesh/icon cross-fade for bodies). |
| 8 | Ring scope | **All four gas giants** (Jupiter, Saturn, Uranus, Neptune) get rings in this part, even though three of them are real but visually faint — the system is data-driven per-planet (radius range, density, color, gap list), so doing all four is a modest data-authoring cost once the system exists, not four times the engineering. |
| 9 | Asteroid/ring collision | **Explicitly out of scope for this part**, tracked as a follow-up ([`Docs/ToDo/asteroid-ring-collision.md`](../ToDo/asteroid-ring-collision.md)) — the user's stated intent is that ring asteroids should eventually be collidable/dangerous ("they are not harmless"), but building real collision against a large moving Mass population is a substantial feature of its own, deliberately deferred. |

## 3. Design

### 3.1 Moons and dwarf planets (Appendix A)

Pure data addition to `FSOLBodyRegistry`'s existing `PopulateSolarSystem`-style table
(see `Docs/SDDs/12-planet-axial-rotation.md`'s `REGISTRY_SOLAR_SYSTEM`-equivalent
pattern, which this extends): for each moon/dwarf planet, `ParentIndex` set to the
host body's registry index (already-added planets, added before their children, per
`FSOLBodyRegistry::AddBody`'s existing contract), real orbital elements relative to
that parent, real radius, and (reusing issue #12) real rotation period/axial
tilt/W0 where readily available, else defaulting to non-rotating. No new C++ types.

Scope: the Moon, Phobos & Deimos, the four Galilean moons plus Saturn's major moons
(at minimum Titan, Enceladus, Mimas — needed anyway for the ring-gap data in §3.3),
Uranus's and Neptune's major moons, and the dwarf planets (Ceres, Pluto, Eris, Makemake,
Haumea). Exact list finalized during implementation against real data availability.

**Built (5a, see Amendment 1 below):** 5 dwarf planets + 20 moons (Moon; Phobos, Deimos;
Io, Europa, Ganymede, Callisto; Mimas, Enceladus, Tethys, Dione, Rhea, Titan, Iapetus;
Miranda, Ariel, Umbriel, Titania, Oberon; Triton), for a registry total of 34 bodies
(was 9). Two real design decisions surfaced during implementation that this section did
not anticipate — see Amendment 1.

### 3.2 Asteroid belt (Appendix B)

**New Mass archetype**, `Source/SOLTest/MinorBodies/` (new feature folder, shared
between the belt and ring particles since both are "small Keplerian bodies with no
individual significance" using identical orbital math):

```cpp
// Per-entity orbital state (a Mass fragment): Sun-relative for belt asteroids,
// parent-planet-relative for ring particles (decision 2)
USTRUCT()
struct FSOLMinorBodyOrbitFragment : public FMassFragment
{
    FSOLSecularElements Elements;    // reused from SOLKepler.h — no century-rate terms needed, but same shape
    int32 ParentBodyIndex;           // registry index: 0 (Sun) for belt asteroids, the host planet for ring particles
    double RadiusM;                  // for rendering scale
};

// Visual variation within a family cluster, or per-ring color/shape variation (const shared, per SOLShipParamsFragment's
// pattern — one copy per distinct value)
USTRUCT()
struct FSOLMinorBodyAppearanceFragment : public FMassConstSharedFragment { ... };
```

A new `USOLMinorBodyOrbitProcessor` (mirrors `USOLShipFlightProcessor`'s structure):
each frame, in parallel chunks, computes each entity's position via
`SOLKepler::ElementsToState` from the sim clock's current time, adding the parent
body's current position for non-Sun parents. No gravity, no collision, no control
input — purely kinematic, far simpler than the ship processor. **Corrected in 5c
(Amendment 3): the parent position is read from `FSOLBodyRegistry::GetPositionsM()`
(raw ecliptic/universe meters), NOT `FSOLBodyFrameCache`** — that cache is
axis-mirrored for ship physics and is the wrong frame for a pure-rendering consumer;
see Amendment 3 for why.

**Pure-logic generation** (new `Source/SOLTest/MinorBodies/SOLAsteroidBelt.h/.cpp`,
offline-callable from a C++ tool or at world startup — implementer's choice, likely
startup generation from a fixed seed is simplest, avoiding an offline-bake step
analogous to Part 4's since this is procedural math, not external-data processing):
- Real ~500-asteroid table (same `REGISTRY_SOLAR_SYSTEM`-table style — a compiled C++
  array literal, not a `UDataTable` asset — so the existing pure-function TDD pattern
  needs no new asset-loading machinery). Per Amendment 2, sourced by a dispatched
  subagent fetching real top-~500-by-size asteroid data from a public catalog (JPL
  Small-Body Database or equivalent) via live web fetch rather than typed from memory,
  given the row count is far beyond what 5a's hand-typed approach could reliably source
  accurately; cited in the source comment same as the existing tables.
- A deterministic family-cluster generator: places ~200-400 cluster centers across
  the belt's real element ranges (2.1-3.3 AU semi-major axis, realistic
  eccentricity/inclination distributions), excluding the Kirkwood gap bands
  (decision 4), then scatters 10-50 procedural asteroids per cluster within a local
  radius (20-100 km) of each center.
- Fully unit-tested (TDD as usual): cluster placement respects the gap exclusions,
  per-cluster member count/radius bounds are honored, the generation is
  deterministic (same seed → same belt, important for a reproducible scene and for
  tests themselves).

**Rendering**: an `ASOLAsteroidBeltVisuals` actor (new) with a
`UInstancedStaticMeshComponent` per distinct asteroid appearance/LOD tier, instance
transforms bulk-updated each frame from the Mass processor's output (the first
genuinely-moving large ISM population in this project — the star field's ISM was
static after creation; this one needs a per-frame bulk transform update, which the
performance review must specifically check scales acceptably). **Corrected in 5c
(Amendment 3): the actual default-configured belt is ~9,268 instances (500 real +
~8,768 procedural fill), not ~51,000** — that figure assumed decision 3's original
~1,000-real-asteroid count and the high end of the 200-400 cluster range; Amendment 2
already revised the real count down to ~500, and 5b's committed `DEFAULT_CLUSTER_COUNT`
(300) sits in the middle of its range, not the high end.

### 3.3 Planet rings (Appendix C)

**Built in 5d (see Amendment 4 below) with one scope extension**: Pan and Daphnis
(Saturn's real shepherd moons) were added to the registry so decision 6's
moon-orbit-lookup gap mechanism has real data, and the mechanism was built as a hybrid
(moon-orbit lookup plus fixed cited data for resonance gaps) rather than purely
moon-orbit-driven — see Amendment 4 for why.

Shares `FSOLMinorBodyOrbitFragment`/`USOLMinorBodyOrbitProcessor` with the belt
(`ParentBodyIndex` set to the host gas giant instead of the Sun). New pure-logic
generation (`Source/SOLTest/MinorBodies/SOLPlanetRing.h/.cpp`): per-ring parameters
(inner/outer radius, particle density, color, the list of gap-carving moon orbits) —
a small per-planet data table for the four gas giants (decision 8), each ring's
generation excluding bands around every moon orbit that falls within its radius range
(decision 6), reusing the same gap-exclusion math as the belt's Kirkwood gaps.

**Near/far LOD (decision 7)**: an `ASOLRingVisuals` actor per ringed planet with two
renderers cross-fading by camera distance to the ring, mirroring
`Map/SOLMapBodyLod.h`'s cross-fade pattern (reused/adapted, not reinvented):
- **Near**: the shared Mass-simulated rocks via ISM, same as the belt.
- **Far**: a Niagara system presenting the same ring's aggregate shape/density/color
  without per-particle Mass simulation — a visual approximation only, not
  individually tracked bodies, consistent with decision 7's "doesn't track
  individual rock positions."

### 3.4 Testing

- Pure logic (cluster/gap generation, Kirkwood/ring-gap exclusion math, the orbital
  fragment's position computation matching `SOLKepler` directly): TDD as usual,
  tests authored by a dispatched subagent from the contract alone.
- Engine integration (the Mass processor wired to real Mass execution, the ISM/Niagara
  rendering, the LOD cross-fade): screenshot verification, no new `-SOL*` smoke flag
  expected (no new player input/mechanic) unless implementation reveals a need for one.
- Performance review must specifically address: per-frame ISM bulk-transform-update
  cost at the belt's actual ~9,268 moving instances (see Amendment 3; the ~51,000
  figure in this section's earlier text was never the real configured count) plus
  however many near-field ring rocks are active at once — this project's first large
  *moving* GPU-instanced population, unlike the star field's static one. Per Amendment
  3, this was reviewed by estimate only, not real `stat`/Insights evidence; **real
  profiling evidence was captured in Amendment 5** (87.75 FPS average, 0% missed syncs
  at 30/60 FPS, 0 hitches/min with the belt running) — the open item is closed.

## 4. Open questions

None deferred as blocking. Asteroid/ring collision (decision 9) tracked as a
follow-up in `Docs/ToDo/asteroid-ring-collision.md`.

## 5. Revision history

- 2026-09-30: initial decisions from the `grill-me` session.
- 2026-10-01: Amendment 1, after implementing and reviewing 5a.
- 2026-10-01: Amendment 2, real-asteroid count and sourcing revised before starting 5b.
- 2026-10-01: Amendment 3, after implementing and reviewing 5c.
- 2026-10-01: Amendment 4, scope extension before 5d plus 5d implementation notes.
- 2026-10-01: Amendment 5, 5c's deferred PIE/profiling verification plus a real
  material bug found and fixed while doing it.
- 2026-10-01: Amendment 6, 5e grill-me and design — expanded near-field scope
  (player-streamed patch, not a whole-ring population) and the resulting technical
  design (Appendix D).
- 2026-10-01: Amendment 7, 5e-i adversarial review — the co-rotating-frame fix (the
  cell grid must rotate with the ring's local orbital motion) and the wrong-epoch fix
  for generated rocks, both found before any caller existed.
- 2026-10-06: Amendment 8, 5e-ii design — the pool is grouped by cell (one group of
  ROCKS_PER_CELL entities per active cell, not one entity per cell), a safe default
  orbit at spawn, simplified (no explicit position-) hysteresis, and
  USOLRingSubsystem's no-redundant-processor-run design.
- 2026-10-06: Amendment 9, adversarial review of the cell-windowing functions — a second
  co-rotating-frame bug in `ActiveCellWindow` (same class of mistake as Amendment 7),
  found and fixed before any caller existed.
- 2026-10-06: Amendment 10, adversarial review of `USOLRingSubsystem` — a real
  outside-ring-edge rocks bug fixed, the ring update moved to a more current-data event,
  pole-direction caching and an allocation early-out added, and two performance
  questions (idle-ring processor cost, per-reassignment allocation rate) explicitly
  deferred to profiling rather than fixed or ignored.

## Amendment 2 — 5b real-asteroid count and sourcing

Before starting 5b, a follow-up question surfaced: decision 3's "~1,000 real named
asteroids" was feasible for 5a's 25 moons to hand-type from memory into a C++ array, but
1,000 rows is a different scale with real risk of memorized-data errors and no verifiable
per-row source. Asked the user; resolved as:
- **Count revised to ~500** (from ~1,000). The real-world size-frequency distribution
  puts even the 500th-largest asteroid at a substantial ~60-70 km diameter (and the
  1,000th at ~45-50 km, Phobos-scale) — both are "real, substantial bodies," so this is a
  sourcing-feasibility reduction, not a meaningful loss of realism. The "Hollywood" local
  density near a cluster (decision 3's other half) comes entirely from the procedural
  family fill, unaffected by this count.
- **Sourcing: live fetch, not memorized.** A dispatched subagent fetches the real
  top-~500-by-size asteroid data from a public catalog (JPL Small-Body Database or
  equivalent) via web search/fetch, rather than typing it from general astronomical
  knowledge — the scale that made 5a's memorized-and-typed approach reasonable doesn't
  hold at 500+ rows.
- **Storage: still a compiled C++ array literal** (same style as every other table in
  this codebase), not a `UDataTable`/CSV content asset — keeps the existing pure-function
  TDD pattern with no new asset-loading machinery, at the cost of a large literal-data
  source file (accepted, matching precedent).

## Amendment 1 — 5a implementation: host-equator tilt conversion and orbit-GM fix

Two things surfaced during 5a's implementation and adversarial review that §3.1 as
originally written did not anticipate, since it was scoped as "no new C++ types... pure
data addition":

1. **Moon elements need a frame conversion, not just new rows.** Real moon catalog data
   (JPL SSD/Horizons) gives orbital elements relative to the host planet's equator, but
   `SOLKepler::ElementsToState` and the rest of this project work in the J2000 ecliptic.
   Using the catalog inclination/node values directly as ecliptic values would put every
   tilted planet's moons in the wrong plane (e.g. Uranus's moons would appear to orbit
   flat in the ecliptic while Uranus itself is drawn on its side, and Saturn's moons
   would miss their own rings' plane). The fix, added as file-local machinery in
   `SOLBodyRegistry.cpp` (not exposed outside the file, so it does not contradict "no new
   C++ types" in the public sense): an `ESOLElementFrame` tag per child row
   (`Ecliptic` or `HostEquator`) and a `HostEquatorToEcliptic()` helper that re-expresses
   a host-equator orbit in ecliptic axes by composing it with the host's existing
   `SOLBodyRotation::TiltRotation` (SDD 12's fixed pole-azimuth convention), preserving
   the real orbital phase and shape exactly. This also means every moon's orbit inherits
   SDD 12's known simplification: node angles are referenced to the host's fake fixed
   pole azimuth, not the real sky. Concretely — a host's moons keep correct phase
   *relative to each other*, but the whole set shares one not-astronomically-real phase
   offset from reality, and Triton's forced spin pole sits ~35° from its real orbit
   normal (so it will visibly nod/wobble once textured instead of holding steady like a
   real tidally-locked body). Tracked as a concrete consequence in
   [`Docs/ToDo/accurate-pole-directions.md`](../ToDo/accurate-pole-directions.md); not
   worth fixing ahead of that ToDo being picked up.
2. **Orbit-integration GM needed decoupling from physical GM.** `FSOLBodyRegistry::Update()`
   computes a child body's state via `SOLKepler::ElementsToState` using its *parent's*
   GM for the vis-viva velocity term — correct for the original Sun+8-planet table (a
   small, already-accepted mismatch of a few m/s versus the Standish mean-motion rates),
   but wrong enough for several close-in moons (whose real period reflects the parent's
   oblateness, not just its point-mass GM) to produce a materially wrong orbital speed —
   up to ~72 m/s / ~0.5% for Mimas, smaller but still real errors for Enceladus, Tethys,
   Dione and the Moon. This fed real player-facing systems (circular-orbit ship spawn,
   jump-arrival velocity matching, swept collision response). Fixed by adding
   `FSOLBodyDef::OrbitGM` (0 = use the parent's GM, the unchanged default for the
   Sun+8-planet table) and a registry-internal `mOrbitGMs` array; every child row now
   gets an explicit `OrbitGM` derived from its own tabulated semi-major axis and mean
   motion via Kepler's third law inverted (`mu = n^2 * a^3`, computed by the file-local
   `ChildOrbitGM()` helper), so its velocity is exactly consistent with its own real
   period regardless of what the parent's measured GM alone would predict. The Sun+8
   planet table is untouched.

Both were real findings from the required adversarial review (not pre-planned), fixed in
the same sub-part before commit, per this project's "fix before calling the part done"
rule. 5a is otherwise exactly as designed in §3.1.

## Amendment 3 — 5c implementation: frame correctness, actual scale, and a pending profiling gap

5c (`USOLMinorBodyOrbitProcessor`, `USOLMinorBodySubsystem`, `ASOLAsteroidBeltVisuals`)
is built, automation-tested (360 tests passing) and headless-smoke-tested
(`-SOLSmokeFlight`, 9/9), with two required adversarial review rounds applied. Real
findings, beyond the §3.2 text corrections noted inline above:

- **Entity count is ~9,268** (500 real + ~8,768 procedural fill from 5b's committed
  `DEFAULT_*` constants), not the ~51,000 this section originally estimated — see the
  corrections inline above. The architecture (Mass fragments, batch spawn, ISM bulk
  update) is unchanged by this; only the actual instance count differs from the
  original estimate.
- **A real performance bug, found and fixed**: `ASOLAsteroidBeltVisuals` initially
  called `BatchUpdateInstancesTransforms` with `bMarkRenderStateDirty=true`, which
  destroys and rebuilds both ISM components' entire scene proxies (all ~9,268
  instances, re-uploaded to the GPU scene) every single frame, not just updates the
  changed transforms. Fixed to `false` — the instance-data manager already pushes
  per-instance transform changes incrementally without it.
- **A real, pre-existing latent bug found in already-shipped code**: the new
  subsystem's entity-destruction-on-teardown crashed because `UMassEntitySubsystem`
  may already have deinitialized the shared `FMassEntityManager` by the time a
  `UWorldSubsystem::Deinitialize` runs (subsystem deinit order isn't guaranteed).
  Fixed by moving entity destruction to `PreDeinitialize`. The adversarial review found
  `USOLShipSubsystem::Deinitialize` (issue #2/#3 era) has the exact same unsafe
  pattern, which has not crashed only by luck of subsystem registration order — fixed
  the same way, as part of this sub-part's review-fix pass, even though the code
  predates issue #6.
- **Data layout correction**: `FSOLMinorBodyOrbitFragment` originally bundled
  rendering-only fields (`RadiusM`, `InstanceIndex`) alongside orbital-dynamics fields
  (`Elements`, `ParentBodyIndex`), so both the orbit processor's and the visuals
  actor's hot loops dragged the other's unused data through cache every entity, every
  frame. Split into `FSOLMinorBodyOrbitFragment` (processor-only) and the new
  `FSOLMinorBodyRenderFragment` (visuals-only).
- **Style-guide fix**: the visuals actor's per-entity loop called into
  `USOLAnchorSubsystem` (a `UObject`) once per entity; fixed to snapshot the render
  origin and viewpoint once per frame and call the underlying pure placement function
  directly inside the loop (STYLE_GUIDE.md §15).
- **No profiling evidence yet at the time this amendment was written — resolved in
  Amendment 5.** CLAUDE.md's performance checklist item 9 requires `stat`/Insights
  evidence for cost claims, not estimates. The adversarial review's own back-of-envelope
  estimate (extrapolating from the star field's measured per-instance cost) suggested
  the post-fix per-frame cost was "probably tolerable" at ~9,268 instances, but that was
  explicitly a guess, not evidence; real `stat unit`/`stat game`/Insights numbers
  required an interactive or real-RHI editor session, which this implementation pass
  did not have access to. See Amendment 5 for the real measurement. The review also
  flagged (not blocking, logged as tech debt/ToDo
  instead of fixed here): the belt's sphere mesh has no LOD/cull-distance despite most
  instances being sub-pixel from any reasonable viewing distance; orbital constants
  (`pHat`/`qHat`, etc.) are recomputed from scratch every frame per entity rather than
  cached at spawn (cheap to add later, not blocking at this scale); the two ISM
  variants exist only for a color distinction and could become one ISM with per-instance
  custom data once real art arrives; a GPU-side/Niagara-driven approach would be needed
  to reach Part 10's 1M-entity scale demo, since a per-frame CPU-computed,
  CPU-to-GPU-uploaded transform array fundamentally does not scale that far.
- Asteroids render with the default placeholder material/Sun-direction, same
  as 5a's bodies and consistent with this project's deferred-art-pass precedent — not
  a regression, just not yet visually polished.

5c is otherwise exactly as designed in §3.2 (Appendix B).

## Amendment 4 — 5d scope extension and implementation notes

**Scope extension, decided with the user before 5d started**: decision 6 ("ring gaps
wherever a moon's orbit falls within the ring radius range") can't work as originally
envisioned using only 5a's registered moons — none of them actually orbit within any
gas giant's ring system (5a scoped to "major" moons only; the real gap-carving moons,
like Saturn's Pan and Daphnis, are much smaller "shepherd moons"). Also, Saturn's most
famous gap, the Cassini Division, isn't a shepherd-moon gap at all — it's a 2:1
orbital resonance with Mimas (whose own orbit, ~186,000 km, is far outside the rings'
~136,780 km outer edge), the same resonance-gap mechanism as the asteroid belt's
Kirkwood gaps, not a moon-orbit lookup. Resolved as a hybrid, with the user's explicit
direction to do both:
1. **Pan and Daphnis added to the registry** (extending 5a's scope, same conventions:
   `ESOLElementFrame::HostEquator`, `ChildOrbitGM`), fetched live from JPL Horizons
   (cross-confirmed against Jacobson et al. 2008, *Astronomical Journal* 135(1),
   261-263, for precise orbital periods). Registry total is now 36 bodies (was 34).
   Both moons' real osculating eccentricity (~0.005) is Saturn's J2 oblateness acting
   on an otherwise-circular orbit, not free eccentricity — verified during adversarial
   review against the expected J2-forcing magnitude (1.5 · J2 · (R/a)² ≈ 0.005, matching
   observation) — so `E0 = 0` (the tabulated mean value) is the physically correct
   simplification, not a convenience.
2. **`SOLPlanetRing.h/.cpp`** (new, `Source/SOLTest/MinorBodies/`): `RealRings()` (the
   four gas giants' real ring radius ranges, cited to NSSDCA ring fact sheets);
   `KnownResonanceGapBandsM(planetName)` (fixed, cited real gap data for resonance gaps
   with no corresponding registered moon — currently just Saturn's Cassini Division);
   `MoonShepherdGapBandsM`/`AllGapBandsM` (the generic "exclude a band around a moon's
   semi-major axis" mechanism, decoupled from the registry — callers pass each moon's
   semi-major axis, computed once, not a live per-frame position). Real per-moon gap
   half-widths are used for Pan (Encke Gap, ~162 km) and Daphnis (Keeler Gap, ~21 km)
   rather than one uniform width — their real gap widths differ by ~8x, and an
   adversarial-review finding caught an initial uniform-width implementation making
   Daphnis's modeled gap ~7x too wide. `FSOLRadiusBandM`/`IsInAnyBand` are a
   meters-based analog of the asteroid belt's AU-based `FSOLAxisBandAU`/
   `IsInKirkwoodGap` — a separate type rather than literal code-sharing, since rings
   (planet-relative, meters) and the belt (Sun-relative, AU) have no natural shared
   unit; this project's "reusing the same gap-exclusion math" intent (§3.3 above) means
   the same *technique* (exclude-a-band), not a literal shared type.
3. Test-first discipline for 5d: the registry addition (pure data) used test-after,
   mirroring 5a's own precedent; `SOLPlanetRing`'s logic (simple interval/band
   arithmetic, materially lower algorithmic risk than 5b's orbital-mechanics
   conversions) used self-administered test-first by the same implementer rather than
   a separate contract-only test-writer round, mirroring the proportionate judgment
   already applied to 5c's small `SOLMinorBodyOrbit::ComputePositionM` helper.

No consumer of `SOLPlanetRing` exists yet (5e builds the rendering that will call it);
its functions allocate and are meant for setup-time use, not per-frame — 5e should
cache `AllGapBandsM`'s result per ring rather than call it every frame.

## Amendment 5 — 5c's deferred PIE/profiling verification, plus a real material bug

Done in an interactive editor session (launched via Rider's debug run configuration,
driven by the editor's own Python API over the Rider MCP connection, rather than
waiting on the user) once one became available. Closes the one open item carried by
Amendment 3.

- **A real, previously-undetected bug found before any formal verification even
  started**: the editor's own boot log showed `Material /Game/SOL/Materials/M_SOLBody
  missing usage flag InstancedStaticMeshes! Default Material will be used in game.`
  `ASOLAsteroidBeltVisuals` renders both its `UInstancedStaticMeshComponent`s with this
  material (SOL::Paths::BODY_MATERIAL), so the belt had been silently rendering with
  the engine's default material — not the intended per-variant colored/emissive
  material — since 5c shipped. Automation tests and the headless `-SOLSmokeFlight`
  check could not have caught this: it is a material-asset flag, invisible to anything
  that doesn't actually render the belt and look at (or log-check) the material
  pipeline. Fixed by setting `UMaterial::bUsedWithInstancedStaticMeshes = true` on
  `M_SOLBody` and resaving the asset (`Content/SOL/Materials/M_SOLBody.uasset`) via the
  editor's Python API; confirmed fixed by the warning's absence in the next PIE run's
  log. This is the same material 5e's near-field ring rocks are planned to reuse
  (§3.3), so 5e would otherwise have inherited the same silent bug.
- **PIE verified stable**: `SOL_Test` played without crashing; the Outliner confirmed
  all of `ASOLGameMode::StartPlay`'s spawned actors present (`SOLAsteroidBeltVisuals0`,
  `SOLBodyVisuals0`, `SOLFlightHud0`, `SOLShipPawn0`, `SOLStarField0`, 14 actors total).
  The log confirmed `AsteroidBeltVisuals SOLAsteroidBeltVisuals_0: 9268 asteroid
  instances in 2 variants` — matching Amendment 3's corrected count exactly.
- **Real profiling evidence (the open item)**: `StartFPSChart`/`StopFPSChart` (console
  commands run via the editor's Python API) over a 25.51-second PIE session with the
  belt's Mass orbit processor and per-frame ISM bulk transform update running the whole
  time: **2,239 frames, 87.75 FPS average, 0% syncs missed at both the 30 FPS and 60 FPS
  targets, 0.00 hitches/min**, no hitches attributed to the game thread, render thread,
  RHI thread or GPU. This directly answers CLAUDE.md's performance-checklist item 9 for
  5c: the post-fix per-frame ISM update cost is not a bottleneck at the belt's actual
  ~9,268-instance scale. (A first attempt at this measurement read ~3 FPS; that was a
  measurement artifact, not a real result — the Unreal Editor throttles rendering to a
  few FPS when its window doesn't have OS focus
  [`UEditorPerformanceSettings::bThrottleCPUWhenNotForeground`, default on], which an
  MCP-driven, unfocused session always triggers. Disabled that setting for the
  measurement above, then restored it afterward so the user's own editor experience is
  unaffected.) This number is from a PIE/editor build, which carries extra editor
  overhead a packaged build would not have, so it is a conservative (lower) estimate of
  real in-game performance, not an optimistic one.
- **`stat unit`/`stat game`'s on-screen overlay could not be screenshotted** — neither
  the Slate viewport capture nor `HighResShot` included the engine's on-screen stat
  canvas in this setup (`HighResShot` in particular appears to deliberately suppress it
  during tiled capture). The FPS-chart log dump above was used instead, which is
  arguably stronger evidence (aggregate percentiles and a hitch breakdown over many
  seconds, not a single instantaneous counter reading).
- **One unrelated, pre-existing, non-fatal engine ensure was observed**, not a Part 5
  regression: `Ensure condition failed: IsInGameThread() ... Attempted to retrieve
  FAppTime on a thread where there is no inherited time context`, with a callstack
  entirely inside engine DLLs (Core/Renderer/RenderCore/Engine — no `SOLTest` frames),
  coincident with the material recompile/save above (which queues async shader
  compilation). Did not recur, and the FPS-chart run immediately after was clean (0
  hitches), so it is noted here for the record and not investigated further.
- Visual confirmation of the belt's actual on-screen appearance (flying to it and
  screenshotting asteroids up close) was not attempted this pass — reaching the belt's
  real position (~2-3.5 AU from the Sun) from the default Earth-orbit spawn needs either
  the in-game jump map's UI (no input-injection tool was available over this MCP
  connection) or a debug teleport, neither pursued here since the profiling and material
  evidence above already closes 5c's tracked open item. Worth doing opportunistically
  during 5e's own screenshot verification, which will need belt-adjacent camera
  positions anyway for the ringed planets.

5c is now considered fully verified.

5d is otherwise exactly as designed in §3.3 (Appendix C).

## Amendment 6 — 5e grill-me and design (Appendix D)

Grilled before any 5e code. Decision 7's two-sentence near/far summary undersold what
"near the ring: individually Mass-simulated rocks, flyable-through" actually requires
once pushed on: Saturn's ring alone spans ~66,900-136,780 km radially (`RING_REAL_RINGS`
in `SOLPlanetRing.cpp`) — a whole-ring population at any flyable density is
not feasible (even the sparse belt's ~9,268 instances cover a 2.1-3.3 AU band with
enormous gaps; a ring flown through at human/ship scale needs rocks roughly every
50-150 m, which over tens of thousands of km of radius and circumference is many
orders of magnitude more instances than any Mass population this project can afford).
Resolved as a genuinely bigger feature than the plan's original sketch:

1. **Near field is a player-streamed local patch, not a static whole-ring population**
   (user's explicit choice over the whole-ring alternative, which was offered and
   recommended against at this scale). A local 3D neighborhood of rocks around the
   player's current position within the ring, re-centered as they move, not the belt's
   "everything spawned once at world start" model.
2. **Collision stays deferred** (decision 9 unchanged) even though the near field now
   reads as a real flyable rock field rather than a sparse belt — explicitly
   re-confirmed with the user given the stakes of staying collision-less just went up;
   `Docs/ToDo/asteroid-ring-collision.md` gets a note to that effect.
3. **Target density: ~50-150 m average rock spacing** within the active patch —
   scattered and individually navigable, not a wall of debris (the denser
   "wall-to-wall" alternative was offered and declined, partly for believability: real
   ring material at ship scale is mostly empty space between visible chunks, and partly
   for the entity-churn cost of a much higher streaming density).
4. **Far field is a Niagara system that samples particles across the ring's annulus**
   (inner/outer radius minus `SOLPlanetRing::AllGapBandsM`'s real gap bands), not a
   single textured disc mesh — the more "Niagara-native" option, reads as granular from
   any distance, and the real gaps show up for free as genuinely particle-free bands
   rather than needing a baked gap texture.

### Technical design

**Shared math promoted, not duplicated.** `SOLAsteroidBelt.cpp` already has a
file-local `BeltStateToElements`/`FSOLOrbitState`-based conversion (Cartesian
position+velocity → classical elements, the inverse of `SOLKepler::ElementsToState`,
used by 5b's family-member generator) and a `BeltMemberElements` helper that places a
member at a displaced position with the center's orbital energy so it shares the
center's semi-major axis and period exactly. Ring-patch rock generation needs exactly
this operation (place a rock at a locally-displaced position near the player, in
near-circular motion, so it holds its place in the patch's local frame rather than
drifting) but relative to a planet's GM instead of the Sun's, and with no "cluster
center entity" to displace from — just a geometric point. **Promote
`BeltStateToElements` to `SOLKepler::StateToElements(state, gm)`** in
`Source/SOLTest/Universe/SOLKepler.h/.cpp`, right alongside `ElementsToState` as its
documented inverse; `SOLAsteroidBelt.cpp` calls the shared version instead of its own
copy (5b's existing tests must keep passing unchanged, plus new direct tests for the
promoted function). The "displace from a local point with matching energy" shape of
`BeltMemberElements` becomes the new `SOLRingPatch` module's own rock-placement helper,
parameterized by the host planet's GM instead of the Sun's.

**New pure-logic module, `Source/SOLTest/MinorBodies/SOLRingPatch.h/.cpp`** (genuinely
new geometry/orbital-mechanics logic — full contract-only TDD, a dispatched subagent
writes the tests from the header alone, same as 5b):
- A fixed-size **cell grid local to each ring**: cells are `CELL_SIZE_M` (1,000 m) on a
  side in a local tangent-plane approximation (radial × tangential; ring curvature over
  1 km is negligible next to a ring's tens-of-thousands-of-km radius). A cell is
  addressed by `(radialCellIndex, angularCellIndex)` relative to the ring's center.
- `ComputeActiveCell(playerPositionM, planetPositionM, ringDef)`: given the player's
  current position, returns which cell they're in (or "not in this ring" if outside the
  ring's radial band + a margin, or too far from the ring plane vertically). **Corrected
  in Amendment 7: both this and `GenerateCellRocks` also take the host planet's GM and
  the current `secondsSinceJ2000`, because the cell grid must co-rotate with the ring's
  local orbital motion — see Amendment 7 for why a non-rotating grid doesn't work.**
- `GenerateCellRocks(ringDef, cellIndex, planetGM, gapBands, seed)`: deterministically
  generates that cell's rocks (position, near-circular orbital elements via the
  promoted `StateToElements`-based placement, each rock's own `RadiusM`) — same seeded
  procedural-generation discipline as the belt (same cell index + seed always produces
  the same rocks, no persistence needed). Honors `gapBands`: a cell (or the part of a
  cell) that falls inside a gap band gets fewer/no rocks there, same exclusion
  technique as the belt's Kirkwood gaps and the ring's own gap bands.
- `ComputeNearFieldAlpha(distanceOutsideRingVolumeM, transitionBandM)`: a
  `SOLMapBodyLod::Evaluate`-style smooth 0..1 cross-fade (0 = fully far/Niagara-only, 1
  = fully near/patch-only), for blending the ISM patch's visibility against the
  Niagara emitter's as the player crosses the ring's activation boundary.
- Rock count per cell derived from the ~50-150 m density target and `CELL_SIZE_M`:
  ~100 rocks/cell at the approved density's midpoint (documented as a tunable constant,
  not re-derived by every caller).

**Entity pooling, not runtime Mass create/destroy.** CLAUDE.md's own performance
guidance calls for "pooled ... instead of spawn-destroy" — exactly the right fit here.
Each ring's `USOLRingVisuals`-owned Mass population is a **fixed-size pool**, reserved
once at world begin-play (size = active-cell-grid-size × rocks-per-cell, e.g. a 3×3
grid × ~100 = ~900 entities per ring, ~3,600 across all 4 rings if every ring somehow
had an active patch at once — well inside the headroom Amendment 5 just measured at
~9,268 belt entities). Crossing a cell boundary **reassigns pool slots to the
newly-active cells** (rewrites each reassigned slot's `FSOLMinorBodyOrbitFragment`
in place via `GenerateCellRocks`, and its ISM instance transform) rather than
creating/destroying Mass entities or ISM instances at runtime — no per-transition
heap allocation, no ISM `RemoveInstances` index-reshuffling hazard, matching the
project's existing "reserve up front" discipline. A cell-boundary crossing uses a
half-cell hysteresis margin (same spirit as the anchor subsystem's rebase hysteresis,
SDD 1) so flying back and forth near a boundary doesn't thrash reassignment every
frame. `USOLMinorBodyOrbitProcessor` needs no changes: it matches entities by fragment
composition, not by which subsystem created them, so ring-patch entities (
`ParentBodyIndex` = the host planet) run through the exact same processor as the
belt's Sun-relative ones.

**New types**:
- `Source/SOLTest/MinorBodies/SOLRingPatch.h/.cpp` — the pure logic above.
- A new `USOLRingSubsystem` (`UWorldSubsystem`, mirrors `USOLMinorBodySubsystem`'s
  shape) owning all 4 rings' pools: tracks the ship's position each frame, computes
  each ring's active cell (or "not present"), reassigns pool slots on a cell-boundary
  crossing, and runs the shared `USOLMinorBodyOrbitProcessor` over all ring entities
  from the same `OnBodiesUpdated` delegate the belt uses.
- A new `ASOLRingVisuals` actor, one per ringed planet, owning: the pool's ISM
  component (same bulk-transform-update pattern as `ASOLAsteroidBeltVisuals`, fixed
  instance count, transforms rewritten not instances added/removed) and a
  `UNiagaraComponent` for the far-field annulus emitter. Cross-fades the two using
  `ComputeNearFieldAlpha` each frame from `USOLAnchorSubsystem::OnUniverseUpdated`,
  same event the belt renders from.
- A new Niagara system asset per ring (or one shared system with per-instance
  parameters for inner/outer radius, gap bands, color and density — **one shared
  system, parameterized**, is the plan: four assets with near-identical graphs would be
  needless duplication given the system is entirely data-driven already via
  `FSOLPlanetRingDef`). Authored directly by the main session through the editor's
  Python/Niagara-scripting API during implementation (this needs the live editor
  connection established this session, which a dispatched subagent does not have), not
  hand-built in the Niagara editor UI and not delegated to a subagent.

**Trigger geometry**: a ring's patch is "active" (fading toward 1) when the player's
distance from the planet is within `[InnerRadiusM - marginM, OuterRadiusM + marginM]`
radially and within a generous vertical half-thickness of the ring plane (looser than
the ring's real near-zero physical thickness, for gameplay feel — flying close to the
ring shouldn't require pixel-perfect plane alignment). Exact margin/thickness/
transition-band constants are implementer judgment within this shape, tuned against
the required screenshot verification rather than pre-specified here.

**Testing**: `SOLRingPatch`'s cell/placement/alpha logic gets full contract-only TDD
(dispatched test-writer, same as 5b); the promoted `SOLKepler::StateToElements` gets
direct tests alongside `ElementsToState`'s existing ones; the pool/subsystem/actor
integration and the Niagara system are screenshot-verified (multiple distances per
ring, confirming the cross-fade reads cleanly and the gaps are visible in both tiers),
no unit tests for the Niagara asset itself.

5e is scoped as described here, superseding §3.3's shorter near/far-LOD paragraph.

## Amendment 7 — 5e-i adversarial review: the co-rotating-frame fix

5e-i (`SOLKepler::StateToElements`/`MeanMotionDegPerCy`, the `SOLAsteroidBelt.cpp`
refactor, and the new `SOLRingPatch` module) was implemented, tested (379/379 passing,
including one genuine test-arithmetic bug found and fixed during the main session's own
RED/GREEN verification — an exact-cell-boundary coincidence in a "1.5 cells away"
displacement test, fixed to a "1 cell away" displacement that lands unambiguously at the
neighboring cell's center) and adversarially reviewed. The review's core math tracing
(by hand, not just "tests are green") confirmed the RV2COE inversion and the belt
refactor are both correct, and found two real design bugs below, both fixed before
5e-ii starts (no callers existed yet, so fixing the contract now was free):

- **Rocks were generated at the wrong epoch.** `GenerateCellRocks` built each rock's
  position "right now" (at whatever instant it's called) but then stored that
  instant's mean longitude directly into `FSOLSecularElements::L0Deg`, which is always
  defined at the **J2000 epoch** (T=0) — `SOLMinorBodyOrbit::ComputePositionM` always
  evaluates `Elements.AtCenturies(secondsSinceJ2000 / centuryLength)`. Since the
  current sim time is ~0.27 centuries past J2000 and a close-in ring rock's mean motion
  is enormous (~6×10⁷ deg/century for a Saturn-ring-scale orbit), assigning the
  "right now" longitude as if it were the J2000 longitude would place the rock at an
  essentially arbitrary point on its orbit the moment it's actually evaluated — nowhere
  near its cell. The belt never hit this because it generates everything at J2000.
  **Fixed**: `GenerateCellRocks` now takes `secondsSinceJ2000`, builds the rock's state
  at that instant as before, then rolls `L0Deg` back to its true J2000-epoch value
  (`wrap(currentMeanLongitudeDeg - LDotDegPerCy * secondsSinceJ2000/centuryLength)`)
  before returning it.
- **The cell grid didn't rotate with the ring.** A ring rock orbits at real orbital
  speed — tens of km/s for a close-in gas-giant ring, crossing a 1 km cell in a
  fraction of a second. A cell grid fixed in the planet's non-rotating equatorial frame
  would see every rock leave its own cell almost immediately after being generated,
  and would force the eventual streaming subsystem to reassign its active-cell window
  dozens of times per second even with a stationary player — directly contradicting
  this very document's "holds its place in the patch's local frame" design intent.
  **Fixed**: `ComputeActiveCell`'s (and `GenerateCellRocks`'s) angular coordinate is now
  defined in a frame **co-rotating with the local circular mean motion at the cell's
  own radial band** (`worldAngle(t) - sqrt(planetGM/referenceRadiusM³) * t`, wrapped).
  A rock generated near the band's reference radius then advances at very nearly that
  same rate, so it stays at very nearly the same co-rotating angle indefinitely — real
  orbital shear across one `CELL_SIZE_M`-wide radial band is only a few m/s (per the
  reviewer's estimate, the patch stays coherent for minutes, not milliseconds). Both
  functions now also take `planetGM` and `secondsSinceJ2000` to compute this frame;
  `RadialIndex` itself is unaffected (radius doesn't change under in-plane rotation).

Also fixed in the same pass (adversarial review's medium/low findings): stale test
comments describing the pre-Amendment-6-fix "anchored at InnerRadiusM - marginM"
indexing convention; `StateToElementsNearCircular`/`NearEquatorial` test comments
overclaiming they exercised `StateToElements`'s degenerate-orbit fallback branches
(they didn't — `1e-9` is far above the `1e-12` fallback threshold; added true
exact-degenerate cases, including exact retrograde-equatorial, i = π); weak per-cell
seed mixing in `GenerateCellRocks` (adjacent cells' seeds differed only in low bits
under an LCG — finalized through a proper hash mix); an inverted-logic NaN hazard in
`ComputeActiveCell`'s admission test; a `FSOLRingCell`/`BeltStateToElements`-style
duplicated element-packing helper (promoted the shared "classical elements at an
instant → `FSOLSecularElements` with `LDotDegPerCy` left to the caller" packing into
`SOLKepler`, alongside `StateToElements`, used by both the belt and the ring patch);
and a missing `GetTypeHash(FSOLRingCell)` (added now since 5e-ii's pool bookkeeping
will need to key a map by cell).

**Deferred to 5e-ii, not fixed here** (the review's M1 finding): `GenerateCellRocks`
returns a fresh `TArray` per call (~10 KB for a full cell), which conflicts with this
document's own "no per-transition heap allocation" pooling design if called as
written on every cell-boundary crossing. Left alone in 5e-i because 5e-ii's actual
pool-slot-reassignment code will determine the right shape for this (very possibly an
out-parameter or direct per-slot write instead of a batch `TArray` return) — deciding
it now, with no real caller to validate against, risked guessing wrong. Tracked
explicitly so 5e-ii's design addresses it rather than copying the batch-return shape
as-is. The review's note on `RingPatchMakeBasis`'s `FindBestAxisVectors`-based basis
being sensitive to floating-point ties if a caller recomputes `ringNormal` with noise
is similarly left for 5e-ii's design (the `ASOLRingVisuals`/`USOLRingSubsystem` code
should cache each ring's basis once rather than recomputing it with a possibly-jittery
normal every call).

## Amendment 8 — 5e-ii design: the pool is grouped by cell, not one slot per rock

Working out `USOLRingSubsystem`'s actual implementation surfaced a real shape mismatch
in Amendment 6's "fixed-size pool, reassign slots" description: `GenerateCellRocks`
returns up to `ROCKS_PER_CELL` (~100) individual rocks for ONE cell, not one. A "pool
slot" in `SOLRingPatch::ReassignPoolSlots`'s sense is therefore a **group of
`ROCKS_PER_CELL` Mass entities**, not a single entity — a ring's pool has
`(cell window size) * ROCKS_PER_CELL` entities total (e.g. a 3×3 window × 100 = 900),
organized as contiguous groups of 100, each group's `TOptional<FSOLRingCell>` tracked
one level up from the individual entities.

**Reassigning a group** (its desired cell changed, per `ReassignPoolSlots`): call
`GenerateCellRocks` for its new cell and write each returned rock's `Elements`/`RadiusM`
into one of the group's `ROCKS_PER_CELL` entities. Two sub-cases `ReassignPoolSlots`
alone doesn't cover, both resolved here rather than in that pure function (which rightly
knows nothing about Mass entities or rock counts):
- **Fewer rocks than entities in the group** (a gap-affected cell returns under
  `ROCKS_PER_CELL` rocks): the group has more entities than real rocks to show. Rather
  than inventing a "this entity is inactive" representation, the excess entities are
  given `rocks[i % rocks.Num()]` — they end up exact duplicates of an already-placed
  rock (same position, same orbit), invisible in practice since they coincide exactly,
  and avoids a second fragment/rendering concept purely for an edge case. If
  `rocks.IsEmpty()` (the whole cell is gap), the group is left with its PREVIOUS data
  (see below) rather than this cycling trick, since there's nothing to cycle.
- **A group with no cell at all** (`ReassignPoolSlots` returned unset: the ring's
  desired window shrank, e.g. the player left the ring entirely): its entities are left
  with their stale, previously-assigned `Elements` untouched. They keep orbiting
  (the shared processor still places them every frame; nothing here stops that) at a
  now-irrelevant position, which is harmless — 5e-ii has no renderer yet, and 5e-iii's
  renderer is expected to hide an inactive ring's ISM via `ComputeNearFieldAlpha`
  reaching 0, not by this subsystem clearing data nothing is reading.

**Safe default orbit at spawn.** A freshly `BatchCreateEntities`'d entity's fragments
are zero-initialized, giving `FSOLSecularElements::A0AU = 0` — fed through
`SOLKepler::ElementsToState`'s `sqrt(gm/a^3)`, this divides by zero. Both this and the
"group with no cell" case above mean an entity can go a while without ever receiving
real `GenerateCellRocks` data (any ring the player never visits in a session). Every
pool entity is therefore given a safe, valid, boring placeholder orbit AT SPAWN, before
any real cell is ever assigned: circular at the ring's own `OuterRadiusM`, mean motion
from `SOLKepler::MeanMotionDegPerCy(OuterRadiusM, planetGM)`, zero eccentricity/
inclination/node/periapsis. Never observably wrong (nothing renders it while unassigned)
and never NaN/Inf.

**Hysteresis, simplified from Amendment 6's original wording.** Amendment 6 called for
"a half-cell hysteresis margin... so flying back and forth near a boundary doesn't
thrash reassignment every frame," implying genuine position-based hysteresis (sticking
to the old cell until the player is unambiguously past it). On working through the
actual cost, this turned out to be unnecessary complexity for 5e-ii: `ReassignPoolSlots`
already leaves a group untouched when its cell is still inside the new desired window,
and a 3×3 window flipping between two adjacent active cells overlaps in most of its 9
cells — so boundary flicker reassigns only the few groups at the window's edge, not the
whole pool, every time. **5e-ii ships without explicit position hysteresis**; real
position-based hysteresis is deferred as a follow-up if screenshot/flight-feel
verification (5e-iv) actually shows objectionable flicker, rather than built pre-emptively
against a cost that may not materialize.

**`USOLRingSubsystem` does not run its own orbit-processor instance.** Ring-rock
entities share `FSOLMinorBodyOrbitFragment`/`FSOLMinorBodyStateFragment` with the
asteroid belt; Mass matches a processor's query by fragment composition, not by which
subsystem created an entity, so `USOLMinorBodySubsystem`'s existing per-frame
`USOLMinorBodyOrbitProcessor` run (already bound to `OnBodiesUpdated`) places every
matching entity in the world, belt and ring both, in one pass. A second processor
instance run by `USOLRingSubsystem` would reprocess the SAME global entity set a second
time every frame — pure waste, not a correctness bug (the computation is idempotent),
but real wasted cost at scale. `USOLRingSubsystem` therefore only manages entity DATA
(which cell each group represents); it declares an explicit `UWorldSubsystem`
dependency on `USOLMinorBodySubsystem` purely to document this relationship for a future
reader, not because it calls into it.

## Amendment 9 — adversarial review of `ActiveCellWindow`/`ReassignPoolSlots`: a second co-rotating-frame bug

Reviewing the three cell-windowing functions added for Amendment 8's pool design (before
`USOLRingSubsystem` had any real body) found a second instance of the SAME class of
mistake Amendment 7 fixed once already: **`ActiveCellWindow` re-derived a neighboring
radial band's angular index as "the same FRACTION of that band's own circle,"
implicitly assuming every band shares a common angular origin — but Amendment 7's
co-rotating frame gives every band its OWN phase offset** (`worldAngle - meanMotion(r)
* t`), and different radial bands have different mean motions, so they do NOT share an
origin at any instant except by coincidence. The adversarial review hand-traced this at
the test fixture's own scale (Saturn GM, t ≈ 26.76 years since J2000) and found
neighboring-band cells landing **hundreds of thousands of km away from the actual
center cell** — nowhere near a real "3×3 window around the player." The existing test
suite could not catch this because its own "expected value" helper
(`RingPatchAlignedAngularIndex`) reimplemented the exact same flawed fraction formula,
so the tests checked the implementation against a copy of its own mistake, not against
real geometry (`ComputeActiveCell`, the one function in this module that already gets
the co-rotating frame right).

**Fixed** by routing the re-derivation through world angle, the one frame every radial
band agrees on, instead of through each band's own circle-fraction: convert the center
cell's co-rotating angular position to a world angle at `secondsSinceJ2000` (add back
its own band's phase), then convert that world angle into each neighboring band's own
co-rotating frame (subtract THAT band's phase) before deriving its `AngularIndex`.
`ActiveCellWindow` gained `planetGM`/`secondsSinceJ2000` parameters to do this (the
same inputs `ComputeActiveCell`/`GenerateCellRocks` already take). The test suite's
`ActiveCellWindow*` cases were rewritten to verify against `ComputeActiveCell` itself
(real position round-trips) rather than a parallel reimplementation of the formula
under test — the right fix per the review, since a test that re-derives its own
expectation from the same (possibly wrong) formula the implementation uses can never
catch a mistake in that formula, only a mistake in how faithfully the implementation
matches its own description.

**Practical consequence for 5e-ii's caller**: because neighboring bands' co-rotating
frames drift against each other continuously (not just at cell-crossing moments), a
cell window computed once does not stay geometrically valid forever even if the player
holds still — `USOLRingSubsystem` should treat `ActiveCellWindow`'s result as needing
periodic refresh, not just refresh-on-cell-change. `ReassignPoolSlots` already makes a
refresh cheap (unchanged groups are left alone), so this is not a performance concern,
just a correctness-timing one worth stating explicitly.

Also fixed from the same review pass: `ReassignPoolSlots` is now documented as requiring
`desiredWindow` to contain no duplicate cells (true of `ActiveCellWindow`'s own output,
its only real caller) rather than silently covering a duplicated cell with two slots; a
genuine-duplicate test case was added. `ActiveCellWindow`'s precondition that
`centerCell` already be a valid cell for its own radial band is likewise now documented
rather than silently clamped into a wrong-but-safe answer. Per the review's own
assessment, the per-call heap allocations and absurd-input (near-`INT32_MAX`) integer
overflow robustness in both functions are not practical concerns at this project's real
ring scales and call frequency, and are not fixed here — consistent with Amendment 7's
same judgment on `GenerateCellRocks`'s allocation shape.

**A further real finding surfaced while proving the fix**: a radius-R window only
guarantees real-world coverage within (R - 0.5) cells of the player, not R cells — the
player can sit anywhere within their own cell, up to half a cell off the center the
window is built around. `SOL::RING_PATCH_WINDOW_RADIAL_RADIUS`/`_ANGULAR_RADIUS` were
raised from 1 to 2 as a result (5x5 = 25 cells/ring, ~2,500 pool entities/ring, ~10,000
across all 4 rings if every ring somehow had an active patch simultaneously — still well
inside the headroom Amendment 5 measured), giving ~1.5 cells (1.5 km) of real margin
instead of ~0.5, a buffer against the player (or a fast warp-speed pass) outrunning the
per-frame reassignment check before a needed cell has real rock data.

**A Mass-architecture gotcha caught while implementing `USOLRingSubsystem` itself** (not
by a dispatched review this time — the main session caught it while writing the spawn
code, before any build): `ASOLAsteroidBeltVisuals`'s existing Mass query matches *any*
entity with `FSOLMinorBodyRenderFragment`/`FSOLMinorBodyStateFragment` and a const-shared
`FSOLMinorBodyAppearanceFragment` present - it has no way to tell "a belt entity" from
"any other entity with the same fragment composition," because nothing distinguished
them until now. Giving ring-pool entities an appearance fragment at all (regardless of
which `SOLMinorBodyVariant` value) would make the belt's already-shipped visuals actor
pick them up too, writing their transforms into the belt's own ISM component using the
ring pool's unrelated `InstanceIndex` values - silently corrupting both populations'
rendering. **Fixed by giving ring-pool entities NO appearance fragment at all**:
`USOLMinorBodyOrbitProcessor`'s query only requires `Orbit`+`State` (confirmed by
reading its `ConfigureQueries`), so the shared orbit-placement processor still places
ring entities correctly without one. This is sufficient for 5e-ii (no ring renderer
exists yet to need an appearance value), but **5e-iii must solve this properly before
`ASOLRingVisuals` can exist**: the natural fix is a Mass tag fragment (e.g.
`FSOLBeltRockTag`, an empty marker type) added to the belt's archetype and required by
`ASOLAsteroidBeltVisuals`'s query, so ring entities (which won't carry that tag) are
excluded by construction rather than by the current accident of "nobody gave them an
appearance fragment yet." Tracked here explicitly so 5e-iii's design doesn't rediscover
this from scratch or skip it.

## Amendment 10 — adversarial review of `USOLRingSubsystem`

The review verified, by tracing actual engine/Mass code rather than trusting comments,
the two claims the subsystem's correctness most depends on: `USOLMinorBodyOrbitProcessor`
really does place ring entities without an appearance fragment (its query only requires
`Orbit`/`State`), and `ASOLAsteroidBeltVisuals`'s `AddConstSharedRequirement` really does
hard-exclude an entity missing that fragment at the archetype level (not a soft/heuristic
match) — confirming Amendment 8's late finding is a real, structural fix, not a fragile
accident. It also confirmed the Mass-entity bookkeeping (pool layout, group reassignment,
gap-cycling, teardown ordering) and the pole-direction/spin-phase-independence claim are
all correct as implemented. Headless verification (a clean 66-second `-SOLSmokeFlight`
run, all 4 rings' pools spawned, zero errors) was judged sufficient for 5e-ii given the
subsystem's own Mass-entity-management code has no dedicated unit test — same standard
already accepted for `USOLMinorBodySubsystem` in 5c — though the review correctly noted
this only exercises the "player not in any ring" path; `AssignGroup`'s live
cell-reassignment path is still unexercised outside the (already-passing) pure
`SOLRingPatch` unit tests until 5e-iv's planned screenshot verification gives a renderer
to actually look at.

One real bug and several real but non-blocking findings:

- **Fixed**: cells beyond a ring's inner/outer edge (within the activation margin,
  `ComputeActiveCell`/`ActiveCellWindow`'s admitted band extends a few cells past
  `[InnerRadiusM, OuterRadiusM]`) were getting full-density rocks, since nothing told
  `GenerateCellRocks` those radii are outside the ring at all. `BuildRingState` now adds
  two synthetic gap bands, `[0, InnerRadiusM)` and `[OuterRadiusM, DOUBLE_MAX)`, to every
  ring's `GapBands`, so the existing gap-fraction math naturally thins (and fully empties)
  any cell straddling or beyond an edge — no new mechanism needed, just correct data.
- **Fixed**: `USOLRingSubsystem::UpdateRings` moved from `USOLAnchorSubsystem::
  OnBodiesUpdated` to `USOLShipSubsystem::OnShipsStepped` (broadcast at the end of the
  ship's own step, strictly after its position is current), so the ring update always
  reads this frame's real ship position rather than whatever stale position happened to
  be set before `OnBodiesUpdated`'s listeners ran in whatever order they're bound. This
  does not fully resolve the update's ordering relative to
  `USOLMinorBodySubsystem`'s orbit-processor run (both still exist as separately-bound
  listeners with no enforced relative order) - tracked as known tech debt in `CLAUDE.md`,
  the same class of issue as `OnUniverseUpdated`'s already-acknowledged ordering gap.
- **Fixed**: the ring plane normal (`RingPlaneNormal`) is now cached per ring on first
  computation rather than recomputed (a `SinCos`+`RotateVector`) every `UpdateRing` call,
  matching the caching Amendment 7 asked for; deferred past `BuildRingState` (called
  before the registry's first `Update()`, when orientations may still read as identity)
  to the first real `UpdateRing` call instead.
- **Fixed**: `UpdateRing` now early-outs without allocating when a ring has no active
  cell and no group is currently assigned (the common case for 3 of the 4 rings during
  ordinary flight far from any of them), avoiding `ActiveCellWindow`'s/
  `ReassignPoolSlots`'s per-call allocations for genuinely no-op frames.
- **Fixed (style)**: `"Pan"`/`"Daphnis"` were bare string literals in
  `USOLRingSubsystem.cpp` despite the registry defining the same names; added
  `SOL::BodyNames::PAN`/`DAPHNIS` and used them. The nested `FRingState` struct was
  renamed `FSOLRingState` (every other type in the project carries the `SOL` infix).
- **Documented as tech debt, not fixed here** (both explicitly offered as an acceptable
  alternative to fixing by the review itself, since neither has real profiling evidence
  yet): the ~10,000 ring-rock pool entities are processed by the shared orbit processor
  every frame regardless of whether any ring is active (roughly doubling the minor-body
  processor's workload against the belt's own ~9,268), and `GenerateCellRocks`'s
  per-reassignment heap allocation (re-examining, now that a real caller exists, the
  exact question Amendment 7 deferred to this point) at an estimated reassignment rate
  of up to ~90 calls/second per active ring from co-rotating-frame sweep alone. Both are
  recorded in `CLAUDE.md`'s "Known tech debt" with the review's own rate estimates and
  concrete fixes, to be acted on if 5e-iv's real profiling (once a renderer exists to
  profile against) shows either is an actual problem rather than guessed at now.
- **Noted for 5e-iii, not acted on now** (no renderer exists yet to need it): a group
  left unassigned (player outside any ring) or assigned a cell with fewer real rocks
  than `ROCKS_PER_CELL` (a gap-affected cell, cycling existing rocks to fill the rest)
  keeps stale or duplicated entities with no "inactive" marker; 5e-iii's renderer will
  need to either skip/zero-scale these or `USOLRingSubsystem` will need to grow one
  (e.g. reusing the idle-tag mechanism above). Also noted: `FSOLMinorBodyRenderFragment::
  InstanceIndex` restarts at 0 per ring, so 5e-iii's renderer needs one ISM set per ring
  (already the plan - `ASOLRingVisuals` is one actor per ringed planet) rather than a
  single shared ISM indexed across all four.

5e-ii is now considered fully verified within the limits described above.

## Amendment 11 — 5e-iii far-field Niagara system: implementation notes and a correctness fix

Per Amendment 6's directive, the far-field Niagara system was authored live by the main
session through the editor's Niagara-scripting API (`unreal-mcp`'s `NiagaraToolsets`
plugin), not hand-built in the Niagara editor UI and not delegated to a subagent. This
amendment records the concrete technique, since none of it is visible from reading the
C++ (the asset itself has no unit test, per this SDD's own testing section).

**Stateless-emitter blocker: resolved as moot.** The standard (stateful)
`/Niagara/Modules/Spawn/Location/V2/ShapeLocation` module supports a Ring/Disc shape
primitive directly in a normal `UNiagaraEmitter` (confirmed via its script digest's
keywords: `"Sphere Cylinder Ring Torus Knot Disc Disk Cone Plane Box"`). No Stateless
emitter or template was needed.

**The system: `/Game/SOL/Rings/NS_SOLRingFar`**, one shared system (built from
`/Niagara/DefaultAssets/Templates/Systems/MinimalLightweight`) with one emitter
`RingDust` (built from `/Niagara/DefaultAssets/Templates/Emitters/Minimal`), parameterized
per planet by 10 auto-created Niagara User Parameters: `RingOuterRadiusCm`,
`RingInnerRadiusCm`, `RingParticleCount`, `RingColor`, and three gap-band pairs
`RingGap{0,1,2}{Inner,Outer}RadiusCm` (3 slots is enough headroom — `AllGapBandsM`'s real
data tops out at 3 interior gaps, all on Saturn: the Cassini Division plus Pan's and
Daphnis's shepherd gaps; Jupiter/Uranus/Neptune currently have none). `ASOLRingVisuals`
will set these per instance from `FSOLPlanetRingDef`/`SOLPlanetRing::AllGapBandsM`,
converting meters to centimeters (Niagara's native unit).

**Module stack** (`ParticleSpawnScript`, in order): `InitializeParticle` (Color Mode =
Direct Set, Color linked to `User.RingColor`) → `ShapeLocation` (Shape Primitive =
Ring/Disc, Distribution = Random, Ring Radius linked to `User.RingOuterRadiusCm` — fills
the full disc out to the outer radius) → `KillParticlesInVolume` (Sphere Radius linked to
`User.RingInnerRadiusCm`, not inverted — carves out the inner hole) → three more
`KillParticlesInVolume` instances, one per gap slot (the annulus technique below) → three
*disabled* `KillParticlesInVolume` instances, one per gap slot, that exist solely to
declare a User Parameter (the declare-only trick below). `EmitterSpawnScript` has
`SpawnBurst_Instantaneous` (Spawn Count linked to `User.RingParticleCount`) instead of a
continuous `SpawnRate`, and `ParticleUpdateScript`'s `ParticleState` has "Kill Particles
When Lifetime Has Elapsed" forced off — particles are placed once by the burst and live
forever after, matching Decision 7's "doesn't track individual rock positions" framing at
near-zero ongoing cost. `simTarget` remains `CPUSim` (not yet investigated whether the
toolset exposes a GPU sim target switch; flagged for 5e-iv's performance pass given the
project's "GPU-driven effects over CPU sprites" guideline).

**The annulus technique** (inner cutout and each gap band): `KillParticlesInVolume`'s
`Kill Shape` stock options (Box/Plane/Slab/Sphere/Cone) have no shape for "an annular band
between two radii" directly — Sphere alone only tests a single inside/outside threshold.
The fix exploits that `Kill Volume Enabled` (unlike `Kill Shape`, `Sphere Radius`, etc.)
is a per-particle dynamic `NiagaraBool` input, not a static switch, and that
`SetStackInputData` accepts an arbitrary HLSL expression for any input slot (confirmed by
probing the schema with a deliberately-invalid struct ref, a now-repeated technique for
this API). For a gap band `[inner, outer]`: `Kill Shape` = Sphere, `Sphere Radius` =
linked to the gap's outer-radius User Parameter, `Invert Volume` = false (shape test =
"inside outer radius"), and `Kill Volume Enabled` = the HLSL expression
`length(Particles.Position) >= User.RingGapNInnerRadiusCm` (the inner-radius test). Niagara
ANDs a module's own shape test with `Kill Volume Enabled` (consistent with the field's
name and with every other stock module's "XEnabled" gating convention), so the particle is
only killed when both hold — exactly the band. The plain inner-radius cutout needs only
the shape test (`Kill Volume Enabled` = literal true), since one threshold is sufficient
there.

**The declare-only trick.** A name referenced only inside an `HlslExpression` string
(e.g. `User.RingGap0InnerRadiusCm` above) does **not** auto-create a Niagara User
Parameter the way a `Linked` input binding does — this was caught empirically (a quick
in-level `NiagaraComponent` placed via `SceneTools.add_to_scene_from_asset` and queried
with `NiagaraToolset_Component.GetUserVariables` showed the outer-radius parameters
present but the inner-radius ones completely absent, meaning every gap band was silently
testing against an unresolved/default-zero inner bound). The fix: one extra
`KillParticlesInVolume` instance per gap slot, permanently disabled (`Kill Volume
Enabled` = literal false, so it can never affect simulation), whose `Sphere Radius` is
linked to the same `User.RingGapNInnerRadiusCm` name purely to force Niagara to create it
as a real, exposed User Parameter. This is the only known way to pre-declare a parameter
that's otherwise referenced solely inside expression text through this API; no dedicated
"add User Parameter" or "Set Parameters" stack-item function was found despite searching
(`AddUserParameter`/`SetUserParameterValue`/`SetUserParameterDefaultValue` are all
"Unknown tool", and routing through `SetStackInputData` with an empty module reference
errors with "Module name not specified in stack reference" — System-level parameter
declaration has no exposed entry point other than piggybacking a real module field).

**A real correctness bug caught during this verification, not yet confirmed fixed by a
screenshot.** The system's default `SystemState` module (`SystemUpdateScript`, present on
every system by default, not something `AddModule` needed to add) had its template
default `Loop Behavior`, which is **not** infinite — a finite `Loop Duration` after which
the system goes `Inactive`. Combined with `ParticleState`'s lifetime-kill already being
off, this meant the *particles* would have lived forever but the *system* would have
deactivated itself shortly after the one-time burst anyway, which in an editor smoke test
left an already-placed `NiagaraComponent` stuck permanently `IsActive() == false` after
its first (short) loop completed — reactivating it from Python during the same session did
not reliably flip it back on mid-session, strongly suggesting the real game would have
hit the same thing after a few seconds. **Fixed**: `SystemState`'s `Loop Behavior` is now
set to `Infinite` (confirmed index 0 of `ENiagara_EmitterStateOptions` maps to exactly
that display name), so the system never goes inactive and the one-time burst's particles
keep rendering indefinitely, as originally intended.

**Still open / deferred to 5e-iv:**
- The `Kill Volume Enabled`-ANDs-with-shape-test assumption above is inferred from the
  field's semantics and naming convention, not confirmed by a screenshot — an editor-only
  smoke test (placing the system in the current level, overriding its User Parameters,
  and screenshotting) was attempted but blocked by the `SystemState` bug above eating the
  session's time; by the time that was fixed, the test actor had already been cleaned up
  rather than re-attempted, since 5e-iv's own PIE screenshot pass (with `ASOLRingVisuals`
  setting real per-planet values) is a strictly better environment to verify this in. If
  gaps don't show up correctly there, re-check this assumption first.
- No sprite material assigned yet (still the `Minimal` template's default renderer
  material); no per-planet material/texture decision made.
- `InitializeParticle`'s `Lifetime`, `Position Mode`, `Sprite Size Mode`/size and other
  fields are all still at template defaults, unreviewed for suitability at ring scale.
- No mechanism found for setting the asset's own System-level default/preview values for
  the ten User Parameters (low priority — `ASOLRingVisuals` sets real values at runtime
  regardless).

## Amendment 12 — 5e-iii `ASOLRingVisuals` and its adversarial review

Built `ASOLRingVisuals` (near-field ISM over `USOLRingSubsystem`'s pool, far-field
`UNiagaraComponent` on the shared `NS_SOLRingFar` system), the `FSOLBeltRockTag`/
`FSOLRingRockTag` Mass tags, and wired `ASOLGameMode` to spawn one instance per
`SOLPlanetRing::RealRings()` entry. PIE-smoke-tested (all 4 rings spawn, zero `LogSOL`
errors) and the full test suite re-run clean (400/400) before the required fresh
adversarial review. The review found three high-severity bugs in the far-field placement
that neither the smoke test nor the unit tests could have caught (none of them touch
Mass data or produce a log error - they are purely about what ends up on screen), plus a
real `bActive` default-value bug and several smaller issues. All are fixed; the fixes are
summarized here since none of them were visible from reading Amendment 11 alone.

**The most consequential finding: `RingDust`'s `bLocalSpace` was `false`.** Checked
directly against the live asset (`GetEmitterData`, not inferred) and confirmed to matter:
with a world-space emitter, the one-time burst's particles are baked at absolute
positions relative to wherever the component happened to be at the moment it first
activates, and never move again — `ASOLRingVisuals` moving the component every frame to
track its planet would have left the entire ring visually stranded at its spawn position
(effectively near the Unreal origin) instead of following the planet at all, and the
gap-band kill expressions (`length(Particles.Position) >= User.RingGapN...`) would have
measured distance from world/LWC origin rather than from the ring's own center. Fixed by
`SetEmitterData({"bLocalSpace": true})` against the live asset (confirmed by reading it
back, and that no other field changed) and saved to disk. This should have been verified
during Amendment 11's own authoring session and was not; the asset's own name table
listing `bLocalSpace` as a field was noticed but not confirmed to be `true`.

**Two more high-severity bugs, both in `PlaceFarFieldNiagara` (`FillTransforms` at the
time):**
- The far-field component's position was log-compressed with distance (via
  `BodyPlacement`, correctly matching every other rendered body), but nothing compensated
  the component's *scale* — the annulus kept its true centimeter size from the Niagara
  system's own User Parameters regardless of distance, so a ring viewed from realistic
  range (e.g. Saturn as seen from Earth's distance) would have rendered roughly 770x too
  large relative to its own planet. Fixed by calling `BodyPlacement` with the ring's real
  `OuterRadiusM` (not `0.0`) and using `placement.RadiusCm / (OuterRadiusM * METERS_TO_CM)`
  as the component's uniform `SetWorldScale3D` — the same compression ratio a body mesh's
  radius gets, applied to the ring.
- The far-field rotation fed the ring-plane normal (ecliptic axes, correct for
  `DistanceOutsideRingVolumeM`'s own math) directly into `FindBetweenNormals` against
  Unreal's `UpVector`, mixing coordinate frames the way `SOLBodyVisuals.cpp` explicitly
  converts before avoiding. Fixed by caching `SOLRender::EclipticToUnreal(planet's
  orientation)` once in `BeginPlay` as `mFarFieldRotation` and using that directly (also
  resolving a separate minor finding: recomputing an unchanging rotation, through a lossy
  quat→rotator round trip, every single frame).

**One correctness bug in the Mass side, caught by the same review:**
`FSOLMinorBodyRenderFragment::bActive` defaulted `true` and `SpawnRingPools` never set it
false at spawn, so a ring-pool group that was never assigned a cell (or any cell with
fewer rocks than the group has entities, which reused `rocks[slot % rocks.Num()]` to fill
the rest) rendered `RingSafeDefaultElements`'s placeholder orbit or a duplicate instance
instead of nothing. Fixed in both places: `SpawnRingPools` now explicitly sets
`render.bActive = false` per entity at creation, and `AssignGroup`'s gap-reduced-cell path
marks excess slots inactive instead of duplicating. The fragment's own default stays
`true` (correct for belt asteroids, which never touch this field) - only the ring
subsystem's spawn path needed the explicit set.

**One design inconsistency, fixed**: `SOLRingPatch::ComputeActiveCell` admitted a margin
only on the radial band, not vertically, so a player approaching a ring from above/below
would see the near-field pool populate (and therefore render at near-full near-field
alpha) only once already inside the ring's real physical half-thickness, with no room for
`ComputeNearFieldAlpha`'s fade to actually run - contradicting `ASOLRingVisuals`'s own
"the margin is a single source of truth shared by both" framing. Fixed by applying
`marginM` to the vertical thickness test too (`ComputeActiveCell`'s `bWithinThickness`),
matching the radial treatment exactly; `SOLRingPatchTest.cpp`'s boundary cases were
updated for the new (wider) admission band.

**One performance fix**: the near-field query/upload (up to ~10,000-entity iteration,
2,500-instance `BatchUpdateInstancesTransforms`, per actor, every frame) ran
unconditionally even when a ring's near field had been fully faded for a long time (the
common case - realistically only one ring is ever near the player at once).
`ASOLRingVisuals` now tracks the previous frame's alpha and skips the near-field work
entirely once both the previous and current alpha are 0, always still re-placing the
(cheap) far field.

**Smaller fixes**: the far-field gap-slot User Parameter names are now six explicit
`SOLConstants.h` constants (with a `static_assert` tying the array to
`RING_FAR_FIELD_GAP_SLOT_COUNT`) instead of a prefix string concatenated with
`FString::Printf` every `BeginPlay`; `FarFieldNiagara` now sets `bAutoActivate = false` in
the constructor and calls `Activate(true)` explicitly after every User Parameter is set,
rather than relying on auto-activation's timing relative to `SetVariable*`; uninitialized
scratch transforms default to zero scale instead of `FTransform::Identity` (a never-
written slot would otherwise show a unit-scale sphere at the Unreal origin); the
`Paths::RING_FAR_NIAGARA_SYSTEM` constant gained its `.NS_SOLRingFar` object-path suffix,
matching every other `Paths` constant's convention; several stale/incomplete comments
were corrected (one of which had already drifted from true to false again within this
same session, caught only by review).

**Not caused by this part**: a recurring PIE log line ("Handled ensure:
`IsInGameThread()` failed... `FAppTime`") was traced to Niagara system compilation inside
the editor generally (reproduced by an unrelated scratch Niagara system, before any
`ASOLRingVisuals` existed) - an engine-side, render-thread issue with zero `SOLTest` call
stack frames, not something this part's C++ causes or can fix.

5e-iii is complete: Mass tags, `bActive`, `ASOLRingVisuals`, and the far-field Niagara
asset's `bLocalSpace` correction are all in place and re-verified (PIE smoke test,
400/400 automated tests). 5e-iv's screenshot verification remains the first real visual
check of the near/far cross-fade and the gap bands now that the placement math is correct.

## Amendment 13 — far-field sprite size, a real unity-build bug, and a headless-runner infra issue

Added a per-ring far-field sprite size instead of a fixed one: `RING_FAR_FIELD_PARTICLE_COUNT`
is the same 4000 for every ringed planet, but the rings' widths/circumferences differ by
roughly 10x (Saturn vs. Jupiter), so a single fixed sprite diameter either left visible gaps
on the widest ring or over-fattened the narrowest one. `SOLRingPatch::FarFieldSpriteSizeCm`
(`SOLRingPatch.h`/`.cpp`) models the burst's particles as scattered uniformly at random over
the ring's annulus area, takes the typical nearest-neighbor spacing
(`sqrt(area / particleCount)`), and scales it by the new `SOL::RING_FAR_FIELD_SPRITE_OVERLAP_FACTOR`
(2.0, implementer judgment) so sprites overlap into a continuous band. Wired into
`ASOLRingVisuals::BeginPlay` via a new 11th far-field User Parameter,
`SOL::RingFarFieldParams::PARTICLE_SIZE_CM` (`RingParticleSizeCm`), set alongside the other
ten from Amendment 11. Covered by 8 new TDD unit tests in `SOLRingPatchTest.cpp` (equal/
inverted radii, Jupiter/Saturn scale, monotonic-in-area, zero/negative particle count, no
inner hole). The Niagara asset side (`NS_SOLRingFar`) was initially missing: the first commit
of this amendment claimed the `RingParticleSizeCm` User Parameter and sprite-size wiring
existed, but a later inspection via unreal-mcp showed the asset had only the original ten User
Parameters, and `SetVariableFloat` on a nonexistent parameter silently does nothing, so the
earlier PIE "no error" check proved nothing about the asset. Fixed by adding
`User.RingParticleSizeCm` (float, default 100) to the asset, setting `RingDust`'s
`InitializeParticle` Sprite Size Mode to Uniform, and linking Uniform Sprite Size to the new
parameter; the system compiles with zero errors/warnings.

**The far field was also invisible at runtime, for four independent reasons, all fixed in the
asset (verified by a real Saturn screenshot, `-SOLStart=Saturn -SOLAltitudeKm=250000
-SOLLookAt=Saturn -SOLSmokeShot`, plus PIE test rings):** (1) `ShapeLocation` used Ring/Disc with
Disc Coverage 0, which spawns particles on the outer circle only, and Hexagon mode rendered in the
wrong plane; it is now a Cylinder (radius = `RingOuterRadiusCm`, height 1 cm, so a flat circular
disc in the XY plane that matches the C++ orientation). (2) `UseOwnerScale` was on, so positions
were scaled twice (the emitter is local-space and the component scale already applies at render
time); it is now off. (3) The inner-hole and gap `KillParticlesInVolume` modules misbehave
whenever the component scale is not 1 (a hole of radius r removed everything at scale 2 once r*2
reached the outer radius), and the far field is always scaled by `placement.RadiusCm /
OuterRadiusCm`; all seven kill modules were removed and replaced by one SetParameters module that
sets `Particles.SpriteSize` to zero for any particle whose local-space radius falls in the hole
or a gap (scale-independent; a zero-size sprite is not drawn). (4) `SpawnBurst_Instantaneous`
looped every second with particles that never die, so the count grew by `RingParticleCount` per
second without bound; it is now limited to one loop. Remaining visual polish (still deferred with
the placeholder-appearance work): the default additive sprite material saturates to white where
sprites overlap, and 4000 random sprites clump, so the band is lumpy rather than smooth.

**A real bug found in passing, not caused by this change**: `ASOLAsteroidBeltVisuals.cpp` and
`ASOLRingVisuals.cpp` each had an identically-named file-local `MakeVisualOnlyPrimitive`
helper in its own anonymous namespace, deliberately kept separate per Amendment 12's comment
("small enough that sharing it is not worth a new shared header"). That assumption was wrong:
Unreal's adaptive unity build can place both `.cpp` files in the same translation unit, and an
anonymous namespace only prevents a symbol collision *within* one TU boundary, not across the
two files once merged into one — so the two identical definitions failed to compile the first
time both files landed in the same unity blob. Fixed by extracting the one real implementation
into a new shared header, `SOLMinorBodyVisualsUtil.h`, with both call sites updated.

**A headless test-runner infrastructure issue, worth recording since it cost significant time
to isolate**: `Tools\RunTests.ps1`'s `UnrealEditor-Cmd -ExecCmds="Automation RunTests ..."`
path reproducibly stalled on this machine, every time, at the identical point in startup
(immediately after `CleanupOrphanedCacheFiles`/DDC maintenance, before any automation-related
log line ever appeared) — with no error, and CPU usage that kept climbing, meaning it was not
a simple deadlock. Neither pausing the system VPN nor closing Rider (both plausible interferers
with RiderLink/the embedded `ModelContextProtocol` server) changed the stall point, ruling both
out. Running with a real RHI instead of `-nullrhi` got one line further (a `LogPSOHitching`
line, impossible under `-nullrhi`) before stalling again, suggesting the headless path never
reaches the automation-controller/worker handshake at all. The root cause was not found.
**Workaround, used for this part's verification and recommended for future parts until the
headless runner is fixed:** drive the already-open interactive Editor instead, via
`mcp__rider__ue_execute_python` to invoke the `Automation RunTests <filter>` console command
and `mcp__rider__ue_get_logs`/`ue_status` to poll `LogAutomationController` for `Test Started`/
`Test Completed` lines. In the interactive editor the test queued and ran via Unreal's own
`FWaitForInteractiveFrameRate` latent gate (which simply waits for editor FPS ≥ 10, logging its
wait every 30s, timing out at 10 minutes) — confirming the interactive path's automation
pipeline itself works correctly; only the headless command-line path is broken. All 8 new
`SOLTest.RingPatch.FarFieldSpriteSize*` tests passed this way, and a PIE smoke test (all 4
ringed planets spawn, zero `LogSOL` errors, `RingParticleSizeCm` set without error on each)
confirmed the runtime wiring. This is new, not-yet-triaged tech debt: the headless runner
should be restored to a known-working state (or the actual root cause found) before relying on
it for an unattended/CI-style run again.

## Amendment 14 — realistic rings: three-tier GPU redesign (grill-me, 2026-10-08)

**Why.** Real PIE screenshots at Saturn (120,000 km and 30,000 km altitude) showed the 5e-iii
far field reading as giant overlapping white blobs: 4,000 additive sprites sized to overlap on a
ring area that large, so the ring gaps are unreadable and the planet limb is hidden. The user
asked for rings that look like Epic's **Cassini Sample Project** (UE 5.5, PCG), whose Saturn
rings are millions of real static meshes spawned with PCG GPU compute and GPU Scene
Instancing — not particles — "and maybe the same technique if it's efficient enough".

**Decisions (grill-me, all answered by the user):**

| # | Decision | Resolution |
|---|---|---|
| 1 | Technique | Hybrid: Niagara mesh particles at range, PCG GPU instancing for the dense layer, "visually like Cassini". |
| 2 | Existing near-field pool | Keep the ~2,500 Mass rocks per ring for the closest rocks (collision-ready, exact positions; preserves `Docs/ToDo/asteroid-ring-collision.md`). Add a non-collidable GPU-instanced dense layer. The user also asked to combine Mass and PCG where possible: the Mass pool stays authoritative for the nearest rocks and the PCG layer fills the density around and beyond them. |
| 3 | Dense layer feature | PCG runtime GPU "Spawn Static Mesh" (5.8). The 5.6-era forum report of GPU-spawned meshes vanishing after a graphics-settings change is unconfirmed fixed in 5.8; the 5.8 plugin does ship `PCGStaticMeshSpawnerKernel`. The first implementation step verifies it empirically (visible at scale, survives a settings change, sits correctly under floating origin on the ring actor); failure is recorded here and the dense tier falls back to Niagara mesh particles. |
| 5 | Rock meshes | Generated procedurally in-project (about 6 Nanite variants under `/Game/SOL/Rings/`), shaped with Cassini / Fab / Quixel rocks as visual reference. |
| 6 | Budget | Match Cassini: about 5M instances for Saturn, scaled by ring area for the other three; tuned against `stat gpu` / Insights, counts as `SOLConstants.h` constants. |
| 7 | Tier handover | Best performance that keeps realism (user's call), so: A = Niagara far field for the whole-ring view from outside, B = PCG GPU dense layer when the camera is near or inside the ring, C = Mass rocks closest; cross-faded through the existing `ComputeNearFieldAlpha` mechanism with thresholds as constants. |

**Open / unresolved.** PCG's behavior under floating-origin rebasing and ring co-rotation is
unproven (verified in the spike below). The sprite look of tier A (soft non-additive material,
count) is retuned as part of this work.

### Amendment 14 — PCG spike findings (2026-10-08, concluded: PCG GPU spawning rejected for rings)

What was learned while trying to prove the PCG GPU Spawn Static Mesh path in 5.8.3:

- PCG is enabled by default in 5.8.3; `PCGStaticMeshSpawnerKernel` ships. The `PCGToolset` MCP toolset can create
  graphs (`CreateGraph`, `AddNode`, `ConnectNodePins`, `SpawnGraphInstance`, `ExecuteGraphInstance`).
- **`Create Points Grid` cannot run on the GPU** (`PCG GPU compiler: ... did not emit any kernels`), so a GPU ring
  needs a `Custom HLSL` `PointGenerator` kernel as the point source.
- **A Custom HLSL node's shader source is not reachable from Python or the MCP toolset** (`ShaderSource` is a plain
  `UPROPERTY()`; `UpdateNode` reports "could not be set"). New editor-only helper `USOLEditorAuthoring`
  (`Source/SOLTest/Tools/`) sets it through reflection and fires `PostEditChangeProperty`; verified working from
  editor Python (`unreal.SOLEditorAuthoring.set_object_string_property`).
- The default generator kernel template fails to compile (`In_GetBoundsMax` undeclared) until a source is supplied.
- With a hash-based annulus kernel (1M points) feeding a GPU Static Mesh Spawner, `ExecuteGraphInstance` ran with no
  errors and the component reported `generated`, but no ISM components were listed on the level, `GetNodeDataView`
  has no data for GPU nodes, and the editor viewport screenshots were black (viewport not rendering, so this is
  **not yet verified**). GPU Scene-written instances may legitimately not appear as ISM components.
- **Root cause of "nothing generated":** the `Gen` node's `KernelType` had been set to `PointGenerator` by plain
  property assignment, which left the default **Required** `In` input pin in place with no edge. PCG culls a node
  whose required input is unconnected, so the whole graph silently produced nothing (no errors, `generated` still
  true). The engine's own tests call `UPCGCustomHLSLSettings::SetKernelType`, which empties `InputPins` for
  generators (see the comment in `Tests/Compute/PCGCustomHLSLTest.cpp`). Fix from Python: `settings.set_editor_property('input_pins', [])`.
  A generator sizes its output from `NumElements` directly, so the output pin's `ElementCountMode` is irrelevant.
- **Working graph** (`/Game/SOL/Rings/PCG_SOLRingSpike`, saved at GPU/1M): `Gen` (Custom HLSL, `PointGenerator`,
  `NumElements` 1000000, `bExecuteOnGPU`, **no input pins**) -> `Spawner` (Static Mesh Spawner, weighted selector,
  `/Engine/BasicShapes/Sphere`, `bExecuteOnGPU`) -> Output. The kernel PCG-hashes `ElementIndex` twice to (U,V),
  sets `R = sqrt(lerp(RMin², RMax², U))` with RMin/RMax 20000/100000 cm and angle `V·2π`, centres on
  `0.5·(GetComponentBoundsMin()+GetComponentBoundsMax())`, then `Out_SetPosition`, `Out_SetScale(20)` and `Out_SetDensity(1)`.
  Both kernels compile (`Gen_PCGCustomHLSLKernel`, `PCGStaticMeshSpawnerCS`: SM6 success). The GPU spawner
  outputs one `UPCGProceduralISMComponent` (`NumInstances` 1,000,000, world bounds = the volume's bounds).
- **Proof 1, visible at 1M: PASS.** In PIE, 1M GPU instances render as a solid annulus with the inner hole
  showing the star field (`Saved/Screenshots/WindowsEditor/RiderMCP/20261008-192628_viewport.png`,
  `...-193032_viewport.png`). The CPU spawner at 20k gives the same placement (`...-192423_viewport.png`).
  Cost: the generation frame shows `ComputeFramework::ExecuteBatches` at about 50 ms, a one-off spike. Steady-state
  per-frame cost was not cleanly isolated (`stat gpu` averages over that window: BasePass about 9 ms, PrePass about 8.5 ms with
  1M default spheres; numbers are rough).
- **Proof 2, survives a graphics-settings change: FAIL.** With PIE paused (so nothing else ticks),
  `r.ScreenPercentage 50` left the ring intact, but **`scalability 1` erased it instantly** (`...-192928_viewport.png`).
  It did not come back after restoring `scalability 3` and resuming. Engine source explains why:
  `UPCGProceduralISMComponent::OnRenderStateDirty` says "there is no explicit persistence of instance data in the
  GPU scene. When this component is dirtied, the instance data is cleared". Its auto-refresh of the owning PCG
  component is `WITH_EDITOR`-only (and did not recover it in PIE either). This is the 5.6 bug class, still present in 5.8.3.
- **The instances also vanish spontaneously while the game runs.** About 20 s after generation with no input, the ring
  disappears (`...-192653_viewport.png`), while the component still reports 1,000,000 instances. With PIE paused
  it survives indefinitely (`...-192834_viewport.png`, 45 s+). So some game-side activity (likely a GPU-scene
  rebuild or reallocation from our per-frame ISM/Niagara/body updates) discards the GPU-only instance data. The exact trigger
  was not bisected.
- **Proof 3, correct under actor movement: FAIL.** The procedural ISM is created with **Static** mobility. When the
  volume's root is Movable it cannot attach (`AttachTo: ... is not static, cannot attach ... PISM_Sphere_N which
  is static to it. Aborting.`), so it sits unattached at the world origin with instance transforms baked in world
  space at generation time. Moving the volume by 500 m did not move the ring (bounds stayed at the old position,
  `...-193046_viewport.png`). Following a ring actor that moves every frame (floating origin plus co-rotation) would
  need a full regeneration (about 50 ms of compute) every frame.
- **Conclusion:** PCG GPU Spawn Static Mesh does produce visible instances at 1M in 5.8.3, but it is not usable for
  the rings. GPU-only instance data is not persistent (it is lost on scalability changes and spontaneously in our running game),
  and it is static world-space data that cannot follow a moving actor. **Recommendation:** use the Niagara mesh-particle
  fallback for the ring tiers (the simulation is GPU-resident and re-creatable, and it follows a moving component in local space).

## Amendment 15 — Niagara mesh-particle ring tiers: asset results and host contract

Asset work for Amendment 14's Niagara fallback (2026-10-08). The assets were authored live in the editor through
the `NiagaraToolset_System` MCP tools, editor Python and GeometryScript. **No C++ was changed.** The host changes
required are listed below. Everything is saved under `/Game/SOL/Rings/`.

### Assets

| Asset | What it is |
|---|---|
| `SM_SOLRingRock_0..5` | Procedural rocks: a subdivided icosahedron with random lumps and dents, 1-4 plane-cut facets and a per-variant aspect ratio. Flat-shaded, with mean radius 100 cm (so particle scale = rock radius in metres). Variants 0-3 have 80 triangles, 4-5 have 320. No Nanite, no collision, box-projected UV0. |
| `M_SOLRingRock` | Default-lit, opaque. Base colour = Particle Colour × lerp(0.55, 1.1, Particle Random). Roughness 0.88, specular 0.3, emissive at 5% of base colour so the unlit sides are not pure black under the single shadowless Sun. Usage flags: Niagara mesh particles and ISM. |
| `SM_SOLRingDisc` | Flat annulus from radius 44 to 102 cm: 512 segments × 5 radial rings (5,120 triangles), +Z normal, planar UV0. |
| `M_SOLRingFar` | Unlit, translucent, two-sided ring-profile material (one Custom HLSL node; details below). Scalar parameter `Gain` = 0.8. |
| `NS_SOLRingFar` (Tier A, rebuilt) | One CPU emitter `RingDust`: a burst of 1 particle that never dies. Its mesh renderer draws `SM_SOLRingDisc` with scale = `RingOuterRadiusCm` × 0.01. Dynamic Material Parameters 0-2 carry the inner, outer and gap radii (normalised by the outer radius), the profile index, the Sun direction and the planet radius. Bounds are Dynamic. The empty `Minimal` emitter, ShapeLocation and the sprite renderer were removed. |
| `NS_SOLRingDense` (Tier B, new) | One GPU-sim emitter. A burst of `User.RingDenseParticleCount` spawns once and lives forever. **Spawn** assigns a random anchor in a (2W)² tile with Gaussian thickness, a radius from a power law (q = 3, `r = rmin / sqrt(1 - u(1 - (rmin/rmax)^2))`), a random aspect ratio (0.75-1.25, 0.75-1.25, 0.6-1.1), a random orientation quaternion and a seed. **Update** wraps the anchor toroidally around the camera and sets scale to 0 when the rock falls outside the annulus, inside a gap, above the fill fraction, or below the angular-size cull. Scale also fades to 0 over the outer 20% of the window, and invisible rocks get VisibilityTag 1. The mesh renderer uses `SM_SOLRingRock_1` with frustum culling on, motion vectors disabled and no shadows. Bounds are fixed at ±6e5 cm. |

`M_SOLRingFar` computes the radius per pixel from the interpolated local position, so gap edges are exact at any
scale. The radial profiles come from a 52-band optical-depth (tau) table:

- **Saturn:** D ring; C ring with the Colombo and Maxwell gaps; B ring; the Cassini Division with the Huygens ringlet; A ring with the Encke and Keeler gaps.
- **Uranus:** the zeta ring, nine narrow rings and the epsilon ring.
- **Neptune:** the Galle, Le Verrier, Lassell, Arago and Adams rings, including Adams's arcs.
- **Jupiter:** the main ring.

How the material shades the profile:

- **Anti-aliasing:** each band is box-filtered over the pixel's radial footprint, and flagged narrow ringlets are kept at least 1.25 px wide.
- **Fine structure:** up to 9 octaves of radial noise modulate tau and albedo. Each octave fades out before it would alias.
- **Lighting:** Lommel-Seeliger layer reflectance on the lit side, and diffuse transmission on the unlit side.
- **Opacity:** `1 - exp(-tau / mu)`, so the planet and stars show through the thin parts.
- **Shadow:** an analytic planet-shadow cylinder.

The generator script for the table and HLSL (`ringprof.py`) lives in the scratchpad. The HLSL is stored in the material itself.
`PCG_SOLRingSpike` is still present (Amendment 14 history); nothing references it.

### Tier A user-parameter contract (`NS_SOLRingFar`)

- **Unchanged and still read:** `RingOuterRadiusCm`, `RingInnerRadiusCm`, `RingColor`, `RingGap{0,1,2}{Inner,Outer}RadiusCm`.
- **Still declared but no longer read:** `RingParticleCount`, `RingParticleSizeCm`. Setting them is harmless. `SOLRingPatch::FarFieldSpriteSizeCm`, `RING_FAR_FIELD_PARTICLE_COUNT` and `RING_FAR_FIELD_SPRITE_OVERLAP_FACTOR` are now dead code. They can be removed together with their 8 tests, or kept until a cleanup pass.
- **New, all optional with safe defaults:**
  - `RingProfileIndex` (int, default -1): 0 Jupiter, 1 Saturn, 2 Uranus, 3 Neptune. When it is -1, the asset infers the profile from `RingOuterRadiusCm`, so the current C++ already gets the right profile.
  - `RingSunDirection` (vec3, default zero): unit vector toward the Sun in ring-local axes. When it is zero, the ring is lit on whichever side the viewer is.
  - `RingPlanetRadiusCm` (float, default 0): radius used for the planet's shadow on the ring. When it is 0, a per-profile radius is used.

### Why the far field is one disc, not sprites

Every sprite variant has the same two failures, whether additive or soft-blended. Overlapping sprites saturate,
and random clumping blurs gap edges that are only 0.03-0.3% of the ring radius wide (Amendment 13's screenshots
show both). A single mesh particle whose pixel shader evaluates the radial profile analytically avoids both. It
gives exact gaps at any distance, fine banding, and real translucency: stars and the planet show through the
C ring and the Cassini Division. It costs one draw call and a fraction of a millisecond of translucent pixels.

### Tier B: the camera-local wrapped window

N rocks are assigned fixed anchors in a tile of side 2W. Each frame the GPU places rock i at
`wrap(anchor_i - phase)`, inside [-W, W]² around the camera. Because `phase` is the camera's ring-frame position
modulo 2W, every rock keeps a fixed ring-frame position: the field is an infinite periodic carpet. A rock leaving
one edge reappears at the opposite edge, where the 20% edge fade has already hidden it. Nothing is ever
respawned, so nothing flickers.

Particle positions stay small (|x| ≤ W), so float precision holds even though the ring is 1e10 cm from the
planet centre. Only the annulus and gap test uses `RingWindowCenterCm + local`. That value carries about 10 m of
float error at 1e10 cm, which is negligible against gaps of 42 km or wider.

### Measurements

All rows are one `ProfileGPU` capture each: PIE in the editor viewport at 3056×1319 (TSR internal 1825×760). The
camera sat 5 m above a B-ring patch, with W = 1.5 km, a 20 m half-thickness and rock radii of 0.25-6 m. Times are
GPU milliseconds.

Absolute frame times varied up to about 2× between captures with GPU clocks and power state. Compare passes
within a row rather than frame totals across rows.

| Config | Niagara sim | Mesh-instance update + cull | BasePass | Velocity | Frame |
|---|---|---|---|---|---|
| No ring (baseline) | - | - | 0.66 | 0.08 | 27.4 |
| 500k, 6-mesh array (MeshIndex) | 2.45 | 6 × ~1.3 + 5.5 | 4.1 | 2.7 | 56.4 |
| 500k, 1 mesh, 320 tris | 2.33 | 1.51 + 0.95 | 11.1 | 9.1 | 51.3 |
| 500k, 1 mesh, 80 tris | 0.90 | 0.22 + 0.20 | 1.6 | 1.3 | 13.9 |
| 2M, 80 tris, cull 4e-4 rad | 1.19 | 0.80 + 0.71 | 5.5 | 5.0 | 26.0 |
| **1M, 80 tris, cull 1e-3 rad, plus the Tier A disc** | **0.56** | **0.28 + 0.21** | **0.57** | **0.45** | **7.4** |

The Tier A disc adds 0.09 ms of translucency in that last capture.

**Conclusions**

1. **The mesh renderer's multi-mesh array is a performance trap.** Every mesh entry runs the instance update and
   culling over all N particles, so six meshes cost about 6× in those passes. Tier B therefore uses a single
   80-triangle mesh. Variety comes from the per-particle aspect ratio and orientation, and at a few pixels per
   rock it cannot be told apart from six meshes. If more variety is wanted, add a second emitter rather than
   using the array: for example the 320-triangle rocks 4 and 5 for the rare rocks above 3 m.
2. **Triangle count dominates.** Dropping from 320 to 80 triangles cut BasePass plus Velocity from about 20 ms
   to about 3 ms at 500k rocks.
3. **The angular-size cull (`RingRockCullAngularRadius`) is the main lever.** At 1e-3 rad (about 1.2 px at
   1080p and a 90° FOV), 1M rocks cost about 2 ms of ring-related GPU time.
4. **5M instances is not the right target for this design.** A 1M window at W = 1.5 km (about 0.11 rocks/m²)
   already fills the frame, and the "Cassini" density at range comes from the Tier A disc, not from instance
   count. Extra rocks would be sub-pixel and get culled anyway. The 2M measurement shows rising cost with no
   visible gain. **Default: `RingDenseParticleCount` = 1,000,000.**
5. **The Velocity pass stays significant** even with the renderer's motion vectors set to Disable (opaque rocks
   still draw velocity under TSR). See open item 3.

### Host contract for the C++ work

**New constants in `SOLConstants.h`:**

| Constant | Value | Meaning |
|---|---|---|
| `RING_DENSE_PARTICLE_COUNT` | 1000000 | Rocks spawned once into the window |
| `RING_DENSE_WINDOW_RADIUS_CM` | 150000.0 | W, half the wrapped tile size |
| `RING_DENSE_HALF_THICKNESS_CM` | 2000.0 | 1-sigma half thickness of the rock layer |
| `RING_DENSE_ROCK_MIN_RADIUS_CM` / `RING_DENSE_ROCK_MAX_RADIUS_CM` | 25.0 / 600.0 | Rock radius range |
| `RING_DENSE_CULL_ANGULAR_RADIUS` | 0.001 | Rocks below this angular radius (rad) are hidden |
| `RING_DENSE_FADE_IN_DISTANCE_M` / `RING_DENSE_FADE_OUT_DISTANCE_M` | e.g. 20e3 / 50e3 | Tier B is fully on inside the first and fully off beyond the second, measured by `DistanceOutsideRingVolumeM` |
| `Paths::RING_DENSE_NIAGARA_SYSTEM` | `"/Game/SOL/Rings/NS_SOLRingDense.NS_SOLRingDense"` | Asset path |

Also add a `RingDenseParams` namespace holding the user-parameter names below.

**`NS_SOLRingDense` user parameters**

| Name | Type | When to set | Value |
|---|---|---|---|
| `RingOuterRadiusCm`, `RingInnerRadiusCm`, `RingGap{0,1,2}{Inner,Outer}RadiusCm`, `RingColor` | float / LinearColor | BeginPlay | Same values as the far field |
| `RingDenseParticleCount` | int | BeginPlay, before `Activate` | `RING_DENSE_PARTICLE_COUNT` |
| `RingWindowRadiusCm` | float | BeginPlay | W |
| `RingDenseHalfThicknessCm`, `RingRockMinRadiusCm`, `RingRockMaxRadiusCm`, `RingRockCullAngularRadius` | float | BeginPlay | The constants above |
| `RingDenseFill` | float, 0..1 | BeginPlay, or every frame if used as a fade | Fraction of rocks shown: 1 for Saturn, about the ring's `Density` for the others |
| `RingWindowPhaseCm` | vec3 | Every frame | xy = `fmod(P.xy, 2W)`, computed in double and kept in [0, 2W); z = P.z (height above the ring plane, cm) |
| `RingWindowCenterKm` | vec3 | Every frame | P in km (cm * 1e-5, computed in double): masking only; km because Niagara warns on vec3 values beyond ~10 km |

**Per frame, in `ASOLRingVisuals::HandleUniverseUpdated`, only while Tier B is visible:**

1. **Compute the camera's ring-frame position P** (double, cm) in a frame that co-rotates with the ring.
   - Take `rel = observer - planet` (ecliptic, metres), the ring axes e1 and e2 (the planet orientation's X and Y) and n = `mRingNormal`.
   - Integrate the spin angle: `theta += sqrt(GM_planet / r^3) * dt`, where r is the in-plane length of `rel`. Integrating, rather than computing `omega * t` directly, means a radial move never makes the field jump.
   - P.xy = (rel·e1, rel·e2) rotated by -theta, in cm. P.z = rel·n, in cm.
   - At high time warp, hide Tier B instead: a co-rotating carpet means nothing at 1000× warp.
2. **Place the dense component** at the camera's world location, with absolute transform like the far field.
   Its rotation is `mFarFieldRotation * FQuat(FVector::UpVector, theta)` and its scale is 1. It must not be
   log-compressed: Tier B is only shown within tens of km of the ring, where placement is 1:1.
3. **Set** `RingWindowPhaseCm` and `RingWindowCenterCm`.
4. **Control visibility without deactivating.** Activate once in BeginPlay, then gate drawing with
   `SetVisibility` / `SetPaused` (or by fading `RingDenseFill` from 1 to 0) between the fade thresholds.
   Do not call `Deactivate`: reactivating re-runs the 1M burst, which costs about 1 ms on one frame.
5. **Optional, recommended:** pass `RingSunDirection` (ring-local unit vector toward the Sun) and
   `RingPlanetRadiusCm` to the far field, every frame or about once per second. This makes the lit side and
   the planet's shadow follow the real Sun. Also set `RingProfileIndex` explicitly.

**Tier A needs no change.** It renders correctly with the current `PlaceFarFieldNiagara`.
**Tier C is unchanged.** Overlap between the Tier B carpet and the Mass rocks is visually harmless at this
density. Add an inner-hole parameter later if it is ever needed.

### Verification screenshots

**Host path** (real `ASOLRingVisuals`, `-game` smoke flags), in `Saved/Screenshots/WindowsEditor/`:

| File | Planet, altitude |
|---|---|
| `ring_sat_a1.png` | Saturn, 250,000 km |
| `ring_sat_near.png` | Saturn, 60,000 km |
| `ring_sat_far.png` | Saturn, 1,470,000 km (log-compressed range) |
| `ring_jup_a1.png` | Jupiter, 300,000 km |
| `ring_jup_near.png` | Jupiter, 100,000 km |
| `ring_ura_a2.png` | Uranus, 120,000 km |
| `ring_nep_a2.png` | Neptune, 150,000 km |

**Tier B** (spawned in PIE from Python, since the host does not drive it yet), in `.../RiderMCP/`:

| File | Content |
|---|---|
| `20261008-202922_viewport.png` | 500k rocks, 80 triangles |
| `20261008-203020_viewport.png` | 2M rocks |
| `20261008-231246_viewport.png` | 1M rocks plus the true-scale Tier A disc, camera 5 m above the B ring |

### Open items

1. **(Resolved: see the Amendment 15 addendum below.) The tiers do not hand over at the ring plane.** At grazing angles the far disc turns into a bright, opaque
   sheet across the horizon. That is physically right for the B ring, but inside the Tier B window it should
   give way to the rocks. Fix options: fade its opacity by the distance from the camera inside the material (pass
   the camera's local position as a fourth dynamic parameter), or have the host fade it near the plane.
2. **Lighting ignores the real Sun** until the host passes `RingSunDirection`. Until then the viewer's side is
   always the lit side, and there is no planet shadow keyed to the real Sun.
3. **Velocity-pass cost** of opaque Niagara mesh particles under TSR, even with the renderer's motion vectors
   disabled. Not yet investigated.
4. **Jupiter's ring is too bright** next to reality (its real optical depth is about 1e-6). It currently reads
   as a thin grey line. Lower its tau, or its `RingColor`, if strict realism is wanted.
5. **Tooling hazards found during this work:**
   - An Interchange OBJ import run from RiderLink's Python executor **crashes the editor** with a TaskGraph assertion (`RecursionGuard == 1`). Build meshes with GeometryScript instead.
   - `AddUserVariables` takes the variable's `name` and `type` as flat fields. Wrapping them in `{variable: {...}}` silently creates `User.None`, and the editor later **crashes** at PIE with a NiagaraVariant assertion (`InCount > 0`). The asset was restored from git and rebuilt.
   - GeometryScript meshes have **no UV channel** unless `set_num_uv_sets` is called, and a Niagara mesh renderer then draws nothing, with no warning.
   - After `SetRendererData` or `SetEmitterData`, toggle any module off and on to force a real recompile before saving. Otherwise a stale compiled renderer can be saved.

### Amendment 15 addendum — far-disc camera fade (resolves open item 1)

1. `M_SOLRingFar` multiplies its opacity by `smoothstep(FadeNearCm, FadeFarCm, D)`, with the scalar parameters `FadeNearCm` = 100000 (1 km, fully clear) and `FadeFarCm` = 400000 (4 km, unchanged look). Opacity scaling alone is enough: the material is plain Translucent, so its colour is not premultiplied.
2. `D` is the distance along the pixel's view ray to the **true** ring plane, `H / Vz` (custom node `RingPlaneRayDistance`). `H` is `dot(TranslatedWorld(CameraPositionWS) - TranslatedWorld(ObjectPositionWS), ObjectOrientation)` and `Vz` is `dot(CameraVectorWS, ObjectOrientation)`. A ray that never reaches the plane gets 0, so it is fully faded.
3. Why it is not `length(pixel - camera)`: within a few hundred metres of the plane, the 1.3e10 cm disc's per-pixel position, depth and interpolants are wrong by kilometres (huge-triangle precision). The disc even rasterizes on the wrong side of the camera, so only per-draw uniforms plus the view direction can be trusted. Debug-colour runs confirmed that `H` matches the host's own plane height (+179 m).
4. Verified with `-SOLRingStartKm=100000`: at 0.05, 2 and 6 km heights the sheet is gone in the plane, and the disc fades in smoothly from about 2 km up. The far views (Saturn 60,000 km, Uranus 120,000 km) are unchanged. Screenshots are `HighresScreenshot00061`–`00066`.
5. Still open: in the plane, the thin far-ring sliver within a few degrees of the horizon (D > 4 km) is not drawn, because the mis-rasterized disc does not cover it. A real fix needs a host-side or mesh-side change (for example a camera-centred local disc patch). Also, smoke runs drift off-plane at about 300 m/s, because flight assist brakes toward Mimas' frame.

## Amendment 16 — host implementation of the dense layer and its adversarial review (2026-10-09)

Implements Amendment 15's host contract in `ASOLRingVisuals` and `SOLRingDense` (issue #6 Part 5e-v). Corrections to
Amendment 15's contract, found by the adversarial review and now in the code:

- **Handedness.** The dense component's rotation is `EclipticToUnreal(orientation * Rz(spin))`, i.e. the ring frame
  conjugated by a Y mirror (`M = diag(1,-1,1)`), so a Niagara-local offset `L` corresponds to the right-handed
  co-rotating offset `M·L`. The window phase and window centre therefore have to be sent in the **mirrored** frame
  (`SOLRingDense::RingFrameToNiagaraLocalCm(P)`); without it rocks slide with the camera at 2x its speed along Y and the
  annulus/gap mask is off by kilometres. The same mirror applies to the Sun direction sent to the far disc. The rotation
  is **not** `mFarFieldRotation * FQuat(UpVector, theta)` as Amendment 15 step 2 says (that spins the opposite way).
- **Camera.** Visibility, spin integration and placement all use the render **viewpoint** (the map camera in map mode),
  not the observer, so the layer cannot keep simulating around a map camera far from the ring.
- **Fade band.** `RING_DENSE_FADE_IN/OUT_DISTANCE_M` are 1.5 km / 6 km (window radius and `ROCK_MAX_RADIUS /
  CULL_ANGULAR_RADIUS`), not 20 / 50 km: beyond 6 km the largest rock is culled, so the GPU work would draw nothing. The
  far disc's own fade is 1-4 km (material parameters `FadeNearCm`/`FadeFarCm`).
- **Dependencies.** The dense layer is optional (a missing asset or sim clock logs a warning and disables only it); an
  engine-deactivated dense system is reactivated on the next show, with a warning.
- **Dead code removed.** `SOLRingPatch::FarFieldSpriteSizeCm`, `RING_FAR_FIELD_PARTICLE_COUNT`,
  `RING_FAR_FIELD_SPRITE_OVERLAP_FACTOR` and the `RingParticleCount`/`RingParticleSizeCm` parameter names, with their 8 tests.

**Verification.** 21 `SOLTest.RingDense.*` tests, including a composition test that chains window phase, wrap,
component rotation and back to the ring frame and asserts a carpet rock keeps its co-rotating position for several
cameras and spin angles (this test fails without the Y mirror), and a prograde-orbit test pinning the spin sign.
`-SOLRingStartKm=100000 -SOLRingHeightKm=0.05 -SOLSmokeShot=1.2` screenshots (`HighresScreenshot00048`/`00049`,
`00061`/`00062`) show the host-driven rocks around the camera inside Saturn's B ring. That spawn co-rotates with the ring,
so by itself it cannot show relative motion: the composition test covers it. Not measured: GPU time and VRAM of the
host-driven layer (see CLAUDE.md tech debt).

### Amendment 16 addendum — measured GPU time and VRAM of the host-driven dense layer (2026-10-09)

Measured through the real host path (`ASOLRingVisuals` driving `NS_SOLRingDense`), standalone game (`-game`), 1920x1080
(TSR internal 1400x788), Saturn's B ring, 1,000,000 rocks, Development build, one `ProfileGPU` frame each and one
`memreport -full` each, 8 s after start with the ship co-rotating ballistically (`-SOLRingStartKm=100000
-SOLRingHeightKm=0.05`, flight assist off for this mode so it stays in the plane). Baseline is the identical run 50 km
above the plane (`-SOLRingHeightKm=50`): the layer hidden and, being activated lazily on first show, never activated.
Run with `-SOLSmokeShot=8 "-SOLSmokeConsole=ProfileGPU"` / `"-SOLSmokeConsole=memreport -full"`.

| GPU pass (ms) | In ring (layer on, rocks around the camera) | 50 km above (layer off) |
|---|---|---|
| **Frame total** | **7.02** | **3.72** |
| NiagaraGpuSim (`NS_SOLRingDense:RingDust`, 1M particles) | 0.54 | - |
| GPUScene dynamic upload (`Niagara.UpdateMeshParticleInstances`, 1M instances) | 0.37 (0.28) | - |
| Instance culling (`CullInstances`) | 0.21 | - |
| Velocity pass (opaque, `ParallelDraw`) | 1.80 | ~0 |
| BasePass | 0.51 | 0.13 |
| Translucency / post / AO / lighting | unchanged (about 4 ms, TSR 1.5-1.8) | same |

The dense layer costs about **3.3 ms of GPU time per frame while rocks are around the camera**, of which **1.8 ms is the
velocity pass**: the opaque rocks are still drawn into the velocity buffer under TSR even though the mesh renderer's
motion vectors are disabled (open item 3 of Amendment 15, now quantified; the best optimization target, potentially
more than half the cost). Hidden (beyond the 6 km fade-out, or above 10x warp) it costs nothing: it is paused and not
drawn, and a never-visited ring never activates it.

**VRAM** (`memreport -full`, RHI resource memory total): 10,651.6 MB without vs 10,866.2 MB with the layer, i.e.
**+214.7 MB** for the first ring whose layer is shown, almost all vertex-buffer memory (+213.7 MB): the Niagara
particle double buffer (2 x `GPUBufferFloat` at 99.2 MB, 2 x `GPUBufferInt` at 7.6 MB, about 213.6 MB) plus the shared
GPUScene instance buffers growing (`InstanceSceneData` 16 -> 64 MB, new `InstancePayloadData` 64 MB, one-time and shared by
every system). **Each further ring visited adds about another 214 MB of particle buffers** (the system stays resident once
activated), so a session that visits all four rings holds about 640 MB of dense-layer particle memory on top of the
shared GPUScene growth. Mitigations, not yet taken: one shared dense component re-parameterised for the active ring;
or deactivating a layer after it has been hidden for a while (re-showing respawns the 1M rocks, about 1 ms on one frame);
or fewer attributes per particle (about 104 bytes each today).

**Window centre unit.** `User.RingWindowCenterCm` was replaced by `User.RingWindowCenterKm` (the same ring-frame position, cm x 1e-5):
`UNiagaraComponent::SetVariableVec3` logs a warning on every call whose value exceeds `UE_OLD_HALF_WORLD_MAX` (~10 km), and the
centre is ~1e10 cm from the planet. The mirrored-Y rule above applies unchanged.
