<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Solid-menu style sources (round 2 / C)

Editable sources for the chosen solid-menu style: **round 2 / C, asymmetric hull** ([`round2-C.png`](../../Images/menu-style-samples/round2-C.png)) on the **direction 1 gunmetal + cyan** palette ([`direction-1.png`](../../Images/menu-style-samples/direction-1.png)). Background and decisions: [`Docs/Research/SOLID_MENU_STYLE_REFERENCES.md`](../../Research/SOLID_MENU_STYLE_REFERENCES.md) and [`Docs/Plans/46-solid-menus-grill-notes.md`](../../Plans/46-solid-menus-grill-notes.md).

| File | What it is |
|---|---|
| `solid-menu-round2C.psd` | Layered Photoshop file, 1280x720, RGB 8-bit. The full board, made of live shape layers, live type layers and layer styles. |
| `solid-menu-round2C.ai` | Vector file with 3 artboards, named layers and a swatch group. It is a **PDF-compatible .ai** (the file starts with `%PDF`), not an Illustrator-native .ai. |

## How they were made

Both files were written by the ArtCraft tools (PhotoCraft and VectorCraft), not converted from the PNG:

- **PSD:** PhotoCraft (`photocraft-cli serve` with a write root) re-ran the exact command log that built the round 2 board: the 512 px brushed tile, then every shape, path, type layer and layer style. The layers were then grouped and renamed, and the file was saved as PSD. Its render matches the original board pixel for pixel, except for the brushed-noise grain, which is random on each build.
- **AI:** VectorCraft (`vectorcraft-cli run`) drew the same path geometry, taken from that command log, as flat-filled vector paths. It added the components and palette artboards and a global swatch group, then exported a PDF-compatible .ai.

## PSD layer groups (top to bottom)

| Group | Contents |
|---|---|
| Palette (reference, hidden) | Swatch chips with names and hex codes on a dark plate. Hidden; turn it on to pick colours. |
| Text samples | Section headers, body paragraph, small-caps footer, notes, board captions (live type). |
| Buttons | Subgroups **Button normal**, **Button pressed** and **Button hover**, each holding its hull, RESUME label and caption, plus the shared **Button LEDs (off)** layer. |
| Toggle on/off | Toggle slots, pucks, LED on/off, ON/OFF state text, row labels. A group mask shows only the toggle rows. |
| Slider | Groove, cyan fill trace, octagon knob, knob LED, value text, row label. A group mask shows only the slider row. |
| Title tab | SETTINGS title and page indicator. The tab shape itself is the stepped top edge of the panel hull. |
| Circuit traces (glow) | Circuit traces, ring terminals, menu hover trace, logo underline (outer glow). |
| Vents + Rivets | Pylon, chin and recess vents, octagonal rivets. |
| Recesses | The three inset recesses (inner shadow, tile overlay). |
| Panel hull | The asymmetric panel: unequal chamfers, stepped title edge, side notches, chin. Bevel, edge stroke, tile overlay, drop shadow. |
| Main menu stack | Pylon hull, menu tabs (normal), extended hover tab, pylon LEDs (off / cyan / amber), hazard stripe, menu labels, SOLTEST logo. |
| Background tile | Brushed gunmetal fill (the tile as a pattern overlay) and vignette. |

The toggle and slider share some shape layers in the original board. Each group therefore holds its own copy of those layers, and a group mask splits them at y = 222.

Every layer has a descriptive name, for example `Button hover hull (lifted, glow)` or `Pylon LED (warning, amber)`. The look comes from layer styles (pattern overlay, bevel & emboss, stroke, inner/outer glow, inner/drop shadow), so editing the shape path or resizing it keeps the style. The BrushedGray pattern is embedded in the PSD.

## AI artboards and layers

Artboards: **1 Round2-C board** (1280x720, the full layout), **2 Components** (1280x720, each part separate and labelled) and **3 Palette** (1280x220, swatch chips with hex codes).

Layers (bottom to top): Background, Panel silhouettes (pylon, panel hull, recesses, menu tabs normal/hover), Hull outline (edge strokes), Vents + Rivets, Circuit traces, Buttons (normal / hover / pressed, LEDs), Slider + Toggles, LEDs + Hazard, Components (artboard 2), Palette swatches, Text samples (Bahnschrift stand-in). The Swatches panel has a global group **SOL solid menu (round2-C)**.

The vector file has the shapes only, as flat fills. Bevels, glows, shadows and the brushed texture are in the PSD.

## Palette

