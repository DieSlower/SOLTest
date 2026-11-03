<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 32: Chase Down autopilot (G bursts planned against the pilot model)

Issue #32. Depends on SDD 18 (Newtonian core, pilot G model, rendezvous guidance), SDD 19 (flight modes, the AUTO
cancel rule), SDD 20 (routes pause on a foreign order), SDD 22 (action table, radial and list menus; Chase Down greyed
until this ticket). Plan of record: [`../Plans/32-chase-down-autopilot-plan.md`](../Plans/32-chase-down-autopilot-plan.md).

**Status: built (2026-11-03), as amended in section 10.** Section 2 holds the user's grill-me decisions (2026-11-02). Section 3
is the design within them. Section 8 records the follow-up answers (Q1-Q10), already folded into the design.

---

## 1. Goal

A **Chase Down** order: the autopilot flies to a moving object as fast as the pilot can survive, using G bursts above
the 1.5 G sustained limit, and then stays on the object's tail until the player takes control. It is issued from the
interaction menus (radial on flight icons, list on the map). Today every autopilot order plans at the sustained limit
(SDD 18 decision 11) and Chase Down is a greyed placeholder row (SDD 22 section 2.4).

## 2. Decisions (user-resolved, grill-me 2026-11-02)

| # | Area | Decision |
|---|---|---|
| 1 | Behaviour | Chase Down **keeps chasing until cancelled**: it continuously re-aims at the moving target, **never settles or ends on arrival**, and stays on the target's tail. |
| 2 | G use | It **exceeds the normal 1.5 G autopilot limit with bursts**, shown on the G meter. The cap is **whatever the pilot can survive without blacking out**: today 9 G for a short time through the SDD 18 pilot model. The cap is **read from the ship / pilot values** (`FSOLFlightParams`: `FSOLPilotGLimits` and `FSOLThrustEnvelope`) so future tech upgrades raise it; **no hard-coded 9**. G **need not be constant**: the planner may vary it over time. |
| 3 | Planner | The planner **simulates the SDD 18 G-tolerance / recovery model** and chooses the G profile (burst to max then cool off, lower sustained G with a shorter cool off, or a mix) that **minimises the predicted time to intercept without ever blacking out**. It **replans as the target moves**. It is **pure logic, unit-testable**, with specified function contracts (plan, "Chase contract"). |
| 4 | Safety margin | A **Setting: slider 0-30%, default 25%** (changed from 10% by Q1), a **floor on the G meter**: the chase stays that far above empty, then bounces back (burst down toward the floor, recover, burst again). This is a **persisted settings change**: the user approved **`SAVE_VERSION` 7 -> 8 with a migration** (old saves keep every value, the new field takes 25%). **`KEY_BINDING_LAYOUT_VERSION` does not change.** |
| 5 | Targets | This ticket: **dropped targets and bodies** (star, planets, moons, dwarf planets). **NPC ships stay greyed until #40**. The chase is written **target-agnostic** so #40 only has to enable the ship row and supply a target source. |
| 6 | Controls | Started **only from the radial / list menu entries** (SDD 22 action table; the Chase Down entry is enabled for the supported kinds). **Any manual thrust / steer input, Esc, or choosing another order cancels it.** **No new key binding.** |

## 3. Design

### 3.1 Overview

Chase Down is an autopilot order of its own kind, flown in the AUTO flight mode (SDD 19). It reuses the SDD 18
rendezvous guidance (`SOLNewtonian::ComputeRendezvousGuidance`) toward an **aim point** on the target's tail, and adds
two things the plain autopilot does not have:

1. a **G policy** that sets each substep's G limit (burst, cool off, brake, hold) instead of the fixed sustained limit;
2. a **planner** that picks the policy's G levels by simulating the pilot model, and is re-run as the target moves.

The arrival never ends the order: once the ship reaches the aim point the guidance keeps holding it (the phase reads
Hold on the HUD) and the order lasts until a cancel (section 3.7).

New pure code lives in `Source/SOLTest/Flight/SOLChase.{h,cpp}` (namespace `SOLChase`), tested by
`SOLTest.Chase.*` in `Tests/ChaseTest.cpp`. The engine glue is in `USOLShipSubsystem` (order, target resolution,
planning), `USOLShipFlightProcessor` (per-substep policy), `USOLInteractionSubsystem` (menu action), the pawn (Esc) and
the HUD.

### 3.2 The cap: what the pilot can survive

`SOLChase::ChaseMaxG(env, limits, bBoost)` = the smallest of `limits.MaxG`, the forward thrust
(`env.ForwardAccelMps2 * (bBoost ? env.BoostMultiplier : 1) / g0`) and the total cap (`env.MaxTotalAccelMps2 / g0` when
positive), never below `limits.SustainedG`. With today's values and boost on that is min(9, 9, 9) = **9 G**; with boost
off it is 3 G. A tech upgrade (ToDo
`ship-tech-upgrades.md`) that raises `MaxG`, `TimeToEmptyAtMaxGS` or the envelope raises the chase cap and endurance
with no code change. The meter model beyond `MaxG` keeps draining faster, so the cap is also where the SDD 18 drain
curve is calibrated.

Reaching 9 G needs the boosted main engine (SDD 18 decision 20: 3 G forward x3 boost). **The chase uses boost only
while the player's Tab boost toggle is on** (Q10): then the cap, the guidance budgets and the thrust allocation are
boosted (`bBoost` true). With the toggle off, the planner, the cap and the allocation use the unboosted engine
(`bBoost` false, a factor of 1, so forward thrust bounds the G: 3 G today). The chase never changes the toggle.
Toggling boost during a chase does not cancel it (section 3.7): the per-substep cap follows the toggle at once and the
next replan (at most `REPLAN_INTERVAL_S` later) plans with the new cap.

### 3.3 The safety margin: a meter floor

