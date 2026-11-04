<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 42: Hologram in-flight menus

Issue #42. Depends on SDD 21 (HUD glow-line shape renderer, effects slider, panels, glow text), SDD 22 (radial menu,
Overlay Menu, hologram cursor, action table), SDD 34 (Overlay Menu profile tabs), SDD 2 section 6 (F3 speed panel)
and SDD 8 (Settings). Sister ticket: #46 (solid console menus for the full screens; notes in
`Docs/Plans/46-solid-menus-grill-notes.md`), which depends on #42 for the shared hover, cursor and animation code.
Plan: [`Docs/Plans/42-hologram-in-flight-menus-plan.md`](../Plans/42-hologram-in-flight-menus-plan.md).

Status: **draft, not approved.** The grill-me decisions below are resolved. The user approved A1 and A2 (section 9.1)
and answered Q1 and Q2 on 2026-11-03 (D7-D10), then Q2b, Q3-Q8, Q10 and Q11 the same day (D11-D19). **Q9 (the frame
shape of the Overlay Menu and the speed panel) is still open**: the user asked for visual examples, which are in
`Docs/Images/hologram-frame-samples/` (section 13). Q12 (cues at strength 0) is a small follow-up item raised while
recording D18. A part that depends on an open question does not start until it is answered. Checked against the code
at HEAD `4128a05`; the card scope of D15 was checked at HEAD `9846dc8`.

## 1. Goal

Give the menus the player uses while flying the same hologram look as the HUD, with motion. They are drawn as glow
outline frames by the HUD's shape renderer. Each one opens and closes with a short draw-in, glows and flickers on
hover, and shows a faint idle scanline and jitter. Sound cues play on open, hover and select. A new Settings slider
sets how strong these effects are, separately from the HUD's own slider. The flight HUD itself is not changed.

## 2. Resolved decisions (grill-me, the user's answers)

