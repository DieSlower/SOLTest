<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 50: Lane traffic flow

SDD: [`SDDs/50-lane-traffic-flow.md`](../SDDs/50-lane-traffic-flow.md). Issue
[#50](https://github.com/DieSlower/SOLTest/issues/50).

**Grill-me complete (2026-11-13):** decisions D1-D12 are in SDD section 2; the user answered Q1-Q8 the same day and they
are folded in as D13-D20. **Q9 was answered by the user on 2026-11-17 (SDD D22):** the override is the "War power move"
entry of the round command menu, per command only, a straight path ignoring lane rules at maximum thrust, nothing
saved. No open question remains.

**Amendment 1 (2026-11-13, D21, SDD section 14):** the user decided that the about 60 s hull flip finishes before
braking starts. Every burning leg gets a zero-thrust window `T_f` = `NPC_LANE_FLIP_S` centred on its midpoint in time;
the leg peak, the thrust state (new `Flip` kind) and the hull facing change. Step 5b reworks the committed 50a math
for it before step 8; the 50a and 50b API contracts below carry the amendment.

**Persistence: none.** `SOL::Settings::SAVE_VERSION` stays 13, `KEY_BINDING_LAYOUT_VERSION` stays 10, no setting, key
binding or `UPROPERTY` of a save class changes (D11). If the work shows that any of these is needed, **stop and
report**; do not choose.

## Commit parts (five, in this order)

Convention `Issue #50: Part 50x - <description>`. Each part is committed and pushed on `master` only once its tests
pass, its smokes pass and its adversarial performance and style review's findings are fixed (CLAUDE.md "Git"). Each
part fits one subagent under about 35 % context; a part that runs over continues with a fresh agent from the resume
notes here.

1. **50a - traffic math** (steps 2-5): `Traffic/SOLLaneTrafficMath` (pure: radial factor, leg profile, leg arc, mean
   field, metric map and inverse, cycle, lane ship position, lane profile, wake bound, thrust state), the lane basis
   moved to `Traffic/` with the `SOLNpcTunnel::TunnelBasis` forwarding name, the `NPC_LANE_*` traffic constants, the
   end margin 1 -> 3 (D14, moved here from 50b by the main session because the 50a tests need it). Nothing else in
   the game changes yet (the end rings sit 3 tunnel radii above the surface).
2. **50b - keep-right rails live** (steps 6-10): spawn counting pass, dimensionless `FSpawn` lane fields, the lane rail
   layout, `FromSpawn` / `EvaluateAtTimeM` / `AdvancedToEpoch` / `BuildFrame`, the group table and the 6-row group
   texture, `Shaders/Private/SOLNpcTraffic.ush` and the `M_SOLNpcPoints` rebuild (CPU and GPU switch together, so no
   seam is ever committed), the wake bound and capsule, the tunnel radius from the frozen wall radius, the label count
   (D15), the hull facing (D17), `-SOLSmokeNpcTraffic` V1-V5. May take two consecutive agents
   (C++ first, then the shader and smoke), committed once.
3. **50c - merge readout** (steps 11-13): `Traffic/SOLLaneTrafficSubsystem` (lane table), `MergeReadout`, the label's
   fourth line and the D18 fallback, the HUD draw, `SOLMenuText` tokens, the Info text and the glossary entry, the player
   guide, smoke V6.
4. **50d - AUTO lane rules** (steps 14-16): the Q9 interview is done (D22: the "War power move" menu entry, per command, nothing saved, no schema
   change); the part adds that menu entry, the per-command flag and the max-thrust behaviour. Then `FindLaneLeg`, `LaneMergeTarget`, `LaneHoldGuidance` in `USOLShipFlightProcessor` for the
   body autopilot, route waypoints and Chase Down (lane rules on by default, D19, a path counts only with 20+ wall radii
   inside the halo, D20), the War power move override, the ETA, the `LANE` marker token, the Info and glossary text, `GAME_MECHANICS.md` rows, smoke V7.
5. **50e - tuning, profiling, docs** (steps 17-22): screenshots and tuning, `-SOLSmokeNpcTrafficProfile` against the
   pre-#50 baseline (gates), every doc, the README gallery, the whole-ticket review; closes #50.

## Files to change

| File | Change | Part |
|---|---|---|
| `Source/SOLTest/SOLConstants.h` | `NPC_LANE_WALL_SPEED_MPS`, `NPC_LANE_PEAK_ACCEL_G`, `NPC_LANE_SPEED_CAP_MPS`, effort min / max / mean, halo fraction / outer radius / kappa / ramp, U-turn depth, metric zone, merge thresholds and direction minimum, AUTO track / overlap / lateral gain and cap, `NPC_GROUP_TEXTURE_ROWS`, `NPC_LANE_FLIP_S` (60, D17), the end margin 3 (D14), the smoke flags, the readout colours if not already HUD palette constants | 50a (math), 50b-50d (rest) |
| `Source/SOLTest/Traffic/SOLLaneTrafficMath.h` / `.cpp` (new) | Pure traffic math (SDD sections 4.2-4.5, 4.8, 4.9, 4.12) | 50a, 50c, 50d |
| `Source/SOLTest/Traffic/SOLLaneTrafficSubsystem.h` / `.cpp` (new) | World subsystem holding the population's lane table (written at spawn, cleared at despawn) | 50c (50b if the rails need it first) |
| `Source/SOLTest/Npc/SOLNpcTunnelMath.h` / `.cpp` | `TunnelBasis` forwards to `SOLLaneTraffic::LaneBasis` (50a); `BuildTunnelLaneData` takes the frozen wall radius (50b) | 50a, 50b |
| `Source/SOLTest/Npc/SOLNpcScaleMath.h` / `.cpp` | `FSpawn` lane fields dimensionless, `MakeSpawn` lane branch (halo stream), `FLayout` speed fields removed, `TriangleWave01` / `LaneParamAt` deleted, `LaneShipSpeedBoundMps` new inputs | 50b |
| `Source/SOLTest/Npc/SOLNpcRail.h` / `.cpp` | Lane rail layout (P0-P6), `FSOLNpcGroupTable` lane profiles, `FSOLNpcRailFrame` per-lane basis and length, `FromSpawn`, `EvaluateAtTimeM`, `AdvancedToEpoch`, `GroupDistanceM` (halo capsule) | 50b |
| `Source/SOLTest/Npc/SOLNpcSubsystem.*` | Counting pass, lane profiles, rails, wake bound, lane table publish | 50b, 50c |
| `Source/SOLTest/Npc/SOLNpcLaneMath.*` | `InWallShipCount`, `WallRadiusM` on `FSOLNpcLaneEntry`; `AccumulateLaneShips` in-wall count | 50b |
| `Source/SOLTest/Npc/SOLNpcVisuals.*`, `SOLNpcTunnelComponent.*` | Group texture rows 2-5; tunnel radius from `WallRadiusM`; label count (D15) | 50b |
| `Source/SOLTest/Npc/SOLNpcTierProcessor.*` | Hull facing per D17 / D21 (`SOLNpcRail::LaneHullForward`: nose first, smoothstep flip in the mid-leg window, engine first while braking, brake attitude held through the U-turn); the per-group Mid margin (`GroupMidMarginM`, the lane hand-over margin) on the promote / keep and evaluate keep radii | 50b |
| `Source/SOLTest/Universe/SOLSimClockSubsystem.cpp` | `-SOLSmokeNpcTunnel` starts at `SMOKE_NPC_TUNNEL_SIM_DATE` unless `-SOLSimDate=` is given (SDD 49 D19 views deterministic) | 50b |
| `Shaders/Private/SOLNpcTraffic.ush` (new), `Tools/Npc/create_npc_assets.py`, `Content/SOL/Npc/M_SOLNpcPoints.uasset` | HLSL mirror of the lane rail; the point vertex node's lane branch calls it with the extra group rows and `w`; rebuilt with `-SOLRebuild`, editor closed (restore any byte drift of the other NPC assets) | 50b |
| `Source/SOLTest/Npc/SOLNpcTrafficSmoke.*` (new), smoke wiring | `-SOLSmokeNpcTraffic` V1-V7, later `-SOLSmokeNpcTrafficProfile` | 50b-50e |
| `Source/SOLTest/UI/SOLLaneTunnelLabel.*`, `UI/SOLFlightHud.*` | Readout line in the label, fallback (D18), draw and colours | 50c |
| `Source/SOLTest/Menu/SOLMenuText.h`, Help glossary table | `LANE_TRAFFIC_*` tokens, Info wording, glossary entry, `LANE` marker token | 50c, 50d |
| `Source/SOLTest/Ship/SOLShipFlightProcessor.cpp`, `Ship/SOLShipFragments.h` | Lane leg per frame, merge / hold / leave per substep, transient lane fields (not saved) | 50d |
| `Source/SOLTest/UI/SOLFlightHud.*` (AUTO marker), ETA | `LANE` while holding; the ETA counts the lane leg | 50d |
| Tests: `NpcTrafficMathTest.cpp` (new), `NpcScaleMathTest.cpp`, `NpcRailTest.cpp`, `NpcLaneMathTest.cpp`, `NpcTunnelMathTest.cpp`, `LaneTunnelLabelTest.cpp`, the HUD bus test, InfoText, PlayerGuide, glossary | SDD section 6 cases 1-16 and section 7 re-validation | 50a-50d |
| `Docs/GAME_MECHANICS.md` | Lane traffic row, tunnel row note, Autopilot / Chase notes, readout HUD row | 50e |
| `Docs/ARCHITECTURE.md` | `Traffic/` folder and edges, `Npc/`, `Ship/`, `UI/` rows, flags | 50a (folder, edges), 50b-50e |
| `Docs/SDDs/11-*.md`, `38-*.md`, `49-*.md` | Amendment notes (SDD section 5) | 50b, 50e |
| `Docs/PLAYER_GUIDE.md`, `Docs/Images/` | Regenerated with `Tools\WritePlayerGuide.bat` | 50c |
| `Docs/TECH_DEBT.md` | New debt found (SDD section 3 items not re-reported) | 50e |
| `Docs/Plans/STATUS.md` | #50 in progress (this draft), Done with results at the end | 1, 50e |
| `README.md`, `Docs/PROJECT_METRICS.md`, `Docs/Images/` | `Tools\UpdateMetrics.bat` before each commit and at pauses; 2-3 gallery shots | 50a-50e |

## Steps

| Step | What | Who (model / effort) | Status |
|---|---|---|---|
| 0 | Grill-me (D1-D12) | main session | Done (2026-11-13) |
| 1 | SDD, this plan, STATUS #50 in progress and #51 added | opus / high | Done (draft, 2026-11-13) |
| 1b | Q1-Q8 put to the user; answers folded into the SDD (D13-D20, contract cases 1, 8, 11, 13, 14), this plan and STATUS; Q9 added | main session, then opus / high | Done (2026-11-13) |
| **50a** | | | |
| 2 | TDD: SDD section 6 cases 1-8, 11, 12, 15 written from the contract only (no implementation shown), in `NpcTrafficMathTest.cpp` (17 tests) | opus / high | Done (2026-11-13) |
| 3 | Red run: skipped, as in 49a (the test file cannot compile without the `SOLLaneTraffic` API it names) | sonnet / medium | Skipped (2026-11-13) |
| 4 | Implement `Traffic/SOLLaneTrafficMath`, the basis move with the forwarding name (`FSOLNpcTunnelBasis` now an alias of `FSOLLaneBasis`), the constants, the end margin 3 (D14, `NpcTunnelMathTest` constant case updated); topic tests and the full suite green | opus / high | Done (2026-11-13): `SOLTest.NpcTrafficMath` 17 / 17, `SOLTest.Npc` 189 / 189, full suite 1920 / 1920 |
| 5 | Adversarial performance and style review of 50a (fresh opus / xhigh, report only; brief: changed files, SDD 50, style guide, performance guidelines, the SDD section 3 debt list); fixes; re-check against SDD 50; full suite; `Tools\UpdateMetrics.bat`; commit and push 50a | opus / xhigh, then opus / high | In progress (2026-11-13): review done, findings 1-9 fixed (see the change log), `SOLTest.NpcTrafficMath` 20 / 20; next: metrics, commit and push |
| 5b | **D21 rework of 50a** (SDD section 14): TDD by a separate author from SDD cases 1, 3, 4, 11, 15 and 15c only (`LegProfile`, `LegArc`, `ThrustState`, `Constants`, `LaneProfile` changed, new `HullFlip`; the other `NpcTrafficMath` tests re-validated unchanged); then `MakeLegProfile(..., flipS)` with the cancellation-free `dv`, `ESOLLaneThrust::Flip`, `FSOLLaneThrustState::FlipFraction` / `HullForward`, `LaneHullFlipFraction`, `LaneHullForward`; `SOLTest.NpcTrafficMath` and the full suite green; review (fresh opus / xhigh); metrics; commit and push as `Issue #50: Part 50a2 - hull flip before braking (D21)` | opus / high, review opus / xhigh | In progress (2026-11-13): tests written (21 `NpcTrafficMath` tests, notes in the 50a API contract); implementation done (uncommitted, see the change log), 20 / 21 green, `ThrustState` red on a test-side count (change log); next: test fix by the test author, review, commit (with 50b, since the working tree now holds both) |
| **50b** | | | |
| 6 | Code check first: every reader of the removed `FSpawn` / `FLayout` lane fields and of `TriangleWave01` / `LaneParamAt`; the hull heading path (`HullForward`, `HandoffOrientation`); the group texture readers (`M_SOLNpcLanes`, `M_SOLNpcTunnel`, points); the label count path. If a decision is not viable, stop and report | opus / high | To do |
| 7 | TDD: section 6 cases 9 and 10 (spawn statistics, rail packing, epoch, the GPU float mirror) and the section 7 re-validation of `NpcScaleMathTest`, `NpcRailTest`, `NpcLaneMathTest`, `NpcTunnelMathTest`, `LaneTunnelLabelTest` (count) from the contract only | opus / high | Done (2026-11-13): 18 new tests, 7 updated, 3 deleted; API in "Part 50b API contract" below; hull facing (D17) left open |
| 7b | D21: add `SOLTest.NpcRail.LaneHullForward` (SDD case 10) from the contract only; no existing 50b assertion changes (SDD section 14) | opus / high | Done (2026-11-13): test written (notes in the 50b API contract) |
| 8 | Implement the spawn pass, rails, frame, group table and texture rows, wake bound and capsule, tunnel radius, label count, end margin (D14), hull facing (D17 / D21, `SOLNpcRail::LaneHullForward` in the tiers); tests and the full suite green | opus / high | Done, uncommitted (2026-11-14, 50b review fixes): CPU side, tier / Full-AI hull facing and the review fixes are in. The tier / Full-AI hull heading fix: a Full lane ship takes its nose from the rail hull heading `SOLNpcRail::HullHeadingVelocityMps` instead of the rail velocity, so promotion and demotion no longer turn or snap the hull 180 degrees; `CollectChunk` and the Full gather derive the lane leg and cycle once per ship per frame through a new combined motion function. Dead code removed: `SOLNpcScale::LaneBasis` / `LanePositionM` and their tests |
| 9 | `SOLNpcTraffic.ush`, the point vertex node, `M_SOLNpcPoints` rebuilt (editor closed); `-SOLSmokeNpcTraffic` V1-V5 PASS at 1280x720 and 1920x1080; `-SOLSmokeNpcLanes` 35 / 35, `-SOLSmokeNpcTunnel` 39 / 39 (updated checks), the 1M ramp PASS | opus / high | Built, smokes pending a final run (2026-11-14, 50b review fixes): `Shaders/Private/SOLNpcTraffic.ush` rewritten for double-float (hi / lo) precision (SDD 50 sections 4.4 and 4.6), `M_SOLNpcPoints` rebuilt by `create_npc_assets.py -SOLRebuild` (the wall speed is now passed as data in group row 2 `.w`, not a baked literal), `-SOLSmokeNpcTraffic` gained a Full-tier hull check |
| 10 | Review of 50b (fresh opus / xhigh, report only, brief plus the `.ush` and material); fixes; re-check against SDD; full suite; metrics; commit and push 50b | opus / xhigh, then opus / high | To do |
| **50c** | | | |
| 11 | TDD: case 13 (merge readout), the label's fourth line, case 16 (text) from the contract only | opus / high | Done (2026-11-16): `LaneMergeReadoutTest.cpp` (13 tests, API contract in its header), `Glossary.LaneTrafficReadoutEntry`, `InfoText.LaneTrafficReadout` |
| 12 | Lane table subsystem, `MergeReadout`, label line and fallback (D18), HUD draw, tokens, Info text, glossary entry, player guide regenerated; tests and suite green; smoke V6 PASS; `-SOLSmokeHud`, `-SOLSmokeSolidMenu` PASS | opus / high | Done, uncommitted (2026-11-17, readout placement reworked per the user's D18 decision, see the change log): after the 2026-11-17 review fixes full suite 1964 / 1964; `-SOLSmokeLaneMerge` 15 / 15, `-SOLSmokeNpcTunnel` 39 / 39, `-SOLSmokeNpcLanes` 35 / 35, `-SOLSmokeNpcTraffic` 19 / 19, `-SOLSmokeHud` 45 / 45 at 1280x720, 1920x1080 and 2560x1440 (4K proxy); Q10 resolved by the user 2026-11-17 (slide sideways, else the fallback line) |
| 13 | Review of 50c; fixes; full suite; metrics; commit and push 50c | opus / xhigh, then opus / high | To do |
| **50d** | | | |
| 13b | Interview the user on Q9 (how AUTO is told to fly around lanes or ignore the safe space); fold the answer into the SDD (decision, contract case 14) | main session | Done (2026-11-17): D22, the "War power move" round-menu entry, per command, straight path ignoring lane rules at maximum thrust, nothing saved |
| 13c | Menu entry and flag path: new `ESOLAction` row (SDD 4.9.1) with its `SOLMenuText` label, `Execute` passing a per-command ignore-lanes flag through `USOLShipSubsystem::EngageAutopilot` into `FSOLShipControlFragment` (reset by every other `Engage*`), the flight processor skipping lane logic and flying at maximum thrust when set; Info and glossary text, `GAME_MECHANICS.md` rows and the in-game Controls / Info screen; no save change; optional debug console flag | opus / high | Done, uncommitted (2026-11-17): `ESOLAction::MoveToWarPower` ("War Power Move"), `EngageAutopilot(index, bWarPower)`, `Ship/SOLAutopilotOrder` (every order write resets `bAutopilotIgnoreLanes`), `SOLFlight::WarPowerAutopilotStep`, Info and glossary text, `GAME_MECHANICS.md`; no save change; smoke `-SOLSmokeAutoLanes` V7 on Earth - Mars (see step 15) |
| 14 | TDD: case 14 (AUTO rules, the War power move override per D22 (flag per command, reset by the next order, max thrust, not saved), the action-table row, and the regression that AUTO without a lane leg is bit-identical) from the contract only | opus / high | Done, uncommitted (2026-11-17): `LaneAutoRulesTest.cpp`, `WarPowerMoveTest.cpp`, `AutopilotRegressionTest.cpp` (`NoLaneBitIdentical` passes), action-table and text tests extended |
| 15 | AUTO lane rules in the flight processor (body, route, chase), the Q9 override, ETA, `LANE` marker; tests and suite green; smoke V7 PASS; the route and chase smokes PASS | opus / high | Done, uncommitted (2026-11-17): `Traffic/SOLLaneTrafficMath` (`FindLaneLeg`, `FindAutoLaneLeg`, `LaneTrackOffsetM`, `LaneMergeTarget`, `IsLaneMerged`, `LaneAutoModeAt`, `IsLaneMarkerShown`, `LaneHoldGuidance`, `LaneLegTimeS`, `LaneAutoEtaS`), `SOLFlight::LaneHoldStep`, the processor's latch / approach / hold / leave, ETA, `LANE` marker; `-SOLSmokeAutoLanes` V7 on Earth - Mars (11 / 11 at 1280x720, 1920x1080 and 2560x1440 (the 4K proxy, screenshots confirmed at 2560x1440); with it, 2026-11-17: full suite 1995 / 1995, `-SOLSmokeLaneMerge` 15 / 15, `-SOLSmokeNpcTunnel` 39 / 39, `-SOLSmokeNpcLanes` 35 / 35, `-SOLSmokeNpcTraffic` 19 / 19, `-SOLSmokeHud` 45 / 45 at the same three sizes, `-SOLSmokeRoute` 26 / 26, `-SOLSmokeChase` 26 / 26 and `-SOLSmokeInteract` 18 / 18 at 1920x1080 (no NPC population, so lane rules on routes and chases are covered by unit tests only), `PlayerGuide.UpToDate` after `WritePlayerGuide -NoCrops`, `GameUserSettings.ini` unchanged): crossing path finds no leg, approach from 2.2 wall radii merges on the right at rho 0.254 within 4.8 % after about 53,000 s sim with the LANE tag, hold from the wrong half moves to rho 0.250 (ETA 11.4 days vs 2.4 straight), the whole leg under 86400x held within 0.4 % and left at Mars's end ring (16.1 days sim), War power from inside the lane: no lane mode, no tag, peak 5.9 G, lowest meter 0.303, no blackout; the route and chase paths unchanged in code |
| 16 | Review of 50d; fixes; full suite; metrics; commit and push 50d | opus / xhigh, then opus / high | Review-fix round 2 done, uncommitted (2026-11-17): mid-lane exit fix (mode `Left`, `LaneLeaveDistanceM`), frame-start lane views (`MakeMergeLane`) and latch re-validation (`CheckLaneLatch`, `LaneAutoDecide`, `FindAutoLaneLeg` removed), lane ETA for every AUTO kind and chase `Lane` activity, pulsed War power (6.80 G, band 0.4 - 0.6, floor 0.3, boost always on), Move To and War Power Move never block, HUD BOOST and WAR POWER activity, smoke V7 extended (mid-lane stop view, side and speed checks, War power ETA); Verification 2026-11-17 (round 2): full suite 2015 / 2015; -SOLSmokeAutoLanes 13 / 13 at 1280x720, 1920x1080 and 2560x1440 (merge 0.25 wall radii on the right, HUD readout MATCHED; whole leg held within 0.02 %, left at 0.982 of the leg, arrived after 16.13 days; mid-lane stop left 3.2e9 m before the waypoint at 261 km/s, braking needs 2.57e9 m, no re-entry, no overshoot; War power peak 6.80 G, lowest meter 0.400, arrived in 47.8 h against plain AUTO straight 51.7 h), LaneMerge 15 / 15, NpcTunnel 39 / 39, NpcLanes 35 / 35, NpcTraffic 19 / 19, Hud 45 / 45, Route 26 / 26, Chase 26 / 26, Interact 18 / 18 at the same three sizes, HoloMenu 86 / 86 at 1280x720 (10-row radial and list menus checked by eye), PlayerGuide.UpToDate after WritePlayerGuide -NoCrops, GameUserSettings.ini MD5 unchanged. To do: final suite, metrics, commit and push 50d |
| **50e** | | | |
| 17 | Screenshots at 1280x720, 1920x1080, 3840x2160, HUD glow 0 / 100 / 200 %: flow, halo, U-turn, readout colours, AUTO in a lane; tuning (halo kappa, U-turn depth, flip time, readout colours) with the main session showing shots to the user where a value needs the user's eye; final values in `SOLConstants.h` and an as-built section of the SDD; candidate shots deleted | opus / high | Partly done (2026-11-17, 50e agent): README gallery shots `Docs/Images/50-lane-traffic-flow.png`, `50-lane-merge-readout.png`, `50-lane-auto-hold.png` (1920x1080, main tree); flip-axis and U-turn-attitude strips shot in a scratch worktree (SDD section 15) for the user, open as SDD Q11; no constant changed. Not done: the 1280x720 / 3840x2160 / HUD glow 0-200 % sweep and the halo, U-turn depth and readout colour tuning passes |
| 18 | Profiling (SDD section 8 item 6): build and run the pre-#50 baseline (`f336e10`) and #50 in one session, T2, T5 and a far view, three runs each, 1920x1080 gates (points draw <= +0.10 ms GPU, NPC update <= +0.10 ms CPU, readout and AUTO rules <= 0.01 ms CPU each), 3840x2160 and 3840x3744 recorded; the 1M ramp; results in the SDD | opus / high | Done (2026-11-17, SDD section 15): baseline `f336e10` in a scratch worktree, `-SOLSmokeNpcTunnelProfile` on both trees (the planned `-SOLSmokeNpcTrafficProfile` was never built; no far view), 3 interleaved runs at 1080p: points draw worst +0.071 ms GPU (T5-all) PASS, NPC update -0.04 to -0.06 ms PASS, readout about 0.005-0.008 ms per frame PASS, AUTO lane rules not isolated (flight step 0.042 ms average under AUTO at warp, upper bound only); wake cost at 1x: 5 -> 7 candidate groups at T5, no measurable time (not measured under warp); spawn: largest slice 24.3 -> 26.9 ms, rail fill 2.91 -> 4.20 ms, re-epoch under warp 9.85 ms median hitch (fill 3.08 ms) at most every 2 s; 2560x1440 and 3840x3744 recorded (one run each); 1M ramp PASS in both (HEAD p95 7.38 ms) |
| 19 | PIE check via unreal-mcp if the editor is open (else noted here) | sonnet / medium | Not run (2026-11-17): unreal-mcp was not connected and the editor was closed for the standalone profile and smoke runs; the `-game` smokes and their screenshots cover the views |
| 20 | Docs: `GAME_MECHANICS.md`, `ARCHITECTURE.md`, SDD 11 / 38 / 49 notes, `TECH_DEBT.md`, README gallery (2-3 shots), in-game text truth check against the constants | sonnet / medium | To do |
| 21 | Whole-ticket adversarial review (fresh opus / xhigh); fixes; full suite; metrics | opus / xhigh, then opus / high | To do |
| 22 | STATUS #50 Done; commit and push 50e; close #50 | sonnet / low | To do |

## Test contract summary

The full contract is SDD section 6 (cases 1-16). In short: constants (1); the radial factor, parabola inside and
ramped halo (2); the per-ship leg profile with the cap blend and the `kappa -> 0` limit (3); the leg arc, symmetric,
`w` at both ends, acceleration bounded (4); the mean speed field (5); the metric end-zone map and its inverse (6); the
cycle and phase wrap (7); the lane ship position: keep right, constant lateral radius, continuous joints, U-turn depth,
planet clearance, end rings at any length (8); spawn statistics and the battle / cluster regression (9); rail packing,
epoch and the GPU float mirror bound (10); the lane profile (11); the wake bound (12); the merge readout (13); the AUTO
rules with the no-lane regression (14); the thrust state (15); text (16).

## Notes

- Brief every subagent with: SDD 50, this plan, the files above, the style guide, "delete temporary screenshots and
  captures once used", "no AI attribution in commits or code comments" and "no persistence change: stop and report if
  one seems needed".
- `SOLNpcTraffic.ush` mirrors `Traffic/SOLLaneTrafficMath.cpp` and the lane branch of `SOLNpcRail::EvaluateAtTimeM`; a
  change to one changes the others (file headers say so).
- The CPU and GPU lane evaluations switch in the same part (50b); never commit one without the other.
- The profile baseline is the pre-#50 commit built in the same session (no fallback cvar keeps the old rails).
- **50b notes from the 50a review (decide in step 6, test in step 7 and V5):**
  - **Phase precision over time.** `frac(phi + t / T_cyc)` in a single float only changes in steps of `ulp x T_cyc`:
    about 0.16 s on an axis ship's cycle up to about 1.3 s on a halo ship's long cycle, a visible stutter (the
    position error bound of SDD section 4.6 is only half the story). Use the freed **P5** either as a second,
    low-order phase float (`phi = P0 + P5`, the epoch re-base splitting the double phase into the two) or to store the
    seconds from the nearest leg joint at the epoch, so the shader's time into the cycle keeps sub-millisecond steps.
  - **One phase formula** for the CPU (`CycleSegmentAt`, now `fmod(phi T_cyc + t, T_cyc)`) and the shader (`frac(phi
    + t / T_cyc) T_cyc` as the SDD writes it): pick one and make both use it, so the float mirror test compares like
    with like.
  - **V5 must test smoothness over time**, not only the CPU / GPU hand-over: a ship's float-mirror position sampled at
    the frame rate over a few seconds moves by `v dt` every frame (no repeated positions, no steps).
  - **Shader cost (reviewer's advice):** upload per lane `1 / Z`, `(l - l0) / (l0 - Z)` and `pi / T_U` instead of
    dividing per vertex; compute the U-turn `sincos` for every ship and select the result instead of branching on the
    segment (the divergence of the rare turn branch costs more than the ALU it saves). On the CPU side, fold
    `FSOLLaneFrame::EndShiftM` (0 on every real lane) into the uploaded B position.
  - **`T_leg` is derived, not stored:** `t_c = (l0 - 2 t_a (w + dv / 2)) / (w + dv)` (one divide per ship, `l0` from
    the group row), split at `T_leg / 2`, mirroring `SOLLaneTraffic::LaneShipLegS` and the nearer-end-in-time leg
    evaluation (SDD section 4.4).
- Pause points per CLAUDE.md (context above 35 %, usage limits, 70 % weekly): save resume state here (the step, files
  mid-edit, next action) and run `Tools\UpdateMetrics.bat`.

## Part 50a API contract (from the test author)

Written with the tests (step 2), from SDD section 6 only. The implementer follows these names and rules exactly;
where the SDD left a choice open, the choice made here is marked *(chosen)*.

**Test file:** `Source/SOLTest/Tests/NpcTrafficMathTest.cpp`, paths `SOLTest.NpcTrafficMath.*` (17 tests: `Constants`,
`EndMarginD14`, `RadialFactor`, `LegProfile`, `LegArc`, `MeanSpeed`, `MetricMap`, `Cycle`, `LaneProfile`, `LaneFrame`,
`KeepRight`, `UTurnJoints`, `UTurnPlanetClearance`, `WakeBound`, `ThrustState`, `EdgeCases`, `Precision`). The SDD's
`LaneTrafficMathTest.cpp` / `SOLTest.LaneTrafficMath` name is replaced by this one (the brief named it), so step 3
filters `SOLTest.NpcTrafficMath`.

**Header** `Source/SOLTest/Traffic/SOLLaneTrafficMath.h`, functions in `namespace SOLLaneTraffic` (`SOLTEST_API`), plain
structs at global scope:

- `struct FSOLLaneBasis { FVector3d Axis, Up, Right; bool bValid; }`; `FSOLLaneBasis LaneBasis(const FVector3d& aM,
  const FVector3d& bM)`, which must equal `SOLNpcTunnel::TunnelBasis` field by field (1e-15). `TunnelBasis` forwards to
  it; `FSOLNpcTunnelBasis` may become an alias of `FSOLLaneBasis` (the test reads only the fields).
- `struct FSOLLaneLeg { double Alpha, PeakMps, DvMps, AccelS, AccelM, CoastS, LegS; bool bValid; }`.
- `double RadialFactor(double rho)`: `|rho|`, NaN gives 0, `kappa_h` for every `rho >= 1 + r_h` (also beyond `H`).
- `FSOLLaneLeg MakeLegProfile(double kappa, double effort, double legM)`: kappa clamped to [0, 1] (NaN gives 0), effort
  clamped to [`EFFORT_MIN`, `EFFORT_MAX`] (NaN gives `EFFORT_MIN` *(chosen)*); `legM <= 0`, NaN or infinite gives
  `bValid` false with all fields 0. `Alpha = kappa x effort x a` even when the cap binds. **`DvMps` must be computed
  without cancellation**, e.g. `(v_p^2 - w^2) / (v_p + w)` with `v_p^2 - w^2 = min(alpha l0, kappa (c^2 - w^2))`: the
  `kappa -> 0` test needs `LegS(1e-9) <= LegS(0)` to 1e-12. `CoastS` clamped to >= 0.
- `double LegArcM(const FSOLLaneLeg&, double legM, double tl)`: `tl` clamped to [0, `LegS`], NaN gives 0; an invalid leg
  gives 0. `double LegSpeedMps(const FSOLLaneLeg&, double legM, double tl)` (speed at leg time; an invalid leg gives
  `w`) and `double LegSpeedAtArcMps(const FSOLLaneLeg&, double legM, double u)` = `sqrt(min(w^2 + 2 Alpha min(u, l0 -
  u), PeakMps^2))` *(chosen: the SDD's "at the same u" needed a speed-by-arc function)*.
- `double MeanSpeedMps(double u, double rho, double legM)` = `LegSpeedAtArcMps(MakeLegProfile(RadialFactor(rho),
  EFFORT_MEAN, legM), legM, u)`; `u` clamped to [0, l0] (NaN as 0); a bad leg gives `w`.
- `double MetricZoneM(double l0)` = `min(METRIC_ZONE_FRACTION x l0, l0 / 2)`, 0 for a bad `l0`. `double MetricMapM(double
  u, double l0, double l)` (X), `double MetricMapSlope(double u, double l0, double l)` (X', *(chosen)*: the readout and
  the leg speed need it), `double MetricMapInverseM(double x, double l0, double l)` (Newton, round trip 1e-9). `u`
  clamped to [0, l0], `x` to [0, l]; a non-positive or non-finite `l0`, or a non-finite `l`, gives 0.
- `enum class ESOLLaneSegment : uint8 { Outbound, TurnAtB, Inbound, TurnAtA }`; `struct FSOLLaneCycle { double LegS,
  TurnS, CycleS; }`; `FSOLLaneCycle MakeCycle(double legS, double uTurnDepthM)` (`TurnS = pi D_U / w`, `CycleS = 2 LegS
  + 2 TurnS`); `struct FSOLLaneCyclePoint { ESOLLaneSegment Segment; double Phase, CycleTimeS, SegmentTimeS; }`;
  `FSOLLaneCyclePoint CycleSegmentAt(const FSOLLaneCycle&, double phase, double timeS)`: `Phase = frac(phase + t /
  CycleS)` in [0, 1) (input phase wrapped, NaN `t` read as 0), `CycleTimeS = Phase x CycleS`, segments half-open
  `[0, LegS)`, `[LegS, LegS + TurnS)`, `[LegS + TurnS, 2 LegS + TurnS)`, `[2 LegS + TurnS, CycleS)`.
- `struct FSOLLaneProfile { double WallRadiusM, HaloOuterRadiusM, NominalLengthM, NominalLegM, EndArcAM, EndArcBM,
  UTurnDepthM, UTurnS, MetricZoneM; bool bDrivable; }`; `FSOLLaneProfile MakeLaneProfile(double wallRadiusM, double
  nominalLengthM, double bodyRadiusAM, double bodyRadiusBM)` *(amended by the 50a review: was `laneRadiusM,
  inWallCount`; the caller passes `SOLNpcTunnel::TunnelRadiusM(R, inWallCount)`)*. `EndArcBM` = `R_B + m Rw` (distance
  before B). Not drivable when `Rw <= 0`, `L0` non-finite, or the nominal leg `<= 2 Z` (in practice `<= 0`).
- `struct FSOLLaneFrame { FVector3d AM, BM, Axis, Up, Right; double CurrentLegM; bool bValid; }`; `FSOLLaneFrame
  MakeLaneFrame(const FSOLLaneProfile&, const FVector3d& aM, const FVector3d& bM)`: the basis from `LaneBasis`,
  `CurrentLegM = |B - A| - EndArcAM - EndArcBM`; invalid for A == B.
- `struct FSOLLaneShip { double Phase, DvMps, UpOffsetM, RightOffsetM, AccelS; }` (the rail's P0-P4; *amended by the
  50a review:* `LegS` removed, `double LaneShipLegS(const FSOLLaneProfile&, const FSOLLaneShip&)` derives it, and
  `FSOLLaneFrame` gains `EndShiftM`);
  `FSOLLaneShip MakeLaneShip(const FSOLLaneProfile&, double phase, double effort, double rho, double clockRad)`: phase
  wrapped into [0, 1) (NaN as 0), `rho` clamped to [0, H] (NaN as 0), `clockRad` clamped to [0, pi] (NaN as pi / 2),
  leg from `MakeLegProfile(RadialFactor(rho), effort, NominalLegM)`.
- `FVector3d LaneShipPositionRelM(const FSOLLaneProfile&, const FSOLLaneFrame&, const FSOLLaneShip&, double timeS, const
  FVector3d& originM)` *(chosen: the result relative to any origin, so a ship near a body is exact relative to it; the
  test asserts 1e-6 m lateral offsets 4e11 m out, which needs the nearer-end evaluation of SDD section 4.4)* and
  `FVector3d LaneShipPositionM(...)` (origin zero). An invalid frame gives A; a non-drivable lane gives the midpoint
  disc at rest (outbound offsets).
- `double LaneShipSpeedBoundMps(double maxPeakMps, double nominalLegM, double currentLegM, double haloOuterRadiusM,
  double endpointRelativeSpeedMps)` *(chosen: in `SOLLaneTraffic`, legs not lengths; 50b's `SOLNpcScale` version may
  forward to it)*. **Note:** the SDD's "length ratio `max(1, l / l0)`" understates the map's largest slope, which is
  `(l - Z) / (l0 - Z)` for `l > l0` (2.05 at `l = 2 l0`); the brute-force test (`l / l0` 2) fails on the literal
  ratio, so use the true slope bound.
- `enum class ESOLLaneThrust : uint8 { Accelerate, Coast, Brake, Turn }`; `struct FSOLLaneThrustState {
  ESOLLaneSegment Segment; ESOLLaneThrust Thrust; double AccelMps2; FVector3d Direction; }`; `FSOLLaneThrustState
  LaneThrustAt(const FSOLLaneProfile&, const FSOLLaneFrame&, const FSOLLaneShip&, double timeS)` *(chosen: the frame is
  passed for the direction)*. Legs: `Alpha` along (Accelerate) or against (Brake) travel, 0 and Coast for `kappa` 0 or
  in the coast. Turns: `(w / D_U)^2 x |p - c|` toward the ellipse centre `c` (end ring centre plus the up offset).

**Amendment 1 (D21, SDD sections 4.3, 4.4, 4.12 and 14), applied in step 5b; supersedes the matching rules above:**

- `FSOLLaneLeg MakeLegProfile(double kappa, double effort, double legM, double flipS = SOL::NPC_LANE_FLIP_S)`: `flipS`
  (`T_f`) negative or NaN reads as 0. `DvMps = max(0, min(dv_f, dv_c))`, `dv_f = 2 alpha (l0 - w T_f) / ((2 w + alpha
  T_f) + sqrt((2 w + alpha T_f)^2 + 4 alpha (l0 - w T_f)))` (taken as 0 when `l0 <= w T_f`), `dv_c = kappa (c^2 - w^2)
  / (c_k + w)`; `PeakMps = w + DvMps`; `AccelS = DvMps / Alpha` (0 when `Alpha` = 0); `AccelM = AccelS (w + DvMps /
  2)`; `CoastS = max(0, (l0 - 2 AccelM) / PeakMps)` (`>= T_f` when `DvMps > 0`); `LegS = 2 AccelS + CoastS`. `flipS`
  0 gives the pre-D21 profile exactly. No new `FSOLLaneLeg` field (the window is derived from `LegS` and `T_f`).
- `MakeLaneShip` uses the default `flipS`; `FSOLLaneShip`, `LaneShipLegS`, `LegArcM`, `LegSpeedMps`,
  `LegSpeedAtArcMps`, `MeanSpeedMps`, the cycle, the position and `LaneShipSpeedBoundMps` keep their signatures and
  formulas (only their inputs' peaks move).
- `enum class ESOLLaneThrust : uint8 { Accelerate, Coast, Brake, Turn, Flip }` (`Flip` appended). `LaneThrustAt` on a
  leg with `AccelS > 0`: Accelerate while `tl < t_a`, Brake while `tl > T_leg - t_a`, Flip while `|tl - T_leg / 2| <
  T_f / 2`, else Coast; Flip has `AccelMps2` 0 and `Direction` the travel direction.
- `FSOLLaneThrustState` gains `double FlipFraction = 0.0` and `FVector3d HullForward = FVector3d::ZeroVector`.
- New `double LaneHullFlipFraction(double legS, double accelS, double legTimeS)`: `S(x)`, `x = (tl - (legS - T_f) / 2)
  / T_f` clamped to [0, 1], `S(x) = 3 x^2 - 2 x^3`; `tl` clamped to [0, `legS`]; 0 when `accelS <= 0`, or any input
  is not finite; with `T_f` = 0 a step at `legS / 2`.
- New `FVector3d LaneHullForward(const FSOLLaneProfile&, const FSOLLaneFrame&, const FSOLLaneShip&, double timeS)`
  (unit): legs `cos(pi f) d + sin(pi f) Up` (`d` = `+Axis` outbound, `-Axis` inbound); turns `-Axis` at B, `+Axis` at
  A when the ship's leg brakes (`AccelS > 0`), else the turn's velocity direction; an invalid frame gives `+Axis` of
  the basis (or `+X` when the basis is invalid); a non-drivable lane gives `+Axis`. *Amended 2026-11-17 (Q11a, SDD
  section 16): the signature gained `uint32 laneShipKey` after the ship (`LaneHullForward(profile, frame, ship,
  laneShipKey, timeS)`, the key being the NPC index), and on legs the hull is `cos(pi f) d + sin(pi f) T` with `T =
  LaneShipTopVector(laneShipKey, d, Up)`, the ship's own top, instead of the lane's `Up`; `LaneThrustAt` and
  `LaneShipMotion` take the same key.* *Amended 2026-11-18 (SDD 50 D24, D25, section 16; contract
  `50-hull-flip-contract.md`): `T` becomes `side x B` (`B` = `T_out` = `LaneShipTopVector(key, +Axis, Up)` outbound,
  `-T_out` inbound, `side` = `LaneFlipSide(key)`), so both flips turn in one plane; new `LaneHullBasis`,
  `HullRotation`, `LaneFlipEndAccelMps2`, `LaneFlipEndAccelLimitMps2`, `LaneFlipDurationS`.*
- **Step 5b tests (test author, from SDD cases 1, 3, 4, 11, 15 and 15c only; no implementation read).**
  `NpcTrafficMathTest.cpp` now has 21 tests: `LegProfile`, `LegArc`, `ThrustState`, `Constants` and `LaneProfile`
  changed, `EdgeCases` (determinism also compares `Thrust`, `FlipFraction`, `HullForward`) extended, new
  `SOLTest.NpcTrafficMath.HullFlip`; every other test re-validated unchanged. No API name beyond the ones above.
  Choices the tests pin *(chosen)*: the default `flipS` equals an explicit `SOL::NPC_LANE_FLIP_S` (same leg, 1e-12);
  `DvMps` equals the cancellation-free `dv_f` to 1e-9 on the Earth - Moon leg; a leg of exactly `w T_f` has no burn;
  `LaneThrustAt`'s `FlipFraction` on a leg equals `f` of SDD section 4.4 (0 before the window, 1 after it on a burning
  leg, also while coasting), in a turn 1 for a braking ship and 0 otherwise; `HullForward` equals
  `LaneHullForward(profile, frame, ship, t)` to 1e-12 in every state; Flip's `Direction` is the travel direction (Coast's
  is not asserted); `LaneHullFlipFraction` returns exactly 0 for `accelS` 0, negative, NaN or infinite and for a NaN or
  infinite `legS` or `legTimeS`; a no-brake ship's turn hull is its velocity direction (1e-6, checked only where the
  speed is at least 1 % of `w`).

**Constants** (`SOLConstants.h`, `namespace SOL`): `NPC_LANE_WALL_SPEED_MPS` 2.0e4, `NPC_LANE_PEAK_ACCEL_G` 1.65,
`NPC_LANE_PEAK_ACCEL_MPS2` (= 1.65 x 9.80665, *(chosen name)*; `SOLConstants.h` has no gravity constant today, the one
in `SOLNewtonian` is `STANDARD_GRAVITY_MPS2`), `NPC_LANE_SPEED_CAP_MPS` 3.0e5, `NPC_LANE_EFFORT_MIN` / `_MAX` / `_MEAN`
0.5 / 1.0 / 0.75, `NPC_LANE_HALO_FRACTION` 0.25, `NPC_LANE_HALO_OUTER_WALL_RADII` 2, `NPC_LANE_HALO_KAPPA` 0.01,
`NPC_LANE_HALO_RAMP_WALL_RADII` 0.25, `NPC_LANE_UTURN_DEPTH_WALL_RADII` 2, `NPC_LANE_METRIC_ZONE_FRACTION` 0.05,
`NPC_LANE_MERGE_MATCH_FRACTION` 0.05, `NPC_LANE_MERGE_AMBER_FRACTION` 0.20, `NPC_LANE_MERGE_DIRECTION_MIN_MPS` 1000,
`NPC_LANE_AUTO_TRACK_WALL_RADII` 0.25, `NPC_LANE_AUTO_MIN_OVERLAP_WALL_RADII` 20, `NPC_LANE_FLIP_S` 60.

**Part boundary to settle (main session; settled 2026-11-13: moved into 50a):** `EndMarginD14` asserts `NPC_TUNNEL_END_MARGIN_RADII` = 3 and
`UTurnPlanetClearance` needs it (with 1 the near-axis U-turn enters the planet). The plan puts D14 in 50b. Either move
the constant change (and `NpcTunnelMathTest.cpp` line 530, which asserts 1) into 50a, or accept those two tests red
until 50b. Both are isolated in their own tests so nothing else depends on it.

**SDD wording to fix:** "Z = l0 / 2 when the leg is shorter than 2 Z" (case 6) and "nominal leg below 2 Z" (section
4.2) can never apply while `Z = 0.05 l0`; the tests read them as `Z = min(0.05 l0, l0 / 2)` and "not drivable when the
nominal leg is `<= 0`". The SDD's "1e-6 m at 1 AU scale" joint tolerance is below the double ulp of absolute positions
at 1 AU (about 3e-5 m); the tests measure joints relative to the nearer body at 1e-3 m (time rounding x speed), and
use 1e-6 m only for lateral offsets.

## Part 50b API contract (from the test author)

Written with the step 7 tests from SDD sections 4.2-4.7 and 6 (cases 9, 10, 12) and the 50b notes above, without
reading any 50b code. The implementer follows these names and rules; choices the SDD left open are marked *(chosen)*.

**Test files.** `NpcRailTest.cpp` (`SOLTest.NpcRail.*`, 14 tests: `EpochMatches`, `EvaluateAtTimeMatches`,
`RailVelocity` rewritten for lane rails; `BuildFrameGroupBody` unchanged; `GpuFloatPrecision` only the new
`AdvancedToEpoch` signature; new `BuildFrameLanes`, `LanePacking`, `LaneClosedForm`, `LaneFloatContinuity`,
`LaneTrafficTexels`, `GpuLaneFloatMirror`, `GpuLaneSmoothness`, `HaloCapsule`, `LaneEdgeCases`). New
`NpcTrafficSpawnTest.cpp` (`SOLTest.NpcTrafficSpawn.*`: `CountingPass`, `WallRadiusFrozen`, `Population`, `MaxPeak`,
`WakeBound`, `EdgeCases`). `NpcScaleMathTest.cpp`: `SpawnLane` rewritten for case 9, `SpawnDeterminism` and
`DefaultLayout` updated, `TriangleWave`, `LaneParam` and `LaneSpeedBound` deleted (their targets go away). New
`NpcSpawnRegressionTest.cpp` (`SOLTest.NpcSpawnRegression.BattleClusterUnchanged`). `NpcLaneMathTest.cpp` gains
`BuildLaneFrameWall`, `NpcTunnelMathTest.cpp` gains `BuildTunnelLaneDataWall`; every other existing test in those files
is still valid unchanged. `LaneTunnelLabelTest.cpp` needs no change in 50b (the label count is `ShipCount`, which keeps
its meaning; the fourth line is 50c).

**Regression capture (step 8, before `MakeSpawn` changes):** `NpcSpawnRegressionTest.cpp` uses only fields that exist
before and after 50b. Build the pre-#50 code with only that file added, run `-Filter SOLTest.NpcSpawnRegression`, paste
the three logged tables over the zero rows and set `NPCSPAWNREG_CAPTURED` to true. It fails on purpose until then.

**`SOLConstants.h`:** `NPC_GROUP_TEXTURE_ROWS` = 6 (`NPC_GROUP_TEXTURE_WIDTH` stays 64).

**`Npc/SOLNpcScaleMath.h` (`SOLNpcScale`):**

- `FSpawn` lane fields replaced by `double LanePhase` ([0, 1)), `double LaneEffort` ([`EFFORT_MIN`, `EFFORT_MAX`]),
  `double LaneRho` (wall radii: [0, 1) inside, [1, H] in the halo), `double LaneClockRad` ([0, pi]), `bool bLaneHalo`;
  all default 0 / false (also for a lane ship with no lane). `LaneStartParam`, `LaneDirection`, `LaneSpeedMps`,
  `LaneLateralOffsetM` removed; `FLayout::LaneMinSpeedMps` / `LaneMaxSpeedMps` removed.
- `TriangleWave01`, `LaneParamAt` and `LaneShipSpeedBoundMps` *(chosen: replaced by `SOLNpcRail::LaneWakeSpeedBoundMps`)*
  deleted. `LaneBasis` and `LanePositionM` stay while they have a caller; if step 6 finds none, delete them with their
  tests (`LaneBasis`, `LanePosition`).
- `void CountLaneSpawns(uint32 seed, const FCounts&, const FLayout&, TArray<int32>& outInWall, TArray<int32>& outHalo)`:
  both reset to `layout.Lanes.Num()` zeros; one per lane ship with a lane, by `bLaneHalo`, equal to `MakeSpawn`'s.
- `void AccumulateLaneSpawns(uint32 seed, const FCounts&, const FLayout&, int32 beginNpcIndex, int32 endNpcIndex,
  TArrayView<int32> inOutInWall, TArrayView<int32> inOutHalo)`: no reset, range clamped to `[0, Total)`, empty or
  reversed ranges add nothing, non-lane indices skipped, lanes past the arrays not counted (the parallel counting pass).

**`Npc/SOLNpcRail.h` (`SOLNpcRail`):**

- Lane rail layout: P0 phase high float in [0, 1) (forced below 1 when `float(phase)` rounds up), P1 `float(dv)`, P2
  `float(rho Rw cos psi)`, P3 `float(rho Rw sin psi)` (>= 0), P4 `float(t_a)`, P5 `float(phase - P0)` (the low part,
  |P5| <= 2^-24), P6 0. Each value is rounded once from `SOLLaneTraffic::MakeLaneShip`'s double.
- `FSOLNpcGroupTable` gains `TArray<FSOLLaneProfile> LaneProfile`, `TArray<int32> LaneInWallCount`,
  `TArray<int32> LaneHaloCount`, `TArray<double> LaneMaxPeakMps` *(chosen: the profile array holds `Rw`, so no separate
  `LaneWallRadiusM`)*.
- `FSOLNpcRailFrame` gains `TArray<FSOLLaneProfile> LaneProfile` (a copy of the table's, so `EvaluateAtTimeM` needs no
  table) and `TArray<FSOLLaneFrame> LaneFrame`, one each per lane.
- `void BuildLaneProfiles(FSOLNpcGroupTable&, TConstArrayView<int32> inWallCounts, TConstArrayView<int32> haloCounts,
  TConstArrayView<double> nominalLengthsM, TConstArrayView<double> bodyRadiiAM, TConstArrayView<double> bodyRadiiBM)`:
  sizes the four arrays to `LaneCount`; `LaneProfile[i] = MakeLaneProfile(TunnelRadiusM(LaneRadiusM[i], inWall[i]),
  L0[i], R_A[i], R_B[i])` (D13; the halo count is stored only); an entry past an input's end reads 0; `LaneMaxPeakMps`
  reset to zeros.
- `FSOLNpcRail FromSpawn(const FSpawn&, int32 groupBase, const FSOLLaneProfile& laneProfile, double bodyGM)` (lane
  constants from `MakeLaneShip(profile, LanePhase, LaneEffort, LaneRho, LaneClockRad)`; NaN spawn values still give a
  finite rail; cluster and battle unchanged).
- `FSOLLaneShip LaneShipFromRail(const FSOLNpcRail&)`: Phase `double(P0) + double(P5)`, then P1-P4.
- `BuildFrame`: also fills `LaneProfile[i]` (default, not drivable, when the table has none) and `LaneFrame[i] =
  SOLLaneTraffic::MakeLaneFrame(profile, A, B)` (invalid for an unresolved body); no reallocation at the same sizes.
- `EvaluateAtTimeM` (lane) = `SOLLaneTraffic::LaneShipPositionM(frame.LaneProfile[g], frame.LaneFrame[g],
  LaneShipFromRail(rail), t)`; finite for a group with no lane frame. **Phase formula** *(chosen, CPU and shader)*:
  `tau = wrap(phase x T_cyc + t, T_cyc)`, as `CycleSegmentAt` does.
- `FSOLNpcRail AdvancedToEpoch(const FSOLNpcRail&, double deltaS, TConstArrayView<FSOLLaneProfile> laneProfiles)`: a
  lane rail's phase advances by `deltaS / T_cyc` in double (`T_cyc = MakeCycle(LaneShipLegS(profile,
  LaneShipFromRail(rail)), D_U).CycleS`), wrapped and split into P0 / P5; P1-P4, P6 kept; a non-finite delta or a group
  without a profile leaves the rail as it is; clusters and battles ignore the profiles.
- `void AccumulateLaneMaxPeak(TConstArrayView<FSOLNpcRail>, TArrayView<double> inOutMaxPeakMps)`: max with `w + P1`
  per lane rail (never lowers a value; other kinds and out-of-range groups ignored).
- `double LaneWakeSpeedBoundMps(const FSOLNpcGroupTable&, const FSOLNpcRailFrame&, int32 lane, double
  endpointRelativeSpeedMps)` = `SOLLaneTraffic::LaneShipSpeedBoundMps(LaneMaxPeakMps[lane], l0, frame.LaneFrame[lane].
  CurrentLegM, H Rw, endpoint)`, without the `NPC_WAKE_SPEED_MARGIN` (the subsystem applies it); finite and >= 0 for an
  out-of-range lane.
- `GroupDistanceM` (lane): capsule A - B of radius `LaneProfile[lane].HaloOuterRadiusM` when there is a profile, else
  `LaneRadiusM`.
- `void WriteLaneTrafficTexels(const FSOLNpcRailFrame&, TArrayView<FVector4f> groupTexels)`: row r, column c at
  `r x W + c`; writes rows 2-5 only: drivable lane = row 2 (Up, Rw), row 3 (Right, D_U), row 4 (l - l0 in double,
  e_A, l0, Z), row 5 (e_B - EndShiftM, T_U, (l - l0) / (l0 - Z), 1 / Z) *(chosen: the spare slots carry the reviewer's
  per-lane reciprocals; `EndShiftM` folded into e_B, rows 0 and 1 keep the body centres)*; valid frame but not
  drivable = rows 2-3 (basis) and zero rows 4-5 (l0 = 0 holds the ship at rest); invalid frame, cluster, battle and
  unused columns = zero rows 2-5; a buffer under 6 rows untouched; lanes past W not written.

**Shader contract (`SOLNpcTraffic.ush`):** the float mirror `NpcRailGpuLaneRelM` in `NpcRailTest.cpp` is the reference
formula: axis = cross(Up, Right); T_leg derived in float; the cycle time split into a constant part `P0 x T_cyc - k
T_cyc - segment start` and a moving part `P5 x T_cyc + t`, the segment chosen from their sum, the time into a leg taken
from the nearer end (`constant + moving` or `(length - constant) - moving`), the arc mapped from the nearer end ring with
row 5's reciprocals; U-turns as SDD section 4.4 with `sincos` of `pi x time / T_U`. A change to one changes the other.
Bound asserted per ship *(chosen)*: `2 x FLT_EPSILON x T_cyc x v_peak x max(1, (l - Z) / (l0 - Z)) + 4 x ulp(max(|A -
V|, |B - V|)) + 1 m`, viewpoint 1,000 km from the ship. Smoothness: 60 Hz for 5 s at 9,000 s into the epoch, near both
end rings and inside both U-turns, each frame step within 20 % of the CPU's plus 8 ulps of the nearer end's distance.

**`Npc/SOLNpcLaneMath.h`:** `FSOLNpcLaneEntry` gains `int32 InWallShipCount` and `double WallRadiusM`, copied by
`BuildLaneFrame` from `LaneInWallCount` and `LaneProfile[].WallRadiusM` (0 when missing) *(chosen: the frozen counts
come from the counting pass via the group table, not from `AccumulateLaneShips`, which keeps counting every lane rail
for D15)*. **`Npc/SOLNpcTunnelMath`:** `BuildTunnelLaneData` uses `Rt = WallRadiusM` when it is positive and finite,
else `TunnelRadiusM(R, ShipCount)` as in #49.

**Not covered by a test (decide before step 8):** the hull facing of D17. The SDD fixes only "about 60 s"; where the
flip sits (completed by the brake start, or centred on it; uncapped legs have no coast to flip in) and when the hull
turns back for the next leg (during the U-turn?) are open, so no contract was written. *Resolved by D21 (amendment 1,
2026-11-13), contract below.*

**Amendment 1 (D21): hull facing contract (step 7b test, step 8 implementation).**

- `FVector3d SOLNpcRail::LaneHullForward(const FSOLNpcRail&, const FSOLNpcRailFrame&, double timeS)`: for a lane rail
  `SOLLaneTraffic::LaneHullForward(frame.LaneProfile[g], frame.LaneFrame[g], LaneShipFromRail(rail), t)` (unit, phase
  formula as `EvaluateAtTimeM`); the zero vector for cluster and battle rails and for a group without a lane frame (the
  caller keeps today's velocity heading).
- The tiers (Mid, Full AI, the instanced hulls) use it for lane ships in place of `HullForward(VelocityMps + deviation
  velocity, ...)`; how a large AI deviation blends back to the velocity heading is confirmed in step 6
  (`HullForward`, `HandoffOrientation`); if not viable, stop and report.
- No change to the rail layout (P0-P6), the group texture, `SOLNpcTraffic.ush` or `M_SOLNpcPoints`: D21 only changes
  the spawn-time `dv` and `t_a`, and `T_leg` stays derived by the same geometry. No existing 50b assertion changes
  (SDD section 14); the Earth - Moon numbers they log move slightly.
- **Step 7b test (test author, contract only):** `SOLTest.NpcRail.LaneHullForward` in `NpcRailTest.cpp` (15 tests
  now). It asserts the rail value equals the pure function (1e-9, unit, fixed and spread times and both U-turns), no
  jump at the float rail's four joints, travel 1 s before each leg's window, the lane's `Up` at the window's middle
  (1e-6), against travel 1 s after it, angle steps below `1.5 pi dt / T_f` across the window, and the zero vector for
  cluster and battle rails and for a lane rail on a frame without lane entries. No new API name; no existing 50b
  assertion changed.

## Change log

- 2026-11-13: first draft (five parts 50a-50e).
- 2026-11-13: Q1-Q8 answered (SDD D13-D20); Q9 (AUTO override mechanism) added, asked at the start of 50d (step 13b).
- 2026-11-17: Q9 answered (SDD D22): the "War power move" round-menu entry, per command; step 13c added for the menu entry and flag path.
- 2026-11-13: step 2 tests written (`NpcTrafficMathTest.cpp`, 17 tests); the 50a API contract recorded above.
- 2026-11-13: main session decisions on the contract's open points: the test file name stays `NpcTrafficMathTest.cpp`
  (SDD wording fixed); the end margin 1 -> 3 (D14) moved into 50a (`NpcTunnelMathTest` constant case updated; GAME_MECHANICS
  tunnel row updated); the wake bound uses the true largest slope `(l - Z) / (l0 - Z)` (SDD section 4.7 updated); the
  zone, drivable and joint-tolerance readings of the test author adopted (SDD sections 4.2 and 6 updated).
- 2026-11-13: step 4 done. `Traffic/SOLLaneTrafficMath.{h,cpp}` built to the contract; `NPC_LANE_*` traffic constants
  (incl. `NPC_LANE_PEAK_ACCEL_MPS2`) and `NPC_TUNNEL_END_MARGIN_RADII` 3 in `SOLConstants.h`; `FSOLNpcTunnelBasis` is an
  alias of `FSOLLaneBasis` (the smoke header's forward declaration became the alias). As built: the wake bound adds a
  1e-8 relative rounding allowance (the bound is tight, a coasting peak ship mid-lane reaches it, and the brute-force
  test sampled it within rounding); `MakeLaneProfile` counts a nominal leg within 1e-9 of `L0` as 0 (the end-ring
  subtraction's rounding); `MakeLaneProfile` calls `SOLNpcTunnel::TunnelRadiusM`, a cpp-only `Traffic -> Npc` edge for
  `ARCHITECTURE.md` (50b). Next: step 5 (adversarial review of 50a, then commit and push).
- 2026-11-13: step 5 review findings applied. (1) `FSOLLaneShip` loses `LegS` (rail P5 freed): `LaneShipLegS` derives
  `T_leg` from `dv`, `t_a` and `l0`, the leg is evaluated from the nearer end in time and split at `T_leg / 2`, so
  float-rounded rails no longer jump mid-leg (up to about 18 km before); new test `FloatRail`. (2) The 50b phase
  precision, phase formula, V5 smoothness and shader cost notes are recorded above. (3) `MakeLaneProfile(Rw, L0, R_A,
  R_B)`: the caller computes the tunnel radius, so `Traffic/` no longer includes `Npc/`; `ARCHITECTURE.md` gains the
  `Traffic/` folder and its edges (no new `-SOL` flag). (4) The thrust state's left-out map term `X'' u'^2` documented
  (SDD section 4.12, header) and bounded below `a` on every default lane (new test `MapCurvature`). (5) Wake bound
  allowance 1e-6, a non-finite peak reads as the cap. (6) `MakeLaneFrame` floors the current leg at `1.001 Z` and
  shifts B's end by the excess (`EndShiftM`), the map functions read a short `l` the same way (new test
  `ShortCurrentLeg`). (7) The vacuous basis-equals-tunnel check replaced by closed-form and frozen basis values; the
  determinism and apex checks strengthened; the long comment line wrapped. (8) `SOL::STANDARD_GRAVITY_MPS2` is the one
  gravity definition (`SOLNewtonian` re-exports it), `NPC_LANE_PEAK_ACCEL_G = 1.1 x PILOT_SUSTAINED_G`, the smoke header
  forward-declares `FSOLLaneBasis`, field and inline comments added. (9) STATUS and the README caption corrected for
  the end margin 3.
  The API contract above is amended accordingly (`MakeLaneProfile`, `FSOLLaneShip` without `LegS`, `LaneShipLegS`,
  `FSOLLaneFrame::EndShiftM`).
