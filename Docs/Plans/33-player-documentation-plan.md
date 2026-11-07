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

**Gate:** the open questions of SDD section 18 (O1-O13) are not answered yet. Each part lists the ones it needs; a
part does not start until they are answered and folded into the SDD.

Every brief carries: the SDD path and the sections the part needs, `Docs/STYLE_GUIDE.md`, the banner and comment
rules, "asset and file paths only in `SOLConstants.h`", the SDD section 3 tech-debt items, the user's answers for the
part, "delete temporary screenshots and scratch files when done", and "report decisions you hit instead of choosing".

## Parts

### 33a: pure logic, tables and tests (tests opus/high; implementation opus/high)

Needs: O2, O3, O4, O9, O11, O12, O13. SDD sections 6-11.

1. [ ] Brief a separate opus/high subagent with SDD sections 6-10 only (no implementation). It writes
   `Tests/KeyCapsTest.cpp`, `KeyboardLayoutTest.cpp`, `KeyDiagramTest.cpp`, `GlossaryTest.cpp`, `HudSymbolsTest.cpp`
   (contract parts that need no HUD move), `PlayerGuideTest.cpp` and `SOLTest.KeyBindings.EveryBindingHasControlRow`.
2. [ ] Stubs; `Tools\RunTests.bat -Build -Filter SOLTest.KeyCaps` (and the others): red for the right reason.
3. [ ] Implement `Menu/SOLKeyCaps`, `SOLKeyboardLayout`, `SOLKeyDiagram`, `SOLGlossary`, `SOLPlayerGuide`,
   `UI/SOLHudSymbols` (the enum, the prims buffer, `AddToBatch`, the symbols that already have builders: icons, edge
   arrows, cards, radar helpers), the `SOL::Help` constants, `SOLMenuText` tables (`KEY_SHORT_LABELS`,
   `FIXED_KEY_SHORT_LABELS`, `GLOSSARY_TABS`, `GLOSSARY_ENTRIES` with descriptions from `GAME_MECHANICS.md`), and move
   `ESOLHudLine` to `UI/SOLHudLines.h` (values unchanged). Green; full suite.
4. [ ] Profile baseline before any restyle: add the `main-controls` and `pause-controls` passes to the default run of
   `-SOLSmokeSolidMenuProfile`; three runs at HEAD; record the medians in the SDD (section 14 amendment).
5. [ ] Adversarial review (fresh opus/xhigh, report only); fixes; full suite.
6. [ ] `graphify update .`; commit `Issue #33: Part 33a - key cap, diagram, glossary and guide logic`; push.

### 33b: key cap widget and the Controls restyle (opus/high)

Needs: O5. Depends on 33a. SDD section 6.

1. [ ] `Menu/SOLKeyCapRow` (`SSOLKeyCapRow` leaf, `USOLKeyCapRow` wrapper): the hull Small brush per cap, legends in
   the menu font, the mouse glyph, alternatives and chords, wrapping from `SOLKeyCaps::LayoutRow`, text measured once
   per content change.
2. [ ] `FillControlsTabs` / `RefreshControlsPage` use it (the 230-unit column kept); the `GetKeyCells` verification
   getter adapted.
3. [ ] `-SOLSmokeSolidMenu`: the rows sweep checks each cap row inside its cell at 1000x800 and 1280x720; expected
   check count updated; screenshots looked at, then deleted unless kept for the README.
4. [ ] Profile `main-controls` / `pause-controls` three runs against the 33a baseline (under 0.2 ms CPU / 0.3 ms GPU).
5. [ ] Review (fresh opus/xhigh); fixes; full suite; commit `Issue #33: Part 33b - key caps on the Controls page`;
   push.

### 33c: the keyboard diagram (opus/high)

Needs: O3, O4, O7. Depends on 33b. SDD section 7.

1. [ ] `Menu/SOLKeyboardDiagram` (`SSOLKeyboardDiagram` leaf, `USOLKeyboardDiagram`): caps, area fills and bars,
   short labels, badges, lock marks, the mouse, the area legend, the detail strip, the "Other keys" and "Unbound"
   lists; hover through `SOLKeyboardLayout::KeyAt` with the hover ramp (instant at strength 0).
2. [ ] Model built on show and on `OnKeyBindingsChanged` only (bind in `NativeConstruct`, unbind in
   `NativeDestruct`); no per-frame work beyond the hover ramp.
3. [ ] A smoke step on a test page put on the viewport (the 46f Info-page pattern, since Help comes in 33d): the
   assignment count, a real-cursor hover, a rebind moving a tint, fit at 1280x720 and 1000x800.
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
   the moved symbols); `HudShapesTest`, `OverlayIconsTest`, `RadarLayoutTest` re-validated.
