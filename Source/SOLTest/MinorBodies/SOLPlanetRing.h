/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// One real gas-giant ring's structural parameters (SDD 6 Appendix C)
struct SOLTEST_API FSOLPlanetRingDef
{
    FName PlanetName;                           // host planet's registry name, e.g. "Saturn"
    double InnerRadiusM = 0.0;                  // ring inner edge, planet-center-relative, meters
    double OuterRadiusM = 0.0;                  // ring outer edge, planet-center-relative, meters
    FLinearColor Color = FLinearColor::White;   // placeholder visual tint (SDD 6 decision 8; real art is deferred)
    double Density = 1.0;                       // relative particle density scalar, placeholder for later visuals/5e
};

// A planet-center-relative radius band (meters), low inclusive / high exclusive, excluded from ring particle placement
struct SOLTEST_API FSOLRadiusBandM
{
    double LowM = 0.0;
    double HighM = 0.0;
};

// Pure-logic ring structure for the four gas giants (SDD 6 Section 3.3, decisions 6 and 8): real ring radius ranges
// plus the gap bands ring particles are kept out of. Gaps are hybrid: fixed, cited resonance gaps that no registered
// moon orbits inside (Saturn's Cassini Division), plus a generic band around every shepherd moon orbiting within the
// ring (Pan's Encke Gap, Daphnis's Keeler Gap). Deliberately decoupled from the body registry: callers pass each
// moon's semi-major axis in (computed once, e.g. Elements.A0AU * SOL::AU_M — not a live per-frame position; these
// are circular-enough orbits that the semi-major axis is an adequate stand-in and recomputing a true live distance
// every frame would be wasted work), so this module stays pure and trivially testable.
namespace SOLPlanetRing
{
    // A reasonable illustrative gap half-width (meters) for a MoonShepherdGapBandsM/AllGapBandsM caller that does not
    // have a specific shepherd moon's real gap width to hand; not fitted to any particular real gap
    SOLTEST_API extern const double DEFAULT_GAP_HALF_WIDTH_M;

    // Saturn's real, cited shepherd-moon gap half-widths (meters): use these for Pan and Daphnis instead of
    // DEFAULT_GAP_HALF_WIDTH_M, since their real gap widths are known and differ by nearly 8x (see SOLPlanetRing.cpp)
    SOLTEST_API extern const double SATURN_PAN_GAP_HALF_WIDTH_M;
    SOLTEST_API extern const double SATURN_DAPHNIS_GAP_HALF_WIDTH_M;

    // Returns the four gas giants' real ring parameters (Jupiter, Saturn, Uranus, Neptune): fixed data, no randomness,
    // same list and order every call
    SOLTEST_API TArray<FSOLPlanetRingDef> RealRings();

    // Returns the fixed, cited real resonance gap bands of a planet's ring that are not derived from any registered
    // moon's orbit: Saturn's Cassini Division (2:1 resonance with Mimas, whose own orbit lies far outside the rings);
    // an empty array for any other planet name, known or not
    SOLTEST_API TArray<FSOLRadiusBandM> KnownResonanceGapBandsM(FName planetName);

    // Returns a band around each moon orbit radius in moonOrbitRadiiM that falls within [ringInnerRadiusM,
    // ringOuterRadiusM), using the matching entry of gapHalfWidthsM (same length as moonOrbitRadiiM; a mismatch is a
    // programmer error, not a runtime condition to handle) as that moon's own half-width; in input order, and a moon
    // orbit outside that range produces no band. A returned band is not clamped to [ringInnerRadiusM,
    // ringOuterRadiusM) and may extend past the ring's own edge.
    SOLTEST_API TArray<FSOLRadiusBandM> MoonShepherdGapBandsM(double ringInnerRadiusM, double ringOuterRadiusM,
        TConstArrayView<double> moonOrbitRadiiM, TConstArrayView<double> gapHalfWidthsM);

    // Returns true if radiusM falls inside any band (LowM inclusive, HighM exclusive); bands may be unsorted or overlap
    SOLTEST_API bool IsInAnyBand(double radiusM, TConstArrayView<FSOLRadiusBandM> bands);

    // Returns every gap band for one ring: its known resonance gaps plus its moon-shepherd gaps (gapHalfWidthsM
    // parallel to moonOrbitRadiiM, see MoonShepherdGapBandsM), sorted ascending by LowM (overlapping bands are not
    // merged; IsInAnyBand handles overlap correctly regardless)
    SOLTEST_API TArray<FSOLRadiusBandM> AllGapBandsM(const FSOLPlanetRingDef& ringDef,
        TConstArrayView<double> moonOrbitRadiiM, TConstArrayView<double> gapHalfWidthsM);
}
