/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyCapPaint.h"
#include "Menu/SOLKeyCaps.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <initializer_list>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a key caps (Docs/SDDs/33-player-documentation.md sections 6 and 10), written against
// the header contract (Menu/SOLKeyCaps.h) before the production code existed: parsing a Controls bindings cell or one
// binding into chords of caps, the cap shapes and widths, the text form (one source with SOLMenuContent::
// FormatBindings) and the wrapping row layout.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float KEYCAPSTEST_TOLERANCE = 1.0e-3f;        // Layout comparisons (menu units)
    constexpr float KEYCAPSTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float KEYCAPSTEST_PLUS_WIDTH = 10.0f;         // A measured "+" for the layout tests
    constexpr float KEYCAPSTEST_SEPARATOR_WIDTH = 12.0f;    // A measured " / " for the layout tests
    constexpr float KEYCAPSTEST_CHAR_WIDTH = 8.0f;          // A stand-in legend width per character
    constexpr int32 KEYCAPSTEST_FIRE = static_cast<int32>(SOLKeyBindings::EBinding::Fire);
    constexpr int32 KEYCAPSTEST_OPEN_MAP = static_cast<int32>(SOLKeyBindings::EBinding::OpenMap);
    constexpr int32 KEYCAPSTEST_CYCLE = static_cast<int32>(SOLKeyBindings::EBinding::CycleOverlayProfile);

    //////////////////////////////////////////////////////////////////////////
    // Returns the metrics the layout tests use (measured pieces stand-ins, the constants' gaps and height)
    FSOLKeyCapMetrics KeyCapsTestMetrics()
    {
        FSOLKeyCapMetrics metrics;
        metrics.PlusWidth = KEYCAPSTEST_PLUS_WIDTH;
        metrics.SeparatorWidth = KEYCAPSTEST_SEPARATOR_WIDTH;
        return metrics;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a hand-built model with one chord per entry, each of that many plain caps (independent of ParseCell)
    FSOLKeyCapModel KeyCapsTestModel(const std::initializer_list<int32> chordCapCounts)
    {
        FSOLKeyCapModel model;
        for (const int32 capCount : chordCapCounts)
        {
            FSOLKeyCapChord chord;
            chord.FirstCap = model.Caps.Num();
            chord.CapCount = capCount;
            for (int32 cap = 0; cap < capCount; ++cap)
            {
                FSOLKeyCap keyCap;
                keyCap.KeyName = TEXT("K");
                keyCap.Label = TEXT("K");
                model.Caps.Add(keyCap);
            }
            model.Chords.Add(chord);
        }
        return model;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the number of '|' alternatives of a bindings cell
    int32 KeyCapsTestAlternativeCount(const TCHAR* cell)
    {
        TArray<FString> alternatives;
        FString(cell).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
        return alternatives.Num();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the chord of a model a cap belongs to, or INDEX_NONE
    int32 KeyCapsTestChordOfCap(const FSOLKeyCapModel& model, const int32 cap)
    {
        for (int32 chord = 0; chord < model.Chords.Num(); ++chord)
        {
            const FSOLKeyCapChord& entry = model.Chords[chord];
            if (cap >= entry.FirstCap && cap < entry.FirstCap + entry.CapCount)
            {
                return chord;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the layout item of a cap, or nullptr
    const FSOLKeyCapLayoutItem* KeyCapsTestItemOfCap(const FSOLKeyCapLayout& layout, const int32 cap)
    {
        for (const FSOLKeyCapLayoutItem& item : layout.Items)
        {
            if (item.Kind == ESOLKeyCapItem::Cap && item.Cap == cap)
            {
                return &item;
            }
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a model's chords are a valid partition of its caps, each chord with at least one cap
    void KeyCapsTestCheckChords(FAutomationTestBase& test, const FSOLKeyCapModel& model, const FString& what)
    {
        int32 nextCap = 0;
        for (int32 chord = 0; chord < model.Chords.Num(); ++chord)
        {
            const FSOLKeyCapChord& entry = model.Chords[chord];
            test.TestTrue(FString::Printf(TEXT("%s: chord %d has a cap"), *what, chord), entry.CapCount >= 1);
            test.TestEqual(FString::Printf(TEXT("%s: chord %d follows the last"), *what, chord), entry.FirstCap,
                nextCap);
            nextCap = entry.FirstCap + entry.CapCount;
        }
        test.TestEqual(what + TEXT(": the chords hold every cap"), nextCap, model.Caps.Num());
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts the invariants of any laid-out row: lines, heights, item rows, line starts and the widest line
    void KeyCapsTestCheckLayout(FAutomationTestBase& test, const FSOLKeyCapModel& model, const FSOLKeyCapLayout& layout,
        const FSOLKeyCapMetrics& metrics, const float returned, const FString& what)
    {
        int32 maxLine = -1;
        TArray<float> lineWidths;
        TArray<bool> lineStarted;
        for (int32 index = 0; index < layout.Items.Num(); ++index)
        {
            const FSOLKeyCapLayoutItem& item = layout.Items[index];
            test.TestTrue(FString::Printf(TEXT("%s: item %d has a line"), *what, index), item.Line >= 0);
            if (item.Line < 0)
            {
                continue;
            }
            // Grow the per-line bookkeeping to this line
            while (lineWidths.Num() <= item.Line)
            {
                lineWidths.Add(0.0f);
                lineStarted.Add(false);
            }
            if (!lineStarted[item.Line])
            {
                test.TestTrue(FString::Printf(TEXT("%s: line %d does not start with a separator"), *what, item.Line),
                    item.Kind != ESOLKeyCapItem::Separator);
                lineStarted[item.Line] = true;
            }
            maxLine = FMath::Max(maxLine, item.Line);
            lineWidths[item.Line] = FMath::Max(lineWidths[item.Line], item.X + item.Width);
            test.TestEqual(FString::Printf(TEXT("%s: item %d top is its line's"), *what, index), item.Y,
                item.Line * (metrics.CapHeight + metrics.RowGap), KEYCAPSTEST_TOLERANCE);
            test.TestTrue(FString::Printf(TEXT("%s: item %d starts inside the row"), *what, index),
                item.X >= -KEYCAPSTEST_TOLERANCE);
            if (item.Kind == ESOLKeyCapItem::Cap)
            {
                test.TestTrue(FString::Printf(TEXT("%s: item %d names a model cap"), *what, index),
                    model.Caps.IsValidIndex(item.Cap));
            }
            else if (item.Kind == ESOLKeyCapItem::Plus)
            {
                test.TestEqual(FString::Printf(TEXT("%s: item %d is a measured plus"), *what, index), item.Width,
                    metrics.PlusWidth, KEYCAPSTEST_TOLERANCE);
            }
            else
            {
                test.TestEqual(FString::Printf(TEXT("%s: item %d is a measured separator"), *what, index),
                    item.Width, metrics.SeparatorWidth, KEYCAPSTEST_TOLERANCE);
            }
        }
        test.TestEqual(what + TEXT(": line count"), layout.LineCount, maxLine + 1);
        const float expectedHeight = layout.LineCount > 0
            ? layout.LineCount * metrics.CapHeight + (layout.LineCount - 1) * metrics.RowGap
            : 0.0f;
        test.TestEqual(what + TEXT(": height from its lines"), layout.Height, expectedHeight, KEYCAPSTEST_TOLERANCE);
        test.TestEqual(what + TEXT(": returns its height"), returned, layout.Height, KEYCAPSTEST_TOLERANCE);
        float widest = 0.0f;
        for (const float lineWidth : lineWidths)
        {
            widest = FMath::Max(widest, lineWidth);
        }
        test.TestEqual(what + TEXT(": width is the widest line"), layout.Width, widest, KEYCAPSTEST_TOLERANCE);

        // Every cap of the model is laid out exactly once
        for (int32 cap = 0; cap < model.Caps.Num(); ++cap)
        {
            int32 seen = 0;
            for (const FSOLKeyCapLayoutItem& item : layout.Items)
            {
                seen += (item.Kind == ESOLKeyCapItem::Cap && item.Cap == cap) ? 1 : 0;
            }
            test.TestEqual(FString::Printf(TEXT("%s: cap %d laid out once"), *what, cap), seen, 1);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsControlRowsTest, "SOLTest.KeyCaps.ControlRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every Controls cell parses at the default keys into one chord per alternative, none empty and none unbound
bool FSOLKeyCapsControlRowsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyCapModel model;
    for (int32 row = 0; row < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)); ++row)
    {
        const TCHAR* cell = SOLMenuText::CONTROL_ROWS[row].Bindings;
        const FString what = FString::Printf(TEXT("row %d '%s'"), row, cell);
        SOLKeyCaps::ParseCell(cell, keys, model);
        TestEqual(what + TEXT(": one chord per alternative"), model.Chords.Num(), KeyCapsTestAlternativeCount(cell));
        KeyCapsTestCheckChords(*this, model, what);
        for (int32 cap = 0; cap < model.Caps.Num(); ++cap)
        {
            const FSOLKeyCap& keyCap = model.Caps[cap];
            TestFalse(FString::Printf(TEXT("%s: cap %d has a legend"), *what, cap), keyCap.Label.IsEmpty());
            TestFalse(FString::Printf(TEXT("%s: cap %d is bound at the defaults"), *what, cap), keyCap.bUnbound);
            TestFalse(FString::Printf(TEXT("%s: cap %d has a key"), *what, cap), keyCap.KeyName.IsEmpty());
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsOneSourceTest, "SOLTest.KeyCaps.TextMatchesFormatBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The caps' text form equals FormatBindings for every Controls cell, at the defaults, after a rebind and with an
// unbound action (text and caps come from one parse)
bool FSOLKeyCapsOneSourceTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> rebound = SOLKeyBindings::DefaultKeys();
    rebound[KEYCAPSTEST_FIRE] = TEXT("K");
    TArray<FString> unbound = SOLKeyBindings::DefaultKeys();
    unbound[KEYCAPSTEST_OPEN_MAP] = FString();
    const TArray<FString> keySets[] = { SOLKeyBindings::DefaultKeys(), rebound, unbound };
    const TCHAR* keySetNames[] = { TEXT("defaults"), TEXT("fire on K"), TEXT("map unbound") };

    FSOLKeyCapModel model;
    for (int32 set = 0; set < static_cast<int32>(UE_ARRAY_COUNT(keySets)); ++set)
    {
        for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
        {
            SOLKeyCaps::ParseCell(row.Bindings, keySets[set], model);
            TestEqual(FString::Printf(TEXT("%s: '%s' text"), keySetNames[set], row.Bindings),
                SOLKeyCaps::ModelText(model), SOLMenuContent::FormatBindings(row.Bindings, keySets[set]));
        }
    }

    // A few cells outside the table
    const TCHAR* cells[] = { TEXT("@Fire|LeftShift+RightMouseButton"), TEXT("@Nope"), TEXT("@OpenMap"),
        TEXT("@ThrustForward|@ThrustBackward") };
    for (const TCHAR* cell : cells)
    {
        SOLKeyCaps::ParseCell(cell, rebound, model);
        TestEqual(FString::Printf(TEXT("'%s' text"), cell), SOLKeyCaps::ModelText(model),
            SOLMenuContent::FormatBindings(cell, rebound));
    }
    SOLKeyCaps::ParseCell(TEXT("@ThrustForward|@ThrustBackward"), rebound, model);
    TestEqual(TEXT("alternatives joined by the separator"), SOLKeyCaps::ModelText(model), FString(TEXT("W / S")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsBindingIdsTest, "SOLTest.KeyCaps.BindingIds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An '@' id resolves to its binding's live key; an unknown id, an empty key or a short key array is one unbound cap
bool FSOLKeyCapsBindingIdsTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString unboundLabel = SOLBindingLabel::KeyDisplayName(FString());
    FSOLKeyCapModel model;

    // A known id
    SOLKeyCaps::ParseCell(TEXT("@Fire"), keys, model);
    TestEqual(TEXT("@Fire: one chord"), model.Chords.Num(), 1);
    TestEqual(TEXT("@Fire: one cap"), model.Caps.Num(), 1);
    if (model.Chords.Num() == 1 && model.Caps.Num() == 1)
    {
        TestEqual(TEXT("@Fire: chord names the binding"), model.Chords[0].Binding, KEYCAPSTEST_FIRE);
        TestEqual(TEXT("@Fire: cap is the live key"), model.Caps[0].KeyName, FString(TEXT("LeftMouseButton")));
        TestEqual(TEXT("@Fire: legend"), model.Caps[0].Label, SOLBindingLabel::KeyDisplayName(TEXT("LeftMouseButton")));
        TestFalse(TEXT("@Fire: bound"), model.Caps[0].bUnbound);
    }

    // The live key follows a rebind
    keys[KEYCAPSTEST_FIRE] = TEXT("K");
    SOLKeyCaps::ParseCell(TEXT("@Fire"), keys, model);
    TestTrue(TEXT("@Fire rebound: one cap"), model.Caps.Num() == 1);
    if (model.Caps.Num() == 1)
    {
        TestEqual(TEXT("@Fire rebound: cap is K"), model.Caps[0].KeyName, FString(TEXT("K")));
    }

    // An unknown id
    SOLKeyCaps::ParseCell(TEXT("@NoSuchAction"), keys, model);
    TestEqual(TEXT("unknown id: one chord"), model.Chords.Num(), 1);
    TestEqual(TEXT("unknown id: one cap"), model.Caps.Num(), 1);
    if (model.Chords.Num() == 1 && model.Caps.Num() == 1)
    {
        TestEqual(TEXT("unknown id: no binding"), model.Chords[0].Binding, static_cast<int32>(INDEX_NONE));
        TestTrue(TEXT("unknown id: unbound cap"), model.Caps[0].bUnbound);
        TestEqual(TEXT("unknown id: Unbound legend"), model.Caps[0].Label, unboundLabel);
    }

    // An empty key
    keys[KEYCAPSTEST_FIRE] = FString();
    SOLKeyCaps::ParseCell(TEXT("@Fire"), keys, model);
    TestEqual(TEXT("empty key: one cap"), model.Caps.Num(), 1);
    if (model.Chords.Num() == 1 && model.Caps.Num() == 1)
    {
        TestEqual(TEXT("empty key: still names the binding"), model.Chords[0].Binding, KEYCAPSTEST_FIRE);
        TestTrue(TEXT("empty key: unbound cap"), model.Caps[0].bUnbound);
        TestEqual(TEXT("empty key: Unbound legend"), model.Caps[0].Label, unboundLabel);
        TestTrue(TEXT("empty key: no engine key"), model.Caps[0].KeyName.IsEmpty());
    }

    // A short key array
    SOLKeyCaps::ParseCell(TEXT("@Fire"), TArray<FString>(), model);
    TestEqual(TEXT("short array: one cap"), model.Caps.Num(), 1);
    if (model.Caps.Num() == 1)
    {
        TestTrue(TEXT("short array: unbound cap"), model.Caps[0].bUnbound);
    }

    // A null and an empty cell
    SOLKeyCaps::ParseCell(nullptr, keys, model);
    TestEqual(TEXT("null cell: one cap"), model.Caps.Num(), 1);
    if (model.Caps.Num() == 1)
    {
        TestTrue(TEXT("null cell: unbound cap"), model.Caps[0].bUnbound);
    }
    SOLKeyCaps::ParseCell(TEXT(""), keys, model);
    TestEqual(TEXT("empty cell: one cap"), model.Caps.Num(), 1);
    if (model.Caps.Num() == 1)
    {
        TestTrue(TEXT("empty cell: unbound cap"), model.Caps[0].bUnbound);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsChordsTest, "SOLTest.KeyCaps.ChordsAndAlternatives",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Chords split on '+', alternatives on '|'; fixed chords name no binding
bool FSOLKeyCapsChordsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyCapModel model;

    SOLKeyCaps::ParseCell(TEXT("J|M|P"), keys, model);
    TestEqual(TEXT("J|M|P: three chords"), model.Chords.Num(), 3);
    TestEqual(TEXT("J|M|P: three caps"), model.Caps.Num(), 3);
    KeyCapsTestCheckChords(*this, model, TEXT("J|M|P"));
    const TCHAR* jmp[] = { TEXT("J"), TEXT("M"), TEXT("P") };
    for (int32 cap = 0; cap < FMath::Min(model.Caps.Num(), 3); ++cap)
    {
        TestEqual(FString::Printf(TEXT("J|M|P: cap %d"), cap), model.Caps[cap].KeyName, FString(jmp[cap]));
    }
    for (int32 chord = 0; chord < model.Chords.Num(); ++chord)
    {
        TestEqual(FString::Printf(TEXT("J|M|P: chord %d is fixed"), chord), model.Chords[chord].Binding,
            static_cast<int32>(INDEX_NONE));
    }

    SOLKeyCaps::ParseCell(TEXT("LeftShift+RightMouseButton"), keys, model);
    TestEqual(TEXT("Shift+RMB: one chord"), model.Chords.Num(), 1);
    TestEqual(TEXT("Shift+RMB: two caps"), model.Caps.Num(), 2);
    if (model.Chords.Num() == 1 && model.Caps.Num() == 2)
    {
        TestEqual(TEXT("Shift+RMB: chord holds both caps"), model.Chords[0].CapCount, 2);
        TestEqual(TEXT("Shift+RMB: modifier first"), model.Caps[0].KeyName, FString(TEXT("LeftShift")));
        TestEqual(TEXT("Shift+RMB: button second"), model.Caps[1].KeyName, FString(TEXT("RightMouseButton")));
    }

    SOLKeyCaps::ParseCell(TEXT("MiddleMouseButton|LeftShift+RightMouseButton"), keys, model);
    TestEqual(TEXT("MMB|Shift+RMB: two chords"), model.Chords.Num(), 2);
    TestEqual(TEXT("MMB|Shift+RMB: three caps"), model.Caps.Num(), 3);
    KeyCapsTestCheckChords(*this, model, TEXT("MMB|Shift+RMB"));
    if (model.Chords.Num() == 2)
    {
        TestEqual(TEXT("MMB|Shift+RMB: first chord one cap"), model.Chords[0].CapCount, 1);
        TestEqual(TEXT("MMB|Shift+RMB: second chord two caps"), model.Chords[1].CapCount, 2);
    }

    SOLKeyCaps::ParseCell(TEXT("@OpenMap"), keys, model);
    TestEqual(TEXT("@OpenMap: one chord"), model.Chords.Num(), 1);
    TestEqual(TEXT("@OpenMap: Shift and M"), model.Caps.Num(), 2);
    if (model.Chords.Num() == 1 && model.Caps.Num() == 2)
    {
        TestEqual(TEXT("@OpenMap: names the binding"), model.Chords[0].Binding, KEYCAPSTEST_OPEN_MAP);
        TestEqual(TEXT("@OpenMap: modifier legend"), model.Caps[0].Label, FString(TEXT("Shift")));
        TestEqual(TEXT("@OpenMap: key second"), model.Caps[1].KeyName, FString(TEXT("M")));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsMouseTest, "SOLTest.KeyCaps.MouseCaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Mouse buttons and axes give Mouse caps lighting the right part; keyboard keys light none
bool FSOLKeyCapsMouseTest::RunTest(const FString& /*parameters*/)
{
    struct FKeyCapsTestMouseCase
    {
        const TCHAR* KeyName;
        ESOLMouseButton Button;
    };
    const FKeyCapsTestMouseCase cases[] = {
        { TEXT("LeftMouseButton"), ESOLMouseButton::Left },
        { TEXT("RightMouseButton"), ESOLMouseButton::Right },
        { TEXT("MiddleMouseButton"), ESOLMouseButton::Middle },
        { TEXT("MouseWheelAxis"), ESOLMouseButton::Wheel },
        { TEXT("Mouse2D"), ESOLMouseButton::Move },
        { TEXT("MouseX"), ESOLMouseButton::Move },
        { TEXT("MouseY"), ESOLMouseButton::Move },
        { TEXT("leftmousebutton"), ESOLMouseButton::Left },
    };
    for (const FKeyCapsTestMouseCase& entry : cases)
    {
        TestTrue(FString::Printf(TEXT("%s is a Mouse cap"), entry.KeyName),
            SOLKeyCaps::WidthClass(entry.KeyName) == ESOLKeyCapShape::Mouse);
        TestTrue(FString::Printf(TEXT("%s lights its part"), entry.KeyName),
            SOLKeyCaps::MouseButtonOf(entry.KeyName) == entry.Button);
    }
    const TCHAR* keyboard[] = { TEXT("K"), TEXT("SpaceBar"), TEXT("F1"), TEXT("") };
    for (const TCHAR* keyName : keyboard)
    {
        TestTrue(FString::Printf(TEXT("'%s' lights no mouse part"), keyName),
            SOLKeyCaps::MouseButtonOf(keyName) == ESOLMouseButton::None);
        TestTrue(FString::Printf(TEXT("'%s' is not a Mouse cap"), keyName),
            SOLKeyCaps::WidthClass(keyName) != ESOLKeyCapShape::Mouse);
    }

    // Parsed caps carry the shape and the part
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyCapModel model;
    const FKeyCapsTestMouseCase cells[] = {
        { TEXT("Mouse2D"), ESOLMouseButton::Move },
        { TEXT("MouseWheelAxis"), ESOLMouseButton::Wheel },
        { TEXT("LeftMouseButton"), ESOLMouseButton::Left },
        { TEXT("@Fire"), ESOLMouseButton::Left },
    };
    for (const FKeyCapsTestMouseCase& entry : cells)
    {
        SOLKeyCaps::ParseCell(entry.KeyName, keys, model);
        TestEqual(FString::Printf(TEXT("'%s': one cap"), entry.KeyName), model.Caps.Num(), 1);
        if (model.Caps.Num() == 1)
        {
            TestTrue(FString::Printf(TEXT("'%s': Mouse shape"), entry.KeyName),
                model.Caps[0].Shape == ESOLKeyCapShape::Mouse);
            TestTrue(FString::Printf(TEXT("'%s': its part"), entry.KeyName), model.Caps[0].Mouse == entry.Button);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsWideTest, "SOLTest.KeyCaps.WideCaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Space, Backspace, Tab, Enter, Shift, Ctrl, Alt and Caps are Wide (Space the widest); other keys are Normal
bool FSOLKeyCapsWideTest::RunTest(const FString& /*parameters*/)
{
    const TCHAR* wide[] = { TEXT("SpaceBar"), TEXT("BackSpace"), TEXT("Tab"), TEXT("Enter"), TEXT("LeftShift"),
        TEXT("RightShift"), TEXT("LeftControl"), TEXT("RightControl"), TEXT("LeftAlt"), TEXT("RightAlt"),
        TEXT("CapsLock"), TEXT("spacebar"), TEXT("TAB") };
    for (const TCHAR* keyName : wide)
    {
        TestTrue(FString::Printf(TEXT("%s is Wide"), keyName),
            SOLKeyCaps::WidthClass(keyName) == ESOLKeyCapShape::Wide);
    }
    TestEqual(TEXT("Space width"), SOLKeyCaps::WidthUnits(TEXT("SpaceBar")), SOL::Help::KEYCAP_SPACE_UNITS);
    TestEqual(TEXT("Backspace width"), SOLKeyCaps::WidthUnits(TEXT("BackSpace")), SOL::Help::KEYCAP_WIDE_UNITS);
    TestEqual(TEXT("Shift width"), SOLKeyCaps::WidthUnits(TEXT("LeftShift")), SOL::Help::KEYCAP_WIDE_UNITS);
    TestEqual(TEXT("Ctrl width"), SOLKeyCaps::WidthUnits(TEXT("RightControl")), SOL::Help::KEYCAP_WIDE_UNITS);

    const TCHAR* normal[] = { TEXT("K"), TEXT("F1"), TEXT("Hyphen"), TEXT("NumPadOne"), TEXT("Home"), TEXT("") };
    for (const TCHAR* keyName : normal)
    {
        TestTrue(FString::Printf(TEXT("'%s' is Normal"), keyName),
            SOLKeyCaps::WidthClass(keyName) == ESOLKeyCapShape::Normal);
        TestEqual(FString::Printf(TEXT("'%s' is one unit"), keyName), SOLKeyCaps::WidthUnits(keyName), 1.0f);
    }
    TestEqual(TEXT("a mouse cap is one unit"), SOLKeyCaps::WidthUnits(TEXT("LeftMouseButton")), 1.0f);

    // Parsed caps carry the shape and the width
    FSOLKeyCapModel model;
    SOLKeyCaps::ParseCell(TEXT("@SteerHold"), SOLKeyBindings::DefaultKeys(), model);
    TestEqual(TEXT("@SteerHold: one cap"), model.Caps.Num(), 1);
    if (model.Caps.Num() == 1)
    {
        TestTrue(TEXT("@SteerHold: Space is Wide"), model.Caps[0].Shape == ESOLKeyCapShape::Wide);
        TestEqual(TEXT("@SteerHold: Space width"), model.Caps[0].WidthUnits, SOL::Help::KEYCAP_SPACE_UNITS);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsParseBindingTest, "SOLTest.KeyCaps.ParseBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// One binding parses to its modifier cap then its key cap, its text BindingDisplayName; bad input is one unbound cap
bool FSOLKeyCapsParseBindingTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TArray<FString> keys = DefaultKeys();
    FSOLKeyCapModel model;
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        const FString what = FString::Printf(TEXT("binding %s"), GetDef(binding).Id);
        SOLKeyCaps::ParseBinding(binding, keys, model);
        const int32 expectedCaps = GetChord(binding) == EKeyChord::None ? 1 : 2;
        TestEqual(what + TEXT(": caps"), model.Caps.Num(), expectedCaps);
        TestEqual(what + TEXT(": one chord"), model.Chords.Num(), 1);
        if (model.Chords.Num() == 1)
        {
            TestEqual(what + TEXT(": chord names it"), model.Chords[0].Binding, binding);
        }
        if (model.Caps.Num() == expectedCaps)
        {
            TestEqual(what + TEXT(": key cap last"), model.Caps.Last().KeyName, keys[binding]);
        }
        TestEqual(what + TEXT(": text"), SOLKeyCaps::ModelText(model), BindingDisplayName(binding, keys));
    }

    // Shift+M and RShift+O
    SOLKeyCaps::ParseBinding(KEYCAPSTEST_OPEN_MAP, keys, model);
    TestEqual(TEXT("Shift+M text"), SOLKeyCaps::ModelText(model), FString(TEXT("Shift+M")));
    SOLKeyCaps::ParseBinding(KEYCAPSTEST_CYCLE, keys, model);
    TestEqual(TEXT("RShift+O text"), SOLKeyCaps::ModelText(model), FString(TEXT("RShift+O")));
    if (model.Caps.Num() == 2)
    {
        TestEqual(TEXT("RShift+O modifier legend"), model.Caps[0].Label, FString(TEXT("RShift")));
    }

    // An empty key: one unbound cap, no modifier cap
    keys[KEYCAPSTEST_OPEN_MAP] = FString();
    SOLKeyCaps::ParseBinding(KEYCAPSTEST_OPEN_MAP, keys, model);
    TestEqual(TEXT("empty key: one cap"), model.Caps.Num(), 1);
    if (model.Caps.Num() == 1)
    {
        TestTrue(TEXT("empty key: unbound"), model.Caps[0].bUnbound);
    }

    // A short key array and out-of-range bindings
    const int32 badBindings[] = { -1, BINDING_COUNT, BINDING_COUNT + 7 };
    for (const int32 binding : badBindings)
    {
        SOLKeyCaps::ParseBinding(binding, keys, model);
        TestEqual(FString::Printf(TEXT("binding %d: one cap"), binding), model.Caps.Num(), 1);
        if (model.Caps.Num() == 1)
        {
            TestTrue(FString::Printf(TEXT("binding %d: unbound"), binding), model.Caps[0].bUnbound);
        }
    }
    SOLKeyCaps::ParseBinding(KEYCAPSTEST_FIRE, TArray<FString>(), model);
    TestEqual(TEXT("short array: one cap"), model.Caps.Num(), 1);
    if (model.Caps.Num() == 1)
    {
        TestTrue(TEXT("short array: unbound"), model.Caps[0].bUnbound);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsCapWidthTest, "SOLTest.KeyCaps.CapWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A cap is its legend plus the padding a side, at least the minimum width times its key units
bool FSOLKeyCapsCapWidthTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    FSOLKeyCap normal;
    normal.WidthUnits = 1.0f;
    TestEqual(TEXT("short legend: the minimum"), SOLKeyCaps::CapWidth(normal, 4.0f), KEYCAP_MIN_WIDTH,
        KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("long legend: legend and padding"), SOLKeyCaps::CapWidth(normal, 40.0f),
        40.0f + 2.0f * KEYCAP_PADDING, KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("negative legend reads 0"), SOLKeyCaps::CapWidth(normal, -50.0f), KEYCAP_MIN_WIDTH,
        KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("NaN legend reads 0"), SOLKeyCaps::CapWidth(normal, KEYCAPSTEST_NAN), KEYCAP_MIN_WIDTH,
        KEYCAPSTEST_TOLERANCE);

    FSOLKeyCap wide;
    wide.Shape = ESOLKeyCapShape::Wide;
    wide.WidthUnits = KEYCAP_WIDE_UNITS;
    TestEqual(TEXT("wide cap minimum"), SOLKeyCaps::CapWidth(wide, 4.0f), KEYCAP_MIN_WIDTH * KEYCAP_WIDE_UNITS,
        KEYCAPSTEST_TOLERANCE);
    FSOLKeyCap space;
    space.Shape = ESOLKeyCapShape::Wide;
    space.WidthUnits = KEYCAP_SPACE_UNITS;
    TestEqual(TEXT("space cap minimum"), SOLKeyCaps::CapWidth(space, 40.0f), KEYCAP_MIN_WIDTH * KEYCAP_SPACE_UNITS,
        KEYCAPSTEST_TOLERANCE);
    const float longLegend = KEYCAP_MIN_WIDTH * KEYCAP_SPACE_UNITS + 10.0f;
    TestEqual(TEXT("space cap with a longer legend"), SOLKeyCaps::CapWidth(space, longLegend),
        longLegend + 2.0f * KEYCAP_PADDING, KEYCAPSTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsLayoutLineTest, "SOLTest.KeyCaps.LayoutOneLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A row that fits lies on one line: caps, "+" and " / " pieces left to right with the gap on both sides of each piece
bool FSOLKeyCapsLayoutLineTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeyCapMetrics metrics = KeyCapsTestMetrics();
    const float gap = metrics.Gap;
    FSOLKeyCapLayout layout;

    // Two alternatives: cap, separator, cap
    const FSOLKeyCapModel pair = KeyCapsTestModel({ 1, 1 });
    const float pairWidths[] = { 30.0f, 40.0f };
    const float pairHeight = SOLKeyCaps::LayoutRow(pair, pairWidths, metrics, 1000.0f, layout);
    KeyCapsTestCheckLayout(*this, pair, layout, metrics, pairHeight, TEXT("pair"));
    TestEqual(TEXT("pair: one line"), layout.LineCount, 1);
    TestEqual(TEXT("pair: three items"), layout.Items.Num(), 3);
    if (layout.Items.Num() == 3)
    {
        TestTrue(TEXT("pair: cap first"), layout.Items[0].Kind == ESOLKeyCapItem::Cap && layout.Items[0].Cap == 0);
        TestTrue(TEXT("pair: separator second"), layout.Items[1].Kind == ESOLKeyCapItem::Separator);
        TestTrue(TEXT("pair: cap third"), layout.Items[2].Kind == ESOLKeyCapItem::Cap && layout.Items[2].Cap == 1);
        TestEqual(TEXT("pair: first cap at 0"), layout.Items[0].X, 0.0f, KEYCAPSTEST_TOLERANCE);
        TestEqual(TEXT("pair: first cap width"), layout.Items[0].Width, 30.0f, KEYCAPSTEST_TOLERANCE);
        TestEqual(TEXT("pair: separator after a gap"), layout.Items[1].X, 30.0f + gap, KEYCAPSTEST_TOLERANCE);
        TestEqual(TEXT("pair: second cap after the separator and a gap"), layout.Items[2].X,
            30.0f + gap + KEYCAPSTEST_SEPARATOR_WIDTH + gap, KEYCAPSTEST_TOLERANCE);
        TestEqual(TEXT("pair: second cap width"), layout.Items[2].Width, 40.0f, KEYCAPSTEST_TOLERANCE);
    }
    TestEqual(TEXT("pair: width"), layout.Width, 30.0f + gap + KEYCAPSTEST_SEPARATOR_WIDTH + gap + 40.0f,
        KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("pair: one cap tall"), pairHeight, metrics.CapHeight, KEYCAPSTEST_TOLERANCE);

    // One chord of two caps: cap, plus, cap
    const FSOLKeyCapModel chord = KeyCapsTestModel({ 2 });
    const float chordWidths[] = { 50.0f, 30.0f };
    const float chordHeight = SOLKeyCaps::LayoutRow(chord, chordWidths, metrics, 1000.0f, layout);
    KeyCapsTestCheckLayout(*this, chord, layout, metrics, chordHeight, TEXT("chord"));
    TestEqual(TEXT("chord: three items"), layout.Items.Num(), 3);
    if (layout.Items.Num() == 3)
    {
        TestTrue(TEXT("chord: plus between the caps"), layout.Items[1].Kind == ESOLKeyCapItem::Plus);
        TestEqual(TEXT("chord: plus after a gap"), layout.Items[1].X, 50.0f + gap, KEYCAPSTEST_TOLERANCE);
        TestEqual(TEXT("chord: second cap after the plus and a gap"), layout.Items[2].X,
            50.0f + gap + KEYCAPSTEST_PLUS_WIDTH + gap, KEYCAPSTEST_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsLayoutWrapTest, "SOLTest.KeyCaps.LayoutWrap",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An alternative that would cross the width wraps onto a new line; a chord stays whole when it fits a line
bool FSOLKeyCapsLayoutWrapTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeyCapMetrics metrics = KeyCapsTestMetrics();
    FSOLKeyCapLayout layout;

    // Two 30-wide alternatives in 60: the second goes to line 1 at the left edge
    const FSOLKeyCapModel pair = KeyCapsTestModel({ 1, 1 });
    const float pairWidths[] = { 30.0f, 30.0f };
    const float pairHeight = SOLKeyCaps::LayoutRow(pair, pairWidths, metrics, 60.0f, layout);
    KeyCapsTestCheckLayout(*this, pair, layout, metrics, pairHeight, TEXT("wrapped pair"));
    TestEqual(TEXT("wrapped pair: two lines"), layout.LineCount, 2);
    TestEqual(TEXT("wrapped pair: two lines tall"), pairHeight, 2.0f * metrics.CapHeight + metrics.RowGap,
        KEYCAPSTEST_TOLERANCE);
    TestTrue(TEXT("wrapped pair: inside the width"), layout.Width <= 60.0f + KEYCAPSTEST_TOLERANCE);
    if (const FSOLKeyCapLayoutItem* second = KeyCapsTestItemOfCap(layout, 1))
    {
        TestEqual(TEXT("wrapped pair: second cap on line 1"), second->Line, 1);
        TestEqual(TEXT("wrapped pair: second cap at the left"), second->X, 0.0f, KEYCAPSTEST_TOLERANCE);
        TestEqual(TEXT("wrapped pair: second cap below the first"), second->Y, metrics.CapHeight + metrics.RowGap,
            KEYCAPSTEST_TOLERANCE);
    }
    else
    {
        AddError(TEXT("wrapped pair: second cap not laid out"));
    }

    // A one-cap alternative then a two-cap chord (88 wide) in 100: the chord moves to line 1 whole
    const FSOLKeyCapModel mixed = KeyCapsTestModel({ 1, 2 });
    const float mixedWidths[] = { 30.0f, 40.0f, 30.0f };
    const float mixedHeight = SOLKeyCaps::LayoutRow(mixed, mixedWidths, metrics, 100.0f, layout);
    KeyCapsTestCheckLayout(*this, mixed, layout, metrics, mixedHeight, TEXT("mixed"));
    TestEqual(TEXT("mixed: two lines"), layout.LineCount, 2);
    const FSOLKeyCapLayoutItem* chordFirst = KeyCapsTestItemOfCap(layout, 1);
    const FSOLKeyCapLayoutItem* chordSecond = KeyCapsTestItemOfCap(layout, 2);
    if (chordFirst != nullptr && chordSecond != nullptr)
    {
        TestEqual(TEXT("mixed: chord starts line 1"), chordFirst->Line, 1);
        TestEqual(TEXT("mixed: chord not split"), chordSecond->Line, chordFirst->Line);
        TestEqual(TEXT("mixed: chord at the left"), chordFirst->X, 0.0f, KEYCAPSTEST_TOLERANCE);
    }
    else
    {
        AddError(TEXT("mixed: chord caps not laid out"));
    }
    for (const FSOLKeyCapLayoutItem& item : layout.Items)
    {
        TestTrue(TEXT("mixed: every item inside the width"), item.X + item.Width <= 100.0f + KEYCAPSTEST_TOLERANCE);
    }

    // Many alternatives wrap onto several lines, each inside the width
    const FSOLKeyCapModel many = KeyCapsTestModel({ 1, 1, 1, 1, 1, 1, 1 });
    const float manyWidths[] = { 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f, 30.0f };
    const float manyHeight = SOLKeyCaps::LayoutRow(many, manyWidths, metrics, 130.0f, layout);
    KeyCapsTestCheckLayout(*this, many, layout, metrics, manyHeight, TEXT("many"));
    TestTrue(TEXT("many: more than one line"), layout.LineCount > 1);
    for (const FSOLKeyCapLayoutItem& item : layout.Items)
    {
        TestTrue(TEXT("many: every item inside the width"), item.X + item.Width <= 130.0f + KEYCAPSTEST_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsLayoutSplitTest, "SOLTest.KeyCaps.LayoutChordSplit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A chord wider than the width on its own is split between its caps, each cap still inside the width
bool FSOLKeyCapsLayoutSplitTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeyCapMetrics metrics = KeyCapsTestMetrics();
    FSOLKeyCapLayout layout;
    const FSOLKeyCapModel chord = KeyCapsTestModel({ 2 });
    const float widths[] = { 40.0f, 30.0f };
    const float height = SOLKeyCaps::LayoutRow(chord, widths, metrics, 60.0f, layout);
    KeyCapsTestCheckLayout(*this, chord, layout, metrics, height, TEXT("split chord"));
    TestEqual(TEXT("split chord: two lines"), layout.LineCount, 2);
    const FSOLKeyCapLayoutItem* first = KeyCapsTestItemOfCap(layout, 0);
    const FSOLKeyCapLayoutItem* second = KeyCapsTestItemOfCap(layout, 1);
    if (first != nullptr && second != nullptr)
    {
        TestTrue(TEXT("split chord: caps on different lines"), first->Line != second->Line);
        TestTrue(TEXT("split chord: first cap inside"), first->X + first->Width <= 60.0f + KEYCAPSTEST_TOLERANCE);
        TestTrue(TEXT("split chord: second cap inside"), second->X + second->Width <= 60.0f + KEYCAPSTEST_TOLERANCE);
    }
    else
    {
        AddError(TEXT("split chord: caps not laid out"));
    }

    // The same chord in a width it fits is not split
    const float wholeHeight = SOLKeyCaps::LayoutRow(chord, widths, metrics, 200.0f, layout);
    KeyCapsTestCheckLayout(*this, chord, layout, metrics, wholeHeight, TEXT("whole chord"));
    TestEqual(TEXT("whole chord: one line"), layout.LineCount, 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsLayoutInputsTest, "SOLTest.KeyCaps.LayoutBadInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A zero, negative or NaN width lays out on one line; missing or bad cap widths read the minimum; an empty model is 0
bool FSOLKeyCapsLayoutInputsTest::RunTest(const FString& /*parameters*/)
{
    const FSOLKeyCapMetrics metrics = KeyCapsTestMetrics();
    FSOLKeyCapLayout layout;
    const FSOLKeyCapModel row = KeyCapsTestModel({ 1, 2, 1 });
    const float widths[] = { 30.0f, 30.0f, 30.0f, 30.0f };
    const float badWidths[] = { 0.0f, -10.0f, KEYCAPSTEST_NAN, std::numeric_limits<float>::infinity() };
    for (const float availableWidth : badWidths)
    {
        const FString what = FString::Printf(TEXT("width %f"), availableWidth);
        const float height = SOLKeyCaps::LayoutRow(row, widths, metrics, availableWidth, layout);
        KeyCapsTestCheckLayout(*this, row, layout, metrics, height, what);
        TestEqual(what + TEXT(": one line"), layout.LineCount, 1);
        TestEqual(what + TEXT(": one cap tall"), height, metrics.CapHeight, KEYCAPSTEST_TOLERANCE);
    }

    // No cap widths at all: every cap the minimum width
    const FSOLKeyCapModel pair = KeyCapsTestModel({ 1, 1 });
    const float pairHeight = SOLKeyCaps::LayoutRow(pair, TConstArrayView<float>(), metrics, 1000.0f, layout);
    KeyCapsTestCheckLayout(*this, pair, layout, metrics, pairHeight, TEXT("no widths"));
    for (const FSOLKeyCapLayoutItem& item : layout.Items)
    {
        if (item.Kind == ESOLKeyCapItem::Cap)
        {
            TestEqual(TEXT("no widths: cap at the minimum"), item.Width, SOL::Help::KEYCAP_MIN_WIDTH,
                KEYCAPSTEST_TOLERANCE);
        }
    }

    // A negative and a NaN cap width read the minimum
    const float bad[] = { -5.0f, KEYCAPSTEST_NAN };
    const float badHeight = SOLKeyCaps::LayoutRow(pair, bad, metrics, 1000.0f, layout);
    KeyCapsTestCheckLayout(*this, pair, layout, metrics, badHeight, TEXT("bad widths"));
    for (const FSOLKeyCapLayoutItem& item : layout.Items)
    {
        if (item.Kind == ESOLKeyCapItem::Cap)
        {
            TestEqual(TEXT("bad widths: cap at the minimum"), item.Width, SOL::Help::KEYCAP_MIN_WIDTH,
                KEYCAPSTEST_TOLERANCE);
        }
    }

    // An empty model
    FSOLKeyCapModel empty;
    layout.Items.AddDefaulted(3);
    layout.LineCount = 5;
    const float emptyHeight = SOLKeyCaps::LayoutRow(empty, widths, metrics, 100.0f, layout);
    TestEqual(TEXT("empty model: height 0"), emptyHeight, 0.0f);
    TestEqual(TEXT("empty model: no items"), layout.Items.Num(), 0);
    TestEqual(TEXT("empty model: no lines"), layout.LineCount, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsReuseTest, "SOLTest.KeyCaps.ModelReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Parsing and laying out into the same model and layout again keeps their buffers (no reallocation)
bool FSOLKeyCapsReuseTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const TCHAR* cell = TEXT("MiddleMouseButton|LeftShift+RightMouseButton");
    FSOLKeyCapModel model;
    SOLKeyCaps::ParseCell(cell, keys, model);
    const void* capsData = model.Caps.GetData();
    const SIZE_T capsSize = model.Caps.GetAllocatedSize();
    const void* chordsData = model.Chords.GetData();
    const SIZE_T chordsSize = model.Chords.GetAllocatedSize();
    TestTrue(TEXT("first parse made caps"), model.Caps.Num() > 0);

    SOLKeyCaps::ParseCell(cell, keys, model);
    TestTrue(TEXT("caps buffer kept"), model.Caps.GetData() == capsData);
    TestEqual(TEXT("caps capacity kept"), static_cast<int64>(model.Caps.GetAllocatedSize()),
        static_cast<int64>(capsSize));
    TestTrue(TEXT("chords buffer kept"), model.Chords.GetData() == chordsData);
    TestEqual(TEXT("chords capacity kept"), static_cast<int64>(model.Chords.GetAllocatedSize()),
        static_cast<int64>(chordsSize));

    // A shorter cell into the grown model keeps the buffers too
    SOLKeyCaps::ParseCell(TEXT("@Fire"), keys, model);
    TestTrue(TEXT("caps buffer kept for a shorter cell"), model.Caps.GetData() == capsData);

    // The layout keeps its buffer
    const FSOLKeyCapMetrics metrics = KeyCapsTestMetrics();
    const FSOLKeyCapModel row = KeyCapsTestModel({ 1, 2 });
    const float widths[] = { 30.0f, 30.0f, 30.0f };
    FSOLKeyCapLayout layout;
    SOLKeyCaps::LayoutRow(row, widths, metrics, 1000.0f, layout);
    const void* itemsData = layout.Items.GetData();
    const SIZE_T itemsSize = layout.Items.GetAllocatedSize();
    SOLKeyCaps::LayoutRow(row, widths, metrics, 1000.0f, layout);
    TestTrue(TEXT("layout buffer kept"), layout.Items.GetData() == itemsData);
    TestEqual(TEXT("layout capacity kept"), static_cast<int64>(layout.Items.GetAllocatedSize()),
        static_cast<int64>(itemsSize));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsColumnTest, "SOLTest.KeyCaps.ControlRowsFitColumn",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every Controls cell at the default keys lays out inside the Controls key column, with the caps sized as the page
// sizes them (SSOLKeyCapRow::Layout: DrawnCapWidth of the measured DrawnLegend, so the LED room and the mouse glyph
// count, and the measured "+" and "/" joints). The real measure needs Slate's font service, which the headless run lacks, so
// every legend glyph and joint glyph is given a conservative upper bound (KEYCAPSTEST_LEGEND_GLYPH_WIDTH)
bool FSOLKeyCapsColumnTest::RunTest(const FString& /*parameters*/)
{
    // An upper bound on one glyph's mean advance in the page's legend font, Rajdhani SemiBold at KEYCAP_FONT_SIZE
    // (15 pt is a 20 px em at Slate's 96 DPI; Rajdhani is condensed, its mean advance about 0.45 em, so 0.6 em = 12)
    constexpr float KEYCAPSTEST_LEGEND_GLYPH_WIDTH = 12.0f;
    static_assert(SOL::Help::KEYCAP_FONT_SIZE == 15, "the glyph bound assumes the 15 pt legend font: re-derive it");
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyCapMetrics metrics;
    metrics.PlusWidth = KEYCAPSTEST_LEGEND_GLYPH_WIDTH * FStringView(SOLMenuText::KEYCAP_CHORD_PLUS).Len();
    metrics.SeparatorWidth = KEYCAPSTEST_LEGEND_GLYPH_WIDTH * FStringView(SOLMenuText::KEYCAP_ALTERNATIVE_SLASH).Len();
    const float column = SOL::MenuStyle::KEY_COLUMN_WIDTH;
    FSOLKeyCapModel model;
    FSOLKeyCapLayout layout;
    TArray<float> widths;
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        const FString what = FString::Printf(TEXT("'%s'"), row.Bindings);
        SOLKeyCaps::ParseCell(row.Bindings, keys, model);
        widths.Reset();
        for (const FSOLKeyCap& keyCap : model.Caps)
        {
            const float legendWidth = KEYCAPSTEST_LEGEND_GLYPH_WIDTH * SOLKeyCaps::DrawnLegend(keyCap).Len();
            widths.Add(SOLKeyCaps::DrawnCapWidth(keyCap, legendWidth));
            TestTrue(FString::Printf(TEXT("%s: cap '%s' narrower than the column"), *what, *keyCap.Label),
                widths.Last() <= column);
        }
        const float height = SOLKeyCaps::LayoutRow(model, widths, metrics, column, layout);
        KeyCapsTestCheckLayout(*this, model, layout, metrics, height, what);
        TestTrue(what + TEXT(": at least one line"), layout.LineCount >= 1);
        for (const FSOLKeyCapLayoutItem& item : layout.Items)
        {
            TestTrue(what + TEXT(": item inside the column"), item.X + item.Width <= column + KEYCAPSTEST_TOLERANCE);
            if (item.Kind == ESOLKeyCapItem::Cap && widths.IsValidIndex(item.Cap))
            {
                TestEqual(what + TEXT(": cap at its drawn width"), item.Width, widths[item.Cap],
                    KEYCAPSTEST_TOLERANCE);
            }
        }
        // Chords only split when one chord alone is wider than the column (none is, even at the glyph bound)
        for (int32 cap = 1; cap < model.Caps.Num(); ++cap)
        {
            const FSOLKeyCapLayoutItem* previous = KeyCapsTestItemOfCap(layout, cap - 1);
            const FSOLKeyCapLayoutItem* current = KeyCapsTestItemOfCap(layout, cap);
            if (previous != nullptr && current != nullptr
                && KeyCapsTestChordOfCap(model, cap) == KeyCapsTestChordOfCap(model, cap - 1))
            {
                TestEqual(what + TEXT(": chord kept on one line"), current->Line, previous->Line);
            }
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Part 33b: the drawn legend, the content width and the mouse glyph of a cap (SDD 33 section 6), written against the
// header contract before the production code existed
// ---------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr float KEYCAPSTEST_INFINITY = std::numeric_limits<float>::infinity();

    //////////////////////////////////////////////////////////////////////////
    // Returns a hand-built cap (independent of ParseCell)
    FSOLKeyCap KeyCapsTestCap(const TCHAR* keyName, const TCHAR* label, const ESOLKeyCapShape shape,
        const ESOLMouseButton mouse)
    {
        FSOLKeyCap cap;
        cap.KeyName = keyName;
        cap.Label = label;
        cap.Shape = shape;
        cap.Mouse = mouse;
        return cap;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the single cap a one-key cell parses to, failing the test (and returning an unbound cap) otherwise
    FSOLKeyCap KeyCapsTestParseOne(FAutomationTestBase& test, const TCHAR* cell)
    {
        FSOLKeyCapModel model;
        SOLKeyCaps::ParseCell(cell, SOLKeyBindings::DefaultKeys(), model);
        if (!test.TestEqual(FString::Printf(TEXT("'%s' parses to one cap"), cell), model.Caps.Num(), 1))
        {
            FSOLKeyCap unbound;
            unbound.bUnbound = true;
            return unbound;
        }
        return model.Caps[0];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a mouse part's name for assertion messages
    const TCHAR* KeyCapsTestPartName(const ESOLMouseGlyphPart part)
    {
        switch (part)
        {
        case ESOLMouseGlyphPart::None:
            return TEXT("None");
        case ESOLMouseGlyphPart::Body:
            return TEXT("Body");
        case ESOLMouseGlyphPart::LeftButton:
            return TEXT("LeftButton");
        case ESOLMouseGlyphPart::RightButton:
            return TEXT("RightButton");
        case ESOLMouseGlyphPart::Wheel:
            return TEXT("Wheel");
        default:
            return TEXT("<invalid>");
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a lit part equals the expected one, naming both in the message
    void KeyCapsTestCheckPart(FAutomationTestBase& test, const FString& what, const ESOLMouseGlyphPart actual,
        const ESOLMouseGlyphPart expected)
    {
        test.TestEqual(FString::Printf(TEXT("%s (got %s, want %s)"), *what, KeyCapsTestPartName(actual),
            KeyCapsTestPartName(expected)), static_cast<int32>(actual), static_cast<int32>(expected));
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a box's corners (with the layout tolerance)
    void KeyCapsTestCheckBox(FAutomationTestBase& test, const FString& what, const FBox2f& box, const FVector2f& min,
        const FVector2f& max)
    {
        test.TestEqual(what + TEXT(": min x"), box.Min.X, min.X, KEYCAPSTEST_TOLERANCE);
        test.TestEqual(what + TEXT(": min y"), box.Min.Y, min.Y, KEYCAPSTEST_TOLERANCE);
        test.TestEqual(what + TEXT(": max x"), box.Max.X, max.X, KEYCAPSTEST_TOLERANCE);
        test.TestEqual(what + TEXT(": max y"), box.Max.Y, max.Y, KEYCAPSTEST_TOLERANCE);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether inner lies inside outer (with the layout tolerance)
    bool KeyCapsTestInside(const FBox2f& inner, const FBox2f& outer)
    {
        return inner.Min.X >= outer.Min.X - KEYCAPSTEST_TOLERANCE && inner.Min.Y >= outer.Min.Y - KEYCAPSTEST_TOLERANCE
            && inner.Max.X <= outer.Max.X + KEYCAPSTEST_TOLERANCE && inner.Max.Y <= outer.Max.Y + KEYCAPSTEST_TOLERANCE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a glyph's boxes are those of the contract formula for a valid topLeft and size
    void KeyCapsTestCheckGlyph(FAutomationTestBase& test, const FString& what, const FSOLMouseGlyph& glyph,
        const FVector2f& topLeft, const FVector2f& size)
    {
        using namespace SOL::Help;
        const float w = size.X;
        const float h = size.Y;
        const float cx = topLeft.X + 0.5f * w;
        const float buttonBottom = topLeft.Y + h * KEYCAP_MOUSE_BUTTON_FRACTION;
        const float wheelHalf = 0.5f * w * KEYCAP_MOUSE_WHEEL_WIDTH_FRACTION;
        KeyCapsTestCheckBox(test, what + TEXT(" body"), glyph.Body, topLeft, topLeft + size);
        KeyCapsTestCheckBox(test, what + TEXT(" left button"), glyph.LeftButton, topLeft,
            FVector2f(cx, buttonBottom));
        KeyCapsTestCheckBox(test, what + TEXT(" right button"), glyph.RightButton, FVector2f(cx, topLeft.Y),
            FVector2f(topLeft.X + w, buttonBottom));
        KeyCapsTestCheckBox(test, what + TEXT(" wheel"), glyph.Wheel,
            FVector2f(cx - wheelHalf, topLeft.Y + h * KEYCAP_MOUSE_WHEEL_TOP_FRACTION),
            FVector2f(cx + wheelHalf, topLeft.Y + h * KEYCAP_MOUSE_WHEEL_BOTTOM_FRACTION));
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts every box of a glyph is the point topLeft (the degenerate glyph of a bad size)
    void KeyCapsTestCheckPointGlyph(FAutomationTestBase& test, const FString& what, const FSOLMouseGlyph& glyph,
        const FVector2f& topLeft)
    {
        KeyCapsTestCheckBox(test, what + TEXT(" body"), glyph.Body, topLeft, topLeft);
        KeyCapsTestCheckBox(test, what + TEXT(" left button"), glyph.LeftButton, topLeft, topLeft);
        KeyCapsTestCheckBox(test, what + TEXT(" right button"), glyph.RightButton, topLeft, topLeft);
        KeyCapsTestCheckBox(test, what + TEXT(" wheel"), glyph.Wheel, topLeft, topLeft);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts box b is box a moved by offset
    void KeyCapsTestCheckMoved(FAutomationTestBase& test, const FString& what, const FBox2f& a, const FBox2f& b,
        const FVector2f& offset)
    {
        KeyCapsTestCheckBox(test, what, b, a.Min + offset, a.Max + offset);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts box b is box a scaled by factor about origin
    void KeyCapsTestCheckScaled(FAutomationTestBase& test, const FString& what, const FBox2f& a, const FBox2f& b,
        const FVector2f& origin, const float factor)
    {
        KeyCapsTestCheckBox(test, what, b, origin + (a.Min - origin) * factor, origin + (a.Max - origin) * factor);
    }

    // Every mouse part a cap can light, for the sweeps
    constexpr ESOLMouseButton KEYCAPSTEST_BUTTONS[] = { ESOLMouseButton::None, ESOLMouseButton::Left,
        ESOLMouseButton::Right, ESOLMouseButton::Middle, ESOLMouseButton::Wheel, ESOLMouseButton::Move };
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsMouseGlyphConstantsTest, "SOLTest.KeyCaps.MouseGlyphConstants",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The mouse glyph constants fit a cap and stack sensibly: the wheel inside the buttons' band, the glyph inside a
// minimum-width cap's padding and under the cap's height; the Move legend reads "Move"
bool FSOLKeyCapsMouseGlyphConstantsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    TestTrue(TEXT("the wheel starts below the glyph top"), KEYCAP_MOUSE_WHEEL_TOP_FRACTION > 0.0f);
    TestTrue(TEXT("the wheel ends below its top"), KEYCAP_MOUSE_WHEEL_TOP_FRACTION < KEYCAP_MOUSE_WHEEL_BOTTOM_FRACTION);
    TestTrue(TEXT("the wheel ends within the buttons' band"),
        KEYCAP_MOUSE_WHEEL_BOTTOM_FRACTION <= KEYCAP_MOUSE_BUTTON_FRACTION);
    TestTrue(TEXT("the buttons leave a body below them"), KEYCAP_MOUSE_BUTTON_FRACTION < 1.0f);
    TestTrue(TEXT("the wheel is narrower than the glyph"),
        KEYCAP_MOUSE_WHEEL_WIDTH_FRACTION > 0.0f && KEYCAP_MOUSE_WHEEL_WIDTH_FRACTION < 1.0f);
    TestTrue(TEXT("the glyph has a size"), KEYCAP_MOUSE_GLYPH_WIDTH > 0.0f && KEYCAP_MOUSE_GLYPH_HEIGHT > 0.0f);
    TestTrue(TEXT("the glyph is shorter than a cap"), KEYCAP_MOUSE_GLYPH_HEIGHT < KEYCAP_HEIGHT);
    TestTrue(TEXT("the glyph and its padding fit a minimum-width cap"),
        KEYCAP_MOUSE_GLYPH_WIDTH + 2.0f * KEYCAP_PADDING <= KEYCAP_MIN_WIDTH);
    TestEqual(TEXT("the Move legend"), FString(SOLMenuText::KEYCAP_MOVE_LEGEND), FString(TEXT("Move")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsDrawnLegendTest, "SOLTest.KeyCaps.DrawnLegend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The drawn legend: none on a mouse button cap (glyph only), "Move" on a mouse-move cap (its Label and text form
// unchanged), the Label on Wheel, unbound, normal and wide caps
bool FSOLKeyCapsDrawnLegendTest::RunTest(const FString& /*parameters*/)
{
    // Mouse buttons draw the glyph only
    const TCHAR* buttons[] = { TEXT("LeftMouseButton"), TEXT("RightMouseButton"), TEXT("MiddleMouseButton") };
    for (const TCHAR* cell : buttons)
    {
        const FSOLKeyCap cap = KeyCapsTestParseOne(*this, cell);
        TestTrue(FString::Printf(TEXT("'%s': no drawn legend"), cell), SOLKeyCaps::DrawnLegend(cap).IsEmpty());
        TestFalse(FString::Printf(TEXT("'%s': its Label is kept for the text form"), cell), cap.Label.IsEmpty());
    }

    // Mouse movement draws "Move"; its Label and the text form stay as they were
    const TCHAR* moves[] = { TEXT("Mouse2D"), TEXT("MouseX"), TEXT("MouseY") };
    for (const TCHAR* cell : moves)
    {
        const FSOLKeyCap cap = KeyCapsTestParseOne(*this, cell);
        TestEqual(FString::Printf(TEXT("'%s': drawn legend is Move"), cell), FString(SOLKeyCaps::DrawnLegend(cap)),
            FString(SOLMenuText::KEYCAP_MOVE_LEGEND));
        TestEqual(FString::Printf(TEXT("'%s': Label unchanged"), cell), cap.Label,
            SOLBindingLabel::KeyDisplayName(cell));
    }
    FSOLKeyCapModel model;
    SOLKeyCaps::ParseCell(TEXT("Mouse2D"), SOLKeyBindings::DefaultKeys(), model);
    TestEqual(TEXT("Mouse2D: the text form is unchanged"), SOLKeyCaps::ModelText(model),
        SOLMenuContent::FormatBindings(TEXT("Mouse2D"), SOLKeyBindings::DefaultKeys()));

    // Wheel, keyboard and unbound caps draw their Label
    const TCHAR* labelled[] = { TEXT("MouseWheelAxis"), TEXT("MouseScrollUp"), TEXT("MouseScrollDown"),
        TEXT("SpaceBar"), TEXT("K"), TEXT("LeftShift"), TEXT("F1"), TEXT("@NoSuchAction") };
    for (const TCHAR* cell : labelled)
    {
        const FSOLKeyCap cap = KeyCapsTestParseOne(*this, cell);
        TestEqual(FString::Printf(TEXT("'%s': drawn legend is the Label"), cell),
            FString(SOLKeyCaps::DrawnLegend(cap)), cap.Label);
        TestFalse(FString::Printf(TEXT("'%s': drawn legend not empty"), cell), SOLKeyCaps::DrawnLegend(cap).IsEmpty());
    }
    const FSOLKeyCap unbound = KeyCapsTestParseOne(*this, TEXT("@NoSuchAction"));
    TestEqual(TEXT("unbound: draws Unbound"), FString(SOLKeyCaps::DrawnLegend(unbound)),
        SOLBindingLabel::KeyDisplayName(FString()));

    // Hand-built caps
    const FSOLKeyCap left = KeyCapsTestCap(TEXT("LeftMouseButton"), TEXT("LMB"), ESOLKeyCapShape::Mouse,
        ESOLMouseButton::Left);
    const FSOLKeyCap right = KeyCapsTestCap(TEXT("RightMouseButton"), TEXT("RMB"), ESOLKeyCapShape::Mouse,
        ESOLMouseButton::Right);
    const FSOLKeyCap middle = KeyCapsTestCap(TEXT("MiddleMouseButton"), TEXT("MMB"), ESOLKeyCapShape::Mouse,
        ESOLMouseButton::Middle);
    const FSOLKeyCap move = KeyCapsTestCap(TEXT("Mouse2D"), TEXT("Mouse"), ESOLKeyCapShape::Mouse,
        ESOLMouseButton::Move);
    const FSOLKeyCap wheel = KeyCapsTestCap(TEXT("MouseScrollUp"), TEXT("Wheel Up"), ESOLKeyCapShape::Mouse,
        ESOLMouseButton::Wheel);
    const FSOLKeyCap normal = KeyCapsTestCap(TEXT("Escape"), TEXT("Esc"), ESOLKeyCapShape::Normal,
        ESOLMouseButton::None);
    const FSOLKeyCap wide = KeyCapsTestCap(TEXT("LeftShift"), TEXT("Shift"), ESOLKeyCapShape::Wide,
        ESOLMouseButton::None);
    TestTrue(TEXT("hand-built left button: glyph only"), SOLKeyCaps::DrawnLegend(left).IsEmpty());
    TestTrue(TEXT("hand-built right button: glyph only"), SOLKeyCaps::DrawnLegend(right).IsEmpty());
    TestTrue(TEXT("hand-built middle button: glyph only"), SOLKeyCaps::DrawnLegend(middle).IsEmpty());
    TestEqual(TEXT("hand-built move: Move"), FString(SOLKeyCaps::DrawnLegend(move)),
        FString(SOLMenuText::KEYCAP_MOVE_LEGEND));
    TestEqual(TEXT("hand-built wheel: its Label"), FString(SOLKeyCaps::DrawnLegend(wheel)), FString(TEXT("Wheel Up")));
    TestEqual(TEXT("hand-built normal: its Label"), FString(SOLKeyCaps::DrawnLegend(normal)), FString(TEXT("Esc")));
    TestEqual(TEXT("hand-built wide: its Label"), FString(SOLKeyCaps::DrawnLegend(wide)), FString(TEXT("Shift")));

    // Every Controls cell at the defaults: only the mouse button caps draw no legend
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        SOLKeyCaps::ParseCell(row.Bindings, SOLKeyBindings::DefaultKeys(), model);
        for (int32 cap = 0; cap < model.Caps.Num(); ++cap)
        {
            const FSOLKeyCap& keyCap = model.Caps[cap];
            const bool bButton = keyCap.Mouse == ESOLMouseButton::Left || keyCap.Mouse == ESOLMouseButton::Right
                || keyCap.Mouse == ESOLMouseButton::Middle;
            TestTrue(FString::Printf(TEXT("'%s' cap %d (%s): %s"), row.Bindings, cap, *keyCap.KeyName,
                bButton ? TEXT("glyph only") : TEXT("a drawn legend")),
                SOLKeyCaps::DrawnLegend(keyCap).IsEmpty() == bButton);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsContentWidthTest, "SOLTest.KeyCaps.ContentWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A Mouse cap's content is the glyph, plus a gap and its legend when it has one; any other cap's is its legend; a
// negative or NaN legend reads 0; a glyph-only cap is the minimum cap width
bool FSOLKeyCapsContentWidthTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;

    // Mouse caps (parsed)
    const TCHAR* mouseCells[] = { TEXT("LeftMouseButton"), TEXT("RightMouseButton"), TEXT("MiddleMouseButton"),
        TEXT("Mouse2D"), TEXT("MouseWheelAxis") };
    for (const TCHAR* cell : mouseCells)
    {
        const FSOLKeyCap cap = KeyCapsTestParseOne(*this, cell);
        TestTrue(FString::Printf(TEXT("'%s' is a Mouse cap"), cell), cap.Shape == ESOLKeyCapShape::Mouse);
        TestEqual(FString::Printf(TEXT("'%s': no legend is the glyph"), cell), SOLKeyCaps::ContentWidth(cap, 0.0f),
            KEYCAP_MOUSE_GLYPH_WIDTH, KEYCAPSTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("'%s': a negative legend reads 0"), cell),
            SOLKeyCaps::ContentWidth(cap, -20.0f), KEYCAP_MOUSE_GLYPH_WIDTH, KEYCAPSTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("'%s': a NaN legend reads 0"), cell),
            SOLKeyCaps::ContentWidth(cap, KEYCAPSTEST_NAN), KEYCAP_MOUSE_GLYPH_WIDTH, KEYCAPSTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("'%s': glyph, gap and legend"), cell), SOLKeyCaps::ContentWidth(cap, 33.0f),
            KEYCAP_MOUSE_GLYPH_WIDTH + KEYCAP_GAP + 33.0f, KEYCAPSTEST_TOLERANCE);
    }

    // A glyph-only cap fits the minimum cap width
    const TCHAR* buttonCells[] = { TEXT("LeftMouseButton"), TEXT("RightMouseButton"), TEXT("MiddleMouseButton") };
    for (const TCHAR* cell : buttonCells)
    {
        const FSOLKeyCap cap = KeyCapsTestParseOne(*this, cell);
        TestEqual(FString::Printf(TEXT("'%s': drawn at the minimum width"), cell),
            SOLKeyCaps::CapWidth(cap, SOLKeyCaps::ContentWidth(cap, 0.0f)), KEYCAP_MIN_WIDTH, KEYCAPSTEST_TOLERANCE);
    }

    // A Move cap with a legend: glyph, gap, legend and the padding a side
    const FSOLKeyCap move = KeyCapsTestParseOne(*this, TEXT("Mouse2D"));
    const float moveLegend = KEYCAPSTEST_CHAR_WIDTH * FString(SOLMenuText::KEYCAP_MOVE_LEGEND).Len();
    TestEqual(TEXT("Mouse2D with Move: the drawn width"), SOLKeyCaps::CapWidth(move, SOLKeyCaps::ContentWidth(move,
        moveLegend)), FMath::Max(KEYCAP_MIN_WIDTH, KEYCAP_MOUSE_GLYPH_WIDTH + KEYCAP_GAP + moveLegend
        + 2.0f * KEYCAP_PADDING), KEYCAPSTEST_TOLERANCE);

    // Other caps: the legend alone
    const TCHAR* otherCells[] = { TEXT("K"), TEXT("SpaceBar"), TEXT("LeftShift"), TEXT("@NoSuchAction") };
    for (const TCHAR* cell : otherCells)
    {
        const FSOLKeyCap cap = KeyCapsTestParseOne(*this, cell);
        TestTrue(FString::Printf(TEXT("'%s' is not a Mouse cap"), cell), cap.Shape != ESOLKeyCapShape::Mouse);
        TestEqual(FString::Printf(TEXT("'%s': the legend"), cell), SOLKeyCaps::ContentWidth(cap, 25.0f), 25.0f,
            KEYCAPSTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("'%s': no legend is 0"), cell), SOLKeyCaps::ContentWidth(cap, 0.0f), 0.0f,
            KEYCAPSTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("'%s': a negative legend reads 0"), cell),
            SOLKeyCaps::ContentWidth(cap, -7.0f), 0.0f, KEYCAPSTEST_TOLERANCE);
        TestEqual(FString::Printf(TEXT("'%s': a NaN legend reads 0"), cell),
            SOLKeyCaps::ContentWidth(cap, KEYCAPSTEST_NAN), 0.0f, KEYCAPSTEST_TOLERANCE);
    }

    // Hand-built caps follow the shape
    const FSOLKeyCap mouse = KeyCapsTestCap(TEXT("RightMouseButton"), TEXT("RMB"), ESOLKeyCapShape::Mouse,
        ESOLMouseButton::Right);
    const FSOLKeyCap normal = KeyCapsTestCap(TEXT("K"), TEXT("K"), ESOLKeyCapShape::Normal, ESOLMouseButton::None);
    TestEqual(TEXT("hand-built Mouse cap"), SOLKeyCaps::ContentWidth(mouse, 10.0f),
        KEYCAP_MOUSE_GLYPH_WIDTH + KEYCAP_GAP + 10.0f, KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("hand-built normal cap"), SOLKeyCaps::ContentWidth(normal, 10.0f), 10.0f, KEYCAPSTEST_TOLERANCE);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsGlyphPartOfTest, "SOLTest.KeyCaps.GlyphPartOf",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each mouse part a cap lights maps to the glyph part drawn lit: the buttons to their halves, the middle button and
// the wheel to the wheel, movement to the whole body, no part to none
bool FSOLKeyCapsGlyphPartOfTest::RunTest(const FString& /*parameters*/)
{
    struct FKeyCapsTestPartCase
    {
        ESOLMouseButton Button;
        ESOLMouseGlyphPart Part;
        const TCHAR* Name;
    };
    const FKeyCapsTestPartCase cases[] = {
        { ESOLMouseButton::Left, ESOLMouseGlyphPart::LeftButton, TEXT("Left") },
        { ESOLMouseButton::Right, ESOLMouseGlyphPart::RightButton, TEXT("Right") },
        { ESOLMouseButton::Middle, ESOLMouseGlyphPart::Wheel, TEXT("Middle") },
        { ESOLMouseButton::Wheel, ESOLMouseGlyphPart::Wheel, TEXT("Wheel") },
        { ESOLMouseButton::Move, ESOLMouseGlyphPart::Body, TEXT("Move") },
        { ESOLMouseButton::None, ESOLMouseGlyphPart::None, TEXT("None") },
    };
    for (const FKeyCapsTestPartCase& entry : cases)
    {
        KeyCapsTestCheckPart(*this, FString::Printf(TEXT("GlyphPartOf(%s)"), entry.Name),
            SOLKeyCaps::GlyphPartOf(entry.Button), entry.Part);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsMouseGlyphTest, "SOLTest.KeyCaps.MouseGlyph",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The mouse glyph's boxes: the body fills the box, the two buttons split its top band at the centre, the wheel sits
// centred in that band; the boxes move with topLeft and scale with size; a bad size gives point boxes at topLeft;
// the lit part is always GlyphPartOf(button)
bool FSOLKeyCapsMouseGlyphTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    const FVector2f topLeft(100.0f, 50.0f);
    const FVector2f size(KEYCAP_MOUSE_GLYPH_WIDTH, KEYCAP_MOUSE_GLYPH_HEIGHT);

    // The formula, at the glyph size and at another one
    const FSOLMouseGlyph glyph = SOLKeyCaps::MouseGlyph(topLeft, size, ESOLMouseButton::Left);
    KeyCapsTestCheckGlyph(*this, TEXT("glyph"), glyph, topLeft, size);
    const FVector2f otherSize(40.0f, 64.0f);
    KeyCapsTestCheckGlyph(*this, TEXT("40x64 glyph"), SOLKeyCaps::MouseGlyph(topLeft, otherSize,
        ESOLMouseButton::None), topLeft, otherSize);

    // The lit part follows the button
    for (const ESOLMouseButton button : KEYCAPSTEST_BUTTONS)
    {
        KeyCapsTestCheckPart(*this, FString::Printf(TEXT("button %d lights its part"), static_cast<int32>(button)),
            SOLKeyCaps::MouseGlyph(topLeft, size, button).Lit, SOLKeyCaps::GlyphPartOf(button));
    }

    // Shape properties
    const float cx = topLeft.X + 0.5f * size.X;
    TestTrue(TEXT("left button inside the body"), KeyCapsTestInside(glyph.LeftButton, glyph.Body));
    TestTrue(TEXT("right button inside the body"), KeyCapsTestInside(glyph.RightButton, glyph.Body));
    TestTrue(TEXT("wheel inside the body"), KeyCapsTestInside(glyph.Wheel, glyph.Body));
    TestEqual(TEXT("left button ends at the centre"), glyph.LeftButton.Max.X, cx, KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("right button starts at the centre"), glyph.RightButton.Min.X, cx, KEYCAPSTEST_TOLERANCE);
    TestTrue(TEXT("the buttons do not overlap"), glyph.LeftButton.Max.X <= glyph.RightButton.Min.X
        + KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("the buttons share a top"), glyph.LeftButton.Min.Y, glyph.RightButton.Min.Y, KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("the buttons share a bottom"), glyph.LeftButton.Max.Y, glyph.RightButton.Max.Y,
        KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("the buttons are as wide"), glyph.LeftButton.Max.X - glyph.LeftButton.Min.X,
        glyph.RightButton.Max.X - glyph.RightButton.Min.X, KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("the wheel is centred"), 0.5f * (glyph.Wheel.Min.X + glyph.Wheel.Max.X), cx, KEYCAPSTEST_TOLERANCE);
    TestTrue(TEXT("the wheel has a size"), glyph.Wheel.Max.X > glyph.Wheel.Min.X && glyph.Wheel.Max.Y
        > glyph.Wheel.Min.Y);
    TestTrue(TEXT("the wheel lies within the buttons' height"), glyph.Wheel.Min.Y >= glyph.LeftButton.Min.Y
        - KEYCAPSTEST_TOLERANCE && glyph.Wheel.Max.Y <= glyph.LeftButton.Max.Y + KEYCAPSTEST_TOLERANCE);
    TestTrue(TEXT("the buttons end above the body's bottom"), glyph.LeftButton.Max.Y < glyph.Body.Max.Y);

    // Moving topLeft moves every box
    const FVector2f origin(0.0f, 0.0f);
    const FVector2f offset(37.5f, -12.25f);
    const FSOLMouseGlyph atOrigin = SOLKeyCaps::MouseGlyph(origin, size, ESOLMouseButton::Right);
    const FSOLMouseGlyph moved = SOLKeyCaps::MouseGlyph(origin + offset, size, ESOLMouseButton::Right);
    KeyCapsTestCheckMoved(*this, TEXT("moved body"), atOrigin.Body, moved.Body, offset);
    KeyCapsTestCheckMoved(*this, TEXT("moved left button"), atOrigin.LeftButton, moved.LeftButton, offset);
    KeyCapsTestCheckMoved(*this, TEXT("moved right button"), atOrigin.RightButton, moved.RightButton, offset);
    KeyCapsTestCheckMoved(*this, TEXT("moved wheel"), atOrigin.Wheel, moved.Wheel, offset);

    // Scaling size scales every box about topLeft
    const float factor = 2.5f;
    const FSOLMouseGlyph scaled = SOLKeyCaps::MouseGlyph(topLeft, size * factor, ESOLMouseButton::Middle);
    KeyCapsTestCheckScaled(*this, TEXT("scaled body"), glyph.Body, scaled.Body, topLeft, factor);
    KeyCapsTestCheckScaled(*this, TEXT("scaled left button"), glyph.LeftButton, scaled.LeftButton, topLeft, factor);
    KeyCapsTestCheckScaled(*this, TEXT("scaled right button"), glyph.RightButton, scaled.RightButton, topLeft, factor);
    KeyCapsTestCheckScaled(*this, TEXT("scaled wheel"), glyph.Wheel, scaled.Wheel, topLeft, factor);

    // A size that is not a positive finite number gives point boxes at topLeft, still lighting the part
    const FVector2f badSizes[] = { FVector2f(0.0f, 18.0f), FVector2f(12.0f, 0.0f), FVector2f(-12.0f, 18.0f),
        FVector2f(12.0f, -1.0f), FVector2f(KEYCAPSTEST_NAN, 18.0f), FVector2f(12.0f, KEYCAPSTEST_NAN),
        FVector2f(KEYCAPSTEST_INFINITY, 18.0f), FVector2f(12.0f, KEYCAPSTEST_INFINITY), FVector2f(0.0f, 0.0f) };
    for (const FVector2f& badSize : badSizes)
    {
        const FString what = FString::Printf(TEXT("size (%f, %f)"), badSize.X, badSize.Y);
        const FSOLMouseGlyph bad = SOLKeyCaps::MouseGlyph(topLeft, badSize, ESOLMouseButton::Right);
        KeyCapsTestCheckPointGlyph(*this, what, bad, topLeft);
        KeyCapsTestCheckPart(*this, what + TEXT(": still lit"), bad.Lit, ESOLMouseGlyphPart::RightButton);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapsDrawnCapWidthTest, "SOLTest.KeyCaps.DrawnCapWidth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// (33b review) A bound cap's content starts past the Small hull's LED (KEYCAP_LED_ROOM, the LED's 13 units plus the
// gap), an unbound cap's after the padding; the drawn width is that room, the content and the right padding, at least
// the cap's minimum width
bool FSOLKeyCapsDrawnCapWidthTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Help;
    TestEqual(TEXT("the LED room clears the LED by the gap"), KEYCAP_LED_ROOM, SOL::SolidMenu::SMALL_BUTTON_LED_X_PX
        + SOL::SolidMenu::LED_RADIUS_PX + KEYCAP_LED_GAP, KEYCAPSTEST_TOLERANCE);
    TestTrue(TEXT("the LED room is wider than the padding"), KEYCAP_LED_ROOM > KEYCAP_PADDING);

    const FSOLKeyCap letter = KeyCapsTestParseOne(*this, TEXT("M"));
    TestEqual(TEXT("a bound cap starts past the LED"), SOLKeyCaps::ContentLeft(letter), KEYCAP_LED_ROOM,
        KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("a bound letter: room, legend and padding"), SOLKeyCaps::DrawnCapWidth(letter, 12.0f),
        FMath::Max(KEYCAP_LED_ROOM + 12.0f + KEYCAP_PADDING, KEYCAP_MIN_WIDTH), KEYCAPSTEST_TOLERANCE);
    TestEqual(TEXT("a bound cap without a legend keeps the minimum"), SOLKeyCaps::DrawnCapWidth(letter, 0.0f),
        FMath::Max(KEYCAP_LED_ROOM + KEYCAP_PADDING, KEYCAP_MIN_WIDTH), KEYCAPSTEST_TOLERANCE);

    const FSOLKeyCap space = KeyCapsTestParseOne(*this, TEXT("SpaceBar"));
    TestEqual(TEXT("the space bar keeps its wide minimum"), SOLKeyCaps::DrawnCapWidth(space, 40.0f),
        KEYCAP_SPACE_UNITS * KEYCAP_MIN_WIDTH, KEYCAPSTEST_TOLERANCE);

    const FSOLKeyCap mouse = KeyCapsTestParseOne(*this, TEXT("Mouse2D"));
    TestEqual(TEXT("a Move cap: room, glyph, gap, legend and padding"), SOLKeyCaps::DrawnCapWidth(mouse, 30.0f),
        KEYCAP_LED_ROOM + KEYCAP_MOUSE_GLYPH_WIDTH + KEYCAP_GAP + 30.0f + KEYCAP_PADDING, KEYCAPSTEST_TOLERANCE);

    FSOLKeyCapModel unbound;
    SOLKeyCaps::ParseCell(TEXT(""), SOLKeyBindings::DefaultKeys(), unbound);
    if (TestEqual(TEXT("an empty cell is one cap"), unbound.Caps.Num(), 1))
    {
        const FSOLKeyCap& cap = unbound.Caps[0];
        TestTrue(TEXT("the cap is unbound"), cap.bUnbound);
        TestEqual(TEXT("an unbound cap starts after the padding"), SOLKeyCaps::ContentLeft(cap), KEYCAP_PADDING,
            KEYCAPSTEST_TOLERANCE);
        TestEqual(TEXT("an unbound cap is CapWidth of its legend"), SOLKeyCaps::DrawnCapWidth(cap, 50.0f),
            SOLKeyCaps::CapWidth(cap, 50.0f), KEYCAPSTEST_TOLERANCE);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// The shared cap painter's helpers (33g) degrade to nothing without a service or a text: no measure gives width 0,
// no font cache or an empty text shapes nothing, and a missing shaped text or a non-positive scale has width 0
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapPaintNullInputsTest, "SOLTest.KeyCapPaint.NullInputs",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

// Runs the cap painter helpers' null and empty input checks
bool FSOLKeyCapPaintNullInputsTest::RunTest(const FString& /*parameters*/)
{
    const FSlateFontInfo font;
    TestEqual(TEXT("No measure service: a range is 0 wide"),
        SOLKeyCapPaint::MeasureRange(nullptr, TEXT("Shift"), 0, 5, font, 1.0f), 0.0f);
    TestEqual(TEXT("No measure service: a text is 0 wide"),
        SOLKeyCapPaint::Measure(nullptr, TEXT("Shift"), font), 0.0f);
    TestFalse(TEXT("No font cache: nothing shaped"),
        SOLKeyCapPaint::Shape(nullptr, TEXT("Shift"), font, 1.0f).IsValid());
    TestFalse(TEXT("No font cache, empty text: nothing shaped"),
        SOLKeyCapPaint::Shape(nullptr, FStringView(), font, 1.0f).IsValid());
    TestEqual(TEXT("No shaped text: 0 wide"), SOLKeyCapPaint::ShapedWidth(nullptr, 1.0f), 0.0f);
    TestEqual(TEXT("No shaped text at scale 0: 0 wide"), SOLKeyCapPaint::ShapedWidth(nullptr, 0.0f), 0.0f);
    return true;
}

#endif
