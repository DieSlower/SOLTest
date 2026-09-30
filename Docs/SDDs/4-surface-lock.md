<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 4 — Level / surface-lock (L)

Ticket `#4` ("Part 3: Level / surface-lock (L)"). Plan of record:
[`Docs/Plans/4-surface-lock-plan.md`](../Plans/4-surface-lock-plan.md).

Refines SDD 1's Level (L) row (updated in place, 2026-09-30, to match this document).
Must not contradict [`Docs/SDDs/1-solar-system-architecture.md`](1-solar-system-architecture.md)
or [`Docs/SDDs/2-foundations-flight-scaffold.md`](2-foundations-flight-scaffold.md) without
updating them too. All decisions below were resolved with the user during a `grill-me`
session on 2026-09-30.

---

## 1. Problem

Give the player a way to fly close to a planet's surface with a sane, continuously
tracking "down" reference, without turning the ship into a different vehicle. Unlike
the jump map (a distinct mode with its own camera and input context), surface-lock
must coexist with normal flight at all times — the player keeps full 6DoF thrust and
rotation control throughout.

## 2. Decisions

| # | Area | Decision |
|---|---|---|
| 1 | What locking changes | **Orientation-only horizon lock.** Full flight-assist/Newtonian thrust and rotation control is unaffected. The only effect is a continuous alignment correction (decision 4) that drives the ship's local "up" toward directly away from the locked body's center. The player can still thrust away, ignore it, or crash — locking never overrides thrust or gravity, and there is no autopilot, hover, or ground-relative control remap. |
| 2 | Reference frame | Engaging **auto-matches the reference frame** to the locked body, via the same mechanism `M` uses (`USOLTargetingSubsystem`): the frame locks to the body's candidate index, overriding whatever `M` had (a prior manual `M` lock on something else is replaced). Releasing surface-lock does **not** revert the reference frame — whatever it is when the lock drops stays in effect; the player's own `M` remains a fully independent toggle at all times. |
| 3 | Control during the alignment | **Full control throughout; the process is continuous and can be interrupted at any time**, not a scripted animation. There is no separate "blend phase" — see decision 4: the same continuous corrector runs for as long as the lock is engaged, so leaving range or pressing `L` simply stops it, and re-engaging restarts the correction from whatever the current error is. |
| 4 | Alignment math | **Continuous exponential minimal-rotation alignment**, not a fixed-duration blend and *not* a roll-only correction (revised 2026-09-30 — see Amendment 1 below; the original roll-only formulation shipped in 3a's first pass and was replaced before commit). Each frame, the ship's local "up" (+Z) is rotated toward the target up direction (away from the locked body's center) by the shortest-arc rotation between them, scaled to a fraction `1 - exp(-dt / τ)` of the full angle (frame-rate independent exponential approach; `τ ≈ 1 s`, ~5 s settle from a 180° start, same as before). The ship's heading (forward) is **not** held fixed — it is carried along by that same minimal rotation (a parallel transport), which is exactly what keeps it "tangent to the down vector" as the ship flies over different latitudes/longitudes of the locked body, with no preferred axis and no singularity anywhere on the sphere (see Amendment 1 for why the original roll-only approach broke down near the ship's own zenith/nadir). The player's own control input is never blocked — thrust and the rotation stick always apply — this is a passive nudge added on top each frame, the same relationship flight-assist's drift-cancellation has with the player's thrust axis. |
| 5 | Engage/release ranges | Two distinct, independently tracked engagement paths, each with its own scaled warn/hard-release hysteresis (same 25%-style margin already used for anchor-switching elsewhere in the codebase): **Auto** — engages with no keypress the moment altitude ≤ 10 km above a body's surface; warns at 10 km and force-releases at 12.5 km if the ship climbs back out without the player pressing `L`. **Manual** — `L` can engage early, from up to `ManualRangeM = max(1,000 km, that body's radius)` out (so small bodies still get a 1,000 km window, and huge bodies like the Sun get a window scaled to their own size); once manually engaged it only warns at `ManualRangeM` and force-releases at `1.25 × ManualRangeM`, so locking on early during an approach from orbit doesn't immediately warn just for being above 12.5 km. Either mode: pressing `L` while already engaged **always manually releases immediately**, regardless of current altitude, **and suppresses auto re-engagement on that same body** until the ship either climbs back out past 12.5 km or a different body becomes nearest (Amendment 2 — without this, auto-engage would silently re-lock on the very next frame whenever `L` was pressed inside the 10 km auto range). Beyond `ManualRangeM`, `L` does nothing but the HUD still shows the out-of-range hint (decision 6). The locked body, once chosen, does not change until release, even if some other body becomes nearer meanwhile — see §3.2. |
| 6 | HUD feedback | A new status line while engaged: `SURFACE LOCK <Body> ALT <n>` (styled like the existing `ANCHOR`/`NEAREST` lines). While unlocked and within `ManualRangeM` of some body, a hint line: `L to surface-lock <Body> ALT <n>`. While locked and past that mode's warn threshold (climbing away), the status line switches to the existing `HUD_WARN_COLOR` (the same color time-warp already uses) until it hard-releases. No body within `ManualRangeM` of anything: no hint line at all (matches the original "HUD hint only" framing — here, simply the absence of the hint communicates being out of range; a hint is only useful once something is actually reachable). |
| 7 | Touchdown / crashing | **No change** — surface-lock rides entirely on the existing generic collision (`SOLFlight::ResolveSweptSphereCollision`), which already stops the ship at contact and removes inward velocity for every body, locked or not. No parked/landed state, no special touchdown handling. Manual and auto-from-orbit landing are real future capabilities, explicitly out of scope here — tracked as [`Docs/ToDo/surface-landing.md`](../ToDo/surface-landing.md) for when real terrain (Part 9) makes "landing" meaningful. |
| 8 | Planet spin | **Out of scope for this ticket.** Raised mid-grill (bodies currently don't rotate at all — no axial tilt or spin), but orientation-lock only depends on the ship-to-body-center direction, not the body's own rotation, so it isn't a prerequisite. Filed as its own follow-up: GitHub issue [`#12`](https://github.com/DieSlower/SOLTest/issues/12), to be picked up (with its own `grill-me`) after this ticket. |

## 3. Design

### 3.1 Module layout

New pure-logic module `Source/SOLTest/Level/SOLSurfaceLock.h/.cpp` (mirrors the
`Flight/SOLFlight.h` pattern: plain structs and free functions, double precision,
zero UObject/engine dependency, fully unit-testable). Engine integration lives in the
existing `Ship/` files (`ASOLShipPawn`, `USOLShipSubsystem`) and
`Targeting/USOLTargetingSubsystem`, the same way flight-assist and the `M` lock do
today — no new subsystem or actor.

### 3.2 Pure-logic contract (Appendix A, 3a)

```cpp
// Tunable ranges and the roll time constant for surface-lock (SDD 4 decisions 4-5)
struct FSOLSurfaceLockParams
{
    double AutoRangeM = 10'000.0;              // 10 km: auto-engage / warn threshold
    double AutoReleaseFactor = 1.25;            // hard-release at AutoRangeM * this
    double ManualRangeMinM = 1'000'000.0;       // 1,000 km floor for the manual range
    double ManualReleaseFactor = 1.25;
    double RollTimeConstantS = 1.0;             // tau; ~5s settle from a 180 deg start
};

// Surface-lock state machine's output: what's locked (if anything), and whether it's
// past its mode's warn threshold. INDEX_NONE / bEngaged=false when not locked. The
// suppression fields are only meaningful while bEngaged is false (Amendment 2); an
// engaged result always carries bAutoSuppressed=false, SuppressedBodyIndex=INDEX_NONE.
struct FSOLSurfaceLockState
{
    int32 BodyIndex = INDEX_NONE;
    bool bEngaged = false;
    bool bManual = false;              // true if engaged via L beyond AutoRangeM, else auto
    bool bWarning = false;             // true while engaged and past this mode's warn threshold
    bool bAutoSuppressed = false;      // true after a manual L release, until cleared (Amendment 2)
    int32 SuppressedBodyIndex = INDEX_NONE;  // the body auto-engage is suppressed for, while above is true
};

namespace SOLSurfaceLock
{
    // Returns max(params.ManualRangeMinM, bodyRadiusM): the manual pre-engage range (decision 5)
    SOLTEST_API double ComputeManualRangeM(double bodyRadiusM, const FSOLSurfaceLockParams& params);

    // Advances the lock state machine by one frame (decision 5, Amendment 2). nearestBodyIndex/
    // nearestAltitudeM/nearestBodyRadiusM describe the globally-nearest body (as FindNearestBody already
    // returns) and are used only to decide whether an engage is possible; they are ignored while already
    // engaged (Amendment 1 review finding 5: this used to be a single dual-purpose radius parameter,
    // split in two here specifically so a caller can never accidentally feed the wrong body's radius into
    // a release-threshold check). While prevState.bEngaged, lockedBodyAltitudeM/lockedBodyRadiusM MUST be
    // the caller-computed altitude and radius of prevState.BodyIndex specifically (which may no longer be
    // the nearest body) - release decisions are always relative to the body actually locked, never a
    // newly-nearer one. bTogglePressed is edge-triggered (true for exactly one call on the frame L is
    // pressed). A manual release (press while engaged) sets bAutoSuppressed on the returned state, for
    // prevState.BodyIndex; while suppressed, the auto-engage path (no press, altitude <= AutoRangeM) is
    // blocked for that same body, but a press still engages it immediately. Suppression clears itself
    // (bAutoSuppressed reset to false) once nearestBodyIndex differs from the suppressed body, or altitude
    // to it exceeds AutoRangeM * AutoReleaseFactor - whichever the caller observes first.
    SOLTEST_API FSOLSurfaceLockState UpdateSurfaceLockState(const FSOLSurfaceLockState& prevState,
        int32 nearestBodyIndex, double nearestAltitudeM, double nearestBodyRadiusM, double lockedBodyAltitudeM,
        double lockedBodyRadiusM, bool bTogglePressed, const FSOLSurfaceLockParams& params);

    // Returns the direction from the body's center to the ship: the lock's target "up" (decision 4)
    SOLTEST_API FVector3d TargetUpDir(const FVector3d& shipPositionM, const FVector3d& bodyPositionM);

    // Rotates currentOrientation by the shortest-arc rotation that maps its local +Z onto targetUpDir, scaled to a
    // fraction (1 - exp(-dt/tau)) of the full angle (decision 4, Amendment 1). Forward is carried along by that same
    // rotation (not held fixed), so there is no forward-parallel-to-up singularity; returns currentOrientation
    // unchanged only when local +Z already equals targetUpDir within a tight tolerance.
    SOLTEST_API FQuat4d ApplyAlignmentCorrection(const FQuat4d& currentOrientation, const FVector3d& targetUpDir,
        double tauS, double dt);
}
```

Constants (`AutoRangeM`, `AutoReleaseFactor`, `ManualRangeMinM`, `ManualReleaseFactor`,
`RollTimeConstantS`) are also mirrored into `SOLConstants.h` as
`SOL::SURFACE_LOCK_AUTO_RANGE_M` etc., per the style guide's "one central place" rule,
the same way `SOL::MAX_SPEED_CAP_MPS` backs `FSOLFlightParams::MaxSpeedMps`.

### 3.3 Engine integration (Appendix B, 3b)

- **Input:** a new `IA_ShipLevel` Enhanced Input action bound to `L` in
  `ASOLShipPawn`'s existing mapping context (alongside `M`, `T`, etc.), wired through
  `OnToggleLevelAction` → `HandleToggleLevel()`, following the exact pattern of
  `OnMatchLockAction`/`HandleMatchLock`. Suppressed while the jump map or the speed
  panel is open or a jump is warping, same as the other flight-only bindings.
- **State ownership:** `USOLShipSubsystem` gains an `FSOLSurfaceLockState` member and
  calls `SOLSurfaceLock::UpdateSurfaceLockState` once per ship step (mirroring where
  gravity and the reference-frame refresh already happen), using
  `AnchorSubsystem->FindNearestBody()` for the nearest-body inputs and, when already
  engaged, `FVector3d::Dist(shipPositionM, registry.GetPositionM(lockedBodyIndex)) -
  registry.GetRadiusM(lockedBodyIndex)` — an **altitude above the surface**, not the raw
  center distance — for `lockedBodyAltitudeM`, matching `FindNearestBody`'s own
  definition exactly (a review of 3a caught this being under-specified; using the raw
  center distance here would force-release an Auto lock on Earth on its very first
  engaged frame, since Earth's radius alone is already 6,371 km, past the 12.5 km
  release threshold). On the frame engagement transitions false→true, it calls a new
  `USOLTargetingSubsystem::LockToBodyIndex(int32 bodyIndex)` (a small addition
  alongside the existing `ToggleFrameLock`, setting `mLockedIndex` directly to that
  body's candidate index) to satisfy decision 2. `HandleToggleLevel` just raises the
  edge-triggered `bTogglePressed` input for one step, the same way other one-shot
  toggles are threaded through today.
- **Alignment application:** after `SOLFlight::Step` computes the frame's new orientation
  (rotation from player pitch/yaw/roll input, exactly as today), if the lock is
  engaged, `SOLSurfaceLock::ApplyAlignmentCorrection` is applied on top using that
  frame's locked-body position for `TargetUpDir`, before the state is written back —
  additive to, not a replacement for, the existing rotation step. `dt` passed in must be
  the same real, `MAX_FRAME_DELTA_S`-clamped ship-step `dt` `SOLFlight::Step` itself just
  used (per substep, if the ship steps in substeps) — never a time-warp-scaled sim `dt`,
  or the correction would snap almost instantly under high warp.
- **HUD:** `SOLFlightHud` reads the ship subsystem's `FSOLSurfaceLockState` each frame
  to draw the status/hint/warning line from decision 6, reusing `SOLHudFormat` for the
  distance formatting exactly like the existing `NEAREST` line.
- **Testing:** `Source/SOLTest/Tests/SurfaceLockTest.cpp` (3a, pure logic, TDD as
  usual: state-machine transitions for every branch in decision 5, the antiparallel
  fallback, exponential convergence over repeated steps). A new `-SOLSmokeLevel`
  command-line flag (`SOL::CommandLine`) drives a scripted approach-and-lock,
  climb-and-release, and manual-engage-from-orbit sequence through the real Enhanced
  Input pipeline, logging `PASS`/`FAIL` checkpoints to `LogSOL` and taking screenshots,
  matching every other `-SOLSmoke*` script.
- **Two things flagged by the 3a re-review for 3b to handle, not bugs in 3a itself:**
  (1) if `M` moves the reference frame off the locked body while surface-locked, the
  body-to-ship direction can jump a large angle in one frame under high time-warp,
  which the corrector isn't designed to track smoothly — 3b should watch for this in
  the `-SOLSmokeLevel` script (fly locked, warp up, press `M` to a different target)
  and decide whether to suspend alignment in that combination or accept the lag;
  (2) the suppression latch (decision 5) is only advanced on frames
  `UpdateSurfaceLockState` actually runs — if 3b skips calling it while the jump map is
  open or a warp is playing, a suppressed lock could still read as suppressed after a
  jump lands back near the same body; simplest fix is to keep calling the update every
  ship step regardless (it's cheap), or explicitly clear the latch in
  `USOLJumpSubsystem`'s arrival path.

## Amendments (post-3a-review, 2026-09-30)

3a's first implementation pass (roll-only correction, no suppression latch) went
green against its own test suite, but a fresh adversarial review caught two real
bugs before commit — both required a user decision, resolved during a follow-up
`grill-me` exchange the same day.

**Amendment 1 — alignment is a minimal whole-orientation rotation, not a roll-only
correction.** The original decision 4 rotated only about the ship's local forward
axis, explicitly to leave pitch/yaw untouched. That has a real singularity: when the
nose points near the local zenith or nadir (forward nearly parallel to the target up
direction), no roll can express the correction, so the corrector held still right up
to a ~0.1° cutoff and then snapped to nearly full-strength the instant the nose moved
past it — a visible jolt, and one that recurs every time the ship's attitude crosses
that cutoff, not just once. The user's own framing resolved it: *"There should not be
a 'pole' calculation... the whole planet should be treated as a sphere with no
specific pole. The ship should keep facing the direction it was travelling in before,
tangent to the down vector, no matter what lat/lon it's over."* That is exactly what a
minimal (shortest-arc) rotation from the current up to the target up gives for free:
it has no preferred axis, degrades gracefully everywhere except an almost-never-
occurring exact 180° flip, and carries heading along as a byproduct (parallel
transport) instead of trying to hold it exactly fixed. Decision 4 above reflects this;
`ApplyAutoRollCorrection` was renamed `ApplyAlignmentCorrection` before any commit, so
there is no shipped API under the old name.

**Amendment 2 — a manual release must suppress auto re-engagement.** The review found
that pressing `L` to release while still inside the 10 km auto range did nothing
durable: the very next frame's auto-engage check (altitude <= 10 km, no press) would
silently re-lock the same body, including re-running the reference-frame auto-match
(decision 2) a second time. The user chose the suppression-latch fix (decision 5,
`bAutoSuppressed`/`SuppressedBodyIndex` on `FSOLSurfaceLockState`) over documenting it
as a limitation, since a player pressing `L` to turn the lock off expects it to
actually stay off until they leave and come back, not for one frame.

Also folded in from the same review pass, neither needing a user decision: §3.3's
engine-integration text was corrected to specify `lockedBodyAltitudeM` as altitude
above the surface (matching `FindNearestBody`), not the raw center distance the first
draft accidentally implied (which would have force-released an Auto lock on Earth on
its first engaged frame); and the dual-purpose `lockedBodyRadiusM` parameter (nearest
body's radius pre-engage, locked body's radius post-engage) was split into two
separate, unambiguous parameters in Appendix A rather than left as a riskier single
overloaded one.

## Implementation clarifications (3b, 2026-09-30)

Engine integration landed as designed in §3.3, with these clarifications (none changes a
decision above):

- **Where the state machine runs.** `UpdateSurfaceLockState` runs once per ship step
  *after* the flight processor, not before it. Before the run, the ship still sits where
  last frame's bodies left it while the body cache already holds this frame's positions,
  which misreads the altitude by up to the body's speed times the frame time (~500 m for
  Earth at 60 fps, several km on a hitch) — enough to trip the 10 km / 12.5 km thresholds
  spuriously. The first smoke run showed exactly this (a steady 5 km hover read as
  4.81 km). After the run, ship and bodies are both at this frame's positions, so the
  altitude is exact. The consequence is one step of latency: the engage/release decision,
  the body to align to and the reference-frame match (`LockToBodyIndex`) take effect from
  the next step. The update runs every step regardless of map, speed panel or jump warp,
  which also resolves §3.3 flag (2).
- **How the alignment reaches the processor.** `FSOLShipControlFragment` gained
  `AlignBodyIndex` (INDEX_NONE = off), which the subsystem writes after each update; the
  time constant is a ship-class tunable, `FSOLFlightParams::AlignTimeConstantS` (default
  `SOL::SURFACE_LOCK_ALIGN_TIME_CONSTANT_S`), read from the const shared params fragment.
  `USOLShipFlightProcessor` applies `ApplyAlignmentCorrection`
  after each substep's collision, with `TargetUpDir` from the ship's position and the
  body's position at the substep's end, and `dt` = the substep's real length. The
  per-entity body index keeps the processor ship-agnostic for future NPC ships.
- **How `L` reaches the step.** `ASOLShipPawn::HandleToggleLevel` calls
  `USOLShipSubsystem::RequestSurfaceLockToggle`, which latches a flag the next step
  consumes and clears — the edge-triggered press is seen by exactly one step, never lost
  and never double-counted, however the pawn tick and the step interleave.
- **Jump arrival.** `USOLJumpSubsystem::CompleteJump` calls
  `USOLShipSubsystem::ClearSurfaceLock` (full release, suppression latch cleared, pending
  press dropped) alongside the existing target / M-lock clear, before the teleport.
- **§3.3 flag (1), `M` moving the frame off the locked body under high warp:** accepted,
  not suspended. The ship is only carried with its reference frame, so once the frame is
  not the locked body, warp moves that body away from the ship at (warp - 1) times the
  two frames' relative speed, and the lock hard-releases quickly (at high warp within a
  frame or two) once the altitude passes the release threshold; there is no prolonged
  period of mis-tracking to suspend. At low warp the direction changes slowly
  and the corrector tracks it normally. Not scripted in `-SOLSmokeLevel`.
- **HUD.** The line sits directly under `NEAREST`; the hint uses the dim HUD color, the
  engaged status the normal text color, and the warning `HUD_WARN_COLOR`. The key-hint
  line lists `L surface-lock` (the HUD hint line is the in-game controls text).

## 4. Open questions

None deferred. Planet spin (decision 8) and future landing are tracked separately
(issue #12 and `Docs/ToDo/surface-landing.md`), not open questions of this ticket.

## 5. Revision history

- 2026-09-30: initial decisions from the `grill-me` session; SDD 1's Level (L) row
  updated to match.
- 2026-09-30: post-3a-review amendments (see "Amendments" above) — alignment math
  changed from roll-only to minimal-rotation (`ApplyAlignmentCorrection`), a
  suppression latch added for manual release, the engine-integration altitude
  definition corrected, and the radius parameter split in two. All resolved before
  3a's first commit, so no roll-only or dual-purpose-parameter code ever shipped.
- 2026-09-30: 3b engine integration built; see "Implementation clarifications (3b)".
