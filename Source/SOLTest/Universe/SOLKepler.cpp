/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Universe/SOLKepler.h"

#include "SOLConstants.h"

namespace
{
    constexpr int32 KEPLER_SOLVER_MAX_ITERATIONS = 64;             // Iteration cap for the eccentric-anomaly solver
    constexpr double KEPLER_SOLVER_TOLERANCE_RAD = 1.0e-15;        // Convergence threshold on the Newton step
}

//////////////////////////////////////////////////////////////////////////
// Evaluates the elements at T Julian centuries since J2000, clamping eccentricity to [0, 0.99]
FSOLKeplerElements FSOLSecularElements::AtCenturies(const double t) const
{
    const double longPeriDeg = LongPeri0Deg + LongPeriDotDegPerCy * t;
    const double longNodeDeg = LongNode0Deg + LongNodeDotDegPerCy * t;
    const double meanLongitudeDeg = L0Deg + LDotDegPerCy * t;

    FSOLKeplerElements elements;
    elements.SemiMajorAxisM = (A0AU + ADotAUPerCy * t) * SOL::AU_M;
    elements.Eccentricity = FMath::Clamp(E0 + EDotPerCy * t, SOL::SECULAR_MIN_ECCENTRICITY,
        SOL::SECULAR_MAX_ECCENTRICITY);
    elements.InclinationRad = FMath::DegreesToRadians(I0Deg + IDotDegPerCy * t);
    elements.LongitudeOfAscendingNodeRad = FMath::DegreesToRadians(longNodeDeg);
    elements.ArgumentOfPeriapsisRad = FMath::DegreesToRadians(longPeriDeg - longNodeDeg);
    elements.MeanAnomalyRad = SOLKepler::WrapAngleRad(FMath::DegreesToRadians(meanLongitudeDeg - longPeriDeg));
    return elements;
}

namespace SOLKepler
{
    //////////////////////////////////////////////////////////////////////////
    // Wraps an angle to [-PI, PI)
    double WrapAngleRad(const double angleRad)
    {
        double wrapped = angleRad - UE_DOUBLE_TWO_PI * FMath::FloorToDouble((angleRad + UE_DOUBLE_PI) / UE_DOUBLE_TWO_PI);

        // Guard against rounding pushing the result onto the wrong side of the half-open range
        if (wrapped >= UE_DOUBLE_PI)
        {
            wrapped -= UE_DOUBLE_TWO_PI;
        }
        else if (wrapped < -UE_DOUBLE_PI)
        {
            wrapped += UE_DOUBLE_TWO_PI;
        }
        return wrapped;
    }

    //////////////////////////////////////////////////////////////////////////
    // Solves Kepler's equation with bracketed Newton-Raphson; E is returned on the same branch as M
    double SolveEccentricAnomaly(const double meanAnomalyRad, const double eccentricity)
    {
        if (eccentricity <= 0.0)
        {
            return meanAnomalyRad;
        }

        // Solve on the principal branch, then shift back by the same whole number of turns
        const double wrappedM = WrapAngleRad(meanAnomalyRad);
        const double branchOffset = meanAnomalyRad - wrappedM;

        // E - M = e sin E, and E shares M's sign on [-PI, PI], so the root lies between M and M +- e
        double lo = 0.0;
        double hi = 0.0;
        if (wrappedM >= 0.0)
        {
            lo = wrappedM;
            hi = FMath::Min(wrappedM + eccentricity, UE_DOUBLE_PI);
        }
        else
        {
            lo = FMath::Max(wrappedM - eccentricity, -UE_DOUBLE_PI);
            hi = wrappedM;
        }

        // Low e: classic first-order guess; high e: start at the far bracket end (monotone Newton on a convex branch)
        double eccAnomaly = eccentricity < SOL::KEPLER_HIGH_ECCENTRICITY ? wrappedM + eccentricity * FMath::Sin(wrappedM)
            : (wrappedM >= 0.0 ? hi : lo);
        eccAnomaly = FMath::Clamp(eccAnomaly, lo, hi);

        // Newton-Raphson, falling back to bisection whenever a step leaves the bracket
        for (int32 iteration = 0; iteration < KEPLER_SOLVER_MAX_ITERATIONS; ++iteration)
        {
            const double residual = eccAnomaly - eccentricity * FMath::Sin(eccAnomaly) - wrappedM;
            if (residual == 0.0)
            {
                break;
            }
            if (residual > 0.0)
            {
                hi = eccAnomaly;
            }
            else
            {
                lo = eccAnomaly;
            }

            const double derivative = 1.0 - eccentricity * FMath::Cos(eccAnomaly);
            double next = eccAnomaly - residual / derivative;
            if (!(next >= lo && next <= hi))
            {
                next = 0.5 * (lo + hi);
            }
            const double step = next - eccAnomaly;
            eccAnomaly = next;
            if (FMath::Abs(step) <= KEPLER_SOLVER_TOLERANCE_RAD)
            {
                break;
            }
        }
        return eccAnomaly + branchOffset;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the orbital period 2*PI*sqrt(a^3/GM)
    double PeriodSeconds(const double semiMajorAxisM, const double gm)
    {
        return UE_DOUBLE_TWO_PI * FMath::Sqrt(semiMajorAxisM * semiMajorAxisM * semiMajorAxisM / gm);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the state elapsedSeconds after the instant of the elements (M advances by sqrt(GM/a^3)*elapsed)
    FSOLOrbitState ElementsToState(const FSOLKeplerElements& elements, const double gm, const double elapsedSeconds)
    {
        const double a = elements.SemiMajorAxisM;
        const double e = elements.Eccentricity;
        const double meanMotion = FMath::Sqrt(gm / (a * a * a));
        const double meanAnomaly = WrapAngleRad(elements.MeanAnomalyRad + meanMotion * elapsedSeconds);
        const double eccAnomaly = SolveEccentricAnomaly(meanAnomaly, e);
        const double sinE = FMath::Sin(eccAnomaly);
        const double cosE = FMath::Cos(eccAnomaly);
        const double sqrtOneMinusE2 = FMath::Sqrt(1.0 - e * e);

        // Position and velocity in the perifocal frame (P toward periapsis, Q 90 degrees ahead)
        const double xP = a * (cosE - e);
        const double yP = a * sqrtOneMinusE2 * sinE;
        const double radius = a * (1.0 - e * cosE);
        const double velocityScale = FMath::Sqrt(gm * a) / radius;
        const double vxP = -velocityScale * sinE;
        const double vyP = velocityScale * sqrtOneMinusE2 * cosE;

        // Perifocal-to-ecliptic rotation (3-1-3: node, inclination, argument of periapsis)
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

        FSOLOrbitState state;
        state.PositionM = pHat * xP + qHat * yP;
        state.VelocityMps = pHat * vxP + qHat * vyP;
        return state;
    }
}
