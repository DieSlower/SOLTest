<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 49: Lane guidance tunnel near NPC lanes

SDD: [`SDDs/49-lane-guidance-tunnel.md`](../SDDs/49-lane-guidance-tunnel.md). Issue
[#49](https://github.com/DieSlower/SOLTest/issues/49).

**Grill-me complete (2026-11-11):** decisions D1-D16 are in SDD section 2, and the derived questions Q1-Q11 were
answered by the user the same day (SDD section 11, resolved decisions). No question is open; the build starts at step 2.
Keep-right rails and the rest of the lane traffic behaviour are the follow-up ticket
[#50](https://github.com/DieSlower/SOLTest/issues/50), not part of #49.

**Persistence: none.** `SOL::Settings::SAVE_VERSION` stays 13, `KEY_BINDING_LAYOUT_VERSION` stays 10, no
`UPROPERTY` of a save class and no key binding changes (D3). If the work shows that any of these is needed, **stop and
report**; do not choose.

## Commit parts (three, in this order)

Convention `Issue #49: Part 49x - <description>`. Each part is committed and pushed on `master` only once its tests
pass, its smokes pass and its adversarial performance and style review's findings are fixed (CLAUDE.md "Git").

1. **49a - tunnel math and label math** (steps 2-5): `Npc/SOLNpcTunnelMath` (pure), `FSOLNpcLaneEntry::bResolved`,
   `UI/SOLLaneTunnelLabel` (pure), the `FSOLHudBus` label snapshot, the constants. Nothing is drawn yet.
2. **49b - renderer and label** (steps 6-9): `M_SOLNpcTunnel`, `Shaders/Private/SOLNpcTunnel.ush`,
   `USOLNpcTunnelComponent` in `ASOLNpcVisuals`, the occluder flag, the glow feed, `sol.NpcTunnel`, the HUD label,
   `-SOLSmokeNpcTunnel`, the Info and Controls text.
3. **49c - tuning, profiling and docs** (steps 10-14): screenshots and tuning, `-SOLSmokeNpcTunnelProfile` and the
   budget gate, every doc, the README gallery, the mock-up folder deleted; whole-ticket review; closes #49.

## Files to change

| File | Change | Part |
|---|---|---|
| `Source/SOLTest/SOLConstants.h` | `NPC_TUNNEL_*` constants (SDD section 6 case 1, plus emissive, ring / dot / chevron sizes, segment counts, slot counts, sort priority, label constants, `NPC_TUNNEL_CVAR`), `SOL::Paths::NPC_TUNNEL_MATERIAL`, `SOL::NpcTunnelMaterialParams`, the two smoke flags | 49a (math constants), 49b (rest) |
| `Source/SOLTest/Npc/SOLNpcTunnelMath.h` / `.cpp` (new) | `SOLNpcTunnel`: fade, radius, basis, keep-right rows, nearest param and distance, ring level / spacing / own level / alpha, phases, depth / near / detail fades, slot coverage (rings, dots, chevrons), end arcs, ring samples, label lane, end distances, renderer feed | 49a |
| `Source/SOLTest/Npc/SOLNpcLaneMath.h` / `.cpp` | `FSOLNpcLaneEntry::bResolved`, set by `BuildLaneFrame`; the Q5 crossfade: new `LaneLineFadeWithTunnel`, `LaneShipPointAlpha` gains the tunnel fade (SDD section 4.2, case 17b) | 49a |
| `Source/SOLTest/UI/SOLLaneTunnelLabel.h` / `.cpp` (new) | `LaneLabelClearOfDots`, `PlaceLaneLabels` (greedy, strongest first, Q4), `FormatLaneLabelLines` | 49a |
| `Source/SOLTest/HudEvents/SOLHudBus.*` | `FSOLHudLaneTunnelLabels` (up to 4 `FSOLHudLaneTunnelLabel` entries, Q4), `PublishLaneTunnelLabels`, `GetLaneTunnelLabels` | 49a |
| `Source/SOLTest/Npc/SOLNpcTunnelComponent.h` / `.cpp` (new) | Slot mesh (built once), fixed-bounds ISM instances per lane in range, MID, tunnel texture and upload ring, per-frame update, label publish, verification override | 49b |
| `Source/SOLTest/Npc/SOLNpcVisuals.h` / `.cpp` | Create the component, call `UpdateTunnels` from `UpdateLanes`, glow push to its MID, `SetLanesDrawn` = lanes or tunnels drawn, override pass-through, bind `NpcTunnelTex` on the lane and far-point MIDs (Q5) | 49b |
| `Shaders/Private/SOLNpcTunnel.ush` (new) | HLSL mirror of `SOLNpcTunnelMath` (accordion warp and its inverse, slots, fades, ring samples), reusing `SOLOrbitLine.ush` and `SOLNpcLane.ush` | 49b |
| `Shaders/Private/SOLNpcLane.ush` | The line's close-up fade times `1 - F` from `NpcTunnelTex` (Q5, mirror of `LaneLineFadeWithTunnel`) | 49b |
| `Tools/Npc/create_npc_assets.py`, `Content/SOL/Npc/M_SOLNpcTunnel.uasset` (new), `M_SOLNpcLanes`, `M_SOLNpcPoints` | The tunnel material; the `NpcTunnelTex` texture parameter on the lane and point materials (the points' lane branch multiplies `SOLNL_LineFade` by `1 - F`, Q5); all rebuilt with `-SOLRebuild`, editor closed | 49b |
| `Source/SOLTest/UI/SOLFlightHud.*` | Read the label snapshot, project the anchors and dot rows, place and draw the glow text | 49b |
| `Source/SOLTest/Npc/SOLNpcTunnelSmoke.*` (new), smoke wiring | `-SOLSmokeNpcTunnel` (SDD section 8 item 2), later `-SOLSmokeNpcTunnelProfile` | 49b, 49c |
| `Source/SOLTest/Menu/SOLMenuText.h` and the Help glossary table | Info sentence and `@OverlayMenu` wording (SDD section 5); the Overlay Menu glossary entry mentions the tunnels (Q7, no new row) | 49b |
| Tests: `NpcTunnelMathTest.cpp` (new), `LaneTunnelLabelTest.cpp` (new), `NpcLaneMathTest.cpp` (17, 17b), `NpcLaneVisualsTest.cpp` (17c), the HUD bus test, InfoText, the glossary test | SDD section 6 cases 1-22; re-validation of the touched files | 49a (17c and 22 in 49b) |
| `Docs/GAME_MECHANICS.md` | "Lane tunnel" row in section 9, Lanes overlay note | 49c |
| `Docs/ARCHITECTURE.md` | `Npc/`, `UI/`, `HudEvents/` rows; the `Npc -> HudEvents` edge and sentence; flags and cvar | 49b (edge, files), 49c |
| `Docs/SDDs/38-npc-lane-lines.md` | Note in section 4.10: extension point consumed, `bResolved` added (49a); Q5 amendment note on D9 / D10 and section 4.8: the crossfade carries the tunnel fade, `LaneLineFade x (1 - F)`, sum still 1 (49b) | 49a, 49b |
| `Docs/PLAYER_GUIDE.md`, `Docs/Images/` | Regenerated with `Tools\WritePlayerGuide.bat` if the Controls or glossary text changes | 49b |
| `Docs/TECH_DEBT.md` | New debt found (existing items in SDD section 3 are not re-reported) | 49c |
| `Docs/Plans/STATUS.md` | #49 in progress (this draft), Done with results at the end | 1, 49c |
| `Docs/ToDo/49-lane-tunnel-mockups/`, `Docs/ToDo/README.md` | Folder and its list entry deleted | 49c |
| `README.md`, `Docs/PROJECT_METRICS.md`, `Docs/Images/` | `Tools\UpdateMetrics.bat` before each commit and at pauses; 2-3 gallery shots | 49a-49c |

## Steps

| Step | What | Who (model / effort) | Status |
|---|---|---|---|
| 0 | Grill-me (D1-D15) | main session | Done (2026-11-11) |
| 1 | SDD, this plan, STATUS #49 in progress | opus / high | Done |
| 1b | Q1-Q11 answered by the user; SDD (decisions, contract), this plan and STATUS updated; follow-up ticket #50 created | main session, then opus / high | Done (2026-11-11) |
| **49a** | | | |
| 2 | TDD: SDD section 6 cases 1-21 (with 17b) written from the contract only (no implementation shown) | opus / high | Done (2026-11-12): `NpcTunnelMathTest.cpp` (23 tests), `LaneTunnelLabelTest.cpp` (4), `NpcLaneMathTest.cpp` cases 17 / 17b, `HudEventBusTest.cpp` case 21 |
| 3 | `Tools\RunTests.bat -Build -Filter` `SOLTest.NpcTunnelMath`, `SOLTest.LaneTunnelLabel`, `SOLTest.NpcLaneMath`, the bus test: fail for the right reason (missing `SOLNpcTunnel`, `bResolved`, `PublishLaneTunnelLabel`), not a typo. Record the red run here | sonnet / medium | Skipped by the main session: the tests cannot compile without the new API (red by construction) |
| 4 | Implement `SOLNpcTunnelMath`, `bResolved`, `LaneLineFadeWithTunnel` and the `LaneShipPointAlpha` tunnel fade, `SOLLaneTunnelLabel`, the bus snapshot, the constants; tests green, full suite green; SDD 38 note | opus / high | Done (2026-11-12, review pending): the 62 tests of the four topics and the full suite (1874) green. The test's `TunnelBasis` length checks were relaxed to 1e-8 (main session decision): a 1 km lane built 5e10 m from the origin carries about 2e-9 of direction error in its rounded inputs. As built: label box centred on its anchor in both axes; dots and chevrons also get a cap fade at `NPC_TUNNEL_DETAIL_SLOTS_PER_SIDE` (never below 1 where the detail fade is above 0); the detail fade is log-linear; the bus snapshot carries the labels only (the shown tunnels' dot-row frames for the HUD come with 49b); SDD 38 note written |
| 5 | Adversarial performance and style review of 49a (fresh opus / xhigh, report only; brief: changed files, SDD 49, style guide, performance guidelines, the TECH_DEBT items of SDD section 3); fixes; re-check against SDD 49; full suite; `Tools\UpdateMetrics.bat`; commit and push 49a | opus / xhigh, then opus / high | Done (2026-11-12): review findings applied (SDD 49 D17 near fade weighted by the distance from the axis, user-approved; label box outward from its anchor; bus alpha, publish frame and `ClearLaneTunnelLabels`; detail cap one gap early; pole fallback blended over 1-2 degrees; label words in `SOLMenuText`; phase wrapping; new and extended tests; SDD as-built section 12); 63 topic tests and the full suite green; committed and pushed |
| **49b** | | | |
| 6 | Engine and code check first: the `SOLOL_*` / `SOLNL_*` entry points the tunnel reuses, the fixed-bounds ISM pattern (`USOLNpcHullInstances`), per-instance custom data in the material, how the HUD projects (`ProjectLocal`) and draws glow text, how the map state is read for the hide rule. If a decision turns out not viable, stop and report | opus / high | Done (2026-11-12): all viable. The map state is read from the player pawn (`ASOLShipPawn::IsMapOpen`, the existing `Npc -> Ship` edge, no `Npc -> Map` edge); the slot mesh reuses `SOLOrbitLineMesh::ApplyRibbonToMesh` with render-space bounds extensions so the ISM instances are never culled |
| 7 | Materials: `M_SOLNpcTunnel` in `create_npc_assets.py`, `SOLNpcTunnel.ush` mirroring `SOLNpcTunnelMath`; the `NpcTunnelTex` fade in `M_SOLNpcLanes` (`SOLNpcLane.ush`) and `M_SOLNpcPoints` (Q5); all rebuilt with the editor closed; compile clean; case 17c; SDD 38 Q5 amendment note | opus / high | Done (2026-11-12): the three materials rebuilt with `-SOLRebuild` (the hull's byte drift restored); `SOLNL_Pixel` gained the last argument `tunnelFade`; the points' lane branch reads row 1 .w of `NpcTunnelTex`; cases 17c green. SDD 38 Q5 amendment note written. |
| 7b | Deferred from the 49a review (pure, test first, before the component): (a) pure functions and tests for the end-ring alpha (floor `NPC_TUNNEL_END_RING_MIN_ALPHA`), the unclamped foot arc and foot point on the axis line, and the label anchors 4 tunnel radii ahead with hysteresis on the sign flip of the camera forward on the axis; (b) a quantised key (the displayed rounded values) checked before a label's text is reformatted, and the SDD 4.9 wording made exact; (c) `Reserve` of every per-lane array in the component at `BeginPlay` | opus / high | Done (2026-11-12): `LaneFoot`, `EndRingAlpha`, `EndRingSlot`, `LabelAheadSign` (dead band `NPC_TUNNEL_LABEL_FLIP_HYSTERESIS` 0.2), `LaneLabelAnchorRelM`, `TunnelDotRowSegmentRelM`, `BuildTunnelLaneData`, `StepRingPhase`, the visibility / upload rules, `RingSegmentsForSlot`, `TunnelSlotMeshVertexCount` (8,900), `ReadTunnelFadeTexel`; `LaneLabelDistanceKey` (the number printed on the stack exactly as `SOLHudFormat` prints it; new `SOLHudFormat::DistanceUnitM`), `MakeLaneLabelKey`, `EaseLaneLabelAlpha`; arrays reserved in `InitializeTunnels`. Test fix: `LaneTunnelLabel.DistanceKey`'s lambda was named `check`, which the UE `check()` macro swallowed (the test checked nothing); renamed `checkDistance`, no assertion changed. SDD 4.9 wording made exact. |
| 8 | Renderer and label: `USOLNpcTunnelComponent`, the `ASOLNpcVisuals` hook, texture, instances on change, glow feed, occluder flag, `sol.NpcTunnel`, override, HUD label, Info / Controls text (case 22), `-SOLSmokeNpcTunnel` checks (SDD section 8 item 2); tests and full suite green; smokes PASS at 1280x720 and 1920x1080; `-SOLSmokeNpcLanes` still 35 / 35; the 1M ramp PASS | opus / high | Done (2026-11-12): tunnel component, `M_SOLNpcTunnel`, HUD labels with D18 (anchor 2.5 tunnel radii, HUD panels avoided, compact one-line fallback), `-SOLSmokeNpcTunnel` 32 / 34 at 1280x720 and 1920x1080 before D19 (T4 and T5 draw no label, accepted as D19, the smoke now asserts 0 labels there), `-SOLSmokeNpcLanes` 35 / 35, overlay / HUD / holo / solid menu smokes PASS, 1M ramp PASS, full suite 1899 / 0, guide regenerated |
| 9 | Review of 49b (fresh opus / xhigh, report only, same brief plus the material and `.ush`); fixes; re-check against SDD; full suite; metrics; commit and push 49b | opus / xhigh, then opus / high | Done (2026-11-12): findings 1-5 and 8-13 fixed (near-plane clamp replaced by hardware clipping, label text cache, label hysteresis, visible-only avoid boxes, labels hide when the HUD does not draw, `RowCount` clamp, `STAT_SOL_HudLaneLabels`, smoke checks, Info text, tests); 6 and 7 deferred to 49c (steps 10a, 10b); full suite 1901 / 1901, smokes green (tunnel 35 / 35 at 1280x720 and 1920x1080); committed and pushed |
| **49c** | | | |
| 10-0 | **First (user report 2026-11-12: the wall chevrons do not look like the preview image).** Make the chevron look match the approved mock-up (`Docs/ToDo/49-lane-tunnel-mockups/tunnel8_rings_dots_sides.gif` and `tunnels8.py`: wall chevrons at 3 and 9 o'clock). Read the script for their exact shape, size, spacing, brightness, stroke and count per wall; take real T2 / T3 screenshots, compare them against the mock-up and list every difference before changing anything; then fix the shader (`SOLNpcTunnel.ush`) and the `NPC_TUNNEL_CHEVRON_*` constants. Note: the chevron stroke was thinned from 0.32 to 0.2 earlier in 49b, which probably contributes | opus / high | To do |
| 10 | Screenshots T1-T6 at 1280x720, 1920x1080, 3840x2160, HUD glow 0 / 100 / 200%, plus a short sequence along the axis for popping; tuning of rho, widths, dot size, emissive, depth floor, label distance (the main session shows the shots to the user if a value needs the user's eye); final values in `SOLConstants.h` and an as-built section of the SDD; candidate shots deleted | opus / high | To do |
| 10a | Review finding 6: chevron fill: fade the detail elements by true distance below about 1 Rt, or cap their screen size | opus / high | To do |
| 10b | Review finding 7: far rings white at the vanishing point: multiply alpha by saturate(projected radius in px / ribbon half extent), 64-96 segments for ring slots 0-1, GPU scaling past 8 lanes measured in the profile, dots faint at 3 px | opus / high | To do |
| 11 | Profiling (SDD section 8 item 6): `-SOLSmokeNpcTunnelProfile`, all lanes forced in range, T2 and T5, `sol.NpcTunnel` 0 / 1, three runs, 1920x1080 (gate: <= 0.15 ms GPU, <= 0.02 ms CPU), 3840x2160 and 3840x3744 recorded; the 1M ramp; results in the SDD | opus / high | To do |
| 12 | PIE check via unreal-mcp if the editor is open (else noted here) | sonnet / medium | To do |
| 13 | Docs: `GAME_MECHANICS.md`, `ARCHITECTURE.md`, `TECH_DEBT.md`, STATUS #49 Done, README gallery (2-3 shots), in-game text truth check against the constants, mock-up folder and its ToDo entry deleted | sonnet / medium | To do |
| 14 | Whole-ticket adversarial review (fresh opus / xhigh); fixes; full suite; metrics; commit and push 49c; close #49 | opus / xhigh, then opus / high | To do |

## Test contract summary

The full contract is SDD section 6 (cases 1-22). In short:

- **`SOLNpcTunnelMath` (new `SOLTest.NpcTunnelMath.*`):** constants and colours (1); the 100 -> 50 lane-radii fade,
  log linear, scale free (2); the radius 1x-3x the lane radius by traffic, never 0 for an empty lane (3); the
  ecliptic-up basis, right-handed, with the fallback (4); keep-right rows, chevron walls and their screen sides from
  both ends (5); the segment point nearest the viewpoint (6); the continuous ring spacing (7); the accordion warp, its
  inverse, the no-pop sweep and the lane-fixed near rings (8); the ring phase advance and its float path on AU lanes
  (9); depth, near, detail, end and cap fades (10); slot coverage against a brute-force reference, exactly once (11);
  end-ring arcs and drawability (12); ring samples clockwise from 12 o'clock (13); the label lanes, strongest first,
  and the dimmed side alpha (14); end distances (15); the renderer feed (zeros when the last tunnel goes), instance
  change detection and the shown rule (16).
- **Lane math:** `bResolved` true for an empty lane with both bodies (17); the Q5 line and point crossfade with the
  tunnel fade, sum 1 (17b). **Lane visuals:** the `NpcTunnelTex` parameter on both lane materials (17c).
- **Labels (new `SOLTest.LaneTunnelLabel.*`):** box clear of the projected dot rows (18); greedy placement of up to 4
  labels, never over a row or another label (19); the three text lines, no side line (20).
- **Bus:** the label snapshot round trip (21). **Text:** tokens resolve, player guide current (22).

## Notes

- Brief every subagent with: SDD 49, this plan, the files above, the style guide, "delete temporary screenshots and
  captures once used", "no AI attribution in commits or code comments" and "no persistence change: stop and report if
  one seems needed".
- `SOLNpcTunnel.ush` mirrors `SOLNpcTunnelMath`; a change to one changes the other (file headers say so).
- D13 / Q9 resolved: the #38 follow-up chose the continuous accordion scale (`b5bae2c`), and the rings use the same
  principle (SDD section 4.5). The accordion's slide of the far rings is accepted; its lever is the constant-spacing
  zone `D0` (SDD section 10).
- Q5: the lane smoke (`-SOLSmokeNpcLanes`) hides the tunnels through their override, so its 35 checks keep measuring
  the #38 crossfade; the new tunnel smoke checks the combined crossfade.
- Pause points per CLAUDE.md (context above 35%, usage limits, 70% weekly): save resume state here (the step, files
  mid-edit, next action) and run `Tools\UpdateMetrics.bat`.

## Resume notes (49b, 2026-11-12, reviewed and committed; next: 49c step 10-0)

**Built, not committed:** every 49b file (SDD 49 section 12b lists them) plus D18 (the user's answer to the label fit:
`NPC_TUNNEL_LABEL_AHEAD_RADII` 2.5, `PlaceLaneLabels` avoids the HUD panel boxes the HUD records in `AddHoloPanel`,
`DrawCard`, the key-hint line and `DrawRadar` (previous frame's set), compact one-line fallback
`FormatLaneLabelCompactLine` / `MakeLaneLabelCompactKey`). Docs synced (SDD 38 and 49, ARCHITECTURE, GAME_MECHANICS,
STATUS, Info text, guide regenerated: only the three images with a real text change kept, the others were version-line
drift and restored). Full suite 1899 / 0.

**Decided (D19, user, 2026-11-12):** T4 and T5 draw no label, by design; the smoke asserts 0 labels drawn there.

**Step 9 done:** the review's findings 1-5 and 8-13 are fixed (SDD 49 section 12b), findings 6 and 7 are in 49c (steps 10a, 10b). Full suite 1901 / 1901, smokes green (NpcTunnel 35 / 35 at 1280x720 and 1920x1080, NpcLanes 35 / 35, Overlay 50 / 50, Hud 45 / 45, HoloMenu 86 / 86, SolidMenu 296 / 296); 49b committed and pushed. Next: 49c step 10-0 (chevron look vs the mock-up).

## Change log

- 2026-11-11: first draft with the SDD (grill-me D1-D15); Q1-Q11 pending with the user.
- 2026-11-11: Q1-Q11 answered by the user and recorded as SDD decisions; the gate removed; files, steps and the test
  summary updated (labels per shown tunnel, the Q5 crossfade and material rebuilds, the accordion ring spacing);
  follow-up ticket #50 (lane traffic flow) created.
