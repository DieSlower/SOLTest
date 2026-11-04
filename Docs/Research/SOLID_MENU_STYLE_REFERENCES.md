<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Solid Menu Style References: How Space and Simulation Games Style Their Menus

**Status:** research supporting the "solid console" look for the full-screen menus. **Compiled:** 2026-11-03 from a short web search pass plus general design knowledge. Read "What could not be verified" before relying on any hex or font in the tables.

## Purpose

The full-screen menus (main menu, settings, demo, the read-only Controls / Info / About screens, pause) will get a solid brushed-metal "console" look with textured panels. It must read as clearly different from the cyan hologram look of the in-flight HUD and menus. The user picks one of three palettes from sample boards. This note collects how other space and simulation games style their UI, so the three directions are grounded in real precedent, and lists the cross-cutting lessons for metal-look UI.

Honesty note: the web search returned few primary sources (no developer style guides, almost no hex values). Most entries below are descriptions from press, community pages and general knowledge. Anything not tied to a source in the Sources list is marked **(inferred)**.

## Per-game findings

Legend: material, palette, frame / bevel, buttons, type.

| Game | Material | Palette | Frame and buttons | Typography |
|---|---|---|---|---|
| Elite Dangerous | Flat dark panels with glowing lines, no metal texture; cockpit UI is projected (semi-diegetic). | Orange HUD with blue highlights by default; colours are user-adjustable via a colour matrix, so there is no fixed hex (sources: Windows Central, PC Gamer). Dark near-black grounds **(inferred)**. | Thin outlines, angled corners **(inferred)**. Hover by brightening/inversion **(inferred)**. | A Frontier forum thread and the Oolite wiki point at Dosis-like rounded type for some UI; unconfirmed. |
| Star Citizen (mobiGlas, MFDs) | Projected hologram / glass cards (diegetic); shared shape, outline, iconography and colour system across the "UI Card System" (hudsandguis). | Cool blues and cyans on dark **(inferred)**. | Thin outlined cards, corner brackets **(inferred)**. | Eurostile-like wide caps are widely assumed; no source found. |
| Everspace 2 | Dark flat/glass panels with accent glow, "sleeker" than the first game (press). | Dark blue-grey with cyan/orange accents **(inferred)**. | Simplified vs detailed ship panels (press). Buttons flat with bright hover **(inferred)**. | Not found. |
| X4: Foundations | Dense dark flat panels; reviewers call the menus intimidating. | Dark with blue-cyan accents **(inferred)**. | Many small tabbed panels. Default font is a texture asset (Egosoft forum). | Not found. |
| Kerbal Space Program 1/2 | KSP 2: slick, flat, "techy" with stark icons (press). KSP 1: cartoon-industrial flat panels **(inferred)**. | Dark slate with white text and a single bright accent **(inferred)**. | Rounded flat buttons **(inferred)**. | KSP 2 font is not user-changeable and drew complaints for small size and poor low-resolution scaling (KSP forums). Name not found. |
| Space Engineers | Industrial, "safety warning" look: very dark grey, light grey, rust brown, safety yellow is the author's stated world palette (Marek Rosa blog). The blog is about world art, not the UI itself. | Dark greys with blue-ish terminal panels **(inferred)**. | Tabbed terminal (inventory, control panel, production, info, etc.). | Not found. |
| Homeworld (1/2/3/Remastered) | Dark, glassy tactical overlay on a 3D scene. | Muted blue-grey with white/cyan lines **(inferred)**. | Not verified. | A Microgramma-family page lists the series among uses; not confirmed for the UI. |
| Microsoft Flight Simulator 2020/2024 | Light-on-dark flat avionics-style menus plus real glass-cockpit displays. | Dark UI with white text and a teal/blue accent **(inferred)**. | Not verified. | Not found (aviation-style humanist sans **(inferred)**). |
| Star Wars Squadrons | Diegetic cockpit instruments; holographic overlay. | Faction colours (blue / red) **(inferred)**. | Not verified. | Not found. |
| No Man's Sky | Flat, colourful, 70s-80s sci-fi illustration spirit; menus influenced by Destiny per a 2016 article. | Bright accent colours on dark **(inferred)**. | Not verified. | Not found. |
| Alien: Isolation | Deliberate retro-future ("Lo Fi Sci Fi", 1979 Alien): CRT and analog artifacts, UI run through VHS and old TVs for a soft, fuzzy look (Engadget, hudsandguis). | Monochrome/duo-tone phosphor screens; Xbox coverage mentions duo-tone and Atari-style fonts. Exact colours not found. | Chunky bezels and physical buttons. | Blocky retro fonts, not named. |
| Dead Space | Fully diegetic holographic UI on the suit and in the world (hudsandguis, VR Room). | Cool blue-white holograms **(inferred)**. | None; no menu chrome. | Not found. |
| Starfield | "NASA-punk": grounded, Golden-Age-of-space feel (TechRadar, PCGamesN). Menus are panels on a dark scene; critics found them hard to navigate and the type too small (Kotaku, Siliconera). | Light type on dark; no official palette. | Clean grid layout; Swiss/NASA graphics-standards flavour. | Community mods replace the font with Helvetica or Exo 2, which implies the default is a geometric/condensed sans; name not confirmed. |
| FTL, Mass Effect, The Expanse games, Ground Branch | Searched only in passing. No usable source. | n/a | n/a | n/a |

