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
  for the new `Level/` module. Commit pending in the main session.)
- [ ] **3b — Engine integration.** `IA_ShipLevel` bound to `L`
  (`OnToggleLevelAction`/`HandleToggleLevel` in `ASOLShipPawn`, suppressed while the
  map/speed-panel/jump-warp has input), `FSOLSurfaceLockState` owned and stepped by
  `USOLShipSubsystem` each frame (using `USOLAnchorSubsystem::FindNearestBody` and the
  locked body's live registry position), `USOLTargetingSubsystem::LockToBodyIndex` for
  the reference-frame auto-match, `ApplyAlignmentCorrection` applied after
  `SOLFlight::Step` each ship step, and the HUD status/hint/warning line in
  `SOLFlightHud` (reusing `SOLHudFormat`). `-SOLSmokeLevel` scripted smoke test
  (approach-and-auto-lock, climb-and-release with warning, manual engage from beyond
  10 km, instant manual release), screenshots, adversarial review, commit.

## Cross-cutting

- [ ] Update `Docs/GAME_MECHANICS.md` with the `L` binding, the 10 km / 12.5 km and
  manual-range numbers, and the roll time constant.
- [ ] Update the in-game Controls/Info screen text for `L` to match.
- [ ] Update `Docs/ARCHITECTURE.md` if a new subsystem-level data flow or `-SOL*` flag
  needs it (the module map already includes `Ship/`, `Targeting/`, `UI/`; this adds a
  `Level/` pure-logic module in the same style as `Flight/`).
