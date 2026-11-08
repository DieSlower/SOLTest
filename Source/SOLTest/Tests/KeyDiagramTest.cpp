/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLKeyDiagram.h"
#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLMenuContent.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"

#include <cmath>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #33 Part 33a keyboard diagram model (Docs/SDDs/33-player-documentation.md sections 7 and 10,
// D13, D15, D17-D19, D29), written against the header contract (Menu/SOLKeyDiagram.h) before the production code
// existed: which Controls actions sit on each diagram key at the live keys, the primary tint and short label, the "+N"
// count, the lock mark, the overflow and unbound lists, the ten-colour area palette and the no-reallocation rebuild.
// Part 33c adds, also test-first: the chord modifier marker (D32 revised: the six physical modifier keys, each chord's
// modifier keys, the chords each key starts, bModifier on every key that starts a chord whether or not it has actions
// of its own, its own fields untouched by the chords), each assignment's alternative and the hover strip's assignments
// (own actions, then chords) and lines.

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
    constexpr int32 KEYDIAGRAMTEST_THRUST_UP = static_cast<int32>(SOLKeyBindings::EBinding::ThrustUp);
    constexpr int32 KEYDIAGRAMTEST_THRUST_DOWN = static_cast<int32>(SOLKeyBindings::EBinding::ThrustDown);
    constexpr int32 KEYDIAGRAMTEST_OPEN_MAP_JUMP = static_cast<int32>(SOLKeyBindings::EBinding::OpenMapJump);
    constexpr int32 KEYDIAGRAMTEST_FREE_LOOK = static_cast<int32>(SOLKeyBindings::EBinding::FreeLook);
    constexpr int32 KEYDIAGRAMTEST_ORBIT_LINES = static_cast<int32>(SOLKeyBindings::EBinding::ToggleOrbitLines);
    constexpr int32 KEYDIAGRAMTEST_OVERLAY_MENU = static_cast<int32>(SOLKeyBindings::EBinding::OverlayMenu);
    constexpr int32 KEYDIAGRAMTEST_CYCLE_PROFILE = static_cast<int32>(SOLKeyBindings::EBinding::CycleOverlayProfile);
    constexpr int32 KEYDIAGRAMTEST_AREA_COUNT = static_cast<int32>(ESOLControlArea::Count);
    constexpr int32 KEYDIAGRAMTEST_ROW_COUNT = static_cast<int32>(UE_ARRAY_COUNT(SOLMenuText::CONTROL_ROWS));
    constexpr float KEYDIAGRAMTEST_MIN_COLOR_DISTANCE = 48.0f;  // sRGB 0-255 Euclidean, pairwise (D17)

    // The layout keys a chord is pressed with (D32 revised), in EKeyChord order, left side first, nullptr past the
    // last: None none, Shift either Shift, Alt either Alt, Control either Ctrl, RightShift Right Shift only
    constexpr int32 KEYDIAGRAMTEST_MAX_CHORD_KEYS = 2;
    const TCHAR* const KEYDIAGRAMTEST_CHORD_KEYS[][KEYDIAGRAMTEST_MAX_CHORD_KEYS] = {
        { nullptr, nullptr },
        { TEXT("LeftShift"), TEXT("RightShift") },
        { TEXT("LeftAlt"), TEXT("RightAlt") },
        { TEXT("LeftControl"), TEXT("RightControl") },
        { TEXT("RightShift"), nullptr },
    };

    // The six physical modifier keys (D32 revised): at the defaults exactly these are bModifier
    const TCHAR* const KEYDIAGRAMTEST_D32_KEYS[] = { TEXT("LeftControl"), TEXT("RightControl"), TEXT("LeftAlt"),
        TEXT("RightAlt"), TEXT("LeftShift"), TEXT("RightShift") };

    // The modifier keys with no action of their own at the defaults (D32 revised)
    const TCHAR* const KEYDIAGRAMTEST_RIGHT_MODIFIERS[] = { TEXT("RightControl"), TEXT("RightAlt"),
        TEXT("RightShift") };

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
    // Returns whether a chord is pressed with a layout key name (KEYDIAGRAMTEST_CHORD_KEYS, case-insensitive)
    bool KeyDiagramTestChordStartsOn(const EKeyChord chord, const TCHAR* keyName)
    {
        const int32 index = static_cast<int32>(chord);
        if (index < 0 || index >= static_cast<int32>(UE_ARRAY_COUNT(KEYDIAGRAMTEST_CHORD_KEYS)) || keyName == nullptr)
        {
            return false;
        }
        for (const TCHAR* chordKey : KEYDIAGRAMTEST_CHORD_KEYS[index])
        {
            if (chordKey != nullptr && FCString::Stricmp(chordKey, keyName) == 0)
            {
                return true;
            }
        }
        return false;
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
    // Asserts one diagram key's derived fields (primary, badge, area, label, lock) agree with its own assignments only,
    // and that it is a modifier exactly when it starts a chord (D32 revised)
    void KeyDiagramTestCheckKey(FAutomationTestBase& test, const FSOLKeyDiagramModel& model, const FSOLDiagramKey& key,
        const FString& what)
    {
        test.TestTrue(what + TEXT(": a modifier exactly when it starts a chord (D32)"),
            key.bModifier == (key.ChordCount > 0));
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

    //////////////////////////////////////////////////////////////////////////
    // Returns the '|' alternatives of a row's bindings cell, or an empty array for a row out of range
    TArray<FString> KeyDiagramTestAlternatives(const TConstArrayView<SOLMenuText::FControlRow> rows, const int32 row)
    {
        TArray<FString> alternatives;
        if (rows.IsValidIndex(row))
        {
            FString(rows[row].Bindings).ParseIntoArray(alternatives, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER);
        }
        return alternatives;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the index of the first Controls row with this bindings cell, or INDEX_NONE
    int32 KeyDiagramTestRowOf(const TCHAR* bindings)
    {
        for (int32 row = 0; row < KEYDIAGRAMTEST_ROW_COUNT; ++row)
        {
            if (FCString::Strcmp(SOLMenuText::CONTROL_ROWS[row].Bindings, bindings) == 0)
            {
                return row;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the (row, alternative) pairs of CONTROL_ROWS whose chord a modifier key starts at the keys: an '@'
    // binding with a key whose chord (GetChord) is pressed with that key (KEYDIAGRAMTEST_CHORD_KEYS), or a fixed '+'
    // chord whose first key's chord is (LeftControl+F1 starts on both Ctrl keys)
    TArray<FIntPoint> KeyDiagramTestExpectedChords(const TArray<FString>& keys, const TCHAR* modifierKey)
    {
        TArray<FIntPoint> expected;
        for (int32 row = 0; row < KEYDIAGRAMTEST_ROW_COUNT; ++row)
        {
            const TArray<FString> alternatives = KeyDiagramTestAlternatives(MakeArrayView(SOLMenuText::CONTROL_ROWS),
                row);
            for (int32 alternative = 0; alternative < alternatives.Num(); ++alternative)
            {
                const FString& text = alternatives[alternative];
                if (text.StartsWith(TEXT("@")))
                {
                    // A binding's chord, when the binding has a key
                    const int32 binding = SOLKeyBindings::FindById(text.RightChop(1));
                    const bool bBound = keys.IsValidIndex(binding) && !keys[binding].IsEmpty();
                    if (bBound && KeyDiagramTestChordStartsOn(SOLKeyBindings::GetChord(binding), modifierKey))
                    {
                        expected.AddUnique(FIntPoint(row, alternative));
                    }
                    continue;
                }
                TArray<FString> chordKeys;
                text.ParseIntoArray(chordKeys, SOLMenuText::BINDING_CHORD_DELIMITER);
                if (KeyDiagramTestChordStartsOn(KeyDiagramTestFixedChord(chordKeys), modifierKey))
                {
                    expected.AddUnique(FIntPoint(row, alternative));
                }
            }
        }
        return expected;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a diagram key's chord range lies inside the model's chords
    bool KeyDiagramTestChordRangeValid(const FSOLKeyDiagramModel& model, const FSOLDiagramKey& key)
    {
        return key.FirstChord >= 0 && key.ChordCount >= 0 && key.FirstChord + key.ChordCount <= model.Chords.Num();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the (row, alternative) pairs of the assignments in a key's chord range (empty for an invalid range)
    TArray<FIntPoint> KeyDiagramTestChordPairs(const FSOLKeyDiagramModel& model, const FSOLDiagramKey& key)
    {
        TArray<FIntPoint> pairs;
        if (!KeyDiagramTestChordRangeValid(model, key))
        {
            return pairs;
        }
        for (int32 index = key.FirstChord; index < key.FirstChord + key.ChordCount; ++index)
        {
            const int32 assignment = model.Chords[index];
            if (model.Assignments.IsValidIndex(assignment))
            {
                pairs.Add(FIntPoint(model.Assignments[assignment].Row, model.Assignments[assignment].Alternative));
            }
        }
        return pairs;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a key's chord range holds an assignment of this binding
    bool KeyDiagramTestChordsHoldBinding(const FSOLKeyDiagramModel& model, const FSOLDiagramKey& key,
        const int32 binding)
    {
        if (!KeyDiagramTestChordRangeValid(model, key))
        {
            return false;
        }
        for (int32 index = key.FirstChord; index < key.FirstChord + key.ChordCount; ++index)
        {
            const int32 assignment = model.Chords[index];
            if (model.Assignments.IsValidIndex(assignment) && model.Assignments[assignment].Binding == binding)
            {
                return true;
            }
        }
        return false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a layout key's chord range holds exactly the chords CONTROL_ROWS gives it at the keys, without
    // duplicates and in Assignments order
    void KeyDiagramTestCheckChords(FAutomationTestBase& test, const FSOLKeyDiagramModel& model,
        const TArray<FString>& keys, const TCHAR* keyName, const FString& what)
    {
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName);
        test.TestTrue(what + TEXT(": on the diagram"), key != nullptr);
        if (key == nullptr)
        {
            return;
        }
        test.TestTrue(what + TEXT(": chord range inside Chords"), KeyDiagramTestChordRangeValid(model, *key));
        if (!KeyDiagramTestChordRangeValid(model, *key))
        {
            return;
        }
        const TArray<FIntPoint> expected = KeyDiagramTestExpectedChords(keys, keyName);
        const TArray<FIntPoint> actual = KeyDiagramTestChordPairs(model, *key);
        test.TestTrue(what + TEXT(": starts at least one chord"), expected.Num() > 0);
        test.TestEqual(what + TEXT(": one chord per Controls chord it starts"), key->ChordCount, expected.Num());
        for (const FIntPoint& pair : expected)
        {
            test.TestTrue(FString::Printf(TEXT("%s: lists row %d alternative %d"), *what, pair.X, pair.Y),
                actual.Contains(pair));
        }

        // Assignments order, so no index twice
        for (int32 index = key->FirstChord + 1; index < key->FirstChord + key->ChordCount; ++index)
        {
            test.TestTrue(FString::Printf(TEXT("%s: chord %d after the one before"), *what, index),
                model.Chords[index - 1] < model.Chords[index]);
        }
    }

    // A fixed chord of CONTROL_ROWS by its row's bindings cell and the alternative's index in it
    struct FKeyDiagramTestFixedCell
    {
        const TCHAR* Cell = TEXT("");
        int32 Alternative = 0;
    };

    // The fixed Control chords (Ctrl+F1, Ctrl+F2, Ctrl+RMB) and Shift chords (map pan, height lock) of CONTROL_ROWS
    const FKeyDiagramTestFixedCell KEYDIAGRAMTEST_CONTROL_CELLS[] = { { TEXT("LeftControl+F1"), 0 },
        { TEXT("LeftControl+F2"), 0 }, { TEXT("LeftControl+RightMouseButton"), 0 } };
    const FKeyDiagramTestFixedCell KEYDIAGRAMTEST_SHIFT_CELLS[] = {
        { TEXT("MiddleMouseButton|LeftShift+RightMouseButton"), 1 },
        { TEXT("LeftShift|LeftShift+LeftMouseButton"), 1 } };

    //////////////////////////////////////////////////////////////////////////
    // Asserts a layout key's chord range is exactly the named chords (D32 revised): these bindings' and these fixed
    // cells' alternatives, one entry each, and none of the excluded bindings
    void KeyDiagramTestCheckNamedChords(FAutomationTestBase& test, const FSOLKeyDiagramModel& model,
        const TCHAR* keyName, const TConstArrayView<int32> bindings,
        const TConstArrayView<FKeyDiagramTestFixedCell> cells, const TConstArrayView<int32> excluded,
        const FString& what)
    {
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName);
        test.TestTrue(what + TEXT(": on the diagram"), key != nullptr);
        if (key == nullptr)
        {
            return;
        }
        test.TestTrue(what + TEXT(": chord range inside Chords"), KeyDiagramTestChordRangeValid(model, *key));
        for (const int32 binding : bindings)
        {
            test.TestTrue(FString::Printf(TEXT("%s: lists %s"), *what, SOLKeyBindings::GetDef(binding).Id),
                KeyDiagramTestChordsHoldBinding(model, *key, binding));
        }
        const TArray<FIntPoint> pairs = KeyDiagramTestChordPairs(model, *key);
        for (const FKeyDiagramTestFixedCell& cell : cells)
        {
            const int32 row = KeyDiagramTestRowOf(cell.Cell);
            test.TestTrue(FString::Printf(TEXT("%s: the '%s' row exists"), *what, cell.Cell), row != INDEX_NONE);
            test.TestTrue(FString::Printf(TEXT("%s: lists '%s' alternative %d"), *what, cell.Cell, cell.Alternative),
                pairs.Contains(FIntPoint(row, cell.Alternative)));
        }
        for (const int32 binding : excluded)
        {
            test.TestFalse(FString::Printf(TEXT("%s: does not list %s"), *what, SOLKeyBindings::GetDef(binding).Id),
                KeyDiagramTestChordsHoldBinding(model, *key, binding));
        }
        test.TestEqual(what + TEXT(": exactly the named chords"), key->ChordCount, bindings.Num() + cells.Num());
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts the six modifier keys start the default chord sets (D32 revised): Ctrl+O and the fixed Control chords on
    // both Ctrl keys, Alt+O on both Alt keys, Shift+M, Shift+J and the fixed Shift chords on both Shift keys, RShift+O
    // on Right Shift only (valid at any keys that leave those chord bindings bound)
    void KeyDiagramTestCheckDefaultChordSets(FAutomationTestBase& test, const FSOLKeyDiagramModel& model,
        const FString& what)
    {
        const int32 control[] = { KEYDIAGRAMTEST_OVERLAY_MENU };
        const int32 alt[] = { KEYDIAGRAMTEST_ORBIT_LINES };
        const int32 shift[] = { KEYDIAGRAMTEST_OPEN_MAP, KEYDIAGRAMTEST_OPEN_MAP_JUMP };
        const int32 rightShift[] = { KEYDIAGRAMTEST_OPEN_MAP, KEYDIAGRAMTEST_OPEN_MAP_JUMP,
            KEYDIAGRAMTEST_CYCLE_PROFILE };
        const int32 notLeftShift[] = { KEYDIAGRAMTEST_CYCLE_PROFILE };
        const TConstArrayView<int32> none;
        const TConstArrayView<FKeyDiagramTestFixedCell> noCells;
        KeyDiagramTestCheckNamedChords(test, model, TEXT("LeftControl"), control, KEYDIAGRAMTEST_CONTROL_CELLS, none,
            what + TEXT(": Left Ctrl"));
        KeyDiagramTestCheckNamedChords(test, model, TEXT("RightControl"), control, KEYDIAGRAMTEST_CONTROL_CELLS, none,
            what + TEXT(": Right Ctrl"));
        KeyDiagramTestCheckNamedChords(test, model, TEXT("LeftAlt"), alt, noCells, none, what + TEXT(": Left Alt"));
        KeyDiagramTestCheckNamedChords(test, model, TEXT("RightAlt"), alt, noCells, none, what + TEXT(": Right Alt"));
        KeyDiagramTestCheckNamedChords(test, model, TEXT("LeftShift"), shift, KEYDIAGRAMTEST_SHIFT_CELLS,
            notLeftShift, what + TEXT(": Left Shift"));
        KeyDiagramTestCheckNamedChords(test, model, TEXT("RightShift"), rightShift, KEYDIAGRAMTEST_SHIFT_CELLS, none,
            what + TEXT(": Right Shift"));
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts the fields of a modifier key with no action of its own (D32 revised): a modifier, no primary, area, badge
    // or label, not locked
    void KeyDiagramTestCheckModifier(FAutomationTestBase& test, const FSOLDiagramKey& key, const FString& what)
    {
        test.TestTrue(what + TEXT(": a modifier"), key.bModifier);
        test.TestEqual(what + TEXT(": no action of its own"), key.AssignmentCount, 0);
        test.TestTrue(what + TEXT(": starts a chord"), key.ChordCount > 0);
        test.TestEqual(what + TEXT(": no primary"), key.Primary, static_cast<int32>(INDEX_NONE));
        test.TestTrue(what + TEXT(": no area"), key.Area == ESOLControlArea::Count);
        test.TestEqual(what + TEXT(": chords not counted in the badge"), key.ExtraCount, 0);
        test.TestTrue(what + TEXT(": no label"), KeyDiagramTestSame(key.ShortLabel, TEXT("")));
        test.TestFalse(what + TEXT(": not locked"), key.bLocked);
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the layout keys (FindKey) a chord assignment of CONTROL_ROWS starts on (D32 revised): the keys of its
    // chord in KEYDIAGRAMTEST_CHORD_KEYS, the chord being a binding's GetChord or a fixed '+' alternative's first key's
    // (the alternative from the assignment's Alternative); none for a plain one
    void KeyDiagramTestModifiersOf(const FSOLKeyAssignment& assignment, TArray<int32>& out)
    {
        out.Reset();
        if (assignment.Chord == EKeyChord::None)
        {
            return;
        }
        EKeyChord chord = EKeyChord::None;
        if (assignment.Binding != INDEX_NONE)
        {
            chord = SOLKeyBindings::GetChord(assignment.Binding);
        }
        else
        {
            const TArray<FString> alternatives = KeyDiagramTestAlternatives(MakeArrayView(SOLMenuText::CONTROL_ROWS),
                assignment.Row);
            if (alternatives.IsValidIndex(assignment.Alternative))
            {
                TArray<FString> chordKeys;
                alternatives[assignment.Alternative].ParseIntoArray(chordKeys, SOLMenuText::BINDING_CHORD_DELIMITER);
                chord = KeyDiagramTestFixedChord(chordKeys);
            }
        }
        const int32 chordIndex = static_cast<int32>(chord);
        if (chordIndex <= 0 || chordIndex >= static_cast<int32>(UE_ARRAY_COUNT(KEYDIAGRAMTEST_CHORD_KEYS)))
        {
            return;
        }
        for (const TCHAR* chordKey : KEYDIAGRAMTEST_CHORD_KEYS[chordIndex])
        {
            const int32 key = chordKey != nullptr ? SOLKeyboardLayout::FindKey(chordKey) : INDEX_NONE;
            if (key != INDEX_NONE)
            {
                out.AddUnique(key);
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts the model's chord ranges over every key (D32 revised): each range inside Chords, holding chord
    // assignments in Assignments order, the ranges grouped by key in layout order, each layout key's range exactly the
    // chord assignments starting on it, no chords on an overflow key, bModifier exactly on a key that starts one, and a
    // mirror key with its main key's chord range and bModifier
    void KeyDiagramTestCheckChordRanges(FAutomationTestBase& test, const FSOLKeyDiagramModel& model,
        const FString& what)
    {
        const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();

        // How many chord assignments name each layout key, from the assignments themselves
        TArray<int32> expectedCounts;
        expectedCounts.Init(0, model.Keys.Num());
        TArray<int32> modifiers;
        for (const FSOLKeyAssignment& assignment : model.Assignments)
        {
            KeyDiagramTestModifiersOf(assignment, modifiers);
            for (const int32 key : modifiers)
            {
                if (expectedCounts.IsValidIndex(key))
                {
                    ++expectedCounts[key];
                }
            }
        }

        int32 total = 0;
        int32 nextFirst = 0;
        for (int32 index = 0; index < model.Keys.Num(); ++index)
        {
            const FSOLDiagramKey& key = model.Keys[index];
            const FString keyWhat = FString::Printf(TEXT("%s: key %d '%s'"), *what, index, *key.KeyName);
            if (layout.IsValidIndex(index) && layout[index].bMirror)
            {
                // A mirror copies its main key's chord range and mark
                const int32 main = SOLKeyboardLayout::FindKey(layout[index].KeyName);
                test.TestTrue(keyWhat + TEXT(": mirror's main key in the model"), model.Keys.IsValidIndex(main));
                if (model.Keys.IsValidIndex(main))
                {
                    const FSOLDiagramKey& mainKey = model.Keys[main];
                    test.TestEqual(keyWhat + TEXT(": mirror's first chord"), key.FirstChord, mainKey.FirstChord);
                    test.TestEqual(keyWhat + TEXT(": mirror's chord count"), key.ChordCount, mainKey.ChordCount);
                    test.TestTrue(keyWhat + TEXT(": mirror's modifier mark"), key.bModifier == mainKey.bModifier);
                }
                continue;
            }
            test.TestTrue(keyWhat + TEXT(": chord range inside Chords"), KeyDiagramTestChordRangeValid(model, key));
            if (!KeyDiagramTestChordRangeValid(model, key))
            {
                continue;
            }
            test.TestEqual(keyWhat + TEXT(": one chord per chord assignment starting on it"), key.ChordCount,
                expectedCounts.IsValidIndex(index) ? expectedCounts[index] : 0);
            test.TestTrue(keyWhat + TEXT(": a modifier exactly when it starts a chord"),
                key.bModifier == (key.ChordCount > 0));
            if (key.ChordCount > 0)
            {
                test.TestTrue(keyWhat + TEXT(": ranges grouped by key in layout order"), key.FirstChord >= nextFirst);
                nextFirst = key.FirstChord + key.ChordCount;
            }
            total += key.ChordCount;

            // Each entry a chord assignment that names this key, in Assignments order
            TArray<int32> entryModifiers;
            for (int32 chord = key.FirstChord; chord < key.FirstChord + key.ChordCount; ++chord)
            {
                const int32 assignment = model.Chords[chord];
                test.TestTrue(keyWhat + TEXT(": a chord indexes an assignment"),
                    model.Assignments.IsValidIndex(assignment));
                if (!model.Assignments.IsValidIndex(assignment))
                {
                    continue;
                }
                test.TestTrue(keyWhat + TEXT(": a chord is a chord assignment"),
                    model.Assignments[assignment].Chord != EKeyChord::None);
                KeyDiagramTestModifiersOf(model.Assignments[assignment], entryModifiers);
                test.TestTrue(keyWhat + TEXT(": a chord starts on the key"),
                    entryModifiers.Contains(index));
                if (chord > key.FirstChord)
                {
                    test.TestTrue(keyWhat + TEXT(": chords in Assignments order, none twice"),
                        model.Chords[chord - 1] < assignment);
                }
            }
        }
        test.TestEqual(what + TEXT(": Chords is the layout keys' ranges"), total, model.Chords.Num());

        // An overflow key starts no chord and is no modifier
        for (const FSOLDiagramKey& key : model.Overflow)
        {
            test.TestEqual(what + TEXT(": overflow ") + key.KeyName + TEXT(" starts no chord"), key.ChordCount, 0);
            test.TestFalse(what + TEXT(": overflow ") + key.KeyName + TEXT(" is no modifier"), key.bModifier);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the hover strip's assignments of a key as the contract states them (D32 revised): its own actions first
    // (the primary, then the others in model order), then its chord range in order; a mirror gives its main key's;
    // nothing for a key with neither or an index out of range
    void KeyDiagramTestExpectedDetail(const FSOLKeyDiagramModel& model, const int32 keyIndex, TArray<int32>& out)
    {
        out.Reset();
        if (!model.Keys.IsValidIndex(keyIndex))
        {
            return;
        }
        const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
        const int32 main = layout.IsValidIndex(keyIndex) && layout[keyIndex].bMirror
            ? SOLKeyboardLayout::FindKey(layout[keyIndex].KeyName) : keyIndex;
        if (!model.Keys.IsValidIndex(main))
        {
            return;
        }
        const FSOLDiagramKey& key = model.Keys[main];

        // Own actions, the primary first
        if (key.AssignmentCount > 0)
        {
            out.Add(key.Primary);
            for (int32 index = key.FirstAssignment; index < key.FirstAssignment + key.AssignmentCount; ++index)
            {
                if (index != key.Primary)
                {
                    out.Add(index);
                }
            }
        }

        // Then the chords it starts
        for (int32 index = key.FirstChord; index < key.FirstChord + key.ChordCount; ++index)
        {
            if (model.Chords.IsValidIndex(index))
            {
                out.Add(model.Chords[index]);
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Asserts a hover strip line is the one the contract gives an assignment: the alternative's FormatBindings at the
    // keys, the row's FormatControlAction, the assignment's area and fixed flag
    void KeyDiagramTestCheckLine(FAutomationTestBase& test, const TConstArrayView<SOLMenuText::FControlRow> rows,
        const FSOLKeyDiagramModel& model, const int32 assignment, const TArray<FString>& keys,
        const FSOLDiagramDetailLine& line, const FString& what)
    {
        if (!model.Assignments.IsValidIndex(assignment))
        {
            test.AddError(what + TEXT(": no such assignment"));
            return;
        }
        const FSOLKeyAssignment& source = model.Assignments[assignment];
        const TArray<FString> alternatives = KeyDiagramTestAlternatives(rows, source.Row);
        test.TestTrue(what + TEXT(": its alternative exists"), alternatives.IsValidIndex(source.Alternative));
        if (alternatives.IsValidIndex(source.Alternative))
        {
            test.TestEqual(what + TEXT(": keys"), line.Keys,
                SOLMenuContent::FormatBindings(*alternatives[source.Alternative], keys));
        }
        if (rows.IsValidIndex(source.Row))
        {
            test.TestEqual(what + TEXT(": action"), line.Action,
                SOLMenuContent::FormatControlAction(rows[source.Row].Action));
        }
        test.TestTrue(what + TEXT(": area"), line.Area == source.Area);
        test.TestTrue(what + TEXT(": fixed flag"), line.bFixed == source.bFixed);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the default keys with ThrustDown moved to K and FreeLook to N, so Left Ctrl and Left Alt carry no action
    // of their own (only chords)
    TArray<FString> KeyDiagramTestFreedModifierKeys()
    {
        TArray<FString> keys = SOLKeyBindings::DefaultKeys();
        keys[KEYDIAGRAMTEST_THRUST_DOWN] = TEXT("K");
        keys[KEYDIAGRAMTEST_FREE_LOOK] = TEXT("N");
        return keys;
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
// A second Build into the same model (same keys, or a rebind between layout keys) keeps every buffer, the chords
// included (D13, D32); Reset keeps the chords' capacity
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
    TestTrue(TEXT("first build made chords (D32)"), model.Chords.Num() > 0);
    const void* chordsData = model.Chords.GetData();
    const SIZE_T chordsSize = model.Chords.GetAllocatedSize();
    const int32 chordCount = model.Chords.Num();
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
    TestEqual(TEXT("same build: same chords"), model.Chords.Num(), chordCount);
    TestTrue(TEXT("same build: chords buffer kept"), model.Chords.GetData() == chordsData);
    TestEqual(TEXT("same build: chords capacity kept"), static_cast<int64>(model.Chords.GetAllocatedSize()),
        static_cast<int64>(chordsSize));

    // A rebind between two layout keys
    keys[KEYDIAGRAMTEST_DROP] = TEXT("K");
    SOLKeyDiagram::Build(keys, model);
    TestTrue(TEXT("rebind: assignments buffer kept"), model.Assignments.GetData() == assignmentsData);
    TestTrue(TEXT("rebind: keys buffer kept"), model.Keys.GetData() == keysData);
    TestTrue(TEXT("rebind: chords buffer kept"), model.Chords.GetData() == chordsData);

    // A chord binding's base key moved: the chords keep their buffer
    keys[KEYDIAGRAMTEST_OVERLAY_MENU] = TEXT("N");
    SOLKeyDiagram::Build(keys, model);
    TestTrue(TEXT("chord rebind: chords buffer kept"), model.Chords.GetData() == chordsData);
    TestEqual(TEXT("chord rebind: chords capacity kept"), static_cast<int64>(model.Chords.GetAllocatedSize()),
        static_cast<int64>(chordsSize));

    // Reset empties the chords and keeps their capacity
    model.Reset();
    TestEqual(TEXT("Reset empties the chords"), model.Chords.Num(), 0);
    TestEqual(TEXT("Reset keeps the chords' capacity"), static_cast<int64>(model.Chords.GetAllocatedSize()),
        static_cast<int64>(chordsSize));
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

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramChordModifierKeysTest, "SOLTest.KeyDiagram.ChordModifierKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each chord names the physical keys it is pressed with, left side first (D32 revised): Shift both Shifts, RightShift
// Right Shift only, Alt both Alts, Control both Ctrls, None none; every name on the layout, six distinct keys in all
bool FSOLKeyDiagramChordModifierKeysTest::RunTest(const FString& /*parameters*/)
{
    const TCHAR* chordNames[] = { TEXT("None"), TEXT("Shift"), TEXT("Alt"), TEXT("Control"), TEXT("RightShift") };
    const EKeyChord chords[] = { EKeyChord::None, EKeyChord::Shift, EKeyChord::Alt, EKeyChord::Control,
        EKeyChord::RightShift };
    TArray<int32> layoutKeys;
    for (const EKeyChord chord : chords)
    {
        const int32 chordIndex = static_cast<int32>(chord);
        const FString what = chordNames[chordIndex];
        const TConstArrayView<const TCHAR*> actual = SOLKeyDiagram::ChordModifierKeys(chord);

        // The expected names in order, up to the first nullptr
        TArray<const TCHAR*> expected;
        for (const TCHAR* keyName : KEYDIAGRAMTEST_CHORD_KEYS[chordIndex])
        {
            if (keyName == nullptr)
            {
                break;
            }
            expected.Add(keyName);
        }
        TestEqual(what + TEXT(": key count"), actual.Num(), expected.Num());
        for (int32 index = 0; index < FMath::Min(actual.Num(), expected.Num()); ++index)
        {
            TestEqual(FString::Printf(TEXT("%s: key %d"), *what, index),
                FString(actual[index] != nullptr ? actual[index] : TEXT("")), FString(expected[index]));
        }
        for (const TCHAR* keyName : actual)
        {
            if (!TestNotNull(what + TEXT(": a key name"), keyName))
            {
                continue;
            }
            const int32 key = SOLKeyboardLayout::FindKey(keyName);
            TestTrue(FString::Printf(TEXT("%s: %s on the layout"), *what, keyName), key != INDEX_NONE);
            if (key != INDEX_NONE)
            {
                layoutKeys.AddUnique(key);
            }
        }
    }
    TestTrue(TEXT("None: no key"), SOLKeyDiagram::ChordModifierKeys(EKeyChord::None).Num() == 0);
    TestEqual(TEXT("six distinct modifier keys"), layoutKeys.Num(),
        static_cast<int32>(UE_ARRAY_COUNT(KEYDIAGRAMTEST_D32_KEYS)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramChordRangesTest, "SOLTest.KeyDiagram.ChordRanges",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At the defaults and at rebound keys, every layout key's chord range is exactly the chord assignments starting on it
// (in Assignments order, no duplicates, inside Chords, grouped by key in layout order), overflow keys start none,
// bModifier marks exactly the keys that start a chord and a mirror copies its main key's range and mark (D32 revised)
bool FSOLKeyDiagramChordRangesTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeyDiagramModel model;
    const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
    SOLKeyDiagram::Build(defaults, model);
    TestTrue(TEXT("defaults: the model holds chords"), model.Chords.Num() > 0);
    KeyDiagramTestCheckChordRanges(*this, model, TEXT("defaults"));

    // The modifiers freed of their own actions
    SOLKeyDiagram::Build(KeyDiagramTestFreedModifierKeys(), model);
    KeyDiagramTestCheckChordRanges(*this, model, TEXT("freed modifiers"));

    // A chord binding's base key moved, and one unbound
    TArray<FString> keys = defaults;
    keys[KEYDIAGRAMTEST_OVERLAY_MENU] = TEXT("K");
    keys[KEYDIAGRAMTEST_ORBIT_LINES] = FString();
    SOLKeyDiagram::Build(keys, model);
    KeyDiagramTestCheckChordRanges(*this, model, TEXT("rebound chords"));

    // A short key array: only the fixed chords
    SOLKeyDiagram::Build(TArray<FString>(), model);
    KeyDiagramTestCheckChordRanges(*this, model, TEXT("short array"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramModifierKeysTest, "SOLTest.KeyDiagram.ModifierKeys",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// At the defaults exactly the six physical modifier keys are bModifier (D32 revised). Each lists the chords computed
// from CONTROL_ROWS and GetChord, which are the named sets: both Ctrl keys Ctrl+O, Ctrl+F1, Ctrl+F2, Ctrl+RMB; both Alt
// keys Alt+O; both Shift keys Shift+M, Shift+J, Shift+RMB, Shift+LMB; Right Shift also RShift+O. Left Ctrl keeps its
// own ThrustDown and Left Alt its own FreeLook (primary, area, label, lock and "+N" from their own actions only);
// Right Ctrl, Right Alt and Right Shift have no action of their own and the empty own fields
bool FSOLKeyDiagramModifierKeysTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);

    // Exactly the six keys carry the mark (mirrors aside, which copy their main key)
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    for (int32 index = 0; index < model.Keys.Num(); ++index)
    {
        if (layout.IsValidIndex(index) && layout[index].bMirror)
        {
            continue;
        }
        bool bExpected = false;
        for (const TCHAR* keyName : KEYDIAGRAMTEST_D32_KEYS)
        {
            bExpected |= SOLKeyboardLayout::FindKey(keyName) == index;
        }
        TestTrue(FString::Printf(TEXT("defaults: key %d '%s' a modifier exactly when one of the six"), index,
            *model.Keys[index].KeyName), model.Keys[index].bModifier == bExpected);
    }

    // Each of the six: the computed chords, the own fields untouched
    for (const TCHAR* keyName : KEYDIAGRAMTEST_D32_KEYS)
    {
        const FString what = FString::Printf(TEXT("defaults: %s"), keyName);
        KeyDiagramTestCheckChords(*this, model, keys, keyName, what);
        if (const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName))
        {
            TestTrue(what + TEXT(" a modifier"), key->bModifier);
            KeyDiagramTestCheckKey(*this, model, *key, what);
        }
    }

    // The named sets
    KeyDiagramTestCheckDefaultChordSets(*this, model, TEXT("defaults"));

    // Left Ctrl and Left Alt: one own action each, the primary, its area and label, no "+N" from the chords
    const int32 ownBindings[] = { KEYDIAGRAMTEST_THRUST_DOWN, KEYDIAGRAMTEST_FREE_LOOK };
    const TCHAR* ownKeys[] = { TEXT("LeftControl"), TEXT("LeftAlt") };
    for (int32 own = 0; own < static_cast<int32>(UE_ARRAY_COUNT(ownKeys)); ++own)
    {
        const FString what = FString::Printf(TEXT("defaults: %s"), ownKeys[own]);
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, ownKeys[own]);
        TestTrue(what + TEXT(" on the diagram"), key != nullptr);
        if (key == nullptr)
        {
            continue;
        }
        TestEqual(what + TEXT(": one action of its own"), key->AssignmentCount, 1);
        const FSOLKeyAssignment* assignment = KeyDiagramTestFind(model, *key, ownBindings[own]);
        TestTrue(what + TEXT(": holds its own action"), assignment != nullptr);
        TestTrue(what + TEXT(": its own action is the primary"), model.Assignments.IsValidIndex(key->Primary)
            && model.Assignments[key->Primary].Binding == ownBindings[own]);
        TestEqual(what + TEXT(": the chords add no \"+N\""), key->ExtraCount, 0);
        if (assignment != nullptr)
        {
            TestTrue(what + TEXT(": the own action's area"), key->Area == assignment->Area);
            TestTrue(what + TEXT(": the own action's label"), KeyDiagramTestSame(key->ShortLabel,
                assignment->ShortLabel));
        }
        TestFalse(what + TEXT(": not locked (a rebindable own action)"), key->bLocked);
    }

    // Right Ctrl, Right Alt and Right Shift: no action of their own
    for (const TCHAR* keyName : KEYDIAGRAMTEST_RIGHT_MODIFIERS)
    {
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName);
        TestTrue(FString::Printf(TEXT("defaults: %s on the diagram"), keyName), key != nullptr);
        if (key != nullptr)
        {
            KeyDiagramTestCheckModifier(*this, *key, FString::Printf(TEXT("defaults: %s"), keyName));
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramModifierKeysFreedTest, "SOLTest.KeyDiagram.ModifierKeysFreed",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// With ThrustDown and FreeLook moved off Left Ctrl and Left Alt (to K and N), only the own fields change: Left Ctrl,
// Left Alt and the three right-side modifiers carry only chords, each a modifier with the empty own fields and its
// default chord set; Left Shift keeps ThrustUp and its chords; K and N hold the moved actions and are no modifiers
// (D32 revised)
bool FSOLKeyDiagramModifierKeysFreedTest::RunTest(const FString& /*parameters*/)
{
    // K and N are free at the defaults
    FSOLKeyDiagramModel defaults;
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), defaults);
    const TCHAR* targets[] = { TEXT("K"), TEXT("N") };
    for (const TCHAR* keyName : targets)
    {
        const FSOLDiagramKey* key = KeyDiagramTestKey(defaults, keyName);
        TestTrue(FString::Printf(TEXT("%s free at the defaults"), keyName), key != nullptr
            && key->AssignmentCount == 0 && key->ChordCount == 0);
    }

    const TArray<FString> keys = KeyDiagramTestFreedModifierKeys();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("ThrustDown on K"), SOLKeyDiagram::KeyOfBinding(model, KEYDIAGRAMTEST_THRUST_DOWN),
        SOLKeyboardLayout::FindKey(TEXT("K")));
    TestEqual(TEXT("FreeLook on N"), SOLKeyDiagram::KeyOfBinding(model, KEYDIAGRAMTEST_FREE_LOOK),
        SOLKeyboardLayout::FindKey(TEXT("N")));
    for (const TCHAR* keyName : KEYDIAGRAMTEST_D32_KEYS)
    {
        const FString what = FString::Printf(TEXT("freed: %s"), keyName);
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName);
        TestTrue(what + TEXT(" on the diagram"), key != nullptr);
        if (key == nullptr)
        {
            continue;
        }
        if (FCString::Stricmp(keyName, TEXT("LeftShift")) != 0)
        {
            KeyDiagramTestCheckModifier(*this, *key, what);
        }
        else
        {
            // Left Shift still holds ThrustUp (and the map's plain LeftShift)
            TestTrue(what + TEXT(" a modifier"), key->bModifier);
            TestTrue(what + TEXT(" keeps ThrustUp"), KeyDiagramTestFind(model, *key, KEYDIAGRAMTEST_THRUST_UP)
                != nullptr);
        }
        KeyDiagramTestCheckChords(*this, model, keys, keyName, what);
        KeyDiagramTestCheckKey(*this, model, *key, what);
    }
    KeyDiagramTestCheckDefaultChordSets(*this, model, TEXT("freed"));

    // The moved actions' new keys start no chord and are no modifiers
    for (const TCHAR* keyName : targets)
    {
        const FSOLDiagramKey* key = KeyDiagramTestKey(model, keyName);
        TestTrue(FString::Printf(TEXT("freed: %s holds one action"), keyName), key != nullptr
            && key->AssignmentCount == 1);
        TestTrue(FString::Printf(TEXT("freed: %s no modifier"), keyName), key != nullptr && !key->bModifier
            && key->ChordCount == 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramLeftShiftChordsTest, "SOLTest.KeyDiagram.LeftShiftChords",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Left Shift holds two actions of its own (ThrustUp, the primary, and the map's plain "LeftShift" height preview) and
// starts chords (Shift+M, Shift+J, Shift+RMB, Shift+LMB, never RShift+O): it is a modifier, and its primary, area,
// label, lock and "+N" come from its own actions only. The plain "LeftShift" alternative is Left Shift's alone: Right
// Shift neither holds it nor lists it (D32 revised)
bool FSOLKeyDiagramLeftShiftChordsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    const FSOLDiagramKey* key = KeyDiagramTestKey(model, TEXT("LeftShift"));
    TestTrue(TEXT("Left Shift on the diagram"), key != nullptr);
    if (key == nullptr)
    {
        return true;
    }
    TestEqual(TEXT("Left Shift has two actions of its own"), key->AssignmentCount, 2);
    TestTrue(TEXT("Left Shift is a modifier"), key->bModifier);
    TestEqual(TEXT("the badge counts only its own actions"), key->ExtraCount, key->AssignmentCount - 1);
    TestTrue(TEXT("ThrustUp is the primary"), model.Assignments.IsValidIndex(key->Primary)
        && model.Assignments[key->Primary].Binding == KEYDIAGRAMTEST_THRUST_UP);
    TestFalse(TEXT("not locked (a rebindable own action)"), key->bLocked);
    KeyDiagramTestCheckChords(*this, model, keys, TEXT("LeftShift"), TEXT("Left Shift"));
    KeyDiagramTestCheckKey(*this, model, *key, TEXT("Left Shift"));
    TestTrue(TEXT("Left Shift lists Shift+M"), KeyDiagramTestChordsHoldBinding(model, *key, KEYDIAGRAMTEST_OPEN_MAP));
    TestFalse(TEXT("Left Shift does not list RShift+O"), KeyDiagramTestChordsHoldBinding(model, *key,
        KEYDIAGRAMTEST_CYCLE_PROFILE));

    // The map's plain "LeftShift" (alternative 0 of the height row) is one of Left Shift's own assignments
    const int32 heightRow = KeyDiagramTestRowOf(TEXT("LeftShift|LeftShift+LeftMouseButton"));
    TestTrue(TEXT("the height row exists"), heightRow != INDEX_NONE);
    bool bOwnHeight = false;
    if (KeyDiagramTestRangeValid(model, *key))
    {
        for (int32 index = key->FirstAssignment; index < key->FirstAssignment + key->AssignmentCount; ++index)
        {
            const FSOLKeyAssignment& assignment = model.Assignments[index];
            if (assignment.Row == heightRow && assignment.Alternative == 0)
            {
                bOwnHeight = true;
                TestTrue(TEXT("height preview: a fixed plain key"), assignment.bFixed
                    && assignment.Binding == INDEX_NONE && assignment.Chord == EKeyChord::None);
            }
        }
    }
    TestTrue(TEXT("Left Shift holds the plain height preview"), bOwnHeight);
    TestFalse(TEXT("the plain height preview is no chord of Left Shift"),
        KeyDiagramTestChordPairs(model, *key).Contains(FIntPoint(heightRow, 0)));

    // Not copied to the other side
    const FSOLDiagramKey* rightShift = KeyDiagramTestKey(model, TEXT("RightShift"));
    TestTrue(TEXT("Right Shift on the diagram"), rightShift != nullptr);
    if (rightShift != nullptr)
    {
        TestEqual(TEXT("Right Shift holds no action of its own"), rightShift->AssignmentCount, 0);
        TestFalse(TEXT("Right Shift does not list the plain height preview"),
            KeyDiagramTestChordPairs(model, *rightShift).Contains(FIntPoint(heightRow, 0)));
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramChordRebindTest, "SOLTest.KeyDiagram.ChordRebind",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A chord binding moved to another key stays in both its modifier keys' chords (the chord is per binding); a binding
// moved off a modifier key (ThrustDown off Left Ctrl) changes only that key's own fields; an unbound chord binding
// leaves its modifier keys' chords, and a key left starting none loses the mark (D32 revised)
bool FSOLKeyDiagramChordRebindTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyDiagramModel defaults;
    SOLKeyDiagram::Build(keys, defaults);
    const TCHAR* controlKeys[] = { TEXT("LeftControl"), TEXT("RightControl") };
    const TCHAR* altKeys[] = { TEXT("LeftAlt"), TEXT("RightAlt") };
    const TCHAR* shiftKeys[] = { TEXT("LeftShift"), TEXT("RightShift") };

    // Ctrl+O to Ctrl+K: still one of both Ctrl keys' chords, the assignment now on K
    keys[KEYDIAGRAMTEST_OVERLAY_MENU] = TEXT("K");
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("OverlayMenu on K"), SOLKeyDiagram::KeyOfBinding(model, KEYDIAGRAMTEST_OVERLAY_MENU),
        SOLKeyboardLayout::FindKey(TEXT("K")));
    for (const TCHAR* keyName : controlKeys)
    {
        const FString what = FString::Printf(TEXT("Ctrl+K: %s"), keyName);
        const FSOLDiagramKey* control = KeyDiagramTestKey(model, keyName);
        const FSOLDiagramKey* defaultControl = KeyDiagramTestKey(defaults, keyName);
        TestTrue(what + TEXT(" on the diagram"), control != nullptr && defaultControl != nullptr);
        if (control != nullptr && defaultControl != nullptr)
        {
            TestTrue(what + TEXT(" still lists the OverlayMenu chord"), KeyDiagramTestChordsHoldBinding(model,
                *control, KEYDIAGRAMTEST_OVERLAY_MENU));
            TestEqual(what + TEXT(" keeps its chord count"), control->ChordCount, defaultControl->ChordCount);
            TestTrue(what + TEXT(" still a modifier"), control->bModifier);
            KeyDiagramTestCheckChords(*this, model, keys, keyName, what);
        }
    }
    const FSOLDiagramKey* k = KeyDiagramTestKey(model, TEXT("K"));
    if (k != nullptr)
    {
        const FSOLKeyAssignment* overlayMenu = KeyDiagramTestFind(model, *k, KEYDIAGRAMTEST_OVERLAY_MENU);
        TestTrue(TEXT("K holds Ctrl+K"), overlayMenu != nullptr && overlayMenu->Chord == EKeyChord::Control);
        TestEqual(TEXT("K starts no chord"), k->ChordCount, 0);
        TestFalse(TEXT("K is no modifier"), k->bModifier);
    }

    // Shift+M to Shift+K: still one of both Shift keys' chords
    keys = SOLKeyBindings::DefaultKeys();
    keys[KEYDIAGRAMTEST_OPEN_MAP] = TEXT("K");
    SOLKeyDiagram::Build(keys, model);
    for (const TCHAR* keyName : shiftKeys)
    {
        const FString what = FString::Printf(TEXT("Shift+K: %s"), keyName);
        const FSOLDiagramKey* shift = KeyDiagramTestKey(model, keyName);
        TestTrue(what + TEXT(" still lists the OpenMap chord"), shift != nullptr
            && KeyDiagramTestChordsHoldBinding(model, *shift, KEYDIAGRAMTEST_OPEN_MAP));
        KeyDiagramTestCheckChords(*this, model, keys, keyName, what);
    }
    KeyDiagramTestCheckDefaultChordSets(*this, model, TEXT("Shift+K"));

    // ThrustDown moved off Left Ctrl: Left Ctrl a modifier with its four chords and no action of its own
    keys = SOLKeyBindings::DefaultKeys();
    keys[KEYDIAGRAMTEST_THRUST_DOWN] = TEXT("K");
    SOLKeyDiagram::Build(keys, model);
    const FSOLDiagramKey* control = KeyDiagramTestKey(model, TEXT("LeftControl"));
    TestTrue(TEXT("ThrustDown on K: Left Ctrl on the diagram"), control != nullptr);
    if (control != nullptr)
    {
        KeyDiagramTestCheckModifier(*this, *control, TEXT("ThrustDown on K: Left Ctrl"));
        KeyDiagramTestCheckKey(*this, model, *control, TEXT("ThrustDown on K: Left Ctrl"));
    }
    KeyDiagramTestCheckDefaultChordSets(*this, model, TEXT("ThrustDown on K"));

    // Alt+O unbound: neither Alt key lists it, and with no other Alt chord neither is a modifier
    keys = SOLKeyBindings::DefaultKeys();
    keys[KEYDIAGRAMTEST_ORBIT_LINES] = FString();
    SOLKeyDiagram::Build(keys, model);
    for (const TCHAR* keyName : altKeys)
    {
        const FString what = FString::Printf(TEXT("Alt+O unbound: %s"), keyName);
        const FSOLDiagramKey* alt = KeyDiagramTestKey(model, keyName);
        TestTrue(what + TEXT(" on the diagram"), alt != nullptr);
        if (alt == nullptr)
        {
            continue;
        }
        TestFalse(what + TEXT(" no longer lists it"), KeyDiagramTestChordsHoldBinding(model, *alt,
            KEYDIAGRAMTEST_ORBIT_LINES));
        const int32 expected = KeyDiagramTestExpectedChords(keys, keyName).Num();
        TestEqual(what + TEXT(" starts the chords CONTROL_ROWS gives it"), alt->ChordCount, expected);
        TestTrue(what + TEXT(" a modifier exactly when it still starts one"), alt->bModifier == (expected > 0));
        KeyDiagramTestCheckKey(*this, model, *alt, what);
    }
    const FSOLDiagramKey* leftAlt = KeyDiagramTestKey(model, TEXT("LeftAlt"));
    TestTrue(TEXT("Alt+O unbound: Left Alt keeps FreeLook"), leftAlt != nullptr
        && KeyDiagramTestFind(model, *leftAlt, KEYDIAGRAMTEST_FREE_LOOK) != nullptr);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramChordOverflowTest, "SOLTest.KeyDiagram.ChordOverflow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A chord binding on a key off the layout: the overflow key starts no chord and is no modifier, and the chord is still
// one of both its modifier keys' (it is a chord assignment starting on them) (D32 revised)
bool FSOLKeyDiagramChordOverflowTest::RunTest(const FString& /*parameters*/)
{
    TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    keys[KEYDIAGRAMTEST_OVERLAY_MENU] = TEXT("F13");
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(keys, model);
    TestEqual(TEXT("one key off the layout"), model.Overflow.Num(), 1);
    if (model.Overflow.Num() == 1)
    {
        TestEqual(TEXT("F13 starts no chord"), model.Overflow[0].ChordCount, 0);
        TestFalse(TEXT("F13 is no modifier"), model.Overflow[0].bModifier);
        TestEqual(TEXT("F13 holds one action"), model.Overflow[0].AssignmentCount, 1);
    }
    const TCHAR* controlKeys[] = { TEXT("LeftControl"), TEXT("RightControl") };
    for (const TCHAR* keyName : controlKeys)
    {
        const FSOLDiagramKey* control = KeyDiagramTestKey(model, keyName);
        TestTrue(FString::Printf(TEXT("%s lists the off-layout Ctrl+F13"), keyName), control != nullptr
            && KeyDiagramTestChordsHoldBinding(model, *control, KEYDIAGRAMTEST_OVERLAY_MENU));
    }
    KeyDiagramTestCheckChordRanges(*this, model, TEXT("Ctrl+F13"));
    KeyDiagramTestCheckDefaultChordSets(*this, model, TEXT("Ctrl+F13"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramAlternativeTest, "SOLTest.KeyDiagram.Alternative",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every assignment's Alternative indexes the '|' alternative of its row that put it there: its binding's '@' id, or a
// fixed chord whose last key is the key holding it
bool FSOLKeyDiagramAlternativeTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    const TArray<FString> keySets[] = { SOLKeyBindings::DefaultKeys(), KeyDiagramTestFreedModifierKeys() };
    const TCHAR* keySetNames[] = { TEXT("defaults"), TEXT("freed modifiers") };
    FSOLKeyDiagramModel model;
    for (int32 set = 0; set < static_cast<int32>(UE_ARRAY_COUNT(keySets)); ++set)
    {
        SOLKeyDiagram::Build(keySets[set], model);

        // The key name holding each assignment (layout keys that are not mirrors, then the overflow)
        TArray<FString> holders;
        holders.SetNum(model.Assignments.Num());
        for (int32 index = 0; index < model.Keys.Num(); ++index)
        {
            const FSOLDiagramKey& key = model.Keys[index];
            if ((layout.IsValidIndex(index) && layout[index].bMirror) || !KeyDiagramTestRangeValid(model, key))
            {
                continue;
            }
            for (int32 assignment = key.FirstAssignment; assignment < key.FirstAssignment + key.AssignmentCount;
                ++assignment)
            {
                holders[assignment] = key.KeyName;
            }
        }
        for (const FSOLDiagramKey& key : model.Overflow)
        {
            if (KeyDiagramTestRangeValid(model, key))
            {
                for (int32 assignment = key.FirstAssignment; assignment < key.FirstAssignment + key.AssignmentCount;
                    ++assignment)
                {
                    holders[assignment] = key.KeyName;
                }
            }
        }

        // Each assignment's alternative names it
        for (int32 index = 0; index < model.Assignments.Num(); ++index)
        {
            const FSOLKeyAssignment& assignment = model.Assignments[index];
            const FString what = FString::Printf(TEXT("%s: assignment %d (row %d)"), keySetNames[set], index,
                assignment.Row);
            const TArray<FString> alternatives = KeyDiagramTestAlternatives(MakeArrayView(SOLMenuText::CONTROL_ROWS),
                assignment.Row);
            TestTrue(what + TEXT(": alternative in range"), alternatives.IsValidIndex(assignment.Alternative));
            if (!alternatives.IsValidIndex(assignment.Alternative))
            {
                continue;
            }
            const FString& text = alternatives[assignment.Alternative];
            if (assignment.Binding != INDEX_NONE)
            {
                TestTrue(what + TEXT(": a binding's alternative is its '@' id"), text.StartsWith(TEXT("@"))
                    && SOLKeyBindings::FindById(text.RightChop(1)) == assignment.Binding);
                continue;
            }
            TestFalse(what + TEXT(": a fixed key's alternative is no '@' id"), text.StartsWith(TEXT("@")));
            TArray<FString> chordKeys;
            text.ParseIntoArray(chordKeys, SOLMenuText::BINDING_CHORD_DELIMITER);
            const int32 holder = SOLKeyboardLayout::FindKey(holders[index]);
            TestTrue(what + TEXT(": the fixed chord's last key holds it"), chordKeys.Num() > 0 && holder != INDEX_NONE
                && SOLKeyboardLayout::FindKey(chordKeys.Last()) == holder);
        }
    }

    // A two-alternative row: each alternative its own index
    const int32 warpRow = KeyDiagramTestRowOf(TEXT("@WarpDown|@WarpUp"));
    TestTrue(TEXT("the warp row exists"), warpRow != INDEX_NONE);
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    for (const FSOLKeyAssignment& assignment : model.Assignments)
    {
        if (assignment.Row == warpRow)
        {
            const int32 expected = assignment.Binding == static_cast<int32>(SOLKeyBindings::EBinding::WarpUp) ? 1 : 0;
            TestEqual(TEXT("warp alternative"), assignment.Alternative, expected);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramDetailAssignmentsTest, "SOLTest.KeyDiagram.DetailAssignments",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The hover strip's assignments of a key (D32 revised): its own actions first (the primary, then the others in model
// order), then the chords it starts in order; a mirror gives its main key's; nothing for a free key or an index out of
// range; the count returned. At the defaults Left Shift gives its 2 own then 4 chords, Right Shift its 5 chords, Left
// Ctrl ThrustDown then its 4 chords
bool FSOLKeyDiagramDetailAssignmentsTest::RunTest(const FString& /*parameters*/)
{
    const TArray<FString> keySets[] = { SOLKeyBindings::DefaultKeys(), KeyDiagramTestFreedModifierKeys() };
    const TCHAR* keySetNames[] = { TEXT("defaults"), TEXT("freed modifiers") };
    FSOLKeyDiagramModel model;
    TArray<int32> actual;
    TArray<int32> expected;
    for (int32 set = 0; set < static_cast<int32>(UE_ARRAY_COUNT(keySets)); ++set)
    {
        SOLKeyDiagram::Build(keySets[set], model);
        int32 modifiers = 0;
        for (int32 index = 0; index < model.Keys.Num(); ++index)
        {
            const FSOLDiagramKey& key = model.Keys[index];
            const FString what = FString::Printf(TEXT("%s: key %d '%s'"), keySetNames[set], index, *key.KeyName);
            const int32 count = SOLKeyDiagram::DetailAssignments(model, index, actual);
            KeyDiagramTestExpectedDetail(model, index, expected);
            TestEqual(what + TEXT(": returns the count"), count, actual.Num());
            TestEqual(what + TEXT(": line count"), actual.Num(), expected.Num());
            if (actual.Num() == expected.Num())
            {
                for (int32 line = 0; line < actual.Num(); ++line)
                {
                    TestEqual(FString::Printf(TEXT("%s: line %d"), *what, line), actual[line], expected[line]);
                }
            }
            TestEqual(what + TEXT(": its own actions plus the chords it starts"), actual.Num(),
                key.AssignmentCount + key.ChordCount);
            modifiers += key.bModifier ? 1 : 0;
        }
        TestTrue(FString::Printf(TEXT("%s: a modifier key exists"), keySetNames[set]), modifiers > 0);
    }

    // The defaults: own actions (primary first), then the chord range in order
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    const TCHAR* spotKeys[] = { TEXT("LeftShift"), TEXT("RightShift"), TEXT("LeftControl") };
    const int32 spotOwn[] = { 2, 0, 1 };
    const int32 spotChords[] = { 4, 5, 4 };
    const int32 spotPrimary[] = { KEYDIAGRAMTEST_THRUST_UP, INDEX_NONE, KEYDIAGRAMTEST_THRUST_DOWN };
    for (int32 spot = 0; spot < static_cast<int32>(UE_ARRAY_COUNT(spotKeys)); ++spot)
    {
        const FString what = FString::Printf(TEXT("defaults: %s"), spotKeys[spot]);
        const int32 index = SOLKeyboardLayout::FindKey(spotKeys[spot]);
        TestTrue(what + TEXT(" on the diagram"), model.Keys.IsValidIndex(index));
        if (!model.Keys.IsValidIndex(index))
        {
            continue;
        }
        const FSOLDiagramKey& key = model.Keys[index];
        SOLKeyDiagram::DetailAssignments(model, index, actual);
        TestEqual(what + TEXT(": own lines then chord lines"), actual.Num(), spotOwn[spot] + spotChords[spot]);
        if (actual.Num() != spotOwn[spot] + spotChords[spot])
        {
            continue;
        }
        if (spotPrimary[spot] != INDEX_NONE)
        {
            TestEqual(what + TEXT(": the primary first"), actual[0], key.Primary);
            TestTrue(what + TEXT(": the primary is its own action"), model.Assignments.IsValidIndex(actual[0])
                && model.Assignments[actual[0]].Binding == spotPrimary[spot]);
        }
        for (int32 line = 0; line < spotOwn[spot]; ++line)
        {
            TestTrue(FString::Printf(TEXT("%s: line %d one of its own"), *what, line),
                actual[line] >= key.FirstAssignment && actual[line] < key.FirstAssignment + key.AssignmentCount);
        }
        for (int32 line = 0; line < spotChords[spot]; ++line)
        {
            const int32 chord = key.FirstChord + line;
            TestTrue(FString::Printf(TEXT("%s: line %d its chord %d"), *what, spotOwn[spot] + line, line),
                model.Chords.IsValidIndex(chord) && actual[spotOwn[spot] + line] == model.Chords[chord]);
        }
    }

    // Right Shift's lines include RShift+O; Left Shift's do not
    const int32 rightShift = SOLKeyboardLayout::FindKey(TEXT("RightShift"));
    SOLKeyDiagram::DetailAssignments(model, rightShift, actual);
    TestTrue(TEXT("Right Shift: lists RShift+O"), actual.ContainsByPredicate([&model](const int32 assignment)
        {
            return model.Assignments.IsValidIndex(assignment)
                && model.Assignments[assignment].Binding == KEYDIAGRAMTEST_CYCLE_PROFILE;
        }));
    SOLKeyDiagram::DetailAssignments(model, SOLKeyboardLayout::FindKey(TEXT("LeftShift")), actual);
    TestFalse(TEXT("Left Shift: does not list RShift+O"), actual.ContainsByPredicate([&model](const int32 assignment)
        {
            return model.Assignments.IsValidIndex(assignment)
                && model.Assignments[assignment].Binding == KEYDIAGRAMTEST_CYCLE_PROFILE;
        }));

    // A mirror key gives its main key's lines
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    TArray<int32> mainLines;
    for (int32 index = 0; index < layout.Num(); ++index)
    {
        if (layout[index].bMirror)
        {
            SOLKeyDiagram::DetailAssignments(model, index, actual);
            SOLKeyDiagram::DetailAssignments(model, SOLKeyboardLayout::FindKey(layout[index].KeyName), mainLines);
            TestTrue(FString::Printf(TEXT("mirror %d '%s': its main key's lines"), index, layout[index].KeyName),
                actual == mainLines);
        }
    }

    // A free key and indices out of range empty a filled array
    const int32 freeKey = SOLKeyboardLayout::FindKey(TEXT("K"));
    TestTrue(TEXT("K is free at the defaults"), model.Keys.IsValidIndex(freeKey)
        && model.Keys[freeKey].AssignmentCount == 0 && model.Keys[freeKey].ChordCount == 0);
    const int32 outOfRange[] = { freeKey, -1, model.Keys.Num(), MAX_int32 };
    for (const int32 index : outOfRange)
    {
        actual = { 1, 2, 3 };
        TestEqual(FString::Printf(TEXT("key %d: returns 0"), index), SOLKeyDiagram::DetailAssignments(model, index,
            actual), 0);
        TestEqual(FString::Printf(TEXT("key %d: emptied"), index), actual.Num(), 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramDetailReuseTest, "SOLTest.KeyDiagram.DetailAssignmentsReuse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DetailAssignments resets its output keeping the capacity: a hover over many keys allocates nothing once reserved
bool FSOLKeyDiagramDetailReuseTest::RunTest(const FString& /*parameters*/)
{
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    TArray<int32> out;
    out.Reserve(SOL::Help::DIAGRAM_DETAIL_LINES * 4);
    const void* data = out.GetData();
    const SIZE_T size = out.GetAllocatedSize();
    const TCHAR* keyNames[] = { TEXT("RightMouseButton"), TEXT("O"), TEXT("K"), TEXT("RightShift"), TEXT("M"),
        TEXT("LeftShift"), TEXT("LeftControl") };
    for (const TCHAR* keyName : keyNames)
    {
        SOLKeyDiagram::DetailAssignments(model, SOLKeyboardLayout::FindKey(keyName), out);
        TestTrue(FString::Printf(TEXT("%s: buffer kept"), keyName), out.GetData() == data);
        TestEqual(FString::Printf(TEXT("%s: capacity kept"), keyName), static_cast<int64>(out.GetAllocatedSize()),
            static_cast<int64>(size));
    }
    SOLKeyDiagram::DetailAssignments(model, INDEX_NONE, out);
    TestEqual(TEXT("out of range: capacity kept"), static_cast<int64>(out.GetAllocatedSize()),
        static_cast<int64>(size));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramDetailLineTest, "SOLTest.KeyDiagram.DetailLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A hover strip line shows how the player presses one alternative (FormatBindings at the live keys: "Alt+O",
// "Ctrl+F1"), the row's action (FormatControlAction), the area and the fixed flag, for every assignment at the defaults
// and at rebound keys; a chord listed under Right Ctrl or Right Shift reads the row's own alternative at the live keys,
// as under the left side; an assignment out of range gives an empty line
bool FSOLKeyDiagramDetailLineTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<SOLMenuText::FControlRow> rows = MakeArrayView(SOLMenuText::CONTROL_ROWS);
    TArray<FString> rebound = KeyDiagramTestFreedModifierKeys();
    rebound[KEYDIAGRAMTEST_FIRE] = TEXT("Y");
    rebound[KEYDIAGRAMTEST_ORBIT_LINES] = TEXT("I");
    const TArray<FString> keySets[] = { SOLKeyBindings::DefaultKeys(), rebound };
    const TCHAR* keySetNames[] = { TEXT("defaults"), TEXT("rebound") };
    FSOLKeyDiagramModel model;
    FSOLDiagramDetailLine line;
    FSOLDiagramDetailLine lineDefaultRows;
    for (int32 set = 0; set < static_cast<int32>(UE_ARRAY_COUNT(keySets)); ++set)
    {
        SOLKeyDiagram::Build(keySets[set], model);
        for (int32 assignment = 0; assignment < model.Assignments.Num(); ++assignment)
        {
            const FString what = FString::Printf(TEXT("%s: assignment %d"), keySetNames[set], assignment);
            SOLKeyDiagram::DetailLine(rows, model, assignment, keySets[set], line);
            KeyDiagramTestCheckLine(*this, rows, model, assignment, keySets[set], line, what);

            // The CONTROL_ROWS overload gives the same line
            SOLKeyDiagram::DetailLine(model, assignment, keySets[set], lineDefaultRows);
            TestEqual(what + TEXT(": default rows keys"), lineDefaultRows.Keys, line.Keys);
            TestEqual(what + TEXT(": default rows action"), lineDefaultRows.Action, line.Action);
            TestTrue(what + TEXT(": default rows area"), lineDefaultRows.Area == line.Area);
            TestTrue(what + TEXT(": default rows fixed"), lineDefaultRows.bFixed == line.bFixed);
        }
    }

    // Spot checks at the defaults: one alternative only, never the whole cell
    const TArray<FString> defaults = SOLKeyBindings::DefaultKeys();
    SOLKeyDiagram::Build(defaults, model);
    const FSOLDiagramKey* o = KeyDiagramTestKey(model, TEXT("O"));
    if (o != nullptr && KeyDiagramTestRangeValid(model, *o))
    {
        for (int32 index = o->FirstAssignment; index < o->FirstAssignment + o->AssignmentCount; ++index)
        {
            if (model.Assignments[index].Binding == KEYDIAGRAMTEST_ORBIT_LINES)
            {
                SOLKeyDiagram::DetailLine(model, index, defaults, line);
                TestEqual(TEXT("Alt+O keys"), line.Keys, SOLMenuContent::FormatBindings(TEXT("@ToggleOrbitLines"),
                    defaults));
                TestTrue(TEXT("Alt+O area"), line.Area == ESOLControlArea::Overlays);
                TestFalse(TEXT("Alt+O rebindable"), line.bFixed);
            }
        }
    }
    const FSOLDiagramKey* f1 = KeyDiagramTestKey(model, TEXT("F1"));
    if (f1 != nullptr && model.Assignments.IsValidIndex(f1->Primary))
    {
        SOLKeyDiagram::DetailLine(model, f1->Primary, defaults, line);
        TestEqual(TEXT("Ctrl+F1 keys"), line.Keys, SOLMenuContent::FormatBindings(TEXT("LeftControl+F1"), defaults));
        TestTrue(TEXT("Ctrl+F1 fixed"), line.bFixed);
        TestTrue(TEXT("Ctrl+F1 area"), line.Area == ESOLControlArea::Menu);
    }
    const FSOLDiagramKey* leftMouse = KeyDiagramTestKey(model, TEXT("LeftMouseButton"));
    if (leftMouse != nullptr && KeyDiagramTestRangeValid(model, *leftMouse))
    {
        for (int32 index = leftMouse->FirstAssignment; index < leftMouse->FirstAssignment
            + leftMouse->AssignmentCount; ++index)
        {
            SOLKeyDiagram::DetailLine(model, index, defaults, line);
            TestFalse(TEXT("an LMB line names one alternative, no ' / '"),
                line.Keys.Contains(SOLMenuText::BINDING_ALTERNATIVE_SEPARATOR));
        }
    }

    // The chords listed under the right-side modifiers read the row's own alternative at the live keys, the same line
    // as under the left side (D32 revised): Right Ctrl's Ctrl+F1 is "LeftControl+F1" formatted, Right Shift's
    // RShift+O the "@CycleOverlayProfile" alternative
    TArray<int32> listed;
    const TCHAR* rightModifiers[] = { TEXT("RightControl"), TEXT("RightShift") };
    const TCHAR* leftModifiers[] = { TEXT("LeftControl"), TEXT("LeftShift") };
    for (int32 side = 0; side < static_cast<int32>(UE_ARRAY_COUNT(rightModifiers)); ++side)
    {
        SOLKeyDiagram::DetailAssignments(model, SOLKeyboardLayout::FindKey(rightModifiers[side]), listed);
        TestTrue(FString::Printf(TEXT("%s lists chords"), rightModifiers[side]), listed.Num() > 0);
        TArray<int32> leftListed;
        SOLKeyDiagram::DetailAssignments(model, SOLKeyboardLayout::FindKey(leftModifiers[side]), leftListed);
        for (const int32 assignment : listed)
        {
            const FString what = FString::Printf(TEXT("%s: assignment %d"), rightModifiers[side], assignment);
            SOLKeyDiagram::DetailLine(model, assignment, defaults, line);
            KeyDiagramTestCheckLine(*this, rows, model, assignment, defaults, line, what);
            const bool bShared = model.Assignments.IsValidIndex(assignment)
                && model.Assignments[assignment].Binding != KEYDIAGRAMTEST_CYCLE_PROFILE;
            if (bShared)
            {
                TestTrue(what + TEXT(": also listed under the left side"), leftListed.Contains(assignment));
            }
        }
    }
    const int32 menuRow = KeyDiagramTestRowOf(TEXT("LeftControl+F1"));
    SOLKeyDiagram::DetailAssignments(model, SOLKeyboardLayout::FindKey(TEXT("RightControl")), listed);
    bool bRightCtrlF1 = false;
    for (const int32 assignment : listed)
    {
        if (model.Assignments.IsValidIndex(assignment) && model.Assignments[assignment].Row == menuRow)
        {
            bRightCtrlF1 = true;
            SOLKeyDiagram::DetailLine(model, assignment, defaults, line);
            TestEqual(TEXT("Right Ctrl: Ctrl+F1 keys"), line.Keys,
                SOLMenuContent::FormatBindings(TEXT("LeftControl+F1"), defaults));
            TestTrue(TEXT("Right Ctrl: Ctrl+F1 fixed"), line.bFixed);
        }
    }
    TestTrue(TEXT("Right Ctrl lists Ctrl+F1"), bRightCtrlF1);
    SOLKeyDiagram::DetailAssignments(model, SOLKeyboardLayout::FindKey(TEXT("RightShift")), listed);
    bool bRightShiftO = false;
    for (const int32 assignment : listed)
    {
        if (model.Assignments.IsValidIndex(assignment)
            && model.Assignments[assignment].Binding == KEYDIAGRAMTEST_CYCLE_PROFILE)
        {
            bRightShiftO = true;
            SOLKeyDiagram::DetailLine(model, assignment, defaults, line);
            TestEqual(TEXT("Right Shift: RShift+O keys"), line.Keys,
                SOLMenuContent::FormatBindings(TEXT("@CycleOverlayProfile"), defaults));
            TestFalse(TEXT("Right Shift: RShift+O rebindable"), line.bFixed);
        }
    }
    TestTrue(TEXT("Right Shift lists RShift+O"), bRightShiftO);

    // Out of range: an empty line, over a filled one
    const int32 outOfRange[] = { -1, model.Assignments.Num(), MAX_int32 };
    for (const int32 assignment : outOfRange)
    {
        line.Keys = TEXT("stale");
        line.Action = TEXT("stale");
        line.Area = ESOLControlArea::Combat;
        line.bFixed = true;
        SOLKeyDiagram::DetailLine(model, assignment, defaults, line);
        const FString what = FString::Printf(TEXT("assignment %d"), assignment);
        TestTrue(what + TEXT(": no keys"), line.Keys.IsEmpty());
        TestTrue(what + TEXT(": no action"), line.Action.IsEmpty());
        TestTrue(what + TEXT(": no area"), line.Area == ESOLControlArea::Count);
        TestFalse(what + TEXT(": not fixed"), line.bFixed);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramDetailLineRowsTest, "SOLTest.KeyDiagram.DetailLineCustomRows",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// DetailLine over a given row table reads that table's cells and actions, one alternative per line; the table's fixed
// Ctrl+F1 starts on both Ctrl keys
bool FSOLKeyDiagramDetailLineRowsTest::RunTest(const FString& /*parameters*/)
{
    const SOLMenuText::FControlRow rows[] = {
        { TEXT("Combat"), TEXT("@Fire|@DropTarget"), TEXT("Fire or drop {DropDistance} ahead") },
        { TEXT("Menu"), TEXT("LeftControl+F1"), TEXT("Menu") },
    };
    const TArray<FString> keys = SOLKeyBindings::DefaultKeys();
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(rows, keys, model);
    TestEqual(TEXT("three assignments"), model.Assignments.Num(), 3);
    FSOLDiagramDetailLine line;
    for (int32 assignment = 0; assignment < model.Assignments.Num(); ++assignment)
    {
        SOLKeyDiagram::DetailLine(rows, model, assignment, keys, line);
        KeyDiagramTestCheckLine(*this, rows, model, assignment, keys, line,
            FString::Printf(TEXT("custom assignment %d"), assignment));
        const FSOLKeyAssignment& source = model.Assignments[assignment];
        if (source.Binding == KEYDIAGRAMTEST_DROP)
        {
            TestEqual(TEXT("DropTarget: only its own alternative"), line.Keys,
                SOLMenuContent::FormatBindings(TEXT("@DropTarget"), keys));
            TestEqual(TEXT("DropTarget: the action filled"), line.Action,
                SOLMenuContent::FormatControlAction(rows[0].Action));
        }
    }

    // The custom Ctrl+F1 is one of both Ctrl keys' chords (D32 revised); no other key starts one
    const TCHAR* controlKeys[] = { TEXT("LeftControl"), TEXT("RightControl") };
    for (const TCHAR* keyName : controlKeys)
    {
        const FSOLDiagramKey* control = KeyDiagramTestKey(model, keyName);
        TestTrue(FString::Printf(TEXT("custom rows: %s starts the Ctrl+F1 chord"), keyName), control != nullptr
            && control->ChordCount == 1);
        TestTrue(FString::Printf(TEXT("custom rows: %s is a modifier"), keyName), control != nullptr
            && control->bModifier);
    }
    TestEqual(TEXT("custom rows: two chord entries"), model.Chords.Num(), 2);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramMaxDetailCountTest, "SOLTest.KeyDiagram.MaxDetailCount",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MaxDetailCount (contract E.5, D36) is the largest DetailAssignments count over every model key, mirrors included: at
// the defaults, with the modifiers freed, after a rebind that piles actions onto one key (it follows the model), and 0
// for an empty model or one built from no rows
bool FSOLKeyDiagramMaxDetailCountTest::RunTest(const FString& /*parameters*/)
{
    // Returns the expected budget: DetailAssignments of every key, the largest count
    auto expectedMax = [](const FSOLKeyDiagramModel& model)
    {
        TArray<int32> scratch;
        int32 best = 0;
        for (int32 key = 0; key < model.Keys.Num(); ++key)
        {
            best = FMath::Max(best, SOLKeyDiagram::DetailAssignments(model, key, scratch));
        }
        return best;
    };

    // The defaults and the freed modifiers
    FSOLKeyDiagramModel model;
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    const int32 defaultMax = expectedMax(model);
    TestTrue(TEXT("defaults: some key has lines"), defaultMax > 0);
    TestEqual(TEXT("defaults: the busiest key's count"), SOLKeyDiagram::MaxDetailCount(model), defaultMax);
    SOLKeyDiagram::Build(KeyDiagramTestFreedModifierKeys(), model);
    TestEqual(TEXT("freed modifiers: the busiest key's count"), SOLKeyDiagram::MaxDetailCount(model),
        expectedMax(model));

    // A rebind piling more bindings than the busiest default key has lines onto K: the budget follows the model
    TArray<FString> piled = SOLKeyBindings::DefaultKeys();
    const int32 pileCount = FMath::Min(defaultMax + 1, SOLKeyBindings::BINDING_COUNT);
    for (int32 binding = 0; binding < pileCount; ++binding)
    {
        piled[binding] = TEXT("K");
    }
    SOLKeyDiagram::Build(piled, model);
    const int32 piledMax = expectedMax(model);
    TestTrue(TEXT("piled onto K: the busiest key is busier than at the defaults"), piledMax > defaultMax);
    TestEqual(TEXT("piled onto K: the busiest key's count"), SOLKeyDiagram::MaxDetailCount(model), piledMax);
    TArray<int32> kLines;
    TestTrue(TEXT("piled onto K: at least K's count"), SOLKeyDiagram::MaxDetailCount(model)
        >= SOLKeyDiagram::DetailAssignments(model, SOLKeyboardLayout::FindKey(TEXT("K")), kLines));

    // Back to the defaults
    SOLKeyDiagram::Build(SOLKeyBindings::DefaultKeys(), model);
    TestEqual(TEXT("defaults again: the busiest key's count"), SOLKeyDiagram::MaxDetailCount(model), defaultMax);

    // Empty: a default model, and a model built from no rows
    TestEqual(TEXT("an empty model: 0"), SOLKeyDiagram::MaxDetailCount(FSOLKeyDiagramModel()), 0);
    FSOLKeyDiagramModel noRows;
    SOLKeyDiagram::Build(TConstArrayView<SOLMenuText::FControlRow>(), SOLKeyBindings::DefaultKeys(), noRows);
    TestEqual(TEXT("no rows: 0"), SOLKeyDiagram::MaxDetailCount(noRows), expectedMax(noRows));
    TestEqual(TEXT("no rows: nothing to list"), SOLKeyDiagram::MaxDetailCount(noRows), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLKeyDiagramLegendAbbreviationsTest, "SOLTest.KeyDiagram.LegendAbbreviations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D38: every DIAGRAM_LEGEND_ABBREVIATIONS entry names a key on the layout (once), is non-empty, at most
// DIAGRAM_ABBREVIATION_MAX_CHARS long and shorter than that key's layout legend; LegendAbbreviation finds each entry
// (case-insensitive) and returns nullptr for a key without one
bool FSOLKeyDiagramLegendAbbreviationsTest::RunTest(const FString& /*parameters*/)
{
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    TestTrue(TEXT("the table has entries"), UE_ARRAY_COUNT(SOLMenuText::DIAGRAM_LEGEND_ABBREVIATIONS) > 0);
    TSet<FString> seen;
    for (const SOLMenuText::FDiagramLegendAbbreviation& entry : SOLMenuText::DIAGRAM_LEGEND_ABBREVIATIONS)
    {
        const FString keyName(entry.KeyName);
        const FString legend(entry.Legend);
        bool bDuplicate = false;
        seen.Add(keyName, &bDuplicate);
        TestFalse(*FString::Printf(TEXT("%s: listed once"), *keyName), bDuplicate);

        // The layout keys of that name (a mirror shares its main key's name): the shortest legend bounds the entry
        int32 shortestLegend = MAX_int32;
        for (const FSOLLayoutKey& key : layout)
        {
            shortestLegend = keyName.Equals(key.KeyName, ESearchCase::IgnoreCase)
                ? FMath::Min(shortestLegend, FCString::Strlen(key.Legend)) : shortestLegend;
        }
        TestTrue(*FString::Printf(TEXT("%s: a layout key"), *keyName), shortestLegend != MAX_int32);
        TestTrue(*FString::Printf(TEXT("%s: not empty"), *keyName), !legend.IsEmpty());
        TestTrue(*FString::Printf(TEXT("%s: at most %d characters"), *keyName,
            SOL::Help::DIAGRAM_ABBREVIATION_MAX_CHARS), legend.Len() <= SOL::Help::DIAGRAM_ABBREVIATION_MAX_CHARS);
        TestTrue(*FString::Printf(TEXT("%s: shorter than its layout legend"), *keyName), legend.Len() < shortestLegend);
        const TCHAR* found = SOLKeyDiagram::LegendAbbreviation(keyName.ToLower());
        TestTrue(*FString::Printf(TEXT("%s: found (any case)"), *keyName), found != nullptr && legend.Equals(found));
    }
    TestNull(TEXT("A has none"), SOLKeyDiagram::LegendAbbreviation(TEXT("A")));
    TestNull(TEXT("an empty name has none"), SOLKeyDiagram::LegendAbbreviation(FStringView()));
    return true;
}

#endif