- 2026-11-13: step 7 tests written from the contract (`NpcRailTest`, `NpcTrafficSpawnTest`, `NpcScaleMathTest`,
  `NpcSpawnRegressionTest`, `NpcLaneMathTest`, `NpcTunnelMathTest`); the 50b API contract recorded above.
- 2026-11-13: amendment 1, user decision D21 (refines D17): the hull flip finishes before braking. SDD sections 2,
  4.3, 4.4, 4.7, 4.9, 4.12, 6 (cases 1, 3, 4, 10, 11, 14, 15, new 15c), 8, 10 and new section 14; this plan gains steps
  5b and 7b and the amendment notes in both API contracts. Peak `v_f = sqrt((alpha T_f / 2)^2 + w^2 + alpha l0) -
  alpha T_f / 2`, capped at `c_k`, floored at `w`; Earth - Moon 82.4 -> 81.9 km/s, the capped lanes unchanged.
- 2026-11-13: step 5b tests and step 7b test written from the contract (`NpcTrafficMathTest.cpp`: `LegProfile`,
  `LegArc`, `ThrustState`, `Constants`, `LaneProfile`, `EdgeCases`, new `HullFlip`; `NpcRailTest.cpp`: new
  `LaneHullForward`); notes in both API contracts. Not built yet (they name the D21 API). Next: the 5b implementation.
