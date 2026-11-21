<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 51: Placeholder main thruster plume

**Issue:** [#51](https://github.com/DieSlower/SOLTest/issues/51). Plan:
[`Plans/51-thruster-plume-plan.md`](../Plans/51-thruster-plume-plan.md). Test contract:
[`Plans/51-thruster-math-contract.md`](../Plans/51-thruster-math-contract.md). Builds on SDD 50 (section 4.12, the per-ship
thrust state `LaneThrustAt`; D21, the flip window; D22, the War power move; D24 / D25, the hull basis), SDD 11 (the
NPC AI and render tiers, the instanced placeholder hulls), SDD 18 (the player flight model and thrust envelope) and
SDD 52 D8 (1080p-reference pixel sizes).
**Persistence change: none.** `SOL::Settings::SAVE_VERSION` stays 13 and `KEY_BINDING_LAYOUT_VERSION` stays 10. The
graphics-quality hookup reads the existing `QualityPreset` (Q12); no setting, key binding or `UPROPERTY` of a save class
changes.
Status: **SDD drafted 2026-11-20 from the grill-me decisions D1-D10; the user answered Q1-Q26 the same day and Q27-Q33
on 2026-11-21 (section 11, Resolved decisions). No open questions remain.** Values marked *start value* are the user's or this document's starting points, tuned
in game (51f). No code, no assets.

## 1. Problem

No ship shows that it is thrusting. The player ship has two always-on orange engine-glow spheres
(`SHIP_ENGINE_GLOW` in `Ship/SOLShipPawn.cpp`) that do not react to thrust, and NPC hulls (a blue cone for faction 0,
an orange flat box for faction 1, both 30 m) show nothing at all, although #50 gave every lane ship a real thrust
profile (accelerate, coast, a 60 s flip, brake, U-turns). Issue #51 asks for a placeholder main thruster effect on the
player ship and on the NPC ships, driven by that thrust state, cheap enough for thousands of hulls.

## 2. Decisions (user, grill-me 2026-11-20)

Later answers (section 11) refine some of these; the note in brackets says where.

- **D1: Niagara plume with a blue-white core.** [Q1: Niagara draws the curved ribbons only; the cones and glows are a
  hull-mesh section.]
- **D2: Shown on the player ship and on NPC hulls in the Mid and Full tiers, not on the Far points.** [Superseded by
  Q5: visibility is judged by the plume's on-screen pixel size, not by tier.]
- **D3: Size and brightness are driven by the actual thrust.** Zero thrust and the lane flip coast mean no plume. NPCs
  consume #50's per-ship thrust state; the player consumes its flight state. [Q7: non-forward thrust shows gas bursts.]
- **D4: Colour from ONE intensity scalar.** Blue-white below 3 G, a smooth ramp from 2.5 G to 3 G to yellow-white,
  red-orange reached at 6.8 G; **War power is always fully tinted.** Red-orange is accepted although it resembles
  faction 1's orange at range. The gradient is done **inside the material from one scalar** (Niagara colour curves
  cannot be user parameters). [Q9: the exact ramp; Q10: colour and brightness are two floats.]
- **D5: Look "B with shock diamonds".** An **emissive cone mesh with panning noise streaks**; a **shock-diamond
  material pattern** on the cone: bright knots along the centre line, rhombus-shaped, **spacing 2.0 x nozzle radius x
  (1 + 0.6 x brightness)**, **each knot 18% dimmer** than the one before, **in the first 80% of the cone**, no
  particles; and a **soft nozzle glow sprite** (at least **1 px**, Q33). **Cone length = hull length x (G / 6.8)^0.8**,
  **brightness = (G / 6.8)^0.5.** [Q13: the glow set is by pixel size, not "nearest ships only"; Q23: 6.8 G is
  `WAR_POWER_PULSE_G`.]
- **D6: The plume is a RIGID cone on the nozzle axis** and swings with the ship; it does not bend.
- **D7: A streak.** A **real curved world-space ribbon** built from a **per-nozzle position history ring buffer (16-32
  points)**, **3x the plume's length**, **fading with age**, on **near ships only** (default the player plus the
  nearest 32 ships within 5 km), **scalable by graphics quality**, **LODed by on-screen pixel size, not distance**. The
  user wants to **compare how ribbons look at 50 vs 20 vs 10 km before the radii are fixed** (plan 51e-0). Ships without
  a ribbon get the cone and the diamonds only. [Q4: the frame; Q12: counts per quality; Q24: parameters.]
- **D8: Nozzle count is per-hull data:** 1 for NPCs, 2 for the player ship. Real ship models define their own nozzle
  sockets later.
- **D9: No sound.** Visual only; audio is a later ticket.
- **D10: No paid pack content in the CC0 repo.** The Fab packs looked at (SF Studio "Thrusters & Jet Engines VFX",
  Shogun Games "Rocket Thruster Exhaust FX", EffectsCreator "Jet Thruster FX") are reference only. Research notes and
  the mock-ups are in the grill-me session's scratchpad, not in the repo; the values taken from them are copied here
  (section 4.3).

## 3. What exists today (code survey, HEAD `4e8a91f`)

