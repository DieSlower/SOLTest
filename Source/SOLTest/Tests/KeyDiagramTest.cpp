/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyDiagram.h"
#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a keyboard diagram model (Docs/SDDs/33-player-documentation.md sections 7 and 10,
// D13, D15, D17-D19, D29), written against the header contract (Menu/SOLKeyDiagram.h) before the production code
// existed: which Controls actions sit on each diagram key at the live keys, the primary tint and short label, the "+N"
// count, the lock mark, the overflow and unbound lists, the ten-colour area palette and the no-reallocation rebuild.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    using SOLKeyBindings::EKeyChord;

    constexpr int32 KEYDIAGRAMTEST_DROP = static_cast<int32>(SOLKeyBindings::EBinding::DropTarget);
    constexpr int32 KEYDIAGRAMTEST_WARP_RESET = static_cast<int32>(SOLKeyBindings::EBinding::WarpReset);
    constexpr int32 KEYDIAGRAMTEST_MATCH_LOCK = static_cast<int32>(SOLKeyBindings::EBinding::MatchLock);
    constexpr int32 KEYDIAGRAMTEST_OPEN_MAP = static_cast<int32>(SOLKeyBindings::EBinding::OpenMap);
    constexpr int32 KEYDIAGRAMTEST_OVERLAY_MASTER = static_cast<int32>(SOLKeyBindings::EBinding::OverlayMaster);
    constexpr int32 KEYDIAGRAMTEST_FIRE = static_cast<int32>(SOLKeyBindings::EBinding::Fire);
    constexpr int32 KEYDIAGRAMTEST_AREA_COUNT = static_cast<int32>(ESOLControlArea::Count);
    constexpr float KEYDIAGRAMTEST_MIN_COLOR_DISTANCE = 48.0f;  // sRGB 0-255 Euclidean, pairwise (D17)

    // The ten areas as CONTROL_ROWS writes them, in ESOLControlArea order
    const TCHAR* const KEYDIAGRAMTEST_AREA_NAMES[] = { TEXT("Flight"), TEXT("Targeting"), TEXT("Combat"), TEXT("Time"),
        TEXT("Overlays"), TEXT("HUD"), TEXT("Map"), TEXT("Routes"), TEXT("Menu"), TEXT("Demo") };

    // The user's area colours (D17, D29), in ESOLControlArea order
    const FColor KEYDIAGRAMTEST_AREA_COLORS[] = { FColor(0x28, 0xD2, 0xF0), FColor(0xF2, 0xC9, 0x4C),
        FColor(0xE5, 0x53, 0x3D), FColor(0x9B, 0x7B, 0xFF), FColor(0x4C, 0xD0, 0x7D), FColor(0x5B, 0x8C, 0xFF),
        FColor(0xFF, 0x8F, 0xB1), FColor(0xD9, 0x8A, 0x5B), FColor(0x9F, 0xD6, 0xD0), FColor(0xE8, 0xEE, 0xF2) };

    // The numpad keys the diagram carries (D19)
    const TCHAR* const KEYDIAGRAMTEST_NUMPAD[] = { TEXT("NumPadZero"), TEXT("NumPadOne"), TEXT("NumPadTwo"),
        TEXT("NumPadThree"), TEXT("NumPadFour"), TEXT("NumPadFive"), TEXT("NumPadSix"), TEXT("NumPadSeven"),
        TEXT("NumPadEight"), TEXT("NumPadNine"), TEXT("Decimal"), TEXT("Add"), TEXT("Subtract"), TEXT("Multiply"),
        TEXT("Divide"), TEXT("NumLock") };

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two possibly null strings are equal (null reads as empty)
    bool KeyDiagramTestSame(const TCHAR* a, const TCHAR* b)
    {
        return FCString::Strcmp(a != nullptr ? a : TEXT(""), b != nullptr ? b : TEXT("")) == 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the model key of an engine key name on the layout (FindKey), or nullptr
    const FSOLDiagramKey* KeyDiagramTestKey(const FSOLKeyDiagramModel& model, const TCHAR* keyName)
    {
        const int32 index = SOLKeyboardLayout::FindKey(keyName);
        return model.Keys.IsValidIndex(index) ? &model.Keys[index] : nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a diagram key's assignment range lies inside the model's assignments
    bool KeyDiagramTestRangeValid(const FSOLKeyDiagramModel& model, const FSOLDiagramKey& key)
    {
        return key.FirstAssignment >= 0 && key.AssignmentCount >= 0
            && key.FirstAssignment + key.AssignmentCount <= model.Assignments.Num();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many of a key's assignments match a binding, or a row when binding is INDEX_NONE
    int32 KeyDiagramTestCount(const FSOLKeyDiagramModel& model, const FSOLDiagramKey& key, const int32 binding,
        const int32 row)
    {
        if (!KeyDiagramTestRangeValid(model, key))
        {
            return 0;
        }
        int32 count = 0;
        for (int32 index = key.FirstAssignment; index < key.FirstAssignment + key.AssignmentCount; ++index)
        {
            const FSOLKeyAssignment& assignment = model.Assignments[index];
            const bool bMatch = binding != INDEX_NONE ? assignment.Binding == binding
                : (assignment.Row == row && assignment.Binding == INDEX_NONE);
            count += bMatch ? 1 : 0;
        }
        return count;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the assignment of a binding on a key, or nullptr
    const FSOLKeyAssignment* KeyDiagramTestFind(const FSOLKeyDiagramModel& model, const FSOLDiagramKey& key,
        const int32 binding)
    {
        if (!KeyDiagramTestRangeValid(model, key))
        {
            return nullptr;
        }
        for (int32 index = key.FirstAssignment; index < key.FirstAssignment + key.AssignmentCount; ++index)
        {
            if (model.Assignments[index].Binding == binding)
            {
                return &model.Assignments[index];
            }
        }
        return nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many assignments of a binding the model holds over the keys that are not mirrors and the overflow
    int32 KeyDiagramTestBindingTotal(const FSOLKeyDiagramModel& model, const int32 binding)
    {
        const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
        int32 total = 0;
        for (int32 index = 0; index < model.Keys.Num(); ++index)
        {
            if (!layout.IsValidIndex(index) || !layout[index].bMirror)
            {
                total += KeyDiagramTestCount(model, model.Keys[index], binding, INDEX_NONE);
            }
        }
        for (const FSOLDiagramKey& key : model.Overflow)
        {
            total += KeyDiagramTestCount(model, key, binding, INDEX_NONE);
        }
        return total;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the chord a fixed '+' chord's first key gives: Shift, RightShift, Alt (either side) or Control (either
    // side) for a modifier; None for a single key or a first key that is no modifier
    EKeyChord KeyDiagramTestFixedChord(const TArray<FString>& chordKeys)
    {
        if (chordKeys.Num() < 2)
        {
            return EKeyChord::None;
        }
        const FString& modifier = chordKeys[0];
        if (modifier.Equals(TEXT("LeftShift"), ESearchCase::IgnoreCase))
        {
            return EKeyChord::Shift;
        }
        if (modifier.Equals(TEXT("RightShift"), ESearchCase::IgnoreCase))
        {
            return EKeyChord::RightShift;
        }
        if (modifier.Equals(TEXT("LeftControl"), ESearchCase::IgnoreCase)
            || modifier.Equals(TEXT("RightControl"), ESearchCase::IgnoreCase))
        {
            return EKeyChord::Control;
        }
        if (modifier.Equals(TEXT("LeftAlt"), ESearchCase::IgnoreCase)
            || modifier.Equals(TEXT("RightAlt"), ESearchCase::IgnoreCase))
        {
            return EKeyChord::Alt;
        }
        return EKeyChord::None;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the sRGB 0-255 Euclidean distance between two colours
    float KeyDiagramTestColorDistance(const FColor& a, const FColor& b)
    {
        const float dr = static_cast<float>(a.R) - static_cast<float>(b.R);
        const float dg = static_cast<float>(a.G) - static_cast<float>(b.G);
        const float db = static_cast<float>(a.B) - static_cast<float>(b.B);
        return std::sqrt(dr * dr + dg * dg + db * db);
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts one diagram key's derived fields (primary, badge, area, label, lock) agree with its assignments
    void KeyDiagramTestCheckKey(FAutomationTestBase& test, const FSOLKeyDiagramModel& model, const FSOLDiagramKey& key,
        const FString& what)
    {
        test.TestTrue(what + TEXT(": assignment range valid"), KeyDiagramTestRangeValid(model, key));
        if (!KeyDiagramTestRangeValid(model, key))
        {
            return;
        }
        if (key.AssignmentCount == 0)
        {
            test.TestEqual(what + TEXT(": free key has no primary"), key.Primary, static_cast<int32>(INDEX_NONE));
            test.TestEqual(what + TEXT(": free key has no badge"), key.ExtraCount, 0);
            test.TestTrue(what + TEXT(": free key has no area"), key.Area == ESOLControlArea::Count);
            test.TestFalse(what + TEXT(": free key is not locked"), key.bLocked);
            test.TestTrue(what + TEXT(": free key has no label"), KeyDiagramTestSame(key.ShortLabel, TEXT("")));
            return;
        }
        const int32 last = key.FirstAssignment + key.AssignmentCount;
        test.TestTrue(what + TEXT(": primary in its range"), key.Primary >= key.FirstAssignment && key.Primary < last);
        test.TestEqual(what + TEXT(": badge counts the further actions"), key.ExtraCount, key.AssignmentCount - 1);
        if (!(key.Primary >= key.FirstAssignment && key.Primary < last))
        {
            return;
        }
        const FSOLKeyAssignment& primary = model.Assignments[key.Primary];
        test.TestTrue(what + TEXT(": the primary's area"), key.Area == primary.Area);
        test.TestTrue(what + TEXT(": the primary's label"), KeyDiagramTestSame(key.ShortLabel, primary.ShortLabel));

        // The plain in-flight action if there is one, else the first row that names the key (D18)
        bool bAnyPlainInFlight = false;
        bool bAllFixed = true;
        int32 firstRow = MAX_int32;
        for (int32 index = key.FirstAssignment; index < last; ++index)
        {
            const FSOLKeyAssignment& assignment = model.Assignments[index];
            bAnyPlainInFlight |= assignment.Chord == EKeyChord::None && assignment.bInFlight;
            bAllFixed &= assignment.bFixed;
            firstRow = FMath::Min(firstRow, assignment.Row);
        }
        if (bAnyPlainInFlight)
        {
            test.TestTrue(what + TEXT(": primary is a plain in-flight action"),
                primary.Chord == EKeyChord::None && primary.bInFlight);
        }
        else
        {
            test.TestEqual(what + TEXT(": primary is the first row"), primary.Row, firstRow);
        }
        test.TestTrue(what + TEXT(": locked when every action is fixed"), key.bLocked == bAllFixed);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramBindingsTest, "SOLTest.KeyDiagram.EveryBindingAssigned",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At the default keys there is one model key per layout key, and every binding sits on exactly one key: its own
bool FSOLKeyDiagramBindingsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TArray<FString> keys = DefaultKeys();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);

    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    TestEqual(TEXT("one model key per layout key"), model.Keys.Num(), layout.Num());
    for (int32 index = 0; index < FMath::Min(model.Keys.Num(), layout.Num()); ++index)
    {
        TestEqual(FString::Printf(TEXT("key %d named as the layout"), index), model.Keys[index].KeyName,
            FString(layout[index].KeyName));
    }
    TestEqual(TEXT("nothing off the layout"), model.Overflow.Num(), 0);
    TestEqual(TEXT("nothing unbound"), model.Unbound.Num(), 0);
    TestTrue(TEXT("the model holds assignments"), model.Assignments.Num() > 0);

    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        const FBindingDef& def = GetDef(binding);
        TestEqual(FString::Printf(TEXT("%s assigned exactly once"), def.Id), KeyDiagramTestBindingTotal(model, binding),
            1);
        const int32 keyIndex = SOLKeyDiagram::KeyOfBinding(model, binding);
        TestEqual(FString::Printf(TEXT("%s on its default key"), def.Id), keyIndex,
            SOLKeyboardLayout::FindKey(def.DefaultKey));
        if (model.Keys.IsValidIndex(keyIndex))
        {
            const FSOLKeyAssignment* assignment = KeyDiagramTestFind(model, model.Keys[keyIndex], binding);
            TestTrue(FString::Printf(TEXT("%s's key holds it"), def.Id), assignment != nullptr);
            if (assignment != nullptr)
            {
                TestTrue(FString::Printf(TEXT("%s chord"), def.Id), assignment->Chord == GetChord(binding));
                TestFalse(FString::Printf(TEXT("%s is rebindable"), def.Id), assignment->bFixed);
                TestTrue(FString::Printf(TEXT("%s short label"), def.Id),
                    KeyDiagramTestSame(assignment->ShortLabel, SOLKeyDiagram::BindingShortLabel(binding)));
            }
        }
    }
    TestEqual(TEXT("KeyOfBinding out of range"), SOLKeyDiagram::KeyOfBinding(model, -1),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("KeyOfBinding past the end"), SOLKeyDiagram::KeyOfBinding(model, BINDING_COUNT),
        static_cast<int32>(INDEX_NONE));

    // The ranges partition the assignments; a mirror shares its main key's range
    int32 total = 0;
    for (int32 index = 0; index < model.Keys.Num(); ++index)
    {
        const FSOLDiagramKey& key = model.Keys[index];
        TestTrue(FString::Printf(TEXT("key %d range valid"), index), KeyDiagramTestRangeValid(model, key));
        if (layout.IsValidIndex(index) && layout[index].bMirror)
        {
            const int32 main = SOLKeyboardLayout::FindKey(layout[index].KeyName);
            if (model.Keys.IsValidIndex(main))
            {
                TestEqual(TEXT("mirror shares the main key's range"), key.FirstAssignment,
                    model.Keys[main].FirstAssignment);
                TestEqual(TEXT("mirror shares the main key's count"), key.AssignmentCount,
                    model.Keys[main].AssignmentCount);
            }
            continue;
        }
        total += key.AssignmentCount;
    }
    for (const FSOLDiagramKey& key : model.Overflow)
    {
        total += key.AssignmentCount;
    }
    TestEqual(TEXT("every assignment belongs to one key"), total, model.Assignments.Num());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramRowsTest, "SOLTest.KeyDiagram.EveryRowKeyAssigned",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every key a Controls row names holds that row: a binding on its live key, a fixed chord on its last key (locked, with
// its modifiers as the chord); each assignment carries the row's area, whether it works in flight and its short label
bool FSOLKeyDiagramRowsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const TArray<FString> keys = DefaultKeys();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);

    for (int32 row = 0; row < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)); ++row)
    {
        const SOLMenuText::FControlRow& controlRow = SOLMenuText::CONTROL_ROWS[row];
        const ESOLControlArea area = SOLKeyDiagram::AreaFromName(controlRow.Area);
        TArray<FString> alternatives;
        FString(controlRow.Bindings).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
        for (const FString& alternative : alternatives)
        {
            const FString what = FString::Printf(TEXT("row %d '%s'"), row, *alternative);
            const FSOLKeyAssignment* assignment = nullptr;
            FString keyName;
            bool bFixed = false;
            EKeyChord chord = EKeyChord::None;
            if (alternative.StartsWith(TEXT("@")))
            {
                const int32 binding = FindById(alternative.RightChop(1));
                if (binding == INDEX_NONE)
                {
                    continue;
                }
                keyName = keys[binding];
                chord = GetChord(binding);
                if (const FSOLDiagramKey* key = KeyDiagramTestKey(model, *keyName))
                {
                    assignment = KeyDiagramTestFind(model, *key, binding);
                }
            }
            else
            {
                TArray<FString> chordKeys;
                alternative.ParseIntoArray(chordKeys, SOLMenuText::BINDING_CHORD_DELIMITER);
                if (chordKeys.Num() == 0)
                {
                    continue;
                }
                keyName = chordKeys.Last();
                bFixed = true;
                chord = KeyDiagramTestFixedChord(chordKeys);
                const FSOLDiagramKey* key = KeyDiagramTestKey(model, *keyName);
                if (key != nullptr && KeyDiagramTestRangeValid(model, *key))
                {
                    // The fixed assignment of this row on its last key
                    for (int32 index = key->FirstAssignment; index < key->FirstAssignment + key->AssignmentCount;
                        ++index)
                    {
                        const FSOLKeyAssignment& candidate = model.Assignments[index];
                        if (candidate.Row == row && candidate.bFixed && candidate.Chord == chord)
                        {
                            assignment = &candidate;
                        }
                    }
                }
            }
            TestTrue(what + TEXT(": its key holds the row"), assignment != nullptr);
            if (assignment == nullptr)
            {
                continue;
            }
            TestEqual(what + TEXT(": row"), assignment->Row, row);
            TestTrue(what + TEXT(": area"), assignment->Area == area);
            TestTrue(what + TEXT(": fixed flag"), assignment->bFixed == bFixed);
            TestTrue(what + TEXT(": chord"), assignment->Chord == chord);
            TestTrue(what + TEXT(": in flight unless a map row"),
                assignment->bInFlight == (area != ESOLControlArea::Map));
            const TCHAR* expectedLabel = bFixed ? SOLKeyDiagram::FixedKeyShortLabel(keyName)
                : SOLKeyDiagram::BindingShortLabel(assignment->Binding);
            TestTrue(what + TEXT(": short label"), KeyDiagramTestSame(assignment->ShortLabel, expectedLabel));
            if (bFixed)
            {
                TestEqual(what + TEXT(": a fixed key names no binding"), assignment->Binding,
                    static_cast<int32>(INDEX_NONE));
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramOKeyTest, "SOLTest.KeyDiagram.OKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// O holds four actions with the four chords; the plain OverlayMaster is its primary and the badge shows +3
bool FSOLKeyDiagramOKeyTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    const FSOLDiagramKey* key = KeyDiagramTestKey(model, TEXT("O"));
    TestTrue(TEXT("O on the diagram"), key != nullptr);
    if (key == nullptr || !KeyDiagramTestRangeValid(model, *key))
    {
        return true;
    }
    TestEqual(TEXT("O holds four actions"), key->AssignmentCount, 4);
    bool bChords[5] = { false, false, false, false, false };
    for (int32 index = key->FirstAssignment; index < key->FirstAssignment + key->AssignmentCount; ++index)
    {
        const int32 chord = static_cast<int32>(model.Assignments[index].Chord);
        if (chord >= 0 && chord < 5)
        {
            bChords[chord] = true;
        }
    }
    TestTrue(TEXT("O plain"), bChords[static_cast<int32>(EKeyChord::None)]);
    TestTrue(TEXT("Alt+O"), bChords[static_cast<int32>(EKeyChord::Alt)]);
    TestTrue(TEXT("Ctrl+O"), bChords[static_cast<int32>(EKeyChord::Control)]);
    TestTrue(TEXT("RShift+O"), bChords[static_cast<int32>(EKeyChord::RightShift)]);
    TestTrue(TEXT("primary valid"), model.Assignments.IsValidIndex(key->Primary));
    if (model.Assignments.IsValidIndex(key->Primary))
    {
        TestEqual(TEXT("primary is OverlayMaster"), model.Assignments[key->Primary].Binding,
            KEYDIAGRAMTEST_OVERLAY_MASTER);
    }
    TestEqual(TEXT("+3 badge"), key->ExtraCount, 3);
    TestTrue(TEXT("tinted Overlays"), key->Area == ESOLControlArea::Overlays);
    TestTrue(TEXT("OverlayMaster's label"), KeyDiagramTestSame(key->ShortLabel,
        SOLKeyDiagram::BindingShortLabel(KEYDIAGRAMTEST_OVERLAY_MASTER)));
    TestFalse(TEXT("O is rebindable"), key->bLocked);
    KeyDiagramTestCheckKey(*this, model, *key, TEXT("O"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramMKeyTest, "SOLTest.KeyDiagram.MKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// M holds MatchLock (primary), Shift+M OpenMap and the map's Move mode row
bool FSOLKeyDiagramMKeyTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    const FSOLDiagramKey* key = KeyDiagramTestKey(model, TEXT("M"));
    TestTrue(TEXT("M on the diagram"), key != nullptr);
    if (key == nullptr || !KeyDiagramTestRangeValid(model, *key))
    {
        return true;
    }
    TestEqual(TEXT("M holds three actions"), key->AssignmentCount, 3);
    const FSOLKeyAssignment* matchLock = KeyDiagramTestFind(model, *key, KEYDIAGRAMTEST_MATCH_LOCK);
    const FSOLKeyAssignment* openMap = KeyDiagramTestFind(model, *key, KEYDIAGRAMTEST_OPEN_MAP);
    TestTrue(TEXT("MatchLock on M"), matchLock != nullptr && matchLock->Chord == EKeyChord::None);
    TestTrue(TEXT("Shift+M opens the map"), openMap != nullptr && openMap->Chord == EKeyChord::Shift);

    // The map's J|M|P row: M is the Move mode
    int32 moveRow = INDEX_NONE;
    for (int32 row = 0; row < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS)); ++row)
    {
        if (FCString::Strcmp(SOLMenuText::CONTROL_ROWS[row].Bindings, TEXT("J|M|P")) == 0)
        {
            moveRow = row;
        }
    }
    TestTrue(TEXT("the J|M|P row exists"), moveRow != INDEX_NONE);
    TestEqual(TEXT("the Move row on M"), KeyDiagramTestCount(model, *key, INDEX_NONE, moveRow), 1);
    for (int32 index = key->FirstAssignment; index < key->FirstAssignment + key->AssignmentCount; ++index)
    {
        const FSOLKeyAssignment& assignment = model.Assignments[index];
        if (assignment.Row == moveRow)
        {
            TestTrue(TEXT("Move is fixed"), assignment.bFixed);
            TestFalse(TEXT("Move is a map action"), assignment.bInFlight);
            TestTrue(TEXT("Move is the Map area"), assignment.Area == ESOLControlArea::Map);
            TestTrue(TEXT("Move label"), KeyDiagramTestSame(assignment.ShortLabel,
                SOLKeyDiagram::FixedKeyShortLabel(TEXT("M"))));
        }
    }
    TestTrue(TEXT("primary valid"), model.Assignments.IsValidIndex(key->Primary));
    if (model.Assignments.IsValidIndex(key->Primary))
    {
        TestEqual(TEXT("primary is MatchLock"), model.Assignments[key->Primary].Binding, KEYDIAGRAMTEST_MATCH_LOCK);
    }
    TestEqual(TEXT("+2 badge"), key->ExtraCount, 2);
    TestTrue(TEXT("tinted Targeting"), key->Area == ESOLControlArea::Targeting);
    TestFalse(TEXT("M is not locked"), key->bLocked);
    KeyDiagramTestCheckKey(*this, model, *key, TEXT("M"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramPrimaryTest, "SOLTest.KeyDiagram.PrimaryAndBadge",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every key reports its primary's area and label and a "+N" of the further actions; free keys are plain (D18)
bool FSOLKeyDiagramPrimaryTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    int32 shared = 0;
    int32 free = 0;
    for (int32 index = 0; index < model.Keys.Num(); ++index)
    {
        const FSOLDiagramKey& key = model.Keys[index];
        KeyDiagramTestCheckKey(*this, model, key, FString::Printf(TEXT("key %d '%s'"), index, *key.KeyName));
        shared += key.AssignmentCount > 1 ? 1 : 0;
        free += key.AssignmentCount == 0 ? 1 : 0;
    }
    TestTrue(TEXT("several keys carry more than one action"), shared >= 5);
    TestTrue(TEXT("most of the keyboard is free"), free > 0);

    // Escape: the map's close (not in flight) and the menu (in flight); the menu row is the primary
    const FSOLDiagramKey* escape = KeyDiagramTestKey(model, TEXT("Escape"));
    TestTrue(TEXT("Escape on the diagram"), escape != nullptr);
    if (escape != nullptr)
    {
        TestEqual(TEXT("Escape holds two rows"), escape->AssignmentCount, 2);
        TestTrue(TEXT("Escape tinted Menu"), escape->Area == ESOLControlArea::Menu);
        TestEqual(TEXT("Escape +1"), escape->ExtraCount, 1);
    }

    // RMB: four map drags and clicks, none in flight: the first row is the primary
    const FSOLDiagramKey* rightMouse = KeyDiagramTestKey(model, TEXT("RightMouseButton"));
    TestTrue(TEXT("RMB on the diagram"), rightMouse != nullptr);
    if (rightMouse != nullptr)
    {
        TestEqual(TEXT("RMB holds four rows"), rightMouse->AssignmentCount, 4);
        TestTrue(TEXT("RMB tinted Map"), rightMouse->Area == ESOLControlArea::Map);
        TestTrue(TEXT("RMB labelled Orbit"), KeyDiagramTestSame(rightMouse->ShortLabel,
            SOLKeyDiagram::FixedKeyShortLabel(TEXT("RightMouseButton"))));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramLockTest, "SOLTest.KeyDiagram.FixedKeysLocked",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Keys with only fixed actions carry the lock mark (Esc, Ctrl+F1, Ctrl+F2, the mouse look, the map's fixed keys); keys
// with a rebindable action do not (D19)
bool FSOLKeyDiagramLockTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    const TCHAR* locked[] = { TEXT("Escape"), TEXT("F1"), TEXT("F2"), TEXT("Mouse2D"), TEXT("Enter"), TEXT("Delete"),
        TEXT("RightMouseButton") };
    for (const TCHAR* keyName : locked)
    {
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName);
        TestTrue(FString::Printf(TEXT("%s on the diagram"), keyName), key != nullptr);
        if (key != nullptr)
        {
            TestTrue(FString::Printf(TEXT("%s has actions"), keyName), key->AssignmentCount > 0);
            TestTrue(FString::Printf(TEXT("%s locked"), keyName), key->bLocked);
        }
    }
    const TCHAR* open[] = { TEXT("O"), TEXT("M"), TEXT("X"), TEXT("J"), TEXT("P"), TEXT("LeftMouseButton"),
        TEXT("LeftShift"), TEXT("MouseWheelAxis"), TEXT("W") };
    for (const TCHAR* keyName : open)
    {
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName);
        TestTrue(FString::Printf(TEXT("%s on the diagram"), keyName), key != nullptr);
        if (key != nullptr)
        {
            TestFalse(FString::Printf(TEXT("%s not locked"), keyName), key->bLocked);
        }
    }

    // Ctrl+F1: the Menu area, the Control chord, its fixed label
    const FSOLDiagramKey* f1 = KeyDiagramTestKey(model, TEXT("F1"));
    if (f1 != nullptr && model.Assignments.IsValidIndex(f1->Primary))
    {
        const FSOLKeyAssignment& assignment = model.Assignments[f1->Primary];
        TestTrue(TEXT("Ctrl+F1 is fixed"), assignment.bFixed);
        TestTrue(TEXT("Ctrl+F1 chord"), assignment.Chord == EKeyChord::Control);
        TestTrue(TEXT("Ctrl+F1 area"), assignment.Area == ESOLControlArea::Menu);
        TestTrue(TEXT("Ctrl+F1 label"),
            KeyDiagramTestSame(f1->ShortLabel, SOLKeyDiagram::FixedKeyShortLabel(TEXT("F1"))));
    }
    else
    {
        AddError(TEXT("F1 has no primary action"));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramRebindTest, "SOLTest.KeyDiagram.Rebind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A rebind moves the action to its new key (with its chord) and frees the old key
bool FSOLKeyDiagramRebindTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyDiagramModel defaults;
    SOLKeyDiagram::Build(keys, defaults);

    // DropTarget from G to K: G becomes free, K holds it
    keys[KEYDIAGRAMTEST_DROP] = TEXT("K");
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("the same number of actions"), model.Assignments.Num(), defaults.Assignments.Num());
    TestEqual(TEXT("DropTarget on K"), SOLKeyDiagram::KeyOfBinding(model, KEYDIAGRAMTEST_DROP),
        SOLKeyboardLayout::FindKey(TEXT("K")));
    const FSOLDiagramKey* oldKey = KeyDiagramTestKey(model, TEXT("G"));
    TestTrue(TEXT("G on the diagram"), oldKey != nullptr);
    if (oldKey != nullptr)
    {
        TestEqual(TEXT("G is free"), oldKey->AssignmentCount, 0);
        KeyDiagramTestCheckKey(*this, model, *oldKey, TEXT("G after the rebind"));
    }
    const FSOLDiagramKey* newKey = KeyDiagramTestKey(model, TEXT("K"));
    TestTrue(TEXT("K on the diagram"), newKey != nullptr);
    if (newKey != nullptr)
    {
        TestEqual(TEXT("K holds one action"), newKey->AssignmentCount, 1);
        TestTrue(TEXT("K holds DropTarget"), KeyDiagramTestFind(model, *newKey, KEYDIAGRAMTEST_DROP) != nullptr);
        TestTrue(TEXT("K tinted Combat"), newKey->Area == ESOLControlArea::Combat);
        KeyDiagramTestCheckKey(*this, model, *newKey, TEXT("K after the rebind"));
    }
    TestEqual(TEXT("DropTarget assigned once"), KeyDiagramTestBindingTotal(model, KEYDIAGRAMTEST_DROP), 1);

    // OpenMap (Shift+M) to N: N holds it with the Shift chord, M keeps MatchLock and Move
    keys = SOLKeyBindings::DefaultKeys();
    keys[KEYDIAGRAMTEST_OPEN_MAP] = TEXT("N");
    SOLKeyDiagram::Build(keys, model);
    const FSOLDiagramKey* n = KeyDiagramTestKey(model, TEXT("N"));
    if (n != nullptr)
    {
        const FSOLKeyAssignment* openMap = KeyDiagramTestFind(model, *n, KEYDIAGRAMTEST_OPEN_MAP);
        TestTrue(TEXT("Shift+N opens the map"), openMap != nullptr && openMap->Chord == EKeyChord::Shift);
    }
    else
    {
        AddError(TEXT("N not on the diagram"));
    }
    const FSOLDiagramKey* m = KeyDiagramTestKey(model, TEXT("M"));
    if (m != nullptr)
    {
        TestEqual(TEXT("M keeps two actions"), m->AssignmentCount, 2);
        TestTrue(TEXT("M no longer opens the map"), KeyDiagramTestFind(model, *m, KEYDIAGRAMTEST_OPEN_MAP) == nullptr);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramOverflowTest, "SOLTest.KeyDiagram.Overflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A binding on a key off the layout goes to the overflow list ("Other keys"), in first-row order, one entry per key
bool FSOLKeyDiagramOverflowTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("MediaPlayPause is off the layout"), SOLKeyboardLayout::FindKey(TEXT("MediaPlayPause")),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("F13 is off the layout"), SOLKeyboardLayout::FindKey(TEXT("F13")), static_cast<int32>(INDEX_NONE));

    // Two bindings on two off-layout keys: DropTarget's row comes before WarpReset's
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    keys[KEYDIAGRAMTEST_DROP] = TEXT("MediaPlayPause");
    keys[KEYDIAGRAMTEST_WARP_RESET] = TEXT("F13");
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("two keys off the layout"), model.Overflow.Num(), 2);
    TestEqual(TEXT("DropTarget not on the layout"), SOLKeyDiagram::KeyOfBinding(model, KEYDIAGRAMTEST_DROP),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("DropTarget still assigned once"), KeyDiagramTestBindingTotal(model, KEYDIAGRAMTEST_DROP), 1);
    TestEqual(TEXT("nothing unbound"), model.Unbound.Num(), 0);
    if (model.Overflow.Num() == 2)
    {
        TestEqual(TEXT("first-row order: MediaPlayPause"), model.Overflow[0].KeyName, FString(TEXT("MediaPlayPause")));
        TestEqual(TEXT("first-row order: F13"), model.Overflow[1].KeyName, FString(TEXT("F13")));
        for (const FSOLDiagramKey& key : model.Overflow)
        {
            TestEqual(FString::Printf(TEXT("%s holds one action"), *key.KeyName), key.AssignmentCount, 1);
            KeyDiagramTestCheckKey(*this, model, key, key.KeyName);
        }
        TestTrue(TEXT("MediaPlayPause holds DropTarget"),
            KeyDiagramTestFind(model, model.Overflow[0], KEYDIAGRAMTEST_DROP) != nullptr);
        TestTrue(TEXT("MediaPlayPause tinted Combat"), model.Overflow[0].Area == ESOLControlArea::Combat);
    }

    // Two bindings on one off-layout key share one entry with a +1
    keys = SOLKeyBindings::DefaultKeys();
    keys[KEYDIAGRAMTEST_DROP] = TEXT("F13");
    keys[KEYDIAGRAMTEST_WARP_RESET] = TEXT("F13");
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("one shared key off the layout"), model.Overflow.Num(), 1);
    if (model.Overflow.Num() == 1)
    {
        TestEqual(TEXT("F13 holds two actions"), model.Overflow[0].AssignmentCount, 2);
        TestEqual(TEXT("F13 +1"), model.Overflow[0].ExtraCount, 1);
        KeyDiagramTestCheckKey(*this, model, model.Overflow[0], TEXT("shared F13"));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramUnboundTest, "SOLTest.KeyDiagram.Unbound",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A binding with an empty key, or every binding of a short key array, goes to the unbound list; fixed keys stay
bool FSOLKeyDiagramUnboundTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    TArray<FString> keys = DefaultKeys();
    keys[KEYDIAGRAMTEST_DROP] = FString();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("one unbound binding"), model.Unbound.Num(), 1);
    if (model.Unbound.Num() == 1)
    {
        TestEqual(TEXT("DropTarget unbound"), model.Unbound[0], KEYDIAGRAMTEST_DROP);
    }
    TestEqual(TEXT("DropTarget on no key"), SOLKeyDiagram::KeyOfBinding(model, KEYDIAGRAMTEST_DROP),
        static_cast<int32>(INDEX_NONE));
    TestEqual(TEXT("DropTarget not assigned"), KeyDiagramTestBindingTotal(model, KEYDIAGRAMTEST_DROP), 0);
    TestEqual(TEXT("an unbound key is not overflow"), model.Overflow.Num(), 0);

    // A short key array: every binding unbound, once each; the fixed rows still sit on their keys
    SOLKeyDiagram::Build(TArray<FString>(), model);
    TestEqual(TEXT("short array: every binding unbound"), model.Unbound.Num(), BINDING_COUNT);
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        TestTrue(FString::Printf(TEXT("short array: %s listed"), GetDef(binding).Id), model.Unbound.Contains(binding));
        TestEqual(FString::Printf(TEXT("short array: %s on no key"), GetDef(binding).Id),
            KeyDiagramTestBindingTotal(model, binding), 0);
    }
    const FSOLDiagramKey* escape = KeyDiagramTestKey(model, TEXT("Escape"));
    TestTrue(TEXT("short array: Escape keeps its fixed rows"), escape != nullptr && escape->AssignmentCount == 2);
    TestEqual(TEXT("short array: one model key per layout key"), model.Keys.Num(), SOLKeyboardLayout::KeyCount());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramPaletteTest, "SOLTest.KeyDiagram.AreaPalette",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The ten Controls areas each map to their own colour, the user's values, all pairwise at least 48 apart (D17, D29)
bool FSOLKeyDiagramPaletteTest::RunTest(const FString& /*parameters*/)
{
    TestEqual(TEXT("ten areas"), KEYDIAGRAMTEST_AREA_COUNT, SOL::Help::AREA_COUNT);
    for (int32 area = 0; area < KEYDIAGRAMTEST_AREA_COUNT; ++area)
    {
        const ESOLControlArea value = static_cast<ESOLControlArea>(area);
        const FString name = KEYDIAGRAMTEST_AREA_NAMES[area];
        TestEqual(FString::Printf(TEXT("area %d name"), area), FString(SOLKeyDiagram::AreaName(value)), name);
        TestTrue(name + TEXT(" from its name"), SOLKeyDiagram::AreaFromName(*name) == value);
        const FColor color = SOLKeyDiagram::AreaColor(value);
        TestTrue(name + TEXT(" colour is the user's"), color == KEYDIAGRAMTEST_AREA_COLORS[area]);
        TestTrue(name + TEXT(" colour is the constant"), color == SOL::Help::AREA_COLORS[area]);
    }
    TestEqual(TEXT("Count has no name"), FString(SOLKeyDiagram::AreaName(ESOLControlArea::Count)), FString());
    TestTrue(TEXT("Count is dim"), SOLKeyDiagram::AreaColor(ESOLControlArea::Count) == SOL::SolidMenu::TEXT_DIM_COLOR);
    TestTrue(TEXT("names are case-sensitive"), SOLKeyDiagram::AreaFromName(TEXT("flight")) == ESOLControlArea::Count);
    TestTrue(TEXT("unknown area"), SOLKeyDiagram::AreaFromName(TEXT("Nowhere")) == ESOLControlArea::Count);

    // Pairwise distinct with a margin on the dark background
    for (int32 a = 0; a < KEYDIAGRAMTEST_AREA_COUNT; ++a)
    {
        for (int32 b = a + 1; b < KEYDIAGRAMTEST_AREA_COUNT; ++b)
        {
            const float distance = KeyDiagramTestColorDistance(
                SOLKeyDiagram::AreaColor(static_cast<ESOLControlArea>(a)),
                SOLKeyDiagram::AreaColor(static_cast<ESOLControlArea>(b)));
            TestTrue(FString::Printf(TEXT("%s and %s apart (%.1f)"), KEYDIAGRAMTEST_AREA_NAMES[a],
                KEYDIAGRAMTEST_AREA_NAMES[b], distance), distance >= KEYDIAGRAMTEST_MIN_COLOR_DISTANCE);
        }
    }

    // Every area of CONTROL_ROWS resolves, and the table uses all ten, each to its own colour
    TArray<ESOLControlArea> seen;
    for (const SOLMenuText::FControlRow& row : SOLMenuText::CONTROL_ROWS)
    {
        const ESOLControlArea area = SOLKeyDiagram::AreaFromName(row.Area);
        TestTrue(FString::Printf(TEXT("row area '%s' known"), row.Area), area != ESOLControlArea::Count);
        if (area != ESOLControlArea::Count)
        {
            seen.AddUnique(area);
        }
    }
    TestEqual(TEXT("CONTROL_ROWS uses ten areas"), seen.Num(), KEYDIAGRAMTEST_AREA_COUNT);
    TArray<FColor> colors;
    for (const ESOLControlArea area : seen)
    {
        colors.AddUnique(SOLKeyDiagram::AreaColor(area));
    }
    TestEqual(TEXT("each area its own colour"), colors.Num(), seen.Num());
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramLabelsTest, "SOLTest.KeyDiagram.ShortLabels",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every binding and every fixed key of the Controls rows has a short label of at most SHORT_LABEL_MAX_CHARS
bool FSOLKeyDiagramLabelsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOLKeyBindings;
    const int32 maxChars = SOL::Help::SHORT_LABEL_MAX_CHARS;
    TestEqual(TEXT("one short label per binding"), static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::KEY_SHORT_LABELS)),
        BINDING_COUNT);
    for (int32 binding = 0; binding < BINDING_COUNT; ++binding)
    {
        const FString label = SOLKeyDiagram::BindingShortLabel(binding);
        const FString what = FString::Printf(TEXT("%s label '%s'"), GetDef(binding).Id, *label);
        TestFalse(what + TEXT(": exists"), label.IsEmpty());
        TestTrue(what + TEXT(": short"), label.Len() <= maxChars);
        if (binding < static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::KEY_SHORT_LABELS)))
        {
            TestEqual(what + TEXT(": from the table"), label, FString(SOLMenuText::KEY_SHORT_LABELS[binding].Label));
            TestEqual(what + TEXT(": the table row names its binding"),
                FString(SOLMenuText::KEY_SHORT_LABELS[binding].BindingId), FString(GetDef(binding).Id));
        }
    }
    TestEqual(TEXT("out-of-range binding"), FString(SOLKeyDiagram::BindingShortLabel(-1)), FString());
    TestEqual(TEXT("binding past the end"), FString(SOLKeyDiagram::BindingShortLabel(BINDING_COUNT)), FString());

    // The fixed-key table
    for (const SOLMenuText::FFixedKeyShortLabel& entry : SOLMenuText::FIXED_KEY_SHORT_LABELS)
    {
        const FString label = entry.Label;
        TestFalse(FString::Printf(TEXT("%s label exists"), entry.KeyName), label.IsEmpty());
        TestTrue(FString::Printf(TEXT("%s label short"), entry.KeyName), label.Len() <= maxChars);
        TestEqual(FString::Printf(TEXT("%s label looked up"), entry.KeyName),
            FString(SOLKeyDiagram::FixedKeyShortLabel(entry.KeyName)), label);
        TestEqual(FString::Printf(TEXT("%s label case-insensitive"), entry.KeyName),
            FString(SOLKeyDiagram::FixedKeyShortLabel(FString(entry.KeyName).ToLower())), label);
    }
    TestEqual(TEXT("no label for a key without a fixed row"), FString(SOLKeyDiagram::FixedKeyShortLabel(TEXT("K"))),
        FString());

    // Every fixed key a Controls row names (a chord's last key) has a label
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
            if (chordKeys.Num() > 0)
            {
                TestFalse(FString::Printf(TEXT("fixed key %s has a label"), *chordKeys.Last()),
                    FString(SOLKeyDiagram::FixedKeyShortLabel(chordKeys.Last())).IsEmpty());
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramNumpadTest, "SOLTest.KeyDiagram.NumpadKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The numpad keys are diagram keys (free at the defaults), and a binding moved onto one shows there (D19)
bool FSOLKeyDiagramNumpadTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    for (const TCHAR* keyName : KEYDIAGRAMTEST_NUMPAD)
    {
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName);
        TestTrue(FString::Printf(TEXT("%s on the diagram"), keyName), key != nullptr);
        if (key != nullptr)
        {
            TestEqual(FString::Printf(TEXT("%s named"), keyName), key->KeyName, FString(keyName));
            TestEqual(FString::Printf(TEXT("%s free at the defaults"), keyName), key->AssignmentCount, 0);
        }
    }
    keys[KEYDIAGRAMTEST_FIRE] = TEXT("NumPadFive");
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("Fire on the numpad 5"), SOLKeyDiagram::KeyOfBinding(model, KEYDIAGRAMTEST_FIRE),
        SOLKeyboardLayout::FindKey(TEXT("NumPadFive")));
    TestEqual(TEXT("numpad 5 is not overflow"), model.Overflow.Num(), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramReuseTest, "SOLTest.KeyDiagram.NoReallocation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A second Build into the same model (same keys, or a rebind between layout keys) keeps every buffer (D13)
bool FSOLKeyDiagramReuseTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    TestTrue(TEXT("first build made assignments"), model.Assignments.Num() > 0);
    const void* assignmentsData = model.Assignments.GetData();
    const SIZE_T assignmentsSize = model.Assignments.GetAllocatedSize();
    const void* keysData = model.Keys.GetData();
    const SIZE_T keysSize = model.Keys.GetAllocatedSize();
    const void* overflowData = model.Overflow.GetData();
    const SIZE_T overflowSize = model.Overflow.GetAllocatedSize();
    const void* unboundData = model.Unbound.GetData();
    const SIZE_T unboundSize = model.Unbound.GetAllocatedSize();
    const int32 assignmentCount = model.Assignments.Num();

    // The same keys again
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("same build: same assignments"), model.Assignments.Num(), assignmentCount);
    TestTrue(TEXT("same build: assignments buffer kept"), model.Assignments.GetData() == assignmentsData);
    TestEqual(TEXT("same build: assignments capacity kept"), static_cast<int64>(model.Assignments.GetAllocatedSize()),
        static_cast<int64>(assignmentsSize));
    TestTrue(TEXT("same build: keys buffer kept"), model.Keys.GetData() == keysData);
    TestEqual(TEXT("same build: keys capacity kept"), static_cast<int64>(model.Keys.GetAllocatedSize()),
        static_cast<int64>(keysSize));
    TestTrue(TEXT("same build: overflow buffer kept"), model.Overflow.GetData() == overflowData);
    TestEqual(TEXT("same build: overflow capacity kept"), static_cast<int64>(model.Overflow.GetAllocatedSize()),
        static_cast<int64>(overflowSize));
    TestTrue(TEXT("same build: unbound buffer kept"), model.Unbound.GetData() == unboundData);
    TestEqual(TEXT("same build: unbound capacity kept"), static_cast<int64>(model.Unbound.GetAllocatedSize()),
        static_cast<int64>(unboundSize));

    // A rebind between two layout keys
    keys[KEYDIAGRAMTEST_DROP] = TEXT("K");
    SOLKeyDiagram::Build(keys, model);
    TestTrue(TEXT("rebind: assignments buffer kept"), model.Assignments.GetData() == assignmentsData);
    TestTrue(TEXT("rebind: keys buffer kept"), model.Keys.GetData() == keysData);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramCustomRowsTest, "SOLTest.KeyDiagram.CustomRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Build over a given row table assigns only what those rows name, with row indices into that table
bool FSOLKeyDiagramCustomRowsTest::RunTest(const FString& /*parameters*/)
{
    const SOLMenuText::FControlRow rows[] = {
        { TEXT("Combat"), TEXT("@Fire"), TEXT("Fire") },
        { TEXT("Map"), TEXT("X"), TEXT("Clear") },
    };
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(rows, SOLKeyBindings::DefaultKeys(), model);
    TestEqual(TEXT("two assignments"), model.Assignments.Num(), 2);
    TestEqual(TEXT("one model key per layout key"), model.Keys.Num(), SOLKeyboardLayout::KeyCount());

    const FSOLDiagramKey* leftMouse = KeyDiagramTestKey(model, TEXT("LeftMouseButton"));
    TestTrue(TEXT("LMB holds Fire only"), leftMouse != nullptr && leftMouse->AssignmentCount == 1);
    if (leftMouse != nullptr && model.Assignments.IsValidIndex(leftMouse->Primary))
    {
        const FSOLKeyAssignment& fire = model.Assignments[leftMouse->Primary];
        TestEqual(TEXT("Fire row 0"), fire.Row, 0);
        TestEqual(TEXT("Fire binding"), fire.Binding, KEYDIAGRAMTEST_FIRE);
        TestTrue(TEXT("Fire area"), fire.Area == ESOLControlArea::Combat);
    }
    const FSOLDiagramKey* x = KeyDiagramTestKey(model, TEXT("X"));
    TestTrue(TEXT("X holds the clear row only"), x != nullptr && x->AssignmentCount == 1);
    if (x != nullptr && model.Assignments.IsValidIndex(x->Primary))
    {
        const FSOLKeyAssignment& clear = model.Assignments[x->Primary];
        TestEqual(TEXT("clear row 1"), clear.Row, 1);
        TestTrue(TEXT("clear is fixed"), clear.bFixed);
        TestFalse(TEXT("clear is a map action"), clear.bInFlight);
        TestTrue(TEXT("X locked"), x->bLocked);
    }
    return true;
}

#endif
