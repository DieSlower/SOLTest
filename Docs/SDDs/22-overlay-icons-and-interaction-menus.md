<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 22: Overlay icons, overlay menu and interaction menus

Issue #22. Depends on SDD 21 (hologram HUD renderer), SDD 20 (map, routes, `SOL::Hologram`), SDD 19 (steering gate),
SDD 17 (orbit lines). Status: Part 1 built (Amendment 1); Part 2 (interaction menus) to do.

## 1. Goal

An LT-style overlay layer: an icon for every selectable object in the flight view, a hologram **Overlay Menu** that
chooses what the O key toggles, and an interaction menu (radial on icons in flight, list on the map) that issues orders
through a generic action table. A hologram mouse pointer everywhere the cursor shows.

## 2. Decisions

### 2.1 Icons

- Objects: star, planets, moons, dwarfs, named asteroids, ring systems, dropped targets, NPC ships (budgeted; nearest and
  selected first, never one icon per NPC). One icon shape per category in the hologram style; the same set replaces the
  map's plain discs (SDD 20 deferred this here).
- Shown only while overlays are on. Shown for every selectable object, and fading out as the real object grows large on
  screen (icon is brightest when the object is small or far). Edge-pinned arrows off-screen, distance fade; Shift+O
  toggles the distance darkening.
- Per-category icon toggles exist (Overlay Menu).

### 2.2 Keys

| Key | Meaning |
|---|---|
| O | Master: turn all overlays that are enabled in the Overlay Menu on / off together (remembers; includes the darkening state) |
| Alt+O | Orbit lines on/off (moved from Ctrl+O). Left Alt alone stays free look; the chord must not trigger free-look side effects (verify in the SDD plan) |
| Ctrl+O | Open / close the hologram **Overlay Menu** |
| Shift+O | Toggle darkening of far overlays |
| G | Stays "drop target" |

### 2.3 Overlay Menu (Ctrl+O)

Hologram checkbox list that selects what O toggles: Icons (with per-category toggles: stars, planets, moons, dwarfs,
asteroids, rings, ships, targets), Planet Orbits, Asteroid Orbits, Moon Orbits, Other Orbits, Ships on Radar,
Darkening, and any future overlay (the list is data-driven so adding one is one table row). Saved in Settings (written at
menu close, SAVE_VERSION bump + migration). Defaults: everything ON except Asteroid Orbits. Overlays are on at flight
start. Also reachable from the Settings screen.

### 2.4 Interaction menus

- Flight: with the cursor free (steering off), hovering an icon highlights it; left press-drag-release opens a radial menu
  at the icon; drag toward an action and release to confirm; Esc or release in the centre cancels. Left mouse still fires
  when the cursor is not on an icon.
- Map: a list menu (same action table).
- Action table: object type -> actions, generic so fleet orders and NPC orders plug in later. Actions in v1: Target,
  Info, Move To (auto flight), Follow / Orbit (autopilot station keeping), Jump To, Match speed, and for dropped
  targets: Target, Info, Attack. Unavailable entries are shown disabled with a reason; Communications / Trade appear as
  disabled placeholders.
