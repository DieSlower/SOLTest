<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 18 — Newtonian flight core, pilot G limits and NPC motion rework

Issue `#18` (ticket T1 of [`../Research/LT_features_for_SOL.md`](../Research/LT_features_for_SOL.md) section F). It refines:

- [`1-solar-system-architecture.md`](1-solar-system-architecture.md): the flight model, and the rule that ships fly
  in real time.
- [`2-foundations-flight-scaffold.md`](2-foundations-flight-scaffold.md): the current `SOLFlight` model.
- [`11-scale-and-performance-demo.md`](11-scale-and-performance-demo.md): NPC rails plus deviation, AI tiers, and the
  swappable motion seam.

Plan of record: [`../Plans/18-newtonian-flight-core-plan.md`](../Plans/18-newtonian-flight-core-plan.md).

**Status: agreed design.** Section 2 holds every decision the user made in the `grill-me` sessions of 2026-10-21
(rows 1-19) and the final answers to the round-2 questions N1-N7 (rows 20-26, all accepted as recommended). Section 3
is the design within those decisions. Section 5 records the round-2 questions and their answers.

---

## 1. Problem

Today the player's "assist" mode is not thrust-limited. `SOLFlight::Step` relaxes the velocity toward
`reference + thrust * cap` in 1.5 s, which implies up to ~1e8 m/s² and cancels gravity for free. Rotation is a
smoothed rate command with no inertia, and the acceleration numbers (50 m/s², about 5 G) have no relation to what a
human can tolerate. NPCs use a separate point-mass steering law (`SOLNpcAi::LimitSteering`).

Issue #18 replaces this with **one Newtonian, thrust-limited flight core** that the player and the NPCs share. The
core covers:

- per-axis thrust, angular acceleration with inertia, and gravity;
- **human G limits**: a sustained limit, short combat bursts, a G-tolerance meter, tunnel vision and G-LOC;
- an assist controller that spends thrust and respects the pilot;
- autopilot guidance (flip-and-burn, moving targets, works under user-chosen time warp, with an ETA and a predicted
  trajectory).

It is the base for the three flight modes of #19, and for the HUD and map pieces of #19 and #20.

## 2. Decisions (user-resolved, grill 2026-10-21, two rounds)

