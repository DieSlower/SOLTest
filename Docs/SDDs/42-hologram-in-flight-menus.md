<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 42: Hologram in-flight menus

Issue #42. Depends on SDD 21 (HUD glow-line shape renderer, effects slider, panels, glow text), SDD 22 (radial menu,
Overlay Menu, hologram cursor, action table), SDD 34 (Overlay Menu profile tabs), SDD 2 section 6 (F3 speed panel)
and SDD 8 (Settings). Sister ticket: #46 (solid console menus for the full screens; notes in
`Docs/Plans/46-solid-menus-grill-notes.md`), which depends on #42 for the shared hover, cursor and animation code.
Plan: [`Docs/Plans/42-hologram-in-flight-menus-plan.md`](../Plans/42-hologram-in-flight-menus-plan.md).

Status: **draft, not approved.** The grill-me decisions below are resolved. The open questions in section 13 and the
two approval items in section 9.1 (the schema bump and the default value) must be answered by the user before any
part that depends on them starts. Checked against the code at HEAD `4128a05`.

## 1. Goal

Give the menus the player uses while flying the same hologram look as the HUD, with motion. They are drawn as glow
outline frames by the HUD's shape renderer. Each one opens and closes with a short draw-in, glows and flickers on
hover, and shows a faint idle scanline and jitter. Sound cues play on open, hover and select. A new Settings slider
sets how strong these effects are, separately from the HUD's own slider. The flight HUD itself is not changed.

## 2. Resolved decisions (grill-me, the user's answers)

| # | Decision |
|---|---|
| D1 | **Scope: in-flight menus only.** The radial interaction menu (#22), the Ctrl+O Overlay Menu with its profile tabs (#34), the F3 speed panel, and the target / info cards. **The HUD stays as it is** (readouts, radar, icons, reticle, panels, glow). The main menu, Settings, demo page, Controls / Info / About and pause are **not** in scope; they are #46 (solid look). |
| D2 | **Rendering: reuse the HUD's glow-line shape renderer** (`FSOLHudShapeBatch`, `M_SOLHudShape`, the SDD 21 effects pipeline). Panels are **outline frames with no solid fill**. |
| D3 | **Backdrop:** a very faint dark tint behind each panel (no solid fill). Its strength follows the effects control. |
| D4 | **Effects:** (a) **open / close:** the frame lines draw in, then the fill fades up, about 0.2 s; (b) **hover:** an edge glow and a brief flicker on the hovered item, pairing with the #22 hologram cursor; (c) **idle:** a faint scanline sweep and jitter, off at effects 0; (d) **sound cues** for open, hover and select. |
| D5 | **Input unchanged:** mouse and keyboard as today. A keyboard / gamepad navigation redesign is a future idea, not part of #42. |
| D6 | **A separate hologram-menu intensity slider** in Settings, a new saved settings field. This is a schema change (section 9); the bump and the default value still need the user's approval (section 9.1). |

## 3. What exists today (checked in code at HEAD `4128a05`)

- **Renderer.** `SSOLHudShapeLayer` (Slate leaf, `UI/SOLHudShapeLayer.h`) paints, per frame, the panel backings
  (stacked rounded boxes), one `FSlateDrawElement::MakeCustomVerts` with `M_SOLHudShape`, then the glow text.
  `FSOLHudShapeBatch` has lines, arcs, rings, dashed lines, ellipses and borderless panels (kinds 0-4,
  `SOLHudShapeMath`). The HUD owns **one** layer and **one** material instance; `ASOLFlightHud::DrawHUD` pushes the
  `EffectsStrength` scalar (`SOL::HudShapeMaterialParams::EFFECTS_STRENGTH`) only when it changes. That scalar drives
  the row jitter, the panel scanlines and the panel flicker (and the off-by-default grain composite).
- **Settings.** `FSettingsValues::HudEffectsStrength` and `USOLSettingsSave::HudEffectsStrength` (0-2, step 0.05,
  default 1; schema 5), the "HUD hologram effects" slider in the Gameplay section of `USOLSettingsPageWidget`
  (`ESOLSettingsSlider::HudEffects`, `SETTINGS_SLIDERS` table with a `static_assert`).
  `SOL::Settings::SAVE_VERSION` = **10**, `SOL::Settings::KEY_BINDING_LAYOUT_VERSION` = **10** (verified in
  `SOLConstants.h`). `SOLSettingsRules::ClassifySaveVersion` ignores a file newer than the build.
- **Radial menu.** Drawn by `ASOLFlightHud::DrawRadialMenu` into the HUD batch: thin rim arcs per sector, the
  highlighted sector in Secondary and filled, labels, title and the disabled reason. State is in
  `USOLInteractionSubsystem` (`FSOLRadialMenuState`, `FSOLActionList`, value types). It appears and vanishes in one
  frame.
- **Info card.** `ASOLFlightHud::DrawInfoCard`: text lines in a borderless `AddHoloPanel` at the top right, under the
  key hints. **Target readout:** `ASOLFlightHud::DrawTarget`'s top-right text block on a borderless panel (part of
  the HUD today).
