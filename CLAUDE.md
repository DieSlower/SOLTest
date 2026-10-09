# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

SOLTest — a solar-system space simulation by Acid Rain Studios LLC. Built with **Unreal Engine 5.8** in **C++** (module `SOLTest`, `SOLTest.uproject`), Windows. The player flies a small ship in third person across a 1:1-scale solar system with realistic orbits, jumps between regions from a map, levels to planet surfaces, and shoots targets. It is built for massive scale (MassEntity for everything, ~1M entities as the long-term goal) using current UE features (Mass, Niagara, Nanite, PCG, Chaos, Enhanced Input) and no deprecated APIs.

The cross-cutting design decisions are in [`Docs/SDDs/1-solar-system-architecture.md`](Docs/SDDs/1-solar-system-architecture.md), and the roadmap is in [`Docs/Plans/1-solar-system-architecture-plan.md`](Docs/Plans/1-solar-system-architecture-plan.md). Read them before designing anything.

## Workflow rules

- **Always invoke the `grill-me` skill before starting any new feature or any large change to existing functionality.** "Large change" means anything that touches multiple files or modules, alters the contract of a public C++ API or reflected type (`UCLASS` / `USTRUCT` / `UFUNCTION` / `UPROPERTY`), modifies persisted data (a `USaveGame` schema or config/settings layout), or changes user-visible behavior or controls in a non-trivial way. Trivial bug fixes, typo corrections, and one-line tweaks are exempt.
- Run `grill-me` *after* `superpowers:brainstorming` (if brainstorming applies) and *before* writing any implementation code or a written SDD. Use it to stress-test the design by walking down each branch of the decision tree one question at a time, with a recommended answer attached to each question.
- **`grill-me` MUST run before the SDD is drafted, and every resolved decision from `grill-me` MUST be incorporated into that SDD.** Do not commit a design to disk until grilling is complete. If a grill-me question was deferred rather than answered, call that out explicitly in the SDD as an open question.
- **When `grill-me` is running, every decision the user has not concretely specified MUST be put to the user — never decided unilaterally.** If there is a decision to be made and the user has not already given an explicit answer to it, ask. Attach a recommended answer to each question, but the user makes the call. "Sensible default", "obvious choice", and "I'll just pick the common one" are not grounds to skip asking during a grill — surface the decision and let the user decide.
- If the user explicitly says "skip grilling" or "don't grill me" for a specific task, honor that — user instructions take precedence over this rule.
- **Ask every question through the `AskUserQuestion` tool (the GUI question widget), never as plain text in a reply.** This applies to all questions to the user, not just `grill-me` ones: design choices, "which way do you want it?" after a finding, confirmations, anything. Put the recommended answer first, labeled "(Recommended)".
- **Never answer a question on the user's behalf, and never carry on as if it were answered.** When a question to the user is open, stop and wait for their reply; do not time out, guess, or keep working past it. This rule is only about questions. Resuming already-approved work after a usage-limit pause (via a one-shot `CronCreate`, per the global CLAUDE.md) is still allowed, but never past a pending question.
- **Build each roadmap part with its own subagent, and keep that subagent's context under ~25%.** If it runs over, continue with a fresh subagent that reads the SDD, plan and progress notes cold. Parts must be small enough for Claude to verify without the user; the user tests only the finished product.
- **On a usage-limit error:** save resume state (task list, files mid-edit, exact next step), read the reset time from the error message (fallback: +5h10m for the 5-hour window, +7d10m for the weekly one), and schedule a one-shot `CronCreate` resume. There is no reliable way to poll usage, so do not run a "usage checker" agent.
- **Pause points while building (context and usage limits):**
  - **Context above 35%:** when the main session's context passes 35%, stop at the next good breakpoint (a finished task or part, never mid-edit or mid-subagent-dispatch). Save resume state (task list, files mid-edit, exact next step) in the plan/progress notes so a fresh context can continue cold.
  - **5-hour session limit:** pause when it is hit, then continue automatically once it resets (one-shot `CronCreate` resume, per the rule above).
  - **70% of the 7-day limit:** pause proactively at a good breakpoint and do not resume until the 7-day window resets. Read the figure from `~/.claude/rate-limits-cache.json` (`seven_day_used_pct`, `seven_day_resets_at`; check `written_at` for freshness) before starting and after finishing each task. Schedule the resume with a one-shot `CronCreate` at `seven_day_resets_at`.
  - As above, none of this authorizes proceeding past an open question to the user.

