<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 18 — Newtonian flight core, pilot G limits and NPC motion rework

Design: [`Docs/SDDs/18-newtonian-flight-core.md`](../SDDs/18-newtonian-flight-core.md). The round-1 open questions
are resolved (SDD section 2). The 18a contract holds for the recommended answers to the new questions N1-N7. If an
answer changes a contract value, update the contract and the matching test together.

- [x] **18a — Pure logic, test-first.** Done 2026-10-21: 66/66 `SOLTest.NewtonianFlightMath.*` pass after SDD 18
  Amendments 1-2 (one law fix, one test threshold fix, plus a compile fix renaming a test lambda `check`, a UE macro). Build `Source/SOLTest/Flight/SOLNewtonianFlight.{h,cpp}` to the contract
  below. The tests are `Source/SOLTest/Tests/NewtonianFlightMathTest.cpp` (`SOLTest.NewtonianFlightMath.*`). They
  were written on 2026-10-21 against this contract (rewritten after the round-2 answers), before any production code
  existed. Make them pass without editing them. If a test contradicts the contract, stop and ask.
- [x] **18b — Player integration.** Done 2026-10-21 (uncommitted, awaiting 18c/18d). 226/226 pass in the editor
  (NewtonianFlightMath 66, PlayerFlight 13, FlightStep 32, SurfaceLock 31, HudFormat 11, InfoText 12, KeyBindings 12,
  MenuState 24, SettingsModel 13, SettingsRules 12). Smokes: `-SOLSmokeFlight` 11/11 (with the G-LOC and the 1000x
  autopilot phases), `-SOLSmokeLevel` 21/21, `-SOLSmokeInput` 29/29, `-SOLSmokeHud` 17/17, after SDD 18 Amendment 4
  (vertical-nose `LevelForward`, smoke thresholds). Screenshots of the 9 G warning, G critical, blackout and the
  AUTOPILOT marker are in the session scratchpad (`n18shots/`). Not re-run: `-SOLSmokeJump`, `-SOLSmokeMap*`.
  - Add `FSOLThrustEnvelope` and `FSOLPilotGLimits` to `FSOLFlightParams` (keep `ParamsCrc` in sync), and add the
    per-ship `FSOLGMeterState` to the ship state fragment.
  - Turn `SOLFlight::Step` into the mode front end.
    - Assisted: velocity hold, then `ClampToPilotLimit` at sustained, then `AllocateThrust`, then the rate command.
    - Manual: thrust and torque fractions go in directly.
    - Stop spin on `Z`, through `SOLKeyBindings`, `KEY_BINDING_TEXTS` and the settings defaults.
  - Sum gravity in every mode. Each substep, compute the G-load (thrust plus resting contact) and run
    `StepGMeter`. Zero player input during G-LOC.
  - HUD:
    - the manual `OverSustained` warning, plus the Critical and Blackout states;
    - a `GVisionLoss` post-process (vignette and desaturation; material path in `SOLConstants.h`);
    - an `AUTOPILOT` marker with the ETA countdown (minimal; #19 owns the final look).
  - Surface-lock alignment becomes an attitude target, in assisted mode only.
  - Add the autopilot order on `FSOLShipControl`, plus `sol.Autopilot <Body|Target|Off>`.
    - Planning: `MakeGuidanceParams(env, AutopilotAccelLimitMps2(limits, meter), |g|, ...)`.
    - Under a warp the user chose: sim-time substeps of `max(10 s, frame sim time / 256)`, at most 256.
    - The autopilot never changes warp.
  - Re-validate `FlightStepTest` and `SurfaceLockTest`. Retune `-SOLSmokeFlight` and `-SOLSurfaceLockSmoke`, and add
    smoke phases:
    - a manual 9 G boost to G-LOC and back;
    - an assisted burn that stays at or below 1.5 G;
    - the autopilot under 1000x user warp, with the ETA counting down.
  - Update `GAME_MECHANICS.md`, `SOLMenuText` / Info / Controls and `ARCHITECTURE.md` (time domain, G meter).
  - Take PIE screenshots (vision loss, warning, AUTOPILOT marker).
- [ ] **18c — NPC rework.** (N5 answered: interceptor = AI drone.) Contract: "18c contract" below. Progress notes:
  - 2026-10-22: adversarial review fixes applied (SDD 18 Amendment 7, "Review-fix contract" below; uncommitted).
    Editor: NewtonianFlightMath, PlayerFlight, FlightStep, NpcAiMath, NpcRail, SurfaceLock, KeyBindings, InfoText,
    MenuState, HudFormat 254/254 (the last two groups re-run after the L6 revert: 87/87). Smokes: `-SOLSmokeFlight`
    11/11, `-SOLSmokeMapPick` 21/21, `-SOLSmokeJump` 14/14. Open: M2 (GPU far layer per-ship hide).
  - 2026-10-21: contract written (items 1-12, then 13 the gun gimbal); tests written first by separate opus agents;
    production built; SDD 18 Amendment 5 has the design as built and the measurements (1M ramp p95 8.47 ms, battle
    alive 2,101 of 4,000). `FSOLSurfaceLockParams::AlignTimeConstantS`, `ApplyAlignmentCorrection` and
    `SOL::SURFACE_LOCK_ALIGN_TIME_CONSTANT_S` removed with their tests. `-SOLSmokeJump` 14/14, `-SOLSmokeMap` 16/16;
    `-SOLSmokeMapPick` 17/21 (height-preview checks d2, e, f, f2 fail deterministically; not investigated whether it
    predates 18b; 18c touches no map or input code). Left for 18d: review, full suite, commit.
  - Extend `SOLNpcAi::FClassParams` with `ESOLPilotKind PilotKind`, the thrust box in G, `ClassGCapG` and the
    angular limits (SDD 3.7 table). The envelope comes from `WithGCap(..., EffectiveGCapG(kind, cap))`.
  - Add `FSOLNpcAttitudeFragment`, added on promotion to Full and removed on demotion.
  - Full AI: velocity hold, then `AllocateThrust`, then attitude, then `StepNewtonian` in the rail frame.
  - Mid: point-mass rendezvous back to the rail, budget `min(lateral, cap)`.
  - Add the `IsHandoffSnapInvisible` demotion gate, and use `OrientationFromForward` at Mid-to-Full.
  - Retire `LimitSteering` and its tests, and re-validate `NpcAiMathTest`. Re-run the 10d/10f measurements.
- [ ] **18d — Docs, review, verification.**
  - Update ARCHITECTURE, GAME_MECHANICS and the in-game Info.
  - Write the SDD amendments with `stat` / Insights measurements: player autopilot under warp, the G meter, Mid and
    Full.
  - Add a `Docs/ToDo/` item for the 3D-map autopilot trajectory toggle and the pilot G upgrades, unless #20 or the
    equipment ticket already covers them.
  - Run the adversarial opus review and fix its findings. Run the full suite, then `graphify update .`, then commit
    and push (`Issue #18: ...`).

**Map note (#20).** The 3D map must have a toggle that shows the autopilot's predicted trajectory. 18a provides the
pure sampler (`SampleRendezvousTrajectory`). #20 adds the target's own predicted motion and draws it.

## 18a contract

**Files.**

- Production: `Source/SOLTest/Flight/SOLNewtonianFlight.h/.cpp`, `namespace SOLNewtonian`, with `SOLTEST_API` free
  functions and plain structs.
  - No UObjects, no `USTRUCT`, no world access, no allocation, no global mutable state.
  - Includes `Flight/SOLFlight.h` (for `FSOLShipState`) and `SOLConstants.h`.
- Tests: `Source/SOLTest/Tests/NewtonianFlightMathTest.cpp`, paths `SOLTest.NewtonianFlightMath.<Case>`.

**Conventions.**

- Units are metres, seconds, radians and G (multiples of `STANDARD_GRAVITY_MPS2`).
- The frame is SDD 2's Unreal-handed universe frame. Local axes: +X forward, +Y right, +Z up. Angular velocity:
  X roll, Y pitch, Z yaw.
- Positive pitch raises the nose (forward gains +local up). Positive yaw turns right (forward gains +local right).
  Positive roll drops the right wing (up gains +local right).
- "Non-finite" means NaN or ±inf. `DBL_MAX` = `TNumericLimits<double>::Max()`.

### Constants (`inline constexpr double` in `SOLNewtonian`)

| Name | Value |
|---|---|
| `EPSILON` | 1e-9 |
| `STANDARD_GRAVITY_MPS2` | 9.80665 |
| `HUMAN_MAX_G` | 9.0 |
| `ATTITUDE_GAIN_PER_S` | 4.0 |
| `ATTITUDE_BRAKE_FRACTION` | 0.8 |
| `GUIDANCE_ACCEL_MARGIN` | 0.9 |
| `GUIDANCE_MIN_ACCEL_FRACTION` | 0.1 |
| `MIN_TIME_CONSTANT_S` | 1e-3 |
| `FORWARD_HOLD_FRACTION` | 0.05 |

### Types

```cpp
struct FSOLThrustEnvelope        // thrust CAPABILITY of the hull/engines (defaults: the player, SDD 18 3.2)
{
    double ForwardAccelMps2 = 3.0 * STANDARD_GRAVITY_MPS2;
    double ReverseAccelMps2 = 1.5 * STANDARD_GRAVITY_MPS2;
    double LateralAccelMps2 = 2.0 * STANDARD_GRAVITY_MPS2;
    double VerticalAccelMps2 = 2.0 * STANDARD_GRAVITY_MPS2;
    double BoostMultiplier = 3.0;                                   // every translation axis
    double MaxTotalAccelMps2 = 9.0 * STANDARD_GRAVITY_MPS2;         // |thrust accel| cap; <= 0 or non-finite = none
    FVector3d MaxAngularAccelRadPerS2 = FVector3d(4.0, 3.0, 3.0);  // X roll, Y pitch, Z yaw
    FVector3d MaxAngularRateRadPerS = FVector3d(2.0943951023931953, 1.5707963267948966, 1.5707963267948966);
    double MaxSpinRateRadPerS = 6.283185307179586;
    double MaxSpeedMps = SOL::MAX_SPEED_CAP_MPS;
};

struct FSOLNewtonianCommand { FVector3d ThrustFraction = 0; FVector3d AngularAccelFraction = 0; bool bBoost = false; };
struct FSOLThrustAllocation { FVector3d ThrustFraction = 0; FVector3d AchievedAccelWorldMps2 = 0; bool bSaturated = false; };

struct FSOLGuidanceParams
{
    double PlanAccelMps2 = 0.0;
    double BrakeAccelMps2 = 0.0;
    double FlipTimeS = 0.0;
    double MaxCruiseSpeedMps = SOL::MAX_SPEED_CAP_MPS;
    double ArrivalRadiusM = 10.0;
    double ArrivalSpeedMps = 1.0;
    double VelocityGainPerS = 2.0;
};
enum class ESOLGuidancePhase : uint8 { Accelerate, Cruise, Brake, Arrived };
struct FSOLGuidanceOutput
{
    FVector3d DesiredNetAccelMps2 = 0; FVector3d DesiredThrustAccelMps2 = 0; FVector3d DesiredForward = 0;
    ESOLGuidancePhase Phase = ESOLGuidancePhase::Arrived;
    double DistanceM = 0.0; double ClosingSpeedMps = 0.0; double SpeedLimitMps = 0.0;
    double EtaS = 0.0;                                   // NEW: estimated seconds to arrival; 0 when Arrived
};

struct FSOLPilotGLimits          // human pilot (upgradeable); defaults = untrained baseline
{
    double SustainedG = 1.5;
    double MaxG = 9.0;
    double TimeToEmptyAtMaxGS = 10.0;
    double DrainExponent = 2.0;
    double RecoverPerS = 0.05;
    double BlackoutDurationS = 5.0;
    double WakeMeter = 0.2;
    double CriticalMeter = 0.25;
    double VisionStartMeter = 0.4;
    double AutopilotRecoveryG = 1.5;
};
struct FSOLGMeterState { double Meter = 1.0; bool bBlackout = false; double BlackoutRemainingS = 0.0; };
enum class ESOLGWarning : uint8 { None, OverSustained, Critical, Blackout };
enum class ESOLPilotKind : uint8 { Human, AI };
```

### Functions: integrator, rotation, allocation (round 1, with the total cap added)

1. `FVector3d LocalThrustAccelMps2(thrustFraction, env, bBoost)`.
   - Each component: a non-finite value counts as 0, then it is clamped to [-1, 1].
   - Axes: `X >= 0 ? X * Forward : X * Reverse`, `Y * Lateral`, `Z * Vertical`, all times
     `bBoost ? BoostMultiplier : 1`.
   - **Then**, if `MaxTotalAccelMps2` is finite and > 0 and the result is longer than it, the result is scaled to
     that length.
2. `FSOLShipState StepNewtonian(state, command, env, gravityAccelMps2, dt)`.
   - Non-finite or `<= 0` dt returns the state unchanged. Non-finite command components or gravity count as 0.
   - `w += clamp(torque, -1, 1) * alpha * dt`, then each component is clamped to `±MaxSpinRateRadPerS`.
   - `v += (q_start.Rotate(LocalThrustAccelMps2) + g) * dt`, then `|v|` is clamped to `MaxSpeedMps` (direction kept).
   - `x += v_new * dt`.
   - `q *= rotation(w_new * dt)` (the conventions above), then normalized.
   - No damping. Spherical inertia (torque-free spin keeps its world axis).
3. `FVector3d RateCommandAngularFraction(w, commandRate, env, dt)`.
   - Zero if `dt` is non-finite or `<= 0`, or any vector is non-finite.
   - The command is clamped to `±MaxAngularRateRadPerS`. Per axis:
     `alpha_i <= 0 ? 0 : clamp((cmd_i - w_i) / (alpha_i * dt), -1, 1)`.
   - Stop spin is a zero command.
4. `FVector3d AttitudeRateCommand(q, desiredForwardWorld, desiredUpWorld, env)`.
   - Zero for a zero or non-finite forward, or a non-finite `q`.
   - Pitch and yaw errors come from the shortest arc from the nose to the normalized forward. If the angle exceeds
     `PI - 1e-6`, the error is pure positive pitch of PI.
   - Roll error is the signed angle about the current nose, from local +Z to the up's component perpendicular to
     the nose. It is 0 if that component is `<= EPSILON` or the up is non-finite or zero.
   - Per axis: `sign(e) * min(rate_i, sqrt(2 * 0.8 * alpha_i * |e|), 4 * |e|)`, and 0 when `alpha_i <= 0`.
5. `double RotationTimeS(angle, maxRate, alpha)`.
   - `theta = |angle|`. `theta == 0` gives 0.
   - Non-finite `theta`, `alpha <= 0` or non-finite, or `rate <= 0` or NaN gives `DBL_MAX`.
   - If `theta >= rate^2 / alpha`: `theta / rate + rate / alpha`. Otherwise (including rate = +inf):
     `2 * sqrt(theta / alpha)`.
6. `double StoppingDistanceM(speed, a, flipTime)`.
   - `v = |speed|`, `tf = max(0, flipTime)`; the result is `v * tf + v^2 / (2a)`.
   - `v == 0` gives 0.
   - A non-finite speed or flip time, or a non-finite or `<= 0` `a` with `v > 0`, gives `DBL_MAX`.
7. `double BrakingSpeedLimitMps(d, a, flipTime)` = `2d / (sqrt(tf^2 + 2d/a) + tf)`, with `tf = max(0, flipTime)`.
   - `d <= 0`, a non-finite `d`, `a <= 0` or non-finite, or a non-finite flip time gives 0.
8. `FSOLThrustAllocation AllocateThrust(desiredWorld, q, env, bBoost)`.
   - `local = q.Unrotate(desired)`. Per-axis limits as in function 1, times boost.
   - `s = min(1, min over axes with |local_i| > 0 of limit_i / |local_i|)`. An axis with limit 0 and a non-zero
     demand makes `s = 0`.
   - **Also**: if `MaxTotalAccelMps2` is finite and > 0, `s = min(s, MaxTotal / |desired|)`.
   - Fraction: `limit_i > 0 ? s * local_i / limit_i : 0`.
   - `Achieved = q.Rotate(LocalThrustAccelMps2(fraction))`, which equals `s * desired`.
   - `bSaturated = s < 1 - 1e-12`.
   - Non-finite input, or a zero request, gives zero and not saturated.
9. `FVector3d VelocityHoldAccelMps2(v, vTarget, g, tau, dt)` = `(vTarget - v) / max(tau, dt, MIN_TIME_CONSTANT_S) - g`.
   - A non-finite or negative `tau` or `dt` counts as 0 inside the `max`.
   - A non-finite vector gives zero.

### Functions: pilot G (new)

10. `double GLoadG(const FVector3d& properAccelMps2)` = `|a| / STANDARD_GRAVITY_MPS2`.
    - Non-finite input gives 0.
    - The caller passes the **proper** acceleration: thrust plus contact, never gravity.
11. `double GDrainPerS(double gLoad, const FSOLPilotGLimits& p)`.
    - 0 if `gLoad <= SustainedG` or `gLoad` is NaN.
    - Otherwise `pow((gLoad - SustainedG) / max(MaxG - SustainedG, EPSILON), DrainExponent) / max(TimeToEmptyAtMaxGS, EPSILON)`.
    - +inf G gives +inf.
12. `FSOLGMeterState StepGMeter(const FSOLGMeterState& s, double gLoad, const FSOLPilotGLimits& p, double dt)`.
    The state stays unchanged if `dt` is non-finite or `<= 0`, or `gLoad` is NaN. A negative `gLoad` counts as 0.
    - **Rate:** `r = gLoad <= SustainedG ? +RecoverPerS : -GDrainPerS(gLoad)`, constant over the step.
    - **Normal** (`!bBlackout`):
      - if `r < 0` and `Meter + r * dt <= 0`: the crossing time is `tc = Meter / -r` (0 when `r` is -inf or the
        meter is already 0). Then `Meter = 0`, `bBlackout = true`, `BlackoutRemainingS = BlackoutDurationS`, and the
        remaining `dt - tc` is processed as G-LOC;
      - otherwise `Meter = clamp(Meter + r * dt, 0, 1)`.
    - **G-LOC:**
      - `tTimer = BlackoutRemainingS`;
      - `tMeter = Meter >= WakeMeter ? 0 : (r > 0 ? (WakeMeter - Meter) / r : +inf)`;
      - `tWake = max(tTimer, tMeter)`.
      - If `tWake <= dt`: `Meter = clamp(Meter + r * tWake, 0, 1)`, `bBlackout = false`, `BlackoutRemainingS = 0`,
        and the remaining `dt - tWake` is processed as Normal.
      - Otherwise: `Meter = clamp(Meter + r * dt, 0, 1)` and `BlackoutRemainingS = max(0, tTimer - dt)`.
    - Because the meter is piecewise linear, one step of `T` equals many small steps covering `T` at the same
      `gLoad` (to rounding).
13. `ESOLGWarning GWarningLevel(const FSOLGMeterState& s, double gLoad, const FSOLPilotGLimits& p)`.
    - Blackout if `bBlackout`.
    - Otherwise Critical if `Meter <= CriticalMeter`.
    - Otherwise OverSustained if `gLoad > SustainedG`.
    - Otherwise None. A NaN `gLoad` counts as not over.
14. `double GVisionLoss(const FSOLGMeterState& s, const FSOLPilotGLimits& p)`.
    - 1 if `bBlackout`.
    - Otherwise `clamp((VisionStartMeter - Meter) / max(VisionStartMeter, EPSILON), 0, 1)`.
15. `bool PilotHasControl(const FSOLGMeterState& s)` = `!bBlackout`.
16. `double PilotSustainedAccelMps2(const FSOLPilotGLimits& p)` = `max(0, SustainedG) * STANDARD_GRAVITY_MPS2`. A
    non-finite value gives 0.
17. `double AutopilotAccelLimitMps2(const FSOLPilotGLimits& p, const FSOLGMeterState& s)`.
    - `(bBlackout || Meter <= CriticalMeter) ? min(SustainedG, AutopilotRecoveryG) : SustainedG`, times
      `STANDARD_GRAVITY_MPS2`, at least 0.
18. `FVector3d ClampToPilotLimit(const FVector3d& desiredThrustAccelMps2, double limitMps2)` =
    `ClampLength(desired, limit)`.
    - A non-finite desired vector, or a limit that is NaN or `<= 0`, gives zero. A limit of +inf gives `desired`
      unchanged.
19. `double EffectiveGCapG(ESOLPilotKind kind, double classCapG)`.
    - Human: `min(classCapG, HUMAN_MAX_G)`. AI: `classCapG`.
    - NaN or `<= 0` gives 0. An AI with +inf gives +inf.
20. `FSOLThrustEnvelope WithGCap(const FSOLThrustEnvelope& env, double capG)`.
    - A copy of `env` with `MaxTotalAccelMps2 = min(existing cap (+inf if none), capG * STANDARD_GRAVITY_MPS2)`.
    - A non-finite or `<= 0` `capG` leaves `env` unchanged.

### Functions: guidance, ETA, trajectory

21. `FSOLGuidanceOutput ComputeRendezvousGuidance(relPos = target - ship, relVel = ship - target, targetAccel, gravity, params, dt)`.
    The round-1 law, unchanged:
    - **Invalid inputs** (any non-finite input or params field, or non-finite or negative `dt`): the default
      output. A `PlanAccelMps2 <= 0` gives zero accelerations.
    - `d`, `dir`, `vc = dot(v, dir)`, `tfEff = max(0, FlipTimeS) + max(0, dt)`.
    - `curve = BrakingSpeedLimitMps(d, Brake, tfEff)`, `SpeedLimit = min(cap, curve)`, `vDes = dir * SpeedLimit`.
    - **Phase:**
      - Arrived if `d <= ArrivalRadius && |v| <= ArrivalSpeed`;
      - otherwise Brake if `curve < cap && vc > 0 && vc >= curve * (1 - 1e-6)`;
      - otherwise Cruise if `vc >= cap * 0.99`;
      - otherwise Accelerate.
    - **Correction:** `tau = max(gain > 0 ? 1 / gain : 0, dt, MIN_TIME_CONSTANT_S)`, `corr = (vDes - v) / tau`. In
      Brake, when `d > EPSILON` and `Brake > 0`, add `-k * dir * vc / sqrt(tfEff^2 + 2d / Brake)`, with
      `k = gain > 0 ? min(1, (1 / gain) / tau) : 1` (amended 2026-10-21, SDD 18 Amendment 1: `k = 1` for any
      `dt <= 1 / gain`, so only steps longer than the tracker's 0.5 s change).
    - **Clamp:** clamp the length to `A` (`max(Plan, Brake)` in Brake, `Plan` otherwise).
      `Net = targetAccel + relAccel`, `Thrust = Net - gravity`.
    - **DesiredForward:**
      - zero when Arrived;
      - otherwise `normalize(Thrust)` if `|Thrust| >= 0.05 * Plan` (and `> EPSILON`);
      - otherwise `-dir` if `vc > 0 && d <= StoppingDistanceM(vc, Brake, 2 * tfEff)`;
      - otherwise `dir`.
    - **New:** `EtaS = Arrived ? 0 : EstimateTimeToArrivalS(d, vc, Plan, Brake, FlipTimeS, cap)`.
22. `FSOLGuidanceParams MakeGuidanceParams(env, double pilotAccelLimitMps2, double gravityMagnitudeMps2, bool bFlipToBrake, bool bBoost)`.
    - `m = bBoost ? BoostMultiplier : 1`.
    - `limit = min(pilot limit if finite and > 0 else +inf, MaxTotalAccelMps2 if finite and > 0 else +inf)`.
    - `fwd = min(Forward * m, limit)`. `rev = min(Reverse * m, limit)`.
    - `g = finite and > 0 ? gravity : 0`.
    - `Plan = max(0.1 * fwd, 0.9 * fwd - g)`.
    - `Brake = flip ? Plan : max(0.1 * rev, 0.9 * rev - g)`.
    - `FlipTimeS = flip ? RotationTimeS(PI, rate.Y, 0.8 * alpha.Y) + 1 / ATTITUDE_GAIN_PER_S : 0`.
    - Every other field keeps the struct default.
23. `double EstimateTimeToArrivalS(double d, double vc, double aAccel, double aBrake, double flipTimeS, double cap)`:
    the 1-D braking-curve plan. Let `tf = max(0, flipTimeS)`.
    - **Invalid:** a non-finite input, or `aAccel <= 0`, `aBrake <= 0`, `cap <= 0` or `d < 0`, gives `DBL_MAX`.
    - (a) If `d <= EPSILON && |vc| <= EPSILON`: 0.
    - (b) **Receding** (`vc < 0`): `t0 = -vc / aAccel`, `d' = d + vc^2 / (2 aAccel)`; the result is
      `t0 + ETA(d', 0)`.
    - (c) **Too fast** (`vc > BrakingSpeedLimitMps(d, aBrake, tf)`): `tStop = tf + vc / aBrake`,
      `o = StoppingDistanceM(vc, aBrake, tf) - d`; the result is `tStop + ETA(o, 0)`.
    - (d) **Above the cap** (`vc > cap`, not too fast): `t1 = (vc - cap) / aAccel`,
      `d1 = (vc^2 - cap^2) / (2 aAccel)`; the result is `t1 + ETA(d - d1, cap)` (case e or f).
    - (e) **Peak below the cap:** with `A = 1/(2 aAccel) + 1/(2 aBrake)` and `C = d + vc^2 / (2 aAccel)`, the peak
      is `v = 2C / (tf + sqrt(tf^2 + 4AC))`. If `v <= cap`, the result is `(v - vc) / aAccel + tf + v / aBrake`.
    - (f) **Cruise:** otherwise the result is
      `(cap - vc) / aAccel + (d - (cap^2 - vc^2) / (2 aAccel) - cap * tf - cap^2 / (2 aBrake)) / cap + tf + cap / aBrake`.
24. `int32 SampleRendezvousTrajectory(const FVector3d& relativePositionM, const FVector3d& relativeVelocityMps, const FSOLGuidanceParams& params, double sampleIntervalS, int32 substepsPerSample, TArrayView<FVector3d> outShipMinusTargetM)`.
    - **Invalid:** non-finite vectors or params, `sampleIntervalS` non-finite or `<= 0`, `substepsPerSample < 1`, or
      an empty view gives 0.
    - **Start:** `p = -relativePositionM`, `v = relativeVelocityMps`, `h = sampleIntervalS / substepsPerSample`.
      `out[0] = p`, `count = 1`.
    - **Loop:** while `count < out.Num()`, for each substep:
      - `o = ComputeRendezvousGuidance(-p, v, 0, 0, params, h)`;
      - if `o.Phase == Arrived`: if `p != out[count - 1]`, write `out[count++] = p`; then return `count`;
      - otherwise `v += o.DesiredNetAccelMps2 * h`, then `p += v * h`.
    - **After the substeps:** `out[count++] = p`.
    - **Return:** `count`. The target is unaccelerated, gravity is ignored, and nothing is allocated.

**NPC hand-off** (round 1, unchanged):

25. `OrientationFromForward(forward, upHint, fallback)`.
    - A non-finite or zero forward returns `fallback.GetNormalized()`.
    - Otherwise X goes to `normalize(forward)` and Z to the normalized component of the hint perpendicular to it.
    - If the hint is degenerate (non-finite, or `<= 1e-6` after normalizing), the fallback chain is world +Z, then
      world +X. Then `Y = Cross(Z, X)`.
26. `IsHandoffSnapInvisible(offsetM, distanceM, pixelAngleRad)` = `offset <= 0.5 * pixel * distance`. A non-finite or
    negative input gives false.

**Test-only conventions.** Point-mass helpers use the same semi-implicit Euler (velocity, then position) and compute
gravity inline.

## 18b contract (player front end, pure logic in `Flight/SOLFlight.{h,cpp}`)

Written 2026-10-21 before the 18b code; the tests are `Source/SOLTest/Tests/FlightStepTest.cpp`
(`SOLTest.FlightStep.*`, re-validated for the new `Step`) and `Source/SOLTest/Tests/PlayerFlightTest.cpp`
(`SOLTest.PlayerFlight.*`, new). Conventions as in 18a.

**Types.**

- `FSOLShipState` moves unchanged to `Flight/SOLShipState.h`; `SOLNewtonianFlight.h` includes that, and
  `SOLFlight.h` includes `SOLNewtonianFlight.h`.
- `FSOLFlightParams` becomes: `AssistTimeConstantS = 0.75`, `FSOLThrustEnvelope Envelope` (player defaults),
  `FSOLPilotGLimits PilotLimits` (defaults), `BoostSpeedMultiplier = 2.0` (assisted: boost doubles the target speed),
  `MaxSpeedMps = 0.5c` (assisted target clamp and the speed-cap clamp), `MinSpeedCapMps = 1`, `ShipRadiusM = 10`.
  Removed: `NewtonianMainAccelMps2`, `NewtonianStrafeAccelMps2`, `BoostAccelMultiplier`, `MaxPitchYawRateRadPerS`,
  `MaxRollRateRadPerS`, `AngularResponseTimeS`, `AlignTimeConstantS`.
- `FSOLShipControl` gains `bool bStopSpin = false` and `FVector3d AttitudeUpWorld = 0` (assisted attitude target's
  desired up, zero = none; the processor fills it from surface-lock). `Rotation` is a rate-command fraction in
  assisted mode and a torque fraction in manual mode. `bFlightAssist` true = assisted, false = manual.
- `struct FSOLFlightStepOutput { FVector3d ThrustAccelWorldMps2 = 0; };` the achieved thrust acceleration of the
  step (world frame, start-of-step orientation), the G-load source.
- Constants in `SOLConstants.h`: `SOL::AUTOPILOT_MIN_SUBSTEP_S = 10.0`, `SOL::AUTOPILOT_MAX_SUBSTEPS = 256`,
  `SOL::AUTOPILOT_HINT_WARP = 86400.0` (1 d/s), `SOL::AUTOPILOT_HINT_RADII = 10.0`.

**Functions (`namespace SOLFlight`).**

1. `FVector3d AssistThrustAccelMps2(v, vTarget, g, tau, dt, limitMps2)`: the assisted thrust request with
   **gravity priority** (SDD 18 Amendment 3).
   - Any non-finite vector, or `limitMps2` NaN or `<= 0`, gives zero.
   - `hold = (vTarget - v) / max(tau', dt', MIN_TIME_CONSTANT_S)` (tau', dt' = 0 when non-finite or negative);
     `comp = -g`.
   - `|comp| >= limit`: return `comp` scaled to length `limit`.
   - Otherwise return `comp + s * hold` with the largest `s` in [0, 1] such that `|comp + s * hold| <= limit`
     (`s = 1` when it already fits; `limit = +inf` gives `comp + hold`).
2. `FSOLShipState Step(state, control, params, gravity, dt, FSOLFlightStepOutput* outStep = nullptr)`.
   - Non-finite or `<= 0` dt: state unchanged, output zero.
   - `env = params.Envelope`, `r` = `control.Rotation` with non-finite components 0, clamped to [-1, 1].
   - **Assisted:** rate command `rc_i = r_i * env.MaxAngularRateRadPerS_i`. If `AttitudeUpWorld` is finite and
     non-zero: `att = AttitudeRateCommand(q, LevelForward(q, up), up, env)`, and every axis with `r_i == 0` uses
     `rc_i = att_i`. `bStopSpin` makes `rc = 0`. Torque = `RateCommandAngularFraction(w, rc, env, dt)`.
     Translation: `t = ClampLength(clampUnit(Thrust), 1)`;
     `rel = ClampLength(q.Rotate(t) * SpeedCapMps * (bBoost ? BoostSpeedMultiplier : 1), params.MaxSpeedMps)`;
     `vT = ReferenceVelocityMps + rel`;
     `a = AssistThrustAccelMps2(v, vT, g, AssistTimeConstantS, dt, PilotSustainedAccelMps2(PilotLimits))`;
     thrust fraction = `AllocateThrust(a, q, env, bBoost).ThrustFraction`.
   - **Manual:** torque = `bStopSpin ? RateCommandAngularFraction(w, 0, env, dt) : r`; thrust fraction =
     `clampUnit(Thrust)` (no unit-length clamp: a diagonal burn uses several thrusters, then the total cap).
     `AttitudeUpWorld` is ignored.
   - Then `StepNewtonian(state, {fraction, torque, bBoost}, env, gravity, dt)`; output
     `q_start.Rotate(LocalThrustAccelMps2(fraction, env, bBoost))`.
   - So in assisted mode the achieved thrust never exceeds `SustainedG`; in manual mode it reaches the envelope (9 G
     boosted forward).
3. `FVector3d LevelForward(q, upWorld)`: the ship's forward projected on the plane perpendicular to the normalized
   up, normalized. When that projection is `<= 1e-6` long (nose along the up), the ship's up (negated when the nose
   points along the up, as is when against it) projected and normalized instead, so a vertical nose levels by the
   shortest pitch (amended after the 18b `-SOLSmokeLevel` run, where a nose-up ship never levelled). The ship's forward
   unchanged when the up is zero or non-finite, or both projections are degenerate.
4. `FVector3d ContactAccelMps2(outwardNormal, bodyGravityAccelMps2, thrustAccelWorldMps2)` =
   `n * max(0, -dot(bodyGravity + thrust, n))` with `n` the normalized normal; zero for a zero or non-finite input.
   (Resting on Earth without thrust gives 9.81 m/s² up: 1 G.)
5. `int32 AutopilotSubstepCount(frameSimS)` = `clamp(ceil(T / max(AUTOPILOT_MIN_SUBSTEP_S, T / AUTOPILOT_MAX_SUBSTEPS)),
   1, AUTOPILOT_MAX_SUBSTEPS)`; 0 for a non-finite or `<= 0` T. The substep is `T / count`.
6. `FSOLShipState AutopilotStep(state, targetPositionM, targetVelocityMps, targetAccelMps2, gravity, params, meter,
   dt, FSOLFlightStepOutput* outStep = nullptr, FSOLGuidanceOutput* outGuidance = nullptr)`.
   - Non-finite or `<= 0` dt: state unchanged, outputs default.
   - `limit = AutopilotAccelLimitMps2(PilotLimits, meter)`; `gp = MakeGuidanceParams(Envelope, limit, |g|, true,
     false)`; `o = ComputeRendezvousGuidance(target - x, v - vTarget, targetAccel, g, gp, dt)`.
   - Thrust fraction = `AllocateThrust(ClampToPilotLimit(o.DesiredThrustAccelMps2, limit), q, env, false)`.
   - Torque = `RateCommandAngularFraction(w, AttitudeRateCommand(q, o.DesiredForward, 0, env), env, dt)`.
   - `StepNewtonian(...)` with gravity; outputs the achieved thrust and `o`.
7. `bool IsAutopilotPrecisionReduced(warpFactor, distanceToBodyCenterM, bodyRadiusM)` =
   `warp > AUTOPILOT_HINT_WARP && distance < AUTOPILOT_HINT_RADII * radius`; false for non-finite input or a radius
   `<= 0`.

## 18c contract (NPC motion, pure logic in `Npc/SOLNpcAiMath.{h,cpp}` and `Npc/SOLNpcRail.{h,cpp}`)

Written 2026-10-21 before the 18c code. Tests: `Source/SOLTest/Tests/NpcAiMathTest.cpp` (`SOLTest.NpcAiMath.*`,
re-validated: the `LimitSteering*` cases and `MidLeashCoversClasses` are removed with the function and the Mid spring)
and `Source/SOLTest/Tests/NpcRailTest.cpp` (`SOLTest.NpcRail.*`). Conventions as in 18a; `g0` =
`SOLNewtonian::STANDARD_GRAVITY_MPS2`. `SOLNpcAi::LimitSteering` and `FClassParams::MaxAccelMps2` /
`MaxTurnRateRadPerS` are removed; `SOL::NPC_MID_DEVIATION_OMEGA` / `NPC_MID_DEVIATION_LEASH_M` are removed.
`SOLNpcAiMath.h` includes `Flight/SOLNewtonianFlight.h`.

**Constants (`inline constexpr double` in `SOLNpcAi`).** `FULL_HOLD_TIME_CONSTANT_S = 0.5`,
`NOSE_ON_TARGET_RANGE_SCALE = 1.5`, `HULL_HEADING_MIN_SPEED_MPS = 1.0`, `MID_ARRIVAL_RADIUS_M = 1.0`,
`MID_ARRIVAL_SPEED_MPS = 0.5`, `CLASS_SPIN_RATE_SCALE = 2.0`. In `SOLNpcRail`: `RAIL_VELOCITY_STEP_S = 0.5`.

**`FClassParams` gains** (after `MaxSpeedMps`): `ESOLPilotKind PilotKind`, `double ForwardG, ReverseG, LateralG,
VerticalG, ClassGCapG`, `FVector3d MaxAngularAccelRadPerS2` and `FVector3d MaxAngularRateRadPerS` (X roll, Y pitch,
Z yaw). The other fields stay. Class order and values (SDD 18 section 3.7):

| Index | Class | PilotKind | Fwd / rev / lat / vert G | ClassGCapG | Alpha (roll, pitch, yaw) | Rates rad/s |
|---|---|---|---|---|---|---|
| 0 | Fighter | Human | 8 / 4 / 4 / 4 | 6 | (6, 4, 4) | (3.0, 1.5, 1.5) |
| 1 | Gunship | Human | 4 / 2 / 2 / 2 | 4 | (2, 1.5, 1.5) | (1.2, 0.8, 0.8) |
| 2 | Interceptor | AI | 15 / 8 / 8 / 8 | 20 | (8, 6, 6) | (4.0, 2.5, 2.5) |

`FireConeRad` is no longer pinned (re-tuned by measurement in 18c); the invariants of `ClassValidity` stay, plus:
every G field, `ClassGCapG` and every angular component finite and > 0.

**Functions (`namespace SOLNpcAi`).**

1. `FSOLThrustEnvelope ClassEnvelope(const FClassParams& c)`: `Forward/Reverse/Lateral/VerticalAccelMps2` = the G
   fields times `g0`; `BoostMultiplier = 1`; angular accel and rate from the class; `MaxSpinRateRadPerS =
   CLASS_SPIN_RATE_SCALE * max component of MaxAngularRateRadPerS`; `MaxSpeedMps = SOL::MAX_SPEED_CAP_MPS`;
   `MaxTotalAccelMps2 = EffectiveGCapG(c.PilotKind, c.ClassGCapG) * g0` (that is `WithGCap` of an uncapped
   envelope). So the fighter's total cap is 6 G, the gunship's 4 G, the interceptor's 20 G; a Human class never exceeds
   `HUMAN_MAX_G`.
2. `double MidReturnAccelMps2(const FClassParams& c)` = `min(LateralG, EffectiveGCapG(kind, ClassGCapG)) * g0`; a
   non-finite or `<= 0` `LateralG` gives 0. Fighter 4 G, gunship 2 G, interceptor 8 G.
3. `FSOLGuidanceParams MidReturnGuidanceParams(const FClassParams& c)`: `PlanAccelMps2 = BrakeAccelMps2 =
   GUIDANCE_ACCEL_MARGIN * MidReturnAccelMps2(c)`, `FlipTimeS = 0`, `MaxCruiseSpeedMps = c.MaxSpeedMps`,
   `ArrivalRadiusM = MID_ARRIVAL_RADIUS_M`, `ArrivalSpeedMps = MID_ARRIVAL_SPEED_MPS`, other fields default.
4. `struct FMidReturnStep { FVector3d DeviationM = 0; FVector3d DeviationVelocityMps = 0; bool bAtRest = false; };`
   `FMidReturnStep StepMidReturn(deviationM, deviationVelocityMps, const FSOLGuidanceParams& params, double dt)`: the
   Mid tier's point-mass return to the rail (target = the rail: `relPos = -deviation`, `relVel = velocity`).
   - A non-finite deviation or velocity: both zero, at rest.
   - Both exactly zero: zero, at rest (a ship at rest on its rail; the processor skips it).
   - `dt` non-finite or `<= 0`: unchanged, not at rest.
   - `o = ComputeRendezvousGuidance(-deviation, velocity, 0, 0, params, dt)`. Arrived: both zero, at rest.
   - Otherwise `v' = v + o.DesiredNetAccelMps2 * dt`, `x' = x + v' * dt`, not at rest.
   - Hence `|v' - v| <= PlanAccelMps2 * dt` (to rounding), and the speed never exceeds `max(|v|, MaxCruiseSpeedMps)`
     by more than `PlanAccelMps2 * dt`.
   - (Found in 18c: at `dt` = 0.5 s = 1 / gain the braking law rang near the rail; fixed in the guidance by the
     continuous feedforward fade of the review-fix contract, and `StepMidReturnLongStep` requires rest at 0.5 s again.)
5. `FVector3d HullForward(const FVector3d& velocityMps, const FVector3d& upHint)`: the hull heading of a Mid ship and
   the nose a ship is promoted to Full with.
   - `normalize(v)` if `v` is finite and `|v| > HULL_HEADING_MIN_SPEED_MPS`;
   - else, if `upHint` is finite and `|upHint| >= STEER_EPSILON`, with `u = normalize(upHint)`:
     `normalize(Cross(u, |u.X| < 0.9 ? +X : +Y))`;
   - else `+X`.
6. `FQuat4d HandoffOrientation(const FVector3d& velocityMps, const FVector3d& upHint)` =
   `OrientationFromForward(HullForward(velocity, upHint), upHint, FQuat4d::Identity)`; its forward (local +X) equals
   `HullForward`.
7. `FVector3d FullShipDesiredForward(aimDirection, targetDistanceM, fireRangeM, desiredVelocityMps, velocityMps,
   currentForward)`: where a Full ship points its nose.
   - `normalize(aim)` when the aim is finite with `|aim| >= STEER_EPSILON`, `targetDistanceM` and `fireRangeM` are
     finite and `> 0`, and `targetDistanceM <= NOSE_ON_TARGET_RANGE_SCALE * fireRangeM` (guns on target);
   - else `normalize(desiredVelocity)` if finite and `|desired| > HULL_HEADING_MIN_SPEED_MPS`;
   - else `normalize(velocity)` if finite and `|velocity| > HULL_HEADING_MIN_SPEED_MPS`;
   - else `normalize(currentForward)` if finite and non-zero, else `+X`.
8. `FSOLShipState StepFullShip(state, desiredVelocityMps, desiredForward, desiredUp, const FSOLThrustEnvelope& env,
   double dt)`: one Full-tier step in the rail frame (no gravity).
   - `dt` non-finite or `<= 0`: state unchanged.
   - `a = VelocityHoldAccelMps2(state.Velocity, desiredVelocity, 0, FULL_HOLD_TIME_CONSTANT_S, dt)`;
     `fraction = AllocateThrust(a, q, env, false).ThrustFraction`;
     `torque = RateCommandAngularFraction(w, AttitudeRateCommand(q, desiredForward, desiredUp, env), env, dt)`;
     result = `StepNewtonian(state, {fraction, torque, false}, env, 0, dt)`.
   - Hence `|v' - v| / dt <= env.MaxTotalAccelMps2` and within the per-axis box (to rounding), the spin stays within
     the spin clamp, and with a constant desired forward the nose converges to it.
9. `bool AllowFarDemotion(double deviationLengthM, double distanceM, double pixelAngleRad, bool bBudgetForced)` =
   `bBudgetForced || IsHandoffSnapInvisible(deviationLengthM, distanceM, pixelAngleRad)` (the Mid-to-Far gate).

**Functions (`namespace SOLNpcRail`).**

10. `FVector3d EvaluateAtTimeM(rail, frame, double railTimeS)`: the rail position at `railTimeS` with the frame's
    group points held fixed. `EvaluateM(rail, frame) == EvaluateAtTimeM(rail, frame, frame.RailTimeS)` exactly.
11. `FVector3d VelocityMps(rail, frame)` = `(EvaluateAtTimeM(t + h) - EvaluateAtTimeM(t - h)) / (2h)`, `t =
    frame.RailTimeS`, `h = RAIL_VELOCITY_STEP_S`: the rail's velocity relative to its group's anchor points. Battle:
    exactly zero. Cluster: tangential, `|v|` = orbit radius times mean motion within 0.1%. Lane (away from a
    turnaround): `(B - A)` times the lane rate (`P1`) times +-1, within 1e-6 relative.
12. `FSOLNpcRailFrame` gains `TArray<FVector3d> GroupBodyM`, filled by `BuildFrame`: lane = body A's position,
    cluster = its body's, battle = the battle body's (zero for an invalid index). Used as the "up away from the group
    body" hint.
13. `FClassParams` gains a last field `double GunGimbalRad` (how far the guns swivel off the nose; the gunship's
    turrets PI). `FVector3d GunDirection(const FVector3d& noseDirection, const FVector3d& aimDirection, double
    gimbalRad)`: the direction a shot leaves in.
    - A non-finite aim or `|aim| < STEER_EPSILON`: zero.
    - A non-finite nose or `|nose| < STEER_EPSILON`: `normalize(aim)`.
    - `g` = the gimbal, NaN or negative counting as 0. With `n`, `a` the normalized nose and aim and
      `theta = atan2(|n x a|, n . a)`: `theta <= g` gives `a`; otherwise the unit vector at angle `g` from `n` toward
      `a` in their common plane, `n cos g + p sin g` with `p = normalize(a - n (n . a))`, or, when that is degenerate
      (`a` antiparallel), `p = normalize(Cross(n, |n.X| < 0.9 ? +X : +Y))`.
    - So the result is unit, within `g` of the nose, and equals the aim inside the gimbal.

## Review-fix contract (SDD 18 Amendment 7, 2026-10-22)

Changes to the 18a / 18b contracts after the adversarial review (user decisions H1, H2). Tests:
`NewtonianFlightMathTest.cpp`, `PlayerFlightTest.cpp`, `KeyBindingsTest.cpp`.

**G meter (18a items 11-14).**
- `SOLNewtonian::G_LOAD_TOLERANCE_G = 1e-9`. "Above sustained" means `gLoad > SustainedG + G_LOAD_TOLERANCE_G` in
  `GDrainPerS` (0 at or below it), in `StepGMeter`'s rate choice and in `GWarningLevel`'s OverSustained test, so a thrust
  clamped to exactly `PilotSustainedAccelMps2` never drains or warns.
- `FSOLPilotGLimits` gains `UnconsciousRecoverPerS = 0.02` and `LethalWarningS = 30.0`. In G-LOC the meter moves at
  `max(rate, UnconsciousRecoverPerS)` (it always recovers while unconscious, whatever the load), so G-LOC always ends:
  at the later of the timer and the meter reaching `WakeMeter` at that rate (10 s from empty under load). Normal phases
  are unchanged, so under a constant load above sustained the pilot cycles: drain, G-LOC, wake, drain.
- `FSOLGMeterState` gains `double DepletedS = 0.0`. After a step: if the load is above sustained (with the tolerance)
  and the result is in G-LOC or at or below `CriticalMeter`, `DepletedS += dt`; otherwise `DepletedS = 0`.
- `ESOLGWarning` gains `Lethal` (appended last). `GWarningLevel` returns Lethal first when
  `DepletedS >= LethalWarningS`, then Blackout, Critical, OverSustained, None. HUD only: there is no death yet.

**Guidance (18a items 21, 24).**
- Feedforward scale: `k = gain > 0 ? min(1, (1/gain) / tau, clamp(2 (1 - dt * gain), 0, 1)) : 1` (a continuous fade
  from `dt = 0.5/gain` to `1/gain`, so real-time steps are unchanged; the period-2 ringing near the target at `dt`
  around `1/gain` is gone). (First written as `clamp(1 - dt * gain)`; that slowed the real-time ETA countdown of
  `PlayerFlight.AutopilotFlies100Km`, so the fade starts at half the tracker time.)
- (L6, reverted after measuring) `EtaS` keeps the plan's `FlipTimeS`: with `tfEff = FlipTimeS + dt` a ship tracking
  the braking curve sits on the boundary of the ETA's "too fast" case, so the estimate jumps between branches and the
  real-time countdown of `PlayerFlight.AutopilotFlies100Km` dropped only 5.4-6.3 s per 10 s near the flip.
- Overload `ComputeRendezvousGuidance(..., double dt, bool bComputeEta)`: identical output except `EtaS = 0` when
  `bComputeEta` is false (the Mid hot loop and `SampleRendezvousTrajectory` pass false).

**Player (18b).**
- `SOL::AUTOPILOT_GRAVITY_STANDOFF_FRACTION = 0.9`. `double SOLFlight::AutopilotStandoffM(double bodyRadiusM, double
  bodyGM, double accelLimitMps2)` = `max(R * (1 + AUTOPILOT_BODY_STANDOFF_RADII) + AUTOPILOT_BODY_STANDOFF_M,
  sqrt(GM / (AUTOPILOT_GRAVITY_STANDOFF_FRACTION * limit)))`: the arrival point's distance from the body's centre, moved
  out until the body's gravity there is at most 0.9 of the autopilot limit. A non-finite or negative radius counts as 0;
  a non-finite or `<= 0` GM or limit gives the geometric term only.
- `AutopilotStep` gains a trailing `double targetGravityMps2 = 0.0` and plans with
  `MakeGuidanceParams(env, limit, max(|g|, targetGravityMps2), true, false)`: braking into a deep well is planned for the
  gravity at the arrival point (found while writing the tests: planning with the local gravity only, a ship from 2x
  the Sun standoff entered Brake at about 157 km/s and fell in). The processor passes the body's `GM / standoff^2`.
- `SOL::AUTOPILOT_KINEMATIC_ATTITUDE_S = 1.0`. In `AutopilotStep`, when `dt > AUTOPILOT_KINEMATIC_ATTITUDE_S` (sim-time
  substeps under user warp) and the guidance's `DesiredForward` is non-zero, the attitude is set kinematically before
  the thrust is allocated: `q = OrientationFromForward(DesiredForward, q.GetUpVector(), q)`, angular velocity zero,
  torque zero. At or below 1 s the step is unchanged.

**Key bindings (L5).** After `ResolveKeys` with a saved override that puts `Z` on another action (from a save made
before Stop spin existed), `HasConflict` / `FindConflict` flag both that action and Stop spin, like any other duplicate
(test only; no contract change).
