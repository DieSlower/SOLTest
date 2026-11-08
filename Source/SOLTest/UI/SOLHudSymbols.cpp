/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHudSymbols.h"

#include "Flight/SOLFlightMode.h"
#include "Interaction/SOLInteractMenus.h"
#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloRadial.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudShapeMath.h"
#include "UI/SOLRadarLayout.h"

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

    // The direction the TargetEdgeArrow symbol points (the same)
    const FVector2D HUD_SYMBOL_TARGET_ARROW_DIRECTION(1.0, 0.0);

    // The glossary's multi-element symbols fit one primitive list (contract 4.3)
    static_assert(SOL::Help::GLOSSARY_WARP_STREAKS <= SOL::Help::MAX_PRIMS_PER_SYMBOL, "warp streaks fit one list");
    static_assert(3 * SOL::Help::GLOSSARY_RADIAL_SECTORS <= SOL::Help::MAX_PRIMS_PER_SYMBOL,
        "radial sectors fit one list");

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
    // Appends a HUD line from a to b in a role, intensity and Canvas width (glow half width LineHalfWidthPx of it);
    // returns the primitives appended
    int32 HudSymbolsAddLine(FSOLHudSymbolPrims& out, const ESOLHudPaletteRole role, const float intensity,
        const FVector2D& a, const FVector2D& b, const float thicknessPx)
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Line;
        prim.Role = role;
        prim.Intensity = intensity;
        prim.A = FVector2f(a);
        prim.B = FVector2f(b);
        prim.ThicknessPx = thicknessPx;
        prim.HalfWidthPx = SOLHudSymbols::LineHalfWidthPx(thicknessPx);
        return out.Add(prim) ? 1 : 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a HUD ring in a role and intensity with its Canvas width, segments and glow pad override (0: the
    // replay's); returns the primitives appended
    int32 HudSymbolsAddRing(FSOLHudSymbolPrims& out, const ESOLHudPaletteRole role, const float intensity,
        const FVector2D& centre, const float radiusPx, const float thicknessPx, const int32 segments,
        const ESOLHudPrimPath path = ESOLHudPrimPath::Both, const float padPx = 0.0f)
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Ring;
        prim.Role = role;
        prim.Intensity = intensity;
        prim.A = FVector2f(centre);
        prim.RadiusPx = radiusPx;
        prim.ThicknessPx = thicknessPx;
        prim.Segments = segments;
        prim.Path = path;
        prim.PadPx = padPx;
        return out.Add(prim) ? 1 : 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a filled HUD box (top-left and size) in a role and intensity; returns the primitives appended
    int32 HudSymbolsAddBox(FSOLHudSymbolPrims& out, const ESOLHudPaletteRole role, const float intensity,
        const float x, const float y, const float width, const float height,
        const ESOLHudPrimPath path = ESOLHudPrimPath::Both)
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Box;
        prim.Role = role;
        prim.Intensity = intensity;
        prim.A = FVector2f(x, y);
        prim.B = FVector2f(width, height);
        prim.Path = path;
        return out.Add(prim) ? 1 : 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a radial menu arc (centre, radius, half width, angles, glow pad) in a role and intensity; returns the
    // primitives appended
    int32 HudSymbolsAddArc(FSOLHudSymbolPrims& out, const ESOLHudPaletteRole role, const float intensity,
        const FVector2f& centre, const float radiusPx, const float halfWidthPx, const float midAngleRad,
        const float halfApertureRad, const float padPx)
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Arc;
        prim.Role = role;
        prim.Intensity = intensity;
        prim.A = centre;
        prim.RadiusPx = radiusPx;
        prim.HalfWidthPx = halfWidthPx;
        prim.MidAngleRad = midAngleRad;
        prim.HalfApertureRad = halfApertureRad;
        prim.PadPx = padPx;
        return out.Add(prim) ? 1 : 0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Raises a glossary primitive's features below SOL::Help::GLOSSARY_MIN_FEATURE_PX to it, its centre kept (D42):
    // radii, both ellipse radii, an edge arrow's length, a box's size, a panel's half size (half the floor), a dashed
    // line's dash and gap; lines, text, cards and frames are untouched
    void HudSymbolsApplyFloor(FSOLHudSymbolPrim& prim)
    {
        const float floorPx = SOL::Help::GLOSSARY_MIN_FEATURE_PX;
        switch (prim.Kind)
        {
        case ESOLHudPrimKind::Ring:
        case ESOLHudPrimKind::Arc:
        case ESOLHudPrimKind::Dot:
        case ESOLHudPrimKind::Icon:
            prim.RadiusPx = FMath::Max(prim.RadiusPx, floorPx);
            break;
        case ESOLHudPrimKind::EdgeArrow:
            // Grown about its middle, so a centred arrow stays centred
            if (prim.RadiusPx < floorPx)
            {
                prim.A -= prim.B * (0.5f * (floorPx - prim.RadiusPx));
                prim.RadiusPx = floorPx;
            }
            break;
        case ESOLHudPrimKind::Ellipse:
            prim.RadiusPx = FMath::Max(prim.RadiusPx, floorPx);
            prim.RadiusYPx = FMath::Max(prim.RadiusYPx, floorPx);
            break;
        case ESOLHudPrimKind::Box:
            // Grown about its centre on each short axis
            if (prim.B.X < floorPx)
            {
                prim.A.X -= 0.5f * (floorPx - prim.B.X);
                prim.B.X = floorPx;
            }
            if (prim.B.Y < floorPx)
            {
                prim.A.Y -= 0.5f * (floorPx - prim.B.Y);
                prim.B.Y = floorPx;
            }
            break;
        case ESOLHudPrimKind::Panel:
            prim.B = FVector2f::Max(prim.B, FVector2f(0.5f * floorPx));
            break;
        case ESOLHudPrimKind::DashedLine:
            prim.DashPx = FMath::Max(prim.DashPx, floorPx);
            prim.GapPx = FMath::Max(prim.GapPx, floorPx);
            break;
        default:
            break;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends the glossary's radial menu: GLOSSARY_RADIAL_SECTORS sectors, the first highlighted and one disabled, in
    // the still (fully drawn and filled) look at hologram strength 1
    void HudSymbolsBuildRadialMenu(const FVector2f& centre, const float scale, FSOLHudSymbolPrims& out)
    {
        const int32 count = SOL::Help::GLOSSARY_RADIAL_SECTORS;
        const float halfAperture = static_cast<float>(UE_DOUBLE_PI / count
            - 0.5 * SOL::Interact::RADIAL_SECTOR_GAP_RAD);
        FSOLHoloMenuLook look;
        look.bVisible = true;
        look.Progress = 1.0f;
        look.FrameFraction = 1.0f;
        look.FillAlpha = 1.0f;
        for (int32 sector = 0; sector < count; ++sector)
        {
            const bool bHighlighted = sector == 0;
            const bool bEnabled = sector != SOL::Help::GLOSSARY_RADIAL_DISABLED_SECTOR;
            const FSOLHoloRadialSectorLook sectorLook = SOLHoloRadial::SectorLook(halfAperture, bHighlighted,
                bEnabled, look, 1.0f);
            SOLHudSymbols::AppendRadialSector(centre, scale,
                static_cast<float>(SOLRadialMenu::SectorMidAngleRad(sector, count)), sectorLook, bHighlighted, out);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends the hologram cursor's arrow segments, their box centred on centre, in Primary at CURSOR_INTENSITY
    void HudSymbolsBuildCursor(const FVector2f& centre, const float scale, FSOLHudSymbolPrims& out)
    {
        FVector2f starts[SOL::Overlay::CURSOR_SEGMENTS];
        FVector2f ends[SOL::Overlay::CURSOR_SEGMENTS];
        SOLHologramCursor::ArrowSegments(scale, starts, ends);
        FBox2f box(ForceInit);
        for (int32 segment = 0; segment < SOL::Overlay::CURSOR_SEGMENTS; ++segment)
        {
            box += starts[segment];
            box += ends[segment];
        }
        const FVector2f offset = centre - box.GetCenter();
        for (int32 segment = 0; segment < SOL::Overlay::CURSOR_SEGMENTS; ++segment)
        {
            FSOLHudSymbolPrim prim;
            prim.Kind = ESOLHudPrimKind::Line;
            prim.A = starts[segment] + offset;
            prim.B = ends[segment] + offset;
            prim.Intensity = SOL::Overlay::CURSOR_INTENSITY;
            out.Add(prim);
        }
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
        case ESOLHudPrimKind::Frame:
            return FBox2f(FVector2f::Min(prim.A, prim.B), FVector2f::Max(prim.A, prim.B));
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
    // Builds a Card or Frame primitive's hologram shapes into a frame output and appends them to a batch
    void HudSymbolsReplayHolo(const FSOLHudSymbolPrim& prim, FSOLHudShapeBatch& batch,
        const FSOLHudSymbolPalette& palette, FSOLHoloFrameOutput& output)
    {
        const FBox2f rect(FVector2f::Min(prim.A, prim.B), FVector2f::Max(prim.A, prim.B));
        if (prim.Kind == ESOLHudPrimKind::Card)
        {
            SOLHoloFrameBuild::BuildCard(rect, prim.UiScale, HudSymbolsCardLook(), palette.Strength, output);
        }
        else
        {
            FSOLHoloFrameSpec spec;
            spec.Rect = rect;
            spec.UiScale = prim.UiScale;
            SOLHoloFrameBuild::BuildFrame(spec, HudSymbolsCardLook(), palette.Strength, output);
        }
        SOLHoloFrameBuild::AppendToBatch(output, batch);
    }

    //////////////////////////////////////////////////////////////////////////
    // Replays primitives into a shape batch (no reset), a Card or Frame into cardScratch or, without one, a frame
    // output on the stack; a primitive's PadPx > 0 replaces the replay's pad; returns the shapes added
    int32 HudSymbolsReplay(const FSOLHudSymbolPrims& prims, FSOLHudShapeBatch& batch,
        const FSOLHudSymbolPalette& palette, const float replayPadPx, FSOLHoloFrameOutput* cardScratch)
    {
        const int32 before = batch.GetShapeCount();
        const int32 count = FMath::Clamp(prims.Count, 0, SOL::Help::MAX_PRIMS_PER_SYMBOL);
        for (int32 index = 0; index < count; ++index)
        {
            const FSOLHudSymbolPrim& prim = prims.Prims[index];
            const FLinearColor& color = HudSymbolsRoleColor(palette, prim.Role);
            const float padPx = prim.PadPx > 0.0f ? prim.PadPx : replayPadPx;
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
            case ESOLHudPrimKind::Frame:
            {
                // The card's or frame's shapes come from the hologram frame builder (a card's backing is the layer's,
                // not the batch's)
                if (cardScratch != nullptr)
                {
                    HudSymbolsReplayHolo(prim, batch, palette, *cardScratch);
                    break;
                }
                FSOLHoloFrameOutput output;
                HudSymbolsReplayHolo(prim, batch, palette, output);
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
// Writes a symbol's primitives centred on centre at a UI scale: the HUD symbols through the HUD's own Append*
// builders at the glossary's representative parameters, the symbols with an existing builder as its primitive, then
// every feature raised to the glossary floor (D42); the map symbols without a picture (D39) write nothing
int32 SOLHudSymbols::Build(const ESOLHudSymbol symbol, const FVector2f& centre, const float scale,
    FSOLHudSymbolPrims& out)
{
    out.Reset();
    if (!HudSymbolsIsValidScale(scale))
    {
        return 0;
    }
    const FVector2D c(centre);
    switch (symbol)
    {
    case ESOLHudSymbol::Reticle:
        AppendReticle(c, scale, out);
        break;
    case ESOLHudSymbol::Joystick:
    case ESOLHudSymbol::JoystickFreeLook:
    {
        // The stick circle at its 720 p size (the HUD's is JOYSTICK_RADIUS_FRACTION x min(viewport width, height)),
        // the stick out past the dead zone
        const float radius = static_cast<float>(SOL::JOYSTICK_RADIUS_FRACTION * SOL::Hologram::HUD_REFERENCE_HEIGHT_PX
            * scale);
        const FVector2D offset(SOL::Help::GLOSSARY_STICK_OFFSET_X * radius,
            SOL::Help::GLOSSARY_STICK_OFFSET_Y * radius);
        AppendJoystick(c, radius, offset, symbol == ESOLHudSymbol::JoystickFreeLook, true, scale, out);
        break;
    }
    case ESOLHudSymbol::SteerFree:
        AppendSteerIcon(c, ESOLSteerIcon::Free, true, scale, out);
        break;
    case ESOLHudSymbol::SteerHeld:
        AppendSteerIcon(c, ESOLSteerIcon::Held, true, scale, out);
        break;
    case ESOLHudSymbol::SteerLocked:
        AppendSteerIcon(c, ESOLSteerIcon::Locked, true, scale, out);
        break;
    case ESOLHudSymbol::Prograde:
        AppendPrograde(c, scale, out);
        break;
    case ESOLHudSymbol::Retrograde:
        AppendRetrograde(c, scale, out);
        break;
    case ESOLHudSymbol::TargetBracket:
        AppendTargetBracket(c, static_cast<float>(SOL::Help::GLOSSARY_BRACKET_HALF_PX * scale), out);
        break;
    case ESOLHudSymbol::TargetEdgeArrow:
    {
        // Centred: the base half a length behind the centre, the tip half a length ahead
        const FVector2D anchor = c - HUD_SYMBOL_TARGET_ARROW_DIRECTION
            * (0.5 * SOL::HudGeometry::EDGE_ARROW_LENGTH_PX * scale);
        AppendTargetEdgeArrow(anchor, HUD_SYMBOL_TARGET_ARROW_DIRECTION, scale, out);
        break;
    }
    case ESOLHudSymbol::RadarScope:
    {
        FSOLRadarScopeParams params;
        params.Center = c;
        params.UiScale = scale;
        params.TimeS = SOL::Help::GLOSSARY_RADAR_TIME_S;
        params.Strength = 1.0;
        params.bHasCone = true;
        params.ConeHeadingRad = 0.0;
        params.ConeHalfFovRad = SOL::Help::GLOSSARY_RADAR_CONE_HALF_RAD;
        AppendRadarScope(params, out);
        break;
    }
    case ESOLHudSymbol::RadarContactAbove:
    case ESOLHudSymbol::RadarContactBelow:
    case ESOLHudSymbol::RadarSelected:
    {
        // A stem centred on the centre: above the plane its blip on top, below it underneath (ContactStyle's looks)
        const double halfStem = 0.5 * SOL::Help::GLOSSARY_RADAR_STEM_PX * scale;
        const bool bBelow = symbol == ESOLHudSymbol::RadarContactBelow;
        FSOLRadarStyle style;
        style.Side = bBelow ? ESOLRadarSide::Below : ESOLRadarSide::Above;
        style.bDashedStem = bBelow;
        style.bHollowBlip = bBelow;
        style.bLarge = symbol == ESOLHudSymbol::RadarSelected;
        const FVector2D foot = bBelow ? c - FVector2D(0.0, halfStem) : c + FVector2D(0.0, halfStem);
        const FVector2D top = bBelow ? c + FVector2D(0.0, halfStem) : c - FVector2D(0.0, halfStem);
        AppendRadarContact(foot, top, style, 1.0f, scale, out);
        break;
    }
    case ESOLHudSymbol::SpeedCapBar:
    case ESOLHudSymbol::GMeter:
    {
        // The bar centred on the centre; the G meter with its Chase Down ticks
        const float x = static_cast<float>(c.X - 0.5 * SOL::HudGeometry::SPEED_BAR_WIDTH_PX * scale);
        const float y = static_cast<float>(c.Y - 0.5 * SOL::HudGeometry::SPEED_BAR_HEIGHT_PX * scale);
        const bool bGMeter = symbol == ESOLHudSymbol::GMeter;
        AppendBar(x, y, bGMeter ? SOL::Help::GLOSSARY_G_FILL : SOL::Help::GLOSSARY_BAR_FILL,
            ESOLHudPaletteRole::Primary, scale, out);
        if (bGMeter)
        {
            AppendChaseTicks(x, y, SOL::Help::GLOSSARY_CHASE_FLOOR, true, SOL::Help::GLOSSARY_CHASE_RESUME, scale,
                out);
        }
        break;
    }
    case ESOLHudSymbol::WarpStreaks:
    {
        // Streaks evenly around the centre, each at its own phase of the cycle
        const int32 count = SOL::Help::GLOSSARY_WARP_STREAKS;
        const double halfDiagonal = SOL::Help::GLOSSARY_WARP_HALF_DIAGONAL_PX * scale;
        for (int32 streak = 0; streak < count; ++streak)
        {
            const double angle = 2.0 * UE_DOUBLE_PI * streak / count;
            AppendWarpStreak(c, FVector2D(FMath::Cos(angle), FMath::Sin(angle)), (streak + 0.5) / count, 1.0,
                halfDiagonal, scale, out);
        }
        break;
    }
    case ESOLHudSymbol::RadialMenu:
        HudSymbolsBuildRadialMenu(centre, scale, out);
        break;
    case ESOLHudSymbol::HoloCursor:
        HudSymbolsBuildCursor(centre, scale, out);
        break;
    case ESOLHudSymbol::ListFrame:
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Frame;
        const FVector2f half(SOL::Help::GLOSSARY_FRAME_HALF_WIDTH_PX * scale,
            SOL::Help::GLOSSARY_FRAME_HALF_HEIGHT_PX * scale);
        prim.A = centre - half;
        prim.B = centre + half;
        prim.UiScale = scale;
        out.Add(prim);
        break;
    }
    case ESOLHudSymbol::MapBodyIcon:
    {
        // The map's body icon on the shape renderer at full LOD
        FSOLHudSymbolPrim prim = HudSymbolsIconPrim(SOLOverlayIcons::EIconCategory::Planet, centre, scale);
        prim.RadiusPx = static_cast<float>(0.5 * SOL::MAP_ICON_DIAMETER_PX * scale);
        out.Add(prim);
        break;
    }
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

    // No feature smaller than the glossary floor (only here: the HUD's builders are never floored)
    for (int32 index = 0; index < out.Count; ++index)
    {
        HudSymbolsApplyFloor(out.Prims[index]);
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
    palette.Colors[static_cast<int32>(ESOLHudPaletteRole::Bad)] = SOL::HudPalette::BAD;
    palette.Colors[static_cast<int32>(ESOLHudPaletteRole::WarpStreak)] = SOL::HudPalette::WARP_STREAK;
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

//////////////////////////////////////////////////////////////////////////
// Returns the glow half width of a HUD line of a Canvas width (as ASOLFlightHud::GlowLine draws it)
float SOLHudSymbols::LineHalfWidthPx(const float thicknessPx)
{
    return FMath::Max(SOL::Hologram::HUD_LINE_HALF_WIDTH_PX, 0.5f * (thicknessPx - 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Returns a radar blip's size at a UI scale
float SOLHudSymbols::RadarBlipPx(const bool bLarge, const float uiScale)
{
    return (bLarge ? SOL::HudGeometry::RADAR_SELECTED_BLIP_PX : SOL::HudGeometry::RADAR_BLIP_PX) * uiScale;
}

//////////////////////////////////////////////////////////////////////////
// Appends the centre reticle: four arms from the gap out, then the dimmer dot ring the Canvas fallback skips (always
// with the HUD glow pad, as the HUD drew it before part 33d-i)
int32 SOLHudSymbols::AppendReticle(const FVector2D& centre, const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole primary = ESOLHudPaletteRole::Primary;
    const float gap = RETICLE_GAP_PX * scale;
    const float arm = gap + RETICLE_ARM_PX * scale;
    int32 added = HudSymbolsAddLine(out, primary, 1.0f, FVector2D(centre.X - arm, centre.Y),
        FVector2D(centre.X - gap, centre.Y), CANVAS_LINE_PX);
    added += HudSymbolsAddLine(out, primary, 1.0f, FVector2D(centre.X + gap, centre.Y),
        FVector2D(centre.X + arm, centre.Y), CANVAS_LINE_PX);
    added += HudSymbolsAddLine(out, primary, 1.0f, FVector2D(centre.X, centre.Y - arm),
        FVector2D(centre.X, centre.Y - gap), CANVAS_LINE_PX);
    added += HudSymbolsAddLine(out, primary, 1.0f, FVector2D(centre.X, centre.Y + gap),
        FVector2D(centre.X, centre.Y + arm), CANVAS_LINE_PX);
    added += HudSymbolsAddRing(out, primary, RETICLE_DOT_INTENSITY, centre, RETICLE_DOT_RADIUS_PX * scale,
        CANVAS_LINE_THIN_PX, 0, ESOLHudPrimPath::ShapeOnly, SOL::Hologram::HUD_GLOW_PAD_PX);
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends the virtual joystick: circle, dead zone, the line to the stick past the dead zone, the stick dot
int32 SOLHudSymbols::AppendJoystick(const FVector2D& centre, const float radiusPx, const FVector2D& stickOffsetPx,
    const bool bFreeLooking, const bool bSteering, const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole role = bFreeLooking ? ESOLHudPaletteRole::FreeLook : ESOLHudPaletteRole::Primary;
    int32 added = HudSymbolsAddRing(out, role, FAINT_INTENSITY, centre, radiusPx, CANVAS_LINE_THIN_PX,
        CIRCLE_SEGMENTS);
    added += HudSymbolsAddRing(out, role, FAINT_INTENSITY, centre,
        radiusPx * static_cast<float>(SOL::JOYSTICK_DEAD_ZONE), CANVAS_LINE_THIN_PX, CIRCLE_SEGMENTS / 2);

    // The stick offset is X right, Y up; the screen's Y grows downward
    const FVector2D stick(centre.X + stickOffsetPx.X, centre.Y - stickOffsetPx.Y);
    const float intensity = (bFreeLooking || !bSteering ? SOL::HudPalette::DIM_INTENSITY : 1.0f)
        * STICK_DOT_INTENSITY;
    if (stickOffsetPx.Size() > radiusPx * SOL::JOYSTICK_DEAD_ZONE)
    {
        added += HudSymbolsAddLine(out, role, SOL::HudPalette::DIM_INTENSITY, centre, stick, CANVAS_LINE_THIN_PX);
    }
    const float dot = STICK_DOT_PX * scale;
    added += HudSymbolsAddBox(out, role, intensity, static_cast<float>(stick.X - dot * 0.5f),
        static_cast<float>(stick.Y - dot * 0.5f), dot, dot);
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends the mouse-steer icon: the open arrow (Free) or the reticle (Held), with the lock tick (Locked)
int32 SOLHudSymbols::AppendSteerIcon(const FVector2D& centre, const ESOLSteerIcon icon, const bool bSteering,
    const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole primary = ESOLHudPaletteRole::Primary;
    const float radius = STEER_ICON_RADIUS_PX * scale;
    const float intensity = bSteering ? 1.0f : SOL::HudPalette::DIM_INTENSITY;
    if (icon == ESOLSteerIcon::Free)
    {
        // Cursor arrow: a tip at the top left, a vertical edge and a slanted edge closed by a short base
        const FVector2D tip(centre.X - radius * STEER_ARROW_TIP_X_FRACTION, centre.Y - radius);
        const FVector2D bottom(tip.X, tip.Y + radius * STEER_ARROW_HEIGHT_FRACTION);
        const FVector2D side(tip.X + radius * STEER_ARROW_SIDE_FRACTION, tip.Y + radius * STEER_ARROW_SIDE_FRACTION);
        int32 added = HudSymbolsAddLine(out, primary, intensity, tip, bottom, CANVAS_LINE_PX);
        added += HudSymbolsAddLine(out, primary, intensity, tip, side, CANVAS_LINE_PX);
        added += HudSymbolsAddLine(out, primary, intensity, bottom, side, CANVAS_LINE_PX);
        return added;
    }

    // Held and Locked: a reticle (circle, unscaled centre dot), Locked with a small filled square on its lower right
    int32 added = HudSymbolsAddRing(out, primary, intensity, centre, radius, CANVAS_LINE_PX, STEER_ICON_SEGMENTS);
    added += HudSymbolsAddBox(out, primary, intensity * STICK_DOT_INTENSITY,
        static_cast<float>(centre.X - 0.5f * STEER_DOT_PX), static_cast<float>(centre.Y - 0.5f * STEER_DOT_PX),
        STEER_DOT_PX, STEER_DOT_PX);
    if (icon == ESOLSteerIcon::Locked)
    {
        const float tick = radius * STEER_LOCK_TICK_FRACTION;
        added += HudSymbolsAddBox(out, primary, intensity,
            static_cast<float>(centre.X + radius * STEER_LOCK_OFFSET_FRACTION),
            static_cast<float>(centre.Y + radius * STEER_LOCK_OFFSET_FRACTION), tick, tick);
    }
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends the prograde marker: a Good circle with ticks left, right and up
int32 SOLHudSymbols::AppendPrograde(const FVector2D& point, const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole good = ESOLHudPaletteRole::Good;
    const float radius = VELOCITY_MARKER_RADIUS_PX * scale;
    int32 added = HudSymbolsAddRing(out, good, 1.0f, point, radius, CANVAS_LINE_PX, VELOCITY_MARKER_SEGMENTS);
    added += HudSymbolsAddLine(out, good, 1.0f, FVector2D(point.X - radius, point.Y),
        FVector2D(point.X - PROGRADE_TICK_END_FRACTION * radius, point.Y), CANVAS_LINE_PX);
    added += HudSymbolsAddLine(out, good, 1.0f, FVector2D(point.X + radius, point.Y),
        FVector2D(point.X + PROGRADE_TICK_END_FRACTION * radius, point.Y), CANVAS_LINE_PX);
    added += HudSymbolsAddLine(out, good, 1.0f, FVector2D(point.X, point.Y - radius),
        FVector2D(point.X, point.Y - PROGRADE_TICK_END_FRACTION * radius), CANVAS_LINE_PX);
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends the retrograde marker: a Hostile circle with a cross
int32 SOLHudSymbols::AppendRetrograde(const FVector2D& point, const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole hostile = ESOLHudPaletteRole::Hostile;
    const float radius = VELOCITY_MARKER_RADIUS_PX * scale;
    const float arm = radius * RETROGRADE_ARM_FRACTION;
    int32 added = HudSymbolsAddRing(out, hostile, 1.0f, point, radius, CANVAS_LINE_PX, VELOCITY_MARKER_SEGMENTS);
    added += HudSymbolsAddLine(out, hostile, 1.0f, FVector2D(point.X - arm, point.Y - arm),
        FVector2D(point.X + arm, point.Y + arm), CANVAS_LINE_PX);
    added += HudSymbolsAddLine(out, hostile, 1.0f, FVector2D(point.X - arm, point.Y + arm),
        FVector2D(point.X + arm, point.Y - arm), CANVAS_LINE_PX);
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends the target bracket: per corner (-,-), (+,-), (-,+), (+,+) a horizontal then a vertical leg from its tip
int32 SOLHudSymbols::AppendTargetBracket(const FVector2D& point, const float halfPx, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole secondary = ESOLHudPaletteRole::Secondary;
    const float corner = halfPx * BRACKET_CORNER_FRACTION;
    int32 added = 0;
    for (int32 cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
        const float sx = (cornerIndex & 1) ? 1.0f : -1.0f;
        const float sy = (cornerIndex & 2) ? 1.0f : -1.0f;
        const FVector2D tip(point.X + sx * halfPx, point.Y + sy * halfPx);
        added += HudSymbolsAddLine(out, secondary, BRACKET_INTENSITY, tip, FVector2D(tip.X - sx * corner, tip.Y),
            CANVAS_LINE_PX);
        added += HudSymbolsAddLine(out, secondary, BRACKET_INTENSITY, tip, FVector2D(tip.X, tip.Y - sy * corner),
            CANVAS_LINE_PX);
    }
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends the off-screen target arrow: tip to left, tip to right, left to right
int32 SOLHudSymbols::AppendTargetEdgeArrow(const FVector2D& anchor, const FVector2D& direction, const float scale,
    FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole secondary = ESOLHudPaletteRole::Secondary;
    const FVector2D perpendicular(-direction.Y, direction.X);
    const FVector2D tip = anchor + direction * (EDGE_ARROW_LENGTH_PX * scale);
    const FVector2D left = anchor + perpendicular * (EDGE_ARROW_WIDTH_PX * scale);
    const FVector2D right = anchor - perpendicular * (EDGE_ARROW_WIDTH_PX * scale);
    int32 added = HudSymbolsAddLine(out, secondary, BRACKET_INTENSITY, tip, left, CANVAS_LINE_THICK_PX);
    added += HudSymbolsAddLine(out, secondary, BRACKET_INTENSITY, tip, right, CANVAS_LINE_THICK_PX);
    added += HudSymbolsAddLine(out, secondary, BRACKET_INTENSITY, left, right, CANVAS_LINE_THICK_PX);
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends the radar scope: sonar range ellipses (the outer one brighter), the axes, the side and aft ticks, the
// heading tick, the view cone's edges (left first) and the ship chevron
int32 SOLHudSymbols::AppendRadarScope(const FSOLRadarScopeParams& in, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole primary = ESOLHudPaletteRole::Primary;
    const FVector2D& center = in.Center;
    const float radius = RADAR_RADIUS_PX * in.UiScale;
    const float squashedRadius = radius * RADAR_TILT;
    int32 added = 0;

    // Range rings with LT's sonar pulse travelling outward
    for (const float ring : RADAR_RINGS)
    {
        const float gain = ring >= 1.0f ? RADAR_OUTER_RING_GAIN : 1.0f;
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Ellipse;
        prim.Role = primary;
        prim.Intensity = gain * static_cast<float>(SOLRadar::SonarRingIntensity(ring, in.TimeS, in.Strength));
        prim.A = FVector2f(center);
        prim.RadiusPx = radius * ring;
        prim.SquashY = RADAR_TILT;
        prim.RadiusYPx = prim.RadiusPx * prim.SquashY;
        added += out.Add(prim) ? 1 : 0;
    }

    // Fore-aft and side axes, the side and aft ticks outside the ring and the longer heading tick ahead
    added += HudSymbolsAddLine(out, primary, RADAR_AXIS_INTENSITY,
        SOLRadar::ScopeToScreen(center, radius, RADAR_TILT, FVector2D(-1.0, 0.0)),
        SOLRadar::ScopeToScreen(center, radius, RADAR_TILT, FVector2D(1.0, 0.0)), CANVAS_LINE_THIN_PX);
    added += HudSymbolsAddLine(out, primary, RADAR_AXIS_INTENSITY,
        SOLRadar::ScopeToScreen(center, radius, RADAR_TILT, FVector2D(0.0, -1.0)),
        SOLRadar::ScopeToScreen(center, radius, RADAR_TILT, FVector2D(0.0, 1.0)), CANVAS_LINE_THIN_PX);
    const float tick = RADAR_TICK_PX * in.UiScale;
    const float dim = SOL::HudPalette::DIM_INTENSITY;
    added += HudSymbolsAddLine(out, primary, dim, FVector2D(center.X - radius - tick, center.Y),
        FVector2D(center.X - radius, center.Y), CANVAS_LINE_THIN_PX);
    added += HudSymbolsAddLine(out, primary, dim, FVector2D(center.X + radius, center.Y),
        FVector2D(center.X + radius + tick, center.Y), CANVAS_LINE_THIN_PX);
    added += HudSymbolsAddLine(out, primary, dim, FVector2D(center.X, center.Y + squashedRadius),
        FVector2D(center.X, center.Y + squashedRadius + tick), CANVAS_LINE_THIN_PX);
    added += HudSymbolsAddLine(out, primary, 1.0f,
        FVector2D(center.X, center.Y - squashedRadius - RADAR_HEADING_TICK_PX * in.UiScale),
        FVector2D(center.X, center.Y - squashedRadius), CANVAS_LINE_PX);

    // View cone: the camera's horizontal field of view around its heading on the plane
    if (in.bHasCone)
    {
        for (const double side : { -1.0, 1.0 })
        {
            added += HudSymbolsAddLine(out, primary, RADAR_CONE_INTENSITY, center,
                SOLRadar::ScopeToScreen(center, radius, RADAR_TILT,
                SOLRadar::AzimuthToScope(in.ConeHeadingRad + side * in.ConeHalfFovRad)), CANVAS_LINE_THIN_PX);
        }
    }

    // The ship: a small chevron at the centre pointing forward
    const float chevron = RADAR_CHEVRON_PX * in.UiScale;
    added += HudSymbolsAddLine(out, primary, RADAR_CHEVRON_INTENSITY,
        FVector2D(center.X - chevron, center.Y + RADAR_CHEVRON_RISE_FRACTION * chevron),
        FVector2D(center.X, center.Y - RADAR_CHEVRON_RISE_FRACTION * chevron), CANVAS_LINE_THIN_PX);
    added += HudSymbolsAddLine(out, primary, RADAR_CHEVRON_INTENSITY,
        FVector2D(center.X, center.Y - RADAR_CHEVRON_RISE_FRACTION * chevron),
        FVector2D(center.X + chevron, center.Y + RADAR_CHEVRON_RISE_FRACTION * chevron), CANVAS_LINE_THIN_PX);
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends one radar contact: the foot tick, the solid or dashed stem, the dot or ring blip, and the selected target's
// highlight ring (its glow not dimmed with the contact)
int32 SOLHudSymbols::AppendRadarContact(const FVector2D& foot, const FVector2D& top, const FSOLRadarStyle& style,
    const float intensity, const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole role = style.Side == ESOLRadarSide::Above ? ESOLHudPaletteRole::Primary
        : ESOLHudPaletteRole::Secondary;
    const float footHalf = RADAR_FOOT_HALF_PX * scale;
    int32 added = HudSymbolsAddLine(out, role, RADAR_FOOT_INTENSITY * intensity, FVector2D(foot.X - footHalf, foot.Y),
        FVector2D(foot.X + footHalf, foot.Y), CANVAS_LINE_THIN_PX);
    if (style.bDashedStem)
    {
        // Dashes from the plane down to the blip
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::DashedLine;
        prim.Role = role;
        prim.Intensity = RADAR_STEM_INTENSITY * intensity;
        prim.A = FVector2f(foot);
        prim.B = FVector2f(top);
        prim.DashPx = static_cast<float>(SOLRadar::StemDashPx * scale);
        prim.GapPx = static_cast<float>(SOLRadar::StemGapPx * scale);
        added += out.Add(prim) ? 1 : 0;
    }
    else
    {
        added += HudSymbolsAddLine(out, role, RADAR_STEM_INTENSITY * intensity, foot, top, CANVAS_LINE_THIN_PX);
    }

    const float blip = RadarBlipPx(style.bLarge, scale);
    const float blipRing = style.bHollowBlip ? blip : 0.5f * blip;   // The blip's outer radius
    if (style.bHollowBlip)
    {
        added += HudSymbolsAddRing(out, role, RADAR_BLIP_INTENSITY * intensity, top, blipRing, CANVAS_LINE_THIN_PX,
            RADAR_CANVAS_SEGMENTS);
    }
    else
    {
        FSOLHudSymbolPrim prim;
        prim.Kind = ESOLHudPrimKind::Dot;
        prim.Role = role;
        prim.Intensity = RADAR_BLIP_INTENSITY * intensity;
        prim.A = FVector2f(top);
        prim.RadiusPx = 0.5f * blip;
        added += out.Add(prim) ? 1 : 0;
    }
    if (style.bLarge)
    {
        added += HudSymbolsAddRing(out, role, RADAR_STEM_INTENSITY, top, blipRing + RADAR_HIGHLIGHT_GAP_PX * scale,
            CANVAS_LINE_THIN_PX, RADAR_CANVAS_SEGMENTS);
    }
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends a fill bar: the track (shape renderer only; the Canvas fallback draws its own back), the fill, the end tick
int32 SOLHudSymbols::AppendBar(const float x, const float y, const double fill01, const ESOLHudPaletteRole fillRole,
    const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const float width = SPEED_BAR_WIDTH_PX * scale;
    const float height = SPEED_BAR_HEIGHT_PX * scale;
    const float filled = width * static_cast<float>(FMath::Clamp(fill01, 0.0, 1.0));
    int32 added = HudSymbolsAddBox(out, ESOLHudPaletteRole::Primary, BAR_TRACK_INTENSITY, x, y, width, height,
        ESOLHudPrimPath::ShapeOnly);
    added += HudSymbolsAddBox(out, fillRole, BAR_FILL_INTENSITY, x, y, filled, height);
    added += HudSymbolsAddLine(out, ESOLHudPaletteRole::Primary, SOL::HudPalette::DIM_INTENSITY,
        FVector2D(x + width, y), FVector2D(x + width, y + height), CANVAS_LINE_THIN_PX);
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends the Chase Down ticks: the floor tick past the bar's edges, the dim resume tick when shown
int32 SOLHudSymbols::AppendChaseTicks(const float x, const float y, const double floorMeter, const bool bShowResume,
    const double resumeMeter, const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const ESOLHudPaletteRole secondary = ESOLHudPaletteRole::Secondary;
    const float barWidth = SPEED_BAR_WIDTH_PX * scale;
    const float barHeight = SPEED_BAR_HEIGHT_PX * scale;
    const float floorX = x + barWidth * static_cast<float>(FMath::Clamp(floorMeter, 0.0, 1.0));
    int32 added = HudSymbolsAddLine(out, secondary, 1.0f, FVector2D(floorX, y - CHASE_TICK_OVERHANG_PX * scale),
        FVector2D(floorX, y + barHeight + CHASE_TICK_OVERHANG_PX * scale), CANVAS_LINE_THICK_PX);
    if (bShowResume)
    {
        const float resumeX = x + barWidth * static_cast<float>(FMath::Clamp(resumeMeter, 0.0, 1.0));
        added += HudSymbolsAddLine(out, secondary, SOL::HudPalette::DIM_INTENSITY, FVector2D(resumeX, y),
            FVector2D(resumeX, y + barHeight), CANVAS_LINE_THIN_PX);
    }
    return added;
}

//////////////////////////////////////////////////////////////////////////
// Appends one warp streak: travelling out (squared phase), lengthening, brightening and thickening with its phase
int32 SOLHudSymbols::AppendWarpStreak(const FVector2D& centre, const FVector2D& direction, const double phase,
    const double intensity, const double halfDiagonalPx, const float scale, FSOLHudSymbolPrims& out)
{
    using namespace SOL::HudGeometry;
    const double innerPx = halfDiagonalPx * (WARP_STREAK_START_FRACTION + WARP_STREAK_TRAVEL_FRACTION * phase * phase);
    const double lengthPx = halfDiagonalPx * intensity * FMath::Lerp(WARP_STREAK_MIN_LENGTH_FRACTION,
        WARP_STREAK_MAX_LENGTH_FRACTION, phase);
    const float brightness = static_cast<float>(intensity * (WARP_STREAK_BASE_BRIGHTNESS
        + WARP_STREAK_PHASE_BRIGHTNESS * phase));
    return HudSymbolsAddLine(out, ESOLHudPaletteRole::WarpStreak, brightness, centre + direction * innerPx,
        centre + direction * (innerPx + lengthPx),
        (1.0f + WARP_STREAK_EXTRA_THICKNESS_PX * static_cast<float>(phase)) * scale);
}

//////////////////////////////////////////////////////////////////////////
// Appends one radial menu sector: the fill arc (when filled), then the inner and outer rim arcs, each with the
// sector's pad
int32 SOLHudSymbols::AppendRadialSector(const FVector2f& centre, const float scale, const float midAngleRad,
    const FSOLHoloRadialSectorLook& sector, const bool bHighlighted, FSOLHudSymbolPrims& out)
{
    if (sector.HalfApertureRad <= 0.0f)
    {
        return 0;
    }
    const ESOLHudPaletteRole role = bHighlighted ? ESOLHudPaletteRole::Secondary : ESOLHudPaletteRole::Primary;
    const float innerPx = SOL::Interact::RADIAL_INNER_PX * scale;
    const float outerPx = SOL::Interact::RADIAL_OUTER_PX * scale;
    const float edgePx = SOL::Interact::RADIAL_EDGE_HALF_WIDTH_PX;
    int32 added = 0;
    if (sector.FillIntensity > 0.0f)
    {
        added += HudSymbolsAddArc(out, role, sector.FillIntensity, centre, 0.5f * (innerPx + outerPx),
            0.5f * (outerPx - innerPx), midAngleRad, sector.HalfApertureRad, sector.PadPx);
    }
    added += HudSymbolsAddArc(out, role, sector.EdgeIntensity, centre, innerPx, edgePx, midAngleRad,
        sector.HalfApertureRad, sector.PadPx);
    added += HudSymbolsAddArc(out, role, sector.EdgeIntensity, centre, outerPx, edgePx, midAngleRad,
        sector.HalfApertureRad, sector.PadPx);
    return added;
}
