<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 34: Overlay profiles

SDD: [`Docs/SDDs/34-overlay-profiles.md`](../SDDs/34-overlay-profiles.md). This is a living document: tick each step
as it lands. Each part is built by its own subagent with its context kept under ~35%, and each part is small enough to
verify without the user. Commit convention: `Issue #34: Part 34x - <short description>` on `master`, then push.

**Gate: every question is answered (SDD section 2, D9-D19; section 14.1).** Starting values at HEAD `870e4a7`: `SAVE_VERSION` 9, `KEY_BINDING_LAYOUT_VERSION` 7. Target values:
`SAVE_VERSION` 10, `KEY_BINDING_LAYOUT_VERSION` 10.

## Parts

### 34a: profile model (pure, test-first; tests opus/high, implementation sonnet/medium)

Unblocked (Q2, Q4, Q10 answered: per-profile master and orbit-lines bits, `UsedProfiles`, re-enable restores).

1. [x] Brief a separate opus subagent with SDD sections 4.1-4.4 only (no implementation). It rewrites and extends
   `Source/SOLTest/Tests/OverlayModelTest.cpp`:
   - re-validate every existing test (15 rows, no `SpaceDust`, no `bIgnoresMaster`; delete the dust and master-exempt
     tests);
   - add every contract row of section 4.3, including the orbit-lines functions, `IsProfileUsed`, the D12
     re-enable restore and `ProfileTagAlpha`.
2. [x] Stubs in `UI/SOLOverlayModel.{h,cpp}` plus the `SOL::Overlay` constants (`PROFILE_TAG_SHOW_S`,
   `PROFILE_TAG_FADE_S`, `ALL_PROFILES_MASK` with its `static_assert`). Build, then run
   `Tools\RunTests.bat -Filter SOLTest.OverlayModel`. The tests must be red for the right reasons. (If the headless
   runner stalls, a `TECH_DEBT.md` item, run the tests in the open editor.)
3. [x] Implement:
   - remove the `SpaceDust` row and `bIgnoresMaster`;
   - replace `ProfileCount` with `EnabledProfiles` and `MasterOnProfiles`;
   - add the new functions;
   - update every compile-time caller to the new `IsShown` / `IsOrbitShown` signatures (settings subsystem, HUD, dust
     component stub reads) so the module builds.

   Green.

### 34b: persistence, schema 10 (opus, xhigh; tests by a separate opus/high subagent)

Unblocked (Q1, Q2, Q7, Q10 answered). The brief must carry:
- D6 (the approved 9 -> 10, layout 7 -> 10, the per-profile `bEnabled` / `bMasterOn` / `bOrbitLinesOn`, the global
  `bSpaceDust`; nothing else) with D9, D10, D12, D15, D18;
- "version and migrate, never silently discard";
- that the version is bumped only as approved;
- the exact field list of SDD section 5.1.

The subagent reports exactly which constants and fields it changed. The main session checks the report against SDD
section 5 before ticking.

1. [x] Tests first, from SDD sections 5 and 11:
   - extend `SOLSettingsRulesTest.cpp` (the 9 -> 10 cases, the whole chain from v6 and v8, idempotence);
   - extend `SOLSettingsModelTest.cpp` (`bSpaceDust`, prefs equality);
   - update the pins in `FlightModeMathTest.cpp` (schema 10, the layout per Q1, the `DefaultLayoutChangedSince` cases).

   Red.
2. [x] Fields:
   - `FSOLOverlayProfileSave::bEnabled`, `bMasterOn`, `bOrbitLinesOn`;
   - `USOLSettingsSave::bSpaceDust` (`bShowOrbitLines` kept as a legacy migration source);
   - `FSettingsValues::bSpaceDust` and `SOL::Settings::SPACE_DUST_DEFAULT`; `FSettingsValues::bShowOrbitLines` and
     `SetAppliedShowOrbitLines` removed (the switch lives in the prefs).

   `SetValues` writes 5 entries (unused slots with an empty row map), and `GetValues` reads them per SDD 5.2.
3. [x] The 9 -> 10 migration step (SDD 5.3, literal ids, the orbit-lines carry-over), the `SAVE_VERSION` bump and its
   schema comment, and `KEY_BINDING_LAYOUT_VERSION` 10.
4. [x] `USOLSettingsSubsystem` (SDD 6.3):
   - the master and the orbit-lines switch in the prefs (`mIsOverlayMasterOn` and `mWrittenShowOrbitLines` removed);
   - `CycleOverlayProfile`, `SetActiveOverlayProfile`, `SetOverlayProfileEnabled`, `IsSpaceDustOn`;
   - the owed-write test with `AreEqual`.

   The pawn's flight-start master call is removed (D10). Pawn `EndPlay` calls `FlushSave()`.
