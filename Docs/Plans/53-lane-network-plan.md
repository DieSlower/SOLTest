<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Plan 53: Lane network

**Issue:** #53 (To do in [`STATUS.md`](STATUS.md), row 3b5d; no GitHub issue checked or created by this draft). SDD:
[`SDDs/53-lane-network.md`](../SDDs/53-lane-network.md). Test-first contract:
[`53-lane-network-contract.md`](53-lane-network-contract.md).
Status: **DRAFT (2026-11-22). Gate: nothing is built until the user has answered SDD 53 section 16 (Q1-Q30)** and the
SDD and contract are amended with the answers (blocked contract values unblocked or changed). Grill-me decisions D1-D8
are the user's.
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

1. Amend the SDD and the contract with the user's answers (Q1-Q30); unblock the blocked values. Reword the STATUS.md
   row (Q24).
2. Test author (`opus`, high, **contract only, no implementation shown**): write
   `Source/SOLTest/Tests/LaneNetworkMathTest.cpp` (`SOLTest.LaneNetwork.*`) from the contract, cases 1-9.
3. Add the header `Traffic/SOLLaneNetworkMath.h` with the signatures and stub bodies so the tests compile; run
   `Tools\RunTests.bat -Build -Filter SOLTest.LaneNetwork`; confirm each case fails for the right reason.

### 53b: pure math implementation

1. Implement `Traffic/SOLLaneNetworkMath.cpp` and the constants (`SOLConstants.h`, with `static_assert`s for the
   contract's invariants). No allocation per call beyond caller-reserved arrays; Dijkstra on a fixed array.
2. Green run of `SOLTest.LaneNetwork`, then the full suite.

### 53c: network data, Sun clearance, traffic weights

1. `DefaultLayout`: the 6 new rows appended at indices 8-13 (Q4, Q23) built by `NextClosestPairs` or written out
   (whichever the review prefers; a test pins they agree).
2. Spawn: node table, the lane set, nominal lengths (existing walk), the group table at 14 lanes; check the 64-column
   headroom (Q28).
3. `USOLLaneTrafficSubsystem`: node table, graph, edge times (`LaneEdgeTimeS`), the open state per lane-network epoch
   (`LaneNetworkEpochIndex`, sampled ephemerides, `EpochClearanceBoundM`, `StepSunState`), the route query.
4. Group texel: the per-lane open fade (Q8, Q9) and active flag; `SOLNpcTraffic.ush` and the CPU point layer read it;
   closed lanes' ships hidden and not promoted (Q26); merge readout and lane search skip closed lanes.
5. Re-validate `NpcScaleMathTest`, `NpcTrafficMathTest`, `NpcRailTest`, `NpcLaneMathTest` (stale 8-lane assumptions
   deleted or re-pinned). Smokes `-SOLSmokeNpcTraffic`, `-SOLSmokeNpcLanes`, `-SOLSmokeLaneMerge` at 1280x720,
   1920x1080 and 4K; `GameUserSettings.ini` MD5 unchanged.

### 53d: tunnel activation and pool

1. `USOLNpcTunnelComponent`: per-lane `LaneTunnelMaxRadiusPx` and `StepTunnelActive`; inactive lanes build no frame, no
   texels, no instance; `SelectTunnelPool` with `NPC_TUNNEL_INSTANCE_POOL`; overflow logged once per change and shown
   by a stat counter (reported as a limitation). `NPC_TUNNEL_VERTEX_BUDGET` / `_LANES` replaced (Q18).
2. If Q19 is approved: 160 ring slots (the knee debt), the `static_assert`s and `NpcTunnelFarFieldTest` re-pinned, the
   TECH_DEBT item removed.
3. Legacy controls per Q20 (restated in reference px of the lane radius, or recorded in `TECH_DEBT.md`); `.ush` and
   CPU mirror changed together, `NpcTunnelMirrorTest` extended.
4. Point vertex shader early-out for inactive and closed lanes (Q21, Q22).
5. `-SOLSmokeNpcTunnel` gains a pool-full forced view and a closed-lane view; PIE screenshots of the Earth hub.

### 53e: AUTO routing

1. Tests first for the plan logic that is pure (hop sequencing, per-hop `Left`, re-plan trigger, ETA sum): extend the
   contract with these cases (separate author) before the code.
2. `FSOLShipStateFragment`: transient plan (hops, current hop, serial), reset on every order write
   (`Ship/SOLAutopilotOrder`), never a `UPROPERTY`, never saved.
3. `USOLShipFlightProcessor`: route at order time (Q11), latch each hop, transfer through the next hop's merge point,
   D20 search off while a plan exists (Q15), re-plan on a lane-network epoch change (Q14), War power unchanged.
4. HUD: "VIA <node>" line and the lane-closed notice (`SOLMenuText`), ETA of the whole plan (Q16).
5. `SCOPE_CYCLE_COUNTER` around the AUTO lane rules (closes the SDD 50 profiling debt).
6. New smoke `-SOLSmokeLaneRoute` (two-hop trip at warp, closed-lane re-route, VIA line); `-SOLSmokeAutoLanes`,
   `-SOLSmokeRoute`, `-SOLSmokeChase` re-run; `AutopilotRegression.NoLaneBitIdentical` green.

### 53f: profile, docs, gallery

1. Profile per SDD 53 section 13 (interleaved A/B worktrees against the pre-#53 commit): tunnel draw, CPU on vs off,
   the new CPU gates, points draw recorded, flame CPU gate re-measured (Q27), 1M ramp; 2560x1440 recorded (VR note:
   record only). Misses reported to the user, not relaxed by the agent.
2. Docs: `GAME_MECHANICS.md` (the 14 lanes, Sun closures, routing, transfer, ETA) and the in-game Info text in the
   same change; `ARCHITECTURE.md` (new file, subsystem role); `TECH_DEBT.md` (fixed items removed, new ones added);
   `STATUS.md`.
3. README gallery: 2-3 new 1920x1080 shots (`Docs/Images/53-*.png`: the Earth hub, a closed lane fading near the Sun,
   a VIA trip); temporary shots deleted.
4. Whole-ticket adversarial review, fixes, full suite, commit and push.

## Progress

- 2026-11-22: SDD, plan and contract drafted from D1-D8. Waiting for the user's answers to Q1-Q30.
