/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Shape kinds of the HUD shape renderer (SDD 21), packed into each vertex as kind x SOL::Hologram::HUD_SHAPE_KIND_STRIDE
// plus a size parameter
enum class ESOLHudShapeKind : uint8
{
    Line = 0,       // Capsule segment: half length, half width
    Arc = 1,        // Ring sector symmetric about local +Y: radius, half width, half aperture (pi = full ring)
    Panel = 2,      // Rounded box: half width, half height; filled with a glowing outline
};

/**
 * Pure math of the HUD shape renderer (SDD 21 phase A), the CPU mirror of M_SOLHudShape's Custom node
 * (Tools/UI/create_hud_shape_material.py): LT's glow profile, the per-kind signed pixel distances, the kind packing and
 * the row jitter. Distances are signed (negative inside the shape), in pixels, in the shape's local frame.
 */
namespace SOLHudShapeMath
{
    // LT glow of a pixel at distance d (pixels, clamped to >= 0) from a 1 px core:
    // CORE_WEIGHT exp(-CORE_FALLOFF max(0, d - 0.5)) + HALO_WEIGHT exp(-(HALO_SCALE d)^HALO_POWER)
    SOLTEST_API float GlowProfile(float distancePx);

    // Signed distance to a capsule centred on the origin along local X (half length, half width)
    SOLTEST_API float LineDistancePx(const FVector2f& local, float halfLengthPx, float halfWidthPx);

    // Signed distance to an arc of a ring centred on the origin, symmetric about local +Y, spanning +-halfApertureRad
    // from +Y (pi = the full ring), with round caps
    SOLTEST_API float ArcDistancePx(const FVector2f& local, float radiusPx, float halfWidthPx, float halfApertureRad);

    // Signed distance to a rounded box centred on the origin (half size, corner radius)
    SOLTEST_API float PanelDistancePx(const FVector2f& local, const FVector2f& halfSizePx, float cornerRadiusPx);

    // Packs a kind and a size parameter (0 <= param < HUD_SHAPE_KIND_STRIDE) into one float
    SOLTEST_API float EncodeKind(ESOLHudShapeKind kind, float param);

    // Unpacks EncodeKind's float into the kind and the size parameter
    SOLTEST_API void DecodeKind(float encoded, ESOLHudShapeKind& outKind, float& outParam);

    // Smooth 1D value noise in [0, 1]: hash of the integer lattice, smoothstep-interpolated
    SOLTEST_API float SmoothNoise(float x);

    // Row jitter as a fraction of the screen width for a screen row (y01 = row / height) at a time and strength:
    // HUD_JITTER_MAX_FRACTION x strength x exp(-HUD_JITTER_SHARPNESS n) x sign (n = SmoothNoise of the row);
    // 0 for strength <= 0
    SOLTEST_API float JitterOffsetFraction(float y01, float ageS, float strength);
}
