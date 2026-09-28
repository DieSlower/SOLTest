/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Orbit at one instant; angles in radians, distances in meters
struct SOLTEST_API FSOLKeplerElements
{
    double SemiMajorAxisM = 0.0;
    double Eccentricity = 0.0;                 // 0 <= e < 1
    double InclinationRad = 0.0;
    double LongitudeOfAscendingNodeRad = 0.0;
    double ArgumentOfPeriapsisRad = 0.0;
    double MeanAnomalyRad = 0.0;               // at the instant the elements describe
};

// Position and velocity relative to the central body, J2000 ecliptic axes
struct SOLTEST_API FSOLOrbitState
{
    FVector3d PositionM = FVector3d::ZeroVector;
    FVector3d VelocityMps = FVector3d::ZeroVector;
};

// JPL Standish "approximate positions of the major planets", 1800-2050
struct SOLTEST_API FSOLSecularElements
{
    double A0AU = 0.0;
    double ADotAUPerCy = 0.0;                  // semi-major axis
    double E0 = 0.0;
    double EDotPerCy = 0.0;                    // eccentricity
    double I0Deg = 0.0;
    double IDotDegPerCy = 0.0;                 // inclination
    double L0Deg = 0.0;
    double LDotDegPerCy = 0.0;                 // mean longitude
    double LongPeri0Deg = 0.0;
    double LongPeriDotDegPerCy = 0.0;          // longitude of perihelion
    double LongNode0Deg = 0.0;
    double LongNodeDotDegPerCy = 0.0;          // longitude of ascending node

    // Evaluates the elements at T Julian centuries since J2000 (ArgPeri = LongPeri - LongNode; M = L - LongPeri; e clamped)
    FSOLKeplerElements AtCenturies(double t) const;
};

namespace SOLKepler
{
    // Wraps an angle to [-PI, PI)
    SOLTEST_API double WrapAngleRad(double angleRad);

    // Solves Kepler's equation for any M so that |M - (E - e*sin E)| < 1e-10
    SOLTEST_API double SolveEccentricAnomaly(double meanAnomalyRad, double eccentricity);

    // Returns the orbital period 2*PI*sqrt(a^3/GM)
    SOLTEST_API double PeriodSeconds(double semiMajorAxisM, double gm);

    // Returns the state elapsedSeconds after the instant of the elements (M advances by sqrt(GM/a^3)*elapsed)
    SOLTEST_API FSOLOrbitState ElementsToState(const FSOLKeplerElements& elements, double gm, double elapsedSeconds);
}