| Piece | Where | Facts that matter here |
|---|---|---|
| Lane thrust state | `Traffic/SOLLaneTrafficMath` (`SOLLaneTraffic::LaneThrustAt(rail, profile, t)` with the ship key), SDD 50 section 4.12 | `ESOLLaneThrust` Accelerate (`alpha` along travel), Coast (0), Brake (`alpha` against travel, hull flipped), Turn (centripetal), Flip (0, hull rotating, 60 s); `FSOLLaneThrustState` carries `AccelMps2`, `Direction`, `FlipFraction`, `HullForward`. Pure, CPU side. In a turn the main engine is aligned with the thrust only at the apex. Legs report the **nominal** profile (the metric end-zone term is left out) |
| NPC G values | `SOLConstants.h` | `NPC_LANE_PEAK_ACCEL_G` = 1.65 G times a per-ship effort 0.5-1.0, so lane NPCs thrust at **0.825-1.65 G**: always blue-white |
| Hull basis | `SOLLaneTraffic::LaneHullBasis`, `HullRotation`, `SOLNpcAi::HullDrawRotation` (D24) | `FSOLNpcCpuShip::HeadingDirection` / `HeadingTop` (Collect pass of `USOLNpcTierProcessor`); a Full ship draws its AI attitude, a Mid ship its rail heading |
| CPU ship record | `Npc/SOLNpcTierProcessor.h` `FSOLNpcCpuShip` | Position, previous position, faction, render tier, AI tier, heading, top, hidden, NPC index, rail distance. **No thrust field** |
| Tiers | `GAME_MECHANICS.md` AI / Render tiers | AI: Full within 50 km (budget 4,000), Mid within 2,000 km (budget 100,000), Far beyond. Render: real mesh within 2 km (budget 32), instanced placeholder hull within 200 km (budget 20,000, never under 3 px, `NPC_HULL_MIN_PIXELS`), CPU points beyond (never under 2 px, `NPC_POINT_MIN_PIXELS`), GPU far points beyond the Mid radius |
| NPC renderer | `Npc/SOLNpcVisuals.cpp` `UpdateCpuShips`, `UploadHulls` | One ISM per faction (`USOLNpcHullInstances`, fixed bounds), parallel transform fill, `BatchUpdateInstancesTransforms` with previous transforms; the mesh pool places up to 32 `UStaticMeshComponent`s; `PixelWorldCm` gives the world size of a pixel |
| Player thrust | `Ship/SOLShipFlightProcessor.cpp` (`stepOutput.ThrustAccelWorldMps2`, per substep), `FSOLShipStateFragment::GLoadG` | The achieved thrust acceleration is local to the substep loop; `GLoadG` includes **surface contact**, so it is not a thrust measure. Forward 3 G, reverse 1.5 G, lateral / vertical 2 G, boost x3, total cap 9 G; War power pulses at `WAR_POWER_PULSE_G` = 1.5 + 7.5 x sqrt(0.5) = 6.803301 G and recovers at 1.5 G (SDD 50 D22) |
| Player hull | `Ship/SOLShipPawn.cpp` `SHIP_PARTS` | Primitive parts, about 20 m long; two engine cylinders at y = +-2.2 m, z = -0.2 m, radius about 0.8 m; two always-on emissive glow spheres at x = -8.05 m. **No hull-length constant** |
| Niagara in the repo | `Combat/SOLCombatVisuals`, `Npc/SOLNpcCombatVisuals` (`NS_SOLBolts`), `Visuals/SOLSpaceDustComponent`, ring layers | Array-fed systems and user parameters named in `SOLConstants.h` |
| Assets by script | `Tools/Npc/create_npc_assets.py` (`-SOLRebuild`, `-SOLOnly=`) | The NPC materials are built by this script, never by hand |
| Graphics quality | `Menu/SOLSettingsModel.h` `QualityPreset` (0 Low to 4 Cinematic, default 3 Epic) | Already persisted as the engine's overall scalability level, so reading it needs no schema change |

**Known debt not to re-report** (`Docs/TECH_DEBT.md`): render-space Niagara and translucent effects have no motion
vectors and smear under TSR (line 29); the 20,000 instanced-hull upload cost (line 35), which per-instance plume data
adds to; assets loaded only by a string path are not cooked (line 59); every hull is the 30 m placeholder (line 106).

## 4. Design

Pixel arithmetic is in 1080p-reference pixels (SDD 52 D8): with a 90 degree horizontal FOV at 16:9 the reference focal
length `f` is 960 px at every resolution.

### 4.1 Overview

Every ship **drawn with a hull mesh** (mesh tier or instanced hull, Q27) that has thrust data (the player, Mid-tier lane
ships, Full-AI ships) and whose plume is about 1 px long or more on screen gets one rigid emissive cone per nozzle (D5, D6), its length and brightness from the main-engine G, its
colour from the colour scalar `c`, the shock-diamond and streak pattern in the material, and a soft nozzle glow when
the glow is about 1 px or more (Q5, Q13). Reverse, lateral and vertical thrust show very small, nearly transparent gas
bursts on the same hull-drawn ships (Q7, Q27). The player and a near set of NPCs also get a curved Niagara ribbon (D7). The player's plume lights its own hull
with one dynamic light (Q17). Nothing moves differently, no gameplay changes, nothing is saved.

### 4.2 Thrust vector and main-engine G (D3, Q2, Q3, Q7, Q8)

- **One thrust vector per ship per frame, in the hull frame** (X nose, Y right, Z top), in G: `a`. Gravity and surface
  contact are not thrust. **Main-engine G** `G_main = max(0, a.X)`; under `PLUME_MIN_G` (*start value* 0.01 G, Q25) the
  plume is off. The other components drive the gas bursts (4.6).
- **Mid-tier lane ships (Q2):** from the rail's thrust profile in the tier processor's Collect pass, projected onto the
  hull basis the pass already computes: `AccelMps2 x Direction / g0` for Accelerate, Brake and Turn, **zero for Coast and
  for the whole Flip window** (the flame comes back exactly when braking starts). Accelerate and Brake face the hull
  along the thrust, so they give `G_main = AccelMps2 / g0` and no bursts; a Turn gives the along-hull component as main
  thrust and the rest as side bursts (the NPC burst source is the smoothed acceleration of Q30, section 4.6).
