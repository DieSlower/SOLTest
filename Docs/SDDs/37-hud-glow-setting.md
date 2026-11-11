<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 37: HUD glow setting (lower default glow, Settings slider 0-200%)

**Issue:** [#37](https://github.com/DieSlower/SOLTest/issues/37), "HUD hologram glow still too strong: lower it and add
a Settings glow slider (0-200%)". Plan: [`Plans/37-hud-glow-setting-plan.md`](../Plans/37-hud-glow-setting-plan.md).
Amends SDD 21 (HUD hologram rendering; Amendment 4 = issue #31's halving) and SDD 20 section 2.4 (the route overlay's
Canvas halo). Persistence change: settings save schema 11 -> 12 (approved, D1).
Status: part 37a built (2026-11-10): schema 12 and the "HUD glow" row, the glow math, the material's `GlowScale`, the
HUD feed, the route halo, and (after the 37a review, D16 and D17) the Settings glow sample and the pointer's glow.
Part 37b built (2026-11-10): the card readouts (section 4.6, engine route proven) and the Help glossary (section 4.7)
follow the HUD glow. Part 37c built (2026-11-11): the default level the user picked (D4: 0.3), the final screenshots,
smokes and profile (sections 7 and 8) and the docs, reviewed (whole ticket, no Critical / High) and committed; #37
is done. Grill-me done 2026-11-10 (D1-D4), open questions answered the same day (D5-D15), review decisions D16-D17,
D4 decided 2026-11-11. No decision is open.

## 1. Problem

Issue #31 (SDD 21 Amendment 4) halved the HUD glow: `HUD_GLOW_CORE_FALLOFF` 2 -> 3, `HUD_GLOW_HALO_WEIGHT` 0.2 -> 0.1
(`HUD_GLOW_CORE_WEIGHT` 0.8 -> 0.9 so the core still peaks at 1), glow-text outline alpha `HUD_TEXT_GLOW_ALPHA`
0.35 -> 0.18, and the route overlay's Canvas halo `GLOW_HALO_INTENSITY` 0.3 -> 0.15. The user still finds it too
strong. The existing "HUD and menu effects" slider (`HudEffectsStrength`, 0-2, formerly "HUD hologram effects") scales
row jitter, panel scanlines and flicker (and, since #46, the solid menus' animations), never the glow, so the player
cannot turn the glow down.

## 2. Decisions (user, 2026-11-10)

Grill-me:

- **D1: Schema bump APPROVED.** The settings save version goes up by one (`SOL::Settings::SAVE_VERSION` 11 -> 12) with
  a migration that keeps every existing value and sets the new field to 100%; never discard data.
  `KEY_BINDING_LAYOUT_VERSION` (10) and every key binding stay untouched.
- **D2: New Settings option "HUD glow",** 0% to 200% in 5% steps (41 positions), default 100%. It scales every glow
  the HUD draws: shapes, glow text, radar, and the map and route overlays' Canvas halo. Live preview; written at the
  usual write point (Apply, then the end of the settings visit), with disk writes minimized per the persistence rules.
- **D3: Scope** is the flight HUD and the map and route overlays only. Hologram menus and the in-flight menu windows
  are **not** affected (refined by D10, D11 and D17: the menu windows and the card frames stay unaffected, the mouse
  pointer and the Settings glow sample follow).
- **D4: The new 100% level is lower than the post-#31 level;** the user picks the amount from screenshots. Before the
  constant is fixed, candidate screenshots at 30%, 50% and 70% of today's glow (1920x1080, HUD smoke tooling, on a
  bright and a dark background) are shown to the user by the main session. **Decided 2026-11-11 (user, from the
  candidate screenshots): 30% of the post-#31 glow** (`SOL::Hologram::HUD_GLOW_DEFAULT_LEVEL = 0.3`; the build used the
  placeholder 0.5 until then). 0% must leave a crisp, legible core with no glow.

Answers to the first draft's open questions (all as recommended):

- **D5 (was Q1): Row position.** Gameplay tab, directly under "HUD and menu effects" and above "Hologram menu
  effects" (HUD scale, HUD and menu effects, HUD glow, Hologram menu effects, then the toggles and Chase Down margin).
- **D6 (was Q2): Wording.** Label "HUD glow", values "0%" ... "200%". In-game Info sentence: "The HUD glow slider in
  Settings sets how much the HUD's lines, text, radar and route lines glow (100% is the default, 0% leaves crisp lines
  with no glow); the menus keep their own look." Settings hint: "Audio, mouse, HUD, glow and hologram effect changes
  preview at once; ..." (rest unchanged).
- **D7 (was Q3): Independent of "HUD and menu effects".** That slider keeps driving only jitter, scanlines, flicker and
  the solid-menu animations; its label and text stay; the glow is only the new slider.
- **D8 (was Q4): Linear in glow energy** (section 4.2): halo weight x g, core skirt width x g (falloff 3 / g capped at
  `HUD_GLOW_CORE_FALLOFF_MAX`, proposed 6, tuned on the 0% screenshot), the core pixel held at 1, text outline alpha
  x g, route halo x g.
- **D9 (was Q5): Glow text at 0%** drops only the outline; the whitened fill and the drop shadow stay.
- **D10 (was Q6): The hologram cards' readouts follow HUD glow, the card frames do not.** The content of the
  date-and-anchor, speed-and-G, target and info cards (text and lines) uses the HUD glow; the corner marks, tint and
  scanline stay with Hologram menu effects. Built with a per-shape glow factor in the unused vertex slot and a
  per-item text glow alpha (section 4.6). **Instruction: the implementer checks the engine's Slate shaders first; if
  the vertex slot is not viable, stop and report instead of choosing another route.**
- **D11 (was Q7): The Help glossary's HUD symbols follow HUD glow** (they are pictures of the HUD); the player guide
  images are regenerated. The two map route pictures are fixed crop textures captured at the default, so they do not
  follow (the Info text says so, 37c review).
- **D12 (was Q8): Not scaled:** the map grid, stems and foot rings (core-only lines, no halo) and every panel fill.
- **D13 (was Q9): The 32 px glow pad is kept** at every level (HUD geometry and cost unchanged).
- **D14 (was Q10): Screenshot tooling** is new `-SOLSmokeHud` glow phases that set the live value through the settings
  subsystem on the real settings save slot (corrected 2026-11-10, user, after the 37a review: there is no smoke slot;
  the value stays pending, never applied or written, and Revert discards it, so the file is untouched); no new
  command-line flag or console variable.
