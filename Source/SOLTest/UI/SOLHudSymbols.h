/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"
#include "UI/SOLOverlayIcons.h"

#include "Containers/StringView.h"
#include "CoreMinimal.h"

enum class ESOLSteerIcon : uint8;
struct FSOLHoloFrameOutput;
struct FSOLHoloRadialSectorLook;
struct FSOLHudShapeBatch;
struct FSOLRadarStyle;

// Every symbol the HUD glossary draws (SDD 33 section 8.1), each one a HUD element's real geometry. The ids of
// SOLMenuText::GLOSSARY_ENTRIES' Symbols are these names (SOLHudSymbols::FromName / ToName)
enum class ESOLHudSymbol : uint8
{
    Reticle,
    Joystick,
    JoystickFreeLook,
    SteerFree,
    SteerHeld,
    SteerLocked,
    Prograde,
    Retrograde,
    TargetBracket,
    TargetEdgeArrow,
    IconStar,
    IconPlanet,
    IconMoon,
    IconDwarf,
    IconAsteroid,
    IconRing,
    IconShip,
    IconTarget,
    IconEdgeArrow,
    RadarScope,
    RadarContactAbove,
    RadarContactBelow,
    RadarSelected,
    GMeter,
    SpeedCapBar,
    Card,
    OrbitLine,
    RouteLeg,
    RouteLoopLeg,
    WaypointAbove,
    WaypointBelow,
    MapBodyIcon,
    MapPick,
    MapGridStem,
    RadialMenu,
    ListFrame,
    HoloCursor,
    WarpStreaks,
    Count,
};

// The kind of one symbol primitive
enum class ESOLHudPrimKind : uint8
{
    Line,           // A to B, HalfWidthPx (0: SOL::Hologram::HUD_LINE_HALF_WIDTH_PX); ASOLFlightHud::GlowLine
    Ring,           // Centre A, RadiusPx; GlowRing
    Arc,            // Centre A, RadiusPx, MidAngleRad, HalfApertureRad (FSOLHudShapeBatch::AddArc)
    Box,            // A filled glow box: top-left A, size B; GlowBox
    Dot,            // A filled dot: centre A, RadiusPx; GlowDot
    DashedLine,     // A to B, DashPx and GapPx (the shape batch's period is their sum); GlowDashedLine
    Ellipse,        // Centre A, RadiusPx (X), RadiusYPx; GlowEllipse
    Panel,          // A borderless panel: centre A, half size B, Fill (FSOLHudShapeBatch::AddPanel)
    Icon,           // An overlay icon: centre A, RadiusPx, Category (SOLOverlayIcons::AddIconShapes)
    EdgeArrow,      // An edge arrow: base A, unit direction B, length RadiusPx (SOLOverlayIcons::AddEdgeArrow)
    Card,           // A hologram card around the content rect A (min) to B (max) at UiScale (SOLHoloFrameBuild::
                    // BuildCard, fully drawn in with its fill, no sweep; the palette's Strength)
    Frame,          // A hologram list frame, rect A (min) to B (max) at UiScale (SOLHoloFrameBuild::BuildFrame, the
                    // same still look as Card)
    Text,           // A glow text sample at A (top-left): Text, drawn through the layer's text list, not the batch
};

// The HUD palette role of a primitive (its colour comes from the palette at replay). A dim element is any role at
// Intensity SOL::HudPalette::DIM_INTENSITY, as the HUD draws it
enum class ESOLHudPaletteRole : uint8
{
    Primary,        // SOL::HudPalette::PRIMARY
    Secondary,      // SECONDARY
    Good,           // GOOD
    Hostile,        // HOSTILE
    FreeLook,       // FREE_LOOK
    Bad,            // BAD (the G meter's alarm)
    WarpStreak,     // WARP_STREAK
    Count,
};

// Which renderers draw a primitive: Both, or ShapeOnly (the shape renderer and the menus; the HUD's Canvas fallback
// skips it)
enum class ESOLHudPrimPath : uint8
{
    Both,
    ShapeOnly,
};

