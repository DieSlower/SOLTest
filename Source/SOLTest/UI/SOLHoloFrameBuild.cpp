/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloFrameBuild.h"

#include "UI/SOLHudShapeBatch.h"

namespace
{
    // Section 4.6 worst case of one frame: outline, gap dots, ruler, accents, column divider, broken divider, every tab
    // marker, the selected tab's dot, the hover mark and its dot, the fill and the sweep (126 with 5 tabs)
    constexpr int32 HOLOBUILD_FRAME_WORST_CASE = SOL::HoloMenu::FRAME_MAX_SEGMENTS + SOL::HoloMenu::FRAME_MAX_DOTS
        + SOL::HoloMenu::FRAME_RULER_TICK_COUNT + SOL::HoloMenu::FRAME_ACCENT_COUNT + SOL::HoloMenu::DIVIDER_MAX_DASHES
        + SOL::HoloMenu::DIVIDER_MAX_SEGMENTS + SOL::HoloMenu::FRAME_MAX_TABS * SOL::HoloMenu::TAB_MAX_SEGMENTS + 1
        + SOL::HoloMenu::HOVER_MAX_SEGMENTS + 1 + 2;
    constexpr int32 HOLOBUILD_CARD_WORST_CASE = 1 + SOL::HoloMenu::CARD_TICK_SEGMENTS + 1;   // Fill, ticks, sweep

    static_assert(HOLOBUILD_FRAME_WORST_CASE <= SOL::HoloMenu::FRAME_MAX_SHAPES,
        "FRAME_MAX_SHAPES must hold the worst-case option (c) frame");
    static_assert(HOLOBUILD_CARD_WORST_CASE <= SOL::HoloMenu::CARD_MAX_SHAPES,
        "CARD_MAX_SHAPES must hold a card's fill, corner ticks and sweep");
    static_assert(SOL::HoloMenu::CARD_MAX_SHAPES <= SOL::HoloMenu::FRAME_MAX_SHAPES,
        "a card is built into an FSOLHoloFrameOutput");

    // Writes shapes into an output up to a capacity; a shape with intensity <= 0 (or NaN) is never written
    struct FSOLHoloShapeWriter
    {
        FSOLHoloFrameOutput& Out;
        int32 Capacity;
        float HalfWidthPx;      // Line half width (FRAME_HALF_WIDTH_PX x u)
        float PadPx;            // Glow pad of every line and dot

        // Writes one shape when it has a positive intensity and there is room
        void Add(const ESOLHoloPart part, const ESOLHudShapeKind kind, const FVector2f& a, const FVector2f& b,
            const float halfWidthPx, const FLinearColor& color, const float intensity, const float fill,
            const float padPx)
        {
            if (!(intensity > 0.0f) || Out.ShapeCount >= Capacity)
            {
                return;
            }
            FSOLHoloShape& shape = Out.Shapes[Out.ShapeCount];
            shape.Part = part;
            shape.Kind = kind;
            shape.A = a;
            shape.B = b;
            shape.HalfWidthPx = halfWidthPx;
            shape.Color = color;
            shape.Intensity = intensity;
            shape.Fill = fill;
            shape.PadPx = padPx;
            ++Out.ShapeCount;
        }

        // Writes a frame line a -> b
        void Line(const ESOLHoloPart part, const FVector2f& a, const FVector2f& b, const FLinearColor& color,
            const float intensity)
        {
            Add(part, ESOLHudShapeKind::Line, a, b, HalfWidthPx, color, intensity, 0.0f, PadPx);
        }

        // Writes every segment of an array as frame lines
        void Segments(const ESOLHoloPart part, const FSOLHoloSegment* segments, const int32 count,
            const FLinearColor& color, const float intensity)
        {
            for (int32 i = 0; i < count; ++i)
            {
                Line(part, segments[i].A, segments[i].B, color, intensity);
            }
        }

        // Writes a filled dot: a zero-length line with the radius as its half width
        void Dot(const ESOLHoloPart part, const FVector2f& p, const float radiusPx, const FLinearColor& color,
            const float intensity)
        {
            Add(part, ESOLHudShapeKind::Line, p, p, radiusPx, color, intensity, 0.0f, PadPx);
        }

