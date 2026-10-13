<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 14 — Gas giant fidelity: layered clouds, volumetric near layer, moving storms

Ticket `#14`. Plan: [`Docs/Plans/14-gas-giant-fidelity-plan.md`](../Plans/14-gas-giant-fidelity-plan.md). Decisions resolved with the user in a `grill-me` session on 2026-10-12.

## 1. Problem

Gas giants (Jupiter, Saturn, Uranus, Neptune) look static and flat. They need a realistic atmosphere with band and storm patterns that move.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Technique | Both: layered translucent cloud shells with animated flow-map/noise bands and storm vortices for distance, plus a true volumetric cloud layer that fades in when the ship is within a few planet radii. |
| 2 | Scope | All four gas/ice giants via one shared material system with per-planet parameters (band colours, storm count, spin); Jupiter (with a Great-Red-Spot-style storm) is the tuning reference. |
| 3 | Animation | Driven by the simulation clock (follows time warp) with an artistic speed factor so bands visibly drift at 1x; differential rotation per latitude; storms are moving vortices. Deterministic and pause-safe. |
| 4 | Budget | Shells <= 0.5 ms GPU; with the volumetric layer active <= 1.5 ms GPU total. The volumetric layer costs nothing far away (hidden, resources released after a delay like the ring dense layer). Measured with `ProfileGPU` and recorded in an amendment. |
| 5 | Architecture | A separate `ASOLGasGiantVisuals` actor per giant (like `ASOLRingVisuals`), spawned by the game mode: independent LOD, visibility and resource release, no extra per-body state in `ASOLBodyVisuals`. Placement/precision follows the render-origin snapshot conventions. |

## 3. Acceptance

Screenshots from far, mid and near for each giant; bands and storms visibly move at 1x and under warp; measured GPU cost within decision 4; pure logic (band/latitude rotation, LOD fade) unit-tested.

## 4. Open questions

Whether UE Volumetric Clouds can be used at planetary scale around a single body without precision problems (spike in 14b). **Answered by Amendment 1: yes, with a SkyAtmosphere carrying the planet centre.**

## Amendment 1 (14b) — volumetric cloud spike at Jupiter scale

Spike run on 2026-10-13 in the editor world (UE 5.8.3, RTX 2080 Ti, 1825x760 viewport, ~1.4 MPix), engine `SkyAtmosphere` + `VolumetricCloud` actors with the engine's default `m_SimpleVolumetricCloud_Inst`, nothing saved.

**Findings**

