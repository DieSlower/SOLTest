<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 36: Radar manual zoom floor of 1 km with or without a target

**Issue:** [#36](https://github.com/DieSlower/SOLTest/issues/36), "Radar cannot zoom in below 10,000 km unless a
target is selected". Plan: [`Plans/36-radar-manual-zoom-floor-plan.md`](../Plans/36-radar-manual-zoom-floor-plan.md).
Supersedes the manual-range parts of SDD 2 Appendix C Amendments 1 and 3 (radar floor depends on the target) and the
"no snapping" rule of the manual zoom steps.
Status: built and reviewed (2026-11-10). Grill-me done 2026-11-10 (D1-D7); no open questions.

## 1. Problem

With no target selected the radar's smallest range is 10,000 km in both AUTO and MANUAL, and `-` does nothing once it
is there. The player wants to zoom in to 10 km or 1 km at any time, for example to see nearby NPC ships.

Cause: one function, `SOLRadar::EffectiveFloorM(bHasSelectedTarget)` (`UI/SOLRadarLayout.cpp`), returns
`TargetedFloorM` (1 km) with a target and `DefaultFloorM` (10,000 km) without one, and it is used for three things:

1. `SOLRadar::StepManualRange` clamps the manual zoom to it (so `-` stops at 10,000 km with no target).
2. `ASOLFlightHud::DrawRadar` uses it as the AUTO range (`rangeM = mIsRadarManual ? mManualRadarRangeM : floorM`).
3. `ASOLFlightHud::DrawRadar` re-clamps the stored manual range to it every frame (so dropping a target while zoomed
   in pushes the manual range back up to 10,000 km).

This was a deliberate choice (SDD 2 Appendix C Amendment 1 "Target-floor tracking", Amendment 3 "AUTO is the fixed
floor"), documented in `Docs/GAME_MECHANICS.md` (Radar row). The fix separates the two meanings: the **AUTO range**
(still target-dependent) and the **manual zoom floor** (now a single constant), and makes the manual steps land on
powers of ten.

## 2. Decisions (grill-me, 2026-11-10)

Recorded verbatim:

- **D1:** The manual zoom floor is 1 km regardless of target selection (with and without a target). No new setting, no
  persistence, NO schema change (SAVE_VERSION 11 and KEY_BINDING_LAYOUT_VERSION 10 stay; settings layout unchanged).
- **D2:** The user picks the value by how far they zoom: the manual zoom steps go down through 10 km to 1 km (check the
  existing step ladder; make sure both 10 km and 1 km are reachable steps with no target, and the `-` key no longer
  dead-ends at 10,000 km).
- **D3:** AUTO range keeps its 10,000 km default with no target (unchanged). With a target AUTO behaviour is unchanged.

Answers to the open questions of the first draft:

- **D4 (was Q1):** Selecting or clearing a target while the radar is in MANUAL keeps the mode: the radar stays MANUAL.
- **D5 (was Q2):** ... and keeps the manual range, across any number of target changes, for the session only (not
  saved; D1).
- **D6 (was Q3, changed from the recommendation):** Manual steps snap to powers of ten, so 10 km is always a stop.
  Zoom in = the largest power of ten (in metres) strictly below the current range; zoom out = the smallest power of ten
  strictly above it; then clamp to `[1e3, MaxRangeM]`. 1 AU (`MaxRangeM`) is the top clamp and is not a power of ten.
  Exact powers step by one decade. Exact powers must not be misclassified by floating point (section 4).
- **D7 (was Q4):** Names: new `MANUAL_FLOOR_M`; `DefaultFloorM` -> `AUTO_RANGE_M`; `TargetedFloorM` ->
  `AUTO_RANGE_TARGETED_M`; `EffectiveFloorM` -> `AutoRangeM`; glossary tokens `RadarAutoFloor` / `RadarAutoFloorTarget`
  -> `RadarAutoRange` / `RadarAutoRangeTarget`; `StepManualRange` without the target parameter.

**Units note on D6.** The answer's worked examples read "from 1 AU zoom in gives 1e8; from 1e8 zoom out gives 1 AU"
and "1 AU, 1e8, 1e7 ... 1e3". In metres, as the rule itself states, the largest power of ten below 1 AU
(1.496e11 m) is **1e11 m**, which is 1e8 **km** (100,000,000 km); and from 1e8 m the next power up is 1e9 m. This SDD
applies the rule in metres: 1 AU in -> 1e11 m; 1e11 m out -> 1 AU (1e12 clamped). The ceiling ladder is therefore
1 AU, 1e11, 1e10, ..., 1e3 m (nine presses). The examples are taken as written in km. On the HUD label (km below
0.01 AU, AU with four decimals above) the stops read 1.0 km, 10.0 km, ..., 1000000.0 km, then 0.0668 AU (1e10 m),
0.6685 AU (1e11 m) and 1.0000 AU; the user kept this behaviour after the review (2026-11-10), so the two AU stops
are documented rather than changed.

**Ladder check for D2.** AUTO with no target is 10,000 km (1e7 m), and the first `-` starts from the range on screen
(`StepRadarZoom` uses `mLastRadarRangeM`), so `-` from AUTO with no target gives 1,000 km, 100 km, 10 km, 1 km (all
MANUAL), and a fifth `-` is a no-op. With D6 every manual stop below 1 AU is a power of ten, so 10 km is a stop from any
starting range. Labels: `RADAR 10.0 km (MANUAL)`, `RADAR 1.0 km (MANUAL)`.

## 3. Design

### 3.1 `SOLRadar` constants and functions (`UI/SOLRadarLayout.h` / `.cpp`)

| Today | After #36 | Value |
|---|---|---|
| `DefaultFloorM` (floor with no target, also the AUTO range) | `AUTO_RANGE_M`: AUTO range with no target | 1e7 m (10,000 km), unchanged |
| `TargetedFloorM` (floor with a target, also the AUTO range) | `AUTO_RANGE_TARGETED_M`: AUTO range with a target | 1e3 m (1 km), unchanged |
| (none) | `MANUAL_FLOOR_M`: manual zoom floor, any target state | 1e3 m (1 km) |
| (none) | `ZOOM_SNAP_RELATIVE_TOLERANCE`: a range this close (relative) to a power of ten counts as that power | 1e-9 |
| `MaxRangeM` | unchanged (top clamp, 1 AU) | 149597870700.0 |
| `ZoomStepFactor` | removed (the snap rule defines the decades; no other user) | |
| `EffectiveFloorM(bool bHasSelectedTarget)` | `AutoRangeM(bool bHasSelectedTarget)`: the AUTO range | as today |
| `StepManualRange(double currentRangeM, bool bZoomIn, bool bHasSelectedTarget)` | `StepManualRange(double currentRangeM, bool bZoomIn)`: snap to the next power of ten (D6), clamp `[MANUAL_FLOOR_M, MaxRangeM]` | |

New and renamed constants follow `Docs/STYLE_GUIDE.md` (UPPER_SNAKE_CASE). `MaxRangeM` and `CollapseFraction` keep
their legacy names (not renamed, D7 does not cover them). The manual floor equals the targeted AUTO range today (both
1 km) but they are separate constants: one is "how close AUTO looks in combat", the other "how far `-` may go".
`MANUAL_FLOOR_M <= AutoRangeM(any)` must hold so AUTO is never below the manual floor.

**Implementation note (not part of the contract).** Powers of ten from 1e0 to 1e12 are exact doubles as decimal
literals, so a small `constexpr` table (or the same values computed once by repeated multiplication from exact
literals) gives exact candidates; the step picks a table entry, `MANUAL_FLOOR_M` or `MaxRangeM`, so every result is an
exact constant and no `Pow`/`Log10` rounding reaches the output. A `Log10`-based guess is fine for finding the decade
as long as the final comparison is made against the exact table values with the tolerance below.

### 3.2 HUD (`UI/SOLFlightHud.cpp` / `.h`)

- `ASOLFlightHud::StepRadarZoom`: calls `SOLRadar::StepManualRange(currentM, bZoomIn)`. The rest is unchanged: the
  first press starts from the range on screen, a press that leaves the range unchanged is a no-op that keeps the
  current mode (SDD 2 Appendix C Amendment 4 item 3).
- `ASOLFlightHud::DrawRadar`: the AUTO range is `SOLRadar::AutoRangeM(HasSelectedTarget())` every frame, as today.
  The per-frame target-dependent re-clamp of `mManualRadarRangeM` is removed: the stored manual range is only ever
  written from `StepManualRange`, which already clamps to `[MANUAL_FLOOR_M, MaxRangeM]`. Nothing reads the target
  state for MANUAL (D4, D5).
- `mManualRadarRangeM`'s header comment ("reclamped every frame") and the comment above the range block (which cites
  Amendment 3) are updated to cite SDD 36.
- Resulting behaviour: MANUAL range and mode are independent of the target. Selecting or clearing a target while in
  MANUAL keeps both (D4, D5); Home is the only way back to AUTO. In AUTO the range still jumps 10,000 km <-> 1 km as a
  target is selected or cleared (D3). Both are HUD members, reset only by a new HUD (session only, never saved).

### 3.3 What does not change

- AUTO values and their target dependence (D3); Home returns to AUTO; the 1 AU ceiling; the no-op rule; the label
  format; contact projection, budget, styling and drawing.
- No setting, no persistence, no save or key-layout schema change, no key binding change (D1).

## 4. Contract for the test author (pure functions)

All in namespace `SOLRadar`, header `UI/SOLRadarLayout.h`.

**Constants.** `MANUAL_FLOOR_M` = 1000.0; `AUTO_RANGE_M` = 1.0e7; `AUTO_RANGE_TARGETED_M` = 1.0e3;
`MaxRangeM` = 149597870700.0 (unchanged); `ZOOM_SNAP_RELATIVE_TOLERANCE` = 1e-9. `ZoomStepFactor` no longer exists.

**`double AutoRangeM(bool bHasSelectedTarget)`**

- `false` -> `AUTO_RANGE_M` (1e7) exactly; `true` -> `AUTO_RANGE_TARGETED_M` (1e3) exactly.
- Invariant: `MANUAL_FLOOR_M <= AutoRangeM(false)` and `MANUAL_FLOOR_M <= AutoRangeM(true)`.

**`double StepManualRange(double currentRangeM, bool bZoomIn)`**

Let `x = currentRangeM` and `P = { 10^k : k integer }` (powers of ten in metres).

1. **Invalid input.** If `x` is not `> 0` (0, -0, negative, NaN): return `MANUAL_FLOOR_M` exactly, both directions.
2. **+infinity:** return `MaxRangeM` exactly, both directions.
3. **Exact-power test.** `x` counts as the power `p` in `P` when `|x - p| <= p * ZOOM_SNAP_RELATIVE_TOLERANCE`
   (at most one `p` can match).
4. **Zoom in:** if `x` counts as `p`, the target is `p / 10`; otherwise it is the largest `p` in `P` with `p < x`.
   **Zoom out:** if `x` counts as `p`, the target is `p * 10`; otherwise it is the smallest `p` in `P` with `p > x`.
5. **Clamp** the target to `[MANUAL_FLOOR_M, MaxRangeM]` and return it.

Guarantees: the result is always exactly one of `MANUAL_FLOOR_M`, a power of ten in `(1e3, 1e11]`, or `MaxRangeM`, so
tests may compare with zero tolerance. Never NaN, never outside `[MANUAL_FLOOR_M, MaxRangeM]`. The HUD's no-op test
is `stepped == current`, so `StepManualRange(MANUAL_FLOOR_M, true)` must return exactly `MANUAL_FLOOR_M` and
`StepManualRange(MaxRangeM, false)` exactly `MaxRangeM`. The function does not depend on the target state.

Cases (every expected value exact):

| Input (m) | In | Out | Why |
|---|---|---|---|
| 1e7 (AUTO, no target) | 1e6 | 1e8 | exact power, one decade (today `-` was a no-op here) |
| 1e6, 1e5, 1e4 | 1e5, 1e4, 1e3 | 1e7, 1e6, 1e5 | exact powers |
| 1e3 (floor, AUTO with a target) | 1e3 | 1e4 | in clamps at the floor (no-op) |
| 5e3 | 1e3 | 1e4 | between powers |
| 1.5e3 | 1e3 | 1e4 | |
| 2.5e4 | 1e4 | 1e5 | |
| 3e9 | 1e9 | 1e10 | |
| 9.99e6 | 1e6 | 1e7 | just under a power, not within tolerance |
| 1e6 * (1 + 1e-12) | 1e5 | 1e7 | within tolerance: counts as 1e6 |
| 1e6 * (1 - 1e-12) | 1e5 | 1e7 | within tolerance: counts as 1e6 |
| 1e6 * (1 + 1e-6) | 1e6 | 1e7 | above 1e6, outside tolerance |
| 1e6 * (1 - 1e-6) | 1e5 | 1e6 | below 1e6, outside tolerance |
| 1e6 computed as 1e7 / 10 or 1e3 * 10 * 10 * 10 | 1e5 | 1e7 | arithmetic results of exact powers |
| `MaxRangeM` (1 AU) | 1e11 | `MaxRangeM` | 1 AU is not a power; out clamps (no-op) |
| 1e11 | 1e10 | `MaxRangeM` | 1e12 clamped |
| 1e10 | 1e9 | 1e11 | |
| 1e8 | 1e7 | 1e9 | |
| 30 AU (4.49e12) | `MaxRangeM` | `MaxRangeM` | 1e12 / 1e13 clamped |
| 1.0 or 0.5 (below the floor) | 1e3 | 1e3 | clamped up |
| +infinity | `MaxRangeM` | `MaxRangeM` | |
| 0, -0, -1, -5e8, -`MaxRangeM`, NaN | 1e3 | 1e3 | invalid input |

Ladders:

- **No target, from AUTO (D2):** from `AutoRangeM(false)`, zoom in five times: 1e6, 1e5, 1e4, 1e3, 1e3 (the fifth is a
  no-op). One zoom out from there: 1e4.
- **Target, from AUTO:** from `AutoRangeM(true)` (1e3), zoom in: 1e3 (no-op, so AUTO is kept by the HUD); zoom out:
  1e4.
- **From the ceiling:** from `MaxRangeM`, zoom in nine times: 1e11, 1e10, 1e9, 1e8, 1e7, 1e6, 1e5, 1e4, 1e3; the tenth
  is a no-op. Every stop is a power of ten, so 10 km (1e4) is on it.
- **Up to the ceiling:** from 1e3, zoom out nine times: 1e4, ..., 1e11, then `MaxRangeM`; the tenth is a no-op.
- **Off-decade start:** from 3e9, in: 1e9, 1e8, ...; from 3e9, out: 1e10, 1e11, `MaxRangeM`.

**Tests to change in `Tests/RadarLayoutTest.cpp`** (re-validate every existing radar test, per the project rules):

- `EffectiveFloorByTargetState` -> `AutoRangeByTargetState` (values above plus the invariant).
- `StepManualRangeInOut`, `...Clamps`, `...Chained`, `...DegenerateInput`: drop the target argument and rewrite to
  the table and ladders above. Delete the cases that pinned the old target-dependent floor ("in from 5e7 ->
  DefaultFloorM", "From 1e7, no target -> stays 1e7", "same range zooms in further with a target", "clearing the
  target re-clamps up to DefaultFloorM") and the "non-power-of-10 ranges step by the same factor (no snapping)" cases
  (3e9 -> 3e8, 2.5e3 -> 2.5e4, `MaxRangeM` -> `MaxRangeM / 10`): that behaviour no longer exists. Add NaN, +infinity
  and the tolerance cases.
- New `StepManualRangeSnapsToPowersOfTen` (the table) and `ManualFloorLadders` (the ladders) tests.
- `Tests/GlossaryTest.cpp`: the Radar scope entry's formatted description names the AUTO range (10,000 km), the
  targeted AUTO range (1 km) and the manual floor (1 km), from the constants (section 5).

HUD-side behaviour (AUTO/MANUAL mode, target changes) is UI state, not unit-tested; it is covered by the HUD smoke
(section 6).

## 5. Player-facing text and docs

- **In-game glossary / Info text** (`Menu/SOLMenuText.h`, the "Targets and radar" `RadarScope` entry, and
  `Menu/SOLGlossary.cpp` tokens). Today: "In AUTO the range is {RadarAutoFloor}, tightening to {RadarAutoFloorTarget}
  when you select a target. {RadarZoomIn} and {RadarZoomOut} zoom by ten (MANUAL) and {RadarAuto} returns to AUTO."
  New: "In AUTO the range is {RadarAutoRange}, tightening to {RadarAutoRangeTarget} when you select a target.
  {RadarZoomIn} and {RadarZoomOut} step the range through powers of ten (MANUAL), in to {RadarManualFloor} with or without
  a target; MANUAL keeps its range when you change targets, and {RadarAuto} returns to AUTO." New token
  `RadarManualFloor` formats `MANUAL_FLOOR_M` with `GlossaryKilometres` ("1 km"); `RadarAutoFloor` /
  `RadarAutoFloorTarget` are renamed `RadarAutoRange` / `RadarAutoRangeTarget` and read the renamed constants (D7).
  The Controls row ("Radar range in / out (manual)") stays true.
- **`Docs/PLAYER_GUIDE.md`** is generated from the glossary and `SOLTest.PlayerGuide.UpToDate` fails while it is stale:
  regenerate it with `Tools\WritePlayerGuide.bat` (it keeps unchanged Help images byte for byte; the Targets and radar
  tab images change). If the longer text adds a scrolled page, update `SOL::Paths::PLAYER_GUIDE_TAB_PAGES`.
- **`Docs/GAME_MECHANICS.md`**: the `- / = / Home` control row (section 1: "steps to the next power of ten" instead of
  "by a factor of 10") and the Radar row's "Range modes" text (section 2): AUTO unchanged; MANUAL steps snap to powers
  of ten between 1 km and 1 AU whatever the target state; selecting or clearing a target keeps MANUAL and its range;
  "The floor is re-evaluated every frame (also in MANUAL)" removed.
- **`Docs/ARCHITECTURE.md`** section 6.3 step 2 (`EffectiveFloorM` named there).
- **SDD 2**: a short "Appendix C — Amendment 5 (issue #36)" note after Amendment 4's clarifications: the manual floor
  is `MANUAL_FLOOR_M` (1 km) whatever the target state, manual steps snap to powers of ten, and Amendment 1's
  "Target-floor tracking" and Amendment 3's "MANUAL clamped to `EffectiveFloorM`" are superseded by SDD 36; plus a
  change-log line.
- **SDD 33** section 8.5 lists the token names `{RadarAutoFloor}` / `{RadarAutoFloorTarget}` as examples: a one-line
  note of the rename.
- **`Docs/Plans/STATUS.md`**: #36 in progress, then done.

## 6. Verification

- **Unit tests:** section 4, `Tools\RunTests.bat -Filter SOLTest.RadarLayout`, then `SOLTest.Glossary`,
  `SOLTest.PlayerGuide`, then the full suite.
- **HUD smoke** (`-SOLSmokeHud`, `UI/SOLHudSmoke.cpp`), run at **1280x720** and **1920x1080**
  (`UnrealEditor.exe SOLTest.uproject -game -windowed -ResX=1280 -ResY=720 -SOLSmokeHud -log`, and the same at
  1920x1080), all checks PASS. New phases before `SelectEarth` (no target selected yet, radar AUTO at 10,000 km),
  each check comparing the range exactly:
  1. `-` three times: MANUAL, 1e4 m (10 km, D2).
  2. T (select the target nearest the reticle; the ship faces Earth from the spawn): a target is selected and the
     radar is still MANUAL at 1e4 m (D4, D5; AUTO would read 1 km, so the check tells the two apart).
  3. X (clear the target): no target and the radar is still MANUAL at 1e4 m (D4, D5; the old code pushed it to
     10,000 km).
  4. `-` twice: MANUAL 1e3 m after the first, the second a no-op (still MANUAL 1e3 m); a UI screenshot of
     `RADAR 1.0 km (MANUAL)` with no target.
  5. Home: AUTO, 1e7 m. `SelectEarth` and its screenshot then run as today.

  The existing `h-radar-zoom` check uses the new names: from AUTO with Earth selected (1 km) every `-` is a no-op and
  AUTO is kept; its MANUAL branch accepts a power of ten in `[MANUAL_FLOOR_M, previous range)`. `h-radar-auto`
  compares with `AutoRangeM`. `HUDSMOKE_RADAR_OUT_STEPS` (9: 1 km -> 1 AU) is still right with snapping (eight
  decades to 1e11, the ninth clamps to 1 AU); `HUDSMOKE_RADAR_ZOOM_STEPS` (8) and its comment ("enough to reach the
  1 km floor from any range") becomes nine from 1 AU and is corrected.
- **PIE check** (visual): fly with no target, press `-` from AUTO and read the label down to `RADAR 10.0 km (MANUAL)`
  and `RADAR 1.0 km (MANUAL)`; `=` from 1 km to 1 AU and back with every stop a power of ten; select and clear a target
  while zoomed in and confirm the label keeps its range and MANUAL; Home shows `RADAR 10000.0 km (AUTO)`. Smoke
  screenshots are looked at and then deleted, except the README gallery shots.

## 7. Open questions

None. Q1-Q4 of the first draft are answered as D4-D7.

## 8. Performance

No per-frame cost is added; one is removed. Per frame the HUD still evaluates one branch for the AUTO range, and the
target-dependent clamp of the manual range is gone. `StepManualRange` (a decade search over at most a dozen exact
constants) runs once per key press. No allocation, no new draw, no rendering or material change: the label strings
(`RADAR 1.0 km (MANUAL)`) already exist today with a target, so menu/HUD GPU cost and the VR floor
(`Docs/Research/VR_PERFORMANCE_TARGETS.md`) are unaffected. At 1 km more of the contact list projects onto the rim
(beyond range), the same per-contact work as today with a target selected (the 64-contact budget is unchanged). Known
radar debt (`Docs/TECH_DEBT.md`: `SelectContacts` full sort, overlay `SelectShips` ranking) is unrelated and untouched.

## 9. Change log

- 2026-11-10: drafted from the grill-me decisions D1-D3; open questions Q1-Q4.
- 2026-11-10: Q1-Q4 answered as D4-D7 (MANUAL keeps mode and range across target changes; steps snap to powers of
  ten; names); contract, smoke plan and text updated; no open questions left.
- 2026-11-10: built as designed. As-built notes: `StepManualRange` searches an exact table of powers of ten (1e1 to
  1e15) after two bounds (a range at or below 1e2 returns `MANUAL_FLOOR_M`, at or above 1e14, +infinity included,
  returns `MaxRangeM`; both agree with the section 4 rule), and a `static_assert` holds `MANUAL_FLOOR_M <= AUTO
  range`. HUD smoke: the new phases log as `h0-radar-10km`, `h0-radar-select`, `h0-radar-clear`, `h0-radar-1km`,
  `h0-radar-floor` (with the `HudSmoke_RadarManualFloor` UI screenshot) and `h0-radar-auto`; Setup clears any
  selected target so they start with none; `HUDSMOKE_RADAR_ZOOM_STEPS` is 9. 30 / 30 checks at 1280x720 and
  1920x1080. No schema, setting or binding change.
- 2026-11-10: review fixes. Kept behaviour with the AU stops documented (units note, `GAME_MECHANICS.md`); glossary
  text reads "step the range through powers of ten"; `mLastRadarRangeM` starts at `AUTO_RANGE_M`, so a zoom press
  before the first `DrawRadar` steps from 10,000 km as before; `static_assert(ZOOM_SNAP_RELATIVE_TOLERANCE < 0.5)`.
  Verification as run: the separate PIE check (section 6) was replaced by the HUD smoke (its
  `HudSmoke_RadarManualFloor` screenshot was looked at) plus the unit tests. The smoke drives `-` through real keys
  but not `=`; the `=` ladder up to 1 AU is covered by the unit tests (`ManualFloorLadders`) and the smoke's direct
  `StepRadarZoom(false)` calls before the radar shot.
