<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 17 — Orbit lines

Ticket `#17`. Plan of record: [`Docs/Plans/17-orbit-lines-plan.md`](../Plans/17-orbit-lines-plan.md). Cross-cutting rules: [SDD 1](1-solar-system-architecture.md) (1:1 scale, double-precision universe coordinates, log-compressed far rendering, Keplerian orbits).

## 1. Problem

Orbit lines exist today as a Part 1 (1c) HUD feature: `ASOLFlightHud::DrawOrbitLines` samples every body that has a parent with `SOLOrbitLines::SampleEllipse` (90 segments, uniform in true anomaly), re-samples every 5 s, and draws 1 px canvas lines through `DrawLocalSegment` with a near-plane and screen clip. They are off by default (`O`), have no occlusion (a line behind a planet is drawn across the planet's disc), alias, have a fixed width, and their 90-segment chords visibly miss their own body up close (Earth's chord error is up to ~91,000 km, about 14 Earth radii). The selected target's orbit is drawn in yellow.

Issue #17 replaces them with world-space orbit lines of much higher quality that are on by default, correctly occluded by the planets, and cheap enough to stay on all the time.

## 2. Decisions (grill-me, all final)

| # | Area | Decision |
|---|---|---|
| 1 | Default and toggle | **On by default.** Toggled by a **rebindable key** (the existing `ToggleOrbitLines` binding, default `O`) **and** a **Settings checkbox**. The state is **persisted** in the settings save, following the existing settings/save patterns and the project's minimal-disk-traffic standard. |
| 2 | Which bodies | Lines for **every planet, dwarf planet and moon** (every registry body with a parent), plus a **subset of minor bodies: the ~50 largest named asteroids** (negligible cost). When the player **targets a body that has no line** (for example an ordinary asteroid), **its orbit is shown too**. |
| 3 | Highlight | The **targeted body's orbit line** is drawn in a **different highlight color** from the rest. |
| 4 | Style | **Dashed**, with the dash length fixed in **world arc length** (dashes do not swim as the camera moves). **Constant screen-space pixel width**, **anti-aliased**, **fades with distance**. Better quality than the current solid 1 px canvas lines. |
| 5 | Occlusion | A line **behind a planet, or passing through it, is hidden by the planet's true sphere**. Only a part of a line that lies **between the camera and the planet** is drawn over it. Far planets are log-compressed in depth, so occlusion must use **true-scale geometry**, not the engine depth buffer alone. |
| 6 | Technique | **Analytic ellipse points precomputed once from the elements**, drawn as a **camera-facing ribbon** (or a similarly efficient mesh) **per orbit**, **positioned each frame by its parent body**. Double precision on the CPU, float in the shader. |
| 7 | Budget | **<= 0.5 ms GPU at 1920x1080** with everything shown. **No per-frame CPU rebuild** of line geometry. |

## 3. Scale and precision