- **Chase Down** (ships / moving targets) is a separate ticket (#32): the entry appears greyed "not yet available" until
  it lands.

### 2.5 Steering and cursor

- Setting "Mouse steering at start: On / Off" (default Off) and the snap-to-centre fix are in #31 (HUD polish). With
  steering off the cursor is the hologram pointer over icons.
- Hologram mouse pointer replaces the Windows arrow everywhere in game: flight, map, overlay menu, radial menu, pause and
  in-game menus and the main menu. Software drawn through the HUD renderer in the HUD palette, respecting the effects
  slider; the hardware cursor is hidden.

### 2.6 Other

- The green marker the user asked about is the prograde marker (red = retrograde); player documentation of every
  control and GUI element is issue #33.
- No rectangles around hologram panels (outline-free soft panels) is part of #31.
- The earlier statement that menus stay unchanged is superseded: the user wants all in-game menus as holograms; tracked as issue #42.

### 2.7 Follow-up (issue #34)

Overlay profiles (5, vertical tabs in the Overlay Menu, RShift+O cycles) are a separate later ticket. #22 stores the
overlay choices as one profile in a data structure that can hold several (profile 1 = the defaults above).

## 3. Open items for the implementation notes

- Alt+O versus Left Alt free look (a chord with Alt must not re-orient the camera).
- Hit-testing icons with a free cursor versus the existing Fire-on-left-mouse behaviour.
- Icon shape artwork (vector shapes drawn by the shape renderer, no textures).
- The action table's availability rules and the disabled-reason texts.

## 4. Test plan

Pure-logic tests (icon selection and budgeting, edge pinning, fade and darkening math, action-table availability, overlay
preferences and master toggle memory, hit-testing, radial angle to action mapping, settings migration) authored by a
separate subagent from the contract; smokes for icons, overlay menu and radial menu; screenshots; full suite; adversarial
review.

## Amendment 1: Part 1 as built (icons, O keys, Overlay Menu, hologram cursor)

Plan: [`Docs/Plans/22-overlay-icons-and-interaction-menus-plan.md`](../Plans/22-overlay-icons-and-interaction-menus-plan.md).
Part 2 (radial / list menus, action table) is still to do; #22 stays open.

**Overlay model (`UI/SOLOverlayModel`, pure, test-first).** A data-driven table (`EOverlay` + one `FOverlayDef` row:
id, label, default, parent): Icons with its eight category rows (parent Icons), Planet / Asteroid / Moon / Other
Orbits, Ships on Radar, Darkening. A profile is a bit mask over the table; `FOverlayPrefs` holds up to
`SOL::Overlay::MAX_PROFILES` (5, for #34) masks, a count (1 in #22) and the active one. `IsShown` = master O x row x
parent row, so O off and on again restores exactly what showed (nothing is copied or lost). Orbit rows also need
the Alt+O switch (`IsOrbitShown`). Other Orbits are the dwarf planets' lines and the transient line of a target
without a permanent line; the targeted body's own line shows whatever its row while the master is on.

**Settings.** `FSettingsValues::OverlayPrefs` (sanitized and compared by the model), `SetAppliedOverlayPrefs` (the
menu and Shift+O apply at once, like the orbit-line flag). Schema 7: `FSOLOverlayProfileSave` (row id -> checked,
so a future row takes its default) per profile plus the active index; 6 -> 7 adds one default profile. The overlay
prefs join the "owed write" test (compared with what is on disk), and the Overlay Menu's close is a write point
(`FlushSave`, async, only if something differs). The master switch is session state, set on at every flight start
(pawn `BeginPlay`).

**Keys.** `FBindingDef::bShiftChord` became `EKeyChord Chord` (None, Shift, Alt, Control): new bindings
OverlayMaster (O), OverlayMenu (Ctrl+O), ToggleDarkening (Shift+O); ToggleOrbitLines is now Alt+O. Chords refuse
every modifier key and the mouse buttons; bindings conflict only with the same chord; shown keys get "Shift+",
"Alt+" or "Ctrl+". A rebound ToggleOrbitLines key keeps its override (the same action, now pressed with Alt; an
override an Alt chord cannot take falls back to Alt+O). `KEY_BINDING_LAYOUT_VERSION` 7, so a pre-#22 save gets the
one-time controls-changed notice. The pawn maps `IA_AltChord` (Left / Right Alt) and `IA_CtrlChord` (Left / Right
Ctrl) as chord actions next to `IA_ShiftChord`; Enhanced Input's chord blockers keep plain O silent while a modifier
is held.

- **Alt+O vs free look (open item resolved).** Free look no longer starts on the Alt press: it arms after
  `SOL::Overlay::FREE_LOOK_ARM_S` (0.2 s) held or `FREE_LOOK_ARM_PX` (8 px) of mouse travel while held (never while
  a panel, the map or the menu has the input), so a quick Alt+O does not turn the camera, flip the cursor capture or
  turn the joystick yellow; Alt+O also cancels an armed free look until Alt is released. A big mouse sweep during
  the chord can still arm it.
- **Ctrl / Shift as thrust keys.** Left Ctrl is thrust down and Left Shift thrust up: in AUTO, input held only on
  modifier keys (Shift, Ctrl, Alt) waits `SHIFT_CHORD_GRACE_S` before it counts (the Shift+M rule of SDD 20 widened).
  In assisted or manual flight a chord's modifier still thrusts for the frames before the letter, as with Shift+M;
  Ctrl+O then suspends the ship's input while the menu is open.

**Icons (`UI/SOLOverlayIcons` pure, test-first; `UI/SOLOverlayIconLayer`).** Categories from the registry parent
(`ClassifyBody`: no parent = star; a child of the Sun is a planet up to index 8, else a dwarf; any other = moon), the
50 largest named asteroids (the ones with orbit lines), ring systems (an ellipse around the ringed planet's icon),
dropped targets (`USOLCombatSubsystem`, nearest 16) and NPC ships (`USOLNpcSubsystem::GetCpuShips`, nearest 24 by a
bounded insertion, `SelectBudgeted`; the same choice feeds the radar when Ships on Radar is on). NPCs are not
targetable yet, so "selected first" applies to nothing today (the selection function supports it). Artwork (vector
shapes only, at most 4 per icon): star = ring crossed by two rays (yellow), planet = ring, moon = smaller ring,
dwarf = diamond, asteroid = triangle, ring system = flat ellipse, ship = open chevron (green), target = square
(orange). Brightness = `SizeFade` (1 up to an apparent radius of 6 px, smoothstep to 0 at 30 px) x
`DistanceDarkening` (with Darkening on: 1 up to 1e7 m, 0.25 from 1e12 m, linear in log10). Icons are drawn nearest
first and an icon within 0.9 radii of one already drawn is skipped (moons on a far planet); at most 192 icons.
Off-screen objects get an edge chevron (`PinToScreen`: along the camera-local right/up direction to the screen
edge shrunk by 18 px), the nearest 24. Everything goes into the HUD's one shape batch; nothing allocates per frame
(buffers reserved in `Initialize`; the NPC distance scratch grows only with the population). The map's plain discs
are replaced by the same icons in the batch (LOD alpha as glow); the Canvas discs remain the `sol.HudRenderer 0`
fallback. Icons are not shown while the HUD is hidden, the menu shows or a jump warp runs (like the rest of the HUD).

**Overlay Menu (`USOLOverlayMenuWidget`).** A C++ UMG list (no blueprint) built from the table: "[x] Label" rows in
mono, the categories indented, Primary-tinted text on a dark translucent panel at the screen's left, the
highlighted row in Secondary, unchecked or gated rows dim. Mouse: hover highlights, left click toggles (hit test on
each row's cached geometry). Keyboard: Up / Down, Space / Enter, Esc or Ctrl+<key> closes. Opened by the pawn like
the F3 panel (ship input suspended, UI-only focus). The Settings page embeds the same widget under "Overlays";
there a click applies at once (not part of Apply / Revert) and is written at the page's EndVisit.

**Hologram cursor (`USOLHologramCursorSubsystem`, `SSOLHologramCursor`).** A world subsystem installs one Slate
leaf widget as the game viewport's software cursor for every cursor type at `OnWorldBeginPlay` and removes it in
`Deinitialize`; Slate then hides the hardware arrow and draws the widget centred on the pointer, so the arrow's tip
(`SOLHologramCursor::ArrowSegments`, test-first) sits at the widget centre and clicks land at the tip. The arrow is
four glow lines (18 px, Primary, intensity 0.9) in one custom-vertex draw with its own `M_SOLHudShape` instance
(effects strength pushed when Settings changes). It covers flight with a free cursor, the map, the Overlay Menu,
F3, the pause / main menus and Settings (all in the game viewport). Screenshots do not capture the software cursor,
so the smoke checks it is installed for the default cursor type; its look needs a hands-on check.

**Verification.** Tests: 33 new (OverlayModel 8, OverlayIcons 13, HologramCursor 2, KeyBindings 5, SettingsRules 3,
SettingsModel 2) plus updated expectations (binding count 38, layout version 7, save schema 7, HUD hint
"ORBITS OFF (Alt+O)"), written from the contract by a separate agent, red on stubs (40 failing), then green. Full
suite 1118 / 1118. `-SOLSmokeOverlay` 11/11 at 1280x720 and 1000x800 (start on with icons and the cursor installed;
O off: 0 icons, 0 arrows, 0 orbit lines; O on: same icon count; Alt+O twice: lines off / on, master and free look
untouched; Shift+O twice; Ctrl+O: menu with 15 rows; a Slate click on Planets toggles it; Esc closes with exactly
one settings write). `-SOLSmokeHud` now toggles the orbit lines with Alt+O. Screenshots looked at
(`OverlaySmoke_Start/Menu/Icons.png`): the first run drew every named asteroid (more than 50) as a field of
triangles; limited to the 50 with orbit lines.

**Cost (`-game -SOLNoMenu` start view near Earth facing the Sun, 1280x720, editor build).** GPU (ProfileGPU,
`r.ProfileGPU.ShowUI 1`, one frame): the shape batch `MID_M_SOLHudShape_0` 0.039 ms for 152 shapes (304
triangles; phase C 0.034 ms for 69), the whole Slate UI pass 0.086 ms (46 draws). Game thread (`stat dumpave
-num=240`): `HUD Time` 0.110 ms (phase C single frame 0.066 ms), `Game UI Paint` 0.073 ms. Per frame the icon layer
places ~110 candidates (registry bodies, 50 asteroid Kepler solves, targets, ships) and draws at most 192 icons x 4
shapes + 24 arrows x 2 = 816 shapes of the 4096 batch.

**Adversarial review (opus, no Blockers; fixed).** High: free look could arm under a panel, the map or the menu (the
arming now waits for the input to be the ship's, and `SuspendShipControl` resets the held, suppressed and timer
state). Medium: the transient target line now follows the Other Orbits row; the menu hint is rebuilt at every open
(rebinds) and the Settings copy says "click a row" (it takes no keys); an edge arrow fades with its body's apparent
size (no arrow to a planet that fills the view); free look arms on 8 px of mouse travel, not any delta; Alt chords
refuse F4, Tab, Space and Enter (Windows' own Alt combinations, so a migrated Alt+F4 falls back to Alt+O); the NPC
ranking uses squared distances. Low: ring icons counted only when drawn (they need the Planets row); the icon and
radar-ship counts are cleared at the top of `DrawHUD`; the smoke checks that the cursor paints; prose and comments.
Not fixed, in `Docs/TECH_DEBT.md`: `SelectShips` unprofiled at the 20k-ship worst case; the cursor's fixed size.
Suite 1118 / 1118 after the fixes.
