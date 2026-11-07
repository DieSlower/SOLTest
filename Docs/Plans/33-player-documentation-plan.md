<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 33: Player documentation

SDD: [`Docs/SDDs/33-player-documentation.md`](../SDDs/33-player-documentation.md). This is a living document: tick each
step as it lands. Each part is built by its own subagent with its context kept under ~35%, and each part is small
enough to verify without the user. Commit convention: `Issue #33: Part 33x - <short description>` on `master`, then
push. Run `Tools\UpdateMetrics.bat` before each part commit and include `Docs/PROJECT_METRICS.md` and `README.md`.
Never commit `graphify-out/`, `SOLTest.uproject`, `CLAUDE.md` or `.mcp.json`. No AI attribution anywhere; identity
DieSlower / maximster@gmail.com.

**No schema change** (SDD D11): `SAVE_VERSION` stays 11, `KEY_BINDING_LAYOUT_VERSION` stays 10, no new binding (D10).
A part that finds it needs a saved value or a binding stops and reports back.

**Gate: cleared (2026-11-06).** The open questions O1-O13 were answered by the user and are recorded as decisions
D16-D28 in the SDD (section 2); the design is final and 33a is ready to build. The "Needs" lines below name the
decisions each part builds on. A decision the user has not made, found while building, is reported back, not chosen.

Every brief carries: the SDD path and the sections the part needs, `Docs/STYLE_GUIDE.md`, the banner and comment
rules, "asset and file paths only in `SOLConstants.h`", the SDD section 3 tech-debt items, the user's answers for the
part, "delete temporary screenshots and scratch files when done", and "report decisions you hit instead of choosing".

## Parts

### 33a: pure logic, tables and tests (tests opus/high; implementation opus/high)

Builds on: D17 (ten area colours), D18 (+N badge), D19 (numpad, Other keys, lock marks), D24, D26, D27. SDD sections 6-11.

1. [x] Brief a separate opus/high subagent with SDD sections 6-10 only (no implementation). It writes
   `Tests/KeyCapsTest.cpp`, `KeyboardLayoutTest.cpp`, `KeyDiagramTest.cpp`, `GlossaryTest.cpp`, `HudSymbolsTest.cpp`
   (contract parts that need no HUD move), `PlayerGuideTest.cpp` and `SOLTest.KeyBindings.EveryBindingHasControlRow`.
2. [x] Stubs; `Tools\RunTests.bat -Build -Filter SOLTest.KeyCaps` (and the others): red for the right reason.
3. [x] Implement `Menu/SOLKeyCaps`, `SOLKeyboardLayout`, `SOLKeyDiagram`, `SOLGlossary`, `SOLPlayerGuide`,
   `UI/SOLHudSymbols` (the enum, the prims buffer, `AddToBatch`, the symbols that already have builders: icons, edge
   arrows, cards, radar helpers), the `SOL::Help` constants, `SOLMenuText` tables (`KEY_SHORT_LABELS`,
   `FIXED_KEY_SHORT_LABELS`, `GLOSSARY_TABS`, `GLOSSARY_ENTRIES` with descriptions from `GAME_MECHANICS.md`), and move
   `ESOLHudLine` to `UI/SOLHudLines.h` (values unchanged). Green; full suite.
4. [x] Profile baseline before any restyle: add the `main-controls`, `pause-controls` **and the Settings Key bindings
   views (`main-settings-keys`, `pause-settings-keys`, D20)** to the default run of `-SOLSmokeSolidMenuProfile`; three
   runs at HEAD; record the medians in the SDD (section 14 amendment). The layout test spec covers the ten distinct
   area colours (D17), the numpad keys, the lock flag and the `+N` count (D18, D19).
   **Done (SDD 14.1):** default run 4 -> 12 passes, 9 -> 13 checks; three runs 13/13, medians now the
   `PROFILE_BASE_*_KEYS_*` / `*_CONTROLS_*` constants: main-settings-keys 0.600 / 0.108 (1920x1080) and 0.552 / 0.062
   (1280x720), pause-settings-keys 0.568 / 0.078 (1280x720) and 0.635 / 0.139 (1920x1080), main-controls 0.475 / 0.112
   and 0.471 / 0.066, pause-controls 0.482 / 0.081 (1280x720) and 0.508 / 0.137 (1920x1080) (`cpu_slate` / `gpu_ui`, ms).
