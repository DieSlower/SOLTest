<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 46: Solid console-style menus

SDD: [`Docs/SDDs/46-solid-menus.md`](../SDDs/46-solid-menus.md). Grill notes:
[`46-solid-menus-grill-notes.md`](46-solid-menus-grill-notes.md). This is a living document: tick each step as it
lands. Each part is built by its own subagent with its context kept under ~35%, and each part is small enough to verify
without the user. Commit convention: `Issue #46: Part 46x - <short description>` on `master`, then push. Run
`Tools\UpdateMetrics.bat` before each part commit and include `Docs/PROJECT_METRICS.md` and `README.md`. Never commit
`graphify-out/`, `SOLTest.uproject`, `CLAUDE.md` or `.mcp.json`.

**No schema change** (SDD D16): `SAVE_VERSION` stays 11, `KEY_BINDING_LAYOUT_VERSION` stays 10. A part that finds it
needs a saved value stops and reports back; it never adds one.

**No gate:** all 17 open questions of SDD section 15 were answered by the user on 2026-11-04 and are folded into the
SDD, so every part may start in order (dependencies between parts still apply). The "Answers" lines below name the
answers a part builds on.

Every brief carries: the SDD path and the sections the part needs, the STYLE_GUIDE, the banner and comment rules, "asset
paths only in `SOLConstants.h`", the relevant TECH_DEBT items (SDD section 3), the user's answers (SDD section 15) for the part, and "report decisions you hit instead of choosing".

## Parts

### 46a: pure logic, test-first (tests opus/high; implementation sonnet/medium)

