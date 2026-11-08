/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyDiagram.h"

#include "Menu/SOLKeyboardLayout.h"
#include "Menu/SOLMenuContent.h"
#include "SOLConstants.h"

#include "String/ParseTokens.h"

static_assert(UE_ARRAY_COUNT(SOLMenuText::KEY_SHORT_LABELS) == SOLKeyBindings::BINDING_COUNT,
    "one short label per rebindable action (SOLKeyBindings::EBinding order)");
static_assert(UE_ARRAY_COUNT(SOL::Help::AREA_COLORS) == static_cast<int32>(ESOLControlArea::Count),
    "one area colour per Controls area");

namespace
{
    // The areas as SOLMenuText::CONTROL_ROWS writes them, in ESOLControlArea order
    const TCHAR* const KEY_DIAGRAM_AREA_NAMES[] = { TEXT("Flight"), TEXT("Targeting"), TEXT("Combat"), TEXT("Time"),
        TEXT("Overlays"), TEXT("HUD"), TEXT("Map"), TEXT("Routes"), TEXT("Menu"), TEXT("Demo") };
    static_assert(UE_ARRAY_COUNT(KEY_DIAGRAM_AREA_NAMES) == static_cast<int32>(ESOLControlArea::Count),
        "one name per Controls area");

    // The modifier key names a fixed '+' chord may start with, and the chord each gives
    struct FKeyDiagramModifier
    {
        const TCHAR* KeyName;
        SOLKeyBindings::EKeyChord Chord;
    };

    constexpr FKeyDiagramModifier KEY_DIAGRAM_MODIFIERS[] =
    {
        { TEXT("LeftShift"), SOLKeyBindings::EKeyChord::Shift },
        { TEXT("RightShift"), SOLKeyBindings::EKeyChord::RightShift },
        { TEXT("LeftAlt"), SOLKeyBindings::EKeyChord::Alt },
        { TEXT("RightAlt"), SOLKeyBindings::EKeyChord::Alt },
        { TEXT("LeftControl"), SOLKeyBindings::EKeyChord::Control },
        { TEXT("RightControl"), SOLKeyBindings::EKeyChord::Control },
    };

