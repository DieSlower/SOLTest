/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

#include <cmath>

// The thruster materials' vector parameters (SOL::ThrusterMaterialParams), packed from SOL::Thruster
struct FSOLThrusterMaterialVectors
{
    FLinearColor Rim[3];        // RIM_0..2: blue, yellow, red keys
    FLinearColor Core[3];       // CORE_0..2
    FVector4f Ramp;             // (yellow c, 0, 0, 0)
    FVector4f Shock;            // (spacing factor, brightness gain, decay, zone)
    FVector4f Shape;            // (max knots, tail streak strength, knot half length, knot half width)
    FVector4f Look;             // (emissive, core power, knot gain, streak strength)
    FVector4f Streak;           // (tiles around, tile length in nozzle radii, pan tiles/s at b = 1, 0)
    FVector4f Fade;             // (cone low px, cone high px, reference height px, 0)
    FVector4f Glow;             // (diameter in nozzle radii, min drawn px, emissive, reference height px)
    FVector4f GlowFade;         // (low px, high px, 0, 0)
    FVector4f Burst;            // (max opacity, flicker Hz, flicker depth, emissive)
    FVector4f BurstShape;       // (base radius, tail radius in burst lengths, low px, high px)
    FLinearColor BurstColour;
    FVector4f Flame;            // (flame length factor, tail width ratio, sharp low px, sharp high px)
    FVector4f Ribbon;           // Strip pool: (min width px, texels per flame, flames per block, points per flame)
};

// The C++ float copy of part of the thruster shader's math, Shaders/Private/SOLThruster.ush (SDD 51 sections 4.3-4.4,
// 11.2): the functions below are copied statement for statement and in the same order, so the shader's float path
// can be compared with the double SOLThruster functions on the CPU (SOLTest.Thruster.MaterialMirror,
// Tests/ThrusterMirrorTest.cpp). Not mirrored: the vertex stage (SOLTH_FlameVertex, the strip pool's texel reads and
// tangent), the flame pixel's radial fraction and taper (checked on screen by -SOLSmokeRibbonParity and
// -SOLSmokeFlameSeam instead). It reads the same packed vectors the materials hold (MaterialVectors). A change to a
// mirrored shader function must change this file.
namespace SOLThrusterMirror
{
    // Upper bound of the knot-count loop (SOLTH_KNOT_LOOP_CAP)
    inline constexpr int32 KNOT_LOOP_CAP = 16;
    static_assert(SOL::Thruster::SHOCK_MAX_KNOTS <= KNOT_LOOP_CAP, "the shader's knot loop covers SHOCK_MAX_KNOTS");

