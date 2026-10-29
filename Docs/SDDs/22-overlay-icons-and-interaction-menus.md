<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 22: Overlay icons, overlay menu and interaction menus

Issue #22. Depends on SDD 21 (hologram HUD renderer), SDD 20 (map, routes, `SOL::Hologram`), SDD 19 (steering gate),
SDD 17 (orbit lines). Status: design agreed (grill-me complete), not implemented. Issue #31 (HUD polish) lands first.

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
