<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 51: Placeholder main thruster plume

**Issue:** [#51](https://github.com/DieSlower/SOLTest/issues/51). Plan:
[`Plans/51-thruster-plume-plan.md`](../Plans/51-thruster-plume-plan.md). Builds on SDD 50 (section 4.12, the per-ship
thrust state `LaneThrustAt`; D21, the flip window; D22, the War power move; D24 / D25, the hull basis), SDD 11 (the
NPC AI and render tiers, the instanced placeholder hulls), SDD 18 (the player flight model and thrust envelope) and
SDD 52 D8 (1080p-reference pixel sizes).
**Persistence change: none planned.** `SOL::Settings::SAVE_VERSION` stays 13 and `KEY_BINDING_LAYOUT_VERSION` stays 10.
No setting, key binding or `UPROPERTY` of a save class changes unless the user picks the "new setting" answer to Q12, which
would need its own approval, a version bump and a migration.
Status: **SDD drafted 2026-11-20 from the grill-me decisions D1-D10 (user, 2026-11-20). Open questions Q1-Q26
(section 11) are not answered: everything marked *provisional* below waits on them, and the test contract (section 7)
is frozen only once they are answered.** No code, no assets.

## 1. Problem

No ship shows that it is thrusting. The player ship has two always-on orange engine-glow spheres
(`SHIP_ENGINE_GLOW` in `Ship/SOLShipPawn.cpp`) that do not react to thrust, and NPC hulls (a blue cone for faction 0,
an orange flat box for faction 1, both 30 m) show nothing at all, although #50 gave every lane ship a real thrust
profile (accelerate, coast, a 60 s flip, brake, U-turns). Issue #51 asks for a placeholder main thruster effect on the
player ship and on the NPC ships, driven by that thrust state, cheap enough for thousands of hulls.

## 2. Decisions (user, grill-me 2026-11-20)

- **D1: Niagara plume with a blue-white core.**
- **D2: Shown on the player ship and on NPC hulls in the Mid and Full tiers** (the instanced placeholder hulls and the
  mesh tier), **not on the Far points.** (How "Mid and Full" maps onto the render tiers: Q5.)
- **D3: Size and brightness are driven by the actual thrust.** Zero thrust and the lane flip coast mean no plume. NPCs
  consume #50's per-ship thrust state; the player consumes its flight state.
- **D4: Colour from ONE intensity scalar.** Blue-white below 3 G, a smooth ramp from 2.5 G to 3 G to yellow-white,
  red-orange reached at 6.8 G; **War power is always fully tinted.** Red-orange is accepted although it resembles
  faction 1's orange at range. The gradient is done **inside the material from one scalar** (Niagara colour curves
  cannot be user parameters), delivered through per-instance custom data or a Niagara user parameter.
- **D5: Look "B with shock diamonds".** An **emissive cone mesh with panning noise streaks**; a **shock-diamond
  material pattern** on the cone: bright knots along the centre line, rhombus-shaped, **spacing 2.0 x nozzle radius x
  (1 + 0.6 x brightness)**, **each knot 18% dimmer** than the one before, **in the first 80% of the cone**, no
  particles; and a **soft nozzle glow sprite** (at least **1.2 px**, **nearest ships only**).
  **Cone length = hull length x (G / 6.8)^0.8** (30 m placeholder hull: 6.5 m at 1 G, 15.6 m at 3 G, 30 m at 6.8 G;
  the plume scales with hull length). **Brightness = (G / 6.8)^0.5.**
- **D6: The plume is a RIGID cone on the nozzle axis** and swings with the ship; it does not bend.
- **D7: A streak.** A **real curved world-space ribbon** built from a **per-nozzle position history ring buffer (16-32
  points)**, **3x the plume's length**, **fading with age**, on **near ships only**: **the player plus the nearest 32
  ships within 5 km** as the default, **scalable by graphics quality** (the Full tier is up to 4,000 ships and 50 km:
  too costly). The ribbon is **LODed by on-screen PIXEL SIZE, not distance**. The user wants to **compare how ribbons
  look at 50 vs 20 vs 10 km before the radii are fixed** (a tuning / mock-up step in the plan). Ships without a ribbon
  get the cone and the diamonds only.
- **D8: Nozzle count is per-hull data:** 1 for NPCs, 2 for the player ship. Real ship models define their own nozzle
  sockets later.
- **D9: No sound.** Visual only; audio is a later ticket.
- **D10: No paid pack content in the CC0 repo.** The Fab packs looked at (SF Studio "Thrusters & Jet Engines VFX"
  $10.99, Shogun Games "Rocket Thruster Exhaust FX", EffectsCreator "Jet Thruster FX") are reference only. Research
  notes and the mock-ups (`thruster_refs/report.md`, `thruster_mock`, `thruster_mock2`) are in the grill-me session's
  scratchpad, not in the repo.

## 3. What exists today (code survey, HEAD `4e8a91f`)

