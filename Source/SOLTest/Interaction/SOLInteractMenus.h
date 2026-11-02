/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Interaction/SOLActionTable.h"

#include "CoreMinimal.h"

// One overlay icon as the pointer can hit it: its screen centre and radius (canvas pixels) and its object
struct FSOLIconHit
{
    FVector2D CenterPx = FVector2D::ZeroVector;
    double RadiusPx = 0.0;
    FSOLObjectRef Object;
};

// The flight radial menu's state (SDD 22 section 2.4): closed, or open at a centre over an object's actions
struct FSOLRadialMenuState
{
    bool bOpen = false;
    FSOLObjectRef Object;                           // The object whose actions the sectors are
    FVector2D CenterPx = FVector2D::ZeroVector;     // Menu centre (canvas pixels)
    int32 Count = 0;                                // Sectors (actions)
    int32 Highlighted = INDEX_NONE;                 // Sector under the pointer, INDEX_NONE in the centre
};

// What a radial menu call did
enum class ESOLRadialOutcome : uint8
{
    None,       // Nothing changed (closed menu, nothing to open)
    Opened,     // The menu opened
    Confirmed,  // Released over an enabled sector: the caller runs that action
    Cancelled,  // Released in the centre or over a disabled sector, or cancelled (Esc): nothing runs
};

// The map list menu's state: rows and the highlighted one
struct FSOLListMenuState
{
    int32 Count = 0;
    int32 Highlighted = INDEX_NONE;
};

/**
 * Pure logic of the interaction menus: icon hit-testing, the radial menu's sector geometry and press-drag-release
 * state, and the list menu's highlight and placement. Screen angles follow FSOLHudShapeBatch::AddArc: radians from +X
 * toward +Y (+Y is down), so increasing angle runs clockwise on screen. Sector i of n has its middle at
 * SectorMidAngleRad(i, n) = -pi/2 + i x 2 pi / n (sector 0 at the top, then clockwise) and spans +-pi / n around it.
 */
namespace SOLRadialMenu
{
    // Returns the hit whose centre is nearest the cursor among those within RadiusPx + slackPx of it (Euclidean,
    // canvas pixels; a hit with RadiusPx <= 0 never qualifies; a negative slack counts as 0); ties: the lower index.
    // INDEX_NONE when none qualifies or the cursor is not finite
    SOLTEST_API int32 HitTestIcons(TConstArrayView<FSOLIconHit> hits, const FVector2D& cursorPx, double slackPx);

    // Returns the middle angle of sector index of count (see above); 0 for count <= 0
    SOLTEST_API double SectorMidAngleRad(int32 index, int32 count);

    // Returns the sector an offset from the menu centre points into: INDEX_NONE for count <= 0, an offset shorter than
    // innerPx (the cancel zone) or a non-finite offset; else the sector whose angular span holds the offset's angle
    // (any distance beyond innerPx, however far, still selects); a boundary angle belongs to the higher sector
    // (wrapping to 0)
    SOLTEST_API int32 SectorAt(const FVector2D& offsetPx, int32 count, double innerPx);

    // Returns where the menu opens for an icon: the icon centre clamped per axis to [marginPx, size - marginPx] (the
    // middle of the axis when the screen is narrower than 2 x marginPx)
    SOLTEST_API FVector2D PlaceCenter(const FVector2D& iconPx, const FVector2D& viewSizePx, double marginPx);

    // Opens the menu at centerPx over count actions of an object (nothing highlighted): Opened; None (state unchanged)
    // when count <= 0 or the object is not valid. Count is clamped to SOL::Interact::MAX_ACTIONS
    SOLTEST_API ESOLRadialOutcome Open(FSOLRadialMenuState& state, const FSOLObjectRef& object,
        const FVector2D& centerPx, int32 count);

    // Updates the highlighted sector from the pointer (SectorAt of pointer - centre); no-op while closed
    SOLTEST_API void Move(FSOLRadialMenuState& state, const FVector2D& pointerPx, double innerPx);

    // Releases at the pointer: the sector there (SectorAt) is chosen when it is enabled[sector] (a sector beyond the
    // enabled view counts as disabled): Confirmed with outSector set; otherwise Cancelled with outSector INDEX_NONE.
    // Closes the menu either way (state back to the default). Returns None (outSector INDEX_NONE) while closed
    SOLTEST_API ESOLRadialOutcome Release(FSOLRadialMenuState& state, const FVector2D& pointerPx, double innerPx,
        TConstArrayView<bool> enabled, int32& outSector);

    // Cancels an open menu (Esc, focus loss, the menu or map taking the input): Cancelled and closed; None while closed
    SOLTEST_API ESOLRadialOutcome Cancel(FSOLRadialMenuState& state);
}

namespace SOLListMenu
{
    // Opens the list over enabled.Num() rows: the first enabled row is highlighted, row 0 when none is enabled,
    // INDEX_NONE for an empty list
    SOLTEST_API void Open(FSOLListMenuState& state, TConstArrayView<bool> enabled);

    // Moves the highlight by step rows, wrapping in both directions (disabled rows can be highlighted, so their reason
    // shows); no-op for an empty list. An INDEX_NONE highlight moves to row 0 for step > 0 and the last row for
    // step < 0
    SOLTEST_API void MoveHighlight(FSOLListMenuState& state, int32 step);

    // Returns true when the highlighted row is valid and enabled[row] (a row beyond the view counts as disabled)
    SOLTEST_API bool CanConfirm(const FSOLListMenuState& state, TConstArrayView<bool> enabled);

    // Returns the top-left corner of a panel of panelSizePx opened at the cursor: cursor + offsetPx on each axis, moved
    // to the other side of the cursor (cursor - offsetPx - size) on an axis where it would pass the screen edge less
    // marginPx, then clamped to [marginPx, size - marginPx - panel] (marginPx when the panel does not fit)
    SOLTEST_API FVector2D PlacePanel(const FVector2D& cursorPx, const FVector2D& panelSizePx,
        const FVector2D& viewSizePx, double offsetPx, double marginPx);
}
