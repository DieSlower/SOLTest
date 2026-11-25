<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 53: Lane network

**Issue:** #53 (To do in [`STATUS.md`](STATUS.md), row 3b5d; no GitHub issue checked or created by this draft). SDD:
[`SDDs/53-lane-network.md`](../SDDs/53-lane-network.md). Test-first contract:
[`53-lane-network-contract.md`](53-lane-network-contract.md).
Status: **DRAFT, amended 2026-11-24 with the user's answers to Q1-Q30** (SDD 53 section 16). **Gate: nothing is built
until the user has settled the open items O1-O10 of SDD 53 section 17** (or accepted their proposals) and the SDD and
contract are amended with those answers. Grill-me decisions D1-D8 and Q1-Q30 are the user's.
**Persistence: none planned.** `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay. A part that finds a saved
setting is needed stops and reports; a schema change needs the user's approval.

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

1. Amend the SDD and the contract with the user's answers to O1-O10. Reword the STATUS.md row 3b5d to "pixel on/off,
   Sun clearance" (Q24) in the 53a commit.
2. Test author (`opus`, high, **contract only, no implementation shown**): write
   `Source/SOLTest/Tests/LaneNetworkMathTest.cpp` (`SOLTest.LaneNetwork.*`) from the contract, cases 1-14.
3. Add the header `Traffic/SOLLaneNetworkMath.h` with the signatures and stub bodies so the tests compile; run
   `Tools\RunTests.bat -Build -Filter SOLTest.LaneNetwork`; confirm each case fails for the right reason.

### 53b: pure math implementation

1. Implement `Traffic/SOLLaneNetworkMath.cpp` and the constants (`SOLConstants.h`, with `static_assert`s for the
   contract's invariants). No allocation per call beyond caller-reserved arrays; Dijkstra on a fixed array.
2. Green run of `SOLTest.LaneNetwork`, then the full suite.

### 53c: nodes, daily lane set, Sun closing, traffic weights

1. Node table (24 nodes, SDD 53 4.2): planets, Moon and dwarf planets from the registry; the 10 asteroids from
   `SOLAsteroidBelt::RealAsteroids()` (new `Npc -> MinorBodies` edge, positions with the existing `SOLKepler`
   propagation); tiers and region radii (proposed values as confirmed by O5, O6); aphelia for `L0` (asteroids from
   their elements, not the registry walk).
2. `DefaultLayout`: the 8 base rows stay at slots 0-7 with the population weights (Q4, Q23).
3. `USOLLaneTrafficSubsystem`: the per-epoch lane set (`NextClosestPairs`), slots (`AssignLaneSlots`, report on
   column overflow, Q28), the look-ahead schedule (`NPC_LANE_NETWORK_HORIZON_EPOCHS` ring buffer, one epoch per epoch
   change, full rebuild on a time jump), the Sun state (`EpochClearanceBoundM`, `SunStateFromLookBack`), the windows
   (`LaneOpenWindow`), edge times and the route query.
4. Spawn and traffic: ship blocks and their allocation (O1 as decided), profiles frozen per episode, partial rail
   upload on a block move; the early stop (`LegFitsWindow`, `LaneShipLegDrawn`) in the group texel window floats,
   `SOLNpcTraffic.ush` and the CPU point layer changed together; docked ships not drawn; closed-lane ships promoted as
   usual (Q26), docking at demotion (O9); merge readout hidden for closed lanes (Q30), lane search skips closed lanes.
5. Re-validate `NpcScaleMathTest`, `NpcTrafficMathTest`, `NpcRailTest`, `NpcLaneMathTest` (stale 8-lane assumptions
   deleted or re-pinned) and `NpcTunnelFarFieldTest` `FloatPathAtMaxLength` at 150 AU (SDD 53 4.7; stop and report if
   it fails, O8). Smokes `-SOLSmokeNpcTraffic`, `-SOLSmokeNpcLanes`, `-SOLSmokeLaneMerge` at 1280x720, 1920x1080 and
   4K, plus a warp run across several epoch changes (lanes appear and drain, no ship vanishes mid-leg);
   `GameUserSettings.ini` MD5 unchanged.

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
   finish-if-safe check, Sun detour waypoint): extend the contract with these cases (separate author) before the code.
2. `FSOLShipStateFragment`: transient plan (hops with lane identities, current hop, serial), reset on every order write
   (`Ship/SOLAutopilotOrder`), never a `UPROPERTY`, never saved.
3. `USOLShipFlightProcessor`: route at order time (Q11) with the time-window Dijkstra (O7), latch each hop, transfer
   through the next hop's merge point, D20 search off while a plan exists (Q15), re-plan on an epoch change (Q14),
   finish-if-safe or leave on a closing held lane, the Sun detour (scope per O4), War power unchanged.
4. HUD: "VIA <node>" line and the lane-closed notice (`SOLMenuText`), ETA of the whole plan with the first and last
   mile (Q13). System map: the plan polyline in a new `ESOLRouteDrawStyle`, through the moving nodes each frame (Q16).
5. `SCOPE_CYCLE_COUNTER` around the AUTO lane rules (closes the SDD 50 profiling debt).
6. New smoke `-SOLSmokeLaneRoute` (two-hop trip at warp, a lane closing mid-plan and the re-plan, a Sun-closing held
   lane left with a detour, VIA line, map route); `-SOLSmokeAutoLanes`, `-SOLSmokeRoute`, `-SOLSmokeRouteOverlay`,
   `-SOLSmokeChase` re-run; `AutopilotRegression.NoLaneBitIdentical` green.

### 53f: profile, docs, gallery

1. Profile per SDD 53 section 13 (interleaved A/B worktrees against the pre-#53 commit): tunnel draw with the pool
   full, CPU on vs off, the new CPU gates (epoch step at 30 d/s, look-ahead rebuild, route query, class pass, block
   move), points draw recorded, flame CPU gate re-measured and reported (Q27, no fix), 1M ramp; 2560x1440 recorded (VR
   note: record only). Misses reported to the user, not relaxed by the agent.
2. Docs: `GAME_MECHANICS.md` (nodes, the daily lane set, population weights, Sun closing and finishing trips, the Sun
   danger zone and detour, routing, transfer, ETA, map route) and the in-game Info text in the same change;
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
