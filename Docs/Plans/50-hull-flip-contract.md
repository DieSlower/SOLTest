<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Issue #50: drawn hull attitude and flip side, test contract (from the test author)

The test author wrote this from the user's 2026-11-18 decisions on SDD 50 section 16's open question (the rendered
hull's roll). The tests are in `Source/SOLTest/Tests/NpcHullFlipTest.cpp` (`SOLTest.Npc.HullFlip.*`), written test
first. They do not compile until the API below exists. All new declarations go in `Source/SOLTest/Traffic/SOLLaneTrafficMath.h`.
Frames are universe ecliptic unless a line says otherwise.

## Decided by the user (2026-11-18)

1. **Same plane for both flips.** The inbound flip turns in the same plane as the outbound one. The attitude is held
   through the brake and the U-turn, and there is no extra roll. The flip side (top-over or bottom-over) is fixed per
   key. This is the held-attitude reading the tests already pin; the analysis below explains why it is the only
   no-roll option.
2. **The flip window grows with ship length** so that the end G stays under the ship's limit. The limit is at most
   1 G, smaller for bigger ships and at least 0.63 G at 2 km. The window is
   `LaneFlipDurationS(L) = max(NPC_LANE_FLIP_S, the shortest T with (1.5 pi / T)^2 x L / 2 <= LaneFlipEndAccelLimitMps2(L))`.
   The earlier assumption that the flip stays 60 s whatever the length is withdrawn.

## How the flip duration meets the leg timing (open item for the implementer)

The flip window `T_f` is the zero-thrust coast centred on `T_leg / 2` (D21). It feeds the leg profile
(`MakeLegProfile(kappa, effort, legM, flipS)`: the peak, `t_a`, the coast and so `T_leg` and the cycle), the flip
fraction (`LaneHullFlipFraction`), the "a leg of at most w T_f does not burn" rule, the wake bound, the AUTO hold's mean
profile and the HLSL mirror. A per-ship duration therefore means a **per-ship window**: each ship's profile is built at
spawn with its own `flipS`, and every later evaluation of that ship (CPU math, rail, GPU mirror) must use the same
`flipS`.

**Today no ship length reaches the lane math.** `FSOLLaneShip` (P0-P4) has no length. `FSOLNpcRail` is a packed
32-byte GPU struct (`static_assert`, two float4 texels) with no length or duration field. Every NPC hull is the single
placeholder `SOL::NPC_HULL_LENGTH_M` (30 m), whose duration is exactly `NPC_LANE_FLIP_S` (60 s). So **today every ship's
window is the 60 s constant, and the lane math, rails and mirror need no change for this decision.**

How a per-ship length or duration would be carried (a rail field, a per-faction or per-group table, or a duration
derived from the key) is not decided here. The implementer must not invent it: raise it when ships get real lengths.
The tests pin only the pure functions, plus `LaneFlipDurationS(NPC_HULL_LENGTH_M) == NPC_LANE_FLIP_S`. That keeps the
current lane tests, which use the 60 s window, valid.

## Background: why the flip side cannot fix the inbound roll (resolved by decision 1)

Clause 3 as worded ("pick the sweep side, toward T or toward -T, so the body top at the flip's end best matches the
inbound leg's roll") can't tell the two sides apart. A rotation by pi about an axis and by -pi about the same axis are
the same rotation, so both sides end in the same attitude: nose -d and body top -T_out.

The real mismatch is elsewhere. The top carried out of the outbound flip is -T_out, but the inbound leg's own top
`LaneShipTopVector(key, -Axis, Up)` = cos(theta) Up - sin(theta) Right is a different vector, off by a key-dependent
angle. These four requirements together leave only one option:

- each flip is a rigid half turn about an axis perpendicular to the heading;
- the whole attitude is held from the flip's end through the brake, the U-turn and the next leg's burn;
- the cycle is periodic (no stored state);
- nothing snaps.

That option is for **the inbound flip to turn in the same plane as the outbound one** (its nose through +-T_out, not
through the inbound `LaneShipTopVector`). The proof: two half turns about axes in the plane across the lane make a
rotation about the lane axis by twice the angle between the two axes, so they must be parallel.

The alternative is a slow roll about the held forward somewhere, for example in the U-turn. That would let the inbound
flip go toward the inbound T, but it breaks "attitude held through the U-turn". The user chose the same-plane option
(decision 1), which the tests pin.

So `LaneFlipSide` is tested only for being a pure, repeatable function of the key that takes both values. The
outbound flip's nose passes through `side x T_out` at the window's middle. No "best match" rule is pinned.

## API the tests call

```cpp
// A lane ship's drawn hull attitude: unit Forward (the nose) and unit Top (the body's up), orthogonal
struct FSOLLaneHullBasis
{
    FVector3d Forward = FVector3d::UnitX();
    FVector3d Top = FVector3d::UnitZ();
};

namespace SOLLaneTraffic
{
    // +1 when the ship's mid-leg (outbound) flip swings its nose toward its own top T, -1 toward -T; a pure function of
    // the key (the NPC index), nothing stored; both values occur over the keys
    SOLTEST_API int32 LaneFlipSide(uint32 laneShipKey);

    // The ship's drawn attitude at time t. Forward is bit-identical to LaneHullForward(profile, frame, ship, key, t) at
    // every t, which itself becomes cos(pi f) d + side sin(pi f) T on the outbound leg (side = LaneFlipSide(key)).
    // Top is unit, orthogonal to Forward, continuous over the whole cycle and periodic with it
    SOLTEST_API FSOLLaneHullBasis LaneHullBasis(const FSOLLaneProfile& profile, const FSOLLaneFrame& frame,
        const FSOLLaneShip& ship, uint32 laneShipKey, double timeS);

    // The drawn rotation (MakeFromXZ style): maps +X to forward normalised and +Z to top made orthogonal to forward
    // (Gram-Schmidt); a proper rotation, so +Y = Z x X. Frame-agnostic: the visuals convert both vectors with
    // SOLRender::EclipticToUnreal first. A top that is zero, non-finite or parallel to forward falls back to a
    // perpendicular chosen from forward alone (still unit, finite, X -> forward). No singularity at any heading (today's
    // FindBetweenNormals flips at heading -X)
    SOLTEST_API FQuat HullRotation(const FVector3d& forward, const FVector3d& top);

    // Peak rotational (centripetal) acceleration at a ship's ends in the flip: (1.5 pi / flipS)^2 x shipLengthM / 2
    // (the smoothstep's peak turn rate squared times half the length); 0 for a non-positive or non-finite length or
    // flipS
    SOLTEST_API double LaneFlipEndAccelMps2(double shipLengthM, double flipS = SOL::NPC_LANE_FLIP_S);

    // A ship's limit on that acceleration: finite, > 0, at most 1 G (9.80665 m/s^2), non-increasing with length and
    // strictly lower for a 10 km ship than a 10 m one (bigger ships turn slower); any input gives a value in (0, 1 G]
    SOLTEST_API double LaneFlipEndAccelLimitMps2(double shipLengthM);

    // A ship's flip window (decision 2): max(NPC_LANE_FLIP_S, 1.5 pi sqrt(shipLengthM / (2 x
    // LaneFlipEndAccelLimitMps2(shipLengthM)))), the shortest window at or above 60 s whose end acceleration stays at or
    // under the limit; NPC_LANE_FLIP_S for a non-positive or non-finite length
    SOLTEST_API double LaneFlipDurationS(double shipLengthM);
}
```

## What the tests pin

- **HullRotationNoSingularity:** unit, finite quaternion; X maps to forward and Z to the orthogonalised top (1e-9).
  Headings include -X, -X tilted by 1e-3 down to 1e-12 rad, +-X, +-Y, +-Z and 200 random ones. A skewed or scaled top
  gives the same rotation. Along great circles through -X and +Z with a continuous top, consecutive rotations differ by
  at most the step angle. Degenerate tops still give a finite rotation with X -> forward. On a lane along ecliptic -X
  (Unreal -X) the drawn rotation is continuous through the flip.
- **BasisHeldOutsideWindow:** on lanes along an ecliptic diagonal (capped Earth - Mars), an inclined line (uncapped
  Earth - Moon) and ecliptic -X (Earth - Moon), plus a halo ship:
  - Forward == `LaneHullForward` bit for bit; basis orthonormal (1e-9);
  - Forward = d before the window and -d after it;
  - on the outbound leg before the window, Top = `LaneShipTopVector(key, +Axis, Up)` (1e-9), so the drawn hull carries
    the ship's own roll (the inbound leg's top is whatever the held attitude carries in, -T_out for a pure pitch);
  - Top constant from the leg's start to the window and from the window's end through the brake, the U-turn and the
    next leg's burn (the whole attitude is held, clause 5);
  - periodic over the cycle.
- **FlipRigidHalfTurn:** in the window, the pitch axis A = unit(F(start) x F(middle)) is perpendicular to d, F . A = 0,
  Top . A constant (a rigid rotation about A), angle(d, F) = pi f, F(end) = -F(start), Top(end) = 2 (Top0 . A) A - Top0.
  Per 0.25 s step, Forward and Top turn by at most 1.5 pi / T_f x dt.
- **FlipSide:** `LaneFlipSide` returns only +-1, gives the same value on repeated calls, and both values occur over keys
  0-999 and over keys 65,536 apart. On every outbound leg the nose at the window's middle is side x
  `LaneShipTopVector(key, +Axis, Up)` (1e-6). The lane samples contain ships of both sides.
- **ContinuousOverCycle:** no jump in Forward or Top anywhere in a whole cycle (a recursive bisection down to 1e-4 s
  with a 1e-3 rad threshold), for burning ships and for a ship with no brake (velocity facing in the turns).
- **FlipEndAccel:**
  - the formula holds (1e-12), including the default flipS and a 120 s window;
  - bad inputs give 0;
  - the limit stays in (0, 1 G], is non-increasing from 1 m to 20 km and is strictly lower at 10 km than at 10 m;
  - a 2 km ship and the 30 m placeholder hull are under their limits;
  - the angular rate measured from `LaneHullBasis` matches 1.5 pi / T_f (1 %).

  **Note:** the 2 km figure is not tiny. (1.5 pi / 60)^2 x 1000 m = 6.17 m/s^2 = 0.63 G, and the 60 s flip reaches 1 G
  at about 3.2 km. So the limit's size scaling must leave at least 0.63 G at 2 km for this test to pass. Above the
  limit the window grows (`FlipDuration`).
- **FlipDuration:** `LaneFlipDurationS`, sampled from 10 m to 20 km and on bad lengths:
  - never below `NPC_LANE_FLIP_S`;
  - exactly `NPC_LANE_FLIP_S` for 10 m, 30 m (the placeholder hull), 500 m and 2 km, and for bad lengths;
  - non-decreasing with length, strictly increasing wherever it is above 60 s, and above 60 s at 20 km (where a 60 s
    flip would be about 6.3 G);
  - the end acceleration at the returned window never exceeds the limit (1e-9 relative);
  - wherever the window is above 60 s it is the shortest one: the end acceleration equals the limit (1e-9);
  - it matches the closed form `max(T_f, 1.5 pi sqrt(L / (2 limit)))` (1e-12).
- **BadLanes:** for an invalid frame or a non-drivable profile the basis is finite and orthonormal, and Forward ==
  `LaneHullForward`.

## Existing tests this contract invalidates (re-pin to `side x T` on the outbound leg and to the outbound plane on the inbound leg, decision 1)

- `SOLTest.NpcTrafficMath.HullTopVector` (`NpcTrafficMathTest.cpp` about lines 3198-3242): the hull equals `top` at
  the window's middle, and `T . hull = sin(pi f) >= 0` on **both** legs with `T = LaneShipTopVector(key, travel, Up)`.
  The pure `LaneShipTopVector` checks (lines 3063-3175) stay valid.
- `SOLTest.NpcTrafficMath.HullFlip` (about lines 2790-2800 and 2879): `NpcTrafficRefHull(travel,
  LaneShipTopVector(key, travel, Up), f)` in the window and `expectedMiddle = LaneShipTopVector(...)` on both legs.
- `SOLTest.NpcTrafficMath.ThrustState` (about line 2046): `expectedHull = NpcTrafficRefHull(travel,
  LaneShipTopVector(key, travel, frame.Up), f)`.
- `SOLTest.NpcRail.LaneHullForward` (`NpcRailTest.cpp` about lines 1682-1790): the rail hull equals `LaneShipTopVector`
  at the middle on both legs, and `key + 1` gives that key's top.
- `SOLTest.NpcRail.FullTierHandoff` (`NpcRailTest.cpp` about line 2042): an expected hull built from
  `LaneShipTopVector(npcIndex, epochLane.Axis, ...)`. It breaks for side -1 ships if the sample is inside the window.
- Line numbers come from grep and are approximate.
- These comparisons survive unchanged because they compare one path with another: rail vs math, thrust state and
  `LaneShipMotion` vs `LaneHullForward`, and the CPU mirror.

## Added after the review (2026-11-18)

The user decided the Full-tier roll (SDD 50 sections 11 and 16): a Full-tier lane ship is handed off and steered with
its lane top (`SOLNpcRail::HullUpHint(motion, radialUp)`: the motion's `LaneHullTop`, else the radial hint
unchanged), and every Full ship is drawn with its attitude's up. A non-lane ship's roll pop at demotion is tech debt.
Tests added with the fixes (written after the code, small pure helpers):

- **DrawRotation:** `SOLNpcAi::HullDrawRotation(heading, top, faction)`: faction 1 is `HullRotation`; faction 0 maps
  its cone axis +Z to the heading and +X to minus the orthogonalised top (a quarter turn about +Y), at -X too.
- **TierTopAndPromotion:** `LaneShipMotion` carries `LaneHullBasis` bit for bit; `HullUpHint` is the lane top for a
  lane ship and the radial hint unchanged otherwise; the Mid-drawn and the promoted Full-drawn rotations agree within
  1e-6 rad across the cycle for both factions.
- `SOLTest.NpcRail.FullTierHandoff` now also checks the drawn top at promotion (0.01 degrees), the whole attitude's
  per-frame turn, the up's lag through the flip (15 degrees) and at demotion (3 degrees), and the flip plane measured
  from `SOLNpcRail::Motion` (modulo pi, through the hand-off top).
