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
// mouse placement and the scale for a width.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float KEYBOARDLAYOUTTEST_TOLERANCE = 1.0e-3f;     // Key-unit and pixel comparisons
    constexpr float KEYBOARDLAYOUTTEST_GRID_STEP = 0.1f;        // Hit-test sample spacing (key units)
    constexpr float KEYBOARDLAYOUTTEST_GRID_OFFSET = 0.0333f;   // Keeps the samples off the key edges
    constexpr float KEYBOARDLAYOUTTEST_KEYBOARD_WIDTH = 23.0f;  // Main block, navigation, numpad and gaps (section 7)
    constexpr float KEYBOARDLAYOUTTEST_KEYBOARD_WIDTH_SLACK = 0.5f;
    constexpr float KEYBOARDLAYOUTTEST_KEYBOARD_HEIGHT = 6.5f;  // Function row, half a row gap, five rows
    constexpr int32 KEYBOARDLAYOUTTEST_MIN_KEYS = 100;          // About 104 keyboard keys plus the mouse zones
    constexpr int32 KEYBOARDLAYOUTTEST_MAX_ROW = 5;             // The Ctrl / Space row

    // The two mouse placements
    constexpr ESOLMousePlacement KEYBOARDLAYOUTTEST_PLACEMENTS[] = { ESOLMousePlacement::Beside,
        ESOLMousePlacement::Below };

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
    // Returns the scale the FitToWidth contract gives for a width, restated so the tests stay independent
    FSOLDiagramScale KeyboardLayoutTestExpectedScale(const float widthPx)
    {
        using namespace SOL::Help;
        FSOLDiagramScale scale;
        if (!FMath::IsFinite(widthPx) || widthPx <= 0.0f)
        {
            return scale;
        }
        const float besideUnit = widthPx / SOLKeyboardLayout::Extent(ESOLMousePlacement::Beside).X;
        if (besideUnit >= DIAGRAM_MOUSE_BELOW_UNIT)
        {
            scale.Placement = ESOLMousePlacement::Beside;
            scale.UnitPx = FMath::Min(besideUnit, DIAGRAM_MAX_UNIT);
        }
        else
        {
            scale.Placement = ESOLMousePlacement::Below;
            scale.UnitPx = FMath::Min(widthPx / SOLKeyboardLayout::Extent(ESOLMousePlacement::Below).X,
                DIAGRAM_MAX_UNIT);
        }
        scale.bShortLabels = scale.UnitPx >= DIAGRAM_MIN_UNIT;
        return scale;
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
// No two keys overlap, with the mouse beside or below the keyboard, and every key lies inside the extent
bool FSOLKeyboardLayoutOverlapTest::RunTest(const FString& /*parameters*/)
{
    const int32 count = SOLKeyboardLayout::KeyCount();
    TestTrue(TEXT("the table has keys"), count > 0);
    for (const ESOLMousePlacement placement : KEYBOARDLAYOUTTEST_PLACEMENTS)
    {
        const TCHAR* placementName = placement == ESOLMousePlacement::Beside ? TEXT("beside") : TEXT("below");
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
// KeyAt returns each key at its centre, none in a gap or outside the extent, and agrees with the rects everywhere
bool FSOLKeyboardLayoutKeyAtTest::RunTest(const FString& /*parameters*/)
{
    const int32 count = SOLKeyboardLayout::KeyCount();
    for (const ESOLMousePlacement placement : KEYBOARDLAYOUTTEST_PLACEMENTS)
    {
        const TCHAR* placementName = placement == ESOLMousePlacement::Beside ? TEXT("beside") : TEXT("below");
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
// The scale fits the width (mouse beside first, below when that unit is under the threshold), clamped at the maximum,
// hiding the short labels under the minimum; a bad width gives the default scale
bool FSOLKeyboardLayoutFitTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    const float widths[] = { 200.0f, 300.0f, 460.0f, 530.0f, 640.0f, 770.0f, 1200.0f, 5000.0f };
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

    // The two shell sizes of section 7: the full width beside at 1280x720, the mouse below at 1000x800
    TestTrue(TEXT("770 wide: mouse beside"), SOLKeyboardLayout::FitToWidth(770.0f).Placement
        == ESOLMousePlacement::Beside);
    TestTrue(TEXT("530 wide: mouse below"), SOLKeyboardLayout::FitToWidth(530.0f).Placement
        == ESOLMousePlacement::Below);
    TestTrue(TEXT("530 wide: labels shown"), SOLKeyboardLayout::FitToWidth(530.0f).bShortLabels);
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

#endif