    // Returns the material vector parameters packed from SOL::Thruster (the defaults create_thruster_assets.py writes)
    inline FSOLThrusterMaterialVectors MaterialVectors()
    {
        namespace T = SOL::Thruster;
        const float refHeight = static_cast<float>(SOL::NPC_LANE_REFERENCE_HEIGHT_PX);
        FSOLThrusterMaterialVectors v;
        v.Rim[0] = T::RIM_BLUE;
        v.Rim[1] = T::RIM_YELLOW;
        v.Rim[2] = T::RIM_RED;
        v.Core[0] = T::CORE_BLUE;
        v.Core[1] = T::CORE_YELLOW;
        v.Core[2] = T::CORE_RED;
        v.Ramp = FVector4f(static_cast<float>(T::COLOUR_YELLOW_C), 0.0f, 0.0f, 0.0f);
        v.Shock = FVector4f(static_cast<float>(T::SHOCK_SPACING_FACTOR), static_cast<float>(T::SHOCK_BRIGHTNESS_GAIN),
            static_cast<float>(T::SHOCK_DECAY), static_cast<float>(T::SHOCK_ZONE));
        v.Shape = FVector4f(static_cast<float>(T::SHOCK_MAX_KNOTS), static_cast<float>(T::FLAME_STREAK_TAIL_STRENGTH),
            static_cast<float>(T::SHOCK_KNOT_HALF_LENGTH), static_cast<float>(T::SHOCK_KNOT_HALF_WIDTH));
        v.Look = FVector4f(static_cast<float>(T::PLUME_EMISSIVE), static_cast<float>(T::PLUME_CORE_POWER),
            static_cast<float>(T::SHOCK_KNOT_GAIN), static_cast<float>(T::STREAK_STRENGTH));
        v.Streak = FVector4f(static_cast<float>(T::STREAK_TILES_AROUND), static_cast<float>(T::STREAK_TILE_LENGTH_RADII),
            static_cast<float>(T::STREAK_PAN_TILES_PER_S), 0.0f);
        v.Fade = FVector4f(static_cast<float>(T::CONE_FADE_LOW_PX), static_cast<float>(T::CONE_FADE_HIGH_PX), refHeight,
            0.0f);
        v.Glow = FVector4f(static_cast<float>(T::GLOW_DIAMETER_NOZZLE_RADII), static_cast<float>(T::GLOW_MIN_DRAWN_PX),
            static_cast<float>(T::GLOW_EMISSIVE), refHeight);
        v.GlowFade = FVector4f(static_cast<float>(T::GLOW_FADE_LOW_PX), static_cast<float>(T::GLOW_FADE_HIGH_PX), 0.0f,
            0.0f);
        v.Burst = FVector4f(static_cast<float>(T::BURST_MAX_OPACITY), static_cast<float>(T::BURST_FLICKER_HZ),
            static_cast<float>(T::BURST_FLICKER_DEPTH), static_cast<float>(T::BURST_EMISSIVE));
        v.BurstShape = FVector4f(static_cast<float>(T::BURST_BASE_RADIUS_LENGTHS),
            static_cast<float>(T::BURST_TAIL_RADIUS_LENGTHS), static_cast<float>(T::CONE_FADE_LOW_PX),
            static_cast<float>(T::CONE_FADE_HIGH_PX));
        v.BurstColour = T::BURST_COLOUR;
        v.Flame = FVector4f(static_cast<float>(T::FLAME_LENGTH_FACTOR), static_cast<float>(T::FLAME_TAIL_WIDTH_RATIO),
            static_cast<float>(T::FLAME_SHARP_LOW_PX), static_cast<float>(T::FLAME_SHARP_HIGH_PX));
        v.Ribbon = FVector4f(static_cast<float>(T::RIBBON_MIN_WIDTH_PX), static_cast<float>(T::RIBBON_TEXELS_PER_TRAIL),
            static_cast<float>(T::RIBBON_TRAILS_PER_BLOCK), static_cast<float>(T::FLAME_DRAWN_POINTS));
        return v;
    }

    // Returns a value clamped to [0, 1], NaN as 0 (SOLTH_Unit)
    inline float Unit(const float value)
    {
        return value >= 0.0f ? FMath::Min(value, 1.0f) : 0.0f;
    }

    // Writes the ramp's rim and core colours at c (SOLTH_PlumeColour)
    inline void PlumeColour(const float c, const FVector4f& ramp, const FLinearColor rims[3],
        const FLinearColor cores[3], FLinearColor& outRim, FLinearColor& outCore)
    {
        const float clamped = Unit(c);
        const float yellowC = ramp.X;
        if (clamped <= yellowC)
        {
            const float t = clamped / yellowC;
            outRim = FMath::Lerp(rims[0], rims[1], t);
            outCore = FMath::Lerp(cores[0], cores[1], t);
        }
        else
        {
            const float t = (clamped - yellowC) / (1.0f - yellowC);
            outRim = FMath::Lerp(rims[1], rims[2], t);
            outCore = FMath::Lerp(cores[1], cores[2], t);
        }
    }

    // Returns the brightness min(G / gRef, 1)^exponent, 0 under minG and for NaN or infinite G (SOLTH_PlumeBrightness)
    inline float PlumeBrightness(const float g, const float gRef, const float minG, const float exponent)
    {
        if (!(g >= minG) || std::isinf(g))
        {
            return 0.0f;
        }
        const float x = FMath::Min(g / gRef, 1.0f);
        return x > 0.0f ? std::pow(x, exponent) : 0.0f;
    }

