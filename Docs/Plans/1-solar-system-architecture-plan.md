<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Solar-System Architecture — Roadmap Plan

**Goal:** Deliver the SOL space sim as a sequence of small, independently testable parts, each built by its own subagent.

**SDD of record:** [`Docs/SDDs/1-solar-system-architecture.md`](../SDDs/1-solar-system-architecture.md). Keep it in sync if the design shifts during implementation.

## Global constraints

- Follow [`CLAUDE.md`](../../CLAUDE.md): grill-me before each part's SDD, SDD and plan on disk before code, TDD with tests authored by a separate subagent, fresh adversarial performance review after code, full test run before a part is called done.
- Follow [`Docs/STYLE_GUIDE.md`](../STYLE_GUIDE.md).
- One new subagent per part; keep its context under ~25% and start a fresh one if it is exceeded. Choose the subagent model per the model-selection rules in `CLAUDE.md`.
- Each part is small enough to be verified by us (build, automation tests, PIE checks) without the user; the user tests only the finished product.
- Each part gets its own GitHub ticket; its SDD and plan are prefixed with that ticket number.
- On a usage-limit error: save resume state, read the reset time from the error (fallback +5h10m for the 5-hour window, +7d10m for the weekly one), and schedule a one-shot `CronCreate` resume, never past a pending user question.

## Roadmap

- [ ] **Part 1 (#2) — Foundations and flight scaffold.** Modules/plugins, universe coordinates, origin rebasing, sim clock, Kepler body registry, Sun and 8 planets (textured spheres), Mass ship entity with Pawn proxy, flight-assist/Newtonian, gravity, third-person camera, basic HUD, speed control.
  - [ ] 1a — Universe types, Kepler solver, sim clock, body registry, anchor manager, body visuals, test map, automation tests. *Status: paused; only `Source/SOLTest/Universe/SOLTypes.h` and `SOLKepler.h` exist (uncommitted, untested).*
  - [ ] 1b — Mass ship entity, Pawn proxy, flight model, gravity, camera, HUD.
- [ ] **Part 2 (#3) — Jump map (J)** with zoom, warp, and velocity-matched arrival.
- [ ] **Part 3 (#4) — Level / surface-lock (L).**
- [ ] **Part 4 (#5) — Star field** (AT-HYG pipeline, hybrid renderer, Milky Way).
- [ ] **Part 5 (#6) — Moons, dwarf planets, asteroid belt (Mass), planet rings.**
- [ ] **Part 6 (#7) — Weapons, target drops, destruction VFX and audio.**
- [ ] **Part 7 (#8) — HUD/menus, controls and info/about screens, settings.**
- [ ] **Part 8 (#9) — Art and audio pass** (real ship model, textures, sounds).
- [ ] **Part 9 (#10) — Procedural terrain and atmospheres** (PCG/Nanite).
- [ ] **Part 10 (#11) — Scale and performance demo** (NPC ships in Mass, stress test toward 1M).

## Cross-cutting tooling

- [x] `Tools/RunTests.ps1` + `Tools/RunTests.bat` command-line test runner (see `CLAUDE.md` Testing). Verified 2026-09-28 against the engine's `System.Core.Math` tests; SOLTest has no tests of its own yet.
- [x] GitHub tickets created 2026-09-28: #1 architecture and roadmap, Parts 1-10 as #2-#11 (Part N is issue #N+1). SDD/Plan files for a part use its issue number.