        // Writes the borderless panel fill over a rect (centre, half size), in the dark backdrop tint
        void Fill(const FBox2f& rect, const float fillAlpha)
        {
            Add(ESOLHoloPart::Fill, ESOLHudShapeKind::Panel, (rect.Min + rect.Max) * 0.5f, (rect.Max - rect.Min) * 0.5f,
                0.0f, SOL::HoloMenu::BACKDROP_TINT, SOL::Hologram::HUD_PANEL_INTENSITY,
                SOL::Hologram::HUD_PANEL_FILL * fillAlpha, SOL::Hologram::HUD_PANEL_QUAD_PAD_PX);
        }

        // Writes the idle sweep line across a rect at its look's fraction of the height
        void Sweep(const FBox2f& rect, const FSOLHoloMenuLook& look)
        {
            const float y = rect.Min.Y + look.SweepFraction * (rect.Max.Y - rect.Min.Y);
            Line(ESOLHoloPart::Sweep, FVector2f(rect.Min.X, y), FVector2f(rect.Max.X, y), SOL::HudPalette::PRIMARY,
                look.SweepIntensity);
        }
    };


    //////////////////////////////////////////////////////////////////////////
    // Returns a rect grown by padX / padY on every side (negative values shrink it)
    FBox2f HoloBuildGrow(const FBox2f& rect, const float padX, const float padY)
    {
        return FBox2f(rect.Min - FVector2f(padX, padY), rect.Max + FVector2f(padX, padY));
    }

