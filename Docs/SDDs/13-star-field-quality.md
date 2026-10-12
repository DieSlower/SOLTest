<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 13 — Star field quality: realistic size, sharpness and brightness

Ticket `#13`. Plan: [`Docs/Plans/13-star-field-quality-plan.md`](../Plans/13-star-field-quality-plan.md). Follows SDD 5 (star field, issue #5). Decisions resolved with the user in a `grill-me` session on 2026-10-12.

## 1. Problem

Observed in play: the generated (sprite) stars look larger than real stars, the stars on the cubemap texture are too blurry, and everything seems too bright.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Approach | Staged. Stage A (first): keep the current layers (45,653 bright-star sprites plus the faint-star cubemap) and fix them: clamp sprite apparent size, correct the magnitude-to-brightness mapping, re-sample/re-bake the cubemap sharper with proper filtering and dim it. Stage B (only if stars are still too big after A): sub-pixel, flux-conserving points; evaluate replacing the cubemap with procedural/catalogue points (rank 3 on cost) only after measuring what it costs. |
| 2 | Exposure | Stars follow the scene's auto-exposure (eye-adaptation realism: they fade when a bright planet or the Sun fills the view) with a bounded min/max exposure range so they never blow out in dark sky. The existing exposure setup is the starting point; the root cause of "too bright" must be established (star mapping vs adaptation in empty space) before tuning. |
| 3 | Cost | Runtime cost must stay at or below today's (one static instanced draw plus one cubemap pass, nothing per frame). Any stage B step is measured (GPU ms) before it is adopted. |

## 3. Acceptance

- Before/after screenshots at several FOVs; bright stars read as sharp points at 1:1, faint field not blurry; brightness follows magnitude and the adaptation behaviour in decision 2.
- A documented magnitude-to-pixel-brightness mapping in this SDD, with any new pure mapping functions unit-tested.
- Measured GPU cost recorded in an amendment.

## 4. Open questions

Stage B scope (sub-pixel points and/or procedural) is decided after stage A's screenshots.
