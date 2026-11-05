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

**Gate before any code:** the user answers the open questions of SDD section 15 (Q1-Q17). Each part below names the
questions it waits for; a part whose gate is answered can start even while others are open. No question is answered
yet (2026-11-04).

Every brief carries: the SDD path and the sections the part needs, the STYLE_GUIDE, the banner and comment rules, "asset
paths only in `SOLConstants.h`", the relevant TECH_DEBT items (SDD section 3), the user's answers to the gating
questions verbatim, and "report decisions you hit instead of choosing".

## Parts

### 46a: pure logic, test-first (tests opus/high; implementation sonnet/medium)

Gate: Q2 (strength 0 / 2 rules), Q16 (starting values).

1. [ ] Brief a separate opus subagent with SDD sections 8 and 9 only (no implementation). It writes
   `Source/SOLTest/Tests/SolidMenuTest.cpp` (`SOLTest.SolidMenu.*`, the cases of SDD 8.5).
2. [ ] Stubs `Menu/SOLSolidMenuMath.{h,cpp}` (namespace `SOLSolidMenu`, `FSOLSolidHull`, `ESOLSolidButtonState`,
   `FSOLSolidButtonLook`) and the `SOL::SolidMenu` constants (timings, hull, layout; the palette waits for 46c). Build,
   run `Tools\RunTests.bat -Build -Filter SOLTest.SolidMenu`: red for the right reasons.
3. [ ] Implement; green. Adversarial review (opus/xhigh, report only); fixes; full suite.
4. [ ] Commit `Issue #46: Part 46a - solid menu maths`.

### 46b: assets and import script (sonnet, medium)

Gate: none (the asset list of SDD section 10 does not depend on an open question). Font: Rajdhani (D4).

1. [ ] Textures with the craft MCP tools into `Docs/Art/menu-style/textures/` (SDD 10 table): brushed grain 512 (or
   copy the kept sample tile if the PhotoCraft export fails), rivet, vent strip, hazard mask, noise. PhotoCraft started
   with `--automation-write-root <abs Docs/Art path>`; VectorCraft with absolute export paths. Check each PNG is written
   and seamless where it must tile (offset by half a tile, look for seams).
2. [ ] Rajdhani Regular / Medium / SemiBold / Bold TTFs and `OFL.txt` into `Docs/Art/fonts/rajdhani/` (Google Fonts;
   licence checked: SIL OFL 1.1).
3. [ ] `SOL::Paths` entries for every asset (SDD section 9) and `Tools/UI/import_solid_menu_assets.py` (headless,
   idempotent, MD5 tags, the `import_holo_menu_sounds.py` pattern, paths read from `SOLConstants.h`). Run it twice: the
   second run writes no `.uasset`.
4. [ ] `Docs/Art/menu-style/README.md`: the sources, tool settings and how to regenerate.
5. [ ] Commit `Issue #46: Part 46b - solid menu textures, font and import script` (the `.uasset` files, the PNGs, the
   TTFs, the script, the constants).

### 46c: hull material and kit (opus, high)

Gate: Q1 (which setting), Q2, Q12, Q13, Q14 (palette), Q16, Q17 (budget). Depends on 46a, 46b.

