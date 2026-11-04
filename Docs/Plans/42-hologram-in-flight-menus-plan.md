<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 42: Hologram in-flight menus

SDD: [`Docs/SDDs/42-hologram-in-flight-menus.md`](../SDDs/42-hologram-in-flight-menus.md). Grill notes:
[`42-hologram-menus-grill-notes.md`](42-hologram-menus-grill-notes.md). This is a living document: tick each step as
it lands. Each part is built by its own subagent with its context kept under ~35%, and each part is small enough to
verify without the user. Commit convention: `Issue #42: Part 42x - <short description>` on `master`, then push.
Run `Tools\UpdateMetrics.bat` before each part commit and include `Docs/PROJECT_METRICS.md` and `README.md`.

**Gate before any code:** the user answers the open questions of SDD section 13. **Every question is answered**
(2026-11-03), so no part is gated on a question any more. A1 / A2
approved (SDD 9.1, D7 / D8), Q1 = (a) cues made now (D9, SDD 7.1), Q2 / Q2b = Settings > **Gameplay** right under the
HUD effects slider (D10 / D11), Q3 = shared code built first in #42 (D12), Q4 = radial, Overlay Menu, speed panel,
cards (D13), Q5 = under 0.1 ms CPU and GPU extra with all menus open, profiling smoke (D14), Q6 = frameless cards,
extended to the speed card and the date-and-anchor card (D15), Q7 = map list menu hologram (D16), Q8 = the embedded
Overlay list stays solid for #46 (D17), Q10 = strength 0 is plain flat panels, fully usable (D18), Q11 = the cursor
follows the new slider (D19), Q9 = option (c), the segmented / stepped broken-line glow frame, with a faint dark tint
behind the panel, for the Overlay Menu and the speed panel (D20, SDD 6.3; samples `overlay-c-tint.png`,
`speed-c-tint.png`, `frames-compare-c-tint.png` in `Docs/Images/hologram-frame-samples/`), Q12 = the cues are
independent of the hologram strength and still play at strength 0 (D21). The map list menu takes the same option (c)
frame, as proposed in the Q9 wording; the user confirms it at the hand check. Starting values at HEAD `4128a05`: `SAVE_VERSION` 10,
`KEY_BINDING_LAYOUT_VERSION` 10. Approved target values: `SAVE_VERSION` **11**, `KEY_BINDING_LAYOUT_VERSION` **10**
(unchanged).

The part order below follows the user's answer to Q4 (D13: radial, Overlay Menu, F3, cards); the map list menu (D16)
was not placed by the user and comes last, in 42g. 42a-42c come first because the shared code is built first (D12).

## Parts

### 42a: shared pure logic (tests opus/high; implementation sonnet/medium)