| Piece | Where | Facts that matter here |
|---|---|---|
| Lane thrust state | `Traffic/SOLLaneTrafficMath` (`SOLLaneTraffic::LaneThrustAt(rail, profile, t)` with the ship key), SDD 50 section 4.12 | Kinds Accelerate (`alpha` along travel), Coast (0), Brake (`alpha` against travel, hull flipped), Turn (centripetal), Flip (0, hull rotating, 60 s); `FSOLLaneThrustState` carries `AccelMps2`, `Direction`, `FlipFraction`, `HullForward`. Pure, CPU side. SDD 50 notes for #51: the flame is off for the whole flip window and on again exactly when braking starts; in a turn the main engine is aligned with the thrust only at the apex, so the flame may scale by the thrust's component along `HullForward`. Legs report the **nominal** profile (the metric end-zone term is left out) |
| NPC G values | `SOLConstants.h` | `NPC_LANE_PEAK_ACCEL_G` = 1.1 x 1.5 = 1.65 G, times a per-ship effort 0.5-1.0, so lane NPCs thrust at **0.825-1.65 G**: always blue-white under D4 |
| Hull basis | `SOLLaneTraffic::LaneHullBasis`, `HullRotation`, `SOLNpcAi::HullDrawRotation` (D24) | `FSOLNpcCpuShip::HeadingDirection` / `HeadingTop` (Collect pass of `USOLNpcTierProcessor`); a Full ship draws its AI attitude, a Mid ship its rail heading; the tier processor evaluates `Motion` only for ships without an attitude |
| CPU ship record | `Npc/SOLNpcTierProcessor.h` `FSOLNpcCpuShip` | Position, previous position, faction, render tier, AI tier, heading, top, hidden, NPC index, rail distance. **No thrust field** |
| Tiers | `GAME_MECHANICS.md` AI / Render tiers | AI: Full within 50 km (budget 4,000), Mid within 2,000 km (budget 100,000), Far beyond. Render: real mesh within 2 km (budget 32, `NPC_MESH_POOL_SIZE` 256 pool), **instanced placeholder hull within 200 km (budget 20,000, never under 3 px, `NPC_HULL_MIN_PIXELS`)**, CPU points beyond, GPU far points beyond the Mid radius |
| NPC renderer | `Npc/SOLNpcVisuals.cpp` `UpdateCpuShips`, `UploadHulls` | One ISM per faction (`USOLNpcHullInstances`, fixed bounds), parallel transform fill, `BatchUpdateInstancesTransforms` with previous transforms; the mesh pool places up to 32 `UStaticMeshComponent`s; `PixelWorldCm` gives the world size of a pixel |
| Player thrust | `Ship/SOLShipFlightProcessor.cpp` (`stepOutput.ThrustAccelWorldMps2`, per substep), `FSOLShipStateFragment::GLoadG` | The achieved thrust acceleration is local to the processor's substep loop; `GLoadG` includes **surface contact**, so it is not a thrust measure. Forward 3 G, reverse 1.5 G, lateral / vertical 2 G, boost x3, total cap 9 G; War power pulses at 6.80 G (`WAR_POWER_PULSE_G`) and recovers at 1.5 G (SDD 50 D22) |
| Player hull | `Ship/SOLShipPawn.cpp` `SHIP_PARTS` | Primitive parts, about 20 m long (glow back at x = -8.05 m, nose tip at about +12 m); two engine cylinders at y = +-2.2 m, z = -0.2 m, radius about 0.8 m (scale 1.6 of the 1 m cylinder); two always-on emissive glow spheres at x = -8.05 m. **No hull-length constant** |
| Niagara in the repo | `Combat/SOLCombatVisuals`, `Npc/SOLNpcCombatVisuals` (`NS_SOLBolts` fed from arrays, pooled effects), `Visuals/SOLSpaceDustComponent`, ring layers | Array-fed systems and user parameters named in `SOLConstants.h`; no Niagara colour curve set from code |
| Assets by script | `Tools/Npc/create_npc_assets.py` (`-SOLRebuild`, `-SOLOnly=`) | The NPC materials (`M_SOLNpcPoints`, `M_SOLNpcHull`, `M_SOLNpcLanes`, `M_SOLNpcTunnel`) are built by this script, never by hand |
| Graphics quality | `Menu/SOLSettingsModel.h` `QualityPreset` (default 3) | Already persisted as the engine's overall scalability level (`GetOverallScalabilityLevel`), so reading it needs no schema change |

**Known debt not to re-report** (grep of `Docs/TECH_DEBT.md`): render-space Niagara and translucent effects have no
motion vectors and smear under TSR (line 29: fixed for ISMs with previous transforms, open for Niagara); the 20,000
instanced-hull upload cost (`BatchUpdateInstancesTransforms` about 1 ms game thread plus 1.5 ms proxy updates, line
35), which any per-instance plume data adds to; assets loaded only by a string path are not cooked (line 59); no ship
length reaches the lane math and every hull is the 30 m placeholder (line 106).

## 4. Design