## Subagents — model selection

- **Always evaluate which model a newly dispatched subagent needs, and assign the best model for that specific job — never dispatch on autopilot.** Subagent model selection follows the precedence override > agent-definition frontmatter > inherit-from-parent; passing the `model` field on the `Agent` call is the per-dispatch override and is the lever to use here. The goal is to match capability to task, not to default everything to the parent's model.
- **Hard / reasoning-heavy work gets the smartest model.** Code design, architecture, multi-file refactors, logic-dense implementation, test authoring against a contract (TDD), and adversarial performance/style reviews are reasoning jobs — dispatch these on the strongest tier, `opus`. Do not dispatch `fable`: it is not usable as a subagent model here and silently falls back to a lesser tier.
- **Moderate implementation work** — straightforward single-file changes, routine component or asset wiring, ordinary code edits — fits a mid tier: `sonnet`.
- **Simple / mechanical work gets a cheaper, faster model.** Finding a string across files, listing where a symbol is used, fetching/searching the web for a fact, dumping a value, or other low-judgment lookups don't need a frontier model — dispatch these on `haiku` to save cost and latency.
- **Apply best judgment to the task in front of you.** These are guidelines, not a fixed table — weigh how much reasoning, code understanding, and design judgment the task actually demands, and pick the cheapest model that will still do the job well. When genuinely unsure, err toward the more capable model so quality isn't sacrificed.

## Build & Run

