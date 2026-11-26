<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# SDD 53: Lane network

**Issue:** #53 (listed as To do in [`Plans/STATUS.md`](../Plans/STATUS.md), row 3b5d; no GitHub issue checked or
created for this draft). Plan: [`Plans/53-lane-network-plan.md`](../Plans/53-lane-network-plan.md). Test-first
contract: [`Plans/53-lane-network-contract.md`](../Plans/53-lane-network-contract.md). Builds on SDD 11 (NPC rails and
tiers), SDD 38 (lane lines), SDD 49 (guidance tunnel), SDD 50 (lane traffic, AUTO lane rules) and SDD 52 (tunnel far
field, D15: the vertex budget and active-tunnel cap were left interim for this ticket).
**Status: DRAFT, amended 2026-11-24 with the user's answers to Q1-Q30** (section 16) **and 2026-11-25 with the
user's answers to O1-O10** (section 17) **and 2026-11-25 with the user's answers to the 53c2 decisions P1-P6**
(section 18). **The population tier values (O6), the partner hysteresis values (O3) and the
Sun-safety override UI (O4) were confirmed by the user on 2026-11-25**. The Moon relays island links **through Earth
only** (user decision A at the 53a/53b review, replacing the earlier "the Moon can be a relay"). The pure math
and its tests (53a/53b) are implemented and in review; nothing else is built.
**Persistence: no schema change is needed by this design** (section 11). `SOL::Settings::SAVE_VERSION` stays 13 and
`KEY_BINDING_LAYOUT_VERSION` stays 10. The player's Sun safety override (O4, 6.5) is session-only; saving it across
launches would be a settings schema change and needs the user's approval first.

**Pixel rule (project docs, user rule 2026-11-21), repeated for every part:** LOD, fade, visibility and activation
thresholds are on-screen pixels (1080p reference pixels), never distance or object counts. A fixed pool that cannot
grow is the only allowed cap, and hitting it is reported to the user as a limitation, never used as the LOD rule.

## 1. Goal

Grow the 8 hand-listed lanes into a lane network:

- more lanes and more endpoints: the planets, the Moon, the 5 dwarf planets and the 10 largest belt asteroids, each
  linked to its next-closest node (with partner hysteresis), every island linked to Earth's connected group, the set
  recalculated once per sim day;
- a node model that later carries other moons, space stations and space cities;
- lanes switched on and off for drawing and traffic by their size on screen, not by distance or count;
- lanes closed while their straight line passes too close to the Sun, with every ship in flight finishing its trip;
- "fastest A to Z" routing over the lane graph, used by AUTO, with the planned route shown on the system map;
- a Sun danger zone (20 solar radii) that every AUTO flight detours around, which the player (never an NPC) can
  override for the session;
- the interim tunnel vertex budget (100,000 for 8 lanes, 12,500 per lane, SDD 52 D15) replaced by three pixel classes
  per lane and one hard pool of 24 instances.

## 2. Current state (code survey, HEAD `9943d8e`)

| Piece | Where | Facts that matter here |
|---|---|---|
| Lane list | `Npc/SOLNpcScaleMath.cpp` `DefaultLayout()` | **8 lanes**, hand-written rows (body A, body B, lane radius, weight): Mercury-Venus (2e6 m, 1.0), Venus-Earth (2e6, 1.5), **Earth-Moon** (2e5, 1.0), Earth-Mars (3e6, 2.0), Mars-Jupiter (5e6, 1.5), Jupiter-Saturn (8e6, 1.0), Saturn-Uranus (1e7, 0.5), Uranus-Neptune (1e7, 0.5). That is the 7 neighbouring-orbit planet pairs plus one planet-moon lane, **not every planet pair**. Clusters: 10 rows (9 bodies plus the belt) |
| Lane ships | `SOLNpcScale::SplitCounts`, `MakeSpawn`, `PickWeighted` | Lane ships = `LaneFraction` (0.4) of the population (400,000 of 1M); each lane ship picks its lane by hash over the lane weights (not contiguous per lane) |
| Group table | `Npc/SOLNpcRail.h` `FSOLNpcGroupTable` | Lanes first, then clusters, then battles; all share the **64 group columns** (`NPC_GROUP_TEXTURE_WIDTH`). Today 8 + 10 + up to 16 battles = 34 |
| Lane profile | `SOLNpcRail::BuildLaneProfiles`, `SOLLaneTraffic::MakeLaneProfile` | Frozen at spawn: wall radius `Rw` from the in-wall count (SDD 50 D13), nominal length `L0` = the sum of the two aphelia (D16, `NpcLaneNominalLengthM` walks registry parent chains, so a moon lane works) |
| Lane geometry per frame | `SOLNpcRail::BuildFrame`, `SOLLaneTraffic::MakeLaneFrame` | The straight segment between the two **current** body centres is rebuilt **every frame**; the nominal leg is mapped onto the current leg (SDD 50 4.5), so a ship's **leg time does not depend on the current geometry** |
| Epochs | `USOLNpcSubsystem::Spawn`, `ASOLNpcVisuals::ReepochRails` | Two only: the spawn epoch (profiles frozen) and the GPU rail re-base (`NPC_GPU_RAIL_EPOCH_S` 1e4 s of rail time, at most every `NPC_GPU_RAIL_EPOCH_MIN_INTERVAL_S` 2 s real). **Neither re-derives the lane set**; nothing checks the Sun |
| Lane table for AUTO / HUD | `Traffic/SOLLaneTrafficSubsystem` | Per lane: end-body indices and the frozen profile; no graph |
| AUTO lanes | `SOLLaneTraffic::FindLaneLeg`, `LaneAutoDecide`, `Ship/SOLShipFlightProcessor.cpp` | The **first lane (lowest index)** whose halo holds the **straight path ship -> target** for 20 wall radii (D20) is latched; `Left` is terminal for the order. **No multi-lane trip exists.** Lane views on the stack, up to 64 (`FLIGHT_MAX_LANES`) |
| Edge time | `SOLLaneTraffic::LaneLegTimeS`, `MakeLegProfile` | Closed form, flip window included; the mean-effort leg on the keep-right track (rho 0.25) |
| Tunnel | `Npc/SOLNpcTunnelComponent.cpp`, `SOLNpcTunnelMath` | One slot mesh of 12,500 vertices, one instance per drawable lane, **every lane built and drawn every frame at every distance** (SDD 52 D1). `NPC_TUNNEL_VERTEX_BUDGET` 100,000 / `NPC_TUNNEL_VERTEX_BUDGET_LANES` 8 only produce a **log warning**: there is **no active-tunnel cap in code** |
| Legacy distance / count controls on lanes | `SOLConstants.h` | `NPC_LANE_FADE_NEAR/FAR_RADII` (100 / 400 lane radii, the lane-ship point fade), `NPC_TUNNEL_FADE_START/FULL_RADII` (the 100 -> 50 label strength band), `NPC_TUNNEL_LABEL_MAX_COUNT` 4 (a count cap). Distances in units of the lane's own radius are angular sizes, so each is a pixel threshold at a fixed focal length (100 lane radii = 9.6 reference px of lane radius at 960 px focal) |
| Belt asteroids | `MinorBodies/SOLAsteroidBelt.cpp` `BELT_REAL_ASTEROIDS`, `RealAsteroids()` | 500 JPL SBDB rows **with diameters**, sorted by diameter descending, Ceres left out (it is a registry dwarf planet). **Not registry bodies**: elements are `FSOLSecularElements` evaluated by their users (orbit lines, overlay icons). `Npc/` has no `MinorBodies` include today |
| Dwarf planets | `Universe/SOLBodyRegistry.cpp` `REGISTRY_CHILD_BODIES` | Ceres, Pluto, Eris, Makemake, Haumea are registry bodies parented to the Sun (fixed ellipses, no secular drift) |
| Sun safety | `Ship/SOLShipFlightProcessor.cpp` | Only the swept body collision (the Sun included). **No Sun keep-out zone or detour exists in AUTO** |
| System map routes | `Map/SOLRouteSubsystem`, `Map/SOLRouteOverlayMath.h` (`ESOLRouteDrawStyle`), `FSOLMapRouteOverlay` | The player's waypoint routes; no lane plan drawing |
| Persistence | `Menu/SOLSettingsSave.h` | Settings only (Lanes overlay row). No AUTO order, route, NPC or lane data is saved |

Tech debt handed to or touching #53 (grep of `Docs/TECH_DEBT.md`, not re-reported as new): the ring window ends at
the 1 px knee instead of the 0.5 px gone point (fixed here, Q19); the window assumes a 16:9 focal (21:9 needs more
slots; stays); the tunnel CPU cost has no headroom (0.016-0.022 ms on vs off, about 1 us per drawn lane); the thruster
flame CPU gate miss "revisit with #53" (re-measured only, Q27); the D21 points cost inside a tunnel; the re-epoch hitch
(9.85 ms at 1M under warp); the AUTO lane rules have no profiling scope; `UpdateCpuShips` re-derives float lane
positions per CPU ship; the legacy distance and count NPC LODs (`MeshRadiusM`, `InstancedRadiusM`, the AI radii and
budgets; already recorded, Q29).

## 3. Decisions (user, grill-me)

Round 1 (D1-D8), as amended by round 2 (section 16) and round 3 (section 17):

- **D1: Endpoints v1.** Every lane of today (the 8 rows of section 2) stays, **plus each node to its next-closest node
  where no lane exists**. Nodes v1: the 8 planets, the Moon (Earth-Moon only), the 5 dwarf planets and the 10 largest
  belt asteroids (Q2, Q3). The next-closest partner is chosen by **current distance** and the lane set is
  **recalculated once per sim day** (Q1); a node switches partner only when the new one is clearly closer and after a
  minimum stay (O3). **Every island is linked to its nearest node in Earth's connected group** (O2), so every node is
  in one graph every day. Later nodes (other moons, space stations, space cities) use the **same node type**.
- **D2: Activation by pixels.** A lane is drawn and trafficked by the on-screen pixel size of its tunnel and traffic.
  A hard pool is the only cap and is reported if hit. No distance or count LOD.
- **D3: Routing.** "Fastest A to Z" = shortest **travel time** over the lane graph: Dijkstra over closed-form segment
  travel times (flip-and-burn included) plus a per-node transfer cost, possibly via intermediate nodes; used by AUTO.
- **D4: Transfers only at nodes.** A ship arrives at a node on one lane, then leaves on another.
- **D5: Long lanes and the Sun.** No special length rule: pixels decide drawing and traffic, traffic stays simulated
  (closed form) when far, and invisible lanes stay in the routing graph. **A straight lane must not pass the Sun
  closer than a safe distance** (20 solar radii, about 0.093 AU): such a lane **closes** until it is safe again, and
  routing goes another way. The same sphere is the **Sun danger zone of every AUTO flight** (O4, 6.5). **No curved
  lanes.** **Design hook only:** a later ticket may re-route lanes through
  stations placed at strategic waypoints (section 4.8).
- **D6: Planets move.** The lane set and the Sun clearance are re-derived each lane-network epoch (1 sim day, Q5).
- **D7: Traffic density by population** (Q4, O6): node population tiers, lane weight = **square root of the product
  of its two end tiers**, with the **same total lane ship budget** (400,000) as today.
- **D8: Tunnel vertex budget by pixels.** Each lane's budget follows the pixel size of its tunnel (three classes, Q17);
  one hard pool of 24 instances (Q18) is the only cap, reported if hit.

## 4. Lane graph and node model

### 4.1 Node

New pure types in `Traffic/SOLLaneNetworkMath.h` (namespace `SOLLaneNetwork`), fed plain arrays by the NPC subsystem,
so `Traffic/` keeps no `Universe` or `MinorBodies` edge (SDD 50 4.10):

```cpp
enum class ESOLLaneNodeKind : uint8 { Planet, DwarfPlanet, Moon, Asteroid, Station, City };   // v1: the first four

struct FSOLLaneNode
{
    ESOLLaneNodeKind Kind = ESOLLaneNodeKind::Planet;
    int32 BodyIndex = INDEX_NONE;    // Registry body (planet, dwarf planet, Moon); INDEX_NONE for an asteroid
    int32 SourceIndex = INDEX_NONE;  // Non-registry position source: the asteroid's BELT_REAL_ASTEROIDS row
    int32 ParentNode = INDEX_NONE;   // Moon -> its planet node (future: station -> host)
    double ClearanceRadiusM = 0.0;   // Body radius (end rings sit at it + 3 Rt)
    double AphelionM = 0.0;          // Sun-relative aphelion; L0 of a lane = the sum of its two ends' (D16)
    double RegionRadiusM = 0.0;      // Lane radius of the node's region (4.4, O5)
    double PopulationTier = 0.0;     // Traffic tier (Q4, O6: values confirmed by the user 2026-11-25)
};
```