- **Overlay Menu.** `USOLOverlayMenuWidget`, C++ UMG: `RootBorder` -> `PanelBorder` (dark translucent fill) -> title,
  5 profile tabs and up to 17 rows of `UTextBlock`, hint. Mouse hover and click via cached geometry; Up / Down,
  Space / Enter, PgUp / PgDn, RShift+O, Esc / Ctrl+O. Embedded, mouse-driven, in the Settings page.
- **F3 speed panel.** `USOLSpeedPanelWidget`, C++ UMG: steppers (digits and unit) and a scrollable body list. Keys and
  the mouse wheel only (no mouse hover or click); the highlight moves by keys.
- **Hologram cursor.** `USOLHologramCursorSubsystem` / `SSOLHologramCursor`: its own `M_SOLHudShape` instance,
  following `HudEffectsStrength`.
- **Audio.** No UI sounds exist. `ASOLCombatAudio` loads `USoundBase` assets from `SOL::COMBAT_SOUNDS` paths; the
  Settings subsystem drives the Master / SFX / Music sound classes (`SOL::Paths::SOUND_CLASS_*`) through a sound mix.
- **Tech debt in these areas** (`Docs/TECH_DEBT.md`, to respect, not re-report): glow text costs two string copies
  per line per frame inside Slate; the panel flicker is computed per pixel; the HUD was not profiled in its worst
  case; the cursor is a fixed 18 px; the radial menu is tied to the Fire binding; the radial title can overlap a
  world label at 1000x800; `-SOLSmokeInteract` / `-SOLSmokeOverlay` warm-up flake; `-SOLMenuSmoke` stale cursor
  checks.

## 4. Architecture

### 4.1 Overview

```
USOLSettingsSubsystem --(HoloMenuEffects)--> USOLHoloMenuSubsystem (world)
                                               |- owns the menu MID of M_SOLHudShape (strength = HoloMenuEffects)
                                               |- plays the cues (open / hover / select) through the SFX class
                                               v
     ASOLFlightHud: HUD layer (unchanged, HUD MID)   +   menu layer (2nd SSOLHudShapeLayer, menu MID, above it)
                                                          radial menu, info card, target card
     USOLOverlayMenuWidget / USOLSpeedPanelWidget: USOLHoloFrameWidget (UMG wrapper of SSOLHoloFrame, menu MID)
                                                          behind the existing UTextBlock content
     All of them run FSOLHoloMenuAnim + SOLHoloMenu pure maths (open / close, hover, idle, backdrop, cue rules)
```

### 4.2 Why a second material instance

The effects strength is a material scalar, not a per-vertex value (every vertex channel is already used, SDD 21
Amendment 1). A separate slider for the menus therefore needs a second material instance, and shapes drawn with it
need their own custom-vertex draw. The HUD's layer and instance stay exactly as they are (D1). Cost: one more Slate
custom-vertex draw while a HUD-drawn menu shows, and one per open UMG frame (section 11).

### 4.3 New types (all in `Source/SOLTest/UI/`, `.h` and `.cpp` side by side, STYLE_GUIDE section 14.2)

| Type | Kind | Responsibility |
|---|---|---|
| `SOLHoloMenu` (`SOLHoloMenuMath.h/.cpp`) | Pure namespace, test-first | Open / close progress, frame draw-in and its segments, fill fade, hover glow and flicker, idle sweep, backdrop alpha, cue rules (section 6) |
| `FSOLHoloMenuAnim` (same file) | Plain struct | One menu's phase (Closed / Opening / Open / Closing), phase start, hovered item and hover start, last cue time |
| `USOLHoloMenuSubsystem` | `UWorldSubsystem` | Owns the menu MID (`UPROPERTY` `TObjectPtr`), pushes `HoloMenuEffects` and the view size into it only when they change, loads the cue sounds once from `SOL::Paths` and plays them 2D in the SFX class (`PlayCue`); a missing asset is silent with one warning |
| `SSOLHoloFrame` | Slate leaf, hit-test invisible | Draws one frame: backdrop tint (backing boxes), outline segments, the borderless panel fill, the idle sweep line and the hover edge glow around the hovered item's rectangle, as one custom-vertex draw with the menu MID; buffers reserved once |
| `USOLHoloFrameWidget` | `UWidget` wrapping `SSOLHoloFrame` | Lets the C++ UMG menus place a frame behind their content and feed it the anim look and the hovered item's rectangle |

