/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLPlanetRing.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    // Real Saturn ring features (meters from Saturn's center): the Cassini Division and the two shepherd moons' orbits
    constexpr double RING_CASSINI_LOW_M = 117580.0e3;
    constexpr double RING_CASSINI_HIGH_M = 122170.0e3;
    constexpr double RING_PAN_ORBIT_M = 133584.0e3;
    constexpr double RING_DAPHNIS_ORBIT_M = 136505.0e3;

    // "Approximately cited" tolerance for the Cassini Division edges
    constexpr double RING_CASSINI_TOLERANCE_M = 500.0e3;

    // Arbitrary synthetic ring and half-width for the pure-arithmetic cases
    constexpr double RING_TEST_INNER_M = 1000.0;
    constexpr double RING_TEST_OUTER_M = 2000.0;
    constexpr double RING_TEST_HALF_WIDTH_M = 10.0;

    // Independently-stated real approximate ring radii (meters from the planet's center), for a loose sanity check on
    // RealRings()'s data: same NSSDCA planetary ring fact sheet family SOLPlanetRing.cpp cites, typed independently so
    // a gross data-entry error (e.g. a wrong order of magnitude) in any of the four planets' rows would be caught
    struct FSOLRealRingSanity
    {
        const TCHAR* PlanetName;
        double InnerRadiusM;
        double OuterRadiusM;
    };
    constexpr double RING_SANITY_TOLERANCE_FRACTION = 0.2;
    const FSOLRealRingSanity RING_REAL_SANITY[] =
    {
        { TEXT("Jupiter"), 122500.0e3, 129000.0e3 },
        { TEXT("Saturn"), 66900.0e3, 136780.0e3 },
        { TEXT("Uranus"), 38000.0e3, 51150.0e3 },
        { TEXT("Neptune"), 41900.0e3, 62930.0e3 },
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the ring definition for a planet name from RealRings(), or nullptr if it is missing
    const FSOLPlanetRingDef* RingFind(const TArray<FSOLPlanetRingDef>& rings, const TCHAR* planetName)
    {
        return rings.FindByPredicate([planetName](const FSOLPlanetRingDef& ring)
        {
            return ring.PlanetName == FName(planetName);
        });
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when the list holds a band whose edges match low/high within tolerance
    bool RingHasBand(const TArray<FSOLRadiusBandM>& bands, const double lowM, const double highM, const double tolerance)
    {
        return bands.ContainsByPredicate([lowM, highM, tolerance](const FSOLRadiusBandM& band)
        {
            return FMath::Abs(band.LowM - lowM) <= tolerance && FMath::Abs(band.HighM - highM) <= tolerance;
        });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlanetRingRealRingsTest, "SOLTest.PlanetRing.RealRingsData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RealRings returns the four gas giants with valid, deterministic ranges; Saturn's contains its real gap features
bool FSOLPlanetRingRealRingsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLPlanetRingDef> rings = SOLPlanetRing::RealRings();
    if (!TestEqual(TEXT("Four rings"), rings.Num(), 4))
    {
        return false;
    }
    const TCHAR* planets[] = { TEXT("Jupiter"), TEXT("Saturn"), TEXT("Uranus"), TEXT("Neptune") };
    for (const TCHAR* planet : planets)
    {
        TestNotNull(*FString::Printf(TEXT("%s has a ring"), planet), RingFind(rings, planet));
    }
    for (const FSOLPlanetRingDef& ring : rings)
    {
        TestTrue(*FString::Printf(TEXT("%s inner radius positive"), *ring.PlanetName.ToString()), ring.InnerRadiusM > 0.0);
        TestTrue(*FString::Printf(TEXT("%s inner < outer"), *ring.PlanetName.ToString()),
            ring.InnerRadiusM < ring.OuterRadiusM);
        TestTrue(*FString::Printf(TEXT("%s density positive"), *ring.PlanetName.ToString()), ring.Density > 0.0);
    }

    // Saturn's range spans the Cassini Division and both shepherd moons' orbits
    const FSOLPlanetRingDef* saturn = RingFind(rings, TEXT("Saturn"));
    if (TestNotNull(TEXT("Saturn ring"), saturn))
    {
        TestTrue(TEXT("Saturn contains the Cassini Division"),
            saturn->InnerRadiusM <= RING_CASSINI_LOW_M && RING_CASSINI_HIGH_M < saturn->OuterRadiusM);
        TestTrue(TEXT("Saturn contains Pan's orbit"),
            saturn->InnerRadiusM <= RING_PAN_ORBIT_M && RING_PAN_ORBIT_M < saturn->OuterRadiusM);
        TestTrue(TEXT("Saturn contains Daphnis's orbit"),
            saturn->InnerRadiusM <= RING_DAPHNIS_ORBIT_M && RING_DAPHNIS_ORBIT_M < saturn->OuterRadiusM);
    }

    // Every planet's inner/outer radius is within +/-20% of its independently-stated real value (SDD 6's header
    // documents these as illustrative/approximate, so a tight tolerance would be wrong, but this still catches a
    // gross data-entry error -- e.g. a wrong order of magnitude -- for Jupiter, Uranus or Neptune, not just Saturn
    for (const FSOLRealRingSanity& sanity : RING_REAL_SANITY)
    {
        const FSOLPlanetRingDef* ring = RingFind(rings, sanity.PlanetName);
        if (TestNotNull(*FString::Printf(TEXT("%s ring for sanity check"), sanity.PlanetName), ring))
        {
            TestTrue(*FString::Printf(TEXT("%s inner radius near its real value"), sanity.PlanetName),
                FMath::Abs(ring->InnerRadiusM - sanity.InnerRadiusM) <= sanity.InnerRadiusM * RING_SANITY_TOLERANCE_FRACTION);
            TestTrue(*FString::Printf(TEXT("%s outer radius near its real value"), sanity.PlanetName),
                FMath::Abs(ring->OuterRadiusM - sanity.OuterRadiusM) <= sanity.OuterRadiusM * RING_SANITY_TOLERANCE_FRACTION);
        }
    }

    // Deterministic: a second call returns the same list in the same order
    const TArray<FSOLPlanetRingDef> again = SOLPlanetRing::RealRings();
    if (TestEqual(TEXT("Second call count"), again.Num(), rings.Num()))
    {
        for (int32 index = 0; index < rings.Num(); ++index)
        {
            TestTrue(*FString::Printf(TEXT("Ring %d identical across calls"), index),
                rings[index].PlanetName == again[index].PlanetName && rings[index].InnerRadiusM == again[index].InnerRadiusM
                && rings[index].OuterRadiusM == again[index].OuterRadiusM && rings[index].Color == again[index].Color
                && rings[index].Density == again[index].Density);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlanetRingResonanceGapsTest, "SOLTest.PlanetRing.KnownResonanceGaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Saturn has exactly the Cassini Division; other planets and unknown names have no resonance gaps
bool FSOLPlanetRingResonanceGapsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLRadiusBandM> saturn = SOLPlanetRing::KnownResonanceGapBandsM(FName(TEXT("Saturn")));
    if (TestEqual(TEXT("Saturn has one resonance gap"), saturn.Num(), 1))
    {
        TestEqual(TEXT("Cassini Division low edge"), saturn[0].LowM, RING_CASSINI_LOW_M, RING_CASSINI_TOLERANCE_M);
        TestEqual(TEXT("Cassini Division high edge"), saturn[0].HighM, RING_CASSINI_HIGH_M, RING_CASSINI_TOLERANCE_M);
    }
    const TCHAR* others[] = { TEXT("Jupiter"), TEXT("Uranus"), TEXT("Neptune"), TEXT("Vulcan"), TEXT("") };
    for (const TCHAR* other : others)
    {
        TestEqual(*FString::Printf(TEXT("'%s' has no resonance gaps"), other),
            SOLPlanetRing::KnownResonanceGapBandsM(FName(other)).Num(), 0);
    }
    TestEqual(TEXT("NAME_None has no resonance gaps"), SOLPlanetRing::KnownResonanceGapBandsM(NAME_None).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlanetRingShepherdGapsTest, "SOLTest.PlanetRing.MoonShepherdGaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A moon inside [inner, outer) yields one centered band sized by its own gapHalfWidthsM entry; moons outside yield
// none; a band is not clamped to the ring's own [inner, outer) range
bool FSOLPlanetRingShepherdGapsTest::RunTest(const FString& /*parameters*/)
{
    // One moon inside
    const double inside[] = { 1500.0 };
    const double insideHalfWidths[] = { RING_TEST_HALF_WIDTH_M };
    const TArray<FSOLRadiusBandM> one = SOLPlanetRing::MoonShepherdGapBandsM(RING_TEST_INNER_M, RING_TEST_OUTER_M,
        inside, insideHalfWidths);
    if (TestEqual(TEXT("One moon inside gives one band"), one.Num(), 1))
    {
        TestEqual(TEXT("Band low"), one[0].LowM, 1490.0, 1e-9);
        TestEqual(TEXT("Band high"), one[0].HighM, 1510.0, 1e-9);
        TestEqual(TEXT("Band width is 2*halfWidth"), one[0].HighM - one[0].LowM, 2.0 * RING_TEST_HALF_WIDTH_M, 1e-9);
    }

    // Moons outside the range, including exactly at the exclusive outer edge, give no band; the inner edge is included
    const double outside[] = { 999.0, 2000.0, 5000.0, -10.0 };
    const double outsideHalfWidths[] = { RING_TEST_HALF_WIDTH_M, RING_TEST_HALF_WIDTH_M, RING_TEST_HALF_WIDTH_M,
        RING_TEST_HALF_WIDTH_M };
    TestEqual(TEXT("Moons outside give no bands"), SOLPlanetRing::MoonShepherdGapBandsM(RING_TEST_INNER_M,
        RING_TEST_OUTER_M, outside, outsideHalfWidths).Num(), 0);
    const double atInner[] = { RING_TEST_INNER_M };
    TestEqual(TEXT("Moon at the inner edge gives a band"), SOLPlanetRing::MoonShepherdGapBandsM(RING_TEST_INNER_M,
        RING_TEST_OUTER_M, atInner, insideHalfWidths).Num(), 1);

    // Several moons, mixed inside/outside, with DIFFERENT half-widths for the two inside moons: proves each moon's
    // band is sized from its own gapHalfWidthsM entry, not a single shared value (the whole point of Fix 1)
    const double mixed[] = { 1200.0, 3000.0, 1800.0, 500.0 };
    const double mixedHalfWidths[] = { 30.0, RING_TEST_HALF_WIDTH_M, RING_TEST_HALF_WIDTH_M, RING_TEST_HALF_WIDTH_M };
    const TArray<FSOLRadiusBandM> two = SOLPlanetRing::MoonShepherdGapBandsM(RING_TEST_INNER_M, RING_TEST_OUTER_M,
        mixed, mixedHalfWidths);
    TestEqual(TEXT("Two inside moons give two bands"), two.Num(), 2);
    TestTrue(TEXT("Band around 1200 uses its own 30 half-width"), RingHasBand(two, 1170.0, 1230.0, 1e-9));
    TestTrue(TEXT("Band around 1800 uses its own 10 half-width"), RingHasBand(two, 1790.0, 1810.0, 1e-9));

    // No moons: no bands
    TestEqual(TEXT("No moons give no bands"), SOLPlanetRing::MoonShepherdGapBandsM(RING_TEST_INNER_M,
        RING_TEST_OUTER_M, TConstArrayView<double>(), TConstArrayView<double>()).Num(), 0);

    // Zero half-width: a zero-width band that matches nothing (high is exclusive), without crashing
    const double zeroHalfWidths[] = { 0.0 };
    const TArray<FSOLRadiusBandM> zero = SOLPlanetRing::MoonShepherdGapBandsM(RING_TEST_INNER_M, RING_TEST_OUTER_M,
        inside, zeroHalfWidths);
    if (TestEqual(TEXT("Zero half-width still gives one band"), zero.Num(), 1))
    {
        TestEqual(TEXT("Zero-width band low"), zero[0].LowM, 1500.0, 0.0);
        TestEqual(TEXT("Zero-width band high"), zero[0].HighM, 1500.0, 0.0);
        TestFalse(TEXT("Zero-width band excludes a nearby radius"), SOLPlanetRing::IsInAnyBand(1500.001, zero));
    }

    // Fix 6: a moon orbiting near the ring's outer edge produces a band that is NOT clamped to [inner, outer) -- it
    // extends past OuterRadiusM, exactly as the header documents
    const double nearOuterEdge[] = { RING_TEST_OUTER_M - 5.0 };
    const double nearOuterEdgeHalfWidths[] = { 10.0 };
    const TArray<FSOLRadiusBandM> straddling = SOLPlanetRing::MoonShepherdGapBandsM(RING_TEST_INNER_M,
        RING_TEST_OUTER_M, nearOuterEdge, nearOuterEdgeHalfWidths);
    if (TestEqual(TEXT("Moon near the outer edge still gives one band"), straddling.Num(), 1))
    {
        TestEqual(TEXT("Straddling band low"), straddling[0].LowM, RING_TEST_OUTER_M - 15.0, 1e-9);
        TestEqual(TEXT("Straddling band high"), straddling[0].HighM, RING_TEST_OUTER_M + 5.0, 1e-9);
        TestTrue(TEXT("Straddling band's high edge is not clamped to the ring's own OuterRadiusM"),
            straddling[0].HighM > RING_TEST_OUTER_M);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlanetRingInAnyBandTest, "SOLTest.PlanetRing.IsInAnyBand",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// IsInAnyBand is low-inclusive/high-exclusive, handles overlapping and unsorted bands, and is false for no bands
bool FSOLPlanetRingInAnyBandTest::RunTest(const FString& /*parameters*/)
{
    TestFalse(TEXT("Empty band list is always false"), SOLPlanetRing::IsInAnyBand(100.0, TConstArrayView<FSOLRadiusBandM>()));

    // Unsorted, with the second and third overlapping
    const FSOLRadiusBandM bands[] = { { 500.0, 600.0 }, { 100.0, 200.0 }, { 150.0, 300.0 } };
    TestTrue(TEXT("Low edge is inside"), SOLPlanetRing::IsInAnyBand(100.0, bands));
    TestFalse(TEXT("Just below a low edge is outside"), SOLPlanetRing::IsInAnyBand(99.999, bands));
    TestTrue(TEXT("Inside an overlap"), SOLPlanetRing::IsInAnyBand(175.0, bands));
    TestTrue(TEXT("First band's high edge covered by the overlapping band"), SOLPlanetRing::IsInAnyBand(200.0, bands));
    TestTrue(TEXT("Just below the merged high edge"), SOLPlanetRing::IsInAnyBand(299.999, bands));
    TestFalse(TEXT("Merged high edge is outside"), SOLPlanetRing::IsInAnyBand(300.0, bands));
    TestFalse(TEXT("Between bands is outside"), SOLPlanetRing::IsInAnyBand(400.0, bands));
    TestTrue(TEXT("Inside the last band"), SOLPlanetRing::IsInAnyBand(550.0, bands));
    TestFalse(TEXT("High edge of the last band is outside"), SOLPlanetRing::IsInAnyBand(600.0, bands));
    TestFalse(TEXT("Far outside"), SOLPlanetRing::IsInAnyBand(1.0e9, bands));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLPlanetRingAllGapsTest, "SOLTest.PlanetRing.AllGapBands",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AllGapBandsM combines resonance and shepherd gaps, sorted ascending by LowM, and is empty when there are none
bool FSOLPlanetRingAllGapsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLPlanetRingDef> rings = SOLPlanetRing::RealRings();
    const FSOLPlanetRingDef* saturn = RingFind(rings, TEXT("Saturn"));
    const FSOLPlanetRingDef* jupiter = RingFind(rings, TEXT("Jupiter"));
    if (!TestNotNull(TEXT("Saturn ring"), saturn) || !TestNotNull(TEXT("Jupiter ring"), jupiter))
    {
        return false;
    }

    // Saturn with Pan and Daphnis (Daphnis first, to prove the result gets sorted) plus a far-out Mimas-like radius,
    // each real moon using its own real cited half-width (Fix 1): proves Daphnis's much narrower Keeler Gap is no
    // longer inflated to Pan's Encke Gap width by a single shared default
    const double panHalfWidth = SOLPlanetRing::SATURN_PAN_GAP_HALF_WIDTH_M;
    const double daphnisHalfWidth = SOLPlanetRing::SATURN_DAPHNIS_GAP_HALF_WIDTH_M;
    const double moons[] = { RING_DAPHNIS_ORBIT_M, RING_PAN_ORBIT_M, 186000.0e3 };
    const double halfWidths[] = { daphnisHalfWidth, panHalfWidth, SOLPlanetRing::DEFAULT_GAP_HALF_WIDTH_M };
    const TArray<FSOLRadiusBandM> all = SOLPlanetRing::AllGapBandsM(*saturn, moons, halfWidths);
    TestEqual(TEXT("Saturn: Cassini + Pan + Daphnis"), all.Num(), 3);
    TestTrue(TEXT("Contains the Cassini Division"), RingHasBand(all, RING_CASSINI_LOW_M, RING_CASSINI_HIGH_M,
        RING_CASSINI_TOLERANCE_M));
    TestTrue(TEXT("Contains Pan's ~325 km-wide band"), RingHasBand(all, RING_PAN_ORBIT_M - panHalfWidth,
        RING_PAN_ORBIT_M + panHalfWidth, 1e-6));
    TestTrue(TEXT("Contains Daphnis's ~42 km-wide band"), RingHasBand(all, RING_DAPHNIS_ORBIT_M - daphnisHalfWidth,
        RING_DAPHNIS_ORBIT_M + daphnisHalfWidth, 1e-6));
    TestTrue(TEXT("Daphnis's band is much narrower than Pan's (real gap widths differ by nearly 8x)"),
        (panHalfWidth / daphnisHalfWidth) > 6.0 && (panHalfWidth / daphnisHalfWidth) < 9.0);
    for (int32 index = 1; index < all.Num(); ++index)
    {
        TestTrue(*FString::Printf(TEXT("Sorted ascending by LowM at %d"), index), all[index - 1].LowM <= all[index].LowM);
    }
    TestTrue(TEXT("Pan's orbit is excluded"), SOLPlanetRing::IsInAnyBand(RING_PAN_ORBIT_M, all));
    TestTrue(TEXT("Mid-Cassini is excluded"), SOLPlanetRing::IsInAnyBand(120000.0e3, all));
    TestFalse(TEXT("B ring (100,000 km) is not excluded"), SOLPlanetRing::IsInAnyBand(100000.0e3, all));

    // Jupiter has no resonance gaps; a moon radius outside its ring adds nothing
    const double farMoon[] = { 421800.0e3 };
    const double farMoonHalfWidths[] = { SOLPlanetRing::DEFAULT_GAP_HALF_WIDTH_M };
    TestEqual(TEXT("Jupiter with no in-range moons has no gaps"),
        SOLPlanetRing::AllGapBandsM(*jupiter, farMoon, farMoonHalfWidths).Num(), 0);
    TestEqual(TEXT("Jupiter with no moons has no gaps"),
        SOLPlanetRing::AllGapBandsM(*jupiter, TConstArrayView<double>(), TConstArrayView<double>()).Num(), 0);

    // The default half-width is a sane positive value on the order of real gap widths
    const double defaultHalfWidth = SOLPlanetRing::DEFAULT_GAP_HALF_WIDTH_M;
    TestTrue(TEXT("Default half-width is positive and below 1000 km"), defaultHalfWidth > 0.0 && defaultHalfWidth < 1.0e6);

    // Pan's and Daphnis's real cited half-widths are themselves positive and sane
    TestTrue(TEXT("Pan half-width positive and below 1000 km"), panHalfWidth > 0.0 && panHalfWidth < 1.0e6);
    TestTrue(TEXT("Daphnis half-width positive and below 1000 km"), daphnisHalfWidth > 0.0 && daphnisHalfWidth < 1.0e6);
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
