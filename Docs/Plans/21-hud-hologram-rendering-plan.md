<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 21: HUD hologram rendering

Design: `Docs/SDDs/21-hud-hologram-rendering.md`. Tick steps as they are done.

- [x] A (#28). HUD event bus (cycle removed), batched Slate custom-vertex element and shape material, Settings
  strength slider (+ migration), proof element, tests, review, suite, commit. Done 2026-10-28, see SDD 21
  Amendment 1: suite 1037/1037; smokes Hud 20/20 (1280x720 and 1000x800), Input 40/40, Route 26/26, RouteOverlay
  25/25, Map 16/16, MapPick 22/22, MapAlign 26/26, Hologram 12/12; adversarial review fixed (no blockers).
- [ ] B (#29). Port every flight HUD element; hologram panels; glow text; tests/smokes; review; commit.
- [ ] C (#30). 3D hologram radar; tests/smokes; review; commit.
- [ ] Close #21 when A-C are done.
