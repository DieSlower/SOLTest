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

    // An ellipse's implicit gradient below this is the centre (the material uses the same 1e-6)
    constexpr float HUDSHAPE_ELLIPSE_MIN_GRADIENT = 1.0e-6f;
    constexpr float HUDSHAPE_ELLIPSE_MIN_RADIUS_PX = 1.0e-3f;    // Radii are clamped to this (the material too)

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
// Signed distance to an ellipse ring (first-order approximation, exact on the axes)
float SOLHudShapeMath::EllipseDistancePx(const FVector2f& local, const float radiusXPx, const float radiusYPx,
    const float halfWidthPx)
{
    // Gradient-normalised implicit distance |k0 (k0 - 1) / k1|; k1 = 0 only at the centre, whose distance is the
    // shorter radius (mirrored in M_SOLHudShape's Custom node)
    const FVector2f radius(FMath::Max(radiusXPx, HUDSHAPE_ELLIPSE_MIN_RADIUS_PX),
        FMath::Max(radiusYPx, HUDSHAPE_ELLIPSE_MIN_RADIUS_PX));
    const float k0 = (local / radius).Size();
    const float k1 = (local / (radius * radius)).Size();
    const float distance = k1 > HUDSHAPE_ELLIPSE_MIN_GRADIENT ? FMath::Abs(k0 * (k0 - 1.0f) / k1)
        : FMath::Min(radius.X, radius.Y);
    return distance - halfWidthPx;
}

//////////////////////////////////////////////////////////////////////////
// Signed distance to a dashed segment (dashes from -halfLength, the last clipped at +halfLength)
float SOLHudShapeMath::DashedLineDistancePx(const FVector2f& local, const float halfLengthPx, const float halfWidthPx,
    const float periodPx)
{
    // Clamp to the segment, find the position within the dash period there, then the distance to the end of this
    // period's dash or the start of the next one (only if that dash exists); plus the overshoot past either end
    const float period = FMath::Max(periodPx, 1.0f);
    const float dashLength = SOL::Hologram::HUD_DASH_FRACTION * period;
    const float length = 2.0f * FMath::Max(halfLengthPx, 0.0f);
    const float t = local.X + FMath::Max(halfLengthPx, 0.0f);
    const float clamped = FMath::Clamp(t, 0.0f, length);
    float inPeriod = clamped - FMath::Floor(clamped / period) * period;
    if (length > 0.0f && clamped - inPeriod >= length)
    {
        // The far end falls exactly on a period start: no dash starts there, so it is in the previous period's gap
        inPeriod += period;
    }
    const float nextStart = clamped - inPeriod + period;
    float gap = FMath::Max(inPeriod - dashLength, 0.0f);
    if (inPeriod > dashLength && nextStart < length)
    {
        gap = FMath::Min(gap, nextStart - clamped);
    }
    return FVector2f(FMath::Abs(t - clamped) + gap, local.Y).Size() - halfWidthPx;
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
        static_cast<int32>(ESOLHudShapeKind::DashedLine)));
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

//////////////////////////////////////////////////////////////////////////
// Fade of a shape's glow across the quad padding: 1 - smoothstep(fade start x pad, pad, distance)
float SOLHudShapeMath::PadFade(const float distancePx, const float padPx)
{
    if (padPx <= 0.0f)
    {
        return 1.0f;
    }
    const float start = static_cast<float>(SOL::Hologram::HUD_PAD_FADE_START) * padPx;
    const float t = FMath::Clamp((distancePx - start) / (padPx - start), 0.0f, 1.0f);
    return 1.0f - t * t * (3.0f - 2.0f * t);
}

