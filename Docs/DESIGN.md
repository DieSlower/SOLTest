# SOLTest — Design Decisions & Roadmap

Solar-system space sim, UE 5.8, C++ first. This file is the source of truth for cross-cutting decisions; each part's subagent reads it cold.

## Decisions (resolved with the user)

| Area | Decision |
|---|---|
| Scale | True 1:1 scale. Authoritative positions in our own double-precision "universe coordinates" (meters). Rendering is camera-relative. Far bodies drawn with angular-size-preserving projection. |
| Origin rebasing | Rebase the UE world origin to the body nearest the ship (use UE's built-in origin rebasing). Hysteresis: switch anchor to a new body only when the ship's distance to it is < 25% of its distance to the current anchor / second-closest body, to avoid thrashing. (Interpretation of the user's wording; confirm if behavior feels wrong.) |
| Orbits | Analytic Keplerian from real orbital elements (JPL-style, epoch J2000), moons relative to parent. Asteroids and ring particles also Keplerian. No planetary perturbations. |
| Ship gravity | Ship is pulled by nearby body gravity. |
| Flight model | Flight-assist by default (WASD/mouse set target velocity relative to a reference frame, speed cap settable: match a planet's speed or typed value; thrusters cancel drift and gravity). Toggle assist off for pure Newtonian. |
| Time | Sim clock starts at the real current date, 1x, with adjustable time-warp for bodies. Ship physics always real-time. |
| Jump (J map) | Zoomable map -> ~2-4 s animated warp (FX + sound) -> arrive with velocity matched to the nearest body; re-anchor rebased origin on arrival. |
| Level (L) | Toggle surface-lock. Range: within 1000 m of a body's surface. Smooth alignment takes 5 s so body center becomes "down", then keeps following curvature. L again, or climbing above ~1000 m (with hysteresis/warning), releases. No body in range: HUD hint only. |
| Star field | Custom ship-centric, camera-locked renderer. AT-HYG catalog (2.55M stars). Bright stars (~mag < 8) as GPU sprites; faint stars baked offline into an HDR cubemap. Reuse CelestialVault plugin's Milky Way texture/material functions (its Earth/DaySequence actor is NOT used). |
| Combat | Data-driven projectile bolts (not hitscan, not Actors): arrays + swept traces, Niagara rendering, ship velocity inherited. Player drops targets; health/shields, hit flashes, Chaos/Niagara destruction, explosion VFX + sound. |
| Scalability | Full MassEntity for everything including ships. The player's Pawn is a thin actor for input/camera bridged to its Mass entity. Data-oriented body registry. |
| Content | Procedural/placeholder first; real assets (Fab, D:\MediaLib, NASA/Solar System Scope textures) swapped in per part. |
| Surfaces | Textured spheres + analytic sphere collision now; procedural cube-sphere LOD terrain (PCG/Nanite) in a later part. |
| Budget | On a usage-limit error: save resume state, read reset time from the error (fallback +5h10m / +7d10m), schedule a one-shot CronCreate resume. Each part's subagent writes a short progress file. No fake usage-poller agent. |

## Process rules

- One new subagent per part, keeping context under ~25%; spawn a fresh one if exceeded.
- Each part is small, self-tested by me (build, automation tests, PIE via MCP, screenshots). The user tests only the final product.
- Use the AskUserQuestion GUI tool for every question to the user.
- unreal-mcp only works while the Editor is open: build SOLTestEditor, launch UnrealEditor.exe with SOLTest.uproject, then run `/mcp` to reconnect (see memory note).
- Avoid deprecated APIs; prefer Enhanced Input, Mass, Niagara, Nanite, PCG, Chaos.

## Roadmap (each part a separate testable deliverable)

1. Foundations + flight scaffold: modules/plugins, universe coordinates, origin rebasing, sim clock, Kepler body registry, Sun + 8 planets (textured spheres), Mass ship entity + Pawn proxy, flight-assist/Newtonian, gravity, 3rd-person camera, basic HUD, speed control.
2. Jump map (J) with zoom + warp + velocity-matched arrival.
3. Level/surface-lock (L).
4. Star field (AT-HYG pipeline, hybrid renderer, Milky Way).
5. Moons, dwarf planets, asteroid belt (Mass), planet rings.
6. Weapons, target drops, destruction VFX/audio.
7. HUD/menus, controls + info/about screens, settings.
8. Art/audio pass (real ship model, textures, sounds).
9. Procedural terrain + atmospheres (PCG/Nanite).
10. Scale/perf demo (NPC ships in Mass, stress test toward 1M).