- **D15 (was Q11): Profiling sizes:** 1920x1080, the 4K proxy (3840x2160) and one XR-4 eye (3840x3744); two eyes
  skipped (as in #33 part 33f).

Decisions after the 37a review (user, 2026-11-10):

- **D16: A live glow sample on the Settings page.** A small HUD-style picture (a dark HUD panel backing, a ring, a line
  and one glow-text line) sits under the HUD glow row on the Gameplay tab and is drawn through the same glow path as
  the HUD (`M_SOLHudShape` with `GlowScale`, the glow text's outline alpha), so it follows the **pending** slider value
  at once, before Apply. It must fit at 1280x720, 1920x1080 and 3840x2160 (and in the narrow 1000x800 layout). The
  Settings hint stays as in D6 (it already says glow changes preview at once). Design in section 4.10.
- **D17: The mouse pointer's glow follows HUD glow.** The hologram cursor (its own `M_SOLHudShape` instance) gets the
  pending glow scale too, pushed on change. This narrows D3: the menu windows and the card frames are unaffected, the
  pointer and the D16 sample follow. The Info sentence of D6 becomes: "The HUD glow slider in Settings sets how much
  the HUD's lines, text, radar and route lines glow, and the mouse pointer with them (100% is the default, 0% leaves
  crisp lines with no glow; the sample under the slider shows it at once); the menus keep their own look."

## 3. What draws HUD glow today (code survey)

| Glow | Where | Knob(s) |
|---|---|---|
| Every glow shape of the flight HUD layer: lines, rings, arcs, ellipses, dashed lines, so the radar, overlay icons, map body icons, markers, joystick circles, etc. | `M_SOLHudShape` Custom node (`Tools/UI/create_hud_shape_material.py`, mirrored by `SOLHudShapeMath::GlowProfile`). One MID owned by `ASOLFlightHud` (`ShapeMaterial`, created in `CreateShapeLayer`, parameters pushed in `BeginShapeFrame` only when they change) | `CoreW` 0.9, `CoreF` 3, `HaloW` 0.1 (constant pins read from `SOLConstants.h`) |
| Glow text (HUD layer) | `SSOLHudShapeLayer::PaintTexts` (`UI/SOLHudShapeLayer.cpp`): fill = colour moved 0.3 toward white, 1-unit outline in the colour at `HUD_TEXT_GLOW_ALPHA` | `HUD_TEXT_GLOW_ALPHA` 0.18 |
| Route overlay halo (flight view and map) | `FSOLMapRouteOverlay::AppendGlowPiece` (`Map/SOLMapRouteOverlay.cpp`): a core quad plus two halo quads fading to 0 over `GLOW_HALO_HALF_PX` 4.5 px; drawn by `ASOLFlightHud` (`mRouteOverlay.Draw`) | `GLOW_HALO_INTENSITY` 0.15 |

Findings that shape the design:

1. **`M_SOLHudShape` has five MIDs, four of them menus.** Besides the flight HUD's `ShapeMaterial`:
   `USOLHoloMenuSubsystem::MenuMaterial` (the HUD's menu layer: radial menu, list menus, the four hologram cards and
   their content), `USOLHologramCursorSubsystem` (the cursor), and `USOLSolidMenuSubsystem::HudShapeMaterial`, shared
   by the Help glossary's `USOLHudSymbolView`s. So the glow cannot be lowered by changing the material's constant
   pins: that would lower the menus too (against D3).
2. **Panels are already exempt.** The panel kind sets the glow term to 0 and draws only its fill, so a glow scale in
   the glow term never touches panels or backings (D12).
3. **The map hologram overlay has no halo.** `FSOLMapHologramOverlay` (grid, stems, foot rings) draws core-only lines
   (`GLOW_CORE_HALF_PX`); the only Canvas halo is the route overlay's. The map's body icons go through the HUD shape
   batch (`SOLMapBodyOverlay` writes into `FSOLHudShapeBatch`), so they follow the material scale.
4. **The hologram cards' content lives on the menu layer.** `ASOLFlightHud::BeginCardContent` reroutes the HUD's draw
   helpers to the menu layer's batch and text list for the date-and-anchor, speed-and-G, target and info cards (with
   the line pad from `SOLHoloFrameBuild::LinePad(mCardStrength)`), until `EndCardContent`. A material-level scale
   alone would leave those readouts at today's glow; D10 needs a per-shape factor (section 4.6).
5. **Canvas fallback** (`sol.HudRenderer 0`) draws plain Canvas lines with no glow: nothing to scale.
6. If the hologram menu material is missing, `GetMenuBatch` falls back to the HUD batch and menu shapes would follow
   the HUD glow. Edge case only (an asset failed to load); accepted and documented, not handled.

## 4. Design

### 4.1 Glow scale `g` (reference units)

All runtime glow is expressed as one scalar **`g` in reference units: `g = 1` is exactly today's (post-#31) glow**,
`g = 0` no glow. The Settings value `s` (0-2, default 1) maps to

`g = SanitizedSetting(s) x HUD_GLOW_DEFAULT_LEVEL`

so 100% = `HUD_GLOW_DEFAULT_LEVEL` of today's glow (0.3, D4), 200% = twice that (0.6), 0% = none. The material
default of `g` is 1, so every MID and vertex that never sets it (the menus) keeps today's look exactly (D3), and
`g = 1` reproduces every post-#31 number bit for bit (the existing `SOLTest.HudShapes.GlowProfile` stays valid).

### 4.2 What `g` does to each knob (D8, D9)

Linear in glow energy, with the core pixel held at 1:

| Knob | Formula | At `g = 1` | At `g = 0` |
|---|---|---|---|
| Halo weight | `HUD_GLOW_HALO_WEIGHT x g` | 0.1 | 0 |
| Core weight | `1 - haloWeight(g)` (the core still peaks at 1) | 0.9 | 1 |
| Core falloff | `HUD_GLOW_CORE_FALLOFF / max(g, HUD_GLOW_CORE_FALLOFF / HUD_GLOW_CORE_FALLOFF_MAX)` (skirt width ~ `g`, capped) | 3 | `HUD_GLOW_CORE_FALLOFF_MAX` (proposed 6, a ~1 px antialiased edge; tuned on the 0% screenshot) |
| Glow-text outline alpha | `clamp(HUD_TEXT_GLOW_ALPHA x g, 0, 1)`; at 0 the outline is not drawn (OutlineSize 0) | 0.18 | none; the whitened fill and the drop shadow stay (D9) |
| Route overlay halo | `GLOW_HALO_INTENSITY x g`; at 0 the four halo triangles of a glow piece are skipped | 0.15 | none (core quad only) |

Profile: `G(d; g) = coreW(g) exp(-coreF(g) max(0, d - 0.5)) + haloW(g) exp(-(0.2 d)^0.75)`, `d >= 0`.
Unchanged: `HUD_GLOW_CORE_HALF_PX`, the halo scale and power, `HUD_GLOW_PAD_PX` and the pad fade (D13), the colour
gain, tone curve and blue grade, panel fill and flicker, backings (D12), the text whitening and shadow (D9).

### 4.3 Material (`M_SOLHudShape`, `Tools/UI/create_hud_shape_material.py`)

