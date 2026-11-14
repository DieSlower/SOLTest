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
| `Source/SOLTest/Npc/SOLNpcTunnelMath.h` / `.cpp` | `TunnelBasis` forwards to `SOLLaneTraffic::LaneBasis` (50a); `BuildTunnelLaneData` takes the frozen wall radius (50b) | 50a, 50b |
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

## Change log

- 2026-11-13: first draft (five parts 50a-50e).
- 2026-11-13: Q1-Q8 answered (SDD D13-D20); Q9 (AUTO override mechanism) added, asked at the start of 50d (step 13b).
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
