<!-- SOLTest / Copyright © 2026 Acid Rain Studios LLC -->

# Test contract 51a: thruster math

For the test author of `Source/SOLTest/Tests/ThrusterMathTest.cpp` (`SOLTest.Thruster.<Case>`, one
`IMPLEMENT_SIMPLE_AUTOMATION_TEST` per case below, the pattern of the other files in `Source/SOLTest/Tests/`). Design:
[SDD 51](../SDDs/51-thruster-plume.md) sections 4.2-4.9 and 7. This file gives **signatures, units and expected values
only**; the implementation does not exist yet and must not be consulted. Values marked *parameter* are not pinned
(they wait on the 51e-0 mock-up or on tuning in 51f): pass them as arguments in the test.

**R10 one-piece flame (SDD 51 section 11.2, 2026-11-22): this file is the spec for a separate test author (part
51e2), who reads this file and SDD 51 sections 11.2-11.3 only, never the implementation.** Case status: 1-6, 8-10,
13-15 kept; 7, 11, 18, 19 rewritten below; 12 extended; 16 kept (the user kept the Niagara hybrid, R10-Q6); 17 deleted (replaced by 21); 20-24 new (24 is the R10 part of `RibbonRendererMath`). R10-Q1..Q8 are RESOLVED (SDD 11.3): realistic bend, tube cross-section, quality-scaled pixel vanish size
(Low 10 px, Medium 4.6, High 2.2, Epic / Cinematic 1 px), 0.25 s spool-down, glow kept with a 1 px minimum, hybrid
renderer (both must match, tube applies to both), Q5 (ring or analytic spine) is the implementer's call. The exhaust speed `v_e` and the shutdown time are *parameters*, passed as arguments, never read from a constant in a test.

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
| `TAIL_RADIUS_RATIO` | ~~0.35~~ deleted by R10 (the cone is gone) |
| `FLAME_TAIL_WIDTH_RATIO` | 0.1 (R10: the flame's width at `L_p` over its width at the nozzle; user) |
| `FLAME_LENGTH_FACTOR` | 3.0 (`L_f = 3 L_p`; replaces `RIBBON_LENGTH_FACTOR`, same value) |
| `FLAME_POINTS` | 25 (spine points per flame; replaces `RIBBON_POINTS_PER_TRAIL`, same value) |
| `FLAME_HISTORY_SAMPLES`, `FLAME_SAMPLE_INTERVAL_S` | 8, 1/120 s (pose ring; minimum push spacing) |
| `FLAME_EXHAUST_SPEED_MPS`, `FLAME_SHUTDOWN_S` | start values 2000, 0.25 (resolved start values, R10-Q1 / Q4, tuned in 51f): not pinned, only > 0 |
| `FLAME_STREAK_TAIL_STRENGTH` | start value 0.8 (streak depth in the tail; not pinned, a *parameter* in case 23) |
| `GLOW_MIN_DRAWN_PX` | 1.0 |
| `BURST_LENGTH_HULL_DIVISOR`, `BURST_REF_G` | 6.0, 1.5 |
| `RIBBON_SAMPLES`, `RIBBON_MAX_AGE_S`, `RIBBON_LENGTH_FACTOR` | ~~24, 1.0, 3.0~~ deleted by R10 (see the `FLAME_*` rows) |
| `RIBBON_POINTS_PER_TRAIL` | ~~25~~ renamed `FLAME_POINTS` by R10 |
| `RIBBON_NIAGARA_PX_FACTOR` | 8.0 (Niagara threshold = factor x `V`; start value, tunable; 51e, R7) |
| `RIBBON_NIAGARA_POOL_TRAILS` | 16 (physical Niagara trail components, the player's 2 included; a pool limit, not an LOD rule) |
| `RIBBON_TEXELS_PER_TRAIL`, `RIBBON_TRAILS_PER_BLOCK` | 34, 32 (strip pool layout since the 51e3 end caps; texels >= 27 drawn points + 1 header + 6 width texels) |
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

// R10: a sample is a nozzle pose; DriftMps is deleted
struct FSOLPlumeSample { double EmitTimeS; FVector3f OffsetM; FVector3f ExhaustDir; float Brightness; };
class FSOLPlumeHistory                                               // R10: fixed capacity FLAME_HISTORY_SAMPLES
{
public:
    void Reset();
    bool Push(const FSOLPlumeSample& sample);                        // false, nothing stored, if any field is non-finite
    int32 Num() const;
    const FSOLPlumeSample& ByAge(int32 i) const;                     // 0 = newest
    bool ShouldSample(double nowS) const;                            // R10: interval FLAME_SAMPLE_INTERVAL_S
};
// R10: RibbonDriftMps, RibbonPoint, RibbonAlpha, RibbonWidthM and BuildRibbonPoints are deleted
// R10 one-piece flame (SDD 51 section 11.2; cases 20-23). arcM = s, metres from the nozzle along the flame
double FlameLengthM(double plumeLengthM);                            // L_f = FLAME_LENGTH_FACTOR x L_p
double FlameWidthM(double arcM, double plumeLengthM, double nozzleRadiusM);   // full width W(s), unfloored
double FlameAlongAlpha(double arcM, double flameLengthM);            // 1 - s / L_f, clamped
double FlameTurnOffsetM(double arcM, double turnRateRadS, double exhaustSpeedMps, double nozzleOffsetM);
int32 BuildFlamePoints(const FSOLPlumeHistory& history, const FSOLPlumeSample& head, const FVector3d& shipPositionNow,
    double plumeLengthM, double nozzleRadiusM, double exhaustSpeedMps, double fade, const FVector3d& anchorM,
    TArrayView<FSOLRibbonPoint> outPoints);                          // head.EmitTimeS is now; returns the points written
// R10 flame pixel envelope (C++ reference of SOLTH_FlamePixel's along-arc terms; float copies in SOLThrusterMirror.h)
double FlameCoreWeight(double radialFraction);                       // (sqrt(1 - r^2))^PLUME_CORE_POWER, r clamped to [0, 1]
double FlameKnotIntensity(double arcM, double radialFraction, double plumeLengthM, double nozzleRadiusM,
    double brightness);                                              // the nearest ShockKnots knot's rhombus x strength
double FlameStreakDepth(double arcM, double plumeLengthM, double bodyDepth, double tailDepth);  // smoothstep body -> tail
double FlameSharpness(double trueWidthM, double drawnWidthM, double distanceM, double focalPx);  // CPU copy of SOLTH_FlameSharpness (51e3), float mirror SOLThrusterMirror::FlameSharpness; test SOLTest.Thruster.FlameSharpness
// ASOLThrusterRibbons::SphereTouchesFrustum(frustum, bHasFrustum, centreM, radiusM): pure test behind IsOnScreen (true without a frustum, negative or NaN radius read as 0); test SOLTest.Thruster.FlameOnScreen
// R10 renderer helpers (case 24): RibbonNominalLengthM deleted (FlameLengthM of the held L_p); RibbonTrailReleasable
bool RibbonTrailReleasable(bool bFedThisFrame, int32 sampleCount, double newestBurningAgeS, double flameLifeS,
    double pixelFade);
// Part 51e (SDD 51 section 11.1, R1-R5): no distance or count cap; RibbonCountForQuality and
// RIBBON_COUNTS_BY_QUALITY are removed
bool RibbonShown(double ribbonPx, double vanishPx, bool bHasPlume, bool bRibbonFading);
double RibbonVanishPx(int32 qualityPreset);                          // V: RIBBON_VANISH_PX_BY_QUALITY
double RibbonPixelLength(double plumeLengthM, double distanceM, double focalRefPx);   // PixelLength(3 L_p, d, f)
double RibbonPixelFade(double ribbonPx, double vanishPx);            // 0 at or below V, linear, 1 from 4 V
double RibbonDrawnWidthM(double widthM, double distanceM, double focalPx, double minPx);  // the 1 px floor
double RibbonReachM(double hullLengthM, double focalRefPx, double vanishPx);         // f x 3 L_hull / V
// Part 51e hybrid renderer (SDD 51 R7): Niagara for the player and the large on-screen NPC ribbons, the GPU strip
// pool for the rest; the split is by pixel size only
double RibbonNiagaraMinPx(int32 qualityPreset);                      // RIBBON_NIAGARA_PX_FACTOR x RibbonVanishPx
struct FSOLRibbonShown { int32 NpcIndex = INDEX_NONE; double RibbonPx = 0.0; };
void SplitRibbonRenderers(TConstArrayView<FSOLRibbonShown> shown, double niagaraMinPx, int32 niagaraFreeTrails,
    TArray<int32>& outNiagaraNpcIndices, TArray<int32>& outPoolNpcIndices);
struct FSOLRibbonPoint { FVector3f PositionM = FVector3f::ZeroVector; float Alpha = 0.0f; float WidthM = 0.0f; };
// R10: BuildRibbonPoints deleted (BuildFlamePoints above); PackRibbonTexels takes the flame header (case 18)
void PackRibbonTexels(TConstArrayView<FSOLRibbonPoint> points, float colourScalar, float brightness,
    float plumeLengthM, float nozzleRadiusM, TArrayView<FVector4f> outTexels);   // outTexels.Num() == RIBBON_TEXELS_PER_TRAIL
double NpcThrustGateM(double hullLengthM, double nozzleRadiusM, double focalRefPx, bool bFlames,
    double vanishPx);                                                // case 19, R10: the glow term replaces the cone's

float PlumeLightIntensity(float brightness, float maxIntensity);
FLinearColor PlumeLightColour(double c);

enum class ESOLHullKind : uint8 { NpcFaction0, NpcFaction1, Player };
struct FSOLNozzle { FVector3f PositionM; FVector3f ExhaustDir; float RadiusM; };
struct FSOLHullThrusters { float HullLengthM; int32 NozzleCount; FSOLNozzle Nozzles[2]; };
const FSOLHullThrusters& HullThrusters(ESOLHullKind kind);
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

**7. `RibbonHistory`** (rewritten for R10: a pose ring, no drift)
- Empty: `Num` 0, `ShouldSample(any)` true. After a push at t = 0: `ShouldSample(0.008)` false, `ShouldSample(0.0084)`
  true (interval `FLAME_SAMPLE_INTERVAL_S` = 1/120 = 0.0083333 s).
- 12 pushes at t = k / 120, k = 1..12: `Num` 8, `ByAge(0).EmitTimeS` 12 / 120, `ByAge(7).EmitTimeS` 5 / 120, ages
  ordered.
- `Reset`: `Num` 0. A push with a NaN offset, exhaust, brightness or time: returns false, `Num` unchanged.
- Deleted with their cases: `RibbonDriftMps`, `RibbonPoint`, `RibbonAlpha`, `RibbonWidthM`.

**8. `PixelLod`**
- `FocalRefPx(90, 16/9)` 960; `(90, 4/3)` 720; `(60, 16/9)` 1662.7688 (1e-3).
- `PixelLength(9.659011, 9272.65, 960)` 1.0; `(30, 28800, 960)` 1.0; `(20, 19200, 960)` 1.0; distance <= 0 or NaN: 0.
- `LodAlpha` with *parameter* band (0.5, 1.0): 0.4 gives 0, 0.5 0, 0.75 0.5, 1.0 1, 3 1; band (6, 8): 7 gives 0.5.
  Low >= high: 1 if px >= high, else 0.
- `PlumeMaxDistanceM(30, 960, 0.5)` 57600; `(30, 960, 1)` 28800; lowPx <= 0: 0.
- `GlowDrawnPx(0.9, 1.0)` 1.0; `(3.0, 1.0)` 3.0 (floor `GLOW_MIN_DRAWN_PX` = 1 px, Q33).

**9. `NearestRibbons`** (rewritten in 51e: `RibbonShown`; no radius, no count, no ranking; `SelectRibbonShips` removed in the 51e review, the store pass applies `RibbonShown` per trail)
- `RibbonShown(px, V, plume, fading)`: true when (plume or fading) and px >= V; (1.0, 1, yes, no) true (exactly V);
  (0.99, 1, yes, no) false; (5, 1, no, yes) true; (5, 1, no, no) false; NaN px or NaN V false; V <= 0 with a finite
  px >= 0 true.

**10. `QualityScaling`** (rewritten in 51e): `RibbonVanishPx` 0 (Low) → 10, 1 (Medium) → 4.6, 2 (High) → 2.2, 3 (Epic)
→ 1, 4 (Cinematic) → 1; -1 read as 0 (10), 7 read as 4 (1). Monotonic: V never rises with quality.
(`PlayerHasRibbon` removed in the 51e review: the player is always drawn, R5, checked by `SOLTest.Thruster.Store.PlayerExemption`.)

**15. `RibbonPixelRule`** (51e, SDD 51 section 11.1; `f` 960 = `FocalRefPx(90, 16/9)`, ribbon length 3 `L_p`)
- `RibbonPixelLength(30, 86400, 960)` 1.0; `(30, 21600, 960)` 4.0 (War power: dims from 21.6 km, gone at 86.4 km with
  V 1); `(9.659011, 27817.95, 960)` 1.0 and `(9.659011, 6954.49, 960)` 4.0 (1.65 G, 1e-4); `(20, 1000, 960)` 57.6;
  distance <= 0, NaN, a plume length <= 0 or a focal <= 0: 0.
- `RibbonPixelFade(px, V)` linear between V and `RIBBON_FADE_START_FACTOR` x V: V 1: 0.5 → 0, 1.0 → 0, 1.75 → 0.25,
  2.5 → 0.5, 4.0 → 1, 10 → 1. V 10: 9.9 → 0, 25 → 0.5, 40 → 1. V 4.6: 11.5 → 0.5. NaN px, NaN V or V <= 0: 0.
- `RibbonDrawnWidthM(w, d, f, minPx)` = max(w, minPx d / f): (1.5, 1000, 960, 1) 1.5; (1.5, 86400, 960, 1) 90; (3.0,
  2880, 960, 1) 3.0; (1.5, 86400, 640, 1) 135 (720p device focal 960 x 720 / 1080); NaN or negative w read as 0: (NaN,
  960, 960, 1) 1.0; the floor term 0 for a non-finite or non-positive d, f or minPx: (1.5, 0, 960, 1) 1.5, (1.5, 1000,
  0, 1) 1.5.
- `RibbonReachM(30, 960, 1)` 86400; `(30, 960, 10)` 8640; `(30, 960, 4.6)` 18782.6 (0.1); V <= 0 or NaN, a hull <= 0
  or a focal <= 0: 0.
- Constants: `RIBBON_VANISH_PX_BY_QUALITY` = {10, 4.6, 2.2, 1, 1}; `RIBBON_FADE_START_FACTOR` 4;
  `RIBBON_MIN_WIDTH_PX` 1; `FLAME_LENGTH_FACTOR` 3 (R10: `RibbonPixelLength` is the flame's on-screen length
  `PixelLength(FLAME_LENGTH_FACTOR L_p, d, f)`, so every value above is unchanged).

**16. `RibbonRendererSplit`** (51e hybrid, SDD 51 R7; `RibbonNiagaraMinPx`, `SplitRibbonRenderers`). The input is the
NPC ribbons already shown (`RibbonShown`); the player is not in it (always Niagara, its 2 trails are taken out of the
pool by the caller, so in game `niagaraFreeTrails` = `RIBBON_NIAGARA_POOL_TRAILS` - 2 = 14).
- `RibbonNiagaraMinPx`: 0 (Low) 80, 1 (Medium) 36.8, 2 (High) 17.6, 3 (Epic) 8, 4 (Cinematic) 8; -1 read as 0 (80), 7
  read as 4 (8). Equals `RIBBON_NIAGARA_PX_FACTOR` x `RibbonVanishPx` at every preset.
- **Rule.** An entry qualifies when `RibbonPx >= niagaraMinPx` (the boundary included); a NaN `RibbonPx` never
  qualifies, and a NaN `niagaraMinPx` qualifies nothing. Of the qualifiers, at most `max(0, niagaraFreeTrails)` go to
  Niagara: the highest px, ties by lower `NpcIndex`. `outNiagaraNpcIndices` holds their `NpcIndex` ordered px
  descending, ties lower index first. `outPoolNpcIndices` holds the `NpcIndex` of every other entry (non-qualifiers and
  overflow) **in input order**. Both outputs are cleared first, every input entry lands in exactly one of them, and they
  allocate only to grow. Infinite px compare as ordinary doubles (not tested); duplicate `NpcIndex` values are the
  caller's error (not tested).
- **Implementation constraint (not asserted beyond the allocation check):** no scratch array; an insertion top-k into
  `outNiagaraNpcIndices` (it may hold input positions during the pass and map them to `NpcIndex` at the end), then one
  pass for the pool.
- **20 entries:** entry i = 0..19 has `NpcIndex` 10 i and px `((7 i) mod 20) + 1` (a permutation of 1..20): i 0..19 →
  px 1, 8, 15, 2, 9, 16, 3, 10, 17, 4, 11, 18, 5, 12, 19, 6, 13, 20, 7, 14.
  - min 8, free 14 (the in-game value): 13 qualifiers (px 8..20), all fit. Niagara [170, 140, 110, 80, 50, 20, 190,
    160, 130, 100, 70, 40, 10] (px 20 down to 8; px 8 is index 10, the boundary included). Pool [0, 30, 60, 90, 120,
    150, 180].
  - min 8.0001, free 14: Niagara the same without 10 (12 entries); pool [0, 10, 30, 60, 90, 120, 150, 180].
  - **Overflow** min 8, free 5: Niagara [170, 140, 110, 80, 50] (px 20..16); pool [0, 10, 20, 30, 40, 60, 70, 90,
    100, 120, 130, 150, 160, 180, 190] (the 8 overflow qualifiers keep their input positions among the rest).
  - min 0, free 100: Niagara all 20 in px order, [170, 140, 110, 80, 50, 20, 190, 160, 130, 100, 70, 40, 10, 180,
    150, 120, 90, 60, 30, 0]; pool [].
  - free 0, and free -3: Niagara []; pool all 20 in input order [0, 10, ..., 190]. min 1e30 (the
    `sol.ThrusterRibbonNiagaraPx` "everything to the pool" override) or NaN, free 14: the same.
- **Ties** (index, px): (7, 50), (3, 50), (9, 80), (5, 50), (1, 20). Min 30, free 3: Niagara [9, 3, 5]; pool [7, 1].
  Min 30, free 10: Niagara [9, 3, 5, 7]; pool [1] (20 < 30).
- **NaN** (index, px): (4, NaN), (8, 2.0), (6, NaN), (2, 0.5). Min 0.5, free 10: Niagara [8, 2]; pool [4, 6]. Min 0,
  free 10: the same (NaN never qualifies).
- **Empty input:** both outputs empty, even when they held values before the call (pre-fill them with junk).
- **5,000 entries:** `NpcIndex` k = 0..4999, px `(37 k) mod 5000` (a permutation of 0..4999). Min 4985, free 14: 15
  qualifiers (px 4985..4999), 14 fit. Niagara [2027, 4054, 1081, 3108, 135, 2162, 4189, 1216, 3243, 270, 2297, 4324,
  1351, 3378] (px 4999 down to 4986; k for px 4999 - j is `2027 (j + 1) mod 5000`); pool 4,986 entries, strictly
  increasing k, containing 405 (px 4985, the overflow); the union of both is 0..4999 with no duplicates. Min 0, free
  14: the same Niagara list, pool 4,986. Calling again with the same input and the same output arrays leaves both
  arrays' `Max()` unchanged (no reallocation).

**17. `RibbonPoints`**: deleted by R10 together with `BuildRibbonPoints` (replaced by case 21, `BuildFlamePoints`).

**18. `RibbonTexels`** (rewritten for R10: `PackRibbonTexels`; the strip pool's per-flame texture rows; rewritten
again in 51e3 for the end caps: the points are the drawn points of case 25)
- **Rule.** Only when `outTexels.Num() == RIBBON_TEXELS_PER_TRAIL` (34), otherwise nothing is written. Texels 0 ..
  `FLAME_DRAWN_POINTS - 1` (0..26) = (x, y, z, alpha) of `points` in order; past `points.Num()` they repeat the last
  point's position with alpha 0 (all zeros when `points` is empty); points beyond the 27th are ignored. Texel
  `FLAME_DRAWN_POINTS` (27) = the flame header (`c`, `b`, `L_p`, `r_n`): the shader takes the rim and core from the
  ramp at `c`, the knot spacing from `b` and `r_n`, the arc coordinate of drawn point p as `clamp(p - 1, 0, 24)
  FLAME_LENGTH_FACTOR L_p / (FLAME_POINTS - 1)`, the nozzle's (drawn point 1) width as `2 r_n` (`FlameWidthM(0)`) and the
  caps' (drawn points 0 and 26) as 0. Texels 28..33 = the unfloored `WidthM` of drawn points 2..25, four per texel in
  order (point p in texel `28 + (p - 2) / 4`, component `(p - 2) mod 4`); widths past `points.Num()` are 0; drawn points
  0, 1 and 26 are not packed.
- **3 points:** ((1, 2, 3), alpha 0.5, width 3.0), ((4, 5, 6), 0.25, 2.0), ((7, 8, 9), 0.125, 1.0), c 0.6, b 0.8, L_p
  10, r_n 1.5: texel 0 (1, 2, 3, 0.5), 1 (4, 5, 6, 0.25), 2 (7, 8, 9, 0.125), 3..26 (7, 8, 9, 0), 27 (0.6, 0.8, 10,
  1.5), 28 (1.0, 0, 0, 0), 29..33 (0, 0, 0, 0).
- No points: texels 0..26 (0, 0, 0, 0), 27 the header, 28..33 zero. 27 points with point p's width = p (p = 0..26):
  no padding, texel 26 = point 26, texel 28 (2, 3, 4, 5), 29 (6, 7, 8, 9), ..., 33 (22, 23, 24, 25). 30 points: texel
  26 = point 26, texels 28..33 as for 27 points, points 27..29 ignored.
- `outTexels` of 33 or 35 texels, pre-filled with (9, 9, 9, 9): unchanged after the call.
- The case 21 straight table with its caps (case 25, r_n 1.5: cap depth 0.075) packed with c 0, b 0.8, L_p 10, r_n
  1.5: texel 0 (-14.925, 0, 0, 0.8) (the nozzle cap), texel 1 (-15, 0, 0, 0.8), texel 9 (-25, 0, 0, 0.533333), texel 25
  (-45, 0, 0, 0), texel 26 (-45.075, 0, 0, 0) (the tail cap), 27 (0, 0.8, 10, 1.5), texel 28.x = spine point 1's width
  `FlameWidthM(1.25, 10, 1.5)` = 2.883984.

**19. `ThrustGate`** (rewritten for R10, `NpcThrustGateM`; the Collect pass's thrust gate, SDD 51 R4, R9, R10-7):
the distance beyond which the Collect pass skips an NPC's thrust: `max(glow reach, burst reach, flame reach)` with
glow reach `f GLOW_DIAMETER_NOZZLE_RADII r_n / GLOW_FADE_LOW_PX` (the glow's true diameter at its 0.5 px low edge),
burst reach `f BurstLengthM(L_hull, 1) / CONE_FADE_LOW_PX` (a full-strength gas burst at the bursts' 0.5 px low edge;
the cone term used to cover it) and flame reach
`RibbonReachM(L_hull, f, V)` while the flames are on, 0 when they are off (`sol.ThrusterRibbons 0`: glow and bursts
only). The cone term is gone. With `GLOW_DIAMETER_NOZZLE_RADII` 2.5, `GLOW_FADE_LOW_PX` 0.5, `CONE_FADE_LOW_PX` 0.5,
f 960 (glow 7200 m, burst 9600 m for a 30 m hull):
- Flames off: `(30, 1.5, 960, false, 1)` 9600; `(30, 1.5, 960, false, 10)` 9600 (V ignored).
- Flames on: `(30, 1.5, 960, true, 1)` 86400; `(30, 1.5, 960, true, 2.2)` 39272.7 (0.1); `(30, 1.5, 960, true, 4.6)`
  18782.6 (0.1); `(30, 1.5, 960, true, 10)` 9600 (the burst term beats the flame's 8640); `(30, 1.5, 960, true, 100)`
  9600.
- Bad input: V 0, negative or NaN with flames on: 9600; hull length 0, negative or NaN: 7200 (burst and flame terms
  0); nozzle radius 0, negative or NaN: 86400 at V 1 with flames on, 9600 with flames off; focal 0, negative or NaN: 0.

**Choices made in this contract for cases 16-18** (not user decisions; case 17's are void under R10): NaN px never qualifies and a NaN threshold
qualifies nothing; free trails below 0 read as 0; the pool list keeps input order (overflow included); point 0's
alpha and width go through `RibbonAlpha(0, b)` and `RibbonWidthM(0, r_n)`; fade clamped with NaN as 0; the texel
padding repeats the last position (so the strip's degenerate tail has no stray edge) with alpha 0; missing widths are
0; extra points are ignored; a wrong-size texel view writes nothing.

**11. `Constants`** (rewritten for R10): `G_REF == SOL::WAR_POWER_PULSE_G`; `COLOUR_BLUE_END_G < COLOUR_YELLOW_G <
G_REF`; 0 < `COLOUR_YELLOW_C` < 1; `FLAME_TAIL_WIDTH_RATIO` 0.1; `FLAME_LENGTH_FACTOR` 3; `FLAME_POINTS` 25;
`FLAME_HISTORY_SAMPLES` 8; `FLAME_SAMPLE_INTERVAL_S` 1/120; `FLAME_EXHAUST_SPEED_MPS` > 0 and `FLAME_SHUTDOWN_S` > 0
(values not pinned); `(FLAME_HISTORY_SAMPLES - 1) x FLAME_SAMPLE_INTERVAL_S >= FLAME_LENGTH_FACTOR x NPC_HULL_LENGTH_M /
FLAME_EXHAUST_SPEED_MPS` (the ring covers the longest NPC flame's life; 58.3 ms against 45 ms at the start values);
`HullThrusters(NpcFaction0)` and `(NpcFaction1)`: hull 30 m, 1 nozzle, radius 1.5 m, exhaust (-1, 0, 0);
`HullThrusters(Player)`: hull 20 m, 2 nozzles at (-8.0, +-2.2, -0.2) m (0.05 m), radius 0.8 m, exhaust (-1, 0, 0).

**12. `MaterialMirror`** (51b, after the `.ush` exists): the HLSL mirror of `PlumeColour`, `PlumeBrightness` and
`ShockKnots` equals the C++ functions at c = 0, 0.175, 0.35, 0.675, 1 and G = 1, 3, `G_REF`. **Extended for R10:** the
float mirror of `SOLTH_FlamePixel`'s envelope terms in `Visuals/SOLThrusterMirror.h` equals `FlameCoreWeight`,
`FlameKnotIntensity` and `FlameStreakDepth` at the case 23 point values and over its sweep (1e-5).

**13. `GasBursts`**
- `BurstExhaustDir`: Reverse (1, 0, 0), Right (0, -1, 0), Left (0, 1, 0), Up (0, 0, -1), Down (0, 0, 1) (the gas leaves
  opposite to the thrust).
- `BurstStrength` with *parameters* (0.05, 2.0): 0.04 gives 0, 0.05 0, 1.025 0.5, 2.0 1, 6.0 1; negative or NaN 0;
  refG <= minG: 0.
- Independence: `ThrustAxes((2, 0, 0))` has every burst axis 0; `ThrustAxes((-1, 0, 0))` has Main 0.
- `BurstLengthM(30, 1)` 5.0; `(30, 0.5)` 2.5; `(20, 1)` 3.3333; strength above 1 as 1, below 0 or NaN: 0 (hull length / 6,
  clamped, Q32). `BurstStrength` reference is `BURST_REF_G` 1.5 G in game (the test passes it as a parameter).

**14. `PlayerLight`**: `PlumeLightIntensity(0.5, 1000)` 500; `(0, 1000)` 0; brightness above 1 as 1, below 0 as 0.
`PlumeLightColour(c)` equals `PlumeColour(c).Rim` at c = 0, 0.35, 0.6, 1.
`PlayerLightEnabled` (Q28): quality 0 (Low) false with `bVR` false; true at 1-4 with `bVR` false; true at every quality
with `bVR` true (VR overrides Low).

## R10 one-piece flame cases (SDD 51 section 11.2; for the 51e2 test author)

Common setup unless stated: tolerances 1e-4 m and 1e-5 for scalars; `v_e` (*parameter*) 2000 m/s; vectors are plain
(x, y, z) in metres, no handedness assumed.

**20. `FlameProfile`** (`FlameLengthM`, `FlameWidthM`, `FlameAlongAlpha`; SDD R10-1 to R10-3)
- `FlameLengthM`: (9.659011) 28.977033; (30) 90; (0), negative, NaN or infinite: 0.
- `FlameWidthM(s, L_p, r_n)` = `2 r_n [1 - (1 - FLAME_TAIL_WIDTH_RATIO) S(s / L_p)]`, `S(u) = 3u^2 - 2u^3` on `u`
  clamped to [0, 1] (a full width, the diameter). With L_p 10, r_n 1.5: s 0 → 3.0; 1.25 → 2.883984; 2.5 → 2.578125;
  5 → 1.65; 7.5 → 0.721875; 10 → 0.3; 20 → 0.3; 30 → 0.3; 40 → 0.3 (the width does not end at `L_f`, the alpha does);
  s -1 → 3.0 (negative read as 0). L_p or r_n zero, negative, NaN or infinite, or s NaN: 0.
- **Widest at the nozzle:** over s from 0 to 3 L_p in steps of L_p / 1000 (L_p 9.659011 and 30, r_n 0.8 and 1.5),
  every value is at most the one before plus 1e-9.
- **No kink at `L_p` (C1):** with h = 1e-4 L_p, `|W(L_p) - W(L_p - h)| / h <= 1e-3 x 2 r_n / L_p` and
  `|W(L_p + h) - W(L_p)| / h <= 1e-9`.
- **Slope bound** (used by case 23): every neighbouring pair of the sweep differs by at most `1.5 x 2 r_n (1 -
  FLAME_TAIL_WIDTH_RATIO) x step / L_p + 1e-9` (1.5 is the smoothstep's largest slope).
- `FlameAlongAlpha(s, L_f)` = clamp(1 - s / L_f, 0, 1): (0, 30) 1; (10, 30) 0.666667; (30, 30) 0; (45, 30) 0; (-5, 30)
  1; L_f zero, negative or NaN, or s NaN: 0.

**21. `FlameParcels`** (`BuildFlamePoints`; SDD R10-4, R10-8)
- **Rule.** `nowS = head.EmitTimeS`; `L_f = FlameLengthM(L_p)`. Returns 0 and writes nothing when `outPoints` is
  empty, `L_f` is 0, or `v_e` is not finite and positive. Otherwise writes `n = min(FLAME_POINTS, outPoints.Num())`
  points and returns n. Point j sits at arc coordinate `s_j = j L_f / (FLAME_POINTS - 1)`, parcel age `tau_j = s_j /
  v_e`, emission time `t_j = nowS - tau_j`. The pose sequence is `head, ByAge(0), ByAge(1), ...` (times not
  increasing); the pose at `t_j` comes from the first consecutive pair (newer `B`, older `A`) with `A.t <= t_j <= B.t`
  and `B.t > A.t` (pairs of equal time are skipped): `f = (t_j - A.t) / (B.t - A.t)`, offset `lerp(A, B, f)`, exhaust
  `normalize(lerp(A, B, f))` (B's when the lerp is shorter than 1e-6), brightness `lerp(A, B, f)`, nozzle velocity
  `(B.offset - A.offset) / (B.t - A.t)`. Point 0 (`tau` 0) uses the head's pose. When no pair brackets `t_j` (older
  than the oldest pose, or an empty history), the point takes the oldest pose of the sequence (the head when the
  history is empty), nozzle velocity zero, and alpha 0 (not yet emitted).
- Position `shipPositionNow + offset + (v_e exhaust + nozzle velocity) tau_j - anchorM`, the anchor subtracted in
  double before the float store. Alpha `clamp01(brightness) x FlameAlongAlpha(s_j, L_f) x clamp01(fade)` (NaN fade
  or brightness 0). Width `FlameWidthM(s_j, L_p, r_n)`, unfloored (the 1 px floor stays per renderer).
- **Straight example.** Head: t 10.0, offset (-15, 0, 0), exhaust (-1, 0, 0), b 0.8. History pushed oldest first:
  t 9.98 b 0.6, t 9.99 b 0.8, both with the head's offset and exhaust. Ship (1000, 2000, 3000), anchor = ship, L_p 10
  (L_f 30, s_j = 1.25 j), r_n 1.5, v_e 2000, fade 1, 25 points. Returns 25; every point at (-15 - s_j, 0, 0):

  | j | s | Emission t | Brightness | Alpha | WidthM |
  |---|---|---|---|---|---|
  | 0 | 0 | 10.0 | 0.8 | 0.8 | 3.0 |
  | 4 | 5 | 9.9975 | 0.8 | 0.666667 | 1.65 |
  | 8 | 10 | 9.995 | 0.8 | 0.533333 | 0.3 |
  | 16 | 20 | 9.99 | 0.8 | 0.266667 | 0.3 |
  | 20 | 25 | 9.9875 | 0.75 | 0.125 | 0.3 |
  | 24 | 30 | 9.985 | 0.7 | 0 | 0.3 |

- The same with only one sample, at t 9.9897 (b 0.8, off the point grid): points 0..16 as above; points 17..24
  (emitted before 9.9897) alpha 0, positions still (-15 - s_j, 0, 0). Empty history (ignition): point 0 alpha 0.8, points 1..24 alpha 0, positions on the head's
  straight line. Fade 0.5: every alpha halved; fade NaN or -1: every alpha 0. r_n NaN: every width 0. `outPoints` of
  3: returns 3, element 3 of a larger backing array untouched.
- **Steady turn.** Ship at the origin, anchor the origin, now 10.0, `omega` 2 rad/s, `a` 15 m. The pose at time t:
  `theta = omega (t - 10)`, offset `(-a cos theta, -a sin theta, 0)`, exhaust `(-cos theta, -sin theta, 0)`, b 1.
  Head at t = 10, history pushed oldest first at t = 10 - k / 120, k = 7..1. L_p 30 (L_f 90), r_n 1.5, v_e 2000. For
  every point j: `|y_j - FlameTurnOffsetM(s_j, 2, 2000, 15)| <= 0.02` and `|(-x_j - a) - X(s_j)| <= 0.02` with `X(s) =
  (a + s) cos(phi) + a phi sin(phi) - a`, `phi = omega s / v_e`. Points 8 and 24: y 0.900000 and 8.092711, x
  -44.993249 and -104.696373 (within 0.02; the reference implementation's interpolation error is under 0.01 m). The
  same without the nozzle velocity would miss point 24 by about 1.35 m, so the test fails such an implementation.
- **Far from the origin:** ship (1.5e11, 2.0e10, -3.0e9), anchor = ship, the straight example's poses: positions as in
  the table to 1e-3 m.

**22. `FlameRigidity`** (`FlameTurnOffsetM(s, omega, v_e, a)` = `(a + s) sin(phi) - a phi cos(phi)`, `phi = omega s /
v_e`: the lateral offset of the parcel at arc coordinate s from the current nozzle axis, for a steady turn about the
ship's centre with the nozzle a behind it, positive toward the side the nose turns to; SDD R10-5)
- (0, 2, 2000, 15) 0; (10, 2, 2000, 15) 0.100003; (30, ...) 0.900000; (60, ...) 3.598920; (90, ...) 8.092711 (1e-5).
- Nozzle at the centre: (90, 2, 2000, 0) 8.089069. Turn reversed: (90, -2, 2000, 15) -8.092711. No turn: (90, 0,
  2000, 15) 0.
- Square law: within 1 % of `omega s^2 / v_e` for every phi <= 0.1 (s 0.5 to 100, omega 0.5 to 2, a 0 to 15).
- At 127 degree/s (2.2165682 rad/s), a 15, v_e 2000: (9.659011) 0.1034; (28.977033) 0.9306; (30) 0.9975; (90) 8.9672
  (1e-3) (SDD R10-6 table).
- s negative or NaN, v_e zero, negative or NaN, omega or a non-finite: 0.

**23. `FlameSeam`** (SDD R10-3, the user's "no visible seam"; no joint exists, so the test checks that nothing along
the arc steps)
- **Built points:** the case 21 straight example with b 0.8 on every pose, and the steady-turn example: neighbouring
  points' alphas differ by the same amount everywhere (`0.8 / 24` straight, within 1e-5); widths never increase;
  points 7, 8, 9 (s = 8.75, 10, 11.25 straight, `L_p` at point 8) have exactly the `FlameWidthM` and `FlameAlongAlpha`
  of their s, like every other point. The colour is one header value per flame (case 18), so it is equal on both
  sides by construction.
- **Fine sweep** of `FlameWidthM` and `FlameAlongAlpha` over s from 0 to L_f in steps of L_f / 10000, for (L_p, r_n)
  = (9.659011, 1.5), (30, 1.5), (10.388462, 0.8), (20, 0.8): every neighbouring pair within the case 20 slope bounds,
  and the pair straddling `L_p` no larger than the largest pair inside [0.5 L_p, 0.9 L_p] or [1.1 L_p, 2 L_p], plus
  1e-9 (the alpha steps are all equal, so this is an equality within 1e-9).
- **Material envelope:** `FlameCoreWeight`, `FlameKnotIntensity` and `FlameStreakDepth` (values in [0, 1], absolute
  bounds) at radial fraction 0 and 0.5, swept the same way with b 0.4925 and 1.0 and *parameter* depths body 0.45,
  tail 0.8: the core weight does not change along s at all; the knot intensity's neighbouring pairs differ by at most
  `step / (SHOCK_KNOT_HALF_LENGTH x S) + 1e-9` (S = `ShockSpacingM(r_n, b)`); the streak depth's by at most `1.5 x
  |tail - body| x step / L_p + 1e-9`. Point values: `FlameCoreWeight` (0) 1, (0.5) 0.5625, (1) 0, (1.5) 0 (read as 1),
  (-0.5) 1 (read as 0), NaN 0. `FlameKnotIntensity` for the player at 3 G (L_p 10.388462, r_n 0.8, b 0.664050, knots at 2.23749,
  4.47498, 6.71246): (2.23749, 0) 1; (4.47498, 0) 0.82; (2.23749, 0.3) 0.5; (3.356235, 0) 0 (between knots); (2.23749 +
  0.5 x 0.35 x 2.23749, 0) 0.5; (9.0, 0) 0 (past the knot zone). `FlameStreakDepth` = `lerp(body, tail, S((s - 0.5 L_p) / L_p))` (S the
  case 20 smoothstep), with L_p 10, body 0.45, tail 0.8: s 0 → 0.45, 5 →
  0.45, 10 → 0.625, 15 → 0.8, 30 → 0.8; L_p 0 or NaN: body. The float mirror equals these to 1e-5 (case 12).

**Not in this file, re-validated in 51e3:** `SOLTest.NpcThrust.PlumeInstance` (the zero gate moves to the glow's
fade), `SOLTest.PlayerThruster.Plume` and `SOLTest.Thruster.MaterialAssets` (the plume material and mesh sections are
gone).

**24. `RibbonRendererMath`** (R10 changes only; `RibbonTexelOrigin`, `RibbonBlockCount`, `RibbonAnchorCm` and
`RibbonDeviceFocalPx` keep their existing cases; 51e3: the row is 34 x 32 = 1088 texels, trail t's origin column is
`(t mod 32) x 34`, and the anchor hold below)
- `RibbonAnchorHoldCm(held, bHasHeld, camera)` (`RIBBON_ANCHOR_HOLD_M` 1000, so 100,000 cm): with held (100, 200, 300)
  and bHasHeld: camera (150.4, 250.6, 349.5) → held; camera held + (100000, 0, 0) (exactly at the bound) → held; camera
  held + (60000, 60000, 60000) (straight-line distance 103,923 cm, every component inside the bound) →
  `RibbonAnchorCm(camera)` = (60100, 60200, 60300); camera held + (100001, 0, 0) → (100100, 200, 300) (= RibbonAnchorCm);
  bHasHeld false with camera (150.4, 250.6, 349.5) → (200, 300, 300); held with a NaN or infinite component →
  RibbonAnchorCm(camera); camera NaN → (0, 0, 0) (RibbonAnchorCm's rule), whatever is held. Far from the origin: held
  (1.0e9, -2.0e9, 3.0e8) and camera held + (12345.6, -7000.2, 0.4) → held.
- `RibbonNominalLengthM` is deleted with its asserts: a flame's length for the pixel rule is `FlameLengthM` of the
  `L_p` it burns or last burned with.
- `RibbonTrailReleasable(fed, count, newestBurningAgeS, flameLifeS, fade)`: true when count is 0; true when the newest
  pose with b > 0 is at least `flameLifeS` old (NaN age too: all its gas has left the flame); true when not fed this
  frame and the fade is 0 (NaN fade as 0); otherwise false. (true, 0, 0, 0.045, 1) true; (true, 8, 0.05, 0.045, 1)
  true; (true, 8, 0.04, 0.045, 1) false; (true, 8, 0, 0.045, 1) false (burning now, also while a shutdown spool-down
  still emits); (false, 8, 0, 0.045, 0) true; (false, 8, 0, 0.045, 0.5) false; (false, 8, 0, 0.045, NaN) true;
  (true, 8, NaN, 0.045, 1) true.

**25. `FlameCaps`** (51e3, the user's end caps: the open tube ends looked hollow; `FlameCapDepthM`, `AddFlameCaps`)
- **Constants:** `FLAME_DRAWN_POINTS` 27 (= `FLAME_POINTS` + 2), `FLAME_CAP_DEPTH_NOZZLE_RADII` 0.05,
  `FLAME_CAP_MIN_DEPTH_M` 0.02 (above Niagara's 1 cm point merge), `RIBBON_TEXELS_PER_TRAIL` 34,
  `RIBBON_ANCHOR_HOLD_M` 1000.
- **`FlameCapDepthM(r_n)`** = `max(0.05 r_n, 0.02)`: 1.5 → 0.075; 0.8 → 0.04; 0.4 → 0.02; 0.2 → 0.02; 0 → 0.02;
  -1 → 0.02; NaN and +infinity → 0.02.
- **`AddFlameCaps(spine, r_n, outDrawn)`**, n = min(spine.Num(), FLAME_POINTS): returns 0 and writes nothing when n is
  0 or `outDrawn.Num() < n + 2` (an output pre-filled with a sentinel point stays unchanged). Otherwise out[1..n] =
  spine[0..n-1] unchanged (position, alpha, width), out[0] the nozzle cap and out[n + 1] the tail cap, returns n + 2;
  entries of `outDrawn` past n + 1 are untouched. A cap's position = end + d u, d = `FlameCapDepthM(r_n)`, u the unit
  vector from the neighbour to the end (nozzle: end spine[0], neighbour spine[1]; tail: end spine[n - 1], neighbour
  spine[n - 2]), computed in double from the float positions; u = 0 (the cap on its end point) when n is 1 or the end
  segment is shorter than 1e-6 m. A cap's alpha is its end point's, its width 0. The spine may already sit at
  out[1..n] (built in place, the strip pool's fill); the result is then the same as with a separate output.
- **In place:** the case 21 straight spine copied to out[1..25] of a 27-point output, then `AddFlameCaps` on that view:
  returns 27 and the 27 points equal those written to a separate output.
- **Case 21 straight example** (spine 0 at (-15, 0, 0), spine j at (-15 - 1.25 j, 0, 0), alpha 0.8 (1 - j / 24),
  widths as the case 21 table), r_n 1.5, an output of 27: returns 27; out[0] = (-14.925, 0, 0), alpha 0.8, width 0;
  out[1] = spine 0 (width 3.0); out[9] = spine 8 (-25, 0, 0, alpha 0.533333, width 0.3); out[25] = spine 24 (-45, 0, 0,
  alpha 0, width 0.3); out[26] = (-45.075, 0, 0), alpha 0, width 0.
- **Bent ends:** spine 0 (0, 0, 0), spine 1 (-3, 4, 0), spine 2 (-6, 8, 0) (3 points, alphas 1, 0.5, 0.25, widths 2,
  1, 0.5), r_n 0.8 (d 0.04): returns 5; out[0] = (0.024, -0.032, 0), alpha 1, width 0; out[4] = (-6.024, 8.032, 0),
  alpha 0.25, width 0. With r_n 10 (d 0.5): out[0] = (0.3, -0.4, 0).
- **Degenerate:** one point (1, 2, 3), alpha 0.7, width 2: returns 3, out[0] = out[2] = (1, 2, 3) with alpha 0.7 and
  width 0, out[1] the point. Two points 5e-7 m apart: both caps on their own end points. 30 spine points: n = 25,
  returns 27, spine points 25..29 ignored. An output of 26 for a 25-point spine: returns 0, nothing written; an
  output of 30: returns 27, entries 27..29 untouched.
- **Away from the anchor:** the straight example shifted by (1000, -2000, 300) m (float positions, as a flame 2 km
  from the frame anchor): each cap 0.075 m from its end point, along +x (nozzle) and -x (tail), within 1e-3 m.