Node positions are not stored: for each epoch sample and each frame the caller passes the node positions (registry
nodes from the registry; asteroid nodes from their catalogue elements, evaluated by the NPC subsystem with the same
`FSOLSecularElements` propagation the orbit lines use). Every lane-network function takes node indices, never registry
indices. Kinds Planet, DwarfPlanet and Asteroid take part in the next-closest rule and in the island links (4.3); the
Moon does not (Q2): it is reached through Earth-Moon only.

### 4.2 Nodes v1 (Q2, Q3)

| Node | Name | Kind | Source | Tier | Region radius (m) | Aphelion (AU) |
|---|---|---|---|---|---|---|
| 0-7 | Mercury, Venus, Earth, Mars, Jupiter, Saturn, Uranus, Neptune | Planet | registry | 2, 2, 10, 6, 2, 2, 1, 1 | 2e6, 2e6, 2e6, 3e6, 5e6, 8e6, 1e7, 1e7 | 0.47, 0.73, 1.02, 1.67, 5.45, 10.05, 20.10, 30.33 |
| 8 | Moon | Moon | registry | 10 | (Earth-Moon row: 2e5) | via Earth (existing walk) |
| 9-13 | Ceres, Pluto, Haumea, Makemake, Eris | DwarfPlanet | registry | 4, 1, 1, 1, 1 | 4e6, 1e7, 1e7, 1e7, 1e7 | 2.98, 49.32, 51.51, 52.80, 97.58 |
| 14-23 | the 10 asteroids below | Asteroid | belt catalogue | 3 | 4e6 | 2.57-3.84 |

**The 10 largest belt asteroids** (by `DiameterKm` from `BELT_REAL_ASTEROIDS`, which carries SBDB diameters and is
already sorted descending; Ceres is not in the catalogue, so nothing had to be excluded by hand):

| # | Asteroid | Diameter (km) | a (AU) | Aphelion (AU) |
|---|---|---|---|---|
| 14 | 4 Vesta | 522.77 | 2.361 | 2.57 |
| 15 | 2 Pallas | 513.0 | 2.770 | 3.41 |
| 16 | 10 Hygiea | 407.12 | 3.151 | 3.49 |
| 17 | 704 Interamnia | 306.313 | 3.057 | 3.53 |
| 18 | 52 Europa | 303.918 | 3.094 | 3.44 |
| 19 | 511 Davida | 270.327 | 3.162 | 3.76 |
| 20 | 31 Euphrosyne | 267.08 | 3.162 | 3.84 |
| 21 | 451 Patientia | 253.9 | 3.065 | 3.28 |
| 22 | 87 Sylvia | 253.051 | 3.491 | 3.82 |
| 23 | 3 Juno | 246.596 | 2.671 | 3.35 |

(The next two, 65 Cybele 237.26 km and 88 Thisbe 232.0 km, are out.) **24 nodes**, 23 of them in the next-closest rule.
Asteroid nodes are named by their designation ("4 Vesta"), as the belt names them.

### 4.3 Lane set per epoch (D1, Q1, Q5, O2, O3)

The set of epoch `k` (sample time `t_k = k E_L`, `E_L` = 1 sim day) is built in three steps: base lanes, partner lanes
(nearest node with hysteresis), island links.

- **Base lanes:** today's 8 rows, always in the set, slots 0-7 (Q23).
- **Nearest node (rule A):** each node of kind Planet, DwarfPlanet or Asteroid has as its **nearest** the nearest such
  node by centre distance at `t_k` that is not linked to it by a base lane (ties to the lower node index). (Rule B,
  "the nearest node, added only if no lane exists", was not chosen.)
- **Partner picks with hysteresis (O3):** each taking-part node holds one **pick** (partner, stay in epochs). Per epoch
  the pick steps: no pick yet, or the held partner no longer valid: take the nearest (stay 1). Otherwise keep the
  held partner (stay + 1) unless **both** the stay has reached `NPC_LANE_PARTNER_MIN_STAY_EPOCHS` (30
  days, confirmed by the user 2026-11-25) **and** the nearest is clearly closer: `d(nearest) <
  NPC_LANE_PARTNER_SWITCH_RATIO x d(held)` (0.8, i.e. at least 20 % closer, confirmed by the user 2026-11-25); then take the nearest (stay 1). The partner set is the union of the picks, each
  lane keyed by its sorted node pair `(NodeA < NodeB)` and stored in key order; **a lane is in the set while either
  end picks the other.** The draft named no margin or stay values; the user confirmed the two above on 2026-11-25.
- **Hysteresis by stepping, with a warm-up after a time jump:** the picks of epoch `k + 1` are always one step from
  the picks of epoch `k` (the look-ahead buffer of 5.3 computes each new far-end epoch from the one before it), so a
  pick really lasts its minimum stay and switches only by the margin. A replay that restarts from a fixed number of
  epochs back every day would not do that (it only delays the plain pick), so it is not used. **After a time jump**
  (start, `-SOLSimDate`, any clock set) the picks are warmed up: start at epoch `k - W` (`W` =
  `NPC_LANE_PARTNER_WARMUP_EPOCHS`, 64, confirmed by the user 2026-11-25, which must exceed the minimum stay) with no picks, so the first step
  takes each node's plain nearest, then step forward to `k` and on to the horizon. Consequence: **the partner set is
  no longer a pure function of sim time**; it depends on the sim time and on the time of the last jump (deterministic
  for both, identical for any time inside one epoch, nothing saved). The Sun state keeps its pure look-back. A jump
  already rebuilds the schedule and every window, so a different set after a jump is not visible as a pop of ships
  in flight (their windows are re-derived at the jump, as before).
- **Island links (O2):** after the partner set, the graph (base + partner lanes, Sun-closed lanes included) may have
  islands not connected to Earth. They are linked one at a time: among every pair (u in Earth's group, v outside),
  with finite positions (u and v both of kind Planet, DwarfPlanet or Asteroid), take the pair with the
  smallest centre distance at `t_k` (ties to the lower sorted pair), add that lane, merge v's whole island into
  Earth's group, and repeat until no island is left. So **any planet, dwarf planet or large asteroid already in
  Earth's group may act as the relay**, including one joined by an earlier link of the same epoch. **The Moon relays
  through Earth only** (user decision A, 53a/53b review): it is never an end of an island link; a route from the Moon
  to an island is Moon -> Earth -> island, the island link is Earth's (its nominal length the sum of Earth's and the island node's aphelia), and the Moon
  is shown and treated as a stop on Earth. The Moon never gets a lane of its own to a non-parent node. Each island gets exactly one link (components
  minus 1 links in all; the island is a connected component, not a node). Island links are ordinary lanes (slot, ship
  share, Sun test, window, tunnel); they have no hysteresis of their own (their churn follows the hysteresis-smoothed
  picks, R6). The Moon is never an island (it is always joined to Earth by its base lane), is never a partner pick (4.3 rule A),
  and is never an end of an island link (Earth relays for it). **Stations and space cities as relays stay a design hook (4.8); nothing
  is built for them.**
- **Measured before O2 and O3** (script over 250 sim years from 2026-01-01 at 1-day steps, Kepler propagation of the
  registry rows and the belt catalogue rows, rule A picks with no hysteresis and no island links; the game starts at
  the real date, so these are statistics, not a schedule). These figures are kept as the baseline; the final rules are
  measured in the next table (53c1):

  | Figure | Value (before O2, O3) |
  |---|---|
  | Lanes in the set | 22-29 (mean 25.3): 8 base + 14-21 partner lanes |
  | Changes of the set | about 24 per year (one every 2 weeks) |
  | Distinct pairs over 250 years | 205 (8 base + 197 partner): lane indices 8+ are **slots keyed by node pair**, not fixed rows |
  | Partner-lane episodes (one continuous stay in the set) | 5,096, of which **854 are shorter than the lane's own leg time** (no trip can fit, 5.4) |
  | Lanes Sun-unsafe at once | at most 5, mean 0.39 |
  | Longest nominal length of any lane that occurs | 149.1 AU (Eris-Haumea), leg about 889 d |
  | Graph connected (all 24 nodes reachable from Earth) | **8 % of days** (1 to 6 components) |

- **Reachability from Earth before O2** (fraction of days, longest stranded run): the 8 planets and the Moon always
  (base chain). Ceres 0.67 / 739 d, Vesta 0.78 / 424 d, Pallas 0.75 / 1,089 d, Hygiea 0.68 / 1,002 d, Interamnia
  0.67 / 1,274 d, Europa 0.71 / 1,089 d, Davida 0.69 / 1,017 d, Euphrosyne 0.75 / 1,089 d, Patientia 0.67 / 1,110 d,
  Sylvia 0.73 / 880 d, Juno 0.73 / 1,119 d; Pluto 0.62, Haumea 0.38, Eris 0.53, each stranded up to 18,977 d (52
  years); Makemake 0.76 / 15,887 d. **With the island links every node is in Earth's graph on every day** (R11 keeps
  the remaining cases: a Sun-closed link or a window shorter than the leg).
- **Effect of O2 and O3 (derived from the baseline, not measured):**
  - Island links: one per island, so components minus 1: **0-5 links** at the measured 1-6 components. Lanes in the
    set: **about 22-34, measured 25-30 (below)** (8 base + 14-21 partner + 0-5 links). Hard bound: 8 base + 23 picks + 7 links (15 non-planet
    taking-part nodes, an island needs at least 2) = **38 lanes**.
  - Hysteresis: each node still holds one pick, so partner lanes stay at most 23 and the bounds above hold. It cuts
    set changes and short episodes (a pick lasts at least 30 days, longer than the leg of the inner-planet partner
    lanes, about 9-16 d, and of Mars-4 Vesta, 25.6 d; Earth-belt lanes, about 29 d, barely fit and asteroid-asteroid
    legs of 35-45 d can still miss a short episode). Measured in the table below.
  - Longest lane: an island link can join any two taking-part nodes, so the longest possible nominal length is now
    the largest sum of two aphelia, **Eris + Makemake = 150.38 AU, leg about 897 d** (under the 960-epoch horizon).
    The float path is validated at 151 AU (4.7).
- **Measured with the final rules (53c1):** 250 sim years from 2026-01-01, 91,312 daily steps, one time jump at the
  start, the real schedule code (partner hysteresis, island links, Sun state):

  | Figure | Value (final rules) |
  |---|---|
  | Lanes in the set | 25-30 (mean 27.07) |
  | Changes of the set | 15.5 per year |
  | Island links per day | 0-5 (mean 2.01), so 1-6 components before linking |
  | Lanes Sun-closed at once | at most 5 (mean 0.493) |
  | Distinct pairs over 250 years | 187 |
  | Partner-lane episodes | 3,694, of which **393 are shorter than 30 days** (the minimum stay; not the baseline's "shorter than the leg" metric, and which lanes they are was not broken down) |
  | Longest lane that occurred | Haumea-Eris, 149.08 nominal AU (the possible maximum stays Eris + Makemake, 150.38 AU) |
  | Group columns at the worst day | 30 lanes + 10 clusters + 16 battles = **56 of 64**, no overflow day at 38 lane columns |

- **Snapshot, illustrative (2026-01-01, rule A picks, before hysteresis):** 18 partner lanes: Mercury-Earth,
  Mercury-Mars, Venus-Mars, Earth-511 Davida, Mars-4 Vesta, Jupiter-87 Sylvia, Saturn-Neptune, Saturn-Pluto, Saturn-2
  Pallas, Uranus-87 Sylvia, Neptune-Eris, Ceres-52 Europa, Haumea-Makemake, 4 Vesta-2 Pallas, 10 Hygiea-87 Sylvia,
  704 Interamnia-31 Euphrosyne, 31 Euphrosyne-451 Patientia, 451 Patientia-3 Juno. It has **3 islands** ({Ceres, 52
  Europa}, {Haumea, Makemake}, {704 Interamnia, 31 Euphrosyne, 451 Patientia, 3 Juno}), so **3 island links**, 29
  lanes in all; their endpoints need that day's positions and were not recomputed here.
- **Group columns (Q28):** 64 shared columns stay the hard pool. Lanes hold a column while they are in the set, and a
  closing lane's column is freed after its 3 s tunnel fade (or sooner: a column held only by its fade is the first
  a new lane takes when none is free, 53d review). With the early stop of 5.4 (nothing is in flight at the
  close) the measured worst day is **30 lanes + 10 clusters + 16 battles = 56 of 64** (no overflow day at 38 lane columns, table
  above); the hard bound of 38 lanes gives **exactly 64**. Under high warp the 3 s real fade spans many epochs (about 90 at 30 d/s), so fading lanes can hold
  columns on top of the set (R7). Hitting 64 is logged, counted and reported to the user; a lane that finds no column
  has no traffic and no tunnel until one frees.

