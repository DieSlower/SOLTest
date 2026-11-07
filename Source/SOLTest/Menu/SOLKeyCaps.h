/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

// How a key cap is drawn (SDD 33 section 6)
enum class ESOLKeyCapShape : uint8
{
    Normal,     // A plain cap, as wide as its legend (at least KEYCAP_MIN_WIDTH)
    Wide,       // Space, Backspace, Tab, Enter, Shift, Ctrl, Alt, Caps
    Mouse,      // A mouse glyph with the used part lit
};

// The part of the mouse a Mouse cap lights
enum class ESOLMouseButton : uint8
{
    None,       // Not a mouse cap
    Left,       // LeftMouseButton
    Right,      // RightMouseButton
    Middle,     // MiddleMouseButton
    Wheel,      // MouseWheelAxis
    Move,       // Mouse2D (and the mouse X / Y axes)
};

// One drawn key cap
struct FSOLKeyCap
{
    FString KeyName;                                    // Engine key name ("" for an unbound cap)
    FString Label;                                      // Legend: SOLBindingLabel::KeyDisplayName, or "Unbound"
    ESOLKeyCapShape Shape = ESOLKeyCapShape::Normal;
    ESOLMouseButton Mouse = ESOLMouseButton::None;
    float WidthUnits = 1.0f;                            // 1, KEYCAP_WIDE_UNITS or KEYCAP_SPACE_UNITS (SOLKeyCaps::
                                                        // WidthUnits)
    bool bUnbound = false;                              // A binding with no key (drawn as a recess cap, dim text)
};

// One alternative of a cell: a chord of caps pressed together (one cap for a plain key), a range of
// FSOLKeyCapModel::Caps
struct FSOLKeyCapChord
{
    int32 FirstCap = 0;
    int32 CapCount = 0;
    int32 Binding = INDEX_NONE;                         // The SOLKeyBindings::EBinding of an '@' alternative, else
                                                        // INDEX_NONE (a fixed chord, or an unknown id)
};

// A parsed bindings cell or binding: its caps in draw order and its alternatives (chords) in cell order. ParseCell and
// ParseBinding overwrite the caps in place (their strings keep their buffers), so parsing into the same model again
// allocates nothing once it has held as many caps with as long names
struct FSOLKeyCapModel
{
    TArray<FSOLKeyCap> Caps;
    TArray<FSOLKeyCapChord> Chords;

    // Empties the model, keeping its capacity
    void Reset()
    {
        Caps.Reset();
        Chords.Reset();
    }
};

// The measured pieces of a cap row (menu units), from the widget's font: the "+" inside a chord and the " / " between
// alternatives, each with KEYCAP_GAP on both sides of it
struct FSOLKeyCapMetrics
{
    float PlusWidth = 0.0f;
    float SeparatorWidth = 0.0f;
    float Gap = SOL::Help::KEYCAP_GAP;
    float RowGap = SOL::Help::KEYCAP_ROW_GAP;
    float CapHeight = SOL::Help::KEYCAP_HEIGHT;
};

// What one laid-out piece of a cap row is
enum class ESOLKeyCapItem : uint8
{
    Cap,        // A cap (Cap = its index in FSOLKeyCapModel::Caps)
    Plus,       // The "+" between two caps of a chord
    Separator,  // The " / " between two alternatives
};

// One laid-out piece of a cap row: its kind, its cap, its top-left and width (menu units) and its line
struct FSOLKeyCapLayoutItem
{
    ESOLKeyCapItem Kind = ESOLKeyCapItem::Cap;
    int32 Cap = INDEX_NONE;
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 0.0f;
    int32 Line = 0;
};

// A laid-out cap row (SOLKeyCaps::LayoutRow); Reset keeps the capacity
struct FSOLKeyCapLayout
{
    TArray<FSOLKeyCapLayoutItem> Items;
    int32 LineCount = 0;
    float Width = 0.0f;                                 // The widest line
    float Height = 0.0f;                                // LineCount x CapHeight + (LineCount - 1) x RowGap

    // Empties the layout, keeping its capacity
    void Reset()
    {
        Items.Reset();
        LineCount = 0;
        Width = 0.0f;
        Height = 0.0f;
    }
};

// The part of a Mouse cap's glyph drawn lit (33b)
enum class ESOLMouseGlyphPart : uint8
{
    None,
    Body,           // The whole mouse (Move)
    LeftButton,
    RightButton,
    Wheel,          // The wheel slot (the wheel and the middle button)
};

// A Mouse cap's glyph (33b): the body, the two buttons across its top, the wheel slot between them, and the part lit
struct FSOLMouseGlyph
{
    FBox2f Body = FBox2f(ForceInit);
    FBox2f LeftButton = FBox2f(ForceInit);
    FBox2f RightButton = FBox2f(ForceInit);
    FBox2f Wheel = FBox2f(ForceInit);
    ESOLMouseGlyphPart Lit = ESOLMouseGlyphPart::None;
};

/**
 * Pure logic of the key caps (SDD 33 section 6), test-first: parses a SOLMenuText::CONTROL_ROWS bindings cell (or one
 * binding) against the live keys (indexed by SOLKeyBindings::EBinding) into alternatives of chords of caps, gives a
 * key's cap shape and width, and lays a cap row out in a width with wrapping. Text and caps come from the same parse,
 * so ModelText equals SOLMenuContent::FormatBindings for the same cell. No Slate, no allocation once the caller's
 * model and layout have grown to size.
 */
