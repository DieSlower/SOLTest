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
| Commits | The user allowed commits for this session: one commit per sub-part (1a, 1b, 1c) after build, tests and adversarial review pass. Commit messages start `Issue #2:`. No push unless the user says so. |

## 3. Design detail (Claude's choices within the decisions above)

- **Sub-parts:** 1a foundations (universe, clock, registry, anchoring, body visuals, test map); 1b ship (Mass entity, Pawn proxy, flight model, gravity, collision, camera, input, targeting); 1c HUD (all HUD elements and the F3 panel). Each is built by its own subagent under the workflow in `CLAUDE.md`.
- **1a modules under `Source/SOLTest/`:** `Universe/` for double-precision position/velocity types, Keplerian elements and solver (position and velocity), the sim-clock subsystem, the data-oriented body registry (structure-of-arrays, parent index for later moons), and the anchor subsystem (nearest body with 25% hysteresis per SDD 1). Bodies far beyond UE precision are drawn with angular-size-preserving projection. Origin rebasing uses UE's built-in world-origin rebasing where practical; if not suitable, camera-relative placement with the reason documented.
- **1a rendering and rebasing (as implemented):** rendering is **observer-relative with our own render origin**, not UE's built-in origin rebasing. `USOLAnchorSubsystem` holds the observer's authoritative universe position (double, meters) and a *render origin* (a universe point that maps to Unreal (0,0,0)). The render origin (`FSOLRenderOrigin`) snaps to the observer whenever the anchor body changes (25% hysteresis) or the observer drifts `SOL::RENDER_REBASE_DISTANCE_M` (10 km) or more from it; the observer actor is moved back to (0,0,0) and `OnRenderOriginShifted` broadcasts the shift for any other listener. Every frame, `ASOLBodyVisuals` re-places each body with `SOLRender::ComputePlacement` relative to the observer (exact 1:1 within `SOL::DEFAULT_MAX_RENDER_DISTANCE_CM` = 1,000,000 km, otherwise depth-compressed on a log scale with angular size preserved, per Amendment 1), converted with `EclipticToUnreal`; positions stay `FVector3d` until that last step. `UWorld::SetNewWorldOrigin` was rejected because its origin is an `FIntVector` in int32 centimeters (about ±21,000 km), far too small for AU-scale anchors, it is a World Composition-era feature, and our bodies are re-placed from the universe every frame anyway, so shifting every actor through the engine buys nothing. The anchor still drives rebasing, as SDD 1 requires, but the render origin is the observer's position at that moment rather than the anchor body's center, which can be ~10^9 km away. One tickable subsystem (`USOLAnchorSubsystem`) drives the frame in a fixed order: clock, registry, anchor/rebase, then `OnUniverseUpdated` for the visuals. The clock and registry subsystems do not tick themselves. All three subsystems exist only in Game/PIE worlds whose game mode (the map's override, else the project default) is `ASOLGameMode`. The sim clock stops when the game is paused, because tickable subsystems do not tick while paused. Bodies shade themselves: `/Game/SOL/Materials/M_SOLBody` is an unlit parametric material (two colors, latitude bands, noise patches, polar caps, emissive) whose emissive output is `albedo * (saturate(N . SunDirection) + AmbientLight) * SunIlluminance / pi + EmissiveColor`, and `ASOLBodyVisuals` sets each body's `SunDirection` (unit vector body to Sun, Unreal axes) every frame through a cached parameter index, so every body's phase is correct wherever it is relative to the observer. A shadowless directional light re-aimed from the Sun toward the observer is kept for lit objects near the observer (the 1b ship); the body material ignores it. Exposure is fixed (manual) with bloom. The material is generated headless by `Tools/CreateSOLContent.py`, which also creates `/Game/Maps/SOL_Test`. The debug spectator accepts `-SOLStart=<Body>`, `-SOLAltitudeKm=<km>` and `-SOLLookAt=<Body>`, and the game mode accepts `-SOLSmokeShot=<s>`, which takes a screenshot and then quits, for headless smoke runs.
- **1b:** the player ship is a Mass entity (transform, velocity, orientation quaternion, angular velocity, ship parameters as fragments); processors for flight and gravity. `ASOLShipPawn` is a thin proxy owning input (Enhanced Input, created in C++), the spring-arm camera and the placeholder ship mesh, and writes control state into the entity. Collision is analytic against the body registry, with no physics-engine bodies for planets.
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
