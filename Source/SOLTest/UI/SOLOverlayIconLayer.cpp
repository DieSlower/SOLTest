/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLOverlayIconLayer.h"

#include "MinorBodies/SOLPlanetRing.h"
#include "Npc/SOLNpcTierProcessor.h"
#include "SOLConstants.h"
#include "UI/SOLHudShapeBatch.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistry.h"
#include "Universe/SOLKepler.h"

namespace
{
    // An icon whose centre lies within this many icon radii of one already drawn is skipped (moons on their planet)
    constexpr double OVERLAY_CROWD_RADII = 0.9;

    // Distance of an object that gets no icon (SOLOverlayIcons::SelectBudgeted skips NaN)
    constexpr double OVERLAY_SKIPPED = std::numeric_limits<double>::quiet_NaN();
}

//////////////////////////////////////////////////////////////////////////
// Caches the registry's categories and ring planets and the named asteroids, and reserves the frame buffers
void FSOLOverlayIconLayer::Initialize(const FSOLBodyRegistry& registry)
{
    const int32 bodyCount = registry.Num();
    mBodyCategories.Reset(bodyCount);
    mHasRings.Init(false, bodyCount);
    for (int32 index = 0; index < bodyCount; ++index)
    {
        mBodyCategories.Add(SOLOverlayIcons::ClassifyBody(index, registry.GetParent(index),
            SOL::Overlay::LAST_PLANET_INDEX));
    }
    for (const FSOLPlanetRingDef& ring : SOLPlanetRing::RealRings())
    {
        const int32 planet = registry.FindByName(ring.PlanetName);
        if (mHasRings.IsValidIndex(planet))
        {
            mHasRings[planet] = true;
        }
    }
    BuildNamedAsteroids(mNamedAsteroids);

    // Every per-frame buffer at its bound once
    const int32 maxCandidates = bodyCount + mNamedAsteroids.Num() + SOL::Overlay::MAX_TARGET_ICONS
        + SOL::Overlay::MAX_SHIP_ICONS;
    mCandidates.Reset(maxCandidates);
    mOrder.Reset(maxCandidates);
    mSelectedTargets.Reset(SOL::Overlay::MAX_TARGET_ICONS);
    mSelectedShips.Reset(SOL::Overlay::MAX_SHIP_ICONS);
    mArrowBasesPx.Reset(maxCandidates);
    mArrowDirections.Reset(maxCandidates);
    mArrowColors.Reset(maxCandidates);
    mArrowDistancesM.Reset(maxCandidates);
    mArrowBrightness.Reset(maxCandidates);
    mArrowOrder.Reset(SOL::Overlay::MAX_EDGE_ARROWS);
    mDrawnPx.Reset(SOL::Overlay::MAX_ICONS);
    mHits.Reset(SOL::Overlay::MAX_ICONS);
}

//////////////////////////////////////////////////////////////////////////
// Fills the named asteroids that get icons: the largest ORBIT_LINE_NAMED_MINOR_COUNT with a name (SDD 17), largest
// first (a stable sort, so every caller gets the same order and the same object indices)
void FSOLOverlayIconLayer::BuildNamedAsteroids(TArray<FSOLAsteroidDef>& outAsteroids)
{
    outAsteroids.Reset();
    for (const FSOLAsteroidDef& asteroid : SOLAsteroidBelt::RealAsteroids())
    {
        if (!asteroid.Name.IsNone())
        {
            outAsteroids.Add(asteroid);
        }
    }
    outAsteroids.StableSort([](const FSOLAsteroidDef& a, const FSOLAsteroidDef& b)
    {
        return a.RadiusM > b.RadiusM;
    });
    outAsteroids.SetNum(FMath::Min(outAsteroids.Num(), SOL::ORBIT_LINE_NAMED_MINOR_COUNT));
}

//////////////////////////////////////////////////////////////////////////
// Forgets the last frame's counts (a frame that draws no icons)
void FSOLOverlayIconLayer::ClearStats()
{
    mLastIconCount = 0;
    mLastArrowCount = 0;
    FMemory::Memzero(mLastCategoryCounts);
}

//////////////////////////////////////////////////////////////////////////
// Returns the icons of one category drawn last frame
int32 FSOLOverlayIconLayer::GetLastIconCount(const SOLOverlayIcons::EIconCategory category) const
{
    const int32 index = static_cast<int32>(category);
    return index >= 0 && index < static_cast<int32>(SOLOverlayIcons::EIconCategory::Count)
        ? mLastCategoryCounts[index] : 0;
}

