<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Test contract 51a: thruster math

For the test author of `Source/SOLTest/Tests/ThrusterMathTest.cpp` (`SOLTest.Thruster.<Case>`, one
`IMPLEMENT_SIMPLE_AUTOMATION_TEST` per case below, the pattern of the other files in `Source/SOLTest/Tests/`). Design:
[SDD 51](../SDDs/51-thruster-plume.md) sections 4.2-4.9 and 7. This file gives **signatures, units and expected values
only**; the implementation does not exist yet and must not be consulted. Values marked *parameter* are not pinned
(they wait on the 51e-0 mock-up or on tuning in 51f): pass them as arguments in the test.

**Headers:** `Visuals/SOLThrusterMath.h` (namespace `SOLThruster`), constants in `SOLConstants.h` (namespace
`SOL::Thruster`), `Traffic/SOLLaneTrafficMath.h` for `FSOLLaneThrustState` / `ESOLLaneThrust`. `g0` is
`SOL::STANDARD_GRAVITY_MPS2` (9.80665). Hull frame: X nose, Y right, Z top (UE axes). Tolerances: lengths 1e-3 m,
scalars 1e-4, unless stated.

## Constants (`SOL::Thruster`)

| Name | Value |
|---|---|
| `G_REF` | `SOL::WAR_POWER_PULSE_G` (6.803301 G), exactly |
| `PLUME_MIN_G` | 0.01 |
| `COLOUR_BLUE_END_G`, `COLOUR_YELLOW_G`, `COLOUR_YELLOW_C` | 2.5, 3.0, 0.35 |
| `RIM_BLUE`, `CORE_BLUE` | (0.35, 0.60, 1.00), (0.80, 0.90, 1.00) |
| `RIM_YELLOW`, `CORE_YELLOW` | (1.00, 0.80, 0.35), (1.00, 0.96, 0.82) |
| `RIM_RED`, `CORE_RED` | (1.00, 0.38, 0.10), (1.00, 0.82, 0.55) |
| `LENGTH_EXPONENT`, `BRIGHTNESS_EXPONENT` | 0.8, 0.5 |
| `SHOCK_SPACING_FACTOR`, `SHOCK_BRIGHTNESS_GAIN`, `SHOCK_DECAY`, `SHOCK_ZONE`, `SHOCK_MAX_KNOTS` | 2.0, 0.6, 0.82, 0.8, 8 |
| `TAIL_RADIUS_RATIO` | 0.35 |
| `GLOW_MIN_DRAWN_PX` | 1.0 |
| `BURST_LENGTH_HULL_DIVISOR`, `BURST_REF_G` | 6.0, 1.5 |
| `RIBBON_SAMPLES`, `RIBBON_MAX_AGE_S`, `RIBBON_LENGTH_FACTOR` | 24, 1.0, 3.0 |
| `RIBBON_COUNT_UNLIMITED` | `MAX_int32` |
| `NPC_HULL_LENGTH_M`, `NPC_NOZZLE_RADIUS_M` | 30.0, 1.5 |
| `PLAYER_HULL_LENGTH_M`, `PLAYER_NOZZLE_RADIUS_M` | 20.0, 0.8 |

## Signatures (`SOLThruster`)

