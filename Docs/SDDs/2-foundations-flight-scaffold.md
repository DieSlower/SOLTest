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
- **Targetables:** an interface (`ISOLTargetable`-style) with position, velocity, name, radius; the body registry exposes bodies through it.
- **Testing:** logic is unit-tested with UE automation tests (Kepler accuracy, clock warp math, anchor hysteresis, flight ramp, gravity, quaternion rotation stays gimbal-free, target selection, collision clamp); visual behavior gets a PIE smoke check when the editor is open. Tests are authored by a separate subagent from the contract only.

## 4. Open questions

- Deferred from the 1a review (finding 10): the temporary debug HUD builds strings every frame; it is replaced by the 1c HUD, which must not allocate per frame.
- Deferred from the 1a review (finding 12): profiling evidence (`stat` / Unreal Insights) for the universe update and body visuals is gathered in 1c and Part 10.

Ship stat numbers not fixed above (exact thrust/accel values for Newtonian mode, boost multiplier, camera arm length) are chosen during 1b and recorded in `Docs/GAME_MECHANICS.md`.

## 5. Revision history

- 2026-09-28: initial SDD from the Part 1 grill.
- 2026-09-28: 1a engine integration: observer-relative rendering with our own render origin instead of `SetNewWorldOrigin` (section 3).
- 2026-09-28: 1a adversarial review: Amendment 1 implemented; per-body sun shading in the body material replaces the shared light for bodies; subsystems gated on `ASOLGameMode` (section 3); two findings deferred (section 4).
- 2026-09-28: 1b Mass ship: fragments, flight processor run from the anchor tick via `OnBodiesUpdated`, Unreal-handed body cache, ship subsystem as observer, `-SOLSmokeFlight` (section 3).
- 2026-09-28: 1b pawn: `ASOLShipPawn` (C++ Enhanced Input, virtual joystick, chase camera, placeholder mesh), `USOLTargetingSubsystem` and `ISOLTargetable`, reference velocity moved from the ship subsystem to targeting, `-SOLSmokeInput` (section 3).
- 2026-09-28: 1b adversarial review: Amendment 2 implemented (warp carry incl. a velocity carry, shared hitch budget, interpolated substep bodies, swept collision with broadphase, const shared params, parallel chunk loop, focus loss, manual camera lag, fill light), smoke flight extended with warp phases (section 3).

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
