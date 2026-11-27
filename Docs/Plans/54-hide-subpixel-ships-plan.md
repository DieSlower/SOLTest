<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 54: Hide NPC ships whose true size is under 1 pixel

**Issue:** [#54](https://github.com/DieSlower/SOLTest/issues/54) (In progress in [`STATUS.md`](STATUS.md), row 3b5c).
SDD: [`SDDs/54-hide-subpixel-ships.md`](../SDDs/54-hide-subpixel-ships.md). Test-first contract:
[`54-hide-subpixel-ships-contract.md`](54-hide-subpixel-ships-contract.md). Companion: #56 (target zoom overlay, its
own ticket and grill-me).
Status: **IMPLEMENTED 2026-11-27 (54a-54g docs done); awaiting the README gallery shots, the whole-ticket review follow-ups and the commit.** (Gate history: APPROVED 2026-11-27 after the user answered SDD 54 Q1-Q13.)
Pinned values: `NPC_MESH_MIN_PX` 16, `NPC_HULL_DOT_PX` 4, hysteresis 10% (`RenderHysteresis` 0.1),
`NPC_HULL_BOUNDING_DIAMETER_M` 34.467, `NPC_POINT_RASTER_MIN_DEVICE_PX` 1, `NPC_SHIP_HIDE_PX` 1,
`NPC_SHIP_DOT_FULL_PX` 2. The parts that depend on each answer: 54c on Q1, Q2, Q6, Q12; 54d on Q3, Q4, Q5, Q8, Q9;
54e on Q7.
**Persistence: none planned.** `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay. A part that finds it needs
a saved setting stops and reports; a schema change needs the user's approval.

**Every brief repeats:** the pixel rule (LOD, fade, visibility and switching thresholds in 1080p reference pixels,
never distance or counts; distance only as a derived figure; a fixed pool is the only cap and is reported as a
limitation when hit); the cutoff is fixed at 1 px on every preset (never the quality-scaled lane focal); never use
`FSOLNpcCpuShip::bHidden` for this rule (selection, radar and icons read it); delete temporary screenshots and scratch
files after use; no AI attribution in commits, PRs or code; the file banner and comment rules of
`Docs/STYLE_GUIDE.md`; check `Docs/TECH_DEBT.md` for the files touched.

## Parts

Each part is built by its own subagent (context under about 35%), then reviewed by a fresh adversarial `opus` subagent
at xhigh effort (performance and style, report only, with the relevant `TECH_DEBT.md` items so they are not
re-reported), fixes applied, full suite green (`Tools\RunTests.bat`), then committed and pushed on `master`
(`Issue #54: Part 54x - ...`) with `STATUS.md` and the metrics (`Tools\UpdateMetrics.bat`) in the same commit.

### 54a: tests first, contract only (separate author)

1. Test author (`opus`, high, **contract only, no implementation shown**): write
   `Source/SOLTest/Tests/NpcPixelTest.cpp` (`SOLTest.NpcPixel.*`), cases 1-9 of the contract; undecided values as
   parameters.
2. Main session (or a `sonnet` medium subagent): add `Npc/SOLNpcPixelMath.h` with the signatures and stub bodies
   (return 0 / false), `Npc/SOLNpcPointMirror.h` with stub inline bodies, and the new constants in `SOLConstants.h` at
   their answered values; do not delete
   `NPC_POINT_MIN_PIXELS` / `NPC_HULL_MIN_PIXELS` yet (54c / 54d do).
3. `Tools\RunTests.bat -Build -Filter SOLTest.NpcPixel`: every case fails for the right reason (wrong value), none for
   a build error.

### 54b: pure math, float mirror and shader file

Agent: `opus`, high (numerics, parity).

1. Implement `Npc/SOLNpcPixelMath.cpp`, the bodies of `Npc/SOLNpcPointMirror.h` and the new
   `Shaders/Private/SOLNpcPoint.ush` (`SOLNP_FocalRefPx`, `SOLNP_ShipSizePx`, `SOLNP_DotAlpha`,
   `SOLNP_DrawnRadiusCm` or equivalent), statement for statement with the mirror (header comments naming each other,
   the convention of `SOLNpcTrafficMirror.h`), explicit NaN guards on both sides.
2. `static_assert`s in `SOLConstants.h`: hide < full; `NPC_HULL_DOT_PX / 1.5 >= NPC_SHIP_DOT_FULL_PX`;
   `NPC_MESH_MIN_PX > NPC_HULL_DOT_PX x 1.5`.
3. Green `SOLTest.NpcPixel`, then the full suite. The `.ush` is not used yet (54d wires it), so no material work here.

### 54c: render tier by pixels, hull floor removed

Agent: `opus`, high (multi-file).

