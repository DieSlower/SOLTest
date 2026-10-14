/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingShadowMath.h"

#include "MinorBodies/SOLPlanetRing.h"
#include "SOLConstants.h"
#include "Tests/SOLTestHelpers.h"

#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Written against the "16a contract" in Docs/Plans/16-ring-shadows-plan.md, not against the implementation.
// Helpers here carry names unique across the test files so unity builds do not collide.
namespace
{
    using FRingShadowBand = SOLRingShadow::FTauBand;

    // Reference values typed independently of production code
    constexpr double RINGSHADOW_SUN_RADIUS_M = 6.957e8;
    constexpr double RINGSHADOW_SATURN_DISTANCE_AU = 9.5826;
    constexpr double RINGSHADOW_NEPTUNE_DISTANCE_AU = 30.07;
    constexpr double RINGSHADOW_SATURN_RADIUS_M = 5.8232e7;

    // Saturn-like synthetic profile (m): B ring, Cassini Division, A ring
    constexpr double RINGSHADOW_B_INNER_M = 92.0e6;
    constexpr double RINGSHADOW_B_OUTER_M = 117.58e6;   // also the Cassini Division's inner edge
    constexpr double RINGSHADOW_CD_OUTER_M = 122.17e6;  // also the A ring's inner edge
    constexpr double RINGSHADOW_A_OUTER_M = 136.78e6;
    constexpr double RINGSHADOW_B_TAU = 2.0;
    constexpr double RINGSHADOW_CD_TAU = 0.1;
    constexpr double RINGSHADOW_A_TAU = 0.5;

    // Tolerances
    constexpr double RINGSHADOW_TIGHT_TOL = 1.0e-12;
    constexpr double RINGSHADOW_FILTER_TOL = 1.0e-9;
    constexpr double RINGSHADOW_PENUMBRA_TOL = 1.0e-6;
    constexpr double RINGSHADOW_ERFC_TOL = 1.0e-4;      // contract accuracy of LayerFractionTowardSun
    constexpr double RINGSHADOW_LAYER_T_TOL = 1.0e-3;   // transmittance error that ERFC_TOL can cause at tau <= 2