// One primitive of a symbol, in canvas pixels at the scale it was built for (see ESOLHudPrimKind for which fields
// each kind reads)
struct FSOLHudSymbolPrim
{
    ESOLHudPrimKind Kind = ESOLHudPrimKind::Line;
    ESOLHudPaletteRole Role = ESOLHudPaletteRole::Primary;
    FVector2f A = FVector2f::ZeroVector;
    FVector2f B = FVector2f::ZeroVector;
    float RadiusPx = 0.0f;
    float RadiusYPx = 0.0f;
    float HalfWidthPx = 0.0f;
    float MidAngleRad = 0.0f;
    float HalfApertureRad = 0.0f;
    float DashPx = 0.0f;            // DashedLine: a dash's length (the Canvas fallback draws dashes of it)
    float GapPx = 0.0f;             // DashedLine: the gap between dashes
    float Fill = 0.0f;
    float Intensity = 1.0f;         // The glow factor (SOL::HudPalette::DIM_INTENSITY for a dim element)
    float UiScale = 1.0f;           // Card / Frame: the UI scale its padding and ticks are built at
    float ThicknessPx = 1.0f;       // Line: the Canvas width (its glow half width is LineHalfWidthPx of it); Ring: the
                                    // Canvas width
    int32 Segments = 0;             // Ring: the Canvas fallback's segment count
    float SquashY = 1.0f;           // Ellipse: the Canvas squash (RadiusYPx = RadiusPx x SquashY)
    float PadPx = 0.0f;             // Glow pad override (0: the replay's pad)
    SOLOverlayIcons::EIconCategory Category = SOLOverlayIcons::EIconCategory::Count;
    ESOLHudPrimPath Path = ESOLHudPrimPath::Both;
    const TCHAR* Text = nullptr;
};

// A symbol's primitives (fixed capacity SOL::Help::MAX_PRIMS_PER_SYMBOL, no heap); Add drops a primitive past it. About
// 2.8 KB (32 x 88 B): hold one as a member (or a long-lived scratch) and Reset() it, never build one per call in a hot
// path
struct FSOLHudSymbolPrims
{
    FSOLHudSymbolPrim Prims[SOL::Help::MAX_PRIMS_PER_SYMBOL];
    int32 Count = 0;
    int32 DroppedCount = 0;

    // Empties the list
    void Reset()
    {
        Count = 0;
        DroppedCount = 0;
    }

    // Appends a primitive; false (and counted as dropped) when the list is full
    bool Add(const FSOLHudSymbolPrim& prim)
    {
        if (Count >= SOL::Help::MAX_PRIMS_PER_SYMBOL)
        {
            ++DroppedCount;
            return false;
        }
        Prims[Count++] = prim;
        return true;
    }
};
static_assert(sizeof(FSOLHudSymbolPrims) <= 3072,
    "FSOLHudSymbolPrims outgrew 3 KB: revisit SOL::Help::MAX_PRIMS_PER_SYMBOL or FSOLHudSymbolPrim's fields");

// The colours a replay uses, one per palette role, plus the hologram strength the cards are built at
struct FSOLHudSymbolPalette
{
    FLinearColor Colors[static_cast<int32>(ESOLHudPaletteRole::Count)];
    float Strength = 1.0f;

    // Starts with every colour transparent
    FSOLHudSymbolPalette()
    {
        for (FLinearColor& color : Colors)
        {
            color = FLinearColor::Transparent;
        }
    }
};

// The radar scope's inputs (AppendRadarScope): its centre, the UI scale, the sonar pulse's time and strength, and the
// view cone (heading and half field of view, radians) when it has one
struct FSOLRadarScopeParams
{
    FVector2D Center = FVector2D::ZeroVector;
    float UiScale = 1.0f;
    double TimeS = 0.0;
    double Strength = 1.0;
    bool bHasCone = false;
    double ConeHeadingRad = 0.0;
    double ConeHalfFovRad = 0.0;
};

