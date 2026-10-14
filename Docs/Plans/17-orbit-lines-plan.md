<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 17 — Orbit lines

Design: [`Docs/SDDs/17-orbit-lines.md`](../SDDs/17-orbit-lines.md).

- [x] **17a — Pure logic, test-first.** `Source/SOLTest/OrbitLines/SOLOrbitLineMath.{h,cpp}` and the `SOL::ORBIT_LINE_*` constants in `SOLConstants.h`, per the contract below. Tests: `Source/SOLTest/Tests/OrbitLineMathTest.cpp` (written 2026-10-14 against this contract, before any production code). Make them pass without editing them. If a test contradicts the contract, stop and ask.
- [x] **17b — Renderer and material spike, then measure.**
  - `ASOLOrbitLineVisuals` (spawned by the game mode), one mesh component per permanent line, built once from `SampleOrbit` (double-float pairs, tangent, dash phase, side).
  - `Shaders/Private/SOLOrbitLine.ush` mirrors `SOLOrbitLineMath` (DF subtraction, `PlaceLinePointCm`, ribbon expansion, `LineCoverage`, `DashCoverage`, `DistanceFade`). Add the translucent, unlit material with depth test off, made by a `Tools/OrbitLines/` Python script. Per-frame custom primitive data for `parent - camera` and the fade.
  - Compare `UProceduralMeshComponent`, `UDynamicMeshComponent` and a custom scene proxy for packing fidelity and cost. Record the choice.
  - `ProfileGPU` at 1080p with every line shown, from three views (inner system from above, at Earth along its orbit, at Saturn with its moons), against the 0.5 ms budget. Tune the SDD open question 8 values by screenshots. Record the results as SDD 17 Amendment 1.
- [x] **17c — Occlusion, highlight, targeting.**
  - `SelectOccluders` per frame into a material parameter collection (8 spheres, camera-relative true scale), plus `IsPointOccluded` and `IsHiddenBySceneDepth` in the pixel shader.
  - Map the targeting selection to a `FLineKey`, apply `ResolveTargetLine`, build the transient line component, and use the highlight color.
  - Rebuild on drift (`NeedsRebuild`, at most `ORBIT_LINE_MAX_REBUILDS_PER_FRAME`, round robin).
  - Before starting: get the user's answers to SDD open questions 1-3.
  - Screenshots: Moon's line behind Earth, a line through its own planet, Saturn's moons, the highlight.
- [x] **17d — Settings, toggle, key, Info text.**
  - Add `bShowOrbitLines` (default true) to `FSettingsValues` and `USOLSettingsSave` (schema 2 with a 1 -> 2 migration, per the coordinator; SDD 17 Amendment 2), plus the Settings checkbox.
  - The `ToggleOrbitLines` key flips the live and applied value in memory, and the write is owed until the next lifecycle boundary.
  - (Done in 17c: the HUD canvas orbit lines, `UI/SOLOrbitLines` and `OrbitLinesTest.cpp` are retired.)
  - Update the HUD hint, `SOLMenuText::INFO_PROSE`, `GAME_MECHANICS.md` and `-SOLSmokeHud`.
  - Tests: extend `SOLSettingsModelTest`, `HudFormatTest` and `SOLInfoTextTest`.
- [ ] **17e — Docs, review, verification.**
  - Update `ARCHITECTURE.md` (the `OrbitLines/` folder, the actor, the frame order, the shader file).
  - Run the final screenshot set and budget measurement.
  - Run the adversarial opus review, apply the fixes, run the full suite, `graphify update .`, then commit and push (`Issue #17: ...`).

## 17a contract

**Files.**

- Production: `Source/SOLTest/OrbitLines/SOLOrbitLineMath.h/.cpp`, `namespace SOLOrbitLineMath`, `SOLTEST_API` free functions on plain `double`, `FVector3d`, `FSOLKeplerElements` and `TConstArrayView`, plus the plain nested types below. No `USTRUCT`, no UObjects, no world access, no global mutable state. The only allocation is the documented appends and resets of caller-owned arrays.
- Tests: `Source/SOLTest/Tests/OrbitLineMathTest.cpp`, paths `SOLTest.OrbitLineMath.<Case>`.
- Includes: `Universe/SOLKepler.h`, `Universe/SOLRenderPlacement.h`, `SOLConstants.h`.

**Conventions.**

