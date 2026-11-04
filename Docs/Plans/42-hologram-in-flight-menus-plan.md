<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 42: Hologram in-flight menus

SDD: [`Docs/SDDs/42-hologram-in-flight-menus.md`](../SDDs/42-hologram-in-flight-menus.md). Grill notes:
[`42-hologram-menus-grill-notes.md`](42-hologram-menus-grill-notes.md). This is a living document: tick each step as
it lands. Each part is built by its own subagent with its context kept under ~35%, and each part is small enough to
verify without the user. Commit convention: `Issue #42: Part 42x - <short description>` on `master`, then push.
Run `Tools\UpdateMetrics.bat` before each part commit and include `Docs/PROJECT_METRICS.md` and `README.md`.

**Gate before any code:** the user answers the open questions of SDD section 13. Answered on 2026-11-03: A1 / A2
approved (SDD 9.1, D7 / D8), Q1 = (a) cues made now (D9, SDD 7.1), Q2 / Q2b = Settings > **Gameplay** right under the
HUD effects slider (D10 / D11), Q3 = shared code built first in #42 (D12), Q4 = radial, Overlay Menu, speed panel,
cards (D13), Q5 = under 0.1 ms CPU and GPU extra with all menus open, profiling smoke (D14), Q6 = frameless cards,
extended to the speed card and the date-and-anchor card (D15), Q7 = map list menu hologram (D16), Q8 = the embedded
Overlay list stays solid for #46 (D17), Q10 = strength 0 is plain flat panels, fully usable (D18), Q11 = the cursor
follows the new slider (D19). **Still open: Q9** (frame shape of the Overlay Menu and speed panel; samples in
`Docs/Images/hologram-frame-samples/`) and **Q12** (cues at strength 0). A part does not start while a question it
depends on is open: Q9 gates the frame-geometry contracts of 42a (SDD 6.3) and the frame drawing of 42c / 42e / 42f /
42g's map list menu; Q12 gates 42c's cue rule only. Starting values at HEAD `4128a05`: `SAVE_VERSION` 10,
`KEY_BINDING_LAYOUT_VERSION` 10. Approved target values: `SAVE_VERSION` **11**, `KEY_BINDING_LAYOUT_VERSION` **10**
(unchanged).

The part order below follows the user's answer to Q4 (D13: radial, Overlay Menu, F3, cards); the map list menu (D16)
was not placed by the user and comes last, in 42g. 42a-42c come first because the shared code is built first (D12).

## Parts

### 42a: shared pure logic (tests opus/high; implementation sonnet/medium)

