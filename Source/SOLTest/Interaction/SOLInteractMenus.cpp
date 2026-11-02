/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLInteractMenus.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns true when both components of a 2D vector are finite
    bool InteractMenusIsFinite(const FVector2D& value)
    {
        return FMath::IsFinite(value.X) && FMath::IsFinite(value.Y);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a panel's corner on one axis: past the cursor by the offset, flipped to the other side when it would
    // pass the edge less the margin, then kept inside the margins (the margin when it does not fit)
    double InteractMenusPlaceAxis(const double cursor, const double panel, const double size, const double offset,
        const double margin)
    {
        double corner = cursor + offset;
        if (corner + panel > size - margin)
        {
            corner = cursor - offset - panel;
        }
        return FMath::Max(margin, FMath::Min(corner, size - margin - panel));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the hit nearest the cursor among those whose radius plus the slack reaches it; ties: the lower index
int32 SOLRadialMenu::HitTestIcons(TConstArrayView<FSOLIconHit> hits, const FVector2D& cursorPx, const double slackPx)
{
    if (!InteractMenusIsFinite(cursorPx))
    {
        return INDEX_NONE;
    }
    const double slack = FMath::IsFinite(slackPx) ? FMath::Max(slackPx, 0.0) : 0.0;
    int32 best = INDEX_NONE;
    double bestDistanceSq = 0.0;
    for (int32 index = 0; index < hits.Num(); ++index)
    {
        const FSOLIconHit& hit = hits[index];
        if (!(hit.RadiusPx > 0.0))
        {
            continue;
        }
        const double distanceSq = FVector2D::DistSquared(hit.CenterPx, cursorPx);
        const double reach = hit.RadiusPx + slack;
        if (distanceSq <= reach * reach && (best == INDEX_NONE || distanceSq < bestDistanceSq))
        {
            best = index;
            bestDistanceSq = distanceSq;
        }
    }
    return best;
}

//////////////////////////////////////////////////////////////////////////
// Returns the middle angle of a sector: the top, then clockwise on screen
double SOLRadialMenu::SectorMidAngleRad(const int32 index, const int32 count)
{
    return count > 0 ? -UE_DOUBLE_HALF_PI + index * (2.0 * UE_DOUBLE_PI / count) : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns the sector an offset from the menu centre points into (INDEX_NONE in the cancel zone)
int32 SOLRadialMenu::SectorAt(const FVector2D& offsetPx, const int32 count, const double innerPx)
{
    if (count <= 0 || !InteractMenusIsFinite(offsetPx) || offsetPx.Size() < innerPx)
    {
        return INDEX_NONE;
    }

    // Measured from sector 0's leading edge, so floor puts a boundary into the higher sector
    const double widthRad = 2.0 * UE_DOUBLE_PI / count;
    double fromStart = FMath::Fmod(FMath::Atan2(offsetPx.Y, offsetPx.X) + UE_DOUBLE_HALF_PI + 0.5 * widthRad,
        2.0 * UE_DOUBLE_PI);
    if (fromStart < 0.0)
    {
        fromStart += 2.0 * UE_DOUBLE_PI;
    }
    const int32 sector = FMath::FloorToInt32(fromStart / widthRad);
    return sector >= count ? 0 : FMath::Max(sector, 0);
}

//////////////////////////////////////////////////////////////////////////
// Returns where the menu opens for an icon: clamped per axis inside the margin (the middle on a narrow axis)
FVector2D SOLRadialMenu::PlaceCenter(const FVector2D& iconPx, const FVector2D& viewSizePx, const double marginPx)
{
    // One axis: the middle of a screen narrower than both margins, else the icon clamped inside them
    const auto placeAxis = [marginPx](const double icon, const double size)
    {
        return size < 2.0 * marginPx ? 0.5 * size : FMath::Clamp(icon, marginPx, size - marginPx);
    };
    return FVector2D(placeAxis(iconPx.X, viewSizePx.X), placeAxis(iconPx.Y, viewSizePx.Y));
}

//////////////////////////////////////////////////////////////////////////
// Opens the menu over an object's actions (nothing highlighted)
ESOLRadialOutcome SOLRadialMenu::Open(FSOLRadialMenuState& state, const FSOLObjectRef& object,
    const FVector2D& centerPx, const int32 count)
{
    if (count <= 0 || !object.IsValid())
    {
        return ESOLRadialOutcome::None;
    }
    state.bOpen = true;
    state.Object = object;
    state.CenterPx = centerPx;
    state.Count = FMath::Min(count, SOL::Interact::MAX_ACTIONS);
    state.Highlighted = INDEX_NONE;
    return ESOLRadialOutcome::Opened;
}

//////////////////////////////////////////////////////////////////////////
// Updates the highlighted sector from the pointer
void SOLRadialMenu::Move(FSOLRadialMenuState& state, const FVector2D& pointerPx, const double innerPx)
{
    if (state.bOpen)
    {
        state.Highlighted = SectorAt(pointerPx - state.CenterPx, state.Count, innerPx);
    }
}

//////////////////////////////////////////////////////////////////////////
// Releases at the pointer: confirms an enabled sector, else cancels; closes either way
ESOLRadialOutcome SOLRadialMenu::Release(FSOLRadialMenuState& state, const FVector2D& pointerPx,
    const double innerPx, TConstArrayView<bool> enabled, int32& outSector)
{
    outSector = INDEX_NONE;
    if (!state.bOpen)
    {
        return ESOLRadialOutcome::None;
    }
    const int32 sector = SectorAt(pointerPx - state.CenterPx, state.Count, innerPx);
    state = FSOLRadialMenuState();
    if (sector != INDEX_NONE && enabled.IsValidIndex(sector) && enabled[sector])
    {
        outSector = sector;
        return ESOLRadialOutcome::Confirmed;
    }
    return ESOLRadialOutcome::Cancelled;
}

//////////////////////////////////////////////////////////////////////////
// Cancels an open menu
ESOLRadialOutcome SOLRadialMenu::Cancel(FSOLRadialMenuState& state)
{
    if (!state.bOpen)
    {
        return ESOLRadialOutcome::None;
    }
    state = FSOLRadialMenuState();
    return ESOLRadialOutcome::Cancelled;
}

//////////////////////////////////////////////////////////////////////////
// Opens the list: the first enabled row highlighted (row 0 when none is, INDEX_NONE for an empty list)
void SOLListMenu::Open(FSOLListMenuState& state, TConstArrayView<bool> enabled)
{
    state.Count = enabled.Num();
    state.Highlighted = state.Count > 0 ? 0 : INDEX_NONE;
    for (int32 row = 0; row < enabled.Num(); ++row)
    {
        if (enabled[row])
        {
            state.Highlighted = row;
            break;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves the highlight by step rows, wrapping both ways
void SOLListMenu::MoveHighlight(FSOLListMenuState& state, const int32 step)
{
    if (state.Count <= 0 || step == 0)
    {
        return;
    }
    if (state.Highlighted == INDEX_NONE)
    {
        state.Highlighted = step > 0 ? 0 : state.Count - 1;
        return;
    }
    state.Highlighted = ((state.Highlighted + step) % state.Count + state.Count) % state.Count;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the highlighted row is enabled
bool SOLListMenu::CanConfirm(const FSOLListMenuState& state, TConstArrayView<bool> enabled)
{
    return state.Highlighted >= 0 && state.Highlighted < state.Count && enabled.IsValidIndex(state.Highlighted)
        && enabled[state.Highlighted];
}

//////////////////////////////////////////////////////////////////////////
// Returns the top-left corner of a panel opened at the cursor, kept on screen
FVector2D SOLListMenu::PlacePanel(const FVector2D& cursorPx, const FVector2D& panelSizePx,
    const FVector2D& viewSizePx, const double offsetPx, const double marginPx)
{
    return FVector2D(InteractMenusPlaceAxis(cursorPx.X, panelSizePx.X, viewSizePx.X, offsetPx, marginPx),
        InteractMenusPlaceAxis(cursorPx.Y, panelSizePx.Y, viewSizePx.Y, offsetPx, marginPx));
}