Everything here follows from D1-D10. A value or rule the user did not give is marked *provisional* and points at the
open question that decides it (section 11). Pixel arithmetic uses 1920x1080, 90 degree horizontal FOV, focal length
`f` about 960 px.

### 4.1 Overview

Each thrusting ship on the player, Mesh or Instanced tier gets one rigid emissive cone per nozzle (D5, D6), aligned with
the nozzle axis, its length and brightness from the ship's main-engine G (D3, D5), its colour from one scalar (D4),
with the shock-diamond and noise-streak pattern in the material and a soft glow sprite at the nozzle. The player and
the nearest few ships also get a curved ribbon from a per-nozzle history (D7). Nothing moves differently, no gameplay
changes, nothing is saved.

### 4.2 Main-engine G (D3)

- **One number per ship per frame: `G_main` >= 0**, the main engine's thrust in G along the hull's aft axis:
  `G_main = max(0, a_thrust . HullForward) / g0`, where `a_thrust` is the ship's thrust acceleration (not gravity, not
  contact) and `HullForward` the nose direction. Below a cut-off (*provisional* 0.01 G) the plume is off.
- **NPC lane ships:** from `LaneThrustAt`. Accelerate and Brake give `AccelMps2 / g0` (the hull faces its thrust);
  Coast and Flip give 0 (D3: the flip window is dark, and the flame comes back exactly when braking starts); Turn gives
  the centripetal thrust's component along `HullForward` (SDD 50 section 4.12). Where it is evaluated and carried
  without extra per-frame cost per tier is Q2. *Provisional:* a `float MainThrustG` added to `FSOLNpcCpuShip` (a
  CPU-only render record, never saved, not the 32-byte GPU rail), filled in the Collect pass from the same basis
  evaluation that already produces the heading.
- **NPC non-lane ships** (cluster, battle, orbit rails) have no thrust model: *provisional* no plume (Q3).
- **NPC Full-tier ships** fly the Full AI, whose achieved acceleration is the natural source (Q2).
- **Player:** the flight processor's achieved `ThrustAccelWorldMps2`, projected on the nose. *Provisional:* a transient
  `MainThrustG` in `FSOLShipStateFragment` (a plain field, not a `UPROPERTY`, never saved), the time-weighted mean of
  the step's substeps, so War power pulses do not flicker (Q8). `GLoadG` is not used (it includes surface contact). Only
  the forward component lights the main plume; reverse, lateral and vertical thrust show nothing (Q7).

### 4.3 Plume scalars (D4, D5)

Let `x = G_main / G_REF` with `G_REF` = 6.8 G. Note that `WAR_POWER_PULSE_G` is 1.5 + 7.5 x sqrt(0.5) = 6.803 G, not
6.8: tied to it, the 30 m plume at exactly 6.8 G is 29.988 m; as a literal 6.8, a War power pulse sits just over the
Q6 clamp. Which one is Q23.

| Quantity | Rule (D5) | 30 m hull, 0.825 G | 1 G | 1.65 G | 3 G | 6.8 G |
|---|---|---|---|---|---|---|
| Cone length `L_p = L_hull x^0.8` | proportional to hull length | 5.55 m | 6.47 m | 9.66 m | 15.6 m | 30.0 m |
| Brightness `b = x^0.5` | | 0.348 | 0.383 | 0.493 | 0.664 | 1.0 |
| Colour | D4 ramp | blue-white | blue-white | blue-white | yellow-white | red-orange |

- **Above 6.8 G** (player boost reaches 9 G: `x` = 1.32, unclamped length 37.5 m, brightness 1.15): clamp or extrapolate
  is Q6. *Provisional:* clamp `x` to 1 for all three.
- **Colour scalar `c`.** *Provisional reading of D4:* `c = x` (clamped to [0, 1]); key points blue-white up to `c` =
  2.5 / 6.8 (0.368), a smoothstep to yellow-white at 3 / 6.8 (0.441), a ramp to red-orange at 1. The shape of the
  3 -> 6.8 G segment (linear in `c`, or in G) and the exact colours are not given (Q9). The gradient is evaluated in the
  material from `c` (D4).
- **War power:** while a War power order flies (`bAutopilotIgnoreLanes`), `c` = 1 whatever the G, so the recovery
  phase at 1.5 G shows a short red-orange plume. The length and brightness still follow `G_main`. That makes colour not
  a function of G alone, so the material needs `c` and `b` as two values, or an override flag (Q10).
- **Per-instance data** (*provisional*, D4): two floats (`c`, `b`); the length is in the instance transform or in a third
  float, depending on the rendering path (Q1).

### 4.4 Look (D5, D6)

- **Cone.** A unit cone mesh along -X from the nozzle exit, scaled to `L_p` long and a base radius equal to the nozzle
  radius (*provisional*; the flare angle is not given, Q11). Unlit, additive translucent, two-sided, no depth write, no
  shadow, no collision. Rigid on the nozzle axis (D6).
