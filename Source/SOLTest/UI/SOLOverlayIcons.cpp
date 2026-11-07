/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOverlayIcons.h"

#include "SOLConstants.h"
#include "UI/SOLHudShapeBatch.h"

namespace
{
    // Icon artwork, in icon radii (SDD 22 section 3: vector shapes, no textures)
    constexpr float ICON_STAR_RAY = 1.6f;           // Star: a ring crossed by two rays reaching this far
    constexpr float ICON_MOON_RADIUS = 0.65f;       // Moon: a smaller ring
    constexpr float ICON_DWARF_RADIUS = 0.8f;       // Dwarf planet: a diamond
    constexpr float ICON_ASTEROID_RADIUS = 0.75f;   // Asteroid: a triangle
    constexpr float ICON_SHIP_RADIUS = 0.9f;        // Ship: an open chevron pointing up
    constexpr float ICON_TARGET_HALF = 0.7f;        // Target: a square
    constexpr float ICON_RING_FLATTEN = 0.35f;      // Ring system: ellipse height / width
    constexpr float EDGE_ARROW_SPREAD = 0.6f;       // Edge arrow: half width of the chevron / its length

    // Cursor arrow outline (fractions of its length): tip, bottom of the left edge, the notch and the right corner
    constexpr float CURSOR_NOTCH_X = 0.3f;
    constexpr float CURSOR_CORNER = 0.72f;