5. [x] Suite green.

### 34c: keys and Enhanced Input (opus, high; KeyBindings tests by a separate opus/high subagent)

Blocked by: Q1, Q6, Q7.

1. [x] Tests first: extend `SOLKeyBindingsTest.cpp` per SDD section 11 (`RightShift` chord, the
   `CycleOverlayProfile` binding, the Shift / RightShift overlap, the stale override). Red.
2. [x] `SOLKeyBindings`:
   - `EKeyChord::RightShift` and its "RShift+" prefix;
   - `ToggleDarkening` replaced in place by `CycleOverlayProfile`;
   - `CanBindKeyTo` and `FindConflict` rules;
   - `SOLMenuText::KEY_BINDING_TEXTS`.
3. [x] `ASOLShipPawn`:
   - read the engine's chord blocking (`EnhancedPlayerInput.cpp`, `InjectChordBlockers`) first;
   - add `IA_RightShiftChord` (Right Shift only) and the `RightShift` case in `MapBindingPressed`;
   - add the explicit `IA_ShiftChord` blocker on every plain mapping that shares its key with a `RightShift` binding;
   - add `IA_CycleOverlayProfile` and `HandleCycleOverlayProfile` (it cycles, then notifies the HUD);
   - remove the darkening action, its handler and its switch case.

   If the blocker behaviour cannot be confirmed from the source, stop and report back.
4. [x] Suite green. A quick `-SOLSmokeInput` run confirms that Left Shift still thrusts.

### 34d: Overlay Menu tabs, Settings, HUD tag (opus, high)

Q3, Q5, Q6, Q8, Q9, Q11 answered.

0. [x] The "Orbit Lines" row on each profile's page with the orbit-kind rows indented under it, and the Settings
   "Show orbit lines" checkbox removed (D19, SDD 7.2).
1. [x] `USOLOverlayMenuWidget` (SDD section 7):
   - the tab column (5 tab widgets built once), `mViewedProfile`, the enable / "Profile enabled" rows;
   - tab hit tests, PageUp / PageDown and the cycle key in `NativeOnKeyDown`;
   - the title and hints from `SOLMenuText`;
   - the embedded variant with tabs.
2. [x] Settings page: the `ESOLSettingsToggle::SpaceDust` checkbox in Graphics (label in `SOLMenuText`; Apply /
   Revert as the other toggles). Check the layout at 1000x800 (not yet looked at: do it in step 4).
3. [x] HUD tag (SDD section 8): `ASOLFlightHud::NotifyOverlayProfileShown`, the reserved text buffer, and the drawing
   with `ProfileTagAlpha` under the controls-notice line, behind the usual HUD gates.
4. [x] Suite green. A PIE look through unreal-mcp when the editor is open (the menu, the tabs, the tag, the Settings
   row). If the editor is not open, note it here and rely on 34f's screenshots.

### 34e: space dust consumer (sonnet, medium)

Blocked by: none beyond 34b.

1. [x] `USOLSpaceDustComponent` (SDD section 9):
   - read `Settings->IsSpaceDustOn()`, with `SOL::Settings::SPACE_DUST_DEFAULT` as the fallback;
   - rename `FSOLSpaceDustGate::bRowEnabled` to `bSettingEnabled` (and in `SpaceDustMathTest.cpp`);
   - update the cvar help text.
2. [x] `-SOLSmokeDust` step 9 becomes the Settings option (apply off / on, `EndVisit` with exactly one write).
   `DustSmoke_Settings.png` replaces `DustSmoke_Menu.png`. Step 10 (master O) stays.
3. [x] Run `-SOLSmokeDust` at 1280x720 and 1000x800 and look at the screenshots.

### 34f: overlay smoke (sonnet, medium)

1. [x] Extend `UI/SOLOverlaySmoke.cpp` with SDD section 12's steps (real injected key events for Right Shift+O and
   Left Shift+O; smoke settings slot). Drop the old Shift+O darkening pair.
2. [x] Run `-SOLSmokeOverlay` at 1280x720 and 1000x800. Look at `OverlaySmoke_Tabs.png` and `OverlaySmoke_Tag.png`.
3. [x] Regression smokes: Input, Hud, Dust, Interact, Map. Then the full suite.

### 34g: docs, gallery, review, commit

1. [x] Docs (sonnet, low-medium), per SDD section 10:
   - `GAME_MECHANICS.md`;
   - `SOLMenuText.h` (Controls rows, Info prose, hints, tag, Settings label), so the in-game text stays truthful;
   - `ARCHITECTURE.md`;
   - SDD 22 Amendment 3 and SDD 43 Amendment 3;
   - the `TECH_DEBT.md` smoke-flake wording.
