<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 13 — Star field quality

Design: [`Docs/SDDs/13-star-field-quality.md`](../SDDs/13-star-field-quality.md).

- [x] **13a — Diagnose.** Capture reference screenshots; establish the cause of too-large, too-blurry and too-bright (sprite size/flux mapping, cubemap resolution/filtering, auto-exposure). Done: the mapping (V 8 at white) and the fixed 0.006 rad sprite size, not exposure (SDD 13 Amendment 1, A1.1).
- [x] **13b — Stage A fixes.** Clamp sprite size, fix the magnitude brightness mapping (pure function, test-first), re-bake/re-sample the cubemap sharper and dimmer, bounded exposure behaviour. Done, plus the user's later FOV-invariance requirement: pixel-sized flux-conserving points for sprites and cube, white magnitude 2.5, bounded histogram exposure, `-SOLStarSmoke` (Amendment 1, A1.2-A1.3).
- [ ] **13c — Verify and review.** Before/after screenshots, GPU cost, adversarial opus review, full suite, commit and push. Screenshots, GPU A/B and the adversarial review are done; its fixes are applied (exposure now set by the game from the bodies in view, SOLSceneExposure; per-pixel cube blend; seam and early-out fixes; tests reworked; Amendment 1 updated). Remaining: the user's acceptance of the sky cost against decision 3 (A1.5), the full suite, the commit.
- [ ] **13d — Stage B (conditional).** Sub-pixel flux-conserving points / procedural evaluation if stars are still too big after 13b. Currently judged unnecessary (Amendment 1, A1.4).
