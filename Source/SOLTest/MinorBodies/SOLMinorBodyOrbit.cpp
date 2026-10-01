/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLMinorBodyOrbit.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Returns a minor body's Sun-frame position: its parent's position plus its parent-relative orbital position
FVector3d SOLMinorBodyOrbit::ComputePositionM(const FSOLSecularElements& elements, const FVector3d& parentPositionM,
    const double gm, const double secondsSinceJ2000)
{
    const double centuries = secondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;
    return SOLKepler::ElementsToState(elements.AtCenturies(centuries), gm, 0.0).PositionM + parentPositionM;
}
