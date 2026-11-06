<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 46: Solid console-style menus

Issue #46. Depends on SDD 8 (menus, Settings, the menu flow), SDD 11 Part 10e (Demo page), SDD 22 (hologram cursor,
the Overlay Menu list embedded in Settings) and SDD 42 (the shared animation, hover and cue code, built first for #46:
SDD 42 D12, D17). Plan: [`Docs/Plans/46-solid-menus-plan.md`](../Plans/46-solid-menus-plan.md). Grill notes:
[`Docs/Plans/46-solid-menus-grill-notes.md`](../Plans/46-solid-menus-grill-notes.md). Style research:
[`Docs/Research/SOLID_MENU_STYLE_REFERENCES.md`](../Research/SOLID_MENU_STYLE_REFERENCES.md). Editable style
sources: [`Docs/Art/menu-style/README.md`](../Art/menu-style/README.md).

Status: **Done (2026-11-06; parts 46a-46j built, reviewed and documented).** Originally **drafted 2026-11-04 from the resolved grill-me decisions; all 17 open questions (section 15) answered by the
user on 2026-11-04 and folded into the text below.** Checked against the code at HEAD `8382434`. Items marked
**(proposed)** are starting values or design details that no decision fixes; they were approved as the tuning start in
Q16 (final values are shown at the hand check).

## 1. Goal

Give the full-screen menus (main menu, Settings, Demo, the read-only Controls / Info / About pages and the pause menu)
a solid, opaque brushed-gunmetal "console" look with an asymmetric hull shape language, so they read as clearly
different from the cyan hologram of the flight HUD and the in-flight menus (#42). Each screen keeps its logic, its
widgets and its input; a shared solid-panel kit restyles them in place and adds three animations (panel open, circuit
trace power-up, slow idle pulse) scaled by the existing HUD effects setting (Q1). No saved setting is added, so there is no
schema change.

## 2. Resolved decisions (grill-me, the user's answers)

| # | Decision |
|---|---|
| D1 | **Scope (2026-11-03):** main menu, Settings, Demo, Controls / Info / About and pause. The hologram stays only for the HUD, the radial menu, the Overlay Menu, the F3 speed panel and the target / info cards (#42). The Overlay list embedded in Settings follows the solid Settings look (SDD 42 D17). |
| D2 | **Look (2026-11-03):** direction 1 palette (dark gunmetal brushed steel, cyan accent, dim text #A4AFB8) with the round 2 "C" shape language: asymmetric hull, inset recesses, vents, rivets and glowing circuit traces. Reference board `Docs/Images/menu-style-samples/round2-C.png`; editable sources in `Docs/Art/menu-style/`. |
| D3 | **Textures and materials are generated now** with the MCP texture / material tools, not deferred to #9 (2026-11-03). |
| D4 | **Font (2026-11-03):** Rajdhani or a similar free OFL techno sans, bundled in the project. The HUD font is unchanged. |
| D5 | **Animations (2026-11-03):** panel-open, circuit-trace power-up and a slow idle pulse, scaled by the existing **HUD effects strength** setting (`HudEffectsStrength`, Q1). **No new saved settings.** |
| D6 | **Input unchanged (2026-11-03):** mouse + keyboard + the #22 hologram cursor and hover glow. A keyboard / gamepad navigation redesign is a future idea, not part of #46. |
| D7 | **Rebuild approach (2026-11-04): restyle in place.** Each screen keeps its logic and widgets (`USOLMainMenuWidget`, `USOLPauseMenuWidget`, `USOLSettingsPageWidget`, `USOLDemoPageWidget`, the `SOLMenuContent` page builders, `USOLMenuButton`); a shared solid-panel kit adds the frame, buttons, tabs, sliders and toggles. |
| D8 | **Hull shapes (2026-11-04): SDF in a UMG material** (the procedural shape is computed in a User Interface material), not 9-slice textures. |
| D9 | **Asset pipeline (2026-11-04):** PNG sources exported from the craft MCP tools into `Docs/Art/`, imported into `Content/` by an editor Python script under `Tools/`, with fixed asset paths in `SOLConstants.h` (no hardcoded paths in gameplay code, STYLE_GUIDE section 7). Tool notes: VectorCraft takes absolute paths; PhotoCraft needs `--automation-write-root`. |
| D10 | **Colours (2026-11-04):** hover / selected glow **amber #FFA628**; hull traces stay **cyan #28D2F0**; text **#A4AFB8**. |
| D11 | **Hover (2026-11-04):** the button lifts 3 px, gets an accent edge, its LED lights, an underline shows and a circuit trace extends from it into the panel (the approved board was `hover-compare.png`, panel B, a sample now deleted). |
| D12 | **Main menu layout (2026-11-04):** a left button stack on a hull panel, the title on top, version / credits in a small bottom-left recess. |
| D13 | **Settings layout (2026-11-04):** a large hull panel with a vertical tab column on the left and the content on the right. |
| D14 | **Controls / Info / About (2026-11-04)** reuse the same shell as Settings, with read-only content. |
| D15 | **Pause (2026-11-04):** a slide-in side panel on the **left** over a dimmed, frozen flight view (Q3: only the live view is dimmed, the simulation keeps running as today). |
| D16 | **No schema change (2026-11-04):** no saved setting is added or changed; `SOL::Settings::SAVE_VERSION` stays **11** and `KEY_BINDING_LAYOUT_VERSION` stays **10** (both verified in `SOLConstants.h` at `8382434`). |
| D17 | **Scrolling rails (user, 2026-11-05):** if a tab rail (Settings / Controls / Info / About / pause) or the main-menu button stack has more entries than fit, it scrolls up and down. 46g must implement it for the shared `FSOLSolidTabRail` and the main-menu column, with tests and smoke checks, and check the pause Controls panel's 10 tabs fit or scroll. |

The layout boards the user picked from (`layout-1-main-menu.png` option A, `layout-2-settings.png` option A,
`layout-3-info.png` option A, `layout-4-pause.png` option A, all samples now deleted) used placeholder entries ("NEW
GAME", "CONTINUE", "CONTROLS / INFO"); the real entries and pages below come from the code. In those boards amber marked
the hovered item and a cyan underline marked the active tab, and settings / info shells had a BACK button in the
bottom-right recess.

## 3. What exists today (checked in code at HEAD `8382434`)

All menu widgets are C++ UMG trees built in `NativeOnInitialized` (no widget blueprints), sized in UMG units at the
1080p reference DPI, styled from `SOL::MenuStyle` (`SOLConstants.h` around line 1160): flat translucent dark panels
(`PANEL_BACKGROUND`), rounded-box buttons tinted per state, the engine default font (`FCoreStyle::GetDefaultFontStyle`,
Regular / Bold), a pale-blue accent (`ACCENT_COLOR`) and an orange warning colour (`WARNING_COLOR`).

| Screen / piece | Type and file | Structure today |
|---|---|---|
| **Menu button** (every button of every screen) | `USOLMenuButton : UButton`, `Menu/SOLMenuButton.h/.cpp` | White `FSlateRoundedBoxBrush` (radius `BUTTON_CORNER_RADIUS` 6) for every state, tinted by `SetBackgroundColor` from `RefreshHighlight` (called from the owning widget's `NativeTick`): `BUTTON_NORMAL`, `BUTTON_SELECTED` (active tab / page / start body), `BUTTON_FOCUSED` (keyboard focus); the pressed brush is `BUTTON_PRESSED_TINT`. **Hover takes keyboard focus** (`NotifyHovered`, skipped while a rebind listens), so mouse and keyboard share one highlight. A `UTextBlock` label (`TEXT_COLOR`, accent when focused or selected). |
| **Text and page helpers** | `SOLMenuContent::MakeText`, `MakePage`, `Menu/SOLMenuContent.h/.cpp` | Every text block of every page is made here (font, size, colour, wrap); `MakePage` is a `UVerticalBox` with a heading. |
| **Main menu** | `USOLMainMenuWidget`, `Menu/SOLMainMenuWidget.cpp` | Transparent root, inset `SCREEN_MARGIN` 80 from the left, vertically centred. Left **column panel** (`COLUMN_WIDTH` 340): title `GAME_TITLE` "SOLTest" (56, bold), tagline, studio name (accent), 7 entry buttons (Play, Demo, Start: <body>, Controls, Settings, About, Quit; `BUTTON_HEIGHT` 52, gap 10), the key hint. **Page panel** beside it (`PAGE_WIDTH` x `PAGE_HEIGHT` 760 x 760, hidden when no page) with a `UWidgetSwitcher`: none, Start location, Controls, Settings, About, Demo (`ESOLMainMenuPage`). A page entry toggles its page; Esc closes the page. Over the live solar system (`ASOLMenuCameraPawn`). |
| **Start-location page** | in `USOLMainMenuWidget` | `MakePage` + body text + a `UScrollBox` with one `USOLMenuButton` per registry body (`LIST_BUTTON_HEIGHT` 38); the start body is the selected button. |
| **Pause menu** | `USOLPauseMenuWidget`, `Menu/SOLPauseMenuWidget.cpp` | Full-screen `SCREEN_DIM` (black, alpha 0.45) root, centred panel `PAUSE_WIDTH` x `PAUSE_HEIGHT` 1080 x 780: "PAUSED" heading, a **horizontal tab bar** of 7 equal buttons (Resume, Controls, Info, Settings, Demo, About, Quit; `TAB_HEIGHT` 46), and a switcher with one page per tab: Resume (`PAUSED_BODY`: the simulation keeps running; `PAUSED_HINT`), Controls, Info, Settings, Demo (in-flight mode), About, Quit (prompt + Quit to menu / Quit to desktop; the first was labelled "Quit to main menu" until 46i). Esc resumes. **The simulation keeps running** (SDD 8 decision 3; `-SOLMenuSmoke` check "sim keeps running while paused"; `GAME_MECHANICS.md` section 8). |
| **Settings page** | `USOLSettingsPageWidget`, `Menu/SOLSettingsPageWidget.cpp` (shared by both menus) | `MakePage` "Settings" + hint, then **one scroll box with six sections**: Graphics (Resolution, Window mode, Quality steppers; VSync, Space dust checkboxes), Audio (Master, Effects, Music sliders), Mouse (sensitivity slider, invert Y), Gameplay (HUD scale, HUD hologram effects, Hologram menu effects sliders; Show HUD, Assist at launch, Mouse steering at start checkboxes; Chase Down safety margin slider), Overlays (the embedded `USOLOverlayMenuWidget`, `SetEmbedded(true)`), Key bindings (Reset keys, then 38 rows: label, key text, Rebind button). Footer: dirty text, Revert, Apply; status line under it. Steppers are two small `USOLMenuButton`s around a value text; sliders are default `USlider`s (bar `BUTTON_SELECTED`, handle accent); checkboxes are default `UCheckBox`es. `ScrollToBinding` scrolls to a key row (rebind flow, smoke). |
| **Embedded Overlay list** | `USOLOverlayMenuWidget` in embedded mode, `UI/SOLOverlayMenuWidget.cpp` | No hologram frame (SDD 42 D17); embedded mode strips the root inset and the panel backing; title collapsed; profile tabs and rows as text blocks with their own highlight fills. |
| **Demo page** | `USOLDemoPageWidget`, `Menu/SOLDemoPageWidget.cpp` (shared) | `MakePage` "Scale demo" + hint, a presets row (Showcase, Benchmark, Ramp), a scroll of stepper rows under 6 section headings (`DEMO_SECTION_TITLES`), warnings text, footer Reset + Start demo (main menu) or Stop NPCs + Apply (pause). |
| **Controls page** | `SOLMenuContent::BuildControlsPage` | One scroll of `CONTROL_ROWS` (86 rows) grouped by area (Flight, Targeting, Map, Overlays, Time, HUD, Combat, Routes, Menu, Demo), key column `KEY_COLUMN_WIDTH` 230; key cells refreshed from the live keys when shown. |
| **Info page** (pause only) | `SOLMenuContent::BuildInfoPage` | Flight, Time and travel, Combat (label / value rows, `LABEL_COLUMN_WIDTH` 400 (200 in narrow rows since 46i, section 6.7), values from the constants), How it works (prose with live key names). |
| **About page** | `SOLMenuContent::BuildAboutPage` | Two tabs (Q6, user-confirmed 2026-11-05): Game (description, copyright, engine version `ABOUT_ENGINE_FORMAT`) and Credits. There is **no game version string** anywhere in the code or `Config/DefaultGame.ini` (Q10 adds a generated one). |
| **Flow** | `USOLMenuSubsystem`, `SOLMenuState` | Unchanged by #46: state machine, queued requests, input mode, cursor, settings visits, Demo options. |
| **Shared #42 code** | `UI/SOLHoloMenuMath.h` (`FSOLHoloMenuAnim`, `SOLHoloMenu::BeginOpen / BeginClose / Advance / SetHovered / Evaluate / OpenProgress / GlowScale`), `USOLHoloMenuSubsystem` (`PlayCue`, `FSOLHoloMenuParamCache::SanitizeStrength`), `USOLHologramCursorSubsystem` | Built renderer-free for #46 to reuse (SDD 42 D12). The cursor already follows `HoloMenuEffects` (SDD 42 D19) and stays as it is. |

Out of the restyle and unchanged: the flight HUD and every #42 hologram menu and card, the NPC stats overlay (Ctrl+F2,
`STATS_*`), the menu camera pawn, the menu flow and the settings model, save and subsystem.

**Tech debt in these areas** (`Docs/TECH_DEBT.md`, to respect, not re-report): `-SOLMenuSmoke=all` stale cursor checks
and "Thrust flies the ship" at Mars (44-47/50); hand-written menu prose in `SOLMenuText.h`; string-path assets not
cooked (`DirectoriesToAlwaysCook`), which now also covers the new `/Game/SOL/UI/Solid` assets.

## 4. Architecture

### 4.1 Overview

```
USOLSettingsSubsystem --(HudEffectsStrength, Q1)--> USOLSolidMenuSubsystem (world; created with USOLMenuSubsystem)
                                                    |- loads once: the menu font, M_SOLSolidHull / M_SOLSolidTrace,
                                                    |  the style textures (UPROPERTY TObjectPtr)
                                                    |- one shared MID per hull style; strength pushed only on change
                                                    v
  USOLMainMenuWidget / USOLPauseMenuWidget / USOLSettingsPageWidget / USOLDemoPageWidget / SOLMenuContent pages
     (logic and widgets unchanged, D7)  build with the kit instead of flat borders and default styles:
        USOLSolidPanel (hull panel or recess: an SDF material brush + content slot + SSOLSolidTraceLayer overlay)
        USOLMenuButton (restyled: hull-button brush, state in the brush tint, 3 px lift by render transform)
        SOLSolidStyle::MakeSliderStyle / MakeCheckBoxStyle / MakeScrollBarStyle / MenuFont (style factories)
     each screen runs FSOLHoloMenuAnim + SOLSolidMenu pure maths (open, power-up, pulse, hover look) from NativeTick
```

The kit lives in `Source/SOLTest/Menu/` (the feature folder of the screens it styles, STYLE_GUIDE section 14.2), each
type's `.h` and `.cpp` side by side.

### 4.2 New and changed types

| Type | Kind | Responsibility |
|---|---|---|
| `SOLSolidMenu` (`Menu/SOLSolidMenuMath.h/.cpp`) | Pure namespace, test-first | Hull spec clamping and the CPU mirror of the hull SDF; the button look (lift, edge, LED, underline, trace) from its highlight state and time; the brush-tint encoding of that look; panel-open offset and fade, trace power-up fraction, idle pulse; the pause slide-in; trace polylines (section 8) |
| `FSOLSolidHull` (same file) | Plain struct | One hull's shape: four corner chamfers, optional stepped top edge, side notches, chin, recess flag (section 5.2) |
| `USOLSolidMenuSubsystem` (`Menu/SOLSolidMenuSubsystem.h/.cpp`) | `UWorldSubsystem` | Loads the font, materials and textures once from `SOL::Paths` (one warning per missing asset, flat fallback); owns the shared MIDs (`UPROPERTY` `TObjectPtr`); pushes the effects strength into them only when it changes (`FSOLHoloMenuParamCache` pattern); hands out brushes and fonts to the kit |
| `USOLSolidPanel` (`Menu/SOLSolidPanel.h/.cpp`) | `UContentWidget` wrapping an `SOverlay` of an `SImage` (hull brush), the content and an `SSOLSolidTraceLayer` | A hull panel or recess with its content; its own MID only when its shape is unique (the few big panels); open / pulse look fed from the owning screen |
| `SSOLSolidTraceLayer` (`Menu/SOLSolidTraceLayer.h/.cpp`) | Slate leaf, hit-test invisible | Draws a panel's static circuit traces with ring terminals and the hovered button's extending trace, as `FSlateDrawElement::MakeLines` (a wide faint glow pass and a thin core pass) with the power-up fraction and pulse; point buffers reserved once (proposed) |
| `SOLSolidStyle` (`Menu/SOLSolidStyle.h/.cpp`) | Namespace of style factories | `FSliderStyle` (recessed groove, octagon knob with LED), `FCheckBoxStyle` (toggle slot, puck, LED on / off), `FScrollBarStyle` (thin recess, cyan thumb), `FButtonStyle` variants (stack button, tab, small stepper / rebind button), `MenuFont(weight, size)`; built once per widget at construction, never per frame |
| `USOLMenuButton` (changed) | `UButton` | Same API, delegate, index and hover-takes-focus rule. `Setup` gains a style variant (Stack, Tab, Small); `RefreshHighlight` (still called from the owner's tick, still cheap and skipping unchanged states) now drives the animated look: brush tint (state encoding), render-transform lift, label colour, and asks its panel's trace layer to extend the trace |
| `SOLMenuContent::MakeText` / `MakePage` (changed) | Builders | Use `SOLSolidStyle::MenuFont` and the solid text colours; `MakePage` heading in the solid heading style. Every page picks up the font through these two calls |

The screens keep their classes, members, delegates, public methods and verification getters (the smoke drives them).
Only the build code (`NativeOnInitialized`), the per-tick highlight refresh and the layout constants change; the
per-screen changes are in section 6.

### 4.3 The hull material (D8)

- **`M_SOLSolidHull`** (User Interface domain, translucent, created by a Python script, section 10). A signed-distance
  hull in the widget's local pixel space: four independent corner chamfers, an optional stepped top edge (title step),
  side notches, an optional chin, plus a recess mode (inverted bevel: inner shadow, light lip on the lower edge). Layers
  inside the SDF: the brushed grain (tiled texture, brushed along one axis, faint dither to avoid banding), a 1-2 px
  edge highlight, a bevel, a soft outer drop shadow (outside the SDF, inside the brush rect) and, for buttons, the
  accent edge, LED and underline driven by the state channels. Text never sits on the grain: content recesses are flat
  fills (research: "text sits on flat fills, never on texture").
- **Size.** The SDF needs the widget's size in pixels. The intended source is the `GetUserInterfaceUV` material
  function's size / pixel outputs; **part 46c verifies in the engine content at UE 5.8** which outputs exist. Fallback:
  a size parameter pushed into the panel's MID when its arranged size changes (event-driven; the big panels only, the
  buttons then use a fixed-height variant whose width comes from the UV derivative). Not a user decision.
- **No MID per button.** The 50+ buttons of the Settings page and the up to a few hundred start-location rows share one
  MID per style; each button's state (hover amount, pressed, selected, LED) travels in its **brush tint** (vertex
  colour), which `SetBackgroundColor` already sets per state today. Section 8.3 defines the encoding; the material
  decodes it. The panels with a unique shape (main menu hull, page shell, pause side panel) get their own MID, a
  handful in total.
- **`M_SOLSolidTrace`** is not needed if the trace layer's `MakeLines` look is accepted (two passes, glow and core);
  a material for the traces is a fallback if the lines look too plain at the screenshot check (proposed).
- **Time and strength.** The idle pulse is computed on the CPU from real application time (`FPlatformTime::Seconds`, as
  SDD 42 section 4.4) and fed as a scalar only to the panel MIDs and the trace layer, so a paused or warped world never
  stops it and the shared button MIDs never change per frame. Strength goes into every MID only when it changes.

**46c amendment: the size path (verified in the UE 5.8 engine).**

- `GetUserInterfaceUV` (`Engine/Content/Functions/UserInterface/GetUserInterfaceUV.uasset`) has the outputs **Normalized
  UV** ("a 0-1 UV set that spans the entire element"), **Pixel Size** ("the screen space size of the element being
  drawn"), **9-Slice UV**, **9-Slice UV (No Tiling)** and **Tiling**, all built from TextureCoordinate expressions.
  Pixel Size is UV channel 3: `SlateVertexShader.usf` passes the vertex's `PixelSize` (`ATTRIBUTE5`) as
  `InstanceParam.zw` = channel 3, and `FSlateVertex::Make(..., LocalSize, Scale, ...)` (`RenderingCommon.h`), which
  `FSlateElementBatcher::AddBoxElements` uses for every box and image brush, sets it to `round(LocalSize x DrawScale)`
  as `uint16`. `DrawScale` is the element's accumulated layout scale, so it **includes the DPI scale** (physical
  pixels) and **excludes render transforms** (the hover lift and the panel slide do not change it). The same `Make`
  sets channel 4 (`MaterialTexCoords`) to `LocalPosition / LocalSize`, an exact 0-1 UV even with brush margins.
- **Verdict: the Pixel Size path.** `M_SOLSolidHull` reads TexCoord 4 (normalized UV) and TexCoord 3 (size in physical
  pixels), so a shared material serves any button width and the MID-size fallback is not needed. The hull lengths are
  UMG units; the material turns them into pixels with the `DpiScale` scalar that `USOLSolidMenuSubsystem` pushes (with
  the strength) only when the game viewport's UMG DPI scale changes. Caveat: an element under an extra layout scale (a
  scale box) would get its features scaled by the viewport DPI scale, not its own; the solid menus have none.
- **Slate multiplies a material brush's colour by its tint.** For box and image draws (`ST_Default`),
  `SlateElementPixelShader.usf` `GetColor` returns `MaterialColor x VertexColor` (only custom-vertex draws skip it), so
  the section 8.3 encoding as written (a normal look is R = G = B = 0) would draw the hull black. Fix (no change to
  `EncodeButtonTint` / `DecodeButtonTint`): a hull brush carries `SOLSolidStyle::PackVertexTint(EncodeButtonTint(look))`
  (`SOLSolidStyle::BrushTint`), which maps R, G and B into [`TINT_VERTEX_FLOOR` 0.5, 1] (A unchanged); the material
  unpacks (`UnpackVertexTint`) before it decodes, and divides its output colour by the vertex tint so Slate's multiply
  cancels exactly. A stays the continuous dim and fade that Slate applies itself. The packing also widens the decode
  margins (a disabled G arrives at 0.75, a pressed G at 1). A white tint reads as pressed, selected and lit, so no
  hull brush is ever left white.
- **Slate's disabled effect on a material brush** only multiplies its alpha by 0.45 (`USE_LEGACY_DISABLED_EFFECT` 0);
  the RGB codes survive. It still stacks on the tint's `DISABLED_ALPHA`, so solid buttons do not use
  `SetIsEnabled(false)` (section 5.3.1); a disabled slider or toggle (Slate-disabled) gets disabled brushes at alpha 1.
- **Cost.** One Custom node: the hull clamp of section 8.1 runs per pixel (a shared material cannot pre-clamp for one
  width), then the SDF and the shading, roughly 100-150 ALU (an estimate: Slate shaders have no representative
  instruction count in the material statistics; measured in the 46h profile against the Q17 budget), one texture
  sample (grain) on raised hulls, none on recesses, and a second (the hatch mask) only on a disabled hull with the hatch
  cue (style 0; the committed slot cue, style 1, samples nothing). That is above
  section 13's "a few dozen ALU"; the 46h profile decides whether the clamp moves to the CPU for the unique panels.
- **No pulse in the hull material:** the idle pulse only drives the trace layer, so no hull MID changes per frame; the
  unique panel MIDs exist only for their shape (step, chin, notches), written once per `SetHull`. Rivets and the vent
  strip are texture brushes in `USOLSolidPanel` (not in the material), keeping it at two samples; the hazard stripe
  comes with the main menu in 46d.

### 4.4 Fonts (D4)

Rajdhani (Indian Type Foundry, SIL OFL 1.1, Google Fonts), weights Regular, Medium, SemiBold and Bold (proposed). The
TTF files and `OFL.txt` are kept in `Docs/Art/fonts/rajdhani/`; the import script makes `UFontFace` assets and one
composite `UFont` (`F_SOLMenu`) with the weights as typefaces (`FONT_REGULAR` "Regular", `FONT_BOLD` "Bold" keep their
names; "Medium" and "SemiBold" are added). The OFL text ships with the game (a licence credit line in About, Q10).
Rajdhani is narrower than Bahnschrift and the engine font, so sizes and letter spacing are tuned from screenshots at
1280x720 and 1000x800 (section 9, proposed values).

### 4.5 Reused #42 code (SDD 42 D12)

- **Open / close phase:** each animated panel holds an `FSOLHoloMenuAnim`; `SOLHoloMenu::BeginOpen`, `BeginClose`,
  `Advance` and `Progress` give the progress (and instant open at strength 0, as SDD 42 D18 for the hologram). The
  solid look functions of section 8 map that progress to the solid look (no frame draw-in or flicker).
- **Strength:** `FSOLHoloMenuParamCache::SanitizeStrength` (NaN 0, clamp 0-2) applied to `HudEffectsStrength` (Q1).
- **Cues (Q11):** the #42 open / hover / select cues through `USOLHoloMenuSubsystem::PlayCue` with its gate (SFX class,
  every strength, SDD 42 D21), as the in-flight menus use them; no new sounds.
- **Cursor:** the #22 hologram cursor stays as it is (D6).

### 4.6 Close without blocking input

As SDD 42 section 4.5: closing gives input back at once. The pause panel's slide-out and a page shell's close play from
the widget's tick after the input is already back; a reopen during the close reverses from the current progress. Play
from the main menu is unchanged: the world swaps pawns and the main menu widget is hidden in the same frame as today (no
close animation is waited for).

### 4.7 46c amendment: kit implementation notes (steps 3, 3a, 4, 5)

- **The hull material shades in linear space.** The first render showed every hull far too bright: UE 5.8 Slate
  defines `SOURCE_IN_LINEAR_SPACE 1` in `SlateShaderCommon.ush` only after it includes `Material.ush`, so the
  material's `#if SOURCE_IN_LINEAR_SPACE` tests were always false and it applied a display gamma twice (to the decoded
  tint and to its output, which Slate gamma-corrects again). `SlateVertexShader.usf` already turns the sRGB vertex
  colour linear, so `M_SOLSolidHull` now reads the tint as linear and returns linear colour with no `#if` (the
  `DisplayGamma` input is gone; `HUD_DISPLAY_GAMMA` is no longer one of its constants). Rebuilt with the script.
