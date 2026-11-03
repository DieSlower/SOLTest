<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 32: Chase Down autopilot

SDD: [`Docs/SDDs/32-chase-down-autopilot.md`](../SDDs/32-chase-down-autopilot.md). Living document: tick steps as they
land. Each part is built by its own subagent (context kept under ~35%) and is small enough to verify without the user.

**Gate cleared:** the SDD's questions Q1-Q10 are resolved (SDD section 8) and folded into the contract below.

## Parts

### 32a: pure chase math (test-first)

1. [x] Contract below handed to a separate opus subagent (high effort) that writes `Tests/ChaseTest.cpp`
   (`SOLTest.Chase.*`) from the contract only, no implementation shown.
2. [x] Stubs in `Flight/SOLChase.{h,cpp}` and the `SOL::Chase` constants in `SOLConstants.h`; build; the tests are red
   for the right reasons.
3. [x] Implement (opus, high); green (except the one conflicting case, see the 2026-11-03 note);
   `SOLTest.NewtonianFlightMath` and `SOLTest.PlayerFlight` still green.
4. [x] Measure one full `PlanChase` (45 predictions) in a test-only timing log over the contract's scenarios; note the
   numbers here (budget 0.1 ms). `SOLTest.Chase.PlanTiming`, Development editor, mean of 200 calls: short 2 km
   22.2 us, medium 200 km 41.2 us, long 1e9 m 42.3 us, under-floor 200 km 41.7 us. Within budget; no coarse-then-refine
   fallback needed.

### 32b: setting and save schema 8 (persistence; opus, xhigh)

Brief must carry: the approved bump `SAVE_VERSION` 7 -> 8 only, `KEY_BINDING_LAYOUT_VERSION` unchanged at 7, "version
and migrate, never silently discard", and the exact field table of SDD section 3.10. The subagent reports exactly which
version constants and fields it changed.

1. [x] Tests first (separate subagent from the contract): `SOLSettingsModelTest` (default 0.25, clamp below 0 / above
   0.3, NaN -> 0.25, `AreEqual` sees a 0.01 change and ignores 5e-5), `SOLSettingsRulesTest` (schema is 8; 1-7 migrate;
   8 is used; 9 is ignored; a v7 save with non-default volumes, keys and overlay profile migrates to 8 keeping them and
   gets 0.25; migrating twice is a no-op; a v1 save reaches 8), `FlightModeMathTest` (layout 7 and schema 8; a v7 save
   gets no controls-changed notice).
2. [x] `FSettingsValues::ChaseSafetyMargin`, `USOLSettingsSave::ChaseSafetyMargin`, `SetValues` / `GetValues`,
   `Sanitized`, `AreEqual`, the 7 -> 8 migration step, the `SOL::Settings` constants and the schema comment.
3. [x] Settings page slider row "Chase Down safety margin" (percent display, 1% steps), `ESOLSettingsSlider::ChaseSafetyMargin`,
   menu text; settings subsystem exposes the applied value.
4. [x] Suite green; `-SOLMenuSmoke=all` (Settings page) unchanged apart from the new row.

### 32c: ship integration (opus, high)

1. [x] `ISOLTargetable::IsTargetAlive` (default true) and the dropped-target proxy override; `TargetingTest` case.
2. [x] `FSOLChaseOrder` / `USOLShipSubsystem::EngageChase`, `DisengageChase`, `IsChasing`, `GetChaseStatus` (for the
   HUD: name, distance, closing speed, predicted time, activity, G limit, floor, resume level, target lost flag);
   target resolution per frame (body / targetable), approach direction captured at engage, acceleration estimate,
   replan rule (`ShouldReplacePlan`, 0.25 s, meter and target-velocity triggers), the settings margin read at plan time.
3. [x] Fragments (`bChase` and the chase inputs in the control fragment, `FSOLChaseRunState` in the state fragment);
   processor: chase substeps (aim point, `ChaseGLimit`, `MakeChaseGuidanceParams`, clamp, allocation boosted only while
   the Tab boost toggle is on, Q10), the sim-time domain under warp, no arrival counted; the plan input's `bBoost` is
   the toggle at plan time.
4. [x] Exits: the AUTO cancel rule (unchanged), C, `ResetPilotState`, any other engage (`EngageAutopilot`,
   `EngageAutopilotPoint`), despawn, target lost. Console `sol.Chase <Body|Target|Off>`.
5. [x] Verify with the console: a body chase and a target chase in PIE, log lines of the activity, G and meter; suite
   green.

### 32d: menus and Esc (sonnet, medium; the action-table tests by a separate subagent first)

1. [x] `InteractMenusTest` extended from the contract (rows, blocks, counts) before the change; red.
2. [x] `SOLActionTable`: Chase Down row live for Star / Planet / Moon / Dwarf (after Follow) and DroppedTarget,
   placeholder for Ship; `ESOLActionBlock::AlreadyChasing`, `FSOLActionContext::bIsChased`; texts.
