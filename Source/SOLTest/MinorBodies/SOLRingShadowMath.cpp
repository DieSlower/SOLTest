/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "MinorBodies/SOLRingShadowMath.h"

#include "SOLConstants.h"

#include <cmath>
#include <limits>

// File-local helpers live in an anonymous namespace nested in SOLRingShadow, so unity builds cannot collide with them
namespace SOLRingShadow
{
namespace
{
    constexpr double KM = 1000.0;
    constexpr double SQRT_2 = 1.4142135623730951;

    // Abramowitz & Stegun 7.1.26 erfc(x) for x >= 0 (absolute error <= 1.5e-7); the .ush uses the same coefficients
    constexpr double ERFC_P = 0.3275911;
    constexpr double ERFC_A1 = 0.254829592;
    constexpr double ERFC_A2 = -0.284496736;
    constexpr double ERFC_A3 = 1.421413741;
    constexpr double ERFC_A4 = -1.453152027;
    constexpr double ERFC_A5 = 1.061405429;

    // Real normal optical depths, ported unchanged from M_SOLRingFar's 52-band table (SDD 6 Amendment 15, SDD 16 16a).
    // Saturn: D ring; C ring with the Colombo and Maxwell gaps; B ring; Cassini Division with the Huygens ringlet;
    // A ring with the Encke and Keeler gaps
    constexpr FTauBand SATURN_PROFILE[] = {
        { 66900.0 * KM, 74490.0 * KM, 0.008 },
        { 74490.0 * KM, 77750.0 * KM, 0.07 },
        { 77750.0 * KM, 77950.0 * KM, 0.005 },
        { 77950.0 * KM, 84750.0 * KM, 0.09 },
        { 84750.0 * KM, 85700.0 * KM, 0.35 },
        { 85700.0 * KM, 86000.0 * KM, 0.09 },
        { 86000.0 * KM, 86500.0 * KM, 0.3 },
        { 86500.0 * KM, 87340.0 * KM, 0.1 },
        { 87340.0 * KM, 87480.0 * KM, 0.005 },
        { 87480.0 * KM, 87545.0 * KM, 1.0 },
        { 87545.0 * KM, 87610.0 * KM, 0.005 },
        { 87610.0 * KM, 88500.0 * KM, 0.1 },
        { 88500.0 * KM, 89200.0 * KM, 0.35 },
        { 89200.0 * KM, 89800.0 * KM, 0.1 },
        { 89800.0 * KM, 90200.0 * KM, 0.3 },
        { 90200.0 * KM, 91980.0 * KM, 0.15 },
        { 91980.0 * KM, 95000.0 * KM, 1.2 },
        { 95000.0 * KM, 99000.0 * KM, 1.6 },
        { 99000.0 * KM, 104000.0 * KM, 2.5 },
        { 104000.0 * KM, 110000.0 * KM, 4.0 },
        { 110000.0 * KM, 117580.0 * KM, 2.0 },
        { 117580.0 * KM, 117680.0 * KM, 0.1 },
        { 117680.0 * KM, 117800.0 * KM, 0.003 },
        { 117800.0 * KM, 117860.0 * KM, 1.0 },
        { 117860.0 * KM, 118185.0 * KM, 0.003 },
        { 118185.0 * KM, 122170.0 * KM, 0.12 },
        { 122170.0 * KM, 126000.0 * KM, 0.7 },
        { 126000.0 * KM, 133000.0 * KM, 0.55 },
        { 133000.0 * KM, 133410.0 * KM, 0.5 },
        { 133410.0 * KM, 133740.0 * KM, 0.01 },
        { 133740.0 * KM, 136485.0 * KM, 0.45 },
        { 136485.0 * KM, 136527.0 * KM, 0.005 },
        { 136527.0 * KM, 136780.0 * KM, 0.4 },
    };

    // Jupiter: the main ring (the material's halo-side and Amalthea/Thebe-side bands)
    constexpr FTauBand JUPITER_PROFILE[] = {
        { 122500.0 * KM, 127800.0 * KM, 0.02 },
        { 127800.0 * KM, 128900.0 * KM, 0.05 },
        { 128900.0 * KM, 129000.0 * KM, 0.02 },
    };

