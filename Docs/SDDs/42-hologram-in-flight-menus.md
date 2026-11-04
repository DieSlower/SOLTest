<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 42: Hologram in-flight menus

Issue #42. Depends on SDD 21 (HUD glow-line shape renderer, effects slider, panels, glow text), SDD 22 (radial menu,
Overlay Menu, hologram cursor, action table), SDD 34 (Overlay Menu profile tabs), SDD 2 section 6 (F3 speed panel)
and SDD 8 (Settings). Sister ticket: #46 (solid console menus for the full screens; notes in
`Docs/Plans/46-solid-menus-grill-notes.md`), which depends on #42 for the shared hover, cursor and animation code.
Plan: [`Docs/Plans/42-hologram-in-flight-menus-plan.md`](../Plans/42-hologram-in-flight-menus-plan.md).

Status: **every question is answered (2026-11-03); implementation in progress.** The grill-me decisions below are
resolved. The user approved A1 and A2 (section 9.1) and answered Q1 and Q2 on 2026-11-03 (D7-D10), then Q2b, Q3-Q8,
Q10 and Q11 the same day (D11-D19), then Q9 (frame shape, D20) and Q12 (cues at strength 0, D21). One item is to be
confirmed by the user at the hand check: the map list menu takes the same option (c) frame as the Overlay Menu (D20;
it was proposed in the Q9 wording, not separately answered). Checked against the code at HEAD `4128a05`; the card
scope of D15 was checked at HEAD `9846dc8`.

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
| D20 | **Q9 answered: option (c) with a faint dark tint** for the **Ctrl+O Overlay Menu** and the **F3 speed panel**: a segmented, stepped broken-line glow frame (Homeworld-style). Thin cyan glow lines, a raised title step on the left of the top edge, cut corners and a stepped-in bottom-right corner, fixed gaps with end dots, ruler ticks on the left edge; the hovered row gets Secondary brackets with a pointer line; plus the faint dark tint behind the panel (the `BackdropAlpha` path). Approved samples: [`overlay-c-tint.png`](../Images/hologram-frame-samples/overlay-c-tint.png), [`speed-c-tint.png`](../Images/hologram-frame-samples/speed-c-tint.png), [`frames-compare-c-tint.png`](../Images/hologram-frame-samples/frames-compare-c-tint.png). Geometry in section 6.3. The **map list menu** (D16) uses the same option (c) frame, as proposed in the Q9 wording; **to be confirmed by the user at the hand check** (not separately answered). |
| D21 | **Q12 answered:** the menu **sounds are independent of the hologram strength** and still play at strength 0; the SFX volume controls them. |

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
| `USOLHoloMenuSubsystem` | `UWorldSubsystem` | Owns the menu MID (`UPROPERTY` `TObjectPtr`), pushes `HoloMenuEffects` and the view size into it only when they change, loads the cue sounds once from `SOL::HoloMenu::CUES` and plays them 2D in the SFX class (`PlayCue`); a missing asset is silent with one warning |
| `SSOLHoloFrame` | Slate leaf, hit-test invisible | Draws one frame: backdrop tint (backing boxes), the option (c) broken outline with its gap dots, ruler ticks and accents (D20, section 6.3), the dividers and tab markers its menu asks for, the borderless panel fill, the idle sweep line and the hover brackets of the hovered item, as one custom-vertex draw with the menu MID; buffers reserved once |
| `USOLHoloFrameWidget` | `UWidget` wrapping `SSOLHoloFrame` | Lets the C++ UMG menus place a frame behind their content and feed it the anim look and the hovered item's rectangle |

The menu layer in the HUD is a second `SSOLHudShapeLayer` instance (no new class): `ASOLFlightHud` gets
`mMenuLayer` (z just above the HUD layer, below the UMG menus), filled in `DrawHUD` by `DrawInteraction`, the
target card, the speed card and the date-and-anchor card (D15), with its material set to the subsystem's menu MID.
The two always-on cards move off the HUD layer, so the HUD effects slider no longer moves them; they follow
`HoloMenuEffects` like the other cards. `ResetFrame` / `MarkBatchFrame` follow the HUD
layer's frame-stamp rule. The HUD keeps `GlowLine` / `AddHoloPanel` for the HUD; a few `Menu*` helpers write to the
menu layer's batch, backings and text list (`MenuText`, so a menu's text paints above its own backing and fill; the
menu layer is sized `MENU_LAYER_MAX_SHAPES`). The `sol.HudRenderer 0` Canvas fallback draws the menus as today (no
effects).

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
- **The first open of a UMG menu (42e amendment).** The frame's Slate widget is only made when the menu is first
  arranged, so on the first open there is no frame to start yet. The open is then deferred: the frame's open (its
  `BeginOpen` and the open cue) starts on the widget's first tick, which runs before the content's first paint, and
  the content waits at render opacity 0 until then, so the text never shows before its frame. Later opens (and a
  reopen during the close) start the frame at once.
- **HUD-drawn menus (radial, info, target):** the HUD keeps a copy of the last drawn state (radial state and its
  action list, the info / target object; value types, no allocation) and draws the closing animation from it.
  For the radial (42d review): the close keeps the last hovered sector, so its hover glow fades with the fill rather
  than dropping in one frame; a new radial menu (a new press, possibly on another object) always draws in from
  Closed, even while the previous one still closes (a different menu never reverses from the old one's progress);
  the menu's anim and cues update on every HUD frame, also while the flight HUD is hidden (Jump To starts the warp
  at the release, and its select cue still plays on that frame); the opening update never asks for a hover cue,
  even at strength 0 where the menu is Open at once (the press asks for the open cue only, D21).

### 4.6 Part 42c testable contracts

Written for the separate test author (`Tests/HoloMenuKitTest.cpp`, `SOLTest.HoloMenuKit.*`). Everything here runs
headless: no Slate, no RHI, no sound. `u` is the `uiScale` after the section 6.3 rule (NaN, infinite or `<= 0` reads
1, then clamped to `[UI_SCALE_MIN, UI_SCALE_MAX]`); `s` is the strength after the section 6 rule. Items marked
(proposed) are not settled by D1-D21 or the samples.

**Frame builder (`UI/SOLHoloFrameBuild.h/.cpp`, namespace `SOLHoloFrameBuild`).** It writes into a fixed-capacity
output, not straight into the batch, so colours and roles can be asserted without decoding vertices; a second call
appends that output to an `FSOLHudShapeBatch` (a plain struct, usable headless).

```
enum class ESOLHoloPart : uint8 { Fill, Outline, GapDot, Ruler, Accent, TitleAccent, ColumnDivider, BrokenDivider,
                                  Tab, TabDot, CardTick, Sweep, Hover, HoverDot };
struct FSOLHoloShape { ESOLHoloPart Part; ESOLHudShapeKind Kind; FVector2f A; FVector2f B; float HalfWidthPx;
                       FLinearColor Color; float Intensity; float Fill; float PadPx; };
                       // Line: A -> B (a dot is A == B, HalfWidthPx = radius). Panel: A = centre, B = half size
struct FSOLHoloBacking { FBox2f Rect; float CornerPx; float FeatherPx; float Alpha; };   // Alpha is absolute
struct FSOLHoloFrameOutput { FSOLHoloShape Shapes[FRAME_MAX_SHAPES]; int32 ShapeCount = 0;
                             bool bHasBacking = false; FSOLHoloBacking Backing; };
struct FSOLHoloFrameSpec { FBox2f Rect; float TitleStepWidthPx = 0; float UiScale = 1;
    bool bColumnDivider = false; FVector2f DividerTop; float DividerBottomY; float OpenTopY; float OpenBottomY;
    bool bBrokenDivider = false; FVector2f BrokenStart; float BrokenEndX; float BrokenStepAtPx; float BrokenStepPx;
    int32 TabCount = 0; FBox2f TabRects[FRAME_MAX_TABS]; int32 SelectedTab = INDEX_NONE; float TabDividerX;
    FBox2f HoverItemRect;  /* invalid = nothing hovered */  ESOLHoloHoverMark HoverMark = RowPointer; };
```