The menu layer in the HUD is a second `SSOLHudShapeLayer` instance (no new class): `ASOLFlightHud` gets
`mMenuLayer` (z just above the HUD layer, below the UMG menus), filled in `DrawHUD` by `DrawInteraction` and the
target card, with its material set to the subsystem's menu MID. `ResetFrame` / `MarkBatchFrame` follow the HUD
layer's frame-stamp rule. The HUD keeps `GlowLine` / `AddHoloPanel` for the HUD; a few `Menu*` helpers write to the
menu layer's batch. The `sol.HudRenderer 0` Canvas fallback draws the menus as today (no effects).

### 4.4 Time source

Animations use real application time (`FPlatformTime::Seconds()`, read once per frame or paint), not world time, so
they run while the game is paused or time-warped and never depend on the frame rate. Every pure function takes
`nowS` as an argument (no clock inside, so tests are deterministic).

### 4.5 Close without blocking input

Closing gives input back at once, as today. The visual close then plays for `CLOSE_S`:
- **UMG menus:** the pawn's close path calls `BeginClose` instead of `RemoveFromParent`. The widget becomes
  hit-test invisible and unfocusable, keeps drawing its frame in reverse, and removes itself when the anim reaches
  Closed (from its `NativeTick`, which does nothing while the menu is open and idle). Reopening during the close
  reverses from the current progress (section 6.2).
- **HUD-drawn menus (radial, info, target):** the HUD keeps a copy of the last drawn state (radial state and its
  action list, the info / target object; value types, no allocation) and draws the closing animation from it.

## 5. Per-menu behaviour

| Menu | Open / close | Hover | Idle | Cues |
|---|---|---|---|---|
| **Radial** (HUD menu layer) | The rim arcs grow from each sector's middle to its full aperture (`FrameFraction`), then the highlighted sector's fill and the labels, title and reason fade up (`FillAlpha`). Close plays it backwards from the last drawn state | Highlighted sector change = hover: its rim arcs take `HoverGlow` (edge glow + flicker) on top of today's highlight | Jitter through the menu MID; no sweep (no rectangle to sweep) | Open at the press that opens it; hover on each sector change; select when an **enabled** action is confirmed (none on cancel or a disabled row) |
| **Overlay Menu with tabs** (UMG + `USOLHoloFrameWidget`) | Frame draw-in around the panel, then backdrop, fill and the text (the content box's render opacity) fade up | The highlighted row or tab (mouse or keys, today's highlight) gets an edge-glow frame around its rectangle with the flicker | Sweep across the panel + jitter | Open at Ctrl+O; hover on each highlight change; select on a row toggle or tab switch |
| **F3 speed panel** (UMG + frame) | As the Overlay Menu | The highlighted digit, unit field or body row (keys and wheel move it; no mouse, D5) | Sweep + jitter | Open at F3; hover on each highlight move; select on each applied cap (digit step, list Enter) |
| **Info card** (HUD menu layer) | Opens when Info is chosen, closes when the card goes away | None (no input) | Sweep + jitter | Open only |
| **Target card** (HUD menu layer; scope in Q6) | Opens when a target is selected, closes when it is cleared | None | Sweep + jitter | None (proposed: a target change is frequent, Q6) |

All frames use the HUD palette (`SOL::HudPalette::PRIMARY` lines, `SECONDARY` for the hovered / highlighted item,
dim rows as today). Text stays the existing glow text (HUD layer path for HUD-drawn menus) or the UMG `UTextBlock`s
in the HUD palette.

## 6. Pure logic contracts (`UI/SOLHoloMenuMath`, test-first)

`s` below is the effects strength `clamp(HoloMenuEffects, 0, 2)` (NaN reads as 0). Constants are in section 7.
These contracts are written for a separate test author (no implementation shown to them).

### 6.1 Curves

