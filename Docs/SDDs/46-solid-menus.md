<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 46: Solid console-style menus

Issue #46. Depends on SDD 8 (menus, Settings, the menu flow), SDD 11 Part 10e (Demo page), SDD 22 (hologram cursor,
the Overlay Menu list embedded in Settings) and SDD 42 (the shared animation, hover and cue code, built first for #46:
SDD 42 D12, D17). Plan: [`Docs/Plans/46-solid-menus-plan.md`](../Plans/46-solid-menus-plan.md). Grill notes:
[`Docs/Plans/46-solid-menus-grill-notes.md`](../Plans/46-solid-menus-grill-notes.md). Style research:
[`Docs/Research/SOLID_MENU_STYLE_REFERENCES.md`](../Research/SOLID_MENU_STYLE_REFERENCES.md). Editable style
sources: [`Docs/Art/menu-style/README.md`](../Art/menu-style/README.md).

Status: **drafted 2026-11-04 from the resolved grill-me decisions; all 17 open questions (section 15) answered by the
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
| **Pause menu** | `USOLPauseMenuWidget`, `Menu/SOLPauseMenuWidget.cpp` | Full-screen `SCREEN_DIM` (black, alpha 0.45) root, centred panel `PAUSE_WIDTH` x `PAUSE_HEIGHT` 1080 x 780: "PAUSED" heading, a **horizontal tab bar** of 7 equal buttons (Resume, Controls, Info, Settings, Demo, About, Quit; `TAB_HEIGHT` 46), and a switcher with one page per tab: Resume (`PAUSED_BODY`: the simulation keeps running; `PAUSED_HINT`), Controls, Info, Settings, Demo (in-flight mode), About, Quit (prompt + Quit to main menu / Quit to desktop). Esc resumes. **The simulation keeps running** (SDD 8 decision 3; `-SOLMenuSmoke` check "sim keeps running while paused"; `GAME_MECHANICS.md` section 8). |
| **Settings page** | `USOLSettingsPageWidget`, `Menu/SOLSettingsPageWidget.cpp` (shared by both menus) | `MakePage` "Settings" + hint, then **one scroll box with six sections**: Graphics (Resolution, Window mode, Quality steppers; VSync, Space dust checkboxes), Audio (Master, Effects, Music sliders), Mouse (sensitivity slider, invert Y), Gameplay (HUD scale, HUD hologram effects, Hologram menu effects sliders; Show HUD, Assist at launch, Mouse steering at start checkboxes; Chase Down safety margin slider), Overlays (the embedded `USOLOverlayMenuWidget`, `SetEmbedded(true)`), Key bindings (Reset keys, then 38 rows: label, key text, Rebind button). Footer: dirty text, Revert, Apply; status line under it. Steppers are two small `USOLMenuButton`s around a value text; sliders are default `USlider`s (bar `BUTTON_SELECTED`, handle accent); checkboxes are default `UCheckBox`es. `ScrollToBinding` scrolls to a key row (rebind flow, smoke). |
| **Embedded Overlay list** | `USOLOverlayMenuWidget` in embedded mode, `UI/SOLOverlayMenuWidget.cpp` | No hologram frame (SDD 42 D17); embedded mode strips the root inset and the panel backing; title collapsed; profile tabs and rows as text blocks with their own highlight fills. |
| **Demo page** | `USOLDemoPageWidget`, `Menu/SOLDemoPageWidget.cpp` (shared) | `MakePage` "Scale demo" + hint, a presets row (Showcase, Benchmark, Ramp), a scroll of stepper rows under 6 section headings (`DEMO_SECTION_TITLES`), warnings text, footer Reset + Start demo (main menu) or Stop NPCs + Apply (pause). |
| **Controls page** | `SOLMenuContent::BuildControlsPage` | One scroll of `CONTROL_ROWS` (86 rows) grouped by area (Flight, Targeting, Map, Overlays, Time, HUD, Combat, Routes, Menu, Demo), key column `KEY_COLUMN_WIDTH` 230; key cells refreshed from the live keys when shown. |
| **Info page** (pause only) | `SOLMenuContent::BuildInfoPage` | Flight, Time and travel, Combat (label / value rows, `LABEL_COLUMN_WIDTH` 400, values from the constants), How it works (prose with live key names). |
| **About page** | `SOLMenuContent::BuildAboutPage` | Description, copyright, engine version (`ABOUT_ENGINE_FORMAT`), credits. There is **no game version string** anywhere in the code or `Config/DefaultGame.ini` (Q10 adds a generated one). |
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
| Cyan trace | **#28D2F0** (D10) | **Every** cyan of the solid menus (Q13): circuit traces, ring terminals, lit LEDs at rest, slider fill, the selected underline, values and keys |
| Amber hover / selected | **#FFA628** (D10) | Hover / keyboard-focus only (Q9): edge, LED, underline and trace. The selected state is cyan, not amber |
| Warning | **#E5533D** (Q14) | Red-orange, so a warning never reads as hover: key conflicts, the dirty indicator, Demo warnings (replaces today's orange `WARNING_COLOR`) |

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
| Selected (active tab / page / start body) | Cyan underline + cyan LED, no lift, no amber (Q9) |
| Disabled | Dimmed hull and label, LED off, no hover look (no button is disabled today; kept for completeness) |

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
| **Controls / Info / About** (`SOLMenuContent` builders) | The same page shell as Settings with read-only content (D14): key and value columns in recesses, the key cells in cyan, headings in the heading style. Today's entries are kept (main menu: Controls, About; pause: Controls, Info, About) and each page's sections become tabs (Controls: its 10 areas, Info: Flight, Time and travel, Combat, How it works, About: no tab column) | Q6 |
| **Demo** (`USOLDemoPageWidget`, both menus) | The page shell; no tab column (Q7): presets as small buttons on top, the 6 sections as recessed groups in one scroll, warnings in the warning colour, the footer buttons (Reset, Start demo / Stop NPCs, Apply) bottom right | Q7 |
| **Pause** (`USOLPauseMenuWidget`) | A **side panel on the left** (D15), full height minus margins, sliding in from the left edge; "PAUSED" in its stepped top edge; the 7 tabs become a vertical stack of stack buttons; the live flight view behind is only dimmed, the simulation keeps running (Q3). The pages open in the shell to the right of the side panel, Quit's two choices inside the side panel (Q4) | Q3, Q4 |

