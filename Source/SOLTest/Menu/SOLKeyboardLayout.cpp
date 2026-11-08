/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Menu/SOLKeyboardLayout.h"

// The keyboard diagram's static layout (SDD 33 section 7, D19): the key table, the mouse's place, the lookup and the
// hit test. Nothing here allocates

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a keyboard key on a row (the function row at Y 0, the others 0.5 lower than their row number)
    constexpr FSOLLayoutKey KeyboardLayoutKey(const TCHAR* keyName, const TCHAR* legend, const int32 row, const float x,
        const float width = 1.0f, const float height = 1.0f)
    {
        FSOLLayoutKey key;
        key.KeyName = keyName;
        key.Legend = legend;
        key.Row = row;
        key.X = x;
        key.Y = row == 0 ? 0.0f : static_cast<float>(row) + 0.5f;
        key.Width = width;
        key.Height = height;
        return key;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a mouse zone, placed relative to the mouse's top-left
    constexpr FSOLLayoutKey KeyboardLayoutMouse(const TCHAR* keyName, const TCHAR* altKeyName, const TCHAR* legend,
        const ESOLMouseButton button, const float x, const float y, const float width, const float height)
    {
        FSOLLayoutKey key;
        key.KeyName = keyName;
        key.AltKeyName = altKeyName;
        key.Legend = legend;
        key.X = x;
        key.Y = y;
        key.Width = width;
        key.Height = height;
        key.Mouse = button;
        return key;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the numpad Enter: a second cap of the engine's single Enter key
    constexpr FSOLLayoutKey KeyboardLayoutMirror(const TCHAR* keyName, const TCHAR* legend, const int32 row,
        const float x, const float width, const float height)
    {
        FSOLLayoutKey key = KeyboardLayoutKey(keyName, legend, row, x, width, height);
        key.bMirror = true;
        return key;
    }

    // The US layout with the numpad (main block 15 wide, a 0.5 gap, navigation 3, a 0.5 gap, numpad 4: 23 x 6.5),
    // then the mouse (3 x 4.5: the two buttons with the wheel between them over the body, which is the move zone)
    constexpr FSOLLayoutKey KEYBOARD_LAYOUT_KEYS[] =
    {
        // Function row
        KeyboardLayoutKey(TEXT("Escape"), TEXT("Esc"), 0, 0.0f),
        KeyboardLayoutKey(TEXT("F1"), TEXT("F1"), 0, 2.0f),
        KeyboardLayoutKey(TEXT("F2"), TEXT("F2"), 0, 3.0f),
        KeyboardLayoutKey(TEXT("F3"), TEXT("F3"), 0, 4.0f),
        KeyboardLayoutKey(TEXT("F4"), TEXT("F4"), 0, 5.0f),
        KeyboardLayoutKey(TEXT("F5"), TEXT("F5"), 0, 6.5f),
        KeyboardLayoutKey(TEXT("F6"), TEXT("F6"), 0, 7.5f),
        KeyboardLayoutKey(TEXT("F7"), TEXT("F7"), 0, 8.5f),
        KeyboardLayoutKey(TEXT("F8"), TEXT("F8"), 0, 9.5f),
        KeyboardLayoutKey(TEXT("F9"), TEXT("F9"), 0, 11.0f),
        KeyboardLayoutKey(TEXT("F10"), TEXT("F10"), 0, 12.0f),
        KeyboardLayoutKey(TEXT("F11"), TEXT("F11"), 0, 13.0f),
        KeyboardLayoutKey(TEXT("F12"), TEXT("F12"), 0, 14.0f),
        KeyboardLayoutKey(TEXT("ScrollLock"), TEXT("ScrL"), 0, 16.5f),
        KeyboardLayoutKey(TEXT("Pause"), TEXT("Pause"), 0, 17.5f),

        // Number row
        KeyboardLayoutKey(TEXT("Tilde"), TEXT("`"), 1, 0.0f),
        KeyboardLayoutKey(TEXT("One"), TEXT("1"), 1, 1.0f),
        KeyboardLayoutKey(TEXT("Two"), TEXT("2"), 1, 2.0f),
        KeyboardLayoutKey(TEXT("Three"), TEXT("3"), 1, 3.0f),
        KeyboardLayoutKey(TEXT("Four"), TEXT("4"), 1, 4.0f),
        KeyboardLayoutKey(TEXT("Five"), TEXT("5"), 1, 5.0f),
        KeyboardLayoutKey(TEXT("Six"), TEXT("6"), 1, 6.0f),
        KeyboardLayoutKey(TEXT("Seven"), TEXT("7"), 1, 7.0f),
        KeyboardLayoutKey(TEXT("Eight"), TEXT("8"), 1, 8.0f),
        KeyboardLayoutKey(TEXT("Nine"), TEXT("9"), 1, 9.0f),
        KeyboardLayoutKey(TEXT("Zero"), TEXT("0"), 1, 10.0f),
        KeyboardLayoutKey(TEXT("Hyphen"), TEXT("-"), 1, 11.0f),
        KeyboardLayoutKey(TEXT("Equals"), TEXT("="), 1, 12.0f),
        KeyboardLayoutKey(TEXT("BackSpace"), TEXT("Bksp"), 1, 13.0f, 2.0f),

        // Q row
        KeyboardLayoutKey(TEXT("Tab"), TEXT("Tab"), 2, 0.0f, 1.5f),
        KeyboardLayoutKey(TEXT("Q"), TEXT("Q"), 2, 1.5f),
        KeyboardLayoutKey(TEXT("W"), TEXT("W"), 2, 2.5f),
        KeyboardLayoutKey(TEXT("E"), TEXT("E"), 2, 3.5f),
        KeyboardLayoutKey(TEXT("R"), TEXT("R"), 2, 4.5f),
        KeyboardLayoutKey(TEXT("T"), TEXT("T"), 2, 5.5f),
        KeyboardLayoutKey(TEXT("Y"), TEXT("Y"), 2, 6.5f),
        KeyboardLayoutKey(TEXT("U"), TEXT("U"), 2, 7.5f),
        KeyboardLayoutKey(TEXT("I"), TEXT("I"), 2, 8.5f),
        KeyboardLayoutKey(TEXT("O"), TEXT("O"), 2, 9.5f),
        KeyboardLayoutKey(TEXT("P"), TEXT("P"), 2, 10.5f),
        KeyboardLayoutKey(TEXT("LeftBracket"), TEXT("["), 2, 11.5f),
        KeyboardLayoutKey(TEXT("RightBracket"), TEXT("]"), 2, 12.5f),
        KeyboardLayoutKey(TEXT("Backslash"), TEXT("\\"), 2, 13.5f, 1.5f),

        // A row
        KeyboardLayoutKey(TEXT("CapsLock"), TEXT("Caps"), 3, 0.0f, 1.75f),
        KeyboardLayoutKey(TEXT("A"), TEXT("A"), 3, 1.75f),
        KeyboardLayoutKey(TEXT("S"), TEXT("S"), 3, 2.75f),
        KeyboardLayoutKey(TEXT("D"), TEXT("D"), 3, 3.75f),
        KeyboardLayoutKey(TEXT("F"), TEXT("F"), 3, 4.75f),
        KeyboardLayoutKey(TEXT("G"), TEXT("G"), 3, 5.75f),
        KeyboardLayoutKey(TEXT("H"), TEXT("H"), 3, 6.75f),
        KeyboardLayoutKey(TEXT("J"), TEXT("J"), 3, 7.75f),
        KeyboardLayoutKey(TEXT("K"), TEXT("K"), 3, 8.75f),
        KeyboardLayoutKey(TEXT("L"), TEXT("L"), 3, 9.75f),
        KeyboardLayoutKey(TEXT("Semicolon"), TEXT(";"), 3, 10.75f),
        KeyboardLayoutKey(TEXT("Apostrophe"), TEXT("'"), 3, 11.75f),
        KeyboardLayoutKey(TEXT("Enter"), TEXT("Enter"), 3, 12.75f, 2.25f),

        // Z row
        KeyboardLayoutKey(TEXT("LeftShift"), TEXT("Shift"), 4, 0.0f, 2.25f),
        KeyboardLayoutKey(TEXT("Z"), TEXT("Z"), 4, 2.25f),
        KeyboardLayoutKey(TEXT("X"), TEXT("X"), 4, 3.25f),
        KeyboardLayoutKey(TEXT("C"), TEXT("C"), 4, 4.25f),
        KeyboardLayoutKey(TEXT("V"), TEXT("V"), 4, 5.25f),
        KeyboardLayoutKey(TEXT("B"), TEXT("B"), 4, 6.25f),
        KeyboardLayoutKey(TEXT("N"), TEXT("N"), 4, 7.25f),
        KeyboardLayoutKey(TEXT("M"), TEXT("M"), 4, 8.25f),
        KeyboardLayoutKey(TEXT("Comma"), TEXT(","), 4, 9.25f),
        KeyboardLayoutKey(TEXT("Period"), TEXT("."), 4, 10.25f),
        KeyboardLayoutKey(TEXT("Slash"), TEXT("/"), 4, 11.25f),
        KeyboardLayoutKey(TEXT("RightShift"), TEXT("Shift"), 4, 12.25f, 2.75f),

        // Ctrl / Space row (the Windows and menu keys are left out: gaps after the Ctrl keys)
        KeyboardLayoutKey(TEXT("LeftControl"), TEXT("Ctrl"), 5, 0.0f, 1.5f),
        KeyboardLayoutKey(TEXT("LeftAlt"), TEXT("Alt"), 5, 2.5f, 1.5f),
        KeyboardLayoutKey(TEXT("SpaceBar"), TEXT("Space"), 5, 4.0f, 7.0f),
        KeyboardLayoutKey(TEXT("RightAlt"), TEXT("Alt"), 5, 11.0f, 1.5f),
        KeyboardLayoutKey(TEXT("RightControl"), TEXT("Ctrl"), 5, 13.5f, 1.5f),

        // Navigation cluster and arrows
        KeyboardLayoutKey(TEXT("Insert"), TEXT("Ins"), 1, 15.5f),
        KeyboardLayoutKey(TEXT("Home"), TEXT("Home"), 1, 16.5f),
        KeyboardLayoutKey(TEXT("PageUp"), TEXT("PgUp"), 1, 17.5f),
        KeyboardLayoutKey(TEXT("Delete"), TEXT("Del"), 2, 15.5f),
        KeyboardLayoutKey(TEXT("End"), TEXT("End"), 2, 16.5f),
        KeyboardLayoutKey(TEXT("PageDown"), TEXT("PgDn"), 2, 17.5f),
        KeyboardLayoutKey(TEXT("Up"), TEXT("Up"), 4, 16.5f),
        KeyboardLayoutKey(TEXT("Left"), TEXT("Lt"), 5, 15.5f),
        KeyboardLayoutKey(TEXT("Down"), TEXT("Dn"), 5, 16.5f),
        KeyboardLayoutKey(TEXT("Right"), TEXT("Rt"), 5, 17.5f),

        // Numpad
        KeyboardLayoutKey(TEXT("NumLock"), TEXT("Num"), 1, 19.0f),
        KeyboardLayoutKey(TEXT("Divide"), TEXT("/"), 1, 20.0f),
        KeyboardLayoutKey(TEXT("Multiply"), TEXT("*"), 1, 21.0f),
        KeyboardLayoutKey(TEXT("Subtract"), TEXT("-"), 1, 22.0f),
        KeyboardLayoutKey(TEXT("NumPadSeven"), TEXT("7"), 2, 19.0f),
        KeyboardLayoutKey(TEXT("NumPadEight"), TEXT("8"), 2, 20.0f),
        KeyboardLayoutKey(TEXT("NumPadNine"), TEXT("9"), 2, 21.0f),
        KeyboardLayoutKey(TEXT("Add"), TEXT("+"), 2, 22.0f, 1.0f, 2.0f),
        KeyboardLayoutKey(TEXT("NumPadFour"), TEXT("4"), 3, 19.0f),
        KeyboardLayoutKey(TEXT("NumPadFive"), TEXT("5"), 3, 20.0f),
        KeyboardLayoutKey(TEXT("NumPadSix"), TEXT("6"), 3, 21.0f),
        KeyboardLayoutKey(TEXT("NumPadOne"), TEXT("1"), 4, 19.0f),
        KeyboardLayoutKey(TEXT("NumPadTwo"), TEXT("2"), 4, 20.0f),
        KeyboardLayoutKey(TEXT("NumPadThree"), TEXT("3"), 4, 21.0f),
        KeyboardLayoutMirror(TEXT("Enter"), TEXT("Enter"), 4, 22.0f, 1.0f, 2.0f),
        KeyboardLayoutKey(TEXT("NumPadZero"), TEXT("0"), 5, 19.0f, 2.0f),
        KeyboardLayoutKey(TEXT("Decimal"), TEXT("."), 5, 21.0f),

        // Mouse (relative to its top-left)
        KeyboardLayoutMouse(TEXT("LeftMouseButton"), nullptr, TEXT("LMB"), ESOLMouseButton::Left,
            0.0f, 0.0f, 1.25f, 2.0f),
        KeyboardLayoutMouse(TEXT("MouseWheelAxis"), TEXT("MiddleMouseButton"), TEXT("Wheel"), ESOLMouseButton::Wheel,
            1.25f, 0.0f, 0.5f, 2.0f),
        KeyboardLayoutMouse(TEXT("RightMouseButton"), nullptr, TEXT("RMB"), ESOLMouseButton::Right,
            1.75f, 0.0f, 1.25f, 2.0f),
        KeyboardLayoutMouse(TEXT("Mouse2D"), nullptr, TEXT("Move"), ESOLMouseButton::Move, 0.0f, 2.0f, 3.0f, 2.5f),
    };

    // The gap between the keyboard and the mouse (key units), beside it or below it
    constexpr float KEYBOARD_LAYOUT_MOUSE_GAP = 0.5f;

    // D34 Stacked: keyboard keys from this X are right of the main block (Scroll Lock, Pause, the navigation cluster,
    // the arrows, the numpad) and move under it with the mouse, this far under the main block's bottom
    constexpr float KEYBOARD_LAYOUT_CLUSTER_LEFT = 15.5f;
    constexpr float KEYBOARD_LAYOUT_STACK_GAP = 0.5f;

    // The keyboard's and the mouse's sizes in key units and the Stacked arrangement, measured from the table once
    struct FKeyboardLayoutExtents
    {
        FVector2f Keyboard = FVector2f::ZeroVector;     // Keyboard keys from the origin
        FVector2f Mouse = FVector2f::ZeroVector;        // Mouse zones from the mouse's top-left
        FVector2f Main = FVector2f::ZeroVector;         // The main block's keys (X under the cluster edge)
        FVector2f StackOffset = FVector2f::ZeroVector;  // Stacked: added to a cluster or mouse key's Beside rect
        FVector2f Stacked = FVector2f::ZeroVector;      // Stacked: the whole diagram's size
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a key moves under the main block in Stacked (a mouse zone or a keyboard key right of the block)
    bool KeyboardLayoutIsStackedGroup(const FSOLLayoutKey& key)
    {
        return key.Mouse != ESOLMouseButton::None || key.X >= KEYBOARD_LAYOUT_CLUSTER_LEFT;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the keyboard's and the mouse's sizes (the right and bottom edges of their keys) and the Stacked offset
    // and extent
    const FKeyboardLayoutExtents& KeyboardLayoutExtents()
    {
        static const FKeyboardLayoutExtents extents = []()
        {
            // The keyboard, the main block and the mouse, from their keys' far edges
            FKeyboardLayoutExtents measured;
            for (const FSOLLayoutKey& key : KEYBOARD_LAYOUT_KEYS)
            {
                FVector2f& size = key.Mouse != ESOLMouseButton::None ? measured.Mouse : measured.Keyboard;
                size.X = FMath::Max(size.X, key.X + key.Width);
                size.Y = FMath::Max(size.Y, key.Y + key.Height);
                if (!KeyboardLayoutIsStackedGroup(key))
                {
                    measured.Main.X = FMath::Max(measured.Main.X, key.X + key.Width);
                    measured.Main.Y = FMath::Max(measured.Main.Y, key.Y + key.Height);
                }
            }

            // Stacked: the right part of the Beside layout (the cluster edge to the mouse's right edge, its top at 0)
            // centred under the main block, the gap below it; the extent is the main block and that group
            const float besideRight = measured.Keyboard.X + KEYBOARD_LAYOUT_MOUSE_GAP + measured.Mouse.X;
            const float groupWidth = besideRight - KEYBOARD_LAYOUT_CLUSTER_LEFT;
            const float groupX = 0.5f * (measured.Main.X - groupWidth);
            measured.StackOffset = FVector2f(groupX - KEYBOARD_LAYOUT_CLUSTER_LEFT,
                measured.Main.Y + KEYBOARD_LAYOUT_STACK_GAP);
            const float besideBottom = FMath::Max(measured.Keyboard.Y, measured.Mouse.Y);
            measured.Stacked = FVector2f(FMath::Max(measured.Main.X, groupX + groupWidth),
                FMath::Max(measured.Main.Y, besideBottom + measured.StackOffset.Y));
            return measured;
        }();
        return extents;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the mouse's top-left for a placement: right of the keyboard, bottom-aligned with it (Stacked: that
    // place moved under the main block), or under the keyboard at its right edge (below the numpad)
    FVector2f KeyboardLayoutMouseOrigin(const ESOLMousePlacement placement)
    {
        const FKeyboardLayoutExtents& extents = KeyboardLayoutExtents();
        if (placement == ESOLMousePlacement::Beside || placement == ESOLMousePlacement::Stacked)
        {
            const FVector2f beside(extents.Keyboard.X + KEYBOARD_LAYOUT_MOUSE_GAP,
                FMath::Max(0.0f, extents.Keyboard.Y - extents.Mouse.Y));
            return placement == ESOLMousePlacement::Stacked ? beside + extents.StackOffset : beside;
        }
        return FVector2f(FMath::Max(0.0f, extents.Keyboard.X - extents.Mouse.X),
            extents.Keyboard.Y + KEYBOARD_LAYOUT_MOUSE_GAP);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the layout's keys (the static table)
TConstArrayView<FSOLLayoutKey> SOLKeyboardLayout::Keys()
{
    return TConstArrayView<FSOLLayoutKey>(KEYBOARD_LAYOUT_KEYS, UE_ARRAY_COUNT(KEYBOARD_LAYOUT_KEYS));
}

//////////////////////////////////////////////////////////////////////////
// Returns the number of layout keys
int32 SOLKeyboardLayout::KeyCount()
{
    return UE_ARRAY_COUNT(KEYBOARD_LAYOUT_KEYS);
}

//////////////////////////////////////////////////////////////////////////
// Returns the key with an engine key name or second name (case-insensitive, never a mirror), or INDEX_NONE
int32 SOLKeyboardLayout::FindKey(const FStringView keyName)
{
    if (keyName.IsEmpty())
    {
        return INDEX_NONE;
    }
    for (int32 index = 0; index < KeyCount(); ++index)
    {
        const FSOLLayoutKey& key = KEYBOARD_LAYOUT_KEYS[index];
        if (key.bMirror)
        {
            continue;
        }
        if (keyName.Equals(key.KeyName, ESearchCase::IgnoreCase)
            || (key.AltKeyName != nullptr && keyName.Equals(key.AltKeyName, ESearchCase::IgnoreCase)))
        {
            return index;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the mouse placement for a key unit: beside from DIAGRAM_MOUSE_BELOW_UNIT up, else (or not finite) below
ESOLMousePlacement SOLKeyboardLayout::MousePlacement(const float unitPx)
{
    return FMath::IsFinite(unitPx) && unitPx >= SOL::Help::DIAGRAM_MOUSE_BELOW_UNIT ? ESOLMousePlacement::Beside
        : ESOLMousePlacement::Below;
}

//////////////////////////////////////////////////////////////////////////
// Returns a key's rect in key units (mouse zones moved to the mouse's place), or an empty box out of range
FBox2f SOLKeyboardLayout::KeyRect(const int32 index, const ESOLMousePlacement placement)
{
    if (index < 0 || index >= KeyCount())
    {
        return FBox2f(ForceInit);
    }
    const FSOLLayoutKey& key = KEYBOARD_LAYOUT_KEYS[index];
    FVector2f origin = FVector2f::ZeroVector;
    if (key.Mouse != ESOLMouseButton::None)
    {
        origin = KeyboardLayoutMouseOrigin(placement);
    }
    else if (placement == ESOLMousePlacement::Stacked && KeyboardLayoutIsStackedGroup(key))
    {
        origin = KeyboardLayoutExtents().StackOffset;
    }
    const FVector2f min = origin + FVector2f(key.X, key.Y);
    return FBox2f(min, min + FVector2f(key.Width, key.Height));
}

//////////////////////////////////////////////////////////////////////////
// Returns the diagram's size in key units: the keyboard and the mouse at its place (Stacked: measured once)
FVector2f SOLKeyboardLayout::Extent(const ESOLMousePlacement placement)
{
    const FKeyboardLayoutExtents& extents = KeyboardLayoutExtents();
    if (placement == ESOLMousePlacement::Stacked)
    {
        return extents.Stacked;
    }
    const FVector2f mouseFar = KeyboardLayoutMouseOrigin(placement) + extents.Mouse;
    return FVector2f(FMath::Max(extents.Keyboard.X, mouseFar.X), FMath::Max(extents.Keyboard.Y, mouseFar.Y));
}

//////////////////////////////////////////////////////////////////////////
// Returns the key under a point in key units (edges inclusive), or INDEX_NONE
int32 SOLKeyboardLayout::KeyAt(const FVector2f& pointUnits, const ESOLMousePlacement placement)
{
    for (int32 index = 0; index < KeyCount(); ++index)
    {
        const FBox2f rect = KeyRect(index, placement);
        if (pointUnits.X >= rect.Min.X && pointUnits.X <= rect.Max.X && pointUnits.Y >= rect.Min.Y
            && pointUnits.Y <= rect.Max.Y)
        {
            return index;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the scale that fits the diagram into a width (D34): one row with the mouse beside, else one row with the
// mouse below, else stacked, each when its unit allows it
FSOLDiagramScale SOLKeyboardLayout::FitToWidth(const float widthPx)
{
    using namespace SOL::Help;
    FSOLDiagramScale scale;
    if (!FMath::IsFinite(widthPx) || widthPx <= 0.0f)
    {
        return scale;
    }
    const float besideUnit = widthPx / Extent(ESOLMousePlacement::Beside).X;
    const float belowUnit = widthPx / Extent(ESOLMousePlacement::Below).X;
    float unit = besideUnit;
    if (besideUnit >= DIAGRAM_MOUSE_BELOW_UNIT)
    {
        scale.Placement = ESOLMousePlacement::Beside;
    }
    else if (belowUnit >= DIAGRAM_STACK_UNIT)
    {
        scale.Placement = ESOLMousePlacement::Below;
        unit = belowUnit;
    }
    else
    {
        scale.Placement = ESOLMousePlacement::Stacked;
        unit = widthPx / Extent(ESOLMousePlacement::Stacked).X;
    }
    scale.UnitPx = FMath::Min(unit, DIAGRAM_MAX_UNIT);
    scale.bShortLabels = scale.UnitPx >= DIAGRAM_MIN_UNIT;
    return scale;
}

//////////////////////////////////////////////////////////////////////////
// Converts a widget point to diagram key units (zero for a non-positive unit)
FVector2f SOLKeyboardLayout::ToUnits(const FVector2f& px, const FSOLDiagramScale& scale)
{
    if (!(scale.UnitPx > 0.0f))
    {
        return FVector2f::ZeroVector;
    }
    return (px - scale.OriginPx) / scale.UnitPx;
}

//////////////////////////////////////////////////////////////////////////
// Converts diagram key units to a widget point
FVector2f SOLKeyboardLayout::FromUnits(const FVector2f& units, const FSOLDiagramScale& scale)
{
    return scale.OriginPx + units * scale.UnitPx;
}

//////////////////////////////////////////////////////////////////////////
// Stacks the Keyboard tab's sections in a width: legend, detail (its cap row and detailLines lines, D36; above the
// diagram, D43), diagram, other keys, unbound
FSOLDiagramPanel SOLKeyboardLayout::LayoutPanel(const float widthPx, const bool bOtherKeys, const bool bUnbound,
    const int32 detailLines)
{
    using namespace SOL::Help;
    FSOLDiagramPanel panel;
    const float width = FMath::IsFinite(widthPx) && widthPx > 0.0f ? widthPx : 0.0f;
    panel.Scale = FitToWidth(width);

    // The legend grid: as many columns as fit, at least one, at most one per area
    const int32 fit = FMath::FloorToInt32((width + DIAGRAM_LEGEND_GAP) / (DIAGRAM_LEGEND_ITEM_WIDTH
        + DIAGRAM_LEGEND_GAP));
    panel.LegendColumns = FMath::Clamp(fit, 1, AREA_COUNT);
    panel.LegendRows = FMath::DivideAndRoundUp(AREA_COUNT, panel.LegendColumns);
    panel.Legend = FBox2f(FVector2f::ZeroVector, FVector2f(width, static_cast<float>(panel.LegendRows)
        * DIAGRAM_LEGEND_ROW_HEIGHT));

    // Each section under the previous shown one; a hidden section is a zero-height box with no gap
    float bottom = panel.Legend.Max.Y;
    const auto stack = [&bottom](const bool bShown, const float sectionWidth, const float height)
    {
        if (!bShown)
        {
            return FBox2f(FVector2f(0.0f, bottom), FVector2f(sectionWidth, bottom));
        }
        const float top = bottom + DIAGRAM_SECTION_GAP;
        bottom = top + height;
        return FBox2f(FVector2f(0.0f, top), FVector2f(sectionWidth, bottom));
    };
    panel.DetailLines = FMath::Clamp(detailLines, 1, DIAGRAM_DETAIL_LINES);
    panel.Detail = stack(true, width, DIAGRAM_DETAIL_CAP_HEIGHT + static_cast<float>(panel.DetailLines)
        * DIAGRAM_DETAIL_LINE_HEIGHT);
    const FVector2f diagramSize = Extent(panel.Scale.Placement) * panel.Scale.UnitPx;
    panel.Diagram = stack(true, diagramSize.X, diagramSize.Y);
    panel.Scale.OriginPx = panel.Diagram.Min;
    panel.Other = stack(bOtherKeys, width, DIAGRAM_STRIP_HEIGHT);
    panel.Unbound = stack(bUnbound, width, DIAGRAM_UNBOUND_HEIGHT);
    panel.Height = panel.Unbound.Max.Y;
    return panel;
}

//////////////////////////////////////////////////////////////////////////
// Returns the legend item rect of an area index (row-major in the grid), or an empty box out of range
FBox2f SOLKeyboardLayout::LegendItemRect(const FSOLDiagramPanel& panel, const int32 area)
{
    using namespace SOL::Help;
    if (area < 0 || area >= AREA_COUNT)
    {
        return FBox2f(ForceInit);
    }
    const int32 columns = FMath::Max(panel.LegendColumns, 1);
    const FVector2f min = panel.Legend.Min + FVector2f(static_cast<float>(area % columns) * (DIAGRAM_LEGEND_ITEM_WIDTH
        + DIAGRAM_LEGEND_GAP), static_cast<float>(area / columns) * DIAGRAM_LEGEND_ROW_HEIGHT);
    return FBox2f(min, min + FVector2f(DIAGRAM_LEGEND_ITEM_WIDTH, DIAGRAM_LEGEND_ROW_HEIGHT));
}

//////////////////////////////////////////////////////////////////////////
// D35: fits a key cap's top line: the legend beside the "+N" badge when both fit, else the badge hidden and the legend
// centred in the span, clipped at its right end when wider
FSOLKeyTextFit SOLKeyboardLayout::FitKeyText(const float left, const float right, const float legendWidth,
    const float badgeWidth)
{
    FSOLKeyTextFit fit;
    fit.LegendX = left;
    const float span = right - left;
    const float legend = FMath::Max(legendWidth, 0.0f);
    const bool bHasBadge = badgeWidth > 0.0f;

    // No room: nothing drawn, a legend counts as clipped
    if (!(span > 0.0f))
    {
        fit.bClipped = legend > 0.0f;
        return fit;
    }

    // No legend: the badge alone needs only the span
    if (legend <= 0.0f)
    {
        fit.bBadge = bHasBadge && badgeWidth <= span + KINDA_SMALL_NUMBER;
        return fit;
    }

    // The legend centred left of the badge when both fit
    if (bHasBadge)
    {
        const float beside = span - badgeWidth - SOL::Help::DIAGRAM_BADGE_GAP;
        if (legend <= beside + KINDA_SMALL_NUMBER)
        {
            fit.LegendX = left + 0.5f * (beside - legend);
            fit.LegendWidth = legend;
            fit.bBadge = true;
            return fit;
        }
    }

    // The badge hidden: the legend centred in the whole span, or clipped to it
    if (legend <= span + KINDA_SMALL_NUMBER)
    {
        fit.LegendX = left + 0.5f * (span - legend);
        fit.LegendWidth = legend;
        return fit;
    }
    fit.LegendWidth = span;
    fit.bClipped = true;
    return fit;
}
