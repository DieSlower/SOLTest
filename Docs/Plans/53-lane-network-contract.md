<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# 53a contract: lane network math (test first)

For the 53a test author, from [SDD 53](../SDDs/53-lane-network.md) (D1-D8; the values marked **blocked** wait on the
user's answer to the named open question and must not be pinned until it is answered). Pure functions only: signatures
and expected values, no implementation. Tests in `Source/SOLTest/Tests/LaneNetworkMathTest.cpp`, paths
`SOLTest.LaneNetwork.<Case>` (run with `Tools\RunTests.bat -Build -Filter SOLTest.LaneNetwork`). Everything lives in
`Traffic/SOLLaneNetworkMath.{h,cpp}` (`namespace SOLLaneNetwork`, every function `SOLTEST_API`) and `SOLConstants.h`
(`namespace SOL`). Universe ecliptic metres, sim seconds, double. Inputs are plain arrays; nothing reads the registry.

## Constants (`SOLConstants.h`)

| Name | Type | Value | Status |
|---|---|---|---|
| `NPC_LANE_SUN_CLEARANCE_RADII` | double | 20 | pinned (D5) |
| `NPC_LANE_SUN_REOPEN_RADII` | double | 22 | **blocked (Q6)**; invariant: >= `NPC_LANE_SUN_CLEARANCE_RADII` |
| `NPC_LANE_NETWORK_EPOCH_S` | double | 86,400 | **blocked (Q5)**; invariant: > 0 |
| `NPC_LANE_NETWORK_EPOCH_SAMPLES` | int32 | 5 | implementer; invariant: >= 2 |
| `NPC_LANE_TRANSFER_PENALTY_S` | double | 3,600 | **blocked (Q12)**; invariant: >= 0 |
| `NPC_TUNNEL_ACTIVATE_PX` | double | 0.5 | **blocked (Q17)**; invariant: equals `NPC_TUNNEL_RADIUS_FADE_GONE_PX` |
| `NPC_TUNNEL_DEACTIVATE_PX` | double | 0.4 | **blocked (Q17)**; invariant: 0 < it <= `NPC_TUNNEL_ACTIVATE_PX` |
| `NPC_TUNNEL_INSTANCE_POOL` | int32 | 16 | **blocked (Q18)**; invariant: <= `NPC_GROUP_TEXTURE_WIDTH` |
| `NPC_LANE_ROUTE_MAX_HOPS` | int32 | 8 | implementer; invariant: >= 4 |

Existing constants used (not changed): `SUN_RADIUS_M` 6.957e8, `NPC_LANE_AUTO_TRACK_WALL_RADII` 0.25,
`NPC_LANE_EFFORT_MEAN` 0.75, `NPC_LANE_WALL_SPEED_MPS` 2e4, `NPC_LANE_SPEED_CAP_MPS` 3e5, `NPC_LANE_FLIP_S` 60,
`NPC_LANE_PEAK_ACCEL_MPS2` (1.65 G), `NPC_TUNNEL_END_MARGIN_RADII` 3.

## Types

```cpp
enum class ESOLLaneNodeKind : uint8 { Planet, Moon, Station, City, Asteroid };

struct FSOLLaneNode
{
    ESOLLaneNodeKind Kind = ESOLLaneNodeKind::Planet;
    int32 BodyIndex = INDEX_NONE;
    int32 ParentNode = INDEX_NONE;
    double ClearanceRadiusM = 0.0;
    double SunDistanceKeyM = 0.0;     // semi-major axis about the Sun (Q1)
    double Importance = 0.0;
};

struct FSOLLanePair { int32 NodeA = INDEX_NONE; int32 NodeB = INDEX_NONE; };

struct FSOLLaneEdge
{
    int32 NodeA = INDEX_NONE;
    int32 NodeB = INDEX_NONE;
    double TimeS = 0.0;               // T_edge, same both ways
    bool bOpen = true;
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
```

## Functions and expected values

### 1. `NextClosestPairs` (case `NextClosest`)

```cpp
void NextClosestPairs(TConstArrayView<FSOLLaneNode> nodes, TConstArrayView<FSOLLanePair> baseLanes,
    TArray<FSOLLanePair>& outNew);
```

Only `Planet` nodes take part (both ends). For each planet in index order: the planet with the smallest
`|key_i - key_j|` among those not linked to it by a base lane; ties to the lower index; skipped if none. Each new pair
stored once with `NodeA < NodeB`, in the order first found. `outNew` is reset first.

- Keys {1, 2, 3, 4} (any unit), base {0-1, 1-2, 2-3}: `outNew` = {(0, 2), (1, 3)}.
- Keys {1, 2, 3}, base {0-1, 1-2, 0-2}: empty.
- Keys {1, 3, 5}, base {} : {(0, 1), (1, 2)} (node 1 ties 0 and 2 at 2, picks 0, already stored; node 2 picks 1).
- A `Moon` node is never chosen and chooses nothing.
- **Real values (Q1 answer a):** nodes 0-7 = Mercury .. Neptune with keys 0.38709927, 0.72333566, 1.00000261,
  1.52371034, 5.20288700, 9.53667594, 19.18916464, 30.06992276 (AU, any scale), base = the 7 neighbour pairs:
  `outNew` = {(0, 2), (1, 3), (2, 4), (3, 5), (4, 6), (5, 7)} (Mercury-Earth, Venus-Mars, Earth-Jupiter, Mars-Saturn,
  Jupiter-Uranus, Saturn-Neptune). **Blocked on Q1.**

### 2. `SunSegmentDistanceM` (case `SunClearance`)

```cpp
double SunSegmentDistanceM(const FVector3d& aM, const FVector3d& bM);   // Sun at the origin
```

- A (1e11, 5e9, 0), B (-1e11, 5e9, 0): 5e9.
- A (1e11, 0, 0), B (2e11, 0, 0): 1e11 (endpoint A).
- A (1e11, 0, 0), B (-1e11, 0, 0): 0.
- A == B = (3e10, 4e10, 0): 5e10.
- A (1e11, 0, 0), B (0, 1e11, 0): 1e11 / sqrt(2) = 7.0710678e10 (rel 1e-12).
- Any NaN or infinite input: NaN.

### 3. `StepSunState` (case `SunHysteresis`)

```cpp
ESOLLaneSunState StepSunState(ESOLLaneSunState previous, double clearanceM, double sunRadiusM);
```

Open -> Closed when `clearance < NPC_LANE_SUN_CLEARANCE_RADII x sunRadius`; Closed -> Open when `clearance >
NPC_LANE_SUN_REOPEN_RADII x sunRadius`; otherwise unchanged. NaN clearance: Closed (fail safe).

- Open, 1.39e10, `SUN_RADIUS_M`: Closed (threshold 1.3914e10).
- Open, 1.3914e10: Open (not below).
- Closed, 1.45e10: Closed (blocked on Q6: under 22 x 6.957e8 = 1.53054e10).
- Closed, 1.54e10: Open (blocked on Q6).
- Open, NaN: Closed.

### 4. `EpochClearanceBoundM`, `LaneNetworkEpochIndex` (case `EpochBound`)

```cpp
double EpochClearanceBoundM(TConstArrayView<double> sampleClearancesM, double maxEndpointSpeedMps,
    double sampleSpacingS);
int64 LaneNetworkEpochIndex(double simTimeS, double epochS);
```

- Bound = min(samples) - speed x spacing / 2. {2e10, 1.5e10, 1.8e10}, 5.9e4, 21,600: 1.5e10 - 6.372e8 = 1.43628e10.
- Empty samples, NaN anywhere, speed or spacing negative: -inf (closed, fail safe).
- Index = floor(t / epoch): (0, 86400) -> 0; (86399.9, 86400) -> 0; (86400, 86400) -> 1; (-1, 86400) -> -1;
  (3.15576e9, 86400) -> 36525; epoch <= 0 or NaN input: 0.
- Purity: two calls with the same inputs return bit-identical results.

### 5. `LaneEdgeTimeS` (case `EdgeTime`)

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

### 6. `NodeTransferTimeS` (case `Transfer`) - **blocked on Q12**

```cpp
double NodeTransferTimeS(double endRingDistance1M, double endRingDistance2M, const FVector3d& direction1,
    const FVector3d& direction2, double wallSpeedMps, double penaltyS);
```

= penalty + (0.5 (r1 + r2) theta + |r1 - r2|) / w, theta = the angle between the two unit directions (normalized inside).
- r 2.4e7, 2.4e7, same direction, w 2e4, penalty 3600: 3600.
- r 2.4e7, 2.4e7, opposite directions: 3600 + 2.4e7 pi / 2e4 = 7369.91 s.
- r 2.0e7, 3.0e7, perpendicular: 3600 + (2.5e7 x pi / 2 + 1.0e7) / 2e4 = 6063.50 s.
- Symmetric in (1, 2). A zero or non-finite direction, a non-positive or non-finite speed or distance: +inf (no
  transfer).

### 7. `FindFastestRoute` (case `Dijkstra`)

```cpp
FSOLLaneRoute FindFastestRoute(int32 nodeCount, TConstArrayView<FSOLLaneEdge> edges,
    TConstArrayView<double> transferS, int32 sourceNode, int32 targetNode);
```

**Signature blocked on Q12:** `transferS` here is one flat cost per intermediate node; the recommended Q12 cost
depends on the lane pair (the angle between the two lanes at the node), which needs a per-(node, lane in, lane out)
input instead. The synthetic cases below hold for either form when every transfer at a node costs the same. Closed edges skipped. Ties: lower time, then fewer hops, then the lexicographically lower lane
index sequence. Route longer than `NPC_LANE_ROUTE_MAX_HOPS`: not found.

Synthetic graph, 5 nodes, edges by index: e0 0-1 10, e1 1-2 10, e2 0-2 25, e3 2-3 5, e4 1-3 30, e5 3-4 1.
- transfer all 0, 0 -> 3: found, 25, hops {(e0, +1), (e1, +1), (e3, +1)}.
- transfer all 3, 0 -> 3: 31, same hops (0-2-3 costs 33).
- transfer all 6, 0 -> 3: 36, hops {(e2, +1), (e3, +1)} (0-1-2-3 costs 37).
- transfer 0, e3 closed, 0 -> 3: 40, hops {(e0, +1), (e4, +1)}.
- transfer 0, 4 -> 0: 26, hops {(e5, -1), (e3, -1), (e1, -1), (e0, -1)}.
- e5 closed, 0 -> 4: not found. Source == target: found, time 0, no hops. Node out of range: not found.
- Tie: edges 0-1 5, 1-2 5, 0-2 10, transfer 0, 0 -> 2: hops {(e2, +1)} (fewer hops).
- **Real network** (the 13 planet lanes of SDD 53 4.2 with the SDD 6.1 table's times as inputs, transfers 0): assert
  the hop sequence and that `TimeS` equals the sum of the supplied edge times (relative 1e-12), not day figures:
  Earth -> Saturn = Earth-Mars, Mars-Saturn; Earth -> Neptune = Earth-Mars, Mars-Saturn, Saturn-Neptune; Mercury ->
  Mars = Mercury-Venus, Venus-Mars; Earth -> Jupiter = Earth-Jupiter. **Blocked on Q1 and Q4** (the lane set).

### 8. `LaneTunnelMaxRadiusPx`, `StepTunnelActive`, `SelectTunnelPool` (case `PixelActivation`)

```cpp
double LaneTunnelMaxRadiusPx(double segmentDistanceM, double tunnelRadiusM, double focalRefPx);
bool StepTunnelActive(bool previous, double maxRadiusPx);
int32 SelectTunnelPool(TConstArrayView<double> maxRadiusPx, TConstArrayView<uint8> active, int32 poolSize,
    TArray<int32>& outLanes);   // returns the overflow count
```

- `LaneTunnelMaxRadiusPx` = f Rt / d: (1.92e10, 1e7, 960) -> 0.5; (9.6e9, 1e7, 960) -> 1.0; d <= 0 -> +inf; NaN, Rt <=
  0 or f <= 0 -> 0.
- `StepTunnelActive` (blocked on Q17): false, 0.5 -> true; false, 0.49 -> false; true, 0.45 -> true; true, 0.39 ->
  false; NaN -> false.
- `SelectTunnelPool`: px {3, 0.2, 7, 1}, active {1, 0, 1, 1}, pool 2: outLanes {2, 0} (largest first), overflow 1;
  pool 16: {2, 0, 3}, overflow 0; ties to the lower lane index; `outLanes` reset first; pool <= 0: empty, overflow =
  active count.

### 9. `LaneShipShares` (case `TrafficWeights`)

```cpp
void LaneShipShares(int32 laneShipCount, TConstArrayView<double> weights, TArray<int32>& outCounts);
```

Expected counts by largest remainder (the sum equals `laneShipCount` exactly): 400,000 over {2, 1, 1}: {200,000,
100,000, 100,000}; 10 over {1, 1, 1}: {4, 3, 3} (ties to the lower index); non-positive or NaN weights count 0; all
zero: all 0. (The spawn's hash pick stays as it is; this function is the expected value the spawn's histogram is
checked against within a statistical tolerance in `NpcScaleMathTest`.)
