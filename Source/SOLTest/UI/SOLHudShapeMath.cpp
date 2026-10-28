/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudShapeMath.h"

#include "SOLConstants.h"

namespace
{
    // LT core: the glow stays at full strength within this distance of the shape's centre line (pixels)
    constexpr float HUDSHAPE_CORE_HALF_PX = static_cast<float>(SOL::Hologram::HUD_GLOW_CORE_HALF_PX);

    //////////////////////////////////////////////////////////////////////////
    // Returns the lattice hash of an integer-valued x in [0, 1): frac(sin(wrap(x) x frequency) x scale), the lattice
    // wrapped so sin never sees a large argument (GPU sin loses precision there); the same as the material
    float HudShapeHash(const float x)
    {
        using namespace SOL::Hologram;
        const float wrapped = FMath::Fmod(x, HUD_NOISE_LATTICE_PERIOD);
        return FMath::Frac(FMath::Sin(wrapped * HUD_NOISE_HASH_FREQUENCY) * HUD_NOISE_HASH_SCALE);
    }
}

//////////////////////////////////////////////////////////////////////////
// LT glow of a pixel at a distance from a 1 px core
float SOLHudShapeMath::GlowProfile(const float distancePx)
{
    using namespace SOL::Hologram;
    const float d = FMath::Max(distancePx, 0.0f);
    return static_cast<float>(HUD_GLOW_CORE_WEIGHT) * FMath::Exp(-static_cast<float>(HUD_GLOW_CORE_FALLOFF)
            * FMath::Max(0.0f, d - HUDSHAPE_CORE_HALF_PX))
        + static_cast<float>(HUD_GLOW_HALO_WEIGHT) * FMath::Exp(-FMath::Pow(static_cast<float>(HUD_GLOW_HALO_SCALE) * d,
            static_cast<float>(HUD_GLOW_HALO_POWER)));
}

//////////////////////////////////////////////////////////////////////////
// Signed distance to a capsule along local X
float SOLHudShapeMath::LineDistancePx(const FVector2f& local, const float halfLengthPx, const float halfWidthPx)
{
    return FVector2f(FMath::Max(FMath::Abs(local.X) - halfLengthPx, 0.0f), local.Y).Size() - halfWidthPx;
}

//////////////////////////////////////////////////////////////////////////
// Signed distance to an arc symmetric about local +Y (iq's sdArc)
float SOLHudShapeMath::ArcDistancePx(const FVector2f& local, const float radiusPx, const float halfWidthPx,
    const float halfApertureRad)
{
    const float aperture = FMath::Clamp(halfApertureRad, 0.0f, UE_PI);
    // sin is never negative on [0, pi]; clamped because float sin(pi) is -8.7e-8, which breaks the full ring's test
    const FVector2f sc(FMath::Max(FMath::Sin(aperture), 0.0f), FMath::Cos(aperture));
    const FVector2f q(FMath::Abs(local.X), local.Y);
    const float d = sc.Y * q.X > sc.X * q.Y ? (q - sc * radiusPx).Size() : FMath::Abs(q.Size() - radiusPx);
    return d - halfWidthPx;
}

//////////////////////////////////////////////////////////////////////////
// Signed distance to a rounded box
float SOLHudShapeMath::PanelDistancePx(const FVector2f& local, const FVector2f& halfSizePx, const float cornerRadiusPx)
{
    const FVector2f q(FMath::Abs(local.X) - halfSizePx.X + cornerRadiusPx,
        FMath::Abs(local.Y) - halfSizePx.Y + cornerRadiusPx);
    return FVector2f(FMath::Max(q.X, 0.0f), FMath::Max(q.Y, 0.0f)).Size() + FMath::Min(FMath::Max(q.X, q.Y), 0.0f)
        - cornerRadiusPx;
}

//////////////////////////////////////////////////////////////////////////
// Packs a kind and a size parameter into one float
float SOLHudShapeMath::EncodeKind(const ESOLHudShapeKind kind, const float param)
{
    // A parameter outside [0, stride) would decode as another kind, so it is clamped just inside
    const float clamped = FMath::Clamp(param, 0.0f, SOL::Hologram::HUD_SHAPE_KIND_STRIDE - 1.0f);
    return static_cast<float>(static_cast<uint8>(kind)) * SOL::Hologram::HUD_SHAPE_KIND_STRIDE + clamped;
}

//////////////////////////////////////////////////////////////////////////
// Unpacks EncodeKind's float
void SOLHudShapeMath::DecodeKind(const float encoded, ESOLHudShapeKind& outKind, float& outParam)
{
    const float kind = FMath::Floor(encoded / SOL::Hologram::HUD_SHAPE_KIND_STRIDE + 1.0e-4f);
    outKind = static_cast<ESOLHudShapeKind>(FMath::Clamp(static_cast<int32>(kind), 0,
        static_cast<int32>(ESOLHudShapeKind::Panel)));
    outParam = encoded - kind * SOL::Hologram::HUD_SHAPE_KIND_STRIDE;
}

//////////////////////////////////////////////////////////////////////////
// Smooth 1D value noise in [0, 1]
float SOLHudShapeMath::SmoothNoise(const float x)
{
    const float i = FMath::Floor(x);
    const float f = x - i;
    const float u = f * f * (3.0f - 2.0f * f);
    return FMath::Clamp(FMath::Lerp(HudShapeHash(i), HudShapeHash(i + 1.0f), u), 0.0f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// Row jitter as a fraction of the screen width
float SOLHudShapeMath::JitterOffsetFraction(const float y01, const float ageS, const float strength)
{
    using namespace SOL::Hologram;
    if (strength <= 0.0f)
    {
        return 0.0f;
    }
    const float row = y01 * static_cast<float>(HUD_JITTER_ROW_CELLS);
    const float scroll = FMath::Fmod(ageS, HUD_TIME_WRAP_S) * static_cast<float>(HUD_JITTER_SCROLL_RATE);
    const float n1 = SmoothNoise(row + scroll);
    const float n2 = SmoothNoise(row + scroll * static_cast<float>(HUD_JITTER_SIGN_SPEED)
        + static_cast<float>(HUD_JITTER_SIGN_OFFSET));
    return static_cast<float>(HUD_JITTER_MAX_FRACTION) * strength
        * FMath::Exp(-static_cast<float>(HUD_JITTER_SHARPNESS) * n1) * (n2 >= 0.5f ? 1.0f : -1.0f);
}