/**
 * The HUD symbol helper (SDD 33 section 8.1, D5), pure and test-first: one source of geometry for each HUD symbol,
 * written as primitives at a centre and a UI scale. The flight HUD replays them through its Glow* calls (part 33d-i),
 * the menus' glossary through AddToBatch into an FSOLHudShapeBatch. Symbols with an existing builder (the overlay
 * icons and edge arrows, the cards) are Icon / EdgeArrow / Card primitives that AddToBatch hands to that builder, so
 * no geometry is copied. No heap, no Slate.
 *
 * Part 33d-i: the Append* builders are the flight HUD's own per-element geometry (the HUD replays them each frame, so
 * they reproduce its expressions exactly and never apply the glossary floor); each appends to out (no reset) and
 * returns the primitives it appended (drops are counted in out.DroppedCount). Build draws the glossary picture of a
 * symbol through them at representative parameters, raising every radius, size and dash below
 * SOL::Help::GLOSSARY_MIN_FEATURE_PX to it (D42; lines, text, cards and frames exempt).
 */
namespace SOLHudSymbols
{
    // Writes a symbol's primitives centred on centre at a UI scale into out (Reset first), every feature raised to the
    // glossary floor; returns out.Count (0 for Count, a symbol without a picture, or a scale that is not a positive
    // finite number)
    SOLTEST_API int32 Build(ESOLHudSymbol symbol, const FVector2f& centre, float scale, FSOLHudSymbolPrims& out);

    // Returns a symbol's bounds relative to its centre at a UI scale (linear in the scale; it contains every primitive
    // Build writes, glow pad excluded); an empty box (FBox2f(ForceInit)) for Count or an invalid scale. scratch is
    // overwritten (Build's output), so no primitive list is made per call
    SOLTEST_API FBox2f Bounds(ESOLHudSymbol symbol, float scale, FSOLHudSymbolPrims& scratch);

    // Replays the primitives into a batch (no reset) in the palette's colours with a glow pad (a primitive's PadPx > 0
    // replaces it; Path is ignored, the menus draw both): one shape per Line ... Panel primitive, what the builder adds
    // for Icon / EdgeArrow / Card / Frame, none for Text; returns the shapes added. A Card or Frame primitive is built
    // into cardScratch (overwritten; about 8 KB, so hold one next to the batch)
    SOLTEST_API int32 AddToBatch(const FSOLHudSymbolPrims& prims, FSOLHudShapeBatch& batch,
        const FSOLHudSymbolPalette& palette, float padPx, FSOLHoloFrameOutput& cardScratch);

    // AddToBatch for a one-off replay (tests, tools): a Card or Frame primitive is built into a frame output on the
    // stack
    SOLTEST_API int32 AddToBatch(const FSOLHudSymbolPrims& prims, FSOLHudShapeBatch& batch,
        const FSOLHudSymbolPalette& palette, float padPx);

    // Returns the HUD palette (SOL::HudPalette, one colour per role) at strength 1
    SOLTEST_API FSOLHudSymbolPalette HudPalette();

    // Finds a symbol by its id (case-sensitive, as ToName writes it); false and out unchanged for an unknown id
    SOLTEST_API bool FromName(FStringView name, ESOLHudSymbol& out);

    // Returns a symbol's id ("Reticle", "SteerFree"; "" for Count)
    SOLTEST_API const TCHAR* ToName(ESOLHudSymbol symbol);

    // Returns the glow half width of a HUD line of a Canvas width: LT's 1 px line up to 2 px, half the width past 1 px
    // beyond that
    SOLTEST_API float LineHalfWidthPx(float thicknessPx);

    // Returns a radar blip's size (the selected target's is larger) at a UI scale
    SOLTEST_API float RadarBlipPx(bool bLarge, float uiScale);