- **Sizes.** Orbits span from Pan (a = 133,600 km, inside Saturn's rings) to Eris (a = 67.9 AU = 1.0e13 m). Body radii span from about 50 km (the smallest of the 50 largest named asteroids) to Jupiter (69,911 km).
- **Chord error.** For `N` points uniform in **eccentric anomaly** `E`, the largest gap between a chord and the true ellipse (the sagitta) is at most `a * (2*PI/N)^2 / 8` for any eccentricity. At the ends of the major axis the radius of curvature is `b^2/a` and the chord is about `b * dE` long. At the ends of the minor axis these are `a^2/b` and `a * dE`. Both give a sagitta of at most `a * dE^2 / 8`. The line should pass through its own body when seen up close, so `N` is chosen per orbit so that this error is at most **5% of the body's radius** (`ORBIT_LINE_SAGITTA_RADIUS_FRACTION`). `N` is clamped to [128, 8192]. Examples: Earth 1,523, Jupiter 1,049, Saturn 1,559, Uranus 3,344, Neptune 4,245, the Moon 148. Pluto, Eris and most asteroids hit the 8192 cap. At the cap, Vesta's error is 26 km against its 262 km radius (10%), and Pluto's is 434 km against 1,188 km (37%; see open question 8).
- **Uniform-E sampling** is chosen over the current uniform-true-anomaly sampling because it spreads points evenly along highly eccentric orbits. Uniform true anomaly bunches them at periapsis and leaves long chords at apoapsis.
- **Float precision.** A parent-relative point stored as one float has an error of about 6e-8 of its size: 9 km at 1 AU, 270 km at Neptune. That is fine for a far line. But when the camera is near the line (flying along a planet's orbit next to the planet), `float(parent - camera) + float(point)` cancels catastrophically and the line jitters by kilometres as the camera moves. Each point is therefore stored as a **double-float pair** (`hi = float(v)`, `lo = float(v - hi)`, about 48 bits). The CPU supplies `parent - camera` each frame as a double-float pair too. The shader subtracts the pairs before converting to one float, in the same way as Unreal's own LWC `DFVector` math. The result is camera-relative to centimetres.
- **Log compression.** Bodies beyond `DEFAULT_MAX_RENDER_DISTANCE_CM` (1,000,000 km) are drawn by `SOLRender::ComputePlacement` at `d' = Max * (1 + 0.1 * ln(d / Max))` along the same ray. Each line vertex is placed by the same function (`PlaceLinePointCm`), so it lands on the same screen pixel as a body at that true position, and its depth order against other line points is preserved. But a planet is scaled as a whole by the factor of its centre (`d'/d` at `d = |centre|`), while each line point gets its own factor. Near a compressed planet's limb, the engine depth order of a line point against the planet's surface can therefore be wrong. This is why decision 5 uses analytic true-scale occlusion.

## 4. Design

### 4.1 Pure logic: `SOLOrbitLineMath` (17a)

A new feature folder `Source/SOLTest/OrbitLines/` holds `SOLOrbitLineMath.{h,cpp}`: `SOLTEST_API` free functions on plain doubles, `FVector3d` and `FSOLKeplerElements`, with no UObjects. Every number the shader computes has a C++ mirror here, tested on the CPU. The HLSL in `Shaders/Private/SOLOrbitLine.ush` mirrors it, as `SOLRingShadowMath` and `SOLRingShadow.ush` do (SDD 16). The full contract is in the plan's "17a contract". In summary:

- **Sampling.** `SampleOrbit` appends `N + 1` points uniform in eccentric anomaly from periapsis (`P a (cos E - e) + Q b sin E`, with the same 3-1-3 basis as `SOLKepler::ElementsToState`). The loop closes exactly. It also appends the unit prograde tangents and the cumulative polyline arc lengths. `SegmentCountFor` applies the sagitta rule from section 3, and `ChordSagittaM` is that bound.
- **Rebuilds.** `OrbitDriftM` is a proven upper bound on how far any sampled point moves between two element sets (the mean anomaly is ignored). `NeedsRebuild` compares it with the same 5%-of-radius tolerance. The asteroids' elements have zero rates, so they never rebuild. Planets and moons rebuild only when their secular drift (for example the Moon's 18.6-year nodal and 8.85-year apsidal precession, which matter under time warp) exceeds the tolerance. At most `ORBIT_LINE_MAX_REBUILDS_PER_FRAME` orbits rebuild in one frame (round robin). This is a rare, bounded rebuild, not a per-frame one.
- **Dashes.** `DashPhase` = arc length x dash count / perimeter. The dash count is an integer per orbit (`ORBIT_LINE_DASH_COUNT`, 120), so the loop closes on a whole dash. `DashCoverage` is the exact box-filtered (anti-aliased) coverage of the dash square wave over the pixel's phase footprint (`fwidth` in the shader). It becomes the duty cycle once a whole dash period fits inside one pixel, so far dashes turn into an even dim line instead of shimmering.
- **Width and anti-aliasing.** `ViewportPixelWidth` is `ORBIT_LINE_WIDTH_PX` (1.5 px) at 1080p, scaled with the viewport height like the HUD. `LineCoverage` is the analytic coverage across the ribbon. A line thinner than 1 px is drawn 1 px wide at proportionally lower intensity, so thin lines do not flicker. `RibbonHalfExtentPx` is the geometry's half width, including a 1 px feather margin. `WorldSizePerPixel` and `RibbonSideDirection` expand the ribbon in the vertex shader. The side direction is perpendicular to both the view ray and the tangent, with a deterministic fallback when you look along the line.
- **Fade.** `DistanceFade` is 1 within `ORBIT_LINE_FADE_START_SMA` (20) semi-major axes of the camera and 0 beyond `ORBIT_LINE_FADE_END_SMA` (200), linear in log distance in between. So moon orbits fade away from afar while planet orbits stay. The highlighted line never drops below `ORBIT_LINE_HIGHLIGHT_MIN_ALPHA` (open question 2).
- **Occlusion.** `IsPointOccludedBySphere` checks whether the segment from the camera to the line point passes strictly inside a true-scale sphere. It is false when the camera is inside the sphere, and false at a tangent. `IsPointOccluded` checks the selected occluders. `SelectOccluders` picks at most `ORBIT_LINE_MAX_OCCLUDERS` (8) bodies by angular radius above a minimum (about half a pixel), largest first. `IsHiddenBySceneDepth` is the hybrid rule in section 4.3.
- **Selection.** `BuildPermanentLines` lists every registry body that has a parent (planets, dwarf planets, moons; not the Sun), then the `ORBIT_LINE_NAMED_MINOR_COUNT` (50) largest named minor bodies (finite radius > 0, ties to the lower index). `ResolveTargetLine` returns which permanent line to highlight, or that a transient (highlighted) line is needed for a target that has an orbit but no permanent line. With the lines toggled off it returns nothing (open question 3).
- **Precision.** `SplitDouble` and `SplitVector` produce the double-float pairs. `PlaceLinePointCm` is `SOLRender::ComputePlacement(rel, 0, max).LocationCm`.

### 4.2 Renderer (17b; as built see Amendments 1 and 3)

- **One actor, one component per orbit.** `ASOLOrbitLineVisuals` (in `OrbitLines/`) is spawned by the game mode like `ASOLBodyVisuals`. It owns one mesh component per permanent line, plus one spare component for the transient target line. The transient component is rebuilt only when the target changes. Each component's static vertex buffer holds 2 vertices per sample point (side -1 and +1): `hi` (position), `lo`, the tangent, the dash phase and the side, packed into the position, normal or tangent and UV channels. The 17b spike chooses between `UProceduralMeshComponent`, a `UDynamicMeshComponent` and a small custom `FPrimitiveSceneProxy`, by measurement. The default is a custom proxy if UV packing of the double-float pairs proves lossy. Nothing is rebuilt per frame.
- **Per frame (CPU, on `OnUniverseUpdated`).** For each visible orbit: `parent - camera` (doubles, true scale) is split into a double-float pair and pushed with the fade parameters, the highlight flag and the occluder list as **custom primitive data** (a few floats per component, about 100 components, no allocation). The occluders (`SelectOccluders`, 8 spheres as camera-relative true-scale centres and radii) are shared by all lines, through one material parameter collection.
- **Vertex shader (material WPO with a custom HLSL node from `SOLOrbitLine.ush`).** It does the double-float subtraction to get the true camera-relative position, then `PlaceLinePointCm` for the render position (same screen pixel as the true point). It expands the ribbon by `RibbonHalfExtentPx` x `WorldSizePerPixel(d')` along `RibbonSideDirection`. WPO is the target position minus the vertex's own world position. The component's bounds are set to never cull.
- **Pixel shader (translucent, unlit, depth test disabled, depth write off).** `LineCoverage` x `DashCoverage` x `DistanceFade` x the occlusion term (0 or 1) x the line color. Translucency sort priority puts lines after the body and ring-disc translucency.
- **Budget.** At most ~37 registry orbits plus 50 asteroids, each with at most 8192 segments: under about 1 M vertices (~32 MB of vertex data worst case). The pixel cost is a few pixels wide per line. 17b measures with `ProfileGPU` at 1080p with every line shown, from three views: inner system from above, at Earth looking along its orbit, and at Saturn with the moons. The target is <= 0.5 ms. If vertices dominate, the first lever is `ORBIT_LINE_MAX_SEGMENTS` for minor bodies. The alternative design, kept in reserve, evaluates the ellipse in the vertex shader from per-orbit uniforms with samples clustered around the body (no stored geometry). It is recorded here in case the spike rejects stored ribbons.

### 4.3 Occlusion (17c)

- **True-scale analytic spheres.** For each pixel, the line's true camera-relative position is tested against the occluder spheres with `IsPointOccluded` (ray-sphere, at most 8 spheres). This handles every planet, dwarf planet, moon and the Sun at any distance, including log-compressed ones. The line part between the camera and the planet stays visible, and a line that passes through the planet (its own planet's orbit line passes through its centre) is hidden inside the disc.
- **Scene depth, only in the 1:1 zone.** Nearby things that are not spheres in the registry (the ship, dropped targets, asteroids, ring rocks) are handled by sampling `SceneDepth`. A line pixel is hidden only if the scene depth is inside the 1:1 range (`< DEFAULT_MAX_RENDER_DISTANCE_CM`) and the line's true distance is beyond it (`IsHiddenBySceneDepth`, with a relative bias). Compressed far geometry is ignored, so it can never wrongly hide a line; the analytic spheres cover those bodies.
- **Not occluders:** ring discs and gas-giant shells beyond the planet's radius. The lines draw over translucent ring discs (open question 7).