### 6.1 The page shell

One kit composition (`USOLSolidPanel` hull + optional tab column + content recess + bottom-right recess with a **Back**
button + the footer row when the page has one), used by every page of both menus. The Back button does exactly what Esc
does on that page today (close the page; on the pause menu Esc with no page open still resumes, Q4); it is a new button but no new input path (D6).
Proposed size: 1040 x 760 (the tab column 200 wide), so a Settings row keeps its 250 label column, a slider and the 80
value column; the main menu's column + gap + shell then spans about 1540 of 1920 units.

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
layers' repaint; with `s = 0` the menus are static (no per-frame trace repaint).

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
| `FSOLSolidButtonLook EvaluateButton(ESOLSolidButtonState state, ESOLSolidButtonState previous, double sinceChangeS, float s)` | Hovered: `Lift`, `Edge`, `Led`, `Underline` and `Trace` rise linearly from the previous state's value to 1 over `HOVER_RAMP_S` (instant at `s = 0`); leaving Hovered falls over the same time. `Glow = s * Edge` with `s` sanitized to [0, 2], so 0 at `s = 0` and **doubled at `s = 2`** (section 7, Q2; 46a amendment: was `GlowScale(s) * Edge`, which caps at 1 and so contradicted section 7). Pressed: `bPressedHull`, `Lift` 0, `Edge` (and `Led`, `Underline`, `Trace`) 1, and a Pressed that ends before `PRESSED_LINGER_S` still reports Pressed until then (the caller passes the press time; the ramp to the new state starts when the linger ends). Selected: `bSelectedMark` (cyan, Q9), no lift. Disabled: everything 0 and `bDisabled` (46a amendment: the flag is what lets `EncodeButtonTint` give a disabled look its `DISABLED_ALPHA`, since its amounts equal a Normal look's). NaN or negative `sinceChangeS` reads 0. Values always in [0, 1], `Glow` in [0, 2] |
| `FSOLSolidButtonLook EvaluateButtonFrom(ESOLSolidButtonState state, const FSOLSolidButtonLook& atChange, double sinceChangeS, float s)` | 46a review addition: the same ramp, but from the look the button showed at the change (its amounts clamped to [0, 1]), so a hover that reverses mid-ramp continues instead of popping (the state form ramps from the previous state's *settled* look). No press linger: the caller keeps the state Pressed until `PRESSED_LINGER_S` after the press, then changes it at the release or the linger's end, which also makes the ramp after a long press start at the release. 46c's `USOLMenuButton` should use this form. With the state form, a caller leaving a long press passes `now - max(releaseS - PRESSED_LINGER_S, pressS)` |
| `float LiftOffset(const FSOLSolidButtonLook& look)` | `-HOVER_LIFT_PX * look.Lift` (UMG units, up is negative Y) |

### 8.3 Tint encoding (no MID per button)

| Function | Contract |
|---|---|
| `FLinearColor EncodeButtonTint(const FSOLSolidButtonLook& look)` | R = `Edge` (also the LED and underline amount, they move together), G = `bPressedHull ? 1 : 0`, B = `bSelectedMark ? 1 : 0`, A = 1 for enabled, `DISABLED_ALPHA` for `bDisabled`. Quantized to 8 bits as the vertex colour is |
| `FSOLSolidButtonLook DecodeButtonTint(const FLinearColor& tint)` | The inverse the material implements: round trip within 1/255 for every field the tint carries (`Edge`, `Led` = `Underline` = `Edge`; G, B on at 0.5 or more; `bDisabled` when A is below the midpoint of 1 and `DISABLED_ALPHA`); `Lift`, `Trace` and `Glow` are not carried and read 0 |

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
| Layout | `COLUMN_WIDTH` 380, `PAGE_WIDTH` 1040, `PAGE_HEIGHT` 760, `TAB_COLUMN_WIDTH` 200, `PAUSE_PANEL_WIDTH` 420, `BACK_BUTTON_WIDTH` 160 | Existing `SOL::MenuStyle` sizes stay unless listed |
| Font sizes | Title 56, heading 26, section 20, button 20, body 18, small 15 | Body +2 for Rajdhani's narrower face; checked at 1000x800 |
| Letter spacing | Titles 400, stack labels 260, small caps 200 | From the board's tracking (README), applied as `FSlateFontInfo::LetterSpacing` |
| `SOL::Paths` | `SOLID_MENU_FONT` `/Game/SOL/UI/Solid/Fonts/F_SOLMenu`, `SOLID_HULL_MATERIAL` `/Game/SOL/UI/Solid/M_SOLSolidHull`, `SOLID_TEX_BRUSHED`, `SOLID_TEX_RIVET`, `SOLID_TEX_VENT`, `SOLID_TEX_HAZARD`, `SOLID_TEX_NOISE` under `/Game/SOL/UI/Solid/Textures/` | The scripts read these names (section 10) |

The old `SOL::MenuStyle` colour constants that the restyle no longer uses are removed in the same change (no dead
constants); the layout sizes stay where still used.

## 10. Assets and pipeline (D3, D8, D9)

| Asset | Made with | Source in `Docs/Art/menu-style/` | Content path | Size / tiling |
|---|---|---|---|---|
| Brushed grain (neutral grey) | PhotoCraft (the round 1 recipe: Gaussian noise, 110 px motion blur, wrap cross-fade) | `textures/brushed-gray-512.png` | `T_SOLSolidBrushed` | 512, seamless both ways, grey, sRGB off, tinted in the material |
| Rivet | VectorCraft (octagon + bevel) then PhotoCraft shading | `textures/rivet-32.png` | `T_SOLSolidRivet` | 32, alpha |
| Vent slot strip | VectorCraft | `textures/vent-strip-128x16.png` | `T_SOLSolidVent` | Tiles along X |
| Hazard stripe | VectorCraft | `textures/hazard-128x16.png` | `T_SOLSolidHazard` | Tiles along X, colours from the palette in the material (stripe mask only) |
| Dither / blue noise | PhotoCraft or MaterialCraft | `textures/noise-64.png` | `T_SOLSolidNoise` | 64, tiling, anti-banding |
| Rajdhani TTFs + OFL | Google Fonts (download) | `../fonts/rajdhani/` | `Fonts/FF_Rajdhani_*`, `Fonts/F_SOLMenu` | 4 weights |
| `M_SOLSolidHull` + instances | Python (`unreal.MaterialEditingLibrary`), HLSL Custom node mirroring section 8.1 | - | `/Game/SOL/UI/Solid/` | UI domain |

- The hull, chamfers, notches, recess bevel, edges, LEDs and underline are **procedural** in the material (D8); only the
  grain, rivet, vents, hazard mask and noise are textures. MaterialCraft may also produce the grain if its export works
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

- **Budget (Q17):** per screen, against today's look, under 0.1 ms extra CPU on the game thread's Slate paint and under
  0.3 ms extra GPU in the Slate UI pass, at 1920x1080 and 1280x720, measured by a profiling smoke with three-run medians.
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
  5. Pause: Esc slides the side panel in from the left; Resume, each page, Quit's two choices; Esc resumes with input
     back in the same frame; the slide-out after.
  6. Strength 0: no animation, hover still lifts and shows the amber edge, LED and underline; strength 2 screenshot.
  7. Pause behaviour (Q3): the live view is dimmed and the simulation keeps running (the existing "sim keeps running while paused" check stays).
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
| Q9 | Selected look | **Selected tab / page = cyan underline + LED; amber for hover and keyboard focus only** |
| Q10 | Version / credits | **Version line `0.1.YYMM.<number of commits in the repo>`** (e.g. `0.1.2611.412`), **generated at build time** (section 11.1), no hardcoded value; the `ProjectVersion` alternative is dropped. Plus studio and engine in the chin recess, and the font licence credit ("Rajdhani, SIL OFL 1.1") in About |
| Q11 | Sounds | **Reuse the #42 cues** (`USOLHoloMenuSubsystem::PlayCue`); no new assets |
| Q12 | Text colours | **Body #A4AFB8; titles and the hovered label #DCE3E8;** values and keys in cyan |
| Q13 | Cyan | **All cyan traces #28D2F0** (the same value for every cyan of the solid menus) |
| Q14 | Warning colour | **Red-orange #E5533D** |
| Q15 | Hover trace | **Trace only on the main stack, the pause stack and the tab column;** settings rows get an amber edge and underline only (as do the other small buttons: Rebind, footer, start-location rows, Back) |
| Q16 | Starting values | **Section 9 starting values approved** as the tuning start; final values shown at the hand check |
| Q17 | Performance budget | **Under 0.1 ms extra CPU and 0.3 ms extra GPU per screen** (against today's look, 1920x1080 and 1280x720), three-run medians |

No question remains open and no part is gated on the user any more.

## 16. Out of scope

The flight HUD and every #42 hologram menu and card (D1); the standalone Ctrl+O Overlay Menu (stays hologram); the NPC
stats overlay (Ctrl+F2); the menu flow, the settings model and save (no schema change, D16); keyboard / gamepad
navigation (D6); a mouse hit test that follows the cut corners (buttons keep their rectangular hit area, D6); the art
and audio pass beyond the menus (#9); the cook-paths tech debt (fixed with the first packaged build).
