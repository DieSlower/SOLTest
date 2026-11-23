<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 53: Lane network

**Issue:** #53 (listed as To do in [`Plans/STATUS.md`](../Plans/STATUS.md), row 3b5d; no GitHub issue checked or
created for this draft). Plan: [`Plans/53-lane-network-plan.md`](../Plans/53-lane-network-plan.md). Test-first
contract: [`Plans/53-lane-network-contract.md`](../Plans/53-lane-network-contract.md). Builds on SDD 11 (NPC rails and
tiers), SDD 38 (lane lines), SDD 49 (guidance tunnel), SDD 50 (lane traffic, AUTO lane rules) and SDD 52 (tunnel far
field, D15: the vertex budget and active-tunnel cap were left interim for this ticket).
**Status: DRAFT (2026-11-22).** Grill-me decisions D1-D8 below come from the user. Everything the user has not decided
is listed in section 16 (Open questions) with a recommended answer. Nothing is built.
**Persistence: no schema change is needed by this design** (section 11). `SOL::Settings::SAVE_VERSION` stays 13 and
`KEY_BINDING_LAYOUT_VERSION` stays 10. If an answer to section 16 asks for a saved setting, that is a schema change and
needs the user's approval first.

**Pixel rule (project docs, user rule 2026-11-21), repeated for every part:** LOD, fade, visibility and activation
thresholds are on-screen pixels (1080p reference pixels), never distance or object counts. A fixed pool that cannot
grow is the only allowed cap, and hitting it is reported to the user as a limitation, never used as the LOD rule.

## 1. Goal

Grow the 8 hand-listed lanes into a lane network:

- more lanes, so every planet has a second way in and out;
- a node model that later carries moons, space stations, space cities and large asteroids (built for planets only);
- lanes switched on and off for drawing and traffic by their size on screen, not by distance or count;
- lanes switched off while their straight line passes too close to the Sun;
- "fastest A to Z" routing over the lane graph, used by AUTO;
- the interim tunnel vertex budget (100,000 for 8 lanes, 12,500 per lane, SDD 52 D15) replaced by a pixel-driven
  per-lane budget with one hard pool.

## 2. Current state (code survey, HEAD `9943d8e`)

| Piece | Where | Facts that matter here |
|---|---|---|
| Lane list | `Npc/SOLNpcScaleMath.cpp` `DefaultLayout()` | **8 lanes**, hand-written rows (body A, body B, lane radius, weight): Mercury-Venus (2e6 m, 1.0), Venus-Earth (2e6, 1.5), **Earth-Moon** (2e5, 1.0), Earth-Mars (3e6, 2.0), Mars-Jupiter (5e6, 1.5), Jupiter-Saturn (8e6, 1.0), Saturn-Uranus (1e7, 0.5), Uranus-Neptune (1e7, 0.5). That is the 7 neighbouring-orbit planet pairs plus one planet-moon lane, **not every planet pair**. Clusters: 10 rows (9 bodies plus the belt) |
| Lane ships | `SOLNpcScale::SplitCounts`, `MakeSpawn`, `PickWeighted` | Lane ships = `LaneFraction` (0.4) of the population (400,000 of 1M); each lane ship picks its lane by hash over the lane weights (not contiguous per lane) |
| Group table | `Npc/SOLNpcRail.h` `FSOLNpcGroupTable` | Lanes first, then clusters, then battles; all share the **64 group columns** (`NPC_GROUP_TEXTURE_WIDTH`). Today 8 + 10 + up to 16 battles = 34 |
| Lane profile | `SOLNpcRail::BuildLaneProfiles`, `SOLLaneTraffic::MakeLaneProfile` | Frozen at spawn: wall radius `Rw` from the in-wall count (SDD 50 D13), nominal length `L0` = the sum of the two aphelia (D16, `NpcLaneNominalLengthM` walks parent chains, so a moon lane works) |
| Lane geometry per frame | `SOLNpcRail::BuildFrame`, `SOLLaneTraffic::MakeLaneFrame` | The straight segment between the two **current** body centres is rebuilt **every frame**; the nominal leg is mapped onto the current leg (SDD 50 4.5), so a ship's **leg time does not depend on the current geometry** |
| Epochs | `USOLNpcSubsystem::Spawn`, `ASOLNpcVisuals::ReepochRails` | Two only: the spawn epoch (profiles frozen) and the GPU rail re-base (`NPC_GPU_RAIL_EPOCH_S` 1e4 s of rail time, at most every `NPC_GPU_RAIL_EPOCH_MIN_INTERVAL_S` 2 s real). **Neither re-derives the lane set**; nothing checks the Sun |
| Lane table for AUTO / HUD | `Traffic/SOLLaneTrafficSubsystem` | Per lane: end-body indices and the frozen profile; no graph |
| AUTO lanes | `SOLLaneTraffic::FindLaneLeg`, `LaneAutoDecide`, `Ship/SOLShipFlightProcessor.cpp` | The **first lane (lowest index)** whose halo holds the **straight path ship -> target** for 20 wall radii (D20) is latched; `Left` is terminal for the order. **No multi-lane trip exists.** Lane views on the stack, up to 64 (`FLIGHT_MAX_LANES`) |
| Edge time | `SOLLaneTraffic::LaneLegTimeS`, `MakeLegProfile` | Closed form, flip window included; the mean-effort leg on the keep-right track (rho 0.25) |
| Tunnel | `Npc/SOLNpcTunnelComponent.cpp`, `SOLNpcTunnelMath` | One slot mesh of 12,500 vertices, one instance per drawable lane, **every lane built and drawn every frame at every distance** (SDD 52 D1). `NPC_TUNNEL_VERTEX_BUDGET` 100,000 / `NPC_TUNNEL_VERTEX_BUDGET_LANES` 8 only produce a **log warning**: there is **no active-tunnel cap in code** |
| Legacy distance / count controls on lanes | `SOLConstants.h` | `NPC_LANE_FADE_NEAR/FAR_RADII` (100 / 400 lane radii, the lane-ship point fade), `NPC_TUNNEL_FADE_START/FULL_RADII` (the 100 -> 50 label strength band), `NPC_TUNNEL_LABEL_MAX_COUNT` 4 (a count cap). Distances in units of the lane's own radius are angular sizes, so each is a pixel threshold at a fixed focal length (100 lane radii = 9.6 reference px of lane radius at 960 px focal) |
| Persistence | `Menu/SOLSettingsSave.h` | Settings only (Lanes overlay row). No AUTO order, route, NPC or lane data is saved |

