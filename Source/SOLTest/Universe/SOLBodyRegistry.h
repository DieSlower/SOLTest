/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Universe/SOLKepler.h"

#include "CoreMinimal.h"

// Definition of one celestial body added to the registry
struct SOLTEST_API FSOLBodyDef
{
    FName Name;
    double RadiusM = 0.0;
    double GM = 0.0;                          // m^3/s^2
    int32 ParentIndex = INDEX_NONE;           // parent must be added before its children; INDEX_NONE for the Sun
    FSOLSecularElements Elements;             // relative to the parent; unused for the Sun
    double RotationPeriodH = 0.0;             // sidereal rotation period, hours, always positive; 0 = no rotation
    double AxialTiltDeg = 0.0;                // 0-180 deg from ecliptic-north; > 90 = retrograde
    double W0Deg = 0.0;                       // rotation angle at J2000, degrees
    double OrbitGM = 0.0;                     // GM used to integrate THIS body's own orbit around its parent
                                               // (vis-viva); 0 = use the parent's own GM (the existing default, used
                                               // for the Sun+8-planet table); set explicitly when a body's real mean
                                               // motion doesn't match its parent's measured GM via Kepler's third law
                                               // (e.g. a close moon shaped by the parent's oblateness) - keeps that
                                               // mismatch from producing a wrong orbital velocity while leaving the
                                               // body's own physical GM (used elsewhere, e.g. its own gravity on
                                               // other things) untouched.
};

// Data-oriented (structure-of-arrays) registry of celestial bodies and their Sun-frame states
class SOLTEST_API FSOLBodyRegistry
{
public:

    // Adds a body and returns its new index
    int32 AddBody(const FSOLBodyDef& def);

    // Clears, then adds the Sun and the eight planets (Sun=0 ... Neptune=8), then the dwarf planets and major moons
    void PopulateSolarSystem();

    // Computes absolute (Sun-frame) positions and velocities; the Sun stays fixed at the origin
    void Update(double secondsSinceJ2000);

    // Returns the number of bodies
    int32 Num() const;

    // Returns the index of the named body, or INDEX_NONE if absent
    int32 FindByName(FName name) const;

    // Returns the body's name
    FName GetName(int32 index) const;

    // Returns the body's parent index
    int32 GetParent(int32 index) const;

    // Returns the body's mean radius in meters
    double GetRadiusM(int32 index) const;

    // Returns the body's gravitational parameter in m^3/s^2
    double GetGM(int32 index) const;

    // Returns the body's Sun-frame position in meters
    const FVector3d& GetPositionM(int32 index) const;

    // Returns the body's Sun-frame velocity in m/s
    const FVector3d& GetVelocityMps(int32 index) const;

    // Returns the body's secular orbital elements relative to its parent (unused for the Sun)
    const FSOLSecularElements& GetElements(int32 index) const;

    // Returns all Sun-frame positions, indexed like the bodies
    TConstArrayView<FVector3d> GetPositionsM() const;

    // Returns all Sun-frame velocities, indexed like the bodies
    TConstArrayView<FVector3d> GetVelocitiesMps() const;

    // Returns the body's orientation (ecliptic frame): fixed axial tilt composed with the current spin
    const FQuat4d& GetOrientation(int32 index) const;

private:

    TArray<FName> mNames;                     // Body names
    TArray<double> mRadiiM;                   // Mean radii
    TArray<double> mGMs;                      // Gravitational parameters (the body's own, used for ITS gravity)
    TArray<double> mOrbitGMs;                 // GM used to integrate each body's own orbit around its parent
    TArray<int32> mParents;                   // Parent indices
    TArray<FSOLSecularElements> mElements;    // Orbital elements relative to the parent
    TArray<FVector3d> mPositionsM;            // Sun-frame positions
    TArray<FVector3d> mVelocitiesMps;         // Sun-frame velocities
    TArray<double> mRotationPeriodsH;         // Sidereal rotation periods (hours); <= 0 = no spin
    TArray<double> mAxialTiltsRad;            // Axial tilts from ecliptic-north
    TArray<double> mW0sRad;                   // Rotation angles at J2000
    TArray<FQuat4d> mOrientations;            // Ecliptic-frame orientations, computed each Update()
};