### 4.4 Highlight and targeting (17c)

- The targeting subsystem's selected candidate is mapped to a line key. A candidate index below the body count is `RegistryBody(index)`. Registered `ISOLTargetable`s have no orbit. `ResolveTargetLine` picks the highlight slot or the transient line. The highlight color is `ORBIT_LINE_HIGHLIGHT_COLOR`.
- **Minor bodies are not targetable today** (targeting candidates are registry bodies plus `ISOLTargetable`s, SDD 2). The "targeted body without a line" path (an ordinary asteroid) is therefore implemented and tested in the pure logic, but stays **latent** until asteroid targeting exists (open question 1). The transient-line component and the key mapping are built so that a minor-body target key plugs in without changing the renderer.

### 4.5 Settings, key and text (17d)

- `SOLSettingsModel::FSettingsValues` and `USOLSettingsSave` gain `bShowOrbitLines` (default `true`). It is added within schema 1, like `bVsyncChosen`: a save written before it reads the constructor default, which is the desired default, so `SAVE_VERSION` is not bumped. The Settings page gets a checkbox (live preview, applied on Apply like the other values).
- **The key in flight** (`ToggleOrbitLines`, still default `O` and still rebindable) flips the live value and the applied value in memory and marks a write as owed. The write happens at the next lifecycle boundary that already flushes settings (a Settings or menu visit ending, quitting to the menu, or shutdown in `Deinitialize`), as one async save, and only if the value differs from what is on disk. It never writes per key press.
- **Retired:** `ASOLFlightHud::DrawOrbitLines`, `RefreshOrbitCache`, `mShowOrbitLines`, `UI/SOLOrbitLines.{h,cpp}` and `Tests/OrbitLinesTest.cpp`. Their sampling is superseded by `SampleOrbit` (open question 6). `HUD_HINT_ORBITS_ON/OFF` reads the new state.
- **Text.** `SOLMenuText` (`CONTROL_ROWS` "Show or hide orbit lines" stays; `INFO_PROSE` gets an orbit-line paragraph, using `{ToggleOrbitLines}`), `GAME_MECHANICS.md` (the `O` row and a new "Orbit lines" row with every tunable) and `ARCHITECTURE.md` (the new folder, the actor, the frame-order note and the shader file) are updated in the same change.

