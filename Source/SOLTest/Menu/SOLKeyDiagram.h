/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuText.h"

#include "CoreMinimal.h"

// The ten areas of SOLMenuText::CONTROL_ROWS, in table order (also the index into SOL::Help::AREA_COLORS)
enum class ESOLControlArea : uint8
{
    Flight,
    Targeting,
    Combat,
    Time,
    Overlays,
    HUD,
    Map,
    Routes,
    Menu,
    Demo,
    Count,
};

// One action on a diagram key: the Controls row that names the key, the row's area, the binding (for an '@'
// alternative) and the modifier the key is pressed with
struct FSOLKeyAssignment
{
    int32 Row = INDEX_NONE;                                         // Index into the rows given to Build
    int32 Binding = INDEX_NONE;                                     // SOLKeyBindings::EBinding, or INDEX_NONE for a
                                                                    // fixed key
    ESOLControlArea Area = ESOLControlArea::Count;
    SOLKeyBindings::EKeyChord Chord = SOLKeyBindings::EKeyChord::None; // A binding's chord; for a fixed '+' chord, the
                                                                    // modifier key(s) before its last key
    bool bFixed = false;                                            // Not rebindable (D19 lock mark)
    bool bInFlight = true;                                          // Works in flight (false for the Map area's rows)
    const TCHAR* ShortLabel = TEXT("");                             // SOLMenuText::KEY_SHORT_LABELS of the binding, or
                                                                    // FIXED_KEY_SHORT_LABELS of the key ("" if none)
    int32 Alternative = 0;                                          // The alternative's index in the row's bindings
                                                                    // cell ('|'-separated, 0-based)
};

// One key of the diagram (a SOLKeyboardLayout key, or a bound key off the layout): its assignments (a range of
// FSOLKeyDiagramModel::Assignments), the primary one that tints it and labels it, and the "+N" badge count
struct FSOLDiagramKey
{
    FString KeyName;                                    // Engine key name
    int32 FirstAssignment = 0;
    int32 AssignmentCount = 0;
    int32 Primary = INDEX_NONE;                         // Index into Assignments (INDEX_NONE for a free key): the
                                                        // plain (no chord) in-flight one, else the first (D18)
    int32 ExtraCount = 0;                               // AssignmentCount - 1, 0 for a free key (the "+N" badge)
    ESOLControlArea Area = ESOLControlArea::Count;      // The primary's area (Count for a free key)
    const TCHAR* ShortLabel = TEXT("");                 // The primary's short label
    bool bLocked = false;                               // Every assignment is fixed (lock mark; false for a free key)
    bool bModifier = false;                             // D32 (revised): the key starts at least one chord (ChordCount
                                                        // > 0), whether or not it has actions of its own
    int32 FirstChord = 0;                               // Range into FSOLKeyDiagramModel::Chords: the chords this key
    int32 ChordCount = 0;                               // is a modifier of
};

// The keyboard diagram's model (SDD 33 section 7): one FSOLDiagramKey per SOLKeyboardLayout key (layout order; a mirror
// key shares its main key's assignment range, nothing is stored twice), the bound keys not on the layout ("Other keys")
// and the bindings without a key. Reset keeps the capacity, so Build into the same model allocates nothing after the
// first build
struct FSOLKeyDiagramModel
{
    TArray<FSOLKeyAssignment> Assignments;              // Grouped by key: layout keys in layout order, then Overflow
    TArray<FSOLDiagramKey> Keys;                        // One per SOLKeyboardLayout::Keys() entry
    TArray<FSOLDiagramKey> Overflow;                    // Bound keys off the layout, in first-row order
    TArray<int32> Unbound;                              // SOLKeyBindings::EBinding of every binding without a key
    TArray<int32> Chords;                               // Indices into Assignments, grouped by key in layout order
                                                        // (each key's FirstChord / ChordCount range)

    // Empties the model, keeping its capacity
    void Reset()
    {
        Assignments.Reset();
        Keys.Reset();
        Overflow.Reset();
        Unbound.Reset();
        Chords.Reset();
    }
};

// One line of the diagram's hover popup (D23, D45): how the player presses an assignment and what it does
struct FSOLDiagramDetailLine
{
    FString Keys;                                       // SOLMenuContent::FormatBindings of that one alternative's text
                                                        // at the live keys ("Alt+O", "Shift+M", "Ctrl+F1", "LMB")
    FString Action;                                     // SOLMenuContent::FormatControlAction(rows[Row].Action)
    ESOLControlArea Area = ESOLControlArea::Count;
    bool bFixed = false;
};

/**
 * The keyboard diagram's pure model (SDD 33 section 7, D13, D17-D19), test-first: from the Controls rows and the live
 * keys (indexed by SOLKeyBindings::EBinding), which actions sit on each diagram key, its tint area, short label, "+N"
 * count and lock mark, the overflow and unbound lists, and the area palette. Built on page show and on a rebind only.
 */