- New scalar parameter **`GlowScale`**, default **1.0** (a literal, not `HUD_GLOW_DEFAULT_LEVEL`: menus must keep
  today's glow), name in `SOL::HudShapeMaterialParams::GLOW_SCALE`.
- New Custom-node inputs `GlowScale` and `GlowOverride` (the per-shape factor of section 4.6, a float2) and constant
  pin `CoreFMax` (`HUD_GLOW_CORE_FALLOFF_MAX`).
- The glow line becomes (HLSL, mirrors section 4.2):
  `g = max(GlowOverride.y > 0.5 ? GlowOverride.x : GlowScale, 0); haloW = HaloW * g;`
  `coreF = CoreF / max(g, CoreF / CoreFMax);`
  `shape = isPanel ? 0 : (1 - haloW) * exp(-coreF * max(0, dd - CoreHalf)) + haloW * exp(-pow(HaloS * dd, HaloP));`
  The `CoreW` pin becomes unused (kept or removed by the implementer; the test asserts
  `HUD_GLOW_CORE_WEIGHT + HUD_GLOW_HALO_WEIGHT == 1` so the two forms agree at `g = 1`).
- Rebuilt with the editor closed: `UnrealEditor-Cmd.exe SOLTest.uproject -run=pythonscript
  -script="<abs>/Tools/UI/create_hud_shape_material.py" -unattended -SOLRebuild`; the script header and the
  `SOLConstants.h` comment block above `HUD_GLOW_CORE_HALF_PX` are updated.

### 4.4 Feeding the HUD (`UI/SOLFlightHud.cpp`, `UI/SOLHudShapeLayer.*`, `Map/SOLMapRouteOverlay.*`)

- `ASOLFlightHud::BeginShapeFrame` reads `Settings->GetLiveValues().HudGlow` (live, so the Settings page previews at
  once, like `HudEffectsStrength`), computes `g = SOLHudShapeMath::HudGlowScale(value)` once per frame (stored in
  `mHudGlowScale` for the card content of section 4.6) and, only when `g` differs from the last pushed value
  (`mShapeGlowScale`, reset to -1 in `CreateShapeLayer`), calls `ShapeMaterial->SetScalarParameterValue(GLOW_SCALE, g)`
  and `mShapeLayer->SetTextGlowAlpha(TextGlowAlpha(g))`. No settings subsystem: `HUD_GLOW_DEFAULT` (100%).
- `SSOLHudShapeLayer` gets `SetTextGlowAlpha(float)` (member default `HUD_TEXT_GLOW_ALPHA`, so a layer that never sets
  it, such as the menu layer, is unchanged). `PaintTexts` uses the item's own glow alpha when it has one (section 4.6),
  else the layer's, for the outline colour's alpha, and skips the outline (`OutlineSize = 0`) when it is 0.
- `FSOLRouteOverlayInputs` gets `float GlowScale = 1.0f` (reference default); the HUD sets it to `g`;
  `AppendGlowPiece` multiplies the halo by `SOLRouteOverlayMath::RouteHaloIntensity(GlowScale)` and appends only the
  core quad when that is 0; it reserves `SOLRouteOverlayMath::GlowPieceTriangles(GlowScale)` triangles
  (`SOL::Route::OVERLAY_GLOW_PIECE_TRIANGLES` 6, or `OVERLAY_GLOW_CORE_TRIANGLES` 2 with no halo). The overlay keeps
  the frame's glow scale and halo intensity for verification (`GetLastGlowScale`, `GetLastHaloIntensity`).
- A verification accessor (`GetShapeGlowScale()`) for the smoke.

### 4.5 Scope boundaries (D3, D10, D11, D12)

Follow HUD glow: every HUD-layer shape and text, the route overlay halo, the card readouts (D10), the Help glossary's
HUD symbols and text samples (D11), the hologram mouse pointer and the Settings glow sample (D16, D17). Not affected:
the menu layer's own shapes (radial menu, list menus, card frames), the solid menus' own look, the Ctrl+O Overlay
Menu and F3 speed panel (UMG with their own look),
the map's 3D hologram proxies and post-process, the map grid, stems and foot rings and all panel fills (D12), the
Canvas fallback.

### 4.6 Card readouts: per-shape glow factor and per-item text alpha (D10)

**Engine check (done while drafting, UE 5.8 source; the implementer re-verifies before building):**
`FSOLHudShapeBatch` fills `FSlateVertex::TexCoords[0..3]` and zeroes `MaterialTexCoords` (`UI/SOLHudShapeBatch.cpp`
~147-155). In `Engine/Shaders/Private/SlateVertexShader.usf` (~54-64) the material's **vertex-stage** UV channels are
`0, 1 = TexCoords.xy / .zw`, `2, 3 = InstanceParam (PixelSize)`, **`4 = InMaterialTextureCoordinates`**. In
`SlateElementPixelShader.usf` `FillTexCoords` (~129-160), a custom-vertex element (`ST_Custom`) gets its pixel-stage
`TexCoords[i]` from the interpolated Slate UVs `TextureCoordinates0..3` (`GetSlateUVs`), and `MaterialTexCoords` is not
handed to the pixel stage for custom vertices. So `MaterialTexCoords` is **reachable only through a Customized UV**:
set the material's `NumCustomizedUVs` to 3, route vertex `TexCoord[4]` into Customized UV 2 (and keep UV 0 and 1 as
`TexCoord[0]` / `TexCoord[1]`), then read `TexCoord[2]` in the pixel stage as `GlowOverride`. This looks viable; the
implementer confirms it with the material compile and a smoke screenshot (a card readout at 0% vs its frame). **If it
does not work, stop and report (D10); do not pick another route.**

- **Encoding:** `MaterialTexCoords = (g, 1)` for a shape with its own glow factor; `(0, 0)` (today's default for every
  vertex) means "use the MID's `GlowScale`". Menus, the cursor and the glossary never set it, so they are unchanged.
- **Batch:** `FSOLHudShapeBatch::SetGlowOverride(float g)` / `ClearGlowOverride()`; every `Add*` writes the current
  state into its four vertices; `Reset` clears it. `g` is sanitized (`SanitizeGlowScale`). No allocation.