### 4.6 Frame order

`ASOLOrbitLineVisuals` binds `USOLAnchorSubsystem::OnUniverseUpdated`, after the bodies are placed. It reads the registry positions, the targeting selection and the camera's universe position (the viewpoint plus the camera's render offset from the observer). It writes only custom primitive data and the occluder collection. It has no ordering dependency on other `OnUniverseUpdated` listeners: it reads only this frame's registry and targeting state, which are final before the broadcast.

## 5. Testing

- **17a:** `Source/SOLTest/Tests/OrbitLineMathTest.cpp` (`SOLTest.OrbitLineMath.*`), written from the contract before any production code by a separate subagent. Edge cases covered: circular and highly eccentric orbits, NaN and zero inputs, a camera inside an orbit, segments behind, in front of and through a sphere at true solar-system scale, the camera inside a sphere, tangents, occluder selection, dashes and anti-aliasing at the limits, double-float precision near the camera, drift bounds, and the selection and targeting rules.
- **17b-17d:** C++ host code that is not pure gets its own tests (custom data packing, settings model and save round trip of `bShowOrbitLines`, `SOLSettingsModelTest` extended, `HudFormatTest` hint text, `SOLInfoTextTest`). A PIE smoke check (`-SOLSmokeHud` cases updated: lines on at start, `O` toggles, the setting persists) with screenshots: a line passing behind Earth, the Moon's line hidden behind Earth, Saturn's moons, the highlight, and a dashed line near the camera at Earth.
- **Budget:** `ProfileGPU` numbers from the 17b views are recorded in an amendment to this SDD.

