/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Components/PrimitiveComponent.h"
#include "Engine/CollisionProfile.h"

#include "CoreMinimal.h"

// Shared helper for minor-body visuals actors (ASOLAsteroidBeltVisuals, ASOLRingVisuals): both own a moving
// primitive (ISM) representing thousands of procedurally-placed rocks with no gameplay collision of their own.
// Originally duplicated as an identically-named file-local helper in each .cpp (deemed "too small to share"), which
// turned out to be a real bug: Unreal's adaptive unity build can place both .cpp files in the same translation
// unit, and an anonymous namespace does NOT prevent a symbol collision within one TU - only across separate ones -
// so the two identical definitions failed to compile the first time both files landed in the same unity blob.
namespace SOLMinorBodyVisualsUtil
{
    // Makes a moving primitive that has no collision, navigation, shadows or ray-tracing cost
    inline void MakeVisualOnlyPrimitive(UPrimitiveComponent* component)
    {
        component->SetMobility(EComponentMobility::Movable);
        component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        component->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
        component->SetCanEverAffectNavigation(false);
        component->SetGenerateOverlapEvents(false);
        component->SetCastShadow(false);
        component->bAffectDistanceFieldLighting = false;
        component->bAffectDynamicIndirectLighting = false;
        component->bVisibleInRayTracing = false;
        component->bReceivesDecals = false;
    }
}
