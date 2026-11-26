<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# 53a contract: lane network math (test first)

For the 53a test author, from [SDD 53](../SDDs/53-lane-network.md) (D1-D8, the resolved decisions Q1-Q30 of section
16 and O1-O10 of section 17, amended 2026-11-25). The O3 hysteresis values and the O6 tier values were
**confirmed by the user on 2026-11-25** and are pinned, like every other value. Pure functions only: signatures and expected values, no
implementation. Tests in `Source/SOLTest/Tests/LaneNetworkMathTest.cpp`, paths `SOLTest.LaneNetwork.<Case>` (run with
`Tools\RunTests.bat -Build -Filter SOLTest.LaneNetwork`). Everything lives in `Traffic/SOLLaneNetworkMath.{h,cpp}`
(`namespace SOLLaneNetwork`, every function `SOLTEST_API`) and `SOLConstants.h` (`namespace SOL`). Universe ecliptic
metres, sim seconds, double. Inputs are plain arrays; nothing reads the registry or the belt catalogue.

## Constants (`SOLConstants.h`)

| Name | Type | Value | Status |
|---|---|---|---|
| `NPC_LANE_SUN_CLEARANCE_RADII` | double | 20 | pinned (D5, Q6) |
| `NPC_LANE_SUN_REOPEN_RADII` | double | 22 | pinned (Q6); invariant: > `NPC_LANE_SUN_CLEARANCE_RADII` |
| `NPC_LANE_NETWORK_EPOCH_S` | double | 86,400 | pinned (Q1, Q5); invariant: > 0 |
| `NPC_LANE_NETWORK_EPOCH_SAMPLES` | int32 | 5 | implementer; invariant: >= 2 |
| `NPC_LANE_SUN_LOOKBACK_EPOCHS` | int32 | 64 | pinned (O10); invariant: >= 1 |
| `NPC_LANE_NETWORK_HORIZON_EPOCHS` | int32 | 960 | pinned (O10); invariant: x `NPC_LANE_NETWORK_EPOCH_S` > 897 days (the longest possible leg, Eris-Makemake 150.38 AU, SDD 53 4.3, 6.1) |
| `NPC_LANE_PARTNER_SWITCH_RATIO` | double | 0.8 | confirmed (user 2026-11-25, O3); invariant: 0 < it < 1 |
| `NPC_LANE_PARTNER_MIN_STAY_EPOCHS` | int32 | 30 | confirmed (user 2026-11-25, O3); invariant: >= 1 |
| `NPC_LANE_PARTNER_WARMUP_EPOCHS` | int32 | 64 | confirmed (user 2026-11-25, O3); the warm-up after a time jump; invariant: > `NPC_LANE_PARTNER_MIN_STAY_EPOCHS` |
| `NPC_LANE_BASE_COUNT` | int32 | 8 | pinned (Q23): base lanes own slots 0-7 |
| `NPC_LANE_OPEN_FADE_REAL_S` | double | 3.0 | pinned (Q9), real seconds; invariant: > 0 |
| `NPC_LANE_TRANSFER_PENALTY_S` | double | 3,600 | pinned (Q12); invariant: >= 0 |
| `NPC_LANE_SHIP_BLOCK_SIZE` | int32 | 100 | pinned (O1, O10); invariant: divides the lane ship count of the default population (400,000) |
| `NPC_TUNNEL_ACTIVATE_PX` | double | 0.5 | pinned (Q17); invariant: equals `NPC_TUNNEL_RADIUS_FADE_GONE_PX` |
| `NPC_TUNNEL_DEACTIVATE_PX` | double | 0.4 | pinned (O10); invariant: 0 < it < `NPC_TUNNEL_ACTIVATE_PX` |
| `NPC_TUNNEL_FULL_ON_PX` | double | `NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX / NPC_TUNNEL_RING_BASE_SPACING_RADII` = 10 / 12 | pinned (user, 53d review: dots and chevrons are alpha 0 at and below it, so the switch is invisible; was 1.0); invariant: > `NPC_TUNNEL_FULL_OFF_PX` |
| `NPC_TUNNEL_FULL_OFF_PX` | double | 0.75 | pinned (user, 53d review; was 10 / 12); invariant: > `NPC_TUNNEL_ACTIVATE_PX` |
| `NPC_TUNNEL_RING_SLOTS_PER_SIDE` | int32 | 160 | pinned (Q19; was 80); full slot mesh 15,380 vertices |
| `NPC_TUNNEL_INSTANCE_POOL` | int32 | 24 | pinned (Q18); invariant: <= `NPC_GROUP_TEXTURE_WIDTH` |
| `NPC_LANE_ROUTE_MAX_HOPS` | int32 | 12 | implementer; invariant: >= 8 |
| `NPC_LANE_NETWORK_CATCH_UP_EPOCHS` | int32 | 16 | implemented in 53c1; a forward clock move of up to this many epochs is stepped one by one, more, backward or a marked clock set rebuilds; invariant: 1 <= it < `NPC_LANE_NETWORK_HORIZON_EPOCHS`, and it x `NPC_LANE_NETWORK_EPOCH_S` >= `MAX_FRAME_DELTA_S` (0.5 s) x `SIM_CLOCK_MAX_WARP` (`static_assert`: one clamped frame at the top warp moves at most 15 epochs and always steps) |
| `NPC_LANE_NODE_SPEED_MARGIN` | double | 1.1 | implemented in 53c1; node speed bound = perihelion speed x this (the Moon adds Earth's), covers secular drift, used by the Sun clearance bound; invariant: >= 1 |
| `SIM_CLOCK_MAX_WARP` | double | 2,592,000 | implemented in 53c1; the clock's top warp step, 30 d/s, `static_assert`-tied to the warp table |

Existing constants used (not changed): `SUN_RADIUS_M` 6.957e8, `NPC_GROUP_TEXTURE_WIDTH` 64,
`NPC_LANE_AUTO_TRACK_WALL_RADII` 0.25, `NPC_LANE_EFFORT_MEAN` 0.75, `NPC_LANE_WALL_SPEED_MPS` 2e4,
`NPC_LANE_SPEED_CAP_MPS` 3e5, `NPC_LANE_FLIP_S` 60, `NPC_LANE_PEAK_ACCEL_MPS2` (1.65 G), `NPC_TUNNEL_END_MARGIN_RADII`
3, `NPC_TUNNEL_RADIUS_FADE_GONE_PX` 0.5, `NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX` 10, `NPC_TUNNEL_RING_BASE_SPACING_RADII`
12.

## Types

```cpp
enum class ESOLLaneNodeKind : uint8 { Planet, DwarfPlanet, Moon, Asteroid, Station, City };

struct FSOLLaneNode
{
    ESOLLaneNodeKind Kind = ESOLLaneNodeKind::Planet;
    int32 BodyIndex = INDEX_NONE;
    int32 SourceIndex = INDEX_NONE;
    int32 ParentNode = INDEX_NONE;
    double ClearanceRadiusM = 0.0;
    double AphelionM = 0.0;
    double RegionRadiusM = 0.0;
    double PopulationTier = 0.0;
};

struct FSOLLanePair { int32 NodeA = INDEX_NONE; int32 NodeB = INDEX_NONE; };   // NodeA < NodeB; the lane identity

struct FSOLLaneEdge
{
    int32 NodeA = INDEX_NONE;
    int32 NodeB = INDEX_NONE;
    double TimeS = 0.0;                          // T_edge, same both ways
    double OpenStartS = -INFINITY;               // the lane's current open window (SDD 53 5.4)
    double CloseS = INFINITY;
    FVector3d DirectionAB = FVector3d::ZeroVector;   // unit, NodeA -> NodeB
    double EndRingDistanceAM = 0.0;              // end-ring distance from each node's centre
    double EndRingDistanceBM = 0.0;
};

struct FSOLLaneHop { int32 Lane = INDEX_NONE; int32 DirectionSign = 1; };   // +1 = NodeA -> NodeB

enum class ESOLLaneRouteStatus : uint8 { Found, Unreachable, TooManyHops, TooManyEdges, InvalidEndpoint };

struct FSOLLaneRoute
{
    bool bFound = false;
    ESOLLaneRouteStatus Status = ESOLLaneRouteStatus::Unreachable;
    double TimeS = 0.0;               // sum of edge times and transfer costs
    int32 HopCount = 0;
    FSOLLaneHop Hops[SOL::NPC_LANE_ROUTE_MAX_HOPS];
};

enum class ESOLLaneSunState : uint8 { Open, Closed };
enum class ESOLTunnelClass : uint8 { Off, Reduced, Full };

struct FSOLPartnerPick { int32 Partner = INDEX_NONE; int32 StayEpochs = 0; };   // one node's hysteresis state (O3)
```

## Functions and expected values

### 1. `NextClosestPairs` (case `NextClosest`)

```cpp
void NextClosestPairs(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
    TConstArrayView<FSOLLanePair> baseLanes, TArray<FSOLLanePair>& outNew);
```

Nodes of kind Planet, DwarfPlanet and Asteroid take part (both ends); Moon, Station and City never pick and are never
picked. Each taking-part node picks the node with the smallest **current** centre distance among the taking-part nodes
not linked to it by a base lane; ties to the lower index; skipped if none. A node with a non-finite position neither
picks nor is picked. Each pair is stored once with `NodeA < NodeB`, and `outNew` is **sorted by (NodeA, NodeB)**, not
by pick order. `outNew` is reset first. A pair is in the output if **either** end picks the other. Positions in units
of 1e11 m on the x axis unless stated; all nodes Planet unless stated. `NextClosestPairs` is the plain rule without
hysteresis; an epoch's lane set is the base lanes, `PairsFromPicks` of the epoch's partner picks (1b) and
`IslandLinks` (1c) over those.

- x {1, 2, 3, 4}, base {0-1, 1-2, 2-3}: {(0, 2), (1, 3)} (both picked twice).
- x {1, 2, 3}, base {0-1, 1-2, 0-2}: empty.
- x {1, 3, 5}, base {}: {(0, 1), (1, 2)} (node 1 ties 0 and 2, picks 0; (1, 2) is in only because node 2 picks 1).
- **Current distance, not the orbit radius:** node 0 at (1, 0, 0), node 1 at (-1.2, 0, 0), node 2 at (3, 0, 0), base {}:
  {(0, 1), (0, 2)} (node 0 picks 2 at 2.0 over node 1 at 2.2, though node 1's orbit radius is closer to its own).
- **Kinds:** node 0 Planet at x 1, node 1 Asteroid at 1.1, node 2 Moon at 1.0001, node 3 DwarfPlanet at 5, base {}:
  {(0, 1), (1, 3)} (the Moon is never picked; the dwarf picks the asteroid at 3.9 over the planet at 4.0).
- Node 1 position NaN in the first case (x {1, NaN, 3, 4}, same base): node 1 is skipped both ways; node 0 picks 2,
  node 2 picks 0, node 3 picks 0 (its only candidate left): {(0, 2), (0, 3)}.
- Positions array shorter than nodes: `outNew` empty.

### 1b. `NearestPartners`, `PairsFromPicks`, `StepPartnerPick`, `ReplayPartnerPicks` (case `PartnerHysteresis`, O3)

```cpp
void NearestPartners(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
    TConstArrayView<FSOLLanePair> baseLanes, TArray<int32>& outNearest);   // per node; INDEX_NONE if none
void PairsFromPicks(TConstArrayView<int32> partnerByNode, TArray<FSOLLanePair>& outPairs);
FSOLPartnerPick StepPartnerPick(const FSOLPartnerPick& previous, int32 nearest, double nearestDistanceM,
    double heldDistanceM, int32 minStayEpochs, double switchRatio);
void ReplayPartnerPicks(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsOldestFirstM,
    int32 epochCount, TConstArrayView<FSOLLanePair> baseLanes, int32 minStayEpochs, double switchRatio,
    TArray<FSOLPartnerPick>& outPicks);
```

- `NearestPartners`: each node's nearest by the rules of case 1 (taking-part kinds, base lanes excluded, ties to the
  lower index, NaN nodes skipped both ways); `outNearest` has one entry per node, reset first. `PairsFromPicks`: the
  union of `(n, partner)` over nodes with a partner, sorted pairs, stored once, sorted by (NodeA, NodeB), reset first.
  **Identity:** for every input of case 1, `NextClosestPairs` equals `PairsFromPicks(NearestPartners(...))`.
- `StepPartnerPick`, in this order: `nearest` INDEX_NONE -> {INDEX_NONE, 0}; no held partner (`previous.Partner`
  INDEX_NONE) or `heldDistanceM` not finite (the held partner is no longer a valid candidate) -> {nearest, 1};
  `nearest == previous.Partner` -> {partner, stay + 1}; `previous.StayEpochs >= minStayEpochs` and `nearestDistanceM
  < switchRatio x heldDistanceM` -> {nearest, 1}; otherwise {partner, stay + 1}. Values with min stay 30, ratio 0.8
  (passed as arguments, so the cases do not depend on the pending constants):
  - ({INDEX_NONE, 0}, 5, 2.0, NaN): {5, 1}.
  - ({3, 10}, 3, 2.0, 2.0): {3, 11}.
  - ({3, 10}, 5, 1.0, 2.0): {3, 11} (50 % closer but inside the minimum stay).
  - ({3, 30}, 5, 1.0, 2.0): {5, 1}.
  - ({3, 30}, 5, 1.7, 2.0): {3, 31} (only 15 % closer).
  - ({3, 30}, 5, 1.6, 2.0): {3, 31} (exactly the ratio, not below).
  - ({3, 30}, 5, 1.59, 2.0): {5, 1}.
  - ({3, 5}, 5, 1.0, NaN): {5, 1} (held partner invalid: switch at once, whatever the stay).
  - ({3, 5}, INDEX_NONE, NaN, 2.0): {INDEX_NONE, 0}.
  - Min stay 0: ({3, 0}, 5, 1.0, 2.0): {5, 1}.
- `ReplayPartnerPicks` (the warm-up after a time jump, SDD 53 4.3; in normal running each epoch's picks are one
  `StepPartnerPick` per node from the previous epoch's): `positionsOldestFirstM` holds `epochCount` epochs of all
  nodes, epoch `e`'s node `n` at index `e x nodes.Num() + n`. Every node starts with {INDEX_NONE, 0} and steps through
  epochs 0 .. `epochCount - 1` with `StepPartnerPick`, its nearest from `NearestPartners` at that epoch and the held
  distance = the centre distance to the held partner at that epoch (NaN if either position is not finite or the held
  partner is not a taking-part node). `outPicks` = the picks after the last epoch, one per node (non-taking-part nodes
  {INDEX_NONE, 0}), reset first. `epochCount < 1` or positions shorter than `epochCount x nodes.Num()`: `outPicks`
  empty.
  - 3 Planet nodes, base {}, min stay 2, ratio 0.8, x positions (1e11 m) per epoch: e0 {0, 1, 3}; e1, e2, e3 {0, 1,
    0.5}.
    - `epochCount` 1 (e0): picks {1, 1}, {0, 1}, {1, 1}; pairs {(0, 1), (1, 2)}.
    - `epochCount` 2 (e0-e1): {1, 2}, {0, 2}, {1, 2}; pairs {(0, 1), (1, 2)} (node 0's nearest is now 2 at 0.5,
      but its stay is 1 < 2). Node 2's nearest at e1 ties 0 and 1 at 0.5 and goes to 0.
    - `epochCount` 3 (e0-e2): {2, 1}, {2, 1}, {1, 3}; pairs {(0, 2), (1, 2)} (nodes 0 and 1 switch: 0.5 < 0.8 x 1;
      node 2 keeps 1: 0.5 is not below 0.8 x 0.5).
    - `epochCount` 4 (e0-e3): {2, 2}, {2, 2}, {1, 4}; pairs {(0, 2), (1, 2)}.
  - **Equivalence:** for the series above, the result equals a hand-written chain of `StepPartnerPick` calls from
    {INDEX_NONE, 0} over the same epochs.
  - **Consecutive epochs one step apart:** for each n from 1 to 3, the result over e0 .. e(n) equals one
    `StepPartnerPick` per node (nearest and held distance at e(n)) from the result over e0 .. e(n-1). This is the rule
    the look-ahead buffer follows from epoch to epoch.
  - **Start dependence (documented, not a defect):** the same positions replayed from e1 instead (e1-e3, `epochCount`
    3) give {2, 3}, {2, 3}, {0, 3}: node 2 holds 0, not 1, so a warm-up start changes picks; this is why the warm-up
    runs only after a time jump and the buffer steps otherwise (SDD 53 4.3).
  - **Determinism:** two calls with the same inputs give bit-identical output.
- `bool StepPartnerPicks(nodes, positionsM, baseLanes, minStayEpochs, switchRatio, TArray<FSOLPartnerPick>&
  inOutPicks)` (added in 53c, the look-ahead buffer's step): one `StepPartnerPick` per taking-part node at
  `positionsM`, nearest and held distance as in the replay; picks of a size other than `nodes.Num()` start as
  {INDEX_NONE, 0}; positions shorter than the nodes: false, picks untouched. For the series above, the step from the
  replay over e0 .. e(n-1) equals the replay over e0 .. e(n) (n 1 to 3), and from no picks it equals a one-epoch
  replay. `ReplayPartnerPicks` is built on the same step, so a replay equals a chain of steps bit for bit.

### 1c. `IslandLinks` (case `IslandLinks`, O2)

```cpp
void IslandLinks(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FVector3d> positionsM,
    TConstArrayView<FSOLLanePair> lanes, int32 rootNode, TArray<FSOLLanePair>& outLinks);
```

`lanes` = every lane of the epoch's set (base and partner, Sun-closed ones included). The root group = the nodes
connected to `rootNode` through `lanes`. Candidates = nodes of kind Planet, DwarfPlanet or Asteroid with finite
positions. A Moon is **never** an end of an island link, neither u nor v (user 2026-11-25, final: the Moon relays
through Earth only; a route to an island from the Moon is Moon -> Earth -> island, the island link is Earth's, its
length from Earth's and the island node's aphelia, and the Moon shows as a stop on Earth). Repeat: among all pairs (u in the root group, v outside it,
both candidates), take the smallest centre
distance (ties to the lower sorted pair); add the sorted pair to the links and merge v's whole component (through
`lanes` and the links so far) into the root group; until no candidate is outside the root group. So each island
(component) gets exactly one link and any candidate already in the root group, one joined earlier included, can be
the relay. `outLinks` is reset first and sorted by (NodeA, NodeB). `rootNode` out of range or positions shorter than
nodes: empty. Positions in units of 1e11 m on the x axis; all nodes Planet unless stated.

- x {1, 2, 5, 6, 10, 11}, node 3 Asteroid, lanes {0-1, 2-3, 4-5}, root 0: {(1, 2), (3, 4)} (3 components, 2 links;
  the second link relays through asteroid 3, joined by the first).
- **Nearest first:** x {0, 10, 4}, lanes {}, root 0: {(0, 2), (1, 2)} (node 2 at 4 joins first, then node 1 links to
  it at 6 rather than to node 0 at 10).
- **Tie:** x {1, 3, -1}, lanes {}, root 0: {(0, 1), (0, 2)} ((0, 1) and (0, 2) tie at 2, the lower pair first; then
  (0, 2) at 2 beats (1, 2) at 4).
- **Moon:** node 1 Moon at x 1.0001, node 0 at x 1, lanes {}, root 0: empty (a Moon outside the root group is never
  linked as v). **Moon via Earth:** node 0 (Earth) at x 1, node 1 Moon (parent 0) at x 5, node 2 at x 5.1, lanes
  {0-1}, root 0: {(0, 2)} (the Moon is nearer to node 2 but never relays; the link is Earth's). Same with node 2 at x
  0.5: {(0, 2)}. **Moon / relay tie** (corrected in the 53a/53b review): node 0 Moon (parent 1) at x 5, node 1 Earth
  at x 1 as the root, node 2 at x 3, lanes {0-1}, root 1: node 2 is 2 from both, a Moon relay would win the tie as the
  lower pair (0, 2), the rule gives {(1, 2)}. Asserted in the test file.
- **NaN:** x {1, 2, NaN}, lanes {}, root 0: {(0, 1)} (node 2 neither linked nor counted).
- **Connected:** x {1, 2, 3}, lanes {0-1, 1-2}: empty. Root out of range: empty.
- Purity: two calls with the same inputs give identical output.

### 2. `LaneNetworkEpochIndex`, `LaneEpochSampleTimeS`, `AssignLaneSlots` (case `DailyRecalc`)

```cpp
int64 LaneNetworkEpochIndex(double simTimeS, double epochS);
double LaneEpochSampleTimeS(int64 epochIndex, double epochS);
int32 AssignLaneSlots(TConstArrayView<FSOLLanePair> previousSlots, TConstArrayView<uint8> slotHeld,
    TConstArrayView<FSOLLanePair> partnerSet, int32 baseCount, int32 slotCapacity, TArray<FSOLLanePair>& outSlots);
                                             // returns the number of partner lanes that found no slot
```

- Index = floor(t / epoch): (0, 86400) -> 0; (86399.9, 86400) -> 0; (86400, 86400) -> 1; (-1, 86400) -> -1;
  (3.15576e9, 86400) -> 36525; epoch <= 0 or NaN input: 0. Sample time = index x epoch: (36525, 86400) -> 3.15576e9.
- **Daily stability:** for any two times in the same epoch the index, and so the sample time, is identical; so
  `NextClosestPairs` and `IslandLinks` fed the positions at the sample time, and `StepPartnerPick` fed the same
  previous picks, return the identical result (assert with two times 1 s and 86,399 s into one epoch). Purity: two
  calls with the same inputs return bit-identical results. (The partner picks also depend on the previous epoch's
  picks, so the set is not a function of sim time alone; SDD 53 4.3.)
- `AssignLaneSlots`: `outSlots` has `slotCapacity` entries; slots below `baseCount` are copied from `previousSlots`
  untouched; a partner lane present in `previousSlots` and in `partnerSet` keeps its slot; a slot whose lane left the
  set is freed unless `slotHeld` is 1 (Blocks) or 2 (Fade, its tunnel still fading) there (`ESOLLaneSlotHold`, 53d
  review); the remaining new lanes, in `partnerSet` order, take the lowest free slot from `baseCount`, else the lowest
  Fade-held slot whose lane left the set (pre-empted, its fade cut short), never a Blocks-held one; a free slot is
  `{INDEX_NONE, INDEX_NONE}`.
  - Capacity 12, base 8, previous slot 8 = (0, 2), slot 9 = (1, 3), held none, set {(1, 3), (2, 5), (4, 6)}: slot 8 =
    (2, 5), 9 = (1, 3), 10 = (4, 6), 11 free; returns 0.
  - Same with slot 8 held: 8 = (0, 2) (held), 9 = (1, 3), 10 = (2, 5), 11 = (4, 6); returns 0.
  - Same with slot 8 held and capacity 10: 8 = (0, 2), 9 = (1, 3); returns 2 (the overflow, reported by the caller).
  - The same inputs twice: identical output (no hidden state).

### 3. `SunSegmentDistanceM` (case `SunClearance`)

```cpp
double SunSegmentDistanceM(const FVector3d& aM, const FVector3d& bM);   // Sun at the origin
```

- A (1e11, 5e9, 0), B (-1e11, 5e9, 0): 5e9.
- A (1e11, 0, 0), B (2e11, 0, 0): 1e11 (endpoint A).
- A (1e11, 0, 0), B (-1e11, 0, 0): 0.
- A == B = (3e10, 4e10, 0): 5e10.
- A (1e11, 0, 0), B (0, 1e11, 0): 1e11 / sqrt(2) = 7.0710678e10 (rel 1e-12).
- Any NaN or infinite input: NaN.

### 4. `StepSunState`, `SunStateFromLookBack` (case `SunHysteresis`)

```cpp
ESOLLaneSunState StepSunState(ESOLLaneSunState previous, double clearanceM, double sunRadiusM);
ESOLLaneSunState SunStateFromLookBack(TConstArrayView<double> boundsNewestFirstM, double sunRadiusM);
```

`StepSunState`: Open -> Closed when `clearance < 20 x sunRadius`; Closed -> Open when `clearance > 22 x sunRadius`;
otherwise unchanged; NaN clearance: Closed (fail safe).
- Open, 1.39e10, `SUN_RADIUS_M`: Closed (threshold 1.3914e10).
- Open, 1.3914e10: Open (not below).
- Closed, 1.45e10: Closed (under 22 x 6.957e8 = 1.53054e10).
- Closed, 1.54e10: Open.
- Open, NaN: Closed.

`SunStateFromLookBack` (the pure form, SDD 53 5.3): walk from index 0 (the current epoch) back; the first bound below
`20 x sunRadius` gives Closed, the first above `22 x sunRadius` gives Open, a bound inside the band continues; empty,
exhausted or a NaN reached: Closed.
- {1.6e10}: Open. {1.45e10, 1.6e10}: Open. {1.45e10, 1.3e10}: Closed. {1.45e10, 1.45e10}: Closed (exhausted). {}:
  Closed. {NaN, 1.6e10}: Closed.
- **Equivalence:** for the series oldest to newest {1.6e10, 1.45e10, 1.3e10, 1.45e10, 1.6e10}, `StepSunState` from Open
  gives Open, Open, Closed, Closed, Open, and `SunStateFromLookBack` on each newest-first prefix gives the same five
  states.

### 5. `EpochClearanceBoundM` (case `EpochBound`)

```cpp
double EpochClearanceBoundM(TConstArrayView<double> sampleClearancesM, double maxEndpointSpeedMps,
    double sampleSpacingS);
```

- Bound = min(samples) - speed x spacing / 2. {2e10, 1.5e10, 1.8e10}, 5.9e4, 21,600: 1.5e10 - 6.372e8 = 1.43628e10.
- Empty samples, NaN anywhere, speed or spacing negative: -inf (closed, fail safe).

### 6. `LaneOpenWindow`, `LegFitsWindow`, `LaneDepartureStopS`, `LaneShipLegDrawn` (cases `EarlyStop`, `FinishTrip`)

```cpp
bool LaneOpenWindow(TConstArrayView<uint8> openByEpoch, int64 firstEpoch, double epochS, double simTimeS,
    double latchedOpenStartS, double& outOpenStartS, double& outCloseS);
bool LegFitsWindow(double departS, double legS, double openStartS, double closeS);
double LaneDepartureStopS(double closeS, double legS);
bool LaneShipLegDrawn(double simTimeS, double legDepartS, double legS, double openStartS, double departStopS);
```

`LaneOpenWindow` (the look-ahead schedule, index 0 = `firstEpoch`): false (outputs NaN) if `simTimeS`'s epoch is
outside the buffer or closed. Otherwise `outOpenStartS` = the start of the run of open epochs that holds it and
`outCloseS` = the start of the first closed epoch after it, or **the end of the buffer if none (fail safe: the horizon
counts as a close)**. When the run reaches the buffer's first entry its start is not in the buffer, so it is
**latched** (53a/53b review): `latchedOpenStartS` is the open start the caller held for this lane at the previous
epoch (-inf allowed: open since before anything known); NaN means the lane was not open then (a new lane or a reopen),
so the run opens at the first entry, `firstEpoch x E` (= k E), and no ship appears mid-leg. A latch later than the
first entry is clamped to it. A run starting inside the buffer ignores the latch.
- E = 86400, first 100, flags {1, 1, 0, 0, 1, 1, 1} (epochs 100-106): t = 100.5 E, latch -inf -> true, -inf, 102 E;
  latch 90 E -> 90 E, 102 E; latch NaN (new lane) at t = 100 E or 101.9 E -> 100 E, 102 E; latch 101 E -> 100 E.
  t = 104.2 E (any latch) -> true, 104 E, 107 E (horizon). t = 102.5 E -> false. t = 99 E -> false. t = 107 E -> false.
- New lane: with the window from latch NaN at t = 100.5 E and leg 0.5 E, a leg departed at 99.8 E is not drawn at
  100.1 E, one departed at 100 E is.

`LegFitsWindow`: `depart >= openStart && depart + leg <= close`; NaN anywhere or `leg < 0`: false.
- (70, 30, 0, 100): true. (70.1, 30, 0, 100): false. (-5, 30, 0, 100): false. (10, 30, -inf, 40): true. (0, 30, 0,
  20): false (window shorter than the leg).

`LaneDepartureStopS` = close - leg: (100, 30) -> 70; (inf, 30) -> inf.

`LaneShipLegDrawn`: true iff `openStart <= legDepart <= departStop` and `legDepart <= t < legDepart + leg`.
- **Finish trip:** open 0, departStop 70, leg 30: depart 70 is drawn at t = 70, 85 and 99.999 (it finishes after the
  stop, by the close at 100), not at 100. Depart 70.1: never drawn (t = 80: false). Depart 20: drawn at 20 and 49.9.
- **Window shorter than the leg:** open 0, close 20, leg 30, so departStop = -10: no departure in [0, 20] is drawn (t =
  5, depart 0: false).
- **No ship vanishes mid-leg:** for departs 0, 10, ..., 70 and t stepped by 0.5 over each leg, drawn is constant true
  from depart to depart + leg.

### 7. `LaneEdgeTimeS` (case `EdgeTime`)

```cpp
double LaneEdgeTimeS(const FSOLLaneProfile& profile);
```

Identities (the first exact, relative 1e-12; the second relative 1e-6, since `MetricMapInverseM` is iterative; the
identities are authoritative, the values below were computed by a script re-implementing `MakeLegProfile` as written
in `SOLLaneTrafficMath.cpp`):
- `== SOLLaneTraffic::MakeLegProfile(SOLLaneTraffic::RadialFactor(NPC_LANE_AUTO_TRACK_WALL_RADII),
  NPC_LANE_EFFORT_MEAN, profile.NominalLegM).LegS`.
- `== SOLLaneTraffic::LaneLegTimeS(profile, frame, 0, frame.CurrentLegM, NPC_LANE_AUTO_TRACK_WALL_RADII, d)` for
  `d` = +1 and -1 and frames from `MakeLaneFrame(profile, A, B)` with `|B - A|` = 0.3, 0.7 and 1.0 x
  `NominalLengthM` (A off the origin).

Values (relative 1e-6):
- `MakeLaneProfile(6.0e6, 4.0e11, 6.371e6, 3.3895e6)` (l0 = 3.999542395e11, coasting at the track cap
  `sqrt(w^2 + kappa (c^2 - w^2))` = 290,516.78 m/s, kappa = 0.9375): 1,398,839.39 s.
- `MakeLaneProfile(6.0e5, 4.0e8, 6.371e6, 1.7374e6)` (l0 = 3.882916e8, below the cap, coast = the 60 s flip, peak
  69,069.10 m/s): 8,685.83 s.
- A non-drivable profile: 0.

### 8. `NodeTransferTimeS` (case `Transfer`)

```cpp
double NodeTransferTimeS(double endRingDistance1M, double endRingDistance2M, const FVector3d& outward1,
    const FVector3d& outward2, double wallSpeedMps, double penaltyS);
```

`outward_i` = lane i's direction pointing away from the node (normalized inside). = penalty + (0.5 (r1 + r2) theta +
|r1 - r2|) / w, theta = the angle between the two outward directions (0: both lanes leave on the same side, no arc).
- r 2.4e7, 2.4e7, same direction, w 2e4, penalty 3600: 3600.
- r 2.4e7, 2.4e7, opposite directions: 3600 + 2.4e7 pi / 2e4 = 7369.91 s.
- r 2.0e7, 3.0e7, perpendicular: 3600 + (2.5e7 x pi / 2 + 1.0e7) / 2e4 = 6063.50 s.
- r 0, 0, any directions, penalty 3: 3 (zero distances are allowed).
- Symmetric in (1, 2). A zero or non-finite direction, a non-positive or non-finite speed, a negative or non-finite
  distance or penalty: +inf (no transfer).

### 9. `FindFastestRoute` (case `Dijkstra`)

```cpp
FSOLLaneRoute FindFastestRoute(int32 nodeCount, TConstArrayView<FSOLLaneEdge> edges, double penaltyS,
    double wallSpeedMps, double startS, int32 sourceNode, int32 targetNode);
```

State = (node, arriving lane). A transfer is paid at every intermediate node (not at the source or the target): it is
`NodeTransferTimeS` with the arriving lane's and the leaving lane's outward directions at that node (`DirectionAB` at
NodeA, `-DirectionAB` at NodeB) and their end-ring distances there. A hop reached at time `t` (from `startS`, adding
edge and transfer times) is usable only if `LegFitsWindow(t, TimeS, OpenStartS, CloseS)`; no waiting is modelled.
Ties: lower time, then fewer hops, then the lexicographically lower lane index sequence. A route longer than
`NPC_LANE_ROUTE_MAX_HOPS`: not found. An edge with `TimeS <= 0` or non-finite is never taken. `FSOLLaneRoute::Status`
(`ESOLLaneRouteStatus`) tells the outcomes apart: `Found`, `Unreachable`, `TooManyHops` (the fastest route is longer
than the hop limit), `TooManyEdges` (more than 64 edges, the group texture width: the router's fixed state arrays) and
`InvalidEndpoint`; `bFound` is true only for `Found`. **Pinned limit:** one earliest arrival is kept per (node,
arriving lane) and no waiting is modelled, so the result is exact when every window is already open at `startS`
(current windows); with a window that opens later, a slower arrival at the same state that would meet it is not
explored. Making it exact for future windows would need per-state arrival lists (allocation), so it is not done.

Synthetic graph (windows open forever, `DirectionAB` (1, 0, 0), end-ring distances 0, so a transfer = the penalty),
5 nodes, edges by index: e0 0-1 10, e1 1-2 10, e2 0-2 25, e3 2-3 5, e4 1-3 30, e5 3-4 1; `startS` 0, w 2e4.
- penalty 0, 0 -> 3: found, 25, hops {(e0, +1), (e1, +1), (e3, +1)}.
- penalty 3, 0 -> 3: 31, same hops (0-2-3 costs 33).
- penalty 6, 0 -> 3: 36, hops {(e2, +1), (e3, +1)} (0-1-2-3 costs 37).
- penalty 0, e3 window closed (`CloseS` = -inf), 0 -> 3: 40, hops {(e0, +1), (e4, +1)}.
- penalty 0, 4 -> 0: 26, hops {(e5, -1), (e3, -1), (e1, -1), (e0, -1)}.
- e5 closed, 0 -> 4: not found. Source == target: found, time 0, no hops. Node out of range: not found.
- Tie: edges 0-1 5, 1-2 5, 0-2 10, penalty 0, 0 -> 2: hops {(e2, +1)} (fewer hops).
- **Lexicographic tie** (equal time and hops, decided where both paths reach the same state): edges e0 0-1 5, e1 0-2
  5, e2 1-3 5, e3 2-3 5, e4 3-4 1, 0 -> 4: 11, {(e0, +1), (e2, +1), (e4, +1)}. With e2 2-3 and e3 1-3 instead: 11,
  {(e0, +1), (e3, +1), (e4, +1)} (the first differing position decides). Unequal legs e0 0-1 6, e1 0-2 4, e2 1-3 4,
  e3 2-3 6, e4 3-4 1: 11 via {(e0, +1), (e2, +1), (e4, +1)} (the higher sequence settles node 3 first and is
  replaced on e4).
- **Edge times:** e0 0-1 0 and e1 0-1 7, 0 -> 1: 7 via e1 (also with e0 at -3 or NaN); e1 at +inf too: not found,
  `Unreachable`.
- **Status:** 65 parallel edges 0-1: `TooManyEdges`; 64: found. e5 closed, 0 -> 4: `Unreachable`. Target 5:
  `InvalidEndpoint`. The 14-node chain 0 -> 13: `TooManyHops`.
- **Time windows:** penalty 0, e3 `CloseS` 24, 0 -> 3: 40 via {(e0, +1), (e4, +1)} (e3 would run 20-25 or 25-30).
  e3 `CloseS` 25: 25 via e0, e1, e3 (fits exactly). e3 `OpenStartS` 21: 30 via {(e2, +1), (e3, +1)} (arriving at 20
  on e1 is too early, arriving at 25 on e2 fits).
- **Lane-pair transfer:** nodes 0, 1, 2; penalty 0; e0 0-1 10, `DirectionAB` (1, 0, 0), end ring A 0, B 2e7; e1 1-2 10,
  (1, 0, 0), A 2e7, B 0; e2 1-2 10, (-1, 0, 0), A 2e7, B 0. 0 -> 2: hops {(e0, +1), (e2, +1)}, 20 (e0's outward at
  node 1 is -x, e2's is -x: theta 0). With e2 closed: {(e0, +1), (e1, +1)}, 20 + 2e7 pi / 2e4 = 3161.5926535898 s.
- **Real base network** (the 8 base lanes of SDD 53 section 2, times from the SDD 53 6.1 table as inputs, windows open,
  end-ring distances 0, penalty 0): assert the hop sequence and that `TimeS` equals the sum of the supplied edge times
  (relative 1e-12): Earth -> Saturn = Earth-Mars, Mars-Jupiter, Jupiter-Saturn; Mercury -> Mars = Mercury-Venus,
  Venus-Earth, Earth-Mars; Earth -> Moon = Earth-Moon.

### 10. `LaneTunnelMaxRadiusPx`, `StepTunnelClass` (case `PixelClasses`)

```cpp
double LaneTunnelMaxRadiusPx(double segmentDistanceM, double tunnelRadiusM, double focalRefPx);
double LaneTunnelQualityScale(int32 qualityPreset);
ESOLTunnelClass StepTunnelClass(ESOLTunnelClass previous, double maxRadiusPx, double thresholdScale);
```

**Quality scale (user decision B, 53a/53b review):** every class threshold is multiplied by `thresholdScale` =
`LaneTunnelQualityScale(QualityPreset)` = `RIBBON_VANISH_PX_BY_QUALITY[preset] / RIBBON_VANISH_PX_BY_QUALITY[Epic]`
(the flame vanish size V table, no second table; `NPC_TUNNEL_QUALITY_REFERENCE_PRESET` = 3, Epic; preset clamped to
Low .. Cinematic): Low 10, Medium 4.6, High 2.2, Epic and Cinematic 1. A NaN, infinite or non-positive scale reads as
1. The pool ranking (`SelectTunnelPool`) takes no scale: a uniform factor keeps the order. **Open item (53c/53d):** the
invisible switches below hold only at scale 1; at Low the tunnel switches on at 5 px, where the shader's ring radius
fade (`NPC_TUNNEL_RADIUS_FADE_*_PX`) is nearly solid, so the shader fades must take the same scale for the switch to
stay invisible. Not changed in 53a/53b.
- `LaneTunnelQualityScale`: presets 0-4 -> 10, 4.6, 2.2, 1, 1; -1 -> 10; 7 -> 1.
- Scaled rows (off-boundary; re-pinned in the 53d review to Full on 10/12 px, off 0.75 px): Low (Off, 4.99) Off,
  (Off, 5.0) Reduced, (Off, 10) Full, (Off, 8.34) Full, (Reduced, 3.99) Off, (Full, 7.51) Full, (Full, 7.49) Reduced,
  (Off, 0.5) Off; Medium (Off, 2.29) Off, (Off, 2.31) Reduced, (Off, 3.82) Reduced, (Off, 3.84) Full, (Reduced, 1.85)
  Reduced, (Reduced, 1.83) Off, (Full, 3.46) Full, (Full, 3.44) Reduced; High (Off, 1.09) Off, (Off, 1.11) Reduced,
  (Reduced, 1.84) Full, (Reduced, 0.87) Off, (Full, 1.66) Full, (Full, 1.64) Reduced.
- Bad scale: NaN, +inf, 0 -> as 1 ((Off, 0.5) Reduced); -2 ((Off, 0.49) Off).

- `LaneTunnelMaxRadiusPx` = f Rt / d: (1.92e10, 1e7, 960) -> 0.5; (9.6e9, 1e7, 960) -> 1.0; d <= 0 -> +inf; NaN,
  Rt <= 0 or f <= 0 -> 0.
- `StepTunnelClass` at scale 1 (thresholds 0.5 / 0.4 / 10/12 / 0.75, pinned; the Full pair moved by the user in the
  53d review): (Off, 0.49) -> Off; (Off, 0.5) -> Reduced; (Off, 0.83) -> Reduced; (Off, 0.834) -> Full; (Off, 1.0) ->
  Full; (Reduced, 0.45) -> Reduced; (Reduced, 0.39) -> Off; (Reduced, 0.83) -> Reduced; (Reduced, 0.834) -> Full;
  (Full, 0.76) -> Full; (Full, 0.74) -> Reduced; (Full, 0.39) -> Off; any previous with NaN -> Off.
- **Invisible switches:** `NPC_TUNNEL_ACTIVATE_PX == NPC_TUNNEL_RADIUS_FADE_GONE_PX`, and `NPC_TUNNEL_FULL_ON_PX x
  NPC_TUNNEL_RING_BASE_SPACING_RADII == NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX` (relative 1e-12; `FULL_OFF` before the 53d
  review moved the switch).

### 11. `SelectTunnelPool`, `StepOverflowReport` (case `Pool`)

```cpp
int32 SelectTunnelPool(TConstArrayView<double> maxRadiusPx, TConstArrayView<uint8> drawn, int32 poolSize,
    TArray<int32>& outLanes, TConstArrayView<uint8> previousPool = {}, double keepRatio = 1.0);   // the overflow
// count; since the 53d review a previous pool member ranks by radius x keepRatio (NPC_TUNNEL_POOL_KEEP_RATIO 1.1)
bool StepOverflowReport(int32 previousOverflow, int32 overflow);   // true = log and report now
```

- `SelectTunnelPool`: px {3, 0.2, 7, 1}, drawn {1, 0, 1, 1}, pool 2: outLanes {2, 0} (largest first), overflow 1;
  pool 24: {2, 0, 3}, overflow 0; ties to the lower lane index; `outLanes` reset first; pool <= 0: empty, overflow =
  drawn count.
- **Pool 24 overflow:** 30 drawn lanes with px = 30 - lane index (30 down to 1), pool `NPC_TUNNEL_INSTANCE_POOL`:
  outLanes = lanes 0-23 in order, overflow 6.
- `StepOverflowReport`: true iff `overflow != previousOverflow` (so a new overflow, a changed one and its clearing are
  each logged once): (0, 0) false; (0, 6) true; (6, 6) false; (6, 2) true; (2, 0) true. Used for the tunnel pool and
  the group columns (`AssignLaneSlots`' return).

### 12. `LaneWeight`, `LaneShipShares` (case `TrafficWeights`)

```cpp
double LaneWeight(double tierA, double tierB);
void LaneShipShares(int32 laneShipCount, TConstArrayView<double> weights, TArray<int32>& outCounts);
```

- `LaneWeight` = sqrt(tierA x tierB) (O6, double; relative 1e-12): (10, 10) -> 10 (Earth-Moon); (10, 6) -> sqrt(60) =
  7.745966692; (4, 3) -> sqrt(12) = 3.464101615 (Ceres-asteroid); (3, 3) -> 3; (2, 1) -> 1.414213562; (1, 1) -> 1;
  (0, 5) -> 0; negative or NaN tier -> 0. Symmetric. The tier values themselves are layout data, **confirmed
  by the user 2026-11-25** (O6); the cases below pass weights, not tiers.
- `LaneShipShares`, largest remainder (the sum equals `laneShipCount` exactly; remainder ties to the lower index):
  400,000 over {2, 1, 1}: {200,000, 100,000, 100,000}; 10 over {1, 1, 1}: {4, 3, 3}; non-positive or NaN weights count
  0; all zero: all 0.
- **Population weights of the base lanes** (`LaneWeight` of the proposed tiers, in this order: Mercury-Venus sqrt(4)
  = 2, Venus-Earth sqrt(20), Earth-Moon 10, Earth-Mars sqrt(60), Mars-Jupiter sqrt(12), Jupiter-Saturn 2,
  Saturn-Uranus sqrt(2), Uranus-Neptune 1; total 32.0964178): 400,000 -> {24,925, 55,734, 124,624, 96,534, 43,171,
  24,925, 17,625, 12,462}; as blocks (O1, blocks of 100), 4,000 -> {249, 558, 1,246, 965, 432, 249, 176, 125}. Pass
  the weights as `sqrt(a * b)` computed in double, as `LaneWeight` does.
- **Budget unchanged by the lane count:** for weight lists of length 8, 22, 34 and 38, the counts sum to exactly
  400,000.

### 13. Dwarf-planet and asteroid nodes (case `Nodes`)

- Within `NextClosestPairs` (case 1, kinds) and `LaneWeight` (case 12): a DwarfPlanet and an Asteroid node pick and are
  picked like planets; a Moon node never.
- `AphelionM` holds the aphelion about the node's parent body: the Sun for planets, dwarf planets and asteroids, the
  parent planet for a Moon node (the Moon: its apogee about Earth, about 4.055e8 m).

  ```cpp
  double LaneNominalLengthM(TConstArrayView<FSOLLaneNode> nodes, int32 nodeA, int32 nodeB);
  ```

  If one end's `ParentNode` is the other end: the child's `AphelionM` (Earth-Moon with the Moon at 4.055e8 m ->
  4.055e8, matching today's parent-chain walk). If neither end has a `ParentNode`: `AphelionM` A + B (Neptune 30.33 AU
  + Eris 97.58 AU, as metres -> their sum, relative 1e-12). Any other case (a moon to a node that is not its parent),
  an index out of range, or a non-finite or negative aphelion: 0 (not drivable).
- **Long lanes:** `LaneEdgeTimeS` of `MakeLaneProfile(1e7, 150 AU in metres, 2.4622e7, 1.163e6)` is finite and
  positive, and between 880 and 900 days (the coast at the 290,516.78 m/s cap dominates). The longest possible pair,
  Eris + Makemake through an island link, is 97.58 + 52.80 = 150.38 AU (`LaneNominalLengthM` = the sum, as above),
  about 897 days, under the horizon invariant. (The float path at 151 AU is pinned in `NpcTunnelFarFieldTest`
  `FloatPathAtMaxLength`, not in this file.)

### 14. Pool overflow report and column overflow (case `OverflowReport`)

- `AssignLaneSlots` with capacity 64 - 10 clusters - 16 battles = 38 slots (8 base + 30 partner and island-link
  lanes; the caller's computation) and 31 such lanes (one above the SDD 53 4.3 hard bound of 23 picks + 7 links,
  i.e. an overflow from lanes still fading): returns 1, and `StepOverflowReport(0, 1)` is true; the next epoch with
  30: returns 0 and `StepOverflowReport(1, 0)` is true.
- `SelectTunnelPool` as in case 11 (24 of 30).

### 15. Sun safety (case `SunSafety`, O4; 53e extends it)

```cpp
bool PathEntersSunZone(const FVector3d& fromM, const FVector3d& toM, double zoneRadiusM);
bool SunSafetyActive(bool bPlayerOverride, bool bNpc);
FVector3d SunSafeArrivalM(const FVector3d& shipM, const FVector3d& targetM, double zoneRadiusM,
    double edgeRadiusM);
```

The zone radius is `NPC_LANE_SUN_CLEARANCE_RADII x SUN_RADIUS_M` (1.3914e10 m), the edge (waypoint and arrival)
radius `NPC_LANE_SUN_REOPEN_RADII x SUN_RADIUS_M` (1.53054e10 m); the tests pass them explicitly as the literals
1.3914e10 and 1.53054e10.
- `PathEntersSunZone` = `SunSegmentDistanceM(from, to) < zoneRadius`; NaN anywhere: true (fail safe).
  (1e11, 5e9, 0) -> (-1e11, 5e9, 0), zone 1.3914e10: true (5e9). (1e11, 2e10, 0) -> (-1e11, 2e10, 0): false (2e10).
  (1e11, 0, 0) -> (2e11, 0, 0): false. (1e11, 1.3914e10, 0) -> (-1e11, 1.3914e10, 0): false (equal, not below).
- `SunSafetyActive` = `bNpc || !bPlayerOverride`: (false, false) true; (true, false) false; (false, true) true;
  (true, true) true (an NPC never overrides).
- `SunSafeArrivalM`: a target at or outside the zone is returned unchanged; a target inside it moves to
  `edgeRadius x unit(ship)` (the edge point facing the ship), `edgeRadius x (0, 0, 1)` if the ship is at the origin or
  not finite. Ship (1e11, 0, 0), target (1e9, 0, 0): (1.53054e10, 0, 0). Ship (0, 1e11, 0), target (0, 0, 0): (0,
  1.53054e10, 0). Target (2e10, 0, 0): unchanged. Ship (0, 0, 0), target (1e9, 0, 0): (0, 0, 1.53054e10).
- The detour waypoint (one point on the edge sphere, in the plane of the Sun, the ship and the target, on the side
  nearer the ship's velocity, SDD 53 6.5) and the plan logic are specified by the 53e test author before that code.

## 53c1 additions (schedule, implemented after 53a/53b)

Added by part 53c1 (SDD 53 5.3, 6.3). Not part of the 53a test-first set. Tests in `Tests/LaneNetworkSubsystemTest.cpp`
(`SOLTest.LaneNetworkSubsystem.*`) and `NpcRail.LaneSlotColumns`.

**Positions function:** the schedule takes a node-positions function that must be a **pure function of time** (it is
called from worker threads in the parallel rebuild), the same position for the same time, no shared mutable state.

| API | Notes |
|---|---|
| `SOLKepler::ElementsToPositionM(elements, gm, elapsedSeconds)` (`Universe/SOLKepler.h`) | position only, no velocity; `FVector3d` metres |
| `FSOLBodyRegistry::ComputeRelativePositionAtM(index, secondsSinceJ2000)` (`Universe/SOLBodyRegistry.h`) | a body's position relative to its parent at a time; the Moon adds it to Earth's (no chain re-walk) |
| `FSOLLaneEpochDistances` (`SOLLaneNetworkMath.h`) | one epoch's pair distances, up to 64 nodes: taking-part and candidate bit masks, per-node base-link masks, symmetric distance matrix (NaN where an end is no candidate) |
| `PrepareEpochDistances(nodes, baseLanes, out)` | node kinds and base links, once per node set; false above 64 nodes |
| `FillEpochDistances(positionsM, inOut)` | candidate mask and distance matrix for one epoch's positions |
| `NearestFromDistances(distances, outNearest, outNearestDistanceM)` | each node's nearest candidate not base-linked to it, ties to the lower index, `INDEX_NONE` and NaN if none; equals `StepPartnerPicks`' nearest |
| `IslandLinksFromDistances(distances, lanes, rootNode, outLinks)` | `IslandLinks` over filled distances; equals `IslandLinks`; the tests check both daily |
| `FSOLLaneNetworkSchedule::GetHorizonEndS()` | the start of epoch `k + H`, where a lane open through the last buffer entry closes |
| `FSOLLaneNetworkSchedule::FindRoute(source, target, startS, slotProfiles, wallSpeedMps, penaltyS)` | `FindFastestRoute` over the slot lanes; **horizon rule:** a window whose close equals the horizon end plans as open with no close (no close beyond 960 d), windows that close inside the horizon are respected (SDD 53 6.3) |
| `USOLLaneTrafficSubsystem::MarkNetworkTimeJump()` | the next `AdvanceNetwork` rebuilds |
| `USOLSimClockSubsystem::GetTimeSetCount()` | counts `SetJulianDate` calls with a new date (holding the same pinned date counts once); `USOLNpcSubsystem` compares it each frame before `AdvanceNetwork` |
| `FSOLLaneEdge::PlanOpenFromS` | default +inf; a departure at or after it ignores the edge's window (the schedule sets the horizon end, SDD 53 6.3) |
| `FSOLNpcGroupTable::LaneSlotCapacity()` | lane network slots the group texture leaves: 64 - clusters - battles |
| `FSOLNpcGroupTable::LaneSlotColumn(slot)` | base slots 0 .. `LaneCount` - 1 keep their lane columns, slots from `LaneCount` on map to `slot + clusters + battles`, `INDEX_NONE` past the capacity; 53c2 uses it for schedule slots 8 and up |

## 53c2 additions (ship blocks, windows, activation; test first)

For the 53c2a test author (cases 16-24, 26) and the 53c2b test author (case 25), from SDD 53 4.5 (O1), 5.4 (Q8, Q10,
Q26, Q30), 7.2 (Q21, Q22), 18 (P1-P6, user 2026-11-25), R8, R12, R14. Pure functions only; write the tests from this section, never from the code.
Cases 16-24 and 26 go in **`Source/SOLTest/Tests/LaneNetworkTrafficTest.cpp`**, paths
`SOLTest.LaneNetworkTraffic.<Case>` (run with `Tools\RunTests.bat -Build -Filter SOLTest.LaneNetworkTraffic`); case 25
extends `Source/SOLTest/Tests/NpcRailTest.cpp` as `SOLTest.NpcRail.GpuLaneWindowMirror`. Cases 6 (`LegFitsWindow`,
`LaneDepartureStopS`, `LaneShipLegDrawn`) and 12 (`LaneShipShares`) are reused as they stand, not restated. Module
rule (SDD 53 section 10): `Traffic/` includes `SOLConstants.h` and `SOLLaneTrafficMath.h` only, never `Npc/`.

**Status column:** *pinned* = from the SDD or the user; *implementer* = an encoding or tie order with no visible
effect, the implementer may change it if the tests change with it. The six decisions the SDD left open (P1-P6) were
**answered by the user on 2026-11-25** (table below, SDD 53 section 18); every assertion in cases 16-26 is pinned and
the test author writes all of them.

### Resolved decisions (user, 2026-11-25)

| Id | Question | Answer (pinned) |
|---|---|---|
| P1 | Which window governs a ship in a **U-turn** (between two legs; the shader draws `turnA` / `turnB` today)? | **The leg after the turn:** a turning ship is drawn iff that leg fits (`LegFitsWindow(turn end, T_leg, open, close)`), so a ship that arrives where its next leg is not flown vanishes at the end ring on arrival (R12). **Design note:** this matches the future goal of ships docking at a station at the lane's end: the arrival at the end ring becomes the docking moment (case 21) |
| P2 | A **promoted** (Mid / Full) lane ship whose leg ends and whose next leg is not flown (Q26, R12) | **Demoted and docked at once**, no fade (O9): `LaneShipAiTier` returns Far for any docked ship (case 21) |
| P3 | **Block membership and size** for populations whose lane ship count is not 400,000 (NpcCount, LaneFraction, ramp stages) | As recommended: block `b` = the lane-local ship indices `[100 b, 100 b + 100)` (contiguous, so a block move is one contiguous rail range; the spread along the lane comes from each ship's own hashed phase); block count = ceil(lane ships / 100), the last block partial (case 16) |
| P4 | **Pool policy after spawn**: (a) an opening lane's target; (b) the order when the free pool is short; (c) top-up of a short lane later in its episode; (d) a time jump | As recommended: (a) its `LaneShipShares` block share over the current set's slotted lanes; (b) ascending slot order; (c) none: blocks join a lane only at its episode open (`EpisodeTakeOrder`, case 17); (d) a full exact re-share of every block at a time jump, as at spawn (`AssignShipBlocks`, one full rail upload per jump) |
| P5 | **Q22 scope**: overlay off, and the quality scale | **Changed from the recommendation:** inactive lanes are hidden **always**, with the Lanes overlay on or off (the flag does not depend on the overlay), and the 2.4 px threshold **scales with the QualityPreset** by `LaneTunnelQualityScale` (the same scale as the tunnel classes, case 10): `LaneTrafficActive` takes a `thresholdScale` (case 23). The shader reads the flag in 53d (plan 53d step 4) |
| P6 | **Q30 and the AUTO latch**: `CheckLaneLatch` re-checks an Approach through `FindLaneLeg`, so skipping closed lanes there would turn an Approach on a lane that closes into `Release` (Left, terminal for the order) | As recommended: an **Approach** on a lane that closes is **`Stale`** (dropped, searched again); a **Hold** on a closed lane is unchanged (`Keep`) until 53e's finish-if-safe (case 24) |

**SDD wording note (not a decision):** SDD 5.4 names the slot floats `openStart` and `departStop` (= close - `T_leg`),
but `T_leg` is per ship (effort, `LaneShipLegS`), so one per-slot `departStop` cannot serve every ship. The texel
carries `openStart` and **`close`**, and each ship tests its own leg with `LegFitsWindow` (case 6), as SDD 5.4's rule
"open over all of `[t, t + T_leg]`" requires.

### Constants (`SOLConstants.h`)

| Name | Type | Value | Status |
|---|---|---|---|
| `NPC_GROUP_TEXTURE_ROWS` | int32 | **7** (was 6) | implementer: one new row holds the window and active flag (all 24 channels of rows 0-5 are in use) |
| `NPC_GROUP_WINDOW_ROW` | int32 | 6 | implementer; invariant: `< NPC_GROUP_TEXTURE_ROWS` |
| `NPC_LANE_WINDOW_FAR_S` | double | 1.0e30 | implementer: the finite stand-in for +-inf in the group texel (the shader compiles with fast math, which may assume no infinities); invariant: `< FLT_MAX` and `> 1e3 x NPC_LANE_NETWORK_HORIZON_EPOCHS x NPC_LANE_NETWORK_EPOCH_S` |
| `NPC_LANE_SHIP_BLOCK_COUNT_DEFAULT` | int32 | `NPC_LANE_SHIP_DEFAULT_COUNT / NPC_LANE_SHIP_BLOCK_SIZE` = 4,000 | pinned (O1); `static_assert` == 4000 |
| `NPC_LANE_TRAFFIC_ACTIVE_PX` | double | 2.4 | pinned (Q22); `static_assert` == `NPC_TUNNEL_REFERENCE_FOCAL_PX / NPC_LANE_FADE_FAR_RADII` (960 / 400) |
| `NPC_LANE_SHIP_PARKED_LEG_S` | double | -1.0 | implementer: the rail texel leg time (`railB.w`) of a parked ship (free-pool block); invariant: `< 0` |
| `NPC_RAIL_PARKED_GROUP` | uint16 | 0xFFFF | implementer: `FSOLNpcRail::Group` of a parked ship; invariant: `>= NPC_GROUP_TEXTURE_WIDTH` |

### Time bases (pinned)

Schedule windows (`GetSlotWindow`) are **seconds since J2000**. Rail time = J2000 seconds -
`mEpochSecondsSinceJ2000` (`USOLNpcSubsystem`). The shader's time `t` = rail time - `mGpuRailEpochS`. So a window bound
goes to the texel as `bound - gpuEpochS` with `gpuEpochS = mEpochSecondsSinceJ2000 + mGpuRailEpochS`, subtracted in
double, then rounded once to float (`LaneWindowGpuS`, case 22). Row 6 is written with the same `mGpuRailEpochS` as
the rail texels the shader reads that frame.

### Types

```cpp
// Traffic/SOLLaneNetworkMath.h
struct FSOLShipRange { int32 First = 0; int32 Count = 0; };          // lane-local ship indices [First, First + Count)

// Npc/SOLNpcRail.h
struct FSOLTexelRegion { int32 X = 0; int32 Y = 0; int32 Width = 0; int32 Height = 0; };   // one UpdateTextureRegions rect

// Traffic/SOLLaneTrafficMath.h: FSOLLaneMergeLane gains one field (default keeps every existing test valid)
bool bOpenNow = true;      // The lane is open now (LaneOpenAt over its slot window); Q30
```

### 16. Ship block layout (case `ShipBlocks`, P3)

```cpp
int32 ShipBlockCount(int32 laneShipCount, int32 blockSize);                       // SOLLaneNetwork
int32 ShipBlockSize(int32 block, int32 laneShipCount, int32 blockSize);
int32 ShipBlockOfLaneShip(int32 laneShipIndex, int32 laneShipCount, int32 blockSize);
```

- `ShipBlockCount` = ceil(count / size): (400,000, 100) -> 4,000 (= `NPC_LANE_SHIP_BLOCK_COUNT_DEFAULT`); (400,001,
  100) -> 4,001; (400,050, 100) -> 4,001; (99, 100) -> 1; (0, 100) -> 0; (-5, 100) -> 0; (100, 0) -> 0. Pinned (P3:
  ceil, the last block partial).
- `ShipBlockSize`: (3,999, 400,000, 100) -> 100; (4,000, 400,050, 100) -> 50 (P3, the partial last block); (4,000,
  400,001, 100) -> 1; (0, 99, 100) -> 99; (0, 3,200, 100) -> 100; (31, 3,200, 100) -> 100; (4,000, 400,000, 100) -> 0
  (out of range); (4,001, 400,050, 100) -> 0; (-1, ...) -> 0. The sizes of all blocks sum to the lane ship count:
  400,050 for (400,050, 100), 99 for (99, 100).
- `ShipBlockOfLaneShip` = index / size (P3, contiguous membership): (12,345, 400,000, 100) -> 123; (0, ...) -> 0;
  (99, 400,000, 100) -> 0; (100, 400,000, 100) -> 1; (399,999, 400,000, 100) -> 3,999; (400,049, 400,050, 100) ->
  4,000 (in the partial block); (400,050, 400,050, 100) -> `INDEX_NONE`; index < 0 or >= count, or size <= 0 ->
  `INDEX_NONE`.

### 17. Block pool (case `BlockPool`, O1, R14)

```cpp
void AssignShipBlocks(int32 blockCount, TConstArrayView<double> slotWeights, TArray<int32>& outBlockSlot);
int32 ReleaseShipBlocks(TConstArrayView<uint8> slotRelease, TArray<int32>& inOutBlockSlot);
int32 TakeShipBlocks(TConstArrayView<int32> slotOrder, TConstArrayView<int32> slotTargets,
    TArray<int32>& inOutBlockSlot, TArray<int32>& outMovedBlocks);
bool CanReleaseSlotBlocks(bool bLaneInSetNow, int32 cpuTierShipsOfSlot);
bool LaneEpisodeOpens(const FSOLLanePair& previousSlotLane, bool bPreviousInSet, const FSOLLanePair& slotLane,
    bool bInSetNow);
void EpisodeTakeOrder(TConstArrayView<uint8> slotEpisodeOpens, TArray<int32>& outSlotOrder);   // P4 b, c
```

`outBlockSlot` / `inOutBlockSlot`: one entry per block, the slot that holds it or `INDEX_NONE` (free pool).
- **`AssignShipBlocks` (spawn, exact):** reset first, `blockCount` entries (0 for `blockCount <= 0`); targets =
  `LaneShipShares(blockCount, slotWeights)` (case 12; a weight of 0 for a free or unslotted slot); slot `s` gets the
  next `target[s]` blocks in ascending slot order (contiguous; implementer order); no positive weight: every block
  free. Every block is assigned whenever a weight is positive, so the slots' ship counts (case 18) sum to the lane ship
  count exactly.
  - 10, {1, 1, 1}: {0, 0, 0, 0, 1, 1, 1, 2, 2, 2}. 6, {1, 0, 2}: {0, 0, 2, 2, 2, 2}. 3, {0, 0}: {-1, -1, -1}.
  - 4,000 over the 8 base weights of case 12 (2, sqrt 20, 10, sqrt 60, sqrt 12, 2, sqrt 2, 1): slot `s` holds blocks
    [start_s, start_{s+1}) with starts {0, 249, 807, 2,053, 3,018, 3,450, 3,699, 3,875, 4,000}.
  - 4 blocks (a partial last block changes nothing here, blocks are counted, P3) over {1, 1}: {0, 0, 1, 1}.
  - **Time jump (P4 d, pinned):** the caller re-shares every block exactly by calling `AssignShipBlocks` over the new
    set's weights, whatever the old map held. `outBlockSlot` pre-filled with {2, 2, 2, -1, 0, 1, 1, 1, 1, 1, 7, 7}
    (12 entries) and called with 10, {1, 1, 1}: {0, 0, 0, 0, 1, 1, 1, 2, 2, 2} (10 entries, equal to a fresh call).
- **Opening lane's target (P4 a, pinned):** the caller passes `slotTargets = LaneShipShares(blockCount, w)` with `w`
  the current set's slotted lanes' weights (0 for a free or unslotted slot), the same targets as `AssignShipBlocks`.
  4,000 over the 8 base weights: targets {249, 558, 1,246, 965, 432, 249, 176, 125} (the differences of the starts
  above; sum 4,000). 10 over {1, 1, 1}: {4, 3, 3}.
- **`ReleaseShipBlocks`:** every block whose slot `s` has `slotRelease[s] == 1` becomes free; returns the number
  released; slots past `slotRelease` are kept. {0, 0, 3, 1, 3, 4, 2, 4}, release {0, 1, 0, 0, 1}: {0, 0, 3, -1, 3, -1,
  2, -1}, returns 3.
- **`TakeShipBlocks`:** for each slot in `slotOrder` (the caller passes `EpisodeTakeOrder`'s ascending list, P4 b), the slot takes the lowest-index free
  blocks until it holds `slotTargets[slot]` blocks in all (blocks it already holds count); returns the total shortfall;
  `outMovedBlocks` (reset first) lists the taken blocks in take order. Slots out of range of `slotTargets` are skipped.
  - {0, 0, -1, 1, -1, -1, 2, -1}, order {3, 4}, targets {0, 0, 0, 2, 3}: {0, 0, 3, 1, 3, 4, 2, 4}, moved {2, 4, 5, 7},
    returns 1 (slot 4 short by one). This is the pinned P4 b outcome: when slots 3 and 4 open together and the pool
    is short, the ascending order {3, 4} serves slot 3 first.
  - Same with order {4, 3} (the function follows whatever order it is given; the caller never passes it): slot 4
    takes 2, 4, 5 and slot 3 takes 7: {0, 0, 4, 1, 4, 4, 2, 3}, moved {2, 4, 5, 7}, returns 1 (slot 3 short by one).
  - No free block: nothing moves, returns the sum of the targets. A target at or below what the slot holds: nothing.
  - Determinism: the same inputs twice give identical outputs.
- **`CanReleaseSlotBlocks`** (pinned: ships change lane only while their block is empty, SDD 4.5): true iff
  `!bLaneInSetNow && cpuTierShipsOfSlot <= 0`. (false, 0) -> true; (false, 2) -> false; (true, 0) -> false. A Sun-closed
  lane is still in the set and keeps its blocks (Q10). The early stop leaves no GPU ship in flight at the close, so
  only promoted ships can delay a release, and only until their leg ends: a promoted ship is demoted and docked at
  once at its end ring (P2, case 21), so `cpuTierShipsOfSlot` falls to 0 within one leg time of the close. The caller
  holds the slot (`SetSlotHeld`) while its blocks are not released, so a slot is never reused under blocks that still
  point at it.
- **`LaneEpisodeOpens`** (the profile freeze and the block take happen here, SDD 4.5): true iff `bInSetNow` and
  (`previousSlotLane != slotLane` or `!bPreviousInSet`). ((2, 5), true, (2, 5), true) -> false; ((2, 5), false, (2, 5),
  true) -> true (back after leaving, even in a held slot); ((-1, -1), false, (2, 5), true) -> true; ((2, 5), true, (4,
  6), true) -> true; (any, any, (2, 5), false) -> false.
- **`EpisodeTakeOrder`** (P4 b and c, pinned): `outSlotOrder` (reset first) = the slots `s` with
  `slotEpisodeOpens[s] != 0`, in ascending order. The caller fills `slotEpisodeOpens` from `LaneEpisodeOpens` per slot
  at an epoch step, so **only a slot whose episode opens this step takes blocks**; a lane left short keeps what it
  holds until its next episode (no top-up mid-episode, so no ship appears mid-leg). {0, 1, 0, 1, 1}: {1, 3, 4};
  {1, 0, 0}: {0}; all 0: {}; {}: {}; values 2 and 255 count as opening ({0, 2, 255}: {1, 2}). With the
  `TakeShipBlocks` example above and `slotEpisodeOpens` {0, 0, 0, 1, 1}: order {3, 4}, so the outcome is the first
  example's. A short slot that does not open takes nothing: block map {0, -1, -1}, targets {3}, `slotEpisodeOpens`
  {0}: order {}, `TakeShipBlocks` moves nothing, returns 0, the map stays {0, -1, -1}; with {1}: order {0}, map
  {0, 0, 0}, moved {1, 2}, returns 0.

### 18. Per-slot lists and sums (case `BlockLists`)

```cpp
void SlotBlockLists(TConstArrayView<int32> blockSlot, int32 slotCount, TArray<int32>& outSlotStart,
    TArray<int32>& outBlocks);
void SumBlockCounts(TConstArrayView<int32> blockSlot, TConstArrayView<int32> perBlock, int32 slotCount,
    TArray<int32>& outPerSlot);
```

- `SlotBlockLists` (CSR): `outSlotStart` has `slotCount + 1` entries; slot `s`'s blocks are `outBlocks[outSlotStart[s]
  .. outSlotStart[s + 1])`, ascending; free blocks and slots >= `slotCount` are not listed; both reset first.
  {0, 0, 3, 1, 3, 4, 2, 4}, 5: starts {0, 2, 3, 4, 6, 8}, blocks {0, 1, 3, 6, 2, 4, 5, 7}. slotCount 0: starts {0},
  blocks empty.
- `SumBlockCounts`: per slot, the sum of `perBlock` over its blocks (block sizes give the ship count, block in-wall
  counts give `Rw`'s input). {0, 0, 3, 1, 3, 4, 2, 4}, values {100 x 7, 50}, 5: {200, 100, 100, 200, 150} (sum 750 =
  the lane ships). A block past `perBlock` adds 0.

### 19. Partial upload ranges (cases `DirtyRanges`, `TexelRegions`; SDD 13 gate 0.5 ms per block move)

```cpp
void ShipBlockDirtyRanges(TConstArrayView<int32> movedBlocks, int32 laneShipCount, int32 blockSize,
    TArray<FSOLShipRange>& outRanges);                                              // SOLLaneNetwork
void RailTexelRegions(int32 firstTexel, int32 texelCount, int32 textureWidth, TArray<FSOLTexelRegion>& outRegions);
                                                                                    // SOLNpcRail
```

- Pinned (P3, contiguous membership): block `b` covers the lane-local ships `[b x size, min((b + 1) x size, count))`.
- `ShipBlockDirtyRanges`: blocks in any order, repeats allowed, out-of-range ignored; output sorted, adjacent and
  repeated blocks merged, reset first. {7, 2, 3, 3, 5}, 750, 100: {(200, 200), (500, 100), (700, 50)}. {} -> {}.
- The Npc side maps a range to rail texels: first texel = 2 x (battle ships + `First`), count = 2 x `Count` (two
  texels per ship, `ClassifyIndex` puts lane ships after the battle ships).
- `RailTexelRegions` splits a linear texel span of a `textureWidth`-wide texture into at most three rects (head part
  row, full rows, tail part row), reset first. Width 2,048: (100, 200) -> {(100, 0, 200, 1)}; (2,000, 200) ->
  {(2,000, 0, 48, 1), (0, 1, 152, 1)}; (2,000, 4,154) -> {(2,000, 0, 48, 1), (0, 1, 2,048, 2), (0, 3, 10, 1)}; (0,
  4,096) -> {(0, 0, 2,048, 2)}; count <= 0, width <= 0 or first < 0 -> empty.

### 20. Lane spawn into blocks (case `LaneSpawnBlocks`; `SOLNpcScale`, `SOLNpcRail`)

```cpp
FSpawn MakeSpawn(uint32 seed, int32 npcIndex, const FCounts& counts, const FLayout& layout,
    const FBattleParams& battle, TConstArrayView<int32> blockSlot, int32 blockSize);   // overload
void CountBlockSpawns(uint32 seed, const FCounts& counts, int32 blockSize, TArray<int32>& outInWall,
    TArray<int32>& outHalo);
uint16 LaneRailGroup(const FSOLNpcGroupTable& groups, int32 slot);                     // SOLNpcRail
bool IsParked(const FSOLNpcRail& rail);                                                // SOLNpcRail
void GpuParkedRailTexels(int32 faction, FVector4f& outTexelA, FVector4f& outTexelB);  // SOLNpcRail
```

Population for the cases: `NPC_DEFAULT_SEED`, total 10,000, battle 2,000, lane fraction 0.4 (`SplitCounts`: battle
2,000, lane 3,200, cluster 4,800; 32 blocks), `DefaultLayout()`, blocks from `AssignShipBlocks(32, w)` with `w` the 8
base weights of case 12 unless stated (32 blocks over those weights: targets `LaneShipShares(32, w)`, slot `s` holding
a contiguous run in ascending slot order). The block of a lane ship is `j / blockSize` (P3, contiguous membership,
pinned); 3,200 is a multiple of 100, so no partial block arises. **Partial block row (P3):** total 10,000, battle
2,000, lane fraction 0.40625 (exact in double): `SplitCounts` gives lane 3,250, cluster 4,750; `CountBlockSpawns`
gives 33 entries, blocks 0-31 have in-wall + halo == 100 and block 32 has 50; with `AssignShipBlocks(33, w)` every
lane ship `j` in [3,200, 3,250) has `GroupIndex == blockSlot[32]`, and the per-slot ship counts (`SumBlockCounts` of
the block sizes) sum to 3,250.
- **Lane fields unchanged:** for every lane ship (lane-local index `j`), the overload's `GroupIndex` ==
  `blockSlot[j / blockSize]` (the slot), and Faction, LanePhase, LaneEffort, LaneRho, LaneClockRad and bLaneHalo equal
  the legacy `MakeSpawn` (same hash streams); every battle and cluster ship equals the legacy result field for field.
- **Parked:** a ship whose block is free has `GroupIndex == INDEX_NONE` and its lane fields still filled (equal to the
  legacy values).
- `CountBlockSpawns`: both arrays reset to `ShipBlockCount(counts.Lane, blockSize)` entries; in-wall + halo of block
  `b` == `ShipBlockSize(b, ...)`; the halo total equals the number of lane ships with legacy `bLaneHalo`; summed per
  slot with `SumBlockCounts` they equal a direct tally of the overload's spawns per slot.
- `LaneRailGroup` = `groups.LaneSlotColumn(slot)` as uint16, `NPC_RAIL_PARKED_GROUP` for `INDEX_NONE` or a slot past
  the capacity. With 8 lanes, 10 clusters, 16 battles: slot 3 -> 3; slot 8 -> 34; slot 37 -> 63; slot 38 -> 0xFFFF;
  -1 -> 0xFFFF.
- `IsParked`: Kind Lane and Group == `NPC_RAIL_PARKED_GROUP`. `GpuParkedRailTexels`: texel A = (kind Lane + 4 x
  faction, packed as `GpuRailTexels` packs it with group 0 and fraction 0, then 0, 0, 0), texel B = (0, 0, 0,
  `NPC_LANE_SHIP_PARKED_LEG_S`). Implementer encoding; the 53c2b shader and mirror hide `railB.w < 0` (case 25).

### 21. In space or docked (case `InSpace`, SDD 5.4, R12; P1)

```cpp
bool LaneShipInSpace(const FSOLLaneCycle& cycle, double phase, double railTimeS, double openStartRailS,
    double closeRailS);                                                              // SOLLaneNetwork
```

The double form (tiers, tests); windows in rail time. Invalid cycle (`LegS` or `CycleS` not finite and positive):
true iff `openStart <= t < close` (a lane that is not drivable draws its midpoint disc, as today, while open;
implementer). Otherwise `p = CycleSegmentAt(cycle, phase, t)`: on a leg (Outbound, Inbound) the result is
`LaneShipLegDrawn(t, t - p.SegmentTimeS, LegS, open, LaneDepartureStopS(close, LegS))` (case 6); in a U-turn
(TurnAtA, TurnAtB) it is governed by **the leg after the turn** (P1, pinned): `LegFitsWindow(t - p.SegmentTimeS +
TurnS, LegS, open, close)`. So a ship arriving at an end ring whose next leg is not flown is docked from the moment
it arrives (it vanishes at the end ring; the future station docking), and a ship whose next leg is flown is drawn
through the whole turn. NaN window: false.
Cycle {LegS 30, TurnS 5, CycleS 70} built directly, phase 0 (Outbound [0, 30), TurnAtB [30, 35), Inbound [35, 65),
TurnAtA [65, 70)). Times stay off the segment boundaries on purpose (floating `frac`).

| Window [open, close) | t = 10 (Out, dep 0) | 50 (In, dep 35) | 75 (Out, dep 70) | 99.5 (Out, dep 70) | 120 (In, dep 105) | 32 (TurnB) | 67 (TurnA) | 102 (TurnB) |
|---|---|---|---|---|---|---|---|---|
| [-0.5, 100.5) | true | true | true | true | false | true | true | false |
| [40.5, 100.5) | false | false | true | true | false | false | true | false |
| [0, 20) (shorter than the leg) | false | false | false | false | false | false | false | false |
| [-inf, +inf) | true | true | true | true | true | true | true | true |
| NaN | false at every t | | | | | | | |

Turn columns (P1): t = 32 is in TurnAtB [30, 35), next leg Inbound [35, 65): fits [-0.5, 100.5) (35 >= -0.5, 65 <=
100.5), not [40.5, 100.5) (35 < 40.5). t = 67 is in TurnAtA [65, 70), next leg Outbound [70, 100): fits both (100 <=
100.5). t = 102 is in TurnAtB [100, 105) of the second cycle, next leg Inbound [105, 135): fits neither (135 > 100.5).
Extra turn rows, pinned: t = 33 and 34.5, window [-0.5, 100.5): true (same turn as 32); t = 31, window [40.5, 100.5):
false; t = 66 and 69.5, window [40.5, 100.5): true; t = 67, window [70.5, 100.5): false (next leg departs 70 < 70.5);
t = 67, window [69.5, 99.5): false (arrives 100 > 99.5). The first row opens at -0.5, not 0: `t - SegmentTimeS`
rebuilt from `frac(t / T_cyc)` can land an ulp off the true departure 0. Phase 0.5, t = 10, window [-0.5, 100.5):
true (Inbound, departure about 0). **No ship vanishes mid-leg:** window [-0.5, 100.5), t stepped by 0.25 over
[0.125, 210): on every leg the result is constant from its departure to its arrival, and **on every turn the result
is constant over the turn and equal to the result of the leg that follows it** (P1), so a drawn ship is drawn from
the start of a turn to the end of the next leg, and an undrawn turn is never followed by a drawn leg of the same
cycle position. With window [40.5, 100.5) over that sweep the drawn set is exactly t in [65, 100) (TurnAtA and the
Outbound leg that follows it); t in [0.125, 65) and [100, 210) is false. Invalid cycle {0, 0, 0}: (t 5, [0, 10))
true, (t 10.5, [0, 10)) false.

```cpp
EAiTier LaneShipAiTier(EAiTier previous, EAiTier selected, bool bInSpace);           // SOLNpcScale
```

Pinned (Q26): in space -> `selected`; docked and `previous == Far` -> Far (a docked ship is never promoted). (Far,
Mid, true) -> Mid; (Mid, Mid, true) -> Mid; (Far, Mid, false) -> Far; (Far, Far, false) -> Far. Docked and previously
Mid or Full (P2, pinned): **Far at once**, whatever `selected` is, no fade or grace frame: (Mid, Mid, false) -> Far;
(Mid, Full, false) -> Far; (Full, Full, false) -> Far; (Full, Mid, false) -> Far; (Mid, Far, false) -> Far. In space
the previous tier does not matter: (Full, Far, true) -> Far; (Mid, Full, true) -> Full. So the result is Far iff
`!bInSpace`, else `selected`. **Sequence (pinned):** cycle {30, 5, 70}, phase 0, window [40.5, 100.5), `selected`
= Mid at every step: t 99.875 (Outbound, in space) -> Mid; t 100.125 (TurnAtB, next leg Inbound [105, 135) not
flown, docked) -> Far with previous Mid; t 100.375 -> Far.

### 22. Group texel window row (case `WindowTexel`; `SOLLaneNetwork`, `SOLNpcRail`)

```cpp
float LaneWindowGpuS(double windowS, double gpuEpochS);
FVector4f LaneWindowTexel(bool bHasWindow, double openStartS, double closeS, double gpuEpochS, bool bActive);
void WriteLaneWindowRow(TConstArrayView<FVector4f> windowBySlot, const FSOLNpcGroupTable& groups,
    TArrayView<FVector4f> groupTexels);                                              // SOLNpcRail
```

- `LaneWindowGpuS` = float(clamp(window - epoch, -FAR, +FAR)), FAR = `NPC_LANE_WINDOW_FAR_S`; +inf -> +FAR, -inf ->
  -FAR; NaN -> NaN. With epoch 810,005,000 (= 9,375 E + 5,000): 9,375 E -> -5,000.0f; 9,380 E -> 427,000.0f;
  892,944,000 -> 82,939,000.0f; 892,944,003 -> 82,939,000.0f (float rounding, ulp 8 s there); -inf ->
  `static_cast<float>(NPC_LANE_WINDOW_FAR_S)` negated.
- `LaneWindowTexel` = (`LaneWindowGpuS(open)`, `LaneWindowGpuS(close)`, active ? 1 : 0, 0) when `bHasWindow`, neither
  bound NaN and open < close; otherwise the **empty window** (+FAR, -FAR, active ? 1 : 0, 0), so nothing is drawn (a
  free slot, a lane closed now, a Gone lane in its fade).
- `WriteLaneWindowRow`: row `NPC_GROUP_WINDOW_ROW` (texel `6 x 64 + column`), column = `LaneSlotColumn(slot)` for each
  slot of `windowBySlot`; slots without a column skipped; every other column of row 6 zero; rows 0-5 untouched; a
  buffer under `NPC_GROUP_TEXTURE_ROWS` rows is not touched. With 8 lanes, 10 clusters, 16 battles and 9 slot
  texels, slot 8's texel lands at column 34, column 8 (a cluster) is zero.
- **Profile per episode (SDD 4.5, pinned):**

  ```cpp
  FSOLLaneProfile SlotLaneProfile(double laneRadiusM, int32 inWallCount, double nominalLengthM,
      double bodyRadiusAM, double bodyRadiusBM);                                     // SOLNpcRail
  ```

  Identity: `== SOLLaneTraffic::MakeLaneProfile(SOLNpcTunnel::TunnelRadiusM(laneRadiusM, inWallCount),
  nominalLengthM, bodyRadiusAM, bodyRadiusBM)`, and equal to the entry `BuildLaneProfiles` gives for the same inputs
  (one lane, in-wall count, nominal length, radii). Inputs per slot: the lane radius = the larger region radius of its
  two nodes (O5; Earth-Moon's hand row 2e5 m), the in-wall count = `SumBlockCounts` of its blocks' in-wall counts at the
  open, the nominal length = `LaneNominalLengthM` (case 13), the radii = the nodes' `ClearanceRadiusM`. Row: lane radius
  4e6, in-wall 0 (an empty slot): the wall radius is `4e6 x NPC_TUNNEL_RADIUS_MIN_SCALE` (`TunnelRadiusM` of an empty
  lane). The caller recomputes a slot's profile only when `LaneEpisodeOpens` is true (and at spawn and at a time
  jump, where every block is re-shared, P4 d), so its rows 2-5 do not change mid-episode.

### 23. Traffic activation (case `Activation`, Q21, Q22; P5)

```cpp
double LaneTrafficRadiusPx(const FVector3d& cameraM, const FVector3d& aM, const FVector3d& bM, double laneRadiusM,
    double focalRefPx);
bool LaneTrafficActive(double laneRadiusPx, int32 cpuTierShipsOfLane, double thresholdScale);
```

`LaneTrafficRadiusPx` = `LaneTunnelMaxRadiusPx(segment distance camera -> [A, B], laneRadiusM, focalRefPx)` (case 10:
d <= 0 -> +inf, NaN distance, R <= 0 or f <= 0 -> 0); `focalRefPx` is the live view's focal (SDD 7.1). The lane radius
is `FSOLNpcGroupTable::LaneRadiusM` (the radius the point fade uses), not the tunnel radius. `LaneTrafficActive` =
`laneRadiusPx >= NPC_LANE_TRAFFIC_ACTIVE_PX x s || cpuTierShipsOfLane > 0` (no hysteresis), with `s` =
`thresholdScale`, which the caller sets to `LaneTunnelQualityScale(QualityPreset)` (P5, the same scale as the tunnel
classes, case 10: Low 10, Medium 4.6, High 2.2, Epic and Cinematic 1); a NaN, infinite or non-positive scale reads as
1, as in `StepTunnelClass`. So the thresholds are Low 24 px, Medium 11.04 px, High 5.28 px, Epic and Cinematic 2.4 px.
**Overlay (P5, pinned):** neither function takes the Lanes overlay state; the flag is written the same with the overlay
on or off, and in 53d the point vertex shader hides an inactive lane's ships **always** (with the overlay off too).
A (0, 0, 0), B (1e10, 0, 0), R 2e6, f 960 (px values relative 1e-12; the camera at (5e9, y, 0) gives 1.92e9 / y px):
- **Scale 1 (Epic):** camera (5e9, 8e8, 0): 2.4 px; (5e9, 7.9e8, 0): 2.4303797 px, active; (5e9, 8.1e8, 0):
  2.3703704 px, inactive; the same with 1 CPU-tier ship: active. Inclusive threshold, asserted directly:
  `LaneTrafficActive(NPC_LANE_TRAFFIC_ACTIVE_PX, 0, 1.0)` true, `LaneTrafficActive(NPC_LANE_TRAFFIC_ACTIVE_PX x (1 -
  1e-9), 0, 1.0)` false, (NaN, 0, 1.0) false, (NaN, 1, 1.0) true.
- **Scaled rows (P5), scale = `LaneTunnelQualityScale(preset)`:**

  | Camera y | px | Low (0, x10) | Medium (1, x4.6) | High (2, x2.2) | Epic (3) / Cinematic (4) |
  |---|---|---|---|---|---|
  | 7.9e7 | 24.303797 | active | active | active | active |
  | 8.1e7 | 23.703704 | inactive | active | active | active |
  | 1.7e8 | 11.294118 | inactive | active | active | active |
  | 1.8e8 | 10.666667 | inactive | inactive | active | active |
  | 3.6e8 | 5.3333333 | inactive | inactive | active | active |
  | 3.7e8 | 5.1891892 | inactive | inactive | inactive | active |
  | 7.9e8 | 2.4303797 | inactive | inactive | inactive | active |
  | 8.1e8 | 2.3703704 | inactive | inactive | inactive | inactive |

  Every inactive cell is active with 1 CPU-tier ship. Inclusive at each scale, asserted directly for presets 0-4 with
  `s = LaneTunnelQualityScale(preset)`: `LaneTrafficActive(NPC_LANE_TRAFFIC_ACTIVE_PX x s, 0, s)` true,
  `LaneTrafficActive(NPC_LANE_TRAFFIC_ACTIVE_PX x s x (1 - 1e-9), 0, s)` false.
- **Bad scale:** NaN, +inf, 0 and -2 read as 1: (2.4303797, 0, s) active and (2.3703704, 0, s) inactive for each.
- camera (-6e8, 0, 0) (beyond end A): 3.2 px (active at scale 1, inactive at High 2.2 = 5.28 px). Camera on the
  segment: +inf, active at every scale. A == B: distance to the point. NaN camera: 0 px, inactive unless a CPU-tier
  ship exists.

### 24. Merge readout and lane search skip closed lanes (case `MergeSkip`, Q30; P6)

```cpp
bool LaneOpenAt(double nowS, double openStartS, double closeS);                      // SOLLaneNetwork
```

- `LaneOpenAt` = `openStart <= now < close`; NaN anywhere -> false. (5, 0, 10) true; (0, 0, 10) true; (10, 0, 10)
  false; (-1, 0, 10) false; (5, -inf, +inf) true. A Draining lane (open now, closing within a leg) is open: its
  readout shows (Q30). The caller sets `FSOLLaneMergeLane::bOpenNow = GetSlotWindow(...) && LaneOpenAt(...)`.
- `SOLLaneTraffic::MergeReadout` and `FindLaneLeg` skip a lane with `bOpenNow == false` exactly as they skip an
  invalid lane today. Two identical lanes from `MakeMergeLane(MakeLaneProfile(6.0e6, 4.0e11, 6.371e6, 3.3895e6), 0, 1,
  A, B, 0, 0)` with A (1.0e11, 2.0e10, 0), B (5.0e11, 2.0e10, 0) (both off the origin, as case 7's frames), the player
  on the axis at the midpoint (3.0e11, 2.0e10, 0) moving along +x: first assert the readout is shown on lane 0 with
  both open (so a hidden result below is the flag's doing); lane 0 closed: lane 1; both closed: hidden. `FindLaneLeg`
  from (2.0e11, 2.0e10, 0) to (4.0e11, 2.0e10, 0): lane 0 (asserted found first); lane 0 closed: lane 1; both closed:
  not found.
- **`CheckLaneLatch` (P6, pinned):** an **Approach** whose latched lane has `bOpenNow == false` returns **`Stale`**
  (never `Release`): the check sits with the "lane gone or replaced" checks, before the target-left and 20-radius
  checks, so a closed lane is Stale even when the target has also left. A **Hold** ignores `bOpenNow` (`Keep` as
  today; 53e decides a held lane that closes; so the open flag is not folded into the lane-usable test that makes a
  replaced lane Stale). The other modes are unchanged (None and Left: `Keep`). Setup: the two
  lanes above, ship (2.0e11, 2.0e10, 0), target (4.0e11, 2.0e10, 0), `leg0 = FindLaneLeg(lanes both open, ship,
  target)` (lane 0, bodies 0 and 1, target inside):
  - both open: Approach -> Keep, Hold -> Keep (baseline, so the results below are the flag's doing);
  - lane 0 closed: Approach -> **Stale**; Hold -> **Keep**; None -> Keep; Left -> Keep;
  - lane 0 closed and the target moved sideways out of the halo to (4.0e11, 2.0e10 + 1.0e9, 0) (1e9 m off the axis,
    far outside the halo of a 6e6 m lane): Approach -> **Stale** (not Release); Hold -> Release (as today, the target left);
  - lane 1 closed, lane 0 open: Approach on `leg0` -> Keep (only its own lane counts);
  - `leg1 = FindLaneLeg` with lane 0 closed (lane 1, asserted); then lane 1 closed as well: Approach on `leg1` ->
    Stale.
- **`LaneAutoDecide` after a Stale Approach (P6, pinned: "dropped, searched again"):** latched leg `leg0`, latched mode
  Approach, lane 0 closed, lane 1 open, input ship and target as above, `bSearch` true, `bIgnoreLanes` false: the
  decision's `Leg.bFound` true and `Leg.Lane` == 1, `Mode` is not `Left` (the order is not ended). Same with
  `bSearch` false: `Leg.bFound` false, `Mode` == None. Both lanes closed, `bSearch` true: `Leg.bFound` false, `Mode`
  == None. Control: lane 0 open, latched Approach: `Leg.Lane` == 0.

### 25. Shader window test and its mirror (case `NpcRail.GpuLaneWindowMirror`, 53c2b; R8; P1)

```cpp
bool SOLNpcTrafficMirror::LaneShipWindowDrawn(const FVector4f& railA, const FVector4f& railB, const FVector4f* rows,
    float timeS);                       // rows: the column's NPC_GROUP_TEXTURE_ROWS texels as uploaded
// HLSL, Shaders/Private/SOLNpcTraffic.ush, statement for statement the same:
// bool SOLNTR_LaneShipDrawn(float4 railA, float4 railB, float4 g4, float4 g5, float4 g6, float t)
```

The float form of case 21 on the GPU inputs. `g6 = (openStart, close, active, 0)` relative to the GPU epoch (case
22). The segment logic is `SOLNTR_LaneShipRelM`'s; the departure (leg) or the next leg's departure (turn: the turn
end, P1, pinned) and the arrival
are built **only from per-epoch constants** (the epoch parts, `tau0` low and `T_leg`'s rounding; never `t`), so the
decision cannot flicker from frame to frame. Parked (`railB.w < 0`): false. Not drivable (`g4.z <= 0`) or `railB.w ==
0`: `g6.x <= t < g6.y`. `SOLNTR_LaneShipRelM` and `LaneShipRelM` are unchanged (the position ignores row 6).
- **Agreement:** rails from `GpuRailTexels` of real profiles (Earth-Moon 4.06e8 m, Mars-Jupiter 7.121 AU, a 128 AU
  lane), windows on epoch boundaries, open and draining, t sampled over [0, `NPC_GPU_RAIL_EPOCH_S`]: the mirror equals
  `LaneShipInSpace` (leg and turn segments; a turn follows the leg after it, P1) except where the double departure or arrival lies within a band tau
  of a window bound (float rounding, a documented limit). *Implementer, confirmed by measurement in 53c2b:* tau = 4 x
  ulp_f(max(T_cyc, |open|, |close|)) + 1e-3 s is the starting value; the measured band and the number of ships it
  affects are reported, not assumed.
- **No flicker:** for each leg, and for each turn together with the leg after it (P1), the decision is the same at
  every sampled t of that span inside one GPU epoch, and on
  both sides of a re-epoch (`GpuRailTexels` at epoch e and e + 1e4 s, windows re-based the same way) for legs outside
  the band above.
- **Window extremes:** empty window: false for every ship; (-FAR, +FAR): true for every non-parked ship, so the point
  layer then draws exactly what it draws today.
- **Active flag:** `g6.z` does not change the result of `LaneShipWindowDrawn` / `SOLNTR_LaneShipDrawn` (the same
  inputs with `g6.z` 0 and 1 give the same answer); the shader starts hiding inactive lanes in 53d, always, with the
  Lanes overlay on or off (P5).
- **CPU point layer (R8):** `ASOLNpcVisuals` draws a Point-tier lane ship only if `LaneShipWindowDrawn` on the same
  uploaded texels is true; `CpuPointHandOver` itself is unchanged.

### 26. Constants (case `Constants`)

`static_assert`s and a test: `NPC_GROUP_WINDOW_ROW < NPC_GROUP_TEXTURE_ROWS`; `NPC_LANE_SHIP_BLOCK_COUNT_DEFAULT ==
4000`; `NPC_LANE_TRAFFIC_ACTIVE_PX == NPC_TUNNEL_REFERENCE_FOCAL_PX / NPC_LANE_FADE_FAR_RADII` (exact in double);
`NPC_LANE_SHIP_PARKED_LEG_S < 0`; `NPC_RAIL_PARKED_GROUP >= NPC_GROUP_TEXTURE_WIDTH`; `NPC_LANE_WINDOW_FAR_S` finite
in float and far beyond the horizon.
