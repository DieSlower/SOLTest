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

### 53c2: ship blocks, spawn, traffic shader, CPU point layer

1. Spawn and traffic: ship blocks (O1: 4,000 blocks of 100, moved only through the free pool), profiles frozen per
   episode, partial rail upload on a block move; the early stop (`LegFitsWindow`, `LaneShipLegDrawn`) in the group
   texel window floats, `SOLNpcTraffic.ush` and the CPU point layer changed together; docked ships not drawn;
   closed-lane ships promoted as usual (Q26), docking at demotion with no fade (O9); merge readout hidden for closed
   lanes (Q30), lane search skips closed lanes.
2. **Group columns:** when schedule slots 8 and up get ships, use `FSOLNpcGroupTable::LaneSlotColumn(slot)` (and
   `LaneSlotCapacity()`), not the slot number: slots from `LaneCount` on map to the columns after the clusters and
   battles, so no slot shares a cluster or battle column; `INDEX_NONE` past the capacity.
3. Re-validate `NpcLaneMathTest` and the traffic tests that still assume 8 lanes (deleted or re-pinned). Smokes
   `-SOLSmokeNpcTraffic`, `-SOLSmokeNpcLanes`, `-SOLSmokeLaneMerge` at 1280x720, 1920x1080 and 4K, plus a warp run
   across several epoch changes (lanes appear and drain, no ship vanishes mid-leg, island links appear);
   `GameUserSettings.ini` MD5 unchanged.
4. The final-rule re-measure was done in 53c1 (SDD 53 4.3, 4.4, 7.3). A figure that breaks a bound (columns over 64,
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
4. Tunnel open fade (3 s real, Q9) on close and reopen; point vertex shader early-out for inactive and docked ships
   (Q21, Q22).
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
