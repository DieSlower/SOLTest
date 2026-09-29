<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 2 — Part 1: Foundations and flight scaffold

Issue `#2`. Refines [`1-solar-system-architecture.md`](1-solar-system-architecture.md); plan of record: [`../Plans/2-foundations-flight-scaffold-plan.md`](../Plans/2-foundations-flight-scaffold-plan.md). Decisions in section 2 were resolved with the user in a `grill-me` session on 2026-09-28; section 3 is design detail chosen by Claude within those decisions.

---

## 1. Problem

Deliver the first playable slice: a ship you can fly in third person through a 1:1-scale solar system (Sun and 8 planets) with real orbits, gravity, origin rebasing, a sim clock with time-warp, a target/reference-frame system, and a full sim-style HUD. It proves the scale strategy and the flight feel that every later part builds on.

## 2. Decisions (user-resolved)

| Area | Decision |
|---|---|
| Steering | Mouse as a **virtual joystick**: offset from screen center sets turn rate (pitch/yaw), with a reticle and joystick indicator and a dead zone. Rotation is **quaternion-based with angular velocity in ship-local space, so there is no gimbal lock**. |
| Keys | W/S forward/back thrust, A/D strafe left/right, Space/Ctrl up/down, Q/E roll, Shift boost, Tab flight-assist toggle, hold Alt to free-look the camera. |
| Speed range | Speed cap on a log scale from 1 m/s up to **0.5c**. No relativistic effects are modeled. |
| Acceleration | Flight-assist ("inertial drive"): ramp toward the target velocity with a fixed time constant of ~1-2 s at any magnitude. Assist-off (Newtonian) mode uses a realistic fixed maximum acceleration in m/s². |
| Camera | Chase camera on a spring arm with slight rotational lag; FOV widens with speed; hold Alt to free-look. |
| Speed setting | Mouse wheel steps the cap on the log scale. **M matches the velocity of the selected target** (a planet now; enemies and other objects later) and makes it the reference frame; pressing M again releases. With no target selected, M matches the anchor (nearest) body. F3 opens a speed panel with a numeric field (m/s, km/s or c) and a list of body orbital speeds. |
| Target selection | T selects the object nearest the forward reticle (within a cone), R/F cycle next/previous of the nearest objects, X clears. Built on a generic "targetable" interface so enemies and dropped targets plug in later. HUD shows a bracket with name, distance and relative speed. |
| Speed reference | Speed and the cap are relative to the reference frame (selected target, otherwise the anchor body); absolute Sun-frame speed is shown smaller beneath. The frame name is shown. |
| Time-warp | `[` / `]` step warp: 1x, 10x, 100x, 1000x, 1 h/s, 1 d/s, 10 d/s, 30 d/s. Backspace resets to 1x. Sim date/time and warp are on the HUD. No auto-clamp near bodies. |
| Spawn | Earth orbit: ~10,000 km above Earth, with Earth's velocity plus circular-orbit velocity. |
| Collision | Analytic sphere collision: velocity into the surface is zeroed and the ship rests/slides at the surface (altitude clamped). No damage yet. The Sun gets a "too hot" HUD warning only. |
| HUD | Full sim-style: reticle and joystick indicator; speed readout (auto units m/s, km/s, c); speed cap and target-speed bar; flight-assist state; reference-frame name; target bracket; nearest body and altitude; sim date and warp; anchor body; prograde/retrograde velocity markers; **body orbit ellipses (toggle O) and ship predicted path under gravity (toggle P), both off by default**; **3D spherical radar with auto log range** (Elite-style, height stalks, selected target highlighted). |
| Planet look | Procedural placeholder materials whose **colors match the real planets so each is recognizable** (Sun emissive with bloom glow; Earth blue/green/white; Mars red-orange; Jupiter and Saturn banded tan; Venus pale yellow; Mercury grey; Uranus and Neptune blue). Real textures come in Part 8 (#9). |
| Reference frame and M | By default the assist frame is the anchor (nearest) body. **M locks the frame to the selected target (or to the current nearest body if none is selected)** and keeps it locked even if the anchor or the selection changes, until M is pressed again. The selected target's own relative speed is always shown on its HUD bracket; the speed readout is relative to the active frame. (Resolved 2026-09-28, 1b grill.) |
| Rotation | Same rate-command rotation in both flight modes (smoothed, quaternion-based); only translation becomes Newtonian when assist is off. |
| Boost and thrust | Newtonian acceleration 50 m/s² forward/back and 30 m/s² strafe/vertical. Boost (Shift) = 3x thrust in Newtonian mode; in assist mode it doubles the target speed (capped at 0.5c) and shortens the ramp to one third. |
| Commits | The user allowed commits for this session: one commit per sub-part (1a, 1b, 1c) after build, tests and adversarial review pass. Commit messages start `Issue #2:`. No push unless the user says so. |

## 3. Design detail (Claude's choices within the decisions above)

- **Sub-parts:** 1a foundations (universe, clock, registry, anchoring, body visuals, test map); 1b ship (Mass entity, Pawn proxy, flight model, gravity, collision, camera, input, targeting); 1c HUD (all HUD elements and the F3 panel). Each is built by its own subagent under the workflow in `CLAUDE.md`.
- **1a modules under `Source/SOLTest/`:** `Universe/` for double-precision position/velocity types, Keplerian elements and solver (position and velocity), the sim-clock subsystem, the data-oriented body registry (structure-of-arrays, parent index for later moons), and the anchor subsystem (nearest body with 25% hysteresis per SDD 1). Bodies far beyond UE precision are drawn with angular-size-preserving projection. Origin rebasing uses UE's built-in world-origin rebasing where practical; if not suitable, camera-relative placement with the reason documented.
- **1a rendering and rebasing (as implemented):** rendering is **observer-relative with our own render origin**, not UE's built-in origin rebasing. `USOLAnchorSubsystem` holds the observer's authoritative universe position (double, meters) and a *render origin* (a universe point that maps to Unreal (0,0,0)). The render origin (`FSOLRenderOrigin`) snaps to the observer whenever the anchor body changes (25% hysteresis) or the observer drifts `SOL::RENDER_REBASE_DISTANCE_M` (10 km) or more from it; the observer actor is moved back to (0,0,0) and `OnRenderOriginShifted` broadcasts the shift for any other listener. Every frame, `ASOLBodyVisuals` re-places each body with `SOLRender::ComputePlacement` relative to the observer (exact 1:1 within `SOL::DEFAULT_MAX_RENDER_DISTANCE_CM` = 1,000,000 km, otherwise depth-compressed on a log scale with angular size preserved, per Amendment 1), converted with `EclipticToUnreal`; positions stay `FVector3d` until that last step. `UWorld::SetNewWorldOrigin` was rejected because its origin is an `FIntVector` in int32 centimeters (about ±21,000 km), far too small for AU-scale anchors, it is a World Composition-era feature, and our bodies are re-placed from the universe every frame anyway, so shifting every actor through the engine buys nothing. The anchor still drives rebasing, as SDD 1 requires, but the render origin is the observer's position at that moment rather than the anchor body's center, which can be ~10^9 km away. One tickable subsystem (`USOLAnchorSubsystem`) drives the frame in a fixed order: clock, registry, anchor/rebase, then `OnUniverseUpdated` for the visuals. The clock and registry subsystems do not tick themselves. All three subsystems exist only in Game/PIE worlds whose game mode (the map's override, else the project default) is `ASOLGameMode`. The sim clock stops when the game is paused, because tickable subsystems do not tick while paused. Bodies shade themselves: `/Game/SOL/Materials/M_SOLBody` is an unlit parametric material (two colors, latitude bands, noise patches, polar caps, emissive) whose emissive output is `albedo * (saturate(N . SunDirection) + AmbientLight) * SunIlluminance / pi + EmissiveColor`, and `ASOLBodyVisuals` sets each body's `SunDirection` (unit vector body to Sun, Unreal axes) every frame through a cached parameter index, so every body's phase is correct wherever it is relative to the observer. A shadowless directional light re-aimed from the Sun toward the observer is kept for lit objects near the observer (the 1b ship); the body material ignores it. Exposure is fixed (manual) with bloom. The material is generated headless by `Tools/CreateSOLContent.py`, which also creates `/Game/Maps/SOL_Test`. The debug spectator accepts `-SOLStart=<Body>`, `-SOLAltitudeKm=<km>` and `-SOLLookAt=<Body>`, and the game mode accepts `-SOLSmokeShot=<s>`, which takes a screenshot and then quits, for headless smoke runs.
- **1b:** the player ship is a Mass entity (transform, velocity, orientation quaternion, angular velocity, ship parameters as fragments); processors for flight and gravity. `ASOLShipPawn` is a thin proxy owning input (Enhanced Input, created in C++), the spring-arm camera and the placeholder ship mesh, and writes control state into the entity. Collision is analytic against the body registry, with no physics-engine bodies for planets.
- **1b Mass ship (as implemented):** the ship is one Mass entity (MassEntity and MassCore engine modules; no MassGameplay/MassSimulation plugin needed) with fragments `FSOLShipStateFragment` (wraps `FSOLShipState`, plus the last contact body), `FSOLShipControlFragment` (wraps `FSOLShipControl`), `FSOLShipParamsFragment` (wraps `FSOLFlightParams`; per entity so `SetFlightParams` needs no archetype move) and the tag `FSOLPlayerShipTag`, all in `Ship/`. `USOLShipFlightProcessor` does gravity, `SOLFlight::Step` and `ResolveSphereCollision` against every body (Sun included) per entity in one pass, reading `USOLBodyRegistrySubsystem`'s `FSOLBodyFrameCache` (Unreal-handed positions/velocities, radii, GMs; sized once, refreshed in place right after each registry update). The frame's real delta is split into substeps of at most `SOL::SHIP_MAX_SUBSTEP_S` (1/30 s), at most `SOL::SHIP_MAX_SUBSTEPS` (8); a longer hitch is dropped. Gravity is only summed in Newtonian mode (assist ignores it). **Ordering:** Mass processing phases tick inside tick groups, while the universe update is a tickable subsystem that ticks after `TG_PostPhysics`, so a phase-registered processor would step the ship against last frame's bodies and the anchor would lag it. The processor is therefore not auto-registered; `USOLShipSubsystem` runs it with `UE::Mass::Executor::Run` (reused command buffer, prebuilt chunk callback, so a step allocates nothing) from the anchor subsystem's new `OnBodiesUpdated(realDelta)` event. Each frame is: sim clock, registry update, Unreal-handed cache, ship step (real time), observer sync (`SyncObserverPositionM`, ecliptic), anchor/rebase, `OnUniverseUpdated` (visuals). `USOLShipSubsystem` (created only in `ASOLGameMode` game worlds) spawns the ship at world begin play with `MakeCircularOrbitState` 10,000 km above Earth's Sun side (`-SOLStart`/`-SOLAltitudeKm` override) and teleports the observer onto it; its Pawn-facing API is `SetControl`/`GetControl`, `GetState` (copy, Unreal-handed), `SetState` (teleport), `GetUniversePositionM` (ecliptic), `SetFlightParams`/`GetFlightParams`, `GetReferenceVelocityMps`, `FindNearestBody`, `GetContactBodyIndex`, `IsControlScripted`. The subsystem overwrites `Control.ReferenceVelocityMps` each step with the targeting subsystem's frame velocity (see the next item). With `-SOLSpectator` the debug spectator rides the ship as a camera instead of `ASOLShipPawn`. `-SOLSmokeFlight` runs `FSOLShipSmokeFlight`, a scripted pawn-less flight (hover in assist, assisted cruise at a 100 m/s cap, Newtonian thrust and coast, pitch, assisted braking, Newtonian impact on Earth) that logs `LogSOL` checkpoints and a PASS/FAIL per phase, then quits.
- **1b pawn, input and targeting (as implemented):** `ASOLShipPawn` (`Ship/`) is the game mode's default pawn (`-SOLSpectator` selects the debug spectator). It never carries universe coordinates: it registers as the anchor subsystem's observer actor and, on every `OnUniverseUpdated`, sits at the observer's render location (within 10 km of Unreal's origin) with the ship's orientation. Enhanced Input actions and one mapping context are created in C++ (shared helpers in `Game/SOLInputHelpers`), added in `NotifyControllerChanged` and removed in `UnPossessed`/`EndPlay`; every callback forwards to a plain `Handle*` function. The pawn ticks in `TG_PrePhysics` (after its controller processed input) and composes `FSOLShipControl` from the held state without allocating: thrust, roll, boost, assist, the speed cap (wheel via `SOLFlight::StepSpeedCap`, start 1000 m/s) and the virtual joystick. The joystick accumulates mouse deltas in pixels (Mouse2D divided by the input config's axis sensitivity) inside a circle of 0.35 x the smaller viewport dimension, mapped with `JoystickToRotation` (dead zone 0.05, exponent 1.5); middle mouse recenters; Alt freezes the stick and orbits the spring arm instead. Camera: spring arm 60 m behind, socket 15 m up, rotation lag 8, no position lag, no collision test; FOV eases from 90 to 110 degrees on a log scale of the speed relative to the frame (100 m/s to 0.5c). The placeholder ship is nine engine primitives (~21 m, 16 m span) with `BasicShapeMaterial` instances and an emissive engine glow from the body material. `USOLTargetingSubsystem` (`Targeting/`) owns the candidates (every registry body at its body index, then every registered `ISOLTargetable`, rebuilt in place each frame from the Unreal-handed cache), the selection and the M lock (a candidate index that persists across anchor and selection changes until M is pressed again; with no selection M locks the anchor). **Reference-frame refactor:** the ship subsystem no longer owns a reference body; each step it calls `Targeting->RefreshCandidates()` and then takes `GetReferenceVelocityMps()` (`SOLTargeting::ResolveReferenceVelocity`: the locked target, else the anchor body), so candidates are always refreshed before the ship step. Spawn and `SetState` teleports refresh it too, because they can change the anchor. `-SOLSmokeInput` (`Ship/SOLShipSmokeInput`) injects key, mouse and wheel events with `APlayerController::InputKey(FInputKeyEventArgs::CreateSimulated(...))` (the non-deprecated form of `FInputKeyParams`) so the mapping context, triggers and callbacks run end to end, logs a PASS/FAIL per check, takes a screenshot behind the ship facing Earth and quits.
- **1b review fixes (Amendment 2, as implemented; supersede the matching details in the two items above):** *Warp carry:* each step `USOLShipSubsystem` takes the active frame (`USOLTargetingSubsystem::GetReferenceIndex`: M lock, else anchor) and passes the processor `FrameCarryDisplacement(prev, now, refVelocity, realDt)` for position, plus the same function applied to the frame's velocity with its average sim-time acceleration (`dV / (warp * dt)`, i.e. `dV * (1 - 1/warp)`). The velocity carry was added beyond Amendment 2 because without it assist lags the frame's warp-rotated velocity (~8.5 m/s per frame for Earth at 1 d/s, ~770 m/s steady lag). Ships get the velocity carry at the frame start and the position carry spread evenly over the substeps, so relative to the frame they move exactly on real time. Every body's previous position and velocity is kept each frame, so an anchor or lock switch between bodies carries with the new body at once (a skipped frame would drop ~43,000 km at 1 d/s); only the first frame and a switch to or between non-body targetables carry nothing that frame and re-seed; a paused world does not tick. *Hitch budget:* `USOLAnchorSubsystem` clamps the real delta to `SOL::MAX_FRAME_DELTA_S` (0.5 s) once and feeds that value to both the sim clock and the ship; `SOL::SHIP_MAX_SUBSTEPS` is 16 (16 x 1/30 s >= 0.5 s, `static_assert`), so no ship time is dropped. *Substep bodies:* the processor fills a scratch array (sized once, `(SHIP_MAX_SUBSTEPS + 1) x bodies`) with `BodyPositionAtSubstep` at every substep boundary, using each body's average frame velocity (`(now - prev) / (warp * dt)`) so the substeps interpolate exactly between last frame's and this frame's positions. The instantaneous velocity is not used there because it misses last frame's position by ~6 km for Earth at 1 d/s on the curved orbit. Gravity uses the substep-start positions. *Swept collision and broadphase:* `ResolveSweptSphereCollision` per body per substep, in the body's frame, with the body's instantaneous velocity; a body is skipped when the ship's start distance exceeds `R + r` plus the relative travel of that substep. This is a cheap O(bodies) reject per substep; a spatial structure waits until body counts grow. *Shared params:* `FSOLShipParamsFragment` is a `FMassConstSharedFragment` (one value per ship class, read once per chunk). Mass de-duplicates shared values by the CRC of their reflected properties, so it carries a reflected `ParamsCrc` (CRC of the plain `FSOLFlightParams` bytes, set by `Make`). `SetFlightParams` swaps the value (`SwapConstSharedFragmentForEntity`, a chunk move). *Parallel:* the chunk loop is `ParallelForEachEntityChunk`, because each job writes only its own entities' state and fragment, and everything shared (body cache, scratch positions, frame inputs, the shared params) is read-only during the run. Per-job command buffers are disabled because the query issues no commands. The engine still allocates one small job array per call; our code allocates nothing per run. *Pawn:* on viewport focus loss (edge of `FViewport::HasFocus() && FApp::HasFocus()`, ignored while `-SOLSmokeInput` injects input) the stick recenters and thrust, roll and boost zero. `UnPossessed`/`EndPlay` show the cursor and set game-and-UI input. The spring arm's own rotation lag is off; `UpdateCameraRotation` slerps the arm's world rotation toward the rendered ship orientation (times free-look) by `1 - exp(-8 dt)` right after `FollowShip`. A shadowless directional fill light on the camera (`SOL::SUN_ILLUMINANCE_LUX x 0.1`, tilted 45 degrees down) lights the ship's shadowed side. The ship parts are a named table. `-SOLSmokeFlight` adds f-Warp (1 d/s, assist, hands off 5 s: altitude drift < 50 m, relative speed < 5 m/s, anchor stays Earth), g-Collide under that warp, and h-Surface (resting 5 s under warp), then resets warp to 1x.
- **1c:** HUD is C++-only with no hand-authored editor assets: dynamic markers, radar and orbit lines are drawn in the HUD canvas; the F3 speed panel is a UMG widget tree built in C++. Every HUD number reads from subsystems, not from Actors.
- **1c HUD (as implemented):** `ASOLFlightHud` (`UI/`, the game mode's HUD class; the temporary `SOLDebugHUD` is deleted) does everything in `DrawHUD` and does not tick. It projects with its own pinhole model built from the player camera's cached POV (horizontal FOV across the canvas width), so points behind the camera are rejected explicitly instead of being mirrored. The target bracket uses the body's own render placement (`ComputeBodyRenderPlacement`), sized by its apparent radius, and falls back to an edge arrow along the camera-local direction when off-screen or behind. Prograde/retrograde markers project the relative-velocity direction (the Unreal-handed universe frame shares Unreal's axes). The radar rebuilds `FSOLRadarContact`s in place from the targeting candidates (ship-local through the ship orientation), then calls `ProjectContact` for every contact, drawn on a tilted disc. Radar range per Appendix C Amendments 1 and 3 (implemented): the HUD holds a mode flag (AUTO at start, MANUAL) and a manual range; AUTO's range is `EffectiveFloorM(bHasSelectedTarget)` every frame (10,000 km, or 1 km with a target; `ComputeRange` is removed); `-` / `=` (pawn actions `IA_ShipRadarZoomIn`/`Out`) call `ASOLFlightHud::StepRadarZoom`, which applies `StepManualRange` once to the range on screen and switches to MANUAL; Home (`IA_ShipRadarAuto`) returns to AUTO; in MANUAL the range is reclamped every frame to `[EffectiveFloorM(bHasSelectedTarget), MaxRangeM]`; the label reads e.g. `RADAR 10000.0 km (AUTO)`. The interim nearest-4 rule (`HUD_RADAR_RANGE_CONTACTS`) is removed and every contact is plotted. Orbit ellipses: each planet's `SOLOrbitLines::SampleEllipse` of `AtCenturies(now)` (new accessor `FSOLBodyRegistry::GetElements`) is cached per body (reserved once) and re-sampled on toggle-on and every 5 real seconds; each frame every point goes parent position + point through the new `USOLAnchorSubsystem::ComputePointRenderLocationCm` (the body placement with radius 0), then to camera-local, clipped at a near plane and to the canvas (Liang-Barsky). Text goes through one reused `FString` buffer (`Reset` + `Appendf`/`AppendString`); distances are appended in place with `FormatDistanceM`'s bands and format, and each HUD line keeps a cached `FText` rebuilt only when its text changes, so the only per-frame allocations left are the `FText` of lines whose text changed (speed, altitude, distances, the clock) and whatever Canvas allocates internally to lay out text. Canvas lines ignore alpha, so dim line colors are dimmed in RGB.
- **1c F3 panel (as implemented):** `USOLSpeedPanelWidget` builds its tree in `NativeOnInitialized` (full-screen dimmer border, centered panel, odometer digit text blocks and a unit text block side by side, a size-boxed `UScrollBox` with one text row per body) and re-renders its texts only on input. F3 in game is an Enhanced Input action; the pawn opens the panel at its next tick (outside the input callback): it flushes pressed keys, removes the ship mapping context, zeroes thrust, roll, boost and the stick, adds the widget to the viewport and sets `FInputModeUIOnly` with the widget focused and the cursor shown. The Mass simulation and time keep running; only the ship's own control input stops. The widget consumes every key (key-up too) so nothing bubbles to the game viewport. Digit steps (`SOLSpeedStepper::StepDigit`) and list picks (`FromSpeedMps` of the body's speed relative to its parent) both call `ASOLShipPawn::ApplySpeedCapMps` at once; Enter on the steppers, F3 or Esc close through `ASOLShipPawn::CloseSpeedPanel`, which re-adds the mapping context and restores game-only input with the hidden cursor. The O key toggles the HUD's orbit ellipses; P is bound and does nothing. `-SOLSmokeHud` (`UI/SOLHudSmoke`) verifies it end to end: game keys via `APlayerController::InputKey`, panel keys via `FSlateApplication::ProcessKeyDownEvent` to the focused widget.
- **1c post-review fixes (Appendix C Amendment 4, implemented):** `ProjectContact` scales the whole ship-local vector onto the range sphere before the collapse law; `CanCycleUnit` refuses a unit switch whose value would reach 1e9 and `CycleUnit` then returns the state unchanged. HUD: the radar reserves the longest downward stalk (plus half a selected blip) between the disc and the range label, and the selected blip's name flips left of the blip near the screen edge; the orbit-line near-plane clip now clips at the near plane itself (after rejecting segments with no endpoint strictly in front, the ratio is in (0, 1) by construction; `ProjectLocal` accepts points exactly on the plane); a `-` / `=` press that leaves the range unchanged keeps the current mode (so `-` at the AUTO floor stays AUTO); the HUD's private distance formatter is replaced with `SOLHudFormat::AppendDistanceM` (tested against `FormatDistanceM`); the reticle and joystick draw before the velocity markers and target bracket; the label reads e.g. `RADAR 10000.0 km (AUTO)`; long lines reflowed.
- **Targetables:** an interface (`ISOLTargetable`-style) with position, velocity, name, radius; the body registry exposes bodies through it.
- **Testing:** logic is unit-tested with UE automation tests (Kepler accuracy, clock warp math, anchor hysteresis, flight ramp, gravity, quaternion rotation stays gimbal-free, target selection, collision clamp); visual behavior gets a PIE smoke check when the editor is open. Tests are authored by a separate subagent from the contract only.