    // Appends the centre reticle at a UI scale: four arms and a ShapeOnly dot ring with the HUD glow pad (5)
    SOLTEST_API int32 AppendReticle(const FVector2D& centre, float scale, FSOLHudSymbolPrims& out);

    // Appends the virtual joystick: its circle of radiusPx, the dead zone, a dim line to the stick once past the dead
    // zone, and the stick dot (stickOffsetPx is X right, Y up), all in FreeLook while free-looking (3 or 4)
    SOLTEST_API int32 AppendJoystick(const FVector2D& centre, float radiusPx, const FVector2D& stickOffsetPx,
        bool bFreeLooking, bool bSteering, float scale, FSOLHudSymbolPrims& out);

    // Appends the mouse-steer icon: the Free arrow (3), the Held reticle (2) or the Locked one with its tick (3), dim
    // while not steering
    SOLTEST_API int32 AppendSteerIcon(const FVector2D& centre, ESOLSteerIcon icon, bool bSteering, float scale,
        FSOLHudSymbolPrims& out);

    // Appends the prograde marker at a screen point: a Good ring with three ticks (4)
    SOLTEST_API int32 AppendPrograde(const FVector2D& point, float scale, FSOLHudSymbolPrims& out);

    // Appends the retrograde marker at a screen point: a Hostile ring with a cross (3)
    SOLTEST_API int32 AppendRetrograde(const FVector2D& point, float scale, FSOLHudSymbolPrims& out);

    // Appends the target bracket around a screen point at a half size: two legs per corner (8)
    SOLTEST_API int32 AppendTargetBracket(const FVector2D& point, float halfPx, FSOLHudSymbolPrims& out);

    // Appends the off-screen target arrow: a triangle from its base at anchor toward a unit direction (3)
    SOLTEST_API int32 AppendTargetEdgeArrow(const FVector2D& anchor, const FVector2D& direction, float scale,
        FSOLHudSymbolPrims& out);

    // Appends the radar scope: range ellipses, axes, ticks, the view cone's edges when it has one, the ship chevron
    // (12 or 14)
    SOLTEST_API int32 AppendRadarScope(const FSOLRadarScopeParams& in, FSOLHudSymbolPrims& out);

    // Appends one radar contact from its foot on the plane to its blip at top in a style, its glows times intensity
    // (the selected target's highlight ring excepted): foot tick, stem, blip and the highlight (3 or 4)
    SOLTEST_API int32 AppendRadarContact(const FVector2D& foot, const FVector2D& top, const FSOLRadarStyle& style,
        float intensity, float scale, FSOLHudSymbolPrims& out);

    // Appends a fill bar at its top-left: a ShapeOnly track, the fill (clamped to [0, 1]) in a role, the end tick (3)
    SOLTEST_API int32 AppendBar(float x, float y, double fill01, ESOLHudPaletteRole fillRole, float scale,
        FSOLHudSymbolPrims& out);

    // Appends the Chase Down ticks on a bar at its top-left: the floor tick, and the dim resume tick when shown
    // (1 or 2)
    SOLTEST_API int32 AppendChaseTicks(float x, float y, double floorMeter, bool bShowResume, double resumeMeter,
        float scale, FSOLHudSymbolPrims& out);

    // Appends one warp streak from a centre along a unit direction at a cycle phase and intensity, for a half
    // diagonal (1)
    SOLTEST_API int32 AppendWarpStreak(const FVector2D& centre, const FVector2D& direction, double phase,
        double intensity, double halfDiagonalPx, float scale, FSOLHudSymbolPrims& out);

    // Appends one radial menu sector around a centre at its middle angle in a look: the fill arc when it has a fill,
    // then the inner and outer rim (2 or 3; 0 for a closed aperture)
    SOLTEST_API int32 AppendRadialSector(const FVector2f& centre, float scale, float midAngleRad,
        const FSOLHoloRadialSectorLook& sector, bool bHighlighted, FSOLHudSymbolPrims& out);
}
