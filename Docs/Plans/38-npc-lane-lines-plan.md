<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 38: NPC lanes as one animated hologram line per lane, with an overlay-menu toggle

SDD: [`SDDs/38-npc-lane-lines.md`](../SDDs/38-npc-lane-lines.md). Issue
[#38](https://github.com/DieSlower/SOLTest/issues/38). Decisions D1-D24 are in SDD section 2 (grill-me D1-D6; the
draft's Q1-Q18 answered by the user on 2026-11-11 as D7-D24, with the keep-right rule added to D2). No question is
pending; the build starts at step 2.

**Schema change (D1, approved):** `SOL::Settings::SAVE_VERSION` 12 -> 13 (the settings save's one version constant);
**no** UPROPERTY added or removed; each used profile's existing row map (`FSOLOverlayProfileSave::Overlays`) gains the
key `"Lanes"` (bool) through a frozen 12 -> 13 migration step that adds `"Lanes" = true` to every **non-empty** map
(unused slots keep their empty map) and keeps every other key, flag and field. `KEY_BINDING_LAYOUT_VERSION` stays 10;
no key binding, override or key-layout change. If the work shows a binding is needed, **stop and report** (D1). The
persistence subagent reports exactly which version constants, fields and map keys it changed; the main session checks
the report against SDD section 4.9 before treating the step as done.

## Commit parts (three, in this order)

Convention `Issue #38: Part 38x - <description>`. Each part is committed and pushed on `master` only once its tests
pass, its smokes pass and its adversarial performance and style review's findings are fixed (CLAUDE.md "Git").

1. **38a - lane math, the Lanes row and schema 13** (steps 2-5): `SOLNpcLaneMath` (pure), `EOverlay::Lanes`, the
   12 -> 13 migration, the Controls / Info text. The row exists and persists; nothing is drawn yet (the row has no
   visible effect until 38b; the Info sentence lands in 38b with the renderer so the text is never false).
2. **38b - renderer** (steps 6-9): `M_SOLNpcLanes`, the lane ribbon in `ASOLNpcVisuals`, the far-point crossfade, the
   glow feed, the shared occluders, `sol.NpcLanes`, `-SOLSmokeNpcLanes`, the Info text.
3. **38c - tuning, verification and docs** (steps 10-14): screenshots and tuning, profiling, every doc, the player
   guide, README gallery; whole-ticket review; closes #38.

## Files to change

| File | Change | Part |
|---|---|---|
| `Source/SOLTest/SOLConstants.h` | `SOL::Settings::SAVE_VERSION` 13 and its comment; `NPC_LANE_*` constants (SDD section 6 case 1, plus `NPC_LANE_EMISSIVE`, `NPC_LANE_TRANSLUCENCY_SORT_PRIORITY`); `SOL::Paths::NPC_LANES_MATERIAL` | 38a (constants), 38b (path, emissive) |
| `Source/SOLTest/Npc/SOLNpcLaneMath.h` / `.cpp` (new) | `SOLNpcLane`: fade, point alpha, traffic weight, width, brightness, offset, ribbon extent, profile, flow phase, dash phase and coverage, closest param, distance, side normal, `CountLaneShips`, `FSOLNpcLaneFrame`, `BuildLaneFrame` | 38a |
| `Source/SOLTest/UI/SOLOverlayModel.h` / `.cpp` | `EOverlay::Lanes` (after `ShipsOnRadar`, D17) and its `OVERLAY_DEFS` row | 38a |
| `Source/SOLTest/Menu/SOLSettingsSave.cpp` | Frozen migration step 12 -> 13 (non-empty maps only, literal `"Lanes"`) | 38a |
| `Source/SOLTest/Menu/SOLMenuText.h` | `@OverlayMenu` Controls row (38a); Info overlay and Demo sentences (38b) | 38a, 38b |
| `Source/SOLTest/UI/SOLOverlayMenuWidget.*` | Only if a row-count assumption is found (rows come from the table; `ROW_WIDGET_COUNT` follows `OVERLAY_COUNT`) | 38a |
| `Source/SOLTest/UI/SOLOverlaySmoke.*` | Lanes row checks (SDD section 8 item 4) | 38a (row), 38b (renderer) |
| `Source/SOLTest/Npc/SOLNpcVisuals.h` / `.cpp` | Lane histogram in `FillRailTexels`, `FSOLNpcLaneFrame` built per frame (`GetLaneFrame()`), group texel `.w` lane data, `LaneLines` component and `BuildLaneMesh`, lane MID, `NpcParams4` / `NpcLaneParams0`, `FSOLGlowScaleFeed`, settings reads, verification override, `sol.NpcLanes` | 38b |
| `Shaders/Private/SOLNpcLane.ush` (new) | HLSL mirror of `SOLNpcLaneMath` (fade, closest param, profile, dash coverage), including `SOLOrbitLine.ush` for placement, width and occlusion | 38b |
| `Tools/Npc/create_npc_assets.py`, `Content/SOL/Npc/M_SOLNpcLanes.uasset` (new), `M_SOLNpcPoints.uasset` | New lane material; point material crossfade (kind 1) and `NpcParams4`; rebuilt with `-SOLRebuild`, editor closed | 38b |
| `Source/SOLTest/OrbitLines/SOLOrbitLineVisuals.*` (+ `SOLOrbitLineLogic` if a rule moves) | Occluders written while orbit lines **or** lanes draw (D24) | 38b |
| `Source/SOLTest/Npc/SOLNpcLaneSmoke.*` (new) or in `Npc/`, `SOLConstants.h` `SOL::CommandLine` | `-SOLSmokeNpcLanes` (SDD section 8 item 2), wired where the other smokes start | 38b, 38c (screenshot views) |
| Tests: `NpcLaneMathTest.cpp` (new), `OverlayModelTest.cpp`, `SOLSettingsRulesTest.cpp`, `SOLSettingsModelTest.cpp`, `FlightModeMathTest.cpp`, InfoText test | SDD section 6 cases 1-24; schema 12 -> 13 in existing assertions; row count 15 -> 16 and Darkening index; re-validation of every existing case in the touched files | 38a (24 in 38b) |
| `Docs/GAME_MECHANICS.md` | Lanes overlay row, section 9 "Lane lines" row, Render tiers row, schema note | 38c |
| `Docs/SDDs/11-scale-and-performance-demo.md`, SDD 17, 22, 34 | SDD 11 Amendment 8 (as built, measured); one-line notes in 17, 22, 34 | 38c |
| `Docs/ARCHITECTURE.md` | `Npc/` row (lane math, renderer, materials, `.ush`), dashed `Npc -> Menu` edge and its sentence, `OrbitLines/` occluder note, schema 13, `-SOLSmokeNpcLanes`, `sol.NpcLanes` | 38c |
| `Docs/PLAYER_GUIDE.md`, `Docs/Images/` | Regenerated with `Tools\WritePlayerGuide.bat` (Controls and Info text) | 38b or 38c |
| `Docs/TECH_DEBT.md` | New debt found (e.g. D21 if the Overlay Menu crosses the one-eye ceiling) | 38c |
| `Docs/Plans/STATUS.md` | #38 in progress (this draft), Done with results at the end | 1, 38c |
| `README.md`, `Docs/PROJECT_METRICS.md`, `Docs/Images/` | `Tools\UpdateMetrics.bat` before each commit and at pauses; 2-3 gallery shots (e.g. V2 and V3 with lanes) | 38a-38c |

## Steps

| Step | What | Who (model / effort) | Status |
|---|---|---|---|
| 1 | SDD, this plan, STATUS #38 in progress | opus / high | Done |
| 1b | Q1-Q18 answered by the user (D7-D24, 2026-11-11); SDD, contract and plan updated | main session, then opus / high | Done |
| **38a** | | | |
| 2 | TDD: SDD section 6 cases 1-23 written from the contract only (no implementation shown), including the schema 13 and row-count updates of existing assertions | opus / high | Done |
| 3 | `Tools\RunTests.bat -Build -Filter` `SOLTest.NpcLaneMath`, `OverlayModel`, `SettingsRules`, `SettingsModel`, `FlightModeMath`: fail for the right reason (missing `SOLNpcLane`, `EOverlay::Lanes`, version 12 != 13), not a typo | sonnet / medium | Skipped (see notes: built together with step 4) |
| 4 | Persistence: `SAVE_VERSION` 13, the table row, migration 12 -> 13, Controls text (guardrails: version and migrate, never discard; only what D1 approved; never add a key to an empty map; report every constant, field and key changed). Then `SOLNpcLaneMath`. Tests green, full suite green; `-SOLSmokeOverlay` (row exists, toggles, writes once on close), `-SOLSmokeHoloMenu` and `-SOLSmokeSolidMenu` (row fits at 1280x720 and 1920x1080) PASS | opus / xhigh (persistence), opus / high (math) | Done (not committed; review next) |
| 5 | Adversarial performance and style review of 38a (fresh opus / xhigh, report only; brief: changed files, SDD 38, style guide, performance guidelines, the TECH_DEBT items of SDD section 9, the persistence report); fixes; re-check against SDD 38; full suite again; `Tools\UpdateMetrics.bat`; commit and push 38a | opus / xhigh, then opus / high | Done |
| **38b** | | | |
| 6 | Engine and code check first: the orbit lines' material and `.ush` entry points the lane material reuses, whether `ASOLNpcVisuals` is hidden on the map (SDD section 5, D18: the lanes must show there; record the finding), a time source that ignores warp and stops on pause (D15). If a decision turns out not viable, stop and report | opus / high | Done (map: `ASOLNpcVisuals` is never hidden there; see notes) |
| 7 | Materials: `M_SOLNpcLanes` and the `M_SOLNpcPoints` crossfade in `create_npc_assets.py`, `SOLNpcLane.ush` mirroring `SOLNpcLaneMath`; rebuilt with the editor closed; compile clean | opus / high | Done |
| 8 | Renderer: histogram, lane frame, group texel `.w`, lane mesh and component, parameters, glow feed, settings reads, verification override, `sol.NpcLanes`, shared occluders (D24); `-SOLSmokeNpcLanes` checks (SDD section 8 item 2); the Info sentences (case 24); player guide regenerated; tests and full suite green; smokes PASS at 1280x720 and 1920x1080; the 1M ramp PASS | opus / high | Done (not committed; review next) |
| 9 | Review of 38b (fresh opus / xhigh, report only, same brief plus the materials and `.ush`); fixes; re-check against SDD; full suite; metrics; commit and push 38b | opus / xhigh, then opus / high | Done |
| **38c** | | | |
| 10 | Screenshots V1-V5 at 1280x720, 1920x1080, 3840x2160, lanes on / off, HUD glow 0 / 100 / 200%; tuning of the fade radii, widths, brightness, emissive, flow and dash count from them (main session shows the shots to the user if a value needs the user's eye); final values in `SOLConstants.h` and the SDD; candidate shots deleted | opus / high | Done (emissive 1.3, outbound #7FD0C4; SDD 38 section 4.13) |
| 11 | Profiling (SDD section 8 item 7): `ProfileGPU` and Insights, 1M, V2 and V3, `sol.NpcLanes` 0 / 1, three runs, 1920x1080 / 3840x2160 / 3840x3744, before (worktree at the commit before #38) and after; `-SOLSmokeHoloMenuProfile` P2 at the three sizes (D21: recorded, not gated; budgets gated at 1920x1080 only, D22); results in SDD 11 Amendment 8 and SDD 38 section 9 | opus / high | Done (cvar A/B instead of a pre-#38 worktree, a deviation recorded in SDD 38 section 4.13; `-SOLSmokeNpcLanesProfile`; HoloMenu P2 re-measured: three runs at 3840x3744 in step 11, and in step 14 one run at 1280x720 and three each at 1920x1080 and 3840x2160) |
| 12 | Persistence check (SDD section 8 item 5) and the PIE check via unreal-mcp if the editor is open (else noted here) | sonnet / medium | Done by the tests and smokes (no PIE: the editor was not open; see notes) |
| 13 | Docs: `GAME_MECHANICS.md`, SDD 11 Amendment 8, SDD 17 / 22 / 34 notes, `ARCHITECTURE.md`, `TECH_DEBT.md`, STATUS #38 Done, README gallery (2-3 shots), in-game text truth check against the constants | sonnet / medium | Done (not committed) |
| 14 | Whole-ticket adversarial review (fresh opus / xhigh); fixes; full suite; metrics; commit and push 38c; close #38 | opus / xhigh, then opus / high | Done |

## Test contract summary

The full contract is SDD section 6 (cases 1-24). In short:

- **`SOLNpcLaneMath` (new `SOLTest.NpcLaneMath.*`):** constants and colours (1); the close-up fade in lane radii, log
  linear, 0.5 at the geometric mean, scale free, bad input -> line stays (2); the ship-point alpha as its complement,
  or 1 with lanes off (3); the log ship-count weight 100 -> 0, 1,000 -> 1/3, 10,000 -> 2/3, 100,000 -> 1, no ships ->
  not drawn (4); line width, brightness, centre offset and ribbon extent scaled by the viewport height (5-8); the
  cross-line profile built on the HUD glow profile `GlowProfile(d', g)` (9); the wrapped flow phase (10); the dash phase
  per level and its motion toward B (outbound) or A (inbound) (11); the multi-scale dash coverage equal to the orbit
  lines' at zero flow (12); the analytic closest lane parameter of a view ray and the distance at a parameter (13);
  the "keep right" screen normal (14); the lane-ship histogram (15); the per-frame lane frame for #49 (16).
- **Overlay model:** 16 rows, `Lanes` after `Ships on Radar`, default on, top level, persisted by id, governed by the
  master O, copied on first enable (17).
- **Settings:** `SAVE_VERSION` 13, layout 10, classify and notice rules (18); the full v12 migration with used,
  unused and used-disabled slots (19); keep an existing value, no padding, no profiles stays none (20); chains from
  v1, v6, v9, v10, v11 (21); v13 untouched and round trip (22); write owed on a Lanes toggle (23).
- **Text:** Info and Controls tokens resolve, player guide current (24).

## Notes

- Brief every subagent with: SDD 38, this plan, the files above, the style guide, "delete temporary screenshots and
  captures once used", and "no AI attribution in commits or code comments".
- The point and lane materials' HLSL mirror `SOLNpcLaneMath`; a change to one changes the other (file headers say so).
- **For 38b (from the 38a review):** the lane histogram must be folded into the existing `FillRailTexels`
  `ParallelFor` with per-task count arrays merged at the end (SDD 38 D13), not run as a separate serial
  `CountLaneShips` pass over the 1M rails; `CountLaneShips` stays the pure reference the tests pin. The lane shader's
  glow and ribbon follow D25 (`LaneLineProfile` and `LaneRibbonHalfExtentPx` take the viewport height).
- Pause points per CLAUDE.md (context above 35%, usage limits, 70% weekly): save resume state here (the step, files
  mid-edit, next action) and run `Tools\UpdateMetrics.bat`.
- **38a progress (2026-11-11, steps 2-4 done, not committed):** the contract tests (step 2) were authored by a separate
  agent; step 3's red run was skipped (the implementation was built together with the tests in one build, so the
  red-for-the-right-reason check was not observed). Built: `Npc/SOLNpcLaneMath` (cases 1-16), `EOverlay::Lanes` at
  index 14 (Darkening 15, 16 rows), `SAVE_VERSION` 13 with the frozen 12 -> 13 step, the `@OverlayMenu` Controls text,
  `-SOLSmokeOverlay` Lanes checks (row listed in Profiles 1 and 3, Space toggles it with no write while the menu is
  open, the master O hides it, per-profile bit through a cycle and a re-enable; page row count 17). Results: targeted
  tests 171/171, full suite 1826/1826 (`SOLTest.PlayerGuide.UpToDate` passes after `Tools\WritePlayerGuide.bat
  -NoCrops`; only `33-help-keyboard-hover.png` changed for real, the other 19 images were build-string drift and were
  restored), `-SOLSmokeOverlay` 44/44 (1280x720), `-SOLSmokeHoloMenu` 86/86 at 1280x720 and 1920x1080,
  `-SOLSmokeSolidMenu` 296/296. `GAME_MECHANICS.md` (Lanes row, Ctrl+O row count, saving schema 13) and
  `ARCHITECTURE.md` (schema 13 sentence, `SOLNpcLaneMath` in the `Npc/` row) updated now so neither is stale; the
  rest of their #38 edits stay in 38b / 38c.
- **38a review fixes (2026-11-11, step 5, nothing High):** the user chose D25 (the lane glow distance and the ribbon
  edge scale with the viewport height; `LaneLineProfile` takes the height, cases 8 and 9 updated, new
  `SOLTest.NpcLaneMath.LineProfileScaled` for 720p and 4K), kept the Controls row text and had the Help glossary's
  "Overlay Menu" entry list NPC lanes (no new entry). Also: behind-camera `ClosestLaneParam` cases (a lane wholly
  behind the camera -> 1, a lane crossing behind it -> 0.5; passed without a code change), case 9's "non-decreasing in
  g" narrowed to just past the core edge (measured from about 0.04 px), the frozen v12 fixture's literal 15, the two
  over-long lines wrapped, `IsLanesRowListed` reads the row below Lanes, new `-SOLSmokeOverlay` fit checks
  (`4-menu-fits`, `5-menu-fits`: the hologram frame and the last row of the 17- and 18-row pages inside the viewport),
  the `Npc -> UI` / `Npc -> OrbitLines` edges in `ARCHITECTURE.md` and SDD D23 (the module graph was not rendered: no
  Node.js on this machine; the two new lines were checked by eye). Results after the fixes: affected topics 219/219,
  `-SOLSmokeOverlay` 46/46 at 1280x720 (frame bottom 160 px above the viewport bottom) and 1920x1080,
  `-SOLSmokeHoloMenu` 86/86 at both sizes, `-SOLSmokeSolidMenu` 296/296, full suite 1827/1827; the guide
  regenerated (`33-help-glossary-menus-map.png` and `33-help-keyboard-hover.png` changed for real, 18 build-string-only
  images restored).

- **38b progress (2026-11-11, steps 6-8 done, not committed; step 9 review next):** engine check: the orbit lines'
  `SOLOrbitLine.ush` functions are reused from `SOLNpcLane.ush` (placement, occlusion, scene depth, dash box filter);
  the map view hides only body, ring and gas-giant actors, so the lanes show there unchanged (SDD 38 section 5
  finding); the flow clock uses `FApp::GetDeltaTime` (no time warp) and holds while the world is paused or the pause
  menu shows. Built: the renderer's pure feed in `SOLNpcLaneMath` (test cases 25-32), `ShouldWriteOccluders`, the lane
  histogram inside the rail-fill `ParallelFor` (per-task counts), the lane ribbon (`USOLOrbitLineComponent`,
  `ApplyRibbonToMesh`, 2,080 vertices for 8 lanes), `M_SOLNpcLanes` and the `M_SOLNpcPoints` crossfade
  (`Tools/Npc/create_npc_assets.py -SOLRebuild`), `Shaders/Private/SOLNpcLane.ush`, the per-frame lane frame and `.w`
  texels, the glow feed, `sol.NpcLanes`, `ASOLGameMode::GetNpcVisuals` / `SetOrbitLineVisuals`, the Info sentences
  and Demo clause, `-SOLSmokeNpcLanes` (`Npc/SOLNpcLaneSmoke`), and renderer checks in `-SOLSmokeOverlay`
  (`*-lanes-renderer`: the renderer follows the row and the master; with no NPCs out its lines stay hidden). Deviations
  are listed in SDD 38 section 4.12. The player guide does not include the Info prose, so nothing in it changed
  (`SOLTest.PlayerGuide.UpToDate` passes without a regeneration). Results are in the report of this step (tests, the
  new smoke 30/30 at 1280x720 and 1920x1080, `-SOLSmokeOverlay`, `-SOLSmokeHud`, `-SOLSmokeHoloMenu`,
  `-SOLSmokeSolidMenu`, the 1M ramp, full suite). `ARCHITECTURE.md` (Npc row, `Npc -> Menu` edge, occluder note, flag,
  cvar), `GAME_MECHANICS.md` (Lanes row, Render tiers crossfade, new "Lane lines" row) and STATUS updated now; the
  rest of the docs stay in 38c.
- **38b review fixes (2026-11-11, step 9):** the shader's keep-right sign is pinned by a C++ port and a unit test
  (`SOLNpcLane::LaneOutboundSide`, `SOLTest.NpcLaneMath.OutboundSide`, six views, two with an endpoint behind the
  camera; the HLSL was already right); `SOLTest.NpcLaneVisuals.Materials` checks every `SOL::NpcLaneMaterialParams`
  name, two-sided and the after-motion-blur pass; the misleading sort-priority test and comment went; the lane
  histogram counts on the stack (no false sharing between tasks); `NpcLaneParams0.y` dropped (unused, case 28
  updated); the points' crossfade calls `SOLNL_LineFade` (materials rebuilt); `IsValid(OrbitLines)`; the occluders
  are written at once when lanes turn on with the orbit lines hidden; `FillRailTexels` no longer `const`; smoke view
  V4b (one lane radius off the Earth-Mars axis, 1.5 degrees off it): no near-plane smear at either size, kept as a
  regression view; three `TECH_DEBT.md` items (orbit-line coupling, `Npc <-> Menu` cycle, points moving under the
  pause menu while the dashes hold). Results: lane topics 44/44, full suite 1843/1843, `-SOLSmokeNpcLanes` 34/34 at
  1280x720 and 1920x1080, `-SOLSmokeOverlay` 50/50.

- **38c progress (2026-11-11, steps 10-13 done; step 14 below):** two user decisions recorded as
  the D15 amendment: (A) the dashes never freeze (the flow clock lost its pause input; `ASOLNpcVisuals` no longer
  reads `USOLMenuSubsystem`; the `TECH_DEBT.md` item about dashes freezing under the pause menu is deleted, the
  `Npc <-> Menu` item updated; the smoke's `e-flow-paused` became `e-flow-pause-menu`); (B) short lanes get coarser
  dash levels down to 4 dashes per lane (`NPC_LANE_DASH_MIN_LEVEL` -4, in `SOLNpcLaneMath`, `SOLNpcLane.ush` via
  `NpcLaneStyle2.z`, and test cases 1, 11, 12). The tests were updated with the code in one build, so their red run
  was not observed (the new short-lane case fails on the old clamp by construction: a 130 px lane's 64 dashes
  box-filter to an even line). Tuning from 1080p shots (temporary tuning cvars, removed): emissive 1.3, outbound
  stored #7FD0C4 (SDD 38 section 4.13 has the candidates). Profiling with the new `-SOLSmokeNpcLanesProfile`
  (`STAT_SOL_NpcRedraw` / `STAT_SOL_NpcLanes` added); "before" is `sol.NpcLanes 0` in the same run rather than a
  worktree at the commit before #38 (the old build has no lane views to place). Results in SDD 38 section 4.13 and
  SDD 11 Amendment 8. No PIE check (the editor was not open); the persistence of the Lanes row is covered by the 38a
  tests and `-SOLSmokeOverlay`. Docs: GAME_MECHANICS (Lane lines row), ARCHITECTURE (no menu read, stats, profile
  flag), SDD 11 Amendment 8, SDD 17 Amendment 4, SDD 22 Amendment 4, SDD 34 section 16, TECH_DEBT, STATUS, README
  gallery. The Info and Controls texts name no changed value, so they are unchanged. Results: full suite 1843/1843,
  `-SOLSmokeNpcLanes` 34/34 at 1280x720 and 1920x1080, `-SOLSmokeOverlay` 50/50, `-SOLSmokeHud` 45/45,
  `-SOLSmokeHoloMenu` 86/86 at both sizes, `-SOLSmokeSolidMenu` 296/296.
- **Correction (step 14):** the HoloMenu P2 re-measure *was* run in step 11 (`smoke_c38_holoprof_1..3.log`, three
  runs at 3840x3744: 1.179 / 1.182 / 1.195 ms, median 1.182 ms), as SDD 38 section 4.13 and `TECH_DEBT.md` say; an
  earlier version of this plan said it was not.
- **Step 14 (2026-11-11, whole-ticket review fixes):** the `Npc -> Menu` edge label of `ARCHITECTURE.md` lost
  "pause" (the module graph was checked by eye for `;` and unbalanced quotes, not rendered: no Node.js here); SDD 38's
  D15 amendment states the real coarse-dash threshold (about 64 x 24 = 1,536 px of lane length on screen, not 130 px)
  and the user's wording, as does `GAME_MECHANICS.md`; SDD 38 section 4.6's equality range ([MIN, 8 + MIN]), the
  section 6 / test-file case citations, section 10 (the running clock adopted in 38c) and section 4.13 (the
  `sol.NpcLanes 0` baseline listed as a deviation from section 8 item 7; the Translucency column already includes the
  after-motion-blur pass: V2 at 1080p is 0.412 against 0.393 ms, +0.019 ms) fixed; the smoke's `e-flow-resumed` now
  times the phase from the resume and checks the real-time rate at warp 1, and the new `a-dash-min-level` reads
  `NpcLaneStyle2.z` back (`ASOLNpcVisuals::GetPushedLaneStyle2`): 35 checks; the profile's `stat dumpave -num=` comes
  from `LANESMOKE_PROFILE_FRAMES` and the console commands go through `FSOLUiProfileCapture::RunCommand` (the
  duplicated view-size helper is a `TECH_DEBT.md` note); `NpcLaneMathTest.cpp`'s [0, 1] sweep reaches footprints of
  1e3 and the continuity loop includes the clamp boundary at MIN (footprint 1 / (24 x 2^MIN)); `UploadPopulation`
  and `ReepochRails` log their rail-fill times once per population. Results: full suite 1843/1843 (lane topics
  29/29), `-SOLSmokeNpcLanes` 35/35 at 1280x720 and 1920x1080, `-SOLSmokeOverlay` 50/50, the 1M `-SOLNpcRamp` PASS
  at every stage (1M p95 7.43 ms; upload at 1M 22.0 ms, of which the rail fill with the lane histogram 2.65 ms),
  `-SOLSmokeHoloMenuProfile` P2 0.164 ms at 1280x720 (PASS against its baseline), 0.240 ms at
  1920x1080 and 0.515 ms at 3840x2160 (medians of three); `GameUserSettings.ini` unchanged.
- **Process note (honest record):** for 38c the TDD red run was never observed. The D15-amendment test changes
  (cases 1, 11, 12, 31) and the step 14 test extensions were written and built together with the code, so no run
  showed them failing for the right reason first; the only evidence that they would is by construction (the
  short-lane case cannot pass on the old [0, max] clamp).

## Change log

- 2026-11-11: first draft with the SDD (D1-D6); Q1-Q18 pending with the user.
- 2026-11-11: Q1-Q18 answered (D7-D24) and the keep-right rule added to D2; the open-question gate and the Q references
  removed; ready to build.
- 2026-11-11: 38a and 38b built, reviewed and committed; 38c built (two user decisions as the SDD's D15 amendment,
  tuning, profiling, docs), whole-ticket review next.
- 2026-11-11: whole-ticket review fixes (step 14), the P2 correction, the ramp and upload timings; 38c committed and
  #38 closed.
- 2026-11-11: follow-up (user decision, SDD 38 D15 amendment C): the lane dash size blend (the dimmer finer
  "shadow" copy) removed in `LaneDashCoverage` and `SOLNL_DashCoverage`; case 12 tests updated; suite 1843 / 0,
  `-SOLSmokeNpcLanes` 35 / 35 at 1280x720 and 1920x1080, `-SOLSmokeOverlay` 50 / 50; README lane shots recaptured.
- 2026-11-11: second follow-up (user decision, SDD 38 D15 amendment D, the "accordion"): one continuous dash scale per lane
  laid along the on-screen spacing (`LaneScreenParam`), replacing the stepped `floor(level)` and its pop; cases 11, 12
  and the new 12b; suite 1844 / 0, `-SOLSmokeNpcLanes` 35 / 35 at 1280x720 and 1920x1080, `-SOLSmokeOverlay` 50 / 50.
