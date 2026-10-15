<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 14 — Gas giant fidelity

Design: [`Docs/SDDs/14-gas-giant-fidelity.md`](../SDDs/14-gas-giant-fidelity.md).

- [x] **14a — Pure logic, test-first.** Differential-rotation/band phase, storm placement, near/far blend and release-delay rules. Done 2026-10-13: `GasGiants/SOLGasGiantMath.{h,cpp}` + constants; `SOLTest.GasGiantMath` 40/40 pass, tests unchanged.
- [x] **14b — Spike.** Volumetric clouds at planetary scale around one giant (precision, render origin, cost). Done 2026-10-13: SDD 14 Amendment 1 (works with a `SkyAtmosphere` as the centre carrier; 0.89 ms at sample scale 0.25).
- [x] **14c — Shell material and `ASOLGasGiantVisuals`.** Shared flow-map/noise band material with per-planet parameters; spawned per giant. Done 2026-10-13 (SDD 14 Amendment 2): deck + haze shells, `Tools/GasGiants/create_gas_giant_materials.py`, `ASOLBodyVisuals::SetBodyMeshHidden`, 0.41 ms GPU at 1080p full-screen; screenshots in `Saved/GasGiantShots/`. Its open items (actor test, drift check, GRS, phase wrap) were closed in 14d; the adversarial review remains for 14e.
- [x] **14d — Volumetric near layer.** Fade-in, resource release, budget measurement. Done 2026-10-13 (SDD 14 Amendment 3): engine `VolumetricCloud` + zero-scattering `SkyAtmosphere` centre carrier owned by `ASOLGasGiantVisuals`, `M_SOLGasGiantCloud`, haze fades out as it fades in, `ASOLBodyVisuals::SetSunDrivesAtmosphere`; 0.65 ms volumetric at 1080p (~0.9 ms with the deck), within 1.5 ms. 14c leftovers done: wrap-safe band lookup (`BandBlendAt`, all three materials), drift checked at 1x and 1000x (`-SOLSmokeWarpSteps`), GRS found, `SOLTest.GasGiantVisuals.*` world tests, editor throttle setting was never persisted. `SOLTest.GasGiant*` 47/47 pass (full suite not run). Adversarial review fixes applied 2026-10-13 (SDD 14 Amendment 4): unique near-layer component names, sun-atmosphere requester set, cloud material loaded once, Info screen gas giant paragraph, near-layer world tests, view-cone upload gate, high-warp drift cap, uint noise hash, component-selected phases, magic numbers to `SOLConstants.h`, doc fixes. `SOLTest.GasGiant*` + `InfoText` + `MenuState` 73/73 pass. Open for 14e: faceted limb of the engine sphere within ~1 radius, generic cloud look per giant, full suite.
- [x] **14e — Docs, review, verification.** Screenshots at three distances, GPU numbers, adversarial opus review, full suite, commit and push. In-game Info screen gas giant paragraph (`SOLMenuText::INFO_PROSE`, matching GAME_MECHANICS 3.1a): added early with the review fixes; re-check it is still true at 14e.

## 14a contract

Tests: `Source/SOLTest/Tests/GasGiantMathTest.cpp`, paths `SOLTest.GasGiantMath.<Case>` (written against this contract before the code exists). Production: `Source/SOLTest/GasGiants/SOLGasGiantMath.h/.cpp`, `namespace SOLGasGiant`, `SOLTEST_API` free functions on plain `double`/`float`/`int32`, plain nested structs (no `USTRUCT`, no UObjects, no world access, no global state). All angles in radians, all times in seconds, phases in turns (1 turn = 2*PI rad).

### Animation model

Band and storm motion is a **pure function of sim time** (`FSOLSimClock::GetSecondsSinceJ2000()`, ~8.6e8 s in 2027, negative before J2000), never an accumulator: so it follows time warp, is pause-safe (same time, same phase) and deterministic. The drift is *relative to the body's own spin* (the body already rotates via `SOLBodyRotation`); the artistic `speedFactor` multiplies it so bands visibly drift at 1x. All phases are reduced in double on the CPU and handed to the material as small floats in `[0, 1)` (a float cannot hold `rate * t` at t ~ 1e9 s).