## 6. Acceptance

- Lines are on at first start. `O` and the checkbox toggle them, and the state survives a restart with no write per key press.
- Every planet, dwarf planet and moon, and the 50 largest named asteroids, have a dashed, anti-aliased, constant-width line. The targeted body's line is highlighted.
- A line behind or through a planet is hidden by the planet's true disc, also for log-compressed planets. A line in front of a planet stays visible.
- Up close, a line visibly passes through its own body (chord error <= 5% of the radius below the 8192-segment cap). No kilometre-scale jitter when the camera is near a line.
- <= 0.5 ms GPU at 1080p with everything shown. No per-frame geometry rebuild (rebuilds only on drift, <= 4 per frame).

## 7. Open questions

Raised while writing the SDD. **All answered by the user on 2026-10-14** (answers recorded under each question):

- **Q1:** keep asteroid targeting latent (logic and renderer ready and tested); filed as [`Docs/ToDo/minor-body-targeting.md`](../ToDo/minor-body-targeting.md).
- **Q2:** yes, the targeted line stays at alpha >= 0.35 at any distance.
- **Q3:** yes, toggling the lines off hides everything, the targeted line included.
- **Q4:** width scales with the viewport height (1.5 px at 1080p).
- **Q5:** yes, the lines stay in the jump map.
- **Q6:** yes, retire `ASOLFlightHud::DrawOrbitLines`, `UI/SOLOrbitLines` and `OrbitLinesTest.cpp` (moved forward from 17d into 17c).
- **Q7:** accepted: lines draw over the translucent ring discs in #17.
- **Q8:** accepted: the tunables are tuned visually in 17b and recorded in an amendment.

1. **Asteroid targeting does not exist yet.** Only registry bodies and `ISOLTargetable`s can be targeted, so decision 2's "targeted asteroid gets a line" cannot be seen in game. Recommended: keep it latent (pure logic and renderer ready, tested) and file a ToDo for asteroid targeting. The alternative is adding minor-body targeting in #17 (larger: candidate list, radar and HUD changes). Needed before 17c.
2. **Highlight fade floor.** Should the targeted line stay visible (alpha >= 0.35) even beyond the 200 a fade distance? Recommended: yes, so a targeted moon's orbit is visible from afar. Needed before 17c (17a's tests assume yes).
3. **Toggle off hides the targeted line too?** Recommended: yes, "off" means no lines at all. 17a's tests assume yes.
4. **Width at other resolutions.** 1.5 px at 1080p scaled with the viewport height (3 px at 4K), or a fixed pixel width at every resolution? Recommended: scaled, like the HUD. 17a's tests assume scaled.
5. **Jump map.** Show the lines in the jump map as well, as today? Recommended: yes.
6. **Retire the old HUD canvas lines** and `SOLOrbitLines` together with their test file? Recommended: yes, in 17d.
7. **Lines over ring discs.** Lines draw over the translucent ring discs, not dimmed by them. Recommended: accept for #17.
8. **Tunables** (120 dashes per orbit at 60% duty, 1.5 px, fade 20-200 a, the colors, the 8192-segment cap) are first guesses, to be tuned visually in 17b and recorded in an amendment. At the cap, the far dwarf planets' lines miss the body's centre by up to about a third of its radius (Pluto 434 km); a higher cap for those few orbits, or the in-shader evaluation in 4.2, would fix it if it shows.

