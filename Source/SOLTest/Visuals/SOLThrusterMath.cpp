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
    // Returns an age's fraction of RIBBON_MAX_AGE_S clamped to [0, 1] (NaN as 0)
    double PlumeAgeFraction(const double ageS)
    {
        return PlumeUnit(ageS / SOL::Thruster::RIBBON_MAX_AGE_S);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a QualityPreset clamped to the presets RIBBON_COUNTS_BY_QUALITY covers (Low .. Cinematic)
    int32 PlumeClampQuality(const int32 qualityPreset)
    {
        return FMath::Clamp(qualityPreset, 0,
            static_cast<int32>(UE_ARRAY_COUNT(SOL::Thruster::RIBBON_COUNTS_BY_QUALITY)) - 1);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a ribbon candidate's ranking key: its ribbon px, NaN as 0 (keeps the sort's ordering strict)
    double PlumeRibbonRank(const FSOLRibbonCandidate& candidate)
    {
        return FMath::IsNaN(candidate.RibbonPx) ? 0.0 : candidate.RibbonPx;
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
// Writes a finite sample into the next ring slot as the newest
bool FSOLPlumeHistory::Push(const FSOLPlumeSample& sample)
{
    if (!FMath::IsFinite(sample.EmitTimeS) || !PlumeIsFiniteF(sample.OffsetM) || !PlumeIsFiniteF(sample.ExhaustDir)
        || !FMath::IsFinite(sample.DriftMps) || !FMath::IsFinite(sample.Brightness))
    {
        return false;
    }
    mNewest = mCount == 0 ? 0 : (mNewest + 1) % SOL::Thruster::RIBBON_SAMPLES;
    mSamples[mNewest] = sample;
    mCount = FMath::Min(mCount + 1, SOL::Thruster::RIBBON_SAMPLES);
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
    return mSamples[(mNewest - i + SOL::Thruster::RIBBON_SAMPLES) % SOL::Thruster::RIBBON_SAMPLES];
}

//////////////////////////////////////////////////////////////////////////
// Returns whether one sampling interval has passed since the newest sample (always when empty)
bool FSOLPlumeHistory::ShouldSample(const double nowS) const
{
    if (mCount == 0)
    {
        return true;
    }
    constexpr double intervalS = SOL::Thruster::RIBBON_MAX_AGE_S / SOL::Thruster::RIBBON_SAMPLES;
    return nowS - mSamples[mNewest].EmitTimeS >= intervalS;
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
// Returns the drift speed that makes the ribbon RIBBON_LENGTH_FACTOR plumes long at full age
float SOLThruster::RibbonDriftMps(const double plumeLengthM)
{
    if (!PlumeIsPositive(plumeLengthM))
    {
        return 0.0f;
    }
    return static_cast<float>(SOL::Thruster::RIBBON_LENGTH_FACTOR * plumeLengthM / SOL::Thruster::RIBBON_MAX_AGE_S);
}

//////////////////////////////////////////////////////////////////////////
// Returns a sample's drawn point relative to where the ship is now
FVector3d SOLThruster::RibbonPoint(const FVector3d& shipPositionNow, const FSOLPlumeSample& sample, const double nowS)
{
    const double rawAgeS = nowS - sample.EmitTimeS;
    const double ageS = rawAgeS > 0.0 ? rawAgeS : 0.0;
    return shipPositionNow + FVector3d(sample.OffsetM)
        + FVector3d(sample.ExhaustDir) * (static_cast<double>(sample.DriftMps) * ageS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the linear age fade times the sample's brightness
float SOLThruster::RibbonAlpha(const double ageS, const float brightness)
{
    if (FMath::IsNaN(ageS) || FMath::IsNaN(brightness))
    {
        return 0.0f;
    }
    return static_cast<float>((1.0 - PlumeAgeFraction(ageS)) * PlumeUnit(static_cast<double>(brightness)));
}

//////////////////////////////////////////////////////////////////////////
// Returns the ribbon width growing from r_n at the nozzle to 2 r_n at full age
float SOLThruster::RibbonWidthM(const double ageS, const double nozzleRadiusM)
{
    if (!PlumeIsPositive(nozzleRadiusM))
    {
        return 0.0f;
    }
    return static_cast<float>(nozzleRadiusM * (1.0 + PlumeAgeFraction(ageS)));
}

//////////////////////////////////////////////////////////////////////////
// Writes the best-ranked qualifying candidates' NPC indices, best first
void SOLThruster::SelectRibbonShips(TConstArrayView<FSOLRibbonCandidate> candidates, const double radiusM,
    const int32 maxCount, TArray<int32>& outNpcIndices)
{
    outNpcIndices.Reset();
    if (maxCount <= 0)
    {
        return;
    }

    // Collect the qualifying candidates' positions in the input (the output array is the scratch space)
    for (int32 index = 0; index < candidates.Num(); ++index)
    {
        const FSOLRibbonCandidate& candidate = candidates[index];
        if (candidate.DistanceM <= radiusM && (candidate.bHasPlume || candidate.bRibbonFading))
        {
            outNpcIndices.Add(index);
        }
    }

    // Partial sort of the best maxCount: ribbon px descending, ties by the lower NPC index
    const int32 keep = FMath::Min(maxCount, outNpcIndices.Num());
    int32* const first = outNpcIndices.GetData();
    std::partial_sort(first, first + keep, first + outNpcIndices.Num(),
        [&candidates](const int32 a, const int32 b)
        {
            const double rankA = PlumeRibbonRank(candidates[a]);
            const double rankB = PlumeRibbonRank(candidates[b]);
            if (rankA != rankB)
            {
                return rankA > rankB;
            }
            return candidates[a].NpcIndex < candidates[b].NpcIndex;
        });
    outNpcIndices.SetNum(keep, EAllowShrinking::No);

    // Positions to NPC indices
    for (int32& entry : outNpcIndices)
    {
        entry = candidates[entry].NpcIndex;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the ribbon ship count of the clamped preset
int32 SOLThruster::RibbonCountForQuality(const int32 qualityPreset)
{
    return SOL::Thruster::RIBBON_COUNTS_BY_QUALITY[PlumeClampQuality(qualityPreset)];
}

//////////////////////////////////////////////////////////////////////////
// Returns true at every preset above Low
bool SOLThruster::PlayerHasRibbon(const int32 qualityPreset)
{
    return PlumeClampQuality(qualityPreset) > 0;
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
// Returns true only for a hull-drawn ship
bool SOLThruster::PlumeAllowed(const bool bHullDrawn)
{
    return bHullDrawn;
}

//////////////////////////////////////////////////////////////////////////
// Returns true in VR, else above Low
bool SOLThruster::PlayerLightEnabled(const int32 qualityPreset, const bool bVR)
{
    return bVR || PlumeClampQuality(qualityPreset) > 0;
}