4. [ ] After screenshots compared with the before set; `-SOLSmokeHud`, `-SOLSmokeOverlay`, `-SOLSmokeHoloMenu` green;
   `-SOLSmokeHoloMenuProfile` P0 within its budget.
5. [ ] Review; fixes; full suite; commit `Issue #33: Part 33d-i - shared HUD symbol builder`; push.

### 33d-ii: the Help page and its glossary (opus/high)

Needs: O1, O6, O13. Depends on 33c and 33d-i. SDD sections 5 and 8.2-8.5.

1. [ ] `SSOLHudShapeLayer` retain mode; `Menu/SOLHudSymbolView`; the shared `M_SOLHudShape` MID in
   `USOLSolidMenuSubsystem` (strength pushed on change).
2. [ ] `ESOLReadOnlyPage::Help` in `SOLPageTabs` (tabs, labels per O6, `LeftColumnWidth`); rail captions in
   `FSOLSolidTabRail` if O6 keeps them; `SOLMenuContent::BuildHelpPage` / `FillHelpTabs` / `RefreshHelpPage`.
3. [ ] The Help entry and pause tab (`EEntry::Help`, `ESOLMainMenuPage::Help`, `EMenuTab::Help`, `IsPageTab`), the
   stack cap and order per O1; `SOLMenuStateTest` and `SolidPageTabsTest` re-validated and extended (separate test
   agent for the new contract).
4. [ ] `-SOLSmokeSolidMenu` Help steps (SDD section 16) in both menus and both sizes; `-SOLMenuSmoke=all` no new
   failure; strength 0 / 2 shots looked at.
5. [ ] Review; fixes; full suite; commit `Issue #33: Part 33d-ii - Help page with keyboard and HUD glossary`; push.

### 33e: the player guide flag and `Docs/PLAYER_GUIDE.md` (opus/high)

Needs: O9, O12. Depends on 33d-ii. SDD section 9.

1. [ ] `SOL::Smoke::WRITE_PLAYER_GUIDE`, `SOL::Paths::PLAYER_GUIDE_FILE` / `PLAYER_GUIDE_IMAGE_DIR`;
   `Menu/SOLPlayerGuideWriter` (non-shipping): open Help, one shot per tab plus the hover shot, write the markdown,
   move the images, log and quit (non-zero on failure).
2. [ ] `Tools/WritePlayerGuide.bat` / `.ps1` (refuses while the editor has the project open).
3. [ ] Run it twice: the second run leaves `Docs/PLAYER_GUIDE.md` byte for byte the same; `SOLTest.PlayerGuide.UpToDate`
   green (or the warning step per O12); the player's `GameUserSettings.ini` unchanged.
4. [ ] Review (fresh opus/xhigh); fixes; full suite; commit `Issue #33: Part 33e - generated player guide`; push.

### 33f: smoke and profile, VR floor (opus/high)

Needs: O8. Depends on 33e.

1. [ ] `-SOLSmokeSolidMenuProfile` passes `main-help-keys`, `main-help-glossary`, `pause-help-keys` (with
   `main-controls` / `pause-controls`) in the default and view runs.
2. [ ] Default runs x3 at 1920x1080 and 1280x720: every pass under 0.2 ms CPU / 0.3 ms GPU (medians).
3. [ ] View runs: 3840x2160 (4K proxy) and 3840x3744 (one XR-4 eye), three runs of the new passes, menu-share
   verdict; the two-eye run per O8.
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

Needs: O10.

1. [ ] `Docs/GAME_MECHANICS.md` (section 8, the section 1 pointer, the `SOL::Help` tunables) and the in-game text
   audit (Controls, Info per O10, Help texts) so nothing states something false.
2. [ ] `Docs/ARCHITECTURE.md` (new types, the `-SOLWritePlayerGuide` flag, the HUD's use of `SOLHudSymbols`);
   `Docs/TECH_DEBT.md`; check the Mermaid blocks touched, if any, parse.
3. [ ] Regenerate the guide (`Tools\WritePlayerGuide.bat`); README: the "Player guide" link and 2-3 gallery shots
   (`33-help-keyboard.png`, a glossary tab, the Controls caps); temporary shots deleted.
4. [ ] `Docs/Plans/STATUS.md`: #33 to Done; SDD status line Done; this plan ticked.
5. [ ] `graphify update .`; `Tools\UpdateMetrics.bat`; commit `Issue #33: Part 33h - docs, in-game text and README`;
   push; close issue #33.

## Progress notes

- 2026-11-06: SDD and plan drafted from the grill-me answers (Q1-Q11 and the budget items). Open questions O1-O13
  (SDD section 18) wait for the user; no part has started.