2. [x] README gallery: 2-3 new shots `Docs/Images/34-overlay-profile-tabs.png`, `34-overlay-profile-tag.png` and
   optionally `34-settings-space-dust.png`, plus the feature and smoke lists.
3. [x] Adversarial performance and style review (a fresh opus subagent at xhigh, report only). Brief it with:
   - the changed files, this SDD, `STYLE_GUIDE.md` and `UE_PERFORMANCE_GUIDELINES.md`;
   - the known `TECH_DEBT.md` items (the overlay smoke warm-up flake, the headless runner, the `SelectShips` ranking).

   Fix the findings and re-check them against the SDD. Re-run the full suite, `-SOLSmokeOverlay` and `-SOLSmokeDust`.
4. [x] `graphify update .` and `Tools\UpdateMetrics.bat`. The coordinator updates `Docs/Plans/STATUS.md`. Commit
   `Issue #34: Part 34x - ...` and push.

## Progress notes

- 2026-11-03: SDD and plan drafted from the grill-me decisions D1-D8. Ten questions are open (SDD section 14),
  including Q1 (the layout version value conflicts with the constant's meaning) and Q2 (whether the per-profile master
  is persisted, which needs one more approved field). No code yet.
- 2026-11-03: the user answered Q1-Q10 (SDD D9-D18). Changes against the earlier draft: layout version 10; master
  saved per profile; re-enabling restores a profile's last values (new in-memory `UsedProfiles`, saved as a non-empty
  row map, no new field); Alt+O orbit lines per profile (`bOrbitLinesOn`, migrated from the global
  `bShowOrbitLines`). The Q10 change opens Q11 (SDD 14.2: the Settings "Show orbit lines" checkbox and an orbit-lines
  menu row).
- 2026-11-03: Q11 answered (a), D19: an "Orbit Lines" menu row per profile, orbit rows indented under it, the
  Settings checkbox removed; the `UsedProfiles` encoding and the legacy `bShowOrbitLines` field confirmed. Building
  starts with 34a.
- 2026-11-03: unit-tested layer built (34a 2-3, 34b 2-5, 34c 2, 34d 2, 34e 1). Red with stubs: 40 of 1233 failing
  (OverlayModel, SettingsRules, SettingsModel, KeyBindings, FlightModeMath layout pins). Green: 1233 / 1233. Persisted
  changes: `SAVE_VERSION` 9 -> 10, `KEY_BINDING_LAYOUT_VERSION` 7 -> 10, `FSOLOverlayProfileSave` + `bEnabled` /
  `bMasterOn` / `bOrbitLinesOn`, `USOLSettingsSave` + `bSpaceDust` (`SPACE_DUST_DEFAULT`); `bShowOrbitLines` kept as
  the legacy migration source. Glue left for 34c step 3: `MapBindingPressed` has no `RightShift` case yet, so the
  cycle action is mapped as a plain O (it fires with the master O; harmless while only Profile 1 can be enabled);
  `IA_RightShiftChord`, the explicit Shift blocker and the HUD tag call are not in yet. `-SOLSmokeOverlay`'s Shift+O
  darkening pair now fails (rewritten in 34f); `-SOLSmokeDust` step 9 only got a compile fix (it toggles and applies
  the Settings option instead of clicking the removed row; rewritten in 34e step 2). Next: 34c step 3.
- 2026-11-03: engine glue built (34c 3-4, 34d 0, 1, 3). Engine source (`EnhancedInputSubsystemInterface.cpp`
  `ReorderMappings` / `InjectChordBlockers`, `InputAction.cpp` trigger evaluation): blockers OR together, so the
  explicit and the injected blocker coexist; but a mapping with any `UInputTriggerChordAction` (a blocker too) sorts
  among the chorded mappings in mapping order, and blockers are injected only into same-key mappings *after* a
  chorded one. `SOLInput::BlockPlainMappingsUnderChord` therefore re-adds each blocked plain mapping at the end of the
  ship context (after the Alt / Ctrl / RShift chords of its key). The HUD tag goes through the HUD bus
  (`ESOLHudCommand::ShowOverlayProfileTag` -> `ASOLFlightHud::NotifyOverlayProfileShown`), so the pawn and the menu do
  not include the HUD. Menu display rows: 0-8 icons, 9 "Orbit Lines", 10-13 the orbit rows, 14 Ships on Radar,
  15 Darkening, 16 "Profile enabled" (`USOLOverlayMenuWidget::GetOverlayRow`). Suite 1233 / 1233. `-SOLSmokeInput`
  41 / 41 (Left Shift thrusts). `-SOLSmokeOverlay` 8 / 11: e-shift-o and e-shift-o-back fail as intended (Left
  Shift+O now leaves darkening and the master alone, D1), f-menu-open expects 15 rows (now 16). A one-off local run
  with Right Shift instead (reverted) showed RShift+O cycles 1 -> 1 and, with profile 2 enabled, 1 -> 2, the tag text
  right and the master untouched. `-SOLSmokeDust` 23 / 24: 9-menu-open expects 15 rows. Next: 34e step 2, 34f.
