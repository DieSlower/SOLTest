/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLBindingLabel.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyCapRow.h"
#include "Menu/SOLKeyCaps.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "Menu/SOLSolidStyle.h"

#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33b key cap row widget (Docs/SDDs/33-player-documentation.md sections 6 and 10),
// written against the header contract (Menu/SOLKeyCapRow.h, SOLMenuContent::RefreshControlsPage) before the production
// code existed. They pin the widget's headless state (its model, text form, message, wrap width and legend colour) and
// the Controls page refresh; the Slate painting is checked by the menu smokes. No test calls TakeWidget.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr float KEYCAPROWTEST_COLOR_TOLERANCE = 1.0e-4f;
    constexpr int32 KEYCAPROWTEST_FIRE = static_cast<int32>(SOLKeyBindings::EBinding::Fire);
    constexpr int32 KEYCAPROWTEST_ROW_COUNT = static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS));

    //////////////////////////////////////////////////////////////////////////
    // Returns a fresh cap row widget in the transient package (never given a Slate widget)
    USOLKeyCapRow* KeyCapRowTestNew()
    {
        return NewObject<USOLKeyCapRow>(GetTransientPackage());
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the default keys with Fire moved to K (a rebound key set)
    TArray<FString> KeyCapRowTestReboundKeys()
    {
        TArray<FString> keys = SOLKeyBindings::DefaultKeys();
        if (keys.IsValidIndex(KEYCAPROWTEST_FIRE))
        {
            keys[KEYCAPROWTEST_FIRE] = TEXT("K");
        }
        return keys;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts two models hold the same caps (count, key names, labels, unbound flags) and the same chord count
    void KeyCapRowTestCheckSameModel(FAutomationTestBase& test, const FString& what, const FSOLKeyCapModel& actual,
        const FSOLKeyCapModel& expected)
    {
        test.TestEqual(what + TEXT(": cap count"), actual.Caps.Num(), expected.Caps.Num());
        test.TestEqual(what + TEXT(": chord count"), actual.Chords.Num(), expected.Chords.Num());
        const int32 count = FMath::Min(actual.Caps.Num(), expected.Caps.Num());
        for (int32 cap = 0; cap < count; ++cap)
        {
            test.TestEqual(FString::Printf(TEXT("%s: cap %d key"), *what, cap), actual.Caps[cap].KeyName,
                expected.Caps[cap].KeyName);
            test.TestEqual(FString::Printf(TEXT("%s: cap %d label"), *what, cap), actual.Caps[cap].Label,
                expected.Caps[cap].Label);
            test.TestTrue(FString::Printf(TEXT("%s: cap %d unbound flag"), *what, cap),
                actual.Caps[cap].bUnbound == expected.Caps[cap].bUnbound);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two colours match within the tolerance
    bool KeyCapRowTestSameColor(const FLinearColor& a, const FLinearColor& b)
    {
        return a.Equals(b, KEYCAPROWTEST_COLOR_TOLERANCE);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapRowFreshTest, "SOLTest.KeyCapRow.Fresh",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A row never set has an empty model and text, no message, one line (wrap width 0) and the text colour legend
bool FSOLKeyCapRowFreshTest::RunTest(const FString& /*parameters*/)
{
    const USOLKeyCapRow* const row = KeyCapRowTestNew();
    if (!TestNotNull(TEXT("the row was created"), row))
    {
        return true;
    }
    TestEqual(TEXT("no caps"), row->GetModel().Caps.Num(), 0);
    TestEqual(TEXT("no chords"), row->GetModel().Chords.Num(), 0);
    TestTrue(TEXT("empty text"), row->GetText().IsEmpty());
    TestFalse(TEXT("no message"), row->IsShowingMessage());
    TestEqual(TEXT("wrap width 0 (one line)"), row->GetWrapWidth(), 0.0f);
    TestTrue(TEXT("legend in the text colour"), KeyCapRowTestSameColor(row->GetLegendColor(),
        SOLSolidStyle::TextColor()));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapRowSetCellTest, "SOLTest.KeyCapRow.SetCell",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetCell shows a Controls cell's caps: its text is FormatBindings and its model ParseCell's, for every row at the
// default keys and at a rebound key set, one widget reused across all of them
bool FSOLKeyCapRowSetCellTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyCapRow* const row = KeyCapRowTestNew();
    if (!TestNotNull(TEXT("the row was created"), row))
    {
        return true;
    }
    const TArray<FString> keySets[] = { SOLKeyBindings::DefaultKeys(), KeyCapRowTestReboundKeys() };
    const TCHAR* keySetNames[] = { TEXT("defaults"), TEXT("fire on K") };
    FSOLKeyCapModel expected;
    for (int32 set = 0; set < static_cast<int32>(UE_ARRAY_COUNT(keySets)); ++set)
    {
        for (const SOLMenuText::FControlRow& control : SOLMenuText::CONTROL_ROWS)
        {
            const FString what = FString::Printf(TEXT("%s: '%s'"), keySetNames[set], control.Bindings);
            row->SetCell(control.Bindings, keySets[set]);
            TestEqual(what + TEXT(" text"), row->GetText(),
                SOLMenuContent::FormatBindings(control.Bindings, keySets[set]));
            TestFalse(what + TEXT(" shows no message"), row->IsShowingMessage());
            SOLKeyCaps::ParseCell(control.Bindings, keySets[set], expected);
            KeyCapRowTestCheckSameModel(*this, what, row->GetModel(), expected);
        }
    }

    // The rebound key shows: a cell naming Fire differs between the key sets
    row->SetCell(TEXT("@Fire"), keySets[1]);
    TestEqual(TEXT("@Fire rebound: the text names K"), row->GetText(),
        SOLMenuContent::FormatBindings(TEXT("@Fire"), keySets[1]));
    TestTrue(TEXT("@Fire rebound: the model's key is K"), row->GetModel().Caps.Num() == 1
        && row->GetModel().Caps[0].KeyName == TEXT("K"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapRowSetBindingTest, "SOLTest.KeyCapRow.SetBinding",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetBinding shows one binding's caps: its text is BindingDisplayName and its model ParseBinding's, for every binding
// at the default keys and at a rebound key set
bool FSOLKeyCapRowSetBindingTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyCapRow* const row = KeyCapRowTestNew();
    if (!TestNotNull(TEXT("the row was created"), row))
    {
        return true;
    }
    const TArray<FString> keySets[] = { SOLKeyBindings::DefaultKeys(), KeyCapRowTestReboundKeys() };
    const TCHAR* keySetNames[] = { TEXT("defaults"), TEXT("fire on K") };
    FSOLKeyCapModel expected;
    for (int32 set = 0; set < static_cast<int32>(UE_ARRAY_COUNT(keySets)); ++set)
    {
        for (int32 binding = 0; binding < SOLKeyBindings::BINDING_COUNT; ++binding)
        {
            const FString what = FString::Printf(TEXT("%s: binding %d"), keySetNames[set], binding);
            row->SetBinding(binding, keySets[set]);
            TestEqual(what + TEXT(" text"), row->GetText(), SOLKeyBindings::BindingDisplayName(binding,
                keySets[set]));
            TestFalse(what + TEXT(" shows no message"), row->IsShowingMessage());
            SOLKeyCaps::ParseBinding(binding, keySets[set], expected);
            KeyCapRowTestCheckSameModel(*this, what, row->GetModel(), expected);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapRowMessageTest, "SOLTest.KeyCapRow.Message",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SetMessage shows the rebind waiting prompt in place of the caps, keeping the model; the next SetBinding or SetCell
// ends it
bool FSOLKeyCapRowMessageTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyCapRow* const row = KeyCapRowTestNew();
    if (!TestNotNull(TEXT("the row was created"), row))
    {
        return true;
    }
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const FString waiting(SOLMenuText::SETTINGS_KEY_WAITING);

    // The prompt replaces the text, the caps stay
    row->SetBinding(KEYCAPROWTEST_FIRE, keys);
    const FSOLKeyCapModel before = row->GetModel();
    row->SetMessage(waiting);
    TestTrue(TEXT("waiting: a message shows"), row->IsShowingMessage());
    TestEqual(TEXT("waiting: the text is the prompt"), row->GetText(), waiting);
    KeyCapRowTestCheckSameModel(*this, TEXT("waiting: the model is kept"), row->GetModel(), before);

    // A new key ends the prompt and shows the binding's name
    const TArray<FString> rebound = KeyCapRowTestReboundKeys();
    row->SetBinding(KEYCAPROWTEST_FIRE, rebound);
    TestFalse(TEXT("rebound: the message ended"), row->IsShowingMessage());
    TestEqual(TEXT("rebound: the binding's name"), row->GetText(),
        SOLKeyBindings::BindingDisplayName(KEYCAPROWTEST_FIRE, rebound));

    // A cancelled rebind (the same key set again) ends it too
    row->SetMessage(waiting);
    row->SetBinding(KEYCAPROWTEST_FIRE, keys);
    TestFalse(TEXT("cancelled: the message ended"), row->IsShowingMessage());
    TestEqual(TEXT("cancelled: the binding's name"), row->GetText(),
        SOLKeyBindings::BindingDisplayName(KEYCAPROWTEST_FIRE, keys));

    // SetCell ends a message as well
    row->SetMessage(waiting);
    row->SetCell(TEXT("@Fire"), keys);
    TestFalse(TEXT("SetCell: the message ended"), row->IsShowingMessage());
    TestEqual(TEXT("SetCell: the cell's text"), row->GetText(), SOLMenuContent::FormatBindings(TEXT("@Fire"), keys));

    // A message on a row never set: the text is the message, the model stays empty
    USOLKeyCapRow* const fresh = KeyCapRowTestNew();
    if (TestNotNull(TEXT("the fresh row was created"), fresh))
    {
        fresh->SetMessage(waiting);
        TestTrue(TEXT("fresh: a message shows"), fresh->IsShowingMessage());
        TestEqual(TEXT("fresh: the text is the prompt"), fresh->GetText(), waiting);
        TestEqual(TEXT("fresh: no caps"), fresh->GetModel().Caps.Num(), 0);
    }

    // A second message replaces the first
    row->SetMessage(waiting);
    row->SetMessage(TEXT("Another prompt"));
    TestEqual(TEXT("a second message replaces the first"), row->GetText(), FString(TEXT("Another prompt")));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapRowUnboundTest, "SOLTest.KeyCapRow.Unbound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An empty key shows the single unbound cap ("Unbound"), through SetBinding and through SetCell
bool FSOLKeyCapRowUnboundTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyCapRow* const row = KeyCapRowTestNew();
    if (!TestNotNull(TEXT("the row was created"), row))
    {
        return true;
    }
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    keys[KEYCAPROWTEST_FIRE] = FString();
    const FString unboundLabel = SOLBindingLabel::KeyDisplayName(FString());

    row->SetBinding(KEYCAPROWTEST_FIRE, keys);
    TestEqual(TEXT("SetBinding: one cap"), row->GetModel().Caps.Num(), 1);
    if (row->GetModel().Caps.Num() == 1)
    {
        TestTrue(TEXT("SetBinding: the unbound cap"), row->GetModel().Caps[0].bUnbound);
        TestEqual(TEXT("SetBinding: Unbound legend"), row->GetModel().Caps[0].Label, unboundLabel);
    }
    TestEqual(TEXT("SetBinding: text"), row->GetText(), SOLKeyBindings::BindingDisplayName(KEYCAPROWTEST_FIRE, keys));

    row->SetCell(TEXT("@Fire"), keys);
    TestEqual(TEXT("SetCell: one cap"), row->GetModel().Caps.Num(), 1);
    if (row->GetModel().Caps.Num() == 1)
    {
        TestTrue(TEXT("SetCell: the unbound cap"), row->GetModel().Caps[0].bUnbound);
        TestEqual(TEXT("SetCell: Unbound legend"), row->GetModel().Caps[0].Label, unboundLabel);
    }
    TestEqual(TEXT("SetCell: text"), row->GetText(), SOLMenuContent::FormatBindings(TEXT("@Fire"), keys));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapRowWrapAndColorTest, "SOLTest.KeyCapRow.WrapWidthAndColor",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The wrap width and the legend colour read back as set, and setting them changes neither the caps nor the text
bool FSOLKeyCapRowWrapAndColorTest::RunTest(const FString& /*parameters*/)
{
    USOLKeyCapRow* const row = KeyCapRowTestNew();
    if (!TestNotNull(TEXT("the row was created"), row))
    {
        return true;
    }
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    const TCHAR* cell = SOLMenuText::CONTROL_ROWS[0].Bindings;
    row->SetCell(cell, keys);
    const FString text = row->GetText();
    const FSOLKeyCapModel model = row->GetModel();

    const float widths[] = { 230.0f, 123.5f, 0.0f, 200.0f };
    for (const float width : widths)
    {
        row->SetWrapWidth(width);
        TestEqual(FString::Printf(TEXT("wrap width %f reads back"), width), row->GetWrapWidth(), width);
    }
    TestEqual(TEXT("wrap width: text unchanged"), row->GetText(), text);
    KeyCapRowTestCheckSameModel(*this, TEXT("wrap width: model unchanged"), row->GetModel(), model);

    row->SetLegendColor(SOLSolidStyle::WarningColor());
    TestTrue(TEXT("the warning colour reads back"), KeyCapRowTestSameColor(row->GetLegendColor(),
        SOLSolidStyle::WarningColor()));
    row->SetLegendColor(SOLSolidStyle::TextColor());
    TestTrue(TEXT("the text colour reads back"), KeyCapRowTestSameColor(row->GetLegendColor(),
        SOLSolidStyle::TextColor()));
    TestEqual(TEXT("colour: text unchanged"), row->GetText(), text);
    KeyCapRowTestCheckSameModel(*this, TEXT("colour: model unchanged"), row->GetModel(), model);

    // A new cell keeps the wrap width and the colour
    row->SetWrapWidth(150.0f);
    row->SetLegendColor(SOLSolidStyle::WarningColor());
    row->SetBinding(KEYCAPROWTEST_FIRE, keys);
    TestEqual(TEXT("SetBinding keeps the wrap width"), row->GetWrapWidth(), 150.0f);
    TestTrue(TEXT("SetBinding keeps the colour"), KeyCapRowTestSameColor(row->GetLegendColor(),
        SOLSolidStyle::WarningColor()));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyCapRowRefreshControlsTest, "SOLTest.KeyCapRow.RefreshControlsPage",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// RefreshControlsPage sets each non-null cell to its Controls row's cell at the live keys; a null cell is skipped, a
// cell past the rows is left alone, a short array refreshes only its cells, an empty one nothing
bool FSOLKeyCapRowRefreshControlsTest::RunTest(const FString& /*parameters*/)
{
    if (!TestTrue(TEXT("there are at least two Controls rows"), KEYCAPROWTEST_ROW_COUNT >= 2))
    {
        return true;
    }
    const TArray<FString> rebound = KeyCapRowTestReboundKeys();
    const int32 nullIndex = KEYCAPROWTEST_ROW_COUNT / 2;

    // One cell per row with a null in the middle, plus one past the rows
    TArray<TObjectPtr<USOLKeyCapRow>> cells;
    for (int32 index = 0; index <= KEYCAPROWTEST_ROW_COUNT; ++index)
    {
        cells.Add(index == nullIndex ? nullptr : KeyCapRowTestNew());
    }
    if (cells[0] != nullptr)
    {
        cells[0]->SetMessage(FString(SOLMenuText::SETTINGS_KEY_WAITING));
    }
    SOLMenuContent::RefreshControlsPage(cells, rebound);
    for (int32 index = 0; index < KEYCAPROWTEST_ROW_COUNT; ++index)
    {
        if (index == nullIndex)
        {
            TestTrue(TEXT("the null slot stayed null"), cells[index] == nullptr);
            continue;
        }
        const TCHAR* bindings = SOLMenuText::CONTROL_ROWS[index].Bindings;
        if (TestNotNull(FString::Printf(TEXT("cell %d exists"), index), cells[index].Get()))
        {
            TestEqual(FString::Printf(TEXT("cell %d '%s' text"), index, bindings), cells[index]->GetText(),
                SOLMenuContent::FormatBindings(bindings, rebound));
            TestFalse(FString::Printf(TEXT("cell %d shows no message"), index), cells[index]->IsShowingMessage());
        }
    }
    const USOLKeyCapRow* const extra = cells[KEYCAPROWTEST_ROW_COUNT].Get();
    if (TestNotNull(TEXT("the extra cell exists"), extra))
    {
        TestTrue(TEXT("the extra cell stayed empty"), extra->GetText().IsEmpty());
        TestEqual(TEXT("the extra cell has no caps"), extra->GetModel().Caps.Num(), 0);
    }

    // A second refresh at the defaults rewrites the cells
    const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
    SOLMenuContent::RefreshControlsPage(cells, defaults);
    for (int32 index = 0; index < KEYCAPROWTEST_ROW_COUNT; ++index)
    {
        if (cells[index] != nullptr)
        {
            TestEqual(FString::Printf(TEXT("cell %d at the defaults"), index), cells[index]->GetText(),
                SOLMenuContent::FormatBindings(SOLMenuText::CONTROL_ROWS[index].Bindings, defaults));
        }
    }

    // Fewer cells than rows: only those refresh
    TArray<TObjectPtr<USOLKeyCapRow>> shortCells;
    shortCells.Add(KeyCapRowTestNew());
    shortCells.Add(KeyCapRowTestNew());
    SOLMenuContent::RefreshControlsPage(shortCells, rebound);
    for (int32 index = 0; index < shortCells.Num(); ++index)
    {
        if (TestNotNull(FString::Printf(TEXT("short array cell %d exists"), index), shortCells[index].Get()))
        {
            TestEqual(FString::Printf(TEXT("short array cell %d text"), index), shortCells[index]->GetText(),
                SOLMenuContent::FormatBindings(SOLMenuText::CONTROL_ROWS[index].Bindings, rebound));
        }
    }

    // No cells: nothing to do, nothing breaks
    SOLMenuContent::RefreshControlsPage(TArray<TObjectPtr<USOLKeyCapRow>>(), rebound);
    return true;
}

#endif
