/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLRenderOrigin.h"

//////////////////////////////////////////////////////////////////////////
// Sets the origin to the observer's universe position
void FSOLRenderOrigin::Reset(const FVector3d& observerM)
{
    OriginM = observerM;
}

//////////////////////////////////////////////////////////////////////////
// Snaps the origin to the observer if forced or once it drifted SnapDistanceM or more; returns true if it moved
bool FSOLRenderOrigin::Update(const FVector3d& observerM, const bool bForceSnap)
{
    if (!bForceSnap && FVector3d::DistSquared(observerM, OriginM) < SnapDistanceM * SnapDistanceM)
    {
        return false;
    }
    OriginM = observerM;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Converts a universe position to Unreal-space cm: EclipticToUnreal(UniverseM - OriginM) * MetersToCm
FVector3d FSOLRenderOrigin::UniverseToRenderCm(const FVector3d& universeM) const
{
    return SOLRender::EclipticToUnreal(universeM - OriginM) * SOLRender::MetersToCm;
}

//////////////////////////////////////////////////////////////////////////
// Converts an Unreal-space cm position back to universe meters (the axis flip is its own inverse)
FVector3d FSOLRenderOrigin::RenderCmToUniverseM(const FVector3d& renderCm) const
{
    return OriginM + SOLRender::EclipticToUnreal(renderCm) / SOLRender::MetersToCm;
}

//////////////////////////////////////////////////////////////////////////
// Returns a body's Unreal-space placement: ComputePlacement from the observer, offset by its render location
FSOLRenderPlacement FSOLRenderOrigin::BodyPlacement(const FVector3d& bodyM, const FVector3d& observerM,
    const double radiusM, const double maxRenderDistanceCm) const
{
    FSOLRenderPlacement placement = SOLRender::ComputePlacement(bodyM - observerM, radiusM, maxRenderDistanceCm);
    placement.LocationCm = SOLRender::EclipticToUnreal(placement.LocationCm) + UniverseToRenderCm(observerM);
    return placement;
}
