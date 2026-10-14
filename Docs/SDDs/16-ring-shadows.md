<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 16 — Ring shadows

Ticket `#16`. Plan: [`Docs/Plans/16-ring-shadows-plan.md`](../Plans/16-ring-shadows-plan.md). Decisions were resolved with the user in a `grill-me` session on 2026-10-13. Builds on SDD 6 (rings: Amendments 14-16, the 52-band optical-depth table, `M_SOLRingFar`, `NS_SOLRingDense`) and SDD 14 (gas giant deck, haze and cloud materials).

## 1. Problem

Nothing in the game casts a shadow. The Sun light (`ASOLBodyVisuals::SunLight`) has `CastShadows` off, and every body, ring and rock component has `CastShadow` off. The planets and gas giants use unlit materials with their own Lambert term. The only shadow today is an analytic planet-shadow **cylinder** in `M_SOLRingFar`. It has a hard edge, ignores the Sun's size and does not exist in the dense rock layer. So a ringed planet looks pasted together: the rings do not darken the planet, the planet does not darken the rocks, and a ship flying under the B ring is lit as if in open space.

## 2. Decisions (grill-me, all final)

| # | Area | Decision |
|---|---|---|
| 1 | Shadow directions | All four: (a) **ring shadow on the planet**, (b) **planet shadow on the rings** (far disc and rocks), (c) **ring shadow on the ship and dropped targets**, (d) **ring-rock self-shadowing** in the dense rock layer near the camera. |
| 2 | Technique | **Analytic in materials first**: intersect the ray toward the Sun with the ring plane, look up the **real optical-depth profile** at the hit radius, and attenuate by `exp(-tau / mu)`, using the real Sun direction. No shadow maps in the first build. Then **measure** quality and GPU cost against Unreal's **virtual shadow maps (VSM)** and other efficient alternatives. **Decision rule:** whichever is cheaper at equal quality wins. Record the measurements in this SDD. |
| 3 | Budget | **<= 0.3 ms GPU** for all ring shadow work at **1920x1080**, with **Saturn filling the view**. Measured with `ProfileGPU` as the difference against the same view with shadows off. |
| 4 | Scope | **All four ring systems** (Jupiter, Saturn, Uranus, Neptune), through one shared implementation with per-planet data. **Saturn is tuned first.** |
| 5 | Penumbra | **Physically soft**, from the Sun's **real angular radius at that planet's distance** (`asin(R_sun / d)`) and the real distance from the receiver to the occluder. No artistic softness factor. |
| 6 | Verification date | A developer flag **`-SOLSimDate=<date>`** sets the start sim time (section 7, question 1). |
| 7 | Eclipse on the ship | The ship and dropped targets get the **planet shadow as well** as the ring shadow (section 7, question 3). |
| 8 | Oblateness | **Not modelled**; spherical shadow matching the spherical meshes (section 7, question 2). |

## 3. Physics and scale

**Sun angular radius.** `alpha = asin(R_sun / d)` with `R_sun = 6.957e8 m` (IAU 2015 nominal). Jupiter (5.2 AU): 8.9e-4 rad. Saturn (9.58 AU): 4.85e-4 rad. Uranus (19.2 AU): 2.4e-4 rad. Neptune (30.1 AU): 1.55e-4 rad.

**Penumbra width.** A receiver at distance `L` from the occluding edge, measured along the ray to the Sun, sees the edge blurred over a half-width of `L * tan(alpha)`, measured perpendicular to the ray.

- Ring shadow on Saturn's cloud tops: `L` is about 1e7 to 1e8 m, so the half-width is about 5 to 50 km. It is visible from orbit and grows toward the limb.
- Planet shadow on the rings: the distance behind the planet's limb is up to about 1.4e8 m, so the half-width is about 68 km at the A ring's outer edge.
- Shadows on the ship: the ring can be 1 m to 1e8 m away. Close to the ring the shadow edge is effectively hard.

**Footprint on the ring plane.** For the ring shadow, the Sun's disc seen from the receiver covers an **ellipse** on the ring plane:

- its semi-axis across the Sun's in-plane azimuth is `w = L tan(alpha)`;
- its semi-axis along that azimuth is `w / |mu|`, where `mu` is the sine of the Sun's elevation above the ring plane.

The ring is axisymmetric, so only the footprint's **radial** extent matters. That extent is `w * sqrt(1 + c^2 (1/mu^2 - 1))`, where `c` is the cosine between the radial direction and the Sun's in-plane azimuth. The disc-average transmittance is approximated by a **radial box filter** of that half-width over the piecewise-constant tau profile. For a piecewise-constant profile the box average is exact and cheap: a sum over the overlapped bands.

**Slant optical depth.** Light crossing a layer of normal optical depth `tau` at elevation sine `mu` is attenuated by `exp(-tau / |mu|)`. `mu` is clamped below at `RING_SHADOW_MIN_SUN_ELEVATION_SIN` (1e-3). A ray closer to parallel than that is treated as not crossing the plane, so the transmittance is 1. This is correct for an infinitely thin plane: at equinox the ring's shadow on the planet collapses to a line.