Tech debt handed to or touching #53 (grep of `Docs/TECH_DEBT.md`, not re-reported as new): the ring window ends at
the 1 px knee instead of the 0.5 px gone point (160 slots would fix it, +2,880 vertices per lane); the window assumes a
16:9 focal (21:9 needs more slots); the tunnel CPU cost has no headroom (0.016-0.022 ms on vs off, about 1 us per drawn
lane); the thruster flame CPU gate miss "revisit with #53"; the D21 points cost inside a tunnel; the re-epoch hitch
(9.85 ms at 1M under warp); the AUTO lane rules have no profiling scope; `UpdateCpuShips` re-derives float lane
positions per CPU ship.

## 3. Decisions (user, grill-me)

- **D1: Endpoints v1.** "Every planet pair as today, **plus each planet to its next-closest planet where no lane
  exists**, so every planet is connected; later nodes: moons, space stations, space cities, large asteroids use the
  **same node type** (design the node abstraction now, build planets only)." *Draft's reading, to be confirmed (Q1,
  Q2):* "as today" is the 8 lanes of section 2 (7 neighbouring planet pairs plus Earth-Moon, not every pair, and the
  chain already connects every planet), and "next-closest where no lane exists" is the nearest planet not already
  linked.
- **D2: Activation by pixels.** A lane is drawn and trafficked by the on-screen pixel size of its tunnel and traffic.
  A hard pool is the only cap and is reported if hit. No distance or count LOD.
- **D3: Routing.** "Fastest A to Z" = shortest **travel time** over the lane graph: Dijkstra over closed-form segment
  travel times (flip-and-burn included) plus a per-node transfer cost, possibly via intermediate planets; used by AUTO
  lane use.
- **D4: Transfers only at nodes.** A ship arrives at a node on one lane, then leaves on another.
- **D5: Long lanes and the Sun.** No special length rule: pixels decide drawing and traffic, traffic stays simulated
  (closed form) when far, and invisible lanes stay in the routing graph. **But a straight lane must not pass the Sun
  closer than a safe distance** (20 solar radii, about 0.093 AU, a tunable multiple of the Sun's radius): such a lane
  **turns off** (tunnel fades, traffic drains) until it is safe again, and routing goes another way. **No curved
  lanes** (the closed-form straight rail and the identical CPU / GPU mirror stay). **Design hook only:** a later
  ticket may re-route lanes through space stations or space cities placed at strategic waypoints to cover planet
  movement; recorded in section 4.8, not built.
- **D6: Planets move.** Lanes are re-derived from planet positions each epoch and their Sun clearance re-checked then;
  an unsafe lane switches off at its next epoch boundary. (What "epoch" means is open, Q5: today no epoch re-derives
  lanes, section 2.)
- **D7: Traffic density by lane importance** (planet size or pair traffic weight, which one is open, Q4), with the
  **same total NPC budget** as today: no new global ship count.
- **D8: Tunnel vertex budget by pixels.** Each lane's budget follows the pixel size of its tunnel; a lane too small on
  screen gets no tunnel geometry; one hard pool is the only cap (reported if hit). The interim 100,000 / 12,500 per
  lane figures are revisited (section 8).

## 4. Lane graph and node model

### 4.1 Node

New pure types in `Traffic/SOLLaneNetworkMath.h` (namespace `SOLLaneNetwork`), fed plain arrays by the NPC subsystem,
so `Traffic/` keeps no `Universe` edge (SDD 50 4.10):

```cpp
enum class ESOLLaneNodeKind : uint8 { Planet, Moon, Station, City, Asteroid };   // v1 builds Planet and Moon only

struct FSOLLaneNode
{
    ESOLLaneNodeKind Kind = ESOLLaneNodeKind::Planet;
    int32 BodyIndex = INDEX_NONE;    // Registry body (INDEX_NONE for a future non-body node: a station)
    int32 ParentNode = INDEX_NONE;   // Moon -> its planet node (future: station -> host)
    double ClearanceRadiusM = 0.0;   // Body radius (end rings sit at it + 3 Rt); a station's hull radius later
    double SunDistanceKeyM = 0.0;    // Next-closest key: the semi-major axis about the Sun (Q1)
    double Importance = 0.0;         // Traffic importance (D7, Q4)
};
```

Node positions are not stored: each frame the caller passes the node positions (body nodes from the registry; a
future station node from its own source). Every lane-network function takes node indices, never registry indices, so a
station later needs a new position source and nothing else.

### 4.2 Lane set (D1)

- **Base lanes:** today's 8 rows, unchanged and **kept at indices 0-7** (Q23).
- **Next-closest lanes:** for each planet node, the nearest other planet node **not linked to it by a base lane**,
  by the key `|a_i - a_j|` (difference of semi-major axes, which is the closest-approach distance of two near-circular
  coplanar orbits; Q1). Duplicates merged; ties broken by the lower node index. Computed once at spawn from the
  registry's elements at the spawn epoch, so the lane set is fixed for the population (the rails and profiles are
  frozen at spawn, SDD 50 D10 / D13 / D16).