| # | Decision |
|---|---|
| D1 | **Scope: in-flight menus only.** The radial interaction menu (#22), the Ctrl+O Overlay Menu with its profile tabs (#34), the F3 speed panel, and the target / info cards. **The HUD stays as it is** (readouts, radar, icons, reticle, panels, glow). The main menu, Settings, demo page, Controls / Info / About and pause are **not** in scope; they are #46 (solid look). *Extended by D15 and D16:* the HUD's speed card and date-and-anchor card become hologram cards too, and the map's right-click list menu joins the scope; the rest of the HUD stays as it is. |
| D2 | **Rendering: reuse the HUD's glow-line shape renderer** (`FSOLHudShapeBatch`, `M_SOLHudShape`, the SDD 21 effects pipeline). Panels are **outline frames with no solid fill**. |
| D3 | **Backdrop:** a very faint dark tint behind each panel (no solid fill). Its strength follows the effects control. |
| D4 | **Effects:** (a) **open / close:** the frame lines draw in, then the fill fades up, about 0.2 s; (b) **hover:** an edge glow and a brief flicker on the hovered item, pairing with the #22 hologram cursor; (c) **idle:** a faint scanline sweep and jitter, off at effects 0; (d) **sound cues** for open, hover and select. |
| D5 | **Input unchanged:** mouse and keyboard as today. A keyboard / gamepad navigation redesign is a future idea, not part of #42. |
| D6 | **A separate hologram-menu intensity slider** in Settings, a new saved settings field. This is a schema change (section 9), approved as D7 / D8. |
| D7 | **A1 approved:** settings `SAVE_VERSION` 10 -> **11** for the new field `HoloMenuEffects`, with a 10 -> 11 migration step. `KEY_BINDING_LAYOUT_VERSION` stays **10**. |
| D8 | **A2 approved:** a migrated (v1-v10) save **copies its current `HudEffectsStrength`** into `HoloMenuEffects` (sanitized); a new save defaults to **1.0**. The 1.0 is the full-strength multiplier of the effects slider (shown as "1.00x"); it is not related to the schema version number 11. |
| D9 | **Q1 answered, (a): make the cues now.** Open, hover and select sounds were made with the SoundCraft MCP tool from CC0 source sounds in `D:\MediaLib\Sounds`; sources, licences and the import plan are in section 7.1. |
| D10 | **Q2 answered:** the new slider goes in **Settings > Graphics, right under the HUD effects slider**. Note: in the code today the "HUD hologram effects" slider is in the **Gameplay** section, not Graphics (`USOLSettingsPageWidget`, `SOLSettingsPageWidget.cpp` lines 364-369). *Superseded by D11.* |
| D11 | **Q2b answered:** the new slider goes in **Settings > Gameplay, right under the "HUD hologram effects" slider**. The HUD slider does not move. |
| D12 | **Q3 answered:** #42 builds the **shared hover, cursor and animation code first** (part 42a's renderer-free pure logic, then 42c's kit), and #46 reuses it without a refactor. |
| D13 | **Q4 answered:** conversion order **radial, Overlay Menu, F3 speed panel, then the cards** (info card, target card, speed card, date-and-anchor card). The map list menu (D16) was not placed in that order by the user; the plan puts it last (part 42g), after the cards. |
| D14 | **Q5 answered:** budget **under 0.1 ms extra CPU and under 0.1 ms extra GPU with all menus open**, measured by a profiling smoke (section 11). |
| D15 | **Q6 answered: frameless cards** (option 1 of the card samples): glowing **corner ticks + a faint dark tint**, no outline, so #31's "no visible rectangle around any panel" still holds. Applies to the **info card** and the **target card** (the top-right target readout of `DrawTarget` becomes the target card). **Scope extension:** the **speed card** (bottom-left block: speed, frame, absolute speed, cap and bar, assist / boost, and the G meter above it) and the **date-and-anchor card** (top-left block: sim date and warp, anchor, nearest body and altitude, surface lock) get the same look and effects (section 5). |
| D16 | **Q7 answered:** the map's right-click **list menu** (`USOLActionListWidget`) gets the hologram look. The Attack hint and the profile tag stay HUD. |
| D17 | **Q8 answered:** the Overlay list **embedded in the Settings page stays solid / plain** and follows #46's solid Settings look. #42 does **not** convert it: with `SetEmbedded(true)` no frame widget is added and the embedded copy keeps today's look until #46 restyles it. |
| D18 | **Q10 answered:** at **strength 0 the menus are plain flat panels**: no glow, no open / close animation, no jitter, no sweep, no flicker, and **fully usable** (every row, tab, digit and sector still reads and responds; the highlight keeps its Secondary colour). |
| D19 | **Q11 answered:** the hologram **cursor follows the new slider** (`HoloMenuEffects`), not the HUD slider. |

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
- **Speed card (D15, checked at `9846dc8`).** Bottom left: `ASOLFlightHud::DrawSpeedBlock` (relative speed large,
  frame line, absolute speed, `CAP`, the cap fill bar, assist mode and boost with the steer icon) and the G meter
  line and bar that `DrawPilotBlock` draws above it all grow `mLeftBlockRect`; `DrawHUD` then calls
  `AddHoloPanel(mLeftBlockRect, HUD_PRIMARY)` (`SOLFlightHud.cpp` line 853), a borderless panel on the HUD layer.
- **Date-and-anchor card (D15).** Top left: `ASOLFlightHud::DrawInfoBlock` draws the sim date and warp, `ANCHOR`,
  `NEAREST` with the altitude and the surface-lock line into a local `panel` rectangle and calls
  `AddHoloPanel(panel, HUD_PRIMARY)` (line 1201). The key-hint lines under it are drawn after the panel and are not
  part of the card (they stay HUD). The autopilot / route block (`mTopBlockRect`, top centre) is not a card and stays
  HUD.
- **Map list menu (D16).** `USOLActionListWidget` (`UI/SOLActionListWidget.h`), the map's right-click list of the
  same action table as the radial, C++ UMG.
- **Overlay Menu.** `USOLOverlayMenuWidget`, C++ UMG: `RootBorder` -> `PanelBorder` (dark translucent fill) -> title,
  5 profile tabs and up to 17 rows of `UTextBlock`, hint. Mouse hover and click via cached geometry; Up / Down,
  Space / Enter, PgUp / PgDn, RShift+O, Esc / Ctrl+O. Embedded, mouse-driven, in the Settings page.
- **F3 speed panel.** `USOLSpeedPanelWidget`, C++ UMG: steppers (digits and unit) and a scrollable body list. Keys and
  the mouse wheel only (no mouse hover or click); the highlight moves by keys.
- **Hologram cursor.** `USOLHologramCursorSubsystem` / `SSOLHologramCursor`: its own `M_SOLHudShape` instance,
  following `HudEffectsStrength` (`SOLHologramCursorSubsystem.cpp` line 71). D19 moves it to `HoloMenuEffects`.
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
     ASOLFlightHud: HUD layer (HUD MID, minus the cards)  +  menu layer (2nd SSOLHudShapeLayer, menu MID, above it)
                                                          radial menu, info card, target card, speed card,
                                                          date-and-anchor card
     USOLOverlayMenuWidget / USOLSpeedPanelWidget / USOLActionListWidget: USOLHoloFrameWidget (UMG wrapper of
                                                          SSOLHoloFrame, menu MID) behind the existing UTextBlock content
                                                          (not added to the Overlay list embedded in Settings, D17)
     USOLHologramCursorSubsystem: its own MID, strength = HoloMenuEffects (D19)
     All of them run FSOLHoloMenuAnim + SOLHoloMenu pure maths (open / close, hover, idle, backdrop, cue rules)
```

D12: the pure maths and the anim state are built first (42a) with no dependency on the hologram renderer, so the #46
solid menus reuse the same open / close, hover and cue code with their own drawing.

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
`mMenuLayer` (z just above the HUD layer, below the UMG menus), filled in `DrawHUD` by `DrawInteraction`, the
target card, the speed card and the date-and-anchor card (D15), with its material set to the subsystem's menu MID.
The two always-on cards move off the HUD layer, so the HUD effects slider no longer moves them; they follow
`HoloMenuEffects` like the other cards. `ResetFrame` / `MarkBatchFrame` follow the HUD
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
| **Overlay Menu with tabs** (UMG + `USOLHoloFrameWidget`; frame shape per Q9; the copy embedded in Settings is not converted, D17) | Frame draw-in around the panel, then backdrop, fill and the text (the content box's render opacity) fade up | The highlighted row or tab (mouse or keys, today's highlight) gets an edge-glow frame around its rectangle with the flicker | Sweep across the panel + jitter | Open at Ctrl+O; hover on each highlight change; select on a row toggle or tab switch |
| **F3 speed panel** (UMG + frame; frame shape per Q9) | As the Overlay Menu | The highlighted digit, unit field or body row (keys and wheel move it; no mouse, D5) | Sweep + jitter | Open at F3; hover on each highlight move; select on each applied cap (digit step, list Enter) |
| **Info card** (HUD menu layer; frameless, D15) | Corner ticks grow from the corners, then tint and text fade up, when Info is chosen; reverse when the card goes away | None (no input) | Sweep + jitter | Open only |
| **Target card** (HUD menu layer; frameless, D15; the top-right target readout) | As the info card, when a target is selected / cleared. The "no target" line stays a plain HUD line | None | Sweep + jitter | None (proposed: a target change is frequent; not separately answered) |
| **Speed card** (HUD menu layer; frameless, D15) | Always shown in flight: no open / close event, so no draw-in and no cue (the card is there whenever the HUD is) | None | Sweep + jitter | None |
| **Date-and-anchor card** (HUD menu layer; frameless, D15) | As the speed card (always shown, also on the map) | None | Sweep + jitter | None |
| **Map list menu** (UMG + frame, D16) | As the Overlay Menu | The highlighted row (mouse, as today) | Sweep + jitter | Open, hover, select on an enabled action (as the radial) |

**Card look (D15).** The four cards are frameless: a glowing L-shaped corner tick at each corner of the content
rectangle (`CARD_TICK_PX` long) and the faint dark tint behind it, no outline. Reference:
[`card-frameless.png`](../Images/hologram-card-samples/card-frameless.png). The frame shape of the Overlay Menu and the
speed panel is Q9 (open); the map list menu is proposed to take the Overlay Menu's answer.

**Strength 0 (D18).** Every menu and card becomes a plain flat panel: the open / close is instant, the backdrop is a
flat dark panel (`FLAT_PANEL_ALPHA`, readable without glow), outlines and ticks are thin lines with no glow halo, and
there is no sweep, jitter, flicker or hover glow. The highlighted row, tab, digit or sector keeps today's Secondary
colour, so the menus stay fully usable.

All frames use the HUD palette (`SOL::HudPalette::PRIMARY` lines, `SECONDARY` for the hovered / highlighted item,
dim rows as today). Text stays the existing glow text (HUD layer path for HUD-drawn menus) or the UMG `UTextBlock`s
in the HUD palette.

## 6. Pure logic contracts (`UI/SOLHoloMenuMath`, test-first)

`s` below is the effects strength `clamp(HoloMenuEffects, 0, 2)` (NaN reads as 0). Constants are in section 7.
These contracts are written for a separate test author (no implementation shown to them).

### 6.1 Curves

| Function | Contract |
|---|---|
| `float OpenProgress(double elapsedS, float durationS, float s)` | `s <= 0`: 1 (instant, D18). Else `clamp(elapsedS / durationS, 0, 1)`; `durationS <= 0` gives 1; negative elapsed gives 0 |
| `float FrameDrawIn(float progress)` | `SmoothStep(0, LINE_PHASE_END, progress)`; 0 at 0, 1 from `LINE_PHASE_END` on, monotonic |
| `float FillFade(float progress)` | `SmoothStep(FILL_PHASE_START, 1, progress)`; 0 up to `FILL_PHASE_START`, 1 at 1, monotonic. With `FILL_PHASE_START < LINE_PHASE_END` the two overlap a little |
| `float HoverGlow(double sinceHoverS, float s)` | 0 for `sinceHoverS < 0`. Ramp `r = SmoothStep(0, HOVER_RAMP_S, since)`. Flicker `f = 1 - HOVER_FLICKER_DEPTH * s * SmoothNoise(since * HOVER_FLICKER_RATE_HZ + HOVER_FLICKER_OFFSET)` while `since < HOVER_FLICKER_S`, else 1. Returns `HOVER_GLOW_INTENSITY * r * f`, always in `[0, HOVER_GLOW_INTENSITY]`; with `s <= 0` it is **0** (no glow, D18; the item's Secondary colour shows the highlight); for `s > 0`, from `HOVER_RAMP_S` and `HOVER_FLICKER_S` on it is exactly `HOVER_GLOW_INTENSITY` |
| `float SweepFraction(double ageS)` | `frac(fmod(ageS, HUD_TIME_WRAP_S) / IDLE_SWEEP_PERIOD_S)` in `[0, 1)`: 0 at the top of the frame |
| `float SweepIntensity(float s)` | `IDLE_SWEEP_INTENSITY * s`; 0 at `s = 0` (D4c) |
| `float BackdropAlpha(float s, float fillAlpha)` | `base(s) * clamp(fillAlpha, 0, 1)` with `base(s)` = `lerp(FLAT_PANEL_ALPHA, BACKDROP_ALPHA_PER_UNIT, s)` for `s` in `[0, 1]` and `BACKDROP_ALPHA_PER_UNIT * s` for `s` in `(1, 2]`: a **flat panel at `s = 0`** (`FLAT_PANEL_ALPHA`, D18), the faint tint at 1, continuous in `s`, never above `max(FLAT_PANEL_ALPHA, 2 * BACKDROP_ALPHA_PER_UNIT)`; 0 when `fillAlpha` is 0 (the shape of `base` between 0 and 1 is proposed, not answered) |
| `float GlowScale(float s)` | `clamp(s, 0, 1)`: scales the glow halo of outlines, ticks and sweep; 0 at `s = 0` gives thin flat lines (D18) |

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
| `float Progress(const FSOLHoloMenuAnim&, double nowS, float s)` | Closed 0, Open 1. Opening: `PhaseStartProgress` plus the elapsed fraction of `OPEN_S`, clamped to 1. Closing: `PhaseStartProgress` minus the elapsed fraction of `CLOSE_S`, clamped to 0. With `s <= 0`: Opening reads 1 and Closing 0 (D18) |
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
| `int32 CardCornerTicks(const FBox2f& rect, float tickPx, float fraction, FSOLHoloSegment outSegments[8])` | The frameless card look (D15): two segments per corner, each starting at the corner and running along its edge for `tickPx * clamp(fraction, 0, 1)` (NaN reads 0), with `tickPx` clamped to `[0, min(w, h) / 2]`. Returns 8, or 0 for an invalid or empty rect or a zero length. Mirror-symmetric about both centre lines |

`FrameOutline`, `FramePerimeter` and `FrameDrawSegments` describe a chamfered outline (Q9 option b). **They wait on
Q9:** with (a) the menus reuse `CardCornerTicks` plus a notched title-tab path; with (c) the outline becomes a stepped
path drawn with fixed gaps. This section is finalised before 42a's tests are briefed.

### 6.4 Test cases (for the separate test author; `Tests/HoloMenuTest.cpp`, `SOLTest.HoloMenu.*`)

1. `OpenProgress`: 0 / half / full / beyond the duration; negative elapsed; zero duration; `s = 0` instant.
2. `FrameDrawIn` and `FillFade`: end values, the phase thresholds, monotonic over 101 samples, outputs in `[0, 1]`.
3. `HoverGlow`: negative time 0; ramp end; exact intensity after both windows; `s = 0` gives 0 at every time; bounds
   over a sample sweep at `s` = 0, 1, 2.
4. `SweepFraction` wraps into `[0, 1)` (including large and negative ages); `SweepIntensity(0)` is 0.
5. `BackdropAlpha`: `FLAT_PANEL_ALPHA` at `s = 0` with fill 1; `BACKDROP_ALPHA_PER_UNIT` at `s = 1`; 0 at fill 0;
   continuous across `s = 1`; clamps `fillAlpha` and `s`; `GlowScale(0)` is 0 and `GlowScale(2)` is 1.
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
11. `CardCornerTicks`: 8 segments each `tickPx` long at fraction 1, half at 0.5, 0 at 0 or NaN; every segment starts
    at a corner of the rect; tick clamped on a small rect; invalid rect 0; symmetry.

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
| `FRAME_CHAMFER_PX` | 6 | Corner cut (x UI scale; only if Q9 = b) |
| `CARD_TICK_PX` | 16 | Corner tick length of the frameless cards (x UI scale, at a 720 px canvas; as in the approved sample, D15) |
| `FLAT_PANEL_ALPHA` | 0.45 | Backdrop alpha at strength 0, a plain flat panel (D18; the HUD backing's 0.45) |
| `FRAME_MAX_SEGMENTS` | 18 | Capacity of `FrameDrawSegments` (8 sides split by the 2 stroke starts, plus margin) |
| `IDLE_SWEEP_PERIOD_S` | 4.0 | One scanline pass from top to bottom |
| `IDLE_SWEEP_INTENSITY` | 0.08 | Sweep line glow per unit of strength |
| `BACKDROP_ALPHA_PER_UNIT` | 0.15 | Dark tint alpha per unit of strength (0.30 at 2; the HUD backing is 0.45) |
| `HOVER_CUE_MIN_INTERVAL_S` | 0.05 | Shortest gap between two hover cues |
| `FRAME_MAX_SHAPES` | 128 | Shape capacity of one `SSOLHoloFrame` (reserved once) |

Settings constants (`SOL::Settings`): `HOLO_MENU_EFFECTS_MIN` 0, `HOLO_MENU_EFFECTS_MAX` 2,
`HOLO_MENU_EFFECTS_DEFAULT` 1 (approved, D8), `HOLO_MENU_EFFECTS_STEP` 0.05.
Asset paths for the cues go in `SOLConstants.h` (STYLE_GUIDE section 7), section 7.1.

### 7.1 Sound cues (Q1 = a, D9)

**Files made (2026-11-03).** Source files are in `Docs/Art/ui-sounds/` (outside `Content/`, so never cooked):

| Cue | File | Made from (all CC0) | Processing in SoundCraft | Length | Peak |
|---|---|---|---|---|---|
| Open | `S_SOLHoloOpen.wav` | `kenney_digital-audio/Audio/phaserUp2.ogg` (first 75 ms of silence trimmed) + `kenney_interface-sounds/Audio/maximize_009.ogg` (at +50 ms, -7 dB) | 350 Hz high-pass and a short plate (15 %) on the sweep, a short plate (18 %) on the blip, exponential fade-outs | 0.40 s | -9.6 dBFS |
| Hover | `S_SOLHoloHover.wav` | `kenney_interface-sounds/Audio/select_001.ogg` | -13 dB clip gain, 1.2 kHz high-pass, fade-out; dry (it repeats under a moving cursor) | 0.05 s | -14.6 dBFS |
| Select | `S_SOLHoloSelect.wav` | `kenney_interface-sounds/Audio/select_003.ogg` (-5 dB, cut at 0.27 s) + `kenney_interface-sounds/Audio/confirmation_001.ogg` (-14 dB, 450 Hz high-pass) | a short plate (15 %) on the ping, fade-outs | 0.30 s | -10.0 dBFS |

All three are 48 kHz, 16-bit, stereo WAV, staged so that hover is the quietest. The SoundCraft sessions
(`holo_open.scsession`, `holo_hover.scsession`, `holo_select.scsession`) and the source sounds they use (`Audio Files/`,
the Kenney files converted to WAV) sit next to them, so a cue can be reopened and remade. SoundCraft writes to absolute
paths (no write-root flag needed). Tool note: its `gain` insert plugin had no effect in a bounce, so levels are set
with clip gain. **The user has not listened to them yet:** the listen-and-approve round of part 42h still applies.

**Licences of `D:\MediaLib\Sounds` (checked 2026-11-03).** The GitHub repo `DieSlower/SOLTest` is **public** and
CC0-licensed, so a sound committed here (as a WAV source or inside a `.uasset`) is redistributed, not only shipped in
the game. That rules out packs that allow game use but forbid passing the files on.

| Pack | Licence (from its own licence / readme file) | Shipped game | In this public repo | Used |
|---|---|---|---|---|
| `kenney_digital-audio`, `kenney_interface-sounds`, `kenney_sci-fi-sounds`, `kenney_ui-audio` (the other `kenney_*` folders also have a `License.txt`, not read) | CC0 1.0 (public domain); credit optional | Yes | Yes | **Yes**, the only source used |
| `CrispUI_Sampler` | Royalty-free in commercial games, no credit; no resale or redistribution of the sounds, modified or not | Yes | No (redistribution) | No |
| `Cosy-UI-Sound-Pack-Sampler` (Leckie Pixel Studio) | Commercial games allowed; no redistribution, even modified | Yes | No | No |
| `UI-SFX-Vol1-Lite` | Royalty-free, may ship inside a packaged product; no redistribution "as-is, renamed, converted, or edited" | Yes | No | No |
| `Free UI And Soundtrack Samples` (Echo Chamber Works) | Commercial use allowed; no redistribution "in any form" except inside a larger work; Indian law | Yes, inside the game | Unclear (raw files in a public repo), treated as no | No |
| `UI Soundpack` (Nathan Gibson) | CC BY 4.0 | Yes, with attribution in the credits | Yes, with attribution | No |
| `JDSherbert - Ultimate UI SFX Pack (FREE)` | **No licence or readme file in the folder** | **Unknown** | **Unknown** | No |
| `Sonniss.com - GDC ... Game Audio Bundle` (2015-2026) | Readme: royalty-free, commercial use without attribution; the licence PDFs were not read in detail | Very likely yes | Not checked | No |

Answer: **the three cues are cleared for a shipped game and for this public repo** (all sources CC0, no credit
required; a credit line for Kenney is welcome but optional). No open licensing item blocks #42. If the user later
wants sounds from a non-CC0 pack, two questions come back: whether committing them to the public repo is acceptable
(it is redistribution for most of these licences), and the JDSherbert pack's licence (none on disk).

**Import (part 42h).**
- Asset paths in `SOLConstants.h`, one table like `COMBAT_SOUNDS`: `SOL::HoloMenu::CUES[]` (path, volume, minimum
  interval) for Open, Hover and Select, with the paths `/Game/SOL/UI/Audio/S_SOLHoloOpen.S_SOLHoloOpen`,
  `/Game/SOL/UI/Audio/S_SOLHoloHover.S_SOLHoloHover` and `/Game/SOL/UI/Audio/S_SOLHoloSelect.S_SOLHoloSelect`.
- An editor Python script `Tools/UI/import_holo_menu_sounds.py` (pattern of `Tools/StarField/import_star_field.py`,
  `unreal.AssetImportTask`, automated, replace existing) imports the three WAVs from `Docs/Art/ui-sounds/` into
  `/Game/SOL/UI/Audio/` and sets each wave's sound class to `SC_SOLSfx` (`SOL::Paths::SOUND_CLASS_SFX`), so the SFX
  volume slider drives them. `Tools/CreateSOLAudioClasses.py` gains the same assignment for `/Game/SOL/UI/Audio/S_SOL*`
  so a re-run of it keeps them in the SFX class. The import may fold the stereo files to mono (UI cues are 2D).
- `USOLHoloMenuSubsystem` loads them once (section 4.3); a missing asset stays silent with one warning.
- The user listens in game and approves (or asks for changes) before part 42h is committed.

## 8. Settings slider

- Label `SOLMenuText::SETTINGS_HOLO_MENU_EFFECTS` = "Hologram menu effects" (shown as a multiplier, "1.00x", like the
  HUD slider).
- `ESOLSettingsSlider::HoloMenuEffects` with its `SETTINGS_SLIDERS` row (the `static_assert` count grows), a handler
  and `ApplySliderValue` / value-read cases. Placement on the page (D11): **Settings > Gameplay, right under the
  "HUD hologram effects" slider**; the HUD slider stays where it is.
- It is a normal Settings field: Apply / Revert, written at the page's `EndVisit` (no new write point). Live
  preview: the menu MID reads the live values, like the HUD slider does today; no menu is open while Settings shows,
  so the preview is seen on the next open (the user may want a preview frame on the page; not proposed).
- **The hologram cursor follows the new slider (D19):** `USOLHologramCursorSubsystem` reads `HoloMenuEffects`
  instead of `HudEffectsStrength`. The cursor also shows on the #46 screens, so this slider sets its effects there
  too. A migrated save copies the HUD value (D8), so an existing player sees no change until they move a slider.
- What each slider drives after #42: **HUD hologram effects** = the flight HUD (readouts, radar, icons, reticle, the
  remaining HUD panels and lines); **Hologram menu effects** = the radial, Overlay Menu, speed panel, map list menu,
  the four cards (D15) and the cursor.

## 9. Persistence (schema 11, approved 2026-11-03)

Rule: **version and migrate, never silently discard.** The implementer reports exactly which constants and fields it
changed; the main session checks the report against this section and against the user's answer to A1 / A2 before
the part is ticked.

### 9.1 Approval items (approved by the user, 2026-11-03)

| # | Item | Decision |
|---|---|---|
| A1 | The schema bump `SAVE_VERSION` 10 -> 11 for the new field | **Approved:** bump to 11, new field `HoloMenuEffects`, a 10 -> 11 migration step. `KEY_BINDING_LAYOUT_VERSION` stays 10 (D7) |
| A2 | The default value and the migrated value | **Approved:** a migrated v1-v10 save **copies its current `HudEffectsStrength`** (sanitized), because the radial menu and the info card follow that slider today, so a player who turned the HUD effects down keeps calm menus. A new save defaults to **1.0** (D8). The 1.0 is the full-strength multiplier of the effects slider, not the schema version |

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
1. `HoloMenuEffects` = `clamp(HudEffectsStrength, MIN, MAX)` (NaN -> default 1.0), per the approved A2.
2. Every other field is kept as loaded.
3. `Version = 11`.

A second run is a no-op (the version is then 11). A v1-v9 file runs the earlier steps first (so a v4 file reaches
this step with `HudEffectsStrength` = 1 from the 4 -> 5 step). A file newer than 11 is still ignored by
`ClassifySaveVersion` (unchanged).

### 9.4 Writes

No new write point. The field changes only on the Settings page (Apply), and is written with the rest at `EndVisit`,
in the same async save, only when something differs.

### 9.5 Persistence tests (separate test author)

`SOLSettingsRulesTest.cpp`: 10 -> 11 per A2 (a v10 file with `HudEffectsStrength` 0.3 gives 0.3); NaN and
out-of-range sources sanitized; whole chain from v4 and from v6; idempotence; a current v11 file keeps its value; a
fresh save has the default; `SetValues` / `GetValues` round trip incl. 0. `SOLSettingsModelTest.cpp`: default,
clamping, NaN / inf, `AreEqual` tolerance, dirty and revert. `FlightModeMathTest.cpp`: re-validate the schema and
layout pins (`SAVE_VERSION` 11, layout 10, `DefaultLayoutChangedSince(10)` false, `(9)` true). Every existing
settings test is walked to confirm it still states the current contract.

## 10. Documentation, in-game text and screenshots (same change as the code)

- **`Docs/GAME_MECHANICS.md`:** section 8.1 Settings (new slider row with range, step, default and what it scales);
  section 7a (radial look, open / close, hover, cues); the Ctrl+O and F3 rows of section 1 (frame, effects); a
  tunables table for `SOL::HoloMenu`; the HUD-effects row says it no longer drives the radial menu, the cards
  (info, target, speed, date-and-anchor) or the cursor; the map list menu row.
- **In-game Controls / Info / About** (`SOLMenuText` texts shown by `SOLMenuContent`): the Settings description of the
  new slider (it also sets the cursor, D19); the sentence that names the HUD hologram effects slider (`SOLMenuText.h`
  around line 452) narrowed to the HUD; a line that the in-flight menus animate and play cues (truthful to whatever Q1 decides: no claim of
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

- **Budget (D14):** with **all menus open** (every menu and card that can be shown at the same time), the extra cost
  over the pre-#42 baseline is **under 0.1 ms CPU** (game thread: `HUD Time` + `Game UI Paint` + Slate paint) and
  **under 0.1 ms GPU** (`ProfileGPU` with `r.ProfileGPU.ShowUI 1`: the shape batches and Slate UI), 1280x720, editor
  build. Menus that exclude each other (the radial, Ctrl+O, F3 and the map list menu take the same input) are measured
  one per pass, each together with all four cards; the worst pass must meet the budget. Also: **zero heap allocation
  per frame**; at most `FRAME_MAX_SHAPES` shapes per frame widget; one extra custom-vertex draw per visible menu.
- **Profiling smoke (D14):** `-SOLSmokeHoloMenuProfile` (flag constant in `SOLConstants.h`, listed in ARCHITECTURE)
  flies the fixed smoke scene, opens each pass's menus, waits for the open animation to end, runs
  `stat dumpave -num=240` and one `ProfileGPU`, and logs the numbers with a PASS / FAIL against the budget. The
  baseline is the same scene and passes at the commit before 42c (the menus as they are today), captured once and
  written into the SDD amendment; the final numbers are captured in 42i.
- Event-driven: a closed menu costs nothing (the HUD's menu layer draws nothing and is not stamped; a closed UMG
  frame is removed). The UMG `NativeTick` returns at once when the menu is Open and idle; the idle sweep and hover
  glow are evaluated in the frame's paint from `nowS` (a few float ops), no timers.
- The menu MID parameters are pushed only when they change (strength, view size), as the HUD does.
- The always-on speed and date-and-anchor cards are drawn every flight frame: their idle sweep and jitter are a few
  float ops in the paint, and they cost no more shapes than today's two panels plus 16 tick segments.
- No claim without a measurement; the numbers go into an SDD amendment.

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
  7. Info card: open via Info, then close; target card: select and clear a target (corner ticks drawn, no outline);
     speed card and date-and-anchor card: present in the menu layer with 8 tick segments each, and gone from the HUD
     layer's panels.
  8. Map list menu: open on the map, hover a row, choose an enabled action (open, hover and select cues).
  9. Strength 0 (set in the smoke slot): open is instant, no sweep, flicker, jitter or hover glow drawn, the backdrop
     is `FLAT_PANEL_ALPHA`, and a row toggle, a digit step and a radial selection still work (D18); strength 2
     screenshot.
  10. Settings: the slider is in Gameplay right under the HUD effects slider (D11); Apply writes once at `EndVisit`;
      the cursor's effects strength follows `HoloMenuEffects`, not `HudEffectsStrength` (D19).
  11. The Overlay list embedded in Settings has no frame widget and keeps today's look (D17).
  12. Cue playback is counted through the subsystem (the section 7.1 assets), not heard.
- **Regression smokes:** `-SOLSmokeOverlay`, `-SOLSmokeInteract`, `-SOLSmokeHud` (F3 checks; any check that counts
  the HUD layer's panels or backings is updated for the two cards that move to the menu layer), `-SOLSmokeChase`
  (radial entry), `-SOLMenuSmoke=all` (Settings page; its known stale checks stay as in TECH_DEBT).
- **PIE smoke** with unreal-mcp when the editor is open: screenshots looked at for each menu at strength 0, 1, 2.
- **Adversarial review** per part (opus, xhigh, report only), briefed with this SDD, the STYLE_GUIDE, the performance
  guidelines and the tech-debt items of section 3.

## 13. Questions for the user

Answered on 2026-11-03 (kept here for the record; the decisions are D7-D19):

| # | Question | Answer |
|---|---|---|
| A1 / A2 | Schema bump and the default / migrated value (section 9.1) | Approved: `SAVE_VERSION` 11, migrated saves copy `HudEffectsStrength`, new saves 1.0 (D7, D8) |
| Q1 | **Sound cue assets** for open / hover / select | **(a), now:** made with SoundCraft from CC0 Kenney sounds in `D:\MediaLib\Sounds`; licence check and import plan in section 7.1 (D9). The listen-and-approve round is still to come (part 42h) |
| Q2 | **Where the new slider lives** in Settings | Settings > Graphics, right under the HUD effects slider (D10), clarified by Q2b |
| Q2b | The HUD effects slider is in Gameplay, not Graphics | **Settings > Gameplay, right under the HUD effects slider** (D11) |
| Q3 | #46 shared code | **#42 builds the shared hover / cursor / animation code first; #46 reuses it** (D12) |
| Q4 | Per-widget order | **Radial, Overlay Menu, speed panel, then the cards** (info, target, speed card, date-and-anchor card) (D13) |
| Q5 | Performance budget | **Under 0.1 ms extra CPU and GPU with all menus open, measured by a profiling smoke** (D14, section 11) |
| Q6 | Card look ([`card-compare.png`](../Images/hologram-card-samples/card-compare.png)) | **Frameless** (glowing corner ticks + faint dark tint) for the info and target cards, and the **speed card and the date-and-anchor card get the same look and effects** (scope extension, D15) |
| Q7 | Panels not named | **The map list menu is hologram** (D16); the Attack hint and profile tag stay HUD |
| Q8 | The Overlay list embedded in Settings | **Stays solid / plain, follows #46's solid Settings; not converted by #42** (D17) |
| Q10 | Strength 0 | **Plain flat panels: no glow, animation or jitter; fully usable** (D18) |
| Q11 | Cursor | **Follows the new slider** (D19) |

Still open (not decided; a recommendation is attached but the user decides):

| # | Question | Options (recommendation first, not a decision) |
|---|---|---|
| Q9 | **Frame shape of the Ctrl+O Overlay Menu and the F3 speed panel** (and, proposed, the map list menu). The user asked for visual examples: [`overlay-a.png`](../Images/hologram-frame-samples/overlay-a.png), [`overlay-b.png`](../Images/hologram-frame-samples/overlay-b.png), [`overlay-c.png`](../Images/hologram-frame-samples/overlay-c.png), [`speed-a.png`](../Images/hologram-frame-samples/speed-a.png), [`speed-b.png`](../Images/hologram-frame-samples/speed-b.png), [`speed-c.png`](../Images/hologram-frame-samples/speed-c.png) and the side-by-side [`frames-compare.png`](../Images/hologram-frame-samples/frames-compare.png). They are Python / Pillow mock-ups over the same real game screenshot as the card samples (`43-dust-slow.png`), with today's menu text and the hover / highlight in Secondary; not in-game captures. The speed panel sits a little higher than its real centred place so the ship does not cover it; today's full-screen dim behind it is not drawn | (a) **corner ticks + faint tint with a notched title tab** (recommended: the same family as the chosen frameless cards, so #31's "no visible rectangle" rule holds for every in-flight panel; tabs marked by a left bar, the hovered row by small Secondary corner ticks); (b) **chamfered glowing outline** + faint tint (the selected profile tab opens into the divider like a real tab; the hovered row gets a chamfered Secondary outline; the most readable "panel" look, but it is a visible rectangle); (c) **segmented / stepped broken lines** (Homeworld-style: thin cyan glow only, no fill, a raised title step, cut corners, gaps with end dots, ruler ticks; the hovered row gets Secondary brackets with a pointer line; the lightest look, but with no tint, bright scenery behind the text, such as orbit lines, stays visible) |
| Q12 | **Cues at strength 0** (raised while recording D18, which covers glow, animation and jitter but not sound) | Cues still play at strength 0, since the SFX volume already controls them (recommended); or silence them at strength 0 too |

## 14. Out of scope

The flight HUD (D1) except the speed card and the date-and-anchor card (D15); the full-screen menus (#46); the Overlay
list embedded in Settings (#46, D17; it keeps today's look until #46 restyles it); the Attack hint and the profile tag
(stay HUD, D16); keyboard / gamepad navigation (D5); mouse support for the F3 panel (D5); in-world cockpit screens
(`Docs/ToDo/cockpit-3d-screens.md`); the glow-text and panel-flicker tech debt (may be fixed on the way only if the
user asks).
