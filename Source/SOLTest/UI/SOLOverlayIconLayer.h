/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "MinorBodies/SOLAsteroidBelt.h"
#include "UI/SOLOverlayIcons.h"

#include "CoreMinimal.h"

class FSOLBodyRegistry;
class USOLAnchorSubsystem;
struct FSOLHudShapeBatch;
struct FSOLNpcCpuShip;

// One frame's inputs of the overlay icon layer: the camera (render space), the screen, the universe viewpoint, which
// icon categories show and the non-body objects (dropped targets and the CPU-side NPC ships, ecliptic metres)
struct FSOLOverlayIconFrame
{
    FVector LocationCm = FVector::ZeroVector;           // Camera, render space
    FQuat Rotation = FQuat::Identity;
    FVector2D CenterPx = FVector2D::ZeroVector;         // Screen centre and size (canvas pixels)
    FVector2D SizePx = FVector2D::ZeroVector;
    double FocalPx = 1.0;
    float UiScale = 1.0f;
    FVector3d ViewpointM = FVector3d::ZeroVector;       // Universe viewpoint (ecliptic), for distances
    double Centuries = 0.0;                             // Sim time (named asteroid positions)
    bool bCategoryShown[static_cast<int32>(SOLOverlayIcons::EIconCategory::Count)] = {};
    bool bDarkening = false;                            // Distance darkening (Shift+O)
    TConstArrayView<bool> TargetActive;                 // Dropped targets: active slots and positions
    TConstArrayView<FVector3d> TargetPositionsM;
    double TargetRadiusM = 0.0;
    TConstArrayView<FSOLNpcCpuShip> Ships;              // NPC ships near the camera (CPU side)
};

/**
 * The flight view's overlay icons (SDD 22 section 2.1): one vector icon per selectable object (the star, planets,
 * moons, dwarf planets, the named asteroids, ring systems around their planet, dropped targets and the nearest NPC
 * ships within SOL::Overlay::MAX_SHIP_ICONS) in the hologram batch. An icon fades out as its object grows on screen
 * and darkens with distance while darkening is on; an object off screen gets an edge arrow instead (the nearest
 * SOL::Overlay::MAX_EDGE_ARROWS). Icons are drawn nearest first and one whose centre would sit on an icon already
 * drawn is skipped. Every buffer is reserved in Initialize (the NPC distance scratch only grows with the population),
 * so a frame allocates nothing.
 */
class SOLTEST_API FSOLOverlayIconLayer
{
public:

    // Caches the registry's categories and ring planets and the named asteroids, and reserves the frame buffers
    void Initialize(const FSOLBodyRegistry& registry);

    // Chooses this frame's NPC ships (nearest first, at most MAX_SHIP_ICONS; also the radar's ship contacts)
    void SelectShips(const FSOLOverlayIconFrame& frame);

    // Returns the NPC ships chosen by the last SelectShips (indices into that frame's ship view)
    TConstArrayView<int32> GetSelectedShips() const { return mSelectedShips; }

    // Adds this frame's icons and edge arrows to the batch (call SelectShips first for ship icons)
    void Draw(FSOLHudShapeBatch& batch, const FSOLOverlayIconFrame& frame, const FSOLBodyRegistry& registry,
        const USOLAnchorSubsystem& anchor);

    // Forgets the last frame's counts (a frame that draws no icons)
    void ClearStats();

    // Returns the icons drawn last frame, all categories or one (verification)
    int32 GetLastIconCount() const { return mLastIconCount; }
    int32 GetLastIconCount(SOLOverlayIcons::EIconCategory category) const;

    // Returns the edge arrows drawn last frame (verification)
    int32 GetLastArrowCount() const { return mLastArrowCount; }

    // Returns whether a registry body has a ring system (its icon gets the ring ellipse)
    bool HasRings(int32 bodyIndex) const { return mHasRings.IsValidIndex(bodyIndex) && mHasRings[bodyIndex]; }

    // Returns a registry body's icon category (Count for an unknown index)
    SOLOverlayIcons::EIconCategory GetBodyCategory(int32 bodyIndex) const;

private:

    // One object that may get an icon this frame
    struct FCandidate
    {
        FVector3d PositionM = FVector3d::ZeroVector;    // Universe, ecliptic
        double RadiusM = 0.0;
        double DistanceM = 0.0;                         // From the viewpoint
        int32 BodyIndex = INDEX_NONE;                   // Registry body (its own render placement), else a point
        SOLOverlayIcons::EIconCategory Category = SOLOverlayIcons::EIconCategory::Count;
    };

    // Adds a candidate when its category shows
    void AddCandidate(const FSOLOverlayIconFrame& frame, const FVector3d& positionM, double radiusM, int32 bodyIndex,
        SOLOverlayIcons::EIconCategory category);

    // Returns true when a point lies within the skip radius of an icon already drawn this frame
    bool IsCrowded(const FVector2D& screenPx, double radiusPx) const;

    TArray<SOLOverlayIcons::EIconCategory> mBodyCategories;     // Per registry body
    TArray<bool> mHasRings;                                     // Per registry body
    TArray<FSOLAsteroidDef> mNamedAsteroids;                    // Real named asteroids (positions from elements)
    TArray<FCandidate> mCandidates;                             // This frame's candidates (reserved once)
    TArray<int32> mOrder;                                       // Candidates nearest first
    TArray<double> mTargetDistancesM;                           // Dropped targets' distances (scratch)
    TArray<int32> mSelectedTargets;                             // Nearest dropped targets
    TArray<double> mShipDistancesM;                             // NPC ships' squared distances (scratch, grows)
    TArray<int32> mSelectedShips;                               // Nearest NPC ships
    TArray<FVector2D> mArrowBasesPx;                            // Off-screen candidates: edge point, direction,
    TArray<FVector2D> mArrowDirections;                         // colour and distance (reserved once)
    TArray<FLinearColor> mArrowColors;
    TArray<double> mArrowDistancesM;
    TArray<double> mArrowBrightness;
    TArray<int32> mArrowOrder;
    TArray<FVector2D> mDrawnPx;                                 // Icon centres drawn this frame (crowding)
    int32 mLastIconCount = 0;
    int32 mLastArrowCount = 0;
    int32 mLastCategoryCounts[static_cast<int32>(SOLOverlayIcons::EIconCategory::Count)] = {};
};
