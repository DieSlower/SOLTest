<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# 53a contract: lane network math (test first)

For the 53a test author, from [SDD 53](../SDDs/53-lane-network.md) (D1-D8, the resolved decisions Q1-Q30 of section
16). Values marked **pending O<n>** wait on the user's answer to that open item of SDD 53 section 17 and must not be
pinned until it is answered; every other value is pinned. Pure functions only: signatures and expected values, no
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
| `NPC_LANE_SUN_LOOKBACK_EPOCHS` | int32 | 64 | **pending O10**; invariant: >= 1 |
| `NPC_LANE_NETWORK_HORIZON_EPOCHS` | int32 | 960 | **pending O10**; invariant: x `NPC_LANE_NETWORK_EPOCH_S` > 889 days (the longest leg, SDD 53 6.1) |
| `NPC_LANE_BASE_COUNT` | int32 | 8 | pinned (Q23): base lanes own slots 0-7 |
| `NPC_LANE_OPEN_FADE_REAL_S` | double | 3.0 | pinned (Q9), real seconds; invariant: > 0 |
| `NPC_LANE_TRANSFER_PENALTY_S` | double | 3,600 | pinned (Q12); invariant: >= 0 |
| `NPC_LANE_SHIP_BLOCK_SIZE` | int32 | 100 | **pending O1**; invariant: divides the lane ship count of the default population (400,000) |
| `NPC_TUNNEL_ACTIVATE_PX` | double | 0.5 | pinned (Q17); invariant: equals `NPC_TUNNEL_RADIUS_FADE_GONE_PX` |
| `NPC_TUNNEL_DEACTIVATE_PX` | double | 0.4 | **pending O10** (to tune); invariant: 0 < it < `NPC_TUNNEL_ACTIVATE_PX` |
| `NPC_TUNNEL_FULL_ON_PX` | double | 1.0 | **pending O10** (to tune); invariant: > `NPC_TUNNEL_FULL_OFF_PX` |
| `NPC_TUNNEL_FULL_OFF_PX` | double | `NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX / NPC_TUNNEL_RING_BASE_SPACING_RADII` = 10 / 12 | pinned (Q17, derived: dots and chevrons are alpha 0 below it); invariant: > `NPC_TUNNEL_ACTIVATE_PX` |
| `NPC_TUNNEL_RING_SLOTS_PER_SIDE` | int32 | 160 | pinned (Q19; was 80); full slot mesh 15,380 vertices |
| `NPC_TUNNEL_INSTANCE_POOL` | int32 | 24 | pinned (Q18); invariant: <= `NPC_GROUP_TEXTURE_WIDTH` |
| `NPC_LANE_ROUTE_MAX_HOPS` | int32 | 12 | implementer; invariant: >= 8 |

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

struct FSOLLaneRoute
{
    bool bFound = false;
    double TimeS = 0.0;               // sum of edge times and transfer costs
    int32 HopCount = 0;
    FSOLLaneHop Hops[SOL::NPC_LANE_ROUTE_MAX_HOPS];
};

enum class ESOLLaneSunState : uint8 { Open, Closed };
enum class ESOLTunnelClass : uint8 { Off, Reduced, Full };
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
of 1e11 m on the x axis unless stated; all nodes Planet unless stated.

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
  `NextClosestPairs` fed the positions at the sample time returns the identical set (assert with two times 1 s and
  86,399 s into one epoch). Purity: two calls with the same inputs return bit-identical results.
- `AssignLaneSlots`: `outSlots` has `slotCapacity` entries; slots below `baseCount` are copied from `previousSlots`
  untouched; a partner lane present in `previousSlots` and in `partnerSet` keeps its slot; a slot whose lane left the
  set is freed unless `slotHeld` is 1 there (its tunnel is still fading); the remaining new lanes, in `partnerSet`
  order, take the lowest free slot from `baseCount`; a free slot is `{INDEX_NONE, INDEX_NONE}`.
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
    double& outOpenStartS, double& outCloseS);
