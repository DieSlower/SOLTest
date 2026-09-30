<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Jump Map — Implementation Plan

**Goal:** Deliver Part 2 (issue #3): open a navigable 3D map with J, pick a destination (body or free point) with the two-stage drag gesture, and jump there with a warp animation, arriving matched to the destination's reference.

**SDD of record:** [`Docs/SDDs/3-jump-map.md`](../SDDs/3-jump-map.md). Keep it in sync if the design shifts.

## Global constraints

- Follow `CLAUDE.md` and `Docs/STYLE_GUIDE.md`.
- Same loop as Part 1: tests-first for pure logic (a separate subagent, from the contract only), implement to green, adversarial review, fix, re-verify, docs, commit (`Issue #3: ...`), no push.
- The Unreal Editor must be closed while building; reopen for PIE/screenshot checks, `/mcp` reconnect if unreal-mcp is needed.

## Steps

- [x] **2a — Pure picking/math logic.** `Map/SOLMapPicking.h/.cpp` (ray-plane intersection, planar decomposition, destination composition, body-under-ray picking) per Appendix D. Automation tests, adversarial review, commit.
- [x] **2b — Map camera math (pure logic; the mode/mouse/input wiring is separate).** `Map/SOLMapCamera.h/.cpp` per Appendix E. Automation tests, adversarial review, commit.
- [x] **2b-2 — Map mode and camera actor.** (Built: `Map/SOLMapModeSubsystem`, pawn J/map input, anchor render-viewpoint override, `-SOLSmokeMap` 12/12 PASS (after the 2026-09-30 button rebind: left drag no-op, right-drag orbit, middle and Shift+right pan), screenshots; adversarial review and commit still pending in the main session.) J opens the map (releases the mouse cursor, suppresses ship control input the way F3 does, keeps sim time advancing), a dedicated map camera driven by `FSOLOrbitCameraState`/`SOLMapCamera`, mouse-drag-to-orbit/pan input wiring (pixel-to-radian/meter scaling), mouse-wheel zoom, Esc/J closes it and returns input to the ship. PIE smoke check, screenshots, adversarial review, commit.
- [ ] **2c — Body representation at zoom.** Mesh-to-icon cross-fade, billboarded name labels, pick radius per body reusing `PickBodyUnderRay`. Screenshots at several zoom levels, adversarial review, commit.
- [ ] **2d — Destination picking UI.** The disc visualization, drag-to-lock XY, Shift-drag-to-lock height, destination marker, X to clear, wired to `SOLMapPicking`. Scripted input smoke test (same `FInputKeyEventArgs::CreateSimulated` approach as Part 1), screenshots, adversarial review, commit.
- [ ] **2e — Jump execution.** Enter triggers the warp animation (FX/sound), arrival matched to the destination's reference body's velocity (reuse the anchor/rebase logic from Part 1), origin re-anchored, map closes. Scripted smoke test, adversarial review, commit.

## Cross-cutting

- [ ] Update `Docs/GAME_MECHANICS.md` with the final key bindings, pick radii, warp duration, and any tuned constants.
