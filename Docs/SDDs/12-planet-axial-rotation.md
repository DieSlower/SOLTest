<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 12 — Planet axial rotation (spin + axial tilt)

Ticket `#12` ("Planet axial rotation (spin + real axial tilts)"). Plan of record:
[`Docs/Plans/12-planet-axial-rotation-plan.md`](../Plans/12-planet-axial-rotation-plan.md).

Filed as a follow-up during the Part 3 (`#4`) surface-lock grill (see SDD 4's
Decision 8): the Sun and 8 planets currently don't rotate at all. Refines SDD 1
(no existing row to update; this is new scope) and must not contradict
[`Docs/SDDs/1-solar-system-architecture.md`](1-solar-system-architecture.md). All
decisions below were resolved with the user during a `grill-me` session on 2026-09-30.

---

## 1. Problem

Bodies currently sit at a fixed orientation forever. Give each body a real sidereal
rotation and a real axial tilt, purely as a presentational/data feature — rotation has
no effect on gravity, collision, targeting, or surface-lock (all of which already treat
bodies as perfect, orientation-independent spheres) — while keeping the door open for
a future system (terrain, landing) to read a body's orientation from the same place
its position already lives.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Visual verification | Rotation must be visibly checkable in a screenshot now, even though real planet textures don't exist yet (Part 8). The existing parametric body material (`Tools/CreateSOLContent.py` → `M_SOLBody`) already computes its noise-patch term in **local/object space** (`MaterialExpressionLocalPosition` feeds the noise node), so for the 5 bodies that already have nonzero `NoiseStrength` (Mercury, Venus, Earth, Mars, Jupiter), spin becomes visible for free the moment the mesh's own transform carries the rotation — no material-graph change needed. The 4 bodies at `NoiseStrength = 0` (Sun, Saturn, Uranus, Neptune) get a small nonzero noise/band tuning added to their `VISUALS_APPEARANCES` entry in `ASOLBodyVisuals.cpp` (pure data change) so every body visibly spins using the exact machinery already proven to work. No new material parameters, no changes to `Tools/CreateSOLContent.py`. |
| 2 | Where the data and math live | **Extend `FSOLBodyRegistry`/`FSOLBodyDef`**, not a Visuals-only side table. SDD 1 already calls the registry "data-oriented ... structure-of-arrays"; keeping rotation alongside position/velocity/radius/GM in the same per-body arrays, computed in the same `Update()` pass, matches that stated philosophy and means any future consumer (not just `ASOLBodyVisuals`) reads orientation the same way it already reads position. Purely additive: existing consumers (gravity, collision, targeting, surface-lock) are unaffected and need no changes. |
| 3 | Axial tilt fidelity | **Magnitude only, fixed reference direction** — each body stores a single `AxialTiltDeg`; the pole is tilted away from ecliptic-north by that angle within one fixed reference plane (containing the vernal equinox axis), the same construction for every body. Real tilt magnitudes (Earth 23.44°, Uranus 97.77°, etc.) are used, but not each body's real individual pole *direction*, and no precession is modeled — consistent with the project's existing fidelity level (Standish "approximate positions," no planetary perturbations). Real per-body pole directions (right ascension/declination) and precession are deliberately deferred; tracked as a follow-up ([`Docs/ToDo/accurate-pole-directions.md`](../ToDo/accurate-pole-directions.md)). |
| 4 | Retrograde encoding | Use the standard IAU obliquity convention directly: `AxialTiltDeg` ranges **0-180°**, `RotationPeriodH` is always the **positive magnitude** of the sidereal period. A tilt past 90° (Venus ~177.4°, Uranus ~97.8°) automatically encodes retrograde rotation — this lets published reference tables be copied in verbatim with no sign bookkeeping. |
| 5 | Prime-meridian phase | Each body also stores `W0Deg`: the rotation angle at epoch J2000 (so bodies don't all start arbitrarily aligned). Sourced from a published reference (NASA/JPL planetary fact sheets or the IAU WGCCRE report) where practical, cited the same way Standish is cited for orbits; defaults to `0.0` where a body's exact value isn't readily available — this has no gameplay consequence, only which way a body happens to be facing at t=0. |
| 6 | Sun's rotation | Included, using a single representative sidereal period (the Sun's real rotation is latitude-dependent/differential; using one equatorial value is a stated simplification, same spirit as ignoring planetary perturbations). |

## 3. Design

### 3.1 Pure-logic contract (Appendix A)

