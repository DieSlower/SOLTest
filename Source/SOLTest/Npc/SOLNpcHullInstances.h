/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Components/InstancedStaticMeshComponent.h"

#include "SOLNpcHullInstances.generated.h"

/**
 * Instanced NPC hulls with fixed bounds covering all of render space (SDD 11 Amendment 6). The stock component
 * recomputes its bounds over every instance after each transform update (twice per frame, ~0.4 ms per 10,000 hulls
 * on the game thread); the hulls are culled per instance on the GPU anyway, so the component bounds only need to
 * contain everything.
 */
UCLASS()
class SOLTEST_API USOLNpcHullInstances : public UInstancedStaticMeshComponent
{
    GENERATED_BODY()

public:

    // Returns bounds covering the whole render space, without walking the instances
    virtual FBoxSphereBounds CalcBounds(const FTransform& localToWorld) const override;
};