- **Text:** `FSOLHudTextItem::GlowAlpha = -1` (use the layer's); `FSOLHudTextList::SetGlowAlphaOverride(float)` /
  `ClearGlowAlphaOverride()`; `Add` copies the current value; `Reset` clears it. Capacity reserved as today.
- **HUD:** `BeginCardContent` sets the menu batch's glow override to `mHudGlowScale` and the menu text list's glow
  alpha override to `TextGlowAlpha(mHudGlowScale)`; `EndCardContent` clears both. The card frame (built by
  `SOLHoloFrameBuild` outside that window) carries no override and keeps the menu MID's `GlowScale` 1. The content's
  glow pad stays `LinePad(mCardStrength)` (D13).

**As built (37b, 2026-11-10):**

- **Engine proof.** UE 5.8 `SlateVertexShader.usf` (vertex UV channel 4 = `InMaterialTextureCoordinates`, copied into
  `VertexParameters.TexCoords` up to `NUM_MATERIAL_TEXCOORDS_VERTEX`, then `GetMaterialCustomizedUVs` into the Slate
  interpolants) and `SlateElementPixelShader.usf` `FillTexCoords` (`ST_Custom`: `TexCoords[i]` = the interpolated
  Slate UV i) re-read; `UMaterial::IsPropertyActive` allows Customized UVs in the UI domain. `M_SOLHudShape` now has
  `NumCustomizedUVs` 3, Customized UV 2 = vertex `TexCoord[4]` and the Custom node's `GlowOverride` = pixel
  `TexCoord[2]`; it compiled clean. Screenshot proof (`-SOLSmokeHud` at 1280x720, the speed card at 100% and 200%):
  the G bar readout's halo doubles (column profile 30-55 vs 53-112 beside a 231 core) while the card's corner tick
  profile is identical (218 core, 69-122 skirt in both).
- **Tooling.** The `EMaterialProperty` Customized UV entries are `UMETA(Hidden)`, so Python cannot name them;
  `USOLEditorAuthoring::ConnectMaterialCustomizedUV` (editor-only, the existing authoring helper library) makes the
  connection for `Tools/UI/create_hud_shape_material.py`.
- **Target and info cards.** Their text is drawn with `MenuText` outside a `BeginCardContent` window, so the override
  pair is factored into `ASOLFlightHud::BeginReadoutGlow(card)` / `EndReadoutGlow()`: `BeginCardContent` /
  `EndCardContent` call them, and `DrawTargetCard` / `DrawInfoCard` call them around their text (ended before
  `DrawCard`). `BeginMenuFrame` clears any override left set on the menu layer (a missed end on a frame that drew
  nothing, when `ResetFrame` is skipped). The cards' stats gain `FrameShapeStart`, `ReadoutShapes` and
  `ReadoutTexts`, and `GetMenuLayerBatch` / `GetMenuLayerTexts` let `-SOLSmokeHud` read the vertices: new checks
  `l-card-glow-live` and `l-card-glow-revert` (every overridden shape and line holds the frame's g and text alpha,
  their counts are the readouts', the card frames' 30 shapes carry none).

### 4.7 Help glossary (D11; `Menu/SOLSolidMenuSubsystem.*`, `Menu/SOLHudSymbolView.cpp`)

- `FSOLSolidMenuParamCache` also caches the sanitized glow scale (`HudGlowScale` of the live `HudGlow`); `Update`
  returns true when the strength, the DPI scale or the glow changed; `WriteHudShapeParams` also writes `GLOW_SCALE` into
  `HudShapeMaterial` (the material all glossary views share). Pushed only on change, like the strength.
- Each `USOLHudSymbolView`'s text-sample layer gets `SetTextGlowAlpha(TextGlowAlpha(g))` when the glow changes (the
  sample font's outline size used for measuring stays, so the layout does not move). No rebuild of the symbol
  geometry is needed for a glow change.
- `Docs/PLAYER_GUIDE.md` and its glossary images are regenerated with `Tools\WritePlayerGuide.bat` at the chosen
  default level.
- **As built (37b):** `FSOLSolidMenuFrame` carries the subsystem's `GlowScale` (`GetGlowScale()`); the Help page's
  `UpdateShownSymbolViews` hands it to the shown tab's views (`USOLHudSymbolView::SetGlowScale`, a layer text alpha
  set that repaints only on a change, never a rebuild). `PushParams` takes the glow setting as a third argument
  (default `HUD_GLOW_DEFAULT`). `-SOLSmokeSolidMenu` checks a pending 0% on the Flight HUD tab (`w-help-glow-0`:
  the subsystem's scale, the material's `GlowScale` and every view's text alpha at 0, no view rebuilt) and its revert
  (`w-help-glow-revert`); a whole run logs 296 checks. The player guide was regenerated at the placeholder level in 37b and again at the chosen level 0.3 in 37c.

### 4.8 Settings (persistence; D1, D2, D5)

- **Constants** (`SOL::Settings`): `HUD_GLOW_MIN` 0, `HUD_GLOW_MAX` 2, `HUD_GLOW_DEFAULT` 1, `HUD_GLOW_STEP` 0.05;
  `SAVE_VERSION` 11 -> **12** (its comment gains "12: the HUD glow, SDD 37"). `KEY_BINDING_LAYOUT_VERSION` stays 10.
  (`SOL::Hologram`): `HUD_GLOW_DEFAULT_LEVEL` 0.3 (D4, the user's pick), `HUD_GLOW_CORE_FALLOFF_MAX` 6 (D8, kept after
  the 0% screenshots),
  `HUD_GLOW_SCALE_MAX` 2 (sanitizer bound in reference units); `SOL::HudShapeMaterialParams::GLOW_SCALE`.
- **Model** (`Menu/SOLSettingsModel.*`): `FSettingsValues::HudGlow = HUD_GLOW_DEFAULT`; `Sanitized` clamps it to
  `[MIN, MAX]`, NaN -> default, +-inf -> the ends (`ClampOrDefault`, like `HudEffectsStrength`); `AreEqual` compares
  it within `VALUE_EQUAL_TOLERANCE`. Dirty / Revert / Commit need no new code.
- **Save** (`Menu/SOLSettingsSave.*`): `UPROPERTY() float HudGlow = SOL::Settings::HUD_GLOW_DEFAULT;` copied by
  `SetValues` / `GetValues`. **Migration step 11 -> 12:** `HudGlow = HUD_GLOW_DEFAULT; Version = 12;`, every other
  field, the key overrides and the overlay profiles kept as loaded; frozen like the other steps (inline constant, no
  call into the model). `DefaultLayoutChangedSince(11)` stays false (no controls-changed notice).
- **Write points (unchanged):** the slider changes the pending value (`SetPendingValues`, preview at once); Apply
  commits it (`CommitIfDirty`, a write is owed only if a value changed); the one async write happens at
  `EndVisit` / `FlushSave`; Revert or leaving the page discards it, and the HUD re-pushes the saved value on the next
  frame. A migrated v11 file is written back once at the next write point (existing `mIsSaveOwed`). An older build
  that meets a v12 file ignores it (`ClassifySaveVersion`, existing behaviour).
- **Page** (`Menu/SOLSettingsPageWidget.*`, `Menu/SOLSettingsTabs.cpp`, `Menu/SOLMenuText.h`): new
  `ESOLSettingsSlider::HudGlow` after `HudEffects` (D5), a `SETTINGS_SLIDERS` entry `{ SETTINGS_HUD_GLOW, HUD_GLOW_MIN,
  HUD_GLOW_MAX, HUD_GLOW_STEP, true }` (percent display, 0%-200%), get / apply cases, `HandleHudGlowChanged`, the
  `addSlider` call between the HUD and menu effects and Hologram menu effects rows, the `SETTINGS_SLIDER_TABS` entry
  (Gameplay). Text per D6.

### 4.9 What does not change

The HUD and menu effects slider and everything it drives (D7), the Hologram menu effects slider, the HUD scale, the
glow pad (D13), the HUD's draw counts and capacities, key bindings and the key layout version, the overlay profiles.

### 4.10 Settings glow sample and pointer (D16, D17)

- **Feed** (`UI/SOLHudShapeMath.h`, pure): `FSOLGlowScaleFeed` remembers the glow scale last pushed to one material or
  layer. `Update(hudGlowSetting)` maps the setting through `HudGlowScale` and returns true when the result differs
  from the last one (always on the first call and after `Reset`), so the caller pushes only on change; `GetScale()`
  (-1 before the first update), `GetTextAlpha()` (`TextGlowAlpha(GetScale())`, the default alpha before the first
  update). The cursor and the sample use it; the flight HUD keeps its own per-frame `mHudGlowScale` (section 4.4).
- **Pointer** (`UI/SOLHologramCursorSubsystem.*`): `SSOLHologramCursor::OnPaint` already reads the live settings for
  the effects strength; it also calls `mGlowFeed.Update(live HudGlow)` and, on true, sets `GLOW_SCALE` on its MID.
  `GetPushedGlowScale()` for the smoke. No settings subsystem: `HUD_GLOW_DEFAULT`.
- **Sample** (`Menu/SOLHudGlowSample.*`, a `UWidget`): a retained `SSOLHudShapeLayer` (capacities
  `SETTINGS_GLOW_SAMPLE_MAX_SHAPES` shapes, one text line, one backing; its own paint buffer) with its **own**
  `M_SOLHudShape` instance (`UPROPERTY` `TObjectPtr`, created once in `RebuildWidget`; `Composite` 1 like the glossary,
  `EffectsStrength` = the sanitized live HUD effects strength, the view size in pixels, `GlowScale` from its feed). It
  is not the solid menu subsystem's shared glossary material, so the glossary does not follow in 37a (it does in 37b).
  Content, in the view's pixels (pixel size = laid-out size x layout scale): one HUD panel backing over the view, a
  ring and a line (`HUD_LINE_HALF_WIDTH_PX`, `HUD_GLOW_PAD_PX`, HUD primary colour) and the glow text
  `SOLMenuText::SETTINGS_GLOW_SAMPLE` in the HUD's Medium font at the glossary's HUD scale
  (`GLOSSARY_SYMBOL_UI_SCALE` x layout scale). The batch is rebuilt only when the pixel size changes; a glow or
  strength change only pushes material parameters and the layer's text alpha. The view clips to its bounds and takes
  no hit test. `Update(glowSetting, effectsStrength, layoutScale)` is called from the Settings page's tick (the page
  passes the live values); accessors for the smoke: pushed glow scale, the layer's text alpha, painted shapes / texts
  / backings, build count, pixel size.
