/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLBindingLabel.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Checks KeyDisplayName for one UE key name against its expected display text
    void BindingLabelExpect(FAutomationTestBase& test, const TCHAR* keyName, const TCHAR* expected)
    {
        test.TestEqual(*FString::Printf(TEXT("%s -> %s"), keyName, expected),
            SOLBindingLabel::KeyDisplayName(FString(keyName)), FString(expected));
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelMouseTest, "SOLTest.BindingLabel.Mouse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Mouse buttons and wheel directions get short display names
bool FSOLBindingLabelMouseTest::RunTest(const FString& /*parameters*/)
{
    BindingLabelExpect(*this, TEXT("LeftMouseButton"), TEXT("LMB"));
    BindingLabelExpect(*this, TEXT("RightMouseButton"), TEXT("RMB"));
    BindingLabelExpect(*this, TEXT("MiddleMouseButton"), TEXT("MMB"));
    BindingLabelExpect(*this, TEXT("MouseScrollUp"), TEXT("Wheel Up"));
    BindingLabelExpect(*this, TEXT("MouseScrollDown"), TEXT("Wheel Down"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelModifiersTest, "SOLTest.BindingLabel.Modifiers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Modifier and special keys get short display names
bool FSOLBindingLabelModifiersTest::RunTest(const FString& /*parameters*/)
{
    BindingLabelExpect(*this, TEXT("SpaceBar"), TEXT("Space"));
    BindingLabelExpect(*this, TEXT("LeftShift"), TEXT("Shift"));
    BindingLabelExpect(*this, TEXT("LeftControl"), TEXT("Ctrl"));
    BindingLabelExpect(*this, TEXT("LeftAlt"), TEXT("Alt"));
    BindingLabelExpect(*this, TEXT("Escape"), TEXT("Esc"));
    BindingLabelExpect(*this, TEXT("Home"), TEXT("Home"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelDigitsTest, "SOLTest.BindingLabel.Digits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The digit keys "Zero".."Nine" display as "0".."9"
bool FSOLBindingLabelDigitsTest::RunTest(const FString& /*parameters*/)
{
    const TCHAR* names[] = { TEXT("Zero"), TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Five"),
        TEXT("Six"), TEXT("Seven"), TEXT("Eight"), TEXT("Nine") };
    const TCHAR* digits[] = { TEXT("0"), TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"), TEXT("7"),
        TEXT("8"), TEXT("9") };
    for (int32 index = 0; index < UE_ARRAY_COUNT(names); ++index)
    {
        BindingLabelExpect(*this, names[index], digits[index]);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelSymbolsTest, "SOLTest.BindingLabel.Symbols",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Symbol keys display as their symbol
bool FSOLBindingLabelSymbolsTest::RunTest(const FString& /*parameters*/)
{
    BindingLabelExpect(*this, TEXT("Add"), TEXT("+"));
    BindingLabelExpect(*this, TEXT("Subtract"), TEXT("-"));
    BindingLabelExpect(*this, TEXT("Equals"), TEXT("="));
    BindingLabelExpect(*this, TEXT("LeftBracket"), TEXT("["));
    BindingLabelExpect(*this, TEXT("RightBracket"), TEXT("]"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelPassThroughTest, "SOLTest.BindingLabel.PassThrough",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Single letters and unknown key names are returned unchanged; an empty name is "Unbound"
bool FSOLBindingLabelPassThroughTest::RunTest(const FString& /*parameters*/)
{
    BindingLabelExpect(*this, TEXT("W"), TEXT("W"));
    BindingLabelExpect(*this, TEXT("M"), TEXT("M"));
    BindingLabelExpect(*this, TEXT("Z"), TEXT("Z"));
    BindingLabelExpect(*this, TEXT("PageUp"), TEXT("PageUp"));
    BindingLabelExpect(*this, TEXT("F13"), TEXT("F13"));
    BindingLabelExpect(*this, TEXT("Gamepad_FaceButton_Bottom"), TEXT("Gamepad_FaceButton_Bottom"));
    TestEqual(TEXT("Empty -> Unbound"), SOLBindingLabel::KeyDisplayName(FString()), FString(TEXT("Unbound")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelIdempotentTest, "SOLTest.BindingLabel.Idempotent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Feeding a display name back in returns it unchanged (it is not a mapped key name), and results are repeatable
bool FSOLBindingLabelIdempotentTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLBindingLabel;
    TestEqual(TEXT("LMB stays LMB"), KeyDisplayName(TEXT("LMB")), FString(TEXT("LMB")));
    TestEqual(TEXT("Shift stays Shift"), KeyDisplayName(TEXT("Shift")), FString(TEXT("Shift")));
    TestEqual(TEXT("Space stays Space"), KeyDisplayName(TEXT("Space")), FString(TEXT("Space")));
    TestEqual(TEXT("Digit 5 stays 5"), KeyDisplayName(TEXT("5")), FString(TEXT("5")));
    TestEqual(TEXT("Repeatable"), KeyDisplayName(TEXT("LeftShift")), KeyDisplayName(TEXT("LeftShift")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelChordTest, "SOLTest.BindingLabel.Chord",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Chords join each key's display name with "+", in order
bool FSOLBindingLabelChordTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLBindingLabel;
    TestEqual(TEXT("Shift+W"), JoinChord({ TEXT("LeftShift"), TEXT("W") }), FString(TEXT("Shift+W")));
    TestEqual(TEXT("Single key has no separator"), JoinChord({ TEXT("SpaceBar") }), FString(TEXT("Space")));
    TestEqual(TEXT("Three keys"), JoinChord({ TEXT("LeftControl"), TEXT("LeftAlt"), TEXT("Nine") }),
        FString(TEXT("Ctrl+Alt+9")));
    TestEqual(TEXT("Order preserved"), JoinChord({ TEXT("W"), TEXT("LeftShift") }), FString(TEXT("W+Shift")));
    TestEqual(TEXT("Mouse and symbol"), JoinChord({ TEXT("RightMouseButton"), TEXT("Add") }), FString(TEXT("RMB++")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelChordEdgeTest, "SOLTest.BindingLabel.ChordEdge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An empty chord is "Unbound"; duplicate keys are kept in order
bool FSOLBindingLabelChordEdgeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLBindingLabel;
    TestEqual(TEXT("Empty chord"), JoinChord(TArray<FString>()), FString(TEXT("Unbound")));
    TestEqual(TEXT("Duplicates kept"), JoinChord({ TEXT("W"), TEXT("W") }), FString(TEXT("W+W")));
    TestEqual(TEXT("Duplicate modifier kept"), JoinChord({ TEXT("LeftShift"), TEXT("A"), TEXT("LeftShift") }),
        FString(TEXT("Shift+A+Shift")));

    const TArray<FString> chord = { TEXT("LeftAlt"), TEXT("Home") };
    TestEqual(TEXT("Deterministic"), JoinChord(chord), JoinChord(chord));
    TestEqual(TEXT("Input array not modified"), chord.Num(), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLBindingLabelRebindableTest, "SOLTest.BindingLabel.Rebindable",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Escape is reserved and an empty key cannot be rebound; every other key can
bool FSOLBindingLabelRebindableTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLBindingLabel;
    TestFalse(TEXT("Escape is reserved"), IsRebindable(TEXT("Escape")));
    TestFalse(TEXT("Empty is not rebindable"), IsRebindable(FString()));
    TestTrue(TEXT("Letter"), IsRebindable(TEXT("W")));
    TestTrue(TEXT("Mouse button"), IsRebindable(TEXT("LeftMouseButton")));
    TestTrue(TEXT("Modifier"), IsRebindable(TEXT("LeftShift")));
    TestTrue(TEXT("Digit"), IsRebindable(TEXT("Zero")));
    TestTrue(TEXT("Map key"), IsRebindable(TEXT("M")));
    TestTrue(TEXT("Unknown key"), IsRebindable(TEXT("PageUp")));
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
