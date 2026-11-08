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

1. [x] `Menu/SOLKeyCapRow` (`SSOLKeyCapRow` leaf, `USOLKeyCapRow` wrapper): the hull Small brush per cap, legends in
   the menu font, the mouse glyph, alternatives and chords, wrapping from `SOLKeyCaps::LayoutRow`, text measured once
   per content change.
2. [x] `FillControlsTabs` / `RefreshControlsPage` use it (the 230-unit column kept); the `GetKeyCells` verification
   getter adapted. The mouse-move cap's drawn legend reads Move per SDD section 6 while `KeyDisplayName` stays Mouse.
3. [x] **Settings > Key bindings rows (D20):** the key cell of each row becomes a `USOLKeyCapRow` (a bounded cell,
   `SETTINGS_KEYCAP_CELL_WIDTH`); **the Rebind button stays** and the rebind waiting text replaces the caps in the same
   cell while waiting; the 46i narrow-row layout is kept, so caps and button fit at **1000x800** (and 1280x720); the
   existing Settings row layout tests and the `GetKeyCells`-style verification getters are re-validated and extended.
4. [x] `-SOLSmokeSolidMenu`: the rows sweep checks each Controls cap row and each Settings key-binding cap row and its
   button inside its cell at 1000x800 and 1280x720; expected check count updated; screenshots looked at, then deleted
   unless kept for the README.
5. [x] Profile `main-controls` / `pause-controls` and `main-settings-keys` / `pause-settings-keys` three runs against
   the 33a baselines (under 0.2 ms CPU / 0.3 ms GPU).
6. [x] Review (fresh opus/xhigh); fixes; full suite; commit
   `Issue #33: Part 33b - key cap widget and Controls restyle`; push.

   **Done (SDD 20, 14.2):** tests first (separate opus/high author: `KeyCapRowTest.cpp`, `KeyCapsTest.cpp` glyph /
   legend cases, `SolidSettingsTest` `KeyCellWidth`), red on stubs, green; the caps' legends sit right of the Small
   hull's LED (`KEYCAP_LED_ROOM`, screenshot tuning); `-SOLSmokeSolidMenu` 193 -> 197 checks (`u-keycaps`, `v-keycaps`
   in a new `PauseKeys` step, `h-small-keycaps`, `h-pause-small-keycaps`; the Controls fit and rows sweep read the cap
   rows, painted whole); review: 0 High, 2 Medium, 7 Low, all fixed (the key rows now grow from their minimum height so
   a wrapped rebound chord stays in its row; paint verification; the LED rule pure and tested; measure / layout /
   reshape split; reshape after a Slate invalidate-all) except the single over-wide rebound cap at 1000x800 and the
   bindable-key sweep (TECH_DEBT). At 1000x800 the three O chords (Alt+O, Ctrl+O, RShift+O) wrap onto two lines.

### 33c: the keyboard diagram (opus/high)

Builds on: D17, D18, D19, D23, **D32 revised and D33** (2026-11-07, user: every physical key that starts a chord gets
the modifier mark, the six modifier keys are distinct, each side lists the chords it starts as the game reads it,
chords never count in "+N", the hover strip lists the key's own actions then its chords; default bindings unchanged,
moving ThrustDown off Left Ctrl is a separate ticket; `SOLKeyDiagram` and its tests change for it). Depends on 33b.
SDD section 7.

1. [x] `Menu/SOLKeyboardDiagram` (`SSOLKeyboardDiagram` leaf, `USOLKeyboardDiagram`): caps (**numpad included**, the
   larger layout, D19), area fills and bars in the **ten area colours** (D17), short labels, the **"+N" badges** (D18),
   **lock marks** on fixed keys, the mouse (beside the keyboard, stacked under it when narrow), the area legend, the
   fixed detail strip (D23), the "Other keys" and "Unbound" lists; hover through `SOLKeyboardLayout::KeyAt` with the
   hover ramp (instant at strength 0).
2. [x] Model built on show and on `OnKeyBindingsChanged` only (bind in `NativeConstruct`, unbind in
   `NativeDestruct`); no per-frame work beyond the hover ramp.