```cpp
double PlumeFraction(double gMain);                                  // x in [0, 1]
double PlumeLengthM(double hullLengthM, double gMain);
double PlumeBrightness(double gMain);                                // b
double PlumeColourScalar(double gMain, bool bWarPower);              // c in [0, 1]
struct FSOLPlumeColour { FLinearColor Rim; FLinearColor Core; };
FSOLPlumeColour PlumeColour(double c);
double ShockSpacingM(double nozzleRadiusM, double brightness);
struct FSOLShockKnot { double PositionM = 0.0; double Strength = 0.0; };
int32 ShockKnots(double plumeLengthM, double nozzleRadiusM, double brightness, TArrayView<FSOLShockKnot> out);

FVector3f LaneThrustLocalG(const FSOLLaneThrustState& state, const FVector3d& hullForward, const FVector3d& hullTop);
struct FSOLThrustMean
{
    void Add(const FVector3d& thrustLocalMps2, double dtS);
    FVector3f MeanG() const;
    void Reset();
};
struct FSOLThrustAxes { float Main, Reverse, Right, Left, Up, Down; };
FSOLThrustAxes ThrustAxes(const FVector3f& thrustLocalG);
enum class ESOLBurst : uint8 { Reverse, Right, Left, Up, Down };
FVector3f BurstExhaustDir(ESOLBurst burst);
float BurstStrength(float componentG, float minG, float refG);
double BurstLengthM(double hullLengthM, float strength);          // hull length / 6 x strength, strength clamped to [0, 1]

double FocalRefPx(double hFovDeg, double aspect);
double PixelLength(double lengthM, double distanceM, double focalRefPx);
double LodAlpha(double px, double lowPx, double highPx);
double PlumeMaxDistanceM(double hullLengthM, double focalRefPx, double lowPx);
double GlowDrawnPx(double truePx, double floorPx);

struct FSOLPlumeSample { double EmitTimeS; FVector3f OffsetM; FVector3f ExhaustDir; float DriftMps; float Brightness; };
class FSOLPlumeHistory                                               // fixed capacity RIBBON_SAMPLES, no allocation
{
public:
    void Reset();
    bool Push(const FSOLPlumeSample& sample);                        // false, nothing stored, if any field is non-finite
    int32 Num() const;
    const FSOLPlumeSample& ByAge(int32 i) const;                     // 0 = newest
    bool ShouldSample(double nowS) const;
};
float RibbonDriftMps(double plumeLengthM);
FVector3d RibbonPoint(const FVector3d& shipPositionNow, const FSOLPlumeSample& sample, double nowS);
float RibbonAlpha(double ageS, float brightness);
float RibbonWidthM(double ageS, double nozzleRadiusM);
struct FSOLRibbonCandidate { int32 NpcIndex; double DistanceM; double RibbonPx; bool bHasPlume; bool bRibbonFading; };
void SelectRibbonShips(TConstArrayView<FSOLRibbonCandidate> candidates, double radiusM, int32 maxCount,
    TArray<int32>& outNpcIndices);
int32 RibbonCountForQuality(int32 qualityPreset);
bool PlayerHasRibbon(int32 qualityPreset);

float PlumeLightIntensity(float brightness, float maxIntensity);
FLinearColor PlumeLightColour(double c);

enum class ESOLHullKind : uint8 { NpcFaction0, NpcFaction1, Player };
struct FSOLNozzle { FVector3f PositionM; FVector3f ExhaustDir; float RadiusM; };
struct FSOLHullThrusters { float HullLengthM; int32 NozzleCount; FSOLNozzle Nozzles[2]; };
const FSOLHullThrusters& HullThrusters(ESOLHullKind kind);
bool PlumeAllowed(bool bHullDrawn);                                  // Q27: false for dot-drawn ships
bool PlayerLightEnabled(int32 qualityPreset, bool bVR);              // Q28: VR on, Low off, else on
```

## Cases and expected values

**1. `PlumeLength`** (`PlumeFraction`, `PlumeLengthM`)
- 30 m hull: 0.825 G 5.548, 1 G 6.471, 1.65 G 9.659, 3 G 15.583, `G_REF` 30.0, 9 G 30.0 m. 20 m hull: 3 G 10.388, `G_REF`
  20.0 m. 60 m hull at 3 G: 31.165 m (proportional).
- `PlumeFraction`: 1 G 0.146987, 3 G 0.440962, `G_REF` 1, 9 G 1.
- Off: 0.009 G gives 0; 0.02 G gives > 0. Negative G, NaN or infinite G, a hull length <= 0 or NaN: 0.

**2. `PlumeBrightness`**: 0.825 G 0.34823, 1 G 0.38339, 1.65 G 0.49247, 3 G 0.66405, `G_REF` 1, 9 G 1; 0.009 G, negative
or NaN: 0.

**3. `ColourRamp`** (`PlumeColourScalar`, `PlumeColour`)
- `c`: 0 G 0, 2.5 G 0, 2.75 G 0.175, 3 G 0.35, 4 G 0.52090, 5 G 0.69181, `G_REF` 1, 9 G 1; NaN G 0.
- War power: `PlumeColourScalar(1.5, true)` = 1, `(0, true)` = 1.
- Continuity: over G from 0 to 9 in 0.001 steps, no `|Δc|` above 0.002.
- `PlumeColour`: c = 0 gives (`RIM_BLUE`, `CORE_BLUE`); 0.35 the yellow pair; 1 the red pair; 0.175 rim (0.675, 0.70,
  0.675) core (0.90, 0.93, 0.91); 0.675 rim (1.0, 0.59, 0.225) core (1.0, 0.89, 0.685); c < 0 as 0, c > 1 as 1; alpha 1.