## 4. Open questions

- Deferred from the 1a review (finding 10): the temporary debug HUD built strings every frame. Resolved in 1c: the debug HUD is deleted and the flight HUD reuses its text buffer (section 3 lists the few allocations Canvas text and `FormatDistanceM` still make).
- 1c: with every planet tracked, the radar's auto range (section 6) was always the 50 AU clamp (Neptune at ~30 AU rounds up to 1e13 m), so near contacts collapsed toward the center. Resolved by Appendix C Amendment 1 (implemented, section 3): 1 AU ceiling, target-dependent floor, AUTO/MANUAL modes with `-` / `=` zoom and Home back to AUTO; the interim nearest-4 rule is gone. Appendix C Amendment 2 (implemented) removes the anchor's root star (the Sun) from the AUTO range pass; it is still plotted. Resolved by Appendix C Amendment 3 (implemented): the farthest-fits-all rule and `ComputeRange` are removed, and AUTO is simply the floor (`EffectiveFloorM`: 10,000 km, 1 km with a target), so near contacts are no longer crushed toward the center; Amendment 2's root-star exclusion is gone with it. Manual zoom (`-` / `=` up to 1 AU, Home back to AUTO) is unchanged. The Sun "too hot" HUD warning (section 2, Collision) is not built yet.
- Deferred from the 1a review (finding 12): profiling evidence (`stat` / Unreal Insights) for the universe update and body visuals is gathered in 1c and Part 10.

