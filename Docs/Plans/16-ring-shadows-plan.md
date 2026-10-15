<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 16 — Ring shadows

Design: [`Docs/SDDs/16-ring-shadows.md`](../SDDs/16-ring-shadows.md).

- [x] **16a — Pure logic, test-first.** Done 2026-10-14 (SDD 16 Amendment 1; one test corrected against the contract, see there). `MinorBodies/SOLRingShadowMath.{h,cpp}`, the constants below, and the port of `M_SOLRingFar`'s 52-band optical-depth table to `RealTauProfile`. Tests: `Source/SOLTest/Tests/RingShadowMathTest.cpp` (written 2026-10-13 against this contract, before any production code). Make them pass without editing them; if a test contradicts the contract, stop and ask.
- [x] **16b — Spike and measure: analytic against VSM.** Done 2026-10-14 (SDD 16 Amendment 1): analytic wins (a), (b), (d); VSM rejected (fixed cost > 4 ms in the dense layer); (c) left to 16d. Hardware RT not measured. `-SOLSimDate` added (user answer to open question 1).
  - Build the shared `Shaders/Private/SOLRingShadow.ush` (module shader directory mapping) and a throwaway analytic prototype on `M_SOLRingRock` and one gas giant deck.
  - Measure them against VSM on `SunLight` (rocks, ship and targets casting), screen-space contact shadows, hardware ray-traced shadows (if available), and light function versus per-object custom data for the ship and targets.
  - Use the views and the equal-quality criteria in SDD 16 section 4.3, at 1080p with `ProfileGPU`.
  - Record a table and the decision per shadow direction as SDD 16 Amendment 1.
  - Ask the user about SDD open question 1 (a sim-date verification flag) before adding one.
- [x] **16c — Planet and ring materials.** Done 2026-10-14 (SDD 16 Amendment 2): deck/haze/cloud ring shadow, soft planet shadow on `M_SOLRingFar` and `M_SOLRingRock` (the rock material also got the statistical self-shadow via `MPC_SOLRingShadow`), cost below measurement noise.
  - Generate the shader profile table from `RealTauProfile`, and add a test that the two match.
  - Ring shadow on the planet: deck, haze and cloud, via `create_gas_giant_materials.py`.
  - Soft planet shadow on `M_SOLRingFar` and `M_SOLRingRock`, replacing the cylinder.
  - Hosts pass `alpha` (`SunAngularRadiusRad`), the Sun direction, the planet radius and the profile index.
  - Tune on Saturn first, then screenshot all four planets.
  - Measure against the 0.3 ms budget.