- **Placement** (`Menu/SOLSettingsPageWidget.cpp`): a row right after the HUD glow slider row in the Gameplay scroll
  box: an indent as wide as the tab's label column plus `BUTTON_GAP` (kept in step with the narrow layout in
  `FitNarrowRows`), then the sample in a `SETTINGS_GLOW_SAMPLE_WIDTH` x `SETTINGS_GLOW_SAMPLE_HEIGHT` box, the row gap
  below. It is not a labelled row (`RowLabels`, the row boxes and the slider and toggle counts are unchanged).

## 5. Contract for the test author

Pure functions and plain data, no engine rendering. Tolerance 1e-5 unless stated. Names are proposals the implementer
keeps unless a reason is reported back.

**Settings constants and model** (`SOLSettingsModelTest.cpp`):

1. `HUD_GLOW_MIN == 0`, `HUD_GLOW_MAX == 2`, `HUD_GLOW_DEFAULT == 1`, `HUD_GLOW_STEP == 0.05`;
   `(MAX - MIN) / STEP + 1 == 41` positions.
2. `FSettingsValues().HudGlow == 1`; `Sanitized` keeps 0, 0.05, 0.35, 1.55, 2; clamps -0.5 -> 0, 2.01 and 7 -> 2;
   NaN -> 1; +inf -> 2; -inf -> 0; the HUD glow clamps on its own (a 9.0 glow beside a 0.3 HUD effects strength keeps
   the strength 0.3).
3. Dirty: a model whose pending `HudGlow` differs by more than `VALUE_EQUAL_TOLERANCE` is dirty, within half the
   tolerance it is not; two out-of-range values that sanitize to the same end are equal; Revert returns the saved
   glow; Commit stores it and returns true once, false when nothing differs; `SetPending` sanitizes (12 -> 2).
4. `SOLSettingsRules::SnapToStep(0.52, 0.05, 0, 2) == 0.5`, `(1.976, ...) == 2`, `(-1, ...) == 0`;
   `FormatPercent(0) == "0%"`, `(1) == "100%"`, `(2) == "200%"`, `(0.05) == "5%"`.

**Save and migration** (`SOLSettingsRulesTest.cpp`; also re-validate `FlightModeMathTest.cpp`):

5. `SAVE_VERSION == 12`; `KEY_BINDING_LAYOUT_VERSION == 10`; `ClassifySaveVersion(11, 12) == Migrate`,
   `(12, 12) == Use`, `(13, 12) == UseDefaults`; `DefaultLayoutChangedSince(11) == false`.
6. A v11 save with every field set to a non-default value (volumes, sensitivity, invert, HUD scale, visibility, assist,
   vsync and quality with their chosen flags, HUD effects 0.3, hologram menu effects 1.7, steer at start, chase margin,
   space dust off, five overlay profiles, active profile 3, key overrides) and `HudGlow` deliberately set to 0.35 (to
   prove the step overwrites it): after `MigrateToCurrentVersion`, `Version == 12`, `HudGlow == 1.0`, every other
   field identical.
