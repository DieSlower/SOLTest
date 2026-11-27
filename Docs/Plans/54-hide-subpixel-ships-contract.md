<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Test contract 54: NPC sub-pixel ship rule

For the test author of `Source/SOLTest/Tests/NpcPixelTest.cpp` (`SOLTest.NpcPixel.<Case>`, one
`IMPLEMENT_SIMPLE_AUTOMATION_TEST` per case below with `EAutomationTestFlags::EditorContext |
EAutomationTestFlags::ProductFilter`, the flags of the other files in `Source/SOLTest/Tests/`, e.g.
`NpcTunnelMirrorTest.cpp`). Design:
[SDD 54](../SDDs/54-hide-subpixel-ships.md) sections 4.1-4.6 and 9. This file gives **signatures, units and expected
values only**; the implementation does not exist yet and must not be consulted. The user's answers to SDD 54 Q1-Q12
(2026-11-27) pin the constants below. The math and mirror functions still take thresholds, sizes and diameters as
arguments, so a test passes them explicitly (the values listed in the cases), and reads a `SOL::` constant only where
the table or a case says to.

**Headers:**

- `Npc/SOLNpcPixelMath.h`, namespace `SOLNpcPixel` (new, double precision, pure: no UObjects, no world, no globals).
- `Npc/SOLNpcPointMirror.h`, namespace `SOLNpcPointMirror` (new, header-only, `inline`, float precision: the C++ copy
  of `Shaders/Private/SOLNpcPoint.ush`).
- Existing, used for cross-checks: `Visuals/SOLThrusterMath.h` (`SOLThruster::FocalRefPx(double hFovDeg, double
  aspect)`), `Npc/SOLNpcTunnelMirror.h` (`SOLNpcTunnelMirror::FocalRefPx(float projectionScaleX, const FVector2f&
  viewSize, float referenceHeightPx)`), `Npc/SOLNpcScaleMath.h` (`SOLNpcScale::SelectRenderTier`, `ERenderTier`).
- Constants in `SOLConstants.h` (namespace `SOL`).

**Units:** distances and diameters in metres, pixel sizes in **1080p-reference pixels** (a viewport pixel times
1080 / viewport height) unless a name says device px. Tolerances: pixel sizes 1e-9 relative (double), 1e-4 relative
(float), alpha 1e-12 (double), 1e-5 (float), unless a case says otherwise.

**Notation:** `D0` = `FMath::Sqrt(1188.0)` = 34.46738 m (the faction 0 cone's box diagonal, 12 x 12 x 30 m; compute
it, do not use a rounded literal), `f90` = 960 (focal at 90 degrees, 16:9), `f110`
= 0.5 x 1080 x (16 / 9) / tan(55 degrees) = 672.198 (compute it in the test with `FMath::Tan`, do not hard-code).

## Constants (`SOL`)

| Name | Value | Pinned? |
|---|---|---|
| `NPC_SHIP_HIDE_PX` | 1.0 | yes (user D2, D3) |
| `NPC_SHIP_DOT_FULL_PX` | 2.0 | yes (user D2) |
| `NPC_HULL_DOT_PX` | 4.0 | yes (user Q2, 2026-11-27); test also `>= NPC_SHIP_DOT_FULL_PX x 1.5` |
| `NPC_MESH_MIN_PX` | 16.0 | yes (user Q1, 2026-11-27); test also `> NPC_HULL_DOT_PX x 1.5` |
| `NPC_HULL_BOUNDING_DIAMETER_M` | 34.467 | yes (user Q4, 2026-11-27); test that it equals `BoundingDiameterM` of one of the two placeholder boxes (case 5) within 1e-3 m |
| `NPC_POINT_RASTER_MIN_DEVICE_PX` | 1.0 | yes (user Q5, 2026-11-27); test also `>= 0` |
| Render hysteresis | 0.1 (10%) | yes (user Q1, Q2) |
| `NPC_POINT_MIN_PIXELS`, `NPC_HULL_MIN_PIXELS` | deleted | do not reference them |