    // Uranus: the zeta ring, nine narrow rings (6, 5, 4, alpha, beta, eta, gamma, delta, lambda) and the epsilon ring.
    // Narrow rings keep their real widths here; only the far-disc material widens them for visibility
    constexpr FTauBand URANUS_PROFILE[] = {
        { 38000.0 * KM, 39600.0 * KM, 0.02 },
        { 41836.25 * KM, 41837.75 * KM, 0.3 },
        { 42233.0 * KM, 42235.0 * KM, 0.5 },
        { 42570.0 * KM, 42572.0 * KM, 0.3 },
        { 44714.0 * KM, 44722.0 * KM, 0.4 },
        { 45656.5 * KM, 45665.5 * KM, 0.3 },
        { 47175.0 * KM, 47177.0 * KM, 0.4 },
        { 47625.5 * KM, 47628.5 * KM, 1.5 },
        { 48297.0 * KM, 48303.0 * KM, 0.5 },
        { 50023.0 * KM, 50025.0 * KM, 0.1 },
        { 51092.0 * KM, 51150.0 * KM, 1.0 },
    };

    // Neptune: the Galle, Le Verrier, Lassell, Arago and Adams rings (Adams's arcs are azimuthal, not modelled here)
    constexpr FTauBand NEPTUNE_PROFILE[] = {
        { 41900.0 * KM, 43000.0 * KM, 0.03 },
        { 53143.0 * KM, 53256.0 * KM, 0.3 },
        { 53256.0 * KM, 57150.0 * KM, 0.02 },
        { 57150.0 * KM, 57250.0 * KM, 0.15 },
        { 62893.0 * KM, 62930.0 * KM, 0.4 },
    };

    static_assert(UE_ARRAY_COUNT(JUPITER_PROFILE) + UE_ARRAY_COUNT(SATURN_PROFILE) + UE_ARRAY_COUNT(URANUS_PROFILE) +
        UE_ARRAY_COUNT(NEPTUNE_PROFILE) == 52, "The port must keep all 52 bands of M_SOLRingFar's table");