Ship stat numbers not fixed above (exact thrust/accel values for Newtonian mode, boost multiplier, camera arm length) are chosen during 1b and recorded in `Docs/GAME_MECHANICS.md`.

## 5. Revision history

- 2026-09-28: initial SDD from the Part 1 grill.
- 2026-09-28: 1a engine integration: observer-relative rendering with our own render origin instead of `SetNewWorldOrigin` (section 3).
- 2026-09-28: 1a adversarial review: Amendment 1 implemented; per-body sun shading in the body material replaces the shared light for bodies; subsystems gated on `ASOLGameMode` (section 3); two findings deferred (section 4).
- 2026-09-28: 1b Mass ship: fragments, flight processor run from the anchor tick via `OnBodiesUpdated`, Unreal-handed body cache, ship subsystem as observer, `-SOLSmokeFlight` (section 3).
- 2026-09-28: 1b pawn: `ASOLShipPawn` (C++ Enhanced Input, virtual joystick, chase camera, placeholder mesh), `USOLTargetingSubsystem` and `ISOLTargetable`, reference velocity moved from the ship subsystem to targeting, `-SOLSmokeInput` (section 3).
- 2026-09-28: 1c engine integration: `ASOLFlightHud` (canvas HUD, radar, orbit ellipses), `USOLSpeedPanelWidget` (F3), O/P/F3 bindings, `-SOLSmokeHud`; debug HUD deleted (section 3).
- 2026-09-28: 1b adversarial review: Amendment 2 implemented (warp carry incl. a velocity carry, shared hitch budget, interpolated substep bodies, swept collision with broadphase, const shared params, parallel chunk loop, focus loss, manual camera lag, fill light), smoke flight extended with warp phases (section 3).
- 2026-09-28: 1c radar Appendix C Amendment 1 implemented: 1 AU ceiling, target-dependent floor, AUTO/MANUAL range with `-` / `=` / Home; nearest-4 interim rule removed (sections 3, 4).
- 2026-09-28: 1c radar Appendix C Amendment 2 implemented: AUTO range excludes the anchor's root star; AUTO still pinned at 1 AU by the outer planets (section 4).
- 2026-09-29: 1c adversarial review: Appendix C Amendment 4 implemented (whole-vector radar scaling, stepper unit overflow refusal, HUD integration fixes) (section 3).