| Function | Contract |
|---|---|
| `int32 BuildFrame(const FSOLHoloFrameSpec& spec, const FSOLHoloMenuLook& look, float s, FSOLHoloFrameOutput& out)` | Empties `out` first (a dirty output gives the same result). Returns `out.ShapeCount`. 0 shapes and no backing when `!look.bVisible` or `spec.Rect` is invalid (section 6.3). Shapes are written in this order, each group taken unchanged from its section 6.3 function: Fill; Outline = `FrameDrawSegments(Rect, TitleStepWidthPx, UiScale, look.FrameFraction)`; GapDot = `FrameGapDots(...)` at the same fraction; Ruler = `FrameRulerTicks`; Accent = `FrameAccents` pieces 1-2, TitleAccent = pieces 3-4 (told apart by direction, as
`FrameAccents` skips pieces: the rightward ones are 3-4), in `FrameAccents`' order; ColumnDivider = `DashedDivider` (when `bColumnDivider`); BrokenDivider = `BrokenDivider` (when `bBrokenDivider`); Tab and TabDot = `TabMarker` per tab in index order, `bSelected = (i == SelectedTab)`, its dot after its pieces; Sweep; Hover and HoverDot = `HoverMark(HoverItemRect inflated by HOVER_PAD_X_PX * u, HOVER_PAD_Y_PX * u)`. Same `A`, `B` and counts as those functions (bit for bit) |
| Which groups appear | Outline and GapDot only when `FrameFraction > 0` (0 gives no outline). Fill, Ruler, Accent, TitleAccent, dividers and tabs only when `FillAlpha > 0`. Sweep only when `look.SweepIntensity > 0` (none at `s = 0` or fill 0). Hover only when `HoverItemRect` is valid (the section 6.3 rect rule) and its intensity below is `> 0`. Backing only when `look.BackdropAlpha > 0`: `Rect` inset by `FRAME_TINT_INSET_PX * FrameScale(Rect, UiScale)` per side, `CornerPx` 0, `FeatherPx` `HUD_PANEL_BACKING_FEATHER_PX * u` (proposed), `Alpha = look.BackdropAlpha`. A shape with intensity `<= 0` is never written |
| Colour and intensity | Every shape's `Color` is exactly `SOL::HudPalette::PRIMARY`, except Hover and HoverDot: exactly `SECONDARY`, and Fill: exactly `SOL::HoloMenu::BACKDROP_TINT` (the dark tint of the approved samples; amended at the 42c review, was Primary). Outline, GapDot: `FRAME_INTENSITY`. Ruler, Accent, the selected tab's pieces and TabDot: `FRAME_INTENSITY * FillAlpha`. TitleAccent, both dividers, unselected tab pieces: `FRAME_INTENSITY * FRAME_DIM_ALPHA * FillAlpha`. Sweep: `look.SweepIntensity`. Hover, HoverDot: `look.HoverIntensity * FillAlpha` for `s > 0` (42a review), `FRAME_INTENSITY * FillAlpha` at `s = 0` (D18, the flat Secondary mark of section 6.3, proposed there); the switch uses the sanitized `s`, so NaN and negative strengths count as 0 |
| Sizes and glow | Lines: `Kind` Line, `HalfWidthPx = FRAME_HALF_WIDTH_PX * u`, `Fill` 0. GapDot: radius `FRAME_DOT_RADIUS_PX * FrameScale(...)`; TabDot, HoverDot: `FRAME_DOT_RADIUS_PX * u` (proposed). Every line and dot: `PadPx = lerp(FRAME_FLAT_PAD_PX, HUD_GLOW_PAD_PX, GlowScale(s))`, so exactly `FRAME_FLAT_PAD_PX` at `s = 0` (thin flat lines, no halo) and `HUD_GLOW_PAD_PX` at `s >= 1`. Fill: `Kind` Panel over the backing rect (centre, half size), `HUD_PANEL_INTENSITY`, `Fill = HUD_PANEL_FILL * FillAlpha`, `PadPx = HUD_PANEL_QUAD_PAD_PX` (proposed: the fill keeps this at `s = 0`). Sweep: one line across the backing rect, from `Min.X` to `Max.X`, at `y = Min.Y + look.SweepFraction * height` |
| Capacity, allocation, determinism | Never more than `FRAME_MAX_SHAPES`; a `static_assert` checks the worst case (24 + 14 + 6 + 4 + 40 + 6 + `FRAME_MAX_TABS` * 4 + 1 + 8 + 1 + 2 = 126). No heap use (fixed arrays). The same inputs give identical outputs on every call (an outline cache kept while Open, as the 42a review asked, must not change that). `TabCount` above `FRAME_MAX_TABS` reads `FRAME_MAX_TABS` |
| `int32 BuildCard(const FBox2f& contentRect, float uiScale, const FSOLHoloMenuLook& look, float s, FSOLHoloFrameOutput& out)` | The frameless card (D15). Card rect = `contentRect` grown by `HUD_PANEL_PADDING_PX * u` (`PanelAroundRect`, so the cards keep today's panel size; proposed). Same visibility, colour, pad and "no zero intensity" rules as `BuildFrame`. Order: Fill (as `BuildFrame`, over the card rect); 8 CardTick = `CardCornerTicks(cardRect, CARD_TICK_PX * u, look.FrameFraction)` at `FRAME_INTENSITY`; Sweep across the card rect. No Outline, GapDot, Ruler, Accent, divider, tab or hover shape ever. Backing: the card rect, `CornerPx = HUD_PANEL_CORNER_PX * u`, `FeatherPx = HUD_PANEL_BACKING_FEATHER_PX * u` (as `AddHoloPanel` today), `Alpha = look.BackdropAlpha`. At most `CARD_MAX_SHAPES` (10) |
| `int32 AppendToBatch(const FSOLHoloFrameOutput& in, FSOLHudShapeBatch& batch)` | Adds the shapes in order: Line by `AddLine(A, B, HalfWidthPx, Color, Intensity, PadPx)`, Panel by `AddPanel(A, B, Color, Intensity, Fill, PadPx)`. Does not reset the batch. Returns how many the batch took (the rest are its dropped count). The kind decoded from each new quad (`DecodeKind` of `TexCoords[3]`) matches `Kind`. After `batch.Initialize(FRAME_MAX_SHAPES)`, 200 rounds of `Reset`, `BuildFrame` and `AppendToBatch` leave `GetVertices()` and `GetIndices()` allocated sizes unchanged and `GetDroppedCount()` 0 |
| `float BackingOpacity(float alpha)` | `clamp(alpha / HUD_PANEL_BACKING_COLOR.A, 0, 1)`, NaN 0: the `opacity` for `SSOLHudShapeLayer::AddBacking` / `PaintBacking` (so `FLAT_PANEL_ALPHA` gives 1). The menus pass `BACKDROP_TINT` as the backing's colour (its RGB; the alpha stays `HUD_PANEL_BACKING_COLOR.A * opacity`) |
| `float LinePad(float s)` | `lerp(FRAME_FLAT_PAD_PX, HUD_GLOW_PAD_PX, GlowScale(s))`: the pad of every frame / card line and dot and of the HUD's `MenuLine` (42c review); `FRAME_FLAT_PAD_PX` at `s <= 0` and NaN |
| `float SOLHoloMenu::FrameScale(const FBox2f& rect, float uiScale)` (added to `SOLHoloMenuMath`) | The effective scale `k` of section 6.3; 0 for an invalid rect. R of section 6.4 at `uiScale` 1 / 2: 1 / 2; `(0,0)-(17,32)`: 0.5 |
| `bool SOLHoloMenu::IsValidRect(const FBox2f&)`, `float SOLHoloMenu::UiScale(float)` | The section 6.3 rect rule and `u` rule, exported so the builders use the geometry's own checks (42c review) |

Strength 0 end to end (D18): an anim opened at `s = 0`, advanced once and `Evaluate`d at `s = 0`, gives on R (title step
200): 19 Outline and 14 GapDot shapes at once, the backing at `FLAT_PANEL_ALPHA`, no Sweep, every line's and dot's
`PadPx` equal to `FRAME_FLAT_PAD_PX` (the Fill keeps `HUD_PANEL_QUAD_PAD_PX`, sizes row), and a hovered row's 8 Hover
pieces and 1 HoverDot in `SECONDARY` at `FRAME_INTENSITY`.

**`USOLHoloMenuSubsystem` (`UI/SOLHoloMenuSubsystem.h/.cpp`).** The testable part is two plain structs in the same
header that the subsystem owns and forwards to; a test makes them directly, or a bare
`NewObject<USOLHoloMenuSubsystem>()` that is never initialized (as `AnchorTest` does): no world, settings, MID or sounds.

| Item | Contract |
|---|---|
| `enum class ESOLHoloCue : uint8 { Open, Hover, Select, Count }` | Values equal `SOL::HoloMenu::CUE_OPEN / CUE_HOVER / CUE_SELECT`; `Count == UE_ARRAY_COUNT(CUES)` (`static_assert`) |
| `static float FSOLHoloMenuParamCache::SanitizeStrength(float raw)` | NaN 0 (as section 6), else `clamp(raw, HOLO_MENU_EFFECTS_MIN, HOLO_MENU_EFFECTS_MAX)`; +inf 2, -inf 0 |
| `bool FSOLHoloMenuParamCache::Update(float rawStrength, const FVector2f& viewSizePx)` | Returns true (a push is due) on the first call and whenever the sanitized strength or the view size differs exactly from the last pushed values; then stores them and increments `GetPushCount()`. Same values again: false, no count. A view size with a component non-finite or `<= 0` keeps the last pushed size. `GetStrength()` returns the last sanitized strength (`HOLO_MENU_EFFECTS_DEFAULT` before the first call) |
| `bool FSOLHoloCueGate::Request(ESOLHoloCue cue, double nowS)` | `cue >= Count`: false, nothing counted. Else increments `GetRequestCount(cue)` on every call. Accepted (true, increments `GetAcceptedCount(cue)`, stores `nowS`) when `nowS` is finite and it is the cue's first acceptance or `nowS - last >= CUES[cue].MinIntervalS`; otherwise false (NaN and a clock that went back are rejected, proposed). Cues are independent of each other. It takes no strength (D21) |
| `float USOLHoloMenuSubsystem::GetStrength() const` | The sanitized live `HoloMenuEffects`; `HOLO_MENU_EFFECTS_DEFAULT` with no settings subsystem (a bare subsystem) |
| `void UpdateMaterial(const FVector2f& viewSizePx)` | Feeds `GetStrength()` and the size to the cache; only when `Update` is true and the MID exists does it set `EFFECTS_STRENGTH`, `VIEW_WIDTH` and `VIEW_HEIGHT` (the two sizes only once a valid size has been stored, so a first call with an invalid size never pushes a zero size). With a MID it also sets `COMPOSITE` from `sol.HudComposite` whenever the cvar changed (the HUD layer's rule). Users call it through `UpdateMaterialForFrame`; `GetParamPushCount()` counts the cache's pushes (with or without a MID) |
| `void UpdateMaterialForFrame()` | The one per-frame entry of every user (the HUD's menu layer, each `SSOLHoloFrame` paint): the first call in a frame (`GFrameCounter`) feeds `UpdateMaterial` the game viewport's size (`GetViewSizePx`, the one size source, the HUD canvas size); later calls in the same frame do nothing. `GetMaterialViewSizePx()` returns the size the MID last got (zero until a valid one), which the frames read for their UI scale instead of asking the viewport again |
| `bool PlayCue(ESOLHoloCue cue, double nowS)` | Returns the gate's answer; counters as the gate's (`GetCueRequestCount`, `GetCueAcceptedCount`), whatever the strength (0 included), a missing asset, no world or no audio device. Plays only when accepted and the sound loaded, 2D at `CUES[cue].Volume` (`UGameplayStatics::PlaySound2D` as a UI sound, so it is heard while paused; `GetCuePlayedCount` counts the starts for the smoke). A bare subsystem never crashes |
| `UMaterialInstanceDynamic* GetMenuMaterial() const` | The menu MID the menu layer and the frames draw with; null on a bare subsystem or with the material missing |

**Smoke only, not unit tested** (`-SOLSmokeHoloMenu` scaffold, section 12):
- `SSOLHoloFrame` / `USOLHoloFrameWidget` paint: one custom-vertex draw with the menu MID per paint, the backing drawn
  under it in `BACKDROP_TINT`, window-space mapping; screenshot at strength 0, 1 and 2. The layout is set in the frame's
  local Slate units and scaled to pixels by the geometry scale; `TitleStepWidthPx` is the title text's local width
  alone, and the frame adds `FRAME_TITLE_STEP_PAD_PX * u` (a UMG menu does not know `u`). The frame's UI scale is
  `clamp(material view height / HUD_REFERENCE_HEIGHT_PX, 1, HUD_MAX_UI_SCALE)` **without** the HUD's Settings HUD
  scale multiplier, so a UMG frame scales like its UMG content (42c review; for the user's hand check). While Open and
  unchanged it keeps its build and, at an unchanged window placement, rewrites only the sweep quad's four vertices.
  Both widgets are volatile (`USOLHoloFrameWidget` sets `bIsVolatile`, or `SynchronizeProperties` would undo the Slate
  widget's own `ForceVolatile`). `sol.HudRenderer 0` is not followed by the frames (a UMG menu has no Canvas fallback).
- `ASOLFlightHud`'s menu layer: a second `SSOLHudShapeLayer` above the HUD layer with the menu MID, stamped like the
  HUD layer, drawing nothing when no menu or card is on it; `-SOLSmokeHud` green and the HUD screenshot unchanged.
- MID binding: the subsystem creates the MID from `HUD_SHAPE_MATERIAL` at `OnWorldBeginPlay`, the menu layer and the
  frames draw with it, and its `EFFECTS_STRENGTH` equals `HoloMenuEffects` after a Settings change.
- Cue loading and playback: the three assets load once (one warning per missing one); playback is counted, not heard.

New constants (proposed, `SOL::HoloMenu`): `FRAME_FLAT_PAD_PX` 1, `FRAME_MAX_TABS` = `SOL::Overlay::MAX_PROFILES` (5),
`CARD_MAX_SHAPES` 10.

## 5. Per-menu behaviour

| Menu | Open / close | Hover | Idle | Cues |
|---|---|---|---|---|
| **Radial** (HUD menu layer) | The rim arcs grow from each sector's middle to its full aperture (`FrameFraction`), then the highlighted sector's fill and the labels, title and reason fade up (`FillAlpha`). Close plays it backwards from the last drawn state | Highlighted sector change = hover: its rim arcs take `HoverGlow` (edge glow + flicker) on top of today's highlight | Jitter through the menu MID; no sweep (no rectangle to sweep) | Open at the press that opens it; hover on each sector change; select when an **enabled** action is confirmed (none on cancel or a disabled row) |
| **Overlay Menu with tabs** (UMG + `USOLHoloFrameWidget`; option (c) stepped broken-line frame + faint tint, D20; the copy embedded in Settings is not converted, D17) | The broken outline draws in from the title corner, then backdrop, fill, ruler ticks, accents, dividers, tab markers and the text (the content box's render opacity) fade up | The highlighted row (mouse or keys, today's highlight) gets Secondary brackets with a pointer line, glowing with the flicker (`HoverMark` `RowPointer`); a highlighted tab gets the brackets only (`BracketsOnly`, proposed). The selected profile tab keeps its tab marker (section 6.3) | Sweep across the panel + jitter | Open at Ctrl+O; hover on each highlight change; select on a row toggle or tab switch |
| **F3 speed panel** (UMG + frame; option (c) stepped broken-line frame + faint tint, D20) | As the Overlay Menu | The highlighted digit or unit field gets Secondary brackets with a short tick above and below (`CentreTicks`); a highlighted body row gets brackets with a pointer line (`RowPointer`, proposed). Keys and wheel move it; no mouse, D5 | Sweep + jitter | Open at F3; hover on each highlight move; select on each applied cap (digit step, list Enter) |
| **Info card** (HUD menu layer; frameless, D15) | Corner ticks grow from the corners, then tint and text fade up, when Info is chosen; reverse when the card goes away | None (no input) | Sweep + jitter | Open only |
| **Target card** (HUD menu layer; frameless, D15; the top-right target readout) | As the info card, when a target is selected / cleared. The "no target" line stays a plain HUD line | None | Sweep + jitter | None (proposed: a target change is frequent; not separately answered) |
| **Speed card** (HUD menu layer; frameless, D15) | Always shown in flight: no open / close event, so no draw-in and no cue (the card is there whenever the HUD is) | None | Sweep + jitter | None |
| **Date-and-anchor card** (HUD menu layer; frameless, D15) | As the speed card (always shown, also on the map) | None | Sweep + jitter | None |
| **Map list menu** (UMG + frame, D16; the same option (c) frame + tint as the Overlay Menu, D20, to be confirmed at the hand check) | As the Overlay Menu | The highlighted row (mouse, as today) gets brackets with a pointer line (`RowPointer`) | Sweep + jitter | Open, hover, select on an enabled action (as the radial) |

**Card look (D15).** The four cards are frameless: a glowing L-shaped corner tick at each corner of the content
rectangle (`CARD_TICK_PX` long) and the faint dark tint behind it, no outline. Reference:
[`card-frameless.png`](../Images/hologram-card-samples/card-frameless.png).

**Menu frame look (D20).** The Overlay Menu, the speed panel and the map list menu use option (c) with the faint dark
tint: a stepped broken-line outline (raised title step on the left, cut corners, a stepped-in bottom-right corner),
fixed gaps with an end dot on each side, six ruler ticks on the left edge, two inner corner accents and a broken title
underline, all thin cyan glow lines; the faint dark tint behind the panel is the `BackdropAlpha` path (section 6.1).
Inside the panel the Overlay Menu has a dashed column divider between the tabs and the rows (open beside the selected
tab), tab markers and a broken divider above the hints; the speed panel has a stepped broken divider between the
steppers and the body list. References: [`overlay-c-tint.png`](../Images/hologram-frame-samples/overlay-c-tint.png),
[`speed-c-tint.png`](../Images/hologram-frame-samples/speed-c-tint.png). Exact geometry in section 6.3. The map list
menu taking this frame was proposed in the Q9 wording; the user confirms it at the hand check.

**Strength 0 (D18).** Every menu and card becomes a plain flat panel: the open / close is instant, the backdrop is a
flat dark panel (`FLAT_PANEL_ALPHA`, readable without glow), outlines and ticks are thin lines with no glow halo, and
there is no sweep, jitter, flicker or hover glow. The option (c) frame draws whole at once (gaps, dots, ruler ticks,
accents, dividers and tab markers included) as thin flat lines. The highlighted row, tab, digit or sector keeps
today's Secondary colour, and its hover brackets stay as thin flat Secondary lines (proposed, section 6.3), so the
menus stay fully usable. The sound cues still play at strength 0 (D21).

**Overlay Menu and F3 panel details (42e / 42f amendment, written after the code at the 42e review).** These refine
the two table rows above:
- **Overlay Menu tab hover is the mouse hover.** A tab gets its `BracketsOnly` mark only while the mouse is over it.
  The keys (Up / Down, PgUp / PgDn) move the row highlight as today and drop the tab hover, so the mark goes back to
  the highlighted row. While a tab is hovered its mark wins over the row's. The viewed profile's tab keeps its selected
  tab marker either way (section 6.3). A resting mouse does not take the highlight back from the keys: mouse moves with no movement are ignored for the row highlight (Q14, answered (a)).
- **The cycle key inside the Overlay Menu plays the select cue** (RShift+O switches the active and viewed profile, like
  a tab switch). A tab click or PgUp / PgDn plays it only when the viewed profile changed.
- **No hover cue on open.** The initial highlight (row, digit or unit field) is set while the menu is Opening, and
  `ShouldPlayHoverCue` needs the Open phase (section 6.2), so an open plays the open cue only.
- **The F3 panel keeps today's full-screen dim** (the root border behind the panel, which also catches the wheel
  anywhere on screen). Its alpha is now today's dim alpha times `FillAlpha`, so it fades in and out with the fill and
  the content.
- **A unit change on the F3 panel plays no cue.** Cycling the unit changes how the cap reads, not the cap itself, so it
  is not an applied cap (no select cue), and the highlight does not move (no hover cue).
- **Review fixes (42e / 42f reviews).** The mouse leaving a tab hands the mark back to the highlighted row with no
  hover cue when that row did not change (not a highlight change). An F3 body row scrolled out of the list's view (a
  wheel over the list scrolls it without moving the selection) has no mark and asks for no cue; its rect comes from the
  list's geometry, the row height and the scroll offset (a culled row's own geometry is stale). A menu removed at
  teardown (`Dismiss`: end of play, unpossess) drops its frame to Closed, so the next open draws in with its open cue.

**Map list menu details (42g amendment, written with the code).** These refine the map list menu row above:
- **Layout.** `MakeOverlayLayout` with no tab column: the title step sized to the object's name and the hint divider
  between the rows and the key hint; no column divider, no tab markers. The panel's own dark fill and the highlighted
  row's band are gone (the frame draws the tint and the mark). Room for the frame inside the panel (proposed, tuned
  from screenshots): 18 local units of padding, 8 more on the left (the row brackets clear the top-left accent), 16
  more on the right, a 28-unit gap under the title (the rows start under the title underline) and 12 above the hint.
- **Input as before (D5).** Every mouse move over a row takes the highlight, as before #42; the Overlay Menu's
  zero-delta filter (Q14) is not used here.
- **Close.** Choosing an action or Esc / an outside click: the pawn's close calls `BeginClose` (input back to the map
  at once, the close plays). A right click during the close reopens it, reversing from its progress at the new
  place (the UMG rule of section 4.5). When the map itself closes (a Jump To from the list, or the map's close key
  later) the list is dismissed at once with it (`Dismiss`, frame to Closed), so no list outlives the map.
