/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "SOLConstants.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudSymbols.h"
#include "UI/SOLOverlayIcons.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a HUD symbol helper (Docs/SDDs/33-player-documentation.md sections 8.1, 8.3 and 10,
// D5), written against the header contract (UI/SOLHudSymbols.h) before the production code existed: the symbol ids,
// Build's primitive lists, their bounds, the replay into a shape batch, and one source of geometry for the symbols
// that already have a builder (the overlay icons and edge arrows through SOLOverlayIcons, the cards through
// SOLHoloFrameBuild::BuildCard). The reticle and prograde / retrograde geometry matches belong to part 33d-i.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float HUDSYMBOLSTEST_TOLERANCE = 1.0e-3f;         // Pixel comparisons
    constexpr float HUDSYMBOLSTEST_PAD_PX = 2.0f;               // The glow pad of the replays
    constexpr int32 HUDSYMBOLSTEST_SYMBOL_COUNT = static_cast<int32>(ESOLHudSymbol::Count);
    const FVector2f HUDSYMBOLSTEST_CENTRE(400.0f, 300.0f);
    const FVector2f HUDSYMBOLSTEST_OTHER_CENTRE(37.5f, 912.25f);

    // The symbols built in 33a (those with an existing builder); part 33d-i extends this list to every symbol
    constexpr ESOLHudSymbol HUDSYMBOLSTEST_BUILT[] = { ESOLHudSymbol::IconStar, ESOLHudSymbol::IconPlanet,
        ESOLHudSymbol::IconMoon, ESOLHudSymbol::IconDwarf, ESOLHudSymbol::IconAsteroid, ESOLHudSymbol::IconRing,
        ESOLHudSymbol::IconShip, ESOLHudSymbol::IconTarget, ESOLHudSymbol::IconEdgeArrow, ESOLHudSymbol::Card };

    // The overlay icon symbols, in EIconCategory order
    constexpr ESOLHudSymbol HUDSYMBOLSTEST_ICONS[] = { ESOLHudSymbol::IconStar, ESOLHudSymbol::IconPlanet,
        ESOLHudSymbol::IconMoon, ESOLHudSymbol::IconDwarf, ESOLHudSymbol::IconAsteroid, ESOLHudSymbol::IconRing,
        ESOLHudSymbol::IconShip, ESOLHudSymbol::IconTarget };
    static_assert(UE_ARRAY_COUNT(HUDSYMBOLSTEST_ICONS) == static_cast<int32>(SOLOverlayIcons::EIconCategory::Count),
        "one icon symbol per category");

    // UI scales the built symbols are checked at
    constexpr float HUDSYMBOLSTEST_SCALES[] = { 0.5f, 1.0f, 1.75f, 2.0f };

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
            TestTrue(prim + TEXT(": deterministic"), a.Kind == b.Kind && a.Role == b.Role
                && a.A.Equals(b.A, HUDSYMBOLSTEST_TOLERANCE) && a.B.Equals(b.B, HUDSYMBOLSTEST_TOLERANCE)
                && FMath::IsNearlyEqual(a.RadiusPx, b.RadiusPx, HUDSYMBOLSTEST_TOLERANCE));
            TestTrue(prim + TEXT(": same kind when moved"), c.Kind == a.Kind);
            TestTrue(prim + TEXT(": A moves with the centre"), c.A.Equals(a.A + offset, HUDSYMBOLSTEST_TOLERANCE));
            const bool bBIsPoint = a.Kind == ESOLHudPrimKind::Line || a.Kind == ESOLHudPrimKind::DashedLine
                || a.Kind == ESOLHudPrimKind::Card;
            const FVector2f expectedB = bBIsPoint ? a.B + offset : a.B;
            TestTrue(prim + TEXT(": B moves only if it is a point"), c.B.Equals(expectedB, HUDSYMBOLSTEST_TOLERANCE));
            TestTrue(prim + TEXT(": sizes do not move"), FMath::IsNearlyEqual(c.RadiusPx, a.RadiusPx,
                HUDSYMBOLSTEST_TOLERANCE));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHudSymbolsBoundsScaleTest, "SOLTest.HudSymbols.BoundsScaleLinearly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A symbol's bounds are linear in the UI scale; Count and a bad scale have empty bounds
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
        for (const float scale : HUDSYMBOLSTEST_SCALES)
        {
            const FBox2f scaled = SOLHudSymbols::Bounds(symbol, scale, scratch);
            const float tolerance = HUDSYMBOLSTEST_TOLERANCE * FMath::Max(1.0f, scale) * 10.0f;
            TestTrue(FString::Printf(TEXT("%s: bounds at %.2f valid"), *what, scale), scaled.bIsValid != 0);
            TestTrue(FString::Printf(TEXT("%s: min linear at %.2f"), *what, scale),
                scaled.Min.Equals(unit.Min * scale, tolerance));
            TestTrue(FString::Printf(TEXT("%s: max linear at %.2f"), *what, scale),
                scaled.Max.Equals(unit.Max * scale, tolerance));
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
// A symbol's bounds, placed at its centre, contain every primitive Build writes at the same scale
bool FSOLHudSymbolsBoundsContainTest::RunTest(const FString& /*parameters*/)
{
    FSOLHudSymbolPrims prims;
    FSOLHudSymbolPrims scratch;
    for (const ESOLHudSymbol symbol : HUDSYMBOLSTEST_BUILT)
    {
        for (const float scale : HUDSYMBOLSTEST_SCALES)
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
// AddToBatch adds one shape per Line ... Panel primitive, the builder's shapes for Icon / EdgeArrow / Card, none for
// Text, and never resets the batch
bool FSOLHudSymbolsReplayTest::RunTest(const FString& /*parameters*/)
{
    const FSOLHudSymbolPalette palette = SOLHudSymbols::HudPalette();
    FSOLHudShapeBatch batch;
    batch.Initialize(SOL::Help::GLOSSARY_MAX_SHAPES);
    FSOLHudSymbolPrims prims;

    // One shape per simple primitive
    const ESOLHudPrimKind simple[] = { ESOLHudPrimKind::Line, ESOLHudPrimKind::Ring, ESOLHudPrimKind::Arc,
        ESOLHudPrimKind::Box, ESOLHudPrimKind::Dot, ESOLHudPrimKind::DashedLine, ESOLHudPrimKind::Ellipse,
        ESOLHudPrimKind::Panel };
    for (const ESOLHudPrimKind kind : simple)
    {
        prims.Reset();
        prims.Add(HudSymbolsTestPrim(kind));
        batch.Reset();
        const int32 added = SOLHudSymbols::AddToBatch(prims, batch, palette, HUDSYMBOLSTEST_PAD_PX);
        TestEqual(FString::Printf(TEXT("kind %d: one shape"), static_cast<int32>(kind)), added, 1);
        TestEqual(FString::Printf(TEXT("kind %d: in the batch"), static_cast<int32>(kind)), batch.GetShapeCount(), 1);
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
    for (int32 role = 0; role < static_cast<int32>(ESOLHudPaletteRole::Count); ++role)
    {
        const FLinearColor& color = palette.Colors[role];
        TestTrue(FString::Printf(TEXT("role %d is not transparent"), role),
            color.A > 0.0f && (color.R > 0.0f || color.G > 0.0f || color.B > 0.0f));
    }
    return true;
}

#endif