| # | Area | Decision |
|---|---|---|
| 1 | Physics | **Full Newtonian, thrust-limited**, shared by the player and NPC ships. Nothing writes velocity directly. Every mode produces only thrust and rotation demands inside the ship's envelope. |
| 2 | Long travel | **A mix.** Thrust-limited flight, plus a **full-auto autopilot**: accelerate toward the target, flip at the midpoint (or at the braking point when deceleration differs), then burn to stop. It works point A to B, for **moving targets**, and **under time warp**. The jump map stays. |
| 3 | Rotation | Assisted and auto use **rate-command steering with bounded angular acceleration**. Manual uses **torque with inertia**, plus a **"stop spin"** button that cancels all rotation. |
| 4 | Modes | The three flight modes (manual, assisted, full auto) are follow-up **#19**. The core exposes thrust and torque commands, the assist controller and the autopilot guidance outputs. |
| 5 | NPCs | Mid and Full NPC motion uses the **same Newtonian core** with simple controllers. **Far ships stay on rails.** Tier hand-offs must not pop. |
| 6 | Universe | **Gravity from bodies stays.** Double-precision universe coordinates and the render-origin rules are unchanged. |
| 7 | Envelope shape | **Per-axis box** of independent thrusters (a diagonal burn gets more than one axis), plus a total-acceleration cap (decision 9). |
| 8 | Boost | **x3 on every translation axis.** |
| 9 | **Pilot G limits (human)** | The envelope is expressed in **G**. There are **two separate values**: the ship's **thrust capability** (hull and engines) and the **pilot limit** (human). The **sustained limit is ~1.5 G** (default 1.5 G), and it is **temporary / tech-dependent**: an upgradeable pilot/ship parameter that equipment raises or lengthens later. **Combat maneuvers reach up to 9 G**, temporarily. |
| 10 | **G-tolerance meter** | Above the sustained limit a meter **drains, faster at higher G** (a G-dose model), and **recovers below the limit**. In manual mode the HUD **warns** when the sustained limit is exceeded. As the meter empties, **vision tunnels and grays out**. At zero the pilot **blacks out (G-LOC)**: the ship takes **no player input for a few seconds**, then the pilot recovers. |
| 11 | Who may exceed | **Assisted mode and the autopilot never exceed the sustained limit** (assist clamps the request). **Manual mode may go beyond** it, with the warning. |
| 12 | **NPC G rules** | There are two NPC pilot kinds. **Human NPCs** obey G constraints through class-specific envelopes and caps within the human limits. They have **no per-NPC G meter**. **AI NPCs** (drones, AI ships) are limited only by what the hull and parts withstand (higher, class-specific structural G caps). The NPC class data gets a **pilot-kind flag**. |
| 13 | **Autopilot** | It **plans at the pilot's sustained limit**. It **never changes time warp** by itself: compression is the user's choice, and when the user picks warp the autopilot integrates in sim-time substeps. **If the pilot is out of G meter** (G-LOC or meter depleted), the autopilot **slows to the sustained limit (1.5 G, or the pilot limit)**. Guidance outputs an **ETA countdown** computed from the braking-curve plan, for an **AUTOPILOT** HUD marker. The **3D map gets a toggle showing the autopilot trajectory** (#20), fed by a pure trajectory-sampling function from 18a. |
| 14 | Scope before #19 | Tab assist ON maps to **assisted**, OFF to **manual**. **Stop spin is on `Z`.** The autopilot is reachable only through the `sol.Autopilot` console command and the smoke flight. |
| 15 | NPC frame | NPCs are **Newtonian relative to the rail**: the rail absorbs gravity and lane cruise. **Mid ships are point-mass, Full ships 6-DOF.** |
| 16 | NPC details | The class envelopes from section 3.7 are accepted. A **1-3 px heading switch** at the Full-Mid boundary is accepted. **Mid-to-Far demotion** happens only when the snap is **at most half a pixel** (or the budget forces it). **Lane-end reversal is out of scope.** |
| 17 | Gravity in assist | **Active in assisted mode**, and compensating it **spends thrust** (it counts toward the pilot limit). |
| 18 | Surface lock | In assisted mode its alignment is an **attitude target**. In manual mode it is **off**. |
| 19 | Numbers | Assist time constant **0.75 s**. **0.5c stays a hard clamp.** Autopilot arrival tolerance **10 m / 1 m/s**. |
| 20 | N1 Ship thrust | **Forward 3 G, reverse 1.5 G, lateral 2 G, vertical 2 G, boost x3, total cap 9 G.** 9 G is reachable only by boosting the main engine. |
| 21 | N2 Autopilot, meter out | **`AutopilotRecoveryG` = 1.5 G**: the autopilot keeps flying at 1.5 G while the meter is critical or the pilot is in G-LOC. |
| 22 | N3 Contact G | **Only resting surface contact counts as G-load** (with thrust). Collision impacts do not count until a damage system exists. |
| 23 | N4 Rotational G | **Ignored** for now (no cockpit point yet). |
| 24 | N5 NPC pilot kinds | **The interceptor becomes an AI drone** with a **20 G structural cap**. The fighter and gunship stay human. |
| 25 | N6 Persistence / upgrades | **The G meter is not saved** (full on load). Pilot G upgrades are a ToDo item: [`../ToDo/ship-tech-upgrades.md`](../ToDo/ship-tech-upgrades.md). |
| 26 | N7 High warp hint | Autopilot is allowed at any user warp; the HUD shows **"WARP: autopilot precision reduced"** above 1 d/s within 10 planetary radii. |

## 3. Design

### 3.1 One core: state, envelope, step

`Source/SOLTest/Flight/SOLNewtonianFlight.{h,cpp}` holds `namespace SOLNewtonian`: pure logic, with no world access
and no allocation. It is tested by `SOLTest.NewtonianFlightMath.*`. It reuses `FSOLShipState` and the SDD 2 frame
and sign conventions:

- local +X is forward, +Y right, +Z up;
- angular velocity is X roll, Y pitch, Z yaw;
- positive pitch raises the nose, positive yaw turns right, positive roll drops the right wing.

- **Thrust capability (`FSOLThrustEnvelope`, per ship class).** A per-axis box of forward, reverse, lateral and
  vertical accelerations, and the boost multiplier. It also carries a **total-acceleration cap**
  (`MaxTotalAccelMps2`): the hardest G the ship may ever pull. For a human-piloted ship that is the 9 G combat limit.
  For an AI ship it is the structural limit. Rotation fields: angular acceleration per axis, rate-command limits per
  axis, and a spin clamp. The speed clamp is 0.5c.
- **Command (`FSOLNewtonianCommand`).** Local thrust fractions in [-1, 1], torque fractions in [-1, 1], and boost.
  This is the integrator's only input.
- **Step (`StepNewtonian`).** Semi-implicit Euler, in the order of today's `SOLFlight::Step`:
  1. Angular velocity integrates from the torque fraction, then is clamped to the spin clamp.
  2. Velocity integrates from the thrust (rotated by the orientation at the start of the step) plus gravity, then is
     clamped to 0.5c.
  3. Position integrates from the new velocity.
  4. Orientation advances by the new angular velocity and is renormalized.

  There is no damping, and inertia is spherical. Substeps stay at 1/30 s, at most 16 per frame, outside the autopilot.

### 3.2 Pilot G model

**What counts as G (proper vs. coordinate acceleration).** The G-load is the magnitude of the **proper
acceleration**: the non-gravitational acceleration the pilot's body actually feels. That is the thrust acceleration
plus any contact acceleration from a surface the ship rests on. Gravity itself does not count, because a body in free
fall (coasting, orbiting) feels 0 G whatever the coordinate acceleration. Three cases show why this is the right one:

- Hovering above Earth's surface costs 9.81 m/s² of upward thrust, and the pilot feels 1.0 G.
- A ship in low orbit coasts at 0 G.
- A ship sitting on the ground feels 1 G from the contact force.

Counting coordinate acceleration instead would make an orbit "9 G" near a dense body and a hover "0 G", which is
wrong both ways. Consequence: `GLoadG(properAccel) = |properAccel| / 9.80665`. The thrust part is
`AllocateThrust(...).AchievedAccelWorldMps2`. The contact part is added by the 18b processor while the ship rests on
a body (open question N3).

**Numbers (`FSOLPilotGLimits`, upgradeable per pilot/ship; the defaults are the untrained baseline):**

| Value | Default | Meaning |
|---|---|---|
| `SustainedG` | 1.5 | No meter drain at or below this. Assisted mode and the autopilot never exceed it. Tech raises it. |
| `MaxG` | 9.0 | Combat ceiling (also the human cap on NPC classes). |
| `TimeToEmptyAtMaxGS` | 10 s | Full meter to zero at a constant `MaxG`. Tech lengthens it (G-suit). |
| `DrainExponent` | 2 | Drain = `((G - Sustained) / (Max - Sustained))^2 / TimeToEmpty` per second. |
| `RecoverPerS` | 0.05 | Meter regained per second at or below `SustainedG` (empty to full in 20 s). |
| `BlackoutDurationS` | 5 s | Minimum G-LOC time. |
| `WakeMeter` | 0.2 | G-LOC also lasts until the meter has recovered to this. |
| `CriticalMeter` | 0.25 | At or below this the warning is Critical and the autopilot uses its recovery limit. |
| `VisionStartMeter` | 0.4 | Tunnel and gray-out start below this and are total at 0. |
| `AutopilotRecoveryG` | 1.5 | Autopilot limit while the meter is critical or in G-LOC (open question N2). |

Drain examples: 9 G empties a full meter in 10 s, 6 G in 27.8 s, 4.5 G in 62.5 s, and 3 G in 250 s.

**Meter state machine (`StepGMeter`; it is exact and piecewise-linear, so dt-invariant for a constant G):**

- **Normal.** The meter moves by `+RecoverPerS` (G at or below the sustained limit) or `-drain` per second, clamped
  to [0, 1]. If it reaches 0 inside a step, G-LOC starts at that exact instant and the rest of the step continues in
  G-LOC.
- **G-LOC.** The pilot has no control. The meter keeps moving at the same rate (it recovers only if the G-load is at
  or below sustained, which it is unless an autopilot or other controller is still burning hard). G-LOC ends at the
  later of two moments: the timer (5 s) running out, and the meter reaching `WakeMeter`. Any rest of the step is then
  Normal again.
- **Outputs.** `GWarningLevel`: None, OverSustained (G above sustained: the manual HUD warning), Critical (meter at or
  below 0.25) or Blackout. `GVisionLoss` gives 0 to 1 for the tunnel and gray-out post-process (1 during G-LOC).
  `PilotHasControl` is false during G-LOC.

**Where the limits apply:**

- **Assisted:** the desired thrust goes through `ClampToPilotLimit(desired, SustainedG * g0)` before
  `AllocateThrust`. Gravity compensation is part of that thrust, so it spends the pilot limit too. Hovering at
  Jupiter's cloud tops (2.5 G) is therefore not possible in assisted mode at the default 1.5 G.
- **Autopilot:** it plans with `MakeGuidanceParams(env, AutopilotAccelLimitMps2(limits, meter), ...)`. That is the
  sustained limit, or `AutopilotRecoveryG` while the meter is critical or in G-LOC. Its thrust is clamped the same
  way as assisted.
- **Manual:** there is no clamp below the envelope's 9 G total cap. The meter and warnings run.
- **G-LOC in any mode:** player input is ignored (thrust and torque zero). An engaged autopilot keeps flying at its
  recovery limit. Manual and assisted ships coast.

**Player thrust capability (replaces the 50 / 30 m/s² numbers):**

| Axis | Capability | With boost x3 |
|---|---|---|
| Forward | 3 G (29.42 m/s²) | 9 G |
| Reverse | 1.5 G (14.71) | 4.5 G |
| Lateral | 2 G (19.61) | 6 G |
| Vertical | 2 G (19.61) | 6 G |
| Total cap (`MaxTotalAccelMps2`) | 9 G (88.26) | 9 G (a boosted diagonal is scaled down to 9 G) |

So only the 9 G burst allowance (boosted main engine) reaches 9 G. The unboosted main engine (3 G) already drains
the meter slowly (250 s to empty at 3 G). Assist and the autopilot are capped at 1.5 G whatever the ship can do.
Rotation is unchanged from round 1: angular acceleration 4 / 3 / 3 rad/s², rate limits 120 / 90 / 90 °/s, spin
clamp 360 °/s. Rotation does not count as G-load (open question N4).

### 3.3 Rotation: torque, rate command, stop spin, attitude

- **Manual:** the torque fraction is set directly, and spin persists (only the clamp limits it).
- **Rate command (`RateCommandAngularFraction`):** deadbeat per axis and bounded by the angular acceleration.
  **Stop spin** is a zero command, on `Z`, in every mode.
- **Attitude (`AttitudeRateCommand`):** per axis `sign(e) * min(rate limit, sqrt(2 * 0.8 * alpha * |e|), 4 * |e|)`
  on the shortest-arc error. A target exactly behind turns by pure positive pitch. A zero desired up leaves roll
  free.
- **`RotationTimeS`:** the rest-to-rest bang-bang time, used for the flip estimate.

### 3.4 Assist controller

`VelocityHoldAccelMps2 = (vTarget - v) / max(0.75 s, dt) - g`, then `ClampToPilotLimit`, then `AllocateThrust`.
`AllocateThrust` scales the whole request down to fit the box and the total cap, keeping its direction, and reports
`bSaturated`. Large velocity changes ramp linearly at 1.5 G, so 1,000 m/s takes 68 s.

### 3.5 Autopilot guidance, ETA and trajectory

- **`ComputeRendezvousGuidance`** is a closed-loop braking-curve law in the target's frame. Round 1's law is
  unchanged:
  - `vLim(d) = 2d / (sqrt(tf^2 + 2d/a_b) + tf)`, with `tf = FlipTime + dt`, so no single step overshoots.
  - The tracker gain is 2/s, with feedforward from the braking curve and the target's acceleration.
  - The phases are Accelerate / Cruise / Brake / Arrived.
  - The nose follows the desired thrust, or flips early while coasting.

  New: the output carries **`EtaS`** (0 when Arrived).
- **`MakeGuidanceParams(env, pilotAccelLimitMps2, |g|, bFlip, bBoost)`.** It computes an effective forward / reverse
  acceleration = `min(axis * boost, pilot limit, total cap)`. The plan is
  `max(10%, 90% * effective forward - g)`. Braking uses the same value when flipping, or the reverse value otherwise.
  The flip time is the 180° pitch at 80% of alpha plus 0.25 s. At the default 1.5 G the plan is 13.24 m/s².
- **`EstimateTimeToArrivalS(d, closingSpeed, a_a, a_b, tf, cap)`** gives the ETA of the 1-D braking-curve plan:
  - a receding ship first stops;
  - a ship too fast to stop brakes, overshoots, and comes back from rest;
  - a ship faster than the cap first slows to it;
  - otherwise the profile is accelerate to the peak `v` (from
    `v^2 (1/2a_a + 1/2a_b) + v tf = d + vc^2/2a_a`), or cruise at the cap, then coast `tf`, then brake.

  Lateral velocity is ignored (the HUD shows it as an estimate), and an Arrived ship's ETA is 0.
- **`SampleRendezvousTrajectory`** writes the predicted ship-minus-target path into a caller-provided array view
  (no allocation). It runs the same guidance law on a point mass at `interval / substeps`, assuming the target does
  not accelerate, and stops at Arrived. #20's 3D map toggle draws it, adding the target's own predicted motion. A
  caller picks `interval = ETA / (capacity - 1)`.
- **Time warp: user-driven only.** The autopilot never touches warp. While it flies under a warp the user chose, the
  ship integrates in **sim time**. The substep is `max(10 s, frame sim time / 256)`, at most 256 substeps per frame.
  So 1 d/s at 60 fps is 144 substeps of 10 s, and 30 d/s is 256 substeps of about 169 s. The guidance is safe at
  large steps by construction (tested up to 300 s). Manual and assisted flight keep real time plus the frame carry
  (SDD 2 Amendment 2).

**Trip times at the default plan (1.5 G pilot, 13.24 m/s², flip coast ignored):**

| Trip | Sim time | Peak speed | Real time at 1000x / 1 h/s / 1 d/s / 30 d/s |
|---|---|---|---|
| 100 km | 174 s | 1.15 km/s | - |
| 10,000 km | 29 min | 11.5 km/s | 1.7 s / - / - / - |
| Earth to Moon | 3.0 h | 71 km/s | 10.8 s / 3.0 s / - / - |
| 1 AU | 59 h | 1,410 km/s (0.0047c) | 213 s / 59 s / 2.5 s / - |
| 30 AU | 13.5 d | 7,720 km/s (0.026c) | 19.4 min / 5.4 min / 13.5 s / 0.45 s |

Reaching 0.5c at 1.5 G would take 118 days, so **0.5c stays a pure safety clamp**. Tech that raises `SustainedG`
shortens every trip by `sqrt(old/new)`.

### 3.6 Player integration (18b)

- `FSOLFlightParams` holds `FSOLThrustEnvelope` and `FSOLPilotGLimits` (the const shared `ParamsCrc` covers both).
  The per-pilot `FSOLGMeterState` is per-entity state in the ship fragment.
- **Assisted** (Tab ON): velocity hold, then pilot clamp, then allocate. Rotation comes from the rate command.
  **Manual** (Tab OFF): thrust and torque fractions go in directly. **Stop spin** is `Z`. **Gravity is summed in
  every mode.**
- **Each substep:** compute the G-load from the achieved thrust plus contact, then `StepGMeter`. During G-LOC the
  player's control is zeroed.
- **HUD:**
  - the `OverSustained` warning in manual mode;
  - Critical and Blackout states;
  - the vision loss feeding a post-process (vignette plus desaturation; material in 18b, through `SOLConstants.h`
    paths);
  - an `AUTOPILOT` marker with an ETA countdown while the console autopilot is engaged. #19 owns the final layout.
- **Surface lock:** `AttitudeRateCommand` with the local vertical as the desired up, in assisted mode only.
- **Autopilot order:** on `FSOLShipControl`, set through `sol.Autopilot <Body|Target|Off>` and the smoke flight.
  It uses sim-time substeps under user warp (3.5).
- **Docs:** GAME_MECHANICS, `SOLMenuText` (`CONTROL_ROWS`, `INFO_PROSE`), the Controls table and the Info screen are
  updated in the same change.

### 3.7 NPC rework (18c)

The rail-relative Newtonian state is unchanged from round 1. A Mid or Full ship's deviation is integrated by
`StepNewtonian` with the class envelope and **zero gravity input** (the rail absorbs gravity and lane cruise, and
tidal gravity is under 0.01 m/s² over the 3 km leash). SDD 11's motion seam stays intact.

**Pilot kinds and class caps.** `SOLNpcAi::FClassParams` gains `ESOLPilotKind PilotKind`, the thrust box in G, and
`ClassGCapG`. The envelope used is `WithGCap(classEnvelope, EffectiveGCapG(kind, ClassGCapG))`. For a **Human** the
cap is limited to `HUMAN_MAX_G` (9). For an **AI** it is the structural cap as given. There is no per-NPC meter:
human classes simply never exceed their cap. Mid ships return to the rail with a point-mass budget of
`min(lateral thrust, cap)`.

| Class | Pilot | Thrust fwd / rev / lat / vert (G) | G cap | Alpha roll / pitch / yaw rad/s² | Rates rad/s |
|---|---|---|---|---|---|
| Fighter | Human | 8 / 4 / 4 / 4 | 6 G (trained combat) | 6 / 4 / 4 | 3.0 / 1.5 / 1.5 |
| Gunship | Human | 4 / 2 / 2 / 2 | 4 G | 2 / 1.5 / 1.5 | 1.2 / 0.8 / 0.8 |
| Interceptor | **AI drone** | 15 / 8 / 8 / 8 | 20 G (structural) | 8 / 6 / 6 | 4.0 / 2.5 / 2.5 |

The interceptor becomes an AI drone (open question N5). The class `MaxSpeedMps` (250 / 150 / 350) stays the AI's
desired-speed cap.

- **Full (at most 4,000):** 6-DOF. The desired velocity from `PursuitVelocity`, `SeparationVelocity` and
  `LeashVelocity` goes through velocity hold and `AllocateThrust`, and the nose follows `AttitudeRateCommand`.
  `LimitSteering` is retired. `ShouldFire` tests against the nose, so the 10d gun cones are re-measured. A new
  `FSOLNpcAttitudeFragment` (56 B) is added on promotion to Full and removed on demotion.
- **Mid (at most 100,000):** point-mass return to the rail with `ComputeRendezvousGuidance`. Ships at rest on
  their rail are skipped.

**Hand-off without pops (accepted):**

| Transition | What happens | Why there is no pop |
|---|---|---|
| Far to Mid | Deviation and its velocity start at zero. | Exactly the rail's position and velocity. |
| Mid to Full | The deviation is kept. The attitude comes from `OrientationFromForward(railVelocity + deviationVelocity, up away from the group body)`, with zero spin. | Position and velocity are continuous, and the nose equals the velocity heading the instanced hull was already drawn with. |
| Full to Mid | The deviation is kept and the attitude is dropped. | Only the hull heading switches, by 1-3 px at 50 km (accepted). |
| Mid to Far | The deviation snaps to zero, only if `IsHandoffSnapInvisible` (at most half a pixel) or the budget forces it. | Sub-pixel. |

The rail velocity is a central difference of `SOLNpcRail::EvaluateM`. Lane-end reversal is unchanged (out of
scope).

### 3.8 Interfaces for #19 / #20

- **#19 modes:**
  - Manual uses `FSOLNewtonianCommand`.
  - Assisted rotation and stop spin use `RateCommandAngularFraction`.
  - The assist controller is `VelocityHoldAccelMps2`, then `ClampToPilotLimit`, then `AllocateThrust`.
  - The auto mode uses `ComputeRendezvousGuidance` and `MakeGuidanceParams`.
  - Pointing for orbit, escort and dock uses `AttitudeRateCommand`.
- **#19 HUD:**
  - `EtaS`, `Phase`, `StoppingDistanceM` and `BrakingSpeedLimitMps`;
  - `GLoadG`, `GWarningLevel` and `GVisionLoss`;
  - the `AUTOPILOT` marker.
- **#20 map:** `SampleRendezvousTrajectory`, with a 3D-map toggle to show the autopilot trajectory.

### 3.9 Performance

- **Player:** in normal flight, one entity with at most 16 substeps. Under user warp with the autopilot, up to 256
  substeps per frame, each about 2 µs (guidance, allocation, gravity over about 30 bodies, the swept broadphase, the
  G meter), so about 0.5 ms. The meter is a few flops per substep.
- **Full:** 4,000 x about 0.2 µs, about 1 ms serial, run in parallel within SDD 11's 2 ms budget.
- **Mid:** about 20 ns per moving ship, run in parallel. Most Mid ships are at rest on their rail and skipped.
- **`SampleRendezvousTrajectory`:** about capacity x substeps guidance calls (for example 256 x 8, about 0.2 ms).
  It is run only when the map toggle is on, and only re-sampled when the order or the state changes beyond a
  tolerance (#20).
- **Measurement:** everything is measured in 18d with `stat` / Insights.

### 3.10 Testing and verification

- **18a:** `SOLTest.NewtonianFlightMath.*`, written first against the plan's contract (see the plan for the case
  list).
- **18b:** re-validate `FlightStepTest` and `SurfaceLockTest`, and retune `-SOLSmokeFlight` and
  `-SOLSurfaceLockSmoke`. New smoke phases cover:
  - a manual 9 G boost to G-LOC and recovery, with input ignored while blacked out;
  - an assisted burn that stays at or below 1.5 G;
  - the console autopilot to a body at 1000x user warp, with the ETA counting down.

  Take PIE screenshots of the vision loss.
- **18c:** re-validate `NpcAiMathTest` (the `LimitSteering` cases are removed) and re-run the SDD 11 battle and ramp
  measurements.

## 4. Non-unit-tested pieces

- The processor changes: sim-time substeps under user warp, the G meter per substep, the contact acceleration, and
  zeroing input during G-LOC.
- The pawn mode mapping and the `Z` binding.
- The vision post-process material, the HUD warning, the `AUTOPILOT` / ETA marker, and the console command.
- The NPC Mid and Full processors and the attitude fragment.
- The smoke scripts.

## 5. Open questions

Round-1 questions 1-15 are all answered (section 2, decisions 7-19). The round-2 questions below were **all answered
on 2026-10-21 by accepting the recommended answer** (section 2, decisions 20-26). They are kept for the record:

- **N1. Thrust capability numbers.** (Recommended) Forward 3 G, reverse 1.5 G, lateral and vertical 2 G, boost x3,
  total cap 9 G (section 3.2), so 9 G is reachable only by boosting the main engine. Alternative: forward 9 G
  unboosted, with boost only adding endurance.
- **N2. Autopilot while the meter is out.** (Recommended) `AutopilotRecoveryG` = 1.5 G (the user's "slows to the
  sustained 1.5 G"). Because the autopilot already plans at sustained, this changes nothing by default and only
  matters after a tech upgrade raises `SustainedG`. Alternative: 1.0 G, so recovery is gentler.
- **N3. Contact G.** (Recommended) Resting on a surface counts as G (the contact force); collision impacts do not
  count until a damage system exists. Alternative: thrust only.
- **N4. Rotational G.** (Recommended) Ignored for now: it is short-lived and depends on a cockpit position the
  placeholder ship does not have. It is not negligible, though: a 2 m seat offset gives about 0.9 G at the 120 °/s
  roll limit, and about 0.5 G at 90 °/s pitch. Alternative: add `|w|^2 * seatOffsetM` to the proper acceleration
  once ships have a cockpit point.
- **N5. Which NPC classes are AI.** (Recommended) The interceptor becomes an AI drone, and the fighter and gunship
  stay human. Alternative: add a fourth "drone" class and keep the three human.
- **N6. G meter persistence and upgrades.** (Recommended) The meter is not saved (it resets to full on load). The
  `SustainedG` and `TimeToEmptyAtMaxGS` upgrades are designed later with ship equipment (a ToDo item).
- **N7. Very high user warp with the autopilot near bodies.** (Recommended) Allow it with 169 s substeps at 30 d/s.
  The guidance stays stable, but gravity is sampled coarsely near planets. Show a HUD hint "WARP: autopilot
  precision reduced" above 1 d/s within 10 planetary radii. Alternative: no hint.

## 6. Amendments

### Amendment 1 — guidance feedforward fades for long steps (18a, 2026-10-21)

Implementing the 18a contract literally failed `LongRangeDtInvariance` at `dt = 300 s`: the 1 AU leg took 1.40x the
ideal time (Python port of the law reproduces it). Cause: a step longer than the tracker's time constant (0.5 s) is
deadbeat, so `(vDes - v) / dt` already re-targets the braking curve every step, and the Brake feedforward (the
curve's slope) subtracts the same decrease a second time. Near the end the ship then alternates between the curve
and almost zero speed (period-2 ringing), halving the average speed of the tail. Fix: the feedforward is scaled by
`k = min(1, (1/gain) / tau)`. For `dt <= 1/gain` (every real-time step) `k = 1`, so the law is unchanged there. With
the fix the 1 AU leg takes 1.0002 / 1.0005 / 1.0016 / 1.0075x ideal at dt 1 / 10 / 60 / 300 s, with no overshoot.

### Amendment 2 — `FullShipFlipAndBurn` flip threshold under gravity (18a test fix)

The test required the nose to reach `dot < -0.9` with the line of sight in the uniform-gravity case. That is not
reachable under the contract: braking thrust also cancels gravity, so its direction is at best
`-plan / |(plan, g)|` = -0.855 for the 1.5 G plan (8.24 m/s²) against 5 m/s² of perpendicular gravity. The threshold
is now `-0.9 * plan / |(plan, g)|` for gravity cases and still -0.9 without gravity. The 100 m overshoot and the
1.5 G limit assertions are unchanged.

### Amendment 3 — assisted thrust keeps gravity compensation first (18b, 2026-10-21)

Section 3.4 clamps `VelocityHoldAccelMps2` (velocity error / 0.75 s minus gravity) as a whole to the 1.5 G pilot
limit, keeping its direction. With a large velocity error (for example the default spawn in a 7.7 km/s orbit with
assist on) the velocity term is thousands of m/s² and gravity is a negligible part of the scaled request, so an
assisted ship would brake at 1.5 G while falling out of orbit into the planet. 18b's `SOLFlight::AssistThrustAccelMps2`
therefore spends the pilot limit on gravity compensation first and gives the velocity hold only what is left
(`|-g + s * hold| <= limit`, largest `s`). Unsaturated requests are identical to section 3.4; a body whose gravity
exceeds the limit (Jupiter's 2.5 G) still cannot be hovered over in assisted mode. The 18b contract in the plan has
the exact rule.

### Amendment 4 — vertical-nose levelling and smoke thresholds (18b verification, 2026-10-21)

- **`LevelForward` with the nose along the up.** The `-SOLSmokeLevel` run tilts the ship 90 deg so its nose points
  straight up. Projecting the forward on the horizontal plane then gives nothing, so the old rule kept the forward,
  the pitch error stayed zero and the ship never levelled (`d-aligned` 90 deg off after 6 s). `SOLFlight::LevelForward`
  now falls back to the ship's up (negated for a nose pointing up), projected: the heading the shortest pitch
  reaches. Two `SOLTest.PlayerFlight.LevelForward` cases were updated to this rule. Result: 14 deg off after 1 s,
  0.000 deg after 6.2 s.
- **Smoke thresholds.** Assisted flight now accelerates at the 1.5 G sustained limit instead of the old tau-based
  response, so three smoke checks were retuned to it rather than to the speed cap. `-SOLSmokeInput a-thrust`: at
  least 85% of 1.5 G x t (43.9 m/s measured, 44.2 ideal). `g-lock-mars`: the speed change from Earth is 0.8-1.05 x
  1.5 G x t (81.6 of 88.3 m/s; Mars is ~28 km/s away, so the old "matched within 1 m/s" goal is unreachable in a
  6 s phase). `-SOLSmokeHud e-w-after-close`: at least 20 m/s in 2 s (32.4 measured).

### Amendment 5 — 18c: NPC motion rework as built, gun gimbal, battle station-keeping (2026-10-21)

Built to the plan's "18c contract" (items 1-13; tests written first by separate agents: `SOLTest.NpcAiMath.*` and
`SOLTest.NpcRail.*`). `SOLNpcAi::LimitSteering`, `FClassParams::MaxAccelMps2` / `MaxTurnRateRadPerS`, the Mid spring
(`SOL::NPC_MID_DEVIATION_OMEGA` / `NPC_MID_DEVIATION_LEASH_M`) and `NPC_HEADING_MIN_SPEED_SQ_MPS2` are gone, with their
tests (`LimitSteering*`, `MidLeashCoversClasses`).

- **Classes** carry the section 3.7 table (`PilotKind`, thrust G, `ClassGCapG`, angular limits); `ClassEnvelope` gives
  the `FSOLThrustEnvelope` (total cap `EffectiveGCapG * g0`: fighter 6 G, gunship 4 G, interceptor 20 G; no boost; spin
  clamp twice the largest rate). There is no NPC G meter.
- **Full** (`FSOLNpcCombat::StepShip`): the desired deviation velocity (pursuit, separation, leash, clamped to the class
  `MaxSpeedMps`) goes through `StepFullShip` (velocity hold with tau 0.5 s, `AllocateThrust`, `AttitudeRateCommand` +
  `RateCommandAngularFraction`, `StepNewtonian` without gravity). The nose (`FullShipDesiredForward`) is on the lead aim
  within 1.5x the fire range, else along the desired, else the current velocity relative to the group's anchors; the up
  is away from the group's body (`FSOLNpcRailFrame::GroupBodyM`). `FSOLNpcAttitudeFragment` (quaternion + angular
  velocity, 56 B of data) is added and removed with the Full tag in the same command flush; its zero quaternion means
  "not set" and the Full gather sets it with `HandoffOrientation(rail velocity + deviation velocity, up)`.
- **Mid** (`USOLNpcMidProcessor`): `StepMidReturn`, the rendezvous guidance back to the rail with a point-mass budget of
  0.9 x `min(lateral, cap)` (fighter 3.6 G, gunship 1.8 G, interceptor 7.2 G), cruise capped at the class speed, at rest
  (exactly zero, then skipped) within 1 m and 0.5 m/s.
- **Hull heading** (Collect): a Full ship's nose; a Mid ship's `HullForward(SOLNpcRail::VelocityMps + deviation
  velocity, up)` (the rail velocity is a +-0.5 s central difference with the group points fixed), the same vector a
  promotion starts the nose with. Point-tier ships get none.
- **Mid-to-Far gate**: in Select, a demotion to Far from Mid or Full is turned into Mid unless
  `AllowFarDemotion(|deviation|, distance, pixel angle, budget forced)`; the pixel angle comes from the player camera's
  FOV and the viewport width, "budget forced" is the budgeted Mid radius below the nominal one.
- **Gun cones around the nose, plus a gun gimbal (new, contract item 13).** With the nose on the lead aim, the old
  heading-based arcs (1.6 / 3.1 / 1.0 rad) made almost every shot fire, and the first run (cones 0.35 / 3.1 / 0.25,
  shots along the exact lead aim) was much more lethal than 10d: 1,560 of 4,000 alive. Narrower cones alone did not
  help (0.15 / 3.1 / 0.10: 1,704 alive), because the nose tracks the aim within them. So shots now leave along
  `GunDirection(nose, aim, GunGimbalRad)`: the aim swivelled at most the gimbal off the nose, so fixed guns miss by the
  nose's tracking error. Final values: cones fighter 0.15, gunship 3.1 (turrets), interceptor 0.10 rad; gimbals
  0.015 / PI / 0.015 rad. The statistical model (beyond 20 km in the hybrid) does not use the direction, so it is
  unchanged.
- **Battle station-keeping (fix for an 18b side effect).** The player spawns on a circular orbit; before 18b the
  assist zeroed the velocity relative to Earth in about 1.5 s, now it brakes at 1.5 G (about 380 s from 4.9 km/s), so
  the ship left the battles formed around it: the first 18c baseline ramp had all 4,000 battle ships Mid, none Full.
  `USOLNpcSubsystem::MatchShipToBattleBody` now sets the ship's velocity to the battle body's right after a spawn that
  has battles (flags, console and the Demo page), restoring the 10d-10f scene.

**Lethality parity** (standalone `-game`, 1080p, `-corelimit=16`, 1M NPCs, 4,000 battle ships, hybrid, facing battle 0;
means of the `NpcCombat` lines over the last 60 s of a 160 s run): alive 2,101 of 4,000 (SDD 11 Amendment 6 soak:
2,160-2,200), so within about 4%. Live bolts ~7,100 (was ~3,300): missed shots fly their full lifetime now. Full AI
1.28 ms mean (AI step 0.77 ms) vs 1.0-1.1 ms in 10d.

**Default-budget ramp (same setup, `-SOLNpcRamp`):**

| Stage | Mean ms | p95 | p99 | max | fps | NPC mean / p95 ms | Full / Mid | Result |
|---|---|---|---|---|---|---|---|---|
| 10,000 | 7.19 | 8.12 | 8.77 | 11.54 | 139 | 1.56 / 1.83 | 4,000 / 1 | PASS |
| 100,000 | 7.31 | 8.23 | 8.83 | 18.69 | 137 | 1.64 / 1.94 | 4,000 / 6 | PASS |
| 250,000 | 7.21 | 8.02 | 8.50 | 24.74 | 139 | 1.67 / 1.93 | 4,000 / 17 | PASS |
| 500,000 | 7.44 | 8.46 | 9.11 | 11.15 | 134 | 1.82 / 2.14 | 4,000 / 31 | PASS |
| 1,000,000 | 7.62 | 8.47 | 9.21 | 18.30 | 131 | 2.03 / 2.30 | 4,000 / 67 | PASS |

Against SDD 11 Amendment 7 at 1M: p95 8.47 vs 8.40 ms, mean 7.62 vs 7.32 ms, NPC update 2.03 / 2.30 vs 1.81 / 2.14 ms
(the 6-DOF step, the rail velocity per Full ship and twice the live bolts). Screenshots (session scratchpad `n18c/`):
`npcbattle.png` (battle 0 at FOV 8 with bolts) and `npcbattle_nobolts.png` (FOV 4, combat off: cones and boxes with
individual headings).

### Amendment 6 — `-SOLSmokeMapPick` height-preview failures were OS focus loss, not 18b/18c (2026-10-22)

During 18b/18c verification `-SOLSmokeMapPick` failed d2, e, f and f2: the Shift height preview dropped to 0 within
~2 s of passing d. Not caused by issue #18 code (nothing in the map pick path changed). Root cause: the smoke window
lost OS focus mid-run (desktop activity during the run), and `bShouldFlushPressedKeysOnViewportFocusLost=True`
(`Config/DefaultInput.ini`) makes `UGameViewportClient::LostFocus` flush every pressed key, so the script's synthetic
held Shift was released and the preview correctly abandoned. Reproduced deterministically by moving the foreground
to the taskbar right after check d (17/21, the same four failures); unfocused or focused-without-blip runs pass
21/21. Fix (smoke harness only, player behavior unchanged): while any `-SOLSmoke*` input script runs,
`ASOLShipPawn::BeginPlay` turns that flush off in memory and `EndPlay` restores it; the same repro then passes 21/21.
No automation test (engine focus/input interaction, not pure logic); the repro is the verification.

### Amendment 7 — adversarial review fixes (2026-10-22)

An adversarial opus review of 18a-18c raised the findings below. User decisions: **H1** gravity-aware autopilot
standoff (later the user may add ship power), **H2** G-LOC always ends, a jump or respawn clears G-LOC and the
autopilot, high-G bodies simply drain the meter, and a prolonged drained meter only shows a HUD "lethal G" warning (no
death until a damage system: [`../ToDo/damage-and-death.md`](../ToDo/damage-and-death.md)). The changed contract is the
plan's "Review-fix contract"; its tests were written first by a separate agent.

| # | Finding | Fix |
|---|---|---|
| H1 | The autopilot could not hold its arrival point at the Sun or Jupiter (gravity there above the 1.5 G limit) | `SOLFlight::AutopilotStandoffM`: the arrival point moves out until the body's gravity is at most 0.9 x the autopilot limit (`SOL::AUTOPILOT_GRAVITY_STANDOFF_FRACTION`): about 4.55 solar radii, 1.37 Jupiter radii at 1.5 G; the plan budget there is the 10% minimum, so the hold fits the limit |
| H1b | (found by the test author) Planning braking with the gravity at the ship only, a ship from 2x the Sun standoff entered Brake at about 157 km/s and fell into the Sun | `AutopilotStep` plans with `max(|g here|, targetGravityMps2)`; the processor passes `GM / standoff^2`. Python port: arrival in about 92,800 s from 2x the standoff instead of a crash at about 46,800 s |
| H2 | Resting on a body above 1.5 G kept the pilot in G-LOC forever (the meter only recovered at or below sustained) | `FSOLPilotGLimits::UnconsciousRecoverPerS` = 0.02: unconscious, the meter recovers at least at that rate whatever the load, so G-LOC ends (10 s from empty under load) and the pilot cycles; `FSOLGMeterState::DepletedS` counts the time depleted under load and `ESOLGWarning::Lethal` ("LETHAL G" on the HUD) appears after `LethalWarningS` = 30 s. `NEWTONIAN_MAX_METER_PHASES` 4 -> 256 so a long sim step can hold many cycles |
| H3 | A jump or a respawn kept the autopilot order and the G meter | `USOLShipSubsystem::ResetPilotState` (disengage, full meter) on jump arrival (next to `ClearSurfaceLock`) and on despawn; a new ship's fragments start fresh |
| M1 | A thrust clamped to exactly the sustained limit could read as "over" through rounding | `SOLNewtonian::G_LOAD_TOLERANCE_G` = 1e-9 in the drain, the meter rate and the OverSustained warning |
| M2 | Mid-to-Far gate vs the GPU far layer: beyond the Mid radius the GPU draws every rail point, so a ship the gate holds in Mid is already drawn at its rail there | **Not fixed (open).** Both proposed fixes need per-ship state in the GPU far layer (hide a held ship's rail point), which today has only a distance test; a Far ship inside the Mid hysteresis band must stay GPU-drawn, so the hide radius cannot simply grow. The visible jump at the Mid-radius crossing is at most the 3 km leash: about 1.4 px at the default 2,000 km radius (1080p, 90 deg). Follow-up: a per-ship hidden bit in the rail texture for the (few) gate-held ships |
| M3 | Guidance rang at dt around 1/gain (the Mid return at 0.5 s took 85-150 s) | Feedforward scale `min(1, gainTime / tau, clamp(2 (1 - dt * gain), 0, 1))`, a continuous fade from dt = 0.25 s to 0.5 s (real-time steps unchanged; a fade from dt = 0 slowed the 100 km leg's ETA countdown) (Python port: Mid return at 0.5 s now 15-31 s; the 1 AU leg 1.00003 / 1.0003 / 1.0016 / 1.0076x ideal at dt 1 / 10 / 60 / 300 s, slightly better than Amendment 1). `StepMidReturnLongStep` requires rest at 0.5 s again |
| M4 | Under sim-time substeps (10-169 s) the rate-limited nose could not turn stably | `AutopilotStep` with `dt > SOL::AUTOPILOT_KINEMATIC_ATTITUDE_S` (1 s) sets the nose kinematically to `OrientationFromForward(DesiredForward, current up)` with zero spin before allocating thrust |
| M5 | `MatchShipToBattleBody` overrode velocity on every respawn and during an autopilot order, from the ecliptic registry | Only on a population's first spawn, skipped while the autopilot flies, from `GetUnrealFrameCache().VelocitiesMps`; documented as an **explicit exception to decision 1** (nothing writes velocity) for the demo / benchmark scene |
| M6 | Debug logs in `SOLMapModeSubsystem.cpp` | Already reverted (no diff) |
| L1 | No hint how to cancel the console autopilot | HUD line "sol.Autopilot Off to cancel" under the AUTOPILOT marker (`SOLMenuText::AUTOPILOT_CANCEL_HINT`) |
| L2 | Reference frame on autopilot arrival | No change: on arrival the order ends and assisted flight holds the velocity of the reference frame, which is the anchor (the arrival body, the nearest) unless the player has M-locked another; a lock on the arrival body at arrival is a #19 decision (follow-up) |
| L3 | The ETA cost in hot loops | `ComputeRendezvousGuidance(..., bComputeEta)`; the Mid return and `SampleRendezvousTrajectory` pass false |
| L4 | (note only) | - |
| L5 | A saved key override putting `Z` on another action | Test `KeyBindings.StopSpinOverrideConflict`: the conflict is flagged like any duplicate |
| L6 | ETA used the bare flip time | **Kept** after trying `tfEff = FlipTimeS + dt`: the ship tracks the braking curve built with `tfEff`, so it sat on the ETA's "too fast" boundary and the estimate jumped between branches (the 100 km leg's countdown dropped only 5.4-6.3 s per 10 s at the flip). The bare flip time keeps the curve conservative relative to the ETA |
| L7 | Style | Lines over 120 columns rewrapped; the player envelope and pilot-limit defaults moved to `SOLConstants.h` (`PLAYER_*`, `PILOT_*`, following the `SURFACE_LOCK_*` precedent); file-local math tolerances (`NEWTONIAN_*`) and HUD layout pixels stay in their files' anonymous namespaces, as elsewhere; stale "surface-lock alignment" comment in `SOLShipFlightProcessor.h` fixed |

## 7. Revision history

- 2026-10-21: initial draft from the issue #18 grill (decisions 1-6), with Claude's proposals and open questions;
  18a contract and tests written.
- 2026-10-21: round-2 answers: open questions 1-15 resolved; added pilot G limits, the G meter and G-LOC, NPC pilot
  kinds, user-only warp for the autopilot, the ETA and the trajectory sampling; envelope re-derived in G; new open
  questions N1-N7; 18a contract and tests rewritten.
- 2026-10-21: final answers: N1-N7 all accepted as recommended (decisions 20-26); ToDo item for pilot/ship tech
  upgrades created.
- 2026-10-21: Amendment 5 (18c built: NPC Newtonian motion, gun gimbal, battle station-keeping, measurements).
- 2026-10-22: Amendment 6 (`-SOLSmokeMapPick` focus-loss key flush; smoke-only fix).
- 2026-10-22: Amendment 7 (adversarial review fixes: H1 gravity-aware standoff, H2 G-LOC recovery floor and lethal-G
  warning, H3 pilot reset on jump, M1-M6, L1-L7; M2 left open).