Extends `Source/SOLTest/Universe/SOLBodyRegistry.h` (existing file, additive changes
only — no existing method's signature or behavior changes):

```cpp
// Added to FSOLBodyDef
struct SOLTEST_API FSOLBodyDef
{
    // ... existing fields unchanged ...
    double RotationPeriodH = 0.0;   // Sidereal rotation period, hours, always positive; 0 = no rotation
    double AxialTiltDeg = 0.0;      // 0-180 deg from ecliptic-north; >90 = retrograde (decision 4)
    double W0Deg = 0.0;             // Rotation angle at J2000 epoch, degrees (decision 5)
};
```

```cpp
// Added to FSOLBodyRegistry (alongside the existing per-body arrays)
class SOLTEST_API FSOLBodyRegistry
{
public:
    // ... existing methods unchanged ...

    // Returns the body's orientation (ecliptic frame): axial tilt (fixed) composed with the current spin (time-varying)
    const FQuat4d& GetOrientation(int32 index) const;

private:
    // ... existing arrays unchanged ...
    TArray<double> mRotationPeriodsH;
    TArray<double> mAxialTiltsRad;
    TArray<double> mW0sRad;
    TArray<FQuat4d> mOrientations;   // Computed each Update() call
};
```

```cpp
// New pure functions, likely a small SOLBodyRotation.h/.cpp namespace (mirrors SOLKepler.h's style) that
// FSOLBodyRegistry::Update calls per body — exact file split is an implementation choice, not a design fork
namespace SOLBodyRotation
{
    // Returns the fixed tilt rotation: ecliptic-north rotated by tiltRad within the reference plane containing +X
    SOLTEST_API FQuat4d TiltRotation(double tiltRad);

    // Returns the body's full orientation at secondsSinceJ2000: TiltRotation(tilt) composed with a spin of
    // (w0Rad + 2*PI*secondsSinceJ2000/periodS) about the TILTED axis. periodH <= 0 returns TiltRotation(tilt) alone
    // (no spin) rather than dividing by zero, so a future body added without rotation data just doesn't spin.
    SOLTEST_API FQuat4d ComputeOrientation(double tiltRad, double w0Rad, double periodH, double secondsSinceJ2000);
}
```

`FSOLBodyRegistry::Update` calls `SOLBodyRotation::ComputeOrientation` for every body
right alongside the existing Kepler position/velocity update, storing the result in
`mOrientations[index]`.

### 3.1.1 Reference data

The values below go into `REGISTRY_SOLAR_SYSTEM` in `SOLBodyRegistry.cpp`, alongside
the existing Standish orbital elements, and are the same numbers both the test-author
and implementer subagents must use (so tests assert against the actual data, not a
guess at it) — sidereal rotation periods and obliquities from the standard NASA/JPL
Planetary Fact Sheet figures, the Sun's equatorial sidereal period as the stated
simplification (decision 6). `W0Deg` is `0.0` for every body per decision 5, rather
than risk a mis-cited IAU WGCCRE prime-meridian constant for no gameplay benefit.

| Body | RotationPeriodH | AxialTiltDeg | W0Deg |
|---|---|---|---|
| Sun | 601.2 | 7.25 | 0.0 |
| Mercury | 1407.6 | 0.034 | 0.0 |
| Venus | 5832.6 | 177.36 | 0.0 |
| Earth | 23.9345 | 23.44 | 0.0 |
| Mars | 24.6229 | 25.19 | 0.0 |
| Jupiter | 9.9250 | 3.13 | 0.0 |
| Saturn | 10.656 | 26.73 | 0.0 |
| Uranus | 17.24 | 97.77 | 0.0 |
| Neptune | 16.11 | 28.32 | 0.0 |

### 3.2 Ecliptic-to-Unreal orientation conversion

`ASOLBodyVisuals` needs the orientation in Unreal's left-handed render frame, the same
way it already needs `SOLRender::EclipticToUnreal` for positions. Unlike position,
there is currently **no existing quaternion overload** anywhere in the codebase (the
ship's `FSOLShipState::Orientation` is constructed natively in the Unreal-handed frame
by flight dynamics, never converted from an ecliptic one) — this is new pure logic,
not a reuse of something that already exists.

The defining property such a conversion must satisfy, for any ecliptic rotation `M`
and any ecliptic vector `v` (verified by hand for the case below and to be the
function's actual unit-test contract, not just a design note): converting `v` with
`SOLRender::EclipticToUnreal(FVector3d)` and then rotating by the converted quaternion
must equal converting `M`-rotated-`v` directly. Concretely, verified by hand: an
ecliptic rotation of 90° about `+Z` (which sends ecliptic `+X` to `+Y`) must convert to
an Unreal-frame rotation of **-90° about the same axis** (`+Z` is unaffected by
negating `Y`, so here only the *angle* flips sign) — because negating `Y` sends
Unreal-frame `+Y` to `-Y`, and only a *negative*-angle rotation about `+Z` sends
Unreal `+X` (unchanged by the mirror) to Unreal `-Y`, matching the mirrored target.
The general rule this generalizes to — mirror the rotation's axis with the same
`FVector3d` conversion, and negate the angle — is what to implement. In quaternion
components `(x, y, z, w)`, this rule works out to **`(-x, y, -z, w)`**, verified by a
second adversarial review from first principles (for `R(n, θ)` and the mirror
`S = diag(1,-1,1)`, the converted rotation is `S R S⁻¹ = R(det(S)·S(n), θ)`, which for
this `S` gives axis `(-nx, ny, -nz)` and angle `θ` unchanged — equivalently, mirrored
axis and negated angle, which produces the same `(-x, y, -z, w)` component form).
**Correction to an earlier draft of this SDD:** that draft claimed the component
form `(x, -y, z, -w)` ("negate Y and W") was wrong; it is not — it is simply the
negation of `(-x, y, -z, w)`, and since a quaternion and its negation represent the
identical rotation, both forms are equally correct. Either is fine to implement; do
the derivation above (or an equivalent one) rather than guessing, since getting the
sign of the wrong TERM (angle vs. one axis component) is an easy mistake either way.
Add `SOLRender::EclipticToUnreal(const FQuat4d&)` in `Universe/SOLRenderPlacement.h/.cpp`
next to the existing vector overload, and unit-test it directly against the defining
round-trip property above (build a rotation and a probe vector, convert both ways,
compare) for several non-trivial rotations with all-nonzero axis components — not only
the single-axis case worked out here.

### 3.3 Engine integration (Appendix B)

- `ASOLBodyVisuals::HandleUniverseUpdated` sets each body mesh's world rotation from
  `SOLRender::EclipticToUnreal(registry.GetOrientation(index))`, alongside the existing
  position placement.
- `ASOLBodyVisuals.cpp`'s `VISUALS_APPEARANCES` table: give Sun, Saturn, Uranus and
  Neptune a small nonzero `NoiseStrength` (and/or `BandFrequency`) tweak, tuned by eye
  once running, so spin is visible on every body (decision 1). No other file changes
  needed for the visual-verification requirement.
- No PIE smoke-test checkpoints are needed (no new player input or mechanic) — a
  screenshot comparison a few seconds apart, showing each body's surface pattern having
  visibly rotated, is the verification artifact for this part, alongside the
  automation tests for the rotation math itself.

## 4. Open questions

None deferred as blocking. Real per-body pole directions and precession are
deliberately out of scope, tracked in `Docs/ToDo/accurate-pole-directions.md`.

## 5. Revision history

- 2026-09-30: initial decisions from the `grill-me` session.
- 2026-09-30: 12a implemented and reviewed. §3.2 corrected (see the note inline) — the
  original draft wrongly flagged the `(x,-y,z,-w)` component form as incorrect; it is
  the negation of the correct `(-x,y,-z,w)` form and represents the same rotation, so
  both are valid. Registry field renamed to match the implementation: `mW0Rad` →
  `mW0sRad`. `SOLBodyRotation::ComputeOrientation` doing one avoidable
  `TiltRotation`/`RotateVector` recompute per body per `Update()` (the tilt is fixed
  after `PopulateSolarSystem`, only the spin changes) logged as tech debt in
  `CLAUDE.md`, not fixed now — negligible at 9 bodies.
- 2026-09-30: 12b implemented and reviewed. `ASOLBodyVisuals::HandleUniverseUpdated`
  applies `FQuat(SOLRender::EclipticToUnreal(registry.GetOrientation(index)))` to each
  body mesh's `SetWorldTransform`, alongside its existing location — the `FQuat(FQuat4d)`
  construction is a plain copy under LWC (no precision loss), matching the existing
  `FVector(FVector3d)` pattern already used for location in the same loop. Appearance
  tuning landed at Saturn `NoiseScale=0.04`/`NoiseStrength=0.5`, Uranus
  `BandFrequency=0.2`/`BandStrength=0.6`/`NoiseScale=0.04`/`NoiseStrength=0.6`, Neptune
  `NoiseScale=0.04`/`NoiseStrength=0.4` — all `NoiseBias=0.0`, tuned up from an initial
  0.25 strength guess after that guess proved barely visible on screenshot. The Sun was
  reverted to `NoiseStrength=0` after adversarial review showed its fixed `{60,48,30}`
  emissive in `M_SOLBody` swamps any `ColorA`/`ColorB` noise lerp — no noise value makes
  its surface pattern visible with the current placeholder shading, so its spin is
  exercised by the 12a unit tests only, not by screenshot. Uranus's very close
  `ColorA`/`ColorB` (about 0.07 apart) also makes its noise patches faint regardless of
  strength; `BandFrequency` was raised from an initial 0.04 to 0.2 (more, tighter
  latitude bands) to make the tilt read clearly, since a couple of broad, low-contrast
  bands were nearly invisible but many narrow ones are not — this is a placeholder-art
  workaround, not a claim that Uranus's contrast is fixed. One adversarial review round
  (fresh `opus` subagent): no must-fix items; the Sun no-op above was the one should-fix,
  applied; nits noted (comment wording, Uranus contrast) were addressed or accepted as
  placeholder-art limitations. Verified with screenshots: Saturn's noise pattern visibly
  shifted between two shots ~40 sim-minutes apart at 1x time-warp (10.656 h rotation
  period, so ~23° of spin); Uranus viewed from its sunlit side at 60,000 km altitude
  shows bands running roughly top-to-bottom (near-vertical/diagonal) rather than
  horizontal, consistent with its ~98° tilt. 328/328 automation tests green throughout
  (12a's tests, unaffected by this visuals-only change).
