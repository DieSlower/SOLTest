<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 50: Lane traffic flow

SDD: [`SDDs/50-lane-traffic-flow.md`](../SDDs/50-lane-traffic-flow.md). Issue
[#50](https://github.com/DieSlower/SOLTest/issues/50).

**Grill-me complete (2026-11-13):** decisions D1-D12 are in SDD section 2; the user answered Q1-Q8 the same day and they
are folded in as D13-D20. **One open question remains, Q9 (SDD section 12): how the player tells AUTO to fly around
lanes or ignore the safe space (D19).** It affects only part 50d, to be asked in the 50d interview (recommended: a
console flag plus an interaction-menu entry); 50a-50c may start.

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
   moved to `Traffic/` with the `SOLNpcTunnel::TunnelBasis` forwarding name, the `NPC_LANE_*` traffic constants. Nothing
   in the game changes yet.
2. **50b - keep-right rails live** (steps 6-10): spawn counting pass, dimensionless `FSpawn` lane fields, the lane rail
   layout, `FromSpawn` / `EvaluateAtTimeM` / `AdvancedToEpoch` / `BuildFrame`, the group table and the 6-row group
   texture, `Shaders/Private/SOLNpcTraffic.ush` and the `M_SOLNpcPoints` rebuild (CPU and GPU switch together, so no
   seam is ever committed), the wake bound and capsule, the tunnel radius from the frozen wall radius, the label count
   (D15), the end margin 1 -> 3 (D14), the hull facing (D17), `-SOLSmokeNpcTraffic` V1-V5. May take two consecutive agents
   (C++ first, then the shader and smoke), committed once.
3. **50c - merge readout** (steps 11-13): `Traffic/SOLLaneTrafficSubsystem` (lane table), `MergeReadout`, the label's
   fourth line and the D18 fallback, the HUD draw, `SOLMenuText` tokens, the Info text and the glossary entry, the player
   guide, smoke V6.
4. **50d - AUTO lane rules** (steps 14-16): **starts with an interview for Q9** (how the player tells AUTO to fly
   around lanes or ignore the safe space; recommended console flag plus interaction-menu entry; no setting without the
   user's approval, D11). Then `FindLaneLeg`, `LaneMergeTarget`, `LaneHoldGuidance` in `USOLShipFlightProcessor` for the
   body autopilot, route waypoints and Chase Down (lane rules on by default, D19, a path counts only with 20+ wall radii
   inside the halo, D20), the override, the ETA, the `LANE` marker token, smoke V7.
5. **50e - tuning, profiling, docs** (steps 17-22): screenshots and tuning, `-SOLSmokeNpcTrafficProfile` against the
   pre-#50 baseline (gates), every doc, the README gallery, the whole-ticket review; closes #50.

## Files to change

| File | Change | Part |
|---|---|---|
| `Source/SOLTest/SOLConstants.h` | `NPC_LANE_WALL_SPEED_MPS`, `NPC_LANE_PEAK_ACCEL_G`, `NPC_LANE_SPEED_CAP_MPS`, effort min / max / mean, halo fraction / outer radius / kappa / ramp, U-turn depth, metric zone, merge thresholds and direction minimum, AUTO track / overlap / lateral gain and cap, `NPC_GROUP_TEXTURE_ROWS`, `NPC_LANE_FLIP_S` (60, D17), the end margin 3 (D14), the smoke flags, the readout colours if not already HUD palette constants | 50a (math), 50b-50d (rest) |
| `Source/SOLTest/Traffic/SOLLaneTrafficMath.h` / `.cpp` (new) | Pure traffic math (SDD sections 4.2-4.5, 4.8, 4.9, 4.12) | 50a, 50c, 50d |
| `Source/SOLTest/Traffic/SOLLaneTrafficSubsystem.h` / `.cpp` (new) | World subsystem holding the population's lane table (written at spawn, cleared at despawn) | 50c (50b if the rails need it first) |
| `Source/SOLTest/Npc/SOLNpcTunnelMath.h` / `.cpp` | `TunnelBasis` forwards to `SOLLaneTraffic::LaneBasis`; `BuildTunnelLaneData` takes the frozen wall radius; end margin 3 per D14 | 50a, 50b |
| `Source/SOLTest/Npc/SOLNpcScaleMath.h` / `.cpp` | `FSpawn` lane fields dimensionless, `MakeSpawn` lane branch (halo stream), `FLayout` speed fields removed, `TriangleWave01` / `LaneParamAt` deleted, `LaneShipSpeedBoundMps` new inputs | 50b |
| `Source/SOLTest/Npc/SOLNpcRail.h` / `.cpp` | Lane rail layout (P0-P6), `FSOLNpcGroupTable` lane profiles, `FSOLNpcRailFrame` per-lane basis and length, `FromSpawn`, `EvaluateAtTimeM`, `AdvancedToEpoch`, `GroupDistanceM` (halo capsule) | 50b |
| `Source/SOLTest/Npc/SOLNpcSubsystem.*` | Counting pass, lane profiles, rails, wake bound, lane table publish | 50b, 50c |
| `Source/SOLTest/Npc/SOLNpcLaneMath.*` | `InWallShipCount`, `WallRadiusM` on `FSOLNpcLaneEntry`; `AccumulateLaneShips` in-wall count | 50b |
| `Source/SOLTest/Npc/SOLNpcVisuals.*`, `SOLNpcTunnelComponent.*` | Group texture rows 2-5; tunnel radius from `WallRadiusM`; label count (D15) | 50b |
| `Source/SOLTest/Npc/SOLNpcTierProcessor.cpp` | Hull facing per D17 (thrust direction while braking, smooth flip) | 50b |
| `Shaders/Private/SOLNpcTraffic.ush` (new), `Tools/Npc/create_npc_assets.py`, `Content/SOL/Npc/M_SOLNpcPoints.uasset` | HLSL mirror of the lane rail; the point vertex node's lane branch calls it with the extra group rows and `w`; rebuilt with `-SOLRebuild`, editor closed (restore any byte drift of the other NPC assets) | 50b |
| `Source/SOLTest/Npc/SOLNpcTrafficSmoke.*` (new), smoke wiring | `-SOLSmokeNpcTraffic` V1-V7, later `-SOLSmokeNpcTrafficProfile` | 50b-50e |
| `Source/SOLTest/UI/SOLLaneTunnelLabel.*`, `UI/SOLFlightHud.*` | Readout line in the label, fallback (D18), draw and colours | 50c |
| `Source/SOLTest/Menu/SOLMenuText.h`, Help glossary table | `LANE_TRAFFIC_*` tokens, Info wording, glossary entry, `LANE` marker token | 50c, 50d |
| `Source/SOLTest/Ship/SOLShipFlightProcessor.cpp`, `Ship/SOLShipFragments.h` | Lane leg per frame, merge / hold / leave per substep, transient lane fields (not saved) | 50d |
| `Source/SOLTest/UI/SOLFlightHud.*` (AUTO marker), ETA | `LANE` while holding; the ETA counts the lane leg | 50d |
| Tests: `LaneTrafficMathTest.cpp` (new), `NpcScaleMathTest.cpp`, `NpcRailTest.cpp`, `NpcLaneMathTest.cpp`, `NpcTunnelMathTest.cpp`, `LaneTunnelLabelTest.cpp`, the HUD bus test, InfoText, PlayerGuide, glossary | SDD section 6 cases 1-16 and section 7 re-validation | 50a-50d |
| `Docs/GAME_MECHANICS.md` | Lane traffic row, tunnel row note, Autopilot / Chase notes, readout HUD row | 50e |
| `Docs/ARCHITECTURE.md` | `Traffic/` folder and edges, `Npc/`, `Ship/`, `UI/` rows, flags | 50b (edges), 50e |
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
| 2 | TDD: SDD section 6 cases 1-8, 11, 12, 15 written from the contract only (no implementation shown), in `LaneTrafficMathTest.cpp` | opus / high | To do |
| 3 | `Tools\RunTests.bat -Build -Filter SOLTest.LaneTrafficMath`: red for the right reason (missing `SOLLaneTraffic`), recorded here (skip only if it cannot compile without the API, as in 49a) | sonnet / medium | To do |
| 4 | Implement `Traffic/SOLLaneTrafficMath`, the basis move with the forwarding name, the constants; topic tests and the full suite green | opus / high | To do |
| 5 | Adversarial performance and style review of 50a (fresh opus / xhigh, report only; brief: changed files, SDD 50, style guide, performance guidelines, the SDD section 3 debt list); fixes; re-check against SDD 50; full suite; `Tools\UpdateMetrics.bat`; commit and push 50a | opus / xhigh, then opus / high | To do |
| **50b** | | | |
| 6 | Code check first: every reader of the removed `FSpawn` / `FLayout` lane fields and of `TriangleWave01` / `LaneParamAt`; the hull heading path (`HullForward`, `HandoffOrientation`); the group texture readers (`M_SOLNpcLanes`, `M_SOLNpcTunnel`, points); the label count path. If a decision is not viable, stop and report | opus / high | To do |
| 7 | TDD: section 6 cases 9 and 10 (spawn statistics, rail packing, epoch, the GPU float mirror) and the section 7 re-validation of `NpcScaleMathTest`, `NpcRailTest`, `NpcLaneMathTest`, `NpcTunnelMathTest`, `LaneTunnelLabelTest` (count) from the contract only | opus / high | To do |
| 8 | Implement the spawn pass, rails, frame, group table and texture rows, wake bound and capsule, tunnel radius, label count, end margin (D14), hull facing (D17); tests and the full suite green | opus / high | To do |
| 9 | `SOLNpcTraffic.ush`, the point vertex node, `M_SOLNpcPoints` rebuilt (editor closed); `-SOLSmokeNpcTraffic` V1-V5 PASS at 1280x720 and 1920x1080; `-SOLSmokeNpcLanes` 35 / 35, `-SOLSmokeNpcTunnel` 39 / 39 (updated checks), the 1M ramp PASS | opus / high | To do |
| 10 | Review of 50b (fresh opus / xhigh, report only, brief plus the `.ush` and material); fixes; re-check against SDD; full suite; metrics; commit and push 50b | opus / xhigh, then opus / high | To do |
| **50c** | | | |
| 11 | TDD: case 13 (merge readout), the label's fourth line, case 16 (text) from the contract only | opus / high | To do |
| 12 | Lane table subsystem, `MergeReadout`, label line and fallback (D18), HUD draw, tokens, Info text, glossary entry, player guide regenerated; tests and suite green; smoke V6 PASS; `-SOLSmokeHud`, `-SOLSmokeSolidMenu` PASS | opus / high | To do |
| 13 | Review of 50c; fixes; full suite; metrics; commit and push 50c | opus / xhigh, then opus / high | To do |
| **50d** | | | |
| 13b | Interview the user on Q9 (how AUTO is told to fly around lanes or ignore the safe space); fold the answer into the SDD (decision, contract case 14) | main session | To do |
| 14 | TDD: case 14 (AUTO rules, the override per Q9, and the regression that AUTO without a lane leg is bit-identical) from the contract only | opus / high | To do |
| 15 | AUTO lane rules in the flight processor (body, route, chase), the Q9 override, ETA, `LANE` marker; tests and suite green; smoke V7 PASS; the route and chase smokes PASS | opus / high | To do |
| 16 | Review of 50d; fixes; full suite; metrics; commit and push 50d | opus / xhigh, then opus / high | To do |
| **50e** | | | |
| 17 | Screenshots at 1280x720, 1920x1080, 3840x2160, HUD glow 0 / 100 / 200 %: flow, halo, U-turn, readout colours, AUTO in a lane; tuning (halo kappa, U-turn depth, flip time, readout colours) with the main session showing shots to the user where a value needs the user's eye; final values in `SOLConstants.h` and an as-built section of the SDD; candidate shots deleted | opus / high | To do |
| 18 | Profiling (SDD section 8 item 6): build and run the pre-#50 baseline (`f336e10`) and #50 in one session, T2, T5 and a far view, three runs each, 1920x1080 gates (points draw <= +0.10 ms GPU, NPC update <= +0.10 ms CPU, readout and AUTO rules <= 0.01 ms CPU each), 3840x2160 and 3840x3744 recorded; the 1M ramp; results in the SDD | opus / high | To do |
| 19 | PIE check via unreal-mcp if the editor is open (else noted here) | sonnet / medium | To do |
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
- Pause points per CLAUDE.md (context above 35 %, usage limits, 70 % weekly): save resume state here (the step, files
  mid-edit, next action) and run `Tools\UpdateMetrics.bat`.

## Change log

- 2026-11-13: first draft (five parts 50a-50e).
- 2026-11-13: Q1-Q8 answered (SDD D13-D20); Q9 (AUTO override mechanism) added, asked at the start of 50d (step 13b).