    //////////////////////////////////////////////////////////////////////////
    // Empties an output: no shapes, no backing
    void HoloBuildClear(FSOLHoloFrameOutput& out)
    {
        out.ShapeCount = 0;
        out.bHasBacking = false;
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets an output's backing box
    void HoloBuildBacking(FSOLHoloFrameOutput& out, const FBox2f& rect, const float cornerPx, const float featherPx,
        const float alpha)
    {
        out.bHasBacking = true;
        out.Backing.Rect = rect;
        out.Backing.CornerPx = cornerPx;
        out.Backing.FeatherPx = featherPx;
        out.Backing.Alpha = alpha;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the groups that fade up with the fill: ruler, accents, both dividers and the tab markers
    void HoloBuildFillGroups(const FSOLHoloFrameSpec& spec, const float fillAlpha, FSOLHoloShapeWriter& writer)
    {
        using namespace SOL::HoloMenu;
        const FLinearColor& primary = SOL::HudPalette::PRIMARY;
        const float bright = FRAME_INTENSITY * fillAlpha;
        const float dim = FRAME_INTENSITY * FRAME_DIM_ALPHA * fillAlpha;

        FSOLHoloSegment ruler[FRAME_RULER_TICK_COUNT];
        writer.Segments(ESOLHoloPart::Ruler, ruler, SOLHoloMenu::FrameRulerTicks(spec.Rect, spec.UiScale, ruler),
            primary, bright);

        // Accents: FrameAccents keeps the order 1-4 and only pieces 3-4 (the title underline and dash) run rightward
        FSOLHoloSegment accents[FRAME_ACCENT_COUNT];
        const int32 accentCount = SOLHoloMenu::FrameAccents(spec.Rect, spec.TitleStepWidthPx, spec.UiScale, accents);
        for (int32 i = 0; i < accentCount; ++i)
        {
            const bool bIsTitle = accents[i].B.X > accents[i].A.X;
            writer.Line(bIsTitle ? ESOLHoloPart::TitleAccent : ESOLHoloPart::Accent, accents[i].A, accents[i].B,
                primary, bIsTitle ? dim : bright);
        }

        if (spec.bColumnDivider)
        {
            FSOLHoloSegment dashes[DIVIDER_MAX_DASHES];
            writer.Segments(ESOLHoloPart::ColumnDivider, dashes, SOLHoloMenu::DashedDivider(spec.DividerTop,
                spec.DividerBottomY, spec.OpenTopY, spec.OpenBottomY, spec.UiScale, dashes), primary, dim);
        }
        if (spec.bBrokenDivider)
        {
            FSOLHoloSegment pieces[DIVIDER_MAX_SEGMENTS];
            writer.Segments(ESOLHoloPart::BrokenDivider, pieces, SOLHoloMenu::BrokenDivider(spec.BrokenStart,
                spec.BrokenEndX, spec.BrokenStepAtPx, spec.BrokenStepPx, spec.UiScale, pieces), primary, dim);
        }

        // Tab markers in index order, each tab's dot after its own pieces
        const float dotRadius = FRAME_DOT_RADIUS_PX * SOLHoloMenu::UiScale(spec.UiScale);
        const int32 tabCount = FMath::Clamp(spec.TabCount, 0, FRAME_MAX_TABS);
        for (int32 tab = 0; tab < tabCount; ++tab)
        {
            const bool bIsSelected = tab == spec.SelectedTab;
            FSOLHoloSegment pieces[TAB_MAX_SEGMENTS];
            FVector2f dot = FVector2f::ZeroVector;
            int32 dotCount = 0;
            const int32 pieceCount = SOLHoloMenu::TabMarker(spec.TabRects[tab], spec.TabDividerX, bIsSelected,
                spec.UiScale, pieces, dot, dotCount);
            writer.Segments(ESOLHoloPart::Tab, pieces, pieceCount, primary, bIsSelected ? bright : dim);
            if (dotCount > 0)
            {
                writer.Dot(ESOLHoloPart::TabDot, dot, dotRadius, primary, bright);
            }
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes the hovered item's mark in Secondary: the hover glow x fill for s > 0, the flat frame intensity x fill at
    // s = 0 (D18)
    void HoloBuildHover(const FSOLHoloFrameSpec& spec, const FSOLHoloMenuLook& look, const float s,
        FSOLHoloShapeWriter& writer)
    {
        using namespace SOL::HoloMenu;
        if (!SOLHoloMenu::IsValidRect(spec.HoverItemRect))
        {
            return;
        }
        const bool bHasGlow = SOLHoloMenu::GlowScale(s) > 0.0f;
        const float intensity = (bHasGlow ? look.HoverIntensity : FRAME_INTENSITY) * look.FillAlpha;
        if (!(intensity > 0.0f))
        {
            return;
        }
        const float u = SOLHoloMenu::UiScale(spec.UiScale);
        const FBox2f markRect = HoloBuildGrow(spec.HoverItemRect, HOVER_PAD_X_PX * u, HOVER_PAD_Y_PX * u);
        FSOLHoloSegment pieces[HOVER_MAX_SEGMENTS];
        FVector2f dot = FVector2f::ZeroVector;
        int32 dotCount = 0;
        const int32 pieceCount = SOLHoloMenu::HoverMark(markRect, spec.HoverMark, spec.UiScale, pieces, dot, dotCount);
        writer.Segments(ESOLHoloPart::Hover, pieces, pieceCount, SOL::HudPalette::SECONDARY, intensity);
        if (dotCount > 0)
        {
            writer.Dot(ESOLHoloPart::HoverDot, dot, FRAME_DOT_RADIUS_PX * u, SOL::HudPalette::SECONDARY, intensity);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Builds one option (c) frame: fill, outline and gap dots, the fill-faded groups, sweep, hover mark, backing
int32 SOLHoloFrameBuild::BuildFrame(const FSOLHoloFrameSpec& spec, const FSOLHoloMenuLook& look, const float s,
    FSOLHoloFrameOutput& out)
{
    using namespace SOL::HoloMenu;
    HoloBuildClear(out);
    if (!look.bVisible || !SOLHoloMenu::IsValidRect(spec.Rect))
    {
        return 0;
    }
    const float u = SOLHoloMenu::UiScale(spec.UiScale);
    const float k = SOLHoloMenu::FrameScale(spec.Rect, spec.UiScale);
    const float inset = FRAME_TINT_INSET_PX * k;
    const FBox2f backingRect = HoloBuildGrow(spec.Rect, -inset, -inset);
    FSOLHoloShapeWriter writer{ out, FRAME_MAX_SHAPES, FRAME_HALF_WIDTH_PX * u, SOLHoloFrameBuild::LinePad(s) };

    if (look.FillAlpha > 0.0f)
    {
        writer.Fill(backingRect, look.FillAlpha);
    }

    // The broken outline and its gap dots at the draw-in fraction, at the frame intensity
    if (look.FrameFraction > 0.0f)
    {
        FSOLHoloSegment segments[FRAME_MAX_SEGMENTS];
        writer.Segments(ESOLHoloPart::Outline, segments, SOLHoloMenu::FrameDrawSegments(spec.Rect,
            spec.TitleStepWidthPx, spec.UiScale, look.FrameFraction, segments), SOL::HudPalette::PRIMARY,
            FRAME_INTENSITY);
        FVector2f dots[FRAME_MAX_DOTS];
        const int32 dotCount = SOLHoloMenu::FrameGapDots(spec.Rect, spec.TitleStepWidthPx, spec.UiScale,
            look.FrameFraction, dots);
        for (int32 i = 0; i < dotCount; ++i)
        {
            writer.Dot(ESOLHoloPart::GapDot, dots[i], FRAME_DOT_RADIUS_PX * k, SOL::HudPalette::PRIMARY,
                FRAME_INTENSITY);
        }
    }

    if (look.FillAlpha > 0.0f)
    {
        HoloBuildFillGroups(spec, look.FillAlpha, writer);
    }
    if (look.SweepIntensity > 0.0f)
    {
        writer.Sweep(backingRect, look);
    }
    HoloBuildHover(spec, look, s, writer);

    if (look.BackdropAlpha > 0.0f)
    {
        HoloBuildBacking(out, backingRect, 0.0f, SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX * u, look.BackdropAlpha);
    }
    return out.ShapeCount;
}

//////////////////////////////////////////////////////////////////////////
// Builds one frameless card: fill, corner ticks and sweep around the padded content, its backing as AddHoloPanel's
int32 SOLHoloFrameBuild::BuildCard(const FBox2f& contentRect, const float uiScale, const FSOLHoloMenuLook& look,
    const float s, FSOLHoloFrameOutput& out)
{
    using namespace SOL::HoloMenu;
    HoloBuildClear(out);
    if (!look.bVisible || !SOLHoloMenu::IsValidRect(contentRect))
    {
        return 0;
    }
    const float u = SOLHoloMenu::UiScale(uiScale);
    const float panelPad = SOL::Hologram::HUD_PANEL_PADDING_PX * u;
    const FBox2f cardRect = HoloBuildGrow(contentRect, panelPad, panelPad);
    FSOLHoloShapeWriter writer{ out, CARD_MAX_SHAPES, FRAME_HALF_WIDTH_PX * u, SOLHoloFrameBuild::LinePad(s) };

    if (look.FillAlpha > 0.0f)
    {
        writer.Fill(cardRect, look.FillAlpha);
    }
    FSOLHoloSegment ticks[CARD_TICK_SEGMENTS];
    writer.Segments(ESOLHoloPart::CardTick, ticks, SOLHoloMenu::CardCornerTicks(cardRect, CARD_TICK_PX * u,
        look.FrameFraction, ticks), SOL::HudPalette::PRIMARY, FRAME_INTENSITY);
    if (look.SweepIntensity > 0.0f)
    {
        writer.Sweep(cardRect, look);
    }

    if (look.BackdropAlpha > 0.0f)
    {
        HoloBuildBacking(out, cardRect, static_cast<float>(SOL::Hologram::HUD_PANEL_CORNER_PX) * u,
            SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX * u, look.BackdropAlpha);
    }
    return out.ShapeCount;
}

//////////////////////////////////////////////////////////////////////////
// Adds an output's shapes to a batch in order (Line by AddLine, Panel by AddPanel); returns how many it took
int32 SOLHoloFrameBuild::AppendToBatch(const FSOLHoloFrameOutput& in, FSOLHudShapeBatch& batch)
{
    const int32 count = FMath::Clamp(in.ShapeCount, 0, SOL::HoloMenu::FRAME_MAX_SHAPES);
    int32 taken = 0;
    for (int32 i = 0; i < count; ++i)
    {
        const FSOLHoloShape& shape = in.Shapes[i];
        const bool bIsAdded = shape.Kind == ESOLHudShapeKind::Panel
            ? batch.AddPanel(shape.A, shape.B, shape.Color, shape.Intensity, shape.Fill, shape.PadPx)
            : batch.AddLine(shape.A, shape.B, shape.HalfWidthPx, shape.Color, shape.Intensity, shape.PadPx);
        taken += bIsAdded ? 1 : 0;
    }
    return taken;
}

//////////////////////////////////////////////////////////////////////////
// The glow pad of every menu line and dot: thin and flat at s = 0, the full HUD glow from s = 1
float SOLHoloFrameBuild::LinePad(const float s)
{
    return FMath::Lerp(SOL::HoloMenu::FRAME_FLAT_PAD_PX, SOL::Hologram::HUD_GLOW_PAD_PX, SOLHoloMenu::GlowScale(s));
}

//////////////////////////////////////////////////////////////////////////
// The AddBacking opacity of an absolute backdrop alpha, relative to the HUD backing colour's alpha; NaN gives 0
float SOLHoloFrameBuild::BackingOpacity(const float alpha)
{
    if (FMath::IsNaN(alpha))
    {
        return 0.0f;
    }
    return FMath::Clamp(alpha / SOL::Hologram::HUD_PANEL_BACKING_COLOR.A, 0.0f, 1.0f);
}

//////////////////////////////////////////////////////////////////////////
// The Overlay Menu's frame layout from its title, columns, tabs and hint (the frame's own rect, scale and hover are
// left as they are)
void SOLHoloFrameBuild::MakeOverlayLayout(const FSOLHoloOverlayLayoutInput& in, FSOLHoloFrameSpec& out)
{
    out.TitleStepWidthPx = FMath::IsFinite(in.TitleWidth) && in.TitleWidth > 0.0f ? in.TitleWidth : 0.0f;

    // The tab markers: the tabs given, up to FRAME_MAX_TABS, the rest emptied; a selection outside them is none
    const int32 tabCount = FMath::Clamp(in.TabCount, 0, SOL::HoloMenu::FRAME_MAX_TABS);
    out.TabCount = tabCount;
    for (int32 tab = 0; tab < SOL::HoloMenu::FRAME_MAX_TABS; ++tab)
    {
        out.TabRects[tab] = tab < tabCount ? in.TabRects[tab] : FBox2f(ForceInit);
    }
    out.SelectedTab = in.SelectedTab >= 0 && in.SelectedTab < tabCount ? in.SelectedTab : INDEX_NONE;

    // The dashed column divider halfway between the two columns, over their full height, open beside the selected
    // tab (where its marker runs into the divider); none when a column is missing or they overlap
    const bool bHasTabs = SOLHoloMenu::IsValidRect(in.TabColumn);
    const bool bHasRows = SOLHoloMenu::IsValidRect(in.RowColumn);
    const bool bHasDivider = bHasTabs && bHasRows && in.RowColumn.Min.X >= in.TabColumn.Max.X;
    const float dividerX = bHasDivider ? 0.5f * (in.TabColumn.Max.X + in.RowColumn.Min.X) : 0.0f;
    out.bColumnDivider = bHasDivider;
    out.DividerTop = bHasDivider ? FVector2f(dividerX, FMath::Min(in.TabColumn.Min.Y, in.RowColumn.Min.Y))
        : FVector2f::ZeroVector;
    out.DividerBottomY = bHasDivider ? FMath::Max(in.TabColumn.Max.Y, in.RowColumn.Max.Y) : 0.0f;
    out.TabDividerX = dividerX;
    const bool bHasOpening = bHasDivider && out.SelectedTab != INDEX_NONE
        && SOLHoloMenu::IsValidRect(out.TabRects[out.SelectedTab]);
    out.OpenTopY = bHasOpening ? out.TabRects[out.SelectedTab].Min.Y : 1.0f;
    out.OpenBottomY = bHasOpening ? out.TabRects[out.SelectedTab].Max.Y : 0.0f;

    // The broken hint divider (no step) halfway between the columns' bottom and the hint, across both columns
    FBox2f body(ForceInit);
    body += bHasTabs ? in.TabColumn : FBox2f(ForceInit);
    body += bHasRows ? in.RowColumn : FBox2f(ForceInit);
    const bool bHasHintDivider = body.bIsValid && FMath::IsFinite(in.HintTopY) && in.HintTopY > body.Max.Y;
    const float hintDividerY = bHasHintDivider ? 0.5f * (body.Max.Y + in.HintTopY) : 0.0f;
    out.bBrokenDivider = bHasHintDivider;
    out.BrokenStart = bHasHintDivider ? FVector2f(body.Min.X, hintDividerY) : FVector2f::ZeroVector;
    out.BrokenEndX = bHasHintDivider ? body.Max.X : 0.0f;
    out.BrokenStepAtPx = 0.0f;
    out.BrokenStepPx = 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// The F3 speed panel's frame layout from its title and its two sections (the frame's own rect, scale and hover are
// left as they are)
void SOLHoloFrameBuild::MakeSpeedLayout(const FSOLHoloSpeedLayoutInput& in, FSOLHoloFrameSpec& out)
{
    out.TitleStepWidthPx = FMath::IsFinite(in.TitleWidth) && in.TitleWidth > 0.0f ? in.TitleWidth : 0.0f;

    // No column divider and no tab markers on this panel
    out.bColumnDivider = false;
    out.DividerTop = FVector2f::ZeroVector;
    out.DividerBottomY = 0.0f;
    out.OpenTopY = 1.0f;
    out.OpenBottomY = 0.0f;
    out.TabCount = 0;
    for (FBox2f& tabRect : out.TabRects)
    {
        tabRect = FBox2f(ForceInit);
    }
    out.SelectedTab = INDEX_NONE;
    out.TabDividerX = 0.0f;

    // The stepped broken divider across both sections, its two levels straddling the midpoint of the gap between them
    const bool bHasDivider = SOLHoloMenu::IsValidRect(in.StepperSection) && SOLHoloMenu::IsValidRect(in.ListSection)
        && in.ListSection.Min.Y > in.StepperSection.Max.Y;
    const float localPerPx = FMath::IsFinite(in.LocalPerPx) && in.LocalPerPx > 0.0f ? in.LocalPerPx : 1.0f;
    const float stepPx = SOL::HoloMenu::SPEED_DIVIDER_STEP_PX * localPerPx;
    const float midY = 0.5f * (in.StepperSection.Max.Y + in.ListSection.Min.Y);
    out.bBrokenDivider = bHasDivider;
    out.BrokenStart = bHasDivider ? FVector2f(FMath::Min(in.StepperSection.Min.X, in.ListSection.Min.X),
        midY + 0.5f * stepPx) : FVector2f::ZeroVector;
    out.BrokenEndX = bHasDivider ? FMath::Max(in.StepperSection.Max.X, in.ListSection.Max.X) : 0.0f;
    out.BrokenStepAtPx = bHasDivider ? SOL::HoloMenu::SPEED_DIVIDER_STEP_AT_PX * localPerPx : 0.0f;
    out.BrokenStepPx = bHasDivider ? stepPx : 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// The rect a hover mark goes around for a text item: from the slot's left plus the indent to the text's own width
FBox2f SOLHoloFrameBuild::TextItemRect(const FBox2f& slotRect, const float textWidth, const float indentWidth)
{
    if (!SOLHoloMenu::IsValidRect(slotRect) || !FMath::IsFinite(textWidth))
    {
        return FBox2f(ForceInit);
    }
    const float indent = FMath::IsFinite(indentWidth) && indentWidth > 0.0f ? indentWidth : 0.0f;
    const float left = slotRect.Min.X + indent;
    const float right = slotRect.Min.X + FMath::Min(textWidth, slotRect.Max.X - slotRect.Min.X);
    if (right <= left)
    {
        return FBox2f(ForceInit);
    }
    return FBox2f(FVector2f(left, slotRect.Min.Y), FVector2f(right, slotRect.Max.Y));
}
