/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcPixelMath.h"
#include "Npc/SOLNpcPointMirror.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcTunnelMirror.h"
#include "SOLConstants.h"
#include "Tests/SOLTestHelpers.h"
#include "Visuals/SOLThrusterMath.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #54 (Part 54a) sub-pixel ship rule, written against the contract in
// Docs/Plans/54-hide-subpixel-ships-contract.md (SDD 54 sections 4.1-4.6 and 9) before any production code existed:
// the double SOLNpcPixel math and the float SOLNpcPointMirror copy of the point shader. All sizes are in 1080p
// reference px; every threshold is a pixel size, never a distance or a count.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double NPCPIXEL_REL_D = 1.0e-9;           // Pixel sizes, double, relative
    constexpr double NPCPIXEL_REL_F = 1.0e-4;           // Pixel sizes, float, relative
    constexpr double NPCPIXEL_ALPHA_D = 1.0e-12;        // Alpha, double, absolute
    constexpr double NPCPIXEL_ALPHA_F = 1.0e-5;         // Alpha, float, absolute
    constexpr double NPCPIXEL_F90 = 960.0;              // Focal at 90 degrees, 16:9, reference px
    constexpr double NPCPIXEL_REF_HEIGHT = 1080.0;      // Reference height in px
    constexpr double NPCPIXEL_HYSTERESIS = 0.1;         // Render tier hysteresis (user Q1, Q2)
    constexpr double NPCPIXEL_MESH_PX = 16.0;           // Mesh threshold (user Q1)
    constexpr double NPCPIXEL_HULL_PX = 4.0;            // Hull threshold (user Q2)

    //////////////////////////////////////////////////////////////////////////
    // Returns a quiet NaN for the invalid-input cases
    double NpcPixelNaN()
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns positive infinity for the invalid-input cases
    double NpcPixelInf()
    {
        return std::numeric_limits<double>::infinity();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns D0, the faction 0 cone's box diagonal (12 x 12 x 30 m), computed rather than a rounded literal
    double NpcPixelD0()
    {
        return FMath::Sqrt(1188.0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns f110, the focal length at a 110 degree horizontal field of view, 16:9, in reference px
    double NpcPixelF110()
    {
        return 0.5 * NPCPIXEL_REF_HEIGHT * (16.0 / 9.0) / FMath::Tan(FMath::DegreesToRadians(55.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when a value matches a non-zero expected value within a relative tolerance
    bool NpcPixelNearRel(const double actual, const double expected, const double relTolerance)
    {
        return SOLTestHelpers::RelativeError(actual, expected) <= relTolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the i-th of count log-spaced values from lo to hi inclusive
    double NpcPixelLogSpaced(const int32 index, const int32 count, const double lo, const double hi)
    {
        const double t = static_cast<double>(index) / static_cast<double>(count - 1);
        return lo * FMath::Pow(hi / lo, t);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a readable name of a render tier for messages
    const TCHAR* NpcPixelTierName(const SOLNpcScale::ERenderTier tier)
    {
        switch (tier)
        {
        case SOLNpcScale::ERenderTier::Mesh:
            return TEXT("Mesh");
        case SOLNpcScale::ERenderTier::Instanced:
            return TEXT("Instanced");
        case SOLNpcScale::ERenderTier::Point:
            return TEXT("Point");
        }
        return TEXT("?");
    }

    // One row of the dot alpha table (contract case 2)
    struct FNpcPixelAlphaRow
    {
        double SizePx;
        double Alpha;
        double Tolerance;
        bool bDrawn;
    };

    // One row of the render tier table (contract case 4)
    struct FNpcPixelTierRow
    {
        SOLNpcScale::ERenderTier Current;
        double SizePx;
        SOLNpcScale::ERenderTier Expected;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the dot alpha table of contract case 2 (hide 1, full 2), the 1.0 + 1e-9 row included
    TArray<FNpcPixelAlphaRow> NpcPixelAlphaRows()
    {
        return {
            { 0.0, 0.0, 0.0, false },
            { 0.5, 0.0, 0.0, false },
            { 0.999999, 0.0, 0.0, false },
            { 1.0, 0.0, 0.0, false },
            { 1.0 + 1.0e-9, 1.0e-9, NPCPIXEL_ALPHA_D, true },
            { 1.25, 0.25, NPCPIXEL_ALPHA_D, true },
            { 1.5, 0.5, NPCPIXEL_ALPHA_D, true },
            { 1.75, 0.75, NPCPIXEL_ALPHA_D, true },
            { 2.0, 1.0, 0.0, true },
            { 3.0, 1.0, 0.0, true },
            { 1.0e6, 1.0, 0.0, true },
            { NpcPixelInf(), 1.0, 0.0, true },
            { NpcPixelNaN(), 0.0, 0.0, false },
            { -1.0, 0.0, 0.0, false },
            { -NpcPixelInf(), 0.0, 0.0, false },
        };
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the render tier table of contract case 4
    TArray<FNpcPixelTierRow> NpcPixelTierRows()
    {
        using SOLNpcScale::ERenderTier;
        return {
            { ERenderTier::Point, 20.0, ERenderTier::Mesh },
            { ERenderTier::Point, 10.0, ERenderTier::Instanced },
            { ERenderTier::Point, 4.2, ERenderTier::Instanced },
            { ERenderTier::Point, 3.8, ERenderTier::Point },
            { ERenderTier::Instanced, 3.8, ERenderTier::Instanced },
            { ERenderTier::Instanced, 3.5, ERenderTier::Point },
            { ERenderTier::Instanced, 17.0, ERenderTier::Mesh },
            { ERenderTier::Mesh, 15.0, ERenderTier::Mesh },
            { ERenderTier::Mesh, 14.0, ERenderTier::Instanced },
            { ERenderTier::Mesh, 3.0, ERenderTier::Point },
        };
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelShipSizeTest, "SOLTest.NpcPixel.ShipSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 1: true size = focal x diameter / max(distance, 1), linear in diameter and focal, inverse in distance, the
// distance clamped at 1 m, and 0 for every bad input
bool FSOLNpcPixelShipSizeTest::RunTest(const FString& /*parameters*/)
{
    const double d0 = NpcPixelD0();
    const double f90 = NPCPIXEL_F90;
    const double f110 = NpcPixelF110();

    // The 1 px and 2 px anchors
    TestTrue(TEXT("D0 at D0 x 960 m is 1 px"), NpcPixelNearRel(SOLNpcPixel::ShipSizePx(d0, d0 * f90, f90), 1.0,
        NPCPIXEL_REL_D));
    TestTrue(TEXT("30 m at 28,800 m is 1 px"), NpcPixelNearRel(SOLNpcPixel::ShipSizePx(30.0, 28800.0, 960.0), 1.0,
        NPCPIXEL_REL_D));
    TestTrue(TEXT("30 m at 14,400 m is 2 px"), NpcPixelNearRel(SOLNpcPixel::ShipSizePx(30.0, 14400.0, 960.0), 2.0,
        NPCPIXEL_REL_D));

    // Linear in diameter and focal, inverse in distance
    const double base = SOLNpcPixel::ShipSizePx(d0, 10000.0, f90);
    TestTrue(TEXT("D0 at 10 km, f90 = f90 x D0 / 10,000"), NpcPixelNearRel(base, f90 * d0 / 10000.0, NPCPIXEL_REL_D));
    TestTrue(TEXT("doubling the diameter doubles the size"),
        NpcPixelNearRel(SOLNpcPixel::ShipSizePx(2.0 * d0, 10000.0, f90), 2.0 * f90 * d0 / 10000.0, NPCPIXEL_REL_D));
    TestTrue(TEXT("doubling the focal doubles the size"),
        NpcPixelNearRel(SOLNpcPixel::ShipSizePx(d0, 10000.0, 2.0 * f90), 2.0 * f90 * d0 / 10000.0, NPCPIXEL_REL_D));
    TestTrue(TEXT("doubling the distance halves the size"),
        NpcPixelNearRel(SOLNpcPixel::ShipSizePx(d0, 20000.0, f90), 0.5 * f90 * d0 / 10000.0, NPCPIXEL_REL_D));
    TestTrue(TEXT("doubling the distance from 1 m halves the size"),
        NpcPixelNearRel(SOLNpcPixel::ShipSizePx(d0, 2.0, f90), 0.5 * f90 * d0, NPCPIXEL_REL_D));
    TestTrue(TEXT("f110 size = f90 size x f110 / f90"),
        NpcPixelNearRel(SOLNpcPixel::ShipSizePx(d0, 10000.0, f110), f90 * d0 / 10000.0 * f110 / f90, NPCPIXEL_REL_D));

    // Distance clamp: 0 <= d <= 1 gives f x D, never infinity
    for (const double distance : { 0.0, 0.5, 1.0 })
    {
        TestTrue(FString::Printf(TEXT("clamp: D0 at %.1f m = f90 x D0"), distance),
            NpcPixelNearRel(SOLNpcPixel::ShipSizePx(d0, distance, f90), f90 * d0, NPCPIXEL_REL_D));
        TestTrue(FString::Printf(TEXT("clamp: 30 m at %.1f m, f 960 = 28,800"), distance),
            NpcPixelNearRel(SOLNpcPixel::ShipSizePx(30.0, distance, 960.0), 28800.0, NPCPIXEL_REL_D));
    }

    // Bad inputs give 0
    for (const double diameter : { 0.0, -1.0, NpcPixelNaN(), NpcPixelInf() })
    {
        TestEqual(FString::Printf(TEXT("bad diameter %f -> 0"), diameter),
            SOLNpcPixel::ShipSizePx(diameter, 10000.0, f90), 0.0, 0.0);
    }
    for (const double focal : { 0.0, -960.0, NpcPixelNaN(), NpcPixelInf() })
    {
        TestEqual(FString::Printf(TEXT("bad focal %f -> 0"), focal), SOLNpcPixel::ShipSizePx(d0, 10000.0, focal),
            0.0, 0.0);
    }
    for (const double distance : { -1.0, NpcPixelNaN(), NpcPixelInf() })
    {
        TestEqual(FString::Printf(TEXT("bad distance %f -> 0"), distance),
            SOLNpcPixel::ShipSizePx(d0, distance, f90), 0.0, 0.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelDotAlphaTest, "SOLTest.NpcPixel.DotAlpha",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 2: the dot alpha is 0 at and below 1 px (and for NaN), linear to 1 at 2 px, 1 above; IsDotDrawn is alpha > 0;
// continuous and monotonic; generic thresholds, the step when full <= hide, and non-finite thresholds
bool FSOLNpcPixelDotAlphaTest::RunTest(const FString& /*parameters*/)
{
    // The pinned thresholds (user D2, D3)
    TestEqual(TEXT("NPC_SHIP_HIDE_PX is 1"), SOL::NPC_SHIP_HIDE_PX, 1.0, 0.0);
    TestEqual(TEXT("NPC_SHIP_DOT_FULL_PX is 2"), SOL::NPC_SHIP_DOT_FULL_PX, 2.0, 0.0);
    const double hide = SOL::NPC_SHIP_HIDE_PX;
    const double full = SOL::NPC_SHIP_DOT_FULL_PX;

    // The table
    for (const FNpcPixelAlphaRow& row : NpcPixelAlphaRows())
    {
        const double alpha = SOLNpcPixel::DotAlpha(row.SizePx, hide, full);
        if (row.Tolerance > 0.0)
        {
            TestTrue(FString::Printf(TEXT("alpha at %.10g px = %.10g (got %.10g)"), row.SizePx, row.Alpha, alpha),
                FMath::Abs(alpha - row.Alpha) <= row.Tolerance);
        }
        else
        {
            TestEqual(FString::Printf(TEXT("alpha at %.10g px is exactly %.0f"), row.SizePx, row.Alpha), alpha,
                row.Alpha, 0.0);
        }
        TestEqual(FString::Printf(TEXT("drawn at %.10g px"), row.SizePx), SOLNpcPixel::IsDotDrawn(row.SizePx, hide,
            full), row.bDrawn);
    }

    // Monotonic and continuous over [0, 3]: no pop at 1 or 2
    constexpr int32 sweepCount = 1000;
    const double step = 3.0 / static_cast<double>(sweepCount - 1);
    const double maxJump = step / (full - hide) + 1.0e-12;
    int32 decreases = 0;
    int32 jumps = 0;
    int32 drawnMismatches = 0;
    double previous = SOLNpcPixel::DotAlpha(0.0, hide, full);
    for (int32 index = 1; index < sweepCount; ++index)
    {
        const double size = step * static_cast<double>(index);
        const double alpha = SOLNpcPixel::DotAlpha(size, hide, full);
        decreases += alpha < previous ? 1 : 0;
        jumps += FMath::Abs(alpha - previous) > maxJump ? 1 : 0;
        drawnMismatches += SOLNpcPixel::IsDotDrawn(size, hide, full) != (alpha > 0.0) ? 1 : 0;
        previous = alpha;
    }
    TestEqual(TEXT("sweep: alpha never decreases"), decreases, 0);
    TestEqual(TEXT("sweep: no neighbour step above step / (full - hide)"), jumps, 0);
    TestEqual(TEXT("sweep: IsDotDrawn == (alpha > 0)"), drawnMismatches, 0);
    TestEqual(TEXT("sweep: alpha at 3 px is 1"), previous, 1.0, 0.0);

    // Generic thresholds and the step when full <= hide
    TestTrue(TEXT("DotAlpha(3, 2, 4) = 0.5"), FMath::Abs(SOLNpcPixel::DotAlpha(3.0, 2.0, 4.0) - 0.5)
        <= NPCPIXEL_ALPHA_D);
    TestEqual(TEXT("step: DotAlpha(1.5, 1, 1) = 1"), SOLNpcPixel::DotAlpha(1.5, 1.0, 1.0), 1.0, 0.0);
    TestEqual(TEXT("step: DotAlpha(1.0, 1, 1) = 0"), SOLNpcPixel::DotAlpha(1.0, 1.0, 1.0), 0.0, 0.0);
    TestEqual(TEXT("step: DotAlpha(0.5, 1, 0.5) = 0"), SOLNpcPixel::DotAlpha(0.5, 1.0, 0.5), 0.0, 0.0);
    TestTrue(TEXT("step: drawn at 1.5 px with hide = full = 1"), SOLNpcPixel::IsDotDrawn(1.5, 1.0, 1.0));
    TestFalse(TEXT("step: not drawn at 1.0 px with hide = full = 1"), SOLNpcPixel::IsDotDrawn(1.0, 1.0, 1.0));

    // A non-finite threshold gives 0 for every size
    const double sizes[] = { 0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 1.0e6, NpcPixelInf() };
    for (const double badThreshold : { NpcPixelNaN(), NpcPixelInf(), -NpcPixelInf() })
    {
        for (const double size : sizes)
        {
            TestEqual(FString::Printf(TEXT("hide %f: alpha at %g px is 0"), badThreshold, size),
                SOLNpcPixel::DotAlpha(size, badThreshold, full), 0.0, 0.0);
            TestEqual(FString::Printf(TEXT("full %f: alpha at %g px is 0"), badThreshold, size),
                SOLNpcPixel::DotAlpha(size, hide, badThreshold), 0.0, 0.0);
            TestFalse(FString::Printf(TEXT("hide %f: not drawn at %g px"), badThreshold, size),
                SOLNpcPixel::IsDotDrawn(size, badThreshold, full));
            TestFalse(FString::Printf(TEXT("full %f: not drawn at %g px"), badThreshold, size),
                SOLNpcPixel::IsDotDrawn(size, hide, badThreshold));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelPixelRadiusTest, "SOLTest.NpcPixel.PixelRadius",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 3: the distance at which a ship is exactly p px across, diameter x focal / p, the inverse of ShipSizePx, and 0
// (a tier no ship qualifies for) for every bad input
bool FSOLNpcPixelPixelRadiusTest::RunTest(const FString& /*parameters*/)
{
    const double d0 = NpcPixelD0();
    const double f90 = NPCPIXEL_F90;
    const double f110 = NpcPixelF110();

    // Anchors
    const double radius1 = SOLNpcPixel::PixelRadiusM(d0, f90, 1.0);
    TestTrue(FString::Printf(TEXT("D0 is 1 px at D0 x 960 m (got %.4f)"), radius1),
        FMath::Abs(radius1 - d0 * 960.0) <= 0.01);
    TestTrue(TEXT("D0 is 1 px at 33,088.68 m"), FMath::Abs(radius1 - 33088.68) <= 0.01);
    TestTrue(TEXT("D0 is 4 px at a quarter of that"), NpcPixelNearRel(SOLNpcPixel::PixelRadiusM(d0, f90, 4.0),
        0.25 * d0 * 960.0, NPCPIXEL_REL_D));
    TestTrue(TEXT("30 m is 2 px at 14,400 m with f 960"), NpcPixelNearRel(SOLNpcPixel::PixelRadiusM(30.0, 960.0, 2.0),
        14400.0, NPCPIXEL_REL_D));

    // The inverse of ShipSizePx
    for (const double focal : { f90, f110 })
    {
        for (const double px : { 1.0, 1.5, 2.0, 4.0, 16.0, 100.0 })
        {
            const double size = SOLNpcPixel::ShipSizePx(d0, SOLNpcPixel::PixelRadiusM(d0, focal, px), focal);
            TestTrue(FString::Printf(TEXT("inverse: f %.3f, %g px round-trips (got %.12g)"), focal, px, size),
                NpcPixelNearRel(size, px, 1.0e-12));
        }
    }

    // Bad inputs give 0
    for (const double bad : { 0.0, -1.0, NpcPixelNaN(), NpcPixelInf(), -NpcPixelInf() })
    {
        TestEqual(FString::Printf(TEXT("bad diameter %f -> 0"), bad), SOLNpcPixel::PixelRadiusM(bad, f90, 1.0),
            0.0, 0.0);
        TestEqual(FString::Printf(TEXT("bad focal %f -> 0"), bad), SOLNpcPixel::PixelRadiusM(d0, bad, 1.0), 0.0, 0.0);
        TestEqual(FString::Printf(TEXT("bad threshold %f -> 0"), bad), SOLNpcPixel::PixelRadiusM(d0, f90, bad),
            0.0, 0.0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelRenderTierByPixelsTest, "SOLTest.NpcPixel.RenderTierByPixels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 4: SOLNpcScale::SelectRenderTier fed pixel radii promotes above 16 / 4 px and keeps a tier down to the
// threshold / 1.1, the same at 110 degrees and at 720p, 1080p and 4K (pixel-defined, FOV and resolution independent)
bool FSOLNpcPixelRenderTierByPixelsTest::RunTest(const FString& /*parameters*/)
{
    // The pinned thresholds (user Q1, Q2) and their spacing
    TestEqual(TEXT("NPC_MESH_MIN_PX is 16"), SOL::NPC_MESH_MIN_PX, NPCPIXEL_MESH_PX, 0.0);
    TestEqual(TEXT("NPC_HULL_DOT_PX is 4"), SOL::NPC_HULL_DOT_PX, NPCPIXEL_HULL_PX, 0.0);
    TestTrue(TEXT("NPC_HULL_DOT_PX >= NPC_SHIP_DOT_FULL_PX x 1.5"),
        SOL::NPC_HULL_DOT_PX >= SOL::NPC_SHIP_DOT_FULL_PX * 1.5);
    TestTrue(TEXT("NPC_MESH_MIN_PX > NPC_HULL_DOT_PX x 1.5"), SOL::NPC_MESH_MIN_PX > SOL::NPC_HULL_DOT_PX * 1.5);

    const double d0 = NpcPixelD0();
    const TArray<FNpcPixelTierRow> rows = NpcPixelTierRows();

    // Runs the table at one focal length
    const auto runTable = [this, d0, &rows](const double focal, const TCHAR* label)
    {
        const double meshRadius = SOLNpcPixel::PixelRadiusM(d0, focal, NPCPIXEL_MESH_PX);
        const double hullRadius = SOLNpcPixel::PixelRadiusM(d0, focal, NPCPIXEL_HULL_PX);
        for (const FNpcPixelTierRow& row : rows)
        {
            const double distance = SOLNpcPixel::PixelRadiusM(d0, focal, row.SizePx);
            const SOLNpcScale::ERenderTier tier = SOLNpcScale::SelectRenderTier(row.Current, distance, meshRadius,
                hullRadius, NPCPIXEL_HYSTERESIS);
            TestTrue(FString::Printf(TEXT("%s: %s at %.1f px -> %s (got %s)"), label, NpcPixelTierName(row.Current),
                row.SizePx, NpcPixelTierName(row.Expected), NpcPixelTierName(tier)), tier == row.Expected);
        }
    };

    runTable(NPCPIXEL_F90, TEXT("f90"));
    runTable(NpcPixelF110(), TEXT("f110"));

    // Resolution invariance: the focal at 90 degrees is 960 reference px at every 16:9 size
    const FVector2D sizes[] = { FVector2D(1280.0, 720.0), FVector2D(1920.0, 1080.0), FVector2D(3840.0, 2160.0) };
    for (const FVector2D& size : sizes)
    {
        const double focal = SOLThruster::FocalRefPx(90.0, size.X / size.Y);
        TestTrue(FString::Printf(TEXT("focal at %.0fx%.0f is 960"), size.X, size.Y),
            FMath::Abs(focal - 960.0) <= 1.0e-9);
        runTable(focal, *FString::Printf(TEXT("%.0fx%.0f"), size.X, size.Y));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelBoundingDiameterTest, "SOLTest.NpcPixel.BoundingDiameter",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 5: the bounding sphere diameter of a box is its diagonal, 0 for a bad box, and the hull constant is the
// diameter of one placeholder hull and contains the other
bool FSOLNpcPixelBoundingDiameterTest::RunTest(const FString& /*parameters*/)
{
    const double cone = SOLNpcPixel::BoundingDiameterM(FVector3d(12.0, 12.0, 30.0));
    const double wedge = SOLNpcPixel::BoundingDiameterM(FVector3d(30.0, 15.0, 5.4));
    TestTrue(FString::Printf(TEXT("12 x 12 x 30 m -> 34.4674 (got %.6f)"), cone), FMath::Abs(cone - 34.4674) <= 1.0e-4);
    TestTrue(FString::Printf(TEXT("30 x 15 x 5.4 m -> 33.9729 (got %.6f)"), wedge),
        FMath::Abs(wedge - 33.9729) <= 1.0e-4);
    TestTrue(TEXT("3 x 4 x 0 m -> 5"), NpcPixelNearRel(SOLNpcPixel::BoundingDiameterM(FVector3d(3.0, 4.0, 0.0)), 5.0,
        NPCPIXEL_REL_D));
    TestEqual(TEXT("0 x 0 x 0 m -> 0"), SOLNpcPixel::BoundingDiameterM(FVector3d(0.0, 0.0, 0.0)), 0.0, 0.0);

    // Any negative or non-finite component gives 0
    const FVector3d badBoxes[] = {
        FVector3d(-1.0, 1.0, 1.0), FVector3d(1.0, -1.0, 1.0), FVector3d(1.0, 1.0, -1.0),
        FVector3d(NpcPixelNaN(), 1.0, 1.0), FVector3d(1.0, NpcPixelNaN(), 1.0), FVector3d(1.0, 1.0, NpcPixelNaN()),
        FVector3d(NpcPixelInf(), 1.0, 1.0), FVector3d(1.0, NpcPixelInf(), 1.0), FVector3d(1.0, 1.0, -NpcPixelInf()),
    };
    for (const FVector3d& box : badBoxes)
    {
        TestEqual(FString::Printf(TEXT("bad box (%f, %f, %f) -> 0"), box.X, box.Y, box.Z),
            SOLNpcPixel::BoundingDiameterM(box), 0.0, 0.0);
    }

    // The hull constant is one placeholder's diameter (within 1 mm) and at least the other's
    const double constant = SOL::NPC_HULL_BOUNDING_DIAMETER_M;
    const bool bMatchesCone = FMath::Abs(constant - cone) <= 1.0e-3;
    const bool bMatchesWedge = FMath::Abs(constant - wedge) <= 1.0e-3;
    TestTrue(FString::Printf(TEXT("NPC_HULL_BOUNDING_DIAMETER_M %.4f is a placeholder hull's diameter"), constant),
        bMatchesCone || bMatchesWedge);
    if (bMatchesCone || bMatchesWedge)
    {
        TestTrue(TEXT("NPC_HULL_BOUNDING_DIAMETER_M contains the other hull"),
            constant >= (bMatchesCone ? wedge : cone));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelFarLayerSkipTest, "SOLTest.NpcPixel.FarLayerSkip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 6: the far layer can draw only when a ship at its nearest distance is more than 1 px across; conservative
// (true) on bad inputs; the same decision as IsDotDrawn(ShipSizePx) over a log sweep
bool FSOLNpcPixelFarLayerSkipTest::RunTest(const FString& /*parameters*/)
{
    const double d0 = NpcPixelD0();
    const double f90 = NPCPIXEL_F90;
    const double f110 = NpcPixelF110();
    const double hide = SOL::NPC_SHIP_HIDE_PX;

    // The table
    TestFalse(TEXT("f90, 2,000 km (0.0165 px): skipped"), SOLNpcPixel::FarLayerCanDraw(d0, f90, 2.0e6, hide));
    TestFalse(TEXT("f110, 2,000 km: skipped"), SOLNpcPixel::FarLayerCanDraw(d0, f110, 2.0e6, hide));
    TestTrue(TEXT("f90, 30 km (1.103 px): drawn"), SOLNpcPixel::FarLayerCanDraw(d0, f90, 30000.0, hide));
    TestTrue(TEXT("f90, 33 km (1.0027 px): drawn"), SOLNpcPixel::FarLayerCanDraw(d0, f90, 33000.0, hide));
    TestFalse(TEXT("f90, 33.2 km (0.9966 px): skipped"), SOLNpcPixel::FarLayerCanDraw(d0, f90, 33200.0, hide));
    TestTrue(TEXT("focal 60,000, 2,000 km (1.03 px): drawn"), SOLNpcPixel::FarLayerCanDraw(d0, 60000.0, 2.0e6, hide));

    // Conservative on bad inputs
    for (const double nearest : { 0.0, -1.0, NpcPixelNaN() })
    {
        TestTrue(FString::Printf(TEXT("bad nearest distance %f -> true"), nearest),
            SOLNpcPixel::FarLayerCanDraw(d0, f90, nearest, hide));
    }
    for (const double bad : { 0.0, -1.0, NpcPixelNaN(), NpcPixelInf() })
    {
        TestTrue(FString::Printf(TEXT("bad diameter %f -> true"), bad),
            SOLNpcPixel::FarLayerCanDraw(bad, f90, 2.0e6, hide));
        TestTrue(FString::Printf(TEXT("bad focal %f -> true"), bad),
            SOLNpcPixel::FarLayerCanDraw(d0, bad, 2.0e6, hide));
    }

    // Consistency with the per-ship decision over 200 log-spaced distances in [1e3, 1e9] m
    constexpr int32 sweepCount = 200;
    int32 mismatches = 0;
    for (int32 index = 0; index < sweepCount; ++index)
    {
        const double distance = NpcPixelLogSpaced(index, sweepCount, 1.0e3, 1.0e9);
        const bool bLayer = SOLNpcPixel::FarLayerCanDraw(d0, f90, distance, 1.0);
        const bool bShip = SOLNpcPixel::IsDotDrawn(SOLNpcPixel::ShipSizePx(d0, distance, f90), 1.0, 2.0);
        mismatches += bLayer != bShip ? 1 : 0;
    }
    TestEqual(TEXT("sweep: FarLayerCanDraw == IsDotDrawn(ShipSizePx)"), mismatches, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelMirrorFocalTest, "SOLTest.NpcPixel.MirrorFocal",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 7: the shader's focal length from the projection's [0][0] is the same in reference px at 720p, 1080p and 4K,
// matches the tunnel mirror exactly and SOLThruster::FocalRefPx within float tolerance, and is 0 for a bad view size
bool FSOLNpcPixelMirrorFocalTest::RunTest(const FString& /*parameters*/)
{
    const float refHeight = static_cast<float>(NPCPIXEL_REF_HEIGHT);

    // 90 degrees: 960 at every 16:9 size
    const FVector2f sizes169[] = { FVector2f(1280.0f, 720.0f), FVector2f(1920.0f, 1080.0f),
        FVector2f(3840.0f, 2160.0f) };
    for (const FVector2f& size : sizes169)
    {
        const float focal = SOLNpcPointMirror::FocalRefPx(1.0f, size, refHeight);
        TestTrue(FString::Printf(TEXT("90 deg at %.0fx%.0f -> 960 (got %.4f)"), size.X, size.Y, focal),
            NpcPixelNearRel(focal, 960.0, NPCPIXEL_REL_F));
    }

    // 110 degrees and 21:9
    const float scale110 = static_cast<float>(1.0 / FMath::Tan(FMath::DegreesToRadians(55.0)));
    const float focal110 = SOLNpcPointMirror::FocalRefPx(scale110, FVector2f(1920.0f, 1080.0f), refHeight);
    TestTrue(FString::Printf(TEXT("110 deg at 1080p -> f110 (got %.4f)"), focal110),
        NpcPixelNearRel(focal110, NpcPixelF110(), NPCPIXEL_REL_F));
    const float focalWide = SOLNpcPointMirror::FocalRefPx(1.0f, FVector2f(2560.0f, 1080.0f), refHeight);
    TestTrue(FString::Printf(TEXT("21:9 at 90 deg -> 1280 (got %.4f)"), focalWide),
        NpcPixelNearRel(focalWide, 1280.0, NPCPIXEL_REL_F));

    // Parity with the tunnel mirror (exact) and SOLThruster::FocalRefPx (float tolerance)
    const FVector2f sizes[] = { FVector2f(1280.0f, 720.0f), FVector2f(1920.0f, 1080.0f), FVector2f(3840.0f, 2160.0f),
        FVector2f(2560.0f, 1080.0f) };
    for (const double hFov : { 60.0, 90.0, 110.0 })
    {
        const float scale = static_cast<float>(1.0 / FMath::Tan(FMath::DegreesToRadians(0.5 * hFov)));
        for (const FVector2f& size : sizes)
        {
            const float focal = SOLNpcPointMirror::FocalRefPx(scale, size, refHeight);
            TestEqual(FString::Printf(TEXT("%.0f deg at %.0fx%.0f equals the tunnel mirror"), hFov, size.X, size.Y),
                focal, SOLNpcTunnelMirror::FocalRefPx(scale, size, refHeight), 0.0f);
            const double expected = SOLThruster::FocalRefPx(hFov, static_cast<double>(size.X) / size.Y);
            TestTrue(FString::Printf(TEXT("%.0f deg at %.0fx%.0f matches SOLThruster (%.4f vs %.4f)"), hFov, size.X,
                size.Y, focal, expected), NpcPixelNearRel(focal, expected, NPCPIXEL_REL_F));
        }
    }

    // A bad view size gives 0
    const float nanF = std::numeric_limits<float>::quiet_NaN();
    const FVector2f badSizes[] = { FVector2f(0.0f, 1080.0f), FVector2f(1920.0f, 0.0f), FVector2f(-1920.0f, 1080.0f),
        FVector2f(1920.0f, -1080.0f), FVector2f(nanF, 1080.0f), FVector2f(1920.0f, nanF) };
    for (const FVector2f& size : badSizes)
    {
        TestEqual(FString::Printf(TEXT("bad view size (%f, %f) -> 0"), size.X, size.Y),
            SOLNpcPointMirror::FocalRefPx(1.0f, size, refHeight), 0.0f, 0.0f);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelMirrorParityTest, "SOLTest.NpcPixel.MirrorParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 8: the float shader copy matches the double math over a sweep of diameters, focals and distances, gives the
// same 0 or 1 on every bad-input row, fades in one float ulp above 1 px, and guards NaN explicitly
bool FSOLNpcPixelMirrorParityTest::RunTest(const FString& /*parameters*/)
{
    // The sweep: D in {33.9729, 34.4673}, f in {f110, 960, 1280}, 300 log-spaced distances in [0.5, 1e9] m
    constexpr int32 sweepCount = 300;
    double worstSize = 0.0;
    double worstAlpha = 0.0;
    int32 drawnMismatches = 0;
    int32 drawnCount = 0;
    int32 hiddenCount = 0;
    for (const double diameter : { 33.9729, 34.4673 })
    {
        for (const double focal : { NpcPixelF110(), 960.0, 1280.0 })
        {
            for (int32 index = 0; index < sweepCount; ++index)
            {
                const double distance = NpcPixelLogSpaced(index, sweepCount, 0.5, 1.0e9);
                const double sizeD = SOLNpcPixel::ShipSizePx(diameter, distance, focal);
                const float sizeF = SOLNpcPointMirror::ShipSizePx(static_cast<float>(diameter),
                    static_cast<float>(distance), static_cast<float>(focal));
                worstSize = FMath::Max(worstSize, SOLTestHelpers::RelativeError(sizeF, sizeD));

                const double alphaD = SOLNpcPixel::DotAlpha(sizeD, 1.0, 2.0);
                const float alphaF = SOLNpcPointMirror::DotAlpha(sizeF, 1.0f, 2.0f);
                worstAlpha = FMath::Max(worstAlpha, FMath::Abs(static_cast<double>(alphaF) - alphaD));

                // The drawn decision, away from the 1 px edge
                if (FMath::Abs(sizeD - 1.0) > 1.0e-5)
                {
                    drawnMismatches += (alphaF > 0.0f) != (alphaD > 0.0) ? 1 : 0;
                }
                drawnCount += alphaD > 0.0 ? 1 : 0;
                hiddenCount += alphaD > 0.0 ? 0 : 1;
            }
        }
    }
    TestTrue(FString::Printf(TEXT("sweep: size within 1e-5 relative (worst %.3g)"), worstSize), worstSize <= 1.0e-5);
    TestTrue(FString::Printf(TEXT("sweep: alpha within 1e-5 (worst %.3g)"), worstAlpha),
        worstAlpha <= NPCPIXEL_ALPHA_F);
    TestEqual(TEXT("sweep: the drawn decision matches"), drawnMismatches, 0);
    TestTrue(TEXT("sweep: the sweep holds drawn and hidden ships"), drawnCount > 0 && hiddenCount > 0);

    // Case 1 rows in float: the distance clamp and every bad input
    const float d0 = static_cast<float>(NpcPixelD0());
    const float nanF = std::numeric_limits<float>::quiet_NaN();
    const float infF = std::numeric_limits<float>::infinity();
    for (const float distance : { 0.0f, 0.5f, 1.0f })
    {
        TestTrue(FString::Printf(TEXT("mirror clamp: D0 at %.1f m = 960 x D0"), distance),
            NpcPixelNearRel(SOLNpcPointMirror::ShipSizePx(d0, distance, 960.0f), 960.0 * d0, NPCPIXEL_REL_F));
    }
    for (const float diameter : { 0.0f, -1.0f, nanF, infF })
    {
        TestEqual(FString::Printf(TEXT("mirror bad diameter %f -> 0"), diameter),
            SOLNpcPointMirror::ShipSizePx(diameter, 10000.0f, 960.0f), 0.0f, 0.0f);
    }
    for (const float focal : { 0.0f, -960.0f, nanF, infF })
    {
        TestEqual(FString::Printf(TEXT("mirror bad focal %f -> 0"), focal),
            SOLNpcPointMirror::ShipSizePx(d0, 10000.0f, focal), 0.0f, 0.0f);
    }
    for (const float distance : { -1.0f, nanF, infF })
    {
        TestEqual(FString::Printf(TEXT("mirror bad distance %f -> 0"), distance),
            SOLNpcPointMirror::ShipSizePx(d0, distance, 960.0f), 0.0f, 0.0f);
    }

    // Case 2 rows in float (the 1.0 + 1e-9 row left out: it rounds to 1.0 in float)
    const float hide = static_cast<float>(SOL::NPC_SHIP_HIDE_PX);
    const float full = static_cast<float>(SOL::NPC_SHIP_DOT_FULL_PX);
    for (const FNpcPixelAlphaRow& row : NpcPixelAlphaRows())
    {
        if (row.SizePx > 1.0 && row.SizePx < 1.0 + 1.0e-6)
        {
            continue;
        }
        const float alpha = SOLNpcPointMirror::DotAlpha(static_cast<float>(row.SizePx), hide, full);
        if (row.Tolerance > 0.0)
        {
            TestTrue(FString::Printf(TEXT("mirror alpha at %.6g px = %.6g (got %.6g)"), row.SizePx, row.Alpha, alpha),
                FMath::Abs(alpha - row.Alpha) <= NPCPIXEL_ALPHA_F);
        }
        else
        {
            TestEqual(FString::Printf(TEXT("mirror alpha at %.6g px is exactly %.0f"), row.SizePx, row.Alpha), alpha,
                static_cast<float>(row.Alpha), 0.0f);
        }
    }
    const float ulpAbove = SOLNpcPointMirror::DotAlpha(1.0f + 1.2e-7f, hide, full);
    TestTrue(FString::Printf(TEXT("mirror: one float ulp above 1 px fades in (got %.3g)"), ulpAbove),
        ulpAbove > 0.0f && ulpAbove < 2.0e-7f);
    TestTrue(TEXT("mirror: DotAlpha(3, 2, 4) = 0.5"), FMath::Abs(SOLNpcPointMirror::DotAlpha(3.0f, 2.0f, 4.0f) - 0.5f)
        <= NPCPIXEL_ALPHA_F);
    TestEqual(TEXT("mirror step: DotAlpha(1.5, 1, 1) = 1"), SOLNpcPointMirror::DotAlpha(1.5f, 1.0f, 1.0f), 1.0f, 0.0f);
    TestEqual(TEXT("mirror step: DotAlpha(1.0, 1, 1) = 0"), SOLNpcPointMirror::DotAlpha(1.0f, 1.0f, 1.0f), 0.0f, 0.0f);
    TestEqual(TEXT("mirror step: DotAlpha(0.5, 1, 0.5) = 0"), SOLNpcPointMirror::DotAlpha(0.5f, 1.0f, 0.5f), 0.0f,
        0.0f);
    for (const float badThreshold : { nanF, infF, -infF })
    {
        for (const float size : { 0.0f, 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 1.0e6f, infF })
        {
            TestEqual(FString::Printf(TEXT("mirror hide %f: alpha at %g px is 0"), badThreshold, size),
                SOLNpcPointMirror::DotAlpha(size, badThreshold, full), 0.0f, 0.0f);
            TestEqual(FString::Printf(TEXT("mirror full %f: alpha at %g px is 0"), badThreshold, size),
                SOLNpcPointMirror::DotAlpha(size, hide, badThreshold), 0.0f, 0.0f);
        }
    }

    // The NaN guard: a plain clamp would give 1
    TestEqual(TEXT("mirror: DotAlpha(NaN, 1, 2) = 0"), SOLNpcPointMirror::DotAlpha(nanF, 1.0f, 2.0f), 0.0f, 0.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLNpcPixelDrawnFootprintTest, "SOLTest.NpcPixel.DrawnFootprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 9: a drawn dot's device footprint is its true size scaled to the view height, floored at 1 device px, never
// enlarged at or above 1080p; with a 0 floor it is always the scaled true size
bool FSOLNpcPixelDrawnFootprintTest::RunTest(const FString& /*parameters*/)
{
    // The pinned floor (user Q5)
    TestEqual(TEXT("NPC_POINT_RASTER_MIN_DEVICE_PX is 1"), SOL::NPC_POINT_RASTER_MIN_DEVICE_PX, 1.0, 0.0);
    TestTrue(TEXT("NPC_POINT_RASTER_MIN_DEVICE_PX >= 0"), SOL::NPC_POINT_RASTER_MIN_DEVICE_PX >= 0.0);

    const float refHeight = static_cast<float>(NPCPIXEL_REF_HEIGHT);
    const float rasterMin = static_cast<float>(SOL::NPC_POINT_RASTER_MIN_DEVICE_PX);

    // One row: true size, view height, expected device px
    struct FFootprintRow
    {
        float SizePx;
        float ViewHeightPx;
        double Expected;
    };
    const FFootprintRow rows[] = {
        { 1.5f, 1080.0f, 1.5 },
        { 1.5f, 720.0f, 1.0 },
        { 1.2f, 720.0f, 1.0 },
        { 1.2f, 2160.0f, 2.4 },
        { 3.0f, 1080.0f, 3.0 },
        { 1.0001f, 1080.0f, 1.0001 },
    };

    // A tight tolerance: 1e-4 relative would not tell 1.0001 from the 1.0 floor
    constexpr double footprintTolerance = 1.0e-6;
    for (const FFootprintRow& row : rows)
    {
        const float drawn = SOLNpcPointMirror::DrawnDiameterDevicePx(row.SizePx, row.ViewHeightPx, refHeight,
            rasterMin);
        TestTrue(FString::Printf(TEXT("%.4f px at %.0f p -> %.4f device px (got %.6f)"), row.SizePx, row.ViewHeightPx,
            row.Expected, drawn), NpcPixelNearRel(drawn, row.Expected, footprintTolerance));
    }

    // A 0 floor: always the scaled true size
    for (const float size : { 0.3f, 1.2f, 1.5f, 3.0f })
    {
        for (const float height : { 720.0f, 1080.0f, 1440.0f, 2160.0f })
        {
            const double expected = static_cast<double>(size) * height / NPCPIXEL_REF_HEIGHT;
            const float drawn = SOLNpcPointMirror::DrawnDiameterDevicePx(size, height, refHeight, 0.0f);
            TestTrue(FString::Printf(TEXT("no floor: %.2f px at %.0f p -> %.4f (got %.6f)"), size, height, expected,
                drawn), NpcPixelNearRel(drawn, expected, footprintTolerance));
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
