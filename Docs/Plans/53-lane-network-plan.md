<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 53: Lane network

**Issue:** #53 (To do in [`STATUS.md`](STATUS.md), row 3b5d; no GitHub issue checked or created by this draft). SDD:
[`SDDs/53-lane-network.md`](../SDDs/53-lane-network.md). Test-first contract:
[`53-lane-network-contract.md`](53-lane-network-contract.md).
Status: **DRAFT, amended 2026-11-24 with the user's answers to Q1-Q30** (SDD 53 section 16) **and 2026-11-25 with the
answers to O1-O10** (SDD 53 section 17; SDD and contract amended). **The user confirmed on 2026-11-25 the population tier values
(O6), the partner hysteresis values (O3: switch ratio 0.8, minimum stay 30 days, warm-up 64 epochs, stepped epoch to
epoch so the partner set also depends on the time of the last jump), the Sun-safety override UI (O4) and that the
Moon can be an island-link relay; the gate is cleared.** Grill-me decisions D1-D8,
Q1-Q30 and O1-O10 are the user's.
**Persistence: none planned.** `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay. The Sun safety override
(O4) is session-only. A part that finds a saved setting is needed (for example keeping the override across launches)
stops and reports; a schema change needs the user's approval.

**Every brief repeats:** the pixel rule (LOD, fade, visibility and activation thresholds in 1080p reference pixels,
never distance or counts; a fixed pool is the only cap and is reported as a limitation when hit); delete temporary
screenshots and scratch files after use; no AI attribution in commits or code; the file banner and comment rules of
`Docs/STYLE_GUIDE.md`.

## Parts

Each part is built by its own subagent (context under about 35%), then reviewed by a fresh adversarial `opus`
subagent (performance and style, report only; the relevant `TECH_DEBT.md` items passed so they are not re-reported),
fixes applied, full suite green, then committed and pushed (`Issue #53: Part 53x - ...`) with `STATUS.md` and the
metrics (`Tools\UpdateMetrics.bat`) in the same commit.

### 53a: tests first, pure network math contract (separate author)

1. Done 2026-11-25: SDD and contract amended with the answers to O1-O10. The user confirmed the tier and hysteresis values
   (2026-11-25, gate cleared); they are pinned in the contract. Reword the STATUS.md row 3b5d to "pixel
   on/off, Sun clearance" (Q24) in the 53a commit.
2. Test author (`opus`, high, **contract only, no implementation shown**): write
   `Source/SOLTest/Tests/LaneNetworkMathTest.cpp` (`SOLTest.LaneNetwork.*`) from the contract, cases 1-15 with 1b
   (`PartnerHysteresis`) and 1c (`IslandLinks`); case 15 (`SunSafety`) covers the pinned zone, override and arrival
   rules only.
3. Add the header `Traffic/SOLLaneNetworkMath.h` with the signatures and stub bodies so the tests compile; run
   `Tools\RunTests.bat -Build -Filter SOLTest.LaneNetwork`; confirm each case fails for the right reason.

### 53b: pure math implementation

