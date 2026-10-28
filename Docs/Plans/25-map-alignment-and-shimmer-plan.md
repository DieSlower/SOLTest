<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 25: Map alignment and orbit-line shimmer

SDD: [`SDDs/25-map-alignment-and-shimmer.md`](../SDDs/25-map-alignment-and-shimmer.md). One small part, one commit.

| Step | What | Status |
|---|---|---|
| 1 | Reproduce: `-SOLSmokeMap` and a new `-SOLSmokeMapAlign` (CPU line vs body, per-view screenshots with the target's line highlighted, an orbit sweep) at 16:9 and 5:4 | Done |
| 2 | Diagnose: line placement correct on CPU and GPU at 16:9; at 5:4 the 3D view is scaled against the HUD (engine default MaintainYFOV vs the HUD's horizontal-FOV projection); lines in the AfterDOF pass go through TSR with no velocity | Done |
| 3 | Fix: `DefaultEngine.ini` local player `AspectRatio_MaintainXFOV`; `M_SOLOrbitLine` translucency pass After Motion Blur (script + asset); HUD comment | Done |
| 4 | Smoke compares the HUD-projected icon with the renderer-projected line; fails before (5:4, MaintainYFOV), passes after | Done |
| 5 | Test `SOLTest.ViewProjection.FovIsHorizontalAtEveryAspect` (test-after, small fix) | Done |
| 6 | Identify the white line (asteroid orbit lines; joystick ring; pick overlay) | Done |
| 7 | Docs: SDD, this plan, `GAME_MECHANICS.md`, `ARCHITECTURE.md` | Done |
| 8 | Full suite (960/960), adversarial opus review: fixed the vacuous test (view rect, Y scale), exposure's vertical FOV, highlight-slot check, map-open check once, abort quits, 16:9 warning, includes/line lengths, stale sort-priority comment, docs (screen-percentage trade-off, ultrawide cost) | Done |
| 9 | Re-run the suite, commit and push | Done |
| 10 | Amendment 1: date-dependent 'Earth on line' smoke failure; mirror clips chords at the near plane and samples like the line actor, tolerance includes the line's chord error; tests `SOLTest.MapAlign.*`; smoke at five dates, suite 1014/1014 | Done |
