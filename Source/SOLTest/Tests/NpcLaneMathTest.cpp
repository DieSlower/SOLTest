/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcLaneMath.h"
#include "Npc/SOLNpcRail.h"
#include "OrbitLines/SOLOrbitLineMath.h"
#include "SOLConstants.h"
#include "UI/SOLHudShapeMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #38 NPC lane-line math (SOLNpcLane, SDD 38 section 6 cases 1-16), written against that contract
// before the production code existed. Doubles are compared to 1e-9 unless a case says otherwise.
// Part 38b, also test-first: the renderer's pure feed (cases 25-32 below: lanes shown, lane component visibility, the
// NpcParams4 and NpcLaneParams0 vectors, the group texels' .w lane data, the per-task ship histogram, the real-time
// flow clock and the ribbon mesh layout), from SDD 38 sections 4.2, 4.5, 4.6 and 4.8 with header forms proposed by
// the test author where the SDD names the behaviour but not the signature. Case 33 (from the 38b review): the ribbon's
// keep-right side direction, LaneOutboundSide, checked by projecting a moved lane point. Part 38c (user, 2026-11-11):
// the flow clock has no pause input (case 31), and short lanes get coarser dash levels down to 4 dashes (cases 1, 11,
// 12). Follow-up (user, 2026-11-11, D15 amendment D): the dashes use one continuous "accordion" scale per lane, laid
// along the lane's on-screen spacing, so nothing pops (cases 11, 12 and the screen spacing case 12b).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCLANE_TOL = 1.0e-9;
    constexpr double NPCLANE_FLOAT_TOL = 1.0e-6;          // Results that pass through the float HUD glow profile
    constexpr double NPCLANE_NAN = std::numeric_limits<double>::quiet_NaN();
    constexpr double NPCLANE_INF = std::numeric_limits<double>::infinity();
    constexpr double NPCLANE_AU_M = 1.495978707e11;
    constexpr double NPCLANE_REF_HEIGHT = 1080.0;                // The contract's reference height (case 9 at 1080p)

    // Lane radii of the default layout (Earth-Moon and an outer lane, SDD 38 section 3)
    constexpr double NPCLANE_RADIUS_INNER_M = 2.0e5;
    constexpr double NPCLANE_RADIUS_OUTER_M = 1.0e7;

    // The four line widths the contract samples (px)
    const double NPCLANE_WIDTHS[] = { 0.5, 1.0, 2.5, 5.0 };

    //////////////////////////////////////////////////////////////////////////
    // Returns the circular distance between two phases in [0, 1) (so 0.9999999999 and 0 are close)
    double NpcLaneWrapDistance(const double a, const double b)
    {
        const double d = FMath::Abs(a - b);
        return FMath::Min(d, 1.0 - d);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's closed form of the line fade (inside the band only): log(d / (NEAR R)) / log(FAR / NEAR)
    double NpcLaneFadeClosedForm(const double distanceM, const double radiusM)
    {
        return FMath::Loge(distanceM / (SOL::NPC_LANE_FADE_NEAR_RADII * radiusM))
            / FMath::Loge(SOL::NPC_LANE_FADE_FAR_RADII / SOL::NPC_LANE_FADE_NEAR_RADII);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the viewport scale height / NPC_LANE_REFERENCE_HEIGHT_PX
    double NpcLaneScale(const double heightPx)
    {
        return heightPx / static_cast<double>(SOL::NPC_LANE_REFERENCE_HEIGHT_PX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's line profile: GlowProfile(d', g) x min(width, 1), d' = max(0, |d| - (max(w, 1) / 2 -
    // HUD_GLOW_CORE_HALF_PX)) / (height / REFERENCE) (D25), with g sanitized like the HUD
    double NpcLaneExpectedProfile(const double distancePx, const double widthPx, const double glowScale,
        const double heightPx = 1080.0)
    {
        const double halfCore = FMath::Max(widthPx, 1.0) * 0.5 - SOL::Hologram::HUD_GLOW_CORE_HALF_PX;
        const double shifted = FMath::Max(0.0, FMath::Abs(distancePx) - halfCore)
            / (heightPx / static_cast<double>(SOL::NPC_LANE_REFERENCE_HEIGHT_PX));
        const float g = SOLHudShapeMath::SanitizeGlowScale(static_cast<float>(glowScale));
        return static_cast<double>(SOLHudShapeMath::GlowProfile(static_cast<float>(shifted), g))
            * FMath::Min(widthPx, 1.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the orbit lines' multi-scale dash coverage with the orbit lines' level and solid constants
    double NpcLaneOrbitDash(const double dashPhase, const double phaseFootprint)
    {
        return SOLOrbitLineMath::MultiScaleDashCoverage(dashPhase, SOL::NPC_LANE_DASH_DUTY, phaseFootprint,
            SOL::ORBIT_LINE_DASH_MIN_PERIOD_PX, SOL::ORBIT_LINE_DASH_MAX_LEVEL, SOL::ORBIT_LINE_DASH_SOLID_START_PX,
            SOL::ORBIT_LINE_DASH_SOLID_END_PX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the contract's accordion lane dash coverage (SDD 38 D15 amendment D): one continuous level for the whole
    // lane, clamp(log2(1 / (laneFootprint x MIN_PERIOD_PX)), NPC_LANE_DASH_MIN_LEVEL, ORBIT_LINE_DASH_MAX_LEVEL), the
    // phase (s - 0.5) x BASE x 2^level - direction x f box-filtered over pixelFootprint x 2^level, then the orbit lines'
    // fade to solid by this pixel's own period 1 / (pixelFootprint x 2^level)
    double NpcLaneAccordionDash(const double s, const double flowPhase, const int32 direction,
        const double laneFootprint, const double pixelFootprint)
    {
        const double level = FMath::Clamp(FMath::Log2(1.0 / (laneFootprint * SOL::ORBIT_LINE_DASH_MIN_PERIOD_PX)),
            static_cast<double>(SOL::NPC_LANE_DASH_MIN_LEVEL), static_cast<double>(SOL::ORBIT_LINE_DASH_MAX_LEVEL));
        const double scale = FMath::Exp2(level);
        const double phase = (s - 0.5) * static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT) * scale
            - static_cast<double>(direction) * flowPhase;
        const double coverage = SOLOrbitLineMath::DashCoverage(phase, SOL::NPC_LANE_DASH_DUTY, pixelFootprint * scale);
        const double solid = FMath::Clamp((1.0 / (pixelFootprint * scale) - SOL::ORBIT_LINE_DASH_SOLID_START_PX)
            / (SOL::ORBIT_LINE_DASH_SOLID_END_PX - SOL::ORBIT_LINE_DASH_SOLID_START_PX), 0.0, 1.0);
        return FMath::Lerp(coverage, 1.0, solid);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of dash periods along a lane L px long on screen (the footprint BASE / L for the lane and
    // every pixel): the rises of the box-filtered coverage through 0.5 over one lane length of pattern (s from 0.0137
    // to 1.0137, so no rise sits on an end), at least 8 samples per pixel; exact for a whole number of periods
    int32 NpcLaneDashCount(const double lanePx, const double flowPhase)
    {
        constexpr double startS = 0.0137;
        const double footprint = static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT) / lanePx;
        const int32 sampleCount = FMath::Max(FMath::CeilToInt32(lanePx * 8.0), 4000);
        int32 rises = 0;
        double previous = SOLNpcLane::LaneDashCoverage(startS, flowPhase, 1, footprint);
        for (int32 step = 1; step <= sampleCount; ++step)
        {
            const double coverage = SOLNpcLane::LaneDashCoverage(startS + static_cast<double>(step)
                / static_cast<double>(sampleCount), flowPhase, 1, footprint);
            rises += previous < 0.5 && coverage >= 0.5 ? 1 : 0;
            previous = coverage;
        }
        return rises;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the mean coverage over a lane L px long on screen, one box-filtered sample per pixel centre (the exact
    // mean of the square wave over the lane)
    double NpcLaneMeanCoverage(const double lanePx, const double flowPhase)
    {
        const double footprint = static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT) / lanePx;
        const int32 pixelCount = FMath::RoundToInt32(lanePx);
        double sum = 0.0;
        for (int32 pixel = 0; pixel < pixelCount; ++pixel)
        {
            sum += SOLNpcLane::LaneDashCoverage((static_cast<double>(pixel) + 0.5) / lanePx, flowPhase, 1, footprint);
        }
        return sum / static_cast<double>(pixelCount);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the angle in radians between two directions (the test's own closed form)
    double NpcLaneAngle(const FVector3d& u, const FVector3d& v)
    {
        return FMath::Acos(FMath::Clamp(FVector3d::DotProduct(u.GetSafeNormal(), v.GetSafeNormal()), -1.0, 1.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the fraction of 4,000 samples along a coverage function that are part lit (strictly between 0.05 and
    // 0.95): a sharp single level leaves only its dash edges there, a size blend leaves its dimmer copy too
    template <typename TCoverage>
    double NpcLanePartLitFraction(const TCoverage& coverageAt)
    {
        constexpr int32 sampleCount = 4000;
        int32 partLit = 0;
        for (int32 step = 0; step < sampleCount; ++step)
        {
            const double coverage = coverageAt((static_cast<double>(step) + 0.5) / static_cast<double>(sampleCount));
            partLit += coverage > 0.05 && coverage < 0.95 ? 1 : 0;
        }
        return static_cast<double>(partLit) / static_cast<double>(sampleCount);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one rail of a kind in a global group
    FSOLNpcRail NpcLaneRail(const SOLNpcScale::EPlacementKind kind, const int32 group)
    {
        FSOLNpcRail rail;
        rail.Kind = static_cast<uint8>(kind);
        rail.Group = static_cast<uint16>(group);
        return rail;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance from a point to the ray from the origin along a unit direction (t >= 0)
    double NpcLanePointToRayM(const FVector3d& pointM, const FVector3d& unitDirection)
    {
        const double t = FMath::Max(0.0, FVector3d::DotProduct(pointM, unitDirection));
        return (pointM - unitDirection * t).Size();
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathConstantsTest, "SOLTest.NpcLaneMath.Constants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1: the proposed tuning values (asserted so a tuning change is a visible test edit) and the two hologram colours,
// neither grey nor white
bool FSOLNpcLaneMathConstantsTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("fade near 100 radii"), static_cast<double>(SOL::NPC_LANE_FADE_NEAR_RADII), 100.0, NPCLANE_TOL);
    TestEqual(TEXT("fade far 400 radii"), static_cast<double>(SOL::NPC_LANE_FADE_FAR_RADII), 400.0, NPCLANE_TOL);
    TestTrue(TEXT("far > near > 1"), SOL::NPC_LANE_FADE_FAR_RADII > SOL::NPC_LANE_FADE_NEAR_RADII
        && SOL::NPC_LANE_FADE_NEAR_RADII > 1.0);
    TestEqual(TEXT("traffic low 100"), static_cast<double>(SOL::NPC_LANE_TRAFFIC_LOW), 100.0, NPCLANE_TOL);
    TestEqual(TEXT("traffic high 100,000"), static_cast<double>(SOL::NPC_LANE_TRAFFIC_HIGH), 100000.0, NPCLANE_TOL);
    TestEqual(TEXT("width min 1.0 px"), static_cast<double>(SOL::NPC_LANE_WIDTH_MIN_PX), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("width max 2.5 px"), static_cast<double>(SOL::NPC_LANE_WIDTH_MAX_PX), 2.5, NPCLANE_TOL);
    TestEqual(TEXT("gap 1.5 px"), static_cast<double>(SOL::NPC_LANE_GAP_PX), 1.5, NPCLANE_TOL);
    TestEqual(TEXT("reference height 1080"), static_cast<double>(SOL::NPC_LANE_REFERENCE_HEIGHT_PX), 1080.0,
        NPCLANE_TOL);
    TestEqual(TEXT("brightness min 0.35"), static_cast<double>(SOL::NPC_LANE_BRIGHTNESS_MIN), 0.35, NPCLANE_TOL);
    TestEqual(TEXT("brightness max 1.0"), static_cast<double>(SOL::NPC_LANE_BRIGHTNESS_MAX), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("64 base dashes"), static_cast<int32>(SOL::NPC_LANE_DASH_BASE_COUNT), 64);

    // Short lanes (D15 amendment, 2026-11-11): the levels go coarser down to 4 dashes per lane
    TestEqual(TEXT("coarsest dash level -4"), static_cast<int32>(SOL::NPC_LANE_DASH_MIN_LEVEL), -4);
    TestEqual(TEXT("coarsest level: 4 dashes per lane"), static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT)
        * FMath::Exp2(static_cast<double>(SOL::NPC_LANE_DASH_MIN_LEVEL)), 4.0, NPCLANE_TOL);
    TestTrue(TEXT("min level <= 0 < max level"), SOL::NPC_LANE_DASH_MIN_LEVEL <= 0
        && SOL::ORBIT_LINE_DASH_MAX_LEVEL > 0);
    TestEqual(TEXT("dash duty 0.5"), static_cast<double>(SOL::NPC_LANE_DASH_DUTY), 0.5, NPCLANE_TOL);
    TestEqual(TEXT("flow 0.75 periods per second"), static_cast<double>(SOL::NPC_LANE_DASH_FLOW_PERIODS_PER_S), 0.75,
        NPCLANE_TOL);
    TestEqual(TEXT("glow pad 12 px"), static_cast<double>(SOL::NPC_LANE_GLOW_PAD_PX), 12.0, NPCLANE_TOL);
    TestEqual(TEXT("64 segments"), static_cast<int32>(SOL::NPC_LANE_SEGMENTS), 64);

    // The two colours: teal-white outbound, cyan inbound, stored as sRGB FColor
    // 38c tuning: the stored outbound colour is a deeper teal so its tone-mapped core reads as #9FD6D0 (D16)
    TestTrue(TEXT("outbound #7FD0C4"), SOL::NPC_LANE_OUTBOUND_COLOR == FColor(0x7F, 0xD0, 0xC4));
    TestTrue(TEXT("inbound #3FD0E0"), SOL::NPC_LANE_INBOUND_COLOR == FColor(0x3F, 0xD0, 0xE0));
    const FColor colors[] = { SOL::NPC_LANE_OUTBOUND_COLOR, SOL::NPC_LANE_INBOUND_COLOR };
    for (const FColor& color : colors)
    {
        const FLinearColor linear(color);
        const double maxChannel = FMath::Max3(linear.R, linear.G, linear.B);
        const double minChannel = FMath::Min3(linear.R, linear.G, linear.B);
        TestTrue(FString::Printf(TEXT("%s is neither grey nor white"), *color.ToHex()),
            maxChannel - minChannel >= 0.1);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathLineFadeTest, "SOLTest.NpcLaneMath.LineFade",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 2: the close-up fade is 0 inside NEAR radii, 1 beyond FAR radii, 0.5 at the geometric mean and linear in log
// distance between; it is scale free, and a bad radius or distance keeps the line (1)
bool FSOLNpcLaneMathLineFadeTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneLineFade;
    const double nearRadii = SOL::NPC_LANE_FADE_NEAR_RADII;
    const double farRadii = SOL::NPC_LANE_FADE_FAR_RADII;
    const double radii[] = { NPCLANE_RADIUS_INNER_M, NPCLANE_RADIUS_OUTER_M };
    for (const double radiusM : radii)
    {
        const FString label = FString::Printf(TEXT("R %.0f m"), radiusM);
        const double nearM = nearRadii * radiusM;
        const double farM = farRadii * radiusM;

        // The ends of the band and beyond
        TestEqual(label + TEXT(": 0 at NEAR R"), LaneLineFade(nearM, radiusM), 0.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": 0 below NEAR R"), LaneLineFade(nearM * 0.5, radiusM), 0.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": 0 at d = 0"), LaneLineFade(0.0, radiusM), 0.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": 0 at a negative d"), LaneLineFade(-radiusM, radiusM), 0.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": 1 at FAR R"), LaneLineFade(farM, radiusM), 1.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": 1 beyond FAR R"), LaneLineFade(farM * 10.0, radiusM), 1.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": 1 at +inf"), LaneLineFade(NPCLANE_INF, radiusM), 1.0, NPCLANE_TOL);

        // 0.5 at the geometric mean (200 R with the proposed values)
        TestEqual(label + TEXT(": 0.5 at sqrt(NEAR FAR) R"), LaneLineFade(FMath::Sqrt(nearRadii * farRadii) * radiusM,
            radiusM), 0.5, NPCLANE_TOL);
        TestEqual(label + TEXT(": 0.5 at 200 R"), LaneLineFade(200.0 * radiusM, radiusM), 0.5, NPCLANE_TOL);

        // Linear in log d: three interior points against the closed form
        for (const double t : { 0.25, 0.5, 0.75 })
        {
            const double d = nearM * FMath::Pow(farRadii / nearRadii, t);
            TestEqual(label + FString::Printf(TEXT(": log-linear at t %.2f"), t), LaneLineFade(d, radiusM), t,
                NPCLANE_TOL);
            TestEqual(label + FString::Printf(TEXT(": closed form at t %.2f"), t), LaneLineFade(d, radiusM),
                NpcLaneFadeClosedForm(d, radiusM), NPCLANE_TOL);
        }
        const double oddM = 137.0 * radiusM;
        TestEqual(label + TEXT(": closed form at 137 R"), LaneLineFade(oddM, radiusM),
            NpcLaneFadeClosedForm(oddM, radiusM), NPCLANE_TOL);

        // Non-decreasing in d, in [0, 1]
        double previous = -1.0;
        for (int32 step = 0; step <= 200; ++step)
        {
            const double d = radiusM * 10.0 * FMath::Pow(10.0, static_cast<double>(step) / 100.0);
            const double fade = LaneLineFade(d, radiusM);
            TestTrue(label + FString::Printf(TEXT(": in [0, 1] at step %d"), step), fade >= 0.0 && fade <= 1.0);
            TestTrue(label + FString::Printf(TEXT(": non-decreasing at step %d"), step), fade >= previous - 1.0e-12);
            previous = fade;
        }
    }

    // Scale free: fade(k d, k R) == fade(d, R)
    constexpr double scaleK = 1.0e3;
    for (const double inRadii : { 50.0, 100.0, 150.0, 200.0, 333.0, 400.0, 900.0 })
    {
        const double d = inRadii * NPCLANE_RADIUS_INNER_M;
        TestEqual(FString::Printf(TEXT("scale free at %.0f R"), inRadii),
            LaneLineFade(scaleK * d, scaleK * NPCLANE_RADIUS_INNER_M), LaneLineFade(d, NPCLANE_RADIUS_INNER_M),
            NPCLANE_TOL);
    }

    // Bad input keeps the line
    TestEqual(TEXT("NaN distance -> 1"), LaneLineFade(NPCLANE_NAN, NPCLANE_RADIUS_INNER_M), 1.0, NPCLANE_TOL);
    const double badRadii[] = { NPCLANE_NAN, 0.0, -NPCLANE_RADIUS_INNER_M, NPCLANE_INF, -NPCLANE_INF };
    for (const double badRadius : badRadii)
    {
        const FString label = FString::Printf(TEXT("radius %f"), badRadius);
        TestEqual(label + TEXT(": 1 at d = 0"), LaneLineFade(0.0, badRadius), 1.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": 1 at 1 km"), LaneLineFade(1000.0, badRadius), 1.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": 1 at 1 AU"), LaneLineFade(NPCLANE_AU_M, badRadius), 1.0, NPCLANE_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathShipPointAlphaTest, "SOLTest.NpcLaneMath.ShipPointAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 3: with lanes shown a lane ship's far point is the fade's complement (so line + points sum to 1 everywhere);
// with lanes off every point draws as today (1)
bool FSOLNpcLaneMathShipPointAlphaTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcLane;
    const double radiusM = NPCLANE_RADIUS_INNER_M;
    for (int32 step = 0; step <= 60; ++step)
    {
        const double d = radiusM * FMath::Pow(10.0, static_cast<double>(step) / 15.0);
        const FString label = FString::Printf(TEXT("d %.3e m"), d);
        TestEqual(label + TEXT(": shown = 1 - fade"), LaneShipPointAlpha(d, radiusM, true),
            1.0 - LaneLineFade(d, radiusM), NPCLANE_TOL);
        TestEqual(label + TEXT(": shown + fade = 1"), LaneShipPointAlpha(d, radiusM, true) + LaneLineFade(d, radiusM),
            1.0, NPCLANE_TOL);
        TestEqual(label + TEXT(": not shown = 1"), LaneShipPointAlpha(d, radiusM, false), 1.0, NPCLANE_TOL);
    }
    TestEqual(TEXT("shown, close: 1"), LaneShipPointAlpha(0.0, radiusM, true), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("shown, far: 0"), LaneShipPointAlpha(1.0e3 * radiusM, radiusM, true), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("not shown, far: 1"), LaneShipPointAlpha(1.0e3 * radiusM, radiusM, false), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("shown, bad radius: the line stays, so 0"), LaneShipPointAlpha(1.0e3, NPCLANE_NAN, true), 0.0,
        NPCLANE_TOL);
    TestEqual(TEXT("not shown, bad radius: 1"), LaneShipPointAlpha(1.0e3, NPCLANE_NAN, false), 1.0, NPCLANE_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathTrafficWeightTest, "SOLTest.NpcLaneMath.TrafficWeight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 4: the ship-count weight is log-linear from TRAFFIC_LOW (0) to TRAFFIC_HIGH (1), clamped; a lane with no ships
// is not drawn, any lane with a ship is
bool FSOLNpcLaneMathTrafficWeightTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcLane;
    TestEqual(TEXT("0 ships: weight 0"), LaneTrafficWeight(0), 0.0, NPCLANE_TOL);
    TestFalse(TEXT("0 ships: not drawn"), IsLaneDrawn(0));
    TestEqual(TEXT("-5 ships: weight 0"), LaneTrafficWeight(-5), 0.0, NPCLANE_TOL);
    TestFalse(TEXT("-5 ships: not drawn"), IsLaneDrawn(-5));
    TestFalse(TEXT("INT32_MIN: not drawn"), IsLaneDrawn(TNumericLimits<int32>::Min()));
    TestEqual(TEXT("1 ship: weight 0"), LaneTrafficWeight(1), 0.0, NPCLANE_TOL);
    TestTrue(TEXT("1 ship: drawn"), IsLaneDrawn(1));
    TestEqual(TEXT("100 ships: weight 0"), LaneTrafficWeight(100), 0.0, NPCLANE_TOL);
    TestTrue(TEXT("100 ships: drawn"), IsLaneDrawn(100));
    TestEqual(TEXT("1,000 ships: 1/3"), LaneTrafficWeight(1000), 1.0 / 3.0, NPCLANE_TOL);
    TestEqual(TEXT("10,000 ships: 2/3"), LaneTrafficWeight(10000), 2.0 / 3.0, NPCLANE_TOL);
    TestEqual(TEXT("100,000 ships: 1"), LaneTrafficWeight(100000), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("1,000,000 ships: 1"), LaneTrafficWeight(1000000), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("INT32_MAX ships: 1"), LaneTrafficWeight(TNumericLimits<int32>::Max()), 1.0, NPCLANE_TOL);
    TestTrue(TEXT("INT32_MAX ships: drawn"), IsLaneDrawn(TNumericLimits<int32>::Max()));

    // The closed form between the ends
    const double low = static_cast<double>(SOL::NPC_LANE_TRAFFIC_LOW);
    const double high = static_cast<double>(SOL::NPC_LANE_TRAFFIC_HIGH);
    for (const int32 count : { 250, 4400, 44000, 89000 })
    {
        const double expected = FMath::Loge(static_cast<double>(count) / low) / FMath::Loge(high / low);
        TestEqual(FString::Printf(TEXT("%d ships: closed form"), count), LaneTrafficWeight(count), expected,
            NPCLANE_TOL);
    }

    // Non-decreasing, in [0, 1], drawn for every count >= 1
    double previous = -1.0;
    for (int32 count = 0; count <= 2000000; count = count < 10 ? count + 1 : count + count / 7)
    {
        const double weight = LaneTrafficWeight(count);
        TestTrue(FString::Printf(TEXT("%d: in [0, 1]"), count), weight >= 0.0 && weight <= 1.0);
        TestTrue(FString::Printf(TEXT("%d: non-decreasing"), count), weight >= previous - 1.0e-12);
        TestEqual(FString::Printf(TEXT("%d: drawn iff >= 1"), count), IsLaneDrawn(count), count >= 1);
        previous = weight;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathLineWidthTest, "SOLTest.NpcLaneMath.LineWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 5: each line is lerp(WIDTH_MIN, WIDTH_MAX, clamped weight) px at the reference height, scaled by the viewport
// height; a bad height reads as the reference
bool FSOLNpcLaneMathLineWidthTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneLineWidthPx;
    const double widthMin = SOL::NPC_LANE_WIDTH_MIN_PX;
    const double widthMax = SOL::NPC_LANE_WIDTH_MAX_PX;
    const double reference = SOL::NPC_LANE_REFERENCE_HEIGHT_PX;

    // At the reference height, with the proposed values
    TestEqual(TEXT("w 0 -> 1.0 px"), LaneLineWidthPx(0.0, 1080.0), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("w 1 -> 2.5 px"), LaneLineWidthPx(1.0, 1080.0), 2.5, NPCLANE_TOL);
    TestEqual(TEXT("w 0.5 -> 1.75 px"), LaneLineWidthPx(0.5, 1080.0), 1.75, NPCLANE_TOL);
    TestEqual(TEXT("w -1 -> clamped to 1.0 px"), LaneLineWidthPx(-1.0, 1080.0), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("w 3 -> clamped to 2.5 px"), LaneLineWidthPx(3.0, 1080.0), 2.5, NPCLANE_TOL);
    TestEqual(TEXT("NaN w -> as 0"), LaneLineWidthPx(NPCLANE_NAN, 1080.0), LaneLineWidthPx(0.0, 1080.0), NPCLANE_TOL);

    // The formula against the constants
    for (const double w : { 0.0, 0.2, 0.5, 0.8, 1.0 })
    {
        TestEqual(FString::Printf(TEXT("w %.1f at the reference"), w), LaneLineWidthPx(w, reference),
            FMath::Lerp(widthMin, widthMax, w), NPCLANE_TOL);
    }

    // Scaled by the viewport height
    for (const double w : { 0.0, 0.5, 1.0 })
    {
        const double at1080 = LaneLineWidthPx(w, 1080.0);
        TestEqual(FString::Printf(TEXT("w %.1f: 720 -> x 2/3"), w), LaneLineWidthPx(w, 720.0), at1080 * 2.0 / 3.0,
            NPCLANE_TOL);
        TestEqual(FString::Printf(TEXT("w %.1f: 2160 -> x 2"), w), LaneLineWidthPx(w, 2160.0), at1080 * 2.0,
            NPCLANE_TOL);
        TestEqual(FString::Printf(TEXT("w %.1f: 3744 -> x scale"), w), LaneLineWidthPx(w, 3744.0),
            FMath::Lerp(widthMin, widthMax, w) * NpcLaneScale(3744.0), NPCLANE_TOL);
        for (const double badHeight : { 0.0, -720.0, NPCLANE_NAN })
        {
            TestEqual(FString::Printf(TEXT("w %.1f: height %f -> the reference"), w, badHeight),
                LaneLineWidthPx(w, badHeight), at1080, NPCLANE_TOL);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathBrightnessTest, "SOLTest.NpcLaneMath.Brightness",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 6: brightness lerp(BRIGHTNESS_MIN, BRIGHTNESS_MAX, clamped weight); NaN reads MIN; non-decreasing
bool FSOLNpcLaneMathBrightnessTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneLineBrightness;
    const double minimum = SOL::NPC_LANE_BRIGHTNESS_MIN;
    const double maximum = SOL::NPC_LANE_BRIGHTNESS_MAX;
    TestEqual(TEXT("w 0 -> MIN"), LaneLineBrightness(0.0), minimum, NPCLANE_TOL);
    TestEqual(TEXT("w 1 -> MAX"), LaneLineBrightness(1.0), maximum, NPCLANE_TOL);
    TestEqual(TEXT("w 0.5 -> the middle"), LaneLineBrightness(0.5), FMath::Lerp(minimum, maximum, 0.5), NPCLANE_TOL);
    TestEqual(TEXT("w 0 -> 0.35"), LaneLineBrightness(0.0), 0.35, NPCLANE_TOL);
    TestEqual(TEXT("w -2 -> MIN"), LaneLineBrightness(-2.0), minimum, NPCLANE_TOL);
    TestEqual(TEXT("w 7 -> MAX"), LaneLineBrightness(7.0), maximum, NPCLANE_TOL);
    TestEqual(TEXT("NaN -> MIN"), LaneLineBrightness(NPCLANE_NAN), minimum, NPCLANE_TOL);
    double previous = -1.0;
    for (int32 step = -10; step <= 30; ++step)
    {
        const double w = static_cast<double>(step) / 20.0;
        const double brightness = LaneLineBrightness(w);
        TestTrue(FString::Printf(TEXT("w %.2f: non-decreasing"), w), brightness >= previous - 1.0e-12);
        TestTrue(FString::Printf(TEXT("w %.2f: in [MIN, MAX]"), w), brightness >= minimum - NPCLANE_TOL
            && brightness <= maximum + NPCLANE_TOL);
        previous = brightness;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathCenterOffsetTest, "SOLTest.NpcLaneMath.CenterOffset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 7: each line's centre sits 0.5 x (width + scaled GAP) px from the lane's centre line, so the two cores never
// overlap; a bad height reads as the reference
bool FSOLNpcLaneMathCenterOffsetTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneLineCenterOffsetPx;
    const double gap = SOL::NPC_LANE_GAP_PX;
    for (const double height : { 720.0, 1080.0, 2160.0 })
    {
        const double scale = NpcLaneScale(height);
        for (const double width : NPCLANE_WIDTHS)
        {
            const FString label = FString::Printf(TEXT("width %.1f at %.0f"), width, height);
            const double offset = LaneLineCenterOffsetPx(width, height);
            TestEqual(label + TEXT(": the formula"), offset, 0.5 * (width + gap * scale), NPCLANE_TOL);
            TestTrue(label + TEXT(": cores do not overlap"), 2.0 * offset - width >= gap * scale - 1.0e-12);
        }
    }
    TestEqual(TEXT("1080, width 1: 1.25 px"), LaneLineCenterOffsetPx(1.0, 1080.0), 1.25, NPCLANE_TOL);
    for (const double badHeight : { 0.0, -1080.0, NPCLANE_NAN })
    {
        TestEqual(FString::Printf(TEXT("height %f -> the reference"), badHeight),
            LaneLineCenterOffsetPx(2.5, badHeight), LaneLineCenterOffsetPx(2.5, SOL::NPC_LANE_REFERENCE_HEIGHT_PX),
            NPCLANE_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathRibbonExtentTest, "SOLTest.NpcLaneMath.RibbonHalfExtent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8 (D25): the ribbon's edge sits where the height-scaled glow has its 1080p pad value: max(width, 1) / 2 -
// CORE_HALF + (PAD + CORE_HALF) x scale, exactly max(width, 1) / 2 + PAD at 1080, never less than max(width, 1) / 2
bool FSOLNpcLaneMathRibbonExtentTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneRibbonHalfExtentPx;
    const double pad = SOL::NPC_LANE_GLOW_PAD_PX;
    const double coreHalf = SOL::Hologram::HUD_GLOW_CORE_HALF_PX;
    for (const double height : { 720.0, 1080.0, 2160.0, 3744.0 })
    {
        for (const double width : { 0.25, 0.5, 1.0, 2.5, 5.0 })
        {
            const FString label = FString::Printf(TEXT("width %.2f at %.0f"), width, height);
            const double halfWidth = FMath::Max(width, 1.0) * 0.5;
            const double extent = LaneRibbonHalfExtentPx(width, height);
            TestEqual(label + TEXT(": the formula"), extent,
                FMath::Max(halfWidth, halfWidth - coreHalf + (pad + coreHalf) * NpcLaneScale(height)), NPCLANE_TOL);
            TestTrue(label + TEXT(": at least max(w, 1) / 2"), extent >= halfWidth);
        }
    }
    for (const double width : { 0.5, 1.0, 2.5 })
    {
        TestEqual(FString::Printf(TEXT("1080, width %.1f: max(w, 1) / 2 + PAD"), width),
            LaneRibbonHalfExtentPx(width, 1080.0), FMath::Max(width, 1.0) * 0.5 + pad, NPCLANE_TOL);
    }
    TestEqual(TEXT("1080, width 1: 12.5 px"), LaneRibbonHalfExtentPx(1.0, 1080.0), 12.5, NPCLANE_TOL);
    TestEqual(TEXT("2160, width 2: 1 - 0.5 + 12.5 x 2 = 25.5 px"), LaneRibbonHalfExtentPx(2.0, 2160.0), 25.5,
        NPCLANE_TOL);
    for (const double badHeight : { 0.0, -720.0, NPCLANE_NAN })
    {
        TestEqual(FString::Printf(TEXT("height %f -> the reference"), badHeight),
            LaneRibbonHalfExtentPx(2.5, badHeight), LaneRibbonHalfExtentPx(2.5, SOL::NPC_LANE_REFERENCE_HEIGHT_PX),
            NPCLANE_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathLineProfileTest, "SOLTest.NpcLaneMath.LineProfile",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 9 at 1080p: the cross-line profile is the HUD glow profile shifted out by the line's half width, times
// min(width, 1): full at the centre, symmetric, falling with distance, more halo with more glow just past the core
// edge outward, crisp at g = 0 and inside the pad
bool FSOLNpcLaneMathLineProfileTest::RunTest(const FString& /*parameters*/)
{
    const double glowMax = SOL::Hologram::HUD_GLOW_SCALE_MAX;
    const double settingMaxG = 2.0 * static_cast<double>(SOL::Hologram::HUD_GLOW_DEFAULT_LEVEL);
    const double pad = SOL::NPC_LANE_GLOW_PAD_PX;

    // The profile at the reference height
    const auto profile = [](const double d, const double width, const double g)
    {
        return SOLNpcLane::LaneLineProfile(d, width, g, NPCLANE_REF_HEIGHT);
    };

    for (const double width : NPCLANE_WIDTHS)
    {
        const double halfWidth = FMath::Max(width, 1.0) * 0.5;
        const FString wLabel = FString::Printf(TEXT("width %.1f"), width);

        // The closed form against the HUD glow profile, and the centre value
        for (const double g : { 0.0, 0.3, 1.0, glowMax })
        {
            const FString label = wLabel + FString::Printf(TEXT(", g %.2f"), g);
            TestEqual(label + TEXT(": centre = min(width, 1)"), profile(0.0, width, g), FMath::Min(width, 1.0),
                NPCLANE_FLOAT_TOL);
            for (const double d : { 0.0, 0.3, halfWidth, halfWidth + 0.5, halfWidth + 2.0, halfWidth + 7.5, 30.0 })
            {
                TestEqual(label + FString::Printf(TEXT(": closed form at %.2f px"), d), profile(d, width, g),
                    NpcLaneExpectedProfile(d, width, g), NPCLANE_FLOAT_TOL);
                TestEqual(label + FString::Printf(TEXT(": symmetric at %.2f px"), d), profile(-d, width, g),
                    profile(d, width, g), NPCLANE_FLOAT_TOL);
            }

            // Non-increasing in |d|
            double previous = 2.0;
            for (int32 step = 0; step <= 160; ++step)
            {
                const double d = static_cast<double>(step) * 0.25;
                const double value = profile(d, width, g);
                TestTrue(label + FString::Printf(TEXT(": non-increasing at %.2f px"), d),
                    value <= previous + NPCLANE_FLOAT_TOL);
                TestTrue(label + FString::Printf(TEXT(": in [0, 1] at %.2f px"), d), value >= 0.0
                    && value <= 1.0 + NPCLANE_FLOAT_TOL);
                previous = value;
            }
        }

        // Just past the core edge outward (sampled from 0.25 px past it; the halo outgrows the core weight it takes
        // from about 0.04 px), more glow never darkens a pixel
        for (const double past : { 0.25, 1.0, 3.0, 8.0, 15.0 })
        {
            const double d = halfWidth + past;
            double previous = -1.0;
            for (const double g : { 0.0, 0.1, 0.3, 0.6, 1.0, glowMax })
            {
                const double value = profile(d, width, g);
                TestTrue(wLabel + FString::Printf(TEXT(": non-decreasing in g at %.2f px, g %.2f"), d, g),
                    value >= previous - NPCLANE_FLOAT_TOL);
                previous = value;
            }
        }

        // g = 0 is crisp: under 0.01 from 1 px past the core edge
        for (int32 step = 0; step <= 40; ++step)
        {
            const double d = halfWidth + 1.0 + static_cast<double>(step) * 0.5;
            TestTrue(wLabel + FString::Printf(TEXT(": g 0 under 0.01 at %.2f px"), d), profile(d, width, 0.0) < 0.01);
        }

        // At the ribbon's edge (the 1080p pad) the glow is under 0.01 for every g the HUD glow setting reaches
        for (const double g : { 0.0, 0.15, 0.3, 0.45, settingMaxG })
        {
            TestTrue(wLabel + FString::Printf(TEXT(": under 0.01 at the pad, g %.2f"), g),
                profile(halfWidth + pad, width, g) < 0.01);
        }
    }

    // Width 1 at g = 1 is the HUD glow profile itself
    for (const double d : { 0.0, 0.25, 0.5, 1.0, 2.0, 5.0, 12.0 })
    {
        TestEqual(FString::Printf(TEXT("width 1, g 1: GlowProfile at %.2f px"), d), profile(d, 1.0, 1.0),
            static_cast<double>(SOLHudShapeMath::GlowProfile(static_cast<float>(d), 1.0f)), NPCLANE_FLOAT_TOL);
    }

    // A bad g is sanitized like the HUD's: NaN as 1, clamped to [0, HUD_GLOW_SCALE_MAX]
    for (const double d : { 1.0, 3.0, 9.0 })
    {
        TestEqual(FString::Printf(TEXT("NaN g as 1 at %.0f px"), d), profile(d, 2.5, NPCLANE_NAN), profile(d, 2.5, 1.0),
            NPCLANE_FLOAT_TOL);
        TestEqual(FString::Printf(TEXT("g 9 as the max at %.0f px"), d), profile(d, 2.5, 9.0),
            profile(d, 2.5, glowMax), NPCLANE_FLOAT_TOL);
        TestEqual(FString::Printf(TEXT("g -1 as 0 at %.0f px"), d), profile(d, 2.5, -1.0), profile(d, 2.5, 0.0),
            NPCLANE_FLOAT_TOL);
    }

    // A bad distance or width draws nothing
    TestEqual(TEXT("NaN distance -> 0"), profile(NPCLANE_NAN, 2.5, 0.3), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("width 0 -> 0"), profile(0.0, 0.0, 0.3), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("NaN width -> 0"), profile(0.0, NPCLANE_NAN, 0.3), 0.0, NPCLANE_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathLineProfileScaledTest, "SOLTest.NpcLaneMath.LineProfileScaled",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 9 at 720p and 4K (D25): the glow's distance past the core is in reference pixels, so the profile is the 1080p
// shape stretched by height / 1080 (full at the centre, the same value at the same scaled distance), and it is under
// 0.01 at the ribbon's edge for every g the HUD glow setting reaches; a bad height reads as 1080
bool FSOLNpcLaneMathLineProfileScaledTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneLineProfile;
    using SOLNpcLane::LaneRibbonHalfExtentPx;
    const double coreHalf = SOL::Hologram::HUD_GLOW_CORE_HALF_PX;
    const double settingMaxG = 2.0 * static_cast<double>(SOL::Hologram::HUD_GLOW_DEFAULT_LEVEL);
    for (const double height : { 720.0, 2160.0 })
    {
        const double scale = NpcLaneScale(height);
        for (const double width : NPCLANE_WIDTHS)
        {
            const double halfCore = FMath::Max(width, 1.0) * 0.5 - coreHalf;
            const FString wLabel = FString::Printf(TEXT("%.0f, width %.1f"), height, width);
            for (const double g : { 0.0, 0.3, 0.6, 1.0 })
            {
                const FString label = wLabel + FString::Printf(TEXT(", g %.2f"), g);
                TestEqual(label + TEXT(": centre = min(width, 1)"), LaneLineProfile(0.0, width, g, height),
                    FMath::Min(width, 1.0), NPCLANE_FLOAT_TOL);

                // The closed form, and the 1080p value at the same distance in reference pixels
                for (const double past : { 0.0, 0.4, 1.0, 2.5, 6.0, 12.0 })
                {
                    const double d = halfCore + past * scale;
                    TestEqual(label + FString::Printf(TEXT(": closed form at %.2f px"), d),
                        LaneLineProfile(d, width, g, height), NpcLaneExpectedProfile(d, width, g, height),
                        NPCLANE_FLOAT_TOL);
                    TestEqual(label + FString::Printf(TEXT(": as 1080p at %.2f reference px"), past),
                        LaneLineProfile(d, width, g, height),
                        LaneLineProfile(halfCore + past, width, g, NPCLANE_REF_HEIGHT), NPCLANE_FLOAT_TOL);
                }
            }

            // The ribbon's edge never clips a visible glow
            const double edge = LaneRibbonHalfExtentPx(width, height);
            for (const double g : { 0.0, 0.15, 0.3, 0.45, settingMaxG })
            {
                TestTrue(wLabel + FString::Printf(TEXT(": under 0.01 at the ribbon edge (%.2f px), g %.2f"), edge, g),
                    LaneLineProfile(edge, width, g, height) < 0.01);
            }
        }
    }

    // 4K stretches the glow: 4 px past the core reads as 2 reference px there, brighter than 4 px at 1080p
    TestTrue(TEXT("4K glow is wider in pixels"), LaneLineProfile(0.5 + 4.0, 1.0, 0.6, 2160.0)
        > LaneLineProfile(0.5 + 4.0, 1.0, 0.6, NPCLANE_REF_HEIGHT));
    for (const double badHeight : { 0.0, -1080.0, NPCLANE_NAN })
    {
        TestEqual(FString::Printf(TEXT("height %f -> the reference"), badHeight),
            LaneLineProfile(3.0, 2.5, 0.3, badHeight), LaneLineProfile(3.0, 2.5, 0.3, NPCLANE_REF_HEIGHT),
            NPCLANE_FLOAT_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathFlowPhaseTest, "SOLTest.NpcLaneMath.DashFlowPhase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 10: the flow phase is frac(FLOW x t) in [0, 1), computed in double (a huge t stays exact), wrapping up for a
// negative t; NaN and infinities read 0
bool FSOLNpcLaneMathFlowPhaseTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneDashFlowPhase;
    const double flow = SOL::NPC_LANE_DASH_FLOW_PERIODS_PER_S;
    TestEqual(TEXT("t 0 -> 0"), LaneDashFlowPhase(0.0), 0.0, NPCLANE_TOL);
    const double onePeriod = LaneDashFlowPhase(1.0 / flow);
    TestTrue(TEXT("t 1 / FLOW: below 1"), onePeriod >= 0.0 && onePeriod < 1.0);
    TestTrue(TEXT("t 1 / FLOW: 0 modulo 1"), NpcLaneWrapDistance(onePeriod, 0.0) < NPCLANE_TOL);
    TestEqual(TEXT("t 0.5 / FLOW -> 0.5"), LaneDashFlowPhase(0.5 / flow), 0.5, NPCLANE_TOL);
    TestEqual(TEXT("t 0.25 / FLOW -> 0.25"), LaneDashFlowPhase(0.25 / flow), 0.25, NPCLANE_TOL);

    // A huge time stays in range and matches the closed form
    for (const double t : { 1.0e9, 1.0e9 + 0.3, 3.7e8 + 0.11 })
    {
        const double phase = LaneDashFlowPhase(t);
        const double scaled = flow * t;
        const double expected = scaled - FMath::FloorToDouble(scaled);
        TestTrue(FString::Printf(TEXT("t %.2f: in [0, 1)"), t), phase >= 0.0 && phase < 1.0);
        TestTrue(FString::Printf(TEXT("t %.2f: the closed form"), t), NpcLaneWrapDistance(phase, expected) < 1.0e-6);
    }

    // A negative time wraps up into [0, 1)
    TestEqual(TEXT("t -0.5 / FLOW -> 0.5"), LaneDashFlowPhase(-0.5 / flow), 0.5, NPCLANE_TOL);
    TestEqual(TEXT("t -0.2 -> 1 - 0.15"), LaneDashFlowPhase(-0.2), 1.0 - flow * 0.2, NPCLANE_TOL);
    for (const double t : { -1.0e-3, -7.3, -1.0e9 - 0.3 })
    {
        const double phase = LaneDashFlowPhase(t);
        TestTrue(FString::Printf(TEXT("t %.3f: in [0, 1)"), t), phase >= 0.0 && phase < 1.0);
    }

    // Always in [0, 1) over a sweep
    for (int32 step = -200; step <= 200; ++step)
    {
        const double t = static_cast<double>(step) * 0.173;
        const double phase = LaneDashFlowPhase(t);
        TestTrue(FString::Printf(TEXT("t %.3f in [0, 1)"), t), phase >= 0.0 && phase < 1.0);
    }

    // Bad times
    TestEqual(TEXT("NaN -> 0"), LaneDashFlowPhase(NPCLANE_NAN), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("+inf -> 0"), LaneDashFlowPhase(NPCLANE_INF), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("-inf -> 0"), LaneDashFlowPhase(-NPCLANE_INF), 0.0, NPCLANE_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathDashPhaseTest, "SOLTest.NpcLaneMath.DashPhase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 11 (D15 amendment D): the dash phase at a continuous level is (s - 0.5) x BASE x 2^level - sign(direction) x f
// (level clamped to [DASH_MIN_LEVEL, the orbit lines' max level], NaN as 0, direction 0 as +1): it pivots on the
// lane's middle, is continuous in the level, gives the old s x BASE x 2^k pattern at whole levels, and an outbound
// dash moves toward B and an inbound one toward A as f grows, at every level
bool FSOLNpcLaneMathDashPhaseTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneDashPhase;
    const double base = static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT);
    const int32 maxLevel = SOL::ORBIT_LINE_DASH_MAX_LEVEL;
    const int32 minLevel = SOL::NPC_LANE_DASH_MIN_LEVEL;

    // The closed form, whole and fractional levels
    TestEqual(TEXT("s 0.3, level 2, f 0.25, out"), LaneDashPhase(0.3, 2.0, 0.25, 1), -0.2 * base * 4.0 - 0.25,
        NPCLANE_TOL);
    TestEqual(TEXT("s 0.3, level 2, f 0.25, in"), LaneDashPhase(0.3, 2.0, 0.25, -1), -0.2 * base * 4.0 + 0.25,
        NPCLANE_TOL);
    TestEqual(TEXT("s 0.7, level 0, f 0"), LaneDashPhase(0.7, 0.0, 0.0, 1), 0.2 * base, NPCLANE_TOL);
    TestEqual(TEXT("s 1, top level"), LaneDashPhase(1.0, maxLevel, 0.0, 1), 0.5 * base * FMath::Pow(2.0, maxLevel),
        NPCLANE_TOL);
    TestEqual(TEXT("s 0.8, level 1.5, f 0.4"), LaneDashPhase(0.8, 1.5, 0.4, 1), 0.3 * base * FMath::Pow(2.0, 1.5) - 0.4,
        NPCLANE_TOL);
    TestEqual(TEXT("s 0.1, level -2.25, f 0.6, in"), LaneDashPhase(0.1, -2.25, 0.6, -1),
        -0.4 * base * FMath::Pow(2.0, -2.25) + 0.6, NPCLANE_TOL);

    // Level clamped to [NPC_LANE_DASH_MIN_LEVEL, ORBIT_LINE_DASH_MAX_LEVEL]; NaN as level 0
    TestEqual(TEXT("level -3: an eighth of the base dashes"), LaneDashPhase(0.41, -3.0, 0.2, 1),
        -0.09 * base / 8.0 - 0.2, NPCLANE_TOL);
    TestEqual(TEXT("s 1, coarsest level"), LaneDashPhase(1.0, minLevel, 0.0, 1),
        0.5 * base * FMath::Exp2(static_cast<double>(minLevel)), NPCLANE_TOL);
    TestEqual(TEXT("level below the min as the min"), LaneDashPhase(0.41, minLevel - 5.5, 0.2, 1),
        LaneDashPhase(0.41, minLevel, 0.2, 1), NPCLANE_TOL);
    TestEqual(TEXT("level 20 as the max"), LaneDashPhase(0.41, 20.0, 0.2, 1), LaneDashPhase(0.41, maxLevel, 0.2, 1),
        NPCLANE_TOL);
    TestEqual(TEXT("NaN level as 0"), LaneDashPhase(0.41, NPCLANE_NAN, 0.2, 1), LaneDashPhase(0.41, 0.0, 0.2, 1),
        NPCLANE_TOL);

    // Direction: its sign, 0 as +1
    TestEqual(TEXT("direction 0 as +1"), LaneDashPhase(0.41, 3.0, 0.6, 0), LaneDashPhase(0.41, 3.0, 0.6, 1),
        NPCLANE_TOL);
    TestEqual(TEXT("direction 5 as +1"), LaneDashPhase(0.41, 3.0, 0.6, 5), LaneDashPhase(0.41, 3.0, 0.6, 1),
        NPCLANE_TOL);
    TestEqual(TEXT("direction -7 as -1"), LaneDashPhase(0.41, 3.0, 0.6, -7), LaneDashPhase(0.41, 3.0, 0.6, -1),
        NPCLANE_TOL);

    // Whole levels give the stepped scheme's pattern s x BASE x 2^k: the two differ by a whole number of periods
    for (int32 level = minLevel; level <= maxLevel; ++level)
    {
        for (const double s : { 0.0213, 0.3779, 0.8041 })
        {
            const double oldPhase = s * base * FMath::Exp2(static_cast<double>(level)) - 0.3;
            const double offset = LaneDashPhase(s, level, 0.3, 1) - oldPhase;
            TestTrue(FString::Printf(TEXT("whole level %d, s %.4f: the old pattern (offset %.6f periods)"), level, s,
                offset), FMath::Abs(offset - FMath::RoundToDouble(offset)) < 1.0e-9);
        }
    }

    // The pivot: the lane's middle keeps the phase -direction x f at every level (it never slides)
    for (double level = minLevel - 1.0; level <= maxLevel + 1.0; level += 0.37)
    {
        TestEqual(FString::Printf(TEXT("level %.2f: the middle out"), level), LaneDashPhase(0.5, level, 0.3, 1), -0.3,
            NPCLANE_TOL);
        TestEqual(FString::Printf(TEXT("level %.2f: the middle in"), level), LaneDashPhase(0.5, level, 0.3, -1), 0.3,
            NPCLANE_TOL);
    }

    // Continuous in the level, across every whole level too (no step: the stepped scheme jumped there)
    for (int32 level = minLevel; level <= maxLevel; ++level)
    {
        for (const double s : { 0.0, 0.2, 0.93, 1.0 })
        {
            const double below = LaneDashPhase(s, static_cast<double>(level) - 1.0e-9, 0.3, 1);
            const double above = LaneDashPhase(s, static_cast<double>(level) + 1.0e-9, 0.3, 1);
            TestTrue(FString::Printf(TEXT("level %d, s %.2f: continuous (%.3e)"), level, s, FMath::Abs(above - below)),
                FMath::Abs(above - below) < 1.0e-4);
        }
    }

    // Motion: a dash feature moves toward B (outbound) or A (inbound) by df / (BASE 2^level) of the lane per df, at
    // whole and fractional levels alike
    const double motionLevels[] = { static_cast<double>(minLevel), -2.5, 0.0, 0.3, 3.7, static_cast<double>(maxLevel) };
    for (const double level : motionLevels)
    {
        const double perPhase = 1.0 / (base * FMath::Pow(2.0, level));
        for (const double s : { 0.1, 0.5, 0.83 })
        {
            const double f = 0.3;
            const double df = 0.17;
            const FString label = FString::Printf(TEXT("level %.2f, s %.2f"), level, s);
            TestEqual(label + TEXT(": outbound moves toward B"), LaneDashPhase(s + perPhase * df, level, f + df, 1),
                LaneDashPhase(s, level, f, 1), NPCLANE_TOL);
            TestEqual(label + TEXT(": inbound moves toward A"), LaneDashPhase(s - perPhase * df, level, f + df, -1),
                LaneDashPhase(s, level, f, -1), NPCLANE_TOL);
            TestEqual(label + TEXT(": both directions agree at f 0"), LaneDashPhase(s, level, 0.0, 1),
                LaneDashPhase(s, level, 0.0, -1), NPCLANE_TOL);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathDashCoverageTest, "SOLTest.NpcLaneMath.DashCoverage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 12 (D15 amendment D, user 2026-11-11: the "accordion"): one continuous dash scale for the whole lane from its
// lane-wide footprint, so the dash period is about 24 px on screen between the coarse floor (4 dashes) and the top
// level, and nothing pops: the coverage is continuous in the footprint (no step where the stepped scheme jumped), the
// mean brightness stays the duty, the whole levels reproduce the stepped scheme (64 base dashes at level 0, the
// orbit lines' coverage where their blend is 0, 4 dashes at the floor), the lane's middle never slides, one size is
// drawn (no dimmer copy), the box filter and the solid fade follow the pixel's own footprint, in [0, 1], a level-0
// square wave for a bad lane footprint, periodic in the flow, and the flow moves the pattern at every scale
bool FSOLNpcLaneMathDashCoverageTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcLane;
    const double base = static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT);
    const double minLevelScale = FMath::Exp2(static_cast<double>(SOL::NPC_LANE_DASH_MIN_LEVEL));
    const double minPeriodPx = SOL::ORBIT_LINE_DASH_MIN_PERIOD_PX;

    // The contract over 20 (s, footprint) pairs, from the coarsest level (a level-0 period under 2 px) through the
    // coarse levels to level 0, between whole levels and the top level to the solid fade and beyond, flowing too
    const double footprints[] = { 2.0, 0.5, 0.1, 0.05, 0.035, 0.02, 0.012, 5.0e-3, 2.0e-3, 8.0e-4, 3.0e-4, 1.2e-4,
        6.0e-5, 3.0e-5, 1.8e-5, 1.2e-5, 8.0e-6, 5.5e-6, 3.0e-6, 1.0e-6 };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(footprints)); ++index)
    {
        const double s = 0.013 + 0.049 * static_cast<double>(index);
        const double footprint = footprints[index];
        const FString label = FString::Printf(TEXT("s %.3f, footprint %.1e"), s, footprint);
        TestEqual(label + TEXT(": outbound = the accordion"), LaneDashCoverage(s, 0.0, 1, footprint),
            NpcLaneAccordionDash(s, 0.0, 1, footprint, footprint), NPCLANE_TOL);
        TestEqual(label + TEXT(": inbound = the accordion"), LaneDashCoverage(s, 0.0, -1, footprint),
            NpcLaneAccordionDash(s, 0.0, -1, footprint, footprint), NPCLANE_TOL);
        TestEqual(label + TEXT(": flowing = the accordion"), LaneDashCoverage(s, 0.41, 1, footprint),
            NpcLaneAccordionDash(s, 0.41, 1, footprint, footprint), NPCLANE_TOL);
    }

    // Perspective: the scale follows the lane's footprint only, the box filter and the solid fade the pixel's
    struct FSplitFootprint
    {
        double Lane;
        double Pixel;
    };
    const FSplitFootprint splits[] = { { 0.02, 0.005 }, { 0.02, 0.2 }, { 1.0e-3, 1.0e-5 }, { 0.5, 1.0e-4 },
        { 4.0e-3, 4.0e-3 } };
    for (const FSplitFootprint& split : splits)
    {
        for (const double s : { 0.0917, 0.3333, 0.6071 })
        {
            TestEqual(FString::Printf(TEXT("lane %.1e, pixel %.1e, s %.4f: = the accordion"), split.Lane, split.Pixel,
                s), LaneDashCoverage(s, 0.3, 1, split.Lane, split.Pixel),
                NpcLaneAccordionDash(s, 0.3, 1, split.Lane, split.Pixel), NPCLANE_TOL);
        }
    }
    const double laneLevelOneFootprint = 1.0 / (minPeriodPx * 2.0);
    for (const double s : { 0.0917, 0.3333, 0.6071 })
    {
        // A pixel whose own dash period is past the solid end (a lane near the camera) is solid; a far pixel whose
        // dashes are under a pixel box-filters to the duty
        TestEqual(FString::Printf(TEXT("s %.4f: near pixel, period 1000 px, solid"), s),
            LaneDashCoverage(s, 0.3, 1, laneLevelOneFootprint, 1.0 / (1000.0 * 2.0)), 1.0, NPCLANE_TOL);
        TestEqual(FString::Printf(TEXT("s %.4f: far pixel, period 0.5 px, the duty"), s),
            LaneDashCoverage(s, 0.3, 1, laneLevelOneFootprint, 1.0), SOL::NPC_LANE_DASH_DUTY, NPCLANE_TOL);
    }

    // Whole levels reproduce the stepped scheme where the orbit lines' blend is 0: level 0 and finer equal the orbit
    // lines' coverage of s x BASE, coarser ones that of s x BASE x 2^MIN with the footprint scaled alike; past the top
    // level (the solid fade and beyond) both equal the orbit lines' 64-dash line
    for (int32 level = SOL::NPC_LANE_DASH_MIN_LEVEL; level <= SOL::ORBIT_LINE_DASH_MAX_LEVEL; ++level)
    {
        const double footprint = (1.0 - 1.0e-12) / (minPeriodPx * FMath::Exp2(static_cast<double>(level)));
        for (const double s : { 0.0213, 0.3779, 0.8041 })
        {
            const FString label = FString::Printf(TEXT("whole level %d, s %.4f"), level, s);
            const double expected = level < 0 ? NpcLaneOrbitDash(s * base * minLevelScale, footprint * minLevelScale)
                : NpcLaneOrbitDash(s * base, footprint);
            TestEqual(label + TEXT(": outbound = orbit lines"), LaneDashCoverage(s, 0.0, 1, footprint), expected,
                NPCLANE_FLOAT_TOL);
            TestEqual(label + TEXT(": inbound = orbit lines"), LaneDashCoverage(s, 0.0, -1, footprint), expected,
                NPCLANE_FLOAT_TOL);
        }
    }
    for (const double footprint : { 1.0e-5, 3.0e-6, 1.0e-6 })
    {
        for (const double s : { 0.0213, 0.3779, 0.8041 })
        {
            TestEqual(FString::Printf(TEXT("past the top level, footprint %.1e, s %.4f: = orbit lines"), footprint, s),
                LaneDashCoverage(s, 0.0, 1, footprint), NpcLaneOrbitDash(s * base, footprint), NPCLANE_FLOAT_TOL);
        }
    }

    // The base and the floor: a lane 1,536 px on screen has the 64 base dashes (level 0), 2 and 4 times that length
    // 128 and 256; at 96 px and below (8 and 16 px) the floor's BASE x 2^MIN = 4
    const int32 coarsestDashes = FMath::RoundToInt32(base * minLevelScale);
    TestEqual(TEXT("lane 1536 px: the 64 base dashes"), NpcLaneDashCount(base * minPeriodPx, 0.0),
        static_cast<int32>(SOL::NPC_LANE_DASH_BASE_COUNT));
    TestEqual(TEXT("lane 3072 px: 128 dashes"), NpcLaneDashCount(2.0 * base * minPeriodPx, 0.0), 128);
    TestEqual(TEXT("lane 6144 px: 256 dashes"), NpcLaneDashCount(4.0 * base * minPeriodPx, 0.0), 256);
    for (const double lanePx : { 8.0, 16.0, 96.0 })
    {
        TestEqual(FString::Printf(TEXT("lane %.0f px: %d dashes"), lanePx, coarsestDashes),
            NpcLaneDashCount(lanePx, 0.0), coarsestDashes);
    }

    // The period stays the minimum on screen between the floor and the top: BASE x 2^level periods along a lane L px
    // long is L / MIN_PERIOD_PX, so the dash count grows smoothly with the lane's length (about one per 24 px)
    for (const double lanePx : { 100.0, 137.0, 250.0, 777.0, 1536.0, 2000.0, 9999.0, 1.0e5 })
    {
        const double periods = base * FMath::Exp2(LaneDashLevel(base / lanePx));
        TestEqual(FString::Printf(TEXT("lane %.0f px: a %.0f px period"), lanePx, minPeriodPx), lanePx / periods,
            minPeriodPx, 1.0e-9 * minPeriodPx);
        if (lanePx <= 9999.0)
        {
            const int32 count = NpcLaneDashCount(lanePx, 0.0);
            TestTrue(FString::Printf(TEXT("lane %.0f px: %d dashes, about L / 24 = %.2f"), lanePx, count, periods),
                FMath::Abs(static_cast<double>(count) - periods) <= 1.0);
        }
    }

    // A short lane still shows dashes: a lane L px long whose 64 base dashes would be under 2 px each and box-filter
    // to an even half-bright line has lit dashes and dark gaps along it
    for (const double lanePx : { 32.0, 64.0, 130.0, 400.0 })
    {
        const double footprint = base / lanePx;
        double lowest = 1.0;
        double highest = 0.0;
        for (int32 step = 0; step < 2000; ++step)
        {
            const double coverage = LaneDashCoverage(static_cast<double>(step) / 2000.0, 0.3, 1, footprint);
            lowest = FMath::Min(lowest, coverage);
            highest = FMath::Max(highest, coverage);
        }
        TestTrue(FString::Printf(TEXT("lane %.0f px: dashes visible (coverage %.3f-%.3f)"), lanePx, lowest, highest),
            highest - lowest >= 0.5);
    }

    // No step: across each of the stepped scheme's size steps (the on-screen period crossing MIN_PERIOD_PX x 2^k),
    // coarse and fine, the coverage changes by almost nothing, where the stepped scheme jumped by 0.5 or more
    for (int32 level = SOL::NPC_LANE_DASH_MIN_LEVEL + 1; level <= SOL::ORBIT_LINE_DASH_MAX_LEVEL; ++level)
    {
        const double boundary = 1.0 / (minPeriodPx * FMath::Exp2(static_cast<double>(level)));
        double largestChange = 0.0;
        for (int32 step = 0; step < 200; ++step)
        {
            const double s = (static_cast<double>(step) + 0.37) / 200.0;
            const double coarser = LaneDashCoverage(s, 0.3, 1, boundary * (1.0 + 1.0e-11));
            const double finer = LaneDashCoverage(s, 0.3, 1, boundary * (1.0 - 1.0e-11));
            largestChange = FMath::Max(largestChange, FMath::Abs(coarser - finer));
        }
        TestTrue(FString::Printf(TEXT("old step %d: no jump (largest change %.2e)"), level, largestChange),
            largestChange < 1.0e-4);
    }

    // Smooth with the footprint: a zoom in steps of 0.002% of the lane's length, from the floor to level 2, moves a
    // point's coverage only a little per step (the dashes slide, they never pop)
    for (const double s : { 0.05, 0.31, 0.77 })
    {
        double largestChange = 0.0;
        double lanePx = 96.0;
        double previous = LaneDashCoverage(s, 0.3, 1, base / lanePx);
        while (lanePx < base * minPeriodPx * 4.0)
        {
            lanePx *= 1.00002;
            const double coverage = LaneDashCoverage(s, 0.3, 1, base / lanePx);
            largestChange = FMath::Max(largestChange, FMath::Abs(coverage - previous));
            previous = coverage;
        }
        TestTrue(FString::Printf(TEXT("s %.2f: smooth zoom (largest step %.3f)"), s, largestChange),
            largestChange < 0.1);
    }

    // The lane's middle never slides: with no flow its coverage is the same at every lane length between the floor
    // and the top (the box filter is one pixel there at every scale)
    const double middleCoverage = LaneDashCoverage(0.5, 0.0, 1, base / 200.0);
    for (const double lanePx : { 100.0, 333.0, 1536.0, 5000.0, 4.0e4 })
    {
        TestEqual(FString::Printf(TEXT("lane %.0f px: the middle stays put"), lanePx),
            LaneDashCoverage(0.5, 0.0, 1, base / lanePx), middleCoverage, NPCLANE_FLOAT_TOL);
    }

    // The mean brightness along the lane stays the duty at every scale, across the old size steps too (within the
    // half period the lane's ends can cut off)
    for (const double lanePx : { 100.0, 150.0, 191.9, 192.1, 333.0, 767.9, 768.1, 1536.0, 2500.0, 5000.0 })
    {
        const double periods = base * FMath::Exp2(LaneDashLevel(base / lanePx));
        for (const double f : { 0.0, 0.3 })
        {
            const double mean = NpcLaneMeanCoverage(lanePx, f);
            TestTrue(FString::Printf(TEXT("lane %.1f px, f %.1f: mean %.4f = duty"), lanePx, f, mean),
                FMath::Abs(mean - SOL::NPC_LANE_DASH_DUTY) <= 0.5 / periods + 1.0e-9);
        }
    }

    // One size, no partial copy: half way between two old size steps, coarse and fine, only the dash edges are part
    // lit, where the orbit lines' blend (half way toward the finer size) leaves a dimmer copy part lit over much of
    // the line
    for (int32 level = SOL::NPC_LANE_DASH_MIN_LEVEL; level <= 3; ++level)
    {
        const double footprint = 1.0 / (minPeriodPx * FMath::Exp2(static_cast<double>(level) + 0.5));
        const double lanePartLit = NpcLanePartLitFraction([footprint](const double s)
        {
            return LaneDashCoverage(s, 0.3, 1, footprint);
        });
        TestTrue(FString::Printf(TEXT("level %d.5: only dash edges part lit (%.3f)"), level, lanePartLit),
            lanePartLit <= 0.1);
        if (level >= 0)
        {
            const double orbitPartLit = NpcLanePartLitFraction([footprint, base](const double s)
            {
                return NpcLaneOrbitDash(s * base, footprint);
            });
            TestTrue(FString::Printf(TEXT("level %d.5: the orbit lines' blend is part lit more (%.3f vs %.3f)"), level,
                orbitPartLit, lanePartLit), orbitPartLit >= 0.3);
        }
    }

    // Always in [0, 1]; a whole flow period changes nothing (footprints 1e-6 to 1e3: past the coarsest clamp too)
    for (int32 step = 0; step < 60; ++step)
    {
        const double s = static_cast<double>(step) / 59.0;
        const double f = FMath::Frac(0.37 * static_cast<double>(step));
        const double footprint = FMath::Pow(10.0, -6.0 + 9.0 * static_cast<double>(step) / 59.0);
        for (const int32 direction : { 1, -1 })
        {
            const FString label = FString::Printf(TEXT("step %d, dir %d"), step, direction);
            const double coverage = LaneDashCoverage(s, f, direction, footprint);
            TestTrue(label + TEXT(": in [0, 1]"), coverage >= 0.0 && coverage <= 1.0);
            TestEqual(label + TEXT(": a whole period of flow changes nothing"),
                LaneDashCoverage(s, f + 1.0, direction, footprint), coverage, NPCLANE_FLOAT_TOL);
        }
    }

    // A NaN or non-positive lane footprint: a level-0 point sample of the flowed phase (a square wave of duty
    // DASH_DUTY); a bad pixel footprint with a good lane one: a point sample at the lane's scale, no solid fade
    for (const double badFootprint : { 0.0, -1.0, NPCLANE_NAN })
    {
        for (const double s : { 0.0031, 0.2117, 0.5049, 0.7766 })
        {
            for (const int32 direction : { 1, -1 })
            {
                const double f = 0.29;
                const FString label = FString::Printf(TEXT("footprint %f, s %.4f, dir %d"), badFootprint, s,
                    direction);
                const double coverage = LaneDashCoverage(s, f, direction, badFootprint);
                TestEqual(label + TEXT(": the level-0 point sample"), coverage,
                    SOLOrbitLineMath::DashCoverage(LaneDashPhase(s, 0.0, f, direction), SOL::NPC_LANE_DASH_DUTY, 0.0),
                    NPCLANE_TOL);
                TestTrue(label + TEXT(": 0 or 1"), coverage == 0.0 || coverage == 1.0);
                const double laneFootprint = 1.0 / (minPeriodPx * FMath::Exp2(1.3));
                const double atLane = LaneDashCoverage(s, f, direction, laneFootprint, badFootprint);
                TestEqual(label + TEXT(": bad pixel footprint, the point sample at the lane's level"), atLane,
                    SOLOrbitLineMath::DashCoverage(LaneDashPhase(s, 1.3, f, direction), SOL::NPC_LANE_DASH_DUTY, 0.0),
                    NPCLANE_TOL);
            }
        }
    }

    // At every scale (the floor, between whole levels, level 0, the top level before the solid fade), shifting the
    // flow by delta equals moving s by delta / (BASE 2^level): the dashes flow by the same share of their period
    const double flowFootprints[] = { 2.0, 1.0 / (minPeriodPx * FMath::Exp2(-1.5)), (1.0 - 1.0e-12) / minPeriodPx,
        1.0 / (minPeriodPx * FMath::Exp2(2.7)), 5.0e-5 };
    for (const double footprint : flowFootprints)
    {
        const double perPhase = 1.0 / (base * FMath::Exp2(LaneDashLevel(footprint)));
        for (const double s : { 0.137, 0.4021, 0.866 })
        {
            for (const int32 direction : { 1, -1 })
            {
                const double delta = 0.23;
                const FString label = FString::Printf(TEXT("footprint %.2e, s %.4f, dir %d"), footprint, s, direction);
                const double movedS = s - static_cast<double>(direction) * delta * perPhase;
                TestEqual(label + TEXT(": flow = moved phase"), LaneDashCoverage(s, delta, direction, footprint),
                    LaneDashCoverage(movedS, 0.0, direction, footprint), NPCLANE_FLOAT_TOL);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathDashScreenSpacingTest, "SOLTest.NpcLaneMath.DashScreenSpacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 12b (D15 amendment D): the shader's lane-wide inputs. LaneDashLevel is the continuous clamped level of a lane
// footprint; LaneDashLaneFootprint is BASE / (the angle the lane subtends x the focal length); LaneScreenParam spaces
// the lane parameter by angle from A, so the dashes are evenly spaced on screen in perspective too
bool FSOLNpcLaneMathDashScreenSpacingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcLane;
    const double base = static_cast<double>(SOL::NPC_LANE_DASH_BASE_COUNT);
    const double minPeriodPx = SOL::ORBIT_LINE_DASH_MIN_PERIOD_PX;
    const double minLevel = static_cast<double>(SOL::NPC_LANE_DASH_MIN_LEVEL);
    const double maxLevel = static_cast<double>(SOL::ORBIT_LINE_DASH_MAX_LEVEL);

    // LaneDashLevel: log2 of the period over the minimum, continuous, clamped, 0 for a bad footprint
    TestEqual(TEXT("level 0 at a 24 px period"), LaneDashLevel(1.0 / minPeriodPx), 0.0, NPCLANE_TOL);
    for (const double level : { -3.5, -1.0, 0.25, 2.0, 5.75 })
    {
        TestEqual(FString::Printf(TEXT("level %.2f"), level), LaneDashLevel(1.0 / (minPeriodPx * FMath::Exp2(level))),
            level, NPCLANE_TOL);
    }
    TestEqual(TEXT("clamped at the floor"), LaneDashLevel(10.0), minLevel, NPCLANE_TOL);
    TestEqual(TEXT("clamped at the top"), LaneDashLevel(1.0e-9), maxLevel, NPCLANE_TOL);
    for (const double bad : { 0.0, -2.0, NPCLANE_NAN, NPCLANE_INF })
    {
        TestEqual(FString::Printf(TEXT("bad footprint %f -> 0"), bad), LaneDashLevel(bad), 0.0, NPCLANE_TOL);
    }
    double previousLevel = LaneDashLevel(100.0);
    for (int32 step = 1; step <= 400; ++step)
    {
        const double level = LaneDashLevel(100.0 * FMath::Pow(10.0, -10.0 * static_cast<double>(step) / 400.0));
        TestTrue(FString::Printf(TEXT("step %d: non-decreasing as the lane grows"), step), level >= previousLevel);
        previousLevel = level;
    }

    // LaneDashLaneFootprint: BASE / (angle x focal), scale free, at solar-system scale
    const double focalPx = 1662.8;
    const double d = 1.5 * NPCLANE_AU_M;
    TestEqual(TEXT("a right angle"), LaneDashLaneFootprint(FVector3d(d, 0.0, 0.0), FVector3d(0.0, d, 0.0), focalPx),
        base / (UE_DOUBLE_HALF_PI * focalPx), NPCLANE_TOL);
    const double h = 0.01 * d;
    const double smallAngle = 2.0 * FMath::Atan(h / d);
    TestEqual(TEXT("a symmetric small angle"), LaneDashLaneFootprint(FVector3d(d, -h, 0.0), FVector3d(d, h, 0.0),
        focalPx), base / (smallAngle * focalPx), 1.0e-9 * base / (smallAngle * focalPx));
    TestEqual(TEXT("scale free"), LaneDashLaneFootprint(FVector3d(d, -h, 3.0 * h) * 1.0e-6,
        FVector3d(d, h, -h) * 1.0e-6, focalPx), LaneDashLaneFootprint(FVector3d(d, -h, 3.0 * h), FVector3d(d, h, -h),
        focalPx), 1.0e-9);
    TestEqual(TEXT("behind the camera: the angle through the side"), LaneDashLaneFootprint(FVector3d(d, -h, 0.0),
        FVector3d(-d, -h, 0.0), focalPx), base / ((UE_DOUBLE_PI - smallAngle) * focalPx), 1.0e-9);
    TestEqual(TEXT("end-on (B behind A): 0"), LaneDashLaneFootprint(FVector3d(d, 0.0, 0.0), FVector3d(2.0 * d, 0.0, 0.0),
        focalPx), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("A on the camera: 0"), LaneDashLaneFootprint(FVector3d::ZeroVector, FVector3d(d, h, 0.0), focalPx),
        0.0, NPCLANE_TOL);
    TestEqual(TEXT("focal 0: 0"), LaneDashLaneFootprint(FVector3d(d, -h, 0.0), FVector3d(d, h, 0.0), 0.0), 0.0,
        NPCLANE_TOL);
    TestEqual(TEXT("NaN endpoint: 0"), LaneDashLaneFootprint(FVector3d(NPCLANE_NAN, 0.0, 0.0), FVector3d(d, h, 0.0),
        focalPx), 0.0, NPCLANE_TOL);

    // LaneScreenParam: 0 at A, 1 at B, clamped outside; a symmetric lane's middle is 0.5
    const FVector3d nearA(0.05 * NPCLANE_AU_M, -0.2 * NPCLANE_AU_M, 0.01 * NPCLANE_AU_M);
    const FVector3d farB(1.4 * NPCLANE_AU_M, 0.9 * NPCLANE_AU_M, -0.05 * NPCLANE_AU_M);
    TestEqual(TEXT("A -> 0"), LaneScreenParam(nearA, farB, 0.0), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("B -> 1"), LaneScreenParam(nearA, farB, 1.0), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("symmetric lane: middle -> 0.5"), LaneScreenParam(FVector3d(d, -h, 0.0), FVector3d(d, h, 0.0), 0.5),
        0.5, NPCLANE_TOL);
    TestEqual(TEXT("below 0 -> 0"), LaneScreenParam(nearA, farB, -0.3), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("above 1 -> 1"), LaneScreenParam(nearA, farB, 1.7), 1.0, NPCLANE_TOL);

    // The symmetric lane's closed form, and close to s itself for a small angle (no perspective)
    for (const double s : { 0.1, 0.27, 0.5, 0.66, 0.95 })
    {
        const double expected = 0.5 + FMath::Atan((2.0 * s - 1.0) * h / d) / smallAngle;
        TestEqual(FString::Printf(TEXT("symmetric lane, s %.2f: the closed form"), s),
            LaneScreenParam(FVector3d(d, -h, 0.0), FVector3d(d, h, 0.0), s), expected, 1.0e-9);
        TestEqual(FString::Printf(TEXT("symmetric lane, s %.2f: about s"), s),
            LaneScreenParam(FVector3d(d, -h, 0.0), FVector3d(d, h, 0.0), s), s, 1.0e-4);
    }

    // In perspective: increasing along the lane, and equal steps of it are equal angles: the point a quarter, half
    // and three quarters of the lane's angle from A has the screen parameter 0.25, 0.5 and 0.75
    double previous = LaneScreenParam(nearA, farB, 0.0);
    for (int32 step = 1; step <= 1000; ++step)
    {
        const double param = LaneScreenParam(nearA, farB, static_cast<double>(step) / 1000.0);
        TestTrue(FString::Printf(TEXT("step %d: increasing"), step), param > previous);
        previous = param;
    }
    const double totalAngle = NpcLaneAngle(nearA, farB);
    for (const double share : { 0.25, 0.5, 0.75 })
    {
        // Bisect for the lane parameter whose direction is that share of the whole angle from A
        double low = 0.0;
        double high = 1.0;
        for (int32 iteration = 0; iteration < 100; ++iteration)
        {
            const double mid = 0.5 * (low + high);
            const bool bShort = NpcLaneAngle(nearA, nearA + (farB - nearA) * mid) < share * totalAngle;
            low = bShort ? mid : low;
            high = bShort ? high : mid;
        }
        TestEqual(FString::Printf(TEXT("perspective: %.2f of the angle"), share),
            LaneScreenParam(nearA, farB, 0.5 * (low + high)), share, 1.0e-7);
    }

    // The near quarter of the lane in s covers more than a quarter of its angle on screen, so laying the dashes along
    // the screen parameter keeps the far ones from crowding and the near ones from stretching
    TestTrue(TEXT("perspective: the near quarter in s is more than a quarter on screen"),
        LaneScreenParam(nearA, farB, 0.25) > 0.25);

    // No angle: the lane parameter itself
    TestEqual(TEXT("end-on lane -> s"), LaneScreenParam(FVector3d(d, 0.0, 0.0), FVector3d(2.0 * d, 0.0, 0.0), 0.37),
        0.37, NPCLANE_TOL);
    TestEqual(TEXT("A on the camera -> s"), LaneScreenParam(FVector3d::ZeroVector, FVector3d(d, h, 0.0), 0.37), 0.37,
        NPCLANE_TOL);
    TestEqual(TEXT("NaN endpoint -> s"), LaneScreenParam(FVector3d(NPCLANE_NAN, 0.0, 0.0), FVector3d(d, h, 0.0), 0.37),
        0.37, NPCLANE_TOL);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathClosestParamTest, "SOLTest.NpcLaneMath.ClosestLaneParam",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 13: the lane parameter under a pixel is the point of segment A-B closest to the pixel's view ray (camera at
// the origin), clamped to [0, 1], exact at solar-system scale in double; bad input reads 0. The distance at a
// parameter is |A + s (B - A)| with s clamped
bool FSOLNpcLaneMathClosestParamTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcLane;
    const FVector3d a(1.0e9, -1.0e9, 0.0);
    const FVector3d b(1.0e9, 1.0e9, 0.0);

    // The contract's cases
    TestEqual(TEXT("ray (1, 0, 0) -> 0.5"), ClosestLaneParam(a, b, FVector3d(1.0, 0.0, 0.0)), 0.5, NPCLANE_TOL);
    TestEqual(TEXT("ray toward B -> 1"), ClosestLaneParam(a, b, b), 1.0, 1.0e-6);
    TestEqual(TEXT("ray toward B (unit) -> 1"), ClosestLaneParam(a, b, b.GetSafeNormal()), 1.0, 1.0e-6);
    TestEqual(TEXT("ray toward A -> 0"), ClosestLaneParam(a, b, a * 3.0), 0.0, 1.0e-6);
    TestEqual(TEXT("ray length does not matter"), ClosestLaneParam(a, b, FVector3d(7.0e3, 0.0, 0.0)), 0.5,
        NPCLANE_TOL);

    // A ray pointing away from the segment: its closest point is the origin, so the segment point nearest the origin
    const FVector3d longB(1.0e9, 3.0e9, 0.0);
    TestEqual(TEXT("ray away -> nearest to the origin"), ClosestLaneParam(a, longB, FVector3d(-1.0, 0.0, 0.0)), 0.25,
        NPCLANE_TOL);

    // A ray parallel to A-B: the segment point nearest the origin, clamped
    TestEqual(TEXT("parallel ray -> nearest to the origin"), ClosestLaneParam(a, longB, FVector3d(0.0, 1.0, 0.0)),
        0.25, NPCLANE_TOL);
    const FVector3d farA(1.0e9, 1.0e9, 0.0);
    TestEqual(TEXT("parallel ray, nearest point clamped to A"), ClosestLaneParam(farA, longB,
        FVector3d(0.0, 1.0, 0.0)), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("anti-parallel ray, nearest point clamped to A"), ClosestLaneParam(farA, longB,
        FVector3d(0.0, -1.0, 0.0)), 0.0, NPCLANE_TOL);

    // Degenerate and bad input
    TestEqual(TEXT("A == B -> 0"), ClosestLaneParam(a, a, FVector3d(1.0, 0.0, 0.0)), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("zero ray -> 0"), ClosestLaneParam(a, b, FVector3d::ZeroVector), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("NaN ray -> 0"), ClosestLaneParam(a, b, FVector3d(NPCLANE_NAN, 0.0, 0.0)), 0.0, NPCLANE_TOL);
    TestEqual(TEXT("NaN A -> 0"), ClosestLaneParam(FVector3d(NPCLANE_NAN, 0.0, 0.0), b, FVector3d(1.0, 0.0, 0.0)),
        0.0, NPCLANE_TOL);
    TestEqual(TEXT("NaN B -> 0"), ClosestLaneParam(a, FVector3d(0.0, NPCLANE_NAN, 0.0), FVector3d(1.0, 0.0, 0.0)),
        0.0, NPCLANE_TOL);

    // True solar-system scale: a ray through a known lane point gives its parameter to 1e-9
    const FVector3d auA(NPCLANE_AU_M, 2.0e9, 3.0e8);
    const FVector3d auB(-0.4 * NPCLANE_AU_M, 1.5 * NPCLANE_AU_M, -1.0e9);
    for (const double s0 : { 0.0, 0.003, 0.37, 0.5, 0.91, 1.0 })
    {
        const FVector3d point = auA + (auB - auA) * s0;
        TestEqual(FString::Printf(TEXT("1 AU lane: s %.3f"), s0), ClosestLaneParam(auA, auB, point), s0, NPCLANE_TOL);
    }

    // The result minimizes the segment-to-ray distance (checked against a fine sampling)
    const FVector3d rays[] = { FVector3d(0.9, 0.3, 0.05), FVector3d(0.2, 1.0, -0.1), FVector3d(1.0, -0.6, 0.4) };
    for (const FVector3d& ray : rays)
    {
        const FVector3d unit = ray.GetSafeNormal();
        const double s = ClosestLaneParam(auA, auB, ray);
        TestTrue(FString::Printf(TEXT("ray %s: s in [0, 1]"), *ray.ToString()), s >= 0.0 && s <= 1.0);
        const double best = NpcLanePointToRayM(auA + (auB - auA) * s, unit);
        double sampledMin = TNumericLimits<double>::Max();
        for (int32 step = 0; step <= 4000; ++step)
        {
            const double t = static_cast<double>(step) / 4000.0;
            sampledMin = FMath::Min(sampledMin, NpcLanePointToRayM(auA + (auB - auA) * t, unit));
        }
        TestTrue(FString::Printf(TEXT("ray %s: no sampled point closer"), *ray.ToString()),
            best <= sampledMin * (1.0 + 1.0e-9) + 1.0);
    }

    // A lane wholly behind the camera: the ray's closest point is its origin, so the segment point nearest the camera
    const FVector3d behindA(-3.0e9, 1.0e9, -1.0e9);
    const FVector3d behindB(-1.0e9, 1.0e9, 1.0e9);
    TestEqual(TEXT("lane behind the camera -> its point nearest the camera (B)"),
        ClosestLaneParam(behindA, behindB, FVector3d(1.0, 0.0, 0.0)), 1.0, NPCLANE_TOL);

    // A lane crossing from in front of the camera to behind it: the closest point is where it crosses the plane
    // across the ray (x = 0), nearer than any point in front or behind
    const FVector3d crossA(2.0e9, 1.0e9, 1.0e9);
    const FVector3d crossB(-2.0e9, 1.0e9, -1.0e9);
    TestEqual(TEXT("lane crossing behind the camera -> 0.5"), ClosestLaneParam(crossA, crossB,
        FVector3d(1.0, 0.0, 0.0)), 0.5, NPCLANE_TOL);

    // Both against a fine sampling of the segment
    struct FBehindCase
    {
        FVector3d A;
        FVector3d B;
        FVector3d Ray;
    };
    const FBehindCase behindCases[] = {
        { behindA, behindB, FVector3d(1.0, 0.0, 0.0) },
        { crossA, crossB, FVector3d(1.0, 0.0, 0.0) },
        { crossA, crossB, FVector3d(0.7, 0.2, -0.1) },
        { behindA, behindB, FVector3d(-0.2, 1.0, 0.3) },
    };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(behindCases)); ++index)
    {
        const FBehindCase& entry = behindCases[index];
        const FVector3d unit = entry.Ray.GetSafeNormal();
        const double s = ClosestLaneParam(entry.A, entry.B, entry.Ray);
        const double best = NpcLanePointToRayM(entry.A + (entry.B - entry.A) * s, unit);
        double sampledMin = TNumericLimits<double>::Max();
        for (int32 step = 0; step <= 4000; ++step)
        {
            const double t = static_cast<double>(step) / 4000.0;
            sampledMin = FMath::Min(sampledMin, NpcLanePointToRayM(entry.A + (entry.B - entry.A) * t, unit));
        }
        TestTrue(FString::Printf(TEXT("behind case %d: no sampled point closer"), index),
            best <= sampledMin * (1.0 + 1.0e-9) + 1.0);
    }

    // The distance at a parameter, s clamped
    TestEqual(TEXT("distance at 0 = |A|"), LaneDistanceAtParamM(auA, auB, 0.0), auA.Size(), 1.0e-3);
    TestEqual(TEXT("distance at 1 = |B|"), LaneDistanceAtParamM(auA, auB, 1.0), auB.Size(), 1.0e-3);
    TestEqual(TEXT("distance at 0.5"), LaneDistanceAtParamM(auA, auB, 0.5), ((auA + auB) * 0.5).Size(), 1.0e-3);
    TestEqual(TEXT("distance at -1 as 0"), LaneDistanceAtParamM(auA, auB, -1.0), auA.Size(), 1.0e-3);
    TestEqual(TEXT("distance at 2 as 1"), LaneDistanceAtParamM(auA, auB, 2.0), auB.Size(), 1.0e-3);
    TestEqual(TEXT("distance at 0.5 on the small lane"), LaneDistanceAtParamM(a, b, 0.5), 1.0e9, 1.0e-6);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathSideNormalTest, "SOLTest.NpcLaneMath.OutboundSideNormal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 14: "keep right" on screen (x right, y down): the outbound line's unit normal lies on the right of the travel
// direction A -> B; the inbound line uses the negated normal; A == B or NaN gives (0, 0)
bool FSOLNpcLaneMathSideNormalTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneOutboundSideNormal;
    const FVector2D origin(400.0, 300.0);

    // The contract's directions, and the fourth one
    struct FCase
    {
        FVector2D Direction;
        FVector2D Expected;
    };
    const FCase cases[] = {
        { FVector2D(1.0, 0.0), FVector2D(0.0, 1.0) },
        { FVector2D(0.0, -1.0), FVector2D(1.0, 0.0) },
        { FVector2D(-1.0, 0.0), FVector2D(0.0, -1.0) },
        { FVector2D(0.0, 1.0), FVector2D(-1.0, 0.0) },
    };
    for (const FCase& entry : cases)
    {
        for (const double length : { 1.0, 37.5, 2.0e4 })
        {
            const FVector2D normal = LaneOutboundSideNormal(origin, origin + entry.Direction * length);
            const FString label = FString::Printf(TEXT("A -> B %s x %.1f"), *entry.Direction.ToString(), length);
            TestEqual(label + TEXT(": x"), normal.X, entry.Expected.X, NPCLANE_TOL);
            TestEqual(label + TEXT(": y"), normal.Y, entry.Expected.Y, NPCLANE_TOL);
        }
    }

    // A diagonal: unit length, perpendicular, and the inbound line on the other side
    const FVector2D screenA(100.0, 200.0);
    const FVector2D screenB(700.0, -150.0);
    const FVector2D outbound = LaneOutboundSideNormal(screenA, screenB);
    TestEqual(TEXT("diagonal: unit"), outbound.Size(), 1.0, NPCLANE_TOL);
    TestEqual(TEXT("diagonal: perpendicular"), FVector2D::DotProduct(outbound, (screenB - screenA).GetSafeNormal()),
        0.0, NPCLANE_TOL);
    TestTrue(TEXT("diagonal: on the right (y down)"), FVector2D::CrossProduct(screenB - screenA, outbound) > 0.0);
    const FVector2D inbound = LaneOutboundSideNormal(screenB, screenA);
    TestEqual(TEXT("inbound = -outbound: x"), inbound.X, -outbound.X, NPCLANE_TOL);
    TestEqual(TEXT("inbound = -outbound: y"), inbound.Y, -outbound.Y, NPCLANE_TOL);

    // Degenerate and bad input
    const FVector2D same = LaneOutboundSideNormal(screenA, screenA);
    TestTrue(TEXT("A == B -> (0, 0)"), same.X == 0.0 && same.Y == 0.0);
    const FVector2D nanA = LaneOutboundSideNormal(FVector2D(NPCLANE_NAN, 0.0), screenB);
    TestTrue(TEXT("NaN A -> (0, 0)"), nanA.X == 0.0 && nanA.Y == 0.0);
    const FVector2D nanB = LaneOutboundSideNormal(screenA, FVector2D(0.0, NPCLANE_NAN));
    TestTrue(TEXT("NaN B -> (0, 0)"), nanB.X == 0.0 && nanB.Y == 0.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathCountShipsTest, "SOLTest.NpcLaneMath.CountLaneShips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 15: the lane histogram counts lane rails per group below laneCount; cluster and battle rails (whatever their
// group) and lane rails of an out-of-range group are ignored; the output is reset to laneCount zeros first
bool FSOLNpcLaneMathCountShipsTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::CountLaneShips;
    using SOLNpcScale::EPlacementKind;

    // Three lanes (groups 0-2): 4, 0 and 2 lane ships, plus clusters and battles in groups the lanes also use, and lane
    // rails of groups 3 and 9 (out of range)
    TArray<FSOLNpcRail> rails;
    for (int32 index = 0; index < 4; ++index)
    {
        rails.Add(NpcLaneRail(EPlacementKind::Lane, 0));
    }
    rails.Add(NpcLaneRail(EPlacementKind::Lane, 2));
    rails.Add(NpcLaneRail(EPlacementKind::Cluster, 1));
    rails.Add(NpcLaneRail(EPlacementKind::Cluster, 3));
    rails.Add(NpcLaneRail(EPlacementKind::Battle, 0));
    rails.Add(NpcLaneRail(EPlacementKind::Battle, 4));
    rails.Add(NpcLaneRail(EPlacementKind::Lane, 2));
    rails.Add(NpcLaneRail(EPlacementKind::Lane, 3));
    rails.Add(NpcLaneRail(EPlacementKind::Lane, 9));

    // Stale content is replaced by exactly laneCount counts
    TArray<int32> counts = { 77, 88, 99, 11, 22 };
    CountLaneShips(rails, 3, counts);
    if (TestEqual(TEXT("three counts"), counts.Num(), 3))
    {
        TestEqual(TEXT("lane 0: 4"), counts[0], 4);
        TestEqual(TEXT("lane 1: 0 (the cluster in group 1 ignored)"), counts[1], 0);
        TestEqual(TEXT("lane 2: 2"), counts[2], 2);
    }

    // A fresh call over the same rails gives the same result
    TArray<int32> again;
    CountLaneShips(rails, 3, again);
    TestTrue(TEXT("same result twice"), again == counts);
    CountLaneShips(rails, 3, again);
    TestTrue(TEXT("same result on a reused array"), again == counts);

    // A larger lane count takes in the group 3 lane rail
    TArray<int32> four;
    CountLaneShips(rails, 4, four);
    if (TestEqual(TEXT("four counts"), four.Num(), 4))
    {
        TestEqual(TEXT("lane 3: 1"), four[3], 1);
        TestEqual(TEXT("lane 0 still 4"), four[0], 4);
    }

    // No lanes: no counts
    TArray<int32> none = { 5, 6 };
    CountLaneShips(rails, 0, none);
    TestEqual(TEXT("laneCount 0: empty"), none.Num(), 0);
    none = { 5, 6 };
    CountLaneShips(rails, -2, none);
    TestEqual(TEXT("laneCount -2: empty"), none.Num(), 0);

    // No rails: all zeros
    TArray<int32> empty = { 3 };
    CountLaneShips(TConstArrayView<FSOLNpcRail>(), 8, empty);
    if (TestEqual(TEXT("no rails: eight counts"), empty.Num(), 8))
    {
        for (int32 lane = 0; lane < 8; ++lane)
        {
            TestEqual(FString::Printf(TEXT("no rails: lane %d is 0"), lane), empty[lane], 0);
        }
    }

    // Only cluster and battle rails: all zeros
    TArray<FSOLNpcRail> others = { NpcLaneRail(EPlacementKind::Cluster, 0), NpcLaneRail(EPlacementKind::Battle, 1) };
    TArray<int32> otherCounts;
    CountLaneShips(others, 2, otherCounts);
    TestTrue(TEXT("no lane rails: zeros"), otherCounts.Num() == 2 && otherCounts[0] == 0 && otherCounts[1] == 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathLaneFrameTest, "SOLTest.NpcLaneMath.BuildLaneFrame",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 16: the per-frame lane frame (the #49 extension point) has one entry per lane with its endpoints, radius,
// count, weight and drawn flag (ships and both bodies resolved); a lane past the end of the counts or of the frame
// arrays is not drawn (count 0); a second build with the same sizes does not reallocate
bool FSOLNpcLaneMathLaneFrameTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLNpcLane;

    // Five lanes and one cluster: lane 0 busy, lane 1 empty, lane 2 with an unknown body A, lane 3 with an unknown
    // body B, lane 4 past the end of the counts
    FSOLNpcGroupTable groups;
    groups.LaneCount = 5;
    groups.ClusterCount = 1;
    groups.LaneBodyA = { 1, 2, INDEX_NONE, 4, 5 };
    groups.LaneBodyB = { 2, 3, 4, INDEX_NONE, 6 };
    groups.LaneRadiusM = { 2.0e5, 1.0e7, 5.0e6, 3.0e6, 1.0e7 };
    groups.ClusterBody = { 3 };
    groups.ClusterInnerM = { 1.0e7 };
    groups.ClusterOuterM = { 4.0e7 };

    FSOLNpcRailFrame frame;
    frame.RailTimeS = 12.5;
    for (int32 group = 0; group < groups.Num(); ++group)
    {
        const double offset = static_cast<double>(group + 1);
        frame.GroupPointAM.Add(FVector3d(offset * 1.0e10, -offset * 3.0e9, offset * 1.0e8));
        frame.GroupPointBM.Add(FVector3d(-offset * 2.0e10, offset * 5.0e9, -offset * 2.0e8));
        frame.GroupBodyM.Add(frame.GroupPointAM.Last());
    }
    const TArray<int32> counts = { 44000, 0, 900, 1000, 777 };
    const TConstArrayView<int32> fourCounts(counts.GetData(), 4);

    FSOLNpcLaneFrame laneFrame;
    BuildLaneFrame(groups, frame, fourCounts, laneFrame);
    if (!TestEqual(TEXT("one entry per lane"), laneFrame.Lanes.Num(), groups.LaneCount))
    {
        return false;
    }
    for (int32 lane = 0; lane < groups.LaneCount; ++lane)
    {
        const FSOLNpcLaneEntry& entry = laneFrame.Lanes[lane];
        const FString label = FString::Printf(TEXT("lane %d"), lane);
        TestTrue(label + TEXT(": A from the frame"), entry.A == frame.GroupPointAM[lane]);
        TestTrue(label + TEXT(": B from the frame"), entry.B == frame.GroupPointBM[lane]);
        TestEqual(label + TEXT(": radius from the groups"), entry.RadiusM, groups.LaneRadiusM[lane], NPCLANE_TOL);
        const int32 count = lane < fourCounts.Num() ? fourCounts[lane] : 0;
        TestEqual(label + TEXT(": ship count"), entry.ShipCount, count);
        TestEqual(label + TEXT(": traffic weight"), entry.TrafficWeight, LaneTrafficWeight(count), NPCLANE_TOL);
        const bool bBodies = groups.LaneBodyA[lane] != INDEX_NONE && groups.LaneBodyB[lane] != INDEX_NONE;
        TestEqual(label + TEXT(": drawn"), entry.bDrawn, IsLaneDrawn(count) && bBodies);
    }
    TestTrue(TEXT("lane 0 drawn"), laneFrame.Lanes[0].bDrawn);
    TestFalse(TEXT("lane 1 (no ships) not drawn"), laneFrame.Lanes[1].bDrawn);
    TestFalse(TEXT("lane 2 (body A unknown) not drawn"), laneFrame.Lanes[2].bDrawn);
    TestFalse(TEXT("lane 3 (body B unknown) not drawn"), laneFrame.Lanes[3].bDrawn);
    TestFalse(TEXT("lane 4 (past the counts) not drawn"), laneFrame.Lanes[4].bDrawn);
    TestEqual(TEXT("lane 4 count 0"), laneFrame.Lanes[4].ShipCount, 0);

    // A second build with the same sizes keeps the array (no allocation per frame), with the new values
    const FSOLNpcLaneEntry* dataBefore = laneFrame.Lanes.GetData();
    frame.GroupPointAM[0] = FVector3d(4.0e10, 1.0e9, 0.0);
    BuildLaneFrame(groups, frame, fourCounts, laneFrame);
    TestTrue(TEXT("second build: same array data"), laneFrame.Lanes.GetData() == dataBefore);
    TestEqual(TEXT("second build: same count"), laneFrame.Lanes.Num(), groups.LaneCount);
    TestTrue(TEXT("second build: A follows the body"), laneFrame.Lanes[0].A == frame.GroupPointAM[0]);

    // All five counts: lane 4 drawn
    BuildLaneFrame(groups, frame, counts, laneFrame);
    TestTrue(TEXT("five counts: lane 4 drawn"), laneFrame.Lanes[4].bDrawn);
    TestEqual(TEXT("five counts: lane 4 count"), laneFrame.Lanes[4].ShipCount, 777);

    // Frame arrays shorter than the lanes: the lanes past their end are not drawn (count 0)
    FSOLNpcRailFrame shortFrame = frame;
    shortFrame.GroupPointAM.SetNum(3);
    shortFrame.GroupPointBM.SetNum(3);
    shortFrame.GroupBodyM.SetNum(3);
    FSOLNpcLaneFrame shortLanes;
    BuildLaneFrame(groups, shortFrame, counts, shortLanes);
    if (TestEqual(TEXT("short frame: one entry per lane"), shortLanes.Lanes.Num(), groups.LaneCount))
    {
        TestTrue(TEXT("short frame: lane 0 drawn"), shortLanes.Lanes[0].bDrawn);
        for (const int32 lane : { 3, 4 })
        {
            TestFalse(FString::Printf(TEXT("short frame: lane %d not drawn"), lane), shortLanes.Lanes[lane].bDrawn);
            TestEqual(FString::Printf(TEXT("short frame: lane %d count 0"), lane), shortLanes.Lanes[lane].ShipCount,
                0);
        }
    }

    // No lanes: no entries
    FSOLNpcGroupTable noLanes;
    FSOLNpcLaneFrame emptyFrame;
    BuildLaneFrame(noLanes, frame, counts, emptyFrame);
    TestEqual(TEXT("no lanes: no entries"), emptyFrame.Lanes.Num(), 0);
    return true;
}

// Part 38b helpers (names unique across the test files)
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a lane frame entry with a radius and a ship count, its weight and drawn flag as BuildLaneFrame sets them
    FSOLNpcLaneEntry NpcLaneFeedEntry(const double radiusM, const int32 shipCount, const bool bBodiesResolved = true)
    {
        FSOLNpcLaneEntry entry;
        entry.RadiusM = radiusM;
        entry.ShipCount = shipCount;
        entry.TrafficWeight = SOLNpcLane::LaneTrafficWeight(shipCount);
        entry.bDrawn = SOLNpcLane::IsLaneDrawn(shipCount) && bBodiesResolved;
        return entry;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a deterministic mixed rail set: lane rails over groups 0-9 (8 lanes and two out of range), clusters and
    // battles, in a hashed order
    TArray<FSOLNpcRail> NpcLaneFeedRails(const int32 count)
    {
        TArray<FSOLNpcRail> rails;
        rails.Reserve(count);
        for (int32 index = 0; index < count; ++index)
        {
            const uint32 hash = static_cast<uint32>(index + 1) * 2654435761u;
            const uint32 kindPick = (hash >> 7) % 5u;                  // 0-2 lane, 3 cluster, 4 battle
            const int32 group = static_cast<int32>((hash >> 13) % 10u);
            const SOLNpcScale::EPlacementKind kind = kindPick < 3u ? SOLNpcScale::EPlacementKind::Lane
                : (kindPick == 3u ? SOLNpcScale::EPlacementKind::Cluster : SOLNpcScale::EPlacementKind::Battle);
            rails.Add(NpcLaneRail(kind, group));
        }
        return rails;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns frac(x) in [0, 1) (a negative x wraps up)
    double NpcLaneFeedFrac(const double x)
    {
        return x - FMath::FloorToDouble(x);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathLanesShownTest, "SOLTest.NpcLaneMath.LanesShown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 25 (38b; D10, D11, D20, section 4.8): AreLanesShown(bHasSettings, bRowShown, bCvarOn). The lanes are shown
// while sol.NpcLanes is on and the Lanes row is shown (IsOverlayShown already folds in the master O); without a
// settings subsystem they are on; sol.NpcLanes 0 hides them whatever the row says (and so restores the points)
bool FSOLNpcLaneMathLanesShownTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::AreLanesShown;

    // The cvar off wins over everything
    for (const bool bHasSettings : { false, true })
    {
        for (const bool bRowShown : { false, true })
        {
            TestFalse(FString::Printf(TEXT("cvar off (settings %d, row %d) -> hidden"),
                static_cast<int32>(bHasSettings), static_cast<int32>(bRowShown)),
                AreLanesShown(bHasSettings, bRowShown, false));
        }
    }

    // With settings the row decides
    TestTrue(TEXT("settings, row shown -> shown"), AreLanesShown(true, true, true));
    TestFalse(TEXT("settings, row hidden (unchecked or master off) -> hidden"), AreLanesShown(true, false, true));

    // Without settings the lanes are on
    TestTrue(TEXT("no settings -> shown (row false)"), AreLanesShown(false, false, true));
    TestTrue(TEXT("no settings -> shown (row true)"), AreLanesShown(false, true, true));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathDrawLaneLinesTest, "SOLTest.NpcLaneMath.ShouldDrawLaneLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 26 (38b, section 4.8): ShouldDrawLaneLines(bLanesShown, frame), the lane component's visibility, is true only
// while the lanes are shown and at least one lane of the frame is drawn; with no population (an empty frame) nothing
// is drawn
bool FSOLNpcLaneMathDrawLaneLinesTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::ShouldDrawLaneLines;

    const FSOLNpcLaneFrame empty{};
    TestFalse(TEXT("no population: hidden"), ShouldDrawLaneLines(true, empty));

    // Lanes exist but none is drawn: one with no ships, one with an unresolved body
    FSOLNpcLaneFrame noneDrawn;
    noneDrawn.Lanes = { NpcLaneFeedEntry(2.0e5, 0), NpcLaneFeedEntry(1.0e7, 5000, false) };
    TestFalse(TEXT("precondition: lane 0 not drawn"), noneDrawn.Lanes[0].bDrawn);
    TestFalse(TEXT("precondition: lane 1 not drawn"), noneDrawn.Lanes[1].bDrawn);
    TestFalse(TEXT("no drawn lane: hidden"), ShouldDrawLaneLines(true, noneDrawn));

    // One drawn lane (a single ship is enough) makes the component visible while the lanes are shown
    FSOLNpcLaneFrame oneDrawn = noneDrawn;
    oneDrawn.Lanes.Add(NpcLaneFeedEntry(1.0e7, 1));
    TestTrue(TEXT("one drawn lane, shown: visible"), ShouldDrawLaneLines(true, oneDrawn));
    TestFalse(TEXT("one drawn lane, lanes hidden: hidden"), ShouldDrawLaneLines(false, oneDrawn));
    TestFalse(TEXT("empty, lanes hidden: hidden"), ShouldDrawLaneLines(false, empty));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathPointParamsTest, "SOLTest.NpcLaneMath.PointLaneParams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 27 (38b; D9, D10, section 4.8): PointLaneParams(bLanesShown) is the far-point material's NpcParams4 = (lanes
// shown 0 / 1, fade near radii, fade far radii, 0), the inputs of the kind-1 crossfade LaneShipPointAlpha
bool FSOLNpcLaneMathPointParamsTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::PointLaneParams;
    const float nearRadii = static_cast<float>(SOL::NPC_LANE_FADE_NEAR_RADII);
    const float farRadii = static_cast<float>(SOL::NPC_LANE_FADE_FAR_RADII);

    const FLinearColor shown = PointLaneParams(true);
    TestTrue(TEXT("shown: x = 1"), shown.R == 1.0f);
    TestTrue(TEXT("shown: y = fade near radii"), shown.G == nearRadii);
    TestTrue(TEXT("shown: z = fade far radii"), shown.B == farRadii);
    TestTrue(TEXT("shown: w = 0"), shown.A == 0.0f);

    const FLinearColor hidden = PointLaneParams(false);
    TestTrue(TEXT("hidden: x = 0 (every lane ship point as before)"), hidden.R == 0.0f);
    TestTrue(TEXT("hidden: y = fade near radii"), hidden.G == nearRadii);
    TestTrue(TEXT("hidden: z = fade far radii"), hidden.B == farRadii);
    TestTrue(TEXT("hidden: w = 0"), hidden.A == 0.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathLaneParamsTest, "SOLTest.NpcLaneMath.LaneMaterialParams",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 28 (38b; D15, section 4.8): LaneMaterialParams(flowPhase) is the lane material's NpcLaneParams0 = (flow phase
// f, 0 unused, fade near radii, fade far radii); a non-finite flow phase reads as 0. The y slot once carried a viewport
// scale the shader never read (it scales by its own view height, section 4.12), so it is pinned at 0
bool FSOLNpcLaneMathLaneParamsTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneMaterialParams;
    const float nearRadii = static_cast<float>(SOL::NPC_LANE_FADE_NEAR_RADII);
    const float farRadii = static_cast<float>(SOL::NPC_LANE_FADE_FAR_RADII);

    // The flow phase passes through, y is unused (0), the fade band is the constants'
    for (const double phase : { 0.0, 0.25, 0.5, 0.999 })
    {
        const FLinearColor params = LaneMaterialParams(phase);
        const FString label = FString::Printf(TEXT("f %.3f"), phase);
        TestEqual(label + TEXT(": x = f"), params.R, static_cast<float>(phase), 1.0e-7f);
        TestTrue(label + TEXT(": y unused, 0"), params.G == 0.0f);
        TestTrue(label + TEXT(": z = fade near radii"), params.B == nearRadii);
        TestTrue(label + TEXT(": w = fade far radii"), params.A == farRadii);
    }

    // A non-finite phase is 0, never NaN in the material
    for (const double badPhase : { NPCLANE_NAN, NPCLANE_INF, -NPCLANE_INF })
    {
        TestTrue(FString::Printf(TEXT("bad phase %f -> 0"), badPhase),
            LaneMaterialParams(badPhase).R == 0.0f);
    }

    // Fed from the flow phase, x stays in [0, 1) at any time
    for (const double t : { 0.0, 1.7, 1.0e6 + 0.3, 1.0e9 })
    {
        const float x = LaneMaterialParams(SOLNpcLane::LaneDashFlowPhase(t)).R;
        TestTrue(FString::Printf(TEXT("t %.1f: x in [0, 1]"), t), x >= 0.0f && x <= 1.0f);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathGroupTexelWTest, "SOLTest.NpcLaneMath.GroupTexelW",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 29 (38b, section 4.2): WriteLaneGroupTexelW(frame, groupTexels) writes the lane data into the free .w channels
// of the group texture (row 0 = texels [0, NPC_GROUP_TEXTURE_WIDTH), row 1 = the next NPC_GROUP_TEXTURE_WIDTH): for a
// lane column, row 0 .w = the lane radius (m) and row 1 .w = its traffic weight, or -1 when the lane is not drawn;
// every other column's .w = 0; x, y and z are never touched; a buffer shorter than two rows is left alone
bool FSOLNpcLaneMathGroupTexelWTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::WriteLaneGroupTexelW;
    constexpr int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;

    // Stale .w values and distinct x, y, z in every texel
    TArray<FVector4f> texels;
    texels.SetNum(2 * width);
    for (int32 index = 0; index < texels.Num(); ++index)
    {
        const float value = static_cast<float>(index);
        texels[index] = FVector4f(value + 0.25f, -value - 0.5f, value * 3.0f, 99.0f);
    }
    const TArray<FVector4f> before = texels;

    // Lane 0 busy, lane 1 empty, lane 2 drawn at weight 0 (50 ships), lane 3 busy with an unresolved body
    FSOLNpcLaneFrame frame;
    frame.Lanes = { NpcLaneFeedEntry(2.0e5, 44000), NpcLaneFeedEntry(1.0e7, 0), NpcLaneFeedEntry(5.0e6, 50),
        NpcLaneFeedEntry(3.0e6, 900, false) };
    WriteLaneGroupTexelW(frame, texels);
    if (!TestEqual(TEXT("buffer size unchanged"), texels.Num(), 2 * width))
    {
        return false;
    }

    bool bXyzKept = true;
    bool bOthersZero = true;
    for (int32 column = 0; column < width; ++column)
    {
        const FVector4f& row0 = texels[column];
        const FVector4f& row1 = texels[width + column];
        for (const int32 index : { column, width + column })
        {
            const FVector4f& now = texels[index];
            const FVector4f& old = before[index];
            bXyzKept &= now.X == old.X && now.Y == old.Y && now.Z == old.Z;
        }
        if (column < frame.Lanes.Num())
        {
            const FSOLNpcLaneEntry& entry = frame.Lanes[column];
            const FString label = FString::Printf(TEXT("lane %d"), column);
            TestTrue(label + TEXT(": row 0 .w = radius"), row0.W == static_cast<float>(entry.RadiusM));
            const float weight = entry.bDrawn ? static_cast<float>(entry.TrafficWeight) : -1.0f;
            TestTrue(label + TEXT(": row 1 .w = weight or -1"), row1.W == weight);
        }
        else
        {
            bOthersZero &= row0.W == 0.0f && row1.W == 0.0f;
        }
    }
    TestTrue(TEXT("x, y, z of both rows untouched"), bXyzKept);
    TestTrue(TEXT("non-lane columns: .w = 0"), bOthersZero);
    TestTrue(TEXT("lane 1 (no ships): -1"), texels[width + 1].W == -1.0f);
    TestTrue(TEXT("lane 2 (drawn, weight 0): 0, not -1"), texels[width + 2].W == 0.0f);
    TestTrue(TEXT("lane 3 (body unresolved): -1"), texels[width + 3].W == -1.0f);
    TestTrue(TEXT("lane 0 weight in (0, 1]"), texels[width].W > 0.0f && texels[width].W <= 1.0f);

    // An empty frame (no population) clears every .w
    WriteLaneGroupTexelW(FSOLNpcLaneFrame(), texels);
    bool bAllZero = true;
    for (const FVector4f& texel : texels)
    {
        bAllZero &= texel.W == 0.0f;
    }
    TestTrue(TEXT("empty frame: every .w = 0"), bAllZero);

    // More lanes than columns: the first NPC_GROUP_TEXTURE_WIDTH lanes are written, nothing past the buffer
    FSOLNpcLaneFrame wide;
    for (int32 lane = 0; lane < width + 2; ++lane)
    {
        wide.Lanes.Add(NpcLaneFeedEntry(1.0e6 + 1000.0 * lane, 1000));
    }
    WriteLaneGroupTexelW(wide, texels);
    TestEqual(TEXT("wide frame: buffer size unchanged"), texels.Num(), 2 * width);
    TestTrue(TEXT("wide frame: last column = lane width - 1"),
        texels[width - 1].W == static_cast<float>(wide.Lanes[width - 1].RadiusM));

    // A buffer shorter than two rows is not written
    TArray<FVector4f> shortTexels;
    shortTexels.Init(FVector4f(1.0f, 2.0f, 3.0f, 7.0f), 2 * width - 1);
    WriteLaneGroupTexelW(frame, shortTexels);
    bool bShortKept = true;
    for (const FVector4f& texel : shortTexels)
    {
        bShortKept &= texel == FVector4f(1.0f, 2.0f, 3.0f, 7.0f);
    }
    TestTrue(TEXT("short buffer: untouched"), bShortKept);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathAccumulateShipsTest, "SOLTest.NpcLaneMath.AccumulateLaneShips",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 30 (38b, D13): AccumulateLaneShips(rails, inOutCounts) is one rail-fill task's share of the histogram: it adds
// one per lane rail whose group is below inOutCounts.Num(), without resetting, so per-task counts over any partition
// of the rails, merged, equal CountLaneShips (the pure reference)
bool FSOLNpcLaneMathAccumulateShipsTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::AccumulateLaneShips;
    using SOLNpcLane::CountLaneShips;
    constexpr int32 laneCount = 8;

    const TArray<FSOLNpcRail> rails = NpcLaneFeedRails(1000);
    TArray<int32> reference;
    CountLaneShips(rails, laneCount, reference);

    // The fixture has lane rails in range and out of range, and other kinds
    int32 inRange = 0;
    int32 outOfRange = 0;
    for (const FSOLNpcRail& rail : rails)
    {
        if (rail.Kind == static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane))
        {
            (rail.Group < laneCount ? inRange : outOfRange) += 1;
        }
    }
    TestTrue(TEXT("fixture: lane rails out of range exist"), outOfRange > 0);
    TestTrue(TEXT("fixture: other kinds exist"), inRange + outOfRange < rails.Num());

    // Uneven task boundaries, each task into its own zeroed counts, merged at the end
    const int32 bounds[] = { 0, 1, 8, 108, 441, 1000 };
    TArray<int32> merged;
    merged.Init(0, laneCount);
    for (int32 task = 0; task + 1 < static_cast<int32>(UE_ARRAY_COUNT(bounds)); ++task)
    {
        TArray<int32> local;
        local.Init(0, laneCount);
        AccumulateLaneShips(TConstArrayView<FSOLNpcRail>(rails.GetData() + bounds[task],
            bounds[task + 1] - bounds[task]), local);
        for (int32 lane = 0; lane < laneCount; ++lane)
        {
            merged[lane] += local[lane];
        }
    }
    TestTrue(TEXT("merged task counts == CountLaneShips"), merged == reference);
    int32 total = 0;
    for (const int32 count : merged)
    {
        total += count;
    }
    TestEqual(TEXT("total == lane rails in range"), total, inRange);

    // No reset: the counts are added to what is there
    TArray<int32> offset;
    offset.Init(5, laneCount);
    AccumulateLaneShips(rails, offset);
    bool bAdded = true;
    for (int32 lane = 0; lane < laneCount; ++lane)
    {
        bAdded &= offset[lane] == reference[lane] + 5;
    }
    TestTrue(TEXT("adds to the existing counts"), bAdded);
    AccumulateLaneShips(rails, offset);
    bool bTwice = true;
    for (int32 lane = 0; lane < laneCount; ++lane)
    {
        bTwice &= offset[lane] == 2 * reference[lane] + 5;
    }
    TestTrue(TEXT("a second pass adds again"), bTwice);

    // No rails: unchanged; fewer counts: only those groups
    TArray<int32> untouched = { 1, 2, 3 };
    AccumulateLaneShips(TConstArrayView<FSOLNpcRail>(), untouched);
    TestTrue(TEXT("no rails: unchanged"), untouched == TArray<int32>({ 1, 2, 3 }));
    TArray<int32> three;
    three.Init(0, 3);
    AccumulateLaneShips(rails, three);
    TArray<int32> threeReference;
    CountLaneShips(rails, 3, threeReference);
    TestTrue(TEXT("three counts: groups 0-2 only"), three == threeReference);
    TArray<int32> none;
    AccumulateLaneShips(rails, none);
    TestEqual(TEXT("no counts: still none"), none.Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathFlowClockTest, "SOLTest.NpcLaneMath.FlowClock",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 31 (38b, D15; amended 2026-11-11: the dashes never freeze): AdvanceLaneFlowClockS(clockS, realDeltaS) is the
// dash flow's real-time clock: it takes a real (undilated, warp-free) frame time and has no pause input, so it runs on
// in the pause menu and under any time warp; LaneDashFlowPhase of the result advances by
// NPC_LANE_DASH_FLOW_PERIODS_PER_S x the delta (mod 1); a negative or non-finite delta leaves the clock as it is, and a
// non-finite clock restarts from 0
bool FSOLNpcLaneMathFlowClockTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::AdvanceLaneFlowClockS;
    using SOLNpcLane::LaneDashFlowPhase;
    const double flow = SOL::NPC_LANE_DASH_FLOW_PERIODS_PER_S;

    // Running: the phase advances by flow x delta, wrapped
    for (const double clock : { 0.0, 3.5, 1.0e6 + 0.125 })
    {
        for (const double delta : { 1.0 / 60.0, 0.5, 1.0 / flow, 10.0 })
        {
            const double expected = NpcLaneFeedFrac(LaneDashFlowPhase(clock) + flow * delta);
            const double phase = LaneDashFlowPhase(AdvanceLaneFlowClockS(clock, delta));
            TestTrue(FString::Printf(TEXT("clock %.3f + %.4f s: phase advanced"), clock, delta),
                NpcLaneWrapDistance(phase, expected) <= NPCLANE_TOL);
        }
    }

    // 60 frames of 1/60 s advance the phase by one second's flow
    double clock = 2.0;
    for (int32 frame = 0; frame < 60; ++frame)
    {
        clock = AdvanceLaneFlowClockS(clock, 1.0 / 60.0);
    }
    TestTrue(TEXT("one second of frames: phase + flow"), NpcLaneWrapDistance(LaneDashFlowPhase(clock),
        NpcLaneFeedFrac(LaneDashFlowPhase(2.0) + flow)) <= 1.0e-9);

    // Zero, negative and non-finite deltas leave the clock as it is
    for (const double badDelta : { 0.0, -0.016, NPCLANE_NAN, NPCLANE_INF, -NPCLANE_INF })
    {
        TestEqual(FString::Printf(TEXT("delta %f: unchanged"), badDelta), AdvanceLaneFlowClockS(5.25, badDelta),
            5.25, NPCLANE_TOL);
    }

    // A non-finite clock restarts from 0
    for (const double badClock : { NPCLANE_NAN, NPCLANE_INF })
    {
        const double restarted = AdvanceLaneFlowClockS(badClock, 0.5);
        TestTrue(FString::Printf(TEXT("clock %f: finite"), badClock), FMath::IsFinite(restarted));
        TestTrue(FString::Printf(TEXT("clock %f: phase of 0.5 s"), badClock),
            NpcLaneWrapDistance(LaneDashFlowPhase(restarted), LaneDashFlowPhase(0.5)) <= NPCLANE_TOL);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathRibbonLayoutTest, "SOLTest.NpcLaneMath.RibbonLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 32 (38b, section 4.8): the lane mesh, built once per population. LaneRibbonVertexCount(lanes) = lanes x 2
// lines x (NPC_LANE_SEGMENTS + 1) samples x 2 sides (2,080 for 8 lanes). BuildLaneRibbonLayout(counts, uv0, uv1,
// indices) resets its outputs and lays out one strip per line (0 outbound, 1 inbound) of every lane with ships
// (IsLaneDrawn): UV0 = (sample parameter u = k / NPC_LANE_SEGMENTS, side -1 / +1), UV1 = (lane group index, line
// index); two triangles per segment, each within one strip, spanning one segment and both sides
bool FSOLNpcLaneMathRibbonLayoutTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::BuildLaneRibbonLayout;
    using SOLNpcLane::LaneRibbonVertexCount;
    constexpr int32 segments = SOL::NPC_LANE_SEGMENTS;
    const double segmentU = 1.0 / static_cast<double>(segments);

    // Vertex count (2,080 at the proposed 64 segments)
    TestEqual(TEXT("8 lanes: formula"), LaneRibbonVertexCount(8), 8 * 2 * (segments + 1) * 2);
    TestEqual(TEXT("8 lanes: 2,080 (proposed value)"), LaneRibbonVertexCount(8), 2080);
    TestEqual(TEXT("0 lanes: 0"), LaneRibbonVertexCount(0), 0);
    TestEqual(TEXT("-1 lanes: 0"), LaneRibbonVertexCount(-1), 0);

    // Lanes 0, 2 and 4 have ships; 1 (none) and 3 (a bad count) get no strip. Stale outputs are replaced
    const TArray<int32> counts = { 44000, 0, 900, -5, 1 };
    const int32 drawnLanes[] = { 0, 2, 4 };
    TArray<FVector2f> uv0 = { FVector2f(9.0f, 9.0f) };
    TArray<FVector2f> uv1 = { FVector2f(9.0f, 9.0f), FVector2f(8.0f, 8.0f) };
    TArray<uint32> indices = { 7u, 7u, 7u };
    BuildLaneRibbonLayout(counts, uv0, uv1, indices);
    const int32 drawnCount = static_cast<int32>(UE_ARRAY_COUNT(drawnLanes));
    const int32 vertexCount = LaneRibbonVertexCount(drawnCount);
    if (!TestEqual(TEXT("uv0: one per vertex"), uv0.Num(), vertexCount)
        || !TestEqual(TEXT("uv1: one per vertex"), uv1.Num(), vertexCount)
        || !TestEqual(TEXT("indices: 2 triangles per segment per strip"), indices.Num(),
            drawnCount * 2 * segments * 6))
    {
        return false;
    }

    // Every vertex: a known strip, a sample on the grid and a side; each (strip, sample, side) exactly once
    TMap<int32, TArray<uint8>> stripSides;                  // Strip key (lane x 2 + line) -> side bits per sample
    bool bVerticesValid = true;
    TArray<int32> vertexStrip;
    TArray<int32> vertexSample;
    TArray<uint8> vertexSide;
    for (int32 vertex = 0; vertex < vertexCount; ++vertex)
    {
        const int32 lane = FMath::RoundToInt32(uv1[vertex].X);
        const int32 line = FMath::RoundToInt32(uv1[vertex].Y);
        const int32 sample = FMath::RoundToInt32(uv0[vertex].X * segments);
        const uint8 side = static_cast<uint8>(uv0[vertex].Y == -1.0f ? 1 : (uv0[vertex].Y == 1.0f ? 2 : 0));
        bVerticesValid &= uv1[vertex].X == static_cast<float>(lane) && uv1[vertex].Y == static_cast<float>(line);
        bVerticesValid &= lane == 0 || lane == 2 || lane == 4;
        bVerticesValid &= line == 0 || line == 1;
        bVerticesValid &= sample >= 0 && sample <= segments
            && FMath::Abs(static_cast<double>(uv0[vertex].X) - sample * segmentU) <= 1.0e-6;
        bVerticesValid &= side != 0u;
        const int32 key = lane * 2 + line;
        TArray<uint8>& sides = stripSides.FindOrAdd(key);
        if (sides.Num() == 0)
        {
            sides.Init(0, segments + 1);
        }
        if (sample >= 0 && sample <= segments)
        {
            bVerticesValid &= (sides[sample] & side) == 0;
            sides[sample] |= side;
        }
        vertexStrip.Add(key);
        vertexSample.Add(sample);
        vertexSide.Add(side);
    }
    TestTrue(TEXT("every vertex: known lane and line, grid sample, side -1 or +1, no duplicate"), bVerticesValid);
    TestEqual(TEXT("two strips per drawn lane"), stripSides.Num(), drawnCount * 2);
    bool bStripsFull = true;
    for (const TPair<int32, TArray<uint8>>& strip : stripSides)
    {
        for (const uint8 sides : strip.Value)
        {
            bStripsFull &= sides == 3u;
        }
    }
    TestTrue(TEXT("every strip: both sides at every sample 0..segments"), bStripsFull);

    // Every triangle: three distinct in-range vertices of one strip spanning one segment and both sides; every segment
    // of every strip covered by exactly two triangles
    TMap<int32, TArray<int32>> segmentTriangles;
    bool bTrianglesValid = true;
    for (int32 triangle = 0; triangle + 2 < indices.Num(); triangle += 3)
    {
        const uint32 i0 = indices[triangle];
        const uint32 i1 = indices[triangle + 1];
        const uint32 i2 = indices[triangle + 2];
        const uint32 limit = static_cast<uint32>(vertexCount);
        if (i0 >= limit || i1 >= limit || i2 >= limit || i0 == i1 || i1 == i2 || i0 == i2)
        {
            bTrianglesValid = false;
            continue;
        }
        const int32 key = vertexStrip[i0];
        bTrianglesValid &= vertexStrip[i1] == key && vertexStrip[i2] == key;
        const int32 low = FMath::Min3(vertexSample[i0], vertexSample[i1], vertexSample[i2]);
        const int32 high = FMath::Max3(vertexSample[i0], vertexSample[i1], vertexSample[i2]);
        bTrianglesValid &= high - low == 1;
        bTrianglesValid &= (vertexSide[i0] | vertexSide[i1] | vertexSide[i2]) == 3u;
        TArray<int32>& perSegment = segmentTriangles.FindOrAdd(key);
        if (perSegment.Num() == 0)
        {
            perSegment.Init(0, segments);
        }
        if (low >= 0 && low < segments)
        {
            ++perSegment[low];
        }
    }
    TestTrue(TEXT("every triangle: one strip, one segment, both sides"), bTrianglesValid);
    bool bSegmentsCovered = segmentTriangles.Num() == stripSides.Num();
    for (const TPair<int32, TArray<int32>>& strip : segmentTriangles)
    {
        for (const int32 count : strip.Value)
        {
            bSegmentsCovered &= count == 2;
        }
    }
    TestTrue(TEXT("every segment of every strip: two triangles"), bSegmentsCovered);

    // No lane with ships: empty outputs
    BuildLaneRibbonLayout(TArray<int32>({ 0, 0 }), uv0, uv1, indices);
    TestTrue(TEXT("no ships: empty"), uv0.Num() == 0 && uv1.Num() == 0 && indices.Num() == 0);
    BuildLaneRibbonLayout(TConstArrayView<int32>(), uv0, uv1, indices);
    TestTrue(TEXT("no lanes: empty"), uv0.Num() == 0 && uv1.Num() == 0 && indices.Num() == 0);
    return true;
}

namespace
{
    // One camera and lane of the keep-right side cases (endpoints relative to the camera, metres)
    struct FNpcLaneSideView
    {
        const TCHAR* Name;
        FRotator Rotation;   // Camera rotation: X forward, Y right, Z up
        FVector3d A;
        FVector3d B;
    };

    //////////////////////////////////////////////////////////////////////////
    // Projects a camera-relative point to the screen plane at unit depth (x right, y down)
    FVector2D NpcLaneProjectToScreen(const FVector3d& pointM, const FVector3d& right, const FVector3d& up,
        const FVector3d& forward)
    {
        const double depth = FVector3d::DotProduct(pointM, forward);
        return FVector2D(FVector3d::DotProduct(pointM, right) / depth, -FVector3d::DotProduct(pointM, up) / depth);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcLaneMathOutboundSideTest, "SOLTest.NpcLaneMath.OutboundSide",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 33 (38b review; D2, section 4.8): LaneOutboundSide, the C++ mirror of the lane shader's SOLNL_OutboundSide,
// keeps the outbound line on the screen right of its travel. Over several views (one endpoint behind the camera in
// two), a lane point moved along the returned side lands on the side LaneOutboundSideNormal gives for the projected
// travel (two visible lane points in A -> B order); the side is a unit vector perpendicular to the view rays of the
// lane, and the reversed lane gives the opposite side. Degenerate lanes give zero
bool FSOLNpcLaneMathOutboundSideTest::RunTest(const FString& /*parameters*/)
{
    using SOLNpcLane::LaneOutboundSide;
    const FNpcLaneSideView views[] = {
        { TEXT("broadside, level camera"), FRotator(0.0, 0.0, 0.0), FVector3d(10.0, -3.0, 1.0),
            FVector3d(12.0, 4.0, -2.0) },
        { TEXT("rotated camera"), FRotator(-20.0, 30.0, 40.0), FVector3d(8.0, 9.0, -1.0), FVector3d(3.0, 12.0, 4.0) },
        { TEXT("looking down, diagonal travel"), FRotator(-90.0, 0.0, 0.0), FVector3d(-3.0, -4.0, -20.0),
            FVector3d(5.0, 2.0, -25.0) },
        { TEXT("A behind the camera"), FRotator(0.0, 0.0, 0.0), FVector3d(-5.0, 1.0, 2.0),
            FVector3d(20.0, -3.0, 1.0) },
        { TEXT("B behind the camera, rolled"), FRotator(10.0, -15.0, -70.0), FVector3d(30.0, 2.0, -6.0),
            FVector3d(-8.0, -1.0, 3.0) },
        { TEXT("nearly along the axis, 1.5 degrees off"), FRotator(0.0, 1.5, 0.0), FVector3d(-1.0e3, 1.4, 0.0),
            FVector3d(1.0e5, 1.4, 0.0) },
    };
    for (const FNpcLaneSideView& view : views)
    {
        const FRotationMatrix basis(view.Rotation);
        const FVector3d forward = basis.GetScaledAxis(EAxis::X);
        const FVector3d right = basis.GetScaledAxis(EAxis::Y);
        const FVector3d up = basis.GetScaledAxis(EAxis::Z);
        const FString label(view.Name);
        const FVector3d side = LaneOutboundSide(view.A, view.B, right, up, forward);
        TestEqual(label + TEXT(": unit"), side.Size(), 1.0, 1.0e-9);
        TestTrue(label + TEXT(": perpendicular to the view rays of A and B"),
            FMath::Abs(FVector3d::DotProduct(side, view.A)) <= 1.0e-9 * view.A.Size()
            && FMath::Abs(FVector3d::DotProduct(side, view.B)) <= 1.0e-9 * view.B.Size());

        // The visible part of the lane (depth > 0) and two of its points in A -> B order
        const double depthA = FVector3d::DotProduct(view.A, forward);
        const double depthB = FVector3d::DotProduct(view.B, forward);
        double low = 0.0;
        double high = 1.0;
        if (depthA <= 0.0)
        {
            low = depthA / (depthA - depthB);
        }
        else if (depthB <= 0.0)
        {
            high = depthA / (depthA - depthB);
        }
        const double s1 = low + 0.25 * (high - low);
        const double s2 = low + 0.75 * (high - low);
        const FVector3d p1 = FMath::Lerp(view.A, view.B, s1);
        const FVector3d p2 = FMath::Lerp(view.A, view.B, s2);
        const FVector2D screenRight = SOLNpcLane::LaneOutboundSideNormal(
            NpcLaneProjectToScreen(p1, right, up, forward), NpcLaneProjectToScreen(p2, right, up, forward));

        // A lane point offset along the side lands on the outbound (right-hand) side on screen
        const FVector3d point = FMath::Lerp(p1, p2, 0.5);
        const FVector3d moved = point + side * (1.0e-3 * point.Size());
        const FVector2D shift = NpcLaneProjectToScreen(moved, right, up, forward)
            - NpcLaneProjectToScreen(point, right, up, forward);
        TestTrue(label + TEXT(": moved along the side, lands right of the travel A -> B"),
            FVector2D::DotProduct(shift, screenRight) > 1.0e-3 * shift.Size());

        // The reversed lane (the inbound travel) gives the opposite side
        const FVector3d reversed = LaneOutboundSide(view.B, view.A, right, up, forward);
        TestTrue(label + TEXT(": reversed lane -> opposite side"), (reversed + side).Size() <= 1.0e-9);
    }

    // Degenerate lanes: no side
    const FVector3d forward = FVector3d::UnitX();
    const FVector3d right = FVector3d::UnitY();
    const FVector3d up = FVector3d::UnitZ();
    const FVector3d a(10.0, 1.0, 2.0);
    TestTrue(TEXT("A == B -> zero"), LaneOutboundSide(a, a, right, up, forward).IsZero());
    TestTrue(TEXT("wholly behind the camera -> zero"),
        LaneOutboundSide(FVector3d(-5.0, 1.0, 0.0), FVector3d(-9.0, -2.0, 1.0), right, up, forward).IsZero());
    TestTrue(TEXT("seen end-on -> zero"), LaneOutboundSide(a, a * 3.0, right, up, forward).IsZero());
    TestTrue(TEXT("non-finite endpoint -> zero"),
        LaneOutboundSide(FVector3d(NPCLANE_NAN, 0.0, 0.0), a, right, up, forward).IsZero());
    TestTrue(TEXT("non-finite basis -> zero"),
        LaneOutboundSide(a, FVector3d(12.0, 4.0, -2.0), FVector3d(NPCLANE_INF, 0.0, 0.0), up, forward).IsZero());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