Q3 (D12: built here first, renderer-free, reused by #46) and Q10 (D18: strength 0 is flat and instant) are answered.
The frame-geometry contracts are final for option (c) (D20, SDD 6.3): `FrameOutline`, `FramePerimeter`,
`FrameDrawSegments`, `FrameGapDots`, `FrameRulerTicks`, `FrameAccents`, `HoverMark`, `TabMarker`, `DashedDivider`,
`BrokenDivider`, plus `RadialArcDrawIn` and `CardCornerTicks`, with the curves, anim state and cue rules.

0. [x] Capture the D14 baseline before any rendering change: the profiling passes of SDD 11 at the current HEAD (the
   menus as today), numbers into the SDD amendment. (The `-SOLSmokeHoloMenuProfile` harness is built here as a
   stand-alone smoke flag so the same passes run again in 42i.)
1. [x] Brief a separate opus subagent with SDD sections 6 and 7 only (no implementation). It writes
   `Source/SOLTest/Tests/HoloMenuTest.cpp` (`SOLTest.HoloMenu.*`), the cases of SDD 6.4.
2. [x] Stubs `UI/SOLHoloMenuMath.{h,cpp}` (namespace `SOLHoloMenu`, `FSOLHoloMenuAnim`, `FSOLHoloMenuLook`,
   `FSOLHoloSegment`) and the `SOL::HoloMenu` constants. Build, run `Tools\RunTests.bat -Filter SOLTest.HoloMenu`:
   red for the right reasons.
3. [x] Implement; green. Adversarial review (opus/xhigh, report only); fixes; full suite.
4. [x] Commit `Issue #42: Part 42a - hologram menu animation maths`.

### 42b: persistence, schema 11 (opus, xhigh; tests by a separate opus/high subagent)

**A1 and A2 approved by the user on 2026-11-03.** Verbatim for the brief: A1, bump `SAVE_VERSION` 10 -> 11 for the
new field `HoloMenuEffects` with a 10 -> 11 migration, `KEY_BINDING_LAYOUT_VERSION` stays 10; A2, old saves copy
their current HUD effects value (`HudEffectsStrength`, sanitized), new saves default to 1.0 (1.0 is the
full-strength multiplier of the slider, unrelated to the version number 11). The brief also carries "version and
migrate, never silently discard"; the exact field list of SDD 9.2 (nothing else). The slider's place is answered
(D11: Settings > Gameplay, right under the HUD effects slider).
The subagent reports exactly which constants and fields it changed; the main session checks that report against
SDD section 9 and the user's answers before ticking.

1. [x] Tests first (SDD 9.5): extend `SOLSettingsRulesTest.cpp` and `SOLSettingsModelTest.cpp`, re-validate the pins
   in `FlightModeMathTest.cpp`. Red.
2. [x] Fields: `USOLSettingsSave::HoloMenuEffects`, `FSettingsValues::HoloMenuEffects` (sanitize, equal, dirty),
   `SetValues` / `GetValues`, the `SOL::Settings::HOLO_MENU_EFFECTS_*` constants.
3. [x] The 10 -> 11 step (SDD 9.3), `SAVE_VERSION` 11 and its schema comment.
4. [x] The Settings slider (SDD 8): `ESOLSettingsSlider::HoloMenuEffects`, its `SETTINGS_SLIDERS` row, handler, label in
   `SOLMenuText`, placed in **Settings > Gameplay, right under the "HUD hologram effects" slider** (D11).
5. [x] Suite green; `-SOLMenuSmoke=all` (no new failures beyond its known stale checks); review (opus/xhigh); fixes.
6. [x] Commit `Issue #42: Part 42b - hologram menu effects setting (save schema 11)`.

### 42c: rendering kit (opus, high)

Depends on 42a; 42b for the strength (until 42b lands the subsystem may read `HUD_EFFECTS_DEFAULT`, but 42c is not
committed as done before 42b, so no build ships a menu slider that does nothing).

1. [x] `USOLHoloMenuSubsystem`: the menu MID (strength and view size pushed on change), `PlayCue` hooks (assets from
   `SOL::Paths`, Q1 = a), a cue counter for the smoke; the cues play at every strength, including 0 (D21).
2. [x] `SSOLHoloFrame` + `USOLHoloFrameWidget`: backdrop backings (the faint tint, `BackdropAlpha`), the option (c)
   frame (D20: broken outline draw-in, gap dots, ruler ticks, accents, dividers, tab markers; SDD 6.3), the
   frameless card ticks (`CardCornerTicks`, D15), borderless fill, sweep line, hover brackets (`HoverMark`); the flat strength-0
   look (D18: `FLAT_PANEL_ALPHA` backdrop, `GlowScale` 0, no sweep or flicker); buffers reserved once; one
   custom-vertex draw.
2b. [x] (done in 42b after its review, so the slider does something in the 42b commit) Cursor (D19): `USOLHologramCursorSubsystem` reads `HoloMenuEffects` instead of `HudEffectsStrength`
   (`SOLHologramCursorSubsystem.cpp` line 71); `-SOLMenuSmoke` cursor checks follow.
3. [x] `ASOLFlightHud`: the second `SSOLHudShapeLayer` (menu layer) above the HUD layer with the menu MID, frame
   stamping as the HUD layer; `Menu*` draw helpers. HUD output unchanged (`-SOLSmokeHud` green, screenshot compared).
4. [x] The `-SOLSmokeHoloMenu` scaffold (flag constant, harness, the strength-0 / 2 settings in the smoke slot).
5. [x] Review; fixes; suite. Commit `Issue #42: Part 42c - hologram menu frame and menu layer`.

### 42d: radial menu (opus, high)

1. [x] Draw the radial in the menu layer with `FSOLHoloMenuAnim` (open / close from the cached state, rim arcs via
   `RadialArcDrawIn`, fill and labels by `FillAlpha`, hover glow and flicker on the highlighted sector).
2. [x] Cues: open at the opening press, hover per highlight change (rate limited), select on an enabled action.
3. [x] Smoke steps 5 (and 9 for the radial); `-SOLSmokeInteract` and `-SOLSmokeChase` green; screenshots looked at
   (mid-open, hovered, strength 0 and 2).
4. [x] Review; fixes; suite. Commit `Issue #42: Part 42d - hologram radial menu`.

### 42e: Overlay Menu with profile tabs (opus, high)

1. [x] `USOLOverlayMenuWidget`: replace `PanelBorder`'s dark fill by a `USOLHoloFrameWidget` behind the content; the
   hovered / highlighted row or tab feeds the hover rectangle; content render opacity follows `FillAlpha`; the
   non-blocking close of SDD 4.5 through the pawn's close path. The option (c) frame + tint (D20), with the dashed
   column divider open at the selected tab, the tab markers, the hint divider and `RowPointer` hover brackets. The copy embedded in
   Settings is **not** converted (D17): with `SetEmbedded(true)` no frame widget is added and it keeps today's
   `PanelBorder` look until #46 restyles it with the solid Settings page.
2. [x] Cues: open, hover, select (row toggle, tab switch, the cycle key inside the menu).
3. [x] Smoke steps 1-4; `-SOLSmokeOverlay` green at both sizes; screenshots looked at.
4. [x] Review; fixes; suite (review fixes applied, suite 1323/1323 and smokes green 2026-11-04; commit pending).
   Commit `Issue #42: Part 42e - hologram Overlay Menu`.

### 42f: F3 speed panel (sonnet, medium)

1. [x] Same option (c) frame + tint (D20) with the stepped section divider (`BrokenDivider`), highlight-as-hover
   (digit and unit field: `CentreTicks`; body row: `RowPointer`), open / close, cues (select on each applied cap).
2. [x] Smoke step 6; `-SOLSmokeHud` F3 checks green; screenshot looked at.
3. [x] Review; fixes; suite (review fixes applied, suite 1323/1323 and smokes green 2026-11-04; commit pending).
   Commit `Issue #42: Part 42f - hologram speed panel`.

### 42g: the four cards, then the map list menu (cards sonnet, medium; map list menu opus, high)

Q6 answered (D15: frameless corner ticks + faint tint, extended to the speed card and the date-and-anchor card) and Q7
answered (D16: the map list menu is hologram). The map list menu uses the Overlay Menu's option (c) frame + tint
(D20, as proposed in the Q9 wording; the user confirms it at the hand check).

1. [x] Info card and target card (the top-right `DrawTarget` readout) in the menu layer: corner ticks + tint
   (`CardCornerTicks`, `CARD_TICK_PX`), open / close from the cached object, sweep, jitter; the open cue for the info
   card, none for the target card.
2. [x] Speed card (`DrawSpeedBlock` + the G meter, `mLeftBlockRect`) and date-and-anchor card (`DrawInfoBlock`'s
   panel; the key hints under it stay HUD) moved from `AddHoloPanel` on the HUD layer to the menu layer with the
   same card look; always shown, no open / close or cue; they now follow `HoloMenuEffects` (SDD 4.3). Any
   `-SOLSmokeHud` check that counts HUD-layer panels or backings updated.
3. [x] Smoke step 7 (and the cards' part of step 9); `-SOLSmokeInteract` Info checks and `-SOLSmokeHud` green;
   screenshots looked at (2026-11-04: `-SOLSmokeHoloMenu` 70/70 at 1280x720 and 1000x800, Hud 24/24 at both sizes,
   Interact 18/18, profile P0-P4 PASS; the `-SOLSmokeHud` check `i-glow-text-panels` now counts both layers' painted
   texts and backings and the HUD-layer panels plus the cards).
4. [x] Review; fixes; suite. Commit `Issue #42: Part 42g - hologram cards`. (Review, fixes and suite done
   2026-11-04: the card-content redirect cleared in `BeginShapeFrame`, the content's shapes / texts counted into the
   card stats and checked on the menu layer, content lines at `LinePad(s)` (flat at 0, D18), style fixes, the SDD 11
   cards amendment. Q15 answered (a): the target card stays open on a target change and only swaps its text (smoke
   check `c-target-cycle`). Suite 1323/1323, `-SOLSmokeHoloMenu` 85/85 (86/86 with `c-target-cycle`) at 1280x720
   and 1000x800, Hud 24/24 at both sizes, profile P0-P4 PASS.)
5. [x] Map list menu (`USOLActionListWidget`, opus/high): frame widget (option (c) + tint, D20), hover row
   (`RowPointer`), open / close,
   cues as the radial (select on an enabled action); smoke step 8; map smokes green; screenshot looked at
   (2026-11-04: `-SOLSmokeHoloMenu` 84/84 at 1280x720 and 1000x800, Interact 18/18, Map 16/16, MapPick 22/22 (a first
   run had 5 OS-cursor checks off by 2-5 px, green on the rerun), Route 26/26, profile P0-P4 PASS, suite 1323/1323).
6. [x] Review; fixes; suite. Commit `Issue #42: Part 42g - hologram map list menu`. (Review, fixes and suite done
   2026-11-04, commit pending: the select cue only when the action ran, the layout rebuilt from the new content on a
   reopen during the close, smoke checks `m-list-reopen` (new) and `m-map-close` (first closed frame), the outside
   click in GAME_MECHANICS, the MapPick OS-cursor flake in TECH_DEBT. Interact 18/18, Map 16/16, MapPick 22/22, Route
   26/26, RouteOverlay 25/25, Chase 26/26, Dust 24/24, Input 41/41, Jump 13/14 (known), Overlay 33/34 twice
   (`3-lshift-none`, an icon count 15 vs 16); rerun twice on the same build: `3-lshift-none` passed both times (16 / 16), one run failed `a-cursor` (painted 0 times, focus) and one was 34/34, so a flake of the scene and focus, not 42g (TECH_DEBT).) 42g cards and map list menu committed together.

### 42h: sound cue import (Q1 = a; sonnet, medium)

Done ahead of the code (2026-11-03): the three cues were made with the SoundCraft MCP tool from CC0 Kenney sounds and
committed as sources in `Docs/Art/ui-sounds/` (`S_SOLHoloOpen.wav`, `S_SOLHoloHover.wav`, `S_SOLHoloSelect.wav`, the
three `.scsession` files and `Audio Files/`). Licence check: SDD 7.1 (cleared for the game and the public repo).
Remaining steps (after 42c, which loads the cues):

1. [x] `SOL::HoloMenu::CUES[]` in `SOLConstants.h` (path, volume, minimum interval) with the `/Game/SOL/UI/Audio/`
   paths of SDD 7.1.
2. [x] `Tools/UI/import_holo_menu_sounds.py` (editor Python, `unreal.AssetImportTask`, automated, replace existing):
   import the three WAVs into `/Game/SOL/UI/Audio/`, set the sound class to `SC_SOLSfx`, save. Extend
   `Tools/CreateSOLAudioClasses.py` to assign `/Game/SOL/UI/Audio/S_SOL*` waves to the SFX class too.
3. [x] Run it with the editor open (unreal-mcp) or as a commandlet; check the three assets load (smoke cue counter
   from 42c), and that the SFX slider scales them.
4. [ ] The user listens in game (open, hover, select) and approves or asks for changes; changes are remade from the
   `.scsession` files and re-imported.
5. [x] (the user listen round of step 4 is still to come; remade cues would be re-imported and committed then) Review; commit `Issue #42: Part 42h - hologram menu sound cues` (the `.uasset` files and the script).

### 42i: docs, profiling, screenshots and close (sonnet, medium; profiling by opus/high)

1. [x] Profile (SDD 11, D14; done 2026-11-04, SDD 11.2: three runs, all passes PASS, medians dCPU +0.041 to +0.079
   ms and dGPU +0.002 to +0.046 ms): `-SOLSmokeHoloMenuProfile`, every pass with all the menus and cards that can be open
   together; numbers into an SDD 42 amendment next to the 42a baseline; each pass under **0.1 ms extra CPU and
   0.1 ms extra GPU**, or the part is not done.
   Confirm here that no outline cache inside the geometry functions is needed (closed at the 42c review, about 6 us
   per frame for 6 frames). If the GPU pass is tight, the column divider can be drawn as 2 dashed-line shapes instead
   of one quad per dash (a section 4.6 contract change; 42c review note).
2. [x] `Docs/GAME_MECHANICS.md`, the in-game Controls / Info / About texts, `Docs/ARCHITECTURE.md` (SDD 10). The
   GAME_MECHANICS / info-screen audit by a sonnet/medium subagent (2026-11-04: tab hover does not switch the profile,
   the tint alpha curve, the Info renew rule, the cursor row, a `SOL::HoloMenu` tunables table; in-game texts for Q14,
   Q15, the map list close, the HUD vs hologram menu sliders, cues at strength 0).
3. [x] README: 3 screenshots (`42-radial-hologram.png`, `42-overlay-menu-hologram.png`, `42-hologram-cards.png`) and
   a features line.
4. [x] Full suite 1323/1323; `-SOLSmokeHoloMenu` 86/86 at 1280x720 and 1000x800; regression smokes (Hud, Interact,
   Overlay, Map, MapPick, Chase green; Jump 13/14 is the known `g3-orientation-rebase`). No PIE smoke: the editor was
   not open (unreal-mcp unavailable); the standalone smokes' screenshots were looked at instead.
5. [x] Final adversarial review of the whole ticket (opus/xhigh): no code blockers; the D14 gate re-measured (three
   runs, SDD 11.2); the SDD `BACKDROP_TINT` row and two SDD wordings fixed; unused `MenuLine` removed; style nits
   (line lengths, one-line getter headers, smoke class comment); the profile's single-run verdict in TECH_DEBT. Q16
   (text glow at strength 0) raised for the user.
6. [x] `STATUS.md`: #42 to Done (after Q13 and Q16 are answered); `graphify update .`; metrics; commit
   `Issue #42: Part 42i - docs, profile and screenshots`; push. (Committed and pushed 2026-11-04 with #42 left open:
   Q13, Q16, the hand check and the cue listen round remain.)
7. [x] Q13 (a, scale 0) and Q16 (a, no change) answered 2026-11-04 and applied; samples deleted. Was: Q13: the user picks from the in-game samples in `Docs/Images/hologram-radial-glow-samples/`; set
   `RADIAL_DISABLED_HOVER_GLOW_SCALE` (a 0, b about 0.22, c 1), update the `HoloRadialTest` and smoke expectations and
   GAME_MECHANICS; Q16 likewise.

## Model and effort per part

| Part | Work | Model | Effort |
|---|---|---|---|
| 42a | Test authoring (spec only) | opus | high |
| 42a | Pure logic implementation | sonnet | medium |
| 42b | Persistence and schema (incl. tests by a separate opus/high agent) | opus | xhigh |
| 42c | Renderer kit, subsystem, HUD second layer | opus | high |
| 42d | Radial conversion | opus | high |
| 42e | Overlay Menu conversion | opus | high |
| 42f | Speed panel conversion | sonnet | medium |
| 42g | The four cards | sonnet | medium |
| 42g | Map list menu (D16) | opus | high |
| 42h | Sound cue import (Q1 = a) | sonnet | medium |
| 42i | Docs sync and mechanics audit | sonnet | medium |
| 42i | Profiling | opus | high |
| every part | Adversarial performance / style review (report only) | opus | xhigh |

## Progress notes

- 2026-11-03: SDD and plan drafted from the resolved grill-me decisions (scope, renderer reuse, backdrop, effects,
  input, separate slider). Waiting on Q1-Q11 and A1 / A2.
- 2026-11-03: user answers recorded: A1 / A2 approved (schema 11, migrated saves copy the HUD effects value, new
  saves 1.0), Q1 = (a), Q2 = Settings > Graphics under the HUD effects slider (opened Q2b: that slider is in Gameplay
  today). The three cue WAVs were made from CC0 Kenney sounds (SDD 7.1). For Q6, two card style mock-ups and a
  side-by-side are in `Docs/Images/hologram-card-samples/` (frameless corner ticks vs a full glowing outline frame).
  Still open: Q2b, Q3, Q4, Q5, Q6, Q7-Q11. Next step: the user answers them; then 42a starts.
- 2026-11-03: more answers recorded (SDD D11-D19): Q2b Gameplay under the HUD effects slider; Q3 shared code built
  first in #42 for #46; Q4 radial, Overlay Menu, speed panel, cards; Q5 under 0.1 ms CPU / GPU extra with all menus
  open, measured by a profiling smoke; Q6 frameless cards, extended to the speed card and the date-and-anchor card
  (checked in code: `DrawSpeedBlock` + G meter and `DrawInfoBlock`'s panel); Q7 map list menu hologram; Q8 embedded
  Overlay list stays solid for #46; Q10 strength 0 = plain flat panels, fully usable; Q11 cursor follows the new
  slider. For Q9 the user asked for examples: three frame shapes each for the Overlay Menu and the speed panel are in
  `Docs/Images/hologram-frame-samples/` (`overlay-a/b/c.png`, `speed-a/b/c.png`, `frames-compare.png`; Pillow
  mock-ups over `43-dust-slow.png`). Opened Q12 (cues at strength 0). Next step: the user answers Q9 and Q12; 42a
  can start on everything except the frame-geometry contracts once Q12 is answered.
- 2026-11-03: the last two answers recorded, so every question is answered: Q9 = option (c) with a faint dark tint
  (D20; samples `overlay-c-tint.png`, `speed-c-tint.png`, `frames-compare-c-tint.png`), the map list menu takes the
  same frame as proposed in the question (to be confirmed by the user at the hand check); Q12 = cues independent of
  the hologram strength, still played at strength 0 (D21). SDD 6.3 finalised for option (c): the stepped outline,
  perimeter, two-stroke broken draw-in from the title corner, gap dots, ruler ticks, accents, hover marks, tab markers
  and dividers, with fixed-capacity outputs; the 6.4 test cases and the section 7 constants updated to match
  (`FRAME_MAX_SEGMENTS` 24, `FRAME_MAX_SHAPES` 160). Next step: 42a (baseline capture, then the test brief from SDD
  sections 6 and 7).
- 2026-11-04: 42b done: `SAVE_VERSION` 10 -> 11, new field `HoloMenuEffects` (`USOLSettingsSave`, `FSettingsValues`),
  constants `HOLO_MENU_EFFECTS_MIN/MAX/DEFAULT/STEP` (0 / 2 / 1 / 0.05), the 10 -> 11 step copies the clamped
  `HudEffectsStrength` (NaN gives 1), `KEY_BINDING_LAYOUT_VERSION` stays 10. Slider "Hologram menu effects" under "HUD
  hologram effects" in Settings > Gameplay. The review asked for the cursor switch (42c step 2b, D19) to move into 42b so
  the slider is not inert; done. Suite 1243 / 1243; `-SOLMenuSmoke=all` 44/50 (the 6 failures are the stale
  hidden-cursor and Mars thrust checks in TECH_DEBT; the 3 Play checks fail only with a focused window);
  `-SOLSmokeOverlay` 34/34. 42a tests authored (25 `SOLTest.HoloMenu.*` tests); next step: 42a implementation.
- 2026-11-04: 42a done: `UI/SOLHoloMenuMath` (curves, anim state, the option (c) frame geometry, card ticks, hover
  marks, tab markers, dividers), 29 `SOLTest.HoloMenu.*` tests (25 by the separate author, 4 added from the review:
  sweep across the HUD time wrap, large app time, effective scale, extreme inputs). Review fixes: strength clamp from
  the settings range, unity-safe file-local prefixes, `MIN_PIECE_PX` and `UI_SCALE_MIN/MAX` constants, NaN time guard.
  Step 0: `-SOLSmokeHoloMenuProfile` built and reviewed (blocker fixed: pinned sim date JD 2461348.5 and a named body,
  Neptune); baseline in SDD 11.1 and in the smoke's constants (third run all PASS). Suite 1272 / 1272. Notes for 42c
  from the review: scale the hover intensity by `FillAlpha`; [x] cache the outline while Open and the rect is unchanged
  (closed in the 42c review: `SSOLHoloFrame` keeps its built frame while Open and only moves the sweep; a further
  outline cache inside the geometry functions is not needed at this cost, about 6 us per frame for 6 frames, confirm in
  42i step 1);
  keep the pawn's `GetLastInfoCardLineCount` / `GetLastRadialSectorCount` current (the profile smoke reads them).
  Next step: 42h import (cue assets), then 42c.
- 2026-11-04 (resume state): 42h steps 1-3 done, uncommitted (CUES[] in `SOL::HoloMenu`, `Tools/UI/import_holo_menu_sounds.py`,
  `Tools/CreateSOLAudioClasses.py`, three `.uasset` cues in `Content/SOL/UI/Audio/`; volumes 0.8 / 0.5 / 0.8 chosen
  by the importer, to retune in the user's listen round); they get reviewed and committed with 42c. SDD 4.6 (42c
  testable contracts: `SOLHoloFrameBuild`, `FSOLHoloMenuParamCache`, `FSOLHoloCueGate`) written; its tests
  (`Tests/HoloMenuKitTest.cpp`) are being authored by a separate agent. Next: 42c-1 (builders + subsystem, TDD), then
  42c-2 (SSOLHoloFrame, USOLHoloFrameWidget, HUD menu layer, `-SOLSmokeHoloMenu` scaffold), review, commit 42c + 42h.
- 2026-11-04: 42c and 42h steps 1-3 and 5 done. 42c split in two: 42c-1 (SDD 4.6 contracts written first, 19
  `SOLTest.HoloMenuKit.*` tests by a separate author, then `SOLHoloFrameBuild` and `USOLHoloMenuSubsystem`) and 42c-2
  (`SSOLHoloFrame`, `USOLHoloFrameWidget`, the HUD menu layer `mMenuLayer` with `Menu*` helpers incl. `MenuText`, the
  `-SOLSmokeHoloMenu` scaffold). Two reviews; fixes incl. one viewport-size source per frame, menu text above its
  backing, title-step pad, volatile UMG frame, dark `BACKDROP_TINT` to match the approved sample, an idempotent cue
  import (MD5 tag). Suite 1292 / 1292; `-SOLSmokeHoloMenu` 18/18, `-SOLSmokeHud` 24/24 at 1280x720 and 1000x800,
  `-SOLSmokeOverlay` 34/34, `-SOLSmokeInteract` 18/18, profile P0-P4 PASS. Next step: 42d (radial).
- 2026-11-04: 42d done: the radial draws on the menu layer (`UI/SOLHoloRadial`), closes from a cached state, cues via
  the subsystem; review fixes (radial update before the HUD's early returns so the Jump To select cue is not late,
  hover glow kept through the close, new opens snap to Closed, select counted only when `Execute` succeeds). Suite
  1305 / 1305; `-SOLSmokeHoloMenu` 30/30, Interact 18/18, Chase 26/26, Hud 24/24, Jump 14/14, profile P1 PASS
  (+0.018 ms GPU). Open user question Q13 (disabled-sector hover glow), behaviour unchanged behind
  `RADIAL_DISABLED_HOVER_GLOW_SCALE` until answered. Next step: 42e + 42f (Overlay Menu and speed panel frames).
- 2026-11-04: 42e and 42f done (one commit): the Ctrl+O Overlay Menu and the F3 speed panel use the option (c) frame
  with the faint dark tint, non-blocking close, cues; pure layout helpers `MakeOverlayLayout`, `TextItemRect`,
  `MakeSpeedLayout` with SDD 6.3 contracts and tests rewritten by a separate author (cases 19-29). Review fixes incl.
  the F3 hover mark after a list scroll, tab hover re-hit-test, teardown reset (`Dismiss` / `ResetClosed`), no hover
  cue on open at strength 0. Suite 1323 / 1323; `-SOLSmokeHoloMenu` 58/58 at both sizes, Overlay 34/34, Hud 24/24,
  MenuSmoke 47/50 (known), profile P2 +0.043 / P3 +0.032 ms GPU. Open user questions: Q13, Q14. Next step: 42g.
- 2026-11-04: 42g committed (`d96c36a`) with Q14 (keep the resting-mouse filter) and Q15 (target card stays open on a
  target change) applied; the Overlay smoke's `3-lshift-none` failure was a scene flake (passed on reruns). Q13 samples
  captured in game (`Docs/Images/hologram-radial-glow-samples/`, a temporary build override, not committed). 42i: final
  profile (SDD 11.2, all PASS on three runs), docs / in-game text audit, README screenshots, final review fixes. Open:
  Q13, Q16, the hand check (`handcheck` list in the session notes) and the cue listen round; #42 stays open.