//////////////////////////////////////////////////////////////////////////
// Returns a registry body's icon category
SOLOverlayIcons::EIconCategory FSOLOverlayIconLayer::GetBodyCategory(const int32 bodyIndex) const
{
    return mBodyCategories.IsValidIndex(bodyIndex) ? mBodyCategories[bodyIndex]
        : SOLOverlayIcons::EIconCategory::Count;
}

//////////////////////////////////////////////////////////////////////////
// Chooses this frame's NPC ships, nearest first
void FSOLOverlayIconLayer::SelectShips(const FSOLOverlayIconFrame& frame)
{
    // Hidden (dead) ships are skipped as NaN; squared distances rank the same and need no square root; the scratch
    // only grows with the population
    const int32 shipCount = frame.Ships.Num();
    mShipDistancesM.SetNumUninitialized(shipCount, EAllowShrinking::No);
    for (int32 index = 0; index < shipCount; ++index)
    {
        const FSOLNpcCpuShip& ship = frame.Ships[index];
        mShipDistancesM[index] = ship.bHidden != 0 ? OVERLAY_SKIPPED
            : FVector3d::DistSquared(ship.PositionM, frame.ViewpointM);
    }
    SOLOverlayIcons::SelectBudgeted(mShipDistancesM, INDEX_NONE, SOL::Overlay::MAX_SHIP_ICONS, mSelectedShips);
}

//////////////////////////////////////////////////////////////////////////
// Adds a candidate when its category shows (the candidate buffer is never grown)
void FSOLOverlayIconLayer::AddCandidate(const FSOLOverlayIconFrame& frame, const FVector3d& positionM,
    const double radiusM, const int32 bodyIndex, const SOLOverlayIcons::EIconCategory category,
    const FSOLObjectRef& object)
{
    const int32 categoryIndex = static_cast<int32>(category);
    if (categoryIndex < 0 || categoryIndex >= static_cast<int32>(SOLOverlayIcons::EIconCategory::Count)
        || !frame.bCategoryShown[categoryIndex] || mCandidates.Num() >= mCandidates.Max())
    {
        return;
    }
    FCandidate& candidate = mCandidates.AddDefaulted_GetRef();
    candidate.PositionM = positionM;
    candidate.RadiusM = radiusM;
    candidate.DistanceM = FVector3d::Dist(positionM, frame.ViewpointM);
    candidate.BodyIndex = bodyIndex;
    candidate.Category = category;
    candidate.Object = object;
}