- **Button tint through the background colour.** `SButton` paints the style brush's tint x the inherited colour x
  its background colour, and the style brushes carry the Normal look (the kit tests pin that), so
  `USOLMenuButton` sets the background colour to the look's tint *divided by* the style brush's tint
  (`BrushTint(look) / BrushTint(Normal)`, or the flat colours' ratio without the material); the product the material
  decodes is exactly `BrushTint(look)`. `GetAppliedTint` returns that product.
- **Refresh cost.** Each solid screen's tick calls `USOLSolidMenuSubsystem::BeginFrame` once (it runs
  `UpdateForFrame`, a frame-counter compare after the first caller, and reads `FPlatformTime::Seconds` and the
  strength once) and hands the frame to `USOLMenuButton::RefreshHighlightAt` on each of its buttons (review fix: no
  per-button subsystem lookup or clock read). A refresh returns at once when the last one was already settled with the
  same state, selection and strength; otherwise it advances `FSOLSolidButtonAnim` and writes only what differs on
  screen (`DiffersOnScreen`: the 8-bit tint codes or the lift) from the look **last written** (review fix: comparing
  with the last refresh let ramp steps under `LIFT_EPSILON_PX` each add up to a lift that was never written; the
  release from a press to hover, Edge staying 1, showed it, `SOLTest.SolidMenuKit.Button.LiftFollowsFineRamp`).
  Disabling ends a running press linger at once.
- **Disabled buttons leave the focus order (review fix).** `SetSolidDisabled(true)` also makes the button not
  focusable (`UButton::InitIsFocusable` for a rebuild and the built `SButton`'s focusability, which `SButton` keeps
  protected, so a file-local accessor reaches it), so Tab navigation skips it as the hit-test grid's directional
  navigation already did (hit-test invisible); `SetSolidDisabled(false)` restores the focusability it had.
- **Panel brushes (review fix).** `USOLSolidPanel`'s hull, rivet and vent brushes are `UPROPERTY(Transient)` (the
  SImages hold them by pointer; the property keeps their material or texture referenced), so the subsystem's
  `Deinitialize` no longer resets its asset properties early. `SetHull` with another style recreates the unique
  material from that style's instance.
- **Textures the kit loads (review fix).** `USOLSolidMenuSubsystem` loads only the textures the kit draws itself,
  `T_SOLSolidRivet` and `T_SOLSolidVent` (`ESOLSolidTexture`). `M_SOLSolidHull` references `T_SOLSolidBrushed` (grain)
  and `T_SOLSolidHazard` (the cue mask) as its own texture parameters, so they load with it. **46c follow-up
  (user, 2026-11-05):** `T_SOLSolidNoise` (imported in 46b, never sampled: the hull dithers with a hash) was deleted
  with its source PNG, its `generate_textures.py` step, its import entry and `SOL::Paths::SOLID_TEX_NOISE`.
- **Dither (review fix).** The hull's dither is now +-0.5 / 255 of the sRGB output (scaled in linear by
  `2.2 x c^0.545`); the flat +-0.5 / 255 in linear was about +-2 output levels on the dark hull colours.
- **Font and text colours.** `SOLMenuContent::MakeText` uses F_SOLMenu: bold text the Bold face, other text Medium;
  button labels SemiBold, stack and tab labels with `LETTER_SPACING_STACK`. The `SOL::MenuStyle` font sizes now equal
  section 9's roles (heading 26, section 20, body 18, small 15; a `static_assert` ties them to `SOL::SolidMenu`).
  The old colours map to the solid palette: body text `TEXT_COLOR`, titles and page headings `TEXT_BRIGHT_COLOR`,
  the old accent (values, keys, section headings, the studio name) `CYAN_COLOR`, warnings `WARNING_COLOR`; the old dim
  tier (hints, the start-location and Demo help lines, the dirty text at rest) used `TEXT_COLOR` until the **46c
  follow-up (user, 2026-11-05)** added `TEXT_DIM_COLOR` #8A949D (`SOLSolidStyle::DimTextColor`, section 5.1); it now
  draws the main menu hint and the start-location help line, the pause hint, the Settings hint and the dirty text at
  rest, the Demo hint and its "no warnings" line, and the About copyright line. The Info page's row labels stay
  `TEXT_COLOR` (they are content, not hints). `BUTTON_NORMAL`, `BUTTON_PRESSED_TINT`,
  `BUTTON_FOCUSED`, `BUTTON_SELECTED`, `BUTTON_CORNER_RADIUS`, `DIM_TEXT_COLOR`, `ACCENT_COLOR` and `WARNING_COLOR` were
  removed from `SOL::MenuStyle`; `TEXT_COLOR` stays for the NPC stats overlay, `PANEL_BACKGROUND` and `SCREEN_DIM`
  until the 46d / 46g panels. Settings sliders keep the UMG slider until 46e (bar `RAISED_COLOR`, handle cyan).
- **Button padding.** `BUTTON_PADDING_Y` 8 -> 4: Rajdhani's taller line clipped the labels of the 34-unit small
  buttons (steppers, Rebind) at 8.
- **Variants on today's screens.** Main menu entries and the pause Quit choices: Stack; pause tabs: Tab; start-location
  rows, Settings steppers / actions / Rebind and every Demo button: Small. Selection uses the orthogonal flag.
