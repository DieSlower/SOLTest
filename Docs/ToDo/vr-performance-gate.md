<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# VR-resolution performance gate

A future ticket: add a perf smoke that runs the existing `-SOLSmoke*` scenes at the PC-VR equivalent resolution and
fails when the frame budget is missed, so the game keeps a VR-capable frame rate without anyone owning a headset.

## Why

The developer has no VR headset but wants the game to stay above what VR needs. The numbers and reasoning are in
[`../Research/VR_PERFORMANCE_TARGETS.md`](../Research/VR_PERFORMANCE_TARGETS.md): 2 x 2016 x 2240 (9.0 Mpx, Index at
SteamVR 100%) at 90 fps, an 11.11 ms budget with p99 at or below 10 ms on CPU and GPU, about 3.7x the pixel throughput of
a 1440p60 game.

## Proposal

- A new flag or profile (for example `-SOLSmokeVrRes`) that sets 4032 x 2240 (`-ResX` / `-ResY`, `r.ScreenPercentage 100`)
  and reuses the existing profile smokes (holo-menu profile, flight, map, HUD) to collect `stat unit`, `stat gpu` and
  `ProfileGPU` numbers, reporting p99 frame time, Game and GPU thread times.
- Pass criteria: p99 at or below 9.5 ms for the 90 fps target (includes about 15% stereo margin), reported as a table
  in the smoke log; a stretch line for 120 fps (8.33 ms).
- Re-measure the menu and HUD budgets (#42 0.1 ms, #46 0.1 ms CPU / 0.3 ms GPU) at that resolution, since UI GPU cost
  scales with pixel count (research doc, section 7).
- Decide the renderer question (deferred with Nanite, Lumen-class features and TSR versus a forward/MSAA profile) and
  whether the smoke should also run a forward-shading configuration.

## Open questions

- Is the target a real VR port some day, or just a performance headroom rule? That decides whether forward shading,
  instanced stereo and world-space/stereo-layer UI are in scope.
- Run it in CI-like manual form only (needs a real GPU, no `-NullRHI`) or fold it into `Tools\RunTests.bat -Rhi`.
- Which scenes: orbit at a planet, dense Mass fields, map open.