    //////////////////////////////////////////////////////////////////////////
    // Returns Saturn's Sun angular radius (rad), computed independently of production code
    double RingShadowSaturnAlpha()
    {
        return std::asin(RINGSHADOW_SUN_RADIUS_M / (RINGSHADOW_SATURN_DISTANCE_AU * SOLTestHelpers::AU_M));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the synthetic Saturn-like profile: B [92, 117.58) tau 2, Cassini [117.58, 122.17) tau 0.1, A tau 0.5
    TArray<FRingShadowBand> RingShadowSaturnLike()
    {
        TArray<FRingShadowBand> bands;
        bands.Add({ RINGSHADOW_B_INNER_M, RINGSHADOW_B_OUTER_M, RINGSHADOW_B_TAU });
        bands.Add({ RINGSHADOW_B_OUTER_M, RINGSHADOW_CD_OUTER_M, RINGSHADOW_CD_TAU });
        bands.Add({ RINGSHADOW_CD_OUTER_M, RINGSHADOW_A_OUTER_M, RINGSHADOW_A_TAU });
        return bands;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a small-number profile: [0, 10) tau 1, [10, 20) tau 2, [20, 30) tau 3, then a gap, then [40, 50) tau 1
    TArray<FRingShadowBand> RingShadowSmallProfile()
    {
        TArray<FRingShadowBand> bands;
        bands.Add({ 0.0, 10.0, 1.0 });
        bands.Add({ 10.0, 20.0, 2.0 });
        bands.Add({ 20.0, 30.0, 3.0 });
        bands.Add({ 40.0, 50.0, 1.0 });
        return bands;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the receiver that sees ringHit at distance distanceM along the (unit) Sun direction
    FVector3d RingShadowReceiverFor(const FVector3d& ringHit, const FVector3d& towardSunUnit, const double distanceM)
    {
        return ringHit - towardSunUnit * distanceM;
    }

    //////////////////////////////////////////////////////////////////////////
    // Rotates a vector about +Z by angleRad (independent of production code)
    FVector3d RingShadowRotateZ(const FVector3d& v, const double angleRad)
    {
        const double c = std::cos(angleRad);
        const double s = std::sin(angleRad);
        return FVector3d(v.X * c - v.Y * s, v.X * s + v.Y * c, v.Z);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the reference lens (overlap) area of two discs of radii a and b with centres d apart (any configuration)
    double RingShadowLensArea(const double a, const double b, const double d)
    {
        if (d >= a + b)
        {
            return 0.0;
        }
        if (d <= std::fabs(a - b))
        {
            const double smaller = FMath::Min(a, b);
            return UE_DOUBLE_PI * smaller * smaller;
        }
        const double cosA = FMath::Clamp((d * d + a * a - b * b) / (2.0 * d * a), -1.0, 1.0);
        const double cosB = FMath::Clamp((d * d + b * b - a * a) / (2.0 * d * b), -1.0, 1.0);
        const double k = (-d + a + b) * (d + a - b) * (d - a + b) * (d + a + b);
        return a * a * std::acos(cosA) + b * b * std::acos(cosB) - 0.5 * std::sqrt(FMath::Max(k, 0.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the maximum tau of a profile sampled every stepM over [lowM, highM]
    double RingShadowMaxTau(TConstArrayView<FRingShadowBand> profile, const double lowM, const double highM,
        const double stepM)
    {
        double maxTau = 0.0;
        for (double r = lowM; r <= highM; r += stepM)
        {
            maxTau = FMath::Max(maxTau, SOLRingShadow::OpticalDepthAt(r, profile));
        }
        return maxTau;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the minimum tau of a profile sampled every stepM over [lowM, highM]
    double RingShadowMinTau(TConstArrayView<FRingShadowBand> profile, const double lowM, const double highM,
        const double stepM)
    {
        double minTau = std::numeric_limits<double>::infinity();
        for (double r = lowM; r <= highM; r += stepM)
        {
            minTau = FMath::Min(minTau, SOLRingShadow::OpticalDepthAt(r, profile));
        }
        return minTau;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the largest tau anywhere in a profile
    double RingShadowProfileMaxTau(TConstArrayView<FRingShadowBand> profile)
    {
        double maxTau = 0.0;
        for (const FRingShadowBand& band : profile)
        {
            maxTau = FMath::Max(maxTau, band.Tau);
        }
        return maxTau;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if a value is finite and in [0, 1]
    bool RingShadowInUnit(const double value)
    {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    }

    // Saturn's equatorial radius (m), the body radius BodySunTransmittance is driven with
    constexpr double RINGSHADOW_SATURN_EQ_RADIUS_M = 6.0268e7;

    //////////////////////////////////////////////////////////////////////////
    // Returns v divided by its length (independent of production code; v must be non-zero and finite)
    FVector3d RingShadowUnit(const FVector3d& v)
    {
        const double length = std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
        return FVector3d(v.X / length, v.Y / length, v.Z / length);
    }

    //////////////////////////////////////////////////////////////////////////
    // Expresses v in a right-handed ring frame whose z axis is normalize(ringNormal) (x, y any in-plane pair)
    FVector3d RingShadowToRingFrame(const FVector3d& v, const FVector3d& ringNormal)
    {
        const FVector3d n = RingShadowUnit(ringNormal);
        const FVector3d helper = std::fabs(n.Z) < 0.9 ? FVector3d(0.0, 0.0, 1.0) : FVector3d(1.0, 0.0, 0.0);
        const FVector3d e1 = RingShadowUnit(FVector3d::CrossProduct(helper, n));
        const FVector3d e2 = FVector3d::CrossProduct(n, e1);
        return FVector3d(FVector3d::DotProduct(v, e1), FVector3d::DotProduct(v, e2), FVector3d::DotProduct(v, n));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the reference one-body transmittance: the planet term times the ring term in the ring frame
    double RingShadowBodyReference(const FVector3d& point, const FVector3d& towardSun, const double radiusM,
        const double alpha, const FVector3d& ringNormal, const double sigma, TConstArrayView<FRingShadowBand> profile)
    {
        const double planet = SOLRingShadow::PlanetShadowTransmittance(point, towardSun, radiusM, alpha);
        const double ring = SOLRingShadow::RingShadowTransmittance(RingShadowToRingFrame(point, ringNormal),
            RingShadowToRingFrame(towardSun, ringNormal), alpha, sigma, profile);
        return planet * ring;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns |a - b| / max(|b|, tiny)
    double RingShadowRelDiff(const double a, const double b)
    {
        return std::fabs(a - b) / FMath::Max(std::fabs(b), 1.0e-300);
    }
}

//====================================================================================================================
// Constants
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowConstantsTest, "SOLTest.RingShadowMath.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shadow constants exist with sane values and the layer thickness matches the dense rock layer
bool FSOLRingShadowConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestTrue(TEXT("SUN_RADIUS_M is the IAU nominal radius (0.1%)"),
        SOLTestHelpers::RelativeError(SOL::SUN_RADIUS_M, RINGSHADOW_SUN_RADIUS_M) < 1.0e-3);
    TestTrue(TEXT("MIN_SUN_ELEVATION_SIN in (0, 0.01]"), SOL::RING_SHADOW_MIN_SUN_ELEVATION_SIN > 0.0 &&
        SOL::RING_SHADOW_MIN_SUN_ELEVATION_SIN <= 0.01);
    TestTrue(TEXT("MAX_SUN_ANGULAR_RADIUS_RAD in (0.01, PI/2)"), SOL::RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD > 0.01 &&
        SOL::RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD < 0.5 * UE_DOUBLE_PI);
    TestTrue(TEXT("LAYER_HALF_THICKNESS_M is positive"), SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M > 0.0);
    TestNearlyEqual(TEXT("LAYER_HALF_THICKNESS_M is the dense layer's half thickness in metres"),
        SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M, SOL::RING_DENSE_HALF_THICKNESS_CM / 100.0, RINGSHADOW_TIGHT_TOL);
    TestEqual(TEXT("PROFILE_COUNT is 4"), SOL::RING_SHADOW_PROFILE_COUNT, 4);
    return true;
}

//====================================================================================================================
// Sun angular radius and penumbra width
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowSunAngularRadiusTest, "SOLTest.RingShadowMath.SunAngularRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SunAngularRadiusRad is asin(R_sun / d) at planetary distances, strictly decreasing with distance
bool FSOLRingShadowSunAngularRadiusTest::RunTest(const FString& /*parameters*/)
{
    const double oneAu = SOLTestHelpers::AU_M;
    TestTrue(TEXT("1 AU -> asin(R_sun / AU) (1e-3 relative)"), SOLTestHelpers::RelativeError(
        SOLRingShadow::SunAngularRadiusRad(oneAu), std::asin(RINGSHADOW_SUN_RADIUS_M / oneAu)) < 1.0e-3);
    TestTrue(TEXT("Saturn -> ~4.85e-4 rad (1e-3 relative)"), SOLTestHelpers::RelativeError(
        SOLRingShadow::SunAngularRadiusRad(RINGSHADOW_SATURN_DISTANCE_AU * oneAu), RingShadowSaturnAlpha()) < 1.0e-3);
    TestNearlyEqual(TEXT("Twice the Sun's radius -> PI/6 exactly"), SOLRingShadow::SunAngularRadiusRad(
        2.0 * SOL::SUN_RADIUS_M), UE_DOUBLE_PI / 6.0, RINGSHADOW_TIGHT_TOL);

    // Strictly decreasing from just outside the Sun out to beyond Neptune
    double previous = std::numeric_limits<double>::infinity();
    bool bOk = true;
    for (int32 i = 0; i <= 60; ++i)
    {
        const double d = SOL::SUN_RADIUS_M * 1.001 * std::pow(10.0, i * 0.1);
        const double alpha = SOLRingShadow::SunAngularRadiusRad(d);
        if (!std::isfinite(alpha) || alpha <= 0.0 || alpha >= previous)
        {
            AddError(FString::Printf(TEXT("alpha(%g) = %.17g is not strictly decreasing (previous %.17g)"), d, alpha,
                previous));
            bOk = false;
            break;
        }
        previous = alpha;
    }
    TestTrue(TEXT("Strictly decreasing with distance"), bOk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowSunAngularRadiusEdgeTest,
    "SOLTest.RingShadowMath.SunAngularRadiusEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At or inside the Sun's radius the angular radius is PI/2; NaN and +inf distances give 0
bool FSOLRingShadowSunAngularRadiusEdgeTest::RunTest(const FString& /*parameters*/)
{
    const double halfPi = 0.5 * UE_DOUBLE_PI;
    TestNearlyEqual(TEXT("d = R_sun -> PI/2"), SOLRingShadow::SunAngularRadiusRad(SOL::SUN_RADIUS_M), halfPi,
        RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("d = R_sun / 2 -> PI/2"), SOLRingShadow::SunAngularRadiusRad(0.5 * SOL::SUN_RADIUS_M),
        halfPi, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("d = 0 -> PI/2"), SOLRingShadow::SunAngularRadiusRad(0.0), halfPi, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("d = -1 -> PI/2"), SOLRingShadow::SunAngularRadiusRad(-1.0), halfPi, RINGSHADOW_TIGHT_TOL);
    TestEqual(TEXT("NaN -> 0"), SOLRingShadow::SunAngularRadiusRad(std::numeric_limits<double>::quiet_NaN()), 0.0);
    TestEqual(TEXT("+inf -> 0"), SOLRingShadow::SunAngularRadiusRad(std::numeric_limits<double>::infinity()), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowPenumbraHalfWidthTest, "SOLTest.RingShadowMath.PenumbraHalfWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// PenumbraHalfWidthM is L tan(alpha): linear in L, increasing in alpha, 0 for degenerate input, clamped alpha
bool FSOLRingShadowPenumbraHalfWidthTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double alpha = RingShadowSaturnAlpha();

    TestNearlyEqual(TEXT("L = 1e8 at Saturn -> L tan(alpha)"), SOLRingShadow::PenumbraHalfWidthM(1.0e8, alpha),
        1.0e8 * std::tan(alpha), 1.0e-6);
    // (alpha = atan(0.5) ~ 0.464 stays below the contract's 0.5 rad clamp; PI/4 would be clamped)
    TestNearlyEqual(TEXT("L = 1, alpha = atan(0.5) -> 0.5"), SOLRingShadow::PenumbraHalfWidthM(1.0, std::atan(0.5)),
        0.5, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("Linear in L"), SOLRingShadow::PenumbraHalfWidthM(2.0e7, alpha),
        2.0 * SOLRingShadow::PenumbraHalfWidthM(1.0e7, alpha), 1.0e-6);
    TestTrue(TEXT("Increasing in alpha"), SOLRingShadow::PenumbraHalfWidthM(1.0e7, 2.0 * alpha) >
        SOLRingShadow::PenumbraHalfWidthM(1.0e7, alpha));

    // Degenerate inputs give 0
    TestEqual(TEXT("L = 0 -> 0"), SOLRingShadow::PenumbraHalfWidthM(0.0, alpha), 0.0);
    TestEqual(TEXT("L < 0 -> 0"), SOLRingShadow::PenumbraHalfWidthM(-5.0, alpha), 0.0);
    TestEqual(TEXT("alpha = 0 -> 0"), SOLRingShadow::PenumbraHalfWidthM(1.0e7, 0.0), 0.0);
    TestEqual(TEXT("alpha < 0 -> 0"), SOLRingShadow::PenumbraHalfWidthM(1.0e7, -alpha), 0.0);
    TestEqual(TEXT("NaN L -> 0"), SOLRingShadow::PenumbraHalfWidthM(nan, alpha), 0.0);
    TestEqual(TEXT("inf L -> 0"), SOLRingShadow::PenumbraHalfWidthM(inf, alpha), 0.0);
    TestEqual(TEXT("NaN alpha -> 0"), SOLRingShadow::PenumbraHalfWidthM(1.0e7, nan), 0.0);
    TestEqual(TEXT("inf alpha -> 0"), SOLRingShadow::PenumbraHalfWidthM(1.0e7, inf), 0.0);

    // Alpha at or past PI/2 is clamped so the result stays finite
    const double clamped = 1.0e3 * std::tan(SOL::RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD);
    TestNearlyEqual(TEXT("alpha = PI/2 clamps"), SOLRingShadow::PenumbraHalfWidthM(1.0e3, 0.5 * UE_DOUBLE_PI),
        clamped, 1.0e-9);
    TestNearlyEqual(TEXT("alpha = 10 clamps"), SOLRingShadow::PenumbraHalfWidthM(1.0e3, 10.0), clamped, 1.0e-9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowFootprintTest, "SOLTest.RingShadowMath.FootprintRadialHalfWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The radial half-extent of the Sun's footprint is w sqrt(1 + c^2 (1/mu^2 - 1)), with mu and c clamped
bool FSOLRingShadowFootprintTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double w = 1000.0;
    const double minMu = SOL::RING_SHADOW_MIN_SUN_ELEVATION_SIN;

    // Examples
    TestNearlyEqual(TEXT("mu = 1, c = 1 -> w"), SOLRingShadow::FootprintRadialHalfWidthM(w, 1.0, 1.0), w, 1.0e-9);
    TestNearlyEqual(TEXT("mu = 1, c = 0.3 -> w"), SOLRingShadow::FootprintRadialHalfWidthM(w, 1.0, 0.3), w, 1.0e-9);
    TestNearlyEqual(TEXT("c = 0, mu = 0.3 -> w (azimuthal)"), SOLRingShadow::FootprintRadialHalfWidthM(w, 0.3, 0.0),
        w, 1.0e-9);
    TestNearlyEqual(TEXT("c = 1, mu = 0.5 -> 2w (radial)"), SOLRingShadow::FootprintRadialHalfWidthM(w, 0.5, 1.0),
        2.0 * w, 1.0e-9);
    TestNearlyEqual(TEXT("c = -1, mu = 0.5 -> 2w (even in c)"), SOLRingShadow::FootprintRadialHalfWidthM(w, 0.5,
        -1.0), 2.0 * w, 1.0e-9);
    TestNearlyEqual(TEXT("c = 0.6, mu = 0.5 -> w sqrt(2.08)"), SOLRingShadow::FootprintRadialHalfWidthM(w, 0.5, 0.6),
        w * std::sqrt(2.08), 1.0e-9);
    TestNearlyEqual(TEXT("Negative mu uses |mu|"), SOLRingShadow::FootprintRadialHalfWidthM(w, -0.5, 1.0), 2.0 * w,
        1.0e-9);

    // Clamping
    TestNearlyEqual(TEXT("Tiny mu clamps to MIN_SUN_ELEVATION_SIN"), SOLRingShadow::FootprintRadialHalfWidthM(w,
        1.0e-9, 1.0), w / minMu, 1.0e-6 * w / minMu);
    TestNearlyEqual(TEXT("mu = 0 clamps to MIN_SUN_ELEVATION_SIN"), SOLRingShadow::FootprintRadialHalfWidthM(w, 0.0,
        1.0), w / minMu, 1.0e-6 * w / minMu);
    TestNearlyEqual(TEXT("mu = 2 clamps to 1"), SOLRingShadow::FootprintRadialHalfWidthM(w, 2.0, 1.0), w, 1.0e-9);
    TestNearlyEqual(TEXT("c = 2 clamps to 1"), SOLRingShadow::FootprintRadialHalfWidthM(w, 0.5, 2.0), 2.0 * w, 1.0e-9);

    // Monotonic in |c| (non-decreasing) and in mu (non-increasing)
    bool bOk = true;
    double previous = 0.0;
    for (int32 i = 0; i <= 20; ++i)
    {
        const double value = SOLRingShadow::FootprintRadialHalfWidthM(w, 0.2, i / 20.0);
        bOk &= value >= previous - 1.0e-9;
        previous = value;
    }
    TestTrue(TEXT("Non-decreasing in |c|"), bOk);
    bOk = true;
    previous = std::numeric_limits<double>::infinity();
    for (int32 i = 1; i <= 20; ++i)
    {
        const double value = SOLRingShadow::FootprintRadialHalfWidthM(w, i / 20.0, 0.7);
        bOk &= value <= previous + 1.0e-9;
        previous = value;
    }
    TestTrue(TEXT("Non-increasing in mu"), bOk);

    // Invalid inputs
    TestEqual(TEXT("w = 0 -> 0"), SOLRingShadow::FootprintRadialHalfWidthM(0.0, 0.5, 1.0), 0.0);
    TestEqual(TEXT("w < 0 -> 0"), SOLRingShadow::FootprintRadialHalfWidthM(-1.0, 0.5, 1.0), 0.0);
    TestEqual(TEXT("NaN w -> 0"), SOLRingShadow::FootprintRadialHalfWidthM(nan, 0.5, 1.0), 0.0);
    TestNearlyEqual(TEXT("NaN mu -> no stretch"), SOLRingShadow::FootprintRadialHalfWidthM(w, nan, 1.0), w, 1.0e-9);
    TestNearlyEqual(TEXT("NaN c -> no stretch"), SOLRingShadow::FootprintRadialHalfWidthM(w, 0.5, nan), w, 1.0e-9);
    return true;
}

//====================================================================================================================
// Optical depth lookup and filtered transmittance
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowOpticalDepthTest, "SOLTest.RingShadowMath.OpticalDepthAt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// OpticalDepthAt returns the containing band's tau (inner inclusive, outer exclusive) and 0 elsewhere
bool FSOLRingShadowOpticalDepthTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();

    TestEqual(TEXT("B ring middle"), SOLRingShadow::OpticalDepthAt(105.0e6, saturn), RINGSHADOW_B_TAU);
    TestEqual(TEXT("Cassini middle"), SOLRingShadow::OpticalDepthAt(120.0e6, saturn), RINGSHADOW_CD_TAU);
    TestEqual(TEXT("A ring middle"), SOLRingShadow::OpticalDepthAt(130.0e6, saturn), RINGSHADOW_A_TAU);
    TestEqual(TEXT("Inner edge is inclusive"), SOLRingShadow::OpticalDepthAt(RINGSHADOW_B_INNER_M, saturn),
        RINGSHADOW_B_TAU);
    TestEqual(TEXT("Outer edge is exclusive"), SOLRingShadow::OpticalDepthAt(RINGSHADOW_A_OUTER_M, saturn), 0.0);
    TestEqual(TEXT("Shared edge belongs to the outer band"), SOLRingShadow::OpticalDepthAt(RINGSHADOW_B_OUTER_M,
        saturn), RINGSHADOW_CD_TAU);
    TestEqual(TEXT("Inner hole -> 0"), SOLRingShadow::OpticalDepthAt(80.0e6, saturn), 0.0);
    TestEqual(TEXT("Beyond the rings -> 0"), SOLRingShadow::OpticalDepthAt(140.0e6, saturn), 0.0);
    TestEqual(TEXT("r = 0 -> 0"), SOLRingShadow::OpticalDepthAt(0.0, saturn), 0.0);
    TestEqual(TEXT("Negative r -> 0"), SOLRingShadow::OpticalDepthAt(-105.0e6, saturn), 0.0);
    TestEqual(TEXT("NaN r -> 0"), SOLRingShadow::OpticalDepthAt(nan, saturn), 0.0);
    TestEqual(TEXT("inf r -> 0"), SOLRingShadow::OpticalDepthAt(inf, saturn), 0.0);

    // A gap between non-adjacent bands has tau 0
    const TArray<FRingShadowBand> small = RingShadowSmallProfile();
    TestEqual(TEXT("Gap [30, 40) -> 0"), SOLRingShadow::OpticalDepthAt(35.0, small), 0.0);
    TestEqual(TEXT("Band after the gap"), SOLRingShadow::OpticalDepthAt(45.0, small), 1.0);

    // Empty profile and invalid band taus
    TestEqual(TEXT("Empty profile -> 0"), SOLRingShadow::OpticalDepthAt(105.0e6, TConstArrayView<FRingShadowBand>()),
        0.0);
    TArray<FRingShadowBand> bad;
    bad.Add({ 0.0, 10.0, -3.0 });
    bad.Add({ 10.0, 20.0, nan });
    bad.Add({ 20.0, 30.0, inf });
    TestEqual(TEXT("Negative band tau -> 0"), SOLRingShadow::OpticalDepthAt(5.0, bad), 0.0);
    TestEqual(TEXT("NaN band tau -> 0"), SOLRingShadow::OpticalDepthAt(15.0, bad), 0.0);
    TestEqual(TEXT("inf band tau -> 0"), SOLRingShadow::OpticalDepthAt(25.0, bad), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowFilteredExamplesTest, "SOLTest.RingShadowMath.FilteredTransmittance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FilteredTransmittance is the exact box mean of exp(-tau k) over [c - h, c + h]
bool FSOLRingShadowFilteredExamplesTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const TArray<FRingShadowBand> small = RingShadowSmallProfile();
    const double eB = std::exp(-RINGSHADOW_B_TAU);
    const double eCD = std::exp(-RINGSHADOW_CD_TAU);

    // Point samples and windows inside one band
    TestNearlyEqual(TEXT("h = 0 in B -> exp(-2)"), SOLRingShadow::FilteredTransmittance(105.0e6, 0.0, 1.0, saturn),
        eB, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("h = 0, slant 3 -> exp(-6)"), SOLRingShadow::FilteredTransmittance(105.0e6, 0.0, 3.0,
        saturn), std::exp(-6.0), RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("Window inside B -> exp(-2)"), SOLRingShadow::FilteredTransmittance(105.0e6, 1.0e6, 1.0,
        saturn), eB, RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Window in the hole -> 1"), SOLRingShadow::FilteredTransmittance(50.0e6, 1.0e6, 1.0, saturn),
        1.0, RINGSHADOW_FILTER_TOL);

    // Windows across one edge
    const double edge = RINGSHADOW_B_OUTER_M;
    const double h = 5.0e4;
    TestNearlyEqual(TEXT("Centred on the B/Cassini edge -> mean"), SOLRingShadow::FilteredTransmittance(edge, h, 1.0,
        saturn), 0.5 * (eB + eCD), RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("A quarter of the window in Cassini"), SOLRingShadow::FilteredTransmittance(edge - 0.5 * h, h,
        1.0, saturn), 0.75 * eB + 0.25 * eCD, RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Window touching the edge from inside -> exp(-2)"), SOLRingShadow::FilteredTransmittance(
        edge - h, h, 1.0, saturn), eB, RINGSHADOW_FILTER_TOL);

    // Windows across several bands, gaps and r < 0
    TestNearlyEqual(TEXT("[0, 30] covers three bands"), SOLRingShadow::FilteredTransmittance(15.0, 15.0, 1.0, small),
        (std::exp(-1.0) + std::exp(-2.0) + std::exp(-3.0)) / 3.0, RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("[25, 45] covers band, gap, band"), SOLRingShadow::FilteredTransmittance(35.0, 10.0, 1.0,
        small), (5.0 * std::exp(-3.0) + 10.0 + 5.0 * std::exp(-1.0)) / 20.0, RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("[-5, 5]: below zero has no band"), SOLRingShadow::FilteredTransmittance(0.0, 5.0, 1.0, small),
        0.5 + 0.5 * std::exp(-1.0), RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Slant 2 over [0, 20]"), SOLRingShadow::FilteredTransmittance(10.0, 10.0, 2.0, small),
        0.5 * (std::exp(-2.0) + std::exp(-4.0)), RINGSHADOW_FILTER_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowFilteredInvalidTest,
    "SOLTest.RingShadowMath.FilteredTransmittanceEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Degenerate slant, window or centre values follow the contract (lit by default, point sample for a bad window)
bool FSOLRingShadowFilteredInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const TArray<FRingShadowBand> small = RingShadowSmallProfile();
    const double eB = std::exp(-RINGSHADOW_B_TAU);

    TestEqual(TEXT("Slant 0 -> 1"), SOLRingShadow::FilteredTransmittance(105.0e6, 1.0e4, 0.0, saturn), 1.0);
    TestEqual(TEXT("Slant < 0 -> 1"), SOLRingShadow::FilteredTransmittance(105.0e6, 1.0e4, -2.0, saturn), 1.0);
    TestEqual(TEXT("NaN slant -> 1"), SOLRingShadow::FilteredTransmittance(105.0e6, 1.0e4, nan, saturn), 1.0);
    TestEqual(TEXT("NaN centre -> 1"), SOLRingShadow::FilteredTransmittance(nan, 1.0e4, 1.0, saturn), 1.0);
    TestEqual(TEXT("inf centre -> 1"), SOLRingShadow::FilteredTransmittance(inf, 1.0e4, 1.0, saturn), 1.0);
    TestEqual(TEXT("Empty profile -> 1"), SOLRingShadow::FilteredTransmittance(105.0e6, 1.0e4, 1.0,
        TConstArrayView<FRingShadowBand>()), 1.0);

    // A bad window falls back to a point sample
    TestNearlyEqual(TEXT("NaN h -> point sample"), SOLRingShadow::FilteredTransmittance(105.0e6, nan, 1.0, saturn), eB,
        RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("h < 0 -> point sample"), SOLRingShadow::FilteredTransmittance(105.0e6, -1.0e4, 1.0, saturn),
        eB, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("inf h -> point sample"), SOLRingShadow::FilteredTransmittance(105.0e6, inf, 1.0, saturn), eB,
        RINGSHADOW_TIGHT_TOL);

    // Infinite slant: only the tau-0 part of the window transmits
    TestNearlyEqual(TEXT("inf slant, half the window in a gap -> 0.5"), SOLRingShadow::FilteredTransmittance(30.0, 5.0,
        inf, small), 0.5, RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("inf slant, point in a band -> 0"), SOLRingShadow::FilteredTransmittance(15.0, 0.0, inf,
        small), 0.0, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("inf slant, point in the gap -> 1"), SOLRingShadow::FilteredTransmittance(35.0, 0.0, inf,
        small), 1.0, RINGSHADOW_TIGHT_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowFilteredMonotonicTest,
    "SOLTest.RingShadowMath.FilteredTransmittanceMonotonicAndContinuous",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Sliding the window from an opaque band into a thinner one never darkens it, the result moves by at most
// step / (2h) per step (continuous penumbra), and more slant never brightens it
bool FSOLRingShadowFilteredMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double halfWidths[] = { 1.0e3, 1.0e5, 1.0e6 };
    for (const double h : halfWidths)
    {
        // B (tau 2) -> Cassini (tau 0.1): windows stay within [109e6, 121e6], inside B and Cassini only
        const double step = 1.0e4;
        double previous = -1.0;
        bool bMonotonic = true;
        bool bContinuous = true;
        bool bInUnit = true;
        for (double c = 110.0e6; c <= 120.0e6; c += step)
        {
            const double value = SOLRingShadow::FilteredTransmittance(c, h, 1.0, saturn);
            bInUnit &= RingShadowInUnit(value);
            if (previous >= 0.0)
            {
                bMonotonic &= value >= previous - RINGSHADOW_FILTER_TOL;
                bContinuous &= std::fabs(value - previous) <= step / (2.0 * h) + RINGSHADOW_FILTER_TOL;
            }
            previous = value;
        }
        TestTrue(FString::Printf(TEXT("h = %g: B -> Cassini is non-decreasing"), h), bMonotonic);
        TestTrue(FString::Printf(TEXT("h = %g: B -> Cassini is continuous"), h), bContinuous);
        TestTrue(FString::Printf(TEXT("h = %g: B -> Cassini stays in [0, 1]"), h), bInUnit);

        // Cassini (tau 0.1) -> A (tau 0.5): windows stay within [118e6, 127e6], inside Cassini and A only
        previous = 2.0;
        bMonotonic = true;
        for (double c = 119.0e6; c <= 126.0e6; c += step)
        {
            const double value = SOLRingShadow::FilteredTransmittance(c, h, 1.0, saturn);
            bMonotonic &= value <= previous + RINGSHADOW_FILTER_TOL;
            previous = value;
        }
        TestTrue(FString::Printf(TEXT("h = %g: Cassini -> A is non-increasing"), h), bMonotonic);
    }

    // Non-increasing in slant for a window straddling an edge
    double previous = 2.0;
    bool bSlant = true;
    for (int32 i = 0; i <= 40; ++i)
    {
        const double value = SOLRingShadow::FilteredTransmittance(RINGSHADOW_B_OUTER_M, 5.0e4, 0.25 * i, saturn);
        bSlant &= RingShadowInUnit(value) && value <= previous + RINGSHADOW_FILTER_TOL;
        previous = value;
    }
    TestTrue(TEXT("Non-increasing in slant"), bSlant);
    return true;
}

//====================================================================================================================
// Layer fraction (self-shadowing inside a layer of finite thickness)
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowLayerFractionTest, "SOLTest.RingShadowMath.LayerFractionTowardSun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// LayerFractionTowardSun is 0.5 erfc(z / (sigma sqrt 2)) to 1e-4, symmetric, non-increasing, with sane limits
bool FSOLRingShadowLayerFractionTest::RunTest(const FString& /*parameters*/)
{
    const double sigma = 20.0;
    const double sqrt2 = std::sqrt(2.0);

    TestNearlyEqual(TEXT("z = 0 -> 0.5"), SOLRingShadow::LayerFractionTowardSun(0.0, sigma), 0.5, RINGSHADOW_ERFC_TOL);
    const double zs[] = { -60.0, -20.0, -7.5, 3.0, 20.0, 40.0, 75.0 };
    for (const double z : zs)
    {
        TestNearlyEqual(FString::Printf(TEXT("z = %g matches 0.5 erfc"), z), SOLRingShadow::LayerFractionTowardSun(z,
            sigma), 0.5 * std::erfc(z / (sigma * sqrt2)), RINGSHADOW_ERFC_TOL);
        TestNearlyEqual(FString::Printf(TEXT("F(%g) + F(-%g) = 1"), z, z), SOLRingShadow::LayerFractionTowardSun(z,
            sigma) + SOLRingShadow::LayerFractionTowardSun(-z, sigma), 1.0, 2.0 * RINGSHADOW_ERFC_TOL);
    }
    TestNearlyEqual(TEXT("z = sigma -> 0.158655"), SOLRingShadow::LayerFractionTowardSun(sigma, sigma), 0.158655253931,
        RINGSHADOW_ERFC_TOL);

    // Scale invariance: only z / sigma matters
    TestNearlyEqual(TEXT("F(3, 2) == F(300, 200)"), SOLRingShadow::LayerFractionTowardSun(3.0, 2.0),
        SOLRingShadow::LayerFractionTowardSun(300.0, 200.0), 1.0e-9);

    // Limits and monotonicity
    TestTrue(TEXT("z = 10 sigma -> ~0"), SOLRingShadow::LayerFractionTowardSun(10.0 * sigma, sigma) <
        RINGSHADOW_ERFC_TOL);
    TestTrue(TEXT("z = -10 sigma -> ~1"), SOLRingShadow::LayerFractionTowardSun(-10.0 * sigma, sigma) >
        1.0 - RINGSHADOW_ERFC_TOL);
    double previous = 2.0;
    bool bOk = true;
    for (int32 i = -200; i <= 200; ++i)
    {
        const double value = SOLRingShadow::LayerFractionTowardSun(i * 0.5, sigma);
        bOk &= RingShadowInUnit(value) && value <= previous + 1.0e-12;
        previous = value;
    }
    TestTrue(TEXT("Non-increasing in z and within [0, 1]"), bOk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowLayerFractionEdgeTest,
    "SOLTest.RingShadowMath.LayerFractionTowardSunEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero, negative or non-finite thickness is a thin plane (step); non-finite heights follow the contract
bool FSOLRingShadowLayerFractionEdgeTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    TestEqual(TEXT("sigma 0, z > 0 -> 0"), SOLRingShadow::LayerFractionTowardSun(1.0, 0.0), 0.0);
    TestEqual(TEXT("sigma 0, z < 0 -> 1"), SOLRingShadow::LayerFractionTowardSun(-1.0, 0.0), 1.0);
    TestEqual(TEXT("sigma 0, z = 0 -> 0.5"), SOLRingShadow::LayerFractionTowardSun(0.0, 0.0), 0.5);
    TestEqual(TEXT("sigma 0, tiny z > 0 -> 0"), SOLRingShadow::LayerFractionTowardSun(1.0e-300, 0.0), 0.0);
    TestEqual(TEXT("sigma < 0 is a step (z > 0)"), SOLRingShadow::LayerFractionTowardSun(5.0, -5.0), 0.0);
    TestEqual(TEXT("sigma < 0 is a step (z < 0)"), SOLRingShadow::LayerFractionTowardSun(-5.0, -5.0), 1.0);
    TestEqual(TEXT("NaN sigma is a step"), SOLRingShadow::LayerFractionTowardSun(-5.0, nan), 1.0);
    TestEqual(TEXT("inf sigma is a step"), SOLRingShadow::LayerFractionTowardSun(5.0, inf), 0.0);
    TestEqual(TEXT("NaN z -> 0"), SOLRingShadow::LayerFractionTowardSun(nan, 20.0), 0.0);
    TestEqual(TEXT("+inf z -> 0"), SOLRingShadow::LayerFractionTowardSun(inf, 20.0), 0.0);
    TestEqual(TEXT("-inf z -> 1"), SOLRingShadow::LayerFractionTowardSun(-inf, 20.0), 1.0);
    return true;
}

//====================================================================================================================
// Ring shadow transmittance
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowVerticalSunTest, "SOLTest.RingShadowMath.RingShadowVerticalSun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With the Sun along the normal, a thin ring dims receivers on its dark side by exp(-tau) and never the lit side
bool FSOLRingShadowVerticalSunTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d up(0.0, 0.0, 1.0);
    const FVector3d down(0.0, 0.0, -1.0);

    TestNearlyEqual(TEXT("Under B -> exp(-2)"), SOLRingShadow::RingShadowTransmittance(FVector3d(105.0e6, 0.0, -1.0e7),
        up, alpha, 0.0, saturn), std::exp(-RINGSHADOW_B_TAU), RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Under B on the y axis -> exp(-2)"), SOLRingShadow::RingShadowTransmittance(FVector3d(0.0,
        105.0e6, -1.0e7), up, alpha, 0.0, saturn), std::exp(-RINGSHADOW_B_TAU), RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Under Cassini -> exp(-0.1)"), SOLRingShadow::RingShadowTransmittance(FVector3d(120.0e6, 0.0,
        -1.0e7), up, alpha, 0.0, saturn), std::exp(-RINGSHADOW_CD_TAU), RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Under A -> exp(-0.5)"), SOLRingShadow::RingShadowTransmittance(FVector3d(130.0e6, 0.0,
        -1.0e7), up, alpha, 0.0, saturn), std::exp(-RINGSHADOW_A_TAU), RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Under the inner hole -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(80.0e6, 0.0,
        -1.0e7), up, alpha, 0.0, saturn), 1.0, RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Outside the rings -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(140.0e6, 0.0,
        -1.0e7), up, alpha, 0.0, saturn), 1.0, RINGSHADOW_FILTER_TOL);

    // The lit side is never shadowed by a thin ring
    TestEqual(TEXT("Above B with the Sun above -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(105.0e6, 0.0,
        1.0e7), up, alpha, 0.0, saturn), 1.0);
    TestEqual(TEXT("Below B with the Sun below -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(105.0e6, 0.0,
        -1.0e7), down, alpha, 0.0, saturn), 1.0);
    TestNearlyEqual(TEXT("Above B with the Sun below -> exp(-2)"), SOLRingShadow::RingShadowTransmittance(FVector3d(
        105.0e6, 0.0, 1.0e7), down, alpha, 0.0, saturn), std::exp(-RINGSHADOW_B_TAU), RINGSHADOW_FILTER_TOL);

    // The Sun vector is normalized internally
    TestNearlyEqual(TEXT("Unnormalized Sun vector"), SOLRingShadow::RingShadowTransmittance(FVector3d(105.0e6, 0.0,
        -1.0e7), FVector3d(0.0, 0.0, 1.0e11), alpha, 0.0, saturn), std::exp(-RINGSHADOW_B_TAU), RINGSHADOW_FILTER_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowObliqueTest, "SOLTest.RingShadowMath.RingShadowObliqueSun",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An oblique Sun dims by exp(-tau / mu) at the radius where the ray to the Sun pierces the plane
bool FSOLRingShadowObliqueTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alpha = RingShadowSaturnAlpha();
    const double mu = 0.5;
    const double horizontal = std::sqrt(1.0 - mu * mu);
    const FVector3d sunRadial(horizontal, 0.0, mu);
    const FVector3d sunAzimuthal(0.0, horizontal, mu);
    const double distance = 2.0e7;

    // Hit in A at 129e6 along +x, Sun leaning radially outward: the receiver sits under B, but the ray crosses A
    const FVector3d hitA(129.0e6, 0.0, 0.0);
    TestNearlyEqual(TEXT("Radial Sun, hit in A -> exp(-0.5 / 0.5)"), SOLRingShadow::RingShadowTransmittance(
        RingShadowReceiverFor(hitA, sunRadial, distance), sunRadial, alpha, 0.0, saturn), std::exp(-1.0),
        RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Azimuthal Sun, hit in A -> exp(-1)"), SOLRingShadow::RingShadowTransmittance(
        RingShadowReceiverFor(hitA, sunAzimuthal, distance), sunAzimuthal, alpha, 0.0, saturn), std::exp(-1.0),
        RINGSHADOW_FILTER_TOL);

    // Hit in B: exp(-2 / 0.5)
    const FVector3d hitB(100.0e6, 0.0, 0.0);
    TestNearlyEqual(TEXT("Hit in B -> exp(-4)"), SOLRingShadow::RingShadowTransmittance(RingShadowReceiverFor(hitB,
        sunRadial, distance), sunRadial, alpha, 0.0, saturn), std::exp(-4.0), RINGSHADOW_FILTER_TOL);

    // A ray that leaves the plane in the inner hole is lit, even though the receiver is under B
    const FVector3d hitHole(85.0e6, 0.0, 0.0);
    const FVector3d inward(-horizontal, 0.0, mu);
    TestNearlyEqual(TEXT("Ray through the inner hole -> 1"), SOLRingShadow::RingShadowTransmittance(
        RingShadowReceiverFor(hitHole, inward, distance), inward, alpha, 0.0, saturn), 1.0, RINGSHADOW_FILTER_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowGrazingTest, "SOLTest.RingShadowMath.RingShadowParallelAndGrazing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Sun in (or within MIN_SUN_ELEVATION_SIN of) the plane casts no ring shadow; just above it the slant is huge
bool FSOLRingShadowGrazingTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alpha = RingShadowSaturnAlpha();
    const double minMu = SOL::RING_SHADOW_MIN_SUN_ELEVATION_SIN;

    TestEqual(TEXT("Sun exactly in the plane -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(100.0e6, 0.0,
        -10.0), FVector3d(1.0, 0.0, 0.0), alpha, 0.0, saturn), 1.0);
    TestEqual(TEXT("Sun in the plane, receiver in the plane -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(
        100.0e6, 0.0, 0.0), FVector3d(0.0, 1.0, 0.0), alpha, 0.0, saturn), 1.0);
    const FVector3d grazing = FVector3d(1.0, 0.0, 0.5 * minMu).GetSafeNormal();
    TestEqual(TEXT("|mu| below the minimum -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(100.0e6, 0.0,
        -10.0), grazing, alpha, 0.0, saturn), 1.0);

    // Just above the minimum, with the hit in B, the slant optical depth is ~2 / (2 minMu): effectively opaque
    const double mu = 2.0 * minMu;
    const FVector3d shallow(std::sqrt(1.0 - mu * mu), 0.0, mu);
    const FVector3d hit(104.79e6, 0.0, 0.0);
    const double value = SOLRingShadow::RingShadowTransmittance(RingShadowReceiverFor(hit, shallow, 500.0), shallow,
        alpha, 0.0, saturn);
    TestTrue(FString::Printf(TEXT("Shallow Sun through B is effectively opaque (%.3g)"), value),
        value >= 0.0 && value < 1.0e-12);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowInvalidTest, "SOLTest.RingShadowMath.RingShadowInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-finite inputs, a zero Sun vector or an empty profile are lit; a bad alpha or thickness is treated as 0
bool FSOLRingShadowInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d up(0.0, 0.0, 1.0);
    const FVector3d underB(105.0e6, 0.0, -1.0e7);

    TestEqual(TEXT("NaN receiver -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(nan, 0.0, -1.0e7), up, alpha,
        0.0, saturn), 1.0);
    TestEqual(TEXT("inf receiver -> 1"), SOLRingShadow::RingShadowTransmittance(FVector3d(105.0e6, 0.0, -inf), up,
        alpha, 0.0, saturn), 1.0);
    TestEqual(TEXT("NaN Sun -> 1"), SOLRingShadow::RingShadowTransmittance(underB, FVector3d(0.0, nan, 1.0), alpha,
        0.0, saturn), 1.0);
    TestEqual(TEXT("Zero Sun -> 1"), SOLRingShadow::RingShadowTransmittance(underB, FVector3d::ZeroVector, alpha, 0.0,
        saturn), 1.0);
    TestEqual(TEXT("Empty profile -> 1"), SOLRingShadow::RingShadowTransmittance(underB, up, alpha, 0.0,
        TConstArrayView<FRingShadowBand>()), 1.0);

    // A bad alpha is a hard shadow: directly under the B/Cassini edge the point sample takes the outer band
    const FVector3d underEdge(RINGSHADOW_B_OUTER_M, 0.0, -1.0e7);
    const double hard = SOLRingShadow::RingShadowTransmittance(underEdge, up, 0.0, 0.0, saturn);
    TestNearlyEqual(TEXT("alpha 0 under the edge -> exp(-0.1)"), hard, std::exp(-RINGSHADOW_CD_TAU),
        RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("NaN alpha == alpha 0"), SOLRingShadow::RingShadowTransmittance(underEdge, up, nan, 0.0,
        saturn), hard, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("Negative alpha == alpha 0"), SOLRingShadow::RingShadowTransmittance(underEdge, up, -alpha,
        0.0, saturn), hard, RINGSHADOW_TIGHT_TOL);

    // A bad thickness is a thin plane
    TestNearlyEqual(TEXT("NaN thickness -> thin plane"), SOLRingShadow::RingShadowTransmittance(underB, up, alpha, nan,
        saturn), std::exp(-RINGSHADOW_B_TAU), RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Negative thickness -> thin plane"), SOLRingShadow::RingShadowTransmittance(underB, up, alpha,
        -20.0, saturn), std::exp(-RINGSHADOW_B_TAU), RINGSHADOW_FILTER_TOL);

    // Always in [0, 1] over a sweep of receivers and Sun directions
    bool bOk = true;
    for (int32 i = 0; i < 12; ++i)
    {
        for (int32 j = 0; j < 9; ++j)
        {
            const double azimuth = 0.5 * i;
            const double elevation = -1.4 + 0.35 * j;
            const FVector3d sun(std::cos(elevation) * std::cos(azimuth), std::cos(elevation) * std::sin(azimuth),
                std::sin(elevation));
            const FVector3d receiver(60.0e6 + 8.0e6 * i, -3.0e7 + 7.0e6 * j, -2.0e7 + 4.0e6 * j);
            bOk &= RingShadowInUnit(SOLRingShadow::RingShadowTransmittance(receiver, sun, alpha, 20.0, saturn));
        }
    }
    TestTrue(TEXT("Always finite and in [0, 1]"), bOk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowPenumbraEdgeTest, "SOLTest.RingShadowMath.RingShadowPenumbraAtEdge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Across a band edge the shadow blends over +-L tan(alpha): exact mean at the edge, a box ramp, monotonic, and a
// hard step for a point Sun
bool FSOLRingShadowPenumbraEdgeTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d up(0.0, 0.0, 1.0);
    const double distance = 1.0e7;
    const double w = distance * std::tan(alpha);
    const double edge = RINGSHADOW_B_OUTER_M;
    const double eB = std::exp(-RINGSHADOW_B_TAU);
    const double eCD = std::exp(-RINGSHADOW_CD_TAU);

    // Receiver at radius r under a vertical Sun at depth distance
    auto at = [&](const double r, const double a)
    {
        return SOLRingShadow::RingShadowTransmittance(FVector3d(r, 0.0, -distance), up, a, 0.0, saturn);
    };

    // Point Sun: hard step at the edge
    TestNearlyEqual(TEXT("Point Sun, 1 m inside B -> exp(-2)"), at(edge - 1.0, 0.0), eB, RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Point Sun, 1 m inside Cassini -> exp(-0.1)"), at(edge + 1.0, 0.0), eCD,
        RINGSHADOW_FILTER_TOL);

    // Real Sun: box ramp of half-width w
    TestNearlyEqual(TEXT("At the edge -> mean of both"), at(edge, alpha), 0.5 * (eB + eCD), RINGSHADOW_PENUMBRA_TOL);
    TestNearlyEqual(TEXT("Half a half-width inside B -> 3/4 B + 1/4 Cassini"), at(edge - 0.5 * w, alpha),
        0.75 * eB + 0.25 * eCD, RINGSHADOW_PENUMBRA_TOL);
    TestNearlyEqual(TEXT("Half a half-width inside Cassini -> 1/4 B + 3/4 Cassini"), at(edge + 0.5 * w, alpha),
        0.25 * eB + 0.75 * eCD, RINGSHADOW_PENUMBRA_TOL);
    TestNearlyEqual(TEXT("Beyond the penumbra on the B side -> exp(-2)"), at(edge - 1.01 * w, alpha), eB,
        RINGSHADOW_FILTER_TOL);
    TestNearlyEqual(TEXT("Beyond the penumbra on the Cassini side -> exp(-0.1)"), at(edge + 1.01 * w, alpha), eCD,
        RINGSHADOW_FILTER_TOL);

    // Monotonic across the edge, strictly inside the penumbra
    double previous = -1.0;
    bool bMonotonic = true;
    bool bStrict = true;
    for (int32 i = -40; i <= 40; ++i)
    {
        const double offset = 0.05 * i * w;
        const double value = at(edge + offset, alpha);
        bMonotonic &= value >= previous - RINGSHADOW_FILTER_TOL;
        if (std::fabs(offset) < 0.99 * w)
        {
            bStrict &= value > eB + 1.0e-6 && value < eCD - 1.0e-6;
        }
        previous = value;
    }
    TestTrue(TEXT("Non-decreasing from B into Cassini"), bMonotonic);
    TestTrue(TEXT("Strictly between the band values inside the penumbra"), bStrict);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowPenumbraScaleTest, "SOLTest.RingShadowMath.RingShadowPenumbraScales",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The penumbra widens with distance to the ring and with the Sun's angular size (a nearer Sun)
bool FSOLRingShadowPenumbraScaleTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alphaSaturn = RingShadowSaturnAlpha();
    const double alphaNeptune = std::asin(RINGSHADOW_SUN_RADIUS_M / (RINGSHADOW_NEPTUNE_DISTANCE_AU *
        SOLTestHelpers::AU_M));
    const FVector3d up(0.0, 0.0, 1.0);
    const double edge = RINGSHADOW_B_OUTER_M;
    const double eB = std::exp(-RINGSHADOW_B_TAU);
    const double eCD = std::exp(-RINGSHADOW_CD_TAU);

    // Distance: at 2 near half-widths from the edge, the near receiver is fully under B, the far one is not
    const double nearL = 1.0e6;
    const double farL = 1.0e8;
    const double offset = -2.0 * nearL * std::tan(alphaSaturn);
    const double nearValue = SOLRingShadow::RingShadowTransmittance(FVector3d(edge + offset, 0.0, -nearL), up,
        alphaSaturn, 0.0, saturn);
    const double farValue = SOLRingShadow::RingShadowTransmittance(FVector3d(edge + offset, 0.0, -farL), up,
        alphaSaturn, 0.0, saturn);
    TestNearlyEqual(TEXT("Near receiver is outside its penumbra"), nearValue, eB, RINGSHADOW_FILTER_TOL);
    TestTrue(TEXT("Far receiver is inside its wider penumbra"), farValue > eB + 1.0e-6 && farValue < eCD - 1.0e-6);

    // Ramp shape scales with L: the 3/4 point sits at half a half-width for both distances
    const double distances[] = { 1.0e5, 1.0e7, 1.0e8 };
    for (const double L : distances)
    {
        const double w = L * std::tan(alphaSaturn);
        TestNearlyEqual(FString::Printf(TEXT("L = %g: 3/4 point at half a half-width"), L),
            SOLRingShadow::RingShadowTransmittance(FVector3d(edge - 0.5 * w, 0.0, -L), up, alphaSaturn, 0.0, saturn),
            0.75 * eB + 0.25 * eCD, RINGSHADOW_PENUMBRA_TOL);
    }

    // Sun size: Neptune's smaller Sun leaves a point fully shadowed that is in Saturn's penumbra
    const double L = 1.0e7;
    const double r = edge - 0.6 * L * std::tan(alphaSaturn);
    const double atSaturn = SOLRingShadow::RingShadowTransmittance(FVector3d(r, 0.0, -L), up, alphaSaturn, 0.0,
        saturn);
    const double atNeptune = SOLRingShadow::RingShadowTransmittance(FVector3d(r, 0.0, -L), up, alphaNeptune, 0.0,
        saturn);
    TestTrue(TEXT("Saturn's Sun: inside the penumbra"), atSaturn > eB + 1.0e-6);
    TestNearlyEqual(TEXT("Neptune's Sun: outside the narrower penumbra"), atNeptune, eB, RINGSHADOW_FILTER_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowFootprintStretchTest,
    "SOLTest.RingShadowMath.RingShadowFootprintStretch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With an oblique Sun the penumbra is stretched by 1 / mu when the Sun's azimuth is radial, not when azimuthal
bool FSOLRingShadowFootprintStretchTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alpha = RingShadowSaturnAlpha();
    const double mu = 0.5;
    const double horizontal = std::sqrt(1.0 - mu * mu);
    const FVector3d sunRadial(horizontal, 0.0, mu);
    const FVector3d sunAzimuthal(0.0, horizontal, mu);
    const double L = 2.0e7;
    const double w = L * std::tan(alpha);
    const double edge = RINGSHADOW_B_OUTER_M;
    const double eB2 = std::exp(-RINGSHADOW_B_TAU / mu);
    const double eCD2 = std::exp(-RINGSHADOW_CD_TAU / mu);

    // Hit 1.5 w inside B on the +x axis
    const FVector3d hit(edge - 1.5 * w, 0.0, 0.0);
    const double azimuthal = SOLRingShadow::RingShadowTransmittance(RingShadowReceiverFor(hit, sunAzimuthal, L),
        sunAzimuthal, alpha, 0.0, saturn);
    const double radial = SOLRingShadow::RingShadowTransmittance(RingShadowReceiverFor(hit, sunRadial, L), sunRadial,
        alpha, 0.0, saturn);
    TestNearlyEqual(TEXT("Azimuthal Sun: half-width w, fully under B"), azimuthal, eB2, RINGSHADOW_FILTER_TOL);

    // Radial Sun: half-width 2w, window [edge - 3.5w, edge + 0.5w], one eighth in Cassini
    TestNearlyEqual(TEXT("Radial Sun: half-width 2w reaches Cassini"), radial, 0.875 * eB2 + 0.125 * eCD2,
        RINGSHADOW_PENUMBRA_TOL);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowLayerTest, "SOLTest.RingShadowMath.RingShadowInsideLayer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Inside a layer of finite thickness the shadow deepens smoothly from the lit face to the dark face
bool FSOLRingShadowLayerTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alpha = RingShadowSaturnAlpha();
    const double sigma = 20.0;
    const double sqrt2 = std::sqrt(2.0);
    const FVector3d up(0.0, 0.0, 1.0);
    const FVector3d down(0.0, 0.0, -1.0);
    const double r = 105.0e6;

    // Receiver at height z inside the B ring layer
    auto at = [&](const double z, const FVector3d& sun, const double s)
    {
        return SOLRingShadow::RingShadowTransmittance(FVector3d(r, 0.0, z), sun, alpha, s, saturn);
    };

    // Reference: exp(-tau * 0.5 erfc(z_s / (sigma sqrt 2)))
    auto reference = [&](const double zToSun)
    {
        return std::exp(-RINGSHADOW_B_TAU * 0.5 * std::erfc(zToSun / (sigma * sqrt2)));
    };

    TestNearlyEqual(TEXT("Mid-plane -> exp(-1)"), at(0.0, up, sigma), std::exp(-1.0), RINGSHADOW_LAYER_T_TOL);
    TestNearlyEqual(TEXT("One sigma toward the Sun"), at(sigma, up, sigma), reference(sigma), RINGSHADOW_LAYER_T_TOL);
    TestNearlyEqual(TEXT("One sigma away from the Sun"), at(-sigma, up, sigma), reference(-sigma),
        RINGSHADOW_LAYER_T_TOL);
    TestNearlyEqual(TEXT("Sun below: +sigma is on the dark side"), at(sigma, down, sigma), reference(-sigma),
        RINGSHADOW_LAYER_T_TOL);
    TestNearlyEqual(TEXT("Far below the layer -> full B shadow"), at(-1.0e4, up, sigma), std::exp(-RINGSHADOW_B_TAU),
        RINGSHADOW_LAYER_T_TOL);
    TestNearlyEqual(TEXT("Far above the layer -> lit"), at(1.0e4, up, sigma), 1.0, RINGSHADOW_LAYER_T_TOL);

    // Thin plane at z = 0 is half shadowed (F = 0.5)
    TestNearlyEqual(TEXT("Thin plane, z = 0 -> exp(-1)"), at(0.0, up, 0.0), std::exp(-1.0), RINGSHADOW_FILTER_TOL);

    // Monotonic through the layer: brighter toward the Sun
    double previous = -1.0;
    bool bOk = true;
    for (int32 i = -50; i <= 50; ++i)
    {
        const double value = at(2.0 * i, up, sigma);
        bOk &= RingShadowInUnit(value) && value >= previous - 1.0e-12;
        previous = value;
    }
    TestTrue(TEXT("Non-decreasing with height toward the Sun"), bOk);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowRotationTest, "SOLTest.RingShadowMath.RingShadowAxisymmetric",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Rotating both the receiver and the Sun about the ring normal leaves the transmittance unchanged
bool FSOLRingShadowRotationTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d sun = FVector3d(0.6, 0.3, 0.4).GetSafeNormal();
    const double L = 3.0e7;

    // Hit near the B/Cassini edge so the penumbra filter is active
    const FVector3d hit(RINGSHADOW_B_OUTER_M - 2.0e3, 1.0e6, 0.0);
    const FVector3d receiver = RingShadowReceiverFor(hit, sun, L);
    const double base = SOLRingShadow::RingShadowTransmittance(receiver, sun, alpha, 0.0, saturn);
    TestTrue(TEXT("The base case is in the penumbra"), base > std::exp(-RINGSHADOW_B_TAU / sun.Z) + 1.0e-6 &&
        base < std::exp(-RINGSHADOW_CD_TAU / sun.Z) - 1.0e-6);

    const double angles[] = { 0.3, 1.0, 2.5, -1.7, UE_DOUBLE_PI };
    for (const double angle : angles)
    {
        const double rotated = SOLRingShadow::RingShadowTransmittance(RingShadowRotateZ(receiver, angle),
            RingShadowRotateZ(sun, angle), alpha, 0.0, saturn);
        TestNearlyEqual(FString::Printf(TEXT("Rotation %g about the normal"), angle), rotated, base,
            RINGSHADOW_PENUMBRA_TOL);
    }
    return true;
}

//====================================================================================================================
// Planet shadow
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowSunVisibleExamplesTest, "SOLTest.RingShadowMath.SunVisibleFraction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SunVisibleFraction is 1 - lens / (PI a^2) for two discs, with the disjoint, total and contained cases exact
bool FSOLRingShadowSunVisibleExamplesTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Disjoint -> 1"), SOLRingShadow::SunVisibleFraction(1.0, 1.0, 3.0), 1.0);
    TestNearlyEqual(TEXT("Touching -> 1"), SOLRingShadow::SunVisibleFraction(1.0, 1.0, 2.0), 1.0,
        RINGSHADOW_TIGHT_TOL);
    TestEqual(TEXT("Occluder covers the Sun -> 0"), SOLRingShadow::SunVisibleFraction(1.0, 2.0, 0.5), 0.0);
    TestNearlyEqual(TEXT("Equal discs, concentric -> 0"), SOLRingShadow::SunVisibleFraction(1.0, 1.0, 0.0), 0.0,
        RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("Small occluder, concentric -> 1 - (b/a)^2"), SOLRingShadow::SunVisibleFraction(1.0, 0.5,
        0.0), 0.75, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("Small occluder inside the Sun -> 1 - (b/a)^2"), SOLRingShadow::SunVisibleFraction(1.0, 0.3,
        0.5), 0.91, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("Unit discs one radius apart"), SOLRingShadow::SunVisibleFraction(1.0, 1.0, 1.0),
        1.0 - (2.0 * UE_DOUBLE_PI / 3.0 - 0.5 * std::sqrt(3.0)) / UE_DOUBLE_PI, 1.0e-9);

    // General partial overlaps against the independent lens formula
    struct FCase { double A; double B; double Sep; };
    const FCase cases[] = { { 1.0, 1.5, 1.2 }, { 1.0, 0.4, 0.9 }, { 2.0, 1.0, 2.5 }, { 4.85e-4, 0.5, 0.5 },
        { 4.85e-4, 0.5, 0.5003 }, { 1.0, 100.0, 100.5 } };
    for (const FCase& c : cases)
    {
        const double expected = 1.0 - RingShadowLensArea(c.A, c.B, c.Sep) / (UE_DOUBLE_PI * c.A * c.A);
        TestNearlyEqual(FString::Printf(TEXT("a=%g b=%g sep=%g matches the lens formula"), c.A, c.B, c.Sep),
            SOLRingShadow::SunVisibleFraction(c.A, c.B, c.Sep), expected, 1.0e-6);
    }

    // A huge occluder's limb through the Sun's centre hides about half of it
    TestNearlyEqual(TEXT("Limb through the Sun's centre -> ~0.5"), SOLRingShadow::SunVisibleFraction(1.0e-3, 1.0, 1.0),
        0.5, 0.01);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowSunVisibleMonotonicTest,
    "SOLTest.RingShadowMath.SunVisibleFractionMonotonic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The visible fraction never decreases with separation, never increases with occluder size, is even in the
// separation, stays in [0, 1] and is continuous at the contained-disc boundaries
bool FSOLRingShadowSunVisibleMonotonicTest::RunTest(const FString& /*parameters*/)
{
    const double occluders[] = { 0.3, 1.0, 1.5 };
    for (const double b : occluders)
    {
        double previous = -1.0;
        bool bOk = true;
        for (int32 i = 0; i <= 300; ++i)
        {
            const double sep = 0.01 * i;
            const double value = SOLRingShadow::SunVisibleFraction(1.0, b, sep);
            bOk &= RingShadowInUnit(value) && value >= previous - 1.0e-9;
            bOk &= std::fabs(value - SOLRingShadow::SunVisibleFraction(1.0, b, -sep)) < 1.0e-12;
            previous = value;
        }
        TestTrue(FString::Printf(TEXT("b = %g: non-decreasing in |sep|, even, in [0, 1]"), b), bOk);
    }

    double previous = 2.0;
    bool bOk = true;
    for (int32 i = 0; i <= 300; ++i)
    {
        const double value = SOLRingShadow::SunVisibleFraction(1.0, 0.01 * i, 0.8);
        bOk &= RingShadowInUnit(value) && value <= previous + 1.0e-9;
        previous = value;
    }
    TestTrue(TEXT("Non-increasing in the occluder's size"), bOk);

    // Continuity at sep = a - b (occluder just inside) and sep = b - a (Sun just covered)
    TestNearlyEqual(TEXT("Continuous at sep = a - b"), SOLRingShadow::SunVisibleFraction(1.0, 0.3, 0.7 + 1.0e-9),
        0.91, 1.0e-6);
    TestNearlyEqual(TEXT("Continuous at sep = b - a"), SOLRingShadow::SunVisibleFraction(1.0, 2.0, 1.0 + 1.0e-9), 0.0,
        1.0e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowSunVisibleEdgeTest, "SOLTest.RingShadowMath.SunVisibleFractionEdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A point Sun is a step at the occluder's radius; no occluder is lit; non-finite input is lit
bool FSOLRingShadowSunVisibleEdgeTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    TestEqual(TEXT("Point Sun inside the occluder -> 0"), SOLRingShadow::SunVisibleFraction(0.0, 1.0, 0.5), 0.0);
    TestEqual(TEXT("Point Sun outside the occluder -> 1"), SOLRingShadow::SunVisibleFraction(0.0, 1.0, 1.5), 1.0);
    TestEqual(TEXT("Point Sun on the occluder's edge -> 1"), SOLRingShadow::SunVisibleFraction(0.0, 1.0, 1.0), 1.0);
    TestEqual(TEXT("Negative Sun radius is a point Sun"), SOLRingShadow::SunVisibleFraction(-1.0, 1.0, 0.5), 0.0);
    TestEqual(TEXT("No occluder -> 1"), SOLRingShadow::SunVisibleFraction(1.0, 0.0, 0.0), 1.0);
    TestEqual(TEXT("Negative occluder -> 1"), SOLRingShadow::SunVisibleFraction(1.0, -1.0, 0.0), 1.0);
    TestEqual(TEXT("NaN Sun radius -> 1"), SOLRingShadow::SunVisibleFraction(nan, 1.0, 0.0), 1.0);
    TestEqual(TEXT("NaN occluder -> 1"), SOLRingShadow::SunVisibleFraction(1.0, nan, 0.0), 1.0);
    TestEqual(TEXT("NaN separation -> 1"), SOLRingShadow::SunVisibleFraction(1.0, 1.0, nan), 1.0);
    TestEqual(TEXT("inf Sun radius -> 1"), SOLRingShadow::SunVisibleFraction(inf, 1.0, 0.0), 1.0);
    TestEqual(TEXT("inf separation -> 1"), SOLRingShadow::SunVisibleFraction(1.0, 1.0, inf), 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowPlanetShadowTest, "SOLTest.RingShadowMath.PlanetShadowTransmittance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The planet shadows points behind it, not beside or in front of it, with a hard cylinder for a point Sun
bool FSOLRingShadowPlanetShadowTest::RunTest(const FString& /*parameters*/)
{
    const double R = RINGSHADOW_SATURN_RADIUS_M;
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d sun(1.0, 0.0, 0.0);

    TestEqual(TEXT("Sun side -> 1"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(1.0e8, 0.0, 0.0), sun, R,
        alpha), 1.0);
    TestEqual(TEXT("Beside the planet (terminator plane) -> 1"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(
        0.0, 1.0e8, 0.0), sun, R, alpha), 1.0);
    TestEqual(TEXT("Directly behind -> 0"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(-1.0e8, 0.0, 0.0), sun,
        R, alpha), 0.0);
    TestEqual(TEXT("Behind, 2R off the axis -> 1"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(-1.0e8, 2.0 * R,
        0.0), sun, R, alpha), 1.0);
    TestEqual(TEXT("Inside the planet -> 0"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(1.0e6, 0.0, 0.0), sun,
        R, alpha), 0.0);
    TestEqual(TEXT("On the surface -> 0"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(R, 0.0, 0.0), sun, R,
        alpha), 0.0);
    TestEqual(TEXT("Unnormalized Sun vector"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(-1.0e8, 0.0, 0.0),
        sun * 1.0e12, R, alpha), 0.0);

    // Point Sun: an exact cylinder of radius R
    TestEqual(TEXT("Point Sun, 0.999 R off the axis -> 0"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(-1.0e8,
        0.999 * R, 0.0), sun, R, 0.0), 0.0);
    TestEqual(TEXT("Point Sun, 1.001 R off the axis -> 1"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(-1.0e8,
        1.001 * R, 0.0), sun, R, 0.0), 1.0);
    TestEqual(TEXT("Point Sun, far behind at 0.999 R -> 0"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(-1.0e10,
        0.0, 0.999 * R), sun, R, 0.0), 0.0);

    // Real Sun: the geometric shadow edge is about half lit
    TestNearlyEqual(TEXT("At the geometric edge -> ~0.5"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(-1.0e8,
        R, 0.0), sun, R, alpha), 0.5, 0.02);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowPlanetPenumbraTest, "SOLTest.RingShadowMath.PlanetShadowPenumbra",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Behind the planet the shadow ramps monotonically from umbra to full light within the cone radii, and the ramp
// widens with distance behind the planet
bool FSOLRingShadowPlanetPenumbraTest::RunTest(const FString& /*parameters*/)
{
    const double R = RINGSHADOW_SATURN_RADIUS_M;
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d sun(1.0, 0.0, 0.0);
    const double D = 1.0e8;
    const double pw = D * std::tan(alpha);
    const SOLRingShadow::FPlanetShadowRadii radii = SOLRingShadow::PlanetShadowRadiiM(R, D, alpha);

    // Point behind the planet at distance D, rho off the shadow axis
    auto at = [&](const double rho, const double behind)
    {
        return SOLRingShadow::PlanetShadowTransmittance(FVector3d(-behind, rho, 0.0), sun, R, alpha);
    };

    TestTrue(TEXT("Just inside the umbra -> 0"), at(radii.UmbraRadiusM - 0.05 * pw, D) < 1.0e-6);
    TestTrue(TEXT("Just outside the penumbra -> 1"), at(radii.PenumbraRadiusM + 0.05 * pw, D) > 1.0 - 1.0e-6);

    // Monotonic from the axis outward; partial values only inside the cone radii
    double previous = -1.0;
    bool bMonotonic = true;
    bool bInsideCone = true;
    for (int32 i = 0; i <= 400; ++i)
    {
        const double rho = R - 3.0 * pw + 6.0 * pw * i / 400.0;
        const double value = at(rho, D);
        bMonotonic &= RingShadowInUnit(value) && value >= previous - 1.0e-9;
        if (value > 1.0e-9 && value < 1.0 - 1.0e-9)
        {
            bInsideCone &= rho >= radii.UmbraRadiusM - 0.05 * pw && rho <= radii.PenumbraRadiusM + 0.05 * pw;
        }
        previous = value;
    }
    TestTrue(TEXT("Non-decreasing away from the axis"), bMonotonic);
    TestTrue(TEXT("Partial shadow only within the cone radii"), bInsideCone);

    // Wider further behind: R + pw/2 is in the penumbra at D, but past it at D/10
    const double rho = R + 0.5 * pw;
    const double atD = at(rho, D);
    TestTrue(TEXT("R + pw/2 at D is partial"), atD > 1.0e-6 && atD < 1.0 - 1.0e-6);
    TestTrue(TEXT("R + pw/2 at D/10 is fully lit"), at(rho, 0.1 * D) > 1.0 - 1.0e-6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowPlanetInvalidTest, "SOLTest.RingShadowMath.PlanetShadowInvalidInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Non-finite or degenerate inputs are lit; a bad alpha is a point Sun (hard cylinder)
bool FSOLRingShadowPlanetInvalidTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const double R = RINGSHADOW_SATURN_RADIUS_M;
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d sun(1.0, 0.0, 0.0);
    const FVector3d behind(-1.0e8, 0.0, 0.0);

    TestEqual(TEXT("NaN point -> 1"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(nan, 0.0, 0.0), sun, R,
        alpha), 1.0);
    TestEqual(TEXT("inf point -> 1"), SOLRingShadow::PlanetShadowTransmittance(FVector3d(-inf, 0.0, 0.0), sun, R,
        alpha), 1.0);
    TestEqual(TEXT("NaN Sun -> 1"), SOLRingShadow::PlanetShadowTransmittance(behind, FVector3d(nan, 0.0, 0.0), R,
        alpha), 1.0);
    TestEqual(TEXT("Zero Sun -> 1"), SOLRingShadow::PlanetShadowTransmittance(behind, FVector3d::ZeroVector, R, alpha),
        1.0);
    TestEqual(TEXT("R = 0 -> 1"), SOLRingShadow::PlanetShadowTransmittance(behind, sun, 0.0, alpha), 1.0);
    TestEqual(TEXT("R < 0 -> 1"), SOLRingShadow::PlanetShadowTransmittance(behind, sun, -R, alpha), 1.0);
    TestEqual(TEXT("NaN R -> 1"), SOLRingShadow::PlanetShadowTransmittance(behind, sun, nan, alpha), 1.0);

    // Bad alpha behaves like alpha 0 (hard cylinder)
    const FVector3d nearEdgeInside(-1.0e8, R * (1.0 - 1.0e-4), 0.0);
    const FVector3d nearEdgeOutside(-1.0e8, R * (1.0 + 1.0e-4), 0.0);
    TestEqual(TEXT("NaN alpha, just inside R -> 0"), SOLRingShadow::PlanetShadowTransmittance(nearEdgeInside, sun, R,
        nan), 0.0);
    TestEqual(TEXT("NaN alpha, just outside R -> 1"), SOLRingShadow::PlanetShadowTransmittance(nearEdgeOutside, sun, R,
        nan), 1.0);
    TestEqual(TEXT("Negative alpha, just inside R -> 0"), SOLRingShadow::PlanetShadowTransmittance(nearEdgeInside, sun,
        R, -alpha), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowPlanetRadiiTest, "SOLTest.RingShadowMath.PlanetShadowRadii",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Umbra R - D tan(alpha) (clamped at 0) and penumbra R + D tan(alpha); degenerate inputs follow the contract
bool FSOLRingShadowPlanetRadiiTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double R = RINGSHADOW_SATURN_RADIUS_M;
    const double alpha = RingShadowSaturnAlpha();
    const double t = std::tan(alpha);

    SOLRingShadow::FPlanetShadowRadii radii = SOLRingShadow::PlanetShadowRadiiM(R, 0.0, alpha);
    TestNearlyEqual(TEXT("D = 0: umbra R"), radii.UmbraRadiusM, R, 1.0e-6);
    TestNearlyEqual(TEXT("D = 0: penumbra R"), radii.PenumbraRadiusM, R, 1.0e-6);

    radii = SOLRingShadow::PlanetShadowRadiiM(R, 1.0e8, alpha);
    TestNearlyEqual(TEXT("D = 1e8: umbra R - D tan"), radii.UmbraRadiusM, R - 1.0e8 * t, 1.0e-6);
    TestNearlyEqual(TEXT("D = 1e8: penumbra R + D tan"), radii.PenumbraRadiusM, R + 1.0e8 * t, 1.0e-6);

    radii = SOLRingShadow::PlanetShadowRadiiM(R, 1.0e8, 0.0);
    TestNearlyEqual(TEXT("alpha 0: cylinder umbra"), radii.UmbraRadiusM, R, 1.0e-6);
    TestNearlyEqual(TEXT("alpha 0: cylinder penumbra"), radii.PenumbraRadiusM, R, 1.0e-6);

    radii = SOLRingShadow::PlanetShadowRadiiM(R, -5.0e7, alpha);
    TestNearlyEqual(TEXT("D < 0: umbra R"), radii.UmbraRadiusM, R, 1.0e-6);
    TestNearlyEqual(TEXT("D < 0: penumbra R"), radii.PenumbraRadiusM, R, 1.0e-6);

    // Beyond the umbra's tip the umbra radius is clamped at 0
    const double far = 2.0 * R / t;
    radii = SOLRingShadow::PlanetShadowRadiiM(R, far, alpha);
    TestEqual(TEXT("Past the umbra tip: umbra 0"), radii.UmbraRadiusM, 0.0);
    TestNearlyEqual(TEXT("Past the umbra tip: penumbra 3R"), radii.PenumbraRadiusM, 3.0 * R, 1.0e-6 * R);

    // Alpha is clamped like PenumbraHalfWidthM
    radii = SOLRingShadow::PlanetShadowRadiiM(1.0, 1.0, 0.5 * UE_DOUBLE_PI);
    TestNearlyEqual(TEXT("alpha PI/2 is clamped"), radii.PenumbraRadiusM, 1.0 + std::tan(
        SOL::RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD), 1.0e-9);

    // Degenerate radius or non-finite input -> {0, 0}
    radii = SOLRingShadow::PlanetShadowRadiiM(0.0, 1.0e8, alpha);
    TestTrue(TEXT("R = 0 -> {0, 0}"), radii.UmbraRadiusM == 0.0 && radii.PenumbraRadiusM == 0.0);
    radii = SOLRingShadow::PlanetShadowRadiiM(-R, 1.0e8, alpha);
    TestTrue(TEXT("R < 0 -> {0, 0}"), radii.UmbraRadiusM == 0.0 && radii.PenumbraRadiusM == 0.0);
    radii = SOLRingShadow::PlanetShadowRadiiM(nan, 1.0e8, alpha);
    TestTrue(TEXT("NaN R -> {0, 0}"), radii.UmbraRadiusM == 0.0 && radii.PenumbraRadiusM == 0.0);
    radii = SOLRingShadow::PlanetShadowRadiiM(R, nan, alpha);
    TestTrue(TEXT("NaN D -> {0, 0}"), radii.UmbraRadiusM == 0.0 && radii.PenumbraRadiusM == 0.0);
    radii = SOLRingShadow::PlanetShadowRadiiM(R, 1.0e8, nan);
    TestTrue(TEXT("NaN alpha -> {0, 0}"), radii.UmbraRadiusM == 0.0 && radii.PenumbraRadiusM == 0.0);
    return true;
}

//====================================================================================================================
// Real optical-depth profiles
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowRealProfileStructureTest,
    "SOLTest.RingShadowMath.RealTauProfileStructure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every real profile is non-empty, sorted, non-overlapping, finite, non-negative and within its planet's ring
bool FSOLRingShadowRealProfileStructureTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FSOLPlanetRingDef> rings = SOLPlanetRing::RealRings();
    struct FProfileCase { int32 Index; const TCHAR* Planet; };
    const FProfileCase cases[] = {
        { SOL::RING_PROFILE_INDEX_JUPITER, SOL::BodyNames::JUPITER },
        { SOL::RING_PROFILE_INDEX_SATURN, SOL::BodyNames::SATURN },
        { SOL::RING_PROFILE_INDEX_URANUS, SOL::BodyNames::URANUS },
        { SOL::RING_PROFILE_INDEX_NEPTUNE, SOL::BodyNames::NEPTUNE } };
    const double toleranceM = 100.0e3;

    for (const FProfileCase& c : cases)
    {
        const TConstArrayView<FRingShadowBand> profile = SOLRingShadow::RealTauProfile(c.Index);
        TestTrue(FString::Printf(TEXT("%s: non-empty"), c.Planet), profile.Num() > 0);

        const FSOLPlanetRingDef* ring = rings.FindByPredicate([&](const FSOLPlanetRingDef& def)
        {
            return def.PlanetName == FName(c.Planet);
        });
        if (!TestNotNull(*FString::Printf(TEXT("%s: has a RealRings entry"), c.Planet), ring))
        {
            continue;
        }

        bool bOk = true;
        double previousOuter = -std::numeric_limits<double>::infinity();
        for (int32 i = 0; i < profile.Num(); ++i)
        {
            const FRingShadowBand& band = profile[i];
            const bool bValid = std::isfinite(band.InnerM) && std::isfinite(band.OuterM) && std::isfinite(band.Tau) &&
                band.Tau >= 0.0 && band.InnerM < band.OuterM && band.InnerM >= previousOuter &&
                band.InnerM >= ring->InnerRadiusM - toleranceM && band.OuterM <= ring->OuterRadiusM + toleranceM;
            if (!bValid)
            {
                AddError(FString::Printf(TEXT("%s band %d [%.17g, %.17g) tau %.17g is invalid, unsorted, overlapping "
                    "or outside [%.17g, %.17g]"), c.Planet, i, band.InnerM, band.OuterM, band.Tau, ring->InnerRadiusM,
                    ring->OuterRadiusM));
                bOk = false;
            }
            previousOuter = band.OuterM;
        }
        TestTrue(FString::Printf(TEXT("%s: bands are valid, sorted and inside the ring"), c.Planet), bOk);
    }

    // Unknown indices give an empty view
    TestEqual(TEXT("Index -1 -> empty"), SOLRingShadow::RealTauProfile(-1).Num(), 0);
    TestEqual(TEXT("Index PROFILE_COUNT -> empty"), SOLRingShadow::RealTauProfile(SOL::RING_SHADOW_PROFILE_COUNT).Num(),
        0);
    TestEqual(TEXT("Index 99 -> empty"), SOLRingShadow::RealTauProfile(99).Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowRealSaturnTest, "SOLTest.RingShadowMath.RealTauProfileSaturn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Saturn's real profile has an opaque B ring, a thinner A ring, a clear Cassini Division and Encke Gap, and faint
// C and D rings
bool FSOLRingShadowRealSaturnTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FRingShadowBand> saturn = SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_SATURN);
    const double km = 1000.0;
    const double tauB = SOLRingShadow::OpticalDepthAt(105000.0 * km, saturn);
    const double tauA = SOLRingShadow::OpticalDepthAt(128000.0 * km, saturn);
    const double tauCassini = SOLRingShadow::OpticalDepthAt(120500.0 * km, saturn);
    const double tauC = SOLRingShadow::OpticalDepthAt(85000.0 * km, saturn);
    const double tauD = SOLRingShadow::OpticalDepthAt(70000.0 * km, saturn);
    const double tauEnckeMin = RingShadowMinTau(saturn, 133450.0 * km, 133720.0 * km, km);

    TestTrue(FString::Printf(TEXT("B ring tau %g >= 1"), tauB), tauB >= 1.0);
    TestTrue(FString::Printf(TEXT("A ring tau %g in (0.1, B)"), tauA), tauA > 0.1 && tauA < tauB);
    TestTrue(FString::Printf(TEXT("Cassini tau %g < 0.5 and < B / 2"), tauCassini), tauCassini < 0.5 &&
        tauCassini < 0.5 * tauB);
    TestTrue(FString::Printf(TEXT("Encke Gap minimum tau %g < A / 2"), tauEnckeMin), tauEnckeMin < 0.5 * tauA);
    TestTrue(FString::Printf(TEXT("C ring tau %g < B"), tauC), tauC < tauB);
    TestTrue(FString::Printf(TEXT("D ring tau %g < C ring tau %g"), tauD, tauC), tauD < tauC);

    // The real profile drives a real shadow: a cloud-top point under the B ring with the Sun 20 deg above the plane
    const double R = RINGSHADOW_SATURN_RADIUS_M;
    const double elevation = FMath::DegreesToRadians(20.0);
    const FVector3d sun(std::cos(elevation), 0.0, std::sin(elevation));
    const FVector3d hit(105000.0 * km, 0.0, 0.0);

    // Solve |hit - L sun| = R for the nearer L: L^2 - 2 (hit . sun) L + |hit|^2 - R^2 = 0
    const double b = FVector3d::DotProduct(hit, sun);
    const double L = b - std::sqrt(b * b - (hit.SizeSquared() - R * R));
    const FVector3d cloudTop = hit - sun * L;
    const double shadowed = SOLRingShadow::RingShadowTransmittance(cloudTop, sun, RingShadowSaturnAlpha(), 0.0,
        saturn);
    TestTrue(FString::Printf(TEXT("Cloud top under B is in deep shadow (%g)"), shadowed), shadowed > 0.0 &&
        shadowed < 0.1);
    TestEqual(TEXT("The mirrored cloud top on the Sun's side is lit"), SOLRingShadow::RingShadowTransmittance(
        FVector3d(cloudTop.X, cloudTop.Y, -cloudTop.Z), sun, RingShadowSaturnAlpha(), 0.0, saturn), 1.0);

    // And the planet shadows the ring on its night side
    TestEqual(TEXT("Night-side ring point is in the planet's umbra"), SOLRingShadow::PlanetShadowTransmittance(
        FVector3d(-100000.0 * km, 0.0, 0.0), sun, R, RingShadowSaturnAlpha()), 0.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowRealOthersTest, "SOLTest.RingShadowMath.RealTauProfileOthers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Uranus's epsilon ring stands out from the gaps around it; Jupiter's and Neptune's rings are fainter than Saturn's
bool FSOLRingShadowRealOthersTest::RunTest(const FString& /*parameters*/)
{
    const double km = 1000.0;
    const TConstArrayView<FRingShadowBand> uranus = SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_URANUS);
    const double epsilonMax = RingShadowMaxTau(uranus, 51000.0 * km, 51250.0 * km, km);
    const double betweenMax = RingShadowMaxTau(uranus, 48400.0 * km, 49900.0 * km, km);
    TestTrue(FString::Printf(TEXT("Uranus epsilon ring max tau %g > 0.3"), epsilonMax), epsilonMax > 0.3);
    TestTrue(FString::Printf(TEXT("Uranus between delta and lambda max tau %g < epsilon / 10"), betweenMax),
        betweenMax < 0.1 * epsilonMax);

    const double saturnMax = RingShadowProfileMaxTau(SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_SATURN));
    const double jupiterMax = RingShadowProfileMaxTau(SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_JUPITER));
    const double neptuneMax = RingShadowProfileMaxTau(SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_NEPTUNE));
    TestTrue(FString::Printf(TEXT("Jupiter max tau %g in (0, Saturn %g)"), jupiterMax, saturnMax), jupiterMax > 0.0 &&
        jupiterMax < saturnMax);
    TestTrue(FString::Printf(TEXT("Neptune max tau %g in (0, Saturn %g)"), neptuneMax, saturnMax), neptuneMax > 0.0 &&
        neptuneMax < saturnMax);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowShaderProfileTest, "SOLTest.RingShadowMath.ShaderProfileMatchesCpp",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shaders' generated profile include is byte-identical (up to line endings) to what RealTauProfile generates;
// on drift the regenerated file is written to Saved/RingShadow/ for copying over
bool FSOLRingShadowShaderProfileTest::RunTest(const FString& /*parameters*/)
{
    const FString expected = SOLRingShadow::BuildShaderProfileSource();
    TestTrue(TEXT("Generated source declares 52 bands"), expected.Contains(TEXT("SOLRS_BANDS_KM[52]")));

    const FString path = FPaths::Combine(FPaths::ProjectDir(), SOL::SHADER_SOURCE_FOLDER,
        SOL::RING_SHADOW_PROFILE_SHADER_FILE);
    FString onDisk;
    if (!TestTrue(FString::Printf(TEXT("%s loads"), *path), FFileHelper::LoadFileToString(onDisk, *path)))
    {
        return true;
    }
    onDisk.ReplaceInline(TEXT("\r\n"), TEXT("\n"));
    if (!TestEqual(TEXT("The shader profile include matches RealTauProfile"), onDisk, expected))
    {
        const FString regenerated = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("RingShadow"),
            FPaths::GetCleanFilename(SOL::RING_SHADOW_PROFILE_SHADER_FILE));
        FFileHelper::SaveStringToFile(expected, *regenerated, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
        AddInfo(FString::Printf(TEXT("Regenerated include written to %s"), *regenerated));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowHostInputsTest, "SOLTest.RingShadowMath.HostInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The host-side helpers map planets to profiles, pack (unit Sun, alpha) and encode alpha in the far disc's Sun vector
bool FSOLRingShadowHostInputsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("Saturn -> Saturn profile"), SOLRingShadow::ProfileIndexForPlanet(FName(SOL::BodyNames::SATURN)),
        SOL::RING_PROFILE_INDEX_SATURN);
    TestEqual(TEXT("Neptune -> Neptune profile"), SOLRingShadow::ProfileIndexForPlanet(FName(SOL::BodyNames::NEPTUNE)),
        SOL::RING_PROFILE_INDEX_NEPTUNE);
    TestEqual(TEXT("Earth -> none"), SOLRingShadow::ProfileIndexForPlanet(FName(TEXT("Earth"))), -1);

    const double saturnDistance = RINGSHADOW_SATURN_DISTANCE_AU * SOLTestHelpers::AU_M;
    const FVector4d packed = SOLRingShadow::SunDirectionAndAngularRadius(FVector3d(0.0, 3.0, 4.0), saturnDistance);
    TestNearlyEqual(TEXT("Packed direction is normalized (y)"), packed.Y, 0.6, 1.0e-12);
    TestNearlyEqual(TEXT("Packed direction is normalized (z)"), packed.Z, 0.8, 1.0e-12);
    TestTrue(TEXT("Packed w is the Sun's angular radius"), SOLTestHelpers::RelativeError(packed.W,
        RingShadowSaturnAlpha()) < 1.0e-3);
    const FVector4d none = SOLRingShadow::SunDirectionAndAngularRadius(FVector3d::ZeroVector, saturnDistance);
    TestTrue(TEXT("Zero Sun vector -> zero"), none.X == 0.0 && none.Y == 0.0 && none.Z == 0.0 && none.W == 0.0);

    const FVector3d encoded = SOLRingShadow::EncodeSunDirectionWithAngularRadius(FVector3d(0.0, 0.0, 1.0), 1.0e-3);
    TestNearlyEqual(TEXT("Encoded length is 1 + alpha"), encoded.Length(), 1.001, 1.0e-12);
    TestTrue(TEXT("Encoding keeps the direction"), encoded.X == 0.0 && encoded.Y == 0.0 && encoded.Z > 0.0);
    TestNearlyEqual(TEXT("A NaN alpha encodes as 0"), SOLRingShadow::EncodeSunDirectionWithAngularRadius(FVector3d(
        1.0, 0.0, 0.0), std::numeric_limits<double>::quiet_NaN()).Length(), 1.0, 1.0e-12);
    TestTrue(TEXT("Zero stays zero"), SOLRingShadow::EncodeSunDirectionWithAngularRadius(FVector3d::ZeroVector,
        1.0e-3).IsZero());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowViewpointStateTest, "SOLTest.RingShadowMath.ViewpointShadowState",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The viewpoint state carries the planet shadow, tau / |mu| at the viewpoint's radius, the Sun's side and the height
bool FSOLRingShadowViewpointStateTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FRingShadowBand> saturn = RingShadowSaturnLike();
    const double R = RINGSHADOW_SATURN_RADIUS_M;
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d normal(0.0, 0.0, 2.0);   // unnormalized on purpose
    const FVector3d sun(std::sqrt(0.75), 0.0, -0.5);

    // 30 m above the B ring on the day side: lit by the planet, Sun below the plane, tau 2 / 0.5
    const SOLRingShadow::FViewpointShadowState day = SOLRingShadow::ComputeViewpointShadowState(
        FVector3d(105.0e6, 0.0, 30.0), sun, normal, R, alpha, saturn);
    TestNearlyEqual(TEXT("Day side: no planet shadow"), day.PlanetShadow, 1.0, 1.0e-12);
    TestNearlyEqual(TEXT("Slant optical depth = tau / |mu|"), day.SlantOpticalDepth, 4.0, 1.0e-9);
    TestEqual(TEXT("Sun below the plane -> side -1"), day.SunSide, -1.0);
    TestNearlyEqual(TEXT("Height along the normal"), day.HeightM, 30.0, 1.0e-6);

    // Night side behind the planet: in the umbra
    const SOLRingShadow::FViewpointShadowState night = SOLRingShadow::ComputeViewpointShadowState(
        FVector3d(-105.0e6, 0.0, 0.0), sun, normal, R, alpha, saturn);
    TestEqual(TEXT("Night side: umbra"), night.PlanetShadow, 0.0);

    // Sun in the plane: no ring shadow; degenerate input: the lit default
    TestEqual(TEXT("Sun in the plane -> slant 0"), SOLRingShadow::ComputeViewpointShadowState(FVector3d(105.0e6, 0.0,
        0.0), FVector3d(0.0, 1.0, 0.0), normal, R, alpha, saturn).SlantOpticalDepth, 0.0);
    const SOLRingShadow::FViewpointShadowState bad = SOLRingShadow::ComputeViewpointShadowState(FVector3d(105.0e6,
        0.0, 0.0), FVector3d::ZeroVector, normal, R, alpha, saturn);
    TestTrue(TEXT("Zero Sun -> lit default"), bad.PlanetShadow == 1.0 && bad.SlantOpticalDepth == 0.0);
    return true;
}

//====================================================================================================================
// One body's combined planet and ring transmittance (ship sunlight)
//====================================================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowBodySunTest, "SOLTest.RingShadowMath.BodySunTransmittance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With the ring normal along +Z the result is the planet term times the ring term: 0 in the umbra, 1 on the Sun
// side clear of the rings, and deep ring shadow under the B ring on its dark side
bool FSOLRingShadowBodySunTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FRingShadowBand> saturn = SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_SATURN);
    const double R = RINGSHADOW_SATURN_EQ_RADIUS_M;
    const double alpha = RingShadowSaturnAlpha();
    const double sigma = SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M;
    const FVector3d up(0.0, 0.0, 1.0);
    const double elevation = FMath::DegreesToRadians(30.0);
    const FVector3d sun(std::cos(elevation), 0.0, std::sin(elevation));

    // Behind the planet, in the umbra
    TestEqual(TEXT("Behind the planet in the umbra -> 0"), SOLRingShadow::BodySunTransmittance(-3.0 * R * sun, sun, R,
        alpha, up, sigma, saturn), 0.0);

    // Sun side, high above the lit face of the rings
    TestNearlyEqual(TEXT("Sun side clear of the rings -> 1"), SOLRingShadow::BodySunTransmittance(FVector3d(0.0, 0.0,
        5.0 * R), sun, R, alpha, up, sigma, saturn), 1.0, RINGSHADOW_TIGHT_TOL);
    TestNearlyEqual(TEXT("Sun side at 3 radii toward the Sun -> 1"), SOLRingShadow::BodySunTransmittance(3.0 * R * sun,
        sun, R, alpha, up, sigma, saturn), 1.0, RINGSHADOW_TIGHT_TOL);

    // 1000 km under the B ring (105,000 km) on its dark side
    const FVector3d hitB(105000.0e3, 0.0, 0.0);
    const FVector3d underB = RingShadowReceiverFor(hitB, sun, 1.0e6);
    const double expected = SOLRingShadow::PlanetShadowTransmittance(underB, sun, R, alpha) *
        SOLRingShadow::RingShadowTransmittance(underB, sun, alpha, sigma, saturn);
    const double value = SOLRingShadow::BodySunTransmittance(underB, sun, R, alpha, up, sigma, saturn);
    TestTrue(FString::Printf(TEXT("Under B equals planet * ring (%.17g vs %.17g)"), value, expected),
        RingShadowRelDiff(value, expected) < RINGSHADOW_FILTER_TOL);
    TestTrue(FString::Printf(TEXT("Under B is in shadow (%g < 0.5)"), value), value < 0.5);
    TestTrue(TEXT("Under B is within [0, 1]"), RingShadowInUnit(value));

    // Inside the body
    TestEqual(TEXT("Inside the body -> 0"), SOLRingShadow::BodySunTransmittance(FVector3d(0.5 * R, 0.0, 0.0), sun, R,
        alpha, up, sigma, saturn), 0.0);
    TestEqual(TEXT("At the centre -> 0"), SOLRingShadow::BodySunTransmittance(FVector3d::ZeroVector, sun, R, alpha, up,
        sigma, saturn), 0.0);

    // Every point along a sweep through the system stays in [0, 1] and matches the reference product
    bool bInUnit = true;
    bool bMatches = true;
    for (int32 i = 0; i <= 200; ++i)
    {
        const FVector3d p(-2.5 * R + 5.0 * R * i / 200.0, 0.3 * R, -0.4 * R + 0.8 * R * ((i * 37) % 200) / 200.0);
        const double v = SOLRingShadow::BodySunTransmittance(p, sun, R, alpha, up, sigma, saturn);
        const double ref = SOLRingShadow::PlanetShadowTransmittance(p, sun, R, alpha) *
            SOLRingShadow::RingShadowTransmittance(p, sun, alpha, sigma, saturn);
        bInUnit &= RingShadowInUnit(v);
        bMatches &= std::fabs(v - ref) <= RINGSHADOW_FILTER_TOL * FMath::Max(ref, 1.0e-12) + 1.0e-15;
    }
    TestTrue(TEXT("Sweep stays within [0, 1]"), bInUnit);
    TestTrue(TEXT("Sweep matches planet * ring"), bMatches);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowBodySunFrameTest, "SOLTest.RingShadowMath.BodySunTransmittanceFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The inputs may be in any frame: rotating point, Sun and ring normal together, or scaling the Sun vector or the
// normal by a positive factor, leaves the result unchanged; a tilted normal matches the hand-built ring frame
bool FSOLRingShadowBodySunFrameTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FRingShadowBand> saturn = SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_SATURN);
    const double R = RINGSHADOW_SATURN_EQ_RADIUS_M;
    const double alpha = RingShadowSaturnAlpha();
    const double sigma = SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M;
    const FVector3d up(0.0, 0.0, 1.0);
    const double elevation = FMath::DegreesToRadians(30.0);
    const FVector3d sun(std::cos(elevation), 0.0, std::sin(elevation));

    // Canonical frame: under the B ring, under the A ring and in partial planet shadow
    const FVector3d points[] = {
        RingShadowReceiverFor(FVector3d(105000.0e3, 0.0, 0.0), sun, 1.0e6),
        RingShadowReceiverFor(FVector3d(0.0, 128000.0e3, 0.0), sun, 2.0e6),
        FVector3d(-1.0e8 * sun.X, R, -1.0e8 * sun.Z) };

    // An arbitrary rotation (about a skew axis)
    const FQuat4d rotation(RingShadowUnit(FVector3d(1.0, 2.0, 3.0)), 0.7);

    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(points)); ++i)
    {
        const FVector3d& p = points[i];
        const double canonical = SOLRingShadow::BodySunTransmittance(p, sun, R, alpha, up, sigma, saturn);
        const double reference = SOLRingShadow::PlanetShadowTransmittance(p, sun, R, alpha) *
            SOLRingShadow::RingShadowTransmittance(p, sun, alpha, sigma, saturn);
        TestTrue(FString::Printf(TEXT("Point %d: canonical frame equals the reference (%.17g vs %.17g)"), i, canonical,
            reference), RingShadowRelDiff(canonical, reference) < RINGSHADOW_FILTER_TOL);

        const double rotated = SOLRingShadow::BodySunTransmittance(rotation.RotateVector(p),
            rotation.RotateVector(sun), R, alpha, rotation.RotateVector(up), sigma, saturn);
        TestTrue(FString::Printf(TEXT("Point %d: rotated frame gives the same value (%.17g vs %.17g)"), i, rotated,
            canonical), RingShadowRelDiff(rotated, canonical) < RINGSHADOW_FILTER_TOL);

        const double scaled = SOLRingShadow::BodySunTransmittance(rotation.RotateVector(p),
            rotation.RotateVector(sun) * 1.0e11, R, alpha, rotation.RotateVector(up) * 3.7, sigma, saturn);
        TestTrue(FString::Printf(TEXT("Point %d: scaled Sun and normal give the same value (%.17g vs %.17g)"), i,
            scaled, canonical), RingShadowRelDiff(scaled, canonical) < RINGSHADOW_FILTER_TOL);
    }

    // A tilted ring normal (not a rotated copy of +Z inputs) matches the hand-built ring frame
    const FVector3d tilted(0.3, -0.2, 0.9);
    const FVector3d tiltedSun = RingShadowUnit(FVector3d(0.8, 0.1, 0.7));
    const FVector3d tiltedPoint = RingShadowUnit(FVector3d(0.9, 0.3, -0.2)) * 105000.0e3 - tiltedSun * 1.5e6;
    const double tiltedValue = SOLRingShadow::BodySunTransmittance(tiltedPoint, tiltedSun, R, alpha, tilted, sigma,
        saturn);
    const double tiltedReference = RingShadowBodyReference(tiltedPoint, tiltedSun, R, alpha, tilted, sigma, saturn);
    TestTrue(FString::Printf(TEXT("Tilted normal matches the ring-frame reference (%.17g vs %.17g)"), tiltedValue,
        tiltedReference), RingShadowRelDiff(tiltedValue, tiltedReference) < RINGSHADOW_FILTER_TOL ||
        std::fabs(tiltedValue - tiltedReference) < 1.0e-15);
    TestTrue(TEXT("Tilted value is within [0, 1]"), RingShadowInUnit(tiltedValue));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowBodySunDegenerateTest,
    "SOLTest.RingShadowMath.BodySunTransmittanceDegenerate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An empty profile or a zero or non-finite ring normal leaves the planet term only; a non-finite point or Sun is lit
bool FSOLRingShadowBodySunDegenerateTest::RunTest(const FString& /*parameters*/)
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const TConstArrayView<FRingShadowBand> saturn = SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_SATURN);
    const double R = RINGSHADOW_SATURN_EQ_RADIUS_M;
    const double alpha = RingShadowSaturnAlpha();
    const double sigma = SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M;
    const FVector3d up(0.0, 0.0, 1.0);
    const double elevation = FMath::DegreesToRadians(30.0);
    const FVector3d sun(std::cos(elevation), 0.0, std::sin(elevation));

    // Under the B ring (ring term deep, planet term 1) and at the planet shadow's edge (planet term ~0.5)
    const FVector3d underB = RingShadowReceiverFor(FVector3d(105000.0e3, 0.0, 0.0), sun, 1.0e6);
    const FVector3d atEdge = -1.0e8 * sun + FVector3d(0.0, R, 0.0);
    const FVector3d points[] = { underB, atEdge };
    const FVector3d badNormals[] = { FVector3d::ZeroVector, FVector3d(nan, 0.0, 1.0), FVector3d(0.0, inf, 1.0) };
    const TCHAR* badNormalNames[] = { TEXT("zero"), TEXT("NaN"), TEXT("inf") };

    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(points)); ++i)
    {
        const double planetOnly = SOLRingShadow::PlanetShadowTransmittance(points[i], sun, R, alpha);
        for (int32 j = 0; j < static_cast<int32>(UE_ARRAY_COUNT(badNormals)); ++j)
        {
            TestNearlyEqual(*FString::Printf(TEXT("Point %d, %s normal -> planet term only"), i, badNormalNames[j]),
                SOLRingShadow::BodySunTransmittance(points[i], sun, R, alpha, badNormals[j], sigma, saturn),
                planetOnly, RINGSHADOW_TIGHT_TOL);
        }
        TestNearlyEqual(*FString::Printf(TEXT("Point %d, empty profile -> planet term only"), i),
            SOLRingShadow::BodySunTransmittance(points[i], sun, R, alpha, up, sigma,
                TConstArrayView<FRingShadowBand>()), planetOnly, RINGSHADOW_TIGHT_TOL);
        TestNearlyEqual(*FString::Printf(TEXT("Point %d, profile index -1 -> planet term only"), i),
            SOLRingShadow::BodySunTransmittance(points[i], sun, R, alpha, up, sigma, SOLRingShadow::RealTauProfile(-1)),
            planetOnly, RINGSHADOW_TIGHT_TOL);
    }

    // Sanity: the edge point really is in partial planet shadow, and under B is lit by the planet
    const double edgePlanet = SOLRingShadow::PlanetShadowTransmittance(atEdge, sun, R, alpha);
    TestTrue(FString::Printf(TEXT("Edge point is partially shadowed (%g)"), edgePlanet), edgePlanet > 0.1 &&
        edgePlanet < 0.9);
    TestNearlyEqual(TEXT("Under B with a zero normal -> 1"), SOLRingShadow::BodySunTransmittance(underB, sun, R, alpha,
        FVector3d::ZeroVector, sigma, saturn), 1.0, RINGSHADOW_TIGHT_TOL);

    // Non-finite point or Sun: lit
    TestEqual(TEXT("NaN point -> 1"), SOLRingShadow::BodySunTransmittance(FVector3d(nan, 0.0, 0.0), sun, R, alpha, up,
        sigma, saturn), 1.0);
    TestEqual(TEXT("inf point -> 1"), SOLRingShadow::BodySunTransmittance(FVector3d(-inf, 0.0, 0.0), sun, R, alpha, up,
        sigma, saturn), 1.0);
    TestEqual(TEXT("NaN Sun -> 1"), SOLRingShadow::BodySunTransmittance(underB, FVector3d(nan, 0.0, 0.0), R, alpha, up,
        sigma, saturn), 1.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowShaderLimitsTest, "SOLTest.RingShadowMath.ShaderSearchLimits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The shader's binary search covers the largest profile (2^steps >= bands + 1, minimal), and the generated include
// carries both shader limits
bool FSOLRingShadowShaderLimitsTest::RunTest(const FString& /*parameters*/)
{
    int32 largest = 0;
    for (int32 index = 0; index < SOL::RING_SHADOW_PROFILE_COUNT; ++index)
    {
        largest = FMath::Max(largest, SOLRingShadow::RealTauProfile(index).Num());
    }
    const int32 steps = SOLRingShadow::ShaderSearchSteps();
    TestTrue(FString::Printf(TEXT("2^%d covers %d bands + 1"), steps, largest), (1 << steps) >= largest + 1);
    TestTrue(FString::Printf(TEXT("%d steps is the minimum"), steps), steps == 0 || (1 << (steps - 1)) < largest + 1);
    const FString source = SOLRingShadow::BuildShaderProfileSource();
    TestTrue(TEXT("Generated SOLRS_SEARCH_STEPS"),
        source.Contains(FString::Printf(TEXT("#define SOLRS_SEARCH_STEPS %d\n"), steps)));
    TestTrue(TEXT("Generated SOLRS_MAX_FILTER_BANDS"), source.Contains(FString::Printf(
        TEXT("#define SOLRS_MAX_FILTER_BANDS %d\n"), SOL::RING_SHADOW_MAX_FILTER_BANDS)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLRingShadowRockSelfShadowTest, "SOLTest.RingShadowMath.RockSelfShadowDecomposition",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// M_SOLRingRock's per-pixel self-shadow, planet x exp(-slant tau x F(z x sunSide)) from the viewpoint state, matches
// the full RingShadowTransmittance with the 20 m layer at rock heights through the layer (SDD 16 Amendment 2)
bool FSOLRingShadowRockSelfShadowTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<SOLRingShadow::FTauBand> saturn =
        SOLRingShadow::RealTauProfile(SOL::RING_PROFILE_INDEX_SATURN);
    const double sigma = SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M;
    const double alpha = RingShadowSaturnAlpha();
    const FVector3d normal(0.0, 0.0, 1.0);
    const FVector3d viewpoint(105.0e6, 0.0, 30.0);   // in the B ring's layer, Sun 30 deg below the plane
    for (const FVector3d& sun : { FVector3d(std::sqrt(0.75), 0.0, -0.5), FVector3d(std::sqrt(0.75), 0.0, 0.5) })
    {
        const SOLRingShadow::FViewpointShadowState state = SOLRingShadow::ComputeViewpointShadowState(viewpoint, sun,
            normal, RINGSHADOW_SATURN_RADIUS_M, alpha, saturn);
        for (const double heightM : { -60.0, -20.0, 0.0, 20.0, 60.0 })
        {
            const FVector3d pixel(viewpoint.X + 300.0, 0.0, heightM);
            const double material = state.PlanetShadow * std::exp(-state.SlantOpticalDepth *
                SOLRingShadow::LayerFractionTowardSun(heightM * state.SunSide, sigma));
            const double reference = SOLRingShadow::PlanetShadowTransmittance(pixel, sun, RINGSHADOW_SATURN_RADIUS_M,
                alpha) * SOLRingShadow::RingShadowTransmittance(pixel, sun, alpha, sigma, saturn);
            TestNearlyEqual(FString::Printf(TEXT("Sun z %+.1f, rock at %+.0f m"), sun.Z, heightM), material, reference,
                1.0e-6);
        }

        // The side of the layer away from the Sun is darker than the side facing it
        const double towardSun = std::exp(-state.SlantOpticalDepth *
            SOLRingShadow::LayerFractionTowardSun(60.0, sigma));
        const double awayFromSun = std::exp(-state.SlantOpticalDepth *
            SOLRingShadow::LayerFractionTowardSun(-60.0, sigma));
        TestTrue(FString::Printf(TEXT("Sun z %+.1f: far side darker"), sun.Z), awayFromSun < 0.5 * towardSun);
    }

    // Band edges: a viewpoint 300 m inside either edge of the band at 105,000 km still matches, since a rock's ray to
    // the Sun meets the plane at most ~104 m outward (60 m height, mu 0.5); a ray crossing the edge would not (the
    // material point-samples tau at the viewpoint, SDD 16 Amendment 4)
    double edgeInnerM = 0.0;
    double edgeOuterM = 0.0;
    for (const SOLRingShadow::FTauBand& band : saturn)
    {
        if (band.InnerM <= 105.0e6 && 105.0e6 < band.OuterM)
        {
            edgeInnerM = band.InnerM;
            edgeOuterM = band.OuterM;
        }
    }
    if (!TestTrue(TEXT("A band contains 105,000 km"), edgeOuterM > edgeInnerM + 1000.0))
    {
        return false;
    }
    for (const double radiusM : { edgeInnerM + 300.0, edgeOuterM - 300.0 })
    {
        for (const FVector3d& sun : { FVector3d(std::sqrt(0.75), 0.0, -0.5), FVector3d(std::sqrt(0.75), 0.0, 0.5) })
        {
            const SOLRingShadow::FViewpointShadowState state = SOLRingShadow::ComputeViewpointShadowState(
                FVector3d(radiusM, 0.0, 0.0), sun, normal, RINGSHADOW_SATURN_RADIUS_M, alpha, saturn);
            for (const double heightM : { -60.0, 0.0, 60.0 })
            {
                const FVector3d pixel(radiusM, 0.0, heightM);
                const double material = state.PlanetShadow * std::exp(-state.SlantOpticalDepth *
                    SOLRingShadow::LayerFractionTowardSun(heightM * state.SunSide, sigma));
                const double reference = SOLRingShadow::PlanetShadowTransmittance(pixel, sun,
                    RINGSHADOW_SATURN_RADIUS_M, alpha) * SOLRingShadow::RingShadowTransmittance(pixel, sun, alpha,
                    sigma, saturn);
                TestNearlyEqual(FString::Printf(TEXT("Edge r %.0f m, Sun z %+.1f, rock at %+.0f m"), radiusM, sun.Z,
                    heightM), material, reference, 1.0e-6);
            }
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