### 4.4 Lane radius and weight (Q4, O5, O6)

**Population tiers (confirmed by the user, 2026-11-25):** Earth 10, Moon 10, Mars 6, Ceres 4, each
belt asteroid 3, Mercury, Venus, Jupiter and Saturn 2, Uranus, Neptune, Pluto, Haumea, Makemake and Eris 1. **Lane
weight = sqrt(tier A x tier B)** (O6, the user's rule; double precision), normalised so the lanes in the set share the
same 400,000 lane ships as today (largest remainder, 8.). The square root softens the spread: Earth-Moon 10 against
Uranus-Neptune 1, where the plain product gave 100 against 1.

Planet chain and Earth-Moon (the 8 base lanes):

| Lane | Weight | Ships today | Ships, base lanes only (total weight 32.096) | Ships, 2026-01-01 set (total weight 83.140) |
|---|---|---|---|---|
| Earth-Moon | 10 | 44,444 | 124,624 | 48,112 |
| Earth-Mars | 7.746 | 88,889 | 96,534 | 37,267 |
| Venus-Earth | 4.472 | 66,667 | 55,734 | 21,516 |
| Mars-Jupiter | 3.464 | 66,667 | 43,171 | 16,666 |
| Mercury-Venus | 2 | 44,444 | 24,925 | 9,622 |
| Jupiter-Saturn | 2 | 44,444 | 24,925 | 9,622 |
| Saturn-Uranus | 1.414 | 22,222 | 17,625 | 6,804 |
| Uranus-Neptune | 1 | 22,222 | 12,462 | 4,811 |

Partner lanes of the 2026-01-01 set (the 18 rule-A lanes of 4.3, partner weight 51.044): Earth-511 Davida 5.477
(26,352 ships); Mercury-Earth 4.472 (21,516); Mars-4 Vesta 4.243 (20,412); Mercury-Mars, Venus-Mars and Ceres-52 Europa
3.464 (16,666 each); the 5 asteroid-asteroid lanes 3 (14,433 each); Jupiter- and Saturn-asteroid 2.449 (11,785);
Uranus-87 Sylvia 1.732 (8,333); Saturn-Neptune and Saturn-Pluto 1.414 (6,804); Neptune-Eris and Haumea-Makemake 1
(4,811). The snapshot's 3 island links (endpoints not recomputed, weights about 1 to 3.5 each, about 7-9 in all) are
**not** in the last column; with them every figure there drops by about 8-10 % (Earth-Moon about 44,000, 11 %).

**Earth-Moon share:** 31.2 % with the base lanes only, **12.0 %** of all lane ships in the 2026-01-01 set (about 11 %
with its island links). Over the daily sets of the 250-year run (4.3) it is **measured at 9.9-13.6 %** (mean 12.0 %); it
was 23-33 % with the plain product. *(53c1 interim, lifted in 53c2a/53c2b: the lane ships used these sqrt weights over
the 8 base lanes only, about 31 % Earth-Moon; they are now shared over the slotted set and follow it per epoch.)*

**Lane radius of a partner lane (O5, decided):** the larger of its two ends' region radii (table 4.2). It reproduces
all 7 planet base rows (Mars-Jupiter = max(3e6, 5e6) = 5e6, and so on); the belt value is 4e6 m; Earth-Moon keeps its
hand row 2e5 m. Island links use the same rule.

### 4.5 Lane identity, slots and lifecycle (consequences of Q1)

- **Identity:** the sorted node pair. A lane that leaves the set and comes back later is the same identity in a new
  episode (it may get another slot).
- **Slot:** base lanes own slots 0-7. A partner lane or island link takes the lowest free group column from 8 at its first epoch in
  the set and keeps it while it stays in the set (also while Sun-closed). The column is freed after the close and the
  3 s tunnel fade, and not before the player's held hop on that lane has ended (R17). **Resolved (user, 53d
  review):** a column held only by its fading tunnel (no ship blocks) is pre-emptible: a new lane that finds no free
  column takes the lowest such column, cutting the fade short, before any lane overflows; a column whose lane still
  owns ship blocks is never taken (`ESOLLaneSlotHold` Blocks / Fade, `AssignLaneSlots`). The hold is set by the block
  step, so a lane that closes in the same epoch a new lane arrives is still Blocks-held then and the new lane waits
  one epoch. The assignment depends on the session's history but is deterministic and only feeds rendering and
  the AUTO lane views; nothing is saved.
- **Profile per episode:** `L0` is static per pair (sum of aphelia). The ship count, and with it `Rw` and `Rt` (SDD 50
  D13), is frozen at the episode's open, so a lane's tunnel radius changes only between episodes, never mid-trip.
- **States:** Open (departures allowed), Draining (it closes within one leg time: no new departures, ships in flight
  finish), Closed-Sun (in the set but unsafe; no traffic in flight by construction), Gone (no longer chosen; slot
  freed after the fade).
- **Ship budget (O1, decided):** the 400,000 lane ships are cut into fixed **ship blocks**
  (4,000 blocks of 100 ships). **Membership (P3, section 18):** block `b` holds the lane-local ship indices
  `[100 b, 100 b + 100)`, contiguous so a block move is one contiguous rail range; a block is still spread along its
  lane because each ship's phase is hashed. For other populations the block count is ceil(lane ships / 100), the last
  block partial. Each lane in the set holds blocks in proportion to its weight. **Pool policy (P4):** an opening lane's
  target is its `LaneShipShares` share over the current set; blocks join a lane only at its episode open (no top-up
  mid-episode); when the pool is short, slots take in ascending slot order; a time jump re-shares every block exactly,
  as at spawn. A Sun-closed lane keeps its blocks (Q10). A Gone lane's
  blocks are empty once its trips have finished (the early stop guarantees that at the close) and return to a free
  pool; an opening lane takes its share from the free pool. Ships only change lane while their block is empty, so no
  ship visibly jumps lanes. Cost: the normalisation is exact at spawn and approximate afterwards (blocks move only
  through the free pool); each block move rewrites only that block's rails (a partial GPU rail upload, not the full
  re-epoch). The alternatives are worse: re-hashing every ship daily makes ships jump lanes and re-uploads all rails
  every epoch (at 30 d/s, 30 times the 9.85 ms re-epoch hitch per second); a fixed per-pair share does not keep the
  400,000 total.

### 4.6 Graph

Undirected multigraph per epoch: nodes = lane nodes, edges = lanes in the set with `(nodeA, nodeB, timeS, open
window)`. Each lane can be flown both ways (keep right, SDD 50 D1). A route is a sequence of hops `(lane slot,
direction)`, direction +1 for A -> B.

### 4.7 Float path at the new lengths

SDD 52's float-path bounds were validated at the longest lane of today, Uranus-Neptune at 50.4 AU. Dwarf-planet
lanes reached **149.1 AU** (Eris-Haumea) in the baseline run, and with the island links any two taking-part nodes can
be joined, up to **150.38 AU** (Eris + Makemake, 4.3). So the float rail and tunnel paths are re-validated in 53c at
**151 AU** (`FloatPathAtMaxLength` re-pinned; R10), which covers every possible pair (O8, decided). If they fail, 53c
stops and reports, and the fix is the user's call (cap the nominal length, split the rail time, or leave the Eris lanes
without a tunnel).

**Result (53c, decided by the user 2026-11-25):** the ring slot offsets pass at 151 AU; the end-arc texels do not
hold 0.05 Rt on the end far from the foot point once a lane is longer than about 59 AU (float32 rounding of the arc,
up to 0.09 Rt at 151 AU). The user accepted the error: that end is 50-150 AU from the camera and invisible, and it
becomes small and exact as the camera nears it. The 151 AU row stays in `FloatPathAtMaxLength` with the far arc's
tolerance relaxed to the float32 rounding bound (half an ulp) for lanes over 59 AU; no texel layout change; recorded
in `TECH_DEBT.md`.

### 4.8 Hooks (record only, not built)

Station / city nodes (`ESOLLaneNodeKind::Station`, `City`) with their own position source, later also acting as
island-link relays (O2: a later design, nothing built; the Moon is not one, it relays through Earth only, 4.3);
**waypoint re-routing**: a lane split at a station node placed
off the Sun line, each part a straight closed-form lane; other moons joining the next-closest rule; destination traffic
(SDD 50 4.11) using routes.

## 5. Sun clearance and closing

### 5.1 Math (Q6, Q7)

Sun at the origin (the registry keeps it fixed there). For a lane between body centres `A` and `B`:

    t* = clamp(-(A . (B - A)) / |B - A|^2, 0, 1)
    d  = |A + t* (B - A)|                (|A| for A == B)
    unsafe  if  d < k_off R_sun,   k_off = NPC_LANE_SUN_CLEARANCE_RADII = 20  ->  1.3914e10 m = 0.0930 AU
    safe again only if  d > k_on R_sun,   k_on = NPC_LANE_SUN_REOPEN_RADII = 22  ->  1.53054e10 m

The measure is the axis segment between the body centres (Q7): the halo reaches at most `H Rw` = 6e7 m from the axis,
under 0.5 % of the clearance.

### 5.2 How often lanes close (circular, coplanar estimate, base lanes)

Near opposition of the two bodies, `d ~ r1 r2 sin(pi - theta) / (r1 + r2)`, so the lane is unsafe while
`|pi - theta| < asin(k R_sun (r1 + r2) / (r1 r2))`:

| Lane | Half-angle | Closed per synodic period |
|---|---|---|
| Mercury-Venus | 21.7 deg | 17 d of 145 d (12 %) |
| Venus-Earth | 12.8 deg | 42 d of 584 d (7 %) |
| Earth-Mars | 8.9 deg | 38 d of 780 d (5 %) |
| Mars-Jupiter | 4.5 deg | 21 d of 816 d (2.5 %) |
| Jupiter-Saturn | 1.6 deg | 64 d of 19.9 y (0.9 %) |
| Saturn-Uranus | 0.8 deg | 77 d of 45 y (0.5 %) |
| Uranus-Neptune | 0.45 deg | 158 d of 171 y (0.3 %) |

Partner lanes use the same test; the 250-year run gives at most 5 unsafe lanes at once. Inclination and eccentricity
shift these (an out-of-plane lane can pass above the Sun and stay open); the tests use the exact segment distance.
Earth-Moon never comes near.

### 5.3 Epoch, look-back and look-ahead (Q5)

- **Epoch:** `E_L` = `NPC_LANE_NETWORK_EPOCH_S` = 86,400 s of sim time, `k = floor(t / E_L)`.
- **Choice of partners:** from the node positions at `t_k` (one sample per epoch, no bound needed), each epoch's
  picks one hysteresis step from the previous epoch's (warm-up of 64 epochs after a time jump, 4.3); island links from
  the positions at `t_k`.
- **Sun clearance of epoch `k`:** from positions sampled at `N` points across `[t_k, t_k + E_L]` with the conservative
  bound `d_min_bound = min_i d(t_i) - v_max (dt / 2)` (`v_max` the larger endpoint speed; Mercury at 59 km/s with
  `dt` = 6 h gives a 6.4e8 m margin, under 1 solar radius).
- **Hysteresis as a pure function (look-back):** the Sun state of epoch `k` is found by walking back from `k` to the
  last epoch whose bound was below `k_off R_sun` (Closed) or above `k_on R_sun` (Open), at most
  `NPC_LANE_SUN_LOOKBACK_EPOCHS` epochs (64, O10); if every epoch in reach is inside the band, Closed (fail safe).
  So the state needs no history and nothing is saved.
- **Look-ahead schedule:** the early stop (5.4) needs each lane's close time up to one leg ahead. The lane-network
  state (set, Sun state) of the `H` epochs `k .. k + H - 1` is kept in a ring buffer, `H` = `NPC_LANE_NETWORK_HORIZON_EPOCHS`
  (960, O10; above the longest possible leg of about 897 d, 4.3), each entry also holding the epoch's partner picks.
  Each epoch change computes one new epoch at the far end, stepping the picks from the entry before it; a time jump
  (start, `-SOLSimDate`, any clock set) warms the picks up over 64 epochs and rebuilds all `H` epochs. A lane still open through the last entry closes at the start of epoch `k + H` (fail safe: a leg
  longer than the horizon never departs).