    // One action the rows put on a key, before it is placed: the assignment and the key it sits on ("" for an
    // unbound binding)
    struct FKeyDiagramPending
    {
        FSOLKeyAssignment Assignment;
        FStringView KeyName;
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns a fixed key's short label from its name (case-insensitive), or ""
    const TCHAR* KeyDiagramFixedLabel(const FStringView keyName)
    {
        for (const SOLMenuText::FFixedKeyShortLabel& entry : SOLMenuText::FIXED_KEY_SHORT_LABELS)
        {
            if (keyName.Equals(entry.KeyName, ESearchCase::IgnoreCase))
            {
                return entry.Label;
            }
        }
        return TEXT("");
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the chord a fixed '+' chord's first key gives when the chord has more than one key (None otherwise)
    SOLKeyBindings::EKeyChord KeyDiagramFixedChord(const FStringView firstKey, const int32 keyCount)
    {
        if (keyCount < 2)
        {
            return SOLKeyBindings::EKeyChord::None;
        }
        for (const FKeyDiagramModifier& modifier : KEY_DIAGRAM_MODIFIERS)
        {
            if (firstKey.Equals(modifier.KeyName, ESearchCase::IgnoreCase))
            {
                return modifier.Chord;
            }
        }
        return SOLKeyBindings::EKeyChord::None;
    }

    //////////////////////////////////////////////////////////////////////////
    // Calls visit with every action the rows name, in row and cell order: each '@' alternative on its binding's live
    // key ("" when it has none), each fixed '+' chord on its last key. Unknown ids are skipped
    template <typename FVisit>
    void KeyDiagramForEachAction(const TConstArrayView<SOLMenuText::FControlRow> rows, const TArray<FString>& liveKeys,
        FVisit&& visit)
    {
        for (int32 row = 0; row < rows.Num(); ++row)
        {
            const ESOLControlArea area = SOLKeyDiagram::AreaFromName(rows[row].Area);
            const FStringView cell = rows[row].Bindings != nullptr ? FStringView(rows[row].Bindings) : FStringView();
            const TCHAR alternativeDelimiter = SOLMenuText::BINDING_ALTERNATIVE_DELIMITER[0];
            // Empty alternatives are not skipped by the parse so the index matches a plain '|' split
            int32 alternativeIndex = -1;
            UE::String::ParseTokens(cell, alternativeDelimiter, [&](const FStringView alternative)
            {
                ++alternativeIndex;
                if (alternative.IsEmpty())
                {
                    return;
                }
                FKeyDiagramPending pending;
                pending.Assignment.Alternative = alternativeIndex;
                pending.Assignment.Row = row;
                pending.Assignment.Area = area;
                pending.Assignment.bInFlight = area != ESOLControlArea::Map;

                // '@Id': the binding's live key and chord
                if (alternative[0] == SOLMenuText::BINDING_ID_PREFIX)
                {
                    const int32 binding = SOLKeyBindings::FindById(alternative.RightChop(1));
                    if (binding == INDEX_NONE)
                    {
                        return;
                    }
                    pending.Assignment.Binding = binding;
                    pending.Assignment.Chord = SOLKeyBindings::GetChord(binding);
                    pending.Assignment.ShortLabel = SOLKeyDiagram::BindingShortLabel(binding);
                    pending.KeyName = liveKeys.IsValidIndex(binding) ? FStringView(liveKeys[binding]) : FStringView();
                    visit(pending);
                    return;
                }

                // A fixed chord: its last key, the modifier before it as the chord
                FStringView firstKey;
                FStringView lastKey;
                int32 keyCount = 0;
                UE::String::ParseTokens(alternative, SOLMenuText::BINDING_CHORD_DELIMITER[0], [&](const FStringView key)
                {
                    firstKey = keyCount == 0 ? key : firstKey;
                    lastKey = key;
                    ++keyCount;
                }, UE::String::EParseTokensOptions::SkipEmpty);
                if (keyCount == 0)
                {
                    return;
                }
                pending.Assignment.bFixed = true;
                pending.Assignment.Chord = KeyDiagramFixedChord(firstKey, keyCount);
                pending.Assignment.ShortLabel = KeyDiagramFixedLabel(lastKey);
                pending.KeyName = lastKey;
                visit(pending);
            }, UE::String::EParseTokensOptions::None);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the n-th '|' alternative of a bindings cell (a plain split, empty alternatives counted), or an empty view
    FStringView KeyDiagramAlternative(const TCHAR* cell, const int32 n)
    {
        FStringView found;
        int32 index = 0;
        const FStringView view = cell != nullptr ? FStringView(cell) : FStringView();
        UE::String::ParseTokens(view, SOLMenuText::BINDING_ALTERNATIVE_DELIMITER[0], [&](const FStringView alternative)
        {
            if (index++ == n)
            {
                found = alternative;
            }
        }, UE::String::EParseTokensOptions::None);
        return found;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the overflow key with a name (case-insensitive) among the first count, or INDEX_NONE
    int32 KeyDiagramFindOverflow(const FSOLKeyDiagramModel& model, const int32 count, const FStringView keyName)
    {
        for (int32 index = 0; index < count; ++index)
        {
            if (keyName.Equals(model.Overflow[index].KeyName, ESearchCase::IgnoreCase))
            {
                return index;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Clears a diagram key to a free key at an assignment offset, keeping its name's buffer
    void KeyDiagramClearKey(FSOLDiagramKey& key, const int32 firstAssignment)
    {
        key.FirstAssignment = firstAssignment;
        key.AssignmentCount = 0;
        key.Primary = INDEX_NONE;
        key.ExtraCount = 0;
        key.Area = ESOLControlArea::Count;
        key.ShortLabel = TEXT("");
        key.bLocked = false;
        key.bModifier = false;
        key.FirstChord = 0;
        key.ChordCount = 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets a key's primary (the first plain in-flight action, else the first row: D18), badge, tint, label and lock
    void KeyDiagramFinishKey(const FSOLKeyDiagramModel& model, FSOLDiagramKey& key)
    {
        if (key.AssignmentCount == 0)
        {
            KeyDiagramClearKey(key, key.FirstAssignment);
            return;
        }
        const int32 last = key.FirstAssignment + key.AssignmentCount;
        int32 primary = INDEX_NONE;
        int32 firstRow = INDEX_NONE;
        bool bAllFixed = true;
        for (int32 index = key.FirstAssignment; index < last; ++index)
        {
            const FSOLKeyAssignment& assignment = model.Assignments[index];
            bAllFixed &= assignment.bFixed;
            if (primary == INDEX_NONE && assignment.Chord == SOLKeyBindings::EKeyChord::None && assignment.bInFlight)
            {
                primary = index;
            }
            if (firstRow == INDEX_NONE || assignment.Row < model.Assignments[firstRow].Row)
            {
                firstRow = index;
            }
        }
        key.Primary = primary != INDEX_NONE ? primary : firstRow;
        key.ExtraCount = key.AssignmentCount - 1;
        key.Area = model.Assignments[key.Primary].Area;
        key.ShortLabel = model.Assignments[key.Primary].ShortLabel;
        key.bLocked = bAllFixed;
    }
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the diagram model from the rows and the live keys, reusing the model's buffers
void SOLKeyDiagram::Build(const TConstArrayView<SOLMenuText::FControlRow> rows, const TArray<FString>& liveKeys,
    FSOLKeyDiagramModel& out)
{
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();

    // One model key per layout key, named as the layout (the names are written once, then kept)
    out.Keys.SetNum(layout.Num(), EAllowShrinking::No);
    for (int32 index = 0; index < layout.Num(); ++index)
    {
        FSOLDiagramKey& key = out.Keys[index];
        if (FCString::Strcmp(*key.KeyName, layout[index].KeyName) != 0)
        {
            key.KeyName.Reset();
            key.KeyName.Append(layout[index].KeyName);
        }
        KeyDiagramClearKey(key, 0);
    }
    out.Unbound.Reset();

    // Pass 1: count the actions per key, find the keys off the layout (first-row order) and the unbound bindings
    int32 overflowCount = 0;
    KeyDiagramForEachAction(rows, liveKeys, [&](const FKeyDiagramPending& pending)
    {
        if (pending.KeyName.IsEmpty())
        {
            out.Unbound.AddUnique(pending.Assignment.Binding);
            return;
        }
        const int32 index = SOLKeyboardLayout::FindKey(pending.KeyName);
        if (index != INDEX_NONE)
        {
            ++out.Keys[index].AssignmentCount;
            return;
        }
        int32 overflow = KeyDiagramFindOverflow(out, overflowCount, pending.KeyName);
        if (overflow == INDEX_NONE)
        {
            if (overflowCount == out.Overflow.Num())
            {
                out.Overflow.AddDefaulted();
            }
            overflow = overflowCount++;
            FSOLDiagramKey& key = out.Overflow[overflow];
            key.KeyName.Reset();
            key.KeyName.Append(pending.KeyName.GetData(), pending.KeyName.Len());
            KeyDiagramClearKey(key, 0);
        }
        ++out.Overflow[overflow].AssignmentCount;
    });
    out.Overflow.SetNum(overflowCount, EAllowShrinking::No);

    // Each key's range: layout keys in layout order (mirrors hold none of their own), then the overflow. The fill
    // cursor borrows ExtraCount until the keys are finished
    int32 total = 0;
    for (FSOLDiagramKey& key : out.Keys)
    {
        key.FirstAssignment = total;
        key.ExtraCount = 0;
        total += key.AssignmentCount;
    }
    for (FSOLDiagramKey& key : out.Overflow)
    {
        key.FirstAssignment = total;
        key.ExtraCount = 0;
        total += key.AssignmentCount;
    }
    out.Assignments.SetNum(total, EAllowShrinking::No);

    // Pass 2: place every action in its key's range, in row order
    KeyDiagramForEachAction(rows, liveKeys, [&](const FKeyDiagramPending& pending)
    {
        if (pending.KeyName.IsEmpty())
        {
            return;
        }
        const int32 index = SOLKeyboardLayout::FindKey(pending.KeyName);
        FSOLDiagramKey& key = index != INDEX_NONE ? out.Keys[index]
            : out.Overflow[KeyDiagramFindOverflow(out, overflowCount, pending.KeyName)];
        out.Assignments[key.FirstAssignment + key.ExtraCount++] = pending.Assignment;
    });

    // The derived fields; a mirror then copies its main key's (it shares the range)
    for (FSOLDiagramKey& key : out.Keys)
    {
        KeyDiagramFinishKey(out, key);
    }
    for (FSOLDiagramKey& key : out.Overflow)
    {
        KeyDiagramFinishKey(out, key);
    }

    // The chords (D32 revised): count the chord assignments each modifier key starts (ChordCount), give each key its
    // range in layout order, then fill the ranges in Assignments order (ChordCount is the fill cursor and ends at the
    // count). Overflow keys and mirrors start none of their own (FindKey never returns a mirror)
    for (const FSOLKeyAssignment& assignment : out.Assignments)
    {
        for (const TCHAR* modifierName : ChordModifierKeys(assignment.Chord))
        {
            const int32 modifier = SOLKeyboardLayout::FindKey(modifierName);
            if (modifier != INDEX_NONE)
            {
                ++out.Keys[modifier].ChordCount;
            }
        }
    }
    int32 chordTotal = 0;
    for (FSOLDiagramKey& key : out.Keys)
    {
        key.FirstChord = chordTotal;
        chordTotal += key.ChordCount;
        key.bModifier = key.ChordCount > 0;
        key.ChordCount = 0;
    }
    out.Chords.SetNum(chordTotal, EAllowShrinking::No);
    for (int32 index = 0; index < out.Assignments.Num(); ++index)
    {
        for (const TCHAR* modifierName : ChordModifierKeys(out.Assignments[index].Chord))
        {
            const int32 modifier = SOLKeyboardLayout::FindKey(modifierName);
            if (modifier != INDEX_NONE)
            {
                FSOLDiagramKey& key = out.Keys[modifier];
                out.Chords[key.FirstChord + key.ChordCount++] = index;
            }
        }
    }

    for (int32 index = 0; index < layout.Num(); ++index)
    {
        const int32 main = layout[index].bMirror ? SOLKeyboardLayout::FindKey(layout[index].KeyName) : INDEX_NONE;
        if (main != INDEX_NONE)
        {
            const FSOLDiagramKey& source = out.Keys[main];
            FSOLDiagramKey& mirror = out.Keys[index];
            mirror.FirstAssignment = source.FirstAssignment;
            mirror.AssignmentCount = source.AssignmentCount;
            mirror.Primary = source.Primary;
            mirror.ExtraCount = source.ExtraCount;
            mirror.Area = source.Area;
            mirror.ShortLabel = source.ShortLabel;
            mirror.bLocked = source.bLocked;
            mirror.bModifier = source.bModifier;
            mirror.FirstChord = source.FirstChord;
            mirror.ChordCount = source.ChordCount;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds the diagram model from CONTROL_ROWS and the live keys
void SOLKeyDiagram::Build(const TArray<FString>& liveKeys, FSOLKeyDiagramModel& out)
{
    Build(TConstArrayView<SOLMenuText::FControlRow>(SOLMenuText::CONTROL_ROWS), liveKeys, out);
}

//////////////////////////////////////////////////////////////////////////
// Returns the area of a Controls row's area name (case-sensitive), or Count
ESOLControlArea SOLKeyDiagram::AreaFromName(const TCHAR* areaName)
{
    if (areaName == nullptr)
    {
        return ESOLControlArea::Count;
    }
    for (int32 area = 0; area < static_cast<int32>(ESOLControlArea::Count); ++area)
    {
        if (FCString::Strcmp(areaName, KEY_DIAGRAM_AREA_NAMES[area]) == 0)
        {
            return static_cast<ESOLControlArea>(area);
        }
    }
    return ESOLControlArea::Count;
}

//////////////////////////////////////////////////////////////////////////
// Returns an area's name as CONTROL_ROWS writes it ("" for Count)
const TCHAR* SOLKeyDiagram::AreaName(const ESOLControlArea area)
{
    const int32 index = static_cast<int32>(area);
    return index < static_cast<int32>(ESOLControlArea::Count) ? KEY_DIAGRAM_AREA_NAMES[index] : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Returns an area's colour (the dim text colour for Count)
FColor SOLKeyDiagram::AreaColor(const ESOLControlArea area)
{
    const int32 index = static_cast<int32>(area);
    return index < static_cast<int32>(ESOLControlArea::Count) ? SOL::Help::AREA_COLORS[index]
        : SOL::SolidMenu::TEXT_DIM_COLOR;
}

//////////////////////////////////////////////////////////////////////////
// Returns a binding's short label ("" out of range)
const TCHAR* SOLKeyDiagram::BindingShortLabel(const int32 binding)
{
    return binding >= 0 && binding < SOLKeyBindings::BINDING_COUNT ? SOLMenuText::KEY_SHORT_LABELS[binding].Label
        : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Returns a fixed key's short label (case-insensitive key name; "" if none)
const TCHAR* SOLKeyDiagram::FixedKeyShortLabel(const FString& keyName)
{
    return KeyDiagramFixedLabel(keyName);
}

//////////////////////////////////////////////////////////////////////////
// Returns the layout key holding a binding's assignment, or INDEX_NONE
int32 SOLKeyDiagram::KeyOfBinding(const FSOLKeyDiagramModel& model, const int32 binding)
{
    if (binding < 0 || binding >= SOLKeyBindings::BINDING_COUNT)
    {
        return INDEX_NONE;
    }
    const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
    for (int32 index = 0; index < model.Keys.Num(); ++index)
    {
        const FSOLDiagramKey& key = model.Keys[index];
        if (layout.IsValidIndex(index) && layout[index].bMirror)
        {
            continue;
        }
        for (int32 assignment = key.FirstAssignment; assignment < key.FirstAssignment + key.AssignmentCount;
            ++assignment)
        {
            if (model.Assignments.IsValidIndex(assignment) && model.Assignments[assignment].Binding == binding)
            {
                return index;
            }
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the physical layout keys that start a chord, left side first (static tables)
TConstArrayView<const TCHAR*> SOLKeyDiagram::ChordModifierKeys(const SOLKeyBindings::EKeyChord chord)
{
    static const TCHAR* const SHIFT_KEYS[] = { TEXT("LeftShift"), TEXT("RightShift") };
    static const TCHAR* const RIGHT_SHIFT_KEYS[] = { TEXT("RightShift") };
    static const TCHAR* const ALT_KEYS[] = { TEXT("LeftAlt"), TEXT("RightAlt") };
    static const TCHAR* const CONTROL_KEYS[] = { TEXT("LeftControl"), TEXT("RightControl") };
    switch (chord)
    {
    case SOLKeyBindings::EKeyChord::Shift:
        return TConstArrayView<const TCHAR*>(SHIFT_KEYS);
    case SOLKeyBindings::EKeyChord::RightShift:
        return TConstArrayView<const TCHAR*>(RIGHT_SHIFT_KEYS);
    case SOLKeyBindings::EKeyChord::Alt:
        return TConstArrayView<const TCHAR*>(ALT_KEYS);
    case SOLKeyBindings::EKeyChord::Control:
        return TConstArrayView<const TCHAR*>(CONTROL_KEYS);
    default:
        return TConstArrayView<const TCHAR*>();
    }
}

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Calls visit(assignment) for each of a key's hover strip assignments in strip order (its own, the primary first,
    // then the chords it starts); a mirror reads its main key's ranges. Shared by DetailAssignments and MaxDetailCount
    template <typename FVisit>
    void KeyDiagramForEachDetail(const FSOLKeyDiagramModel& model, const int32 key, FVisit&& visit)
    {
        if (!model.Keys.IsValidIndex(key))
        {
            return;
        }
        // A mirror shares its main key's ranges; read the main key's all the same
        const TConstArrayView<FSOLLayoutKey> layout = SOLKeyboardLayout::Keys();
        int32 source = key;
        if (layout.IsValidIndex(key) && layout[key].bMirror)
        {
            const int32 main = SOLKeyboardLayout::FindKey(layout[key].KeyName);
            source = model.Keys.IsValidIndex(main) ? main : key;
        }
        const FSOLDiagramKey& diagramKey = model.Keys[source];

        // Own actions: the primary, then the rest of the range in model order
        if (model.Assignments.IsValidIndex(diagramKey.Primary))
        {
            visit(diagramKey.Primary);
        }
        const int32 lastOwn = diagramKey.FirstAssignment + diagramKey.AssignmentCount;
        for (int32 index = diagramKey.FirstAssignment; index < lastOwn; ++index)
        {
            if (index != diagramKey.Primary && model.Assignments.IsValidIndex(index))
            {
                visit(index);
            }
        }

        // The chords it starts, in range order
        for (int32 chord = diagramKey.FirstChord; chord < diagramKey.FirstChord + diagramKey.ChordCount; ++chord)
        {
            if (model.Chords.IsValidIndex(chord) && model.Assignments.IsValidIndex(model.Chords[chord]))
            {
                visit(model.Chords[chord]);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Fills out with the hover strip's assignments of a key: its own (primary first), then the chords it starts
int32 SOLKeyDiagram::DetailAssignments(const FSOLKeyDiagramModel& model, const int32 key, TArray<int32>& out)
{
    out.Reset();
    KeyDiagramForEachDetail(model, key, [&out](const int32 assignment) { out.Add(assignment); });
    return out.Num();
}

//////////////////////////////////////////////////////////////////////////
// Fills one hover strip line for an assignment: its one alternative's keys and its row's action
void SOLKeyDiagram::DetailLine(const TConstArrayView<SOLMenuText::FControlRow> rows,
    const FSOLKeyDiagramModel& model, const int32 assignment, const TArray<FString>& liveKeys,
    FSOLDiagramDetailLine& out)
{
    out.Keys.Reset();
    out.Action.Reset();
    out.Area = ESOLControlArea::Count;
    out.bFixed = false;
    if (!model.Assignments.IsValidIndex(assignment))
    {
        return;
    }
    const FSOLKeyAssignment& source = model.Assignments[assignment];
    if (!rows.IsValidIndex(source.Row))
    {
        return;
    }
    const SOLMenuText::FControlRow& row = rows[source.Row];
    const FString alternative(KeyDiagramAlternative(row.Bindings, source.Alternative));
    out.Keys = SOLMenuContent::FormatBindings(*alternative, liveKeys);
    out.Action = SOLMenuContent::FormatControlAction(row.Action != nullptr ? row.Action : TEXT(""));
    out.Area = source.Area;
    out.bFixed = source.bFixed;
}

//////////////////////////////////////////////////////////////////////////
// Fills one hover strip line for an assignment from CONTROL_ROWS
void SOLKeyDiagram::DetailLine(const FSOLKeyDiagramModel& model, const int32 assignment,
    const TArray<FString>& liveKeys, FSOLDiagramDetailLine& out)
{
    DetailLine(TConstArrayView<SOLMenuText::FControlRow>(SOLMenuText::CONTROL_ROWS), model, assignment, liveKeys, out);
}

//////////////////////////////////////////////////////////////////////////
// D36: returns the largest DetailAssignments count over the model's keys (mirrors included), counting without a list
int32 SOLKeyDiagram::MaxDetailCount(const FSOLKeyDiagramModel& model)
{
    int32 best = 0;
    for (int32 key = 0; key < model.Keys.Num(); ++key)
    {
        int32 count = 0;
        KeyDiagramForEachDetail(model, key, [&count](const int32 /*assignment*/) { ++count; });
        best = FMath::Max(best, count);
    }
    return best;
}

//////////////////////////////////////////////////////////////////////////
// D38: returns the diagram legend abbreviation of an engine key name, or nullptr when it has none
const TCHAR* SOLKeyDiagram::LegendAbbreviation(const FStringView keyName)
{
    for (const SOLMenuText::FDiagramLegendAbbreviation& entry : SOLMenuText::DIAGRAM_LEGEND_ABBREVIATIONS)
    {
        if (keyName.Equals(entry.KeyName, ESearchCase::IgnoreCase))
        {
            return entry.Legend;
        }
    }
    return nullptr;
}
