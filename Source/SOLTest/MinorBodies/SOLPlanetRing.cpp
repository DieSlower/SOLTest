/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLPlanetRing.h"

#include "SOLConstants.h"

namespace
{
    // One ring row in kilometers (converted to meters on output): planet name, inner and outer edge from the planet's
    // center, placeholder color and relative density
    struct FSOLPlanetRingRow
    {
        const TCHAR* PlanetName;
        double InnerRadiusKm;
        double OuterRadiusKm;
        FLinearColor Color;
        double Density;
    };

    // Real ring radius ranges (issue #6 Part 5d / SDD 6 Appendix C), from the NASA NSSDCA planetary ring fact sheets
    // (nssdc.gsfc.nasa.gov/planetary/factsheet/{jupiter,saturn,uranus,neptune}ringfact.html) and the Cassini/Voyager
    // ring literature they summarize; all are approximate edge radii, rounded to the commonly quoted values:
    //  - Jupiter: main ring ~122,500-129,000 km (the halo and gossamer rings are not modeled).
    //  - Saturn: D ring inner edge ~66,900 km to A ring outer edge ~136,780 km (the F, G and E rings are not modeled).
    //  - Uranus: the zeta ring (1986U2R), ~38,000 km per NSSDCA, to the epsilon ring ~51,150 km (the narrow ring 6
    //    itself is at ~41,840 km; the outer mu/nu rings are not modeled). The zeta ring's exact inner extent is not
    //    sharply defined, unlike the main rings' edges: it is diffuse, ~2,500 km wide, with faint material observed
    //    to extend even further in per later (post-Voyager) observations, so 38,000 km is its characteristic/center
    //    distance as NSSDCA lists it, not a literal sharp inner edge.
    //  - Neptune: Galle ring ~41,900 km to Adams ring ~62,930 km.
    // Color and Density are placeholders for later visual work (decision 8), not measured values: Saturn's bright
    // water-ice tan at full density; Jupiter's faint reddish dust, Uranus's and Neptune's dark, faint rings at low
    // density so they read as the faint rings they really are.
    const FSOLPlanetRingRow RING_REAL_RINGS[] =
    {
        { SOL::BodyNames::JUPITER, 122500.0, 129000.0, FLinearColor(0.55f, 0.45f, 0.38f), 0.05 },
        { SOL::BodyNames::SATURN, 66900.0, 136780.0, FLinearColor(0.85f, 0.78f, 0.65f), 1.0 },
        { SOL::BodyNames::URANUS, 38000.0, 51150.0, FLinearColor(0.30f, 0.30f, 0.32f), 0.1 },
        { SOL::BodyNames::NEPTUNE, 41900.0, 62930.0, FLinearColor(0.30f, 0.31f, 0.35f), 0.08 },
    };

    // Saturn's Cassini Division, ~117,580-122,170 km from Saturn's center (NSSDCA Saturnian rings fact sheet): a 2:1
    // mean-motion resonance with Mimas (orbit ~186,000 km, outside the rings), so no moon-orbit lookup can produce it
    constexpr double RING_CASSINI_LOW_KM = 117580.0;
    constexpr double RING_CASSINI_HIGH_KM = 122170.0;

    // Saturn's shepherd moons' real gap half-widths (issue #6 Part 5d), from the NSSDCA Saturnian rings fact sheet and
    // the Cassini imaging results it summarizes (Porco, C.C., et al. 2005, "Cassini Imaging Science: Initial Results
    // on Saturn's Rings and Small Satellites", Science 307, 1226-1236): Pan's Encke Gap is ~325 km wide (half-width
    // ~162 km); Daphnis's Keeler Gap is only ~42 km wide (half-width ~21 km) -- nearly an 8x difference, so a single
    // uniform half-width is a poor fit for both.
    constexpr double RING_PAN_GAP_HALF_WIDTH_KM = 162.0;
    constexpr double RING_DAPHNIS_GAP_HALF_WIDTH_KM = 21.0;
}

namespace SOLPlanetRing
{
    // 150 km: a reasonable illustrative width when a specific shepherd moon's real gap width isn't known; not fitted
    // to any particular real gap. Real gap widths vary a lot by shepherd mass (e.g. Saturn's Keeler Gap is ~42 km wide
    // and its Encke Gap ~325 km wide, nearly an 8x difference), so a caller that has real per-moon data -- such as
    // Saturn's SATURN_PAN_GAP_HALF_WIDTH_M / SATURN_DAPHNIS_GAP_HALF_WIDTH_M below -- should use it instead.
    const double DEFAULT_GAP_HALF_WIDTH_M = 150.0 * SOL::METERS_PER_KM;

