<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 43: Space dust speed cues

SDD: [`Docs/SDDs/43-space-dust-speed-cues.md`](../SDDs/43-space-dust-speed-cues.md). This is a living document:
tick each step as it lands. Each part is built by its own subagent, with its context kept under ~35%, and is small
enough to verify without the user.

**Gate: design resolved** (SDD section 10, Q1-Q10 answered). **Sequencing: do not start implementation until issue
#32 is committed** (its SAVE_VERSION 8 must be in the repo first; 43c bumps the then-committed value by one).

## Parts

### 43a: pure math (test-first; tests opus/high, implementation sonnet/medium)

1. [x] Hand SDD section 7's contract (functions, edge cases, the default constants) to a separate opus subagent.
   It writes `Source/SOLTest/Tests/SpaceDustMathTest.cpp` (`SOLTest.SpaceDustMath.*`) from the contract only, with
   no implementation shown.
2. [x] Add stubs in `Visuals/SOLSpaceDustMath.{h,cpp}` and the `SOL::SpaceDust` constants (with the camera
   `static_assert`) in `SOLConstants.h`. Build, then run `Tools\RunTests.bat -Filter SOLTest.SpaceDustMath`. The
   tests must be red for the right reasons. (If the headless runner stalls, a `Docs/TECH_DEBT.md` item, run the tests
   in the open editor instead.)
3. [x] Implement. Green.
4. [x] Full suite green.

### 43b: assets (opus/high; needs the Unreal Editor open via the Rider run configuration)

1. [x] Write `Tools/SpaceDust/create_space_dust_assets.py`. It builds `M_SOLSpaceDust` (unlit, additive, after
   motion blur, soft dot and streak caps, Niagara-sprite usage) in `/Game/SOL/SpaceDust/`, is idempotent and supports
   `SOL_REBUILD`.
2. [x] Build `NS_SOLSpaceDust` in the editor (live through `NiagaraToolsets`) and add a `Tools/SpaceDust/*.py`
   script that recreates it (Q9):
   - a GPU local-space emitter with fixed bounds of ±L/2 and a burst of `User.SpaceDustParticleCount`;
   - the spawn attributes `DustAnchor`, size and brightness;
   - the update wrap, velocity, sprite size and colour alpha with `DustFade`;
   - a velocity-aligned sprite renderer with motion vectors and shadows off.
   Record the recipe in an SDD amendment, and add a `TECH_DEBT.md` item for any part the script cannot express.
3. [x] Add `SOL::Paths::SPACE_DUST_*` and `SOL::SpaceDustParams` names. The script header and the asset agree with
   `SOLConstants.h`.
4. [x] PIE spike: spawn the system from Python at the ship and drive the parameters by hand (stopped, slow, fast).
   Check by screenshot: points versus streaks, no seam, no near-camera blobs, no TSR smear. Note the first
   `ProfileGPU` numbers here.

### 43c: overlay row (persistence; opus, xhigh)