bool LegFitsWindow(double departS, double legS, double openStartS, double closeS);
double LaneDepartureStopS(double closeS, double legS);
bool LaneShipLegDrawn(double simTimeS, double legDepartS, double legS, double openStartS, double departStopS);
```

`LaneOpenWindow` (the look-ahead schedule, index 0 = `firstEpoch`): false (outputs NaN) if `simTimeS`'s epoch is
outside the buffer or closed. Otherwise `outOpenStartS` = the start of the run of open epochs that holds it (-inf if
the run reaches the buffer's first entry) and `outCloseS` = the start of the first closed epoch after it, or **the end
of the buffer if none (fail safe: the horizon counts as a close)**.
- E = 86400, first 100, flags {1, 1, 0, 0, 1, 1, 1} (epochs 100-106): t = 100.5 E -> true, -inf, 102 E. t = 104.2 E ->
  true, 104 E, 107 E (horizon). t = 102.5 E -> false. t = 99 E -> false. t = 107 E -> false.

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
`NPC_LANE_ROUTE_MAX_HOPS`: not found.

Synthetic graph (windows open forever, `DirectionAB` (1, 0, 0), end-ring distances 0, so a transfer = the penalty),
5 nodes, edges by index: e0 0-1 10, e1 1-2 10, e2 0-2 25, e3 2-3 5, e4 1-3 30, e5 3-4 1; `startS` 0, w 2e4.
- penalty 0, 0 -> 3: found, 25, hops {(e0, +1), (e1, +1), (e3, +1)}.
- penalty 3, 0 -> 3: 31, same hops (0-2-3 costs 33).
- penalty 6, 0 -> 3: 36, hops {(e2, +1), (e3, +1)} (0-1-2-3 costs 37).
- penalty 0, e3 window closed (`CloseS` = -inf), 0 -> 3: 40, hops {(e0, +1), (e4, +1)}.
- penalty 0, 4 -> 0: 26, hops {(e5, -1), (e3, -1), (e1, -1), (e0, -1)}.
- e5 closed, 0 -> 4: not found. Source == target: found, time 0, no hops. Node out of range: not found.
- Tie: edges 0-1 5, 1-2 5, 0-2 10, penalty 0, 0 -> 2: hops {(e2, +1)} (fewer hops).
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
ESOLTunnelClass StepTunnelClass(ESOLTunnelClass previous, double maxRadiusPx);
```

- `LaneTunnelMaxRadiusPx` = f Rt / d: (1.92e10, 1e7, 960) -> 0.5; (9.6e9, 1e7, 960) -> 1.0; d <= 0 -> +inf; NaN,
  Rt <= 0 or f <= 0 -> 0.
- `StepTunnelClass` (thresholds 0.5 / 0.4 / 1.0 / 10/12; the 0.4 and 1.0 values pending O10, the transitions pinned):
  (Off, 0.49) -> Off; (Off, 0.5) -> Reduced; (Off, 1.0) -> Full; (Reduced, 0.45) -> Reduced; (Reduced, 0.39) -> Off;
  (Reduced, 0.99) -> Reduced; (Reduced, 1.0) -> Full; (Full, 0.84) -> Full; (Full, 0.83) -> Reduced; (Full, 0.39) ->
  Off; any previous with NaN -> Off.
- **Invisible switches:** `NPC_TUNNEL_ACTIVATE_PX == NPC_TUNNEL_RADIUS_FADE_GONE_PX`, and `NPC_TUNNEL_FULL_OFF_PX x
  NPC_TUNNEL_RING_BASE_SPACING_RADII == NPC_TUNNEL_DETAIL_GAP_FADE_GONE_PX` (relative 1e-12).

### 11. `SelectTunnelPool`, `StepOverflowReport` (case `Pool`)

```cpp
int32 SelectTunnelPool(TConstArrayView<double> maxRadiusPx, TConstArrayView<uint8> drawn, int32 poolSize,
    TArray<int32>& outLanes);   // returns the overflow count
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

- `LaneWeight` = tierA x tierB: (10, 10) -> 100 (Earth-Moon); (10, 6) -> 60; (4, 3) -> 12 (Ceres-asteroid); (3, 3) ->
  9; (1, 1) -> 1; negative or NaN tier -> 0. The tier values themselves are layout data, **pending O6**.
- `LaneShipShares`, largest remainder (the sum equals `laneShipCount` exactly; remainder ties to the lower index):
  400,000 over {2, 1, 1}: {200,000, 100,000, 100,000}; 10 over {1, 1, 1}: {4, 3, 3}; non-positive or NaN weights count
  0; all zero: all 0.
- **Population weights of the base lanes** (Mercury-Venus 4, Venus-Earth 20, Earth-Moon 100, Earth-Mars 60, Mars-Jupiter
  12, Jupiter-Saturn 4, Saturn-Uranus 2, Uranus-Neptune 1): 400,000 -> {7,882, 39,409, 197,044, 118,227, 23,645,
  7,882, 3,941, 1,970}; as blocks, 4,000 -> {79, 394, 1,970, 1,182, 237, 79, 39, 20} (block form **pending O1**).
- **Budget unchanged by the lane count:** for weight lists of length 8, 22 and 29, the counts sum to exactly 400,000.

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
  positive, and between 880 and 900 days (the coast at the 290,516.78 m/s cap dominates).

### 14. Pool overflow report and column overflow (case `OverflowReport`)

- `AssignLaneSlots` with capacity 64 - 10 clusters - 16 battles = 38 slots (8 base + 30 partner; the caller's
  computation) and 31 partner lanes: returns 1, and `StepOverflowReport(0, 1)` is true; the next epoch with 30 partner
  lanes: returns 0 and `StepOverflowReport(1, 0)` is true.
- `SelectTunnelPool` as in case 11 (24 of 30).