---

## Appendix A — Part 1a API contract

The exact public surface that the 1a tests and implementation share. All types live in the `Universe/` feature folder and are plain C++ (not UObjects) so they are unit-testable without a world. Units: meters, m/s, seconds, radians unless a name says otherwise (`Cm` = centimeters, `AU`, `Deg`). **Universe frame:** right-handed J2000 ecliptic, +X toward the vernal equinox, +Z toward the north ecliptic pole. `SOLRender::EclipticToUnreal` converts to Unreal's left-handed axes.

### `Universe/SOLKepler.h`

```cpp
struct FSOLKeplerElements            // orbit at one instant; angles in radians
{
    double SemiMajorAxisM = 0.0;
    double Eccentricity = 0.0;                 // 0 <= e < 1
    double InclinationRad = 0.0;
    double LongitudeOfAscendingNodeRad = 0.0;
    double ArgumentOfPeriapsisRad = 0.0;
    double MeanAnomalyRad = 0.0;               // at the instant the elements describe
};

struct FSOLOrbitState { FVector3d PositionM; FVector3d VelocityMps; };

struct FSOLSecularElements           // JPL Standish "approximate positions of the major planets", 1800-2050
{
    double A0AU, ADotAUPerCy;                  // semi-major axis
    double E0, EDotPerCy;                      // eccentricity
    double I0Deg, IDotDegPerCy;                // inclination
    double L0Deg, LDotDegPerCy;                // mean longitude
    double LongPeri0Deg, LongPeriDotDegPerCy;  // longitude of perihelion
    double LongNode0Deg, LongNodeDotDegPerCy;  // longitude of ascending node
    // T = Julian centuries since J2000. ArgPeri = LongPeri - LongNode; M = L - LongPeri wrapped to [-PI, PI).
    FSOLKeplerElements AtCenturies(double T) const;
};

namespace SOLKepler
{
    double WrapAngleRad(double AngleRad);                                   // result in [-PI, PI)
    double SolveEccentricAnomaly(double MeanAnomalyRad, double Eccentricity); // any M; |M - (E - e*sin E)| < 1e-10
    double PeriodSeconds(double SemiMajorAxisM, double GM);                 // 2*PI*sqrt(a^3/GM)
    // State ElapsedSeconds after the instant of Elements; mean anomaly advances by sqrt(GM/a^3)*Elapsed.
    FSOLOrbitState ElementsToState(const FSOLKeplerElements& Elements, double GM, double ElapsedSeconds);
}
```

### `Universe/SOLSimClock.h`

```cpp
struct FSOLSimClock
{
    static constexpr double J2000JulianDate = 2451545.0;
    static TConstArrayView<double> WarpFactors();   // sim seconds per real second: 1, 10, 100, 1000, 3600, 86400, 864000, 2592000
    static double JulianDateFromUtc(int32 Year, int32 Month, int32 Day, int32 Hour, int32 Minute, double Second); // proleptic Gregorian
    void Init(double StartJulianDate);              // warp index 0
    void Advance(double RealDeltaSeconds);          // sim time += delta * current warp factor
    double GetJulianDate() const;
    double GetSecondsSinceJ2000() const;
    double GetCenturiesSinceJ2000() const;          // Julian centuries of 36525 days
    int32 GetWarpIndex() const;
    double GetWarpFactor() const;
    void StepWarpUp();                              // clamps at the last step
    void StepWarpDown();                            // clamps at index 0
    void ResetWarp();                               // index 0
};
```

### `Universe/SOLBodyRegistry.h`

```cpp
struct FSOLBodyDef
{
    FName Name;
    double RadiusM = 0.0;
    double GM = 0.0;                          // m^3/s^2
    int32 ParentIndex = INDEX_NONE;           // parent must be added before its children; INDEX_NONE for the Sun
    FSOLSecularElements Elements;             // relative to the parent; unused for the Sun
};

class FSOLBodyRegistry
{
public:
    int32 AddBody(const FSOLBodyDef& Def);    // returns the new index
    void PopulateSolarSystem();               // clears, then adds Sun=0, Mercury=1, Venus=2, Earth=3, Mars=4, Jupiter=5, Saturn=6, Uranus=7, Neptune=8 (planets orbit the Sun); names "Sun","Mercury",...
    void Update(double SecondsSinceJ2000);    // absolute (Sun-frame) positions and velocities; Sun fixed at the origin
    int32 Num() const;
    int32 FindByName(FName Name) const;       // INDEX_NONE if absent
    FName GetName(int32 Index) const;
    int32 GetParent(int32 Index) const;
    double GetRadiusM(int32 Index) const;
    double GetGM(int32 Index) const;
    const FVector3d& GetPositionM(int32 Index) const;
    const FVector3d& GetVelocityMps(int32 Index) const;
    TConstArrayView<FVector3d> GetPositionsM() const;
    TConstArrayView<FVector3d> GetVelocitiesMps() const;
};
```

Reference constants for `PopulateSolarSystem` (GM m^3/s^2, mean radius m): Sun 1.32712440018e20, 6.957e8; Mercury 2.2032e13, 2.4397e6; Venus 3.24859e14, 6.0518e6; Earth 3.986004418e14, 6.371e6; Mars 4.282837e13, 3.3895e6; Jupiter 1.26686534e17, 6.9911e7; Saturn 3.7931187e16, 5.8232e7; Uranus 5.793939e15, 2.5362e7; Neptune 6.836529e15, 2.4622e7. One astronomical unit is 149,597,870,700 m. Planet orbits use the Sun's GM as the central body.

### `Universe/SOLAnchor.h`

```cpp
struct FSOLAnchorSelector
{
    static constexpr double SwitchRatio = 0.25;
    int32 GetAnchorIndex() const;             // INDEX_NONE before the first Update
    // First call: anchor = nearest body. Afterwards: candidate = nearest body other than the current anchor;
    // switch to it only if Dist(observer, candidate) < SwitchRatio * Dist(observer, currentAnchor). Returns the anchor.
    int32 Update(TConstArrayView<FVector3d> BodyPositionsM, const FVector3d& ObserverPositionM);
};
```

### `Universe/SOLRenderPlacement.h`

```cpp
struct FSOLRenderPlacement { FVector3d LocationCm; double RadiusCm = 0.0; };   // relative to the observer (camera), ecliptic axes

namespace SOLRender
{
    constexpr double MetersToCm = 100.0;
    // Exact 1:1 when |Relative| in cm <= MaxRenderDistanceCm; otherwise the location is placed along the same direction at
    // MaxRenderDistanceCm and the radius is scaled by the same factor, so angular size (Radius/Distance) is preserved.
    FSOLRenderPlacement ComputePlacement(const FVector3d& RelativeToObserverM, double RadiusM, double MaxRenderDistanceCm);
    FVector3d EclipticToUnreal(const FVector3d& Ecliptic);                        // (X, -Y, Z): right-handed to Unreal's left-handed axes
}
```

### Non-unit-tested 1a pieces

Sim-clock, body-registry and anchor subsystems (UObject wrappers around the classes above), the body-visuals actor, sun lighting, the free-fly spectator pawn, the `/Game/Maps/SOL_Test` map and default-map config. These get a PIE smoke check.

### Contract clarifications (settled after the test author's ambiguity report)

- `SolveEccentricAnomaly` returns E on the same branch as M, so that `M == E - e*sin(E)` holds without wrapping (M is not reduced to [-PI, PI)).
- A child body's orbit uses its **parent's** GM as the central-body GM. A child's absolute position is the parent's absolute position plus its relative state; its absolute velocity is likewise summed.
- `FSOLBodyRegistry::Update(T)` evaluates the secular elements at T (`AtCenturies`) and converts them to a state with `SOLKepler::ElementsToState(elements, parentGM, 0.0)`; it does not propagate from a fixed epoch. Velocities therefore follow the two-body mean motion, which can differ from Standish's fitted rates by up to ~5e-4 (relative).
- `AtCenturies` need not wrap the argument of periapsis or the node; `ElementsToState` must accept any angle.
- Constant names in the contract (`J2000JulianDate`, `SwitchRatio`, `MetersToCm`) stay PascalCase; the style guide's UPPER_SNAKE constant rule is still under review.

