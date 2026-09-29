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

- [x] Core flight HUD: reticle and joystick indicator, speed and cap, assist state, reference frame, nearest body and altitude, sim date and warp, anchor body. (`UI/SOLFlightHud`; the temporary `UI/SOLDebugHUD` is deleted.)
- [x] Target bracket, prograde/retrograde markers.
- [x] 3D spherical radar with auto log range.
- [x] Radar range amendment (SDD Appendix C Amendment 1): 1 AU ceiling, 10,000 km / 1 km (targeted) floor, AUTO/MANUAL modes (`-` / `=` zoom, Home to AUTO); replaces the interim nearest-4 rule. Tests 171 pass; `-SOLSmokeHud` 17/17 (adds h-radar-zoom: 8 x '-' with Earth targeted, 1 AU -> 1.5 km MANUAL; h-radar-auto: Home); non-unity build clean.
- [x] Radar AUTO simplification (SDD Appendix C Amendment 3): `SOLRadar::ComputeRange` and its 6 tests removed; AUTO range = `EffectiveFloorM` (10,000 km, 1 km with a target); Amendment 2's root-star exclusion removed. Tests 165 pass; `-SOLSmokeHud` 17/17 (h-radar-auto now also checks range == floor; a screenshot right after T shows `RADAR 1.0 km (AUTO)`); `-SOLSmokeShot` at 10,000 km above Earth shows `RADAR 10000.0 km (AUTO)`; non-unity build clean.
- [x] Body orbit ellipses (O); predicted ship path (P) bound as a no-op per SDD section 6.
- [x] F3 speed panel (numeric field and body speed list). (`UI/SOLSpeedPanelWidget`, UMG built in C++.)
- [x] Automation tests for HUD data (164 pass); `-SOLSmokeHud` 15/15 (O/P toggles, panel blocks W, digit steps, unit cycle, list select, F3/Esc close) with screenshots of the HUD over Earth and the orbit ellipses from 3 AU above the Sun; non-unity build clean.
- [x] Adversarial review; fixes applied (SDD Appendix C Amendment 4: whole-vector radar scaling, stepper unit overflow refusal at 1e9, radar stalk layout margin, near-plane clip at the plane itself, zoom no-op keeps the mode, shared `SOLHudFormat::AppendDistanceM`, reticle/joystick drawn under the markers, GAME_MECHANICS radar label, long lines). Tests 169 pass (adds `HudFormat.AppendDistanceMatchesFormat`); `-SOLSmokeHud` 17/17 (h-radar-zoom now expects `-` at the 1 km floor to stay AUTO); screenshots checked (radar stalks within the reserved margin, bracket drawn over the joystick ring); non-unity build clean.
- [ ] Commit.
