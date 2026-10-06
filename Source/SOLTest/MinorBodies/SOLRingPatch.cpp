/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingPatch.h"

#include "SOLConstants.h"

#include "Containers/HashTable.h"
#include "Math/RandomStream.h"

// Helpers here carry names unique across the module so unity builds do not collide
namespace
{
    // Smallest and largest procedural ring-rock radius (m): real ring particles run from centimeters to ~10 m, and a
    // 1-10 m spread keeps rocks visible-but-flyable against the ~100 m average spacing of SOLRingPatch::ROCKS_PER_CELL
    constexpr double RINGPATCH_ROCK_MIN_RADIUS_M = 1.0;
    constexpr double RINGPATCH_ROCK_MAX_RADIUS_M = 10.0;

    // Distance (m) rocks are kept inside their cell's edges and thickness, so the float error of the
    // state -> elements -> state round trip can never push one across a cell boundary
    constexpr double RINGPATCH_EDGE_INSET_M = 0.1;

    // Salt mixed into each cell's stream seed so ring-patch streams never coincide with another system's seed
    constexpr uint32 RINGPATCH_STREAM_SALT = 0x52494E47u;

    // A radius band [LowM, HighM] used while building a cell's gap/free intervals
    using FRingPatchInterval = TPair<double, double>;
    using FRingPatchIntervalArray = TArray<FRingPatchInterval, TInlineAllocator<8>>;

    // The orthonormal in-plane frame of a ring: Axis1/Axis2 span the plane, Normal is the ring normal
    struct FRingPatchBasis
    {
        FVector3d Axis1;
        FVector3d Axis2;
        FVector3d Normal;
    };