### Amendment 1 — post-review changes (2026-09-28, 1a adversarial review)

These supersede the matching statements in Appendix A.

1. **Depth-ordered far placement (`SOLRender::ComputePlacement`).** The previous rule (every far body pulled to exactly `MaxRenderDistanceCm`) lets a large distant body swallow a nearer small one. New rule, with `d = |RelativeToObserverM| * MetersToCm` and `Max = MaxRenderDistanceCm`: if `d <= Max` the result is exactly 1:1 (unchanged). Otherwise `d' = Max * (1 + FarDepthLogScale * ln(d / Max))` with `constexpr double SOLRender::FarDepthLogScale = 0.1`; the location is `direction * d'` (direction preserved) and `RadiusCm = RadiusM * MetersToCm * (d' / d)`, so angular size `Radius/Distance` is still preserved. Consequences: `|LocationCm| >= Max`; `d'` is continuous at `d == Max` and strictly increasing in `d`, so bodies keep their true depth order. Zero and non-finite-safe behavior unchanged (no NaN).
2. **Testable render-origin math.** The origin-rebasing arithmetic moves out of `USOLAnchorSubsystem` into a pure struct in `Universe/SOLRenderOrigin.h`:

```cpp
struct FSOLRenderOrigin
{
    static constexpr double SnapDistanceM = 10000.0;
    FVector3d OriginM;                                   // universe point drawn at Unreal (0,0,0)
    void Reset(const FVector3d& ObserverM);              // OriginM = ObserverM
    // Moves the origin to ObserverM and returns true if bForceSnap, or if |ObserverM - OriginM| >= SnapDistanceM; else returns false and leaves OriginM.
    bool Update(const FVector3d& ObserverM, bool bForceSnap);
    // Unreal-space centimeters = EclipticToUnreal(UniverseM - OriginM) * MetersToCm, and the exact inverse.
    FVector3d UniverseToRenderCm(const FVector3d& UniverseM) const;
    FVector3d RenderCmToUniverseM(const FVector3d& RenderCm) const;
    // A body's render location and radius in Unreal space: ComputePlacement(BodyM - ObserverM, RadiusM, MaxRenderDistanceCm)
    // converted to Unreal axes and offset by the observer's render location (UniverseToRenderCm(ObserverM)).
    FSOLRenderPlacement BodyPlacement(const FVector3d& BodyM, const FVector3d& ObserverM, double RadiusM, double MaxRenderDistanceCm) const;
};
```

3. **Orbit-element validity.** `FSOLSecularElements::AtCenturies` clamps the returned eccentricity to `[0.0, 0.99]` for any T (Standish rates go negative for distant epochs). It does not clamp T. Leaving the 1800-2050 validity window (T outside [-2.0, 0.5]) is logged once by the body-registry subsystem, not by the pure class.


---

## Appendix B — Part 1b flight-logic API contract

Plain C++ (no UObjects), unit-testable, in `Source/SOLTest/Flight/`. **Frame:** the ship works in an *Unreal-handed universe frame*: positions and velocities are the universe (ecliptic) values with `SOLRender::EclipticToUnreal` applied (Y negated), in meters and m/s, so `FQuat4d` behaves as in Unreal (local +X forward, +Y right, +Z up). The functions themselves are frame-agnostic for positions; callers convert registry data with `EclipticToUnreal`.

### `Flight/SOLFlight.h`

```cpp
struct FSOLShipState
{
    FVector3d PositionM = FVector3d::ZeroVector;
    FVector3d VelocityMps = FVector3d::ZeroVector;
    FQuat4d Orientation = FQuat4d::Identity;                 // ship-local -> universe frame
    FVector3d AngularVelocityRadPerS = FVector3d::ZeroVector; // ship-local axes: X roll, Y pitch, Z yaw
};

struct FSOLFlightParams
{
    double AssistTimeConstantS = 1.5;
    double NewtonianMainAccelMps2 = 50.0;      // forward/back
    double NewtonianStrafeAccelMps2 = 30.0;    // right/left and up/down
    double BoostAccelMultiplier = 3.0;
    double BoostSpeedMultiplier = 2.0;
    double MaxPitchYawRateRadPerS = 1.5707963267948966;   // 90 deg/s
    double MaxRollRateRadPerS = 2.0943951023931953;       // 120 deg/s
    double AngularResponseTimeS = 0.15;
    double MaxSpeedMps = 149896229.0;          // 0.5c
    double MinSpeedCapMps = 1.0;
    double ShipRadiusM = 10.0;
};

struct FSOLShipControl
{
    FVector3d Thrust = FVector3d::ZeroVector;    // local X forward, Y right, Z up, each in [-1, 1]; clamped to unit length overall
    FVector3d Rotation = FVector3d::ZeroVector;  // X roll, Y pitch, Z yaw, each in [-1, 1]
    bool bBoost = false;
    bool bFlightAssist = true;
    double SpeedCapMps = 1000.0;                 // relative to the reference frame
    FVector3d ReferenceVelocityMps = FVector3d::ZeroVector;
};

namespace SOLFlight
{
    constexpr double SpeedCapStepsPerDecade = 10.0;

    // Advances the ship by Dt seconds (Dt >= 0). Orientation is integrated with quaternions (no Euler angles, no gimbal lock).
    FSOLShipState Step(const FSOLShipState& State, const FSOLShipControl& Control, const FSOLFlightParams& Params,
                       const FVector3d& GravityAccelMps2, double Dt);

    // Sum of GM * delta / r^3 toward every body; r is clamped to at least the body radius (no singularity). Arrays have equal length.
    FVector3d GravityAcceleration(const FVector3d& ShipPositionM, TConstArrayView<FVector3d> BodyPositionsM,
                                  TConstArrayView<double> BodyGMs, TConstArrayView<double> BodyRadiiM);

    // CurrentCap * 10^(WheelSteps / SpeedCapStepsPerDecade), clamped to [MinSpeedCapMps, MaxSpeedMps].
    double StepSpeedCap(double CurrentCapMps, int32 WheelSteps, const FSOLFlightParams& Params);

    // Virtual joystick: OffsetNorm (X right, Y up, each about [-1, 1]) -> (X = yaw, Y = pitch) in [-1, 1].
    // m = |Offset| clamped to 1; m <= DeadZone -> (0,0); else s = ((m - DeadZone) / (1 - DeadZone))^Exponent and the result is Offset/|Offset| * s.
    FVector2d JoystickToRotation(const FVector2d& OffsetNorm, double DeadZone, double Exponent);

    // If the ship overlaps the body (distance < BodyRadiusM + ShipRadiusM): move it to that distance along the outward direction,
    // and remove the inward component of its velocity RELATIVE to the body (tangential relative velocity is kept, so the ship slides).
    // Returns true on contact. A ship exactly at the body's center is pushed out along +Z (no NaN).
    bool ResolveSphereCollision(FSOLShipState& InOutState, const FVector3d& BodyPositionM, const FVector3d& BodyVelocityMps,
                                double BodyRadiusM, double ShipRadiusM);

    // Ship AltitudeM above BodyRadiusM on the TowardSunDir side, on a circular orbit of speed sqrt(GM/r) around the body, moving
    // along normalize(cross(Up=(0,0,1), TowardSunDir)) (or +Y if parallel), velocity = BodyVelocity + that. Orientation: forward = the
    // tangent, up = radially outward.
    FSOLShipState MakeCircularOrbitState(const FVector3d& BodyPositionM, const FVector3d& BodyVelocityMps, double BodyGM,
                                         double BodyRadiusM, double AltitudeM, const FVector3d& TowardSunDir);
}
```

**`Step` semantics.**

- Thrust input is first clamped to unit length overall (both modes).
- *Rotation (both modes):* commanded local angular velocity = (Rotation.X * MaxRollRate, Rotation.Y * MaxPitchYawRate, Rotation.Z * MaxPitchYawRate); the stored angular velocity relaxes toward it by `w += (cmd - w) * (1 - exp(-Dt/AngularResponseTimeS))`; the orientation is then multiplied by the quaternion for the local rotation `w * Dt` and re-normalized. Positive Pitch raises the nose (forward gains +local-up), positive Yaw turns right (forward gains +local-right), positive Roll rolls right-wing-down (up gains +local-right).
- *Assist (`bFlightAssist`):* target velocity = ReferenceVelocity + Orientation.RotateVector(Thrust) * SpeedCap * (bBoost ? BoostSpeedMultiplier : 1), with the relative target speed clamped to MaxSpeedMps; velocity relaxes toward it by `v += (target - v) * (1 - exp(-Dt / tau))`, `tau = AssistTimeConstantS / (bBoost ? BoostAccelMultiplier : 1)`. Gravity is ignored (thrusters cancel it). Zero thrust brakes the ship to the reference frame's velocity.
- *Newtonian:* `v += (Orientation.RotateVector(local accel) + GravityAccel) * Dt`, where local accel = (Thrust.X * Main, Thrust.Y * Strafe, Thrust.Z * Strafe) * (bBoost ? BoostAccelMultiplier : 1). No damping.
- In both modes: position += velocity * Dt after the velocity update (semi-implicit Euler), and if |velocity| exceeds MaxSpeedMps the velocity is scaled down to MaxSpeedMps.

