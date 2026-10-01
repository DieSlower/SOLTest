<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Star Field — Implementation Plan

**Goal:** Deliver Part 4 (issue #5): a real ~2.55M-star sky from the AT-HYG catalog,
rendered as bright-star sprites plus a baked HDR cubemap for the faint remainder, at
near-zero frame cost.

**SDD of record:** [`Docs/SDDs/5-star-field.md`](../SDDs/5-star-field.md). Keep it in
sync if the design shifts.

## Global constraints

- Follow `CLAUDE.md` and `Docs/STYLE_GUIDE.md`.
- Unlike prior parts, this ticket is mostly a content pipeline plus engine-glue, not
  new pure C++ logic — no dedicated TDD sub-part is expected (see SDD 5 §3.2); if a
  genuine pure-logic need turns up mid-implementation, it still gets the normal
  tests-first treatment.
- The Unreal Editor must be closed while building; reopen for the screenshot check and
  for running the Unreal-Python import step (`-run=pythonscript`, editor-cmd, headless
  is fine — see `Tools/CreateSOLContent.py`'s existing invocation for the pattern).
- Adversarial review still applies to both the pipeline code and the runtime actor,
  fix, re-verify, docs, commit (`Issue #5: ...`), no push.

## Steps

- [x] **4a — Offline bake pipeline.** (Built: `Tools/StarField/fetch_athyg.py`
  (fetches AT-HYG and HYG v4.4, both Git LFS, from Codeberg), `bake_star_field.py`
  (numpy: RA/Dec→ecliptic via IAU 2006 obliquity, Tycho BT-VT→Johnson B-V via ESA
  1997 eq. 1.3.20, B-V→RGB via Ballesteros 2012 + a Wyman/Sloan/Shirley CIE fit, the
  mag-8.0 split, bilinear flux-splatting with cross-face reprojection onto
  2048×2048×6 HDR cubemap faces, written as a hand-rolled DX10 DDS since no HDR image
  library was available). Real counts: 45,653 bright stars, 2,513,000 faint. One
  adversarial review round (ran the code, checked the DDS header against UE 5.8's own
  loader source, independently re-derived the astronomical formulas): no blocking
  findings; fixed the brightest ~500 stars' saturated Tycho magnitudes (cross-referenced
  against HYG v4.4, with a data-verified exception for blended multi-star entries),
  added a validated placement self-check (catches deliberately-injected splatting bugs)
  that found and fixed a small real edge/corner splatting inaccuracy, added atomic
  writes and a versioned parse cache. Previews independently inspected in the main
  session: correct Milky Way band and dust-lane structure. Commit 5591976.)
- [x] **4b — Content import and CelestialVault asset copy.** (Built:
  `Source/SOLTest/StarField/SOLStarFieldData.h` (`FSOLBrightStar`, `USOLStarFieldData`),
  `Tools/StarField/import_star_field.py` (a normal-Python driver that temporarily
  enables CelestialVault in `.uproject` only for the one-time copy run — with a sidecar
  backup, a child-process timeout, and byte-for-byte restore in `finally` — then the
  in-editor import of the cubemap and bright-star data). Assets under
  `/Game/SOL/StarField/` (`T_SOLStarFieldCube`: BC6H, sRGB off, generated mips;
  `DA_SOLStarField`: 45,653 stars) and `/Game/SOL/StarField/CelestialVault/` (7 copies).
  Paths in `SOL::Paths`. Cube orientation verified in-engine (rendered all six faces
  and an ecliptic equirectangular view through the imported cube, compared against the
  bake — identical, no mirroring). One adversarial review round found the
  CelestialVault-independence check had real gaps (could silently pass without
  actually checking anything) and ran only once rather than as a lasting guard, plus a
  `.uproject` restore-safety gap and a wrong brightness-matching comment — all fixed:
  a new permanent `SOLTest.StarField.*` automation test (4 tests) now verifies
  independence on every run with the plugin disabled (the normal repo state), proven
  to actually catch a leak (temporarily treating an unrelated module as a leak and
  confirming the test fails, then reverting the probe); the `.uproject` procedure
  gained a sidecar backup and timeout. 332/332 full suite green, independently
  re-verified. Commit pending.)
- [ ] **4c — Runtime actor.** `Source/SOLTest/StarField/ASOLStarField` per SDD 5
  Appendix B: `UInstancedStaticMeshComponent` populated once from `USOLStarFieldData`,
  a large inverted-sphere (or equivalent) unlit cubemap-sampling background, spawned
  by `ASOLGameMode` alongside `ASOLBodyVisuals`, translated (never rotated) to the
  observer's location on `OnUniverseUpdated`, no tick. Screenshot verification (several
  look directions including one that should show the Milky Way band), looked at
  directly, not just logged. Adversarial review (performance — confirm the ISM
  population is one-time, no per-frame allocation; style; correctness of the
  ecliptic-frame orientation against the bake), fix, re-verify, commit.

## Cross-cutting

- [ ] `Docs/ARCHITECTURE.md`: new `StarField/` module entry (module map diagram, key
  types, frame-order note if `OnUniverseUpdated` gains a new listener).
- [ ] No `GAME_MECHANICS.md` entry needed (not a player-facing mechanic/control).
