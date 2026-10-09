/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Universe/SOLKepler.h"

#include "CoreMinimal.h"
#include "Mass/EntityElementTypes.h"

#include "SOLMinorBodyFragments.generated.h"

/**
 * Immutable orbital data of one minor body, set once at spawn (SDD 6 Appendix B). Read only by
 * USOLMinorBodyOrbitProcessor's hot loop; rendering fields live in FSOLMinorBodyRenderFragment instead, so neither
 * consumer drags the other's unused bytes through cache every entity, every frame.
 */
USTRUCT()
struct FSOLMinorBodyOrbitFragment : public FMassFragment
{
    GENERATED_BODY()

    FSOLSecularElements Elements;             // Parent-relative orbital elements (Sun-relative for belt asteroids)
    int32 ParentBodyIndex = INDEX_NONE;       // Registry index of the parent (0, the Sun, for belt asteroids)
};

/**
 * Rendering data of one minor body, set once at spawn (SDD 6 Appendix B) except bActive. Read by
 * ASOLAsteroidBeltVisuals's/ASOLRingVisuals's hot loops; orbital-dynamics fields live in FSOLMinorBodyOrbitFragment
 * instead.
 */
USTRUCT()
struct FSOLMinorBodyRenderFragment : public FMassFragment
{
    GENERATED_BODY()

    double RadiusM = 0.0;                     // Mean radius, for the render scale
    int32 InstanceIndex = INDEX_NONE;         // This entity's slot in its appearance variant's ISM component

    // Whether this entity should currently be rendered. Defaults true (correct for belt asteroids, which never
    // write this field after spawn). USOLRingSubsystem::SpawnRingPools explicitly sets this false for every
    // ring-pool entity at spawn instead (never assigned a cell yet), and AssignGroup sets it false again whenever
    // a group has no assigned cell, an all-gap cell, or (for a gap-reduced cell) more entities than the cell has
    // real rocks for, setting it back true only once real rock data is actually written - so ASOLRingVisuals
    // skips/zero-scales a stale or duplicate instance rather than rendering it (SDD 6 Amendment 10's "no inactive
    // marker" gap, plus the review follow-up that caught the gap-reduced-cell duplicate and the spawn-time default
    // both still rendering). A plain fragment bool rather than a Mass tag deliberately: a ring group can be
    // reassigned up to ~90 times/second per active ring (see CLAUDE.md's ring-rock tech debt notes), and an
    // archetype move at that rate would cost far more than flipping one field the renderer already reads every
    // frame regardless.
    bool bActive = true;

    // Bumped (wrapping) by USOLRingSubsystem::AssignGroup every time it gives this entity a new rock (another ring
    // cell), so a reader comparing it with its last-seen value can tell a reassignment teleport from real motion
    // (USOLCombatSubsystem's sweep does). Belt asteroids keep 0. Sits after bActive, so the fragment stays 16 bytes
    uint16 Generation = 0;
};

/**
 * Position of one minor body this frame, in raw ecliptic universe meters (the frame FSOLBodyRegistry::GetPositionsM
 * and the render placement use, NOT the Unreal-handed ship-physics frame of FSOLBodyFrameCache). Written by
 * USOLMinorBodyOrbitProcessor, read by ASOLAsteroidBeltVisuals.
 */
USTRUCT()
struct FSOLMinorBodyStateFragment : public FMassFragment
{
    GENERATED_BODY()

    FVector3d PositionM = FVector3d::ZeroVector; // Sun-frame position, ecliptic axes, meters
};

/**
 * Which rendering variant (ISM component) a minor body belongs to: a const shared fragment, one copy per distinct
 * value. MeshVariantIndex is reflected, so Mass de-duplicates the shared values by it directly (an int needs no CRC
 * key, unlike FSOLShipParamsFragment's plain-C++ payload). Build it with Make for symmetry with the ship params.
 */
USTRUCT()
struct FSOLMinorBodyAppearanceFragment : public FMassConstSharedFragment
{
    GENERATED_BODY()

    // Returns a fragment for the given rendering variant
    static FSOLMinorBodyAppearanceFragment Make(const int32 meshVariantIndex)
    {
        FSOLMinorBodyAppearanceFragment fragment;
        fragment.MeshVariantIndex = meshVariantIndex;
        return fragment;
    }

    /** Index of the ISM component (rendering variant) this body is drawn by. */
    UPROPERTY()
    int32 MeshVariantIndex = 0;
};

// Rendering variants of the minor bodies (indices of FSOLMinorBodyAppearanceFragment::MeshVariantIndex)
namespace SOLMinorBodyVariant
{
    inline constexpr int32 REAL_ASTEROID = 0;     // A named real asteroid (FSOLAsteroidDef::Name != NAME_None)
    inline constexpr int32 FAMILY_FILL = 1;       // A procedural family-cluster member (Name == NAME_None)
    inline constexpr int32 COUNT = 2;
}

/**
 * Marks an asteroid-belt entity (USOLMinorBodySubsystem::SpawnAsteroidBelt), so ASOLAsteroidBeltVisuals's query can
 * match only belt entities at the archetype level (SDD 6 Amendment 11's follow-up, 5e-iii) instead of relying solely
 * on the implicit "has a const-shared FSOLMinorBodyAppearanceFragment" distinction, which works but doesn't name the
 * actual intent.
 */
USTRUCT()
struct FSOLBeltRockTag : public FMassTag
{
    GENERATED_BODY()
};

/**
 * Marks a ring-pool entity (USOLRingSubsystem::SpawnRingPools), one tag shared by all four rings: ASOLRingVisuals's
 * query matches this tag at the archetype level to exclude belt entities entirely, then still filters per-entity by
 * FSOLMinorBodyOrbitFragment::ParentBodyIndex to separate its own ring's entities from the other three rings' (SDD
 * 6 Amendment 11's follow-up, 5e-iii).
 */
USTRUCT()
struct FSOLRingRockTag : public FMassTag
{
    GENERATED_BODY()
};