**4. `ShockKnots`** (capacity `SHOCK_MAX_KNOTS`)
- `ShockSpacingM(1.5, 0.49247)` = 3.88645; `(0.8, 0.66405)` = 2.23749; `(0.8, 1.0)` = 2.56.
- NPC (L 9.659011, r 1.5, b 0.492473): 1 knot at 3.88645, strength 1.
- Player 3 G (L 10.388462, r 0.8, b 0.664050): 3 knots at 2.23749, 4.47498, 6.71246, strengths 1, 0.82, 0.6724.
- Player War power (L 20, r 0.8, b 1): 6 knots at 2.56, 5.12, 7.68, 10.24, 12.80, 15.36, last strength 0.37074.
- Plume length 0: 0 knots. A smaller `out` view than the knot count: writes and returns `out.Num()`.

**5. `LaneThrust`** (`LaneThrustLocalG`, basis forward (1, 0, 0), top (0, 0, 1) unless stated; right = top x forward)
- Accelerate, `AccelMps2` 1.65 g0, Direction (1, 0, 0): (1.65, 0, 0).
- Brake, 1.65 g0, Direction (-1, 0, 0), basis forward (-1, 0, 0), top (0, 0, 1): (1.65, 0, 0).
- Coast or Flip, 1.65 g0, any Direction: (0, 0, 0).
- Turn, 1 g0, Direction (0.6, 0.8, 0): (0.6, 0.8, 0); Direction (-0.6, -0.8, 0): (-0.6, -0.8, 0).
- Non-finite `AccelMps2` or Direction: (0, 0, 0).

**6. `PlayerThrust`** (`FSOLThrustMean`, `ThrustAxes` for the main component)
- Add((3 g0, 0, 0), 0.004) and Add((0, 0, 0), 0.012): `MeanG` (0.75, 0, 0).
- Add((`G_REF` g0, 0, 0), 0.01) and Add((1.5 g0, 0, 0), 0.01): (4.151651, 0, 0).
- dt <= 0 or non-finite dt or vector: ignored. No samples, or after `Reset`: (0, 0, 0).
- `ThrustAxes((0.5, -0.3, 0.2))`: Main 0.5, Reverse 0, Right 0, Left 0.3, Up 0.2, Down 0.
- `ThrustAxes((-1.5, 0, -2))`: Main 0, Reverse 1.5, Down 2, the rest 0. Pure reverse gives Main 0.
- Non-finite component: that component's axes 0.

**7. `RibbonHistory`**
- Empty: `Num` 0, `ShouldSample(any)` true. After a push at t = 0: `ShouldSample(0.03)` false, `ShouldSample(0.0417)` true
  (interval `RIBBON_MAX_AGE_S / RIBBON_SAMPLES` = 0.041667 s).
- 30 pushes at t = k / 24, k = 1..30: `Num` 24, `ByAge(0).EmitTimeS` 30 / 24, `ByAge(23).EmitTimeS` 7 / 24, ages ordered.
- `Reset`: `Num` 0. A push with a NaN offset: returns false, `Num` unchanged.
- `RibbonDriftMps(30)` 90; `(9.659011)` 28.977.
- `RibbonPoint`: ship (1000, 2000, 3000), sample emit 10.0, offset (-15, 0, 0), exhaust (-1, 0, 0), drift 90, now 10.5:
  (940, 2000, 3000). Now 11.0: (895, 2000, 3000), 90 m from the offset (3x a 30 m plume). Now 9.0 (before emission,
  age read as 0): (985, 2000, 3000).
- `RibbonAlpha`: (0, 0.8) 0.8; (0.5, 0.8) 0.4; (1.0, 0.8) 0; (1.5, 0.8) 0; (-0.1, 0.8) 0.8 (negative age read as 0).
- `RibbonWidthM`: (0, 1.5) 1.5; (0.5, 1.5) 2.25; (1.0, 1.5) 3.0; (2.0, 1.5) 3.0.