- **Full-AI ships (Q2):** the Full AI's achieved acceleration (thrust only), in its attitude basis.
- **Mid-tier non-lane ships (Q3)** (cluster, battle, orbit rails) have no thrust model: no plume and no bursts until
  they are promoted to Full.
- **NPC record:** a transient `FVector3f ThrustLocalG` on `FSOLNpcCpuShip` (CPU-only render record, never saved, not on
  the 32-byte GPU rail).
- **Evaluation gate:** the thrust is evaluated only for ships nearer than `PlumeMaxDistanceM`, the distance at which the
  longest possible plume (the hull length, at `G_REF`) falls under the cone's lower fade threshold: about 58 km for a 30 m
  hull at the 0.5 px start value (29 km at 1 px). Today that is inside the 50 km Full-AI radius except at the 0.5 px fade
  edge, so the Mid rail thrust matters mainly for ships pushed out of the 4,000 Full budget.
- **Player (Q7, Q8):** the flight processor's achieved `ThrustAccelWorldMps2`, rotated into the ship frame with each
  substep's attitude, **time-weighted mean of the step's substeps, no extra smoothing** (tuned in 51f). A transient
  `FVector3f ThrustLocalG` in `FSOLShipStateFragment` (a plain field, not a `UPROPERTY`, never saved). `GLoadG` is not
  used. The forward component lights the main plume; reverse, lateral and vertical light the gas bursts.

### 4.3 Plume scalars (D4, D5, Q6, Q9, Q10, Q23)

`x = clamp(G_main / G_REF, 0, 1)` with **`G_REF` = `WAR_POWER_PULSE_G` = 6.803301 G** (Q23), so a War power pulse is
exactly full. **Above `G_REF`** (boost up to 9 G) **length, brightness and colour are clamped** (Q6).

| Quantity | Rule | 0.825 G | 1 G | 1.65 G | 3 G | `G_REF` and above |
|---|---|---|---|---|---|---|
| Cone length, 30 m hull: `L_p = L_hull x^0.8` | proportional to hull length | 5.548 m | 6.471 m | 9.659 m | 15.583 m | 30.0 m |
| Cone length, player 20 m hull | | 3.698 m | 4.314 m | 6.439 m | 10.388 m | 20.0 m |
| Brightness `b = x^0.5` | | 0.3482 | 0.3834 | 0.4925 | 0.6641 | 1.0 |
| Colour scalar `c` | below | 0 | 0 | 0 | 0.35 | 1 |

- **Colour scalar `c(G)`** (Q9): 0 up to 2.5 G (blue-white); `0.35 x smoothstep((G - 2.5) / 0.5)` from 2.5 to 3 G
  (yellow-white at 3 G); `0.35 + 0.65 (G - 3) / (G_REF - 3)`, linear in G, from 3 G to `G_REF` (red-orange); 1 above.
  Examples: 2.75 G gives 0.175, 4 G 0.5209, 5 G 0.6918.
- **War power (Q10):** while a War power order flies (`bAutopilotIgnoreLanes`), `c` = 1 at any G; `b` and the length
  follow the real `G_main`, so the 1.5 G recovery shows a short red-orange plume. The material therefore gets **two
  floats, `c` and `b`**.
- **Colour ramp `PlumeColour(c)`** (D4, in the material, mirrored in C++): a linear blend between three keys, each with a
  rim (outer) and a core colour. *Start values* from the mock-up the user chose (`thruster_mock2`), tuned in 51f:

  | Key | `c` | G | Rim RGB | Core RGB |
  |---|---|---|---|---|
  | blue-white | 0 | up to 2.5 | 0.35, 0.60, 1.00 | 0.80, 0.90, 1.00 |
  | yellow-white | 0.35 | 3 | 1.00, 0.80, 0.35 | 1.00, 0.96, 0.82 |
  | red-orange | 1 | `G_REF` | 1.00, 0.38, 0.10 | 1.00, 0.82, 0.55 |

### 4.4 Look (D5, D6, Q11, Q16, Q19, Q25)

- **Cone.** A unit cone along -X from the nozzle exit, `L_p` long, base radius the nozzle radius `r_n`, **tail radius
  0.35 `r_n`** (a converging lance, Q11). Unlit, additive translucent, two-sided, no depth write, no shadow, no
  collision. Rigid on the nozzle axis (D6).
- **Core and falloff.** A white-hot core along the axis tinted by the ramp's core colour, the rim colour outside (Q25
  *start*). **The core is bright enough to read without bloom; bloom is a bonus** (Q16).
- **Noise streaks.** A panning, tiling noise texture along the cone (*start*: pan rate proportional to `b`, Q25),
  generated procedurally by the asset script (CC0, D10).
- **Shock diamonds.** Knots on the centre line at `s_k = k S`, `S = 2.0 r_n (1 + 0.6 b)`, `k` = 1, 2, ... while `s_k <=
  0.8 L_p`; knot `k` has strength `0.82^(k-1)`; each knot a rhombus (*start*: half-length 0.35 S, half-width 0.6 of the
  local cone radius, Q25).

  | Ship | G | `L_p` | `b` | `S` | Knots (position, strength) |
  |---|---|---|---|---|---|
  | NPC 30 m, `r_n` 1.5 m | 1.65 | 9.659 m | 0.4925 | 3.8865 m | 1 (3.89 m; the second at 7.773 m is past 0.8 `L_p` = 7.727 m) |
  | Player 20 m, `r_n` 0.8 m | 3.0 | 10.388 m | 0.6641 | 2.2375 m | 3 (2.24, 4.48, 6.71 m; 1, 0.82, 0.6724) |
  | Player, War power | `G_REF` | 20.0 m | 1.0 | 2.56 m | 6 (2.56 to 15.36 m; last 0.3707) |

