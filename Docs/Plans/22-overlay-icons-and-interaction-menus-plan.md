<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 22: overlay icons, Overlay Menu, interaction menus, hologram cursor

SDD: [`Docs/SDDs/22-overlay-icons-and-interaction-menus.md`](../SDDs/22-overlay-icons-and-interaction-menus.md).
Two parts, each built by its own agent. Living document: tick steps as they land.

## Part 1: icons, O keys, Overlay Menu, hologram cursor

Pure logic is test-first (tests written from the contract by a separate opus agent, red on stubs, then green).

1. [x] Overlay model `UI/SOLOverlayModel` (data-driven toggle table, master O, per-profile masks with room for 5
   profiles (#34), profile 1 = defaults: all on except Asteroid Orbits; sanitize, equality, save conversion).
2. [x] Settings: `FSettingsValues::OverlayPrefs`, `SetAppliedOverlayPrefs`, save schema 6 -> 7
   (`FSOLOverlayProfileSave` list, migration fills the defaults), write at Overlay Menu close only when changed.
3. [x] Key bindings: chord kind per binding (None / Shift / Alt / Ctrl), new OverlayMaster (O), OverlayMenu (Ctrl+O),
   ToggleDarkening (Shift+O); ToggleOrbitLines becomes Alt+O (a rebound key stays with the action, now Alt+key);
   layout version 7 (controls-changed notice). Pawn: Alt / Ctrl chord actions (Enhanced Input chord blockers keep
   plain O silent while a modifier is held), Alt+O ends and suppresses free look until Alt is released, Ctrl / Shift
   only input in AUTO waits the chord grace (as Shift+M, SDD 20).
4. [x] Icon logic `UI/SOLOverlayIcons` (categories, body classification, size fade, distance darkening, edge pinning,
   budgeted selection, vector icon shapes) and the HUD icon layer (bodies, named asteroids, rings, dropped targets,
   NPC ships nearest-first with a hard cap), only while overlays are on; orbit categories drive the orbit lines; Ships
   on Radar gates NPC contacts on the radar.
5. [x] Map: the icon set replaces the plain discs (shape renderer; Canvas discs stay as the `sol.HudRenderer 0`
   fallback).
6. [x] Overlay Menu (Ctrl+O): hologram checkbox list (UMG, mouse and keyboard), from the toggle table; also reachable
   from the Settings screen.
7. [x] Hologram cursor: software cursor widget drawn with `M_SOLHudShape` (HUD palette, effects slider), hot spot at
   the arrow tip, registered on the game viewport for every cursor type.
8. [x] Texts: Controls rows, Info prose, `GAME_MECHANICS.md`, `ARCHITECTURE.md`, SDD 22 amendment, `STATUS.md`.
9. [x] Smoke `-SOLSmokeOverlay` (O / Alt+O / Ctrl+O / Shift+O, icon counts, cursor), screenshots at 1280x720 and
   1000x800, ProfileGPU cost in the SDD.
10. [x] Full suite, adversarial review, fixes, re-run suite and smokes (Hud, Input, Route, RouteOverlay, Map, MapPick,
    MapAlign, Hologram, Overlay), commit and push.

## Part 2: interaction menus (later agent)

Radial menu on icons in flight (press-drag-release), list menu on the map, generic action table (Target, Info, Move
To, Follow / Orbit, Jump To, Match speed; dropped targets: Target, Info, Attack; disabled entries with reasons, Chase
Down greyed until #32), icon hover and hit-testing versus Fire on left mouse.

## Progress notes

- 2026-10-29: Part 1 implemented; suite 1118/1118, `-SOLSmokeOverlay` 11/11 at both sizes. Step 10 in progress
  (adversarial review, smoke sweep, commit). Smoke sweep all green (Hud 24/24 x2, Input 41, Route 26, RouteOverlay
  25, Map 16, MapPick 22, Hologram 12, Overlay 11 x2, MapAlign 26). The first review agent was cut by a usage limit;
  re-dispatched. Next: apply review fixes, re-run suite + smokes, commit and push.
- 2026-11-01: resumed; review fixes verified (suite 1118/1118, every smoke green again), committed and pushed.
  Part 1 complete.
