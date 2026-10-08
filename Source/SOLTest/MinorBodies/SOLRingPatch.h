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
// tiny close in) — see SOLRingPatch.cpp for the exact radius-to-index and index-to-radius math. Neighboring RADIAL
// bands have DIFFERENT angular cell counts (a wider band fits more CELL_SIZE_M-arc-length steps around its own
// circumference), so a caller building an active window around a cell must NOT simply offset AngularIndex by ±1 and
// wrap it at the SAME band's count when moving to a different RadialIndex — that silently misaligns with the
// neighboring band's own cells. Use SOLRingPatch::ActiveCellWindow, which handles this correctly.
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

    // Activation margin (meters) beyond a ring's physical [InnerRadiusM, OuterRadiusM) band within which
    // ComputeActiveCell still considers the player "in" the ring. Shared by USOLRingSubsystem (which pool-populates
    // this margin so there is no pop when entering) and ASOLRingVisuals (which uses it as ComputeNearFieldAlpha's
    // transitionBandM, so the near-field visual fade finishes exactly when the Mass pool itself would stop
    // considering the player inside the ring - a single source of truth so the two can never drift apart).
    SOLTEST_API double ActivationMarginM();

    // Generous gameplay half-thickness (meters) a position is still considered "in" the ring plane within - see
    // ComputeActiveCell/GenerateCellRocks. Shared for the same reason as ActivationMarginM.
    SOLTEST_API double HalfThicknessM();

    // Returns one ring's fixed pool size (the active-cell window's cell count times ROCKS_PER_CELL): a hard
    // invariant USOLRingSubsystem::SpawnRingPools (which allocates the pool) and ASOLRingVisuals (which sizes its
    // ISM instance count/scratch arrays to match it exactly) must agree on, so it is computed once here rather than
    // duplicated at each call site where a mismatch would silently under- or over-size one of the two.
    SOLTEST_API int32 PoolEntityCountPerRing();

    // Returns the diameter (cm) each far-field Niagara sprite should be drawn at for a ring of this outer/inner
    // radius and burst particle count, so sprites overlap enough to read as a continuous band rather than sparse
    // discrete dots, consistently across rings of very different width/circumference despite sharing the same fixed
    // particleCount (SOL::RING_FAR_FIELD_PARTICLE_COUNT is identical for every ringed planet, but Saturn's ring is
    // ~10x wider than Jupiter's). Models particleCount as scattered uniformly at random over the ring's annulus area
    // and scales the resulting typical nearest-neighbor spacing by SOL::RING_FAR_FIELD_SPRITE_OVERLAP_FACTOR.
    // Returns 0.0 for a degenerate ring (outerRadiusM <= innerRadiusM) or particleCount <= 0.
    SOLTEST_API double FarFieldSpriteSizeCm(double outerRadiusM, double innerRadiusM, int32 particleCount);

    // Returns how many angular cells radialIndex's radial band has (CELL_SIZE_M-arc-length steps around its own
    // center-radius circle, at least 1). Exposed so a caller (ActiveCellWindow, or a streaming pool sizing itself)
    // can reason about a specific band's own cell count without duplicating the radius-to-count math.
    SOLTEST_API int32 GetAngularCellCount(const FSOLPlanetRingDef& ringDef, int32 radialIndex);

    // Returns the cells within radialRadius radial steps and angularRadius angular steps of centerCell - e.g.
    // radialRadius=1, angularRadius=1 gives a "3x3" window - correctly re-deriving each neighboring radial band's own
    // angular index from centerCell's angular POSITION, not by naively offsetting AngularIndex, since neighboring
    // bands have different angular cell counts (see FSOLRingCell's comment) AND different co-rotating frame phases
    // (see "THE CO-ROTATING FRAME" above): two bands' AngularIndex 0 do NOT point at the same real-world angle at a
    // given instant, so a fraction-of-the-circle re-derivation alone (comparing a position only within each band's
    // OWN co-rotating frame, with no reference to real-world angle) is wrong - it was the actual shape of a bug this
    // function shipped with before a review caught it, since the position round-trips within either frame alone but
    // the two frames silently disagree with each other once planetGM/secondsSinceJ2000 enter the picture. The
    // correct derivation: convert centerCell's co-rotating angular position to a WORLD angle at secondsSinceJ2000
    // (add back centerCell's own band's RingPatchFramePhaseRad-equivalent phase), then convert that world angle into
    // each neighboring band's own co-rotating frame (subtract THAT band's phase) before deriving its AngularIndex -
    // world angle is the one frame every band agrees on. AngularIndex wraps within each band's own count.
    // Deduplicated: a radial band narrow enough to have fewer angular cells than angularRadius*2+1 does not produce
    // repeated entries for the same cell. The caller is responsible for discarding any returned cell whose radial
    // band is invalid for its ring (e.g. GenerateCellRocks already returns no rocks for a cell whose span starts at
    // or below 0). Because neighboring bands' co-rotating frames drift against each other over time (their mean
    // motions differ), a caller should not assume a window computed once stays valid indefinitely even if the player
    // doesn't move - ReassignPoolSlots is cheap enough to re-run this and re-derive the window periodically.
    // Precondition: centerCell.AngularIndex must already be in [0, GetAngularCellCount(ringDef, centerCell.RadialIndex))
    // - i.e. a valid cell, such as ComputeActiveCell's own output, the function's only intended caller today.
    SOLTEST_API TArray<FSOLRingCell> ActiveCellWindow(const FSOLPlanetRingDef& ringDef, const FSOLRingCell& centerCell,
        int32 radialRadius, int32 angularRadius, double planetGM, double secondsSinceJ2000);

    // Returns, for each pool slot (parallel to currentSlotCells — index i of the result corresponds to index i of
    // currentSlotCells), the cell that slot should represent next, for a streaming pool of fixed-size slots mapped to
    // a variable "currently desired" set of cells (desiredWindow, e.g. ActiveCellWindow's result). Pure index
    // bookkeeping only - it decides WHICH cell each slot ends up representing, not how to regenerate a slot's actual
    // rock data for a newly-assigned cell (that remains the caller's job, e.g. via GenerateCellRocks).
    // - A slot whose current cell (currentSlotCells[i]) is unset, or no longer appears in desiredWindow, is a
    //   candidate for reassignment.
    // - A slot whose current cell IS still in desiredWindow is left unchanged (stability: a cell that stays active
    //   across a player's small movement keeps its already-generated rocks rather than being regenerated for no
    //   reason, avoiding needless flicker/popping).
    // - Reassignment candidates are matched, in order, to desiredWindow's cells that no slot is already covering (so
    //   every cell in desiredWindow ends up covered by exactly one slot, as long as enough slots exist).
    // - If desiredWindow has MORE cells than there are reassignment candidates (not enough spare slots - should not
    //   happen if the pool is sized correctly, but is not this function's job to validate), the excess desiredWindow
    //   cells are left uncovered (no slot represents them); the caller may want to grow its pool if this occurs.
    // - If desiredWindow has FEWER cells than there are reassignment candidates (including the case where
    //   desiredWindow is empty, e.g. the player has left the ring entirely), the extra candidate slots become unset
    //   (parked/idle) rather than being left pointing at a now-irrelevant cell.
    // Precondition: desiredWindow must contain no duplicate cells (ActiveCellWindow's own output already guarantees
    // this). A duplicate would be covered by more than one slot, breaking the "exactly one slot per cell" guarantee
    // above; not defended against at runtime since the function's only intended caller already satisfies it.
    SOLTEST_API TArray<TOptional<FSOLRingCell>> ReassignPoolSlots(
        TConstArrayView<TOptional<FSOLRingCell>> currentSlotCells, TConstArrayView<FSOLRingCell> desiredWindow);
}