    //////////////////////////////////////////////////////////////////////////
    // Adds a closed polygon of glow lines; returns the lines added
    int32 IconAddPolygon(FSOLHudShapeBatch& batch, const FVector2f* points, const int32 count,
        const FLinearColor& color, const float intensity, const float padPx)
    {
        int32 added = 0;
        for (int32 index = 0; index < count; ++index)
        {
            added += batch.AddLine(points[index], points[(index + 1) % count], SOL::Hologram::HUD_LINE_HALF_WIDTH_PX,
                color, intensity, padPx) ? 1 : 0;
        }
        return added;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the Overlay Menu row that toggles a category's icons
SOLOverlay::EOverlay SOLOverlayIcons::CategoryOverlay(const EIconCategory category)
{
    using SOLOverlay::EOverlay;
    switch (category)
    {
    case EIconCategory::Star:
        return EOverlay::IconStars;
    case EIconCategory::Planet:
        return EOverlay::IconPlanets;
    case EIconCategory::Moon:
        return EOverlay::IconMoons;
    case EIconCategory::Dwarf:
        return EOverlay::IconDwarfs;
    case EIconCategory::Asteroid:
        return EOverlay::IconAsteroids;
    case EIconCategory::Ring:
        return EOverlay::IconRings;
    case EIconCategory::Ship:
        return EOverlay::IconShips;
    case EIconCategory::Target:
        return EOverlay::IconTargets;
    default:
        return EOverlay::Count;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a registry body's category from its parent (the Sun is body 0)
SOLOverlayIcons::EIconCategory SOLOverlayIcons::ClassifyBody(const int32 bodyIndex, const int32 parentIndex,
    const int32 lastPlanetIndex)
{
    if (parentIndex == INDEX_NONE)
    {
        return EIconCategory::Star;
    }
    if (parentIndex == 0)
    {
        return bodyIndex <= lastPlanetIndex ? EIconCategory::Planet : EIconCategory::Dwarf;
    }
    return EIconCategory::Moon;
}

//////////////////////////////////////////////////////////////////////////
// Returns the icon's fade from the object's apparent radius (1 small, 0 large, smoothstep between)
double SOLOverlayIcons::SizeFade(const double apparentRadiusPx)
{
    if (FMath::IsNaN(apparentRadiusPx) || apparentRadiusPx <= SOL::Overlay::ICON_FADE_START_PX)
    {
        return 1.0;
    }
    if (apparentRadiusPx >= SOL::Overlay::ICON_FADE_END_PX)
    {
        return 0.0;
    }
    const double t = (apparentRadiusPx - SOL::Overlay::ICON_FADE_START_PX)
        / (SOL::Overlay::ICON_FADE_END_PX - SOL::Overlay::ICON_FADE_START_PX);
    return 1.0 - t * t * (3.0 - 2.0 * t);
}

//////////////////////////////////////////////////////////////////////////
// Returns the distance darkening factor (1 near or off, the floor far, linear in log10 between)
double SOLOverlayIcons::DistanceDarkening(const double distanceM, const bool bDarkening)
{
    if (!bDarkening || FMath::IsNaN(distanceM) || distanceM <= SOL::Overlay::DARKEN_NEAR_M)
    {
        return 1.0;
    }
    if (distanceM >= SOL::Overlay::DARKEN_FAR_M)
    {
        return SOL::Overlay::DARKEN_FLOOR;
    }
    const double nearLog = FMath::LogX(10.0, SOL::Overlay::DARKEN_NEAR_M);
    const double t = (FMath::LogX(10.0, distanceM) - nearLog) / (FMath::LogX(10.0, SOL::Overlay::DARKEN_FAR_M)
        - nearLog);
    return 1.0 + (SOL::Overlay::DARKEN_FLOOR - 1.0) * t;
}

//////////////////////////////////////////////////////////////////////////
// Returns the size fade times the distance darkening
double SOLOverlayIcons::IconBrightness(const double apparentRadiusPx, const double distanceM, const bool bDarkening)
{
    return SizeFade(apparentRadiusPx) * DistanceDarkening(distanceM, bDarkening);
}

//////////////////////////////////////////////////////////////////////////
// Places a camera-local point on the screen, or pins it to the screen edge with the direction toward it
SOLOverlayIcons::FEdgePin SOLOverlayIcons::PinToScreen(const FVector& cameraLocal, const FVector2D& centerPx,
    const double focalPx, const FVector2D& viewSizePx, const double marginPx)
{
    FEdgePin pin;
    if (cameraLocal.X > 0.0)
    {
        const FVector2D projected(centerPx.X + focalPx * (cameraLocal.Y / cameraLocal.X),
            centerPx.Y - focalPx * (cameraLocal.Z / cameraLocal.X));
        if (projected.X >= marginPx && projected.Y >= marginPx && projected.X <= viewSizePx.X - marginPx
            && projected.Y <= viewSizePx.Y - marginPx)
        {
            pin.PositionPx = projected;
            pin.bOnScreen = true;
            return pin;
        }
    }

    // Off screen or behind: along the screen direction of the camera-local right / up offset, to the shrunk edge
    FVector2D direction(cameraLocal.Y, -cameraLocal.Z);
    if (!direction.Normalize())
    {
        direction = FVector2D(0.0, 1.0);
    }
    const double halfWidth = centerPx.X - marginPx;
    const double halfHeight = centerPx.Y - marginPx;
    const double reach = FMath::Min(FMath::Abs(direction.X) > UE_KINDA_SMALL_NUMBER
        ? halfWidth / FMath::Abs(direction.X) : TNumericLimits<double>::Max(),
        FMath::Abs(direction.Y) > UE_KINDA_SMALL_NUMBER ? halfHeight / FMath::Abs(direction.Y)
        : TNumericLimits<double>::Max());
    pin.Direction = direction;
    pin.PositionPx = centerPx + direction * reach;
    return pin;
}

//////////////////////////////////////////////////////////////////////////
// Fills outIndices with at most cap indices: the selected one first, then the nearest (ties: lower index first)
void SOLOverlayIcons::SelectBudgeted(TConstArrayView<double> distancesM, const int32 selectedIndex, const int32 cap,
    TArray<int32>& outIndices)
{
    outIndices.Reset();
    if (cap <= 0)
    {
        return;
    }
    const bool bHasSelected = distancesM.IsValidIndex(selectedIndex) && FMath::IsFinite(distancesM[selectedIndex]);
    if (bHasSelected)
    {
        outIndices.Add(selectedIndex);
    }
    const int32 first = outIndices.Num();

    // Returns whether a should come before b (nearer, or as near with a lower index)
    const auto isBefore = [&distancesM](const int32 a, const int32 b)
    {
        return distancesM[a] < distancesM[b] || (distancesM[a] == distancesM[b] && a < b);
    };

    // A bounded insertion: the kept entries stay sorted, a candidate no better than the worst kept one is skipped
    for (int32 index = 0; index < distancesM.Num(); ++index)
    {
        if (index == selectedIndex || !FMath::IsFinite(distancesM[index]))
        {
            continue;
        }
        if (outIndices.Num() >= cap)
        {
            if (outIndices.Num() == first || !isBefore(index, outIndices.Last()))
            {
                continue;
            }
            outIndices.Last() = index;
        }
        else
        {
            outIndices.Add(index);
        }
        for (int32 slot = outIndices.Num() - 1; slot > first && isBefore(outIndices[slot], outIndices[slot - 1]);
            --slot)
        {
            Swap(outIndices[slot], outIndices[slot - 1]);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a category's icon colour in the hologram palette
FLinearColor SOLOverlayIcons::CategoryColor(const EIconCategory category)
{
    switch (category)
    {
    case EIconCategory::Star:
        return SOL::HudPalette::FREE_LOOK;
    case EIconCategory::Ship:
        return SOL::HudPalette::GOOD;
    case EIconCategory::Target:
        return SOL::HudPalette::SECONDARY;
    default:
        return SOL::HudPalette::PRIMARY;
    }
}

//////////////////////////////////////////////////////////////////////////
// Adds a category's icon (at most MAX_SHAPES_PER_ICON vector shapes) to the batch; returns the shapes added
int32 SOLOverlayIcons::AddIconShapes(FSOLHudShapeBatch& batch, const EIconCategory category,
    const FVector2f& centerPx, const float radiusPx, const FLinearColor& color, const float intensity,
    const float padPx)
{
    if (radiusPx <= 0.0f)
    {
        return 0;
    }
    const float halfWidth = SOL::Hologram::HUD_LINE_HALF_WIDTH_PX;
    const FVector2f& c = centerPx;
    const float r = radiusPx;
    switch (category)
    {
    case EIconCategory::Star:
    {
        // A ring crossed by a horizontal and a vertical ray
        const float ray = ICON_STAR_RAY * r;
        int32 added = batch.AddRing(c, r, halfWidth, color, intensity, padPx) ? 1 : 0;
        added += batch.AddLine(c - FVector2f(ray, 0.0f), c + FVector2f(ray, 0.0f), halfWidth, color, intensity, padPx)
            ? 1 : 0;
        added += batch.AddLine(c - FVector2f(0.0f, ray), c + FVector2f(0.0f, ray), halfWidth, color, intensity, padPx)
            ? 1 : 0;
        return added;
    }
    case EIconCategory::Planet:
        return batch.AddRing(c, r, halfWidth, color, intensity, padPx) ? 1 : 0;
    case EIconCategory::Moon:
        return batch.AddRing(c, ICON_MOON_RADIUS * r, halfWidth, color, intensity, padPx) ? 1 : 0;
    case EIconCategory::Dwarf:
    {
        const float d = ICON_DWARF_RADIUS * r;
        const FVector2f diamond[] = { c + FVector2f(0.0f, -d), c + FVector2f(d, 0.0f), c + FVector2f(0.0f, d),
            c + FVector2f(-d, 0.0f) };
        return IconAddPolygon(batch, diamond, UE_ARRAY_COUNT(diamond), color, intensity, padPx);
    }
    case EIconCategory::Asteroid:
    {
        const float a = ICON_ASTEROID_RADIUS * r;
        const FVector2f triangle[] = { c + FVector2f(0.0f, -a), c + FVector2f(0.866f * a, 0.5f * a),
            c + FVector2f(-0.866f * a, 0.5f * a) };
        return IconAddPolygon(batch, triangle, UE_ARRAY_COUNT(triangle), color, intensity, padPx);
    }
    case EIconCategory::Ring:
    {
        const float width = SOL::Overlay::RING_ICON_SCALE * r;
        return batch.AddEllipse(c, width, ICON_RING_FLATTEN * width, halfWidth, color, intensity, padPx) ? 1 : 0;
    }
    case EIconCategory::Ship:
    {
        // An open chevron pointing up (two lines from the apex)
        const float s = ICON_SHIP_RADIUS * r;
        const FVector2f apex = c + FVector2f(0.0f, -s);
        int32 added = batch.AddLine(apex, c + FVector2f(-s, s), halfWidth, color, intensity, padPx) ? 1 : 0;
        added += batch.AddLine(apex, c + FVector2f(s, s), halfWidth, color, intensity, padPx) ? 1 : 0;
        return added;
    }
    case EIconCategory::Target:
    {
        const float h = ICON_TARGET_HALF * r;
        const FVector2f square[] = { c + FVector2f(-h, -h), c + FVector2f(h, -h), c + FVector2f(h, h),
            c + FVector2f(-h, h) };
        return IconAddPolygon(batch, square, UE_ARRAY_COUNT(square), color, intensity, padPx);
    }
    default:
        return 0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Adds an edge arrow: a chevron whose tip is lengthPx beyond the base along the direction; returns the shapes added
int32 SOLOverlayIcons::AddEdgeArrow(FSOLHudShapeBatch& batch, const FVector2f& basePx, const FVector2f& direction,
    const float lengthPx, const FLinearColor& color, const float intensity, const float padPx)
{
    if (lengthPx <= 0.0f || direction.IsNearlyZero())
    {
        return 0;
    }
    const FVector2f unit = direction.GetSafeNormal();
    const FVector2f perpendicular(-unit.Y, unit.X);
    const FVector2f tip = basePx + unit * lengthPx;
    const FVector2f spread = perpendicular * (EDGE_ARROW_SPREAD * lengthPx);
    const float halfWidth = SOL::Hologram::HUD_LINE_HALF_WIDTH_PX;
    int32 added = batch.AddLine(tip, basePx + spread, halfWidth, color, intensity, padPx) ? 1 : 0;
    added += batch.AddLine(tip, basePx - spread, halfWidth, color, intensity, padPx) ? 1 : 0;
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Returns the box a category's icon shapes span (the same factors AddIconShapes draws with)
FBox2f SOLOverlayIcons::IconExtent(const EIconCategory category, const FVector2f& centerPx, const float radiusPx)
{
    if (!(radiusPx > 0.0f))
    {
        return FBox2f(ForceInit);
    }
    const float r = radiusPx;
    FVector2f lower;
    FVector2f upper;
    switch (category)
    {
    case EIconCategory::Star:
        upper = FVector2f(FMath::Max(1.0f, ICON_STAR_RAY) * r);
        lower = -upper;
        break;
    case EIconCategory::Planet:
        upper = FVector2f(r);
        lower = -upper;
        break;
    case EIconCategory::Moon:
        upper = FVector2f(ICON_MOON_RADIUS * r);
        lower = -upper;
        break;
    case EIconCategory::Dwarf:
        upper = FVector2f(ICON_DWARF_RADIUS * r);
        lower = -upper;
        break;
    case EIconCategory::Asteroid:
    {
        const float a = ICON_ASTEROID_RADIUS * r;
        lower = FVector2f(-0.866f * a, -a);
        upper = FVector2f(0.866f * a, 0.5f * a);
        break;
    }
    case EIconCategory::Ring:
    {
        const float width = SOL::Overlay::RING_ICON_SCALE * r;
        upper = FVector2f(width, ICON_RING_FLATTEN * width);
        lower = -upper;
        break;
    }
    case EIconCategory::Ship:
        upper = FVector2f(ICON_SHIP_RADIUS * r);
        lower = -upper;
        break;
    case EIconCategory::Target:
        upper = FVector2f(ICON_TARGET_HALF * r);
        lower = -upper;
        break;
    default:
        return FBox2f(ForceInit);
    }
    return FBox2f(centerPx + lower, centerPx + upper);
}

//////////////////////////////////////////////////////////////////////////
// Returns the box an edge arrow's chevron spans: its tip and the two ends of its spread at the base
FBox2f SOLOverlayIcons::EdgeArrowExtent(const FVector2f& basePx, const FVector2f& direction, const float lengthPx)
{
    if (!(lengthPx > 0.0f) || direction.IsNearlyZero())
    {
        return FBox2f(ForceInit);
    }
    const FVector2f unit = direction.GetSafeNormal();
    const FVector2f spread = FVector2f(-unit.Y, unit.X) * (EDGE_ARROW_SPREAD * lengthPx);
    const FVector2f tip = basePx + unit * lengthPx;
    FBox2f box(tip, tip);
    box += basePx + spread;
    box += basePx - spread;
    return box;
}

//////////////////////////////////////////////////////////////////////////
// Fills the cursor arrow's outline segments relative to the hot spot (the tip at the origin)
void SOLHologramCursor::ArrowSegments(const float scale, FVector2f (&outStart)[SOL::Overlay::CURSOR_SEGMENTS],
    FVector2f (&outEnd)[SOL::Overlay::CURSOR_SEGMENTS])
{
    static_assert(SOL::Overlay::CURSOR_SEGMENTS == 4, "the arrow outline has four segments");
    const float length = SOL::Overlay::CURSOR_LENGTH_PX * (scale > 0.0f ? scale : 1.0f);
    const FVector2f points[] =
    {
        FVector2f(0.0f, 0.0f),
        FVector2f(0.0f, length),
        FVector2f(CURSOR_NOTCH_X * length, CURSOR_CORNER * length),
        FVector2f(CURSOR_CORNER * length, CURSOR_CORNER * length),
    };
    for (int32 segment = 0; segment < SOL::Overlay::CURSOR_SEGMENTS; ++segment)
    {
        outStart[segment] = points[segment];
        outEnd[segment] = points[(segment + 1) % SOL::Overlay::CURSOR_SEGMENTS];
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the side of the square cursor widget whose centre is the hot spot
float SOLHologramCursor::WidgetSidePx(const float scale, const float padPx)
{
    return 2.0f * (SOL::Overlay::CURSOR_LENGTH_PX * (scale > 0.0f ? scale : 1.0f) + padPx);
}