- **Why not the plain nearest:** the base chain already links every planet to its nearest neighbour, so a
  "nearest" rule adds nothing; the rule is read as "nearest planet not yet linked" (Q1 confirms). **Why not the
  time-mean distance:** averaged over time, Mercury is the closest planet to every planet, which would make Mercury
  a hub of every new lane.
- **Result with the registry's J2000 elements** (computed by a script from `SOLBodyRegistry.cpp`, not from memory):

  | Planet | Linked today | Nearest unlinked by `|da|` | New lane |
  |---|---|---|---|
  | Mercury | Venus | Earth (0.613 AU) | Mercury-Earth |
  | Venus | Mercury, Earth | Mars (0.800 AU) | Venus-Mars |
  | Earth | Venus, Mars (Moon is not a planet) | Mercury (0.613 AU) | (Mercury-Earth, merged) |
  | Mars | Earth, Jupiter | Venus (0.800 AU) | (Venus-Mars, merged) |
  | Jupiter | Mars, Saturn | Earth (4.203 AU) | Earth-Jupiter |
  | Saturn | Jupiter, Uranus | Mars (8.013 AU) | Mars-Saturn |
  | Uranus | Saturn, Neptune | Jupiter (13.986 AU) | Jupiter-Uranus |
  | Neptune | Uranus | Saturn (20.533 AU) | Saturn-Neptune |

  **6 new lanes, 14 in all** (indices 8-13 in the order of the table). The longest new nominal length is
  Saturn-Neptune, 40.4 AU, under Uranus-Neptune's 50.4 AU, so the SDD 52 float-path bounds at the maximum lane length
  still cover every lane (no re-validation needed beyond the existing `FloatPathAtMaxLength` case).
- **Group columns:** 14 lanes + 10 clusters + up to 16 battles = 40 of the 64 shared columns. The 64 columns are the
  real hard pool for future nodes' lanes (Q28).
- **Dwarf planets** (Pluto, Ceres, ... in the registry) are not planets here (Q3).

### 4.3 Lane radius and weight of the new lanes

Today's radius and weight are hand-written per row. The new rows need both (Q4). Recommended rows (pair traffic
weight, the lane radius of the outer endpoint's region as today):

| Lane | Radius (m) | Weight |
|---|---|---|
| Mercury-Earth | 2.0e6 | 0.75 |
| Venus-Mars | 2.0e6 | 0.75 |
| Earth-Jupiter | 5.0e6 | 1.0 |
| Mars-Saturn | 8.0e6 | 0.5 |
| Jupiter-Uranus | 1.0e7 | 0.5 |
| Saturn-Neptune | 1.0e7 | 0.25 |

### 4.4 Graph

Undirected multigraph: nodes = lane nodes, edges = lanes with `(nodeA, nodeB, open, timeS)`. Each lane can be flown
both ways (keep right, SDD 50 D1). A route is a sequence of hops `(lane, direction)`, direction +1 for A -> B.

## 5. Sun clearance rule

### 5.1 Math

Sun at the origin (the registry keeps it fixed there). For a lane between body centres `A` and `B`:

    t* = clamp(-(A . (B - A)) / |B - A|^2, 0, 1)
    d  = |A + t* (B - A)|                (|A| for A == B)
    unsafe  if  d < k_off R_sun,   k_off = NPC_LANE_SUN_CLEARANCE_RADII = 20  ->  1.3914e10 m = 0.0930 AU

The closest point is inside the segment only when the triangle Sun-A-B has acute angles at A and B, which needs the
two bodies on roughly opposite sides of the Sun; otherwise `d` is an endpoint's distance to the Sun (at least
Mercury's perihelion, 0.307 AU, always safe). The halo reaches at most `H Rw` = 2 x 3 x 1e7 = 6e7 m from the axis, under
0.5 % of the clearance, so the axis segment is the measure (Q7). Hysteresis: a closed lane reopens only above
`k_on R_sun` (Q6, recommended `k_on` = 22).

### 5.2 How often lanes close (circular, coplanar estimate)

Near opposition of the two bodies (angle `pi - theta` at the Sun small), `d ~ r1 r2 sin(pi - theta) / (r1 + r2)`, so
the lane is unsafe while `|pi - theta| < asin(k R_sun (r1 + r2) / (r1 r2))`:

| Lane | Half-angle | Closed per synodic period |
|---|---|---|
| Mercury-Venus | 21.7 deg | 17 d of 145 d (12 %) |
| Venus-Earth | 12.8 deg | 42 d of 584 d (7 %) |
| Earth-Mars | 8.9 deg | 38 d of 780 d (5 %) |
| Mars-Jupiter | 4.5 deg | 21 d of 816 d (2.5 %) |
| Jupiter-Saturn | 1.6 deg | 64 d of 19.9 y (0.9 %) |
| Saturn-Uranus | 0.8 deg | 77 d of 45 y (0.5 %) |
| Uranus-Neptune | 0.45 deg | 158 d of 171 y (0.3 %) |
| Mercury-Earth | 19.5 deg | 13 d of 116 d (11 %) |
| Venus-Mars | 10.9 deg | 20 d of 334 d (6 %) |
| Earth-Jupiter | 6.4 deg | 14 d of 399 d (3.5 %) |
| Mars-Saturn | 4.1 deg | 17 d of 734 d (2.3 %) |
| Jupiter-Uranus | 1.3 deg | 37 d of 13.8 y (0.7 %) |
| Saturn-Neptune | 0.7 deg | 54 d of 35.9 y (0.4 %) |

Inclination and eccentricity shift these (an out-of-plane lane can pass above the Sun and stay open); the tests use
the exact segment distance, not this table. Earth-Moon never comes near.

### 5.3 Epoch (D6, Q5)

Recommended (Q5): a **lane-network epoch** of fixed sim time `E_L` (`NPC_LANE_NETWORK_EPOCH_S`, provisional 1 day). The
state of epoch `k = floor(t / E_L)` is decided from the closed-form body positions sampled at `N` points across
`[k E_L, (k + 1) E_L]` with a conservative bound: the distance from a fixed point to a segment changes at most as fast
as its faster endpoint, so

    d_min_bound = min_i d(t_i) - v_max (dt / 2),   unsafe if d_min_bound < k_off R_sun

(`dt` = sample spacing, `v_max` = the larger endpoint speed; Mercury at 59 km/s and `dt` = 6 h gives a 6.4e8 m margin,
under 1 solar radius). The open state is then a **pure function of sim time** (deterministic, the same every session,
nothing saved, warp-proof: at 30 d/s many epochs pass per frame but only the current one is evaluated). The
alternative, the GPU rail re-base, depends on real time and warp and can skip a whole Earth-Mars closure at high warp
(an epoch can span about 60 sim days there). Cost: one evaluation per epoch change, `N` x 14 segment distances plus
the ephemeris samples, far under 0.01 ms.

### 5.4 Off and on (D5, Q8, Q9, Q10)

- **Routing:** a closed lane is not an edge.
- **Tunnel:** its draw fade (tunnel texture row 3 `.z`, today always 1, SDD 52 4.9) goes 1 -> 0 over a short
  real-time fade at the switch, 0 -> 1 when it reopens (Q9, recommended 3 s real, so warp does not make it pop).
- **Traffic, recommended (Q8 option a):** the lane ships fade with the tunnel and stay hidden while the lane is
  closed; their rails keep running (closed form, stateless, SDD 50 D10); they are not promoted to the Mid / Full tiers
  while closed (Q26); on reopening they fade in at their closed-form positions. One per-lane "open fade" uniform in
  the group texel is read by both the CPU point layer and `SOLNpcTraffic.ush`, so CPU and GPU stay identical. Their
  share of the budget stays with the lane (Q10), so nothing respawns.
- **Merge readout / AUTO (Q30, Q14):** recommended, a closed lane is skipped by the merge readout and by the lane
  search; a ship holding in it follows Q14.

## 6. Routing (D3, D4)

### 6.1 Edge weight

The lane's travel time is the mean-effort leg on the AUTO keep-right track, end ring to end ring:

    T_edge = MakeLegProfile(RadialFactor(NPC_LANE_AUTO_TRACK_WALL_RADII), NPC_LANE_EFFORT_MEAN, l0).LegS
           = LaneLegTimeS(profile, frame, 0, frame.CurrentLegM, 0.25, +-1)   for every valid frame

The second line holds because the metric map's slope cancels (SDD 50 4.9). `l0` is the frozen nominal leg (the
maximum length, D16), so **`T_edge` is constant for the population**: it does not change as the planets move, the
graph's weights never change, and Dijkstra reruns only when an order is given or a lane on the plan opens or closes
(never per frame). Same time both ways.

Approximate edge times, from a script that re-implements `MakeLegProfile` as written in `SOLLaneTrafficMath.cpp`
(1.65 G design peak, rho 0.25, mean effort: alpha 11.38 m/s^2; the track's cap `c_k = sqrt(w^2 + kappa (c^2 - w^2))`
= 290.5 km/s, which every planet lane coasts at; Earth-Moon does not; `l0` taken as `L0`, the end arcs are under
1e-3 of it). The tests pin the identity above, not these figures:

| Lane | `L0` (AU) | `T_edge` |
|---|---|---|
| Earth-Moon | 4.06e8 m | about 2.5 h |
| Mercury-Venus | 1.195 | 7.4 d |
| Mercury-Earth | 1.483 | 9.1 d |
| Venus-Earth | 1.745 | 10.7 d |
| Venus-Mars | 2.394 | 14.5 d |
| Earth-Mars | 2.683 | 16.2 d (SDD 50's "16 days") |
| Earth-Jupiter | 6.471 | 38.8 d |
| Mars-Jupiter | 7.121 | 42.7 d |
| Mars-Saturn | 11.716 | 70.1 d |
| Jupiter-Saturn | 15.505 | 92.7 d |
| Jupiter-Uranus | 25.551 | 152.5 d |
| Saturn-Uranus | 30.146 | 179.9 d |
| Saturn-Neptune | 40.379 | 240.9 d |
| Uranus-Neptune | 50.424 | 300.8 d |

Example routes (transfers not counted): Earth -> Saturn 86.3 d via Mars (Earth-Jupiter-Saturn is 131.5 d); Earth ->
Uranus 191.4 d via Jupiter; Earth -> Neptune 327.2 d via Mars and Saturn; Mercury -> Mars 21.9 d via Venus.

### 6.2 Transfer cost (Q12)

At a node the ship leaves lane 1 at its end ring (wall speed relative to the node, SDD 50 D3) and merges into lane 2
at its end ring. Recommended: `T_transfer = P + (0.5 (r1 + r2) theta + |r1 - r2|) / w`, `r_i` the end-ring distances
from the node centre, `theta` the angle between the two lane directions at the node, `w` the wall speed (20 km/s), `P`
a fixed penalty (`NPC_LANE_TRANSFER_PENALTY_S`, provisional 1 h) so a route does not gain a hop for a few seconds. The
arc keeps the estimate off the planet (a chord can pass through it). At Earth this is minutes to an hour against days
per lane.

### 6.3 Dijkstra

`SOLLaneNetwork::FindFastestRoute(nodes, edges, transfer, source, target)` over at most 64 edges and a few dozen nodes:
binary heap on the stack or a fixed array (no allocation per call beyond a reserved scratch), state = (node, arriving
lane) so the transfer cost can depend on the lane pair. Closed lanes skipped. Ties: lower time, then fewer hops, then
the lower lane index sequence (deterministic). `source == target`: found, no hops. Unreachable: not found.

### 6.4 AUTO integration

- **Router scope (Q11).** Direct flight is much faster than lanes (Earth-Mars about 3 days plain AUTO against 16 days
  in the lane, SDD 50 D19), so the router is **lane-only**: a "direct" edge would win every time and lanes would never
  be used. Recommended: route when the AUTO target is a node, or a body whose parent planet is a node (the last mile
  flies direct), and the ship is at a node (its anchor body) or holding in a lane. Otherwise, and when no route exists,
  AUTO behaves as today (D20 straight-path search). War power moves ignore lanes as today (SDD 50 D22).
- **Plan.** A transient fragment field (not a `UPROPERTY`, not saved): hop list (up to a fixed count), current hop,
  plan serial. Each hop latches its lane like today's leg: entry arc 0 (or the ship's arc), exit arc at the hop's end
  ring, Approach / Hold / leave through the existing `LaneAutoDecide` pieces. `Left` becomes **per hop**; after the
  last hop it is final as today. While a plan exists the D20 straight-path search is off (Q15).
- **Transfer.** Leaving a hop at its end ring, the next hop's merge point (its keep-right track at entry arc 0) is the
  Approach target, so the existing moving-target rendezvous does the transfer.
- **Re-plan (Q14).** At each lane-network epoch change, if a not-yet-flown hop's lane closed, re-plan from the current
  hop's end node. If the lane being held closes: leave it (per-hop `Left`), fly the normal AUTO step to the hop's end
  node, then re-plan from there; HUD notice text in `SOLMenuText`.
- **ETA.** The HUD ETA becomes the current hop's `LaneAutoEtaS` plus the remaining edge and transfer times.
- **Display (Q16).** Recommended: no route drawing in v1; the AUTOPILOT marker gets a "VIA <node>" line while a
  multi-hop plan runs (new `SOLMenuText` token).

## 7. Activation and budget by pixels (D2, D8)

### 7.1 Tunnel activation

Per lane per frame on the CPU (cheap: one point-segment distance):

    r_max_px = f_ref Rt / d_seg

`d_seg` = camera distance to the lane's axis segment (`SOLNpcScale::DistancePointToSegmentM`), `f_ref` = the **live
view's** focal length in reference pixels (the same value the tunnel shader derives from `ViewToClip[0][0]`, SDD 52
4.9; a fixed reference focal would break the "invisible switch" at another field of view), `Rt` = the tunnel radius. Every ring centre lies on the segment, so no ring is larger on
screen than `r_max_px`. Below `NPC_TUNNEL_RADIUS_FADE_GONE_PX` (0.5 px) every ring, end ring, dot and chevron of the
lane has alpha 0 (SDD 52 D9, D12, D14), so **dropping the lane's instance there is invisible**. Recommended rule (Q17):
activate at `r_max_px >= 0.5`, deactivate below `0.4` (the band only avoids instance churn; both sides are invisible).
An inactive lane builds no tunnel frame, writes no texels and has no instance (this also answers the CPU headroom debt:
cost follows the active lanes, not the lane count).

