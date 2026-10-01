/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Universe/SOLKepler.h"

#include "CoreMinimal.h"

// Position math shared by every minor body (belt asteroids now, ring particles later; SDD 6 Appendix B)
namespace SOLMinorBodyOrbit
{
    // Returns a minor body's Sun-frame (raw ecliptic/universe meters) position: its parent's current position plus its
    // own parent-relative orbital position at the given time. gm only needs to be positive: SOLKepler::ElementsToState
    // derives velocity from it, which this function discards, so position does not depend on it (pass the Sun's GM)
    SOLTEST_API FVector3d ComputePositionM(const FSOLSecularElements& elements, const FVector3d& parentPositionM,
        double gm, double secondsSinceJ2000);
}
