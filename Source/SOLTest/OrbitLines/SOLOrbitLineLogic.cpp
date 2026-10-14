/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "OrbitLines/SOLOrbitLineLogic.h"

#include "SOLConstants.h"

#include <limits>

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Wraps a slot into [0, count) for a positive count
    int32 WrapSlot(const int32 slot, const int32 count)
    {
        return ((slot % count) + count) % count;
    }
}

namespace SOLOrbitLineLogic
{
    //////////////////////////////////////////////////////////////////////////
    // Maps a targeting candidate to a line key (registry bodies only; other targetables have no orbit)
    SOLOrbitLineMath::FLineKey TargetKeyForCandidate(const int32 selectedCandidate,
        const TConstArrayView<int32> registryParents, bool& outHasOrbit)
    {
        outHasOrbit = false;
        SOLOrbitLineMath::FLineKey key;
        if (selectedCandidate >= 0 && selectedCandidate < registryParents.Num())
        {
            key.Source = SOLOrbitLineMath::ELineSource::RegistryBody;
            key.Index = selectedCandidate;
            outHasOrbit = registryParents[selectedCandidate] != INDEX_NONE;
        }
        return key;
    }

    //////////////////////////////////////////////////////////////////////////
    // Picks up to maxRebuilds flagged slots, scanning once round robin from startSlot
    int32 SelectDriftRebuilds(const TConstArrayView<bool> needsRebuild, const int32 startSlot, const int32 maxRebuilds,
        TArray<int32>& outSlots)
    {
        outSlots.Reset();
        const int32 count = needsRebuild.Num();
        if (count == 0 || maxRebuilds <= 0)
        {
            return 0;
        }
        const int32 start = WrapSlot(startSlot, count);
        for (int32 step = 0; step < count && outSlots.Num() < maxRebuilds; ++step)
        {
            const int32 slot = (start + step) % count;
            if (needsRebuild[slot])
            {
                outSlots.Add(slot);
            }
        }
        return outSlots.Num();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the next round-robin start
    int32 NextRebuildCursor(const int32 cursor, const int32 lineCount)
    {
        return lineCount <= 0 ? 0 : WrapSlot(cursor + 1, lineCount);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the orbit's orientation Rz(node) Rx(inclination) Rz(argument)
    FQuat4d OrbitOrientation(const FSOLKeplerElements& elements)
    {
        const FQuat4d node(FVector3d::UnitZ(), elements.LongitudeOfAscendingNodeRad);
        const FQuat4d inclination(FVector3d::UnitX(), elements.InclinationRad);
        const FQuat4d argument(FVector3d::UnitZ(), elements.ArgumentOfPeriapsisRad);
        return (node * inclination * argument).GetNormalized();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the rotation from built's orientation to now's
    FQuat4d OrbitRotationBetween(const FSOLKeplerElements& built, const FSOLKeplerElements& now)
    {
        return (OrbitOrientation(now) * OrbitOrientation(built).Inverse()).GetNormalized();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns q.RotateVector(v) - v without forming q v, so a small rotation keeps relative precision
    FVector3d RotationOffset(const FQuat4d& q, const FVector3d& v)
    {
        const FVector3d axis(q.X, q.Y, q.Z);
        return 2.0 * (axis ^ ((axis ^ v) + q.W * v));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the semi-major axis and eccentricity part of the drift bound
    double OrbitShapeDriftM(const FSOLKeplerElements& built, const FSOLKeplerElements& now)
    {
        if (!SOLOrbitLineMath::IsDrawableOrbit(built) || !SOLOrbitLineMath::IsDrawableOrbit(now))
        {
            return std::numeric_limits<double>::infinity();
        }
        const double maxA = FMath::Max(built.SemiMajorAxisM, now.SemiMajorAxisM);
        const double maxE = FMath::Max(built.Eccentricity, now.Eccentricity);
        return FMath::Abs(now.SemiMajorAxisM - built.SemiMajorAxisM) * (1.0 + maxE)
            + maxA * FMath::Abs(now.Eccentricity - built.Eccentricity) * (1.0 + maxE / FMath::Sqrt(1.0 - maxE * maxE));
    }

    //////////////////////////////////////////////////////////////////////////
    // True when the shape drifted beyond the rebuild tolerance
    bool NeedsShapeRebuild(const FSOLKeplerElements& built, const FSOLKeplerElements& now, const double bodyRadiusM)
    {
        if (!SOLOrbitLineMath::IsDrawableOrbit(now))
        {
            return false;
        }
        if (!SOLOrbitLineMath::IsDrawableOrbit(built))
        {
            return true;
        }
        const double toleranceM = (FMath::IsFinite(bodyRadiusM) && bodyRadiusM > 0.0)
            ? SOL::ORBIT_LINE_SAGITTA_RADIUS_FRACTION * bodyRadiusM
            : SOL::ORBIT_LINE_REBUILD_FALLBACK_FRACTION * now.SemiMajorAxisM;
        return OrbitShapeDriftM(built, now) > toleranceM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the depth below which no log-compressed body's surface can lie
    double SceneDepthTrustLimitCm(const FVector3d& viewpointM, const TConstArrayView<FVector3d> centersM,
        const TConstArrayView<double> radiiM, const double maxRenderDistanceCm)
    {
        double largestFraction = 0.0;
        const int32 count = FMath::Min(centersM.Num(), radiiM.Num());
        for (int32 index = 0; index < count; ++index)
        {
            const double radiusM = radiiM[index];
            const FVector3d& centerM = centersM[index];
            if (!FMath::IsFinite(radiusM) || radiusM <= 0.0 || !FMath::IsFinite(centerM.X)
                || !FMath::IsFinite(centerM.Y) || !FMath::IsFinite(centerM.Z))
            {
                continue;
            }
            const double distanceM = (centerM - viewpointM).Size();
            if (distanceM * SOL::METERS_TO_CM > maxRenderDistanceCm)
            {
                largestFraction = FMath::Max(largestFraction, FMath::Min(radiusM / distanceM, 1.0));
            }
        }
        return maxRenderDistanceCm * (1.0 - largestFraction);
    }

    //////////////////////////////////////////////////////////////////////////
    // True when the whole line is beyond the fade-out distance
    bool IsLineBeyondFade(const double cameraToParentDistanceM, const double semiMajorAxisM, const double eccentricity,
        const bool bHighlighted)
    {
        if (bHighlighted || !FMath::IsFinite(cameraToParentDistanceM) || !FMath::IsFinite(semiMajorAxisM)
            || !FMath::IsFinite(eccentricity) || semiMajorAxisM <= 0.0)
        {
            return false;
        }
        return cameraToParentDistanceM - semiMajorAxisM * (1.0 + eccentricity)
            >= SOL::ORBIT_LINE_FADE_END_SMA * semiMajorAxisM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the lines are drawn
    bool AreLinesVisible(const bool bSettingShown, const bool bInMainMenu)
    {
        return bSettingShown && !bInMainMenu;
    }
}
