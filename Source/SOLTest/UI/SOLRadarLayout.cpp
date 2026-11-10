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
    // Returns the AUTO range: AUTO_RANGE_TARGETED_M with a selected target, AUTO_RANGE_M without
    double AutoRangeM(const bool bHasSelectedTarget)
    {
        return bHasSelectedTarget ? AUTO_RANGE_TARGETED_M : AUTO_RANGE_M;
    }

    //////////////////////////////////////////////////////////////////////////
    // Steps a manual range to the next power of ten (exact table values), clamped to [MANUAL_FLOOR_M, MaxRangeM]
    double StepManualRange(const double currentRangeM, const bool bZoomIn)
    {
        // Exact powers of ten (decimal literals up to 1e22 are exact doubles), one decade beyond each search bound
        static constexpr double DECADES_M[] = { 1.0e1, 1.0e2, 1.0e3, 1.0e4, 1.0e5, 1.0e6, 1.0e7, 1.0e8, 1.0e9, 1.0e10,
            1.0e11, 1.0e12, 1.0e13, 1.0e14, 1.0e15 };
        constexpr int32 DECADE_COUNT = UE_ARRAY_COUNT(DECADES_M);
        constexpr double SEARCH_LOW_M = 1.0e2;      // At or below: every step clamps to the floor
        constexpr double SEARCH_HIGH_M = 1.0e14;    // At or above (also +infinity): every step clamps to the ceiling

        // Not > 0 (zero, negative, -infinity, NaN) is invalid; the bounds keep the search inside the table
        if (!(currentRangeM > 0.0) || currentRangeM <= SEARCH_LOW_M)
        {
            return MANUAL_FLOOR_M;
        }
        if (currentRangeM >= SEARCH_HIGH_M)
        {
            return MaxRangeM;
        }

        // A range within the tolerance of a power counts as that power and steps one decade
        double targetM = MANUAL_FLOOR_M;
        int32 match = INDEX_NONE;
        for (int32 index = 0; index < DECADE_COUNT; ++index)
        {
            if (FMath::Abs(currentRangeM - DECADES_M[index]) <= DECADES_M[index] * ZOOM_SNAP_RELATIVE_TOLERANCE)
            {
                match = index;
                break;
            }
        }
        if (match != INDEX_NONE)
        {
            targetM = DECADES_M[bZoomIn ? match - 1 : match + 1];
        }
        else if (bZoomIn)
        {
            // The largest power strictly below (exists: the range is above SEARCH_LOW_M)
            for (int32 index = DECADE_COUNT - 1; index >= 0; --index)
            {
                if (DECADES_M[index] < currentRangeM)
                {
                    targetM = DECADES_M[index];
                    break;
                }
            }
        }
        else
        {
            // The smallest power strictly above (exists: the range is below SEARCH_HIGH_M)
            for (int32 index = 0; index < DECADE_COUNT; ++index)
            {
                if (DECADES_M[index] > currentRangeM)
                {
                    targetM = DECADES_M[index];
                    break;
                }
            }
        }
        return FMath::Clamp(targetM, MANUAL_FLOOR_M, MaxRangeM);
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

    //////////////////////////////////////////////////////////////////////////
    // A world-space offset in the ship-horizontal frame (X forward, Y right, Z ship-up)
    FVector3d ToShipFrame(const FVector3d& contactWorldM, const FVector3d& shipWorldM, const FQuat4d& shipOrientation)
    {
        return shipOrientation.UnrotateVector(contactWorldM - shipWorldM);
    }

    //////////////////////////////////////////////////////////////////////////
    // The side of the plane for a projected height: Below only for a negative height (NaN counts as above)
    ESOLRadarSide ClassifySide(const double heightStalkUnit)
    {
        return heightStalkUnit < 0.0 ? ESOLRadarSide::Below : ESOLRadarSide::Above;
    }

    //////////////////////////////////////////////////////////////////////////
    // The draw style of a projected contact: below the plane dashed and hollow, the selected one large
    FSOLRadarStyle ContactStyle(const FSOLRadarPoint& point)
    {
        FSOLRadarStyle style;
        style.Side = ClassifySide(point.HeightStalkUnit);
        style.bDashedStem = style.Side == ESOLRadarSide::Below;
        style.bHollowBlip = style.bDashedStem;
        style.bLarge = point.bSelected;
        return style;
    }

    //////////////////////////////////////////////////////////////////////////
    // Number of dashes along a stem: one per (dash + gap) period whose start lies inside the stem
    int32 DashCount(const double lengthPx, const double dashPx, const double gapPx)
    {
        if (!(lengthPx > 0.0) || !(dashPx > 0.0))
        {
            return 0;
        }
        const double period = dashPx + FMath::Max(gapPx, 0.0);
        return FMath::CeilToInt32(lengthPx / period);
    }

    //////////////////////////////////////////////////////////////////////////
    // One dash's span from the plane toward the blip, clipped to the stem; false for an index outside the dashes
    bool DashSegment(const double lengthPx, const double dashPx, const double gapPx, const int32 index,
        double& outStartPx, double& outEndPx)
    {
        if (index < 0 || index >= DashCount(lengthPx, dashPx, gapPx))
        {
            return false;
        }
        const double start = index * (dashPx + FMath::Max(gapPx, 0.0));
        outStartPx = start;
        outEndPx = FMath::Min(start + dashPx, lengthPx);
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // A scope point on the screen: the plane as an ellipse squashed vertically, forward up the screen
    FVector2D ScopeToScreen(const FVector2D& center, const double radiusPx, const double squash,
        const FVector2D& scopeUnit)
    {
        return FVector2D(center.X + scopeUnit.X * radiusPx, center.Y - scopeUnit.Y * radiusPx * squash);
    }

    //////////////////////////////////////////////////////////////////////////
    // The top of a contact's stem: a positive height goes up the screen
    FVector2D StemTop(const FVector2D& foot, const double heightStalkUnit, const double radiusPx,
        const double stalkScale)
    {
        return FVector2D(foot.X, foot.Y - heightStalkUnit * radiusPx * stalkScale);
    }

    //////////////////////////////////////////////////////////////////////////
    // A ship-frame azimuth as a scope unit vector (0 = forward = +Y, +pi/2 = right = +X)
    FVector2D AzimuthToScope(const double azimuthRad)
    {
        double sinA = 0.0;
        double cosA = 0.0;
        FMath::SinCos(&sinA, &cosA, azimuthRad);
        return FVector2D(sinA, cosA);
    }

    //////////////////////////////////////////////////////////////////////////
    // The azimuth of a ship-frame direction on the plane (0 forward, +pi/2 right); 0 with no lateral part
    double ShipAzimuth(const FVector3d& shipLocal)
    {
        return shipLocal.X == 0.0 && shipLocal.Y == 0.0 ? 0.0 : FMath::Atan2(shipLocal.Y, shipLocal.X);
    }

    //////////////////////////////////////////////////////////////////////////
    // Picks the budgeted contacts: selected first (by index), then nearest first (ties by index), in place
    int32 SelectContacts(const TConstArrayView<FSOLRadarContact> contacts, const int32 budget, TArray<int32>& outOrder)
    {
        outOrder.Reset();
        const int32 count = FMath::Min(contacts.Num(), FMath::Max(budget, 0));
        if (count == 0)
        {
            return 0;
        }

        // All indices ordered (a stable key: selected, squared distance, index), then cut to the budget
        for (int32 index = 0; index < contacts.Num(); ++index)
        {
            outOrder.Add(index);
        }
        outOrder.Sort([&contacts](const int32 a, const int32 b)
        {
            const FSOLRadarContact& ca = contacts[a];
            const FSOLRadarContact& cb = contacts[b];
            if (ca.bSelected != cb.bSelected)
            {
                return ca.bSelected;
            }
            if (!ca.bSelected)
            {
                const double da = ca.RelativePositionM.SquaredLength();
                const double db = cb.RelativePositionM.SquaredLength();
                if (da != db)
                {
                    return da < db;
                }
            }
            return a < b;
        });
        outOrder.SetNum(count, EAllowShrinking::No);
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // A range ring's glow: a steady base plus LT's sonar pulse travelling outward, scaled by the effects strength
    double SonarRingIntensity(const double ringFraction, const double timeS, const double strength)
    {
        const double s = FMath::Clamp(strength, 0.0, SonarMaxStrength);
        const double wave = FMath::Abs(FMath::Sin(UE_DOUBLE_PI * (timeS * SonarRingsPerSecond - ringFraction)));
        return SonarBaseIntensity + SonarPulseIntensity * s * FMath::Pow(wave, SonarSharpness);
    }

    //////////////////////////////////////////////////////////////////////////
    // True when a point lies within a radius of any already drawn blip
    bool IsCrowded(const TConstArrayView<FVector2D> drawnPx, const FVector2D& pointPx, const double radiusPx)
    {
        if (!(radiusPx > 0.0))
        {
            return false;
        }
        const double radiusSq = radiusPx * radiusPx;
        for (const FVector2D& drawn : drawnPx)
        {
            if (FVector2D::DistSquared(drawn, pointPx) <= radiusSq)
            {
                return true;
            }
        }
        return false;
    }
}
