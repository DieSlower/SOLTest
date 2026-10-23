<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 19 — Flight modes (manual / assisted / auto) and controls

Issue `#19` (ticket T3 of [`../Research/LT_features_for_SOL.md`](../Research/LT_features_for_SOL.md) section F, minus
the orders UI, which moves to #20 / #22). It builds on:

- [`18-newtonian-flight-core.md`](18-newtonian-flight-core.md): the Newtonian core, the assist controller, the pilot
  G model, the autopilot guidance with its ETA, and the `AUTOPILOT` HUD marker. Section 3.8 there lists the
  interfaces this ticket consumes.
- [`8-hud-menus-settings.md`](8-hud-menus-settings.md): key rebinding (`SOLKeyBindings`), the settings save and its
  versioned migration, and the Controls / Info pages.
- [`2-foundations-flight-scaffold.md`](2-foundations-flight-scaffold.md): the virtual joystick and the flight HUD.

Plan of record: [`../Plans/19-flight-modes-and-controls-plan.md`](../Plans/19-flight-modes-and-controls-plan.md).

**Status: draft for review.** Section 2 holds the user's decisions from the `grill-me` session (2026-10-22). Section
3 is the design within them. Section 5 lists the details that are still open, each with a recommended answer; the
19a contract and its tests are written against the recommended answers.

---

## 1. Problem

After #18 the ship has two modes behind one boolean (`FSOLShipControl::bFlightAssist`, toggled by Tab), and an
autopilot that only the `sol.Autopilot` console command can start or stop. The mouse is an always-on virtual stick,
so the cursor can never be used for anything else, and the key layout still reflects the old arcade model (Space up,
Left Shift boost held).

Issue #19 gives the ship three named flight modes with a clean state machine, a steering gate so the mouse steers
only when the player asks for it (LT style), and a reworked key layout. It provides the API that the map (#20) and
the interaction menus (#22) will use to start the autopilot.

## 2. Decisions (user-resolved, grill 2026-10-22)

| # | Area | Decision |
|---|---|---|
| 1 | Mouse steering (assisted) | **Hold Space** to steer until released. **Clicking the mouse wheel** (middle button) **toggles** steering on / off (persistent). The cursor's offset from the screen centre is the command (LT style, research doc A.1). Steering **recenters automatically when it turns on**, so the old `RecenterStick` binding moves to **B**. |
| 2 | Manual mode | The **same steering gate**. The mouse offset commands **pitch / yaw torque** (Q / E roll torque), with inertia; **stop spin stays on Z**. To be revisited later. |
| 3 | Keys | **Left Shift = thrust up** (new default); it must not control the ship while the map is open. **Boost moves to Tab and becomes a toggle** (not hold). The **assist / manual toggle moves to C**. **Space is the steer-hold key** (new binding `SteerHold`, default `SpaceBar`); `ThrustUp` defaults to `LeftShift`. **Left Ctrl stays thrust down.** `RecenterStick` defaults to **B**. Key reservations and conflicts (F1, F2, Ctrl+F1 / F2), rebinding conflict detection and the **migration of saved key overrides** (settings schema versioned and migrated, never discarded) are handled. The Controls table, the Info text, `GAME_MECHANICS.md` and the settings key-binding tests are updated. |
| 4 | HUD | A **mode label** (`MANUAL` / `ASSISTED` / `AUTO`) near the speed panel, **color-coded**, plus a **mouse-steer state icon**. The camera is unchanged for now. |
| 5 | Full auto | **No hotkey in this ticket.** It is reached from the map and the radial / list interaction menus in #20 / #22. #19 provides the clean **engage / disengage API**, the **cancel-on-manual-input rule** (any manual thrust or steer input cancels auto; the HUD keeps #18's `AUTOPILOT` marker and ETA), the **three-mode state machine** (pure logic, test-first) and the transitions: manual <-> assisted by **C**; auto entered and left through the API, plus the console `sol.Autopilot`. |
| 6 | G meter, boost | **SDD 18's rules are kept.** Boost is x3 within the 9 G total cap. |

## 3. Design

### 3.1 Pure logic: `Flight/SOLFlightMode.{h,cpp}`

A new pure-logic unit, `namespace SOLFlightMode` (no world access, no allocation, no UObjects), tested by
`SOLTest.FlightModeMath.*` (written first, 19a). The exact contract is in the plan ("19a contract"). It holds four
small state machines:

1. **Flight mode** (`FSOLFlightModeState`, `ESOLFlightMode { Manual, Assisted, Auto }`).
2. **Steering gate** (`FSOLSteerGateState`): hold OR toggle, suspended by menus, with a recenter edge.
3. **Boost toggle** (`FSOLBoostToggle`).
4. **Manual-input detection** (`FSOLPilotInput`, `IsManualInput`) used by the auto cancel rule.

The key-layout changes and their save migration are in `SOLKeyBindings` (3.5).

### 3.2 Flight mode state machine

| From | Event | To | Notes |
|---|---|---|---|
| Manual | C (`ToggleAssist`) | Assisted | |
| Assisted | C | Manual | |
| Auto | C | Assisted | Counts as taking control (open question Q2). |
| any | `EngageAuto` (API: console now, map / menus in #20 / #22) | Auto | Already in Auto: the mode stays, the new order replaces the old one, and the input latch is re-armed. |
| Auto | `DisengageAuto` (API, `sol.Autopilot Off`, jump arrival, respawn, order lost) | Assisted | |
| Auto | Autopilot arrived | Assisted | Assisted then holds the reference frame's velocity (SDD 18 Amendment 7 L2). |
| Auto | Manual input while the pilot has control | Assisted | The cancel rule (3.3). |
| Manual / Assisted | Disengage, arrival, manual input | unchanged | No-ops. |
| any | C during G-LOC | unchanged | The pilot is unconscious (SDD 18: input ignored). |

Every exit from Auto lands in **Assisted** (open question Q1): it is the safe mode (velocity hold at 1.5 G), and it
matches the research doc's "hands back to mode 2 on arrival or on any manual input".

The flight processor still reads one boolean: `FlightAssistFor(mode)` is false for Manual and true for Assisted and
Auto (so a frame where the order disappears falls into assisted flight, never into an uncommanded manual coast).

The player's mode lives in `USOLShipSubsystem`, next to the autopilot order it already owns
(`FSOLShipControlFragment::AutopilotBodyIndex`) and `ResetPilotState`. The start mode still comes from the Settings
"Flight assist on at launch" (`MakeInitialState(bAssistOn)`). Nothing about the mode is saved.

### 3.3 Cancel-on-manual-input rule

Each frame the pawn hands the subsystem its **gated** pilot input (`FSOLPilotInput`: thrust fractions, roll, the
steering stick command after the gate, stop spin). In Auto:

- **Manual input** is any thrust axis, roll or stick component with `|x| > MANUAL_INPUT_THRESHOLD` (1e-3), or stop
  spin held. Non-finite values do not count. Boost, the speed cap wheel, targeting, warp, the M / L locks, free-look
  and the steering gate itself do **not** cancel (open question Q3).
- Mouse motion while steering is **off** never cancels: the gate zeroes the stick command first.
- **Input latch.** Input already held at the moment auto engages (for example a key still down when the map closes)
  does not cancel. It is latched, and the latch clears on the first frame with no manual input. Only input after that
  cancels (open question Q4).
- **G-LOC.** While the pilot has no control, input never cancels (it is ignored), and the engaged autopilot keeps
  flying at its recovery limit (SDD 18 decision 13). The latch can still clear while blacked out.

On a cancel the mode becomes Assisted, the autopilot order is cleared (`AutopilotBodyIndex = INDEX_NONE`), and the
same frame's input flies the ship in assisted mode.

### 3.4 Steering gate (hold, toggle, recenter)

- **Active** = (`SteerHold` held **or** the `SteerToggle` latch on) **and** not suspended. Holding Space while the
  toggle is on keeps steering on, and releasing it does not turn steering off (OR, open question Q6).
- **Suspended** while the jump map, the pause menu or the F3 speed panel has input, during the jump warp, and while
  the viewport is unfocused. Suspension (and focus loss) also releases the hold. The toggle latch survives.
- **Recenter on every off-to-on edge**: the virtual stick returns to the centre whenever steering turns on, including
  when it resumes after a suspension. `B` (`RecenterStick`) still recenters on demand.
- **While inactive** the stick command is zero, mouse deltas do not move the stick (Alt free-look still orbits the
  camera), and the OS cursor is shown and free (open question Q8). This is the groundwork for the G-key icons and
  the radial / list menus of #22. The left mouse still fires along the camera aim.
- **While active** the cursor is hidden and captured as today. The command is the stick's offset from the centre,
  through SOL's existing virtual stick (radius 0.35 of the short side, dead zone 0.05, exponent 1.5,
  `SOLFlight::JoystickToRotation`) rather than LT's linear absolute cursor offset (open question Q9).
- **Assisted** reads the command as a rate (as today). **Manual** reads it as pitch / yaw torque (as #18 built): when
  steering turns off, the torque goes to zero but the spin persists until Z.
- **Start state** of the toggle at each flight start: ON (open question Q7). It is not saved.
- **HUD icon** (`SteerIconFor`): `Locked` when the toggle is on, else `Held` while Space is held, else `Free`.

### 3.5 Keys, reservations and the settings migration

**New and changed defaults** (`SOLKeyBindings`, `KEY_BINDING_DEFS`, `SOLMenuText::KEY_BINDING_TEXTS`):

| Binding id | Old default | New default | Behavior |
|---|---|---|---|
| `ThrustUp` | SpaceBar | **LeftShift** | Hold, unchanged |
| `ThrustDown` | LeftControl | LeftControl | Unchanged |
| `Boost` | LeftShift (hold) | **Tab (toggle)** | Press toggles boost on / off |
| `ToggleAssist` | Tab | **C** | Manual <-> assisted; in Auto: cancel to assisted |
| `RecenterStick` | MiddleMouseButton | **B** | Recenter the stick |
| `SteerHold` (new) | - | **SpaceBar** | Hold to steer with the mouse |
| `SteerToggle` (new) | - | **MiddleMouseButton** | Click to toggle mouse steering (open question Q10: rebindable) |

The two new bindings sit right after `StopSpin` in `EBinding` order (Settings and Controls order). The table grows
from 29 to 31 rows. All defaults stay distinct, bindable and outside the reserved keys (Escape, the mouse axes and
wheel, P, F1, F2). `LeftShift` and `MiddleMouseButton` are jump-map fixed keys, which only the map toggle is refused;
the ship's whole mapping context is removed while the map is open (`SuspendShipControl`), so Left Shift never
thrusts with the map open. 19b adds a smoke check for that. Ctrl+F1 / Ctrl+F2 are unaffected: Left Ctrl was already
thrust down.

**Rebinding** is unchanged: a duplicate key is accepted and both rows are flagged until one changes.

**Settings schema 2 -> 3** (`SOL::Settings::SAVE_VERSION` = 3). Saved overrides are deltas from the defaults, keyed
by binding id, and no id changes. The migration (`SOLKeyBindings::MigrateOverrides`, called from
`USOLSettingsSave::MigrateToCurrentVersion`) therefore:

- keeps every saved override verbatim: an explicit player choice always wins;
- lets every binding the player never rebound pick up its new default (implicit, since overrides are deltas);
- changes nothing else, and does not resolve conflicts silently. A saved override that now shares a key with a new
  default (for example `ClearTarget = C` against `ToggleAssist = C`) is flagged in Settings like any duplicate, and
  both actions fire until the player changes one (open question Q11).

A schema 1 save migrates 1 -> 2 (orbit lines) and then 2 -> 3. `SOLKeyBindings::DefaultLayoutChangedSince(version)`
tells 19c whether to show a one-time "default controls changed" notice (open question Q11). The version bump also
makes sure an old save is classified Migrate and kept, never discarded.

### 3.6 Boost toggle

- `Boost` (Tab) toggles `FSOLBoostToggle::bOn`. While on, `FSOLShipControl::bBoost` is true: x3 thrust on every
  axis within the 9 G total cap, and the doubled assisted target speed (SDD 18, unchanged).
- It turns **off at G-LOC** (and cannot be toggled while the pilot has no control), so a pilot never wakes up still
  boosting. It is also cleared on jump arrival and respawn (`ResetPilotState`). It survives the map, the menus, the
  speed panel and focus loss, since it is a state and not a held key (open question Q12). It is not saved.
- In Auto, toggling boost does not cancel the autopilot. The autopilot ignores it, because it plans at the pilot
  limit, below the 3 G main engine.
- The speed readout's existing boost indicator shows the state.

### 3.7 HUD

- **Mode label** next to the speed readout, replacing today's `ASSIST ON` / `ASSIST OFF (manual)`. `MANUAL` amber,
  `ASSISTED` the HUD cyan, `AUTO` magenta (the map destination colour) (open question Q13). Text lives in
  `SOLMenuText`; the label and colour functions go in `SOLHudFormat` (tested in `HudFormatTest`, 19b).
- **Steer icon** beside it: three states, Free (an open cursor glyph), Held, Locked (a closed reticle with a lock
  tick) (open question Q14).
- **AUTOPILOT marker and ETA** from #18 are kept. The cancel hint under it becomes
  "Thrust or steer to take control" (`SOLMenuText::AUTOPILOT_CANCEL_HINT`), replacing the console hint (open
  question Q16).
- The camera is unchanged (decision 4).

### 3.8 Integration (19b)

- **Pawn** (`ASOLShipPawn`): owns the steering gate and the boost toggle (input-side state). New Enhanced Input
  actions `IA_ShipSteerHold` and `IA_ShipSteerToggle`; `Boost` becomes a press action. Each tick it updates the gate
  with its suspension state, recenters on the edge, shows or captures the cursor on a change, gates the stick,
  updates the boost toggle from `PilotHasControl`, and hands the `FSOLPilotInput` to the subsystem. It reads
  `bFlightAssist` from the mode, not its own boolean.
- **`USOLShipSubsystem`**: owns `FSOLFlightModeState` for the player. API: `GetFlightMode`, `ToggleAssist`,
  `EngageAutopilot(bodyIndex, offsetDir)`, `DisengageAutopilot`, `ApplyPilotInput`. The processor's arrival flag calls
  `NotifyAutopilotArrived`. `ResetPilotState` disengages and resets boost. `sol.Autopilot <Body|Target|Off>` routes
  through the same API.
- **Docs and text** (19c): `GAME_MECHANICS.md` sections 1-2, `SOLMenuText` (`CONTROL_ROWS`, `KEY_BINDING_TEXTS`,
  `INFO_PROSE`, the mode-label texts and the cancel hint), and `ARCHITECTURE.md`.

### 3.9 Performance

One player ship: a few branches per frame. No allocation, no new Mass processor, no change to the NPC path.

### 3.10 Testing and verification

- **19a:** `SOLTest.FlightModeMath.*` (mode machine, cancel rule and latch, steering gate, boost, key defaults and
  migration), written first against the plan's contract by a separate agent. Re-validate `SOLKeyBindingsTest` and
  `SOLSettingsRulesTest`: several `KeyBindings` cases assert the old Boost default `LeftShift`.
- **19b:** `HudFormatTest` cases for the label and colour. Re-validate `PlayerFlightTest` and `FlightStepTest` if
  `FSOLShipControl` changes. `-SOLSmokeInput` phases: Space hold steers and recenters, MMB toggles, mouse motion is
  ignored with steering off, C cycles manual / assisted, Tab latches boost, Left Shift thrusts up but not with the map
  open, and `sol.Autopilot` then W cancels to ASSISTED. PIE screenshots of the three labels and icons.
- **19c:** the Controls / Info pages and the Settings key list (31 rows), the one-time notice, and the migration of a
  real v2 save file.
- **19d:** adversarial review, full suite, docs, commit.

## 4. Non-unit-tested pieces

- The pawn's Enhanced Input actions, the cursor show / capture switching, and the suspension sources.
- The subsystem wiring (arrival hook, console, `ResetPilotState`).
- The HUD drawing of the label and the icon (the text and colour functions are tested).
- The Controls / Info / Settings UI and the notice.

## 5. Open questions (recommended answers used by the 19a contract)

- **Q1. Where auto hands back.** (Recommended) Every exit from Auto (arrival, cancel, C, API, jump) lands in
  **Assisted**. Alternative: return to the mode the player was in before auto engaged.
- **Q2. C during Auto.** (Recommended) It cancels auto to Assisted. Alternative: C is ignored while Auto.
- **Q3. What cancels auto.** (Recommended) Thrust (W / S / A / D / Left Shift / Left Ctrl), roll (Q / E), the gated
  mouse stick outside the dead zone, and stop spin (Z). Not boost, the speed cap, targeting, warp, the M / L locks,
  free-look or the steering toggle. Alternative: only thrust and roll cancel (mouse steering never does).
- **Q4. Input held when auto engages.** (Recommended) It is latched and ignored until it is released once.
  Alternative: no latch (a key held at engage cancels at once).
- **Q5. G-LOC.** (Recommended) Input never cancels during G-LOC; C and the boost toggle are ignored; boost turns off
  at G-LOC; the steering toggle still works (it is a mouse mode, and the stick is zeroed anyway).
- **Q6. Hold plus toggle.** (Recommended) OR: either one steers, and the stick recenters on every off-to-on edge,
  including the resume after the map, a menu, the speed panel or focus loss. Alternative: holding Space while the
  toggle is on frees the cursor (inverts the toggle).
- **Q7. Steering toggle at flight start.** (Recommended) ON at every flight start (today's feel), not saved. With it
  on, mouse motion cancels the autopilot (Q3). Alternative: OFF (pure LT: Space or a wheel click to steer).
- **Q8. Steering off.** (Recommended) The OS cursor is shown and free, mouse motion does not steer, Alt free-look
  still works, and the left mouse still fires along the camera aim. Alternative: keep the cursor hidden until #22.
- **Q9. Stick shape.** (Recommended) Keep SOL's virtual stick (accumulated deltas, radius 0.35 of the short side, dead
  zone 0.05, exponent 1.5), which is an offset from the centre; the research doc recommends it over LT's linear
  absolute cursor offset. Alternative: LT's absolute cursor offset, linear, no dead zone.
- **Q10. Steering toggle key.** (Recommended) A rebindable binding `SteerToggle`, default middle mouse. Alternative:
  a fixed key.
- **Q11. Saved key overrides.** (Recommended) Adopt the new defaults for every binding the player had not rebound,
  keep every explicit override, and flag (not resolve) the conflicts this creates; show a one-time
  "Default controls changed" notice on the first launch after migrating a schema 1 or 2 save. Alternative: pin the
  old layout for existing players.
- **Q12. Boost toggle lifetime.** (Recommended) It survives the map, menus, speed panel and focus loss; it is cleared
  by G-LOC, a jump arrival and a respawn; it is not saved.
- **Q13. Label colours.** (Recommended) MANUAL amber, ASSISTED HUD cyan, AUTO magenta.
- **Q14. Steer icon.** (Recommended) Three states: Free, Held, Locked.
- **Q15. Frame on arrival (SDD 18 Amendment 7 L2).** (Recommended) No change in #19: assisted holds the reference
  frame (normally the arrival body, as the nearest anchor); an explicit lock on arrival is decided with #20's orders.
- **Q16. Cancel hint.** (Recommended) Replace "sol.Autopilot Off to cancel" with "Thrust or steer to take control".

## 6. Amendments

None yet.

## 7. Revision history

- 2026-10-22: initial draft from the issue #19 grill (decisions 1-6), with open questions Q1-Q16; 19a contract and
  tests written.

## Grill resolution (2026-10-23)

The user accepted the recommended answer to every open question Q1-Q16 in section 5 as written
(auto-mode exit and cancel rules, G-LOC and steering-gate rules, virtual stick kept, rebindable
`SteerToggle`, key migration with a one-time notice, boost-toggle lifetime, HUD label colours and
steer-icon states, no reference-frame change on arrival in #19, cancel hint text). No question is
open. Implementation (19a-19d) was paused at the 70% weekly usage limit and resumes after the reset.
