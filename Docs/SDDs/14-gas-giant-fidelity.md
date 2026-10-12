<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 14 — Gas giant fidelity: layered clouds, volumetric near layer, moving storms

Ticket `#14`. Plan: [`Docs/Plans/14-gas-giant-fidelity-plan.md`](../Plans/14-gas-giant-fidelity-plan.md). Decisions resolved with the user in a `grill-me` session on 2026-10-12.

## 1. Problem

Gas giants (Jupiter, Saturn, Uranus, Neptune) look static and flat. They need a realistic atmosphere with band and storm patterns that move.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | Technique | Both: layered translucent cloud shells with animated flow-map/noise bands and storm vortices for distance, plus a true volumetric cloud layer that fades in when the ship is within a few planet radii. |
| 2 | Scope | All four gas/ice giants via one shared material system with per-planet parameters (band colours, storm count, spin); Jupiter (with a Great-Red-Spot-style storm) is the tuning reference. |
| 3 | Animation | Driven by the simulation clock (follows time warp) with an artistic speed factor so bands visibly drift at 1x; differential rotation per latitude; storms are moving vortices. Deterministic and pause-safe. |
| 4 | Budget | Shells <= 0.5 ms GPU; with the volumetric layer active <= 1.5 ms GPU total. The volumetric layer costs nothing far away (hidden, resources released after a delay like the ring dense layer). Measured with `ProfileGPU` and recorded in an amendment. |
| 5 | Architecture | A separate `ASOLGasGiantVisuals` actor per giant (like `ASOLRingVisuals`), spawned by the game mode: independent LOD, visibility and resource release, no extra per-body state in `ASOLBodyVisuals`. Placement/precision follows the render-origin snapshot conventions. |

## 3. Acceptance

Screenshots from far, mid and near for each giant; bands and storms visibly move at 1x and under warp; measured GPU cost within decision 4; pure logic (band/latitude rotation, LOD fade) unit-tested.

## 4. Open questions

Whether UE Volumetric Clouds can be used at planetary scale around a single body without precision problems (spike in 14b).