### `Flight/SOLTargeting.h`

```cpp
struct FSOLTargetInfo { FName Name; FVector3d PositionM; FVector3d VelocityMps; double RadiusM = 0.0; };

namespace SOLTargeting
{
    constexpr double DefaultConeHalfAngleRad = 0.17453292519943295;   // 10 deg

    // Angular offset of a candidate = max(0, angle(Forward, direction to center) - asin(min(1, Radius / distance))). Candidates with
    // distance <= Radius (ship inside) are ignored. Returns the candidate with the smallest offset <= ConeHalfAngleRad (ties: nearer),
    // or INDEX_NONE. ForwardDir is a unit vector.
    int32 PickUnderReticle(TConstArrayView<FSOLTargetInfo> Candidates, const FVector3d& ShipPositionM, const FVector3d& ForwardDir,
                           double ConeHalfAngleRad);

    // Steps through the candidates ordered by center distance from the ship (nearest first, stable for ties).
    // Direction +1 = next farther, -1 = next nearer, wrapping. From CurrentIndex == INDEX_NONE: +1 -> nearest, -1 -> farthest.
    // Returns an index into Candidates, or INDEX_NONE if Candidates is empty.
    int32 Cycle(TConstArrayView<FSOLTargetInfo> Candidates, const FVector3d& ShipPositionM, int32 CurrentIndex, int32 Direction);

    // The active frame's velocity: the locked target's velocity when bLockActive and LockedTarget is non-null, else AnchorVelocityMps.
    FVector3d ResolveReferenceVelocity(bool bLockActive, const FSOLTargetInfo* LockedTarget, const FVector3d& AnchorVelocityMps);
}
```

### Non-unit-tested 1b pieces

Mass fragments and processors (ship state/control/params fragments; flight, gravity and collision processors that call the functions above), the Mass ship entity and its spawn, `ISOLTargetable` and the registry-backed targetables, `ASOLShipPawn` (Enhanced Input assets created in C++, virtual-joystick mouse using `JoystickToRotation`, key bindings per section 2, spring-arm chase camera with lag, speed-scaled FOV, Alt free-look, placeholder ship mesh), target selection and M-lock state, and sync of the ship's position into the anchor subsystem's observer state. These get a PIE smoke check.

### Appendix B clarifications (settled after the test author's ambiguity report)

- `GravityAcceleration`: for each body the magnitude is `GM / max(r, R)^2` and the direction is the unit vector from the ship toward the body; a ship exactly at a body's center gets zero from that body (direction undefined). So inside a body the pull is `GM/R^2` toward the center, never larger.
- `Step` uses the orientation at the START of the step to turn local thrust into a world direction; the rotation integrated in the same step affects the next step.
- `MakeCircularOrbitState` normalizes `TowardSunDir` internally (a zero vector is treated as +X).
- `Cycle`: a `CurrentIndex` outside `[0, Num)` is treated as `INDEX_NONE`; `Direction` > 0 means +1, < 0 means -1, 0 returns `CurrentIndex` unchanged.
- `Step` clamps each `Rotation` and `Thrust` component to [-1, 1] before use (thrust is then also clamped to unit length overall).

### Appendix B — Amendment 2 (post-review of 1b, 2026-09-28)

Additions to `Flight/SOLFlight.h` (existing declarations unchanged):

```cpp
namespace SOLFlight
{
    // Extra displacement a ship inherits because its reference frame moved by more than the ship's real-time flight accounts for
    // (time-warp): (RefPositionNowM - RefPositionPrevM) - RefVelocityMps * RealDt. At warp 1x with a body on a near-straight path it is ~0.
    FVector3d FrameCarryDisplacement(const FVector3d& RefPositionPrevM, const FVector3d& RefPositionNowM,
                                     const FVector3d& RefVelocityMps, double RealDt);

    // A body's position at a substep: EndPositionM - VelocityMps * WarpFactor * RemainingRealSeconds (RemainingRealSeconds >= 0 is the
    // real time left until the end of the frame). Used so gravity and collision see the body where it was during that substep.
    FVector3d BodyPositionAtSubstep(const FVector3d& EndPositionM, const FVector3d& VelocityMps, double WarpFactor,
                                    double RemainingRealSeconds);

    // Swept variant of ResolveSphereCollision. Work in the body's frame: the ship's segment runs from
    // (Before.PositionM - BodyPositionBeforeM) to (InOutAfter.PositionM - BodyPositionAfterM). If that segment enters the sphere of
    // radius BodyRadiusM + ShipRadiusM (even if both end points are outside it, i.e. tunnelling) the ship is placed on the sphere at the
    // ENTRY direction (position = BodyPositionAfterM + entryDirection * (BodyRadiusM + ShipRadiusM)) and the inward component of its
    // velocity relative to BodyVelocityMps (measured along that entry normal) is removed, keeping the tangential part. If the segment
    // does not enter but the end point is inside, behave exactly like ResolveSphereCollision. Returns true on contact.
    bool ResolveSweptSphereCollision(const FSOLShipState& Before, FSOLShipState& InOutAfter, const FVector3d& BodyPositionBeforeM,
                                     const FVector3d& BodyPositionAfterM, const FVector3d& BodyVelocityMps, double BodyRadiusM,
                                     double ShipRadiusM);
}
```

Behavioral decisions that need no new API (implementation and smoke-verified, not unit-tested):

