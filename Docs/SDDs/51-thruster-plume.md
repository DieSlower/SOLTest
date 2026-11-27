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
on 2026-11-21 (section 11, Resolved decisions). 2026-11-22: the user rejected the cone-plus-ribbon look of 51a-51e;
section 11.2 (R10, one-piece flame) supersedes the separate cone and ribbon wherever they disagree, and section 11.3
lists the open questions R10 raises (not decided).** Values marked *start value* are the user's or this document's
starting points, tuned in game (51f). No code, no assets.

## 1. Problem

No ship shows that it is thrusting. The player ship has two always-on orange engine-glow spheres
(`SHIP_ENGINE_GLOW` in `Ship/SOLShipPawn.cpp`) that do not react to thrust, and NPC hulls (a blue cone for faction 0,
an orange flat box for faction 1, both 30 m) show nothing at all, although #50 gave every lane ship a real thrust
profile (accelerate, coast, a 60 s flip, brake, U-turns). Issue #51 asks for a placeholder main thruster effect on the
player ship and on the NPC ships, driven by that thrust state, cheap enough for thousands of hulls.

## 2. Decisions (user, grill-me 2026-11-20)

Later answers (section 11) refine some of these; the note in brackets says where.

- **D1: Niagara plume with a blue-white core.** [Q1: Niagara draws the curved ribbons only; the cones and glows are a
  hull-mesh section. R10: there is no cone section any more; one flame per nozzle, renderer open (11.3 R10-Q6).]
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
  `WAR_POWER_PULSE_G`. R10: no cone mesh; `L_p` is where the one-piece flame's taper ends, and the knots and streaks
  are patterns along its arc.]
- **D6: The plume is a RIGID cone on the nozzle axis** and swings with the ship; it does not bend. [Superseded by R10:
  the flame is ballistic gas, nearly straight near the nozzle and curving farther out.]
- **D7: A streak.** A **real curved world-space ribbon** built from a **per-nozzle position history ring buffer (16-32
  points; R10: an 8-pose ring, 25 drawn points)**, **3x the plume's length**, **fading with age**, on every hull-drawn ship whose ribbon passes the **pixel rule**
  (section 11.1, R1-R5: `V` by graphics quality, full at `4 V` on screen, gone below `V`, at least 1 px wide, no distance
  or count cap, the player always), **LODed by on-screen pixel size, never by distance or count**. The user compared how
  ribbons look at 50, 20 and 10 km (plan 51e-0, done) and chose pixel thresholds from it. Ships without a ribbon get the
  cone and the diamonds only. [Q4: the frame; Q12 and Q29: superseded by R3/R4; Q24: parameters. R10: the ribbon IS
  the flame (nozzle to tail, one piece); there is no cone-only fallback, and the 1 s age and 3x length are open
  (R10-Q1).]
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

**R10 (section 11.2) replaces the cone and the ribbon in this overview:** each main nozzle draws ONE flame, a single
bending mesh from the nozzle to the tail (widest at the nozzle, tapering to 0.1 `r_n` at `L_p`, then a thin tail to
`3 L_p`), with the knots, streaks, core and colour as patterns along its arc. The glow, the gas bursts and the hull light
stay as described. The paragraph below is the pre-R10 design (no cone is drawn any more).

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
- **Evaluation gate** (R10: the cone term is replaced by the glow and gas-burst reach, contract case 19): the thrust
  is evaluated only for ships nearer than `PlumeMaxDistanceM`, the distance at which the
  longest possible plume (the hull length, at `G_REF`) falls under the cone's lower fade threshold: about 58 km for a 30 m
  hull at the 0.5 px start value (29 km at 1 px). Today that is inside the 50 km Full-AI radius except at the 0.5 px fade
  edge, so the Mid rail thrust matters mainly for ships pushed out of the 4,000 Full budget.