1. `USOLNpcSubsystem` (budget block before the Select pass): nominal mesh and hull radii from
   `SOLNpcPixel::PixelRadiusM(NPC_HULL_BOUNDING_DIAMETER_M, f, threshold)` with `f = SOLThruster::FocalRefPx` of
   `ViewCamera` (thresholds times the quality factor, Q12: Low x10, Medium x4.6, High x2.2, Epic and Cinematic x1; the
   1 px cutoff is never scaled), capped by the Mid radius, then `BudgetedRadiusM`
   as today. `SelectRenderTier` unchanged.
   The lane hand-over's `hullStartM` in `ASOLNpcVisuals::UpdateCpuShips` becomes `max(hull radius,
   PixelRadiusM(D, f, NPC_SHIP_HIDE_PX))` (SDD 54 4.5), so every visible dot sits at its double position.
2. Demo options per Q6 (decided: `MeshRadiusM` / `InstancedRadiusM` replaced by `MeshMinPx` / `HullMinPx`;
   descriptors with the hull minimum at 3 px and the mesh threshold never below the hull one, sanitize rules, `sol.Npc.Option` names, `SOLDemoPageWidget` rows, `SOLMenuState` formatting); update and
   re-validate `NpcScaleMathTest` and `SOLMenuStateTest` (every existing case walked, stale ones rewritten or
   deleted). Stats overlay radius line marks a binding budget (Q6).
3. `ASOLNpcVisuals::UpdateCpuShips`: instanced hulls at their true length (remove the `NPC_HULL_MIN_PIXELS` term);
   delete `NPC_HULL_MIN_PIXELS`.
4. Tests: extend `NpcScaleMathTest` (or `NpcPixelTest`) with the subsystem's radius derivation if it is factored into a
   pure helper; full suite green.
5. Quick PIE check (unreal-mcp if the editor is open): Demo battle, hull count drops to the ships within about 8 km.

### 54d: point material, far layer skip, map and selection rules

Agent: `opus`, high (rendering). The material rebuild is approved (Q8), once.

1. `Tools/Npc/create_npc_assets.py`: `VERTEX_HLSL` includes `SOLNpcPoint.ush` and calls it after `dM`: alpha 0 returns
   the hidden offset; `Shade.y` x alpha; the billboard radius `max(D / 2 x scale, 0.5 x NpcParams2.x x pixelCm)`. New
   vector parameter `NpcParams5` (hide px, full px, reference height, map flag). Update the script header's history
   notes. **Rebuild `M_SOLNpcPoints` once** (`-SOLRebuild -SOLOnly=M_SOLNpcPoints`, editor closed; approved in Q8),
   check the log for shader compile errors.
2. `ASOLNpcVisuals`: `NpcParams2` = (raster min device px, `D / 2`, brightness ref, brightness min), `NpcParams5` set
   with the shared parameters (map flag from `IsMapOpen` per Q9); the far layer component shown only while
   `SOLNpcPixel::FarLayerCanDraw(D, f, stats.MidRadiusM, NPC_SHIP_HIDE_PX)` (visibility changed only on a flip), the skip
   bypassed while the map flag is set (Q9); delete `NPC_POINT_MIN_PIXELS`.
3. No selected-NPC exemption (Q3 final): a selected NPC under 1 px is hidden like any other, nothing is pushed to the
   material for selection.
4. PIE smoke (unreal-mcp): screenshots of a battle at 5, 10, 20 and 30 km at 1080p; confirm whether NPC dots drew on
   the jump map before the change (a map screenshot taken before step 1) and that the Q9 choice holds after it. Delete the screenshots after use.

### 54e: thruster flames independent of the hull

Agent: `opus`, high. Built as Q7 was decided (flames on dot and hidden ships; glow stays a hull section).

1. `USOLNpcTierProcessor::CollectChunk`: heading, top and thrust for every CPU ship inside `PlumeGateM`, whatever its
   render tier (dead ships still skipped).
2. `ASOLNpcVisuals::UpdateCpuShips`: reserve ribbon feed slots for point-tier ships with a non-zero `ThrustLocalG` in
   the serial pass; feed them from the parallel pass at their double position (no allocation; `BeginNpcFeeds` sized
   once).
3. Re-validate `ThrusterMathTest` / `ThrusterMirrorTest` and the thruster smoke expectations that assumed hull-only
   flames (SDD 51 Q27); add a case for a dot ship's flame where it is testable.
4. Measure the flame CPU gate at the spawn view (TECH_DEBT: 0.054 ms vs 0.05 ms); report the delta, it must not grow.

### 54f: smoke, re-baselines and profile

Agent: `opus`, high (smoke authoring and profiling); the review at the end is a fresh `opus` xhigh.

1. New `-SOLSmokeNpcPixel` (cases a-f of SDD 54 section 9.6): tiers against true px, no hull under the kept
   threshold, far layer visibility against `FarLayerCanDraw` (plus a forced narrow-Mid case), the screenshot GPU check
   at 0.8 / 1.5 / 3 px against the mirror's alpha, a hidden ship still selectable with its icon, a dot ship's flame.
2. Re-baseline, at 1280x720 and 1920x1080: `-SOLSmokeNpcTraffic` (its V2 hull fly-by and the hand-over end at the new
   hull radius), the thruster smoke (mid band from `InstancedRadiusM`), `-SOLSmokeNpcLanes`, `-SOLSmokeNpcTunnel`
   (points inside a tunnel only from 1 px), `-SOLSmokeLaneMerge`, `-SOLSmokeAutoLanes`, and the 1M ramp. A changed
   expectation is explained in the plan notes, never silently loosened.
3. Profile (1M NPCs, spawn view and lane view, 1080p and 4K): points draw and far layer GPU ms, `STAT_SOL_NpcRedraw`,
   `ASOLNpcVisuals::UploadShips`, the NPC update; before / after against the 54b commit. Gates: no GPU or CPU increase
   at any view; a far layer and hull upload decrease expected. VR one-eye numbers are record only.
4. Watchdog: check headless Unreal processes every 15 minutes, kill hung ones.

### 54g: docs, in-game text, gallery, close

Agent: `sonnet`, medium (docs and text audit).

1. `Docs/GAME_MECHANICS.md` (Render tiers, NPC thruster flame, Lane tunnel rows), the in-game info text in
   `Menu/SOLMenuText.h` (Demo paragraph, plume sentence, tunnel paragraph, Demo row labels), SDD 51 section 4.5 / Q27
   amendment note, SDD 11 render tier note, `Docs/TECH_DEBT.md` (legacy-LOD item rewritten: render tiers converted,
   AI tiers and budgets remain; the far layer's dead distance brightness; the distance-based NPC bolt radius if not
   listed), `Docs/ARCHITECTURE.md` (new files, the far layer skip).
2. README gallery: 2-3 new shots (`Docs/Images/54-*.png`: battle hulls and dots, a fading dot, an empty far sky with
   the radar still full).
3. `STATUS.md`: #54 to Done with the measured numbers; `Tools\UpdateMetrics.bat`; `graphify update .`.
4. The whole-ticket adversarial review, fixes, full suite, commit and push.

## Notes

- 2026-11-27: SDD, contract and plan drafted; STATUS.md rows for #54 (In progress) and #56 (To do) added.
- 2026-11-27: user approved Q1-Q12 (SDD section 3); gate cleared, SDD, plan and contract amended, 54a can start.
- 2026-11-27, 54a: a separate test author wrote `Tests/NpcPixelTest.cpp` (`SOLTest.NpcPixel.*`) from the contract only, against stubs of `Npc/SOLNpcPixelMath.h` and `Npc/SOLNpcPointMirror.h` and the new constants (no per-case record of the red run was kept in these notes). `NPC_POINT_MIN_PIXELS` and `NPC_HULL_MIN_PIXELS` were left for 54c / 54d, as planned.
- 2026-11-27, 54b: `Npc/SOLNpcPixelMath.cpp`, the bodies of `Npc/SOLNpcPointMirror.h` and `Shaders/Private/SOLNpcPoint.ush` (`SOLNP_FocalRefPx`, `SOLNP_ShipSizePx`, `SOLNP_DotAlpha`, statement for statement with the mirror, explicit NaN guards on both sides); the `static_assert`s in `SOLConstants.h` (hide < full, hull threshold / 1.5 >= full, mesh > hull x 1.5). `SOLTest.NpcPixel` green. The `.ush` was not used until 54d.
- 2026-11-27, 54c: render tier radii from `SOLNpcPixel::TierRadiusM` (thresholds x `LaneTunnelQualityScale`, capped
  by the Mid radius, then budgeted); hand-over start from `SOLNpcPixel::HandOverStartM` (visuals and the traffic
  smoke's replica); hull floor, `NPC_HULL_MIN_PIXELS` and the unused `PixelWorldCm` removed. Demo rows `MeshMinPx`
  (3-128 px, step 1, default 16) and `HullMinPx` (3-64 px, step 1, default 4; `NPC_HULL_DOT_PX_MIN` 3), shown as plain
  numbers under "Mesh from (px)" / "Hull from (px)"; sanitize raises the mesh threshold to the hull one
  (`EOptionIssue::MeshPxRaised`; the "instanced <= Mid" rule is gone, the Mid cap is per frame). The overlay has no
  radius line, so a binding pool is marked "(budget)" on its render line (and `meshCap` / `instCap` in the
  `NpcStats` log). The ranges and the step are this part's choice (Q6 pinned only the defaults and the 3 px minimum).
  No PIE check: unreal-mcp was not connected (editor closed). `SOLThrusterSmoke` (mid band from `InstancedRadiusM`)
  is left for the 54f re-baseline.
- 2026-11-27, 54d: the baked vertex node of `M_SOLNpcPoints` makes one call, `SOLNP_PointDot` (new in
  `SOLNpcPoint.ush`, mirror `SOLNpcPointMirror::PointDot`): map branch (Q9, 2 device px dot, alpha 1), else true size
  -> alpha (0 returns the hidden offset) and the footprint `0.5 x DrawnDiameterDevicePx x pixelCm`; `Shade.y` x alpha.
  So later tuning of the rule is `.ush`-only. Parameters built by `SOLNpcPixel::PointParams2` / `PointParams5`
  (new constant `NPC_POINT_REFERENCE_HEIGHT_PX` 1080; `NPC_POINT_MIN_PIXELS` deleted), `NpcParams5` pushed to both
  point layers on a map flag change only. Far layer: `SOLNpcPixel::FarLayerShown(map, live focal, Mid radius)`,
  `SetVisibility` on a flip only (`IsFarLayerShown` getter for the 54f smoke). Fast math: DXC compiles UE materials
  without `-Gis`, so every float compare carries the `fast` flag (checked with the SDK's dxc on the guards and on the
  whole vertex body); the NaN / infinity guards in the `.ush` and the mirror are now integer tests on the float's bits
  (`and` / `icmp` in the DXIL, which fast math cannot fold), with the contract's results unchanged (+inf -> 1, NaN ->
  0). Verified in the real compiled material: after the rebuild, `M_SOLNpcPoints` was compiled for PCD3D_SM6 (material
  statistics in a rendering commandlet with `r.DumpShaderDebugInfo=1`, no compile errors, 934 VS instructions) and
  its base pass vertex shader (inspected) holds the bit tests (`and i32 ..., 2139095040` / `icmp eq`) next to 24
  `fcmp fast` compares, the velocity, hit proxy and Lumen shaders match the same pattern (not inspected line by line);
  the dump was deleted. Not checked: SM5 (FXC) and a rendered NaN case (54f). New tests `SOLTest.NpcPointDot.*`
  (`Tests/NpcPointDotTest.cpp`, test-after). The CPU cull is unchanged: the shader is the only authority for the dot rule on every point (CPU and rail, lane,
  combat), `bHidden` untouched, no selection exemption. No PIE smoke or before/after map screenshot: unreal-mcp was
  not connected; left to 54f.
- 2026-11-27, 54e: flames independent of the hull as decided in Q7. `USOLNpcTierProcessor::CollectChunk` computes heading, top and `ThrustLocalG` for every CPU ship inside the plume gate whatever its render tier (dead ships still skipped, `bHidden` not used for the rule); `ASOLNpcVisuals::UpdateCpuShips` reserves ribbon feed slots for point-tier ships with thrust in the serial pass and feeds them from the parallel pass at their double position, no allocation. The glow stays a hull section. SDD 51 Q27 is superseded by SDD 54 Q7 (note added to SDD 51). The flame CPU gate was measured in the 54f A/B below (not shown closed, not grown).
- 2026-11-27, 54f (first agent, stopped at its context limit; in progress): new smoke `-SOLSmokeNpcPixel`
  (`Npc/SOLNpcPixelSmoke.h/.cpp`, flag `SOL::CommandLine::SMOKE_NPC_PIXEL`, wired in `ASOLShipPawn`; 54g adds its
  `ARCHITECTURE.md` row). Cases: (a) tiers against the budgeted pixel radii and the radii against `TierRadiusM`,
  (b) no hull under 4 / 1.1 px, (c) far layer per `FarLayerShown` (dark in flight; on with the camera FOV locked at
  1 deg, `GetFOVAngle` only; on with the jump map open), (e) 40 km outside a battle the 24 nearest ships (all under
  1 px) keep `bHidden` 0, resolve through `USOLInteractionSubsystem` and get icons (the player's overlays hide ship
  icons and radar ships, so the HUD selects none: the same selection is run on a local `FSOLOverlayIconLayer`),
  (f) burning dot ships have drawn flames, (d) the screenshot GPU check after a respawn with one sparse battle
  (500 ships, 20 km ball; combat, plumes and flames off, AA off, screen percentage 100, no HUD), the ridden battle
  ship projected through the pawn's camera component (the camera manager's cached view is a frame old and moved
  about 14 px at 30 km/s), and the jump map pair (with the population, then despawned): the NPCs must brighten many
  pixels in several places. SDD 4.8 open item settled: NPC dots draw on the jump map now (1M dots overlap into solid
  crowds at map scale, so a per-dot size cannot be isolated; the 2 device px size is the shader's map branch,
  `SOLTest.NpcPointDot.*`). Results: 1920x1080 20 / 20, 2560x1440 20 / 20 (1.5 px peak 120-165 levels against
  238-242 at 3 px, 0-2 at 0.8 px), 1280x720 19 / 20: **`d-gpu-fading-1.5px` fails at 720p** (peak 2 levels against
  210 at 3 px): at 720p a 1.5 ref px dot is 1.0 device px (the raster floor), and with AA off its triangle (incircle
  radius 0.5 device px) and the soft mask leave almost nothing at the pixel centre. Not yet checked with the
  project's TSR (the game's real AA); open: whether this is the R2 shimmer accepted in the SDD or a defect of the
  floor (a user decision if the floor needs to grow). An engine ensure `DoubleFloat.cpp OriginMax` fires once per
  run at startup (frame ~570, before any smoke phase); not investigated. Remaining in 54f: the re-baselines (Traffic,
  Thruster with Ribbons, Lanes, Tunnel, LaneMerge, AutoLanes at 1280x720 and 1920x1080), the 1M ramp, the profile
  (no "before" build is possible: nothing of #54 is committed and checkout / stash are not allowed, so compare with
  the #53 numbers in `STATUS.md` and report it as a limitation) and the full suite. Thruster P2 mid band: at every
  legal setting the band is empty by construction (low 1.25 x Full radius >= 6.25 km at the 5 km option minimum,
  high 0.8 x min(hull 8.27 km, glow gate 7.2 km) = 5.76 km at 90 deg), so its Mid half is vacuous and says so; not
  loosened.
- 2026-11-27, 54f2 (second agent):
  - **720p fading dot under TSR:** with the game's anti-aliasing (TSR, `r.AntiAliasingMethod` 4, screen percentage
    100) the 1280x720 `d-gpu-fading-1.5px` passes (peak 55-68 levels against 183-194 at 3 px; 0.8 px peak 2). So the
    AA-off failure is the raster floor without temporal accumulation, not a defect of the rule; the shader is unchanged.
    The smoke's measured shots now use the game's AA at every resolution (what the player sees); `-SOLNpcPixelAaOff`
    keeps the crisp AA-off variant (its 1080p / 1440p 20 / 20 results are the first agent's above). Results with TSR:
    1280x720, 1920x1080, 2560x1440 all 20 / 20 (1.5 px peaks 55 / 115 / 180 against 194 / 234 / 240 at 3 px; 0.8 px
    peaks 2 / 0 / 0). Caveats: TSR accumulates a screen-stationary dot (the ridden ship); a moving dot at 720p and the
    Low quality preset were not measured; the menu has no anti-aliasing option.
  - **Re-baselines (first run, before fixes):** at 1280x720 and 1920x1080 `-SOLSmokeNpcLanes` 35 / 35, `-SOLSmokeNpcTunnel`
    48 / 48, `-SOLSmokeLaneMerge` 15 / 15, `-SOLSmokeAutoLanes` 13 / 13, thruster 5 / 5, thruster ribbons 3 / 3 (no
    expectation changed); `-SOLSmokeNpcTraffic` 17 / 19 at both: `V2-handoff-nose` (0 promotions / demotions) and
    `V2-mid-heading` (0 Mid hull frames). Cause: since 54c a hull is drawn only from 4 px (6.8 km in the 720p run, 8.3 km in
    the 1080p run; reference px do not depend on the resolution at the same aspect, so the difference is explained by
    the chase camera's FOV widening, not by the resolution), inside the default 50 km Full radius, so no Mid hull is ever drawn and the tracked ship is promoted and
    demoted while it is a dot.
  - **Traffic V2 fix (smoke only, a judgment call for review):** `TrackFlyBy` now follows every ship with a hull heading,
    hulls and flame-data dots (SDD 54 4.7: a dot's flame follows that heading), and logs the tracked ship's first
    heading distance against the Full radius; when the plume gate lies inside the Full radius the hand-off is reported
    as not observable (still a FAIL). Results 19 / 19 at 1280x720 and 1920x1080: first heading at 77 km (Full radius
    50 km), 1 promotion and 1 demotion with a 0.000 deg nose change, 118 Mid ship-frames (all flame-data dots) 0.000
    deg off `LaneHullForward`. Underlying fact: at the default radii no Mid hull is ever drawn (hull radius 6.8-8.3 km, by the chase camera's FOV
    against the 50 km Full radius), so the hand-off shows only in the flame; at a quality preset whose ribbon reach is
    under 50 km (High, V 2.2 px: about 39 km) V2 cannot observe it. The runs used the machine's own quality preset.
  - **Thruster P2 Mid half removed:** a Mid hull inside the glow gate needs the Full radius inside the hull radius and
    the gate (5.9-7.2 km); the default 50 km is far beyond both, so the half could never be populated. Tried and dropped:
    the Full radius option at its 5 km minimum (in memory) still gave 0 Mid coasting hulls (only the ridden lane ship
    is near; lane ships are far apart) and turned the battle ships Mid, emptying P3's burning-dot count. P2 now
    checks the coasting hulls (Full) are dark and still fails on any Mid one with b > 0; the coast ride is at half the
    hull radius / gate. Thruster 5 / 5 at both resolutions after the change.
  - **1M ramp** (`-SOLNpcRamp -SOLNpcCount=1000000 -corelimit=16`, `r.VSync 0`, 1920x1080): PASS at every stage, 1M
    mean 7.32 ms, p95 8.64 ms, p99 9.87 ms, 136.6 fps (earlier: #49 p95 8.20 ms at 1280x720, #18 8.47 ms, the
    default-budget worst case 8.75 ms).
  - **Profile** (`-SOLSmokeNpcTunnelProfile`, T2 = Earth-Mars lane view, T5 = Earth spawn view, 3 ProfileGPU frames and
    a 240-frame `stat dumpave` per window, tunnels drawn / `sol.NpcTunnel 0`): no "before" build (nothing of #54 is
    committed, checkout / stash not allowed), so the only points-draw baseline is #50's (SDD 50 section 15, 1080p:
    T2-all 0.676 / 0.469 ms, T5-all 0.411 / 0.359 ms; #53 never re-measured it, `TECH_DEBT.md`). Now at 1920x1080 and
    2560x1440: T2 has no `M_SOLNpcPoints` draw in the base pass at all, T5 only the CPU point layer at 0.000 ms; the far
    layer never draws in flight (the expected win, about -0.4 to -0.7 ms GPU against #50). `STAT_SOL_NpcRedraw` 1080p:
    T2 0.058-0.069 ms, T5 0.161-0.178 ms (#50: 0.046-0.056, 0.135-0.154); NPC update (`npcMs`) T2 0.41-0.44, T5
    0.81-0.86 ms (#50: 0.36-0.37, 0.69). These CPU rises cannot be attributed: #52 and #53 (lane network, ship blocks)
    landed in between; an interleaved A/B against the 54b commit is needed to gate them. GPU frame 4.4-6.1 ms at
    1080p, 7.1-8.8 ms at 2560x1440. `ASOLNpcVisuals::UploadShips` has no stat of its own (not measured). VR one-eye
    (3840x3744) not run (record only, VR work deferred).
  - **OriginMax ensure:** predates #54. `Saved/Crashes` holds the same `DoubleFloat.cpp` ensure
    (`OriginX <= OriginMax ...`, Renderer module stack) on 2026-10-24, 10-27 (twice), 10-28, 10-30 and 11-11, before
    any #54 work, and on three of the first 54f agent's runs (04:21-04:29). It is not once per run: none of this
    agent's 30-odd `-game` runs produced one. Not investigated further.
  - Also: the `SMOKE_NPC_PIXEL` constant had been inserted between `SMOKE_LANE_ROUTE` and its comment in
    `SOLConstants.h`; the comments are back with their constants.
  - Full suite (`Tools\RunTests.ps1`): 2193 / 2193 passed, 0 warnings. Settings files restored byte-exact after
    every run (MD5 checked). Left for 54g / the review: the smoke edits (`SOLNpcPixelSmoke`, `SOLNpcTrafficSmoke`,
    `SOLThrusterSmoke`) need the adversarial review; the CPU A/B gate against 54b is open.

#### A/B result (54f, 2026-11-27)

A = the pre-#54 tree (commit e6e34ee, the last #53 commit; nothing of #54 is committed, so there is no 54b commit),
built in a temporary git worktree with its own `Saved` (a copy of the same `SOLSettings.sav` and
`GameUserSettings.ini`, same MD5s); B = the main tree with the uncommitted #54 work. Same machine (i9-9960X, RTX 2080
Ti), `-game -windowed`, `-corelimit=16`, VSync off, sequential A B B A runs after one discarded A warm-up per smoke.
Smokes: `-SOLSmokeNpcTunnel -SOLSmokeNpcTunnelProfile` (T2 = Earth-Mars lane view, T5 = Earth spawn view; per window
one 240-frame `stat dumpave` of `STAT_SOL_NpcRedraw`, 3 ProfileGPU frames, the `NpcStats` `npcMs` lines),
`-SOLSmokeThruster -SOLSmokeThrusterRibbons -SOLSmokeThrusterProfile` (8 interleaved ribbon on / off windows per view
per run; flame gate = ribbon tick on minus off per window pair, as in SDD 53), `-SOLNpcRamp -SOLNpcCount=1000000`.
Values are medians over all runs (range in brackets); n = 2 runs per tree (16 window pairs for the thruster rows).

| Metric | Res | A (pre-#54) | B (#54) | B - A |
|---|---|---|---|---|
| Points draw GPU, T2 lane view (tunnels on / off) | 1080p | 0.524 / 0.419 ms | no draw / no draw | -0.52 / -0.42 |
| Points draw GPU, T2 lane view (tunnels on / off) | 1440p | 0.538 / 0.435 ms | no draw / no draw | -0.54 / -0.44 |
| Points draw GPU, T5 spawn view (on / off) | 1080p | 0.308 / 0.305 ms (2 draws) | 0.000 / 0.000 (1 draw) | -0.31 / -0.31 |
| Points draw GPU, T5 spawn view (on / off) | 1440p | 0.287 / 0.266 ms | 0.000 / 0.000 | -0.29 / -0.27 |
| GPU frame, thruster spawn view (window mean) | 1080p | 5.90 (5.80-5.97) ms | 5.09 (4.97-5.16) | **-0.81** |
| GPU frame, thruster spawn view | 1440p | 8.43 (8.33-8.77) | 7.71 (7.65-7.77) | **-0.72** |
| GPU frame, thruster lane view | 1080p / 1440p | 5.31 / 8.32 | 5.06 / 8.07 | -0.26 / -0.25 |
| Hulls drawn at the spawn view (Mid / Full, not Point) | 1080p / 1440p | 1192 / 1213 | 259 / 267 | -78 % |
| `STAT_SOL_NpcRedraw` T2-all on / off | 1080p | 0.0650 / 0.0535 ms | 0.0660 / 0.0535 | +0.001 / 0.000 |
| `STAT_SOL_NpcRedraw` T5-all on / off | 1080p | 0.1665 (0.163-0.170) / 0.1640 | 0.1780 (0.178-0.178) / 0.1610 | +0.0115 / -0.003 |
| `STAT_SOL_NpcRedraw` T2-all on / off | 1440p | 0.0550 / 0.0445 | 0.0555 / 0.0455 | +0.0005 / +0.001 |
| `STAT_SOL_NpcRedraw` T5-all on / off | 1440p | 0.1700 / 0.1540 | 0.1635 / 0.1520 | -0.0065 / -0.002 |
| NPC update `npcMs`, T2-all on / T5-all on | 1080p | 0.431 / 0.823 | 0.452 / 0.817 | +0.021 / -0.007 |
| NPC update `npcMs`, T2-all on / T5-all on | 1440p | 0.427 / 0.843 | 0.424 / 0.809 | -0.003 / -0.034 |
| NPC update `npcMs`, thruster spawn view | 1080p / 1440p | 1.743 / 1.674 | 1.716 / 1.659 | -0.026 / -0.015 |
| Collect (`CollectMs`), spawn view, ribbons on / off | 1080p | 0.0951 / 0.0770 | 0.0922 / 0.0711 | -0.003 / -0.006 |
| Collect, spawn view, ribbons on / off | 1440p | 0.0960 / 0.0741 | 0.0906 / 0.0699 | -0.005 / -0.004 |
| **Flame gate (ribbon tick), spawn view** | 1080p | 0.0516 (0.047-0.060) | 0.0493 (0.040-0.054) | -0.0023 |
| **Flame gate, spawn view** | 1440p | 0.0539 (0.044-0.079) | 0.0457 (0.041-0.052) | -0.0082 |
| Flame gate, lane view | 1080p / 1440p | 0.0336 / 0.0316 | 0.0338 / 0.0313 | 0.000 |
| Pool flames at the spawn view / lit plumes | 1080p | 24.0 / 89.5 | 22.5 / 91.0 | same workload |
| 1M ramp: mean / p95 / p99 / max frame | 1080p | 7.44 / 8.67 / 9.50 / 11.4 ms | 7.45 / 8.78 / 9.80 / 11.5 | +0.01 / +0.11 / +0.30 / +0.11 |
| 1M ramp: mean / p95 / p99 / max frame | 1440p | 8.47 / 9.81 / 11.22 / 27.1 | 7.70 / 8.90 / 9.53 / 12.0 | **-0.78 / -0.91 / -1.69 / -15.2** |
| 1M ramp: NPC update mean / p95 at 1M | 1080p / 1440p | 1.76 / 2.10, 1.78 / 2.29 | 1.77 / 2.15, 1.67 / 2.01 | noise / -0.10 / -0.28 |
| 1M ramp result, every stage | both | PASS (4 runs) | PASS (4 runs) | - |

Gate verdicts (SDD 54 section 5):

- **GPU, points: PASS.** No increase at any view; the far layer never draws in flight in B, so the points draw goes to
  zero at both views and resolutions (-0.27 to -0.71 ms per ProfileGPU frame), and the thruster profile's GPU window
  mean drops 0.72-0.81 ms at the spawn view and 0.25 ms at the lane view. That spawn-view drop is not decomposed: the
  points draw accounts for about 0.3 ms of it, the rest coincides with about 930 fewer hulls and was not measured
  separately. The 3-frame ProfileGPU frame totals point the same way but are too noisy to quote.
- **CPU, tiers / redraw / NPC update: PASS (no increase beyond the noise).** `STAT_SOL_NpcRedraw` deltas are -0.0065 to
  +0.0115 ms; the only one above 0.003 ms (T5-all tunnels on, 1080p, +0.0115) reverses at 1440p (-0.0065) and has no
  counterpart with tunnels off, so it is read as run-to-run noise of a single 240-frame average (n = 2 per tree), not
  a measured rise (not resolved below about 0.01 ms at n = 2). `npcMs` is within +-0.03 ms with no consistent sign
  (T2-all 1080p +0.021 / +0.026 on / off, but T2-own, the same view, +0.004 / +0.002, and 1440p -0.003 / -0.015);
  `CollectMs` is lower in B everywhere (-0.003 to -0.006 ms at the spawn view). **Attribution:** A already shows the CPU figures that 54f2 measured in B (redraw T2 0.053-0.065, T5
  0.16-0.17 ms; `npcMs` T2 0.43, T5 0.82 ms), so the rise against #50 noted in 54f2 comes from #52 / #53, not from
  #54.
- **CPU, hulls: decrease as expected (indirect).** Hulls drawn at the spawn view fall from about 1,200 to about 260
  (-78 %); `ASOLNpcVisuals::UploadShips` still has no stat of its own, so the upload time itself was not measured.
- **Flame CPU gate (must not grow): PASS.** Spawn view B 0.0493 ms (1080p) and 0.0457 ms (1440p) against A 0.0516 and
  0.0539. B had 6-9 % fewer pool flames (22.5 vs 24.0 at 1080p, 23.4 vs 25.6 at 1440p; lit plumes equal), which may
  account for part of the 1440p -0.008 ms. The 1080p delta (-0.0023 ms) lies inside A's own window spread
  (0.047-0.060), and this session's A (0.0516) differs from SDD 53's recorded 0.0542 for the same commit by about as
  much, so it is "did not grow", not a proven fix; B's median sits just under 0.05 ms at 1080p but its windows reach
  0.054, so the TECH_DEBT 0.05 ms miss is not shown closed. The lane view is unchanged (0.034 / 0.031). These runs
  added `-corelimit=16` (both trees), which SDD 53's command line does not show.
- **1M ramp: PASS** in both trees at every stage. At 1080p A and B are equal within noise (p95 8.67 vs 8.78 ms, A's
  range 8.22-9.12); at 1440p B is clearly faster (p95 -0.91 ms, max 27.1 -> 12.0 ms: both A runs had a 25-29 ms
  spike, not attributed). The GPU saving is about the same at both resolutions (points draw and the spawn-view delta
  are flat), so the reading is that the bottleneck moved: at 1080p the frame is game-thread bound (game about
  7.2-7.5 ms against GPU 5.1-5.9 ms at the spawn view), so a GPU saving cannot show; at 1440p the ramp mean matches the
  spawn-view GPU frame (A 8.47 vs 8.43, B 7.70 vs 7.71 ms), so the same saving of about 0.75 ms shows in frame time.
- **VR floor (record only):** frame GPU at 2560x1440 is 7.7-8.7 ms in B, under the XR-4 90 Hz budget of 11.11 ms
  (`VR_PERFORMANCE_TARGETS.md` section 3) at this resolution; one eye (3840x3744) and two eyes were not run (VR work
  deferred), so the VR floor itself is not measured.

Caveats: A mounts the Fab plugin (HEAD's `SOLTest.uproject`; B disables it), judged negligible in `-game`. The thruster
profile's `gpuMs` is the window mean of the GPU frame time, the tunnel profile's points draw is from 3 ProfileGPU
frames per window. 1280x720 and 4K were not part of this A/B. The settings files were restored byte-exact after every
run (MD5 checked) in both trees; the worktree was removed afterwards. The raw logs were deleted with the other temp
files, so these numbers cannot be re-derived from them.