## `SOLNpcPixel` (double)

```cpp
namespace SOLNpcPixel
{
    // True size on screen in reference px, focalRefPx x diameterM / max(distanceM, 1)
    SOLTEST_API double ShipSizePx(double diameterM, double distanceM, double focalRefPx);

    // Dot brightness factor: 0 at and below hidePx (and for NaN), linear to 1 at fullPx, 1 above
    SOLTEST_API double DotAlpha(double sizePx, double hidePx, double fullPx);

    // Whether the dot is drawn at all (DotAlpha > 0)
    SOLTEST_API bool IsDotDrawn(double sizePx, double hidePx, double fullPx);

    // Distance at which a ship of diameterM is exactly thresholdPx across, diameterM x focalRefPx / thresholdPx
    SOLTEST_API double PixelRadiusM(double diameterM, double focalRefPx, double thresholdPx);

    // Bounding sphere diameter of an axis-aligned box of the given full size (its diagonal)
    SOLTEST_API double BoundingDiameterM(const FVector3d& boxSizeM);

    // Whether any ship at or beyond nearestDistanceM can be more than hidePx across (the far layer skip)
    SOLTEST_API bool FarLayerCanDraw(double diameterM, double focalRefPx, double nearestDistanceM, double hidePx);
}
```

### Case 1 `ShipSize`

- `ShipSizePx(D0, D0 x f90, f90)` = 1.0; `ShipSizePx(30, 28800, 960)` = 1.0; `ShipSizePx(30, 14400, 960)` = 2.0.
- Linear in diameter and focal, inverse in distance: doubling `D` doubles `px`; doubling `d` (with `d >= 1`) halves
  it; `ShipSizePx(D0, 10000, f110)` = `ShipSizePx(D0, 10000, f90) x f110 / f90`.
- Distance clamp: for `0 <= d <= 1` the result equals `ShipSizePx(D, 1, f)` = `f D` (so `d` = 0 and `d` = 0.5 both
  give `f D`, never infinity).
- Bad inputs give 0: `D` in {0, -1, NaN, +inf}; `f` in {0, -960, NaN, +inf}; `d` in {-1, NaN}. `d` = +inf gives 0.

### Case 2 `DotAlpha`

With `hidePx` = `NPC_SHIP_HIDE_PX`, `fullPx` = `NPC_SHIP_DOT_FULL_PX`:

| `sizePx` | Expected alpha | `IsDotDrawn` |
|---|---|---|
| 0, 0.5, 0.999999 | 0 | false |
| 1.0 exactly | 0 | false |
| 1.0 + 1e-9 | 1e-9 (tolerance 1e-12) | true |
| 1.25 | 0.25 | true |
| 1.5 | 0.5 | true |
| 1.75 | 0.75 | true |
| 2.0 exactly | 1 | true |
| 3, 1e6, +inf | 1 | true |
| NaN, -1, -inf | 0 | false |

- Monotonic: over 1,000 evenly spaced sizes in [0, 3], alpha never decreases and changes by at most
  step / (full - hide) between neighbours (continuity, no pop at 1 or 2).
- Other thresholds (generic): `DotAlpha(3, 2, 4)` = 0.5. `fullPx <= hidePx` is a step at `hidePx`:
  `DotAlpha(1.5, 1, 1)` = 1, `DotAlpha(1.0, 1, 1)` = 0, `DotAlpha(0.5, 1, 0.5)` = 0. A non-finite `hidePx` or `fullPx`
  gives 0 for every size.

### Case 3 `PixelRadius`

- `PixelRadiusM(D0, f90, 1)` = `D0 x 960` (33,088.68 m within 0.01 m); `PixelRadiusM(D0, f90, 4)` = a quarter of it;
  `PixelRadiusM(30, 960, 2)` = 14,400.
