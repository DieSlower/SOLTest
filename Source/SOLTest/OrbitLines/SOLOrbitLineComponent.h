/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"

#include "SOLOrbitLineComponent.generated.h"

/** One orbit line: a runtime-built ribbon mesh whose vertices M_SOLOrbitLine places, with fixed huge bounds. */
UCLASS()
class SOLTEST_API USOLOrbitLineComponent : public UStaticMeshComponent
{
    GENERATED_BODY()

public:

    // Sets up an unlit, shadowless, non-colliding line that is never culled
    USOLOrbitLineComponent();

    // Returns a sphere of SOL::ORBIT_LINE_BOUNDS_RADIUS_CM around the Unreal origin: the vertices sit at zero and the
    // material places them anywhere in render space, so the mesh's own bounds mean nothing
    virtual FBoxSphereBounds CalcBounds(const FTransform& localToWorld) const override;
};
