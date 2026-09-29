/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// One tracked object to plot on the radar, positioned relative to the ship (meters)
struct FSOLRadarContact
{
    FVector3d RelativePositionM = FVector3d::ZeroVector;
    FName Name;
    bool bSelected = false;
};

// A contact projected onto the radar scope (unit disc, ship-forward = +Y, ship-right = +X)
struct FSOLRadarPoint
{
    FVector2D ScreenOffsetUnit = FVector2D::ZeroVector;
    double HeightStalkUnit = 0.0;
    bool bBehind = false;
    FName Name;
    bool bSelected = false;
};

namespace SOLRadar
{
    constexpr double MaxRangeM = 149597870700.0;   // 1 AU
    constexpr double DefaultFloorM = 10000000.0;   // 10,000 km: floor with no target selected
    constexpr double TargetedFloorM = 1000.0;      // 1 km: floor when a target is selected (close-combat detail)
    constexpr double ZoomStepFactor = 10.0;        // '-' / '=' step the manual range by this factor (one decade)
    constexpr double CollapseFraction = 0.01;

    // Returns the range floor for auto and manual range: TargetedFloorM if a target is selected, else DefaultFloorM
    SOLTEST_API double EffectiveFloorM(bool bHasSelectedTarget);

    // Steps a manual range one ZoomStepFactor decade (in = divide, out = multiply), clamped to [EffectiveFloorM, MaxRangeM]
    SOLTEST_API double StepManualRange(double currentRangeM, bool bZoomIn, bool bHasSelectedTarget);

    // Projects one ship-relative contact onto the scope at the given range (collapse law near center, bBehind if astern)
    SOLTEST_API FSOLRadarPoint ProjectContact(const FSOLRadarContact& contact, double rangeM);
}