- **Engine:** `C:\Program Files\Epic Games\UE_5.8`.
- **Build the editor target:** `"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" SOLTestEditor Win64 Development -Project="D:\Development\UnrealProjects\SOLTest\SOLTest.uproject" -WaitMutex`. Close the Unreal Editor first (a running editor locks the module DLL, and Live Coding is not relied on).
- **Run the editor:** launch `C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe "D:\Development\UnrealProjects\SOLTest\SOLTest.uproject"`.
- **Run the tests from a terminal:** `Tools\RunTests.bat` (or `.\Tools\RunTests.ps1`); see [Testing](#testing).
- **To use unreal-mcp, first start the Unreal Editor with the Rider run command (the editor run configuration in Rider), not by launching `UnrealEditor.exe` directly.** unreal-mcp starts automatically with the editor; no `/mcp` from the user is needed, so don't ask for it — just load the tools via ToolSearch and use them.
- **unreal-mcp only works while the Editor is open.** The MCP server (`.mcp.json`, `http://127.0.0.1:33445/mcp`) is hosted inside `UnrealEditor.exe`. Claude Code resolves MCP connections once at session start, so if the editor was not running then, the connection stays failed until the user runs `/mcp` to reconnect. Workflow: build, launch the editor, wait for it to load (the port opens within ~30 s), then ask the user to run `/mcp`. Do not rely on unreal-mcp in a headless or subagent flow; prefer command-line builds and automation tests there.
- `Docs/` and `Tools/` sit outside `Content/`, so they are never cooked into a packaged build.

## Architecture

**[`Docs/ARCHITECTURE.md`](Docs/ARCHITECTURE.md) is the living high-level map** of modules, the per-frame update order, coordinate systems, key types and data flows — read it before working across more than one feature folder. The *why* behind each decision lives in the SDDs: [`Docs/SDDs/1-solar-system-architecture.md`](Docs/SDDs/1-solar-system-architecture.md) (scale and origin rebasing, Keplerian orbits, flight model, Mass for everything, star field, combat) and each part's own SDD for what it actually built. Module and file layout rules are in [`Docs/STYLE_GUIDE.md`](Docs/STYLE_GUIDE.md) §14.2: the game module is `Source/SOLTest/`, organized by feature folder with each type's `.h` and `.cpp` side by side.

**Keep `Docs/ARCHITECTURE.md` in sync**, per its own "Keeping this document in sync" section: whenever a module/subsystem/Actor/widget is added or removed, a class's responsibility or a module dependency changes, the frame update order changes, a coordinate frame/conversion changes, or a `-SOL*` command-line flag changes.

**Read [`Docs/STYLE_GUIDE.md`](Docs/STYLE_GUIDE.md) before writing or refactoring any C++.** The guide is still a draft with `Review needed` notes; where it is unsettled, follow the surrounding code and ask the user.

## Persistence — minimize disk traffic

**Standard for all persistence code (`USaveGame` slots, `UGameUserSettings`, config files): keep disk reads and writes to the minimum that still preserves the player's data.**

- **Mutate in memory freely; write at lifecycle boundaries.** A setter changes the in-memory value only. The write happens once at a natural boundary (menu close, level exit, app shutdown), not per tap, per slider step, or per frame.
- **Skip the write when nothing changed.** Compare against the stored value first.
- **Batch.** If several values change in one visit, they go out in a single save.
- **Read once.** Load at boot; everything afterwards reads the in-memory copy.
- **Prefer async saves** (`AsyncSaveGameToSlot`) so a save never hitches a frame.
- **Accepted trade-off:** a change made and not yet flushed is lost if the OS kills the app before the boundary.
- When the persisted schema changes incompatibly, version it and migrate rather than silently discarding a player's data.

## Game mechanics catalog & in-game info

[`Docs/GAME_MECHANICS.md`](Docs/GAME_MECHANICS.md) is the catalog of every mechanic, control binding and tunable value, with exact numbers and source locations. Consult it first when reasoning about gameplay. **Keep it in sync** whenever any of these change: a control binding, a flight/jump/level/weapon/time-warp value or rule, or any tunable that the player can feel.

**Keep the in-game Controls / Info / About screen truthful.** The game has an in-game menu showing what each control does and how each mechanic works. Whenever a mechanic, binding or value changes (or a new one is added), update that screen's content and `GAME_MECHANICS.md` in the same change, so the player-facing text never makes a stale or false statement.

## Software design documents (SDDs)

Every **new feature** or **major change** (the same trigger that requires TDD — a new system, module, Mass processor, widget or subsystem, or a substantive multi-file refactor or behavior change) is designed against a written **SDD**. Save it under [`Docs/SDDs/`](Docs/SDDs/) so the design is versioned alongside the code and can be diffed against the implementation during the [performance review](#performance-review--required-after-code-changes).

**Naming:** `<issue-number>-<short-content-slug>.md`, where `<issue-number>` is the GitHub issue number (branches are named after the issue, e.g. `#2`) and the slug summarizes what the ticket delivered. Example: `1-solar-system-architecture.md`. One SDD per ticket; keep it updated if the design shifts during implementation so the "compare against the SDD" step stays meaningful. See [`Docs/SDDs/README.md`](Docs/SDDs/README.md).

**A written plan is required alongside the SDD — both are saved to disk, neither is optional.** Once the SDD is agreed, save the implementation plan under [`Docs/Plans/`](Docs/Plans/) named `<issue-number>-<slug>-plan.md`. Like the SDD, the plan is a living document: keep it in sync as steps get done or reordered. **Do not skip either artifact on your own judgment** — if you think a change is too small to warrant an SDD and a plan, ask the user first and get explicit confirmation before proceeding without them.

## Research and future work

- [`Docs/Research/`](Docs/Research/) holds research reports that support decisions (e.g. `UE_PERFORMANCE_GUIDELINES.md`, `STAR_FIELD_CELESTIALVAULT_AND_ATHYG.md`).
- Ideas and follow-up work that are identified but not yet scheduled as a ticket live under [`Docs/ToDo/`](Docs/ToDo/), one file per item, no issue number (see [`Docs/ToDo/README.md`](Docs/ToDo/README.md)). This is distinct from **Known tech debt to revisit** below: tech debt is imperfections in code that already exists, ToDo items are new capabilities that haven't been built. When an item is picked up, it graduates into a normal ticket — write its SDD as usual (`grill-me` still applies) — and the ToDo file is deleted.

## Testing

Tests use the **UE Automation Framework** in the `SOLTest` module:

- Test source lives under `Source/SOLTest/Tests/`, one file per topic (`<Topic>Test.cpp`). Test paths are named `SOLTest.<Topic>.<Case>` so `-Filter SOLTest.<Topic>` runs one topic.
- **Command-line runner:** `Tools\RunTests.bat` (or `.\Tools\RunTests.ps1`). Options: `-Filter <prefix>` (default `SOLTest`), `-Build` (build the editor target first), `-List` (list matching tests), `-ShowLog` (stream the engine log), `-Rhi` (real RHI for rendering tests). It runs `UnrealEditor-Cmd` headless, prints a pass/fail summary, and exits 0 (all passed), 1 (failures) or 2 (could not run / no results). It refuses to run while the editor has the project open.
- **Results are in `Saved/AutomationReports/index.json`** (engine log in `Saved/Logs`). Read that file for results instead of asking the user to relay them.
- **Visual and gameplay parts also get a PIE smoke check** (play in editor, screenshot, log check via unreal-mcp) when the editor is open; note in the SDD/plan when a part could not be smoke-checked.

### Required: tests for new and modified code

- **New features.** Plan the test file alongside the implementation, in `Source/SOLTest/Tests/`.
- **Modified classes.** Any change — refactor, behavior change, bug fix — requires either adding tests covering the change or extending existing ones.
- **Re-validate the existing test file when modifying a tested class.** Walk every existing test, confirm it still reflects the current contract, add tests for new code paths, and delete tests whose target no longer exists. A green run alone is not sufficient — stale tests that happen to pass are a regression risk.

### Test-Driven Development for new features and large changes

Whenever you build a **new feature** (a new system, module, processor, subsystem or widget) or make a **large change** (substantive refactors, new public API on an existing class, behavior changes spanning multiple files), use **TDD**:

1. **Write the failing test first** in the appropriate `Source/SOLTest/Tests/<Topic>Test.cpp`. State the expected behavior as assertions before the production code exists.
2. **Run it with `Tools\RunTests.bat -Build`** and confirm it fails for the right reason (missing function, wrong value), not a typo or build error.
3. **Write the minimum implementation** to make it pass.
4. **Re-run** and confirm green, then refactor with the test as a safety net.

**The test file in step 1 must be authored by a dispatched subagent, not inline in the main session.** Brief the subagent with the contract under test (inputs, outputs, edge cases), the target test file path, and the automation-test macros in use. Do NOT show it the production implementation — the tests must be written against the spec, not retrofitted to whatever the code does. The main session then handles steps 2–4. The same rule applies when *extending* an existing test file as part of a large change. Small bug fixes and trivial tweaks may use test-after.

## Performance review — required after code changes

Once code is written, it **must be reviewed by a fresh, adversarial `opus` subagent — a separate context from the one that wrote the code — for performance/optimization _and_ style-guide conformance** before the change is considered done. The bar is scalability to ~1M entities at frame rate, not just correctness. Brief the subagent with the changed files, the SDD, [`Docs/STYLE_GUIDE.md`](Docs/STYLE_GUIDE.md), and [`Docs/Research/UE_PERFORMANCE_GUIDELINES.md`](Docs/Research/UE_PERFORMANCE_GUIDELINES.md) (the full Epic and Tom Looman checklist); have it report findings rather than edit, so the main session applies fixes.

The review must check at least the following (the research file has the complete list):

1. **Tick and update cost.** Tick is opt-in (`bCanEverTick = false` by default); no per-frame work that could be event-driven or batched; Mass processors instead of per-Actor ticking.
2. **Allocation and GC.** No heap allocation, `NewObject`, string building, or `TArray` growth inside Tick or a Mass processor's hot loop; `Reserve` up front; every `UObject*` member is a `UPROPERTY` `TObjectPtr`; avoid UObject churn.
3. **Data layout.** Structure-of-arrays / Mass fragments, cache-friendly iteration, no Actor-per-datum simulation, pooled bolts/targets/effects instead of spawn-destroy.
4. **Lifecycle cleanup.** Delegates, timers and subscriptions bound in `BeginPlay` are unbound in `EndPlay`; nothing keeps running after teardown.
5. **Threading.** Parallelizable work uses `ParallelFor` / Mass parallel processors; game-thread-only calls stay on the game thread.
6. **Rendering.** Niagara and GPU-driven effects over CPU sprites; Nanite and LOD used where applicable; minimal draw calls and material cost; instancing over Actors.
7. **Physics/collision.** Cheap channels and shapes; swept traces batched; no complex collision for gameplay queries.
8. **Numerics.** Double precision for universe coordinates; origin rebasing and anchor hysteresis behave per SDD 1; no float truncation of universe positions.
9. **Profiling evidence.** Claims about cost are backed by `stat` commands or Unreal Insights, not guesses.
10. **Style-guide conformance.** New and changed code follows every applicable rule in `Docs/STYLE_GUIDE.md` (banner, formatting, naming, comment rule, asset paths in one place, UE reflection rules). Flag deviations in new/changed code; don't require rewriting untouched legacy code.

**When the review finds something below this bar, fix it, then close the loop:** re-check the change against its SDD to confirm the fix didn't change intended behavior, then **re-run the full test suite** (`Tools\RunTests.bat`) and confirm every test still passes before treating the work as complete.

## Git

**Commit and push once a part is done.** Once a roadmap part is implemented, its tests pass, and the required adversarial performance/style review's findings are fixed (or explicitly accepted as tech debt), stage the part's changes, commit on `master` (this project's established pattern — no per-part feature branches), and push to `origin`. Commit message convention (match the existing log): `Issue #<N>: Part <slug> - <short description>` (`<N>` is the GitHub issue number — Part N's own ticket — not the part letter), e.g. `Issue #6: Part 5e-iii - ring visuals and far-field Niagara`. End the message with the attribution line the session's own system reminder specifies.

*(Superseded 2026-10-07: the previous rule here was "never run `git add`/`git commit`, leave it to the user" — the user explicitly asked for this to change after noticing parts were going uncommitted. If this instruction and a stricter verbal one conflict in a future session, the verbal one wins; update this file to match rather than silently reverting.)*

Read-only git operations (`git status`, `git diff`, `git log`, `git show`, `git branch`) may be run freely, by the main session or a dispatched subagent. Destructive/history-rewriting operations (force-push, reset --hard past a shared commit, rebase of pushed history) still require asking first.

## Conventions

- **Copyright header:** every new `.h` / `.cpp` / `.cs` / `.usf` file starts with the banner in `Docs/STYLE_GUIDE.md` §1 (`SOLTest` / `Copyright © 2026 Acid Rain Studios LLC`); docs use `<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->`.
- **Comments:** every function (including file-local helpers) gets a single-line `//` header above it describing what it does. Any non-trivial block longer than ~5 lines inside a function also gets a brief inline comment.
- **Asset and content paths** are never hardcoded in gameplay code; they live in one central place (`Source/SOLTest/SOLConstants.h` or data assets), per `Docs/STYLE_GUIDE.md` §7.
- **Tests required for changes** — see [Testing](#testing).
- **SDD and plan for new features and major changes** — see [Software design documents (SDDs)](#software-design-documents-sdds).
- **TDD for new features and large changes** — see [Test-Driven Development](#test-driven-development-for-new-features-and-large-changes).
- **Performance review after code changes** — see [Performance review](#performance-review--required-after-code-changes).
- **Keep `Docs/GAME_MECHANICS.md` and the in-game info screen in sync** — see [Game mechanics catalog & in-game info](#game-mechanics-catalog--in-game-info).

## Known tech debt to revisit

Items the team is aware of but hasn't scheduled yet. Don't treat as blockers, but keep in mind when working in the area:

- `Ship/` and `UI/` include each other's headers (`ASOLShipPawn` and `ASOLFlightHud`), found while writing `Docs/ARCHITECTURE.md`. It builds fine today, but is worth breaking (e.g. a small shared interface or event) before either file grows further.
- `USOLAnchorSubsystem::OnUniverseUpdated` has multiple listeners (`ASOLBodyVisuals`, the pawn) with no enforced order; found while writing `Docs/ARCHITECTURE.md`. Neither adversarial review flagged an actual ordering bug, but if a future listener depends on another having already run this frame, that dependency needs to be made explicit rather than relying on bind order.
- `SOLMapBodyLod::ComputeApparentDiameterPx` recomputes the camera's focal length (a `tan` and a divide) on every call; found in the Part 2c adversarial review. Negligible today (a handful of map bodies, once per frame), but if a Mass-scale user of this function shows up (e.g. Part 10's scale demo), precompute the focal length once per frame and pass it in instead.
- `SOLBodyRotation::ComputeOrientation` recomputes `TiltRotation` (a `SinCos` and a `RotateVector`) every call even though a body's tilt never changes after `PopulateSolarSystem` — only its spin angle does; found in the issue #12 adversarial review. Negligible at 9 bodies. If Part 5's moons/asteroids or Part 10's scale demo make this a hot path, cache each body's tilt quaternion once and compose `tilt * FQuat4d(+Z, spinAngle)` per call instead (equivalent, since `pole = tilt·Z`), and consider precomputing `2*PI / (periodH*3600)` per body to drop a divide.
- The Sun's GM (`1.32712440018e20`) is a repeated literal across `SOLBodyRegistry.cpp`, `SOLAsteroidBelt.cpp`, `SOLMinorBodyOrbitProcessor.cpp`, `Tests/SOLTestHelpers.h` and `Tests/BodyRegistryTest.cpp`; found in the issue #6 Part 5b/5c adversarial reviews. No `SOL::SUN_GM` constant exists yet in `SOLConstants.h`. Harmless today (all copies agree, and for `SOLMinorBodyOrbitProcessor.cpp` specifically a minor body's position never actually depends on the GM value passed), but if it ever needs correcting, every copy must be found and changed by hand. Worth a small follow-up: add `SOL::SUN_GM` to `SOLConstants.h` and migrate the production call sites (test files may reasonably keep their own independently-typed copy, per this project's test-independence convention).
- `ASOLAsteroidBeltVisuals`'s belt mesh (the default sphere) has no LOD or cull-distance/minimum-screen-size setting, even though almost every one of its ~9,268 instances is sub-pixel from any reasonable viewing distance per `SOLRender::ComputePlacement`'s true-angular-size placement; found in the issue #6 Part 5c adversarial review. Likely wastes GPU time on invisible sub-pixel triangles. Cheap fix whenever visual polish for minor bodies is scheduled (same umbrella as the deferred appearance/material work from 5a): a lower-poly mesh or an LOD/cull-distance setting on the ISM components.
- `USOLMinorBodyOrbitProcessor` recomputes every minor body's full orbital basis (`pHat`/`qHat`, etc., inside `SOLKepler::ElementsToState`) from scratch every frame, even though every minor body's elements have zero rate terms except `LDotDegPerCy`, meaning that basis is actually constant per entity after spawn; found in the issue #6 Part 5c adversarial review. Negligible at ~9,268 entities. If a future scale-up (more clusters, rings, or Part 10's demo) makes this a hot path, cache each entity's constant orbital-basis values at spawn and only recompute the mean-anomaly-dependent part per frame.
- The asteroid belt's per-frame ISM bulk-transform-update approach (CPU computes every instance's transform, then uploads the whole array to the GPU every frame) does not scale toward Part 10's ~1M-entity goal; found in the issue #6 Part 5c adversarial review, after fixing the `bMarkRenderStateDirty` scene-proxy-rebuild bug (see SDD 6 Amendment 3). At the current ~9,268 instances this is believed (not yet profiled) to be acceptable. A genuinely scalable version would move the per-frame position solve to the GPU (vertex shader/WPO or Niagara), uploading each entity's fixed orbital elements once at spawn instead of a transform every frame.
- `USOLRingSubsystem`'s ~10,000 ring-rock pool entities (4 rings × 2,500) match `USOLMinorBodyOrbitProcessor`'s query unconditionally, so they get a full Kepler solve every frame even when the player is nowhere near any ring, roughly doubling the minor-body processor's per-frame workload against the belt's own ~9,268; found in the issue #6 Part 5e-ii adversarial review. Not yet profiled (unlike the belt's ~9,268 figure, which has real `stat`/Insights evidence from SDD 6 Amendment 5) — captured here rather than guessed at. If this turns out to matter, the fix is a Mass tag (e.g. `FSOLRingRockIdleTag`) excluded from the orbit processor's query, added/removed via the command buffer only when a group transitions between unset and assigned (rare), not every frame.
- `USOLRingSubsystem::AssignGroup`/`SOLRingPatch::GenerateCellRocks` allocate a fresh ~10 KB `TArray` every time a group is reassigned; found in the issue #6 Part 5e-ii adversarial review (the same question Amendment 7 explicitly deferred to 5e-ii "once a real caller exists to validate the right shape against" — now one does, and this is that re-evaluation, landing on "document, don't fix yet"). At a rough estimate (Saturn's B ring, a stationary player relative to the planet still crosses ~17.8 cells/second as the co-rotating grid sweeps past), reassignment can run up to ~90 times/second per active ring (~900 KB/s), and a warp-speed pass through a ring could regenerate all ~25 groups in one frame. Not yet profiled. If this shows up as real GC/allocator pressure, change `GenerateCellRocks` to write into a caller-provided, reserved-once scratch `TArray<FSOLRingRockDef>&` instead of returning a new one (touches its existing tests too).
- `USOLRingSubsystem::UpdateRings` and `USOLMinorBodySubsystem::UpdateOrbits` both bind to `USOLAnchorSubsystem::OnBodiesUpdated`, whose listener order is bind-order-dependent and not guaranteed (same class of issue as the `OnUniverseUpdated` item above); found in the issue #6 Part 5e-ii adversarial review. Today's bind order happens to put the ring update before the shared orbit-processor run, which is correct, but only by accident of subsystem dependency-initialization order. If this ever flips, a just-reassigned group would show its previous cell's rocks for one extra frame — harmless today (no renderer exists yet to show it), but worth an explicit ordering guarantee before 5e-iii's renderer makes a one-frame-late pop-in visible.
- **`Tools\RunTests.ps1`'s headless `UnrealEditor-Cmd -ExecCmds="Automation RunTests ..."` path is currently broken on this machine**: it reproducibly stalls right after `CleanupOrphanedCacheFiles`/DDC maintenance, before any automation-related log line appears, with no error (CPU usage keeps climbing, so it's not a simple deadlock); found during issue #6 Part 5e-iii Amendment 13 verification. Pausing the VPN and closing Rider (both plausible RiderLink/`ModelContextProtocol` interferers) did not change the stall point; running with a real RHI instead of `-nullrhi` got one line further (a `LogPSOHitching` line) before stalling again. Root cause not found. **Until fixed, verify tests via the already-open interactive Editor instead**: `mcp__rider__ue_execute_python` to run the `Automation RunTests <filter>` console command, then `mcp__rider__ue_get_logs`/`ue_status` polling for `LogAutomationController` `Test Started`/`Test Completed` lines (see SDD 6 Amendment 13 for the full writeup). The interactive path's automation pipeline itself works correctly — only the headless command-line path is broken.
- Each of the four `ASOLRingVisuals` actors owns its own `NS_SOLRingDense` component (1M GPU particles), activated on its first show and then kept resident (paused and hidden away from its ring); found in the issue #6 Part 5e-v adversarial review and **measured** (SDD 6 Amendment 16 addendum): about 214 MB of particle buffers per ring ever visited (plus one-time shared GPUScene growth of about 112 MB), so visiting all four rings holds about 640 MB, and the layer costs about 3.3 GPU ms per frame while rocks are around the camera, 1.8 ms of it the velocity pass. Fixes in order of payoff: stop the opaque rocks being drawn into the velocity pass; one shared dense component re-parameterised for the active ring (caps VRAM at one set); deactivating a layer after it has been hidden a while (re-showing respawns the rocks, about 1 ms on one frame).
- The ring dense rock layer masks only the 3 `AllGapBandsM` gaps and uses one fill fraction per ring, while the far disc's material uses a 52-band optical-depth table, so in e.g. Saturn's C ring and the Cassini Division's ringlets the rock carpet is denser than the translucent disc beside it; found in the issue #6 Part 5e-v adversarial review. Fix would be to feed the carpet the same radial profile (a profile texture or the same table).

Add new items here as they're discovered rather than letting them live only in commit messages or memory.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
