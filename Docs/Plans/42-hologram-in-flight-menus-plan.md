<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 42: Hologram in-flight menus

SDD: [`Docs/SDDs/42-hologram-in-flight-menus.md`](../SDDs/42-hologram-in-flight-menus.md). Grill notes:
[`42-hologram-menus-grill-notes.md`](42-hologram-menus-grill-notes.md). This is a living document: tick each step as
it lands. Each part is built by its own subagent with its context kept under ~35%, and each part is small enough to
verify without the user. Commit convention: `Issue #42: Part 42x - <short description>` on `master`, then push.
Run `Tools\UpdateMetrics.bat` before each part commit and include `Docs/PROJECT_METRICS.md` and `README.md`.

**Gate before any code:** the user answers SDD section 13 (Q1-Q11) and approves A1 / A2 (SDD section 9.1). Until
then nothing below starts. Starting values at HEAD `4128a05`: `SAVE_VERSION` 10, `KEY_BINDING_LAYOUT_VERSION` 10.
Target values (if A1 is approved): `SAVE_VERSION` 11, `KEY_BINDING_LAYOUT_VERSION` 10 (unchanged).

The part order below follows the recommended answer to Q4 (radial, Overlay Menu, F3, cards). If the user picks
another order, reorder 42d-42g here; 42a-42c stay first.

## Parts

### 42a: shared pure logic (tests opus/high; implementation sonnet/medium)

Depends on Q3 (where the shared code lives) and Q10 (strength-0 behaviour, which the contract pins).

1. [ ] Brief a separate opus subagent with SDD sections 6 and 7 only (no implementation). It writes
   `Source/SOLTest/Tests/HoloMenuTest.cpp` (`SOLTest.HoloMenu.*`), the cases of SDD 6.4.
2. [ ] Stubs `UI/SOLHoloMenuMath.{h,cpp}` (namespace `SOLHoloMenu`, `FSOLHoloMenuAnim`, `FSOLHoloMenuLook`,
   `FSOLHoloSegment`) and the `SOL::HoloMenu` constants. Build, run `Tools\RunTests.bat -Filter SOLTest.HoloMenu`:
   red for the right reasons.
3. [ ] Implement; green. Adversarial review (opus/xhigh, report only); fixes; full suite.
4. [ ] Commit `Issue #42: Part 42a - hologram menu animation maths`.

### 42b: persistence, schema 11 (opus, xhigh; tests by a separate opus/high subagent)

**Blocked until the user approves A1 (the bump) and A2 (default 1.0, migrated value copy-from-HUD or 1.0).** The
brief carries: the approved A1 / A2 answers verbatim; "version and migrate, never silently discard"; that the version
is bumped only as approved; the exact field list of SDD 9.2 (nothing else); `KEY_BINDING_LAYOUT_VERSION` stays 10.
The subagent reports exactly which constants and fields it changed; the main session checks that report against
SDD section 9 and the user's answers before ticking.

1. [ ] Tests first (SDD 9.5): extend `SOLSettingsRulesTest.cpp` and `SOLSettingsModelTest.cpp`, re-validate the pins
   in `FlightModeMathTest.cpp`. Red.
2. [ ] Fields: `USOLSettingsSave::HoloMenuEffects`, `FSettingsValues::HoloMenuEffects` (sanitize, equal, dirty),
   `SetValues` / `GetValues`, the `SOL::Settings::HOLO_MENU_EFFECTS_*` constants.
3. [ ] The 10 -> 11 step (SDD 9.3), `SAVE_VERSION` 11 and its schema comment.
4. [ ] The Settings slider (SDD 8): `ESOLSettingsSlider::HoloMenuEffects`, its `SETTINGS_SLIDERS` row, handler, label in
   `SOLMenuText`, placed per Q2.
5. [ ] Suite green; `-SOLMenuSmoke=all` (no new failures beyond its known stale checks); review (opus/xhigh); fixes.
6. [ ] Commit `Issue #42: Part 42b - hologram menu effects setting (save schema 11)`.

### 42c: rendering kit (opus, high)

Depends on 42a; 42b for the strength (until 42b lands the subsystem may read `HUD_EFFECTS_DEFAULT`, but 42c is not
committed as done before 42b, so no build ships a menu slider that does nothing).

1. [ ] `USOLHoloMenuSubsystem`: the menu MID (strength and view size pushed on change), `PlayCue` hooks (per Q1:
   assets from `SOL::Paths`, or counted placeholders), a cue counter for the smoke.
