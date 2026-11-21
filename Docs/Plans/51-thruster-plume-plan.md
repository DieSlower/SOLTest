<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 51: Placeholder main thruster plume

**Issue:** [#51](https://github.com/DieSlower/SOLTest/issues/51). SDD:
[`SDDs/51-thruster-plume.md`](../SDDs/51-thruster-plume.md). Test contract:
[`51-thruster-math-contract.md`](51-thruster-math-contract.md).
Status: **Q1-Q26 answered by the user 2026-11-20 and Q27-Q33 on 2026-11-21 (SDD section 11). No open questions remain; no
part is blocked. Asset creation is approved (Q20).**
**Persistence: none.** `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay; graphics quality is read from the
existing `QualityPreset`. Any finding that needs a schema change is stopped and reported.
**Scope increase (user, 2026-11-20):** gas bursts for reverse / lateral / vertical thrust on the player and NPCs (Q7,
part 51g) and a player hull light (Q17, in 51d).

## Parts

Each part is built by its own subagent (context under about 35%), test first where it adds or changes pure functions,
then reviewed by a fresh adversarial `opus` subagent (performance and style, with the TECH_DEBT items listed in SDD 51
section 3), then committed and pushed (`Issue #51: Part 51x - ...`), with `Docs/Plans/STATUS.md` and the metrics updated
in the same commit. Temporary screenshots, mock-ups and scratch renders are deleted once used.

Build order: 51a, 51b, 51c, 51d, 51g, 51e-0, 51e, 51f (51g was added after the first draft, so its letter is out of
order).

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

### 51e-0: ribbon distance mock-up (user tuning step, D7)

1. Render ribbons at **50, 20 and 10 km** (and close up) at 1920x1080, scratchpad only, and show the user, so `R` and
   the pixel thresholds (cone 0.5-1 px, ribbon 6-8 px start values) are chosen from pictures. Record the answers in the
   SDD; delete the mock-ups afterwards.

### 51e: ribbons

1. History ring buffers (24 samples, 1 s, Q24) and the ribbon selection wired to `NS_SOLThrusterTrails`; counts from
   `QualityPreset` (Q12: 0 / 8 / 16 / 32 / no limit within `R`, the player except at Low), Cinematic with no cap at all (Q29, frame-spike risk recorded);
   pixel-size LOD.
2. Screenshots: the player turning, an NPC flipping close by (the ribbon must curve, not break).

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
| `SOLTest.Thruster.GasBursts` | 51g | extend with `BurstLengthM` and `PlumeAllowed` (Q27, Q32) |

## Performance gates (SDD 51 Q21)

- NPC redraw CPU delta at most 0.10 ms (Earth spawn view, 1M NPCs, default budgets).
- Plume GPU at most 0.20 ms at 1920x1080 (busy lane view, player boost view), bursts and the player light included.
- Ribbons at most 0.05 ms CPU (at the default Epic count; Cinematic uncapped, recorded only).
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