- **Cues.** Open on each open; hover on each highlight change while open (keys, the mouse, or a click on a disabled
  row); select only when an enabled action runs (Enter / Space / click), none on a disabled row, Esc or an outside
  click.

All frames use the HUD palette (`SOL::HudPalette::PRIMARY` lines, `SECONDARY` for the hovered / highlighted item,
dim rows as today). Text stays the existing glow text (on the menu layer for HUD-drawn menus, through `MenuText`, so it
paints above the menu's backing and fill; 42c review) or the UMG `UTextBlock`s
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

### 6.3 Frame geometry (option (c) + tint, D20)

All functions here are pure and renderer-free. They write into fixed-size output arrays the caller owns (no heap
allocation, no `TArray`), never write past the stated capacity, and return how many entries they wrote. Shared types:

```
struct FSOLHoloSegment { FVector2f A; FVector2f B; };      // One straight line piece, drawn A -> B
struct FSOLHoloGap { float AtFraction; float LengthPx; };   // A fixed outline gap: start as a fraction of the
                                                            // perimeter, length in px at 1x
enum class ESOLHoloHoverMark : uint8 { BracketsOnly, RowPointer, CentreTicks };
```

**Common rules.** Screen space, y down, px of the 720 px canvas times the UI scale (like `CARD_TICK_PX`).
- A rect is **valid** when all four coordinates are finite and `w = Max.X - Min.X > 0`, `h = Max.Y - Min.Y > 0`.
  An invalid rect gives a count of 0 (and a perimeter of 0).
- `uiScale` that is NaN, infinite or `<= 0` reads 1.
- **Effective scale of the outline:** `k = min(uiScale, w / FRAME_MIN_W_PX, h / FRAME_MIN_H_PX)`, so a rect too small
  for the stepped shape shrinks every outline length uniformly instead of folding over. With it:
  `a = FRAME_CUT_TL_PX * k` (top-left cut), `s = FRAME_STEP_PX * k` (title step and bottom-right step-in),
  `c = FRAME_CUT_PX * k` (top-right and bottom-left cuts), `n = FRAME_NOTCH_PX * k` (height of the bottom-right
  step-in). `FRAME_MIN_W_PX = FRAME_CUT_TL_PX + FRAME_STEP_PX + FRAME_CUT_PX` and
  `FRAME_MIN_H_PX = FRAME_STEP_PX + FRAME_CUT_PX + FRAME_NOTCH_PX`.
- **Title step width** `T = clamp(titleStepWidthPx, a, w - s - c)`; NaN reads `a`. The widget passes its title text
  width plus `FRAME_TITLE_STEP_PAD_PX * uiScale`; a menu with no title passes 0.
- A piece shorter than 1e-3 px is never written (degenerate pieces are skipped, not counted).

**The stepped outline.** With `x0, y0, x1, y1` the rect's corners, the closed outline is these 11 points, clockwise
on screen, back to `P0` (sample: `overlay-c-tint.png`, `speed-c-tint.png`):

| Point | Position | Edge to the next point |
|---|---|---|
| `P0` | `(x0, y0 + a)` | top-left cut, `a * sqrt(2)` |
| `P1` | `(x0 + a, y0)` | raised title section, `T - a` |
| `P2` | `(x0 + T, y0)` | title step down at 45 degrees, `s * sqrt(2)` |
| `P3` | `(x0 + T + s, y0 + s)` | lower top edge, `w - c - T - s` |
| `P4` | `(x1 - c, y0 + s)` | top-right cut, `c * sqrt(2)` |
| `P5` | `(x1, y0 + s + c)` | right edge, `h - n - s - c` |
| `P6` | `(x1, y1 - n)` | step in at 45 degrees, `s * sqrt(2)` |
| `P7` | `(x1 - s, y1 - n + s)` | inset right edge, `n - s` |
| `P8` | `(x1 - s, y1)` | bottom edge (square corner at `P8`), `w - s - c` |
| `P9` | `(x0 + c, y1)` | bottom-left cut, `c * sqrt(2)` |
| `P10` | `(x0, y1 - c)` | left edge back to `P0`, `h - c - a` |

The arc coordinate `u` is the distance along the outline from `P0`, clockwise, in `[0, L]` (`L` = perimeter).

**Gaps.** Gap `i` of `FRAME_GAPS` (sorted by `AtFraction`) covers `u` in
`[AtFraction * L, AtFraction * L + LengthPx * k]`, clipped at `L`; gaps that overlap or touch merge into one. The same
table is used by every option (c) frame, so the gaps sit at the same relative places on every menu.

**Draw-in.** At a draw-in fraction `f = clamp(fraction, 0, 1)` (NaN reads 0) two strokes leave `P0`, the title
corner: one clockwise (up the top-left cut, along the title and the top edge, down the right edge), one
counter-clockwise (down the left edge, along the bottom). The drawn arc is `D(f) = [0, f * L / 2]` together with
`[L - f * L / 2, L]`; at `f = 1` the strokes meet at `u = L / 2` and the whole outline is drawn. The visible outline is
`D(f)` minus the gaps.

| Function | Contract |
|---|---|
| `int32 FrameOutline(const FBox2f& rect, float titleStepWidthPx, float uiScale, FVector2f outPoints[FRAME_OUTLINE_POINTS])` | Writes `P0`-`P10` of the table above (closed implicitly back to `P0`; `P0` is not repeated). Returns `FRAME_OUTLINE_POINTS` (11) for a valid rect, 0 otherwise. Points may coincide when the rect is at the minimum size; the count stays 11 |
| `float FramePerimeter(const FBox2f& rect, float titleStepWidthPx, float uiScale)` | `L`, the length of that closed outline: `2 (w + h) - (2a + 4c + 4s) + sqrt(2) (a + 2c + 2s)` with the effective `a, c, s` (independent of `T` and `n`). 0 for an invalid rect |
| `int32 FrameDrawSegments(const FBox2f& rect, float titleStepWidthPx, float uiScale, float fraction, FSOLHoloSegment outSegments[FRAME_MAX_SEGMENTS])` | The visible outline at the draw-in fraction: its maximal straight pieces, split at every outline point `P0`-`P10` and at `u = L / 2` (where the two strokes meet; each stroke is cut into pieces on its own), ordered by increasing `u` of their start, each running clockwise (`A` at the smaller `u`). The summed length equals `f * L` minus the gap length inside `D(f)` (within 1e-3 px); at `f = 1` it is `L` minus the merged gap length. 0 at `f <= 0`, NaN or an invalid rect. Every piece lies on the outline. Worst case `11 + FRAME_GAP_COUNT + 1` pieces; never more than `FRAME_MAX_SEGMENTS` (24) |
| `int32 FrameGapDots(const FBox2f& rect, float titleStepWidthPx, float uiScale, float fraction, FVector2f outDots[FRAME_MAX_DOTS])` | The end dots: one dot at each end of each merged gap whose `u` lies inside `D(f)` (ends included), at the outline point of that `u`, ordered by increasing `u`. A dot appears as a stroke reaches it. No dot at the stroke fronts and none at `P0` unless a gap ends there. At `f = 1` with no merged gaps: `2 * FRAME_GAP_COUNT` (14). 0 at `f <= 0`, NaN or an invalid rect; never more than `FRAME_MAX_DOTS` (14). Drawn as filled dots of radius `FRAME_DOT_RADIUS_PX * k` |
| `int32 FrameRulerTicks(const FBox2f& rect, float uiScale, FSOLHoloSegment outSegments[FRAME_RULER_TICK_COUNT])` | The left-edge ruler: tick `i` (from 0) at `y_i = y0 + FRAME_RULER_START * h + i * FRAME_RULER_SPACING_PX * k`, from `(x0, y_i)` inward to `(x0 + FRAME_RULER_TICK_PX * k, y_i)`. Ticks are written in order and stop at the first `y_i > y1 - c` (past the left edge's straight part). Returns the count (6 on a normal panel), 0 for an invalid rect |
| `int32 FrameAccents(const FBox2f& rect, float titleStepWidthPx, float uiScale, FSOLHoloSegment outSegments[FRAME_ACCENT_COUNT])` | Up to 4 pieces, in this order, each skipped when it would have no length or run the wrong way (its end not beyond its start: downward for 1, leftward for 2, rightward for 3 and 4); the rest keep their order: (1) the top-left inner accent `(x0 + 6k, y0 + 18k)` -> `(x0 + 6k, min(y0 + 70k, y1 - c))`; (2) the bottom-right inner accent `(x1 - s - 6k, y1 - 6k)` -> `(max(x1 - s - 80k, x0 + c), y1 - 6k)`; (3) the title underline `(x0 + 12k, y0 + 34k)` -> `(x0 + T - 40k, y0 + 34k)`; (4) its short dash `(x0 + T - 32k, y0 + 34k)` -> `(x0 + T - 20k, y0 + 34k)`; pieces 3 and 4 are both skipped when piece 3 would have no length (a short or missing title). The numbers are the `FRAME_ACCENT_*` and `FRAME_TITLE_*` constants (section 7). Pieces 1-2 draw at the frame intensity, 3-4 dim (`FRAME_DIM_ALPHA`). Returns the count, 0 for an invalid rect |
| `int32 HoverMark(const FBox2f& rect, ESOLHoloHoverMark mark, float uiScale, FSOLHoloSegment outSegments[HOVER_MAX_SEGMENTS], FVector2f& outDot, int32& outDotCount)` | The hovered item's mark, in Secondary. `rect` is the item's rectangle inflated by `HOVER_PAD_X_PX` / `HOVER_PAD_Y_PX` (times `uiScale`, done by the widget). With arm `e = min(HOVER_BRACKET_ARM_PX * uiScale, w / 2)`: left bracket `(x0 + e, y0) -> (x0, y0)`, `(x0, y0) -> (x0, y1)`, `(x0, y1) -> (x0 + e, y1)`; right bracket mirrored, `(x1 - e, y0) -> (x1, y0)`, `(x1, y0) -> (x1, y1)`, `(x1, y1) -> (x1 - e, y1)` (6 pieces, in this order). `RowPointer` adds, with `m = (y0 + y1) / 2`, `p = HOVER_POINTER_RUN_PX`, `r = HOVER_POINTER_RISE_PX` (times `uiScale`): `(x1, m) -> (x1 + p, m)`, `(x1 + p, m) -> (x1 + p + r, m - r)` and one dot at `(x1 + p + r + d, m - r - d)`, `d = HOVER_POINTER_DOT_PX * uiScale` (8 pieces, 1 dot). `CentreTicks` adds, with `mx = (x0 + x1) / 2`, `t = HOVER_CENTRE_TICK_PX * uiScale`: `(mx, y0) -> (mx, y0 - t)` and `(mx, y1) -> (mx, y1 + t)` (8 pieces, 0 dots). `BracketsOnly`: 6 pieces, 0 dots. An invalid rect gives 0 pieces and `outDotCount = 0`. Never more than `HOVER_MAX_SEGMENTS` (8) |
| `int32 TabMarker(const FBox2f& tabRect, float dividerX, bool bSelected, float uiScale, FSOLHoloSegment outSegments[TAB_MAX_SEGMENTS], FVector2f& outDot, int32& outDotCount)` | The Overlay Menu's profile tab marker. `D = max(dividerX, x1)` (NaN reads `x1`). **Not selected:** a dim left bar with short arms, `g = min(TAB_BRACKET_ARM_PX * uiScale, w)`, `i = min(TAB_BRACKET_INSET_PX * uiScale, h / 2)`: `(x0 + g, y0 + i) -> (x0, y0 + i)`, `(x0, y0 + i) -> (x0, y1 - i)`, `(x0, y1 - i) -> (x0 + g, y1 - i)`; 3 pieces, 0 dots. **Selected:** an outline open on the right that runs into the column divider, with a cut top-left corner `q = min(TAB_CUT_PX * uiScale, w, h / 2)`: `(D, y0) -> (x0 + q, y0)`, `(x0 + q, y0) -> (x0, y0 + q)`, `(x0, y0 + q) -> (x0, y1)`, `(x0, y1) -> (D, y1)`; 4 pieces, in Primary, plus one dot at `(x0 - TAB_DOT_OFFSET_PX * uiScale, (y0 + y1) / 2)`. Invalid rect: 0 pieces, 0 dots. Never more than `TAB_MAX_SEGMENTS` (4) |
| `int32 DashedDivider(FVector2f top, float bottomY, float openTopY, float openBottomY, float uiScale, FSOLHoloSegment outSegments[DIVIDER_MAX_DASHES])` | The Overlay Menu's dim vertical column divider between the tabs and the rows. Dash `j` runs from `y = top.Y + j * DIVIDER_PERIOD_PX * uiScale` down `DIVIDER_DASH_PX * uiScale`, clipped at `bottomY`, all at `x = top.X`. A dash that overlaps `[openTopY, openBottomY]` (ends included) is left out whole; that is where the selected tab joins the divider. No opening when `openTopY > openBottomY` or either is NaN. 0 when `bottomY <= top.Y` or a coordinate is not finite. Dashes are written top to bottom and stop at `DIVIDER_MAX_DASHES` (40); the rest are dropped |
| `int32 BrokenDivider(FVector2f start, float endX, float stepAtPx, float stepPx, float uiScale, FSOLHoloSegment outSegments[DIVIDER_MAX_SEGMENTS])` | A dim horizontal divider with two fixed gaps (the Overlay Menu's line above the hints, the speed panel's line between the steppers and the body list). The path runs from `start` to `x = endX` at `y = start.Y`; when `stepPx > 0` it rises at `x = start.X + stepAtPx` by `stepPx` (toward smaller y) at 45 degrees and carries on at `y = start.Y - stepPx`, with `stepAtPx` clamped to `[0, endX - start.X - stepPx]`. The gaps of `DIVIDER_GAPS` are placed like the outline's (fraction of the path length, length times `uiScale`); no end dots. Pieces are split at the path's corners, ordered left to right. 0 when `endX <= start.X` or a value is not finite; `stepPx` NaN or `<= 0` means no step. Never more than `DIVIDER_MAX_SEGMENTS` (6) |
| `void RadialArcDrawIn(float halfApertureRad, float frameFraction, float& outHalfApertureRad)` | A radial sector's rim arc grows from its middle: `halfApertureRad * clamp(frameFraction, 0, 1)` |
| `int32 CardCornerTicks(const FBox2f& rect, float tickPx, float fraction, FSOLHoloSegment outSegments[8])` | The frameless card look (D15): two segments per corner, each starting at the corner and running along its edge for `tickPx * clamp(fraction, 0, 1)` (NaN reads 0), with `tickPx` clamped to `[0, min(w, h) / 2]`. Returns 8, or 0 for an invalid or empty rect or a zero length. Mirror-symmetric about both centre lines |

**How a frame uses them (the widget, not the pure maths).** Per paint, from the menu's `FSOLHoloMenuLook`:
- The outline is `FrameDrawSegments` and `FrameGapDots` at `FrameFraction`, in Primary at `FRAME_INTENSITY`, the glow
  halo scaled by `GlowScale(s)`.
- The ruler ticks, the accents, the dividers and the tab markers are drawn whole, their alpha times `FillAlpha`, so
  they fade up with the fill and the text after the outline has drawn in (proposed).
- The hover mark: for `s > 0`, in Secondary at `HoverIntensity` (ramp and flicker of section 6.1). At `s = 0`
  `HoverGlow` is 0, so the mark is drawn as thin flat Secondary lines at `FRAME_INTENSITY`, with no ramp or flicker
  (proposed): the highlight stays visible without glow (D18).
- **The faint dark tint** is the existing `BackdropAlpha` path (section 6.1): one backing box, the frame rect inset by
  `FRAME_TINT_INSET_PX * k`, a plain rectangle (not the stepped shape, as in the sample), alpha
  `BackdropAlpha(s, FillAlpha)`.
- **Strength 0 (D18).** `Progress` is 1 at once (section 6.2), so `FrameFraction` and `FillAlpha` are 1: the whole
  broken outline, every gap dot, the ruler ticks, accents, dividers and tab markers draw at once, as thin flat lines
  (`GlowScale(0) = 0`), on the flat `FLAT_PANEL_ALPHA` backdrop. The gaps and dots are part of the shape, not an
  effect, so they stay. No draw-in, sweep, jitter or flicker.

**Layout helpers (parts 42e / 42f amendment, `UI/SOLHoloFrameBuild.h`, namespace `SOLHoloFrameBuild`).** Written
after the code, at the 42e review's request, so a separate author can test them from this contract. They turn a menu's
arranged content into the layout fields of an `FSOLHoloFrameSpec` (section 4.6) and give the rect a hover mark goes
around. Pure, no heap, no Slate. Coordinates are the frame's local units with the menu panel's top-left as the origin,
y down; the frame scales them to px when it paints. "Valid" is the section 6.3 rect rule (`SOLHoloMenu::IsValidRect`);
"finite" means not NaN and not infinite; "invalid box" is `FBox2f(ForceInit)` (`bIsValid` false). Items marked
(proposed, tuned from screenshots) are choices the code made that D20, the samples and sections 5 and 6.3 did not
settle; they were tuned against `overlay-c-tint.png` and `speed-c-tint.png`.

```
struct FSOLHoloOverlayLayoutInput { float TitleWidth = 0;          // the title text's own width
    FBox2f TabColumn; FBox2f RowColumn;                          // invalid = missing
    int32 TabCount = 0; FBox2f TabRects[FRAME_MAX_TABS];         // each tab's rect; all invalid by default
    int32 SelectedTab = INDEX_NONE;                              // the viewed profile's tab
    float HintTopY = NaN; };                                     // top of the key hint; NaN = no hint
struct FSOLHoloSpeedLayoutInput { float TitleWidth = 0;
    FBox2f StepperSection; FBox2f ListSection;                   // invalid = missing
    float LocalPerPx = 1; };                                     // local units per px of the 720 px canvas
```

**Written fields.** Both layout functions write exactly these fields of `out`, on every call and whatever the input:
`TitleStepWidthPx`, `bColumnDivider`, `DividerTop`, `DividerBottomY`, `OpenTopY`, `OpenBottomY`, `TabDividerX`,
`TabCount`, all `FRAME_MAX_TABS` entries of `TabRects`, `SelectedTab`, `bBrokenDivider`, `BrokenStart`, `BrokenEndX`,
`BrokenStepAtPx` and `BrokenStepPx`. They never write `Rect`, `UiScale`, `HoverItemRect` or `HoverMark` (the frame's
own). So an `out` that already holds other values (a dirty spec) gives the same written fields as a fresh one. A piece
that is absent writes its "absent" values below, never leftovers.

| Function | Contract |
|---|---|
| `void MakeOverlayLayout(const FSOLHoloOverlayLayoutInput& in, FSOLHoloFrameSpec& out)`: title step | `TitleStepWidthPx = in.TitleWidth` when it is finite and `> 0`; else 0 (0, negative, NaN, infinite). The raw text width: the frame adds `FRAME_TITLE_STEP_PAD_PX * u` itself (section 4.6) |
| `MakeOverlayLayout`: tabs | `n = clamp(in.TabCount, 0, FRAME_MAX_TABS)`. `out.TabCount = n`; `out.TabRects[i] = in.TabRects[i]` for `i < n`, copied as given (an invalid one too); every `i >= n` is an invalid box, even when `in.TabRects[i]` is valid. `out.SelectedTab = in.SelectedTab` when it is in `[0, n)`, else `INDEX_NONE` |
| `MakeOverlayLayout`: column divider | **Present** (`bColumnDivider` true) when `TabColumn` and `RowColumn` are both valid and `RowColumn.Min.X >= TabColumn.Max.X` (touching allowed, overlapping not). Then `xd = (TabColumn.Max.X + RowColumn.Min.X) / 2`, halfway between the columns (proposed, tuned from screenshots); `DividerTop = (xd, min(TabColumn.Min.Y, RowColumn.Min.Y))`, `DividerBottomY = max(TabColumn.Max.Y, RowColumn.Max.Y)` (the columns' full height), `TabDividerX = xd` (the selected tab's marker runs into the divider). **Absent** (a column missing, or overlapping columns): `bColumnDivider` false, `DividerTop (0, 0)`, `DividerBottomY 0`, `TabDividerX 0` (so `TabMarker` reads the tab's own right edge, section 6.3), and no opening |
| `MakeOverlayLayout`: opening at the viewed tab | `OpenTopY = TabRects[SelectedTab].Min.Y`, `OpenBottomY = TabRects[SelectedTab].Max.Y` when the column divider is present, `out.SelectedTab != INDEX_NONE` and that tab's rect is valid. Otherwise no opening: `OpenTopY 1`, `OpenBottomY 0` (the `DashedDivider` "top above bottom" rule). A valid selection with an invalid tab rect gives no opening; `SelectedTab` keeps its value (that tab's marker then draws nothing, by the `TabMarker` invalid-rect rule) |
| `MakeOverlayLayout`: hint divider | `body` = the union of the valid columns (one missing column still gives a body). **Present** (`bBrokenDivider` true) when at least one column is valid, `HintTopY` is finite and `HintTopY > body.Max.Y` (strictly). Then `yh = (body.Max.Y + HintTopY) / 2`, halfway between the columns' bottom and the hint (proposed, tuned from screenshots); `BrokenStart = (body.Min.X, yh)`, `BrokenEndX = body.Max.X`, from the tab column's left to the row column's right (proposed, tuned from screenshots). It never steps: `BrokenStepAtPx` and `BrokenStepPx` are always 0. **Absent** (no valid column, `HintTopY` NaN or infinite, or the hint at or above the columns' bottom): `bBrokenDivider` false, `BrokenStart (0, 0)`, `BrokenEndX 0`. The field's default is NaN, no hint (proposed: the 42e code has 0 with a "NaN: no hint" comment, a review nit to align; for columns at `y >= 0` both defaults give no hint divider) |
| `FBox2f TextItemRect(const FBox2f& slotRect, float textWidth, float indentWidth)` | The rect a hover mark goes around for a text item (before the frame's `HOVER_PAD_X_PX` / `HOVER_PAD_Y_PX` inflation, section 4.6). Invalid box when `slotRect` is not valid or `textWidth` is not finite. Indent `i = indentWidth` when finite and `> 0`, else 0. `left = slotRect.Min.X + i`, `right = slotRect.Min.X + min(textWidth, slotRect width)` (the text's own width, at most the slot's; proposed, tuned from screenshots). Invalid box when `right <= left` (a zero or negative text width, an indent as wide as the text or wider, an indent past the slot). Else `(left, slotRect.Min.Y) - (right, slotRect.Max.Y)`: the slot's full height |
| `void MakeSpeedLayout(const FSOLHoloSpeedLayoutInput& in, FSOLHoloFrameSpec& out)`: title step, tabs | Title step as `MakeOverlayLayout`. No column divider and no tabs ever: `bColumnDivider` false, `DividerTop (0, 0)`, `DividerBottomY 0`, `OpenTopY 1`, `OpenBottomY 0`, `TabDividerX 0`, `TabCount 0`, every `TabRects` entry an invalid box, `SelectedTab INDEX_NONE` |
| `MakeSpeedLayout`: stepped divider | `lp = in.LocalPerPx` when finite and `> 0`, else 1. **Present** (`bBrokenDivider` true) when both sections are valid and `ListSection.Min.Y > StepperSection.Max.Y` (strictly: touching or overlapping sections give none). With `t = SPEED_DIVIDER_STEP_PX * lp` and `m = (StepperSection.Max.Y + ListSection.Min.Y) / 2`: `BrokenStart = (min(StepperSection.Min.X, ListSection.Min.X), m + t / 2)`, `BrokenEndX = max(StepperSection.Max.X, ListSection.Max.X)` (across both sections), `BrokenStepAtPx = SPEED_DIVIDER_STEP_AT_PX * lp`, `BrokenStepPx = t`. `BrokenDivider` rises toward smaller y, so the left level is `m + t / 2` and the right level `m - t / 2`: the two levels straddle the midpoint of the gap (proposed, tuned from screenshots; the sample's left part is the lower one). The levels are not clamped to the gap: a gap narrower than `t` still gives the divider (proposed). The step position's own clamp is `BrokenDivider`'s (section 6.3). **Absent:** `bBrokenDivider` false, `BrokenStart (0, 0)`, `BrokenEndX 0`, `BrokenStepAtPx 0`, `BrokenStepPx 0` |

**How the widgets use them (smoke only, not unit tested).** Each menu rebuilds its layout from its content's cached
geometry over the 2 frames after a change (the content settles first). The hovered item, its rect and its mark:

| Menu item | Hover item id | Rect given to the frame | Mark |
|---|---|---|---|
| Overlay Menu row (the highlighted row, keys or mouse; none for a hidden row) | the row index | `TextItemRect(row text slot, its text width, the child indent's width for an indented row, else 0)`, so the brackets go around `[x] Rings`, not the indent (as in the sample) | `RowPointer` |
| Overlay Menu tab (only while the mouse is over it; wins over the row) | `ROW_WIDGET_COUNT + tab` | `TextItemRect(tab text slot, its text width, 0)` | `BracketsOnly` |
| F3 digit | the digit's slot (`MaxDigit - HighlightedDigit`) | `TextItemRect(digit text, its width, 0)`, then narrowed by `6 * LocalPerPx` per side, keeping at least 1 local unit of half width, so the brackets hug the glyph and not the next digits (proposed, tuned from screenshots) | `CentreTicks` |
| F3 unit field | the digit count | as the digit | `CentreTicks` |
| F3 body row (list focused) | `digit count + 1 + list index` | `TextItemRect(body row text, its width, 0)` | `RowPointer` |

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
Worked example for cases 8-13 (computed from the contracts with the section 7 values; distances within 1e-2 px):
rect **R** = `(0, 0)-(400, 300)`, title step width 200, `uiScale` 1, so `k = 1` and `T = 200`.

8. `FrameOutline` / `FramePerimeter`: R gives 11 points, `P0` to `P10` = `(0,10) (10,0) (200,0) (210,10) (386,10)
   (400,24) (400,260) (390,270) (390,300) (14,300) (0,286)`, and `L = 1284 + 58 sqrt(2) = 1366.024`, equal to the sum
   of the point-to-point distances; `L` does not change with the title step width (try 50 and 300). `uiScale` 2 on R:
   `k = 2`, `L = 1332.049`. Title step clamping: 0 and NaN give `P1 == P2`; 1000 gives `P2.X = w - s - c = 376`.
   Small rect `(0,0)-(17,32)` at `uiScale` 1: `k = 0.5`, still 11 points, `(0,5) (5,0) (5,0) (10,5) (10,5) (17,12)
   (17,12) (12,17) (12,32) (7,32) (0,25)`, `L = 81.012`. Invalid rects (zero or negative width or height, NaN or
   infinite coordinate): 0 points and `L = 0`. `uiScale` 0, -1 and NaN behave as 1.
9. `FrameDrawSegments` on R: `f = 1`: 19 pieces, summed length `L - 96 = 1270.024`; `f = 0.5`: 9 pieces, summed length
   661.012; `f = 0.25`: 4 pieces, 329.506; `f <= 0`, NaN, or an invalid rect: 0; `f = 1.5` equals `f = 1`. For every
   `f` in 0.05 steps: the count never exceeds `FRAME_MAX_SEGMENTS`, every piece is at least 1e-3 px long, both ends of
   every piece lie on the outline (distance to the nearest outline edge below 1e-3), the pieces are ordered by `u`
   with no two overlapping, the first piece starts at `P0` `(0, 10)` and runs toward `P1`, and the summed length is
   non-decreasing in `f`. No piece crosses an outline point (each piece lies on one edge).
10. `RadialArcDrawIn`: 0, 1, clamping.
11. `CardCornerTicks`: 8 segments each `tickPx` long at fraction 1, half at 0.5, 0 at 0 or NaN; every segment starts
    at a corner of the rect; tick clamped on a small rect; invalid rect 0; symmetry.
12. `FrameGapDots` on R: `f = 1`: 14 dots, the first two at `(264.921, 10)` and `(274.921, 10)` (the first gap, on the
    lower top edge), the last two at `(0, 146.602)` and `(0, 134.602)` (on the left edge); `f = 0.5`: 4 dots;
    `f = 0.25`: 2 dots (the last gap's, on the left edge); `f = 0`, NaN, invalid rect: 0. Every dot lies on the
    outline; every dot at `f = 1` is an end of a gap, so the summed piece length of case 9 plus the summed gap lengths
    (pairs of dots along `u`) equals `L`. Small rect `(0,0)-(17,32)`: gaps merge, the count is even, at most 14, and
    below 14.
13. `FrameRulerTicks` and `FrameAccents` on R: 6 ticks at `y = 150, 159, ..., 195`, each from `(0, y)` to `(4, y)`;
    rect `(0,0)-(40,70)`: 3 ticks at `y = 35, 44, 53`; invalid rect 0. Accents on R: 4 pieces, `(6,18)->(6,70)`,
    `(384,294)->(310,294)`, `(12,34)->(160,34)`, `(168,34)->(180,34)`; with title step width 0: 2 pieces (the two
    corner accents only); invalid rect 0.
14. `HoverMark` on `(100,50)-(200,70)`, `uiScale` 1: `RowPointer` gives 8 pieces, in order `(105,50)->(100,50)`,
    `(100,50)->(100,70)`, `(100,70)->(105,70)`, `(195,50)->(200,50)`, `(200,50)->(200,70)`, `(200,70)->(195,70)`,
    `(200,60)->(234,60)`, `(234,60)->(242,52)`, and 1 dot at `(244,50)`; `CentreTicks` gives the same 6 bracket pieces
    plus `(150,50)->(150,42)` and `(150,70)->(150,78)`, 0 dots; `BracketsOnly` gives the 6 bracket pieces, 0 dots. A
    rect 6 px wide: arm 3. `uiScale` 2 doubles the arm, pointer and tick lengths. Invalid rect: 0 pieces, dot count 0.
15. `TabMarker` on `(78,160)-(228,185)`, divider at 238, `uiScale` 1: not selected, 3 pieces `(84,162)->(78,162)`,
    `(78,162)->(78,183)`, `(78,183)->(84,183)`, 0 dots; selected, 4 pieces `(238,160)->(84,160)`, `(84,160)->(78,166)`,
    `(78,166)->(78,185)`, `(78,185)->(238,185)`, 1 dot at `(74,172.5)`; divider NaN or 100 (left of the tab's right
    edge) reads 228; invalid rect 0.
16. `DashedDivider` from `(0,0)` to 100, `uiScale` 1: no opening (`openTopY` 1 > `openBottomY` 0, or NaN): 8 dashes,
    `0-9, 14-23, 28-37, 42-51, 56-65, 70-79, 84-93, 98-100` (the last clipped); opening `[30, 50]`: 6 (the 28-37 and
    42-51 dashes left out); an opening that only touches a dash end (`[23, 27]`) still leaves that dash out; bottom
    1000: exactly `DIVIDER_MAX_DASHES` (40) written; bottom at or above the top: 0.
17. `BrokenDivider` from `(0,0)` to 200, `uiScale` 1: no step, 3 pieces `(0,0)->(66,0)`, `(74,0)->(150,0)`,
    `(166,0)->(200,0)`; step at 100 by 6: path length `194 + 6 sqrt(2) = 202.485`, 5 pieces `(0,0)->(66.820,0)`,
    `(74.820,0)->(100,0)`, `(100,0)->(106,-6)`, `(106,-6)->(149.379,-6)`, `(165.379,-6)->(200,-6)`; step NaN or 0 is
    the no-step case; `endX <= start.X`: 0; never more than `DIVIDER_MAX_SEGMENTS`.
18. Capacity and allocation: every function in 6.3 called with an output array of exactly its stated capacity, over a
    sweep of rects (1x1 up to 4000x2000), fractions (-1, 0, NaN, 0.01-1 in 0.01 steps, 2) and `uiScale` (0.5, 1, 2,
    NaN) never writes past the capacity (guard entries after the array stay untouched) and returns a count within
    `[0, capacity]`.

**Layout helpers (42e / 42f amendment).** Cases 19-26 go in `Tests/HoloOverlayLayoutTest.cpp`
(`SOLTest.HoloOverlayLayout.*`), cases 27-29 in `Tests/HoloSpeedLayoutTest.cpp` (`SOLTest.HoloSpeedLayout.*`). Exact
values (all inputs are representable floats). Worked input **O** for `MakeOverlayLayout`: `TitleWidth` 150,
`TabColumn (10,50)-(110,200)`, `RowColumn (130,40)-(380,260)`, `TabCount` 5 with tab `i` at `(10, 50 + 30i)-(110,
75 + 30i)` (so tab 2 is `(10,110)-(110,135)`), `SelectedTab` 2, `HintTopY` 280. A **dirty spec** is an
`FSOLHoloFrameSpec` pre-filled with other values in every written field (column divider on at other places, 5 other
valid tab rects, `SelectedTab` 4, a broken divider with a step) and with `Rect (0,0)-(400,300)`, `UiScale` 2,
`HoverItemRect (1,2)-(3,4)`, `HoverMark` `CentreTicks`.

19. `MakeOverlayLayout` on O: `TitleStepWidthPx` 150; `TabCount` 5, `TabRects[0..4]` equal to O's; `SelectedTab` 2;
    `bColumnDivider` true, `DividerTop (120, 40)`, `DividerBottomY` 260, `TabDividerX` 120; `OpenTopY` 110,
    `OpenBottomY` 135; `bBrokenDivider` true, `BrokenStart (10, 270)`, `BrokenEndX` 380, `BrokenStepAtPx` 0,
    `BrokenStepPx` 0. Into a dirty spec: the same written fields, and `Rect`, `UiScale`, `HoverItemRect` and
    `HoverMark` unchanged.
20. Title rule (`MakeOverlayLayout` and `MakeSpeedLayout` alike): `TitleWidth` 0, -5, NaN, +inf and -inf give
    `TitleStepWidthPx` 0; 150 gives 150.
21. Tabs on O: `TabCount` 7 reads 5 (rects 0-4 copied); `TabCount` 3 copies rects 0-2 and leaves 3 and 4 invalid
    (`bIsValid` false) although O's are valid; `TabCount` -1 gives 0, every rect invalid, `SelectedTab` `INDEX_NONE`.
    `SelectedTab` 5 (with 5 tabs), -1, or 3 with `TabCount` 3: `INDEX_NONE`, `OpenTopY` 1, `OpenBottomY` 0, the column
    divider still present at 120.
22. Opening with an invalid tab rect: O with tab 2's rect set to `FBox2f(ForceInit)`, to `(10,110)-(110,110)` (zero
    height) or with a NaN coordinate: `SelectedTab` stays 2, `OpenTopY` 1, `OpenBottomY` 0, `bColumnDivider` true at
    120; `TabRects[2]` is copied as given.
23. Missing or overlapping columns: O with `TabColumn` invalid: `bColumnDivider` false, `DividerTop (0,0)`,
    `DividerBottomY` 0, `TabDividerX` 0, `OpenTopY` 1, `OpenBottomY` 0 (the selection 2 stays); the hint divider from
    the row column alone, `BrokenStart (130, 270)`, `BrokenEndX` 380. O with `RowColumn` invalid: no column divider; hint
    divider `BrokenStart (10, 240)`, `BrokenEndX` 110. O with `RowColumn (100,40)-(380,260)` (overlapping): no column
    divider, no opening; hint divider `BrokenStart (10, 270)`, `BrokenEndX` 380. O with `RowColumn (110,40)-(380,260)`
    (touching): column divider at `xd` 110, opening 110-135.
24. No hint divider: O with `HintTopY` NaN, +inf, 260 (equal to the columns' bottom) or 200: `bBrokenDivider` false,
    `BrokenStart (0,0)`, `BrokenEndX` 0, both step fields 0; the column divider unchanged from case 19. Both columns
    invalid with `HintTopY` 280: no column divider and no hint divider. A default-constructed input: `TitleStepWidthPx`
    0, `TabCount` 0, every tab rect invalid, `SelectedTab` `INDEX_NONE`, `bColumnDivider` false, `bBrokenDivider` false,
    `OpenTopY` 1, `OpenBottomY` 0. Each of these into a dirty spec gives the absent values listed, never the dirty ones.
25. With the frame builder: the spec of case 19 plus `Rect (0,0)-(400,300)`, `UiScale` 1, through `BuildFrame` with an
    Open look (`FrameFraction` 1, `FillAlpha` 1) at `s` 1: no `ColumnDivider` shape has a y range that overlaps
    `[110, 135]` (ends included), every one has `x = 120`, and the selected tab's 4 `Tab` pieces include
    `(120,110)->(16,110)` and `(10,135)->(120,135)` (section 6.3 `TabMarker` with `D = 120`). Exactly one `TabDot`.
26. `TextItemRect` with slot `(40,100)-(240,120)`: text 90, indent 0 gives `(40,100)-(130,120)`; text 120, indent 34
    gives `(74,100)-(160,120)`; text 1000, indent 0 gives `(40,100)-(240,120)` (clamped to the slot); indent NaN or -5
    reads 0 (text 90 gives `(40,100)-(130,120)`). Invalid box (`bIsValid` false) for: text 34 with indent 34; text 0;
    text -10; text NaN; text +inf; indent 250 with text 1000; slot `FBox2f(ForceInit)`; slot `(5,5)-(5,20)` (zero
    width); a slot with a NaN coordinate.
27. `MakeSpeedLayout` with `TitleWidth` 120, `StepperSection (12,40)-(330,150)`, `ListSection (12,170)-(330,380)`:
    `LocalPerPx` 1: `TitleStepWidthPx` 120, `bBrokenDivider` true, `BrokenStart (12, 163)`, `BrokenEndX` 330,
    `BrokenStepAtPx` 104, `BrokenStepPx` 6 (so the right level is 157 and the two levels straddle 160); `LocalPerPx`
    0.5: `BrokenStart (12, 161.5)`, `BrokenStepAtPx` 52, `BrokenStepPx` 3; `LocalPerPx` 0, -1, NaN and +inf behave as
    1. Different widths, `StepperSection (20,40)-(300,150)`: `BrokenStart.X` 12, `BrokenEndX` 330 (the outer edges of
    both). A gap narrower than the step, `ListSection.Min.Y` 154: still present, `BrokenStart.Y` 155, step 6.
28. `MakeSpeedLayout` into a dirty spec (case 27's input): `bColumnDivider` false, `DividerTop (0,0)`,
    `DividerBottomY` 0, `OpenTopY` 1, `OpenBottomY` 0, `TabDividerX` 0, `TabCount` 0, all `FRAME_MAX_TABS` tab rects
    invalid, `SelectedTab` `INDEX_NONE`, the divider as case 27; `Rect`, `UiScale`, `HoverItemRect` and `HoverMark`
    unchanged.
29. No speed divider: case 27 with `ListSection` invalid, `StepperSection` invalid, `ListSection.Min.Y` 150 (touching)
    or 140 (overlapping): `bBrokenDivider` false, `BrokenStart (0,0)`, `BrokenEndX` 0, `BrokenStepAtPx` 0,
    `BrokenStepPx` 0, also into a dirty spec; the title step is still set (120).

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
| `CARD_TICK_PX` | 16 | Corner tick length of the frameless cards (x UI scale, at a 720 px canvas; as in the approved sample, D15) |
| `FLAT_PANEL_ALPHA` | 0.45 | Backdrop alpha at strength 0, a plain flat panel (D18; the HUD backing's 0.45) |
| `IDLE_SWEEP_PERIOD_S` | 4.0 | One scanline pass from top to bottom |
| `IDLE_SWEEP_INTENSITY` | 0.08 | Sweep line glow per unit of strength |
| `BACKDROP_ALPHA_PER_UNIT` | 0.15 | Dark tint alpha per unit of strength (0.30 at 2; the HUD backing is 0.45). Also the faint tint of the option (c) frames (D20). The approved sample `overlay-c-tint.png` is darker (the background kept at about 22 %, an alpha near 0.78); compare at the hand check |
| `HOVER_CUE_MIN_INTERVAL_S` | 0.05 | Shortest gap between two hover cues |
| `FRAME_MAX_SHAPES` | 160 | Shape capacity of one `SSOLHoloFrame` (reserved once). Worst case, the Overlay Menu, as the section 4.6 `static_assert` counts it (the backing is not a shape): outline 24 + gap dots 14 + ruler 6 + accents 4 + column divider 40 + hint divider 6 + tab markers 20 (`FRAME_MAX_TABS` x `TAB_MAX_SEGMENTS`; 16 in practice, one selected tab of 4 and four of 3) + tab dot 1 + hover mark 8 + its dot 1 + fill and sweep 2 = 126 |
| `CARD_MAX_SHAPES` | 10 | Shapes of one frameless card: fill 1 + corner ticks 8 + sweep 1 (section 4.6) |
| `FRAME_FLAT_PAD_PX` | 1 | Glow pad of every frame line and dot at strength 0 (thin flat lines, D18), lerped to `HUD_GLOW_PAD_PX` by `GlowScale(s)` |
| `FRAME_MAX_TABS` | `SOL::Overlay::MAX_PROFILES` (5) | Profile tab markers one frame holds |
| `MIN_PIECE_PX` | 1e-3 | The section 6.3 minimum piece length: a geometry piece shorter than this is never written (42a review) |
| `UI_SCALE_MIN` / `UI_SCALE_MAX` | 0.1 / 16 | Section 6.3 geometry clamps a valid `uiScale` to this range (after NaN, infinite or `<= 0` reads 1), so no input can stall `DashedDivider` or give infinite coordinates (42a review) |
| `MENU_LAYER_MAX_SHAPES` | 512 | Shape capacity of the HUD's menu layer (the HUD layer reserves `HUD_MAX_SHAPES` 4096; the menus and cards need about 100; 42c review) |
| `BACKDROP_TINT` | (0.007, 0.045, 0.165) linear | RGB of the frames' and cards' dark tint: their backing box (alpha `BackdropAlpha`) and their additive fill. Tuned at the 42c review so a frame's interior over black at strength 1 matches the approved sample's, about sRGB (3, 9, 17); it was the HUD backing colour plus a Primary fill, which read bluer and brighter |

**Option (c) frame (D20).** All lengths in px at a 720 px canvas, x UI scale (as `CARD_TICK_PX`), measured from the
approved samples (`overlay-c-tint.png`, `speed-c-tint.png`); proposed, tuned from screenshots.

| Constant | Proposed | Meaning |
|---|---|---|
| `FRAME_CUT_TL_PX` | 10 | Top-left corner cut (`a`) |
| `FRAME_CUT_PX` | 14 | Top-right and bottom-left corner cuts (`c`) |
| `FRAME_STEP_PX` | 10 | Height of the title step and depth of the bottom-right step-in (`s`), both at 45 degrees |
| `FRAME_NOTCH_PX` | 40 | Height above the bottom edge where the right edge steps in (`n`) |
| `FRAME_MIN_W_PX` / `FRAME_MIN_H_PX` | 34 / 64 | Smallest rect for the full-size shape (`a + s + c` / `s + c + n`); a smaller rect shrinks the shape (`k`) |
| `FRAME_TITLE_STEP_PAD_PX` | 26 | The raised title section's width is the title text width plus this |
| `FRAME_OUTLINE_POINTS` | 11 | Points of the stepped outline (`P0`-`P10`) |
| `FRAME_GAPS[]` / `FRAME_GAP_COUNT` | `{0.20, 10}, {0.27, 20}, {0.36, 12}, {0.52, 10}, {0.66, 24}, {0.70, 8}, {0.90, 12}` / 7 | The outline's fixed gaps: start as a fraction of the perimeter from `P0`, length in px (96 px in all). One table for every option (c) frame |
| `FRAME_MAX_SEGMENTS` | 24 | Capacity of `FrameDrawSegments` (worst case 11 points + 7 gaps + the stroke meeting point = 19, plus margin) |
| `FRAME_MAX_DOTS` | 14 | Capacity of `FrameGapDots` (2 per gap) |
| `FRAME_DOT_RADIUS_PX` | 1.4 | Radius of a gap end dot |
| `FRAME_RULER_TICK_COUNT` | 6 | Ruler ticks on the left edge |
| `FRAME_RULER_TICK_PX` | 4 | Ruler tick length, pointing inward |
| `FRAME_RULER_SPACING_PX` | 9 | Distance between ruler ticks |
| `FRAME_RULER_START` | 0.5 | First tick at this fraction of the rect height from the top |
| `FRAME_ACCENT_COUNT` | 4 | Capacity of `FrameAccents` |
| `FRAME_ACCENT_INSET_PX` | 6 | Inset of both corner accents from their edge |
| `FRAME_ACCENT_LEFT_TOP_PX` / `FRAME_ACCENT_LEFT_BOTTOM_PX` | 18 / 70 | Top-left accent, from and to (below the top) |
| `FRAME_ACCENT_BOTTOM_PX` | 74 | Length of the bottom-right accent (from 6 to 80 px left of the inset edge) |
| `FRAME_TITLE_UNDERLINE_Y_PX` | 34 | Title underline, below the top |
| `FRAME_TITLE_UNDERLINE_START_PX` / `_END_PX` | 12 / 40 | Underline starts 12 px from the left, ends 40 px before the title step |
| `FRAME_TITLE_DASH_START_PX` / `_END_PX` | 32 / 20 | Its short dash, 32 to 20 px before the title step |
| `FRAME_DIM_ALPHA` | 0.45 | Alpha of the dim lines (title underline, dividers, unselected tab bars) relative to the frame lines |
| `FRAME_TINT_INSET_PX` | 2 | Inset of the tint box from the frame rect |
| `HOVER_PAD_X_PX` / `HOVER_PAD_Y_PX` | 8 / 3 | The widget inflates the hovered item's rectangle by these before `HoverMark` |
| `HOVER_BRACKET_ARM_PX` | 5 | Arm of the hover brackets |
| `HOVER_POINTER_RUN_PX` / `HOVER_POINTER_RISE_PX` / `HOVER_POINTER_DOT_PX` | 34 / 8 / 2 | Pointer line: straight run, 45-degree rise, dot beyond its end |
| `HOVER_CENTRE_TICK_PX` | 8 | Tick above and below a hovered digit or unit field |
| `HOVER_MAX_SEGMENTS` | 8 | Capacity of `HoverMark` |
| `TAB_BRACKET_ARM_PX` / `TAB_BRACKET_INSET_PX` | 6 / 2 | Unselected tab bar: arm length and vertical inset |
| `TAB_CUT_PX` / `TAB_DOT_OFFSET_PX` | 6 / 4 | Selected tab: top-left cut, and the dot's distance left of the tab |
| `TAB_MAX_SEGMENTS` | 4 | Capacity of `TabMarker` |
| `DIVIDER_DASH_PX` / `DIVIDER_PERIOD_PX` | 9 / 14 | Column divider dash length and repeat |
| `DIVIDER_MAX_DASHES` | 40 | Capacity of `DashedDivider` (a full-height Overlay Menu needs about 25) |
| `DIVIDER_GAPS[]` | `{0.33, 8}, {0.75, 16}` | Gaps of `BrokenDivider`, like `FRAME_GAPS` |
| `DIVIDER_MAX_SEGMENTS` | 6 | Capacity of `BrokenDivider` (worst case 3 legs + 2 gaps = 5) |
| `SPEED_DIVIDER_STEP_AT_PX` / `SPEED_DIVIDER_STEP_PX` | 104 / 6 | The speed panel's divider steps up 6 px, 104 px from its start (the Overlay Menu's hint divider has no step) |

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
  **under 0.1 ms GPU** (`ProfileGPU`: the game window's Slate UI pass, with the shape batches, the median of three
  frames; `r.ProfileGPU.ShowUI 0`, because the visualizer window would add its own Slate pass to later profiles while
  the log gets the table either way), 1280x720, editor build. Menus that exclude each other (the radial, Ctrl+O, F3 and the map list menu take the same input) are measured
  one per pass, each together with all four cards; the worst pass must meet the budget. Also: **zero heap allocation
  per frame in project code** (as SDD 21: Slate's `MakeCustomVerts` copies the vertex and index arrays into the
  draw element, two engine allocations per draw); at most `FRAME_MAX_SHAPES` shapes per frame widget; one extra custom-vertex draw per visible menu.
- **Profiling smoke (D14):** `-SOLSmokeHoloMenuProfile` (flag constant in `SOLConstants.h`, listed in ARCHITECTURE)
  flies the fixed smoke scene (pinned sim date, named body, section 11.1), opens each pass's menus, refreshes the info
  card and waits at least `OPEN_S` + 0.1 s of real time before every capture (so no open animation is caught), runs
  three `ProfileGPU` frames (the median is used) and `stat dumpave -num=240`, and logs the numbers with a PASS / FAIL /
  NOBASE verdict against the budget. The
  baseline is the same scene and passes at the commit before 42c (the menus as they are today), captured once and
  written into the SDD amendment; the final numbers are captured in 42i.
- Event-driven: a closed menu costs nothing (the HUD's menu layer draws nothing and is not stamped; a closed UMG
  frame is removed). The UMG `NativeTick` returns at once when the menu is Open and idle; the idle sweep and hover
  glow are evaluated in the frame's paint from `nowS` (a few float ops), no timers.
- The menu MID parameters are pushed only when they change (strength, view size, the `sol.HudComposite` switch), as
  the HUD does, once per frame for every user (`UpdateMaterialForFrame`).
- The always-on speed and date-and-anchor cards are drawn every flight frame: their idle sweep and jitter are a few
  float ops in the paint, and they cost no more shapes than today's two panels plus 16 tick segments and 2 sweep lines.
- No claim without a measurement; the numbers go into an SDD amendment.
- **More draws than one per menu (42e amendment).** The P2 and P3 profile passes add more Slate draws than "one extra
  custom-vertex draw per visible menu" above: P2 UI draws went from 57 to 62. Attributed from the code (not from a
  per-draw capture): each open UMG frame (`SSOLHoloFrame` paint) draws its faint tint with `PaintBacking`, a stack of up
  to `HUD_PANEL_BACKING_LAYERS` (6) rounded `MakeBox` boxes, then its one `MakeCustomVerts`, so up to 7 draws. Against
  that, the Overlay Menu's own dark panel fill and its viewed-tab / highlighted-row fill boxes are now transparent, and
  Slate culls a fully transparent box, so 2 draws fewer: net +5, which matches 57 -> 62. The content's render opacity
  adds no draw at the captures (the menu is Open, opacity 1). The F3 panel's full-screen dim is today's box (now faded
  with the fill, section 5), not a new draw; P3 adds the same frame parts as P2. The D14 budget is time, not draw
  count; the pass verdicts stay the measure.
- **The cards (42g amendment, written at the 42g review).** These refine the bullets above and sections 4.3, 5 and 12:
  - **The menu layer is no longer idle in flight.** "A closed menu costs nothing (the menu layer draws nothing and is
    not stamped)" holds only with no card drawn: the always-on speed and date-and-anchor cards draw on the menu layer
    every flight frame, so it is stamped and painted every flight frame (one extra custom-vertex draw for the layer,
    plus its card backings), with every menu closed. The smoke checks change intent to match: `a-menu-layer` checks
    that the layer is in use for the cards only (no menu open), and `k-menu-layer-idle` that it holds exactly one
    backing per drawn card and no radial (the UMG frame never draws on it), not that it is empty.
  - **Card content on the menu layer (`BeginCardContent(card)` / `EndCardContent`).** The always-on cards' content
    (the speed block, the G meter and the date-and-anchor lines) is drawn by the HUD's ordinary helpers (`DrawBuffer`,
    `GlowLine`, `GlowBox`, `GlowRing`, `DrawBar`). Between the two calls they write to the menu layer's batch and text
    list, so the content paints above the card's own tint. `EndCardContent` records the shapes and text lines added
    into the card's stats (the smoke checks both are on the menu layer), and `BeginShapeFrame` clears a call left
    open, so a missed `EndCardContent` cannot send the next frame's HUD to the menu layer. No `AddHoloPanel`,
    `MenuHoloShapes` or `DrawCard` call may run between the two.
  - **Card content lines follow D18.** Between the two calls the line helpers pad with `LinePad(s)` of the hologram
    menu strength, as the card's ticks do, so at strength 0 the bars, the steer icon and the G meter are flat
    (`FRAME_FLAT_PAD_PX`); the text keeps its glow, as the radial labels do.
  - **HUD-layer items under a card.** The card's tint is on the menu layer, above the whole HUD layer, so a HUD-layer
    item drawn where a card is (the off-screen target's edge arrow and label at the top right, the jump map's labels)
    now sits under that card's tint.
  - **"TARGET none" timing.** The plain HUD line "TARGET none" (with its HUD panel) shows once the target card has
    finished closing, not while it closes from its cache.
  - **Target change (Q15, answered (a)).** Another target (R / F cycling, a click, Target in a menu) keeps the card
    open and only swaps its text; the card draws in only when a target appears from none and closes only when the
    target is cleared. A target that appears while the card is still closing reverses the close from its progress (the
    section 4.5 reopen rule). Checked by the `c-target-cycle` step of `-SOLSmokeHoloMenu`.

### 11.1 Baseline (measured before 42c)

**Conditions.** `-game -windowed -ResX=1280 -ResY=720 -SOLSmokeHoloMenuProfile`, the Development editor binary
(`UnrealEditor.exe`, uncooked), commit `49949c0` plus the uncommitted 42a files (pure maths, not drawn, so the
rendering is the pre-conversion state), UE 5.8.3, D3D12 SM6, Intel i9-9960X, NVIDIA RTX 2080 Ti, VSync off,
scalability 3 (Epic, from the machine's game user settings; both are logged in every RESULT line), smoke settings slot
(defaults), measured 2026-11-04. Two baseline runs back to back, then a third run against the stored baseline; logs
`Saved/Logs/Smoke_HoloMenuProfile_run1.log`, `_run2.log` and `_run3.log`, screenshots per run in
`Saved/Screenshots/WindowsEditor/HoloMenuProfile_run<n>/`. All 12 script checks passed in every run, and every
capture frame had its pass's menu open.

**Scene (fixed, the same every run).** The sim date is pinned: Setup calls `USOLSimClockSubsystem::SetJulianDate`
with `PROFILE_SIM_JD` = **JD 2461348.5 (2026-11-04 00:00 UTC)**, then (once the bodies have moved to that date) places
the ship as the normal spawn does: 10,000 km above Earth's Sun-facing side, at rest in Earth's frame, facing Earth,
not rotating (the anchor is Earth). The menus and cards work on one named body, `PROFILE_BODY_NAME` = **Neptune**
(about six weeks past opposition, so its icon is on screen behind Earth; the icons drawn are Earth, Dione, Oberon,
Neptune and Eris, Neptune's at (1169, 380)). If Neptune's icon is missing, the `a-body-icon` check fails, an error line
says every pass is invalid, and every pass logs FAIL. P0: no menu, no target, no info card ("TARGET none"). P1-P4:
Neptune targeted with the radial menu's Target and its info card shown with Info, so the target card, the info card,
the speed card and the date-and-anchor card are all on screen. P1: the radial menu held open on Neptune's icon with
the Info sector highlighted. P2: the Ctrl+O Overlay Menu (Profile 1). P3: the F3 speed panel. P4: the jump map with
the list menu on Neptune; the four cards show on the map too. Before every capture (each `ProfileGPU` frame and each
`stat dumpave` window) the info card is refreshed and the script waits at least `SOL::HoloMenu::OPEN_S` + 0.1 s of real
time, so after the conversion no capture lands on a card's open animation.

**Measures.** CPU: `stat dumpave -num=240 -ms=0`, one 240-frame window per root, ms per frame: `HUD Time` (`DrawHUD`,
radial menu and cards included) and `Draw Window And Children Time` (the whole Slate paint: the HUD shape layer and
the UMG menus; `Game UI Paint` is inside it and is listed for reference). `cpu_total` = HUD Time + Draw Window. GPU:
three `ProfileGPU` frames per pass, the median of the inclusive time of the game window's `SlateUI` pass (all UI
drawing: HUD shapes, glow text, Canvas, UMG). `r.ProfileGPU.ShowUI 0`: in UE 5.8 that cvar only opens the profile
visualizer window (the Slate passes are in the log either way), and the visualizer's own Slate pass would join every
later profile. The table settings the parser relies on (`TableFormatting`, `UnicodeOutput`, `ShowStats`,
`ShowPercentColumn`, `ShowInclusive`, `ShowExclusive` on, `ThresholdPercent 0`, `Root *`) are set at the start.

| Pass | CPU HUD (ms) run 1 / 2 | CPU Slate (ms) run 1 / 2 | **CPU total** run 1 / 2 | **GPU UI** run 1 / 2 | UI draws |
|---|---|---|---|---|---|
| P0 flight | 0.113 / 0.113 | 0.131 / 0.116 | **0.244 / 0.229** | **0.079 / 0.078** | 41 |
| P1 radial | 0.137 / 0.138 | 0.153 / 0.142 | **0.290 / 0.280** | **0.106 / 0.109** | 53 |
| P2 Overlay Menu | 0.147 / 0.135 | 0.259 / 0.262 | **0.406 / 0.397** | **0.114 / 0.114** | 57 |
| P3 F3 panel | 0.147 / 0.134 | 0.292 / 0.282 | **0.439 / 0.416** | **0.128 / 0.126** | 59 |
| P4 map list | 0.700 / 0.720 | 0.215 / 0.219 | **0.915 / 0.939** | **0.079 / 0.078** | 51 |

`Game UI Paint` (inside Slate): P0 0.075 / 0.080, P1 0.091 / 0.091, P2 0.206 / 0.206, P3 0.230 / 0.231, P4 0.148 /
0.160. Frames ran at 4.2-5.5 ms (no frame cap). The P4 HUD time is the map's overlays (body icons, labels, routes),
not the list menu.

**Spread.** Between the two baseline runs the CPU total moved by at most 0.024 ms (P4) and the GPU UI time by at most
0.003 ms (P1); within a run the three GPU frames agreed within 0.004 ms. Both are well inside the 0.1 ms budget, so a
single run of the flag after the conversion is a fair comparison.

**Stored baseline and check run.** The mean of the two runs is stored in `SOLHoloMenuProfileSmoke.cpp`
(`PROFILE_BASELINE_CPU_MS` = 0.2365, 0.285, 0.4015, 0.4275, 0.927; `PROFILE_BASELINE_GPU_MS` = 0.0785, 0.1075, 0.114,
0.127, 0.0785). The third run, against it, logged PASS for all five passes: CPU deltas +0.008, +0.009, -0.020, +0.006,
-0.008 ms and GPU deltas -0.003, +0.001, +0.001, +0.001, +0.002 ms (P0-P4).

**Verdicts.** Each pass logs `HoloMenuProfile RESULT P<n> PASS|FAIL|NOBASE ... dcpu=.. dgpu=..`, then a `SUMMARY`
line:

- **PASS:** the capture is valid and both deltas against the baseline are under 0.1 ms.
- **FAIL:** a delta is 0.1 ms or more, or the capture is invalid: the ship not placed for the pinned date, Neptune's
  icon missing, not a standalone `-game` run (`GIsEditor`), a view other than 1280x720, a number not found in the log,
  the pass's menu closed on any capture frame, or the cards not as the pass wants them (`valid=no` in the line).
- **NOBASE:** the capture is valid but the pass has no baseline (its constants are zero), so no comparison is made.

The numbers are machine-specific: on another machine, re-measure at the 42c parent commit and replace the constants.

## 12. Verification

- **Unit tests:** section 6.4 (`SOLTest.HoloMenu.*`) and section 9.5, written from the contracts by a separate opus
  subagent, red on stubs, then green. Existing tests re-validated: `HudShapes`, `HudPanels` (unchanged renderer
  maths), `InteractMenus` (radial geometry unchanged), `OverlayModel`, the settings files.
- **New smoke `-SOLSmokeHoloMenu`** (constant in `SOLConstants.h`, listed in ARCHITECTURE), at 1280x720 and 1000x800,
  real injected input, smoke settings slot:
  1. Ctrl+O: the frame widget exists; after `OPEN_S` + a frame the phase is Open and the frame drew its full outline
     (shape count >= outline segments); one open cue requested.
  2. Mid-open capture at about 0.1 s: frame fraction strictly between 0 and 1 (a screenshot for the look).
  3. Hover a row with the mouse, then Down: two hover cues at most, the hover brackets and pointer line drawn at the
     highlighted row.
  4. Toggle a row: one select cue. Esc: input back to the ship in the same frame; the widget gone after `CLOSE_S`.
  5. Radial: press on an icon, check the draw-in, drag across two sectors (hover cues), release on an enabled action
     (select cue), check the close animation from the cached state.
  6. F3: open, step a digit (select cue), move the highlight (hover cue), close.
  7. Info card: open via Info, then close; target card: select and clear a target (corner ticks drawn, no outline);
     speed card and date-and-anchor card: present in the menu layer with 8 tick segments each, and gone from the HUD
     layer's panels.
  8. Map list menu: open on the map, hover a row, choose an enabled action (open, hover and select cues).
  9. Strength 0 (set in the smoke slot): open is instant (the whole broken outline on the first frame), no sweep,
     flicker, jitter or hover glow drawn, the backdrop is `FLAT_PANEL_ALPHA`, and a row toggle, a digit step and a
     radial selection still work (D18); the open, hover and select cues are still requested (D21); strength 2
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

All but Q13 answered (Q1-Q12 on 2026-11-03, Q14 and Q15 on 2026-11-04; kept here for the record; the decisions are D7-D21):

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
| Q9 | **Frame shape of the Ctrl+O Overlay Menu and the F3 speed panel** (and, proposed in the question, the map list menu). Options shown as Pillow mock-ups over `43-dust-slow.png` (not in-game captures): (a) corner ticks + tint with a notched title tab ([`overlay-a.png`](../Images/hologram-frame-samples/overlay-a.png), [`speed-a.png`](../Images/hologram-frame-samples/speed-a.png)); (b) chamfered glowing outline + tint ([`overlay-b.png`](../Images/hologram-frame-samples/overlay-b.png), [`speed-b.png`](../Images/hologram-frame-samples/speed-b.png)); (c) segmented / stepped broken lines, glow only ([`overlay-c.png`](../Images/hologram-frame-samples/overlay-c.png), [`speed-c.png`](../Images/hologram-frame-samples/speed-c.png)); side by side in [`frames-compare.png`](../Images/hologram-frame-samples/frames-compare.png) | **(c) with a faint dark tint behind the panel** (D20): samples [`overlay-c-tint.png`](../Images/hologram-frame-samples/overlay-c-tint.png), [`speed-c-tint.png`](../Images/hologram-frame-samples/speed-c-tint.png), [`frames-compare-c-tint.png`](../Images/hologram-frame-samples/frames-compare-c-tint.png). The map list menu uses the same frame, as proposed in the question; **to be confirmed by the user at the hand check** (not separately answered) |
| Q10 | Strength 0 | **Plain flat panels: no glow, animation or jitter; fully usable** (D18) |
| Q11 | Cursor | **Follows the new slider** (D19) |
| Q12 | Cues at strength 0 (raised while recording D18) | **Sounds are independent of the hologram strength and still play at strength 0**; the SFX volume controls them (D21) |
| Q13 | **Hover glow on a highlighted disabled radial sector** (raised by the 42d review): today the rim takes the disabled intensity (0.22) plus the full hover glow (up to 0.9), so it peaks at 1.12, brighter than an enabled highlighted sector's rim without glow. Options: **(a, recommended)** no hover glow on disabled sectors; (b) the glow scaled by the disabled dimming; (c) full glow, as now | **Open** (asked 2026-11-04). The code keeps (c) behind `SOL::HoloMenu::RADIAL_DISABLED_HOVER_GLOW_SCALE` (1 = as now), so the answer is a one-line change |
| Q14 | **Overlay Menu: ignore zero-delta synthetic mouse moves** so the keyboard highlight stays until the mouse really moves (a D5 input change, raised by the 42e build and review). Before 42e a resting mouse over a row took the highlight back after a key move, because Slate also sends mouse moves with no movement; the 42e code filters those. The filter is meant to gate only the row-highlight takeover; the tab hover follows every move, so the tab brackets stay under the cursor when the panel re-centres (42e review finding 2). Options: **(a, recommended)** keep the filter: a mouse move with zero delta does not take the row highlight; (b) revert to today's input (every mouse move, real or not, takes the highlight) | **(a) keep the filter** (answered 2026-11-04): the 42e code stays |
| Q15 | **Target card on a target change** (raised by the 42g review): cycling targets (R / F) replays the target card's draw-in on every change, so fast cycling keeps the card mid draw-in. Options: **(a, recommended)** keep the card open and swap its text, drawing in only from "no target"; (b) replay the draw-in on every target change, as then | **(a)** (answered 2026-11-04): the card stays open on a target change and only swaps its text; it draws in only when a target appears from none and closes only when cleared (`DrawTarget` no longer restarts the anim on a name change; smoke check `c-target-cycle`; section 11 42g amendment) |

Still open: **Q13** (in-game samples of the three options for the user: [`Docs/Images/hologram-radial-glow-samples/`](../Images/hologram-radial-glow-samples/)).

## 14. Out of scope

The flight HUD (D1) except the speed card and the date-and-anchor card (D15); the full-screen menus (#46); the Overlay
list embedded in Settings (#46, D17; it keeps today's look until #46 restyles it); the Attack hint and the profile tag
(stay HUD, D16); keyboard / gamepad navigation (D5); mouse support for the F3 panel (D5); in-world cockpit screens
(`Docs/ToDo/cockpit-3d-screens.md`); the glow-text and panel-flicker tech debt (may be fixed on the way only if the
user asks).
