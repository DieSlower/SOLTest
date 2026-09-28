<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Foundations and Flight Scaffold — Implementation Plan

**Goal:** Deliver Part 1 (issue #2) as three verified sub-parts: foundations (1a), ship flight (1b), HUD (1c).

**SDD of record:** [`Docs/SDDs/2-foundations-flight-scaffold.md`](../SDDs/2-foundations-flight-scaffold.md). Keep it in sync if the design shifts.

## Global constraints

- Follow `CLAUDE.md` and `Docs/STYLE_GUIDE.md` (banner, comment-on-every-function rule, naming, asset paths in one place).
- Each sub-part follows this loop, with a different subagent at each numbered step:
  1. **Tests first:** an `opus` subagent writes the automation tests from the contract only, without seeing the implementation, in `Source/SOLTest/Tests/`.
  2. **Confirm red:** run `Tools\RunTests.bat -Build`; tests must fail for the right reason.
  3. **Implement:** an `opus` subagent writes the minimum implementation to make them pass (it may read the tests).
  4. **Green:** run `Tools\RunTests.bat -Build`; all tests pass.
  5. **Adversarial review:** a fresh `opus` subagent reviews performance and style, report only.
  6. **Fix, re-verify:** apply fixes, re-check against the SDD, rerun the full suite.
  7. **Docs and commit:** update `Docs/GAME_MECHANICS.md` and this plan, then commit (`Issue #2: ...`), no push.
- The Unreal Editor must be closed while building; reopen it for PIE smoke checks and ask the user to run `/mcp` if unreal-mcp is needed.

## Steps

### 1a — Foundations

- [x] Re-validate or replace the unfinished `Source/SOLTest/Universe/SOLTypes.h` and `SOLKepler.h` against the style guide.
- [x] Double-precision universe position/velocity types, Keplerian elements and solver (position and velocity).
- [x] Sim-clock subsystem: real start date, warp steps, pause (game pause stops the universe tick), Backspace reset.
- [x] Data-oriented body registry: Sun and 8 planets (real radius, GM, JPL elements with rates), parent index.
- [x] Anchor subsystem: nearest body with 25% hysteresis; universe-to-render conversion; angular-size-preserving projection for far bodies; origin rebasing.
- [x] Body visuals: sphere meshes with recognizable procedural colors, emissive Sun with bloom, sun lighting.
- [x] Test map `/Game/Maps/SOL_Test`, free-fly spectator pawn for verification, set as default map.
- [x] Automation tests (64 pass).
- [x] Adversarial review; fixes applied (log-depth far placement, per-body sun shading, `FSOLRenderOrigin`, subsystem gating, eccentricity clamp, input-context cleanup, constants).
- [ ] Commit.

### 1b — Ship flight

- [x] Mass ship entity fragments and processors (flight, gravity), quaternion rotation, no gimbal lock. (`Ship/`; processor run from the anchor tick, see SDD section 3.)
- [x] Flight-assist and Newtonian modes, boost, speed cap on a log scale up to 0.5c. (Integrated in the Mass step and verified by `-SOLSmokeFlight`; player input for them comes with the Pawn.)
- [x] `ASOLShipPawn` proxy: Enhanced Input (virtual-joystick mouse, keys per SDD), spring-arm chase camera with lag, speed FOV, Alt free-look, placeholder ship mesh. (`Ship/SOLShipPawn`; default pawn, `-SOLSpectator` for the debug camera.)
- [x] Spawn in Earth orbit matched to Earth; analytic sphere collision.
- [x] Targetable interface; target selection (T, R/F, X); M reference-frame matching. (`Targeting/`; the ship takes its reference velocity from the targeting subsystem.)
- [x] Automation tests (114 pass).
- [x] Smoke checks: `-SOLSmokeFlight` 7/7, `-SOLSmokeInput` 29/29 through the real input pipeline, screenshot behind the ship over Earth.
- [x] Adversarial review; fixes applied (Amendment 2: warp carry, hitch budget, substep bodies, swept collision and broadphase, const shared params, parallel chunk loop, focus loss, manual camera lag, fill light, style). Tests 131 pass; `-SOLSmokeFlight` 9/9 incl. the 1 d/s warp phases; `-SOLSmokeInput` 29/29; non-unity build clean.
- [ ] Commit.

### 1c — HUD

- [ ] Core flight HUD: reticle and joystick indicator, speed and cap, assist state, reference frame, nearest body and altitude, sim date and warp, anchor body.
- [ ] Target bracket, prograde/retrograde markers.
- [ ] 3D spherical radar with auto log range.
- [ ] Body orbit ellipses (O) and predicted ship path (P).
- [ ] F3 speed panel (numeric field and body speed list).
- [ ] Automation tests for HUD data, PIE smoke check, adversarial review, commit.
