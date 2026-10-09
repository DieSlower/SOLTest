<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SOLTest architecture

A high-level map of how the game's systems fit together: modules, the per-frame update order, coordinate frames, key types and the main data flows. It is a living reference, not a design record. The *why* behind each decision lives in the SDDs ([`SDDs/1-solar-system-architecture.md`](SDDs/1-solar-system-architecture.md) for the cross-cutting decisions, [`SDDs/2-foundations-flight-scaffold.md`](SDDs/2-foundations-flight-scaffold.md) section 3 for what Part 1 actually built, [`SDDs/3-jump-map.md`](SDDs/3-jump-map.md) for Part 2, [`SDDs/4-surface-lock.md`](SDDs/4-surface-lock.md) for Part 3). Exact numbers and bindings are in [`GAME_MECHANICS.md`](GAME_MECHANICS.md).

**Status:** Part 1 (foundations, ship flight, HUD) and Part 2 (jump map) are complete. Part 3 (surface-lock) is built: the pure logic (`Level/SOLSurfaceLock`, 3a) and its engine integration (3b: the `L` key, `USOLShipSubsystem` stepping the lock state, the flight processor applying the alignment per substep, the HUD status/hint line, and the jump clearing the lock). Part 4 (star field) is built: the offline bake and import (`Tools/StarField/`, 4a/4b) and the runtime sky actor `ASOLStarField` (4c). Part 6 (weapons, issue #7; its sub-parts are labelled 7a-7f after the issue's plan file, commits say "Part 6x") is built: the pure combat math (7a), the combat subsystem with fire/drop input (7b), visuals (7c), audio (7d) and asteroid/ring-rock hits (7e); 7f (docs, review, verification) is closing out the adversarial-review fixes (SDD 7 Amendment 4).

---

## 1. Overview

SOLTest is a 1:1-scale solar-system space sim: the player flies a small ship in third person among the Sun, eight planets, five dwarf planets and their major moons on real Keplerian orbits, with a sim clock and time-warp, gravity, a reference-frame/target system and a sim-style HUD. The core architectural bet has three parts:

1. **Double-precision universe coordinates.** Every authoritative position is an `FVector3d` in meters in a right-handed ecliptic frame, far beyond what Unreal's world can hold.
2. **Observer-relative rendering.** Unreal's world is only a render view. A render origin near the observer maps to Unreal (0,0,0), and every body is re-placed from universe coordinates each frame. Far bodies are depth-compressed with their angular size preserved.
3. **MassEntity for scale.** The ship is a Mass entity stepped by a Mass processor. The Pawn is a thin input and camera proxy. The same pattern is meant to extend to NPC ships, asteroids and projectiles, with ~1M entities as the goal.

Throughout the code, the rule is **pure logic in plain C++, engine glue in thin UObjects** (see [section 7](#7-testing-architecture)).

---

## 2. Module map

All code is in the single game module `Source/SOLTest/`, organized by feature folder (`Docs/STYLE_GUIDE.md` §14.2). Arrows point from a module to what it depends on.

```mermaid
flowchart TD
    Game["Game<br/>game mode, input helpers, debug spectator"]
    Menu["Menu (Part 7, issue #8)<br/>menu state, main and pause menus, menu camera"]
    Ship["Ship<br/>Mass ship, pawn, smoke scripts"]
    UI["UI<br/>flight HUD, radar, F3 panel"]
    Targeting["Targeting<br/>candidates, selection, M lock"]
    Visuals["Visuals<br/>body meshes and sun light"]
    StarField["StarField (Part 4)<br/>star data asset, sky actor"]
    Map["Map (Part 2)<br/>jump-map picking math"]
    Combat["Combat (Part 6)<br/>bolts, targets, damage, sweeps"]
    MinorBodies["MinorBodies (Part 5)<br/>belt and ring-rock Mass entities"]
    Level["Level (Part 3)<br/>pure surface-lock state machine and alignment math"]
    Flight["Flight<br/>pure flight and targeting math"]
    Universe["Universe<br/>clock, orbits, registry, anchor, render origin"]
    Const["SOLConstants.h<br/>constants, tunables, asset paths, CLI flags"]

    Game --> Ship
    Game --> Menu
    Menu --> Ship
    Menu --> Universe
    Ship -. "Esc, map open/close, menu state" .-> Menu
    UI -. "hidden while a menu shows" .-> Menu
    Game --> UI
    Game --> Visuals
    Game --> StarField
    Ship --> Targeting
    Ship --> Flight
    Ship --> Level
    UI --> Level
    Ship --> Universe
    Ship <--> UI
    UI --> Targeting
    UI --> Universe
    UI --> Flight
    Targeting --> Flight
    Targeting --> Universe
    Visuals --> Universe
    Ship --> Combat
    Combat --> Ship
    Combat --> Targeting
    Combat --> MinorBodies
    Combat --> Universe
    Universe -. "IsSOLGameWorld gate" .-> Game
    Flight --> Const
    Universe --> Const
    Map --> Const
    Level --> Const
```

`Ship` and `UI` depend on each other: the pawn opens the F3 panel and forwards radar zoom keys to the HUD, and the HUD reads the pawn's joystick and camera state. `Ship` and `Combat` also point both ways: the pawn hands the trigger, aim and drop requests to `USOLCombatSubsystem`, which reads the player ship's state from `USOLShipSubsystem`. The Universe subsystems ask `ASOLGameMode::IsSOLGameWorld` whether to exist at all. Every module includes `SOLConstants.h`. Only the pure leaves are drawn above.

| Folder | Responsibility | Key files |
|---|---|---|
| `Universe/` | Sim clock, Kepler orbits, body registry, anchor selection, render origin and far placement, and the subsystem that drives the frame | `SOLAnchorSubsystem`, `SOLBodyRegistry(Subsystem)`, `SOLSimClock(Subsystem)`, `SOLKepler`, `SOLRenderOrigin`, `SOLRenderPlacement`, `SOLBodyFrameCache` |
| `Flight/` | Pure flight model, gravity, collision, warp carry, and target-picking math | `SOLFlight`, `SOLTargeting` |
| `Ship/` | Player ship as a Mass entity, its processor and subsystem (including the surface-lock state and its per-substep alignment), the Pawn proxy, scripted smoke tests | `SOLShipSubsystem`, `SOLShipFlightProcessor`, `SOLShipFragments`, `SOLShipPawn`, `SOLShipSmokeFlight`, `SOLShipSmokeInput`, `SOLSurfaceLockSmoke` |
| `Targeting/` | Target candidates, selection, the M frame lock, reference velocity | `SOLTargetingSubsystem`, `SOLTargetable` |
| `UI/` | Canvas flight HUD, radar, orbit lines, F3 speed panel, and their pure layout and format math | `SOLFlightHud`, `SOLSpeedPanelWidget`, `SOLRadarLayout`, `SOLOrbitLines`, `SOLHudFormat`, `SOLSpeedStepper`, `SOLHudSmoke` |
| `Visuals/` | Places body meshes each frame and sets their per-body sun direction | `SOLBodyVisuals` |
| `StarField/` | Star field (Part 4, issue #5): the baked star data asset (4b) and the runtime sky actor (4c) that draws the bright-star sprites and the faint-star cubemap sphere. Content is produced offline by `Tools/StarField/` (bake, then `import_star_field.py`, then `create_star_field_materials.py`) | `SOLStarFieldData`, `SOLStarField` |
| `MinorBodies/` | Minor bodies (Part 5, issue #6): the asteroid belt's pure-logic generator (5b), the shared minor-body orbit math, Mass fragments and orbit processor, the subsystem that spawns and steps the belt entities, and the belt's instanced-mesh visuals (5c); real ring structure/gap data (5d) and the near-field ring-rock patch system (pure logic plus a streaming Mass pool, 5e-i/ii) | `SOLAsteroidBelt`, `SOLMinorBodyOrbit`, `SOLMinorBodyFragments`, `SOLMinorBodyOrbitProcessor`, `SOLMinorBodySubsystem`, `SOLAsteroidBeltVisuals`, `SOLPlanetRing`, `SOLRingPatch`, `SOLRingDense`, `SOLRingSubsystem`, `SOLRingVisuals` |
| `Combat/` | Weapons, targets and destruction (Part 6, issue #7): pure bolt, damage, grid, drop and fire-cadence math (7a), and the combat subsystem that owns the pooled bolts and targets, sweeps bolts against targets and minor bodies, and queues one-shot events for the visuals and audio (7b, 7e); the combat visuals actor that draws them (7c) and the combat audio actor that plays their placeholder sounds (7d) | `SOLBoltMath`, `SOLDamageMath`, `SOLCombatGrid`, `SOLTargetDrop`, `SOLFireCadence`, `SOLCombatTypes`, `SOLCombatSubsystem`, `SOLCombatVisuals`, `SOLCombatAudioRules`, `SOLCombatAudio` |
| `Map/` | Jump map (Part 2): picking, orbit-camera and warp-curve math, the map mode and its camera, the jump sequence, the smoke scripts | `SOLMapPicking`, `SOLMapCamera`, `SOLWarpCurve`, `SOLMapModeSubsystem`, `SOLJumpSubsystem`, `SOLMapSmoke`, `SOLMapPickSmoke`, `SOLJumpSmoke` |
| `Level/` | Surface-lock (Part 3) pure logic: the engage/warn/release state machine and the up-alignment math. Its engine integration lives in `Ship/`, `Targeting/` and `UI/` (no new subsystem) | `SOLSurfaceLock` |
| `Menu/` | Menus (issue #8, Parts 8a-8c): the pure menu state machine, info-text, settings and key-label logic (8a); the menu subsystem that owns the flow (main menu, Play, Esc pause, Quit to menu), the main-menu camera pawn, the C++-built UMG main and pause menus with their Controls/Info/About/Settings pages, all menu strings in one file, and the `-SOLMenuSmoke` script | `SOLMenuState`, `SOLInfoText`, `SOLSettingsModel`, `SOLBindingLabel`, `SOLMenuSubsystem`, `SOLMenuCameraPawn`, `SOLMainMenuWidget`, `SOLPauseMenuWidget`, `SOLMenuButton`, `SOLMenuContent`, `SOLMenuText`, `SOLMenuSmoke` |
| `Game/` | Game mode (default pawn and HUD, spawns the body visuals, the star field and the asteroid-belt visuals, smoke screenshot), shared Enhanced Input helpers, debug spectator | `SOLGameMode`, `SOLInputHelpers`, `SOLSpectatorPawn` |
| root | Module boilerplate, log category, central constants | `SOLTest.h/.cpp`, `SOLConstants.h`, `SOLTest.Build.cs` |

---

## 3. Frame update order

The whole universe is advanced by **one tickable world subsystem, `USOLAnchorSubsystem::Tick`**, which ticks after `TG_PostPhysics`. The clock, registry and targeting subsystems do not tick themselves. The ship's Mass processor is **not** registered with a Mass processing phase. Instead, `USOLShipSubsystem` runs it explicitly from the anchor's `OnBodiesUpdated` event, so the ship always steps against this frame's bodies.

```mermaid
sequenceDiagram
    autonumber
    participant Pawn as ASOLShipPawn (TG_PrePhysics)
    participant Anchor as USOLAnchorSubsystem::Tick
    participant Clock as USOLSimClockSubsystem
    participant Reg as USOLBodyRegistrySubsystem
    participant Ship as USOLShipSubsystem
    participant Tgt as USOLTargetingSubsystem
    participant Proc as USOLShipFlightProcessor
    participant Vis as ASOLBodyVisuals / pawn
    participant HUD as ASOLFlightHud::DrawHUD

    Pawn->>Ship: SetControl(composed input)
    Note over Anchor: dt = min(realDelta, MAX_FRAME_DELTA_S)
    Anchor->>Clock: AdvanceFrame(dt) (sim time += dt x warp)
    Anchor->>Reg: UpdateFromClock() (Kepler positions, then refresh Unreal-handed FSOLBodyFrameCache)
    Anchor->>Ship: OnBodiesUpdated(dt)
    Ship->>Tgt: RefreshCandidates(), GetReferenceVelocityMps()
    Ship->>Ship: ComputeFrameInputs (warp carry, previous body positions)
    Ship->>Proc: Executor::Run (substeps: gravity, SOLFlight::Step, swept collision, surface-lock alignment)
    Ship->>Ship: UpdateSurfaceLock (state machine on this frame's ship and bodies; engaging calls Tgt LockToBodyIndex)
    Ship->>Anchor: SyncObserverPositionM(ship position)
    Ship->>Ship: OnShipsStepped broadcast (end of StepShips, after the observer sync)
    Note over Ship: USOLRingSubsystem::UpdateRings runs here (ring-rock pool streaming; reads this frame's real ship position)
    Note over Anchor: OnBodiesUpdated also runs USOLMinorBodySubsystem (Executor::Run of USOLMinorBodyOrbitProcessor, raw ecliptic positions) - this also places USOLRingSubsystem's ring-rock entities, since they share the same fragments and Mass matches by composition, not by creator
    Anchor->>Anchor: anchor selector update (25% hysteresis), RebaseRenderOrigin
    Anchor->>Vis: OnUniverseUpdated (place bodies, pawn follows ship, ASOLAsteroidBeltVisuals bulk-updates its ISMs)
    Note over Anchor: OnUniverseUpdated also runs USOLCombatSubsystem::Update (targets, drop, bolts, grids, parallel sweep, hits, guns), which then broadcasts OnCombatUpdated for the combat visuals and audio
    Note over HUD: after the world tick, at draw time
    HUD->>HUD: read subsystems, draw reticle, markers, bracket, radar, text
```

**Why the order matters.** Each stage consumes the output of the previous one *from the same frame*. The ship needs this frame's bodies for gravity and collision, and this frame's reference velocity (so targeting is refreshed first). The anchor and rebase need this frame's ship position. The visuals and HUD need the final render origin. If the processor were phase-registered, the ship would step against last frame's bodies and the anchor would lag it by one frame.

- **Time-warp frame carry:** ship flight always runs in real time, so each frame the ship also inherits the part of its reference frame's motion that warp adds beyond real time (`SOLFlight::FrameCarryDisplacement`, for both position and velocity). This keeps the ship co-moving with Earth at 1 d/s.
- **Surface-lock timing:** the lock state is advanced after the flight run, when the ship and the bodies are both at this frame's positions (before the run the ship is one body-step behind, which misreads altitude by up to the body's speed times the frame time). Its decision (the body to align to, the frame lock) takes effect from the next step; the alignment itself runs inside the processor per substep with the substep's real dt.
- **Combat timing:** `USOLCombatSubsystem` updates on `OnUniverseUpdated`, i.e. after every `OnBodiesUpdated` listener has finished (ship step, ring streaming, minor-body orbits, whatever their bind order), so this frame's ship, bodies and rocks are final; it only stores the frame's clamped real delta from `OnBodiesUpdated`. Anything that draws or plays combat state binds to its own `OnCombatUpdated`, never beside it on `OnUniverseUpdated`. Bolts fired by the gun in an update start sweeping in the next one; the pawn's trigger and aim, set in its pre-physics tick, are read by the same frame's update.
- **Hitch budget:** the real delta is clamped once to `SOL::MAX_FRAME_DELTA_S` (0.5 s) and fed to both the clock and the ship. It is split into at most `SHIP_MAX_SUBSTEPS` (16) substeps of 1/30 s, which cover that budget exactly (a `static_assert` enforces this), so a hitch slows the universe uniformly and no ship time is dropped.

To change this order, update this section, SDD 2 section 3, and the tick comments in `SOLAnchorSubsystem.cpp` together.

---

## 4. Coordinate systems and conversions

| Frame | Handedness and units | Used by |
|---|---|---|
| **Universe (ecliptic)** | Right-handed J2000 ecliptic (+X vernal equinox, +Z north ecliptic pole), meters, double, Sun at origin | Body registry, anchor, observer, map picking |
| **Unreal-handed universe** | Ecliptic with Y negated (`EclipticToUnreal`), meters, double | Ship state, flight math, targeting candidates, body frame cache. `FQuat4d` works as in Unreal (X forward, Y right, Z up) |
| **Unreal render** | Left-handed, centimeters, relative to `FSOLRenderOrigin::OriginM` (within 10 km of the observer) | Actor locations, HUD projection |

```mermaid
flowchart LR
    U["Universe position<br/>FVector3d m, ecliptic"] -->|"minus observer"| R["Relative to observer (m)"]
    R -->|"SOLRender::ComputePlacement<br/>1:1 within 1e6 km, else log depth,<br/>angular size kept"| P["Placement (cm, ecliptic)"]
    P -->|"EclipticToUnreal (X, -Y, Z)<br/>+ observer render location"| W["Unreal location (cm)"]
    U -->|"EclipticToUnreal"| H["Unreal-handed universe (m)<br/>ship and flight math"]
```

"Observer" in the pipeline above is really the **render viewpoint**: the observer (ship) normally, or the jump-map camera while the map is open (`USOLAnchorSubsystem::SetViewpointOverrideM`). Anchor selection always uses the observer. The render origin snaps to the viewpoint when the anchor body changes, when the override is cleared, or when the viewpoint drifts `SOL::RENDER_REBASE_DISTANCE_M` (10 km) from it. Unreal's own `SetNewWorldOrigin` is deliberately not used: its int32 cm origin is too small for AU-scale distances. Beyond `SOL::DEFAULT_MAX_RENDER_DISTANCE_CM`, depth is compressed as `d' = Max * (1 + 0.1 * ln(d / Max))` and the radius is scaled by `d'/d`, which keeps depth order and angular size (SDD 2 Amendment 1). Positions stay `FVector3d` until the final conversion to `FVector`.

---

## 5. Key types per module

Each type is marked **pure** (plain C++, unit-tested without a world) or **engine** (UObject, Actor, subsystem or Mass type, covered by smoke checks).

**Universe** (`Source/SOLTest/Universe/`)
- `FSOLSimClock` (pure, `SOLSimClock.h`): Julian date and the warp ladder. `USOLSimClockSubsystem` (engine) wraps it.
- `FSOLKeplerElements` / `FSOLSecularElements` / `SOLKepler` (pure, `SOLKepler.h`): JPL secular elements and the Kepler solver that turns them into a state.
- `FSOLBodyRegistry` (pure, `SOLBodyRegistry.h`): structure-of-arrays bodies (name, radius, GM, **parent index**, elements, Sun-frame position and velocity, and — issue #12 — sidereal rotation period/axial tilt/W0 with a computed orientation via `GetOrientation`). `SOLBodyRotation` (pure, `SOLBodyRotation.h`): the tilt-plus-spin quaternion math `Update()` calls per body. `USOLBodyRegistrySubsystem` (engine) owns the registry plus the Unreal-handed `FSOLBodyFrameCache` (positions only as of #12; visuals convert orientation separately via `SOLRender::EclipticToUnreal(FQuat4d)`).
- `FSOLAnchorSelector` (pure, `SOLAnchor.h`): picks the nearest body with 25% hysteresis.
- `FSOLRenderOrigin` / `SOLRender::ComputePlacement` (pure, `SOLRenderOrigin.h`, `SOLRenderPlacement.h`): rebasing and far-placement math.
- `USOLAnchorSubsystem` (engine, `SOLAnchorSubsystem.h`): the frame driver. It holds the observer position and the render origin, and exposes `OnBodiesUpdated`, `OnUniverseUpdated` and `OnRenderOriginShifted`.

**Flight** (`Source/SOLTest/Flight/`)
- `FSOLShipState`, `FSOLShipControl`, `FSOLFlightParams` (pure, `SOLFlight.h`): the ship's state, input and tuning.
- `SOLFlight` (pure): `Step` (assist or Newtonian, quaternion rotation), `GravityAcceleration`, swept sphere collision, `FrameCarryDisplacement`, `BodyPositionAtSubstep`, `MakeCircularOrbitState`.
- `FSOLTargetInfo` / `SOLTargeting` (pure, `SOLTargeting.h`): `PickUnderReticle`, `Cycle`, `ResolveReferenceVelocity`.

**Ship** (`Source/SOLTest/Ship/`)
- `FSOLShipStateFragment`, `FSOLShipControlFragment`, `FSOLShipParamsFragment` (const shared), `FSOLPlayerShipTag` (engine, `SOLShipFragments.h`): the Mass data for the ship.
- `USOLShipFlightProcessor` (engine, `SOLShipFlightProcessor.h`): the substepped gravity, flight, collision and surface-lock alignment loop (`FSOLShipControlFragment::AlignBodyIndex` names the body per ship), with a parallel chunk loop that allocates nothing per run.
- `USOLShipSubsystem` (engine, `SOLShipSubsystem.h`): spawns the entity, runs the processor on `OnBodiesUpdated`, computes the warp carry, owns and steps the player's `FSOLSurfaceLockState` (L presses arrive via `RequestSurfaceLockToggle`; `ClearSurfaceLock` for a jump), syncs the observer, fires `OnShipsStepped` after each step, and is the Pawn's API (`SetControl`, `GetState`, `SetState` and so on).
- `ASOLShipPawn` (engine, `SOLShipPawn.h`): Enhanced Input (created in C++), virtual joystick, chase camera, placeholder mesh. It carries no universe coordinates.
- `FSOLShipSmokeFlight` / `FSOLShipSmokeInput` / `FSOLSurfaceLockSmoke` (engine test scripts): the `-SOLSmokeFlight`, `-SOLSmokeInput` and `-SOLSmokeLevel` runs.

**Targeting** (`Source/SOLTest/Targeting/`)
- `ISOLTargetable` (engine interface, `SOLTargetable.h`): for non-body targets. It returns an `FSOLTargetInfo` each frame.
- `USOLTargetingSubsystem` (engine, `SOLTargetingSubsystem.h`): the candidate list (registry bodies at their body index, then registered targetables), the selection, the M lock (also set by surface-lock through `LockToBodyIndex`) and the reference velocity.

**UI** (`Source/SOLTest/UI/`)
- `ASOLFlightHud` (engine, `SOLFlightHud.h`): everything drawn in `DrawHUD` with its own pinhole projection. It does not tick.
- `USOLSpeedPanelWidget` (engine, `SOLSpeedPanelWidget.h`): the F3 UMG panel, built in C++.
- `SOLRadar` / `FSOLRadarContact` (pure, `SOLRadarLayout.h`): radar projection, floor and zoom.
- `SOLHudFormat`, `SOLSpeedStepper`, `SOLOrbitLines` (pure): unit and distance formatting, odometer stepping, ellipse sampling.
- `FSOLHudSmoke` (engine test script, `SOLHudSmoke.h`): the `-SOLSmokeHud` run.

**Visuals** (`Source/SOLTest/Visuals/`)
- `ASOLBodyVisuals` (engine, `SOLBodyVisuals.h`): one sphere mesh and material instance per body, re-placed on `OnUniverseUpdated`, with `SunDirection` set per body and — issue #12 — its world rotation set each update from `SOLRender::EclipticToUnreal(registry.GetOrientation(index))`, so every body spins visibly on its real axial tilt. `SOLBodyAppearance::FindBaseColor` exposes each body's base color so the map icons match the meshes.

**StarField** (`Source/SOLTest/StarField/`, Part 4)
- `FSOLBrightStar` / `USOLStarFieldData` (engine data asset, `SOLStarFieldData.h`): the bright-star (V < 8) list, brightest first (ecliptic unit direction, V, flux relative to mag 8, luminance-1 linear color), plus the cube texel solid angle that converts sprite flux to the faint-star cubemap's radiance units and the CC BY-SA attribution. No logic; written only by `Tools/StarField/import_star_field.py` into `/Game/SOL/StarField/DA_SOLStarField` next to the `T_SOLStarFieldCube` TextureCube (paths in `SOL::Paths`).
- `ASOLStarField` (engine actor, `SOLStarField.h`, 4c): spawned by `ASOLGameMode` next to `ASOLBodyVisuals`. It doesn't tick, binds no events, and never moves after `BeginPlay`. `BeginPlay` fills one `UInstancedStaticMeshComponent` once: 45,653 quads at `STAR_FIELD_SPRITE_RADIUS_CM` (2e12 cm), each turned to face the center, with custom data = flux × color and material `M_SOLStarSprite`. It also sets up a two-sided sphere of radius `STAR_FIELD_SKY_RADIUS_CM` (4e12 cm, inside `HALF_WORLD_MAX`) that samples the cube along the view direction (`M_SOLStarSky`). Both components use absolute transforms pinned at the Unreal origin. Moving the 45k-instance ISM cost ~4 ms of game thread per frame, and the sky gains nothing from moving. The render-origin rebase keeps the camera within ~10 km of the origin, which is a ≤5e-7 rad error. Keep `RENDER_REBASE_DISTANCE_M` small (SDD 5 §3.2). Nothing rotates: the directions are already ecliptic, converted once with `SOLRender::EclipticToUnreal`. The sprites use a very low translucency sort priority, so later world-space translucency draws over them.

**Map** (`Source/SOLTest/Map/`, Part 2)
- `FSOLMapPickState` / `SOLMapPicking` (pure, `SOLMapPicking.h`): ray-plane intersection, planar decomposition, destination composition, body picking under a ray.
- `FSOLOrbitCameraState` / `SOLMapCamera` (pure, `SOLMapCamera.h`): orbit-camera position, look-at orientation, orbit, ground-plane pan and log zoom.
- `USOLMapModeSubsystem` (engine, `SOLMapModeSubsystem.h`): the open map's orbit state, its `ACameraActor` view target, and the anchor's render-viewpoint override (set on `OnBodiesUpdated` before the rebase; the camera is placed on `OnUniverseUpdated`). The ship pawn owns J/Esc, the `IMC_Map` mapping and the cursor, and forwards drags and wheel notches. Destination picking lives here too (pick state recomputed on `OnUniverseUpdated`).
- `USOLJumpSubsystem` (engine, `SOLJumpSubsystem.h`): the jump's warp sequence. Enter makes the pawn hand it the pick and close the map; its real-time clock advances on `USOLShipSubsystem::OnShipsStepped` (end of the ship step, before the anchor update), and at 3 s it teleports the ship through `SetState` (which re-anchors and force-snaps the render origin). The pawn drives the camera FOV and post-process from the clock; the HUD draws the streaks instead of the flight HUD.
- `FSOLBodyLodParams` / `SOLMapBodyLod` (pure, `SOLMapBodyLod.h`): apparent diameter in pixels and the mesh/icon cross-fade alpha.
- `SOLMapPickRadius` (pure, `SOLMapPickRadius.h`): `ComputeMapPickRadiusM`, a body's pick radius that never drops below a minimum on-screen size.
- `FSOLMapBodyOverlay` (engine, `SOLMapBodyOverlay.h`): owned and drawn by `ASOLFlightHud` while the map is open. It evaluates each body's LOD from the map camera and draws its icon disc (one translucent Canvas triangle batch, in the body's material base color from `SOLBodyAppearance::FindBaseColor` in `Visuals/`) and a decluttered name label, and keeps each body's pick radius for the frame.
- `FSOLMapSmoke` (engine test script, `SOLMapSmoke.h`): the `-SOLSmokeMap` run.
- `FSOLMapPickSmoke` and `FSOLJumpSmoke` (engine test scripts): the `-SOLSmokeMapPick` and `-SOLSmokeJump` runs.

**Level** (`Source/SOLTest/Level/`, Part 3)
- `FSOLSurfaceLockParams` / `FSOLSurfaceLockState` / `SOLSurfaceLock` (pure, `SOLSurfaceLock.h`): `UpdateSurfaceLockState` (the auto/manual engage-warn-release state machine, with a suppression latch so a manual release can't be immediately overridden by auto-engage), `TargetUpDir` and `ApplyAlignmentCorrection` (a minimal shortest-arc rotation of the ship's whole orientation toward "up = away from the locked body," not a roll-only correction, so there is no attitude-dependent singularity). Called by `USOLShipSubsystem` (state machine, once per step), `USOLShipFlightProcessor` (alignment, per substep) and `ASOLFlightHud` (`ComputeManualRangeM` for the hint line).

**MinorBodies** (`Source/SOLTest/MinorBodies/`, Part 5)
- `SOLAsteroidBelt` (pure, `SOLAsteroidBelt.h`, 5b): the 500 real asteroids plus the deterministic family-cluster fill (`GenerateBelt(seed)`).
- `SOLMinorBodyOrbit::ComputePositionM` (pure, `SOLMinorBodyOrbit.h`, 5c): parent position + `SOLKepler::ElementsToState(elements.AtCenturies(t))`, in raw ecliptic meters.
- `FSOLMinorBodyOrbitFragment` / `FSOLMinorBodyRenderFragment` / `FSOLMinorBodyStateFragment` / `FSOLMinorBodyAppearanceFragment` (Mass, `SOLMinorBodyFragments.h`): immutable elements + parent index (processor-only); radius + per-variant `InstanceIndex` plus a `bActive` flag (visuals-only, split out from the orbit fragment in 5c's review so neither consumer's hot loop drags the other's unused bytes through cache; `bActive` added in 5e-iii, always true for belt asteroids, explicitly set false at spawn and again by `USOLRingSubsystem::AssignGroup` for a ring-pool group with no assigned cell, an all-gap cell, or a gap-reduced cell's excess entities, so `ASOLRingVisuals` skips it instead of rendering a stale or duplicate instance); the per-frame position (raw ecliptic universe meters, NOT the Unreal-handed `FSOLBodyFrameCache` frame); the const-shared rendering variant (`SOLMinorBodyVariant`: 0 real, 1 family fill). `FSOLBeltRockTag`/`FSOLRingRockTag` (Mass tags, same file, 5e-iii): let each of `ASOLAsteroidBeltVisuals`'s and `ASOLRingVisuals`'s per-ring queries match only their own population at the archetype level.
- `USOLMinorBodyOrbitProcessor` (Mass, not phase-registered): one Kepler solve per entity per run, parallel chunks, parent positions from `FSOLBodyRegistry::GetPositionsM`.
- `USOLMinorBodySubsystem` (world subsystem): at `OnWorldBeginPlay` generates the belt with `SOL::ASTEROID_BELT_SEED` and batch-creates one entity batch per variant; runs the processor from `OnBodiesUpdated`; destroys its entities in `PreDeinitialize` (in `Deinitialize` the Mass entity manager may already be gone).
- `ASOLAsteroidBeltVisuals` (engine actor, spawned by `ASOLGameMode`): one `UInstancedStaticMeshComponent` per variant (generic body sphere and material, placeholder grays), pinned at the Unreal origin. On `OnUniverseUpdated` it runs its own read-only parallel query, converts each position with `USOLAnchorSubsystem::ComputeRenderPlacement`, and pushes each variant with one `BatchUpdateInstancesTransforms`.
- `SOLPlanetRing` (pure, `SOLPlanetRing.h`, 5d): `RealRings()` (the four gas giants' real ring radius ranges), `KnownResonanceGapBandsM`/`MoonShepherdGapBandsM`/`AllGapBandsM` (the hybrid gap mechanism — fixed cited data for resonance gaps with no corresponding moon, e.g. Saturn's Cassini Division via its 2:1 Mimas resonance, plus a generic per-moon-radius band exclusion for real shepherd-moon gaps, e.g. Pan/Daphnis's Encke/Keeler gaps); decoupled from the registry (callers pass each moon's semi-major axis, computed once, not looked up live). Two tiny Saturn shepherd moons, Pan and Daphnis, were added to `FSOLBodyRegistry` in 5d specifically so this mechanism has real moon data to exclude bands around (registry total is now 36 bodies).
- `SOLRingPatch` (pure, `SOLRingPatch.h`, 5e-i/ii): the near-field ring-rock patch system. Cells are indexed in a frame **co-rotating** with each radial band's own local circular mean motion (not the planet's non-rotating equatorial frame — a ring rock orbits too fast, tens of km/s, for a fixed-frame cell grid to stay meaningful for more than a fraction of a second). `ComputeActiveCell`/`GenerateCellRocks` both take `planetGM`/`secondsSinceJ2000` to compute this frame; `GenerateCellRocks`'s rocks carry J2000-epoch `FSOLSecularElements` (their "right now" mean longitude is rolled back to T=0 before returning, since the elements type is always defined at that epoch). `ActiveCellWindow` returns a window of cells around a center cell, correctly re-deriving each neighboring band's angular index through world angle (not a naive index offset or same-band-circle-fraction — two review-caught co-rotating-frame bugs, SDD 6 Amendments 7 and 9). `ReassignPoolSlots` is pure index bookkeeping deciding which pool "slot" (a whole group of rocks, not one) represents which cell next, preferring to leave an already-correct slot alone.
- `USOLRingSubsystem` (world subsystem, 5e-ii/iii): owns a fixed-size Mass entity pool per ringed planet (grouped into `SOLRingPatch::ROCKS_PER_CELL`-sized chunks, one per cell the pool can represent at once; `SOLRingPatch::PoolEntityCountPerRing()` is the single source of truth for the size, shared with `ASOLRingVisuals` so the pool and its ISM instance count can never disagree), reserved once at `OnWorldBeginPlay`. Every `USOLShipSubsystem::OnShipsStepped` (not `OnBodiesUpdated` — this needs the ship's position already current this frame) it checks the ship against each ring's volume and reassigns groups whose cell the desired window no longer covers, rewriting their `FSOLMinorBodyOrbitFragment`/`FSOLMinorBodyRenderFragment` data in place (never creating/destroying entities at runtime); a group with no cell, or an all-gap cell, instead gets `FSOLMinorBodyRenderFragment::bActive` cleared. Shares the belt's three core fragments so `USOLMinorBodyOrbitProcessor`'s existing per-frame run also places ring entities for free (same fragment composition, and Mass matches by composition, not creator) — `USOLRingSubsystem` deliberately does **not** run its own processor instance, which would reprocess the same global entity set a second time. Ring entities carry `FSOLRingRockTag` (no `FSOLMinorBodyAppearanceFragment`) so `ASOLAsteroidBeltVisuals`'s belt-tagged query and `ASOLRingVisuals`'s ring-tagged query each match only their own population at the archetype level, not per-entity. `SOLRingPatch::ActivationMarginM()`/`HalfThicknessM()` are likewise shared with `ASOLRingVisuals` (the activation margin doubles as the near-field cross-fade's transition band, so the visual fade finishes exactly when the pool itself would stop considering the player inside the ring).
- `SOLRingDense` (pure, `SOLRingDense.h`, issue #6 5e-v): the dense rock layer's per-frame maths: `WindowPhaseCm` (tile modulo), `AdvanceSpinAngle` (Keplerian co-rotation integration), `ComputeDenseAlpha` (smoothstep fade by distance from the ring volume), `RingFramePositionCm` (camera position in the ring's co-rotating frame), `ShouldShowDenseTier`, `DenseFillFraction`.
- `ASOLRingVisuals` (engine actor, one per ringed planet, spawned deferred by `ASOLGameMode` with `PlanetName` set before `BeginPlay`, 5e-iii): near field is a `UInstancedStaticMeshComponent` over `USOLRingSubsystem`'s pool (same bulk-transform pattern as `ASOLAsteroidBeltVisuals`, filtered per-entity to its own planet since the shared tag matches all 4 rings, scaled to zero for an inactive entity or when the cross-fade alpha is 0 rather than hidden a different way); far field is one `UNiagaraComponent` on the shared, parameterized `/Game/SOL/Rings/NS_SOLRingFar` system (SDD 6 Amendments 11 and 15: since Amendment 15 a single analytic-profile disc mesh whose material computes the real radial ring profile per pixel, not sprites), given this ring's radii/colour/gap bands, profile index, planet radius and (every frame) Sun direction as Niagara User Parameters, and always rendering. A third layer, the dense rock layer (SDD 6 Amendments 14-15), is a second `UNiagaraComponent` on `/Game/SOL/Rings/NS_SOLRingDense`: ~1M GPU-simulated rock meshes in a wrapped window around the camera, activated once in `BeginPlay`, then shown/hidden and paused/resumed (never deactivated, which would respawn every rock) by `SOLRingDense::ComputeDenseAlpha` / `ShouldShowDenseTier` (near the ring and at low time warp only); `UpdateDenseLayer` integrates its co-rotation angle (`SOLRingDense::AdvanceSpinAngle`), places it at the viewpoint, and feeds the window phase/centre parameters. The far-field disc's material fades out near the camera so the rocks show through. `SOLRingPatch::ComputeNearFieldAlpha` (from the ring's own `DistanceOutsideRingVolumeM` helper) drives the near-field scale every `OnUniverseUpdated`.

**Combat** (`Source/SOLTest/Combat/`, Part 6)
- `SOLBoltMath`, `SOLDamageMath`, `SOLTargetDrop` (pure, 7a): muzzle velocity, crosshair convergence, bolt integration and lifetime, swept sphere (stationary and moving) hit times; shield-then-health damage and shield regeneration; drop position, drop cooldown and oldest-first eviction.
- `SOLCombatGrid::FGrid` (pure, 7a; internals reworked in 7b): uniform spatial hash over double positions. Cells are linked lists threaded through one flat entry array with an int-valued cell map, so `Clear` keeps every allocation and a rebuild allocates nothing however far objects moved; queries de-duplicate in place and are safe to run from several threads at once.
- `SOLFireCadence` (pure, 7b): the gun's shot timeline (shots per step, cooldown, each shot's age within the step), frame-rate independent.
- `FSOLCombatEvent` / `ESOLCombatEventType` (`SOLCombatTypes.h`): the one-shot events (fired, target hit, shield broken, destroyed, absorbed by a rock, dropped, evicted) with a universe position and velocity, plus the frame body the event is held relative to and its offset from it, so lingering effects and sounds recompute their position from the body each frame (correct under time warp).
- `SOLCombatRules` (pure, 7f): the subsystem's decision rules, unit-tested in `SOLTest.CombatRules.*`: sweep start and teleport rule, minor-body previous-position validity (including the ring-rock `Generation` check), bolt range, target-hit resolution with the double-kill guard, event trim and cap, the end-of-frame hit point, an event's current position, and the drawn bolt count.
- `USOLCombatSubsystem` (engine world subsystem, 7b/7e): owns the bolt pool (packed structure-of-arrays, reserved to `COMBAT_MAX_BOLTS`) and the target pool (fixed `COMBAT_MAX_TARGETS` slots), deliberately not Mass entities (see the class comment: cross-population writes, spawn/destroy churn, and a packed render array). Each bolt and target is stored relative to the player's reference body at the time it was fired or dropped, so both keep their frame's motion, including the time-warp carry the ship gets. Each update sweeps every bolt segment in a ship-centred frame against the targets and the minor bodies (belt asteroids and active ring-pool rocks, read through its own Mass query into a snapshot that supplies last frame's positions) as they move, through two `FGrid`s filled only with what the frame's bolt bounding box can reach; the sweep is a `ParallelForWithExistingTaskContext` over bolts with per-task scratch; hits are applied serially in bolt order (a bolt whose target an earlier bolt destroyed is re-swept on the game thread). New shots are swept from their muzzles in the frame they are fired; bolts more than `COMBAT_BOLT_MAX_RANGE_M` (20 km) from the ship expire; a ring rock whose render fragment `Generation` changed (reassigned by `USOLRingSubsystem::AssignGroup`) is treated as teleported. Read API for 7c/7d: packed bolt positions/velocities/owners, per-slot target flags/positions/shield and health fractions/hit flash, the queued events, and `OnCombatUpdated`. Input API: `SetPlayerTriggerHeld`, `SetPlayerAim`, `RequestPlayerTargetDrop`, plus `FireBolt` and `DropTarget` for scripts.
- `ASOLCombatVisuals` (engine actor, `SOLCombatVisuals.h`, 7c): spawned by `ASOLGameMode`, never ticks, redraws on `USOLCombatSubsystem::OnCombatUpdated`. Bolts: one `NS_SOLBolts` component (CPU sim, one particle per drawn bolt, spawned once at `COMBAT_BOLT_RENDER_CAP` = 2,048; paused and hidden while no bolt is live; bolts past the cap fly and hit undrawn) fed each frame with the packed bolt positions (render cm) and ship-relative velocities through two Niagara float3 arrays plus `User.BoltCount`; particles past the count are hidden through the sprite renderer's visibility tag, and streaks widen with camera distance (`DistanceToCamera`) so they stay a few pixels wide. Targets: one `UInstancedStaticMeshComponent` (instance i = target slot i, instances added only as the highest used slot grows, hidden while no target is active; `M_SOLTarget`) with three custom floats (shield, health, hit flash) written only when they change and transforms pushed with one `BatchUpdateInstancesTransforms` without a render-state rebuild. Effects: fixed pools of pre-registered Niagara components (impact 32, shield break 8, spark 32, explosion 8) started round-robin from the frame's events, re-placed every update from the event's frame body (aged with the combat update's clamped delta) and retired after their duration. Niagara gotcha found here: a `User.*` parameter read only from an HLSL expression input (or from a Set Parameters input linked straight into a `Particles.*` attribute) reads as zero at runtime; each system links its user parameters once into `Emitter.*` attributes in the emitter update script (or links them somewhere in the system) and the particle expressions read those.
- `SOLCombatAudioRules` (pure, 7d): which `ESOLCombatSound` an event plays (shot, shield hit, hull hit, shield break, explosion, spark, target drop; an eviction is silent) and the per-kind minimum-interval throttle `MayPlay`.
- `ASOLCombatAudio` (engine actor, `SOLCombatAudio.h`, 7d): spawned by `ASOLGameMode` next to the visuals, never ticks, plays on `USOLCombatSubsystem::OnCombatUpdated`. One fixed pool of pre-registered `UAudioComponent`s per sound kind (sizes, volumes, minimum intervals and asset paths in the `SOL::COMBAT_SOUNDS` table; 21 voices), each set once to its placeholder `USoundWave` under `/Game/SOL/Combat/Audio/` and one of two transient `USoundAttenuation`s (normal: 300 m inner, 2 km falloff; long range for explosions: 1.2 km / 8 km). A new sound reuses the oldest voice of its kind (stop-oldest concurrency) unless the kind's minimum interval drops it. Voices are 3D at the event's position (render-origin snapshot) and are re-placed every update from the event's frame body (the player's own shots keep their offset from the ship) until the sound ends. Logs one played/dropped summary per kind at end of play.

**Menu** (`Source/SOLTest/Menu/`, issue #8)
- `SOLMenuState` / `SOLInfoText` / `SOLSettingsModel` / `SOLBindingLabel` (pure, 8a): menu state machine (MainMenu, Playing, Paused, map-open flag, input-suspension, cursor and spawn rules), Info-page formatting, settings clamping and dirty tracking, key display names.
- `USOLMenuSubsystem` (engine world subsystem, 8b): owns the `FMenuState`. The game starts in the main menu unless a `-SOL*` verification switch is present (`ShouldStartInMenu`). Requests (Play, Esc, Resume, Quit to menu) are queued and applied next frame: Play calls `USOLShipSubsystem::SpawnPlayerShip` for the picked start body and swaps the controller's pawn to the ship pawn; Quit to menu swaps back to `ASOLMenuCameraPawn`, then `DespawnPlayerShip` and clears target and frame lock. While a menu shows it sets UI-only input with a visible cursor and focuses the widget; in flight the pawn owns input and cursor. `OnStateChanged` tells the ship pawn to suspend or restore its control (as the map does); the simulation is never paused. The pawn reports map open/close (`NotifyMapOpen`), so Esc then belongs to the map.
- `ASOLMenuCameraPawn` (engine, 8b): the main-menu pawn and observer. On `OnBodiesUpdated` it places the observer 4.5 radii from the start body on its lit side (slow real-time sway; a new start body teleports it), on `OnUniverseUpdated` it moves to the render location and looks at the body. No tick, no input.
- `USOLMainMenuWidget` / `USOLPauseMenuWidget` / `USOLMenuButton` (engine UMG, built in C++, 8c): main menu column (Play, Start location picker over every registry body, Controls, Settings placeholder, About, Quit) with a page panel; pause menu with Resume/Controls/Info/Settings/About/Quit tabs. `SOLMenuContent` builds the shared pages once (Info values generated from `SOLConstants.h` and `FSOLFlightParams` through `SOLInfoText`); `SOLMenuText` holds every fixed string and the Controls table. Layout and colours are `SOL::MenuStyle` in `SOLConstants.h`.
- `FSOLMenuSmoke` (engine test script): the `-SOLMenuSmoke` run.

**Game** (`Source/SOLTest/Game/`)
- `ASOLGameMode` (engine, `SOLGameMode.h`): default pawn (the menu camera while the main menu shows, else the ship, or spectator with `-SOLSpectator`) and HUD, spawns `ASOLBodyVisuals`, `ASOLStarField`, `ASOLAsteroidBeltVisuals` and one `ASOLRingVisuals` per `SOLPlanetRing::RealRings()` entry (deferred spawn, `PlanetName` set before `FinishSpawning`), spawns `ASOLCombatVisuals` and `ASOLCombatAudio`, runs `-SOLSmokeShot` and `-SOLCombatDemo`, and provides `IsSOLGameWorld`, which gates all the SOL subsystems.
- `SOLInputHelpers` (engine, `SOLInputHelpers.h`): shared helpers that create Enhanced Input actions and mappings in C++.
- `ASOLSpectatorPawn` (engine, `SOLSpectatorPawn.h`): a debug free-fly camera that rides the ship with `-SOLSpectator`.

---

## 6. Data flow narratives

### 6.1 Player presses W

```mermaid
sequenceDiagram
    participant EI as Enhanced Input
    participant Pawn as ASOLShipPawn
    participant Ship as USOLShipSubsystem
    participant Proc as Flight processor
    participant HUD as ASOLFlightHud
    EI->>Pawn: IA thrust callback -> Handle* (held state)
    Pawn->>Ship: Tick (TG_PrePhysics): compose FSOLShipControl, SetControl
    Ship->>Proc: OnBodiesUpdated: reference velocity + warp carry, Executor::Run
    Proc->>Proc: per substep SOLFlight::Step -> FSOLShipStateFragment
    Ship->>HUD: (read at draw) GetState, speed relative to frame
```

Later in the same frame, the observer sync and `OnUniverseUpdated` move the pawn to the ship's render location. The HUD then shows the speed relative to the active frame, with the Sun-frame speed beneath it.

### 6.2 M pressed with Mars selected

1. The pawn's M callback calls `USOLTargetingSubsystem::ToggleFrameLock()`, which locks Mars's candidate index (or the anchor if nothing is selected).
2. On the next `OnBodiesUpdated`, `RefreshCandidates()` rebuilds Mars's `FSOLTargetInfo` from the body cache, and `SOLTargeting::ResolveReferenceVelocity` returns Mars's velocity.
3. `USOLShipSubsystem` writes it into `Control.ReferenceVelocityMps` and uses Mars (`GetReferenceIndex`) for the warp carry. Assist now brakes toward Mars's velocity, and the speed cap is relative to Mars.
4. The lock persists through anchor and selection changes until M is pressed again.

### 6.3 Radar draw

1. `DrawHUD` rebuilds its reused `FSOLRadarContact` array from the targeting candidates, transforming each one into ship-local axes (X forward, Y right, Z up) through the ship orientation.
2. The range is `SOLRadar::EffectiveFloorM(bHasSelectedTarget)` in AUTO mode (10,000 km, or 1 km with a target). In MANUAL mode (`-` / `=`) it is the stepped range, reclamped to between the floor and 1 AU.
3. `SOLRadar::ProjectContact` maps each contact to a disc offset and a height stalk (whole-vector scaling onto the range sphere, with a collapse law near the center). The HUD draws the result on a tilted disc with the selected target highlighted.

---

## 7. Testing architecture

The project splits **pure logic** from **engine glue**. The math that decides behavior (orbits, clock, anchor, render placement, flight, targeting, radar, formatting, map picking) lives in plain C++ namespaces and structs and is unit-tested with the UE Automation Framework (`Source/SOLTest/Tests/<Topic>Test.cpp`, `SOLTest.<Topic>.<Case>`). Tests are written test-first by a separate subagent from each SDD's API appendix. The UObject, Actor and Mass glue is listed in each SDD's "Non-unit-tested pieces" section and verified with PIE smoke checks and the scripted command-line runs below, which log a PASS or FAIL per check to `LogSOL` and then quit. For rules and the runner (`Tools\RunTests.bat`), see [`CLAUDE.md` → Testing](../CLAUDE.md#testing).

| Flag | Owner | Exercises |
|---|---|---|
| `-SOLSmokeShot=<s>` | `ASOLGameMode` | Takes a screenshot after N seconds, then quits (visual check of any state) |
| `-SOLCombatDemo` | `ASOLGameMode` | From 8 s: drops a target ahead, holds the trigger until it dies, rests 2 s, repeats; the first cycle takes screenshots while firing, just after the kill and of the debris (pair with a late `-SOLSmokeShot` to quit). It tracks its own target's slot, tells a kill from a loss, and gives up on a target after 20 s of fire |
| `-SOLCombatStress=<bolts>` | `ASOLGameMode` | With `-SOLCombatDemo`: keeps about this many extra bolts alive (fired in a 25° forward cone, topped up one bolt lifetime's share per 0.1 s so the count holds steady), for load measurements |
| `-SOLSmokeFlight` | `FSOLShipSmokeFlight` | Pawn-less scripted flight: hover, assisted cruise, Newtonian thrust and coast, pitch, braking, Earth impact, then 1 d/s warp co-motion, collision and surface rest under warp |
| `-SOLSmokeInput` | `FSOLShipSmokeInput` | Simulated key, mouse and wheel events through the real mapping context and callbacks (thrust, stick, targeting, M lock, warp), with a screenshot |
| `-SOLSmokeHud` | `FSOLHudSmoke` | HUD toggles and radar zoom via game keys, and the F3 panel via Slate key events to the focused widget |
| `-SOLSmokeMap` | `FSOLMapSmoke` | Jump map via injected events: J open (cursor, ship input suspended, map camera, render viewpoint), orbit/zoom/pan drags, J and Esc close, with screenshots; body icon/label state checked at 1e13 m, ~9e10 m, ~8e8 m (Earth cross-fade) and ~5e7 m (mesh only) |
| `-SOLSmokeMapPick` | `FSOLMapPickSmoke` | Destination pick via injected events: body and empty-space references, planar drag/lock, Shift height preview/lock, X clear, OS-cursor sync after a drag, close/reopen clears; screenshots of the disc, guide line and marker |
| `-SOLSmokeJump` | `FSOLJumpSmoke` | Pick on Mars and jump: Enter with no pick ignored, warp start, midpoint FOV/post-process on the warp curve with the HUD hidden, arrival position/velocity/orientation/anchor/origin, restore; mid-warp and arrival screenshots |
| `-SOLSmokeLevel` | `FSOLSurfaceLockSmoke` | Surface-lock via injected L presses and scripted teleports above Earth: L out of range ignored, auto-engage with frame match (replacing an M lock on another candidate) and alignment (time constant checked one tau in), L release that stays released (suppression latch) and stops aligning, latch cleared by a climb, auto warn/release (alignment stops), hint, manual engage/warn/release, a scripted jump that drops an L pressed during the warp and releases the lock on arrival; screenshots of each HUD line |
| `-SOLMenuSmoke=<all\|screens\|flow>` | `FSOLMenuSmoke` | Main menu, start-location picker (Mars), Controls and About pages, Play, thrust, Esc pause (real ship mapping), every pause tab, Esc resume through Slate, J then Esc (closes the map, no pause), Quit to menu and Play twice more; screenshots with the UI as `Saved/Screenshots/WindowsEditor/MenuSmoke_*.png` (none for `flow`) |
| `-SOLMenu`, `-SOLNoMenu` | `USOLMenuSubsystem` | The game starts in the main menu unless another `-SOL*` switch is present; `-SOLMenu` forces the menu anyway, `-SOLNoMenu` skips it |
| `-SOLSpectator` | `ASOLGameMode` / `ASOLSpectatorPawn` | Debug free-fly camera riding the ship instead of the pawn |
| `-SOLStart=<Body>`, `-SOLAltitudeKm=<km>`, `-SOLLookAt=<Body>` | Ship subsystem / spectator | Spawn body and altitude, and the debug camera's look target, for any of the runs above |
| `-SOLSmokeConsole="<cmd>[\|<cmd>...]"` | `ASOLGameMode` | With `-SOLSmokeShot`: runs these console commands (e.g. `ProfileGPU`, `memreport -full`, `stat unit`) just before the screenshot (which then waits 2 s so on-screen stats settle) |
| `-SOLRingStartKm=<km>`, `-SOLRingHeightKm=<km>` | Ship subsystem | With `-SOLStart=<ringed planet>`: spawn inside its ring plane at that radius from its centre (and that height above the plane), for ring close-ups |

All flag strings live in `SOL::CommandLine` in `SOLConstants.h`.

---

## 8. Extension points for future parts

These are grounded in SDD 1, SDD 2 and the roadmap ([`Plans/1-solar-system-architecture-plan.md`](Plans/1-solar-system-architecture-plan.md)). Each part's own SDD settles the details.

- **Part 2 (jump map):** `SOLMapPicking` produces a universe-frame destination; `USOLJumpSubsystem` executes the jump through `USOLShipSubsystem::SetState` (a teleport that refreshes targeting and force-snaps the render origin). Listeners that must run after the ship step bind to `OnShipsStepped`, not beside it on `OnBodiesUpdated`. Native `TMulticastDelegate` broadcasts iterate in reverse of the *current* bind order, but any unbind compacts the list with `RemoveAtSwap`, so that order is not a durable guarantee and nothing should rely on relative bind or fire order for correctness. `OnShipsStepped` fires after the flight processor's step because of *where* it is broadcast (after `UE::Mass::Executor::Run` returns in `StepShips`), not because of delegate ordering. On arrival the jump clears the selected target, the M frame lock and any surface-lock (with its suppression latch) before the teleport, and it aborts the warp if a frame's universe update runs without a ship step.
- **Part 5 (moons, asteroids, rings):** `FSOLBodyDef::ParentIndex` already chains a child's orbit to its parent (parent GM, positions summed), so moons are more registry entries. Asteroids and ring particles are Keplerian too but are planned as Mass entities, not registry bodies. The collision broadphase is an O(bodies) reject per substep, and a spatial structure is deferred until body counts grow. `USOLRingSubsystem::UpdateRings` binds to `OnShipsStepped` specifically to get this frame's real ship position (not `OnBodiesUpdated` directly, which is where `USOLMinorBodySubsystem`'s orbit-processor run also lives) — but per the bind-order caveat above, its ordering RELATIVE to that processor run is still not guaranteed; today's bind order happens to put the ring update first, which is correct, but only by accident of subsystem dependency-initialization order (tracked in `CLAUDE.md`'s tech debt list).
- **Part 6 (weapons, targets):** bolts and targets live in `USOLCombatSubsystem`'s pools (7b) and are rendered from its packed arrays (7c: Niagara bolts, instanced target meshes). Dropped targets are not yet `ISOLTargetable`s; registering them with `USOLTargetingSubsystem` would make them selectable, M-lockable and visible on the radar with no HUD changes (a target dropped while the frame is locked to a non-body targetable already moves with that targetable's velocity).
- **Part 10 (scale demo):** NPC ships reuse the ship archetype (state and control fragments, a const shared params fragment per ship class) and the parallel `USOLShipFlightProcessor`. `FSOLPlayerShipTag` is what separates the player's entity from other ships.
- **Pawn/entity bridge:** any new controllable entity follows the same pattern: a thin Actor writes control state into the entity through a subsystem, and reads its state back once per `OnUniverseUpdated`.

---

## Keeping this document in sync

Like [`GAME_MECHANICS.md`](GAME_MECHANICS.md), this file must stay truthful. Update it in the same change whenever:

- a new feature folder, subsystem, Mass processor, Actor or widget is added, or one is removed (sections 2 and 5);
- a class's responsibility or a module dependency changes (sections 2 and 5);
- the frame update order, the tick driver, or the hitch/substep/warp-carry rules change (section 3, together with SDD 2 section 3);
- a coordinate frame or conversion changes (section 4);
- a `-SOL*` command-line flag is added or changes behavior (section 7).

Keep it high level. Put exact values in `GAME_MECHANICS.md` and rationale in the SDDs, and link to them instead of copying.