- [x] **16d — Ship, targets and rock self-shadow.** Done 2026-10-14 (SDD 16 Amendment 3): CPU `BodySunTransmittance` at the observer, product over every body, applied by a lighting-channel-1 `ShipSunLight` shown only while shadowed; Info text, `GAME_MECHANICS.md` and `ARCHITECTURE.md` updated (16e's doc part for 16d). Tests `SOLTest.RingShadowMath.BodySunTransmittance*`, `RockSelfShadowDecomposition`, `SOLTest.ShipSunlight.*`, `SOLTest.InfoText.FollowsRebinds`.
  - Implement the 16b winner for the ring shadow on the ship and dropped targets.
  - Implement the rock self-shadow (`sigma` = the dense half-thickness).
  - Add tests for any C++ host code.
  - ~~Decide with the user whether the planet's eclipse of the ship stays on~~: answered, it stays on (SDD 16 section 7).
  - The rock self-shadow already exists (16c, statistical); 16d judges contact shadows as an add-on and measures hardware RT if available.
  - Make a final budget measurement.
- [x] **16e — Docs, review, verification.**
  - Update `ARCHITECTURE.md` (shader directory, the new math module, host inputs), `GAME_MECHANICS.md` and the in-game Info text (`SOLMenuText::INFO_PROSE`).
  - Take the screenshot set and record the GPU numbers.
  - Run the adversarial opus review, apply the fixes, run the full suite, then commit and push (`Issue #16: ...`).

## 16a contract

**Files.**

- Production: `Source/SOLTest/MinorBodies/SOLRingShadowMath.h/.cpp`, `namespace SOLRingShadow`, `SOLTEST_API` free functions on plain `double`, `FVector3d` and `TConstArrayView`, plus plain nested structs (no `USTRUCT`, no UObjects, no world access, no global mutable state, no allocation).
- Tests: `Source/SOLTest/Tests/RingShadowMathTest.cpp`, paths `SOLTest.RingShadowMath.<Case>`.

**Conventions.**

- Units are metres and radians.
- The **ring frame** is planet-centred and right-handed: x and y in the ring plane, z along the ring normal (the planet's pole). This is the frame `SOLRingDense::RingFramePositionCm` uses, but in metres.
- "Toward the Sun" vectors need not be normalized; each function normalizes them.
- **Lit is the safe default.** Any non-finite input, or a zero Sun vector, gives transmittance 1, unless a row below says otherwise.

### Types

```cpp
struct FTauBand { double InnerM = 0.0; double OuterM = 0.0; double Tau = 0.0; };          // [InnerM, OuterM), normal optical depth
struct FPlanetShadowRadii { double UmbraRadiusM = 0.0; double PenumbraRadiusM = 0.0; };
```

A profile is a `TConstArrayView<FTauBand>` sorted ascending by `InnerM` with non-overlapping bands. Behaviour with overlapping bands is unspecified. Gaps between bands have tau 0.

### Functions

| Function | Contract |
|---|---|
| `double SunAngularRadiusRad(double sunDistanceM)` | `asin(SOL::SUN_RADIUS_M / d)`. `d <= SUN_RADIUS_M` (including 0 and negative) gives `PI/2`. NaN or `+inf` gives 0. Strictly decreasing for `d > SUN_RADIUS_M`. |
| `double PenumbraHalfWidthM(double distanceToOccluderM, double sunAngularRadiusRad)` | `L * tan(min(alpha, SOL::RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD))`. `L <= 0`, `alpha <= 0`, or any non-finite input gives 0. Linear in `L`, increasing in `alpha`. |
| `double FootprintRadialHalfWidthM(double penumbraHalfWidthM, double sunElevationSin, double radialSunAzimuthCos)` | `w * sqrt(1 + c^2 (1/mu^2 - 1))`, where `mu = clamp(|sunElevationSin|, RING_SHADOW_MIN_SUN_ELEVATION_SIN, 1)` and `c = clamp(radialSunAzimuthCos, -1, 1)`. `w <= 0` or non-finite gives 0. A non-finite `mu` is treated as 1, and a non-finite `c` as 0 (no stretch). Even in `c`. Non-decreasing in `|c|` and non-increasing in `|mu|`. |
| `double OpticalDepthAt(double radiusM, TConstArrayView<FTauBand> profile)` | The tau of the band with `InnerM <= r < OuterM` (inner inclusive, outer exclusive, so at a shared edge the outer band wins), otherwise 0. A non-finite `r` gives 0. A negative or non-finite band tau counts as 0. |
| `double FilteredTransmittance(double centerRadiusM, double halfWidthM, double slantFactor, TConstArrayView<FTauBand> profile)` | The exact mean of `exp(-tau(r) * slantFactor)` over `r` in `[c - h, c + h]` (a box filter over the piecewise-constant profile; radii below 0 simply have no band). With `h <= 0` or non-finite it is a point sample `exp(-OpticalDepthAt(c) * k)`. `slantFactor <= 0` or NaN gives 1. `slantFactor = +inf` gives the fraction of the window where tau is 0. A non-finite `c` gives 1. Always in `[0, 1]`. Non-increasing in `slantFactor`. |
| `double LayerFractionTowardSun(double heightTowardSunM, double layerHalfThicknessM)` | `0.5 * erfc(z / (sigma * sqrt(2)))`: the fraction of a Gaussian layer lying between the receiver and the Sun. Accurate to 1e-4 absolute (an approximation shared with the HLSL is allowed). `sigma <= 0` or non-finite gives a step: `z > 0` gives 0, `z < 0` gives 1, `z == 0` gives 0.5. NaN `z` gives 0. `+inf` gives 0 and `-inf` gives 1. `F(z) + F(-z) = 1`, and it is non-increasing in `z`. |
| `double RingShadowTransmittance(const FVector3d& receiverRingFrameM, const FVector3d& towardSunRingFrame, double sunAngularRadiusRad, double layerHalfThicknessM, TConstArrayView<FTauBand> profile)` | See the algorithm below. Always in `[0, 1]`. Invariant under a rotation about the ring normal applied to both the receiver and the Sun, and under scaling the Sun vector. |
| `double SunVisibleFraction(double sunAngularRadiusRad, double occluderAngularRadiusRad, double separationRad)` | The fraction of a uniform Sun disc (radius `a`) not covered by an occluder disc (radius `b`) whose centre is `|separation|` away, using flat-angle disc geometry. `sep >= a + b` gives 1. `b >= a + sep` gives 0. `a >= b + sep` (occluder inside the Sun) gives `1 - b^2/a^2`. Otherwise it is `1 - lens / (PI a^2)`. A point Sun (`a <= 0`) gives a step: `sep < b` gives 0, else 1. `b <= 0` gives 1. Any non-finite input gives 1. Always in `[0, 1]`, non-decreasing in `sep`, non-increasing in `b`. |
| `double PlanetShadowTransmittance(const FVector3d& pointRelPlanetM, const FVector3d& towardSun, double planetRadiusM, double sunAngularRadiusRad)` | `SunVisibleFraction(alpha, asin(R / |p|), atan2(|s x q|, s . q))`, where `q = -p` (toward the planet's centre). `|p| <= R` gives 0. `R <= 0`, a zero Sun vector or any non-finite input gives 1. A NaN or negative `alpha` is treated as 0 (hard cylinder). |
| `FPlanetShadowRadii PlanetShadowRadiiM(double planetRadiusM, double distanceBehindM, double sunAngularRadiusRad)` | Umbra `max(0, R - D tan(alpha))`, penumbra `R + D tan(alpha)`, with `alpha` clamped like `PenumbraHalfWidthM`. `D <= 0` gives `{R, R}`. `alpha <= 0` gives `{R, R}` (cylinder). `R <= 0` or any non-finite input gives `{0, 0}`. |
| `TConstArrayView<FTauBand> RealTauProfile(int32 profileIndex)` | Static data for `SOL::RING_PROFILE_INDEX_JUPITER/SATURN/URANUS/NEPTUNE` (0-3), ported from `M_SOLRingFar`'s 52-band table. Any other index gives an empty view. Each profile is non-empty, sorted, non-overlapping, finite, with `tau >= 0`, and lies within the matching `SOLPlanetRing::RealRings()` entry's `[InnerRadiusM, OuterRadiusM]` (100 km tolerance, since narrow edge rings such as Uranus's epsilon and Neptune's Adams sit on the quoted outer radius). Saturn: B ring (105,000 km) tau >= 1; the A ring (128,000 km) is between 0.1 and B; the Cassini Division middle (120,500 km) is below 0.5 and below half of B; the minimum over the Encke Gap (133,450-133,720 km) is below half of A; the C ring (85,000 km) is below B; the D ring (70,000 km) is below C. Uranus: the maximum tau over 51,000-51,250 km (the epsilon ring) is above 0.3, and the maximum over 48,400-49,900 km (between the delta and lambda rings) is below a tenth of that. Jupiter's and Neptune's maximum tau is above 0 and below Saturn's. |

### `RingShadowTransmittance` algorithm

1. Return 1 for any non-finite input, a zero Sun vector, or an empty profile. A non-finite or negative `alpha` is treated as 0, and a non-finite or negative `sigma` as 0.
2. Set `s = normalize(towardSun)` and `mu = s.z`. If `|mu| < RING_SHADOW_MIN_SUN_ELEVATION_SIN`, return 1 (the ray is parallel to the plane).
3. Set `z_s = receiver.z * sign(mu)` (the height toward the Sun's side) and `F = LayerFractionTowardSun(z_s, sigma)`. If `F == 0`, return 1.
4. Set `t = -receiver.z / mu` and `hit = receiver + t * s`. Then `r = |hit.xy|` and `L = |t|`.
5. Let `c` be the cosine between `hit.xy` and `s.xy`, or 0 if either is zero. Set `h = FootprintRadialHalfWidthM(PenumbraHalfWidthM(L, alpha), mu, c)`.
6. Return `FilteredTransmittance(r, h, F / |mu|, profile)`.

Consequences the tests rely on:

- A thin plane (`sigma = 0`) with the receiver on the Sun's side gives 1.
- On the dark side, with `alpha = 0`, it gives exactly `exp(-tau(r_hit) / |mu|)`.
- At a band edge, with the Sun along the normal, the receiver directly below the edge gives the exact mean of the two bands' transmittances. The transition spans `+-L tan(alpha)` around the edge.
- At `z = 0` inside a layer, `F = 0.5`.

### Constants (`SOLConstants.h`, namespace `SOL`)

| Name | Type | Value | Meaning |
|---|---|---|---|
| `SUN_RADIUS_M` | `double` | 6.957e8 | IAU 2015 nominal solar radius. |
| `RING_SHADOW_MIN_SUN_ELEVATION_SIN` | `double` | 1.0e-3 | Below this `|mu|`, the Sun is treated as in the ring plane (no crossing), and the footprint stretch is clamped here. |
| `RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD` | `double` | 0.5 | Clamp on `alpha` inside `tan`, so a receiver at or inside the Sun stays finite. |
| `RING_SHADOW_LAYER_HALF_THICKNESS_M` | `double` | `RING_DENSE_HALF_THICKNESS_CM * CM_TO_METERS` (20 m) | `sigma` for rock self-shadowing and for receivers inside the layer. |
| `RING_SHADOW_PROFILE_COUNT` | `int32` | 4 | Number of profiles `RealTauProfile` serves. |

The tests type their own reference values (Sun radius, AU, ring radii) independently of the production code. They read `SOL::` constants only to check the constants themselves and the clamp behaviour.

## Progress notes (resume state)

- 2026-10-14: 16a-16c done, not committed (the user asked for no commit). Files: `Source/SOLTest/MinorBodies/SOLRingShadowMath.{h,cpp}`, `Shaders/Private/SOLRingShadow{,Profile}.ush`, `SOLTest.cpp` (module class, shader mapping), `SOLConstants.h`, `Universe/SOLSimClock*` (`-SOLSimDate`), `GasGiants/SOLGasGiantVisuals.*`, `MinorBodies/SOLRingVisuals.*`, `Tools/GasGiants/create_gas_giant_materials.py` (rerun with `SOL_REBUILD`), assets `M_SOLRingFar`, `M_SOLRingRock`, `MPC_SOLRingShadow`, `M_SOLGasGiant{Deck,Haze,Cloud}`; tests `RingShadowMathTest.cpp`, `SimClockTest.cpp`.
- Next: 16d (ship and targets: CPU viewpoint state scaling the sunlight vs light function vs per-object data; contact shadows look; hardware RT if enabled), then 16e (Info text `SOLMenuText::INFO_PROSE`, review, full suite, commit).
- The adversarial review of 16a-16c has not run yet (16e).
- 2026-10-14: 16d done, not committed. Files: `MinorBodies/SOLRingShadowMath.{h,cpp}` (`BodySunTransmittance`), `Visuals/SOLBodyVisuals.{h,cpp}` (`ShipSunLight`, `UpdateShipSunlight`), `Ship/SOLShipPawn.cpp` (parts on channel 1, fill light on 0 and 1), `Combat/SOLCombatVisuals.cpp` (target ISM on channel 1), `SOLConstants.h` (`SHIP_SUN_LIT_THRESHOLD`), `Menu/SOLMenuText.h` (`INFO_PROSE` shadow paragraph); tests `RingShadowMathTest.cpp`, new `ShipSunlightTest.cpp`, `SOLInfoTextTest.cpp`. Run 2026-10-14 in the editor: `SOLTest.RingShadowMath+ShipSunlight+InfoText+GasGiantVisuals`, 56/56 passed. Next: 16e (the remaining screenshot set, review of 16a-16d, full suite, commit).
- 2026-10-14: adversarial review of 16a-16d done; fixes applied (SDD 16 Amendment 4), not committed. New files: `MinorBodies/SOLRingShadowSwitch.{h,cpp}`, `Tools/Rings/create_ring_shadow_assets.py` (rerun with `SOL_REBUILD`; `M_SOLRingRock` rebuilt with `RockTint`). In-editor run of `SOLTest.RingShadowMath+ShipSunlight+SimClock+InfoText+GasGiant+Ring`: 166/166 passed. Remaining for 16e: full suite, commit and push.
