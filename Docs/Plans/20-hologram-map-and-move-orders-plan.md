<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 20: Hologram map and move orders

Design: `Docs/SDDs/20-hologram-map-and-move-orders.md`. Tick steps as they are done.

- [ ] 0. Issue #25 (map alignment and shimmer) fixed and pushed as its own commit (agent running).
- [ ] 1. Key scheme: `OpenMap` / `OpenMapJump` (Shift required), `Resume`, route toggle on P (flight), map-mode keys,
  Ctrl+right click, Delete; `SOLKeyBindings`, settings SAVE_VERSION bump and migration; menu text.
- [ ] 2. Pure route logic (test-first): `SOLRoute` model, waypoint resolution, patrol Loop/Ping-pong sequencing,
  `SOLRoutePlanner` corner speed from turn angle and G budget, capture radius, progress/ETA.
- [ ] 3. Map mode state (Jump/Move/Patrol), draft route editing, Esc/close behaviour.
- [ ] 4. Route subsystem driving the autopilot through waypoints; pause on manual input, resume; Move end at rest.
- [ ] 5. Route overlay (map and flight view), styles from SDD 2.4; P toggle; HUD marker with route ETA.
- [ ] 6. Hologram look: `SOL::Hologram` constants, proxy body materials, ecliptic grid, stems, map post-process
  (`Tools/Map/create_hologram_assets.py`).
- [ ] 7. Docs: GAME_MECHANICS, ARCHITECTURE, in-game Controls/Info, ToDo files, SDD amendments.
- [ ] 8. Adversarial opus review, fixes, full suite (baseline 959 + new), smokes, commit and push, close #20 (and #25).

Notes: split into sub-agents per ~2-3 steps (context under 35%). Tests are authored by a separate subagent from the
contract only.