**Layer thickness (self-shadowing).** Inside a ring of finite thickness, a receiver at height `z` sees only the part of the layer between itself and the Sun. With a Gaussian vertical distribution of 1-sigma half-thickness `sigma`, the fraction of the layer on the Sun's side of `z` is `F = 0.5 * erfc(z_s / (sigma * sqrt(2)))`. Here `z_s` is the height measured toward the Sun's side of the plane. The transmittance is then `exp(-tau * F / |mu|)`.

- With `sigma = 0` this reduces to the thin plane: `F` is 1 on the dark side and 0 on the lit side.
- Inside the dense rock layer (`sigma = RING_DENSE_HALF_THICKNESS_CM`, 20 m), this gives the **statistical** self-shadowing: rocks on the dark side of the layer are darker than those on the lit side.

This model is the analytic answer to decision 1(d). Per-rock contact shadows are what VSM would add, and are measured in 16b.

**Planet shadow.** The Sun is a uniform disc of angular radius `alpha`, and the planet is a disc of angular radius `beta = asin(R / |p|)` as seen from the point `p`. Their angular separation is `theta`, the angle between the Sun direction and the direction to the planet's centre (computed with `atan2(|s x q|, s . q)` for precision at tiny angles). The visible fraction is `1 - lens(alpha, beta, theta) / (pi alpha^2)`, using the flat-angle disc-overlap (lens) area.