## Cross-cutting takeaways

- **Almost no game uses real textured metal for its menus.** Space-game menus are mostly dark flat or glass panels with a glow accent. A solid brushed-metal look is a deliberate departure, and it is what separates the full-screen menus from the hologram HUD. Closest precedents: Alien: Isolation (physical bezels, CRT), Space Engineers' industrial palette, Starfield's grounded NASA look.
- **Diegetic vs non-diegetic.** Full-screen menus are non-diegetic by nature. A "console" frame (a panel you are looking at) gives them a diegetic flavour without costing readability. Dead Space and Elite show the cost of full diegesis: it works for HUDs, not for dense settings screens.
- **Text sits on flat fills, never on texture.** Brush noise behind text lowers contrast and shimmers when the UI scales. Keep the texture on frames, rails and bezels; put text on smooth inset panels.
- **Contrast.** Aim for at least 4.5:1 body text, 3:1 large text and controls (WCAG). Dark-text-on-light-metal (direction 3) needs deep text (#1B2127) and no gloss behind labels. Dim text must still pass 4.5:1 on its panel.
- **Banding and tiling.** Metal gradients on 8-bit UI textures band; add faint noise/dither or use a high-bit source and keep gradients subtle. Brushed grain tiles visibly; use a large tile, brush along one axis only, and vary by panel.
- **Glare.** Specular streaks and big highlights fight the text and tire the eye in long sessions. Keep highlights to thin edge lines (1-2 px) and avoid animated glare on text panels.
- **State clarity beats ornament.** Hover, focus, pressed and disabled must differ by more than hue: change brightness/position (raised vs inset), add an accent edge or underline, and never rely on colour alone (accessibility, colour-vision).
- **Scale.** Starfield and KSP 2 drew complaints for small text and poor low-resolution scaling. Pick a minimum body size and test at 1080p and on a small window.
- **One accent colour, one warning colour.** The three directions already follow this. Elite shows the downside of baking a single colour into bitmaps: players cannot fix it, so palette values should be data, not texture.
- **Distinguish from the HUD.** The hologram HUD is cyan, thin-lined and translucent. The menus should be opaque, thick-bordered and raised/inset. Reusing the cyan only as a small accent (direction 1) ties the two without confusing them.

## Three candidate directions

All values are as specified for the sample boards.

### 1. Gunmetal console

Dark brushed gunmetal steel, cool and military.

- Base #24282C, panel #343A40, raised #4A5159, edge highlight #8C969F, shadow #121416.
- Text #DCE3E8, dim text #8E9AA4. Accent HUD cyan #46D2E6, warning amber #F2A93B.
- Type: wide geometric sans caps (Eurostile / Orbitron-like), letter-spaced titles.
- References: Star Citizen and Elite (wide caps, cool accent), Alien: Isolation (physical bezels), Homeworld (military tactical).
- Pros: closest to the existing HUD, so the cyan accent feels like one family; dark panels are easy on the eyes in a space scene; highest text contrast.
- Cons: nearest to the HUD, so the "clearly different" goal depends on solid texture and thick bevels, not colour; cyan accent can read as hologram if overused.

### 2. Brass industrial

Warm, worn bronze and brass, steampunk-adjacent NASA-punk industrial.

- Base #1E1913, panel #3A2E20, brass #B08A4A, highlight #E2C47E, shadow #0E0B07.
- Text #F1E3C2, dim text #A8946E. Accent amber #FFA630, warning red #D9472B.
- Type: condensed industrial sans caps (DIN Condensed / Bank Gothic-like), stencil-ish feel.
- References: Space Engineers (rust and safety colours), Starfield NASA-punk, Alien: Isolation retro-future.
- Pros: strongest contrast with the cyan HUD; distinctive; warm tones suit a solid, lived-in feel.
- Cons: amber accent and warning red sit close together; dim text #A8946E on #3A2E20 needs checking for 4.5:1; risk of looking themed (steampunk) rather than a space sim.

### 3. Cool light aluminium

Light brushed aluminium, clean lab / cockpit avionics.

- Base #B9BFC5, panel #CDD2D7, raised #E2E6EA, edge shadow #6C747C, deep #3A4047.
- Text #1B2127, dim text #56606A. Accent signal orange #F26B1D, info blue #1F6FD1.
- Type: clean humanist/technical sans (DIN / Rajdhani-like), dark text on light metal.
- References: Microsoft Flight Simulator and KSP (clean, light, instrument-like), Starfield's NASA graphics-standards flavour.
- Pros: most different from a dark space scene and the HUD; excellent for the long read-only Controls / Info / About text; modern and clean.
- Cons: a bright full-screen panel over a dark game can feel glaring and break immersion; dim text #56606A on #CDD2D7 is near the contrast limit; gloss and glare are most visible on light metal.

## Sample boards

One 1280x720 board per direction, each showing a seamless brushed-metal tile (repeated), bevelled panels with title bars, a slider and toggles, a button in normal / hover / pressed states, a sample text line and the palette with hex values:

- [`direction-1.png`](../Images/menu-style-samples/direction-1.png): gunmetal console.
- [`direction-2.png`](../Images/menu-style-samples/direction-2.png): brass industrial (condensed type, chisel bevels, rivets, dark engraved text on brass).
- [`direction-3.png`](../Images/menu-style-samples/direction-3.png): cool light aluminium (hover and pressed use the info blue; the signal orange marks "on").
- [`brushed-tile-gray-512.png`](../Images/menu-style-samples/brushed-tile-gray-512.png): the neutral 512 px seamless grain used on every board, tinted per direction with an overlay blend.

How they were made: PhotoCraft (MCP). The grain is monochrome Gaussian noise, a 110 px horizontal motion blur, then a half-width wrap offset cross-faded through a feathered selection so the left and right edges meet (checked by offsetting the tile half a tile both ways: no seam). Panels are shape layers with a pattern overlay of that tile plus bevel, stroke, inner/drop shadow and outer-glow layer styles. A MaterialCraft graph (anisotropic noise, blend, gradient maps for gunmetal, bronze, brass and aluminium) produced matching procedural tiles, but it could not be exported (see below), so the boards use the PhotoCraft tile.

Computed WCAG contrast on each direction's panel colour (replaces the rough judgements above): direction 1 text 8.9:1, dim text 4.0:1, cyan accent 6.4:1; direction 2 text 10.4:1, dim text 4.5:1, dark title text on brass 5.5:1; direction 3 text 10.7:1, dim text 4.2:1, signal orange 2.0:1 (so orange is used as a fill behind dark text, never as text). Dim text in directions 1 and 3 is just under 4.5:1 and would need a step brighter / darker for body-size use.

Limits of the boards:

- **Font.** All three use Bahnschrift (a DIN-family variable font that ships with Windows), varying weight and width (condensed for direction 2). No Eurostile, Orbitron, Bank Gothic or Rajdhani is installed, so direction 1's "wide geometric caps" is only approximated with letter-spacing. The final font is still an open decision.
- **Static mock.** Layer-style bevels and glows stand in for what would be a UMG material or 9-slice texture; hover glow, cursor and pressed motion are shown as stills. The cyan arrow is a stand-in for the #22 hologram cursor, which keeps its own look in every direction.
- **Tool file access.** The MCP tools ran without write/read roots, so nothing could be saved or exported from them directly (no `.pcraft` / `.mcraft` package files); the PNGs are the tools' full-size preview renders, copied into the repo.

## What could not be verified

- No official style guides, and almost no hex values, were found for any game. Every "palette" entry marked inferred comes from general impressions, not sampled pixels. Sample real screenshots before quoting any hex.
- Fonts: nothing confirmed by a developer. Elite Dangerous (Dosis) and Homeworld (Microgramma) are leads from a forum thread / wiki mirror only; Starfield's and KSP 2's names were not found; Eurostile for Star Citizen is an assumption.
- Elite Dangerous colours: only that the default is orange with blue highlights and that it is user-adjustable.
- No usable sources at all for: X4 (visual style), Homeworld (UI details), Microsoft Flight Simulator, Star Wars Squadrons, No Man's Sky (UI specifics beyond the Destiny comparison), FTL, Mass Effect, The Expanse games, Ground Branch.
- Button hover/pressed behaviour was not documented for any game; all such entries are inferred.
- Contrast notes in the direction pros/cons were judgements; the computed ratios are in "Sample boards".
- The Frontier forum thread on the Elite font returned HTTP 403 when fetched; its claim comes from the search snippet only.

## Sources

- https://www.pcgamer.com/how-to-change-the-colour-of-your-ships-hud-in-elite-dangerous
- https://windowscentral.com/how-change-your-hud-color-elite-dangerous-pc-only
- https://forums.frontier.co.uk/threads/graphics-what-font-does-elite-dangerous-use.60385 (search snippet only)
- https://wiki.alioth.net/index.php/Fonts
- https://www.techradar.com/news/starfields-developers-are-aiming-for-a-nasa-punk-aesthetic
- https://www.pcgamesn.com/starfield/nasa-punk
- https://www.siliconera.com/starfields-biggest-ui-problem-isnt-the-start-screen/
- https://kotaku.com/starfield-release-menu-ui-starmap-inventory-xbox-review-1850797661
- https://allmods.net/starfield-mods/starfield-user-interface/starfield-string-sharpener-a-font-overhaul-v2-6-1/
- https://hudsandguis.com/home/star-citizen-revisited-part-2
- https://www.hudsandguis.com/home/star-citizen-revisited-part-1
- https://robertsspaceindustries.com/en/comm-link/engineering/14466-Design-Notes-MobiGlas (listed in results, not read)
- https://www.windowscentral.com/everspace-2-features-new-drones-interface-improvements-and-more
- https://forum.kerbalspaceprogram.com/topic/224169-font-is-too-small
- https://forum.kerbalspaceprogram.com/topic/214422-ui-and-text-at-lower-resolution-and-smaller-displays
- https://blog.marekrosa.org/2017/09/my-vision-for-visual-style-of-space/
- https://forum.egosoft.com/viewtopic.php?p=4770968
- https://wccftech.com/review/x4-foundations-beautiful-framework/
- https://hudsandguis.com/home/2014/06/04/alien-isolation
- https://www.engadget.com/2014/10/07/alien-isolation-launch-interview/
- https://news.xbox.com/en-us/2014/09/24/games-8-awesome-retro-gadgets-in-alien-isolation/
- https://www.hudsandguis.com/home/2012/08/22/dead-space-2-diegetic-interface-design
- https://archive.vrroom.studio/?p=30438
- https://medium.com/super-jump/armchair-vgux-no-mans-sky-9d91bb257e16
- https://twinfinite.net/ps4/5-traits-no-mans-sky-adopted-from-destiny/2/