- 2026-11-03: smokes done (34e 2-3, 34f 1-3). `-SOLSmokeOverlay` 30 / 30 at 1280x720 and 1000x800 (SDD 12 steps 1-14; real
  Right / Left Shift+O, Ctrl+O, Alt+O, O, PageUp and Esc, Slate clicks on tabs and rows; the pawn's quit wrote the owed
  change once, write 3 in the log). The HUD planet icon count is 0 at the start position (planets are edge arrows), so
  steps 9 / 10 check the model's Planets row verdict next to the icon count. `-SOLSmokeDust` 24 / 24 at both sizes (step 9
  = pending Space dust off / on + Apply, one write at EndVisit; `DustSmoke_Settings.png` shows the Graphics option; the
  Settings tab is selected a few frames after the pause menu opens, as the open is a queued event). Regression, all
  green: Hud 24 / 24 (1280 and 1000), Input 41, Route 26, RouteOverlay 25, Map 16, MapPick 22, MapAlign 26, Hologram 12,
  Interact 18, Chase 26; Jump 14 / 14, MenuSmoke 47 / 50 (unchanged). Suite 1233 / 1233. Next: 34g.
- 2026-11-04: 34g step 3, review findings fixed. (1) The pawn's `EndPlay` write point is now
  `ASOLShipPawn::FlushSettingsAtLeave`, which starts no write on `EEndPlayReason::Quit` / `EndPlayInEditor` (engine:
  `GameEngine.cpp` and `PlayLevel.cpp` run `World->EndPlay` and `GameInstance->Shutdown` back to back, and
  `Deinitialize` writes synchronously); SDD 5.4 and step 12.14 updated. `-SOLSmokeOverlay` step 14 now counts the
  writes: leave with a change owed (1 started, 1 finished), leave with nothing changed (0), leave on quit with a change
  owed (0; the log then shows one "shutdown write ... (0 async in flight)"). (2) `SOLInput::BlockPlainMappingsUnderChord`
  rebuilds the context in two passes (kept mappings in order, then every blocked plain mapping last), since `UnmapKey`
  uses `RemoveAtSwap`; new `Tests/InputHelpersTest.cpp` (3 tests, `SOLTest.InputHelpers.*`). (3) The Overlay Menu tabs
  are rebuilt only when the enabled / active / viewed state changes. (4) `SOLOverlay::HasProfileBit` / `SetProfileBit`
  shared by the model and the save. (5) `ESOLOverlayRowKind`; `SOLSettingsRules.h` forward-declares `FOverlayPrefs`;
  comment and text lines rewrapped to 120 columns. (6) Every overlay read and write in `USOLSettingsSubsystem` uses the
  applied prefs (`mModel.Saved`). (7) The HUD profile tag's dark backing fades with the text (`AddBacking` /
  `AddHoloPanel` take an opacity). No schema change. Suite 1236 / 1236. `-SOLSmokeOverlay` 34 / 34 at 1280x720 and
  1000x800, `-SOLSmokeDust` 24 / 24, `-SOLSmokeInput` 41 / 41. Next: 34g step 4.
- 2026-11-04: **34g done.** Docs synced (GAME_MECHANICS, ARCHITECTURE, SDD 22 / 43 Amendment 3, TECH_DEBT, STATUS,
  README feature / smoke lists, in-game Controls / Info text). Review (opus, xhigh): no blockers; both should-fix items
  (quit / PIE-end double write, two-pass chord-blocker rebuild) and the nits fixed; the rebind-to-a-plain-key side
  effect is recorded in `TECH_DEBT.md` as a user decision. Final: unit suite 1236/1236; smokes Overlay 34/34 (1280x720,
  1000x800, 1920x1080), Dust 24/24, Input 41, Hud 24 (1280x720, 1000x800), Interact 18, Chase 26, Route 26,
  RouteOverlay 25, Map 16, MapPick 22, MapAlign 26, Hologram 12, Jump 14/14, MenuSmoke 47/50 (pre-existing). Gallery:
  `Docs/Images/34-overlay-profile-tabs.png`, `34-overlay-profile-tag.png`, `34-settings-space-dust.png`. unreal-mcp
  was not connected this session, so 34d step 4's PIE look relied on the smoke screenshots (34f). Committed and pushed
  as one commit.