5. [x] Adversarial review (fresh opus/xhigh, report only); fixes; full suite. **Done:** 20 findings; all fixed but
   the radar symbols (no builder outside `DrawRadar`, moved to 33d-i) and finding 2 (chord modifier keys drawn as free
   keys: user decision, SDD O14); one test-helper bug fixed (`KeyDiagramTestFixedChord`); suite 1613, 0 failures;
   smokes after the fixes: SolidMenu 193/193, HoloMenu 86/86, Overlay 34/34, Hud 24/24; `GameUserSettings.ini` unchanged.
6. [x] `graphify update .`; commit `Issue #33: Part 33a - pure logic, tables and tests`; push.

### 33b: key cap widget, the Controls restyle and the Settings key rows (opus/high)

Builds on: D20. Depends on 33a. SDD section 6. **Scope grew (D20): the Settings > Key bindings rows get caps too.**

1. [ ] `Menu/SOLKeyCapRow` (`SSOLKeyCapRow` leaf, `USOLKeyCapRow` wrapper): the hull Small brush per cap, legends in
   the menu font, the mouse glyph, alternatives and chords, wrapping from `SOLKeyCaps::LayoutRow`, text measured once
   per content change.
2. [ ] `FillControlsTabs` / `RefreshControlsPage` use it (the 230-unit column kept); the `GetKeyCells` verification
   getter adapted. The mouse-move cap's drawn legend reads Move per SDD section 6 while `KeyDisplayName` stays Mouse.
3. [ ] **Settings > Key bindings rows (D20):** the key cell of each row becomes a `USOLKeyCapRow` (a bounded cell,
   `SETTINGS_KEYCAP_CELL_WIDTH`); **the Rebind button stays** and the rebind waiting text replaces the caps in the same
   cell while waiting; the 46i narrow-row layout is kept, so caps and button fit at **1000x800** (and 1280x720); the
   existing Settings row layout tests and the `GetKeyCells`-style verification getters are re-validated and extended.
4. [ ] `-SOLSmokeSolidMenu`: the rows sweep checks each Controls cap row and each Settings key-binding cap row and its
   button inside its cell at 1000x800 and 1280x720; expected check count updated; screenshots looked at, then deleted
   unless kept for the README.
5. [ ] Profile `main-controls` / `pause-controls` and `main-settings-keys` / `pause-settings-keys` three runs against
   the 33a baselines (under 0.2 ms CPU / 0.3 ms GPU).
6. [ ] Review (fresh opus/xhigh); fixes; full suite; commit
   `Issue #33: Part 33b - key caps on the Controls page and Settings key rows`; push.

### 33c: the keyboard diagram (opus/high)

Builds on: D17, D18, D19, D23. Depends on 33b. SDD section 7.

1. [ ] `Menu/SOLKeyboardDiagram` (`SSOLKeyboardDiagram` leaf, `USOLKeyboardDiagram`): caps (**numpad included**, the
   larger layout, D19), area fills and bars in the **ten area colours** (D17), short labels, the **"+N" badges** (D18),
   **lock marks** on fixed keys, the mouse (beside the keyboard, stacked under it when narrow), the area legend, the
   fixed detail strip (D23), the "Other keys" and "Unbound" lists; hover through `SOLKeyboardLayout::KeyAt` with the
   hover ramp (instant at strength 0).
2. [ ] Model built on show and on `OnKeyBindingsChanged` only (bind in `NativeConstruct`, unbind in
   `NativeDestruct`); no per-frame work beyond the hover ramp.
3. [ ] A smoke step on a test page put on the viewport (the 46f Info-page pattern, since Help comes in 33d): the
   assignment count, a real-cursor hover, a rebind moving a tint, **fit of the larger numpad diagram at 1280x720 and
   1000x800 (the SDD 7 estimates are unproven: tune `DIAGRAM_MIN_UNIT` / `DIAGRAM_MOUSE_BELOW_UNIT`)**.
4. [ ] Profile the diagram page (a temporary pass or the 33d pass early); apply the invalidation-panel fallback only if
   over budget.
5. [ ] Review; fixes; full suite; commit `Issue #33: Part 33c - keyboard and mouse diagram`; push.

### 33d-i: HUD symbol helper, the HUD drawing through it (opus/high)

Needs: none beyond 33a. SDD section 8.1, risk R1.

1. [ ] Before screenshots with `-SOLSmokeHud` (both renderers) at 1280x720 and 1000x800.
2. [ ] Move the inline geometry (reticle, joystick, steer icons, prograde / retrograde, target bracket and edge arrow,
   radar contact marks, G meter marks, route and waypoint marks if inline) into `SOLHudSymbols`; the HUD replays the
   prims through `GlowLine` / `GlowRing` / `GlowBox` / `GlowDashedLine` / `GlowEllipse` / `GlowDot`. No visible change.
