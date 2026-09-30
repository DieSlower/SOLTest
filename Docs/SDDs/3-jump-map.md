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
| Camera | J releases the mouse cursor. Left-drag on empty space orbits the camera around the current focus point; middle/right-drag (or Shift+drag) pans the focus; mouse wheel zooms on a log scale from system-wide down to a single body's neighborhood. |
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
