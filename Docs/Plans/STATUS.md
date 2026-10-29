<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Project status: tickets and remaining work

Living document. **Update it whenever a ticket is added, started, finished or re-scoped** (rule in `CLAUDE.md`).
Last updated: 2026-10-29.

Sizes are rough: S = a day of agent work or less, M = a few sessions, L = a large multi-session feature.

## Done

| Issue | What | SDD |
|---|---|---|
| #2 | Part 1: foundations and flight scaffold | 1, 2 |
| #3 | Part 2: jump map | 3 |
| #4 | Part 3: surface-lock | 4 |
| #5 | Part 4: star field (AT-HYG) | 5 |
| #6 | Part 5: moons, dwarf planets, asteroid belt, rings | 6 |
| #7 | Part 6: weapons, target drops, destruction | 7 |
| #8 | Part 7: HUD, menus, controls, info, settings | 8 |
| #11 | Part 10: scale and performance demo (1M NPC ships) | 11 |
| #12 | Planet axial rotation | 12 |
| #13 | Star field quality | 13 |
| #14 | Gas giant fidelity | 14 |
| #15 | Dropped target blur | 7 (amendment) |
| #16 | Ring shadows | 16 |
| #17 | Orbit lines | 17 |
| #18 | Newtonian flight core, G-force pilot model, autopilot, NPC motion | 18 |
| #19 | Three flight modes and controls | 19 |
| #20 | Hologram map, Move/Patrol routes, route overlay, map hologram look | 20 |
| #28 | HUD phase A: event bus, batched Slate renderer, shape material, effects slider | 21 |
| #29 | HUD phase B: every flight HUD element on the glow renderer, hologram panels, glow text | 21 |
| #30 | HUD phase C: 3D hologram radar (ship-horizontal plane, above/below colours, sonar rings) | 21 |
| #21 | HUD hologram rendering (umbrella, phases A-C) | 21 |
| #25 | Map alignment and shimmer (incl. date-dependent smoke fix) | 25 |
| #31 | HUD polish: half glow, Alt yellow circles, borderless panels, steering start setting and hint, stick recenter on release | 19, 21 (amendments) |

## In progress

| Issue | What | Notes |
|---|---|---|

## To do (ordered)

| Order | Issue | What | Size | Depends on |
|---|---|---|---|---|
| 1 | #22 | Overlay icons, Overlay Menu (Ctrl+O), O/Alt+O/Shift+O keys, radial and list interaction menus, hologram cursor (SDD 22 written) | L | #31 |
| 2 | #32 | Chase Down autopilot mode (exceeds 1.5 G, G-meter bursts) | M | #22 |
| 2b | #34 | Overlay profiles in the Ctrl+O menu (5 profiles, RShift+O cycles) | M | #22 |
| 3 | #33 | Player documentation: controls with keyboard pictures and a GUI glossary | M | #22 |
| 4 | #23 | Orbit line quality (noise where a line enters a planet) | S-M | none |
| 5 | #27 | Ship jitters slightly at rest | M | #18 |
| 6 | #26 | Anti-aliasing evaluation (TSR vs alternatives), report then change | M | none |
| 7 | #24 | Decide each of the 120 Limit Theory catalogue features, split adopted ones into tickets | M (user decisions) | none |
| 8 | #10 | Part 9: procedural terrain and atmospheres | L | #2, #4 |
| 9 | #9 | Part 8: art and audio pass | L | #2-#8 |
| - | #1 | Umbrella: architecture and roadmap, closes when the above is done | - | all |

Order is a proposal; the user can reorder at any time. #24 produces new tickets, so the total will grow.

## Not yet ticketed (`Docs/ToDo/`)

Picked up as tickets when scheduled; each needs grill-me first.

- `sun-glow-and-flare.md`: bigger Sun glow and lens flare
- `adaptive-body-tessellation.md`: finer sphere tessellation when close
- `minor-body-targeting.md`
- `ship-tech-upgrades.md`: G tolerance, thrust and other upgrades
- `damage-and-death.md`: damage system (G-LOC death, planet-impact G)
- `predicted-path.md`: ballistic path under gravity (P key now toggles the route overlay)
- `cockpit-3d-screens.md`: HUD on in-world cockpit screens
- `supercruise-travel.md`, `planetary-perturbations.md`, `surface-landing.md`, `star-diffraction-spikes.md`,
  `accurate-pole-directions.md`, `asteroid-ring-collision.md`

## Known follow-ups and checks (not tickets)

- Hands-on PIE checks the user needs to do: Left Shift vs Shift+M chord in Auto, shimmer gone in the map, free-cursor
  behaviour, map-close key repeat.
- Failing or unverified smokes: `-SOLMenuSmoke=all` 49/50 ("Thrust flies the ship" at Mars), `-SOLSmokeJump` 13/14
  (arrives at Deimos not Mars).
- Tech debt list: `CLAUDE.md` "Known tech debt to revisit" (M2 Mid-to-Far pop, packaged 1M run, render-thread stalls,
  orbit-line memory, and others).

## How much is left

About 5 tickets (#22, #23, #26, #27, #24 decisions) before the two large roadmap items (#10, #9),
plus whatever #24 adopts. The ToDo list above is the backlog beyond that.
