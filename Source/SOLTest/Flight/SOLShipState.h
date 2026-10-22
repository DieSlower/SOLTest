/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Kinematic state of the ship in the Unreal-handed universe frame (meters, m/s)
struct FSOLShipState
{
    FVector3d PositionM = FVector3d::ZeroVector;
    FVector3d VelocityMps = FVector3d::ZeroVector;
    FQuat4d Orientation = FQuat4d::Identity;                    // ship-local -> universe frame
    FVector3d AngularVelocityRadPerS = FVector3d::ZeroVector;   // ship-local axes: X roll, Y pitch, Z yaw
};
