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
- **Ask every question through the `AskUserQuestion` tool (the GUI question widget), never as plain text in a reply.** Put the recommended answer first, labeled "(Recommended)".
- **Never answer a question on the user's behalf, and never carry on as if it were answered.** When a question to the user is open, stop and wait for their reply; do not time out, guess, or keep working past it. This rule is only about questions. Resuming already-approved work after a usage-limit pause (via a one-shot `CronCreate`, per the global CLAUDE.md) is still allowed, but never past a pending question.
- **Build each roadmap part with its own subagent, and keep that subagent's context under ~25%.** If it runs over, continue with a fresh subagent that reads the SDD, plan and progress notes cold. Parts must be small enough for Claude to verify without the user; the user tests only the finished product.
- **On a usage-limit error:** save resume state (task list, files mid-edit, exact next step), read the reset time from the error message (fallback: +5h10m for the 5-hour window, +7d10m for the weekly one), and schedule a one-shot `CronCreate` resume. There is no reliable way to poll usage, so do not run a "usage checker" agent.

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
- **unreal-mcp only works while the Editor is open.** The MCP server (`.mcp.json`, `http://127.0.0.1:33445/mcp`) is hosted inside `UnrealEditor.exe`. Claude Code resolves MCP connections once at session start, so if the editor was not running then, the connection stays failed until the user runs `/mcp` to reconnect. Workflow: build, launch the editor, wait for it to load (the port opens within ~30 s), then ask the user to run `/mcp`. Do not rely on unreal-mcp in a headless or subagent flow; prefer command-line builds and automation tests there.
- `Docs/` and `Tools/` sit outside `Content/`, so they are never cooked into a packaged build.

## Architecture

The architecture is defined in [`Docs/SDDs/1-solar-system-architecture.md`](Docs/SDDs/1-solar-system-architecture.md) (scale and origin rebasing, Keplerian orbits, flight model, Mass for everything, star field, combat). Module and file layout rules are in [`Docs/STYLE_GUIDE.md`](Docs/STYLE_GUIDE.md) §14.2: the game module is `Source/SOLTest/`, organized by feature folder with each type's `.h` and `.cpp` side by side. Add a concise layout summary here once the first parts have landed.

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

**Never run `git add` or `git commit`.** Do not stage or commit changes under any circumstances — leave that to the user. Read-only git operations (e.g. `git status`, `git diff`, `git log`, `git show`, `git branch`) may be run freely by dispatched subagents; the main session runs them only when the user explicitly allows it, so ask first.

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

- `Source/SOLTest/Universe/SOLTypes.h` and `SOLKepler.h` were started by Part 1a before it was paused; they are untested and their naming predates the style guide's decisions (see the `Review needed` notes in `Docs/STYLE_GUIDE.md`). Re-validate them when Part 1a resumes.

Add new items here as they're discovered rather than letting them live only in commit messages or memory.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
