// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Universe/SOLTypes.h"

/** Classical elliptical orbit in SI/radians, evaluated at one instant. Angles are relative to the ecliptic J2000. */
struct FSOLKeplerElements
{
	double SemiMajorAxisM = 0.0;
	double Eccentricity = 0.0;
	double InclinationRad = 0.0;
	double ArgumentOfPeriapsisRad = 0.0;
	double LongitudeOfAscendingNodeRad = 0.0;
	/** Mean anomaly at the evaluation instant. */
	double MeanAnomalyRad = 0.0;
	/** dM/dt, rad/s. Sets the velocity magnitude; see SetMeanMotionFromGM for the two-body-consistent value. */
	double MeanMotionRadPerSec = 0.0;

	void SetMeanMotionFromGM(double ParentGM)
	{
		MeanMotionRadPerSec = FMath::Sqrt(ParentGM / (SemiMajorAxisM * SemiMajorAxisM * SemiMajorAxisM));
	}

	double GetPeriodSeconds() const { return UE_DOUBLE_TWO_PI / MeanMotionRadPerSec; }
};

/**
 * JPL "Keplerian elements for approximate positions of the major planets" (E.M. Standish), table 1, valid 1800-2050 AD:
 * element values at J2000 plus linear rates per Julian century. Units as published: AU, degrees.
 */
struct FSOLSecularElements
{
	double A0 = 0.0, ADot = 0.0;
	double E0 = 0.0, EDot = 0.0;
	double I0 = 0.0, IDot = 0.0;
	double L0 = 0.0, LDot = 0.0;
	double VarPi0 = 0.0, VarPiDot = 0.0;
	double Omega0 = 0.0, OmegaDot = 0.0;

	/** T is Julian centuries (36525 d) since J2000. Mean motion is the rate of M = L - varpi, matching the position. */
	FSOLKeplerElements Evaluate(double CenturiesSinceJ2000) const;
};

namespace SOLKepler
{
	/** Wraps to (-PI, PI]. */
	double WrapAngle(double Radians);

	/** Solves Kepler's equation M = E - e sin E for the eccentric anomaly of an elliptic orbit (0 <= e < 1). */
	double SolveEccentricAnomaly(double MeanAnomalyRad, double Eccentricity, double Tolerance = 1e-13, int32 MaxIterations = 60);

	/** Position (m) and velocity (m/s) relative to the parent body, in the ecliptic J2000 universe frame. */
	FSOLState ElementsToState(const FSOLKeplerElements& Elements);
}