- **As built in 51c:** a Full ship's position is its rail plus the deviation its AI flies, so its achieved
  acceleration is **the lane rail's thrust plus the AI's deviation acceleration**: the Full step flies the deviation
  only (in the rail frame), and a Full lane ship sitting on its rail mid-burn would otherwise read about 0 G although
  nearly every visible plume is on a Full ship (the gate lies inside the 50 km Full radius). The Full AI Scatter stores
  `SOLNpcAi::AchievedAccelMps2` (the step's deviation velocity change over the frame, zero on a respawn) in a transient
  `FVector3f AchievedAccelMps2` on `FSOLNpcAttitudeFragment` (Full ships only, never saved); the Collect pass adds
  `SOLNpcRail::LaneThrustMps2` (the rail's `LaneThrustAt` as a vector, zero in Coast, Flip and on non-lane rails) and
  projects the sum with `SOLThruster::HullThrustLocalG` on the **drawn** hull basis in Unreal axes (the top made
  orthogonal first; +Y right as drawn). A Mid ship uses the rail thrust alone; a Full cluster or battle ship its AI
  share alone. The gate `PlumeGateM` is `PlumeMaxDistanceM(30 m, FocalRefPx(camera FOV, viewport aspect), 0.5 px)`,
  computed once per frame by `USOLNpcSubsystem::ViewPlumeGateM`.
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

**R10 (section 11.2):** the cone bullet below is superseded (no cone mesh, tail width 0.1 `r_n` instead of 0.35, not
rigid). What stays: the core and falloff, the streaks and the shock-diamond formulas and table (now patterns along the
flame's arc coordinate `s`, the rhombus half-width taken from the flame's local half-width), the nozzle glow and the
replacement of the engine-glow spheres.

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

**R10 (section 11.2):** the cone's 0.5-1 px fade goes with the cone. The flame uses the ribbon's rules: drawn width
floored at 1 device px per point (R1) and the whole flame faded by its on-screen length `PixelLength(3 L_p, d, f)`
between `4 V` and `V` (R2, R3). The glow keeps its own rule below. Consequence at Low / Medium / High: open question
R10-Q3.

- **SUPERSEDED by SDD 54 Q7 (issue #54, 2026-11-27):** the flame (ribbon) of an NPC now follows its own pixel rule on every CPU ship with thrust data, including dot-drawn and not-drawn ones; the glow stays a hull section. The Q27 text below is the original rule, kept as history.
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

**R10 (section 11.2) supersedes this section's sample model:** samples are nozzle poses (no drift `u_i`); the drawn
points are gas parcels at a fixed exhaust speed `v_e`, placed at uniform arc coordinates, with the width profile and
along-arc fade of R10 instead of the age alpha and the `r_n` to `2 r_n` widening. The frame (non-rotating ship-relative
axes, Q4), the game-time clock (R8), the shared store, the pixel rules R1-R5 and the renderer choice (R7, reopened as
R10-Q6) carry over.

- **Frame (Q4).** Each sample stores the **nozzle exit position relative to the ship's centre at emission, in
  non-rotating world-aligned axes**, the exhaust direction at emission, a drift speed `u_i = 3 L_p,i / A` and the
  brightness `b_i`. Sample `i` is drawn at `ship position now + offset_i + exhaust_i x u_i x age_i`, so translation (20-300
  km/s lane speeds, the 10 km rebases) drops out, the ribbon is 3x the plume at full age (D7), and it curves when the
  ship turns (the flip, the player's turns, the U-turns).
- **Parameters (Q24):** maximum age `A` **1 s**, **24 samples**, one every `A / 24`; alpha `(1 - age / A) x b_i`
  (linear); width `r_n` at the nozzle to `2 r_n` at the tail; a new owner starts empty and grows in, a ship that leaves
  the set keeps its ribbon until its samples age out.
- **Superseded in 51e (section 11.1, R1-R9):** no count or distance cap, the pixel rule with `V` by quality, the
  1 px width floor, the player at every preset, and the **hybrid renderer** (R7): Niagara for the player's two trails
  and for NPC ribbons at least `RibbonNiagaraMinPx` long on screen, a GPU strip pool for every other shown ribbon, both
  fed from one history store and one point builder so they look the same. The two bullets below are the pre-51e
  design, kept for the record.
- **Who gets one (Q12, superseded by R3/R4):** the pre-51e rule was a count by `QualityPreset` (Low 0, Medium 8, High
  16, Epic 32, Cinematic unlimited) within a 5 km radius, ranked by pixel length. Now: every hull-drawn ship with a
  plume or a fading ribbon (Q24) whose on-screen ribbon length `p` is at least `V` (by quality, R3) gets one, with no
  ranking, count or radius; the player always (R5).
- **Pixel lengths for the 51e-0 mock-up (illustration only, not a rule):** a 1.65 G NPC ribbon (29 m) is 2.8 px long on
  screen when 10 km away, 1.4 px at 20 km, 0.56 px at 50 km; a player War power ribbon (60 m) 5.8, 2.9 and 1.2 px. The
  distances only show what a pixel size corresponds to; the mock-up rendered at those distances to give the user
  pixel sizes to judge, and the user chose pixel thresholds (R2/R3).

### 4.9 Per-hull nozzle data (D8, Q11, Q18)

A constant table in `SOLConstants.h` now (a data asset with the real models later, TECH_DEBT line 106): per hull kind,
hull length, main nozzles (position and axis in the hull frame, radius `r_n`), tail ratio 0.35 and the burst ports
(Q31). Entries: NPC faction 0 cone and faction 1 box, 30 m, 1 nozzle on the aft centre, `r_n` **1.5 m (provisional,
Q11)**; player, `PLAYER_HULL_LENGTH_M` **20 m**, 2 nozzles at the engine cylinders' aft faces (x -8.0 m, y +-2.2 m, z
-0.2 m), `r_n` 0.8 m.

### 4.10 Rendering and instancing (Q1, Q14)

**R10 (section 11.2, impact table there):** the hull meshes lose their Plume section and `M_SOLThrusterPlume` is
retired; the Glow and Burst sections and the per-instance custom data stay (the true plume length float, index 2, is
then read by no section). The flame is drawn by the ribbon renderer(s) of this section; whether Niagara stays is open
(R10-Q6). The as-built notes below describe 51b-51e before R10.

- **Cones, glows and bursts are sections on the hull mesh** driven by per-instance custom data (Q1): the placeholder
  hull meshes gain the plume, glow and burst sections, scaled by world-position offset, so the existing transform upload
  carries them and only custom floats are added. Per instance: `c`, `b`, a plume length scale (true length over drawn
  scale, undoing the 3 px inflation, Q14) and the three burst floats: 6 custom floats (`NumCustomDataFloats`). The mesh
  pool uses custom primitive data, the player pawn its own plume and burst meshes with the same materials. The hull ISMs
  already have fixed bounds, so WPO growth is safe.
- **Ribbons:** one array-fed Niagara ribbon system (D1 applies to the ribbons only, Q1), fed from the history arrays of
  the ribbon set. **Superseded in 51e by R7 (section 11.1):** a hybrid. `NS_SOLThrusterTrails` (one system instance
  per trail) draws the player's two trails and the NPC ribbons at least `RibbonNiagaraMinPx` long on screen, up to the
  physical Niagara pool size; every other shown ribbon is drawn by the GPU strip pool (`M_SOLThrusterRibbon`, one
  instanced draw). Membership is a per-frame pure draw choice by pixel size.
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
- **As built in 51c** (`Npc/SOLNpcVisuals.cpp`): both hull layers draw the thruster meshes; the plain engine shapes
  stay loaded as the A/B baseline behind the developer cvar `sol.NpcPlumes 0` (`SOL::NPC_PLUME_CVAR`; the hull ISMs
  then carry no custom floats at all, and `ViewPlumeGateM` returns 0 so the Collect pass skips the thrust, as before
  #51). The placement
  ParallelFor writes `SOLThruster::PlumeInstance` (c, b, true `L_p`; all zero under the cone's 0.5 px low edge, where
  the material's alpha is 0 anyway) through `PlumeCustomData` (bursts 0 until 51g) into a per-faction scratch and
  notes per task the lowest and highest slot with a plume. Only the union of this and last frame's plume slot ranges is
  uploaded (one ranged `SetCustomData` per faction; every instance outside it already holds zeros, slots past this
  frame's use are written as zeros, a shrink rebuild or a cvar switch resets the bookkeeping); the range spans the
  slots between the first and last ship with a plume, usually far fewer than the 20,000 hulls since the near (Full)
  ships are collected together in chunk order. A pool mesh gets
  `SetCustomPrimitiveDataFloatArray` (sent to the render thread only when the floats change). NPCs never fly War power,
  so `c` follows the G ramp.
- **As built in 51e (renderers, 2026-11-21; wiring 2026-11-22, see the next bullet):** owner **`ASOLThrusterRibbons`**
  (`Visuals/SOLThrusterRibbons.{h,cpp}`, spawned by `ASOLGameMode::StartPlay`, `GetThrusterRibbons`), chosen over
  `ASOLNpcVisuals` because the player pawn feeds it too and the draw must run once after both feeders. It holds the
  **shared history store** (one `FSOLPlumeHistory` per owner in a reserved, grow-only slot array with a free list and a
  `TMap` key -> slot; key = NPC index, the player's nozzles `-1 - nozzle`), the **Niagara set** (16 `NS_SOLThrusterTrails`
  components created and activated once in `BeginPlay`, `ENiagaraTickBehavior::ForceTickLast`) and the **strip pool**
  component. Feeding: `BeginNpcFeeds(slotCount)` returns per-slot `FSOLRibbonFeed` scratch that a `ParallelFor` writes
  one slot per task (no shared state), `CommitNpcFeeds()` commits serially (find or create the slot, copy the state,
  push the feed's sample when burning and `ShouldSample`, emission time = the history clock); `FeedPlayer(nozzle, feed)`
  commits one player nozzle. Feeds are dropped while `sol.ThrusterRibbons` is 0. Positions are render-space metres
  (Unreal axes) **after this frame's rebase**, so feeders must run in the universe update (the NPC redraw, the
  player's `OnUniverseUpdated`), never in the pawn's pre-physics tick (the spike's first run fed from there and the ribbon landed far off
  screen: a frame of ship motion and any rebase behind).
  The draw is the actor's own tick in **`TG_PostUpdateWork`** (after the tickable anchor update and the camera, before
  Niagara's `TG_LastDemotable` tick): history clock `UWorld::GetTimeSeconds` (R8); camera and the real projection
  (`ULocalPlayer::GetProjectionData`: device focal `RibbonDeviceFocalPx(ViewToClip[1][1], constrained view height)`,
  reference focal `FocalRefPx(fov, aspect)`); frame anchor `RibbonAnchorCm(camera)` (whole metres, exact in float, the
  same value for the Niagara component location and the pool's `ThrusterRibbonFrame`); per live slot the pixel rule
  (`PixelLength(RibbonNominalLengthM(L_p, newest drift), d, f)`, `RibbonPixelFade`, the player's fade 1) and the
  release rule `RibbonTrailReleasable` (no sample, newest sample at least `A` old, or not fed this frame with fade 0; a
  slot not fed this frame is kept but not drawn); `SplitRibbonRenderers` with `RibbonNiagaraMinPx` (or the
  `sol.ThrusterRibbonNiagaraPx` override) and 14 free components; the player's trails (at most 2) then the Niagara
  list get components in order. A Niagara push is `BuildRibbonPoints` into a stack array, always 25 array entries
  (padding = the last point at alpha and width 0, so the arrays never reallocate and Niagara's 1 cm rule drops the
  padding), widths floored per point with `RibbonDrawnWidthM(w, |anchor + p - camera|, device focal, 1)`, three array
  setters, the point count and the component moved to the anchor; an unused component gets count 0 once.
  `STAT_SOL_ThrusterRibbons` (group `SOLThruster`) times the draw. **Pool** `USOLThrusterRibbonComponent`
  (`Visuals/SOLThrusterRibbonComponent.{h,cpp}`, a `USOLNpcHullInstances` subclass for the fixed bounds): block mesh
  built at runtime (32 trails x 25 points x 2 vertices, UV0 = point and side, UV1.x = trail in block, all at zero,
  `ApplyRibbonToMesh`), identity instances added grow-only (custom data 0 = first trail; a block past the live count
  collapses in the vertex shader), a `PF_A32B32G32R32F` texture 1024 wide with rows doubling on growth (one
  `FlushRenderingCommands` per growth, never per frame), a `ParallelFor` (batches of 16) filling each trail's 32 texels
  with `BuildRibbonPoints` + `PackRibbonTexels` into a ring of 3 upload buffers, the live rows uploaded, the frame
  vector set, hidden when no trail. **Device pixel fix:** `SOLTH_RibbonVertex` is now called with
  `ViewSizeAndInvSize.y x ResolutionFractionAndInv.y` (the output height), so a temporal upscale below 100 % does not
  widen the pool's floor against the CPU's (`M_SOLThrusterRibbon` rebuilt: VS 383 / PS 100). **Parity spike**
  (`-SOLSmokeRibbonParity`, `Visuals/SOLThrusterRibbonParitySmoke.{h,cpp}`, -game, 1920x1080, `-SOLNpcCount=0`, frozen
  history clock, the trail fed through `SetVerificationFeeder` with this frame's camera): a 90 m curved ribbon at 160 m
  and a 900 m one at 25 km (1.5 m wide, drawn by the 1 px floor), each drawn by Niagara and by the pool twice: 10/10
  checks. **Near: equal within noise.** Cross-sections at 6 rows: peak, FWHM (10-20 px), summed gain and centroid
  equal within the A/A spread (centroids within 0.1 px, sums within 0.5 %); on a ribbon mask (on - off > 8, dilated 2
  px, the off shot's lit pixels dilated 6 px removed) N/P mean 0.82 / 0.83 against A/A 0.98 / 1.20 (8-bit levels).
  **Far 1 px line: equal; the earlier mismatch was temporal AA noise, not the renderers (2026-11-22).** The
  script now sweeps 14 configurations per launch (near; far at four quarter-pixel phases; farfat, the far geometry 10+
  px wide; multi, 40 trails of which 14 go to Niagara and the pool shot spans two pool blocks; each with the project's
  TSR and with `r.AntiAliasingMethod 0`) and logs the camera, the anchor residual, the device focal and the view state
  per shot. Three launches of one build, 70/70 checks each, 8-bit levels on the ribbon mask: **AA off, far line:**
  Niagara / pool mean difference 0.50 (max 0.59) against the same-renderer A/A 0.98 (max 1.15), largest pixel 29
  against 47, integrated gain pool / Niagara 0.992-1.008, row centroids within 0.094 px; near 0.26 against 0.44, fat
  0.35 against 0.63, multi 0.79 against 1.50 (gain 1.001), so the two strips rasterise the same pixels at 1 px too
  (N/P is below A/A because the N/P pairs are adjacent shots and the scene behind drifts). **With TSR, far line:** the
  same renderer differs from itself between shots by as much as the two renderers do (A/A gain 0.83-1.26, mean 2.44;
  N/P gain 0.88-1.20, mean 2.15, max 6.88 against A/A 5.40), so the earlier "pool 10-13 % dimmer, centroids 0.5 px
  apart" run was one draw of this frame-to-frame spread of TSR's resolve of a sub-pixel (1 px floor) line, not a
  geometric difference. Nothing in the renderers was changed. **Observation for 51f (not a parity issue):** under TSR
  a far 1 px ribbon's brightness varies by up to about 25 % from frame to frame in both renderers. The camera moved
  about 7 m a frame in render space in these runs (anchors in the log). At `r.ScreenPercentage 50` (earlier run) the
  output-height floor change was not contradicted.
  **Same frame:**
  by tick order (engine source: tickable objects, then the camera, then `TG_PostUpdateWork`, then the Niagara
  `ForceTickLast` tick in `TG_LastDemotable`); the near centroid agreement is consistent with it (a one-frame Niagara
  lag would move the near ribbon by the anchor rounding residual, up to about 1 m, about 5 px). **Exercised since:** 40 trails at once (14 Niagara
  components and the pool's second block, the multi configuration above); the player's trails come with the wiring. **Tie-break deviation:** the split receives store slots as its `NpcIndex`, so an exact px tie goes to the
  lower store slot, not the lower NPC index (R7's tie rule); deterministic, recorded rather than paid for with a
  per-entry key lookup. **Cost: not measured yet** (step 7); the 16 always-active Niagara instances are the open risk
  against the 0.05 ms gate.

- **As built in 51e (wiring, 2026-11-22):** NPC hull-drawn ships (pool meshes and instanced hulls) are fed from
  `ASOLNpcVisuals::UpdateCpuShips`: the ribbon switch and V read once before the placement `ParallelFor`, one feed slot
  per hull-drawn ship (pool-mesh ships fed serially, instance slots by the tasks), a feed written only for a burning
  ship whose ribbon reaches V on screen (`RibbonPixelLength`) or one that already has a trail
  (`ASOLThrusterRibbons::HasTrail`, a read-only map lookup while nothing writes the store), from the raw
  `ThrustLocalG.X` (`PlumeLengthM`, `PlumeBrightness`, `PlumeColourScalar`; not the cone-gated `PlumeInstance`), the
  nozzle exit `HullThrusters(kind).Nozzles[0]` along the drawn heading and the exhaust `-heading`, both in world axes;
  `CommitNpcFeeds` after the pass. Off with `sol.NpcPlumes 0` (no thrust is collected then). The Collect gate is
  `SOLThruster::NpcThrustGateM` (contract case 19). The player pawn feeds its two nozzles from `UpdateThrusterPlume`
  (the universe-update placement) through `ShipRoot`'s transform, only while `sol.PlayerPlume` is 1. A trail keeps its
  last burning colour while it fades (R7-C).

- **Cost fixes in 51e step 7 (2026-11-22, the user's order: frustum, feeds, Niagara; supersedes the "before" numbers
  in the next bullet).** (1) **View-frustum cull** in `ASOLThrusterRibbons::Tick`: the planes of the real projection
  (`GetViewFrustumBounds` of `FSceneViewProjectionData::ComputeViewProjectionMatrix`); a shown NPC trail whose bound
  sphere (centre the ship; radius = nozzle offset + the largest stored drift x `A` + `2 r_n` + a 2 px floor margin at
  its distance) misses the frustum is neither built nor drawn and takes no Niagara component. The player's trails are
  never culled (R5). **History decision:** an off-screen ship keeps being fed (its samples keep being pushed), so a
  ship the view turns onto shows its full, correctly curved ribbon at once; the feed was made cheap instead of being
  skipped (a skipped feed would leave a gap: a 1 s grow-in or a straight synthetic ribbon on a turning ship). The cull
  is in the tick, not the feeders, because the feeders run before the camera update. (2) **Feeds:** the placement
  tasks look the trail up (`FindTrail`, read-only) and commit an existing trail in place (`FeedNpc`; each key, so each
  slot, is fed by one task); only a new trail goes through the serial `CommitNpcFeeds`. The per-frame state is split
  from the sample rings (`mHistories`; newest sample, count and largest drift cached, the largest drift kept
  incrementally), cache-line aligned, and walked through a dense live list by a `ParallelFor` store pass (release,
  pixel rule, cull), then a short serial pass. (3) **Niagara:** components active only while drawing (R7 above).
  Profile (interleaved A/B with `sol.ThrusterRibbons`, -game, Epic, 1M NPCs, combat on, 1920x1080, 8 pairs per view,
  one launch per row; whole-frame delta mean (sem); ribbon tick = the actor's own tick):

  | Step | Spawn view frame | Spawn ribbon tick | Spawn redraw | Lane frame | Lane ribbon tick |
  |---|---|---|---|---|---|
  | Before | +1.11 (0.10) | 0.50 | +0.21 | +0.08 (0.05) | 0.067 |
  | (1) frustum | +0.70 (0.10) | 0.25 | (commit 0.16) | +0.11 (0.03) | 0.069 |
  | (2) feeds in place | +0.50 (0.08) | 0.19 | +0.22 | +0.20 (0.10) | 0.054 |
  | (3) Niagara on demand | +0.45 (0.18) | 0.20 | +0.22 | +0.08 (0.06) | 0.052 |
  | (2b) parallel store pass | +0.46 (0.13) | 0.14 | +0.20 | +0.03 (0.03) | 0.050 |
  | Final (sub-timers) | +0.34 (0.10) | 0.14 | +0.20 | +0.02 (0.04) | 0.051 |
  | Final, every NPC in the pool | +0.40 (0.14) | 0.14 | +0.21 | -0.00 (0.04) | 0.027 |

  **Spawn view (stress, about 980 live trails, about 25-29 shown on screen, 770-780 culled, no NPC trail in
  Niagara): +0.34 ms, gate 0.05 not met.** Breakdown: the NPC feeds in the placement pass +0.20 ms (about 1,000
  ships fed every frame, memory-bound: each feed writes the trail state and every second or third frame a ring
  sample), the store pass 0.086 ms (about 1,000 live trails), the strip pool fill and upload 0.03 ms, the rest about
  0.02 ms. Reaching the gate there would need feeding only on-screen ships, i.e. dropping the off-screen history
  continuity chosen above (a user decision). **Lane ride-along (typical, 1 ship on screen, Niagara): frame +0.02 ms
  (sem 0.04), the ribbon tick itself 0.051 ms, of which the one Niagara trail's three array setters and point count
  cost 0.024 ms and the component move 0.005 ms;** with the NPC ribbon in the strip pool instead
  (`-SOLSmokeRibbonNiagaraPx=1000000000`) the tick is 0.027 ms. So the typical set is at the gate with one Niagara NPC
  trail and clearly under it without; a higher Niagara threshold is the user's call (R7). GPU: not resolvable in
  either view (spawn -0.03 to -0.06, lane +0.00 to +0.05 ms, noise about 0.02-0.03 sem), the #51 total stays inside
  0.20 ms. The side-view player shot at 1280x720 that faced the battle drew 14 NPC Niagara trails and 146 pool trails
  with a 0.41 ms tick (the dense on-screen battle is the remaining stress case).
  **No-pop pair, valid (2026-11-22):** the frozen sequence now runs while riding the U-turn lane ship at 400 m (its
  2.36 G ribbon 84 px, Niagara, on screen), combat off during the freeze, the invariant the camera relative to the
  ridden ship (within 1 cm). Crop diff on its ribbon mask (795 px), 8-bit levels: AA off, adjacent Niagara / pool
  shots mean 0.047 (max 2) against same-renderer A/A 0.23 / 0.37, integrated gain pool / Niagara 1.0002; TSR adjacent
  0.28 (max 7) against A/A 0.29 / 0.46, gain 1.0008. The switch does not pop. **U-turn ribbon (not a bug):** a lane
  U-turn reverses the velocity with the heading held, and samples are ship-relative with the exhaust drift, so the
  ribbon is straight and collinear with the plume, extending it to 3x its length; only heading changes curve a ribbon.
  Screenshots at 1920x1080 and 1280x720 viewed (the 1 px floor holds at 720p); kept
  `Docs/Images/51-thruster-ribbon-curved.png` (player) and `Docs/Images/51-thruster-ribbon-npc.png` (the U-turn ship's
  plume and ribbon); no NPC with a strongly curved ribbon was captured (needs a ride on a manoeuvring combat ship).

- **Measured in 51e step 7, before the fixes (2026-11-22; superseded by the bullet above):** `-SOLSmokeThruster -SOLSmokeThrusterRibbons`
  (`FSOLThrusterSmoke`, verification only; with `-SOLSmokeThrusterProfile` the interleaved A B B A windows switch
  `sol.ThrusterRibbons` 1 / 0, and B also deactivates the 16 Niagara components through
  `ASOLThrusterRibbons::SetVerificationNiagaraActive`, because at `sol.ThrusterRibbons 0` they stay active at point
  count 0 and their idle instance ticks would otherwise be in both states). New verification getters:
  `GetLastTickMs` (the actor's tick, cycle-timed), `GetDrawnNpcTrails`. One launch, -game, Epic, 1M NPCs, default
  budgets, combat on, 1920x1080, VSync off, 8 pairs per view (pair delta = A - B of adjacent windows, 240 frames each;
  mean, sd over the 8 pairs):
  - **Earth spawn view (the typical set of R9 and the stress view, about 680 lit of 1,220 hulls):** whole frame
    **+1.11 ms** (sd 0.28) and game thread +1.11 ms (sd 0.27) against the **0.05 ms gate: missed about 22x**. Parts:
    the ribbons' own tick **0.50 ms** (sd 0.02; 14 Niagara trails + about 780 pool trails, about 980 live
    histories), `STAT_SOL_NpcRedraw` +0.21 ms (the feeds in the placement pass), NPC update +0.02 ms (noise), Collect
    +0.003 ms; the remaining about 0.4 ms of game thread is outside these scopes (most likely the 14 Niagara
    instances' game-thread tick / sync and the pool's texture upload; not yet profiled per scope). GPU frame -0.03 ms
    (sd 0.07): not resolvable, inside the noise.
  - **Lane ride-along (1,500 m from a braking Earth-Mars ship, 1 hull, 1 trail in Niagara):** whole frame +0.08 ms
    (sd 0.14), game thread +0.40 ms (sd 0.29, noisy), ribbon tick 0.067 ms for a single Niagara trail (sd 0.003; already
    over 0.05 ms alone), GPU +0.05 ms (sd 0.05).
  - **Top costs:** (1) the ribbon tick scales with the shown set: about 800 trails at the spawn view, most of them
    off screen: the pixel rule and `SplitRibbonRenderers` do not look at the view frustum, so off-screen battle
    ribbons are built and drawn and take Niagara components (all 14 Niagara-drawn trails in the spawn view projected
    off screen); (2) the NPC feed (+0.21 ms); (3) the Niagara set's fixed cost (0.067 ms with one trail). Not
    re-architected: the caller / user decides (options: frustum test before the split, cheaper feeds, fewer
    Niagara components).
  - **GPU, whole #51 budget (0.20 ms at 1080p):** ribbons +0.05 ms (lane) / not resolvable (spawn), plus the 51c plume
    (at most +0.03 spawn, +0.05 lane) and the 51d player plume and light (+0.03 to +0.07 ms): about 0.15 ms worst case
    summed, inside the gate, but each figure is near its noise floor.
  - **Screenshots (1920x1080):** `Docs/Images/51-thruster-ribbon-curved.png`: the player thrusting at 3 G while
    yawing at 0.8 rad/s, its two curved ribbons (Niagara), side view. The far "1 px line" shot is **not** usable: the
    hardest-burning lane ship (1.63 G) at 22 km has a 1.03 px ribbon, so R2 fades it to alpha 0.01 (by design; a far
    ribbon needs a War-power-class burn to be seen), and the spawn view's ribbons at 20+ km are 1.5-3.6 px long and
    were off screen. A U-turn lane ship (2.36 G, ridden at 400 m, Niagara, 84 px by the rule) showed its plume but no
    clearly separate ribbon in the crop: to be looked at (the samples are ship-relative, so only heading changes curve
    a ribbon). 1280x720 not yet shot.
  - **No-pop pair: not yet valid.** Freezing by `UGameplayStatics::SetGlobalTimeDilation(0.0001)` plus the ribbons'
    verification clock stops the game time (8.4919 s to 8.4927 s over the sequence), but (a) the combat bolts keep
    moving between shots, (b) the camera and anchor drift about 2 m per shot in render space (absolute motion at the
    minimum dilation), (c) live trails grew 888 -> 1,012 while frozen (new feeds), so the pool-count check failed, and
    (d) every Niagara-drawn NPC trail in the spawn view was off screen, so the pair has nothing to compare. Needs a
    view facing a battle at a few km with combat off during the freeze, and an invariant relative to a ship rather
    than absolute.

### 4.11 Assets (Q20)

**R10 (as built in 51e3):** `M_SOLThrusterPlume` and the Plume mesh sections are deleted; the flame look moves into the ribbon
material(s); `M_SOLThrusterTrail` and `NS_SOLThrusterTrails` are reworked or retired by R10-Q6 (impact table, 11.2).

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

**As built in 51e (step 4 assets, 2026-11-21):** the script also builds **`M_SOLThrusterRibbon`** (the GPU strip pool,
R7: additive, unlit, two-sided, ISM usage; vertex Custom node `Load`s the trail's point, its neighbours p - 2 .. p + 2,
the header and the width texel from `ThrusterRibbonTex` and calls `SOLTH_RibbonVertex`; pixel `SOLTH_TrailPixel` as on
`M_SOLThrusterTrail`) and its placeholder texture default **`T_SOLThrusterFloatDefault`** (linear TC_HDR copy of the
engine default texture). Parameters: `ThrusterRibbon` (min width px, texels per trail, trails per block, points per
trail), `ThrusterRibbonFrame` (anchor in render-space cm, live trail count; default 0 draws nothing), `ThrusterTrail`
(the same emissive vector as the trail). The block mesh is built at runtime by the pool component (layout in the
`SOLThruster.ush` header). `NS_SOLThrusterTrails` rebuilt as one trail per system (`TrailMaxPoints` 25), ribbon
tessellation **Disabled**, facing **Screen** (the script reads both back). Compiled instruction counts: ribbon VS 382 /
PS 100 (0 samplers: `Load` only).

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
placeholder hulls); 51g assembles it. **R10:** this list predates 51e; the contract is the current list. R10 deletes
`RibbonDriftMps`, `RibbonPoint`, `RibbonAlpha`, `RibbonWidthM` and `BuildRibbonPoints` and adds `FlameLengthM`,
`FlameWidthM`, `FlameAlongAlpha`, `FlameTurnOffsetM`, `BuildFlamePoints` and the pixel-envelope references
`FlameCoreWeight`, `FlameKnotIntensity`, `FlameStreakDepth` (contract cases 20-24).

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
9. `NearestRibbons` (rewritten in 51e as the pixel rule): `RibbonShown` / `SelectRibbonShips` with no count cap and
   no radius; shown when (plume or fading ribbon) and on-screen length `p >= V`; no ranking; 1,000 and 5,000 candidates
   all shown.
10. `QualityScaling` (rewritten in 51e): `V` per preset Low 10, Medium 4.6, High 2.2, Epic 1, Cinematic 1 px; the
    player's ribbon at every preset.
11. `Constants`: `G_REF == WAR_POWER_PULSE_G`; the colour keys ordered; 24 samples within 16-32; `A` = 1 s; the nozzle
    table.
12. `MaterialMirror` (51b): the HLSL mirror equals the C++ ramp, brightness and knots at sample points.
13. `GasBursts`: the direction mapping, independence from the main plume, the threshold and clamp (threshold and
    reference as test parameters, start values 0.05 G and 1.5 G, Q32); `BurstLengthM` (hull length / 6 x strength,
    strength clamped, Q32); `PlumeAllowed` (no plume or burst on dot-drawn ships, Q27).
14. `PlayerLight`: intensity proportional to `b`, zero when off, colour equal to the ramp's rim colour;
    `PlayerLightEnabled` (on in VR at every quality, otherwise off only at Low, Q28).
15. `RibbonPixelRule` (51e): on-screen ribbon length, the linear fade between `4 V` and `V`, the 1 px width floor, the
    player's alpha factor 1 (R1-R5).
16. `RibbonRendererSplit` (51e): `SplitRibbonRenderers`, Niagara for NPC ribbons at least `RibbonNiagaraMinPx` long
    (within the free trail count), the strip pool for the rest (R7).
17. `RibbonPoints` (51e): `BuildRibbonPoints`, the one shared point builder for both renderers.
18. `RibbonTexels` (51e): `PackRibbonTexels`, the strip pool's per-trail texture rows.
19. `ThrustGate` (51e): `NpcThrustGateM`, the Collect pass's thrust gate derived from the pixel rule (R4).
20. `FlameProfile`, 21. `FlameParcels`, 22. `FlameRigidity`, 23. `FlameSeam` (R10): the width taper, the along-arc
    fade, the ballistic parcel spine against the closed-form turn offset, and the seam sweep; 24. `RibbonRendererMath`
    (R10 release rule); cases 7, 11, 12, 18 and 19 rewritten and 17 deleted for R10.
    (Exact values in `Plans/51-thruster-math-contract.md`.)

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
  screen and wash the HUD under bloom. **R10:** with no 3D cone, a flat camera-facing flame seen along its axis shrinks
  to a sliver; the cross-section is open (R10-Q2).
- **Colour clash.** Red-orange resembles faction 1's orange (accepted in D4); NPCs never reach it today (0.825-1.65 G).
- **Nominal profile.** A ship marked Coast in the end zones is really slowing, so the plume is off where a tiny thrust
  exists (accepted, SDD 50 section 4.12).
- **Overdraw.** Many overlapping additive cones and translucent bursts in a busy lane close up.
- **Scope increase.** Gas bursts (Q7) and the player light (Q17) were added after the first draft; they add a material,
  hull sections, 3 custom floats and a light (Q27-Q33 resolved).
- **Ribbons uncapped at every preset (Q29, superseded by R4).** No count cap at any preset, by design. The cost is
  bounded by the pixel rule (a ribbon under `V` px is not drawn) and by the view-frustum cull (an off-screen NPC ribbon is
  neither built nor drawn). The stress case is a dense on-screen battle; history memory grows with the number of
  burning hull-drawn ships.
- **Assets.** A hand-edited asset would drift from the script; the script is the only source (Q20).

## 10. Persistence

None. No `USaveGame` field, no `UGameUserSettings` field, no config layout change. `SAVE_VERSION` stays 13,
`KEY_BINDING_LAYOUT_VERSION` stays 10. The `ThrustLocalG` fields are transient plain fields. Graphics quality is read
from the existing `QualityPreset` (Q12).

## 11. Resolved decisions (user, 2026-11-20 and 2026-11-21)

- **Q1 Instancing:** the cone and glow (and the gas bursts) are a section on the hull mesh driven by per-instance custom
  data; Niagara is used only for the curved ribbons, so D1's "Niagara" applies to the ribbons only. [R10: the cone
  section goes; the glow and bursts stay hull sections; the flame is drawn by the ribbon renderer(s), R10-Q6.]
- **Q2 NPC thrust source:** per tier: Mid lane ships from the rail acceleration profile in the tier processor's Collect
  pass; Full ships from the Full AI's achieved acceleration; the lane flip coast is zero.
- **Q3 Non-lane NPCs:** no plume on Mid non-lane ships (no thrust model) until they are Full.
- **Q4 Ribbon frame:** history relative to the ship in non-rotating world-aligned axes with an exhaust drift (stable
  under origin rebasing), section 4.8.
- **Q5 / Q14 (pixel rule, limited by Q27):** plume visibility (cone, glow) is judged by the plume's on-screen pixel
  size, not by tier, on ships drawn with a hull. Any hull-drawn ship with thrust data (Mid lane rail thrust, Full AI)
  whose plume is about 1 px long (tuned in game) gets one; **dot-drawn ships get none (Q27)**. The plume uses the true
  hull length, not the inflated drawn size. Ticket #54 (hide ships under 1 px) is out of scope and may revisit this.
  [R10: the flame is judged by the ribbon's pixel rule on `3 L_p` (R10-7, R10-Q3); the glow keeps this rule.]
- **Q6 Above 6.8 G:** clamp length, brightness and colour at `G_REF`.
- **Q7 Non-forward thrust:** reverse, lateral and vertical thrust show clear gas bursts (short, nearly transparent puffs,
  visually different from the main flame) for the player and NPCs. NPC per-direction thrust is derived from the
  acceleration vector (velocity change in the ship frame) on all drawn ships. Section 4.6; see Q27, Q30-Q32.
- **Q8 Player sampling:** the time-weighted mean of the step's substeps, no extra smoothing, tuned in 51f.
- **Q9 Ramp:** blue-white to 2.5 G, a smoothstep to yellow-white at 3 G, linear in G to red-orange at `G_REF`; colours
  from the mock-up the user chose (section 4.3).
- **Q10 War power:** two floats, colour `c` and brightness `b`; War power forces `c` to 1 while `b` follows the real G.
- **Q11 Cone shape:** tail radius 0.35 x nozzle radius; NPC nozzle radius 1.5 m on a 30 m hull (provisional).
  [Tail ratio superseded by R10: 0.1 x nozzle radius at `L_p`, widest at the nozzle; the nozzle radius stays.]
- **Q12 Quality hookup (superseded by R3/R4, section 11.1):** read the existing `QualityPreset` (no schema change). Now
  `V` per preset (Low 10, Medium 4.6, High 2.2, Epic 1, Cinematic 1 px), no count or radius, the player at every
  preset. Old answer: Low 0, Medium 8, High 16, Epic 32 ribbon ships within 5 km, Cinematic unlimited, the player
  always except at Low.
- **Q13 Glow set:** by pixel size too: any ship whose glow is at least about 1 px.
- **Q14:** see Q5.
- **Q15 Pixel thresholds:** resolved by the pixel rule: cone fades under about 1 px, ribbon on above about 8 px with a
  fade band; start values, tuned after 51e-0. [Superseded by R2/R3 and R10-7: no cone fade.]
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
  ships with a plume or a fading ribbon are in the set. [R10: the width becomes the R10 taper; the 1 s age, the age
  fade and the 3x length are replaced by the ballistic flame, which conflicts with a 1 s tail: open question R10-Q1.
  25 points stay.]
- **Q25 Look constants:** the section 4.4 start values (cut-off 0.01 G, core layering, streak pan rate, rhombus size),
  tuned in game.
- **Q26 Test list:** `Tests/ThrusterMathTest.cpp`, `SOLTest.Thruster.*`, the section 7 cases, extended for the pixel
  rule, gas bursts, the clamp above `G_REF`, the colour ramp and the player light.

- **Q27 Plumes vs dots (conflict resolved; flame part superseded by SDD 54 Q7, see 4.5):** plumes **and gas bursts** appear only on ships drawn with a hull mesh
  (mesh tier or instanced hull). Dot-drawn ships get none. The earlier "pixel rule for dots" is limited to: the plume's
  pixel size decides fade and visibility on hull-drawn ships; no plume on points. #54 may revisit.
- **Q28 Player hull light:** on in VR, off at Low quality, on otherwise; on its own lighting channel.
- **Q29 Cinematic ribbons (superseded by R4, section 11.1):** now no count or distance cap at any preset, only the
  pixel rule. Old answer: no cap at all for every ship within 5 km. The frame-spike risk is a note (section 9).
- **Q30 NPC burst acceleration:** only ships with a drawn hull, smoothed over about 0.25 s, evaluated in the existing
  Collect pass (no extra pass), profiled in 51g.
- **Q31 Burst ports:** a fixed per-hull port table in `SOLConstants.h`: a bow pair plus 4 side ports (stern ports dropped) (reverse =
  bow ports, lateral = side ports, vertical = top / bottom ports).
- **Q32 Burst size:** very small next to the main plume, a few metres: about 5 m on a 30 m hull (hull length / 6),
  opacity may scale with G, clamped at 1.5 G; tuned in 51f.
- **Q33 Glow floor:** 1 px (matches the pixel rule), replacing the 1.2 px draft.
- **Accepted starting values (tuned in 51f):** a distance gate for plumes instead of tier gating, fade edges 0.5 px and
  6 px, glow diameter 2.5 x nozzle radius, the hull light on its own lighting channel.

Q1-Q33 are all resolved; R10 (11.2) raises the open questions of 11.3.

### 11.1 Part 51e decisions (user, 2026-11-21, after the 51e-0 mock-up; they replace Q12, Q15's ribbon edge and Q29)

Mock-up (30 m hull, 960 px reference focal, ribbon length `3 L_p`): at War power (6.8 G, `L_p` 30 m, ribbon 90 m) the
ribbon dims from 21.6 km and is gone at 86.4 km with `V` = 1 px; at 1.65 G (ribbon 29 m) it dims from 7.0 km and is gone
at 27.8 km. Two ribbons in one Niagara system merged into one zigzag.

- **R1 Width floor:** a ribbon is drawn at least **1 px** wide at every distance (fires are bright). The floor is in
  device pixels (the pixel size of the real viewport), so it is never sub-pixel at 1280x720.
- **R2 Brightness rule:** with `p` the ribbon's on-screen length (`PixelLength(3 L_p, d, f)`, 1080p-reference px like
  every other pixel rule) and `V` the vanish length: full brightness while `p >= 4 V`, dimming **linearly** (alpha, so an
  additive ribbon gets darker) from 1 at `4 V` to 0 at `V`, gone below `V`. A fading ribbon (the plume off) is judged by
  its stored samples' nominal length (`drift x A`, 3x the plume each sample was emitted with), so it does not pop when
  the thrust cuts. [R10: the rule now fades the whole flame, `p = PixelLength(3 L_p, d, f)`; a shutting-down flame is
  judged by the `L_p` it last burned with (there is no drift).]
- **R3 V by quality** (the existing `QualityPreset`, no schema change), geometric starting steps, tunable constants
  `RIBBON_VANISH_PX_BY_QUALITY`: **Low 10, Medium 4.6, High 2.2, Epic 1, Cinematic 1** px; fade start
  `RIBBON_FADE_START_FACTOR` 4 x `V`.
- **R4 No distance cap and no count cap:** the 5 km range and the Low 0 / Medium 8 / High 16 / Epic 32 counts are
  removed (`RibbonCountForQuality`, `RIBBON_COUNTS_BY_QUALITY` and the radius / count of `SelectRibbonShips` are
  deleted). Every **hull-drawn** ship whose ribbon passes the pixel rule gets one, at every preset (Low too: its
  ribbons go dark at 10 px). The Collect pass's thrust gate grows to the ribbon's reach,
  `max(PlumeMaxDistanceM(L_hull, f, 0.5), PlumeMaxDistanceM(3 L_hull, f, V))`. This is a work bound derived from the
  pixel rule (past it even a hull-length plume's ribbon is under `V` on screen), not a level-of-detail distance.
- **R5 Player:** the player's two nozzles always have ribbons, at every preset, with no pixel gate (alpha factor 1).
- **R6 Unchanged:** the cone fade and the hull drawing (hiding sub-pixel hulls is ticket #54). [R10: the cone fade
  goes with the cone; the hull drawing is still unchanged.]
- **R7 Renderer: hybrid (user, 2026-11-21; confirmed by R10-Q6: the user kept the hybrid for the one-piece flame
  over the recommended pool-only renderer).** The user chose it over the pure GPU strip pool the previous agent had
  picked when the renderer choice was delegated (rejected option, for the record: one strip pool for every ribbon,
  player included, Niagara unused). Two renderers draw the same ribbon:
  - **Niagara:** `NS_SOLThrusterTrails` with `M_SOLThrusterTrail`, **one system instance per trail** (two trails in one
    system merge into a zigzag, 51e-0). It draws the player's two trails (always, R5: the player is the only exemption
    from the pixel split) and every shown NPC ribbon whose on-screen length `RibbonPixelLength` is at least the Niagara
    threshold `RibbonNiagaraMinPx` = `RIBBON_NIAGARA_PX_FACTOR` (start 8, tunable) x `V`: **Low 80, Medium 36.8,
    High 17.6, Epic 8, Cinematic 8 px.** Why 8 px at Epic (51e-0 mock-up, 960 px reference focal): it is about what a
    War power 6.8 G ribbon (90 m) looks like at 10 km (8.6 px, 17 px at 5 km) and a 1.65 G ribbon (29 m) at 3.5 km,
    the size where a ribbon has become a short line and Niagara's per-point detail no longer shows. The split is by
    pixel size only, never by distance or by a ship count (project rule).
  - **GPU strip pool:** every other shown NPC ribbon (`RibbonShown`, R2), in one instanced draw: one strip mesh in
    blocks of `RIBBON_TRAILS_PER_BLOCK` (32) trails, one ISM instance per block, the block's first trail in its custom
    data (like the NPC point blocks), and a float texture of `RIBBON_TEXELS_PER_TRAIL` (32) texels per trail (25 points:
    position relative to the frame anchor and alpha, then a header: colour and nozzle radius, then the widths of
    points 1..24; `PackRibbonTexels`),
    written each frame by a `ParallelFor` over the live trails into a reused ring of upload buffers. The vertex shader
    (`SOLTH_RibbonVertex` in `Shaders/Private/SOLThruster.ush`) faces each point to the camera and applies the same
    width as the Niagara trail with the 1 px device floor (the widths are packed in the trail's texels, R7-W below);
    trails never merge because each has its own strip.
  - **Niagara pool size, a limitation and not a level-of-detail rule:** the Niagara trails are a physical pool of
    `RIBBON_NIAGARA_POOL_TRAILS` (start 16) components created once and reserved, no per-frame allocation. **Changed in
    51e step 7 (user's cost order, 2026-11-22):** a component is active only while it draws a trail: resumed when a
    trail is given to it (on that resume frame the strip pool draws the trail and the component gets no points, so
    a first-tick delay cannot pop), point count zeroed when unused, parked (paused and hidden, never deactivated: a
    deactivated-then-reactivated system drew nothing; `ParkNiagaraComponent`) after `RIBBON_NIAGARA_IDLE_S` (1 s,
    history clock) unused so a trail at the threshold does not churn; `sol.ThrusterRibbons 0` parks the whole
    set. Without the Niagara system (a failed load) the strip pool draws every flame, the player's included. Only NPC ribbons inside the view frustum reach the split (section 4.10). The player's two trails always take 2, so
    14 are free for NPC ribbons (one nozzle, one trail each). When more NPC ribbons pass the threshold than there are
    free components, the longest on screen (highest px, ties by lower NPC index) get Niagara and the overflow is drawn
    by the strip pool (`SplitRibbonRenderers`). Niagara needs a pool size at all because every trail costs a system
    instance tick and three array pushes per frame: at the Earth spawn view about 700 of 1,300 hulls burn (51c status,
    combat on; 1,450 with combat off), so Niagara for every ribbon is far over the 0.05 ms gate.
  - **Parity:** the two renderers must look the same: the same 1 px device width floor (R1), colour, the `4 V` to `V`
    fade (R2), age alpha and width curves (`RibbonAlpha`, `RibbonWidthM`), pixel profile (`SOLTH_TrailPixel`) and
    emissive (`TRAIL_EMISSIVE`). Niagara's ribbon tessellation is off so its straight segments match the pool's.
  - **No state per set:** set membership is recomputed every frame as a pure draw choice (`SplitRibbonRenderers`) from
    **one shared history store** keyed by ship (a reserved side pool in the renderer, never a Mass fragment, so it
    survives tier switches) and **one shared point builder** (`BuildRibbonPoints`). A ship that changes sets carries
    no state and must not pop. **No hysteresis** (the user did not ask for one; a possible follow-up only if a ribbon
    crossing the threshold is seen to flicker).
  - **Verification:** developer cvar `sol.ThrusterRibbonNiagaraPx` overrides the Niagara threshold in px (default -1:
    the preset's `RibbonNiagaraMinPx`; a huge value sends every NPC ribbon to the pool). A side-by-side screenshot pair
    of a frozen game, one ship's ribbon drawn by Niagara and then by the pool, compared by a numeric crop diff, shows
    the switch has no visible pop.
  - D1's "Niagara draws the ribbons" now holds for the player and the large on-screen ribbons only.
  - **R7-W pool widths (resolved 2026-11-21, an implementation detail required by the parity rule):** the strip pool
    draws the same age-based `RibbonWidthM` as the Niagara trail. Texels 26-31 of each trail hold the unfloored widths
    of points 1..24, four per texel (point 1 in texel 26.x; 0 past the trail's points); the head's width is the
    header's `r_n` (contract case 18). A rank-based width was rejected: it is off by about 4 % while the ship burns and
    far off on a fading ribbon (newest sample at 0.9 s: 1.04 `r_n` by rank against 1.9 `r_n` by age).
- **R7-C ribbon colour (start value, tuned with the user in 51f; not a user decision):** each trail is drawn in the
  plume ramp's rim colour at the ship's current colour scalar c (`PlumeColour(PlumeColourScalar(G_main, War
  power)).Rim`), the same value in both renderers (the store holds one colour per trail). While the plume is off the
  trail keeps the colour it had when it last burned, so a ribbon does not change colour when the thrust cuts. [R10:
  the trail carries the colour scalar `c` (not a colour), and the material takes the rim and core from the ramp; `c`
  is held while the flame shuts down.]
- **R8 Clock (accepted by the user, 2026-11-21):** history samples use the world's game time (`UWorld::GetTimeSeconds`:
  stops with the pause menu, not scaled by the simulation time warp), so a ribbon is always 1 s of the ship's
  on-screen motion.
- **R9 A/B and gate:** developer cvar `sol.ThrusterRibbons` (0 hides every ribbon, skips the work in both renderers and
  restores the pre-51e Collect thrust gate, so the A/B measures the whole cost). Gates: 0.05 ms CPU for the typical set
  (the Epic spawn view), measured as the whole-frame delta so the Niagara instance ticks count; a stress profile at
  about 700 burning hulls, reported; GPU at most 0.20 ms at 1920x1080. Cinematic has the same `V` as Epic, so its
  cost is the Epic cost.

### 11.2 R10 one-piece flame (2026-11-22, after the user reviewed GIFs of the 51a-51e build)

**What the user said (verbatim):** "You combined and now have the cone AND the strips. There should only be one fire.
When I said to combine them, I mean the flame starts as the cone, and on the cone's ends you have the streaks. Right
now you have a cone that does not move or bend and a strip that bends and they both come out from the same position.
The cone should be the starting of the flame, and the ribbon should be on the end of the flames, or the cone itself
needs to bend after a short distance. In reality, we want it to be realistic. So if in reality the flame that is long
will not bend, then we dont want it to bend either. But if it will, then we also want it to bend. So let's make it
realistic. Also the cone should always be wider at the beginning." Asked which option gives no visible seam, the user
said: "Which option would give us no visible seam between the tail and cone? Let's do that one, and make sure with
visual checks that there is no visible seam." **Cone shape (user):** widest at the nozzle, tapering to a point of about
0.1 x the nozzle radius, the bright core following the same taper. **Physics given to the user:** exhaust leaves at
km/s, so within the first tens of metres it cannot visibly bend; gas already emitted keeps flying straight in the
inertial frame while the ship turns, so farther out the flame curves behind the ship like a swung hose.
**Attribution:** the one-piece flame was selected by the main session under the user's instruction "Let's do that one"
(the only option with no seam by construction: there is no joint to see), recorded as the user's choice in that sense,
as R7 records its delegated pick. The rejected alternatives were a rigid cone with a ribbon starting at its tip, and a
cone mesh that bends.

**Decided (R10-1 to R10-9).** Start values are marked; they are tuned in 51f.

- **R10-1 One piece.** Each main nozzle draws one flame: a single continuous mesh along a spine from the nozzle exit to
  the tail, with one material, one width profile and one brightness and colour curve along the arc. There is no cone
  mesh, so no joint exists to show a seam. Arc coordinate `s` (metres from the nozzle along the flame), flame length
  `L_f = FLAME_LENGTH_FACTOR x L_p` (3, the old ribbon factor; R10-Q1), `L_p` from 4.3 unchanged.
- **R10-2 Width (full width, the diameter).** `W(s) = 2 r_n [1 - (1 - rho) S(s / L_p)]` with `S` the smoothstep
  `3u^2 - 2u^3` on `u` clamped to [0, 1] and `rho = FLAME_TAIL_WIDTH_RATIO` 0.1 (user); past `L_p` the tail keeps
  `2 rho r_n` (*start value*) to `L_f`. `W` never increases along the arc (widest at the nozzle, user). The smoothstep
  equals a straight cone at mid-length (0.55 of the nozzle width) and has zero slope at `L_p`, so the silhouette has no
  kink at the plume-length mark. Replaces the cone's 0.35 tail (Q11) and the ribbon's `r_n` to `2 r_n` widening (Q24).
  The drawn width is floored at 1 device px per point (R1): on a 1.5 m nozzle the 0.3 m tail reaches the floor at
  about 290 m, the 3 m base at about 2.9 km (1080p, 90 degree FOV); on the player's 0.8 m nozzle 150 m and 1.5 km.
- **R10-3 Brightness, colour and patterns.** Alpha `b(parcel) x (1 - s / L_f) x pixel fade` (a parcel carries the `b`
  of its emission time). Colour from the trail's one colour scalar `c` (4.3 ramp: rim outside, core on the centre
  line); the core weight is taken from the radial fraction of the local half-width `W(s) / 2`, so the core tapers with
  the flame. The shock diamonds keep the 4.4 formulas (spacing `2 r_n (1 + 0.6 b)`, strength `0.82^(k-1)`, knots in the
  first 80% of `L_p`, rhombus half-length 0.35 S, half-width 0.6 of the local half-width) and the panning streak noise
  runs along `s` over the whole flame, its depth rising by a smoothstep from the body's `STREAK_STRENGTH` (0.45) at
  0.5 `L_p` to `FLAME_STREAK_TAIL_STRENGTH` (*start value* 0.8) at 1.5 `L_p`, so the tail reads as streaks
  (`FlameCoreWeight`, `FlameKnotIntensity`, `FlameStreakDepth`, contract case 23). **Seam rule:** every along-arc term of the material is a continuous function of
  `s` with a stated slope bound (contract case 23); nothing branches at `L_p`; a term that changes between the body and
  the tail (for example a stronger streak contrast in the tail, the "streaks on the cone's end") changes through a
  smoothstep, never a step.
- **R10-4 Gas parcels (the physical spine).** A parcel emitted at time `t_e` leaves with the nozzle's velocity
  relative to the ship's centre plus `v_e` along the exhaust direction at `t_e`, and then flies straight in the
  non-rotating ship-relative axes of Q4 (ship translation drops out; the ship's own acceleration acting on the gas,
  under 0.1 m over a flame's life, is ignored). The point at arc coordinate `s` is the parcel of age `tau = s / v_e`:
  `P(s) = ship now + offset(t_e) + (v_e exhaust(t_e) + v_nozzle(t_e)) tau`. The pose at `t_e` is interpolated from the
  pose history (offset lerped, exhaust direction normalised-lerped, `v_nozzle` the finite difference of the two
  bracketing poses; between the newest stored pose and now, the live head pose is the newer end). `FLAME_POINTS` (25,
  the old `RIBBON_POINTS_PER_TRAIL`) spine points at `s_j = j L_f / 24`. A point older than the oldest stored pose has
  not been emitted yet: it takes that pose and alpha 0, so a flame grows in at `v_e` on ignition (15-45 ms). `s` is the
  parcel's travel distance `v_e tau`, not the measured curve length (they differ by under 0.4 % at the worst bend of
  the table below).
- **R10-5 Rigidity is automatic.** For a steady turn at `omega` about the ship's centre with the nozzle `a` behind it,
  the parcel model gives the lateral offset from the current nozzle axis `delta(s) = (a + s) sin(phi) - a phi
  cos(phi)`, `phi = omega s / v_e`, about `omega s^2 / v_e`: it grows with the square of the distance, so the first
  metres cannot bend visibly and the far end swings like a hose. The nozzle's own swing (`v_nozzle`) cancels the
  nozzle-offset term to first order; without it the flame would leave the nozzle at an angle.
- **R10-6 Visual exhaust speed `v_e` = `FLAME_EXHAUST_SPEED_MPS` 2000 m/s (provisional, *start value*; R10-Q1).** At
  127 degree/s (2.2166 rad/s):

  | Flame | `a` | `L_p` | `L_f` | Life `L_f / v_e` | `delta` at `L_p` | `delta` at `L_f` | Bend at `L_f` seen from the nozzle |
  |---|---|---|---|---|---|---|---|
  | NPC 30 m, 1.65 G | 15 m | 9.659 m | 28.977 m | 14.5 ms | 0.10 m | 0.93 m | 1.8 degree |
  | NPC 30 m, `G_REF` | 15 m | 30 m | 90 m | 45 ms | 1.00 m | 8.97 m | 5.7 degree |
  | Player 20 m, 3 G | 8 m | 10.388 m | 31.165 m | 15.6 ms | 0.12 m | 1.08 m | 2.0 degree |
  | Player 20 m, War power | 8 m | 20 m | 60 m | 30 ms | 0.44 m | 3.99 m | 3.8 degree |

  **Why 2 km/s:** real chemical exhaust leaves at about 2.5-4.5 km/s. 2 km/s keeps the taper part (`s <= L_p`) of
  every flame within about one nozzle radius (1 m) of its axis at 127 degree/s, while a full-length (`G_REF`) flame's
  tail visibly swings, 9 m over 90 m. That is "visible but modest". At 1 km/s the same tail swings 17.9 m (11.4 degree)
  and the taper end 2.0 m; at 3 km/s 6.0 m (3.8 degree) and 0.67 m. The as-built ribbon (drift `3 L_p / 1 s`, no nozzle
  velocity) puts the plume-length point 30.3 m (`G_REF`) or 16.6 m (1.65 G) off the axis, a 42 degree bend: the strip
  the user saw bending beside a rigid cone.
- **R10-7 Pixel rules.** As the ribbon today: drawn width floored at `RIBBON_MIN_WIDTH_PX` (1 device px) per point
  (R1, `RibbonDrawnWidthM` on the CPU, `SOLTH_RibbonVertex` in the pool); the whole flame faded by its on-screen
  length, `RibbonPixelFade(PixelLength(L_f, d, f), V)` with `V` by quality (R2, R3); no distance or count cap (R4); the
  player at every preset, fade 1 (R5). The cone's 0.5-1 px fade is gone (R10-Q3).
- **R10-8 History.** Poses (emission time, nozzle offset and exhaust direction in non-rotating world axes, `b`), pushed
  at most every `FLAME_SAMPLE_INTERVAL_S` (1/120 s, so every frame below 120 fps) into a ring of `FLAME_HISTORY_SAMPLES`
  (8, at least `L_f / v_e` = 45 ms plus one interval for a 30 m hull at `G_REF`), on the game-time clock (R8). Only the
  last `L_f / v_e` is ever read. The store keyed by ship, the frustum cull and the in-place feeds of 4.10 stay; the
  ring shrinks from 24 to 8 samples and the push rate rises from every 41.7 ms to every frame. The colour scalar `c`
  and `L_p` are held at their last burning values while a flame shuts down (R10-Q4).
- **R10-9 What stays and what goes.** Stays: the thrust plumbing (4.2), the plume scalars and ramp (4.3), the knot
  formulas (4.4), the nozzle glow as a hull section with its own pixel rule (R10-Q7), the gas bursts (4.6), the hull
  light (4.7), the nozzle table (4.9), the per-instance custom data for the glow and bursts, R1-R5 and R8, the shared
  store, the frustum cull and the feed path. Goes: the cone mesh sections, `M_SOLThrusterPlume`, the cone fade, the
  rigid-cone rule (D6), the cone-only fallback for ships without a ribbon (D7), the ribbon's age alpha, age widening and
  drift (Q24). The R10 questions are resolved (11.3).

**Impact on what is built (51b-51d committed, 51e uncommitted).** "Rework" means kept and changed in 51e3.

| Item | Built in | Fate under R10 |
|---|---|---|
| `SM_SOLNpcHullCone`, `SM_SOLNpcHullBox`: slot `Plume` (16-sided cone per nozzle) | 51b | Removed (`create_thruster_assets.py` stops calling `plume.cone`, meshes rebuilt); slots `Hull`, `Glow`, `Burst` stay |
| `SM_SOLPlayerThrusters`: two plume cones | 51b, 51d | Removed; glows and bursts stay |
| `M_SOLThrusterPlume`, `SOLTH_PlumeVertex` | 51b | Retired (asset, `Paths::THRUSTER_PLUME_MATERIAL` and its `MaterialAssets` check deleted). The look of `SOLTH_PlumePixel` (ramp, core, knots, streaks) moves into a new `SOLTH_FlamePixel` driven by `s`; the ramp, brightness and knot mirrors stay |
| `M_SOLThrusterRibbon` (pool) | 51e | Rework: becomes the flame material (`SOLTH_FlamePixel`; trail header `c`, `b`, `L_p`, `r_n`; tube rings, R10-Q2) |
| `M_SOLThrusterTrail`, `NS_SOLThrusterTrails`, the 16-component Niagara set, `SplitRibbonRenderers`, `RibbonNiagaraMinPx`, `-SOLSmokeRibbonParity` | 51b, 51e | Kept and reworked (user chose the hybrid, R10-Q6) (flame pixel, per-trail `c`, `b`, `L_p`, `r_n` user parameters, ribbon Shape `Tube` / `TubeSubdivisions`, which `ENiagaraRibbonShapeMode` in `NiagaraRibbonRendererProperties.h` offers, or `Plane`), parity and no-pop re-run |
| `M_SOLThrusterGlow`, `M_SOLThrusterBurst`, `T_SOLThrusterNoise`, `T_SOLThrusterFloatDefault` | 51b, 51e | Unchanged |
| `SOLThruster.ush` | 51b, 51e | Plume vertex and pixel removed; `SOLTH_FlamePixel` added; `SOLTH_RibbonVertex` reads the head width as `2 r_n` from the header (and builds rings for a tube) |
| Per-instance custom data (6 floats) | 51b, 51c | Layout kept; float 2 (true `L_p`) is read by no section after R10 (compaction left to 51f, no re-layout now) |
| `PlumeInstance`, `PlumeCustomData`, `PlayerPlume` | 51c, 51d | Rework: the zero gate moves from the cone's 0.5 px of `L_p` to the glow's own fade; layout unchanged. `NpcThrust.PlumeInstance` and `PlayerThruster.Plume` re-validated |
| `NpcThrustGateM` (case 19) | 51e | Rework: the cone term becomes the glow and gas-burst terms (7.2 km and 9.6 km for a 30 m hull); "ribbons off" now means no flame at all (glow and bursts only) |
| `ASOLNpcVisuals` feeds, pawn `FeedPlayer` | 51e | Rework: the payload is a pose (no drift) plus held `L_p` and `c`; the set of fed ships is today's ribbon set (same pixel length `3 L_p`), so slot counts do not grow unless R10-Q3 b is chosen; pushes become per frame into an 8-sample ring |
| `ASOLThrusterRibbons` store, `BuildRibbonPoints`, `PackRibbonTexels`, `RibbonTrailReleasable`, `RibbonNominalLengthM` | 51e | Rework (`RibbonNominalLengthM` deleted, contract case 24): `BuildFlamePoints` replaces `BuildRibbonPoints`; texel 25 becomes (`c`, `b`, `L_p`, `r_n`); a slot is releasable once its newest burning pose is older than `L_f / v_e` |
| `USOLThrusterRibbonComponent` | 51e | Rework: header layout; ring block mesh (tube, R10-Q2) |
| Cvars `sol.ThrusterRibbons`, `sol.NpcPlumes`, `sol.PlayerPlume` | 51c-51e | `sol.ThrusterRibbons` hides the whole flame (no cone fallback; renaming to `sol.ThrusterFlames` is optional); the other two unchanged |
| `FSOLThrusterSmoke`, `-SOLSmokePlayerThruster` | 51c-51e | Rework: the plume-section checks become glow checks; flame seam crops added (51e3) |
| Tests | 51a-51e | Contract: cases 1-6, 8-10, 13-15 kept; 7, 11, 12, 18, 19 rewritten or extended; 16 kept or deleted with R10-Q6; 17 deleted (replaced by 21); 20-24 new (24 = the R10 part of `RibbonRendererMath`) |
| Images `51-npc-thruster-plume.png`, `51-player-thruster-boost.png`, `51-player-thruster-chase.png`, `51-thruster-ribbon-curved.png`, `51-thruster-ribbon-npc.png` | 51c-51e | Show the rejected look: replaced in 51e4 / 51f |
| `GAME_MECHANICS.md` thruster rows, Info line, `ARCHITECTURE.md` | 51c-51e | Cone wording updated in 51e3 (done for the first and the last, see the as-built note below) |

**Cheapest path (rejected by the user, R10-Q6):** the GPU strip pool alone draws every flame, the player's included, and Niagara is retired for
thrusters. The flame look is then written once, with no parity spike or switch no-pop pair to redo for it. The 16
Niagara components and their array pushes go too: the measured lane tick is 0.051 ms with one Niagara NPC trail and
0.027 ms with the same trail in the pool. The user insisted on Niagara for near ships and a pool beyond (R7), so this
was put to the user (R10-Q6), who chose to keep the hybrid instead, accepting the Niagara cost per flame (measured and reported).

**As built in 51e3 (2026-11-22).** No decision above changed; this records what was built.

- **Flame.** One tube per main nozzle (`FLAME_TUBE_SIDES` 16, round cross-section; 8 read as an octagon in the chase view, facets 7.6 % of the radius, 16 give 1.9 %) along 25 gas parcels emitted at 2 km/s from a ring
  of 8 nozzle poses (pushed at most every 1/120 s of game time): 2 `r_n` wide at the nozzle, a smoothstep taper to
  0.1 of that at `L_p`, a thin tail to 3 `L_p`; brightness `1 - s / L_f` along the arc; diamonds in the first 80% of
  `L_p` and streaks deepening toward the tail are material patterns by arc length; colour from the 4.3 scalar `c`
  (rim outside, white core on the spine). Shutdown is a 0.25 s spool-down (`b` falls to 0, `L_p` and `c` held). Width
  floor 1 device px; the whole flame fades by its on-screen length from `4 V` to `V` (the player's never); no distance
  or count rule. The nozzle glow stays a mesh section (1 px minimum), the gas bursts and the player hull light are
  unchanged.
- **Renderers (R10-Q6, hybrid kept).** `ASOLThrusterRibbons` keeps the shared pose store. Niagara (16 `NS_SOLThrusterTrails`
  components, Shape Tube 16, one flame each) draws the player's flames and NPC flames at least
  `RIBBON_NIAGARA_PX_FACTOR` x `V` long on screen; the GPU strip pool (`M_SOLThrusterRibbon`, one instanced draw of an
  16-sided tube block mesh (17 vertices per ring), 25 texels (x, y, z, alpha) per flame plus a header (c, b, `L_p`, `r_n`) and widths) draws the
  rest. Both shade with `SOLTH_FlamePixel` (`Shaders/Private/SOLThruster.ush`). Niagara per point carries
  (`s / L_f`, alpha, thin-flame sharpness, 0), widths floored on the CPU, and the user Vector4 `TrailFlame` = (c, b,
  `L_p`, `r_n`) as dynamic material parameter 0.
- **Niagara findings.** The ribbon renderer's Shape Tube keeps the default camera-facing normals (`bUseGeometryNormals`
  False): geometry normals drew nothing in 5.8. The flame pixel's radial fraction (`SOLTH_FlameRadialFraction`) is |cos(2 pi V)| from the interpolated around
  coordinate V (`UV0.y = k / FLAME_TUBE_SIDES`), identical in both renderers, with the near-axial blend to the taper kept. Idle components are PAUSED and hidden, never deactivated: a system that was deactivated and then
  reactivated drew nothing.
- **Thin-flame sharpness.** Where the drawn width is the floor or a few px the radial fraction blends to 0.5
  (`FLAME_SHARP_LOW_PX` 1, `FLAME_SHARP_HIGH_PX` 4), so a hair-thin flame stays a bright line instead of a dim rim.
- **Feeding.** Only ships inside last frame's view frustum are fed (`IsOnScreen`), so a ship turning into view grows
  its flame in over 1-3 frames; off screen its flame is released once its gas has left (`L_f / v_e`). The Collect
  thrust gate `NpcThrustGateM` is the largest of the glow, gas-burst and (while flames are on) flame reach;
  `sol.ThrusterRibbons 0` hides the flames, the glow and bursts stay. `PlumeInstance`'s zero gate is the glow's 0.5 px
  fade (new nozzle-radius parameter).
- **Seam verification.** `-SOLSmokeFlameSeam` (with `-SOLSmokeRibbonParity`) shoots 92 configurations (83 at first; v135 front-quarter cap views and the player's burn states at v170 added in 51e3) (`-SOLSmokeFlameSeamAAOff` turns anti-aliasing off, `-SOLSmokeFlameSeamOnly=<text>` filters by name): NPC and player
  flames at the pixel-floor distances, side, oblique, chase and front views, burn states idle, 1 G, 3 G, boost and War
  power, and a 127 degree/s turn, each as a Niagara and a pool shot, then zoomed crops are inspected for a visible
  seam. Tests: `SOLTest.Thruster.*` (28, with `FlameProfile`, `FlameParcels`, `FlameRigidity`, `FlameSeam`,
  `FlameShutdown`, `FlameMirror`, `FlameSharpness` (the CPU `SOLThruster::FlameSharpness`, parity with `SOLThrusterMirror::FlameSharpness`), `FlameOnScreen` (`ASOLThrusterRibbons::SphereTouchesFrustum`, the pure test behind `IsOnScreen`)), `NpcThrust`, `PlayerThruster`.

- **Niagara / pool parity finding (fixed).** With anti-aliasing off the flame-masked difference between the renderers was
  at the same-renderer spread in side views but 1.3-2 % at 45 degrees and 3-12 % in the chase view (Niagara brighter), a
  pop risk when a flame changes renderer. The normal-based radial fraction was the cause; taking it from V fixed it:
  every chase and 45 degree configuration is now at or below the same-renderer spread. The pool material lost its
  normal output and one interpolator (`M_SOLThrusterRibbon` vertex shader 1028 -> 684 instructions, pixel 254;
  `M_SOLThrusterTrail` VS 153, PS 230). Measured again in the 51e review on the current assets (end caps and neck fix
  in): `M_SOLThrusterRibbon` VS 700 / PS 277, `M_SOLThrusterTrail` VS 153 / PS 250.
- **Real-feeder smokes (passed).** `-SOLSmokeThruster -SOLSmokeThrusterRibbons` 3/3, `-SOLSmokeThruster` 5/5,
  `-SOLSmokePlayerThruster` 25/25. No seam, kink, width or brightness step along the flame in any view, state, turn or
  distance tried (NPC 30 m-10 km, player 30 m-2 km).
- **Hollow tube: RESOLVED by the user (2026-11-22), end caps** (not an along-arc fade). The tube was open at both
  ends, so in near-axial views (chase, about 10 degrees) the nozzle rim showed as an arc inside the flame (NPC 30-100 m,
  player 30-150 m) and at idle a dark pinhole showed on the axis through both open ends. As built (51e3): both renderers
  draw `FLAME_DRAWN_POINTS` 27 points, the spine with a cap point before the nozzle and one past the tail
  (`SOLThruster::AddFlameCaps`, contract case 25): each cap lies `FlameCapDepthM(r_n)` = max(0.05 `r_n`, 0.02 m) out
  along its end segment at width 0 (alpha and arc coordinate of its end point), so the same tube closes in a flat cone
  in both renderers, with the end ring shared, so no joint exists. The cap segment's ring vertices keep their around
  coordinate V, and a cap fraction (0 on the cap point, 1 on the tube: Niagara particle colour a, pool `Interp2.w`)
  scales the pixel's radial fraction (`SOLTH_FlamePixel`): a cap pixel on ring vertex V's line, the cap fraction of
  the way out, has exactly that share of the tube's screen offset across the spine, so the radial fraction is
  continuous across the rim and the cap reads as the flame's cross-section (core on the axis). A cap point takes its
  end point's true width for the thin-flame sharpness (both renderers) and is drawn at width 0 (no 1 px floor). The
  pool's texels follow (case 18: 27 points, header at 27, widths of drawn points 2..25 in 28..33,
  `RIBBON_TEXELS_PER_TRAIL` 34), `NS_SOLThrusterTrails` spawns 27 particles. The tail cap sits where the along-arc
  alpha is 0, so it draws nothing in practice; it is kept for symmetry and for flames whose gas has not reached the
  tail. End-ring orientation: with the caps the nozzle and tail rings are interior points of the ribbon, so they face
  along their end segment instead of the reflected end tangent (a tilt of half a segment's bend, the same in both
  renderers).
- **Idle axis dot: FIXED (51e3, fifth agent).** At idle in the near-axial view a faint dot about 3-4 px wide and about
  5 % darker sat on the axis, in both renderers and with anti-aliasing off. It was not the cap. A debug split of the
  layers (cap, tube before `L_p`, tube after `L_p` in separate channels) showed the cap smooth through the dot. The tube
  before `L_p` had a clean hole there, and the tube after `L_p` filled it only dimly. Cause: the flame is a hollow,
  two-sided, additive shell, and it is not convex at the neck where the taper ends and the narrow tail (0.1 of the
  nozzle width) begins. Seen down the spine from the nozzle side, a ray that enters the tail's mouth meets the tail's
  far wall a run of w |sin a| cot(theta) further on (w the tail width, a the ring angle, theta the view's angle to the
  spine). The along fade 1 - s / `L_f` is lower there than on the cone wall beside the mouth, so the mouth read as a
  dark dot. The hole moves tailward with `L_p`: at 1 G it sits at the disc edge. The dot showed only at idle, where
  that run is a large share of `L_f` (0.9 m of about 1.5 m) and the flame is not saturated. Fix:
  `SOLTH_FlameNeckAlpha` (`SOLThruster.ush`, called by `SOLTH_FlamePixel`; float mirror
  `SOLThrusterMirror::FlameNeckAlpha`, test `SOLTest.Thruster.FlameNeck`). On the tail, the wall the ray crosses
  farther along s takes the along fade at its other crossing, s - run, never before `L_p`. Seen from the nozzle side
  that is the far half (sin(2 pi V) < 0), seen from the tail side the near half. The same dot showed from the tail
  side, in the new `player-<state>-30m-v170` seam configurations. The alpha is unchanged on the other half, in the
  taper and side-on. The run is 0 at each of those boundaries, so the blend is continuous and only ever brightens. This is a shader body change with no new material
  argument, so no asset rebuild. With anti-aliasing off at idle, in both renderers: from the nozzle side the minimum
  around the dot went from 173 (median 199-200) to 189-192 (median 201), inside the cap's streak spread of 189-206.
  From the tail side the minimum in the disc core went from 171-179 to 180-183 (median 199-200), and the dot no
  longer shows at 4x. From the tail side at War power (NPC 30 m), a narrow waist the tail had just before the disc
  is also gone. The test was written after the fix, as a bug-fix test, and has no contract case. The GPU
  figures in 11.3 and the pixel-shader instruction counts predate this pixel code and were not re-measured. The bright column
  beside the old dot is the tail's two extra walls seen behind the cone, the same layer-count effect. It existed before
  the fix and is unchanged: about 210 against 200, only at idle near the axis. The nozzle-side view at 1 G, 3 G, boost,
  War power and the turn is unchanged by eye in both renderers. Removing the layer-count effect entirely would change
  the look or the contract, so those are the user's call: face culling with a brightness rebalance, a taper that keeps
  the shell convex, or a different tail width ratio.

### 11.3 R10 questions (all RESOLVED by the user, 2026-11-22)

No open questions remain for R10. Items the implementer must report back are listed at the end of this section.

- **R10-Q1 Bend realism: RESOLVED, realistic.** Ballistic at `v_e` = `FLAME_EXHAUST_SPEED_MPS` 2 km/s (*provisional start
  value*, tuned in 51f), a small bend, the tail fading along the flame over its 15-45 ms life (the same as an age fade);
  flame length 3 `L_p`. The 1 s age no longer draws anything (supersedes D7's 3x-plume-at-1-s and Q24's 1 s age).
- **R10-Q2 Cross-section: RESOLVED, tube (round flame), not a flat strip.** `FLAME_TUBE_SIDES` 16 sides swept along the spine; the core
  comes from the screen-radial fraction as `SOLTH_ConeRadialFraction` does. Applies to both renderers (Q6).
- **R10-Q3 Fade rule at Low / Medium / High: RESOLVED, yes.** The ribbon's pixel fade rule stands for the one-piece
  flame; quality scales the vanish size: Low 10 px, Medium 4.6, High 2.2, Epic / Cinematic 1 px.
- **R10-Q4 Shutdown: RESOLVED, spool-down.** `FLAME_SHUTDOWN_S` 0.25 s: `b` falls linearly to 0, `L_p` and `c` held,
  then the last gas leaves at `v_e`.
- **R10-Q5 Spine source: RESOLVED as the implementer's call (technical, delegated).** Recommendation kept: the pose
  history ring of R10-8 (exact for any manoeuvre, keeps the tested store); the analytic constant-turn spine is the
  alternative (cheaper feed, but needs a per-ship angular rate and misses turn start and end). The implementer chooses
  and reports the choice with reasons and measurements.
- **R10-Q6 Renderer: RESOLVED, keep the Niagara hybrid (user override of the recommended pool-only).** Niagara draws
  large on-screen flames, chosen by pixel size per the pixel-LOD rule (the pixel threshold, retuned in 51f); the GPU
  strip pool draws the rest. The two renderers must match with no seam and no pop at the switch; the tube
  cross-section and the one-piece flame (no separate cone mesh) apply to both. Downside recorded: Niagara cost per
  flame; it is measured and reported, and the implementer does not relax any gate. The hybrid reworks of 11.2 apply
  (flame pixel, per-trail `c`, `b`, `L_p`, `r_n`, ribbon Shape `Tube`), parity and no-pop re-run; contract case 16 is
  kept.
- **R10-Q7 Nozzle glow: RESOLVED, keep as a separate effect, 1 px minimum.**
- **R10-Q8 Pending 51e decisions: RESOLVED.** (1) Feed only ships on screen (last frame's frustum); a ship turning
  into view grows its flame in over 1-3 frames. (2) The earlier "keep Niagara factor 8" is superseded: the hybrid
  pixel threshold (`RIBBON_NIAGARA_PX_FACTOR`) is retuned in 51f.

**Verification demanded by the user:** seam-free results, checked by zoomed crops at several distances and view
angles (plan 51e4 and the seam checks in 51e3).

**Implementer reports back:** the Q5 choice and reasons; Niagara cost per flame and the hybrid's total cost against
the gates (no relaxation); the tuned `v_e`, the hybrid pixel threshold and the factor 3 (51f).

**As reported (51e3, 2026-11-22; details in the plan's 51e3 status):**
- **Q5:** the pose history ring. It is exact for any manoeuvre (turn start and end, rolls, the player's free
  rotation), where a constant-turn spine needs a per-ship angular rate and is wrong at every change. Contract cases 7
  and 21 pin it. The feed is one 32-byte pose copy per fed (on-screen) ship per frame, and it is inside the store
  timer below (store 0.008-0.011 ms).
- **End caps** (the user's choice over an arc fade) are built as described in 11.2. They add 2 drawn points per
  flame (27).
- **CPU, `sol.ThrusterRibbons` A/B** (1920x1080 Epic, interleaved A B B A, 8 pairs, `ribbonMs` = the actor tick, fast
  machine state): spawn view (about 25 pool flames) median **0.054 ms against the 0.05 ms gate, a miss of about
  1.1x**. Lane view (one Niagara flame) 0.033 ms, which passes. The gate is not relaxed. The user's options for the
  spawn view: pool spines built on the GPU from the uploaded pose ring, or fewer points per pool flame (a contract
  and parity change).
- **GPU:** the #51 set on / off (`sol.NpcPlumes`, 4 pairs) +0.039 ms at the spawn view and +0.037 ms at the lane
  view. Player War power (`sol.PlayerPlume`) +0.007 ms. All are inside the 0.20 ms gate.
- **Tests:** 2078 / 2078 before the idle axis dot fix; with `SOLTest.Thruster.FlameNeck` the full suite is
  2079 / 2079.


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
- 2026-11-21: part 51c: the as-built NPC thrust source in 4.2 (a Full ship's achieved acceleration is its lane rail's
  thrust plus its AI's deviation acceleration) and the renderer path in 4.10 (range-limited custom-data upload,
  `sol.NpcPlumes` A/B cvar); new glue functions `SOLThruster::HullThrustLocalG`, `PlumeInstance`, `PlumeCustomData`,
  `SOLNpcRail::LaneThrustMps2`, `SOLNpcAi::AchievedAccelMps2`, tested in `SOLTest.NpcThrust.*` and
  `SOLTest.NpcRail.LaneThrust`.

- 2026-11-21: part 51d: the player plume as built (section 4.2: the substep thrust is rotated by the attitude it was
  applied with, `SOLThruster::ShipThrustLocalMps2`, and a zero-length step keeps the last value; section 4.7: the light
  is on lighting channel 2, which nothing else uses, the hull parts keep channel 1 for the sun and fill lights and join
  channel 2; start intensity 40 cd; section 4.10: the pawn's `ThrusterMesh` gets custom primitive data only on change).
  New glue `SOLThruster::PlayerPlume`, tested in `SOLTest.PlayerThruster.*`; A/B cvar `sol.PlayerPlume`; measured
  costs in the plan's 51d status.
- 2026-11-21: part 51e: the user's ribbon decisions after the mock-up (section 11.1, R1-R6: 1 px width floor, fade
  between `4 V` and `V`, `V` by quality, no distance or count cap, the player always) and the renderer choice (R7: one
  GPU-driven strip pool instead of Niagara, justified by the uncapped count), the clock (R8) and the A/B cvar (R9).
  Sections 4.5, 4.8, 4.10 and 9 are read with 11.1 taking precedence where they disagree.
- 2026-11-21: part 51e: R7 rewritten as the user's hybrid renderer (Niagara, one instance per trail, for the player
  and for NPC ribbons at least `RibbonNiagaraMinPx` = `RIBBON_NIAGARA_PX_FACTOR` x `V` on screen, within the physical
  `RIBBON_NIAGARA_POOL_TRAILS` pool; the GPU strip pool for every other shown ribbon; one history store and one point
  builder, membership a per-frame pure choice by pixel size, no hysteresis), replacing the previous agent's pure strip
  pool; `NS_SOLThrusterTrails` and `M_SOLThrusterTrail` are used. R8 accepted by the user. R9 gates restated (whole-frame
  CPU delta, about 700 burning hulls reported, GPU 0.20 ms at 1080p; the cvar restores the pre-51e Collect gate).
  Sections 4.8 and 4.10 point at the hybrid. New pure functions in contract cases 16-18. R7-W (the pool's
  per-point widths, packed in texels 26-31 for parity) resolved.

- 51c decision (user): combat-off NPC redraw CPU gate relaxed to 0.12 ms (combat-on stays 0.10 ms); Full lane ship plume = rail thrust plus AI correction.
- 2026-11-21: part 51e: the renderers as built (section 4.10): `ASOLThrusterRibbons` (shared store, feed API, Niagara
  set with `ForceTickLast`, draw in `TG_PostUpdateWork`), the strip pool `USOLThrusterRibbonComponent`, the output
  height in the pool's 1 px floor, and the parity spike numbers.
- 2026-11-22: part 51e step 7: the user's cost fixes (section 4.10 "Cost fixes"): view-frustum cull before the split
  (off-screen ships keep their history), feeds committed in place from the placement tasks, the store split into
  per-frame state and sample rings with a parallel store pass, Niagara set components active only while drawing (R7
  text changed, `RIBBON_NIAGARA_IDLE_S`); the valid no-pop pair; the U-turn ribbon explained; leftover distance and
  count wording (D7, 4.8, Q12, Q29, sections 7 and 9) converted to the pixel rules. Spawn view still +0.34 ms against
  0.05 ms (reported to the user); lane +0.02 ms frame, 0.051 ms tick.
- 2026-11-22: R10 one-piece flame (section 11.2) after the user rejected the cone-plus-ribbon GIFs: one bending flame
  per nozzle (smoothstep taper to 0.1 `r_n` at `L_p`, thin tail to 3 `L_p`), ballistic gas parcels at a visual exhaust
  speed (start 2 km/s), seam rule, ribbon pixel rules; the impact table on 51b-51e; superseded statements marked in D1,
  D5-D7, 4.1, 4.2 (gate), 4.4, 4.5, 4.8, 4.10, 4.11, 4.14, 7, 9, Q1, Q5, Q11, Q15, Q24, R2, R6, R7 and R7-C; R10-Q1 to R10-Q8 resolved by the user (section 11.3). Not user picks but this document's start values: the smoothstep taper, the
  constant tail width, the linear along-arc alpha, the nozzle-velocity term, the 8-pose ring, 2 km/s and the 0.8 tail
  streak depth.
- 2026-11-22: part 51e3: end caps (11.2 "Hollow tube"), the idle axis dot fix (11.2, `SOLTH_FlameNeckAlpha`), the Q5
  choice and the measured CPU and GPU costs (11.3 "As reported").
- 2026-11-22: 51e adversarial review fixes: the strip pool instances only the blocks its live flames need (added at
  once, the trailing ones removed after `RIBBON_BLOCK_SHRINK_FRAMES` 60 frames of lower need; before, the 256-trail
  pre-size kept 8 blocks, 117,504 vertices and 212,992 triangles, drawn whenever one pool flame showed; now 14,688 /
  26,624 for up to 32 flames). Without the Niagara system every flame goes to the pool; the drawn-trail report lists
  only what each renderer drew and never the player as an NPC; `DeactivateNiagaraComponent` renamed
  `ParkNiagaraComponent` (R7: paused and hidden). Dead `SelectRibbonShips` / `FSOLRibbonCandidate`, `PlayerHasRibbon`
  and `PlumeAllowed` removed with their test cases (sections 7 and 9 list them as originally planned; Q27 holds by
  construction, only the hull-drawn pass feeds plumes); new `SOLTest.Thruster.Store.*` tests of the store in a test
  world. The tangent-in-texels idea was not built: it adds CPU fill and upload work while the spawn-view CPU gate is
  already missed, and the pool GPU cost is inside the noise.
- 2026-11-22: wrap-up: sections 4.14, 7 and 9 still name functions and tests removed in the 51e review and R10 (`PlumeAllowed`, `SelectRibbonShips`, `FSOLRibbonCandidate`, `PlayerHasRibbon`); the as-built set is in the code and the R10 sections (TECH_DEBT lists the cleanup). Gas bursts (51g) were not built.