- **Panel decor** (`USOLSolidPanel`, starting values): rivets `RIVET_SIZE_PX` 10 at `RIVET_INSET_PX` 16 tinted
  `RIVET_COLOR`; the vent strip `VENT_WIDTH_PX` 96 x `VENT_HEIGHT_PX` 12 at `VENT_INSET_PX` 24 tinted `LED_OFF_COLOR`.
  The hull image has no desired size (the content decides the panel's size).
- **`-SOLSmokeSolidMenu` (46c scaffold, completed in 46h).** Smoke settings slot, starts in the main menu, 1280x720:
  assets loaded (no fallback), the entry buttons on the hull material, a mouse-style hover and a Down key (one
  highlight; the settled look and its tint equal `EvaluateButton` / `BrushTint`), then `USOLSolidMenuSmokeBoard`, a
  hull panel with a title step (its own MID), rivets, vents and a trace, holding every control type enabled and
  disabled side by side (stack button, tab, small button, settings row, slider, toggle): selected + hovered,
  hover-only beside selected-only, strength 0 (the full hover look on the next frame, no glow) and strength 2 (glow 2).
  The screenshots were looked at: disabled reads clearly different on every type (flat dark hull without grain, no LED,
  dim label, the hatch band; the disabled toggle keeps its puck on the right with the LED off), a selected + hovered tab
  shows the amber edge, glow and lift with the cyan LED and underline (the mark takes the LED and the underline),
  hover-only is all amber, selected-only all cyan.

## 5. Visual specification

### 5.1 Palette (`SOL::SolidMenu`, sRGB hex; converted with `FLinearColor::FromSRGBColor`)

| Role | Hex | Use |
|---|---|---|
| Screen base / recess | #24282C | Content recesses, flat fills behind text |
| Panel hull | #343A40 | Hull panels |
| Raised | #4A5159 | Buttons, tabs at rest |
| Raised, lifted | #58626B | Hovered / focused button hull |
| Pressed | #1C2024 | Pressed button hull |
| Edge highlight | #8C969F | 1-2 px edge lines, bevel light |
| Shadow / vent / LED off | #121416 / #0B0D0F | Shadows, vent slots, unlit LEDs |
| Rivet | #6A737C | Rivets |
| Text | **#A4AFB8** (D10) | Body text and button labels at rest (Q12) |
| Bright text | #DCE3E8 | Titles, headings and the hovered / focused label (Q12) |
| Dim text (46c follow-up, user 2026-11-05) | #8A949D | Hints and secondary lines (key hints, help lines, the dirty text at rest, the copyright line). `TEXT_DIM_COLOR`. Contrast about 3.8:1 on the panel hull, 4.8:1 on the recess: below body text's 5.15:1 by design, above the 3:1 non-text floor; only for short secondary lines, never body copy |
| Cyan trace | **#28D2F0** (D10) | **Every** cyan of the solid menus (Q13): circuit traces, ring terminals, lit LEDs at rest, slider fill, the selected underline, values and keys |
| Amber hover / selected | **#FFA628** (D10) | Hover / keyboard-focus only (Q9): edge, LED, underline and trace. The selected state is cyan, not amber |
| Warning | **#E5533D** (Q14) | Red-orange, so a warning never reads as hover: key conflicts, the dirty indicator, Demo warnings (replaces today's orange `WARNING_COLOR`) |
| Disabled hull (46c amendment, starting value) | #303234 | A disabled button's hull: near-grey, darker than Raised, distinct from every enabled hull. `DISABLED_HULL_COLOR` |
| Disabled text (46c amendment, starting value) | #65696C | A disabled label: near-grey, darker than Text, brighter than the disabled hull. `DISABLED_TEXT_COLOR` |
| Disabled cue (46c amendment, starting value) | #3C3C3A | The disabled hatch / cue: a dull colour that never reads as amber or cyan. `DISABLED_CUE_COLOR` |

Palette values are data in `SOLConstants.h`, never baked into a texture (research: Elite's baked colour lesson); the
brushed tile is a neutral grey tinted in the material.

### 5.2 Shape language (round 2 C)

- **Hull panel:** unequal 45° chamfers (proposed: top-left 28, top-right 12, bottom-right 28, bottom-left 12 UMG
  units), a stepped top edge carrying the title, a notch in each side, a vented chin at the bottom, octagonal rivets near
  the corners, a row of vent slots top-right (the five-bar mark of the boards), circuit traces with ring terminals along
  the lower edge.
- **Recess:** a sunken flat sub-panel (inner shadow, light lower lip) that groups controls or holds text.
- **Stack button / tab:** raised hull with a cut top-left corner and a pointed right cap (the arrow tabs of board C),
  an LED left of the label. Small buttons (stepper '<' / '>', Rebind, footer) use the same hull without the point.
- **Slider:** a recessed groove, a cyan fill trace up to the value, an octagonal knob with an LED.
- **Toggle:** a recessed slot with a sliding puck and an LED (cyan on, dark off).
- **Hazard stripe:** amber / black stripe under the Quit entry (main menu) and the Quit to desktop button (pause).
- **Scroll bar:** a thin recess with a cyan thumb.

### 5.3 Button states (D10, D11)

| State | Look |
|---|---|
| Normal | Raised hull #4A5159, LED off, label #A4AFB8 |
| Hover = keyboard focus (one highlight, as today) | Lift 3 UMG units up (render transform, no relayout), hull #58626B, amber accent edge, amber LED lit, amber underline, the circuit trace extends from the button's right end into the panel (amber, only where Q15 allows), label #DCE3E8 (Q12) |
| Pressed | Hull #1C2024, no lift, edge kept; shown at least `PRESSED_LINGER_S` so a quick click is seen (FreeSpace 2's 100 ms, proposed) |
| Selected (active tab / page / start body) | Cyan underline + cyan LED, no lift, no amber (Q9). **46c amendment:** selected is orthogonal to hover / focus / press: a selected button that is hovered or focused shows the full amber hover look (lift included) plus the cyan underline and LED, a pressed one the pressed hull plus the mark; hover-only is amber, selected-only is cyan |
| Disabled | **46c amendment (user requirement, 2026-11-04):** must never look like an enabled control. Desaturated steel (`DISABLED_HULL_COLOR` #303234, no brushed grain, no edge highlight, half the bevel), LED off, no trace, no hover / glow response, label `DISABLED_TEXT_COLOR` #65696C, the tint's `DISABLED_ALPHA` dim, and a distinct cue (section 5.3.1). Applies to buttons, tabs, sliders, toggles and rows |

#### 5.3.1 Disabled art (46c amendment)

- **Where.** The disabled look is decoded from the brush tint's G code (`TINT_DISABLED_CODE`) by `M_SOLSolidHull`, so
  every hull style has it: the button variants, the slider groove and knob, the toggle slot and puck. A disabled hull
  ignores the hover amount (no lift, edge, LED, underline, glow or trace).
- **Cue, a material parameter (`DisabledCueStyle`) with two options. 46c follow-up: the user chose (b), the dark slotted
  recess (2026-11-05); it is the committed default (`SOL::SolidMenu::DISABLED_CUE_STYLE` 1).** The constant is not read
  by C++: `create_solid_menu_materials.py` bakes it into `M_SOLSolidHull` as the parameter default, and
  `SOLTest.SolidMenuKit.Subsystem.WorldLoadsAndPushes` checks that every style material carries it. (a) **Hatched
  stripe** (style 0, kept as an option): a band `CUE_BAND_PX` tall above the bottom edge, inside
  the hull, hatched with 45-degree stripes in `DISABLED_CUE_COLOR` #3C3C3A, using `T_SOLSolidHazard` as the mask
  (one stripe period every `CUE_PERIOD_PX`; no new texture). (b) **Dark slotted recess** (1): the same band as a dark
  slot (`SHADOW_COLOR`) with a faint lit lower lip (`CUE_LIP`), the default. Both are in the shared material; switching
  is one constant and a re-run of the material script.
- **Buttons (`USOLMenuButton::SetSolidDisabled`)** never call `SetIsEnabled(false)` (Slate's disabled effect would
  multiply the hull and label alpha by 0.45 on top of `DISABLED_ALPHA`): the button turns hit-test invisible, refuses
  hover focus and swallows clicks, and its owner moves keyboard focus off it. (46i) A button that has the keyboard
  focus when it is disabled hands it on itself: the file-local `MenuButtonFocusOwner` (`Menu/SOLMenuButton.cpp`) gives
  it to the nearest focusable user widget that owns the button, or clears it, so a disabled button never keeps the
  focus while its owner has not moved it yet.
- **Sliders and toggles** are disabled with Slate's own `SetIsEnabled(false)` (it blocks input, drag and navigation)
  and show disabled brushes: `MakeSliderStyle` fills `DisabledBarImage` / `DisabledThumbImage`, and a toggle (whose
  `FCheckBoxStyle` has no disabled images) gets `MakeCheckBoxStyle(subsystem, true)` from its owner; those brushes
  keep alpha 1, since Slate's 0.45 is the dim.

The trace extension (D11) applies only on the main menu stack, the pause side panel stack and the shell's tab column (Q15). The settings rows (steppers, sliders, toggles, Rebind) get an amber edge and underline only, no trace; every button gets the lift, edge, LED and
underline.

## 6. Per-screen restyle

All dimensions are UMG units at the 1080p reference DPI (proposed, section 9).

| Screen | Restyle (logic and widgets unchanged, D7) | Resolved by |
|---|---|---|
| **Main menu** (`USOLMainMenuWidget`) | The column becomes a **hull panel** (D12): title "SOLTest" in the stepped top edge with the tagline under it, the 7 entries as **stack buttons** (Play, Demo, Start: <body>, Controls, Settings, About, Quit; the hazard stripe under Quit), the key hint under the stack, and a **small bottom-left recess** in the chin holding the version / studio / engine line (Q10). Circuit traces run along the panel's lower edge; the hovered entry's trace extends to the right edge of the hull (toward the page shell, Q15). The version line (Q10) reads `v0.1.YYMM.<commit count>`, then studio and engine. The page panel becomes the **page shell** (section 6.1). Panel-open plays when the main menu first shows and when returning from flight; the traces power up after it; the idle pulse runs while it shows | Q5, Q10 |
| **Start-location page** | In the page shell, no tab column: the body text, then the body list in a recess with a solid scroll bar; rows are small buttons, the start body is the selected row (cyan underline + LED, Q9) | Q5, Q9 |
| **Settings** (`USOLSettingsPageWidget`, both menus) | The page shell **with a vertical tab column** (D13): the six existing sections become six tabs (Graphics, Audio, Mouse, Gameplay, Overlays, Key bindings); the content area shows the chosen section's rows in a recess (labels left, controls right; sliders, toggles and steppers from `SOLSolidStyle`); the footer (dirty text, status line, Revert, Apply) stays under the content for every tab; Settings always opens on the first tab, Graphics, with no last-tab memory (Q8); `ScrollToBinding` first switches to Key bindings. Every control keeps its handler and the setters the smoke uses | Q8, Q15 |
| **Embedded Overlay list** (in the Overlays tab) | Solid text font and colours, the profile tabs as small tab buttons, rows on the recess, the highlight in the solid hover look; the standalone Ctrl+O menu keeps its hologram frame (`SetEmbedded(true)` path only) | - |
| **Controls / Info / About** (`SOLMenuContent` builders) | The same page shell as Settings with read-only content (D14): key and value columns in recesses, the key cells in cyan, headings in the heading style. Today's entries are kept (main menu: Controls, About; pause: Controls, Info, About) and each page's sections become tabs (Controls: its 10 areas, Info: Flight, Time and travel, Combat, How it works, About: two tabs, Game and Credits, user-confirmed 2026-11-05) | Q6 |
| **Demo** (`USOLDemoPageWidget`, both menus) | The page shell; no tab column (Q7): presets as small buttons on top, the 6 sections as recessed groups in one scroll, warnings in the warning colour, the footer buttons (Reset, Start demo / Stop NPCs, Apply) bottom right | Q7 |
| **Pause** (`USOLPauseMenuWidget`) | A **side panel on the left** (D15), full height minus margins, sliding in from the left edge; "PAUSED" in its stepped top edge; the 7 tabs become a vertical stack of stack buttons; the live flight view behind is only dimmed, the simulation keeps running (Q3). The pages open in the shell to the right of the side panel, Quit's two choices inside the side panel (Q4) | Q3, Q4 |

### 6.1 The page shell

One kit composition (`USOLSolidPanel` hull + optional tab column + content recess + bottom-right recess with a **Back**
button + the footer row when the page has one), used by every page of both menus. The Back button does exactly what Esc
does on that page today (close the page; on the pause menu Esc with no page open still resumes, Q4); it is a new button but no new input path (D6).
Proposed size: 1040 x 760 (the tab column 200 wide), so a Settings row keeps its 250 label column, a slider and the 80
value column; the main menu's column + gap + shell then spans about 1540 of 1920 units.

### 6.2 46d amendment: the main menu and the page shell as built

- **Column** (`USOLMainMenuWidget::BuildColumn`): a `USOLSolidPanel` (Panel style, its own material) `COLUMN_WIDTH` 380
  x `COLUMN_HEIGHT` (= `PAGE_HEIGHT`) with the panel chamfers, a title step (`COLUMN_TITLE_STEP_WIDTH` 290 x
  `COLUMN_TITLE_STEP_HEIGHT` 12), a chin (`COLUMN_CHIN_WIDTH` 300 x `COLUMN_CHIN_HEIGHT` 14), rivets and vents. Every
  row has a fixed height (`COLUMN_TITLE_HEIGHT` 76, `COLUMN_TAGLINE_HEIGHT` 28, `COLUMN_STACK_GAP` 20, the stack at
  `BUTTON_HEIGHT` 52 + `BUTTON_GAP` 10, `HAZARD_HEIGHT_PX` 10, the hint after `COLUMN_HINT_GAP` 14, the version
  recess `VERSION_RECESS_HEIGHT` 58 at the bottom), so the traces are computed once from the constants: each entry's
  hover trace runs from the button's right end (the point) to the hull's right edge (`COLUMN_WIDTH - PANEL_INSET_PX`),
  and three static traces with ring terminals run in two rows (`COLUMN_TRACE_GAP` 16 apart, 45-degree bends) above the
  version recess. The studio name is no longer a line of its own under the tagline: it is in the version line.
- **Menu focus (user decision 2026-11-05).** After the main menu shows (first show and every return from flight)
  the keyboard focus lands on Play: `USOLMenuSubsystem::RefreshWidgets` passes `GetDefaultButton()` (Play; the pause
  menu's selected tab) as the input mode's `WidgetToFocus` instead of the menu root, whose deferred focus change used
  to land after `FocusDefault` and leave no entry focused. Play therefore shows lit amber at rest whenever the menu
  shows (accepted). The `-SOLSmokeSolidMenu` check `t-return-play-trace` asserts Play focused on the first frame back
  and a settle later.
- **Title.** "SOLTest" uses the title size (56, Bold) **without** the section 9 title letter spacing (400): with it the
  word is about 450 units wide and does not fit the 380 column; as in 46c it draws untracked.
- **Hazard stripe:** `T_SOLSolidHazard` (now loaded by the subsystem, `ESOLSolidTexture::Hazard`) as a horizontally
  tiled image brush (`HAZARD_TILE_WIDTH_PX` 80 x 10) tinted `AMBER_COLOR`; the mask's black stays black; a flat amber bar
  when the texture is missing.
- **Version recess:** bottom-left in the chin (a Recess panel from the content's left edge to the chin's flank), the
  line `SOLBuildVersion::FormatVersionLine(CurrentVersion(), engine version)` in `DimTextColor` at the small size,
  auto-wrapped: at 380 units the line takes two lines (`v0.1.2611.135 · ARS LLC · UE 5.8.3`; with the short credit `SOLMenuText::STUDIO_NAME_SHORT` "ARS LLC", user 2026-11-05, the line takes two lines, breaking before "UE 5.8.3" because the format joins "UE" and the version with a no-break space; the About page keeps the full `STUDIO_NAME`). The
  separator is the middle dot (U+00B7), which Rajdhani draws (checked on the screenshots).
- **Page shell** (`BuildShell`): a Panel hull with its own material (the panel chamfers and a notch in each side,
  `SHELL_NOTCH_DEPTH` 6 x `SHELL_NOTCH_HEIGHT` 72 at half height), rivets and vents; inside (`SHELL_PADDING` 28) the
  content recess (`SHELL_RECESS_PADDING` 20) with the page switcher and, under it after `SHELL_BACK_GAP` 12, a
  bottom-right recess (`SHELL_BACK_RECESS_PADDING` 6) with **Back** (Small variant, `BACK_BUTTON_WIDTH` 160 x
  `SETTINGS_ROW_HEIGHT` 34, `SOLMenuText::PAGE_BACK`). Back and Esc both call `ClosePage` (the page closes, the focus goes
  to Play); no new input path. No tab column and no traces on the shell in 46d.
- **Layout values changed.** `PAGE_HEIGHT` 760 -> **800** (the column, level with the shell, had no room for its traces
  between the hint and the version recess at 760). The shell sits in a Fill slot of the row with a `PAGE_WIDTH` x
  `PAGE_HEIGHT` size box, so it is 1040 wide where the view has room and **narrows** to the room right of the column
  where it does not: at 1000x800 (DPI scale 0.74, 1350 units across) it is about 787 units wide and everything fits
  (smoke `h-fit-*`); at 1280x720 and wider it is the full 1040. `SOL::MenuStyle::PAGE_WIDTH` / `PAGE_HEIGHT` (the flat
  page panel) were removed; `PANEL_BACKGROUND` stays for the pause panel until 46g.
- **Start-location page:** the body text, then the rows (Small buttons) in a recess list (`LIST_RECESS_PADDING` 10)
  with `SOLSolidStyle::MakeScrollBarStyle` and `SCROLL_BAR_THICKNESS`; the start body's row is selected (`SetSelected`,
  the cyan underline and LED), kept in sync by `RefreshStartBody`.
- **Animations.** The column's `FSOLHoloMenuAnim` opens through `SOLSolidMenu::BeginPanelOpen` /
  `PanelProgress` / `AdvancePanel` (the new duration-parameter forms of the #42 `SOLHoloMenu` phase functions at
  `PANEL_OPEN_S` / `CLOSE_S`; the #42 forms forward with `OPEN_S` / `CLOSE_S`, unchanged) each time the menu is added to
  the viewport (`NativeConstruct`, which also binds the entries' hover traces to the rebuilt trace layer), with
  `ApplyOpenLook` (fade, `PANEL_SLIDE_PX` slide from the left); `TracePower` runs from the open's start (dark until
  `TRACE_POWER_DELAY_S`, lit at 0.5 s), `IdlePulse` while shown. The shell opens with a page (`BeginPanelOpen`, from the
  left too) and on `ShowPage(None)` the page state is None and the focus is back at once while the shell closes over
  `CLOSE_S` hit-test invisible with the old page still drawn, then hides and drops to the empty switcher child; a reopen
  during the close reverses. At strength 0 everything is at its end on the same frame. Nothing runs while the menu is
  not in the viewport (no tick).
- **Power-up LED blink (review fix; an interpretation of section 7's "the LEDs blink on in stack order", open to the
  user).** At rest a stack button's LED is off (section 5.3), and the hull material ties the LED to the amber edge (the
  tint's R), so the blink is drawn by the column's trace layer, not the buttons: during the trace power-up each entry's
  LED (`BUTTON_LED_X_PX` from the button's left, its vertical centre) shows as a filled amber dot of `LED_RADIUS_PX`
  from `TRACE_POWER_DELAY_S + TRACE_POWER_S x i / 7` for `LED_BLINK_S` 0.1 s, top to bottom, and **ends off**
  (`SOLSolidMenu::PowerUpLedLit`). An entry whose own LED already shows (hovered or focused, pressed, selected,
  disabled) is skipped. At strength 0 there is no blink; once `IsPowerUpRunning` is false the widget stops computing
  it (no per-frame work, the mask stays 0, no repaint).
- **Idle pulse dot (review fix).** A faint cyan dot (`LED_RADIUS_PX`, alpha `PULSE_DOT_ALPHA` 0.35 x s, at most 1,
  times the pulse's core alpha) travels along the longest static trace (the first, found once in `SetTraces`) over
  each `IDLE_PULSE_PERIOD_S`, start to end (`PulseDotFraction`, `PulseDotAlpha`), drawn only once the traces are fully
  lit. One more exact-size line array per paint while it shows; at strength 0 its alpha is 0, the layer ignores the
  place and never repaints for it.
- **Reshow (review fix).** `PlayShowAnimation` (each add to the viewport) snaps the shell closed whenever it is not
  Closed or a page is set: a page left open at Play ends its Settings visit, the page is None, the shell is hidden at
  once (no close animation on the return to the main menu) and the selection marks are cleared. A button given a new
  trace layer (the panel rebuilt on the return) pushes its current look's hover trace at once, since a settled button
  writes nothing on later refreshes, so the focused Play keeps its trace.
- **Smoke (SDD 14 steps 1-3 for the main menu).** `-SOLSmokeSolidMenu` grew from 13 to 36 checks: the column panel
  (own material, 7 stack buttons, 3 traces), the version line (equal to `FormatVersionLine(CurrentVersion(), engine)`,
  shape `0.1.DDDD.D+`), the first open finished, the replayed open (Opening with dark traces, the power-up mid-way,
  monotonic, Open), the hover and Down hover traces, each page opened by its entry's click handler and closed by Back
  (Enter on the focused Back) or Esc alternately (page None and the focus on Play at once, the shell closing
  input-transparent, then hidden), a reopen during a close (no pop), strength 0 (column, shell and hover at once) and 2
  (glow 2), and the view at 1000x800 with Settings and Start location (column and shell inside the view). The
  screenshots at 1280x720 and 1000x800 were looked at: no clipped text; the shell is 1040 wide at 1280x720 and about 787
  at 1000x800. The review fixes added `a-hint-clear` (the wrapped key hint ends at least `COLUMN_HINT_CLEARANCE_PX`
  above trace row A, from the laid-out geometry), `a-led-blink` (every entry whose own LED is dark blinked, no other,
  and the blink ended off), `a-pulse-dot` (the dot on trace 0 at `PulseDotAlpha(1)`), `s-main-s0-lights` (no blink, no
  dot at strength 0) and the return from flight (`PlayReturn`: Play focused and settled with About open, Play, Esc,
  Quit to menu): `t-return-shell-closed` (page None, Closed, hidden, no selection on the first frame back) and
  `t-return-play-trace` (Play focused with its hover trace drawn, on the first frame back and a settle later).

### 6.3 46e amendment: Settings with the tab column as built

- **Tab logic** (`Menu/SOLSettingsTabs.h/.cpp`, pure, test-first: 16 `SOLTest.SolidSettings.*` tests written from the
  contract by a separate agent, red on the missing header, then green): `ESOLSettingsTab` (Graphics, Audio, Mouse,
  Gameplay, Overlays, Key bindings), `OpeningTab` (Graphics), `SelectTab` (an invalid request keeps the tab, an invalid
  current one falls back to the opening tab), `TabForBinding` (Key bindings for a valid binding), `TabOfSlider` /
  `TabOfToggle` / `TabOfStepper` (each control's old section), `TabLabel` (the section headings) and the button
  variants (`TabButtonVariant` Tab, `RowButtonVariant` Small).
- **Layout** (`USOLSettingsPageWidget`, inside the shell's content recess on the main menu and the flat pause panel
  until 46g): the heading and hint, then a row of the **tab rail** (`USOLSolidPanel::MakeRecess`, now the one recess
  factory the main menu uses too, `TAB_COLUMN_WIDTH` wide, the six Tab buttons top-aligned, `TAB_HEIGHT` tall and
  `BUTTON_GAP` apart, `SETTINGS_TAB_RAIL_PADDING` 8 from the rail's edge) and the **content recess**
  (`SETTINGS_CONTENT_PADDING` 10, `SETTINGS_BODY_GAP` 12 right of the rail) holding a `UWidgetSwitcher` with one scroll
  box per tab (the solid scroll bar); then the footer (dirty text, Revert, Apply) and the status line under both, for
  every tab. Each tab shows its old section's rows without a section heading (the tab names it); the Overlays tab opens
  with the explanation the old heading carried (`SETTINGS_OVERLAYS_HINT`, dim), since `SETTINGS_OVERLAYS` is now the
  tab label "Overlays" (it was "Overlays (what the overlay key turns on and off)", too long for a tab).
- **Tuned from the screenshots.** `TAB_COLUMN_WIDTH` 200 -> **220** and the tab labels' letter spacing
  `SETTINGS_TAB_LETTER_SPACING` **100** (`USOLMenuButton::SetLabelLetterSpacing`; the stack's 260 is kept everywhere
  else): at 200 and 260 "Key bindings" ran past its tab and every label touched the LED. Each tab is `TAB_COLUMN_WIDTH
  - 2 x 8 - SETTINGS_TAB_TRACE_PX` (14) = 190 wide.
- **Hover (Q15).** The tab buttons are the Tab variant: their hover trace (a straight run at the tab's centre line from
  its point to `SETTINGS_TAB_TRACE_END_PX` inside the rail's edge) is drawn by the rail's trace layer, bound in
  `NativeConstruct` (the layer is rebuilt with the page); the page pushes the effects strength to that layer only when
  it changes. Every row button (steppers, Rebind, Reset keys, Revert, Apply) is Small: lift, amber edge, LED and
  underline, no trace. The sliders and toggles use `MakeSliderStyle` / `MakeCheckBoxStyle` (bar and handle colours
  white so the brush tints reach the material). The tab buttons are refreshed in the page's tick with the rest (a
  state compare when settled); they are not in the row button array, so the stepper, footer and Rebind indices are
  unchanged.
- **Opening tab (Q8).** `RefreshFromSettings` (what both menus call when they show the page) shows `OpeningTab()` and
  re-reads the controls; Apply, Revert and `NotifyHidden` call the new private `RefreshControls`, which leaves the tab,
  so an Apply on Key bindings stays on Key bindings. `ScrollToBinding` shows `TabForBinding`'s tab first, then asks the
  Key bindings scroll box to scroll (the scroll box runs the request on its next tick, once the newly shown tab is laid
  out). New on the page for the smokes: `SelectTab`, `GetSelectedTab`, `GetShownContentIndex`, `GetTabButton`,
  `GetStepperButton`, `GetRevertButton` / `GetApplyButton`, `GetSlider` / `GetToggle`, `IsTabHoverTraceShown`,
  `IsAnyTabHoverTraceShown`, `IsBindingRowInView`; `GetOverlayList` returns a non-const pointer.
- **Animations.** No new animation: the tabs and rows ramp with the kit's hover (`HOVER_RAMP_S`, instant and flat at
  strength 0, glow doubled at 2) inside the shell's open of 46d. No Settings control is disabled today; the slider
  style carries the disabled brushes and a toggle would take `MakeCheckBoxStyle(solid, true)` if one were.
- **Embedded Overlay list** (`USOLOverlayMenuWidget`, `SetEmbedded(true)` only): the menu font (Body, Medium) on the
  tabs and rows, the Small font on the dim hint, `BUTTON_PADDING_X / Y` around each entry so the hull's cut and LED
  clear the text; the profile tabs on the Small hull at rest too (the viewed one with the selected mark, the active profile's text
  cyan, a disabled one's dim); rows on the recess, the one under the mouse on the Small hull in the settled hover look
  (a brush written on a hover change only, not per frame; no 0.12 s ramp, the list has no tick). The hover follows real
  mouse moves (a synthetic move after a layout change leaves it, as the standalone rows do) and clears on leave and
  on every show of the page (`ClearEmbeddedHover` from `RefreshFromSettings`, which also drops the tab rail's trace
  unless a tab still has the keyboard focus). A hovered row gets no lift (a `UBorder` has no render translation).
  Review follow-up: `BeginRebind` shows the Key bindings tab, so a rebind started from code never listens on a
  hidden row. The
  standalone Ctrl+O menu is unchanged (`-SOLSmokeHoloMenu`, `-SOLSmokeOverlay`).
- **Narrow views (resolved in the 46e follow-ups, section 6.4).** At 1000x800 the shell narrows to about 787 units
  (6.2), and with the tab column the content had about 413 units for a row: the 250 label column left about 77 units
  between a stepper's buttons, so "1000 x 800" and "Borderless window" no longer fit. The value text is clipped to its
  slot (`ClipToBounds`; each of the three values then draws in its own batch, part of UI draws 28 -> 31; the review's
  `ETextOverflowPolicy::Ellipsis` was tried and cut nothing, the row box gives the text its full width) instead of
  drawing over the buttons, as a guard; the user chose a narrower label column (section 6.4).
- **Smoke (SDD 14 step 4).** `-SOLSmokeSolidMenu` grew from 42 to 55 checks: `u-settings-first-tab` (six solid Tab
  buttons, Graphics shown and the only one selected, the footer laid out), `u-settings-solid-rows` (8 sliders and 6
  toggles on the solid styles, 6 Small stepper buttons), `u-tab-audio` ... `u-tab-keys` (each tab clicked as the mouse
  does: shown, the only one selected, its hover trace drawn, the footer kept), `u-overlay-solid` (the embedded list's
  rows in F_SOLMenu, a hovered row), `u-row-hover` (the Quality stepper's '>' in the settled hover look with no trace
  drawn on the rail), `u-controls-revert` (a slider, a toggle and a stepper make the page dirty; Revert restores them,
  the tab kept), `u-scroll-binding` (from Graphics, Key bindings at once, the last binding's row inside the scroll box
  after a settle), `u-reopen-first-tab` (left on Key bindings, Esc, Settings again: Graphics), `u-settings-closed`.
- **Left for the user (answered 2026-11-05, section 6.4):** the slider fill, two amber highlights at once, per-tab
  scroll memory and the narrow-view stepper values; the short "Overlays" tab label with its dim hint line and
  `TAB_COLUMN_WIDTH` 220 were kept as built.

### 6.4 46e follow-ups (the user's decisions of 2026-11-05)

Six items left open by 46e were decided by the user: (1) the short "Overlays" tab label and its dim hint line stay;
(2) `TAB_COLUMN_WIDTH` stays 220; (3) a narrower label column so the stepper values fit at 1000x800; (4) the cyan
slider fill of section 5.2; (5) one highlight: the mouse over another control wins over a button's keyboard focus;
(6) every Settings tab back at its top on every open. As built:

- **Narrow views (3).** Measured in Rajdhani Medium 18 (the body text) as drawn (glyphs at the layout scale, 0.740 at
  1000x800, 0.666 at 1280x720): the Graphics labels are at most about 139 units ("Window mode"), the longest stepper
  value about 186 ("Borderless window"), but the other tabs' labels need up to about 266 ("Chase Down safety margin",
  which already ran 6 units into its slider at 1000x800 with the shared 250 column). One narrower column for every tab
  would cut those, so the label column is per tab (`SOLSettingsTabs::LabelColumnWidth`): Graphics, the tab with the
  steppers, `SOL::SolidMenu::SETTINGS_GRAPHICS_LABEL_WIDTH` **134**; Gameplay `SETTINGS_GAMEPLAY_LABEL_WIDTH` **264**;
  every other tab keeps `SETTINGS_LABEL_WIDTH` 250 (the Demo page too). A label may run into the 10-unit gap after its
  column, never into its control. The 413-unit row at 1000x800 was still too short (139 + 10 + two 44 buttons + 186 =
  423), so the 10-unit gap that each row kept on its right for the scroll bar moved onto the scroll bar itself
  (`UScrollBox::SetScrollbarPadding`, left `BUTTON_GAP`): a tab whose bar is hidden (every tab but Key bindings)
  gives its rows the full width. At 1000x800 the value slot is now 190.9 units (186.4 drawn) and "Window mode" ends
  4.9 units before its '<' button; at 1280x720 and 1920x1080 (the same 1040-unit shell) the slot is 444 and the
  stepper buttons sit further apart. The smoke measures every value and label with Slate's font measure at the
  layout scale against its laid-out slot or its column plus the gap (`u-stepper-values`, `h-small-stepper-values`,
  `u-labels`, `h-small-labels`). Two key rows still run past their column (known debt).
- **Slider fill (4).** `USOLSolidSlider` (`Menu/SOLSolidSlider.h/.cpp`) is a `USlider` whose Slate widget,
  `SSOLSolidSlider`, paints SSlider's horizontal geometry with one more box between the groove and the knob: the fill,
  from the groove's start plus `SLIDER_FILL_INSET_PX` 2 to the knob's centre (`SOLSolidStyle::SliderFill`, pure and
  tested), `SLIDER_FILL_THICKNESS` 4 units tall and centred in the 8-unit groove, in a flat `CYAN_COLOR` brush
  (`SOLSolidStyle::MakeSliderFillBrush`; Slate's disabled effect dims it on a disabled slider). The knob covers the
  fill's end, so the visible run ends at the knob; at 0% nothing is drawn. The fill reads the same normalized value as
  the knob, so it follows a drag, a key step and `SetValue` alike. `USOLSolidSlider::SetSolidStyle` gives the groove
  and knob style, white bar and handle colours and the fill brush; the Settings page (all eight sliders, including the
  HUD and menu effects sliders of Gameplay) and the smoke board use it. A vertical slider would paint without a fill
  (none exists).
- **One highlight (5).** `FSOLSolidButtonAnim::ResolveState` gained `bHoverElsewhere`: a focused button resolves to
  Normal (its rest look, the selected mark kept) while the mouse is over another control that takes no keyboard focus
  on hover. The Settings page computes it (`IsHoverOnFocuslessControl`: a slider or toggle of the shown tab under the
  mouse, or an embedded Overlay tab or row) and passes it to every tab and row button's `RefreshHighlightAt`; the
  main menu (its column entries and Back, while Settings is the open page) and the pause menu (its tabs and Quit
  choices, on the Settings tab) read the same function live and pass it to their own buttons, so a clicked Settings
  entry or pause tab drops its amber too. Last input wins (review fix): hover wins only while the last input was a
  real mouse move or press (the page's input pre-processor: a move with a non-zero cursor delta, Slate's synthetic
  moves have none); any key press gives the focus its highlight back until the mouse moves again, so keyboard use
  with the cursor resting on a slider still shows the focus. The hovered tab's rail trace falls with the look. Found on the way: a button whose look settled without a visible
  tint change left the last step of its falling trace drawn (a stub at the tab's tip); `RefreshHighlightFor` now feeds
  the trace on every unsettled refresh.
- **Scroll position (6).** `RefreshFromSettings` (every show of the page, both menus) calls `ScrollToStart` on every
  tab's scroll box next to the opening tab of Q8.
- **Smoke.** `-SOLSmokeSolidMenu` grew from 55 to 66 checks: `u-stepper-values` / `u-labels` (1280x720) and
  `h-small-stepper-values` / `h-small-labels` (1000x800, the longest window mode pending for the screenshot, then
  reverted), `u-slider-fill` (Master volume read at 0%, 100% and 50% and the other Audio sliders at 50%: each painted
  fill's width against its value within 0.5 units, the cyan brush), `u-slider-fill-drag` (a real Slate mouse drag from
  25% to 75% of the bar: captured, the value 0.75, the fill following it), `u-hover-wins-slider`, `-key`, `-overlay`
  and `-column` (the Audio tab focused and lit with its trace, the real cursor over Master volume: the tab unlit with
  no rail trace; a key with the cursor still there: lit again; a real move: unlit; the cursor parked: lit; the same
  with the Overlays tab and an embedded row; the main menu's focused Settings entry unlit while a row is hovered),
  `u-reopen-scroll-top` (Key bindings scrolled to its last row, Esc, Settings again: every tab at offset 0).
  `u-tab-overlays` expects the Overlays tab's trace dark when hover wins there (the smoke hovers an embedded row for
  the screenshot) and drawn otherwise. The pause menu path is the same function, not driven by this smoke.
- **Profile.** `-SOLSmokeSolidMenuProfileTab=<n>` makes every profile pass show one Settings tab; the slider fill was
  measured on Gameplay (four sliders), section 13.1.

### 6.5 46f amendment: Controls, Info, About and Demo as built

- **The tab rail is a kit piece.** `FSOLSolidTabRail` (USTRUCT, `Menu/SOLSolidTabRail.h/.cpp`) was promoted out of
  `USOLSettingsPageWidget`: `Build`, `Bind`, `Select`, `Tick`, `ClearStaleHover`, `IsTraceShown`, and the pure
  `SOLSolidTabRail::TraceForTab` and `RailHeight`. Settings uses it with unchanged behavior.
- **Read-only pages (Q6).** `USOLReadOnlyPageWidget` (`Menu/SOLReadOnlyPageWidget.h/.cpp`) hosts Controls, Info and
  About: the rail beside a content recess with one scroll box per tab in a switcher. `SOLMenuContent::BuildControlsPage` /
  `BuildInfoPage` / `BuildAboutPage` return it and `FillControlsTabs` / `FillInfoTabs` / `FillAboutTabs` fill it from
  today's entries. The pure `SOLPageTabs` (`Menu/SOLPageTabs.h/.cpp`, `ESOLReadOnlyPage`) decides the tabs: **Controls**
  one tab per contiguous area of `SOLMenuText::CONTROL_ROWS` (10: Flight, Targeting, Combat, Time, Overlays, HUD, Map,
  Routes, Menu, Demo); **Info** Flight, Time, Combat, How it works (the tab label is `INFO_TIME_TAB` "Time" because
  "Time and travel" was clipped in the 190-unit tab, the heading in the content stays "Time and travel"); **About**
  Game (`ABOUT_TAB_GAME`: title, studio, description, engine version, copyright) and Credits.
- **Behavior.** Each page opens on its first tab with every tab scrolled to its top on `ShowOpening` (called by the main
  and pause menus when the page shows; Q8). The hover trace is only on the tab column (rows are plain text). Keys in
  Controls and Info still refresh from the live keys (`RefreshControlsPage` / `RefreshInfoPage`, unchanged).
- **Demo (Q7).** No tab column: the presets and each of the option sections are recess groups
  (`USOLSolidPanel::MakeRecess`, `SETTINGS_CONTENT_PADDING`) in one scroll box with the solid scroll bar style,
  warnings in the warning colour, footer unchanged; `RefreshFromMenu` scrolls back to the top.
- **No new constants, no schema change.** The Settings key-row label overflow in TECH_DEBT is in the Settings page and
  was not touched.
- **Tests.** 14 new `SOLTest.SolidPages.*` tests (`Tests/SolidPageTabsTest.cpp`, written by a separate agent from the
  contract); full suite 1476/1476.
- **Smoke.** `-SOLSmokeSolidMenu` 83/83 (was 66): `p-controls-tabs` / `-fit` / `-tab-switch`, `p-about-*`,
  `p-demo-layout`, `r-about-first-tab`, `i-info-*` (an Info page put on the viewport, the main menu has none),
  `h-fit-controls`, `h-controls-*`, `h-fit-demo`, `h-demo-layout` at 1000x800. The 1280x720 Controls / About fit is
  not read: a page sliding in leaves its children's cached geometry stale, so the same page is measured at 1000x800 with
  the shell already open (TECH_DEBT). Holo 86/86, overlay 34/34, hud 24/24, `-SOLMenuSmoke=all` 47 passed / 3 failed
  (the same TECH_DEBT stale checks: thrust, Esc, Ctrl+F1 resume).
- **Profile.** Three runs at 1920x1080, all PASS against the baseline (`cpu_slate` / `gpu_ui`, ms): P0 0.448 / 0.437 /
  0.452 and 0.104 / 0.105 / 0.107, P1 0.470 / 0.419 / 0.479 and 0.062 x3, P2 0.378 / 0.391 / 0.373 and 0.046-0.047, P3
  0.406 / 0.382 / 0.421 and 0.091 / 0.082 / 0.080. The profile covers the Settings screens only; the new pages add no
  per-frame work beyond the rail tick.
- **Review findings and fixes:** see plan 46f step 5 (no High finding; fixes applied, the rest in TECH_DEBT or left for the user).
- **Tab re-click is a no-op (user decision, after 46f).** Clicking the already-selected tab does nothing and resets
  nothing: not the page's tab, not its scroll. Pure logic: `SOLPageTabs::IsTabChange(current, requested, count)`,
  `SOLSettingsTabs::IsTabChange(current, requested)` and `SOLMenuState::IsTabChange(state, tab)` (true only for a
  different, valid tab while a menu shows). `USOLReadOnlyPageWidget` and `USOLSettingsPageWidget` skip a re-click in
  `HandleTabClicked`, and `USOLMenuSubsystem::SelectTab` returns early on the selected pause-menu tab (it used to call
  `ShowTab` again, which re-ran `ShowOpening` / `RefreshFromSettings` and snapped the page back to its first tab and
  top). Opening a page (the main menu entry, the pause menu opening) still resets to the first tab (Q8). `SelectTab`'s own
  contract is unchanged, so its tests stand. Checks: `p-controls` / `p-about` / `i-info` / `h-controls`
  `-tab-reclick` and `u-tab-*-reclick` in `-SOLSmokeSolidMenu` (92/92).

### 6.6 46g amendment: the pause side panel and scrolling rails as built

- **Side panel (D15, Q3, Q4).** `USOLPauseMenuWidget` is a left side panel (`USOLSolidPanel` hull `PAUSE_PANEL_WIDTH`
  420 wide, 380 since 46i (section 6.7), full height minus `SCREEN_MARGIN`, "PAUSED" on the stepped top edge) over the
  live view, only dimmed
  (`PAUSE_DIM_ALPHA` 0.55; the simulation keeps running, `PAUSED_BODY` and its smoke check unchanged). The 7 tabs are
  stack buttons with hover traces and power-up LEDs; under them a switcher shows the paused text and key hint, or with
  Quit selected the Quit prompt and its two choices (Q4). The page tabs (`SOLMenuState::IsPageTab`: Controls, Info,
  Settings, Demo, About) open their page in the main menu's page shell right of the panel, with a Back button.
- **Slide and close (section 4.6).** `PlayOpen` slides the panel in over `PAUSE_SLIDE_S` 0.25 s from the panel width
  plus `SCREEN_MARGIN` plus `PAUSE_SLIDE_MARGIN_PX` 16 past its place (`PauseSlideX`: 516 units with the 420-unit panel,
  given here as 500 before 46i; 476 since 46i), so it starts wholly past the left edge; `BeginClose` makes the widget
  hit-test invisible at once (the
  input is back on the first frame in flight) and slides it out over `CLOSE_S` 0.15 s with the shell, then the widget
  leaves the viewport in its tick. A re-pause during the slide-out reverses it from where it is: the power-up, the LED
  blink and the stack's scroll are reset (and the open cue played) only when the panel was Closed. At strength 0
  everything is instant.
- **Pause close flow (Q4).** Pure `SOLMenuState::EscapeClosesPage` (paused with a page tab selected) and
  `SOLMenuState::ClosePage` (to the Resume tab, still paused); `USOLMenuSubsystem::ClosePausePage` applies it at once
  for Esc, Ctrl+F1 and Back on a page and focuses Resume. Esc with no page resumes, as before; `Apply`'s Esc contract
  is unchanged. The hint reads `PAUSED_HINT` "Esc: close page / resume", and the Controls page's Menu / Escape row says
  the same.
- **Cues (Q11).** The #42 cues through `USOLSolidMenuSubsystem::PlayCue`, no new asset: `USOLMenuButton` plays Hover
  when it gains the keyboard focus (not when solid-disabled) and Select on a click; `SOLMenuContent::BeginPanelOpenWithCue`
  plays Open only for an open from Closed. A hover takes the focus only on a real mouse move
  (`HasRealMouseMoveThisFrame`, from a pass-through Slate input pre-processor), so a list scrolled under a still cursor
  does not take the keyboard's focus.
- **Scrolling rails (D17).** Pure maths in `SOLSolidScroll` (`Menu/SOLSolidScroll.h/.cpp`: `NeedsScroll`, `MaxOffset`,
  `ClampOffset`, `OffsetToShow`, `StackItemTop`, `StackContentHeight`, `StackViewHeight`, `IsLineInView`) and the kit
  piece `FSOLSolidScrollStack` (`Menu/SOLSolidScrollStack.h/.cpp`): a `UScrollBox` with the solid bar around a vertical
  box of equal items, optionally in a size box capped at a number of items (the cap is stored and applied as items are
  added, never a 0 height). While the items fit (within `SCROLL_FIT_TOLERANCE_PX` 0.5) the bar is collapsed, nothing
  clips and the trace layer gets no view band; once they do not, the bar shows, the box clips, a newly focused item
  scrolls whole into view and the trace layer (`SSOLSolidTraceLayer::SetScroll`, pushed only on a change, to a weakly
  held layer) shifts the hover traces and LEDs with the scroll and clips them to the band. `FSOLSolidTabRail` uses it
  (`Select` scrolls the selected tab into view); the main-menu column and the pause stack are capped at
  `STACK_MAX_VISIBLE_ENTRIES` 7 (today's 7 entries fit). A scrolling rail's bar sits in the hover-trace gap, the trace
  drawn over it.
- **Fit at 1280x720.** No stack or rail scrolls: the pause Controls rail's 10 tabs take 560 units of a 562.5-unit view
  (the TECH_DEBT estimate of 558 of 576 units is resolved). The scroll paths are smoke-checked on a column capped at 4
  entries and on a Controls page put on the viewport 300 units tall.
- **De-duplication.** The main and pause menus share `SOLMenuContent::MakeSized`, `MakeEdgeTraces` and
  `BeginPanelOpenWithCue`. `SOLSolidScroll::VisibleRange` and `FSOLScrollWindow` (unused) were removed with their tests.
- **Tests.** 27 new tests, written from the contract: `SOLTest.SolidScroll.*` (`Tests/SolidScrollTest.cpp`) and
  `SOLTest.SolidPause.*` (`Tests/SolidPauseTest.cpp`); full suite 1506/1506.
- **Smoke.** `-SOLSmokeSolidMenu` 169/169 (was 92): cue deltas (`a-open-cue`, `b-hover-cue`, `c-cue-click`,
  `p-*-open-cue`, `r-reopen-no-cue`), the column and rail scroll steps, every rail laid out without a scroll, the pause
  steps (`v-pause-stack` with `IsTabHoverTraceShown`, page close by Esc and Back, `v-resume` and `v-resume-input` (a
  failure if its frame is skipped), the slide-out, the re-pause reversal, strength 0, Quit to menu with the pause menu
  out of the viewport). Holo 86/86, overlay 34/34, hud 24/24, `-SOLMenuSmoke=all` 47 passed / 3 failed (the TECH_DEBT
  stale checks; the review-fix run had 44 / 6, as the pre-46g logs of 2026-11-05, when the Play checks also see the
  focused window's free cursor).
- **Profile (three runs each at 1920x1080; `cpu_slate` / `gpu_ui` medians in ms; baseline 0.423-0.444 /
  0.027-0.075).** An A/B in one session against `HEAD` (46f, the old flat pause), HEAD -> 46g: P0 main-settings
  0.476 -> 0.582 / 0.105 -> 0.105, P1 0.495 -> 0.545 / 0.062 -> 0.061, P2 pause-settings 0.435 -> 0.558 /
  0.047 -> 0.077, P3 0.421 -> 0.587 / 0.081 -> 0.140. The GPU stays well inside the 0.3 ms budget; the CPU extra over
  the baseline is +0.10 to +0.16 ms in these medians (the review-fix run before: +0.12 / +0.10 / +0.05 / +0.11), so the
  0.1 ms CPU budget is exceeded on most screens. The run-to-run noise is about 0.1 ms, but the A/B shows 46g itself
  costs about +0.05-0.1 ms on the main menu and +0.12-0.17 ms on the pause menu. One cheap win was applied: a fitting
  stack pushes no view band, so its trace layer pushes no clip zone (2 fewer UI draws per screen: 34 -> 32 and
  33 -> 31). Open for the user (TECH_DEBT): no CPU margin is left; the next step is an Insights capture of the menu
  tick and paint (scroll boxes, button refresh) in 46h.
- **Review (opus, xhigh) and fixes.** All applied: the hover cue only on a focus gained and the hover focus only on a
  real mouse move; cue smoke checks; `PlayOpen` resets only from Closed; the trace layer clips the hover traces and LEDs
  to the scroll band; the stack's cap stored and applied lazily and its pushed layer a weak pointer; `Select` scrolls
  into view; `VisibleRange` / `FSOLScrollWindow` deleted; `IsTabHoverTraceShown` in `v-pause-stack`;
  `v-resume-input`'s skipped-frame failure and the Quit-to-menu viewport assert; the shared helpers in
  `SOLMenuContent`; lines over 120 columns wrapped.
- **The implementer's choices, for the user to confirm.** Resume no longer shows the selected mark (the opening tab is
  focused, not marked); the slide distance is the panel width plus `SCREEN_MARGIN` and `PAUSE_SLIDE_MARGIN_PX` (516
  units then, 476 since 46i); the hover cue plays with the open cue when a menu focuses its default button on open; a
  scrolling rail's bar sits in the hover-trace gap with the trace drawn over it; a re-pause during the slide-out plays
  no open cue (it reverses, it does not open from Closed).

### 6.7 46i amendment: final review fixes

- **Pause side panel width (confirmed by the user, 2026-11-06).** `PAUSE_PANEL_WIDTH` is `COLUMN_WIDTH` 380 (was 420), so the
  pause shell has the main menu shell's room at every view size and every main-menu fit check holds in the pause menu.
  At 1000x800 (a 1350 x 1080-unit view) both shells are 786 units and the Graphics stepper value slot is 190.9 units,
  wide enough for the widest value "Borderless window" (186.4; at 420 the slot was 150.9 and the value was clipped
  to "orderless windo", the 46h TECH_DEBT entry, now removed). Any panel wider than about 384 clips it again. The slide
  follows the width: 380 + `SCREEN_MARGIN` 80 + `PAUSE_SLIDE_MARGIN_PX` 16 = 476 units, so the panel still starts and
  ends wholly past the left edge. At 380 the panel's own content fits (PAUSED on the stepped edge, the seven tab
  labels, the Quit prompt and both choices with the hazard stripe). The old label "Quit to main menu" (about 274 units
  with the stack letter spacing) filled its button's label box (about 271 units) and touched the LED dot; the user
  chose the shorter label "Quit to menu" (`SOLMenuText::QUIT_TO_MENU`, user decision below) and kept the width 380.
- **Info label column in narrow rows.** The pure `SOLPageTabs::LeftColumnWidth(page, rowWidth)`: Controls always
  `KEY_COLUMN_WIDTH` 230; Info `LABEL_COLUMN_WIDTH` 400 while the value column beside it keeps
  `VALUE_COLUMN_MIN_WIDTH` 220 after the `BUTTON_GAP`, else `LABEL_COLUMN_NARROW_WIDTH` 200 with the labels wrapping;
  400 for a row not laid out yet; About none. `USOLReadOnlyPageWidget::FitLeftColumn` (in its tick) reads the content
  switcher's laid-out width less a scroll bar's room and rewrites the label boxes only when the width changes (the
  first frame of a page shown in a new view size still has the old column). Each row registers its left text, its box
  and its right cell (`FSOLReadOnlyColumnText`). Measured: at 1000x800 the Info value cells are 194.9 to 212.9 units
  (they were about 0 to 13 beside the 400 column), at 1280x720 the column stays 400 with 266-unit values (unchanged).
  Controls at 1000x800: action cells 164.9 to 182.9 units.
- **Smokes give GameUserSettings back.** Every menu smoke flag already chose the smoke save slot; it now also copies
  the engine's `UGameUserSettings` at boot (`FSOLEngineSettingsSnapshot`, `Menu/SOLEngineSettingsSnapshot.h`: the
  config text of every config property, 39 elements in UE 5.8, written with `ExportTextItem_Direct` because the
  delta-less `ExportText_InContainer` writes nothing for a zero value, plus the engine's scalability levels) and in
  `USOLSettingsSubsystem::Deinitialize` (the game instance's shutdown, after `UGameEngine::PreExit`'s unconditional
  `SaveSettings`) writes it back without applying it and saves once. A windowed smoke's resizes are stored as the
  confirmed resolution by `UGameEngine::OnViewportResized`, which is what overwrote the player's file before. Both
  smoke runs of 46i left `Saved/Config/WindowsEditor/GameUserSettings.ini` byte for byte unchanged (4 values given
  back each: the resolution and the confirmed resolution). The normal path is unchanged; no save schema, config layout
  or version changed. The smoke's `ApplyStrength` now reverts every other pending value before its Apply.
- **Review fixes, settings (R1-R5).** R1: while a smoke has the snapshot, `FlushSave` never calls the engine's
  `SaveSettings` mid-run (a Quality Apply would have written the player's file; the log says "GameUserSettings write
  skipped"); the values stay in memory and the shutdown gives the boot values back. R2-R5, the snapshot: the engine's
  scalability levels are copied too and set back through `Scalability::SetQualityLevels` (SaveSettings writes the
  engine's levels to `[ScalabilityGroups]`, not the settings object's); the copy is restored only into a settings
  object of the captured class (a weak class pointer); `WindowPositions` is left to SaveSettings, which reloads it from
  the config cache before it writes; nothing is saved when no value differed (`RestoreTo` counts the values it wrote
  back). Tested in `SOLTest.SettingsSnapshot.*`.
- **Review fixes, buttons and focus (R6, R7).** R6: a button disabled while it has the keyboard focus hands it to its
  nearest focusable owning user widget (`MenuButtonFocusOwner`, section 5.3.1). R7: `FSOLSolidMenuFrame` carries the
  keyboard-focused widget, read once per screen tick by `BeginFrame`, and `RefreshHighlightAt(frame, ...)` compares the
  button's widget with it, so a settled button costs a pointer compare, a press read and a state compare (no Slate focus
  query per button). Hidden tabs: the Settings page refreshes only the shown tab's buttons, the footer's and any hidden
  button not `IsAtRest` (a ramp, press or focus still to play out), so the 38 Rebind buttons cost nothing while another
  tab shows; a newly shown tab's buttons are refreshed at once.
- **Review fixes, kit and pages.** The panel phase functions (`PanelProgress`, `BeginPanelOpen`, `BeginPanelClose`,
  `AdvancePanel`, `BeginPanelOpenWithCue`) gain forms with an explicit open / close duration: the pause side panel
  slides in over `PAUSE_SLIDE_S` and out over `CLOSE_S` (section 7; it used `PANEL_OPEN_S` before). The pause menu's
  Quit to desktop gets the hazard stripe under it, as the main menu's Quit has (section 5.2;
  `SOLMenuContent::MakeHazardStripe`, `HAZARD_HEIGHT_PX` 10). `EMenuTab::Count` ends the pause tabs, and
  `IsTabChange` is true only for a valid tab other than the selected one (a value past Quit changes nothing); the
  sized-box helper is shared (`SOLMenuContent::MakeSized`). The trace layer's core pass follows the pulse smoothly
  (`CorePassAlpha`, section 7).
- **User decisions (2026-11-06).** (B1) The stack LEDs' power-up blink is cyan (`CYAN_COLOR` #28D2F0, was amber);
  amber stays for hover. (B2) The selected pause tab reads as selected, Resume included (the cyan underline and LED
  mark, Q9). (B3) The pause menu's first Quit choice reads "Quit to menu" (was "Quit to main menu", which touched its
  LED dot at 380), and `PAUSE_PANEL_WIDTH` stays 380. (B4) Key bindings at 1000x800, in both menus, narrow like the
  Info page: see the next bullet; widescreen views are unchanged.
- **Narrow Settings rows (B4).** The pure `SOLSettingsTabs::IsNarrowLayout(tab, rowWidth)`, the two-argument
  `LabelColumnWidth(tab, rowWidth)` and `RebindWidth(rowWidth)` (the one-argument `LabelColumnWidth` is unchanged):
  Key bindings narrows when its key text would keep less than `SETTINGS_KEY_TEXT_MIN_WIDTH` 120 beside the wide 250
  column, the gap and the 120 Rebind (a row under 500 units), to a label column of `SETTINGS_KEYS_NARROW_LABEL_WIDTH`
  168 and Rebind boxes of `SETTINGS_REBIND_NARROW_WIDTH` 104 (the button needs 103.1: "Rebind" and 18 padding a side;
  96 clipped it; since 46j a minimum width, so a box takes the button's desired width if that is ever wider); Gameplay narrows when a slider would keep less than `SETTINGS_SLIDER_MIN_WIDTH` 160 beside its 264
  column and the value readout (a row under 514), to `SETTINGS_GAMEPLAY_NARROW_LABEL_WIDTH` 150 (the most that still
  leaves a slider 160 at 1000x800); no other tab narrows, nor a row not laid out yet. Two narrow widths, not one: at
  150 the long key labels would wrap to three lines, at 168 the Gameplay sliders would drop below 160.
  `USOLSettingsPageWidget::FitNarrowRows` (each tick) reads the content switcher's width less a scroll bar's room (the
  Info page's rule) and rewrites only on a change of a tab's narrow state: the column width, the labels' wrap, the row
  boxes (a minimum height of `SETTINGS_ROW_HEIGHT` 34 when narrow, so a wrapped label grows its row, the fixed 34 when
  wide) and the Rebind boxes (held to 34 tall, so one-line key rows keep their height). Measured at 1000x800 (layout
  scale 0.740): rows 404.9 units; key text slot 122.9 (was 24.9), the widest key name "Backspace" 104.0, so every name
  fits beside its Rebind; Gameplay sliders 164.9 (were 68.9); the widest wrapped word 121.6 (Key bindings) and 94.6
  (Gameplay); the tallest wrapped key row 62.1 (two lines); on Gameplay 6 of 7 labels wrap, the tallest row 93.2 (three
  lines, "Mouse steering at start"), so the tab scrolls. At 1280x720 the rows are 658.0 and both tabs stay wide. Seen
  in the smoke: a key row scrolled out of view applies its wrap once it is painted (TECH_DEBT).
- **Smoke.** `-SOLSmokeSolidMenu` 192/192 (was 177 at 46h; about 100 s; 193/193 since the 46j review fixes, which add
  `j-settings-wide`, the narrow rows back to wide at 1280x720, and make the row and Rebind checks able to fail): `CheckSettingsFit` takes the page, so the
  pause menu's Settings is measured at 1000x800 (`h-pause-small-stepper-values`, `h-pause-small-labels`, and
  `h-pause-small-values-in-shell`; the first would fail at 420, its slot 150.9) and measures each label against the
  column actually applied (the widest word on a narrow tab); a rows sweep (`TickRowsSweep` / `CheckSweepTab`) shows
  each Controls and Info tab with a left column in turn and checks every row's box width, the widest word of each cell
  inside it and both cells inside the content and the shell (`h-controls-rows` and `h-pause-small-controls-rows` /
  `-info-rows` at 1000x800, `v-controls-rows` / `v-info-rows` at 1280x720, with screenshots of the Info tabs). The
  narrow rows (B4) are checked in both menus at 1000x800 (`h-small-gameplay-rows`, `h-small-gameplay-sliders`,
  `h-small-keys-rows`, `h-small-keys-names`, `h-pause-small-keys-rows`, `h-pause-small-keys-names`: the narrow column,
  each word and wrapped label inside its column and row, each key name inside its slot left of a Rebind box with
  room for its button, with shots) and the wide layout at 1280x720 (`u-settings-wide`). `SOLID_SMOKE_EXPECTED_CHECKS`
  192 (profile: 9 default, 14 a view run) makes a run that logs another number of checks fail (`z-check-count`); a
  missing page or pause menu logs a FAIL instead of skipping; `f-s0-instant` reads only its own step; the
  `small-settings` shot keeps the pending window mode (reverted by the next step); the comparison board's tab-row
  traces use `TAB_HEIGHT` rows and the frame form of `RefreshHighlightAt`. The real-cursor stages bring the game
  window to the front and retry a stage whose hover or capture did not take (at most `SOLID_SMOKE_CURSOR_RETRIES` 3,
  each logged as a warning).
- **Left as tech debt.** The Info page's "rad/s²" shows a missing-glyph box for "²" in the menu font. In wide views
  two Key bindings labels ("Steer with the mouse (hold)" 276.3, "Open the map in Jump mode" 280.8) still run past their
  260-unit column and gap (widescreen unchanged, per the user).
- **Profile (SDD 13 budget: under 0.2 ms CPU, 0.3 ms GPU a screen).** Three default runs, 4/4 PASS and 9/9 checks
  each; medians of `dcpu` / `dgpu` (ms): P0 main-settings 1920x1080 +0.076 / +0.063, P1 main-settings 1280x720 +0.015
  / +0.033, P2 pause-settings 1280x720 +0.090 / +0.038, P3 pause-settings 1920x1080 +0.028 / +0.064 (the highest
  single run P2 +0.169).
- **Tests and regression.** `SOLTest.SettingsSnapshot.*` (`Tests/SettingsSnapshotTest.cpp`: RoundTrip, Coverage,
  Empty, on a transient `UGameUserSettings`; written after the code, and it caught the zero-value export),
  `SOLTest.SolidPages.LeftColumn.Width`, `SOLTest.SolidSettings.Layout.NarrowLayout` / `.NarrowWidths` (both sides of
  each threshold, the other tabs, zero, negative and NaN widths, the room a narrow row keeps at 1000x800) and the
  review's kit, page and pause tests; full suite 1537/1537. Holo 86/86, overlay 34/34, HUD 24/24, `-SOLMenuSmoke=all`
  47 / 3 (the TECH_DEBT stale cursor and thrust checks). `Saved/Config/WindowsEditor/GameUserSettings.ini` was byte
  for byte unchanged after every run.

## 7. Animations and the effects strength (D5)

`s` is the sanitized `HudEffectsStrength` (Q1, 0-2, default 1). The behaviour at 0 and 2 is the same rule as #42
(Q2, SDD 42 D18 / D21) and is used below.

| Animation | Behaviour (starting values approved in Q16) |
|---|---|
| **Panel open** | The hull fades up and slides `PANEL_SLIDE_PX` 24 in from its anchor side over `PANEL_OPEN_S` 0.25 s (ease-out); the content fades with it. The pause side panel slides its full width in from the left edge over `PAUSE_SLIDE_S` 0.25 s while the dim fades up. At `s = 0`: no slide or fade, the panel is there at once (`OpenProgress` already returns 1 at strength 0) |
| **Circuit power-up** | After `TRACE_POWER_DELAY_S` 0.15 s the traces light from their terminals along their length over `TRACE_POWER_S` 0.35 s, the LEDs blink on in stack order. At `s = 0`: traces lit at once, flat (no glow pass) |
| **Idle pulse** | The traces' brightness swings by `IDLE_PULSE_DEPTH` 0.15 x `s` with a period of `IDLE_PULSE_PERIOD_S` 4 s (a smooth sine), a faint dot travels along the longest trace every period. None at `s = 0` |
| **Hover** | Lift, edge, LED, underline and trace fade in over `HOVER_RAMP_S` 0.12 s, out over the same; the glow halo scales with `s`; at `s = 0` the hover look is instant and flat but **fully present** (lift, amber edge, LED, underline), so hover never depends on the effects (research: state clarity beats ornament) |
| **Close** | Pause slide-out and shell close over `CLOSE_S` 0.15 s after the input is back (section 4.6); instant at `s = 0` |
| **Strength 2** | Glow halo and pulse depth doubled; durations unchanged |

Nothing animates while idle except the pulse, which is a few float ops per tick for the visible panels plus their trace
layers' repaint; with `s = 0` the menus are static (no pulse, so nothing invalidates). (46c amendment: Slate still
repaints the trace layers every frame at any strength, as it does every widget, since the menus are not under an
invalidation panel; the layer's paint-only invalidation saves work only where it is cached. See section 13.)

(46i amendment) The trace core line's alpha follows the pulse smoothly: `CorePassAlpha(pulse, s)` = (1 + pulse -
`IDLE_PULSE_DEPTH` x s) / 2 with s = min(s, `HUD_EFFECTS_MAX`), so over the pulse's swing [1 - depth x s,
1 + depth x s] the core swings in [1 - depth x s, 1] and reaches full brightness at the pulse's top instead of
clipping flat for half the period; at s <= 0 it is the pulse itself (1, no pulse), clamped to [0, 1]. The glow pass
keeps `TRACE_GLOW_ALPHA` x min(s, `HUD_EFFECTS_MAX`) x pulse.

## 8. Pure logic contracts (`Menu/SOLSolidMenuMath`, test-first)

Written for the separate test author (`Source/SOLTest/Tests/SolidMenuTest.cpp`, `SOLTest.SolidMenu.*`). Headless: no
Slate, no RHI. Every function takes `nowS` or an elapsed time as an argument (no clock inside). `s` is sanitized as
`FSOLHoloMenuParamCache::SanitizeStrength`. Exact numbers come from `SOL::SolidMenu` (section 9), so the tests read the
constants, not literals.

### 8.1 Hull

```
struct FSOLSolidHull { float ChamferTL, ChamferTR, ChamferBR, ChamferBL;   // UMG units, >= 0
                       float TitleStepWidth = 0, TitleStepHeight = 0;       // 0 = no stepped top edge
                       float NotchY = -1, NotchDepth = 0, NotchHeight = 0;  // side notches, NotchY < 0 = none
                       float ChinWidth = 0, ChinHeight = 0;                 // 0 = no chin
                       bool bRecess = false; };
```

| Function | Contract |
|---|---|
| `FSOLSolidHull ClampHull(const FSOLSolidHull& hull, FVector2f size)` | Negative or NaN values read 0. Chamfers are scaled down together so that on every edge the two chamfers sum to at most `HULL_MAX_CHAMFER_FRACTION` of that edge; the title step, notches and chin are clamped inside the rect and never overlap a chamfer; a size with a component `<= 0` or non-finite returns a hull with every value 0 |
| `float HullDistance(FVector2f p, FVector2f size, const FSOLSolidHull& hull)` | The CPU mirror of the material's SDF: negative inside, 0 on the edge, positive outside; exact on the straight edges and the chamfer diagonals (to 1e-3); a cut corner's tip region is outside. Used by the tests and by the material script as the spec (the script's HLSL mirrors it, as `create_hud_shape_material.py` mirrors `SOLHudShapeMath`) |

**46a amendment (implementation choices, not user decisions).** The feature geometry follows the round 2 C board:

- **Title step:** the top edge is at y 0 left of `TitleStepWidth`, then a 45° flank down to y `TitleStepHeight`, which
  the rest of the top edge keeps; the top-right chamfer sits on that lowered corner `(w, TitleStepHeight)`.
- **Chin:** the mirror at the bottom: the bottom edge is at y `h` left of `ChinWidth`, then a 45° flank up to
  `h - ChinHeight`; the bottom-right chamfer sits on the corner `(w, h - ChinHeight)`.
- **Notches:** a 45° trapezoid cut into both sides, `NotchHeight` tall at the edge, centred on `NotchY`, `NotchDepth`
  deep.
- **SDF:** the max of the chamfered rect's half-plane distances, then `max(d, -cut)` for the step, chin and notch cuts
  (each a max of half-planes). A feature with a zero size is skipped.
- **Clamp:** a step or chin with a zero width or height, before or after the clamp, is removed (both 0, so a lone
  height never lowers a chamfer); the step / chin height is at most
  `HULL_MAX_FEATURE_FRACTION` (0.25, new constant) of the height and at most `w` minus its edge's two chamfers; the
  flank starts at or past the near chamfer (width at least `ChamferTL` / `ChamferBL`) and ends before the far one
  (width at most `w - far chamfer - height`). The notch spans only the straight part both sides share,
  `[max(TL, step height + TR), min(h - BL, h - chin height - BR)]`; its depth is at most half its height (a V at the
  limit) and `HULL_MAX_FEATURE_FRACTION` of the width; a notch with `NotchY < 0` or NaN, or a zero depth or height, or
  no room, is removed as `NotchY -1`, depth and height 0. Non-finite lengths read 0. The clamp is idempotent.
- `HullDistance` expects a clamped hull and returns `FLT_MAX` for an invalid size or a non-finite point.

### 8.2 Button look

```
enum class ESOLSolidButtonState : uint8 { Normal, Hovered, Pressed, Selected, Disabled };
struct FSOLSolidButtonLook { float Lift; float Edge; float Led; float Underline; float Trace;   // each 0-1
                             float Glow;                                                        // 0-2
                             bool bPressedHull; bool bSelectedMark; bool bDisabled; };
```

| Function | Contract |
|---|---|
| `FSOLSolidButtonLook EvaluateButton(ESOLSolidButtonState state, ESOLSolidButtonState previous, double sinceChangeS, float s, bool bSelected = false)` | **46c amendment:** `bSelected` is orthogonal to the state. With Normal, Hovered or Pressed it sets `bSelectedMark` and changes nothing else (the full amber hover look incl. lift, plus the cyan mark). State Selected is Normal plus the mark (the mark is always on, whatever `bSelected`). Disabled ignores `bSelected` (all 0, no mark, `bDisabled`). During the press linger the mark follows `bSelected` or state Selected. Hovered: `Lift`, `Edge`, `Led`, `Underline` and `Trace` rise linearly from the previous state's value to 1 over `HOVER_RAMP_S` (instant at `s = 0`); leaving Hovered falls over the same time. `Glow = s * Edge` with `s` sanitized to [0, 2], so 0 at `s = 0` and **doubled at `s = 2`** (section 7, Q2; 46a amendment: was `GlowScale(s) * Edge`, which caps at 1 and so contradicted section 7). Pressed: `bPressedHull`, `Lift` 0, `Edge` (and `Led`, `Underline`, `Trace`) 1, and a Pressed that ends before `PRESSED_LINGER_S` still reports Pressed until then (the caller passes the press time; the ramp to the new state starts when the linger ends). Selected: `bSelectedMark` (cyan, Q9), no lift. Disabled: everything 0 and `bDisabled` (46a amendment: the flag is what lets `EncodeButtonTint` give a disabled look its `DISABLED_ALPHA`, since its amounts equal a Normal look's). NaN or negative `sinceChangeS` reads 0. Values always in [0, 1], `Glow` in [0, 2] |
| `FSOLSolidButtonLook EvaluateButtonFrom(ESOLSolidButtonState state, const FSOLSolidButtonLook& atChange, double sinceChangeS, float s, bool bSelected = false)` | 46c amendment: the same `bSelected` rule as `EvaluateButton`. 46a review addition: the same ramp, but from the look the button showed at the change (its amounts clamped to [0, 1]), so a hover that reverses mid-ramp continues instead of popping (the state form ramps from the previous state's *settled* look). No press linger: the caller keeps the state Pressed until `PRESSED_LINGER_S` after the press, then changes it at the release or the linger's end, which also makes the ramp after a long press start at the release. 46c's `USOLMenuButton` should use this form. With the state form, a caller leaving a long press passes `now - max(releaseS - PRESSED_LINGER_S, pressS)` |
| `float LiftOffset(const FSOLSolidButtonLook& look)` | `-HOVER_LIFT_PX * look.Lift` (UMG units, up is negative Y) |

### 8.3 Tint encoding (no MID per button)

| Function | Contract |
|---|---|
| `FLinearColor EncodeButtonTint(const FSOLSolidButtonLook& look)` | R = `Edge` (also the LED and underline amount, they move together), G = a three-level code (46c amendment): 0 normal, `TINT_DISABLED_CODE` 0.5 disabled, 1 pressed hull (pressed wins), B = `bSelectedMark ? 1 : 0`, A = 1 for enabled, `DISABLED_ALPHA` for `bDisabled` (a continuous dim only). Quantized to 8 bits as the vertex colour is |
| `FSOLSolidButtonLook DecodeButtonTint(const FLinearColor& tint)` | The inverse the material implements: round trip within 1/255 for every field the tint carries (`Edge`, `Led` = `Underline` = `Edge`; 46c amendment: pressed hull when G >= `TINT_PRESSED_G_MIN` 0.75, disabled when `TINT_DISABLED_G_MIN` 0.25 <= G < 0.75, mark when B >= `TINT_MARK_B_MIN` 0.5; A is ignored); `Lift`, `Trace` and `Glow` are not carried and read 0 |

**46c amendment (why G is three-level).** Slate fades multiply the tint's alpha by the inherited opacity, so an alpha
threshold would read a fading panel as disabled. The disabled flag therefore moves to G (the 0.5 code, decoded with
wide bands that survive 8-bit and sRGB rounding), and A stays only as the continuous dim factor.

**46c amendment (vertex packing).** The encoded tint above is the logical code; the brush carries it packed into
[`TINT_VERTEX_FLOOR`, 1] (`SOLSolidStyle::PackVertexTint`, `BrushTint`) because Slate multiplies the material colour by
the tint (section 4.3 amendment); the material unpacks, then applies `DecodeButtonTint`'s thresholds.

**46a review notes for 46c (tint).** Slate multiplies the brush tint by the widget's inherited colour and opacity
(`SButton` paints tint x `GetColorAndOpacityTint()` x background colour), so the encoding holds only for a white brush
at full inherited colour and opacity: fade solid panels with render opacity only, keep every solid brush tint white,
do not use `SetIsEnabled(false)`'s disabled effect on solid buttons, and have the material use A as a continuous dim
factor (not a threshold) so a panel fade never reads as disabled. Slate sends RGB vertex colour as sRGB 8-bit, so the
material must not threshold R on the linear k / 255 grid (the 0 / 1 flags in G and B are unaffected).
`RefreshHighlight` should skip `SetBackgroundColor` and the render-transform call when the 8-bit tint and the lift are
unchanged, so settled buttons cost nothing per tick.

### 8.4 Panels, traces, pause

| Function | Contract |
|---|---|
| `float PanelSlide(float progress)` | `PANEL_SLIDE_PX * (1 - EaseOut(progress))` with the cubic ease-out `EaseOut(p) = 1 - (1 - p)^3` (46a); 0 at progress 1, `PANEL_SLIDE_PX` at 0; progress clamped to [0, 1], NaN 0 |
| `float PauseSlideX(float progress, float panelWidth)` | `-(panelWidth + PAUSE_SLIDE_MARGIN_PX) * (1 - EaseOut(progress))`; 0 when open; never positive |
| `float TracePower(double sinceOpenS, float s)` | 0 before `TRACE_POWER_DELAY_S`, then linear to 1 over `TRACE_POWER_S`; 1 at once at `s = 0`; monotonic |
| `float IdlePulse(double nowS, float s)` | `1 + IDLE_PULSE_DEPTH * min(s, 2) * sin(2 pi nowS / IDLE_PULSE_PERIOD_S)`; exactly 1 at `s = 0`; NaN `nowS` reads 1 |
| `int32 TracePolyline(const FVector2f* points, int32 count, float fraction, FVector2f* out, int32 outCapacity)` | The first `fraction` of the polyline's length (clamped [0, 1]): whole segments, then the partial last one; returns the point count, at least 2 or 0 (0 for fraction 0, fewer than 2 points, no length or `outCapacity < 2`, 46a review); never writes past `outCapacity`; fixed arrays, no heap |
| `float DimAlpha(float progress, float s)` | The pause dim's alpha: `PAUSE_DIM_ALPHA * FillFade(progress)`, at `s = 0` `PAUSE_DIM_ALPHA` at once (the dim is not an effect); the caller hides the dim once the panel is Closed |

### 8.5 Test cases (for the separate test author)

`SOLTest.SolidMenu.Hull.*` (clamping per edge, NaN / negative / zero size, notch and chin clamping, SDF signs on
edges, chamfer diagonals and corner tips), `.Button.*` (ramp up / down timing, strength 0 instant and flat, strength 2
glow, pressed linger, selected mark, disabled zeros, NaN times, range), `.Tint.*` (round trip for every state, 8-bit
quantization), `.Panel.*` (slide endpoints and monotonicity, pause slide never positive), `.Trace.*` (power-up delay and
ramp, strength 0, polyline partial segment, capacity, zero fraction), `.Pulse.*` (strength 0 exact 1, period, depth,
NaN). The test author gets sections 8 and 9 only, not the implementation.

## 9. Constants (`SOL::SolidMenu` and `SOL::Paths` in `SOLConstants.h`; starting values approved in Q16, tuned from screenshots)

| Constant | Value | Notes |
|---|---|---|
| Palette | section 5.1 | `FColor` sRGB |
| `HOVER_LIFT_PX` | 3 | D11 |
| `HOVER_RAMP_S` | 0.12 | Research effect vocabulary |
| `PRESSED_LINGER_S` | 0.10 | FreeSpace 2 `button.cpp` |
| `PANEL_OPEN_S` / `PANEL_SLIDE_PX` | 0.25 / 24 | |
| `PAUSE_SLIDE_S` / `PAUSE_SLIDE_MARGIN_PX` / `PAUSE_DIM_ALPHA` | 0.25 / 16 / 0.55 | Dim today 0.45 |
| `TRACE_POWER_DELAY_S` / `TRACE_POWER_S` | 0.15 / 0.35 | |
| `IDLE_PULSE_PERIOD_S` / `IDLE_PULSE_DEPTH` | 4.0 / 0.15 | Per unit of strength |
| `CLOSE_S` | 0.15 | |
| `HULL_MAX_CHAMFER_FRACTION` | 0.45 | |
| `HULL_MAX_FEATURE_FRACTION` | 0.25 | 46a: title step and chin height (of the height), notch depth (of the width) |
| Hull chamfers (panel) | 28 / 12 / 28 / 12 | TL / TR / BR / BL |
| Button cut / point | 10 / 14 | Top-left cut, right point |
| `TRACE_CORE_WIDTH_PX` / `TRACE_GLOW_WIDTH_PX` / `TRACE_GLOW_ALPHA` | 1.5 / 6 / 0.25 | |
| `LED_RADIUS_PX` / `UNDERLINE_PX` / `EDGE_PX` | 3 / 2 / 1.5 | |
| `DISABLED_ALPHA` | 0.4 | |
| Layout | `COLUMN_WIDTH` 380, `PAGE_WIDTH` 1040, `PAGE_HEIGHT` 760 (**800** since 46d, section 6.2), `TAB_COLUMN_WIDTH` 200 (**220** since 46e, section 6.3), `SETTINGS_GRAPHICS_LABEL_WIDTH` 134 and `SETTINGS_GAMEPLAY_LABEL_WIDTH` 264 (46e follow-ups, section 6.4), `PAUSE_PANEL_WIDTH` 420 (**380** = `COLUMN_WIDTH` since 46i, section 6.7), `LABEL_COLUMN_NARROW_WIDTH` 200 and `VALUE_COLUMN_MIN_WIDTH` 220 (`SOL::MenuStyle`, 46i: the Info label column in narrow rows), `BACK_BUTTON_WIDTH` 160; 46d adds the column and shell layout constants of section 6.2 | Existing `SOL::MenuStyle` sizes stay unless listed |
| Font sizes | Title 56, heading 26, section 20, button 20, body 18, small 15 | Body +2 for Rajdhani's narrower face; checked at 1000x800 |
| Letter spacing | Titles 400, stack labels 260, small caps 200 | From the board's tracking (README), applied as `FSlateFontInfo::LetterSpacing` |
| `SOL::Paths` | `SOLID_MENU_FONT` `/Game/SOL/UI/Solid/Fonts/F_SOLMenu`, `SOLID_HULL_MATERIAL` `/Game/SOL/UI/Solid/M_SOLSolidHull`, `SOLID_TEX_BRUSHED`, `SOLID_TEX_RIVET`, `SOLID_TEX_VENT`, `SOLID_TEX_HAZARD` (46c follow-up: `SOLID_TEX_NOISE` removed) under `/Game/SOL/UI/Solid/Textures/` | The scripts read these names (section 10) |

The old `SOL::MenuStyle` colour constants that the restyle no longer uses are removed in the same change (no dead
constants); the layout sizes stay where still used.

## 10. Assets and pipeline (D3, D8, D9)

| Asset | Made with | Source in `Docs/Art/menu-style/` | Content path | Size / tiling |
|---|---|---|---|---|
| Brushed grain (neutral grey) | PhotoCraft (the round 1 recipe: Gaussian noise, 110 px motion blur, wrap cross-fade) | `textures/brushed-gray-512.png` | `T_SOLSolidBrushed` | 512, seamless both ways, grey, sRGB off, tinted in the material |
| Rivet | VectorCraft (octagon + bevel) then PhotoCraft shading | `textures/rivet-32.png` | `T_SOLSolidRivet` | 32, alpha |
| Vent slot strip | VectorCraft | `textures/vent-strip-128x16.png` | `T_SOLSolidVent` | Tiles along X |
| Hazard stripe | VectorCraft | `textures/hazard-128x16.png` | `T_SOLSolidHazard` | Tiles along X, colours from the palette in the material (stripe mask only) |
| Rajdhani TTFs + OFL | Google Fonts (download) | `../fonts/rajdhani/` | `Fonts/FF_Rajdhani_*`, `Fonts/F_SOLMenu` | 4 weights |
| `M_SOLSolidHull` + instances | Python (`unreal.MaterialEditingLibrary`), HLSL Custom node mirroring section 8.1 | - | `/Game/SOL/UI/Solid/` | UI domain |

- The hull, chamfers, notches, recess bevel, edges, LEDs and underline are **procedural** in the material (D8); only the
  grain, rivet, vents and hazard mask are textures. MaterialCraft may also produce the grain if its export works
  with a write root; the PhotoCraft tile is the fallback (it already exists as the kept sample
  `Docs/Images/menu-style-samples/brushed-tile-gray-512.png`).
- **Tool notes (D9):** VectorCraft exports need **absolute paths**; PhotoCraft must be started with
  **`--automation-write-root <abs Docs/Art path>`** to save or export; earlier rounds had no write roots, so check the
  write before relying on it.
- **`Tools/UI/import_solid_menu_assets.py`** (headless, idempotent, the `import_holo_menu_sounds.py` pattern): imports
  the PNGs (compression `UserInterface2D` or `Masks` per asset, no mips, the texture group UI) and the fonts, stores each
  source's MD5 in a metadata tag and skips unchanged assets (an unchanged run writes no `.uasset`). Asset paths are read
  from `SOLConstants.h` (`SOL::Paths`), as `create_hud_shape_material.py` reads its constants.
- **`Tools/UI/create_solid_menu_materials.py`** (headless, idempotent, `-SOLRebuild` to rebuild in place): builds
  `M_SOLSolidHull` (parameters named from `SOL::SolidMenuMaterialParams`) and the style instances (panel, recess,
  stack button, tab, small button, slider groove, knob, toggle slot, puck), defaults read from `SOLConstants.h`.
- `Docs/Art/menu-style/README.md` gets a section listing the sources, the tool settings and how to regenerate.

### 10.1 46c amendment: `M_SOLSolidHull` parameters and instances

Built by `Tools/UI/create_solid_menu_materials.py` (headless; an MD5 tag of each asset's full spec makes an unchanged
run write no `.uasset`; a changed spec rebuilds in place; `-SOLRebuild` forces it; `-AllowCommandletRendering` gives the
commandlet the D3D12 RHI so the HLSL is really compiled, checked 2026-11-04: no errors, 3 samplers, 10 UV scalars).
Names are `SOL::SolidMenuMaterialParams`, defaults `SOL::SolidMenu`, paths `SOL::Paths` (`SOLID_HULL_MATERIAL`,
`SOLID_MI_*`, `SOLID_STYLE_MATERIALS` in `ESOLSolidStyle` order). Lengths are UMG units.

| Group | Parameters |
|---|---|
| Pushed by the subsystem (on change only) | `EffectsStrength` (sanitized `HudEffectsStrength`), `DpiScale` |
| Shape (`FSOLSolidHull`, clamped in the material as `ClampHull`) | `ChamferTL`, `ChamferTR`, `ChamferBR`, `ChamferBL`, `TitleStepWidth`, `TitleStepHeight`, `NotchY` (< 0 none), `NotchDepth`, `NotchHeight`, `ChinWidth`, `ChinHeight`, `Recess` (1 sunken), `InsetPx` (room for the shadow), `MarkShift` (puck: narrower by this fraction, right when the mark is set) |
| Look | `ShadowPx`, `ShadowOpacity`, `GrainAmount`, `GrainTilePx`, `BevelPx`, `BevelAmount`, `EdgePx`, `EdgeHighlight`, `GlowPx`, `LedX` (< 0 none, large = centred), `LedRadius`, `UnderlineStart`, `UnderlineEnd` (from the right), `UnderlineBottom` (< 0 none), `UnderlinePx`, `DisabledCueStyle` (0 hatch, 1 slot, default 1), `CueBandPx`, `CuePeriodPx` |
| Colours (linear, from the section 5.1 palette) | `BaseColor`, `LiftedColor`, `PressedColor`, `DisabledColor`, `EdgeColor`, `ShadowColor`, `LedOffColor`, `AccentColor` (amber), `MarkColor` (cyan), `CueColor` |
| Textures | `GrainTexture` (`T_SOLSolidBrushed`), `CueMask` (`T_SOLSolidHazard`) |
| Constants (not parameters) | `TINT_VERTEX_FLOOR`, the `TINT_*` decode thresholds, `HULL_MAX_CHAMFER_FRACTION`, `HULL_MAX_FEATURE_FRACTION`, `LIGHT_DIR_X / Y`, `GLOW_GAIN`, `CUE_MASK_PERIODS_X`, `CUE_LIP` |

| Instance | Shape and look (starting values, `SOL::SolidMenu`) |
|---|---|
| `MI_SOLSolidPanel` | Panel chamfers 28 / 12 / 28 / 12, inset 8, drop shadow, grain, `HULL_COLOR`; no LED or underline |
| `MI_SOLSolidRecess` | Recess 8 / 4 / 8 / 4, flat `BASE_COLOR`, no grain or shadow |
| `MI_SOLSolidStackButton`, `MI_SOLSolidTab` | Cut 10 top-left, point 14 / 14 right, LED at 16, underline 28 to 18 from the right, 5 up; `RAISED_COLOR` lifting to `RAISED_LIFTED_COLOR` |
| `MI_SOLSolidSmallButton` | Cut 6 top-left, no point, LED at 10, underline 18 to 8, 4 up |
| `MI_SOLSolidSliderGroove`, `MI_SOLSolidToggleSlot` | Recess, chamfers 2 / 4, `BASE_COLOR` |
| `MI_SOLSolidKnob` | Octagon (chamfers 5), centred LED |
| `MI_SOLSolidPuck` | Chamfers 3, half the slot wide (`PUCK_TRAVEL_FRACTION`), right and cyan-lit when checked (the tint's mark) |

## 11. Persistence

None (D5, D16). No `USaveGame` field, no `UGameUserSettings` value and no config layout changes; `SAVE_VERSION` stays 11
and `KEY_BINDING_LAYOUT_VERSION` 10. The strength is read from the existing live `HudEffectsStrength` value (Q1). The version line (Q10) is a build-time
generated header (section 11.1), not player data and not a config value.

### 11.1 Version line (Q10)

Format `0.1.YYMM.<commit count>`, e.g. `0.1.2611.412` (YYMM = two-digit year and month of the build, zero padded, so November
2026 is `2611`; the last field is the number of commits in the repo, `git rev-list --count HEAD`). Nothing is hardcoded
and `ProjectVersion` is not used. Design (built in 46d):

- `SOLTest.Build.cs` runs, when UBT evaluates the module rules, `git rev-list --count HEAD` in the project directory (via
  `System.Diagnostics.Process`, short timeout) and reads the build date, and writes
  `Intermediate/Generated/SOLBuildVersion.generated.h` (`SOL_BUILD_VERSION_STRING`, `SOL_BUILD_COMMIT_COUNT`,
  `SOL_BUILD_YYMM`). The file is rewritten only when its content changes (no needless recompile), and the folder is added
  to the module's private include paths.
- If git is missing or fails the commit count is 0 (`0.1.2611.0`) and the build logs one warning, so a build never
  fails on it. The generated header is a build artefact (Intermediate), never committed.
- `SOLConstants.h` (or `SOLMenuText.h`) formats the line from the macros; the format helper is a pure function so a test
  covers the zero padding and the field order. The check is a unit test plus the smoke screenshot of the main menu.

### 11.2 46d amendment: the version line as built

- **Generated header.** `SOLTest.Build.cs` (`WriteBuildVersionHeader`) runs `git rev-list --count HEAD` in the project
  directory (`System.Diagnostics.Process`, no shell, 5 s timeout, output read asynchronously) and writes
  `<project>/Intermediate/Generated/SOLBuildVersion.generated.h` with two macros, `SOL_BUILD_YYMM` (an int literal, the
  build's local year and month) and `SOL_BUILD_COMMIT_COUNT`, only when the text differs; the folder is a private
  include path and only `Menu/SOLBuildVersion.cpp` includes the header. Git missing, failing, timing out or printing
  no number gives a count of 0 and one `Logger.LogWarning`; the build never fails on it. `SOL_BUILD_VERSION_STRING` is
  not generated: the format lives in C++ (`SOLBuildVersion::FormatVersion`, `SOLMenuText::MAIN_MENU_VERSION_FORMAT`), so
  the tests cover it. The `.generated.h` name does not clash with UHT (UHT only generates one for a header with
  reflection markup, and `SOLBuildVersion.h` has none); kept, but it **would clash** if `SOLBuildVersion.h` ever gained
  a reflected type (UHT would then write its own `SOLBuildVersion.generated.h`): rename the generated header first.
- **Never fails the build (review fix).** `SOLBuildVersion.cpp` includes the header under
  `#if __has_include("SOLBuildVersion.generated.h")`; without it `BuildYYMM` is the compile date's (`__DATE__`, parsed
  at compile time through `MakeYYMM`, so `SOLTest.BuildVersion.Build.ValuesInRange` still passes) and the count 0. The
  rules create `Intermediate/Generated` before writing (also when the write then fails) and add it as an include path
  only when it exists. The content is computed once per UBT process and project under a static lock (git runs once),
  written to `<header>.<pid>.tmp` and moved over the header (`File.Move(..., overwrite: true)`, .NET 10 in UE 5.8), so a
  concurrent build never reads half a header; git's first stderr line goes into the single warning; the project folder
  is `Target.ProjectFile`'s (the module folder's grandparent without one), and `.git` is looked for there and upward.
- **Rebuild cost.** Every commit, checkout or reset touches the reflog, so it invalidates the makefile (the rules run
  again, git runs once) and, when the count changed, rewrites the header, which recompiles the unity blob holding
  `SOLBuildVersion.cpp` (a few seconds); a build with no commit in between recompiles nothing.
- **`SOLBuildVersion`** (`Menu/SOLBuildVersion.h/.cpp`, pure): `MakeYYMM`, `FormatVersion` (`0.1.` + YYMM clamped to
  [0, 9999] in four digits + `.` + the count, never negative), `BuildYYMM` / `BuildCommitCount` (the macros),
  `CurrentVersion`, `FormatVersionLine(version, engineVersion)` = `v<version>  ·  <STUDIO_NAME>  ·  UE <engine>` (the
  `MAIN_MENU_VERSION_NO_ENGINE_FORMAT` form without the engine part when it is empty). Tests: `SOLTest.BuildVersion.*`.
- **UBT invalidation (checked in the UE 5.8 UBT source).** UBT caches the target makefile and does not run the module
  rules again while it is valid, so a new commit alone would not refresh the count. `ModuleRules.ExternalDependencies`
  ("external files which invalidate the makefile if modified", `Configuration/Rules/ModuleRules.cs`) are added to the
  makefile (`UEBuildTarget.cs`), and `TargetMakefile.IsValidForSourceFiles` drops the makefile when one was deleted or
  is newer than the makefile. The rules add the git HEAD reflog (`.git/logs/HEAD`; in a worktree `.git` is a file whose
  `gitdir:` line names the git directory), which a commit, checkout, reset or pull appends to. Verified: touching the
  reflog made the next build log "Invalidating makefile for SOLTestEditor (HEAD modified)", re-run the rules and leave
  the unchanged header untouched (same timestamp, nothing recompiled); a build with no change between does not re-run
  them. Limits: a new month without a commit keeps the old YYMM until the next commit (or any other makefile
  invalidation); the header is a build artefact under `Intermediate`, never committed.

## 12. Documentation, in-game text and screenshots (same change as the code)

- **`Docs/GAME_MECHANICS.md`:** section 8 rows (main menu, pause menu: the new layout and, per Q3, what pause does);
  section 8.1 the HUD effects slider row (Q1: it now also scales the solid menu animations); a tunables table for
  `SOL::SolidMenu`; the version line.
- **In-game text (`SOLMenuText.h`):** the HUD effects slider text (Q1). Today the label is "HUD hologram effects"
  (`SETTINGS_HUD_EFFECTS`) and the help text near `SOLMenuText.h:469` says it sets "how strong those HUD effects are"
  and that the Hologram menu effects slider, not the HUD one, sets the menus. **Both need widening** (the label, e.g.
  "HUD and menu effects", and the help text) to say the HUD slider also scales the full-screen menu animations, and the
  Hologram menu effects sentence must not claim the HUD slider has no say over menus; `PAUSED_BODY` stays true (Q3, no
  behaviour change); the Back button label; the version line and the About credit line for the font licence (Q10); the hints if the Back button or tabs change what Esc /
  Tab do on a page (they do not, D6).
- **`Docs/ARCHITECTURE.md`:** the new types (section 4.2) in "Key types per module"; the new smoke flag(s); no module
  dependency change (`Menu/` already reads Settings and `UI/`).
- **`Docs/Plans/STATUS.md`:** the #46 row through the parts; Done at the end.
- **README:** 2-3 new screenshots in `Docs/Images/` and the gallery: `46-main-menu-solid.png`,
  `46-settings-solid.png`, `46-pause-solid.png` (window screenshots; the software cursor is not captured).
- **`Docs/Art/menu-style/README.md`:** the asset sources and pipeline (section 10).
- **`Docs/TECH_DEBT.md`:** whatever the review leaves open; the cook-paths item gains `/Game/SOL/UI/Solid`.

## 13. Performance

- **Budget (Q17):** per screen, against today's look, under 0.2 ms extra CPU on the game thread's Slate paint and under
  0.3 ms extra GPU in the Slate UI pass, at 1920x1080 and 1280x720, measured by a profiling smoke with three-run medians.
  **The CPU budget was 0.1 ms until the user moved it to 0.2 ms on 2026-11-06** (after 46g's medians ran over it, section
  6.6): the whole frame with a solid menu open is about 5.9-6.3 ms (160-170 fps) on the i9-9960X / RTX 2080 Ti (measured
  2026-11-06), so 0.2 ms is about 3% of it. The GPU budget stays 0.3 ms and the #42 hologram budget is unchanged.
- **Event-driven:** no per-frame allocation in project code; MIDs are created once (a handful); the shared button MIDs
  never change per frame (state in the vertex colour); strength pushed only on change; `RefreshHighlight` stays a
  state compare and only animates during a 0.12 s ramp; the idle pulse updates a few scalars per tick on the visible
  panels; nothing runs while a menu is hidden (its widget is collapsed, its tick does nothing).
- **Draws:** each hull is one material box; buttons stay one box each (as today); the trace layer is two line draws per
  panel. The cost to watch is pixel cost of the SDF over large panels at 1080p and above (the Settings shell is about
  1040 x 760 units); the material keeps the SDF to a few dozen ALU ops and one or two texture samples.
- **Profiling smoke (`-SOLSmokeSolidMenuProfile`, proposed):** the main menu with Settings open, the pause menu with
  Settings open, each at strength 1, three runs, `stat dumpave` and `ProfileGPU` medians, against a baseline captured
  before 46c at the same screens (the SDD 42 section 11 method, with the median fix from the TECH_DEBT item).
- No claim without a measurement; the numbers go into an SDD amendment.
- **46c amendment (review).** *Allocation:* the one per-frame allocation in project code is the trace layer's line
  points: `FSlateDrawElement::MakeLines` takes them by value and the draw element owns them, so each drawn line (glow,
  core, ring: at most three per trace) gets one exact-size array, moved in without a further copy; the ring's octagon is
  computed once. *ALU:* the compiled `M_SOLSolidHull` pixel shader is about 307 instructions
  (`MaterialEditingLibrary.get_statistics`), not "a few dozen": the per-pixel `ClampHull` mirror, the SDF and the
  layers. Both are TECH_DEBT items, to act on if the 46h panel profiling shows the cost.

- **46h amendment: VR verdict scope (user decision, 2026-11-06).** The RTX 2080 Ti is below Varjo's minimum, so the real XR-4 resolutions cannot pass here (scene alone 22-28 ms GPU at one eye). The profile smoke's view-run verdict judges only the menu's own share: CPU delta vs baseline <= 0.2 ms and menu GPU within the provisional VR ceilings 1.2 ms (one eye) / 2.4 ms (two eyes). The whole-frame check uses the proxy "4K (3840x2160) at 100 Hz or better, frame p99 <= 10.0 ms" and is recorded as project-level VR work, not a menu failure; XR-4 numbers already taken stay informational. Non-Settings pages (Demo, Key bindings, etc.) are judged against the Settings baseline, re-measured with 3 runs, flagged only if still over 0.2 ms CPU.

### 13.1 46c amendment: profiling baseline

**Harness.** `-SOLSmokeSolidMenuProfile` (`SOL::CommandLine::SMOKE_SOLID_MENU_PROFILE`, `FSOLSolidMenuProfileSmoke` in
`Menu/SOLSolidMenuProfileSmoke.h`, run by `USOLMenuSubsystem` once per frame) is a stand-alone flag; it forces the
main menu like `-SOLMenuSmoke` and uses the smoke settings slot (cleared at boot, so `HudEffectsStrength` is the default
1; checked, and logged in every line), never the player's slot. The ProfileGPU / `stat dumpave` capture of SDD 42
section 11 now lives in a shared `FSOLUiProfileCapture` (`UI/SOLUiProfileCapture.h`), used by both
`-SOLSmokeHoloMenuProfile` and this smoke (the holo smoke rechecked after the move: 12/12 checks, all five passes PASS).

**Method (machine-independent).** A standalone `-game` process (the Development editor binary, uncooked, real RHI),
windowed, started at 1920x1080; the smoke sets each view size itself with `r.SetRes <w>x<h>w` and waits until the
viewport has that size for 10 frames. The sim date is pinned to JD 2461348.5 (the other profiling smokes' date) so the
live view behind the menus is the same every run. The mouse cursor is parked in the view's top-right corner at each
pass (no button hovered). Four passes: **P0** the main menu with Settings open at 1920x1080, **P1** the same at
1280x720, then Play and Esc: **P2** the pause menu (in flight) with its Settings tab at 1280x720, **P3** the same at
1920x1080. Each pass settles 1.5 s (longer than every open animation of section 7), takes a window screenshot, then
runs three `ProfileGPU` frames (`r.ProfileGPU.ShowUI 0`; the median of the game window's `SlateUI` pass inclusive time
is `gpu_ui`) and one 240-frame `stat dumpave -ms=0` window each of `Draw Window And Children Time` (the whole Slate
paint on the game thread, `cpu_slate`, the CPU budget's measure) and `Game UI Paint` (`cpu_gameui`, reference only; it
is measured in its own window, so it can read above `cpu_slate`). The pass's screen is checked on every capture frame.
Each pass logs one line: `SolidMenuProfile RESULT P<n> PASS|FAIL|NOBASE screen=.. view=.. cpu_slate=.. cpu_gameui=..
gpu_ui=.. gpu_slate=.. gpu_solid=.. gpu_frame=.. ui_draws=.. ... dcpu=.. dgpu=..` (`gpu_solid` sums the Slate
material draws whose material name contains `SOLSolid`, zero before 46c), then a `SUMMARY` line; then it quits (a
watchdog of 60 s plus 45 s a pass, `PROFILE_WATCHDOG_BASE_S` / `PROFILE_WATCHDOG_PASS_S`, quits a stuck run; a view
run allows 180 s a pass, section 13.2). FAIL also when the capture is invalid (`valid=no`: not `-game`, a view size not
reached, the screen not shown on a capture frame, strength not 1, a number not read). Since 46i a run that logs another
number of checks than a whole run (9 default, 14 a view run) also logs a `z-check-count` FAIL.

**Baseline (today's flat look).** Measured 2026-11-04 at `816da1a` plus the uncommitted 46a review and 46c step 0
files (no rendering change), UE 5.8.3, D3D12 SM6, i9-9960X, RTX 2080 Ti, VSync off, scalability 3, frames 4.5 to
5.4 ms (no cap). Three runs back to back, all 9 checks passed and every capture frame was on its screen in each run.
Medians of the three runs (ms; the three values in brackets):

| Pass | Screen | View | **CPU Slate paint** | **GPU Slate UI pass** | Game UI Paint | UI draws |
|---|---|---|---|---|---|---|
| P0 | Main menu, Settings | 1920x1080 | **0.423** (0.423 / 0.433 / 0.423) | **0.046** (0.046 / 0.048 / 0.039) | 0.360 | 14-15 |
| P1 | Main menu, Settings | 1280x720 | **0.444** (0.449 / 0.444 / 0.412) | **0.027** (0.027 / 0.025 / 0.028) | 0.369 | 14-15 |
| P2 | Pause menu, Settings | 1280x720 | **0.430** (0.407 / 0.430 / 0.440) | **0.037** (0.037 / 0.037 / 0.038) | 0.359 | 15-16 |
| P3 | Pause menu, Settings | 1920x1080 | **0.429** (0.429 / 0.418 / 0.467) | **0.075** (0.063 / 0.075 / 0.075) | 0.341 | 16 |

Spread between runs: CPU up to 0.049 ms (P3), GPU up to 0.012 ms (P3). Today's menus are cheap on the GPU (UMG
boxes and text); the CPU side is the Slate paint of the widget tree. The medians are stored in
`SOLSolidMenuProfileSmoke.cpp` (`PROFILE_BASELINE_CPU_MS` = 0.423, 0.444, 0.430, 0.429; `PROFILE_BASELINE_GPU_MS` =
0.046, 0.027, 0.037, 0.075), so every later line carries `dcpu` / `dgpu` against them and a verdict against the Q17
budget (`dcpu` < 0.1 ms and `dgpu` < 0.3 ms at the time; 0.2 ms CPU since 2026-11-06). A check run against the stored values logged PASS for all four passes
(`dcpu` -0.035 to +0.022, `dgpu` -0.012 to +0.002).

**46c check (restyled buttons and font, panels still flat).** 2026-11-05, three runs after 46c steps 3-5 (the solid
buttons, Rajdhani and the linear-space hull material; the hull panels arrive in 46d), every run 4/4 PASS and 9/9 checks.
Medians of the three runs' deltas against the baseline (ms): P0 `dcpu` +0.015 / `dgpu` +0.005, P1 -0.014 / +0.004,
P2 -0.008 / +0.002, P3 -0.013 / -0.010; `gpu_solid` (the hull draws) 0.000-0.006. All well inside the Q17 budget
(0.1 / 0.3); the CPU deltas are within the run-to-run spread.

**46c review re-check.** 2026-11-05, three runs after the review fixes (written-look compare, one frame read per
screen tick, the trace layer's precomputed ring, the sRGB-scaled dither), 9/9 checks each run. Medians of `dcpu` /
`dgpu` (ms): P0 +0.033 / +0.006, P1 -0.014 / +0.004, P2 -0.017 / +0.002, P3 -0.007 / -0.010; `gpu_solid`
0.002-0.007. Two single-run FAILs (P1 +0.134 in run 2, P0 +0.180 in run 3) did not repeat in the other runs: noise, as
the single-run TECH_DEBT item describes.

**46d check (the main menu's hull column and page shell).** 2026-11-05, three runs after 46d (P0 / P1 now draw the
column, the shell, their recesses and the version recess on the hull material; P2 / P3, the pause menu, are unchanged
until 46g), 9/9 checks each run. Medians of `dcpu` / `dgpu` (ms; the three values in brackets): P0 **+0.044**
(+0.039 / +0.106 / +0.044) / **+0.047** (+0.057 / +0.047 / +0.046), P1 **+0.049** (+0.044 / +0.202 / +0.049) /
**+0.028** (+0.028 / +0.027 / +0.029), P2 -0.020 / +0.001, P3 +0.018 / +0.003. `gpu_solid` (the hull draws) 0.084-0.095
at 1920x1080 and 0.048-0.050 at 1280x720 on the main menu passes (0.002-0.003 on the pause passes); UI draws 28 (base
14-15). All medians inside the Q17 budget; the main menu now uses about half of the CPU budget (more widgets: two
unique-material panels, four recesses, the trace layers) and a sixth of the GPU budget, so the per-pixel `ClampHull`
TECH_DEBT item is not forced by 46d. Run 2's P0 / P1 FAILs (+0.106, +0.202) did not repeat: run-to-run noise (the
single-run TECH_DEBT item). The profile flag has fixed passes (1920x1080 and 1280x720, `PROFILE_VIEW_LARGE` /
`_SMALL`); it does not take its view size from `-ResX` / `-ResY`, so no number at the Varjo XR-4 per-eye size
(3840x3744) was taken.

**46d check after the review fixes (power-up LED blink, idle pulse dot, reshow snap).** 2026-11-05, three runs, 9/9
checks and 4/4 PASS each. Medians of `dcpu` / `dgpu` (ms; the three values in brackets): P0 **+0.042** (+0.042 / +0.042
/ +0.026) / **+0.046** (+0.044 / +0.046 / +0.046), P1 **+0.025** (+0.068 / +0.025 / +0.009) / **+0.030** (+0.029 /
+0.030 / +0.030), P2 -0.025 / +0.002, P3 -0.024 / -0.007. `gpu_solid` 0.081-0.084 at 1920x1080 and 0.051-0.052 at
1280x720; UI draws 28. The first build drew the dots as a 3 px thick line, a draw batch of its own (UI draws 29): its
three runs read P0 +0.136 and P1 +0.174 median `dcpu`, over the budget (an A/B without the dot read 28 draws and
+0.084 / +0.027, +0.128 / +0.021 on two runs, so the noise is large but the extra batch was real); drawn at the core
width as two concentric octagons the dots batch with the core lines and the medians are back at the earlier 46d
numbers. The VR floor is still only extrapolated (TECH_DEBT).

**46e check (Settings with the tab column).** 2026-11-05, three runs after 46e (every pass now draws the Settings
tab rail, the content recess and the solid sliders and toggles; P0 / P1 inside the main menu's shell, P2 / P3 in the
pause menu's still-flat panel), 9/9 checks and 4/4 PASS each. Medians of `dcpu` / `dgpu` (ms; the three values in
brackets): P0 **+0.021** (+0.021 / +0.033 / +0.020) / **+0.058** (+0.060 / +0.058 / +0.047), P1 **-0.011** (-0.014 /
-0.011 / -0.003) / **+0.035** (+0.035 / +0.035 / +0.035), P2 **-0.043** (-0.043 / -0.056 / +0.003) / **+0.010**, P3
**-0.036** (-0.054 / -0.036 / -0.033) / **+0.015** (+0.015 / +0.015 / +0.016). `gpu_solid` 0.088-0.101 at 1920x1080 and
0.054-0.056 at 1280x720 on the main menu passes, 0.010-0.017 on the pause passes; UI draws 31 on the main menu (28 in
46d) and 19 on the pause menu (15-16 before). All inside the Q17 budget. The profile flag still has fixed passes, so no
number at the VR floor sizes was taken (46h, TECH_DEBT).

**46e follow-up check (the slider fill).** 2026-11-06, one session, `-SOLSmokeSolidMenuProfileTab=3` (every pass on
the Gameplay tab: four filled sliders and three toggles), three runs with the fill and three with the fill's draw
disabled in a temporary build (A/B, so the machine's drift between sessions cancels). Medians of `cpu_slate` /
`gpu_ui` (ms) with / without the fill: P0 0.488 / 0.109 against 0.435 / 0.108, P1 0.442 / 0.062 against 0.441 /
0.062, P2 0.428 / 0.048 against 0.405 / 0.049, P3 0.430 / 0.092 against 0.417 / 0.091: the fill costs **+0.001 to
+0.053 ms CPU** (P0's +0.053 is inside the run-to-run spread; the same content at 1280x720, P1, reads +0.001) and
**0.000-0.001 ms GPU**, and one UI draw (28 against 27 on the main menu, 17 against 16 on the pause menu: the fill
boxes of every slider share one batch). Against the pre-46c baseline the Gameplay tab with the fill reads P0
**+0.065** / **+0.063**, P1 **-0.002** / **+0.035**, P2 **-0.002** / **+0.011**, P3 **+0.001** / **+0.017**, inside
the Q17 budget. The default (Graphics) passes after the follow-ups read P0 +0.040 / +0.060, P1 +0.034 / +0.035 in
the same session (no slider on that tab; the drift since the 46e check). The fill is a flat colour box (the plain
Slate shader, not the hull material), so at the VR floor it adds a few thousand pixels of a trivial shader per slider.

**46f check (Controls, Info, About and Demo).** 2026-11-05, three runs at 1920x1080 after 46f, all PASS: see section 6.5 for the medians per pass. The new pages are not profiled (the profile shows Settings screens), and add no per-frame work beyond the rail tick.

**Comparing later (46i).** Close the editor, build, then run three times and take, per pass, the median of `dcpu` and
of `dgpu` over the three `RESULT` lines (a single run's PASS / FAIL is advisory, the run-to-run spread above is up to
half the CPU budget):

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "D:\Development\UnrealProjects\SOLTest\SOLTest.uproject" -game -windowed -ResX=1920 -ResY=1080 -SOLSmokeSolidMenuProfile -nosplash -abslog=<log path>
```

The numbers are machine-specific: on another machine, re-measure the baseline at the 46c parent commit (a worktree)
and replace the constants.

### 13.2 46h amendment: the profile at 1920x1080, the 4K proxy and the VR floor

**View runs.** `-SOLSmokeSolidMenuProfile` with `-ResX` / `-ResY` other than 1920x1080 is a view run: nine passes at
that size (the game window's size limits lifted past the desktop): `main`, `main-settings`, `main-keys` (Settings on
Key bindings), `main-controls`, `main-about`, `main-demo`, then Play and Esc: `pause`, `pause-settings`, `pause-info`.
After the stat dumps each pass samples 240 plain frames for the p99 of the frame (`frame_p99`), the game thread
(`game_p99`) and the GPU (`gpu_p99`). Per the user's decision of 2026-11-06 the verdict judges only the menu's share
(`SOLSolidMenuProfile::IsMenuShareWithinVrFloor`): `dcpu` against the 1920x1080 Settings baseline of its menu (0.423
main, 0.429 pause; the Slate paint does not scale with pixels, and the non-Settings pages have no flat baseline of
their own) under 0.2 ms, and the menu's whole `gpu_ui` under the provisional VR ceiling (1.2 ms one eye, 2.4 ms two
eyes, `VrFloorForView`). The whole frame is logged, informational only: `vr_frame=PASS|MISS` against the XR-4 line
(10.0 ms one eye, 9.5 ms two eyes, all three p99 times) and, at 3840x2160, `proxy=PASS|MISS` against the stand-in
rule "4K at 100 Hz or better, `frame_p99` <= 10.0 ms" (`IsVrProxyView`, `MeetsVrProxy`;
`Docs/Research/VR_PERFORMANCE_TARGETS.md` section 7a). A miss there is project-level VR work, not a menu failure.
The view-run watchdog is 180 s a pass (`PROFILE_WATCHDOG_VIEW_PASS_S`). Tests: `SOLTest.SolidMenuProfile.*` (Budget,
VrFloor, MenuShare, Proxy, Percentile; `Tests/SolidMenuProfileTest.cpp`).

**Default runs (1920x1080 and 1280x720, against the pre-46c baseline).** 2026-11-06, after the 46h build, three runs,
every run 4/4 PASS and 9/9 checks. Medians (ms; the three values in brackets): P0 main-settings 1920x1080 `dcpu`
**+0.063** (+0.059 / +0.063 / +0.068) / `dgpu` **+0.058** (`gpu_ui` 0.104 all three), P1 1280x720 **+0.044** (+0.072 /
+0.017 / +0.044) / **+0.034**, P2 pause-settings 1280x720 **+0.059** (+0.044 / +0.068 / +0.059) / **+0.038**, P3
1920x1080 **+0.057** (+0.075 / +0.057 / +0.041) / **+0.062**. `gpu_solid` 0.097 / 0.055 / 0.039 / 0.071; UI draws 32
on the main menu and 31 on the pause menu; the plain frame 4.1-5.2 ms. All inside the Q17 budget (0.2 ms CPU, 0.3 ms
GPU); against 46g's medians (+0.10 to +0.16 ms CPU, section 6.6) the CPU deltas are lower in this session, inside the
run-to-run spread, with no code change to the menus in 46h.

**4K proxy (3840x2160), one run.** 9/9 PASS (menu share), 14/14 checks. `cpu_slate` / `dcpu` / `gpu_ui` /
`gpu_solid`, then `frame_p99` / `game_p99` / `gpu_p99` (ms): main 0.259 / -0.164 / 0.083 / 0.076, 16.02 / 5.10 /
15.24; main-settings 0.493 / +0.070 / 0.377 / 0.367, 16.04 / 5.34 / 15.65; main-keys 0.580 / +0.157 / 0.385 / 0.372,
16.09 / 5.07 / 15.69; main-controls 0.482 / +0.059 / 0.401 / 0.386, 16.42 / 4.81 / 16.20; main-about 0.372 / -0.051 /
0.384 / 0.376, 16.02 / 4.56 / 15.79; main-demo 0.548 / +0.125 / 0.372 / 0.362, 16.73 / 5.30 / 16.53; pause 0.266 /
-0.163 / 0.223 / 0.059, 17.00 / 4.64 / 16.68; pause-settings 0.487 / +0.058 / 0.517 / 0.346, 17.70 / 5.16 / 17.25;
pause-info 0.471 / +0.042 / 0.539 / 0.360, 17.27 / 6.45 / 16.92. VSync off, scalability 3. **The proxy misses on
every pass (`frame_p99` 16.0-17.7 ms, about 60 fps, against 10.0 ms)**: the frame is GPU-bound (plain GPU 14.0-15.7 ms
mean) by the scene behind the menus; the menus' own GPU is 0.08-0.54 ms of it. Recorded as project-level VR work
(the ToDo `vr-performance-gate.md`), not a menu failure.

**One XR-4 eye (3840x3744), runs 2 and 3 (run 1 was before the reboot, its numbers are in the plan's progress
notes).** Both 9/9 PASS (menu share), 14/14 checks. `dcpu` (run 2 / run 3), `gpu_ui` (run 3), `frame_p99` (run 3):
main -0.152 / -0.151, 0.205, 27.98; main-settings +0.076 / +0.074, 0.666, 32.62; main-keys +0.092 / +0.089, 0.661,
28.56; main-controls +0.065 / +0.061, 0.698, 32.38; main-about -0.014 / -0.025, 0.667, 29.01; main-demo +0.132 /
+0.131, 0.646, 32.20; pause -0.147 / -0.152, 0.466, 29.19; pause-settings +0.067 / +0.066, 0.883, 29.42; pause-info
+0.014 / +0.021, 0.902, 29.67. The menus' GPU is 0.21-0.90 ms, under the 1.2 ms ceiling; `vr_frame` misses on every
pass (p99 frame 27-33 ms, GPU 27-32 ms, game thread 4.4-6.7 ms), informational.

**Two XR-4 eyes (7680x3744): not re-run.** Its passes take 60-110 s each at 50-130 ms frames, and killing the first run
mid-pass hung the GPU driver (a reboot was needed), so it was skipped in 46h. Run 1's P0-P2 stay as recorded
(informational): main `dcpu` -0.102, `gpu_ui` 0.205; main-settings +0.118, 1.103; main-keys +0.377, 1.115; menu GPU
under the 2.4 ms ceiling; frame p99 64-131 ms.

**Non-Settings pages against the Settings baseline (user decision, 2026-11-06).** Three runs at the 46h build (4K,
one eye run 2, one eye run 3): `main-demo` `dcpu` +0.125 / +0.132 / +0.131 (median **+0.131**), `main-keys` +0.157 /
+0.092 / +0.089 (median **+0.092**). Both under 0.2 ms, so neither is flagged. Run 1's Demo +0.236 (one eye) and
the two-eye Key bindings +0.377 (one run, two eyes' larger frame) did not repeat.

**Reading.** The menus stay inside the Q17 budget at 1920x1080 and 1280x720, and inside the VR floor's menu share
at 4K and one XR-4 eye. The hull shader (about 307 instructions per pixel) and the recess overdraw cost 0.6-0.9 ms of
GPU at one eye, under the 1.2 ms ceiling, so neither TECH_DEBT optimisation is forced by 46h (both stay listed). The
whole frame misses both the 4K proxy and the XR-4 lines because of the scene behind the menus on the RTX 2080 Ti.

## 14. Verification

- **Unit tests:** section 8 (`SOLTest.SolidMenu.*`), written from the contracts by a separate opus subagent, red on
  stubs, then green. Re-validate `SOLMenuStateTest` (flow unchanged), `SOLInfoTextTest` (texts), `HoloMenuTest` /
  `HoloMenuKitTest` (shared code unchanged).
- **New smoke `-SOLSmokeSolidMenu`** (flag in `SOLConstants.h`, listed in ARCHITECTURE), 1280x720 and 1000x800, smoke
  settings slot, real injected input, screenshots for every step:
  1. Main menu: the hull panel, the 7 stack buttons, the version recess (format `0.1.YYMM.<n>`); the font asset and hull material loaded (no
     fallback warning); panel-open then power-up then Open.
  2. Hover a stack button with the mouse, then move with Down: one highlight at a time, the lift and the tint encoding
     match `EvaluateButton`; the trace extension drawn.
  3. Each main-menu page in the shell (Start location, Controls, Settings, About, Demo); Back and Esc both close.
  4. Settings: opens on Graphics (no last-tab memory, Q8), each tab shows its section; a slider drag, a toggle, a stepper; Apply / Revert unchanged (the settings
     smoke checks still pass); a rebind switches to Key bindings and scrolls to the row.
  3a. (46f) Controls, Info, About and Demo: tab counts and fit, a tab switch, the first tab on reopen, the Demo layout (section 6.5).
  5. Pause: Esc slides the side panel in from the left; Resume, each page, Quit's two choices; Esc resumes with input
     back in the same frame; the slide-out after.
  6. Strength 0: no animation, hover still lifts and shows the amber edge, LED and underline; strength 2 screenshot.
  7. Pause behaviour (Q3): the live view is dimmed and the simulation keeps running (the existing "sim keeps running while paused" check stays).
  8. (46i) Fit at 1000x800 in both menus and at 1280x720: the pause menu's Settings stepper values, labels and value
     texts in their slots and the shell; every Controls and Info tab with a left column, each row's cells inside
     their slots, the content and the shell (section 6.7); the Gameplay and Key bindings rows' room logged. A run
     that logs another number of checks than a whole run fails, and every menu smoke leaves the player's
     GameUserSettings.ini as it found it.
- **Regression smokes:** `-SOLMenuSmoke=all` (no new failures beyond its known stale checks), `-SOLSmokeHoloMenu`
  (the embedded Overlay list check of step 11 is updated to the solid look), `-SOLSmokeOverlay`.
- **PIE smoke** with unreal-mcp when the editor is open: screenshots looked at for each screen at strength 0, 1, 2.
- **Adversarial review** per part (opus, xhigh, report only), briefed with this SDD, the STYLE_GUIDE, the performance
  guidelines and the tech-debt items of section 3.

## 15. Open questions: all resolved (user answers, 2026-11-04)

| # | Question | Answer |
|---|---|---|
| Q1 | Which existing setting scales the solid animations | **`HudEffectsStrength` ("HUD hologram effects").** Not `HoloMenuEffects`. The slider's label and help text must be widened (section 12) because they say it only affects the HUD and that the Hologram menu effects slider governs the menus; the Hologram menu effects slider keeps governing the #42 menus and the cursor only. No schema change |
| Q2 | Strength 0 and 2 | **Same rule as #42** (SDD 42 D18): 0 = static (no open slide, power-up, pulse or glow halo), hover still fully shown, fully usable; 2 = glow and pulse doubled, same durations |
| Q3 | "Frozen" view under pause | **Dim the live view only**, no behaviour change: the simulation keeps running, `PAUSED_BODY` and the "sim keeps running while paused" check stay true |
| Q4 | Pause: where pages open | **Pages open in the shell to the right of the side panel;** Quit's two choices (to main menu, to desktop) appear inside the side panel; Back / Esc on a page closes the page, Esc with no page resumes |
| Q5 | Main menu page placement | **The shell opens to the right of the button stack;** the stack stays visible; Back and Esc close the page |
| Q6 | Controls / Info / About | **Keep today's entries** (main menu: Controls, About; pause: Controls, Info, About); each page's sections become tabs in the shell |
| Q7 | Demo page | **No tab column:** presets on top, the 6 sections as recessed groups in one scroll, warnings and footer at the bottom |
| Q8 | Settings tabs | **Settings always opens on the first tab** (Graphics), no last-tab memory; one shared footer under every tab; Reset keys at the top of Key bindings |
| Q9 | Selected look | **Selected tab / page = cyan underline + LED; amber for hover and keyboard focus only**. **46c amendment (user, 2026-11-04):** selected is orthogonal to hover / focus / press: a selected button that is hovered or focused shows the full amber hover look plus the cyan underline and LED (`bSelected` flag on the evaluate functions) |
| Q10 | Version / credits | **Version line `0.1.YYMM.<number of commits in the repo>`** (e.g. `0.1.2611.412`), **generated at build time** (section 11.1), no hardcoded value; the `ProjectVersion` alternative is dropped. Plus studio and engine in the chin recess, and the font licence credit ("Rajdhani, SIL OFL 1.1") in About |
| Q11 | Sounds | **Reuse the #42 cues** (`USOLHoloMenuSubsystem::PlayCue`); no new assets |
| Q12 | Text colours | **Body #A4AFB8; titles and the hovered label #DCE3E8;** values and keys in cyan |
| Q13 | Cyan | **All cyan traces #28D2F0** (the same value for every cyan of the solid menus) |
| Q14 | Warning colour | **Red-orange #E5533D** |
| Q15 | Hover trace | **Trace only on the main stack, the pause stack and the tab column;** settings rows get an amber edge and underline only (as do the other small buttons: Rebind, footer, start-location rows, Back) |
| Q16 | Starting values | **Section 9 starting values approved** as the tuning start; final values shown at the hand check |
| Q17 | Performance budget | **Under 0.2 ms extra CPU and 0.3 ms extra GPU per screen** (against today's look, 1920x1080 and 1280x720), three-run medians. **Amended by the user on 2026-11-06:** the CPU budget was 0.1 ms (frame about 5.9-6.3 ms with a solid menu open) |

No question remains open and no part is gated on the user any more.

## 16. Out of scope

The flight HUD and every #42 hologram menu and card (D1); the standalone Ctrl+O Overlay Menu (stays hologram); the NPC
stats overlay (Ctrl+F2); the menu flow, the settings model and save (no schema change, D16); keyboard / gamepad
navigation (D6); a mouse hit test that follows the cut corners (buttons keep their rectangular hit area, D6); the art
and audio pass beyond the menus (#9); the cook-paths tech debt (fixed with the first packaged build).