1. **Planet size.** `UVolumetricCloudComponent::PlanetRadius` and `USkyAtmosphereComponent::BottomRadius` are `ClampMax = 10000` km in the *editor UI only*; set from C++/Python, 71,492 km (Jupiter) is accepted and neither `FAtmosphereSetup` nor the cloud renderer clamps it. Cloud layers render with correct curvature at that radius.
2. **Planet centre — the deciding constraint.** Without a `SkyAtmosphere`, the cloud renderer hard-codes the layer centre at `(0, 0, -PlanetRadius)` km in absolute world space (`VolumetricCloudRendering.cpp`: "planet top at the world origin, +Z up"). That is unusable with the render origin (the observer sits near the Unreal origin and the giant's centre is in an arbitrary direction). **With** a `SkyAtmosphere` in `PlanetCenterAtComponentTransform` mode the centre is the component's location (`SkyAtmosphereCommonData.cpp`), so placing that component at the giant's render-space centre each frame works: tested with the "up" axis tilted to (0.3, 0.5, 0.81) and the centre at ~7e9 cm from the origin; the horizon, curvature and lighting are correct.
3. **Precision.** The centre is handed to the shaders as a float in km (`CloudLayerCenterKm`, marked `LWC_TODO: Precision Loss`): at ~7.2e4 km the step is ~7.8 m. The near layer is only drawn within 4 planet radii (~2.9e10 cm for Jupiter), well inside `DEFAULT_MAX_RENDER_DISTANCE_CM` (1e11 cm), so the giant's placement there is exact (no log compression) and the only error is that ~4 m quantization, which can make the clouds hop by up to a few metres at a 10 km render-origin rebase. Not visible at cloud distances of kilometres or more; accepted.
4. **Scope.** The renderer draws one `SkyAtmosphere` and one `VolumetricCloud` per scene, so the near layer can only exist around one giant at a time. That matches the design (it is shown only within a few radii of one giant). The `SkyAtmosphere` also draws a sky/limb and applies aerial perspective to everything in the scene, and cloud lighting needs a directional light flagged `bAtmosphereSunLight` (the project's `ASOLBodyVisuals::SunLight` is not today). 14d must decide whether the atmosphere's own scattering is kept (a visible limb haze) or zeroed (Rayleigh/Mie scattering 0, used only as the cloud's centre carrier), and must enable/disable the sun flag with the layer.
5. **Far away.** From 2.2 radii the default cloud material's km-scale detail is sub-pixel and the disc reads as a smooth haze: far detail has to come from the band shells (14c), confirming decision 1's split.

**Measured GPU cost** (`ProfileGPU`, `VolumetricCloud` > `CloudView` pass, camera ~50 km above the cloud base looking across the cloud tops, layer 5-45 km, tracing max distance 100 km, shadow tracing 5 km, `r.VolumetricRenderTarget.Mode 0`):

| `ViewSampleCountScale` | CloudView |
|---|---|
| 1.0 (default, max tracing distance 500 km) | 3.58 ms |
| 0.5 | 1.62 ms (Mode 2: 1.60 ms) |
| 0.25 | 0.89 ms (noisy but acceptable once TSR accumulates) |

Compose/reconstruct passes are ~0.05 ms. Cost scales with pixel count, so ~1.3 ms is expected at 1080p with 0.25.

**Chosen approach (for 14d).** Use the engine `VolumetricCloud` with a `SkyAtmosphere` as its centre carrier, both owned by the `ASOLGasGiantVisuals` of the giant the camera is nearest to (only one active at a time), the atmosphere placed at that giant's render-space centre every frame with `BottomRadius` = its radius. Start with `ViewSampleCountScale` 0.25, tracing max distance <= 100 km, shadow tracing <= 5 km, with a project cloud material (band colours, flow from the 14a phases) replacing the default. Faded by `NearLayerBlend` and released via `StepNearLayer` (components unregistered / actors hidden). If the 1.5 ms budget cannot be met at the target resolution, fall back to a raymarched shell material (no engine clouds) and record it in a further amendment.

## Amendment 2 (14c) — band shells as built

- **Two shells per giant, not N.** `ASOLGasGiantVisuals` draws an opaque deck (`M_SOLGasGiantDeck`) at exactly the planet radius and one translucent haze shell (`M_SOLGasGiantHaze`) at `GAS_GIANT_HAZE_RADIUS_SCALE` = 1.003 radii, both on the engine sphere, placed with the body's own `ComputeBodyRenderPlacement` and orientation (so far-away log compression and spin match the body exactly). The deck *replaces* the placeholder body mesh: `ASOLBodyVisuals` gained `SetBodyMeshHidden(bodyIndex, bHide)` (no new per-body state; it toggles the component's visibility; exposure still counts the body). Hiding avoids z-fighting between two coincident spheres at any distance and keeps the visible surface at the true radius. If the materials are missing the giant keeps its placeholder mesh (logged warning).
- **Procedural, no textures.** Both materials are one Custom HLSL node each (built by `Tools/GasGiants/create_gas_giant_materials.py`): hashed value-noise fBm on the unit sphere sampled at band-advected longitude `lon - 2 PI phase(lat)`, seamless (3D noise, no UV seam), stretched 4x in latitude frequency so features run east-west. Bands are `cos(2 PI (s F + 0.3 sin(2 PI 1.3 s)))` of the turbulence-warped `sin(lat)` (uneven widths, a light equatorial zone). The deck's turbulence and fine detail share one 7-octave fBm pass. Lighting matches `M_SOLBody` (unlit, Lambert toward `SunDirection` + ambient, `E / pi`) with a mild limb darkening `(N.V)^0.25`.
- **Phases.** The 16 `ComputeBandPhases` samples (4 float4 parameters) are interpolated across latitude on the shortest arc. Consequence: once neighbouring samples drift more than half a turn apart (after ~days of real time at 1x, sooner under warp) the interpolated longitude between them jumps; the band texture is mostly longitude-uniform so this is rarely visible, but it is a known artefact (fixed in Amendment 3 with a wrap-safe cross-fade). The haze gets its own phases at `GAS_GIANT_HAZE_SPEED_SCALE` = 1.35x the deck's speed factor so high clouds visibly slide over the bands.
- **Storms.** Per planet: an optional authored primary storm (Jupiter: lat -0.391 rad, radius 0.07 rad, drawn 1.6x wider in longitude, reddish; Neptune: dark spot at -0.35 rad) plus 2-5 generated ovals (`GenerateStorms`, seed `GAS_GIANT_STORM_SEED` + body index, within +-`GAS_GIANT_STORM_MAX_LATITUDE_RAD`, radius `GAS_GIANT_STORM_MIN/MAX_RADIUS_RAD`), dropping any generated storm whose latitude band overlaps the primary. Each frame `StormLongitudeRad` places them in the body frame (8 float4 parameters: lat, lon, radius, kind), so a storm keeps its shape while riding its band.
- **Per-frame CPU.** Parameters are set by cached index (no name lookups, no allocation), and only while a shell was rendered in the last 0.5 s (`WasRecentlyRendered`), plus once at start.
- **Translucency order.** The haze uses sort priority `GAS_GIANT_HAZE_TRANSLUCENCY_SORT_PRIORITY` = -10 so a ring (`NS_SOLRingFar`, priority 0) crossing in front of the planet draws over the haze, not under it.
- **Measured GPU cost** (`ProfileGPU`, RTX 2080 Ti, 1920x1080, screen percentage 100, Jupiter filling the view from 15,000 km, minus the same passes with Jupiter 3,000,000 km away): deck (BasePass) 0.339 - 0.156 = **0.18 ms**, haze (Translucency) 0.280 - 0.052 = **0.23 ms**, **0.41 ms total**, within decision 4's 0.5 ms shell budget. Before sharing the fBm pass and cutting the haze to 3 octaves it was 0.32 + 0.28 = 0.60 ms. At 1280x720 the total is ~0.3 ms.
- **Screenshots** (`Saved/GasGiantShots/`, standalone `-game -SOLSpectator -SOLStart=<giant> -SOLAltitudeKm=<km> -SOLLookAt=<giant> -SOLSmokeShot=8`, no new flag needed): Jupiter far (3,000,000 km), mid (250,000 km) and near (15,000 km, also at 1080p); Saturn (300,000 km), Uranus and Neptune (80,000 km) mid. Close up (15,000 km) the deck is soft (finest fBm octave ~100 km); near detail is 14d's job.

## Amendment 3 (14d) — volumetric near layer as built, and 14c leftovers

**Near layer.** Built as Amendment 1 chose, owned by each `ASOLGasGiantVisuals` (no shared manager):

- **Residency and fade.** Each universe update the blend is `NearLayerBlend(observer distance, radius, GAS_GIANT_NEAR_FADE_IN_RADII = 2, GAS_GIANT_NEAR_FADE_OUT_RADII = 4)` from the exact double positions, stepped through `StepNearLayer` with real (unwarped) `FApp::GetDeltaTime()`. `Activate` creates and registers the two components (hidden); while the blend is > 0 they are visible; `Release` (20 s hidden) destroys them and the material instance. Fade constants were kept: from 2-4 radii the layer reads as a soft band-coloured haze, and the cost there is near zero (the cloud tracing only runs on pixels that reach the layer, 0.02 ms at 2.4 radii).
- **Atmosphere neutralised.** The `USkyAtmosphereComponent` is only the planet-centre carrier (`PlanetCenterAtComponentTransform`, placed at the giant's render-space centre every update, `BottomRadius` = planet radius): Rayleigh/Mie scattering, Mie and other absorption and multi-scattering are 0, sky and sky-and-aerial-perspective luminance factors are black, aerial-perspective distance scale and height-fog contribution are 0, and `bRenderInMainPass` is false. Checked by A/B screenshots at Saturn, 40,000 km (`ShowFlag.Atmosphere 0` vs on, `Saved/GasGiantShots/saturn_nearlayer_on_off.png`): the rings, stars and limb are pixel-identical outside the planet disc. Its remaining cost is ~0.05 ms (LUTs + the `SkyAtmosphere` pass).
- **Sun light.** `ASOLBodyVisuals::SetSunDrivesAtmosphere` flags the sun light as the atmosphere light only while a near layer is shown (the cloud renderer lights from atmosphere light 0); its sun disk colour scale is black permanently, so no engine sun disk appears. `bUsePerSampleAtmosphericLightTransmittance` is on so cloud on the night side gets the planet's shadow.
- **Cloud material** (`M_SOLGasGiantCloud`, volume domain, additive, `UsedWithVolumetricCloud` — without that flag the renderer draws the layer black). Per sample: position relative to `PlanetCenter` gives the altitude in the layer; in the body frame (`BodyAxisX/Y/Z`) the latitude and band-advected longitude, using the deck's 16 phases with the same wrap-safe cross-fade as the shells; coverage follows the band profile (zones 0.6, belts 0.35); 3 octaves of value noise at 300 km features with altitude as a slow third axis shape the tops, eroded toward the layer top; albedo from the zone/belt colours; extinction `0.0005 /m * density * NearBlend`; emission `albedo * extinction * 0.2` as a sky-light stand-in so cloud sides away from the Sun are not black. Layer `GAS_GIANT_CLOUD_LAYER_BOTTOM_KM` = 2 to +40 km, `ViewSampleCountScale` 0.25, shadow sample scale 0.5, tracing max 100 km, shadow tracing 5 km, tracing start max distance = 4 radii.
- **Shells.** The haze fades out as the volumetric fades in (`HazeFadeForNearBlend` = 1 - blend on its opacity, hidden at 0). The opaque deck stays as the floor under the layer (fading it would leave a hole under the cloud gaps), so "shells fade out" applies to the haze only.
- **One per scene.** Not enforced: the giants are AU apart, so two near layers can never be visible together; a hidden-but-resident layer from another giant is not in the scene (invisible components are not added).

**Measured GPU cost** (`ProfileGPU` via `-SOLSmokeConsole`, RTX 2080 Ti, 1920x1080, screen percentage 100, Jupiter):

| View | CloudView (480x270) | Reconstruct + compose | Sky LUTs + pass | Volumetric total |
|---|---|---|---|---|
| 2,000 km, looking down | 0.495 ms | 0.106 ms | 0.05 ms | **0.65 ms** |
| 15,000 km | 0.434 ms | 0.108 ms | 0.05 ms | **0.59 ms** |
| 100,000 km (2.4 radii) | 0.018 ms | 0.088 ms | 0.05 ms | 0.16 ms |

With the deck full-screen (0.27 ms, below) the total with the volumetric layer active is ~0.9 ms, within decision 4's 1.5 ms; the raymarched-shell fallback was not needed.

**Shell cost after the phase-wrap fix.** The deck now evaluates its fBm twice in the cross-fade zone (half of every gap between band samples): full-screen deck 0.433 - 0.161 = **0.27 ms** (was 0.18). The haze could not be measured full-screen any more, because it is hidden whenever the planet fills the view (blend 1 within 2 radii); its last full-screen figure was 0.23 ms and at most ~1.5x that with the cross-fade, but it never fills the screen while visible. In practice the shells stay within 0.5 ms; the theoretical full-screen deck + haze (~0.6 ms) cannot occur.

**14c leftovers.**

- **Phase wrap fixed.** The shortest-arc interpolation between band samples is replaced by a wrap-safe lookup (`SOLGasGiant::BandBlendAt`, mirrored in all three materials): each sample's phase advects the noise rigidly, and the two neighbouring samples' fields are cross-faded over the middle `2 * GAS_GIANT_BAND_BLEND_HALF_WIDTH` (0.5) of the gap, so the result depends only on each phase modulo one turn. Tested in `SOLTest.GasGiantMath.BandBlendExamples`, `BandBlendDegenerate` and `BandBlendWrapSafeOverTime` (a field advected through it changes < 2e-3 per 5 s step over 1e5 s at Jupiter's rates, while the old formula jumps > 0.1 in the same window). Trade-off: differential rotation is now stepwise (16 rigid strips with soft shear zones) instead of a linear shear, which reads the same for longitude-dominated bands.
- **Drift verified.** New dev flag `-SOLSmokeWarpSteps=<n>`. At 1000x two shots 2 s apart (`drift_warp1000_pair.png`) and at 1x two runs 5 minutes apart (`drift_1x_5min_pair.png`) show the band edges and ovals moving. Band *positions* barely change by design: the band profile is latitude-only and drift moves the turbulence and storms along it.
- **Great Red Spot** found (`jupiter_grs.png`, `grs_sheet_3600x.png`, 250,000 km at 3600x): reddish oval with a light collar at 22 deg S, crossing the disc in ~8 real seconds at that warp.
- **Tests** for the actor: `SOLTest.GasGiantVisuals.*` (bare game world with the universe subsystems, real `BeginPlay`/`EndPlay`): `SetBodyMeshHidden` (valid/invalid indices, new `IsBodyMeshHidden`), the deck replacing and restoring only its own giant's placeholder mesh with a mesh, material and planet-sized scale, the near layer released when far away, and an unknown planet / missing body visuals being inert.
- **Editor setting.** `bThrottleCPUWhenNotForeground` was never persisted (no entry in any project or user config), so there was nothing to restore; tests and profiling now bring the editor to the foreground instead.

**Open (for 14e).** The engine sphere's tessellation shows as a faceted limb within ~1 radius (`s_on.png`, Saturn at 40,000 km; existed since 14c); the cloud look is generic (one noise for all four giants, speckly on Saturn at 40,000 km) and has no storm-aware clearing over the GRS; cloud lighting uses the 1 AU `SUN_ILLUMINANCE_LUX` like the shells; no PIE check of the 20 s release while flying away (covered by the pure `StepNearLayer` tests and the `EndPlay` release path).


## Amendment 4 (14d review) — adversarial review fixes

Findings of the 14d adversarial review and what changed:

- **Component name reuse (high).** The near layer's `NewObject` calls used fixed names, so a re-activation right after a release could reuse a destroyed component's name before garbage collection. They now use `NAME_None` (unique names). `SOLTest.GasGiantVisuals.NearLayerLifecycle` drives activate -> hide -> release -> immediate re-activate and checks a fresh registered component.
- **Shared sun flag.** `ASOLBodyVisuals::SetSunDrivesAtmosphere(requester, bool)` keeps a set of requesters; the flag is on while any holds it (`IsSunDrivingAtmosphere`). Tested in `SunDrivesAtmosphereRequesters` and by the lifecycle test (on near, off far, off after `EndPlay`).
- **Material loading.** `M_SOLGasGiantCloud` is loaded once in `BeginPlay` into a `UPROPERTY`; if missing it warns once and the near layer stays inert (blend forced to 0).
- **Stale first frame.** The parameter upload now also runs while the near layer is shown or the giant is in the player camera's view cone (`SOLGasGiant::IsSphereInViewCone`, half-angle over the horizontal FOV's diagonal; no camera = visible), not only after a shell was rendered, so the first frame back in view is current.
- **High-warp aliasing.** Band phases and storm longitudes are evaluated at a drift clock: it starts at the sim time in `BeginPlay` and advances by `CappedDriftStepSeconds(sim step, real step, GAS_GIANT_BAND_MAX_TIME_WARP = 1000)`. At or below 1000x drift is still the exact function of sim time (from the actor's start); above it, it runs at 1000x speed. Trade-off: after a stretch above 1000x, drift no longer matches the sim-time formula (it is a presentation clock, like the rings' co-rotation angle).
- **Materials.** The noise hash is integer-based (`uint3` lattice hash) instead of large float products; band phases are picked by vector-component selection instead of a dynamic `float P[16]` index; the storm loop is `[unroll]`ed. The storm east-west stretch and primary-kind threshold come from a `StormShape` parameter set from `SOL::GAS_GIANT_STORM_EAST_WEST_STRETCH` / `GAS_GIANT_PRIMARY_STORM_KIND_THRESHOLD`; `AmbientLight` is now set (`GAS_GIANT_AMBIENT_LIGHT`); the cloud tunables (`CloudNoise`, `CloudAmbientLuminance`) are set from `GAS_GIANT_CLOUD_*` constants. Materials regenerated; the far, near and near-layer views look as before (`Saved/GasGiantShots/post_pair.png`).
- **Info screen.** `SOLMenuText::INFO_PROSE` has a gas giant paragraph (bands, drift by latitude, storms riding bands, drift capped at high warp, near volumetric layer); `SOLTest.InfoText.FollowsRebinds` checks it is present.
- **Constants, tests and docs.** `RECENT_RENDER_SECONDS`, the storm kinds, `radiusM * 100` (now `METERS_TO_CM`) moved to `SOLConstants.h`. The deck scale test now compares against `ComputeBodyRenderPlacement(...).RadiusCm / BODY_MESH_RADIUS_CM`. Lines over 120 columns were rewrapped, and stale text in the Python header, `GAME_MECHANICS.md` and the `ARCHITECTURE.md` frame diagram was fixed.
- **Sunlight check.** With the ship 20,000 km above Jupiter (near layer active, sun flagged as atmosphere light) and 250,000 km away (layer off), the ship's pixels differ by 1.2/255 on average (engine glow flicker), so the flag does not change how the ship is lit (`ship_near_jupiter.png`, `ship_far_jupiter.png`, `ship_ab.png`). The ship pawn ignores `-SOLLookAt`, so Jupiter is behind the camera in those shots.