3. [ ] `HudSymbolsTest` geometry cases (added by the 33a test author, extended by a separate opus/high test agent for
   the moved symbols); `HudShapesTest`, `OverlayIconsTest`, `RadarLayoutTest` re-validated. Add the test that every
   symbol a `GLOSSARY_ENTRIES` row names builds at least one primitive (33a builds only the icons, the edge arrow and
   the card; the radar symbols wait for this part, their geometry being inline in `ASOLFlightHud::DrawRadar`).
4. [ ] After screenshots compared with the before set; `-SOLSmokeHud`, `-SOLSmokeOverlay`, `-SOLSmokeHoloMenu` green;
   `-SOLSmokeHoloMenuProfile` P0 within its budget.
5. [ ] Review; fixes; full suite; commit `Issue #33: Part 33d-i - shared HUD symbol builder`; push.

### 33d-ii: the Help page and its glossary (opus/high)

Builds on: D16, D22, D26. Depends on 33c and 33d-i. SDD sections 5 and 8.2-8.5.

1. [ ] `SSOLHudShapeLayer` retain mode; `Menu/SOLHudSymbolView`; the shared `M_SOLHudShape` MID in
   `USOLSolidMenuSubsystem` (strength pushed on change). The view holds its `FSOLHudSymbolPrims` and an
   `FSOLHoloFrameOutput` as members and passes them as the `Bounds` / `AddToBatch` scratches.
2. [ ] `ESOLReadOnlyPage::Help` in `SOLPageTabs` (short tab labels, full names as headings, `LeftColumnWidth`, the
   tab column stays 220, D22); the group captions KEYS and HUD GLOSSARY in `FSOLSolidTabRail` (D22);
   `SOLMenuContent::BuildHelpPage` / `FillHelpTabs` / `RefreshHelpPage`.
3. [ ] The Help entry and pause tab (`EEntry::Help`, `ESOLMainMenuPage::Help`, `EMenuTab::Help`, `IsPageTab`) right
   after Controls; `STACK_MAX_VISIBLE_ENTRIES` stays 7 and the stacks scroll (D16); `SOLMenuStateTest` and
   `SolidPageTabsTest` re-validated and extended (separate test agent for the new contract).
4. [ ] `-SOLSmokeSolidMenu` Help steps (SDD section 16) in both menus and both sizes, including every stack entry
   reachable by scroll and click with 8 entries; `-SOLMenuSmoke=all` no new failure; strength 0 / 2 shots looked at.
5. [ ] The 33d review re-checks the starting values and the glossary list (D26). The Distance darkening row needs a
   second brightness of IconPlanet: the table has no field for it yet.
6. [ ] Review; fixes; full suite; commit `Issue #33: Part 33d-ii - Help page with keyboard and HUD glossary`; push.

### 33e: the player guide flag and `Docs/PLAYER_GUIDE.md` (opus/high)

Builds on: D24, D27. Depends on 33d-ii. SDD section 9.

0. Carried over from 33a (already in code): the flag name is `SOL::CommandLine::WRITE_PLAYER_GUIDE` (there is no
   `SOL::Smoke` namespace), the path and image-name constants are in `SOL::Paths`, and the guide's fixed text is the
   `SOLMenuText::GUIDE_*` strings. Check that the `GUIDE_INTRO` wording ("Esc, then Help") and the generated-file
   line's `Tools\WritePlayerGuide.bat` read true once the Help page (33d-ii) and the tool (this part) exist; adjust
   them if not. `SOLTest.PlayerGuide.UpToDate` is written in this part (moved from 33a). The writer saves with
   `EEncodingOptions::ForceUTF8WithoutBOM` (the © banner would otherwise make UTF-16).
1. [ ] `SOL::CommandLine::WRITE_PLAYER_GUIDE` (exists), `SOL::Paths::PLAYER_GUIDE_FILE` / `PLAYER_GUIDE_IMAGE_DIR` (exist);
   `Menu/SOLPlayerGuideWriter` (non-shipping): open Help, one shot per tab plus the hover shot, write the markdown,
   move the images, log and quit (non-zero on failure).
2. [ ] `Tools/WritePlayerGuide.bat` / `.ps1` (refuses while the editor has the project open).
3. [ ] Run it twice: the second run leaves `Docs/PLAYER_GUIDE.md` byte for byte the same; `SOLTest.PlayerGuide.UpToDate`
   (a failing test, D27) green; the player's `GameUserSettings.ini` unchanged.
4. [ ] Review (fresh opus/xhigh); fixes; full suite; commit `Issue #33: Part 33e - generated player guide`; push.

### 33f: smoke and profile, VR floor (opus/high)