- **Nozzle glow.** A camera-facing soft disc at the nozzle exit, brightness `b`, colour from `c`, diameter *start*
  2.5 `r_n` (Q25), shown by the pixel rule (4.5, Q13) and drawn at least **1 px** once shown (Q33, replacing the 1.2 px
  of the draft).
- **The player's engine-glow spheres** (`SHIP_ENGINE_GLOW`) are **replaced** by this thrust-driven glow (Q19).

### 4.5 Pixel-size visibility (Q5, Q13, Q14, Q15)

- **The rule (Q27):** plumes and gas bursts appear **only on ships drawn with a hull mesh** (mesh tier or instanced
  hull). **Dot-drawn ships (CPU or GPU points) get none**; there is no plume on points. On hull-drawn ships, the plume's
  own on-screen pixel size decides the fade and visibility of cone and glow. A hull-drawn ship needs thrust data (Mid
  lane rail thrust, Full AI, the player); Mid non-lane ships get none. The plume uses the **true hull length**, not the
  drawn size (hulls are inflated to 3 px, points to 2 px).
- **Pixel length** `px = f L / d` (reference pixels). **Cone:** alpha 0 at 0.5 px rising linearly to 1 at 1 px (*start*,
  "fades under about 1 px"). **Glow:** the same rule on its diameter (Q13). **Ribbon:** on above about 8 px of ribbon
  length `3 L_p`, faded over 6 to 8 px (*start*). All thresholds are tuned after the 51e-0 mock-up.
- Since `L_p <= L_hull`, a visible cone implies a hull at least 1 px long. **Ticket #54** (hide ships whose true size
  is under 1 px instead of the 2 px point and 3 px hull floors, out of scope here) **may revisit the dot rule of Q27.**
- **What the numbers mean:** a 1.65 G NPC cone (9.66 m) is 1 px at 9.3 km and 3 px at 3.1 km; a 30 m cone at `G_REF`
  is 1 px at 28.8 km; the player's War power cone (20 m) at 19.2 km. A point-drawn ship (beyond 200 km, or over the
  20,000-hull budget) has no hull instance and no plume (Q27).

### 4.6 Gas bursts (Q7, scope increase)

- **Who (Q27, Q30):** the player and NPCs **drawn with a hull** (never dots), shown by the same pixel rule on the
  burst's own length.
- **Mapping:** from the hull-frame thrust `a` (G): Reverse `max(0, -a.X)`, Right `max(0, a.Y)`, Left `max(0, -a.Y)`,
  Up `max(0, a.Z)`, Down `max(0, -a.Z)`. The gas leaves **opposite** to the thrust: **reverse thrust gives
  forward-facing bursts** (exhaust +X, at the bow), thrust to the right gives a burst on the left side exhausting -Y,
  left gives +Y on the right side, up gives -Z on the bottom, down gives +Z on the top. The main plume and the bursts are
  independent: pure forward thrust shows no burst, pure reverse no main plume.
- **Strength:** `s = clamp((g - BURST_MIN_G) / (BURST_REF_G - BURST_MIN_G), 0, 1)`, *start* `BURST_MIN_G` 0.05 G,
  `BURST_REF_G` **1.5 G** (Q32: opacity clamps at 1.5 G; tuned in 51f). Burst length and opacity from `s` (Q32).
- **Look:** short, **nearly transparent puffs** of grey-white gas, visually distinct from the main flame: translucent
  (not hot additive), no colour ramp, no core, no diamonds; a soft noise-dissolved puff that flickers in time with a
  per-instance phase (the NPC index), *start* maximum opacity 0.25, **scaling with `s`** (Q32).
- **Size (Q32):** **very small compared with the main plume**, only a few metres long: `L_b = L_hull / 6 x s` (about 5 m
  on a 30 m hull at full strength, 3.33 m on the 20 m player hull), tuned in 51f.
- **Material parameters:** three signed floats per ship (reverse >= 0, lateral, vertical) beside `c` and `b`; the
  material picks the side from the sign. NPCs carry them in the hull-instance custom data (4.10), the player in custom
  primitive data.
- **Burst ports (Q31):** a **fixed per-hull port table in `SOLConstants.h`**: a bow pair (reverse) plus 4 side ports (stern ports dropped, user decision).
  Reverse thrust uses the **bow ports**, lateral thrust the **side ports**, vertical thrust the **top / bottom ports**.
- **NPC burst acceleration (Q30):** only NPCs with a **drawn hull**. The acceleration vector (velocity change in the
  ship frame) is **smoothed over about 0.25 s** and **evaluated in the existing Collect pass** (no extra pass); its cost
  is profiled in 51g. The main plume of Mid lane and Full ships still uses the Q2 sources; Mid non-lane ships have no
  thrust model (Q3), and for them a smoothed velocity change would show free-fall gravity as steady side bursts (51g
  must exclude gravity or those ships).

### 4.7 Player hull light (Q17, scope increase)

- **One movable point light** on the player pawn, a little aft of the nozzles' centroid, no shadows, attenuation
  radius *start* `PLAYER_HULL_LENGTH_M`, **on its own lighting channel** shared only with the player's hull parts (Q28),
  so it lights its own hull and nothing else. **No light on NPCs.**
- **Intensity** `b x PLAYER_PLUME_LIGHT_MAX_INTENSITY` (*start* value set in 51d, tuned in 51f); **colour** the ramp's
  rim colour at `c`; hidden while `b` = 0.
- **Cost:** one unshadowed local light of about 20 m radius, measured inside the 0.20 ms GPU gate. Gating
  (Q28): **on in VR, off at Low quality, on at every other quality.**

### 4.8 Ribbon (D7, Q4, Q12, Q24)