| Function | Contract |
|---|---|
| `float OpenProgress(double elapsedS, float durationS, float s)` | `s <= 0`: 1 (instant, pending Q10). Else `clamp(elapsedS / durationS, 0, 1)`; `durationS <= 0` gives 1; negative elapsed gives 0 |
| `float FrameDrawIn(float progress)` | `SmoothStep(0, LINE_PHASE_END, progress)`; 0 at 0, 1 from `LINE_PHASE_END` on, monotonic |
| `float FillFade(float progress)` | `SmoothStep(FILL_PHASE_START, 1, progress)`; 0 up to `FILL_PHASE_START`, 1 at 1, monotonic. With `FILL_PHASE_START < LINE_PHASE_END` the two overlap a little |
| `float HoverGlow(double sinceHoverS, float s)` | 0 for `sinceHoverS < 0`. Ramp `r = SmoothStep(0, HOVER_RAMP_S, since)`. Flicker `f = 1 - HOVER_FLICKER_DEPTH * s * SmoothNoise(since * HOVER_FLICKER_RATE_HZ + HOVER_FLICKER_OFFSET)` while `since < HOVER_FLICKER_S`, else 1. Returns `HOVER_GLOW_INTENSITY * r * f`, always in `[0, HOVER_GLOW_INTENSITY]`; with `s = 0` it is `HOVER_GLOW_INTENSITY * r` (glow kept, no flicker); from `HOVER_RAMP_S` and `HOVER_FLICKER_S` on it is exactly `HOVER_GLOW_INTENSITY` |
| `float SweepFraction(double ageS)` | `frac(fmod(ageS, HUD_TIME_WRAP_S) / IDLE_SWEEP_PERIOD_S)` in `[0, 1)`: 0 at the top of the frame |
| `float SweepIntensity(float s)` | `IDLE_SWEEP_INTENSITY * s`; 0 at `s = 0` (D4c) |
| `float BackdropAlpha(float s, float fillAlpha)` | `BACKDROP_ALPHA_PER_UNIT * s * clamp(fillAlpha, 0, 1)`; 0 at `s = 0` (pending Q10), at most `2 * BACKDROP_ALPHA_PER_UNIT` |

`SmoothNoise` and `HUD_TIME_WRAP_S` are SDD 21's (`SOLHudShapeMath`), reused.

### 6.2 Anim state (`FSOLHoloMenuAnim`)

```
enum class ESOLHoloMenuPhase : uint8 { Closed, Opening, Open, Closing };
struct FSOLHoloMenuAnim
{
    ESOLHoloMenuPhase Phase = ESOLHoloMenuPhase::Closed;
    double PhaseStartS = 0.0;          // When the current phase started
    float PhaseStartProgress = 0.0f;   // Progress at that moment (a reversal starts mid-way)
    int32 HoveredItem = INDEX_NONE;
    double HoverStartS = 0.0;
    double LastHoverCueS = -1.0e9;
};
```

| Function | Contract |
|---|---|
| `float Progress(const FSOLHoloMenuAnim&, double nowS, float s)` | Closed 0, Open 1. Opening: `PhaseStartProgress` plus the elapsed fraction of `OPEN_S`, clamped to 1. Closing: `PhaseStartProgress` minus the elapsed fraction of `CLOSE_S`, clamped to 0. With `s <= 0`: Opening reads 1 and Closing 0 (pending Q10) |
| `bool BeginOpen(FSOLHoloMenuAnim&, double nowS, float s)` | Closed -> Opening from 0; Closing -> Opening from the current progress; Opening / Open unchanged. Returns true when it left Closed or Closing (the open cue). Clears the hover |
| `bool BeginClose(FSOLHoloMenuAnim&, double nowS, float s)` | Opening / Open -> Closing from the current progress; Closed / Closing unchanged. Returns true when it started a close. Clears the hover |
| `void Advance(FSOLHoloMenuAnim&, double nowS, float s)` | Opening at progress 1 -> Open; Closing at progress 0 -> Closed. Idempotent; never skips a state backwards |
| `bool SetHovered(FSOLHoloMenuAnim&, int32 item, double nowS)` | Same item: false, nothing changes. A different item: `HoveredItem = item`, `HoverStartS = nowS`; returns true only when `item != INDEX_NONE` |
| `bool ShouldPlayHoverCue(FSOLHoloMenuAnim&, bool bHoverChanged, double nowS)` | True only when `bHoverChanged`, the phase is Open (no cue for the highlight a menu opens with) and `nowS - LastHoverCueS >= HOVER_CUE_MIN_INTERVAL_S`; when true it sets `LastHoverCueS = nowS` (a fast sweep across rows does not machine-gun) |
| `FSOLHoloMenuLook Evaluate(const FSOLHoloMenuAnim&, double nowS, float s)` | `{ bVisible = Phase != Closed, Progress, FrameFraction = FrameDrawIn(p), FillAlpha = FillFade(p), HoverIntensity = HoveredItem != INDEX_NONE ? HoverGlow(nowS - HoverStartS, s) : 0, SweepFraction, SweepIntensity * FillAlpha, BackdropAlpha(s, FillAlpha) }` |

