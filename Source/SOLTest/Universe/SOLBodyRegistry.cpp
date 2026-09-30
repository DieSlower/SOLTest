/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLBodyRegistry.h"

#include "SOLConstants.h"
#include "Universe/SOLBodyRotation.h"

namespace
{
    // One row of the built-in solar-system table: name, mean radius (m), GM (m^3/s^2), Standish elements, then
    // sidereal rotation period (h), axial tilt (deg, 0-180) and rotation angle at J2000 (deg)
    struct FSOLSolarSystemRow
    {
        const TCHAR* Name;
        double RadiusM;
        double GM;
        FSOLSecularElements Elements;
        double RotationPeriodH;
        double AxialTiltDeg;
        double W0Deg;
    };

    // Sun and planets; elements from JPL Standish "Keplerian elements for approximate positions", Table 1 (1800-2050);
    // rotation periods and obliquities from the NASA/JPL Planetary Fact Sheet (Sun: equatorial period), W0 = 0 (SDD 12)
    const FSOLSolarSystemRow REGISTRY_SOLAR_SYSTEM[] =
    {
        { TEXT("Sun"), 6.957e8, 1.32712440018e20, {}, 601.2, 7.25, 0.0 },
        { TEXT("Mercury"), 2.4397e6, 2.2032e13,
            { 0.38709927, 0.00000037, 0.20563593, 0.00001906, 7.00497902, -0.00594749,
              252.25032350, 149472.67411175, 77.45779628, 0.16047689, 48.33076593, -0.12534081 },
            1407.6, 0.034, 0.0 },
        { TEXT("Venus"), 6.0518e6, 3.24859e14,
            { 0.72333566, 0.00000390, 0.00677672, -0.00004107, 3.39467605, -0.00078890,
              181.97909950, 58517.81538729, 131.60246718, 0.00268329, 76.67984255, -0.27769418 },
            5832.6, 177.36, 0.0 },
        { TEXT("Earth"), 6.371e6, 3.986004418e14,
            { 1.00000261, 0.00000562, 0.01671123, -0.00004392, -0.00001531, -0.01294668,
              100.46457166, 35999.37244981, 102.93768193, 0.32327364, 0.0, 0.0 },
            23.9345, 23.44, 0.0 },
        { TEXT("Mars"), 3.3895e6, 4.282837e13,
            { 1.52371034, 0.00001847, 0.09339410, 0.00007882, 1.84969142, -0.00813131,
              -4.55343205, 19140.30268499, -23.94362959, 0.44441088, 49.55953891, -0.29257343 },
            24.6229, 25.19, 0.0 },
        { TEXT("Jupiter"), 6.9911e7, 1.26686534e17,
            { 5.20288700, -0.00011607, 0.04838624, -0.00013253, 1.30439695, -0.00183714,
              34.39644051, 3034.74612775, 14.72847983, 0.21252668, 100.47390909, 0.20469106 },
            9.9250, 3.13, 0.0 },
        { TEXT("Saturn"), 5.8232e7, 3.7931187e16,
            { 9.53667594, -0.00125060, 0.05386179, -0.00050991, 2.48599187, 0.00193609,
              49.95424423, 1222.49362201, 92.59887831, -0.41897216, 113.66242448, -0.28867794 },
            10.656, 26.73, 0.0 },
        { TEXT("Uranus"), 2.5362e7, 5.793939e15,
            { 19.18916464, -0.00196176, 0.04725744, -0.00004397, 0.77263783, -0.00242939,
              313.23810451, 428.48202785, 170.95427630, 0.40805281, 74.01692503, 0.04240589 },
            17.24, 97.77, 0.0 },
        { TEXT("Neptune"), 2.4622e7, 6.836529e15,
            { 30.06992276, 0.00026291, 0.00859048, 0.00005105, 1.77004347, 0.00035372,
              -55.12002969, 218.45945325, 44.96476227, -0.32241464, 131.78422574, -0.00508664 },
            16.11, 28.32, 0.0 },
    };
}

//////////////////////////////////////////////////////////////////////////
// Adds a body and returns its new index
int32 FSOLBodyRegistry::AddBody(const FSOLBodyDef& def)
{
    checkf(def.ParentIndex == INDEX_NONE || (def.ParentIndex >= 0 && def.ParentIndex < Num()),
        TEXT("FSOLBodyRegistry::AddBody: parent %d of %s must be added before the child"), def.ParentIndex,
        *def.Name.ToString());

    mNames.Add(def.Name);
    mRadiiM.Add(def.RadiusM);
    mGMs.Add(def.GM);
    mParents.Add(def.ParentIndex);
    mElements.Add(def.Elements);
    mPositionsM.Add(FVector3d::ZeroVector);
    mVelocitiesMps.Add(FVector3d::ZeroVector);
    mRotationPeriodsH.Add(def.RotationPeriodH);
    mAxialTiltsRad.Add(FMath::DegreesToRadians(def.AxialTiltDeg));
    mW0sRad.Add(FMath::DegreesToRadians(def.W0Deg));
    return mOrientations.Add(FQuat4d::Identity);
}

