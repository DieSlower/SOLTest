/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "InputCoreTypes.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a keyboard diagram layout (Docs/SDDs/33-player-documentation.md sections 7 and 10,
// D19, D30), written against the header contract (Menu/SOLKeyboardLayout.h) before the production code existed: the
// static key table (a compact US layout with the numpad and the mouse's zones), the key lookup, the hit test, the
// mouse placement and the scale for a width. Part 33c adds, also test-first, the Keyboard tab's panel layout
// (LayoutPanel: the legend, diagram, "Other keys", "Unbound" and hover strip stacked in a width) and the legend grid.
// The 33c revision (contract section E, D34-D36), also test-first: the Stacked arrangement under a width threshold
// (FitToWidth's new rule, the clusters and the mouse under the main block), the hover strip sized for a line count
// (LayoutPanel's detailLines, DIAGRAM_DETAIL_CAP_HEIGHT) and the key text fit (FitKeyText: clipped legends, the "+N"
// badge hidden when it would overlap).

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float KEYBOARDLAYOUTTEST_TOLERANCE = 1.0e-3f;     // Key-unit and pixel comparisons
    constexpr float KEYBOARDLAYOUTTEST_PX_TOLERANCE = 1.0e-2f;  // Panel pixel comparisons (sums of several sections)

    // Panel widths: narrow to wide, the two shell sizes of section 7 among them
    constexpr float KEYBOARDLAYOUTTEST_PANEL_WIDTHS[] = { 120.0f, 200.0f, 300.0f, 530.0f, 640.0f, 770.0f, 1200.0f,
        2000.0f };
    constexpr float KEYBOARDLAYOUTTEST_GRID_STEP = 0.1f;        // Hit-test sample spacing (key units)
    constexpr float KEYBOARDLAYOUTTEST_GRID_OFFSET = 0.0333f;   // Keeps the samples off the key edges
    constexpr float KEYBOARDLAYOUTTEST_KEYBOARD_WIDTH = 23.0f;  // Main block, navigation, numpad and gaps (section 7)
    constexpr float KEYBOARDLAYOUTTEST_KEYBOARD_WIDTH_SLACK = 0.5f;
    constexpr float KEYBOARDLAYOUTTEST_KEYBOARD_HEIGHT = 6.5f;  // Function row, half a row gap, five rows
    constexpr int32 KEYBOARDLAYOUTTEST_MIN_KEYS = 100;          // About 104 keyboard keys plus the mouse zones
    constexpr int32 KEYBOARDLAYOUTTEST_MAX_ROW = 5;             // The Ctrl / Space row

    // The two one-row arrangements (the keyboard keys at their table rects)
    constexpr ESOLMousePlacement KEYBOARDLAYOUTTEST_PLACEMENTS[] = { ESOLMousePlacement::Beside,
        ESOLMousePlacement::Below };

    // Every arrangement, Stacked (D34) included
    constexpr ESOLMousePlacement KEYBOARDLAYOUTTEST_ALL_PLACEMENTS[] = { ESOLMousePlacement::Beside,
        ESOLMousePlacement::Below, ESOLMousePlacement::Stacked };

    // The measured shell content width (contract section E, D37): 1280x720, the same at 1920x1080 and 3840x2160 (menu
    // units are DPI-scaled); it stacks
    constexpr float KEYBOARDLAYOUTTEST_SHELL_WIDE = 658.0f;
    constexpr float KEYBOARDLAYOUTTEST_ONE_ROW_WIDTH = 1000.0f; // Wide enough for the one-row layout, mouse beside

    // Stacked geometry (contract E.8, key units): the main block is the keyboard keys left of the cluster edge; every
    // key right of it (the function-row keys there included) and every mouse key moves by the stack offset from its
    // Beside rect; the lower group's top; the anchors and the stacked extent
    constexpr float KEYBOARDLAYOUTTEST_CLUSTER_LEFT = 15.5f;
    constexpr float KEYBOARDLAYOUTTEST_MAIN_WIDTH = 15.0f;
    constexpr float KEYBOARDLAYOUTTEST_STACK_TOP = 7.0f;
    const FVector2f KEYBOARDLAYOUTTEST_STACK_OFFSET(-13.5f, 7.0f);
    const FVector2f KEYBOARDLAYOUTTEST_STACK_LMB(10.0f, 9.0f);
    const FVector2f KEYBOARDLAYOUTTEST_STACK_EXTENT(15.0f, 13.5f);
    const FBox2f KEYBOARDLAYOUTTEST_STACK_SCROLL_LOCK(FVector2f(3.0f, 7.0f), FVector2f(4.0f, 8.0f));
    const FBox2f KEYBOARDLAYOUTTEST_STACK_INSERT(FVector2f(2.0f, 8.5f), FVector2f(3.0f, 9.5f));
    const FBox2f KEYBOARDLAYOUTTEST_STACK_NUMLOCK(FVector2f(5.5f, 8.5f), FVector2f(6.5f, 9.5f));

    // Hover strip line counts LayoutPanel is tried with (clamped to 1 .. DIAGRAM_DETAIL_LINES)
    constexpr int32 KEYBOARDLAYOUTTEST_DETAIL_LINES[] = { 1, 4 };

    // Key text fit spans (menu units) and a width past the edge tolerance
    constexpr float KEYBOARDLAYOUTTEST_FIT_LEFT = 10.0f;
    constexpr float KEYBOARDLAYOUTTEST_FIT_RIGHT = 50.0f;
    constexpr float KEYBOARDLAYOUTTEST_FIT_BADGE = 8.0f;
    constexpr float KEYBOARDLAYOUTTEST_FIT_OVER = 0.01f;

    // The numpad's engine key names (its Enter is a mirror of "Enter")
    const TCHAR* const KEYBOARDLAYOUTTEST_NUMPAD[] = { TEXT("NumPadZero"), TEXT("NumPadOne"), TEXT("NumPadTwo"),
        TEXT("NumPadThree"), TEXT("NumPadFour"), TEXT("NumPadFive"), TEXT("NumPadSix"), TEXT("NumPadSeven"),
        TEXT("NumPadEight"), TEXT("NumPadNine"), TEXT("Decimal"), TEXT("Add"), TEXT("Subtract"), TEXT("Multiply"),
        TEXT("Divide"), TEXT("NumLock") };

    // The navigation cluster and the arrows
    const TCHAR* const KEYBOARDLAYOUTTEST_NAVIGATION[] = { TEXT("Insert"), TEXT("Home"), TEXT("PageUp"),
        TEXT("Delete"), TEXT("End"), TEXT("PageDown"), TEXT("Up"), TEXT("Down"), TEXT("Left"), TEXT("Right") };

    // Keys of the main block and the function row a compact US layout has
    const TCHAR* const KEYBOARDLAYOUTTEST_MAIN[] = { TEXT("Escape"), TEXT("F1"), TEXT("F2"), TEXT("F3"), TEXT("F4"),
        TEXT("F5"), TEXT("F6"), TEXT("F7"), TEXT("F8"), TEXT("F9"), TEXT("F10"), TEXT("F11"), TEXT("F12"),
        TEXT("Tilde"), TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Five"), TEXT("Six"),
        TEXT("Seven"), TEXT("Eight"), TEXT("Nine"), TEXT("Zero"), TEXT("Hyphen"), TEXT("Equals"), TEXT("BackSpace"),
        TEXT("Tab"), TEXT("Q"), TEXT("W"), TEXT("E"), TEXT("R"), TEXT("T"), TEXT("Y"), TEXT("U"), TEXT("I"), TEXT("O"),
        TEXT("P"), TEXT("LeftBracket"), TEXT("RightBracket"), TEXT("Backslash"), TEXT("CapsLock"), TEXT("A"),
        TEXT("S"), TEXT("D"), TEXT("F"), TEXT("G"), TEXT("H"), TEXT("J"), TEXT("K"), TEXT("L"), TEXT("Semicolon"),
        TEXT("Apostrophe"), TEXT("Enter"), TEXT("LeftShift"), TEXT("Z"), TEXT("X"), TEXT("C"), TEXT("V"), TEXT("B"),
        TEXT("N"), TEXT("M"), TEXT("Comma"), TEXT("Period"), TEXT("Slash"), TEXT("RightShift"), TEXT("LeftControl"),
        TEXT("LeftAlt"), TEXT("SpaceBar"), TEXT("RightAlt"), TEXT("RightControl") };

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two rects share an area larger than the tolerance (touching edges do not overlap)
    bool KeyboardLayoutTestOverlap(const FBox2f& a, const FBox2f& b)
    {
        return a.Min.X < b.Max.X - KEYBOARDLAYOUTTEST_TOLERANCE && b.Min.X < a.Max.X - KEYBOARDLAYOUTTEST_TOLERANCE
            && a.Min.Y < b.Max.Y - KEYBOARDLAYOUTTEST_TOLERANCE && b.Min.Y < a.Max.Y - KEYBOARDLAYOUTTEST_TOLERANCE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a point lies in a rect, edges inclusive
    bool KeyboardLayoutTestInside(const FBox2f& rect, const FVector2f& point)
    {
        return point.X >= rect.Min.X && point.X <= rect.Max.X && point.Y >= rect.Min.Y && point.Y <= rect.Max.Y;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the bounds of the keyboard keys (or the mouse keys) for a placement
    FBox2f KeyboardLayoutTestBounds(const ESOLMousePlacement placement, const bool bMouse)
    {
        FBox2f bounds(ForceInit);
        const TConstArrayView<FSOLLayoutKey> keys = SOLKeyboardLayout::Keys();
        for (int32 index = 0; index < keys.Num(); ++index)
        {
            if ((keys[index].Mouse != ESOLMouseButton::None) == bMouse)
            {
                bounds += SOLKeyboardLayout::KeyRect(index, placement);
            }
        }
        return bounds;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the layout index of a key name, matching mirror keys too (unlike FindKey), or INDEX_NONE
    int32 KeyboardLayoutTestIndexOf(const TCHAR* keyName, const bool bMirror)
    {
        const TConstArrayView<FSOLLayoutKey> keys = SOLKeyboardLayout::Keys();
        for (int32 index = 0; index < keys.Num(); ++index)
        {
            if (keys[index].bMirror == bMirror && FCString::Stricmp(keys[index].KeyName, keyName) == 0)
            {
                return index;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an arrangement's name for messages
    const TCHAR* KeyboardLayoutTestPlacementName(const ESOLMousePlacement placement)
    {
        switch (placement)
        {
        case ESOLMousePlacement::Beside:
            return TEXT("beside");
        case ESOLMousePlacement::Below:
            return TEXT("below");
        case ESOLMousePlacement::Stacked:
            return TEXT("stacked");
        default:
            return TEXT("?");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Stacked rect the contract (E.8) gives a key, derived from its Beside rect: main block keys keep it,
    // every key right of the cluster edge (mirrors too) and every mouse zone moves by the stack offset
    FBox2f KeyboardLayoutTestExpectedStacked(const int32 index)
    {
        const FSOLLayoutKey& key = SOLKeyboardLayout::Keys()[index];
        const FBox2f beside = SOLKeyboardLayout::KeyRect(index, ESOLMousePlacement::Beside);
        if (key.Mouse == ESOLMouseButton::None && key.X < KEYBOARDLAYOUTTEST_CLUSTER_LEFT)
        {
            return beside;
        }
        return FBox2f(beside.Min + KEYBOARDLAYOUTTEST_STACK_OFFSET, beside.Max + KEYBOARDLAYOUTTEST_STACK_OFFSET);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the scale the FitToWidth contract (E.2) gives for a width, restated so the tests stay independent: the
    // mouse beside at a unit of at least DIAGRAM_MOUSE_BELOW_UNIT, else below at a unit of at least DIAGRAM_STACK_UNIT,
    // else stacked at the width over the main block
    FSOLDiagramScale KeyboardLayoutTestExpectedScale(const float widthPx)
    {
        using namespace SOL::Help;
        FSOLDiagramScale scale;
        if (!FMath::IsFinite(widthPx) || widthPx <= 0.0f)
        {
            return scale;
        }
        const float besideUnit = widthPx / SOLKeyboardLayout::Extent(ESOLMousePlacement::Beside).X;
        const float belowUnit = widthPx / SOLKeyboardLayout::Extent(ESOLMousePlacement::Below).X;
        float unit = 0.0f;
        if (besideUnit >= DIAGRAM_MOUSE_BELOW_UNIT)
        {
            scale.Placement = ESOLMousePlacement::Beside;
            unit = besideUnit;
        }
        else if (belowUnit >= DIAGRAM_STACK_UNIT)
        {
            scale.Placement = ESOLMousePlacement::Below;
            unit = belowUnit;
        }
        else
        {
            scale.Placement = ESOLMousePlacement::Stacked;
            unit = widthPx / KEYBOARDLAYOUTTEST_MAIN_WIDTH;
        }
        scale.UnitPx = FMath::Min(unit, DIAGRAM_MAX_UNIT);
        scale.bShortLabels = scale.UnitPx >= DIAGRAM_MIN_UNIT;
        return scale;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the hover strip's line count the LayoutPanel contract (E.3) gives: 1 to DIAGRAM_DETAIL_LINES
    int32 KeyboardLayoutTestDetailLines(const int32 detailLines)
    {
        return FMath::Clamp(detailLines, 1, SOL::Help::DIAGRAM_DETAIL_LINES);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the hover strip's height the LayoutPanel contract (E.3) gives: the cap row and the clamped lines
    float KeyboardLayoutTestDetailHeight(const int32 detailLines)
    {
        using namespace SOL::Help;
        return static_cast<float>(DIAGRAM_DETAIL_CAP_HEIGHT) + static_cast<float>(
            KeyboardLayoutTestDetailLines(detailLines)) * static_cast<float>(DIAGRAM_DETAIL_LINE_HEIGHT);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the legend columns the LayoutPanel contract gives a width: the items that fit with their gaps, 1 to
    // AREA_COUNT
    int32 KeyboardLayoutTestLegendColumns(const float widthPx)
    {
        using namespace SOL::Help;
        const float gap = static_cast<float>(DIAGRAM_LEGEND_GAP);
        const float item = static_cast<float>(DIAGRAM_LEGEND_ITEM_WIDTH);
        const int32 fit = FMath::FloorToInt32((widthPx + gap) / (item + gap));
        return FMath::Clamp(fit, 1, AREA_COUNT);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the panel the LayoutPanel contract gives, restated so the tests stay independent: the sections stacked
    // top to bottom from y 0 with a section gap under each shown one, a hidden one a zero-height box adding no gap
    FSOLDiagramPanel KeyboardLayoutTestExpectedPanel(const float widthPx, const bool bOtherKeys, const bool bUnbound,
        const int32 detailLines)
    {
        using namespace SOL::Help;
        const float width = FMath::IsFinite(widthPx) && widthPx > 0.0f ? widthPx : 0.0f;
        const float sectionGap = static_cast<float>(DIAGRAM_SECTION_GAP);
        FSOLDiagramPanel panel;
        panel.Scale = SOLKeyboardLayout::FitToWidth(width);

        // The legend grid
        panel.LegendColumns = KeyboardLayoutTestLegendColumns(width);
        panel.LegendRows = FMath::DivideAndRoundUp(AREA_COUNT, panel.LegendColumns);
        panel.Legend = FBox2f(FVector2f::ZeroVector, FVector2f(width,
            static_cast<float>(panel.LegendRows) * static_cast<float>(DIAGRAM_LEGEND_ROW_HEIGHT)));

        // The diagram at its scaled extent; the scale's origin its top-left
        const float diagramTop = panel.Legend.Max.Y + sectionGap;
        const FVector2f extent = SOLKeyboardLayout::Extent(panel.Scale.Placement) * panel.Scale.UnitPx;
        panel.Diagram = FBox2f(FVector2f(0.0f, diagramTop), FVector2f(extent.X, diagramTop + extent.Y));
        panel.Scale.OriginPx = panel.Diagram.Min;

        // The optional strips: a shown one a gap under the previous, a hidden one flat at the previous bottom
        float bottom = panel.Diagram.Max.Y;
        const float otherTop = bOtherKeys ? bottom + sectionGap : bottom;
        panel.Other = FBox2f(FVector2f(0.0f, otherTop), FVector2f(width,
            otherTop + (bOtherKeys ? static_cast<float>(DIAGRAM_STRIP_HEIGHT) : 0.0f)));
        bottom = panel.Other.Max.Y;
        const float unboundTop = bUnbound ? bottom + sectionGap : bottom;
        panel.Unbound = FBox2f(FVector2f(0.0f, unboundTop), FVector2f(width,
            unboundTop + (bUnbound ? static_cast<float>(DIAGRAM_UNBOUND_HEIGHT) : 0.0f)));
        bottom = panel.Unbound.Max.Y;

        // The hover strip, always shown: the cap row and the clamped line count
        const float detailTop = bottom + sectionGap;
        panel.DetailLines = KeyboardLayoutTestDetailLines(detailLines);
        panel.Detail = FBox2f(FVector2f(0.0f, detailTop), FVector2f(width, detailTop
            + KeyboardLayoutTestDetailHeight(detailLines)));
        panel.Height = panel.Detail.Max.Y;
        return panel;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a shown panel box matches the expected one (corners within the pixel tolerance)
    void KeyboardLayoutTestCheckBox(FAutomationTestBase& test, const FString& what, const FBox2f& actual,
        const FBox2f& expected)
    {
        test.TestTrue(FString::Printf(TEXT("%s: min (%.2f, %.2f), expected (%.2f, %.2f)"), *what, actual.Min.X,
            actual.Min.Y, expected.Min.X, expected.Min.Y), actual.Min.Equals(expected.Min,
            KEYBOARDLAYOUTTEST_PX_TOLERANCE));
        test.TestTrue(FString::Printf(TEXT("%s: max (%.2f, %.2f), expected (%.2f, %.2f)"), *what, actual.Max.X,
            actual.Max.Y, expected.Max.X, expected.Max.Y), actual.Max.Equals(expected.Max,
            KEYBOARDLAYOUTTEST_PX_TOLERANCE));
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a hidden panel box is flat at the expected top and starts at x 0 (its width is not part of the contract)
    void KeyboardLayoutTestCheckHidden(FAutomationTestBase& test, const FString& what, const FBox2f& actual,
        const float expectedTop)
    {
        test.TestEqual(what + TEXT(": hidden starts at x 0"), actual.Min.X, 0.0f, KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        test.TestEqual(what + TEXT(": hidden at the previous bottom"), actual.Min.Y, expectedTop,
            KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        test.TestEqual(what + TEXT(": hidden has zero height"), actual.Max.Y - actual.Min.Y, 0.0f,
            KEYBOARDLAYOUTTEST_PX_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a panel equals the expected one: the scale (FitToWidth, origin at the diagram), the legend grid, every
    // section box (hidden ones flat) and the height
    void KeyboardLayoutTestCheckPanel(FAutomationTestBase& test, const FString& what, const FSOLDiagramPanel& actual,
        const FSOLDiagramPanel& expected, const bool bOtherKeys, const bool bUnbound)
    {
        test.TestTrue(what + TEXT(": placement from FitToWidth"), actual.Scale.Placement == expected.Scale.Placement);
        test.TestEqual(what + TEXT(": unit from FitToWidth"), actual.Scale.UnitPx, expected.Scale.UnitPx,
            KEYBOARDLAYOUTTEST_TOLERANCE);
        test.TestTrue(what + TEXT(": short labels from FitToWidth"),
            actual.Scale.bShortLabels == expected.Scale.bShortLabels);
        test.TestTrue(what + TEXT(": origin at the diagram's top-left"),
            actual.Scale.OriginPx.Equals(actual.Diagram.Min, KEYBOARDLAYOUTTEST_PX_TOLERANCE));
        test.TestEqual(what + TEXT(": legend columns"), actual.LegendColumns, expected.LegendColumns);
        test.TestEqual(what + TEXT(": legend rows"), actual.LegendRows, expected.LegendRows);
        KeyboardLayoutTestCheckBox(test, what + TEXT(": legend"), actual.Legend, expected.Legend);
        KeyboardLayoutTestCheckBox(test, what + TEXT(": diagram"), actual.Diagram, expected.Diagram);
        if (bOtherKeys)
        {
            KeyboardLayoutTestCheckBox(test, what + TEXT(": other keys"), actual.Other, expected.Other);
        }
        else
        {
            KeyboardLayoutTestCheckHidden(test, what + TEXT(": other keys"), actual.Other, expected.Other.Min.Y);
        }
        if (bUnbound)
        {
            KeyboardLayoutTestCheckBox(test, what + TEXT(": unbound"), actual.Unbound, expected.Unbound);
        }
        else
        {
            KeyboardLayoutTestCheckHidden(test, what + TEXT(": unbound"), actual.Unbound, expected.Unbound.Min.Y);
        }
        KeyboardLayoutTestCheckBox(test, what + TEXT(": detail"), actual.Detail, expected.Detail);
        test.TestEqual(what + TEXT(": detail lines"), actual.DetailLines, expected.DetailLines);
        test.TestEqual(what + TEXT(": height is the detail's bottom"), actual.Height, actual.Detail.Max.Y,
            KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        test.TestEqual(what + TEXT(": height"), actual.Height, expected.Height, KEYBOARDLAYOUTTEST_PX_TOLERANCE);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutTableTest, "SOLTest.KeyboardLayout.Table",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The table holds a full compact keyboard then the mouse zones; every key name is unique and an engine key
bool FSOLKeyboardLayoutTableTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FSOLLayoutKey> keys = SOLKeyboardLayout::Keys();
    TestEqual(TEXT("KeyCount is the table's size"), SOLKeyboardLayout::KeyCount(), keys.Num());
    TestTrue(FString::Printf(TEXT("about 104 keys and the mouse (%d)"), keys.Num()),
        keys.Num() >= KEYBOARDLAYOUTTEST_MIN_KEYS);

    bool bMouseSeen = false;
    for (int32 index = 0; index < keys.Num(); ++index)
    {
        const FSOLLayoutKey& key = keys[index];
        const FString what = FString::Printf(TEXT("key %d '%s'"), index, key.KeyName);
        TestTrue(what + TEXT(": has a name"), key.KeyName != nullptr && key.KeyName[0] != TEXT('\0'));
        TestTrue(what + TEXT(": has a legend"), key.Legend != nullptr && key.Legend[0] != TEXT('\0'));
        TestTrue(what + TEXT(": an engine key"), FKey(key.KeyName).IsValid());
        if (key.AltKeyName != nullptr)
        {
            TestTrue(what + TEXT(": its second key is an engine key"), FKey(key.AltKeyName).IsValid());
        }
        TestTrue(what + TEXT(": positive size"), key.Width > 0.0f && key.Height > 0.0f);
        TestTrue(what + TEXT(": row in range"), key.Row >= 0 && key.Row <= KEYBOARDLAYOUTTEST_MAX_ROW);

        // Keyboard keys first, then the mouse keys
        const bool bMouse = key.Mouse != ESOLMouseButton::None;
        if (bMouseSeen)
        {
            TestTrue(what + TEXT(": the mouse keys come last"), bMouse);
        }
        bMouseSeen |= bMouse;
        if (bMouse)
        {
            TestFalse(what + TEXT(": a mouse key is no mirror"), key.bMirror);
        }

        // Names are unique (case-insensitive) among the keys that are not mirrors, second names included
        if (!key.bMirror)
        {
            for (int32 other = index + 1; other < keys.Num(); ++other)
            {
                if (keys[other].bMirror)
                {
                    continue;
                }
                TestTrue(FString::Printf(TEXT("%s: name not repeated at %d"), *what, other),
                    FCString::Stricmp(key.KeyName, keys[other].KeyName) != 0);
                if (key.AltKeyName != nullptr)
                {
                    TestTrue(FString::Printf(TEXT("%s: second name not repeated at %d"), *what, other),
                        FCString::Stricmp(key.AltKeyName, keys[other].KeyName) != 0);
                }
                if (keys[other].AltKeyName != nullptr)
                {
                    TestTrue(FString::Printf(TEXT("%s: name not another's second name (%d)"), *what, other),
                        FCString::Stricmp(key.KeyName, keys[other].AltKeyName) != 0);
                }
            }
        }
        else
        {
            TestTrue(what + TEXT(": a mirror has a main key"), KeyboardLayoutTestIndexOf(key.KeyName, false)
                != INDEX_NONE);
        }
    }
    TestTrue(TEXT("the mouse is in the table"), bMouseSeen);

    // The main block and the function row are complete
    for (const TCHAR* keyName : KEYBOARDLAYOUTTEST_MAIN)
    {
        TestTrue(FString::Printf(TEXT("%s on the layout"), keyName), SOLKeyboardLayout::FindKey(keyName)
            != INDEX_NONE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutOverlapTest, "SOLTest.KeyboardLayout.NoOverlap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// No two keys overlap, with the mouse beside or below the keyboard or the clusters stacked (D34), and every key lies
// inside the extent
bool FSOLKeyboardLayoutOverlapTest::RunTest(const FString& /*parameters*/)
{
    const int32 count = SOLKeyboardLayout::KeyCount();
    TestTrue(TEXT("the table has keys"), count > 0);
    for (const ESOLMousePlacement placement : KEYBOARDLAYOUTTEST_ALL_PLACEMENTS)
    {
        const TCHAR* placementName = KeyboardLayoutTestPlacementName(placement);
        const FVector2f extent = SOLKeyboardLayout::Extent(placement);
        int32 overlaps = 0;
        for (int32 index = 0; index < count; ++index)
        {
            const FBox2f rect = SOLKeyboardLayout::KeyRect(index, placement);
            TestTrue(FString::Printf(TEXT("mouse %s: key %d has a rect"), placementName, index), rect.bIsValid != 0);
            TestTrue(FString::Printf(TEXT("mouse %s: key %d inside the extent"), placementName, index),
                rect.Min.X >= -KEYBOARDLAYOUTTEST_TOLERANCE && rect.Min.Y >= -KEYBOARDLAYOUTTEST_TOLERANCE
                && rect.Max.X <= extent.X + KEYBOARDLAYOUTTEST_TOLERANCE
                && rect.Max.Y <= extent.Y + KEYBOARDLAYOUTTEST_TOLERANCE);
            for (int32 other = index + 1; other < count; ++other)
            {
                if (KeyboardLayoutTestOverlap(rect, SOLKeyboardLayout::KeyRect(other, placement)))
                {
                    ++overlaps;
                    AddError(FString::Printf(TEXT("mouse %s: keys %d '%s' and %d '%s' overlap"), placementName, index,
                        SOLKeyboardLayout::Keys()[index].KeyName, other, SOLKeyboardLayout::Keys()[other].KeyName));
                }
            }
        }
        TestEqual(FString::Printf(TEXT("mouse %s: no overlaps"), placementName), overlaps, 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutExtentTest, "SOLTest.KeyboardLayout.Extent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The keyboard is about 23 key units wide and 6.5 tall from the origin; the mouse sits right of it or under it
bool FSOLKeyboardLayoutExtentTest::RunTest(const FString& /*parameters*/)
{
    const FBox2f keyboard = KeyboardLayoutTestBounds(ESOLMousePlacement::Beside, false);
    TestTrue(TEXT("the keyboard has keys"), keyboard.bIsValid != 0);
    TestEqual(TEXT("keyboard starts at x 0"), keyboard.Min.X, 0.0f, KEYBOARDLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("keyboard starts at y 0"), keyboard.Min.Y, 0.0f, KEYBOARDLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("keyboard width"), keyboard.Max.X, KEYBOARDLAYOUTTEST_KEYBOARD_WIDTH,
        KEYBOARDLAYOUTTEST_KEYBOARD_WIDTH_SLACK);
    TestEqual(TEXT("keyboard height"), keyboard.Max.Y, KEYBOARDLAYOUTTEST_KEYBOARD_HEIGHT,
        KEYBOARDLAYOUTTEST_TOLERANCE);
    const FBox2f keyboardBelow = KeyboardLayoutTestBounds(ESOLMousePlacement::Below, false);
    TestTrue(TEXT("the keyboard does not move with the mouse"),
        keyboardBelow.Min.Equals(keyboard.Min, KEYBOARDLAYOUTTEST_TOLERANCE)
        && keyboardBelow.Max.Equals(keyboard.Max, KEYBOARDLAYOUTTEST_TOLERANCE));

    // The mouse beside: right of the keyboard
    const FBox2f mouseBeside = KeyboardLayoutTestBounds(ESOLMousePlacement::Beside, true);
    TestTrue(TEXT("the mouse has zones"), mouseBeside.bIsValid != 0);
    TestTrue(TEXT("mouse beside: right of the keyboard"), mouseBeside.Min.X >= keyboard.Max.X
        - KEYBOARDLAYOUTTEST_TOLERANCE);
    // The mouse below: under the keyboard
    const FBox2f mouseBelow = KeyboardLayoutTestBounds(ESOLMousePlacement::Below, true);
    TestTrue(TEXT("mouse below: under the keyboard"), mouseBelow.Min.Y >= keyboard.Max.Y
        - KEYBOARDLAYOUTTEST_TOLERANCE);
    TestTrue(TEXT("mouse below: within the keyboard's width"), mouseBelow.Max.X <= keyboard.Max.X
        + KEYBOARDLAYOUTTEST_TOLERANCE);

    // The extents hold both and differ by the mouse's place
    const FVector2f beside = SOLKeyboardLayout::Extent(ESOLMousePlacement::Beside);
    const FVector2f below = SOLKeyboardLayout::Extent(ESOLMousePlacement::Below);
    TestEqual(TEXT("beside extent width"), beside.X, mouseBeside.Max.X, KEYBOARDLAYOUTTEST_TOLERANCE);
    TestTrue(TEXT("beside extent height holds the keyboard"),
        beside.Y >= keyboard.Max.Y - KEYBOARDLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("below extent width is the keyboard's"), below.X, keyboard.Max.X, KEYBOARDLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("below extent height"), below.Y, mouseBelow.Max.Y, KEYBOARDLAYOUTTEST_TOLERANCE);
    TestTrue(TEXT("beside is wider"), beside.X > below.X);
    TestTrue(TEXT("below is taller"), below.Y > beside.Y);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutBindingKeysTest, "SOLTest.KeyboardLayout.BindingKeysOnLayout",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every binding's default key and chord modifier, and every fixed key of the Controls rows, is on the layout or the
// mouse
bool FSOLKeyboardLayoutBindingKeysTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    // The modifier key of each EKeyChord (None has none)
    const TCHAR* modifiers[] = { TEXT(""), TEXT("LeftShift"), TEXT("LeftAlt"), TEXT("LeftControl"),
        TEXT("RightShift") };
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        const FBindingDef& def = GetDef(binding);
        TestTrue(FString::Printf(TEXT("%s default key %s on the layout"), def.Id, def.DefaultKey),
            SOLKeyboardLayout::FindKey(def.DefaultKey) != INDEX_NONE);
        const int32 chord = static_cast<int32>(GetChord(binding));
        if (chord > 0 && chord < static_cast<int32>(UE_ARRAY_COUNT(modifiers)))
        {
            TestTrue(FString::Printf(TEXT("%s modifier %s on the layout"), def.Id, modifiers[chord]),
                SOLKeyboardLayout::FindKey(modifiers[chord]) != INDEX_NONE);
        }
    }

    // The fixed keys of every '+' chord alternative
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        TArray<FString> alternatives;
        FString(row.Bindings).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
        for (const FString& alternative : alternatives)
        {
            if (alternative.StartsWith(TEXT("@")))
            {
                continue;
            }
            TArray<FString> chordKeys;
            alternative.ParseIntoArray(chordKeys, SOLMenuText::BINDING_CHORD_DELIMITER);
            for (const FString& keyName : chordKeys)
            {
                TestTrue(FString::Printf(TEXT("fixed key %s of '%s' on the layout"), *keyName, row.Bindings),
                    SOLKeyboardLayout::FindKey(keyName) != INDEX_NONE);
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutNumpadTest, "SOLTest.KeyboardLayout.Numpad",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The numpad (0-9, decimal, the four operators, Num Lock and its Enter) sits right of the navigation cluster and arrows
bool FSOLKeyboardLayoutNumpadTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FSOLLayoutKey> keys = SOLKeyboardLayout::Keys();
    float navigationRight = -1.0f;
    for (const TCHAR* keyName : KEYBOARDLAYOUTTEST_NAVIGATION)
    {
        const int32 index = SOLKeyboardLayout::FindKey(keyName);
        TestTrue(FString::Printf(TEXT("%s on the layout"), keyName), index != INDEX_NONE);
        if (index != INDEX_NONE)
        {
            navigationRight = FMath::Max(navigationRight, SOLKeyboardLayout::KeyRect(index,
                ESOLMousePlacement::Below).Max.X);
        }
    }

    TArray<int32> numpad;
    for (const TCHAR* keyName : KEYBOARDLAYOUTTEST_NUMPAD)
    {
        const int32 index = SOLKeyboardLayout::FindKey(keyName);
        TestTrue(FString::Printf(TEXT("numpad %s on the layout"), keyName), index != INDEX_NONE);
        if (index != INDEX_NONE)
        {
            numpad.Add(index);
        }
    }
    const int32 numpadEnter = KeyboardLayoutTestIndexOf(TEXT("Enter"), true);
    TestTrue(TEXT("the numpad Enter is a mirror of Enter"), numpadEnter != INDEX_NONE);
    if (numpadEnter != INDEX_NONE)
    {
        numpad.Add(numpadEnter);
        TestEqual(TEXT("the numpad Enter is two rows tall"), keys[numpadEnter].Height, 2.0f,
            KEYBOARDLAYOUTTEST_TOLERANCE);
    }
    const int32 add = SOLKeyboardLayout::FindKey(TEXT("Add"));
    if (add != INDEX_NONE)
    {
        TestEqual(TEXT("the numpad Add is two rows tall"), keys[add].Height, 2.0f, KEYBOARDLAYOUTTEST_TOLERANCE);
    }

    // Right of the navigation cluster and the arrows, not overlapping them
    for (const int32 index : numpad)
    {
        const FBox2f rect = SOLKeyboardLayout::KeyRect(index, ESOLMousePlacement::Below);
        TestTrue(FString::Printf(TEXT("numpad key '%s' right of the navigation cluster"), keys[index].KeyName),
            rect.Min.X >= navigationRight - KEYBOARDLAYOUTTEST_TOLERANCE);
        for (const TCHAR* keyName : KEYBOARDLAYOUTTEST_NAVIGATION)
        {
            const int32 navigation = SOLKeyboardLayout::FindKey(keyName);
            if (navigation != INDEX_NONE)
            {
                TestFalse(FString::Printf(TEXT("numpad key '%s' clear of %s"), keys[index].KeyName, keyName),
                    KeyboardLayoutTestOverlap(rect, SOLKeyboardLayout::KeyRect(navigation, ESOLMousePlacement::Below)));
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutFindKeyTest, "SOLTest.KeyboardLayout.FindKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FindKey matches a name or a second name case-insensitively, never returns a mirror, and finds the mouse zones
bool FSOLKeyboardLayoutFindKeyTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FSOLLayoutKey> keys = SOLKeyboardLayout::Keys();
    const int32 space = SOLKeyboardLayout::FindKey(TEXT("SpaceBar"));
    TestTrue(TEXT("SpaceBar found"), space != INDEX_NONE);
    TestEqual(TEXT("case-insensitive"), SOLKeyboardLayout::FindKey(TEXT("spacebar")), space);
    TestEqual(TEXT("unknown key"), SOLKeyboardLayout::FindKey(TEXT("NoSuchKey")), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("empty name"), SOLKeyboardLayout::FindKey(FString()), static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("a key off the layout"), SOLKeyboardLayout::FindKey(TEXT("MediaPlayPause")),
        static_cast<int32>(INDEX_NONE));

    // Every key that is not a mirror is found at its own index
    for (int32 index = 0; index < keys.Num(); ++index)
    {
        if (!keys[index].bMirror)
        {
            TestEqual(FString::Printf(TEXT("'%s' found at its index"), keys[index].KeyName),
                SOLKeyboardLayout::FindKey(keys[index].KeyName), index);
        }
    }

    // Enter is the main key, not the numpad mirror
    const int32 enter = SOLKeyboardLayout::FindKey(TEXT("Enter"));
    TestTrue(TEXT("Enter found"), keys.IsValidIndex(enter));
    if (keys.IsValidIndex(enter))
    {
        TestFalse(TEXT("Enter is not the mirror"), keys[enter].bMirror);
    }

    // The mouse zones: LMB, RMB, the wheel with the middle button, the move
    const int32 left = SOLKeyboardLayout::FindKey(TEXT("LeftMouseButton"));
    const int32 right = SOLKeyboardLayout::FindKey(TEXT("RightMouseButton"));
    const int32 wheel = SOLKeyboardLayout::FindKey(TEXT("MouseWheelAxis"));
    const int32 middle = SOLKeyboardLayout::FindKey(TEXT("MiddleMouseButton"));
    const int32 move = SOLKeyboardLayout::FindKey(TEXT("Mouse2D"));
    TestTrue(TEXT("LMB zone"), keys.IsValidIndex(left) && keys[left].Mouse == ESOLMouseButton::Left);
    TestTrue(TEXT("RMB zone"), keys.IsValidIndex(right) && keys[right].Mouse == ESOLMouseButton::Right);
    TestTrue(TEXT("wheel zone"), keys.IsValidIndex(wheel)
        && (keys[wheel].Mouse == ESOLMouseButton::Wheel || keys[wheel].Mouse == ESOLMouseButton::Middle));
    TestEqual(TEXT("the middle button shares the wheel zone"), middle, wheel);
    TestTrue(TEXT("move zone"), keys.IsValidIndex(move) && keys[move].Mouse == ESOLMouseButton::Move);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutPlacementTest, "SOLTest.KeyboardLayout.MousePlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Under DIAGRAM_MOUSE_BELOW_UNIT (or for a non-finite unit) the mouse goes below the keyboard, else beside it
bool FSOLKeyboardLayoutPlacementTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    TestTrue(TEXT("at the threshold: beside"),
        SOLKeyboardLayout::MousePlacement(DIAGRAM_MOUSE_BELOW_UNIT) == ESOLMousePlacement::Beside);
    TestTrue(TEXT("just under: below"),
        SOLKeyboardLayout::MousePlacement(DIAGRAM_MOUSE_BELOW_UNIT - 0.01f) == ESOLMousePlacement::Below);
    TestTrue(TEXT("the maximum unit: beside"),
        SOLKeyboardLayout::MousePlacement(DIAGRAM_MAX_UNIT) == ESOLMousePlacement::Beside);
    TestTrue(TEXT("the minimum unit: below"),
        SOLKeyboardLayout::MousePlacement(DIAGRAM_MIN_UNIT) == ESOLMousePlacement::Below);
    TestTrue(TEXT("zero: below"), SOLKeyboardLayout::MousePlacement(0.0f) == ESOLMousePlacement::Below);
    TestTrue(TEXT("NaN: below"), SOLKeyboardLayout::MousePlacement(std::numeric_limits<float>::quiet_NaN())
        == ESOLMousePlacement::Below);
    TestTrue(TEXT("infinity: below"), SOLKeyboardLayout::MousePlacement(std::numeric_limits<float>::infinity())
        == ESOLMousePlacement::Below);

    // MousePlacement never returns Stacked (E.1: only FitToWidth stacks)
    for (float unit = 0.0f; unit <= DIAGRAM_MAX_UNIT * 2.0f; unit += 0.5f)
    {
        TestTrue(FString::Printf(TEXT("unit %.1f: not stacked"), unit),
            SOLKeyboardLayout::MousePlacement(unit) != ESOLMousePlacement::Stacked);
    }

    // KeyRect outside the table is an empty box; keyboard keys are where the table says in both placements
    TestFalse(TEXT("KeyRect(-1) empty"), SOLKeyboardLayout::KeyRect(-1, ESOLMousePlacement::Beside).bIsValid != 0);
    TestFalse(TEXT("KeyRect(count) empty"),
        SOLKeyboardLayout::KeyRect(SOLKeyboardLayout::KeyCount(), ESOLMousePlacement::Below).bIsValid != 0);
    const TConstArrayView<FSOLLayoutKey> keys = SOLKeyboardLayout::Keys();
    for (int32 index = 0; index < keys.Num(); ++index)
    {
        if (keys[index].Mouse != ESOLMouseButton::None)
        {
            continue;
        }
        const FBox2f expected(FVector2f(keys[index].X, keys[index].Y),
            FVector2f(keys[index].X + keys[index].Width, keys[index].Y + keys[index].Height));
        for (const ESOLMousePlacement placement : KEYBOARDLAYOUTTEST_PLACEMENTS)
        {
            const FBox2f rect = SOLKeyboardLayout::KeyRect(index, placement);
            TestTrue(FString::Printf(TEXT("'%s' rect from the table"), keys[index].KeyName),
                rect.Min.Equals(expected.Min, KEYBOARDLAYOUTTEST_TOLERANCE)
                && rect.Max.Equals(expected.Max, KEYBOARDLAYOUTTEST_TOLERANCE));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutKeyAtTest, "SOLTest.KeyboardLayout.KeyAt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// KeyAt returns each key at its centre, none in a gap or outside the extent, and agrees with the rects everywhere, in
// every arrangement (Stacked included)
bool FSOLKeyboardLayoutKeyAtTest::RunTest(const FString& /*parameters*/)
{
    const int32 count = SOLKeyboardLayout::KeyCount();
    for (const ESOLMousePlacement placement : KEYBOARDLAYOUTTEST_ALL_PLACEMENTS)
    {
        const TCHAR* placementName = KeyboardLayoutTestPlacementName(placement);
        TArray<FBox2f> rects;
        rects.Reserve(count);
        for (int32 index = 0; index < count; ++index)
        {
            rects.Add(SOLKeyboardLayout::KeyRect(index, placement));
            TestEqual(FString::Printf(TEXT("mouse %s: key %d at its centre"), placementName, index),
                SOLKeyboardLayout::KeyAt(rects.Last().GetCenter(), placement), index);
        }

        // Sample the extent: a point in a rect gives that key, a point in no rect (a gap) gives none
        const FVector2f extent = SOLKeyboardLayout::Extent(placement);
        int32 gaps = 0;
        int32 mismatches = 0;
        for (float y = KEYBOARDLAYOUTTEST_GRID_OFFSET; y < extent.Y; y += KEYBOARDLAYOUTTEST_GRID_STEP)
        {
            for (float x = KEYBOARDLAYOUTTEST_GRID_OFFSET; x < extent.X; x += KEYBOARDLAYOUTTEST_GRID_STEP)
            {
                const FVector2f point(x, y);
                const int32 hit = SOLKeyboardLayout::KeyAt(point, placement);
                bool bInAny = false;
                for (const FBox2f& rect : rects)
                {
                    bInAny |= KeyboardLayoutTestInside(rect, point);
                }
                gaps += bInAny ? 0 : 1;
                const bool bAgrees = bInAny ? (rects.IsValidIndex(hit) && KeyboardLayoutTestInside(rects[hit], point))
                    : hit == INDEX_NONE;
                if (!bAgrees && mismatches++ < 5)
                {
                    AddError(FString::Printf(TEXT("mouse %s: KeyAt(%.3f, %.3f) = %d disagrees with the rects"),
                        placementName, x, y, hit));
                }
            }
        }
        TestTrue(FString::Printf(TEXT("mouse %s: the layout has gaps"), placementName), gaps > 0);
        TestEqual(FString::Printf(TEXT("mouse %s: KeyAt agrees with the rects"), placementName), mismatches, 0);

        // Outside the extent
        TestEqual(FString::Printf(TEXT("mouse %s: above-left"), placementName),
            SOLKeyboardLayout::KeyAt(FVector2f(-1.0f, -1.0f), placement), static_cast<int32>(INDEX_NONE));
        TestEqual(FString::Printf(TEXT("mouse %s: right"), placementName),
            SOLKeyboardLayout::KeyAt(FVector2f(extent.X + 1.0f, 0.5f), placement), static_cast<int32>(INDEX_NONE));
        TestEqual(FString::Printf(TEXT("mouse %s: below"), placementName),
            SOLKeyboardLayout::KeyAt(FVector2f(0.5f, extent.Y + 1.0f), placement), static_cast<int32>(INDEX_NONE));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutFitTest, "SOLTest.KeyboardLayout.FitToWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The scale fits the width (mouse beside first, below when that unit is under DIAGRAM_MOUSE_BELOW_UNIT, stacked when
// the below unit is under DIAGRAM_STACK_UNIT; E.2), clamped at the maximum, hiding the short labels under the minimum;
// a bad width gives the default scale
bool FSOLKeyboardLayoutFitTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    const float widths[] = { 200.0f, 300.0f, 460.0f, 530.0f, 640.0f,
        KEYBOARDLAYOUTTEST_SHELL_WIDE, 700.0f, 770.0f, KEYBOARDLAYOUTTEST_ONE_ROW_WIDTH, 1200.0f, 5000.0f };
    for (const float width : widths)
    {
        const FString what = FString::Printf(TEXT("width %.0f"), width);
        const FSOLDiagramScale scale = SOLKeyboardLayout::FitToWidth(width);
        const FSOLDiagramScale expected = KeyboardLayoutTestExpectedScale(width);
        TestTrue(what + TEXT(": placement"), scale.Placement == expected.Placement);
        TestEqual(what + TEXT(": unit"), scale.UnitPx, expected.UnitPx, KEYBOARDLAYOUTTEST_TOLERANCE);
        TestTrue(what + TEXT(": short labels"), scale.bShortLabels == expected.bShortLabels);
        TestTrue(what + TEXT(": origin 0"), scale.OriginPx.Equals(FVector2f::ZeroVector, KEYBOARDLAYOUTTEST_TOLERANCE));
        TestTrue(what + TEXT(": fits the width"), scale.UnitPx * SOLKeyboardLayout::Extent(scale.Placement).X
            <= width + KEYBOARDLAYOUTTEST_TOLERANCE);
        TestTrue(what + TEXT(": at most the maximum unit"),
            scale.UnitPx <= DIAGRAM_MAX_UNIT + KEYBOARDLAYOUTTEST_TOLERANCE);
    }

    // The measured shell width (E.2, D37) stacks, the unit the width over the main block's 15 units
    const float shells[] = { KEYBOARDLAYOUTTEST_SHELL_WIDE };
    for (const float width : shells)
    {
        const FSOLDiagramScale scale = SOLKeyboardLayout::FitToWidth(width);
        const FString what = FString::Printf(TEXT("shell %.0f"), width);
        TestTrue(what + TEXT(": stacked"), scale.Placement == ESOLMousePlacement::Stacked);
        TestEqual(what + TEXT(": unit is the width over the main block"), scale.UnitPx,
            width / KEYBOARDLAYOUTTEST_MAIN_WIDTH, KEYBOARDLAYOUTTEST_TOLERANCE);
        TestTrue(what + TEXT(": labels shown"), scale.bShortLabels);
    }
    TestTrue(TEXT("1000 wide: one row, mouse beside"), SOLKeyboardLayout::FitToWidth(
        KEYBOARDLAYOUTTEST_ONE_ROW_WIDTH).Placement == ESOLMousePlacement::Beside);
    TestEqual(TEXT("very wide: the maximum unit"), SOLKeyboardLayout::FitToWidth(5000.0f).UnitPx, DIAGRAM_MAX_UNIT,
        KEYBOARDLAYOUTTEST_TOLERANCE);
    TestFalse(TEXT("very narrow: labels hidden"), SOLKeyboardLayout::FitToWidth(200.0f).bShortLabels);

    // Bad widths
    const float bad[] = { 0.0f, -100.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity() };
    for (const float width : bad)
    {
        const FSOLDiagramScale scale = SOLKeyboardLayout::FitToWidth(width);
        const FString what = FString::Printf(TEXT("bad width %f"), width);
        TestEqual(what + TEXT(": default unit"), scale.UnitPx, DIAGRAM_MIN_UNIT, KEYBOARDLAYOUTTEST_TOLERANCE);
        TestTrue(what + TEXT(": mouse below"), scale.Placement == ESOLMousePlacement::Below);
        TestTrue(what + TEXT(": labels shown"), scale.bShortLabels);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutScaleTest, "SOLTest.KeyboardLayout.ScaleRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ToUnits and FromUnits invert each other through the origin and the unit; a non-positive unit gives zero units
bool FSOLKeyboardLayoutScaleTest::RunTest(const FString& /*parameters*/)
{
    FSOLDiagramScale scale;
    scale.UnitPx = 29.0f;
    scale.OriginPx = FVector2f(40.0f, 12.0f);
    TestTrue(TEXT("origin maps to 0"), SOLKeyboardLayout::ToUnits(scale.OriginPx, scale).Equals(FVector2f::ZeroVector,
        KEYBOARDLAYOUTTEST_TOLERANCE));
    TestTrue(TEXT("FromUnits(0) is the origin"), SOLKeyboardLayout::FromUnits(FVector2f::ZeroVector, scale).Equals(
        scale.OriginPx, KEYBOARDLAYOUTTEST_TOLERANCE));
    TestTrue(TEXT("ToUnits formula"), SOLKeyboardLayout::ToUnits(FVector2f(98.0f, 70.0f), scale).Equals(
        FVector2f(2.0f, 2.0f), KEYBOARDLAYOUTTEST_TOLERANCE));
    const FVector2f samples[] = { FVector2f(0.5f, 0.5f), FVector2f(12.25f, 3.75f), FVector2f(22.9f, 6.4f),
        FVector2f(-1.0f, 8.0f) };
    for (const FVector2f& units : samples)
    {
        const FVector2f roundTrip = SOLKeyboardLayout::ToUnits(SOLKeyboardLayout::FromUnits(units, scale), scale);
        TestTrue(FString::Printf(TEXT("(%.2f, %.2f) round-trips"), units.X, units.Y),
            roundTrip.Equals(units, KEYBOARDLAYOUTTEST_TOLERANCE));
    }

    // A fitted scale round-trips each key's centre back to that key
    const FSOLDiagramScale fitted = SOLKeyboardLayout::FitToWidth(770.0f);
    for (int32 index = 0; index < SOLKeyboardLayout::KeyCount(); ++index)
    {
        const FVector2f centre = SOLKeyboardLayout::KeyRect(index, fitted.Placement).GetCenter();
        const FVector2f units = SOLKeyboardLayout::ToUnits(SOLKeyboardLayout::FromUnits(centre, fitted), fitted);
        TestEqual(FString::Printf(TEXT("key %d hit through the scale"), index),
            SOLKeyboardLayout::KeyAt(units, fitted.Placement), index);
    }

    // A non-positive unit
    FSOLDiagramScale flat;
    flat.UnitPx = 0.0f;
    TestTrue(TEXT("zero unit gives zero units"), SOLKeyboardLayout::ToUnits(FVector2f(100.0f, 50.0f), flat).Equals(
        FVector2f::ZeroVector, KEYBOARDLAYOUTTEST_TOLERANCE));
    flat.UnitPx = -3.0f;
    TestTrue(TEXT("negative unit gives zero units"), SOLKeyboardLayout::ToUnits(FVector2f(100.0f, 50.0f), flat).Equals(
        FVector2f::ZeroVector, KEYBOARDLAYOUTTEST_TOLERANCE));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutPanelStackTest, "SOLTest.KeyboardLayout.PanelStack",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// LayoutPanel stacks the legend, the diagram, "Other keys" and "Unbound" (when shown) and the hover strip in a width,
// a section gap under each shown one; a hidden section is flat and adds no gap; the scale is FitToWidth's with the
// origin at the diagram, which is its scaled extent; the hover strip is its cap row and the line count tall (E.3)
bool FSOLKeyboardLayoutPanelStackTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    const bool flags[] = { false, true };
    const int32 combos = 4 * static_cast<int32>(UE_ARRAY_COUNT(KEYBOARDLAYOUTTEST_DETAIL_LINES));
    for (const float width : KEYBOARDLAYOUTTEST_PANEL_WIDTHS)
    {
        // Every combination of the shown strips and the hover strip line counts
        for (int32 combo = 0; combo < combos; ++combo)
        {
            const bool bOtherKeys = flags[combo & 1];
            const bool bUnbound = flags[(combo >> 1) & 1];
            const int32 detailLines = KEYBOARDLAYOUTTEST_DETAIL_LINES[combo >> 2];
            const FString what = FString::Printf(TEXT("width %.0f other %d unbound %d lines %d"), width,
                bOtherKeys ? 1 : 0, bUnbound ? 1 : 0, detailLines);
            const FSOLDiagramPanel panel = SOLKeyboardLayout::LayoutPanel(width, bOtherKeys, bUnbound,
                detailLines);
            KeyboardLayoutTestCheckPanel(*this, what, panel, KeyboardLayoutTestExpectedPanel(width, bOtherKeys,
                bUnbound, detailLines), bOtherKeys, bUnbound);

            // The stack read directly: the order, the gaps and the section sizes
            TestEqual(what + TEXT(": legend at the top"), panel.Legend.Min.Y, 0.0f,
                KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            TestEqual(what + TEXT(": legend full width"), panel.Legend.Max.X - panel.Legend.Min.X, width,
                KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            TestEqual(what + TEXT(": legend height"), panel.Legend.Max.Y - panel.Legend.Min.Y,
                static_cast<float>(panel.LegendRows) * static_cast<float>(DIAGRAM_LEGEND_ROW_HEIGHT),
                KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            TestEqual(what + TEXT(": a gap above the diagram"), panel.Diagram.Min.Y - panel.Legend.Max.Y,
                static_cast<float>(DIAGRAM_SECTION_GAP), KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            const FVector2f size = SOLKeyboardLayout::Extent(panel.Scale.Placement) * panel.Scale.UnitPx;
            TestEqual(what + TEXT(": diagram width is the scaled extent"),
                panel.Diagram.Max.X - panel.Diagram.Min.X, size.X, KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            TestEqual(what + TEXT(": diagram height is the scaled extent"),
                panel.Diagram.Max.Y - panel.Diagram.Min.Y, size.Y, KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            TestEqual(what + TEXT(": diagram at x 0"), panel.Diagram.Min.X, 0.0f, KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            const float stripsBottom = bUnbound ? panel.Unbound.Max.Y
                : (bOtherKeys ? panel.Other.Max.Y : panel.Diagram.Max.Y);
            TestEqual(what + TEXT(": a gap above the hover strip"), panel.Detail.Min.Y - stripsBottom,
                static_cast<float>(DIAGRAM_SECTION_GAP), KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            TestEqual(what + TEXT(": hover strip height"), panel.Detail.Max.Y - panel.Detail.Min.Y,
                static_cast<float>(DIAGRAM_DETAIL_CAP_HEIGHT) + static_cast<float>(detailLines)
                * static_cast<float>(DIAGRAM_DETAIL_LINE_HEIGHT), KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            TestEqual(what + TEXT(": detail lines"), panel.DetailLines, detailLines);
            TestEqual(what + TEXT(": hover strip full width"), panel.Detail.Max.X - panel.Detail.Min.X, width,
                KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            if (bOtherKeys)
            {
                TestEqual(what + TEXT(": other keys height"), panel.Other.Max.Y - panel.Other.Min.Y,
                    static_cast<float>(DIAGRAM_STRIP_HEIGHT), KEYBOARDLAYOUTTEST_PX_TOLERANCE);
                TestEqual(what + TEXT(": other keys full width"), panel.Other.Max.X - panel.Other.Min.X, width,
                    KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            }
            if (bUnbound)
            {
                TestEqual(what + TEXT(": unbound height"), panel.Unbound.Max.Y - panel.Unbound.Min.Y,
                    static_cast<float>(DIAGRAM_UNBOUND_HEIGHT), KEYBOARDLAYOUTTEST_PX_TOLERANCE);
                TestEqual(what + TEXT(": unbound full width"), panel.Unbound.Max.X - panel.Unbound.Min.X, width,
                    KEYBOARDLAYOUTTEST_PX_TOLERANCE);
                TestTrue(what + TEXT(": unbound under other keys"), panel.Unbound.Min.Y >= panel.Other.Max.Y
                    - KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            }
        }

        // Each shown strip makes the panel taller by its height and one gap; each hover strip line by a line height
        const float plain = SOLKeyboardLayout::LayoutPanel(width, false, false, 1).Height;
        TestEqual(FString::Printf(TEXT("width %.0f: other keys add a strip and a gap"), width),
            SOLKeyboardLayout::LayoutPanel(width, true, false, 1).Height - plain,
            static_cast<float>(DIAGRAM_STRIP_HEIGHT) + static_cast<float>(DIAGRAM_SECTION_GAP),
            KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        TestEqual(FString::Printf(TEXT("width %.0f: unbound adds a line and a gap"), width),
            SOLKeyboardLayout::LayoutPanel(width, false, true, 1).Height - plain,
            static_cast<float>(DIAGRAM_UNBOUND_HEIGHT) + static_cast<float>(DIAGRAM_SECTION_GAP),
            KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        TestEqual(FString::Printf(TEXT("width %.0f: a second hover line adds a line height"), width),
            SOLKeyboardLayout::LayoutPanel(width, false, false, 2).Height - plain,
            static_cast<float>(DIAGRAM_DETAIL_LINE_HEIGHT), KEYBOARDLAYOUTTEST_PX_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutPanelLegendTest, "SOLTest.KeyboardLayout.PanelLegend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The area legend is a grid of AREA_COUNT items, as many columns as fit (1 to AREA_COUNT), row-major; LegendItemRect
// gives each item's rect and an empty box out of range
bool FSOLKeyboardLayoutPanelLegendTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    const float item = static_cast<float>(DIAGRAM_LEGEND_ITEM_WIDTH);
    const float gap = static_cast<float>(DIAGRAM_LEGEND_GAP);
    const float rowHeight = static_cast<float>(DIAGRAM_LEGEND_ROW_HEIGHT);
    for (const float width : KEYBOARDLAYOUTTEST_PANEL_WIDTHS)
    {
        const FString what = FString::Printf(TEXT("width %.0f"), width);
        const FSOLDiagramPanel panel = SOLKeyboardLayout::LayoutPanel(width, false, false, 1);
        TestEqual(what + TEXT(": columns"), panel.LegendColumns, KeyboardLayoutTestLegendColumns(width));
        TestTrue(what + TEXT(": 1 to AREA_COUNT columns"), panel.LegendColumns >= 1
            && panel.LegendColumns <= AREA_COUNT);
        TestEqual(what + TEXT(": rows hold every area"), panel.LegendRows,
            FMath::DivideAndRoundUp(AREA_COUNT, FMath::Max(panel.LegendColumns, 1)));
        if (panel.LegendColumns < 1)
        {
            continue;
        }

        // Row-major, each item its width and a row tall, a gap apart, inside the legend
        for (int32 area = 0; area < AREA_COUNT; ++area)
        {
            const int32 column = area % panel.LegendColumns;
            const int32 row = area / panel.LegendColumns;
            const FVector2f itemMin = panel.Legend.Min + FVector2f(static_cast<float>(column) * (item + gap),
                static_cast<float>(row) * rowHeight);
            const FBox2f expected(itemMin, itemMin + FVector2f(item, rowHeight));
            const FBox2f rect = SOLKeyboardLayout::LegendItemRect(panel, area);
            TestTrue(FString::Printf(TEXT("%s: area %d has a rect"), *what, area), rect.bIsValid != 0);
            KeyboardLayoutTestCheckBox(*this, FString::Printf(TEXT("%s: area %d"), *what, area), rect, expected);
            TestTrue(FString::Printf(TEXT("%s: area %d inside the legend's height"), *what, area),
                rect.Max.Y <= panel.Legend.Max.Y + KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            if (width >= item)
            {
                TestTrue(FString::Printf(TEXT("%s: area %d inside the width"), *what, area),
                    rect.Max.X <= width + KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            }
        }

        // Out of range
        TestFalse(what + TEXT(": area -1 empty"), SOLKeyboardLayout::LegendItemRect(panel, -1).bIsValid != 0);
        TestFalse(what + TEXT(": area AREA_COUNT empty"),
            SOLKeyboardLayout::LegendItemRect(panel, AREA_COUNT).bIsValid != 0);
    }

    // A wide panel puts every area on one row; a narrow one a single column
    const FSOLDiagramPanel wide = SOLKeyboardLayout::LayoutPanel(
        static_cast<float>(AREA_COUNT) * (item + gap) + item, false, false, 1);
    TestEqual(TEXT("wide: AREA_COUNT columns"), wide.LegendColumns, AREA_COUNT);
    TestEqual(TEXT("wide: one row"), wide.LegendRows, 1);
    const FSOLDiagramPanel narrow = SOLKeyboardLayout::LayoutPanel(item * 0.5f, false, false, 1);
    TestEqual(TEXT("narrow: one column"), narrow.LegendColumns, 1);
    TestEqual(TEXT("narrow: a row per area"), narrow.LegendRows, AREA_COUNT);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutPanelBadWidthTest, "SOLTest.KeyboardLayout.PanelInvalidWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A width that is not a positive finite number lays the panel out at width 0 with the default FitToWidth scale (its
// origin moved to the diagram)
bool FSOLKeyboardLayoutPanelBadWidthTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    const float bad[] = { 0.0f, -100.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity() };
    const FSOLDiagramScale defaultScale = SOLKeyboardLayout::FitToWidth(0.0f);
    for (const float width : bad)
    {
        const FString what = FString::Printf(TEXT("bad width %f"), width);
        const FSOLDiagramPanel panel = SOLKeyboardLayout::LayoutPanel(width, true, true, 3);
        KeyboardLayoutTestCheckPanel(*this, what, panel, KeyboardLayoutTestExpectedPanel(0.0f, true, true, 3), true,
            true);
        TestEqual(what + TEXT(": the default unit"), panel.Scale.UnitPx, defaultScale.UnitPx,
            KEYBOARDLAYOUTTEST_TOLERANCE);
        TestTrue(what + TEXT(": the default placement"), panel.Scale.Placement == defaultScale.Placement);
        TestEqual(what + TEXT(": the legend at width 0"), panel.Legend.Max.X, 0.0f, KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        TestEqual(what + TEXT(": the hover strip at width 0"), panel.Detail.Max.X, 0.0f,
            KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        TestEqual(what + TEXT(": one legend column"), panel.LegendColumns, 1);
        TestTrue(what + TEXT(": a finite height"), FMath::IsFinite(panel.Height) && panel.Height > 0.0f);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutPanelShellTest, "SOLTest.KeyboardLayout.PanelShellWidths",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At the measured shell width (E.2, D37) the panel stacks the clusters (the diagram box the stacked extent, 15 x 13.5
// units, at the width over 15) and every key's centre maps through its scale into the box and back onto that key; a
// width of 1000 keeps the one-row layout with the mouse beside
bool FSOLKeyboardLayoutPanelShellTest::RunTest(const FString& /*parameters*/)
{
    const float shells[] = { KEYBOARDLAYOUTTEST_SHELL_WIDE };
    for (const float width : shells)
    {
        const FString what = FString::Printf(TEXT("%.0f"), width);
        const FSOLDiagramPanel panel = SOLKeyboardLayout::LayoutPanel(width, true, true, 1);
        TestTrue(what + TEXT(": stacked"), panel.Scale.Placement == ESOLMousePlacement::Stacked);
        TestTrue(what + TEXT(": as FitToWidth"),
            panel.Scale.Placement == SOLKeyboardLayout::FitToWidth(width).Placement);
        const float unit = width / KEYBOARDLAYOUTTEST_MAIN_WIDTH;
        TestEqual(what + TEXT(": unit"), panel.Scale.UnitPx, unit, KEYBOARDLAYOUTTEST_TOLERANCE);
        TestEqual(what + TEXT(": diagram 15 units wide"), panel.Diagram.Max.X - panel.Diagram.Min.X,
            KEYBOARDLAYOUTTEST_STACK_EXTENT.X * unit, KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        TestEqual(what + TEXT(": diagram 13.5 units tall"), panel.Diagram.Max.Y - panel.Diagram.Min.Y,
            KEYBOARDLAYOUTTEST_STACK_EXTENT.Y * unit, KEYBOARDLAYOUTTEST_PX_TOLERANCE);
        TestTrue(what + TEXT(": the diagram fits the width"), panel.Diagram.Max.X <= width
            + KEYBOARDLAYOUTTEST_PX_TOLERANCE);

        // A key's centre (mirrors aside) maps through the panel's scale into the diagram box and back onto that key
        const TConstArrayView<FSOLLayoutKey> keys = SOLKeyboardLayout::Keys();
        for (int32 index = 0; index < keys.Num(); ++index)
        {
            if (keys[index].bMirror)
            {
                continue;
            }
            const FVector2f centre = SOLKeyboardLayout::KeyRect(index, panel.Scale.Placement).GetCenter();
            const FVector2f px = SOLKeyboardLayout::FromUnits(centre, panel.Scale);
            TestTrue(FString::Printf(TEXT("%s: key %d inside the diagram box"), *what, index),
                KeyboardLayoutTestInside(panel.Diagram, px));
            TestEqual(FString::Printf(TEXT("%s: key %d hit through the panel scale"), *what, index),
                SOLKeyboardLayout::KeyAt(SOLKeyboardLayout::ToUnits(px, panel.Scale), panel.Scale.Placement), index);
        }
    }

    // A wide width: one row, the mouse beside
    const FSOLDiagramPanel wide = SOLKeyboardLayout::LayoutPanel(KEYBOARDLAYOUTTEST_ONE_ROW_WIDTH, false, false, 1);
    TestTrue(TEXT("1000: mouse beside"), wide.Scale.Placement == ESOLMousePlacement::Beside);
    TestTrue(TEXT("1000: the diagram fits the width"), wide.Diagram.Max.X <= KEYBOARDLAYOUTTEST_ONE_ROW_WIDTH
        + KEYBOARDLAYOUTTEST_PX_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutFitBoundaryTest, "SOLTest.KeyboardLayout.FitToWidthBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FitToWidth's two boundaries (E.2), computed from the constants: DIAGRAM_MOUSE_BELOW_UNIT x Extent(Beside).X is Beside
// and a hair less is not; DIAGRAM_STACK_UNIT x Extent(Below).X is Below and a hair less is Stacked (guarded: the stack
// boundary must lie under the beside one, so a retune that breaks the order fails here, not silently)
bool FSOLKeyboardLayoutFitBoundaryTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    constexpr float hair = 0.01f;                   // Menu units under a boundary
    constexpr float rounding = 1.0e-3f;             // Menu units over a boundary, against float rounding
    const float besideBoundary = DIAGRAM_MOUSE_BELOW_UNIT * SOLKeyboardLayout::Extent(ESOLMousePlacement::Beside).X;
    const float stackBoundary = DIAGRAM_STACK_UNIT * SOLKeyboardLayout::Extent(ESOLMousePlacement::Below).X;

    // The beside boundary
    const FSOLDiagramScale atBeside = SOLKeyboardLayout::FitToWidth(besideBoundary + rounding);
    TestTrue(TEXT("the beside boundary: beside"), atBeside.Placement == ESOLMousePlacement::Beside);
    TestEqual(TEXT("the beside boundary: the unit is DIAGRAM_MOUSE_BELOW_UNIT"), atBeside.UnitPx,
        FMath::Min(DIAGRAM_MOUSE_BELOW_UNIT, DIAGRAM_MAX_UNIT), KEYBOARDLAYOUTTEST_TOLERANCE);
    TestTrue(TEXT("a hair under the beside boundary: not beside"),
        SOLKeyboardLayout::FitToWidth(besideBoundary - hair).Placement != ESOLMousePlacement::Beside);

    // The stack boundary, which must lie under the beside one for Below to exist
    if (!TestTrue(FString::Printf(TEXT("the stack boundary (%.2f) is under the beside boundary (%.2f)"), stackBoundary,
        besideBoundary), stackBoundary < besideBoundary - hair))
    {
        return true;
    }
    const FSOLDiagramScale atStack = SOLKeyboardLayout::FitToWidth(stackBoundary + rounding);
    TestTrue(TEXT("the stack boundary: below"), atStack.Placement == ESOLMousePlacement::Below);
    TestEqual(TEXT("the stack boundary: the unit is DIAGRAM_STACK_UNIT"), atStack.UnitPx,
        FMath::Min(DIAGRAM_STACK_UNIT, DIAGRAM_MAX_UNIT), KEYBOARDLAYOUTTEST_TOLERANCE);
    const FSOLDiagramScale underStack = SOLKeyboardLayout::FitToWidth(stackBoundary - hair);
    TestTrue(TEXT("a hair under the stack boundary: stacked"), underStack.Placement == ESOLMousePlacement::Stacked);
    TestEqual(TEXT("a hair under the stack boundary: the unit is the width over 15"), underStack.UnitPx,
        FMath::Min((stackBoundary - hair) / KEYBOARDLAYOUTTEST_MAIN_WIDTH, DIAGRAM_MAX_UNIT),
        KEYBOARDLAYOUTTEST_TOLERANCE);
    const FSOLDiagramScale between = SOLKeyboardLayout::FitToWidth(0.5f * (stackBoundary + besideBoundary));
    TestTrue(TEXT("between the boundaries: below"), between.Placement == ESOLMousePlacement::Below);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutStackedTest, "SOLTest.KeyboardLayout.StackedRects",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Stacked (E.8): the main block keeps its rects; every key right of it (Scroll Lock, Pause, the clusters, mirrors with
// them) and the mouse move under it by one offset, every rect derived from the key's Beside rect; the anchors (Scroll
// Lock, Insert, Num Lock, LMB) and the extent (15, 13.5) as the contract states; Beside and Below unchanged
bool FSOLKeyboardLayoutStackedTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FSOLLayoutKey> keys = SOLKeyboardLayout::Keys();
    int32 moved = 0;
    int32 kept = 0;
    for (int32 index = 0; index < keys.Num(); ++index)
    {
        const FBox2f rect = SOLKeyboardLayout::KeyRect(index, ESOLMousePlacement::Stacked);
        const FBox2f expected = KeyboardLayoutTestExpectedStacked(index);
        const FString what = FString::Printf(TEXT("stacked key %d '%s'"), index, keys[index].KeyName);
        TestTrue(what + TEXT(": has a rect"), rect.bIsValid != 0);
        TestTrue(FString::Printf(TEXT("%s: min (%.2f, %.2f), expected (%.2f, %.2f)"), *what, rect.Min.X, rect.Min.Y,
            expected.Min.X, expected.Min.Y), rect.Min.Equals(expected.Min, KEYBOARDLAYOUTTEST_TOLERANCE));
        TestTrue(FString::Printf(TEXT("%s: max (%.2f, %.2f), expected (%.2f, %.2f)"), *what, rect.Max.X, rect.Max.Y,
            expected.Max.X, expected.Max.Y), rect.Max.Equals(expected.Max, KEYBOARDLAYOUTTEST_TOLERANCE));
        const bool bMainBlock = keys[index].Mouse == ESOLMouseButton::None
            && keys[index].X < KEYBOARDLAYOUTTEST_CLUSTER_LEFT;
        (bMainBlock ? kept : moved) += 1;
    }
    TestTrue(TEXT("stacked: main block keys kept"), kept > 0);
    TestTrue(TEXT("stacked: cluster and mouse keys moved"), moved > 0);

    // The anchors
    const TCHAR* anchorKeys[] = { TEXT("ScrollLock"), TEXT("Insert"), TEXT("NumLock") };
    const FBox2f anchorRects[] = { KEYBOARDLAYOUTTEST_STACK_SCROLL_LOCK, KEYBOARDLAYOUTTEST_STACK_INSERT,
        KEYBOARDLAYOUTTEST_STACK_NUMLOCK };
    for (int32 anchor = 0; anchor < static_cast<int32>(UE_ARRAY_COUNT(anchorKeys)); ++anchor)
    {
        const FBox2f rect = SOLKeyboardLayout::KeyRect(SOLKeyboardLayout::FindKey(anchorKeys[anchor]),
            ESOLMousePlacement::Stacked);
        TestTrue(FString::Printf(TEXT("stacked: %s at its anchor"), anchorKeys[anchor]),
            rect.Min.Equals(anchorRects[anchor].Min, KEYBOARDLAYOUTTEST_TOLERANCE)
            && rect.Max.Equals(anchorRects[anchor].Max, KEYBOARDLAYOUTTEST_TOLERANCE));
    }
    const FBox2f leftMouse = SOLKeyboardLayout::KeyRect(SOLKeyboardLayout::FindKey(TEXT("LeftMouseButton")),
        ESOLMousePlacement::Stacked);
    TestTrue(FString::Printf(TEXT("stacked: LMB's top-left (%.2f, %.2f) at its anchor"), leftMouse.Min.X,
        leftMouse.Min.Y), leftMouse.Min.Equals(KEYBOARDLAYOUTTEST_STACK_LMB, KEYBOARDLAYOUTTEST_TOLERANCE));

    // The extent: the main block's width, the numpad's bottom; the mouse bounds inside it
    const FVector2f extent = SOLKeyboardLayout::Extent(ESOLMousePlacement::Stacked);
    TestTrue(FString::Printf(TEXT("stacked extent (%.2f, %.2f) is (15, 13.5)"), extent.X, extent.Y),
        extent.Equals(KEYBOARDLAYOUTTEST_STACK_EXTENT, KEYBOARDLAYOUTTEST_TOLERANCE));
    const FBox2f keyboard = KeyboardLayoutTestBounds(ESOLMousePlacement::Stacked, false);
    const FBox2f mouse = KeyboardLayoutTestBounds(ESOLMousePlacement::Stacked, true);
    TestEqual(TEXT("stacked keyboard bottom is the extent's"), keyboard.Max.Y, extent.Y, KEYBOARDLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("stacked keyboard width is the main block's"), keyboard.Max.X, extent.X,
        KEYBOARDLAYOUTTEST_TOLERANCE);
    TestTrue(TEXT("stacked mouse under the main block"), mouse.Min.Y >= KEYBOARDLAYOUTTEST_STACK_TOP
        - KEYBOARDLAYOUTTEST_TOLERANCE);

    // Beside and Below unchanged
    TestEqual(TEXT("beside extent width unchanged"), SOLKeyboardLayout::Extent(ESOLMousePlacement::Beside).X, 26.5f,
        KEYBOARDLAYOUTTEST_TOLERANCE);
    TestEqual(TEXT("below extent width unchanged"), SOLKeyboardLayout::Extent(ESOLMousePlacement::Below).X, 23.0f,
        KEYBOARDLAYOUTTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutPanelDetailLinesTest, "SOLTest.KeyboardLayout.PanelDetailLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hover strip (E.3) is DIAGRAM_DETAIL_CAP_HEIGHT plus clamp(detailLines, 1, DIAGRAM_DETAIL_LINES) lines tall and
// DetailLines is that clamped count: 0 and negative counts give one line, counts over the cap the cap; nothing above
// the strip moves
bool FSOLKeyboardLayoutPanelDetailLinesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    const int32 counts[] = { MIN_int32, -3, 0, 1, 2, DIAGRAM_DETAIL_LINES - 1, DIAGRAM_DETAIL_LINES,
        DIAGRAM_DETAIL_LINES + 1, DIAGRAM_DETAIL_LINES + 50, MAX_int32 };
    const float widths[] = { KEYBOARDLAYOUTTEST_SHELL_WIDE, KEYBOARDLAYOUTTEST_ONE_ROW_WIDTH };
    for (const float width : widths)
    {
        const FSOLDiagramPanel one = SOLKeyboardLayout::LayoutPanel(width, true, true, 1);
        for (const int32 count : counts)
        {
            const FString what = FString::Printf(TEXT("width %.0f lines %d"), width, count);
            const FSOLDiagramPanel panel = SOLKeyboardLayout::LayoutPanel(width, true, true, count);
            const int32 clamped = FMath::Clamp(count, 1, DIAGRAM_DETAIL_LINES);
            TestEqual(what + TEXT(": detail lines clamped"), panel.DetailLines, clamped);
            TestEqual(what + TEXT(": strip height"), panel.Detail.Max.Y - panel.Detail.Min.Y,
                static_cast<float>(DIAGRAM_DETAIL_CAP_HEIGHT) + static_cast<float>(clamped)
                * static_cast<float>(DIAGRAM_DETAIL_LINE_HEIGHT), KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            TestEqual(what + TEXT(": height is the strip's bottom"), panel.Height, panel.Detail.Max.Y,
                KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            KeyboardLayoutTestCheckPanel(*this, what, panel, KeyboardLayoutTestExpectedPanel(width, true, true, count),
                true, true);

            // The sections above the strip do not depend on the line count
            TestEqual(what + TEXT(": strip top as with one line"), panel.Detail.Min.Y, one.Detail.Min.Y,
                KEYBOARDLAYOUTTEST_PX_TOLERANCE);
            KeyboardLayoutTestCheckBox(*this, what + TEXT(": diagram as with one line"), panel.Diagram, one.Diagram);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyboardLayoutFitKeyTextTest, "SOLTest.KeyboardLayout.FitKeyText",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FitKeyText (E.4, D35): the legend centred beside the "+N" badge when both fit (edges inclusive within
// KINDA_SMALL_NUMBER), else the badge hidden and the legend centred in the whole span, clipped at its left when wider;
// no badge, no legend, and an empty or negative span
bool FSOLKeyboardLayoutFitKeyTextTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    const float left = KEYBOARDLAYOUTTEST_FIT_LEFT;
    const float right = KEYBOARDLAYOUTTEST_FIT_RIGHT;
    const float badge = KEYBOARDLAYOUTTEST_FIT_BADGE;
    const float span = right - left;
    const float beside = right - badge - static_cast<float>(DIAGRAM_BADGE_GAP) - left;   // The legend's span by a badge
    if (!TestTrue(TEXT("the badge leaves the legend a span"), beside > 1.0f))
    {
        return true;
    }

    // Asserts one fit against its expected fields
    auto checkFit = [this](const FString& what, const FSOLKeyTextFit& fit, const float legendX, const float legendWidth,
        const bool bClipped, const bool bBadge)
    {
        TestEqual(what + TEXT(": legend x"), fit.LegendX, legendX, KEYBOARDLAYOUTTEST_TOLERANCE);
        TestEqual(what + TEXT(": legend width"), fit.LegendWidth, legendWidth, KEYBOARDLAYOUTTEST_TOLERANCE);
        TestTrue(what + TEXT(": clipped"), fit.bClipped == bClipped);
        TestTrue(what + TEXT(": badge"), fit.bBadge == bBadge);
    };

    // The badge fits: the legend centred in the span left of it
    const float small = beside * 0.5f;
    checkFit(TEXT("badge fits"), SOLKeyboardLayout::FitKeyText(left, right, small, badge),
        left + (beside - small) * 0.5f, small, false, true);

    // Edge-inclusive: exactly the span, and within the tolerance over it, still fit beside the badge
    checkFit(TEXT("legend exactly the badge span"), SOLKeyboardLayout::FitKeyText(left, right, beside, badge), left,
        beside, false, true);
    const float withinTolerance = beside + 0.5f * KINDA_SMALL_NUMBER;
    const FSOLKeyTextFit tolerant = SOLKeyboardLayout::FitKeyText(left, right, withinTolerance, badge);
    TestTrue(TEXT("within the tolerance: the badge shows"), tolerant.bBadge);
    TestFalse(TEXT("within the tolerance: not clipped"), tolerant.bClipped);

    // The badge would overlap: hidden, the legend centred in the whole span
    const float medium = beside + KEYBOARDLAYOUTTEST_FIT_OVER;
    checkFit(TEXT("badge would overlap"), SOLKeyboardLayout::FitKeyText(left, right, medium, badge),
        left + (span - medium) * 0.5f, medium, false, false);
    const float wider = (beside + span) * 0.5f;
    checkFit(TEXT("badge hidden, legend fits the span"), SOLKeyboardLayout::FitKeyText(left, right, wider, badge),
        left + (span - wider) * 0.5f, wider, false, false);

    // Wider than the whole span: clipped at the left edge, with or without a badge
    const float wide = span * 1.5f;
    checkFit(TEXT("clipped, badge"), SOLKeyboardLayout::FitKeyText(left, right, wide, badge), left, span, true, false);
    checkFit(TEXT("clipped, no badge"), SOLKeyboardLayout::FitKeyText(left, right, wide, 0.0f), left, span, true,
        false);
    checkFit(TEXT("exactly the span: not clipped"), SOLKeyboardLayout::FitKeyText(left, right, span, 0.0f), left, span,
        false, false);
    checkFit(TEXT("just over the span: clipped"), SOLKeyboardLayout::FitKeyText(left, right,
        span + KEYBOARDLAYOUTTEST_FIT_OVER, 0.0f), left, span, true, false);

    // No badge (zero or negative width): the legend centred in the whole span
    checkFit(TEXT("no badge"), SOLKeyboardLayout::FitKeyText(left, right, small, 0.0f), left + (span - small) * 0.5f,
        small, false, false);
    checkFit(TEXT("negative badge width"), SOLKeyboardLayout::FitKeyText(left, right, small, -badge),
        left + (span - small) * 0.5f, small, false, false);

    // No legend (E.8): at left, zero width, not clipped; the badge shows when it fits the whole span (the gap not
    // needed)
    checkFit(TEXT("no legend, badge"), SOLKeyboardLayout::FitKeyText(left, right, 0.0f, badge), left, 0.0f, false,
        true);
    const float badgeGap = static_cast<float>(DIAGRAM_BADGE_GAP);
    if (badgeGap > 0.0f)
    {
        const float tight = span - 0.5f * badgeGap;
        checkFit(TEXT("no legend, badge within the gap of the span"), SOLKeyboardLayout::FitKeyText(left, right, 0.0f,
            tight), left, 0.0f, false, true);
    }
    checkFit(TEXT("no legend, badge exactly the span"), SOLKeyboardLayout::FitKeyText(left, right, 0.0f, span), left,
        0.0f, false, true);
    checkFit(TEXT("no legend, badge wider than the span"), SOLKeyboardLayout::FitKeyText(left, right, 0.0f,
        span + KEYBOARDLAYOUTTEST_FIT_OVER), left, 0.0f, false, false);
    checkFit(TEXT("no legend, no badge"), SOLKeyboardLayout::FitKeyText(left, right, 0.0f, 0.0f), left, 0.0f, false,
        false);

    // An empty or negative span: at left, zero width, clipped when there is a legend, never a badge
    const float spanRights[] = { left, left - 5.0f };
    for (const float spanRight : spanRights)
    {
        const FString what = FString::Printf(TEXT("span %.1f"), spanRight - left);
        checkFit(what + TEXT(", legend and badge"), SOLKeyboardLayout::FitKeyText(left, spanRight, small, badge), left,
            0.0f, true, false);
        checkFit(what + TEXT(", legend"), SOLKeyboardLayout::FitKeyText(left, spanRight, small, 0.0f), left, 0.0f, true,
            false);
        checkFit(what + TEXT(", badge only"), SOLKeyboardLayout::FitKeyText(left, spanRight, 0.0f, badge), left, 0.0f,
            false, false);
        checkFit(what + TEXT(", nothing"), SOLKeyboardLayout::FitKeyText(left, spanRight, 0.0f, 0.0f), left, 0.0f,
            false, false);
    }
    return true;
}

#endif
