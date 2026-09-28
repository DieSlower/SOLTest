/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLTargeting.h"

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "SOLTargetable.generated.h"

/** Marks an object that the player can select as a target and lock as the reference frame. */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USOLTargetable : public UInterface
{
    GENERATED_BODY()
};

/**
 * Implemented by non-body targets (enemies, dropped targets) and registered with USOLTargetingSubsystem, which adds
 * them to the same candidate list as the registry bodies. Registry bodies are not UObjects; the subsystem builds their
 * candidates from the body cache directly.
 */
class SOLTEST_API ISOLTargetable
{
    GENERATED_BODY()

public:

    // Returns the target's snapshot in the Unreal-handed universe frame (meters, m/s); called once per frame, no allocation
    virtual FSOLTargetInfo GetTargetInfo() const = 0;
};
