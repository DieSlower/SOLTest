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
// numpad); mouse keys (Mouse != None) are placed relative to the mouse's top-left, which KeyRect moves beside or below
// the keyboard (ESOLMousePlacement)
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

// Where the mouse sits in the diagram
enum class ESOLMousePlacement : uint8
{
    Beside,     // Right of the keyboard (the key unit is at least DIAGRAM_MOUSE_BELOW_UNIT)
    Below,      // Under the keyboard (a narrow view)
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

    // Returns a key's rect in diagram key units for a mouse placement (mouse keys moved to the mouse's place); an
    // empty box (FBox2f(ForceInit)) for an index outside the table
    SOLTEST_API FBox2f KeyRect(int32 index, ESOLMousePlacement placement);

    // Returns the whole diagram's size in key units (keyboard and mouse) for a mouse placement
    SOLTEST_API FVector2f Extent(ESOLMousePlacement placement);

    // Returns the key under a point in diagram key units (KeyRect, edges inclusive), or INDEX_NONE in a gap or outside
    // the extent
    SOLTEST_API int32 KeyAt(const FVector2f& pointUnits, ESOLMousePlacement placement);

    // Returns the scale that fits the diagram into widthPx: the unit is the width over the extent (the mouse beside
    // first; if that unit is under DIAGRAM_MOUSE_BELOW_UNIT, the mouse below and the unit from that extent), at most
    // DIAGRAM_MAX_UNIT; below DIAGRAM_MIN_UNIT the unit still fits the width and bShortLabels is false. OriginPx is 0.
    // A width that is not a positive finite number gives the default scale (DIAGRAM_MIN_UNIT, Below, labels shown)
    SOLTEST_API FSOLDiagramScale FitToWidth(float widthPx);

    // Converts a widget point (menu units) to diagram key units: (px - OriginPx) / UnitPx (zero for a non-positive
    // unit)
    SOLTEST_API FVector2f ToUnits(const FVector2f& px, const FSOLDiagramScale& scale);

    // Converts diagram key units to a widget point: OriginPx + units x UnitPx
    SOLTEST_API FVector2f FromUnits(const FVector2f& units, const FSOLDiagramScale& scale);
}
