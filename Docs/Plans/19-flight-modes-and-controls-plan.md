<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 19 — Flight modes (manual / assisted / auto) and controls

Design: [`Docs/SDDs/19-flight-modes-and-controls.md`](../SDDs/19-flight-modes-and-controls.md). The user's grill
decisions are SDD section 2. The 19a contract below holds for the **recommended** answers to the open questions
Q1-Q16 (SDD section 5). If an answer changes a contract rule, update the contract and the matching test together.

- [ ] **19a — Pure logic, test-first.** Tests written 2026-10-22 against the contract below, before any production
  code: `Source/SOLTest/Tests/FlightModeMathTest.cpp` (`SOLTest.FlightModeMath.*`). Build
  `Source/SOLTest/Flight/SOLFlightMode.{h,cpp}` and the `SOLKeyBindings` / settings-save changes to the contract and
  make the tests pass without editing them. If a test contradicts the contract, stop and ask.
  - Mode state machine, cancel rule and input latch, steering gate (hold / toggle / suspension / recenter edge),
    steer icon, boost toggle.
  - `SOLKeyBindings`: `SteerHold` and `SteerToggle` after `StopSpin`; new defaults; `MigrateOverrides`;
    `DefaultLayoutChangedSince`. `SOLMenuText::KEY_BINDING_TEXTS` gets the two rows (the `static_assert` enforces it).
  - `SOL::Settings::SAVE_VERSION` = 3; `USOLSettingsSave::MigrateToCurrentVersion` adds the 2 -> 3 step.
  - Re-validate (CLAUDE.md "modified classes"): `SOLKeyBindingsTest` (cases asserting `Boost` = `LeftShift`, at
    lines ~113, ~149, ~281, ~414 in the 18 version, and the "boost B" override now sharing `RecenterStick`'s B) and
    `SOLSettingsRulesTest` (version-agnostic today; confirm). Done by a test-author agent per CLAUDE.md.
- [ ] **19b — Controls, pawn and subsystem integration, HUD label and icon.**
  - Pawn: `IA_ShipSteerHold`, `IA_ShipSteerToggle`, boost as a press toggle; gate update, recenter on the edge,
    cursor show / capture on a change, gated stick; boost toggle from `PilotHasControl`; hand `FSOLPilotInput` to the
    subsystem; `bFlightAssist` from the mode.
  - `USOLShipSubsystem`: owns `FSOLFlightModeState`; `GetFlightMode`, `ToggleAssist`, `EngageAutopilot`,
    `DisengageAutopilot`, `ApplyPilotInput`; arrival -> `NotifyAutopilotArrived`; `ResetPilotState` disengages and
    resets boost; `sol.Autopilot` through the same API.
  - HUD: mode label (replacing ASSIST ON / OFF) and colour, steer icon, new cancel hint; `HudFormatTest` cases.
  - Smoke (`-SOLSmokeInput` phases, SDD 3.10) and PIE screenshots.
- [ ] **19c — Settings, key-binding UI, migration, Info and Controls text.**
  - Settings key list (31 rows) and conflict flags with the new defaults; the one-time "default controls changed"
    notice (Q11); a real v2 save migrated in the editor.
  - `SOLMenuText`: `CONTROL_ROWS` (Steer hold / toggle, Boost toggle, Thrust up, Toggle assist, Recenter), `INFO_PROSE`
    (modes, steering gate, cancel rule), mode labels; `GAME_MECHANICS.md` sections 1-2 and 8; `SOLInfoTextTest` and
    `SOLKeyBindingsTest` row checks.
- [ ] **19d — Docs, review, verification.**
  - `ARCHITECTURE.md` (mode owner, input flow), SDD amendments.
  - Adversarial opus review (performance + style) and fixes; full suite; `graphify update .`; commit and push
    (`Issue #19: ...`).

## 19a contract

**Files.**

- Production: `Source/SOLTest/Flight/SOLFlightMode.h/.cpp`. The plain types below at global scope, the functions in
  `namespace SOLFlightMode` with `SOLTEST_API`. No UObjects, no `USTRUCT` / `UENUM`, no world access, no allocation,
  no global mutable state. Includes `CoreMinimal.h` only.
- Production changes: `Menu/SOLKeyBindings.{h,cpp}`, `Menu/SOLMenuText.h` (`KEY_BINDING_TEXTS`),
  `Menu/SOLSettingsSave.cpp`, `SOLConstants.h` (`SOL::Settings::SAVE_VERSION`).
- Tests: `Source/SOLTest/Tests/FlightModeMathTest.cpp`, paths `SOLTest.FlightModeMath.<Case>`.

### Types

```cpp
enum class ESOLFlightMode : uint8 { Manual, Assisted, Auto };

enum class ESOLSteerIcon : uint8 { Free, Held, Locked };

struct FSOLFlightModeState
{
    ESOLFlightMode Mode = ESOLFlightMode::Assisted;
    bool bInputLatched = false;     // Auto only: input held at engage, ignored until released once
};

struct FSOLPilotInput               // this frame's manual input, AFTER the steering gate
{
    FVector3d Thrust = FVector3d::ZeroVector;   // local fractions X fwd, Y right, Z up
    double Roll = 0.0;                          // [-1, 1]
    FVector2d Steer = FVector2d::ZeroVector;    // gated stick command (X yaw, Y pitch)
    bool bStopSpin = false;
};

struct FSOLSteerGateState
{
    bool bToggleOn = false;         // SteerToggle latch
    bool bHoldDown = false;         // SteerHold held
    bool bWasActive = false;        // gate result of the previous update (edge detection)
};

struct FSOLSteerGateUpdate
{
    bool bActive = false;           // the mouse steers this frame (cursor captured)
    bool bRecenter = false;         // off-to-on edge: recenter the stick now
};

struct FSOLBoostToggle
{
    bool bOn = false;
};
```

### Constants (`namespace SOLFlightMode`)

| Name | Value | Meaning |
|---|---|---|
| `MANUAL_INPUT_THRESHOLD` | 1e-3 | A component counts as manual input only when its magnitude is strictly greater |

### Functions: flight mode

| Function | Rule |
|---|---|
| `FSOLFlightModeState MakeInitialState(bool bAssistOn)` | Assisted when true, Manual when false; latch false. |
| `bool IsAuto(const FSOLFlightModeState&)` | Mode == Auto. |
| `bool FlightAssistFor(ESOLFlightMode)` | Manual false; Assisted true; Auto true. |
| `bool IsManualInput(const FSOLPilotInput&)` | True if `bStopSpin`, or any of Thrust.X/Y/Z, Roll, Steer.X/Y has `abs > MANUAL_INPUT_THRESHOLD`. Non-finite components never count (NaN and +-inf are ignored). |
| `bool ToggleAssist(FSOLFlightModeState&, bool bPilotHasControl)` | No control: unchanged, false. Manual -> Assisted, Assisted -> Manual, Auto -> Assisted (latch cleared); returns true. |
| `bool EngageAuto(FSOLFlightModeState&, const FSOLPilotInput& heldInput)` | Mode = Auto; `bInputLatched = IsManualInput(heldInput)` (re-armed even if already Auto). Returns true if the mode changed (false if it was already Auto). Allowed with or without pilot control. |
| `bool DisengageAuto(FSOLFlightModeState&)` | Auto -> Assisted, latch cleared, true. Otherwise unchanged, false. |
| `bool NotifyAutopilotArrived(FSOLFlightModeState&)` | Same as `DisengageAuto`. |
| `bool ApplyPilotInput(FSOLFlightModeState&, const FSOLPilotInput&, bool bPilotHasControl)` | Not Auto: unchanged, false (the latch is untouched and stays false). Auto, latched: if `!IsManualInput` the latch clears (with or without control); never cancels this frame; false. Auto, not latched: if `bPilotHasControl && IsManualInput` -> Assisted, true; otherwise false. |

### Functions: steering gate

| Function | Rule |
|---|---|
| `FSOLSteerGateState MakeSteerGate(bool bToggleOn)` | Toggle as given, hold false, `bWasActive` false (so the first update with the toggle on is an edge and recenters). |
| `void SetSteerHold(FSOLSteerGateState&, bool bDown)` | Sets `bHoldDown`. |
| `void ToggleSteer(FSOLSteerGateState&)` | Flips `bToggleOn`. |
| `FSOLSteerGateUpdate UpdateSteerGate(FSOLSteerGateState&, bool bSuspended)` | `active = (bToggleOn \|\| bHoldDown) && !bSuspended`; `recenter = active && !bWasActive`; then `bWasActive = active`. Does not change the toggle or the hold. |
| `FVector2d GateSteer(const FVector2d& stickCommand, bool bActive)` | The command when active, else zero. |
| `ESOLSteerIcon SteerIconFor(const FSOLSteerGateState&)` | `Locked` if `bToggleOn`; else `Held` if `bHoldDown`; else `Free`. Ignores suspension. |

### Functions: boost toggle

| Function | Rule |
|---|---|
| `bool ToggleBoost(FSOLBoostToggle&, bool bPilotHasControl)` | With control: flips `bOn`. Without: unchanged. Returns the resulting `bOn`. |
| `void UpdateBoost(FSOLBoostToggle&, bool bPilotHasControl)` | Without control: `bOn = false`. With control: unchanged (it never turns itself back on). |
| `void ResetBoost(FSOLBoostToggle&)` | `bOn = false` (jump arrival, respawn). |

### Key bindings (`namespace SOLKeyBindings`)

- `EBinding` gains `SteerHold` and `SteerToggle`, in that order, directly after `StopSpin`. Ids `"SteerHold"`,
  `"SteerToggle"`. `BINDING_COUNT` becomes 31. `KEY_BINDING_TEXTS` gets an Area and a Label for each.
- Defaults: `ThrustUp` `LeftShift`, `ThrustDown` `LeftControl`, `Boost` `Tab`, `ToggleAssist` `C`, `StopSpin` `Z`,
  `SteerHold` `SpaceBar`, `SteerToggle` `MiddleMouseButton`, `RecenterStick` `B`. Every other default is unchanged.
  All defaults remain distinct, bindable (`CanBindKey`) and not reserved.
- `TMap<FString, FString> MigrateOverrides(const TMap<FString, FString>& overrides, int32 fromVersion)`: returns the
  overrides **verbatim** (same entries, keys and values, invalid ones included; `ResolveKeys` keeps ignoring those)
  for every `fromVersion`. Schemas 1-3 share binding ids, so an explicit choice survives and every binding without
  an override takes its new default. Conflicts are not resolved.
- `bool DefaultLayoutChangedSince(int32 savedVersion)`: true for `1 <= savedVersion < 3`; false for 3 and above,
  and for `savedVersion <= 0`.
- Unchanged: `CanBindKeyTo` (the map toggle still refuses `LeftShift` and `MiddleMouseButton`; any other binding may
  take them), `Rebind`, `HasConflict`, `FindConflict`, `ResolveKeys`, `MakeOverrides`.

### Settings save

- `SOL::Settings::SAVE_VERSION` = 3. `ClassifySaveVersion(1 or 2, 3)` is Migrate (existing rule).
- `USOLSettingsSave::MigrateToCurrentVersion`: after the 1 -> 2 step, a version 2 save runs
  `KeyOverrides = SOLKeyBindings::MigrateOverrides(KeyOverrides, 2)` and becomes version 3. Every other field is
  untouched. Current, unversioned and newer saves stay unchanged (existing rule).

### Test list (`SOLTest.FlightModeMath.*`)

| Case | Covers |
|---|---|
| `InitialState` | `MakeInitialState` both ways, latch false, `IsAuto` |
| `FlightAssistMapping` | `FlightAssistFor` for the three modes |
| `ToggleAssistCycle` | Manual <-> Assisted, Auto -> Assisted with the latch cleared, return values |
| `ToggleAssistIgnoredWithoutControl` | C during G-LOC in every mode |
| `EngageFromEveryMode` | Manual / Assisted -> Auto, return true, latch from the held input |
| `EngageWhileAutoRearmsLatch` | Re-engage returns false, keeps Auto, re-arms or clears the latch |
| `DisengageAndArrival` | Both exits land in Assisted; no-ops outside Auto |
| `ManualInputDetection` | Each component alone, the threshold boundary, negatives, stop spin, NaN / inf |
| `CancelOnEachManualInput` | Each input kind cancels Auto to Assisted; the next frame's same input does nothing more |
| `NoCancelBelowThreshold` | Sub-threshold noise and zero input keep Auto |
| `NoCancelOutsideAuto` | `ApplyPilotInput` in Manual / Assisted changes nothing |
| `NoCancelWithoutControl` | G-LOC input keeps Auto; control back plus input cancels |
| `LatchedInputDoesNotCancel` | Held at engage, still held: no cancel; released: latch clears without cancel; next input cancels |
| `LatchClearsWithoutControl` | Release during G-LOC clears the latch |
| `SteerGateHold` | Hold on / off, recenter only on the press edge |
| `SteerGateToggle` | Toggle on / off persistence, recenter on the toggle-on edge |
| `SteerGateHoldAndToggleOr` | OR semantics: no recenter when the second source joins; releasing one keeps steering |
| `SteerGateSuspension` | Suspension deactivates, keeps the toggle, resume recenters; hold released during suspension |
| `SteerGateInitialToggle` | `MakeSteerGate(true)` recenters on the first update; `(false)` is inactive |
| `SteerGateGatesStick` | `GateSteer`; a gated-off stick is not manual input and does not cancel Auto |
| `SteerIcon` | Free / Held / Locked, priority of the toggle, suspension ignored |
| `BoostToggle` | Toggle on / off with control, return value |
| `BoostOffWithoutControl` | Toggle refused and `UpdateBoost` clears at G-LOC; it stays off after control returns |
| `BoostReset` | `ResetBoost` |
| `KeyDefaults` | New ids exist after `StopSpin`, the eight defaults, count 31, labels present |
| `KeyDefaultsBindable` | New defaults bindable, not reserved, distinct; map toggle refuses Left Shift / MMB; F1 / F2 refused for the new bindings |
| `KeyMigrationAdoptsNewDefaults` | An empty v2 override map resolves to the new layout |
| `KeyMigrationKeepsOverrides` | Explicit v1 / v2 overrides survive verbatim, including invalid entries |
| `KeyMigrationFlagsConflicts` | Overrides on C, SpaceBar, MMB, Tab, B, LeftShift collide with new defaults and are flagged; rebinding clears |
| `KeyMigrationIdempotent` | Migrating twice is the same as once |
| `LayoutChangedSince` | `DefaultLayoutChangedSince` for -1, 0, 1, 2, 3, 4 |
| `SettingsSaveMigratesKeys` | v2 and v1 `USOLSettingsSave` objects migrate to `SAVE_VERSION` (>= 3) with overrides and other fields kept; current saves untouched |
