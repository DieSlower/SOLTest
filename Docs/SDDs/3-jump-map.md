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
- **Known limitation for 2c.** At the default 1e13 m every body is sub-pixel (smoke run: Sun 0.08 px radius, Jupiter 0.008 px, Earth 0.0007 px), so the default map view shows no bodies at all, only the HUD (and the orbit ellipses if O was on). Real meshes do render from the map camera when zoomed in (Earth ~110 px radius at 5e7 m). The icon/billboard fallback of 2c is required for the map to be usable at system scale.
- **Mouse buttons (settled 2026-09-30).** Right drag orbits, middle drag or Shift + right drag pans, and the left button is left unbound in `IMC_Map` so a left click/drag currently does nothing; 2d binds it to the destination-picking gesture (section 3 step 1). The `-SOLSmokeMap` run checks all three (left drag leaves the camera unchanged, right drag orbits, middle and Shift + right drags pan).

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
