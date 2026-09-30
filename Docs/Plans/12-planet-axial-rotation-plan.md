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
  performance note as tech debt. Commit 894f895.)
- [x] **12b — Visuals integration.** (Built:
  `ASOLBodyVisuals::HandleUniverseUpdated` sets `const FQuat bodyRotation(SOLRender::
  EclipticToUnreal(registry.GetOrientation(index)))` and passes it into each body mesh's
  `SetWorldTransform`, alongside the existing location — same per-body loop, same
  double-to-single-precision pattern already used for location (a plain copy under LWC).
  `VISUALS_APPEARANCES` tuned: Saturn `NoiseScale 0.04`/`NoiseStrength 0.5`, Uranus
  `BandFrequency 0.2`/`BandStrength 0.6`/`NoiseScale 0.04`/`NoiseStrength 0.6`, Neptune
  `NoiseScale 0.04`/`NoiseStrength 0.4`, all `NoiseBias 0`. The Sun was left at
  `NoiseStrength 0` — adversarial review found its fixed emissive swamps any noise lerp,
  so no value would make its spin visible; its rotation is exercised by 12a's unit tests
  only. Uranus needed a much higher `BandFrequency` than first tried (0.04 → 0.2) because
  its `ColorA`/`ColorB` are only ~0.07 apart — a couple of broad bands were invisible,
  many narrow ones are not. One adversarial review round (fresh `opus` subagent): no
  must-fix, one should-fix (the Sun no-op, applied), a few nits addressed. Verified with
  screenshots (no new `-SOL*` flag; used the existing `-SOLSpectator`/`-SOLStart`/
  `-SOLAltitudeKm`/`-SOLLookAt`/`-SOLSmokeShot` flags): Saturn's noise pattern visibly
  shifted between two shots ~40 real/sim-minutes apart at 1x time-warp (its 10.656 h
  period gives ~23° of spin over that gap — real-time-limited bodies like Saturn need
  a long wait at 1x since there's no console/command-line lever to accelerate the sim
  clock without a player controller); a wide Uranus shot shows its bands reading
  roughly top-to-bottom rather than horizontal, consistent with its ~98° tilt.
  328/328 automation suite green, unaffected (visuals-only change). Commit `1620059`.)

**Issue #12 (planet axial rotation) is complete: 12a and 12b are both built, tested,
adversarially reviewed and committed.**

## Cross-cutting

- [x] `Docs/ARCHITECTURE.md`: note `FSOLBodyRegistry`'s new `GetOrientation` in its Key
  Types section (Universe module) once built. (Done in 12a; 12b additionally noted in
  the Visuals module's `ASOLBodyVisuals` entry that it now applies body orientation.)
- [x] No `GAME_MECHANICS.md` entry needed (not a player-facing mechanic/control).
