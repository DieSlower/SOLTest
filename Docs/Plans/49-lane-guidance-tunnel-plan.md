<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 49: Lane guidance tunnel near NPC lanes

SDD: [`SDDs/49-lane-guidance-tunnel.md`](../SDDs/49-lane-guidance-tunnel.md). Issue
[#49](https://github.com/DieSlower/SOLTest/issues/49).

**Grill-me complete (2026-11-11):** decisions D1-D15 are in SDD section 2. **Gate:** the derived choices Q1-Q11 (SDD
section 11) are pending with the user; the build starts at step 2 only once they are answered and the SDD, contract and
this plan are updated to match.

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
| `Source/SOLTest/Npc/SOLNpcLaneMath.h` / `.cpp` | `FSOLNpcLaneEntry::bResolved`, set by `BuildLaneFrame` | 49a |
| `Source/SOLTest/UI/SOLLaneTunnelLabel.h` / `.cpp` (new) | `LaneLabelClearOfDots`, `PlaceLaneLabel`, `FormatLaneLabelLines` | 49a |
| `Source/SOLTest/HudEvents/SOLHudBus.*` | `FSOLHudLaneTunnelLabel`, `PublishLaneTunnelLabel`, `GetLaneTunnelLabel` | 49a |
| `Source/SOLTest/Npc/SOLNpcTunnelComponent.h` / `.cpp` (new) | Slot mesh (built once), fixed-bounds ISM instances per lane in range, MID, tunnel texture and upload ring, per-frame update, label publish, verification override | 49b |
| `Source/SOLTest/Npc/SOLNpcVisuals.h` / `.cpp` | Create the component, call `UpdateTunnels` from `UpdateLanes`, glow push to its MID, `SetLanesDrawn` = lanes or tunnels drawn, override pass-through | 49b |
| `Shaders/Private/SOLNpcTunnel.ush` (new) | HLSL mirror of `SOLNpcTunnelMath` (slots, levels, fades, ring samples), reusing `SOLOrbitLine.ush` and `SOLNpcLane.ush` | 49b |
| `Tools/Npc/create_npc_assets.py`, `Content/SOL/Npc/M_SOLNpcTunnel.uasset` (new) | The tunnel material; rebuilt with `-SOLRebuild`, editor closed | 49b |
| `Source/SOLTest/UI/SOLFlightHud.*` | Read the label snapshot, project the anchors and dot rows, place and draw the glow text | 49b |
| `Source/SOLTest/Npc/SOLNpcTunnelSmoke.*` (new), smoke wiring | `-SOLSmokeNpcTunnel` (SDD section 8 item 2), later `-SOLSmokeNpcTunnelProfile` | 49b, 49c |
| `Source/SOLTest/Menu/SOLMenuText.h` | Info sentence and `@OverlayMenu` wording (SDD section 5); glossary entry text if Q7 (a) | 49b |
| Tests: `NpcTunnelMathTest.cpp` (new), `LaneTunnelLabelTest.cpp` (new), `NpcLaneMathTest.cpp`, the HUD bus test, InfoText | SDD section 6 cases 1-22; re-validation of the touched files | 49a (22 in 49b) |
| `Docs/GAME_MECHANICS.md` | "Lane tunnel" row in section 9, Lanes overlay note | 49c |
| `Docs/ARCHITECTURE.md` | `Npc/`, `UI/`, `HudEvents/` rows; the `Npc -> HudEvents` edge and sentence; flags and cvar | 49b (edge, files), 49c |
| `Docs/SDDs/38-npc-lane-lines.md` | Note in section 4.10: extension point consumed, `bResolved` added | 49a |
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
| 1b | Q1-Q11 answered by the user; SDD (decisions, contract), this plan and STATUS updated | main session, then opus / high | **Pending** |
| **49a** | | | |
| 2 | TDD: SDD section 6 cases 1-21 written from the contract only (no implementation shown) | opus / high | To do |
| 3 | `Tools\RunTests.bat -Build -Filter` `SOLTest.NpcTunnelMath`, `SOLTest.LaneTunnelLabel`, `SOLTest.NpcLaneMath`, the bus test: fail for the right reason (missing `SOLNpcTunnel`, `bResolved`, `PublishLaneTunnelLabel`), not a typo. Record the red run here | sonnet / medium | To do |
| 4 | Implement `SOLNpcTunnelMath`, `bResolved`, `SOLLaneTunnelLabel`, the bus snapshot, the constants; tests green, full suite green; SDD 38 note | opus / high | To do |
| 5 | Adversarial performance and style review of 49a (fresh opus / xhigh, report only; brief: changed files, SDD 49, style guide, performance guidelines, the TECH_DEBT items of SDD section 3); fixes; re-check against SDD 49; full suite; `Tools\UpdateMetrics.bat`; commit and push 49a | opus / xhigh, then opus / high | To do |
| **49b** | | | |
| 6 | Engine and code check first: the `SOLOL_*` / `SOLNL_*` entry points the tunnel reuses, the fixed-bounds ISM pattern (`USOLNpcHullInstances`), per-instance custom data in the material, how the HUD projects (`ProjectLocal`) and draws glow text, how the map state is read for the hide rule. If a decision turns out not viable, stop and report | opus / high | To do |
| 7 | Material: `M_SOLNpcTunnel` in `create_npc_assets.py`, `SOLNpcTunnel.ush` mirroring `SOLNpcTunnelMath`; rebuilt with the editor closed; compile clean | opus / high | To do |
| 8 | Renderer and label: `USOLNpcTunnelComponent`, the `ASOLNpcVisuals` hook, texture, instances on change, glow feed, occluder flag, `sol.NpcTunnel`, override, HUD label, Info / Controls text (case 22), `-SOLSmokeNpcTunnel` checks (SDD section 8 item 2); tests and full suite green; smokes PASS at 1280x720 and 1920x1080; `-SOLSmokeNpcLanes` still 35 / 35; the 1M ramp PASS | opus / high | To do |
| 9 | Review of 49b (fresh opus / xhigh, report only, same brief plus the material and `.ush`); fixes; re-check against SDD; full suite; metrics; commit and push 49b | opus / xhigh, then opus / high | To do |
| **49c** | | | |
| 10 | Screenshots T1-T6 at 1280x720, 1920x1080, 3840x2160, HUD glow 0 / 100 / 200%, plus a short sequence along the axis for popping; tuning of rho, widths, dot size, emissive, depth floor, label distance (the main session shows the shots to the user if a value needs the user's eye); final values in `SOLConstants.h` and an as-built section of the SDD; candidate shots deleted | opus / high | To do |
| 11 | Profiling (SDD section 8 item 6): `-SOLSmokeNpcTunnelProfile`, all lanes forced in range, T2 and T5, `sol.NpcTunnel` 0 / 1, three runs, 1920x1080 (gate: <= 0.15 ms GPU, <= 0.02 ms CPU), 3840x2160 and 3840x3744 recorded; the 1M ramp; results in the SDD | opus / high | To do |
| 12 | PIE check via unreal-mcp if the editor is open (else noted here) | sonnet / medium | To do |
| 13 | Docs: `GAME_MECHANICS.md`, `ARCHITECTURE.md`, `TECH_DEBT.md`, STATUS #49 Done, README gallery (2-3 shots), in-game text truth check against the constants, mock-up folder and its ToDo entry deleted | sonnet / medium | To do |
| 14 | Whole-ticket adversarial review (fresh opus / xhigh); fixes; full suite; metrics; commit and push 49c; close #49 | opus / xhigh, then opus / high | To do |

## Test contract summary

The full contract is SDD section 6 (cases 1-22). In short:

- **`SOLNpcTunnelMath` (new `SOLTest.NpcTunnelMath.*`):** constants and colours (1); the 100 -> 50 lane-radii fade,
  log linear, scale free (2); the radius 1x-3x the lane radius by traffic, never 0 for an empty lane (3); the
  ecliptic-up basis, right-handed, with the fallback (4); keep-right rows, chevron walls and their screen sides from
  both ends (5); the segment point nearest the viewpoint (6); ring level and spacing (7); nested own levels and the
  no-pop alpha (8); float-safe grid phases on AU lanes (9); depth, near and detail fades (10); slot coverage against
  a brute-force reference, exactly once (11); end-ring arcs and drawability (12); ring samples clockwise from 12
  o'clock (13); the label lane (14); end distances (15); the renderer feed, instance change detection and the shown
  rule (16).
- **Lane frame:** `bResolved` true for an empty lane with both bodies (17).
- **Label (new `SOLTest.LaneTunnelLabel.*`):** box clear of the projected dot rows (18); candidate placement, never
  over a row (19); the three text lines, no side line (20).
- **Bus:** label snapshot round trip (21). **Text:** tokens resolve, player guide current (22).

## Notes

- Brief every subagent with: SDD 49, this plan, the files above, the style guide, "delete temporary screenshots and
  captures once used", "no AI attribution in commits or code comments" and "no persistence change: stop and report if
  one seems needed".
- `SOLNpcTunnel.ush` mirrors `SOLNpcTunnelMath`; a change to one changes the other (file headers say so).
- D13 / Q9: if the #38 dash-length follow-up lands before 49b with a different smooth scheme, the ring spacing follows
  it (cases 7, 8, 11 and the shader change together).
- Pause points per CLAUDE.md (context above 35%, usage limits, 70% weekly): save resume state here (the step, files
  mid-edit, next action) and run `Tools\UpdateMetrics.bat`.

## Change log

- 2026-11-11: first draft with the SDD (grill-me D1-D15); Q1-Q11 pending with the user.