- **Core and falloff.** Blue-white core (D1) that tints by `c` toward the rim and tail (*provisional* layering: a white
  hot core along the axis, the D4 colour outside).
- **Noise streaks.** A panning, tiling noise texture along the cone's length (*provisional* pan rate proportional to
  `b`), generated procedurally by the asset script (CC0, D10), no imported texture.
- **Shock diamonds.** Knots on the centre line at `s_k = k x S`, `S = 2.0 r_n (1 + 0.6 b)`, `k` = 1, 2, ... while `s_k
  <= 0.8 L_p`; knot `k` has strength `0.82^(k-1)`; each knot is a rhombus in the cone's (axial, radial) plane
  (*provisional* rhombus half-length 0.35 S, half-width 0.6 of the local cone radius). Worked examples with the
  *provisional* nozzle radii of section 4.6:

  | Ship | G | `L_p` | `b` | `S` | Knots |
  |---|---|---|---|---|---|
  | NPC 30 m, `r_n` 1.5 m | 1.65 | 9.66 m | 0.493 | 3.89 m | 1 (3.9 m; the second at 7.8 m is past 80% = 7.7 m) |
  | Player about 20 m, `r_n` 0.8 m | 3.0 | 10.4 m | 0.664 | 2.24 m | 3 (2.2, 4.5, 6.7 m; strengths 1, 0.82, 0.67) |
  | Player, War power | 6.8 | 20 m | 1.0 | 2.56 m | 6 |

- **Nozzle glow sprite.** A camera-facing soft disc at the nozzle exit, brightness `b`, colour from `c`, drawn at least
  **1.2 px** (D5) on **the nearest ships only** (which set: Q13).
- **Pixel size.** At `f` = 960 px a plume is `960 L_p / d` px long: a 9.66 m NPC plume is 1 px at about 9.3 km and 3 px
  at 3.1 km; a 30 m plume is 1 px at about 29 km. Instanced hulls are drawn at least 3 px (a 30 m hull is inflated
  beyond about 9.6 km), so whether the plume follows the true or the drawn hull length is Q14. *Provisional:* the true
  length, the cone fading out below a pixel threshold (Q15).
- **Bloom.** The look relies on emissive values above 1 and the engine's bloom; whether it must also read with bloom off
  (VR, low quality) is Q16. **Hull lighting:** none for NPCs; whether the player plume lights its own hull is Q17.

### 4.5 Ribbon (D7)

- **History.** A ring buffer per nozzle of `N` samples (*provisional* 24, D7 range 16-32), sampled every `A / N`
  seconds where `A` is the ribbon's maximum age; each sample stores its emission time, the nozzle exit position and the
  nozzle axis at emission. A new owner of a ribbon slot starts empty and grows in; a ship that leaves the set keeps its
  ribbon until its samples age out (*provisional*).
- **The frame (Q4, blocks the contract).** A literal heliocentric or render-space history is degenerate: lane ships move
  at 20-300 km/s (330 m to 5 km per frame at 60 fps) while the ribbon is about 3 x 10-30 m, and the camera moves
  through render space at the ship's speed (the render origin is fixed between its 10 km rebases, TECH_DEBT line 29). *Provisional:* store the samples relative to the ship's current position in
  non-rotating (ecliptic) axes, and draw sample `i` at `p_i + (-axis_i) u age_i` with an exhaust drift speed `u = 3
  L_p / A`, so the ribbon is 3x the plume at full age (D7), curves when the ship turns (the flip, the player's own
  turns, the U-turns) and is immune to rebases. This is an interpretation of "world-space": the user decides.
- **Fade.** Alpha falls with age (*provisional* linear to 0 at `A`), times `b`; width from the nozzle radius at the
  nozzle to *provisional* 2x at the tail.
- **Who gets one.** The player (2 nozzles) always, plus the nearest `K` NPCs within `R` (D7 defaults `K` = 32, `R` = 5
  km), the count scaled by graphics quality (Q12). Selection per frame: a partial selection (`nth_element`-style, no
  allocation after `Reserve`) over the Mesh / Instanced CPU ships within `R` whose `G_main` > 0 or whose ribbon is
  still fading (*provisional*), ties broken by NPC index.
- **Pixel-size LOD (D7).** A selected ribbon draws only while its on-screen length `960 x 3 L_p / d` (1080p-reference,
  scaled by the viewport height as SDD 52 D8) is above a threshold (*provisional* 8 px, with a fade band, Q15). For
  equal hulls the rank by pixel size equals the rank by distance; with real hull lengths it differs, so the selection
  ranks by pixel size.
- **What the distances mean** (input to the D7 mock-up): a 1.65 G NPC ribbon (29 m) is 2.8 px at 10 km, 1.4 px at 20
  km and 0.56 px at 50 km; a player War power ribbon (60 m) is 5.8, 2.9 and 1.2 px. The mock-up step (plan 51e-0)
  shows these before `R` and the thresholds are fixed. Note: an 8 px threshold on a 29 m NPC ribbon is reached at about
  3.5 km, inside the 5 km radius, so with that threshold no NPC ribbon would show at 10, 20 or 50 km at all.