7. Chains: v1, v5, v6 and v10 saves migrate to 12 with `HudGlow == 1.0` and their earlier-step results unchanged
   (the existing chain tests' expected version moves from 11 to 12).
8. A v12 save with `HudGlow == 0.35` is left alone by `MigrateToCurrentVersion` (current version); `SetValues` /
   `GetValues` round-trip 0.35; a v12 file holding NaN or 9.0 is sanitized by the model to 1 / 2 (not by the save).

**Glow math** (`HudShapesTest.cpp`, `SOLHudShapeMath`):

9. `HudGlowScale(s) = SanitizedSetting(s) x HUD_GLOW_DEFAULT_LEVEL`, with the setting sanitized like the model (NaN
   -> 1, clamp [0, 2]): `HudGlowScale(1) == HUD_GLOW_DEFAULT_LEVEL`, `(0) == 0`, `(2) == 2 x LEVEL`, `(-1) == 0`,
   `(5) == 2 x LEVEL`, `(NaN) == LEVEL`. `0 < HUD_GLOW_DEFAULT_LEVEL < 1` (D4: below today's). Do not hard-code 0.5.
10. `GlowHaloWeight(g) == HUD_GLOW_HALO_WEIGHT x g`; `GlowCoreWeight(g) == 1 - GlowHaloWeight(g)`;
    `GlowCoreFalloff(g) == HUD_GLOW_CORE_FALLOFF / max(g, HUD_GLOW_CORE_FALLOFF / HUD_GLOW_CORE_FALLOFF_MAX)`;
    `GlowCoreFalloff(1) == HUD_GLOW_CORE_FALLOFF` (3), `(0) == HUD_GLOW_CORE_FALLOFF_MAX`, non-increasing in `g`;
    `HUD_GLOW_CORE_WEIGHT + HUD_GLOW_HALO_WEIGHT == 1`; `HUD_GLOW_CORE_FALLOFF_MAX > HUD_GLOW_CORE_FALLOFF`.
    Negative or NaN `g` behaves as 0 / 1 respectively (`SanitizeGlowScale`: NaN -> 1, clamp
    `[0, HUD_GLOW_SCALE_MAX]`, `HUD_GLOW_SCALE_MAX` = 2 in reference units, so `g = 1` is never clamped whatever level
    the user picks, and every reachable `HudGlowScale` value, at most `2 x LEVEL < 2`, passes unchanged).
11. `GlowProfile(d, g)` per section 4.2: `GlowProfile(d, 1) == GlowProfile(d)` for d in {0, 0.25, 0.5, 1, 3, 10, 20,
    32} (the reference stays today's); `GlowProfile(0, g) == 1` for g in {0, 0.25, 0.5, 1, `HUD_GLOW_SCALE_MAX`};
    negative d clamps to 0; `GlowProfile(0.5, 0) == 1` and `GlowProfile(1.5, 0) < 0.01` (crisp core, D4); for
    d > 0.5 the value is non-decreasing in g (sample g = 0, 0.25, 0.5, 0.75, 1, `HUD_GLOW_SCALE_MAX` at d = 1, 3, 10);
    for every sampled g it is non-increasing in d up to the pad and below 1% at `HUD_GLOW_PAD_PX`; matches the closed
    form computed in the test from the constants.
12. `TextGlowAlpha(g) == clamp(HUD_TEXT_GLOW_ALPHA x SanitizeGlowScale(g), 0, 1)`: `(1) == 0.18`, `(0) == 0`,
    `(NaN)` as `g = 1`.

**Per-shape glow factor and per-item text alpha** (`HudShapesTest.cpp` batch cases, `HudPanelsTest.cpp` or a new
`HudTextList` case for the text list; D10):

13. A fresh `FSOLHudShapeBatch`: every vertex of an added line, ring, arc, ellipse, dashed line and panel has
    `MaterialTexCoords == (0, 0)`. After `SetGlowOverride(0.3)`, the next shapes' four vertices carry `(0.3, 1)`;
    after `ClearGlowOverride()`, `(0, 0)` again; `Reset` clears an override left set; `SetGlowOverride(NaN)` stores
    `(1, 1)`, `SetGlowOverride(-2)` `(0, 1)`, `SetGlowOverride(9)` `(HUD_GLOW_SCALE_MAX, 1)`. The override changes
    nothing else in the vertex (positions, local frame, kind packing and colour as the existing batch tests expect).
14. `FSOLHudTextList`: an item added with no override has `GlowAlpha == -1`; after `SetGlowAlphaOverride(0.09)` the
    next items carry 0.09; after `ClearGlowAlphaOverride()` -1 again; `Reset` clears it; values are clamped to [0, 1]
    (NaN -> -1, meaning the layer's). Capacity and drop counting unchanged.

**Glossary parameter cache** (`SolidMenuKitTest.cpp`, `FSOLSolidMenuParamCache`; D11):

15. `Update(strength, dpi, glowSetting)` returns true on the first call, false on an identical repeat, true when only
    the glow setting changes (beyond `VALUE_EQUAL_TOLERANCE`), and the cache's glow scale equals
    `HudGlowScale(glowSetting)` (NaN setting -> `HudGlowScale(1)`). The existing strength / DPI cases keep passing with
    the new argument.

**Route halo** (`RouteOverlayMathTest.cpp`, `SOLRouteOverlayMath`):

16. `RouteHaloIntensity(g) == GLOW_HALO_INTENSITY x SanitizeGlowScale(g)`: `(1) == 0.15`, `(0) == 0`, `(-2) == 0`,
    monotone in g.

**Settings tabs** (`SolidSettingsTest.cpp`):

17. `TabOfSlider(HudGlow) == Gameplay` (D5); the table covers every slider including the new one
    (`ESOLSettingsSlider::Count` grows by one); `HudGlow` follows `HudEffects` in enum order.

**After the 37a review** (D16, D17, review findings 2 and 3):

18. `FSOLGlowScaleFeed` (`HudShapesTest.cpp`): a new feed has `GetScale() == -1` and
    `GetTextAlpha() == HUD_TEXT_GLOW_ALPHA`; the first `Update(1)` returns true with `GetScale() == HudGlowScale(1)`
    and `GetTextAlpha() == TextGlowAlpha(HudGlowScale(1))`; the same value again returns false; `Update(0)` true with
    scale 0 and alpha 0; `Update(2)` true, then `Update(5)` false (both clamp to `2 x LEVEL`); `Update(NaN)` reads as
    the default setting (true after 2, scale `HudGlowScale(1)`); `Reset()` restores -1 and the next update returns true
    even for the value it had.
19. `SSOLHudShapeLayer` text glow alpha (`HudShapeLayerTest.cpp`, `SOLTest.HudShapeLayer.TextGlowAlpha`): a new layer
    reads `HUD_TEXT_GLOW_ALPHA`; `SetTextGlowAlpha(0.09)` reads 0.09, `(0)` 0, `(-1)` 0, `(3)` 1, `(NaN)`
    `HUD_TEXT_GLOW_ALPHA`; setting it keeps the retain mode and the queued shapes in either mode.
20. `SOLRouteOverlayMath::GlowPieceTriangles(g)` (`RouteOverlayMathTest.cpp`): `SOL::Route::OVERLAY_GLOW_PIECE_TRIANGLES
    == 6`, `OVERLAY_GLOW_CORE_TRIANGLES == 2`; `(1)`, `(0.05)`, `(HUD_GLOW_SCALE_MAX)` and `(NaN)` give 6; `(0)` and
    `(-1)` give 2; it is 2 exactly when `RouteHaloIntensity(g) == 0`.

The material itself is not unit-testable; the smoke and screenshots check it (section 7).

## 6. Player-facing text and docs

- **Settings row label:** "HUD glow" (D6), value "0%" ... "200%"; Settings hint as in D6.
- **In-game Info** (`SOLMenuText.h`, the flight HUD paragraph): the D6 sentence as amended by D17, after the sentence
  on the HUD and menu effects slider. 37b adds that the readouts inside the hologram cards and the Help glossary's HUD
  pictures follow, and that the cards' corner marks keep their own look; 37c (review) adds the exception of the
  glossary's two route pictures, fixed textures captured at the default.
- **Settings glow sample text** (`SOLMenuText::SETTINGS_GLOW_SAMPLE`): "TARGET  Earth", a HUD-style readout.
- `Docs/GAME_MECHANICS.md`: a new Settings row "HUD glow (`HudGlow`) | 0-200% in 5% steps; default 100% (= N% of the
  issue #31 glow) ... | At once (preview); kept on Apply | ..." (save schema 12, the knob table of section 4.2, card
  readouts and glossary follow it, menus do not); the Flight HUD row's glow numbers rewritten as "reference x g" with
  the default level; the Route overlay row's halo; the glossary paragraph (the symbols follow the HUD glow).
- **SDD 21 Amendment 5** (as built): the glow-scale model, the material parameter and per-shape override, the default
  level the user chose, the knob meanings, measured cost and the screenshots. SDD 20 section 2.4 gets a one-line
  pointer for the halo; SDD 42 and SDD 33 a one-line note each (card readouts and glossary symbols follow HUD glow).
- `Docs/ARCHITECTURE.md`: `SSOLHudShapeLayer` / flight HUD / `USOLSolidMenuSubsystem` / `USOLHudSymbolView` entries
  (the `GlowScale` parameter pushed on change, the per-shape override used by card content), the HUD reads `HudGlow`,
  `USOLSettingsSave` schema 12. No new `-SOL*` flag (D14).
- `Docs/Plans/STATUS.md`: #37 in progress now, Done with results at the end.
- `Docs/PLAYER_GUIDE.md` and its images: regenerated with `Tools\WritePlayerGuide.bat` (D11); `SOLTest.PlayerGuide`
  must pass.

## 7. Verification

1. **Tests:** the section 5 contract (TDD, authored by a separate agent from this contract, red for the right reason
   first), then the full suite (`Tools\RunTests.bat`). Existing tests re-validated: every "schema version is 11" /
   "-> 11" assertion in `SOLSettingsRulesTest.cpp` and `FlightModeMathTest.cpp`, `SOLTest.HudShapes.GlowProfile`
   (unchanged contract), the batch vertex tests (`MaterialTexCoords` now meaningful), `SolidSettingsTest` slider
   table, the `FSOLSolidMenuParamCache` cases.
2. **Candidate screenshots for D4** (before the level is fixed; D14): with the placeholder level 0.5, the slider at
   60% / 100% / 140% gives exactly 30% / 50% / 70% of today's glow. New `-SOLSmokeHud` glow phases set the live
   (pending, never applied) value and take UI screenshots at 1920x1080 over a bright background (the lit Earth disc
   of the existing Earth-facing phase) and a dark one (facing away, star field), with a target selected so the target
   card shows, plus the route overlay. Today's look (`g = 1`, slider 200% at level 0.5) is shot as the baseline. The
   main session shows them to the user; the chosen level replaces the placeholder, and the candidate shots are
   deleted.
3. **Final screenshots:** 0% / 100% / 200% at 1280x720 and 1920x1080, bright and dark: 0% must show crisp, legible
   lines and text with no halo and no aliasing shimmer on the radar rings; the card readouts change with the slider
   while the card corner marks do not (D10); 200% stays at or below today's look when the level is at most 0.5 (it is 0.3). One
   shot of the radial menu at 0% proves the menus are unchanged (D3), and one Help glossary tab at 0% and 100% proves
   D11.
4. **Smokes:** `-SOLSmokeHud` at 1280x720 and 1920x1080, all checks PASS, with new checks that the pushed glow scale
   follows the live value and returns to the saved one after a revert, and that card content shapes carry the override
   while the frame shapes do not; `-SOLSmokeRouteOverlay`, `-SOLSmokeHoloMenu` and `-SOLSmokeSolidMenu` (the Settings
   Gameplay rows, narrow layout and slider widths with the new row, glossary pushes) PASS. After the 37a review:
   `-SOLSmokeRouteOverlay` checks the HUD-to-overlay wiring (the overlay's glow scale and halo intensity are the HUD's
   `g` and `RouteHaloIntensity(g)`) and that a pending 0% draws the route with fewer triangles and no halo, then
   reverts; `-SOLSmokeSolidMenu` opens Settings, Gameplay at 1280x720 and 1920x1080 (windowed) and in windowed
   fullscreen at the display's own size (3840x2160 on a 4K display: the engine clamps a windowed size to the desktop's
   work area, so a 4K view needs a 4K display) and checks the glow sample is laid out inside the tab, painted (shapes,
   one text line, one backing), follows a pending 0% and 200% (pushed scale and text alpha) together with the
   pointer, and returns to the saved value on Revert, with a screenshot at each size. The solid menus scale with the
   view's height, so 3840x2160 is the 1920x1080 layout at twice the pixels.
5. **Persistence check:** a v11 settings file in the smoke slot loads, shows HUD glow 100%, and is written back once as
   v12 at the next write point with every other value intact; moving the slider and leaving without Apply writes
   nothing.
6. **PIE smoke check** via unreal-mcp when the editor is open (slider drag on the pause menu's Settings, HUD updates at
   once); noted in the plan if it could not be run.

Temporary screenshots are deleted once looked at; only README gallery shots are kept.

**As run in part 37c (2026-11-11, level 0.3).** Full suite 1800/1800. `-SOLSmokeHud` 45/45 at 1280x720 and
1920x1080: the glow series (pending 0%, 100%, 200% over the lit Earth and the dark star field, each checking the pushed
glow scale, text glow alpha and the menu material's `GlowScale` 1, `HudSmoke_GlowSeries_<earth|dark>_<percent>.png`),
then the F3 hologram speed panel opened at a pending 0% and at the saved glow (`l-menu-*`,
`HudSmoke_GlowSeries_menu_<0|saved>.png`: the panel's frame and text identical, only the HUD behind it and the pointer
change; this stands in for the radial-menu shot of item 3, the same menu material), then the existing pending / revert
and card checks. `-SOLSmokeRouteOverlay` 29/29 (overlay glow 0.3, halo 0.045, 340 triangles; at 0% no halo and 180
triangles, with a screenshot; Revert restores). `-SOLSmokeHoloMenu` 86/86. `-SOLSmokeSolidMenu` 296/296 (the glossary
at 0% and back, the Settings glow sample). Looked at: 0% is crisp with smooth radar rings at both sizes; the card
readouts change with the slider and the corner marks do not; 200% (60% of the issue #31 glow) stays below the old
look. `Tools\WritePlayerGuide.ps1` run in full: the two route crops (`T_SOLGlossaryRouteLegs`, `T_SOLGlossaryWaypoints`)
re-captured and re-imported at the new glow, the other three kept; the guide text unchanged, its Help images redrawn.
Persistence: the 11 -> 12 migration is covered by `SOLTest.SettingsRules.MigrateV11HudGlow` and the migration chain
tests; the real slot `SOLSettings.sav` was migrated once in 37a and not written by any 37b / 37c smoke (no write
without Apply). The PIE check via unreal-mcp was not run (the editor was closed for builds and smokes; unreal-mcp needs
it open); the smokes drive the same settings path.

## 8. Performance

- **CPU:** one float compare per frame in `BeginShapeFrame`; `SetScalarParameterValue` and the text-alpha set only
  when the value changes (a slider step); the glossary cache compares one more float. Card content sets and clears two
  floats per card per frame; every `Add*` copies one more float2 into vertices it already writes. The route overlay
  does one multiply per glow piece and, at 0%, appends 2 instead of 6 triangles. No allocation, no new UObject, no new
  tick, no string building; one more float per text item (capacity reserved once).
- **GPU:** the glow line gains a select and about three ALU ops per pixel in a shader already dominated by the
  distance and tone code, plus one more interpolated float2 (Customized UV 2). Draw count, quad sizes (the 32 px pad is
  kept, D13) and overdraw are unchanged. At 0% the glow text drops its outline pass and the route overlay draws fewer
  triangles, so 0% is never dearer.
- **Budget:** HUD cost unchanged within measurement noise. Measured, not assumed (CLAUDE.md review items 9 and 11,
  `Docs/Research/VR_PERFORMANCE_TARGETS.md` section 6): `ProfileGPU` with `r.ProfileGPU.ShowUI 1` and
  `stat dumpave` in a `-game -SOLNoMenu` run (the SDD 21 method: `Slate Material: MID_M_SOLHudShape_0`, the menu
  layer's MID and the whole Slate UI pass) at 0% / 100% / 200%, before and after the change, at 1920x1080, the 4K
  proxy (3840x2160, VR doc section 7a) and one XR-4 eye (3840x3744); two eyes skipped (D15). The UI must stay inside
  the provisional one-eye ceiling (1.2 ms GPU, VR doc section 7) and add no more than +0.01 ms GPU over the before
  figure at 1920x1080.
- **Measured (part 37c, 2026-11-11, RTX 2080 Ti).** `-SOLSmokeHoloMenuProfile` (the SDD 42 profile, `ProfileGPU` and
  `stat dumpave` per pass, vsync off, scalability 3) in three runs per build and size, interleaved, before (a worktree
  at `5388c2f`, the commit before #37) and after (level 0.3), at 1920x1080, 3840x2160 and 3840x3744 (one XR-4 eye).
  The profile gained two flight passes for this ticket, P5 at HUD glow 0% and P6 at 200%; P0-P4 run at the saved
  100%. The pre-#37 build logs every pass as FAIL with `valid=no` at these sizes: its stored baseline and validity
  rule are for 1280x720 only, so its verdict is ignored and validity is read from the logged fields (vsync off,
  scalability 3, the view size asked for); the new build logs NOBASE with `valid=yes`. Medians of the whole Slate UI pass's GPU (ms), before / after:

  | Pass | 1920x1080 | 3840x2160 | 3840x3744 |
  |---|---|---|---|
  | P0 flight HUD | 0.115 / 0.117 | 0.192 / 0.194 | 0.317 / 0.322 |
  | P1 radial menu | 0.173 / 0.176 | 0.271 / 0.274 | 0.393 / 0.404 |
  | P2 Overlay Menu | 0.235 / 0.236 | 0.497 / 0.501 | 1.142 / 1.152 |
  | P3 speed panel | 0.247 / 0.249 | 0.602 / 0.604 | 1.248 / 1.259 |
  | P4 map list | 0.156 / 0.158 | 0.244 / 0.249 | 0.380 / 0.384 |
  | P5 flight HUD at 0% | - / 0.109 | - / 0.187 | - / 0.315 |
  | P6 flight HUD at 200% | - / 0.117 | - / 0.196 | - / 0.323 |

  The change adds +0.001 to +0.003 ms at 1920x1080 (inside the +0.01 ms limit; at most +1.7%), up to +0.005 ms at 4K
  (at most +2.0%) and +0.004 to +0.011 ms at one eye (up to +2.8%, P1 radial: 0.393 -> 0.404 ms). The `M_SOLHudShape` MID alone moves by the same
  +0.001 to +0.015 ms. The flight HUD stays at 0.32 ms at one eye, far inside the 1.2 ms ceiling. 0% is the cheapest
  setting (0.109 ms against 0.117 at 1920x1080: no text outline pass, the route's halo triangles skipped) and 200%
  costs the same as 100% (only the shader's constants differ). The speed panel's one-eye figure (1.248 ms before) was
  already above 1.2 ms; its frame is drawn by the unchanged menu material (D3), so #37 does not change it (+0.011 ms,
  the shared `GlowScale` multiply and the HUD behind it). CPU (`cpu_total` of the profile) is unchanged within its
  run-to-run spread (0.27-1.02 ms before and after, no pass moving in one direction at every size); draws per pass
  are unchanged (45-69), except 43 instead of 45 at 0% (the text outline batch is not drawn).
- Known debt not to re-report: `Docs/TECH_DEBT.md` "HUD glow text ... two string copies per line per frame",
  "HUD hologram panels ... flicker per pixel / worst-case profile", "HUD shape layers copy their vertices on every
  paint", and the Settings key-row items.

## 9. Open questions

None. Resolved in 37c: the default level (D4) is 0.3, the user's pick from the candidate screenshots (tests reference
the constant, so no test changed); `HUD_GLOW_CORE_FALLOFF_MAX` stays 6 (the 0% shots are crisp, with smooth,
antialiased radar rings and no stair steps or shimmer at 1280x720 and 1920x1080). At level 0.3 the cap binds for
every slider value up to about 167% (g <= 0.5), so the core edge is the same crisp edge from 0% to 167% and only the
halo and text outline grow there; at 200% (g = 0.6) the falloff is 5.

## 10. Change log

- 2026-11-10: first draft from grill-me D1-D4; open questions Q1-Q11 and the default level reported.
- 2026-11-10: Q1-Q11 answered as recommended (D5-D15); card readouts (section 4.6) and glossary (section 4.7) added to
  the design and contract; engine Slate shader check for the vertex slot recorded (section 4.6). Remaining open: the
  default level (D4).
- 2026-11-10: part 37a built and reviewed. User decisions on the review: D16 (Settings glow sample) and D17 (the
  pointer follows HUD glow, narrowing D3), section 4.10 and contract cases 18-20; D14 corrected (the real save slot,
  values kept pending); the route piece triangle count as a pure helper with the overlay's glow scale kept for the
  smoke.
- 2026-11-10: part 37b built: the per-shape glow override (engine route proven by compile and screenshots), the
  per-line text glow alpha, the target and info cards' readouts through `BeginReadoutGlow`, the glossary glow push and
  the views' text alpha; the Customized UV connection through an editor-only helper (hidden enum entries).
- 2026-11-11: part 37c: D4 decided by the user (level 0.3), section 9 closed; the glow series and F3 panel phases in
  `-SOLSmokeHud`, the 0% route shot, the profile's glow passes (P5, P6) and view sizes; results in sections 7 and 8;
  SDD 21 Amendment 5, docs, the player guide and route crops, the README gallery. Whole-ticket review (no Critical /
  High): the one-eye percentages corrected in section 8, the pre-#37 profile verdicts explained, the Info text names
  the glossary's two fixed route pictures, a hold before the F3 panel closes in `-SOLSmokeHud`, a distinct override
  value in `SOLTest.HudShapes.BatchGlowOverride`, editor-only includes, style nits; the speed panel's one-eye cost
  recorded as tech debt and in the VR targets doc.