The select cue is fired by the caller when an enabled item is confirmed; it needs no rule.

### 6.3 Frame geometry

| Function | Contract |
|---|---|
| `int32 FrameOutline(const FBox2f& rect, float chamferPx, FVector2f outPoints[8])` | The closed outline clockwise from the top edge's left end: a rectangle with its four corners cut by `chamferPx` (clamped to `[0, min(w, h) / 2]`; 0 gives the 4 corners, 4 points). Returns the point count (8, 4, or 0 for an invalid or empty rect) |
| `float FramePerimeter(const FBox2f& rect, float chamferPx)` | Length of that outline |
| `int32 FrameDrawSegments(const FBox2f& rect, float chamferPx, float fraction, FSOLHoloSegment outSegments[FRAME_MAX_SEGMENTS])` | The drawn part of the outline at a draw-in fraction: two strokes start at the top edge's midpoint and run in opposite directions, each covering `fraction` of half the perimeter, meeting at the bottom edge's midpoint at 1. Returns the segment count: 0 at `fraction <= 0` or an invalid rect; the summed length is `fraction * perimeter` (within 1e-3 px); segments are contiguous along each stroke; the result is mirror-symmetric about the rect's vertical centre line; `fraction` is clamped to `[0, 1]`; at most `FRAME_MAX_SEGMENTS` (18) |
| `void RadialArcDrawIn(float halfApertureRad, float frameFraction, float& outHalfApertureRad)` | A radial sector's rim arc grows from its middle: `halfApertureRad * clamp(frameFraction, 0, 1)` |

### 6.4 Test cases (for the separate test author; `Tests/HoloMenuTest.cpp`, `SOLTest.HoloMenu.*`)

1. `OpenProgress`: 0 / half / full / beyond the duration; negative elapsed; zero duration; `s = 0` instant.
2. `FrameDrawIn` and `FillFade`: end values, the phase thresholds, monotonic over 101 samples, outputs in `[0, 1]`.
3. `HoverGlow`: negative time 0; ramp end; exact intensity after both windows; `s = 0` has no flicker; bounds over a
   sample sweep at `s` = 0, 1, 2.
4. `SweepFraction` wraps into `[0, 1)` (including large and negative ages); `SweepIntensity(0)` is 0.
5. `BackdropAlpha`: 0 at `s = 0` or fill 0; linear in `s`; clamps `fillAlpha`.
6. Anim: open -> Open after `OPEN_S`; close -> Closed after `CLOSE_S`; reopen during a close resumes from the current
   progress (no jump); close during an open likewise; `BeginOpen` twice returns true once; `Advance` idempotent;
   `s = 0` reaches Open / Closed on the next `Advance`; hover cleared by open and close.
7. `SetHovered` and `ShouldPlayHoverCue`: same item no cue; new item cue; INDEX_NONE no cue; no cue while Opening;
   rate limit at `HOVER_CUE_MIN_INTERVAL_S`.
8. `FrameOutline` / `FramePerimeter`: chamfer 0 (4 points, `2(w + h)`), normal chamfer (8 points), chamfer clamped,
   invalid rect 0.
9. `FrameDrawSegments`: 0 / 0.5 / 1 lengths, symmetry, contiguity, start at the top midpoint, capacity never exceeded,
   NaN fraction treated as 0.
10. `RadialArcDrawIn`: 0, 1, clamping.

## 7. Constants (`SOL::HoloMenu` in `SOLConstants.h`; proposed, tuned from screenshots)

| Constant | Proposed | Meaning |
|---|---|---|
| `OPEN_S` / `CLOSE_S` | 0.2 / 0.2 | Open and close times (D4a) |
| `LINE_PHASE_END` | 0.6 | Frame lines fully drawn at this progress |
| `FILL_PHASE_START` | 0.4 | Fill, backdrop and text start fading up here |
| `HOVER_RAMP_S` | 0.06 | Hover glow rise time |
| `HOVER_FLICKER_S` | 0.15 | Length of the hover flicker |
| `HOVER_FLICKER_DEPTH` | 0.25 | Flicker depth per unit of strength (0.5 at strength 2) |
| `HOVER_FLICKER_RATE_HZ` / `HOVER_FLICKER_OFFSET` | 30 / 17 | Flicker noise speed and lattice offset |
| `HOVER_GLOW_INTENSITY` | 0.9 | Edge glow of the hovered item |
| `FRAME_INTENSITY` | 0.5 | Outline frame glow (not scaled by the slider) |
| `FRAME_HALF_WIDTH_PX` | 0.5 | Outline line half width (a 1 px core, x UI scale) |
| `FRAME_CHAMFER_PX` | 6 | Corner cut (x UI scale; pending Q9) |
| `FRAME_MAX_SEGMENTS` | 18 | Capacity of `FrameDrawSegments` (8 sides split by the 2 stroke starts, plus margin) |
| `IDLE_SWEEP_PERIOD_S` | 4.0 | One scanline pass from top to bottom |
| `IDLE_SWEEP_INTENSITY` | 0.08 | Sweep line glow per unit of strength |
| `BACKDROP_ALPHA_PER_UNIT` | 0.15 | Dark tint alpha per unit of strength (0.30 at 2; the HUD backing is 0.45) |
| `HOVER_CUE_MIN_INTERVAL_S` | 0.05 | Shortest gap between two hover cues |
| `FRAME_MAX_SHAPES` | 128 | Shape capacity of one `SSOLHoloFrame` (reserved once) |

