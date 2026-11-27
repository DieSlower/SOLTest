/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "Templates/TypeCompatibleBytes.h"

// The C++ float copy of the NPC point shader's sub-pixel rule, Shaders/Private/SOLNpcPoint.ush (SDD 54 sections
// 4.2-4.4, 4.8), statement for statement, so the shader's float path can be compared with the double SOLNpcPixel
// functions on the CPU (SOLTest.NpcPixel.*, Tests/NpcPixelTest.cpp; SOLTest.NpcPointDot.*, Tests/NpcPointDotTest.cpp).
// A change to the shader must change this file and the other way round. NaN and infinities are rejected by tests on the
// float's bits (HLSL saturate(NaN) is 0 but FMath::Clamp(NaN, 0, 1) is 1, and fast math, the material compiler's and a
// game build's /fp:fast, may fold float comparisons with NaN or infinity), the same tests in both files.
namespace SOLNpcPointMirror
{
    // The exponent bits of a float (all set: infinity or NaN) and its sign bit (SOLNP_EXPONENT_MASK, SOLNP_SIGN_MASK)
    inline constexpr uint32 EXPONENT_MASK = 0x7F800000u;
    inline constexpr uint32 SIGN_MASK = 0x80000000u;

    // Returns true for a finite value, false for NaN and infinities (SOLNP_IsFinite)
    inline bool IsFinite(const float value)
    {
        return (BitCast<uint32>(value) & EXPONENT_MASK) != EXPONENT_MASK;
    }

    // Returns true for NaN (SOLNP_IsNan)
    inline bool IsNan(const float value)
    {
        return (BitCast<uint32>(value) & ~SIGN_MASK) > EXPONENT_MASK;
    }

    // Returns true for a value greater than 0 and finite (SOLNP_IsPositiveFinite)
    inline bool IsPositiveFinite(const float value)
    {
        return IsFinite(value) && value > 0.0f;
    }

    // Focal length in reference px from the projection's [0][0] (SOLNP_FocalRefPx); 0 for a non-positive or NaN size
    inline float FocalRefPx(const float projectionScaleX, const FVector2f& viewSize, const float referenceHeightPx)
    {
        if (!(viewSize.X > 0.0f) || !(viewSize.Y > 0.0f))
        {
            return 0.0f;
        }
        return 0.5f * viewSize.X * projectionScaleX * referenceHeightPx / viewSize.Y;
    }

    // True size in reference px (SOLNP_ShipSizePx): focal x diameter / max(distance, 1), 0 for any bad input
    inline float ShipSizePx(const float diameterM, const float distanceM, const float focalRefPx)
    {
        if (!IsPositiveFinite(diameterM) || !IsPositiveFinite(focalRefPx) || !IsFinite(distanceM) || distanceM < 0.0f)
        {
            return 0.0f;
        }
        return focalRefPx * diameterM / FMath::Max(distanceM, 1.0f);
    }

    // Dot alpha (SOLNP_DotAlpha): 0 at and below hidePx (and NaN), linear to 1 at fullPx, 1 at and above it
    // (+infinity included); 0 for a non-finite threshold
    inline float DotAlpha(const float sizePx, const float hidePx, const float fullPx)
    {
        if (!IsFinite(hidePx) || !IsFinite(fullPx) || IsNan(sizePx))
        {
            return 0.0f;
        }
        if (!IsFinite(sizePx))
        {
            return (BitCast<uint32>(sizePx) & SIGN_MASK) == 0u ? 1.0f : 0.0f;
        }
        if (!(sizePx > hidePx))
        {
            return 0.0f;
        }
        if (sizePx >= fullPx)
        {
            return 1.0f;
        }
        return (sizePx - hidePx) / (fullPx - hidePx);
    }

    // Drawn dot diameter in device px (SOLNP_DrawnDiameterDevicePx), max(sizePx x viewHeightPx / referenceHeightPx,
    // rasterMinDevicePx); called only for drawn dots; the floor alone for a non-positive, infinite or NaN reference
    // height
    inline float DrawnDiameterDevicePx(const float sizePx, const float viewHeightPx, const float referenceHeightPx,
        const float rasterMinDevicePx)
    {
        if (!IsPositiveFinite(referenceHeightPx))
        {
            return rasterMinDevicePx;
        }
        return FMath::Max(sizePx * viewHeightPx / referenceHeightPx, rasterMinDevicePx);
    }

    // The size a dot is drawn at in reference px (SOLNP_CappedSizePx, SDD 54 Q13): the true size capped at capPx; the
    // true size for a non-positive, infinite or NaN cap
    inline float CappedSizePx(const float sizePx, const float capPx)
    {
        if (!IsPositiveFinite(capPx))
        {
            return sizePx;
        }
        return FMath::Min(sizePx, capPx);
    }

    // One NPC point's dot alpha (0: not drawn) and billboard incircle radius in render cm (SOLNP_PointDot, SDD 54 4.3,
    // 4.8): params2 = NpcParams2 (raster floor device px, D / 2 m, ...), params5 = NpcParams5 (hide px, full px,
    // reference height px, map flag), params6 = NpcParams6 (dot cap px, ...). The map flag keeps today's dot (alpha 1,
    // never under full px device px across); in flight the drawn size is the true size capped at the dot cap (Q13)
    inline float PointDot(const float distanceM, const float scale, const float pixelCm, const float projectionScaleX,
        const FVector2f& viewSize, const FVector4f& params2, const FVector4f& params5, const FVector4f& params6,
        float& outRadiusCm)
    {
        if (params5.W > 0.5f)
        {
            outRadiusCm = FMath::Max(params2.Y * scale, 0.5f * params5.Y * pixelCm);
            return 1.0f;
        }
        const float focalRefPx = FocalRefPx(projectionScaleX, viewSize, params5.Z);
        const float sizePx = ShipSizePx(2.0f * params2.Y, distanceM, focalRefPx);
        const float alpha = DotAlpha(sizePx, params5.X, params5.Y);
        outRadiusCm = 0.0f;
        if (alpha > 0.0f)
        {
            outRadiusCm = 0.5f * DrawnDiameterDevicePx(CappedSizePx(sizePx, params6.X), viewSize.Y, params5.Z,
                params2.X) * pixelCm;
        }
        return alpha;
    }
}