The details' extra test is weak: the ring gap never exceeds `f S0 / d`, so dots and chevrons can show only while
`r_max_px >= 10 / 12` = 0.83 px. A three-class scheme (off, rings only, full) would only help between 0.5 and 0.83 px,
so the recommendation is two classes, off and full (Q17).

### 7.2 Traffic activation

Today far lane ships are hidden by the point alpha `1 - LaneLineFade(d, R) x (1 - strength)` (0 beyond 400 lane radii
of the camera), a distance-in-lane-radii rule, which at the reference focal is "lane radius below 2.4 px". Recommended
(Q20, Q22): restate those thresholds in reference px of the lane radius (same look at 90 deg, correct at other fields
of view) and mark a lane's traffic active while some ship could pass the restated rule (lane radius at the nearest
point at least 2.4 px), or while a CPU-tier ship of it exists. An inactive lane's flag in the group texel lets the point
vertex shader skip the rail evaluation of its ships (Q21; it saves the evaluation and the pixels, not the vertex
invocation). Ships keep their closed-form rails at every distance (D5).

### 7.3 Pool and budget (D8, Q18, Q19)

- **Per-lane budget:** 0 vertices for an inactive lane, the slot mesh for an active one (12,500 today; 15,380 with the
  knee fix of Q19, 160 ring slots).
- **Pool:** `NPC_TUNNEL_INSTANCE_POOL` instances (recommended 16, so the 14 v1 lanes all fit and the cap is never hit in
  v1). Vertex budget = pool x slot mesh: 200,000 (246,080 with the knee fix). Replaces
  `NPC_TUNNEL_VERTEX_BUDGET` / `_LANES` (the 100,000 / 8 interim).
- **When the pool is full:** the active lanes are ranked by `r_max_px` and the smallest on screen lose their instance;
  a warning is logged once per change of the overflow, a stat counter shows it, and the user is told (the limitation,
  never the LOD rule). With 14 lanes and 16 slots it cannot happen in v1.
- **Labels:** `NPC_TUNNEL_LABEL_MAX_COUNT` 4 is a count cap on screen clutter (Q20).

## 8. Traffic weights and future hooks

- **Budget:** lane ships stay `LaneFraction` x population (400,000 of 1M). 14 lanes share them by weight, so **each
  existing lane gets fewer ships** (with the Q4 rows the weights sum from 9.0 to 12.75: Earth-Mars from about 88,900 to
  about 62,700 ships). Its in-wall count, so its tunnel radius `Rt` and wall radius `Rw` (SDD 50 D13), shrink a little:
  visible as slightly thinner tunnels.
- **Importance (Q4):** recommended per-lane pair weights (section 4.3); the planet-size option would make the outer
  giants the busiest lanes, the reverse of today's tuning.
- **Hooks (record only, not built):** station / city nodes (`ESOLLaneNodeKind::Station`, `City`) with their own
  position source; **waypoint re-routing**: a lane split at a station node placed off the Sun line or at a strategic
  point, each part a straight closed-form lane, so a lane that would close near the Sun can bypass it; moon and
  asteroid nodes joining the next-closest rule; destination traffic (SDD 50 4.11) using routes.

## 9. Tunnel geometry impact

- Slot mesh unchanged unless Q19 (160 ring slots, +2,880 vertices per lane) is approved; the material and asset are
  unchanged (`NpcTunnelLayout4.zw` free if a tunable is needed).
- Instances: rewritten when the active set changes (as today on a change of drawn lanes).
- Texture: 64 x 4 texels unchanged; row 3 `.z` becomes the open fade x draw flag.
- Earth hub: 5 lanes end at Earth (Venus-Earth, Earth-Mars, Earth-Moon, Mercury-Earth, Earth-Jupiter); their end rings
  sit in different directions at Earth radius + 3 Rt and their labels compete for the 4 label slots (Risk R3).
- 21:9 focal: not addressed (16:9 only supported); stays debt.

## 10. Module edges

- `Traffic/SOLLaneNetworkMath.{h,cpp}`: new, pure (graph build, next-closest, Sun clearance, epoch bound, edge and
  transfer times, Dijkstra, pixel class, pool selection). Includes `SOLConstants.h` and `SOLLaneTrafficMath.h` only.
- `Traffic/SOLLaneTrafficSubsystem`: gains the node table, the graph, the open state per lane-network epoch and the
  route query.
- `Npc`: `DefaultLayout` rows, spawn (node table, lane set), visuals (open fade, active flags), tunnel component
  (activation, pool). `Ship`: flight processor (plan, hops). `UI`: VIA line, notice.
- No new `Traffic -> Universe`, `Ship -> Npc` or `UI -> Npc` edge.

## 11. Save and persistence impact