Answers: Q2 (strength 0 / 2 as #42), Q16 (starting values approved).

1. [x] Brief a separate opus subagent with SDD sections 8 and 9 only (no implementation). It writes
   `Source/SOLTest/Tests/SolidMenuTest.cpp` (`SOLTest.SolidMenu.*`, the cases of SDD 8.5).
2. [x] `Menu/SOLSolidMenuMath.{h,cpp}` (namespace `SOLSolidMenu`, `FSOLSolidHull`, `ESOLSolidButtonState`,
   `FSOLSolidButtonLook`) and the `SOL::SolidMenu` constants (timings, hull, layout; the palette comes in 46c). The
   stub / red step was folded into the implementation (the tests already existed).
3. [x] Implement; green (43 `SOLTest.SolidMenu` tests after the review; full suite 1366). Two test / contract conflicts resolved in the
   SDD 8.2 amendment: the glow halo is `s x Edge` (doubles at strength 2, section 7) instead of `GlowScale(s) x Edge`;
   `FSOLSolidButtonLook` gained `bDisabled` so a disabled look's tint carries `DISABLED_ALPHA`. Hull feature geometry
   and clamp rules recorded in the SDD 8.1 amendment (new `HULL_MAX_FEATURE_FRACTION`). Adversarial review
   (opus/xhigh, report only); fixes (a step / chin left with no width removed whole, `EvaluateButtonFrom` for a
   mid-ramp reversal without a pop, `TracePolyline` returns 0 or at least 2 points, chamfer sums in double,
   `static_assert`s on the constant invariants, step / chin / notch SDF tests, a clamp sweep, the ramp after the press
   linger, the cubic ease pinned); tint caveats for 46c recorded in SDD 8.3; full suite.
4. [x] Commit `Issue #46: Part 46a - solid menu maths`.
5. [x] **Answered by the user on 2026-11-04:** selected is orthogonal to hover (a separate selected flag), so an
   active tab that is also focused or hovered shows the amber hover look *and* its cyan selected mark. Built in 46c
   (SDD 8.2 46c amendment, `FSOLSolidButtonAnim`, `USOLMenuButton::SetSelected`). (Review finding: hover takes keyboard
   focus and focus stays on a clicked tab, so one state per button would have hidden the active tab's mark.)

### 46b: assets and import script (sonnet, medium)

Answers: none needed (the asset list of SDD section 10). Font: Rajdhani (D4).

1. [x] Textures with the craft MCP tools into `Docs/Art/menu-style/textures/` (SDD 10 table): brushed grain 512 (or
   copy the kept sample tile if the PhotoCraft export fails), rivet, vent strip, hazard mask, noise. PhotoCraft started
   with `--automation-write-root <abs Docs/Art path>`; VectorCraft with absolute export paths. Check each PNG is written
   and seamless where it must tile (offset by half a tile, look for seams).
2. [x] Rajdhani Regular / Medium / SemiBold / Bold TTFs and `OFL.txt` into `Docs/Art/fonts/rajdhani/` (Google Fonts;
   licence checked: SIL OFL 1.1).
3. [x] `SOL::Paths` entries for every asset (SDD section 9) and `Tools/UI/import_solid_menu_assets.py` (headless,
   idempotent, MD5 tags, the `import_holo_menu_sounds.py` pattern, paths read from `SOLConstants.h`). Run it twice: the
   second run writes no `.uasset`.
4. [x] `Docs/Art/menu-style/README.md`: the sources, tool settings and how to regenerate.
5. [x] Commit `Issue #46: Part 46b - solid menu textures, font and import script` (the PNGs (scripted by
   `generate_textures.py`), the TTFs, the script, the constants). **Open:** no `.uasset` was in the working tree at the
   commit (`Content/SOL/UI/Solid/` absent), so the import script's two runs (step 3) still have to be done and the
   assets committed, at the latest at the start of 46c. **Done in 46c step 1** (script run twice, the second run
   wrote no `.uasset`; commit `Issue #46: Part 46c - import solid menu textures and Rajdhani font assets`).

### 46c: hull material and kit (opus, high)

Answers: Q1 (`HudEffectsStrength`), Q2, Q12, Q13, Q14 (palette), Q16, Q17 (budget). Depends on 46a, 46b.

0. [x] Capture the profiling baseline before any rendering change: build `-SOLSmokeSolidMenuProfile` as a stand-alone
   flag (SDD 13) and run it three times at HEAD (today's look), medians into an SDD amendment. **Done:** the
   ProfileGPU / `stat dumpave` capture moved out of the holo smoke into the shared `FSOLUiProfileCapture` (holo smoke
   rechecked, 12/12, all PASS); `FSOLSolidMenuProfileSmoke` (main menu and pause menu with Settings, 1920x1080 and
   1280x720); three runs at `816da1a` plus the working tree, medians in SDD 13.1 and stored in the .cpp; a check run
   against them PASS on all four passes.
1. [x] Verify in the UE 5.8 engine content which outputs `GetUserInterfaceUV` has (the size the SDF needs, SDD 4.3);
   record the answer in an SDD amendment; pick the size path (UV outputs, or the MID size fallback for the big panels).
   **Done:** Pixel Size (UV 3, physical pixels incl. DPI) + normalized UV 4; lengths in UMG units x a pushed
   `DpiScale` (SDD 4.3 amendment). Engine finding: Slate multiplies a material brush's colour by its tint, so the tint
   is packed above `TINT_VERTEX_FLOOR` and divided out in the material (SDD 4.3, 8.3 amendments).
2. [x] `Tools/UI/create_solid_menu_materials.py`: `M_SOLSolidHull` (HLSL mirroring `SOLSolidMenu::HullDistance`, tint
   decode mirroring `DecodeButtonTint`) and the style instances; parameter names in `SOL::SolidMenuMaterialParams`.
   Idempotent, `-SOLRebuild`. **Done:** nine `MI_SOLSolid*` instances, the disabled art with its two cue options (SDD
   5.3.1, the choice open for the user); compiled with `-AllowCommandletRendering` (no errors); a second run wrote no
   `.uasset` (SDD 10.1).
3. [x] **Done** (SDD 4.7 amendment): kit implemented, 27 `SOLTest.SolidMenuKit` tests green without edits (full
   suite 1402/1402). Material fix found at the first render: `M_SOLSolidHull` shaded in display space (Slate's
   `SOURCE_IN_LINEAR_SPACE` is not visible to material code) and now shades in linear; rebuilt by the script. Was:
   **API half done** (headers with stub bodies, the editor target builds): `SOLSolidMenuSubsystem.h`
   (`FSOLSolidMenuParamCache`, `ESOLSolidTexture`), `SOLSolidStyle.h` (`ESOLSolidStyle`, `ESOLSolidButtonVariant`,
   `ESOLMenuFontWeight`, `ESOLMenuTextRole`, pure decisions and factories), `SOLSolidPanel.h`, `SOLSolidTraceLayer.h`
   (`FSOLSolidTrace`), `USOLMenuButton` (variant, `SetSolidDisabled`, hover trace, `FSOLSolidButtonAnim`; the flat
   look still runs until step 4). Next: tests against the headers (separate agent), then the implementation.
   `USOLSolidMenuSubsystem` (loads once, one warning per missing asset with a flat fallback, shared MIDs, strength
   pushed on change from `HudEffectsStrength`), `USOLSolidPanel`, `SSOLSolidTraceLayer`, `SOLSolidStyle` (slider, checkbox,
   scroll bar, button variants, `MenuFont`), the palette constants (Q12-Q14).
3a. [x] Widen the HUD effects slider's label and help text (`SETTINGS_HUD_EFFECTS`, the help text near
   `SOLMenuText.h:469`, and the Hologram menu effects sentence "not the HUD one") so they say it also scales the
   full-screen menu animations (Q1); `SOLInfoTextTest` re-validated. **Done:** label "HUD and menu effects", the HUD
   help text and the Hologram menu effects sentence widened (`SOLMenuText.h`), `GAME_MECHANICS.md` 8.1 row; the
   info-text tests pass unchanged (they check placeholders, not this wording).
4. [x] `USOLMenuButton` restyle (variants, `RefreshHighlight` drives `EvaluateButton`, tint, lift, trace request;
   hover still takes focus, rebind listening unchanged) and `SOLMenuContent::MakeText` / `MakePage` on the new font.
   At this point every screen shows the new buttons and font with today's layouts. **Done** (SDD 4.7): variants on
   every screen, the orthogonal selected flag, `SetSolidDisabled`, the background colour = tint / style tint, the
   Rajdhani font and solid text colours, the unused `SOL::MenuStyle` colours removed, `BUTTON_PADDING_Y` 4.
5. [x] The `-SOLSmokeSolidMenu` scaffold (flag constant, harness, strength 0 / 2 in the smoke slot, screenshots).
   **Done:** `FSOLSolidMenuSmoke` + `USOLSolidMenuSmokeBoard` (enabled / disabled comparison of every control type),
   13/13 checks; the screenshots were looked at and deleted.
6. [x] `-SOLMenuSmoke=all` no new failures; review (opus/xhigh); fixes; suite. **Done (2026-11-05).** Adversarial
   review (opus/xhigh, report only), three should-fix items and eight nits, all fixed (SDD 4.7 and 13 amendments):
   the button compares a refresh with the look **last written** (a ramp in steps under `LIFT_EPSILON_PX` locked a
   stale lift; the new test failed on the old code and passes now); new `SOLTest.SolidMenuKit.Button.*` tests
   (normal, selected, selected + hover, disabled, strength 0 / 1 / 2, settled refresh writes nothing, the fine-step
   ramp, the background divide) and `Panel.UniqueMaterialFollowsStyle`; the panel brushes are `UPROPERTY(Transient)`
   (early resets in the subsystem's `Deinitialize` dropped); screens read the frame once per tick
   (`USOLSolidMenuSubsystem::BeginFrame`, `RefreshHighlight` removed); the trace layer's ring is precomputed and each
   line's points moved in once, its header and SDD 7 / 13 corrected (the layer repaints every frame; one allocation
   per drawn line is the engine API's); solid-disabled buttons are not focusable; `SetHull` with another style
   recreates the unique material; only the rivet and vent textures are loaded (`T_SOLSolidNoise` unused, its load
   removed); the hull dither is +-0.5 / 255 in sRGB output (script rerun, second run wrote nothing); docs and
   TECH_DEBT (single-run verdicts widened, the per-pixel `ClampHull` at about 307 PS instructions, the trace layer's
   per-paint arrays). Tests: `SOLTest.SolidMenu` 52, `SOLTest.SolidMenuKit` 35, full suite 1410/1410. Smokes:
   `-SOLSmokeSolidMenu` 13/13, `-SOLSmokeHoloMenu` 86/86, `-SOLSmokeOverlay` 34/34, `-SOLSmokeHud` 24/24,
   `-SOLMenuSmoke=all` 47 passed / 3 failed (the three known stale checks of TECH_DEBT: thrust at Mars, Esc and
   Ctrl+F1 resume cursor). Profile, three runs, medians of `dcpu` / `dgpu` (ms): P0 +0.033 / +0.006, P1 -0.014 /
   +0.004, P2 -0.017 / +0.002, P3 -0.007 / -0.010 (all inside 0.1 / 0.3; two single-run FAILs, P1 +0.134 and P0
   +0.180, were run-to-run noise, the TECH_DEBT single-run item). Commit
   `Issue #46: Part 46c - solid hull material, panel kit, restyled buttons, selected-with-hover and disabled art,
   profiling baseline`.
7. [x] **46c addition (user requirement, 2026-11-04): disabled art.** Every control type has a disabled look that is
   clearly different from normal and hover (desaturated hull without grain or edge highlight, LED off, no hover, the
   disabled cue, `DISABLED_TEXT_COLOR` label; SDD 5.3.1), `USOLMenuButton::SetSolidDisabled` without Slate's disabled
   dim, the factories' disabled brushes, checked by the kit tests and the smoke board.
8. [x] **46c follow-ups (user decisions, 2026-11-05).** Disabled cue = (b) the dark slotted recess, committed as the
   default (`DISABLED_CUE_STYLE` 1, baked by the material script, pinned by a kit test); `TEXT_DIM_COLOR` #8A949D and
   `SOLSolidStyle::DimTextColor` for the old dim tier (hints, help lines, dirty text at rest, Demo "no warnings", the
   copyright line); `T_SOLSolidNoise` deleted (asset, PNG, generator step, import entry, path constant); the
   cue-compare scratch image deleted; selected + hover kept as built. Commit `Issue #46: Part 46c - follow-ups`.

### 46d: main menu and start-location page (opus, high)

Answers: Q5 (shell right of the stack), Q9, Q10, Q15. Depends on 46c.

1. [x] `USOLMainMenuWidget`: hull panel column (title step, tagline, stack buttons, hazard stripe under Quit, hint, the
   version / credits recess per Q10), traces and the hovered entry's trace (Q15), the page shell (SDD 6.1) placed to the
   right of the stack (Q5) with Back, panel-open / power-up / pulse. **Done** (SDD 6.2): fixed-height column
   (`PAGE_HEIGHT` 760 -> 800), traces from the layout constants, hover traces rebound in `NativeConstruct`, the shell
   narrowing to the room right of the column (fits 1000x800), Back = Esc through `ClosePage`, `SOLSolidMenu::
   BeginPanelOpen` / `BeginPanelClose` / `PanelProgress` / `AdvancePanel` on the #42 phases (duration-parameter forms
   of `SOLHoloMenu`'s functions; TDD: 12 `SOLTest.SolidMenu.PanelAnim.*` tests, red 12/12 on stubs, then green),
   `ESOLSolidTexture::Hazard` for the stripe; About credits gain "Menu font: Rajdhani, SIL OFL 1.1".
2. [x] Start-location page in the shell (recess list, solid scroll bar, selected row: cyan underline + LED, Q9).
3. [x] The build-time version line (Q10, SDD 11.1): `SOLTest.Build.cs` writes `Intermediate/Generated/SOLBuildVersion.generated.h` from `git rev-list --count HEAD` and the build month / year, only when the content changes, fallback commit count 0 with one warning; a pure format helper (`0.1.YYMM.<count>`) with a unit test; the chin recess shows the line plus studio and engine. Not committed (Intermediate). **Done** (SDD 11.2): `Menu/SOLBuildVersion`, 14 `SOLTest.BuildVersion.*` tests (red 13/14 on stubs, the 14th checks the test's own helper), the git reflog as a UBT external dependency.
4. [x] Smoke steps 1-3 of SDD 14 for the main menu; screenshots looked at; `-SOLMenuSmoke=all` no new failures.
   **Done:** `-SOLSmokeSolidMenu` 36/36 (was 13), screenshots at 1280x720 and 1000x800 looked at and deleted;
   `-SOLSmokeHud` 24/24, `-SOLSmokeOverlay` 34/34, `-SOLSmokeHoloMenu` 86/86, `-SOLMenuSmoke=all` 44 passed / 6 failed
   with the game window focused (the TECH_DEBT stale free-cursor checks: Play, Play again, Play a third time, Esc,
   Ctrl+F1, and thrust at Mars; no new failure). Profile medians (SDD 13.1 46d check): P0 +0.044 / +0.047, P1 +0.049 /
   +0.028, P2 -0.020 / +0.001, P3 +0.018 / +0.003 ms, all inside the budget.
5. [x] Review; fixes; suite. Commit `Issue #46: Part 46d - solid main menu` (left to the main session).
   **Final user decisions applied (2026-11-05):** `TEXT_DIM_COLOR` #8A949D, version-line credit "ARS LLC", keyboard focus
   on Play after every menu show (TECH_DEBT item removed), screenshot `46-main-menu-solid.png` in the README gallery.
   **Review fixes done** (SDD 6.2, 11.2, 13.1): the power-up LED blink (drawn by the column's trace layer in stack
   order, ending off; an interpretation of SDD 7 recorded in SDD 6.2) and the idle pulse dot on the longest trace; a
   reshow snaps the shell closed (a page left open at Play no longer animates shut on the return); a button given a
   new trace layer pushes its current hover trace; the version header never fails the build (`__has_include` with a
   `__DATE__` YYMM fallback, the folder created first, a locked per-process cache, temp file + move, git's stderr in
   the warning, `.git` looked for upward); the trace bends named; the hint-above-traces check; header comments on the
   `SOLHoloMenu` duration overloads; three TECH_DEBT items (VR floor extrapolated, recess overdraw, the input mode's
   deferred focus leaving no entry focused after a menu show). Tests: +7 (`SOLTest.HoloMenu.AnimDurationInstant`,
   `.AnimDurationDefault`, `SOLTest.BuildVersion.Line.Exact`, `SOLTest.SolidMenu.PowerUp.LedBlinkOrder`,
   `.LedBlinkNone`, `.Running`, `SOLTest.SolidMenu.Pulse.Dot`); full suite 1443/1443. Smokes: `-SOLSmokeSolidMenu`
   42/42 (was 36; new `a-hint-clear`, `a-led-blink`, `a-pulse-dot`, `s-main-s0-lights`, `t-return-shell-closed`,
   `t-return-play-trace`), `-SOLSmokeHud` 24/24, `-SOLSmokeOverlay` 34/34, `-SOLSmokeHoloMenu` 86/86,
   `-SOLMenuSmoke=all` 44/6 run without focusing the window (the same six stale-cursor TECH_DEBT failures, no new one).
   Profile medians (three runs at 1920x1080): P0 +0.042 / +0.046, P1 +0.025 / +0.030, P2 -0.025 / +0.002, P3 -0.024 /
   -0.007 ms. README shot `Docs/Images/46-main-menu-solid.png` (1920x1080, strength 1, no page).

### 46e: Settings shell with the tab column (opus, high)

Answers: Q8 (opens on the first tab), Q15 (rows: amber edge + underline, no trace). Depends on 46c (46d for the shell if it was built there; otherwise the shell is built here and 46d reuses
it).

1. [x] `USOLSettingsPageWidget`: the six sections as six tabs (D13), content recess, `SOLSolidStyle` sliders, toggles
   and steppers, the shared footer, `ScrollToBinding` switching to Key bindings; every handler and smoke setter kept.
   **Done** (SDD 6.3): the pure `SOLSettingsTabs` (16 `SOLTest.SolidSettings.*` tests by a separate agent, red on the
   missing header, then green), the tab rail and content recess, `RefreshFromSettings` on Graphics every open (Q8),
   `RefreshControls` for Apply / Revert / hide, `USOLSolidPanel::MakeRecess`; tuned `TAB_COLUMN_WIDTH` 220 and the
   tab label spacing 100 from the screenshots; `SETTINGS_OVERLAYS` shortened to "Overlays" with its explanation as the
   tab's hint line.
2. [x] The embedded Overlay list (`USOLOverlayMenuWidget`, embedded path only) in the solid font, colours and hover
   look; the standalone Ctrl+O menu untouched (check `-SOLSmokeHoloMenu` and `-SOLSmokeOverlay`). **Done:** holo
   86/86, overlay 34/34.
3. [x] Smoke step 4; the existing settings checks of `-SOLMenuSmoke` still pass. **Done:** `-SOLSmokeSolidMenu` 55/55
   (was 42), `-SOLMenuSmoke=all` 47 passed / 3 failed (the TECH_DEBT stale checks: thrust at Mars, Esc and Ctrl+F1
   resume; no settings failure). Profile medians (SDD 13.1 46e check) P0 +0.021 / +0.058, P1 -0.011 / +0.035, P2
   -0.043 / +0.010, P3 -0.036 / +0.015 ms, inside the budget.
4. [x] Review; fixes; suite. Commit `Issue #46: Part 46e - solid Settings with the tab column`. **Review fixes:**
   the embedded Overlay tabs on the Small hull at rest, the list's hover and the rail's trace cleared on every show,
   `BeginRebind` shows Key bindings, the settled-look time from `HOVER_RAMP_S`, long lines and comment wraps; the
   suggested ellipsis for the stepper values was tried and cut nothing (clipping kept). Open for the user: the slider
   fill, two amber highlights at once (focus kept on a clicked button while a slider is hovered), per-tab scroll memory,
   the narrow-view stepper values. For 46f: factor the tab rail into a kit piece for Controls / Info.
5. [x] Follow-ups (the user's six decisions, SDD 6.4; commit `Issue #46: Part 46e - follow-ups`): "Overlays" label
   and `TAB_COLUMN_WIDTH` 220 kept; per-tab label columns (`SETTINGS_GRAPHICS_LABEL_WIDTH` 134,
   `SETTINGS_GAMEPLAY_LABEL_WIDTH` 264) and the scroll bar's gap moved onto the bar (stepper values fit at 1000x800:
   186.4 drawn in a 190.9 slot); the cyan slider fill (`USOLSolidSlider` / `SSOLSolidSlider`,
   `SOLSolidStyle::SliderFill` / `MakeSliderFillBrush`); hover wins over a button's focus (`ResolveState`'s
   `bHoverElsewhere`, the page's `IsHoverOnFocuslessControl`, also passed by the main and pause menus; last input
   wins: a key gives the focus back; the falling trace stub fixed); every tab back at its top on each open. Tests
   `SolidMenuKit.Anim.ResolveState` (16 cases), `SolidMenuKit.Style.SliderFill`, `SolidMenuKit.Factory.SliderFillBrush`,
   `SolidSettings.Layout.LabelColumnWidth`; `-SOLSmokeSolidMenu` 66/66 (was 55). Review (opus, xhigh) fixes: the main
   and pause menus' buttons, last input wins, the fit checks measured at the drawn scale (they found "Chase Down
   safety margin" running into its slider at 1000x800: the Gameplay column), the SDD section 7 heading, slider
   null-style guard, fill brush default and repaint, null checks, smoke knob geometry from the style.

### 46f: Controls, Info, About and Demo in the shell (sonnet, medium)

Answers: Q6 (today's entries, sections as tabs), Q7 (Demo: no tab column). Depends on 46e (the shell with a tab column).

1. [x] `SOLMenuContent::BuildControlsPage`, `BuildInfoPage`, `BuildAboutPage` in the shell (Q6: today's entries, each page's sections as tabs; read-only
   recesses, key / value columns, headings); the live-key refresh unchanged.
   **Done** (SDD 6.5): the tab rail was promoted into the kit first (`FSOLSolidTabRail`, Settings unchanged), then
   `USOLReadOnlyPageWidget` and the pure `SOLPageTabs` (14 `SOLTest.SolidPages.*` tests by a separate agent from the
   contract); Controls 10 tabs, Info 4, About 2.
2. [x] `USOLDemoPageWidget` in the shell, no tab column (Q7: presets, recessed sections, warnings in #E5533D, footer).
   **Done:** recess groups in one scroll box with the solid scroll bar style, scrolled to the top by `RefreshFromMenu`.
3. [x] Smoke step 3 for these pages in both menus; screenshots. **Done:** `-SOLSmokeSolidMenu` 83/83 (was 66), holo
   86/86, overlay 34/34, hud 24/24, `-SOLMenuSmoke=all` 47 passed / 3 failed (the TECH_DEBT stale checks); full suite
   1476/1476; profile PASS in three runs (SDD 6.5).
4. [x] Review; fixes; suite; commit `Issue #46: Part 46f - solid Controls, Info, About and Demo`.
5. **Review (opus, xhigh) and fixes:** no High finding. Fixed: `GetFillScroll` bounds check, Demo grouping guards (title count static_assert, last group added after the loop), stale and wrong comments, long lines and separators, the Info smoke page removed in `Finish`. Recorded in TECH_DEBT: the pause menu's 10 Controls tabs may not fit the flat pause panel (estimated 558 of 576 units, measure in 46g), recess overdraw now also on these pages (profile in 46h). Left for the user: About's two tabs (the SDD table said "no tab column"; Q6 asks for sections as tabs), clicking the already-selected pause tab resets Controls / Info to the first tab (as Settings does), the review's low items (raw `UTextBlock*` in `FColumnText`, duplicate key arrays, kit names for the `SETTINGS_TAB_*` constants).

### 46g: pause side panel (opus, high)

Answers: Q3 (dim only), Q4, Q11 (reuse #42 cues). Depends on 46c, 46e, 46f.

1. [x] `USOLPauseMenuWidget`: the left side panel (PAUSED in the title step, the vertical stack, the hint), the slide-in
   and slide-out (input back at once, SDD 4.6), the dim of the live view (Q3: no behaviour change), the pages in the shell right of the side panel and Quit's choices inside the side panel (Q4).
2. [x] Q3 means the simulation keeps running: `PAUSED_BODY` and the "sim keeps running while paused" smoke check stay
   as they are (verify they still pass).
3. [x] Cue wiring on all solid screens with the #42 cues through `PlayCue` (Q11); no new assets.
4. [x] Smoke steps 5 and 7; screenshots.
5. [x] Scrolling rails (user, 2026-11-05; SDD D17): if a tab rail (Settings/Controls/Info/About/pause) or the main-menu button stack has more entries than fit, it scrolls up and down. Implement for the shared `FSOLSolidTabRail` and the main-menu column, with tests and smoke checks, and check the pause Controls panel's 10 tabs fit/scroll.
6. [x] Review; fixes; suite. Commit `Issue #46: Part 46g - solid pause side panel`.
7. **Done (SDD 6.6):** suite 1506/1506 (27 new tests), `-SOLSmokeSolidMenu` 169/169, holo 86/86, overlay
   34/34, hud 24/24, `-SOLMenuSmoke=all` 47 / 3 (TECH_DEBT stale checks). Review fixes all applied. The
   profile is over the 0.1 ms CPU budget on most screens (A/B in SDD 6.6, TECH_DEBT); the GPU is in budget.

### 46h: smoke completion and profile (opus, high)

Depends on 46d-46g.

1. [x] `-SOLSmokeSolidMenu` complete (every step of SDD 14 at 1280x720 and 1000x800, strength 0 / 1 / 2).
2. [x] `-SOLSmokeSolidMenuProfile`: three runs, medians against the 46c baseline, verdicts against the Q17 budget (under 0.2 ms CPU, moved from 0.1 ms by the user on 2026-11-06, and 0.3 ms GPU extra per screen);
   numbers into an SDD amendment (SDD 13).
3. [x] Regression: `-SOLMenuSmoke=all` (no new failures beyond TECH_DEBT), `-SOLSmokeHoloMenu`, `-SOLSmokeOverlay`;
   PIE smoke with unreal-mcp if the editor is open (screenshots at strength 0, 1, 2).
4. [x] Fix what fails; suite. Commit `Issue #46: Part 46h - solid menu profile, VR floor and budget`.

**46h change (user decision, 2026-11-06).** Resume notes:
- (Resolved: the machine was rebooted after the hung GPU driver. The two-eye run was not re-run in 46h: it must never be killed mid-run.)
- The profile smoke's view-run verdict judges ONLY the menu's own share (CPU delta vs baseline <= 0.2 ms, menu GPU within the provisional VR ceilings 1.2 ms one eye / 2.4 ms two eyes), not the whole-frame VR floor. The whole-frame check uses the 4K / 100 Hz proxy (3840x2160, frame p99 <= 10.0 ms) and is project-level VR work, not a menu failure (`Docs/Research/VR_PERFORMANCE_TARGETS.md`, "Proxy on this machine"). XR-4 measurements already taken stay as informational.
- Non-Settings pages (Demo, Key bindings, etc.) are judged against the Settings baseline: re-measure with 3 runs and flag only if still over 0.2 ms CPU.

5. **Done (SDD 13.2):** suite 1511/1511 (5 `SOLTest.SolidMenuProfile.*` tests), `-SOLSmokeSolidMenu` 177/177, holo
   86/86, overlay 34/34, hud 24/24, `-SOLMenuSmoke=all` 44 / 6 (the TECH_DEBT stale cursor and thrust checks). Profile:
   three default runs 4/4 PASS each (median `dcpu` +0.044 to +0.063, `dgpu` +0.034 to +0.062); the 4K proxy run and two
   one-eye runs 9/9 PASS on the menu share (menu GPU 0.08-0.90 ms, Demo median `dcpu` +0.131, Key bindings +0.092);
   the whole frame misses the 4K / 100 Hz proxy (p99 16.0-17.7 ms) and the XR-4 lines, project-level VR work. The
   two-eye run was skipped (driver hang risk); its first-run P0-P2 stay recorded.

### 46i: final adversarial review (opus, xhigh, report only; fixes by sonnet or opus by size)

1. [x] Fresh reviewer over all #46 changes: performance (SDD 13), style guide, the SDD against the code (every decision
   D1-D16 and every answered question), TECH_DEBT items not re-reported.
2. [x] Fixes; re-check against the SDD; full suite and the smokes of 46h. Leftovers into `Docs/TECH_DEBT.md`.
3. [x] Commit `Issue #46: Part 46i - final review fixes`.
4. **Done (SDD 6.7):** suite 1537/1537, `-SOLSmokeSolidMenu` 192/192, holo 86/86, overlay 34/34, hud 24/24,
   `-SOLMenuSmoke=all` 47 / 3 (the TECH_DEBT stale cursor and thrust checks); profile, three default runs 4/4 PASS
   each, medians `dcpu` / `dgpu` P0 +0.076 / +0.063, P1 +0.015 / +0.033, P2 +0.090 / +0.038, P3 +0.028 / +0.064;
   `GameUserSettings.ini` byte for byte unchanged after every run. Leftovers in TECH_DEBT (T15, T16, S10, S11, the
   wide key-row labels, the off-screen wrap, the "²" glyph, W12, K3, K7).

### 46j: docs sync, in-game text and README screenshots (sonnet, medium)

1. [x] `GAME_MECHANICS.md` (section 8, 8.1 HUD effects slider row per Q1, the `SOL::SolidMenu` tunables table) and the
   in-game texts in `SOLMenuText.h` (slider label and description, Back label, version line and font credit, `PAUSED_BODY` unchanged per Q3), audited
   against the code.
2. [x] `ARCHITECTURE.md` (new types, smoke flags), `TECH_DEBT.md` (cook paths gain `/Game/SOL/UI/Solid`),
   `Docs/Art/menu-style/README.md` final.
3. [x] README gallery: `46-main-menu-solid.png`, `46-settings-solid.png`, `46-pause-solid.png` in `Docs/Images/`
   (window screenshots; recaptured at 1280x720 from the 46j smoke run, plus `46-controls-solid.png` and the new
   `46-demo-solid.png`).
4. [x] `STATUS.md` #46 Done; `Tools\UpdateMetrics.bat`. Commit `Issue #46: Part 46j - docs, in-game text and
   screenshots`.
5. [x] From the 46i review (C8, C9, C15): (C8) `GAME_MECHANICS.md` has no `SOL::SolidMenu` tunables table (SDD 12;
   only constants scattered through the section 8 and 8.1 rows), and does not say that Controls, Info and About are
   tab pages (open on the first tab, a re-click is a no-op) or list the main menu's cues; (C9) four wrong or
   incomplete statements there: the Ctrl+O row's "the Overlays list in Settings keeps its plain look" (false since
   46e), key conflicts "turn orange" (now `WARNING_COLOR` #E5533D, Q14), section 8.1's "sliders and toggles get the
   amber edge and underline" (the groove, knob, slot and puck have no underline), and the 8.1 HUD row omits the pause
   slide and dim and the shell close; (C15) `ARCHITECTURE.md`'s main menu line omits Demo from the column, and its
   `-SOLSmokeSolidMenu` lines describe only the 46c-46f steps (no 46g pause, 46h or 46i steps).

## Progress notes

- 2026-11-04: SDD and plan drafted from the grill-me decisions (D1-D16). 17 open questions (SDD section 15) were
  put to the user. The temporary layout and hover sample images were deleted once their decisions were recorded (D11-D15).
- 2026-11-04: all 17 open questions answered by the user and folded into SDD 46 (Q1 `HudEffectsStrength`, version line
  `0.1.YYMM.<commits>` generated at build time, dim-only pause, pages in the shell beside the panels, #42 cues reused,
  budget 0.1 ms CPU / 0.3 ms GPU). The plan has no remaining gate; parts follow their dependencies.
- 2026-11-05: 46c done (review fixes, tests, smokes, profile; plan step 6). Next: 46d (main menu and start-location
  page).
- 2026-11-05: 46d steps 1-4 built and verified (SDD 6.2, 11.2, 13.1 46d check); full suite 1436/1436. Next: the 46d
  review (step 5), then the commit.
- 2026-11-05: 46d committed with the user's final decisions (dim text #8A949D, "ARS LLC", focus on Play). Next: 46e.
- 2026-11-05: 46d review fixes applied and verified (plan step 5; suite 1443/1443, solid smoke 42/42, profile in
  budget). Next: the main session's commit of 46d, then 46e.
- 2026-11-05: 46e done (SDD 6.3, 13.1 46e check): tab logic test-first (16 tests), the tab column, the embedded list
  restyle, smoke step 4 (55/55), review fixes; full suite 1459/1459. Next: 46f.
- 2026-11-05: 46e follow-ups done (SDD 6.4, 13.1 follow-up check; plan 46e step 5). Next: 46f.
- 2026-11-05: 46f built and verified (SDD 6.5); next: the review fixes and the commit, then 46g.
- 2026-11-05: 46f follow-up: a re-click on the selected tab is a no-op in the pause menu, Controls, Info, About and Settings
  (SDD 6.5, `IsTabChange`); suite 1479/1479, solid smoke 92/92, holo 86/86, overlay 34/34. Next: 46g.
- 2026-11-05: 46f done (8456579, 8150cc4); next 46g; main session paused for /compact.
- 2026-11-06: 46g done (SDD 6.6): the pause side panel, the close flow, the #42 cues and the scrolling rails
  (D17, the duplicate D15 label fixed); review fixes applied; the CPU profile has no margin left (user
  decision). Next: 46h.
- 2026-11-06: the user moved the Q17 CPU budget from 0.1 ms to 0.2 ms (GPU 0.3 ms and #42 unchanged; frame about
  5.9-6.3 ms, 160-170 fps, with a solid menu open, i9-9960X / RTX 2080 Ti). 46h in progress, PAUSED on a hung GPU
  driver (see the resume state below).
- **46h resume state (2026-11-06, before the reboot; superseded by the note below).** Done: `-SOLSmokeSolidMenuProfile` view runs (with
  `-ResX` / `-ResY` other than 1920x1080: nine screens at that size, the game window's size limits lifted past the
  desktop, a 240-frame plain window per pass giving `frame_p99` / `game_p99` / `gpu_p99`, the VR floor verdict
  `vr=`); the verdict maths in `SOLSolidMenuProfile` (`IsWithinBudget`, `VrFloorForView`, `Percentile`) with
  `Tests/SolidMenuProfileTest.cpp` (3 tests, not yet run); the CPU budget 0.2 in code, SDD 46 (section 13 budget, Q17)
  and this plan; `-SOLSmokeSolidMenu` gains `SmallAbout`, `PauseStrengthTwo` and `PauseSmall` (built, not yet run);
  TECH_DEBT: the VR-extrapolation item deleted, the trace-layer item says why no cheap fix exists (both 5.8
  `MakeLines` overloads take a `TArray` by value). Measured: the one-eye run (3840x3744, the real size, 14/14 checks)
  and P0-P2 of the two-eye run (7680x3744, the real size). Not compiled yet: the view-run watchdog
  (`PROFILE_WATCHDOG_VIEW_PASS_S` 180 s; the old 45 s a pass is too short for two eyes, 60-110 s a pass). **Blocker:**
  killing the two-eye run left `UnrealEditor.exe` (and `LiveCodingConsole.exe`) unkillable and the GPU driver hung
  (`nvidia-smi` and a new D3D12 init hang), so the DLL cannot be relinked; needs a reboot or a driver reset. Next,
  in order: build; run the full suite; the two-eye run (all nine passes); three default runs; `-SOLSmokeSolidMenu`,
  holo, overlay, hud, `-SOLMenuSmoke=all`; then SDD 46 section 13.2 (numbers below), the TECH_DEBT hull / recess /
  CPU items, `VR_PERFORMANCE_TARGETS.md` section 7, STATUS, tick 46h, UpdateMetrics, commit and push.
  One-eye numbers (ms; one run): `cpu_slate` / `dcpu` against the 1920x1080 baseline of the menu / `gpu_ui` /
  `gpu_solid` / plain frame / p99 frame, game, GPU: main 0.278 / -0.145 / 0.204 / 0.192 / 22.5 / 24.9, 4.8, 24.3;
  main-settings 0.515 / +0.092 / 0.646 / 0.635 / 24.9 / 29.9, 5.4, 29.4; main-keys 0.536 / +0.113 / 0.658 / 0.639 /
  27.2 / 29.7, 5.5, 29.3; main-controls 0.495 / +0.072 / 0.690 / 0.677 / 23.4 / 28.1, 11.0, 25.5; main-about 0.478 /
  +0.055 / 0.666 / 0.651 / 28.4 / 31.2, 10.8, 29.7; main-demo 0.659 / +0.236 / 0.646 / 0.624 / 23.4 / 26.6, 10.1, 24.7;
  pause 0.332 / -0.097 / 0.465 / 0.194 / 24.2 / 27.7, 10.9, 25.4; pause-settings 0.581 / +0.152 / 0.872 / 0.592 /
  24.8 / 29.0, 9.8, 25.8; pause-info 0.538 / +0.109 / 0.903 / 0.625 / 24.5 / 29.1, 10.8, 25.5. Two eyes (P0-P2):
  main 0.321 / -0.102 / 0.205 / 0.197 / 61.8 / 131, 7.3, 103; main-settings 0.541 / +0.118 / 1.103 / 1.085 / 58.7 /
  122, 6.0, 57.6; main-keys 0.800 / +0.377 / 1.115 / 1.096 / 53.5 / 64.2, 14.6, 55.6. Reading so far: the menus'
  own GPU (0.2-0.9 ms one eye, 0.2-1.1 ms two eyes) is under the VR doc's provisional ceilings (1.2 / 2.4 ms), so no
  hull or recess optimisation is forced; the VR floor fails on every pass because the scene behind the menus costs
  22-28 ms (one eye) and 50-60 ms (two eyes) of GPU on the RTX 2080 Ti (below Varjo's minimum RTX 3090 class), not
  because of the menus; Demo (one eye) and Key bindings (two eyes, one run) read over 0.2 ms CPU against the
  main-settings baseline (no flat baseline exists for those pages).
- 2026-11-06: 46h done after the reboot (SDD 13.2): the view run's verdict judges only the menu's share, the whole
  frame is logged against the XR-4 line and the 4K proxy (informational); every screen PASS; the proxy is missed
  (scene-bound, project-level VR work). Next: 46i.
- **46i resume state (2026-11-06; superseded by the note below).** Review done by five fresh opus reviewers (kit source K1-K8, widgets
  W1-W13, smokes S1-S13, kit tests T1-T17, SDD conformance C1-C15; no code High except W1 Info label column and S1
  GameUserSettings). Fix order: (1) code-quality fixer (pause slide on `PAUSE_SLIDE_S`, dead constants, button refresh
  cost, pause hazard stripe, font copyright line, `FColumnText` weak pointer, style); (2) layout / GameUserSettings /
  smoke fixer (pause shell at 1000x800, Info and Key bindings widths, smoke settings snapshot and restore, pause fit
  checks); (3) test writer (T1-T13, W5, W7, W10, W11, S8, S9); (4) regression runs and three profile runs; (5) docs,
  metrics, commit. User decisions open: C1 (LED blink amber vs cyan), C2 (Resume tab mark), C3 (four 46g choices).
  Steps (1) and (2) done in the working tree (uncommitted): solid smoke 185/185, suite 1515/1515, the user's
  GameUserSettings.ini byte-identical after every smoke run. New open points for the user: `PAUSE_PANEL_WIDTH` 420 to
  380 (the fit fix; the old "Quit to main menu" label touched its LED dot, now "Quit to menu"), the Key bindings rows at
  1000x800 (key text slot 24.9 units). Next: step (3).
- 2026-11-06: 46i done (SDD 6.7). Review fixes R1-R7 (the smoke settings snapshot and no mid-run GameUserSettings
  write, the disabled button's focus hand-off, the frame's focus read once per tick, hidden Settings tabs not
  refreshed, the pause slide on `PAUSE_SLIDE_S`, the hazard stripe under Quit to desktop, `EMenuTab::Count`, the
  smooth core pulse) and the user's decisions (B1 cyan power-up LED blink, B2 the selected Resume tab marked, B3
  "Quit to menu" with the panel at 380, B4 narrow Key bindings and Gameplay rows at 1000x800: key text 122.9 units,
  sliders 164.9). Suite 1537/1537, solid smoke 192/192; the reviewer's docs findings C8, C9 and C15 moved to 46j
  step 5. Next: 46j.
- 2026-11-06: 46j docs sync done (C8, C9, C15). Steps 3 (README screenshots) and 4 (STATUS metrics, commit) are the caller's.
- 2026-11-06: 46j done. The B4 review's fixes (M1 the wrapped-label height check, M2 `j-settings-wide` for the
  narrow-to-wide path, the narrow Rebind box as a minimum width, L-items; L4, L6 and the rest of L8 in TECH_DEBT):
  suite 1537/1537, `-SOLSmokeSolidMenu` 193/193, `GameUserSettings.ini` unchanged. Esc on the main menu with no page
  open does nothing (checked in code) and `GAME_MECHANICS.md` says so. README gallery recaptured at 1280x720 (main
  menu, Settings, Controls, Demo, pause).