2. [ ] `SSOLHoloFrame` + `USOLHoloFrameWidget`: backdrop backings, outline draw-in segments, borderless fill, sweep
   line, hover edge glow; buffers reserved once; one custom-vertex draw.
3. [ ] `ASOLFlightHud`: the second `SSOLHudShapeLayer` (menu layer) above the HUD layer with the menu MID, frame
   stamping as the HUD layer; `Menu*` draw helpers. HUD output unchanged (`-SOLSmokeHud` green, screenshot compared).
4. [ ] The `-SOLSmokeHoloMenu` scaffold (flag constant, harness, the strength-0 / 2 settings in the smoke slot).
5. [ ] Review; fixes; suite. Commit `Issue #42: Part 42c - hologram menu frame and menu layer`.

### 42d: radial menu (opus, high)

1. [ ] Draw the radial in the menu layer with `FSOLHoloMenuAnim` (open / close from the cached state, rim arcs via
   `RadialArcDrawIn`, fill and labels by `FillAlpha`, hover glow and flicker on the highlighted sector).
2. [ ] Cues: open at the opening press, hover per highlight change (rate limited), select on an enabled action.
3. [ ] Smoke steps 5 (and 8 for the radial); `-SOLSmokeInteract` and `-SOLSmokeChase` green; screenshots looked at
   (mid-open, hovered, strength 0 and 2).
4. [ ] Review; fixes; suite. Commit `Issue #42: Part 42d - hologram radial menu`.

### 42e: Overlay Menu with profile tabs (opus, high)

1. [ ] `USOLOverlayMenuWidget`: replace `PanelBorder`'s dark fill by a `USOLHoloFrameWidget` behind the content; the
   hovered / highlighted row or tab feeds the hover rectangle; content render opacity follows `FillAlpha`; the
   non-blocking close of SDD 4.5 through the pawn's close path. The embedded Settings copy per Q8.
2. [ ] Cues: open, hover, select (row toggle, tab switch, the cycle key inside the menu).
3. [ ] Smoke steps 1-4; `-SOLSmokeOverlay` green at both sizes; screenshots looked at.
4. [ ] Review; fixes; suite. Commit `Issue #42: Part 42e - hologram Overlay Menu`.

### 42f: F3 speed panel (sonnet, medium)

1. [ ] Same frame, highlight-as-hover (digit, unit field, body row), open / close, cues (select on each applied cap).
2. [ ] Smoke step 6; `-SOLSmokeHud` F3 checks green; screenshot looked at.
3. [ ] Review; fixes; suite. Commit `Issue #42: Part 42f - hologram speed panel`.

### 42g: info and target cards (sonnet, medium)

Depends on Q6 (which cards get the frame) and Q7 (the map list menu: if included, it gets its own step here, opus
high, since it is a UMG widget with map input).

1. [ ] Info card (and the target card per Q6) in the menu layer: frame, open / close from the cached object, sweep,
   jitter; the open cue for the info card.
2. [ ] Smoke step 7; `-SOLSmokeInteract` Info checks green; screenshots looked at.
3. [ ] Review; fixes; suite. Commit `Issue #42: Part 42g - hologram info and target cards`.

### 42h: sound cue assets (per Q1; sonnet, medium)

Only if Q1 = (a): make the open / hover / select sounds with the `soundcraft` MCP tool, import them under the
content folder named in `SOL::Paths`, route them to the SFX sound class, and let the user listen and approve before
the commit. If Q1 = (b) or (c), this part is skipped and a `Docs/ToDo/` or #9 note records the missing assets.

### 42i: docs, profiling, screenshots and close (sonnet, medium; profiling by opus/high)

1. [ ] Profile (SDD 11): `ProfileGPU` and `stat dumpave` with the radial open and with Ctrl+O open; numbers into an
   SDD 42 amendment; compare with the Q5 budget.
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
| 42g | Cards (map list menu, if included: opus/high) | sonnet | medium |
| 42h | Sound assets (if Q1 = a) | sonnet | medium |
| 42i | Docs sync and mechanics audit | sonnet | medium |
| 42i | Profiling | opus | high |
| every part | Adversarial performance / style review (report only) | opus | xhigh |

## Progress notes

- 2026-11-03: SDD and plan drafted from the resolved grill-me decisions (scope, renderer reuse, backdrop, effects,
  input, separate slider). Waiting on Q1-Q11 and A1 / A2.