namespace SOLKeyDiagram
{
    // Rebuilds out from the rows and the live keys (keys and buffers reused in place): every '@' alternative goes to
    // its binding's live key (chord from SOLKeyBindings::GetChord), every fixed alternative to its chord's last key
    // (bFixed); a key on the layout goes to Keys, one off it to Overflow, a binding with an empty key (or a short key
    // array) to Unbound
    SOLTEST_API void Build(TConstArrayView<SOLMenuText::FControlRow> rows, const TArray<FString>& liveKeys,
        FSOLKeyDiagramModel& out);

    // Build with SOLMenuText::CONTROL_ROWS
    SOLTEST_API void Build(const TArray<FString>& liveKeys, FSOLKeyDiagramModel& out);

    // Returns the area of a Controls row's area name (case-sensitive, as CONTROL_ROWS writes it), or Count
    SOLTEST_API ESOLControlArea AreaFromName(const TCHAR* areaName);

    // Returns an area's name as CONTROL_ROWS writes it ("" for Count)
    SOLTEST_API const TCHAR* AreaName(ESOLControlArea area);

    // Returns an area's colour (SOL::Help::AREA_COLORS); SOL::SolidMenu::TEXT_DIM_COLOR for Count
    SOLTEST_API FColor AreaColor(ESOLControlArea area);

    // Returns over blended onto under at an alpha clamped to [0, 1], per sRGB channel (Slate blends in gamma space),
    // opaque
    SOLTEST_API FColor BlendSrgb(const FColor& under, const FColor& over, float alpha);

    // Returns the WCAG 2 contrast ratio of two sRGB colours (1 to 21, symmetric, alpha ignored)
    SOLTEST_API float ContrastRatio(const FColor& a, const FColor& b);

    // Returns an assigned key's face: the raised (or the hovered key's lifted) hull tinted by the area colour at
    // SOL::Help::DIAGRAM_TINT_ALPHA (D49)
    SOLTEST_API FColor AssignedFaceColor(ESOLControlArea area, bool bLifted);

    // Returns a free key's face: the raised hull at SOL::Help::DIAGRAM_FREE_ALPHA over the panel (D49)
    SOLTEST_API FColor FreeFaceColor();

    // Returns a free key's legend: the key text colour at SOL::Help::DIAGRAM_FREE_ALPHA over the free face (D49)
    SOLTEST_API FColor FreeLegendColor();

    // Returns a binding's short label (SOLMenuText::KEY_SHORT_LABELS; "" for an out-of-range binding)
    SOLTEST_API const TCHAR* BindingShortLabel(int32 binding);

    // Returns a fixed key's short label (SOLMenuText::FIXED_KEY_SHORT_LABELS, case-insensitive key name; "" if none)
    SOLTEST_API const TCHAR* FixedKeyShortLabel(const FString& keyName);

    // Returns the layout key (index into model.Keys) that holds a binding's assignment, or INDEX_NONE (off the layout,
    // unbound or out of range)
    SOLTEST_API int32 KeyOfBinding(const FSOLKeyDiagramModel& model, int32 binding);

    // Returns the physical layout keys that start a chord, as the game reads them, left side first: Shift {LeftShift,
    // RightShift}, RightShift {RightShift}, Alt {LeftAlt, RightAlt}, Control {LeftControl, RightControl}, None empty.
    // Static data, no allocation
    SOLTEST_API TConstArrayView<const TCHAR*> ChordModifierKeys(SOLKeyBindings::EKeyChord chord);

    // Fills out (reset, capacity kept) with the hover popup's assignments of a key (index into model.Keys; a mirror
    // gives its main key's): the key's own actions first (the primary first, then its other assignments in model
    // order), then the chords it starts (its Chords range in order); empty for a key with neither or an index out of
    // range. Returns out.Num()
    SOLTEST_API int32 DetailAssignments(const FSOLKeyDiagramModel& model, int32 key, TArray<int32>& out);

    // D38: returns the diagram legend abbreviation of an engine key name (SOLMenuText::DIAGRAM_LEGEND_ABBREVIATIONS,
    // case-insensitive), or nullptr when it has none. The leaf uses it only when the layout legend does not fit
    SOLTEST_API const TCHAR* LegendAbbreviation(FStringView keyName);

    // Fills one hover popup line for an assignment (index into model.Assignments); out of range: Keys and Action
    // empty, Area Count, bFixed false
    SOLTEST_API void DetailLine(TConstArrayView<SOLMenuText::FControlRow> rows, const FSOLKeyDiagramModel& model,
        int32 assignment, const TArray<FString>& liveKeys, FSOLDiagramDetailLine& out);

    // DetailLine with SOLMenuText::CONTROL_ROWS
    SOLTEST_API void DetailLine(const FSOLKeyDiagramModel& model, int32 assignment, const TArray<FString>& liveKeys,
        FSOLDiagramDetailLine& out);
}
