/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "OrbitLines/SOLOrbitLineComponent.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Sets up an unlit, shadowless, non-colliding line that is never culled
USOLOrbitLineComponent::USOLOrbitLineComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SetGenerateOverlapEvents(false);
    CastShadow = false;
    bCastDynamicShadow = false;
    bAffectDistanceFieldLighting = false;
    bAffectDynamicIndirectLighting = false;
    bReceivesDecals = false;
    bNeverDistanceCull = true;
    bUseAsOccluder = false;
    bVisibleInRayTracing = false;
    bVisibleInReflectionCaptures = false;
    TranslucencySortPriority = SOL::ORBIT_LINE_TRANSLUCENCY_SORT_PRIORITY;
    SetCanEverAffectNavigation(false);
}

//////////////////////////////////////////////////////////////////////////
// Returns a fixed sphere around the Unreal origin that covers every placed line vertex
FBoxSphereBounds USOLOrbitLineComponent::CalcBounds(const FTransform& /*localToWorld*/) const
{
    const double radiusCm = SOL::ORBIT_LINE_BOUNDS_RADIUS_CM;
    return FBoxSphereBounds(FVector::ZeroVector, FVector(radiusCm), radiusCm);
}