- **Ribbon parameters** (Q24): the maximum age `A` (no value given; *provisional* 1 s), `N` (*provisional* 24), the
  fade curve, the width, the enter / leave behaviour and the selection filter (`G_main` > 0 or a fading ribbon).

### 4.6 Per-hull nozzle data (D8)

- *Provisional:* a small constant table in `SOLConstants.h`, per hull kind: hull length, nozzle count, each nozzle's
  position and axis in the hull frame, nozzle radius. Placeholder entries: NPC faction 0 cone and faction 1 box, 1
  nozzle on the hull's aft centre, `r_n` 1.5 m (*provisional*); player, 2 nozzles at the engine cylinders' aft faces
  (x about -8.0 m, y +-2.2 m, z -0.2 m), `r_n` 0.8 m, hull length about 20 m (a new `PLAYER_HULL_LENGTH_M`). Real ship
  models later replace this with sockets (D8). Where this lives (constants vs a data asset) is Q18; it overlaps the
  ship-length debt (TECH_DEBT line 106).
- **The player's existing engine-glow spheres** are always on and orange, which contradicts D3 and D4 (Q19).

### 4.7 Rendering and instancing (Q1)

Options for the cones and glow sprites of up to 20,000 instanced hulls plus 32 mesh-pool ships (the user decides):

Under A and B the cones and glows are not Niagara, so D1's "Niagara plume" applies to the ribbons only; Q1 asks the
user to confirm that.

- **A. A plume section on the hull mesh** (recommended in Q1): the placeholder hull meshes get a second section (the
  cone and glow quad) with the plume material, scaled by world-position offset from per-instance custom data, so the
  hull's existing transform upload carries the plume and only the custom floats are added (`NumCustomDataFloats`, the
  mesh pool through custom primitive data). The hull ISM already has fixed bounds, so WPO growth is safe. Cost: the
  custom-data upload for every hull each frame, and one translucent draw per faction. The section inherits the hull's
  3 px inflation, so a true-length plume (Q14) needs the inflation factor per instance to undo it.
- **B. Separate plume ISMs or HISMs** (per faction or one shared): a second transform and previous-transform upload of up to
  20,000 instances, roughly doubling the TECH_DEBT line 35 cost.
- **C. An array-fed Niagara system** (the `NS_SOLBolts` pattern): positions, axes and scalars pushed as arrays each
  frame, a mesh renderer for the cones and a sprite renderer for the glows; no previous positions (TECH_DEBT line 29
  smear) and a CPU sim over every plume.
- **Ribbons** (D1, D7): a Niagara ribbon renderer fed from the history arrays of at most `K` + 1 ships (about 66
  nozzles at the default), *provisional* one system for all ribbons.

### 4.8 Assets (Q20)

*Provisional:* `M_SOLThrusterPlume` (cone, diamonds, streaks, colour ramp from `c`, WPO length), `M_SOLThrusterGlow`
(sprite), the cone / glow geometry (a section added to the NPC hull meshes under option A, or `SM_SOLThrusterCone`), a
procedural noise texture, and `NS_SOLThrusterTrails` (ribbons). All are built by a script in the style of
`Tools/Npc/create_npc_assets.py` (a new `Tools/Fx/create_thruster_assets.py` or an extension of that script), never by
hand, CC0 only (D10). **Creating or rebuilding any asset needs the user's approval** (Q20). Paths go in
`SOL::Paths` (STYLE_GUIDE section 7); their folders are added to the cook list known gap (TECH_DEBT line 59).
The material's ramp, brightness and knot formulas are mirrored by C++ pure functions with tests (the
`SOLNpcTunnelMirror.h` pattern: a `.ush` included by a Custom node, pinned by tests).

### 4.9 Per-frame cost (provisional, measured in 51f)

- CPU: one float per CPU ship in the Collect pass, the custom-data writes in the existing parallel placement pass, the
  near-set selection over ships within `R`, and at most `K` + 1 history pushes. Target: **NPC redraw delta at most 0.10
  ms** at the Earth spawn view with 1M NPCs and default budgets (Q21).
- GPU: additive translucent cones mostly a few pixels large; the near player plume and ribbon can cover a large part of
  the screen in third person. Target: **at most 0.20 ms GPU at 1920x1080** in a busy lane view and in the player-boost
  view (Q21).
- VR: recorded only (see section 8).

### 4.10 What does not change

