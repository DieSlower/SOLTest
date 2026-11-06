<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# VR-resolution performance gate (Varjo XR-4)

A future ticket: add a perf smoke that runs the existing `-SOLSmoke*` scenes at the Varjo XR-4 resolution, as one eye and
as two eyes, and fails when the frame budget is missed, so the game keeps a VR-capable frame rate without anyone owning a
headset.

## Why

The developer has no VR headset but decided the project's performance floor is the Varjo XR-4. The numbers and reasoning
are in [`../Research/VR_PERFORMANCE_TARGETS.md`](../Research/VR_PERFORMANCE_TARGETS.md) section 5a: 3840 x 3744 per eye
(14.38 Mpx), 7680 x 3744 for two eyes (28.75 Mpx), 90 Hz (11.11 ms budget), about 1.3 Gpx/s for one eye and 2.6 Gpx/s for
two. The Index tier (4032 x 2240, 9.0 Mpx) stays as a secondary reference.

## Proposal

- A new flag or profile (for example `-SOLSmokeVrRes=1eye|2eyes`) that sets 3840 x 3744 or 7680 x 3744 (`-ResX` /
  `-ResY`, `r.ScreenPercentage 100`) and reuses the existing profile smokes (holo-menu profile, solid menu, flight, map,
  HUD) to collect `stat unit`, `stat gpu` and `ProfileGPU` numbers, reporting p99 frame time, Game and GPU thread times.
- Pass criteria: `1eye` p99 <= 10.0 ms and `2eyes` p99 <= 9.5 ms on both Game and GPU, reported as a table in the smoke log.
  The 75 Hz mode (13.33 ms) is not a target.
- Re-measure the menu and HUD GPU budgets (#42, #46: 0.1 ms CPU / 0.3 ms GPU at the flat reference) at both resolutions.
  Provisional fill-bound ceilings: about 1.2 ms GPU at `1eye` and about 2.4 ms at `2eyes`; CPU unchanged.
- Decide the renderer question (deferred with Nanite, Lumen-class features and TSR versus a forward/MSAA profile) and
  whether the smoke should also run a forward-shading configuration.
- Review checklist item 11 in the project docs already requires this for every code change.

## Open questions

- Is the target a real VR port some day, or just a performance headroom rule? That decides whether forward shading,
  instanced stereo and world-space/stereo-layer UI are in scope.
- Run it in CI-like manual form only (needs a real GPU with enough VRAM for 28.75 Mpx, no `-NullRHI`) or fold it into
  `Tools\RunTests.bat -Rhi`.
- Which scenes: orbit at a planet, dense Mass fields, map open.
- Varjo publishes no recommended render resolution, so the panel resolution is used; add a supersampling multiplier if
  one is ever sourced.

## Proxy on this machine (user decision, 2026-11-06)

The RTX 2080 Ti is below Varjo's minimum, so the real XR-4 resolutions (3840x3744 per eye, 7680x3744 two eyes) cannot pass here (scene alone 22-28 ms GPU at one eye). Stand-in rule: VR-ready if the game runs at 4K (3840x2160) at 100 Hz or better (frame p99 <= 10.0 ms) on this machine; XR-4 optimisation happens later on a different card. The menu profile smoke judges only the menus' own share (CPU delta <= 0.2 ms, GPU within 1.2 / 2.4 ms); the whole-frame check is this gate's job. First reading (issue #46 part 46h, SDD 46 section 13.2): with a solid menu open at 4K the p99 frame is 16.0-17.7 ms (about 60 fps, GPU-bound by the scene), so the proxy is not met yet.