## 8. Amendments

### Amendment 1 (2026-10-14): 17a-17c as built, renderer choice, budget

- **Mesh type.** `UProceduralMeshComponent` was rejected by inspection: its proxy builds `FStaticMeshVertexBuffers::InitFromDynamicVertex`, which stores UVs at half precision, so the double-float points cannot be packed losslessly (a 1e11 m coordinate overflows a half). `UDynamicMeshComponent` keeps full-precision UVs but adds the GeometryFramework dependency and a dynamic-mesh proxy. The choice is a **runtime `UStaticMesh` per line** on a `USOLOrbitLineComponent` (`UStaticMeshComponent` subclass, fixed bounds of 4e11 cm, no shadows, collision or ray tracing): static draw lists and GPU Scene, custom primitive data per component. A first version went through `FMeshDescription` and `BuildFromMeshDescriptions` (full-precision UVs via `FBuildMeshDescriptionsLODParams`); it took **2,104 ms** at load (1,712 ms of it building descriptions). `SOLOrbitLineMesh::ApplyRibbonToMesh` now fills the LOD resources directly (16-bit indices, full-precision UVs): **168 ms** for all 85 lines (43 ms ribbons, 98 ms render data). Drift rebuilds use the same path (a planet or moon line is 300-9,000 vertices).
- **Packing.** Vertex positions are all zero; UV0-UV2 carry the point's double-float pair, UV3 the dash phase and side, the normal the orbit tangent (Unreal axes, high-precision tangent basis). Custom primitive data per line (12 floats): parent minus viewpoint as a double-float pair, semi-major axis, highlight flag, colour. Placement is relative to the **viewpoint** (as for the bodies), and the shader adds the viewpoint's render location from the collection.
- **Counts.** 85 permanent lines (35 registry bodies with a parent and 50 asteroids), **952,218 vertices** (the asteroids hit the 8192-segment cap and are about 820k of them). GPU memory is about 64 bytes per vertex plus indices, so roughly **65 MB**, twice the 4.2 estimate. Levers if it matters: a lower segment cap for minor bodies (costs their close-up chord accuracy), a low-precision tangent basis, or dropping the colour stream.
- **GPU (ProfileGPU, 1920x1080, the `Translucency(AfterDOF)` pass with and without lines via the new developer switch `sol.OrbitLines 0`, single frames, repeated where noisy):**

  | View | Lines on | Lines off | Line cost |
  |---|---|---|---|
  | Saturn and its moons (from Titan + 600,000 km) | 0.224-0.233 ms | 0.045 ms | ~0.18 ms |
  | Earth close-up (60,000 km), Earth's line through the disc | 0.248 ms | 0.036 ms | ~0.21 ms |
  | Moon looking at Earth | 0.250 ms | 0.037 ms | ~0.21 ms |
  | Inner system from 3 AU above the Sun (`-SOLSmokeHud` final view) | 0.301 ms | 0.035 ms | ~0.27 ms |

  All are within the 0.5 ms budget; the cost is vertex-bound (the ~950k vertices are drawn in every view). One first run measured 0.88 ms (shader warm-up), not reproduced.