3. [x] A smoke step on a test page put on the viewport (the 46f Info-page pattern, since Help comes in 33d): the
   assignment count, a real-cursor hover, a rebind moving a tint, **fit of the larger numpad diagram at 1280x720 and
   1000x800 (the SDD 7 estimates are unproven: tune `DIAGRAM_MIN_UNIT` / `DIAGRAM_MOUSE_BELOW_UNIT`)**.
4. [x] Profile the diagram page (a temporary pass or the 33d pass early); apply the invalidation-panel fallback only if
   over budget.
5. [x] Review; fixes; full suite; commit `Issue #33: Part 33c - keyboard diagram`; push.

   **Progress (2026-11-07, paused for a user decision; all uncommitted in the working tree):** tests updated to D32
   revised by a separate opus/high author (red on stubs: 48 tests, 23 failing), then implemented: `SOLKeyDiagram`
   (`ChordModifierKeys`, chord ranges, `bModifier`, `Alternative`, `DetailAssignments`, `DetailLine`),
   `SOLKeyboardLayout::LayoutPanel` / `LegendItemRect`, `Menu/SOLKeyboardDiagram` (leaf + wrapper; hover strip with
   the hovered key's cap at its left), smoke page `Menu/SOLKeyboardDiagramSmokePage`; full suite 1651 / 0;
   `-SOLSmokeSolidMenu` 223 / 223 (was 197: `Diagram` at 1280x720 and `SmallDiagram` at 1000x800, real-cursor
   hovers on O, LMB and the six modifier keys, every key's strip rows, Fire-to-K rebind and restore), ini unchanged.
   **Blocker (R2 came true):** the real shell content width is 658 units at 1280x720 and 405 at 1000x800 (SDD 7
   estimated 770 / 530), so the key unit is 24.8 / 17.6 and the legends are unreadable (16 / 37 legends wider than
   their key, short labels hidden at 1000x800); and the hover strip needs 17 lines (`DIAGRAM_DETAIL_LINES` 17) at
   1000x800, most of it empty at 1280x720. Needs the user's layout decision (see the 33c report). Not yet done: the
   layout fix, the adversarial review, the profile (deferred to the 33d-ii Help pass by the smoke agent), docs
   (SDD 7 / 11 / 20, ARCHITECTURE, TECH_DEBT, STATUS), commit. Next step after the decision: change the layout
   (contract C / `SOLKeyboardLayout`, tests by a separate author first), re-run the smoke and screenshots, then the
   review.

   **Progress (2026-11-07, later; paused at 70% of the weekly limit, all uncommitted):** user decisions D34 (stack the
   navigation cluster, arrows, numpad and mouse under the main block below a width threshold), D35 (legends clipped to
   their key, the "+N" badge hidden when it would overlap), D36 (hover strip sized once per width for the busiest key,
   the cap on its own row above the lines) and D37 (wide screens only, at least 1280x720; targets 1280x720, 1920x1080,
   3840x2160; nothing for 1000x800) - to be written into SDD section 2. Contract: scratchpad `33c_contract.md`
   sections E.1-E.8 (Stacked = Beside rect + (-13.5, 7) for cluster and mouse keys, Extent (15, 13.5); FitToWidth with
   `DIAGRAM_MOUSE_BELOW_UNIT` 30 / `DIAGRAM_STACK_UNIT` 30; `LayoutPanel(..., detailLines)`, `DIAGRAM_DETAIL_LINES` a
   cap of 24, `DIAGRAM_DETAIL_CAP_HEIGHT` 30; `FitKeyText`, `DIAGRAM_BADGE_GAP` 2; `MaxDetailCount`;
   `GetDetailLineBudget`, `GetPaintedHiddenBadges`). Tests by a separate author (red: 107 / 12 on stubs), implemented:
   `SOLTest.Key` 107 / 0, full suite 1659 / 0. **Next step:** the smoke agent updates `SOLSolidMenuSmoke.cpp` (about
   lines 4368 `-fit` placement now Stacked at 658; 4399 note add hidden badges, wide legends 0; 4511-4533 and 4572
   strip rows against `GetPanel().DetailLines` and the panel unchanged across hovers; the cap row is above the lines;
   drop the 1000x800 `SmallDiagram` step per D37), screenshots at 1280x720 / 1920x1080 incl. hovers (check the 1-unit
   keys' 4-5 letter legends at unit 43.9 and the clipped "Wheel" legend), settles the constants, adds the diagram
   profile pass (three-run medians at 1920x1080 / 1280x720); then the opus/xhigh review, fixes, full suite and the
   four smokes (ini hash cbcf9b65...), docs, graphify, metrics, commit `Issue #33: Part 33c - keyboard diagram`.

   **Progress (2026-11-08; paused for a user decision, all uncommitted):** `SOLSolidMenuSmoke` updated for D34-D37
   (the 1000x800 `SmallDiagram` step dropped; `k-diagram-fit` wants Stacked at unit 658 / 15 = 43.87 and the strip
   sized to `GetDetailLineBudget()`; a new `k-diagram-legends` check: 0 wide legends, 4 badges hidden; each hover checks
   the panel unchanged and the cap row; `strip-all` checks the busiest key's rows = `DetailLines` (8, Left Ctrl) and no
   panel change over every hover): `-SOLSmokeSolidMenu` 212 / 212, ini unchanged. `SOLSolidMenuProfileSmoke` gained P12
   / P13 `main-diagram` / `main-diagram-hover` at 1920x1080 and P14 / P15 at 1280x720 (the diagram test page over the
   main Controls page, against the main Settings baseline; 17 checks): one run 16 PASS, 17 / 17 (P12 dcpu +0.189 /
   dgpu +0.102, P13 +0.161 / +0.105, P14 +0.091 / +0.066, P15 +0.120 / +0.067; against the same run's main-controls
   pass +0.142 / +0.033 at 1920x1080 and +0.088 / +0.027 at 1280x720, the test page's own hull panel included).
   **Blocker (the known risk came true):** the layout is in menu units, so 1280x720 and 1920x1080 clip the same
   legends. At `DIAGRAM_KEY_FONT_SIZE` 11 nine 1-unit legends clip to 2-3 letter prefixes (Esc "Es", ScrL "Scr", Pause
   "Pau", Home "Ho", PgUp "PgU", PgDn "PgD", Del "De", Num "Nu", numpad Enter "En") and the narrow Wheel shows no
   legend; at 10 (tried, reverted) Esc, Del, ScrL and Num fit but Home, Pause, PgUp, PgDn, numpad Enter and Wheel still
   clip, and the 1280x720 text gets smaller everywhere. Font and pad tuning cannot fix it; the user picks between no LED
   room on the diagram caps, abbreviated legends, or another option. Next after the decision: apply it (tests first if
   the contract changes), re-run the solid smoke and screenshots, three profile runs, then the review (step 4 onwards).

   **Done (2026-11-08):** D38 applied (LED-free diagram caps, `DIAGRAM_LEGEND_ABBREVIATIONS` for numpad Enter, Wheel
   and Pause, `SOLTest.KeyDiagram.LegendAbbreviations`), 0 legends clipped at both paint scales, screenshots looked at
   (every legend readable), profile medians in SDD 14.3, the opus/xhigh review (no must-fix; 5 should-fix and 7 nits,
   fixed by an opus/high agent except the ones moved to `Docs/TECH_DEBT.md` and the 33f note above), docs, commit.

### 33d-i: HUD symbol helper, the HUD drawing through it (opus/high)

Needs: none beyond 33a. SDD section 8.1, risk R1.

1. [x] Before screenshots with `-SOLSmokeHud` (both renderers) at 1280x720 and 1000x800.
2. [x] Move the inline geometry (reticle, joystick, steer icons, prograde / retrograde, target bracket and edge arrow,
   radar contact marks, G meter marks, route and waypoint marks if inline) into `SOLHudSymbols`; the HUD replays the
   prims through `GlowLine` / `GlowRing` / `GlowBox` / `GlowDashedLine` / `GlowEllipse` / `GlowDot`. No visible change.
3. [x] `HudSymbolsTest` geometry cases (added by the 33a test author, extended by a separate opus/high test agent for
   the moved symbols); `HudShapesTest`, `OverlayIconsTest`, `RadarLayoutTest` re-validated. Add the test that every
   symbol a `GLOSSARY_ENTRIES` row names builds at least one primitive (33a builds only the icons, the edge arrow and
   the card; the radar symbols wait for this part, their geometry being inline in `ASOLFlightHud::DrawRadar`).
4. [x] After screenshots compared with the before set; `-SOLSmokeHud`, `-SOLSmokeOverlay`, `-SOLSmokeHoloMenu` green;
   `-SOLSmokeHoloMenuProfile` P0 within its budget.
5. [x] Review; fixes; full suite; commit `Issue #33: Part 33d-i - shared HUD symbol builder`; push.

   **Done (2026-11-08, SDD 20):** user decisions D39-D42 (map rows description-only with screenshot crops later in
   33e-ii, the bright planet icon only, the joystick radius parameter, the 2 px glossary feature floor). Screens per
   D37: 1280x720 and 1920x1080 instead of 1000x800. Contract by a separate agent, tests first by a separate author (red
   on stubs), builders and retain mode, the HUD through `ReplaySymbolPrims`; a temporary same-frame A/B recorder showed
   the glow-shape renderer identical and the Canvas fallback within one float ulp; before / after screenshots at the
   noise floor; P0 cost unchanged; review fixes; full suite 1683 / 0.

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
5. [ ] The 33d review re-checks the starting values and the glossary list (D26). The Distance darkening row shows the
   bright IconPlanet only (D40, no second brightness). Symbols are shrunk to fit with the 2 px feature floor (D42,
   applied by `SOLHudSymbols::Build` since 33d-i). The five map rows stay description-only until 33e-ii (D39).
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

### 33e-ii: screenshot crops for the map glossary rows (D39; opus/high; grill-me the open points first)

Builds on: D39. Depends on 33d-ii (the rows) and 33e (the shot-taking writer). Until this part the five rows (orbit
lines, route legs, waypoints, map destination pick, map grid) are description-only.

1. [ ] A capture step (a `-SOLWritePlayerGuide` sub-step or its own smoke flag) that opens the map in a pinned scene
   (fixed date and view, a planned route with a loop, a pick in progress, orbit lines on), renders the real overlays
   and saves one crop per row at fixed rects (2x the Help box so it stays sharp at 1920x1080 and 3840x2160).
2. [ ] In-game display: the crops as imported `UTexture2D` assets under `Content/SOL/UI/Glossary/` (paths in
   `SOLConstants.h`), drawn in the row's symbol box by an image brush; a row field or a small id -> texture table.
3. [ ] The same crops in `Docs/PLAYER_GUIDE.md` (copied next to the other guide images).
4. [ ] Tests: every description-only map row has a crop asset; the capture is deterministic enough to re-run.

Feasibility and cost (estimate, 2026-11-08): the screenshot path exists (`FScreenshotRequest` with UI, the smokes and
the 33e writer use it), so capture is about a day. Showing PNGs in game needs them imported as assets to be cooked
(re-run the capture and re-import when the map look changes, or the crops go stale; a test can only check that they
exist, not that they match). Runtime cost is a few small textures (about 0.25 MB each at 256x256 RGBA, less
compressed) and one image draw per row. Open points for the grill: crop size, whether the guide uses the same files,
and how a stale crop is noticed.

### 33f: smoke and profile, VR floor (opus/high)

Builds on: D21. Depends on 33e.

1. [ ] `-SOLSmokeSolidMenuProfile` passes `main-help-keys`, `main-help-glossary`, `pause-help-keys` (with
   `main-controls` / `pause-controls` and `main-settings-keys` / `pause-settings-keys`) in the default and view runs.
   Also profile the Overlays and route glossary tab (16 rows, the most symbol views).
2. [ ] Default runs x3 at 1920x1080 and 1280x720: every pass under 0.2 ms CPU / 0.3 ms GPU (medians).
3. [ ] View runs: 3840x2160 (4K proxy) and 3840x3744 (one XR-4 eye), three runs of the new passes, menu-share-only
   verdict. **The two-eye run (7680x3744) is skipped (D21)**; note it in the SDD amendment.
   The `main-help-keys` passes in the view runs carry the keyboard diagram, with the 33c `diagram-P<n>` check (0 legends
   clipped at that paint scale, D38: 33c verified only the 1280x720 and 1920x1080 scales) and the measured menu GPU
   replacing 33c's one-eye extrapolation (SDD 14.3, about 0.27 ms). 33c's test-page passes P12-P15 may be dropped once
   the Help page passes exist.
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
- 2026-11-07: O14 answered by the user: option A, the modifier marker (SDD D32), built in 33c.
- 2026-11-07: 33b done (SDD sections 14.2 and 20): the key cap widget on the Controls page and the Settings key rows.
  Next: 33c (the keyboard diagram, with D32).