No flight, AI, lane or combat behaviour; no rail or GPU traffic layout (`FSOLNpcRail` stays 32 bytes); no far-point
shader; no save data, settings or key bindings (unless Q12's "new setting" answer is chosen); no audio (D9).

### 4.11 Pure functions (provisional names, `Visuals/SOLThrusterMath.{h,cpp}`, namespace `SOLThruster`)

`PlumeFraction(G)`, `PlumeLengthM(hullLengthM, G)`, `PlumeBrightness(G)`, `PlumeColourScalar(G, bWarPower)`,
`PlumeColour(c)` (the C++ mirror of the material ramp), `ShockSpacingM(nozzleRadiusM, b)`, `ShockKnots(...)` (positions
and strengths), `LaneMainThrustG(thrustState)`, `PlayerMainThrustG(thrustAccel, forward)`, `FPlumeHistory` (ring
buffer: push, sample by age, reset), `RibbonPointM(sample, ageS, driftMps)`, `PlumePixelLength(lengthM, distanceM,
viewportHeightPx, fovDeg)`, `RibbonLodAlpha(px)`, `SelectNearestRibbons(...)`, `RibbonCountForQuality(preset)`.

## 5. SDD sections that change

- SDD 50 section 4.12: "#51 decides how a flame uses it" gets the answer (section 4.2 here).
- SDD 11 render tiers: the Mesh and Instanced tiers draw plumes; the CPU points and the Far points do not.
- SDD 18 / 19: the player's engine glow (Q19).

## 6. Player-facing text and docs

- `Docs/GAME_MECHANICS.md`: a new "Thruster plume" row (what drives it, G values, colours, who gets a ribbon, the
  graphics-quality scaling) and notes in the Lane traffic row (dark through the flip window) and the War power row
  (always red-orange).
- In-game Info screen: one line in the flight / ship section saying the plume shows main-engine thrust and turns
  yellow then red-orange at high G, red-orange during War power (Q22).
- README: 2-3 gallery screenshots (player at 1 G, 3 G and War power, a lane with flaming and flipping NPCs) after the
  ticket, at 1920x1080.
- `Docs/ARCHITECTURE.md`: the plume renderer and the thrust field in the frame order.
- `Docs/Plans/STATUS.md`: the #51 row.

## 7. Test list (test first; contract frozen after section 11 is answered)

New file `Source/SOLTest/Tests/ThrusterMathTest.cpp`, paths `SOLTest.Thruster.<Case>`, written by a test-author
subagent against this spec only:

1. `PlumeLength`: 6.47 / 15.59 / 30.0 m at 1 / 3 / 6.8 G on a 30 m hull (1e-2 m); proportional to hull length; 0 at G <=
   the cut-off; non-finite or negative G gives 0; above 6.8 G per Q6.
2. `PlumeBrightness`: `x^0.5` at the table's points; 0 off; above 6.8 G per Q6.
3. `ColourRamp`: blue-white up to 2.5 G, yellow-white at 3 G, red-orange at and above 6.8 G; continuous (no step larger
   than a tolerance over a fine G sweep); the 3 -> 6.8 segment per Q9; War power gives `c` = 1 at any G (Q10).
4. `ShockKnots`: spacing `2 r (1 + 0.6 b)`; knots only within the first 80%; strength `0.82^(k-1)`; the section 4.4
   examples (1, 3 and 6 knots); none when the plume is off.
5. `LaneMainThrust`: Accelerate and Brake give `AccelMps2 / g0`; Coast and Flip give 0 (sampled through the flip
   window, on again at the first braking sample); Turn gives the component along `HullForward`, never negative.
6. `PlayerMainThrust`: forward component only; reverse / lateral / vertical give 0 (Q7); surface contact excluded;
   the substep mean (Q8).
7. `RibbonHistory`: push and wrap at `N`, samples ordered by age, ages and positions after wrap, reset on a new owner,
   non-finite input ignored, the drift rule (section 4.5, Q4), full-age length 3x the plume.
8. `PixelLod`: pixel length from length, distance, viewport height and FOV (1080p reference, SDD 52 D8); the threshold
   and fade band (Q15); the glow's 1.2 px floor.
9. `NearestRibbons`: the player always; at most `K` within `R`; deterministic ties by NPC index; ships with no plume and
   no fading ribbon excluded (provisional); ranked by pixel size.
10. `QualityScaling`: ribbon count per quality preset (Q12).
11. `Constants`: `G_REF` per Q23 (no equality assert with `WAR_POWER_PULSE_G` unless Q23 ties them); the colour key
    points ordered; `N` within 16-32; nozzle table sane.

The near-set defaults (`K`, `R`) and the pixel thresholds are **test parameters** in 51a, not pinned values, until the
51e-0 mock-up and Q15 fix them; the cases test the rules (selection, fade shape), not the numbers. The case list and
file name are themselves put to the user (Q26).
12. `MaterialMirror` (if a `.ush` is used): the HLSL mirror equals the C++ ramp, brightness and knots at sample points.

Re-validate: `SOLTest.NpcTrafficMath.ThrustState` (unchanged contract, re-read), `SOLTest.NpcRail.*` and
`SOLTest.Npc.HullFlip.*` if the Collect pass changes, the flight tests if `FSOLShipStateFragment` gains a field.

## 8. Verification and performance gates

- Full suite green; `SOLTest.Thruster` green.
- Smokes: `-SOLSmokeNpcTraffic`, `-SOLSmokeLaneMerge`, `-SOLSmokeAutoLanes` still pass; a PIE / smoke screenshot set at
  1920x1080 (player idle, 1 G, 3 G, boost, War power; a lane with accelerating, flipping and braking NPCs) checked by
  eye; the flip window must show no plume.
- Gates (*provisional*, Q21): NPC redraw CPU delta at most 0.10 ms (Earth spawn view, 1M, default budgets), plume GPU
  at most 0.20 ms at 1080p, ribbons at most 0.05 ms CPU; the default-budget 1M worst case keeps passing 60 fps (SDD 11
  Amendment 6). Measured with `stat` / Unreal Insights, not estimated.
- **VR (record only):** one-eye and two-eye Varjo XR-4 resolution costs are measured and recorded against
  `Docs/Research/VR_PERFORMANCE_TARGETS.md`, not gated; VR work waits until after procedural terrain (project rule).
- 4K and 1280x720 spot checks (supported resolutions).

## 9. Risks

- **Instanced-hull cost.** Any per-instance plume data adds to the 20,000-hull upload (TECH_DEBT line 35); option B
  roughly doubles it.
- **Translucent smear.** Render-space translucent cones and Niagara ribbons have no motion vectors (TECH_DEBT line 29)
  and may smear under TSR at high speed relative to the render origin.
- **Third-person camera.** The camera looks down the player's plume and ribbon; at War power they may cover much of the
  screen and wash the HUD under bloom.
- **Colour clash.** Red-orange resembles faction 1's orange at range (accepted in D4); NPCs never reach it today
  (0.825-1.65 G), so the clash is the player's plume only, in War power and in manual boost (3 G x 3, capped at 9 G).
- **Nominal profile.** SDD 50's legs report the nominal profile; a ship marked Coast in the end zones is really slowing,
  so the plume is off where a tiny thrust exists (accepted simplification, SDD 50 section 4.12).
- **Ribbon frame.** The wrong frame (Q4) gives straight or broken ribbons; it blocks the contract.
- **Overdraw.** Many overlapping additive cones in a busy lane close up.
- **Assets.** A hand-edited asset would drift from the script; the script is the only source (Q20).
- **Inflated hulls.** True-length plumes on 3 px inflated hulls look detached or vanish (Q14).

## 10. Persistence

None planned. No `USaveGame` field, no `UGameUserSettings` field, no config layout change. `SAVE_VERSION` stays 13,
`KEY_BINDING_LAYOUT_VERSION` stays 10. The player's `MainThrustG` and the NPC `MainThrustG` are transient plain fields.
If Q12 is answered "new setting", that is a persistence change: version and migrate, never silently discard, with the
user's explicit approval of the bump, built by a separate `opus` / xhigh subagent.

## 11. Open questions (user decisions; none decided here)

Each carries a recommendation; the user decides.

- **Q1 Instancing.** How are the cones and glows drawn for thousands of hulls: A (a plume section on the hull mesh with
  per-instance custom data), B (separate plume ISMs), C (an array-fed Niagara system)? *Recommend A, with Niagara only
  for the ribbons (D1), and confirm that D1's "Niagara" then means the ribbons only.*
- **Q2 NPC thrust source per tier without extra per-frame cost.** Mid CPU ships: fold `LaneThrustAt`'s kind and
  magnitude into the Collect pass's existing basis evaluation and carry a `float MainThrustG` on `FSOLNpcCpuShip`? Full
  ships: the Full AI's achieved acceleration along the nose (a lane ship flying its rail could also use its rail
  thrust)? Gate it like the heading (already zero for Point-tier ships): evaluated only for the up to 20,000 Mesh /
  Instanced ships, not all 100,000 CPU ships. *Recommend both as written, gated.*