    // Returns the knot spacing (SOLTH_ShockSpacing)
    inline float ShockSpacing(const float nozzleRadiusM, const float b, const FVector4f& shock)
    {
        if (!(nozzleRadiusM > 0.0f) || std::isinf(nozzleRadiusM))
        {
            return 0.0f;
        }
        return shock.X * nozzleRadiusM * (1.0f + shock.Y * Unit(b));
    }

    // Returns the knot count (SOLTH_ShockKnotCount)
    inline int32 ShockKnotCount(const float plumeLengthM, const float spacingM, const FVector4f& shock,
        const FVector4f& shape)
    {
        if (!(plumeLengthM > 0.0f) || std::isinf(plumeLengthM) || !(spacingM > 0.0f))
        {
            return 0;
        }
        const float zoneM = shock.W * plumeLengthM;
        const int32 maxKnots = FMath::Min(static_cast<int32>(shape.X + 0.5f), KNOT_LOOP_CAP);
        int32 count = 0;
        for (int32 k = 0; k < KNOT_LOOP_CAP; ++k)
        {
            if (count >= maxKnots || static_cast<float>(count + 1) * spacingM > zoneM)
            {
                break;
            }
            ++count;
        }
        return count;
    }

    // Writes knot k's (1-based) position and strength (SOLTH_ShockKnot)
    inline void ShockKnot(const int32 k, const float spacingM, const FVector4f& shock, float& outPositionM,
        float& outStrength)
    {
        outPositionM = static_cast<float>(k) * spacingM;
        outStrength = std::pow(shock.Z, static_cast<float>(k - 1));
    }

    // Returns the shock-diamond brightness at xM and radial fraction rFrac (SOLTH_ShockIntensity)
    inline float ShockIntensity(const float xM, const float rFrac, const float spacingM, const int32 count,
        const FVector4f& shock, const FVector4f& shape)
    {
        if (count <= 0 || !(spacingM > 0.0f))
        {
            return 0.0f;
        }
        const int32 k = static_cast<int32>(FMath::Clamp(std::floor(xM / spacingM + 0.5f), 1.0f,
            static_cast<float>(count)));
        float positionM;
        float strength;
        ShockKnot(k, spacingM, shock, positionM, strength);
        const float rhombus = FMath::Abs(xM - positionM) / (shape.Z * spacingM) + rFrac / shape.W;
        return strength * FMath::Clamp(1.0f - rhombus, 0.0f, 1.0f);
    }

    // Returns the smoothstep of u clamped to [0, 1] (SOLTH_Smoothstep)
    inline float Smoothstep(const float u)
    {
        const float clamped = FMath::Clamp(u, 0.0f, 1.0f);
        return clamped * clamped * (3.0f - 2.0f * clamped);
    }

    // Returns the flame core weight (sqrt(1 - r^2))^look.y, r clamped to [0, 1], NaN as 0 (SOLTH_FlameCoreWeight)
    inline float FlameCoreWeight(const float radialFraction, const FVector4f& look)
    {
        if (FMath::IsNaN(radialFraction))
        {
            return 0.0f;
        }
        const float r = FMath::Clamp(radialFraction, 0.0f, 1.0f);
        return std::pow(std::sqrt(FMath::Max(1.0f - r * r, 0.0f)), look.Y);
    }

    // Returns the flame's knot brightness at arc coordinate s and radial fraction r (SOLTH_FlameKnot): the knot
    // spacing and count of the flame's header, then the nearest knot's rhombus
    inline float FlameKnot(const float arcM, const float radialFraction, const float plumeLengthM,
        const float nozzleRadiusM, const float b, const FVector4f& shock, const FVector4f& shape)
    {
        const float spacingM = ShockSpacing(nozzleRadiusM, b, shock);
        const int32 count = ShockKnotCount(plumeLengthM, spacingM, shock, shape);
        return ShockIntensity(arcM, Unit(radialFraction), spacingM, count, shock, shape);
    }

