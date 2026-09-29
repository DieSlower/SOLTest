/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOrbitLines.h"

namespace SOLOrbitLines
{
    //////////////////////////////////////////////////////////////////////////
    // Appends EllipseSegments+1 parent-relative points (closed loop), true anomaly uniform over [0, 2*PI]
    void SampleEllipse(const FSOLKeplerElements& elements, TArray<FVector3d>& outPoints)
    {
        const double e = elements.Eccentricity;
        const double semiLatusRectum = elements.SemiMajorAxisM * (1.0 - e * e);

        // Perifocal-to-ecliptic basis (3-1-3: node, inclination, argument of periapsis), as in SOLKepler::ElementsToState
        const double sinNode = FMath::Sin(elements.LongitudeOfAscendingNodeRad);
        const double cosNode = FMath::Cos(elements.LongitudeOfAscendingNodeRad);
        const double sinArg = FMath::Sin(elements.ArgumentOfPeriapsisRad);
        const double cosArg = FMath::Cos(elements.ArgumentOfPeriapsisRad);
        const double sinInc = FMath::Sin(elements.InclinationRad);
        const double cosInc = FMath::Cos(elements.InclinationRad);
        const FVector3d pHat(cosNode * cosArg - sinNode * sinArg * cosInc, sinNode * cosArg + cosNode * sinArg * cosInc,
            sinArg * sinInc);
        const FVector3d qHat(-cosNode * sinArg - sinNode * cosArg * cosInc, -sinNode * sinArg + cosNode * cosArg * cosInc,
            cosArg * sinInc);

        // No-op when the caller already reserved enough capacity
        const int32 firstIndex = outPoints.Num();
        outPoints.Reserve(firstIndex + EllipseSegments + 1);

        // Conic radius r = p / (1 + e cos nu) at uniform true anomaly steps, placed in the orbital plane
        for (int32 segment = 0; segment < EllipseSegments; ++segment)
        {
            const double trueAnomaly = UE_DOUBLE_TWO_PI * segment / EllipseSegments;
            const double cosNu = FMath::Cos(trueAnomaly);
            const double sinNu = FMath::Sin(trueAnomaly);
            const double radius = semiLatusRectum / (1.0 + e * cosNu);
            outPoints.Add(pHat * (radius * cosNu) + qHat * (radius * sinNu));
        }

        // Close the loop exactly on the first point
        const FVector3d firstPoint = outPoints[firstIndex];
        outPoints.Add(firstPoint);
    }
}
