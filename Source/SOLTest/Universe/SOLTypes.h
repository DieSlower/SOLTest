// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogSOL, Log, All);

/**
 * Universe frame: heliocentric ecliptic J2000, right-handed, X toward the vernal equinox, Z toward the ecliptic north
 * pole. All positions are meters and all velocities m/s, held in double precision (FVector == FVector3d in UE5).
 * The Sun is fixed at the origin (the solar system barycenter offset of ~1e9 m is ignored).
 */
using FSOLPosition = FVector3d;
using FSOLVelocity = FVector3d;

struct FSOLState
{
	FSOLPosition Position = FSOLPosition::ZeroVector;
	FSOLVelocity Velocity = FVector3d::ZeroVector;
};

namespace SOL
{
	constexpr double AstronomicalUnitM = 149597870700.0;
	constexpr double GravitationalConstant = 6.67430e-11;
	constexpr double SecondsPerDay = 86400.0;
	constexpr double DaysPerJulianCentury = 36525.0;
	constexpr double SecondsPerJulianCentury = SecondsPerDay * DaysPerJulianCentury;
	constexpr double J2000JulianDate = 2451545.0;
	constexpr double UnrealUnitsPerMeter = 100.0;

	/**
	 * Converts between universe axes (right-handed) and Unreal axes (left-handed) by mirroring Y, so the solar system
	 * is not mirror-imaged and orbits stay counter-clockwise seen from ecliptic north. The mapping is its own inverse.
	 * Does not change units.
	 */
	FORCEINLINE FVector3d MirrorHandedness(const FVector3d& V)
	{
		return FVector3d(V.X, -V.Y, V.Z);
	}
}