The colours are from the research doc (round 2 values; dim text was raised from #8E9AA4 to #A4AFB8 for contrast).

| Role | Hex | Use |
|---|---|---|
| Base | #24282C | Screen base, recesses |
| Panel | #343A40 | Panel hull |
| Raised | #4A5159 | Buttons, tabs |
| Edge | #8C969F | Edge highlight / outline |
| Shadow | #121416 | Deep shadow |
| Text | #DCE3E8 | Main text (8.9:1 on panel) |
| Dim text | #A4AFB8 | Secondary text (5.15:1 on panel); in game this is the body text `TEXT_COLOR`, and hints use the dimmer `TEXT_DIM_COLOR` #8A949D (SDD 46 section 5.1) |
| Accent | #46D2E6 | HUD cyan: traces, LEDs, values, hover |
| Warning | #F2A93B | Amber LED, hazard stripe |

The board also uses a few in-between shades: board background #1D2124, pylon #2E3338, hover hull #58626B, knob #5A626B, pressed hull #1C2024, LED on #7FE6F4, vent/LED-off black #0B0D0F, rivet #6A737C.

## Fonts

All type is **Bahnschrift**, a Windows system DIN-family variable font, used as a **stand-in**. The final menu font is **Rajdhani or a similar free OFL techno sans**, bundled with the project (see the #46 grill notes). Tracking: titles 380-450, menu labels 260, small caps 150-300. When the final font is chosen, change the font on the type layers. Sizes and tracking may need small adjustments, because Rajdhani is narrower than Bahnschrift.

## Reusing for a new menu

1. Duplicate the PSD. Keep **Background tile**, **Panel hull**, **Recesses** and **Vents + Rivets**. Edit the panel path for a new size, keeping the chamfer and step motifs.
2. Copy a **Button** subgroup, **Slider** or **Toggle on/off** group for each control. The layer styles come with them. For a new button state, duplicate a state subgroup and change only the hull fill, LED and glow.
3. Use the AI's **2 Components** artboard as the clean vector reference for 9-slice brushes. Export a piece with File > Export Selection (or `vectorcraft-cli convert`) to SVG or PNG. Keep the chamfers in the corner slices: a mid-edge notch cannot stretch in a 9-slice (see the shape vocabulary table in the research doc).
4. Pick colours only from the **SOL solid menu** swatch group or the hidden palette group, so new screens stay on the palette.
5. Final in-game art is a UMG material or 9-slice texture (not decided yet). These files are the style reference and the source for those textures.

## Limitations

- **.ai format:** the file is a PDF-compatible .ai written by VectorCraft. VectorCraft reopens it fully editable (artboards, layers, swatches). Adobe Illustrator opens it as PDF content: vectors and text come through, but the layers are stored only as PDF optional-content groups and may not come back as Illustrator layers. VectorCraft cannot write an Illustrator-native .ai (with AIPrivateData).
- **PSD:** made by PhotoCraft. Layer styles, group masks and live type are standard PSD features, but the file has not been checked in Adobe Photoshop. If Bahnschrift is missing, type layers show a font substitution warning.
- The effects are still images. Motion (rollover lift, trace pulse, panel slide-in) is described in the board's MOTION text and the research doc.

## Solid menu asset sources (SDD 46, part 46b)

Textures in `textures/` are generated by `generate_textures.py` (Pillow and numpy, fixed seed, so a re-run reproduces
the same PNGs): `brushed-gray-512.png` (neutral grey grain, 110 px circular motion blur along X, seamless both ways,
tinted in the material), `rivet-32.png` (octagon with bevel, alpha), `vent-strip-128x16.png` (white slot mask, tiles
in X), `hazard-128x16.png` (45 degree stripe mask, colours come from the palette in the material; the mask of the
optional hatched disabled cue). The `noise-64.png` dither tile of 46b was dropped in the 46c follow-ups (the hull
dithers with a hash, nothing sampled it). They were scripted rather than made with the craft MCP tools (the SDD allows either).
Regenerate: `python Docs/Art/menu-style/generate_textures.py`.

The font is Rajdhani (Indian Type Foundry, SIL OFL 1.1) in `../fonts/rajdhani/` (Regular, Medium, SemiBold, Bold and
`OFL.txt`), from the Google Fonts repository (`ofl/rajdhani`).

Import into `Content/` (closed editor, nothing else holding the project):

```
"C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "D:\Development\UnrealProjects\SOLTest\SOLTest.uproject" -run=pythonscript -script="D:\Development\UnrealProjects\SOLTest\Tools\UI\import_solid_menu_assets.py" -unattended -nosplash -nullrhi -stdout -FullStdOutLogOutput
```

Look for `SOLSolidAssets:` lines ending in `DONE`. The script reads the asset paths from `SOL::Paths` (`SOLID_*`) in
`Source/SOLTest/SOLConstants.h`, tags each asset with the MD5 of its source and skips unchanged ones (a second run
writes no `.uasset`). It creates `FF_Rajdhani_<Weight>` font faces and the composite font `F_SOLMenu`.