3. [x] `USOLInteractionSubsystem`: context `bIsChased`, execute Chase Down (does not change the selection, chase proxy for a dropped
   target, `EngageChase`), the HUD's target-lost note timer.
4. [x] Pawn: Esc (and Ctrl+F1) cancels a running chase instead of opening the pause menu.
5. [x] Suite green; `-SOLSmokeInteract` green.

### 32e: HUD and texts (sonnet, medium)

1. [x] HUD: CHASE marker and activity line, the cancel hint, the floor and resume ticks on the G bar, the burst
   warning rule, the target-lost note.
2. [x] `SOLMenuText` (INFO_PROSE paragraph, block text, HUD strings, Esc row), `GAME_MECHANICS.md` (sections 1, 2, 7a,
   8.1), `ARCHITECTURE.md` (Flight row, key types, frame order note, Interaction row).

### 32f: smoke, screenshots, cost (opus, high)

1. [x] `-SOLSmokeChase` (SDD section 5 checks, including the 100x / 1000x warp chase and the size-scaled standoff) at 1280x720 and 1000x800; screenshots looked at (burst, cool, hold,
   target lost).
2. [x] Cost: `SOL_ChasePlan` scope (`stat dumpave` / Insights) during a long and a short chase, plus the ship step under
   a 1 d/s warp chase; numbers into the SDD amendment.
3. [x] Regression smokes: Interact, Route, Input, Hud, Flight, MenuSmoke=all.

### 32g: review and close (opus, xhigh review; sonnet docs)

1. [x] Fresh adversarial performance / style review (report only; no blockers, 4 should-fix, 6 nits) with the changed files, SDD 32, STYLE_GUIDE,
   UE_PERFORMANCE_GUIDELINES and the TECH_DEBT items for autopilot / Follow / radial menu / settings.
2. [x] Fixes (SDD 32 Amendment 4); re-check against the SDD; full suite 1183/1183 and the smokes again.
3. [x] README gallery (user rule, 2026-11-02): 2-3 new screenshots of what #32 added, captured after the suite and
   smokes via standalone smoke flags or PIE (a sonnet / medium agent), saved under `Docs/Images/` (outside
   `Content/`), one-line caption each, in the README gallery section (created if missing); suggested shots: the Chase
   Down entry in the radial / list menu, and the HUD during a chase (G meter burst and the CHASE marker). Same commit.
