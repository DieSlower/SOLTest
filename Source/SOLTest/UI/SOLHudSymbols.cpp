/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudSymbols.h"

#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudShapeMath.h"

namespace
{
    // Every symbol's id, in ESOLHudSymbol order (the ids GLOSSARY_ENTRIES' Symbols cells use)
    constexpr const TCHAR* HUD_SYMBOL_NAMES[] =
    {
        TEXT("Reticle"), TEXT("Joystick"), TEXT("JoystickFreeLook"), TEXT("SteerFree"), TEXT("SteerHeld"),
        TEXT("SteerLocked"), TEXT("Prograde"), TEXT("Retrograde"), TEXT("TargetBracket"), TEXT("TargetEdgeArrow"),
        TEXT("IconStar"), TEXT("IconPlanet"), TEXT("IconMoon"), TEXT("IconDwarf"), TEXT("IconAsteroid"),
        TEXT("IconRing"), TEXT("IconShip"), TEXT("IconTarget"), TEXT("IconEdgeArrow"), TEXT("RadarScope"),
        TEXT("RadarContactAbove"), TEXT("RadarContactBelow"), TEXT("RadarSelected"), TEXT("GMeter"),
        TEXT("SpeedCapBar"), TEXT("Card"), TEXT("OrbitLine"), TEXT("RouteLeg"), TEXT("RouteLoopLeg"),
        TEXT("WaypointAbove"), TEXT("WaypointBelow"), TEXT("MapBodyIcon"), TEXT("MapPick"), TEXT("MapGridStem"),
        TEXT("RadialMenu"), TEXT("ListFrame"), TEXT("HoloCursor"), TEXT("WarpStreaks"),
    };
    static_assert(UE_ARRAY_COUNT(HUD_SYMBOL_NAMES) == static_cast<int32>(ESOLHudSymbol::Count),
        "one id per HUD symbol");

    // The overlay icon symbols run IconStar ... IconTarget in EIconCategory order
    static_assert(static_cast<int32>(ESOLHudSymbol::IconTarget) - static_cast<int32>(ESOLHudSymbol::IconStar) + 1
        == static_cast<int32>(SOLOverlayIcons::EIconCategory::Count), "one icon symbol per overlay icon category");

