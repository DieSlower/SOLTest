<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 3 — Part 2: Jump map (J)

Issue `#3`. Refines [`1-solar-system-architecture.md`](1-solar-system-architecture.md); plan of record: [`../Plans/3-jump-map-plan.md`](../Plans/3-jump-map-plan.md). Decisions in section 2 were resolved with the user in a `grill-me` session on 2026-09-29.

---

## 1. Problem

Let the player open a zoomable map of the solar system (J), pick a destination anywhere in SOL — a body or a free point in space — with enough precision to place it accurately, and jump there with a short animated warp, arriving with velocity matched to the destination's reference body (per SDD 1).

## 2. Decisions (user-resolved)

| Area | Decision |
|---|---|
| Map style | A genuinely navigable 3D tactical view in the same world (Homeworld-style sensors-manager feel), not a separate 2D schematic. |
| Camera | J releases the mouse cursor. **Right-drag** orbits the camera around the current focus point; **middle-drag** (or Shift+right-drag) pans the focus; mouse wheel zooms on a log scale from system-wide down to a single body's neighborhood. **Left** is reserved for the destination-picking gesture (section 3), matching Homeworld's own left-orders/right-orbits split. (Resolved 2026-09-30: an earlier draft of this row said "left-drag orbits," which directly contradicted section 3's left-mouse-down destination gesture; right/middle for camera control is the fix.) |
| Body representation | Each body renders its real mesh when large enough on screen; below a pixel-size threshold it cross-fades to a billboarded icon (color/size-coded per body) with a name label, so every body stays visible and clickable at any zoom. |
| Time while mapped | The sim clock keeps advancing at whatever warp was active before J was pressed; bodies (and the ship, coasting under its last control state) keep moving live while the map is open. |
| Destination picking | Homeworld-style two-stage gesture, described precisely in section 3. |
| Confirm / cancel | Enter (or an on-screen "Jump" button) executes the jump using the destination's LIVE position at that instant and closes the map to play the warp sequence. Esc or J again closes the map without jumping. X clears the current destination pick. |
| Jump execution | Per SDD 1: ~2-4 s animated warp (FX + sound), arrival with velocity matched to the destination's reference (the selected target if one was picked, otherwise the body nearest the free-space destination — reuse the existing anchor logic), origin re-anchored on arrival. |

## 3. Destination-picking mechanic (precise, user-specified)

Let `R(t)` be the **reference point**: the ship's live universe position if no body was clicked, or the clicked body's live universe position if one was. All positions are in the ecliptic (universe) frame; `Up` = ecliptic north `(0,0,1)`.

1. **Mouse-down** on the map: if it lands on a body's mesh/icon (within its pick radius), that body becomes the reference (highlighted) and `R(t)` tracks it from then on. Otherwise the reference is the ship, and `R(t)` tracks the ship's live position.
2. **Drag (XY):** while the button is held, a circular disc is drawn in the plane through `R(t)` with normal `Up`, centered on `R(t)`'s screen projection, radius equal to the current drag distance (projected onto that plane) — this is the "direction and range" visual. The live planar offset is `(dx, dy)` = the ecliptic X/Y components of `(RayPlaneIntersection(cameraRay, planeThrough R(t), normal Up) − R(t))`.
3. **Mouse-up:** locks `(dx, dy)`. The destination so far is `R(t) + (dx, dy, 0)` in ecliptic axes.
4. **Hold Shift + move the mouse** (no button needed): live-previews a height offset `dz` along `Up`, visualized with a vertical guide line at the locked XY point. The exact screen-to-`dz` mapping (which camera-facing vertical plane the mouse ray intersects) is a visual-feel detail tuned during implementation, not pre-specified here — it must feel proportional and reversible (moving the mouse back exactly retraces `dz`), scale sensibly with the current map zoom level, and never produce NaN/instability.
5. **Click while still holding Shift:** locks `dz`. The full destination is `R(t) + (dx, dy, dz)` in ecliptic axes, tracked live if `R(t)` moves (target or ship).
6. **Enter:** executes the jump to the destination's position at that instant. **X:** clears the pick and returns to step 1. **Esc / J:** closes the map with no jump.

## 4. Appendix D — Part 2 pure-logic API contract

Plain C++ (no UObjects), in `Source/SOLTest/Map/`.

### `Map/SOLMapPicking.h`

```cpp
struct FSOLMapPickState
{
    bool bHasReference = false;       // false until the first mouse-down
    bool bReferenceIsBody = false;    // true if the reference is a clicked body, false if it's the ship
    int32 ReferenceBodyIndex = INDEX_NONE;   // valid only when bReferenceIsBody
    bool bPlanarLocked = false;       // true after mouse-up locks (dx, dy)
    double PlanarOffsetXM = 0.0;
    double PlanarOffsetYM = 0.0;
    bool bHeightLocked = false;       // true after the Shift-click locks dz
    double HeightOffsetZM = 0.0;
};

namespace SOLMapPicking
{
    // Intersects a ray with the plane {point: PlaneOrigin, normal: PlaneNormal}. Returns false (no intersection) if the ray is
    // parallel to the plane (|dot(RayDir, PlaneNormal)| below a small epsilon) or the intersection is behind the ray origin.
    bool RayPlaneIntersect(const FVector3d& RayOrigin, const FVector3d& RayDir, const FVector3d& PlaneOrigin,
                           const FVector3d& PlaneNormal, FVector3d& OutPoint);

    // Decomposes (Point - PlaneOrigin) into components along EclipticX/EclipticY (both unit, orthogonal to Up and to each other;
    // callers pass (1,0,0) and (0,1,0) in the ecliptic frame). Returns (dx, dy).
    FVector2D DecomposePlanarOffset(const FVector3d& Point, const FVector3d& PlaneOrigin, const FVector3d& EclipticX,
                                    const FVector3d& EclipticY);

    // Composes a locked pick back into an absolute universe-frame destination: ReferencePositionM + dx*EclipticX + dy*EclipticY +
    // dz*Up. Pure vector math; the caller supplies the reference's CURRENT (live) position.
    FVector3d ComposeDestination(const FVector3d& ReferencePositionM, double PlanarOffsetXM, double PlanarOffsetYM,
                                 double HeightOffsetZM, const FVector3d& EclipticX, const FVector3d& EclipticY,
                                 const FVector3d& Up);

    // Finds the body whose screen-space pick radius (a sphere of PickRadiusM around its position, tested for ray intersection)
    // is hit by the ray, nearest one wins if several overlap; INDEX_NONE if none. Bodies array is parallel: positions and radii.
    int32 PickBodyUnderRay(const FVector3d& RayOrigin, const FVector3d& RayDir, TConstArrayView<FVector3d> BodyPositionsM,
                           TConstArrayView<double> BodyPickRadiiM);
}
```

### Non-unit-tested Part 2 pieces

The map camera (orbit/pan/zoom controller, free mouse cursor handling), the mesh-to-icon cross-fade and billboard/label rendering, the disc and vertical-guide-line visualization, the exact camera-facing vertical plane used for the Shift height drag, the destination marker, the warp animation (FX/sound/camera work per SDD 1), and wiring the map's chosen destination into the existing jump/arrival logic. These get PIE smoke checks and screenshots, iterated the way Part 1's HUD was.

## 5. Open questions

None deferred; the pick radius per body, disc/guide-line visuals, and warp FX specifics are chosen during implementation and recorded in `Docs/GAME_MECHANICS.md`.

## 6. Revision history

- 2026-09-29: initial SDD from the Part 2 grill.
- 2026-09-30: sub-part 2b-2 map mode as built (Appendix E, "Map mode design"): render-viewpoint override in `USOLAnchorSubsystem`, wheel forward = zoom in.
- 2026-09-30: sub-part 2c body icons, labels and pick radius as built (Appendix G, "Non-unit-tested Part 2c pieces").
- 2026-09-30: sub-part 2d destination picking as built (Appendix H, "Non-unit-tested Part 2d pieces"): the Shift height uses a frozen distance-proportional pixel scale instead of a camera-facing plane.

### Appendix D clarifications (settled after the test author's ambiguity report)

- **Ray semantics:** `RayDir` is a unit vector in all four functions (callers normalize before calling). A "ray" is a half-line: only intersections at `t >= 0` count, matching `RayPlaneIntersect`'s existing behavior. An origin already on the plane, or already inside a pick sphere, counts as a hit at `t == 0`.
- **`RayPlaneIntersect`:** the plane is two-sided (a flipped or non-unit `PlaneNormal` gives the same hit point, since `t`'s scale cancels between numerator and denominator). The parallel-ray epsilon is `1e-9` on `dot(RayDir, normalize(PlaneNormal))`.
- **`PickBodyUnderRay` "nearest":** nearest means the smallest `t` (entry distance from `RayOrigin` along the ray to where the ray enters the body's pick sphere), not center distance. A body whose center is behind the ray origin is never picked, even if the infinite line through the ray would pass within its radius.
- **Precision requirement (binding on the implementation, not just the tests):** every function must compute by first taking differences between large solar-system-scale coordinates (e.g. `PlaneOrigin - RayOrigin`, `Point - PlaneOrigin`) and only then dotting/scaling the resulting small relative vectors — never dot two raw ~1e11-magnitude absolute vectors directly, which would lose precision. This is required to hit the sub-millimeter accuracy the tests check at solar-system-scale coordinates.

### Appendix D clarifications, addendum (resolves a review-found ordering conflict)

- **`PickBodyUnderRay` rule ordering:** "origin already inside a pick sphere counts as a hit at `t == 0`" takes priority over "a body centered behind the ray origin is never picked." Concretely, per body: first test whether `RayOrigin` is inside the pick sphere (`|BodyPosition - RayOrigin| < PickRadius`) — if so, that body is a candidate at `t = 0`, regardless of where its center is relative to the ray direction. Only when the origin is NOT already inside a body's sphere does the "center behind the origin excludes it" rule apply to that body.
- **NaN/Inf inputs:** every function must be explicitly non-crashing and never return a hit/point built from a non-finite input. Concretely: check `RayDir`, `RayOrigin`, `PlaneOrigin`, `PlaneNormal` (and, per body, position/radius) for `FMath::IsNaN`/non-finite components at the top of each function, and return `false`/`INDEX_NONE` immediately if any are non-finite. Do not rely on NaN's comparison semantics (`NaN < x` and `NaN >= x` are both false) to reject bad input implicitly — that pattern is what let a NaN through previously; make the rejection explicit.
- **Non-unit `PlaneNormal`:** any nonzero normal (however small) must work, matching "a flipped or scaled normal gives the same hit." Only a normal that is exactly zero (within double-precision machine epsilon, not a coarse tolerance like `1e-8` on the squared length) is invalid. Do not use a helper whose default zero-tolerance would reject a small-but-nonzero, non-unit normal.

### Appendix D clarifications, addendum 2 (implementation note)

`PickBodyUnderRay` skips an individual non-finite or negative-radius body rather than failing the whole query when it encounters one; only non-finite ray inputs (`RayOrigin`/`RayDir`) cause an immediate `INDEX_NONE`. This is a robustness choice made during implementation, not a change to the contract's happy-path behavior.

## 7. Appendix E — Part 2b map camera API contract

Plain C++ (no UObjects), in `Source/SOLTest/Map/SOLMapCamera.h`. Universe frame throughout (ecliptic, meters); `Up = (0,0,1)`.

```cpp
struct FSOLOrbitCameraState
{
    FVector3d FocusPositionM = FVector3d::ZeroVector;   // the point the camera orbits
    double YawRad = 0.0;      // rotation around Up; wrapped to (-PI, PI]
    double PitchRad = 0.5;    // tilt above/below the horizontal plane; clamped (positive = above the ecliptic, looking down)
    double DistanceM = 1.0e10;
};

struct FSOLOrbitCameraParams
{
    double MinPitchRad = -1.5;      // ~-85.9 deg, stops short of the pole to avoid a flip
    double MaxPitchRad = 1.5;
    double MinDistanceM = 1.0e6;    // 1,000 km
    double MaxDistanceM = 6.0e13;   // well beyond Neptune's orbit
    double ZoomStepFactor = 1.2;    // per wheel notch, gentler than the HUD radar's 10x since this is continuous
};

namespace SOLMapCamera
{
    // Spherical position around the focus: at Yaw=0, Pitch=0 the camera sits at FocusPositionM + DistanceM * (-1,0,0) (looking
    // toward +EclipticX). Yaw rotates that offset around Up; Pitch tilts it up/down from the horizontal (Yaw=0,Pitch=0) plane.
    FVector3d ComputeCameraPositionM(const FSOLOrbitCameraState& State);

    // Look-at orientation: forward = normalize(FocusPositionM - ComputeCameraPositionM(State)), up = Up re-orthogonalized against
    // forward (standard look-at), both converted with SOLRender::EclipticToUnreal before building the FQuat4d (Unreal-handed).
    FQuat4d ComputeCameraOrientation(const FSOLOrbitCameraState& State);

    // Adds YawDeltaRad/PitchDeltaRad (already converted from screen pixels by the caller) to the state; Yaw wraps to (-PI, PI],
    // Pitch clamps to [MinPitchRad, MaxPitchRad]. FocusPositionM and DistanceM are unchanged.
    FSOLOrbitCameraState ApplyOrbitDelta(const FSOLOrbitCameraState& State, double YawDeltaRad, double PitchDeltaRad,
                                        const FSOLOrbitCameraParams& Params);

    // Moves FocusPositionM by PanDeltaRightM/PanDeltaUpM (meters, already zoom-scaled by the caller) along the CURRENT camera's
    // screen-relative right/up directions (right = derived from Yaw only, i.e. as if Pitch were 0; up = Up rotated the same way as
    // right so the pair stays orthogonal). Yaw/Pitch/DistanceM are unchanged.
    FSOLOrbitCameraState ApplyPan(const FSOLOrbitCameraState& State, double PanDeltaRightM, double PanDeltaUpM);

    // Multiplies DistanceM by ZoomStepFactor^WheelSteps (negative WheelSteps zooms in), clamped to [MinDistanceM, MaxDistanceM].
    FSOLOrbitCameraState ApplyZoom(const FSOLOrbitCameraState& State, int32 WheelSteps, const FSOLOrbitCameraParams& Params);
}
```

### Non-unit-tested Part 2b pieces

The map mode/subsystem (J toggles it, releases the mouse cursor, suppresses ship control input the same way the F3 panel does, does NOT pause the sim clock), the map's dedicated camera actor/component driven by `FSOLOrbitCameraState`, mouse-drag-to-orbit/pan input wiring and pixel-to-radian/meter scaling, and Esc/J to close. These get a PIE smoke check and screenshots.

### Appendix E clarifications (settled after the test author's ambiguity report)

- **Default pitch bug fix:** `FSOLOrbitCameraState::PitchRad`'s default is `+0.5`, not `-0.5` as first written — "positive pitch = above the ecliptic" means the default must be positive so the map opens looking down at the system from above, not up at it from below.
- **Yaw rotation direction:** positive `YawRad` rotates the camera's offset from the focus by the standard right-hand rule around `Up = (0,0,1)` (i.e. the same sense as a standard `(x,y) -> (x*cos-y*sin, x*sin+y*cos)` rotation of the offset's ecliptic X/Y components).
- **Pitch convention, precisely:** at `Yaw=0`, the offset from focus to camera is `Distance * (-cos(Pitch), 0, sin(Pitch))` in ecliptic axes (so `Pitch=0` gives `(-1,0,0)` as already specified, and increasing `Pitch` swings the camera up toward `+Up`). At nonzero Yaw, this offset is then rotated around `Up` by `Yaw` per the rule above.
- **Pan axes at `Yaw=0, Pitch=0`:** "right" = `(0,-1,0)` in ecliptic axes (`cross(Forward=(1,0,0), Up=(0,0,1))`, which is Unreal's `+Y` after `EclipticToUnreal`); "up" = `Up = (0,0,1)`, unaffected by Pitch. At nonzero Yaw, both "right" and this pan-up are rotated around `Up` by `Yaw` the same way the camera offset is (pan-up therefore equals `Up` at every Yaw, since `Up` is the rotation axis).
- **Pitch pole (`|PitchRad|` near `PI/2`):** `ComputeCameraOrientation`'s look-at is degenerate there (forward parallel to `Up`). No specific behavior is required beyond not producing NaN; this is intentionally untested at the exact pole, matching how the flight camera's own pole cases are handled elsewhere in the project.
- **Out-of-range input state:** if a caller constructs `FSOLOrbitCameraState` with `PitchRad`/`DistanceM` already outside `FSOLOrbitCameraParams`'s bounds and then calls a delta function with a zero delta, the function is not required to re-clamp; clamping only happens when a nonzero delta is actually applied. Not tested.

### Appendix E — Amendment 1: Homeworld-style ground-plane panning (2026-09-29, post-review)

Replaces `ApplyPan`'s vertical-drag behavior. In Homeworld's tactical camera, panning always slides the focus across the map's horizontal plane, regardless of the camera's current tilt — a vertical screen drag moves you forward/back across the plane, not up/down into space, so panning stays intuitive at any pitch including a top-down view.

`ApplyPan(state, panDeltaRightM, panDeltaUpM)`: both pan directions are derived from **Yaw only** (as already specified) and lie **in the ecliptic plane** (Z-component zero) — neither is affected by Pitch, and neither moves the focus along `Up`:
- "right" (unchanged): the Yaw=0 vector `(0,-1,0)`, rotated around `Up` by the current Yaw.
- "forward-horizontal" (replaces the old "up" = `Up`): the Yaw=0 vector `(1,0,0)` (the ecliptic-plane projection of the Yaw=0/Pitch=0 forward direction), rotated around `Up` by the current Yaw the same way.

`FocusPositionM += right * panDeltaRightM + forwardHorizontal * panDeltaUpM`. The parameter named `panDeltaUpM` (a vertical screen-drag amount) now drives a horizontal forward/backward move, not a height change — there is deliberately no way to move the focus along `Up` at all; only `ApplyOrbitDelta`'s Pitch changes how the SAME horizontal point is viewed. `Yaw`/`Pitch`/`DistanceM` are unchanged by `ApplyPan`, as before.

### Map mode design (sub-part 2b-2, as built)

- **Mode owner, split by responsibility.** `USOLMapModeSubsystem` (`Map/SOLMapModeSubsystem`, a `UWorldSubsystem` gated by `ASOLGameMode::IsSOLGameWorld` like every SOL subsystem) owns the map state: the `FSOLOrbitCameraState` / `FSOLOrbitCameraParams`, the camera actor, the view-target swap and the render-viewpoint override. `ASOLShipPawn` owns the input side, exactly as it already owns the F3 panel: J is in the ship's mapping context; opening (deferred to the pawn's next tick, outside the Enhanced Input callback) runs the same `SuspendShipControl` as F3 (keys flushed, ship mapping removed, thrust/roll/boost/stick zeroed; the Mass step, sim clock and warp keep running), adds a separate `IMC_Map` (J / Esc close, right mouse = orbit button, middle mouse or Shift + right mouse = pan button, `Mouse2D` delta, wheel; the left mouse button is deliberately unmapped, reserved for 2d's destination-picking gesture) and switches to game-and-UI input with a visible, unlocked cursor (the viewport captures and hides it only while a button is held, so drags produce deltas). Closing removes `IMC_Map` and runs `RestoreShipControl` (the F3 close path: ship mapping, hidden captured cursor, game-only input). The pawn forwards drags and wheel notches to the subsystem; 2c/2d read the camera state from the subsystem without going through the pawn.
- **Camera swap.** A plain engine `ACameraActor` (spawned once, transient, no tick, 60° FOV, no aspect constraint) becomes the controller's view target on open; the previous view target (the ship pawn) is restored on close. The ship's spring-arm chase camera is untouched.
- **Input scaling ("grab the scene").** Orbit: yaw -= dx x 0.005 rad/px, pitch -= dy x 0.005 rad/px (dy positive = mouse up). Pan: `ApplyPan(-dx x m, -dy x m)` with m = `DistanceM` x 0.0015 per pixel. Zoom: wheel forward (positive notches) zooms **in**, i.e. `ApplyZoom(-notches)`, one notch = one 1.2x step. The map opens with the focus on the ship's live position at that instant (not tracked afterwards), Yaw 0, Pitch 0.5, `DistanceM` = `SOL::MAP_DEFAULT_DISTANCE_M` (1e13 m).
- **Focus does not follow the ship (intentional, not a bug).** Once the map is open the focus is a fixed point the player orbits, pans and zooms around; it is set once (`FocusPositionM` = ship position when J is pressed) and only pan moves it afterwards. The ship keeps coasting on its last control state while the map is open (section 2, time keeps advancing), but the camera is never silently re-centered on it, exactly like Homeworld's tactical camera. Do not "fix" this by tracking the ship.
- **Render-origin integration (render viewpoint).** Placing the camera at `UniverseToRenderCm(cameraM)` with the render origin still on the ship would put it ~1e15 cm out (beyond Unreal's large-world bounds), and the bodies' far-field depth compression is radial from the observer, so they would be warped as seen from anywhere else. Instead `USOLAnchorSubsystem` gained a **render viewpoint**: `SetViewpointOverrideM` / `ClearViewpointOverride` / `GetViewpointM` (the observer unless overridden). The render origin rebases onto the viewpoint, and `ComputeBodyRenderPlacement` / `ComputePointRenderLocationCm` compress from it. The map subsystem applies queued input on `OnBodiesUpdated` and sets the viewpoint to the camera's universe position **before** the anchor's rebase, so in the same frame the origin sits at the camera, every body (and the HUD orbit ellipses and target bracket, which use the same placement) is placed with correct angles from the camera, and on `OnUniverseUpdated` the camera actor is placed at `UniverseToRenderCm(cameraM)` (within the 10 km rebase distance of Unreal's origin) with `ComputeCameraOrientation`. The observer (ship), anchor selection and hysteresis are unaffected. Closing clears the override and forces a snap back onto the ship at the next update. The ship pawn and the observer actor are placed through `ComputePointRenderLocationCm(observer)`, which is identical to the old `UniverseToRenderCm(observer)` in normal play and stays bounded while the map views from far away.
- **Body visuals.** `ASOLBodyVisuals` needed no change: it already placed bodies through `ComputeBodyRenderPlacement` on `OnUniverseUpdated` and never referenced the ship camera or pawn. The flight HUD hides the reticle, joystick and velocity markers while the map is open and shows the map's key hints.
- **Known limitation for 2c.** At the default 1e13 m every body is sub-pixel (smoke run: Sun 0.08 px radius, Jupiter 0.008 px, Earth 0.0007 px), so the default map view shows no bodies at all, only the HUD (and the orbit ellipses if O was on). Real meshes do render from the map camera when zoomed in (Earth ~110 px radius at 5e7 m). The icon/billboard fallback of 2c is required for the map to be usable at system scale (resolved in 2c, Appendix G).
- **Mouse buttons (settled 2026-09-30).** Right drag orbits, middle drag or Shift + right drag pans, and the left button is left unbound in `IMC_Map` so a left click/drag currently does nothing; 2d binds it to the destination-picking gesture (section 3 step 1; bound as of 2d, see Appendix H). The `-SOLSmokeMap` run checks all three (left drag leaves the camera unchanged, right drag orbits, middle and Shift + right drags pan).

### Appendix E, additional post-review fixes

1. **Yaw direction is untested (review finding #1).** Add a definitive test: at exactly `Yaw = +PI/2` starting from the Yaw=0 offset `(-1,0,0)*Distance`, the result must be `(0,-Distance,0)` (not `(0,+Distance,0)`) per the right-hand-rule clarification — pin the actual sign, not just its magnitude. Add the matching definitive pan-right test: at `Yaw = +PI/2`, `ApplyPan` with `panDeltaRightM = D` moves the focus by `(+D, 0, 0)`.
2. **Pole guard (review finding #3).** `FSOLOrbitCameraParams::MaxPitchRad`/`MinPitchRad` must stay strictly inside `(-PI/2, PI/2)` with enough margin that `ComputeCameraOrientation`'s up-vector re-orthogonalization never degenerates (the review found degeneracy starting around 1e-4 rad from the exact pole). Add a `static_assert` (or a runtime `check`) in `SOLMapCamera.cpp` enforcing `MaxPitchRad < PI/2 - 1e-3` and `MinPitchRad > -(PI/2 - 1e-3)` against the DEFAULT `FSOLOrbitCameraParams`, and document that a caller must not raise the limits past that margin.
3. **Style (review finding #4).** Reflow lines over the 120-column soft limit in `SOLMapCamera.h` and `MapCameraTest.cpp`.

## 8. Appendix F — viewpoint-resolution contract (post-review, 2b-2)

Extracted per the 2b-2 adversarial review: `USOLAnchorSubsystem`'s "which position does rendering use this frame, and does the origin need a forced snap" decision (introduced for the map's `SetViewpointOverrideM`/`ClearViewpointOverride`) becomes a small pure function, mirroring how `FSOLRenderOrigin` was already extracted from this same subsystem in Part 1a.

### `Universe/SOLViewpointResolve.h`

```cpp
struct FSOLViewpointResolveResult { FVector3d EffectivePositionM; bool bForceSnap = false; };

namespace SOLViewpoint
{
    // EffectivePositionM = OverridePositionM if bIsOverrideActiveThisFrame, else ObserverPositionM. bForceSnap is true
    // exactly when the override's on/off state changed since last frame (bWasOverrideActiveLastFrame !=
    // bIsOverrideActiveThisFrame) — a still-active override moving frame to frame does NOT force a snap; that case
    // is left to FSOLRenderOrigin's own drift-distance snap rule, unchanged.
    FSOLViewpointResolveResult Resolve(bool bWasOverrideActiveLastFrame, bool bIsOverrideActiveThisFrame,
                                       const FVector3d& OverridePositionM, const FVector3d& ObserverPositionM);
}
```

`USOLAnchorSubsystem` calls this once per frame instead of branching inline, and feeds `EffectivePositionM`/`bForceSnap` into `FSOLRenderOrigin::Update` exactly as it does today for the observer-only path.

### Other post-review fixes (2b-2), no new contract needed

- **Target bracket under a viewpoint override (review finding 2):** `ASOLFlightHud`'s target-bracket placement must use the same viewpoint-aware placement helper the ship and bodies already use (`ComputePointRenderLocationCm` against the CURRENT effective viewpoint), not a ship-relative 1:1 offset composed with the ship's own compressed placement. Latent today (no non-body `ISOLTargetable` is registered yet), but fix it now so it's correct before Part 6 adds one.
- **`IMC_Map` add-order (review finding 3):** add the map's input mapping context BEFORE suspending the ship's own control/mapping context, and if adding it fails (no local player/input subsystem), roll back (close the map) rather than leaving the player in a state with no mapping context bound at all, which would make the map unclosable.
- **Stale `OnRenderOriginShifted` broadcast (review finding 4):** a viewpoint-source transition (map open/close) produces a large, not-physically-meaningful "shift" on `OnRenderOriginShifted`. Document on the delegate that a shift caused by a viewpoint-source change (as opposed to normal anchor-driven rebasing) is not meaningful for anything that reacts to real spatial rebasing (e.g. a future pooled-effect position fixup), or suppress the broadcast specifically for a viewpoint-transition-caused snap. Pick whichever is simpler given the current delegate's callers (there are none yet).
- **Style (review finding 5):** reflow the two over-120-column lines in `SOLConstants.h` and `SOLFlightHud.h`; correct the 2b-2 revision-history date in this SDD to 2026-09-30 (the button rebind happened that day, not the 29th).

## 9. Appendix G — Part 2c body-representation API contract

Plain C++, in `Source/SOLTest/Map/SOLMapBodyLod.h`. Decides, per body per frame, whether the map draws its real mesh, a billboarded icon, or a cross-fade blend of both, based on apparent screen size — a body's mesh becomes visually useless (sub-pixel or a few pixels) at map zoom-out distances, per the 2b-2 smoke test's own finding (Earth's mesh was 0.0007 px at the 1e13 m default view).

```cpp
struct FSOLBodyLodParams
{
    double IconFullBelowPx = 4.0;     // apparent diameter at/under this: icon only, alpha = 1
    double MeshFullAbovePx = 32.0;    // apparent diameter at/over this: mesh only, alpha = 0
    double ViewportHeightPx = 720.0;  // caller's current viewport height, for the projection
    double VerticalFovRad = 1.0471975511965976;   // 60 deg, matches the map camera's FOV
};

struct FSOLBodyLodResult
{
    double ApparentDiameterPx = 0.0;
    double IconAlpha = 0.0;    // 0 = mesh only, 1 = icon only, linearly interpolated between the two thresholds
};

namespace SOLMapBodyLod
{
    // Apparent diameter in pixels of a sphere of radiusM at distanceM from the camera, given the params' viewport
    // height and vertical FOV:
    // ApparentDiameterPx = (2*radiusM/distanceM) * (ViewportHeightPx / (2*tan(VerticalFovRad/2))).
    // distanceM <= 0 or radiusM <= 0 returns 0 (no valid apparent size).
    SOLTEST_API double ComputeApparentDiameterPx(double radiusM, double distanceM, const FSOLBodyLodParams& params);

    // IconAlpha = 0 at/above MeshFullAbovePx, 1 at/below IconFullBelowPx, linearly interpolated in between
    // (clamped to [0,1] outside that range, e.g. for a degenerate ApparentDiameterPx of 0 -> alpha = 1).
    SOLTEST_API FSOLBodyLodResult Evaluate(double radiusM, double distanceM, const FSOLBodyLodParams& params);
}
```

### Non-unit-tested Part 2c pieces (design as built)

- **Where it lives.** `FSOLMapBodyOverlay` (`Map/SOLMapBodyOverlay.h/.cpp`, plain C++, no UObject), owned by `ASOLFlightHud` and drawn from `DrawHUD` only while the map is open, right after the orbit ellipses. It lives in `Map/` because it is the map's body representation; the HUD only hosts it because the Canvas is there. `ASOLBodyVisuals` is unchanged: the meshes are placed exactly as before.
- **LOD per frame.** For every body: `SOLMapBodyLod::Evaluate(realRadius, |bodyPos - mapCameraPos|, params)` with the default thresholds (4 px / 32 px), `ViewportHeightPx` = the canvas height and `VerticalFovRad` from the new `USOLMapModeSubsystem::GetVerticalFovRad(aspect)` (the camera component's FOV is horizontal, 60°, so it is converted for the canvas aspect). The distance is from the map camera (the effective viewpoint), never the ship.
- **Icons: Canvas discs, not billboard components.** Each body with `IconAlpha > 0` gets a flat disc (20-segment triangle fan from a unit-circle table built once at `Initialize`, plus a 1 px dark RING just outside it, so only the fill disc covers the mesh), all bodies in ONE translucent `FCanvasTriangleItem` batch (`GWhiteTexture`, `SE_BLEND_Translucent`, per-vertex colors), projected through the same render placement as the mesh (`ComputeBodyRenderPlacement`). Chosen over `UBillboardComponent`/material billboards because it needs no texture or material asset (the project builds everything in C++, and the one body material comes from `Tools/CreateSOLContent.py`), it matches the HUD's labels and orbit lines, and the opaque body material cannot fade anyway. Disc diameter = max(`MAP_ICON_DIAMETER_PX` (10 px at 720 p) x UI scale, apparent diameter); alpha = `IconAlpha`. **Cross-fade:** the disc is drawn over the opaque mesh and is never smaller than it, so what the player sees is `IconAlpha x disc + (1 - IconAlpha) x mesh` — a linear blend with no scale pop. At `IconAlpha == 1` the mesh is left visible (under 4 px, fully covered by the disc); at `IconAlpha == 0` nothing is drawn and the mesh looks exactly as in normal play. An icon whose screen center lies inside a NEARER body's apparent (mesh) radius is hidden, so a nearer mesh or icon wins (O(N^2) over the handful of bodies), and the icon batch is sorted back to front by camera distance so overlapping icons composite in order (2c-2 post-review fixes 1-2; smoke check `l-sun-occluded` looks at Earth from the anti-Sun side and requires the Sun's icon to be hidden).
- **Colors.** `SOLBodyAppearance::FindBaseColor(name, out)` (new, declared in `Visuals/SOLBodyVisuals.h`) returns the body material's linear `ColorA` from `ASOLBodyVisuals`'s appearance table (single source of truth); the overlay scales it so its brightest channel is 1 (hue kept, dark bodies like Earth stay visible). Earth reads blue, Mars orange, Neptune deep blue, Uranus pale cyan, Jupiter tan, Saturn/Venus cream, Mercury near-white, the Sun pale yellow-white. Bodies without an appearance get white.
- **Labels.** Small engine font, HUD text color, shadowed, scaled by the UI scale, alpha = `IconAlpha`. One `FText` per body built at `Initialize`, label sizes measured once; no per-frame string work. Only icons with `IconAlpha > 0.05` get a label or block label space (post-review fix 5). Placement tries right, left, above, below the disc and takes the first spot overlapping no disc and no earlier label; bodies are processed by descending radius, so in a tight cluster (the inner system at 1e13 m) the smaller bodies' labels are hidden until the player zooms in (smoke run: 9 icons, 8 labels at the default view).
- **Pick radius.** `SOLMapPickRadius::ComputeMapPickRadiusM(double bodyRadiusM, double distanceM, double minPickPx, double viewportHeightPx, double verticalFovRad)` (`Map/SOLMapPickRadius.h`) returns max(real radius, the world radius spanning `minPickPx` on screen at that distance), derived from `SOLMapBodyLod::ComputeApparentDiameterPx` so both share one projection; invalid inputs fall back to the real radius. The overlay already fills a per-body array with it each frame (`MAP_MIN_PICK_RADIUS_PX` = 12 px x UI scale), exposed as `GetPickRadiiM()` (reachable through `ASOLFlightHud::GetMapBodyOverlay()`), ready to pass to `PickBodyUnderRay` in 2d. No clicking is wired yet (2d).
- **Verification.** `-SOLSmokeMap` now takes map screenshots at 1e13 m (every body an icon + label), ~8.7e10 m (inner system), ~7.6e8 m (Earth ~18 px, `IconAlpha` ~0.49: mesh visible through a half-transparent disc) and ~5e7 m (Earth mesh only), with PASS/FAIL checks on the overlay's icon alphas and draw counts. A mid-fade body cannot be framed at 1e11-1e12 m from the default focus (Earth crosses the band at ~4e8-3e9 m; the Sun cannot get closer than ~1.5e11 m while the focus is on Earth), hence the ~7.6e8 m cross-fade shot. The pick-radius helper and `FindBaseColor` are covered by `Tests/MapPickRadiusTest.cpp` and `Tests/BodyAppearanceTest.cpp` (post-review fix 4). A ~7.6e8 m anti-Sun screenshot (Sun behind Earth) follows the cross-fade shot.

### Appendix G clarifications (settled after the test author's ambiguity report)

- **Inverted or equal thresholds** (`MeshFullAbovePx <= IconFullBelowPx`, including the degenerate equal case that would divide by zero): treat `IconFullBelowPx` as a single hard cutoff instead of interpolating — `IconAlpha = ApparentDiameterPx < IconFullBelowPx ? 1.0 : 0.0`. This avoids the division by zero and keeps the result always finite and in `[0,1]`; it is not expected to occur with sane params (the defaults satisfy `MeshFullAbovePx > IconFullBelowPx`).
- **Value exactly at a threshold:** `IconAlpha == 1.0` at `ApparentDiameterPx == IconFullBelowPx` (matches "at/below"), `IconAlpha == 0.0` at `ApparentDiameterPx == MeshFullAbovePx` (matches "at/over").
- **NaN/Inf inputs:** not required to be handled specially; this is a non-safety-critical visual LOD helper (unlike the map-picking/collision math), so undefined output for garbage input is acceptable as long as it doesn't crash.

### Appendix G clarifications, addendum (resolves review finding #2)

The equal-threshold hard-cutoff rule (`MeshFullAbovePx <= IconFullBelowPx`) OVERRIDES the general at-threshold rule from the first clarification for that degenerate case specifically: at `ApparentDiameterPx == IconFullBelowPx == MeshFullAbovePx`, `IconAlpha == 0.0` (matches the cutoff's `< IconFullBelowPx ? 1 : 0`), not `1.0`. The general "1.0 at/below IconFullBelowPx, 0.0 at/above MeshFullAbovePx" wording only applies when `MeshFullAbovePx > IconFullBelowPx`.

### Appendix G, post-review fixes (2c-2, 2026-09-30)

1. **Icon-over-nearer-mesh occlusion (finding 1).** Before adding a body's icon/label to the draw batch, skip it if its screen-space center falls within any NEARER body's apparent on-screen radius (O(N x M) over the small body count; a body is "nearer" if its distance from the viewpoint is less). Separately, sort the disc batch back-to-front by distance from the viewpoint before appending triangles, so overlapping discs (not mesh-vs-icon, but icon-vs-icon) composite correctly.
2. **Rim breaks the linear cross-fade (finding 2).** The rim must be a RING between radius `r` and `r + RimWidthPx`, not a second full disc drawn under the fill disc (a full under-disc double-darkens the blend, per the review's math). Redraw the rim as an annulus (reuse the same triangle-fan approach, just with an inner and outer radius) so the fill disc's alpha alone determines the mesh/icon blend, matching the SDD's stated `alpha*disc + (1-alpha)*mesh`.
3. **Pick-radius vs. nearest-hit conflict (finding 3) — belongs to 2d, not 2c.** Note added to the 2d plan step: `PickBodyUnderRay`'s "nearest by entry distance" rule can pick a farther-but-closer-to-camera body over a nearer-on-screen one when pick spheres overlap (e.g. Mercury near the Sun at wide zoom). 2d must resolve ties (or near-ties) among multiple hits by screen-space distance from the cursor to each body's icon center, not by ray entry distance alone.
4. **Missing tests (finding 4).** `SOLMapPickRadius::ComputeMapPickRadiusM` and `SOLBodyAppearance::FindBaseColor` need automation tests per `CLAUDE.md`'s testing rule, added test-after since both are already-correct, already-reviewed pure/near-pure functions (this is closing a testing gap, not introducing new behavior).
5. **Near-invisible disc still blocks label space (finding 5).** Only reserve label-placement space for a body whose `IconAlpha` is above a small visibility threshold (e.g. `> 0.05`), not any nonzero value.
6. **Perf/style (findings 6-7).** Precompute a small unit-circle table (e.g. 20 points) for disc/ring triangle generation instead of calling `sin`/`cos` 40 times per body per frame; reflow the over-120-column line in `SOLFlightHud.h`. The O(N^2) label-declutter check is fine at today's body count and is already noted as a scale concern for later (moons/asteroids), not something to fix now.

## 10. Appendix H — Part 2d screen-space body pick

Resolves the 2c-2 review's finding 3 (overlapping pick spheres at wide zoom letting a nearer-to-camera-but-farther-on-screen body win over a closer-on-screen one). For picking WHICH BODY is under the cursor when starting the destination gesture (section 3 step 1), the map uses a screen-space test instead of `SOLMapPicking::PickBodyUnderRay`'s 3D ray/sphere test — it naturally reuses the screen positions and pick radii `SOLMapBodyOverlay` already computes every frame for icon/label drawing.

### `Map/SOLMapScreenPick.h`

```cpp
struct FSOLScreenPickCandidate { int32 BodyIndex = INDEX_NONE; FVector2D ScreenPositionPx = FVector2D::ZeroVector; double PickRadiusPx = 0.0; };

namespace SOLMapScreenPick
{
    // Among candidates whose ScreenPositionPx is within PickRadiusPx of CursorPx (Euclidean, in pixels), returns the
    // one with the SMALLEST cursor-to-center distance (not the smallest PickRadiusPx, not any 3D/ray measure).
    // Returns INDEX_NONE if no candidate qualifies. A candidate with PickRadiusPx <= 0 never qualifies.
    int32 PickNearestOnScreen(TConstArrayView<FSOLScreenPickCandidate> Candidates, const FVector2D& CursorPx);
}
```

Empty-space picking (mouse-down that hits no body) still uses `SOLMapPicking::RayPlaneIntersect` against the ecliptic plane through the ship, exactly as section 3 specifies — this screen-space pick only replaces the "is the cursor over a body" test.

### Non-unit-tested Part 2d pieces (design as built)

- **State owner.** `USOLMapModeSubsystem` owns the pick, next to the camera state it already owned: the `FSOLMapPickState`, the reference's live position, the live planar and height previews, and the frozen height scale. The pick is recomputed on `OnUniverseUpdated` (body positions and the camera final, before the HUD draws), always from the reference's CURRENT position, so a locked pick rides along with a moving body or the coasting ship. `OpenMap` and `CloseMap` (Esc / J) both reset it, so nothing carries over to the next time the map opens. `GetLiveDestinationM(out)` is the hook for 2e: it returns true once the planar offset is locked and gives `ComposeDestination(liveReference, dx, dy, lockedDz or 0)`. `RequestJump()` (Enter) currently only logs that position to `LogSOL` and leaves the map open; 2e replaces its body with the close-and-warp.
- **Input (pawn side).** `IMC_Map` gained the left mouse button (press/release), X (clear) and Enter (jump). Shift is the existing pan-modifier action, forwarded to the subsystem as the height modifier as well. The ship's X (clear target) is in the ship context, which is removed while the map is open, so there is no conflict. The pawn keeps a map cursor: the OS mouse position while no map button is held; during a left drag, the position at mouse-down plus the mouse deltas (the viewport captures and hides the cursor while a button is held). It forwards the cursor and the viewport size to the subsystem.
- **Gesture.** Mouse-down picks the body with `SOLMapScreenPick::PickNearestOnScreen` over the body overlay's last-frame screen positions and on-screen pick radii (`FSOLMapBodyOverlay::GetScreenPositionsPx` / `GetPickRadiiPx`: max(apparent radius, `MAP_MIN_PICK_RADIUS_PX` x UI scale), 0 for a body behind the camera or hidden behind a nearer body); no hit means the ship is the reference. While held, the cursor ray (map camera position plus a direction built from the camera's look-at orientation and its 60° horizontal FOV across the viewport, the inverse of the HUD's projection) meets the ecliptic plane through the reference (`RayPlaneIntersect`, `DecomposePlanarOffset`); a ray that misses keeps the last valid preview. Mouse-up locks it. With the planar offset locked, holding Shift starts the height preview; releasing Shift without a click drops it (back to 0). A Shift + click locks it. Any plain click starts a new pick; X clears everything.
- **Shift height scaling.** `dz = (cursor Y at preview start - cursor Y) x metersPerPixel`, with `metersPerPixel = |map camera - locked XY point| x SOL::MAP_HEIGHT_DISTANCE_FRACTION_PER_PIXEL` (0.001), frozen when the preview starts. It is a pure function of vertical cursor travel, so moving back retraces it exactly, it cannot divide by zero or produce NaN, and it scales with the zoom. 0.001 is close to one pixel's world size at that depth (60° FOV at 720 p is ~0.0009 of the distance per pixel), so the guide line's tip roughly follows the cursor. This replaces the camera-facing vertical plane idea from section 3 step 4, which blows up in a steep top-down view.
- **Visuals.** `FSOLMapPickOverlay` (`Map/SOLMapPickOverlay`, plain C++, owned by `ASOLFlightHud`, drawn right after the body icons) draws everything in cyan: a ring around the reference; while dragging, a translucent disc (64-point triangle fan) in the ecliptic plane through the reference with the drag distance as radius, its outline, the radius line and a small square at the XY point; after the planar lock, the same without the fill, dimmed; during the height preview, a vertical guide line from the XY point to the height with a tick at its tip (dim once locked). The **destination marker** is a magenta diamond with four crosshair arms and a center dot, deliberately unlike the round body icons: dim after the planar lock only, bright once the height is locked too. Every point goes through the bodies' render placement (`ComputePointRenderLocationCm`), so the visuals line up with the bodies at any zoom. The only per-frame buffers are sized at `Initialize`.
- **Verification.** `-SOLSmokeMapPick` (`FSOLMapPickSmoke`) drives the real input pipeline: it clicks the most isolated body icon (the body is the reference), drags 80 x 50 px (screenshot of the disc), releases (the locked XY point must project back onto the cursor within 1.5 px, measured at 0.000 px), holds Shift and moves up 60 px then back 20 px (preview exactly 60, then 40 px x the frozen scale; screenshot of the guide line), Shift + clicks (height locked, destination = live reference + offsets; screenshot of the marker), presses Enter (logged position = live destination), X (cleared), then repeats the click / drag / release / Enter on empty space (the ship is the reference), and closes and reopens the map (cleared both times).
- **2c-2 finding 3** (overlapping pick spheres letting the wrong body win) is resolved by using the screen-space pick above instead of `PickBodyUnderRay`.

### Part 2d, post-review fixes (2026-09-30)

1. **OS cursor desync after a pick drag (finding 1, the important one).** Unreal's `FSceneViewport` restores the OS cursor to its pre-capture (mouse-down) position when mouse capture ends, but the map's internally-tracked cursor position stays at the drag's end point — so after releasing a drag, the visible cursor and the game's idea of "where the cursor is" disagree until the player moves the mouse again, at which point the tracked position jumps. Fix: immediately after a drag's mouse-up (planar lock) or when transitioning out of any captured drag, explicitly warp the real cursor to the tracked map-cursor position (`APlayerController::SetMouseLocation`) and reset the last-OS-position tracking to match, so there is no jump on the next mouse movement. Re-anchor the height-preview's starting cursor Y from the corrected position, not the stale one.
2. **Shift overload between height-preview and camera pan (finding 2).** Shift is also the alternate pan-trigger modifier (with right-drag, from 2b-2). Fix: the height preview must not start or continue while a right-drag/pan is active (`mIsMapRightHeld`/the pan-drag flag), and a left-click that arrives while Shift is held ONLY locks the height if a height preview is actually active for the picking gesture (not merely because Shift happens to be down for an unrelated pan). Document this interaction explicitly in `GAME_MECHANICS.md`.
3. **Height-lock reads a stale preview (finding 3).** The Shift+click lock path must recompute the height preview from the current cursor before locking it, the same way the planar mouse-up path already refreshes before locking (do not lock whatever `mPreviewHeightM` happened to hold from the last `UpdatePick`).
4. **Reference-body-index robustness (finding 4).** If `ReferenceBodyIndex` is ever out of range (defensive; should not happen with a stable registry), fall back to the ship AND clear `bReferenceIsBody`, with a one-line comment explaining the assumption that registry indices are stable for the session, plus a `LogSOL` warning on the fallback path so it's visible if the assumption is ever violated.
5. **Docs (finding 5).** Correct `GAME_MECHANICS.md`'s height-scale description: frozen when the height preview actually starts (either on first Shift-hold after the planar lock, or immediately at mouse-up if Shift was already held), not simply "when Shift goes down."
6. **Style (finding 6).** Reflow the two over-120-column lines; make the rim thickness scale with the UI scale instead of a bare `1.0f`; de-duplicate the ecliptic axis constants shared between `SOLMapModeSubsystem.cpp` and `SOLMapPickOverlay.cpp` into one shared location (e.g. `SOLConstants.h` or a small shared header).

**As applied.** (1) `ASOLShipPawn::SyncOsCursorToMapCursor` runs when the last held map button (left, right or middle) is released: it rounds and clamps the map cursor to a viewport pixel, warps the OS cursor there (`SetMouseLocation`), stores it as `mLastOsMousePx`, and forwards it as the map cursor; on a left release this happens before `ReleasePick`, so the planar lock and a height preview started at mouse-up both use the synced position. A right or middle press first re-reads the OS cursor. It is skipped while the cursor is scripted. (2) The pawn forwards `SetCameraDragActive(right || middle held)`: a camera drag starting drops a running height preview, none starts while one lasts, and ending the drag does not start one. `PressPick` locks the height only while `mIsHeightPreviewing`. (3) The lock path calls `UpdatePick()` first. (4) `ResolvePickReferenceM` (now non-const) falls back to the ship, clears `bReferenceIsBody` and the index, and logs a warning. (6) Axes are `SOL::MAP_PICK_ECLIPTIC_X/Y` and `SOL::MAP_PICK_UP` in `SOLConstants.h` (also used by the pick smoke); the rim is `MAP_PICK_RIM_LINE_PX` x UI scale. **Verification:** `-SOLSmokeMapPick` now also runs one drag with the scripted cursor off (checks `n1`-`n6`): it places the real OS cursor, presses, injects mouse deltas while the OS cursor stays put (as the viewport's capture pins it), puts the OS cursor back at the drag start right before the release (as the viewport does), and holds Shift; after mouse-up the OS cursor must read the map cursor's position, the planar lock must project onto it, the height preview must start at 0 there, and a following 15 x 10 px OS move must move the map cursor by exactly that and give 10 px of height.