- 2026-11-13: step 5b and the CPU side of step 8 implemented (one agent, uncommitted; tests untouched except the
  regression capture). **Regression capture:** `NpcSpawnRegressionTest.cpp` tables pasted from the pre-#50 build
  (`a2fe894` code with only that file added), `NPCSPAWNREG_CAPTURED` true; passes after the `MakeSpawn` change.
  **5b:** `MakeLegProfile(kappa, effort, legM, flipS = NPC_LANE_FLIP_S)` with `dv = max(0, min(dv_f, dv_c))`,
  `ESOLLaneThrust::Flip`, `FSOLLaneThrustState::FlipFraction` / `HullForward`, `LaneHullFlipFraction`,
  `LaneHullForward` (shared file-local `TrafficHullForward`, so the state's hull equals the pure function exactly; a
  no-brake ship's zero-speed turn apex falls back to the reversal direction). **50b CPU:** `FSpawn` lane fields
  dimensionless, `FLayout` speed fields, `TriangleWave01`, `LaneParamAt`, `SOLNpcScale::LaneShipSpeedBoundMps` removed
  (`LaneBasis` / `LanePositionM` kept: their tests remain); the halo hash stream appended last (battle and cluster
  streams keep their values; the retired direction stream keeps its slot); `CountLaneSpawns` / `AccumulateLaneSpawns`;
  `SOLNpcRail`: `FromSpawn(spawn, base, laneProfile, GM)`, P0 / P5 phase split, `LaneShipFromRail`,
  `BuildLaneProfiles`, `AccumulateLaneMaxPeak`, `BuildFrame` lane profiles and frames (invalid for an unresolved body),
  `EvaluateAtTimeM` = the 50a closed form (endpoint A without a lane frame), `AdvancedToEpoch(rail, delta, profiles)`
  (`fmod(delta, T_cyc) / T_cyc`), `LaneHullForward`, `LaneWakeSpeedBoundMps` (an unknown lane gives the cap bound),
  `WriteLaneTrafficTexels`, the halo capsule; `NPC_GROUP_TEXTURE_ROWS` 6; `FSOLNpcLaneEntry::InWallShipCount` /
  `WallRadiusM`; `BuildTunnelLaneData` (and the tunnel smoke's expected radius) from the frozen wall radius. Subsystem:
  parallel counting pass (per-block arrays merged), nominal length = the apoapsis sums up the parent chains to the common
  ancestor at the epoch (file-local `NpcLaneNominalLengthM`, D16), body radii from the registry, rails with the lane
  profile, per-block largest peaks merged, the wake bound from `LaneWakeSpeedBoundMps`; visuals: the 6-row group
  texture (all readers use `Load`, so rows 0-1 are unaffected), rows 2-5 written per frame, `AdvancedToEpoch` with the
  table's profiles. `ARCHITECTURE.md` still needs the `Npc -> Traffic` edges (`SOLNpcRail.h` includes
  `Traffic/SOLLaneTrafficMath.h`; `SOLNpcRail.cpp` includes `SOLNpcTunnelMath.h`). **Test issue (for the test author,
  not edited):** `NpcTrafficMath.ThrustState` asserts 14 flip samples per burning ship, but its own skip rule (samples
  within `1e-6 T_leg` of a window edge) drops the `x = +-0.95` samples when `T_leg > 1.5e6 s` and also `+-0.7` when
  `T_leg > 9e6 s` (Earth - Mars rho 0.5: 10, rho 1.6: 8); every sample it does check passes. Suggested fix: count the
  samples that were not skipped, or cap the edge tolerance (e.g. `min(1e-6 T_leg, 0.1 s)`).
- 2026-11-13: `NpcTrafficMath.ThrustState` count fixed (test only, skip rule unchanged): the skip predicate is shared,
  the expected flip count is `2 x` the window samples that survive it (asserted at least 3 per leg: the centre and
  `+-0.2` samples, 24 s clear of the edges), and the two just-outside samples sit `max(5 s, 2 x tolerance)` beyond the
  window edges so a long leg no longer skips them.
- 2026-11-14: 50b adversarial-review fixes (uncommitted). **GPU:** `Shaders/Private/SOLNpcTraffic.ush` rewritten for
  double-float precision: the CPU rail layout is unchanged (P0 phase high, P5 phase low, P6 reserved 0), the GPU upload is
  built per epoch in double by a new `SOLNpcRail` function (used by `ASOLNpcVisuals::FillRailTexels`, the smoke and the
  test) with `tau0 = phi x T_cyc` split into a hi / lo float pair and `T_leg` computed in double and rounded once; the
  shader takes the segment from `(segment constant - boundary) + moving part` and splits the coast arc into an epoch
  constant and a small moving part, so motion is smooth at any distance (SDD 50 sections 4.4 and 4.6). Group row 2 `.w`
  is now the wall speed (written by `WriteLaneTrafficTexels` from `SOL::NPC_LANE_WALL_SPEED_MPS`); rows 4 and 5 re-laid
  out (`pi / T_U`, `e_A`, `l0`, `Z`; `R_B + m Rw - end shift`, `T_U`, `(l - l0)/(l0 - Z)`, `1/Z`). `M_SOLNpcPoints`
  rebuilt with `create_npc_assets.py -SOLRebuild`. **Smoke:** `-SOLSmokeNpcTraffic` gained a Full-tier hull check.
  **Tiers:** a Full lane ship takes its nose from `SOLNpcRail::HullHeadingVelocityMps`, not the rail velocity (no 180
  degree turn or snap on promotion / demotion); `CollectChunk` and the Full gather derive the lane leg and cycle once per
  ship per frame via a combined motion function. **Dead code:** `SOLNpcScale::LaneBasis` / `LanePositionM` and their tests
  removed. **Docs:** `GAME_MECHANICS.md` lane rows and the wall-radius rule, SDD 50 sections 4.4, 4.6 and case 10. The
  in-game Info text states no lane speeds, so the player guide needs no regeneration for this change.
  Measured: Earth - Mars mid-lane 27.8 km, outer lane 307.5 km, repeated frames mid-lane 0, added ALU about 36.
- 2026-11-16: 50b test pass of the review fixes (uncommitted). `M_SOLNpcPoints` rebuilt with `-SOLRebuild` (the other
  NPC materials' byte drift restored to HEAD). First full suite: 1938 / 1940, two failures. (1)
  `NpcRail.GpuLaneFloatMirror`: Earth - Mars 42.4 km over the 30 km target; diagnosis: `T_leg`'s single float rounding
  (`v_p x ulp(T_leg) / 2`, doubled on the inbound far half) dominated. Fix: `SOLNpcRail::GpuRailTexels` puts `r =
  T_leg - float(T_leg)` as `r / (2 ulp)` in the packed value's fraction (within +-0.25; the vertex node decodes the
  integer by rounding, so no material change), new `SOLNpcRail::GpuLegUlpS`; `SOLNpcTraffic.ush` and
  `SOLNpcTrafficMirror.h` decode it and carry it into the moving part of every leg boundary passed and the far-half time
  to the leg's end. (2) `NpcRail.GpuRailTexels`: the `tau0` high + low bound of 1e-9 s cannot hold with a float low part
  (its own rounding is up to 1.5e-8 s); the bound is now one float step of the low part, and the test checks the
  packed integer, the fraction range and the decoded `r`. Measured after the fix (`GpuLaneFloatMirror`, worst anywhere,
  viewpoint 1,000 km from the ship): Mercury - Venus 9.9 km, Venus - Earth 13.2, Earth - Moon 0.08, Earth - Mars 27.8
  (0.93 of the 30 km clamp), Mars - Jupiter 68.7, Jupiter - Saturn 234, Saturn - Uranus 307.5, Uranus - Neptune 433.6,
  Earth - Mars at 0.3 L0 11.1; `GpuLaneSmoothness` mid-lane 0 of 7,200 frames repeat. ALU (DXC `-O3`, `cs_6_0`, the
  function alone): 213 DXIL ALU ops (158 float math) against 177 (134) for the same formula in single float, so about
  +36. Full suite 1940 / 1940. Smokes at 1280x720 and 1920x1080: `-SOLSmokeNpcTraffic` 14 / 14 (hand-over worst 7.1 /
  7.7 km, 0 repeated of 6,000 frames; `V2-full-hull-heading` passes with no Full-tier lane hull near the view, so it is
  not exercised), `-SOLSmokeNpcLanes` 35 / 35, `-SOLSmokeNpcTunnel` 39 / 39; `GameUserSettings.ini` unchanged. The wake
  cost stays unmeasured.
- 2026-11-16: fixes of the second 50b review (uncommitted). (1) `SOLNpcTraffic.ush`: `T_leg`'s rounding carry, the
  coast split, `fromRing*M`, the ring and leg positions are `precise` (UE's DXC runs without `-Gis`). Verified on the
  DXIL (engine `dxcompiler.dll`, `-O3 -Zpr -HV 2018`, `vs_6_6`, the function alone): `fast` ops 129 / 149 before, 47 /
  153 after, all in the U-turn and not-drivable branches; the leg position's 103 feeding ops carry none. (2) Mid
  hand-over: `ASOLNpcVisuals::UpdateCpuShips` decides a Point-tier lane ship by the float mirror's length (the shader's
  test) and draws it at `SOLNpcTrafficMirror::CpuPointHandOver` (mirror at the Mid radius, smoothstep to the double
  position at the instanced radius); smoke `V5-*-handover-px`: within 0.003 px of the far point (limit 0.5), where the
  double position alone popped up to 3.1 px (720p) / 7.1 px (1080p) on Earth - Mars. Residual, reported not fixed: a
  ship inside by the float test and outside by the double one gets no point if it arrives from beyond the radius (3-4 %
  of the hand-over frames, runs up to 31 frames). (3) `GpuLaneSmoothness` straddles `T_leg / 2`: worst one-frame jump
  0.094 km (Earth - Moon), 34.0 km (Earth - Mars, about 16 px at the Mid radius at 1080p), 298.7 km (Jupiter - Saturn),
  523.3 km (Saturn - Uranus), 0.15-0.40 of the case-10 bound, so the foot-point fix was not taken (SDD 50 section 4.6).
  (4) Smoke: V2 flies 5 km past a coasting mid-lane ship (28 km/s relative): 943 Mid and 812 Full hull frames, all
  within 0.0 deg of `LaneHullForward`, 1 promotion and 1 demotion with 0.000 deg nose change (new `V2-handoff-nose`);
  the V5 hand-over samples lie within 0.5-2 Mid radii (no real ship there at 1M, so 100 sampled ships seen from
  viewpoints that make them cross, 1,888-2,111 km; mirror vs CPU worst 11.2-19.7 km, 0.06-0.10 of the bound); spreading
  the samples over the whole lane shows 227-239 repeated float positions of 6,000 frames (reported). New tests
  `NpcRail.FullTierHandoff` (promotion 0.0000 deg, worst frame 0.075 deg, flip lag 1.05 deg, demotion 0.000 deg) and
  `NpcRail.CpuPointHandOver`. (5) Dead code removed (`accelM` in the shader, `FSOLNpcAiShip::RailVelocityMps`, the
  rail overload of `HullHeadingVelocityMps`; `HullHeading` uses the Motion overload); `GpuRailTexels` derives the leg
  and cycle once; `static_assert` on the packed value. `M_SOLNpcPoints` rebuilt (the other NPC materials restored to
  HEAD). Full suite 1942 / 1942. Smokes at 1280x720 and 1920x1080: `-SOLSmokeNpcTraffic` 17 / 17, `-SOLSmokeNpcLanes`
  35 / 35, `-SOLSmokeNpcTunnel` 38 / 39: `T5-labels` draws the Earth - Mars compact label in T5, where D19 expects none;
  the code path is untouched here (the sim clock starts at the wall-clock date, so the T5 geometry moves), left open.
  The 1080p runs rewrite `GameUserSettings.ini`; it was restored from a backup after each run.
- 2026-11-16: third 50b pass (uncommitted). (A) `T5-labels` was date-fragile, not a code regression: the same build
  passes at `-SOLSimDate=2026-11-16T12:00:00Z` and `2026-11-12T12:00:00Z` and failed at the real 21:10 UTC (T5 faces the
  Moon, about 0.5 degrees an hour; the Earth - Mars anchor entered the top-left corner unoccluded and its compact label
  drew, as the label rules say). `-SOLSmokeNpcTunnel` now starts at `SOL::CommandLine::SMOKE_NPC_TUNNEL_SIM_DATE`
  (2026-11-12 12:00 UTC) unless `-SOLSimDate=` is given (`USOLSimClockSubsystem::Initialize`); the check is unchanged
  (SDD 49 D19 note). (B) Lane hand-over margin: `SOLNpcRail::LaneHandOverMarginM` per lane per frame
  (`USOLNpcSubsystem::mGroupMidMarginM`, `GetGroupMidMarginsM`), added by `USOLNpcTierProcessor` to the Mid radius
  (promote and keep) and the evaluate keep radius, and by the subsystem to the group cull; smoke V5 `-handover-once`
  (tiers simulated from the subsystem's margin, the ship arriving Far): 0 missing, 0 doubled of 6,100 frames per view,
  Earth - Mars margin 292.9 km at the current length (224-279 frames per view inside by the float test but outside the
  bare radius by the double one, runs up to 31, all CPU ships now); new test `NpcRail.LaneHandOverMargin` (margins 0.49 km
  Earth - Moon to 5,993 km Uranus - Neptune at the nominal lengths, sampled errors 0.04-0.33 of them). (C) The V5
  whole-lane repeats were real: the end zones (5 % of the nominal leg) used the direct float arc. New test
  `NpcRail.GpuLaneCycleSweep` (whole cycle, per phase): end-zone repeats before Earth - Mars 498 / 5,760 (0.52 px worst
  step at 2,000 km, 1080p), Jupiter - Saturn 2,345 (2.75 px), Saturn - Uranus 4,000 (5.38 px), none in the coast or on
  Earth - Moon. Fix in `SOLNpcTraffic.ush` and `SOLNpcTrafficMirror.h`: the accelerate arc and the end-zone map split into
  epoch constant and moving part (the ramp's exact difference quotient) while `|m| <= s` and `|d| <= 1`, else direct;
  after: 0 repeats in every phase, worst step 0.26 px; `GpuLaneSmoothness`, `GpuLaneFloatMirror` unchanged (Earth - Mars
  27.8 km). +42 float ops in the function (171 -> 213, SDK DXC `cs_6_0 -O3`), not profiled. `M_SOLNpcPoints` rebuilt
  (`-SOLRebuild`; Hull, Lanes, Tunnel restored to HEAD, byte-identical). Full suite 1944 / 1944. Smokes at 1280x720 and
  1920x1080: `-SOLSmokeNpcTraffic` 19 / 19 (V5 0 repeated of 6,000 frames, hand-over within 0.001 px),
  `-SOLSmokeNpcLanes` 35 / 35, `-SOLSmokeNpcTunnel` 39 / 39; `GameUserSettings.ini` restored after each run (MD5
  unchanged).
- 2026-11-16: step 12 (50c, uncommitted). **Math:** `SOLLaneTraffic::RateMerge`, `MergeDirectionSign`, `LanePointOf`, `TrafficSpeedMps`, `MergeReadout` and their structs in `Traffic/SOLLaneTrafficMath`; `UI/SOLLaneTunnelLabel`: `MergeVerdictText`, `FormatMergeReadoutLine`, `MakeMergeReadoutKey` (each speed's printed digits, sign and unit packed on the stack, like `LaneLabelDistanceKey`), `MergeBandColor`, `ChooseMergeReadoutSpot`, `LaneLabelSizeWithLine`; `HudPalette::AMBER`, `SOLMenuText::LANE_TRAFFIC_*`. **Lane table:** new `Traffic/SOLLaneTrafficSubsystem` (end bodies and frozen profiles, `SetLanes` at spawn after `BuildLaneProfiles`, `ClearLanes` in `Despawn`; a readout verification override). **HUD:** `ASOLFlightHud::UpdateMergeReadout` per frame before the labels (fixed 64-lane array, frames from the registry, the line rebuilt and measured only on a key change), hidden on the map, in menus, warp jumps, HUD off and with the Lanes row off. *As built:* the labels are placed at their own size first (growing the box inside the placement made the Earth - Mars label unplaceable in the #49 smoke's T2 / T3 / T4b / T6 views, 35 / 39, so the labels never give way to the readout); the readout placement was reworked the same day (next entry). The fallback line is drawn right of the speed card on the row under the large speed readout, moved to the large readout's row or above the card when a label drawn this frame covers it; it is not a label avoid box (as one it pushed the T4b label off). **Text:** one Info sentence after the tunnel sentences; glossary row "Lane traffic readout" (Overlays and route, description-only with a reason); `GlossaryLayoutTest` row counts updated for the new row (46 rows, 2 None); `PLAYER_GUIDE_TAB_PAGES` Overlays and route 5 -> 6; player guide regenerated (`-NoCrops`). **Smoke:** new `-SOLSmokeLaneMerge` (`Npc/SOLLaneMergeSmoke`, V6: rho 1.5 / 0.8 / 0.2 x 0.9 / 1.0 / 1.3 v_t, hidden at 2.5, the #49 T2 and T4b views with the fallback clear of the drawn label, the warp note at 1000x). **Test issue (for the test author, not edited):** `SOLTest.LaneMergeReadout.BandColor` compares with `FLinearColor::Equals(c, 0.0f)`, which is `|d| < 0` per channel and so never true; a tolerance above 0 (or `==`) fixes it. *Resolved 2026-11-17: the test now uses a 1e-6 tolerance.*
- 2026-11-17: step 12 rework (50c, uncommitted), the user's final D18 decision: the readout is always its placed label's last line (the fourth of the full form, the second of the compact one) and that label's box grows by the line and may overlap the guide-dot rows. New pure `SOLLaneTunnelLabel::PlaceLaneLabelWithLine`: after the normal placement the readout's label is re-placed grown (`LaneLabelSizeWithLine`, both forms), with no dot rows, the HUD panels and the other placed labels as avoid boxes (they never move), first at the spot and form it took; vertically on the anchor as before, horizontally slid off centre (still covering its own centred box) to the clear position nearest to centred. The slide is the implementer's reading of "keep the clamping": in the #49 T4b view the centred grown box met the speed card. If even the slid box fits nowhere the readout keeps the fallback line (open question Q10 for the user). The fallback line is otherwise only for no placed label of the lane. Info and glossary text updated ("the tunnel label's last line, or a line under your speed when no label is shown"), player guide regenerated (`-NoCrops`). Tests: new `LaneMergeReadout.LabelWithLine` (same spot, slide, 6 o'clock fallback, compact, the T4b case, off-screen, unplaced, bad line); `Spot` comments updated. Smokes: V6 asserts the readout in its label (inside the label box) on the axis and, on the fallback line, no label of its lane drawn; V6's beside-the-wall view places no label at all (seen at all three sizes), so its fallback is right. `-SOLSmokeNpcTunnel` turns the readout override on, exempts the readout's label from the dot-row check and asserts the readout in the Earth - Mars label in T2 / T3 / T4b. Full suite 1961 / 1961; smokes LaneMerge 15 / 15, NpcTunnel 39 / 39, NpcLanes 35 / 35, NpcTraffic 19 / 19, Hud 45 / 45 at 1280x720, 1920x1080 and 2560x1440; `GameUserSettings.ini` restored after every run (MD5 unchanged).
- 2026-11-17: step 12 review-fix pass (50c, uncommitted): Q10 resolved by the user (the grown label slides sideways, else the readout uses the fallback line), an empty lane still shows the readout. Placement hysteresis fed by the own-size placement, the readout width cached per lane, the full form tried first, `ChooseMergeReadoutSpot` with the 0.25 s label-return delay and the line ease, the fallback spot in the pure `ChooseMergeFallbackSpot`, the 0.1 s text refresh and the 0.05 m/s zero rule, `IsMergeReadoutEnabled`, `NPC_LANE_MERGE_FORWARD_MIN`. Smoke V6 pinned to a sim date with an expected spot per view. New tests `LaneMergeReadout.FallbackSpot`, `.Enabled`, `.LaneTable`, and `Spot`, `LabelWithLine`, `TextLine`, `TrafficSpeed`, `LaneChoice` extended. SDD 4.8, 4.10 and the V6 text updated. Full suite 1964 / 1964 (`PlayerGuide.UpToDate` after `WritePlayerGuide -NoCrops`); smokes LaneMerge 15 / 15, NpcTunnel 39 / 39, NpcLanes 35 / 35, NpcTraffic 19 / 19, Hud 45 / 45 at 1280x720, 1920x1080 and 2560x1440 (the 4K proxy: a `-ResX=3840` window is not enlarged past the 2560x1440 desktop, it rendered at 1920x1080); the Q10 no-fit path seen for about 20 frames right after V6's two large repositions (the quarter-leg axis view and the warp view; none in the rho 0.2 views); `GameUserSettings.ini` restored after every run (MD5 unchanged).
- 2026-11-17: steps 13c, 14 and 15 (50d, uncommitted). AUTO obeys the lane rules by default (D19, D20: at least 20 wall radii of axis in the halo), approach to the merge point then hold at the traffic speed, leave at the exit arc; War Power Move per D22 (full boosted thrust, meter floor 0.3, braking at the 1.5 G budget). New `Ship/SOLAutopilotOrder`, `Npc/SOLAutoLanesSmoke` (`-SOLSmokeAutoLanes`), module edge `Traffic -> Flight`, constants `WAR_POWER_METER_FLOOR` and `NPC_LANE_AUTO_*`. As built: the lane trip time is the nominal mean-effort leg time (about 16 days Earth - Mars), not the SDD's 9 days. Next: the 50c and 50d reviews (steps 13, 16).
- 2026-11-17: step 16 review-fix round 2 (50d, uncommitted). Mid-lane exit fix (new final mode `Left`, leave arc = the nearer of the exit arc and the target's arc minus `LaneLeaveDistanceM`), lane views from frame-start bodies, latch re-validation, lane ETA for body, route and chase orders, chase `Lane` activity, pulsed War power (D22 amended in the SDD), Move To and War Power Move never block, HUD BOOST and WAR POWER activity. SDD 4.9, 4.9.1, D19, D22 and GAME_MECHANICS updated.
- 2026-11-17: 50e steps 17 (partly) and 18 done by the 50e agent: profile results in SDD section 15, README gallery shots, tuning strips for the user (SDD Q11), new `TECH_DEBT.md` items (re-epoch hitch, AUTO rules not isolated). Next: the user's Q11 answer, the rest of step 17, steps 20-22.
- 2026-11-17: Q11 answered by the user and built, with the tunnel guide thinning (SDD 50 D23, uncommitted). **Q11:** the mid-leg hull flip turns toward each ship's own top (`SOLLaneTraffic::LaneShipTopVector`, a roll hashed from the NPC index, nothing stored), the U-turn attitude kept. **Thinning (SDD 49 section 12d):** every 6th ring of the first #49 lattice kept (`NPC_TUNNEL_RING_KEEP_EVERY` 6, spacing 12 tunnel radii, rho 1.5), dots and chevrons at each kept ring and halfway (`NPC_TUNNEL_CHEVRONS_PER_GAP` 2, the count in `NpcTunnelStyle3.w`, the index in UV2.y), 12 ring and 6 gap slots per side, LOD bands 2 / 3 / 5, 2,884 vertices per lane (23,072 for 8, was 75,808). Tests first (`NpcTunnelMathTest`, new `GuideThinning`; `NpcTrafficMathTest`, `NpcRailTest`). Full suite 2017 / 2017; smokes NpcTunnel 39 / 39, NpcTraffic 19 / 19, NpcLanes 35 / 35, LaneMerge 15 / 15, AutoLanes 13 / 13, Hud 45 / 45 at 1280x720, 1920x1080 and 2560x1440; `GameUserSettings.ini` unchanged (MD5). Not re-profiled after the thinning (fewer vertices, so the 50e GPU numbers are an upper bound); the README shots `49-lane-tunnel-*.png` and `50-lane-*.png` show the pre-thinning ring density. SDD 49 (D7, 4.5, 4.7, 6, 9, 10, 12d), SDD 50, GAME_MECHANICS, ARCHITECTURE and STATUS updated.
- 2026-11-18: the rendered hull's roll decided by the user and built test first (SDD 50 D24, D25, section 16; contract `50-hull-flip-contract.md`, uncommitted). Hulls are drawn from a forward and a body top (`LaneHullBasis`, `HullRotation`, replacing the heading-only `FindBetweenNormals` that was singular at heading -X), both flips turn in one plane (the inbound flip toward `-side x T_out`), the attitude is held through the brake and the U-turn, the flip side is fixed per NPC index (`LaneFlipSide`); the per-length flip window (`LaneFlipDurationS`, limit `NPC_LANE_FLIP_END_ACCEL_*`, provisional) is not yet fed a length (every hull is the 30 m placeholder: open item, `TECH_DEBT.md`). New test file `NpcHullFlipTest.cpp` (8 cases); `NpcTrafficMath.HullTopVector`, `HullFlip`, `ThrustState`, `NpcRail.LaneHullForward` and `FullTierHandoff` re-pinned. `SOLTest.Npc` 223 / 223, full suite 2025 / 2025, smokes NpcTraffic 19 / 19, LaneMerge 15 / 15, AutoLanes 13 / 13 at 1280x720, `GameUserSettings.ini` unchanged (MD5). The drawn roll is not eye-checked (no smoke asserts it, no PIE run). Not re-profiled.
- 2026-11-18: D24 review fixes and the user's Full-tier roll answer (SDD 50 sections 11 and 16, uncommitted): a Full lane ship is handed off and steered with its lane top (`SOLNpcRail::HullUpHint` in `USOLNpcFullAiProcessor`), every Full ship is drawn with its attitude's up and the tier processor evaluates `Motion` only without an attitude; the forward-only `LaneHullForward` / `LaneThrustAt` skip the top vector outside the flip window again (forward bit-identical); `SOLNpcAi::HullDrawRotation` replaces the visuals' file-local helper (its cone quarter turn a literal quaternion); `NPC_LANE_FLIP_PEAK_RATE_FACTOR` named once; new tests `Npc.HullFlip.DrawRotation` and `Npc.HullFlip.TierTopAndPromotion`, roll checks and a modulo-pi plane check in `NpcRail.FullTierHandoff`; the non-lane demotion roll pop logged in `TECH_DEBT.md`. `SOLTest.Npc` 225 / 225, `Lane` + `Autopilot` 62 / 62, full suite 2027 / 2027, smokes NpcTraffic 19 / 19, LaneMerge 15 / 15, AutoLanes 13 / 13 at 1280x720, `GameUserSettings.ini` unchanged (MD5). The drawn roll is still not eye-checked (no PIE run).