- It is exact in the small-angle limit. No limb darkening is modelled.
- A point Sun (`alpha = 0`) gives the exact **cylinder** of radius `R`, which is today's material behaviour.
- The umbra and penumbra radii at distance `D` behind the planet are `max(0, R - D tan(alpha))` and `R + D tan(alpha)`. The umbra reaches far beyond every ring: about 1.2e11 m long at Saturn.
- Planet oblateness (Saturn's is 10%) is **not modelled** (user decision, section 7 question 2). The shadow uses the registry's mean radius, consistent with the sphere mesh. **If the planet meshes ever become oblate, this shadow shape must change with them.**

**Precision.** The materials work in float. Ring-frame positions relative to the planet's centre reach about 1.4e8 m, where the float step is about 8 m. That is negligible against penumbrae of kilometres and gaps of 42 km or more. CPU code (tests, and any per-object queries) uses double.

## 4. Design

### 4.1 One profile, one math, two mirrors

- **Profile.** The optical-depth table that `M_SOLRingFar` uses today (52 bands across the four planets, generated by the lost scratchpad script `ringprof.py`) is ported to C++ as `SOLRingShadow::RealTauProfile(profileIndex)`: static data, returned as a view with no allocation, indexed by `SOL::RING_PROFILE_INDEX_*`.
  - This C++ table becomes the **single source of truth**.
  - 16c generates the shaders' copy of the table from it, as a generated `.ush` or material parameters. A test then checks the generated copy against the C++ table, so the two cannot drift.
  - If porting finds the material's bands outside `SOLPlanetRing::RealRings()` radii, 16a reconciles the two and records it in an amendment.
- **Math.** `MinorBodies/SOLRingShadowMath.{h,cpp}` (namespace `SOLRingShadow`) holds pure functions on doubles. It is the test oracle and the reference implementation. A project shader include, `Shaders/Private/SOLRingShadow.ush`, mirrors it line for line. The module maps `/Project/SOLTest` to `Shaders/` in `StartupModule` (adds the `RenderCore` and `Projects` dependencies). Each material's Custom HLSL node includes the `.ush` instead of pasting copies. Keeping one shader file prevents the planet, ring and rock materials from drifting apart.
- **Inputs per material**, set by the existing hosts. All of these exist or are cheap:
  - the ring frame (planet centre, ring axes `e1`/`e2`/`n`), in the units the material already uses;
  - the Sun direction (ring-local);
  - `alpha`, computed once per frame per planet from the double Sun distance;
  - the planet radius;
  - the profile index.
  - Nothing is computed per pixel on the CPU.

### 4.2 Where each direction is drawn

| Direction | Receiver | Where | Notes |
|---|---|---|---|
| (a) Ring on planet | Gas giant deck, haze and near-layer cloud materials (SDD 14) | `Tools/GasGiants/create_gas_giant_materials.py` adds `RingShadowTransmittance` (thin plane) to the Lambert term. The cloud material uses it for its sun visibility. | All four giants are gas giants, so the M_SOLBody path is not needed. |
| (b) Planet on rings | `M_SOLRingFar` (far disc) and `M_SOLRingRock` (dense layer, and the Mass near-field ISM) | Replace the cylinder with `PlanetShadowTransmittance` (soft). | The far disc's lit and unlit sides keep their current Lommel-Seeliger and transmission terms, both multiplied by the factor. |
| (c) Ring on ship and targets | Everything lit by `SunLight` | **Candidate A:** a light function material on `SunLight` that evaluates `RingShadowTransmittance` (and the planet shadow, for free) at each lit pixel. **Candidate B:** a per-object CPU query on the ship and each target writing a custom primitive data value that a material function multiplies in. **Candidate C:** VSM on `SunLight` with the rocks and the ship casting. | 16b measures all three. A light function costs a full-screen pass, but it also covers the rocks, which are lit by `SunLight`. |
| (d) Rock self-shadow | `M_SOLRingRock` | `RingShadowTransmittance` with `sigma` = the dense layer's half-thickness. | Statistical. VSM per-rock contact shadows are compared in 16b. |

Gameplay is unaffected. Shadows are presentation only, with no change to flight, combat or controls. They are still mentioned on the in-game Info screen and in `GAME_MECHANICS.md` (16e), because the player can see them.

### 4.3 VSM and alternatives: what 16b must measure

Hypotheses to confirm or reject with data, not assume:

1. **VSM cannot do (a) or (b) at planetary scale.**
   - The directional VSM clipmap covers about 2^22 cm (about 42 km) around the camera at the default `r.Shadow.Virtual.Clipmap.LastLevel`, while these shadows span about 1e5 km.
   - The far disc is translucent and does not cast into VSM.
   - Far bodies are log-compressed in render space (`SOLRender::ComputePlacement`), so shadow geometry would be wrong anyway.
   - Expected result: analytic wins (a) and (b) by default. 16b records the attempt and the measurement.
2. **VSM may beat analytic on (d) and on (c) inside the dense layer**, because it gives real per-rock shadows. Its cost is the main concern: 1M Niagara mesh particles casting into VSM, page invalidation from moving rocks, and the 1080p page budget. That is the measured contest.
3. **Alternatives to measure alongside:**
   - screen-space contact shadows on `SunLight` (cheap, local, for the per-rock look);
   - ray-traced shadows, if hardware RT is enabled on the target machine;
   - the light function (candidate A above) against per-object custom data (candidate B).
   - Distance-field shadows are out: Niagara mesh particles have no distance fields.

**Equal quality**, for decision 2's rule, means:

- the penumbra width is within 25% of `L tan(alpha)` where it is measurable;
- no visible acne, banding, aliasing or temporal flicker during a 10 s flight;
- the shadow is present and correct at all of these test views: Saturn filling the view (about 200,000 km), mid-ring (about 50,000 km above the B ring), in the dense layer (50 m above the B ring), and the ship under the B ring.
- Screenshot pairs go in `Saved/RingShadowShots/` and their file names are recorded in the amendment.

### 4.4 Budget accounting

The 0.3 ms covers everything ring-shadow related at 1080p with Saturn filling the view: the extra ALU in the deck, haze, far disc and rock materials, plus any light function or shadow pass. Expected analytic cost per pixel: one ray-plane intersection, about 5-15 band reads for the box filter, and one `exp`. The `.ush` must keep this bounded: binary search, or a precomputed per-profile cumulative table, instead of a linear scan over all bands. If the measured cost exceeds 0.3 ms, the first levers are:

1. a 1D profile **texture** with a prefiltered (cumulative-transmittance) channel, which makes the box filter two taps;
2. skipping the filter where the footprint is below a pixel.

## 5. Testing

- **16a (TDD):** `Source/SOLTest/Tests/RingShadowMathTest.cpp`, paths `SOLTest.RingShadowMath.*`, written by a subagent against the plan's 16a contract before the production code exists.
- **16c:** a test that the shader profile table matches `RealTauProfile`. Material output is checked by screenshots and PIE smoke checks (`-SOLStart=Saturn -SOLAltitudeKm=... -SOLLookAt=Saturn -SOLSmokeShot=...`, `-SOLRingStartKm`, `-SOLRingHeightKm`).
- **16d:** if candidate B is chosen, world tests for the custom-data writer, in the style of `SOLTest.GasGiantVisuals.*`.
- **Performance:** `ProfileGPU` numbers per view, recorded in amendments. Then the adversarial opus review and the full suite, as in CLAUDE.md.

## 6. Acceptance

1. All four shadow directions are visible and physically placed at Saturn, with a soft penumbra of the predicted width. Jupiter, Uranus and Neptune are checked by screenshot.
2. The measured GPU cost is <= 0.3 ms at 1080p with Saturn filling the view.
3. The analytic-versus-VSM measurements, and the choice they lead to, are recorded here.
4. `SOLTest.RingShadowMath.*` passes, the full suite passes, and the adversarial review findings are fixed or accepted as tech debt.
5. `ARCHITECTURE.md`, `GAME_MECHANICS.md` and the in-game Info screen are updated.

## 7. Open questions (answered by the user, 2026-10-13)

1. **Viewing geometry at the game's dates.** Saturn's equinox was 2025-05-06. At the game's default 2026-2027 sim time the Sun is only a few degrees above Saturn's ring plane, so the ring shadow on the planet is a narrow band just off the equator, and the planet's shadow on the rings is long and thin.
   - **Answer:** add a **developer flag `-SOLSimDate=<date>`** that sets the starting sim time, for verification and tuning (documented in `ARCHITECTURE.md`'s command-line flag list). Players see other geometries through time warp; no player-facing date setting is added.
2. **Planet oblateness.**
   - **Answer:** **not modelled.** The planet shadow uses a sphere of the registry's mean radius, which matches the spherical planet meshes that are rendered today. **If planet shapes ever become oblate, the planet-shadow shape (`PlanetShadowTransmittance`, the `.ush` mirror and every material that calls it) must be updated to match**, or the shadow will disagree with the visible limb. Recorded in CLAUDE.md's Known tech debt.
3. **Planet shadow on the ship** (eclipse).
   - **Answer:** **on.** The ship and dropped targets receive both the planet's eclipse shadow and the ring shadow, whichever candidate 16d implements.

## Amendment 1 — 16a and 16b results (2026-10-14)

### 16a: math module and profile port

- `MinorBodies/SOLRingShadowMath.{h,cpp}` implements the 16a contract. The 52-band table was read out of `M_SOLRingFar`'s Custom node (its HLSL is stored as plain text in the `.uasset`) and ported unchanged to `RealTauProfile`: Jupiter 3 bands, Saturn 33, Uranus 11, Neptune 5. **No reconciliation was needed.** Every band lies inside its `SOLPlanetRing::RealRings()` entry, and all the tests' landmarks hold for the material's own values: B, A, Cassini, Encke, C and D; Uranus's epsilon ring and the delta-lambda gap; Jupiter and Neptune below Saturn.
  - The table keeps each band's **real** width. The far-disc material also widens the narrow Uranus and Neptune ringlets to at least 15 km or 1.25 px so they stay visible, and modulates tau with radial noise. The shadow does neither: it uses the mean profile.
  - `LayerFractionTowardSun` uses the Abramowitz-Stegun 7.1.26 erfc (error <= 1.5e-7). It is symmetric by construction, and the `.ush` uses the identical formula. `SunVisibleFraction` computes the lens half-angles with `atan2` and cancellation-free denominators, so it stays accurate in float even when the Sun's disc is 1000 times smaller than the planet's.
- **One test was wrong against the contract, and it was corrected.** `SOLTest.RingShadowMath.PenumbraHalfWidth` expected `PenumbraHalfWidthM(1, PI/4) == 1`. The contract clamps alpha to `RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD` = 0.5 rad, so the contract value is `tan(0.5)` = 0.546. The example now uses `alpha = atan(0.5)`, which is inside the clamp and gives exactly 0.5. No other 16a test was changed. **Test corrected to match the contract's clamp**: a spec-consistent fix made by the implementer, not separately approved by the user.
- **Added beyond the contract**, with tests in the same file:
  - `BuildShaderProfileSource`, which generates the shaders' table (test `ShaderProfileMatchesCpp`);
  - the host helpers `ProfileIndexForPlanet`, `SunDirectionAndAngularRadius` and `EncodeSunDirectionWithAngularRadius` (test `HostInputs`);
  - `ComputeViewpointShadowState` (test `ViewpointShadowState`).
- **Final run (2026-10-14), in the editor:** `SOLTest.RingShadowMath.*` (33 tests), `SOLTest.SimClock.*` (including the new `ParseSimDate`), `SOLTest.GasGiant*` and `SOLTest.Ring*`: 145/145 passed. The full suite was not run (16e).

### `-SOLSimDate` and the in-game ring geometry

- `-SOLSimDate=<date>` starts the sim clock at that UTC date. It accepts ISO 8601 (`2032-06-01`, `2032-06-01T12:00:00Z`, or with an offset such as `+02:00`, converted to UTC) or Unreal's `2032.06.01-12.00.00` (`FSOLSimClock::TryParseUtcJulianDate`). A date and time must use the `T` form (or the whole value must be quoted): the command line splits on spaces. An impossible date (`2026-02-30`) or a bad value logs a warning and the real date is kept. The flag is compiled out of Shipping builds (Amendment 4).
- **Finding: section 7 question 1's premise does not hold in the game.** That question assumed Saturn's real 2025 equinox and a narrow ring shadow in 2026-2027.
  - The body registry builds each pole from the obliquity only, tipped toward ecliptic +X (SDD 12 convention, `SOLBodyRotation::TiltRotation`), not from the IAU pole.
  - So the Sun's elevation over the ring plane is `-asin(sin(tilt) cos(L))`, where `L` is the planet's heliocentric longitude.
  - At the game's 2026-10 date that is about -25 deg at Saturn, -22 deg at Uranus and -28 deg at Neptune, so the Sun is high over every ring.
  - **2032 is an in-game Saturn equinox.** The first 2032 screenshot showed the rings edge-on.
  - Jupiter never exceeds its 3.1 deg tilt.
  - The verification shots therefore use `-SOLSimDate=2026-10-13T00:00:00Z`. If the poles are ever made real (IAU RA/Dec), the dates in this SDD's screenshots need to be revisited.

### 16b: analytic against VSM and alternatives

**Method.** Standalone `-game` at a 1920x1080 render with the window in the foreground. `ProfileGPU` runs through `-SOLSmokeConsole` and profiles one frame per run; repeat runs agree within about 0.01 ms for BasePass and Translucency. The analytic A/B uses the developer console variable `sol.RingShadows`: at 0 it sends a zero Sun vector, so each material's ring-shadow call returns at its first test. **Correction (Amendment 4):** at the time of these runs the variable only gated the gas giant decks, haze and clouds; the far disc's planet shadow and the rocks' shadow stayed on in the "off" runs. It now gates every shadow, and the re-measured A/B is in Amendment 4. The run names in the table are the screenshot and log names in `Saved/RingShadowShots/`.

| View | Configuration | BasePass | Translucency | Shadow passes | Frame |
|---|---|---|---|---|---|
| Saturn filling the view: 64,000 km away, 60,000 km above the ring plane (`fill_on1/2`) | analytic on | 0.255, 0.256 ms | 0.286, 0.292 ms | - | 4.8, 5.1 ms |
| same (`fill_off1/2`) | analytic off (`sol.RingShadows 0`) | 0.262, 0.261 ms | 0.288, 0.285 ms | - | 5.7, 5.2 ms |
| In the dense layer, 50 m below the B ring on its lit side (`dense_analytic`) | analytic: planet shadow plus statistical self-shadow | 0.742 ms | 0.143 ms | ShadowDepths 0.100 ms (empty) | 7.3 ms |
| same (`dense_vsm`) | VSM on `SunLight` (`set DirectionalLightComponent CastShadows 1`; every mesh and Niagara component `CastShadow 1`) | 0.668 ms | 0.143 ms | ShadowDepths 0.905 ms, MarkPages 0.168 ms, VSM projection 0.735 ms; Niagara also re-simulates the 1M-rock system for the shadow views (+2.2 ms of GPU sim) | 11.9 ms |
| same (`dense_contact`) | screen-space contact shadows only (VSM off, no cascades, `ContactShadowLength` 0.05) | 1.285 ms (noisy frame) | - | light pass 0.098 ms | 9.5 ms |

- **Analytic cost for (a) and (b): not measurable.** It is below the 0.01 ms run-to-run noise; the "on" runs were not slower than the "off" runs. The instruction counts agree: `M_SOLRingFar` has 267 pixel instructions with the soft planet shadow and had 273 with the old cylinder. The deck's ring-shadow block sits behind a `[branch]` on the lit side. This is well inside the 0.3 ms budget.
- **VSM: hypothesis 1 holds by construction, and hypothesis 2 was measured.**
  - VSM cannot draw (a) or (b) at all: its clipmap covers 42 km while these shadows are 1e5 km long, the far disc is translucent, and far bodies are log-compressed in render space.
  - In the dense layer, VSM's **fixed** cost, before the rocks are even drawn into it, is about 1.8 ms of shadow passes plus about 2.2 ms of repeated Niagara simulation. That is more than ten times the whole budget.
  - The screenshot (`dense_vsm.png`) shows no visible gain over the analytic self-shadow.
  - **Rejected** for this ticket under decision 2's rule.
- **Contact shadows** cost about 0.1 ms in the light pass. They remain a candidate **addition** for per-rock contact detail in 16d; their look has not been judged yet.
- **Hardware ray-traced shadows** were not measured. They are deferred to 16d, with the rest of the ship and rock contest.

**Decision per direction:**

- **(a) ring on planet and (b) planet on rings: analytic.** Built in 16c.
- **(d) rock self-shadow: the analytic statistical model.** It is already in `M_SOLRingRock`: 16c built it together with the planet shadow, because both need the same inputs. 16d judges contact shadows as an add-on.
- **(c) ship and targets: not decided.** The cheapest candidate found evaluates the same viewpoint state on the CPU (`ComputeViewpointShadowState`, already computed every frame for the rocks) and scales the ship's and targets' sunlight, at zero GPU cost. 16d chooses between that, a light function (candidate A) and per-object data (candidate B).

## Amendment 2 — 16c: planet and ring materials (2026-10-14)

- **Shader include.**
  - `FSOLTestModule::StartupModule` maps `SOL::SHADER_VIRTUAL_DIRECTORY` (`/Project/SOLTest`) to `<Project>/Shaders`. The module did not need the `Projects` dependency, only `RenderCore`, which it already had.
  - `Shaders/Private/SOLRingShadow.ush` mirrors `SOLRingShadowMath` in km, as `SOLRS_*` functions: a binary search over the band table, a box filter capped at 16 bands, the A&S erfc and the atan2 lens.
  - `Shaders/Private/SOLRingShadowProfile.ush` is **generated** from `RealTauProfile` by `SOLRingShadow::BuildShaderProfileSource()`. `SOLTest.RingShadowMath.ShaderProfileMatchesCpp` fails on any drift and writes the regenerated file to `Saved/RingShadow/`.
- **(a) Ring shadow on the gas giants.**
  - `Tools/GasGiants/create_gas_giant_materials.py` gives the deck, haze and cloud Custom nodes the include and two parameters:
    - `RingShadowSun`: the Sun direction in the mesh's local axes (whose z is the pole, which is also the ring normal), with w = alpha;
    - `RingShadowParams`: the profile index and the planet radius in km. A negative index disables the shadow.
  - The deck and haze multiply their Lambert term by `SOLRS_RingShadowTransmittance` (thin plane). The near cloud multiplies its albedo; its ambient emission is unaffected.
  - `ASOLGasGiantVisuals` sets the static parameters in `BeginPlay` and the Sun on every universe update.
- **(b) Planet shadow on the far disc.** `M_SOLRingFar`'s cylinder is replaced by `SOLRS_PlanetShadowTransmittance`, which is soft and uses the real Sun size. Instead of adding a Niagara parameter, `ASOLRingVisuals` sends `RingSunDirection` with **length 1 + alpha** (`EncodeSunDirectionWithAngularRadius`). The material normalizes it for lighting and reads alpha back as the length minus 1, with a float error of about 2e-4 relative on alpha.
- **(b) and (d) on the rocks.** The dense-layer rocks and the Mass near-field ISM share `M_SOLRingRock`.
  - Every penumbra is kilometres wide, while the rock window is 1.5 km. So the shadow state is evaluated **once per frame at the viewpoint**, on the CPU, by `ComputeViewpointShadowState`: the planet shadow, tau/|mu| at the viewpoint's radius, the Sun's side and the height.
  - The ring whose dense layer is shown writes that state to the material parameter collection `MPC_SOLRingShadow` (`RingShadowState`, `RingShadowPlane`, `RingShadowOrigin`). When the layer hides, it resets the collection to "no shadow" once.
  - Per pixel, the material adds only the pixel's own height: `base colour *= planetShadow * exp(-tau/|mu| * F(z_s))`. `F` is the fraction of the Gaussian layer toward the Sun, with sigma = the dense layer's 20 m.
  - The emissive (the 5% fake ambient) is not shadowed.
- **Screenshots** are in `Saved/RingShadowShots/`, taken with `-SOLSimDate=2026-10-13T00:00:00Z`:
  - `saturn_side_a.png`: the planet's shadow across the rings behind the planet; on the northern hemisphere, the B-ring shadow band, the lit Cassini Division band and the A ring.
  - `fill_on1.png` and `fill_off1.png`: Saturn filling the view, with the shadow on and off.
  - `uranus_side.png`: the epsilon ring's thin shadow arc.
  - `jupiter_side.png` and `neptune_side.png`: the shadows are present but faint, as their tau of 0.02-0.4 predicts. Neptune was seen from its night side.
  - `saturn_dense_B_h0.05.png`: the dark side of the B ring. The rocks are in deep shadow (`exp(-tau/mu)` is about 1e-4).
  - `saturn_dense_B_h-0.05.png`: the lit side. The rocks darken with depth into the layer.
- **Budget:** see Amendment 1's table. The cost is not measurable against the noise, far below 0.3 ms.
- **Known limitations and open items:**
  - **Penumbra width is not yet checked on screen.** Equal-quality criterion 1 is the penumbra width, and that width is a pixel or less in every whole-planet view: about 67 km per pixel at 64,000 km, against half-widths of 5-50 km. The unit tests on the C++ mirror verify it, but no zoomed screenshot does yet.
  - **The `.ush` is not checked numerically.** No GPU test compares its output to the C++; only the band table is checked.
  - **Dark-side rocks are black.** Rocks on the dark side of an opaque ring get nothing but their 5% emissive. Real rings are lit there by light that diffuses through the ring and reflects off the ring and the planet. The far disc already has a diffuse-transmission term; the rocks do not.
  - **The near-field Mass ISM rocks** read the same viewpoint state, which is written only while a dense layer is shown. **Correction (Amendment 4):** when this was written they did not; they still used the unlit placeholder `M_SOLBody` and got no shadow at all. Amendment 4 moved them to `M_SOLRingRock`.

## Amendment 3 — 16d: ship, targets and rock self-shadow (2026-10-14)

- **Choice for (c): CPU evaluation at the observer, applied by lighting channel.** `SOLRingShadow::BodySunTransmittance` (planet shadow times ring shadow for one body, any frame, ring normal = the body's pole; the ring uses the 20 m Gaussian layer, so inside a ring the ship matches the rocks' statistical self-shadow) is multiplied over **every** registry body except the Sun (planets, dwarf planets and moons eclipse the ship, not only the ringed planet) once per universe update in `ASOLBodyVisuals::UpdateShipSunlight`.
  - Scaling `SunLight` itself was ruled out: `M_SOLRingRock` already shadows itself per pixel, so the rocks would be shadowed twice, and the GBuffer clamps base colour, so the rock material cannot divide the scale back out.
  - The ship's parts and the dropped-target ISM are on **lighting channel 1 only**; the ship's fill light lights channels 0 and 1. While the observer is lit (Amendment 4: hysteresis, shadowed below `SHIP_SUN_SHADOW_ENTER_BELOW` = 0.9999, lit again at `SHIP_SUN_SHADOW_LEAVE_AT` = 0.99995), `SunLight` lights channels 0 and 1 and the new `ShipSunLight` is hidden, so there is no extra light pass. Below it, `SunLight` keeps channel 0 (rocks, belt) and `ShipSunLight` lights channel 1 at `SunIlluminanceLux x T` with the Sun light's rotation. The visibility and channel flips rebuild light proxies, so they happen only on transitions, which are logged (at Verbose since Amendment 4).
  - A light function (candidate A) and per-object custom data (candidate B) were not built: the ship uses the engine's basic material, the targets and ship are metres across while every penumbra is kilometres wide, and the CPU path costs nothing per pixel. **Limitation:** targets use the ship's value, so a target across a hard ring-shadow edge from the ship (close to the ring plane) is lit wrongly.
  - The observer inside a body's sphere gets 0 from that body (as `PlanetShadowTransmittance` defines); not reachable in normal flight.
- **Rock self-shadow:** unchanged from 16c (statistical). `SOLTest.RingShadowMath.RockSelfShadowDecomposition` checks that the material's split (viewpoint state x per-pixel `F(z x sunSide)`) equals the full `RingShadowTransmittance` with the 20 m layer at rock heights through the layer, for the Sun on either side. Contact shadows and hardware RT were **not** added: VSM's measured fixed cost (Amendment 1) already rules out shadow-casting rocks, and the analytic self-shadow reads well in the shots. **Dark-side rocks** (note only): they are not black in practice, because the ship's fill light (channel 0 and 1, 10%) and the 5% emissive light them (`ship_B_h0.050.png`); no change.
- **Verification** (`Saved/RingShadowShots/`, 1920x1080 standalone, `-SOLCombatDemo`):
  - `ship_B_h0.050/1.png`: 50 m on the B ring's dark side (`-SOLSimDate=2026-10-13T00:00:00Z -SOLRingStartKm=105000 -SOLRingHeightKm=0.05`): logged T = 5.2e-5, the ship shows only its fill light.
  - `ship_B_h-0.050/1.png`: 50 m on the lit side: T = 0.94 (the layer above it), the ship is sunlit.
  - `ship_eclipse0/1.png`: in Saturn's umbra (`-SOLSimDate=2026-10-13T06:00:00Z -SOLRingStartKm=105000 -SOLRingHeightKm=48000`; the ring-start azimuth turns with Saturn's spin, so the hour picks the side): T = 0, the ship is dark against the lit rings.
  - No combat target was on screen in these shots; targets share the ship's light and channel, which `SOLTest.ShipSunlight.*` covers on the light side only.
- **Cost** (`ProfileGPU`, `ship_gpu_h*.log`): `CopyStencilToLightingChannels` 0.022 ms whenever channel-1 primitives are drawn (new, permanent); `ShipSunLight` 0.049 ms while shadowed and visible (an umbra intensity near 0 is culled by the renderer); `SunLight` is unchanged at 0.073-0.077 ms. CPU: one loop over the registry's bodies per frame (a few transcendental calls each), not measurable in `stat unit`. Total ring-shadow budget use stays far below 0.3 ms.
- Tests: `SOLTest.RingShadowMath.BodySunTransmittance`, `BodySunTransmittanceFrame`, `BodySunTransmittanceDegenerate` (written by a subagent against the contract, before the code), `RockSelfShadowDecomposition`, `SOLTest.ShipSunlight.*` (world tests: lit, eclipse, under the B ring, back to lit), and `SOLTest.InfoText.FollowsRebinds` (the new Info paragraph). 56/56 passed with `SOLTest.GasGiantVisuals.*`.

## Amendment 4 — adversarial review fixes for 16a-16d (2026-10-14)

The user confirmed that **every body** (planets, dwarf planets and moons) eclipses the ship and targets, as Amendment 3 built it.

- **Near-field Mass ring rocks were not shadowed.** Amendment 2 said they were, but they used the unlit placeholder `M_SOLBody`. They now use `M_SOLRingRock` (`SOL::Paths::RING_ROCK_MATERIAL`) through a material instance whose new `RockTint` vector parameter (`SOL::RingRockMaterialParams::TINT`) is the ring colour. `RockTint` multiplies `ParticleColor`, which is white outside Niagara, and defaults to white, so the Niagara layer is unchanged. Outside Niagara, `ParticleRandom` is 0, so every near-field rock has the same brightness (0.55 of the tint). Verified: `nearfield_umbra.png` (in Saturn's umbra, `-SOLSimDate=2026-10-13T06:00:00Z -SOLRingStartKm=105000 -SOLRingHeightKm=0.05`) shows the near-field spheres dark like the Niagara rocks; `nearfield_lit.png` shows them lit and tinted.
- **The rock material and its collection now have a build script**, `Tools/Rings/create_ring_shadow_assets.py` (`SOL_REBUILD` rebuilds `M_SOLRingRock` in place, keeping the asset, so the Niagara renderer's reference stays valid; the collection only gains missing parameters, so their GUIDs stay stable). `M_SOLRingFar` has no script and still carries its own copy of the band table in its Custom node (it also widens narrow ringlets for visibility). Recorded as tech debt in CLAUDE.md.
- **Shader box filter bias.** `SOLRS_FilteredTransmittance` stopped after `SOLRS_MAX_FILTER_BANDS` bands but divided by the whole window, so a wider window read too bright. The rest of the window now takes the last visited band's absorption. `SOLRS_MAX_FILTER_BANDS` (`SOL::RING_SHADOW_MAX_FILTER_BANDS` = 16) and `SOLRS_SEARCH_STEPS` (`SOLRingShadow::ShaderSearchSteps()`, 6 for Saturn's 33 bands) are now generated into `SOLRingShadowProfile.ush`. `SOLTest.RingShadowMath.ShaderSearchLimits` checks that 2^steps covers the largest profile, minimally, and that both defines are generated. The named HLSL helpers `SOLRS_KM_PER_CM` and `SOLRS_ProfileIndex` replace the gas giant materials' `1e-5` and `(int)(RS.x + 0.5)` literals.
- **Ship-sunlight hysteresis.** `SOLRingShadow::NextShipShadowedState` enters the shadow below `SHIP_SUN_SHADOW_ENTER_BELOW` (0.9999) and leaves at `SHIP_SUN_SHADOW_LEAVE_AT` (0.99995) or above, and keeps its state for NaN (test `SOLTest.ShipSunlight.Hysteresis`). The transition log line is now Verbose. `BeginPlay` copies `SunLight`'s colour, temperature, specular and diffuse scales and source angles to `ShipSunLight`.
- **`sol.RingShadows` now gates every shadow** (`SOLRingShadow::AreShadowsEnabled`, `MinorBodies/SOLRingShadowSwitch.{h,cpp}`): the gas giants (zero Sun vector), the far disc (a 1 cm shadowing planet, `RING_FAR_SHADOW_OFF_PLANET_RADIUS_CM`, because `M_SOLRingFar` replaces 0 or less with a default radius), the rocks (the collection's no-shadow values) and the ship's sunlight (T = 1). **Re-measured A/B** (Saturn filling the view, the 16b `fill` view, 1920x1080, one frame each): on, BasePass 0.256 ms and Translucency 0.290 ms (`ab_fill_1b`); off, 0.255 ms and 0.281 ms (`ab_fill_0b`). So all ring and planet shadows together cost about 0.01 ms in this view, within run-to-run noise and far below 0.3 ms. A first pair of runs (`ab_fill_1`/`_0`) had one noisy frame (Translucency 0.654 ms) and was discarded.
- **Tests.** Pinned: `SOLTest.ShipSunlight.LightingChannelSetup` (the shared `SOLShipSunlight::PutOnShipLightingChannel` used by the ship's parts and the target ISM puts a primitive on channel 1 only; the target ISM default is on channel 1 only; the fill light on 0 and 1). `SOLTest.ShipSunlight.*` now pins the sim date (`2026-10-13T00:00:00Z`, through `FSOLSimClock::TryParseUtcJulianDate` and the new `USOLSimClockSubsystem::SetJulianDate`) and its all-body oracle is independent of the production loop (own planet and ring terms, ringed planets named explicitly, own ring frame). `RockSelfShadowDecomposition` gained band-edge cases (viewpoint 300 m inside either edge of the band at 105,000 km). `SOLTest.SimClock.ParseSimDate` gained impossible dates and a timezone offset.
- **Module and flag hygiene.** `FSOLTestModule::StartupModule` warns (editor builds) when `Shaders/` is missing. Moving the module to the `PostConfigInit` loading phase was evaluated and **not** done: the module holds UCLASSes, which must not load before their UObject dependencies, and the Default phase already runs before any project material loads. `-SOLSimDate` is compiled out of Shipping builds.
- **Final run (2026-10-14), in the editor:** `SOLTest.RingShadowMath`, `ShipSunlight`, `SimClock`, `InfoText`, `GasGiant*` and `Ring*`: 166/166 passed. The full suite was not run.

**Known limitations** (recorded here, not fixed):

- **Viewpoint tau is point-sampled.** The rocks' ring term uses tau at the viewpoint's radius for the whole 1.5 km window. A rock whose ray to the Sun crosses a band edge (within about `height / tan(elevation)` of it) gets the wrong band's tau. The same is true of the ship's sunlight for targets far from the ship.
- **Near-field rocks are shadowed only while a dense layer is shown**, because the collection is written only then (for example, not above 10x time warp).
- **Lighting channels and Lumen.** Lumen is not enabled in this project (no Lumen passes in `ProfileGPU`). If it ever is, check that Lumen's direct lighting honours the channel split, or the ship could be lit twice or not at all. Lighting channels also add `CopyStencilToLightingChannels` (0.022 ms, Amendment 3).
- **Pooled combat effects** (impact, shield break, spark, explosion) and the bolts are not on channel 1 and are not shadowed. They are mostly emissive, so it is barely visible.
