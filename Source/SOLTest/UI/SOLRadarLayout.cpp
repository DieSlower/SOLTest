/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLRadarLayout.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Applies the collapse law to a range fraction: sqrt(t * CollapseFraction) inside the collapse zone, linear outside
    double RadarCollapse(const double t)
    {
        return t < SOLRadar::CollapseFraction ? FMath::Sqrt(t * SOLRadar::CollapseFraction) : t;
    }
}

namespace SOLRadar
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the range floor: TargetedFloorM with a selected target, DefaultFloorM without
    double EffectiveFloorM(const bool bHasSelectedTarget)
    {
        return bHasSelectedTarget ? TargetedFloorM : DefaultFloorM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Steps a manual range by one ZoomStepFactor (in = divide, out = multiply), clamped to [EffectiveFloorM, MaxRangeM]
    double StepManualRange(const double currentRangeM, const bool bZoomIn, const bool bHasSelectedTarget)
    {
        const double floorM = EffectiveFloorM(bHasSelectedTarget);
        if (!(currentRangeM > 0.0))
        {
            return floorM;
        }
        const double stepped = bZoomIn ? currentRangeM / ZoomStepFactor : currentRangeM * ZoomStepFactor;
        return FMath::Clamp(stepped, floorM, MaxRangeM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Projects one ship-relative contact (X forward, Y right, Z up) onto the scope at the given range
    FSOLRadarPoint ProjectContact(const FSOLRadarContact& contact, const double rangeM)
    {
        // Whole-vector scaling: a contact beyond range is pulled onto the range sphere along its own direction
        const FVector3d& rawPosition = contact.RelativePositionM;
        const double distanceM = rawPosition.Length();
        const FVector3d position = distanceM <= rangeM ? rawPosition : rawPosition * (rangeM / distanceM);

        FSOLRadarPoint point;
        point.Name = contact.Name;
        point.bSelected = contact.bSelected;
        point.bBehind = rawPosition.X < 0.0;

        // Lateral plane: forward (X) maps to scope +Y, right (Y) maps to scope +X; zero lateral stays at the center
        const double lateral = FMath::Sqrt(position.X * position.X + position.Y * position.Y);
        if (lateral > 0.0)
        {
            const double radius = RadarCollapse(FMath::Clamp(lateral / rangeM, 0.0, 1.0));
            point.ScreenOffsetUnit = FVector2D(position.Y / lateral * radius, position.X / lateral * radius);
        }

        // Height stalk: same law on the ship-up component, keeping its sign
        const double height = RadarCollapse(FMath::Clamp(FMath::Abs(position.Z) / rangeM, 0.0, 1.0));
        point.HeightStalkUnit = FMath::Clamp(position.Z < 0.0 ? -height : height, -1.0, 1.0);
        return point;
    }
}
