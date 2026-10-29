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
    Ellipse = 3,    // Ellipse ring, axes along local X and Y: X radius, half width, Y / X radius ratio (SDD 21 C)
    DashedLine = 4, // Dashed capsule segment: half length, half width, dash period in px (SDD 21 C)
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

    // Signed distance to an ellipse ring centred on the origin (radii along local X and Y, half width), first-order
    // approximation |k0 (k0 - 1) / k1| - halfWidth with k0 = |p / r|, k1 = |p / r^2| (exact on the axes); at the centre
    // (k1 = 0) the unsigned distance is min(radiusX, radiusY); radii are clamped to >= 1e-3 px (no division by zero)
    SOLTEST_API float EllipseDistancePx(const FVector2f& local, float radiusXPx, float radiusYPx, float halfWidthPx);

    // Signed distance to a dashed segment along local X from -halfLength to +halfLength: dashes of length
    // HUD_DASH_FRACTION x period start at -halfLength every period (period clamped to >= 1), the last one clipped at
    // +halfLength; length(dx, y) - halfWidth with dx the distance along X to the nearest dash's span (0 within one)
    SOLTEST_API float DashedLineDistancePx(const FVector2f& local, float halfLengthPx, float halfWidthPx,
        float periodPx);

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

    // Fade of a shape's glow across the quad padding: 1 - smoothstep(HUD_PAD_FADE_START x padPx, padPx, distancePx);
    // 1 when padPx <= 0
    SOLTEST_API float PadFade(float distancePx, float padPx);

    // A panel's interior fill at a signed distance (negative inside) and screen row: 0 outside (distancePx >= 0), else
    // fill x (HUD_PANEL_FILL_BASE + (1 - HUD_PANEL_FILL_BASE) exp(distancePx / HUD_PANEL_EDGE_FALLOFF_PX))
    // x (1 + HUD_PANEL_SCANLINE x max(strength, 0) x sin(pi screenY)) (a fresnel-like edge and 1 px scanlines)
    SOLTEST_API float PanelFill(float distancePx, float fill, float screenY, float strength);

    // Panel brightness factor of the flicker: 1 - HUD_PANEL_FLICKER x max(strength, 0)
    // x SmoothNoise(fmod(ageS, HUD_TIME_WRAP_S) x HUD_PANEL_FLICKER_RATE_HZ + HUD_PANEL_FLICKER_OFFSET)
    SOLTEST_API float PanelFlicker(float ageS, float strength);

    // LT tone curve: 1 - exp(-HUD_TONE_GAIN x max(c, 0)^HUD_TONE_POWER)
    SOLTEST_API float ToneCurve(float c);

    // LT blue grade: the quartic Bezier with control points 0, HUD_GRADE_BLUE_1, _2, _3, 1 at t (t clamped to [0, 1])
    SOLTEST_API float GradeBlue(float t);

    // The panel around a content rectangle grown by paddingPx on every side: its centre and half size; a zero centre
    // and half size for an invalid (bIsValid false) rectangle
    SOLTEST_API void PanelAroundRect(const FBox2f& content, float paddingPx, FVector2f& outCenter,
        FVector2f& outHalfSize);

    // The horizontal capsule filling a box (x, y, width, height) whose glow core reaches coreHalfPx past the capsule:
    // half width min(max(height/2 - coreHalfPx, coreHalfPx), width/2), so the capsule never extends past the box's
    // ends; its end centres and half width; false (outputs untouched) for a non-positive width or height
    SOLTEST_API bool BoxCapsule(float x, float y, float width, float height, float coreHalfPx, FVector2f& outStart,
        FVector2f& outEnd, float& outHalfWidth);
}