    // The direction the IconEdgeArrow symbol points (to the right, as for an object off the right edge)
    const FVector2f HUD_SYMBOL_EDGE_ARROW_DIRECTION(1.0f, 0.0f);

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a UI scale is a positive finite number
    bool HudSymbolsIsValidScale(const float scale)
    {
        return scale > 0.0f && FMath::IsFinite(scale);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an overlay icon primitive of a category at a centre, at the overlay layer's radius and intensity
    FSOLHudSymbolPrim HudSymbolsIconPrim(const SOLOverlayIcons::EIconCategory category, const FVector2f& centre,
        const float scale)
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Icon;
        prim.A = centre;
        prim.RadiusPx = SOL::Overlay::ICON_RADIUS_PX * scale;
        prim.Intensity = SOL::Overlay::ICON_INTENSITY;
        prim.Category = category;
        return prim;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the box a primitive draws over (its points and sizes, the builders' real reach for Icon / EdgeArrow /
    // Card; glow pad excluded)
    FBox2f HudSymbolsPrimExtent(const FSOLHudSymbolPrim& prim)
    {
        const FVector2f radius(prim.RadiusPx, prim.RadiusPx);
        switch (prim.Kind)
        {
        case ESOLHudPrimKind::Line:
        case ESOLHudPrimKind::DashedLine:
            return FBox2f(FVector2f::Min(prim.A, prim.B), FVector2f::Max(prim.A, prim.B));
        case ESOLHudPrimKind::Ring:
        case ESOLHudPrimKind::Arc:
        case ESOLHudPrimKind::Dot:
            return FBox2f(prim.A - radius, prim.A + radius);
        case ESOLHudPrimKind::Ellipse:
            return FBox2f(prim.A - FVector2f(prim.RadiusPx, prim.RadiusYPx),
                prim.A + FVector2f(prim.RadiusPx, prim.RadiusYPx));
        case ESOLHudPrimKind::Box:
            return FBox2f(FVector2f::Min(prim.A, prim.A + prim.B), FVector2f::Max(prim.A, prim.A + prim.B));
        case ESOLHudPrimKind::Panel:
            return FBox2f(prim.A - prim.B, prim.A + prim.B);
        case ESOLHudPrimKind::Icon:
        {
            // The icon's nominal radius and whatever its shapes reach past it (the star's rays, the ring's ellipse)
            FBox2f box(prim.A - radius, prim.A + radius);
            const FBox2f drawn = SOLOverlayIcons::IconExtent(prim.Category, prim.A, prim.RadiusPx);
            return drawn.bIsValid ? box + drawn : box;
        }
        case ESOLHudPrimKind::EdgeArrow:
        {
            const FVector2f tip = prim.A + prim.B * prim.RadiusPx;
            const FBox2f box(FVector2f::Min(prim.A, tip), FVector2f::Max(prim.A, tip));
            const FBox2f drawn = SOLOverlayIcons::EdgeArrowExtent(prim.A, prim.B, prim.RadiusPx);
            return drawn.bIsValid ? box + drawn : box;
        }
        case ESOLHudPrimKind::Card:
            return SOLHoloFrameBuild::CardRect(FBox2f(FVector2f::Min(prim.A, prim.B), FVector2f::Max(prim.A, prim.B)),
                prim.UiScale);
        case ESOLHudPrimKind::Text:
        default:
            return FBox2f(prim.A, prim.A);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a palette role's colour (Primary's for a role out of range)
    const FLinearColor& HudSymbolsRoleColor(const FSOLHudSymbolPalette& palette, const ESOLHudPaletteRole role)
    {
        const int32 index = static_cast<int32>(role);
        return palette.Colors[index < static_cast<int32>(ESOLHudPaletteRole::Count) ? index
            : static_cast<int32>(ESOLHudPaletteRole::Primary)];
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the look a glossary card is built with: fully drawn in, filled, no idle sweep (a still picture)
    FSOLHoloMenuLook HudSymbolsCardLook()
    {
        FSOLHoloMenuLook look;
        look.bVisible = true;
        look.Progress = 1.0f;
        look.FrameFraction = 1.0f;
        look.FillAlpha = 1.0f;
        return look;
    }

    //////////////////////////////////////////////////////////////////////////
    // Replays primitives into a shape batch (no reset), a Card into cardScratch or, without one, a frame output on the
    // stack; returns the shapes added
    int32 HudSymbolsReplay(const FSOLHudSymbolPrims& prims, FSOLHudShapeBatch& batch,
        const FSOLHudSymbolPalette& palette, const float padPx, FSOLHoloFrameOutput* cardScratch)
    {
        const int32 before = batch.GetShapeCount();
        const int32 count = FMath::Clamp(prims.Count, 0, SOL::Help::MAX_PRIMS_PER_SYMBOL);
        for (int32 index = 0; index < count; ++index)
        {
            const FSOLHudSymbolPrim& prim = prims.Prims[index];
            const FLinearColor& color = HudSymbolsRoleColor(palette, prim.Role);
            const float halfWidth = prim.HalfWidthPx > 0.0f ? prim.HalfWidthPx : SOL::Hologram::HUD_LINE_HALF_WIDTH_PX;
            switch (prim.Kind)
            {
            case ESOLHudPrimKind::Line:
                batch.AddLine(prim.A, prim.B, halfWidth, color, prim.Intensity, padPx);
                break;
            case ESOLHudPrimKind::Ring:
                batch.AddRing(prim.A, prim.RadiusPx, halfWidth, color, prim.Intensity, padPx);
                break;
            case ESOLHudPrimKind::Arc:
                batch.AddArc(prim.A, prim.RadiusPx, halfWidth, prim.MidAngleRad, prim.HalfApertureRad, color,
                    prim.Intensity, padPx);
                break;
            case ESOLHudPrimKind::Box:
            {
                // A filled box is one capsule along its long axis, as ASOLFlightHud::GlowBox draws it
                FVector2f start;
                FVector2f end;
                float capsuleHalfWidth = 0.0f;
                if (SOLHudShapeMath::BoxCapsule(prim.A.X, prim.A.Y, prim.B.X, prim.B.Y,
                    SOL::Hologram::HUD_LINE_HALF_WIDTH_PX, start, end, capsuleHalfWidth))
                {
                    batch.AddLine(start, end, capsuleHalfWidth, color, prim.Intensity, padPx);
                }
                break;
            }
            case ESOLHudPrimKind::Dot:
                // A dot is a zero-length line of the dot's radius, as ASOLFlightHud::GlowDot draws it
                batch.AddLine(prim.A, prim.A, prim.RadiusPx, color, prim.Intensity, padPx);
                break;
            case ESOLHudPrimKind::DashedLine:
                // One shape whose period is a dash and a gap, as ASOLFlightHud::GlowDashedLine draws it
                batch.AddDashedLine(prim.A, prim.B, halfWidth, prim.DashPx + prim.GapPx, color, prim.Intensity, padPx);
                break;
            case ESOLHudPrimKind::Ellipse:
                batch.AddEllipse(prim.A, prim.RadiusPx, prim.RadiusYPx, halfWidth, color, prim.Intensity, padPx);
                break;
            case ESOLHudPrimKind::Panel:
                batch.AddPanel(prim.A, prim.B, color, prim.Intensity, prim.Fill, padPx);
                break;
            case ESOLHudPrimKind::Icon:
                // Icons keep their category's colour, as the overlay layer draws them
                SOLOverlayIcons::AddIconShapes(batch, prim.Category, prim.A, prim.RadiusPx,
                    SOLOverlayIcons::CategoryColor(prim.Category), prim.Intensity, padPx);
                break;
            case ESOLHudPrimKind::EdgeArrow:
                SOLOverlayIcons::AddEdgeArrow(batch, prim.A, prim.B, prim.RadiusPx, color, prim.Intensity, padPx);
                break;
            case ESOLHudPrimKind::Card:
            {
                // The card's shapes come from the card builder (its backing is the layer's, not the batch's)
                const FBox2f content(FVector2f::Min(prim.A, prim.B), FVector2f::Max(prim.A, prim.B));
                if (cardScratch != nullptr)
                {
                    SOLHoloFrameBuild::BuildCard(content, prim.UiScale, HudSymbolsCardLook(), palette.Strength,
                        *cardScratch);
                    SOLHoloFrameBuild::AppendToBatch(*cardScratch, batch);
                    break;
                }
                FSOLHoloFrameOutput card;
                SOLHoloFrameBuild::BuildCard(content, prim.UiScale, HudSymbolsCardLook(), palette.Strength, card);
                SOLHoloFrameBuild::AppendToBatch(card, batch);
                break;
            }
            case ESOLHudPrimKind::Text:
            default:
                break;
            }
        }
        return batch.GetShapeCount() - before;
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes a symbol's primitives centred on centre at a UI scale (33a: the symbols with an existing builder; part 33d-i
// moves the HUD's inline geometry here, the radar symbols too: their sizes, tilt and intensities are inline in
// ASOLFlightHud::DrawRadar, SOLRadarLayout holds only the projection math)
int32 SOLHudSymbols::Build(const ESOLHudSymbol symbol, const FVector2f& centre, const float scale,
    FSOLHudSymbolPrims& out)
{
    out.Reset();
    if (!HudSymbolsIsValidScale(scale))
    {
        return 0;
    }
    switch (symbol)
    {
    case ESOLHudSymbol::IconStar:
    case ESOLHudSymbol::IconPlanet:
    case ESOLHudSymbol::IconMoon:
    case ESOLHudSymbol::IconDwarf:
    case ESOLHudSymbol::IconAsteroid:
    case ESOLHudSymbol::IconShip:
    case ESOLHudSymbol::IconTarget:
        out.Add(HudSymbolsIconPrim(static_cast<SOLOverlayIcons::EIconCategory>(static_cast<int32>(symbol)
            - static_cast<int32>(ESOLHudSymbol::IconStar)), centre, scale));
        break;
    case ESOLHudSymbol::IconRing:
        // The ring system's ellipse goes around its planet's icon, as the overlay layer draws it
        out.Add(HudSymbolsIconPrim(SOLOverlayIcons::EIconCategory::Planet, centre, scale));
        out.Add(HudSymbolsIconPrim(SOLOverlayIcons::EIconCategory::Ring, centre, scale));
        break;
    case ESOLHudSymbol::IconEdgeArrow:
    {
        // The overlay layer's chevron, centred: its base half a length behind the centre, its tip half a length ahead
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::EdgeArrow;
        prim.RadiusPx = SOL::Overlay::EDGE_ARROW_PX * scale;
        prim.B = HUD_SYMBOL_EDGE_ARROW_DIRECTION;
        prim.A = centre - prim.B * (0.5f * prim.RadiusPx);
        prim.Intensity = SOL::Overlay::EDGE_ARROW_INTENSITY;
        out.Add(prim);
        break;
    }
    case ESOLHudSymbol::Card:
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Card;
        const FVector2f half(SOL::Help::GLOSSARY_CARD_HALF_WIDTH_PX * scale,
            SOL::Help::GLOSSARY_CARD_HALF_HEIGHT_PX * scale);
        prim.A = centre - half;
        prim.B = centre + half;
        prim.UiScale = scale;
        out.Add(prim);
        break;
    }
    default:
        break;
    }
    return out.Count;
}

//////////////////////////////////////////////////////////////////////////
// Returns a symbol's bounds relative to its centre: the union of what each of its primitives draws over
FBox2f SOLHudSymbols::Bounds(const ESOLHudSymbol symbol, const float scale, FSOLHudSymbolPrims& scratch)
{
    FBox2f bounds(ForceInit);
    const int32 count = Build(symbol, FVector2f::ZeroVector, scale, scratch);
    for (int32 index = 0; index < count; ++index)
    {
        bounds += HudSymbolsPrimExtent(scratch.Prims[index]);
    }
    return bounds;
}

//////////////////////////////////////////////////////////////////////////
// Replays primitives into a shape batch (no reset), a Card through the caller's frame output; returns the shapes added
int32 SOLHudSymbols::AddToBatch(const FSOLHudSymbolPrims& prims, FSOLHudShapeBatch& batch,
    const FSOLHudSymbolPalette& palette, const float padPx, FSOLHoloFrameOutput& cardScratch)
{
    return HudSymbolsReplay(prims, batch, palette, padPx, &cardScratch);
}

//////////////////////////////////////////////////////////////////////////
// Replays primitives into a shape batch (no reset), a Card through a frame output on the stack; returns the shapes
// added
int32 SOLHudSymbols::AddToBatch(const FSOLHudSymbolPrims& prims, FSOLHudShapeBatch& batch,
    const FSOLHudSymbolPalette& palette, const float padPx)
{
    return HudSymbolsReplay(prims, batch, palette, padPx, nullptr);
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD palette at strength 1
FSOLHudSymbolPalette SOLHudSymbols::HudPalette()
{
    FSOLHudSymbolPalette palette;
    palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Primary)] = SOL::HudPalette::PRIMARY;
    palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Secondary)] = SOL::HudPalette::SECONDARY;
    palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Good)] = SOL::HudPalette::GOOD;
    palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Hostile)] = SOL::HudPalette::HOSTILE;
    palette.Colors[static_cast<int32>(ESOLHudPaletteRole::FreeLook)] = SOL::HudPalette::FREE_LOOK;
    palette.Strength = 1.0f;
    return palette;
}

//////////////////////////////////////////////////////////////////////////
// Finds a symbol by its id (case-sensitive)
bool SOLHudSymbols::FromName(const FStringView name, ESOLHudSymbol& out)
{
    if (name.IsEmpty())
    {
        return false;
    }
    for (int32 index = 0; index < static_cast<int32>(ESOLHudSymbol::Count); ++index)
    {
        if (name.Equals(HUD_SYMBOL_NAMES[index], ESearchCase::CaseSensitive))
        {
            out = static_cast<ESOLHudSymbol>(index);
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns a symbol's id ("" for Count)
const TCHAR* SOLHudSymbols::ToName(const ESOLHudSymbol symbol)
{
    const int32 index = static_cast<int32>(symbol);
    return index < static_cast<int32>(ESOLHudSymbol::Count) ? HUD_SYMBOL_NAMES[index] : TEXT("");
}
