/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLGlossary.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSolidMenuSubsystem.h"
#include "SOLConstants.h"
#include "UI/SOLHudFormat.h"

#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33d-ii Help page glossary layout (Docs/SDDs/33-player-documentation.md sections 5 and 8,
// D26, D37, D39, D42), written against the 33d-ii contract before the production code existed. They pin the pure,
// widget-free logic of Menu/SOLGlossary.h that the Help page's symbol views use:
//
//   enum class ESOLGlossaryArt : uint8 { Symbols, Sample, None };
//   ESOLGlossaryArt EntryArt(int32 index);
//   FSOLGlossaryCellGrid CellGrid(int32 count);                                 // cols ceil(sqrt n), rows ceil(n/cols)
//   FBox2f CellRect(int32 cell, int32 count, const FBox2f& area);               // a short last row is centred
//   float SymbolFitScale(const FBox2f& boundsAtMaxScale, const FVector2f& cellSizePx, float maxScale);
//   FVector2f SymbolCentre(const FBox2f& boundsAtScale, const FBox2f& cell);
//   void LayoutSample(FStringView sample, const FVector2f& areaPx, float maxScale, float minScale,
//       float lineHeightAtUnit, TFunctionRef<float(FStringView)> measureAtUnit, FSOLGlossarySampleLayout& out);
//   FIntPoint ViewPixelSize(const FVector2f& localSize, float layoutScale);
//   bool ViewNeedsRebuild(const FSOLGlossaryViewStamp& stamp, const FIntPoint& sizePx, float strength);
//   void MarkViewBuilt(FSOLGlossaryViewStamp& stamp, const FIntPoint& sizePx, float strength);

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float GLOSSARYLAYOUTTEST_TOLERANCE = 1.0e-3f;
    constexpr float GLOSSARYLAYOUTTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float GLOSSARYLAYOUTTEST_INF = std::numeric_limits<float>::infinity();

    // The sample layout's test inputs: 10 units per character (spaces included), a 20-unit line, an 84 x 84 area
    constexpr float GLOSSARYLAYOUTTEST_CHAR_UNITS = 10.0f;
    constexpr float GLOSSARYLAYOUTTEST_LINE_UNITS = 20.0f;
    constexpr float GLOSSARYLAYOUTTEST_AREA = 84.0f;
    constexpr float GLOSSARYLAYOUTTEST_MAX_SCALE = 1.5f;
    constexpr float GLOSSARYLAYOUTTEST_MIN_SCALE = 0.75f;

    // Today's GLOSSARY_ENTRIES art counts (cross-checks of the table-derived expectations)
    constexpr int32 GLOSSARYLAYOUTTEST_ROWS = 45;
    constexpr int32 GLOSSARYLAYOUTTEST_SYMBOL_ROWS = 33;
    constexpr int32 GLOSSARYLAYOUTTEST_SAMPLE_ROWS = 6;
    constexpr int32 GLOSSARYLAYOUTTEST_NONE_ROWS = 6;

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of GLOSSARY_ENTRIES rows
    int32 GlossaryLayoutTestRowCount()
    {
        return static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::GLOSSARY_ENTRIES));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a table cell holds any text (null reads empty)
    bool GlossaryLayoutTestHasText(const TCHAR* text)
    {
        return text != nullptr && text[0] != 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a row's expected art from the table: Symbols when it names symbols, else Sample when it has a sample,
    // else None (independent of production)
    ESOLGlossaryArt GlossaryLayoutTestExpectedArt(const int32 index)
    {
        const SOLMenuText::FGlossaryEntry& row = SOLMenuText::GLOSSARY_ENTRIES[index];
        if (GlossaryLayoutTestHasText(row.Symbols))
        {
            return ESOLGlossaryArt::Symbols;
        }
        return GlossaryLayoutTestHasText(row.Sample) ? ESOLGlossaryArt::Sample : ESOLGlossaryArt::None;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the GLOSSARY_ENTRIES row with this Name, or INDEX_NONE
    int32 GlossaryLayoutTestRowByName(const TCHAR* name)
    {
        for (int32 index = 0; index < GlossaryLayoutTestRowCount(); ++index)
        {
            if (FCString::Strcmp(SOLMenuText::GLOSSARY_ENTRIES[index].Name, name) == 0)
            {
                return index;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an art kind's name for assertion messages
    const TCHAR* GlossaryLayoutTestArtName(const ESOLGlossaryArt art)
    {
        switch (art)
        {
        case ESOLGlossaryArt::Symbols:
            return TEXT("Symbols");
        case ESOLGlossaryArt::Sample:
            return TEXT("Sample");
        case ESOLGlossaryArt::None:
            return TEXT("None");
        default:
            return TEXT("<invalid>");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // The test measure: 10 units per character, spaces included
    float GlossaryLayoutTestMeasure(const FStringView text)
    {
        return GLOSSARYLAYOUTTEST_CHAR_UNITS * static_cast<float>(text.Len());
    }

    // The test measure as a callable object (bound to LayoutSample's TFunctionRef)
    const auto GlossaryLayoutTestMeasureRef = [](const FStringView text) { return GlossaryLayoutTestMeasure(text); };

    //////////////////////////////////////////////////////////////////////////
    // Runs LayoutSample with the test measure, line height and an 84 x 84 area at max 1.5 / min 0.75
    void GlossaryLayoutTestLayout(const TCHAR* sample, FSOLGlossarySampleLayout& out)
    {
        SOLGlossary::LayoutSample(FStringView(sample), FVector2f(GLOSSARYLAYOUTTEST_AREA, GLOSSARYLAYOUTTEST_AREA),
            GLOSSARYLAYOUTTEST_MAX_SCALE, GLOSSARYLAYOUTTEST_MIN_SCALE, GLOSSARYLAYOUTTEST_LINE_UNITS,
            GlossaryLayoutTestMeasureRef, out);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the expected cell of a valid (cell, count, area): row-major equal cells, a short last row centred
    // (independent of production)
    FBox2f GlossaryLayoutTestExpectedCell(const int32 cell, const int32 count, const FBox2f& area)
    {
        const int32 columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(count)));
        const int32 rows = (count + columns - 1) / columns;
        const float cw = (area.Max.X - area.Min.X) / columns;
        const float ch = (area.Max.Y - area.Min.Y) / rows;
        const int32 row = cell / columns;
        const int32 column = cell % columns;
        const int32 inRow = row == rows - 1 ? count - row * columns : columns;
        const float offset = (columns - inRow) * cw * 0.5f;
        const FVector2f min(area.Min.X + offset + column * cw, area.Min.Y + row * ch);
        return FBox2f(min, min + FVector2f(cw, ch));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two boxes match within the tolerance (both valid)
    bool GlossaryLayoutTestBoxNear(const FBox2f& a, const FBox2f& b)
    {
        return a.bIsValid && b.bIsValid && a.Min.Equals(b.Min, GLOSSARYLAYOUTTEST_TOLERANCE) &&
            a.Max.Equals(b.Max, GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a box's text for assertion messages
    FString GlossaryLayoutTestBoxText(const FBox2f& box)
    {
        return box.bIsValid ? FString::Printf(TEXT("(%.3f, %.3f)-(%.3f, %.3f)"), box.Min.X, box.Min.Y, box.Max.X,
            box.Max.Y) : FString(TEXT("<invalid>"));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Row art
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutEntryArtTest, "SOLTest.Glossary.Layout.EntryArt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Each row's art follows its table cells (Symbols, else Sample, else None); today 33 / 6 / 6 of 45; an index outside
// the table is None
bool FSOLGlossaryLayoutEntryArtTest::RunTest(const FString& /*parameters*/)
{
    int32 counts[3] = { 0, 0, 0 };
    for (int32 index = 0; index < GlossaryLayoutTestRowCount(); ++index)
    {
        const ESOLGlossaryArt expected = GlossaryLayoutTestExpectedArt(index);
        const ESOLGlossaryArt art = SOLGlossary::EntryArt(index);
        TestTrue(FString::Printf(TEXT("row %d (%s) is %s, got %s"), index, SOLMenuText::GLOSSARY_ENTRIES[index].Name,
            GlossaryLayoutTestArtName(expected), GlossaryLayoutTestArtName(art)), art == expected);
        ++counts[static_cast<int32>(expected)];
    }

    // Today's table
    TestEqual(TEXT("45 rows"), GlossaryLayoutTestRowCount(), GLOSSARYLAYOUTTEST_ROWS);
    TestEqual(TEXT("33 Symbols rows"), counts[static_cast<int32>(ESOLGlossaryArt::Symbols)],
        GLOSSARYLAYOUTTEST_SYMBOL_ROWS);
    TestEqual(TEXT("6 Sample rows"), counts[static_cast<int32>(ESOLGlossaryArt::Sample)],
        GLOSSARYLAYOUTTEST_SAMPLE_ROWS);
    TestEqual(TEXT("6 None rows"), counts[static_cast<int32>(ESOLGlossaryArt::None)], GLOSSARYLAYOUTTEST_NONE_ROWS);

    // Outside the table
    const int32 rows = GlossaryLayoutTestRowCount();
    const int32 badRows[] = { -1, -2, INDEX_NONE, rows, rows + 1, MIN_int32, MAX_int32 };
    for (const int32 index : badRows)
    {
        TestTrue(FString::Printf(TEXT("row %d is None"), index), SOLGlossary::EntryArt(index) == ESOLGlossaryArt::None);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutArtByNameTest, "SOLTest.Glossary.Layout.ArtByName",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The six Sample rows and the six None rows (the five D39 map rows and Space dust) by name; the rows with several
// symbols are Symbols rows with 2, 3 and 2 symbols
bool FSOLGlossaryLayoutArtByNameTest::RunTest(const FString& /*parameters*/)
{
    const TCHAR* const sampleRows[] = { TEXT("Flight mode"), TEXT("Key hints and notices"), TEXT("Autopilot marker"),
        TEXT("Chase Down line"), TEXT("Overlay profile tag"), TEXT("Route progress line") };
    const TCHAR* const noneRows[] = { TEXT("Orbit lines"), TEXT("Route legs"), TEXT("Waypoints"), TEXT("Space dust"),
        TEXT("Map destination pick"), TEXT("Map grid and stems") };
    for (const TCHAR* name : sampleRows)
    {
        const int32 index = GlossaryLayoutTestRowByName(name);
        if (TestTrue(FString::Printf(TEXT("row \"%s\" exists"), name), index != INDEX_NONE))
        {
            TestTrue(FString::Printf(TEXT("\"%s\" is a Sample row"), name),
                SOLGlossary::EntryArt(index) == ESOLGlossaryArt::Sample);
        }
    }
    for (const TCHAR* name : noneRows)
    {
        const int32 index = GlossaryLayoutTestRowByName(name);
        if (TestTrue(FString::Printf(TEXT("row \"%s\" exists"), name), index != INDEX_NONE))
        {
            TestTrue(FString::Printf(TEXT("\"%s\" is a None row"), name),
                SOLGlossary::EntryArt(index) == ESOLGlossaryArt::None);
        }
    }

    // Rows with several symbols (a grid of cells in the box)
    struct FMultiSymbolRow
    {
        const TCHAR* Name;
        int32 Symbols;
    };
    const FMultiSymbolRow multiRows[] = { { TEXT("Virtual joystick"), 2 }, { TEXT("Mouse-steer icon"), 3 },
        { TEXT("Speed card"), 2 } };
    for (const FMultiSymbolRow& row : multiRows)
    {
        const int32 index = GlossaryLayoutTestRowByName(row.Name);
        if (!TestTrue(FString::Printf(TEXT("row \"%s\" exists"), row.Name), index != INDEX_NONE))
        {
            continue;
        }
        TestTrue(FString::Printf(TEXT("\"%s\" is a Symbols row"), row.Name),
            SOLGlossary::EntryArt(index) == ESOLGlossaryArt::Symbols);
        FSOLGlossaryEntry entry;
        TestTrue(FString::Printf(TEXT("\"%s\" resolves"), row.Name), SOLGlossary::ResolveEntry(index, entry));
        TestEqual(FString::Printf(TEXT("\"%s\" symbol count"), row.Name), entry.SymbolCount, row.Symbols);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutArtPerTabTest, "SOLTest.Glossary.Layout.ArtPerTab",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Per tab (EntriesOfTab): Flight HUD 13 = 9 Symbols + 4 Sample; Targets and radar 7 Symbols; Overlays and route 16 =
// 10 Symbols + 2 Sample + 4 None; Menus and map 9 = 7 Symbols + 2 None; Keyboard has no rows
bool FSOLGlossaryLayoutArtPerTabTest::RunTest(const FString& /*parameters*/)
{
    struct FTabArt
    {
        ESOLHelpTab Tab;
        const TCHAR* Name;
        int32 Symbols;
        int32 Sample;
        int32 None;
    };
    const FTabArt tabs[] = {
        { ESOLHelpTab::FlightHud, TEXT("Flight HUD"), 9, 4, 0 },
        { ESOLHelpTab::TargetsRadar, TEXT("Targets and radar"), 7, 0, 0 },
        { ESOLHelpTab::OverlaysRoute, TEXT("Overlays and route"), 10, 2, 4 },
        { ESOLHelpTab::MenusMap, TEXT("Menus and map"), 7, 0, 2 },
    };
    TArray<int32> indices;
    int32 total = 0;
    for (const FTabArt& tab : tabs)
    {
        SOLGlossary::EntriesOfTab(tab.Tab, indices);
        int32 counts[3] = { 0, 0, 0 };
        for (const int32 index : indices)
        {
            const ESOLGlossaryArt art = SOLGlossary::EntryArt(index);
            TestTrue(FString::Printf(TEXT("%s row %d art follows the table"), tab.Name, index),
                art == GlossaryLayoutTestExpectedArt(index));
            ++counts[static_cast<int32>(GlossaryLayoutTestExpectedArt(index))];
        }
        TestEqual(FString::Printf(TEXT("%s rows"), tab.Name), indices.Num(), tab.Symbols + tab.Sample + tab.None);
        TestEqual(FString::Printf(TEXT("%s Symbols rows"), tab.Name), counts[static_cast<int32>(
            ESOLGlossaryArt::Symbols)], tab.Symbols);
        TestEqual(FString::Printf(TEXT("%s Sample rows"), tab.Name), counts[static_cast<int32>(
            ESOLGlossaryArt::Sample)], tab.Sample);
        TestEqual(FString::Printf(TEXT("%s None rows"), tab.Name), counts[static_cast<int32>(ESOLGlossaryArt::None)],
            tab.None);
        total += indices.Num();
    }
    TestEqual(TEXT("the four glossary tabs hold every row"), total, GlossaryLayoutTestRowCount());

    SOLGlossary::EntriesOfTab(ESOLHelpTab::Keyboard, indices);
    TestEqual(TEXT("Keyboard has no glossary rows"), indices.Num(), 0);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Symbol grid and cells (rule (a))
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutCellGridTest, "SOLTest.Glossary.Layout.CellGrid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A near-square grid: columns ceil(sqrt n), rows ceil(n / columns); no grid for n <= 0
bool FSOLGlossaryLayoutCellGridTest::RunTest(const FString& /*parameters*/)
{
    struct FGridCase
    {
        int32 Count;
        int32 Columns;
        int32 Rows;
    };
    const FGridCase cases[] = { { 1, 1, 1 }, { 2, 2, 1 }, { 3, 2, 2 }, { 4, 2, 2 }, { 5, 3, 2 }, { 6, 3, 2 },
        { 7, 3, 3 }, { 9, 3, 3 }, { 10, 4, 3 } };
    for (const FGridCase& gridCase : cases)
    {
        const FSOLGlossaryCellGrid grid = SOLGlossary::CellGrid(gridCase.Count);
        TestEqual(FString::Printf(TEXT("CellGrid(%d) columns"), gridCase.Count), grid.Columns, gridCase.Columns);
        TestEqual(FString::Printf(TEXT("CellGrid(%d) rows"), gridCase.Count), grid.Rows, gridCase.Rows);
    }

    // The formula over a range, and every grid holds its cells with no empty row
    for (int32 count = 1; count <= 50; ++count)
    {
        const FSOLGlossaryCellGrid grid = SOLGlossary::CellGrid(count);
        const int32 columns = FMath::CeilToInt(FMath::Sqrt(static_cast<float>(count)));
        TestEqual(FString::Printf(TEXT("CellGrid(%d) columns = ceil(sqrt n)"), count), grid.Columns, columns);
        TestEqual(FString::Printf(TEXT("CellGrid(%d) rows = ceil(n / columns)"), count), grid.Rows,
            (count + columns - 1) / columns);
        TestTrue(FString::Printf(TEXT("CellGrid(%d) holds every cell"), count), grid.Columns * grid.Rows >= count);
        TestTrue(FString::Printf(TEXT("CellGrid(%d) has no empty row"), count), (grid.Rows - 1) * grid.Columns < count);
    }

    const int32 empty[] = { 0, -1, -9, INDEX_NONE, MIN_int32 };
    for (const int32 count : empty)
    {
        const FSOLGlossaryCellGrid grid = SOLGlossary::CellGrid(count);
        TestEqual(FString::Printf(TEXT("CellGrid(%d) columns"), count), grid.Columns, 0);
        TestEqual(FString::Printf(TEXT("CellGrid(%d) rows"), count), grid.Rows, 0);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutCellRectTest, "SOLTest.Glossary.Layout.CellRect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Equal row-major cells with a short last row centred; the contract's worked examples in an 84 x 84 area; the cells of
// one count never overlap and lie inside the area, also for an area off the origin
bool FSOLGlossaryLayoutCellRectTest::RunTest(const FString& /*parameters*/)
{
    const FBox2f area(FVector2f(0.0f, 0.0f), FVector2f(84.0f, 84.0f));

    // The worked examples
    struct FCellCase
    {
        int32 Cell;
        int32 Count;
        FBox2f Expected;
    };
    const FCellCase cases[] = {
        { 0, 1, FBox2f(FVector2f(0.0f, 0.0f), FVector2f(84.0f, 84.0f)) },
        { 0, 2, FBox2f(FVector2f(0.0f, 0.0f), FVector2f(42.0f, 84.0f)) },
        { 1, 2, FBox2f(FVector2f(42.0f, 0.0f), FVector2f(84.0f, 84.0f)) },
        { 0, 3, FBox2f(FVector2f(0.0f, 0.0f), FVector2f(42.0f, 42.0f)) },
        { 1, 3, FBox2f(FVector2f(42.0f, 0.0f), FVector2f(84.0f, 42.0f)) },
        { 2, 3, FBox2f(FVector2f(21.0f, 42.0f), FVector2f(63.0f, 84.0f)) },
        { 3, 5, FBox2f(FVector2f(14.0f, 42.0f), FVector2f(42.0f, 84.0f)) },
        { 4, 5, FBox2f(FVector2f(42.0f, 42.0f), FVector2f(70.0f, 84.0f)) },
    };
    for (const FCellCase& cellCase : cases)
    {
        const FBox2f rect = SOLGlossary::CellRect(cellCase.Cell, cellCase.Count, area);
        TestTrue(FString::Printf(TEXT("count %d cell %d is %s, got %s"), cellCase.Count, cellCase.Cell,
            *GlossaryLayoutTestBoxText(cellCase.Expected), *GlossaryLayoutTestBoxText(rect)),
            GlossaryLayoutTestBoxNear(rect, cellCase.Expected));
    }

    // Every count 1-10 in two areas: the formula, inside the area, no two cells overlapping
    const FBox2f areas[] = { area, FBox2f(FVector2f(10.0f, 20.0f), FVector2f(110.0f, 80.0f)) };
    for (const FBox2f& testArea : areas)
    {
        for (int32 count = 1; count <= 10; ++count)
        {
            TArray<FBox2f> cells;
            for (int32 cell = 0; cell < count; ++cell)
            {
                const FBox2f rect = SOLGlossary::CellRect(cell, count, testArea);
                const FBox2f expected = GlossaryLayoutTestExpectedCell(cell, count, testArea);
                TestTrue(FString::Printf(TEXT("area %s count %d cell %d is %s, got %s"),
                    *GlossaryLayoutTestBoxText(testArea), count, cell, *GlossaryLayoutTestBoxText(expected),
                    *GlossaryLayoutTestBoxText(rect)), GlossaryLayoutTestBoxNear(rect, expected));
                TestTrue(FString::Printf(TEXT("count %d cell %d inside the area"), count, cell), rect.bIsValid &&
                    rect.Min.X >= testArea.Min.X - GLOSSARYLAYOUTTEST_TOLERANCE &&
                    rect.Min.Y >= testArea.Min.Y - GLOSSARYLAYOUTTEST_TOLERANCE &&
                    rect.Max.X <= testArea.Max.X + GLOSSARYLAYOUTTEST_TOLERANCE &&
                    rect.Max.Y <= testArea.Max.Y + GLOSSARYLAYOUTTEST_TOLERANCE);
                cells.Add(rect);
            }

            // No overlap: the intersection of any two cells has no area
            for (int32 a = 0; a < cells.Num(); ++a)
            {
                for (int32 b = a + 1; b < cells.Num(); ++b)
                {
                    const float overlapX = FMath::Min(cells[a].Max.X, cells[b].Max.X) -
                        FMath::Max(cells[a].Min.X, cells[b].Min.X);
                    const float overlapY = FMath::Min(cells[a].Max.Y, cells[b].Max.Y) -
                        FMath::Max(cells[a].Min.Y, cells[b].Min.Y);
                    TestFalse(FString::Printf(TEXT("count %d cells %d and %d do not overlap"), count, a, b),
                        overlapX > GLOSSARYLAYOUTTEST_TOLERANCE && overlapY > GLOSSARYLAYOUTTEST_TOLERANCE);
                }
            }
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutCellRectInvalidTest, "SOLTest.Glossary.Layout.CellRectInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// An invalid box (bIsValid false) for a cell outside [0, count), a count <= 0, an invalid area and an area with a
// non-positive width or height
bool FSOLGlossaryLayoutCellRectInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FBox2f area(FVector2f(0.0f, 0.0f), FVector2f(84.0f, 84.0f));
    struct FBadCell
    {
        int32 Cell;
        int32 Count;
    };
    const FBadCell badCells[] = { { -1, 3 }, { 3, 3 }, { 4, 3 }, { MIN_int32, 3 }, { MAX_int32, 3 }, { 0, 0 },
        { 0, -1 }, { -1, -1 }, { 0, MIN_int32 } };
    for (const FBadCell& bad : badCells)
    {
        TestFalse(FString::Printf(TEXT("cell %d of %d is invalid"), bad.Cell, bad.Count),
            static_cast<bool>(SOLGlossary::CellRect(bad.Cell, bad.Count, area).bIsValid));
    }

    // Bad areas: invalid, zero or negative width / height
    const FBox2f badAreas[] = {
        FBox2f(ForceInit),
        FBox2f(FVector2f(10.0f, 10.0f), FVector2f(10.0f, 50.0f)),
        FBox2f(FVector2f(10.0f, 10.0f), FVector2f(50.0f, 10.0f)),
        FBox2f(FVector2f(50.0f, 10.0f), FVector2f(10.0f, 50.0f)),
        FBox2f(FVector2f(10.0f, 50.0f), FVector2f(50.0f, 10.0f)),
    };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(badAreas)); ++index)
    {
        TestFalse(FString::Printf(TEXT("bad area %d gives an invalid cell"), index),
            static_cast<bool>(SOLGlossary::CellRect(0, 1, badAreas[index]).bIsValid));
        TestFalse(FString::Printf(TEXT("bad area %d gives an invalid cell (count 3)"), index),
            static_cast<bool>(SOLGlossary::CellRect(1, 3, badAreas[index]).bIsValid));
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Fit scale and centring
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutFitScaleTest, "SOLTest.Glossary.Layout.FitScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A symbol is shrunk to fit its cell and never enlarged: maxScale x min(1, cellW / w, cellH / h); only the bounds'
// size counts; a zero-size box keeps maxScale
bool FSOLGlossaryLayoutFitScaleTest::RunTest(const FString& /*parameters*/)
{
    const FVector2f cell(84.0f, 84.0f);
    const float maxScale = 1.5f;

    // The worked examples
    TestEqual(TEXT("200 x 100 in 84 x 84"), SOLGlossary::SymbolFitScale(FBox2f(FVector2f(-100.0f, -50.0f),
        FVector2f(100.0f, 50.0f)), cell, maxScale), 0.63f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("40 x 40 in 84 x 84 is not enlarged"), SOLGlossary::SymbolFitScale(FBox2f(FVector2f(-20.0f, -20.0f),
        FVector2f(20.0f, 20.0f)), cell, maxScale), 1.5f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("84 x 42 in 42 x 84"), SOLGlossary::SymbolFitScale(FBox2f(FVector2f(-42.0f, -21.0f),
        FVector2f(42.0f, 21.0f)), FVector2f(42.0f, 84.0f), maxScale), 0.75f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("off-centre (10,10)-(50,30) uses its size 40 x 20"), SOLGlossary::SymbolFitScale(
        FBox2f(FVector2f(10.0f, 10.0f), FVector2f(50.0f, 30.0f)), FVector2f(20.0f, 84.0f), maxScale), 0.75f,
        GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("off-centre equals the same size at the origin"), SOLGlossary::SymbolFitScale(
        FBox2f(FVector2f(10.0f, 10.0f), FVector2f(50.0f, 30.0f)), FVector2f(30.0f, 15.0f), maxScale),
        SOLGlossary::SymbolFitScale(FBox2f(FVector2f(-20.0f, -10.0f), FVector2f(20.0f, 10.0f)),
        FVector2f(30.0f, 15.0f), maxScale), GLOSSARYLAYOUTTEST_TOLERANCE);

    // Exactly the cell size: no shrink
    TestEqual(TEXT("84 x 84 in 84 x 84"), SOLGlossary::SymbolFitScale(FBox2f(FVector2f(0.0f, 0.0f),
        FVector2f(84.0f, 84.0f)), cell, maxScale), maxScale, GLOSSARYLAYOUTTEST_TOLERANCE);

    // A zero-size valid box keeps maxScale; a zero-width box fits by its height only
    TestEqual(TEXT("a point keeps maxScale"), SOLGlossary::SymbolFitScale(FBox2f(FVector2f(5.0f, 5.0f),
        FVector2f(5.0f, 5.0f)), cell, maxScale), maxScale, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("0 x 168 fits by its height"), SOLGlossary::SymbolFitScale(FBox2f(FVector2f(0.0f, -84.0f),
        FVector2f(0.0f, 84.0f)), cell, maxScale), 0.75f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("168 x 0 fits by its width"), SOLGlossary::SymbolFitScale(FBox2f(FVector2f(-84.0f, 0.0f),
        FVector2f(84.0f, 0.0f)), cell, maxScale), 0.75f, GLOSSARYLAYOUTTEST_TOLERANCE);

    // Never above maxScale, and the fitted bounds fit the cell
    const FVector2f sizes[] = { FVector2f(1.0f, 1.0f), FVector2f(30.0f, 300.0f), FVector2f(500.0f, 20.0f),
        FVector2f(84.0f, 85.0f), FVector2f(1000.0f, 1000.0f) };
    for (const FVector2f& size : sizes)
    {
        const float scale = SOLGlossary::SymbolFitScale(FBox2f(size * -0.5f, size * 0.5f), cell, maxScale);
        TestTrue(FString::Printf(TEXT("%.0f x %.0f: scale %f in (0, max]"), size.X, size.Y, scale),
            scale > 0.0f && scale <= maxScale + GLOSSARYLAYOUTTEST_TOLERANCE);
        const float factor = scale / maxScale;
        TestTrue(FString::Printf(TEXT("%.0f x %.0f: fits the cell"), size.X, size.Y),
            size.X * factor <= cell.X + GLOSSARYLAYOUTTEST_TOLERANCE &&
            size.Y * factor <= cell.Y + GLOSSARYLAYOUTTEST_TOLERANCE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutFitScaleInvalidTest, "SOLTest.Glossary.Layout.FitScaleInvalid",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// 0 for a maxScale that is not a positive finite number, a cell axis that is not a positive finite number, and invalid
// bounds
bool FSOLGlossaryLayoutFitScaleInvalidTest::RunTest(const FString& /*parameters*/)
{
    const FBox2f bounds(FVector2f(-20.0f, -20.0f), FVector2f(20.0f, 20.0f));
    const FVector2f cell(84.0f, 84.0f);
    const float badScales[] = { 0.0f, -1.0f, -0.5f, GLOSSARYLAYOUTTEST_NAN, GLOSSARYLAYOUTTEST_INF,
        -GLOSSARYLAYOUTTEST_INF };
    for (const float scale : badScales)
    {
        TestEqual(FString::Printf(TEXT("maxScale %f"), scale), SOLGlossary::SymbolFitScale(bounds, cell, scale), 0.0f,
            GLOSSARYLAYOUTTEST_TOLERANCE);
    }
    const float badAxes[] = { 0.0f, -84.0f, GLOSSARYLAYOUTTEST_NAN, GLOSSARYLAYOUTTEST_INF, -GLOSSARYLAYOUTTEST_INF };
    for (const float axis : badAxes)
    {
        TestEqual(FString::Printf(TEXT("cell width %f"), axis), SOLGlossary::SymbolFitScale(bounds,
            FVector2f(axis, 84.0f), 1.5f), 0.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("cell height %f"), axis), SOLGlossary::SymbolFitScale(bounds,
            FVector2f(84.0f, axis), 1.5f), 0.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    }
    TestEqual(TEXT("invalid bounds"), SOLGlossary::SymbolFitScale(FBox2f(ForceInit), cell, 1.5f), 0.0f,
        GLOSSARYLAYOUTTEST_TOLERANCE);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutCentreTest, "SOLTest.Glossary.Layout.SymbolCentre",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The build point centres the symbol's bounds in the cell (cell centre - bounds centre); invalid bounds give the cell
// centre
bool FSOLGlossaryLayoutCentreTest::RunTest(const FString& /*parameters*/)
{
    const FBox2f cell(FVector2f(0.0f, 0.0f), FVector2f(84.0f, 84.0f));

    // The worked example
    const FVector2f example = SOLGlossary::SymbolCentre(FBox2f(FVector2f(-10.0f, -20.0f), FVector2f(30.0f, 20.0f)),
        cell);
    TestEqual(TEXT("example x"), example.X, 32.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("example y"), example.Y, 42.0f, GLOSSARYLAYOUTTEST_TOLERANCE);

    // Bounds centred on the origin: the cell centre, also for a cell off the origin
    const FBox2f offCell(FVector2f(10.0f, 20.0f), FVector2f(50.0f, 40.0f));
    const FVector2f centred = SOLGlossary::SymbolCentre(FBox2f(FVector2f(-5.0f, -5.0f), FVector2f(5.0f, 5.0f)),
        offCell);
    TestEqual(TEXT("centred bounds x"), centred.X, 30.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("centred bounds y"), centred.Y, 30.0f, GLOSSARYLAYOUTTEST_TOLERANCE);

    // The bounds moved to the build point are centred in the cell
    const FBox2f bounds(FVector2f(-3.0f, 7.0f), FVector2f(21.0f, 15.0f));
    const FVector2f point = SOLGlossary::SymbolCentre(bounds, offCell);
    const FVector2f placedCentre = point + (bounds.Min + bounds.Max) * 0.5f;
    TestEqual(TEXT("placed bounds centred x"), placedCentre.X, 30.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("placed bounds centred y"), placedCentre.Y, 30.0f, GLOSSARYLAYOUTTEST_TOLERANCE);

    // Invalid bounds: the cell centre
    const FVector2f invalid = SOLGlossary::SymbolCentre(FBox2f(ForceInit), cell);
    TestEqual(TEXT("invalid bounds x"), invalid.X, 42.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("invalid bounds y"), invalid.Y, 42.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Text samples (rule (b))
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutSampleFitsTest, "SOLTest.Glossary.Layout.SampleFits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Samples that fit at or above the minimum scale: one line per '|' part, trimmed (inner runs kept), empty parts
// dropped, shrunk to the widest part and the area height, centred as a block
bool FSOLGlossaryLayoutSampleFitsTest::RunTest(const FString& /*parameters*/)
{
    FSOLGlossarySampleLayout out;

    // "MANUAL|ASSISTED|AUTO": the widest part (80) sets the scale 84 / 80
    GlossaryLayoutTestLayout(TEXT("MANUAL|ASSISTED|AUTO"), out);
    if (TestEqual(TEXT("flight modes: 3 lines"), out.Lines.Num(), 3))
    {
        TestEqual(TEXT("flight modes: scale"), out.Scale, 1.05f, GLOSSARYLAYOUTTEST_TOLERANCE);
        const TCHAR* const texts[] = { TEXT("MANUAL"), TEXT("ASSISTED"), TEXT("AUTO") };
        const FVector2f positions[] = { FVector2f(10.5f, 10.5f), FVector2f(0.0f, 31.5f), FVector2f(21.0f, 52.5f) };
        for (int32 line = 0; line < 3; ++line)
        {
            TestEqual(FString::Printf(TEXT("flight modes line %d text"), line), out.Lines[line].Text,
                FString(texts[line]));
            TestEqual(FString::Printf(TEXT("flight modes line %d x"), line), out.Lines[line].Position.X,
                positions[line].X, GLOSSARYLAYOUTTEST_TOLERANCE);
            TestEqual(FString::Printf(TEXT("flight modes line %d y"), line), out.Lines[line].Position.Y,
                positions[line].Y, GLOSSARYLAYOUTTEST_TOLERANCE);
        }
    }

    // " | AUTO |": empty parts dropped, the part trimmed; the scale capped at maxScale
    GlossaryLayoutTestLayout(TEXT(" | AUTO |"), out);
    if (TestEqual(TEXT("\" | AUTO |\": 1 line"), out.Lines.Num(), 1))
    {
        TestEqual(TEXT("\" | AUTO |\": text"), out.Lines[0].Text, FString(TEXT("AUTO")));
        TestEqual(TEXT("\" | AUTO |\": scale"), out.Scale, 1.5f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("\" | AUTO |\": x"), out.Lines[0].Position.X, 12.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("\" | AUTO |\": y"), out.Lines[0].Position.Y, 27.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    // Inner runs of spaces are kept, the ends trimmed: "ROUTE  MOVE" (110) fits at 84 / 110 >= 0.75
    GlossaryLayoutTestLayout(TEXT("  ROUTE  MOVE  "), out);
    if (TestEqual(TEXT("trimmed: 1 line"), out.Lines.Num(), 1))
    {
        TestEqual(TEXT("trimmed: inner run kept"), out.Lines[0].Text, FString(TEXT("ROUTE  MOVE")));
        TestEqual(TEXT("trimmed: scale 84 / 110"), out.Scale, 84.0f / 110.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("trimmed: x 0"), out.Lines[0].Position.X, 0.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    // Many short parts: the height term sets the scale (84 / (4 x 20) = 1.05)
    GlossaryLayoutTestLayout(TEXT("A|B|C|D"), out);
    if (TestEqual(TEXT("four parts: 4 lines"), out.Lines.Num(), 4))
    {
        TestEqual(TEXT("four parts: scale from the height"), out.Scale, 1.05f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("four parts: block fills the height"), out.Lines[0].Position.Y, 0.0f,
            GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("four parts: last line"), out.Lines[3].Position.Y, 3.0f * 21.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    // A measure of 0 (widest 0): no width term
    SOLGlossary::LayoutSample(FStringView(TEXT("AB|CD")), FVector2f(84.0f, 84.0f), 1.5f, 0.75f, 20.0f,
        [](FStringView) { return 0.0f; }, out);
    if (TestEqual(TEXT("zero measure: 2 lines"), out.Lines.Num(), 2))
    {
        TestEqual(TEXT("zero measure: scale from maxScale"), out.Scale, 1.5f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("zero measure: x is the area centre"), out.Lines[0].Position.X, 42.0f,
            GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("zero measure: top"), out.Lines[0].Position.Y, 12.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutSampleWrapsTest, "SOLTest.Glossary.Layout.SampleWraps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Below the minimum scale the sample is drawn at the minimum and each too-wide part wraps greedily at runs of spaces
// (the run at a break dropped); a single too-wide word stays whole; parts that fit stay one line; no further shrink
bool FSOLGlossaryLayoutSampleWrapsTest::RunTest(const FString& /*parameters*/)
{
    FSOLGlossarySampleLayout out;

    // "AUTOPILOT > MARS" (160): 0.525 < 0.75, so wrapped at 0.75 (a line holds 112 units)
    GlossaryLayoutTestLayout(TEXT("AUTOPILOT > MARS"), out);
    if (TestEqual(TEXT("autopilot: 2 lines"), out.Lines.Num(), 2))
    {
        TestEqual(TEXT("autopilot: scale is the minimum"), out.Scale, 0.75f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("autopilot line 0"), out.Lines[0].Text, FString(TEXT("AUTOPILOT >")));
        TestEqual(TEXT("autopilot line 1"), out.Lines[1].Text, FString(TEXT("MARS")));
        TestEqual(TEXT("autopilot top"), out.Lines[0].Position.Y, 27.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("autopilot line 1 y"), out.Lines[1].Position.Y, 42.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("autopilot line 0 x"), out.Lines[0].Position.X, (84.0f - 82.5f) * 0.5f,
            GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("autopilot line 1 x"), out.Lines[1].Position.X, (84.0f - 30.0f) * 0.5f,
            GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    // The route progress line: four lines, inner runs kept inside a line, dropped at a break
    GlossaryLayoutTestLayout(TEXT("ROUTE  MOVE  leg 2/5  40%   ETA 1:02:03"), out);
    if (TestEqual(TEXT("route: 4 lines"), out.Lines.Num(), 4))
    {
        TestEqual(TEXT("route: scale is the minimum"), out.Scale, 0.75f, GLOSSARYLAYOUTTEST_TOLERANCE);
        const TCHAR* const texts[] = { TEXT("ROUTE  MOVE"), TEXT("leg 2/5"), TEXT("40%   ETA"), TEXT("1:02:03") };
        for (int32 line = 0; line < 4; ++line)
        {
            TestEqual(FString::Printf(TEXT("route line %d text"), line), out.Lines[line].Text, FString(texts[line]));
            TestEqual(FString::Printf(TEXT("route line %d y"), line), out.Lines[line].Position.Y,
                12.0f + line * 15.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
            TestEqual(FString::Printf(TEXT("route line %d x centred"), line), out.Lines[line].Position.X,
                (84.0f - GlossaryLayoutTestMeasure(texts[line]) * 0.75f) * 0.5f, GLOSSARYLAYOUTTEST_TOLERANCE);
        }
    }

    // A single word wider than the area stays whole, centred with a negative x
    GlossaryLayoutTestLayout(TEXT("ABCDEFGHIJKLMNOP"), out);
    if (TestEqual(TEXT("long word: 1 line"), out.Lines.Num(), 1))
    {
        TestEqual(TEXT("long word: scale is the minimum"), out.Scale, 0.75f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("long word: whole"), out.Lines[0].Text, FString(TEXT("ABCDEFGHIJKLMNOP")));
        TestEqual(TEXT("long word: x"), out.Lines[0].Position.X, -18.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("long word: y"), out.Lines[0].Position.Y, 34.5f, GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    // A part that fits at the minimum stays one line while another wraps
    GlossaryLayoutTestLayout(TEXT("AUTO|AUTOPILOT > MARS"), out);
    if (TestEqual(TEXT("mixed: 3 lines"), out.Lines.Num(), 3))
    {
        TestEqual(TEXT("mixed: scale is the minimum"), out.Scale, 0.75f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("mixed line 0"), out.Lines[0].Text, FString(TEXT("AUTO")));
        TestEqual(TEXT("mixed line 1"), out.Lines[1].Text, FString(TEXT("AUTOPILOT >")));
        TestEqual(TEXT("mixed line 2"), out.Lines[2].Text, FString(TEXT("MARS")));
        TestEqual(TEXT("mixed top"), out.Lines[0].Position.Y, (84.0f - 45.0f) * 0.5f, GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    // Too many lines for the height: no further shrink, the block overflows (negative top)
    GlossaryLayoutTestLayout(TEXT("A|B|C|D|E|F|G|H"), out);
    if (TestEqual(TEXT("eight parts: 8 lines"), out.Lines.Num(), 8))
    {
        TestEqual(TEXT("eight parts: scale is the minimum"), out.Scale, 0.75f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("eight parts: negative top"), out.Lines[0].Position.Y, -18.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        TestEqual(TEXT("eight parts: last line"), out.Lines[7].Position.Y, -18.0f + 7.0f * 15.0f,
            GLOSSARYLAYOUTTEST_TOLERANCE);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutSampleEdgesTest, "SOLTest.Glossary.Layout.SampleEdges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Nothing to draw (no lines, scale 0) for no part, a bad area, a bad maxScale or a bad line height; out is reset first
// with its capacity kept; a bad or too-large minScale reads as maxScale
bool FSOLGlossaryLayoutSampleEdgesTest::RunTest(const FString& /*parameters*/)
{
    FSOLGlossarySampleLayout out;

    // A filled layout, reset by a call with nothing to draw (capacity kept)
    GlossaryLayoutTestLayout(TEXT("A|B|C|D|E"), out);
    const int32 capacity = out.Lines.Max();
    TestTrue(TEXT("pre-filled"), out.Lines.Num() == 5 && out.Scale > 0.0f);
    GlossaryLayoutTestLayout(TEXT(""), out);
    TestEqual(TEXT("\"\": no lines"), out.Lines.Num(), 0);
    TestEqual(TEXT("\"\": scale 0"), out.Scale, 0.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestTrue(TEXT("reset keeps the capacity"), out.Lines.Max() >= capacity);

    // No part
    const TCHAR* const empties[] = { TEXT(""), TEXT("|"), TEXT("||"), TEXT(" | |  "), TEXT("   ") };
    for (const TCHAR* sample : empties)
    {
        GlossaryLayoutTestLayout(TEXT("AUTO"), out);
        GlossaryLayoutTestLayout(sample, out);
        TestEqual(FString::Printf(TEXT("\"%s\": no lines"), sample), out.Lines.Num(), 0);
        TestEqual(FString::Printf(TEXT("\"%s\": scale 0"), sample), out.Scale, 0.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    // Bad area axes
    const float badValues[] = { 0.0f, -84.0f, GLOSSARYLAYOUTTEST_NAN, GLOSSARYLAYOUTTEST_INF };
    for (const float bad : badValues)
    {
        const FVector2f areas[] = { FVector2f(bad, 84.0f), FVector2f(84.0f, bad) };
        for (const FVector2f& area : areas)
        {
            GlossaryLayoutTestLayout(TEXT("AUTO"), out);
            SOLGlossary::LayoutSample(FStringView(TEXT("AUTO")), area, 1.5f, 0.75f, 20.0f,
                GlossaryLayoutTestMeasureRef, out);
            TestEqual(FString::Printf(TEXT("area (%f, %f): no lines"), area.X, area.Y), out.Lines.Num(), 0);
            TestEqual(FString::Printf(TEXT("area (%f, %f): scale 0"), area.X, area.Y), out.Scale, 0.0f,
                GLOSSARYLAYOUTTEST_TOLERANCE);
        }

        // Bad maxScale, bad line height
        SOLGlossary::LayoutSample(FStringView(TEXT("AUTO")), FVector2f(84.0f, 84.0f), bad, 0.75f, 20.0f,
            GlossaryLayoutTestMeasureRef, out);
        TestEqual(FString::Printf(TEXT("maxScale %f: no lines"), bad), out.Lines.Num(), 0);
        TestEqual(FString::Printf(TEXT("maxScale %f: scale 0"), bad), out.Scale, 0.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        SOLGlossary::LayoutSample(FStringView(TEXT("AUTO")), FVector2f(84.0f, 84.0f), 1.5f, 0.75f, bad,
            GlossaryLayoutTestMeasureRef, out);
        TestEqual(FString::Printf(TEXT("line height %f: no lines"), bad), out.Lines.Num(), 0);
        TestEqual(FString::Printf(TEXT("line height %f: scale 0"), bad), out.Scale, 0.0f,
            GLOSSARYLAYOUTTEST_TOLERANCE);
    }

    // A bad or too-large minScale reads as maxScale: "MANUAL|ASSISTED|AUTO" (1.05 < 1.5) is drawn at 1.5, its single
    // words whole on their own lines
    const float badMins[] = { 0.0f, -1.0f, GLOSSARYLAYOUTTEST_NAN, GLOSSARYLAYOUTTEST_INF, 2.0f };
    for (const float minScale : badMins)
    {
        SOLGlossary::LayoutSample(FStringView(TEXT("MANUAL|ASSISTED|AUTO")), FVector2f(84.0f, 84.0f), 1.5f, minScale,
            20.0f, GlossaryLayoutTestMeasureRef, out);
        if (TestEqual(FString::Printf(TEXT("minScale %f: 3 lines"), minScale), out.Lines.Num(), 3))
        {
            TestEqual(FString::Printf(TEXT("minScale %f: scale is maxScale"), minScale), out.Scale, 1.5f,
                GLOSSARYLAYOUTTEST_TOLERANCE);
            TestEqual(FString::Printf(TEXT("minScale %f: ASSISTED whole"), minScale), out.Lines[1].Text,
                FString(TEXT("ASSISTED")));
            TestEqual(FString::Printf(TEXT("minScale %f: ASSISTED overflows to x -18"), minScale),
                out.Lines[1].Position.X, -18.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// View pixel size and the rebuild rule
// ---------------------------------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutViewPixelSizeTest, "SOLTest.Glossary.Layout.ViewPixelSize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// The view's pixel size is its local size times the layout scale, rounded; (0, 0) for any non-finite input, a scale
// <= 0 or a local axis <= 0
bool FSOLGlossaryLayoutViewPixelSizeTest::RunTest(const FString& /*parameters*/)
{
    struct FSizeCase
    {
        FVector2f Local;
        float Scale;
        FIntPoint Expected;
    };
    const FSizeCase cases[] = {
        { FVector2f(88.0f, 88.0f), 1.0f, FIntPoint(88, 88) },
        { FVector2f(88.0f, 88.0f), 0.6666667f, FIntPoint(59, 59) },
        { FVector2f(88.0f, 88.0f), 2.0f, FIntPoint(176, 176) },
        { FVector2f(100.0f, 50.0f), 1.5f, FIntPoint(150, 75) },
        { FVector2f(2.5f, 3.5f), 1.0f, FIntPoint(3, 4) },
        { FVector2f(2.4f, 2.6f), 1.0f, FIntPoint(2, 3) },
    };
    for (const FSizeCase& sizeCase : cases)
    {
        const FIntPoint size = SOLGlossary::ViewPixelSize(sizeCase.Local, sizeCase.Scale);
        TestEqual(FString::Printf(TEXT("(%.2f, %.2f) x %f: X"), sizeCase.Local.X, sizeCase.Local.Y, sizeCase.Scale),
            size.X, sizeCase.Expected.X);
        TestEqual(FString::Printf(TEXT("(%.2f, %.2f) x %f: Y"), sizeCase.Local.X, sizeCase.Local.Y, sizeCase.Scale),
            size.Y, sizeCase.Expected.Y);
    }

    // Invalid inputs
    const FSizeCase invalid[] = {
        { FVector2f(GLOSSARYLAYOUTTEST_NAN, 88.0f), 1.0f, FIntPoint::ZeroValue },
        { FVector2f(88.0f, GLOSSARYLAYOUTTEST_NAN), 1.0f, FIntPoint::ZeroValue },
        { FVector2f(GLOSSARYLAYOUTTEST_INF, 88.0f), 1.0f, FIntPoint::ZeroValue },
        { FVector2f(88.0f, 88.0f), GLOSSARYLAYOUTTEST_NAN, FIntPoint::ZeroValue },
        { FVector2f(88.0f, 88.0f), GLOSSARYLAYOUTTEST_INF, FIntPoint::ZeroValue },
        { FVector2f(88.0f, 88.0f), 0.0f, FIntPoint::ZeroValue },
        { FVector2f(88.0f, 88.0f), -1.0f, FIntPoint::ZeroValue },
        { FVector2f(0.0f, 88.0f), 1.0f, FIntPoint::ZeroValue },
        { FVector2f(88.0f, 0.0f), 1.0f, FIntPoint::ZeroValue },
        { FVector2f(-88.0f, 88.0f), 1.0f, FIntPoint::ZeroValue },
        { FVector2f(88.0f, -1.0f), 1.0f, FIntPoint::ZeroValue },
    };
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(invalid)); ++index)
    {
        const FIntPoint size = SOLGlossary::ViewPixelSize(invalid[index].Local, invalid[index].Scale);
        TestTrue(FString::Printf(TEXT("invalid case %d is (0, 0), got (%d, %d)"), index, size.X, size.Y),
            size == FIntPoint::ZeroValue);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutViewRebuildTest, "SOLTest.Glossary.Layout.ViewRebuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// A view builds once when first laid out, then only when its pixel size or the sanitized effects strength changes
// (exact compare), never on a repeated call; clearing bBuilt forces a rebuild at any valid size; nothing is built
// before layout
bool FSOLGlossaryLayoutViewRebuildTest::RunTest(const FString& /*parameters*/)
{
    const FIntPoint size(88, 88);
    const float rangeMax = SOL::Settings::HUD_EFFECTS_MAX;
    const float rangeMin = SOL::Settings::HUD_EFFECTS_MIN;

    // Not laid out: never a rebuild, built or not
    FSOLGlossaryViewStamp stamp;
    TestFalse(TEXT("a new stamp is not built"), stamp.bBuilt);
    const FIntPoint unlaid[] = { FIntPoint(0, 0), FIntPoint(0, 88), FIntPoint(88, 0), FIntPoint(-1, -1),
        FIntPoint(88, -5) };
    for (const FIntPoint& bad : unlaid)
    {
        TestFalse(FString::Printf(TEXT("size (%d, %d) before a build: no rebuild"), bad.X, bad.Y),
            SOLGlossary::ViewNeedsRebuild(stamp, bad, 1.0f));
    }

    // First layout: build, then mark
    TestTrue(TEXT("first layout needs a build"), SOLGlossary::ViewNeedsRebuild(stamp, size, 1.0f));
    SOLGlossary::MarkViewBuilt(stamp, size, 1.0f);
    TestTrue(TEXT("marked built"), stamp.bBuilt);
    TestTrue(TEXT("marked size"), stamp.SizePx == size);
    TestEqual(TEXT("marked strength"), stamp.Strength, 1.0f, GLOSSARYLAYOUTTEST_TOLERANCE);

    // Repeated calls with the stamp's own size and strength: never a rebuild (no per-frame work)
    bool bAnyRebuild = false;
    for (int32 frame = 0; frame < 100; ++frame)
    {
        bAnyRebuild |= SOLGlossary::ViewNeedsRebuild(stamp, size, 1.0f);
    }
    TestFalse(TEXT("100 repeated calls: no rebuild"), bAnyRebuild);
    for (const FIntPoint& bad : unlaid)
    {
        TestFalse(FString::Printf(TEXT("size (%d, %d) after a build: no rebuild"), bad.X, bad.Y),
            SOLGlossary::ViewNeedsRebuild(stamp, bad, 2.0f));
    }

    // A size change (DPI / window) or a strength change: rebuild
    TestTrue(TEXT("width change"), SOLGlossary::ViewNeedsRebuild(stamp, FIntPoint(89, 88), 1.0f));
    TestTrue(TEXT("height change"), SOLGlossary::ViewNeedsRebuild(stamp, FIntPoint(88, 59), 1.0f));
    TestTrue(TEXT("strength change"), SOLGlossary::ViewNeedsRebuild(stamp, size, 0.5f));
    TestTrue(TEXT("1 -> 1.0000001 is a change (exact compare)"), SOLGlossary::ViewNeedsRebuild(stamp, size,
        1.0000001f));

    // Strengths compare after sanitizing: NaN after a build at 0, and anything at or above the range's maximum
    // after a build at the maximum, is no change
    SOLGlossary::MarkViewBuilt(stamp, size, rangeMin);
    TestFalse(TEXT("NaN after a build at 0: no change"), SOLGlossary::ViewNeedsRebuild(stamp, size,
        GLOSSARYLAYOUTTEST_NAN));
    TestFalse(TEXT("below the range after a build at its minimum: no change"), SOLGlossary::ViewNeedsRebuild(stamp,
        size, rangeMin - 3.0f));
    SOLGlossary::MarkViewBuilt(stamp, size, rangeMax + 5.0f);
    TestEqual(TEXT("marked strength is sanitized (clamped)"), stamp.Strength, rangeMax, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("marked strength equals SanitizeStrength"), stamp.Strength,
        FSOLSolidMenuParamCache::SanitizeStrength(rangeMax + 5.0f), GLOSSARYLAYOUTTEST_TOLERANCE);
    TestFalse(TEXT("above the range after a build at its maximum: no change"), SOLGlossary::ViewNeedsRebuild(stamp,
        size, rangeMax + 1.0f));
    TestFalse(TEXT("the maximum itself: no change"), SOLGlossary::ViewNeedsRebuild(stamp, size, rangeMax));
    SOLGlossary::MarkViewBuilt(stamp, size, GLOSSARYLAYOUTTEST_NAN);
    TestEqual(TEXT("NaN marks as 0"), stamp.Strength, 0.0f, GLOSSARYLAYOUTTEST_TOLERANCE);
    TestFalse(TEXT("after MarkViewBuilt with NaN, NaN is no change"), SOLGlossary::ViewNeedsRebuild(stamp, size,
        GLOSSARYLAYOUTTEST_NAN));

    // After any mark, the same inputs need no rebuild
    const float strengths[] = { 0.0f, 0.25f, 1.0f, rangeMax, -2.0f, 7.0f };
    const FIntPoint sizes[] = { FIntPoint(88, 88), FIntPoint(59, 59), FIntPoint(176, 120) };
    for (const FIntPoint& markSize : sizes)
    {
        for (const float strength : strengths)
        {
            SOLGlossary::MarkViewBuilt(stamp, markSize, strength);
            TestFalse(FString::Printf(TEXT("after a mark at (%d, %d) / %f: no rebuild"), markSize.X, markSize.Y,
                strength), SOLGlossary::ViewNeedsRebuild(stamp, markSize, strength));
        }
    }

    // A content change (new live keys) clears bBuilt: a rebuild at any valid size, none before layout
    SOLGlossary::MarkViewBuilt(stamp, size, 1.0f);
    stamp.bBuilt = false;
    TestTrue(TEXT("cleared: rebuild at the same size and strength"), SOLGlossary::ViewNeedsRebuild(stamp, size, 1.0f));
    TestTrue(TEXT("cleared: rebuild at another size"), SOLGlossary::ViewNeedsRebuild(stamp, FIntPoint(40, 40), 1.0f));
    TestFalse(TEXT("cleared: still none before layout"), SOLGlossary::ViewNeedsRebuild(stamp, FIntPoint(0, 0), 1.0f));
    return true;
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the GLOSSARY_ENTRIES row of a name, or INDEX_NONE
    int32 GlossaryLayoutTestRowOf(const TCHAR* name)
    {
        for (int32 index = 0; index < SOLGlossary::EntryCount(); ++index)
        {
            if (FCString::Strcmp(SOLMenuText::GLOSSARY_ENTRIES[index].Name, name) == 0)
            {
                return index;
            }
        }
        return INDEX_NONE;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLGlossaryLayoutSampleRoleTest, "SOLTest.Glossary.Layout.SampleLineRole",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (D2, test-after) In the Flight mode row each mode's HUD label is drawn in that mode's colour role, the HUD's own
// source SOLHudFormat::FlightModeRole (MANUAL Secondary, ASSISTED Primary, AUTO Good); any other text, any other row
// and an index outside the table are Primary. The row's formatted sample is exactly the three labels
bool FSOLGlossaryLayoutSampleRoleTest::RunTest(const FString& /*parameters*/)
{
    const int32 modeRow = GlossaryLayoutTestRowOf(TEXT("Flight mode"));
    if (!TestTrue(TEXT("the Flight mode row exists"), modeRow != INDEX_NONE))
    {
        return false;
    }
    const ESOLFlightMode modes[] = { ESOLFlightMode::Manual, ESOLFlightMode::Assisted, ESOLFlightMode::Auto };
    FString sample;
    SOLGlossary::FormatEntryText(SOLMenuText::GLOSSARY_ENTRIES[modeRow].Sample, SOLKeyBindings::DefaultKeys(), sample);
    TArray<FString> parts;
    sample.ParseIntoArray(parts, TEXT("|"));
    TestEqual(TEXT("the sample has one part per mode"), parts.Num(), static_cast<int32>(UE_ARRAY_COUNT(modes)));
    for (int32 index = 0; index < static_cast<int32>(UE_ARRAY_COUNT(modes)); ++index)
    {
        const FString label = SOLHudFormat::FlightModeLabel(modes[index]);
        TestEqual(label + TEXT(": the HUD's role"), static_cast<int32>(SOLGlossary::SampleLineRole(modeRow, label)),
            static_cast<int32>(SOLHudFormat::FlightModeRole(modes[index])));
        TestTrue(label + TEXT(": in the sample"), parts.IsValidIndex(index) && parts[index] == label);
    }

    // Anything else is Primary
    const int32 autopilotRow = GlossaryLayoutTestRowOf(TEXT("Autopilot marker"));
    TestTrue(TEXT("the Autopilot marker row exists"), autopilotRow != INDEX_NONE);
    TestEqual(TEXT("another row's AUTO"), static_cast<int32>(SOLGlossary::SampleLineRole(autopilotRow, TEXT("AUTO"))),
        static_cast<int32>(ESOLHudPaletteRole::Primary));
    TestEqual(TEXT("lower case"), static_cast<int32>(SOLGlossary::SampleLineRole(modeRow, TEXT("manual"))),
        static_cast<int32>(ESOLHudPaletteRole::Primary));
    TestEqual(TEXT("a longer line"), static_cast<int32>(SOLGlossary::SampleLineRole(modeRow, TEXT("AUTOPILOT"))),
        static_cast<int32>(ESOLHudPaletteRole::Primary));
    TestEqual(TEXT("empty"), static_cast<int32>(SOLGlossary::SampleLineRole(modeRow, TEXT(""))),
        static_cast<int32>(ESOLHudPaletteRole::Primary));
    const int32 badIndices[] = { -2, INDEX_NONE, SOLGlossary::EntryCount(), MAX_int32 };
    for (const int32 bad : badIndices)
    {
        TestEqual(FString::Printf(TEXT("index %d"), bad), static_cast<int32>(SOLGlossary::SampleLineRole(bad,
            TEXT("MANUAL"))), static_cast<int32>(ESOLHudPaletteRole::Primary));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
