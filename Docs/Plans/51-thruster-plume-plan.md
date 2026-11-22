<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 51: Placeholder main thruster plume

**Issue:** [#51](https://github.com/DieSlower/SOLTest/issues/51). SDD:
[`SDDs/51-thruster-plume.md`](../SDDs/51-thruster-plume.md). Test contract:
[`51-thruster-math-contract.md`](51-thruster-math-contract.md).
Status: **Q1-Q26 answered by the user 2026-11-20 and Q27-Q33 on 2026-11-21 (SDD section 11). 2026-11-22: the user
rejected the cone-plus-ribbon look; SDD 51 R10 (one-piece flame, section 11.2) is decided, and parts 51e2-51e4 are
unblocked: the user answered R10-Q1 to R10-Q8 on 2026-11-22 (SDD section 11.3). Asset creation is approved (Q20).**
**Persistence: none.** `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay; graphics quality is read from the
existing `QualityPreset`. Any finding that needs a schema change is stopped and reported.
**Scope increase (user, 2026-11-20):** gas bursts for reverse / lateral / vertical thrust on the player and NPCs (Q7,
part 51g) and a player hull light (Q17, in 51d).

## Parts

Each part is built by its own subagent (context under about 35%), test first where it adds or changes pure functions,
then reviewed by a fresh adversarial `opus` subagent (performance and style, with the TECH_DEBT items listed in SDD 51
section 3), then committed and pushed (`Issue #51: Part 51x - ...`), with `Docs/Plans/STATUS.md` and the metrics updated
in the same commit. Temporary screenshots, mock-ups and scratch renders are deleted once used.

Build order: 51a, 51b, 51c, 51d, 51g, 51e-0, 51e, 51e1-51e4 (R10), 51f (51g was added after the first draft, so its
letter is out of order).

### 51-0: answers (done 2026-11-20)

1. Q1-Q26 answered and recorded in SDD section 11 (Resolved decisions); the test contract written to
   `51-thruster-math-contract.md`. Q27-Q33 were answered 2026-11-21.

### 51a: pure math (test first)

1. Test author (`opus`, high, given only the contract file and SDD sections 4.2-4.9): write
   `Source/SOLTest/Tests/ThrusterMathTest.cpp` (`SOLTest.Thruster.*`), contract cases 1-11, 13 and 14 (case 12 waits
   for 51b's mirror).
2. Run `Tools\RunTests.bat -Build -Filter SOLTest.Thruster`; confirm the cases fail for the right reason.
3. Implement `Visuals/SOLThrusterMath.{h,cpp}` (`SOLThruster::`) and the `SOL::Thruster` constants in `SOLConstants.h`
   (`G_REF` = `WAR_POWER_PULSE_G`, the colour keys, history size and age, the quality counts, the nozzle table,
   `PLAYER_HULL_LENGTH_M`).
4. Green run; full suite green.

### 51b: assets (approved, Q20; starts only after 51a is green)

1. Asset script (`opus`, high): new `Tools/Fx/create_thruster_assets.py`: `M_SOLThrusterPlume` (ramp from `c`,
   brightness `b`, knots, streaks, WPO length from per-instance data, core readable without bloom), `M_SOLThrusterGlow`,
   `M_SOLThrusterBurst`, the procedural noise texture, the plume / glow / burst sections on the NPC hull meshes (burst
   sections can be built here from the Q31 port table, otherwise added in 51g by re-running the script), the player's plume meshes and
   `NS_SOLThrusterTrails`. Paths in `SOL::Paths`. No paid content (D10).
2. The material mirror `.ush` and contract case 12 (`MaterialMirror`), test first.
3. Run the script headless; record the compiled instruction counts.

**51b status (2026-11-21): built, not committed.** `Tools/Fx/create_thruster_assets.py` (run with
`-AllowCommandletRendering` so the HLSL compiles; `-SOLRebuild`, `-SOLOnly=`; idempotent, a second run changes nothing)
builds under `/Game/SOL/Fx`: `T_SOLThrusterNoise`, `M_SOLThrusterPlume`, `M_SOLThrusterGlow`, `M_SOLThrusterBurst`,
`M_SOLThrusterTrail`, `SM_SOLNpcHullCone`, `SM_SOLNpcHullBox` (burst sections built here from the Q31 port table, so
51g need not re-run the script), `SM_SOLPlayerThrusters`, `NS_SOLThrusterTrails`. Shader `Shaders/Private/SOLThruster.ush`,
float mirror `Visuals/SOLThrusterMirror.h`, tests `SOLTest.Thruster.MaterialMirror` (case 12) and
`SOLTest.Thruster.MaterialAssets` in `Tests/ThrusterMirrorTest.cpp`. Instruction counts (VS / PS): plume 632 / 165, glow
296 / 134, burst 462 / 118, trail 153 / 96. As-built layout in SDD 51 sections 4.10 and 4.11. Not wired, so nothing is
checked by eye yet.

### 51c: NPC plumes

1. Thrust plumbing (`opus`, high): `ThrustLocalG` per CPU ship (Q2): the rail thrust vector in the Collect pass for Mid
   lane ships (zero through Coast and Flip), the Full AI's achieved acceleration for Full ships, nothing for Mid non-lane
   ships (Q3); evaluated only within `PlumeMaxDistanceM`; no new allocation, nothing saved. Re-validate
   `NpcTrafficMath.ThrustState`, `NpcRail.*` and `Npc.HullFlip.*`.
2. Renderer (`opus`, high): the hull-mesh section path (Q1) in `Npc/SOLNpcVisuals.cpp` (instanced hulls and the mesh
   pool), per-instance `c`, `b` and the plume length scale (true length, Q14) written in the existing parallel placement
   pass; the pixel-size rule for cones and glows (Q5, Q13, Q15); the glow floor 1 px (Q33). Plumes only on hull-drawn ships (Q27): dot-drawn ships get none.
3. Smokes: `-SOLSmokeNpcTraffic`, `-SOLSmokeLaneMerge`, `-SOLSmokeAutoLanes`; screenshots of a lane showing burn, dark
   flip window, turn and brake.

**51c status (2026-11-21): built, not committed.** Thrust plumbing and renderer as in SDD 51 sections 4.2 and 4.10
(as-built notes): `FSOLNpcCpuShip::ThrustLocalG` set in the Collect pass inside `ViewPlumeGateM` (Mid lane ships: rail
thrust; Full ships: rail thrust plus the AI's achieved deviation acceleration, `FSOLNpcAttitudeFragment::
AchievedAccelMps2` from the Full AI Scatter; Mid non-lane: none), thruster hull meshes on both hull layers with the
per-instance plume floats and a range-limited upload, `sol.NpcPlumes` A/B cvar. Tests first: `SOLTest.NpcThrust.*`
(new `Tests/NpcThrustTest.cpp`) and `SOLTest.NpcRail.LaneThrust`; re-validated `NpcRail.*`, `Npc.HullFlip.*`,
`NpcTrafficMath.ThrustState`, `Thruster.*`, `NpcAiMath.*` (98 green). Docs: GAME_MECHANICS row "NPC thruster plume",
one Info line on the Demo paragraph, ARCHITECTURE. Smoke `-SOLSmokeThruster` (`Npc/SOLThrusterSmoke.{h,cpp}`) 5/5 at 1M
(burning ship b 0.489, L 9.56 m at 1.63 G, coasting dark, no plume on 5,490 dot-drawn ship-frames, the cvar round trip,
175k hull / pool ship-frames equal to `PlumeInstance`); `-SOLSmokeNpcTraffic` 19/19 with
`-SOLSimDate=2026-11-12T12:00:00Z` (undated it fails 2 V5 checks with `sol.NpcPlumes 0` too: date-dependent, not #51).
At default radii every gated lane ship is Full (the gate, about 48 km at the speed-widened FOV, lies inside the 50 km
Full radius), so the Mid branch is not reached by the smoke. Screenshot `Docs/Images/51-npc-thruster-plume.png`.
Profile `-SOLSmokeThrusterProfile` (A B B A x 4, 3 runs each, A minus B): NPC redraw +0.051-0.053 ms with default
combat (about 700 lit of 1,300 hulls), +0.089-0.116 ms with combat off (1,450 lit of 2,000; at the 0.10 ms gate, mostly
the placement ParallelFor's game-thread share: 2,000 hulls make only 2 tasks of 1,024); Collect +0.03-0.05 ms; GPU frame
mean at most +0.03 ms (spawn) and +0.05 ms (lane view, only 1 hull: not a busy lane) against the 0.20 ms gate. Not yet
done: the adversarial review, the busy-lane GPU view and the lane screenshots of burn / flip / turn / brake (lanes are
sparse near the viewpoint), the gate decision on the combat-off case.

### 51d: player plume and hull light

1. `ThrustLocalG` on `FSOLShipStateFragment` from the flight processor: substep time-weighted mean (Q8), War power
   colour override (Q10).
2. Two nozzle plumes on the pawn per the nozzle table; the engine-glow spheres replaced by the thrust-driven glow (Q19).
3. The player hull light (Q17): one unshadowed movable point light, intensity from `b`, colour from `c`, own lighting
   channel; on in VR, off at Low, on otherwise (Q28), on its own lighting channel.
4. Re-validate the flight and War power tests; screenshots at idle, 1 G, 3 G, boost (clamped, Q6) and War power.

**51d status (2026-11-21): built, not committed.** Tests first (`Tests/PlayerThrusterTest.cpp`, `SOLTest.PlayerThruster.*`,
6 cases, seen failing by value against stubs, then green): `SOLThruster::ShipThrustLocalMps2` (the step's world thrust
in the ship frame), `SOLThruster::PlayerPlume` (plume floats of the 20 m hull plus the hull light), the transient
`FSOLShipStateFragment::ThrustLocalG` (not reflected), the light constants and the pawn's `PlumeLight` component
(channel 2 only, unshadowed, candelas, hidden at rest) with `SOLShipSunlight::AddToPlayerHullLightChannel`. The flight
processor adds each substep's thrust (rotated by the attitude it thrust with) to an `FSOLThrustMean` and writes the mean
(`USOLShipSubsystem::GetThrustLocalG`); the pawn drops the glow spheres, adds `ThrusterMesh` (`SM_SOLPlayerThrusters`)
and updates mesh and light in `FollowShip` (`UpdateThrusterPlume`); `sol.PlayerPlume` A/B cvar. Hull light start value
40 cd (`PLAYER_LIGHT_MAX_INTENSITY_CD`): it lights the aft faces clearly from the chase camera, the sides only grazingly.
Smoke `-SOLSmokePlayerThruster` (`Ship/SOLPlayerThrusterSmoke.{h,cpp}`) 25/25 at 1280x720 and 1920x1080 (idle b 0;
1.000 G c 0, b 0.3834, L 4.314 m; 3.000 G c 0.35, b 0.664, L 10.388 m; boost 9 G clamped c 1, b 1, L 20 m; Low preset
light off, VR at Low light on; cvar off hides both; War power pulse 6.803 G c 1, recovery 1.50 G c 1, b 0.469, L
5.95 m). The light colour reads back about 1/255 off the ramp (the engine stores it as an 8-bit sRGB `FColor`).
Screenshots `Docs/Images/51-player-thruster-boost.png` (side view, red-orange) and `51-player-thruster-chase.png` (chase
camera, hull light on). Profile `-SOLSmokePlayerThrusterProfile` under a War power order (A B B A x 2, 300 frames per
window, A minus B of the window means), 1920x1080, 3 runs: GPU frame +0.033, +0.055, +0.071 ms (gate 0.20 ms, bursts
not yet built); game thread -0.01 to +0.03 ms (noise). VR one-eye 3840x3744 (record only): GPU frame about 23.4 ms, A
minus B -0.11 ms, inside the window-to-window drift (about 0.4 ms), so not resolvable. Re-validated: `ShipSunlight.*`,
`WarPowerMove.*`, flight and `Thruster.*` in the full suite. Not yet done: the adversarial review.

### 51g: gas bursts (scope increase, Q7; Q27-Q32 resolved)

1. Burst ports: the fixed per-hull port table in `SOLConstants.h` (Q31: bow pair plus 4 side ports, no stern ports); burst sections on the NPC hull meshes and the player meshes (re-run
   `create_thruster_assets.py` if 51b did not build them).
2. NPC: the three burst floats per instance from `ThrustLocalG` (`ThrustAxes`, `BurstStrength`) in the existing placement pass, only for ships with a
   drawn hull (Q27, Q30), the acceleration smoothed over about 0.25 s and evaluated in the existing Collect pass (no
   extra pass), its cost profiled here. Player: the same from its `ThrustLocalG`.
3. Burst look tuned to Q32 (about hull length / 6 long, opacity scaling with G and clamped at 1.5 G, flicker), visually distinct from the main flame.
4. Screenshots: player reverse, strafe and vertical thrust; an NPC U-turn (side bursts); a braking NPC (no burst).

### 51e-0: ribbon pixel-size mock-up (done, user tuning step, D7)

Done. Ribbons were rendered at 50, 20 and 10 km (and close up) at 1920x1080, scratchpad only, purely to show the user
what different on-screen pixel sizes look like; there is no distance rule. The user chose pixel thresholds from the
pictures (SDD section 11.1, R1-R5: 1 px width floor, `V` by quality, full at `4 V`). The mock-ups were deleted.

### 51e: ribbons (rules changed by the user after 51e-0: SDD section 11.1, R1-R9)

1. Contract cases 9, 10 rewritten and case 15 `RibbonPixelRule` added (on-screen length, `V` per quality, the linear
   fade between `4 V` and `V`, the 1 px width floor, no count cap, the player at every preset); tests by a fresh
   subagent from the contract only, seen failing against stubs, then green.
2. Constants in `SOLConstants.h` (`RIBBON_VANISH_PX_BY_QUALITY` 10 / 4.6 / 2.2 / 1 / 1, `RIBBON_FADE_START_FACTOR` 4,
   `RIBBON_MIN_WIDTH_PX` 1, the pool layout); the old counts and radius removed (script and tests follow).
Steps 3-8 build the **hybrid renderer** the user chose on 2026-11-21 (SDD 51 R7): Niagara (one
`NS_SOLThrusterTrails` instance per trail) for the player's two trails and for NPC ribbons at least
`RibbonNiagaraMinPx` = `RIBBON_NIAGARA_PX_FACTOR` (8) x `V` long on screen, within the physical pool of
`RIBBON_NIAGARA_POOL_TRAILS` (16, the player's 2 included; overflow goes to the strip pool, a limitation, not a
level-of-detail rule), and the GPU strip pool for every other shown ribbon. The split is by pixel size only.

3. **Pure functions, test first** (contract cases 16-18: `RibbonNiagaraMinPx`, `SplitRibbonRenderers`,
   `BuildRibbonPoints`, `PackRibbonTexels`; the header stubs and constants exist): a fresh `opus` subagent writes the
   cases in `Tests/ThrusterMathTest.cpp` from the contract only; run `Tools\RunTests.bat -Build -Filter
   SOLTest.Thruster`, see them fail by value against the stubs; implement; green.
4. **Assets and the pool component.** `M_SOLThrusterRibbon` built by `create_thruster_assets.py` (additive, unlit,
   two-sided, ISM usage; `Paths::THRUSTER_RIBBON_MATERIAL` and the parameter names: `ThrusterRibbonTex`,
   `ThrusterRibbonFrame` = anchor cm and live trail count, `ThrusterRibbon` = min width px, texels per trail, trails
   per block, points per trail as built; emissive is the shared `ThrusterTrail` vector). Its
   vertex Custom node loads the point, its neighbours and the header with `Load` and calls a new `SOLTH_RibbonVertex`
   in `SOLThruster.ush` (camera-facing across vector from `GetTranslatedWorldPosition(Parameters) - LocalPos + p -
   ResolvedView.TranslatedWorldCameraOrigin`, the Niagara trail's width floored at the device pixel size `2 d /
   (ViewToClip[1][1] ViewSizeAndInvSize.y)`, WPO = target - `Parameters.PreSkinnedPosition`); the pixel stage uses
   the same `SOLTH_TrailPixel` and `TRAIL_EMISSIVE` as `M_SOLThrusterTrail`. Run with `-AllowCommandletRendering
   -SOLRebuild -SOLOnly=M_SOLThrusterRibbon`. The pool component `Visuals/SOLThrusterRibbonComponent.{h,cpp}` (a
   `UInstancedStaticMeshComponent` subclass: block mesh like `ASOLNpcVisuals::BuildPointBlockMesh`, float texture
   like `CreateFloatTexture`, blocks sized like `SizePointBlocks`, a `ParallelFor` texel fill with `PackRibbonTexels`
   into a ring of 3 upload buffers). **Rebuild `NS_SOLThrusterTrails` as one trail per system**: the script already
   sets `TrailMaxPoints` = one trail, but the asset on disk is still the 51b 825-point build; ribbon tessellation off
   so its straight segments match the pool's.
5. **Parity spike, before any set management:** one fixed trail drawn by both renderers in a frozen scene; numeric
   crop diff of the two screenshots, near (a curved trail) and at 20+ km (the 1 px line). Fix any mismatch (width,
   floor, fade, emissive, profile) before step 6.
6. **Shared history store, Niagara set and wiring.** One history store keyed by ship (reserved, in the renderer, never
   a Mass fragment) and the shared `BuildRibbonPoints`; each frame `SplitRibbonRenderers` with the preset's
   `RibbonNiagaraMinPx` (or the `sol.ThrusterRibbonNiagaraPx` override) and the free Niagara trails
   (`RIBBON_NIAGARA_POOL_TRAILS` - 2). The Niagara set: `RIBBON_NIAGARA_POOL_TRAILS` components created once, reserved, kept alive,
   point count zeroed when unused; no per-frame activate, deactivate or allocation. Wiring: NPC hull-drawn ships from
   the placement `ParallelFor` (raw thrust, not `PlumeInstance`), the Collect gate widened to the `RibbonReachM` range
   (R4); the player's two nozzles (R5, absolute world axes, always Niagara); cvars `sol.ThrusterRibbons` (R9: 0 hides
   both renderers, skips the work and restores the pre-51e Collect gate) and `sol.ThrusterRibbonNiagaraPx`. Release a
   history not fed this frame, with fade 0, or dark for longer than `RIBBON_MAX_AGE_S`.
7. **Screenshots** at 1920x1080 and 1280x720: a curved ribbon on a turning ship, the 1 px line on a 20+ km ship, and
   the no-pop set-switch pair (frozen game, A/B with `sol.ThrusterRibbonNiagaraPx`, numeric crop diff); 2 kept as
   `Docs/Images/51-thruster-ribbon-*.png`, the rest deleted. Interleaved A/B profile with `sol.ThrusterRibbons`: the
   typical Epic spawn-view set against the 0.05 ms CPU gate (whole-frame delta, so the Niagara instance ticks count),
   the stress view at about 700 burning hulls reported, GPU at most 0.20 ms at 1920x1080.
8. **Docs and suite:** SDD, plan, ARCHITECTURE, GAME_MECHANICS, Info text if needed; full suite.

**51e status (2026-11-22, not committed): steps 1-6 done; step 7 done except a curved-NPC README shot; step 8 docs
synced (SDD, plan, ARCHITECTURE, GAME_MECHANICS; Info text needs no change); full suite 2070 / 2070 passed. Open for
the user: the spawn-view CPU gate (+0.34 ms against 0.05) and the Niagara threshold (R7).** **Superseded in look by
R10 (51e1-51e4 below):** the user rejected the cone plus ribbon from the GIFs; the store, feeds, frustum cull and pool
carry over, the two items are resolved in R10-Q8. The "Next (exact)" lists below are replaced by 51e1.
- Done (cost fixes, 2026-11-22; numbers and decisions in SDD 4.10 "Cost fixes in 51e step 7"): (1) view-frustum cull
  in the tick (off-screen ships keep their history, no pop on turning into view); (2) feeds committed in place from
  the placement tasks, per-frame state split from the sample rings, parallel store pass; (3) Niagara set components
  active only while drawing (`RIBBON_NIAGARA_IDLE_S`). Whole-frame deltas spawn / lane: before +1.11 / +0.08 ms, now
  +0.34 / +0.02 ms (lane tick 0.051 ms with one Niagara NPC trail, 0.027 ms with it in the pool). Verification:
  `GetLastPhaseMs`, `GetCulledTrailCount`, `GetActiveNiagaraCount`, `-SOLSmokeRibbonNiagaraPx=`;
  `SetVerificationNiagaraActive` removed. Valid no-pop pair (U-turn ride, AA off adjacent N/P 0.047 levels against
  A/A 0.23-0.37); U-turn ribbon straight by design; 1280x720 shots viewed; kept `51-thruster-ribbon-curved.png` and
  `51-thruster-ribbon-npc.png`, all other shots deleted.
- **Next (exact):** (1) the user's decisions: accept the spawn-view stress number or allow feeding only on-screen
  ships (dropping off-screen history), and whether to raise `RIBBON_NIAGARA_PX_FACTOR` (lane tick 0.051 -> 0.027 ms
  with NPC ribbons in the pool); (2) adversarial performance / style review (not run); (3) optionally a README shot
  of an NPC with a curved ribbon (a ride on a manoeuvring combat ship); (4) run `Tools/UpdateMetrics.bat` and commit
  51e.
- Done (step 7, 2026-11-22): `-SOLSmokeThrusterRibbons` added to `FSOLThrusterSmoke` (shots, frozen-frame pop
  sequence, U-turn and far rides; with `-SOLSmokeThrusterProfile` the `sol.ThrusterRibbons` A/B with the Niagara set
  deactivated in B) and the verification getters `GetLastTickMs`, `GetDrawnNpcTrails`, `SetVerificationNiagaraActive`
  on `ASOLThrusterRibbons`; `SOLTest.Thruster` 21/21. One profile launch (8 pairs per view): spawn +1.11 ms frame /
  +1.11 ms game thread (ribbon tick 0.50 ms with about 800 trails, NPC redraw +0.21 ms, about 0.4 ms unscoped), GPU
  not resolvable; lane ride +0.08 ms frame, ribbon tick 0.067 ms with one Niagara trail, GPU +0.05 ms. Kept
  `Docs/Images/51-thruster-ribbon-curved.png` (player, two curved ribbons); no usable far shot (a 1.63 G ribbon at
  22 km is 1.03 px, faded to alpha 0.01 by R2).
- **Next (exact):** (1) report the gate miss to the user with the options (frustum test before the split so off-screen
  ribbons are neither built nor given Niagara components; cheaper NPC feeds; fewer or lazily ticked Niagara components)
  and get a decision; (2) fix the pop sequence: face a battle at a few km (camera aimed at the longest on-screen
  Niagara trail), `sol.Npc.Combat 0` during the freeze (bolts move in real time), check the camera relative to a
  ship instead of absolute, tolerate or stop the live-count growth, then run `popdiff`-style crop diffs (AA off and
  TSR) at 1920x1080; (3) the far shot from a War-power-class ribbon (or the player's at 20+ km seen from an NPC
  viewpoint) and a curved NPC ribbon (look into the U-turn ride: plume visible, ribbon not clearly separate);
  1280x720 shots; (4) more profile launches after any fix (3 launches); (5) docs sync: ARCHITECTURE (new actor, both
  cvars, `-SOLSmokeRibbonParity`, `-SOLSmokeThrusterRibbons`), GAME_MECHANICS, the Info text; (6) adversarial review;
  (7) full suite.
- Previous status line (superseded): steps 1-5 done, step 6 done (renderers and wiring); step 7 partly (one smoke).
- Done (step 6 wiring, 2026-11-22): `SOLThruster::NpcThrustGateM` (contract case 19, `SOLTest.Thruster.ThrustGate`
  written by a fresh subagent from the contract, seen failing by value against a stub, then green) used by
  `USOLNpcSubsystem::ViewPlumeGateM` (the cone gate widened to `RibbonReachM` at the preset's V while
  `sol.ThrusterRibbons` is 1, exactly the pre-51e gate at 0; still 0 under `sol.NpcPlumes 0`). `ASOLNpcVisuals`'s
  placement pass feeds the store (`SetThrusterRibbons` from the game mode; switches and V read once before the
  `ParallelFor`; feed slots = pool-mesh ships, then faction 0 / 1 instance slots; a feed only for a burning ship whose
  ribbon reaches V or a ship that already has a trail, `HasTrail` read-only during the pass; raw `ThrustLocalG.X`, not
  `PlumeInstance`; nozzle exit and exhaust from the drawn heading in world axes) and commits after the pass. The
  player pawn feeds both nozzles from `UpdateThrusterPlume` (the `OnUniverseUpdated` placement; through `ShipRoot`,
  world axes, no pixel gate, only while `sol.PlayerPlume` is 1). Colour R7-C (rim at c, start value for 51f): a fading
  trail keeps its last burning colour. `SOLTest.Thruster` 21/21, `NpcThrust` 3/3, `PlayerThruster` 6/6.
- **Next (exact):** (1) a ribbon smoke (extend `-SOLSmokeRibbonParity` or `-SOLSmokeThruster`): Earth spawn view and
  a ride with a turning Full-tier ship, screenshots viewed (NPC and player ribbons, a curved one), and the frozen-frame
  pop diff (freeze the game, `sol.ThrusterRibbonNiagaraPx` 1e9 vs default, numeric crop diff); (2) docs screenshots at
  1920x1080 and 1280x720; (3) interleaved A/B profile with `sol.ThrusterRibbons` (typical Epic spawn view against 0.05
  ms, ~700-hull stress), GPU at 1080p; (4) ARCHITECTURE (new actor, both cvars, `-SOLSmokeRibbonParity`),
  GAME_MECHANICS, the Info text if needed; (5) adversarial review; (6) full suite.
- Done (steps 4 rest, 5, 6 renderer half, 2026-11-21; SDD 4.10 "As built in 51e (renderers)"): `ASOLThrusterRibbons`
  (`Visuals/SOLThrusterRibbons.{h,cpp}`, spawned by the game mode: shared store, feed API, Niagara set of 16 with
  `ForceTickLast`, split, release rule, draw in `TG_PostUpdateWork`, cvars `sol.ThrusterRibbons` and
  `sol.ThrusterRibbonNiagaraPx`), `USOLThrusterRibbonComponent` (pool), pure helpers `RibbonTexelOrigin`,
  `RibbonBlockCount`, `RibbonAnchorCm`, `RibbonDeviceFocalPx`, `RibbonNominalLengthM`, `RibbonTrailReleasable` with
  `SOLTest.Thruster.RibbonRendererMath` (seen failing by value against stubs, then green; `SOLTest.Thruster` 20/20).
  `M_SOLThrusterRibbon` rebuilt with the output-height floor (`ResolutionFractionAndInv`). Parity spike
  `-SOLSmokeRibbonParity` 10/10 checks: near renderers equal within the A/A noise; **the 25 km 1 px line is not yet
  equal** (one run equal, one run with a sub-pixel strip difference and a 10-13 % dimmer pool, numbers in the SDD);
  all screenshots deleted. Not reviewed, not profiled (the draw is timed by `STAT_SOL_ThrusterRibbons`).
- **Step 5 far line done (2026-11-22):** the renderers are pixel-equal with AA off at 1 px (three launches of the
  14-configuration sweep incl. 40 trails, numbers in SDD 4.10); the earlier far mismatch was TSR's frame-to-frame
  spread on a sub-pixel line (same renderer A/A gain 0.83-1.26). No renderer change. The item below is superseded.
- **(Superseded) Next (exact), first:** finish step 5 on the far line: rerun `-SOLSmokeRibbonParity` a few times, dump the pool's
  texels and the Niagara arrays of the shot frame (positions, widths) and compare the strips' vertices for the far
  trail (suspects: the end / joint tangents or the side vector on 1.3 px segments); then add a pool check with 33+
  trails and 2+ Niagara components before any wiring, so a block / row / custom-data bug is not blamed on step 6.
- **Then:** step 6 wiring. (a) NPC: in `ASOLNpcVisuals`'s placement `ParallelFor`, for each hull-drawn ship
  that burns or has a store slot, fill `BeginNpcFeeds(hullSlots)[slot]` (raw `ThrustLocalG`, not `PlumeInstance`:
  ship and nozzle exit in render-space metres, Unreal axes, after the rebase; sample offset / exhaust in world axes,
  drift `RibbonDriftMps(L_p)`, b, `L_p`, distance, colour), then `CommitNpcFeeds()` after the pass; "has a slot" needs
  a read-only per-frame lookup (e.g. a key set snapshot) or feed every hull-drawn ship within the reach. (b) Widen the
  Collect thrust gate to `max(PlumeMaxDistanceM(L_hull, f, 0.5), RibbonReachM(L_hull, f, V))` while
  `AreRibbonsEnabled()` (R4/R9). (c) Player: `FeedPlayer(0/1, ...)` for the two nozzles from the pawn's
  `OnUniverseUpdated` placement (absolute world axes, R5), never from its pre-physics tick. Then step 7 (screenshots,
  A/B profile) and step 8 (docs: ARCHITECTURE for the new actor, the two cvars and `-SOLSmokeRibbonParity`).
- **Open (user):** the ribbon colour per trail is not decided in the SDD (the spike used the plume ramp's rim colour
  at c 0.6; proposal: the rim colour at the ship's current c).
- Done (steps 1-2): SDD section 11.1 (R1-R9) and this plan; the contract's signature block, cases 9 and 10 rewritten
  and case 15 added; `SOLConstants.h` (`RIBBON_VANISH_PX_BY_QUALITY`, `RIBBON_FADE_START_FACTOR`, `RIBBON_MIN_WIDTH_PX`;
  the counts removed); `SOLThrusterMath.{h,cpp}` (`RibbonShown`, `SelectRibbonShips` without radius or count,
  `RibbonVanishPx`, `RibbonPixelLength`, `RibbonPixelFade`, `RibbonDrawnWidthM`, `RibbonReachM`, `PlayerHasRibbon`
  true everywhere; `RibbonCountForQuality` deleted); `Tests/ThrusterMathTest.cpp` (NearestRibbons, QualityScaling
  rewritten, RibbonPixelRule new, written by a fresh subagent from the contract) seen failing by value against stubs,
  then implemented; `create_thruster_assets.py` no longer reads the removed counts (`TrailMaxPoints` = one trail).
- Done (hybrid docs and contract): SDD R7 rewritten as the hybrid, R8 accepted, R9 gates; contract cases 16-18 and
  the new constants; header stubs in `SOLThrusterMath.{h,cpp}` (`FSOLRibbonShown`, `FSOLRibbonPoint`,
  `RibbonNiagaraMinPx`, `SplitRibbonRenderers`, `BuildRibbonPoints`, `PackRibbonTexels`, wrong-value bodies) and
  `RIBBON_NIAGARA_PX_FACTOR`, `RIBBON_NIAGARA_POOL_TRAILS`, `RIBBON_TEXELS_PER_TRAIL`, `RIBBON_TRAILS_PER_BLOCK` in
  `SOLConstants.h`; not built.
- Done (step 3, 2026-11-21): the cases 16-18 tests (`RibbonRendererSplit`, `RibbonPoints`, `RibbonTexels`) were
  written by a fresh subagent from the contract and seen failing by value; `RibbonNiagaraMinPx`,
  `SplitRibbonRenderers` (insertion top-k into the Niagara list holding input positions, one pool pass, no scratch,
  grow-only), `BuildRibbonPoints` and `PackRibbonTexels` implemented; `SOLTest.Thruster` 19/19 green.
- Done (step 4 assets, 2026-11-21): `SOLTH_RibbonTexel`,
  `SOLTH_RibbonPointWidth`, `SOLTH_RibbonTangent`, `SOLTH_RibbonVertex` in `SOLThruster.ush` (the tangent mirrors the
  Niagara CPU ribbon builder with tessellation off: normalised sum of the unit segments, the end tangents reflected,
  segments under 1 cm ignored; the across vector `cross(camera - point, direction)`, +-0.5 width, V 0 / 1 like
  Niagara's plane slice; the floor at the point's distance). `Paths::THRUSTER_RIBBON_MATERIAL`,
  `Paths::THRUSTER_FLOAT_DEFAULT_TEXTURE`, `ThrusterMaterialParams::RIBBON` (`ThrusterRibbon` = min width px, texels
  per trail, trails per block, points per trail), `RIBBON_FRAME` (`ThrusterRibbonFrame` = anchor render-space cm,
  live trail count), `RIBBON_TEXTURE` (`ThrusterRibbonTex`); emissive reuses `ThrusterTrail` (the brief's choice; the
  plan text above had emissive in `ThrusterRibbon`). `SOLThrusterMirror::MaterialVectors().Ribbon`;
  `SOLTest.Thruster.MaterialAssets` extended test first (ribbon material, tessellation off, screen facing,
  `TrailMaxPoints` = 25). Script: `build_ribbon`, `ensure_float_default_texture`, the ribbon renderer's
  `TessellationMode` Disabled with a read-back check. Ran `-AllowCommandletRendering -SOLRebuild
  -SOLOnly=M_SOLThrusterRibbon,NS_SOLThrusterTrails`: `M_SOLThrusterRibbon` (VS 382 / PS 100, SDD 4.11),
  `T_SOLThrusterFloatDefault` new, `NS_SOLThrusterTrails` rebuilt as one trail (25 points), tessellation off, screen
  facing. `SOLTest.Thruster` 19/19 green (MaterialAssets seen failing first on the missing material, tessellation and
  825 points). Not built or committed beyond the editor target; not reviewed.
- (Done since, see the first bullets) Earlier next step: step 4 rest, the pool component `Visuals/SOLThrusterRibbonComponent.{h,cpp}` (a
  `UInstancedStaticMeshComponent` subclass: the runtime block mesh below, `NumCustomDataFloats` 1, blocks sized like
  `SizePointBlocks`, a `PF_A32B32G32R32F` texture `1024` wide and one row per block, a `ParallelFor` texel fill with
  `PackRibbonTexels` into a ring of 3 upload buffers, a MID setting `ThrusterRibbonTex` and `ThrusterRibbonFrame`),
  then the `RIBBON_NIAGARA_POOL_TRAILS` Niagara trail set, then step 5's parity spike.
- **Design choices for the pool component (step 4 rest):** the block mesh is built **at runtime in C++** by the pool
  component (like `BuildPointBlockMesh`, no asset, bounds extension `NPC_RENDER_SPACE_BOUNDS_CM`): per trail in the
  block, 25 points x 2 vertices, UV0 = (point index, side 0 / 1), UV1.x = trail index in the block, 24 x 2 triangles
  per trail; per-instance custom data 0 = the block's first trail; the component at the render-space origin with
  identity instances (local = render cm). Texture: width `RIBBON_TEXELS_PER_TRAIL x RIBBON_TRAILS_PER_BLOCK` (1024),
  one block per row, `PF_A32B32G32R32F` like `CreateFloatTexture`. Texel positions are metres from the frame anchor
  in **render-space (Unreal) axes**, so the history store / point builder caller must give Unreal-axis positions (the
  Niagara side multiplies the same points by 100 in its component's local space, identity rotation, for parity); far
  depth compression, if ribbons need it, is the caller's job on both renderers alike. Parity caveat for the spike:
  Niagara drops points closer than 1 cm to the previous kept one (`GNiagaraRibbonMinSegmentLength`); the pool keeps
  them at zero length (such a point follows the next segment's direction, so the strip does not pinch). When the head
  and point 1 coincide, point 0 follows the segment after point 1 (Niagara drops point 1 then). The tangent mirror relies on the ribbon renderer's `bUseGPUInit` false (the engine
  default, not set by the script). **Width convention:** `FSOLRibbonPoint::WidthM` is the full width (Niagara
  `RibbonWidth`, plane slice +-0.5); the Niagara feeder floors each point with `RibbonDrawnWidthM(w, d, f,
  RIBBON_MIN_WIDTH_PX)` where `d` is the camera-to-point distance and `f` the device focal `ViewToClip[1][1] x view
  height / 2`, exactly what `SOLTH_RibbonVertex` does. The layout comment is the "Ribbon strip pool" section at the
  end of `SOLThruster.ush`.

### 51e1-51e4: one-piece flame (SDD 51 R10, user 2026-11-22)

The user rejected the 51a-51e look (a rigid cone and a bending strip from the same nozzle) and chose the option with no
visible seam: one bending flame per nozzle, widest at the nozzle, tapering to 0.1 `r_n` at `L_p`, then a thin tail, as
ballistic gas parcels. Decisions R10-1 to R10-9 and the impact table are in SDD 51 section 11.2; the open questions in
11.3. Nothing is committed until 51e4's GIFs are accepted.

1. **51e1, user answers: DONE (2026-11-22), SDD 11.3 has the full text.** Q1 realistic (`v_e` 2 km/s start value, small
   bend, tail fades over the 15-45 ms life), Q2 tube, Q3 pixel fade rule with quality-scaled vanish size (Low 10 px,
   Medium 4.6, High 2.2, Epic 1), Q4 0.25 s spool-down, Q5 ring or analytic spine is the implementer's call (report
   choice and reasons), Q6 Niagara hybrid kept (user override; both renderers match, tube and one-piece flame in both;
   Niagara cost per flame measured and reported, no gate relaxed), Q7 glow kept separate, 1 px minimum, Q8 feed only
   on-screen ships (1-3 frame grow-in), the Niagara factor retuned in 51f. No open questions remain; the user wants
   seam-free results verified by zoomed crops at several distances and view angles (51e3 / 51e4).
2. **51e2, test author** (`opus`, high, fresh, given only the contract and SDD 11.2-11.3): rewrite cases 7, 11, 18, 19,
   extend 12, delete 17 (16 is kept, Q6 hybrid), write 20-24 in `Tests/ThrusterMathTest.cpp` / `ThrusterMirrorTest.cpp`;
   header stubs for the new functions with wrong-value bodies; run `Tools\RunTests.bat -Build -Filter SOLTest.Thruster`
   and see the new cases fail by value, not by build error.
3. **51e3, implementation** (`opus`, high; context under 35 %, continued by a fresh agent from this plan if needed):
   - Pure functions and constants (`FLAME_*`; `TAIL_RADIUS_RATIO`, `RIBBON_SAMPLES`, `RIBBON_MAX_AGE_S`, the ribbon
     drift / age functions and `BuildRibbonPoints` deleted); green.
   - Assets by `create_thruster_assets.py` only: Plume sections out of `SM_SOLNpcHullCone`, `SM_SOLNpcHullBox`,
     `SM_SOLPlayerThrusters`; `M_SOLThrusterPlume` retired; `SOLTH_FlamePixel` in `SOLThruster.ush` and the flame
     material (`M_SOLThrusterRibbon`; plus `M_SOLThrusterTrail` / `NS_SOLThrusterTrails` kept and reworked, Q6 hybrid); the tube ring
     block mesh (Q2); `Paths` and `MaterialAssets` follow.
   - Store and feeds: poses pushed per `FLAME_SAMPLE_INTERVAL_S` into the 8-sample ring, held `L_p` and `c`, the Q4
     shutdown, `BuildFlamePoints`, header (`c`, `b`, `L_p`, `r_n`), release after `L_f / v_e`; the glow gate in
     `PlumeInstance` and `NpcThrustGateM`; the player's two nozzles likewise. Re-validate `NpcThrust.*`,
     `PlayerThruster.*`, the smokes (`-SOLSmokeThruster`, `-SOLSmokePlayerThruster`, `-SOLSmokeThrusterRibbons`).
   - **Seam checks by zoomed crops** (the user's "make sure with visual checks"), `-game`, frozen frame, AA off and
     TSR, 1920x1080 plus one set at 1280x720. Camera distances where the 1 px floor changes the flame: NPC (`r_n`
     1.5 m) 60 m, 290 m (the tail reaches the floor), 1 km, 2.9 km (the base reaches it), 10 km; player (`r_n` 0.8 m)
     30 m, 150 m, 1.5 km. View angles: side (90 degree to the axis), oblique (45 degree), near-axial (the chase
     camera, about 10 degree). At each: crop around the `L_p` mark at 4x zoom and view it; numerically, cross-sections
     every 2 px along the projected spine (FWHM and summed gain) form a curve along the flame, and the step between
     neighbouring cross-sections at `L_p +- 10 %` must not exceed the largest step elsewhere on the flame plus the A/A
     spread of two shots of the same frame. A turning case too (the player yawing at War power, a manoeuvring Full
     NPC). Niagara / pool parity (Q6 hybrid) and no-pop pair again on the new look.
   - Profile (interleaved A/B with `sol.ThrusterRibbons`, the 51e method) at the spawn and lane views; GPU at 1080p
     against 0.20 ms (a tube adds vertices); adversarial performance / style review; docs (`GAME_MECHANICS.md`
     thruster rows and the Info line say "flame", not "cone"; `ARCHITECTURE.md`; `STATUS.md`); full suite.
   **51e3 status (2026-11-22, in progress, not committed; resume from here):**
   - Q5 decided: the pose history ring (R10-8), not the analytic spine. Reasons: exact for any manoeuvre (turn start
     and end, rolls, the player's free rotation) where a constant-turn spine needs a per-ship angular rate and is wrong
     at every change; contract cases 7 and 21 already pin `FSOLPlumeHistory` and `BuildFlamePoints(history, head, ...)`;
     the feed cost is one 32-byte pose copy per fed ship per frame and the store/feeds of 51e carry over unchanged.
     Feed cost measured in the profile step (not yet run).
   - Done: constants (`FLAME_*`, `FLAME_TUBE_SIDES` 8 (now 16, see 51e3 second agent), `FLAME_STREAK_TAIL_STRENGTH`; `TAIL_RADIUS_RATIO`,
     `RIBBON_SAMPLES`, `RIBBON_MAX_AGE_S`, `RIBBON_LENGTH_FACTOR`, `RIBBON_POINTS_PER_TRAIL`, `TRAIL_EMISSIVE`,
     `Paths::THRUSTER_PLUME_MATERIAL`, `SLOT_PLUME`, `ThrusterMaterialParams::TRAIL` deleted; `ThrusterMaterialParams::FLAME`
     and `ThrusterTrailParams::FLAME_HEADER` added); pure functions `FlameLengthM`, `FlameWidthM`, `FlameAlongAlpha`,
     `FlameTurnOffsetM`, `BuildFlamePoints`, `FlameCoreWeight`, `FlameKnotIntensity`, `FlameStreakDepth`,
     `FlameShutdownBrightness` (new glue, test `SOLTest.Thruster.FlameShutdown` written first by a subagent), new
     `PackRibbonTexels` / `NpcThrustGateM` / `RibbonTrailReleasable` signatures, `PlumeInstance` gains r_n with the glow
     gate; ribbon drift/age functions, `BuildRibbonPoints`, `RibbonNominalLengthM` deleted. Float mirror
     `SOLThrusterMirror::FlameCoreWeight / FlameKnot / FlameStreakDepth` and test `SOLTest.Thruster.FlameMirror` (case 12
     sweep; the L_p 30, r_n 1.5, b 1 zone-edge knot excluded). Re-validated by a subagent: `NpcThrust.PlumeInstance`,
     `PlayerThruster.Plume`, `Thruster.MaterialAssets` (R10 asset contract: no plume material or slot, tube Niagara
     ribbon, flame look defaults on both flame materials). Store rework (`ASOLThrusterRibbons`: feed = pose + c + L_p,
     spool-down, dark poses pushed, release after `L_f / v_e`, `IsOnScreen` from last frame's frustum, Niagara colour
     (s / L_f, alpha) + `TrailFlame` header); NPC feeder feeds only on-screen ships; player feeder; parity smoke
     reworked to poses. `SOLThruster.ush`: plume section and `SOLTH_TrailPixel` / `SOLTH_RibbonVertex` replaced by
     `SOLTH_FlamePixel`, `SOLTH_FlameNoiseUv`, `SOLTH_FlameRadialFraction`, `SOLTH_FlameVertex` (Niagara Tube slice
     mirrored: vertex k at angle -2 pi k / 8, geometry normal, axial taper fallback). Pool block mesh is 8-sided tubes
     (9 vertices per ring, V = k / 8; now 16 sides, 17 vertices). Script: plume builder and mesh plume sections removed, `build_trail` /
     `build_ribbon` rebuilt on the flame pixel, Niagara Shape Tube / 8 / geometry normals / header user vector, retired
     `M_SOLThrusterPlume` deleted on rebuild. C++ builds.
   - Done since (same day): asset script run (M_SOLThrusterTrail VS 153 / PS 243, M_SOLThrusterRibbon VS 1028 / PS
     267 instructions; meshes without Plume slots; NS_SOLThrusterTrails Tube 8 (now 16); M_SOLThrusterPlume deleted);
     SOLTest.Thruster 26 / 26, NpcThrust + PlayerThruster + NpcRail + Npc.HullFlip 41 / 41. Two engine
     findings fixed: (a) a Niagara tube with geometry normals draws nothing in 5.8, so the ribbon keeps the camera-facing
     normals and SOLTH_FlameVertex mirrors them (SOLTH_FlameRadialFraction divides the facing part by sin(psi) to
     recover the ring's radial direction); (b) a Niagara component deactivated and activated again draws nothing (observed;
     cause not confirmed, likely the one-shot particle burst; the old sweep passed because each config used a fresh component), so
     idle set components are paused and hidden instead (UseNiagaraComponent / DeactivateNiagaraComponent). Thin-flame
     shading added (FLAME_SHARP_LOW_PX 1, FLAME_SHARP_HIGH_PX 4, SOLTH_FlameSharpness, Niagara colour b): the
     radial fraction blends to 0.5 where the drawn width is the floor or a few px, which removed a white-to-orange colour
     step at the L_p mark at 290 m (a 1 px line's pixel centres sat on the rim). Seam harness: -SOLSmokeRibbonParity
     -SOLSmokeFlameSeam (71 configurations, 5 shots each, project anti-aliasing), crops by a scratch script (4x nearest,
     sheets of 1440 px). Verdict by eye, final shaders (TSR, sharpness), 4x crops at the L_p mark: no seam, width or brightness step at NPC 100 m,
     290 m (side and 45 degree), 1 km, 2.9 km side and player 150 m side, plus the full player War power 30 m oblique frame;
     the 1 px width changes along the taper are pixel quantisation. NOT viewed on the final shaders (shots deleted): chase,
     front, the turns, idle / 1 G / 3 G / boost, NPC 30, 60, 500 m, 2, 10 km and player 30 m side, 500 m, 1.5 km (the
     earlier AA-off run showed chase, front, turn and 1 G at half size only): re-run the seam sweep and view every sheet.
     Niagara vs pool on the final shaders, legacy sweep (side view, AA off, sum of absolute pixel differences): near
     67.3 k vs A/A 95.2 k, far 1 px 27.1 k vs 41.3 k, farfat 28.8 k vs 49.6 k, multi 101 k vs 174 k: equal within the
     noise. OPEN pop risk: in the AA-off seam run (before sharpness) the pair differed view-dependently, 3 G at 100 m
     side 0.01 % but 45 degree 2.3 % (max pixel 29 vs A/A 19), boost 45 degree 2.7 %, chase 5-8 %
     (player-war-30m-v10-turn max 84 vs A/A 19): check AA-off pairs at 45 and 10 degree and find the cause (suspects:
     the facing-normal interpolation vs the pool's, the end tangents) before 51e4. The real feeders (NPC placement feed,
     the pawn's two nozzles, spool-down, on-screen feeding, release, a hard player yaw) have NOT run at runtime (the
     sweep uses -SOLNpcCount=0 and sol.PlayerPlume 0): run -SOLSmokeThrusterRibbons and view its shots.
     Open look items for 51f:
     looking down the axis (chase, 10 degree) the 8 sides showed as an octagon at 30 m (RESOLVED: FLAME_TUBE_SIDES 16, see below); the flame core saturates to white at
     War power (emissive tuning). README images: the placeholder seam-harness frames 51-thruster-flame-turn.png and 51-thruster-flame-oblique.png were replaced by real captures Docs/Images/51-thruster-flame-player-warpower.png (player War power, real feeder) and 51-thruster-flame-npc-lane.png (NPC lane ship flame); README.md unchanged until 51e4 acceptance; the
     untracked 51-thruster-ribbon-*.png deleted; the committed 51c/51d images stay until 51e4.
   - Full suite 2075 / 2075 passed (2026-11-22, after every change above); settings files restored (MD5 checked).
   - Continued (2026-11-22, second agent): tests first for the untested glue, seen failing by value against stubs,
     then green: `SOLTest.Thruster.FlameSharpness` (the CPU copy moved to `SOLThruster::FlameSharpness`, parity with
     the new float mirror `SOLThrusterMirror::FlameSharpness`) and `SOLTest.Thruster.FlameOnScreen`
     (`ASOLThrusterRibbons::SphereTouchesFrustum`, the pure test behind `IsOnScreen`); SOLTest.Thruster 28 / 28.
     Chase-view octagon fixed: `FLAME_TUBE_SIDES` 8 -> 16 (facets 7.6 % -> 1.9 % of the radius; NS_SOLThrusterTrails
     rebuilt, pool mesh follows the constant); the player chase crop at 30 m reads round. Seam sweep now 83
     configurations (player also at 100 m and 2 km, the burn states also in the chase view), new flags
     `-SOLSmokeFlameSeamAAOff` and `-SOLSmokeFlameSeamOnly=<text>`; crops by `seam4.py` (true 4x, whole lit flame).
     Niagara / pool pop risk found and fixed: with AA off, the flame-masked difference was at the A/A level in side
     views but 1.3-2 % (45 degree) and 3-12 % (chase) of the flame's light, Niagara brighter; an experiment (radial
     fraction forced to the taper) removed it, so the normal-based radial fraction disagreed between the renderers.
     `SOLTH_FlameRadialFraction` now takes |cos(2 pi V)| from the interpolated around coordinate (exact in every view,
     identical in both renderers; the axial blend to the taper kept); the pool's normal output and fourth
     interpolator are gone (M_SOLThrusterRibbon VS 1028 -> 684 instructions, PS 254; M_SOLThrusterTrail VS 153 / PS
     230). Re-check (AA off, `par2.py`): every chase and 45 degree configuration now at or below the A/A spread.
   - Done (2026-11-22, second agent): real-feeder smokes passed (-SOLSmokeThruster -SOLSmokeThrusterRibbons 3 / 3,
     -SOLSmokeThruster 5 / 5, -SOLSmokePlayerThruster 25 / 25); docs synced (SDD 11.2 / 11.3, math contract,
     ARCHITECTURE, GAME_MECHANICS, STATUS); the in-game text (`SOLMenuText.h`, "engine plume") makes no cone or side
     count claim, so it is unchanged; `SOLTest.PlayerGuide.UpToDate` compares `PLAYER_GUIDE.md`, not GAME_MECHANICS.
     Profile at 1920x1080 Epic, interleaved A B B A, 240 frames per window (player 300), A minus B of the means:
     - `sol.ThrusterRibbons` (8 pairs per view): `STAT_SOL_ThrusterRibbons` spawn view (about 1,200 hulls, 88 lit,
       21-28 pool flames) 0.102 ms (pool 0.062, store 0.009, commit 0.008; sd 0.007), lane view with one Niagara NPC
       flame 0.070 ms (Niagara arrays 0.016, move 0.006): both MISS the 0.05 ms CPU gate (about 2x and 1.4x). GPU
       -0.017 ms (spawn) and -0.015 ms (lane), noise (sd 0.04): no measurable GPU cost. The B windows' sub-timers
       repeat the previous window's values (stale when the set is off), so only the A sub-timers are used.
     - #51 set on / off (`-SOLSmokeThrusterProfile`, 4 pairs): NPC GPU spawn -0.017 ms, lane +0.027 ms; player
       (`-SOLSmokePlayerThrusterProfile`, War power) GPU +0.070 ms (sd 0.01): all within the 0.20 ms gate. Game
       thread A minus B +0.12 to +0.30 ms, but sd about 0.2 and the A / B windows differ in hull count (spawn 1,226 vs
       1,145), so not a usable number. The spawn-view stress case was not re-run.
   - Continued (2026-11-22, third agent, the user's two approved jobs; NOT committed, all in the tree):
     **(B) end caps (user's choice over an arc fade), built:** `FLAME_DRAWN_POINTS` 27 (spine + nozzle cap + tail cap),
     `FLAME_CAP_DEPTH_NOZZLE_RADII` 0.05, `FLAME_CAP_MIN_DEPTH_M` 0.02, `SOLThruster::FlameCapDepthM` / `AddFlameCaps`
     (contract case 25), `PackRibbonTexels` on the drawn points (case 18 rewritten, `RIBBON_TEXELS_PER_TRAIL` 34), cap
     fraction in `SOLTH_FlamePixel` (Niagara colour a, pool `Interp2.w`), `SOLTH_FlameVertex` ring scale and arc
     fraction input, pool block mesh 27 rings, `NS_SOLThrusterTrails` 27 particles, both flame materials rebuilt
     (Trail VS 153 / PS 230, Ribbon VS 700 / PS 255). SDD 11.2 "Hollow tube: RESOLVED" has the design. Tests first by a
     subagent (seen failing by value: FlameCaps, RibbonTexels, RibbonRendererMath, MaterialAssets), then
     SOLTest.Thruster 29 / 29. Seam sweep (-SOLSmokeRibbonParity -SOLSmokeFlameSeam, now 88 configurations with v135
     front-quarter cap views) 440 / 440 checks passed; 4x crops viewed: chase (v10) idle / 1 G / 3 G / boost / War
     power player 30 m and NPC 100 m, v135 NPC 60 m and player 30 m: the nozzle end is filled (no rim arc, no
     pinhole), no seam or brightness step at the cap rim, Niagara and pool identical by eye. One residual: at idle in
     the chase view a faint grey dot about 3 px wide, about 5 % darker, on the axis where the cap's streak-noise spokes
     converge (the old pinhole was black); candidate fix: blend the streak noise to its mean by the cap fraction. Not
     yet viewed: v170, v45 / v90 at the far distances (290 m - 10 km), the turn configurations.
     **(A) CPU cut, built and measured** (interleaved A B B A, 8 pairs, 1920x1080 Epic, `ribbonMs` = the actor tick):
     projection cached (`UpdateView`: `GetProjectionData` only when the viewport size, FOV or aspect change, frustum
     from the camera now), anchor hold (`RibbonAnchorHoldCm`, `RIBBON_ANCHOR_HOLD_M` 1000, case 24: Niagara moves only
     on re-anchor), Niagara header and point count set only on change, pending NPC feeds listed atomically (commit no
     longer scans every feed slot; `BeginNpcFeeds` no longer clears them; the parity smoke now feeds through
     `FeedNpc`), pool fill inline up to 32 flames (`RIBBON_FILL_BATCH` 32), `BuildFlamePoints` gathers its poses once.
     A direct proxy path for the Niagara arrays was tried and dropped (its helper header is private to Niagara). New
     verification phases: setupMs, fillMs, nBuildMs. Before -> after: spawn view (about 25 pool flames) 0.0975 ->
     0.060 ms (setup 0.026 -> 0.005, fill 0.046 -> 0.036, store 0.008, rest of the pool 0.014 -> 0.010, commit 0.008 ->
     0.0003): still MISSES 0.05 (about 1.2x); lane view (one Niagara flame) 0.0628 -> 0.0345 ms (arrays 0.016 ->
     0.0145, move 0.0057 -> 0.0002, setup 0.012): passes. GPU A - B spawn +0.003, lane +0.027 ms (noise, sd about
     0.04). What would close the spawn gap: the pool fill (about 1.4 us per flame, 27 points) is the bulk; options are
     building pool spines on the GPU from the uploaded pose ring (no CPU points for pool flames), fewer points per pool
     flame (changes the look and the contract), or a ParallelFor above a measured flame count.
   - Continued (2026-11-22, fourth agent; NOT committed): **baseline smokes on the caps build all passed**
     (-SOLSmokeThruster 5 / 5, + -SOLSmokeThrusterRibbons 3 / 3, -SOLSmokePlayerThruster 25 / 25, -SOLSmokeRibbonParity
     70 / 70, + -SOLSmokeFlameSeam 440 / 440). **CPU pass (one bounded pass, no gate relaxed):** a throwaway
     micro-benchmark (deleted) put the pool fill per flame at 1.74 us hot (BuildFlamePoints 1.55, AddFlameCaps 0.18,
     PackRibbonTexels 0.23). Kept: `BuildFlamePoints` tables its per-point along alpha and taper (both depend on j
     only: s / L_f = j / 24, s / L_p = 3 j / 24) and converts each pose pair once (1 / span, the pair's velocity), one
     sqrt-reciprocal per point; the pool fill builds the spine in place at drawn points 1.. so `AddFlameCaps` only
     writes the two caps (in-place case added to `SOLTest.Thruster.FlameCaps` and contract case 25); the pool's frame
     parameter (anchor, count) is sent only on change (no measured benefit: the "rest of pool", about 0.012 ms, did not
     move; that it is the texture-region upload is inferred, not measured). Hot per flame 1.74 -> 0.93 us; same output
     (benchmark checksum identical, SOLTest.Thruster 29 / 29). **An in-game gain is not established:** there is no
     same-session baseline run of the old code. Tried and reverted: `RIBBON_FILL_BATCH` 6 with an inline path under 12 flames (slower:
     task wake-up and waits). In game (A B B A, 8 pairs a run, 1920x1080 Epic, `ribbonMs` = the actor tick, A windows):
     the machine ran in two states (spawn-view `npcMs` 1.62 vs 1.84 ms, `gameMs` 7.3 vs 8.4 ms; cause not identified,
     and `run_g2.ps1` did not bring the game window to the front), so absolute numbers differ by run: fast state 0.050 ms (builder cut only, 22 pool
     flames) and 0.055 ms median (final but the frame-param skip, 26 flames, fill 1.15 us per flame); slow state, final
     code, four runs: 0.070, 0.075, 0.073, 0.075 ms (25-29 flames, fill 1.5-1.6 us per flame). **The spawn view still
     misses the 0.05 ms gate** (1.1x in the fast state, 1.4-1.5x in the slow one); the lane view (one Niagara flame)
     passes in every run: 0.034-0.039 ms (Niagara arrays 0.014-0.017). The whole-frame delta is not resolvable (game
     thread sd about 0.2 ms). What is left of the spawn cost: fill about 1.2-1.6 us per flame in game (cache misses on
     each flame's history, trail and texels; 0.93 us hot), the texture upload about 0.012 ms, store 0.008-0.011, setup
     0.006. Options for the user: GPU-built pool spines from the uploaded pose ring, or fewer points per pool flame (a
     contract and parity change). **Idle axis dot:** the cap streak-noise blend to its mean was tried and did NOT remove
     it (pixels 180-188 vs 200-210 around, about 2 x 4 px; it smoothed the converging spokes only), so it was reverted;
     the dot is not the noise mip. At 1x it is faint but findable on close inspection, only at idle in the chase view.
     With anti-aliasing off (`-SOLSmokeFlameSeamAAOff`, idle chase only) the dot stays, in BOTH renderers alike
     (pixels 173-191 beside a brighter 210-214 column, median 200): not TSR and not the streak noise. (Superseded by
     the fifth agent below: the dot is NOT in the cap; it is the narrow tail seen through the neck, now fixed.)
   - **Final build verification (2026-11-22, fourth agent):** smokes re-run on the final C++: -SOLSmokeThruster 5 / 5,
     + -SOLSmokeThrusterRibbons 3 / 3, -SOLSmokePlayerThruster 25 / 25, -SOLSmokeRibbonParity 70 / 70, the chase v10
     seam subset 100 / 100 (trail-count checks, not a seam verdict); full suite **2078 / 2078**. Profiles with the game
     window kept in front (fast machine state, spawn `npcMs` 1.61): `sol.ThrusterRibbons` 8 pairs: spawn view
     `ribbonMs` median 0.054 ms (mean 0.057, 25 pool flames, fill 1.17 us per flame) **MISSES 0.05**; lane view 0.033
     ms passes; GPU A - B spawn +0.024, lane +0.033 ms (sd 0.05-0.13, noise). #51 set (`sol.NpcPlumes`, 4 pairs) GPU
     spawn +0.039, lane +0.037 ms. Player War power (`sol.PlayerPlume`, 4 pairs) GPU +0.007 ms (pair sd 0.067; the
     A and B windows of a pair fly different G, only pair 0-1 matches at 6.8 G: -0.031): all inside the 0.20 ms gate.
   - Continued (2026-11-22, fifth agent; NOT committed): **idle axis dot found and fixed.** A debug build of
     `SOLTH_FlamePixel` wrote each layer to its own channel: the caps in green, the tube before `L_p` in blue, the tube
     after it in red. At idle the cap layer is smooth through the dot. The tube before `L_p` has a clean hole there,
     about 4 px, and the tube after `L_p` fills it only dimly. With `-SOLSmokeFlameSeamOnly=30m-v10` the hole moves
     tailward with `L_p` (at 1 G it sits at the disc edge). Cause: the hollow, additive, two-sided shell is not convex
     at the neck. Seen down the spine from the nozzle side, a ray that enters the narrow tail's mouth meets its far wall
     a run of w |sin a| cot(theta) further on, where the along fade is lower than on the cone wall beside the mouth.
     Fix: `SOLTH_FlameNeckAlpha` (shader body only, no new material argument, no asset rebuild). The tail's far wall,
     seen from the nozzle side, takes the along fade at the ray's entry, never before `L_p`, so it only ever brightens
     and blends continuously. Float mirror `SOLThrusterMirror::FlameNeckAlpha`, test `SOLTest.Thruster.FlameNeck`
     (written after the fix, as a bug-fix test, no contract case). `SOLTest.Thruster` 30 / 30.
     **Tail side too:** the harness's v10 has the nozzle nearest, so the player's real chase view (exhaust toward the
     camera) is the v170 family, which had only War power. New seam configurations `player-<state>-30m-v170` (idle,
     1 G, 3 G, boost), now 92 in all. Idle from the tail side showed the same dot: core minimum 171-179 against a median
     of 197, anti-aliasing off. The fix was generalised: the tail wall the ray crosses farther along s takes the fade at
     its other crossing, the far half from the nozzle side and the near half from the tail side. After it the minimum is
     180-183 (median 199-200) and the dot no longer shows at 4x in either renderer. The branch is live in every
     tail-side view, so the v170 War power views were re-checked (TSR, NPC 30 / 60 / 100 m, player 30 / 100 m). At NPC
     30 m the narrow waist just before the disc (column width 6 px, summed gain 14, against 8-9 px elsewhere on the tail)
     is gone (9 px, 17): the same neck deficit. The tail now runs evenly into the disc, and Niagara and pool agree to
     1 px. The 1 G, 3 G and boost v170 views are clean. Final TSR sweep: 92 configurations, 460 / 460 checks passed.
     With anti-aliasing off at idle (`dotdump.py`), the minimum went from 173 (median 199-200) to 189 Niagara and 192
     pool (median 201), inside the cap streak's spread of 189-206. Both renderers agree, so the tangent sign matches.
     The bright column beside the old dot, about 210 against 200, is the same layer-count effect (the tail's extra
     walls behind the cone). It is unchanged by the fix and was there before it; judged at 1x it is faint. At 1 G the
     light tip where the tail meets the disc is the same with the fix off (checked).
     1 G, 3 G, boost, War power and the turn at v10, and v135, are unchanged by eye in both renderers, with anti-aliasing
     off and with TSR. Removing the layer-count effect entirely is the user's call (it changes the look or the
     contract): face culling with a brightness rebalance, a convex taper, or another tail width ratio.
     **Seam sweep on the final shaders** (`-SOLSmokeRibbonParity -SOLSmokeFlameSeam`, TSR, 440 / 440 checks passed),
     viewed at 1x / 2x / 4x around the flame (scratch `flamesheet.py`):
     - v170 (tail nearest; NPC 30 m-10 km, player 30 m-2 km): no seam, kink or brightness step. Near the disc the
       tail narrows quickly on screen (perspective compresses its far end), with 1 px row steps along the band (pixel
       quantisation, at different places in the two renderers under TSR).
     - v45 and v90 from 290 m out (NPC 290 m, 500 m, 1 km, 2 km, 2.9 km, 10 km; player 500 m, 1.5 km, 2 km; pool also
       checked for NPC 500 m and 2 km): smooth tapers and 1-2 px lines, only slope and row quantisation.
     - The three turns (NPC 100 m v90, player 30 m v90 and v10): smooth bends with no kinks, Niagara and pool alike.
       The v90 turns were judged as the whole flame at 1x, with 4x on a body section only, not at the `L_p` mark.
     `np_max` (Niagara against pool) is within the range of `aa_max` (two shots from one renderer) across these
     views: 25-489 against 87-558. Docs synced (SDD 11.2 "Idle axis dot", 11.3 "As reported", ARCHITECTURE,
     STATUS; GAME_MECHANICS unchanged, no mechanic changed). Full suite **2079 / 2079** (2078 plus `FlameNeck`),
     settings files restored (MD5 checked), `graphify update .` run.
   - Next (exact), after the fifth agent: (1) the user's decision on the spawn-view CPU miss (0.054 ms median against
     0.05, about 1.1x; GPU pool spines or fewer points per pool flame); (2) DONE: the full seam sweep and the views not
     yet seen; (3) STILL OPEN: the anti-aliasing-off Niagara / pool parity of the whole sweep (it predates the caps
     and the neck fix); (4) optional: re-measure the GPU (the figures predate the neck pixel code); (6) DONE: docs;
     (7) STILL OPEN: the adversarial performance / style review; (8) DONE: full suite; (9) DONE: `graphify update .`,
     `Tools\UpdateMetrics.bat` still to run before the commit; then 51e4. The older list below is kept for reference:
     the full seam sweep on this build (`-SOLSmokeRibbonParity -SOLSmokeFlameSeam`) and view the crops not yet
     viewed: every v170, v45 / v90 from 290 m out (NPC 500 m and 2 km included), the three `-turn` configurations
     (`seam4.py`, also with `pool-a` for the far NPC ones, compare `np_max` with `aa_max`); the chase v10 subset was
     re-run on this build (100 / 100) and its idle / 1 G crops viewed; (3) the AA-off parity of the whole sweep
     (`-SOLSmokeFlameSeamAAOff`, `par2.py`; the last one predates the caps); (4) DONE: GPU profiles and the smokes on
     this build (see "Final build verification"); optionally a player GPU run whose A / B windows fly the same G;
     (6) `ARCHITECTURE.md` (27 drawn points with end caps, anchor hold, projection cache,
     pending feed list, 88 seam configurations, the fill notes above), `GAME_MECHANICS.md` (end caps if a claim
     changes), `STATUS.md` (#51: caps built, CPU numbers, the hollow-tube item resolved), SDD 11.2 / 11.3 (the Q5
     history-ring choice with these measurements, the CPU numbers); `SOLTest.PlayerGuide.UpToDate` must stay green;
     (7) adversarial performance / style review (fresh opus, report only); (8) full suite (last 2075, plus the
     FlameCaps in-place case); (9) `graphify update .` (never commit graphify-out), `Tools\UpdateMetrics.bat`; then
     51e4. Games and tests must run outside the sandbox (inside it the game hangs at "Waiting for ZenServer"). Keep the
     game window in front while profiling (`keep_focus.ps1`) and log `npcMs` with every CPU figure (machine state). Scratch runner `run_g2.ps1` (-Extra flags, -LogName; restores both
     settings files), `run5.ps1` (the five smokes), `prof2x.ps1`, `rib_prof.py`, `fitrib.py` (ribbon cost vs pool
     count per run), crop scripts `capsheet.py`, `seam4.py`, `crop1x.py` (1:1 and 4x) in the session scratchpad
     `C:\Users\DIESLO~1\AppData\Local\Temp\claude\497f8b90972349d9\01e67762-2ee7-4db8-a96c-decfd7648c4f\scratchpad`.
   **51e review (adversarial performance and style review, fixes done 2026-11-22):** the strip pool instances only the
   blocks its live flames need (`RIBBON_BLOCK_SHRINK_FRAMES` 60 before a trailing block goes; before, 8 blocks, 117,504
   vertices, were drawn whenever one pool flame showed); the strip pool draws every flame when the Niagara system is
   missing; the drawn-trail report never lists the player as an NPC; `DeactivateNiagaraComponent` renamed
   `ParkNiagaraComponent`; dead `SelectRibbonShips`, `PlayerHasRibbon` and `PlumeAllowed` removed (contract cases 9, 10
   and 13 trimmed); new `SOLTest.Thruster.Store.*` (6 cases, each checked to fail under a deliberate mutation of the
   code it covers). The mirror header states what it does not mirror. Checks: `SOLTest.Thruster` 36/36; smokes
   `-SOLSmokeThruster` 5/5, `+Ribbons` 3/3, `-SOLSmokePlayerThruster` 25/25, `-SOLSmokeRibbonParity` 70/70,
   `+FlameSeam` 460/460. Profile (2 runs each, before / after): ribbon CPU 0.032-0.034 / 0.034-0.037 ms in the lane view,
   0.054-0.055 / 0.054-0.056 ms in the spawn view, GPU and NPC ms inside the run spread. Materials now
   `M_SOLThrusterRibbon` VS 700 / PS 277, `M_SOLThrusterTrail` VS 153 / PS 250. Tangent-in-texels not built (more CPU
   fill on an already missed spawn-view CPU gate).
4. **51e4, GIFs again:** the same set the user reviewed (player at 3 G and War power, turning, side and chase views;
   NPC lane burn, flip, U-turn, brake), shown to the user; on acceptance, README shots replaced (SDD impact table),
   `Tools\UpdateMetrics.bat`, commit 51e with R10 (`Issue #51: Part 51e - ...`); all GIFs and crops deleted.

### 51f: tuning, profile, docs, gallery

1. In-game tuning with the user (colours, knot look, streak speed, burst look, light intensity, LOD thresholds).
2. Profile against the gates below with interleaved A/B runs (Earth spawn view at 1M default budgets, a busy lane view,
   the player boost and War power view) at 1920x1080 with `stat` / Unreal Insights; 4K and 1280x720 spot checks. **VR
   record only:** one-eye and two-eye XR-4 numbers written to the SDD.
3. Docs (Q22): `GAME_MECHANICS.md` (Thruster plume row, Lane traffic and War power notes), the in-game Info text,
   `ARCHITECTURE.md`, `STATUS.md`, `TECH_DEBT.md` for anything accepted; README gallery 2-3 shots at 1920x1080.
4. Whole-ticket adversarial review; fixes; full suite and smokes re-run; commit and push.

## Tests to write or re-validate

| Test | Part | Action |
|---|---|---|
| `SOLTest.Thruster.*` (contract cases 1-11, 13, 14) | 51a | new, test first, from the contract file only |
| `SOLTest.Thruster.MaterialMirror` (case 12) | 51b | new, test first |
| `SOLTest.NpcTrafficMath.ThrustState` | 51c | re-read against the plume's use (contract unchanged) |
| `SOLTest.NpcRail.*`, `SOLTest.Npc.HullFlip.*` | 51c | re-validate (the Collect pass changes) |
| Flight / War power tests (`SOLTest.WarPowerMove.*`, flight model) | 51d | re-validate (`FSOLShipStateFragment` gains a field) |
| `SOLTest.Thruster.GasBursts` | 51g | extend with `BurstLengthM` (Q32); `PlumeAllowed` was removed in the 51e review (Q27 holds by construction, smoke P3) |
| `SOLTest.Thruster.Store.*` (SpoolDown, ReleaseAcquire, PendingOrder, ResumeFallback, PlayerExemption, PoolBlocks) | 51e review | new: the shared store and the strip pool's blocks in a test world |
| `SOLTest.Thruster.NearestRibbons`, `QualityScaling`, `RibbonPixelRule` (cases 9, 10, 15) | 51e | rewritten / new, test first |
| `SOLTest.Thruster.RibbonRendererSplit`, `RibbonPoints`, `RibbonTexels` (cases 16-18) | 51e | new, test first, from the contract only |
| `SOLTest.Thruster.FlameProfile`, `FlameParcels`, `FlameRigidity`, `FlameSeam` (cases 20-23); `RibbonRendererMath` (case 24); cases 7, 11, 18, 19 rewritten, 12 extended, 17 deleted (16 kept, Q6 hybrid) | 51e2 | new / rewritten, test first, from the contract only |
| `SOLTest.NpcThrust.PlumeInstance`, `SOLTest.PlayerThruster.Plume`, `SOLTest.Thruster.MaterialAssets` | 51e3 | re-validate (glow gate; plume material and sections gone) |

## Performance gates (SDD 51 Q21)

- NPC redraw CPU delta at most 0.10 ms (Earth spawn view, 1M NPCs, default budgets).
- Plume GPU at most 0.20 ms at 1920x1080 (busy lane view, player boost view), bursts and the player light included.
- Ribbons at most 0.05 ms CPU for the typical Epic spawn-view set, as the whole-frame delta so the Niagara instance
  ticks count (SDD 51 R9); a stress view at about 700 burning hulls reported; their GPU cost inside the 0.20 ms gate.
- The default-budget 1M worst case still passes 60 fps (SDD 11 Amendment 6).
- Interleaved A/B profiling. VR one-eye / two-eye: recorded only, not gated.

## Risks

See SDD 51 section 9: instanced-hull upload cost (6 custom floats), translucent smear without motion vectors, the
third-person camera looking down the player's plume, red-orange vs faction 1 orange, the nominal-profile coast,
additive and translucent overdraw in busy lanes, the burst / light scope increase, script-only assets.

## Change log

- 2026-11-20: drafted with SDD 51; parts 51-0 to 51f; blocked on Q1-Q26.
- 2026-11-20: Q1-Q26 answered; 51-0 done; contract file added; part 51g (gas bursts) and the player hull light (51d)
  added as scope increases; Q27-Q33 open.
- 2026-11-21: Q27-Q33 answered and folded in (plumes and bursts only on hull-drawn ships, light gating, uncapped
  Cinematic ribbons, smoothed NPC burst acceleration, burst port table, burst size, 1 px glow floor); nothing blocked.
- 2026-11-21: part 51b built (assets, shader, mirror and tests); status under 51b.
- 2026-11-21: 51e steps 3-8 rewritten for the user's hybrid ribbon renderer (SDD 51 R7: Niagara for the player and for
  NPC ribbons at least `RibbonNiagaraMinPx` on screen within the physical Niagara pool, the GPU strip pool for the
  rest; parity spike before set management); contract cases 16-18 and header stubs added.
- 2026-11-22: the user rejected the cone-plus-ribbon look; SDD 51 R10 (one-piece ballistic flame) and parts 51e1-51e4
  added; contract cases 20-24 new, 7, 11, 18, 19 rewritten, 12 extended, 17 deleted.
