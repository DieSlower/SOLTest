/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLThrusterMath.h"

#include "SOLConstants.h"
#include "Traffic/SOLLaneTrafficMath.h"

#include <algorithm>

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns true for a finite, positive length or radius
    bool PlumeIsPositive(const double value)
    {
        return FMath::IsFinite(value) && value > 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether shown entry a ranks above entry b (px descending, then lower NpcIndex, then lower position)
    bool RibbonRanksAbove(TConstArrayView<FSOLRibbonShown> shown, const int32 a, const int32 b)
    {
        const FSOLRibbonShown& entryA = shown[a];
        const FSOLRibbonShown& entryB = shown[b];
        if (entryA.RibbonPx != entryB.RibbonPx)
        {
            return entryA.RibbonPx > entryB.RibbonPx;
        }
        if (entryA.NpcIndex != entryB.NpcIndex)
        {
            return entryA.NpcIndex < entryB.NpcIndex;
        }
        return a < b;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of a double vector is finite
    bool PlumeIsFiniteD(const FVector3d& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when every component of a float vector is finite
    bool PlumeIsFiniteF(const FVector3f& v)
    {
        return FMath::IsFinite(v.X) && FMath::IsFinite(v.Y) && FMath::IsFinite(v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a value clamped to [0, 1], NaN as 0
    double PlumeUnit(const double value)
    {
        return FMath::IsNaN(value) ? 0.0 : FMath::Clamp(value, 0.0, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the smoothstep 3u^2 - 2u^3 of u clamped to [0, 1] (the flame's taper and streak blend, R10-2, R10-3)
    double FlameSmoothstep(const double u)
    {
        const double clamped = FMath::Clamp(u, 0.0, 1.0);
        return clamped * clamped * (3.0 - 2.0 * clamped);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a QualityPreset clamped to the presets RIBBON_VANISH_PX_BY_QUALITY covers (Low .. Cinematic)
    int32 PlumeClampQuality(const int32 qualityPreset)
    {
        return FMath::Clamp(qualityPreset, 0,
            static_cast<int32>(UE_ARRAY_COUNT(SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY)) - 1);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a nozzle in the hull frame exhausting -X
    FSOLNozzle PlumeAftNozzle(const double xM, const double yM, const double zM, const double radiusM)
    {
        FSOLNozzle nozzle;
        nozzle.PositionM = FVector3f(static_cast<float>(xM), static_cast<float>(yM), static_cast<float>(zM));
        nozzle.ExhaustDir = FVector3f(-1.0f, 0.0f, 0.0f);
        nozzle.RadiusM = static_cast<float>(radiusM);
        return nozzle;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an NPC hull's table: the placeholder length, one nozzle on the aft centre
    FSOLHullThrusters PlumeNpcHull()
    {
        FSOLHullThrusters hull;
        hull.HullLengthM = static_cast<float>(SOL::Thruster::NPC_HULL_LENGTH_M);
        hull.NozzleCount = 1;
        hull.Nozzles[0] = PlumeAftNozzle(SOL::Thruster::NPC_NOZZLE_X_M, 0.0, 0.0, SOL::Thruster::NPC_NOZZLE_RADIUS_M);
        return hull;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the player hull's table: two nozzles on the engine cylinders' aft faces, +Y first
    FSOLHullThrusters PlumePlayerHull()
    {
        FSOLHullThrusters hull;
        hull.HullLengthM = static_cast<float>(SOL::Thruster::PLAYER_HULL_LENGTH_M);
        hull.NozzleCount = 2;
        hull.Nozzles[0] = PlumeAftNozzle(SOL::Thruster::PLAYER_NOZZLE_X_M, SOL::Thruster::PLAYER_NOZZLE_Y_M,
            SOL::Thruster::PLAYER_NOZZLE_Z_M, SOL::Thruster::PLAYER_NOZZLE_RADIUS_M);
        hull.Nozzles[1] = PlumeAftNozzle(SOL::Thruster::PLAYER_NOZZLE_X_M, -SOL::Thruster::PLAYER_NOZZLE_Y_M,
            SOL::Thruster::PLAYER_NOZZLE_Z_M, SOL::Thruster::PLAYER_NOZZLE_RADIUS_M);
        return hull;
    }

    // The nozzle tables by ESOLHullKind, built once at load from SOL::Thruster
    const FSOLHullThrusters PLUME_HULL_TABLE[] = { PlumeNpcHull(), PlumeNpcHull(), PlumePlayerHull() };
}

//////////////////////////////////////////////////////////////////////////
// Adds one substep's thrust weighted by its duration; bad samples are skipped
void FSOLThrustMean::Add(const FVector3d& thrustLocalMps2, const double dtS)
{
    if (!PlumeIsPositive(dtS) || !PlumeIsFiniteD(thrustLocalMps2))
    {
        return;
    }
    mSumMps2S += thrustLocalMps2 * dtS;
    mSumS += dtS;
}

//////////////////////////////////////////////////////////////////////////
// Returns the time-weighted mean thrust in G (zero with no samples)
FVector3f FSOLThrustMean::MeanG() const
{
    if (!(mSumS > 0.0))
    {
        return FVector3f::ZeroVector;
    }
    return FVector3f(mSumMps2S / (mSumS * SOL::STANDARD_GRAVITY_MPS2));
}

//////////////////////////////////////////////////////////////////////////
// Clears the accumulated substeps
void FSOLThrustMean::Reset()
{
    mSumMps2S = FVector3d::ZeroVector;
    mSumS = 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Empties the ring
void FSOLPlumeHistory::Reset()
{
    mNewest = 0;
    mCount = 0;
}

//////////////////////////////////////////////////////////////////////////
// Writes a finite pose into the next ring slot as the newest
bool FSOLPlumeHistory::Push(const FSOLPlumeSample& sample)
{
    if (!FMath::IsFinite(sample.EmitTimeS) || !PlumeIsFiniteF(sample.OffsetM) || !PlumeIsFiniteF(sample.ExhaustDir)
        || !FMath::IsFinite(sample.Brightness))
    {
        return false;
    }
    mNewest = mCount == 0 ? 0 : (mNewest + 1) % SOL::Thruster::FLAME_HISTORY_SAMPLES;
    mSamples[mNewest] = sample;
    mCount = FMath::Min(mCount + 1, SOL::Thruster::FLAME_HISTORY_SAMPLES);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of samples held
int32 FSOLPlumeHistory::Num() const
{
    return mCount;
}

//////////////////////////////////////////////////////////////////////////
// Returns the sample i ranks from the newest, walking the ring backwards
const FSOLPlumeSample& FSOLPlumeHistory::ByAge(const int32 i) const
{
    check(i >= 0 && i < mCount);
    return mSamples[(mNewest - i + SOL::Thruster::FLAME_HISTORY_SAMPLES) % SOL::Thruster::FLAME_HISTORY_SAMPLES];
}

//////////////////////////////////////////////////////////////////////////
// Returns whether one push interval has passed since the newest pose (always when empty)
bool FSOLPlumeHistory::ShouldSample(const double nowS) const
{
    if (mCount == 0)
    {
        return true;
    }
    return nowS - mSamples[mNewest].EmitTimeS >= SOL::Thruster::FLAME_SAMPLE_INTERVAL_S;
}

//////////////////////////////////////////////////////////////////////////
// Returns the plume fraction x of a main-engine G
double SOLThruster::PlumeFraction(const double gMain)
{
    if (!FMath::IsFinite(gMain) || gMain < SOL::Thruster::PLUME_MIN_G)
    {
        return 0.0;
    }
    return FMath::Min(gMain / SOL::Thruster::G_REF, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the cone length from the hull length and the plume fraction
double SOLThruster::PlumeLengthM(const double hullLengthM, const double gMain)
{
    const double x = PlumeFraction(gMain);
    if (!PlumeIsPositive(hullLengthM) || x <= 0.0)
    {
        return 0.0;
    }
    return hullLengthM * FMath::Pow(x, SOL::Thruster::LENGTH_EXPONENT);
}

//////////////////////////////////////////////////////////////////////////
// Returns the plume brightness from the plume fraction
double SOLThruster::PlumeBrightness(const double gMain)
{
    const double x = PlumeFraction(gMain);
    return x > 0.0 ? FMath::Pow(x, SOL::Thruster::BRIGHTNESS_EXPONENT) : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the colour scalar: blue to BLUE_END, smoothstep to the yellow key, linear to red at G_REF
double SOLThruster::PlumeColourScalar(const double gMain, const bool bWarPower)
{
    if (bWarPower)
    {
        return 1.0;
    }
    if (FMath::IsNaN(gMain) || gMain <= SOL::Thruster::COLOUR_BLUE_END_G)
    {
        return 0.0;
    }
    if (gMain < SOL::Thruster::COLOUR_YELLOW_G)
    {
        const double t = (gMain - SOL::Thruster::COLOUR_BLUE_END_G)
            / (SOL::Thruster::COLOUR_YELLOW_G - SOL::Thruster::COLOUR_BLUE_END_G);
        return SOL::Thruster::COLOUR_YELLOW_C * t * t * (3.0 - 2.0 * t);
    }
    if (gMain < SOL::Thruster::G_REF)
    {
        return SOL::Thruster::COLOUR_YELLOW_C + (1.0 - SOL::Thruster::COLOUR_YELLOW_C)
            * (gMain - SOL::Thruster::COLOUR_YELLOW_G) / (SOL::Thruster::G_REF - SOL::Thruster::COLOUR_YELLOW_G);
    }
    return 1.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the rim and core colours, linear between the two keys around c
FSOLPlumeColour SOLThruster::PlumeColour(const double c)
{
    const double clamped = PlumeUnit(c);
    FSOLPlumeColour colour;
    if (clamped <= SOL::Thruster::COLOUR_YELLOW_C)
    {
        const float t = static_cast<float>(clamped / SOL::Thruster::COLOUR_YELLOW_C);
        colour.Rim = FMath::Lerp(SOL::Thruster::RIM_BLUE, SOL::Thruster::RIM_YELLOW, t);
        colour.Core = FMath::Lerp(SOL::Thruster::CORE_BLUE, SOL::Thruster::CORE_YELLOW, t);
    }
    else
    {
        const float t = static_cast<float>((clamped - SOL::Thruster::COLOUR_YELLOW_C)
            / (1.0 - SOL::Thruster::COLOUR_YELLOW_C));
        colour.Rim = FMath::Lerp(SOL::Thruster::RIM_YELLOW, SOL::Thruster::RIM_RED, t);
        colour.Core = FMath::Lerp(SOL::Thruster::CORE_YELLOW, SOL::Thruster::CORE_RED, t);
    }
    return colour;
}

//////////////////////////////////////////////////////////////////////////
// Returns the shock-diamond spacing from the nozzle radius and brightness
double SOLThruster::ShockSpacingM(const double nozzleRadiusM, const double brightness)
{
    if (!PlumeIsPositive(nozzleRadiusM))
    {
        return 0.0;
    }
    return SOL::Thruster::SHOCK_SPACING_FACTOR * nozzleRadiusM
        * (1.0 + SOL::Thruster::SHOCK_BRIGHTNESS_GAIN * PlumeUnit(brightness));
}

//////////////////////////////////////////////////////////////////////////
// Writes the knots inside the shock zone, each weaker by SHOCK_DECAY, and returns how many were written
int32 SOLThruster::ShockKnots(const double plumeLengthM, const double nozzleRadiusM, const double brightness,
    TArrayView<FSOLShockKnot> out)
{
    const double spacingM = ShockSpacingM(nozzleRadiusM, brightness);
    if (!PlumeIsPositive(plumeLengthM) || !(spacingM > 0.0))
    {
        return 0;
    }
    const double zoneM = SOL::Thruster::SHOCK_ZONE * plumeLengthM;
    const int32 capacity = FMath::Min(out.Num(), SOL::Thruster::SHOCK_MAX_KNOTS);
    double strength = 1.0;
    int32 count = 0;

    // Knot k at k S while it lies in the zone and the output has room
    while (count < capacity)
    {
        const double positionM = static_cast<double>(count + 1) * spacingM;
        if (positionM > zoneM)
        {
            break;
        }
        out[count].PositionM = positionM;
        out[count].Strength = strength;
        strength *= SOL::Thruster::SHOCK_DECAY;
        ++count;
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane ship's thrust in G projected on its hull basis (zero while it coasts or flips)
FVector3f SOLThruster::LaneThrustLocalG(const FSOLLaneThrustState& state, const FVector3d& hullForward,
    const FVector3d& hullTop)
{
    if (state.Thrust == ESOLLaneThrust::Coast || state.Thrust == ESOLLaneThrust::Flip
        || !FMath::IsFinite(state.AccelMps2) || !PlumeIsFiniteD(state.Direction) || !PlumeIsFiniteD(hullForward)
        || !PlumeIsFiniteD(hullTop))
    {
        return FVector3f::ZeroVector;
    }
    const FVector3d thrustG = state.Direction * (state.AccelMps2 / SOL::STANDARD_GRAVITY_MPS2);
    const FVector3d hullRight = FVector3d::CrossProduct(hullTop, hullForward);
    return FVector3f(static_cast<float>(FVector3d::DotProduct(thrustG, hullForward)),
        static_cast<float>(FVector3d::DotProduct(thrustG, hullRight)),
        static_cast<float>(FVector3d::DotProduct(thrustG, hullTop)));
}

//////////////////////////////////////////////////////////////////////////
// Returns a thrust acceleration in G on an orthonormalised hull basis (zero for a bad input or a degenerate basis)
FVector3f SOLThruster::HullThrustLocalG(const FVector3d& thrustMps2, const FVector3d& hullForward,
    const FVector3d& hullTop)
{
    if (!PlumeIsFiniteD(thrustMps2) || !PlumeIsFiniteD(hullForward) || !PlumeIsFiniteD(hullTop))
    {
        return FVector3f::ZeroVector;
    }

    // Orthonormal basis: the unit forward, the top's part orthogonal to it, right = top x forward
    const FVector3d forward = hullForward.GetSafeNormal();
    const FVector3d top = (hullTop - FVector3d::DotProduct(hullTop, forward) * forward).GetSafeNormal();
    if (forward.IsZero() || top.IsZero())
    {
        return FVector3f::ZeroVector;
    }
    const FVector3d right = FVector3d::CrossProduct(top, forward);
    const FVector3d thrustG = thrustMps2 / SOL::STANDARD_GRAVITY_MPS2;
    return FVector3f(static_cast<float>(FVector3d::DotProduct(thrustG, forward)),
        static_cast<float>(FVector3d::DotProduct(thrustG, right)),
        static_cast<float>(FVector3d::DotProduct(thrustG, top)));
}

//////////////////////////////////////////////////////////////////////////
// Returns a hull-drawn ship's plume floats (zero when off or when the glow is under its low pixel edge)
FSOLPlumeInstance SOLThruster::PlumeInstance(const double gMain, const bool bWarPower, const double hullLengthM,
    const double nozzleRadiusM, const double distanceM, const double focalRefPx)
{
    const double lengthM = PlumeLengthM(hullLengthM, gMain);
    const double glowPx = PixelLength(SOL::Thruster::GLOW_DIAMETER_NOZZLE_RADII * nozzleRadiusM, distanceM, focalRefPx);
    if (!(lengthM > 0.0) || !(glowPx >= SOL::Thruster::GLOW_FADE_LOW_PX))
    {
        return FSOLPlumeInstance();
    }
    FSOLPlumeInstance plume;
    plume.Colour = static_cast<float>(PlumeColourScalar(gMain, bWarPower));
    plume.Brightness = static_cast<float>(PlumeBrightness(gMain));
    plume.LengthM = static_cast<float>(lengthM);
    return plume;
}

//////////////////////////////////////////////////////////////////////////
// Writes the per-ship floats in the material layout (bursts zero), at most out.Num() of them
void SOLThruster::PlumeCustomData(const FSOLPlumeInstance& plume, TArrayView<float> out)
{
    namespace P = SOL::ThrusterMaterialParams;
    float values[P::CUSTOM_DATA_FLOATS] = {};
    values[P::CUSTOM_COLOUR] = plume.Colour;
    values[P::CUSTOM_BRIGHTNESS] = plume.Brightness;
    values[P::CUSTOM_PLUME_LENGTH_M] = plume.LengthM;
    const int32 count = FMath::Min(out.Num(), static_cast<int32>(P::CUSTOM_DATA_FLOATS));
    for (int32 index = 0; index < count; ++index)
    {
        out[index] = values[index];
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the positive and negative parts of each hull axis; a non-finite component gives 0 on both of its axes
FSOLThrustAxes SOLThruster::ThrustAxes(const FVector3f& thrustLocalG)
{
    const float x = FMath::IsFinite(thrustLocalG.X) ? thrustLocalG.X : 0.0f;
    const float y = FMath::IsFinite(thrustLocalG.Y) ? thrustLocalG.Y : 0.0f;
    const float z = FMath::IsFinite(thrustLocalG.Z) ? thrustLocalG.Z : 0.0f;
    FSOLThrustAxes axes;
    axes.Main = FMath::Max(x, 0.0f);
    axes.Reverse = FMath::Max(-x, 0.0f);
    axes.Right = FMath::Max(y, 0.0f);
    axes.Left = FMath::Max(-y, 0.0f);
    axes.Up = FMath::Max(z, 0.0f);
    axes.Down = FMath::Max(-z, 0.0f);
    return axes;
}

//////////////////////////////////////////////////////////////////////////
// Returns the direction a burst's gas leaves the hull in
FVector3f SOLThruster::BurstExhaustDir(const ESOLBurst burst)
{
    switch (burst)
    {
    case ESOLBurst::Reverse:
        return FVector3f(1.0f, 0.0f, 0.0f);
    case ESOLBurst::Right:
        return FVector3f(0.0f, -1.0f, 0.0f);
    case ESOLBurst::Left:
        return FVector3f(0.0f, 1.0f, 0.0f);
    case ESOLBurst::Up:
        return FVector3f(0.0f, 0.0f, -1.0f);
    case ESOLBurst::Down:
        return FVector3f(0.0f, 0.0f, 1.0f);
    default:
        return FVector3f::ZeroVector;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a burst's strength on the linear ramp from minG to refG
float SOLThruster::BurstStrength(const float componentG, const float minG, const float refG)
{
    if (FMath::IsNaN(componentG) || !FMath::IsFinite(minG) || !FMath::IsFinite(refG) || !(refG > minG))
    {
        return 0.0f;
    }
    return FMath::Clamp((componentG - minG) / (refG - minG), 0.0f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Returns a burst's length from the hull length and its strength
double SOLThruster::BurstLengthM(const double hullLengthM, const float strength)
{
    if (!PlumeIsPositive(hullLengthM))
    {
        return 0.0;
    }
    return hullLengthM / SOL::Thruster::BURST_LENGTH_HULL_DIVISOR * PlumeUnit(static_cast<double>(strength));
}

//////////////////////////////////////////////////////////////////////////
// Returns the 1080p-reference focal length of a horizontal fov at an aspect ratio
double SOLThruster::FocalRefPx(const double hFovDeg, const double aspect)
{
    if (!FMath::IsFinite(hFovDeg) || !(hFovDeg > 0.0) || !(hFovDeg < 180.0) || !PlumeIsPositive(aspect))
    {
        return 0.0;
    }
    return 0.5 * SOL::NPC_LANE_REFERENCE_HEIGHT_PX * aspect / FMath::Tan(FMath::DegreesToRadians(0.5 * hFovDeg));
}

//////////////////////////////////////////////////////////////////////////
// Returns f L / d (0 for a bad input)
double SOLThruster::PixelLength(const double lengthM, const double distanceM, const double focalRefPx)
{
    if (!PlumeIsPositive(lengthM) || !PlumeIsPositive(distanceM) || !PlumeIsPositive(focalRefPx))
    {
        return 0.0;
    }
    return focalRefPx * lengthM / distanceM;
}

//////////////////////////////////////////////////////////////////////////
// Returns the linear fade across the band, or a step at high for a degenerate band
double SOLThruster::LodAlpha(const double px, const double lowPx, const double highPx)
{
    if (FMath::IsNaN(px) || FMath::IsNaN(lowPx) || FMath::IsNaN(highPx))
    {
        return 0.0;
    }
    if (!(lowPx < highPx))
    {
        return px >= highPx ? 1.0 : 0.0;
    }
    return FMath::Clamp((px - lowPx) / (highPx - lowPx), 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the distance where a hull-length plume falls to lowPx
double SOLThruster::PlumeMaxDistanceM(const double hullLengthM, const double focalRefPx, const double lowPx)
{
    if (!PlumeIsPositive(hullLengthM) || !PlumeIsPositive(focalRefPx) || !PlumeIsPositive(lowPx))
    {
        return 0.0;
    }
    return focalRefPx * hullLengthM / lowPx;
}

//////////////////////////////////////////////////////////////////////////
// Returns the larger of the true size and the floor
double SOLThruster::GlowDrawnPx(const double truePx, const double floorPx)
{
    return truePx > floorPx ? truePx : floorPx;
}

//////////////////////////////////////////////////////////////////////////
// Returns the flame length FLAME_LENGTH_FACTOR x L_p (0 for a bad plume length)
double SOLThruster::FlameLengthM(const double plumeLengthM)
{
    return PlumeIsPositive(plumeLengthM) ? SOL::Thruster::FLAME_LENGTH_FACTOR * plumeLengthM : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the full width tapering by a smoothstep from 2 r_n at the nozzle to the tail width at L_p
double SOLThruster::FlameWidthM(const double arcM, const double plumeLengthM, const double nozzleRadiusM)
{
    if (FMath::IsNaN(arcM) || !PlumeIsPositive(plumeLengthM) || !PlumeIsPositive(nozzleRadiusM))
    {
        return 0.0;
    }
    const double taper = FlameSmoothstep(FMath::Max(arcM, 0.0) / plumeLengthM);
    return 2.0 * nozzleRadiusM * (1.0 - (1.0 - SOL::Thruster::FLAME_TAIL_WIDTH_RATIO) * taper);
}

//////////////////////////////////////////////////////////////////////////
// Returns the along-arc alpha 1 - s / L_f clamped to [0, 1]
double SOLThruster::FlameAlongAlpha(const double arcM, const double flameLengthM)
{
    if (FMath::IsNaN(arcM) || !PlumeIsPositive(flameLengthM))
    {
        return 0.0;
    }
    return FMath::Clamp(1.0 - arcM / flameLengthM, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the steady-turn parcel model's lateral offset from the current nozzle axis
double SOLThruster::FlameTurnOffsetM(const double arcM, const double turnRateRadS, const double exhaustSpeedMps,
    const double nozzleOffsetM)
{
    if (!(arcM >= 0.0) || !FMath::IsFinite(arcM) || !PlumeIsPositive(exhaustSpeedMps) || !FMath::IsFinite(turnRateRadS)
        || !FMath::IsFinite(nozzleOffsetM))
    {
        return 0.0;
    }
    const double phi = turnRateRadS * arcM / exhaustSpeedMps;
    return (nozzleOffsetM + arcM) * FMath::Sin(phi) - nozzleOffsetM * phi * FMath::Cos(phi);
}

//////////////////////////////////////////////////////////////////////////
// Writes the gas-parcel spine: each point's pose interpolated between the bracketing poses of (head, history newest
// first), moved ballistically for its age
int32 SOLThruster::BuildFlamePoints(const FSOLPlumeHistory& history, const FSOLPlumeSample& head,
    const FVector3d& shipPositionNow, const double plumeLengthM, const double nozzleRadiusM,
    const double exhaustSpeedMps, const double fade, const FVector3d& anchorM, TArrayView<FSOLRibbonPoint> outPoints)
{
    const double flameLengthM = FlameLengthM(plumeLengthM);
    if (outPoints.Num() == 0 || !(flameLengthM > 0.0) || !PlumeIsPositive(exhaustSpeedMps))
    {
        return 0;
    }
    const int32 count = FMath::Min(static_cast<int32>(SOL::Thruster::FLAME_POINTS), outPoints.Num());
    const double fadeUnit = PlumeUnit(fade);
    const double nowS = head.EmitTimeS;
    const FVector3d shipRelM = shipPositionNow - anchorM;
    const int32 poseCount = 1 + history.Num();

    // The per-point profile depends on j only (s_j / L_f = j / 24, s_j / L_p = 3 j / 24): the along-arc alpha and the
    // taper's width over 2 r_n, tabled once (the CPU pool fill builds 25 points per flame per frame)
    struct FFlameProfileTable
    {
        double Along[SOL::Thruster::FLAME_POINTS];
        double Width[SOL::Thruster::FLAME_POINTS];
    };
    static const FFlameProfileTable profile = []
    {
        FFlameProfileTable table;
        for (int32 j = 0; j < SOL::Thruster::FLAME_POINTS; ++j)
        {
            const double u = static_cast<double>(j) / static_cast<double>(SOL::Thruster::FLAME_POINTS - 1);
            table.Along[j] = FMath::Clamp(1.0 - u, 0.0, 1.0);
            table.Width[j] = 1.0 - (1.0 - SOL::Thruster::FLAME_TAIL_WIDTH_RATIO)
                * FlameSmoothstep(SOL::Thruster::FLAME_LENGTH_FACTOR * u);
        }
        return table;
    }();
    const double widthScaleM = PlumeIsPositive(nozzleRadiusM) ? 2.0 * nozzleRadiusM : 0.0;
    const double arcStepM = flameLengthM / static_cast<double>(SOL::Thruster::FLAME_POINTS - 1);
    const double tauStepS = arcStepM / exhaustSpeedMps;

    // The sequence head, ByAge(0), ByAge(1), ... gathered once (the scan below reads each pose several times)
    FSOLPlumeSample poses[1 + SOL::Thruster::FLAME_HISTORY_SAMPLES];
    poses[0] = head;
    for (int32 i = 1; i < poseCount; ++i)
    {
        poses[i] = history.ByAge(i - 1);
    }

    // The current pair (newer B, older A), converted once per pair: positions, directions, brightness, 1 / span and
    // the nozzle's own velocity (the pair's finite difference)
    int32 pairNewer = INDEX_NONE;
    FVector3d offsetA = FVector3d::ZeroVector;
    FVector3d offsetB = FVector3d::ZeroVector;
    FVector3d dirA = FVector3d::ZeroVector;
    FVector3d dirB = FVector3d::ZeroVector;
    FVector3d pairVelocityMps = FVector3d::ZeroVector;
    double brightnessA = 0.0;
    double brightnessB = 0.0;
    double timeA = 0.0;
    double invSpan = 0.0;

    // The pairs are scanned forward once: the parcels' emission times fall with j and the poses' times do not rise
    int32 newer = 0;
    for (int32 j = 0; j < count; ++j)
    {
        const double tauS = static_cast<double>(j) * tauStepS;
        const double emitS = nowS - tauS;
        FVector3d offsetM(head.OffsetM);
        FVector3d exhaust(head.ExhaustDir);
        FVector3d velocityMps = FVector3d::ZeroVector;
        double brightness = static_cast<double>(head.Brightness);
        bool bEmitted = true;
        if (j > 0)
        {
            // The first consecutive pair (newer B, older A) with A.t <= t <= B.t and B.t > A.t
            while (newer + 1 < poseCount)
            {
                const FSOLPlumeSample& b = poses[newer];
                const FSOLPlumeSample& a = poses[newer + 1];
                if (a.EmitTimeS <= emitS && emitS <= b.EmitTimeS && b.EmitTimeS > a.EmitTimeS)
                {
                    break;
                }
                ++newer;
            }
            if (newer + 1 < poseCount)
            {
                // Interpolated pose within the pair
                if (newer != pairNewer)
                {
                    const FSOLPlumeSample& b = poses[newer];
                    const FSOLPlumeSample& a = poses[newer + 1];
                    pairNewer = newer;
                    offsetA = FVector3d(a.OffsetM);
                    offsetB = FVector3d(b.OffsetM);
                    dirA = FVector3d(a.ExhaustDir);
                    dirB = FVector3d(b.ExhaustDir);
                    brightnessA = static_cast<double>(a.Brightness);
                    brightnessB = static_cast<double>(b.Brightness);
                    timeA = a.EmitTimeS;
                    invSpan = 1.0 / (b.EmitTimeS - a.EmitTimeS);
                    pairVelocityMps = (offsetB - offsetA) * invSpan;
                }
                const double f = (emitS - timeA) * invSpan;
                offsetM = FMath::Lerp(offsetA, offsetB, f);
                const FVector3d lerped = FMath::Lerp(dirA, dirB, f);
                const double lerpedLengthSquared = lerped.SizeSquared();
                exhaust = lerpedLengthSquared >= 1.0e-12 ? lerped * (1.0 / FMath::Sqrt(lerpedLengthSquared)) : dirB;
                brightness = FMath::Lerp(brightnessA, brightnessB, f);
                velocityMps = pairVelocityMps;
            }
            else
            {
                // Older than every pose: not yet emitted, on the oldest pose's line at alpha 0
                const FSOLPlumeSample& oldest = poses[poseCount - 1];
                offsetM = FVector3d(oldest.OffsetM);
                exhaust = FVector3d(oldest.ExhaustDir);
                bEmitted = false;
            }
        }
        const FVector3d positionM = shipRelM + offsetM + (exhaust * exhaustSpeedMps + velocityMps) * tauS;
        FSOLRibbonPoint& point = outPoints[j];
        point.PositionM = FVector3f(positionM);
        point.Alpha = bEmitted ? static_cast<float>(PlumeUnit(brightness) * profile.Along[j] * fadeUnit) : 0.0f;
        point.WidthM = static_cast<float>(widthScaleM * profile.Width[j]);
    }
    return count;
}

//////////////////////////////////////////////////////////////////////////
// Returns the core weight (sqrt(1 - r^2))^PLUME_CORE_POWER
double SOLThruster::FlameCoreWeight(const double radialFraction)
{
    if (FMath::IsNaN(radialFraction))
    {
        return 0.0;
    }
    const double r = FMath::Clamp(radialFraction, 0.0, 1.0);
    return FMath::Pow(FMath::Sqrt(1.0 - r * r), SOL::Thruster::PLUME_CORE_POWER);
}

//////////////////////////////////////////////////////////////////////////
// Returns the nearest shock knot's rhombus brightness times its strength
double SOLThruster::FlameKnotIntensity(const double arcM, const double radialFraction, const double plumeLengthM,
    const double nozzleRadiusM, const double brightness)
{
    FSOLShockKnot knots[SOL::Thruster::SHOCK_MAX_KNOTS];
    const int32 count = ShockKnots(plumeLengthM, nozzleRadiusM, brightness, knots);
    const double spacingM = ShockSpacingM(nozzleRadiusM, brightness);
    if (count <= 0 || !(spacingM > 0.0) || FMath::IsNaN(arcM))
    {
        return 0.0;
    }
    const int32 k = static_cast<int32>(FMath::Clamp(FMath::FloorToDouble(arcM / spacingM + 0.5), 1.0,
        static_cast<double>(count)));
    const FSOLShockKnot& knot = knots[k - 1];
    const double rhombus = FMath::Abs(arcM - knot.PositionM) / (SOL::Thruster::SHOCK_KNOT_HALF_LENGTH * spacingM)
        + PlumeUnit(radialFraction) / SOL::Thruster::SHOCK_KNOT_HALF_WIDTH;
    return knot.Strength * FMath::Clamp(1.0 - rhombus, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the streak depth blended by a smoothstep from the body (0.5 L_p) to the tail (1.5 L_p)
double SOLThruster::FlameStreakDepth(const double arcM, const double plumeLengthM, const double bodyDepth,
    const double tailDepth)
{
    if (FMath::IsNaN(arcM) || !PlumeIsPositive(plumeLengthM))
    {
        return bodyDepth;
    }
    return FMath::Lerp(bodyDepth, tailDepth, FlameSmoothstep((arcM - 0.5 * plumeLengthM) / plumeLengthM));
}

//////////////////////////////////////////////////////////////////////////
// Returns the spool-down brightness, falling linearly to 0 over FLAME_SHUTDOWN_S
double SOLThruster::FlameShutdownBrightness(const double lastBurningBrightness, const double secondsSinceCutoff)
{
    if (FMath::IsNaN(lastBurningBrightness) || FMath::IsNaN(secondsSinceCutoff))
    {
        return 0.0;
    }
    const double elapsedS = FMath::Max(secondsSinceCutoff, 0.0);
    return PlumeUnit(lastBurningBrightness)
        * FMath::Clamp(1.0 - elapsedS / SOL::Thruster::FLAME_SHUTDOWN_S, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a burning or fading flame is at least V long on screen
bool SOLThruster::RibbonShown(const double ribbonPx, const double vanishPx, const bool bHasPlume,
    const bool bRibbonFading)
{
    if (FMath::IsNaN(ribbonPx) || FMath::IsNaN(vanishPx))
    {
        return false;
    }
    return (bHasPlume || bRibbonFading) && ribbonPx >= vanishPx;
}

//////////////////////////////////////////////////////////////////////////
// Returns V of the clamped preset
double SOLThruster::RibbonVanishPx(const int32 qualityPreset)
{
    return SOL::Thruster::RIBBON_VANISH_PX_BY_QUALITY[PlumeClampQuality(qualityPreset)];
}

//////////////////////////////////////////////////////////////////////////
// Returns the flame's length on screen
double SOLThruster::RibbonPixelLength(const double plumeLengthM, const double distanceM, const double focalRefPx)
{
    return PixelLength(SOL::Thruster::FLAME_LENGTH_FACTOR * plumeLengthM, distanceM, focalRefPx);
}

//////////////////////////////////////////////////////////////////////////
// Returns the linear fade from 0 at V to 1 at RIBBON_FADE_START_FACTOR x V
double SOLThruster::RibbonPixelFade(const double ribbonPx, const double vanishPx)
{
    if (FMath::IsNaN(ribbonPx) || !PlumeIsPositive(vanishPx))
    {
        return 0.0;
    }
    return LodAlpha(ribbonPx, vanishPx, SOL::Thruster::RIBBON_FADE_START_FACTOR * vanishPx);
}

//////////////////////////////////////////////////////////////////////////
// Returns the width with the pixel floor
double SOLThruster::RibbonDrawnWidthM(const double widthM, const double distanceM, const double focalPx,
    const double minPx)
{
    const double width = FMath::IsFinite(widthM) && widthM > 0.0 ? widthM : 0.0;
    const double floorM = PlumeIsPositive(distanceM) && PlumeIsPositive(focalPx) && PlumeIsPositive(minPx)
        ? minPx * distanceM / focalPx : 0.0;
    return width > floorM ? width : floorM;
}

//////////////////////////////////////////////////////////////////////////
// Returns the thin-flame shading weight: the true-width share times the ramp over the drawn pixel width
double SOLThruster::FlameSharpness(const double trueWidthM, const double drawnWidthM, const double distanceM,
    const double focalPx)
{
    if (!(drawnWidthM > 0.0) || !(distanceM > 0.0) || !(focalPx > 0.0))
    {
        return 1.0;
    }
    const double drawnPx = drawnWidthM * focalPx / distanceM;
    const double ramp = (drawnPx - SOL::Thruster::FLAME_SHARP_LOW_PX)
        / (SOL::Thruster::FLAME_SHARP_HIGH_PX - SOL::Thruster::FLAME_SHARP_LOW_PX);
    return FMath::Clamp(FMath::Max(trueWidthM, 0.0) / drawnWidthM, 0.0, 1.0) * FMath::Clamp(ramp, 0.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the distance where the longest flame shrinks to V
double SOLThruster::RibbonReachM(const double hullLengthM, const double focalRefPx, const double vanishPx)
{
    if (!PlumeIsPositive(hullLengthM))
    {
        return 0.0;
    }
    return PlumeMaxDistanceM(SOL::Thruster::FLAME_LENGTH_FACTOR * hullLengthM, focalRefPx, vanishPx);
}

//////////////////////////////////////////////////////////////////////////
// Returns the Collect pass's NPC thrust gate: the glow's and a full gas burst's 0.5 px edges, widened to the flame
// reach while the flames are on
double SOLThruster::NpcThrustGateM(const double hullLengthM, const double nozzleRadiusM, const double focalRefPx,
    const bool bFlames, const double vanishPx)
{
    const double glowM = PlumeIsPositive(nozzleRadiusM) ? PlumeMaxDistanceM(
        SOL::Thruster::GLOW_DIAMETER_NOZZLE_RADII * nozzleRadiusM, focalRefPx, SOL::Thruster::GLOW_FADE_LOW_PX) : 0.0;
    const double burstM = PlumeMaxDistanceM(BurstLengthM(hullLengthM, 1.0f), focalRefPx,
        SOL::Thruster::CONE_FADE_LOW_PX);
    const double flameM = bFlames ? RibbonReachM(hullLengthM, focalRefPx, vanishPx) : 0.0;
    return FMath::Max3(glowM, burstM, flameM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the Niagara threshold RIBBON_NIAGARA_PX_FACTOR x V of the clamped preset
double SOLThruster::RibbonNiagaraMinPx(const int32 qualityPreset)
{
    return SOL::Thruster::RIBBON_NIAGARA_PX_FACTOR * RibbonVanishPx(qualityPreset);
}

//////////////////////////////////////////////////////////////////////////
// Sends the top niagaraFreeTrails qualifiers (px >= min) to Niagara by px, every other entry to the pool in input order
void SOLThruster::SplitRibbonRenderers(TConstArrayView<FSOLRibbonShown> shown, const double niagaraMinPx,
    const int32 niagaraFreeTrails, TArray<int32>& outNiagaraNpcIndices, TArray<int32>& outPoolNpcIndices)
{
    outNiagaraNpcIndices.Reset();
    outPoolNpcIndices.Reset();
    const int32 capacity = niagaraFreeTrails > 0 ? niagaraFreeTrails : 0;
    const int32 count = shown.Num();

    // Insertion top-k: outNiagaraNpcIndices holds input positions, best first, until the final mapping
    if (capacity > 0)
    {
        for (int32 i = 0; i < count; ++i)
        {
            if (!(shown[i].RibbonPx >= niagaraMinPx))
            {
                continue;
            }
            int32 slot = outNiagaraNpcIndices.Num();
            if (slot == capacity)
            {
                if (!RibbonRanksAbove(shown, i, outNiagaraNpcIndices[slot - 1]))
                {
                    continue;
                }
                --slot;
            }
            else
            {
                outNiagaraNpcIndices.Add(i);
            }
            while (slot > 0 && RibbonRanksAbove(shown, i, outNiagaraNpcIndices[slot - 1]))
            {
                outNiagaraNpcIndices[slot] = outNiagaraNpcIndices[slot - 1];
                --slot;
            }
            outNiagaraNpcIndices[slot] = i;
        }
    }

    // One pass for the pool: non-qualifiers, and qualifiers ranking below the last kept one when Niagara is full
    const int32 kept = outNiagaraNpcIndices.Num();
    const int32 worst = kept == capacity && kept > 0 ? outNiagaraNpcIndices[kept - 1] : INDEX_NONE;
    outPoolNpcIndices.Reserve(count - kept);
    for (int32 i = 0; i < count; ++i)
    {
        const bool bQualifies = capacity > 0 && shown[i].RibbonPx >= niagaraMinPx;
        if (!bQualifies || (worst != INDEX_NONE && RibbonRanksAbove(shown, worst, i)))
        {
            outPoolNpcIndices.Add(shown[i].NpcIndex);
        }
    }

    // Map the kept input positions to NpcIndex
    for (int32& entry : outNiagaraNpcIndices)
    {
        entry = shown[entry].NpcIndex;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a flame end cap's depth max(CAP_DEPTH_NOZZLE_RADII r_n, CAP_MIN_DEPTH_M)
double SOLThruster::FlameCapDepthM(const double nozzleRadiusM)
{
    const double depthM = PlumeIsPositive(nozzleRadiusM)
        ? SOL::Thruster::FLAME_CAP_DEPTH_NOZZLE_RADII * nozzleRadiusM : 0.0;
    return FMath::Max(depthM, SOL::Thruster::FLAME_CAP_MIN_DEPTH_M);
}

//////////////////////////////////////////////////////////////////////////
// Writes the drawn flame: the nozzle cap, the spine, the tail cap (each cap out along its end segment, width 0)
int32 SOLThruster::AddFlameCaps(TConstArrayView<FSOLRibbonPoint> spine, const double nozzleRadiusM,
    TArrayView<FSOLRibbonPoint> outDrawn)
{
    const int32 count = FMath::Min(spine.Num(), static_cast<int32>(SOL::Thruster::FLAME_POINTS));
    if (count <= 0 || outDrawn.Num() < count + 2)
    {
        return 0;
    }
    const double depthM = FlameCapDepthM(nozzleRadiusM);

    // Returns the cap point beyond end, away from its neighbour (on end itself for a degenerate segment)
    const auto cap = [depthM](const FSOLRibbonPoint& end, const FSOLRibbonPoint* neighbour)
    {
        FSOLRibbonPoint point = end;
        point.WidthM = 0.0f;
        if (neighbour != nullptr)
        {
            const FVector3d segment = FVector3d(end.PositionM) - FVector3d(neighbour->PositionM);
            const double lengthM = segment.Size();
            if (lengthM >= 1.0e-6)
            {
                point.PositionM = FVector3f(FVector3d(end.PositionM) + segment * (depthM / lengthM));
            }
        }
        return point;
    };
    // The spine moves up one slot unless it was built in place there (the strip pool's fill)
    if (outDrawn.GetData() + 1 != spine.GetData())
    {
        for (int32 j = 0; j < count; ++j)
        {
            outDrawn[j + 1] = spine[j];
        }
    }
    outDrawn[0] = cap(spine[0], count > 1 ? &spine[1] : nullptr);
    outDrawn[count + 1] = cap(spine[count - 1], count > 1 ? &spine[count - 2] : nullptr);
    return count + 2;
}

//////////////////////////////////////////////////////////////////////////
// Packs one drawn flame's points, header and widths into RIBBON_TEXELS_PER_TRAIL texels (the strip pool texture row)
void SOLThruster::PackRibbonTexels(TConstArrayView<FSOLRibbonPoint> points, const float colourScalar,
    const float brightness, const float plumeLengthM, const float nozzleRadiusM, TArrayView<FVector4f> outTexels)
{
    constexpr int32 pointCapacity = SOL::Thruster::FLAME_DRAWN_POINTS;
    constexpr int32 firstPackedWidth = 2;
    if (outTexels.Num() != SOL::Thruster::RIBBON_TEXELS_PER_TRAIL)
    {
        return;
    }
    const int32 used = FMath::Min(points.Num(), pointCapacity);

    // Texels 0..26: (x, y, z, alpha), padded with the last position at alpha 0
    const FVector3f padPosition = used > 0 ? points[used - 1].PositionM : FVector3f::ZeroVector;
    for (int32 p = 0; p < pointCapacity; ++p)
    {
        if (p < used)
        {
            const FSOLRibbonPoint& point = points[p];
            outTexels[p] = FVector4f(point.PositionM.X, point.PositionM.Y, point.PositionM.Z, point.Alpha);
        }
        else
        {
            outTexels[p] = FVector4f(padPosition.X, padPosition.Y, padPosition.Z, 0.0f);
        }
    }

    // Texel 27: the header (c, b, L_p, r_n)
    outTexels[pointCapacity] = FVector4f(colourScalar, brightness, plumeLengthM, nozzleRadiusM);

    // Texels 28..33: the unfloored widths of drawn points 2..25, four per texel
    for (int32 texel = pointCapacity + 1; texel < SOL::Thruster::RIBBON_TEXELS_PER_TRAIL; ++texel)
    {
        float widths[4];
        for (int32 c = 0; c < 4; ++c)
        {
            const int32 p = firstPackedWidth + (texel - pointCapacity - 1) * 4 + c;
            widths[c] = p < used ? points[p].WidthM : 0.0f;
        }
        outTexels[texel] = FVector4f(widths[0], widths[1], widths[2], widths[3]);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a trail's first texel (column, row) in the strip pool texture
FIntPoint SOLThruster::RibbonTexelOrigin(const int32 trail)
{
    if (trail < 0)
    {
        return FIntPoint(-1, -1);
    }
    constexpr int32 perBlock = SOL::Thruster::RIBBON_TRAILS_PER_BLOCK;
    return FIntPoint((trail % perBlock) * SOL::Thruster::RIBBON_TEXELS_PER_TRAIL, trail / perBlock);
}

//////////////////////////////////////////////////////////////////////////
// Returns the strip blocks that hold a trail count
int32 SOLThruster::RibbonBlockCount(const int32 trails)
{
    return trails > 0 ? FMath::DivideAndRoundUp(trails, SOL::Thruster::RIBBON_TRAILS_PER_BLOCK) : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the camera position rounded to whole metres (render-space cm)
FVector3d SOLThruster::RibbonAnchorCm(const FVector3d& cameraRenderCm)
{
    if (!FMath::IsFinite(cameraRenderCm.X) || !FMath::IsFinite(cameraRenderCm.Y) || !FMath::IsFinite(cameraRenderCm.Z))
    {
        return FVector3d::ZeroVector;
    }
    return FVector3d(FMath::RoundToDouble(cameraRenderCm.X / 100.0), FMath::RoundToDouble(cameraRenderCm.Y / 100.0),
        FMath::RoundToDouble(cameraRenderCm.Z / 100.0)) * 100.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the held anchor while the camera stays within RIBBON_ANCHOR_HOLD_M of it, else the camera rounded to metres
FVector3d SOLThruster::RibbonAnchorHoldCm(const FVector3d& heldAnchorCm, const bool bHasHeld,
    const FVector3d& cameraRenderCm)
{
    const double holdCm = SOL::Thruster::RIBBON_ANCHOR_HOLD_M * SOL::METERS_TO_CM;
    const bool bHeldFinite = FMath::IsFinite(heldAnchorCm.X) && FMath::IsFinite(heldAnchorCm.Y)
        && FMath::IsFinite(heldAnchorCm.Z);
    if (bHasHeld && bHeldFinite && FVector3d::DistSquared(cameraRenderCm, heldAnchorCm) <= holdCm * holdCm)
    {
        return heldAnchorCm;
    }
    return RibbonAnchorCm(cameraRenderCm);
}

//////////////////////////////////////////////////////////////////////////
// Returns the device focal length in device pixels
double SOLThruster::RibbonDeviceFocalPx(const double projectionScaleY, const double viewHeightPx)
{
    if (!FMath::IsFinite(projectionScaleY) || !FMath::IsFinite(viewHeightPx) || projectionScaleY <= 0.0
        || viewHeightPx <= 0.0)
    {
        return 0.0;
    }
    return projectionScaleY * viewHeightPx * 0.5;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a flame store slot may be released
bool SOLThruster::RibbonTrailReleasable(const bool bFedThisFrame, const int32 sampleCount,
    const double newestBurningAgeS, const double flameLifeS, const double pixelFade)
{
    if (sampleCount <= 0 || !(newestBurningAgeS < flameLifeS))
    {
        return true;
    }
    return !bFedThisFrame && !(pixelFade > 0.0);
}

//////////////////////////////////////////////////////////////////////////
// Returns the light intensity proportional to the clamped brightness
float SOLThruster::PlumeLightIntensity(const float brightness, const float maxIntensity)
{
    if (!FMath::IsFinite(maxIntensity) || maxIntensity < 0.0f)
    {
        return 0.0f;
    }
    return static_cast<float>(PlumeUnit(static_cast<double>(brightness))) * maxIntensity;
}

//////////////////////////////////////////////////////////////////////////
// Returns the ramp's rim colour at c
FLinearColor SOLThruster::PlumeLightColour(const double c)
{
    return PlumeColour(c).Rim;
}

//////////////////////////////////////////////////////////////////////////
// Returns the hull's row of the nozzle table
const FSOLHullThrusters& SOLThruster::HullThrusters(const ESOLHullKind kind)
{
    const int32 index = static_cast<int32>(kind);
    return PLUME_HULL_TABLE[index < static_cast<int32>(UE_ARRAY_COUNT(PLUME_HULL_TABLE)) ? index : 0];
}

//////////////////////////////////////////////////////////////////////////
// Returns true in VR, else above Low
bool SOLThruster::PlayerLightEnabled(const int32 qualityPreset, const bool bVR)
{
    return bVR || PlumeClampQuality(qualityPreset) > 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns a world thrust in the ship frame of an attitude (zero for a non-finite input)
FVector3d SOLThruster::ShipThrustLocalMps2(const FQuat4d& orientation, const FVector3d& thrustWorldMps2)
{
    if (thrustWorldMps2.ContainsNaN() || !FMath::IsFinite(orientation.X) || !FMath::IsFinite(orientation.Y)
        || !FMath::IsFinite(orientation.Z) || !FMath::IsFinite(orientation.W))
    {
        return FVector3d::ZeroVector;
    }
    const FVector3d local = orientation.UnrotateVector(thrustWorldMps2);
    return local.ContainsNaN() ? FVector3d::ZeroVector : local;
}

//////////////////////////////////////////////////////////////////////////
// Returns the player's plume floats from the forward G on its hull and its hull light from the uncut brightness
FSOLPlayerPlume SOLThruster::PlayerPlume(const FVector3f& thrustLocalG, const bool bWarPower, const double distanceM,
    const double focalRefPx, const bool bLightEnabled)
{
    FSOLPlayerPlume result;
    const double gMain = static_cast<double>(thrustLocalG.X);
    result.Plume = PlumeInstance(gMain, bWarPower, SOL::Thruster::PLAYER_HULL_LENGTH_M,
        SOL::Thruster::PLAYER_NOZZLE_RADIUS_M, distanceM, focalRefPx);
    const float brightness = static_cast<float>(PlumeBrightness(gMain));
    result.bLightVisible = bLightEnabled && brightness > 0.0f;
    if (result.bLightVisible)
    {
        result.LightIntensityCd = PlumeLightIntensity(brightness,
            static_cast<float>(SOL::Thruster::PLAYER_LIGHT_MAX_INTENSITY_CD));
        result.LightColour = PlumeLightColour(PlumeColourScalar(gMain, bWarPower));
    }
    return result;
}