| Function | Contract |
|---|---|
| `double DriftRateRadPerS(double latitudeRad, double equatorDriftRadPerS, double polarDriftRadPerS)` | `eq + (polar - eq) * sin^2(lat)`. Latitude clamped to `[-PI/2, PI/2]`; even in latitude. NaN latitude -> treated as 0 (equator). A non-finite drift is treated as 0. |
| `double PhaseTurns(double simSeconds, double rateRadPerS, double speedFactor)` | `frac(simSeconds * rateRadPerS * speedFactor / (2*PI))`, always in `[0, 1)` (negatives wrap upward, never returns exactly 1). Accurate to 1e-6 turns for `|t| <= 1e10 s` and `|rate * factor| <= 1 rad/s`. `speedFactor <= 0`, or any non-finite input -> 0. |
| `double BandPhaseTurns(double simSeconds, double latitudeRad, double equatorDriftRadPerS, double polarDriftRadPerS, double speedFactor)` | `PhaseTurns(simSeconds, DriftRateRadPerS(lat, eq, polar), speedFactor)`. |
| `float PhaseToShader(double phaseTurns)` | Wraps any finite value into float `[0, 1)`; a value that would round to `1.0f` becomes `0.0f`; non-finite -> `0.0f`. |
| `double BandSampleLatitudeRad(int32 index, int32 count)` | Evenly spaced from `-PI/2` (index 0) to `+PI/2` (index `count - 1`) inclusive. `count <= 1` -> 0. `index` clamped to `[0, count - 1]`. |
| `void ComputeBandPhases(double simSeconds, double equatorDriftRadPerS, double polarDriftRadPerS, double speedFactor, TArrayView<float> outPhases)` | `outPhases[i] = PhaseToShader(BandPhaseTurns(t, BandSampleLatitudeRad(i, n), ...))` with `n = outPhases.Num()`. Empty view is a no-op. No allocation. |

### Storms

`struct FStorm { double LatitudeRad = 0.0; double LongitudeRad = 0.0; double RadiusRad = 0.0; };` (angular radius on the sphere; longitude in the body frame at t = 0).

| Function | Contract |
|---|---|
| `void GenerateStorms(int32 seed, int32 count, double maxAbsLatitudeRad, double minRadiusRad, double maxRadiusRad, TArray<FStorm>& outStorms)` | Resets `outStorms` (never appends). `count` clamped to `[0, SOL::GAS_GIANT_MAX_STORMS]`. Deterministic: same arguments -> bit-identical output, independent of any global RNG state (own `FRandomStream(seed)`). **Prefix-stable**: the first k storms of a call with count n >= k equal a call with count k. Each storm: `|lat| <= maxAbsLat`, longitude in `[0, 2*PI)`, radius in `[min, max]`. **No overlap**: great-circle distance between any two centres >= sum of radii; candidates are rejection-sampled up to `SOL::GAS_GIANT_STORM_PLACEMENT_ATTEMPTS` times per storm, and a storm that cannot be placed is skipped (so the output can be shorter than `count`, but when storms are small relative to the sphere all are placed). `maxAbsLat` NaN -> 0, clamped to `[0, PI/2]`. `minRadius < 0` -> 0; `min > max` swapped; any non-finite radius or `max <= 0` -> empty output. |
| `double StormLongitudeRad(const FStorm& storm, double simSeconds, double equatorDriftRadPerS, double polarDriftRadPerS, double speedFactor)` | The storm drifts with the band at its latitude: `wrap(storm.LongitudeRad + 2*PI * BandPhaseTurns(t, storm.LatitudeRad, ...))` into `[0, 2*PI)`. Equals the (wrapped) authored longitude at t = 0 or factor 0. |

### Near volumetric layer: blend and release