- **Warp carry:** each ship step, the ship subsystem adds `FrameCarryDisplacement` for the active reference body (the M-locked target, otherwise the anchor body) to every ship, so a ship co-moves with its frame under time-warp; ship flight itself stays in real time.
- **Hitch budget:** the real delta fed to BOTH the sim clock and the ship is clamped to one shared `SOL::MAX_FRAME_DELTA_S` (0.5 s), so a long hitch slows the sim uniformly instead of desynchronising ship and bodies. The substep cap covers that budget.
- **Substep body positions:** gravity and collision use `BodyPositionAtSubstep` per substep.
- **Collision broadphase and scale:** a body is skipped unless the ship could reach `BodyRadiusM + ShipRadiusM` this frame (distance test with the frame's travel); flight parameters live in a const shared fragment per ship class; the chunk loop runs in parallel where safe.
- **Input and camera:** on viewport focus loss the virtual stick recenters and thrust/roll inputs zero; un-possessing restores the cursor and input mode; camera rotation lag is applied manually after the ship orientation is updated (no rigid-then-lagged double step); a shadowless fill "headlight" on the camera lights the ship's shadowed side.

### Amendment 2 clarifications (settled after the test author's ambiguity report)

- `ResolveSweptSphereCollision` removes the inward component from `InOutAfter`'s velocity (the post-step velocity), relative to `BodyVelocityMps`, along the entry normal. `Before`'s velocity is not used.
- Contact for an exact tangent graze or a ship resting exactly at `R + r` may be reported either way (not pinned).
- A segment that starts inside the sphere is handled like `ResolveSphereCollision` on the end point (no sweep). A segment that starts outside and ends outside after crossing (tunnelling) uses the entry point; only the FIRST entry matters.
- `FSOLBodyRegistry`-level targeting-subsystem tests (index fix-up, weak-pointer pruning, lock persistence) need a live world and are deferred; they are covered by the `-SOLSmokeInput` script for now.

## 6. Part 1c: HUD decisions (2026-09-28 grill)

| Area | Decision |
|---|---|
| Radar range | The outer ring auto-picks the smallest power-of-10 range in meters that contains every currently tracked targetable, clamped to [1 km, 50 AU]; a text label shows the current range; objects inside 1% of the range collapse toward the center radially rather than overlapping at the origin. |
| Orbit/prediction lines | Body orbit ellipses only (toggle O); the ship predicted-path toggle (P) is a no-op in 1c, deferred. Lines are drawn as 3D polylines in the ship's render frame projected to screen space through the HUD Canvas, with segments that cross behind the camera clipped; no new rendering system. |
| F3 speed panel | Modal UMG panel (built in C++, no editor assets) that pauses ship control input while open. Two steppers side by side: a numeric VALUE as odometer-style digits and a UNIT field (m/s / km/s / c). Up/Down (or wheel) steps the highlighted digit or unit with carry between digits; Left/Right move which digit/field is highlighted. Below the steppers, a scrollable list of every registry body with its current orbital speed; arrow-select + Enter sets the cap to that body's speed and its natural unit. Enter/F3 applies and closes; Esc closes without applying a pending edit beyond what Enter already committed. Returns input to the ship on close. |

## 7. Appendix C — Part 1c HUD API contract

Plain C++ (no UObjects) where the logic is non-trivial enough to unit test, in `Source/SOLTest/UI/`. Drawing itself (Canvas calls) is not unit-tested; the pure math that decides WHAT to draw is.

### `UI/SOLHudFormat.h`

```cpp
enum class ESOLSpeedUnit : uint8 { MetersPerSecond, KilometersPerSecond, LightSpeed };

namespace SOLHudFormat
{
    // Picks the most readable unit for a magnitude (m/s): < 1000 -> m/s; < 0.01c -> km/s; else -> c. Zero -> m/s.
    ESOLSpeedUnit PickSpeedUnit(double SpeedMps);

    // Converts to the unit's display value (m/s: unchanged; km/s: /1000; c: /299792458.0).
    double ToUnitValue(double SpeedMps, ESOLSpeedUnit Unit);

    // Inverse of ToUnitValue.
    double FromUnitValue(double Value, ESOLSpeedUnit Unit);

    // Formats a distance in meters into a compact string with an appropriate unit (m, km, or AU), e.g. "23402.9 km". Values under
    // 1000 m print as meters with one decimal; under 0.01 AU print as km with one decimal; else AU with 4 decimals.
    FString FormatDistanceM(double DistanceM);
}
```

### `UI/SOLRadarLayout.h`

```cpp
struct FSOLRadarContact { FVector3d RelativePositionM; FName Name; bool bSelected = false; };
struct FSOLRadarPoint { FVector2D ScreenOffsetUnit; double HeightStalkUnit = 0.0; bool bBehind = false; FName Name; bool bSelected = false; };

namespace SOLRadar
{
    constexpr double MinRangeM = 1000.0;
    constexpr double MaxRangeM = 50.0 * 149597870700.0;   // 50 AU
    constexpr double CollapseFraction = 0.01;

    // Smallest power of 10 (in meters) that is >= every |Contact.RelativePositionM|, clamped to [MinRangeM, MaxRangeM].
    // No contacts -> MinRangeM.
    double ComputeRange(TConstArrayView<FSOLRadarContact> Contacts);

    // Projects one contact into the radar's local ship-relative frame (ship-forward = local +Y "up" on the scope, ship-right = local
    // +X, ship-up = the height stalk) at the given Range: radial distance from center = min(1, |lateral| / Range), scaled up as
    // (dist/Range)^CollapseFraction-power-law so anything inside Range*CollapseFraction is pulled outward from the exact center by
    // that same law (never fully overlapping at the origin); ScreenOffsetUnit is in [-1, 1] on each axis; HeightStalkUnit is the
    // ship-up component similarly scaled; bBehind is true when the contact's ship-forward component is negative (still plotted, so the
    // scope shows all-around contacts, matching the Elite-style brief).
    FSOLRadarPoint ProjectContact(const FSOLRadarContact& Contact, double RangeM);
}
```

### `UI/SOLOrbitLines.h`

```cpp
namespace SOLOrbitLines
{
    constexpr int32 EllipseSegments = 90;

    // Samples an ellipse from Keplerian elements into EllipseSegments+1 points (closed: first == last) in the body's PARENT-relative
    // ecliptic frame (meters), by evaluating true anomaly uniformly over [0, 2*PI] via SOLKepler's position formula at that anomaly
    // (not time-stepped). Appends to OutPoints (caller-reserved, no internal allocation if capacity is already sufficient).
    void SampleEllipse(const FSOLKeplerElements& Elements, TArray<FVector3d>& OutPoints);
}
```

### `UI/SOLSpeedStepper.h`

```cpp
struct FSOLSpeedStepperState
{
    double ValueInUnit = 1.0;         // the displayed numeric value, always > 0
    ESOLSpeedUnit Unit = ESOLSpeedUnit::KilometersPerSecond;
    int32 HighlightedDigit = 0;       // 0 = ones place of the integer part, 1 = tens, ... negative = fractional digits (-1 = tenths)
};

namespace SOLSpeedStepper
{
    constexpr int32 MinDigit = -2;    // hundredths
    constexpr int32 MaxDigit = 6;     // up to 10^6 in the current unit

    // Returns the resulting mps after adding +1 (bDown=false) or -1 (bDown=true) at State.HighlightedDigit's place value in
    // State.Unit, with carry (e.g. 9 -> 0 carries into the next digit up; a carry past MaxDigit or below 0 clamps instead of
    // wrapping), then reports the corresponding SpeedCapMps clamped to [SOL::MIN_SPEED_CAP_MPS, SOL::MAX_SPEED_CAP_MPS].
    double StepDigit(const FSOLSpeedStepperState& State, bool bDown);

    // Cycles Unit forward (bDown=false: m/s -> km/s -> c -> m/s) or backward, converting ValueInUnit's underlying mps unchanged, and
    // reclamping HighlightedDigit into [MinDigit, MaxDigit] for the new unit's typical range.
    FSOLSpeedStepperState CycleUnit(const FSOLSpeedStepperState& State, bool bDown);

    // Builds a state from an absolute speed (used when opening the panel, or when a body is selected from the list): picks the unit
    // via SOLHudFormat::PickSpeedUnit and HighlightedDigit = 0.
    FSOLSpeedStepperState FromSpeedMps(double SpeedMps);
}
```

### Non-unit-tested 1c pieces

The UMG F3 widget tree and its input capture/pause behavior, the Canvas HUD drawing (reticle, joystick indicator, target bracket, prograde/retrograde markers, the radar scope rendering from `FSOLRadarPoint`, the orbit-ellipse polyline projection and clipping, the sim date/warp readout), the O toggle wiring, and replacing the temporary debug HUD. These get a PIE smoke check and the existing `-SOLSmokeShot` screenshot mechanism (extended with an `-SOLSmokeHUD` flag if useful to exercise the new HUD state without full input scripting).

### Appendix C clarifications (settled after the test author's ambiguity report)

- **Radar input frame (fixes an inconsistency with Appendix B):** `FSOLRadarContact::RelativePositionM` is in the ship-local frame with the SAME axes as `FSOLShipControl::Thrust` (X forward, Y right, Z up) — the earlier wording "ship-forward = local +Y" was wrong and is superseded. On screen: forward (X) maps to `ScreenOffsetUnit.Y` (up on the scope), right (Y) maps to `ScreenOffsetUnit.X`, up (Z) maps to `HeightStalkUnit`.
- **Collapse law (replaces the vague "power-law" wording):** let `t = clamp(|lateral| / RangeM, 0, 1)` where `|lateral|` is the ship-forward/right magnitude (excluding the height component). The projected radial magnitude is `r(t) = t < CollapseFraction ? sqrt(t * CollapseFraction) : t`. This is continuous and monotonically non-decreasing, equals 0 only at `t == 0`, equals `CollapseFraction` at `t == CollapseFraction`, and equals 1 at `t == 1`. `ScreenOffsetUnit` is `r(t)` times the unit direction of the lateral component (zero lateral component with nonzero height is a degenerate direction — treat it as `(0, 0)`). `HeightStalkUnit` uses the same `r` applied to `|ship-up component| / RangeM`, signed by the component's sign, clamped to `[-1, 1]`.
- **`StepDigit` borrow/carry:** ordinary decimal arithmetic on `ValueInUnit` at the given place value (e.g. tens digit 20 -> 19 stepping down by one at digit 0, or 20 -> 10 stepping down by one at digit 1), with the result clamped so the corresponding `SpeedCapMps` never goes below `SOL::MIN_SPEED_CAP_MPS` or above `SOL::MAX_SPEED_CAP_MPS`; clamping (not wrapping) applies at both ends, so a step that would cross a bound instead lands exactly on it.
- **`CycleUnit` digit target:** `HighlightedDigit` is unchanged by a unit cycle (it is already guaranteed to be within `[MinDigit, MaxDigit]`, which does not depend on the unit).
- **`PickSpeedUnit` sign:** uses `abs(SpeedMps)`.
- **`FormatDistanceM` boundaries:** `DistanceM < 1000.0` -> meters; else `DistanceM < 0.01 * 149597870700.0` -> kilometers; else astronomical units. Strict `<` at both boundaries (the boundary value itself takes the next band up).

### Appendix C — Amendment 1: radar range and manual zoom (2026-09-28, post-bug-fix)

Supersedes the radar range parts of Appendix C and its clarifications. Root cause of the original bug: the HUD fed every registry body into `SOLRadar::ComputeRange`, so a body at ~30 AU (Neptune) always forced the 50 AU clamp, crushing nearby contacts to the center. The interim fix (nearest-4-contacts) is replaced by this amendment, which the user resolved directly (not left to a subagent).

Changes to `UI/SOLRadarLayout.h`:

```cpp
namespace SOLRadar
{
    constexpr double MaxRangeM = 149597870700.0;         // 1 AU (was 50 AU)
    constexpr double DefaultFloorM = 10000000.0;          // 10,000 km: floor with no target selected
    constexpr double TargetedFloorM = 1000.0;             // 1 km: floor when a target is selected (close-combat detail)
    constexpr double ZoomStepFactor = 10.0;                // '-' / '=' step the manual range by this factor (one decade)

    // Effective floor for both auto and manual range: TargetedFloorM if bHasSelectedTarget, else DefaultFloorM.
    double EffectiveFloorM(bool bHasSelectedTarget);

    // Unchanged signature and behavior, EXCEPT the clamp is now [EffectiveFloorM(bHasSelectedTarget), MaxRangeM] instead of the old
    // [MinRangeM, MaxRangeM] (MinRangeM is removed from the public contract).
    double ComputeRange(TConstArrayView<FSOLRadarContact> Contacts, bool bHasSelectedTarget);

    // Steps a manual range by one decade (ZoomStepFactor), bZoomIn true = divide (zoom in), false = multiply (zoom out), clamped to
    // [EffectiveFloorM(bHasSelectedTarget), MaxRangeM].
    double StepManualRange(double CurrentRangeM, bool bZoomIn, bool bHasSelectedTarget);
}
```

(`ProjectContact` is unchanged.)

Behavioral decisions (HUD-side, not unit-tested — this is UI state, not pure math per the "Non-unit-tested 1c pieces" note):

- **Mode:** the radar starts in AUTO mode (uses `ComputeRange` every frame with the current contacts and whether a target is selected). Pressing `-` or `=` switches to MANUAL mode and applies `StepManualRange` once per press to the last-known range. **Home** returns to AUTO mode (Backspace is already the time-warp reset per section 2, so it is not reused here).
- **Display:** the range label shows "AUTO" or "MANUAL" next to the distance, e.g. "RADAR 3.2 AU (AUTO)" or "RADAR 12,000 km (MANUAL)".
- **Target-floor tracking:** the effective floor (and therefore the manual range's lower clamp) is re-evaluated every frame from whether a target is currently selected, so selecting a target while already zoomed in manually immediately unlocks the tighter 1 km floor without leaving manual mode; clearing the target re-clamps up to the 10,000 km floor if the current manual range was below it.

### Appendix C — Amendment 2: AUTO excludes the anchor's root star (2026-09-28)

HUD-integration-only change (no `SOLRadar` API change: `ComputeRange` stays a pure function of whatever contact list it's given). In AUTO mode, the contact array passed to `SOLRadar::ComputeRange` excludes the star at the root of the current anchor body's parent chain (walk `ParentIndex` from the anchor up to the body whose `ParentIndex == INDEX_NONE`; today that is always the Sun). That excluded star is still included in the array passed to `ProjectContact` for drawing, so it still appears on the scope (clipped to the rim if beyond the computed range). MANUAL mode is unaffected — it already ignores auto-computed range entirely. This lets AUTO tighten around whatever planets/moons/targets are actually nearby instead of being pinned at 1 AU by the ever-present Sun.

### Appendix C — Amendment 3: AUTO is the fixed floor, not farthest-fits-all (2026-09-29)

Retires the "smallest power of 10 that fits every contact" idea entirely — with real interplanetary distances, some tracked body is almost always farther than 1 AU, so that rule pins AUTO at the ceiling almost everywhere and two attempts to patch around it (nearest-N, excluding the anchor's root star) didn't fix it for normal play.

- **`SOLRadar::ComputeRange` is REMOVED** from `UI/SOLRadarLayout.h`/`.cpp`, along with its dedicated tests (`RangeConstantsAndEmpty`, `RangeSmallestPowerOfTen`, `RangeFarthestContactGoverns`, `RangeClamps`, `RangeFloorNoTarget`, `RangeFloorTargeted`) — deleted, not left disabled, since the behavior they pinned no longer exists in the product. `EffectiveFloorM`, `StepManualRange`, `MaxRangeM`, `DefaultFloorM`, `TargetedFloorM`, `ZoomStepFactor`, `ProjectContact` and their tests are unchanged and still load-bearing.
- **AUTO mode's range is simply `EffectiveFloorM(bHasSelectedTarget)`**, recomputed every frame from whether a target is currently selected: 10,000 km with no target, 1 km the instant a target is selected (and back to 10,000 km the instant it's cleared) — no contact list is consulted at all. This reuses the existing tested floor logic instead of introducing a new constant, and it naturally tightens to combat range exactly when a target is locked, per the original intent behind `TargetedFloorM`.
- **MANUAL mode is unchanged**: `-`/`=` still call `StepManualRange` from the current range, clamped to `[EffectiveFloorM(bHasSelectedTarget), MaxRangeM]`; **Home** returns to AUTO (which is now just "the floor," not a stored value).
- Drawing (`ProjectContact` for every contact, including the anchor's root star) is unaffected; only the number fed to it as `RangeM` changes.

### Appendix C — Amendment 4: post-review fixes (2026-09-29)

1. **`ProjectContact` whole-vector scaling (replaces separate lateral/height clamping).** Let `v` be the contact's position in the ship-local frame (X forward, Y right, Z up) and `d = |v|`. If `d > RangeM`, scale the WHOLE vector together: `v' = v * (RangeM / d)`, then derive `ScreenOffsetUnit` and `HeightStalkUnit` from `v'` exactly as before (lateral = `v'.X`/`v'.Y` through the existing `r(t)` collapse law with `t = |lateral(v')| / RangeM`, height = `v'.Z / RangeM` scaled by the same `r`). If `d <= RangeM`, behavior is unchanged (the existing collapse law still applies for near contacts). This keeps a contact's on-screen direction geometrically correct instead of pinning height and lateral offset to independent maximums. `SOLTest.RadarLayout.ProjectHeightStalk` and `ProjectRangeEdgeAndSaturation` are updated test-first for this.
2. **Speed-stepper unit overflow (fixes a real bug, not a design change).** `CycleUnit` must refuse to switch to a unit whose converted value would exceed the stepper's maximum representable value (currently bounded by `MaxDigit`); on refusal it leaves `State` unchanged. Additionally, `MaxDigit` is raised from 6 to 8 (representable up to ~999,999,999 in the current unit) so that 0.5c (~1.498e8 m/s) is representable in the m/s unit without overflow. `SOLTest.SpeedStepper.CycleUnitForward`/`CycleUnitBackward` gain a case at high speed (near 0.5c) confirming no digit corruption, done test-first.
3. Engine-integration-only fixes (no contract change, fixed directly, test-after where trivial): radar scope layout margin now accounts for the maximum stalk length so nothing runs off-screen; the near-plane orbit-line clip divides by the clipped segment instead of the raw endpoints (removes a possible NaN); a zoom key (`-`/`=`) that would leave the range exactly at the current AUTO floor no longer switches to MANUAL; `GAME_MECHANICS.md`'s radar row is corrected to match the actual "10000.0 km" label format; the HUD's inline distance formatter is replaced with a shared, tested `SOLHudFormat` append helper instead of a second hand-written copy; draw order changed so the reticle/joystick indicator no longer paints over the target bracket and prograde markers; long lines reflowed to the style guide's soft 120-column limit.

### Appendix C — Amendment 4 clarifications (settled after the test author's ambiguity report)

- **Whole-vector scaling, precise formula:** let `v` be the ship-local contact vector and `d = |v|`. Define `v' = d <= RangeM ? v : v * (RangeM / d)` (so `|v'| <= RangeM` always, with equality when the original was beyond range). Then, using `v'` in place of `v` throughout: lateral magnitude `t_lat = |lateral(v')| / RangeM`, `ScreenOffsetUnit = r(t_lat) * unitDirection(lateral(v'))` (zero if the lateral component is zero); height `t_h = |v'.Z| / RangeM`, `HeightStalkUnit = sign(v'.Z) * r(t_h)`, both using the same `r(t) = t < CollapseFraction ? sqrt(t * CollapseFraction) : t` from Amendment 3's predecessor (Appendix C's original collapse law). This applies uniformly whether the original contact was within or beyond range — a within-range contact has `v' == v` and behaves exactly as before; the pure-axis regression cases are unaffected by construction, matching what the test author already found.
- **`CanCycleUnit` / `CycleUnit` overflow threshold:** refuse (return `false` / leave `State` unchanged) exactly when converting `State`'s current mps into the target unit would give an absolute value `>= 1e9` (`10^(MaxDigit+1)`, i.e. an integer part of 9 or more digits). `999,999,999.x` in the target unit is allowed. `CycleUnit` does not separately clamp to `SOL::MAX_SPEED_CAP_MPS`; that clamp already happens in `StepDigit` and `FromSpeedMps`, and normal gameplay values stay well under the 1e9 overflow threshold regardless of unit.