**8. `PixelLod`**
- `FocalRefPx(90, 16/9)` 960; `(90, 4/3)` 720; `(60, 16/9)` 1662.7688 (1e-3).
- `PixelLength(9.659011, 9272.65, 960)` 1.0; `(30, 28800, 960)` 1.0; `(20, 19200, 960)` 1.0; distance <= 0 or NaN: 0.
- `LodAlpha` with *parameter* band (0.5, 1.0): 0.4 gives 0, 0.5 0, 0.75 0.5, 1.0 1, 3 1; band (6, 8): 7 gives 0.5.
  Low >= high: 1 if px >= high, else 0.
- `PlumeMaxDistanceM(30, 960, 0.5)` 57600; `(30, 960, 1)` 28800; lowPx <= 0: 0.
- `GlowDrawnPx(0.9, 1.0)` 1.0; `(3.0, 1.0)` 3.0 (floor `GLOW_MIN_DRAWN_PX` = 1 px, Q33).

**9. `NearestRibbons`** (`SelectRibbonShips`, *parameter* radius 5000 m)
- Candidates (index, distance, px, plume, fading): (5, 1000, 40, yes, no), (2, 2000, 40, yes, no), (9, 4000, 10, no,
  yes), (7, 3000, 20, no, no), (1, 6000, 50, yes, no), (3, 5000, 5, yes, no).
- `maxCount` `RIBBON_COUNT_UNLIMITED`: [2, 5, 9, 3] (px descending, ties by lower index; 7 excluded, no plume and not
  fading; 1 beyond the radius; 3 at exactly the radius included).
- `maxCount` 2: [2, 5]. `maxCount` 0 or negative: []. The output array is cleared first.

**10. `QualityScaling`**: `RibbonCountForQuality` 0 → 0, 1 → 8, 2 → 16, 3 → 32, 4 → `RIBBON_COUNT_UNLIMITED`; -1 as 0,
7 as 4 (Cinematic is `RIBBON_COUNT_UNLIMITED`, no cap at all, Q29). `PlayerHasRibbon` false at 0, true at
1-4.

**11. `Constants`**: `G_REF == SOL::WAR_POWER_PULSE_G`; `COLOUR_BLUE_END_G < COLOUR_YELLOW_G < G_REF`; 0 <
`COLOUR_YELLOW_C` < 1; `RIBBON_SAMPLES` 24 and within 16-32; `RIBBON_MAX_AGE_S` 1; `TAIL_RADIUS_RATIO` 0.35;
`HullThrusters(NpcFaction0)` and `(NpcFaction1)`: hull 30 m, 1 nozzle, radius 1.5 m, exhaust (-1, 0, 0);
`HullThrusters(Player)`: hull 20 m, 2 nozzles at (-8.0, +-2.2, -0.2) m (0.05 m), radius 0.8 m, exhaust (-1, 0, 0).

**12. `MaterialMirror`** (51b, after the `.ush` exists): the HLSL mirror of `PlumeColour`, `PlumeBrightness` and
`ShockKnots` equals the C++ functions at c = 0, 0.175, 0.35, 0.675, 1 and G = 1, 3, `G_REF`.

**13. `GasBursts`**
- `BurstExhaustDir`: Reverse (1, 0, 0), Right (0, -1, 0), Left (0, 1, 0), Up (0, 0, -1), Down (0, 0, 1) (the gas leaves
  opposite to the thrust).
- `BurstStrength` with *parameters* (0.05, 2.0): 0.04 gives 0, 0.05 0, 1.025 0.5, 2.0 1, 6.0 1; negative or NaN 0;
  refG <= minG: 0.
- Independence: `ThrustAxes((2, 0, 0))` has every burst axis 0; `ThrustAxes((-1, 0, 0))` has Main 0.
- `BurstLengthM(30, 1)` 5.0; `(30, 0.5)` 2.5; `(20, 1)` 3.3333; strength above 1 as 1, below 0 or NaN: 0 (hull length / 6,
  clamped, Q32). `BurstStrength` reference is `BURST_REF_G` 1.5 G in game (the test passes it as a parameter).
- `PlumeAllowed(true)` true, `PlumeAllowed(false)` false: dot-drawn ships get no plume and no burst (Q27).

**14. `PlayerLight`**: `PlumeLightIntensity(0.5, 1000)` 500; `(0, 1000)` 0; brightness above 1 as 1, below 0 as 0.
`PlumeLightColour(c)` equals `PlumeColour(c).Rim` at c = 0, 0.35, 0.6, 1.
`PlayerLightEnabled` (Q28): quality 0 (Low) false with `bVR` false; true at 1-4 with `bVR` false; true at every quality
with `bVR` true (VR overrides Low).