- **Catch-up and constants:** a forward move of up to `NPC_LANE_NETWORK_CATCH_UP_EPOCHS` (16) epochs is stepped one
  epoch at a time. A longer move, a backward move or a marked clock set rebuilds. `USOLSimClockSubsystem` counts its
  `SetJulianDate` calls that set a new date (`GetTimeSetCount`; every clock set goes through it, and a writer that holds
  its pinned date every frame counts once; `-SOLSimDate` at startup bypasses the counter, which is harmless because it runs before the spawn). `USOLNpcSubsystem` compares the count each
  frame before `AdvanceNetwork` and calls `USOLLaneTrafficSubsystem::MarkNetworkTimeJump` when it changed.
  `SOL::SIM_CLOCK_MAX_WARP` = 2,592,000 (30 d/s, the clock's top warp step, `static_assert`-tied to the warp table), and
  `NPC_LANE_NETWORK_CATCH_UP_EPOCHS x NPC_LANE_NETWORK_EPOCH_S >= MAX_FRAME_DELTA_S (0.5 s) x SIM_CLOCK_MAX_WARP`, so one
  clamped frame at the top warp moves at most 15 epochs and always steps. `NPC_LANE_NODE_SPEED_MARGIN` (1.1): a node's
  speed bound is its perihelion speed x 1.1 (the Moon adds Earth's), which covers secular drift. The Sun clearance bound
  uses it.
- **Cost (measured, asserted by `SOLTest.LaneNetworkSubsystem.MeasuredCost`):** a full rebuild takes **1.7-2.1 ms**
  (gate 5 ms, 39.3 ms before the fixes below; met only with parallel passes on an idle 16-core machine, see R13). An epoch step takes a mean 0.0113-0.0154 ms, i.e. **0.0056-0.0077 ms
  per frame** at the top warp 30 d/s and 60 fps (0.5 epochs per frame; gate 0.01 ms per frame, 0.0265 before the fixes).
  The rebuild did not need spreading over frames. How:
  - Node positions at each epoch start sit in a direct-mapped ring covering `k - max(W, L) .. k + H` (`W` = 64 warm-up,
    `L` = 64 Sun look-back, `H` = 960). The end sample of epoch `e` (`t_k + E`) is the next epoch's start, and the
    look-back re-reads the same entries.
  - The inner clearance samples (3 per epoch) are taken only for a lane whose two end-sample bound cannot classify it.
    Every sample lies at least `min(d0, dE) - v E / 2` from the Sun, so `min(d0, dE) - v (E + dt) / 2` above the reopen
    distance is surely open and `min(d0, dE) - v dt / 2` below the close distance is surely closed. They go in a second
    ring of the same span (the two rings are about 0.6 MB and 1.8 MB for v1).
  - The Moon adds its Earth-relative position to Earth's (no chain re-walk). The position path is
    `SOLKepler::ElementsToPositionM` (position only, no velocity) and `FSOLBodyRegistry::ComputeRelativePositionAtM`. The
    position function must be a pure function of time, because worker threads call it.
  - One shared distance matrix per epoch (`FSOLLaneEpochDistances`, `NearestFromDistances`, `IslandLinksFromDistances`).
    The results equal `StepPartnerPicks`' nearest and `IslandLinks` (checked daily in the tests).
  - The rebuild runs in four passes: start positions and nearest partners of every epoch in parallel
    (`ParallelForWithExistingTaskContext`, one scratch context per worker), the picks in order, each entry's lane set and
    per-lane clearance class (above reopen, below close or in band, as bit masks) in parallel, then the Sun state in
    order. Slot assignment is skipped while the set and the held flags are unchanged.
  - Trace scopes: `Advance`, `Rebuild` (with `RebuildPositions`, `RebuildSets`), `StepOne`, `UpdateSlots`.

### 5.4 Close, drain and finish (Q8, Q9, Q10, Q14, Q26, Q30)

- **When a lane closes:** at the first epoch where it is Sun-unsafe or no longer chosen (Gone).
- **Early stop:** a ship leg departing at `t` is flown only if the lane is open over all of `[t, t + T_leg]`. So
  departures stop one leg time before a close, **every ship in flight finishes its trip, none vanishes, and no NPC is
  in flight on a lane when it becomes unsafe** (the clearance bound is conservative). A leg that does not fit is not
  flown: the ship is **docked** at its node (not drawn; its rail keeps running in closed form; nothing respawns). When
  the lane reopens (or a new episode starts), departures resume at the ships' closed-form phases.
- **Pure and mirrored:** each lane's slot carries its current window as two floats relative to the GPU rail epoch,
  `openStart` and `departStop` (= close - `T_leg`), plus the active flag (Q21), in the group texel. A ship is drawn iff
  its current leg's departure time lies in `[openStart, departStop]`. `SOLNpcTraffic.ush` and the CPU point layer read
  the same texel (R8). **U-turns (P1, section 18):** a turning ship follows the leg after the turn: it is drawn iff
  that leg fits, so a ship arriving where its next leg is not flown vanishes at the end ring on arrival (the future
  docking-at-station moment). **Built (53c2b):** `SOLNTR_LaneShipDrawn` in the traffic shader and its CPU mirror
  `SOLNpcTrafficMirror::LaneShipWindowDrawn` read group texture row 6 and also hide parked ships (rail `.w` < 0);
  the CPU point layer draws a Point-tier lane ship only when the mirror says drawn. Measured float band
  (`SOLTest.NpcRail.GpuLaneWindowMirror`): Earth-Moon 0.064 s, Mars-Jupiter 32 s, a 128 AU lane 512 s; every in-band
  disagreement lies exactly on a window bound, none outside the band.
- **Latched open start (53a/53b review):** the look-ahead buffer starts at the current epoch `k`, so a lane open at
  `k` has no open start inside the buffer. `LaneOpenWindow` then takes the start the slot held at the previous epoch
  (the latch; -inf allowed, meaning open since before anything known). A lane that was not open at the previous
  epoch (a new lane, or a reopen at `k`) latches `openStart = k E_L`, so no ship is drawn mid-leg on a lane that
  just appeared; departures from `k E_L` on are drawn as they start. The latch is carried per slot from epoch to
  epoch (in memory only, nothing saved). **Latch after a time jump (decided by the user, 2026-11-25):** a time jump
  (start, `-SOLSimDate`, a clock set) passes **-inf** as the latch for lanes open at the new `k`, so those lanes are
  full at once, as today. (NaN, every lane opening at `k E_L` and filling over one leg time, up to about 897 days for
  the longest, was not chosen.) A lane that opens at a later epoch still latches `k E_L`.
- **Tunnel:** only the tunnel fades, 1 -> 0 over **3 s real time** at the close (tunnel texture row 3 `.z`), 0 -> 1 at a
  reopen (Q9). The traffic is not faded; it drains by finishing.
- **Share (Q10):** a Sun-closed lane keeps its share (its blocks); a Gone lane's empty blocks go back to the pool
  (4.5, O1). **Built (53c2b):** per-epoch block moves (release, take at an episode open, re-bucket, partial rail
  upload; `Npc/SOLNpcBlockMoves`, `SOLLaneNetwork::StepShipBlocks`) and an exact re-share with one full upload at a
  time jump (P4). A slot is held while its blocks still own ships. **Open (known hitches, `TECH_DEBT.md`):** in game
  a move costs 0.58-0.88 ms per block (mean 0.68, over the 0.5 ms gate; headless 0.38-0.40), 34-274 ms per opening
  in the 53c2b warp run, and a time-jump re-share 1.8-2.1 s, almost all of it the per-entity Mass re-bucket.
- **AI tiers (Q26):** lane ships are promoted to the Mid / Full AI tiers as usual. Interplay: only ships flying a leg
  are in space, so a docked ship is never promoted; a promoted ship whose leg ends where the next leg is not flown is
  **demoted to Far and docked at once** (P2, section 18; it disappears at the end ring, which the player can see; R12,
  accepted by the user, O9: no fade).
- **Merge readout (Q30):** hidden for a closed lane; shown while Draining (traffic still flows). The lane search skips
  closed lanes too; an AUTO **Approach** latched on a lane that closes is **Stale** (dropped and searched again, P6),
  a **Hold** on a closed lane is unchanged until 53e's finish-if-safe.
- **Short episodes:** a lane whose open window is shorter than its leg time never gets a departure: 854 of 5,096
  partner episodes in the baseline run (17 %; Mercury-Mars alone has 54 episodes in 20 years, some of 4 days; before
  O3). Its tunnel still draws while it is in the set (an empty lane). The partner hysteresis (O3, 4.3) keeps a pick at
  least 30 days, which removes most short inner-planet episodes (a Sun closure can still cut one); the measured share is 393 of 3,694 episodes under 30 days (4.3; a different metric from the baseline's "shorter than the leg").

## 6. Routing (D3, D4)

### 6.1 Edge weight

The lane's travel time is the mean-effort leg on the AUTO keep-right track, end ring to end ring:

    T_edge = MakeLegProfile(RadialFactor(NPC_LANE_AUTO_TRACK_WALL_RADII), NPC_LANE_EFFORT_MEAN, l0).LegS
           = LaneLegTimeS(profile, frame, 0, frame.CurrentLegM, 0.25, +-1)   for every valid frame

The second line holds because the metric map's slope cancels (SDD 50 4.9). `l0` is the frozen nominal leg (from the
pair's aphelia), so **`T_edge` is constant per lane identity**: it does not change as the bodies move. Dijkstra reruns
only when an order is given or at an epoch change that touches the plan (never per frame). Same time both ways.

Approximate edge times (script re-implementing `MakeLegProfile`: 1.65 G design peak, rho 0.25, mean effort, alpha
11.38 m/s^2, track cap 290.5 km/s; `l0` taken as `L0`). The tests pin the identity above, not these figures:

| Lane | `L0` (AU) | `T_edge` |
|---|---|---|
| Earth-Moon | 4.06e8 m | about 2.5 h |
| Mercury-Venus | 1.195 | 7.4 d |
| Venus-Earth | 1.745 | 10.7 d |
| Earth-Mars | 2.683 | 16.2 d |
| Mars-Jupiter | 7.121 | 42.7 d |
| Jupiter-Saturn | 15.505 | 92.7 d |
| Saturn-Uranus | 30.146 | 179.9 d |
| Uranus-Neptune | 50.424 | 300.8 d |
| Mercury-Earth (partner) | 1.483 | 9.1 d |
| Mars-4 Vesta (partner) | 4.24 | 25.6 d |
| asteroid-asteroid (partner) | 5.8-7.7 | 35-45 d |
| Saturn-Neptune (partner) | 40.379 | 240.9 d |
| Haumea-Makemake (partner) | 104.3 | 621.9 d |
| Neptune-Eris (partner) | 127.9 | 762.6 d |
| longest in the baseline run (Eris-Haumea) | 149.1 | about 889 d |
| longest possible (Eris-Makemake, through an island link) | 150.38 | about 897 d |

Routes depend on the day's lane set, so no fixed example routes are given.

### 6.2 Transfer cost (Q12)

At a node the ship leaves lane 1 at its end ring (wall speed relative to the node, SDD 50 D3) and merges into lane 2
at its end ring: `T_transfer = P + (0.5 (r1 + r2) theta + |r1 - r2|) / w`, `r_i` the end-ring distances from the node
centre, `theta` the angle between the two lane directions at the node, `w` the wall speed (20 km/s), `P` =
`NPC_LANE_TRANSFER_PENALTY_S` = 1 h. The arc keeps the estimate off the planet.

### 6.3 Dijkstra

`SOLLaneNetwork::FindFastestRoute(nodeCount, edges, startS, source, target)` over at most 64 lane slots and 24 nodes
(v1): fixed-array binary heap, no allocation per call beyond reserved scratch; state = (node, arriving lane) so the
transfer cost depends on the lane pair (each edge carries its unit direction A -> B and its end-ring distances at both
ends; the transfer is `NodeTransferTimeS` with the penalty). **Time windows:** each edge carries its open window; a hop
reached at predicted time `t` is usable only if its window covers `[t, t + T_edge]` (the NPC early-stop rule applied to
the player, O7, decided); otherwise the edge is skipped (no waiting modelled). Ties: lower time, then fewer hops, then the lower
lane slot sequence (deterministic). `source == target`: found, no hops. Unreachable: not found. An edge with a
non-positive or non-finite time is never taken. The result carries a status (`ESOLLaneRouteStatus`: `Found`,
`Unreachable`, `TooManyHops`, `TooManyEdges` for more than the 64 edges the fixed state arrays hold, and
`InvalidEndpoint`), so the edge cap is told apart from an unreachable target and can be logged and reported as a
limit.

**Pinned limit (53a/53b review):** each (node, arriving lane) state keeps only its earliest arrival, and no waiting is
modelled. The route is therefore exact for **current windows** (every window already open at the start time, which is
the case for a route planned now: a lane in the set is open now or closed). With a window that opens later, a faster
arrival can miss it where a slower arrival at the same state would meet it, and that slower path is not explored.
Exactness there would need a list of arrivals per state (allocation per call), so it is not built; routes are
re-planned at each epoch change that touches the plan (6.1), which picks up a lane once it opens.

**Horizon rule (user decision, 53c1):** routes over the lane graph ignore windows beyond the look-ahead horizon (960 d).
A slot window whose close equals the horizon end (the start of epoch `k + H`) plans as open with no close, and every
edge carries `PlanOpenFromS` = the horizon end: a leg departing at or after it uses the lane as open whatever its known
window (the current lane set past the horizon). The plan is re-planned at each node, when the real windows are known
(Q14). A leg departing inside the horizon must fit its window. Measured on 2026-01-01: routes from Earth are found to
10 of 23 nodes (the Moon, Mercury, Venus, Mars and six belt asteroids). The cause that day, from the slot windows:
Venus-Earth and Earth-Mars are Sun-closed, and Mars-Jupiter, the only way outward, closes 0.5 d after the start against
a 42.7 d leg, so nothing past Mars is reachable in time; every block lies inside the horizon.
`SOLTest.LaneNetworkSubsystem.RouteQuery` asserts that the found set lies between a pessimistic earliest-arrival search
(worst transfer at every node) and an optimistic one (free transfers), both 10 that day, and checks the router's
plan-open rule directly (R11).

### 6.4 AUTO integration (Q11, Q13, Q15, Q16)

- **Router scope (Q11):** lane-only routing when the AUTO target is a node, or a body whose parent planet is a node
  (the last mile flies direct), and the ship is at a node (its anchor body) or holding in a lane. Otherwise, and when
  no route exists (a node cut off by a Sun-closed island link or a too-short window, R11), AUTO behaves as today (D20
  straight-path search). War power moves ignore lanes as today (SDD 50 D22), **but obey the Sun safety of 6.5** like
  every AUTO flight (O4).
- **Plan:** a transient fragment field (not a `UPROPERTY`, not saved): hop list (up to `NPC_LANE_ROUTE_MAX_HOPS`),
  current hop, plan serial, and the lane identity (node pair) of each hop, so a hop survives a slot change. Each hop
  latches its lane like today's leg: Approach / Hold / leave through the existing `LaneAutoDecide` pieces. `Left`
  becomes **per hop**; after the last hop it is final as today. **While a plan exists the D20 straight-path search is
  off** (Q15).
- **Transfer:** leaving a hop at its end ring, the next hop's merge point is the Approach target, so the existing
  moving-target rendezvous does the transfer.
- **Re-plan (Q14):** at each epoch change, hops not yet flown are re-planned from the current hop's end node with the
  new set and windows. The held hop follows 6.5.
- **ETA (Q13):** the HUD ETA = the current hop's `LaneAutoEtaS` + the remaining edge and transfer times + the first and
  last mile (direct AUTO estimate). The first and last mile are not in the Dijkstra weights.
- **Display (Q16):** the AUTOPILOT marker gets a "VIA <node>" line while a multi-hop plan runs (new `SOLMenuText`
  token), **and the planned route is drawn on the system map**: a polyline through the current positions of the plan's
  nodes, rebuilt each frame so it follows the moving bodies, in a new `ESOLRouteDrawStyle` distinct from the player's
  waypoint routes (colour and dash). Its line width is in pixels; no distance or count LOD.

### 6.5 Player Sun safety (Q14, O4)

- **Danger zone (O4, decided):** the sphere of `k_off R_sun` (20 solar radii, 1.3914e10 m) about the Sun. **It
  applies to all AUTO flight**: every autopilot order (Move To, Move To (War Power), Follow, Chase Down, the router's
  hops, first and last mile, leave-and-fly). AUTO never plans or keeps a path through it unless the player has
  overridden the safety (below). Manual flight is not affected. **NPCs never override**: they have no override flag,
  and their lane traffic never crosses the zone because the lanes close (5.4). NPC flight outside the lanes (the AI
  tiers) is not changed by this ticket; it is not verified here whether it can cross the zone (recorded as R20).
- **Target inside the zone** (confirmed by the user 2026-11-25; only the Sun itself and points near it qualify: Mercury's perihelion,
  0.307 AU, is outside): with the safety on, the order's arrival point is moved to the `k_on` sphere on the
  Sun-to-ship line (the edge point facing the ship; +Z if the ship is at the Sun's centre), and the HUD notice says
  the order stopped at the danger zone. With the override, the order flies as today.
- **Starting a hop:** only if the lane's window covers the player's estimated hop time (6.3). The estimate uses the
  hold profile at the player's effort; it is the mean-effort `T_edge` unless the player flies slower.
- **A held lane that will close before the hop ends** (Sun or Gone): the hop is **finished only if safe**: the
  remaining straight path (ship -> hop end ring) keeps more than `k_on R_sun` from the Sun at every epoch until the
  estimated arrival (positions from the schedule). A Gone closure is usually safe, so the hop is finished without its
  tunnel. A Sun closure is not: AUTO leaves the lane at once (per-hop `Left`), flies to the hop's end node with the Sun
  detour below, and re-plans there; a HUD notice (`SOLMenuText`) says why.
- **Sun detour (O4, decided, new behaviour):** any AUTO leg whose straight path (ship -> target or waypoint) passes
  inside the danger zone gets one waypoint on the `k_on` sphere, in the plane of the Sun, the ship and the target, on
  the side nearer the ship's velocity; the exact waypoint math and its tests are written in 53e (contract case 15).
  Scope: all AUTO legs, as above.
- **Player override (O4, decided; UI home chosen from the code):** the interaction menus are the GUI that issues every
  AUTO order (`Interaction/SOLActionTable`: Move To, Move To (War Power), Follow, Chase Down; shown by the round
  radial menu `UI/SOLHoloRadial` and the list menu `UI/SOLActionListWidget` from the same table). The override is a
  **toggle row in that table**:
  - a new action `ESOLAction::SunSafetyOverride`, appended after `MoveToWarPower` (no ordinal moves; nothing persists
    `ESOLAction`: no save, settings or key-binding file names it, per `WarPowerMoveTest`);
  - offered on the Star, Planet, Moon and Dwarf rows (the rows that carry the AUTO orders), placed right after Move To
    (War Power); not on Asteroid, Ship or DroppedTarget rows;
  - a fixed label, **"Toggle Sun Safety"** (one `SOLMenuText::INTERACT_ACTION_LABELS` entry: `ActionLabel` takes no
    context and the table ties one label to each action, so a two-state label would need an API change; not done);
    blocked only by NoShip;
  - choosing it flips a **session-only** flag on `USOLShipSubsystem` (default: safety on; never saved; reset at each
    launch); it takes effect at once, also on an order already flying;
  - the state is shown on the HUD: while overridden, the AUTOPILOT marker area shows a "SUN SAFETY OFF" line (new
    `SOLMenuText` token) at all times, AUTO order or not; the Controls / Info screen and `GAME_MECHANICS.md` describe
    the row (53e, 53f).
  - Consequences in code: the body rows grow from 10 to 11 entries, so `SOL::Interact::MAX_ACTIONS` goes from 10 to
    11; the radial sectors scale with the row count, the list menu builds `MAX_ACTIONS` rows, and `ESOLHudLine` gains
    `RadialItem10` (its `static_assert` ties the radial label lines to `MAX_ACTIONS`). No per-row key exists (the list
    menu uses Up / Down / Enter only), so no key binding changes. `WarPowerMoveTest` (MoveToWarPower "last before
    Count", the menus "fill MAX_ACTIONS"), `InteractMenusTest` ("MAX_ACTIONS is 10") and `HoloRadialTest` are
    re-validated.
  - **Persistence:** none. No `USOLSettingsSave` field, no key binding, so `SAVE_VERSION` 13 and
    `KEY_BINDING_LAYOUT_VERSION` 10 stay. Keeping the override across launches would add a settings field and bump
    `SAVE_VERSION` to 14 with a migration: **that needs the user's approval and is not designed here.**
  - Rejected alternative: a per-order variant ("Move To (Sun Unsafe)") would need one extra row per AUTO order (Move
    To, War power, Follow, Chase Down) and the body rows have no room for four.
- **Cases where finishing and Sun safety can conflict (flagged):**
  1. A leg longer than the lane's open window: the lane never accepts the hop; a target reachable only that way is
     unreachable by lane and AUTO falls back to today's behaviour.
  2. The player flies slower than the mean effort: the hop outlasts the window it was started in; the finish-if-safe
     check decides; on a Sun closure the ship leaves mid-lane, near the axis that is swinging towards the Sun, and the
     detour must start at once.
  3. Merging mid-lane through the D20 search (no plan) during Draining: the same check.
  4. A look-ahead horizon shorter than a hop: treated as closing (fail safe), so with too short a horizon long lanes
     become unusable; the horizon is sized above the longest leg.
  5. The detour time is not in the ETA until the detour is planned.
  6. With the override on, the finish-if-safe check and the detour are skipped for the player: a held hop on a
     Sun-closing lane is finished, and direct legs fly straight. Starting a hop still needs the lane's window (6.3),
     since a closed lane has no window.

## 7. Activation and budget by pixels (D2, D8)

### 7.1 Tunnel classes (Q17, Q19)

Per lane per frame on the CPU (one point-segment distance):

    r_max_px = f_ref Rt / d_seg

`d_seg` = camera distance to the lane's axis segment, `f_ref` = the **live view's** focal length in reference pixels
(the value the tunnel shader derives from `ViewToClip[0][0]`, SDD 52 4.9), `Rt` = the tunnel radius. Every ring centre
lies on the segment, so no ring is larger on screen than `r_max_px`.

**Three classes with hysteresis (Q17).** The 0.5 px and 0.833 px thresholds are fixed by the invisible-switch rule
below; the 0.4 px one is the draft's value, accepted by the user (O10). **Resolved (user, 53d review):** the
Reduced -> Full switch moved from the draft's 1.0 px to 0.833 px and Full -> Reduced from 0.833 px to 0.75 px, so both
switches lie where the dots and chevrons are alpha 0 (at 1.0 px they were already visible, so the old switch-on popped
them in):

| Class | Draws | Enter | Leave |
|---|---|---|---|
| Off | nothing (no frame, no texels, no instance) | below 0.4 px from Reduced | - |
| Reduced | rings and end rings; no dots, chevrons or labels | `r_max_px >= 0.5` from Off (`NPC_TUNNEL_RADIUS_FADE_GONE_PX`: everything is alpha 0 below it, SDD 52 D9, D12, D14) | below 0.4 px to Off |
| Full | the whole slot mesh | `r_max_px >= 0.833` from Reduced (`NPC_TUNNEL_FULL_ON_PX` = `NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX` / `NPC_TUNNEL_RING_BASE_SPACING_RADII` = 10 / 12: the ring gap never exceeds `f S0 / d`, so dots and chevrons are alpha 0 at and below it) | below 0.75 px to Reduced (`NPC_TUNNEL_FULL_OFF_PX`, below the Full-on radius, so the dropped dots are alpha 0 too) |

**Graphics quality (user decision B, 53a/53b review):** the four class thresholds (0.4 / 0.5 / 0.75 / 0.833 px) are
the Epic values; every one is multiplied by the QualityPreset's scale, the same factor table as the flame vanish size
V (`SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY` over its Epic entry, no second table): Low x10, Medium x4.6, High
x2.2, Epic and Cinematic x1 (`SOLLaneNetwork::LaneTunnelQualityScale`, passed to `StepTunnelClass`). So a lower
preset turns tunnels off at a larger on-screen size; the rule stays in pixels. The pool ranking (7.3) is unchanged:
a uniform factor keeps the largest-first order. **Resolved (user, 53d):** the same QualityPreset factor scales every
pixel threshold: the shader's ring radius, ring-gap and detail-gap fades read the live focal over the scale
(`SOLNpcTunnel::FadeFocalRefPx`, material `NpcTunnelLayout4.z`, `SOLNT_FadeFocalRefPx`), and so do the Q20 bands
(7.2), so the invisible-switch argument below holds at every preset (`SOLTest.Npc.TunnelMirror.QualityScale`); sizes
and widths keep the live focal.

Both switches happen where the dropped parts are already invisible (at scale 1), so no class change can pop. Labels need a lane
radius of 9.6 px or more (7.2), so they only ever show in Full. An Off lane builds no tunnel frame and writes no texels,
so the CPU cost follows the drawn lanes, not the lane count.

**Knee fix (Q19):** 160 ring slots per side (was 80), so the ring window reaches the 0.5 px gone point: **15,380
vertices per Full lane** (+2,880). The Reduced mesh is the ring and end-ring part only (the slot mesh's first
vertices): **7,220 vertices** (built 53d, `SOLNpcTunnel::TunnelReducedMeshVertexCount`). The 21:9 focal debt stays.

### 7.2 Traffic activation (Q20, Q21, Q22)

- **Restated legacy controls (Q20):** `NPC_LANE_FADE_NEAR/FAR_RADII` (100 / 400 lane radii) become **9.6 / 2.4
  reference px of the lane radius**, and the label band (100 -> 50 lane radii) becomes **9.6 -> 19.2 px**, evaluated
  with the live focal (the same look at 90 degrees, correct at other fields of view), both times the QualityPreset
  scale (built 53d: `SOLNpcLane::LaneLineFade` / `SOLNpcTunnel::TunnelFade` with a focal; the point and lane shaders
  keep their lane-radii inputs, fed `f / 9.6` and `f / 2.4` of the fade focal by `LaneFadeRadiiAtFocal`, so no
  material rebuild). The 4-label cap
  (`NPC_TUNNEL_LABEL_MAX_COUNT`) stays as a screen-clutter limit and is recorded in `TECH_DEBT.md` as a count control.
- **Traffic active (Q22):** while the lane radius at the camera's nearest point on the segment is at least 2.4 reference
  px (the restated 400 lane radii, below which the point alpha is 0) times the QualityPreset's
  `LaneTunnelQualityScale` (P5, section 18: Low 24 px, Medium 11.04 px, High 5.28 px, Epic and Cinematic 2.4 px; the
  same scale as the tunnel classes, 7.1), or while a CPU-tier ship of the lane exists. **Inactive lanes are hidden
  always**, with the Lanes overlay on or off (P5); the flag does not depend on the overlay.
- **Flag (Q21):** a per-lane active flag in the group texel; the point vertex shader skips the rail evaluation of an
  inactive or docked ship (it saves the evaluation and the pixels, not the vertex invocation). Built 53d:
  `SOLNTR_LaneShipDrawn` (the call the material already holds, so no rebuild) is now active flag and window, the window
  test itself `SOLNTR_LaneShipWindowDrawn`; mirror `SOLNpcTrafficMirror::LaneShipPointDrawn`, used by the CPU point
  layer. Without a network the base lanes' full-open window is active (traffic as before the network). Ships keep their
  closed-form rails at every distance (D5).

### 7.3 Pool and budget (D8, Q18)

- **Pool:** `NPC_TUNNEL_INSTANCE_POOL` = **24 instances**, shared by Reduced and Full (one instanced mesh per class).
  Upper bound **24 x 15,380 = 369,120 vertices** (all Full). Replaces `NPC_TUNNEL_VERTEX_BUDGET` / `_LANES` (the
  100,000 / 8 interim).
- **Real worst case:** every lane of the set at once could need an instance only if all were above 0.5 px together;
  the set holds at most **30 lanes** (mean 27.07, measured, 4.3), 38 at the hard bound. Measured with the final rules
  (53c1): at most **8** tunnels above 0.5 px from one viewpoint (cameras at every node and every lane midpoint every
  10 days over the 250 years, tunnel radius 3 lane radii, focal 960 reference px), well under the 24-instance pool.
  So the pool is not expected to fill, but can in principle.
- **When the pool is full:** the drawn lanes are ranked by `r_max_px` and the smallest on screen lose their instance; a
  warning is logged once per change of the overflow, a stat counter shows it, and the user is told (the limitation,
  never the LOD rule). **Hysteresis (53d review):** a lane that had an instance last update ranks by `r_max_px` x
  `NPC_TUNNEL_POOL_KEEP_RATIO` (1.1), so a newcomer must be 10 % larger on screen to take it and near-equal lanes do
  not swap every frame (`SelectTunnelPool`'s previous-pool input).

## 8. Traffic weights

- **Budget:** lane ships stay `LaneFraction` x population (400,000 of 1M), shared by the lanes in the set by weight
  (4.4, sqrt of the tier product, O6) through the ship blocks (4.5, O1). The outer and the old Mercury-Venus lanes get
  fewer ships than today (2026-01-01 set: Uranus-Neptune about 4,800 instead of 22,222, Mercury-Venus about 9,600
  instead of 44,444), so their tunnels shrink towards the minimum radius `R`; Earth-Moon gets about 48,000, 1.1 times
  today's 44,444 (12 % of the lane ships against 11 % today).
- **Profiles:** frozen per episode (4.5).

## 9. Tunnel geometry impact

- Slot mesh: 160 ring slots per side (Q19), 15,380 vertices; a second, Reduced mesh (rings and end rings). Material and
  asset otherwise unchanged (`NpcTunnelLayout4.zw` free if a tunable is needed).
- Instances: rewritten when the drawn set or a class changes.
- Texture: 64 x 4 texels unchanged; row 3 `.z` becomes the open fade x draw flag; the window floats and the active
  flag go into the group texel (5.4, 7.2).
- Earth hub: Venus-Earth, Earth-Moon and Earth-Mars always, plus the partner lanes that pick Earth that day
  (Mercury-Earth and an Earth-asteroid lane in the snapshot) and any island link that uses Earth as its relay (an
  island link never starts at the Moon: Earth relays for it); labels
  compete for the 4 label slots (R3).
- 21:9 focal: not addressed (16:9 only supported); stays debt.

## 10. Module edges

- `Traffic/SOLLaneNetworkMath.{h,cpp}`: new, pure (next-closest, partner hysteresis step and warm-up, island links,
  lane keys and slots, Sun clearance, look-back, windows and the early stop, edge and transfer times, Dijkstra, pixel
  classes, pool selection, ship shares, the Sun danger zone tests). Includes
  `SOLConstants.h` and `SOLLaneTrafficMath.h` only.
- `Traffic/SOLLaneTrafficSubsystem`: the node table, the per-epoch set and slots, the look-ahead schedule, the graph
  and the route query.
- `Npc`: `DefaultLayout` (base rows, tiers, region radii), spawn (node table, ship blocks), visuals (windows, open
  fade, active flags), tunnel component (classes, pool). **New edge `Npc -> MinorBodies`** to read the 10 asteroid
  rows (`RealAsteroids()`), evaluated with the existing `SOLKepler` propagation. `Ship`: flight processor (plan,
  hops, Sun safety for every AUTO order), `USOLShipSubsystem` (the session-only Sun safety override flag).
  `Interaction`: the `SunSafetyOverride` action row (`SOLActionTable`, `SOLInteractionSubsystem::Execute`). `UI`: VIA
  line, notices, the "SUN SAFETY OFF" line, `ESOLHudLine::RadialItem10`; the radial and list menus draw the new row
  with their existing code. `Map`: the plan polyline (reads the plan through the existing ship
  state path; no `Map -> Npc` edge).
- No new `Traffic -> Universe`, `Traffic -> MinorBodies`, `Ship -> Npc` or `UI -> Npc` edge.

## 11. Save and persistence impact

**No schema change is needed.** The Sun state is a pure function of sim time (bounded look-back); the partner set is
a deterministic function of the sim time and the time of the last jump (stepped hysteresis, warm-up after a jump);
the windows follow from both and the look-ahead is rebuilt from the clock at any jump; slots, blocks, routes and hops
are transient, never saved; the NPC population is not saved. The player's Sun safety override (6.5) is a
session-only flag, not saved and bound to no key. `SAVE_VERSION` 13 and `KEY_BINDING_LAYOUT_VERSION` 10 stay. **If the
user wants a saved player setting** (the Sun safety override across launches, "prefer lanes" or a route display
toggle), that changes the settings schema and needs the user's approval and a version bump with migration
(`SAVE_VERSION` 14); this SDD does not decide it.

## 12. Test plan

Pure math first (`Source/SOLTest/Tests/LaneNetworkMathTest.cpp`, `SOLTest.LaneNetwork.*`), authored by a separate
subagent from [`Plans/53-lane-network-contract.md`](../Plans/53-lane-network-contract.md), not from the code:

1. **NextClosest:** current distance, base lanes excluded, ties, key order, Moon excluded, DwarfPlanet and Asteroid
   kinds take part, "either picks the other" persistence.
   - **PartnerHysteresis:** the pick step (margin, minimum stay, invalid partner), the warm-up equal to the step
     chain, consecutive epochs one step apart, determinism.
   - **IslandLinks:** one link per island, nearest pair, relay through a node joined earlier, never through the Moon (Moon via Earth, the Moon / relay tie), ties, NaN nodes
     excluded (the Moon is never an island or a partner pick), a connected graph gives none.
2. **DailyRecalc:** epoch index and sample time; the set is identical for any time inside one epoch; slots stable for
   lanes that stay; new lanes take the lowest free slot from 8; held slots; overflow counted.
3. **SunClearance:** segment distance.
4. **SunHysteresis:** the step thresholds and the pure look-back state, equivalent to the step.
5. **EpochBound:** the conservative bound.
6. **EarlyStop / FinishTrip:** the open window and horizon fail safe; a leg flies only if it fits; departures stop one
   leg before the close; a flown leg stays drawn to its end; windows shorter than the leg allow nothing.
7. **EdgeTime:** the identities.
8. **Transfer:** values, symmetry, zero distances.
9. **Dijkstra:** routes with lane-pair transfers and time windows; the base network.
10. **PixelClasses:** `r_max_px`, the three-class hysteresis, the invisible-switch identities.
11. **Pool:** ranking, the 24-instance overflow, overflow report once per change.
12. **TrafficWeights:** lane weight = sqrt of the product of the tiers (O6), shares sum exactly to 400,000 for any
    lane count.
13. **Nodes:** dwarf planets and asteroids in the rule, nominal length (Earth-Moon and Sun-orbiting pairs), a 150 AU
    lane's edge time.
14. **OverflowReport:** group-column overflow at 38 lane slots and its report.
15. **SunSafety** (53e): the danger zone test on a path, the safety active unless the player overrides (NPCs never),
    the arrival point for a target inside the zone; the detour waypoint cases are added by the 53e test author.

Re-validated: `NpcScaleMathTest` (`DefaultLayout`), `LaneAutoRulesTest` (layout rows by name; per-hop `Left`),
`NpcTrafficMathTest`, `NpcTunnelFarFieldTest` (`SlotMeshBudget`, `Constants`, `FloatPathAtMaxLength` at 151 AU),
`NpcTunnelMathTest` (`VisibilityRules`), `NpcLaneMathTest`, `NpcTunnelMirrorTest`, `WarPowerMoveTest` (the War power
row is no longer the last action; War power obeys the Sun safety), `InteractMenusTest` and `HoloRadialTest` (11 body
rows, `MAX_ACTIONS` 11), `AutopilotRegression.NoLaneBitIdentical` (stays bit-identical for paths that keep out of the
danger zone; zone paths get new tests), smokes that assume 8 lanes (`-SOLSmokeNpcTunnel`, `-SOLSmokeNpcLanes`,
`-SOLSmokeAutoLanes`, `-SOLSmokeLaneMerge`, `-SOLSmokeNpcTraffic`). New smoke `-SOLSmokeLaneRoute` (a two-hop AUTO trip
at warp, a lane closing mid-plan and the re-plan, a Sun detour, the override row toggled, the VIA line, the map route).
PIE smoke screenshots at 1280x720, 1920x1080 and 4K.

## 13. Performance targets (Q25)

The #52 gates (SDD 52 section 8, D18), at 1920x1080, 1M NPCs, plus new CPU gates:

- **Tunnel draw <= 0.15 ms GPU**, measured with the pool full (24 forced Full) and in the T2 / T5 views.
- **Tunnel update + labels <= 0.025 ms CPU on vs off** (`sol.NpcTunnel 0`, SDD 49 D20 method). With pixel classes the
  cost follows the drawn lanes; report if missed.
- **New CPU gates:** lane-network epoch step (pick step and island links included) <= 0.01 ms averaged per frame
  at 30 d/s warp; full look-ahead rebuild (960 epochs plus 64 epochs of positions) <= 5 ms once per time jump (spread
  over frames if missed); a route query <= 0.05 ms; the per-frame class pass (about 34 point-segment distances, 38 at
  most) <= 0.003 ms; a ship-block move <= 0.5 ms (partial rail upload); the AUTO
  lane rules get their own `SCOPE_CYCLE_COUNTER` (closes the SDD 50 debt).
- **Measured (53c1), asserted by `SOLTest.LaneNetworkSubsystem.MeasuredCost`:** full rebuild 1.7-2.1 ms (gate 5 ms),
  epoch step 0.0056-0.0077 ms per frame at 30 d/s and 60 fps (gate 0.01 ms; thin headroom). The rebuild meets its gate
  through the parallel passes (16-core machine); its serial cost was about 8.8 ms. The route query, class pass and block move
  gates are measured in their own parts. **Block move (53c2a), `SOLTest.NpcRail.BlockMoveCost`: 0.024 ms, a partial
  measurement** (spawn, rail and GPU texels of 100 ships, the dirty range and its texel rects; no release, take, Mass
  re-bucket or render-thread upload), re-measured with the full move in 53c2b: **0.384-0.398 ms per block headless,
  0.581-0.878 ms (mean 0.684) in game** over 66 moves of a 10 d/s warp run at 1M NPCs, so the 0.5 ms gate is
  missed in game (reported, not relaxed); a large opening (1,246 blocks) costs 310-338 ms headless, and a time-jump
  re-share 1.1 s headless, 1.76-2.07 s in game (`TECH_DEBT.md`). Caveat: the 5 ms gate is met at 1.97 ms only with the parallel passes on a 16-core idle machine (serial 8.8 ms; roughly 2.5 ms on 8 free threads, 3.5 ms on 4, 5 ms on 2, up to 8.8 ms when the workers are busy); the rebuild lands at spawn, and VR at 90 Hz may drop a frame at spawn (VR work deferred).
- **Points draw:** no gate (SDD 52 D19), recorded against SDD 52's interleaved baseline. The flame CPU gate (#51 debt)
  is re-measured and reported, no fix in #53 (Q27).
- **1M ramp:** every stage passes as in SDD 52 13.3.
- Method: interleaved A/B worktree runs (SDD 52 13.3 amendment), `-SOLSmokeNpcTunnelProfile`.

## 14. VR floor and risks

**VR floor:** record only, no VR work before procedural terrain (user rule): 2560x1440 recorded as the one-eye proxy as
in SDD 52; 3840x3744 and two-eye not measured.

- **R1 thin outer lanes:** population weights leave the outer lanes with about 4,800 ships (Uranus-Neptune, Neptune-Eris,
  Haumea-Makemake in the 2026-01-01 set); their tunnels sit near the minimum radius.
- **R2 long trips:** lanes up to about 897 d per leg; the ETA must read well at years.
- **R3 Earth hub clutter:** 3 base lanes plus partner lanes and island links at Earth; labels, end rings and merge
  readout pick among more lanes.
- **R4 lanes near third bodies:** a straight lane can pass close to another body; no rule, recorded only.
- **R5 multi-hop AUTO regressions:** per-hop `Left`, re-planning and the Sun detour (now on every AUTO order, War power
  included) touch the 50d review-fixed code; `AutopilotRegression.NoLaneBitIdentical` and the AUTO smokes guard it.
- **R6 daily churn:** before O3, about 24 set changes a year and 17 % of partner episodes too short for a trip. The
  partner hysteresis cuts both: measured 15.5 set changes a year and 393 of 3,694 episodes under 30 days (4.3). The
  short ones are island links and lanes held by the other end, which have no hysteresis of their own and can still
  switch relay between two nearly equal candidates from one day to the next (an empty, flickering link). If link churn is
  visible, a link hysteresis is a new decision for the user.
- **R7 group columns:** 56 of 64 at the worst measured day (30 lanes + 10 clusters + 16 battles, no overflow day), exactly
  64 at the hard bound of 38 lanes, plus lanes still in their 3 s fade under warp. Report if hit.
- **R8 shader mirror drift:** windows, open fade and active flag go into `SOLNpcTraffic.ush` and `SOLNpcTunnel.ush` and
  their CPU mirrors in the same change. The traffic window half is built (53c2b: `SOLNTR_LaneShipDrawn` with
  `SOLNpcTrafficMirror::LaneShipWindowDrawn`, pinned by `SOLTest.NpcRail.GpuLaneWindowMirror`); the open fade and
  active flag follow in 53d.
- **R9 re-epoch hitch** (existing debt) is untouched; block moves must use partial uploads, not the full re-epoch.
- **R10 float path up to 150.38 AU:** SDD 52 validated 50.4 AU only; 53c validates 151 AU and stops if it fails (4.7,
  O8).
- **R11 islands (reduced by O2):** the island links put every node in Earth's graph every day, but a node can still be
  unreachable by route while its link is Sun-closed or its window is shorter than the leg (an Eris link of about 897 d
  needs a window that long); AUTO then falls back to today's flight. **Horizon rule (6.3):** routes ignore windows beyond
  the 960 d horizon (a leg departing past it uses the current lanes), but a window closing inside it still blocks.
  Measured on 2026-01-01: routes from Earth are found to 10 of 23 nodes, because Mars-Jupiter closes 0.5 d after the
  start (42.7 d leg) and Venus-Earth and Earth-Mars are Sun-closed; the outer system waits for the next re-plan.
- **R12 docking pop (accepted, O9; P1, P2):** a lane ship that arrives at a node where the next leg is not flown
  disappears at the end ring on arrival (its turn is not drawn), and a promoted one is demoted and docked at that
  moment, which can be on screen. No fade.
- **R13 look-ahead rebuild hitch** on a time jump (5.3): measured 1.7-2.1 ms, but the 5 ms gate is met at 1.97 ms only with the parallel passes on a 16-core idle machine (serial 8.8 ms; roughly 2.5 ms on 8 free threads, 3.5 ms on 4, 5 ms on 2, up to 8.8 ms when the workers are busy); the rebuild lands at spawn, and VR at 90 Hz may drop a frame at spawn (VR work deferred). Recorded in `Docs/TECH_DEBT.md`.
- **R13b router plan-open rule near the horizon is untested by a real route:** no route on 2026-01-01 reaches the horizon, and in v1 it is unreachable (12 hops x 42.7 d < 960 d); only the pure-math case covers it.
- **R14 approximate normalisation** with ship blocks (4.5, O1, accepted).
- **R15 Earth-Moon share:** with the sqrt weights about 12 % in the 2026-01-01 set and a measured 9.9-13.6 % over the
  daily sets (was 23-33 % with the plain product, O6). Since 53c2a the lane ships are shared over the whole slotted
  set (the 53c1 interim of 8 base lanes and about 31 % is gone), and since 53c2b they follow the set as it changes.
- **R16 draw gate at the new pool:** the 0.15 ms tunnel draw gate was set for 100,000 vertices; it is now measured with
  the pool full at up to 369,120 vertices and may miss (reported, not relaxed).
- **R17 a player hop on a Gone lane:** a player finishing a hop on a lane that is no longer chosen still needs the
  lane's frame and profile after the lane leaves the set; its slot stays held (`slotHeld`) until the player's held hop
  ends, and the hop keeps the lane identity (6.4).
- **R18 Sun safety override:** with the override on, the player's AUTO can fly through the danger zone and into the
  Sun's swept collision; the HUD line "SUN SAFETY OFF" is the only reminder, and the flag lasts the session.
- **R20 NPC flight off the lanes:** promoted NPCs (Mid / Full AI tiers) are not given the danger zone by this ticket;
  whether their steering can cross it is not verified (53e checks and reports; a rule for them is a new decision).
- **R19 radial menu with 11 sectors:** the body menu grows to 11 rows; sector width and label fit at 1280x720 are
  checked in the 53e smoke screenshots.

## 15. Change log

- 2026-11-22: first draft from the user's grill-me decisions D1-D8; current state from the code at `9943d8e`; open
  questions Q1-Q30 for the user. No code, no persistence change.
- 2026-11-24: amended with the user's answers to Q1-Q30 (section 16). Changed: partners by current distance, daily
  recalculation, 24 nodes (planets, Moon, 5 dwarf planets, 10 asteroids), population weights, finish-trip closing,
  three tunnel classes, pool 24, knee fix, map route drawing, closed-lane ships promoted. Measured lane counts,
  reachability and the column worst case by script. New open items O1-O10 (section 17). No code, no persistence
  change.
- 2026-11-25: amended with the user's answers to O1-O10 (section 17). Changed: ship blocks decided (4,000 of 100);
  island links to Earth's connected group (4.3); partner hysteresis stepped epoch to epoch with a warm-up after a time
  jump (mechanism and values confirmed by the user 2026-11-25; the partner set is no longer a pure function of sim
  time); the Sun danger zone on every AUTO order with a session-only player override row in the interaction menus
  (6.5); lane weight = sqrt of the tier product (4.4, new share table); partner lane radius, window rule for player
  hops, docking pop and the O10 constants accepted; float path validated at 151 AU. Lane counts, columns, pool and the
  Earth-Moon share re-derived from the baseline (marked as not measured; re-measured in 53c). No code, no persistence
  change.
- 53a/53b review: the Moon relays island links through Earth only (decision A, 4.3, 4.8, 17); tunnel class thresholds
  scale with the QualityPreset by the flame vanish table (decision B, 7.1, open item on the shader fades); latched open
  start for a new or reopened lane (5.4, open item on time jumps); router rejects non-positive or non-finite edge
  times, reports a status (edge cap told apart from unreachable) and pins the current-windows limit (6.3). Scratch
  speed-ups with identical outputs (base-link masks, per-epoch distance matrix, incremental island search). No
  persistence change.

- 2026-11-25: 53c1 review fixes. Performance: the full rebuild went from 39.3 ms to 1.7-2.1 ms and the epoch step from
  0.0265 to 0.0056-0.0077 ms per frame (rings of epoch-start positions and clearance samples, bound-classified lanes,
  shared distance matrix, parallel rebuild, 5.3), and the gates are asserted by `MeasuredCost`. Routing reach (user
  decision): routes ignore windows beyond the 960 d horizon (6.3, R11). Time jumps: the clock counts its sets and the
  NPC subsystem marks a jump (5.3, `NPC_LANE_NETWORK_CATCH_UP_EPOCHS`, `NPC_LANE_NODE_SPEED_MARGIN`,
  `SIM_CLOCK_MAX_WARP`). Final-rule 250-year figures replace the derived ones (4.3, 4.4, 7.3, R6, R7, R15). The ring
  buffer holds the entries `k .. k + H - 1` (5.3). `FSOLNpcGroupTable::LaneSlotCapacity` and `LaneSlotColumn` keep
  schedule slots clear of the cluster and battle columns (53c2 uses them). No persistence change.
- 2026-11-25: the user's answers to the 53c2 decisions P1-P6 (section 18): a U-turn follows the leg after it (5.4,
  design note on station docking), a promoted ship at its leg end is demoted and docked at once (5.4, R12), block
  membership and pool policy as recommended (4.5), traffic activation hides inactive lanes always and its 2.4 px
  threshold scales with `LaneTunnelQualityScale` (7.2, changed from the recommendation), an Approach on a closing lane
  is Stale (5.4). No persistence change.
- 2026-11-25: 53c2b built: the traffic shader window and its CPU mirror (5.4, R8), per-epoch block moves and the
  time-jump re-share (5.4 Share, P4), measured in game (13). The 53c2a interim (held spawn slots, closed lanes still
  drawing their far points) is gone. Open: the in-game per-block gate miss and the opening and time-jump hitches
  (`TECH_DEBT.md`). A developer console command `sol.SimDate <date>` sets the clock for time-jump checks. No
  persistence change.
- 2026-11-25: 53d built: per-lane tunnel classes (Off builds nothing, Reduced 7,220 vertices, Full 15,380), the
  24-instance pool shared by both (overflow logged once per change, stat `SOL_NpcTunnelPoolOverflow`), the knee fix
  (160 ring slots per side), the 3 s real-time open fade on row 3 `.z` with the closing lane's column held until the
  fade ends (4.5), the Q20 bands in pixels and one QualityPreset factor for every pixel threshold (7.1, 7.2, user
  decision), the active-flag early-out of the point shader (7.2). `NPC_TUNNEL_VERTEX_BUDGET` / `_LANES` removed. No
  persistence change.
- 2026-11-25: 53d review fixes (user decisions): the Full switch moved to 10/12 px on and 0.75 px off, both where the
  dots and chevrons are alpha 0 (7.1); a column held only by its tunnel fade is pre-emptible by a new lane (4.5,
  `ESOLLaneSlotHold`); only a block owner out of the set re-runs the block step every frame (a fade hold is re-checked
  once at its end); pool hysteresis `NPC_TUNNEL_POOL_KEEP_RATIO` 1.1 (7.3); the wall label alpha follows the open fade;
  the open-fade step, the hold rule and the per-class instance split are pure functions with tests. No persistence
  change.

## 16. Resolved decisions (user, 2026-11-24)

The draft's recommendation is marked *superseded* where the answer differs.

- **Q1 Next-closest rule: CHANGED.** The partner is chosen by **current distance**, and the lane set is
  **recalculated once per sim day** (the Q5 epoch). *Superseded:* the static `|da|` key computed once at spawn. Design
  consequences: 4.3, 4.5, 5.3, 5.4, 6.3; open items O1-O3, O7.
- **Q2 Earth-Moon: CHANGED scope.** Keep Earth-Moon; the Moon is a node through that lane only. Nodes are the 8
  planets, the Moon, the 5 dwarf planets and the 10 largest belt asteroids (4.2).
- **Q3 Dwarf planets: CHANGED.** Ceres, Pluto, Haumea, Makemake and Eris are nodes, plus the 10 largest belt
  asteroids by diameter (Ceres excluded). *Superseded:* "not nodes in v1".
- **Q4 Importance: CHANGED.** Traffic weights by **population**: node tiers Earth / Moon 10, Mars 6, Ceres 4, belt
  asteroids 3, Venus / Mercury / Jupiter / Saturn 2, Uranus / Neptune / Pluto / Haumea / Makemake / Eris 1; lane
  weight = product of the end tiers, normalised to the same 400,000 lane ships. *Superseded:* hand-written pair
  weights. *Amended by O6:* the weight is the square root of the product. The tier values are confirmed by the user
  (2026-11-25, 4.4).
- **Q5 Epoch:** a fixed 1-day sim epoch with the conservative sampled bound, the state a pure function of sim time.
  *Amended by O3:* the Sun state stays pure; the partner set also depends on the time of the last jump (4.3).
- **Q6 Hysteresis:** close below 20 solar radii, reopen above 22.
- **Q7 Clearance measure:** the minimum distance from the Sun to the straight segment between the body centres.
- **Q8 Drain: CHANGED.** No new departures one leg time before a lane closes (Sun clearance or no longer chosen), so
  every ship in flight finishes its trip and none vanishes. *Superseded:* option (a), ships fading with the tunnel.
- **Q9 Switch fade:** only the tunnel fades, 3 s real time.
- **Q10 Share:** the traffic share stays with the lane.
- **Q11 Router scope:** lane-only routing when the target is a node (or a body whose parent planet is a node) and the
  ship is at a node or in a lane; otherwise today's behaviour; War power unchanged (still ignores lanes). *Amended by
  O4:* War power, like every AUTO order, obeys the Sun safety.
- **Q12 Transfer cost:** arc at wall speed plus a 1 h penalty.
- **Q13 First and last mile:** in the HUD ETA only.
- **Q14 Lane closes during a trip: CHANGED.** Re-plan at the epoch change for hops not yet flown; a held lane that
  closes is finished only if safe; the ship stays out of the Sun's danger zone (AUTO leaves or avoids unsafe
  geometry, 6.5). *Superseded:* "leave the held lane, fly to the end node" in every case, and "current state only".
- **Q15 D20 search while a plan runs:** off.
- **Q16 Route display: CHANGED.** "VIA <node>" on the AUTOPILOT marker **and** the planned route drawn on the system
  map, distinct from the player's waypoint routes and following the moving bodies. *Superseded:* no map drawing.
- **Q17 Tunnel classes: CHANGED.** Three classes (off, reduced, full) with pixel hysteresis; thresholds in 7.1 (the
  0.4 / 1.0 px values accepted with O10; the Full switch moved to 10/12 px on and 0.75 px off in the 53d review, so
  it is invisible). *Superseded:* two classes.
- **Q18 Pool: CHANGED.** 24 instances (369,120 vertices at most). *Superseded:* 16.
- **Q19 Knee:** fixed now: 160 ring slots per side, 15,380 vertices per lane.
- **Q20 Legacy controls:** restated in reference px (7.2); the 4-label cap kept and recorded in `TECH_DEBT.md` as a
  count control.
- **Q21 Traffic activation granularity:** a per-lane active flag in the group texel.
- **Q22 Traffic pixel metric:** lane radius at the nearest point >= 2.4 reference px, or a CPU-tier ship of the lane
  exists. *Amended by P5 (section 18):* the 2.4 px scales with the QualityPreset and inactive lanes are hidden always.
- **Q23 Lane order:** new lanes after the existing 8 (as slots keyed by node pair, 4.5).
- **Q24 STATUS.md wording:** row 3b5d to read "pixel on/off, Sun clearance"; a 53a task (not edited yet).
- **Q25 Gates:** the #52 gates plus the new CPU gates (section 13).
- **Q26 Closed lanes' ships and the AI tiers: CHANGED.** Promoted as usual (interplay with finishing trips in 5.4).
  *Superseded:* not promoted while closed.
- **Q27 Flame gate:** re-measured in 53f, no fix.
- **Q28 Group columns:** keep 64 as a hard pool, report if hit; the real worst case is in 4.3.
- **Q29 NPC tier radii and budgets:** legacy distance / count debt, out of scope; already recorded in `TECH_DEBT.md`
  (the legacy NPC LOD item), cross-referenced there for the lane ships.
- **Q30 Merge readout on a closed lane:** hidden.

## 17. Resolved open items (user, 2026-11-25)

The open items O1-O10 raised by the Q answers, as answered by the user. The tier values (O6), the hysteresis values
(O3) and the Sun-safety override UI (O4) were all confirmed by the user on 2026-11-25; the Moon-as-relay reversal (O2)
was itself replaced at the 53a/53b review by "the Moon relays through Earth only" (user decision A).

- **O1 Ship budget: proposal accepted.** Fixed ship blocks (4,000 of 100 ships; membership by contiguous index, P3, section 18) moved only through a free
  pool (4.5); exact normalisation at spawn, approximate after (R14).
- **O2 Islands: option (b), extended.** Each island (a connected component without Earth) is linked to its nearest
  node in Earth's connected group, one link per island, processed nearest first; any body already in Earth's group
  may act as the relay, the large asteroids and dwarf planets included (4.3), **but not the Moon: it relays through
  Earth only** (user decision A at the 53a/53b review, replacing the 2026-11-25 "the Moon can be a relay"; the island
  link is Earth's, its length from Earth's and the island node's aphelia, and the Moon shows as a stop on Earth). Stations and space cities as relays stay
  a later design hook, nothing built (4.8). Effect (derived, re-measured in 53c): 0-5 links, about 22-34 lanes (38 at
  most), about 60 of 64 group columns (64 at the bound), pool of 24 not expected to fill (about 13 lanes above 0.5 px
  at most), every node in Earth's graph every day (R11 reduced), longest possible lane 150.38 AU (about 897 d).
- **O3 Churn: partner hysteresis.** A node switches partner only when the new nearest is clearly closer (margin) and
  it has kept its partner for a minimum stay (4.3). Mechanism: each epoch's picks step from the previous epoch's, with a 64-epoch warm-up after a time jump, so the
  partner set depends on the sim time and the time of the last jump (no longer a pure function of sim time; nothing
  saved). Values **confirmed by the user, 2026-11-25**: switch ratio 0.8 (the new partner at least 20 % closer),
  minimum stay 30 epochs (days), warm-up 64 epochs.
- **O4 Sun danger zone: 20 solar radii on all AUTO flight, with a detour; player override.** Every AUTO order of the
  player keeps out of the zone with the detour of 6.5 (War power included); NPC lanes close at the zone (5.4). The
  player can override the safety with a toggle row ("Toggle Sun Safety", state shown by a "SUN SAFETY OFF" HUD line)
  in the interaction menus (radial and list) of the Star, Planet, Moon and Dwarf rows, next to Move To (War Power);
  session-only, effective at once, no schema or key-binding change (`MAX_ACTIONS` 10 -> 11); a target inside the
  zone stops at the zone edge. All confirmed by the user, 2026-11-25. Saving it across launches
  would need the user's approval (`SAVE_VERSION` 14). NPCs never override.
- **O5 Partner lane radius: proposal accepted.** The larger region radius of the two ends; belt 4e6 m (4.4).
- **O6 Weights: CHANGED.** Lane weight = sqrt(tier A x tier B) (4.4). Earth-Moon: 12.0 % of lane ships in the
  2026-01-01 set (31.2 % with the base lanes only), estimated 9-14 % over the daily sets. The tier values (Earth /
  Moon 10, Mars 6, Ceres 4, asteroids 3, Venus / Mercury / Jupiter / Saturn 2, Uranus / Neptune and the other dwarf
  planets 1) are confirmed by the user (2026-11-25).
- **O7 Player hops: proposal accepted.** The player's AUTO starts a hop only if it fits the lane's window (6.3).
- **O8 Float path: proposal accepted.** Re-validated in 53c, now at 151 AU since island links can reach 150.38 AU
  (4.7); if it fails, 53c stops and the fix is the user's call.
- **O9 Docking pop: accepted** (R12), no fade.
- **O10 Constants: accepted.** Sun look-back 64 epochs, horizon 960 epochs (above the 897 d longest leg), ship blocks
  of 100, tunnel class thresholds 0.4 px (Reduced -> Off) and 1.0 px (Reduced -> Full; moved to 10/12 px, Full off
  0.75 px, by the user in the 53d review, 7.1).

## 18. Resolved 53c2 decisions (user, 2026-11-25)

The six decisions P1-P6 left open by the 53c2 contract (`Docs/Plans/53-lane-network-contract.md`, "Resolved
decisions"), as answered by the user. Every assertion of contract cases 16-26 is pinned by them.

- **P1 U-turn window: the leg after the turn decides.** A ship in a U-turn is drawn iff the leg after the turn fits
  the window (`LegFitsWindow(turn end, T_leg, open, close)`), so a ship that arrives at a node where its next leg is
  not flown vanishes at the end ring on arrival (5.4, R12). **Design note:** this matches the future goal of ships
  docking at a station at the lane's end: the arrival at the end ring becomes the docking moment, so no turn is drawn
  for a ship that will not fly on.
- **P2 Promoted ship at its leg end: demoted and docked at once.** A Mid / Full lane ship whose leg ends where the
  next leg is not flown drops to Far and docks the same frame, no fade (O9, R12). It does not keep flying its rail on
  a closed or Sun-unsafe lane.
- **P3 Block membership: as recommended.** Block `b` = the lane-local ship indices `[100 b, 100 b + 100)`
  (contiguous; a block move is one contiguous rail range, the spread along the lane comes from each ship's hashed
  phase); block count = ceil(lane ships / 100), the last block partial (4.5).
- **P4 Pool policy: as recommended.** (a) An opening lane's target is its `LaneShipShares` block share over the
  current set's slotted lanes; (b) when the free pool is short, slots take in ascending slot order; (c) blocks join a
  lane only at its episode open (no top-up mid-episode, so no ship appears mid-leg); (d) a time jump re-shares every
  block exactly, as at spawn (one full rail upload per jump).
- **P5 Traffic activation scope: CHANGED from the recommendation.** Inactive lanes are hidden **always**, with the
  Lanes overlay on or off, and the 2.4 px threshold **scales with the QualityPreset** by `LaneTunnelQualityScale` (the
  tunnel classes' scale: Low 24 px, Medium 11.04 px, High 5.28 px, Epic and Cinematic 2.4 px). *Superseded
  recommendation:* active whenever the overlay is off, no quality scale. The shader reads the flag in 53d (7.2).
- **P6 AUTO latch on a closing lane: as recommended.** An Approach on a lane that closes is Stale (dropped, searched
  again on the next search); a Hold on a closed lane is unchanged until 53e's finish-if-safe (5.4).
