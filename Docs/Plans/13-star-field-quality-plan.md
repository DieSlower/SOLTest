<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 13 — Star field quality

Design: [`Docs/SDDs/13-star-field-quality.md`](../SDDs/13-star-field-quality.md).

- [ ] **13a — Diagnose.** Capture reference screenshots; establish the cause of too-large, too-blurry and too-bright (sprite size/flux mapping, cubemap resolution/filtering, auto-exposure).
- [ ] **13b — Stage A fixes.** Clamp sprite size, fix the magnitude brightness mapping (pure function, test-first), re-bake/re-sample the cubemap sharper and dimmer, bounded exposure behaviour.
- [ ] **13c — Verify and review.** Before/after screenshots, GPU cost, adversarial opus review, full suite, commit and push.
- [ ] **13d — Stage B (conditional).** Sub-pixel flux-conserving points / procedural evaluation if stars are still too big after 13b.