4. [x] SDD 32 amendment (as built, numbers), STATUS (#32 done), TECH_DEBT, metrics (`Tools\UpdateMetrics.bat`), commit
   `Issue #32: ...` and push.

## Chase contract (part 32a, pure logic in `Flight/SOLChase.{h,cpp}`)

Frames and units as SDD 18: Unreal-handed universe frame, metres, m/s, seconds; `g0` = `SOLNewtonian::STANDARD_GRAVITY_MPS2`.
No allocation, no world access. Every function returns a defined value for NaN / infinite / negative inputs as stated.

### Constants (`SOL::Chase` in `SOLConstants.h`)

| Name | Value | Meaning |
|---|---|---|
| `MIN_METER_FLOOR` | 0.01 | Technical floor under any margin |
| `COOL_HYSTERESIS_METER` | 0.15 | A cool off ends this far above floor + reserve |
| `COOL_SWITCH_TOLERANCE_METER` | 0.01 | A burst switches to cooling this close above floor + reserve |
| `PLAN_G_LEVELS` | 8 | Grid steps between sustained and the cap (9 levels; pairs with brake <= burst: 45) |
| `PLAN_TIE_FRACTION` | 0.005 | Predicted times this close are a tie |
| `REPLAN_INTERVAL_S` | 0.25 | Real seconds between periodic replans |
| `REPLAN_METER_DELTA` | 0.05 | Meter off the prediction by more: replan now |
| `REPLAN_TARGET_DV_FRACTION` | 0.1 | Target velocity change (of the closing speed, at least 1 m/s): replan now |
| `REPLAN_IMPROVEMENT_FRACTION` | 0.02 | A new plan must be this much faster to replace a feasible one |
| `TAIL_MIN_SPEED_MPS` | 1.0 | Slower than this relative to its frame: no tail direction, use the approach side |
| `TARGET_TRAIL_M` | 200.0 | Minimum trail distance behind a targetable |
| `TARGET_TRAIL_RADIUS_FACTOR` | 3.0 | Tunable k: trail = max(`TARGET_TRAIL_M`, k x radius) |
| `REPLAN_MAX_SIM_S` | 30.0 | Sim seconds between replans under time warp (tunable) |
| `TARGET_ACCEL_FILTER_S` | 0.5 | Low-pass time constant of the target acceleration estimate |
| `PLAN_MAX_SEGMENTS` | 4096 | Segment cap of one prediction (beyond it: not intercepted) |

`SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT` 0.25, `_MIN` 0.0, `_MAX` 0.30, `_STEP` 0.01.

### Types

- `enum class ESOLChaseActivity : uint8 { Burst, Cool, BrakeBurst, BrakeCool, Hold }`.
- `FSOLChasePolicy { double BurstG; double BrakePlanG; double FloorMeter; double ResumeHysteresis = COOL_HYSTERESIS_METER; }`.
- `FSOLChaseRunState { bool bCooling = false; double LastLimitG = 0; ESOLChaseActivity LastActivity = Burst; }`.
- `FSOLChaseGLimit { double LimitG; ESOLChaseActivity Activity; double ReserveMeter; double ResumeMeter; }`.
- `FSOLChasePlanInput { double DistanceM; double ClosingSpeedMps; double GravityMps2; double CruiseCapMps;
  double ArrivalRadiusM = 10; double ArrivalSpeedMps = 1; FSOLThrustEnvelope Envelope; FSOLPilotGLimits Limits;
  FSOLGMeterState Meter; double FloorMeter; bool bBoost = true; }` (the flip time comes from `MakeChaseGuidanceParams`;
  `bBoost` is the Tab boost toggle, Q10, and sets the cap through `ChaseMaxG`).
- `FSOLChasePrediction { bool bIntercepts; double TimeS; double MinMeter; double EndMeter; }`.
- `FSOLChasePlan { bool bValid; FSOLChasePolicy Policy; FSOLChasePrediction Prediction; }`.

S = `SustainedG`, M = `MaxG`, T = `TimeToEmptyAtMaxGS`, p = `DrainExponent`, R = `RecoverPerS`.

### Functions and cases

1. **`double ChaseMaxG(const FSOLThrustEnvelope&, const FSOLPilotGLimits&, bool bBoost)`**: min(M, forward x (bBoost ?
   BoostMultiplier : 1) / g0, total cap / g0 when > 0), at least S. `bBoost` is the player's Tab boost toggle (Q10).
   Cases: default player, boost on = 9 (within 1e-9); default player, boost off = 3 (the unboosted cap, Q10); M 12 with
   forward 5 G x3 and cap 20 G, boost on = 12; the same with boost off = 5; M 12 with cap 10 G, boost on = 10; cap <= 0
   ignored; BoostMultiplier 1 and forward 3 G, boost on = 3; M below S = S; NaN M = S. No literal 9 anywhere.
2. **`double EffectiveMeterFloor(double margin)`**: clamp [0, 0.3], at least 0.01; NaN -> 0.25. Cases: 0 -> 0.01, 0.1,
   0.25, 0.3, 0.5 -> 0.3, -1 -> 0.01, NaN -> 0.25.
3. **`double MaxSafeGForStep(const FSOLGMeterState&, double floor, const FSOLPilotGLimits&, double dt)`**: the largest G
   whose drain over dt leaves the meter at or above the floor: `S + (M - S) * (((meter - floor) / dt) * T)^(1/p)`
   (uncapped: the caller caps); S when the meter <= floor, in G-LOC, or for dt <= 0 / non-finite. Cases (default
   limits): full meter, floor 0.1, dt 10 -> 8.615 (+-1e-3); dt 1 -> 24 (> 9); dt 169 -> 3.23 (+-0.01); meter = floor -> S;
   round trip: `StepGMeter(meter, result, limits, dt).Meter >= floor - 1e-9` and not G-LOC, for a grid of meters (0.05-1),
   floors (0.01-0.3) and dts (0.01-300 s); never below S.
4. **`double BrakeReserveMeter(double closingSpeedMps, double burstG, double brakePlanG, double brakeAccelMps2,
   const FSOLPilotGLimits&)`**: with `f = clamp((brakePlanG - S) / (burstG - S), 0, 1)` (f = 0 when burstG <= S), returns
   `max(0, f * GDrainPerS(burstG) - (1 - f) * R) * max(0, v) / a_bp`; +inf when a_bp <= 0 and that rate is positive.
   Cases: burst 9, plan 9, 300 m/s, a_bp 79.434 -> 0.3777 (+-1e-3); burst 9, plan 5.25, 300 m/s, a_bp 46.337 -> 0.1619
   (+-1e-3); burst 9, plan 3.0 -> 0 (sustainable duty); plan S -> 0; receding (v < 0) -> 0.
5. **`FSOLGuidanceParams MakeChaseGuidanceParams(const FSOLThrustEnvelope&, double limitG, double brakePlanG,
   double gravityMps2, bool bBoost)`**: `PlanAccelMps2 = max(0.1 * a, 0.9 * a - g)` with `a = limitG * g0` capped by the
   forward thrust (boosted only when `bBoost`) and the total cap; `BrakeAccelMps2` the same from `brakePlanG`; flip time
   as `MakeGuidanceParams(..., bFlipToBrake true, bBoost)`; other fields default. Cases: limit 1.5 G, plan 1.5 G, no
   gravity, boost on -> both equal `MakeGuidanceParams(env, 1.5 g0, 0, true, true).PlanAccelMps2`; limit 9 G, boost on
   -> plan 0.9 x 88.26 (+-1e-6); limit 9 G, boost off on the default envelope -> capped at the unboosted 3 G forward
   (0.9 x 3 g0); gravity above 0.9 a -> the 10% floor; brake differs from plan when the levels differ; limit 20 G on
   the default envelope, boost on -> capped at 9 G.
6. **`FSOLChaseGLimit ChaseGLimit(const FSOLChasePolicy&, FSOLChaseRunState& inOutRun, const FSOLGMeterState&,
   ESOLGuidancePhase, double closingSpeedMps, double brakeAccelMps2, double capG, const FSOLPilotGLimits&, double dt)`**:
   SDD 32 section 3.4. Reserve = `BrakeReserveMeter(v, BurstG, BrakePlanG, brakeAccelMps2)`; resume = floor + reserve +
   hysteresis. Bursting: if the meter <= floor + reserve + `COOL_SWITCH_TOLERANCE_METER`, start cooling. Cooling: if the meter >= resume, stop cooling.
   If resume > 1, cooling is forced (no burst possible). Activity from the phase (Accelerate / Cruise -> Burst or Cool;
   Brake -> BrakeBurst or BrakeCool; Arrived -> Hold, never cooling-forced unless the meter <= floor + reserve). Limit:
   BurstG for Burst / BrakeBurst / Hold, S for Cool / BrakeCool; then min with `capG`; then min with
   `MaxSafeGForStep(meter, floor + reserve, dt)` for Burst / Hold or `MaxSafeGForStep(meter, floor, dt)` for BrakeBurst;
   never below S. G-LOC: `AutopilotRecoveryG`, activity Cool. `inOutRun.LastLimitG` / `LastActivity` are set. Cases:
   full meter, Accelerate -> Burst at BurstG; meter at floor + reserve -> Cool at S and `bCooling` set; cooling below
   resume -> Cool; reaching resume -> Burst and `bCooling` cleared; Brake phase not cooling -> BrakeBurst at BurstG,
   cooling -> BrakeCool at S; Arrived -> Hold; meter below the floor -> S in every activity; BurstG above the cap -> the
   cap; floor + reserve + hysteresis > 1 -> Cool at S from a full meter. **Safety property:** from a start meter >= floor,
   2,000 random steps (dt 0.01-300 s, random phases, speeds and policies within [S, cap], fixed seed), each followed by
   `StepGMeter` with the returned limit, never end below `floor - 1e-9` and never reach G-LOC.
7. **`FSOLChasePrediction PredictChase(const FSOLChasePlanInput&, const FSOLChasePolicy&)`**: closed-form segment
   prediction of SDD 32 section 3.5 (accelerate as burst / cool cycles with the item 6 thresholds, the cruise cap, the
   flip coast at 0 G, the brake as uniform `a_bp` with the meter at the duty cycle's net rate, recovery capped at 1).
   `bIntercepts` false (TimeS = DBL_MAX) beyond `PLAN_MAX_SEGMENTS` or for invalid input (NaN, negative distance).
   MinMeter is the lowest predicted meter; a policy that would go below the floor still predicts (the planner rejects
   it). Cases: policy (S, S) from rest at 100 km, no gravity, full meter -> within 1% of
   a 1-D 0.02 s sustained-autopilot flight, and >= `EstimateTimeToArrivalS(1e5, 0, plan, plan, flip, cap)` with the
   1.5 G plan (SDD 32 Amendment 1); already there (d <= 10 m, |v| <= 1 m/s)
   -> 0 with EndMeter = start meter; receding target -> finite and longer than the same distance from rest; (S, S) never
   changes the meter except recovery; burst 9 / plan 9 at 1e9 m -> MinMeter below the floor (an unfundable brake);
   **consistency with execution**: for a short chase (2 km from rest) and a medium one (200 km from rest), full meter,
   floor 0.1, the PlanChase policy, TimeS within 10% and EndMeter within 0.1 of a 0.02 s-step 1-D simulation that
   composes `ChaseGLimit`, `MakeChaseGuidanceParams`, `ComputeRendezvousGuidance` (x axis only, no gravity),
   `ClampToPilotLimit` and `StepGMeter`, run until the SDD 18 arrival tolerance.
8. **`FSOLChasePlan PlanChase(const FSOLChasePlanInput&)`**: the grid search over BurstG and BrakePlanG <= BurstG (9
   levels: S + k (cap - S) / 8), feasible = intercepts and MinMeter >= min(floor, start meter) - 1e-9 (a pilot already
   under the floor only has to not go lower); the smallest TimeS, ties within 0.5% to the lower BurstG, then the lower
   BrakePlanG. Cases: never slower than the (S, S) prediction; short chase (2 km from rest, full meter, floor 0.1) ->
   BurstG > S and time <= 0.75 x the (S, S) time; long chase (1e9 m from rest, full meter, floor 0.1) -> time <= 0.7 x
   the (S, S) time and BurstG below the cap; meter below the floor (0.05, floor 0.1) -> `bValid` true and the first
   `ChaseGLimit` of the policy (full run state) is S; floor 0.3 -> MinMeter >= 0.3 and
   time >= the floor-0.1 time; **upgrade monotonicity**: raising M (with an envelope that allows it), T or S never makes
   the time longer; M = S -> policy (S, S); invalid input (NaN or negative distance) -> `bValid` false and policy (S, S);
   deterministic (two calls give equal plans).
9. **`bool ShouldReplacePlan(const FSOLChasePlan& current, const FSOLChasePlan& candidate)`**: true when the current is
   invalid or not intercepting and the candidate is valid; true when both are valid and the candidate is faster by more
   than 2%; false otherwise. Cases: 1.5% faster -> false; 3% faster -> true; current invalid -> true; candidate invalid
   -> false.
10. **`FVector3d ChaseAimPointM(const FVector3d& targetM, const FVector3d& targetRelFrameVelocityMps,
    const FVector3d& approachDir, double trailM)`**: `targetM - unit(relVel) * trail` when |relVel| > 1 m/s, else
    `targetM + unit(approachDir) * trail`; a zero or non-finite approach dir -> +Z; a negative or NaN trail -> 0 (the
    target itself). Cases: each branch, the 1 m/s boundary (exactly 1 -> approach side), the fallbacks.
12. **`double ChaseTrailM(double radiusM, double minTrailM, double k)`**: `max(minTrailM, k * radiusM)`; NaN or negative
    radius or NaN k -> `minTrailM`. Cases: radius 10 m -> 200; radius 5 km, k 3 -> 15 km; boundary radius 200/3 -> 200.
11. **`FVector3d EstimateTargetAccelMps2(const FVector3d& prevVelocityMps, const FVector3d& velocityMps,
    const FVector3d& prevEstimateMps2, double dt, double capMps2)`**: blend `prev + (diff / dt - prev) * (1 - exp(-dt /
    0.5))`, length clamped to `capMps2`; dt <= 0 or non-finite -> the previous estimate. Cases: constant velocity -> decays
    toward 0; a steady 5 m/s^2 change -> 5 within 1% after 5 s of 1/60 s steps; a teleport-sized jump -> length = cap.

13. **`FSOLGuidanceParams MakeChasePhaseProbeParams(env, policy, gravity, bBoost)`**: params for a probe
    `ComputeRendezvousGuidance` call (`bComputeEta` false) that gives the guidance phase and closing speed fed to
    `ChaseGLimit`; the real guidance then uses `MakeChaseGuidanceParams(env, limit.LimitG, BrakePlanG, ...)`. The phase
    does not depend on the plan budget.

### Action table contract (part 32d)

- Rows: Star / Planet / Moon / Dwarf: Target, Info, Move To, Follow, **Chase Down** (right after Move To / Follow), Jump To, Match speed,
  Communicate (placeholder), Trade (placeholder) (9). Asteroid unchanged (6). Ship: Target, Info, Chase Down
  (placeholder), Follow (placeholder), Communicate (placeholder), Trade (placeholder) (6). DroppedTarget: Target, Info,
  Attack, **Chase Down (live)** (4).
- ChaseDown availability (non-placeholder rows): NoShip unless `bHasShip`; JumpWarping if `bJumpWarping`;
  NoPilotControl unless `bPilotHasControl`; AlreadyChasing if `bIsChased`. The Ship row's placeholder stays
  NotYetAvailable whatever the context.
- `ESOLActionBlock::AlreadyChasing` is appended before `Count`; `BlockText` gives "Already chasing it".

## Progress notes

- 2026-11-02: SDD and plan drafted from grill-me decisions 1-6; Q1-Q10 answered the same day and folded in (default margin 0.25, 1% step, size-scaled standoff, no auto-select, warp allowed, boost only when available). Ready for part 32a. No code yet.
- 2026-11-02 18:00 EST: Q10 corrected by the user (boost = the Tab toggle is on; `bBoost` added to contract items 1
  and 5 and to `FSOLChasePlanInput`). Approved persisted change restated for 32b: `SAVE_VERSION` 7 -> 8 with the
  migration step, new field `ChaseSafetyMargin` (default 0.25, range 0-0.30, 1% slider steps),
  `KEY_BINDING_LAYOUT_VERSION` unchanged at 7.
- **PAUSED before part 32a (2026-11-02 18:00 EST): 7-day usage at 89% (`~/.claude/rate-limits-cache.json`, fresh),
  above the project's 70% pause threshold.** The window resets Tue 2026-11-03 05:00 EST; resume at or after 05:10 EST.
  Resume state: nothing built, no code or test files touched; only the SDD/plan Q10 text edits above (uncommitted,
  with the untracked SDD and plan). Exact next step: 32a step 1, dispatch a separate opus/high subagent with the
  "Chase contract" section only to write `Source/SOLTest/Tests/ChaseTest.cpp`; then 32a step 2 (stubs, build, red).
  Test runs: headless `Tools\RunTests` is broken; build with the editor closed, relaunch the editor (Rider run config),
  run `Automation RunTests SOLTest` in the open editor, count `Test Completed` lines and wait for a new
  `Test Queue Empty` in `Saved/Logs/SOLTest.log` (baseline 1147 tests; smokes Hud, Input, Route, RouteOverlay, Map,
  MapPick, MapAlign, Hologram, Overlay, Interact green).
- 2026-11-03: **Part 32a built** (steps 1-4). `Flight/SOLChase.{h,cpp}` (contract items 1-12 plus
  `MakeChasePhaseProbeParams`, the phase probe a caller feeds to `ChaseGLimit`; the per-substep recipe is documented in
  `SOLChase.h`), the `SOL::Chase` constants, and compile-only declarations for 32b-32d (`ChaseSafetyMargin` in
  `FSettingsValues` / `USOLSettingsSave`, the `CHASE_SAFETY_MARGIN_*` constants, `ISOLTargetable::IsTargetAlive`,
  `ESOLActionBlock::AlreadyChasing` with its text, `FSOLActionContext::bIsChased`; no schema bump, migration, UI or
  behaviour). Stubs: 32 red (16 `SOLTest.Chase.*` plus the 32b-32d cases), no crash. Implemented: full suite 1173
  tests, 1156 pass; the 16 failures owned by 32b / 32d (`SettingsModel.ChaseSafetyMargin*`, `SettingsRules.*` schema 8,
  `FlightModeMath.LayoutChangedSince` / `SettingsSaveMigratesKeys`, `InteractMenus.*` rows) and one 32a case (below).
  Timing in step 4 (22-43 us per plan). Model amendments, to fold into SDD 32 section 3.5 when approved:
  - **Brake = the real braking curve.** The SDD 18 guidance never coasts the flip: it follows `v = a (u - tf)`, whose
    deceleration `a (1 - tf / u)` fades near the end, so the brake takes `(v - vEnd) / a + tf ln(v / vEnd)` (vEnd = the
    arrival speed), not `tf + v / a`. With the ideal ETA brake the prediction was 10.5 s for a 2 km chase flown in
    24.2 s (and end meters 0.4-0.7 off), failing the execution-consistency case. With the curve model: 2 km predicted
    24.09 s / flown 24.22 s; 200 km 164.56 s / 164.70 s, end meter 0.96 / 0.91. The brake meter integrates the curve's
    load (6-point Gauss-Legendre on two pieces); bursts drain at the flown load (90% of the level, the guidance margin).
  - **Open: `SOLTest.Chase.PredictChase` "(S, S) 100 km within 1% of EstimateTimeToArrivalS" fails** (predicted 191.3 s,
    ETA 173.8 s, +10%; a 1-D flight of the sustained autopilot takes 191.4 s). It contradicts the execution-consistency
    case; the test is left unchanged pending the user's call (proposed: compare against a 1-D sustained flight, or
    against ETA + the curve tail).
  - **`COOL_SWITCH_TOLERANCE_METER` = 0.01** (new constant): a burst cools off at floor + reserve + 0.01. With an exact
    test the safety clamp makes the meter approach the switch level forever (the guidance flies 90% of the limit), so
    the ship sat at about sustained G with the meter pinned instead of cycling (200 km: 173.9 s flown vs 164.7 s).
  - Steady cycles with no brake reserve are skipped in closed form (a quadratic in the cycle count), so the cost no
    longer grows with the trip length (1e9 m: about 11,000 cycles skipped per prediction).
- 2026-11-02: Contract amended (SDD 32 Amendment 1): item 7 first case and COOL_SWITCH_TOLERANCE_METER; ChaseTest updated by the spec-only test author; re-run pending in 32b.
- 2026-11-02 22:00 EST: **Part 32b built** (steps 1-4, not committed). Version change: `SOL::Settings::SAVE_VERSION`
  7 -> 8 only (schema comment "8: Chase Down safety margin, SDD 32"); `KEY_BINDING_LAYOUT_VERSION` unchanged at 7. Field
  `ChaseSafetyMargin` (model and save, from 32a) now carried by `SetValues` / `GetValues`, clamped to [0, 0.30] with NaN
  -> 0.25 in `Sanitized`, compared in `AreEqual` (1e-4); migration step `if (Version == 7) { ChaseSafetyMargin = 0.25;
  Version = 8; }`, every other field kept. No new write point. Settings page: `ESOLSettingsSlider::ChaseSafetyMargin`
  (after `HudEffects`), row "Chase Down safety margin" (`SOLMenuText::SETTINGS_CHASE_SAFETY_MARGIN`, percent, 1% steps)
  in Gameplay right after "Mouse steering at start"; `USOLSettingsSubsystem::GetChaseSafetyMargin()` returns the
  applied (saved) value for 32c. No test added or changed (the existing suite has no per-slider order or text test).
  Full suite 1173 tests, 1167 pass: the 6 failures are the 32d `InteractMenus.*` rows (BuildBlocks, BuildRowOrder,
  ChaseDownRows, EvaluateChaseDown, OffersAndPlaceholders, RowCount); `SOLTest.Chase.*` 18/18 including the amended
  `PredictChase`; SettingsModel 21/21, SettingsRules 23/23, FlightModeMath 40/40. `-SOLMenuSmoke=all` 47/50: "thrust
  flies the ship" is the known failure; "Esc resumes" and "Ctrl+F1 resumes" fail on `cursor 1` because the smoke's
  flight check still expects a hidden cursor while flight starts with mouse steering off and a free cursor since issue
  #31 (the last full run, 2026-10-27, predates #31); unrelated to the settings. The smoke's Settings screenshots are
  unchanged (the Gameplay section is below the fold); a manual standalone run scrolled to it shows the new row at 25%,
  a track click giving "10%" with "Unapplied changes", and Revert back to 25%. That run also migrated the real player
  slot (v7) in memory and wrote it back at the first write point: only the header's custom-version order and the
  Version byte (7 -> 8) changed, every property byte identical; the original v7 file was restored afterwards.
  Next: part 32c.
- 2026-11-03: **Part 32c built** (steps 1-5, not committed). `IsTargetAlive` override and a new
  `ISOLTargetable::GetTargetFrameBodyIndex` (dropped target: its combat frame body); `FSOLChaseOrder`,
  `FSOLChaseTargetSample`, `FSOLChaseStatus` and `EngageChase` / `DisengageChase` / `IsChasing` / `GetChaseOrder` /
  `GetChaseStatus` / `GetChaseTargetLostCount` on `USOLShipSubsystem`; per-frame target resolution and the replan
  triggers in `UpdateChase` (before the flight run); chase inputs in `FSOLShipControlFragment`, `ChaseRun` /
  `ChaseLimit` in `FSOLShipStateFragment`; `SOLFlight::ChaseStep` (shares the autopilot's nose / clamp / allocation
  helper) flown by `USOLShipFlightProcessor` in the autopilot's time domain, no arrival counted. Every autopilot cancel
  ends a chase; `EngageAutopilot` / `EngageAutopilotPoint` replace it; a lost target ends it in ASSISTED. Console
  `sol.Chase <Body [Name]|Target|Drop|Off|Status>` and the chase proxy (`ChaseDroppedTarget`) in
  `USOLInteractionSubsystem`, ready for 32d. Choices and PIE numbers in SDD 32 Amendment 2 (dropped target: bursts to
  9 G limit / 7.96 G load, meter 0.26-0.40 against the 0.25 floor, hold at 200 m; Moon: 6.19 G bursts). Full suite
  1173, all green except the 6 `InteractMenus.*` cases owned by 32d. Pending tests for the new pure helper
  `SOLFlight::ChaseStep` and the `GetTargetFrameBodyIndex` default (contract in the 32c report). Next: part 32d.
- 2026-11-03: Part 32d built. Chase Down rows live (bodies after Follow, 9 rows; dropped target 4), `bIsChased` filled,
  Execute ChaseDown (selection untouched), `USOLInteractionSubsystem::IsChaseTargetLostNoteShown() const` (lazy timestamp
  of the ship's loss count, `SOL::Chase::TARGET_LOST_NOTE_S` = 3 s), pawn `HandlePause` cancels a running chase first
  (Esc and Ctrl+F1). Full suite 1173/1173 green; `-SOLSmokeInteract` 18/18 at 1280x720 and 1000x800. Next: 32e (HUD, texts).
- 2026-11-03: Part 32e built (not committed). HUD: CHASE marker + activity line + cancel hint replace the AUTOPILOT marker
  (`ASOLFlightHud::DrawPilotBlock`; new line `ESOLHudLine::ChaseActivity`), floor tick and cooling resume tick on the G
  bar (the bar was already orange above sustained), target-lost note (`IsChaseTargetLostNoteShown`). New pure
  `SOLHudFormat::AppendSpeedMps`, `AppendChaseMarker`, `AppendChaseActivity`; strings in `SOLMenuText` (`CHASE_*`);
  Info prose: new Chase Down paragraph, "Greyed entries" sentence (Chase Down on a ship only), Esc control row.
  `GAME_MECHANICS.md` sections 1, 2 (Chase Down and Chase HUD rows), 7a, 8.1 and `ARCHITECTURE.md` updated. Full suite
  1180 run: 1179 pass; `SOLTest.ChaseStep.BadDtLeavesStateUnchanged` FAILS (boost, dt 0 and -0.5: limit output not
  default, ChaseStepTest.cpp line 209) - for the 32c owner to fix; `-SOLSmokeHud` 24/24 at 1280x720. Next: 32f.
- 2026-11-03: **Part 32f step 1 done** (not committed). Fix: `SOLFlight::ChaseStep` now resets its `outLimit` to the
  default on entry (it was only written after the dt check), so `SOLTest.ChaseStep.BadDtLeavesStateUnchanged` passes.
  New smoke `-SOLSmokeChase` (`Interaction/SOLChaseSmoke.{h,cpp}`, `FSOLChaseSmoke` plus the smoke-only stand-in
  target `USOLChaseSmokeTarget`; pawn wiring; `SOL::CommandLine::SMOKE_CHASE`; smoke settings slot). 25/25 checks at
  1280x720 and at 1000x800 (and 25/25 with `t.MaxFPS 15`). Measured (1280x720): radial engage, selection kept; first
  load above sustained after 0.0 s; 20 km chase on the tail after 51.0 s vs 56.5 s predicted (limit 70.7 s), peak
  limit 9.0 G / load 6.3 G; 10 s hold at 200.0 m (trail 200 m), no arrival counted; receding 2 km/s starts: 30% margin
  min meter 0.310, 25% margin min meter 0.260 (128 s vs 141-142 s predicted); stand-in radius 1 km held at 2999.2 m
  (trail 3 km); 100x / 1000x on the target: HOLD, within 202.3 m of the target; Moon at 1000x: HOLD on every measured
  frame, aim within 3 m (5 m at 15 fps), min meter 0.265; boost off: limit 3.00 G, load 2.54 G; boost on: limit 7.12 G;
  target shot down: ASSISTED with the note; the meter never under the floor on 103770 chase frames. The 32c 1000x
  item (hover 14 m off the tail, BURST instead of HOLD) did not reproduce at 15-220 fps, so no warp fix (SDD 32
  Amendment 3). Screenshots looked at, both sizes: marker, ticks and note legible and clear of the other panels.
  Full suite 1183/1183 green. Next: 32f step 2 (cost).
- 2026-11-03: **Part 32f steps 2-3 done** (not committed). Cost: new game-thread cycle stats in
  `USOLShipSubsystem` (`STATGROUP_SOLShip`: `SOL_ChasePlan`, `SOL_ChaseUpdate`, `SOL_ShipFlightRun`) and the smoke
  option `-SOLSmokeChaseProfile` (`SOL::CommandLine::SMOKE_CHASE_PROFILE`; `stat dumpsum` / `dumpmax` over 1000 frames
  in the 20 km chase, the Moon chase, and two profile-only steps: a Moon chase and a Move To Neptune at 1 d/s).
  PlanChase per call: 37 / 41 us (mean / max) at 20 km, 49 / 74 us for the Moon, 16 / 85 us at 1 d/s; 3.8 plans/s at
  1x, one per frame at 1 d/s (the 30 sim s replan timer). Ship flight run at 1 d/s: chase 86 us/frame vs autopilot
  83 us/frame; the chase update adds 25 us/frame. All under budget (SDD 32 Amendment 3 (e)). Regression smokes
  (1280x720 unless noted): Interact 18/18 (a first run 4/18, hover on, not reproduced: flaky), Route 26/26,
  RouteOverlay 25/25, Input 41/41, Hud 24/24 (and 24/24 at 1000x800), Flight 11/11, Map 16/16, MapPick 22/22,
  MapAlign 26/26, Hologram 12/12, Overlay 11/11 (a first run 7/11, Shift+O / Ctrl+O, not reproduced: flaky),
  MenuSmoke=all 47/50 (the 3 known failures), Chase 25/25 (and 25/25 with the profile flag). Full suite 1183/1183
  green in the editor. Next: 32g.
- 2026-11-03: **Part 32g steps 1-2 done** (not committed). Adversarial review: no blockers, 4 should-fix and 6 nits; all fixed (SDD 32 Amendment 4: `FSOLShipChaseFragment`, frame-relative target-turn trigger, Jump To ends the chase with the smoke check `z-jump-ends-chase` (26 checks), target-lost note timing, HUD name without a heap string). Full suite 1183/1183 and the smokes green. Docs synced (STATUS #32 done, TECH_DEBT items from the smokes, ARCHITECTURE). Next: step 3 (README gallery) and step 4 (metrics, commit, push).
- 2026-11-03: **Part 32g done.** Regression smokes after the review fixes: RouteOverlay 25/25, Map 16/16, MapPick
  22/22, MapAlign 26/26, Hologram 12/12, Overlay 11/11, Flight 11/11, Interact 18/18, Hud 24/24, Route 26/26, Chase
  26/26 at 1280x720 and 1000x800, MenuSmoke=all 47/50 (the 3 known failures). Input first read 40/41 (`j-thrust-up`)
  twice: not a regression; the test harness re-focused the game window every ~30 s, which flushes held keys
  (`UGameViewportClient::LostFocus` -> `FlushPressedKeys`) and landed in the Left Shift phase; without the re-focus
  Input is 41/41 on three runs. README gallery: three shots in `Docs/Images/` (radial menu, burst, cool-off). Full
  suite 1183/1183. Committed and pushed; issue #32 closed.