//////////////////////////////////////////////////////////////////////////
// Records one pointer hit (the caller keeps the buffer within its reserved capacity)
void FSOLOverlayIconLayer::AddHit(const FVector2D& centerPx, const double radiusPx, const FSOLObjectRef& object)
{
    FSOLIconHit& hit = mHits.AddDefaulted_GetRef();
    hit.CenterPx = centerPx;
    hit.RadiusPx = radiusPx;
    hit.Object = object;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when a point lies within the skip radius of an icon already drawn this frame
bool FSOLOverlayIconLayer::IsCrowded(const FVector2D& screenPx, const double radiusPx) const
{
    const double limitSq = FMath::Square(OVERLAY_CROWD_RADII * radiusPx);
    for (const FVector2D& drawn : mDrawnPx)
    {
        if (FVector2D::DistSquared(drawn, screenPx) < limitSq)
        {
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Adds this frame's icons and edge arrows to the batch
void FSOLOverlayIconLayer::Draw(FSOLHudShapeBatch& batch, const FSOLOverlayIconFrame& frame,
    const FSOLBodyRegistry& registry, const USOLAnchorSubsystem& anchor)
{
    using SOLOverlayIcons::EIconCategory;
    ClearStats();
    mCandidates.Reset();
    mDrawnPx.Reset();
    mHits.Reset();
    mArrowBasesPx.Reset();
    mArrowDirections.Reset();
    mArrowColors.Reset();
    mArrowDistancesM.Reset();
    mArrowBrightness.Reset();

    // Candidates: every registry body, the named asteroids (Sun-relative elements at the sim time), the nearest
    // dropped targets and the ships chosen by SelectShips
    const int32 bodyCount = FMath::Min(registry.Num(), mBodyCategories.Num());
    for (int32 index = 0; index < bodyCount; ++index)
    {
        AddCandidate(frame, registry.GetPositionM(index), registry.GetRadiusM(index), index, mBodyCategories[index],
            FSOLObjectRef{ ESOLObjectKind::Body, index });
    }
    if (frame.bCategoryShown[static_cast<int32>(EIconCategory::Asteroid)] && bodyCount > 0)
    {
        const FVector3d& sunM = registry.GetPositionM(0);
        const double sunGm = registry.GetGM(0);
        for (int32 index = 0; index < mNamedAsteroids.Num(); ++index)
        {
            const FSOLAsteroidDef& asteroid = mNamedAsteroids[index];
            const FSOLOrbitState state = SOLKepler::ElementsToState(asteroid.Elements.AtCenturies(frame.Centuries),
                sunGm, 0.0);
            AddCandidate(frame, sunM + state.PositionM, asteroid.RadiusM, INDEX_NONE, EIconCategory::Asteroid,
                FSOLObjectRef{ ESOLObjectKind::Asteroid, index });
        }
    }
    if (frame.bCategoryShown[static_cast<int32>(EIconCategory::Target)])
    {
        const int32 slots = FMath::Min(frame.TargetActive.Num(), frame.TargetPositionsM.Num());
        mTargetDistancesM.SetNumUninitialized(slots, EAllowShrinking::No);
        for (int32 slot = 0; slot < slots; ++slot)
        {
            mTargetDistancesM[slot] = frame.TargetActive[slot]
                ? FVector3d::Dist(frame.TargetPositionsM[slot], frame.ViewpointM) : OVERLAY_SKIPPED;
        }
        SOLOverlayIcons::SelectBudgeted(mTargetDistancesM, INDEX_NONE, SOL::Overlay::MAX_TARGET_ICONS,
            mSelectedTargets);
        for (const int32 slot : mSelectedTargets)
        {
            AddCandidate(frame, frame.TargetPositionsM[slot], frame.TargetRadiusM, INDEX_NONE, EIconCategory::Target,
                FSOLObjectRef{ ESOLObjectKind::DroppedTarget, slot });
        }
    }
    if (frame.bCategoryShown[static_cast<int32>(EIconCategory::Ship)])
    {
        for (const int32 ship : mSelectedShips)
        {
            if (frame.Ships.IsValidIndex(ship))
            {
                AddCandidate(frame, frame.Ships[ship].PositionM, 0.0, INDEX_NONE, EIconCategory::Ship,
                    FSOLObjectRef{ ESOLObjectKind::Ship, frame.Ships[ship].NpcIndex });
            }
        }
    }

    // Nearest first, so a nearer icon claims its spot and a crowded farther one is skipped
    mOrder.Reset();
    for (int32 index = 0; index < mCandidates.Num(); ++index)
    {
        mOrder.Add(index);
    }
    mOrder.Sort([this](const int32 a, const int32 b)
    {
        return mCandidates[a].DistanceM < mCandidates[b].DistanceM
            || (mCandidates[a].DistanceM == mCandidates[b].DistanceM && a < b);
    });

    const float radiusPx = SOL::Overlay::ICON_RADIUS_PX * frame.UiScale;
    const float padPx = SOL::Hologram::HUD_GLOW_PAD_PX;
    const double marginPx = SOL::Overlay::EDGE_MARGIN_PX * frame.UiScale;
    const bool bRingsShown = frame.bCategoryShown[static_cast<int32>(EIconCategory::Ring)];
    for (const int32 index : mOrder)
    {
        const FCandidate& candidate = mCandidates[index];
        const FSOLRenderPlacement placement = candidate.BodyIndex != INDEX_NONE
            ? anchor.ComputeBodyRenderPlacement(candidate.BodyIndex)
            : anchor.ComputeRenderPlacement(candidate.PositionM, candidate.RadiusM);
        const FVector local = frame.Rotation.UnrotateVector(placement.LocationCm - frame.LocationCm);
        const SOLOverlayIcons::FEdgePin pin = SOLOverlayIcons::PinToScreen(local, frame.CenterPx, frame.FocalPx,
            frame.SizePx, marginPx);
        const FLinearColor color = SOLOverlayIcons::CategoryColor(candidate.Category);
        const double darkening = SOLOverlayIcons::DistanceDarkening(candidate.DistanceM, frame.bDarkening);

        // Off screen: remembered for the edge arrows (the nearest get one after the loop)
        // Off screen: remembered for the edge arrows (the nearest get one after the loop); a body whose disc is large
        // (it fills the view, e.g. low over a planet) fades out of the arrows as its icon would
        if (!pin.bOnScreen)
        {
            const double rangeCm = local.Size();
            const double tangentCm = FMath::Sqrt(FMath::Max(rangeCm * rangeCm - FMath::Square(placement.RadiusCm), 0.0));
            const double arrowApparentPx = tangentCm > 0.0 ? frame.FocalPx * placement.RadiusCm / tangentCm
                : TNumericLimits<double>::Max();
            mArrowBasesPx.Add(pin.PositionPx);
            mArrowDirections.Add(pin.Direction);
            mArrowColors.Add(color);
            mArrowDistancesM.Add(candidate.DistanceM);
            mArrowBrightness.Add(darkening * SOLOverlayIcons::SizeFade(arrowApparentPx));
            continue;
        }
        const double apparentPx = local.X > 0.0 ? frame.FocalPx * placement.RadiusCm / local.X : 0.0;
        const bool bHighlighted = frame.Highlighted.IsValid() && candidate.Object == frame.Highlighted;
        const double brightness = bHighlighted ? 1.0 : SOLOverlayIcons::SizeFade(apparentPx) * darkening;
        if (brightness < SOL::Overlay::ICON_MIN_BRIGHTNESS || mLastIconCount >= SOL::Overlay::MAX_ICONS
            || IsCrowded(pin.PositionPx, radiusPx))
        {
            // A faded icon (its object is large on screen) stays a pointer hit at its centre, so the interaction
            // menus still reach the object; hovering it draws the icon (SDD 22 Amendment 2). Not the selected
            // target: that is what the guns aim at, and Fire must fire
            if (brightness < SOL::Overlay::ICON_MIN_BRIGHTNESS && mHits.Num() < mHits.Max()
                && candidate.Object != frame.Selected)
            {
                AddHit(pin.PositionPx, radiusPx, candidate.Object);
            }
            continue;
        }
        const FVector2f center(pin.PositionPx);
        const float intensity = SOL::Overlay::ICON_INTENSITY * static_cast<float>(brightness);
        const FLinearColor iconColor = bHighlighted ? SOL::HudPalette::SECONDARY : color;
        if (SOLOverlayIcons::AddIconShapes(batch, candidate.Category, center, radiusPx, iconColor, intensity, padPx)
            == 0)
        {
            continue;
        }

        // The hovered (or menu) object gets a highlight ring; every drawn icon is a pointer hit (SDD 22 section 2.4)
        if (bHighlighted)
        {
            batch.AddRing(center, radiusPx * SOL::Interact::HOVER_RING_SCALE, 1.0f, SOL::HudPalette::SECONDARY,
                SOL::Interact::RADIAL_HIGHLIGHT_INTENSITY, padPx);
        }
        if (mHits.Num() < mHits.Max())
        {
            AddHit(pin.PositionPx, radiusPx, candidate.Object);
        }
        // The ring system's ellipse goes around its planet's icon (so it needs the Planets row too)
        if (bRingsShown && HasRings(candidate.BodyIndex) && SOLOverlayIcons::AddIconShapes(batch, EIconCategory::Ring,
            center, radiusPx, SOLOverlayIcons::CategoryColor(EIconCategory::Ring), intensity, padPx) > 0)
        {
            ++mLastCategoryCounts[static_cast<int32>(EIconCategory::Ring)];
        }
        mDrawnPx.Add(pin.PositionPx);
        ++mLastCategoryCounts[static_cast<int32>(candidate.Category)];
        ++mLastIconCount;
    }

    // Edge arrows for the nearest off-screen objects, dimmer than the icons
    SOLOverlayIcons::SelectBudgeted(mArrowDistancesM, INDEX_NONE, SOL::Overlay::MAX_EDGE_ARROWS, mArrowOrder);
    const float arrowPx = SOL::Overlay::EDGE_ARROW_PX * frame.UiScale;
    for (const int32 index : mArrowOrder)
    {
        const float intensity = SOL::Overlay::EDGE_ARROW_INTENSITY * static_cast<float>(mArrowBrightness[index]);
        if (intensity < SOL::Overlay::ICON_MIN_BRIGHTNESS)
        {
            continue;
        }
        mLastArrowCount += SOLOverlayIcons::AddEdgeArrow(batch, FVector2f(mArrowBasesPx[index]),
            FVector2f(mArrowDirections[index]), arrowPx, mArrowColors[index], intensity, padPx) > 0 ? 1 : 0;
    }
}
