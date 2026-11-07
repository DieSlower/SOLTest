/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"
#include "UI/SOLOverlayIcons.h"

#include "Containers/StringView.h"
#include "CoreMinimal.h"

struct FSOLHoloFrameOutput;
struct FSOLHudShapeBatch;

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
    Count,
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
    float UiScale = 1.0f;           // Card: the UI scale its padding and ticks are built at
    SOLOverlayIcons::EIconCategory Category = SOLOverlayIcons::EIconCategory::Count;
    const TCHAR* Text = nullptr;
};

// A symbol's primitives (fixed capacity SOL::Help::MAX_PRIMS_PER_SYMBOL, no heap); Add drops a primitive past it. About
// 2.5 KB: hold one as a member (or a long-lived scratch) and Reset() it, never build one per call in a hot path
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

/**
 * The HUD symbol helper (SDD 33 section 8.1, D5), pure and test-first: one source of geometry for each HUD symbol,
 * written as primitives at a centre and a UI scale. The flight HUD replays them through its Glow* calls (part 33d-i),
 * the menus' glossary through AddToBatch into an FSOLHudShapeBatch. Symbols with an existing builder (the overlay
 * icons and edge arrows, the cards) are Icon / EdgeArrow / Card primitives that AddToBatch hands to that builder, so
 * no geometry is copied. No heap, no Slate.
 */
namespace SOLHudSymbols
{
    // Writes a symbol's primitives centred on centre at a UI scale into out (Reset first); returns out.Count (0 for
    // Count or a scale that is not a positive finite number)
    SOLTEST_API int32 Build(ESOLHudSymbol symbol, const FVector2f& centre, float scale, FSOLHudSymbolPrims& out);

    // Returns a symbol's bounds relative to its centre at a UI scale (linear in the scale; it contains every primitive
    // Build writes, glow pad excluded); an empty box (FBox2f(ForceInit)) for Count or an invalid scale. scratch is
    // overwritten (Build's output), so no primitive list is made per call
    SOLTEST_API FBox2f Bounds(ESOLHudSymbol symbol, float scale, FSOLHudSymbolPrims& scratch);

    // Replays the primitives into a batch (no reset) in the palette's colours with a glow pad: one shape per Line ...
    // Panel primitive, what the builder adds for Icon / EdgeArrow / Card, none for Text; returns the shapes added. A
    // Card primitive is built into cardScratch (overwritten; about 8 KB, so hold one next to the batch)
    SOLTEST_API int32 AddToBatch(const FSOLHudSymbolPrims& prims, FSOLHudShapeBatch& batch,
        const FSOLHudSymbolPalette& palette, float padPx, FSOLHoloFrameOutput& cardScratch);

    // AddToBatch for a one-off replay (tests, tools): a Card primitive is built into a frame output on the stack
    SOLTEST_API int32 AddToBatch(const FSOLHudSymbolPrims& prims, FSOLHudShapeBatch& batch,
        const FSOLHudSymbolPalette& palette, float padPx);

    // Returns the HUD palette (SOL::HudPalette, one colour per role) at strength 1
    SOLTEST_API FSOLHudSymbolPalette HudPalette();

    // Finds a symbol by its id (case-sensitive, as ToName writes it); false and out unchanged for an unknown id
    SOLTEST_API bool FromName(FStringView name, ESOLHudSymbol& out);

    // Returns a symbol's id ("Reticle", "SteerFree"; "" for Count)
    SOLTEST_API const TCHAR* ToName(ESOLHudSymbol symbol);
}
