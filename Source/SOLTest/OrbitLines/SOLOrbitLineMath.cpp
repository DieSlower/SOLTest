/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "OrbitLines/SOLOrbitLineMath.h"

#include "SOLConstants.h"
#include "Universe/SOLRenderPlacement.h"

#include <limits>

namespace
{
    constexpr double SIDE_DIRECTION_MIN_CROSS = 1.0e-9;    // Below this |v x t| the ribbon side uses the fallback
    constexpr double SIDE_FALLBACK_MAX_Z = 0.9;            // Fallback crosses with +Z unless the view is this near it

    //////////////////////////////////////////////////////////////////////////
    // True if every component of the vector is finite
    bool OrbitLineIsFiniteVector(const FVector3d& value)
    {
        return FMath::IsFinite(value.X) && FMath::IsFinite(value.Y) && FMath::IsFinite(value.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unit vector along the input, or zero for a zero or non-finite length
    FVector3d NormalizeOrZero(const FVector3d& value)
    {
        const double length = value.Size();
        return (FMath::IsFinite(length) && length > 0.0) ? value / length : FVector3d::ZeroVector;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns x - floor(x)
    double Frac(const double x)
    {
        return x - FMath::FloorToDouble(x);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the integral of the dash square wave (duty d) from 0 to x: floor(x) * d + min(frac(x), d)
    double DashIntegral(const double x, const double duty)
    {
        return FMath::FloorToDouble(x) * duty + FMath::Min(Frac(x), duty);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the angular radius of a valid occluder seen from the camera, or a negative value if it is not valid
    double OccluderAngularRadius(const FVector3d& cameraM, const FVector3d& centerM, const double radiusM)
    {
        if (!OrbitLineIsFiniteVector(centerM) || !FMath::IsFinite(radiusM) || radiusM <= 0.0)
        {
            return -1.0;
        }
        const double distanceM = (centerM - cameraM).Size();
        if (!FMath::IsFinite(distanceM) || distanceM <= radiusM)
        {
            return -1.0;
        }
        return FMath::Asin(radiusM / distanceM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the 3-1-3 periapsis (P) and in-plane normal (Q) unit vectors of SOLKepler::ElementsToState
    void OrbitBasis(const FSOLKeplerElements& elements, FVector3d& outP, FVector3d& outQ)
    {
        const double sinNode = FMath::Sin(elements.LongitudeOfAscendingNodeRad);
        const double cosNode = FMath::Cos(elements.LongitudeOfAscendingNodeRad);
        const double sinArg = FMath::Sin(elements.ArgumentOfPeriapsisRad);
        const double cosArg = FMath::Cos(elements.ArgumentOfPeriapsisRad);
        const double sinInc = FMath::Sin(elements.InclinationRad);
        const double cosInc = FMath::Cos(elements.InclinationRad);
        outP = FVector3d(cosNode * cosArg - sinNode * sinArg * cosInc, sinNode * cosArg + cosNode * sinArg * cosInc,
            sinArg * sinInc);
        outQ = FVector3d(-cosNode * sinArg - sinNode * cosArg * cosInc, -sinNode * sinArg + cosNode * cosArg * cosInc,
            cosArg * sinInc);
    }
}

namespace SOLOrbitLineMath
{
    //////////////////////////////////////////////////////////////////////////
    // True for a finite a > 0, a finite 0 <= e < 1 and finite angles (the mean anomaly is ignored)
    bool IsDrawableOrbit(const FSOLKeplerElements& elements)
    {
        return FMath::IsFinite(elements.SemiMajorAxisM) && elements.SemiMajorAxisM > 0.0
            && FMath::IsFinite(elements.Eccentricity) && elements.Eccentricity >= 0.0 && elements.Eccentricity < 1.0
            && FMath::IsFinite(elements.InclinationRad) && FMath::IsFinite(elements.LongitudeOfAscendingNodeRad)
            && FMath::IsFinite(elements.ArgumentOfPeriapsisRad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a * (2*PI/N)^2 / 8, the chord-error bound of an N-segment uniform-E polyline
    double ChordSagittaM(const double semiMajorAxisM, const int32 segments)
    {
        if (!FMath::IsFinite(semiMajorAxisM) || semiMajorAxisM <= 0.0)
        {
            return 0.0;
        }
        if (segments < 3)
        {
            return std::numeric_limits<double>::infinity();
        }
        const double step = UE_DOUBLE_TWO_PI / static_cast<double>(segments);
        return semiMajorAxisM * step * step / 8.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the smallest N whose chord error is within the radius fraction, clamped to the segment range
    int32 SegmentCountFor(const double semiMajorAxisM, const double bodyRadiusM)
    {
        if (!FMath::IsFinite(semiMajorAxisM) || semiMajorAxisM <= 0.0)
        {
            return SOL::ORBIT_LINE_MIN_SEGMENTS;
        }
        if (!FMath::IsFinite(bodyRadiusM) || bodyRadiusM <= 0.0)
        {
            return SOL::ORBIT_LINE_MAX_SEGMENTS;
        }

        // Closed form N >= 2*PI*sqrt(a / (8 tol)), then nudged so rounding cannot leave it one off
        const double toleranceM = SOL::ORBIT_LINE_SAGITTA_RADIUS_FRACTION * bodyRadiusM;
        const double exact = UE_DOUBLE_TWO_PI * FMath::Sqrt(semiMajorAxisM / (8.0 * toleranceM));
        if (!(exact < static_cast<double>(SOL::ORBIT_LINE_MAX_SEGMENTS)))
        {
            return SOL::ORBIT_LINE_MAX_SEGMENTS;
        }
        int32 segments = FMath::Max(3, static_cast<int32>(FMath::CeilToDouble(exact)));
        while (segments > 3 && ChordSagittaM(semiMajorAxisM, segments - 1) <= toleranceM)
        {
            --segments;
        }
        while (ChordSagittaM(semiMajorAxisM, segments) > toleranceM && segments < SOL::ORBIT_LINE_MAX_SEGMENTS)
        {
            ++segments;
        }
        return FMath::Clamp(segments, SOL::ORBIT_LINE_MIN_SEGMENTS, SOL::ORBIT_LINE_MAX_SEGMENTS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends N + 1 uniform-E points from periapsis with their prograde tangents and cumulative arc lengths
    int32 SampleOrbit(const FSOLKeplerElements& elements, const int32 segments, TArray<FVector3d>& outPointsM,
        TArray<FVector3d>& outTangents, TArray<double>& outArcLengthsM)
    {
        if (!IsDrawableOrbit(elements) || segments < 3)
        {
            return 0;
        }

        FVector3d pHat;
        FVector3d qHat;
        OrbitBasis(elements, pHat, qHat);
        const double a = elements.SemiMajorAxisM;
        const double e = elements.Eccentricity;
        const double b = a * FMath::Sqrt(1.0 - e * e);

        outPointsM.Reserve(outPointsM.Num() + segments + 1);
        outTangents.Reserve(outTangents.Num() + segments + 1);
        outArcLengthsM.Reserve(outArcLengthsM.Num() + segments + 1);
        const int32 firstPoint = outPointsM.Num();
        const int32 firstTangent = outTangents.Num();

        // Points and tangents at E_k = 2*PI*k/N, arc lengths accumulated along the polyline
        double arcLengthM = 0.0;
        FVector3d previous = FVector3d::ZeroVector;
        for (int32 index = 0; index < segments; ++index)
        {
            const double eccentricAnomaly = UE_DOUBLE_TWO_PI * static_cast<double>(index)
                / static_cast<double>(segments);
            double sinE = 0.0;
            double cosE = 0.0;
            FMath::SinCos(&sinE, &cosE, eccentricAnomaly);
            const FVector3d point = pHat * (a * (cosE - e)) + qHat * (b * sinE);
            const FVector3d tangent = (pHat * (-a * sinE) + qHat * (b * cosE)).GetSafeNormal();
            if (index > 0)
            {
                arcLengthM += (point - previous).Size();
            }
            outPointsM.Add(point);
            outTangents.Add(tangent);
            outArcLengthsM.Add(arcLengthM);
            previous = point;
        }

        // Close the loop: entry N repeats entry 0, and its arc length includes the closing chord
        const FVector3d firstPointM = outPointsM[firstPoint];
        const FVector3d firstTangentDir = outTangents[firstTangent];
        arcLengthM += (firstPointM - previous).Size();
        outPointsM.Add(firstPointM);
        outTangents.Add(firstTangentDir);
        outArcLengthsM.Add(arcLengthM);
        return segments + 1;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns arcLength * dashCount / perimeter in dash periods
    double DashPhase(const double arcLengthM, const double perimeterM, const int32 dashCount)
    {
        if (!FMath::IsFinite(perimeterM) || perimeterM <= 0.0 || dashCount < 1 || !FMath::IsFinite(arcLengthM))
        {
            return 0.0;
        }
        return arcLengthM * static_cast<double>(dashCount) / perimeterM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the box-filtered dash coverage over the phase footprint
    double DashCoverage(const double dashPhase, const double dutyCycle, const double phaseFootprint)
    {
        const double duty = FMath::IsNaN(dutyCycle) ? 0.0 : FMath::Clamp(dutyCycle, 0.0, 1.0);
        if (!FMath::IsFinite(dashPhase))
        {
            return 0.0;
        }

        // Point sample for no footprint, the duty cycle once a whole period fits, the exact box mean in between
        if (FMath::IsNaN(phaseFootprint) || phaseFootprint <= 0.0)
        {
            return Frac(dashPhase) < duty ? 1.0 : 0.0;
        }
        if (phaseFootprint >= 1.0)
        {
            return duty;
        }
        const double halfFootprint = 0.5 * phaseFootprint;
        const double coverage = (DashIntegral(dashPhase + halfFootprint, duty)
            - DashIntegral(dashPhase - halfFootprint, duty)) / phaseFootprint;
        return FMath::Clamp(coverage, 0.0, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the multi-scale dash coverage: power-of-two finer dashes up close, then a fade to solid
    double MultiScaleDashCoverage(const double dashPhase, const double dutyCycle, const double phaseFootprint,
        const double minPeriodPx, const int32 maxLevel, const double solidStartPx, const double solidEndPx)
    {
        if (!FMath::IsFinite(dashPhase))
        {
            return 0.0;
        }
        if (FMath::IsNaN(phaseFootprint) || phaseFootprint <= 0.0 || !FMath::IsFinite(phaseFootprint)
            || !(minPeriodPx > 0.0))
        {
            return DashCoverage(dashPhase, dutyCycle, phaseFootprint);
        }

        // Continuous level: how many halvings bring the on-screen period down to minPeriodPx
        const int32 topLevel = FMath::Max(maxLevel, 0);
        const double periodPx = 1.0 / phaseFootprint;
        const double level = FMath::Clamp(FMath::Log2(periodPx / minPeriodPx), 0.0, static_cast<double>(topLevel));
        const int32 lower = FMath::Min(static_cast<int32>(FMath::FloorToDouble(level)), topLevel);
        const double blend = level - static_cast<double>(lower);
        const double lowerScale = FMath::Exp2(static_cast<double>(lower));
        const double lowerCoverage = DashCoverage(dashPhase * lowerScale, dutyCycle, phaseFootprint * lowerScale);
        double coverage = lowerCoverage;
        if (blend > 0.0 && lower < topLevel)
        {
            const double upperScale = 2.0 * lowerScale;
            coverage = FMath::Lerp(lowerCoverage,
                DashCoverage(dashPhase * upperScale, dutyCycle, phaseFootprint * upperScale), blend);
        }

        // Past the last level the period keeps growing: fade to a solid line so it never sits in a gap
        const double remainingPeriodPx = periodPx / FMath::Exp2(level);
        double solid = 0.0;
        if (solidEndPx > solidStartPx)
        {
            solid = FMath::Clamp((remainingPeriodPx - solidStartPx) / (solidEndPx - solidStartPx), 0.0, 1.0);
        }
        else
        {
            solid = remainingPeriodPx >= solidStartPx ? 1.0 : 0.0;
        }
        return FMath::Lerp(coverage, 1.0, solid);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the line width scaled by the viewport height
    double ViewportPixelWidth(const double viewportHeightPx)
    {
        if (!FMath::IsFinite(viewportHeightPx) || viewportHeightPx <= 0.0)
        {
            return SOL::ORBIT_LINE_WIDTH_PX;
        }
        return SOL::ORBIT_LINE_WIDTH_PX * viewportHeightPx / SOL::ORBIT_LINE_REFERENCE_HEIGHT_PX;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the analytic anti-aliased coverage across the line
    double LineCoverage(const double distanceFromCenterPx, const double widthPx)
    {
        if (!FMath::IsFinite(widthPx) || widthPx <= 0.0 || !FMath::IsFinite(distanceFromCenterPx))
        {
            return 0.0;
        }
        const double effectiveWidth = FMath::Max(widthPx, 1.0);
        const double edge = FMath::Clamp(effectiveWidth * 0.5 + 0.5 - FMath::Abs(distanceFromCenterPx), 0.0, 1.0);
        return (widthPx / effectiveWidth) * edge;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ribbon geometry's half width including the feather margin
    double RibbonHalfExtentPx(const double widthPx)
    {
        const double width = (FMath::IsFinite(widthPx) && widthPx > 0.0) ? widthPx : 1.0;
        return FMath::Max(width, 1.0) * 0.5 + SOL::ORBIT_LINE_AA_MARGIN_PX;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the world size of one pixel at the given distance
    double WorldSizePerPixel(const double distance, const double tanHalfHorizontalFov, const double viewportWidthPx)
    {
        if (!FMath::IsFinite(distance) || !FMath::IsFinite(tanHalfHorizontalFov) || !FMath::IsFinite(viewportWidthPx)
            || distance < 0.0 || tanHalfHorizontalFov <= 0.0 || viewportWidthPx <= 0.0)
        {
            return 0.0;
        }
        return 2.0 * distance * tanHalfHorizontalFov / viewportWidthPx;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unit ribbon side direction, with a deterministic fallback when looking along the line
    FVector3d RibbonSideDirection(const FVector3d& cameraToPoint, const FVector3d& tangent)
    {
        if (!OrbitLineIsFiniteVector(cameraToPoint))
        {
            return FVector3d::ZeroVector;
        }
        const FVector3d view = NormalizeOrZero(cameraToPoint);
        if (view.IsZero())
        {
            return FVector3d::ZeroVector;
        }

        // The normal case: perpendicular to both the view ray and the tangent
        if (OrbitLineIsFiniteVector(tangent))
        {
            const FVector3d cross = view ^ NormalizeOrZero(tangent);
            if (cross.Size() > SIDE_DIRECTION_MIN_CROSS)
            {
                return NormalizeOrZero(cross);
            }
        }

        // Fallback: cross the view with a fixed axis that is not near-parallel to it
        const FVector3d axis = FMath::Abs(view.Z) < SIDE_FALLBACK_MAX_Z ? FVector3d::UnitZ() : FVector3d::UnitX();
        return NormalizeOrZero(view ^ axis);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the log-linear distance fade, floored for a highlighted line
    double DistanceFade(const double distanceM, const double semiMajorAxisM, const bool bHighlighted)
    {
        if (!FMath::IsFinite(distanceM) || distanceM < 0.0 || !FMath::IsFinite(semiMajorAxisM) || semiMajorAxisM <= 0.0)
        {
            return 0.0;
        }
        const double startM = SOL::ORBIT_LINE_FADE_START_SMA * semiMajorAxisM;
        const double endM = SOL::ORBIT_LINE_FADE_END_SMA * semiMajorAxisM;
        double fade = 0.0;
        if (distanceM <= startM)
        {
            fade = 1.0;
        }
        else if (distanceM < endM)
        {
            fade = 1.0 - (FMath::Loge(distanceM) - FMath::Loge(startM)) / (FMath::Loge(endM) - FMath::Loge(startM));
        }
        return bHighlighted ? FMath::Max(fade, SOL::ORBIT_LINE_HIGHLIGHT_MIN_ALPHA) : fade;
    }

    //////////////////////////////////////////////////////////////////////////
    // True when the segment from the camera to the point passes strictly inside the sphere
    bool IsPointOccludedBySphere(const FVector3d& cameraM, const FVector3d& pointM, const FVector3d& centerM,
        const double radiusM)
    {
        if (!OrbitLineIsFiniteVector(cameraM) || !OrbitLineIsFiniteVector(pointM) || !OrbitLineIsFiniteVector(centerM)
            || !FMath::IsFinite(radiusM)
            || radiusM <= 0.0)
        {
            return false;
        }

        // Work relative to the camera so solar-system-scale inputs keep their precision
        const FVector3d toCenter = centerM - cameraM;
        if (toCenter.SizeSquared() <= radiusM * radiusM)
        {
            return false;
        }
        const FVector3d toPoint = pointM - cameraM;
        const double lengthSq = toPoint.SizeSquared();
        double t = 0.0;
        if (lengthSq > 0.0)
        {
            t = FMath::Clamp((toCenter | toPoint) / lengthSq, 0.0, 1.0);
        }
        const FVector3d closest = toPoint * t;
        return (toCenter - closest).SizeSquared() < radiusM * radiusM;
    }

    //////////////////////////////////////////////////////////////////////////
    // True when any of the spheres occludes the point
    bool IsPointOccluded(const FVector3d& cameraM, const FVector3d& pointM, const TConstArrayView<FVector3d> centersM,
        const TConstArrayView<double> radiiM)
    {
        const int32 count = FMath::Min(centersM.Num(), radiiM.Num());
        for (int32 index = 0; index < count; ++index)
        {
            if (IsPointOccludedBySphere(cameraM, pointM, centersM[index], radiiM[index]))
            {
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills outIndices with the valid occluders, largest angular radius first, truncated to maxCount
    int32 SelectOccluders(const FVector3d& cameraM, const TConstArrayView<FVector3d> centersM,
        const TConstArrayView<double> radiiM, const double minAngularRadiusRad, const int32 maxCount,
        TArray<int32>& outIndices)
    {
        outIndices.Reset();
        const int32 capacity = FMath::Max(0, maxCount);
        const double minAngle = FMath::IsNaN(minAngularRadiusRad) ? 0.0 : minAngularRadiusRad;
        const int32 count = FMath::Min(centersM.Num(), radiiM.Num());

        // Bounded insertion sort: strictly larger angles go first, so ties keep the lower (earlier) index first
        for (int32 index = 0; index < count && capacity > 0; ++index)
        {
            const double angle = OccluderAngularRadius(cameraM, centersM[index], radiiM[index]);
            if (angle < 0.0 || angle < minAngle)
            {
                continue;
            }
            int32 slot = outIndices.Num();
            while (slot > 0
                && OccluderAngularRadius(cameraM, centersM[outIndices[slot - 1]], radiiM[outIndices[slot - 1]]) < angle)
            {
                --slot;
            }
            if (slot >= capacity)
            {
                continue;
            }
            if (outIndices.Num() >= capacity)
            {
                outIndices.Pop(EAllowShrinking::No);
            }
            outIndices.Insert(index, slot);
        }
        return outIndices.Num();
    }

    //////////////////////////////////////////////////////////////////////////
    // True only when the scene depth is inside the 1:1 zone and the line lies beyond it
    bool IsHiddenBySceneDepth(const double lineDistanceCm, const double sceneDepthCm, const double maxRenderDistanceCm)
    {
        if (!FMath::IsFinite(lineDistanceCm) || !FMath::IsFinite(sceneDepthCm) || !FMath::IsFinite(maxRenderDistanceCm))
        {
            return false;
        }
        if (sceneDepthCm <= 0.0 || sceneDepthCm >= maxRenderDistanceCm)
        {
            return false;
        }
        return lineDistanceCm > sceneDepthCm * (1.0 + SOL::ORBIT_LINE_DEPTH_BIAS);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the line point's render location, placed exactly like a body at that true position
    FVector3d PlaceLinePointCm(const FVector3d& relativeToObserverM, const double maxRenderDistanceCm)
    {
        return SOLRender::ComputePlacement(relativeToObserverM, 0.0, maxRenderDistanceCm).LocationCm;
    }

    //////////////////////////////////////////////////////////////////////////
    // Splits a double into a high float and the float of the remainder
    void SplitDouble(const double value, float& outHigh, float& outLow)
    {
        outHigh = static_cast<float>(value);
        outLow = FMath::IsFinite(value) ? static_cast<float>(value - static_cast<double>(outHigh)) : 0.0f;
        if (!FMath::IsFinite(outLow))
        {
            outLow = 0.0f;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Splits a vector into float pairs per component
    void SplitVector(const FVector3d& value, FVector3f& outHigh, FVector3f& outLow)
    {
        SplitDouble(value.X, outHigh.X, outLow.X);
        SplitDouble(value.Y, outHigh.Y, outLow.Y);
        SplitDouble(value.Z, outHigh.Z, outLow.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an upper bound on the movement of any sampled point between the two element sets
    double OrbitDriftM(const FSOLKeplerElements& built, const FSOLKeplerElements& now)
    {
        if (!IsDrawableOrbit(built) || !IsDrawableOrbit(now))
        {
            return std::numeric_limits<double>::infinity();
        }
        const double maxA = FMath::Max(built.SemiMajorAxisM, now.SemiMajorAxisM);
        const double maxE = FMath::Max(built.Eccentricity, now.Eccentricity);
        const double angleSum = FMath::Abs(SOLKepler::WrapAngleRad(now.InclinationRad - built.InclinationRad))
            + FMath::Abs(SOLKepler::WrapAngleRad(now.LongitudeOfAscendingNodeRad - built.LongitudeOfAscendingNodeRad))
            + FMath::Abs(SOLKepler::WrapAngleRad(now.ArgumentOfPeriapsisRad - built.ArgumentOfPeriapsisRad));
        return FMath::Abs(now.SemiMajorAxisM - built.SemiMajorAxisM) * (1.0 + maxE)
            + maxA * FMath::Abs(now.Eccentricity - built.Eccentricity) * (1.0 + maxE / FMath::Sqrt(1.0 - maxE * maxE))
            + maxA * (1.0 + maxE) * angleSum;
    }

    //////////////////////////////////////////////////////////////////////////
    // True when the drift since the line was built exceeds the rebuild tolerance
    bool NeedsRebuild(const FSOLKeplerElements& built, const FSOLKeplerElements& now, const double bodyRadiusM)
    {
        if (!IsDrawableOrbit(now))
        {
            return false;
        }
        if (!IsDrawableOrbit(built))
        {
            return true;
        }
        const double toleranceM = (FMath::IsFinite(bodyRadiusM) && bodyRadiusM > 0.0)
            ? SOL::ORBIT_LINE_SAGITTA_RADIUS_FRACTION * bodyRadiusM
            : SOL::ORBIT_LINE_REBUILD_FALLBACK_FRACTION * now.SemiMajorAxisM;
        return OrbitDriftM(built, now) > toleranceM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Lists every registry body with a parent, then the largest named minor bodies
    void BuildPermanentLines(const TConstArrayView<int32> registryParents, const TConstArrayView<double> minorRadiiM,
        const TConstArrayView<bool> minorIsNamed, const int32 maxMinorLines, TArray<FLineKey>& outLines)
    {
        outLines.Reset();
        for (int32 index = 0; index < registryParents.Num(); ++index)
        {
            if (registryParents[index] != INDEX_NONE)
            {
                outLines.Add({ ELineSource::RegistryBody, index });
            }
        }

        // Bounded insertion sort of the named minor bodies by radius (strictly larger first, ties to the lower index)
        const int32 firstMinor = outLines.Num();
        const int32 capacity = FMath::Max(0, maxMinorLines);
        const int32 minorCount = FMath::Min(minorRadiiM.Num(), minorIsNamed.Num());
        for (int32 index = 0; index < minorCount && capacity > 0; ++index)
        {
            const double radiusM = minorRadiiM[index];
            if (!minorIsNamed[index] || !FMath::IsFinite(radiusM) || radiusM <= 0.0)
            {
                continue;
            }
            int32 slot = outLines.Num();
            while (slot > firstMinor && minorRadiiM[outLines[slot - 1].Index] < radiusM)
            {
                --slot;
            }
            if (slot - firstMinor >= capacity)
            {
                continue;
            }
            if (outLines.Num() - firstMinor >= capacity)
            {
                outLines.Pop(EAllowShrinking::No);
            }
            outLines.Insert({ ELineSource::MinorBody, index }, slot);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Resolves the highlight slot or transient line for the current target
    FTargetLineResult ResolveTargetLine(const bool bLinesEnabled, const FLineKey& target, const bool bTargetHasOrbit,
        const TConstArrayView<FLineKey> permanentLines)
    {
        FTargetLineResult result;
        if (!bLinesEnabled || target.Source == ELineSource::None || target.Index == INDEX_NONE)
        {
            return result;
        }
        for (int32 slot = 0; slot < permanentLines.Num(); ++slot)
        {
            if (permanentLines[slot] == target)
            {
                result.HighlightSlot = slot;
                return result;
            }
        }
        result.bShowTransient = bTargetHasOrbit;
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the highlight color if highlighted, else the normal line color
    FLinearColor LineColor(const bool bHighlighted)
    {
        return bHighlighted ? SOL::ORBIT_LINE_HIGHLIGHT_COLOR : SOL::ORBIT_LINE_COLOR;
    }
}