Settings constants (`SOL::Settings`): `HOLO_MENU_EFFECTS_MIN` 0, `HOLO_MENU_EFFECTS_MAX` 2,
`HOLO_MENU_EFFECTS_DEFAULT` 1 (**pending approval A2**), `HOLO_MENU_EFFECTS_STEP` 0.05.
Asset paths for the cues go in `SOL::Paths` (STYLE_GUIDE section 7), once Q1 is answered.

## 8. Settings slider

- Label `SOLMenuText::SETTINGS_HOLO_MENU_EFFECTS` = "Hologram menu effects" (shown as a multiplier, "1.00x", like the
  HUD slider).
- `ESOLSettingsSlider::HoloMenuEffects` with its `SETTINGS_SLIDERS` row (the `static_assert` count grows), a handler
  and `ApplySliderValue` / value-read cases. Placement on the page: **Q2**.
- It is a normal Settings field: Apply / Revert, written at the page's `EndVisit` (no new write point). Live
  preview: the menu MID reads the live values, like the HUD slider does today; no menu is open while Settings shows,
  so the preview is seen on the next open (the user may want a preview frame on the page; not proposed).
- The cursor keeps following the HUD slider (pending Q11).

## 9. Persistence (schema 11, NOT YET APPROVED)

Rule: **version and migrate, never silently discard.** The implementer reports exactly which constants and fields it
changed; the main session checks the report against this section and against the user's answer to A1 / A2 before
the part is ticked.

### 9.1 Approval items (the user must still approve these)