- Inverse of case 1: for `p` in {1, 1.5, 2, 4, 16, 100} and `f` in {f90, f110}, `ShipSizePx(D0, PixelRadiusM(D0, f, p),
  f)` = `p` (relative 1e-12).
- Bad inputs give 0 (a tier no ship qualifies for; `SelectTierIndex` skips radii <= 0): any of `D`, `f`, `thresholdPx`
  <= 0, NaN or infinite.

### Case 4 `RenderTierByPixels`

Composition with the existing `SOLNpcScale::SelectRenderTier(current, distanceM, meshRadiusM, instancedRadiusM,
hysteresisFraction)`. Parameters: `meshPx` = 16, `hullPx` = 4, `h` = 0.1, `D` = `D0`, `f` = `f90` (pinned by Q1 and Q2,
passed as arguments; the expected tiers below follow from the rule "promote above the threshold, keep down to threshold / (1 + h)"). For a
target size `p`, use `d = PixelRadiusM(D, f, p)` and radii `PixelRadiusM(D, f, meshPx)`, `PixelRadiusM(D, f,
hullPx)`.

| Current tier | `p` (px) | Expected |
|---|---|---|
| Point | 20 | Mesh |
| Point | 10 | Instanced |
| Point | 4.2 | Instanced |
| Point | 3.8 | Point (not promoted below 4) |
| Instanced | 3.8 | Instanced (kept down to 3.636) |
| Instanced | 3.5 | Point |
| Instanced | 17 | Mesh |
| Mesh | 15 | Mesh (kept down to 14.545) |
| Mesh | 14 | Instanced |
| Mesh | 3.0 | Point |

- The same table at `f110` gives the same tiers (pixel-defined, FOV-independent in px).
- Resolution: the same table with `f` from `SOLThruster::FocalRefPx(90, 1280 / 720)`, `(90, 1920 / 1080)` and
  `(90, 3840 / 2160)` gives the same tiers (all three focals equal 960 within 1e-9).

### Case 5 `BoundingDiameter`

- `BoundingDiameterM((12, 12, 30))` = 34.4674 (within 1e-4); `BoundingDiameterM((30, 15, 5.4))` = 33.9729 (within
  1e-4); `BoundingDiameterM((3, 4, 0))` = 5; `(0, 0, 0)` = 0.
- Any negative or non-finite component gives 0.
- `SOL::NPC_HULL_BOUNDING_DIAMETER_M` equals one of the two placeholder results within 1e-3 m and is at least the
  other (it must contain both hulls).

### Case 6 `FarLayerSkip`

Hide threshold `NPC_SHIP_HIDE_PX`:

| `D`, `f`, nearest distance | Expected |
|---|---|
| `D0`, `f90`, 2.0e6 (the default Mid radius) | false (0.0165 px) |
| `D0`, `f110`, 2.0e6 | false |
| `D0`, `f90`, 30,000 | true (1.103 px) |
| `D0`, `f90`, 33,000 | true (1.0027 px) |
| `D0`, `f90`, 33,200 | false (0.9966 px) |
| `D0`, 60,000 (a very narrow FOV), 2.0e6 | true (1.03 px) |

- Conservative on bad inputs (the shader then decides): nearest distance <= 0 or NaN gives true; `D` or `f` <= 0, NaN
  or infinite gives true.
- Consistency: for 200 log-spaced distances in [1e3, 1e9] m, `FarLayerCanDraw(D0, f90, d, 1)` equals
  `IsDotDrawn(ShipSizePx(D0, d, f90), 1, 2)`.

## `SOLNpcPointMirror` (float, the shader's copy)

