/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Menu/SOLKeyCaps.h"
#include "SOLConstants.h"

#include "CoreMinimal.h"

// One key of the keyboard diagram (SDD 33 section 7) in key units (1 = a letter key; +Y down). Keyboard keys are placed
// on the keyboard (top-left at 0, 0: the function row, then the main block, the navigation cluster, the arrows and the
// numpad); mouse keys (Mouse != None) are placed relative to the mouse's top-left, which KeyRect moves to its place in
// the arrangement (ESOLMousePlacement; Stacked also moves the keys right of the main block)
struct FSOLLayoutKey
{
    const TCHAR* KeyName = TEXT("");                    // Engine key name (FKey name: "SpaceBar", "NumPadOne")
    const TCHAR* AltKeyName = nullptr;                  // A second engine key on the same cap (the wheel zone is
                                                        // MouseWheelAxis and MiddleMouseButton), or nullptr
    const TCHAR* Legend = TEXT("");                     // Shown legend
    int32 Row = 0;                                      // 0 the function row ... 5 the Ctrl / Space row; mouse keys 0
    float X = 0.0f;                                     // Left edge
    float Y = 0.0f;                                     // Top edge (the function row is 0.5 above the number row)
    float Width = 1.0f;
    float Height = 1.0f;                                // 2 for the numpad Add and Enter
    ESOLMouseButton Mouse = ESOLMouseButton::None;      // The mouse zone (None for a keyboard key)
    bool bMirror = false;                               // A second cap of an engine key with one name for two physical
                                                        // keys (the numpad Enter is "Enter"): drawn like the main key,
                                                        // never returned by FindKey
};

// The diagram's arrangement (where the mouse and the clusters sit)
enum class ESOLMousePlacement : uint8
{
    Beside,     // One row: the keyboard, the mouse right of it (the key unit is at least DIAGRAM_MOUSE_BELOW_UNIT)
    Below,      // One row keyboard, the mouse under its right end
    Stacked,    // D34: the main block on top; under it, 0.5 units lower and centred under the main block, everything
                // right of the main block in Beside (Scroll Lock / Pause, navigation and arrows, numpad, the mouse),
                // each key its Beside rect moved by (-13.5, +7)
};

// How the diagram is scaled into a width: pixels (menu units) per key unit, the mouse placement, the diagram's
// top-left in the widget and whether the short action labels fit
struct FSOLDiagramScale
{
    float UnitPx = SOL::Help::DIAGRAM_MIN_UNIT;
    ESOLMousePlacement Placement = ESOLMousePlacement::Below;
    FVector2f OriginPx = FVector2f::ZeroVector;
    bool bShortLabels = true;
};

// The Keyboard tab's sections stacked in a width (33c, SOLKeyboardLayout::LayoutPanel), in menu units from the panel's
// top-left: the area legend, the diagram (keyboard and mouse), the "Other keys" strip, the "Unbound" line and the hover
// detail strip
struct FSOLDiagramPanel
{
    FSOLDiagramScale Scale;                             // FitToWidth(width) with OriginPx = Diagram.Min
    FBox2f Legend = FBox2f(ForceInit);                  // The area legend: AREA_COUNT swatch + name items in a grid
    int32 LegendColumns = 1;                            // max(1, floor((width + DIAGRAM_LEGEND_GAP) /
                                                        // (DIAGRAM_LEGEND_ITEM_WIDTH + DIAGRAM_LEGEND_GAP))), at most
                                                        // AREA_COUNT
    int32 LegendRows = 1;                               // ceil(AREA_COUNT / LegendColumns)
    FBox2f Diagram = FBox2f(ForceInit);                 // The keyboard and mouse: Extent(Scale.Placement) * UnitPx
    FBox2f Other = FBox2f(ForceInit);                   // The "Other keys" strip, DIAGRAM_STRIP_HEIGHT tall when shown
    FBox2f Unbound = FBox2f(ForceInit);                 // The "Unbound" line, DIAGRAM_UNBOUND_HEIGHT tall when shown
    FBox2f Detail = FBox2f(ForceInit);                  // The hover strip, DIAGRAM_DETAIL_CAP_HEIGHT + DetailLines *
                                                        // DIAGRAM_DETAIL_LINE_HEIGHT tall
    int32 DetailLines = 1;                              // clamp(detailLines, 1, DIAGRAM_DETAIL_LINES)
    float Height = 0.0f;                                // Detail.Max.Y
};

// D35: how a key cap's top line fits (SOLKeyboardLayout::FitKeyText), in menu units
struct FSOLKeyTextFit
{
    float LegendX = 0.0f;                               // The legend's drawn left edge
    float LegendWidth = 0.0f;                           // The drawn width: the legend's width, or the span when clipped
    bool bClipped = false;                              // The legend is wider than its span and clipped at its right
    bool bBadge = false;                                // The "+N" badge shows (right-aligned at the span's right)
};