//////////////////////////////////////////////////////////////////////////
// A panel's interior fill: a fresnel-like edge and 1 px scanlines scaled by the strength; 0 outside
float SOLHudShapeMath::PanelFill(const float distancePx, const float fill, const float screenY, const float strength)
{
    using namespace SOL::Hologram;
    if (distancePx >= 0.0f)
    {
        return 0.0f;
    }
    const float base = static_cast<float>(HUD_PANEL_FILL_BASE);
    const float edge = base + (1.0f - base) * FMath::Exp(distancePx / static_cast<float>(HUD_PANEL_EDGE_FALLOFF_PX));
    const float scan = 1.0f + static_cast<float>(HUD_PANEL_SCANLINE) * FMath::Max(strength, 0.0f)
        * FMath::Sin(UE_PI * screenY);
    return fill * edge * scan;
}

//////////////////////////////////////////////////////////////////////////
// Panel brightness factor of the flicker: 1 minus a smooth time noise scaled by the strength
float SOLHudShapeMath::PanelFlicker(const float ageS, const float strength)
{
    using namespace SOL::Hologram;
    if (strength <= 0.0f)
    {
        return 1.0f;
    }
    const float x = FMath::Fmod(ageS, HUD_TIME_WRAP_S) * static_cast<float>(HUD_PANEL_FLICKER_RATE_HZ)
        + static_cast<float>(HUD_PANEL_FLICKER_OFFSET);
    return 1.0f - static_cast<float>(HUD_PANEL_FLICKER) * strength * SmoothNoise(x);
}

//////////////////////////////////////////////////////////////////////////
// LT tone curve: 1 - exp(-gain x max(c, 0)^power)
float SOLHudShapeMath::ToneCurve(const float c)
{
    using namespace SOL::Hologram;
    return 1.0f - FMath::Exp(-static_cast<float>(HUD_TONE_GAIN) * FMath::Pow(FMath::Max(c, 0.0f),
        static_cast<float>(HUD_TONE_POWER)));
}

//////////////////////////////////////////////////////////////////////////
// LT blue grade: the quartic Bezier 0, B1, B2, B3, 1 at t clamped to [0, 1]
float SOLHudShapeMath::GradeBlue(const float tIn)
{
    using namespace SOL::Hologram;
    const float t = FMath::Clamp(tIn, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return 4.0f * t * u * u * u * static_cast<float>(HUD_GRADE_BLUE_1)
        + 6.0f * t * t * u * u * static_cast<float>(HUD_GRADE_BLUE_2)
        + 4.0f * t * t * t * u * static_cast<float>(HUD_GRADE_BLUE_3) + t * t * t * t;
}

//////////////////////////////////////////////////////////////////////////
// The panel around a content rectangle grown by a padding: its centre and half size (zero for an invalid rectangle)
void SOLHudShapeMath::PanelAroundRect(const FBox2f& content, const float paddingPx, FVector2f& outCenter,
    FVector2f& outHalfSize)
{
    if (!content.bIsValid)
    {
        outCenter = FVector2f::ZeroVector;
        outHalfSize = FVector2f::ZeroVector;
        return;
    }
    outCenter = content.GetCenter();
    outHalfSize = content.GetExtent() + FVector2f(paddingPx, paddingPx);
}

//////////////////////////////////////////////////////////////////////////
// The horizontal capsule filling a box (glow core reaching coreHalfPx past it): end centres and half width
bool SOLHudShapeMath::BoxCapsule(const float x, const float y, const float width, const float height,
    const float coreHalfPx, FVector2f& outStart, FVector2f& outEnd, float& outHalfWidth)
{
    if (width <= 0.0f || height <= 0.0f)
    {
        return false;
    }
    // Thinner than the box by the core's reach, but never wider than the box is long (a short fill stays inside it)
    outHalfWidth = FMath::Min(FMath::Max(0.5f * height - coreHalfPx, coreHalfPx), 0.5f * width);
    const float midY = y + 0.5f * height;
    outStart = FVector2f(x + outHalfWidth, midY);
    outEnd = FVector2f(x + width - outHalfWidth, midY);
    return true;
}
