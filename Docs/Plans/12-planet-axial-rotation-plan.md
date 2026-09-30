<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Planet Axial Rotation — Implementation Plan

**Goal:** Deliver issue #12: the Sun and 8 planets spin on their real (magnitude) axial
tilts at their real sidereal rates, visibly, with no change to gravity, collision,
targeting or surface-lock.

**SDD of record:** [`Docs/SDDs/12-planet-axial-rotation.md`](../SDDs/12-planet-axial-rotation.md).
Keep it in sync if the design shifts.

## Global constraints

- Follow `CLAUDE.md` and `Docs/STYLE_GUIDE.md`.
- Same loop as prior parts: tests-first for pure logic (a separate subagent, from the
  contract only), implement to green, adversarial review, fix, re-verify, docs,
  commit (`Issue #12: ...`), no push.
- The Unreal Editor must be closed while building; reopen for the screenshot check.

## Steps

- [x] **12a — Pure rotation math and registry extension.** (Built:
  `Universe/SOLBodyRotation.h/.cpp` (`TiltRotation`, `ComputeOrientation`);
  `FSOLBodyDef` gains `RotationPeriodH`/`AxialTiltDeg`/`W0Deg`; `FSOLBodyRegistry` gains
  parallel arrays, `GetOrientation(index)`, computed each `Update()` for every body
  including the Sun; `SOLRender::EclipticToUnreal(const FQuat4d&)` added. Real
  reference data (SDD 12 §3.1.1) in `PopulateSolarSystem`, cross-checked against
  published figures in review. 20 new automation tests across `BodyRotationTest.cpp`
  (new), `BodyRegistryTest.cpp` and `RenderPlacementTest.cpp` (extended) — 328/328 full
  suite green. One adversarial review round: caught a genuine contradiction in two of
  the test author's own assertions (fixed by the test author), found no bugs in the
  implementation, corrected a wrong claim in SDD 12 §3.2 (both the implemented
  `(-x,y,-z,w)` quaternion form and the originally-doubted `(x,-y,z,-w)` form are
  correct — they're the same rotation, negated), and logged one low-priority
  performance note as tech debt. Commit pending.)
- [ ] **12b — Visuals integration.** `ASOLBodyVisuals` applies
  `SOLRender::EclipticToUnreal(registry.GetOrientation(index))` to each body mesh
  alongside its existing position placement; `VISUALS_APPEARANCES` gets a small nonzero
  noise/band tweak for the Sun, Saturn, Uranus and Neptune entries (SDD 12 decision 1).
  No new `-SOL*` flag or scripted checkpoints (no new input/mechanic); verify with a PIE
  screenshot a few seconds apart per body (or a couple of representative bodies:
  something prograde like Earth, something retrograde like Venus or Uranus) showing the
  surface pattern has visibly rotated, plus Uranus's bands reading as roughly
  perpendicular to its orbital plane (its ~98° tilt). Adversarial review, commit.

## Cross-cutting

- [ ] `Docs/ARCHITECTURE.md`: note `FSOLBodyRegistry`'s new `GetOrientation` in its Key
  Types section (Universe module) once built.
- [ ] No `GAME_MECHANICS.md` entry needed (not a player-facing mechanic/control).