- Units are metres, radians and pixels, except `*Cm` names, which are centimetres.
- `SampleOrbit` returns parent-relative points in the frame `SOLKepler::ElementsToState` uses (J2000 ecliptic, right-handed). Every other function is frame-agnostic: it only needs one consistent frame for all its vector inputs.
- "Drawable" elements (`IsDrawableOrbit`) have a finite `SemiMajorAxisM > 0`, a finite `0 <= Eccentricity < 1`, and finite inclination, node and argument of periapsis. `MeanAnomalyRad` is ignored everywhere (it may be NaN).
- `wrap(x)` means the angle reduced to [-PI, PI).

### Types (in `SOLOrbitLineMath`)

```cpp
enum class ELineSource : uint8 { None, RegistryBody, MinorBody };

struct FLineKey
{
    ELineSource Source = ELineSource::None;
    int32 Index = INDEX_NONE;                 // registry body index or minor-body (asteroid list) index
    bool operator==(const FLineKey& other) const { return Source == other.Source && Index == other.Index; }
};

struct FTargetLineResult
{
    int32 HighlightSlot = INDEX_NONE;         // index into the permanent line list to draw highlighted
    bool bShowTransient = false;              // the target needs its own (highlighted) line
};
```

### Functions

| Function | Contract |
|---|---|
| `bool IsDrawableOrbit(const FSOLKeplerElements& elements)` | True if the elements are drawable (see Conventions). |
| `double ChordSagittaM(double semiMajorAxisM, int32 segments)` | `a * (2*PI/N)^2 / 8`: an upper bound on the gap between the true ellipse and any chord of `SampleOrbit`'s polyline with `N` segments, for any `e < 1`. A non-finite `a` or `a <= 0` gives 0 (checked first). `N < 3` gives `+inf`. |
| `int32 SegmentCountFor(double semiMajorAxisM, double bodyRadiusM)` | The smallest `N >= 3` with `ChordSagittaM(a, N) <= ORBIT_LINE_SAGITTA_RADIUS_FRACTION * R`, clamped to `[ORBIT_LINE_MIN_SEGMENTS, ORBIT_LINE_MAX_SEGMENTS]`. A non-finite or non-positive `a` gives `MIN`. A non-finite or non-positive `R` (with a valid `a`) gives `MAX`. Non-decreasing in `a`, non-increasing in `R`. |
| `int32 SampleOrbit(const FSOLKeplerElements& elements, int32 segments, TArray<FVector3d>& outPointsM, TArray<FVector3d>& outTangents, TArray<double>& outArcLengthsM)` | If the elements are not drawable or `segments < 3`, it returns 0 and leaves all three arrays untouched. Otherwise it **appends** (never clears) `N + 1` entries to each array and returns `N + 1`. Point `k` (`0 <= k < N`) is at eccentric anomaly `E_k = 2*PI*k/N`: `P * a (cos E - e) + Q * b sin E`, with `b = a sqrt(1 - e^2)`, and `P` and `Q` the periapsis and in-plane-normal unit vectors of the 3-1-3 (node, inclination, argument) basis used by `SOLKepler::ElementsToState`. So point 0 is periapsis and equals `ElementsToState(elements with M = 0, gm, 0).PositionM`. Entry `N` is an exact copy of entry 0, for points and tangents. Tangent `k` is the unit vector along `dPoint/dE` (prograde: `point x tangent` lies along the orbit normal). Arc length `k` is the cumulative polyline length from this call's point 0 (entry 0 is exactly 0; entry `N` is the closed polyline perimeter, including the closing chord), independent of what the arrays already held. |
| `double DashPhase(double arcLengthM, double perimeterM, int32 dashCount)` | `arcLength * dashCount / perimeter`, in dash periods (0 at periapsis, `dashCount` after a full loop, so the loop closes on a whole dash). A non-finite or non-positive perimeter, `dashCount < 1`, or a non-finite arc length gives 0. |
| `double DashCoverage(double dashPhase, double dutyCycle, double phaseFootprint)` | The anti-aliased dash coverage, always in [0, 1]. `duty` is clamped to [0, 1] (NaN counts as 0). A non-finite phase gives 0. **Point sample:** for a footprint that is NaN or `<= 0`, it gives 1 if `frac(phase) < duty`, else 0 (`frac(x) = x - floor(x)`, so negative phases wrap). **Box filter:** for `0 < fp < 1`, it gives `(F(phase + fp/2) - F(phase - fp/2)) / fp`, where `F(x) = floor(x) * duty + min(frac(x), duty)` (the exact mean of the square wave over the window). **Average:** for `fp >= 1` (including `+inf`), it gives exactly `duty`. |
| `double ViewportPixelWidth(double viewportHeightPx)` | `ORBIT_LINE_WIDTH_PX * h / ORBIT_LINE_REFERENCE_HEIGHT_PX`. A non-finite or non-positive `h` gives `ORBIT_LINE_WIDTH_PX`. |
| `double LineCoverage(double distanceFromCenterPx, double widthPx)` | With `wEff = max(w, 1)`: `(w / wEff) * clamp(wEff/2 + 0.5 - |dist|, 0, 1)`. A non-finite or non-positive `w`, or a non-finite `dist`, gives 0. Even in `dist`. A line thinner than 1 px keeps a 1 px footprint at intensity `w`. |
| `double RibbonHalfExtentPx(double widthPx)` | `max(w, 1)/2 + ORBIT_LINE_AA_MARGIN_PX`. A non-finite or non-positive `w` counts as 1. `LineCoverage(RibbonHalfExtentPx(w), w) == 0` for every valid `w`. |
| `double WorldSizePerPixel(double distance, double tanHalfHorizontalFov, double viewportWidthPx)` | `2 * d * tanHalf / W`: the world size (in the units of `d`) of one pixel at distance `d`. Any non-finite input, `d < 0`, `tanHalf <= 0` or `W <= 0` gives 0. |
| `FVector3d RibbonSideDirection(const FVector3d& cameraToPoint, const FVector3d& tangent)` | A unit vector perpendicular to `cameraToPoint`: `normalize(v x t)` (with `v` and `t` normalized) when `|v x t| > 1e-9`. Otherwise (looking along the line, or a zero or non-finite tangent), a fixed deterministic unit vector perpendicular to `v`. A zero or non-finite `cameraToPoint` gives the zero vector. |
| `double DistanceFade(double distanceM, double semiMajorAxisM, bool bHighlighted)` | 1 for `d <= FADE_START_SMA * a`, 0 for `d >= FADE_END_SMA * a`, and `1 - (ln d - ln(START a)) / (ln(END a) - ln(START a))` in between (linear in log distance, 0.5 at `sqrt(START * END) * a`). Highlighted: `max(that, ORBIT_LINE_HIGHLIGHT_MIN_ALPHA)`. A non-finite or negative `d`, or a non-finite or non-positive `a`, gives 0 (highlighted too). |
| `bool IsPointOccludedBySphere(const FVector3d& cameraM, const FVector3d& pointM, const FVector3d& centerM, double radiusM)` | True when the closed segment from the camera to the point passes **strictly** inside the sphere: the minimum distance from the centre to the segment is `< R`. So a point inside the sphere is occluded, a point behind it on a ray through it is occluded, a point in front of it is not, and a tangent or grazing segment is not. False when the camera is inside or on the sphere (`|camera - centre| <= R`), when `R <= 0`, or for any non-finite input. Must stay correct at true solar-system scale (inputs around 1e11-1e13 m; compute relative to the camera). |
| `bool IsPointOccluded(const FVector3d& cameraM, const FVector3d& pointM, TConstArrayView<FVector3d> centersM, TConstArrayView<double> radiiM)` | True if `IsPointOccludedBySphere` holds for any `i < min(centers.Num(), radii.Num())`. Empty gives false. |
| `int32 SelectOccluders(const FVector3d& cameraM, TConstArrayView<FVector3d> centersM, TConstArrayView<double> radiiM, double minAngularRadiusRad, int32 maxCount, TArray<int32>& outIndices)` | `Reset`s `outIndices` (keeping capacity) and fills it with the indices `i < min(lengths)` whose sphere is a valid occluder, then returns `outIndices.Num()`. A valid occluder has a finite centre, a finite `R > 0`, the camera strictly outside it, and angular radius `asin(R / |c - camera|) >= minAngularRadiusRad` (a NaN minimum counts as 0). The indices are ordered by angular radius, largest first, with ties to the lower index, and truncated to `max(0, maxCount)`. |
| `bool IsHiddenBySceneDepth(double lineDistanceCm, double sceneDepthCm, double maxRenderDistanceCm)` | True only when the scene depth is inside the 1:1 zone (`0 < sceneDepth < maxRenderDistance`) and `lineDistance > sceneDepth * (1 + ORBIT_LINE_DEPTH_BIAS)`. Any non-finite input (for example the sky's infinite depth) gives false. A log-compressed scene depth (`>= max`) never hides a line. |
| `FVector3d PlaceLinePointCm(const FVector3d& cameraToPointM, double maxRenderDistanceCm)` | Exactly `SOLRender::ComputePlacement(cameraToPointM, 0.0, maxRenderDistanceCm).LocationCm`: 1:1 within range, otherwise log-compressed along the same direction (same screen position as a body at that true position). |
| `void SplitDouble(double value, float& outHigh, float& outLow)` | `outHigh = float(value)` and `outLow = float(value - double(outHigh))`, so `double(hi) + double(lo)` is within about `|value| * 2^-47` of `value`. A non-finite value gives `outHigh = float(value)` and `outLow = 0`. |
| `void SplitVector(const FVector3d& value, FVector3f& outHigh, FVector3f& outLow)` | `SplitDouble` per component. |
| `double OrbitDriftM(const FSOLKeplerElements& built, const FSOLKeplerElements& now)` | An upper bound on how far any `SampleOrbit` point moves between the two element sets at the same `E` (any `N`). With `aM = max(a1, a2)` and `eM = max(e1, e2)`, it is `|a2 - a1| (1 + eM) + aM |e2 - e1| (1 + eM / sqrt(1 - eM^2)) + aM (1 + eM) (|wrap(di)| + |wrap(dNode)| + |wrap(dArg)|)`. Both sets identical, or differing only in the mean anomaly or by whole turns of an angle, give 0 (up to rounding). Either set not drawable gives `+inf`. |
| `bool NeedsRebuild(const FSOLKeplerElements& built, const FSOLKeplerElements& now, double bodyRadiusM)` | `now` not drawable gives false (keep the old line). `built` not drawable (with `now` drawable) gives true. Otherwise it is `OrbitDriftM(built, now) > tol`, where `tol = ORBIT_LINE_SAGITTA_RADIUS_FRACTION * R` for a finite `R > 0`, else `ORBIT_LINE_REBUILD_FALLBACK_FRACTION * now.SemiMajorAxisM`. |
| `void BuildPermanentLines(TConstArrayView<int32> registryParents, TConstArrayView<double> minorRadiiM, TConstArrayView<bool> minorIsNamed, int32 maxMinorLines, TArray<FLineKey>& outLines)` | `Reset`s `outLines`. It first adds `{RegistryBody, i}` for every registry index whose parent is not `INDEX_NONE`, in index order. Then it adds `{MinorBody, i}` for minor indices `i < min(radii.Num(), named.Num())` that are named and have a finite radius `> 0`, ordered by radius, largest first, with ties to the lower index, truncated to `max(0, maxMinorLines)`. |
| `FTargetLineResult ResolveTargetLine(bool bLinesEnabled, const FLineKey& target, bool bTargetHasOrbit, TConstArrayView<FLineKey> permanentLines)` | Rules in order: (1) lines disabled gives `{INDEX_NONE, false}`; (2) a target with `Source == None` or `Index == INDEX_NONE` gives `{INDEX_NONE, false}`; (3) a target equal to a permanent line gives `{that slot (first match), false}`, whatever `bTargetHasOrbit` is; (4) otherwise a target with an orbit gives `{INDEX_NONE, true}`; (5) otherwise `{INDEX_NONE, false}` (for example the Sun or a dropped target). Keys compare source and index: `RegistryBody 3` is not `MinorBody 3`. |
| `FLinearColor LineColor(bool bHighlighted)` | `SOL::ORBIT_LINE_HIGHLIGHT_COLOR` if highlighted, else `SOL::ORBIT_LINE_COLOR`. |

### Constants (`SOLConstants.h`, namespace `SOL`)

| Name | Type | Value | Meaning |
|---|---|---|---|
| `ORBIT_LINE_MIN_SEGMENTS` | `int32` | 128 | Fewest segments per orbit. |
| `ORBIT_LINE_MAX_SEGMENTS` | `int32` | 8192 | Most segments per orbit (memory and vertex cap). |
| `ORBIT_LINE_SAGITTA_RADIUS_FRACTION` | `double` | 0.05 | Largest chord error, and rebuild drift, as a fraction of the body's radius. |
| `ORBIT_LINE_REBUILD_FALLBACK_FRACTION` | `double` | 1.0e-6 | Rebuild tolerance as a fraction of `a` when the body's radius is unknown. |
| `ORBIT_LINE_MAX_REBUILDS_PER_FRAME` | `int32` | 4 | Drift rebuilds allowed per frame (round robin). |
| `ORBIT_LINE_DASH_COUNT` | `int32` | 120 | Whole dashes per orbit. |
| `ORBIT_LINE_DASH_DUTY` | `double` | 0.6 | Lit fraction of a dash period. |
| `ORBIT_LINE_WIDTH_PX` | `double` | 1.5 | Line width at the reference height. |
| `ORBIT_LINE_REFERENCE_HEIGHT_PX` | `double` | 1080.0 | Viewport height that `ORBIT_LINE_WIDTH_PX` is defined at. |
| `ORBIT_LINE_AA_MARGIN_PX` | `double` | 1.0 | Ribbon feather margin beyond the coverage edge. |
| `ORBIT_LINE_FADE_START_SMA` | `double` | 20.0 | Fully opaque within this many semi-major axes of the camera. |
| `ORBIT_LINE_FADE_END_SMA` | `double` | 200.0 | Fully faded beyond this many semi-major axes. |
| `ORBIT_LINE_HIGHLIGHT_MIN_ALPHA` | `double` | 0.35 | The highlighted line's fade floor (open question 2). |
| `ORBIT_LINE_NAMED_MINOR_COUNT` | `int32` | 50 | Largest named asteroids that get permanent lines. |
| `ORBIT_LINE_MAX_OCCLUDERS` | `int32` | 8 | Occluder spheres passed to the shader. |
| `ORBIT_LINE_DEPTH_BIAS` | `double` | 1.0e-3 | Relative bias of the 1:1-zone scene-depth test. |
| `ORBIT_LINE_COLOR` | `FLinearColor` | (0.25, 0.42, 0.75, 0.6) | Normal line color (tuned in 17b). |
| `ORBIT_LINE_HIGHLIGHT_COLOR` | `FLinearColor` | (0.95, 0.78, 0.25, 0.9) | Targeted line color (tuned in 17b). |

The tests type their own reference values (AU, body radii, GM) independently of the production code. They read `SOL::` constants only to check the constants themselves and the behaviour that the contract defines in terms of them.

## Progress notes (resume state)

- 2026-10-14 (17e, review fixes): adversarial review findings 1-13 and the style items applied, see SDD 17 Amendment 3. Targeted tests (OrbitLine*, Settings*, InfoText, HudFormat, KeyBindings, MenuState) 123/123 pass; HUD and map smokes pass. Nothing committed. **Next:** full suite, `graphify update .`, commit and push (`Issue #17: ...`).
- 2026-10-14 (17d): done, see SDD 17 Amendment 2. Tests OrbitLine* 47, Settings* 23, InfoText 11, HudFormat 10, KeyBindings 9 pass; `-SOLSmokeHud` 17/17. Nothing committed. **Next: 17e** (adversarial opus review, full suite, `graphify update .`, commit and push).
- 2026-10-14 (later): user answered SDD 17 open questions 1-8 (recorded in the SDD; ToDo `minor-body-targeting.md` filed). 17a-17c built and verified, see SDD 17 Amendment 1 (renderer choice, GPU numbers, multi-scale dashes, minor-line colour). Tests: `SOLTest.OrbitLineMath.*` 32/32, `SOLTest.OrbitLinePacking.*` 10/10 (written by a separate subagent from the spec), `SOLTest.HudFormat.*` 10/10; full suite not run. `-SOLSmokeHud` 17/17. Nothing committed. **Next: 17d** (persist `bShowOrbitLines`, Settings checkbox, the key writing the applied value, Info prose, `SOLSettingsModelTest`/`SOLInfoTextTest`), then 17e (adversarial review, full suite, `graphify update .`, commit). The Info prose and `GAME_MECHANICS.md` still need the 17d persistence wording.
- 2026-10-14: SDD 17 and this plan written. `OrbitLineMathTest.cpp` written against the 17a contract (no production code yet, not built, not committed). Next: get the user's answers to SDD 17 open questions 1-4 (17a's tests assume the recommended answers to 2-4), then 17a implementation.