    // See the RING_PAN_GAP_HALF_WIDTH_KM / RING_DAPHNIS_GAP_HALF_WIDTH_KM citation above
    const double SATURN_PAN_GAP_HALF_WIDTH_M = RING_PAN_GAP_HALF_WIDTH_KM * SOL::METERS_PER_KM;
    const double SATURN_DAPHNIS_GAP_HALF_WIDTH_M = RING_DAPHNIS_GAP_HALF_WIDTH_KM * SOL::METERS_PER_KM;

    //////////////////////////////////////////////////////////////////////////
    // Converts the ring table to meters, in table order
    TArray<FSOLPlanetRingDef> RealRings()
    {
        TArray<FSOLPlanetRingDef> rings;
        rings.Reserve(UE_ARRAY_COUNT(RING_REAL_RINGS));
        for (const FSOLPlanetRingRow& row : RING_REAL_RINGS)
        {
            FSOLPlanetRingDef& ring = rings.AddDefaulted_GetRef();
            ring.PlanetName = FName(row.PlanetName);
            ring.InnerRadiusM = row.InnerRadiusKm * SOL::METERS_PER_KM;
            ring.OuterRadiusM = row.OuterRadiusKm * SOL::METERS_PER_KM;
            ring.Color = row.Color;
            ring.Density = row.Density;
        }
        return rings;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Cassini Division for Saturn and nothing for any other name
    TArray<FSOLRadiusBandM> KnownResonanceGapBandsM(const FName planetName)
    {
        TArray<FSOLRadiusBandM> bands;
        if (planetName == FName(SOL::BodyNames::SATURN))
        {
            bands.Add({ RING_CASSINI_LOW_KM * SOL::METERS_PER_KM, RING_CASSINI_HIGH_KM * SOL::METERS_PER_KM });
        }
        return bands;
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds a centered band for each moon orbiting within the ring, sized by that moon's own gapHalfWidthsM entry: real
    // gap widths scale with the shepherd's mass (Encke ~325 km, Keeler ~42 km), so a per-moon width is required rather
    // than one value for every moon. A negative half-width is treated as 0 so a band never has LowM > HighM. The two
    // array lengths must match: a mismatch is a caller bug, not a runtime condition, hence the checkf rather than a
    // silent truncation
    TArray<FSOLRadiusBandM> MoonShepherdGapBandsM(const double ringInnerRadiusM, const double ringOuterRadiusM,
        const TConstArrayView<double> moonOrbitRadiiM, const TConstArrayView<double> gapHalfWidthsM)
    {
        checkf(moonOrbitRadiiM.Num() == gapHalfWidthsM.Num(),
            TEXT("MoonShepherdGapBandsM: moonOrbitRadiiM (%d) and gapHalfWidthsM (%d) must have the same length"),
            moonOrbitRadiiM.Num(), gapHalfWidthsM.Num());

        TArray<FSOLRadiusBandM> bands;
        for (int32 index = 0; index < moonOrbitRadiiM.Num(); ++index)
        {
            const double orbitRadiusM = moonOrbitRadiiM[index];
            if (orbitRadiusM >= ringInnerRadiusM && orbitRadiusM < ringOuterRadiusM)
            {
                const double halfWidthM = FMath::Max(gapHalfWidthsM[index], 0.0);
                bands.Add({ orbitRadiusM - halfWidthM, orbitRadiusM + halfWidthM });
            }
        }
        return bands;
    }

    //////////////////////////////////////////////////////////////////////////
    // Linear scan; each band is tested independently, so order and overlap do not matter
    bool IsInAnyBand(const double radiusM, const TConstArrayView<FSOLRadiusBandM> bands)
    {
        for (const FSOLRadiusBandM& band : bands)
        {
            if (radiusM >= band.LowM && radiusM < band.HighM)
            {
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Concatenates the resonance and shepherd gaps, then stable-sorts by LowM so equal lows keep a deterministic order
    TArray<FSOLRadiusBandM> AllGapBandsM(const FSOLPlanetRingDef& ringDef, const TConstArrayView<double> moonOrbitRadiiM,
        const TConstArrayView<double> gapHalfWidthsM)
    {
        TArray<FSOLRadiusBandM> bands = KnownResonanceGapBandsM(ringDef.PlanetName);
        bands.Append(MoonShepherdGapBandsM(ringDef.InnerRadiusM, ringDef.OuterRadiusM, moonOrbitRadiiM, gapHalfWidthsM));
        bands.StableSort([](const FSOLRadiusBandM& a, const FSOLRadiusBandM& b)
        {
            return a.LowM < b.LowM;
        });
        return bands;
    }
}