1. Implement `Traffic/SOLLaneNetworkMath.cpp` and the constants (`SOLConstants.h`, with `static_assert`s for the
   contract's invariants). No allocation per call beyond caller-reserved arrays; Dijkstra on a fixed array.
2. Green run of `SOLTest.LaneNetwork`, then the full suite.

### 53c1: node table, schedule, slots, windows, route query, time-jump hook

1. Node table (24 nodes, SDD 53 4.2): planets, Moon and dwarf planets from the registry; the 10 asteroids from
   `SOLAsteroidBelt::RealAsteroids()` (new `Npc -> MinorBodies` edge, positions with the existing `SOLKepler`
   propagation); tiers (as confirmed, O6) and region radii (O5: larger end, belt 4e6 m); aphelia for `L0` (asteroids
   from their elements, not the registry walk). Files `Npc/SOLLaneNodeTable.h/.cpp`.
2. `DefaultLayout`: the 8 base rows stay at slots 0-7 with the population weights, sqrt of the tier product (Q4, O6,
   Q23). In 53c1 the lane ships still use these interim sqrt weights over the 8 base lanes only (no partner lane gets
   ships until 53c2), which makes Earth-Moon about 31 percent of the lane ships; accepted by the user. With all lanes of
   the daily set the measured share is 9.9-13.6 %.
3. `FSOLLaneNetworkSchedule` (`Traffic/SOLLaneNetworkSchedule.h/.cpp`, owned by `USOLLaneTrafficSubsystem`, not saved):
   the per-epoch lane set (base lanes, the picks stepped from the previous epoch, `PairsFromPicks`, then island links
   with Earth as the root, O2, O3), slots (`AssignLaneSlots`, report on column overflow, Q28), the look-ahead ring
   buffer (`NPC_LANE_NETWORK_HORIZON_EPOCHS`, one epoch per epoch change stepped from the one before, a warm-up over
   `NPC_LANE_PARTNER_WARMUP_EPOCHS` and a full rebuild on a time jump), the Sun state (`EpochClearanceBoundM`,
   `SunStateFromLookBack`), the windows (`LaneOpenWindow`), edge times and the route query with the horizon rule (SDD
   6.3).
4. Time-jump hook: `USOLSimClockSubsystem::GetTimeSetCount`, `USOLNpcSubsystem` marks a jump through
   `USOLLaneTrafficSubsystem::MarkNetworkTimeJump` before `AdvanceNetwork`.
5. Tests in `Tests/LaneNetworkSubsystemTest.cpp` (`SOLTest.LaneNetworkSubsystem.*`, including `MeasuredCost`, which
   asserts the SDD 13 gates) and `NpcRail.LaneSlotColumns`; the 250-year re-measure (SDD 53 4.3, 4.4, 7.3).
6. Re-validate `NpcScaleMathTest`, `NpcTrafficMathTest`, `NpcRailTest` and `NpcTunnelFarFieldTest` (stale 8-lane
   assumptions re-pinned), `FloatPathAtMaxLength` at **151 AU** (SDD 53 4.7, O8).

### 53c2a: ship blocks, spawn, group table upload, CPU side (pure logic first)

Contract: [`53-lane-network-contract.md`](53-lane-network-contract.md) section "53c2 additions", cases 16-24 and 26.
**Gate:** open. The user answered P1-P6 on 2026-11-25 (contract table "Resolved decisions", SDD 53 section 18): P1
a turn follows the leg after it, P2 a promoted ship is demoted and docked at once, P3 / P4 as recommended
(contiguous blocks of 100, last block partial; joins only at an episode open; ascending slot order when the pool is
short; a full exact re-share at a time jump), P5 inactive lanes hidden always (overlay on or off) with the 2.4 px
threshold scaled by `LaneTunnelQualityScale`, P6 an Approach on a closing lane is Stale (Hold unchanged until 53e).
Every assertion in cases 16-26 is pinned; the test author writes all of them.

1. Test author (`opus`, high, **contract only, no implementation shown**): write
   `Source/SOLTest/Tests/LaneNetworkTrafficTest.cpp` (`SOLTest.LaneNetworkTraffic.*`, cases 16-24, 26); stub the
   signatures so it compiles; `Tools\RunTests.bat -Build -Filter SOLTest.LaneNetworkTraffic` fails for the right
   reason.
2. Pure code: the block layout and pool (`ShipBlockCount`, `AssignShipBlocks`, `ReleaseShipBlocks`, `TakeShipBlocks`,
   `CanReleaseSlotBlocks`, `LaneEpisodeOpens`, `EpisodeTakeOrder`, `SlotBlockLists`, `SumBlockCounts`, `ShipBlockDirtyRanges`),
   `LaneShipInSpace`, `LaneWindowGpuS` / `LaneWindowTexel`, `LaneTrafficRadiusPx` / `LaneTrafficActive`,
   `LaneOpenAt` in `Traffic/SOLLaneNetworkMath`; the `MakeSpawn` block overload, `CountBlockSpawns` and
   `LaneShipAiTier` in `Npc/SOLNpcScaleMath`; `RailTexelRegions`, `LaneRailGroup`, `IsParked`, `GpuParkedRailTexels`,
   `WriteLaneWindowRow` and `SlotLaneProfile` in `Npc/SOLNpcRail`; the constants (`NPC_GROUP_TEXTURE_ROWS` 6 -> 7 and the rest of the
   contract's table) with their `static_assert`s.
3. Spawn: the lane ships go into blocks shared over the spawn epoch's slotted set (exact, every block assigned); a
   lane rail's group is `LaneRailGroup(slot)` = `FSOLNpcGroupTable::LaneSlotColumn(slot)` (and `LaneSlotCapacity()`),
   never the slot number, so slots from `LaneCount` on use the columns after the clusters and battles and none shares
   a cluster or battle column; the group table and rail frame cover every slotted column; each slot's profile is
   frozen at its episode open (`SlotLaneProfile` from `SumBlockCounts` of its blocks' in-wall counts). **`Npc/SOLNpcLaneMath`
   must become column-aware too:** `BuildLaneFrame`, `WriteLaneGroupTexelW`, `CountLaneShips` / `AccumulateLaneShips`
   and the ribbon layout (`BuildLaneRibbonLayout`) work on lanes 0 .. `LaneCount` - 1 today, so a slot column at 34 and
   up would get row 0 `.w` = 0, which the point material reads as "line stays, point hides": partner-lane ships would
   vanish whenever the Lanes overlay is on. This ends the 53c1 interim (Earth-Moon about 31 %).
4. Block moves: release, take, slot block lists, dirty ranges and the partial rail upload (`RailTexelRegions`, the
   `UpdateTextureRegions` rects) built and tested headless, the 0.5 ms per-move gate measured. **Interim:** the
   per-epoch moves are switched on only in 53c2b, because parked and docked ships need the shader's hide (the current
   shader would draw a parked rail as a midpoint disc). Until then each lane keeps its spawn blocks and a Gone lane
   that still owns blocks keeps its slot held (`SetSlotHeld`; a column overflow is reported as usual). Consequences
   of the interim, accepted only until 53c2b: a lane that opens after the spawn has no ships, a closed lane still
   shows its ships (the shader ignores the window until 53c2b), and held slots can overflow the columns in a long
   warp run.
5. Group texel row 6 written every frame (`WriteLaneWindowRow`): each slot's window from `GetSlotWindow` relative to
   the GPU epoch, and the active flag (`LaneTrafficActive` with the live focal and the slot's CPU-tier ship count).
   The active flag uses `LaneTrafficActive(px, cpuTierShips, LaneTunnelQualityScale(QualityPreset))` (P5) and does
   not depend on the Lanes overlay. The shader reads the window from 53c2b and the active flag from 53d (step 4).
6. Tiers: a docked lane ship (`LaneShipInSpace`, double; a turning ship follows the leg after the turn, P1) is never
   promoted (`LaneShipAiTier`, Q26); a promoted ship whose leg ends where the next leg is not flown is demoted to Far
   and docked at once, no fade (P2). Merge readout and lane search skip closed lanes (`FSOLLaneMergeLane::bOpenNow`,
   Q30); `CheckLaneLatch` returns Stale for an Approach on a closed lane (dropped and searched again, P6), Hold is
   unchanged until 53e.
7. CPU point layer: the slot column mapping and parked rails skipped. **SDD R8 says the shader and its CPU mirror
   change together**, so the window gate on the CPU point layer (case 25) lands in 53c2b with the shader, not here.
8. Re-validate `NpcScaleMathTest` (`MakeSpawn`, `CountLaneSpawns`), `NpcLaneMathTest`, `NpcRailTest` (the
   "6 group rows" case re-pinned to 7), `NpcTrafficMathTest`, `LaneTrafficMathTest` / `LaneAutoRulesTest`
   (`MergeReadout`, `FindLaneLeg`, `CheckLaneLatch` and `LaneAutoDecide` with `bOpenNow`) and the tests that still assume 8 lanes (deleted or re-pinned);
   full suite; adversarial review; commit and push.

### 53c2b: traffic shader window, mirror, block moves on, smokes

1. Test author (`opus`, high, contract case 25 only): `SOLTest.NpcRail.GpuLaneWindowMirror` in
   `Tests/NpcRailTest.cpp` (agreement with `LaneShipInSpace` outside the float band, no flicker within an epoch and
   across a re-epoch, window extremes, parked rails).
2. `SOLNTR_LaneShipDrawn` in `Shaders/Private/SOLNpcTraffic.ush` and `SOLNpcTrafficMirror::LaneShipWindowDrawn` in
   the same change (R8); the point material's vertex HLSL in `Tools/Npc/create_npc_assets.py` loads row 6 and hides
   docked and parked lane ships (a new call, so the material is rebuilt in the editor and the asset saved); the CPU
   point layer draws a Point-tier lane ship only when the mirror says drawn.
3. Block moves switched on per epoch step (release, take, partial upload) by the P4 policy: only slots whose episode
   opens take blocks (`EpisodeTakeOrder`, ascending slot order when the pool is short), targets from `LaneShipShares`
   over the current set, no mid-episode top-up; a time jump re-shares every block exactly (`AssignShipBlocks`, one
   full rail upload). Two parked conventions exist and a moved block must re-bucket both: the rail's group
   (`FSOLNpcRail::Group` = `NPC_RAIL_PARKED_GROUP`, 0xFFFF) and the Mass group fragment, whose parked ships sit in the
   trailing bucket at index `groupCount` (`FSOLNpcGroupTable::Num()`); a take moves a ship out of both, a release into
   both. `SOLNpcRail::EvaluateAtTimeM` (and `EvaluateM`) silently return the universe origin for an invalid group (a
   parked rail included), so any consumer that evaluates a released ship before its re-bucket reads the origin, not an
   error: guard on `IsParked` or assert. Re-measure `SOLTest.NpcRail.BlockMoveCost` with the full move (release, take,
   re-bucket, partial upload); 53c2a's figure is a partial measurement.
4. Smokes `-SOLSmokeNpcTraffic`, `-SOLSmokeNpcLanes`, `-SOLSmokeLaneMerge` at 1280x720, 1920x1080 and 4K, plus a warp
   run across several epoch changes (lanes appear and drain, no ship vanishes mid-leg, island links appear, block
   moves within the 0.5 ms gate); `GameUserSettings.ini` MD5 unchanged; temporary screenshots deleted.
5. The final-rule re-measure was done in 53c1 (SDD 53 4.3, 4.4, 7.3). A figure that breaks a bound (columns over 64,
   pool over 24, horizon under the longest leg) or visible island-link churn is reported to the user, not fixed unasked.

### 53d: tunnel classes and pool

1. `USOLNpcTunnelComponent`: per-lane `LaneTunnelMaxRadiusPx` and `StepTunnelClass` (Off, Reduced, Full); Off lanes
   build no frame, no texels, no instance; a Reduced mesh (rings and end rings) beside the Full slot mesh;
   `SelectTunnelPool` with `NPC_TUNNEL_INSTANCE_POOL` = 24 shared by both classes; overflow logged once per change and
   shown by a stat counter (reported as a limitation). `NPC_TUNNEL_VERTEX_BUDGET` / `_LANES` replaced (Q18).
2. Knee fix (Q19): 160 ring slots per side, 15,380 vertices per Full lane; the `static_assert`s and
   `NpcTunnelFarFieldTest` re-pinned; the knee item removed from `TECH_DEBT.md`.
3. Legacy controls (Q20): `NPC_LANE_FADE_NEAR/FAR_RADII` and the label band restated in reference px of the lane
   radius with the live focal; `.ush` and CPU mirror changed together, `NpcTunnelMirrorTest` extended. The 4-label cap
   stays (already in `TECH_DEBT.md`).
4. Tunnel open fade (3 s real, Q9) on close and reopen; point vertex shader early-out for inactive lanes (Q21, Q22:
   reads the group texel row 6 active flag uploaded since 53c2a; inactive lanes are hidden always, with the Lanes
   overlay on or off, and the flag's 2.4 px threshold is already scaled by the QualityPreset, P5), skipping
   the rail evaluation of inactive and docked ships (the docked hide itself is done in 53c2b).
5. `-SOLSmokeNpcTunnel` gains a pool-full forced view (24 Full), a class-switch view and a closing-lane view; PIE
   screenshots of the Earth hub and the belt.

### 53e: AUTO routing and Sun safety

1. Tests first for the pure plan logic (hop sequencing, per-hop `Left`, re-plan trigger at an epoch change, ETA sum,
   finish-if-safe check with and without the override, Sun detour waypoint): extend the contract (case 15 and new
   cases) with these (separate author) before the code.
2. `FSOLShipStateFragment`: transient plan (hops with lane identities, current hop, serial), reset on every order write
   (`Ship/SOLAutopilotOrder`), never a `UPROPERTY`, never saved.
3. `USOLShipFlightProcessor`: route at order time (Q11) with the time-window Dijkstra (O7), latch each hop, transfer
   through the next hop's merge point, D20 search off while a plan exists (Q15), re-plan on an epoch change (Q14),
   finish-if-safe or leave on a closing held lane, **the Sun detour on every AUTO order** (Move To, War power, Follow,
   Chase Down, router legs; O4) and the target-inside-zone arrival, both skipped while the player's override is on;
   War power still ignores lanes.
4. Sun safety override (O4, SDD 53 6.5): `ESOLAction::SunSafetyOverride` appended after `MoveToWarPower`, a row on the
   Star / Planet / Moon / Dwarf rows after Move To (War Power), fixed label "Toggle Sun Safety" (`SOLMenuText`),
   `SOL::Interact::MAX_ACTIONS` 10 -> 11 with `ESOLHudLine::RadialItem10`; a session-only flag on `USOLShipSubsystem`
   (default off, never saved, no key binding); a "SUN SAFETY OFF" HUD line by the AUTOPILOT marker while overridden.
   Check whether promoted NPCs (AI tiers) can cross the danger zone and report (SDD 53 R20). Re-validate `WarPowerMoveTest`, `InteractMenusTest`, `HoloRadialTest`; radial and list menu
   screenshots at 1280x720, 1920x1080 and 4K (11 sectors fit, R19). If keeping the flag across launches is wanted,
   stop and ask (settings schema, `SAVE_VERSION` 14).
5. HUD: "VIA <node>" line and the lane-closed and stopped-at-danger-zone notices (`SOLMenuText`), ETA of the whole plan
   with the first and last mile (Q13). System map: the plan polyline in a new `ESOLRouteDrawStyle`, through the moving
   nodes each frame (Q16).
6. `SCOPE_CYCLE_COUNTER` around the AUTO lane rules (closes the SDD 50 profiling debt).
7. New smoke `-SOLSmokeLaneRoute` (two-hop trip at warp, a lane closing mid-plan and the re-plan, a Sun-closing held
   lane left with a detour, a direct Move To across the Sun with the detour, the same with the override on, VIA line,
   map route); `-SOLSmokeAutoLanes`, `-SOLSmokeRoute`, `-SOLSmokeRouteOverlay`, `-SOLSmokeChase` re-run;
   `AutopilotRegression.NoLaneBitIdentical` green (paths outside the danger zone stay bit-identical).

### 53f: profile, docs, gallery

1. Profile per SDD 53 section 13 (interleaved A/B worktrees against the pre-#53 commit): tunnel draw with the pool
   full, CPU on vs off, the new CPU gates (epoch step at 30 d/s, look-ahead rebuild, route query, class pass, block
   move), points draw recorded, flame CPU gate re-measured and reported (Q27, no fix), 1M ramp; 2560x1440 recorded (VR
   note: record only). Misses reported to the user, not relaxed by the agent.
2. Docs: `GAME_MECHANICS.md` (nodes, the daily lane set with hysteresis and island links, population weights, Sun
   closing and finishing trips, the Sun danger zone, detour and the override row, routing, transfer, ETA, map route)
   and the in-game Controls / Info text in the same change;
   `ARCHITECTURE.md` (new file, subsystem role, the `Npc -> MinorBodies` edge, the epoch step in the frame order);
   `TECH_DEBT.md` (fixed items removed, new ones added); `STATUS.md`.
3. README gallery: 2-3 new 1920x1080 shots (`Docs/Images/53-*.png`: the Earth hub, a belt lane, a closing lane near
   the Sun, a VIA trip with its map route); temporary shots deleted.
4. Whole-ticket adversarial review, fixes, full suite, commit and push.

## Progress

- 2026-11-22: SDD, plan and contract drafted from D1-D8. Waiting for the user's answers to Q1-Q30.
- 2026-11-24: Q1-Q30 answered and folded into the SDD (section 16), this plan and the contract. Node and lane counts,
  reachability and the column worst case measured by a script (SDD 53 4.3). `TECH_DEBT.md`: the 4-label cap recorded
  as a count control; Q29's legacy NPC LOD item cross-referenced. Waiting for the user's answers to O1-O10 (SDD 53
  section 17).
- 2026-11-25: O1-O10 answered and folded into the SDD (section 17 now resolved decisions), this plan and the contract
  (new cases 1b `PartnerHysteresis`, 1c `IslandLinks`, 15 `SunSafety`; sqrt weights in case 12; O1 and O10 constants
  pinned). Lane counts, columns, pool and Earth-Moon share re-derived from the baseline, to be re-measured in 53c
  (step 6).
- 2026-11-25: the user confirmed the tier values (O6), the hysteresis values (O3) and the Sun-safety override UI (O4,
  `MAX_ACTIONS` 10 -> 11, danger-zone target stops at the zone edge, override applies at once), and reversed "the Moon
  is never a relay": the Moon can relay island links (SDD 4.3, contract 1c `IslandLinks`). 53a step 2 may start.
- 2026-11-25: 53c1 done, pending commit (review fixes applied). Gates measured and now asserted by
  `SOLTest.LaneNetworkSubsystem.MeasuredCost`: full rebuild 1.7-2.1 ms (gate 5 ms, was 39.3 ms before the fixes), epoch
  step 0.0056-0.0077 ms per frame at 30 d/s and 60 fps (gate 0.01 ms, was 0.0265). Routes over the lane graph ignore
  windows beyond the 960 d horizon (user decision, SDD 6.3, R11). The final-rule 250-year figures are in SDD 4.3, 4.4
  and 7.3. 53c2 next, using `LaneSlotColumn`.
- 2026-11-25: 53c2 split into 53c2a (pure logic, blocks, spawn, group texel row 6, tiers, merge and search skip) and
  53c2b (shader window and mirror, block moves on, smokes). The 53c2 contract (cases 16-26) is written; pending user
  decisions P1-P6 (U-turn window, promoted ship at leg end, block membership for other populations, pool policy, Q22
  scope, Approach latch on a closing lane) gate the code and assertions that depend on them, not the rest of the
  test file.
- 2026-11-25: the user answered P1-P6: P1 the leg after the turn decides (the ship vanishes at the end ring on arrival,
  matching the future station docking), P2 demoted and docked at once (no fade), P3 / P4 as recommended, P5 changed
  (inactive lanes hidden always, also with the overlay off; the 2.4 px threshold scales with `LaneTunnelQualityScale`,
  so `LaneTrafficActive` takes a `thresholdScale`), P6 an Approach on a closing lane is Stale, Hold unchanged until
  53e. Contract cases 16-26 pinned in full (new `EpisodeTakeOrder`, scaled activation rows, P6 latch cases); 53c2a
  may start with the test author.
- 2026-11-25: 53c2a steps 1-2 done (pending commit). The test file `LaneNetworkTrafficTest.cpp` (cases 16-24, 26) is
  green, 11 of 11, with the pure code in place: the block layout and pool, `LaneShipInSpace`, the window texel, the
  activation test with its quality scale, `LaneOpenAt` (`Traffic/SOLLaneNetworkMath`); the `MakeSpawn` block overload,
  `CountBlockSpawns`, `LaneShipAiTier` (`Npc/SOLNpcScaleMath`); `RailTexelRegions`, `LaneRailGroup`, `IsParked`,
  `GpuParkedRailTexels`, `WriteLaneWindowRow`, `SlotLaneProfile` (`Npc/SOLNpcRail`); `MergeReadout` and `FindLaneLeg`
  skip a lane with `bOpenNow` false, and `CheckLaneLatch` returns Stale for an Approach on such a lane (P6). The 7-row
  group texture was checked: every material reads it by integer `Load(int3(column, row, 0))` and the C++ sizes it from
  `NPC_GROUP_TEXTURE_ROWS`, so the extra row breaks nothing. **Next: steps 3-7** (spawn into blocks, the column-aware
  `SOLNpcLaneMath` / frame / buckets, the row 6 upload, tiers, the CPU point layer), still under the interim of step 4:
  no block moves until 53c2b.
- 2026-11-25: 53c2a steps 3-7 wired (pending commit). `Spawn()` builds the node table, configures the network and
  advances it to the spawn epoch first (`ConfigureLaneNetwork`), then fills the slot columns (`BuildSlotColumns`: a
  partner lane's radius is the larger region radius, O5; weights `LaneWeight` of the end tiers over the spawn epoch's
  set), shares the blocks exactly (`AssignShipBlocks`), freezes each column's profile from its blocks' in-wall counts
  (`CountBlockSpawns` + `SumBlockCounts`) and builds the rails with the block `MakeSpawn` and `LaneRailGroup`. The group
  table's lane arrays, the rail frame, `SOLNpcLaneMath` (lane frame, `.w` rows, counts, ribbon) and the tunnel / visuals
  code are indexed by column (`FSOLNpcGroupTable::ExtraSlotCount`, `ColumnSlot`, `LaneColumnEnd`); parked rails get a
  trailing bucket (no clamp into a battle column), hidden GPU texels and no CPU point. A node without a registry body
  (an asteroid) is an end point index past the registry, its position from `SOLLaneNodeTable::PositionsAt` each frame
  and its speed bound from the node table. Row 6 is written each frame at the rail texels' GPU epoch; the tier rule
  (`LaneShipAiTier`, P1 / P2) runs in the tier processor's Select pass from each column's window in rail time; the lane
  table carries each column's slot and `bOpenNow` (merge readout and AUTO search skip closed lanes). The per-move CPU
  cost is asserted by `SOLTest.NpcRail.BlockMoveCost` (0.024 ms measured, gate 0.5 ms). **That figure is a partial
  measurement:** it covers the spawn, rail and GPU texels of 100 ships plus the dirty range and its texel rects only,
  not the release, the take, the Mass re-bucket or the render-thread upload; it is re-measured with the full move in
  53c2b. **Not built yet:** the gated dirty-range rail upload in `ASOLNpcVisuals` (the pure
  `ShipBlockDirtyRanges` / `RailTexelRegions` exist); row 6 is written but read by nothing until 53c2b, so it is
  unverified on screen. Suite 2128 / 2128. Smokes at 1920x1080: `-SOLSmokeNpcLanes` 35 / 35, `-SOLSmokeLaneMerge`
  15 / 15, `-SOLSmokeNpcTraffic` 19 / 19 after two stale smoke assumptions were fixed (production code unchanged):
  V5's bit-for-bit row check ran over every group row and so compared the new row 6 (lane window) with zeros, now it
  covers rows 2-5 only; V2 took the steady mid-lane ship nearest the middle whatever its speed, and with the new share
  that was a 9.8 km/s ship, which the 4 s closest-time clamp started 39 km away, inside the 50 km Full radius (Far
  straight to Full, never Mid; ticket 50's passing run had a 28 km/s ship), now it takes only ships of at least 2 Full radii per
  4 s, 25 km/s (87 km/s taken, 1 promotion, 1 demotion, 479 Mid hull frames). Spawn: 50 slots, 42 extra columns, 0 parked. The
  tunnel's fixed 8-lane vertex budget is exceeded (15 lanes drawn, logged), a fixed-pool limitation until 53d.
  **Interim until 53c2b (accepted by the user):** no block moves and no exact re-share at a time jump, so every slot
  that owns blocks at the spawn is held (`SetSlotHeld`) and keeps its ships: a lane that opens after the spawn has no
  ships, a closed lane still shows its far-layer points (the shader ignores the window until 53c2b), and held slots
  can overflow the columns in a long warp run (reported by the usual overflow log). Docked ships (outside their
  slot's window) are never promoted and are demoted at once, so near the camera (inside the Mid radius, where the far
  layer hides lane ships) a docked ship is not drawn while far away the far layer still draws it. Lanes that end on an
  asteroid node have ships and points but no tunnel and no merge view yet (both key on registry bodies; 53d / 53e).
- 2026-11-25, 53c2a review fixes: the live wiring is routed through pure functions and pinned by
  `SOLTest.NpcRail.LaneWindowWiring` (`LaneRailFromSpawn`: a spawn's rail group is its block's slot column;
  `RailTimeS` / `SetSlotWindowRailS`: a slot's J2000 window at its column in rail time, the epoch taken off once, tested
  at an 8e8 s epoch so a dropped or doubled subtraction fails; `LaneRailInSpace`: the Select pass's docked tier rule).
  `CountBlockSpawns` runs in parallel over runs of 256 blocks (deterministic, `SOLTest.NpcRail.BlockSpawnCountsParallel`);
  `LaneEndpointPositions` evaluates only the 10 asteroid nodes (`SOLLaneNodeTable::AsteroidPositionsAt`) and no longer
  copies the registry: `BuildFrame` takes the node positions as a second view (`SOLTest.NpcRail.NodeEndpoints`); trace
  scopes on `UpdateLaneWindows` and `LaneEndpointPositions`. The in-game lane readout and AUTO texts now say "open
  lane" (the Help page screenshots were not regenerated). Suite 2131 / 2131.
- 2026-11-25: 53c2b done, pending commit. **Steps 1-2:** `SOLNTR_LaneShipDrawn` (`Shaders/Private/SOLNpcTraffic.ush`)
  and its CPU mirror `SOLNpcTrafficMirror::LaneShipWindowDrawn` hide a lane ship outside its slot window (group texture
  row 6) and a parked ship (rail `.w` < 0); `M_SOLNpcPoints` rebuilt headless by `create_npc_assets.py`; the CPU point
  layer draws a Point-tier lane ship only when the mirror says drawn. `SOLTest.NpcRail.GpuLaneWindowMirror` measures
  the float band: Earth-Moon 0.064 s, Mars-Jupiter 32 s, a 128 AU lane 512 s; 8 of 8 ships touch the band, every
  in-band disagreement lies exactly on a window bound, 0 mismatches outside it. The first real-RHI game run compiled
  the new call without a shader error. **Step 3:** per-epoch block moves (`USOLNpcSubsystem::StepLaneBlocks` /
  `ApplyBlockMoves`, the new `Npc/SOLNpcBlockMoves.h/.cpp`, `SOLLaneNetwork::StepShipBlocks`, the dirty-range rail
  upload `ASOLNpcVisuals::UploadMovedRails`, an exact re-share and full upload at a time jump), pinned by
  `SOLTest.LaneBlockMove.Step / UploadRegions / Rebucket / LaneTable`. A slot is held while its blocks own ships.
  Headless: 0.384-0.398 ms per block (gate 0.5 ms); a large lane opening (1,246 blocks) 310-338 ms in one frame; a
  time-jump re-share (4,000 blocks) about 1.1 s. **Step 4, smokes** (run on the binary before the `sol.SimDate`
  command below was added, which none of them uses; 1M NPCs, LaneMerge its default 250k): `-SOLSmokeNpcLanes`
  35 / 35, `-SOLSmokeLaneMerge` 15 / 15, `-SOLSmokeNpcTraffic` 19 / 19 at 1280x720, 1920x1080 and 2560x1440 (4K
  proxy, screenshots checked at 2560x1440, not clamped); no smoke fix was needed. **Caveat:** NpcLanes and NpcTraffic
  start at the real UTC date, so on another day their lane can be closed or an epoch can flip mid-run (with
  `-SOLSimDate=2026-01-01` the Earth-Mars lane is Sun-closed and NpcTraffic fails its three V2 fly-by checks with 0
  hull frames, as the window rule requires); pinning their dates is left to the user. **Warp run** (1920x1080, 1M,
  10 d/s for an estimated 135 s, about 1,350 sim days, then a clock set): 66 per-epoch moves opening 65 slots, 43-394
  blocks (mean 174), 8,867 blocks
  released and 8,773 taken, shortfall up to 195 blocks, 0 demoted; **in game 0.581-0.878 ms per block (mean 0.684),
  every move over the 0.5 ms gate** (headless 0.38-0.40), 34-274 ms per move (mean 118 ms), about 99% of it the Mass
  re-bucket (`SwapConstSharedFragmentForEntity` per entity); rails up to 2.4 ms; the partial rail upload 1.06 ms mean,
  2.86 ms max (64 uploads). No column overflow warning, no ensure or crash. **Time jump** (the new developer console
  command `sol.SimDate <date>`, `Universe/SOLSimClockSubsystem.cpp`, driven by `-SOLSmokeConsole=`): `full=1`, 3,992
  blocks, 50 slots opened, a **1.76 s** game-thread hitch at 1x and **2.07 s** after the warp run (rails 5-7 ms, the
  rest Mass). Island links are not logged per epoch at run time, so their appearance and churn in game were not
  observed (the contract cases `IslandLinks` / `PartnerHysteresis` cover them); 65 openings in an estimated 1,350
  days is about one per 20 days. Screenshots (smoke views, the default view, the view after the jump) show traffic in
  open lanes and no row 6 artifacts (no midpoint discs, no flicker clusters); the Sun-closed Earth-Mars lane on
  2026-01-01 (NpcTraffic V1a, inside the lane) shows its tunnel but no lane traffic, so the GPU window hides docked
  ships (the material holds the new call). A ship closing mid-leg was not framed, so "no ship vanishes mid-leg" rests
  on the mirror test and the window rule. After a clock set mid-play the view fills with white space-dust streaks;
  they appear with 0 NPCs too, so they come from setting the clock during play (the ship's motion against the new
  frame), not from the re-share. Temporary screenshots
  and logs deleted; `GameUserSettings.ini` MD5 unchanged. **Interim lifted:** lanes now fill when they open and drain
  when they close (released blocks go back to the pool), closed lanes hide their ships on the GPU and CPU layers, and
  parked ships are hidden. **Known, reported, not fixed:** the per-block gate is missed in game, and the opening and
  time-jump hitches (`TECH_DEBT.md`; the lever is a batched Mass re-bucket); at 10 d/s that is roughly a 120 ms
  hitch every couple of seconds. Not checked: the tunnel component's lane caches for Gone / released columns.
  `sol.SimDate` is a thin developer hook over the tested date parser and `SetJulianDate`, with no test of its own.
  Suite 2136 / 2136.
- 2026-11-25: 53d done, pending commit and adversarial review. **Steps 1-2:** `USOLNpcTunnelComponent` steps each
  lane's class (`LaneTunnelMaxRadiusPx` with the live focal, `StepTunnelClass` with `LaneTunnelQualityScale`) before
  any frame is built, so an Off lane builds no frame, texels or instance; a Reduced mesh (the slot mesh's ring and
  end-ring prefix, 7,220 vertices) beside the Full mesh (15,380, 160 ring slots per side); `SelectTunnelPool` with the
  pool of 24 shared by both classes, instances rewritten per class only when that class's lane set changes, the
  overflow logged once per change and counted (stat group `SOLNpcTunnel`: `SOL_NpcTunnelFull`, `_Reduced`,
  `_PoolOverflow`, `_Vertices`). `NPC_TUNNEL_VERTEX_BUDGET` / `_LANES` removed, tests re-pinned. **Step 3:** Q20 bands
  in px (`LaneLineFade` / `TunnelFade` with a focal, `NPC_LANE_FADE_NEAR/FAR_PX`, `NPC_TUNNEL_LABEL_START/FULL_PX`);
  the point and lane materials keep their radii inputs, fed from the fade focal (`LaneFadeRadiiAtFocal`), and the
  tunnel shader's fades read the focal over the quality scale (`Layout4.z`, user decision: one factor for every
  threshold), `NpcTunnelMirrorTest` QualityScale case added; no `.uasset` rebuilt. **Step 4:** the 3 s real-time open
  fade (`StepLaneOpenFade`, row 3 `.z` = open fade x draw flag, per column from `FSOLNpcLaneSlotWindow::bOpenNow`, a
  time jump's re-share snaps it, a column taken by a new lane starts at 0); a closing network column is held, and its
  end points kept, until its fade is 0 (`USOLNpcSubsystem::SetTunnelFadingColumns`, SDD 4.5); the point shader's
  `SOLNTR_LaneShipDrawn` now reads the active flag (window test renamed `SOLNTR_LaneShipWindowDrawn`, mirror
  `LaneShipPointDrawn`, also used by the CPU point layer); without a network the base lanes are active. Info text
  and `GAME_MECHANICS.md` updated. Suite 2145 / 2145 (one unrelated HTTP-timeout warning). Smokes:
  `-SOLSmokeNpcTunnel` 48 / 48 at 1920x1080 after its oracles were updated (T1 / T5 feed at the fade focal, Full-only
  label strength; F3000 at 0.32 px is now Off and checked as such); `-SOLSmokeNpcLanes` 35 / 35,
  `-SOLSmokeLaneMerge` 15 / 15, `-SOLSmokeNpcTraffic` 19 / 19 at 1280x720, 1920x1080 and 2560x1440; no pool overflow
  logged. **Deferred:** step 5's new tunnel smoke views (pool-full, class switch, closing lane) and the Earth hub /
  belt PIE screenshots (unreal-mcp was down; the smoke screenshots T1, F300, F3000 and the Earth hub T5 were viewed
  and deleted); the tunnel CPU cost was not re-profiled.
- 2026-11-25: 53d review fixes, pending commit (user decisions: Full switch invisible, fade holds pre-emptible,
  interim calls accepted). Full on at 10/12 px, off below 0.75 px (constants, SDD 7.1, contract, tests, Info text and
  `GAME_MECHANICS.md`); `mHasPendingRelease` only for a block owner out of the set (a fade-only hold re-checks once at
  its fade's end, or once when the fade reports go stale); `ESOLLaneSlotHold` (Blocks / Fade) and fade pre-emption in
  `AssignLaneSlots`; pool hysteresis `NPC_TUNNEL_POOL_KEEP_RATIO` 1.1; wall label alpha x the open fade; pure
  `StepColumnOpenFades`, `LaneSlotHold`, `IsSlotReleasePending`, `SplitTunnelPoolByClass`, `SyncTunnelClassLanes`
  with tests (written by the implementer after the code, said so in `LaneTunnelClassTest.cpp`); the `QualityScale`
  mirror row now evaluates every dot and chevron with the fade focal (with live-focal and twice-radius controls); a
  component test checks the real Reduced mesh (7,220); dead `CollectTunnelLanes`, `ShouldDrawTunnels`, one-argument
  `PointLaneParams` / `LaneMaterialParams` deleted (tests re-pinned); vanish-table `static_assert`; 120-column wraps.
  Suite 2152 / 2152. Smokes: NpcTunnel 48 / 48 (1920x1080), NpcLanes 35 / 35, LaneMerge 15 / 15, NpcTraffic 19 / 19 at
  1280x720, 1920x1080, 2560x1440. **Tunnel CPU** (`-SOLSmokeNpcTunnelProfile`, 1080p, tunnel + label scopes, on minus
  off): T2-all 0.022 ms, T5-all 0.019 ms, T2-own 0.019 ms, within the 0.025 ms gate (tunnel scope alone 0.019-0.023
  on, 0.007-0.008 off). **Warp run** (1M, 10 d/s, about 140 s): 1,444 block steps in 15,392 frames (about one per
  epoch, no per-frame re-runs), 65 block moves, no column overflow, no pool overflow; per-block move 0.57-0.90 ms
  (the known Mass re-bucket debt). Peak column use and pre-emption counts were not instrumented, so the fade
  pre-emption is covered by unit tests only. Item 10 (a lane opened with 0 blocks) not changed: my reading is that in
  the set it keeps its slot by membership and after its close by its fade hold; to confirm with the reviewer.