```cpp
namespace SOLNpcPointMirror
{
    // Focal length in reference px from the projection's [0][0] (SOLNP_FocalRefPx); 0 for a non-positive or NaN size
    inline float FocalRefPx(float projectionScaleX, const FVector2f& viewSize, float referenceHeightPx);

    // True size in reference px (SOLNP_ShipSizePx): the rules of SOLNpcPixel::ShipSizePx in float
    inline float ShipSizePx(float diameterM, float distanceM, float focalRefPx);

    // Dot alpha (SOLNP_DotAlpha): the rules of SOLNpcPixel::DotAlpha in float, NaN guarded explicitly
    inline float DotAlpha(float sizePx, float hidePx, float fullPx);

    // Drawn dot diameter in device px: max(sizePx x viewHeightPx / referenceHeightPx, rasterMinDevicePx); 0 when the
    // dot is not drawn is NOT this function's job (it is called only for drawn dots)
    inline float DrawnDiameterDevicePx(float sizePx, float viewHeightPx, float referenceHeightPx,
        float rasterMinDevicePx);
}
```

### Case 7 `MirrorFocal`

- `FocalRefPx(1.0, (1280, 720), 1080)` = `FocalRefPx(1.0, (1920, 1080), 1080)` = `FocalRefPx(1.0, (3840, 2160), 1080)`
  = 960 (projection [0][0] = 1 / tan(45 degrees) = 1).
- `FocalRefPx(1 / tan(55 degrees), (1920, 1080), 1080)` = `f110` (float tolerance).
- 21:9: `FocalRefPx(1.0, (2560, 1080), 1080)` = 1280.
- Equals `SOLNpcTunnelMirror::FocalRefPx` with the same arguments exactly, and `SOLThruster::FocalRefPx(hFov, W / H)`
  within float tolerance for hFov in {60, 90, 110} and the three 16:9 sizes plus 2560x1080.
- A view size component 0, negative or NaN gives 0.

### Case 8 `MirrorParity`

- Sweep `D` in {33.9729, 34.4673}, `f` in {f110, 960, 1280}, 300 log-spaced `d` in [0.5, 1e9] m: the mirror's
  `ShipSizePx` matches `SOLNpcPixel::ShipSizePx` within 1e-5 relative; `DotAlpha` (hide 1, full 2) matches within
  1e-5; the drawn decision (`alpha > 0`) matches wherever the double `px` is more than 1e-5 away from 1.
- Every bad-input row of cases 1 and 2 gives the same result in the mirror (0 or 1 exactly as the double). Leave out
  the `1.0 + 1e-9` row of case 2: it rounds to exactly 1.0 in float (use `1.0f + 1.2e-7f`, one float ulp above 1, which
  must give an alpha > 0 and below 2e-7).
- NaN guard: `DotAlpha(NaN, 1, 2)` = 0 (a plain clamp would give 1 in C++).

### Case 9 `DrawnFootprint`

With `referenceHeightPx` = 1080 and `rasterMinDevicePx` = 1 (`NPC_POINT_RASTER_MIN_DEVICE_PX`, pinned by Q5):

| `sizePx` | view height | Expected device px |
|---|---|---|
| 1.5 | 1080 | 1.5 |
| 1.5 | 720 | 1.0 |
| 1.2 | 720 | 1.0 (0.8 floored) |
| 1.2 | 2160 | 2.4 |
| 3.0 | 1080 | 3.0 |
| 1.0001 | 1080 | 1.0001 (no enlargement at or above 1080p for a drawn dot) |

Added after the adversarial review (SDD 54 Q13, user-approved 2026-11-27): `DrawnDiameterDevicePx` itself is unchanged; the drawn size passed to it is first capped at `NPC_POINT_DOT_MAX_PX` (4 reference px) by the separate `CappedSizePx` step, tested in `SOLTest.NpcPointDot.Cap` (written after the code), so every row above still holds.

- With `rasterMinDevicePx` = 0 the result is always `sizePx x height / 1080`.

## Out of scope for unit tests (checked by the 54f smoke)

The shader itself (`SOLNpcPoint.ush` inside `M_SOLNpcPoints`), the far layer component's visibility, the hull tier of
real CPU ships, flames on dot ships, and that a hidden ship stays selectable.