- **Q3 Non-lane NPCs** (cluster, battle, orbit rails; Full combat ships). No thrust model on the Mid tier: no plume, or a
  fixed idle plume? Full combat ships: their AI thrust? *Recommend no plume on Mid non-lane ships, AI thrust on Full.*
- **Q4 Ribbon frame (blocks the test contract).** History relative to the ship in non-rotating axes with an exhaust
  drift (section 4.5), or another frame (the viewer's, the lane's, the reference body's)? *Recommend the ship-relative
  drift model.*
- **Q5 D2 wording.** The AI Mid tier (2,000 km, 100,000 ships) is not the render Instanced tier (200 km, 20,000 hulls):
  Mid ships beyond 200 km are CPU points. Reading used here: plumes on every ship drawn as a mesh or an instanced hull,
  none on CPU or Far points. *Recommend confirming that reading.*
- **Q6 Above 6.8 G** (player boost up to 9 G). Clamp length, brightness and colour at 6.8 G, or extrapolate length and
  brightness? *Recommend clamp.*
- **Q7 Player non-forward thrust.** Reverse (1.5 G), lateral and vertical thrust: no main plume (only the forward
  component counts), with RCS puffs a later ticket? *Recommend yes.*
- **Q8 Player G sampling and smoothing.** The time-weighted mean of the step's substeps (War power pulses switch inside
  a step); add a short on / off ramp (for example 0.1 s up, 0.2 s down) or show thrust raw? *Recommend the mean, no
  extra smoothing first, tune in 51f.*