    // Returns the streak depth lerp(body, tail, smoothstep((s - 0.5 L_p) / L_p)); the body depth for a non-positive
    // L_p (SOLTH_FlameStreakDepth)
    inline float FlameStreakDepth(const float arcM, const float plumeLengthM, const float bodyDepth,
        const float tailDepth)
    {
        if (!(plumeLengthM > 0.0f) || std::isinf(plumeLengthM) || FMath::IsNaN(arcM))
        {
            return bodyDepth;
        }
        return FMath::Lerp(bodyDepth, tailDepth, Smoothstep((arcM - 0.5f * plumeLengthM) / plumeLengthM));
    }

    // Returns the thin-flame shading weight from widths and the pixel size in cm (SOLTH_FlameSharpness): saturate(true
    // / drawn) x saturate((drawn / pixel - flame.z) / max(flame.w - flame.z, 1e-3)); 1 without a drawn width or pixel
    inline float FlameSharpness(const float trueCm, const float drawnCm, const float pixelCm, const FVector4f& flame)
    {
        if (!(drawnCm > 0.0f) || !(pixelCm > 0.0f))
        {
            return 1.0f;
        }
        const float drawnPx = drawnCm / pixelCm;
        return Unit(trueCm / drawnCm) * Unit((drawnPx - flame.Z) / FMath::Max(flame.W - flame.Z, 1.0e-3f));
    }

    // Returns the point alpha of a pixel on the narrow tail seen through the neck (SOLTH_FlameNeckAlpha): seen down the
    // spine, the tail wall (s > L_p) the ray crosses farther along s (the far half from the nozzle side, the near half
    // from the tail side) takes the along fade at its other crossing, s - w |sin a| cot(theta), never before L_p; the
    // alpha unchanged elsewhere. header = (c, b, L_p, r_n)
    inline float FlameNeckAlpha(const float alpha, const float arcFraction, const float around,
        const FVector3f& tangentW, const FVector3f& toCameraW, const FVector4f& header, const FVector4f& flame)
    {
        const float arcM = arcFraction * flame.X * header.Z;
        const float tangentLength = tangentW.Size();
        const float viewLength = toCameraW.Size();
        if (!(arcM > header.Z) || !(tangentLength > 0.0f) || !(viewLength > 0.0f) || !(flame.X > 1.0f))
        {
            return alpha;
        }
        const FVector3f axis = tangentW / tangentLength;
        const FVector3f viewDir = toCameraW / viewLength;
        // The wall half crossed farther along s: the far half running toward the tail, the near half toward the nozzle
        const float towardTail = -FVector3f::DotProduct(viewDir, axis);
        const float laterSide = -std::sin(6.2831853f * around) * FMath::Sign(towardTail);
        if (!(laterSide > 0.0f))
        {
            return alpha;
        }

        // The ray's run along the spine between the tail's two walls, back to its crossing nearer the nozzle (clamped
        // at the neck)
        const float sinTheta = FMath::Max((viewDir - FVector3f::DotProduct(viewDir, axis) * axis).Size(), 1.0e-4f);
        const float runM = 2.0f * header.W * flame.Y * laterSide * FMath::Abs(towardTail) / sinTheta;
        const float entryM = FMath::Max(arcM - runM, header.Z);
        const float along = FMath::Max(1.0f - arcFraction, 1.0e-3f);
        return alpha * Unit(1.0f - entryM / (flame.X * header.Z)) / along;
    }

    // Returns the focal length in reference px (SOLTH_FocalRefPx)
    inline float FocalRefPx(const float projectionScaleX, const FVector2f& viewSize, const float referenceHeightPx)
    {
        if (!(viewSize.X > 0.0f) || !(viewSize.Y > 0.0f))
        {
            return 0.0f;
        }
        return 0.5f * viewSize.X * projectionScaleX * referenceHeightPx / viewSize.Y;
    }

    // Returns the linear pixel fade (SOLTH_LodAlpha)
    inline float LodAlpha(const float px, const float lowPx, const float highPx)
    {
        return FMath::Clamp((px - lowPx) / (highPx - lowPx), 0.0f, 1.0f);
    }
}