**No schema change is needed.** The lane set is derived from the registry and the layout at spawn; the open state is
a pure function of sim time (Q5 option); routes and hops are transient fragment fields, never saved; the NPC
population is not saved. `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay. **If the user wants a player
setting** (for example "prefer lanes" or a route display toggle), that changes the settings schema and needs the
user's approval and a version bump with migration; this SDD does not decide it.

## 12. Test plan

Pure math first (`Source/SOLTest/Tests/LaneNetworkMathTest.cpp`, `SOLTest.LaneNetwork.*`), authored by a separate
subagent from [`Plans/53-lane-network-contract.md`](../Plans/53-lane-network-contract.md), not from the code:

1. **GraphBuild:** base lanes kept in order at indices 0-7; node table from synthetic bodies.
2. **NextClosest:** synthetic 4-node chain gives exactly {0-2, 1-3}; ties to the lower index; the real J2000 keys give
   the 6 lanes of section 4.2 in table order; a fully linked node adds nothing.
3. **SunClearance:** segment distance (interior, endpoint, degenerate, through the origin), threshold 1.3914e10 m,
   hysteresis state machine.
4. **EpochBound:** the conservative bound, epoch index (negative and huge times), purity (same input, same output).
5. **EdgeTime:** the identity against `MakeLegProfile` and `LaneLegTimeS` (for several frames and both directions);
   the two synthetic profiles' values.
6. **Transfer:** formula values, symmetry, `theta` = 0 and pi (blocked on Q12).
7. **Dijkstra:** the synthetic 5-node graph (penalty switches the route), closed edge, unreachable, source == target,
   tie-break; real network routes of section 6.1 with transfers 0.
8. **PixelClass / Pool:** `r_max_px`, the 0.5 / 0.4 hysteresis, NaN and zero distance, pool ranking and overflow count.
9. **TrafficWeights:** lane ship total unchanged by the lane count; shares by weight.

Re-validated: `NpcScaleMathTest` (`DefaultLayout`), `LaneAutoRulesTest` (layout rows by name; new per-hop `Left`),
`NpcTrafficMathTest`, `NpcTunnelFarFieldTest` (`SlotMeshBudget`, `Constants` if Q19), `NpcTunnelMathTest`
(`VisibilityRules`), `NpcLaneMathTest`, smokes that assume 8 lanes (`-SOLSmokeNpcTunnel`, `-SOLSmokeNpcLanes`,
`-SOLSmokeAutoLanes`, `-SOLSmokeLaneMerge`, `-SOLSmokeNpcTraffic`). New smoke `-SOLSmokeLaneRoute` (a two-hop AUTO trip
at warp, a closed lane re-route, the VIA line). PIE smoke screenshots at 1280x720, 1920x1080 and 4K.

## 13. Performance targets

Same gates as #52 (SDD 52 section 8, D18), at 1920x1080, 1M NPCs, 14 lanes:

- **Tunnel draw <= 0.15 ms GPU**, measured with the pool full (all 14 forced active) and in the T2 / T5 views.
- **Tunnel update + labels <= 0.025 ms CPU on vs off** (`sol.NpcTunnel 0`, SDD 49 D20 method). With pixel activation
  the cost follows the active lanes; report if missed.
- **CPU gates to confirm (Q25), proposed:** lane-network epoch evaluation <= 0.01 ms averaged per frame at 30 d/s
  warp; a route query <= 0.05 ms; the per-frame activation pass (14 point-segment distances) <= 0.002 ms; the AUTO lane
  rules get their own `SCOPE_CYCLE_COUNTER` (closes the SDD 50 debt that the 0.01 ms gate could not be isolated).
- **Points draw:** no gate (SDD 52 D19), recorded against SDD 52's interleaved baseline (cross-session drift about 10 %,
  so only interleaved A/B runs count). The flame CPU gate (#51 debt) is re-measured and reported.
- **1M ramp:** every stage passes as in SDD 52 13.3.
- Method: interleaved A/B worktree runs (SDD 52 13.3 amendment), `-SOLSmokeNpcTunnelProfile`.

## 14. VR floor and risks

**VR floor:** record only, no VR work before procedural terrain (user rule): 2560x1440 recorded as the one-eye proxy as
in SDD 52; 3840x3744 and two-eye not measured.

- **R1 thinner lanes:** the fixed lane budget spread over 14 lanes thins today's tunnels.
- **R2 long trips:** Earth -> Neptune about 317 days of sim time; the ETA must read well at that size.
- **R3 Earth hub clutter:** 5 lane ends at Earth; labels, end rings and merge readout pick among more lanes.
- **R4 lanes near third bodies:** a straight lane can pass close to another planet (Earth-Jupiter near Mars at some
  alignments); no rule today, recorded only.
- **R5 multi-hop AUTO regressions:** `Left` per hop and re-planning touch the 50d review-fixed code; the
  `AutopilotRegression.NoLaneBitIdentical` case and the AUTO smokes guard it.
- **R6 epoch definition:** the GPU re-base option (Q5 b) can miss closures at warp.
- **R7 group columns:** future nodes' lanes plus clusters plus battles must fit 64.
- **R8 shader mirror drift:** the open fade and the active flag go into `SOLNpcTraffic.ush` and `SOLNpcTunnel.ush` and
  their CPU mirrors in the same change.
- **R9 re-epoch hitch** (existing debt) is untouched; more lanes do not grow it (it is per ship).

## 15. Change log

- 2026-11-22: first draft from the user's grill-me decisions D1-D8; current state from the code at `9943d8e`; open
  questions Q1-Q30 for the user. No code, no persistence change.

## 16. Open questions (for the user; each with a recommended answer, none decided here)

- **Q1 Next-closest rule.** (a) **Recommended:** the nearest planet not linked by a base lane, by `|da|` (semi-major
  axes, static, computed once at spawn); one per planet, duplicates merged, giving the 6 lanes of 4.2. (b) by current
  distance at spawn (the set changes with the spawn date). (c) time-mean distance (Mercury becomes every planet's
  nearest).
- **Q2 Earth-Moon.** Your D1 says planets only, but today's lanes include Earth-Moon. **Recommended:** keep it as the
  one existing Moon-node lane; moons do not join the next-closest rule in v1.
- **Q3 Dwarf planets.** **Recommended:** not nodes in v1 (later "large asteroid" nodes).
- **Q4 Importance and new rows.** Pair traffic weight or planet size? **Recommended:** pair weights per lane, today's 8
  rows unchanged, the 6 new rows as in 4.3 (radius and weight).
- **Q5 Epoch.** (a) **Recommended:** a fixed sim-time lane-network epoch (1 day, tunable) with the conservative
  sampled bound (5.3), the state a pure function of sim time. (b) the GPU rail re-base.
- **Q6 Hysteresis.** **Recommended:** off below 20 solar radii, on above 22.
- **Q7 Clearance measure.** **Recommended:** the axis segment between body centres (the halo is under 0.5 %).
- **Q8 Drain.** (a) **Recommended:** ships fade out with the tunnel and stay hidden, rails keep running. (b) no new
  departures after closing, ships in a leg finish it (they still pass near the Sun). (c) (b) started one leg time early
  (long lanes close for far longer than unsafe: Uranus-Neptune leg 291 d against a 158 d window).
- **Q9 Switch fade.** **Recommended:** 3 s real time.
- **Q10 Drained ships' share.** **Recommended:** stays with the closed lane (no respawn).
- **Q11 Router scope.** **Recommended:** lane-only routing when the target is a node (or a body whose parent planet is a
  node) and the ship is at a node or in a lane; otherwise today's behaviour; War power unchanged.
- **Q12 Transfer cost.** **Recommended:** arc at wall speed plus a 1 h penalty (6.2). Alternatives: penalty only, or 0.
- **Q13 First and last mile in the weights.** **Recommended:** not in Dijkstra (small against days), added to the HUD
  ETA only.
- **Q14 Lane closes during a trip.** **Recommended:** re-plan at the epoch change for hops not yet flown; a held lane
  that closes is left, the ship flies normal AUTO to that hop's end node and re-plans there, with a HUD notice. Plan
  with the current state only (no time-dependent prediction) in v1.
- **Q15 D20 search while a plan runs.** **Recommended:** off while a plan exists, on otherwise.
- **Q16 Route display.** **Recommended:** no map route drawing in v1, a "VIA <node>" line on the AUTOPILOT marker.
- **Q17 Tunnel classes and thresholds.** **Recommended:** two classes (off, full), on at 0.5 px, off below 0.4 px.
- **Q18 Pool size.** **Recommended:** 16 instances (200,000 vertices; 246,080 with Q19); overflow ranked by pixels,
  logged, counted and reported.
- **Q19 Knee debt.** **Recommended:** fix now (160 ring slots, 15,380 vertices per lane), since only active lanes pay.
  The 21:9 focal debt stays.
- **Q20 Legacy lane controls.** **Recommended:** restate `NPC_LANE_FADE_NEAR/FAR_RADII` and the 100 -> 50 label band in
  reference px of the lane radius (9.6 / 2.4 px and 9.6 -> 19.2 px at the reference focal; no visible change at 90
  deg); keep the 4-label cap as a screen-clutter limit and record it in `TECH_DEBT.md` as a count control.
- **Q21 Traffic activation granularity.** **Recommended:** a per-lane active flag in the group texel, the point vertex
  shader skips the rail evaluation; not contiguous per-lane rail ranges (that changes `MakeSpawn`).
- **Q22 Traffic pixel metric.** **Recommended:** the lane radius at the nearest point at least 2.4 reference px (the
  restated 400 lane radii), or a CPU-tier ship of the lane exists.
- **Q23 Lane order.** **Recommended:** new lanes appended at indices 8-13. (Every lane ship's lane still changes, since
  the weights change.)
- **Q24 STATUS.md wording.** Row 3b5d says "distance on/off", which the pixel rule forbids. **Recommended:** reword to
  "pixel on/off, Sun clearance" in the 53a commit.
- **Q25 Gates.** **Recommended:** keep the #52 gates; add the CPU gates of section 13.
- **Q26 Closed lanes' ships and the AI tiers.** **Recommended:** not promoted while closed.
- **Q27 Flame gate.** **Recommended:** re-measure and report in 53f, no fix in #53.
- **Q28 Group columns.** **Recommended:** keep 64 in v1 (40 used at most) and record it as the hard pool for future
  nodes.
- **Q29 NPC tier radii and budgets.** Lane ships still pass through the distance radii and count budgets of SDD 11
  (`FullAiRadiusM`, `MidAiRadiusM`, `MeshRadiusM`, `InstancedRadiusM`, `FullAiBudget`, `MidAiBudget`, `MeshBudget`,
  `InstancedBudget`), which D2 and the pixel rule do not allow as LOD. **Recommended:** out of scope for #53 (they are
  every NPC's tiers, not the lanes'); recorded in `TECH_DEBT.md` as legacy distance / count LOD to convert in its own
  ticket.
- **Q30 Merge readout on a closed lane.** **Recommended:** hidden for a closed lane (the readout would advise merging
  into traffic that is not drawn).