0. [ ] Capture the profiling baseline before any rendering change: build `-SOLSmokeSolidMenuProfile` as a stand-alone
   flag (SDD 13) and run it three times at HEAD (today's look), medians into an SDD amendment.
1. [ ] Verify in the UE 5.8 engine content which outputs `GetUserInterfaceUV` has (the size the SDF needs, SDD 4.3);
   record the answer in an SDD amendment; pick the size path (UV outputs, or the MID size fallback for the big panels).
2. [ ] `Tools/UI/create_solid_menu_materials.py`: `M_SOLSolidHull` (HLSL mirroring `SOLSolidMenu::HullDistance`, tint
   decode mirroring `DecodeButtonTint`) and the style instances; parameter names in `SOL::SolidMenuMaterialParams`.
   Idempotent, `-SOLRebuild`.
3. [ ] `USOLSolidMenuSubsystem` (loads once, one warning per missing asset with a flat fallback, shared MIDs, strength
   pushed on change from the Q1 setting), `USOLSolidPanel`, `SSOLSolidTraceLayer`, `SOLSolidStyle` (slider, checkbox,
   scroll bar, button variants, `MenuFont`), the palette constants (Q12-Q14).
4. [ ] `USOLMenuButton` restyle (variants, `RefreshHighlight` drives `EvaluateButton`, tint, lift, trace request;
   hover still takes focus, rebind listening unchanged) and `SOLMenuContent::MakeText` / `MakePage` on the new font.
   At this point every screen shows the new buttons and font with today's layouts.
5. [ ] The `-SOLSmokeSolidMenu` scaffold (flag constant, harness, strength 0 / 2 in the smoke slot, screenshots).
6. [ ] `-SOLMenuSmoke=all` no new failures; review (opus/xhigh); fixes; suite. Commit
   `Issue #46: Part 46c - solid hull material, panel kit and restyled buttons`.

### 46d: main menu and start-location page (opus, high)

Gate: Q5, Q9, Q10, Q15. Depends on 46c.

1. [ ] `USOLMainMenuWidget`: hull panel column (title step, tagline, stack buttons, hazard stripe under Quit, hint, the
   version / credits recess per Q10), traces and the hovered entry's trace (Q15), the page shell (SDD 6.1) placed per
   Q5 with Back, panel-open / power-up / pulse.
2. [ ] Start-location page in the shell (recess list, solid scroll bar, selected row per Q9).
3. [ ] If Q10 picks `ProjectVersion`: the config value and its reading (project config, not player data).
4. [ ] Smoke steps 1-3 of SDD 14 for the main menu; screenshots looked at; `-SOLMenuSmoke=all` no new failures.
5. [ ] Review; fixes; suite. Commit `Issue #46: Part 46d - solid main menu`.

### 46e: Settings shell with the tab column (opus, high)

Gate: Q8. Depends on 46c (46d for the shell if it was built there; otherwise the shell is built here and 46d reuses
it).

1. [ ] `USOLSettingsPageWidget`: the six sections as six tabs (D13), content recess, `SOLSolidStyle` sliders, toggles
   and steppers, the shared footer, `ScrollToBinding` switching to Key bindings; every handler and smoke setter kept.
2. [ ] The embedded Overlay list (`USOLOverlayMenuWidget`, embedded path only) in the solid font, colours and hover
   look; the standalone Ctrl+O menu untouched (check `-SOLSmokeHoloMenu` and `-SOLSmokeOverlay`).
3. [ ] Smoke step 4; the existing settings checks of `-SOLMenuSmoke` still pass.
4. [ ] Review; fixes; suite. Commit `Issue #46: Part 46e - solid Settings with the tab column`.

### 46f: Controls, Info, About and Demo in the shell (sonnet, medium)

Gate: Q6, Q7. Depends on 46e (the shell with a tab column).

1. [ ] `SOLMenuContent::BuildControlsPage`, `BuildInfoPage`, `BuildAboutPage` in the shell per Q6 (read-only
   recesses, key / value columns, headings); the live-key refresh unchanged.
2. [ ] `USOLDemoPageWidget` in the shell per Q7 (presets, recessed sections, warnings in the Q14 colour, footer).
3. [ ] Smoke step 3 for these pages in both menus; screenshots.
4. [ ] Review; fixes; suite. Commit `Issue #46: Part 46f - solid Controls, Info, About and Demo pages`.

### 46g: pause side panel (opus, high)

Gate: Q3, Q4, Q11. Depends on 46c, 46e, 46f.

1. [ ] `USOLPauseMenuWidget`: the left side panel (PAUSED in the title step, the vertical stack, the hint), the slide-in
   and slide-out (input back at once, SDD 4.6), the dim (and the frozen view per Q3), the pages per Q4, Quit's choices.
2. [ ] If Q3 is (c): the behaviour change of SDD 8 decision 3 (world pause or clock freeze), `PAUSED_BODY`, the
   "sim keeps running while paused" smoke check and `GAME_MECHANICS.md` section 8 in the same part; the brief carries
   the user's exact answer.
3. [ ] Cue wiring per Q11 (all screens, if (a) or (b); (b) adds a SoundCraft asset step and an import like 42h).
4. [ ] Smoke steps 5 and 7; screenshots.
5. [ ] Review; fixes; suite. Commit `Issue #46: Part 46g - solid pause side panel`.

### 46h: smoke completion and profile (opus, high)

Depends on 46d-46g.

1. [ ] `-SOLSmokeSolidMenu` complete (every step of SDD 14 at 1280x720 and 1000x800, strength 0 / 1 / 2).
2. [ ] `-SOLSmokeSolidMenuProfile`: three runs, medians against the 46c baseline, verdicts against the Q17 budget;
   numbers into an SDD amendment (SDD 13).
3. [ ] Regression: `-SOLMenuSmoke=all` (no new failures beyond TECH_DEBT), `-SOLSmokeHoloMenu`, `-SOLSmokeOverlay`;
   PIE smoke with unreal-mcp if the editor is open (screenshots at strength 0, 1, 2).
4. [ ] Fix what fails; suite. Commit `Issue #46: Part 46h - solid menu smoke and profile`.

### 46i: final adversarial review (opus, xhigh, report only; fixes by sonnet or opus by size)

1. [ ] Fresh reviewer over all #46 changes: performance (SDD 13), style guide, the SDD against the code (every decision
   D1-D16 and every answered question), TECH_DEBT items not re-reported.
2. [ ] Fixes; re-check against the SDD; full suite and the smokes of 46h. Leftovers into `Docs/TECH_DEBT.md`.
3. [ ] Commit `Issue #46: Part 46i - final review fixes`.

### 46j: docs sync, in-game text and README screenshots (sonnet, medium)

1. [ ] `GAME_MECHANICS.md` (section 8, 8.1 effects slider row per Q1, the `SOL::SolidMenu` tunables table) and the
   in-game texts in `SOLMenuText.h` (slider description, Back label, version line, `PAUSED_BODY` per Q3), audited
   against the code.
2. [ ] `ARCHITECTURE.md` (new types, smoke flags), `TECH_DEBT.md` (cook paths gain `/Game/SOL/UI/Solid`),
   `Docs/Art/menu-style/README.md` final.
3. [ ] README gallery: `46-main-menu-solid.png`, `46-settings-solid.png`, `46-pause-solid.png` in `Docs/Images/`
   (window screenshots).
4. [ ] `STATUS.md` #46 Done; `Tools\UpdateMetrics.bat`. Commit `Issue #46: Part 46j - docs, in-game text and
   screenshots`.

## Progress notes

- 2026-11-04: SDD and plan drafted from the grill-me decisions (D1-D16). 17 open questions (SDD section 15) wait for
  the user. The temporary layout and hover sample images were deleted once their decisions were recorded (D11-D15).
