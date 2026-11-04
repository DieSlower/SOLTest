/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "UI/SOLHoloMenuMath.h"
#include "UI/SOLHudShapeMath.h"

#include "CoreMinimal.h"

struct FSOLHudShapeBatch;

// The role of one shape of a hologram frame or card (SDD 42 section 4.6), so tests and the smoke can tell them apart
enum class ESOLHoloPart : uint8
{
    Fill,           // The borderless panel fill over the backing rect
    Outline,        // A visible piece of the stepped broken outline
    GapDot,         // A dot at an end of an outline gap
    Ruler,          // A left-edge ruler tick
    Accent,         // An inner corner accent (pieces 1-2 of FrameAccents)
    TitleAccent,    // The title underline and its dash (pieces 3-4 of FrameAccents)
    ColumnDivider,  // A dash of the Overlay Menu's column divider
    BrokenDivider,  // A piece of the broken horizontal divider
    Tab,            // A piece of a profile tab marker
    TabDot,         // The selected tab's dot
    CardTick,       // A corner tick of a frameless card
    Sweep,          // The idle scanline sweep
    Hover,          // A piece of the hovered item's mark
    HoverDot,       // The hover row pointer's dot
};

// One shape for FSOLHudShapeBatch: a Line from A to B (a dot is A == B with the radius as HalfWidthPx), or a Panel
// with A = its centre and B = its half size
struct FSOLHoloShape
{
    ESOLHoloPart Part = ESOLHoloPart::Fill;
    ESOLHudShapeKind Kind = ESOLHudShapeKind::Line;
    FVector2f A = FVector2f::ZeroVector;
    FVector2f B = FVector2f::ZeroVector;
    float HalfWidthPx = 0.0f;
    FLinearColor Color = FLinearColor::Transparent;
    float Intensity = 0.0f;
    float Fill = 0.0f;
    float PadPx = 0.0f;
};

// The faint dark tint box behind a frame or card (SSOLHudShapeLayer::AddBacking); Alpha is absolute
struct FSOLHoloBacking
{
    FBox2f Rect = FBox2f(ForceInit);
    float CornerPx = 0.0f;
    float FeatherPx = 0.0f;
    float Alpha = 0.0f;
};

// What one frame or card draws this paint: its shapes in draw order and its backing (fixed capacity, no heap)
struct FSOLHoloFrameOutput
{
    FSOLHoloShape Shapes[SOL::HoloMenu::FRAME_MAX_SHAPES];
    int32 ShapeCount = 0;
    bool bHasBacking = false;
    FSOLHoloBacking Backing;
};

// One option (c) frame's layout: its rect and title step, the optional dividers and tab markers, and the hovered item
struct FSOLHoloFrameSpec
{
    FBox2f Rect = FBox2f(ForceInit);                    // The frame rect (screen px, y down)
    float TitleStepWidthPx = 0.0f;                      // Title text width + FRAME_TITLE_STEP_PAD_PX x uiScale, or 0
    float UiScale = 1.0f;

    bool bColumnDivider = false;                        // DashedDivider: the Overlay Menu's column divider
    FVector2f DividerTop = FVector2f::ZeroVector;
    float DividerBottomY = 0.0f;
    float OpenTopY = 1.0f;                              // The opening where the selected tab joins (none when
    float OpenBottomY = 0.0f;                           // OpenTopY > OpenBottomY)

    bool bBrokenDivider = false;                        // BrokenDivider: the hint / steppers divider
    FVector2f BrokenStart = FVector2f::ZeroVector;
    float BrokenEndX = 0.0f;
    float BrokenStepAtPx = 0.0f;
    float BrokenStepPx = 0.0f;                          // 0: no step

    int32 TabCount = 0;                                 // Profile tab markers (above FRAME_MAX_TABS reads the maximum)
    FBox2f TabRects[SOL::HoloMenu::FRAME_MAX_TABS];     // Set to empty rects by the constructor
    int32 SelectedTab = INDEX_NONE;
    float TabDividerX = 0.0f;

    FBox2f HoverItemRect = FBox2f(ForceInit);           // The hovered item's rect; an invalid rect = nothing hovered
    ESOLHoloHoverMark HoverMark = ESOLHoloHoverMark::RowPointer;

    // Starts with every tab rect empty (zero, invalid)
    FSOLHoloFrameSpec()
    {
        for (FBox2f& tabRect : TabRects)
        {
            tabRect = FBox2f(ForceInit);
        }
    }
};

/**
 * The hologram menu kit's pure frame and card builders (SDD 42 section 4.6): they turn a menu's layout and its
 * FSOLHoloMenuLook into fixed-capacity shape lists (each group taken unchanged from its SOLHoloMenu section 6.3
 * function, with its role, colour, intensity and glow pad), and append those to an FSOLHudShapeBatch. No heap, no
 * Slate, no RHI; the same inputs always give the same output.
 */
namespace SOLHoloFrameBuild
{
    // Builds one option (c) frame (emptying out first); returns out.ShapeCount, 0 when hidden or the rect is invalid
    SOLTEST_API int32 BuildFrame(const FSOLHoloFrameSpec& spec, const FSOLHoloMenuLook& look, float s,
        FSOLHoloFrameOutput& out);

    // Builds one frameless card around its content (emptying out first): fill, 8 corner ticks and the sweep around the
    // content grown by HUD_PANEL_PADDING_PX; returns out.ShapeCount (at most CARD_MAX_SHAPES)
    SOLTEST_API int32 BuildCard(const FBox2f& contentRect, float uiScale, const FSOLHoloMenuLook& look, float s,
        FSOLHoloFrameOutput& out);

    // Adds an output's shapes to a batch in order (no reset); returns how many the batch took
    SOLTEST_API int32 AppendToBatch(const FSOLHoloFrameOutput& in, FSOLHudShapeBatch& batch);

    // The SSOLHudShapeLayer::AddBacking opacity of an absolute backdrop alpha: clamp(alpha / backing alpha, 0, 1)
    SOLTEST_API float BackingOpacity(float alpha);

    // The glow pad of every menu line and dot at strength s: lerp(FRAME_FLAT_PAD_PX, HUD_GLOW_PAD_PX, GlowScale(s)),
    // so exactly FRAME_FLAT_PAD_PX at s <= 0 (D18) and HUD_GLOW_PAD_PX from s = 1
    SOLTEST_API float LinePad(float s);
}