The brief must carry:
- the approved Q1 decision: bump `SAVE_VERSION` (8 -> 9, relative to the value committed after #32) with an explicit
  migration step writing `SpaceDust` = on into every overlay profile (SDD section 6.3);
- Q2 (the master O does not govern the dust: a table-level flag) and Q3 (default on);
- "version and migrate, never silently discard";
- `KEY_BINDING_LAYOUT_VERSION` unchanged.

The subagent reports exactly which version constants and fields it changed (the new `SAVE_VERSION`, the migration
function, no `UPROPERTY` layout change); the main session checks that against SDD section 6.3.

1. [x] Tests first, by a separate subagent from the contract:
   - extend `OverlayModelTest` (16 rows, `SpaceDust` last with its id, label, default and parent; saved map round
     trip; missing key -> default; `IsShown` for `SpaceDust` ignores the master O while other rows still follow it);
   - extend `SOLSettingsRulesTest` (the migration: every profile gets `SpaceDust` = on, other keys and the active index
     untouched, an existing value kept, idempotent; `KEY_BINDING_LAYOUT_VERSION` pinned unchanged) plus the pinned
     "schema is N" updates in `FlightModeMathTest`. Red.
2. [x] Append `EOverlay::SpaceDust` and its `OVERLAY_DEFS` row with the "not governed by the master" flag, then the
   migration step, the `SAVE_VERSION` bump and the schema comment.
3. [x] Re-validate the existing overlay tests: every hard-coded row count, mask and `-SOLSmokeOverlay`'s "15 rows"
   check becomes 16. Suite green. `-SOLSmokeOverlay` green at both aspect ratios.

### 43d: engine integration (opus, high)

1. [x] `Visuals/SOLSpaceDustComponent.{h,cpp}`: the component on `ASOLShipPawn`, its Niagara component (absolute transform), `BeginPlay`
   (constant parameters, a single activate, then hidden and paused; cached `FName`s), the `OnUniverseUpdated` handler,
   and `EndPlay` (unbind, deactivate). No tick.
2. [x] The per-frame handler:
   - read `vRel` (locked reference frame; confirm the surface-landed case reads about 0 or report), `dt`, warp, jump,
     map, paused and the row's `IsEnabled` (not the master); `ShouldRun`;
   - smoothing, with a reset after a hard gate;
   - `AdvanceOffsetCm`;
   - the ship's render location from the final render origin (double, cast last);
   - the camera offset from the same-frame pawn camera and ship root;
   - set the parameters, then visibility and pause.
3. [x] Create the component on `ASOLShipPawn` (player ship only, not under `-SOLSpectator`). Register the
   `sol.SpaceDust` cvar. Scale the particle count with effects quality (Low 1024, Medium 2048, High and Epic 4096; at
   BeginPlay and on a preset change).
4. [ ] PIE check by hand through unreal-mcp: thrust, strafe, M match, warp, jump, map, pause, a landed ship, the master O
   (no effect) and the Ctrl+O row.
   Screenshots. (Scripted instead by `-SOLSmokeDust`, 24/24, with screenshots: unreal-mcp was not connected this
   session. The hands-on strafe/drift direction and TSR-smear check is left to the user.)

### 43e: smoke and profiling (sonnet, medium)

1. [x] `FSOLSpaceDustSmoke` and `-SOLSmokeDust` / `-SOLSmokeDustProfile` (SDD section 11, steps 1-12), using the
   smoke settings slot.
2. [x] Run it at 1280x720 and 1000x800. Look at `DustSmoke_Slow/Fast/Menu.png`.
3. [x] Profile: GPU with the dust shown at `Amax` against `sol.SpaceDust 0`, game thread `stat dumpave`; record the
   results against SDD section 9's budget in an SDD amendment.
4. [x] Regression smokes: Overlay, Hud, Jump, Map, Interact.

### 43f: docs, review, commit

1. [x] Docs (sonnet, low-medium), per SDD section 12:
   - `GAME_MECHANICS.md` (including that the master O does not affect the dust);
   - `SOLMenuText.h` (the Controls Ctrl+O row and the Info overlays paragraph), so the in-game text stays truthful;
   - `ARCHITECTURE.md` (the Visuals row, the `OnUniverseUpdated` listeners, the pawn-owned component, the smoke flags
     and the cvar);
   - `TECH_DEBT.md` (the bind-order item and an item for any part the recreate script cannot express).
2. [x] README: a new "Screenshots" gallery section with images in `Docs/Images/` (slow points, fast streaks, the
   Overlay Menu row; `43-dust-*.png`), plus the smoke list. The coordinator adds the project rule "2-3 new shots per
   finished ticket" to the project workflow rules.
3. [x] Adversarial performance and style review (a fresh opus subagent at xhigh, report only). Brief it with:
   - the changed files, the SDD, `STYLE_GUIDE.md` and `UE_PERFORMANCE_GUIDELINES.md`;
   - the known `TECH_DEBT.md` items: the TSR smear in render space, the after-motion-blur upsampling, the
     `OnUniverseUpdated` bind order, and the headless test runner.
   Fix the findings, re-check against the SDD, and re-run the full suite plus `-SOLSmokeDust` and `-SOLSmokeOverlay`.
4. [x] `graphify update .`, `Tools\UpdateMetrics.bat`. The coordinator updates `Docs/Plans/STATUS.md`. Then commit
   `Issue #43: Part 43x - ...` on `master` and push.

## Progress notes

- 2026-11-03: SDD and plan drafted from the user's grill-me decisions D1-D5.
- 2026-11-03: Q1-Q10 answered and folded into the SDD (section 10). Waiting only on #32 being committed. No code yet.
- 2026-11-03: #32 committed (c045cf2, SAVE_VERSION 8). Implementation started.
  - Landed-ship check (SDD 4.2): `GetReferenceVelocityMps()` is the reference body's non-rotating centre velocity, and the
    ship physics (`SOLFlight::ResolveSweptSphereCollision`) also rests the ship in that non-rotating body frame (no
    co-rotation with the spin), so `vRel` reads ~0 for a resting ship. No workaround needed.
  - 43a and 43c tests written by separate subagents from the contract (SpaceDustMathTest 24 tests; OverlayModelTest,
    SOLSettingsRulesTest, FlightModeMathTest extended). Stubs built; red for the right reasons (stub values, schema 8,
    master flag). `SOL::SpaceDust::COLOR` is named `DUST_COLOR` (COLOR is an engine macro).
  - Headless runner still broken: tests run in the open editor (`Automation RunTests`, via Rider's `ue_execute_python`;
    unreal-mcp was not connected this session).
  - 43a-43e code written; 43c migration 8 -> 9 (`SAVE_VERSION` 9, layout 7 unchanged). Assets built by
    `Tools/SpaceDust/create_space_dust_assets.py` (NiagaraToolsets via `unreal.ToolsetRegistry.execute_tool`, nothing
    manual). Full suite 1212/1212. Regression smokes green (Jump g3-orientation-rebase fails as before since 10-27,
    date-dependent anchor Deimos, unrelated). Docs/in-game text synced.
  - **Resume state:** the review found (1) BLOCKER: dust not visible in `DustSmoke_Slow/Fast.png` although the CPU state
    says shown (debugging in progress); (2) `-SOLSmokeDust` check 11-rebase fails (the hold teleports every frame and
    the bound uses the pawn's dt, not the anchor's); (3) no profiling numbers yet (add a cycle stat, run
    `-SOLSmokeDustProfile`, amend the SDD); (4) the Overlay Menu footer "O: everything checked on / off together" must
    say the dust is exempt; nits: frame-0 reactivate warning, literal id in the migration, dead `IsPaused` branch,
    math header comments / long lines / section separators. Then screenshots for the README, metrics, commit.
  - Blocker (1) FIXED: every sprite had size 0 (the colour module's SpriteSize input read its own output) and the
    in-place `SOL_REBUILD` left a dead system (the script now deletes and re-creates both assets in a running editor).
    By-eye tuning (SDD Q4): `POINT_SIZE_CM` 30 -> 60, `INTENSITY` 0.6 -> 2.0 (tests, SDD, GAME_MECHANICS updated) -
    flag for the user's check. Suite 1212/1212; proof shots in `Saved/Screenshots/Dust43/` (A/B with
    `sol.SpaceDust 0`). Still open: items (2)-(4) and the nits above, then README screenshots, metrics, commit.
  - Review fixes done (rebase check, cycle stat, footer text, nits); a fresh re-review found no blocker; its
    should-fix items were fixed (parameter writes skip unchanged values through the override store: game thread
    0.036 -> 0.015 ms shown, GPU ~0.03 ms; SDD Amendment 2). Final: suite 1212/1212, `-SOLSmokeDust` 24/24 at
    1280x720, 1000x800 and 1920x1080, `-SOLSmokeOverlay` 11/11 at both. README gallery: `Docs/Images/43-dust-*`.
    Ticket done.
  - Re-review fixes (43g): `PushFrame` skips unchanged writes and writes the per-frame parameters straight into the
    override store (SDD Amendment 2; re-measure pending, the user runs it). `USOLShipSubsystem::SetVelocityMps` has no
    automation test: the ship subsystem exists only in Game and PIE worlds, and `-SOLSmokeDust` check 11-rebase covers
    it (SDD section 11). Menu screenshot now taken before the Space Dust row click; smoke literals named; SDD, script
    header and long-line nits fixed.