Per Q1, the margin is a **floor on the G-tolerance meter**, as a fraction of a full meter ("stay 25-30% above empty,
then bounce back"). Blackout (G-LOC) starts when the meter reaches 0 (SDD 18 section 3.2), so the default 25% margin
means the chase never lets the meter fall below 0.25: it bursts down toward the floor, cools off and recovers, and
bursts again.

- `SOLChase::EffectiveMeterFloor(margin)` = the margin clamped to [0, 0.3] (NaN -> the 0.25 default), and at least
  `SOL::Chase::MIN_METER_FLOOR` = 0.01 so a 0% setting keeps a technical 1% floor and never reaches exactly 0 through
  rounding (0 would be G-LOC).
- `SOLChase::MaxSafeGForStep(meter, floor, limits, dt)`: the highest constant G for a step of `dt` that leaves the
  meter at or above the floor (inverting `GDrainPerS`: `S + (M - S) * (((m - floor) / dt) * T)^(1/p)`), and
  `SustainedG` when the meter is at or below the floor. This **per-step safety clamp** is applied to every chase
  substep whatever the plan says, so the floor holds even if the plan is stale, the target jinks, or a sim-time
  substep is long (example: full meter, floor 0.1, a 10 s warp substep allows 8.62 G; a 169 s substep about 3.2 G; at
  the default 0.25 floor the same substeps allow less).
- The meter keeps recovering only at or below `SustainedG` (SDD 18), so a pilot under the floor (for example after a
  manual 9 G burst just before the order) is flown at or below sustained until the meter is back above the policy's
  resume level.

The plain autopilot's rule "use `AutopilotRecoveryG` while the meter is critical (<= 0.25)" does **not** apply to the
chase: the floor replaces it (otherwise the 25% default margin could never be used, since 0.25 is the critical level). If the pilot is in G-LOC during a chase
(not reachable by the chase itself, only by an outside load such as resting on a heavy body), the chase flies at
`AutopilotRecoveryG` like the autopilot (SDD 18 decision 21).

### 3.4 The G policy (per substep)

A chase plan is an `FSOLChasePolicy` of two G levels between `SustainedG` and the cap, plus the meter floor:

- `BurstG`: the level of every burst, while accelerating and while braking;
- `BrakePlanG`: the **average** deceleration the braking curve is designed for. When it is below `BurstG` the brake is
  flown as bursts at `BurstG` with cool offs at sustained, whose average is `BrakePlanG`.

The pilot only recovers at or below sustained, so a long chase cannot burst all the time: the policy cycles **burst**
(at `BurstG`, the meter draining) and **cool off** (at `SustainedG`, the meter recovering at `RecoverPerS`) with
hysteresis on the meter. Each substep, `SOLChase::ChaseGLimit` gives the substep's G limit and activity from the
policy, a small run state (`bCooling`), the meter, the guidance phase and the closing speed:

| Activity | When | G limit |
|---|---|---|
| **Burst** | phase Accelerate or Cruise, not cooling | `BurstG` |
| **Cool** | phase Accelerate or Cruise, cooling | `SustainedG` |
| **Brake burst** | phase Brake, not cooling | `BurstG` |
| **Brake cool** | phase Brake, cooling | `SustainedG` |
| **Hold** | phase Arrived (on the tail) | `BurstG` (the hold uses little thrust) |

- **Brake reserve.** `BrakeReserveMeter(v, BurstG, BrakePlanG, a_bp)` is the meter the rest of the brake will use net of
  recovery: the brake lasts `v / a_bp`, a fraction `f = (BrakePlanG - S) / (BurstG - S)` of it bursting, so it uses
  `max(0, f * GDrainPerS(BurstG) - (1 - f) * RecoverPerS) * v / a_bp`. A duty cycle that the recovery can sustain needs
  no reserve.
- **Switching.** Bursting (in any phase) switches to cooling when the meter reaches `floor + reserve`; cooling ends at
  `ResumeMeter = floor + reserve + SOL::Chase::COOL_HYSTERESIS_METER` (0.15). When `ResumeMeter` would exceed 1 no burst
  is possible: the activity is Cool (or Brake cool) at sustained, so there are no zero-length cycles. The burst to
  cool switch has a tolerance of `SOL::Chase::COOL_SWITCH_TOLERANCE_METER` (0.01): it fires at `floor + reserve + 0.01`
  (Amendment 1).
- **Safety clamp.** Every limit is then capped by the cap and by `MaxSafeGForStep` against `floor + reserve` (Burst,
  Hold) or `floor` (Brake burst), so no substep can end below the floor.

The guidance gets the policy's budgets: `PlanAccelMps2` from the current G limit and `BrakeAccelMps2` from `BrakePlanG`
(each `max(10%, 0.9 * G * g0 - |g|)`, as `MakeGuidanceParams`, through `SOLChase::MakeChaseGuidanceParams`), so the
braking curve matches the average deceleration the brake will really fly; during a brake cool off the ship slips
above the curve and the next brake burst catches up. The thrust is clamped to the limit (`ClampToPilotLimit`) and
allocated boosted only while the Tab boost toggle is on (section 3.2).

### 3.5 The planner

`SOLChase::PlanChase(input)` chooses `BurstG` and `BrakePlanG` from a grid of `SOL::Chase::PLAN_G_LEVELS` + 1 levels
(8 steps: `SustainedG + k * (cap - SustainedG) / 8`, k = 0..8), with `BrakePlanG <= BurstG` (45 pairs), by predicting,
for each pair, the time to intercept with `SOLChase::PredictChase`:

- **Model:** the SDD 18 ETA model (one-dimensional along the line of sight in the target's frame, the target assumed
  unaccelerated, lateral speed ignored), flown with the section 3.4 policy and the exact SDD 18 meter model
  (`GDrainPerS`, `RecoverPerS`). The accelerate phase is the burst / cool cycle with its thresholds; the brake is the
  SDD 18 braking curve as flown (uniform `a_bp`, fading near arrival; Amendment 1) with the meter changing at the duty
  cycle's net rate.
  Every segment is linear in the meter and uniformly accelerated, so it is integrated **segment by segment in closed
  form** (events: the meter reaching `floor + reserve` or `ResumeMeter`, the cruise cap, the brake point, arrival). The
  cost depends on the number of burst / cool cycles (at most `PLAN_MAX_SEGMENTS`), not on the trip length.
- **Objective:** the smallest predicted time to intercept (aim point reached within the SDD 18 arrival tolerance
  10 m / 1 m/s). Ties within `PLAN_TIE_FRACTION` (0.5%) take the lower `BurstG`, then the lower `BrakePlanG` (gentler
  on the pilot, stable).
- **Safety:** a candidate whose predicted meter ever drops below the floor (for example a brake duty cycle the meter
  cannot fund over a long brake) is rejected. The pair (Sustained, Sustained) is always feasible and equals the plain
  autopilot's plan, so the chase is **never predicted slower than the sustained autopilot**.
- **What the family covers (decision 3):** burst to the cap then cool off (high `BurstG`), a lower burst level with
  shorter cool offs (middle `BurstG`), the mix (repeated burst / cool cycles, in the accelerate phase and in the brake),
  and no burst at all. Example with the default pilot: over long chases a steady duty cycle beats bursting at the cap.
  A burst at x = (G - S) / (M - S) can run a fraction 1 / (1 + 2 x^2) of the time against the 0.05/s recovery, so the
  sustainable average extra G is 7.5 x / (1 + 2 x^2), best at x = 0.71 (about 6.8 G bursts): about 4.1 G on average
  instead of 1.5 G. With the brake planned at a sustainable 3.4 G grid level, a 1e9 m chase takes about 0.64 of the
  sustained time. Short chases (a few km) are best with one burst near the cap and a brake at a similar level.
- **Replanning:** the ship subsystem replans on engage, then every `REPLAN_INTERVAL_S` (0.25 s of real time), and at once
  when the meter differs from the plan's prediction by more than `REPLAN_METER_DELTA` (0.05) or the target's velocity
  changed by more than `REPLAN_TARGET_DV_FRACTION` (10% of the closing speed, at least 1 m/s). A new plan replaces the
  current one only if it is predicted faster by more than `REPLAN_IMPROVEMENT_FRACTION` (2%) or the current one is no
  longer feasible (`SOLChase::ShouldReplacePlan`), so the levels do not flap.
- **Cost budget:** a full plan (45 predictions) at most 0.1 ms on the game thread, measured with a `SOL_ChasePlan`
  stat / Insights scope in part 32f. If the measurement is over, the fallback is a coarse-then-refine search (5 levels,
  then the neighbours of the best), recorded as an amendment.

### 3.6 Targets and the aim point (target-agnostic)

The order holds a **chase source**, resolved once per frame into an `FSOLChaseTargetSample` (Unreal-handed universe
frame): position, velocity, acceleration, the velocity of the target's own frame (zero for a body), radius, name and
`bAlive`.

| Source | Resolution | Frame velocity | Trail distance |
|---|---|---|---|
| Body (registry index) | body cache position and velocity; acceleration = the other bodies' gravity at its centre (as the body autopilot) | the body's own | `max(SOLFlight::AutopilotStandoffM(radius, GM, sustained limit), TARGET_TRAIL_RADIUS_FACTOR * radius)`, so the hold point is holdable without draining the meter |
| `ISOLTargetable` (dropped target now, NPC ship in #40) | `GetTargetInfo()`; acceleration estimated from the velocity change (`SOLChase::EstimateTargetAccelMps2`, low-pass 0.5 s, clamped to twice the cap) | the frame body's (dropped target: its combat frame body) | `SOLChase::ChaseTrailM(radius)` = `max(TARGET_TRAIL_M, TARGET_TRAIL_RADIUS_FACTOR * radius)` (Q3, below) |

`ISOLTargetable` gains `virtual bool IsTargetAlive() const { return true; }`; `USOLDroppedTargetTargetable` overrides
it with `IsBoundTargetAlive()`. The interaction subsystem owns a **second dropped-target proxy for the chase**, not
registered with targeting, so the chase does not depend on the current selection (R / F cycling never sees it). #40
adds an NPC-ship targetable and enables the Ship row; nothing in the chase changes.

**Size-scaled standoff (Q3).** The tail distance is `trail = max(TARGET_TRAIL_M, k * radius)` with
`k = SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR` = 3 (proposed; a **tunable**, tune in the smoke). 200 m is the floor for
small dropped targets and ships; an extremely large target (radius 5 km: 15 km) is not flown to within its own
silhouette. Bodies take the larger of this and the holdable standoff, which stays the binding term for massive bodies.

**Aim point** (`SOLChase::ChaseAimPointM`): the target's tail.

- If the target moves relative to its frame faster than `TAIL_MIN_SPEED_MPS` (1 m/s): `trail` behind it along that
  relative velocity (an NPC ship in #40).
- Otherwise (a body, or a dropped target at rest in its frame): `trail` out from the target along the **approach
  direction** (target to ship, captured when the order starts and kept in the target's non-rotating frame), so the ship
  stops on its own side as Move To and Follow do. A zero direction falls back to +Z (as `SOLActionOrders::ArrivalOffset`).

Under a user warp the target's position at each sim-time substep is interpolated back from its end-of-frame position
with its velocity, exactly like the bodies (`SOLFlight::BodyPositionAtSubstep`).

**Target lost** (Q5): when `bAlive` turns false (the dropped target destroyed, evicted, or its slot reused) the chase
ends in ASSISTED and the HUD shows "CHASE ENDED: target lost" for 3 s.

### 3.7 Starting and cancelling

- **Start:** only the Chase Down entry of the radial menu (flight icons) or the map list menu. Rows (SDD 22 action
  table): Star / Planet / Moon / Dwarf gain a live Chase Down row (placed right after Move To / Follow, Q8), so
  their row count becomes 9 (`MAX_ACTIONS` 10 holds it); the dropped target's Chase Down row becomes live; the Ship row
  stays a placeholder (Not yet available, #40); asteroids get no row. Availability: NoShip, JumpWarping and
  NoPilotControl as Move To, plus a new block **AlreadyChasing** ("Already chasing it") when the chase already has this
  object. `FSOLActionContext` gains `bIsChased`.
- **Engage:** `USOLShipSubsystem::EngageChase(const FSOLChaseOrder&)` enters AUTO with the last frame's held input
  latched (as `EngageAutopilot`), gives a new order serial (an active route pauses, SDD 20), and plans at once. Chase
  Down does **not** select the object (Q4): the current target is unchanged.
- **Cancel** (decision 6), every exit lands in ASSISTED:
  - manual thrust, roll, steer or stop spin: the existing SDD 19 AUTO cancel rule, unchanged (input held at engage is
    latched; while blacked out input never cancels);
  - **Esc** (Q2): while a chase runs, Esc cancels the chase only and does not open the pause menu on that press; a
    second Esc opens the pause menu (the radial-menu precedent of SDD 22). Ctrl+F1 acts as Esc;
  - **another order**: Move To, Follow / Orbit, Jump To, a route start (Enter on the map) or Resume (V) replaces it;
  - C (take control), a respawn (`ResetPilotState`), Quit to menu; Jump To (and the map's J) ends it as the jump starts
    (Amendment 4 (c)), so a jump arrival never finds a chase running;
  - target lost (section 3.6).
- **Not cancelling:** boost, the speed cap, targeting keys, warp, the M / L locks, free look, opening the map, a menu
  or the F3 panel (the ship keeps chasing, as the autopilot keeps flying).
- No new key binding; `KEY_BINDING_LAYOUT_VERSION` stays 7.

### 3.8 Flight integration

- `FSOLShipControlFragment` gains the chase order for the processor: `bChase`, the target's end-of-frame position,
  velocity and acceleration, the aim offset, the planning gravity, and the `FSOLChasePolicy`. `FSOLShipStateFragment`
  gains `FSOLChaseRunState` (cooling flag, the last substep's G limit and activity, for the HUD). The order itself
  (source, approach direction, plan, replan timers) lives in `USOLShipSubsystem`.
- The processor treats a chase like an autopilot order for the time domain: real-time substeps at 1x, sim-time
  substeps under a user warp above 1x (SDD 18 section 3.5), no frame carry. Per substep: aim point, `ChaseGLimit`,
  guidance with the chase budgets, clamp, allocate (boosted when the Tab toggle is on), `StepNewtonian`, swept collision, G meter (unchanged
  order). The kinematic nose of long substeps (SDD 18 Amendment 7 M4) applies too.
- The chase **never counts an autopilot arrival** (`bAutopilotArrived` stays false), so the route and the Follow
  station-keeping arrival-count debt item are not affected.
- **Chase under time warp (Q7): allowed at any warp**, like the autopilot. The planner and the G meter work in
  **sim seconds** (the meter, target velocity and distances all evolve in sim time under warp, SDD 18 section 3.5), so
  the plan needs no warp scaling; the plan's levels stay valid, only the substeps get longer. Handling: (1) the replan
  timer is real time (0.25 s) **and** also fires after `REPLAN_MAX_SIM_S` (30 s, tunable) of sim time, so at 1000x the
  plan is refreshed every 30 sim seconds rather than every 250; (2) the per-substep safety clamp (`MaxSafeGForStep`)
  keeps each long substep above the floor, so bursts shrink as the substep grows; above about 1000x the chase flies
  close to sustained; (3) the target position is interpolated per sim substep (section 3.6). **Risks, to verify in the
  smoke:** a body's orbital curvature over a long substep (acceleration assumed constant) can make the aim point lag and
  the hold oscillate at very high warp; a stale plan between replans; the 256-substep cap per frame. If the smoke shows
  instability above some warp, the fallback is to cap the burst G at high warp, recorded as an amendment.
- Console: `sol.Chase <Body|Target|Off>` engages or cancels a chase (verification and smokes; Target = the selected
  targetable).
- Nothing about the chase is saved (like the autopilot).

### 3.9 HUD and G meter

- **Mode label:** AUTO (green), as for every autopilot order.
- **Top centre marker** (replaces the AUTOPILOT marker while a chase runs):
  `CHASE > <name>  <distance>  closing <speed>  T <time to intercept>` (T shows `HOLD` on the tail), a second line with
  the activity and G limit: `BURST 6.8 G`, `COOL 1.5 G (resume at 45%)`, `BRAKE 6.8 G`, `BRAKE COOL 1.5 G` or `HOLD`,
  and the cancel hint
  "Thrust, steer or Esc to stop chasing" (`SOLMenuText::CHASE_CANCEL_HINT`).
- **G meter** (bottom left, SDD 18): unchanged text and bar, plus a **floor tick** on the tolerance bar at the safety
  margin and a fainter **resume tick** while cooling; the bar is Secondary orange while the G-load is above sustained
  (the bursts are visible there, decision 2).
- **Warnings under the reticle** (Q6): shown as normal. The "G CRITICAL - EASE OFF" panel and the tunnel vision /
  gray-out post-process follow the meter exactly as in manual flight; the chase suppresses neither. With the default
  25% floor the meter bottoms at 0.25, which is the critical threshold, so the warning appears at the bottom of each
  burst.
- **Target lost:** "CHASE ENDED: target lost" for 3 s in the marker's place.

### 3.10 Settings: Chase Down safety margin (schema 8)

| Item | Value |
|---|---|
| Model field | `SOLSettingsModel::FSettingsValues::ChaseSafetyMargin` (float, fraction of a full meter) |
| Save field | `USOLSettingsSave::ChaseSafetyMargin` (`UPROPERTY() float`, initialised to the default) |
| Default | `SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT` = **0.25** (Q1) |
| Range | `CHASE_SAFETY_MARGIN_MIN` = 0.0, `CHASE_SAFETY_MARGIN_MAX` = 0.30; step `CHASE_SAFETY_MARGIN_STEP` = **0.01** (Q9) |
| Sanitize | clamp to the range; NaN -> the default (`SOLSettingsModel::Sanitized`); part of `AreEqual` (1e-4 tolerance). At 0 the chase still uses the technical 1% floor (`EffectiveMeterFloor`) |
| Schema | `SOL::Settings::SAVE_VERSION` 7 -> **8** (comment lists "8: Chase Down safety margin, SDD 32") |
| Migration | `USOLSettingsSave::MigrateToCurrentVersion`: a new step `if (Version == 7) { ChaseSafetyMargin = CHASE_SAFETY_MARGIN_DEFAULT (0.25); Version = 8; }`. Every other field (volumes, keys, overlay profiles, ...) is kept as loaded. Saves 1-6 reach 8 through the existing steps then this one. Unversioned or newer files are left alone as today |
| Key layout | `KEY_BINDING_LAYOUT_VERSION` stays **7**: a v7 save migrated to v8 does **not** get the controls-changed notice |
| UI row | Settings page slider "Chase Down safety margin", shown as a percent ("25%", 1% steps), with the flight options (after "Mouse steering at start"); `ESOLSettingsSlider::ChaseSafetyMargin` in page order. Pending until Apply, discarded by Revert, like the other sliders |
| Applies | At Apply: an active chase uses the new floor at its next replan (and its safety clamp at once) |
| Writes | No new write point: the existing ones (Settings page / menu close, only when something changed; async) |

### 3.11 Docs and in-game text

- `GAME_MECHANICS.md`: a Chase Down row in section 2 (Flight: cap, floor, policy, planner, cancel), section 7a rows
  (dropped target and body rows, the AlreadyChasing block, the Chase Down action), section 8.1 (the new setting and the
  Saving row: schema 8, the 7 -> 8 step), the Esc row in section 1 (cancels a chase).
- In-game Controls / Info (`SOLMenuText`): `INFO_PROSE` gets a Chase Down paragraph (bursts, the margin setting, how to
  cancel) and the "Greyed entries" sentence no longer lists Chase Down for bodies and dropped targets;
  `INTERACT_BLOCK_TEXTS` gains "Already chasing it"; the Esc control row mentions the chase; the new HUD strings.
- `ARCHITECTURE.md`: `SOLChase` in the `Flight/` row and the key types; the frame order note (the chase target is
  resolved and the plan refreshed in `StepShips` before the processor run; `ChaseGLimit` per substep);
  `ISOLTargetable::IsTargetAlive`; the Interaction row (chase proxy). No new module dependency.
- `STATUS.md` (#32 row), this SDD's amendments, `TECH_DEBT.md` for anything left.

## 4. Performance

- Per substep: `ChaseGLimit`, `MaxSafeGForStep` (one `pow`) and the existing guidance; at most 256 sim substeps per frame
  under warp, about the autopilot's cost (SDD 18 section 3.9).
- Planner: one ship, budget 0.1 ms per plan (section 3.5), no allocation (fixed-size candidate grid on the stack). At
  1x and low warp about 4 plans per second (the 0.25 s real-time timer) plus event replans. Under warp above about
  1800x at 60 fps (where a frame is longer than `REPLAN_MAX_SIM_S` of sim time) the sim-time trigger fires every
  frame, so the chase plans once per frame: measured at 1 d/s, 16 us mean and 85 us max per plan, 25 us per frame for the whole
  chase update (Amendment 3 (e)).
- Target resolution: one `GetTargetInfo` or one body lookup per frame. The chase proxy is one UObject created once
  (no churn).
- Nothing for NPCs: Chase Down is a player order. #40's NPC targets reuse the same per-frame resolution.

## 5. Testing and verification

- **Pure logic, test-first** (`Tests/ChaseTest.cpp`, `SOLTest.Chase.*`): written by a separate subagent from the plan's
  contract only (no implementation shown), red on stubs, then green. Cases are listed in the plan ("Chase contract").
- **Extended tests:** `InteractMenusTest.cpp` (rows, the live / placeholder Chase Down rows, AlreadyChasing, row counts
  9 / 4 / 6, Chase Down right after Follow), `SOLSettingsModelTest.cpp` (sanitize, equality), `SOLSettingsRulesTest.cpp` (schema 8 current, 7 migrated,
  the 7 -> 8 step keeps every field and sets 0.25, a v1 save reaches 8), `FlightModeMathTest.cpp` (its assertion that
  the schema equals the key layout version becomes "layout 7, schema 8, a v7 save gets no controls notice"), and a
  `TargetingTest` case for `IsTargetAlive`'s default.
- **Smoke `-SOLSmokeChase`** (new, `Interaction/SOLChaseSmoke.cpp`): drop a target, move the ship 20 km away, Chase Down
  from the radial, check AUTO, a burst above sustained within 2 s, the meter never below the floor (every frame),
  intercept within the predicted time plus 25%, 10 s on the tail within 2x the trail distance with no arrival counted,
  W cancels to ASSISTED; again with Esc (no pause menu, a second Esc opens it); again replaced by Move To; a body chase
  (the Moon) cancelled by C; the current target unchanged by the order; a 30% margin (smoke settings slot) keeps the
  meter at or above 0.30; the default 25% keeps it at or above 0.25; a size-scaled standoff (a large dropped or fake
  target holds at 3 x radius); a chase at 100x and 1000x warp holds the floor and does not diverge; with the Tab boost
  toggle off the chase stays at or below the unboosted cap (3 G today) and turning boost on lets it burst higher; the target destroyed ends the chase in ASSISTED with the HUD note; screenshots of the CHASE
  marker during a burst and while cooling.
- **Regression smokes:** `-SOLSmokeInteract`, `-SOLSmokeRoute`, `-SOLSmokeInput`, `-SOLSmokeHud`, `-SOLSmokeFlight`,
  `-SOLMenuSmoke=all` (Settings page with the new row).
- **Full suite**, the adversarial performance / style review (opus, fresh context), fixes, suite and smokes again.

## 6. Non-unit-tested pieces

The ship subsystem's order handling and replan timing, the processor integration, the chase proxy, the pawn's Esc
handling, the HUD drawing, the Settings page row and the console command: covered by `-SOLSmokeChase`, the regression
smokes and screenshots.

## 7. Out of scope

- NPC ships as chase targets (#40), NPC pilots chasing anything.
- A damage or death model for G (ToDo `damage-and-death.md`); rotational G (SDD 18 N4).
- Predicting the target's own manoeuvres beyond its current acceleration estimate.

## 8. Resolved decisions (follow-up answers, 2026-11-02)

No open questions remain. All are folded into the sections noted.

- **Q1. Margin:** a floor on the G meter ("stay 25-30% above empty, then bounce back"). Setting range 0-30%, **default
  25%** (was 10%), migration 7 -> 8 sets 0.25, `KEY_BINDING_LAYOUT_VERSION` unchanged. A 0% setting keeps the technical
  1% floor (3.3, 3.10).
- **Q2. Esc:** cancels the chase only; a second Esc opens the pause menu (3.7).
- **Q3. Tail:** 200 m on the approach side for dropped targets, scaled by size: `max(200 m, k x radius)`, k = 3
  proposed as a tunable (`TARGET_TRAIL_RADIUS_FACTOR`); bodies hold at the larger of that and the holdable standoff (3.6).
- **Q4.** Chase Down does not select the object; the current target is unchanged (3.7).
- **Q5.** Target destroyed: the chase ends in ASSISTED with the HUD note (3.6).
- **Q6.** The G CRITICAL warning and tunnel vision are shown as normal (3.9).
- **Q7.** Chase is allowed under time warp; handling and risks in 3.8.
- **Q8.** Menu order: right after Move To / Follow (3.7).
- **Q9.** Slider step 1% (3.10).
- **Q10.** "Boost" means the player's Tab boost toggle is ON: the chase uses boost only while the toggle is on;
  otherwise it plans and flies with the unboosted cap (3.2). (Corrected 2026-11-02: an earlier reading of this answer,
  "availability in the flight model, `BoostMultiplier > 1`, ignoring the toggle", was wrong.)

## 9. Revision history

- 2026-11-02: initial draft from the issue #32 grill-me decisions 1-6, with the open questions Q1-Q10.
- 2026-11-02: Q1-Q10 resolved and folded in (default margin 25%, 1% step, size-scaled standoff, no auto-select, warp
  allowed, boost only when available).
- 2026-11-02: Q10 corrected by the user: boost means the Tab toggle is on (3.2, 8, the smoke in 5); `ChaseMaxG`,
  `MakeChaseGuidanceParams` and `FSOLChasePlanInput` take `bBoost` (plan contract items 1 and 5).
- 2026-11-02: Amendment 1 (part 32a as built): real braking curve, switch tolerance, phase probe (section 10).
- 2026-11-03: Amendment 2 (part 32c as built): ship integration choices and the PIE numbers (section 10).
- 2026-11-03: Amendment 3 (part 32f smoke as built): the smoke's numbers; no high-warp fix needed (section 10).
- 2026-11-03: Amendment 3 (e): measured plan and ship-step cost; within budget (section 10).
- 2026-11-03: Amendment 4 (review fixes): chase fragment, frame-relative target-turn trigger, Jump To ends the chase,
  target-lost note timing; section 4 corrected to the measured plan rate under warp (section 10).
- 2026-11-03: status set to built after the review fixes (suite 1183/1183, smoke 26 checks); section 3.7 cancel list updated for Jump To.

## 10. Amendments

### Amendment 1: part 32a as built (2026-11-02)

Flagged for the user's review.

- **(a) Brake model.** The SDD 18 guidance flies a braking curve whose deceleration fades near arrival, so a brake
  takes `(v - vEnd)/a + tf*ln(v/vEnd)`, not the ideal `tf + v/a`. `PredictChase` models the real curve so it matches
  execution: 2 km chase predicted 24.09 s vs 24.22 s flown; 200 km 164.56 s vs 164.70 s (end meter 0.96 vs 0.91). An
  ideal-brake prediction gave 10.5 s vs 24.2 s at 2 km, which would fail the execution-consistency case (plan item 7)
  and the section 5 smoke check (intercept within predicted + 25%). Consequence: the (S, S) prediction equals a flown
  sustained autopilot (100 km: 191.3 s vs 191.4 s flown) and is about 10% longer than SDD 18's `EstimateTimeToArrivalS`
  (173.8 s), which stays a lower bound. Plan item 7's first case changes from "within 1% of `EstimateTimeToArrivalS`"
  to "within 1% of a 1-D 0.02 s sustained-autopilot flight, and >= `EstimateTimeToArrivalS`".
- **(b) Switch tolerance.** New constant `SOL::Chase::COOL_SWITCH_TOLERANCE_METER` = 0.01: bursting switches to cooling
  at `floor + reserve + 0.01`, because the `MaxSafeGForStep` clamp makes the meter approach `floor + reserve` only
  asymptotically. With an exact switch the ship never cooled (it sat near sustained G, meter stuck at the floor). The
  floor guarantee is unchanged.
- **(c) Closed-form cycles.** `PredictChase` skips repeating burst / cool cycles in closed form, so the cost does not
  grow with trip length.
- **(d) Measured cost.** `PlanChase` mean cost (`SOLTest.Chase.PlanTiming`, 200 calls each): short 2 km 22-24 us,
  medium 200 km 41-43 us, long 1e9 m 38-42 us, under-floor 200 km 36-42 us. Budget 100 us; no fallback search needed.
- **(e) Phase probe.** New helper `SOLChase::MakeChasePhaseProbeParams(env, policy, gravity, bBoost)`. The processor
  gets the guidance phase and closing speed for `ChaseGLimit` from a probe `ComputeRendezvousGuidance` call
  (`bComputeEta` false) with these params, then runs the real guidance with
  `MakeChaseGuidanceParams(env, limit.LimitG, BrakePlanG, ...)`. The phase does not depend on the plan budget.

### Amendment 2: part 32c as built (2026-11-03)

Technical choices inside sections 3.5-3.8, none user-visible beyond the SDD. Flagged for the user's review.

- **(a) Per-substep step.** New `SOLFlight::ChaseStep` (next to `AutopilotStep`, sharing one file-local helper for the
  nose, the clamp, the allocation and `StepNewtonian`, so the kinematic nose of long substeps is the autopilot's): the
  phase probe, `ChaseGLimit`, the guidance with the limit's budget (the probe is reused when the limit equals
  `BurstG`), `ClampToPilotLimit(limitG * g0)`, the allocation and `StepNewtonian` boosted with the Tab toggle
  (`FSOLNewtonianCommand::bBoost` set too). Guidance ETA is not computed (`bComputeEta` false); the HUD's T comes from
  the plan's prediction counting down in sim time between replans.
- **(b) Target interpolation.** The control fragment carries the target's end-of-frame position and velocity, its frame
  velocity and its frame body. Per substep the target is its frame body's substep position plus its end offset moved
  back at its frame-relative velocity (`BodyPositionAtSubstep`), so a dropped target follows its body's curved path
  under warp; without a frame body, its own position and velocity. The target's acceleration is its frame body's gravity
  from the other bodies at the substep (as the body autopilot) plus the subsystem's own-acceleration estimate.
- **(c) Frame body of a targetable.** `ISOLTargetable` gains `virtual int32 GetTargetFrameBodyIndex() const`
  (default `INDEX_NONE`: the target's own velocity is its frame's); the dropped-target proxy returns its combat frame
  body (`USOLCombatSubsystem::GetTargetFrameBodyIndex`, new). The acceleration estimate (`EstimateTargetAccelMps2`)
  runs on the velocity relative to that body, in sim seconds, clamped to twice the boosted cap, and restarts when the
  frame body changes.
- **(d) Replan comparison.** At each replan the current policy is re-predicted from now (its stored prediction counted
  from its own plan time, so any fresh candidate would look faster by the elapsed time), and counts as not feasible
  when it would dip under min(floor, meter) or its `BurstG` is above a cap lowered by the boost toggle; then
  `ShouldReplacePlan`. Two predictions are added per replan (46 instead of 45).
- **(e) Meter trigger.** "The meter differs from the prediction" = the meter leaves the band
  [`MinMeter` - 0.05, max(meter at plan time, `EndMeter`) + 0.05] of the current plan. Also replanned at once: the
  applied margin changed (the floor in the fragment's policy follows the setting every frame, so the safety clamp uses
  it at once, section 3.10) and the boost toggle changed.
- **(f) Planning at the ship's time.** Before the flight run the ship still sits where last frame's bodies left it,
  while the sample is this frame's; the plan input backs the target off by the frame (its frame body's last position
  plus its own velocity times the sim frame). Without this the plan saw an Earth-frame target 500 m (60 fps) to 10 km
  (3 fps editor) off, and the levels flapped (121 replacements in 5 min; 1 to 3 after the fix).
- **(g) Hold policy.** At the tail every candidate predicts 0 s and the tie rule picks (S, S), so a fresh plan on the
  tail holds at sustained; a moving target makes the next replan (at most 0.25 s later) burst again. A held plan is kept
  (not 2% slower). To revisit with #40's moving ships if the response is too soft.
- **(h) Console.** `sol.Chase <Body [Name]|<BodyName>|Target|Drop|Off|Status>` lives in `SOLInteractionSubsystem.cpp`
  (it needs the chase proxy): Body defaults to the Moon; Target is the selected body or dropped target; Drop chases the
  most recent drop (one dropped ahead first when none is alive); Status logs `GetChaseStatus` once. The chase proxy
  (`USOLInteractionSubsystem::ChaseProxy`, `ChaseDroppedTarget(slot)`, `GetChasedDroppedTargetSlot()`) is built here for
  the console; part 32d reuses it for the menu action.
- **(i) Target lost.** `GetChaseStatus().bTargetLost` stays true from the loss until the next `EngageChase`, and
  `GetChaseTargetLostCount()` counts losses (the HUD starts its 3 s note when it changes, part 32e).
- **(j) PIE check (editor at 3-60 fps, 1x unless noted).** Dropped-target chase, boost on, ship receding at 4.9 km/s:
  bursts 7.1-9 G (load up to 7.96 G), cool at 1.5 G, meter cycling 0.26-0.40, never below the 0.25 floor; hold on the
  tail at 200 m (trail 200 m), 0.15 G load. Moon chase: 6.19 G bursts (5.47 G load), meter 0.26-0.40. Boost off:
  limit 3 G (load 2.69 G). 1000x warp Moon approach: the hold point reached at 5212 km (trail 5212 km) but the guidance
  hovers at 14 m / 1.0 m/s, just outside the 10 m / 1 m/s arrival tolerance, so the activity reads BURST (low load)
  instead of HOLD until warp drops; a smoke item for 32f. Cancels verified: thrust and steer (AUTO cancel rule), C,
  Move To (`sol.Autopilot`), another chase, target lost (ASSISTED).

### Amendment 3: part 32f smoke as built (2026-11-03)

Verification only; no design change. Flagged for the user's review.

- **(a) Smoke.** `-SOLSmokeChase` (`FSOLChaseSmoke`) runs the section 5 checks, 25 in all (26 with Amendment 4's `z-jump-ends-chase`), through the pawn's real
  paths (radial menu by a scripted flight cursor, W / Esc / C / Tab / warp keys by their handlers, the menus' `Execute`
  for re-engages and Move To). Additions to section 5: the margin chases start receding at 2 km/s from 20 km so the
  bursts reach the floor (a 20 km chase from rest bottoms at a meter of about 0.65); the large target is a smoke-only
  `USOLChaseSmokeTarget` (a dropped target's slot reported with a 1 km radius); the target is destroyed with bolts
  (`USOLCombatSubsystem::FireBolt`); a Moon chase at 1000x from just outside its tail point is checked too.
- **(b) Numbers (1280x720, about 220 fps; 1000x800 and a 15 fps cap agree).** 20 km chase: on the tail after 51.0 s
  against 56.5 s predicted, limit up to 9.0 G, load up to 6.3 G; 10 s hold at 200.0 m (trail 200 m), no arrival
  counted. Margins: 30% min meter 0.310, 25% min meter 0.260. Stand-in radius 1 km: held at 2999 m. Boost off: limit
  3.00 G, load 2.54 G; boost on: limit 7.12 G. Warp: 100x and 1000x on the dropped target hold within 202 m and read
  HOLD; Moon at 1000x reads HOLD on every measured frame with the tail point within 3 m (5 m at 15 fps), min meter
  0.265. The meter stayed at or above the floor on every chase frame (103770).
- **(c) High warp.** The Amendment 2 (j) reading (hovering 14 m / 1.0 m/s off the tail at 1000x, BURST instead of
  HOLD) did not reproduce in standalone runs at 15-220 fps, so the section 3.8 fallback (cap the burst G at high warp)
  was not needed. It was seen in the editor at 3-60 fps (frames of up to 300 sim seconds at 1000x); left as is.
- **(d) Fix.** `SOLFlight::ChaseStep` resets its limit output at entry like its other outputs, so a zero, negative or
  non-finite substep writes default outputs (the 32c contract case).
- **(e) Cost (part 32f step 2).** Method: game-thread cycle stats in `USOLShipSubsystem` (`STATGROUP_SOLShip`:
  `SOL_ChasePlan` around the `SOLChase::PlanChase` call alone, `SOL_ChaseUpdate` around `UpdateChase` (target
  resolution, replan checks, the plan), `SOL_ShipFlightRun` around the flight processor run with its substeps), read with
  `stat dumpsum` then `stat dumpmax` over 1000 frames each by `-SOLSmokeChase -SOLSmokeChaseProfile` (standalone,
  1280x720, Development, i9-9960X, about 180-205 fps). Per plan call (sum / calls; max = the worst frame with one call):
  20 km chase 37 us mean, 41 us max, 21 plans in 5.6 s (3.8 per second); Moon chase from Earth 49 us mean, 74 us max,
  3.8 per second; Moon chase at 1 d/s (holding the tail) 16 us mean, 85 us max, one plan every frame (205 per second),
  because the `REPLAN_MAX_SIM_S` (30 s) sim timer fires on every frame of about 420 sim seconds (as section 3.8
  intends). Every plan is under the 0.1 ms budget, so the coarse-then-refine fallback is not needed. Ship step at 1 d/s,
  per frame: chase flight run 86 us mean (max 0.44 ms) against 83 us (max 0.51 ms) for a Move To Neptune at the same
  warp, so the chase substeps cost about the autopilot's; the chase update adds 25 us per frame (16 us of it the
  per-frame plan) against 0.5 us without a chase. At 1x: chase update 8-11 us and flight run 23-25 us per frame.

### Amendment 4: review fixes (2026-11-03)

From the adversarial performance and style review of #32. Items (c) and (d) make the build match sections 3.7 and 3.9;
the rest change no behaviour.

- **(a) Chase fragment (supersedes the fragment split in section 3.8).** The chase inputs, the policy, the run state
  and the last substep's limit moved out of `FSOLShipControlFragment` and `FSOLShipStateFragment` into a new
  `FSOLShipChaseFragment` (`Ship/SOLShipFragments.h`, 232 bytes), carried only by the player ship's archetype. The
  flight processor requires it as `EMassFragmentPresence::Optional` (read-write); chunks without it skip all chase
  work, and the per-entity chase setup runs only while `bChase` is set. The control and state fragments are back to
  their pre-#32 sizes, so NPC ships (#40) pay nothing for the chase.
- **(b) Target-turn replan trigger.** The "target velocity changed" trigger compares the target's velocity relative to
  its frame (as the aim point uses it) with the one at the last plan, so the frame's own orbit (the Sun's pull on an
  Earth-frame target under warp) no longer replans. Its 1 m/s floor is `SOL::Chase::REPLAN_TARGET_DV_MIN_MPS`; the
  feasibility slack of the replan comparison (Amendment 2 (d)) is `SOL::Chase::REPLAN_FEASIBILITY_TOLERANCE`. The
  guidance and plan-input arrival tolerances (10 m, 1 m/s) are now named once, `SOL::AUTOPILOT_ARRIVAL_RADIUS_M` and
  `SOL::AUTOPILOT_ARRIVAL_SPEED_MPS`.
- **(c) Jump To ends the chase.** `USOLJumpSubsystem::StartJump` (both the map's J and the Jump To action) calls
  `USOLShipSubsystem::DisengageChase`, so the ship is in ASSISTED for the whole jump warp exactly as a jump started from
  manual assisted flight (before, the chase kept bursting up to 9 G through the 3 s warp until the arrival's
  `ResetPilotState`). A plain autopilot order is unchanged: it still flies through the warp until the arrival.
  `-SOLSmokeChase` gains the check `z-jump-ends-chase`.
- **(d) Target-lost note.** The loss time is recorded (world real time) by `USOLShipSubsystem` where the loss is
  counted (`GetChaseTargetLostRealS`); `USOLInteractionSubsystem::IsChaseTargetLostNoteShown` only reads it, so a loss
  while the map is open no longer shows late. The note shows only while no autopilot order or chase runs, so it never
  hides a newly ordered AUTOPILOT or CHASE marker.
- **(e) HUD.** The chase marker takes the target name as an `FStringView` from a stack `FNameBuilder` (no heap string
  per frame). A resume meter above a full meter or not finite (the cool off lasts until the brake) is not shown:
  "COOL 1.5 G" without "(resume at ...)" and no resume tick on the G meter (`SOLHudFormat::IsChaseResumeShown`).

