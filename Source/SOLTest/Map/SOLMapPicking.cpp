/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Map/SOLMapPicking.h"

#include "SOLConstants.h"

// Jump-map picking math (SDD 3, Appendix D). Large universe coordinates are always differenced first, and only the
// resulting small relative vectors are dotted or scaled, so results stay accurate at solar-system scale
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns true only if every component of the vector is finite (not NaN and not +/-Inf)
    bool MapPickIsFinite(const FVector3d& value)
    {
        return FMath::IsFinite(value.X) && FMath::IsFinite(value.Y) && FMath::IsFinite(value.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Normalizes any finite, nonzero vector regardless of magnitude; false only for an exactly-zero vector
    bool MapPickNormalizeAnyMagnitude(const FVector3d& value, FVector3d& outUnit)
    {
        // Pre-scale by the largest component so tiny or huge vectors neither underflow nor overflow when squared
        const double maxAbs = FMath::Max3(FMath::Abs(value.X), FMath::Abs(value.Y), FMath::Abs(value.Z));
        if (!(maxAbs >= TNumericLimits<double>::Min()) || !FMath::IsFinite(maxAbs))
        {
            return false;
        }
        const FVector3d scaled = value / maxAbs;
        outUnit = scaled / FMath::Sqrt(scaled.SizeSquared());
        return true;
    }
}

namespace SOLMapPicking
{
    //////////////////////////////////////////////////////////////////////////
    // Intersects a ray (half-line, t >= 0) with a two-sided plane; false if parallel, behind the origin or non-finite
    bool RayPlaneIntersect(const FVector3d& rayOrigin, const FVector3d& rayDir, const FVector3d& planeOrigin,
        const FVector3d& planeNormal, FVector3d& outPoint)
    {
        outPoint = FVector3d::ZeroVector;

        // Reject non-finite input explicitly; NaN comparisons are always false and would slip past the checks below
        if (!MapPickIsFinite(rayOrigin) || !MapPickIsFinite(rayDir) || !MapPickIsFinite(planeOrigin)
            || !MapPickIsFinite(planeNormal))
        {
            return false;
        }

        FVector3d unitNormal;
        if (!MapPickNormalizeAnyMagnitude(planeNormal, unitNormal))
        {
            return false;
        }
        const double denom = FVector3d::DotProduct(rayDir, unitNormal);
        if (!(FMath::Abs(denom) >= SOL::MAP_PICK_PARALLEL_EPSILON))
        {
            return false;
        }

        // Relative vector first, then dot: t is the signed distance along the ray to the plane
        const FVector3d planeToRay = rayOrigin - planeOrigin;
        const double t = -FVector3d::DotProduct(planeToRay, unitNormal) / denom;
        if (!(t >= 0.0) || !FMath::IsFinite(t))
        {
            return false;
        }

        // Build the hit as an offset from the plane origin and strip any residual normal component, so the point lies
        // exactly on the plane before the large plane origin is added back
        FVector3d offset = planeToRay + t * rayDir;
        offset -= FVector3d::DotProduct(offset, unitNormal) * unitNormal;
        if (!MapPickIsFinite(offset))
        {
            return false;
        }
        outPoint = planeOrigin + offset;
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns (point - planeOrigin) projected onto eclipticX and eclipticY; (0, 0) for any non-finite input
    FVector2D DecomposePlanarOffset(const FVector3d& point, const FVector3d& planeOrigin,
        const FVector3d& eclipticX, const FVector3d& eclipticY)
    {
        if (!MapPickIsFinite(point) || !MapPickIsFinite(planeOrigin) || !MapPickIsFinite(eclipticX)
            || !MapPickIsFinite(eclipticY))
        {
            return FVector2D::ZeroVector;
        }
        const FVector3d rel = point - planeOrigin;
        const FVector2D result(FVector3d::DotProduct(rel, eclipticX), FVector3d::DotProduct(rel, eclipticY));
        return (FMath::IsFinite(result.X) && FMath::IsFinite(result.Y)) ? result : FVector2D::ZeroVector;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns referencePositionM plus the (dx, dy, dz) offset along the supplied basis; zero for any non-finite input
    FVector3d ComposeDestination(const FVector3d& referencePositionM, double planarOffsetXM,
        double planarOffsetYM, double heightOffsetZM, const FVector3d& eclipticX,
        const FVector3d& eclipticY, const FVector3d& up)
    {
        if (!MapPickIsFinite(referencePositionM) || !FMath::IsFinite(planarOffsetXM)
            || !FMath::IsFinite(planarOffsetYM) || !FMath::IsFinite(heightOffsetZM) || !MapPickIsFinite(eclipticX)
            || !MapPickIsFinite(eclipticY) || !MapPickIsFinite(up))
        {
            return FVector3d::ZeroVector;
        }
        const FVector3d offset = planarOffsetXM * eclipticX + planarOffsetYM * eclipticY + heightOffsetZM * up;
        const FVector3d destination = referencePositionM + offset;
        return MapPickIsFinite(destination) ? destination : FVector3d::ZeroVector;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the index of the pick sphere the ray enters first (smallest entry t >= 0), or INDEX_NONE
    int32 PickBodyUnderRay(const FVector3d& rayOrigin, const FVector3d& rayDir,
        TConstArrayView<FVector3d> bodyPositionsM, TConstArrayView<double> bodyPickRadiiM)
    {
        if (!MapPickIsFinite(rayOrigin) || !MapPickIsFinite(rayDir))
        {
            return INDEX_NONE;
        }

        // Mismatched parallel arrays are a caller bug; only the common prefix is ever read, so it stays in bounds
        const int32 count = FMath::Min(bodyPositionsM.Num(), bodyPickRadiiM.Num());
        int32 bestIndex = INDEX_NONE;
        double bestEntryM = TNumericLimits<double>::Max();

        for (int32 index = 0; index < count; ++index)
        {
            // A body with a non-finite position or a non-finite / negative radius can never be picked
            const double radiusM = bodyPickRadiiM[index];
            if (!MapPickIsFinite(bodyPositionsM[index]) || !FMath::IsFinite(radiusM) || !(radiusM >= 0.0))
            {
                continue;
            }
            const FVector3d rel = bodyPositionsM[index] - rayOrigin;
            const double radiusSq = radiusM * radiusM;

            // Addendum ordering: an origin already inside the sphere is a t = 0 candidate wherever the center lies
            if (rel.SizeSquared() < radiusSq)
            {
                if (0.0 < bestEntryM)
                {
                    bestEntryM = 0.0;
                    bestIndex = index;
                }
                continue;
            }

            // Origin outside: bodies centered behind the origin are never picked
            const double alongM = FVector3d::DotProduct(rel, rayDir);
            if (!(alongM >= 0.0))
            {
                continue;
            }

            // Perpendicular miss distance from the ray line, compared against the pick radius
            const double missSq = (rel - alongM * rayDir).SizeSquared();
            if (!(missSq <= radiusSq))
            {
                continue;
            }

            // Entry distance into the sphere along the ray
            const double entryM = FMath::Max(0.0, alongM - FMath::Sqrt(radiusSq - missSq));
            if (entryM < bestEntryM)
            {
                bestEntryM = entryM;
                bestIndex = index;
            }
        }
        return bestIndex;
    }
}