/**
 * The keyboard diagram's static layout (SDD 33 section 7, D19), pure and test-first: a compact US layout with the
 * numpad (about 23 key units wide, 6.5 tall) and the mouse's zones (LMB, RMB, the wheel / MMB, the move), the key
 * lookup, the hit test and the scale for a width. The table is static data; nothing allocates.
 */
namespace SOLKeyboardLayout
{
    // Returns the layout's keys (keyboard keys, then the mouse keys)
    SOLTEST_API TConstArrayView<FSOLLayoutKey> Keys();

    // Returns the number of keys of Keys()
    SOLTEST_API int32 KeyCount();

    // Returns the key whose KeyName or AltKeyName is this engine key name (case-insensitive; a mirror key is never
    // returned), or INDEX_NONE
    SOLTEST_API int32 FindKey(FStringView keyName);

    // Returns the mouse placement for a key unit: Below when unitPx is under DIAGRAM_MOUSE_BELOW_UNIT or is not a
    // finite number, else Beside
    SOLTEST_API ESOLMousePlacement MousePlacement(float unitPx);

    // Returns a key's rect in diagram key units for an arrangement (mouse keys moved to the mouse's place; in Stacked
    // every key right of the main block and every mouse key is its Beside rect moved under the main block); an empty
    // box (FBox2f(ForceInit)) for an index outside the table
    SOLTEST_API FBox2f KeyRect(int32 index, ESOLMousePlacement placement);

    // Returns the whole diagram's size in key units (keyboard and mouse) for a mouse placement
    SOLTEST_API FVector2f Extent(ESOLMousePlacement placement);

    // Returns the key under a point in diagram key units (KeyRect, edges inclusive), or INDEX_NONE in a gap or outside
    // the extent
    SOLTEST_API int32 KeyAt(const FVector2f& pointUnits, ESOLMousePlacement placement);

    // Returns the scale that fits the diagram into widthPx (D34): the mouse beside when the width over Extent(Beside)
    // is at least DIAGRAM_MOUSE_BELOW_UNIT; else the mouse below when the width over Extent(Below) is at least
    // DIAGRAM_STACK_UNIT; else Stacked at the width over Extent(Stacked). The unit is at most DIAGRAM_MAX_UNIT; below
    // DIAGRAM_MIN_UNIT it still fits the width and bShortLabels is false. OriginPx is 0. A width that is not a
    // positive finite number gives the default scale (DIAGRAM_MIN_UNIT, Below, labels shown)
    SOLTEST_API FSOLDiagramScale FitToWidth(float widthPx);

    // Converts a widget point (menu units) to diagram key units: (px - OriginPx) / UnitPx (zero for a non-positive
    // unit)
    SOLTEST_API FVector2f ToUnits(const FVector2f& px, const FSOLDiagramScale& scale);

    // Converts diagram key units to a widget point: OriginPx + units x UnitPx
    SOLTEST_API FVector2f FromUnits(const FVector2f& units, const FSOLDiagramScale& scale);

    // Stacks the Keyboard tab's sections in a width, top to bottom: Legend, Diagram, Other (if bOtherKeys), Unbound
    // (if bUnbound), Detail, each DIAGRAM_SECTION_GAP under the previous shown one; a hidden section is a zero-height
    // box at the previous section's bottom and adds no gap. Every box starts at x 0; Legend, Other, Unbound and Detail
    // are the full width, the Diagram its scaled extent. A width that is not a positive finite number lays out at width
    // 0 with the default FitToWidth scale (OriginPx moved to Diagram.Min). The Detail box (D36) is
    // DIAGRAM_DETAIL_CAP_HEIGHT + clamp(detailLines, 1, DIAGRAM_DETAIL_LINES) * DIAGRAM_DETAIL_LINE_HEIGHT tall and
    // DetailLines that clamped count
    SOLTEST_API FSOLDiagramPanel LayoutPanel(float widthPx, bool bOtherKeys, bool bUnbound, int32 detailLines);

    // D35: fits a key cap's top line between left and right (menu units). With a badge (badgeWidth > 0):
    // a legend that fits in [left, right - badgeWidth - DIAGRAM_BADGE_GAP] is centred there and the badge shows.
    // Otherwise (or with no badge) the badge hides and the legend is centred in [left, right]; a legend wider than that
    // span starts at left, LegendWidth = right - left, bClipped. A span with right <= left: LegendX = left, LegendWidth
    // 0, bClipped = legendWidth > 0, no badge. A legend width of 0: LegendX = left, LegendWidth 0, not clipped, and the
    // badge shows when badgeWidth <= right - left. "Fits" is width <= span within KINDA_SMALL_NUMBER
    SOLTEST_API FSOLKeyTextFit FitKeyText(float left, float right, float legendWidth, float badgeWidth);

    // Returns the legend item rect of an area index (0 .. AREA_COUNT - 1, row-major in the legend grid), or an empty
    // box (FBox2f(ForceInit)) out of range
    SOLTEST_API FBox2f LegendItemRect(const FSOLDiagramPanel& panel, int32 area);
}