Q3 (D12: built here first, renderer-free, reused by #46) and Q10 (D18: strength 0 is flat and instant) are answered.
The frame-geometry contracts (SDD 6.3, `FrameOutline` / `FrameDrawSegments`) wait on **Q9**; the rest of the part
(curves, anim state, cue rules, `CardCornerTicks`) does not.

0. [ ] Capture the D14 baseline before any rendering change: the profiling passes of SDD 11 at the current HEAD (the
   menus as today), numbers into the SDD amendment. (The `-SOLSmokeHoloMenuProfile` harness is built here as a
   stand-alone smoke flag so the same passes run again in 42i.)
1. [ ] Brief a separate opus subagent with SDD sections 6 and 7 only (no implementation). It writes
   `Source/SOLTest/Tests/HoloMenuTest.cpp` (`SOLTest.HoloMenu.*`), the cases of SDD 6.4.
2. [ ] Stubs `UI/SOLHoloMenuMath.{h,cpp}` (namespace `SOLHoloMenu`, `FSOLHoloMenuAnim`, `FSOLHoloMenuLook`,
   `FSOLHoloSegment`) and the `SOL::HoloMenu` constants. Build, run `Tools\RunTests.bat -Filter SOLTest.HoloMenu`:
   red for the right reasons.
3. [ ] Implement; green. Adversarial review (opus/xhigh, report only); fixes; full suite.
4. [ ] Commit `Issue #42: Part 42a - hologram menu animation maths`.

### 42b: persistence, schema 11 (opus, xhigh; tests by a separate opus/high subagent)

**A1 and A2 approved by the user on 2026-11-03.** Verbatim for the brief: A1, bump `SAVE_VERSION` 10 -> 11 for the
new field `HoloMenuEffects` with a 10 -> 11 migration, `KEY_BINDING_LAYOUT_VERSION` stays 10; A2, old saves copy
their current HUD effects value (`HudEffectsStrength`, sanitized), new saves default to 1.0 (1.0 is the
full-strength multiplier of the slider, unrelated to the version number 11). The brief also carries "version and
migrate, never silently discard"; the exact field list of SDD 9.2 (nothing else). The slider's place is answered
(D11: Settings > Gameplay, right under the HUD effects slider).
The subagent reports exactly which constants and fields it changed; the main session checks that report against
SDD section 9 and the user's answers before ticking.

1. [ ] Tests first (SDD 9.5): extend `SOLSettingsRulesTest.cpp` and `SOLSettingsModelTest.cpp`, re-validate the pins
   in `FlightModeMathTest.cpp`. Red.
2. [ ] Fields: `USOLSettingsSave::HoloMenuEffects`, `FSettingsValues::HoloMenuEffects` (sanitize, equal, dirty),
   `SetValues` / `GetValues`, the `SOL::Settings::HOLO_MENU_EFFECTS_*` constants.
3. [ ] The 10 -> 11 step (SDD 9.3), `SAVE_VERSION` 11 and its schema comment.
4. [ ] The Settings slider (SDD 8): `ESOLSettingsSlider::HoloMenuEffects`, its `SETTINGS_SLIDERS` row, handler, label in
   `SOLMenuText`, placed in **Settings > Gameplay, right under the "HUD hologram effects" slider** (D11).
5. [ ] Suite green; `-SOLMenuSmoke=all` (no new failures beyond its known stale checks); review (opus/xhigh); fixes.
6. [ ] Commit `Issue #42: Part 42b - hologram menu effects setting (save schema 11)`.

### 42c: rendering kit (opus, high)

Depends on 42a; 42b for the strength (until 42b lands the subsystem may read `HUD_EFFECTS_DEFAULT`, but 42c is not
committed as done before 42b, so no build ships a menu slider that does nothing).

1. [ ] `USOLHoloMenuSubsystem`: the menu MID (strength and view size pushed on change), `PlayCue` hooks (assets from
   `SOL::Paths`, Q1 = a), a cue counter for the smoke; the strength-0 cue rule per Q12.
2. [ ] `SSOLHoloFrame` + `USOLHoloFrameWidget`: backdrop backings, outline draw-in segments (shape per Q9), the
   frameless card ticks (`CardCornerTicks`, D15), borderless fill, sweep line, hover edge glow; the flat strength-0
   look (D18: `FLAT_PANEL_ALPHA` backdrop, `GlowScale` 0, no sweep or flicker); buffers reserved once; one
   custom-vertex draw.
2b. [ ] Cursor (D19): `USOLHologramCursorSubsystem` reads `HoloMenuEffects` instead of `HudEffectsStrength`
   (`SOLHologramCursorSubsystem.cpp` line 71); `-SOLMenuSmoke` cursor checks follow.
3. [ ] `ASOLFlightHud`: the second `SSOLHudShapeLayer` (menu layer) above the HUD layer with the menu MID, frame
   stamping as the HUD layer; `Menu*` draw helpers. HUD output unchanged (`-SOLSmokeHud` green, screenshot compared).
4. [ ] The `-SOLSmokeHoloMenu` scaffold (flag constant, harness, the strength-0 / 2 settings in the smoke slot).
5. [ ] Review; fixes; suite. Commit `Issue #42: Part 42c - hologram menu frame and menu layer`.

### 42d: radial menu (opus, high)

1. [ ] Draw the radial in the menu layer with `FSOLHoloMenuAnim` (open / close from the cached state, rim arcs via
   `RadialArcDrawIn`, fill and labels by `FillAlpha`, hover glow and flicker on the highlighted sector).
2. [ ] Cues: open at the opening press, hover per highlight change (rate limited), select on an enabled action.
3. [ ] Smoke steps 5 (and 9 for the radial); `-SOLSmokeInteract` and `-SOLSmokeChase` green; screenshots looked at
   (mid-open, hovered, strength 0 and 2).
4. [ ] Review; fixes; suite. Commit `Issue #42: Part 42d - hologram radial menu`.

### 42e: Overlay Menu with profile tabs (opus, high)

1. [ ] `USOLOverlayMenuWidget`: replace `PanelBorder`'s dark fill by a `USOLHoloFrameWidget` behind the content; the
   hovered / highlighted row or tab feeds the hover rectangle; content render opacity follows `FillAlpha`; the
   non-blocking close of SDD 4.5 through the pawn's close path. The frame shape per Q9. The copy embedded in
   Settings is **not** converted (D17): with `SetEmbedded(true)` no frame widget is added and it keeps today's
   `PanelBorder` look until #46 restyles it with the solid Settings page.
2. [ ] Cues: open, hover, select (row toggle, tab switch, the cycle key inside the menu).
3. [ ] Smoke steps 1-4; `-SOLSmokeOverlay` green at both sizes; screenshots looked at.
4. [ ] Review; fixes; suite. Commit `Issue #42: Part 42e - hologram Overlay Menu`.

### 42f: F3 speed panel (sonnet, medium)

1. [ ] Same frame (shape per Q9), highlight-as-hover (digit, unit field, body row), open / close, cues (select on each
   applied cap).
2. [ ] Smoke step 6; `-SOLSmokeHud` F3 checks green; screenshot looked at.
3. [ ] Review; fixes; suite. Commit `Issue #42: Part 42f - hologram speed panel`.

### 42g: the four cards, then the map list menu (cards sonnet, medium; map list menu opus, high)

Q6 answered (D15: frameless corner ticks + faint tint, extended to the speed card and the date-and-anchor card) and Q7
answered (D16: the map list menu is hologram). The map list menu's frame shape waits on Q9 (proposed: the Overlay
Menu's answer).

1. [ ] Info card and target card (the top-right `DrawTarget` readout) in the menu layer: corner ticks + tint
   (`CardCornerTicks`, `CARD_TICK_PX`), open / close from the cached object, sweep, jitter; the open cue for the info
   card, none for the target card.
2. [ ] Speed card (`DrawSpeedBlock` + the G meter, `mLeftBlockRect`) and date-and-anchor card (`DrawInfoBlock`'s
   panel; the key hints under it stay HUD) moved from `AddHoloPanel` on the HUD layer to the menu layer with the
   same card look; always shown, no open / close or cue; they now follow `HoloMenuEffects` (SDD 4.3). Any
   `-SOLSmokeHud` check that counts HUD-layer panels or backings updated.
3. [ ] Smoke step 7; `-SOLSmokeInteract` Info checks and `-SOLSmokeHud` green; screenshots looked at.
4. [ ] Review; fixes; suite. Commit `Issue #42: Part 42g - hologram cards`.
5. [ ] Map list menu (`USOLActionListWidget`, opus/high): frame widget (shape per Q9), hover row, open / close,
   cues as the radial (select on an enabled action); smoke step 8; map smokes green; screenshot looked at.
6. [ ] Review; fixes; suite. Commit `Issue #42: Part 42g - hologram map list menu`.

### 42h: sound cue import (Q1 = a; sonnet, medium)

Done ahead of the code (2026-11-03): the three cues were made with the SoundCraft MCP tool from CC0 Kenney sounds and
committed as sources in `Docs/Art/ui-sounds/` (`S_SOLHoloOpen.wav`, `S_SOLHoloHover.wav`, `S_SOLHoloSelect.wav`, the
three `.scsession` files and `Audio Files/`). Licence check: SDD 7.1 (cleared for the game and the public repo).
Remaining steps (after 42c, which loads the cues):

1. [ ] `SOL::HoloMenu::CUES[]` in `SOLConstants.h` (path, volume, minimum interval) with the `/Game/SOL/UI/Audio/`
   paths of SDD 7.1.
2. [ ] `Tools/UI/import_holo_menu_sounds.py` (editor Python, `unreal.AssetImportTask`, automated, replace existing):
   import the three WAVs into `/Game/SOL/UI/Audio/`, set the sound class to `SC_SOLSfx`, save. Extend
   `Tools/CreateSOLAudioClasses.py` to assign `/Game/SOL/UI/Audio/S_SOL*` waves to the SFX class too.
3. [ ] Run it with the editor open (unreal-mcp) or as a commandlet; check the three assets load (smoke cue counter
   from 42c), and that the SFX slider scales them.
4. [ ] The user listens in game (open, hover, select) and approves or asks for changes; changes are remade from the
   `.scsession` files and re-imported.
5. [ ] Review; commit `Issue #42: Part 42h - hologram menu sound cues` (the `.uasset` files and the script).

### 42i: docs, profiling, screenshots and close (sonnet, medium; profiling by opus/high)

1. [ ] Profile (SDD 11, D14): `-SOLSmokeHoloMenuProfile`, every pass with all the menus and cards that can be open
   together; numbers into an SDD 42 amendment next to the 42a baseline; each pass under **0.1 ms extra CPU and
   0.1 ms extra GPU**, or the part is not done.
2. [ ] `Docs/GAME_MECHANICS.md`, the in-game Controls / Info / About texts, `Docs/ARCHITECTURE.md` (SDD 10). The
   GAME_MECHANICS / info-screen audit by a sonnet/medium subagent.
3. [ ] README: 2-3 screenshots into `Docs/Images/` and the gallery (SDD 10).
4. [ ] Full suite; `-SOLSmokeHoloMenu` at 1280x720 and 1000x800; the regression smokes of SDD 12; PIE smoke via
   unreal-mcp if the editor is open (else noted here).
5. [ ] Final adversarial review of the whole ticket (opus/xhigh); fixes; tech debt recorded.
6. [ ] `STATUS.md`: #42 to Done; `graphify update .`; metrics; commit `Issue #42: Part 42i - docs, profile and
   screenshots`; push.

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