    //////////////////////////////////////////////////////////////////////////
    // Builds the ring's in-plane basis deterministically from its unit normal alone
    FRingPatchBasis RingPatchMakeBasis(const FVector3d& ringNormal)
    {
        FRingPatchBasis basis;
        basis.Normal = ringNormal;
        ringNormal.FindBestAxisVectors(basis.Axis1, basis.Axis2);
        basis.Axis1.Normalize();
        basis.Axis2 = FVector3d::CrossProduct(ringNormal, basis.Axis1).GetSafeNormal();
        return basis;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the inner radius (m) of a radial cell index (indices are anchored at the ring's own InnerRadiusM)
    double RingPatchRadialLowM(const FSOLPlanetRingDef& ringDef, const int32 radialIndex)
    {
        return ringDef.InnerRadiusM + static_cast<double>(radialIndex) * SOLRingPatch::CELL_SIZE_M;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a radial band's reference radius (m): the center of its [low, low + CELL_SIZE_M) span
    double RingPatchReferenceRadiusM(const FSOLPlanetRingDef& ringDef, const int32 radialIndex)
    {
        return RingPatchRadialLowM(ringDef, radialIndex) + 0.5 * SOLRingPatch::CELL_SIZE_M;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many angular cells a radial band has: CELL_SIZE_M arc steps around its center-radius circle
    int32 RingPatchAngularCellCount(const FSOLPlanetRingDef& ringDef, const int32 radialIndex)
    {
        const double referenceRadiusM = RingPatchReferenceRadiusM(ringDef, radialIndex);
        const double count = FMath::FloorToDouble(UE_DOUBLE_TWO_PI * referenceRadiusM / SOLRingPatch::CELL_SIZE_M);
        return count >= 1.0 ? static_cast<int32>(FMath::Min(count, static_cast<double>(MAX_int32))) : 1;
    }

    //////////////////////////////////////////////////////////////////////////
    // Wraps an angle in radians to [0, 2*PI)
    double RingPatchWrapTwoPi(const double angleRad)
    {
        double wrapped = FMath::Fmod(angleRad, UE_DOUBLE_TWO_PI);
        if (wrapped < 0.0)
        {
            wrapped += UE_DOUBLE_TWO_PI;
        }
        return wrapped >= UE_DOUBLE_TWO_PI ? 0.0 : wrapped;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the co-rotating frame's rotation (rad, wrapped to [0, 2*PI)) at secondsSinceJ2000 for a radial band:
    // its circular mean motion sqrt(planetGM / referenceRadiusM^3) times the elapsed time. Shared by
    // ComputeActiveCell and GenerateCellRocks so both use exactly the same frame (0 for a non-positive GM or radius)
    double RingPatchFramePhaseRad(const FSOLPlanetRingDef& ringDef, const int32 radialIndex, const double planetGM,
        const double secondsSinceJ2000)
    {
        const double referenceRadiusM = RingPatchReferenceRadiusM(ringDef, radialIndex);
        if (!(planetGM > 0.0 && referenceRadiusM > 0.0))
        {
            return 0.0;
        }
        const double meanMotionRadPerS = FMath::Sqrt(planetGM / (referenceRadiusM * referenceRadiusM
            * referenceRadiusM));
        return RingPatchWrapTwoPi(meanMotionRadPerS * secondsSinceJ2000);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a uniform double in [low, high) from the stream
    double RingPatchUniform(const FRandomStream& stream, const double low, const double high)
    {
        return low + (high - low) * static_cast<double>(stream.GetFraction());
    }

    //////////////////////////////////////////////////////////////////////////
    // Converts a planet-relative state at centuriesSinceJ2000 to J2000-epoch secular elements: rate terms 0 except
    // LDotDegPerCy (the orbit's mean motion), with L0Deg rolled back from that instant to the J2000 epoch
    FSOLSecularElements RingPatchStateToElements(const FSOLOrbitState& state, const double planetGM,
        const double centuriesSinceJ2000)
    {
        const FSOLKeplerElements kepler = SOLKepler::StateToElements(state, planetGM);
        FSOLSecularElements elements = SOLKepler::KeplerElementsToSecular(kepler);
        elements.LDotDegPerCy = SOLKepler::MeanMotionDegPerCy(kepler.SemiMajorAxisM, planetGM);

        // Wrap the (possibly huge) forward advance before subtracting, so L0Deg keeps its full precision
        const double advanceDeg = SOLKepler::WrapDegrees(elements.LDotDegPerCy * centuriesSinceJ2000);
        elements.L0Deg = SOLKepler::WrapDegrees(elements.L0Deg - advanceDeg);
        return elements;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the merged (sorted, non-overlapping) parts of gapBands that fall inside [lowM, highM]
    FRingPatchIntervalArray RingPatchMergedGaps(TConstArrayView<FSOLRadiusBandM> gapBands, const double lowM,
        const double highM)
    {
        // Clip every band to the span, dropping the ones that miss it
        FRingPatchIntervalArray clipped;
        for (const FSOLRadiusBandM& band : gapBands)
        {
            const double clipLowM = FMath::Max(band.LowM, lowM);
            const double clipHighM = FMath::Min(band.HighM, highM);
            if (clipHighM > clipLowM)
            {
                clipped.Emplace(clipLowM, clipHighM);
            }
        }
        clipped.Sort([](const FRingPatchInterval& a, const FRingPatchInterval& b) { return a.Key < b.Key; });

        // Merge overlapping or touching bands so overlaps count once
        FRingPatchIntervalArray merged;
        for (const FRingPatchInterval& interval : clipped)
        {
            if (merged.Num() > 0 && interval.Key <= merged.Last().Value)
            {
                merged.Last().Value = FMath::Max(merged.Last().Value, interval.Value);
            }
            else
            {
                merged.Add(interval);
            }
        }
        return merged;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the parts of [lowM, highM] not covered by the sorted, merged gaps
    FRingPatchIntervalArray RingPatchFreeIntervals(const FRingPatchIntervalArray& mergedGaps, const double lowM,
        const double highM)
    {
        FRingPatchIntervalArray freeBands;
        double cursorM = lowM;
        for (const FRingPatchInterval& gap : mergedGaps)
        {
            if (gap.Key > cursorM)
            {
                freeBands.Emplace(cursorM, FMath::Min(gap.Key, highM));
            }
            cursorM = FMath::Max(cursorM, gap.Value);
        }
        if (highM > cursorM)
        {
            freeBands.Emplace(cursorM, highM);
        }
        freeBands.RemoveAll([](const FRingPatchInterval& interval) { return interval.Value <= interval.Key; });
        return freeBands;
    }

    //////////////////////////////////////////////////////////////////////////
    // Maps a uniform draw in [0, totalLengthM) onto a radius inside the free intervals
    double RingPatchPickFreeRadiusM(const FRingPatchIntervalArray& freeBands, const double totalLengthM,
        const FRandomStream& stream)
    {
        double remainingM = RingPatchUniform(stream, 0.0, totalLengthM);
        for (const FRingPatchInterval& interval : freeBands)
        {
            const double lengthM = interval.Value - interval.Key;
            if (remainingM < lengthM)
            {
                return interval.Key + remainingM;
            }
            remainingM -= lengthM;
        }
        return freeBands.Last().Value;
    }
}

namespace SOLRingPatch
{
    const double CELL_SIZE_M = 1000.0;
    const int32 ROCKS_PER_CELL = 100;

    //////////////////////////////////////////////////////////////////////////
    // Returns the ring cell (co-rotating angular index) containing worldPositionM at secondsSinceJ2000, or unset when
    // it is outside the ring's radial band or thickness
    TOptional<FSOLRingCell> ComputeActiveCell(const FVector3d& worldPositionM, const FVector3d& planetPositionM,
        const FVector3d& ringNormal, const FSOLPlanetRingDef& ringDef, const double marginM,
        const double halfThicknessM, const double planetGM, const double secondsSinceJ2000)
    {
        // Split the planet-relative offset into plane distance and in-plane coordinates
        const FRingPatchBasis basis = RingPatchMakeBasis(ringNormal);
        const FVector3d relativeM = worldPositionM - planetPositionM;
        const double planeDistanceM = FVector3d::DotProduct(relativeM, basis.Normal);
        const double x = FVector3d::DotProduct(relativeM, basis.Axis1);
        const double y = FVector3d::DotProduct(relativeM, basis.Axis2);
        const double radiusM = FMath::Sqrt(x * x + y * y);

        // Admit only when every check positively passes, so a NaN anywhere is rejected rather than admitted
        const bool bWithinThickness = FMath::Abs(planeDistanceM) <= halfThicknessM;
        const bool bWithinBand = radiusM >= ringDef.InnerRadiusM - marginM && radiusM <= ringDef.OuterRadiusM + marginM;
        if (!(bWithinThickness && bWithinBand))
        {
            return TOptional<FSOLRingCell>();
        }

        // Radial index from InnerRadiusM itself; angular index in the band's co-rotating frame, from its cell count
        FSOLRingCell cell;
        cell.RadialIndex = FMath::FloorToInt32((radiusM - ringDef.InnerRadiusM) / CELL_SIZE_M);
        const int32 angularCount = RingPatchAngularCellCount(ringDef, cell.RadialIndex);
        const double worldAngleRad = FMath::Atan2(y, x);
        const double coRotatingAngleRad = RingPatchWrapTwoPi(worldAngleRad
            - RingPatchFramePhaseRad(ringDef, cell.RadialIndex, planetGM, secondsSinceJ2000));
        const int32 angularIndex = FMath::FloorToInt32(coRotatingAngleRad / (UE_DOUBLE_TWO_PI / angularCount));
        cell.AngularIndex = FMath::Clamp(angularIndex, 0, angularCount - 1);
        return cell;
    }

    //////////////////////////////////////////////////////////////////////////
    // Deterministically scatters one cell's rocks, as they are at secondsSinceJ2000, on near-circular planet-relative
    // orbits (J2000-epoch elements), skipping gap bands
    TArray<FSOLRingRockDef> GenerateCellRocks(const FSOLPlanetRingDef& ringDef, const FSOLRingCell& cell,
        const FVector3d& ringNormal, const double planetGM, const double halfThicknessM,
        TConstArrayView<FSOLRadiusBandM> gapBands, const int32 seed, const double secondsSinceJ2000)
    {
        TArray<FSOLRingRockDef> rocks;

        // Rock count from the fraction of the cell's radial span covered by gaps (overlaps counted once)
        const double spanLowM = RingPatchRadialLowM(ringDef, cell.RadialIndex);
        const double spanHighM = spanLowM + CELL_SIZE_M;
        const FRingPatchIntervalArray gaps = RingPatchMergedGaps(gapBands, spanLowM, spanHighM);
        double gapLengthM = 0.0;
        for (const FRingPatchInterval& gap : gaps)
        {
            gapLengthM += gap.Value - gap.Key;
        }
        const double gapFraction = FMath::Clamp(gapLengthM / CELL_SIZE_M, 0.0, 1.0);
        const int32 rockCount = FMath::RoundToInt32(ROCKS_PER_CELL * (1.0 - gapFraction));

        // Rocks may only land in the gap-free part of the span, kept a hair inside the cell's edges
        const FRingPatchIntervalArray freeBands = RingPatchFreeIntervals(RingPatchMergedGaps(gapBands,
            spanLowM + RINGPATCH_EDGE_INSET_M, spanHighM - RINGPATCH_EDGE_INSET_M),
            spanLowM + RINGPATCH_EDGE_INSET_M, spanHighM - RINGPATCH_EDGE_INSET_M);
        double freeLengthM = 0.0;
        for (const FRingPatchInterval& interval : freeBands)
        {
            freeLengthM += interval.Value - interval.Key;
        }
        if (rockCount <= 0 || freeLengthM <= 0.0 || spanLowM <= 0.0 || planetGM <= 0.0)
        {
            return rocks;
        }

        // The cell's co-rotating angular span on its band, inset like the radial span (inset angle taken at the inner
        // edge), and the frame's rotation at secondsSinceJ2000 that maps it to world angles
        const int32 angularCount = RingPatchAngularCellCount(ringDef, cell.RadialIndex);
        const double cellAngleRad = UE_DOUBLE_TWO_PI / angularCount;
        const double angleInsetRad = FMath::Min(RINGPATCH_EDGE_INSET_M / spanLowM, 0.25 * cellAngleRad);
        const double angleLowRad = cell.AngularIndex * cellAngleRad + angleInsetRad;
        const double angleHighRad = (cell.AngularIndex + 1) * cellAngleRad - angleInsetRad;
        const double heightLimitM = FMath::Max(halfThicknessM - RINGPATCH_EDGE_INSET_M, 0.0);
        const double framePhaseRad = RingPatchFramePhaseRad(ringDef, cell.RadialIndex, planetGM, secondsSinceJ2000);
        const double centuriesSinceJ2000 = secondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;

        // One stream per (seed, cell), so any cell regenerates identically on its own; the combined hash is
        // finalized so adjacent cells' seeds differ in every bit, not just the low ones the LCG mixes poorly
        const uint32 cellHash = MurmurFinalize32(HashCombineFast(HashCombineFast(HashCombineFast(GetTypeHash(seed),
            RINGPATCH_STREAM_SALT), GetTypeHash(cell.RadialIndex)), GetTypeHash(cell.AngularIndex)));
        const FRandomStream stream(static_cast<int32>(cellHash));
        const FRingPatchBasis basis = RingPatchMakeBasis(ringNormal);

        rocks.Reserve(rockCount);
        for (int32 rock = 0; rock < rockCount; ++rock)
        {
            // Random point in the cell's co-rotating footprint and thickness, rotated to its world angle right now
            const double radiusM = RingPatchPickFreeRadiusM(freeBands, freeLengthM, stream);
            const double coRotatingAngleRad = RingPatchUniform(stream, angleLowRad, angleHighRad);
            const double heightM = RingPatchUniform(stream, -heightLimitM, heightLimitM);
            const double worldAngleRad = coRotatingAngleRad + framePhaseRad;
            double sinAngle = 0.0;
            double cosAngle = 0.0;
            FMath::SinCos(&sinAngle, &cosAngle, worldAngleRad);
            const FVector3d radialDir = basis.Axis1 * cosAngle + basis.Axis2 * sinAngle;

            // Prograde circular velocity (from the rock's own distance) in the ring plane, perpendicular to the radius
            FSOLOrbitState state;
            state.PositionM = radialDir * radiusM + basis.Normal * heightM;
            const double speedMps = FMath::Sqrt(planetGM / state.PositionM.Size());
            state.VelocityMps = FVector3d::CrossProduct(basis.Normal, radialDir) * speedMps;

            FSOLRingRockDef& def = rocks.AddDefaulted_GetRef();
            def.Elements = RingPatchStateToElements(state, planetGM, centuriesSinceJ2000);
            def.RadiusM = RingPatchUniform(stream, RINGPATCH_ROCK_MIN_RADIUS_M, RINGPATCH_ROCK_MAX_RADIUS_M);
        }
        return rocks;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the near-field layer's cross-fade weight: 1 inside the ring volume, 0 past the transition band
    double ComputeNearFieldAlpha(const double distanceOutsideRingVolumeM, const double transitionBandM)
    {
        if (distanceOutsideRingVolumeM <= 0.0)
        {
            return 1.0;
        }
        if (transitionBandM <= 0.0)
        {
            return 0.0;
        }
        return FMath::Clamp(1.0 - distanceOutsideRingVolumeM / transitionBandM, 0.0, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many angular cells radialIndex's radial band has (exposes the file-local helper of the same shape)
    int32 GetAngularCellCount(const FSOLPlanetRingDef& ringDef, const int32 radialIndex)
    {
        return RingPatchAngularCellCount(ringDef, radialIndex);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the cells within radialRadius/angularRadius steps of centerCell, re-aligning each band via world angle
    TArray<FSOLRingCell> ActiveCellWindow(const FSOLPlanetRingDef& ringDef, const FSOLRingCell& centerCell,
        const int32 radialRadius, const int32 angularRadius, const double planetGM, const double secondsSinceJ2000)
    {
        const int32 safeRadialRadius = FMath::Max(radialRadius, 0);
        const int32 safeAngularRadius = FMath::Max(angularRadius, 0);
        TArray<FSOLRingCell> result;
        result.Reserve((safeRadialRadius * 2 + 1) * (safeAngularRadius * 2 + 1));

        // Center cell's co-rotating angle (cell center, hence +0.5) converted to the world angle every band agrees on
        const int32 centerCount = RingPatchAngularCellCount(ringDef, centerCell.RadialIndex);
        const double centerCoRotatingRad = (static_cast<double>(centerCell.AngularIndex) + 0.5)
            * (UE_DOUBLE_TWO_PI / static_cast<double>(centerCount));
        const double worldAngleRad = RingPatchWrapTwoPi(centerCoRotatingRad
            + RingPatchFramePhaseRad(ringDef, centerCell.RadialIndex, planetGM, secondsSinceJ2000));

        for (int32 radialOffset = -safeRadialRadius; radialOffset <= safeRadialRadius; ++radialOffset)
        {
            // World angle mapped into this band's own co-rotating frame, then to its angular index
            const int32 radialIndex = centerCell.RadialIndex + radialOffset;
            const int32 bandCount = RingPatchAngularCellCount(ringDef, radialIndex);
            const double bandCoRotatingRad = RingPatchWrapTwoPi(worldAngleRad
                - RingPatchFramePhaseRad(ringDef, radialIndex, planetGM, secondsSinceJ2000));
            const int32 aligned = FMath::Clamp(
                FMath::FloorToInt32(bandCoRotatingRad / (UE_DOUBLE_TWO_PI / static_cast<double>(bandCount))), 0,
                bandCount - 1);

            // Narrow bands (fewer cells than the angular span) would revisit cells, so dedup only in that case
            const bool bMayRepeat = bandCount <= safeAngularRadius * 2 + 1;
            for (int32 angularOffset = -safeAngularRadius; angularOffset <= safeAngularRadius; ++angularOffset)
            {
                FSOLRingCell cell;
                cell.RadialIndex = radialIndex;
                // Double modulo keeps the result in [0, bandCount) even when aligned + angularOffset is negative
                cell.AngularIndex = ((aligned + angularOffset) % bandCount + bandCount) % bandCount;
                if (bMayRepeat)
                {
                    result.AddUnique(cell);
                }
                else
                {
                    result.Add(cell);
                }
            }
        }
        return result;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns each pool slot's next cell: slots still on a desired cell keep it, the rest fill uncovered cells in order
    TArray<TOptional<FSOLRingCell>> ReassignPoolSlots(TConstArrayView<TOptional<FSOLRingCell>> currentSlotCells,
        TConstArrayView<FSOLRingCell> desiredWindow)
    {
        TArray<TOptional<FSOLRingCell>> result;
        result.Init(TOptional<FSOLRingCell>(), currentSlotCells.Num());
        TArray<bool> claimed;
        claimed.Init(false, desiredWindow.Num());

        // Pass 1: a slot already on a desired cell no earlier slot has claimed stays put and claims it
        for (int32 slot = 0; slot < currentSlotCells.Num(); ++slot)
        {
            if (!currentSlotCells[slot].IsSet())
            {
                continue;
            }
            const int32 desiredIndex = desiredWindow.IndexOfByKey(currentSlotCells[slot].GetValue());
            if (desiredIndex != INDEX_NONE && !claimed[desiredIndex])
            {
                claimed[desiredIndex] = true;
                result[slot] = currentSlotCells[slot];
            }
        }

        // Pass 2: every other slot is a candidate, matched in order to the unclaimed desired cells (extras stay unset)
        int32 nextUnclaimed = 0;
        for (int32 slot = 0; slot < currentSlotCells.Num(); ++slot)
        {
            if (result[slot].IsSet())
            {
                continue;
            }
            while (nextUnclaimed < desiredWindow.Num() && claimed[nextUnclaimed])
            {
                ++nextUnclaimed;
            }
            if (nextUnclaimed >= desiredWindow.Num())
            {
                break;
            }
            claimed[nextUnclaimed] = true;
            result[slot] = desiredWindow[nextUnclaimed];
            ++nextUnclaimed;
        }
        return result;
    }
}
