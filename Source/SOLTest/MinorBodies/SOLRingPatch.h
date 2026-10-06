/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "MinorBodies/SOLPlanetRing.h"
#include "Universe/SOLKepler.h"

#include "CoreMinimal.h"

// Identifies one cell of a ring's local polar cell grid (SDD 6 Appendix D). Cells are SOLRingPatch::CELL_SIZE_M on a
// side in the ring's own plane: RadialIndex counts CELL_SIZE_M steps outward from ringDef.InnerRadiusM itself (NOT
// from InnerRadiusM - marginM — indexing is deliberately independent of whatever margin a given ComputeActiveCell
// call used, so GenerateCellRocks, which has no marginM of its own, can still reconstruct a cell's absolute radial
// span from RadialIndex alone: [InnerRadiusM + RadialIndex*CELL_SIZE_M, InnerRadiusM + (RadialIndex+1)*CELL_SIZE_M)).
// RadialIndex is negative for a cell entirely within the margin band below InnerRadiusM. AngularIndex counts
// CELL_SIZE_M-arc-length steps around the circle AT THAT RADIAL BAND's own reference radius (so angular cells stay
// roughly square at any ring radius, rather than a fixed angular width that would make cells enormous far out and
// tiny close in) — see SOLRingPatch.cpp for the exact radius-to-index and index-to-radius math. Neighboring cells in
// either index are adjacent in space; a caller building an active grid around ComputeActiveCell's result (e.g. a 3x3
// window) offsets RadialIndex/AngularIndex directly, wrapping AngularIndex at that radial band's own cell count.
struct SOLTEST_API FSOLRingCell
{
    int32 RadialIndex = 0;
    int32 AngularIndex = 0;

    // Returns true when both indices match
    bool operator==(const FSOLRingCell& other) const
    {
        return RadialIndex == other.RadialIndex && AngularIndex == other.AngularIndex;
    }
};

// Returns a hash combining both indices, so FSOLRingCell can key a TMap/TSet (a streaming pool's cell bookkeeping)
SOLTEST_API inline uint32 GetTypeHash(const FSOLRingCell& cell)
{
    return HashCombine(::GetTypeHash(cell.RadialIndex), ::GetTypeHash(cell.AngularIndex));
}

// One procedurally-placed ring rock (SDD 6 Appendix D): Elements are planet-relative (not Sun-relative), ready to
// assign directly to FSOLMinorBodyOrbitFragment::Elements with ParentBodyIndex set to the host planet
struct SOLTEST_API FSOLRingRockDef
{
    FSOLSecularElements Elements;
    double RadiusM = 0.0;
};

// Pure logic for the near-field ring patch (SDD 6 Amendment 6, Appendix D): which local cell of a ring the player is
// currently in (if any), deterministic per-cell rock generation, and the near/far cross-fade weight. Deliberately
// decoupled from Mass/the body registry — callers pass positions, a GM and a ring plane normal already resolved, so
// this module stays pure and trivially testable, same convention as SOLPlanetRing and SOLAsteroidBelt.
namespace SOLRingPatch
{
    // Cell size (meters) in the ring's local tangent plane: small enough that curvature over one cell is negligible
    // next to any real ring's tens-of-thousands-of-km radius (SOLPlanetRing.cpp's RING_REAL_RINGS)
    SOLTEST_API extern const double CELL_SIZE_M;

    // Target rocks generated per (non-gap) cell, at the approved ~50-150m average rock spacing's midpoint (~100m):
    // roughly (CELL_SIZE_M / 100)^2 rocks scattered through the cell's volume
    SOLTEST_API extern const int32 ROCKS_PER_CELL;

    // THE CO-ROTATING FRAME (read this before ComputeActiveCell/GenerateCellRocks): a ring rock orbits at real orbital
    // speed (tens of km/s for a close-in gas-giant ring) - far faster than CELL_SIZE_M per second - so a cell grid
    // fixed in the planet's (non-rotating) equatorial frame would see every rock leave its cell within a fraction of
    // a second, and the active-cell window would need reassigning dozens of times per second even with a motionless
    // player. Both functions below instead define AngularIndex in a frame CO-ROTATING with the local circular mean
    // motion at the cell's own radial band (sqrt(planetGM / referenceRadiusM^3), referenceRadiusM = the band's own
    // center radius): co-rotatingAngle(t) = worldAngle(t) - meanMotionRadPerS * secondsSinceJ2000, wrapped to
    // [0, 2*PI). A rock generated near-circular at (approximately) the band's reference radius then advances at
    // (approximately) that same rate, so it stays at (approximately) the same co-rotating angle indefinitely - true
    // orbital shear across one CELL_SIZE_M-wide radial band is a few m/s at most (far slower than CELL_SIZE_M per
    // relevant timescale), so a cell stays coherent for minutes, not milliseconds. RadialIndex is unaffected by this
    // (radius doesn't change under in-plane rotation), so it is computed exactly as before.

