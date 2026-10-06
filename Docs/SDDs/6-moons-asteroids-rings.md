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