namespace SOLKeyCaps
{
    // Parses a bindings cell into out (its old caps overwritten in place, strings and buffers kept): '|' alternatives,
    // each '@' + a binding id (that binding's live key as its chord's caps; an unknown id or an empty key gives one
    // unbound cap) or a '+' chord of fixed engine key names. A null or empty cell gives one unbound cap
    SOLTEST_API void ParseCell(const TCHAR* cell, const TArray<FString>& liveKeys, FSOLKeyCapModel& out);

    // Parses one binding into out (overwritten in place as ParseCell): its chord's modifier cap (Shift, Alt, Ctrl or
    // RShift) then its key's cap, or one unbound cap for an empty key, a short key array or an out-of-range binding
    SOLTEST_API void ParseBinding(int32 binding, const TArray<FString>& liveKeys, FSOLKeyCapModel& out);

    // Returns a key's cap shape from its engine key name (case-insensitive): Mouse for the mouse buttons and axes, Wide
    // for SpaceBar, BackSpace, Tab, Enter, the Shift, Control and Alt keys and CapsLock, else Normal
    SOLTEST_API ESOLKeyCapShape WidthClass(const FString& keyName);

    // Returns the mouse part a key lights (None for a key that is not a mouse button or axis)
    SOLTEST_API ESOLMouseButton MouseButtonOf(const FString& keyName);

    // Returns a key's cap width in key units for a cap row: KEYCAP_SPACE_UNITS for SpaceBar, KEYCAP_WIDE_UNITS for the
    // other Wide keys, else 1
    SOLTEST_API float WidthUnits(const FString& keyName);

    // Returns a cap's drawn width from its measured legend width: the legend plus KEYCAP_PADDING a side, at least
    // KEYCAP_MIN_WIDTH x the cap's WidthUnits; a negative or NaN legend width reads as 0
    SOLTEST_API float CapWidth(const FSOLKeyCap& cap, float legendWidth);

    // Returns the model's text form: each chord's labels joined by "+", the alternatives by " / "
    // (SOLMenuText::BINDING_ALTERNATIVE_SEPARATOR); equal to SOLMenuContent::FormatBindings of the parsed cell
    SOLTEST_API FString ModelText(const FSOLKeyCapModel& model);

    // Lays the model's caps out into out (Reset first) in availableWidth: left to right, a chord's caps joined by
    // "+" pieces and alternatives by " / " pieces (each piece with metrics.Gap on both sides), wrapping onto a new line
    // (metrics.RowGap below the last) before an alternative that would cross the width; a chord is split between its
    // caps only when it alone is wider than the width; a line never starts with a separator. capWidths has one drawn
    // width per model cap (CapWidth); a missing, negative or NaN one reads as KEYCAP_MIN_WIDTH. An availableWidth that
    // is not a positive finite number lays everything out on one line. Returns out.Height (0 for an empty model)
    SOLTEST_API float LayoutRow(const FSOLKeyCapModel& model, TConstArrayView<float> capWidths,
        const FSOLKeyCapMetrics& metrics, float availableWidth, FSOLKeyCapLayout& out);

    // (33b) Returns the legend drawn on a cap: empty for the left, right and middle mouse buttons (the glyph alone),
    // SOLMenuText::KEYCAP_MOVE_LEGEND for a mouse-move cap (its Label stays "Mouse"), else the cap's Label
    SOLTEST_API FStringView DrawnLegend(const FSOLKeyCap& cap);

    // (33b) Returns a cap's content width from its drawn legend's measured width: a Mouse cap's glyph
    // (KEYCAP_MOUSE_GLYPH_WIDTH) plus KEYCAP_GAP and the legend when the legend is wider than 0, else the legend; a
    // negative or NaN legend width reads as 0 (DrawnCapWidth makes the cap's width from it)
    SOLTEST_API float ContentWidth(const FSOLKeyCap& cap, float legendWidth);

    // (33b) Returns where a cap's content may start from its left edge: KEYCAP_LED_ROOM on a bound cap (past the Small
    // hull's LED), KEYCAP_PADDING on an unbound (recess) cap
    SOLTEST_API float ContentLeft(const FSOLKeyCap& cap);

    // (33b) Returns a cap's drawn width from its drawn legend's measured width: ContentLeft, the content (ContentWidth)
    // and KEYCAP_PADDING on the right, at least KEYCAP_MIN_WIDTH x WidthUnits (CapWidth of the content widened by
    // ContentLeft - KEYCAP_PADDING)
    SOLTEST_API float DrawnCapWidth(const FSOLKeyCap& cap, float legendWidth);

    // (33b) Returns the glyph part a mouse button lights: Left and Right their buttons, Middle and Wheel the wheel
    // slot, Move the body, None none
    SOLTEST_API ESOLMouseGlyphPart GlyphPartOf(ESOLMouseButton button);

    // (33b) Returns the mouse glyph in a box at topLeft of size (menu units): the body the box, the buttons its top
    // KEYCAP_MOUSE_BUTTON_FRACTION split at the centre, the wheel slot centred (KEYCAP_MOUSE_WHEEL_* fractions), the
    // part GlyphPartOf(button) lit. A size component that is not a positive finite number collapses every box to the
    // point topLeft
    SOLTEST_API FSOLMouseGlyph MouseGlyph(const FVector2f& topLeft, const FVector2f& size, ESOLMouseButton button);
}
