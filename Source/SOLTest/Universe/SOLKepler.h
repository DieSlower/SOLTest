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

    // The documented inverse of ElementsToState: converts a Cartesian position+velocity relative to a central body of
    // the given gm into the instantaneous classical elements describing that exact state (same orbit-plane/angle
    // conventions ElementsToState uses, so ElementsToState(StateToElements(state, gm), gm, 0.0) reproduces state up to
    // floating-point error). Degenerate-orbit fallbacks (part of the contract): when the orbit is equatorial
    // (prograde i = 0 or retrograde i = PI, node vector shorter than 1e-12 of |h|) the ascending node is taken along
    // +X (LongitudeOfAscendingNodeRad = 0); when it is circular (e <= 1e-12) periapsis is placed at the ascending node
    // (ArgumentOfPeriapsisRad = 0), so M is then the argument of latitude. Angles are returned as atan2 produces them
    // (not normalized; M may lie outside [-PI, PI), ElementsToState wraps it on use). Undefined: gm <= 0, a zero
    // position or zero angular momentum, and unbound orbits (e >= 1, parabolic or hyperbolic), which are unsupported.
    SOLTEST_API FSOLKeplerElements StateToElements(const FSOLOrbitState& state, double gm);

    // Wraps an angle in degrees to [0, 360)
    SOLTEST_API double WrapDegrees(double angleDeg);

    // Packs instantaneous classical elements (radians, meters) into FSOLSecularElements as an epoch value: A0AU, E0,
    // I0Deg, LongNode0Deg, LongPeri0Deg (node + argPeri) and L0Deg (node + argPeri + M), the longitudes wrapped to
    // [0, 360). Every rate term is left 0, including LDotDegPerCy, which the caller sets to the mean motion it wants
    // (and, if kepler describes an instant other than J2000, the caller also rolls L0Deg back to the J2000 epoch).
    SOLTEST_API FSOLSecularElements KeplerElementsToSecular(const FSOLKeplerElements& kepler);

    // Returns the mean motion (degrees per Julian century) of a circular-enough orbit with the given semi-major axis
    // around a body of the given gm: sqrt(gm / a^3), converted from rad/s. Shared by any caller that needs to assign
    // a Standish-style FSOLSecularElements::LDotDegPerCy to a procedurally-placed body (the asteroid belt's family
    // fill, the ring patch's rocks) without re-deriving the conversion.
    SOLTEST_API double MeanMotionDegPerCy(double semiMajorAxisM, double gm);
}