    // Returns the cell containing worldPositionM's projection onto the ring's plane (the plane through
    // planetPositionM with unit normal ringNormal) at secondsSinceJ2000, or an unset result if worldPositionM's
    // in-plane radius from planetPositionM falls outside [ringDef.InnerRadiusM - marginM, ringDef.OuterRadiusM +
    // marginM] (the ring is not present there) or its perpendicular distance from the ring plane exceeds
    // halfThicknessM (generous for gameplay feel, not the ring's real near-zero physical thickness). ringNormal is
    // assumed unit-length; planetGM is the host planet's GM, used only to compute the co-rotating frame (see above).
    SOLTEST_API TOptional<FSOLRingCell> ComputeActiveCell(const FVector3d& worldPositionM,
        const FVector3d& planetPositionM, const FVector3d& ringNormal, const FSOLPlanetRingDef& ringDef,
        double marginM, double halfThicknessM, double planetGM, double secondsSinceJ2000);

    // Deterministically generates one cell's rocks AS THEY ARE AT secondsSinceJ2000: the same ringDef/cell/
    // ringNormal/planetGM/halfThicknessM/gapBands/seed/secondsSinceJ2000 always produces the same rocks (no
    // persistence needed, same discipline as SOLAsteroidBelt) - re-generating the same cell at a LATER
    // secondsSinceJ2000 reproduces the same rocks at their new (advanced) positions, since each rock's Elements are
    // real Keplerian orbital elements, not a one-shot snapshot. Each rock is scattered to a random point within the
    // cell's footprint AT secondsSinceJ2000 (in the ring's co-rotating frame, converted to a world-frame position at
    // that instant; within +/-halfThicknessM of the ring plane), then given a near-circular orbit (eccentricity near
    // 0) around a body of planetGM passing through that point at that instant (via
    // SOLKepler::StateToElements/MeanMotionDegPerCy); the resulting FSOLSecularElements::L0Deg is then rolled back to
    // its equivalent J2000 (T=0) epoch value (L0Deg = wrap(currentMeanLongitudeDeg - LDotDegPerCy *
    // secondsSinceJ2000/SOL::SECONDS_PER_JULIAN_CENTURY)), since FSOLSecularElements is always defined at the J2000
    // epoch regardless of when it was generated - assigning the un-rolled-back "current" longitude as if it were the
    // epoch value would place the rock at a essentially arbitrary point on its orbit once evaluated via AtCenturies
    // at any time other than exactly secondsSinceJ2000. gapBands is planet-center-relative radius bands, same
    // convention as SOLPlanetRing::AllGapBandsM: the returned count is round(ROCKS_PER_CELL * (1 - gapFraction)),
    // where gapFraction is the fraction of the cell's own RADIAL span (its [innerM, outerM) extent at the cell's
    // angular position, from the cell's center) that falls inside any band in gapBands (0 if none of it does, 1 if
    // all of it does); a rock is never placed inside a gap band regardless of gapFraction. Returns an empty array if
    // the computed count is 0.
    SOLTEST_API TArray<FSOLRingRockDef> GenerateCellRocks(const FSOLPlanetRingDef& ringDef, const FSOLRingCell& cell,
        const FVector3d& ringNormal, double planetGM, double halfThicknessM,
        TConstArrayView<FSOLRadiusBandM> gapBands, int32 seed, double secondsSinceJ2000);

    // Smooth cross-fade weight for the near-field (Mass/ISM patch) layer's visibility, 0..1: 1 at/inside the ring's
    // active volume (distanceOutsideRingVolumeM <= 0), 0 at/beyond transitionBandM past it (far-field Niagara only),
    // linearly interpolated in between. transitionBandM <= 0 is a hard cut (no blending).
    SOLTEST_API double ComputeNearFieldAlpha(double distanceOutsideRingVolumeM, double transitionBandM);
}