    //////////////////////////////////////////////////////////////////////////
    // Returns a band's tau, or 0 if it is negative or non-finite
    double SanitizedTau(const FTauBand& band)
    {
        return std::isfinite(band.Tau) && band.Tau > 0.0 ? band.Tau : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns exp(-tau * slant), treating an infinite slant as opaque for tau > 0 and clear for tau == 0
    double BandTransmittance(const double tau, const double slantFactor)
    {
        if (tau <= 0.0)
        {
            return 1.0;
        }
        return std::isinf(slantFactor) ? 0.0 : std::exp(-tau * slantFactor);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the index of the first band whose OuterM is greater than radiusM (profile.Num() if none)
    int32 FirstBandEndingAfter(const double radiusM, TConstArrayView<FTauBand> profile)
    {
        int32 low = 0;
        int32 high = profile.Num();
        while (low < high)
        {
            const int32 mid = low + (high - low) / 2;
            if (profile[mid].OuterM > radiusM)
            {
                high = mid;
            }
            else
            {
                low = mid + 1;
            }
        }
        return low;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns A&S 7.1.26 erfc(x) for x >= 0
    double ErfcNonNegative(const double x)
    {
        const double t = 1.0 / (1.0 + ERFC_P * x);
        const double poly = t * (ERFC_A1 + t * (ERFC_A2 + t * (ERFC_A3 + t * (ERFC_A4 + t * ERFC_A5))));
        return poly * std::exp(-x * x);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true if every component of a vector is finite
    bool IsFiniteVector(const FVector3d& v)
    {
        return std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z);
    }
}
    //////////////////////////////////////////////////////////////////////////
    // Returns the Sun's angular radius asin(R_sun / d); PI/2 at or inside the Sun, 0 for NaN or +inf
    double SunAngularRadiusRad(const double sunDistanceM)
    {
        if (std::isnan(sunDistanceM) || sunDistanceM == std::numeric_limits<double>::infinity())
        {
            return 0.0;
        }
        if (sunDistanceM <= SOL::SUN_RADIUS_M)
        {
            return 0.5 * UE_DOUBLE_PI;
        }
        return std::asin(SOL::SUN_RADIUS_M / sunDistanceM);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns L tan(min(alpha, max)); 0 for non-positive or non-finite input
    double PenumbraHalfWidthM(const double distanceToOccluderM, const double sunAngularRadiusRad)
    {
        if (!std::isfinite(distanceToOccluderM) || !std::isfinite(sunAngularRadiusRad) || distanceToOccluderM <= 0.0 ||
            sunAngularRadiusRad <= 0.0)
        {
            return 0.0;
        }
        return distanceToOccluderM * std::tan(FMath::Min(sunAngularRadiusRad,
            SOL::RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns w sqrt(1 + c^2 (1/mu^2 - 1)) with mu and c clamped; 0 for a bad w
    double FootprintRadialHalfWidthM(const double penumbraHalfWidthM, const double sunElevationSin,
        const double radialSunAzimuthCos)
    {
        if (!std::isfinite(penumbraHalfWidthM) || penumbraHalfWidthM <= 0.0)
        {
            return 0.0;
        }
        const double mu = std::isfinite(sunElevationSin) ? FMath::Clamp(std::fabs(sunElevationSin),
            SOL::RING_SHADOW_MIN_SUN_ELEVATION_SIN, 1.0) : 1.0;
        const double c = std::isfinite(radialSunAzimuthCos) ? FMath::Clamp(radialSunAzimuthCos, -1.0, 1.0) : 0.0;
        return penumbraHalfWidthM * std::sqrt(1.0 + c * c * (1.0 / (mu * mu) - 1.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the tau of the band containing radiusM, 0 outside every band (binary search)
    double OpticalDepthAt(const double radiusM, TConstArrayView<FTauBand> profile)
    {
        if (!std::isfinite(radiusM))
        {
            return 0.0;
        }
        const int32 index = FirstBandEndingAfter(radiusM, profile);
        if (index < profile.Num() && profile[index].InnerM <= radiusM)
        {
            return SanitizedTau(profile[index]);
        }
        return 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the exact box-filtered mean transmittance over [c - h, c + h]
    double FilteredTransmittance(const double centerRadiusM, const double halfWidthM, const double slantFactor,
        TConstArrayView<FTauBand> profile)
    {
        if (!std::isfinite(centerRadiusM) || std::isnan(slantFactor) || slantFactor <= 0.0 || profile.Num() == 0)
        {
            return 1.0;
        }

        const double low = centerRadiusM - halfWidthM;
        const double high = centerRadiusM + halfWidthM;
        if (!std::isfinite(halfWidthM) || halfWidthM <= 0.0 || high <= low)
        {
            return BandTransmittance(OpticalDepthAt(centerRadiusM, profile), slantFactor);
        }

        // Sum the absorbed fraction over the bands the window overlaps; uncovered radii transmit fully
        double absorbed = 0.0;
        for (int32 index = FirstBandEndingAfter(low, profile); index < profile.Num() && profile[index].InnerM < high;
            ++index)
        {
            const FTauBand& band = profile[index];
            const double overlap = FMath::Min(high, band.OuterM) - FMath::Max(low, band.InnerM);
            if (overlap > 0.0)
            {
                absorbed += overlap * (1.0 - BandTransmittance(SanitizedTau(band), slantFactor));
            }
        }
        return FMath::Clamp(1.0 - absorbed / (high - low), 0.0, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns 0.5 erfc(z / (sigma sqrt 2)), or a step for a degenerate thickness
    double LayerFractionTowardSun(const double heightTowardSunM, const double layerHalfThicknessM)
    {
        if (std::isnan(heightTowardSunM))
        {
            return 0.0;
        }
        if (!std::isfinite(layerHalfThicknessM) || layerHalfThicknessM <= 0.0 || std::isinf(heightTowardSunM))
        {
            if (heightTowardSunM > 0.0)
            {
                return 0.0;
            }
            return heightTowardSunM < 0.0 ? 1.0 : 0.5;
        }

        // Symmetric by construction, so F(z) + F(-z) = 1 exactly
        const double x = heightTowardSunM / (layerHalfThicknessM * SQRT_2);
        return x >= 0.0 ? 0.5 * ErfcNonNegative(x) : 1.0 - 0.5 * ErfcNonNegative(-x);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ring's sunlight transmittance at a receiver (SDD 16 section 3, plan 16a algorithm)
    double RingShadowTransmittance(const FVector3d& receiverRingFrameM, const FVector3d& towardSunRingFrame,
        const double sunAngularRadiusRad, const double layerHalfThicknessM, TConstArrayView<FTauBand> profile)
    {
        if (!IsFiniteVector(receiverRingFrameM) || !IsFiniteVector(towardSunRingFrame) || profile.Num() == 0)
        {
            return 1.0;
        }
        const double sunLength = towardSunRingFrame.Length();
        if (!(sunLength > 0.0) || !std::isfinite(sunLength))
        {
            return 1.0;
        }
        const double alpha = std::isfinite(sunAngularRadiusRad) && sunAngularRadiusRad > 0.0 ?
            sunAngularRadiusRad : 0.0;
        const double sigma = std::isfinite(layerHalfThicknessM) && layerHalfThicknessM > 0.0 ?
            layerHalfThicknessM : 0.0;

        // A Sun (nearly) in the ring plane casts no ring shadow
        const FVector3d s = towardSunRingFrame / sunLength;
        const double mu = s.Z;
        if (std::fabs(mu) < SOL::RING_SHADOW_MIN_SUN_ELEVATION_SIN)
        {
            return 1.0;
        }

        // Fraction of the layer between the receiver and the Sun
        const double heightTowardSun = mu > 0.0 ? receiverRingFrameM.Z : -receiverRingFrameM.Z;
        const double layerFraction = LayerFractionTowardSun(heightTowardSun, sigma);
        if (layerFraction <= 0.0)
        {
            return 1.0;
        }

        // Where the ray toward the Sun pierces the ring plane, and how far away
        const double t = -receiverRingFrameM.Z / mu;
        const FVector3d hit = receiverRingFrameM + t * s;
        const double hitRadius = std::sqrt(hit.X * hit.X + hit.Y * hit.Y);
        const double sunHorizontal = std::sqrt(s.X * s.X + s.Y * s.Y);

        // Penumbra stretched along the Sun's in-plane azimuth, projected on the radial direction
        const double radialSunCos = hitRadius > 0.0 && sunHorizontal > 0.0 ?
            (hit.X * s.X + hit.Y * s.Y) / (hitRadius * sunHorizontal) : 0.0;
        const double halfWidth = FootprintRadialHalfWidthM(PenumbraHalfWidthM(std::fabs(t), alpha), mu, radialSunCos);
        return FilteredTransmittance(hitRadius, halfWidth, layerFraction / std::fabs(mu), profile);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the uncovered fraction of a Sun disc behind an occluder disc (flat-angle lens geometry)
    double SunVisibleFraction(const double sunAngularRadiusRad, const double occluderAngularRadiusRad,
        const double separationRad)
    {
        if (!std::isfinite(sunAngularRadiusRad) || !std::isfinite(occluderAngularRadiusRad) ||
            !std::isfinite(separationRad))
        {
            return 1.0;
        }
        const double a = sunAngularRadiusRad;
        const double b = occluderAngularRadiusRad;
        const double d = std::fabs(separationRad);
        if (b <= 0.0)
        {
            return 1.0;
        }
        if (a <= 0.0)
        {
            return d < b ? 0.0 : 1.0;
        }
        if (d >= a + b)
        {
            return 1.0;
        }
        if (b >= a + d)
        {
            return 0.0;
        }
        if (a >= b + d)
        {
            return 1.0 - (b * b) / (a * a);
        }

        // Partial overlap: lens area from the half-angles at each centre, via atan2 for precision at tiny a / b
        const double k = (-d + a + b) * (d + a - b) * (d - a + b) * (d + a + b);
        const double rootK = std::sqrt(FMath::Max(k, 0.0));
        const double halfAngleSun = std::atan2(rootK, (d - b) * (d + b) + a * a);
        const double halfAngleOccluder = std::atan2(rootK, (d - a) * (d + a) + b * b);
        const double lens = a * a * halfAngleSun + b * b * halfAngleOccluder - 0.5 * rootK;
        return FMath::Clamp(1.0 - lens / (UE_DOUBLE_PI * a * a), 0.0, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Sun's visible fraction past a spherical planet from a point relative to its centre
    double PlanetShadowTransmittance(const FVector3d& pointRelPlanetM, const FVector3d& towardSun,
        const double planetRadiusM, const double sunAngularRadiusRad)
    {
        if (!IsFiniteVector(pointRelPlanetM) || !IsFiniteVector(towardSun) || !std::isfinite(planetRadiusM) ||
            planetRadiusM <= 0.0 || sunAngularRadiusRad == std::numeric_limits<double>::infinity())
        {
            return 1.0;
        }
        const double sunLength = towardSun.Length();
        if (!(sunLength > 0.0) || !std::isfinite(sunLength))
        {
            return 1.0;
        }
        const double distance = pointRelPlanetM.Length();
        if (distance <= planetRadiusM)
        {
            return 0.0;
        }
        const double alpha = sunAngularRadiusRad > 0.0 ? sunAngularRadiusRad : 0.0;   // NaN fails the test too

        // Angle between the Sun and the planet's centre as seen from the point
        const FVector3d s = towardSun / sunLength;
        const FVector3d towardPlanet = -pointRelPlanetM;
        const double separation = std::atan2(FVector3d::CrossProduct(s, towardPlanet).Length(),
            FVector3d::DotProduct(s, towardPlanet));
        return SunVisibleFraction(alpha, std::asin(planetRadiusM / distance), separation);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the sunlight fraction a body and its rings leave at a point: planet shadow times ring shadow, the ring
    // evaluated in a frame whose z is the ring normal (any in-plane axes: the ring shadow is axisymmetric)
    double BodySunTransmittance(const FVector3d& pointRelBodyM, const FVector3d& towardSun, const double bodyRadiusM,
        const double sunAngularRadiusRad, const FVector3d& ringNormal, const double layerHalfThicknessM,
        const TConstArrayView<FTauBand> profile)
    {
        const double planet = PlanetShadowTransmittance(pointRelBodyM, towardSun, bodyRadiusM, sunAngularRadiusRad);
        const double normalLength = ringNormal.Length();
        if (planet <= 0.0 || profile.IsEmpty() || !(normalLength > 0.0) || !std::isfinite(normalLength))
        {
            return planet;
        }

        // Express the receiver and the Sun in the ring frame (z along the normal)
        const FVector3d axisZ = ringNormal / normalLength;
        FVector3d axisX;
        FVector3d axisY;
        axisZ.FindBestAxisVectors(axisX, axisY);
        const FVector3d receiverRingFrame(FVector3d::DotProduct(pointRelBodyM, axisX),
            FVector3d::DotProduct(pointRelBodyM, axisY), FVector3d::DotProduct(pointRelBodyM, axisZ));
        const FVector3d sunRingFrame(FVector3d::DotProduct(towardSun, axisX), FVector3d::DotProduct(towardSun, axisY),
            FVector3d::DotProduct(towardSun, axisZ));
        return planet * RingShadowTransmittance(receiverRingFrame, sunRingFrame, sunAngularRadiusRad,
            layerHalfThicknessM, profile);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the ship's shadowed state with hysteresis between the enter and leave thresholds
    bool NextShipShadowedState(const bool bWasShadowed, const double sunTransmittance)
    {
        if (std::isnan(sunTransmittance))
        {
            return bWasShadowed;
        }
        return bWasShadowed ? sunTransmittance < SOL::SHIP_SUN_SHADOW_LEAVE_AT :
            sunTransmittance < SOL::SHIP_SUN_SHADOW_ENTER_BELOW;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the umbra and penumbra radii R -/+ D tan(alpha) behind the planet
    FPlanetShadowRadii PlanetShadowRadiiM(const double planetRadiusM, const double distanceBehindM,
        const double sunAngularRadiusRad)
    {
        if (!std::isfinite(planetRadiusM) || !std::isfinite(distanceBehindM) || !std::isfinite(sunAngularRadiusRad) ||
            planetRadiusM <= 0.0)
        {
            return FPlanetShadowRadii();
        }
        const double spread = PenumbraHalfWidthM(distanceBehindM, sunAngularRadiusRad);
        return FPlanetShadowRadii{ FMath::Max(0.0, planetRadiusM - spread), planetRadiusM + spread };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the static real profile for a SOL::RING_PROFILE_INDEX_*, empty for any other index
    TConstArrayView<FTauBand> RealTauProfile(const int32 profileIndex)
    {
        static_assert(SOL::RING_SHADOW_PROFILE_COUNT == 4, "RealTauProfile serves exactly four profiles");
        switch (profileIndex)
        {
        case SOL::RING_PROFILE_INDEX_JUPITER:
            return TConstArrayView<FTauBand>(JUPITER_PROFILE);
        case SOL::RING_PROFILE_INDEX_SATURN:
            return TConstArrayView<FTauBand>(SATURN_PROFILE);
        case SOL::RING_PROFILE_INDEX_URANUS:
            return TConstArrayView<FTauBand>(URANUS_PROFILE);
        case SOL::RING_PROFILE_INDEX_NEPTUNE:
            return TConstArrayView<FTauBand>(NEPTUNE_PROFILE);
        default:
            return TConstArrayView<FTauBand>();
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the smallest step count whose 2^steps covers the largest profile's count + 1 search outcomes
    int32 ShaderSearchSteps()
    {
        int32 largest = 0;
        for (int32 index = 0; index < SOL::RING_SHADOW_PROFILE_COUNT; ++index)
        {
            largest = FMath::Max(largest, RealTauProfile(index).Num());
        }
        int32 steps = 0;
        while ((1 << steps) < largest + 1)
        {
            ++steps;
        }
        return steps;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the generated text of the shaders' profile include (constants plus all bands in km)
    FString BuildShaderProfileSource()
    {
        FString text;
        text += TEXT("/*\n* SOLTest\n* Copyright © 2026 Acid Rain Studios LLC\n*/\n\n");
        text += TEXT("// GENERATED by SOLRingShadow::BuildShaderProfileSource() from RealTauProfile and ")
            TEXT("SOLConstants.h (SDD 16).\n// Do not edit by hand: SOLTest.RingShadowMath.ShaderProfileMatchesCpp ")
            TEXT("fails on any drift and writes the\n// regenerated file to Saved/RingShadow/.\n\n#pragma once\n\n");
        text += FString::Printf(TEXT("static const float SOLRS_MIN_SUN_ELEVATION_SIN = %.9g;\n"),
            SOL::RING_SHADOW_MIN_SUN_ELEVATION_SIN);
        text += FString::Printf(TEXT("static const float SOLRS_MAX_SUN_ANGULAR_RADIUS_RAD = %.9g;\n"),
            SOL::RING_SHADOW_MAX_SUN_ANGULAR_RADIUS_RAD);
        text += FString::Printf(TEXT("static const float SOLRS_LAYER_HALF_THICKNESS_KM = %.9g;\n"),
            SOL::RING_SHADOW_LAYER_HALF_THICKNESS_M / KM);

        // Per-profile start and count into one flat band array
        FString starts;
        FString counts;
        int32 total = 0;
        for (int32 index = 0; index < SOL::RING_SHADOW_PROFILE_COUNT; ++index)
        {
            const int32 count = RealTauProfile(index).Num();
            starts += FString::Printf(TEXT("%s%d"), index == 0 ? TEXT("") : TEXT(", "), total);
            counts += FString::Printf(TEXT("%s%d"), index == 0 ? TEXT("") : TEXT(", "), count);
            total += count;
        }
        text += FString::Printf(TEXT("#define SOLRS_MAX_FILTER_BANDS %d\n"), SOL::RING_SHADOW_MAX_FILTER_BANDS);
        text += FString::Printf(TEXT("#define SOLRS_SEARCH_STEPS %d\n"), ShaderSearchSteps());
        text += FString::Printf(TEXT("static const int SOLRS_PROFILE_COUNT = %d;\n"), SOL::RING_SHADOW_PROFILE_COUNT);
        text += FString::Printf(TEXT("static const int SOLRS_PROFILE_START[%d] = { %s };\n"),
            SOL::RING_SHADOW_PROFILE_COUNT, *starts);
        text += FString::Printf(TEXT("static const int SOLRS_PROFILE_BANDS[%d] = { %s };\n\n"),
            SOL::RING_SHADOW_PROFILE_COUNT, *counts);

        // Bands as (inner km, outer km, tau), profile after profile
        text += FString::Printf(TEXT("static const float3 SOLRS_BANDS_KM[%d] =\n{\n"), total);
        for (int32 index = 0; index < SOL::RING_SHADOW_PROFILE_COUNT; ++index)
        {
            for (const FTauBand& band : RealTauProfile(index))
            {
                text += FString::Printf(TEXT("    float3(%.9g, %.9g, %.9g),\n"), band.InnerM / KM, band.OuterM / KM,
                    band.Tau);
            }
        }
        text += TEXT("};\n");
        return text;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the planet shadow, slant optical depth, Sun side and height at a viewpoint near the ring
    FViewpointShadowState ComputeViewpointShadowState(const FVector3d& viewpointRelPlanetM, const FVector3d& towardSun,
        const FVector3d& ringNormal, const double planetRadiusM, const double sunAngularRadiusRad,
        TConstArrayView<FTauBand> profile)
    {
        FViewpointShadowState state;
        const double sunLength = towardSun.Length();
        const double normalLength = ringNormal.Length();
        if (!IsFiniteVector(viewpointRelPlanetM) || !IsFiniteVector(towardSun) || !IsFiniteVector(ringNormal) ||
            !(sunLength > 0.0) || !(normalLength > 0.0))
        {
            return state;
        }
        const FVector3d n = ringNormal / normalLength;
        const double mu = FVector3d::DotProduct(towardSun / sunLength, n);
        const double height = FVector3d::DotProduct(viewpointRelPlanetM, n);
        const double radius = (viewpointRelPlanetM - height * n).Length();

        state.PlanetShadow = PlanetShadowTransmittance(viewpointRelPlanetM, towardSun, planetRadiusM,
            sunAngularRadiusRad);
        state.SunSide = mu >= 0.0 ? 1.0 : -1.0;
        state.HeightM = height;
        state.SlantOpticalDepth = std::fabs(mu) < SOL::RING_SHADOW_MIN_SUN_ELEVATION_SIN ? 0.0 :
            OpticalDepthAt(radius, profile) / std::fabs(mu);
        return state;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the profile index of a ringed planet, -1 for any other name
    int32 ProfileIndexForPlanet(const FName planetName)
    {
        if (planetName == FName(SOL::BodyNames::JUPITER))
        {
            return SOL::RING_PROFILE_INDEX_JUPITER;
        }
        if (planetName == FName(SOL::BodyNames::SATURN))
        {
            return SOL::RING_PROFILE_INDEX_SATURN;
        }
        if (planetName == FName(SOL::BodyNames::URANUS))
        {
            return SOL::RING_PROFILE_INDEX_URANUS;
        }
        if (planetName == FName(SOL::BodyNames::NEPTUNE))
        {
            return SOL::RING_PROFILE_INDEX_NEPTUNE;
        }
        return SOL::RING_PROFILE_INDEX_INFER;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns (unit Sun direction, Sun angular radius) for a material, zero for a degenerate vector
    FVector4d SunDirectionAndAngularRadius(const FVector3d& towardSunLocal, const double sunDistanceM)
    {
        const double length = towardSunLocal.Length();
        if (!IsFiniteVector(towardSunLocal) || !(length > 0.0) || !std::isfinite(length))
        {
            return FVector4d(0.0, 0.0, 0.0, 0.0);
        }
        const FVector3d unit = towardSunLocal / length;
        return FVector4d(unit.X, unit.Y, unit.Z, SunAngularRadiusRad(sunDistanceM));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the unit Sun direction scaled by (1 + alpha), with a non-finite or negative alpha treated as 0
    FVector3d EncodeSunDirectionWithAngularRadius(const FVector3d& towardSunUnit, const double sunAngularRadiusRad)
    {
        const double alpha = std::isfinite(sunAngularRadiusRad) && sunAngularRadiusRad > 0.0 ?
            sunAngularRadiusRad : 0.0;
        return towardSunUnit * (1.0 + alpha);
    }
}