Builds on: D21. Depends on 33e.

1. [ ] `-SOLSmokeSolidMenuProfile` passes `main-help-keys`, `main-help-glossary`, `pause-help-keys` (with
   `main-controls` / `pause-controls` and `main-settings-keys` / `pause-settings-keys`) in the default and view runs.
   Also profile the Overlays and route glossary tab (16 rows, the most symbol views).
2. [ ] Default runs x3 at 1920x1080 and 1280x720: every pass under 0.2 ms CPU / 0.3 ms GPU (medians).
3. [ ] View runs: 3840x2160 (4K proxy) and 3840x3744 (one XR-4 eye), three runs of the new passes, menu-share-only
   verdict. **The two-eye run (7680x3744) is skipped (D21)**; note it in the SDD amendment.
4. [ ] Full smoke set: `-SOLSmokeSolidMenu`, `-SOLSmokeHud`, `-SOLSmokeHoloMenu`, `-SOLSmokeOverlay`,
   `-SOLMenuSmoke=all`; PIE smoke via unreal-mcp if the editor is open (else noted here).
5. [ ] Record the numbers in the SDD (section 14 amendment); commit `Issue #33: Part 33f - profile and smoke`; push.

### 33g: final adversarial review (opus/xhigh, report only; fixes opus/high)

1. [ ] Fresh opus/xhigh reviewer: every changed file of #33, the SDD, `Docs/STYLE_GUIDE.md`,
   `Docs/Research/UE_PERFORMANCE_GUIDELINES.md`, `Docs/Research/VR_PERFORMANCE_TARGETS.md`, the SDD section 3 tech
   debt; the CLAUDE.md checklist 1-11.
2. [ ] Fixes; re-check against the SDD; full suite and the smoke set; findings left open go to `Docs/TECH_DEBT.md`
   with the user's agreement.
3. [ ] Commit `Issue #33: Part 33g - final review fixes`; push.

### 33h: docs, in-game text, README screenshots, close (sonnet/medium)

Builds on: D25.

1. [ ] `Docs/GAME_MECHANICS.md` (section 8, the section 1 pointer, the `SOL::Help` tunables) and the in-game text
   audit (Controls, Info with its HUD paragraph shortened to a pointer to Help per D25, Help texts, Settings key rows) so nothing states something false.
2. [ ] `Docs/ARCHITECTURE.md` (new types, the `-SOLWritePlayerGuide` flag, the HUD's use of `SOLHudSymbols`);
   `Docs/TECH_DEBT.md`; check the Mermaid blocks touched, if any, parse.
3. [ ] Regenerate the guide (`Tools\WritePlayerGuide.bat`); README: the "Player guide" link and 2-3 gallery shots
   (`33-help-keyboard.png`, a glossary tab, the Controls caps); temporary shots deleted.
4. [ ] `Docs/Plans/STATUS.md`: #33 to Done; SDD status line Done; this plan ticked.
5. [ ] `graphify update .`; `Tools\UpdateMetrics.bat`; commit `Issue #33: Part 33h - docs, in-game text and README`;
   push; close issue #33.

## Progress notes

- 2026-11-06: SDD and plan drafted from the grill-me answers (Q1-Q11 and the budget items).
- 2026-11-06: open questions O1-O13 answered by the user and recorded as D16-D28 in the SDD; design final, ready to
  build 33a; no part has started. Scope changes from the answers: 33b now also restyles the Settings > Key bindings
  rows (D20); 33c draws the larger numpad diagram with ten area colours, `+N` badges and lock marks; 33d-ii keeps the
  stack cap at 7 so the stacks scroll (D16); 33f drops the two-eye XR-4 run (D21); 33h shortens the Info HUD paragraph
  (D25). Items to watch at the hand check: Quit and About sit below the fold in the scrolling stacks, and the diagram
  fit at 1000x800.
- 2026-11-07: user decisions recorded in the SDD: D29 the Menu area colour is teal-white #9FD6D0 (not #8E9BAA); D30
  the diagram fit is approved (`DIAGRAM_MIN_UNIT` 20, mouse below the keyboard when narrow, `DIAGRAM_MOUSE_BELOW_UNIT`
  24, settled by the 33c smoke); D31 the scrolling stacks are acceptable (Quit / About below the fold, the smoke must
  prove every entry reachable). The colour samples (`Docs/Images/33-samples/`) were deleted. 33a started.
- 2026-11-07: 33a done (SDD section 20): pure logic, tables and tests, profile baselines (SDD 14.1), review fixes.
  Open for the user before 33c: O14 (how the diagram shows chord modifier keys). Next: 33b.
