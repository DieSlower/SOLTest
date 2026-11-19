<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# 52a contract: tunnel far-field math (test first)

Written by the 52a test author from [SDD 52](../SDDs/52-lane-tunnel-far-field.md) (D1-D19) and
[plan 52](52-lane-tunnel-far-field-plan.md), without the implementation. The tests are in
`Source/SOLTest/Tests/NpcTunnelFarFieldTest.cpp`, paths `SOLTest.Npc.TunnelFarField.*` (run with
`Tools\RunTests.bat -Build -Filter SOLTest.Npc.TunnelFarField`; the plan's `-Filter SOLTest.NpcTunnelMath` does **not**
pick them up). Everything goes in `Npc/SOLNpcTunnelMath.{h,cpp}` (`namespace SOLNpcTunnel`) and `SOLConstants.h`
(`namespace SOL`). Universe metres, double, unless a signature says float.

**Pixels.** Every pixel value is a **1080p-reference pixel** (D8): actual px x `NPC_LANE_REFERENCE_HEIGHT_PX` /
viewport height. A focal length in reference px (`focalRefPx`) is the one view input: with UE's fixed horizontal FOV
and a fixed aspect it is the same at 1280x720, 1920x1080 and 3840x2160, so the look and the window are too.

## Constants (`SOLConstants.h`, `namespace SOL`)