- **Multi-scale dashes (new).** With 120 dashes per orbit a dash of Earth's orbit is ~4.7 million km long, so near a planet its line was either solid or entirely in a gap (the targeted Earth line was invisible in the first screenshots). `SOLOrbitLineMath::MultiScaleDashCoverage` (mirrored in the shader) halves the dash period in power-of-two levels (crossfaded, so still fixed in world arc length per level) while a dash is longer than `ORBIT_LINE_DASH_MIN_PERIOD_PX` (24) on screen, up to `ORBIT_LINE_DASH_MAX_LEVEL` (8, where the float phase still has ~1e-3 dash precision), then fades the line to solid between 200 and 800 px of period, so it can never vanish in a gap. Tested in `SOLTest.OrbitLinePacking.Dash*`.
- **Tuning (open question 8).** The 50 asteroid lines swamp the inner system at the normal colour (they are within 20 semi-major axes of any inner-system camera, so they never fade). They now use `ORBIT_LINE_MINOR_COLOR` (0.42, 0.42, 0.5) at alpha 0.12. The other tunables are kept: 1.5 px, 120 dashes at 60% duty, fade 20-200 a, the colours.
- **Occlusion (17c)** is as designed: up to 8 spheres (`SelectOccluders`, minimum angular radius `ORBIT_LINE_MIN_OCCLUDER_ANGULAR_RADIUS_RAD` = 5e-4 rad, about half a pixel) per pixel, plus the 1:1-zone scene-depth test (the line's true depth is its pixel depth scaled by true over placed distance). The occluders are chosen from last frame's camera, but their centres are viewpoint-relative for this frame and the shader uses this frame's camera.
- **Retired in 17c instead of 17d (user, Q6):** `ASOLFlightHud::DrawOrbitLines`, `RefreshOrbitCache`, `DrawLocalSegment`, the Liang-Barsky clip helper, `UI/SOLOrbitLines` and `OrbitLinesTest.cpp`. `ASOLFlightHud::ToggleOrbitLines` and `AreOrbitLinesVisible` now delegate to the actor. `-SOLSmokeHud` expects the lines on at start (first O off, second O on, no O before the 3 AU shot), 17/17 checks pass; `-SOLSmokeMap` logs the line state instead of a segment count.
- **Test fixes (not weakened):** `OrbitLineMathTest.cpp` named a lambda `check`, which collides with UE's `check` macro (renamed `expectTarget`), and compared colours with `FLinearColor::Equals(x, 0.0f)`, which is a strict `<` and can never pass (now `==`, the exact equality the test meant).

### Amendment 2 (2026-10-14): 17d as built

- **Settings.** `FSettingsValues::bShowOrbitLines` (default true) and `USOLSettingsSave::bShowOrbitLines`. Per the coordinator's instruction the save schema is **bumped to 2** (instead of 4.5's "added within schema 1"): `USOLSettingsSave::MigrateToCurrentVersion` upgrades a schema 1 save step by step (1 -> 2 sets the flag to its default, true) and the upgraded save is written back once at the next write point. The visible behaviour is the same as 4.5 (an old save starts with lines on).
- **Key and writes.** O goes `ASOLFlightHud::ToggleOrbitLines` -> `ASOLOrbitLineVisuals::ToggleLines` -> `USOLSettingsSubsystem::SetOrbitLinesShown` (`SOLSettingsModel::SetAppliedShowOrbitLines`: applied and pending value, other pending edits untouched), in memory only. The slot is written at the existing write points (`EndVisit`, shutdown) only if `IsSaveOwed`: an Apply or migration owed a write, or the flag differs from the value last loaded or written. Verified: O off then on in `-SOLSmokeHud` writes nothing.
- **Settings page.** A "Show orbit lines" checkbox (Gameplay section) previews live: the actor follows `AreOrbitLinesShown` (the pending value) each update; Apply/Revert as for the other values.
- **Info prose.** A paragraph on orbit lines using `{ToggleOrbitLines}`.
- **Actor logic** extracted into the pure `SOLOrbitLineLogic` (`TargetKeyForCandidate`, `SelectDriftRebuilds`, `NextRebuildCursor`); outside a SOL game world the actor builds nothing.
- **Drift rebuild under warp** (Earth view, 10 days/s, ~20 s): 534 rebuilds, never more than 4 per frame, mostly Mars (176), Neptune (139) and Uranus (115); lines stay correct (screenshot). Cost per rebuild not profiled; see the CLAUDE.md tech-debt entry.
- **GPU memory.** The tangent basis is now 8-bit (it only steers the ribbon side) and the colour stream is dropped: ~52 bytes per vertex, about 55 MB for 952k vertices (was ~65 MB). The minor-body segment cap stays at 8192 (lowering it would make the asteroid lines miss their bodies up close); recorded as tech debt in CLAUDE.md.
- **Tests** (new cases written by a separate subagent from the spec): `SOLTest.SettingsModel.OrbitLines*` and `SetAppliedOrbitLines`, `SOLTest.SettingsRules.OrbitLinesSave`, `MigrateV1`, `MigrateNoOp`, `SOLTest.InfoText.OrbitLinesParagraph`, `SOLTest.OrbitLineLogic.*` (5, including a bare-world actor toggle). Run: OrbitLine* 47, Settings* 23, InfoText 11, HudFormat 10, KeyBindings 9, all pass.