- **Q9 D4 ramp details.** "Blue-white below 3 G" and "a ramp from 2.5 G" overlap: read as blue-white to 2.5 G, a
  smoothstep to yellow-white at 3 G, then a ramp to red-orange at 6.8 G? Linear in G for the 3 -> 6.8 segment? The exact
  colours from the mock-ups? *Recommend that reading, linear in G, colours from the mock-up the user chose.*
- **Q10 War power and the one scalar.** "Always fully tinted" during War power (including the 1.5 G recovery) means the
  material gets `c` separately from `b` (two floats) or an override flag. *Recommend two floats (`c`, `b`).*
- **Q11 Cone shape.** Base radius = nozzle radius; the tail radius / flare angle; NPC nozzle radius (1.5 m on a 30 m
  hull is provisional). *Recommend tail radius 0.35 x nozzle radius (a converging lance), NPC `r_n` 1.5 m.*
- **Q12 Graphics-quality hookup.** Read the existing, already persisted `QualityPreset` (engine overall scalability)
  (Low, Medium, High, Epic, Cinematic; default 3 = Epic) to scale `K` (for example Low 0, Medium 8, High 16, Epic 32 =
  the D7 default, Cinematic 64), no schema change; or a new "Thruster trails" setting
  (a `UGameUserSettings` change: version bump, migration, user approval)? *Recommend reading `QualityPreset`.*
- **Q13 Glow sprite set.** "Nearest ships only": the same set as the ribbons (player + `K` within `R`), or its own
  larger set (for example every Mesh-tier ship, 2 km)? *Recommend every Mesh-tier ship plus the ribbon set.*
- **Q14 Inflated hulls.** Instanced hulls are drawn at least 3 px. Does the plume follow the true hull length (it
  vanishes on far inflated hulls) or the drawn one (it inflates with the hull)? *Recommend the true length.*
- **Q15 Pixel-size LOD thresholds.** Cone fade below about 1 px long? Ribbon on above about 8 px long, faded over a
  band? Set after the 51e-0 mock-up. *Recommend those starting values, tuned in game.*
- **Q16 Bloom dependence.** Must the plume read with bloom off (VR, Low quality)? *Recommend a core bright enough
  without bloom, bloom as a bonus.*
- **Q17 Hull lighting.** A dynamic light from the player plume on its own hull? None on NPCs? *Recommend none for now
  (no light at all), revisit with real ship models.*
- **Q18 Per-hull nozzle data home.** A constant table in `SOLConstants.h` now, a data asset later with the real models?
  Add `PLAYER_HULL_LENGTH_M` (about 20 m)? *Recommend both.*
- **Q19 The player's existing engine-glow spheres.** Remove them, drive them by thrust like the glow sprite, or keep
  them? *Recommend replacing them with the new glow sprite.*
- **Q20 Asset creation.** Approve building the new materials, texture, Niagara system (and, under Q1 A, rebuilding the
  NPC hull meshes) by script (`Tools/Fx/create_thruster_assets.py` or `create_npc_assets.py -SOLOnly=...`)?
  *Recommend a new script under `Tools/Fx/`, run once the user approves.*
- **Q21 Performance gates.** 0.10 ms CPU NPC redraw delta, 0.20 ms GPU at 1080p, 0.05 ms ribbons; VR one-eye /
  two-eye recorded only. *Recommend these.*
- **Q22 Docs and text.** The GAME_MECHANICS row, the one-line Info text and the README shots of section 6? *Recommend
  as listed.*
- **Q23 `G_REF`.** A literal 6.8 G, or tied to `WAR_POWER_PULSE_G` (6.803 G)? *Recommend tying it, so a War power pulse
  is exactly full and the tests use 6.803.*
- **Q24 Ribbon parameters.** Maximum age `A` (no value given), `N` within 16-32, the age fade curve, the width, what
  happens when a ship enters or leaves the near set, and the selection filter. *Recommend `A` 1 s, `N` 24, linear fade,
  width nozzle radius to 2x, grow in / age out, ships with a plume or a fading ribbon.*
- **Q25 Look constants tuned in 51f.** The plume-off cut-off (0.01 G), the core layering, the streak pan rate, the
  rhombus size. *Recommend the provisional values of section 4.4, tuned in game.*
- **Q26 Test list.** Confirm the section 7 cases and `Tests/ThrusterMathTest.cpp` / `SOLTest.Thruster.*`. *Recommend
  as listed.*

## 12. Change log

- 2026-11-20: drafted from the grill-me decisions D1-D10 (user, 2026-11-20); code survey at HEAD `4e8a91f`; open
  questions Q1-Q26 listed, none answered.