| # | Item | Proposal |
|---|---|---|
| A1 | The schema bump `SAVE_VERSION` 10 -> 11 for the new field | Bump to 11 (a new `UPROPERTY` on a versioned save always gets a step, as schemas 5, 6 and 8 did) |
| A2 | The default value and the migrated value | New saves: **1.0** (the HUD slider's default). A migrated v1-v10 save: **copy its `HudEffectsStrength`** (sanitized), because the radial menu and the info card follow that slider today, so a player who turned the HUD effects down keeps calm menus. The alternative is a fixed 1.0 for every migrated save |

### 9.2 Fields

| Where | Change |
|---|---|
| `USOLSettingsSave` | + `UPROPERTY() float HoloMenuEffects = SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;` with a doc comment ("added in schema 11; an older save is migrated per A2") |
| `USOLSettingsSave::SetValues` / `GetValues` | Copy the field both ways |
| `SOLSettingsModel::FSettingsValues` | + `float HoloMenuEffects = SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT;`, joining `Sanitized` (clamp to `[MIN, MAX]`, NaN -> default, +-inf -> the bounds, as `HudEffectsStrength`), `AreEqual` (within `VALUE_EQUAL_TOLERANCE`) and `IsDirty` |
| `SOL::Settings::SAVE_VERSION` | 10 -> **11** (A1); the schema comment gains "11: the hologram menu effects strength, SDD 42" |
| `SOL::Settings::KEY_BINDING_LAYOUT_VERSION` | **Unchanged at 10.** No binding changes, so no controls-changed notice (`DefaultLayoutChangedSince(10)` stays false); the `SAVE_VERSION >= layout` test invariant holds |
| Anything else | No other field, constant or save layout changes |

### 9.3 Migration step 10 -> 11 (`MigrateToCurrentVersion`)

Runs in memory at load; the file is rewritten at the next normal write point (no extra write at load).
1. `HoloMenuEffects` = per A2: `clamp(HudEffectsStrength, MIN, MAX)` (NaN -> default) **or** the default.
2. Every other field is kept as loaded.
3. `Version = 11`.

A second run is a no-op (the version is then 11). A v1-v9 file runs the earlier steps first (so a v4 file reaches
this step with `HudEffectsStrength` = 1 from the 4 -> 5 step). A file newer than 11 is still ignored by
`ClassifySaveVersion` (unchanged).

### 9.4 Writes

No new write point. The field changes only on the Settings page (Apply), and is written with the rest at `EndVisit`,
in the same async save, only when something differs.

### 9.5 Persistence tests (separate test author)

`SOLSettingsRulesTest.cpp`: 10 -> 11 per A2 (a v10 file with `HudEffectsStrength` 0.3 gives 0.3, or 1.0); NaN and
out-of-range sources sanitized; whole chain from v4 and from v6; idempotence; a current v11 file keeps its value; a
fresh save has the default; `SetValues` / `GetValues` round trip incl. 0. `SOLSettingsModelTest.cpp`: default,
clamping, NaN / inf, `AreEqual` tolerance, dirty and revert. `FlightModeMathTest.cpp`: re-validate the schema and
layout pins (`SAVE_VERSION` 11, layout 10, `DefaultLayoutChangedSince(10)` false, `(9)` true). Every existing
settings test is walked to confirm it still states the current contract.

## 10. Documentation, in-game text and screenshots (same change as the code)

- **`Docs/GAME_MECHANICS.md`:** section 8.1 Settings (new slider row with range, step, default and what it scales);
  section 7a (radial look, open / close, hover, cues); the Ctrl+O and F3 rows of section 1 (frame, effects); a
  tunables table for `SOL::HoloMenu`; the HUD-effects row says it no longer drives the radial menu and info card.
- **In-game Controls / Info / About** (`SOLMenuText` texts shown by `SOLMenuContent`): the Settings description of the
  new slider; the sentence that names the HUD hologram effects slider (`SOLMenuText.h` around line 452) narrowed to
  the HUD; a line that the in-flight menus animate and play cues (truthful to whatever Q1 decides: no claim of
  sounds if the cues are silent placeholders).
- **`Docs/ARCHITECTURE.md`:** the new types (section 4.3) in "Key types per module"; the HUD's second layer in the
  frame-order diagram (menu layer painted above the HUD layer, below the UMG menus); the `-SOLSmokeHoloMenu` flag;
  no module dependency changes (all in `UI/`, which already reads Settings).
- **`Docs/Plans/STATUS.md`:** the #42 row through the parts; Done at the end.
- **README:** 2-3 new screenshots in `Docs/Images/` and the gallery: `42-radial-hologram.png` (radial open, a sector
  hovered), `42-overlay-menu-hologram.png` (Ctrl+O with the frame and tabs), `42-speed-panel-hologram.png` or
  `42-info-card.png`. Window screenshots (HighResShot misses Slate layers; the software cursor is never captured).
- **`Docs/TECH_DEBT.md`:** whatever the review leaves open.

## 11. Performance

- Proposed budget (pending **Q5**), 1280x720, editor build, one menu open: GPU at most +0.05 ms over the #22 radial
  baseline (shape batch 0.052 ms, Slate UI 0.096 ms); game thread at most +0.05 ms in `HUD Time` + `Game UI Paint`;
  **zero heap allocation per frame**; at most `FRAME_MAX_SHAPES` shapes per frame widget; one extra custom-vertex
  draw per visible menu.
- Event-driven: a closed menu costs nothing (the HUD's menu layer draws nothing and is not stamped; a closed UMG
  frame is removed). The UMG `NativeTick` returns at once when the menu is Open and idle; the idle sweep and hover
  glow are evaluated in the frame's paint from `nowS` (a few float ops), no timers.
- The menu MID parameters are pushed only when they change (strength, view size), as the HUD does.
- Measured with `ProfileGPU` (`r.ProfileGPU.ShowUI 1`) and `stat dumpave -num=240`, radial and Ctrl+O each open,
  numbers written into the SDD amendment. No claim without a measurement.

## 12. Verification

- **Unit tests:** section 6.4 (`SOLTest.HoloMenu.*`) and section 9.5, written from the contracts by a separate opus
  subagent, red on stubs, then green. Existing tests re-validated: `HudShapes`, `HudPanels` (unchanged renderer
  maths), `InteractMenus` (radial geometry unchanged), `OverlayModel`, the settings files.
- **New smoke `-SOLSmokeHoloMenu`** (constant in `SOLConstants.h`, listed in ARCHITECTURE), at 1280x720 and 1000x800,
  real injected input, smoke settings slot:
  1. Ctrl+O: the frame widget exists; after `OPEN_S` + a frame the phase is Open and the frame drew its full outline
     (shape count >= outline segments); one open cue requested.
  2. Mid-open capture at about 0.1 s: frame fraction strictly between 0 and 1 (a screenshot for the look).
  3. Hover a row with the mouse, then Down: two hover cues at most, the hover glow drawn around the highlighted row.
  4. Toggle a row: one select cue. Esc: input back to the ship in the same frame; the widget gone after `CLOSE_S`.
  5. Radial: press on an icon, check the draw-in, drag across two sectors (hover cues), release on an enabled action
     (select cue), check the close animation from the cached state.
  6. F3: open, step a digit (select cue), move the highlight (hover cue), close.
  7. Info card: open via Info, then close; target card per Q6.
  8. Strength 0 (set in the smoke slot): open is instant, no sweep or flicker drawn, the backdrop per Q10; strength 2
     screenshot.
  9. Settings: the slider exists in the agreed section, Apply writes once at `EndVisit`.
  10. Cue playback is counted through the subsystem (assets or placeholders per Q1), not heard.
- **Regression smokes:** `-SOLSmokeOverlay`, `-SOLSmokeInteract`, `-SOLSmokeHud` (F3 checks), `-SOLSmokeChase`
  (radial entry), `-SOLMenuSmoke=all` (Settings page; its known stale checks stay as in TECH_DEBT).
- **PIE smoke** with unreal-mcp when the editor is open: screenshots looked at for each menu at strength 0, 1, 2.
- **Adversarial review** per part (opus, xhigh, report only), briefed with this SDD, the STYLE_GUIDE, the performance
  guidelines and the tech-debt items of section 3.

## 13. Open questions (for the user; not decided)

| # | Question | Options (recommendation first, not a decision) |
|---|---|---|
| Q1 | **Sound cue assets** for open / hover / select | (a) create them now with the `soundcraft` MCP tool (short synthetic blips, imported as `USoundWave`, routed to the SFX class); (b) placeholder hooks: the cue calls and counting exist, no assets until #9; (c) defer the cues entirely to #9. Recommendation: (a) if a quick listen-and-approve round with the user is acceptable, else (b) |
| Q2 | **Where the new slider lives** in Settings | Gameplay, right under "HUD hologram effects" (recommended); or a new "Hologram" group holding both effects sliders. #46 restyles the page later either way |
| Q3 | **#46 shared code:** factor the hover / cursor / animation code for #46 first, or build it inside #42 and let #46 reuse it | Build it in #42 as renderer-free pure logic (`SOLHoloMenuMath`'s anim state, curves and cue rules have no hologram dependency), so #46 reuses it without a refactor (recommended); or a separate shared-UI part before both; or let #46 extract it later |
| Q4 | **Per-widget order** of the conversions | Radial, Overlay Menu, F3 panel, info / target cards (recommended: most visible first, the radial also proves the HUD menu layer); or Overlay Menu first (proves the UMG frame); or all in one part |
| Q5 | **Performance budget** | Section 11's numbers (recommended); or a looser cap; or "measure and report only" |
| Q6 | **Target / info cards vs #31's "no visible rectangle around any panel":** do the two cards get the outline frame (this ticket's look), and is the "target card" the top-right target readout (today part of the HUD) | Frame both, the target readout becomes the target card, no cue on target change (recommended); or frame only the info card and leave the target readout as HUD |
| Q7 | **Panels not named:** the map's right-click list menu (`USOLActionListWidget`, same action table as the radial), the Attack hint and the profile tag | Include the map list menu (same menu family), leave the hint and the tag as HUD (recommended); or exclude all |
| Q8 | **The Overlay list embedded in the Settings page** | Leave it to #46 (it sits on a solid page; the frame widget is not added when `SetEmbedded(true)`) (recommended); or hologram there too |
| Q9 | **Frame shape** | Chamfered corners (`FRAME_CHAMFER_PX` 6) (recommended, matches the radial's cut sectors); plain rectangle; corner brackets only |
| Q10 | **At strength 0:** is open / close instant and is the backdrop tint gone | Animations instant and no tint, the text keeps its drop shadow (recommended, a true "off"); or keep the 0.2 s draw-in and a minimum tint at 0 |
| Q11 | **Cursor:** does the hologram cursor follow the new slider | Keep it on the HUD slider (recommended; it also shows on the #46 screens); or the menu slider |

## 14. Out of scope

The flight HUD (D1); the full-screen menus (#46); keyboard / gamepad navigation (D5); mouse support for the F3 panel
(D5); in-world cockpit screens (`Docs/ToDo/cockpit-3d-screens.md`); the glow-text and panel-flicker tech debt (may be
fixed on the way only if the user asks).