- **Frame (Q4).** Each sample stores the **nozzle exit position relative to the ship's centre at emission, in
  non-rotating world-aligned axes**, the exhaust direction at emission, a drift speed `u_i = 3 L_p,i / A` and the
  brightness `b_i`. Sample `i` is drawn at `ship position now + offset_i + exhaust_i x u_i x age_i`, so translation (20-300
  km/s lane speeds, the 10 km rebases) drops out, the ribbon is 3x the plume at full age (D7), and it curves when the
  ship turns (the flip, the player's turns, the U-turns).
- **Parameters (Q24):** maximum age `A` **1 s**, **24 samples**, one every `A / 24`; alpha `(1 - age / A) x b_i`
  (linear); width `r_n` at the nozzle to `2 r_n` at the tail; a new owner starts empty and grows in, a ship that leaves
  the set keeps its ribbon until its samples age out.
- **Who gets one (Q12):** count by the existing `QualityPreset`: **Low 0, Medium 8, High 16, Epic 32, Cinematic no
  count limit and no cap at all** (every qualifying ship within `R`, Q29). The **player always gets ribbons except at Low.** Candidates are
  ships within `R` (*start* 5 km, D7) that have a plume or a fading ribbon (Q24), ranked by ribbon pixel length, ties by
  NPC index; selection by partial sort, no allocation after `Reserve`. **Risk note, not a cap (Q29):** at Cinematic a dense 5 km crowd can spike the frame; this is recorded, not limited.
- **Distances for the 51e-0 mock-up:** a 1.65 G NPC ribbon (29 m) is 2.8 px at 10 km, 1.4 px at 20 km, 0.56 px at 50
  km; a player War power ribbon (60 m) 5.8, 2.9 and 1.2 px. With the 8 px start threshold an NPC ribbon appears only
  within about 3.5 km.

### 4.9 Per-hull nozzle data (D8, Q11, Q18)

A constant table in `SOLConstants.h` now (a data asset with the real models later, TECH_DEBT line 106): per hull kind,
hull length, main nozzles (position and axis in the hull frame, radius `r_n`), tail ratio 0.35 and the burst ports
(Q31). Entries: NPC faction 0 cone and faction 1 box, 30 m, 1 nozzle on the aft centre, `r_n` **1.5 m (provisional,
Q11)**; player, `PLAYER_HULL_LENGTH_M` **20 m**, 2 nozzles at the engine cylinders' aft faces (x -8.0 m, y +-2.2 m, z
-0.2 m), `r_n` 0.8 m.

### 4.10 Rendering and instancing (Q1, Q14)

- **Cones, glows and bursts are sections on the hull mesh** driven by per-instance custom data (Q1): the placeholder
  hull meshes gain the plume, glow and burst sections, scaled by world-position offset, so the existing transform upload
  carries them and only custom floats are added. Per instance: `c`, `b`, a plume length scale (true length over drawn
  scale, undoing the 3 px inflation, Q14) and the three burst floats: 6 custom floats (`NumCustomDataFloats`). The mesh
  pool uses custom primitive data, the player pawn its own plume and burst meshes with the same materials. The hull ISMs
  already have fixed bounds, so WPO growth is safe.
- **Ribbons:** one array-fed Niagara ribbon system (D1 applies to the ribbons only, Q1), fed from the history arrays of
  the ribbon set.
- **As built in 51b** (`SOL::ThrusterMaterialParams`, header of `Shaders/Private/SOLThruster.ush`): the 6 per-ship
  floats are, in order, `c`, `b`, the **true plume length in metres** (not a scale: the vertex stage re-places every
  thruster vertex in true metres from its anchor, which undoes both the non-uniform hull instance scale and the 3 px
  inflation without the CPU knowing either, Q14), the reverse burst strength in [0, 1], and the signed
  lateral and vertical strengths in [-1, 1] (right minus left, up minus down; `BurstStrength` is applied on the CPU).
  The material reads per-instance custom data plus the same index of custom primitive data, so the ISMs, the mesh pool
  and the player use one material each. An idle or sub-fade section collapses onto its anchor (no pixels). The NPC hull
  meshes are new assets (`THRUSTER_NPC_HULL_MESH_FACTION_0/1`, a generated cone / box of the engine Cone / Cube's size
  and pivot in slot `Hull` plus slots
  `Plume`, `Glow`, `Burst`); `NPC_HULL_MESH_FACTION_0/1` are unchanged until 51c switches to them.

### 4.11 Assets (Q20)

**Approved:** a new script `Tools/Fx/create_thruster_assets.py` builds `M_SOLThrusterPlume` (cone, diamonds, streaks,
ramp from `c`, WPO length), `M_SOLThrusterGlow`, `M_SOLThrusterBurst`, a procedural noise texture, the plume / glow /
burst sections of the NPC hull meshes, the player's plume meshes and `NS_SOLThrusterTrails`. It runs in 51b, **only
after the 51a pure math tests are green**. Never by hand, CC0 only (D10). Paths in `SOL::Paths` (STYLE_GUIDE section
7); folders added to the cook-list gap (TECH_DEBT line 59). The ramp, brightness and knot formulas are mirrored by C++
pure functions pinned by tests (the `SOLNpcTunnelMirror.h` pattern: a `.ush` included by a Custom node).

