<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Level / Surface-Lock — Implementation Plan

**Goal:** Deliver Part 3 (issue #4): `L` toggles an orientation-only surface-lock that
auto-engages within 10 km of a body (or manually from farther out), continuously rolls
the ship's "up" away from the body's center, and releases with hysteresis and HUD
feedback — without touching the player's thrust or pitch/yaw control.

**SDD of record:** [`Docs/SDDs/4-surface-lock.md`](../SDDs/4-surface-lock.md). Keep it
in sync if the design shifts.

## Global constraints

- Follow `CLAUDE.md` and `Docs/STYLE_GUIDE.md`.
- Same loop as Parts 1-2: tests-first for pure logic (a separate subagent, from the
  contract only), implement to green, adversarial review, fix, re-verify, docs,
  commit (`Issue #4: ...`), no push.
- The Unreal Editor must be closed while building; reopen for PIE/screenshot checks.

## Steps

- [x] **3a — Pure surface-lock logic.** (Built: `Level/SOLSurfaceLock.h/.cpp` —
  `FSOLSurfaceLockParams`, `FSOLSurfaceLockState` (with the `bAutoSuppressed`/
  `SuppressedBodyIndex` latch added post-review), `ComputeManualRangeM`,
  `UpdateSurfaceLockState`, `TargetUpDir`, `ApplyAlignmentCorrection` (a minimal
  shortest-arc whole-orientation rotation, not the roll-only design first implemented —
  see SDD 4 Amendments). Constants in `SOLConstants.h`. 31 automation tests
  (`SurfaceLockTest.cpp`), 308/308 full suite green, non-unity build clean. Two
  adversarial review rounds: the first caught two real design bugs (both required a
  user decision, resolved and documented as SDD 4 Amendments 1-2) plus test-coverage
  gaps and a stale name; the second round, after the rework, found no correctness
  bugs, only additional test-hardening, all applied. `Docs/ARCHITECTURE.md` updated
  for the new `Level/` module. Commit `05e82fa`.)
- [x] **3b — Engine integration.** (Built: `IA_ShipLevel` bound to `L`
  (`OnToggleLevelAction`/`HandleToggleLevel` in `ASOLShipPawn`, dropped — not queued —
  while the map/speed-panel/jump-warp has input), `FSOLSurfaceLockState` owned and
  stepped by `USOLShipSubsystem::UpdateSurfaceLock` after the flight run each step (see
  SDD 4 "Implementation clarifications (3b)" for why altitude must be read post-step),
  `USOLTargetingSubsystem::LockToBodyIndex` for the reference-frame auto-match (replaces
  an existing `M` lock on anything else), `ApplyAlignmentCorrection` applied per substep
  in `USOLShipFlightProcessor` via `FSOLShipControlFragment::AlignBodyIndex` and
  `FSOLFlightParams::AlignTimeConstantS`, the HUD status/hint/warning line in
  `SOLFlightHud` (hidden while the jump map is open), and `ClearSurfaceLock` on jump
  arrival (`USOLJumpSubsystem::CompleteJump`). One adversarial review round found a real
  regression (surface-lock's alignment interfered with `-SOLSmokeFlight`'s pre-existing
  nose-down Collide test, 7/9) plus test-coverage gaps and nits — all fixed and
  re-verified: `-SOLSmokeFlight` 9/9 (restored), `-SOLSmokeLevel` 21/21 (hardened with
  release-stops-alignment, a pinned time-constant check, the M-lock-replaced case, and a
  scripted jump-arrival clear), `-SOLSmokeInput` 29/29, `-SOLSmokeHud` 17/17,
  `-SOLSmokeMap` 16/16, `-SOLSmokeMapPick` 21/21, `-SOLSmokeJump` 14/14, 308/308
  automation tests, all independently re-verified in the main session. Commit pending.)

## Cross-cutting

- [x] Update `Docs/GAME_MECHANICS.md` with the `L` binding, the 10 km / 12.5 km and
  manual-range numbers, and the roll time constant.
- [x] Update the in-game Controls/Info screen text for `L` to match (the HUD key-hint
  line is the in-game controls text; it now lists `L surface-lock`).
- [x] Update `Docs/ARCHITECTURE.md` if a new subsystem-level data flow or `-SOL*` flag
  needs it (the module map already includes `Ship/`, `Targeting/`, `UI/`; this adds a
  `Level/` pure-logic module in the same style as `Flight/`).
