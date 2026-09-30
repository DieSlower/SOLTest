<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 1 — Solar-system space sim: architecture and cross-cutting decisions

Ticket `#1` (to be created as "Architecture and roadmap" on GitHub; if the number differs, rename this file, the matching plan and the references to it). Plan of record: [`Docs/Plans/1-solar-system-architecture-plan.md`](../Plans/1-solar-system-architecture-plan.md).

This is the source of truth for the decisions that cut across every part of the game. Each part's own SDD refines it and must not contradict it without updating this document. Every decision below was resolved with the user during a `grill-me` session on 2026-09-27.

---

## 1. Problem

Build a solar-system simulation in Unreal Engine 5.8 (C++ first) where the player flies a small ship in third person, jumps between regions of SOL from a zoomable map, can level the ship to a planet or asteroid surface, and can shoot and destroy targets. Planets, moons, asteroids and rings follow realistic orbits. The whole thing must be built for massive scale (a future goal of ~1,000,000 animated ships or characters) using current UE features (Mass, Niagara, Nanite, PCG) and no deprecated APIs.

## 2. Decisions

| Area | Decision |
|---|---|
| Scale | True 1:1 scale. Authoritative positions live in our own double-precision "universe coordinates" (meters). Rendering is camera-relative. Far bodies are drawn with angular-size-preserving projection so they look correct. UE's Large World Coordinates only cover roughly ±88 million km, and Neptune is ~4.5 billion km from the Sun, so LWC alone is not enough. |
| Origin rebasing | Rebase rendering to the observer's own position, not the anchor body's (SDD 2 §3 has the final design: `FSOLRenderOrigin` snaps to the observer, not to UE's built-in `SetNewWorldOrigin`, whose int32-centimeter origin is too small for AU-scale anchors). The anchor itself still picks the body nearest the ship with hysteresis: switch to a new body only when the ship's distance to it is under 25% of its distance to the current anchor (or second-closest body), so the origin does not thrash at the midpoint. |
| Orbits | Analytic Keplerian from real orbital elements (JPL-style, epoch J2000); moons relative to their parent. Asteroids and ring particles are Keplerian too. No planetary perturbations. Deterministic and O(1) per body at any time, so time-warp and jumps are trivial and velocity is exact for speed-matching. |
| Ship gravity | The ship is pulled by nearby body gravity (bodies themselves stay on rails). |
| Flight model | Flight-assist by default: WASD and mouse set a target velocity relative to a reference frame, with a settable speed cap (match a planet's speed, or type a value); thrusters cancel drift and gravity. Toggle assist off for pure Newtonian flight. |
| Time | The sim clock starts at the real current date at 1x, with adjustable time-warp applied to bodies. Ship physics always runs in real time. |
| Jump (J map) | Zoomable map, then a ~2-4 s animated warp (FX and sound), then arrival with velocity matched to the nearest body. Arrival re-anchors the rebased origin. |
| Level (L) | Toggle surface-lock: an orientation-only aid, not an autopilot — full 6DoF flight-assist/Newtonian control is unaffected; a continuous minimal-rotation alignment (not a roll-only correction — see SDD 4 Amendment 1) corrects ship "up" toward directly away from the locked body's center (~1s time constant, visibly settles within ~5 s from a worst-case 180° start), so the body's center reads as "down" and the horizon keeps tracking as the ship flies. Auto-engages within 10 km of a body's surface (warns at 10 km, releases at 12.5 km climbing away); L can also manually engage it early, from up to `max(1000 km, body radius)` out, with the same warn/release hysteresis scaled to that range. L always manually releases instantly regardless of altitude. See SDD 4 (`4-surface-lock.md`) for the full design, including reference-frame and HUD details. |
| Star field | Custom ship-centric, camera-locked renderer using the AT-HYG catalog (2.55M stars). Bright stars (~mag < 8) are GPU sprites; faint stars are baked offline into an HDR cubemap. Reuses the CelestialVault plugin's Milky Way texture and material functions; its Earth/DaySequence actor is not used. See [`Docs/Research/STAR_FIELD_CELESTIALVAULT_AND_ATHYG.md`](../Research/STAR_FIELD_CELESTIALVAULT_AND_ATHYG.md). |
| Combat | Data-driven projectile bolts (not hitscan, not Actors): arrays plus swept traces, rendered through Niagara, ship velocity inherited. The player drops targets; targets have health and shields, hit flashes, Chaos/Niagara destruction, explosion VFX and sound. |
| Scalability | Full MassEntity for everything, including ships. The player's Pawn is a thin actor for input and camera, bridged to its Mass entity. A data-oriented body registry (structure-of-arrays) holds the bodies. |
| Content | Procedural/placeholder first; real assets (Fab, `D:\MediaLib`, NASA/Solar System Scope textures) are swapped in per part. |
| Surfaces | Textured spheres plus analytic sphere collision for now; procedural cube-sphere LOD terrain (PCG/Nanite) in a later part. |

## 3. Open questions

None deferred during the grill. Details that were intentionally left for each part's own grill: HUD layout and speed panel, map UI details, ship stats and weapon numbers, planet material/atmosphere specifics.

## 4. Revision history

- 2026-09-27: initial decisions from the `grill-me` session.
- 2026-09-28: split out of `Docs/DESIGN.md` into this SDD and its plan; process rules moved into `CLAUDE.md`.
- 2026-09-30: Level (L) row rewritten during the Part 3 (#4) grill: range raised from 1000 m to 10 km (auto) / up to `max(1000 km, radius)` (manual), and the mechanic detailed as an orientation-only auto-alignment rather than left unspecified. Full design in SDD 4. Planet axial rotation (spin/tilt) filed separately as issue #12, not part of Part 3.