**As built in 51b** (2026-11-21): the script also builds **`M_SOLThrusterTrail`** (additive, Niagara ribbon usage), the
material the ribbon renderer needs, which this list had left out. The noise texture `T_SOLThrusterNoise` is a seeded,
periodic value-noise fBm written by the script (R streaks, G / B puffs). The tunables reach the materials as vector
parameters whose defaults the script reads from `SOL::Thruster` (`SOLThrusterMirror::MaterialVectors` packs the same
values; `SOLTest.Thruster.MaterialAssets` fails if the asset defaults drift). `NS_SOLThrusterTrails`: one CPU-sim
emitter, a one-shot burst of `TrailMaxPoints` (825 = 33 trails x 25 points at Epic) never-dying particles, point `i`
reading element `i` (`Particles.UniqueID`) of the `TrailPositionsCm` / `TrailColours` / `TrailWidthsCm` arrays, width
and alpha 0 past `TrailPointCount`, ribbon id the int `Particles.TrailRibbon = i / TrailPointsPerTrail` (25: the 24
samples plus the live nozzle exit), link order `i % 25`. Compiled instruction counts (the editor's shader platform,
`MaterialEditingLibrary.get_statistics` in the script's run): plume VS 632 / PS 165, glow VS 296 / PS 134, burst VS 462 / PS 118, trail VS
153 / PS 96; the plume's vertex cost over 20,000 hulls is the first thing 51c/51g must profile. Assets are
compile-checked only: nothing is drawn until 51c-51e, so the look is unverified by eye.

### 4.12 Per-frame cost and gates (Q21)

- CPU: one hull-frame thrust vector per gated ship in the Collect pass, 6 custom floats in the existing parallel
  placement pass, the ribbon selection and history pushes. **Gate: NPC redraw CPU delta at most 0.10 ms** (Earth spawn
  view, 1M NPCs, default budgets); **ribbons at most 0.05 ms CPU**.
- GPU: **at most 0.20 ms at 1920x1080** in a busy lane view and the player boost / War power view, including the bursts
  and the player light.
- Measured by **interleaved A/B profiling** (the #52 method), not estimated. **VR one-eye / two-eye recorded only.**

### 4.13 What does not change

No flight, AI, lane or combat behaviour; no rail or GPU traffic layout (`FSOLNpcRail` stays 32 bytes); no far-point
shader; no save data, settings or key bindings; no audio (D9).

### 4.14 Pure functions (`Visuals/SOLThrusterMath.{h,cpp}`, namespace `SOLThruster`)

Signatures and expected values are in the [test contract](../Plans/51-thruster-math-contract.md): `PlumeFraction`,
`PlumeLengthM`, `PlumeBrightness`, `PlumeColourScalar`, `PlumeColour`, `ShockSpacingM`, `ShockKnots`,
`LaneThrustLocalG`, `FSOLThrustMean`, `ThrustAxes`, `BurstExhaustDir`, `BurstStrength`, `BurstLengthM`, `FocalRefPx`,
`PixelLength`, `LodAlpha`, `PlumeMaxDistanceM`, `GlowDrawnPx`, `FSOLPlumeHistory`, `RibbonDriftMps`, `RibbonPoint`,
`RibbonAlpha`, `RibbonWidthM`, `SelectRibbonShips`, `RibbonCountForQuality`, `PlayerHasRibbon`, `PlumeLightIntensity`,
`PlumeLightColour`, `HullThrusters`, `PlumeAllowed`, `PlayerLightEnabled`. `LaneThrustLocalG` takes the thrust
direction and the hull basis in one frame; the caller passes Unreal axes so that +Y (top x forward) is the hull's
right. The burst port table (Q31) is constants only in 51a (`SOL::Thruster::*_PORT_*`, provisional positions on the
placeholder hulls); 51g assembles it.

## 5. SDD sections that change

- SDD 50 section 4.12: "#51 decides how a flame uses it" gets the answer (section 4.2 here).
- SDD 11 render tiers: plumes are judged by on-screen size, not by tier (4.5).
- SDD 18 / 19: the player's engine glow is replaced by the thrust-driven glow, and the plume lights the hull (Q17, Q19).

## 6. Player-facing text and docs (Q22)

- `Docs/GAME_MECHANICS.md`: a new "Thruster plume" row (what drives it, G values, colours, gas bursts, the pixel rule,
  who gets a ribbon per graphics quality, the player hull light) and notes in the Lane traffic row (dark through the
  flip window) and the War power row (always red-orange).
- In-game Info screen: one line saying the plume shows main-engine thrust and turns yellow then red-orange at high G,
  red-orange during War power, and that reverse and side thrust show gas bursts.
- README: 2-3 gallery screenshots at 1920x1080 after the ticket.
- `Docs/ARCHITECTURE.md`: the plume renderer, the thrust fields and the frame order. `Docs/Plans/STATUS.md`: the #51 row.

## 7. Test list (test first)

New file `Source/SOLTest/Tests/ThrusterMathTest.cpp`, paths `SOLTest.Thruster.<Case>` (Q26), written by a test-author
subagent from [`Plans/51-thruster-math-contract.md`](../Plans/51-thruster-math-contract.md) only (signatures and
expected values, no implementation):

1. `PlumeLength`: the section 4.3 values; proportional to hull length; 0 under the cut-off; clamped at and above
   `G_REF` (9 G gives the hull length); non-finite or negative input gives 0.
2. `PlumeBrightness`: `x^0.5`; clamped above `G_REF`.
3. `ColourRamp`: `c(G)` key points and examples, continuity over a fine sweep, clamp above `G_REF`, War power `c` = 1;
   `PlumeColour` at the three keys and between them.
4. `ShockKnots`: spacing, the 80% zone, strengths, the three examples of 4.4, none when off.
5. `LaneThrust`: the hull-frame vector for each `ESOLLaneThrust` kind; Coast and Flip 0; Turn split into main and side.
6. `PlayerThrust`: the substep time-weighted mean; main plume forward only.
7. `RibbonHistory`: push, wrap at 24, age order, reset, non-finite input ignored, the Q4 drift point, full-age length 3x
   the plume, linear alpha.
8. `PixelLod`: pixel length, the reference focal length, the fade bands (test parameters), the glow floor, the
   evaluation-gate distance.
9. `NearestRibbons`: within `R`, ranked by pixel size, ties by NPC index, no-plume ships without a fading ribbon
   excluded, the count limit and the unlimited case (`K`, `R` test parameters).
10. `QualityScaling`: 0 / 8 / 16 / 32 / unlimited; the player's ribbon off only at Low.
11. `Constants`: `G_REF == WAR_POWER_PULSE_G`; the colour keys ordered; 24 samples within 16-32; `A` = 1 s; the nozzle
    table.
12. `MaterialMirror` (51b): the HLSL mirror equals the C++ ramp, brightness and knots at sample points.
13. `GasBursts`: the direction mapping, independence from the main plume, the threshold and clamp (threshold and
    reference as test parameters, start values 0.05 G and 1.5 G, Q32); `BurstLengthM` (hull length / 6 x strength,
    strength clamped, Q32); `PlumeAllowed` (no plume or burst on dot-drawn ships, Q27).
14. `PlayerLight`: intensity proportional to `b`, zero when off, colour equal to the ramp's rim colour;
    `PlayerLightEnabled` (on in VR at every quality, otherwise off only at Low, Q28).

Re-validate: `SOLTest.NpcTrafficMath.ThrustState` (contract unchanged), `SOLTest.NpcRail.*` and `SOLTest.Npc.HullFlip.*`
(the Collect pass changes), the flight and War power tests (`FSOLShipStateFragment` gains a field).

## 8. Verification and performance gates

- Full suite green; `SOLTest.Thruster` green.
- Smokes: `-SOLSmokeNpcTraffic`, `-SOLSmokeLaneMerge`, `-SOLSmokeAutoLanes` still pass; a screenshot set at 1920x1080
  (player idle, 1 G, 3 G, boost, War power, reverse and strafe bursts; a lane with accelerating, flipping, turning and
  braking NPCs) checked by eye; the flip window must show no plume.
- Gates (Q21): NPC redraw CPU delta at most 0.10 ms, plume GPU at most 0.20 ms at 1080p, ribbons at most 0.05 ms CPU;
  the default-budget 1M worst case keeps passing 60 fps (SDD 11 Amendment 6). Interleaved A/B, `stat` / Unreal Insights.
- **VR (record only):** one-eye and two-eye Varjo XR-4 costs recorded against
  `Docs/Research/VR_PERFORMANCE_TARGETS.md`, not gated (VR work waits until after procedural terrain).
- 4K and 1280x720 spot checks.

## 9. Risks

- **Instanced-hull cost.** 6 custom floats per hull add to the 20,000-hull upload (TECH_DEBT line 35).
- **Translucent smear.** Translucent cones, bursts and Niagara ribbons have no motion vectors (TECH_DEBT line 29).
- **Third-person camera.** The camera looks down the player's plume and ribbon; at War power they may cover much of the
  screen and wash the HUD under bloom.
- **Colour clash.** Red-orange resembles faction 1's orange (accepted in D4); NPCs never reach it today (0.825-1.65 G).
- **Nominal profile.** A ship marked Coast in the end zones is really slowing, so the plume is off where a tiny thrust
  exists (accepted, SDD 50 section 4.12).
- **Overdraw.** Many overlapping additive cones and translucent bursts in a busy lane close up.
- **Scope increase.** Gas bursts (Q7) and the player light (Q17) were added after the first draft; they add a material,
  hull sections, 3 custom floats and a light (Q27-Q33 resolved).
- **Cinematic ribbons uncapped (Q29).** No cap on ribbon ships within 5 km: a possible frame spike and unbounded history
  memory, accepted and recorded.
- **Assets.** A hand-edited asset would drift from the script; the script is the only source (Q20).

## 10. Persistence

None. No `USaveGame` field, no `UGameUserSettings` field, no config layout change. `SAVE_VERSION` stays 13,
`KEY_BINDING_LAYOUT_VERSION` stays 10. The `ThrustLocalG` fields are transient plain fields. Graphics quality is read
from the existing `QualityPreset` (Q12).

## 11. Resolved decisions (user, 2026-11-20 and 2026-11-21)

- **Q1 Instancing:** the cone and glow (and the gas bursts) are a section on the hull mesh driven by per-instance custom
  data; Niagara is used only for the curved ribbons, so D1's "Niagara" applies to the ribbons only.
- **Q2 NPC thrust source:** per tier: Mid lane ships from the rail acceleration profile in the tier processor's Collect
  pass; Full ships from the Full AI's achieved acceleration; the lane flip coast is zero.
- **Q3 Non-lane NPCs:** no plume on Mid non-lane ships (no thrust model) until they are Full.
- **Q4 Ribbon frame:** history relative to the ship in non-rotating world-aligned axes with an exhaust drift (stable
  under origin rebasing), section 4.8.
- **Q5 / Q14 (pixel rule, limited by Q27):** plume visibility (cone, glow) is judged by the plume's on-screen pixel
  size, not by tier, on ships drawn with a hull. Any hull-drawn ship with thrust data (Mid lane rail thrust, Full AI)
  whose plume is about 1 px long (tuned in game) gets one; **dot-drawn ships get none (Q27)**. The plume uses the true
  hull length, not the inflated drawn size. Ticket #54 (hide ships under 1 px) is out of scope and may revisit this.
- **Q6 Above 6.8 G:** clamp length, brightness and colour at `G_REF`.
- **Q7 Non-forward thrust:** reverse, lateral and vertical thrust show clear gas bursts (short, nearly transparent puffs,
  visually different from the main flame) for the player and NPCs. NPC per-direction thrust is derived from the
  acceleration vector (velocity change in the ship frame) on all drawn ships. Section 4.6; see Q27, Q30-Q32.
- **Q8 Player sampling:** the time-weighted mean of the step's substeps, no extra smoothing, tuned in 51f.
- **Q9 Ramp:** blue-white to 2.5 G, a smoothstep to yellow-white at 3 G, linear in G to red-orange at `G_REF`; colours
  from the mock-up the user chose (section 4.3).
- **Q10 War power:** two floats, colour `c` and brightness `b`; War power forces `c` to 1 while `b` follows the real G.
- **Q11 Cone shape:** tail radius 0.35 x nozzle radius; NPC nozzle radius 1.5 m on a 30 m hull (provisional).
- **Q12 Quality hookup:** read the existing `QualityPreset` (no schema change): Low 0, Medium 8, High 16, Epic 32
  ribbon ships; Cinematic no count limit, every ship within the 5 km range. The player always gets ribbons except at
  Low.
- **Q13 Glow set:** by pixel size too: any ship whose glow is at least about 1 px.
- **Q14:** see Q5.
- **Q15 Pixel thresholds:** resolved by the pixel rule: cone fades under about 1 px, ribbon on above about 8 px with a
  fade band; start values, tuned after 51e-0.
- **Q16 Bloom:** the core is bright enough without bloom; bloom is a bonus.
- **Q17 Hull lighting:** the player plume lights its own hull with one dynamic light; none on NPCs. Section 4.7; gating
  in Q28.
- **Q18 Nozzle data:** a constant table in `SOLConstants.h` now (nozzle positions, radii, hull lengths) plus
  `PLAYER_HULL_LENGTH_M` about 20 m.
- **Q19 Engine-glow spheres:** replaced by the new thrust-driven glow.
- **Q20 Assets:** approved, through the new script `Tools/Fx/create_thruster_assets.py`, run in 51b only after the 51a
  pure math tests are green.
- **Q21 Gates:** 0.10 ms CPU NPC redraw delta, 0.20 ms GPU at 1080p, 0.05 ms ribbons; VR recorded only; interleaved A/B
  profiling.
- **Q22 Docs and text:** the GAME_MECHANICS row, the in-game Info text and the README shots of section 6.
- **Q23 `G_REF`:** tied to `WAR_POWER_PULSE_G` (6.803301 G), so a War power pulse is exactly full.
- **Q24 Ribbon parameters:** maximum age 1 s, 24 points, linear age fade, width nozzle radius to 2x, grow in / age out;
  ships with a plume or a fading ribbon are in the set.
- **Q25 Look constants:** the section 4.4 start values (cut-off 0.01 G, core layering, streak pan rate, rhombus size),
  tuned in game.
- **Q26 Test list:** `Tests/ThrusterMathTest.cpp`, `SOLTest.Thruster.*`, the section 7 cases, extended for the pixel
  rule, gas bursts, the clamp above `G_REF`, the colour ramp and the player light.

- **Q27 Plumes vs dots (conflict resolved):** plumes **and gas bursts** appear only on ships drawn with a hull mesh
  (mesh tier or instanced hull). Dot-drawn ships get none. The earlier "pixel rule for dots" is limited to: the plume's
  pixel size decides fade and visibility on hull-drawn ships; no plume on points. #54 may revisit.
- **Q28 Player hull light:** on in VR, off at Low quality, on otherwise; on its own lighting channel.
- **Q29 Cinematic ribbons:** no cap at all (every ship within 5 km). The frame-spike risk is a note (section 9).
- **Q30 NPC burst acceleration:** only ships with a drawn hull, smoothed over about 0.25 s, evaluated in the existing
  Collect pass (no extra pass), profiled in 51g.
- **Q31 Burst ports:** a fixed per-hull port table in `SOLConstants.h`: a bow pair plus 4 side ports (stern ports dropped) (reverse =
  bow ports, lateral = side ports, vertical = top / bottom ports).
- **Q32 Burst size:** very small next to the main plume, a few metres: about 5 m on a 30 m hull (hull length / 6),
  opacity may scale with G, clamped at 1.5 G; tuned in 51f.
- **Q33 Glow floor:** 1 px (matches the pixel rule), replacing the 1.2 px draft.
- **Accepted starting values (tuned in 51f):** a distance gate for plumes instead of tier gating, fade edges 0.5 px and
  6 px, glow diameter 2.5 x nozzle radius, the hull light on its own lighting channel.

**No open questions remain** (Q1-Q33 all resolved).

## 12. Change log

- 2026-11-20: drafted from the grill-me decisions D1-D10; code survey at HEAD `4e8a91f`; open questions Q1-Q26 listed.
- 2026-11-20: the user's answers to Q1-Q26 folded in (section 11): pixel-size visibility rule replaces the tier rule
  (Q5 / Q14), gas bursts (Q7) and the player hull light (Q17) added as scope increases, `G_REF` tied to
  `WAR_POWER_PULSE_G`, colours from the chosen mock-up; test contract split out to
  `Plans/51-thruster-math-contract.md`; new open questions Q27-Q33.
- 2026-11-21: the user's answers to Q27-Q33 folded in: hull-drawn ships only for plumes and bursts (Q27), player light
  gating (Q28), uncapped Cinematic ribbons (Q29), smoothed NPC burst acceleration (Q30), burst port table and size
  (Q31, Q32), 1 px glow floor (Q33). No open questions remain.
- 2026-11-21: part 51a: sections 4.14 and 7 list `BurstLengthM`, `PlumeAllowed` and `PlayerLightEnabled` like the
  contract; the frame note on `LaneThrustLocalG` and the 51a burst port constants added to 4.14.
- 2026-11-21: part 51b: the as-built custom-data layout (true plume length in metres, signed burst strengths) in 4.10,
  the ribbon material, Niagara layout and instruction counts in 4.11.