| Function | Contract |
|---|---|
| `double NearLayerBlend(double distanceToCenterM, double planetRadiusM, double fadeInRadii, double fadeOutRadii)` | With `x = distance / radius` (planet radii from the centre): 1 at `x <= fadeIn` (including inside the planet), 0 at `x >= fadeOut`, `1 - smoothstep((x - fadeIn) / (fadeOut - fadeIn))` between; non-increasing in distance. `fadeOut <= fadeIn` -> hard step (1 at `x <= fadeIn`, else 0). Non-finite distance, or non-finite / non-positive radius -> 0. |
| `bool IsNearLayerVisible(double blend)` | `blend > 0` and finite. |
| `struct FNearLayerState { bool bActive = false; double HiddenSeconds = 0.0; };` `enum class ENearLayerAction : uint8 { None, Activate, Release };` | |
| `ENearLayerAction StepNearLayer(FNearLayerState& state, double blend, double realDeltaSeconds, double releaseDelaySeconds)` | Mirrors the ring dense layer's rule. Visible (`IsNearLayerVisible`): `HiddenSeconds = 0`; if not active -> set active, return `Activate`; else `None`. Hidden and active: `HiddenSeconds += dt` (`dt` NaN/inf/negative counts as 0); once `HiddenSeconds >= releaseDelay` -> `bActive = false`, `HiddenSeconds = 0`, return `Release`; else `None`. Hidden and inactive: `None`, `HiddenSeconds` stays 0. `releaseDelay <= 0` or NaN -> released on the first hidden step. Hiding for less than the delay and showing again keeps the resources (no Release, no second Activate) and restarts the timer. `dt` is real (unwarped) seconds, so time warp neither speeds up nor stalls the release. Not `[[nodiscard]]` (tests and callers may step without reading the action). |

### Constants (`SOLConstants.h`, namespace `SOL`)

| Name | Type | Initial value | Meaning |
|---|---|---|---|
| `GAS_GIANT_BAND_SPEED_FACTOR` | `double` | 200.0 | Artistic multiplier on band/storm drift. Contract: Jupiter's reference equatorial drift (1.4e-6 rad/s, ~100 m/s jet) drifts >= 0.5 deg per real minute at 1x and takes >= 10 real minutes per full relative turn. |
| `GAS_GIANT_BAND_SAMPLE_COUNT` | `int32` | 16 | Latitudes sampled by `ComputeBandPhases` (four float4 material parameters). |
| `GAS_GIANT_STORM_SEED` | `int32` | 20261014 | Base storm seed; a giant's seed is this plus its body index. |
| `GAS_GIANT_MAX_STORMS` | `int32` | 8 | Upper clamp on generated storms per giant. |
| `GAS_GIANT_STORM_PLACEMENT_ATTEMPTS` | `int32` | 64 | Rejection-sampling attempts per storm. |
| `GAS_GIANT_NEAR_FADE_IN_RADII` | `double` | 2.0 | Near layer fully on within this many planet radii of the centre. |
| `GAS_GIANT_NEAR_FADE_OUT_RADII` | `double` | 4.0 | Near layer fully off beyond this. |
| `GAS_GIANT_NEAR_RELEASE_DELAY_SECONDS` | `double` | 20.0 | Hidden this long (real seconds): release the volumetric resources. |

14d additions (test-after, SDD 14 Amendment 3): `FBandBlend BandBlendAt(double latitudeRad, int32 count, double blendHalfWidth)` (Lower/Upper samples, `UpperWeight = smoothstep(0.5 - h, 0.5 + h, w)`, h clamped to [0, 0.5], h = 0 a step at 0.5; count < 2 -> zeros; NaN latitude = equator) and `double HazeFadeForNearBlend(double blend)` (`1 - clamp(blend, 0, 1)`, non-finite -> 1); constant `GAS_GIANT_BAND_BLEND_HALF_WIDTH` = 0.25.

Review additions (test-after, SDD 14 Amendment 4): `double CappedDriftStepSeconds(double simDelta, double realDelta, double maxTimeWarp)` (sim step clamped to +-realDelta * maxTimeWarp; non-finite or negative real delta or non-finite sim delta -> 0; maxTimeWarp <= 0 or non-finite -> no cap) and `bool IsSphereInViewCone(cameraLocation, cameraForward, halfAngleRad, sphereCenter, sphereRadius)` (inside the sphere or within halfAngle + asin(r/d) of forward; zero forward or non-finite input -> true); constant `GAS_GIANT_BAND_MAX_TIME_WARP` = 1000.

The tests use literal values except where they check the constants themselves (sanity ranges, the 1x visibility rule, the storm-count clamp).