//////////////////////////////////////////////////////////////////////////
// Clears, then adds the Sun and the eight planets (Sun=0 ... Neptune=8)
void FSOLBodyRegistry::PopulateSolarSystem()
{
    const int32 bodyCount = UE_ARRAY_COUNT(REGISTRY_SOLAR_SYSTEM);
    mNames.Reset(bodyCount);
    mRadiiM.Reset(bodyCount);
    mGMs.Reset(bodyCount);
    mParents.Reset(bodyCount);
    mElements.Reset(bodyCount);
    mPositionsM.Reset(bodyCount);
    mVelocitiesMps.Reset(bodyCount);
    mRotationPeriodsH.Reset(bodyCount);
    mAxialTiltsRad.Reset(bodyCount);
    mW0sRad.Reset(bodyCount);
    mOrientations.Reset(bodyCount);

    // Row 0 is the Sun (the root); every planet orbits it
    for (int32 index = 0; index < bodyCount; ++index)
    {
        const FSOLSolarSystemRow& row = REGISTRY_SOLAR_SYSTEM[index];
        FSOLBodyDef def;
        def.Name = FName(row.Name);
        def.RadiusM = row.RadiusM;
        def.GM = row.GM;
        def.ParentIndex = index == 0 ? INDEX_NONE : 0;
        def.Elements = row.Elements;
        def.RotationPeriodH = row.RotationPeriodH;
        def.AxialTiltDeg = row.AxialTiltDeg;
        def.W0Deg = row.W0Deg;
        AddBody(def);
    }
}

//////////////////////////////////////////////////////////////////////////
// Computes absolute (Sun-frame) positions and velocities; root bodies stay fixed at the origin
void FSOLBodyRegistry::Update(const double secondsSinceJ2000)
{
    const double centuries = secondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;
    const int32 bodyCount = mNames.Num();

    // Parents precede children, so one forward pass sees every parent's absolute state already computed
    for (int32 index = 0; index < bodyCount; ++index)
    {
        // Every body spins, root or not
        mOrientations[index] = SOLBodyRotation::ComputeOrientation(mAxialTiltsRad[index], mW0sRad[index],
            mRotationPeriodsH[index], secondsSinceJ2000);

        const int32 parent = mParents[index];
        if (parent == INDEX_NONE)
        {
            mPositionsM[index] = FVector3d::ZeroVector;
            mVelocitiesMps[index] = FVector3d::ZeroVector;
            continue;
        }
        const FSOLOrbitState relative = SOLKepler::ElementsToState(mElements[index].AtCenturies(centuries), mGMs[parent],
            0.0);
        mPositionsM[index] = mPositionsM[parent] + relative.PositionM;
        mVelocitiesMps[index] = mVelocitiesMps[parent] + relative.VelocityMps;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of bodies
int32 FSOLBodyRegistry::Num() const
{
    return mNames.Num();
}

//////////////////////////////////////////////////////////////////////////
// Returns the index of the named body, or INDEX_NONE if absent
int32 FSOLBodyRegistry::FindByName(const FName name) const
{
    return mNames.IndexOfByKey(name);
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's name
FName FSOLBodyRegistry::GetName(const int32 index) const
{
    return mNames[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's parent index
int32 FSOLBodyRegistry::GetParent(const int32 index) const
{
    return mParents[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's mean radius in meters
double FSOLBodyRegistry::GetRadiusM(const int32 index) const
{
    return mRadiiM[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's gravitational parameter in m^3/s^2
double FSOLBodyRegistry::GetGM(const int32 index) const
{
    return mGMs[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's Sun-frame position in meters
const FVector3d& FSOLBodyRegistry::GetPositionM(const int32 index) const
{
    return mPositionsM[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's Sun-frame velocity in m/s
const FVector3d& FSOLBodyRegistry::GetVelocityMps(const int32 index) const
{
    return mVelocitiesMps[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's secular orbital elements relative to its parent (unused for the Sun)
const FSOLSecularElements& FSOLBodyRegistry::GetElements(const int32 index) const
{
    return mElements[index];
}

//////////////////////////////////////////////////////////////////////////
// Returns all Sun-frame positions, indexed like the bodies
TConstArrayView<FVector3d> FSOLBodyRegistry::GetPositionsM() const
{
    return mPositionsM;
}

//////////////////////////////////////////////////////////////////////////
// Returns all Sun-frame velocities, indexed like the bodies
TConstArrayView<FVector3d> FSOLBodyRegistry::GetVelocitiesMps() const
{
    return mVelocitiesMps;
}

//////////////////////////////////////////////////////////////////////////
// Returns the body's orientation (ecliptic frame): fixed axial tilt composed with the current spin
const FQuat4d& FSOLBodyRegistry::GetOrientation(const int32 index) const
{
    return mOrientations[index];
}
