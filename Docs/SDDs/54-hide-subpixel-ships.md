<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 54: Hide NPC ships whose true size is under 1 pixel

**Issue:** [#54](https://github.com/DieSlower/SOLTest/issues/54). Plan:
[`Plans/54-hide-subpixel-ships-plan.md`](../Plans/54-hide-subpixel-ships-plan.md). Test-first contract:
[`Plans/54-hide-subpixel-ships-contract.md`](../Plans/54-hide-subpixel-ships-contract.md). Companion ticket:
[#56](https://github.com/DieSlower/SOLTest/issues/56), the target zoom overlay that makes hidden ships findable (not
designed here). Builds on SDD 11 (NPC AI and render tiers, budgets, the point layers), SDD 50 (lane ship CPU / GPU
hand-over), SDD 51 (thruster flame and glow pixel rules, Q27), SDD 52 D8 (1080p-reference pixels) and SDD 53 (lane
ship windows).
**Status: IMPLEMENTED 2026-11-27 (parts 54a-54g). Grill-me decisions D1-D8 and the answers Q1-Q13 are all resolved by the
user (section 3); no open questions remain. `M_SOLNpcPoints` was rebuilt for `NpcParams5` and `NpcParams6`. The A/B
result and the gate verdicts are in the plan notes; the README gallery shots and the commit are pending.**
**Persistence: none.** `SOL::Settings::SAVE_VERSION` stays 13 and `KEY_BINDING_LAYOUT_VERSION` stays 10. The Demo-page
options (`SOLNpcScale::FDemoOptions`) are not saved, so changing their rows (Q6) is not a schema change.

**Pixel rule (project docs, user rule 2026-11-21), repeated for every part:** LOD, fade, visibility and switching
thresholds are on-screen pixels (1080p reference pixels), never distance or object counts. Distance appears only as a
derived figure. A fixed pool that cannot grow is the only allowed cap, and hitting it is reported to the user as a
limitation, never used as the LOD rule.

## 1. Problem

NPC ships are drawn far larger than they are. Every CPU point is at least 2 px across (`NPC_POINT_MIN_PIXELS`), every
instanced hull at least 3 px (`NPC_HULL_MIN_PIXELS`), and the render tier is picked by distance (real mesh within
2 km, instanced hull within 200 km, `FDemoOptions::MeshRadiusM` / `InstancedRadiusM`). A 30 m hull at 200 km is
0.15 px across in truth, yet it is drawn as a 3 px hull, and a million GPU far points millions of kilometres away are
each a 2 px dot. The sky fills with ships that could not be seen, and the distance tiers break the pixel LOD rule
(`Docs/TECH_DEBT.md`, legacy distance- and count-based NPC LODs, which names this ticket as the first step).

## 2. Goals and non-goals

**Goals**

- A ship whose true size on screen is under 1 px is not drawn at all; from 1 px to 2 px it fades in; from 2 px it
  looks as it does today. No pop.
- The pixel size floors that enlarge ships (`NPC_POINT_MIN_PIXELS`, `NPC_HULL_MIN_PIXELS`) are removed.
- The render tier (mesh, instanced hull, dot) is chosen by true projected size, not by the 2 km / 200 km radii.
- The same rule on every NPC draw path: GPU far points, CPU points, instanced hulls, mesh-pool hulls, battle (combat)
  ships and lane ships. The point shader and its C++ float mirror agree.
- Thruster flames keep their own pixel rule, independent of how the hull is drawn.
- No per-frame allocation, no new per-ship CPU pass, and a lower GPU and upload cost than today.

**Non-goals**

- Radar, map markers, overlay icons, target lock and selection are unchanged: a hidden ship can still be selected
  (D4). `FSOLNpcCpuShip::bHidden` keeps its meaning (dead, waiting to respawn) and is never used for this rule.
- The target zoom overlay (#56).
- The AI tiers (Full 50 km / budget 4,000, Mid 2,000 km / budget 100,000) stay distance-based legacy debt.
- The GPU far layer's distance brightness falloff (`NPC_POINT_BRIGHTNESS_*`), the NPC bolt radius
  (`NPC_COMBAT_VISUAL_RADIUS_M`) and bolt sizes (#35) are untouched.
- The brightness side of "realistic visibility" (#39, Q11).
- VR (record only until after procedural terrain).

## 3. Resolved decisions (user, grill-me 2026-11-27; Q1-Q12 user-approved 2026-11-27; Q13 2026-11-27)

| # | Decision (verbatim intent) | Where |
|---|---|---|
| D1 | The cutoff is measured on the ship's **bounding sphere diameter** projected to pixels at the **1080p reference**, scaled with the actual resolution and FOV the way the existing pixel code does it (SDD 52 D8). | 4.1, 4.2 |
| D2 | Ships whose true size is **under 1 px are not drawn at all**. **Between 1 px and 2 px** a dot is drawn with brightness ramping **linearly from 0 at 1 px to full at 2 px** (no pop). **At and above 2 px: today's look.** The 2 px point floor (`NPC_POINT_MIN_PIXELS`) is **removed as a size enlarger**. | 4.3, 4.4 |
| D3 | The cutoff is **fixed at 1 px on every graphics preset** (no quality scaling). | 4.2 |
| D4 | **Visuals only:** radar, map markers, target lock and selection logic are unchanged (a hidden ship can still be selected). The target zoom overlay is a separate ticket, **#56**, the companion that makes hidden ships findable. | 2, 4.8 |
| D5 | **Hull tiers become pixel-based:** remove the 3 px hull floor (`NPC_HULL_MIN_PIXELS`); an instanced hull whose true size is **below about 4 px becomes a dot**; the **mesh-to-hull switch is also decided by true projected size**, replacing the 2 km / 200 km distance radii for the **render** tier choice (thresholds and hysteresis: Q1, Q2 below). GPU far points and the AI tiers stay legacy debt unless trivially coupled. The fixed pools (mesh budget 32, hull budget 20,000) may remain only as reported hard limits. | 4.5 |
| D6 | The player's own ship is never hidden (it is not an NPC). **Final: the cutoff is universal for NPCs, a selected NPC is not exempt** (Q3). | 4.8, Q3 |
| D7 | **Thruster plumes (#51) follow their own pixel rule, independent of the hull.** | 4.7 |
| D8 | Applies to **points, instanced hulls, combat ships and lane ships** (every NPC path); the **GPU shader and the CPU mirror must agree**. | 4.3, 4.6, 9 |
| Q1 | **Mesh-to-hull threshold: `NPC_MESH_MIN_PX` = 16 reference px**; hysteresis the existing `RenderHysteresis` (10%, 0.1): a mesh is kept down to 14.5 px. | 4.5 |
| Q2 | **Hull-to-dot threshold: `NPC_HULL_DOT_PX` = 4 reference px**, kept down to 3.64 px with the same 10% hysteresis. | 4.5 |
| Q3 | **Selected NPC under 1 px is hidden like any other** (no exemption; the lock marker shows it; #56 makes it findable). D6 is final. | 4.8 |
| Q4 | **One constant `NPC_HULL_BOUNDING_DIAMETER_M` = 34.467 m** (the cone's box diagonal), the dot drawn at that diameter; a per-class table when real ships arrive. | 4.1 |
| Q5 | **Dot footprint 1-2 px: true size with a 1 device px rasterization floor** (`NPC_POINT_RASTER_MIN_DEVICE_PX` = 1), brightness from the true size. | 4.3 |
| Q6 | **Demo rows:** "Mesh radius" and "Instanced radius" are replaced by "Mesh from (px)" 16 and "Hull from (px)" 4 (hull minimum 3 px, mesh never below hull); the two budgets and the hysteresis rows stay; budgets documented as hard limits and a binding budget marked in the stats overlay radius line. | 4.5 |
| Q7 | **Flames on dot and hidden ships:** flames follow their own rule for every CPU ship with thrust data (amends SDD 51 Q27); the glow stays a hull section. | 4.7 |
| Q8 | **One rebuild of `M_SOLNpcPoints`** approved (size code in `SOLNpcPoint.ush`, new `NpcParams5`), by script with the editor closed. | 4.4 |
| Q9 | **Jump map keeps today's 2 px NPC dots** (map view exempt, `NpcParams5.w` = 1, far layer skip bypassed while the map is open); the rule applies in the flight view. | 4.6, 4.8 |
| Q10 | **Far layer goes dark in flight is accepted:** at a normal FOV no ship beyond the Mid radius is drawn (far layer skipped), the scale demo shows no distant points, and inside a tunnel only lane ships of 1 px or more show. | 4.6, 8 |
| Q11 | **Overlap with #39:** #54 covers the size floor; #39 is re-scoped to the brightness side (4% brightness floor, distance falloff, lighting) and grilled when picked up. | 10 |
| Q12 | **Quality scaling of the tier thresholds:** the hull and mesh thresholds (not the 1 px cutoff) are scaled by the shared quality factor (`RIBBON_VANISH_PX_BY_QUALITY[preset] / [Epic]`: Low x10, Medium x4.6, High x2.2, Epic and Cinematic x1, as the tunnel classes do, SDD 53); Epic unchanged. | 4.5 |
| Q13 | **Dot size cap (adversarial review H2, user-approved 2026-11-27):** a dot is drawn at most `NPC_POINT_DOT_MAX_PX` = 4 reference px across (the hull switch size `NPC_HULL_DOT_PX` of the reference preset), in device px scaled by the view height, the raster floor still last; the quality scale (Q12, Low x10 .. Epic x1) moves only where a ship switches to a hull, never the dot's size; the jump map's dot (Q9) is unchanged. One more rebuild of `M_SOLNpcPoints` (new `NpcParams6`). | 4.3 |

## 4. Design

### 4.1 Bounding sphere diameter

Every NPC hull is the 30 m placeholder (`NPC_HULL_LENGTH_M`), drawn by `HullTransform` in `Npc/SOLNpcVisuals.cpp` from
the 100 cm basic shapes:

| Hull | Scale x 30 m | Box (m) | Bounding sphere diameter = box diagonal |
|---|---|---|---|
| Faction 0 cone | (0.4, 0.4, 1.0) | 12 x 12 x 30 | sqrt(12² + 12² + 30²) = **34.467 m** |
| Faction 1 box | (1.0, 0.5, 0.18) | 30 x 15 x 5.4 | sqrt(30² + 15² + 5.4²) = **33.973 m** |

The diameter `D` is the diagonal of the hull's bounding box (a sphere that contains the hull; conservative, and
computable for any future mesh from its bounds). It is the hull only: the flame and the glow have their own rules
(4.7). **Decided (Q4):** one constant `NPC_HULL_BOUNDING_DIAMETER_M` = 34.467 m (the larger of the two, 1.5% above the
box), until real ship classes bring a per-class table. The dot is drawn at this diameter too (its incircle radius
`D / 2`, today `NPC_HULL_LENGTH_M / 2`), so the size that is drawn is the size that is measured.

### 4.2 Pixel size (CPU and GPU)

True size in 1080p-reference pixels: **`px = f D / d`**, with

- `f` the focal length in reference px: `f = (1080 x aspect / 2) / tan(hFOV / 2)` (960 at 90 degrees, 16:9, at every
  resolution; 672.2 at the 110 degree maximum FOV). CPU: the existing `SOLThruster::FocalRefPx(fovDeg, aspect)` from
  `ViewCamera` (both `USOLNpcSubsystem` and `ASOLNpcVisuals` have it). GPU: `0.5 x ViewSize.x x ViewToClip[0][0] x
  1080 / ViewSize.y`, the same expression as `SOLNT_FocalRefPx` / `SOLNpcTunnelMirror::FocalRefPx`. It depends only on
  the aspect and the FOV, so the render-resolution scaling of TSR does not change it.
- `d` the **true** distance from the viewpoint in metres (the point shader's `dM = max(length(relM), 1)`, the CPU
  tier's `state.DistanceM`), never the far-depth-compressed render distance. The viewpoint, not the third-person
  camera, as both paths use it today; the camera offset (tens of metres) is negligible at the distances where the
  thresholds fall (kilometres, table 4.9).
- **Not** the quality-scaled lane focal (`mLaneFadeFocalRefPx`, `FadeFocalRefPx`): the cutoff is the same on every
  preset (D3).

### 4.3 Dot rule (D2)

`alpha(px) = clamp((px - 1) / (2 - 1), 0, 1)` with **`NPC_SHIP_HIDE_PX` = 1** and **`NPC_SHIP_DOT_FULL_PX` = 2**
(reference px):

| True size | Drawn |
|---|---|
| `px <= 1` (and NaN) | nothing: the vertex returns the hidden offset, no triangle is rasterized |
| `1 < px < 2` | the dot, brightness x `alpha` (linear, 0 at 1 px, 1 at 2 px) |
| `px >= 2` | today's dot: faction tint, soft mask, distance brightness, lane crossfade, unchanged |

The ramp is continuous, so the 1 px cutoff needs **no hysteresis**: a ship crossing it fades rather than pops. The
dot's final brightness is `alpha x distance brightness x lane crossfade` (`Shade.y` in the point vertex), so every
existing factor still applies on top.

**Drawn footprint (Q5, decided).** Today the billboard's incircle radius is `max(true radius, 1 device px)` (the 2 px floor in
device pixels). The floor goes. A billboard 1-2 px across can fall between pixel centres and shimmer as it moves, so the
design keeps a **rasterization floor of 1 device pixel** (`NPC_POINT_RASTER_MIN_DEVICE_PX`, the precedent is
`GLOW_MIN_DRAWN_PX` and `RIBBON_MIN_WIDTH_PX`) with the brightness still taken from the true size. At 1080p and above a
drawn dot is at least 1 reference px = 1 device px, so the floor never enlarges anything there; at 720p (1 reference px
= 0.67 device px) it widens the dimmest dots (alpha under about 0.5) to 1 device px. Without the floor the dimmest dots
would drop out unevenly at 720p.

**Dot size cap (Q13, decided after the adversarial review).** With the quality scale (Q12) a ship stays a dot up to
its scaled hull switch (40 px on Low), and drawn at its true size the dot would grow to a 40 px disc. So the drawn size
is `min(true size, NPC_POINT_DOT_MAX_PX)` with `NPC_POINT_DOT_MAX_PX` = 4 reference px (= `NPC_HULL_DOT_PX`,
static_asserted), converted to device px and floored at the raster floor: `SOLNP_CappedSizePx` in `SOLNpcPoint.ush`
(mirror `SOLNpcPointMirror::CappedSizePx`), the cap passed as `NpcParams6.x` (`SOLNpcPixel::PointParams6`). On Epic a
dot is at most 4 px before it becomes a hull, so the cap never acts there; the alpha and the 1-2 px ramp are untouched
and the jump map's dot (Q9) is not capped. The Demo "Hull from (px)" row does not move the cap.

### 4.4 Where the dot rule runs

Both point layers (the GPU far layer in rail mode and the CPU point layer in CPU mode) draw with the same material,
`M_SOLNpcPoints`, so **one shader function covers rail, CPU, cluster, battle and lane points**:

- New `Shaders/Private/SOLNpcPoint.ush` (`SOLNP_` prefix): `SOLNP_FocalRefPx`, `SOLNP_ShipSizePx`,
  `SOLNP_DotAlpha`. The vertex node of `M_SOLNpcPoints` (`VERTEX_HLSL` in `Tools/Npc/create_npc_assets.py`) includes it
  and calls it after `dM` is known; a dot alpha of 0 returns `hiddenOffset` before the billboard is built.
- New C++ float mirror `Npc/SOLNpcPointMirror.h` (`SOLNpcPointMirror`), statement for statement, the pattern of
  `SOLNpcTrafficMirror.h` and `SOLNpcTunnelMirror.h`, with explicit NaN guards in both (HLSL `saturate(NaN)` is 0 but
  `FMath::Clamp(NaN, 0, 1)` is 1, so neither side may rely on clamp for NaN).
- New pure double math `Npc/SOLNpcPixelMath.h/.cpp` (`SOLNpcPixel`) for the CPU decisions (render tier radii, far
  layer skip) and the double reference of the mirror tests.
- Material parameters: `NpcParams2.x` changes meaning from "min pixels" (2) to the raster floor in device px (1, Q5) and
  `NpcParams2.y` from half the hull length to `D / 2`; a new `NpcParams5` = (hide px 1, full px 2, reference height
  1080, map flag for Q9). Today's size code sits inline in `VERTEX_HLSL`, which is baked into the asset, so this needs
  **one rebuild of `M_SOLNpcPoints`** (`-SOLRebuild -SOLOnly=M_SOLNpcPoints`, editor closed, approved in Q8). Afterwards the size
  rule lives in the `.ush` and later tuning needs no rebuild.

### 4.5 Render tier by pixels (D5)

The tier selection itself (`SOLNpcScale::SelectRenderTier`, `SelectTierIndex`, the budgets) is unchanged. What changes
is where its radii come from: `USOLNpcSubsystem` derives them **from pixel thresholds each frame**, so distance is only
a derived figure.

1. `f` from `ViewCamera` (4.2).
2. Nominal radii `R_mesh = SOLNpcPixel::PixelRadiusM(D, f, NPC_MESH_MIN_PX)` and `R_hull = PixelRadiusM(D, f,
   NPC_HULL_DOT_PX)`, where `PixelRadiusM = D f / px` is the distance at which the ship is exactly `px` across.
3. Capped by the Mid radius as today, then shrunk by `BudgetedRadiusM` against the mesh budget (32) and the hull budget
   (20,000): the fixed pools stay as **hard limits only**, documented as limitations (Q6).
4. `SelectRenderTier(current, d, R_mesh, R_hull, RenderHysteresis)` as today.

In pixel terms: a ship is promoted to the hull tier when it is more than `NPC_HULL_DOT_PX` across and keeps its hull
down to `NPC_HULL_DOT_PX / (1 + h)` (h = `RenderHysteresis`, default 0.1); the same for the mesh tier with
`NPC_MESH_MIN_PX`. **Decided (Q1, Q2):** `NPC_HULL_DOT_PX` = 4 (kept down to 3.64 px) and `NPC_MESH_MIN_PX` = 16
(today's 2 km mesh radius is 16.6 px for `D` at 90 degrees; kept down to 14.5 px). **Quality scaling (Q12, decided):**
both thresholds are multiplied by the shared quality factor (`RIBBON_VANISH_PX_BY_QUALITY[preset] / [Epic]`: Low x10,
Medium x4.6, High x2.2, Epic and Cinematic x1) before `PixelRadiusM`; the 1 px cutoff is never scaled (D3).

- **Hull floor removed:** `UpdateCpuShips` draws the instanced hull at its true length (`sizeCm = lengthCm`); no
  `NPC_HULL_MIN_PIXELS x pixelPerCm x distanceCm` term.
- **A hull is never under the full-brightness dot size:** `NPC_HULL_DOT_PX / (1 + 0.5)` (the Demo maximum hysteresis)
  is 2.67 px >= `NPC_SHIP_DOT_FULL_PX`, a `static_assert`, so mesh and instanced hulls never need the 1-2 px ramp and
  never fall under 1 px (one frame of FOV change between the tier pass and the draw is the only exception, and the hull
  is then still drawn at its true size). The hull threshold is a Demo row (Q6), so the guarantee lives in the
  option's descriptor: its minimum is 3 px (3 / 1.5 = 2 px = full) and the sanitize rule keeps the mesh threshold at or
  above it, so no setting can put a hull under the dot's full size. A quality scale (Q12) only raises the thresholds,
  so it keeps the guarantee.
- **Look at the switch:** a 4 px hull hands over to a 4 px full-brightness dot (as it handed over at 3 px before). The
  plume glow is already gone there (4.7), so nothing else pops.
- **Hand-over blend start moves out to the 1 px distance:** the lane ship CPU point hand-over blends from the
  mirror's float position at the Mid radius to the double position at `hullStartM`
  (`SOLNpcTrafficMirror::CpuPointHandOver`), today the instanced radius (200 km). Left at the new hull radius (about
  8 km) the blend weight at 33 km would be `SmoothStep(8.3 km, 2,000 km, 33 km)` = 0.0005, and the float position is
  off by tens of km (hundreds on the outer lanes, `GAME_MECHANICS.md`), so a visible dot would sit tens of metres to
  over 100 m (1-4 px) away from its icon and flame, which use the double position. So `hullStartM` becomes
  `max(hull radius, PixelRadiusM(D, f, NPC_SHIP_HIDE_PX))` (still capped by the Mid radius in the hand-over): every
  dot that can be seen is drawn at its double position, and the blend only acts on ships too small to draw.
  **Review fix (H1, 2026-11-27):** the blend weight is keyed to the mirror's float length, which on an outer lane is
  tens to hundreds of km off, so a dot at 9 km could still be blended toward a mirror position 100 km away (tens of px
  off, jumping at the hull demotion). The weight is now 0 (the double position) whenever the **double** distance is
  under `hullStartM`, and the mirror-keyed smoothstep beyond it, so the Mid radius hand-over still lands on the
  mirror's position; the step where the double distance crosses `hullStartM` happens at the 1 px cutoff or the hull
  switch, where it is not seen (`SOLTest.NpcRail.CpuPointHandOverInside`, smoke `V5-quarter-handover-inside` and `V5-earth-end-handover-inside`).
  `-SOLSmokeNpcTraffic` and the thruster smoke read the instanced radius and must be re-baselined (plan 54f).
- **Demo page (Q6, decided):** the `MeshRadiusM` and `InstancedRadiusM` rows (and their `sol.Npc.Option` names, the
  sanitize rules and the pinned tests in `NpcScaleMathTest` / `SOLMenuStateTest`) no longer drive anything. They are
  replaced by "Mesh from (px)" (16) and "Hull from (px)" (4), the budget and hysteresis rows stay.

### 4.6 GPU far layer and lane ships

- **Nothing beyond the Mid radius can reach 1 px at a normal FOV.** The far layer draws only ships at
  `length(relM) >= P0.y` (the Mid radius, 2,000 km by default), where `px <= f D / R_mid` = 0.0165 px at 90 degrees. A
  far point could only show if the Mid radius shrank under about 33 km (the Mid budget would need 100,000 ships within
  it) or the FOV narrowed under about 2 degrees. So in flight the far layer draws nothing. **Skip:** each frame
  `ASOLNpcVisuals` hides the far layer component when `SOLNpcPixel::FarLayerCanDraw(D, f, R_mid, NPC_SHIP_HIDE_PX)`
  is false (exact, not a heuristic: it is the shader's own rule applied to the nearest ship the layer may draw), and
  shows it again the frame it becomes true. `SetVisibility` only on a change. This removes the far layer's whole
  vertex cost (1M ships x 3 vertices plus the rail evaluation) in normal flight. **While the map flag is set (Q9,
  the map keeps today's dots) the skip is bypassed** and the layer draws as today.
- **Lane ships:** the GPU lane branch and the CPU hand-over are unchanged; the dot rule runs after them in the same
  vertex code, so a lane ship hidden by its slot window, its active flag or the lane crossfade stays hidden, and a
  shown one also needs `px > 1`. Inside a lane tunnel (SDD 49 Q5, "the ships show as points again") only the lane ships
  at least 1 px across now show; the in-game text must say so (section 10).
- **Distance brightness** (`NPC_POINT_BRIGHTNESS_REF_M` 2,000 km, exponent 0.5, floor 4%) stays in the shader but is
  1.0 for every ship that can still be seen (all within about 33 km), so it is dead in practice. Recorded as legacy
  debt, not removed (non-goal).

### 4.7 Thruster flames and glow (D7, amends SDD 51 Q27)

Today plumes and flames exist only on hull-drawn ships (SDD 51 Q27): `USOLNpcTierProcessor::CollectChunk` computes the
heading, top and `ThrustLocalG` only for non-point ships, and `UpdateCpuShips` feeds `ASOLThrusterRibbons` only from
the mesh and instanced branches. With hulls ending near 4 px that coupling would cut flames off well inside their own
visible range:

| Piece | Own rule (SDD 51) | Where it ends (90 degrees, `f` 960) | Against the hull switch (4 px = 8.3 km) |
|---|---|---|---|
| Flame of the hardest-burning lane ship (1.65 G: `L_p` 9.66 m, flame 29.0 m) | Epic: full at 4 px, gone at 1 px of flame length | full to 7.0 km, gone at 27.8 km | still at about 0.7 alpha at the switch: **would pop** |
| Nozzle glow (2.5 x 1.5 m = 3.75 m) | alpha 0 at 0.5 px, 1 at 1 px of diameter | gone at 7.2 km | glow px = hull px x 3.75 / 34.47; at 4 px hull the glow is 0.44 px, **already 0** |
| Gas bursts | not built (#55) | - | - |

Both ratios are independent of the FOV and resolution (all sizes scale with `f`), so the comparison holds everywhere.
*Note (2026-11-27): the kilometre figures in this section and in 4.9 assume a focal length `f` of 960 reference px (90 degrees, 16:9). The game's actual device focal length is lower than 960, so the real distances are shorter, in proportion to `f` (for example 33 km at 960 becomes 23 km at 672, the 110 degree case); the pixel thresholds themselves are exact.*

The flame figures are from the current code (`SOLThruster::PlumeLengthM` = 30 m x (1.65 / 6.8033)^0.8,
`RibbonPixelLength` = 3 `L_p` f / d, `RibbonPixelFade` linear from `V` to 4 `V`, `V` = 1 px at Epic); SDD 51 line 494
(1.03 px at 22 km for 1.63 G) gives a lower figure than this formula (1.25 px), so 54e re-checks them in game. The
conclusion (the flame is still lit at the hull switch) holds either way.

**Decided (Q7):**

- **Flames follow their own rule on every CPU ship with thrust data, however the hull is drawn** (mesh, instanced,
  dot or hidden). `CollectChunk` computes heading, top and thrust for any CPU ship inside `PlumeGateM` (already
  pixel-derived, `SOLThruster::NpcThrustGateM`), dropping the `RenderTier != Point` condition; `UpdateCpuShips` reserves
  ribbon feed slots for point-tier ships with a non-zero thrust and feeds them from the parallel pass at their double
  position. The set of ships that can grow a flame is the same as today (today every ship inside the gate was a hull),
  so the flame cost does not change.
- **The glow stays a hull section** (per-instance custom data on the hull ISM): by the numbers above it is invisible
  before the hull becomes a dot, at any FOV, so a dot or hidden ship needs no glow path.
- A flame may show without its ship near the end of its range (a 29 m flame against a 34.5 m hull: the flame is gone at
  27.8 km, the hull at 33.1 km, so in practice they end together); that is the intended "own rule".
- SDD 51 section 4.5 / Q27 and `GAME_MECHANICS.md` ("never a dot") are amended (section 10).

### 4.8 Exemptions and what does not change

- **The player ship** is not an NPC and is drawn by `ASOLShipPawn`; untouched.
- **Selected NPC:** not exempt (D6, Q3 final): a selected NPC under 1 px is hidden like any other; the lock marker
  shows where it is. No selected-index plumbing into the material.
- **Selection, radar, map markers, overlay icons** read `GetCpuShips` and `bHidden`; `bHidden` is not touched, the
  cutoff lives only in the draw (the shader's hidden offset and the hull tier choice), so a hidden ship is still found,
  ranked and selectable (D4). Ship icons for hidden ships still show where they did.
- **Jump map:** only the lane lines check `IsMapOpen`; the point layers draw in the map view too (to be confirmed by
  the 54d smoke). At map scale every NPC is far under 1 px, so the rule as written would hide every NPC dot on the map.
  **Decided (Q9):** the map keeps today's dots (`NpcParams5.w` = 1 switches the shader back to a 2 px dot, no cutoff,
  while the map is open, and the far layer skip of 4.6 is bypassed while it is set). The 2 px map dot is
  `NPC_SHIP_DOT_FULL_PX`, so the constant `NPC_POINT_MIN_PIXELS` is still deleted.
- **Bolts and effects** (`ASOLNpcCombatVisuals`) are not ships: unchanged. Their 20 km visual radius is inside the
  1 px distance (33 km at 90 degrees, 23 km at 110 degrees), so no bolt comes out of an invisible ship at the default
  options.

### 4.9 What the numbers mean (`D` = 34.467 m)

| Threshold (reference px) | Distance at 90 degrees (`f` 960) | at 110 degrees (`f` 672.2) |
|---|---|---|
| 16 px, mesh promoted (Q1) | 2.07 km | 1.45 km |
| 14.5 px, mesh kept (h 0.1) | 2.27 km | 1.59 km |
| 4 px, hull promoted (Q2) | 8.27 km | 5.79 km |
| 3.64 px, hull kept (h 0.1) | 9.10 km | 6.37 km |
| 2 px, dot at full brightness | 16.5 km | 11.6 km |
| 1 px, hidden beyond | 33.1 km | 23.2 km |
| Largest far point at the 2,000 km Mid radius | 0.0165 px | 0.0116 px |

Default battles (5-20 km, 3 km radius) stay visible: hulls inside 8 km, full dots to 16.5 km, fading dots to 23 km
(1.44 px). Today the same ships were hulls to 200 km. At 4K the same rows hold (reference px), each reference px being
2 device px; at 720p 0.67 device px.

### 4.10 Frame flow

Unchanged order (`USOLNpcSubsystem` tier passes, then `ASOLNpcVisuals::Redraw` on `OnNpcsUpdated`):

1. Evaluate (distances), budgets: **new:** nominal render radii from `PixelRadiusM` before `BudgetedRadiusM`.
2. Select (render tier with the same `SelectRenderTier`), Mid, Full AI.
3. Collect: **changed:** heading and thrust for every CPU ship inside the plume gate.
4. Visuals: points (texels as today), hulls at true size (**no floor**), ribbons fed for point-tier ships too
   (**new**), far layer shown or hidden by `FarLayerCanDraw` (**new**), point material parameters `NpcParams2` /
   `NpcParams5` (**changed / new**).
5. GPU: the point vertex computes `px`, returns the hidden offset at `alpha = 0`, scales `Shade.y` by `alpha`.

## 5. Performance

- **GPU, points:** per vertex a few multiplies, one divide and two compares after the existing rail evaluation;
  negligible beside it, and a hidden dot rasterizes nothing (no pixel work for the sub-pixel majority). The far layer
  is skipped entirely in normal flight (4.6): the biggest win of the ticket. Measure: points draw and far layer GPU ms
  at 1080p and 4K with 1M NPCs, before and after (`stat gpu`, a `-SOLSmokeNpcLanesProfile`-style capture); gate: no
  increase at any view, a decrease at the spawn view expected.
- **CPU, tiers:** two `PixelRadiusM` calls per frame; the Select pass is unchanged.
- **CPU, hulls:** the instanced hull set shrinks from every Mid / Full ship within 200 km to those within about 8 km;
  the `BatchUpdateInstancesTransforms` upload with previous transforms (TECH_DEBT, about 1 ms game thread plus about
  1.5 ms proxy at 20k hulls) shrinks with it. The CPU point texels grow by the same ships (16 bytes each, already
  uploaded in one region). Measure `STAT_SOL_NpcRedraw` and `ASOLNpcVisuals::UploadShips`.
- **CPU, flames:** the same ships as today inside the pixel-derived gate (4.7); measure the flame CPU gate at the spawn
  view (TECH_DEBT: 0.054 ms against 0.05 ms) and report the delta; it must not grow.
- **No allocation per frame:** the ribbon feed array is sized by the serial pass as today (`BeginNpcFeeds`), the far
  layer flag is a bool, the new parameter vector is set with the others.
- **1M entities:** nothing per entity is added on the CPU. On the GPU the per-vertex cost is O(1).
- **VR floor (record only):** no new per-pixel cost; at one XR-4 eye (3840x3744) one reference px is about 3.5 device
  px, so the 1-device-px raster floor never applies there. Re-measure the points draw at one-eye resolution when the VR
  gate is built (`ToDo/vr-performance-gate.md`).

## 6. Module edges

No new module edge. `Npc/SOLNpcPixelMath` uses `SOLConstants.h` only; `Npc/SOLNpcPointMirror.h` is header-only (the
tunnel and traffic mirrors' pattern); `USOLNpcSubsystem` already reads `Visuals/SOLThrusterMath` (`FocalRefPx`). The new
shader file goes under `Shaders/Private/` (the project's shader directory) and into the material by
`create_npc_assets.py`.

## 7. Persistence

None. No setting, key binding, save field or config entry changes. The Demo options are session-only (Q6 changes
rows, not saved data). `SAVE_VERSION` 13, `KEY_BINDING_LAYOUT_VERSION` 10.

## 8. Risks

- **R1, the far layer goes dark.** The million-ship "sea of points" is no longer visible in flight (Q10). The Demo
  page's scale is then shown by the stats overlay, the radar and the tunnels; #56 makes hidden ships findable.
- **R2, shimmer of 1-2 px dots** while they move: mitigated by the raster floor (Q5) and by the low brightness there.
- **R3, a frame of disagreement** between the CPU tier pass and the GPU (FOV change between them): a hull drawn for one
  frame at 3.6 px instead of a dot; invisible.
- **R4, smokes that counted hull frames at 200 km** (`-SOLSmokeNpcTraffic` V2, the thruster mid band) change their
  expectations: re-baseline in 54f, do not loosen checks silently.
- **R5, material rebuild** touches a binary asset; done only with the user's approval (Q8), by script.

## 9. Test plan

Contract: [`Plans/54-hide-subpixel-ships-contract.md`](../Plans/54-hide-subpixel-ships-contract.md); tests in
`Source/SOLTest/Tests/NpcPixelTest.cpp` (`SOLTest.NpcPixel.*`), written by a separate author from the contract only.

1. **Pure math** (`SOLNpcPixel`): `ShipSizePx`, `DotAlpha`, `IsDotDrawn`, `PixelRadiusM`, `BoundingDiameterM`,
   `FarLayerCanDraw`; exact values at 1.0 and 2.0 px, the ramp's midpoint, NaN / infinite / zero / negative inputs,
   720p / 1080p / 4K invariance (the same `px` at the same FOV and aspect), FOV dependence.
2. **Tier composition:** `SelectRenderTier` with radii from `PixelRadiusM` puts ships just above / below each pixel
   threshold in the right tier, keeps them through the hysteresis band and drops them below it (thresholds passed as
   parameters until Q1 / Q2 are answered).
3. **CPU / GPU mirror parity:** `SOLNpcPointMirror` (float) against `SOLNpcPixel` (double) over a sweep of distances,
   FOVs and resolutions (tolerance 1e-4 relative on `px`, 1e-5 on alpha, and the same hidden / drawn decision away from
   the 1 px edge by more than the tolerance); `SOLNpcPointMirror::FocalRefPx` equals `SOLNpcTunnelMirror::FocalRefPx`
   and `SOLThruster::FocalRefPx` for the same view. As with the tunnel and thruster mirrors, the shader itself is
   checked by the smoke's screenshot case (6d), not by a unit test.
4. **Constants:** `static_assert`s (hide < full, hull threshold / 1.5 >= full, mesh > hull) and a test that
   `NPC_HULL_BOUNDING_DIAMETER_M` equals `BoundingDiameterM` of the placeholder hull it was taken from.
5. **Modified tests (re-validated, not just re-run):** `NpcScaleMathTest` (render radii defaults and sanitize rules,
   per Q6), `SOLMenuStateTest` (Demo rows, per Q6), `ThrusterMathTest` / thruster smoke (Q27 amendment), any test that
   names `NPC_POINT_MIN_PIXELS` or `NPC_HULL_MIN_PIXELS` (both constants are deleted).
6. **Smokes (editor, PIE):** a new `-SOLSmokeNpcPixel`: (a) every CPU ship's render tier matches its true px against
   the thresholds and hysteresis; (b) no instanced or mesh hull under `NPC_HULL_DOT_PX / (1 + h)`; (c) the far layer's
   visibility equals `FarLayerCanDraw` (and a narrow-FOV or shrunken-Mid case where it turns on); (d) a GPU check by
   screenshot: one battle ship placed at true sizes 0.8, 1.5 and 3 px (distance options), its projected pixel black,
   about half and full brightness against the mirror's alpha; (e) a hidden ship is still selectable and keeps its
   overlay icon; (f) a dot ship with thrust inside the gate has a flame (Q7). Re-baseline `-SOLSmokeNpcTraffic`,
   the thruster smoke, `-SOLSmokeNpcLanes`, `-SOLSmokeNpcTunnel`, `-SOLSmokeLaneMerge`, `-SOLSmokeAutoLanes` at
   1280x720 and 1920x1080, and the 1M ramp.
7. **Visual check:** PIE screenshots of a battle at 5, 10, 20 and 30 km (hull, dot, fading dot, nothing) at 1080p and
   4K; deleted after use except the README gallery shots.

## 10. Docs to sync (in the parts that change them)

- `Docs/GAME_MECHANICS.md`: the Render tiers row (no 2 px / 3 px floors, pixel tiers, the 1 px rule, budgets as
  limitations), the NPC thruster flame row ("never a dot" goes), the Lane tunnel row (points inside a tunnel only from
  1 px).
- In-game info text (`Menu/SOLMenuText.h`): the Demo paragraph ("drawn as points, or not at all along a far lane" ->
  points while at least about a pixel across, smaller ships not drawn, still on radar and selectable), the plume
  sentence (dot ships too), the tunnel paragraph ("its ships show as points again"), and the Demo row labels (Q6).
- `Docs/SDDs/51-thruster-plume.md` section 4.5 / Q27 amendment note pointing here; `Docs/SDDs/11-scale-and-performance-demo.md` render tier note.
- `Docs/TECH_DEBT.md`: rewrite the legacy-LOD item (render tiers converted; AI tiers and budgets remain), add the dead
  distance brightness falloff of the far layer and the distance-based NPC bolt radius if not listed.
- `Docs/ARCHITECTURE.md`: the new math file, mirror header and shader file; the far layer skip in the NPC draw flow.
- `Docs/Plans/STATUS.md`: #54 In progress (done in this draft), #56 To do (done in this draft); #54 to Done at the end.
- README gallery: 2-3 shots (54g). Metrics refreshed before each commit.

## 11. Alternatives considered

- **Distance radii recomputed from the old 2 km / 200 km at the reference FOV:** keeps a distance control; rejected by
  the pixel rule.
- **CPU-side cull of sub-pixel CPU points** (no texel): would save a little upload but makes the CPU the decider for
  points while the GPU decides far points; one decider (the shader) is simpler and the saving is small. Possible later.
- **Hysteresis at 1 px:** not needed, the ramp is continuous.

## 12. Open questions

None. Q1-Q12 were answered by the user on 2026-11-27 and moved into the resolved-decisions table (section 3); Q13 (the dot size cap) was decided by the user on 2026-11-27 after the adversarial review.

## 13. Change log

- 2026-11-27: drafted from the grill-me decisions D1-D8; Q1-Q12 open.
- 2026-11-27: user approved Q1-Q12 (Q1 16 px, Q2 4 px, hysteresis 10%, Q3 selected NPC hidden like any other, Q10 far layer dark accepted, the rest as recommended); all moved into section 3, section 12 emptied, proposal wording made decisive.
- 2026-11-27: adversarial review fixes: Q13 (dot size cap at 4 reference px, user-approved; new `NpcParams6`, second rebuild of `M_SOLNpcPoints`) added to section 3 and 4.3; the lane hand-over keyed to the double distance inside `hullStartM` (H1, 4.5).
- 2026-11-27: implemented (parts 54a-54g): status updated, the kilometre figures of 4.7 and 4.9 noted as assuming focal 960, SDD 51 Q27 and SDD 11 render tier notes pointed here.