### Amendment 3 (2026-10-14): adversarial review fixes

- **Scene-depth straddle (High).** Just past the 1:1 boundary a log-compressed planet's near surface can be placed inside `DEFAULT_MAX_RENDER_DISTANCE_CM` (Jupiter from 1.5e6 km: near surface at ~9.92e10 cm), so the 1:1-zone depth test hid lines truly in front of it (Io's line). The test now trusts scene depth only below `SOLOrbitLineLogic::SceneDepthTrustLimitCm` = max * (1 - the largest R/d of the compressed bodies), written each frame to `OrbitLineStyle2.w` and passed as `IsHiddenBySceneDepth`'s limit. Near 1:1 geometry (the ship) still hides lines. Screenshot: the Jovian moons' lines now cross Jupiter's disc in front.
- **Orientation drift is a rotation.** Each line's custom data carries the quaternion from its built orientation to the current one (`OrbitOrientation` = Rz(node) Rx(i) Rz(arg), `OrbitRotationBetween`); the vertex shader adds `RotationOffset(q, hi)` (computed on the float high part, small for a small rotation) to the double-float point and rotates the tangent. Only shape drift (`OrbitShapeDriftM`, `NeedsShapeRebuild`: the a and e terms of `OrbitDriftM`) rebuilds; rebuilt lines are processed before the per-line writes so they draw with an identity rotation the same frame. At 30 days/s: 18 rebuilds in ~20 s (was 534 at 10 days/s).
- **Main menu.** The lines are hidden while `EMenuMode::MainMenu` shows (user decision 2026-10-14; `SOLOrbitLineLogic::AreLinesVisible`). Verified with the `-SOLMenuSmoke=screens` main-menu screenshot.
- **Smokes** call `ASOLOrbitLineVisuals::BeginVerificationOverride`: lines on, settings neither read nor written. The HUD smoke's "off" check now counts visible line components (`CountVisibleLines`), so it can fail.
- **Per-frame cost.** The 50 asteroid lines read the Sun's offset from the collection (`OrbitLineSharedParentHigh/Low`), so their custom data is written only when their highlight changes; a line wholly beyond its fade distance (`IsLineBeyondFade`) is hidden (no vertex cost). Registry lines still write 20 floats each per frame.
- **Other fixes.** `ResolveLineSource` failing at `BeginPlay` leaves an empty, never-drawn line. Meshes have `bSupportRayTracing = false`. Components set `TranslucencySortPriority` (`ORBIT_LINE_TRANSLUCENCY_SORT_PRIORITY` = 100) above the bodies, ring discs and haze. The redundant `SetStaticMesh(nullptr)` is gone. `IsSaveOwed` is the pure `SOLSettingsRules::IsSaveOwed` (tested with the O-twice / once / migration / failed-write scenarios and a real schema-1 slot round trip). The O binding's Controls area is now "Gameplay".
- **Considered, not done.** Storing the high float in the position stream would save ~8 MB, but the WPO would then subtract two large floats and lose the point's precision; rejected. The load-time build (~170-250 ms) is not parallelized (CLAUDE.md tech debt).
- **Not verified:** a packaged (cooked) build. The material includes `/Project/SOLTest/Private/SOLOrbitLine.ush` through the module's shader directory mapping, which a cooked build only needs at cook time; this has not been tried.
- **Tests:** OrbitLine* 56 (Math 32, Packing 10, Logic 14), Settings* 25, InfoText 11, HudFormat 10, KeyBindings 9, MenuState 12, all pass; `-SOLSmokeHud` 17/17, `-SOLSmokeMap` 16/16, `-SOLMenuSmoke=screens` 40/41 (the failure is its audio-volume check, because the run used `-NoSound`). GPU in the Jupiter view: translucency 0.255 ms with lines.

## 9. Revision history

- 2026-10-14: initial design from the grill-me decisions; 17a contract and test file written.
- 2026-10-14: open questions answered by the user; 17a-17c built (Amendment 1).
- 2026-10-14: 17d built (Amendment 2).
- 2026-10-14: adversarial review fixes (Amendment 3).
