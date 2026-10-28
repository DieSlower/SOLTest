<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 20: Hologram map and move orders

Design: `Docs/SDDs/20-hologram-map-and-move-orders.md`. Tick steps as they are done.

- [x] 0. Issue #25 (map alignment and shimmer) fixed and pushed as its own commit (commit 3a7de13).
- [x] 1. Key scheme: `OpenMap` / `OpenMapJump` (Shift required), `Resume`, route toggle on P (flight), map-mode keys,
  Ctrl+right click, Delete; `SOLKeyBindings`, settings SAVE_VERSION bump and migration; menu text.
- [x] 2. Pure route logic (test-first): `SOLRoute` model, waypoint resolution, patrol Loop/Ping-pong sequencing,
  `SOLRoutePlanner` corner speed from turn angle and G budget, capture radius, progress/ETA.
- [x] 3. Map mode state (Jump/Move/Patrol), draft route editing, Esc/close behaviour.
- [x] 4. Route subsystem driving the autopilot through waypoints; pause on manual input, resume; Move end at rest.
- [x] 5. Route overlay (map and flight view), styles from SDD 2.4; P toggle; HUD marker with route ETA.
- [x] 6. Hologram look: `SOL::Hologram` constants, proxy body materials, ecliptic grid, stems, map post-process
  (`Tools/Map/create_hologram_assets.py`).
- [x] 7. Docs: GAME_MECHANICS, ARCHITECTURE, in-game Controls/Info, ToDo files, SDD amendments.
- [x] 8. Adversarial opus review, fixes, full suite (baseline 959 + new), smokes, commit and push, close #20 (and #25).

Progress (2026-10-28): steps 1-4 done (SDD 20 Amendment 1): suite 990/990 in the editor, `-SOLSmokeRoute` 26/26;
`-SOLSmokeMap`, `-SOLSmokeMapPick`, `-SOLSmokeInput`, `-SOLSmokeHud` and `-SOLSmokeMapAlign` all PASS on the new keys;
`-SOLMenuSmoke=all` 49/50 and `-SOLSmokeJump` 13/14 (both failures outside this change, see the amendment). Step 7's
GAME_MECHANICS / ARCHITECTURE / in-game text updates for steps 1-4 are done; the overlay parts remain with step 5.

Progress (2026-10-28, step 5): route overlay and HUD route marker built (SDD 20 Amendment 2); suite 1000/1000 in the
editor (10 new `SOLTest.RouteOverlayMath` tests), `-SOLSmokeRouteOverlay` 25/25 at 1280x720 (screenshots checked in
flight, map and with a draft). Next: step 6 (hologram look).

Notes: split into sub-agents per ~2-3 steps (context under 35%). Tests are authored by a separate subagent from the
contract only.

Progress (2026-10-28, steps 6-8): hologram look built (SDD 20 Amendment 3): suite 1012/1012 in the editor (12 new
`SOLTest.HologramMath`), `-SOLSmokeHologram` 12/12 at three aspect ratios; docs updated; adversarial review findings
fixed (suite re-run 1012/1012, hologram smoke 12/12 after the fixes); not yet committed, issue #20 still open.
