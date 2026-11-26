/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Npc/SOLNpcTunnelMath.h"

#include "CoreMinimal.h"

#include <cmath>

// The C++ float copy of the tunnel shader's slot math, Shaders/Private/SOLNpcTunnel.ush (SDD 52 sections 4.2-4.5),
// statement for statement and in the same order, so the shader's float path can be compared with the double
// SOLNpcTunnel functions on the CPU (SOLTest.Npc.TunnelMirror.*, Tests/NpcTunnelMirrorTest.cpp). It reads the same
// packed vectors the material receives (SOLNpcTunnel::TunnelMaterialVectors). A change to the shader must change this
// file.
namespace SOLNpcTunnelMirror
{
    // A value standing for "infinitely many pixels" (SOLNT_HUGE_PX)
    inline constexpr float HUGE_PX = 1.0e30f;

    // Element kinds (SOLNT_ELEMENT_*)
    enum class EElement : uint8
    {
        Ring,
        Dot,
        Chevron
    };

    // Returns the descending smoothstep, 1 at and below low, 0 at and above high (SOLNT_SmoothFall)
    inline float SmoothFall(const float value, const float low, const float high)
    {
        const float t = FMath::Clamp((value - low) / (high - low), 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }

    // Returns the log-linear ramp, 0 at and below gone (and NaN), 1 from full (SOLNT_LogRamp)
    inline float LogRamp(const float value, const float gone, const float full)
    {
        if (!(value > gone))
        {
            return 0.0f;
        }
        if (value >= full)
        {
            return 1.0f;
        }
        return FMath::Clamp(std::log(value / gone) / std::log(full / gone), 0.0f, 1.0f);
    }

    // Returns the focal length in reference px from the projection's [0][0] = 1 / tan(fov / 2) (SOLNT_FocalRefPx)
    inline float FocalRefPx(const float projectionScaleX, const FVector2f& viewSize, const float referenceHeightPx)
    {
        if (!(viewSize.X > 0.0f) || !(viewSize.Y > 0.0f))
        {
            return 0.0f;
        }
        return 0.5f * viewSize.X * projectionScaleX * referenceHeightPx / viewSize.Y;
    }

    // Returns the focal the pixel fades read, the focal over the QualityPreset scale, a scale below 1 read as 1
    // (SOLNT_FadeFocalRefPx; the scale is Layout4.z, part 53d)
    inline float FadeFocalRefPx(const float focalRefPx, const float qualityScale)
    {
        return focalRefPx / FMath::Max(qualityScale, 1.0f);
    }

    // Returns a world size of sizeRadii x Rt on screen in reference px, huge for d <= 0 (SOLNT_SizePx)
    inline float SizePx(const float d, const float rt, const float focalRefPx, const float sizeRadii)
    {
        if (!(d > 0.0f))
        {
            return HUGE_PX;
        }
        return focalRefPx * sizeRadii * rt / d;
    }

    // Returns the pixel-size fade (SOLNT_RadiusFade)
    inline float RadiusFade(const float radiusPx, const FVector4f& shape, const FVector4f& layout1)
    {
        const float solidPx = layout1.Y;
        const float kneePx = layout1.Z;
        const float kneeAlpha = layout1.W;
        const float gonePx = shape.W;
        if (radiusPx >= solidPx)
        {
            return 1.0f;
        }
        if (radiusPx >= kneePx)
        {
            return kneeAlpha + (1.0f - kneeAlpha) * std::log(radiusPx / kneePx) / std::log(solidPx / kneePx);
        }
        if (radiusPx > gonePx)
        {
            return kneeAlpha * std::log(radiusPx / gonePx) / std::log(kneePx / gonePx);
        }
        return 0.0f;
    }

    // Returns the on-screen ring gap in reference px (SOLNT_RingGapPx)
    inline float RingGapPx(const float x, const float h, const float rt, const float focalRefPx, const float s0)
    {
        const float ax = FMath::Abs(x);
        const float ah = FMath::Abs(h);
        const float d = std::sqrt(ax * ax + ah * ah);
        if (!(d > 0.0f))
        {
            return HUGE_PX;
        }
        const float nextX = ax + s0;
        const float nextD = std::sqrt(nextX * nextX + ah * ah);
        const float rays = std::atan2(nextX, ah) - std::atan2(ax, ah);
        const float radii = rt / d - rt / nextD;
        return focalRefPx * FMath::Max(rays, radii);
    }

    // Returns the near fade weighted by the camera's distance from the axis (SOLNT_NearFadeAt)
    inline float NearFadeAt(const float x, const float h, const float rt, const FVector4f& layout2)
    {
        const float t = FMath::Clamp((FMath::Abs(x) - layout2.X * rt) / ((layout2.Y - layout2.X) * rt), 0.0f, 1.0f);
        const float nearFade = t * t * (3.0f - 2.0f * t);
        const float weight = SmoothFall(FMath::Abs(h), layout2.Z * rt, layout2.W * rt);
        return 1.0f - weight * (1.0f - nearFade);
    }

    // Returns the chevrons' far fade (SOLNT_ChevronFarFade)
    inline float ChevronFarFade(const float d, const float rt, const FVector4f& style3)
    {
        const float t = FMath::Clamp((d - style3.Y * rt) / ((style3.Z - style3.Y) * rt), 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }

    // Returns an element's alpha at warp coordinate w and its axial offset (SOLNT_ElementAlpha)
    inline float ElementAlpha(const float w, const float capFade, const EElement element, const float h, const float rt,
        const float focalRefPx, const FVector2f& endArcs, const FSOLNpcTunnelMaterialVectors& v, float& outX)
    {
        const float s0 = v.Layout0.X * rt;
        const float x = w * s0;
        outX = x;
        if (!(x > endArcs.X && x < endArcs.Y) || !(capFade > 0.0f))
        {
            return 0.0f;
        }

        // Cap, end and near fades shared by every element
        const float d = std::sqrt(x * x + h * h);
        const float distanceToEnd = FMath::Min(x - endArcs.X, endArcs.Y - x);
        float alpha = capFade * FMath::Clamp(distanceToEnd / (v.Layout1.X * s0), 0.0f, 1.0f)
            * NearFadeAt(x, h, rt, v.Layout2);
        if (!(alpha > 0.0f))
        {
            return 0.0f;
        }

        // The element's own size and gap fades
        const float gapPx = RingGapPx(x, h, rt, focalRefPx, s0);
        if (element == EElement::Ring)
        {
            alpha *= RadiusFade(SizePx(d, rt, focalRefPx, 1.0f), v.Shape, v.Layout1)
                * LogRamp(gapPx, v.Style3.X, v.Layout0.Y);
        }
        else if (element == EElement::Dot)
        {
            alpha *= RadiusFade(SizePx(d, rt, focalRefPx, 1.0f), v.Shape, v.Layout1)
                * LogRamp(gapPx, v.Layout3.X, v.Layout3.Y);
        }
        else
        {
            alpha *= RadiusFade(SizePx(d, rt, focalRefPx, 2.0f * v.Shape.Z), v.Shape, v.Layout1)
                * LogRamp(gapPx, v.Layout3.X, v.Layout3.Y) * ChevronFarFade(d, rt, v.Style3);
        }
        return alpha;
    }

    // Returns ring slot k's alpha and axial offset as the vertex stage computes them (the ring branch)
    inline float RingSlotAlpha(const int32 slot, const float phase, const float h, const float rt,
        const float focalRefPx, const FVector2f& endArcs, const FSOLNpcTunnelMaterialVectors& v, float& outX)
    {
        const float k = static_cast<float>(slot);
        const float perSide = v.Layout0.Z;
        const float w = k < perSide ? k + 1.0f - phase : -((k - perSide) + phase);
        return ElementAlpha(w, FMath::Clamp(perSide - FMath::Abs(w), 0.0f, 1.0f), EElement::Ring, h, rt, focalRefPx,
            endArcs, v, outX);
    }

    // Returns a gap slot's dot or chevron alpha and axial offset as the vertex stage computes them, before the
    // element's own near fade (the dot and chevron branch)
    inline float DetailSlotAlpha(const int32 slot, const int32 index, const bool bDot, const float phase, const float h,
        const float rt, const float focalRefPx, const FVector2f& endArcs, const FSOLNpcTunnelMaterialVectors& v,
        float& outX)
    {
        const float k = static_cast<float>(slot);
        const float perSide = v.Layout0.W;
        const float offset = static_cast<float>(index) / FMath::Max(bDot ? v.Layout3.Z : v.Style3.W, 1.0f);
        const float w = (k < perSide ? k - phase : -(k - perSide + 1.0f) - phase) + offset;
        return ElementAlpha(w, FMath::Clamp(perSide - 1.0f - FMath::Abs(w), 0.0f, 1.0f),
            bDot ? EElement::Dot : EElement::Chevron, h, rt, focalRefPx, endArcs, v, outX);
    }

    // Returns an end ring's alpha and its radius on screen in reference px (SOLNT_EndRingAlpha)
    inline float EndRingAlpha(const float x, const float h, const float rt, const float focalRefPx,
        const FSOLNpcTunnelMaterialVectors& v, float& outRadiusPx)
    {
        const float d = std::sqrt(x * x + h * h);
        outRadiusPx = SizePx(d, rt, focalRefPx, 1.0f);
        return NearFadeAt(x, h, rt, v.Layout2) * RadiusFade(outRadiusPx, v.Shape, v.Layout1);
    }

    // Returns the end ring's drawn radius in reference px, max(r, floor), the floor for NaN
    // (SOLNT_EndRingDrawnRadiusPx)
    inline float EndRingDrawnRadiusPx(const float radiusPx, const float floorPx)
    {
        return radiusPx > floorPx ? radiusPx : floorPx;
    }

    // Returns a ring ribbon's inner reach toward the centre in px, min(half extent, fraction x ring radius), 0 for a
    // NaN or non-positive input (SOLNT_RingRibbonInnerExtentPx; the fraction is Layout4.y)
    inline float RingRibbonInnerExtentPx(const float halfExtentPx, const float ringRadiusPx, const float fraction)
    {
        if (!(halfExtentPx > 0.0f) || !(ringRadiusPx > 0.0f))
        {
            return 0.0f;
        }
        return FMath::Min(halfExtentPx, fraction * ringRadiusPx);
    }
}
