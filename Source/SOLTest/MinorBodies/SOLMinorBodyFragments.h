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
 * Immutable rendering data of one minor body, set once at spawn (SDD 6 Appendix B). Read only by
 * ASOLAsteroidBeltVisuals's hot loop; orbital-dynamics fields live in FSOLMinorBodyOrbitFragment instead.
 */
USTRUCT()
struct FSOLMinorBodyRenderFragment : public FMassFragment
{
    GENERATED_BODY()

    double RadiusM = 0.0;                     // Mean radius, for the render scale
    int32 InstanceIndex = INDEX_NONE;         // This entity's slot in its appearance variant's ISM component
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
