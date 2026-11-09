/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlightMode.h"
#include "Interaction/SOLInteractMenus.h"
#include "SOLConstants.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloMenuMath.h"
#include "UI/SOLHoloRadial.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudSymbols.h"
#include "UI/SOLOverlayIcons.h"
#include "UI/SOLRadarLayout.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 HUD symbol helper (Docs/SDDs/33-player-documentation.md sections 8.1, 8.3 and 10, D5),
// written against the header contract (UI/SOLHudSymbols.h) before the production code existed. Part 33a: the symbol
// ids, Build's primitive lists, their bounds, the replay into a shape batch, and one source of geometry for the
// symbols that already have a builder (the overlay icons and edge arrows through SOLOverlayIcons, the cards through
// SOLHoloFrameBuild::BuildCard). Part 33d-i: the per-element Append* builders the flight HUD replays (every primitive's
// kind, role, path, intensity and geometry from the SOL::HudGeometry constants), the Frame primitive, the PadPx
// override, the cursor and map icon delegation, and Build of each HUD symbol being its Append* builder (D5). The map
// symbols the user left without a picture (D39: OrbitLine, RouteLeg, RouteLoopLeg, WaypointAbove, WaypointBelow,
// MapPick, MapGridStem) are not required to build. Build (only Build, never an Append* builder) raises every feature
// below SOL::Help::GLOSSARY_MIN_FEATURE_PX to it (D42).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float HUDSYMBOLSTEST_TOLERANCE = 1.0e-3f;         // Pixel comparisons
    constexpr double HUDSYMBOLSTEST_INTENSITY_TOLERANCE = 1.0e-6;   // Glow factors
    constexpr float HUDSYMBOLSTEST_PAD_PX = 2.0f;               // The glow pad of the replays
    constexpr int32 HUDSYMBOLSTEST_SYMBOL_COUNT = static_cast<int32>(ESOLHudSymbol::Count);
    const FVector2f HUDSYMBOLSTEST_CENTRE(400.25f, 300.5f);
    const FVector2f HUDSYMBOLSTEST_OTHER_CENTRE(37.5f, 912.25f);
    const FVector2D HUDSYMBOLSTEST_CENTRE_D(400.25, 300.5);     // The same centre, as the HUD builders take it

    // The symbols without a builder (D39): the map glossary rows show screenshot crops instead, ids kept for later
    constexpr ESOLHudSymbol HUDSYMBOLSTEST_UNBUILT[] = { ESOLHudSymbol::OrbitLine, ESOLHudSymbol::RouteLeg,
        ESOLHudSymbol::RouteLoopLeg, ESOLHudSymbol::WaypointAbove, ESOLHudSymbol::WaypointBelow, ESOLHudSymbol::MapPick,
        ESOLHudSymbol::MapGridStem };

    // Every symbol Build draws (part 33d-i: all but HUDSYMBOLSTEST_UNBUILT)
    constexpr ESOLHudSymbol HUDSYMBOLSTEST_BUILT[] = { ESOLHudSymbol::Reticle, ESOLHudSymbol::Joystick,
        ESOLHudSymbol::JoystickFreeLook, ESOLHudSymbol::SteerFree, ESOLHudSymbol::SteerHeld,
        ESOLHudSymbol::SteerLocked, ESOLHudSymbol::Prograde, ESOLHudSymbol::Retrograde, ESOLHudSymbol::TargetBracket,
        ESOLHudSymbol::TargetEdgeArrow, ESOLHudSymbol::IconStar, ESOLHudSymbol::IconPlanet, ESOLHudSymbol::IconMoon,
        ESOLHudSymbol::IconDwarf, ESOLHudSymbol::IconAsteroid, ESOLHudSymbol::IconRing, ESOLHudSymbol::IconShip,
        ESOLHudSymbol::IconTarget, ESOLHudSymbol::IconEdgeArrow, ESOLHudSymbol::RadarScope,
        ESOLHudSymbol::RadarContactAbove, ESOLHudSymbol::RadarContactBelow, ESOLHudSymbol::RadarSelected,
        ESOLHudSymbol::GMeter, ESOLHudSymbol::SpeedCapBar, ESOLHudSymbol::Card, ESOLHudSymbol::MapBodyIcon,
        ESOLHudSymbol::RadialMenu, ESOLHudSymbol::ListFrame, ESOLHudSymbol::HoloCursor, ESOLHudSymbol::WarpStreaks };
    static_assert(UE_ARRAY_COUNT(HUDSYMBOLSTEST_BUILT) + UE_ARRAY_COUNT(HUDSYMBOLSTEST_UNBUILT)
        == static_cast<int32>(ESOLHudSymbol::Count), "every symbol is built or left without a picture");

    // The overlay icon symbols, in EIconCategory order
    constexpr ESOLHudSymbol HUDSYMBOLSTEST_ICONS[] = { ESOLHudSymbol::IconStar, ESOLHudSymbol::IconPlanet,
        ESOLHudSymbol::IconMoon, ESOLHudSymbol::IconDwarf, ESOLHudSymbol::IconAsteroid, ESOLHudSymbol::IconRing,
        ESOLHudSymbol::IconShip, ESOLHudSymbol::IconTarget };
    static_assert(UE_ARRAY_COUNT(HUDSYMBOLSTEST_ICONS) == static_cast<int32>(SOLOverlayIcons::EIconCategory::Count),
        "one icon symbol per category");

    // UI scales the built symbols are checked at
    constexpr float HUDSYMBOLSTEST_SCALES[] = { 0.5f, 1.0f, 1.75f, 2.0f };

    // UI scales Bounds must be linear over (scale 1 is the unit): at or above 1 no glossary floor (D42) can move a
    // symbol's outline, below it a raised feature may
    constexpr float HUDSYMBOLSTEST_LINEAR_SCALES[] = { 1.0f, 1.5f, 1.75f, 2.0f };

    // UI scales the bounds must contain every primitive at, including a tiny one where the floor (D42) applies
    constexpr float HUDSYMBOLSTEST_CONTAIN_SCALES[] = { 0.05f, 0.5f, 1.0f, 1.75f, 2.0f };

    // UI scales the HUD builders are checked at (contract 9: non-integer centres, these scales)
    constexpr float HUDSYMBOLSTEST_BUILDER_SCALES[] = { 0.5f, 1.0f, 1.5f, 2.0f };

    //////////////////////////////////////////////////////////////////////////
    // Returns a symbol's id for test messages
    FString HudSymbolsTestName(const ESOLHudSymbol symbol)
    {
        return FString::Printf(TEXT("%s (%d)"), SOLHudSymbols::ToName(symbol), static_cast<int32>(symbol));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the box a primitive's own points and sizes span (see ESOLHudPrimKind; glow pad and line width excluded)
    FBox2f HudSymbolsTestPrimExtent(const FSOLHudSymbolPrim& prim)
    {
        const FVector2f radius(prim.RadiusPx, prim.RadiusPx);
        switch (prim.Kind)
        {
        case ESOLHudPrimKind::Line:
        case ESOLHudPrimKind::DashedLine:
        case ESOLHudPrimKind::Card:
        case ESOLHudPrimKind::Frame:
            return FBox2f(FVector2f::Min(prim.A, prim.B), FVector2f::Max(prim.A, prim.B));
        case ESOLHudPrimKind::Ring:
        case ESOLHudPrimKind::Arc:
        case ESOLHudPrimKind::Dot:
        case ESOLHudPrimKind::Icon:
            return FBox2f(prim.A - radius, prim.A + radius);
        case ESOLHudPrimKind::Ellipse:
            return FBox2f(prim.A - FVector2f(prim.RadiusPx, prim.RadiusYPx),
                prim.A + FVector2f(prim.RadiusPx, prim.RadiusYPx));
        case ESOLHudPrimKind::Box:
            return FBox2f(FVector2f::Min(prim.A, prim.A + prim.B), FVector2f::Max(prim.A, prim.A + prim.B));
        case ESOLHudPrimKind::Panel:
            return FBox2f(prim.A - prim.B, prim.A + prim.B);
        case ESOLHudPrimKind::EdgeArrow:
        {
            const FVector2f tip = prim.A + prim.B * prim.RadiusPx;
            return FBox2f(FVector2f::Min(prim.A, tip), FVector2f::Max(prim.A, tip));
        }
        default:
            return FBox2f(prim.A, prim.A);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a primitive of a kind at a point, with sizes that make one shape
    FSOLHudSymbolPrim HudSymbolsTestPrim(const ESOLHudPrimKind kind)
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = kind;
        prim.A = FVector2f(100.0f, 100.0f);
        prim.B = FVector2f(140.0f, 120.0f);
        prim.RadiusPx = 12.0f;
        prim.RadiusYPx = 6.0f;
        prim.MidAngleRad = 0.5f;
        prim.HalfApertureRad = 0.7f;
        prim.DashPx = 4.0f;
        prim.GapPx = 2.0f;
        prim.Fill = 0.5f;
        prim.Intensity = 1.0f;
        prim.Category = SOLOverlayIcons::EIconCategory::Planet;
        prim.Text = TEXT("MANUAL");
        if (kind == ESOLHudPrimKind::Box || kind == ESOLHudPrimKind::Panel)
        {
            prim.B = FVector2f(20.0f, 10.0f);
        }
        else if (kind == ESOLHudPrimKind::EdgeArrow)
        {
            prim.B = FVector2f(1.0f, 0.0f);
        }
        return prim;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the card look of a BuildCard reference count: fully drawn in, with or without its fill and sweep
    FSOLHoloMenuLook HudSymbolsTestCardLook(const bool bFillAndSweep)
    {
        FSOLHoloMenuLook look;
        look.bVisible = true;
        look.Progress = 1.0f;
        look.FrameFraction = 1.0f;
        look.FillAlpha = bFillAndSweep ? 1.0f : 0.0f;
        look.SweepIntensity = bFillAndSweep ? 1.0f : 0.0f;
        look.SweepFraction = 0.5f;
        return look;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the look a Frame primitive and the glossary's radial sectors are built with: fully drawn in and filled,
    // no sweep, nothing hovered (contract 1.3 and 3.11)
    FSOLHoloMenuLook HudSymbolsTestFullLook()
    {
        FSOLHoloMenuLook look;
        look.bVisible = true;
        look.Progress = 1.0f;
        look.FrameFraction = 1.0f;
        look.FillAlpha = 1.0f;
        look.HoverIntensity = 0.0f;
        look.SweepFraction = 0.0f;
        look.SweepIntensity = 0.0f;
        return look;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a card primitive's replay adds between BuildCard's ticks-only count and its full count around its rect
    void HudSymbolsTestCheckCardCount(FAutomationTestBase& test, const FSOLHudSymbolPrim& prim, const int32 added,
        const FString& what)
    {
        const FBox2f content(FVector2f::Min(prim.A, prim.B), FVector2f::Max(prim.A, prim.B));
        FSOLHoloFrameOutput output;
        const int32 least = SOLHoloFrameBuild::BuildCard(content, 1.0f, HudSymbolsTestCardLook(false), 1.0f, output);
        const int32 most = SOLHoloFrameBuild::BuildCard(content, 1.0f, HudSymbolsTestCardLook(true), 1.0f, output);
        test.TestTrue(FString::Printf(TEXT("%s: BuildCard draws a card"), *what), least > 0);
        test.TestTrue(FString::Printf(TEXT("%s: the card's shapes (%d, BuildCard %d..%d)"), *what, added, least, most),
            added >= least && added <= most && added <= SOL::HoloMenu::CARD_MAX_SHAPES);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts two batches hold the same quads (positions and shape parameters; colours are not compared)
    void HudSymbolsTestCheckSameShapes(FAutomationTestBase& test, const FSOLHudShapeBatch& actual,
        const FSOLHudShapeBatch& expected, const FString& what)
    {
        test.TestEqual(what + TEXT(": shape count"), actual.GetShapeCount(), expected.GetShapeCount());
        const TArray<FSlateVertex>& a = actual.GetVertices();
        const TArray<FSlateVertex>& b = expected.GetVertices();
        if (a.Num() != b.Num())
        {
            return;
        }
        int32 mismatches = 0;
        for (int32 index = 0; index < a.Num(); ++index)
        {
            bool bSame = FMath::IsNearlyEqual(a[index].Position.X, b[index].Position.X, HUDSYMBOLSTEST_TOLERANCE)
                && FMath::IsNearlyEqual(a[index].Position.Y, b[index].Position.Y, HUDSYMBOLSTEST_TOLERANCE);
            for (int32 coord = 0; coord < 4; ++coord)
            {
                bSame &= FMath::IsNearlyEqual(a[index].TexCoords[coord], b[index].TexCoords[coord],
                    HUDSYMBOLSTEST_TOLERANCE);
            }
            if (!bSame && mismatches++ < 3)
            {
                test.AddError(FString::Printf(TEXT("%s: vertex %d differs from the builder's"), *what, index));
            }
        }
        test.TestEqual(what + TEXT(": every vertex the builder's"), mismatches, 0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a stored value is within a tolerance of one the test computed in double
    void HudSymbolsTestNear(FAutomationTestBase& test, const FString& what, const double actual,
        const double expected, const double tolerance = HUDSYMBOLSTEST_TOLERANCE)
    {
        test.TestTrue(FString::Printf(TEXT("%s (%.6f, expected %.6f)"), *what, actual, expected),
            FMath::Abs(actual - expected) <= tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a stored point is within HUDSYMBOLSTEST_TOLERANCE px of one the test computed in double
    void HudSymbolsTestCheckPoint(FAutomationTestBase& test, const FString& what, const FVector2f& actual,
        const FVector2D& expected)
    {
        test.TestTrue(FString::Printf(TEXT("%s (%.4f, %.4f), expected (%.4f, %.4f)"), *what, actual.X, actual.Y,
            expected.X, expected.Y), FMath::Abs(actual.X - expected.X) <= HUDSYMBOLSTEST_TOLERANCE
            && FMath::Abs(actual.Y - expected.Y) <= HUDSYMBOLSTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a primitive's kind, role, path and intensity
    void HudSymbolsTestCheckHead(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPrimKind kind, const ESOLHudPaletteRole role, const double intensity,
        const ESOLHudPrimPath path)
    {
        test.TestTrue(FString::Printf(TEXT("%s: kind %d (expected %d)"), *what, static_cast<int32>(prim.Kind),
            static_cast<int32>(kind)), prim.Kind == kind);
        test.TestTrue(FString::Printf(TEXT("%s: role %d (expected %d)"), *what, static_cast<int32>(prim.Role),
            static_cast<int32>(role)), prim.Role == role);
        test.TestTrue(FString::Printf(TEXT("%s: path %d (expected %d)"), *what, static_cast<int32>(prim.Path),
            static_cast<int32>(path)), prim.Path == path);
        HudSymbolsTestNear(test, what + TEXT(": intensity"), prim.Intensity, intensity,
            HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a Line primitive: head, ends, Canvas thickness and the half width LineHalfWidthPx gives for it
    void HudSymbolsTestCheckLine(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPaletteRole role, const double intensity, const FVector2D& a, const FVector2D& b,
        const double thicknessPx, const ESOLHudPrimPath path = ESOLHudPrimPath::Both)
    {
        HudSymbolsTestCheckHead(test, what, prim, ESOLHudPrimKind::Line, role, intensity, path);
        HudSymbolsTestCheckPoint(test, what + TEXT(": A"), prim.A, a);
        HudSymbolsTestCheckPoint(test, what + TEXT(": B"), prim.B, b);
        HudSymbolsTestNear(test, what + TEXT(": thickness"), prim.ThicknessPx, thicknessPx);
        HudSymbolsTestNear(test, what + TEXT(": half width"), prim.HalfWidthPx,
            SOLHudSymbols::LineHalfWidthPx(static_cast<float>(thicknessPx)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a Ring primitive: head, centre, radius, and (when not negative) Canvas thickness and segment count
    void HudSymbolsTestCheckRing(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPaletteRole role, const double intensity, const FVector2D& centre, const double radiusPx,
        const double thicknessPx, const int32 segments, const ESOLHudPrimPath path = ESOLHudPrimPath::Both)
    {
        HudSymbolsTestCheckHead(test, what, prim, ESOLHudPrimKind::Ring, role, intensity, path);
        HudSymbolsTestCheckPoint(test, what + TEXT(": centre"), prim.A, centre);
        HudSymbolsTestNear(test, what + TEXT(": radius"), prim.RadiusPx, radiusPx);
        if (thicknessPx >= 0.0)
        {
            HudSymbolsTestNear(test, what + TEXT(": thickness"), prim.ThicknessPx, thicknessPx);
        }
        if (segments >= 0)
        {
            test.TestEqual(what + TEXT(": segments"), prim.Segments, segments);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a Box primitive: head, top-left A and size B
    void HudSymbolsTestCheckBox(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPaletteRole role, const double intensity, const FVector2D& topLeft, const FVector2D& size,
        const ESOLHudPrimPath path = ESOLHudPrimPath::Both)
    {
        HudSymbolsTestCheckHead(test, what, prim, ESOLHudPrimKind::Box, role, intensity, path);
        HudSymbolsTestCheckPoint(test, what + TEXT(": top-left"), prim.A, topLeft);
        HudSymbolsTestCheckPoint(test, what + TEXT(": size"), prim.B, size);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts an Ellipse primitive: head, centre, X radius, squash and the Y radius they make
    void HudSymbolsTestCheckEllipse(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPaletteRole role, const double intensity, const FVector2D& centre, const double radiusPx,
        const double squashY)
    {
        HudSymbolsTestCheckHead(test, what, prim, ESOLHudPrimKind::Ellipse, role, intensity, ESOLHudPrimPath::Both);
        HudSymbolsTestCheckPoint(test, what + TEXT(": centre"), prim.A, centre);
        HudSymbolsTestNear(test, what + TEXT(": radius"), prim.RadiusPx, radiusPx);
        HudSymbolsTestNear(test, what + TEXT(": squash"), prim.SquashY, squashY, HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
        HudSymbolsTestNear(test, what + TEXT(": Y radius is radius x squash"), prim.RadiusYPx,
            static_cast<double>(prim.RadiusPx) * prim.SquashY);
        HudSymbolsTestNear(test, what + TEXT(": Y radius"), prim.RadiusYPx, radiusPx * squashY);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a DashedLine primitive: head, ends, dash and gap
    void HudSymbolsTestCheckDashed(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPaletteRole role, const double intensity, const FVector2D& a, const FVector2D& b,
        const double dashPx, const double gapPx)
    {
        HudSymbolsTestCheckHead(test, what, prim, ESOLHudPrimKind::DashedLine, role, intensity, ESOLHudPrimPath::Both);
        HudSymbolsTestCheckPoint(test, what + TEXT(": A"), prim.A, a);
        HudSymbolsTestCheckPoint(test, what + TEXT(": B"), prim.B, b);
        HudSymbolsTestNear(test, what + TEXT(": dash"), prim.DashPx, dashPx);
        HudSymbolsTestNear(test, what + TEXT(": gap"), prim.GapPx, gapPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a Dot primitive: head, centre and radius
    void HudSymbolsTestCheckDot(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPaletteRole role, const double intensity, const FVector2D& centre, const double radiusPx)
    {
        HudSymbolsTestCheckHead(test, what, prim, ESOLHudPrimKind::Dot, role, intensity, ESOLHudPrimPath::Both);
        HudSymbolsTestCheckPoint(test, what + TEXT(": centre"), prim.A, centre);
        HudSymbolsTestNear(test, what + TEXT(": radius"), prim.RadiusPx, radiusPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts an Arc primitive: head, centre, radius, half width, angles and glow pad
    void HudSymbolsTestCheckArc(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPaletteRole role, const double intensity, const FVector2D& centre, const double radiusPx,
        const double halfWidthPx, const double midAngleRad, const double halfApertureRad, const double padPx)
    {
        HudSymbolsTestCheckHead(test, what, prim, ESOLHudPrimKind::Arc, role, intensity, ESOLHudPrimPath::Both);
        HudSymbolsTestCheckPoint(test, what + TEXT(": centre"), prim.A, centre);
        HudSymbolsTestNear(test, what + TEXT(": radius"), prim.RadiusPx, radiusPx);
        HudSymbolsTestNear(test, what + TEXT(": half width"), prim.HalfWidthPx, halfWidthPx);
        HudSymbolsTestNear(test, what + TEXT(": mid angle"), prim.MidAngleRad, midAngleRad,
            HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
        HudSymbolsTestNear(test, what + TEXT(": half aperture"), prim.HalfApertureRad, halfApertureRad,
            HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
        HudSymbolsTestNear(test, what + TEXT(": pad"), prim.PadPx, padPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Empties a list and adds one sentinel primitive (a Text sample) so a builder can be seen to append after it
    void HudSymbolsTestSeed(FSOLHudSymbolPrims& prims)
    {
        prims.Reset();
        FSOLHudSymbolPrim sentinel;
        sentinel.Kind = ESOLHudPrimKind::Text;
        sentinel.A = FVector2f(-7.0f, -9.0f);
        sentinel.Text = TEXT("SENTINEL");
        prims.Add(sentinel);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a builder appended expected primitives after the sentinel, returned that count and dropped none; true
    // when the count matches (the caller then reads prims.Prims[1 ..])
    bool HudSymbolsTestCheckAppended(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrims& prims,
        const int32 returned, const int32 expected)
    {
        test.TestEqual(what + TEXT(": returns the primitives it appended"), returned, expected);
        test.TestEqual(what + TEXT(": appended after the sentinel"), prims.Count, 1 + expected);
        test.TestTrue(what + TEXT(": the sentinel kept"), prims.Count >= 1
            && prims.Prims[0].Kind == ESOLHudPrimKind::Text && prims.Prims[0].A.Equals(FVector2f(-7.0f, -9.0f)));
        test.TestEqual(what + TEXT(": nothing dropped"), prims.DroppedCount, 0);
        test.TestTrue(what + TEXT(": at most the capacity"), expected <= SOL::Help::MAX_PRIMS_PER_SYMBOL);
        return prims.Count == 1 + expected;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two primitives are the same in every field (points and sizes within the pixel tolerance)
    bool HudSymbolsTestSamePrim(const FSOLHudSymbolPrim& a, const FSOLHudSymbolPrim& b)
    {
        const float tolerance = HUDSYMBOLSTEST_TOLERANCE;
        const float fine = static_cast<float>(HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
        return a.Kind == b.Kind && a.Role == b.Role && a.Path == b.Path && a.Category == b.Category
            && a.Segments == b.Segments && a.A.Equals(b.A, tolerance) && a.B.Equals(b.B, tolerance)
            && FMath::IsNearlyEqual(a.RadiusPx, b.RadiusPx, tolerance)
            && FMath::IsNearlyEqual(a.RadiusYPx, b.RadiusYPx, tolerance)
            && FMath::IsNearlyEqual(a.HalfWidthPx, b.HalfWidthPx, tolerance)
            && FMath::IsNearlyEqual(a.MidAngleRad, b.MidAngleRad, fine)
            && FMath::IsNearlyEqual(a.HalfApertureRad, b.HalfApertureRad, fine)
            && FMath::IsNearlyEqual(a.DashPx, b.DashPx, tolerance) && FMath::IsNearlyEqual(a.GapPx, b.GapPx, tolerance)
            && FMath::IsNearlyEqual(a.Fill, b.Fill, fine) && FMath::IsNearlyEqual(a.Intensity, b.Intensity, fine)
            && FMath::IsNearlyEqual(a.UiScale, b.UiScale, fine)
            && FMath::IsNearlyEqual(a.ThicknessPx, b.ThicknessPx, tolerance)
            && FMath::IsNearlyEqual(a.SquashY, b.SquashY, fine) && FMath::IsNearlyEqual(a.PadPx, b.PadPx, tolerance);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a primitive's features are at least the glossary floor (D42): Dot / Ring / Arc / Icon /
    // EdgeArrow radius (length), both Ellipse radii, a Box's size, a Panel's half size (floor / 2), a dashed line's
    // dash and gap; lines, text, cards and frames are exempt
    bool HudSymbolsTestMeetsFloor(const FSOLHudSymbolPrim& prim)
    {
        const float floorPx = SOL::Help::GLOSSARY_MIN_FEATURE_PX - HUDSYMBOLSTEST_TOLERANCE;
        switch (prim.Kind)
        {
        case ESOLHudPrimKind::Dot:
        case ESOLHudPrimKind::Ring:
        case ESOLHudPrimKind::Arc:
        case ESOLHudPrimKind::Icon:
        case ESOLHudPrimKind::EdgeArrow:
            return prim.RadiusPx >= floorPx;
        case ESOLHudPrimKind::Ellipse:
            return prim.RadiusPx >= floorPx && prim.RadiusYPx >= floorPx;
        case ESOLHudPrimKind::Box:
            return prim.B.X >= floorPx && prim.B.Y >= floorPx;
        case ESOLHudPrimKind::Panel:
            return prim.B.X >= 0.5f * floorPx && prim.B.Y >= 0.5f * floorPx;
        case ESOLHudPrimKind::DashedLine:
            return prim.DashPx >= floorPx && prim.GapPx >= floorPx;
        default:
            return true;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the joystick radius the HUD itself draws at a UI scale on the 720 px reference canvas
    double HudSymbolsTestHudJoystickRadius(const float scale)
    {
        return SOL::JOYSTICK_RADIUS_FRACTION * SOL::Hologram::HUD_REFERENCE_HEIGHT_PX * scale;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills out with what the glossary draws for a class (a) HUD symbol (contract section 3, through the Append*
    // builders, no floor); joystickRadiusPx is the radius Build chose (decision U3 / D42 not asserted here). False for
    // a symbol that is not class (a). A deliberate copy of Build's switch: comparing against it shows Build delegates
    // to the builders, not that the builders' geometry is right (the per-builder tests and GoldenValues pin that)
    bool HudSymbolsTestGlossaryReference(const ESOLHudSymbol symbol, const FVector2f& centre, const float s,
        const float joystickRadiusPx, FSOLHudSymbolPrims& out)
    {
        out.Reset();
        const FVector2D c(centre);
        switch (symbol)
        {
        case ESOLHudSymbol::Reticle:
            SOLHudSymbols::AppendReticle(c, s, out);
            return true;
        case ESOLHudSymbol::Joystick:
        case ESOLHudSymbol::JoystickFreeLook:
        {
            const FVector2D offset(SOL::Help::GLOSSARY_STICK_OFFSET_X * joystickRadiusPx,
                SOL::Help::GLOSSARY_STICK_OFFSET_Y * joystickRadiusPx);
            SOLHudSymbols::AppendJoystick(c, joystickRadiusPx, offset, symbol == ESOLHudSymbol::JoystickFreeLook, true,
                s, out);
            return true;
        }
        case ESOLHudSymbol::SteerFree:
            SOLHudSymbols::AppendSteerIcon(c, ESOLSteerIcon::Free, true, s, out);
            return true;
        case ESOLHudSymbol::SteerHeld:
            SOLHudSymbols::AppendSteerIcon(c, ESOLSteerIcon::Held, true, s, out);
            return true;
        case ESOLHudSymbol::SteerLocked:
            SOLHudSymbols::AppendSteerIcon(c, ESOLSteerIcon::Locked, true, s, out);
            return true;
        case ESOLHudSymbol::Prograde:
            SOLHudSymbols::AppendPrograde(c, s, out);
            return true;
        case ESOLHudSymbol::Retrograde:
            SOLHudSymbols::AppendRetrograde(c, s, out);
            return true;
        case ESOLHudSymbol::TargetBracket:
            SOLHudSymbols::AppendTargetBracket(c, static_cast<float>(SOL::Help::GLOSSARY_BRACKET_HALF_PX * s), out);
            return true;
        case ESOLHudSymbol::TargetEdgeArrow:
        {
            const FVector2D direction(1.0, 0.0);
            const FVector2D anchor = c - direction * (0.5 * SOL::HudGeometry::EDGE_ARROW_LENGTH_PX * s);
            SOLHudSymbols::AppendTargetEdgeArrow(anchor, direction, s, out);
            return true;
        }
        case ESOLHudSymbol::RadarScope:
        {
            FSOLRadarScopeParams params;
            params.Center = c;
            params.UiScale = s;
            params.TimeS = SOL::Help::GLOSSARY_RADAR_TIME_S;
            params.Strength = 1.0;
            params.bHasCone = true;
            params.ConeHeadingRad = 0.0;
            params.ConeHalfFovRad = SOL::Help::GLOSSARY_RADAR_CONE_HALF_RAD;
            SOLHudSymbols::AppendRadarScope(params, out);
            return true;
        }
        case ESOLHudSymbol::RadarContactAbove:
        case ESOLHudSymbol::RadarContactBelow:
        case ESOLHudSymbol::RadarSelected:
        {
            const double halfStem = 0.5 * SOL::Help::GLOSSARY_RADAR_STEM_PX * s;
            const bool bBelow = symbol == ESOLHudSymbol::RadarContactBelow;
            FSOLRadarStyle style;
            style.Side = bBelow ? ESOLRadarSide::Below : ESOLRadarSide::Above;
            style.bDashedStem = bBelow;
            style.bHollowBlip = bBelow;
            style.bLarge = symbol == ESOLHudSymbol::RadarSelected;
            const FVector2D foot = bBelow ? c - FVector2D(0.0, halfStem) : c + FVector2D(0.0, halfStem);
            const FVector2D top = bBelow ? c + FVector2D(0.0, halfStem) : c - FVector2D(0.0, halfStem);
            SOLHudSymbols::AppendRadarContact(foot, top, style, 1.0f, s, out);
            return true;
        }
        case ESOLHudSymbol::SpeedCapBar:
        case ESOLHudSymbol::GMeter:
        {
            const float x = static_cast<float>(c.X - 0.5 * SOL::HudGeometry::SPEED_BAR_WIDTH_PX * s);
            const float y = static_cast<float>(c.Y - 0.5 * SOL::HudGeometry::SPEED_BAR_HEIGHT_PX * s);
            const bool bGMeter = symbol == ESOLHudSymbol::GMeter;
            SOLHudSymbols::AppendBar(x, y, bGMeter ? SOL::Help::GLOSSARY_G_FILL : SOL::Help::GLOSSARY_BAR_FILL,
                ESOLHudPaletteRole::Primary, s, out);
            if (bGMeter)
            {
                SOLHudSymbols::AppendChaseTicks(x, y, SOL::Help::GLOSSARY_CHASE_FLOOR, true,
                    SOL::Help::GLOSSARY_CHASE_RESUME, s, out);
            }
            return true;
        }
        case ESOLHudSymbol::WarpStreaks:
        {
            const int32 count = SOL::Help::GLOSSARY_WARP_STREAKS;
            const double halfDiagonal = SOL::Help::GLOSSARY_WARP_HALF_DIAGONAL_PX * s;
            for (int32 streak = 0; streak < count; ++streak)
            {
                const double angle = 2.0 * UE_DOUBLE_PI * streak / count;
                const FVector2D direction(FMath::Cos(angle), FMath::Sin(angle));
                SOLHudSymbols::AppendWarpStreak(c, direction, (streak + 0.5) / count, 1.0, halfDiagonal, s, out);
            }
            return true;
        }
        case ESOLHudSymbol::RadialMenu:
        {
            const int32 count = SOL::Help::GLOSSARY_RADIAL_SECTORS;
            const float halfAperture = static_cast<float>(UE_DOUBLE_PI / count
                - 0.5 * SOL::Interact::RADIAL_SECTOR_GAP_RAD);
            for (int32 sector = 0; sector < count; ++sector)
            {
                const bool bHighlighted = sector == 0;
                const bool bEnabled = sector != SOL::Help::GLOSSARY_RADIAL_DISABLED_SECTOR;
                const FSOLHoloRadialSectorLook look = SOLHoloRadial::SectorLook(halfAperture, bHighlighted, bEnabled,
                    HudSymbolsTestFullLook(), 1.0f);
                SOLHudSymbols::AppendRadialSector(centre, s,
                    static_cast<float>(SOLRadialMenu::SectorMidAngleRad(sector, count)), look, bHighlighted, out);
            }
            return true;
        }
        default:
            return false;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsNamesTest, "SOLTest.HudSymbols.Names",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every symbol has its own id that FromName reads back; unknown and wrong-case ids are refused, leaving out unchanged
bool FSOLHudSymbolsNamesTest::RunTest(const FString& /*parameters*/)
{
    for (int32 symbol = 0; symbol < HUDSYMBOLSTEST_SYMBOL_COUNT; ++symbol)
    {
        const ESOLHudSymbol value = static_cast<ESOLHudSymbol>(symbol);
        const FString id = SOLHudSymbols::ToName(value);
        TestFalse(FString::Printf(TEXT("symbol %d has an id"), symbol), id.IsEmpty());
        ESOLHudSymbol found = ESOLHudSymbol::Count;
        TestTrue(FString::Printf(TEXT("'%s' found"), *id), SOLHudSymbols::FromName(id, found));
        TestTrue(FString::Printf(TEXT("'%s' round-trips"), *id), found == value);
        for (int32 other = symbol + 1; other < HUDSYMBOLSTEST_SYMBOL_COUNT; ++other)
        {
            TestFalse(FString::Printf(TEXT("'%s' not repeated at %d"), *id, other),
                id.Equals(SOLHudSymbols::ToName(static_cast<ESOLHudSymbol>(other)), ESearchCase::IgnoreCase));
        }
    }
    TestEqual(TEXT("Count has no id"), FString(SOLHudSymbols::ToName(ESOLHudSymbol::Count)), FString());
    TestEqual(TEXT("the SDD's ids"), FString(SOLHudSymbols::ToName(ESOLHudSymbol::SteerFree)),
        FString(TEXT("SteerFree")));
    TestEqual(TEXT("the SDD's ids"), FString(SOLHudSymbols::ToName(ESOLHudSymbol::Reticle)), FString(TEXT("Reticle")));

    ESOLHudSymbol untouched = ESOLHudSymbol::Card;
    TestFalse(TEXT("unknown id refused"), SOLHudSymbols::FromName(TEXT("NoSuchSymbol"), untouched));
    TestTrue(TEXT("unknown id leaves out"), untouched == ESOLHudSymbol::Card);
    TestFalse(TEXT("wrong case refused"), SOLHudSymbols::FromName(TEXT("reticle"), untouched));
    TestTrue(TEXT("wrong case leaves out"), untouched == ESOLHudSymbol::Card);
    TestFalse(TEXT("empty id refused"), SOLHudSymbols::FromName(FStringView(), untouched));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsBuildTest, "SOLTest.HudSymbols.BuildPrimitiveCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each built symbol writes between one and MAX_PRIMS_PER_SYMBOL primitives (nothing dropped); Count and a bad scale
// write none; Build resets its output first
bool FSOLHudSymbolsBuildTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    for (const ESOLHudSymbol symbol : HUDSYMBOLSTEST_BUILT)
    {
        for (const float scale : HUDSYMBOLSTEST_SCALES)
        {
            const FString what = FString::Printf(TEXT("%s at %.2f"), *HudSymbolsTestName(symbol), scale);
            prims.Reset();
            prims.Add(HudSymbolsTestPrim(ESOLHudPrimKind::Line));
            const int32 count = SOLHudSymbols::Build(symbol, HUDSYMBOLSTEST_CENTRE, scale, prims);
            TestTrue(what + TEXT(": at least one primitive"), count >= 1);
            TestTrue(what + TEXT(": at most the capacity"), count <= SOL::Help::MAX_PRIMS_PER_SYMBOL);
            TestEqual(what + TEXT(": returns its count"), count, prims.Count);
            TestEqual(what + TEXT(": nothing dropped"), prims.DroppedCount, 0);
        }
    }

    // Count and bad scales
    const float badScales[] = { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity() };
    for (const float scale : badScales)
    {
        prims.Add(HudSymbolsTestPrim(ESOLHudPrimKind::Line));
        TestEqual(FString::Printf(TEXT("scale %f writes nothing"), scale),
            SOLHudSymbols::Build(ESOLHudSymbol::IconStar, HUDSYMBOLSTEST_CENTRE, scale, prims), 0);
        TestEqual(FString::Printf(TEXT("scale %f leaves an empty list"), scale), prims.Count, 0);
    }
    prims.Add(HudSymbolsTestPrim(ESOLHudPrimKind::Line));
    TestEqual(TEXT("Count writes nothing"), SOLHudSymbols::Build(ESOLHudSymbol::Count, HUDSYMBOLSTEST_CENTRE, 1.0f,
        prims), 0);
    TestEqual(TEXT("Count leaves an empty list"), prims.Count, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsCentreTest, "SOLTest.HudSymbols.CentredAndDeterministic",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The same input gives the same primitives, and moving the centre moves every primitive's points with it
bool FSOLHudSymbolsCentreTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f offset = HUDSYMBOLSTEST_OTHER_CENTRE - HUDSYMBOLSTEST_CENTRE;
    FSOLHudSymbolPrims first;
    FSOLHudSymbolPrims again;
    FSOLHudSymbolPrims moved;
    for (const ESOLHudSymbol symbol : HUDSYMBOLSTEST_BUILT)
    {
        const FString what = HudSymbolsTestName(symbol);
        SOLHudSymbols::Build(symbol, HUDSYMBOLSTEST_CENTRE, 1.25f, first);
        SOLHudSymbols::Build(symbol, HUDSYMBOLSTEST_CENTRE, 1.25f, again);
        SOLHudSymbols::Build(symbol, HUDSYMBOLSTEST_OTHER_CENTRE, 1.25f, moved);
        TestEqual(what + TEXT(": same count again"), again.Count, first.Count);
        TestEqual(what + TEXT(": same count when moved"), moved.Count, first.Count);
        const int32 count = FMath::Min3(first.Count, again.Count, moved.Count);
        for (int32 index = 0; index < count; ++index)
        {
            const FSOLHudSymbolPrim& a = first.Prims[index];
            const FSOLHudSymbolPrim& b = again.Prims[index];
            const FSOLHudSymbolPrim& c = moved.Prims[index];
            const FString prim = FString::Printf(TEXT("%s prim %d"), *what, index);
            TestTrue(prim + TEXT(": deterministic"), HudSymbolsTestSamePrim(a, b));
            TestTrue(prim + TEXT(": same kind when moved"), c.Kind == a.Kind);
            TestTrue(prim + TEXT(": A moves with the centre"), c.A.Equals(a.A + offset, HUDSYMBOLSTEST_TOLERANCE));
            const bool bBIsPoint = a.Kind == ESOLHudPrimKind::Line || a.Kind == ESOLHudPrimKind::DashedLine
                || a.Kind == ESOLHudPrimKind::Card || a.Kind == ESOLHudPrimKind::Frame;
            const FVector2f expectedB = bBIsPoint ? a.B + offset : a.B;
            TestTrue(prim + TEXT(": B moves only if it is a point"), c.B.Equals(expectedB, HUDSYMBOLSTEST_TOLERANCE));
            TestTrue(prim + TEXT(": sizes do not move"), FMath::IsNearlyEqual(c.RadiusPx, a.RadiusPx,
                HUDSYMBOLSTEST_TOLERANCE) && FMath::IsNearlyEqual(c.RadiusYPx, a.RadiusYPx, HUDSYMBOLSTEST_TOLERANCE)
                && FMath::IsNearlyEqual(c.ThicknessPx, a.ThicknessPx, HUDSYMBOLSTEST_TOLERANCE)
                && c.Segments == a.Segments && c.Path == a.Path && c.Role == a.Role);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsBoundsScaleTest, "SOLTest.HudSymbols.BoundsScaleLinearly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A symbol's bounds are linear in the UI scale at and above scale 1 (below it the glossary floor, D42, may raise a
// feature); Count and a bad scale have empty bounds
bool FSOLHudSymbolsBoundsScaleTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims scratch;
    for (const ESOLHudSymbol symbol : HUDSYMBOLSTEST_BUILT)
    {
        const FString what = HudSymbolsTestName(symbol);
        const FBox2f unit = SOLHudSymbols::Bounds(symbol, 1.0f, scratch);
        TestTrue(what + TEXT(": bounds at scale 1"), unit.bIsValid != 0);
        TestTrue(what + TEXT(": bounds have an area"), unit.bIsValid != 0 && unit.GetSize().X > 0.0f
            && unit.GetSize().Y > 0.0f);
        for (const float scale : HUDSYMBOLSTEST_LINEAR_SCALES)
        {
            const FBox2f scaled = SOLHudSymbols::Bounds(symbol, scale, scratch);
            const float tolerance = HUDSYMBOLSTEST_TOLERANCE * FMath::Max(1.0f, scale) * 10.0f;
            TestTrue(FString::Printf(TEXT("%s: bounds at %.2f valid"), *what, scale), scaled.bIsValid != 0);
            TestTrue(FString::Printf(TEXT("%s: min linear at %.2f"), *what, scale),
                scaled.Min.Equals(unit.Min * scale, tolerance));
            TestTrue(FString::Printf(TEXT("%s: max linear at %.2f"), *what, scale),
                scaled.Max.Equals(unit.Max * scale, tolerance));
        }
        for (const float scale : HUDSYMBOLSTEST_CONTAIN_SCALES)
        {
            TestTrue(FString::Printf(TEXT("%s: bounds at %.2f valid"), *what, scale),
                SOLHudSymbols::Bounds(symbol, scale, scratch).bIsValid != 0);
        }
        TestFalse(what + TEXT(": scale 0 empty"), SOLHudSymbols::Bounds(symbol, 0.0f, scratch).bIsValid != 0);
        TestFalse(what + TEXT(": negative scale empty"), SOLHudSymbols::Bounds(symbol, -1.0f, scratch).bIsValid != 0);
        TestFalse(what + TEXT(": NaN scale empty"),
            SOLHudSymbols::Bounds(symbol, std::numeric_limits<float>::quiet_NaN(), scratch).bIsValid != 0);
    }
    TestFalse(TEXT("Count has empty bounds"),
        SOLHudSymbols::Bounds(ESOLHudSymbol::Count, 1.0f, scratch).bIsValid != 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsBoundsContainTest, "SOLTest.HudSymbols.BoundsContainPrimitives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A symbol's bounds, placed at its centre, contain every primitive Build writes at the same scale (floored ones too)
bool FSOLHudSymbolsBoundsContainTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    FSOLHudSymbolPrims scratch;
    for (const ESOLHudSymbol symbol : HUDSYMBOLSTEST_BUILT)
    {
        for (const float scale : HUDSYMBOLSTEST_CONTAIN_SCALES)
        {
            const FBox2f bounds = SOLHudSymbols::Bounds(symbol, scale, scratch);
            const int32 count = SOLHudSymbols::Build(symbol, HUDSYMBOLSTEST_CENTRE, scale, prims);
            const float tolerance = HUDSYMBOLSTEST_TOLERANCE * FMath::Max(1.0f, scale) * 10.0f;
            for (int32 index = 0; index < count; ++index)
            {
                const FBox2f extent = HudSymbolsTestPrimExtent(prims.Prims[index]);
                const FVector2f minimum = extent.Min - HUDSYMBOLSTEST_CENTRE;
                const FVector2f maximum = extent.Max - HUDSYMBOLSTEST_CENTRE;
                TestTrue(FString::Printf(TEXT("%s at %.2f: prim %d inside the bounds"), *HudSymbolsTestName(symbol),
                    scale, index), bounds.bIsValid != 0 && minimum.X >= bounds.Min.X - tolerance
                    && minimum.Y >= bounds.Min.Y - tolerance && maximum.X <= bounds.Max.X + tolerance
                    && maximum.Y <= bounds.Max.Y + tolerance);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsIconsTest, "SOLTest.HudSymbols.IconsMatchOverlayIcons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The icon symbols are overlay icon (and edge arrow) primitives: their replay adds exactly the shapes
// SOLOverlayIcons::AddIconShapes / AddEdgeArrow add at the same centre and size (one source of geometry, D5)
bool FSOLHudSymbolsIconsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    FSOLHudSymbolPrims prims;
    FSOLHudShapeBatch actual;
    FSOLHudShapeBatch expected;
    actual.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    expected.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);

    TArray<ESOLHudSymbol> symbols;
    for (const ESOLHudSymbol symbol : HUDSYMBOLSTEST_ICONS)
    {
        symbols.Add(symbol);
    }
    symbols.Add(ESOLHudSymbol::IconEdgeArrow);
    for (const ESOLHudSymbol symbol : symbols)
    {
        for (const float scale : HUDSYMBOLSTEST_SCALES)
        {
            const FString what = FString::Printf(TEXT("%s at %.2f"), *HudSymbolsTestName(symbol), scale);
            const int32 count = SOLHudSymbols::Build(symbol, HUDSYMBOLSTEST_CENTRE, scale, prims);
            TestTrue(what + TEXT(": built"), count > 0);

            // Only builder primitives, and the symbol's own one among them
            bool bOwnPrim = false;
            bool bOnlyBuilderPrims = true;
            for (int32 index = 0; index < count; ++index)
            {
                const FSOLHudSymbolPrim& prim = prims.Prims[index];
                bOnlyBuilderPrims &= prim.Kind == ESOLHudPrimKind::Icon || prim.Kind == ESOLHudPrimKind::EdgeArrow;
                if (symbol == ESOLHudSymbol::IconEdgeArrow)
                {
                    bOwnPrim |= prim.Kind == ESOLHudPrimKind::EdgeArrow;
                }
                else
                {
                    const int32 category = static_cast<int32>(symbol) - static_cast<int32>(ESOLHudSymbol::IconStar);
                    bOwnPrim |= prim.Kind == ESOLHudPrimKind::Icon && static_cast<int32>(prim.Category) == category
                        && prim.A.Equals(HUDSYMBOLSTEST_CENTRE, HUDSYMBOLSTEST_TOLERANCE) && prim.RadiusPx > 0.0f;
                }
            }
            TestTrue(what + TEXT(": only overlay icon builder primitives"), bOnlyBuilderPrims);
            TestTrue(what + TEXT(": its own icon (or arrow) at the centre"), bOwnPrim);

            // The replay equals the builders called with the same points and sizes
            actual.Reset();
            expected.Reset();
            const int32 added = SOLHudSymbols::AddToBatch(prims, actual, palette, HUDSYMBOLSTEST_PAD_PX);
            for (int32 index = 0; index < count; ++index)
            {
                const FSOLHudSymbolPrim& prim = prims.Prims[index];
                if (prim.Kind == ESOLHudPrimKind::Icon)
                {
                    SOLOverlayIcons::AddIconShapes(expected, prim.Category, prim.A, prim.RadiusPx,
                        SOLOverlayIcons::CategoryColor(prim.Category), prim.Intensity, HUDSYMBOLSTEST_PAD_PX);
                }
                else if (prim.Kind == ESOLHudPrimKind::EdgeArrow)
                {
                    SOLOverlayIcons::AddEdgeArrow(expected, prim.A, prim.B, prim.RadiusPx, SOL::HudPalette::PRIMARY,
                        prim.Intensity, HUDSYMBOLSTEST_PAD_PX);
                }
            }
            TestEqual(what + TEXT(": returns the shapes added"), added, actual.GetShapeCount());
            TestTrue(what + TEXT(": adds shapes"), added > 0);
            HudSymbolsTestCheckSameShapes(*this, actual, expected, what);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsCardTest, "SOLTest.HudSymbols.CardUsesBuildCard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The Card symbol is a card primitive around a content rect, replayed through SOLHoloFrameBuild::BuildCard
bool FSOLHudSymbolsCardTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    FSOLHudSymbolPrims prims;
    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    for (const float scale : HUDSYMBOLSTEST_SCALES)
    {
        const FString what = FString::Printf(TEXT("Card at %.2f"), scale);
        const int32 count = SOLHudSymbols::Build(ESOLHudSymbol::Card, HUDSYMBOLSTEST_CENTRE, scale, prims);
        int32 cards = 0;
        for (int32 index = 0; index < count; ++index)
        {
            const FSOLHudSymbolPrim& prim = prims.Prims[index];
            if (prim.Kind != ESOLHudPrimKind::Card)
            {
                continue;
            }
            ++cards;
            TestTrue(what + TEXT(": a content rect"), prim.B.X > prim.A.X && prim.B.Y > prim.A.Y);
            TestTrue(what + TEXT(": around the centre"), HUDSYMBOLSTEST_CENTRE.X > prim.A.X
                && HUDSYMBOLSTEST_CENTRE.X < prim.B.X && HUDSYMBOLSTEST_CENTRE.Y > prim.A.Y
                && HUDSYMBOLSTEST_CENTRE.Y < prim.B.Y);

            // The card alone, through the replay
            FSOLHudSymbolPrims single;
            single.Add(prim);
            batch.Reset();
            const int32 added = SOLHudSymbols::AddToBatch(single, batch, palette, HUDSYMBOLSTEST_PAD_PX);
            TestEqual(what + TEXT(": returns the shapes added"), added, batch.GetShapeCount());
            HudSymbolsTestCheckCardCount(*this, prim, added, what);

            // Through a caller's frame output: the same shapes
            FSOLHudShapeBatch scratchBatch;
            scratchBatch.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
            FSOLHoloFrameOutput cardScratch;
            TestEqual(what + TEXT(": the scratch replay adds as many"),
                SOLHudSymbols::AddToBatch(single, scratchBatch, palette, HUDSYMBOLSTEST_PAD_PX, cardScratch), added);
            HudSymbolsTestCheckSameShapes(*this, scratchBatch, batch, what + TEXT(" with a scratch"));
        }
        TestTrue(what + TEXT(": holds a card primitive"), cards >= 1);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsReplayTest, "SOLTest.HudSymbols.AddToBatchShapeCounts",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AddToBatch adds one shape per Line ... Panel primitive (a ShapeOnly one too), the builder's shapes for Icon /
// EdgeArrow / Card / Frame, none for Text, and never resets the batch
bool FSOLHudSymbolsReplayTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    FSOLHudSymbolPrims prims;

    // One shape per simple primitive, whatever its path (the menus draw both)
    const ESOLHudPrimKind simple[] = { ESOLHudPrimKind::Line, ESOLHudPrimKind::Ring, ESOLHudPrimKind::Arc,
        ESOLHudPrimKind::Box, ESOLHudPrimKind::Dot, ESOLHudPrimKind::DashedLine, ESOLHudPrimKind::Ellipse,
        ESOLHudPrimKind::Panel };
    for (const ESOLHudPrimKind kind : simple)
    {
        for (const ESOLHudPrimPath path : { ESOLHudPrimPath::Both, ESOLHudPrimPath::ShapeOnly })
        {
            FSOLHudSymbolPrim prim = HudSymbolsTestPrim(kind);
            prim.Path = path;
            prims.Reset();
            prims.Add(prim);
            batch.Reset();
            const int32 added = SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX);
            const FString what = FString::Printf(TEXT("kind %d path %d"), static_cast<int32>(kind),
                static_cast<int32>(path));
            TestEqual(what + TEXT(": one shape"), added, 1);
            TestEqual(what + TEXT(": in the batch"), batch.GetShapeCount(), 1);
        }
    }

    // A dashed line is one dashed shape whose period is its dash and its gap
    FSOLHudShapeBatch dashed;
    dashed.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    const FSOLHudSymbolPrim dash = HudSymbolsTestPrim(ESOLHudPrimKind::DashedLine);
    dashed.AddDashedLine(dash.A, dash.B, SOL::Hologram::HUD_LINE_HALF_WIDTH_PX, dash.DashPx + dash.GapPx,
        SOL::HudPalette::PRIMARY, dash.Intensity, HUDSYMBOLSTEST_PAD_PX);
    prims.Reset();
    prims.Add(dash);
    batch.Reset();
    SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX);
    HudSymbolsTestCheckSameShapes(*this, batch, dashed, TEXT("dashed line"));

    // Text adds no shape
    prims.Reset();
    prims.Add(HudSymbolsTestPrim(ESOLHudPrimKind::Text));
    batch.Reset();
    TestEqual(TEXT("text adds no shape"), SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX), 0);
    TestEqual(TEXT("text leaves the batch empty"), batch.GetShapeCount(), 0);

    // An icon adds what AddIconShapes adds; an edge arrow two shapes
    FSOLHudShapeBatch reference;
    reference.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    const FSOLHudSymbolPrim icon = HudSymbolsTestPrim(ESOLHudPrimKind::Icon);
    const int32 iconShapes = SOLOverlayIcons::AddIconShapes(reference, icon.Category, icon.A, icon.RadiusPx,
        SOLOverlayIcons::CategoryColor(icon.Category), icon.Intensity, HUDSYMBOLSTEST_PAD_PX);
    prims.Reset();
    prims.Add(icon);
    batch.Reset();
    TestEqual(TEXT("icon: the builder's shapes"), SOLHudSymbols::AddToBatch(prims, batch, palette,
        HUDSYMBOLSTEST_PAD_PX), iconShapes);
    reference.Reset();
    const FSOLHudSymbolPrim arrow = HudSymbolsTestPrim(ESOLHudPrimKind::EdgeArrow);
    const int32 arrowShapes = SOLOverlayIcons::AddEdgeArrow(reference, arrow.A, arrow.B, arrow.RadiusPx,
        SOL::HudPalette::PRIMARY, arrow.Intensity, HUDSYMBOLSTEST_PAD_PX);
    TestEqual(TEXT("the reference arrow is two lines"), arrowShapes, 2);
    prims.Reset();
    prims.Add(arrow);
    batch.Reset();
    TestEqual(TEXT("edge arrow: the builder's shapes"), SOLHudSymbols::AddToBatch(prims, batch, palette,
        HUDSYMBOLSTEST_PAD_PX), arrowShapes);

    // A card adds BuildCard's shapes
    const FSOLHudSymbolPrim card = HudSymbolsTestPrim(ESOLHudPrimKind::Card);
    prims.Reset();
    prims.Add(card);
    batch.Reset();
    HudSymbolsTestCheckCardCount(*this, card, SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX),
        TEXT("card primitive"));

    // A frame adds BuildFrame's shapes for its rect and UI scale
    const FSOLHudSymbolPrim frame = HudSymbolsTestPrim(ESOLHudPrimKind::Frame);
    FSOLHoloFrameSpec spec;
    spec.Rect = FBox2f(frame.A, frame.B);
    spec.UiScale = frame.UiScale;
    FSOLHoloFrameOutput frameOutput;
    const int32 frameShapes = SOLHoloFrameBuild::BuildFrame(spec, HudSymbolsTestFullLook(), palette.Strength,
        frameOutput);
    TestTrue(TEXT("BuildFrame draws the reference frame"), frameShapes > 0);
    prims.Reset();
    prims.Add(frame);
    batch.Reset();
    TestEqual(TEXT("frame: BuildFrame's shapes"), SOLHudSymbols::AddToBatch(prims, batch, palette,
        HUDSYMBOLSTEST_PAD_PX), frameShapes);

    // A mixed list, replayed twice without a reset, accumulates
    prims.Reset();
    for (const ESOLHudPrimKind kind : simple)
    {
        prims.Add(HudSymbolsTestPrim(kind));
    }
    prims.Add(HudSymbolsTestPrim(ESOLHudPrimKind::Text));
    prims.Add(icon);
    batch.Reset();
    const int32 first = SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX);
    TestEqual(TEXT("mixed: eight simple shapes and the icon's"), first,
        static_cast<int32>(UE_ARRAY_COUNT(simple)) + iconShapes);
    const int32 second = SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX);
    TestEqual(TEXT("mixed: the same again"), second, first);
    TestEqual(TEXT("mixed: no reset in between"), batch.GetShapeCount(), first + second);

    // An empty list adds nothing
    prims.Reset();
    TestEqual(TEXT("empty list"), SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsCapacityTest, "SOLTest.HudSymbols.NothingDroppedInGlossaryBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A glossary view's batch (GLOSSARY_MAX_SHAPES) takes every built symbol and a full primitive list without dropping
bool FSOLHudSymbolsCapacityTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    FSOLHudSymbolPrims prims;
    for (const ESOLHudSymbol symbol : HUDSYMBOLSTEST_BUILT)
    {
        const FString what = HudSymbolsTestName(symbol);
        SOLHudSymbols::Build(symbol, HUDSYMBOLSTEST_CENTRE, 1.0f, prims);
        batch.Reset();
        const int32 added = SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX);
        TestTrue(what + TEXT(": adds shapes"), added > 0);
        TestEqual(what + TEXT(": nothing dropped"), batch.GetDroppedCount(), 0);
        TestEqual(what + TEXT(": returns the shapes added"), added, batch.GetShapeCount());
    }

    // A full list of simple primitives fits the glossary batch
    prims.Reset();
    for (int32 index = 0; index < SOL::Help::MAX_PRIMS_PER_SYMBOL; ++index)
    {
        prims.Add(HudSymbolsTestPrim(ESOLHudPrimKind::Line));
    }
    batch.Reset();
    TestEqual(TEXT("a full list: one shape each"), SOLHudSymbols::AddToBatch(prims, batch, palette,
        HUDSYMBOLSTEST_PAD_PX), SOL::Help::MAX_PRIMS_PER_SYMBOL);
    TestEqual(TEXT("a full list: nothing dropped"), batch.GetDroppedCount(), 0);
    TestTrue(TEXT("a full list fits the glossary batch"),
        SOL::Help::MAX_PRIMS_PER_SYMBOL <= SOL::Help::GLOSSARY_MAX_SHAPES);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsPrimsTest, "SOLTest.HudSymbols.PrimsCapacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The primitive list holds MAX_PRIMS_PER_SYMBOL and counts what it drops past that; Reset empties both counts
bool FSOLHudSymbolsPrimsTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    for (int32 index = 0; index < SOL::Help::MAX_PRIMS_PER_SYMBOL; ++index)
    {
        TestTrue(FString::Printf(TEXT("prim %d taken"), index), prims.Add(HudSymbolsTestPrim(ESOLHudPrimKind::Dot)));
    }
    TestFalse(TEXT("one past the capacity refused"), prims.Add(HudSymbolsTestPrim(ESOLHudPrimKind::Dot)));
    TestEqual(TEXT("full"), prims.Count, SOL::Help::MAX_PRIMS_PER_SYMBOL);
    TestEqual(TEXT("one dropped"), prims.DroppedCount, 1);
    prims.Reset();
    TestEqual(TEXT("reset count"), prims.Count, 0);
    TestEqual(TEXT("reset dropped"), prims.DroppedCount, 0);

    // The new fields' defaults keep a 33a primitive unchanged (contract 1.2)
    const FSOLHudSymbolPrim prim;
    TestEqual(TEXT("default thickness"), prim.ThicknessPx, 1.0f);
    TestEqual(TEXT("default segments"), prim.Segments, 0);
    TestEqual(TEXT("default squash"), prim.SquashY, 1.0f);
    TestEqual(TEXT("default pad"), prim.PadPx, 0.0f);
    TestTrue(TEXT("default path"), prim.Path == ESOLHudPrimPath::Both);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsPaletteTest, "SOLTest.HudSymbols.HudPalette",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The HUD palette gives each role its SOL::HudPalette colour at strength 1 (a dim element is a role at the dim
// intensity, so no role is a pre-dimmed colour)
bool FSOLHudSymbolsPaletteTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    TestEqual(TEXT("strength 1"), palette.Strength, 1.0f);
    TestTrue(TEXT("primary"), palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Primary)]
        == SOL::HudPalette::PRIMARY);
    TestTrue(TEXT("secondary"), palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Secondary)]
        == SOL::HudPalette::SECONDARY);
    TestTrue(TEXT("good"), palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Good)] == SOL::HudPalette::GOOD);
    TestTrue(TEXT("hostile"), palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Hostile)]
        == SOL::HudPalette::HOSTILE);
    TestTrue(TEXT("free-look"), palette.Colors[static_cast<int32>(ESOLHudPaletteRole::FreeLook)]
        == SOL::HudPalette::FREE_LOOK);
    TestTrue(TEXT("bad"), palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Bad)] == SOL::HudPalette::BAD);
    TestTrue(TEXT("warp streak"), palette.Colors[static_cast<int32>(ESOLHudPaletteRole::WarpStreak)]
        == SOL::HudPalette::WARP_STREAK);
    for (int32 role = 0; role < static_cast<int32>(ESOLHudPaletteRole::Count); ++role)
    {
        const FLinearColor& color = palette.Colors[role];
        TestTrue(FString::Printf(TEXT("role %d is not transparent"), role),
            color.A > 0.0f && (color.R > 0.0f || color.G > 0.0f || color.B > 0.0f));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsReticleTest, "SOLTest.HudSymbols.Reticle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendReticle appends four arms from the gap to the arm's end (Canvas width 1.5) and a ShapeOnly dot ring
bool FSOLHudSymbolsReticleTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        const FString what = FString::Printf(TEXT("reticle at %.2f"), s);
        const FVector2D c = HUDSYMBOLSTEST_CENTRE_D;
        HudSymbolsTestSeed(prims);
        const int32 added = SOLHudSymbols::AppendReticle(c, s, prims);
        if (!HudSymbolsTestCheckAppended(*this, what, prims, added, 5))
        {
            continue;
        }
        const double gap = SOL::HudGeometry::RETICLE_GAP_PX * s;
        const double arm = gap + SOL::HudGeometry::RETICLE_ARM_PX * s;
        const double thick = SOL::HudGeometry::CANVAS_LINE_PX;
        const ESOLHudPaletteRole primary = ESOLHudPaletteRole::Primary;
        HudSymbolsTestCheckLine(*this, what + TEXT(" left arm"), prims.Prims[1], primary, 1.0,
            FVector2D(c.X - arm, c.Y), FVector2D(c.X - gap, c.Y), thick);
        HudSymbolsTestCheckLine(*this, what + TEXT(" right arm"), prims.Prims[2], primary, 1.0,
            FVector2D(c.X + gap, c.Y), FVector2D(c.X + arm, c.Y), thick);
        HudSymbolsTestCheckLine(*this, what + TEXT(" top arm"), prims.Prims[3], primary, 1.0,
            FVector2D(c.X, c.Y - arm), FVector2D(c.X, c.Y - gap), thick);
        HudSymbolsTestCheckLine(*this, what + TEXT(" bottom arm"), prims.Prims[4], primary, 1.0,
            FVector2D(c.X, c.Y + gap), FVector2D(c.X, c.Y + arm), thick);
        HudSymbolsTestCheckRing(*this, what + TEXT(" dot"), prims.Prims[5], primary,
            SOL::HudGeometry::RETICLE_DOT_INTENSITY, c, SOL::HudGeometry::RETICLE_DOT_RADIUS_PX * s, -1.0,
            INDEX_NONE, ESOLHudPrimPath::ShapeOnly);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsJoystickTest, "SOLTest.HudSymbols.Joystick",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendJoystick: two faint rings (the stick circle and the dead zone), a dim line to the stick only past the dead
// zone, and the stick dot (dimmed when free-looking or not steering); free look recolours it all; the screen Y of the
// stick is the offset's negative; the geometry follows the radius it is given, the HUD's own at its default
bool FSOLHudSymbolsJoystickTest::RunTest(const FString& /*parameters*/)
{
    const double deadZone = SOL::JOYSTICK_DEAD_ZONE;
    const FVector2D unit(0.6, 0.8);
    FSOLHudSymbolPrims prims;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        const float radius = static_cast<float>(HudSymbolsTestHudJoystickRadius(s));
        // Offsets: none, just inside the dead zone, just outside it, and well out (line expected for the last two)
        const FVector2D offsets[] = { FVector2D::ZeroVector, unit * (radius * deadZone * 0.98),
            unit * (radius * deadZone * 1.02), FVector2D(0.5 * radius, 0.25 * radius) };
        const bool bLines[] = { false, false, true, true };
        for (int32 offsetIndex = 0; offsetIndex < static_cast<int32>(UE_ARRAY_COUNT(offsets)); ++offsetIndex)
        {
            for (int32 variant = 0; variant < 4; ++variant)
            {
                const bool bFreeLooking = (variant & 1) != 0;
                const bool bSteering = (variant & 2) != 0;
                const FVector2D& offset = offsets[offsetIndex];
                const FString what = FString::Printf(TEXT("joystick at %.2f offset %d free %d steer %d"), s,
                    offsetIndex, bFreeLooking ? 1 : 0, bSteering ? 1 : 0);
                const FVector2D c = HUDSYMBOLSTEST_CENTRE_D;
                HudSymbolsTestSeed(prims);
                const int32 added = SOLHudSymbols::AppendJoystick(c, radius, offset, bFreeLooking, bSteering, s,
                    prims);
                const bool bLine = bLines[offsetIndex];
                if (!HudSymbolsTestCheckAppended(*this, what, prims, added, bLine ? 4 : 3))
                {
                    continue;
                }
                const ESOLHudPaletteRole role = bFreeLooking ? ESOLHudPaletteRole::FreeLook
                    : ESOLHudPaletteRole::Primary;
                const double thin = SOL::HudGeometry::CANVAS_LINE_THIN_PX;
                const int32 segments = SOL::HudGeometry::CIRCLE_SEGMENTS;
                HudSymbolsTestCheckRing(*this, what + TEXT(" circle"), prims.Prims[1], role,
                    SOL::HudGeometry::FAINT_INTENSITY, c, radius, thin, segments);
                HudSymbolsTestCheckRing(*this, what + TEXT(" dead zone"), prims.Prims[2], role,
                    SOL::HudGeometry::FAINT_INTENSITY, c, radius * deadZone, thin, segments / 2);
                const FVector2D stick(c.X + offset.X, c.Y - offset.Y);
                int32 next = 3;
                if (bLine)
                {
                    HudSymbolsTestCheckLine(*this, what + TEXT(" line"), prims.Prims[next++], role,
                        SOL::HudPalette::DIM_INTENSITY, c, stick, thin);
                }
                const double dotIntensity = (bFreeLooking || !bSteering ? SOL::HudPalette::DIM_INTENSITY : 1.0)
                    * SOL::HudGeometry::STICK_DOT_INTENSITY;
                const double dot = SOL::HudGeometry::STICK_DOT_PX * s;
                HudSymbolsTestCheckBox(*this, what + TEXT(" dot"), prims.Prims[next], role, dotIntensity,
                    FVector2D(stick.X - 0.5 * dot, stick.Y - 0.5 * dot), FVector2D(dot, dot));
            }
        }

        // The radius scales the circle, the dead zone and the stick position linearly (the dot keeps its size)
        FSOLHudSymbolPrims doubled;
        const FVector2D offset(0.5 * radius, 0.25 * radius);
        prims.Reset();
        SOLHudSymbols::AppendJoystick(HUDSYMBOLSTEST_CENTRE_D, radius, offset, false, true, s, prims);
        SOLHudSymbols::AppendJoystick(HUDSYMBOLSTEST_CENTRE_D, 2.0f * radius, offset * 2.0, false, true, s, doubled);
        const FString what = FString::Printf(TEXT("joystick at %.2f, radius doubled"), s);
        if (TestEqual(what + TEXT(": same count"), doubled.Count, prims.Count) && prims.Count == 4)
        {
            HudSymbolsTestNear(*this, what + TEXT(": circle"), doubled.Prims[0].RadiusPx,
                2.0 * prims.Prims[0].RadiusPx);
            HudSymbolsTestNear(*this, what + TEXT(": dead zone"), doubled.Prims[1].RadiusPx,
                2.0 * prims.Prims[1].RadiusPx);
            const FVector2D stick = FVector2D(prims.Prims[2].B) - HUDSYMBOLSTEST_CENTRE_D;
            HudSymbolsTestCheckPoint(*this, what + TEXT(": stick"), doubled.Prims[2].B,
                HUDSYMBOLSTEST_CENTRE_D + stick * 2.0);
            HudSymbolsTestCheckPoint(*this, what + TEXT(": dot size kept"), doubled.Prims[3].B,
                FVector2D(prims.Prims[3].B));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsSteerTest, "SOLTest.HudSymbols.SteerIcons",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendSteerIcon: Free is an arrow of three lines, Held a ring and an unscaled 2 px dot, Locked adds a corner tick;
// all Primary, dimmed when not steering
bool FSOLHudSymbolsSteerTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const ESOLHudPaletteRole primary = ESOLHudPaletteRole::Primary;
    const double thick = SOL::HudGeometry::CANVAS_LINE_PX;
    const double dotPx = SOL::HudGeometry::STEER_DOT_PX;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        for (const bool bSteering : { true, false })
        {
            const FVector2D c = HUDSYMBOLSTEST_CENTRE_D;
            const double r = SOL::HudGeometry::STEER_ICON_RADIUS_PX * s;
            const double k = bSteering ? 1.0 : SOL::HudPalette::DIM_INTENSITY;

            // Free: tip, bottom and side of the arrow
            FString what = FString::Printf(TEXT("steer free at %.2f steering %d"), s, bSteering ? 1 : 0);
            HudSymbolsTestSeed(prims);
            int32 added = SOLHudSymbols::AppendSteerIcon(c, ESOLSteerIcon::Free, bSteering, s, prims);
            if (HudSymbolsTestCheckAppended(*this, what, prims, added, 3))
            {
                const FVector2D tip(c.X - r * SOL::HudGeometry::STEER_ARROW_TIP_X_FRACTION, c.Y - r);
                const FVector2D bottom(tip.X, tip.Y + r * SOL::HudGeometry::STEER_ARROW_HEIGHT_FRACTION);
                const FVector2D side(tip.X + r * SOL::HudGeometry::STEER_ARROW_SIDE_FRACTION,
                    tip.Y + r * SOL::HudGeometry::STEER_ARROW_SIDE_FRACTION);
                HudSymbolsTestCheckLine(*this, what + TEXT(" back"), prims.Prims[1], primary, k, tip, bottom, thick);
                HudSymbolsTestCheckLine(*this, what + TEXT(" top"), prims.Prims[2], primary, k, tip, side, thick);
                HudSymbolsTestCheckLine(*this, what + TEXT(" foot"), prims.Prims[3], primary, k, bottom, side, thick);
            }

            // Held and Locked: the ring and the unscaled centre dot, then Locked's tick
            for (const ESOLSteerIcon icon : { ESOLSteerIcon::Held, ESOLSteerIcon::Locked })
            {
                const bool bLocked = icon == ESOLSteerIcon::Locked;
                what = FString::Printf(TEXT("steer %s at %.2f steering %d"), bLocked ? TEXT("locked") : TEXT("held"),
                    s, bSteering ? 1 : 0);
                HudSymbolsTestSeed(prims);
                added = SOLHudSymbols::AppendSteerIcon(c, icon, bSteering, s, prims);
                if (!HudSymbolsTestCheckAppended(*this, what, prims, added, bLocked ? 3 : 2))
                {
                    continue;
                }
                HudSymbolsTestCheckRing(*this, what + TEXT(" ring"), prims.Prims[1], primary, k, c, r, thick,
                    SOL::HudGeometry::STEER_ICON_SEGMENTS);
                HudSymbolsTestCheckBox(*this, what + TEXT(" dot"), prims.Prims[2], primary,
                    k * SOL::HudGeometry::STICK_DOT_INTENSITY, FVector2D(c.X - 0.5 * dotPx, c.Y - 0.5 * dotPx),
                    FVector2D(dotPx, dotPx));
                if (bLocked)
                {
                    const double offset = r * SOL::HudGeometry::STEER_LOCK_OFFSET_FRACTION;
                    const double tick = r * SOL::HudGeometry::STEER_LOCK_TICK_FRACTION;
                    HudSymbolsTestCheckBox(*this, what + TEXT(" tick"), prims.Prims[3], primary, k,
                        FVector2D(c.X + offset, c.Y + offset), FVector2D(tick, tick));
                }
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsVelocityTest, "SOLTest.HudSymbols.VelocityMarkers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendPrograde: a Good ring with three ticks out to 2r (left, right, up); AppendRetrograde: a Hostile ring with a
// cross of arms 0.7r
bool FSOLHudSymbolsVelocityTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const double thick = SOL::HudGeometry::CANVAS_LINE_PX;
    const int32 segments = SOL::HudGeometry::VELOCITY_MARKER_SEGMENTS;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        const FVector2D p = HUDSYMBOLSTEST_CENTRE_D;
        const double r = SOL::HudGeometry::VELOCITY_MARKER_RADIUS_PX * s;
        const double end = r * SOL::HudGeometry::PROGRADE_TICK_END_FRACTION;
        const ESOLHudPaletteRole good = ESOLHudPaletteRole::Good;

        FString what = FString::Printf(TEXT("prograde at %.2f"), s);
        HudSymbolsTestSeed(prims);
        int32 added = SOLHudSymbols::AppendPrograde(p, s, prims);
        if (HudSymbolsTestCheckAppended(*this, what, prims, added, 4))
        {
            HudSymbolsTestCheckRing(*this, what + TEXT(" ring"), prims.Prims[1], good, 1.0, p, r, thick, segments);
            HudSymbolsTestCheckLine(*this, what + TEXT(" left tick"), prims.Prims[2], good, 1.0,
                FVector2D(p.X - r, p.Y), FVector2D(p.X - end, p.Y), thick);
            HudSymbolsTestCheckLine(*this, what + TEXT(" right tick"), prims.Prims[3], good, 1.0,
                FVector2D(p.X + r, p.Y), FVector2D(p.X + end, p.Y), thick);
            HudSymbolsTestCheckLine(*this, what + TEXT(" top tick"), prims.Prims[4], good, 1.0,
                FVector2D(p.X, p.Y - r), FVector2D(p.X, p.Y - end), thick);
        }

        what = FString::Printf(TEXT("retrograde at %.2f"), s);
        HudSymbolsTestSeed(prims);
        added = SOLHudSymbols::AppendRetrograde(p, s, prims);
        if (HudSymbolsTestCheckAppended(*this, what, prims, added, 3))
        {
            const double arm = r * SOL::HudGeometry::RETROGRADE_ARM_FRACTION;
            const ESOLHudPaletteRole hostile = ESOLHudPaletteRole::Hostile;
            HudSymbolsTestCheckRing(*this, what + TEXT(" ring"), prims.Prims[1], hostile, 1.0, p, r, thick, segments);
            HudSymbolsTestCheckLine(*this, what + TEXT(" falling arm"), prims.Prims[2], hostile, 1.0,
                FVector2D(p.X - arm, p.Y - arm), FVector2D(p.X + arm, p.Y + arm), thick);
            HudSymbolsTestCheckLine(*this, what + TEXT(" rising arm"), prims.Prims[3], hostile, 1.0,
                FVector2D(p.X - arm, p.Y + arm), FVector2D(p.X + arm, p.Y - arm), thick);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsBracketTest, "SOLTest.HudSymbols.TargetBracket",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendTargetBracket: eight Secondary legs, two per corner in the order (-,-), (+,-), (-,+), (+,+), each leg
// BRACKET_CORNER_FRACTION of the half size long, from the corner's tip inward
bool FSOLHudSymbolsBracketTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const double halves[] = { SOL::HudGeometry::BRACKET_MIN_HALF_PX, 28.5, SOL::HudGeometry::BRACKET_MAX_HALF_PX };
    for (const double half : halves)
    {
        const FString what = FString::Printf(TEXT("bracket half %.2f"), half);
        const FVector2D p = HUDSYMBOLSTEST_CENTRE_D;
        HudSymbolsTestSeed(prims);
        const int32 added = SOLHudSymbols::AppendTargetBracket(p, static_cast<float>(half), prims);
        if (!HudSymbolsTestCheckAppended(*this, what, prims, added, 8))
        {
            continue;
        }
        const double corner = half * SOL::HudGeometry::BRACKET_CORNER_FRACTION;
        for (int32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
        {
            const double sx = (cornerIndex & 1) != 0 ? 1.0 : -1.0;
            const double sy = (cornerIndex & 2) != 0 ? 1.0 : -1.0;
            const FVector2D tip(p.X + sx * half, p.Y + sy * half);
            const FString at = FString::Printf(TEXT("%s corner %d"), *what, cornerIndex);
            HudSymbolsTestCheckLine(*this, at + TEXT(" horizontal"), prims.Prims[1 + 2 * cornerIndex],
                ESOLHudPaletteRole::Secondary, SOL::HudGeometry::BRACKET_INTENSITY, tip,
                FVector2D(tip.X - sx * corner, tip.Y), SOL::HudGeometry::CANVAS_LINE_PX);
            HudSymbolsTestCheckLine(*this, at + TEXT(" vertical"), prims.Prims[2 + 2 * cornerIndex],
                ESOLHudPaletteRole::Secondary, SOL::HudGeometry::BRACKET_INTENSITY, tip,
                FVector2D(tip.X, tip.Y - sy * corner), SOL::HudGeometry::CANVAS_LINE_PX);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsEdgeArrowTest, "SOLTest.HudSymbols.TargetEdgeArrow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendTargetEdgeArrow: a closed Secondary triangle (Canvas width 2) from the tip ahead of the anchor to the two
// sides across it
bool FSOLHudSymbolsEdgeArrowTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const FVector2D directions[] = { FVector2D(1.0, 0.0), FVector2D(0.0, -1.0), FVector2D(0.6, 0.8) };
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        for (const FVector2D& direction : directions)
        {
            const FString what = FString::Printf(TEXT("edge arrow at %.2f toward (%.1f, %.1f)"), s, direction.X,
                direction.Y);
            const FVector2D anchor = HUDSYMBOLSTEST_CENTRE_D;
            HudSymbolsTestSeed(prims);
            const int32 added = SOLHudSymbols::AppendTargetEdgeArrow(anchor, direction, s, prims);
            if (!HudSymbolsTestCheckAppended(*this, what, prims, added, 3))
            {
                continue;
            }
            const FVector2D perp(-direction.Y, direction.X);
            const FVector2D tip = anchor + direction * (SOL::HudGeometry::EDGE_ARROW_LENGTH_PX * s);
            const FVector2D left = anchor + perp * (SOL::HudGeometry::EDGE_ARROW_WIDTH_PX * s);
            const FVector2D right = anchor - perp * (SOL::HudGeometry::EDGE_ARROW_WIDTH_PX * s);
            const double thick = SOL::HudGeometry::CANVAS_LINE_THICK_PX;
            const ESOLHudPaletteRole secondary = ESOLHudPaletteRole::Secondary;
            const double intensity = SOL::HudGeometry::BRACKET_INTENSITY;
            HudSymbolsTestCheckLine(*this, what + TEXT(" left side"), prims.Prims[1], secondary, intensity, tip, left,
                thick);
            HudSymbolsTestCheckLine(*this, what + TEXT(" right side"), prims.Prims[2], secondary, intensity, tip,
                right, thick);
            HudSymbolsTestCheckLine(*this, what + TEXT(" base"), prims.Prims[3], secondary, intensity, left, right,
                thick);
            TestTrue(what + TEXT(": closed (the base joins the sides' far ends)"),
                prims.Prims[3].A.Equals(prims.Prims[1].B, HUDSYMBOLSTEST_TOLERANCE)
                && prims.Prims[3].B.Equals(prims.Prims[2].B, HUDSYMBOLSTEST_TOLERANCE));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsRadarScopeTest, "SOLTest.HudSymbols.RadarScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendRadarScope: the sonar range ellipses (the outer one brighter), the two axes through ScopeToScreen, three dim
// side ticks, the heading tick down to the disc's top, the cone's two edges when it has one (left first), and the
// ship chevron
bool FSOLHudSymbolsRadarScopeTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const int32 ringCount = static_cast<int32>(UE_ARRAY_COUNT(SOL::HudGeometry::RADAR_RINGS));
    const ESOLHudPaletteRole primary = ESOLHudPaletteRole::Primary;
    const double thin = SOL::HudGeometry::CANVAS_LINE_THIN_PX;
    const double heading = 0.4;
    const double halfFov = 0.6;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        for (const bool bCone : { false, true })
        {
            for (const double timeS : { 0.0, 1.37 })
            {
                for (const double strength : { 0.0, 1.0 })
                {
                    const FString what = FString::Printf(TEXT("radar at %.2f cone %d t %.2f strength %.0f"), s,
                        bCone ? 1 : 0, timeS, strength);
                    const FVector2D c = HUDSYMBOLSTEST_CENTRE_D;
                    FSOLRadarScopeParams params;
                    params.Center = c;
                    params.UiScale = s;
                    params.TimeS = timeS;
                    params.Strength = strength;
                    params.bHasCone = bCone;
                    params.ConeHeadingRad = heading;
                    params.ConeHalfFovRad = halfFov;
                    HudSymbolsTestSeed(prims);
                    const int32 added = SOLHudSymbols::AppendRadarScope(params, prims);
                    if (!HudSymbolsTestCheckAppended(*this, what, prims, added, ringCount + 8 + (bCone ? 2 : 0)))
                    {
                        continue;
                    }
                    const double radius = SOL::HudGeometry::RADAR_RADIUS_PX * s;
                    const double tilt = SOL::HudGeometry::RADAR_TILT;
                    const double squashed = radius * tilt;
                    const double tick = SOL::HudGeometry::RADAR_TICK_PX * s;

                    // The range ellipses
                    int32 next = 1;
                    for (int32 ring = 0; ring < ringCount; ++ring)
                    {
                        const double fraction = SOL::HudGeometry::RADAR_RINGS[ring];
                        const double gain = fraction >= 1.0 ? SOL::HudGeometry::RADAR_OUTER_RING_GAIN : 1.0;
                        HudSymbolsTestCheckEllipse(*this, FString::Printf(TEXT("%s ring %d"), *what, ring),
                            prims.Prims[next++], primary, gain * SOLRadar::SonarRingIntensity(fraction, timeS,
                            strength), c, radius * fraction, tilt);
                    }

                    // Axes, side ticks and the heading tick
                    HudSymbolsTestCheckLine(*this, what + TEXT(" lateral axis"), prims.Prims[next++], primary,
                        SOL::HudGeometry::RADAR_AXIS_INTENSITY, SOLRadar::ScopeToScreen(c, radius, tilt,
                        FVector2D(-1.0, 0.0)), SOLRadar::ScopeToScreen(c, radius, tilt, FVector2D(1.0, 0.0)), thin);
                    HudSymbolsTestCheckLine(*this, what + TEXT(" fore axis"), prims.Prims[next++], primary,
                        SOL::HudGeometry::RADAR_AXIS_INTENSITY, SOLRadar::ScopeToScreen(c, radius, tilt,
                        FVector2D(0.0, -1.0)), SOLRadar::ScopeToScreen(c, radius, tilt, FVector2D(0.0, 1.0)), thin);
                    const double dim = SOL::HudPalette::DIM_INTENSITY;
                    HudSymbolsTestCheckLine(*this, what + TEXT(" left tick"), prims.Prims[next++], primary, dim,
                        FVector2D(c.X - radius - tick, c.Y), FVector2D(c.X - radius, c.Y), thin);
                    HudSymbolsTestCheckLine(*this, what + TEXT(" right tick"), prims.Prims[next++], primary, dim,
                        FVector2D(c.X + radius, c.Y), FVector2D(c.X + radius + tick, c.Y), thin);
                    HudSymbolsTestCheckLine(*this, what + TEXT(" aft tick"), prims.Prims[next++], primary, dim,
                        FVector2D(c.X, c.Y + squashed), FVector2D(c.X, c.Y + squashed + tick), thin);
                    HudSymbolsTestCheckLine(*this, what + TEXT(" heading tick"), prims.Prims[next++], primary, 1.0,
                        FVector2D(c.X, c.Y - squashed - SOL::HudGeometry::RADAR_HEADING_TICK_PX * s),
                        FVector2D(c.X, c.Y - squashed), SOL::HudGeometry::CANVAS_LINE_PX);

                    // The cone, left edge first
                    if (bCone)
                    {
                        for (const double side : { -1.0, 1.0 })
                        {
                            HudSymbolsTestCheckLine(*this, FString::Printf(TEXT("%s cone %.0f"), *what, side),
                                prims.Prims[next++], primary, SOL::HudGeometry::RADAR_CONE_INTENSITY, c,
                                SOLRadar::ScopeToScreen(c, radius, tilt, SOLRadar::AzimuthToScope(heading
                                + side * halfFov)), thin);
                        }
                    }

                    // The ship chevron
                    const double chevron = SOL::HudGeometry::RADAR_CHEVRON_PX * s;
                    const double rise = SOL::HudGeometry::RADAR_CHEVRON_RISE_FRACTION * chevron;
                    HudSymbolsTestCheckLine(*this, what + TEXT(" chevron left"), prims.Prims[next++], primary,
                        SOL::HudGeometry::RADAR_CHEVRON_INTENSITY, FVector2D(c.X - chevron, c.Y + rise),
                        FVector2D(c.X, c.Y - rise), thin);
                    HudSymbolsTestCheckLine(*this, what + TEXT(" chevron right"), prims.Prims[next++], primary,
                        SOL::HudGeometry::RADAR_CHEVRON_INTENSITY, FVector2D(c.X, c.Y - rise),
                        FVector2D(c.X + chevron, c.Y + rise), thin);
                }
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsRadarContactTest, "SOLTest.HudSymbols.RadarContact",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendRadarContact for the four styles ContactStyle gives: a foot tick, a solid or dashed stem, a dot or a ring
// blip, and for the selected target a highlight ring whose glow ignores the contact's dimming; RadarBlipPx's sizes
bool FSOLHudSymbolsRadarContactTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const double thin = SOL::HudGeometry::CANVAS_LINE_THIN_PX;
    const int32 segments = SOL::HudGeometry::RADAR_CANVAS_SEGMENTS;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        HudSymbolsTestNear(*this, FString::Printf(TEXT("blip at %.2f"), s), SOLHudSymbols::RadarBlipPx(false, s),
            SOL::HudGeometry::RADAR_BLIP_PX * s);
        HudSymbolsTestNear(*this, FString::Printf(TEXT("selected blip at %.2f"), s),
            SOLHudSymbols::RadarBlipPx(true, s), SOL::HudGeometry::RADAR_SELECTED_BLIP_PX * s);

        for (const double height : { 0.3, -0.3 })
        {
            for (const bool bSelected : { false, true })
            {
                for (const float intensity : { 1.0f, 0.35f })
                {
                    FSOLRadarPoint point;
                    point.HeightStalkUnit = height;
                    point.bSelected = bSelected;
                    const FSOLRadarStyle style = SOLRadar::ContactStyle(point);
                    const bool bAbove = height > 0.0;
                    const FString what = FString::Printf(TEXT("contact at %.2f %s selected %d intensity %.2f"), s,
                        bAbove ? TEXT("above") : TEXT("below"), bSelected ? 1 : 0, intensity);
                    TestTrue(what + TEXT(": the style ContactStyle documents"), style.bDashedStem == !bAbove
                        && style.bHollowBlip == !bAbove && style.bLarge == bSelected);

                    const FVector2D foot = HUDSYMBOLSTEST_CENTRE_D;
                    const FVector2D top = foot + FVector2D(0.0, bAbove ? -30.25 : 30.25);
                    HudSymbolsTestSeed(prims);
                    const int32 added = SOLHudSymbols::AppendRadarContact(foot, top, style, intensity, s, prims);
                    if (!HudSymbolsTestCheckAppended(*this, what, prims, added, style.bLarge ? 4 : 3))
                    {
                        continue;
                    }
                    const ESOLHudPaletteRole role = style.Side == ESOLRadarSide::Above ? ESOLHudPaletteRole::Primary
                        : ESOLHudPaletteRole::Secondary;
                    const double footHalf = SOL::HudGeometry::RADAR_FOOT_HALF_PX * s;
                    const double blip = SOL::HudGeometry::RADAR_BLIP_PX * s;
                    const double blipPx = style.bLarge ? SOL::HudGeometry::RADAR_SELECTED_BLIP_PX * s : blip;
                    const double blipRing = style.bHollowBlip ? blipPx : 0.5 * blipPx;
                    HudSymbolsTestCheckLine(*this, what + TEXT(" foot"), prims.Prims[1], role,
                        SOL::HudGeometry::RADAR_FOOT_INTENSITY * intensity, FVector2D(foot.X - footHalf, foot.Y),
                        FVector2D(foot.X + footHalf, foot.Y), thin);
                    const double stem = SOL::HudGeometry::RADAR_STEM_INTENSITY * intensity;
                    if (style.bDashedStem)
                    {
                        HudSymbolsTestCheckDashed(*this, what + TEXT(" stem"), prims.Prims[2], role, stem, foot, top,
                            SOLRadar::StemDashPx * s, SOLRadar::StemGapPx * s);
                    }
                    else
                    {
                        HudSymbolsTestCheckLine(*this, what + TEXT(" stem"), prims.Prims[2], role, stem, foot, top,
                            thin);
                    }
                    const double blipIntensity = SOL::HudGeometry::RADAR_BLIP_INTENSITY * intensity;
                    if (style.bHollowBlip)
                    {
                        HudSymbolsTestCheckRing(*this, what + TEXT(" ring blip"), prims.Prims[3], role,
                            blipIntensity, top, blipRing, thin, segments);
                    }
                    else
                    {
                        HudSymbolsTestCheckDot(*this, what + TEXT(" dot blip"), prims.Prims[3], role, blipIntensity,
                            top, 0.5 * blipPx);
                    }
                    if (style.bLarge)
                    {
                        HudSymbolsTestCheckRing(*this, what + TEXT(" highlight"), prims.Prims[4], role,
                            SOL::HudGeometry::RADAR_STEM_INTENSITY, top,
                            blipRing + SOL::HudGeometry::RADAR_HIGHLIGHT_GAP_PX * s, thin, segments);
                    }
                }
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsBarsTest, "SOLTest.HudSymbols.Bars",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendBar: a ShapeOnly track, the fill (clamped to [0, 1], in the role given) and the end tick; AppendChaseTicks:
// the floor tick past the bar's edges, and the dim resume tick when shown, both clamped to the bar
bool FSOLHudSymbolsBarsTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const float x = 100.25f;
    const float y = 200.5f;
    const double thin = SOL::HudGeometry::CANVAS_LINE_THIN_PX;
    const ESOLHudPaletteRole primary = ESOLHudPaletteRole::Primary;
    const ESOLHudPaletteRole secondary = ESOLHudPaletteRole::Secondary;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        const double w = SOL::HudGeometry::SPEED_BAR_WIDTH_PX * s;
        const double h = SOL::HudGeometry::SPEED_BAR_HEIGHT_PX * s;
        for (const double fill : { -0.5, 0.0, 0.4, 1.0, 1.7 })
        {
            for (const ESOLHudPaletteRole role : { primary, secondary, ESOLHudPaletteRole::Bad })
            {
                const FString what = FString::Printf(TEXT("bar at %.2f fill %.2f role %d"), s, fill,
                    static_cast<int32>(role));
                HudSymbolsTestSeed(prims);
                const int32 added = SOLHudSymbols::AppendBar(x, y, fill, role, s, prims);
                if (!HudSymbolsTestCheckAppended(*this, what, prims, added, 3))
                {
                    continue;
                }
                HudSymbolsTestCheckBox(*this, what + TEXT(" track"), prims.Prims[1], primary,
                    SOL::HudGeometry::BAR_TRACK_INTENSITY, FVector2D(x, y), FVector2D(w, h),
                    ESOLHudPrimPath::ShapeOnly);
                HudSymbolsTestCheckBox(*this, what + TEXT(" fill"), prims.Prims[2], role,
                    SOL::HudGeometry::BAR_FILL_INTENSITY, FVector2D(x, y),
                    FVector2D(w * FMath::Clamp(fill, 0.0, 1.0), h));
                HudSymbolsTestCheckLine(*this, what + TEXT(" end tick"), prims.Prims[3], primary,
                    SOL::HudPalette::DIM_INTENSITY, FVector2D(x + w, y), FVector2D(x + w, y + h), thin);
            }
        }

        // Chase ticks: the floor alone, or with the resume tick
        const double overhang = SOL::HudGeometry::CHASE_TICK_OVERHANG_PX * s;
        for (const double floorMeter : { -0.2, 0.3, 1.4 })
        {
            for (int32 resumeCase = 0; resumeCase < 3; ++resumeCase)
            {
                const bool bShowResume = resumeCase > 0;
                const double resumeMeter = resumeCase == 2 ? 1.3 : 0.5;
                const FString what = FString::Printf(TEXT("chase ticks at %.2f floor %.2f resume %d %.2f"), s,
                    floorMeter, bShowResume ? 1 : 0, resumeMeter);
                HudSymbolsTestSeed(prims);
                const int32 added = SOLHudSymbols::AppendChaseTicks(x, y, floorMeter, bShowResume, resumeMeter, s,
                    prims);
                if (!HudSymbolsTestCheckAppended(*this, what, prims, added, bShowResume ? 2 : 1))
                {
                    continue;
                }
                const double floorX = x + w * FMath::Clamp(floorMeter, 0.0, 1.0);
                HudSymbolsTestCheckLine(*this, what + TEXT(" floor"), prims.Prims[1], secondary, 1.0,
                    FVector2D(floorX, y - overhang), FVector2D(floorX, y + h + overhang),
                    SOL::HudGeometry::CANVAS_LINE_THICK_PX);
                if (bShowResume)
                {
                    const double resumeX = x + w * FMath::Clamp(resumeMeter, 0.0, 1.0);
                    HudSymbolsTestCheckLine(*this, what + TEXT(" resume"), prims.Prims[2], secondary,
                        SOL::HudPalette::DIM_INTENSITY, FVector2D(resumeX, y), FVector2D(resumeX, y + h), thin);
                }
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsWarpTest, "SOLTest.HudSymbols.WarpStreak",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendWarpStreak: one WarpStreak line outward along its direction, its start travelling and its length growing
// with the phase, brighter and thicker with the phase; zero intensity is a zero-length line at the inner radius
bool FSOLHudSymbolsWarpTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const FVector2D direction(0.6, -0.8);
    const double halfDiagonal = 500.25;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        for (const double phase : { 0.0, 0.5, 1.0 })
        {
            for (const double intensity : { 0.8, 0.0 })
            {
                const FString what = FString::Printf(TEXT("streak at %.2f phase %.2f intensity %.2f"), s, phase,
                    intensity);
                const FVector2D c = HUDSYMBOLSTEST_CENTRE_D;
                HudSymbolsTestSeed(prims);
                const int32 added = SOLHudSymbols::AppendWarpStreak(c, direction, phase, intensity, halfDiagonal, s,
                    prims);
                if (!HudSymbolsTestCheckAppended(*this, what, prims, added, 1))
                {
                    continue;
                }
                const double inner = halfDiagonal * (SOL::HudGeometry::WARP_STREAK_START_FRACTION
                    + SOL::HudGeometry::WARP_STREAK_TRAVEL_FRACTION * phase * phase);
                const double length = halfDiagonal * intensity * FMath::Lerp(
                    static_cast<double>(SOL::HudGeometry::WARP_STREAK_MIN_LENGTH_FRACTION),
                    static_cast<double>(SOL::HudGeometry::WARP_STREAK_MAX_LENGTH_FRACTION), phase);
                const double brightness = intensity * (SOL::HudGeometry::WARP_STREAK_BASE_BRIGHTNESS
                    + SOL::HudGeometry::WARP_STREAK_PHASE_BRIGHTNESS * phase);
                // A 1 px line at phase 0, WARP_STREAK_EXTRA_THICKNESS_PX thicker at phase 1, times the UI scale
                const double thickness = (1.0 + SOL::HudGeometry::WARP_STREAK_EXTRA_THICKNESS_PX * phase) * s;
                HudSymbolsTestCheckLine(*this, what, prims.Prims[1], ESOLHudPaletteRole::WarpStreak, brightness,
                    c + direction * inner, c + direction * (inner + length), thickness);
                if (intensity == 0.0)
                {
                    TestTrue(what + TEXT(": zero length"), prims.Prims[1].A.Equals(prims.Prims[1].B,
                        HUDSYMBOLSTEST_TOLERANCE));
                }
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsRadialTest, "SOLTest.HudSymbols.RadialSector",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendRadialSector: nothing at a closed aperture, else the fill arc (only with a fill), then the inner and outer rim
// arcs, every arc with the sector's pad, Secondary iff highlighted; from SectorLook's real looks
bool FSOLHudSymbolsRadialTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    const int32 sectorCount = 4;
    const float halfAperture = static_cast<float>(UE_DOUBLE_PI / sectorCount
        - 0.5 * SOL::Interact::RADIAL_SECTOR_GAP_RAD);
    const float midAngle = static_cast<float>(SOLRadialMenu::SectorMidAngleRad(1, sectorCount));
    const FVector2f centre = HUDSYMBOLSTEST_CENTRE;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        // A closed (or negative) aperture draws nothing
        for (const float closed : { 0.0f, -0.1f })
        {
            FSOLHoloRadialSectorLook look;
            look.HalfApertureRad = closed;
            look.EdgeIntensity = 0.6f;
            look.FillIntensity = 0.2f;
            HudSymbolsTestSeed(prims);
            const FString what = FString::Printf(TEXT("sector at %.2f aperture %.2f"), s, closed);
            HudSymbolsTestCheckAppended(*this, what, prims,
                SOLHudSymbols::AppendRadialSector(centre, s, midAngle, look, true, prims), 0);
        }

        const double inner = SOL::Interact::RADIAL_INNER_PX * s;
        const double outer = SOL::Interact::RADIAL_OUTER_PX * s;
        for (int32 variant = 0; variant < 4; ++variant)
        {
            const bool bHighlighted = (variant & 1) != 0;
            const bool bEnabled = (variant & 2) != 0;
            for (const float strength : { 0.0f, 1.0f })
            {
                const FSOLHoloRadialSectorLook look = SOLHoloRadial::SectorLook(halfAperture, bHighlighted, bEnabled,
                    HudSymbolsTestFullLook(), strength);
                const FString what = FString::Printf(TEXT("sector at %.2f highlighted %d enabled %d strength %.0f"),
                    s, bHighlighted ? 1 : 0, bEnabled ? 1 : 0, strength);
                const bool bFill = look.FillIntensity > 0.0f;
                HudSymbolsTestSeed(prims);
                const int32 added = SOLHudSymbols::AppendRadialSector(centre, s, midAngle, look, bHighlighted,
                    prims);
                if (!HudSymbolsTestCheckAppended(*this, what, prims, added, bFill ? 3 : 2))
                {
                    continue;
                }
                const ESOLHudPaletteRole role = bHighlighted ? ESOLHudPaletteRole::Secondary
                    : ESOLHudPaletteRole::Primary;

                // The look's colour is the colour the builder's role replays in (one rule, bit for bit)
                TestTrue(what + TEXT(": look colour is the role's palette colour"),
                    SOLHudSymbols::HudPalette().Colors[static_cast<int32>(role)] == look.Color);
                const FVector2D c(centre);
                int32 next = 1;
                if (bFill)
                {
                    HudSymbolsTestCheckArc(*this, what + TEXT(" fill"), prims.Prims[next++], role, look.FillIntensity,
                        c, 0.5 * (inner + outer), 0.5 * (outer - inner), midAngle, look.HalfApertureRad, look.PadPx);
                }
                HudSymbolsTestCheckArc(*this, what + TEXT(" inner rim"), prims.Prims[next++], role,
                    look.EdgeIntensity, c, inner, SOL::Interact::RADIAL_EDGE_HALF_WIDTH_PX, midAngle,
                    look.HalfApertureRad, look.PadPx);
                HudSymbolsTestCheckArc(*this, what + TEXT(" outer rim"), prims.Prims[next++], role,
                    look.EdgeIntensity, c, outer, SOL::Interact::RADIAL_EDGE_HALF_WIDTH_PX, midAngle,
                    look.HalfApertureRad, look.PadPx);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsHalfWidthTest, "SOLTest.HudSymbols.LineHalfWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// LineHalfWidthPx: the HUD's 1 px glow line up to 2 px of Canvas width, half of the width past 1 px beyond that
bool FSOLHudSymbolsHalfWidthTest::RunTest(const FString& /*parameters*/)
{
    const float base = SOL::Hologram::HUD_LINE_HALF_WIDTH_PX;
    for (const float thickness : { 0.0f, 1.0f, 1.5f, 2.0f })
    {
        HudSymbolsTestNear(*this, FString::Printf(TEXT("thickness %.1f is the 1 px line"), thickness),
            SOLHudSymbols::LineHalfWidthPx(thickness), base, HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
    }
    for (const float thickness : { 3.0f, 6.0f })
    {
        HudSymbolsTestNear(*this, FString::Printf(TEXT("thickness %.1f"), thickness),
            SOLHudSymbols::LineHalfWidthPx(thickness), FMath::Max(base, 0.5f * (thickness - 1.0f)),
            HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
    }
    HudSymbolsTestNear(*this, TEXT("thickness 3 gives 1"), SOLHudSymbols::LineHalfWidthPx(3.0f), 1.0,
        HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
    HudSymbolsTestNear(*this, TEXT("thickness 6 gives 2.5"), SOLHudSymbols::LineHalfWidthPx(6.0f), 2.5,
        HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsFrameTest, "SOLTest.HudSymbols.FramePrimitive",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Frame primitive replays as BuildFrame (its rect and UI scale, the full filled sweep-free look, the palette's
// strength) + AppendToBatch; ListFrame is one such primitive around the centre; a Line's PadPx > 0 replaces the
// replay's pad
bool FSOLHudSymbolsFrameTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    FSOLHudShapeBatch actual;
    FSOLHudShapeBatch expected;
    actual.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    expected.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    FSOLHudSymbolPrims prims;
    FSOLHoloFrameOutput output;

    // A Frame primitive's shapes are BuildFrame's
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        const FString what = FString::Printf(TEXT("frame at %.2f"), s);
        FSOLHudSymbolPrim frame;
        frame.Kind = ESOLHudPrimKind::Frame;
        frame.A = FVector2f(120.25f, 80.5f);
        frame.B = FVector2f(260.75f, 190.25f);
        frame.UiScale = s;
        prims.Reset();
        prims.Add(frame);
        actual.Reset();
        const int32 added = SOLHudSymbols::AddToBatch(prims, actual, palette, HUDSYMBOLSTEST_PAD_PX);

        FSOLHoloFrameSpec spec;
        spec.Rect = FBox2f(frame.A, frame.B);
        spec.UiScale = s;
        const int32 built = SOLHoloFrameBuild::BuildFrame(spec, HudSymbolsTestFullLook(), palette.Strength, output);
        expected.Reset();
        SOLHoloFrameBuild::AppendToBatch(output, expected);
        TestTrue(what + TEXT(": BuildFrame draws it"), built > 0);
        TestTrue(what + TEXT(": fits the glossary batch"), built <= SOL::Help::GLOSSARY_MAX_SHAPES);
        TestEqual(what + TEXT(": returns the shapes added"), added, actual.GetShapeCount());
        HudSymbolsTestCheckSameShapes(*this, actual, expected, what);

        // Through a caller's frame output: the same shapes
        FSOLHudShapeBatch scratchBatch;
        scratchBatch.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
        FSOLHoloFrameOutput scratch;
        TestEqual(what + TEXT(": the scratch replay adds as many"),
            SOLHudSymbols::AddToBatch(prims, scratchBatch, palette, HUDSYMBOLSTEST_PAD_PX, scratch), added);
        HudSymbolsTestCheckSameShapes(*this, scratchBatch, expected, what + TEXT(" with a scratch"));

        // ListFrame: one Frame primitive, the glossary rect around the centre at the UI scale
        const FString list = FString::Printf(TEXT("ListFrame at %.2f"), s);
        if (TestEqual(list + TEXT(": one primitive"), SOLHudSymbols::Build(ESOLHudSymbol::ListFrame,
            HUDSYMBOLSTEST_CENTRE, s, prims), 1))
        {
            const FVector2D half(SOL::Help::GLOSSARY_FRAME_HALF_WIDTH_PX * s,
                SOL::Help::GLOSSARY_FRAME_HALF_HEIGHT_PX * s);
            TestTrue(list + TEXT(": a Frame"), prims.Prims[0].Kind == ESOLHudPrimKind::Frame);
            HudSymbolsTestCheckPoint(*this, list + TEXT(": min"), prims.Prims[0].A, HUDSYMBOLSTEST_CENTRE_D - half);
            HudSymbolsTestCheckPoint(*this, list + TEXT(": max"), prims.Prims[0].B, HUDSYMBOLSTEST_CENTRE_D + half);
            HudSymbolsTestNear(*this, list + TEXT(": UI scale"), prims.Prims[0].UiScale, s);
        }
    }

    // A Line's PadPx > 0 replaces the replay's pad; 0 keeps it
    for (const float padPx : { 7.0f, 0.0f })
    {
        FSOLHudSymbolPrim line;
        line.Kind = ESOLHudPrimKind::Line;
        line.A = FVector2f(50.5f, 60.25f);
        line.B = FVector2f(180.75f, 95.5f);
        line.HalfWidthPx = 1.25f;
        line.Intensity = 0.8f;
        line.PadPx = padPx;
        prims.Reset();
        prims.Add(line);
        actual.Reset();
        SOLHudSymbols::AddToBatch(prims, actual, palette, HUDSYMBOLSTEST_PAD_PX);
        expected.Reset();
        expected.AddLine(line.A, line.B, line.HalfWidthPx, SOL::HudPalette::PRIMARY, line.Intensity,
            padPx > 0.0f ? padPx : HUDSYMBOLSTEST_PAD_PX);
        HudSymbolsTestCheckSameShapes(*this, actual, expected, FString::Printf(TEXT("line with pad %.1f"), padPx));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsCursorTest, "SOLTest.HudSymbols.HoloCursorUsesArrowSegments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// HoloCursor is the cursor's ArrowSegments, every segment shifted by one common offset that centres their box on the
// symbol's centre, Primary at CURSOR_INTENSITY
bool FSOLHudSymbolsCursorTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        const FString what = FString::Printf(TEXT("cursor at %.2f"), s);
        FVector2f starts[SOL::Overlay::CURSOR_SEGMENTS];
        FVector2f ends[SOL::Overlay::CURSOR_SEGMENTS];
        SOLHologramCursor::ArrowSegments(s, starts, ends);
        if (!TestEqual(what + TEXT(": one line per segment"), SOLHudSymbols::Build(ESOLHudSymbol::HoloCursor,
            HUDSYMBOLSTEST_CENTRE, s, prims), SOL::Overlay::CURSOR_SEGMENTS))
        {
            continue;
        }
        const FVector2f offset = prims.Prims[0].A - starts[0];
        FBox2f box(ForceInit);
        for (int32 segment = 0; segment < SOL::Overlay::CURSOR_SEGMENTS; ++segment)
        {
            const FSOLHudSymbolPrim& prim = prims.Prims[segment];
            const FString at = FString::Printf(TEXT("%s segment %d"), *what, segment);
            HudSymbolsTestCheckHead(*this, at, prim, ESOLHudPrimKind::Line, ESOLHudPaletteRole::Primary,
                SOL::Overlay::CURSOR_INTENSITY, ESOLHudPrimPath::Both);
            HudSymbolsTestCheckPoint(*this, at + TEXT(": start"), prim.A, FVector2D(starts[segment] + offset));
            HudSymbolsTestCheckPoint(*this, at + TEXT(": end"), prim.B, FVector2D(ends[segment] + offset));
            box += prim.A;
            box += prim.B;
        }
        HudSymbolsTestCheckPoint(*this, what + TEXT(": the arrow's box is centred"), box.GetCenter(),
            HUDSYMBOLSTEST_CENTRE_D);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsMapIconTest, "SOLTest.HudSymbols.MapBodyIcon",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MapBodyIcon is one Planet Icon primitive at the centre, the map icon's radius at the UI scale, at ICON_INTENSITY
bool FSOLHudSymbolsMapIconTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
    {
        const FString what = FString::Printf(TEXT("map body icon at %.2f"), s);
        if (!TestEqual(what + TEXT(": one primitive"), SOLHudSymbols::Build(ESOLHudSymbol::MapBodyIcon,
            HUDSYMBOLSTEST_CENTRE, s, prims), 1))
        {
            continue;
        }
        const FSOLHudSymbolPrim& prim = prims.Prims[0];
        TestTrue(what + TEXT(": an icon"), prim.Kind == ESOLHudPrimKind::Icon);
        TestTrue(what + TEXT(": a planet"), prim.Category == SOLOverlayIcons::EIconCategory::Planet);
        HudSymbolsTestCheckPoint(*this, what + TEXT(": centre"), prim.A, HUDSYMBOLSTEST_CENTRE_D);
        HudSymbolsTestNear(*this, what + TEXT(": radius"), prim.RadiusPx,
            FMath::Max(0.5 * SOL::MAP_ICON_DIAMETER_PX * s, static_cast<double>(SOL::Help::GLOSSARY_MIN_FEATURE_PX)));
        HudSymbolsTestNear(*this, what + TEXT(": intensity"), prim.Intensity, SOL::Overlay::ICON_INTENSITY,
            HUDSYMBOLSTEST_INTENSITY_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsBuildAppendTest, "SOLTest.HudSymbols.BuildMatchesAppend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Build of each HUD (class a) symbol is its Append* builder at the glossary's representative parameters (D5): the
// same primitives wherever the builder's output is already at the glossary floor (D42), else the same primitives in
// kind, role and path with every feature raised to the floor
bool FSOLHudSymbolsBuildAppendTest::RunTest(const FString& /*parameters*/)
{
    const ESOLHudSymbol hudSymbols[] = { ESOLHudSymbol::Reticle, ESOLHudSymbol::Joystick,
        ESOLHudSymbol::JoystickFreeLook, ESOLHudSymbol::SteerFree, ESOLHudSymbol::SteerHeld,
        ESOLHudSymbol::SteerLocked, ESOLHudSymbol::Prograde, ESOLHudSymbol::Retrograde, ESOLHudSymbol::TargetBracket,
        ESOLHudSymbol::TargetEdgeArrow, ESOLHudSymbol::RadarScope, ESOLHudSymbol::RadarContactAbove,
        ESOLHudSymbol::RadarContactBelow, ESOLHudSymbol::RadarSelected, ESOLHudSymbol::GMeter,
        ESOLHudSymbol::SpeedCapBar, ESOLHudSymbol::WarpStreaks, ESOLHudSymbol::RadialMenu };
    FSOLHudSymbolPrims built;
    FSOLHudSymbolPrims reference;
    int32 fullCompares = 0;
    for (const ESOLHudSymbol symbol : hudSymbols)
    {
        for (const float s : HUDSYMBOLSTEST_BUILDER_SCALES)
        {
            const FString what = FString::Printf(TEXT("%s at %.2f"), *HudSymbolsTestName(symbol), s);
            const int32 count = SOLHudSymbols::Build(symbol, HUDSYMBOLSTEST_CENTRE, s, built);
            const float joystickRadius = count > 0 ? built.Prims[0].RadiusPx : 0.0f;
            TestTrue(what + TEXT(": a class (a) symbol"), HudSymbolsTestGlossaryReference(symbol,
                HUDSYMBOLSTEST_CENTRE, s, joystickRadius, reference));
            TestEqual(what + TEXT(": the builder's count"), count, reference.Count);
            TestEqual(what + TEXT(": the reference dropped nothing"), reference.DroppedCount, 0);
            if (count != reference.Count)
            {
                continue;
            }
            bool bAboveFloor = true;
            for (int32 index = 0; index < count; ++index)
            {
                bAboveFloor &= HudSymbolsTestMeetsFloor(reference.Prims[index]);
            }
            fullCompares += bAboveFloor ? 1 : 0;
            for (int32 index = 0; index < count; ++index)
            {
                const FSOLHudSymbolPrim& a = built.Prims[index];
                const FSOLHudSymbolPrim& b = reference.Prims[index];
                const FString prim = FString::Printf(TEXT("%s prim %d"), *what, index);
                if (bAboveFloor)
                {
                    TestTrue(prim + TEXT(": the builder's primitive"), HudSymbolsTestSamePrim(a, b));
                }
                else
                {
                    TestTrue(prim + TEXT(": the builder's kind, role and path"), a.Kind == b.Kind
                        && a.Role == b.Role && a.Path == b.Path);
                    TestTrue(prim + TEXT(": at the glossary floor"), HudSymbolsTestMeetsFloor(a));
                }
            }
        }
    }
    TestTrue(TEXT("most symbols are compared in full"), fullCompares > 0);

    // Symbols that are above the floor at the HUD's own scale 1 are not touched by it
    for (const ESOLHudSymbol symbol : { ESOLHudSymbol::Prograde, ESOLHudSymbol::TargetBracket,
        ESOLHudSymbol::RadarScope, ESOLHudSymbol::SpeedCapBar })
    {
        HudSymbolsTestGlossaryReference(symbol, HUDSYMBOLSTEST_CENTRE, 1.0f, 0.0f, reference);
        bool bAboveFloor = reference.Count > 0;
        for (int32 index = 0; index < reference.Count; ++index)
        {
            bAboveFloor &= HudSymbolsTestMeetsFloor(reference.Prims[index]);
        }
        TestTrue(HudSymbolsTestName(symbol) + TEXT(": above the floor at scale 1 (compared in full)"), bAboveFloor);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsBuildersUnclampedTest, "SOLTest.HudSymbols.BuildersNotFloored",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The HUD's Append* builders never apply the glossary floor (D42): at a tiny scale their features stay below it
bool FSOLHudSymbolsBuildersUnclampedTest::RunTest(const FString& /*parameters*/)
{
    const float tiny = 0.05f;
    FSOLHudSymbolPrims prims;
    SOLHudSymbols::AppendReticle(HUDSYMBOLSTEST_CENTRE_D, tiny, prims);
    if (TestEqual(TEXT("reticle: five primitives"), prims.Count, 5))
    {
        HudSymbolsTestNear(*this, TEXT("reticle dot unfloored"), prims.Prims[4].RadiusPx,
            SOL::HudGeometry::RETICLE_DOT_RADIUS_PX * tiny);
    }
    prims.Reset();
    SOLHudSymbols::AppendPrograde(HUDSYMBOLSTEST_CENTRE_D, tiny, prims);
    if (TestEqual(TEXT("prograde: four primitives"), prims.Count, 4))
    {
        HudSymbolsTestNear(*this, TEXT("prograde ring unfloored"), prims.Prims[0].RadiusPx,
            SOL::HudGeometry::VELOCITY_MARKER_RADIUS_PX * tiny);
    }
    TestTrue(TEXT("the tiny scale is below the floor"),
        SOL::HudGeometry::VELOCITY_MARKER_RADIUS_PX * tiny < SOL::Help::GLOSSARY_MIN_FEATURE_PX);
    return true;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Asserts a golden Line primitive against literal values, its glow half width and glow pad included
    void HudSymbolsTestGoldenLine(FAutomationTestBase& test, const FString& what, const FSOLHudSymbolPrim& prim,
        const ESOLHudPaletteRole role, const double intensity, const FVector2D& a, const FVector2D& b,
        const double thicknessPx, const double halfWidthPx)
    {
        HudSymbolsTestCheckLine(test, what, prim, role, intensity, a, b, thicknessPx);
        HudSymbolsTestNear(test, what + TEXT(": literal half width"), prim.HalfWidthPx, halfWidthPx);
        HudSymbolsTestNear(test, what + TEXT(": pad"), prim.PadPx, 0.0);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsGoldenValuesTest, "SOLTest.HudSymbols.GoldenValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The builders at UI scale 1 against literal numbers from the pre-part-33d-i flight HUD (its old file-local constants:
// reticle gap 6 / arm 12 / width 1.5 / dot 1 at 0.5 with the 32 px pad, prograde radius 9 / 16 segments, speed bar
// 240 x 8 / track 0.1 / fill 0.45 / tick 0.55, radar foot 2.5 at 0.35 / stem 0.45 / blip 4 or 7 at 0.6 / highlight
// gap 3 at 0.45 / 12 segments), so a constant drifting in SOL::HudGeometry is caught
bool FSOLHudSymbolsGoldenValuesTest::RunTest(const FString& /*parameters*/)
{
    const ESOLHudPaletteRole primary = ESOLHudPaletteRole::Primary;
    const ESOLHudPaletteRole good = ESOLHudPaletteRole::Good;
    FSOLHudSymbolPrims prims;

    // Reticle at (100, 200): arms from 6 to 18 px out, 1.5 px wide (glow half width 0.5), the ShapeOnly dot ring
    if (TestEqual(TEXT("reticle: five primitives"), SOLHudSymbols::AppendReticle(FVector2D(100.0, 200.0), 1.0f, prims),
        5))
    {
        HudSymbolsTestGoldenLine(*this, TEXT("reticle left"), prims.Prims[0], primary, 1.0, FVector2D(82.0, 200.0),
            FVector2D(94.0, 200.0), 1.5, 0.5);
        HudSymbolsTestGoldenLine(*this, TEXT("reticle right"), prims.Prims[1], primary, 1.0, FVector2D(106.0, 200.0),
            FVector2D(118.0, 200.0), 1.5, 0.5);
        HudSymbolsTestGoldenLine(*this, TEXT("reticle top"), prims.Prims[2], primary, 1.0, FVector2D(100.0, 182.0),
            FVector2D(100.0, 194.0), 1.5, 0.5);
        HudSymbolsTestGoldenLine(*this, TEXT("reticle bottom"), prims.Prims[3], primary, 1.0, FVector2D(100.0, 206.0),
            FVector2D(100.0, 218.0), 1.5, 0.5);
        HudSymbolsTestCheckRing(*this, TEXT("reticle dot"), prims.Prims[4], primary, 0.5, FVector2D(100.0, 200.0), 1.0,
            -1.0, INDEX_NONE, ESOLHudPrimPath::ShapeOnly);
        HudSymbolsTestNear(*this, TEXT("reticle dot: the HUD glow pad"), prims.Prims[4].PadPx, 32.0);
    }

    // Prograde at (100, 200): a Good ring of 9 px (16 Canvas segments, 1.5 px) and ticks from 9 to 18 px out
    prims.Reset();
    if (TestEqual(TEXT("prograde: four primitives"), SOLHudSymbols::AppendPrograde(FVector2D(100.0, 200.0), 1.0f,
        prims), 4))
    {
        HudSymbolsTestCheckRing(*this, TEXT("prograde ring"), prims.Prims[0], good, 1.0, FVector2D(100.0, 200.0), 9.0,
            1.5, 16);
        HudSymbolsTestGoldenLine(*this, TEXT("prograde left"), prims.Prims[1], good, 1.0, FVector2D(91.0, 200.0),
            FVector2D(82.0, 200.0), 1.5, 0.5);
        HudSymbolsTestGoldenLine(*this, TEXT("prograde right"), prims.Prims[2], good, 1.0, FVector2D(109.0, 200.0),
            FVector2D(118.0, 200.0), 1.5, 0.5);
        HudSymbolsTestGoldenLine(*this, TEXT("prograde top"), prims.Prims[3], good, 1.0, FVector2D(100.0, 191.0),
            FVector2D(100.0, 182.0), 1.5, 0.5);
    }

    // Speed bar at (10, 20), half full in Secondary: the 240 x 8 track, a 120 px fill and the end tick at x 250
    prims.Reset();
    if (TestEqual(TEXT("bar: three primitives"), SOLHudSymbols::AppendBar(10.0f, 20.0f, 0.5,
        ESOLHudPaletteRole::Secondary, 1.0f, prims), 3))
    {
        HudSymbolsTestCheckBox(*this, TEXT("bar track"), prims.Prims[0], primary, 0.1, FVector2D(10.0, 20.0),
            FVector2D(240.0, 8.0), ESOLHudPrimPath::ShapeOnly);
        HudSymbolsTestCheckBox(*this, TEXT("bar fill"), prims.Prims[1], ESOLHudPaletteRole::Secondary, 0.45,
            FVector2D(10.0, 20.0), FVector2D(120.0, 8.0));
        HudSymbolsTestGoldenLine(*this, TEXT("bar end tick"), prims.Prims[2], primary, 0.55, FVector2D(250.0, 20.0),
            FVector2D(250.0, 28.0), 1.0, 0.5);
    }

    // A radar contact above the plane, foot (100, 200) and blip (100, 170): foot tick, stem, a dot of radius 2
    FSOLRadarStyle style;
    style.Side = ESOLRadarSide::Above;
    prims.Reset();
    if (TestEqual(TEXT("contact: three primitives"), SOLHudSymbols::AppendRadarContact(FVector2D(100.0, 200.0),
        FVector2D(100.0, 170.0), style, 1.0f, 1.0f, prims), 3))
    {
        HudSymbolsTestGoldenLine(*this, TEXT("contact foot"), prims.Prims[0], primary, 0.35, FVector2D(97.5, 200.0),
            FVector2D(102.5, 200.0), 1.0, 0.5);
        HudSymbolsTestGoldenLine(*this, TEXT("contact stem"), prims.Prims[1], primary, 0.45, FVector2D(100.0, 200.0),
            FVector2D(100.0, 170.0), 1.0, 0.5);
        HudSymbolsTestCheckDot(*this, TEXT("contact blip"), prims.Prims[2], primary, 0.6, FVector2D(100.0, 170.0), 2.0);
    }

    // The selected contact at half intensity: a 7 px blip (radius 3.5) and the highlight ring 3 px out at 0.45, which
    // ignores the intensity
    style.bLarge = true;
    prims.Reset();
    if (TestEqual(TEXT("selected contact: four primitives"), SOLHudSymbols::AppendRadarContact(FVector2D(100.0, 200.0),
        FVector2D(100.0, 170.0), style, 0.5f, 1.0f, prims), 4))
    {
        HudSymbolsTestCheckDot(*this, TEXT("selected blip"), prims.Prims[2], primary, 0.3, FVector2D(100.0, 170.0),
            3.5);
        HudSymbolsTestCheckRing(*this, TEXT("selected highlight"), prims.Prims[3], primary, 0.45,
            FVector2D(100.0, 170.0), 6.5, 1.0, 12);
    }
    return true;
}

#endif