| Name | Type | Value | Status |
|---|---|---|---|
| `NPC_TUNNEL_RING_BASE_SPACING_RADII` | double | 12 (S0 in Rt, flat everywhere, D2) | pinned; `static_assert` tied to `NPC_TUNNEL_RING_KEEP_EVERY` (6) x 2 |
| `NPC_TUNNEL_RING_SPACING_PER_DISTANCE` | | removed (rho, accordion gone) | the tests never name it |
| `NPC_TUNNEL_RING_SLOTS_PER_SIDE` | int32 | 80 | pinned (window, SDD 4.3) |
| `NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE` | int32 | implementer | invariants: in [2, RING_SLOTS_PER_SIDE]; covers every detail with a gap gate > 0 (below); budget |
| `NPC_TUNNEL_REFERENCE_FOCAL_PX` | double | 960 | pinned: focal of a 16:9 view at `NPC_LANE_REFERENCE_HEIGHT_PX` and `SHIP_CAMERA_BASE_FOV_DEG` (90) |
| `NPC_TUNNEL_RADIUS_FADE_SOLID_PX` | double | 6 | pinned (D9) |
| `NPC_TUNNEL_RADIUS_FADE_KNEE_PX` | double | 1 | pinned (D9) |
| `NPC_TUNNEL_RADIUS_FADE_KNEE_ALPHA` | double | 0.1 | pinned (D9) |
| `NPC_TUNNEL_RADIUS_FADE_GONE_PX` | double | 0.5 | pinned (D9) |
| `NPC_TUNNEL_RING_GAP_FADE_FULL_PX` | double | 3 | pinned (D11) |
| `NPC_TUNNEL_RING_GAP_FADE_GONE_PX` | double | 2 | pinned (D11) |
| `NPC_TUNNEL_DETAIL_GAP_PX` | double | 20 | pinned (D11) |
| `NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX` / `_FULL_PX` | double | **open** (suggested 16 / 25, 20 px the log midpoint) | invariants: 0 < GONE < FULL, GONE <= 20 <= FULL, 10 <= GONE, FULL <= 40 |
| `NPC_TUNNEL_END_RING_MIN_DRAWN_PX` | double | 1 | pinned (D14) |
| `NPC_TUNNEL_VERTEX_BUDGET` | int32 | 80,000 | pinned, interim (D15, #53 revisits) |
| `NPC_TUNNEL_VERTEX_BUDGET_LANES` | int32 | 8 | pinned, the lane count the budget is for |
| Ring LOD bands (`NPC_TUNNEL_RING_*_SLOTS`, `_SEGMENTS_*`) | | implementer (SDD 4.3 suggests 96 / 48 / 24 / 12 / 8 at slots 0-1 / 2 / 3-4 / 5-9 / 10+) | invariants: slot 0 at `_SEGMENTS_NEAREST`, non-increasing per side, > 0, ahead = behind; budget |

May be removed by the implementer (D10, the tests never name them): `NPC_TUNNEL_DEPTH_FADE_*`,
`NPC_TUNNEL_DETAIL_FADE_START/END_RADII`, `NPC_TUNNEL_SCREEN_FADE_HALF_EXTENTS`, `NPC_TUNNEL_END_RING_MIN_ALPHA`.
Kept and used: `NPC_LANE_REFERENCE_HEIGHT_PX` (1080), the chevron far fade (13-30 Rt, D22), the near fade, the end
fade, the cap fades, `NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII`, `NPC_TUNNEL_DOT_SIZE_PX`,
`NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII`.

## Struct change

`FSOLNpcTunnelSlotInput` gains `double FocalRefPx = 0.0;` (focal length in reference px). `RingSlot`, `DotSlot`,
`ChevronSlot` and `EndRingSlot` return **false** when it is not finite or <= 0. The other fields keep their meaning
(`Phase` is now `RingLatticePhase` of the foot arc).

## New and changed functions (`namespace SOLNpcTunnel`, all `SOLTEST_API`)

Notation: `S0 = RingLatticeSpacingM(Rt)`, `d = sqrt(x^2 + h^2)` (camera to the axis point at axial offset `x`),
"bad Rt" = NaN, infinite or <= 0, "bad f" = NaN, infinite or <= 0.

```cpp
// View
double FocalLengthRefPx(double horizontalFovDeg, double viewportWidthPx, double viewportHeightPx);
//   (W / 2) / tan(fov / 2) x NPC_LANE_REFERENCE_HEIGHT_PX / H; 0 for a non-finite input, W or H <= 0, fov outside (0, 180)
double ToReferencePx(double viewportPx, double viewportHeightPx);
//   px x NPC_LANE_REFERENCE_HEIGHT_PX / H; 0 for NaN px, a non-finite or non-positive H

// Flat lattice (replaces RingSpacingM, RingWarp, RingWarpInverse, AdvanceRingPhase, InitialRingPhase, StepRingPhase)
double RingLatticeSpacingM(double tunnelRadiusM);              // NPC_TUNNEL_RING_BASE_SPACING_RADII x Rt; 0 bad Rt
double RingLatticePhase(double footArcM, double tunnelRadiusM);
//   frac(footArc / S0) in [0, 1), always < 1 (a result that rounds to 1 reads 0), negative arcs wrap up; computed directly
//   from the unclamped foot arc, no history; 0 for a non-finite arc or bad Rt
float RingSlotOffsetM(int32 slot, float phase, float tunnelRadiusM);   // NEW SIGNATURE (radial removed)
//   the shader path in float: w(slot, phase) x BASE x Rt; phase wrapped into [0, 1), non-finite phase reads 0;
//   0 for a slot out of range or a bad Rt

// Pixel sizes (reference px)
double RingRadiusPx(double distanceM, double tunnelRadiusM, double focalRefPx);    // f Rt / d; +inf for d <= 0
double ChevronLengthPx(double distanceM, double tunnelRadiusM, double focalRefPx);
//   f x 2 x NPC_TUNNEL_CHEVRON_HALF_LENGTH_RADII x Rt / d; +inf for d <= 0
//   both: 0 for a NaN distance, bad Rt or bad f
double DotDrawnSizePx(double distanceM, double tunnelRadiusM, double focalRefPx);
//   max(f x NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII x Rt / d, NPC_TUNNEL_DOT_SIZE_PX); the floor for any bad input
double EndRingDrawnRadiusPx(double radiusRefPx);   // max(r, NPC_TUNNEL_END_RING_MIN_DRAWN_PX); the floor for NaN

// Fades
double RingRadiusFade(double radiusRefPx);
//   D9: 1 at >= 6 px; 0.1 + 0.9 ln(r / 1) / ln(6 / 1) on [1, 6]; 0.1 ln(r / 0.5) / ln(1 / 0.5) on [0.5, 1]; 0 at <= 0.5;
//   (constants by name) 0 for NaN, 1 for +inf
double RingGapPx(double axialM, double radialM, double tunnelRadiusM, double focalRefPx);
//   X = |x|, H = |h|, X' = X + S0 (the neighbour away from the camera), d = sqrt(X^2 + H^2), d' = sqrt(X'^2 + H^2):
//   f x max(atan2(X', H) - atan2(X, H), Rt / d - Rt / d'); +inf when d == 0; 0 for NaN, bad Rt or bad f
double RingGapFade(double gapRefPx);
//   0 at <= RING_GAP_FADE_GONE_PX (2), 1 at >= _FULL_PX (3), continuous and non-decreasing between (log-linear
//   suggested); 0 for NaN, 1 for +inf
double DetailGapFade(double gapRefPx);       // same shape on DETAIL_GAP_FADE_GONE_PX .. _FULL_PX
double TunnelDrawFade(bool bShown);          // D7: 1 when shown, 0 hidden; no distance input (F = 1 for drawing)
```

`TunnelFade(distance, laneRadius)` is **unchanged** and becomes the **label strength** only (the 100 -> 50 lane radii
band, D7). No ring, dot, chevron or end-ring alpha reads it.

### Slot alphas (the lane's draw fade is the caller's, as today)

Slot warp coordinates are unchanged with the warp made linear: ring slot `k < PER_SIDE` at `w = k + 1 - phase`, else
`w = -((k - PER_SIDE) + phase)`; `AxialOffsetM = w x S0`, `W = w`. Detail slots keep the gap rule (gap ring at
`w = k - phase` ahead, `-(k - PER_SIDE + 1) - phase` behind, element `e` at `e / PER_GAP` of the gap). `dEnd = min(x -
EndArcAM, EndArcBM - x)`.

- `RingSlot`: `RingCapFade(w) x TunnelEndFade(dEnd, S0) x TunnelNearFadeAt(x, h, Rt) x RingRadiusFade(RingRadiusPx(d))
  x RingGapFade(RingGapPx(x, h))`; true when strictly between the end rings and alpha > 0. No depth fade, no screen
  fade (D10).
- `DotSlot`: `DetailCapFade x TunnelEndFade(dEnd, S0) x TunnelNearFadeAt x RingRadiusFade(RingRadiusPx(d)) (D12: the
  dot by its ring's radius) x DetailGapFade(RingGapPx(x, h))` at the dot's own `x`. No depth fade, no 256-512 Rt
  detail fade (D10). The shader still applies `TunnelDetailNearFade` and draws the dot at `DotDrawnSizePx`.
- `ChevronSlot`: the same with `RingRadiusFade(ChevronLengthPx(d))` (D12: by its world length) in place of the ring
  radius fade, times `TunnelChevronFarFade(d)` (D22 stays).
- `DetailCapFade` applies to the element's or its gap ring's `w` (implementer); the tests need only that the cap is 1
  wherever the gap gate is not 0 (window invariant below).
- `EndRingAlpha(double axialOffsetM, double radialM, double tunnelRadiusM, double focalRefPx)` (**new signature**):
  `TunnelNearFadeAt(x, h, Rt) x E(r)`, `r = RingRadiusPx(d)`, with `E(r) = RingRadiusFade(r)` for `r >= 1 px`; the 0.5
  floor and the 49c exemption are gone (D14). **Open (user):** below 1 px, `E` is only required to be <= 0.1 and
  non-increasing as `r` shrinks: either the curve (gone at 0.5 px, so a far lane is not marked, SDD 4.5) or held at
  0.1 (end rings always faintly visible, SDD D2 "only the end rings show"). The ring is drawn at
  `EndRingDrawnRadiusPx(r)`. `EndRingSlot` returns that alpha (beyond the ring cap too), false for a bad focal.

### Window invariants the tests check

- **Rings:** `PER_SIDE x BASE >= NPC_TUNNEL_REFERENCE_FOCAL_PX / KNEE_PX`, so at any `f` up to the reference focal (FOV
  90 to 110 degrees) a ring outside the window (`|w| >= PER_SIDE`) has its uncapped alpha <= 0.1: the window only cuts
  rings already below 1 px, through the cap fade (continuous). The lattice is never subsampled (D11).
- **Details:** every dot or chevron position at `|x| >= (DETAIL_PER_SIDE - 3) x S0` has `DetailGapFade(RingGapPx) ==
  0` at any `f` up to the reference focal, so the detail cap is 1 wherever the gate is not 0 (whichever `w` it reads).
  From the side `RingGapPx ~ f S0 h / (h^2 + x^2)`, largest reach `x = f S0 / (2 GONE)`, so this needs about
  `(DETAIL_PER_SIDE - 3) x 12 >= 960 x 12 / (2 x GONE)`: 33 slots per side for GONE 16, 27 for GONE 20.
- **Budget:** `NPC_TUNNEL_VERTEX_BUDGET_LANES x TunnelSlotMeshVertexCount() <= NPC_TUNNEL_VERTEX_BUDGET`, the count by
  today's formula (ring ribbons + 2 end rings at the nearest count + dot and chevron quads). With 33 detail slots per
  side the SDD's 8-segment far band is needed (about 9,620 per lane, 77,000 for 8); with the old 12-segment far band
  it is over budget.

## Existing tests this contract invalidates (`NpcTunnelMathTest.cpp`, do not keep them as they are)

| Case | Why | Change |
|---|---|---|
| `Constants` | rho 1.5, the constant zone 8 Rt, 12 / 6 slots, depth fade, 256-512 detail fade, end ring 0.5 floor | keep the unaffected rows; drop rho, the zone, depth and detail rows; slots now in `TunnelFarField.Constants` |
| `RingSpacing`, `RingWarp` | accordion | delete (flat spacing in `TunnelFarField.FlatSpacing`) |
| `RingPhase`, `StepRingPhase` | accumulated phase | delete or re-pin onto `RingLatticePhase` (`TunnelFarField.DirectPhase`) |
| `RingSlotFloatPath` | 4-arg `RingSlotOffsetM`, warp reference | delete (`TunnelFarField.FloatPathAtMaxLength`) |
| `NoPopSweep`, `LaneFixedNearRings` | warp reference, depth fade, no focal | delete or re-pin to the flat lattice with a `FocalRefPx` (`TunnelFarField.NoPops*`) |
| `RingSlotCoverage`, `DetailSlotCoverage` | brute force on the accordion and depth / detail fades, 12 / 6 slots | delete (`TunnelFarField.RingWindow`, `DetailFade`) |
| `GuideThinning` | the pre-thinning accordion as the reference | re-pin: dots and chevrons at offsets 0 and 0.5 of the flat gap stays true; the warp-subset part goes |
| `Fades` | `TunnelDepthFade`, `TunnelDetailFade`, 12 / 6-slot cap values | keep the end / near / cap-formula rows with the new slot counts; drop depth and detail fade rows |
| `EndRing` | 3-arg `EndRingAlpha`, 0.5 floor | delete (`TunnelFarField.EndRings`) |
| `ShapeFades` | `TunnelScreenSizeFade`, `TunnelRingScreenFade` (D10) | keep the detail near fade, the chevron far fade and the dart; drop the screen fade half |
| `DotRowSegment` | the dot rows end at `NPC_TUNNEL_DETAIL_FADE_END_RADII` | re-pin the row's end to what replaces it (the detail gate's reach) or delete with the function if 52b drops it |
| `SlotMesh` | 2,884 vertices, bands 2 / 3 / 5, the test-local 80,000 | keep the formula; the budget moves to `NPC_TUNNEL_VERTEX_BUDGET` (`TunnelFarField.SlotMeshBudget`); drop the 2,884 pin and band pins |
| `TexelCrossfade`, `VisibilityRules` | `F` read by the line and point materials, `EffectiveTunnelFade` | 52b (D6 / D7); unaffected by 52a's math |

Any slot-input fixture (`NpcTunnelInput`) needs `FocalRefPx` set, or every slot returns false.
