/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLHoloFrameBuild.h"
#include "UI/SOLHoloMenuMath.h"
#include "UI/SOLHoloMenuSubsystem.h"
#include "UI/SOLHudShapeBatch.h"
#include "UI/SOLHudShapeMath.h"
#include "SOLConstants.h"

#include "Misc/AutomationTest.h"
#include "Templates/UniquePtr.h"

#include <cmath>
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #42 part 42c hologram menu rendering kit (Docs/SDDs/42-hologram-in-flight-menus.md section 4.6):
// the frame and card builders, AppendToBatch, BackingOpacity, FrameScale, the parameter cache, the cue gate and the
// bare menu subsystem. Written against the section 4.6 contracts before the production code existed; every expected
// shape is rebuilt here from the finished section 6.3 functions and the SOL::HoloMenu constants
namespace
{
    constexpr float HOLOKITTEST_NAN = std::numeric_limits<float>::quiet_NaN();
    constexpr float HOLOKITTEST_INF = std::numeric_limits<float>::infinity();
    constexpr double HOLOKITTEST_NAN_D = std::numeric_limits<double>::quiet_NaN();
    constexpr double HOLOKITTEST_INF_D = std::numeric_limits<double>::infinity();

    // Tolerances
    constexpr float HOLOKITTEST_VALUE_TOLERANCE = 1.0e-6f;  // Intensities, fills, radii, half widths, alphas
    constexpr float HOLOKITTEST_PAD_TOLERANCE = 1.0e-4f;    // Glow pads (a lerp)
    constexpr float HOLOKITTEST_PX_TOLERANCE = 5.0e-3f;     // Positions rebuilt here from rect arithmetic

    // Section 6 strength range
    constexpr float HOLOKITTEST_S_MAX = 2.0f;

    // Output guards: shapes after the output struct, pre-filled with a sentinel that must survive every call
    constexpr int32 HOLOKITTEST_GUARD = 4;
    constexpr float HOLOKITTEST_SENTINEL = -98765.25f;

    // Capacity of the reference output built here (above FRAME_MAX_SHAPES, so the reference itself never clips)
    constexpr int32 HOLOKITTEST_REF_CAPACITY = 256;
    constexpr int32 HOLOKITTEST_PART_COUNT = static_cast<int32>(ESOLHoloPart::HoverDot) + 1;

    // Worked example (SDD 42 sections 4.6 and 6.4): rect R = (0,0)-(400,300), title step 200
    constexpr float HOLOKITTEST_R_TITLE = 200.0f;
    constexpr int32 HOLOKITTEST_R_OUTLINE_PIECES = 19;
    constexpr int32 HOLOKITTEST_R_GAP_DOTS = 14;
    constexpr int32 HOLOKITTEST_R_RULER_TICKS = 6;
    constexpr int32 HOLOKITTEST_ROW_POINTER_PIECES = 8;
    constexpr int32 HOLOKITTEST_BRACKET_PIECES = 6;
    constexpr int32 HOLOKITTEST_CARD_TICKS = 8;
    constexpr float HOLOKITTEST_SWEEP_FRACTION = 0.25f;

    // Section 4.6 worst case: 24 + 14 + 6 + 4 + 40 + 6 + FRAME_MAX_TABS * 4 + 1 + 8 + 1 + 2 (= 126 with 5 tabs)
    constexpr int32 HOLOKITTEST_WORST_CASE_FIXED = 24 + 14 + 6 + 4 + 40 + 6 + 1 + 8 + 1 + 2;

    // Batch rounds of the no-growth check (section 4.6)
    constexpr int32 HOLOKITTEST_BATCH_ROUNDS = 200;

    // The reference output: the shapes, count and backing the contracts give for one input
    struct FSOLHoloKitTestExpected
    {
        FSOLHoloShape Shapes[HOLOKITTEST_REF_CAPACITY];
        int32 ShapeCount = 0;
        bool bHasBacking = false;
        FSOLHoloBacking Backing;
    };

    // A builder output followed by guard shapes, to catch writes past the output
    struct FSOLHoloKitTestGuardedOutput
    {
        FSOLHoloFrameOutput Out;
        FSOLHoloShape Guard[HOLOKITTEST_GUARD];
    };

    // Counts failures of a sweep and keeps the first one's description (avoids flooding the log)
    struct FSOLHoloKitTestSweepLog
    {
        int32 Failures = 0;
        FString First;

        // Records a failure, keeping the first description
        void Fail(const FString& what)
        {
            if (Failures == 0)
            {
                First = what;
            }
            ++Failures;
        }
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the rect (x0, y0)-(x1, y1)
    FBox2f HoloKitRect(const float x0, const float y0, const float x1, const float y1)
    {
        return FBox2f(FVector2f(x0, y0), FVector2f(x1, y1));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the worked-example rect R
    FBox2f HoloKitRectR()
    {
        return HoloKitRect(0.0f, 0.0f, 400.0f, 300.0f);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a rect is valid by the section 6.3 rule: finite corners, positive width and height
    bool HoloKitRectValid(const FBox2f& rect)
    {
        return FMath::IsFinite(rect.Min.X) && FMath::IsFinite(rect.Min.Y) && FMath::IsFinite(rect.Max.X)
            && FMath::IsFinite(rect.Max.Y) && rect.Max.X - rect.Min.X > 0.0f && rect.Max.Y - rect.Min.Y > 0.0f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns u, the UI scale after the section 6.3 rule (NaN, infinite or <= 0 reads 1, then clamped)
    float HoloKitU(const float uiScale)
    {
        if (!FMath::IsFinite(uiScale) || uiScale <= 0.0f)
        {
            return 1.0f;
        }
        return FMath::Clamp(uiScale, SOL::HoloMenu::UI_SCALE_MIN, SOL::HoloMenu::UI_SCALE_MAX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns s, the strength after the section 6 rule (NaN reads 0, clamped to [0, 2])
    float HoloKitS(const float s)
    {
        if (FMath::IsNaN(s))
        {
            return 0.0f;
        }
        return FMath::Clamp(s, 0.0f, HOLOKITTEST_S_MAX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the effective outline scale k of section 6.3 (0 for an invalid rect)
    float HoloKitK(const FBox2f& rect, const float uiScale)
    {
        if (!HoloKitRectValid(rect))
        {
            return 0.0f;
        }
        const float w = rect.Max.X - rect.Min.X;
        const float h = rect.Max.Y - rect.Min.Y;
        return FMath::Min3(HoloKitU(uiScale), w / SOL::HoloMenu::FRAME_MIN_W_PX, h / SOL::HoloMenu::FRAME_MIN_H_PX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the glow pad of every line and dot: lerp(FRAME_FLAT_PAD_PX, HUD_GLOW_PAD_PX, GlowScale(s))
    float HoloKitPad(const float s)
    {
        const float glow = FMath::Clamp(HoloKitS(s), 0.0f, 1.0f);
        return FMath::Lerp(static_cast<float>(SOL::HoloMenu::FRAME_FLAT_PAD_PX),
            static_cast<float>(SOL::Hologram::HUD_GLOW_PAD_PX), glow);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a rect shrunk by inset on every side
    FBox2f HoloKitInset(const FBox2f& rect, const float inset)
    {
        return FBox2f(rect.Min + FVector2f(inset, inset), rect.Max - FVector2f(inset, inset));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a rect grown by padX / padY on every side
    FBox2f HoloKitGrow(const FBox2f& rect, const float padX, const float padY)
    {
        return FBox2f(rect.Min - FVector2f(padX, padY), rect.Max + FVector2f(padX, padY));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a visible look with the given draw-in, fill and hover, its sweep and backdrop derived as Evaluate does
    FSOLHoloMenuLook HoloKitLook(const float frameFraction, const float fillAlpha, const float s,
        const float hoverIntensity, const float sweepFraction)
    {
        FSOLHoloMenuLook look;
        look.bVisible = true;
        look.Progress = 1.0f;
        look.FrameFraction = frameFraction;
        look.FillAlpha = fillAlpha;
        look.HoverIntensity = hoverIntensity;
        look.SweepFraction = sweepFraction;
        look.SweepIntensity = SOLHoloMenu::SweepIntensity(s) * fillAlpha;
        look.BackdropAlpha = SOLHoloMenu::BackdropAlpha(s, fillAlpha);
        return look;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the fully open look at strength s (hover at full glow for s > 0, none at s <= 0, as HoverGlow gives)
    FSOLHoloMenuLook HoloKitOpenLook(const float s)
    {
        const float hover = HoloKitS(s) > 0.0f ? SOL::HoloMenu::HOVER_GLOW_INTENSITY : 0.0f;
        return HoloKitLook(1.0f, 1.0f, s, hover, HOLOKITTEST_SWEEP_FRACTION);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a spec with only a rect, title step and UI scale: no dividers, tabs or hovered item, every field set
    FSOLHoloFrameSpec HoloKitPlainSpec(const FBox2f& rect, const float titleStepWidthPx, const float uiScale)
    {
        FSOLHoloFrameSpec spec;
        spec.Rect = rect;
        spec.TitleStepWidthPx = titleStepWidthPx;
        spec.UiScale = uiScale;
        spec.bColumnDivider = false;
        spec.DividerTop = FVector2f::ZeroVector;
        spec.DividerBottomY = 0.0f;
        spec.OpenTopY = 1.0f;
        spec.OpenBottomY = 0.0f;
        spec.bBrokenDivider = false;
        spec.BrokenStart = FVector2f::ZeroVector;
        spec.BrokenEndX = 0.0f;
        spec.BrokenStepAtPx = 0.0f;
        spec.BrokenStepPx = 0.0f;
        spec.TabCount = 0;
        for (int32 i = 0; i < SOL::HoloMenu::FRAME_MAX_TABS; ++i)
        {
            spec.TabRects[i] = FBox2f(ForceInit);
        }
        spec.SelectedTab = INDEX_NONE;
        spec.TabDividerX = 0.0f;
        spec.HoverItemRect = FBox2f(ForceInit);
        spec.HoverMark = ESOLHoloHoverMark::RowPointer;
        return spec;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an Overlay Menu-like spec on R: every tab (tab 2 selected), the column divider open at the selected
    // tab, the hint divider and a hovered row with the row pointer
    FSOLHoloFrameSpec HoloKitOverlaySpec(const float uiScale)
    {
        FSOLHoloFrameSpec spec = HoloKitPlainSpec(HoloKitRectR(), HOLOKITTEST_R_TITLE, uiScale);
        spec.TabCount = SOL::HoloMenu::FRAME_MAX_TABS;
        for (int32 i = 0; i < SOL::HoloMenu::FRAME_MAX_TABS; ++i)
        {
            spec.TabRects[i] = HoloKitRect(12.0f, 50.0f + 30.0f * i, 80.0f, 72.0f + 30.0f * i);
        }
        spec.SelectedTab = 2;
        spec.TabDividerX = 90.0f;
        spec.bColumnDivider = true;
        spec.DividerTop = FVector2f(90.0f, 44.0f);
        spec.DividerBottomY = 260.0f;
        spec.OpenTopY = 110.0f;
        spec.OpenBottomY = 132.0f;
        spec.bBrokenDivider = true;
        spec.BrokenStart = FVector2f(12.0f, 272.0f);
        spec.BrokenEndX = 380.0f;
        spec.BrokenStepAtPx = 0.0f;
        spec.BrokenStepPx = 0.0f;
        spec.HoverItemRect = HoloKitRect(110.0f, 150.0f, 370.0f, 166.0f);
        spec.HoverMark = ESOLHoloHoverMark::RowPointer;
        return spec;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a spec with every option on, scaled to a rect (tabs, both dividers with a step, a hovered row)
    FSOLHoloFrameSpec HoloKitFullSpecFor(const FBox2f& rect, const float uiScale)
    {
        const float w = rect.Max.X - rect.Min.X;
        const float h = rect.Max.Y - rect.Min.Y;
        FSOLHoloFrameSpec spec = HoloKitPlainSpec(rect, 0.5f * w, uiScale);
        spec.TabCount = SOL::HoloMenu::FRAME_MAX_TABS;
        for (int32 i = 0; i < SOL::HoloMenu::FRAME_MAX_TABS; ++i)
        {
            const float top = rect.Min.Y + h * (0.2f + 0.12f * i);
            spec.TabRects[i] = HoloKitRect(rect.Min.X + 0.05f * w, top, rect.Min.X + 0.2f * w, top + 0.08f * h);
        }
        spec.SelectedTab = 1;
        spec.TabDividerX = rect.Min.X + 0.22f * w;
        spec.bColumnDivider = true;
        spec.DividerTop = FVector2f(rect.Min.X + 0.22f * w, rect.Min.Y + 0.15f * h);
        spec.DividerBottomY = rect.Max.Y - 0.1f * h;
        spec.OpenTopY = spec.TabRects[1].Min.Y;
        spec.OpenBottomY = spec.TabRects[1].Max.Y;
        spec.bBrokenDivider = true;
        spec.BrokenStart = FVector2f(rect.Min.X + 0.05f * w, rect.Max.Y - 0.08f * h);
        spec.BrokenEndX = rect.Max.X - 0.05f * w;
        spec.BrokenStepAtPx = 0.3f * w;
        spec.BrokenStepPx = SOL::HoloMenu::SPEED_DIVIDER_STEP_PX;
        spec.HoverItemRect = HoloKitRect(rect.Min.X + 0.3f * w, rect.Min.Y + 0.5f * h, rect.Max.X - 0.1f * w,
            rect.Min.Y + 0.55f * h);
        spec.HoverMark = ESOLHoloHoverMark::RowPointer;
        return spec;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the worst-case spec of section 4.6 on R: every tab, a 40-dash column divider, a stepped hint divider
    // and a row pointer
    FSOLHoloFrameSpec HoloKitWorstCaseSpec()
    {
        FSOLHoloFrameSpec spec = HoloKitOverlaySpec(1.0f);
        spec.DividerTop = FVector2f(90.0f, 0.0f);
        spec.DividerBottomY = 5000.0f;
        spec.OpenTopY = 1.0f;
        spec.OpenBottomY = 0.0f;
        spec.BrokenStepAtPx = SOL::HoloMenu::SPEED_DIVIDER_STEP_AT_PX;
        spec.BrokenStepPx = SOL::HoloMenu::SPEED_DIVIDER_STEP_PX;
        return spec;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends one expected shape unless its intensity is <= 0 (the builders never write those)
    void HoloKitPush(FSOLHoloKitTestExpected& out, const ESOLHoloPart part, const ESOLHudShapeKind kind,
        const FVector2f& a, const FVector2f& b, const float halfWidthPx, const FLinearColor& color,
        const float intensity, const float fill, const float padPx)
    {
        if (!(intensity > 0.0f) || out.ShapeCount >= HOLOKITTEST_REF_CAPACITY)
        {
            return;
        }
        FSOLHoloShape& shape = out.Shapes[out.ShapeCount];
        shape.Part = part;
        shape.Kind = kind;
        shape.A = a;
        shape.B = b;
        shape.HalfWidthPx = halfWidthPx;
        shape.Color = color;
        shape.Intensity = intensity;
        shape.Fill = fill;
        shape.PadPx = padPx;
        ++out.ShapeCount;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends an expected line: half width FRAME_HALF_WIDTH_PX x u, fill 0
    void HoloKitPushLine(FSOLHoloKitTestExpected& out, const ESOLHoloPart part, const FVector2f& a, const FVector2f& b,
        const float u, const FLinearColor& color, const float intensity, const float padPx)
    {
        HoloKitPush(out, part, ESOLHudShapeKind::Line, a, b, SOL::HoloMenu::FRAME_HALF_WIDTH_PX * u, color, intensity,
            0.0f, padPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends an expected dot: a line with A == B and the radius as its half width
    void HoloKitPushDot(FSOLHoloKitTestExpected& out, const ESOLHoloPart part, const FVector2f& p, const float radius,
        const FLinearColor& color, const float intensity, const float padPx)
    {
        HoloKitPush(out, part, ESOLHudShapeKind::Line, p, p, radius, color, intensity, 0.0f, padPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends the expected fill: a panel over a rect (centre, half size) in the dark backdrop tint
    void HoloKitPushFill(FSOLHoloKitTestExpected& out, const FBox2f& rect, const float fillAlpha)
    {
        const FVector2f center = (rect.Min + rect.Max) * 0.5f;
        const FVector2f half = (rect.Max - rect.Min) * 0.5f;
        HoloKitPush(out, ESOLHoloPart::Fill, ESOLHudShapeKind::Panel, center, half, 0.0f, SOL::HoloMenu::BACKDROP_TINT,
            SOL::Hologram::HUD_PANEL_INTENSITY, SOL::Hologram::HUD_PANEL_FILL * fillAlpha,
            SOL::Hologram::HUD_PANEL_QUAD_PAD_PX);
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends the expected sweep: one line across a rect at Min.Y + SweepFraction x height
    void HoloKitPushSweep(FSOLHoloKitTestExpected& out, const FBox2f& rect, const FSOLHoloMenuLook& look,
        const float u, const float padPx)
    {
        const float y = rect.Min.Y + look.SweepFraction * (rect.Max.Y - rect.Min.Y);
        HoloKitPushLine(out, ESOLHoloPart::Sweep, FVector2f(rect.Min.X, y), FVector2f(rect.Max.X, y), u,
            SOL::HudPalette::PRIMARY, look.SweepIntensity, padPx);
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds the reference output of BuildFrame from the section 4.6 contract and the section 6.3 functions
    void HoloKitReferenceFrame(const FSOLHoloFrameSpec& spec, const FSOLHoloMenuLook& look, const float s,
        FSOLHoloKitTestExpected& out)
    {
        using namespace SOL::HoloMenu;
        out.ShapeCount = 0;
        out.bHasBacking = false;
        if (!look.bVisible || !HoloKitRectValid(spec.Rect))
        {
            return;
        }
        const float u = HoloKitU(spec.UiScale);
        const float strength = HoloKitS(s);
        const float k = HoloKitK(spec.Rect, spec.UiScale);
        const float pad = HoloKitPad(s);
        const float fill = look.FillAlpha;
        const FBox2f backingRect = HoloKitInset(spec.Rect, FRAME_TINT_INSET_PX * k);
        const FLinearColor& primary = SOL::HudPalette::PRIMARY;
        const float bright = FRAME_INTENSITY * fill;
        const float dim = FRAME_INTENSITY * FRAME_DIM_ALPHA * fill;

        if (fill > 0.0f)
        {
            HoloKitPushFill(out, backingRect, fill);
        }

        // Outline and gap dots at the draw-in fraction, at the frame intensity
        if (look.FrameFraction > 0.0f)
        {
            FSOLHoloSegment segments[FRAME_MAX_SEGMENTS];
            const int32 count = SOLHoloMenu::FrameDrawSegments(spec.Rect, spec.TitleStepWidthPx, spec.UiScale,
                look.FrameFraction, segments);
            for (int32 i = 0; i < count; ++i)
            {
                HoloKitPushLine(out, ESOLHoloPart::Outline, segments[i].A, segments[i].B, u, primary, FRAME_INTENSITY,
                    pad);
            }
            FVector2f dots[FRAME_MAX_DOTS];
            const int32 dotCount = SOLHoloMenu::FrameGapDots(spec.Rect, spec.TitleStepWidthPx, spec.UiScale,
                look.FrameFraction, dots);
            for (int32 i = 0; i < dotCount; ++i)
            {
                HoloKitPushDot(out, ESOLHoloPart::GapDot, dots[i], FRAME_DOT_RADIUS_PX * k, primary, FRAME_INTENSITY,
                    pad);
            }
        }

        // Ruler, accents, dividers and tabs fade up with the fill
        if (fill > 0.0f)
        {
            FSOLHoloSegment ruler[FRAME_RULER_TICK_COUNT];
            const int32 rulerCount = SOLHoloMenu::FrameRulerTicks(spec.Rect, spec.UiScale, ruler);
            for (int32 i = 0; i < rulerCount; ++i)
            {
                HoloKitPushLine(out, ESOLHoloPart::Ruler, ruler[i].A, ruler[i].B, u, primary, bright, pad);
            }

            // Accents: pieces 1 (down) and 2 (left) are Accent, pieces 3 and 4 (rightward) are TitleAccent
            FSOLHoloSegment accents[FRAME_ACCENT_COUNT];
            const int32 accentCount = SOLHoloMenu::FrameAccents(spec.Rect, spec.TitleStepWidthPx, spec.UiScale,
                accents);
            for (int32 i = 0; i < accentCount; ++i)
            {
                const bool bTitle = accents[i].B.X > accents[i].A.X;
                if (!bTitle)
                {
                    HoloKitPushLine(out, ESOLHoloPart::Accent, accents[i].A, accents[i].B, u, primary, bright, pad);
                }
            }
            for (int32 i = 0; i < accentCount; ++i)
            {
                const bool bTitle = accents[i].B.X > accents[i].A.X;
                if (bTitle)
                {
                    HoloKitPushLine(out, ESOLHoloPart::TitleAccent, accents[i].A, accents[i].B, u, primary, dim, pad);
                }
            }

            if (spec.bColumnDivider)
            {
                FSOLHoloSegment dashes[DIVIDER_MAX_DASHES];
                const int32 dashCount = SOLHoloMenu::DashedDivider(spec.DividerTop, spec.DividerBottomY, spec.OpenTopY,
                    spec.OpenBottomY, spec.UiScale, dashes);
                for (int32 i = 0; i < dashCount; ++i)
                {
                    HoloKitPushLine(out, ESOLHoloPart::ColumnDivider, dashes[i].A, dashes[i].B, u, primary, dim, pad);
                }
            }
            if (spec.bBrokenDivider)
            {
                FSOLHoloSegment pieces[DIVIDER_MAX_SEGMENTS];
                const int32 pieceCount = SOLHoloMenu::BrokenDivider(spec.BrokenStart, spec.BrokenEndX,
                    spec.BrokenStepAtPx, spec.BrokenStepPx, spec.UiScale, pieces);
                for (int32 i = 0; i < pieceCount; ++i)
                {
                    HoloKitPushLine(out, ESOLHoloPart::BrokenDivider, pieces[i].A, pieces[i].B, u, primary, dim, pad);
                }
            }

            // Tabs in index order, each tab's dot after its pieces; TabCount above FRAME_MAX_TABS reads the maximum
            const int32 tabCount = FMath::Clamp(spec.TabCount, 0, static_cast<int32>(FRAME_MAX_TABS));
            for (int32 tab = 0; tab < tabCount; ++tab)
            {
                const bool bSelected = tab == spec.SelectedTab;
                FSOLHoloSegment pieces[TAB_MAX_SEGMENTS];
                FVector2f dot = FVector2f::ZeroVector;
                int32 dotCount = 0;
                const int32 pieceCount = SOLHoloMenu::TabMarker(spec.TabRects[tab], spec.TabDividerX, bSelected,
                    spec.UiScale, pieces, dot, dotCount);
                for (int32 i = 0; i < pieceCount; ++i)
                {
                    HoloKitPushLine(out, ESOLHoloPart::Tab, pieces[i].A, pieces[i].B, u, primary,
                        bSelected ? bright : dim, pad);
                }
                if (dotCount > 0)
                {
                    HoloKitPushDot(out, ESOLHoloPart::TabDot, dot, FRAME_DOT_RADIUS_PX * u, primary, bright, pad);
                }
            }
        }

        if (look.SweepIntensity > 0.0f)
        {
            HoloKitPushSweep(out, backingRect, look, u, pad);
        }

        // Hover mark: the glow times the fill for s > 0, the flat frame intensity times the fill at s = 0 (D18)
        if (HoloKitRectValid(spec.HoverItemRect))
        {
            const float hoverIntensity = strength > 0.0f ? look.HoverIntensity * fill : FRAME_INTENSITY * fill;
            if (hoverIntensity > 0.0f)
            {
                const FBox2f inflated = HoloKitGrow(spec.HoverItemRect, HOVER_PAD_X_PX * u, HOVER_PAD_Y_PX * u);
                FSOLHoloSegment pieces[HOVER_MAX_SEGMENTS];
                FVector2f dot = FVector2f::ZeroVector;
                int32 dotCount = 0;
                const int32 pieceCount = SOLHoloMenu::HoverMark(inflated, spec.HoverMark, spec.UiScale, pieces, dot,
                    dotCount);
                for (int32 i = 0; i < pieceCount; ++i)
                {
                    HoloKitPushLine(out, ESOLHoloPart::Hover, pieces[i].A, pieces[i].B, u, SOL::HudPalette::SECONDARY,
                        hoverIntensity, pad);
                }
                if (dotCount > 0)
                {
                    HoloKitPushDot(out, ESOLHoloPart::HoverDot, dot, FRAME_DOT_RADIUS_PX * u,
                        SOL::HudPalette::SECONDARY, hoverIntensity, pad);
                }
            }
        }

        if (look.BackdropAlpha > 0.0f)
        {
            out.bHasBacking = true;
            out.Backing.Rect = backingRect;
            out.Backing.CornerPx = 0.0f;
            out.Backing.FeatherPx = SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX * u;
            out.Backing.Alpha = look.BackdropAlpha;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the card rect: the content grown by HUD_PANEL_PADDING_PX x u
    FBox2f HoloKitCardRect(const FBox2f& contentRect, const float uiScale)
    {
        const float pad = SOL::Hologram::HUD_PANEL_PADDING_PX * HoloKitU(uiScale);
        return HoloKitGrow(contentRect, pad, pad);
    }

    //////////////////////////////////////////////////////////////////////////
    // Builds the reference output of BuildCard from the section 4.6 contract
    void HoloKitReferenceCard(const FBox2f& contentRect, const float uiScale, const FSOLHoloMenuLook& look,
        const float s, FSOLHoloKitTestExpected& out)
    {
        using namespace SOL::HoloMenu;
        out.ShapeCount = 0;
        out.bHasBacking = false;
        if (!look.bVisible || !HoloKitRectValid(contentRect))
        {
            return;
        }
        const float u = HoloKitU(uiScale);
        const float pad = HoloKitPad(s);
        const FBox2f cardRect = HoloKitCardRect(contentRect, uiScale);

        if (look.FillAlpha > 0.0f)
        {
            HoloKitPushFill(out, cardRect, look.FillAlpha);
        }
        FSOLHoloSegment ticks[CARD_TICK_SEGMENTS];
        const int32 tickCount = SOLHoloMenu::CardCornerTicks(cardRect, CARD_TICK_PX * u, look.FrameFraction, ticks);
        for (int32 i = 0; i < tickCount; ++i)
        {
            HoloKitPushLine(out, ESOLHoloPart::CardTick, ticks[i].A, ticks[i].B, u, SOL::HudPalette::PRIMARY,
                FRAME_INTENSITY, pad);
        }
        if (look.SweepIntensity > 0.0f)
        {
            HoloKitPushSweep(out, cardRect, look, u, pad);
        }
        if (look.BackdropAlpha > 0.0f)
        {
            out.bHasBacking = true;
            out.Backing.Rect = cardRect;
            out.Backing.CornerPx = static_cast<float>(SOL::Hologram::HUD_PANEL_CORNER_PX) * u;
            out.Backing.FeatherPx = SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX * u;
            out.Backing.Alpha = look.BackdropAlpha;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the position tolerance of a part: 0 (bit for bit) for groups copied from a section 6.3 function, a small
    // one for positions rebuilt here from rect arithmetic
    float HoloKitPositionTolerance(const ESOLHoloPart part)
    {
        switch (part)
        {
            case ESOLHoloPart::Fill:
            case ESOLHoloPart::Sweep:
            case ESOLHoloPart::Hover:
            case ESOLHoloPart::HoverDot:
            case ESOLHoloPart::CardTick:
                return HOLOKITTEST_PX_TOLERANCE;
            default:
                return 0.0f;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two points are within a tolerance of each other on both axes
    bool HoloKitNear(const FVector2f& a, const FVector2f& b, const float tolerance)
    {
        return FMath::Abs(a.X - b.X) <= tolerance && FMath::Abs(a.Y - b.Y) <= tolerance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a built shape matches its expected shape; describes the first difference
    bool HoloKitShapeMatches(const FSOLHoloShape& actual, const FSOLHoloShape& expected, FString& outWhy)
    {
        if (actual.Part != expected.Part)
        {
            outWhy = FString::Printf(TEXT("part %d, expected %d"), static_cast<int32>(actual.Part),
                static_cast<int32>(expected.Part));
            return false;
        }
        if (actual.Kind != expected.Kind)
        {
            outWhy = FString::Printf(TEXT("part %d: kind %d, expected %d"), static_cast<int32>(expected.Part),
                static_cast<int32>(actual.Kind), static_cast<int32>(expected.Kind));
            return false;
        }

        // Ends (the sweep may run across in either direction)
        const float tolerance = HoloKitPositionTolerance(expected.Part);
        bool bEndsMatch = HoloKitNear(actual.A, expected.A, tolerance) && HoloKitNear(actual.B, expected.B, tolerance);
        if (!bEndsMatch && expected.Part == ESOLHoloPart::Sweep)
        {
            bEndsMatch = HoloKitNear(actual.A, expected.B, tolerance) && HoloKitNear(actual.B, expected.A, tolerance);
        }
        if (!bEndsMatch)
        {
            outWhy = FString::Printf(
                TEXT("part %d: A (%.4f, %.4f) B (%.4f, %.4f), expected A (%.4f, %.4f) B (%.4f, %.4f)"),
                static_cast<int32>(expected.Part), actual.A.X, actual.A.Y, actual.B.X, actual.B.Y, expected.A.X,
                expected.A.Y, expected.B.X, expected.B.Y);
            return false;
        }

        // Size, colour, intensity, fill and pad
        if (expected.Kind == ESOLHudShapeKind::Line
            && FMath::Abs(actual.HalfWidthPx - expected.HalfWidthPx) > HOLOKITTEST_VALUE_TOLERANCE)
        {
            outWhy = FString::Printf(TEXT("part %d: half width %.6f, expected %.6f"), static_cast<int32>(expected.Part),
                actual.HalfWidthPx, expected.HalfWidthPx);
            return false;
        }
        if (actual.Color != expected.Color)
        {
            outWhy = FString::Printf(TEXT("part %d: colour %s, expected %s"), static_cast<int32>(expected.Part),
                *actual.Color.ToString(), *expected.Color.ToString());
            return false;
        }
        if (FMath::Abs(actual.Intensity - expected.Intensity) > HOLOKITTEST_VALUE_TOLERANCE)
        {
            outWhy = FString::Printf(TEXT("part %d: intensity %.6f, expected %.6f"), static_cast<int32>(expected.Part),
                actual.Intensity, expected.Intensity);
            return false;
        }
        if (FMath::Abs(actual.Fill - expected.Fill) > HOLOKITTEST_VALUE_TOLERANCE)
        {
            outWhy = FString::Printf(TEXT("part %d: fill %.6f, expected %.6f"), static_cast<int32>(expected.Part),
                actual.Fill, expected.Fill);
            return false;
        }
        if (FMath::Abs(actual.PadPx - expected.PadPx) > HOLOKITTEST_PAD_TOLERANCE)
        {
            outWhy = FString::Printf(TEXT("part %d: pad %.6f, expected %.6f"), static_cast<int32>(expected.Part),
                actual.PadPx, expected.PadPx);
            return false;
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns "part:count" for every part present in the first count shapes (failure descriptions)
    FString HoloKitPartSummary(const FSOLHoloShape* shapes, const int32 count)
    {
        int32 perPart[HOLOKITTEST_PART_COUNT] = {};
        for (int32 i = 0; i < count; ++i)
        {
            const int32 part = static_cast<int32>(shapes[i].Part);
            if (part >= 0 && part < HOLOKITTEST_PART_COUNT)
            {
                ++perPart[part];
            }
        }
        FString summary;
        for (int32 part = 0; part < HOLOKITTEST_PART_COUNT; ++part)
        {
            if (perPart[part] > 0)
            {
                summary += FString::Printf(TEXT("%d:%d "), part, perPart[part]);
            }
        }
        return summary;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a built output matches the reference (count, every shape in order, the backing); describes the
    // first difference
    bool HoloKitOutputMatches(const FSOLHoloFrameOutput& actual, const FSOLHoloKitTestExpected& expected,
        FString& outWhy)
    {
        if (actual.ShapeCount != expected.ShapeCount)
        {
            const int32 shown = FMath::Clamp(actual.ShapeCount, 0, static_cast<int32>(SOL::HoloMenu::FRAME_MAX_SHAPES));
            outWhy = FString::Printf(TEXT("shape count %d [%s], expected %d [%s]"), actual.ShapeCount,
                *HoloKitPartSummary(actual.Shapes, shown), expected.ShapeCount,
                *HoloKitPartSummary(expected.Shapes, expected.ShapeCount));
            return false;
        }
        for (int32 i = 0; i < expected.ShapeCount; ++i)
        {
            FString shapeWhy;
            if (!HoloKitShapeMatches(actual.Shapes[i], expected.Shapes[i], shapeWhy))
            {
                outWhy = FString::Printf(TEXT("shape %d: %s"), i, *shapeWhy);
                return false;
            }
        }
        if (actual.bHasBacking != expected.bHasBacking)
        {
            outWhy = FString::Printf(TEXT("backing %d, expected %d"), actual.bHasBacking ? 1 : 0,
                expected.bHasBacking ? 1 : 0);
            return false;
        }
        if (expected.bHasBacking)
        {
            const FSOLHoloBacking& a = actual.Backing;
            const FSOLHoloBacking& e = expected.Backing;
            if (!HoloKitNear(a.Rect.Min, e.Rect.Min, HOLOKITTEST_PX_TOLERANCE)
                || !HoloKitNear(a.Rect.Max, e.Rect.Max, HOLOKITTEST_PX_TOLERANCE))
            {
                outWhy = FString::Printf(
                    TEXT("backing rect (%.4f, %.4f)-(%.4f, %.4f), expected (%.4f, %.4f)-(%.4f, %.4f)"), a.Rect.Min.X,
                    a.Rect.Min.Y, a.Rect.Max.X, a.Rect.Max.Y, e.Rect.Min.X, e.Rect.Min.Y, e.Rect.Max.X, e.Rect.Max.Y);
                return false;
            }
            if (FMath::Abs(a.CornerPx - e.CornerPx) > HOLOKITTEST_VALUE_TOLERANCE
                || FMath::Abs(a.FeatherPx - e.FeatherPx) > HOLOKITTEST_VALUE_TOLERANCE
                || FMath::Abs(a.Alpha - e.Alpha) > HOLOKITTEST_VALUE_TOLERANCE)
            {
                outWhy = FString::Printf(TEXT("backing corner %.4f feather %.4f alpha %.6f, expected %.4f %.4f %.6f"),
                    a.CornerPx, a.FeatherPx, a.Alpha, e.CornerPx, e.FeatherPx, e.Alpha);
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks a built output against the reference, reporting the first difference as an error
    void HoloKitTestOutput(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameOutput& actual,
        const FSOLHoloKitTestExpected& expected)
    {
        FString why;
        if (!test.TestTrue(label + TEXT(": matches the section 4.6 contract"), HoloKitOutputMatches(actual, expected,
            why)))
        {
            test.AddError(label + TEXT(": ") + why);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether two outputs are identical (every specified field exactly; a panel's half width is not specified)
    bool HoloKitSameOutput(const FSOLHoloFrameOutput& a, const FSOLHoloFrameOutput& b, FString& outWhy)
    {
        if (a.ShapeCount != b.ShapeCount || a.bHasBacking != b.bHasBacking)
        {
            outWhy = FString::Printf(TEXT("count %d / %d, backing %d / %d"), a.ShapeCount, b.ShapeCount,
                a.bHasBacking ? 1 : 0, b.bHasBacking ? 1 : 0);
            return false;
        }
        const int32 count = FMath::Clamp(a.ShapeCount, 0, static_cast<int32>(SOL::HoloMenu::FRAME_MAX_SHAPES));
        for (int32 i = 0; i < count; ++i)
        {
            const FSOLHoloShape& x = a.Shapes[i];
            const FSOLHoloShape& y = b.Shapes[i];
            const bool bHalfWidthSame = x.Kind == ESOLHudShapeKind::Panel || x.HalfWidthPx == y.HalfWidthPx;
            if (x.Part != y.Part || x.Kind != y.Kind || x.A != y.A || x.B != y.B || !bHalfWidthSame
                || x.Color != y.Color || x.Intensity != y.Intensity || x.Fill != y.Fill || x.PadPx != y.PadPx)
            {
                outWhy = FString::Printf(TEXT("shape %d differs (part %d / %d)"), i, static_cast<int32>(x.Part),
                    static_cast<int32>(y.Part));
                return false;
            }
        }
        if (a.bHasBacking)
        {
            const FSOLHoloBacking& x = a.Backing;
            const FSOLHoloBacking& y = b.Backing;
            if (x.Rect.Min != y.Rect.Min || x.Rect.Max != y.Rect.Max || x.CornerPx != y.CornerPx
                || x.FeatherPx != y.FeatherPx || x.Alpha != y.Alpha)
            {
                outWhy = TEXT("backing differs");
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that two outputs are identical, reporting the first difference as an error
    void HoloKitTestSame(FAutomationTestBase& test, const FString& label, const FSOLHoloFrameOutput& a,
        const FSOLHoloFrameOutput& b)
    {
        FString why;
        if (!test.TestTrue(label, HoloKitSameOutput(a, b, why)))
        {
            test.AddError(label + TEXT(": ") + why);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many shapes of a part a built output holds
    int32 HoloKitCount(const FSOLHoloFrameOutput& out, const ESOLHoloPart part)
    {
        const int32 count = FMath::Clamp(out.ShapeCount, 0, static_cast<int32>(SOL::HoloMenu::FRAME_MAX_SHAPES));
        int32 found = 0;
        for (int32 i = 0; i < count; ++i)
        {
            found += out.Shapes[i].Part == part ? 1 : 0;
        }
        return found;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how many shapes of a part the reference holds
    int32 HoloKitCountExpected(const FSOLHoloKitTestExpected& out, const ESOLHoloPart part)
    {
        int32 found = 0;
        for (int32 i = 0; i < out.ShapeCount; ++i)
        {
            found += out.Shapes[i].Part == part ? 1 : 0;
        }
        return found;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the index of a part's first shape in a built output, or INDEX_NONE
    int32 HoloKitFirst(const FSOLHoloFrameOutput& out, const ESOLHoloPart part)
    {
        const int32 count = FMath::Clamp(out.ShapeCount, 0, static_cast<int32>(SOL::HoloMenu::FRAME_MAX_SHAPES));
        for (int32 i = 0; i < count; ++i)
        {
            if (out.Shapes[i].Part == part)
            {
                return i;
            }
        }
        return INDEX_NONE;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills an output with garbage: a stale count, a stale backing and nonsense shapes
    void HoloKitDirty(FSOLHoloFrameOutput& out)
    {
        for (int32 i = 0; i < SOL::HoloMenu::FRAME_MAX_SHAPES; ++i)
        {
            FSOLHoloShape& shape = out.Shapes[i];
            shape.Part = ESOLHoloPart::Hover;
            shape.Kind = ESOLHudShapeKind::Panel;
            shape.A = FVector2f(HOLOKITTEST_SENTINEL, HOLOKITTEST_SENTINEL);
            shape.B = FVector2f(HOLOKITTEST_SENTINEL, HOLOKITTEST_SENTINEL);
            shape.HalfWidthPx = 77.0f;
            shape.Color = FLinearColor(0.3f, 0.2f, 0.1f, 0.5f);
            shape.Intensity = 9.0f;
            shape.Fill = 9.0f;
            shape.PadPx = 99.0f;
        }
        out.ShapeCount = 123;
        out.bHasBacking = true;
        out.Backing.Rect = HoloKitRect(-5.0f, -5.0f, 5.0f, 5.0f);
        out.Backing.CornerPx = 3.0f;
        out.Backing.FeatherPx = 4.0f;
        out.Backing.Alpha = 0.9f;
    }

    //////////////////////////////////////////////////////////////////////////
    // Fills the guard shapes after a guarded output with the sentinel
    void HoloKitFillGuard(FSOLHoloKitTestGuardedOutput& guarded)
    {
        for (int32 i = 0; i < HOLOKITTEST_GUARD; ++i)
        {
            guarded.Guard[i].A = FVector2f(HOLOKITTEST_SENTINEL, HOLOKITTEST_SENTINEL);
            guarded.Guard[i].B = FVector2f(HOLOKITTEST_SENTINEL, HOLOKITTEST_SENTINEL);
            guarded.Guard[i].Intensity = HOLOKITTEST_SENTINEL;
            guarded.Guard[i].PadPx = HOLOKITTEST_SENTINEL;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether the guard shapes after a guarded output still hold the sentinel
    bool HoloKitGuardIntact(const FSOLHoloKitTestGuardedOutput& guarded)
    {
        for (int32 i = 0; i < HOLOKITTEST_GUARD; ++i)
        {
            const FSOLHoloShape& g = guarded.Guard[i];
            if (g.A.X != HOLOKITTEST_SENTINEL || g.A.Y != HOLOKITTEST_SENTINEL || g.B.X != HOLOKITTEST_SENTINEL
                || g.B.Y != HOLOKITTEST_SENTINEL || g.Intensity != HOLOKITTEST_SENTINEL
                || g.PadPx != HOLOKITTEST_SENTINEL)
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Checks that every vertex of two batches is identical (position, local frame, kind, colour) and so are indices
    void HoloKitTestSameBatch(FAutomationTestBase& test, const FString& label, const FSOLHudShapeBatch& actual,
        const FSOLHudShapeBatch& expected)
    {
        const TArray<FSlateVertex>& a = actual.GetVertices();
        const TArray<FSlateVertex>& e = expected.GetVertices();
        if (!test.TestEqual(label + TEXT(": vertex count"), a.Num(), e.Num()))
        {
            return;
        }
        int32 mismatches = 0;
        for (int32 i = 0; i < a.Num(); ++i)
        {
            const bool bSame = a[i].Position == e[i].Position && a[i].TexCoords[0] == e[i].TexCoords[0]
                && a[i].TexCoords[1] == e[i].TexCoords[1] && a[i].TexCoords[2] == e[i].TexCoords[2]
                && a[i].TexCoords[3] == e[i].TexCoords[3] && a[i].Color == e[i].Color;
            if (!bSame && mismatches++ == 0)
            {
                test.AddError(FString::Printf(TEXT("%s: vertex %d differs from the direct AddLine / AddPanel call"),
                    *label, i));
            }
        }
        test.TestEqual(label + TEXT(": vertices differing from the direct calls"), mismatches, 0);
        const TArray<SlateIndex>& ai = actual.GetIndices();
        const TArray<SlateIndex>& ei = expected.GetIndices();
        if (test.TestEqual(label + TEXT(": index count"), ai.Num(), ei.Num()))
        {
            bool bIndicesSame = true;
            for (int32 i = 0; i < ai.Num(); ++i)
            {
                bIndicesSame &= ai[i] == ei[i];
            }
            test.TestTrue(label + TEXT(": indices identical"), bIndicesSame);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Adds a built output's shapes to a batch by the direct AddLine / AddPanel calls the contract names
    void HoloKitAddDirect(const FSOLHoloFrameOutput& in, FSOLHudShapeBatch& batch)
    {
        const int32 count = FMath::Clamp(in.ShapeCount, 0, static_cast<int32>(SOL::HoloMenu::FRAME_MAX_SHAPES));
        for (int32 i = 0; i < count; ++i)
        {
            const FSOLHoloShape& shape = in.Shapes[i];
            if (shape.Kind == ESOLHudShapeKind::Panel)
            {
                batch.AddPanel(shape.A, shape.B, shape.Color, shape.Intensity, shape.Fill, shape.PadPx);
            }
            else
            {
                batch.AddLine(shape.A, shape.B, shape.HalfWidthPx, shape.Color, shape.Intensity, shape.PadPx);
            }
        }
    }
}

// --- FrameScale (section 4.6, added to SOLHoloMenuMath) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitFrameScaleTest, "SOLTest.HoloMenuKit.FrameScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// FrameScale is k = min(u, w / FRAME_MIN_W_PX, h / FRAME_MIN_H_PX) with the section 6.3 uiScale rule; 0 when invalid
bool FSOLHoloMenuKitFrameScaleTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloMenu::FrameScale;
    const FBox2f rectR = HoloKitRectR();

    // Section 6.4 values
    TestEqual(TEXT("R at uiScale 1"), FrameScale(rectR, 1.0f), 1.0f, HOLOKITTEST_VALUE_TOLERANCE);
    TestEqual(TEXT("R at uiScale 2"), FrameScale(rectR, 2.0f), 2.0f, HOLOKITTEST_VALUE_TOLERANCE);
    TestEqual(TEXT("(0,0)-(17,32) at uiScale 1"), FrameScale(HoloKitRect(0.0f, 0.0f, 17.0f, 32.0f), 1.0f), 0.5f,
        HOLOKITTEST_VALUE_TOLERANCE);

    // uiScale NaN, infinite or <= 0 reads 1
    const float badScales[] = { HOLOKITTEST_NAN, HOLOKITTEST_INF, -HOLOKITTEST_INF, 0.0f, -1.0f };
    for (const float uiScale : badScales)
    {
        TestEqual(FString::Printf(TEXT("R at uiScale %f reads 1"), uiScale), FrameScale(rectR, uiScale), 1.0f,
            HOLOKITTEST_VALUE_TOLERANCE);
    }

    // The clamp to [UI_SCALE_MIN, UI_SCALE_MAX], then the size limit
    TestEqual(TEXT("R at uiScale 0.05 clamps to UI_SCALE_MIN"), FrameScale(rectR, 0.05f),
        SOL::HoloMenu::UI_SCALE_MIN, HOLOKITTEST_VALUE_TOLERANCE);
    TestEqual(TEXT("R at uiScale 100: clamped to 16, then limited by h / FRAME_MIN_H_PX"), FrameScale(rectR, 100.0f),
        300.0f / SOL::HoloMenu::FRAME_MIN_H_PX, HOLOKITTEST_VALUE_TOLERANCE);
    TestEqual(TEXT("(0,0)-(4000,2000) at uiScale 3"), FrameScale(HoloKitRect(0.0f, 0.0f, 4000.0f, 2000.0f), 3.0f),
        3.0f, HOLOKITTEST_VALUE_TOLERANCE);
    TestEqual(TEXT("narrow (0,0)-(17,640) at uiScale 1: w / FRAME_MIN_W_PX"),
        FrameScale(HoloKitRect(0.0f, 0.0f, 17.0f, 640.0f), 1.0f), 17.0f / SOL::HoloMenu::FRAME_MIN_W_PX,
        HOLOKITTEST_VALUE_TOLERANCE);

    // Invalid rects give 0
    const FBox2f invalidRects[] = {
        FBox2f(ForceInit), HoloKitRect(0.0f, 0.0f, 0.0f, 300.0f), HoloKitRect(0.0f, 300.0f, 400.0f, 0.0f),
        HoloKitRect(0.0f, 0.0f, HOLOKITTEST_NAN, 300.0f), HoloKitRect(0.0f, 0.0f, 400.0f, HOLOKITTEST_INF) };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(invalidRects)); ++i)
    {
        TestEqual(FString::Printf(TEXT("invalid rect %d: 0"), i), FrameScale(invalidRects[i], 1.0f), 0.0f);
    }
    return true;
}

// --- Frame builder (section 4.6) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitFrameHiddenTest, "SOLTest.HoloMenuKit.FrameHidden",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// An invisible look or an invalid rect gives 0 shapes and no backing, even into a dirty output
bool FSOLHoloMenuKitFrameHiddenTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloFrameBuild::BuildFrame;
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();

    // Not visible (a Closed menu), every other look field full
    {
        FSOLHoloMenuLook look = HoloKitOpenLook(1.0f);
        look.bVisible = false;
        HoloKitDirty(*out);
        const int32 count = BuildFrame(HoloKitOverlaySpec(1.0f), look, 1.0f, *out);
        TestEqual(TEXT("invisible: returns 0"), count, 0);
        TestEqual(TEXT("invisible: ShapeCount 0"), out->ShapeCount, 0);
        TestFalse(TEXT("invisible: no backing"), out->bHasBacking);
    }

    // Invalid rects
    const FBox2f invalidRects[] = {
        FBox2f(ForceInit), HoloKitRect(0.0f, 0.0f, 0.0f, 300.0f), HoloKitRect(0.0f, 300.0f, 400.0f, 0.0f),
        HoloKitRect(0.0f, 0.0f, HOLOKITTEST_NAN, 300.0f), HoloKitRect(-HOLOKITTEST_INF, 0.0f, 400.0f, 300.0f) };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(invalidRects)); ++i)
    {
        FSOLHoloFrameSpec spec = HoloKitOverlaySpec(1.0f);
        spec.Rect = invalidRects[i];
        HoloKitDirty(*out);
        const int32 count = BuildFrame(spec, HoloKitOpenLook(1.0f), 1.0f, *out);
        TestEqual(FString::Printf(TEXT("invalid rect %d: returns 0"), i), count, 0);
        TestEqual(FString::Printf(TEXT("invalid rect %d: ShapeCount 0"), i), out->ShapeCount, 0);
        TestFalse(FString::Printf(TEXT("invalid rect %d: no backing"), i), out->bHasBacking);
    }

    // Visible at progress 0 (Opening just begun): nothing drawn yet, no backing
    {
        HoloKitDirty(*out);
        const int32 count = BuildFrame(HoloKitOverlaySpec(1.0f), HoloKitLook(0.0f, 0.0f, 1.0f, 0.0f, 0.0f), 1.0f,
            *out);
        TestEqual(TEXT("progress 0: returns 0"), count, 0);
        TestEqual(TEXT("progress 0: ShapeCount 0"), out->ShapeCount, 0);
        TestFalse(TEXT("progress 0: no backing"), out->bHasBacking);
    }

    // The return value is out.ShapeCount
    {
        const int32 count = BuildFrame(HoloKitOverlaySpec(1.0f), HoloKitOpenLook(1.0f), 1.0f, *out);
        TestTrue(TEXT("open: some shapes"), count > 0);
        TestEqual(TEXT("open: returns ShapeCount"), count, out->ShapeCount);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitFrameFullOpenTest, "SOLTest.HoloMenuKit.FrameFullOpen",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A fully open Overlay Menu frame at strength 1: every group, in the contract's order, equal to its section 6.3
// function, with the backing and the right part counts
bool FSOLHoloMenuKitFrameFullOpenTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const FSOLHoloFrameSpec spec = HoloKitOverlaySpec(1.0f);
    const FSOLHoloMenuLook look = HoloKitOpenLook(1.0f);
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloKitTestExpected> expected = MakeUnique<FSOLHoloKitTestExpected>();

    const int32 count = SOLHoloFrameBuild::BuildFrame(spec, look, 1.0f, *out);
    HoloKitReferenceFrame(spec, look, 1.0f, *expected);
    TestEqual(TEXT("returns ShapeCount"), count, out->ShapeCount);
    HoloKitTestOutput(*this, TEXT("overlay, open, s = 1"), *out, *expected);

    // Part counts on R (section 6.4 worked example)
    TestEqual(TEXT("1 Fill"), HoloKitCount(*out, ESOLHoloPart::Fill), 1);
    TestEqual(TEXT("19 Outline"), HoloKitCount(*out, ESOLHoloPart::Outline), HOLOKITTEST_R_OUTLINE_PIECES);
    TestEqual(TEXT("14 GapDot"), HoloKitCount(*out, ESOLHoloPart::GapDot), HOLOKITTEST_R_GAP_DOTS);
    TestEqual(TEXT("6 Ruler"), HoloKitCount(*out, ESOLHoloPart::Ruler), HOLOKITTEST_R_RULER_TICKS);
    TestEqual(TEXT("2 Accent"), HoloKitCount(*out, ESOLHoloPart::Accent), 2);
    TestEqual(TEXT("2 TitleAccent"), HoloKitCount(*out, ESOLHoloPart::TitleAccent), 2);
    TestEqual(TEXT("ColumnDivider as DashedDivider"), HoloKitCount(*out, ESOLHoloPart::ColumnDivider),
        HoloKitCountExpected(*expected, ESOLHoloPart::ColumnDivider));
    TestTrue(TEXT("some ColumnDivider dashes"), HoloKitCount(*out, ESOLHoloPart::ColumnDivider) > 0);
    TestEqual(TEXT("3 BrokenDivider (no step)"), HoloKitCount(*out, ESOLHoloPart::BrokenDivider), 3);
    TestEqual(TEXT("Tab pieces: 4 selected + 3 per unselected tab"), HoloKitCount(*out, ESOLHoloPart::Tab),
        4 + 3 * (FRAME_MAX_TABS - 1));
    TestEqual(TEXT("1 TabDot (selected tab)"), HoloKitCount(*out, ESOLHoloPart::TabDot), 1);
    TestEqual(TEXT("1 Sweep"), HoloKitCount(*out, ESOLHoloPart::Sweep), 1);
    TestEqual(TEXT("8 Hover (row pointer)"), HoloKitCount(*out, ESOLHoloPart::Hover), HOLOKITTEST_ROW_POINTER_PIECES);
    TestEqual(TEXT("1 HoverDot"), HoloKitCount(*out, ESOLHoloPart::HoverDot), 1);
    TestEqual(TEXT("no CardTick in a frame"), HoloKitCount(*out, ESOLHoloPart::CardTick), 0);

    // The group order: Fill first, the hover dot last, the sweep just before the hover
    TestEqual(TEXT("Fill is shape 0"), HoloKitFirst(*out, ESOLHoloPart::Fill), 0);
    TestEqual(TEXT("Outline starts at 1"), HoloKitFirst(*out, ESOLHoloPart::Outline), 1);
    TestEqual(TEXT("HoverDot is the last shape"), HoloKitFirst(*out, ESOLHoloPart::HoverDot), out->ShapeCount - 1);
    TestEqual(TEXT("Sweep right before the Hover pieces"), HoloKitFirst(*out, ESOLHoloPart::Sweep) + 1,
        HoloKitFirst(*out, ESOLHoloPart::Hover));

    // The selected tab's dot follows its own 4 pieces (tab 2, after 2 unselected tabs of 3 pieces)
    const int32 firstTab = HoloKitFirst(*out, ESOLHoloPart::Tab);
    TestEqual(TEXT("TabDot after tab 0-1 (3 + 3) and tab 2 (4) pieces"), HoloKitFirst(*out, ESOLHoloPart::TabDot),
        firstTab + 3 + 3 + 4);

    // Backing: R inset by FRAME_TINT_INSET_PX x k (k = 1), plain corner, the HUD feather, the look's alpha
    if (TestTrue(TEXT("backing present"), out->bHasBacking))
    {
        TestEqual(TEXT("backing min x"), out->Backing.Rect.Min.X, 2.0f, HOLOKITTEST_PX_TOLERANCE);
        TestEqual(TEXT("backing min y"), out->Backing.Rect.Min.Y, 2.0f, HOLOKITTEST_PX_TOLERANCE);
        TestEqual(TEXT("backing max x"), out->Backing.Rect.Max.X, 398.0f, HOLOKITTEST_PX_TOLERANCE);
        TestEqual(TEXT("backing max y"), out->Backing.Rect.Max.Y, 298.0f, HOLOKITTEST_PX_TOLERANCE);
        TestEqual(TEXT("backing corner 0"), out->Backing.CornerPx, 0.0f);
        TestEqual(TEXT("backing feather"), out->Backing.FeatherPx, SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX,
            HOLOKITTEST_VALUE_TOLERANCE);
        TestEqual(TEXT("backing alpha = BACKDROP_ALPHA_PER_UNIT at s = 1"), out->Backing.Alpha,
            BACKDROP_ALPHA_PER_UNIT, HOLOKITTEST_VALUE_TOLERANCE);
    }

    // Never above the capacity
    TestTrue(TEXT("within FRAME_MAX_SHAPES"), out->ShapeCount <= FRAME_MAX_SHAPES);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitFrameGroupsTest, "SOLTest.HoloMenuKit.FrameGroupsByLook",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Which groups appear follows the look: outline with the draw-in, the fill-faded groups with the fill, the sweep
// with its intensity, the hover with a valid item and a positive intensity, the backing with the backdrop alpha
bool FSOLHoloMenuKitFrameGroupsTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloKitTestExpected> expected = MakeUnique<FSOLHoloKitTestExpected>();
    const FSOLHoloFrameSpec overlay = HoloKitOverlaySpec(1.0f);

    // Builds one case, compares it with the reference and returns nothing
    auto buildCase = [&](const TCHAR* label, const FSOLHoloFrameSpec& spec, const FSOLHoloMenuLook& look,
        const float s)
    {
        HoloKitDirty(*out);
        SOLHoloFrameBuild::BuildFrame(spec, look, s, *out);
        HoloKitReferenceFrame(spec, look, s, *expected);
        HoloKitTestOutput(*this, label, *out, *expected);
    };

    // Mid draw-in, fill not started: outline and gap dots only, no backing
    buildCase(TEXT("draw-in 0.5, fill 0"), overlay, HoloKitLook(0.5f, 0.0f, 1.0f, HOVER_GLOW_INTENSITY, 0.25f), 1.0f);
    TestTrue(TEXT("draw-in 0.5: some outline"), HoloKitCount(*out, ESOLHoloPart::Outline) > 0);
    TestEqual(TEXT("draw-in 0.5: no fill"), HoloKitCount(*out, ESOLHoloPart::Fill), 0);
    TestEqual(TEXT("draw-in 0.5: no ruler"), HoloKitCount(*out, ESOLHoloPart::Ruler), 0);
    TestEqual(TEXT("draw-in 0.5: no accent"), HoloKitCount(*out, ESOLHoloPart::Accent), 0);
    TestEqual(TEXT("draw-in 0.5: no title accent"), HoloKitCount(*out, ESOLHoloPart::TitleAccent), 0);
    TestEqual(TEXT("draw-in 0.5: no column divider"), HoloKitCount(*out, ESOLHoloPart::ColumnDivider), 0);
    TestEqual(TEXT("draw-in 0.5: no broken divider"), HoloKitCount(*out, ESOLHoloPart::BrokenDivider), 0);
    TestEqual(TEXT("draw-in 0.5: no tab"), HoloKitCount(*out, ESOLHoloPart::Tab), 0);
    TestEqual(TEXT("draw-in 0.5: no sweep"), HoloKitCount(*out, ESOLHoloPart::Sweep), 0);
    TestEqual(TEXT("draw-in 0.5: no hover (intensity x fill 0)"), HoloKitCount(*out, ESOLHoloPart::Hover), 0);
    TestFalse(TEXT("draw-in 0.5: no backing"), out->bHasBacking);

    // Fill half way: every group, the fill-faded ones at half intensity
    buildCase(TEXT("draw-in 1, fill 0.5"), overlay, HoloKitLook(1.0f, 0.5f, 1.0f, HOVER_GLOW_INTENSITY, 0.6f), 1.0f);
    TestEqual(TEXT("fill 0.5: 1 sweep"), HoloKitCount(*out, ESOLHoloPart::Sweep), 1);
    TestTrue(TEXT("fill 0.5: backing"), out->bHasBacking);

    // Hover just began (its glow still 0 at s > 0): no hover mark
    buildCase(TEXT("hover glow 0 at s = 1"), overlay, HoloKitLook(1.0f, 1.0f, 1.0f, 0.0f, 0.25f), 1.0f);
    TestEqual(TEXT("hover glow 0: no Hover"), HoloKitCount(*out, ESOLHoloPart::Hover), 0);
    TestEqual(TEXT("hover glow 0: no HoverDot"), HoloKitCount(*out, ESOLHoloPart::HoverDot), 0);

    // Nothing hovered (an invalid rect, or NaN corners): no hover mark
    {
        FSOLHoloFrameSpec spec = overlay;
        spec.HoverItemRect = FBox2f(ForceInit);
        buildCase(TEXT("nothing hovered"), spec, HoloKitOpenLook(1.0f), 1.0f);
        TestEqual(TEXT("nothing hovered: no Hover"), HoloKitCount(*out, ESOLHoloPart::Hover), 0);
        spec.HoverItemRect = HoloKitRect(HOLOKITTEST_NAN, 150.0f, 370.0f, 166.0f);
        buildCase(TEXT("NaN hover rect"), spec, HoloKitOpenLook(1.0f), 1.0f);
        TestEqual(TEXT("NaN hover rect: no Hover"), HoloKitCount(*out, ESOLHoloPart::Hover), 0);
    }

    // The sweep needs its own intensity, the backing its own alpha
    {
        FSOLHoloMenuLook look = HoloKitOpenLook(1.0f);
        look.SweepIntensity = 0.0f;
        buildCase(TEXT("sweep intensity 0"), overlay, look, 1.0f);
        TestEqual(TEXT("sweep intensity 0: no Sweep"), HoloKitCount(*out, ESOLHoloPart::Sweep), 0);
        look = HoloKitOpenLook(1.0f);
        look.BackdropAlpha = 0.0f;
        buildCase(TEXT("backdrop alpha 0"), overlay, look, 1.0f);
        TestFalse(TEXT("backdrop alpha 0: no backing"), out->bHasBacking);
        TestEqual(TEXT("backdrop alpha 0: the fill still drawn"), HoloKitCount(*out, ESOLHoloPart::Fill), 1);
    }

    // Options off: no dividers, no tabs
    {
        FSOLHoloFrameSpec spec = overlay;
        spec.bColumnDivider = false;
        spec.bBrokenDivider = false;
        spec.TabCount = 0;
        buildCase(TEXT("no dividers, no tabs"), spec, HoloKitOpenLook(1.0f), 1.0f);
        TestEqual(TEXT("no dividers: no ColumnDivider"), HoloKitCount(*out, ESOLHoloPart::ColumnDivider), 0);
        TestEqual(TEXT("no dividers: no BrokenDivider"), HoloKitCount(*out, ESOLHoloPart::BrokenDivider), 0);
        TestEqual(TEXT("no tabs: no Tab"), HoloKitCount(*out, ESOLHoloPart::Tab), 0);
        TestEqual(TEXT("no tabs: no TabDot"), HoloKitCount(*out, ESOLHoloPart::TabDot), 0);
    }

    // No tab selected: every tab unselected, no tab dot
    {
        FSOLHoloFrameSpec spec = overlay;
        spec.SelectedTab = INDEX_NONE;
        buildCase(TEXT("no selected tab"), spec, HoloKitOpenLook(1.0f), 1.0f);
        TestEqual(TEXT("no selected tab: 3 pieces per tab"), HoloKitCount(*out, ESOLHoloPart::Tab),
            3 * FRAME_MAX_TABS);
        TestEqual(TEXT("no selected tab: no TabDot"), HoloKitCount(*out, ESOLHoloPart::TabDot), 0);
    }

    // The speed panel's stepped divider
    {
        FSOLHoloFrameSpec spec = overlay;
        spec.BrokenStepAtPx = SPEED_DIVIDER_STEP_AT_PX;
        spec.BrokenStepPx = SPEED_DIVIDER_STEP_PX;
        buildCase(TEXT("stepped broken divider"), spec, HoloKitOpenLook(1.0f), 1.0f);
    }

    // Hover marks: brackets only and centre ticks
    {
        FSOLHoloFrameSpec spec = overlay;
        spec.HoverMark = ESOLHoloHoverMark::BracketsOnly;
        buildCase(TEXT("brackets only"), spec, HoloKitOpenLook(1.0f), 1.0f);
        TestEqual(TEXT("brackets only: 6 Hover"), HoloKitCount(*out, ESOLHoloPart::Hover), HOLOKITTEST_BRACKET_PIECES);
        TestEqual(TEXT("brackets only: no HoverDot"), HoloKitCount(*out, ESOLHoloPart::HoverDot), 0);
        spec.HoverMark = ESOLHoloHoverMark::CentreTicks;
        buildCase(TEXT("centre ticks"), spec, HoloKitOpenLook(1.0f), 1.0f);
        TestEqual(TEXT("centre ticks: 8 Hover"), HoloKitCount(*out, ESOLHoloPart::Hover),
            HOLOKITTEST_ROW_POINTER_PIECES);
        TestEqual(TEXT("centre ticks: no HoverDot"), HoloKitCount(*out, ESOLHoloPart::HoverDot), 0);
    }

    // No title: only the two corner accents, no title accent
    {
        FSOLHoloFrameSpec spec = overlay;
        spec.TitleStepWidthPx = 0.0f;
        buildCase(TEXT("no title"), spec, HoloKitOpenLook(1.0f), 1.0f);
        TestEqual(TEXT("no title: 2 Accent"), HoloKitCount(*out, ESOLHoloPart::Accent), 2);
        TestEqual(TEXT("no title: no TitleAccent"), HoloKitCount(*out, ESOLHoloPart::TitleAccent), 0);
    }

    // An open anim followed over its whole open / close, compared every step
    {
        FSOLHoloMenuAnim anim;
        const double startS = 50.0;
        SOLHoloMenu::BeginOpen(anim, startS, 1.0f);
        SOLHoloMenu::SetHovered(anim, 4, startS);
        for (int32 step = 0; step <= 60; ++step)
        {
            const double nowS = startS + 0.01 * step;
            if (step == 30)
            {
                SOLHoloMenu::BeginClose(anim, nowS, 1.0f);
            }
            SOLHoloMenu::Advance(anim, nowS, 1.0f);
            const FSOLHoloMenuLook look = SOLHoloMenu::Evaluate(anim, nowS, 1.0f);
            buildCase(*FString::Printf(TEXT("anim step %d"), step), overlay, look, 1.0f);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitFrameColoursTest, "SOLTest.HoloMenuKit.FrameColoursAndIntensity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every shape is exactly Primary except the hover mark (exactly Secondary); each part has its contract intensity
bool FSOLHoloMenuKitFrameColoursTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const float fill = 0.8f;
    const float hoverGlow = 0.7f;
    const FSOLHoloMenuLook look = HoloKitLook(1.0f, fill, 1.0f, hoverGlow, 0.4f);
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    SOLHoloFrameBuild::BuildFrame(HoloKitOverlaySpec(1.0f), look, 1.0f, *out);

    const float bright = FRAME_INTENSITY * fill;
    const float dim = FRAME_INTENSITY * FRAME_DIM_ALPHA * fill;
    int32 colourFailures = 0;
    int32 intensityFailures = 0;
    int32 brightTabs = 0;
    int32 dimTabs = 0;
    const int32 count = FMath::Clamp(out->ShapeCount, 0, static_cast<int32>(FRAME_MAX_SHAPES));
    for (int32 i = 0; i < count; ++i)
    {
        const FSOLHoloShape& shape = out->Shapes[i];

        // Colour
        const bool bHover = shape.Part == ESOLHoloPart::Hover || shape.Part == ESOLHoloPart::HoverDot;
        const FLinearColor& expectedColor = bHover ? SOL::HudPalette::SECONDARY
            : shape.Part == ESOLHoloPart::Fill ? SOL::HoloMenu::BACKDROP_TINT : SOL::HudPalette::PRIMARY;
        if (shape.Color != expectedColor && colourFailures++ == 0)
        {
            AddError(FString::Printf(TEXT("shape %d (part %d): colour %s"), i, static_cast<int32>(shape.Part),
                *shape.Color.ToString()));
        }

        // Intensity by part
        float expectedIntensity = -1.0f;
        switch (shape.Part)
        {
            case ESOLHoloPart::Fill:
                expectedIntensity = SOL::Hologram::HUD_PANEL_INTENSITY;
                break;
            case ESOLHoloPart::Outline:
            case ESOLHoloPart::GapDot:
                expectedIntensity = FRAME_INTENSITY;
                break;
            case ESOLHoloPart::Ruler:
            case ESOLHoloPart::Accent:
            case ESOLHoloPart::TabDot:
                expectedIntensity = bright;
                break;
            case ESOLHoloPart::TitleAccent:
            case ESOLHoloPart::ColumnDivider:
            case ESOLHoloPart::BrokenDivider:
                expectedIntensity = dim;
                break;
            case ESOLHoloPart::Tab:
                brightTabs += FMath::Abs(shape.Intensity - bright) <= HOLOKITTEST_VALUE_TOLERANCE ? 1 : 0;
                dimTabs += FMath::Abs(shape.Intensity - dim) <= HOLOKITTEST_VALUE_TOLERANCE ? 1 : 0;
                expectedIntensity = shape.Intensity;
                break;
            case ESOLHoloPart::Sweep:
                expectedIntensity = look.SweepIntensity;
                break;
            case ESOLHoloPart::Hover:
            case ESOLHoloPart::HoverDot:
                expectedIntensity = hoverGlow * fill;
                break;
            default:
                break;
        }
        if (FMath::Abs(shape.Intensity - expectedIntensity) > HOLOKITTEST_VALUE_TOLERANCE && intensityFailures++ == 0)
        {
            AddError(FString::Printf(TEXT("shape %d (part %d): intensity %.6f, expected %.6f"), i,
                static_cast<int32>(shape.Part), shape.Intensity, expectedIntensity));
        }
    }
    TestEqual(TEXT("every colour exact"), colourFailures, 0);
    TestEqual(TEXT("every intensity per part"), intensityFailures, 0);
    TestEqual(TEXT("selected tab: 4 pieces at FRAME_INTENSITY x fill"), brightTabs, 4);
    TestEqual(TEXT("unselected tabs: 3 pieces each, dim"), dimTabs, 3 * (FRAME_MAX_TABS - 1));
    TestTrue(TEXT("the hover mark is drawn"), HoloKitCount(*out, ESOLHoloPart::Hover) > 0);
    TestTrue(TEXT("the sweep is drawn"), HoloKitCount(*out, ESOLHoloPart::Sweep) == 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitFramePadTest, "SOLTest.HoloMenuKit.FramePadByStrength",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Every line and dot pads by lerp(FRAME_FLAT_PAD_PX, HUD_GLOW_PAD_PX, GlowScale(s)); the fill keeps its own pad; the
// hover mark is flat at FRAME_INTENSITY x fill at s = 0 (NaN and negative strengths included)
bool FSOLHoloMenuKitFramePadTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const float flatPad = static_cast<float>(FRAME_FLAT_PAD_PX);
    const float glowPad = SOL::Hologram::HUD_GLOW_PAD_PX;
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloKitTestExpected> expected = MakeUnique<FSOLHoloKitTestExpected>();
    const FSOLHoloFrameSpec spec = HoloKitOverlaySpec(1.0f);

    const float strengths[] = { 0.0f, 0.25f, 0.5f, 1.0f, 1.5f, 2.0f, HOLOKITTEST_NAN, -1.0f, 5.0f, HOLOKITTEST_INF };
    for (const float s : strengths)
    {
        const FString label = FString::Printf(TEXT("s = %f"), s);
        const FSOLHoloMenuLook look = HoloKitOpenLook(s);
        SOLHoloFrameBuild::BuildFrame(spec, look, s, *out);
        HoloKitReferenceFrame(spec, look, s, *expected);
        HoloKitTestOutput(*this, label, *out, *expected);

        // The pad of every line and dot, and of the fill
        const float sClamped = HoloKitS(s);
        const float expectedPad = sClamped <= 0.0f ? flatPad : (sClamped >= 1.0f ? glowPad : HoloKitPad(s));
        int32 padFailures = 0;
        const int32 count = FMath::Clamp(out->ShapeCount, 0, static_cast<int32>(FRAME_MAX_SHAPES));
        for (int32 i = 0; i < count; ++i)
        {
            const FSOLHoloShape& shape = out->Shapes[i];
            const float want = shape.Part == ESOLHoloPart::Fill ? SOL::Hologram::HUD_PANEL_QUAD_PAD_PX : expectedPad;
            padFailures += FMath::Abs(shape.PadPx - want) > HOLOKITTEST_PAD_TOLERANCE ? 1 : 0;
        }
        TestEqual(label + TEXT(": every pad"), padFailures, 0);

        // The hover mark: glow x fill for s > 0, FRAME_INTENSITY x fill at s = 0
        const int32 firstHover = HoloKitFirst(*out, ESOLHoloPart::Hover);
        if (TestTrue(label + TEXT(": hover mark drawn"), firstHover != INDEX_NONE))
        {
            const float wantHover = sClamped > 0.0f ? HOVER_GLOW_INTENSITY : FRAME_INTENSITY;
            TestEqual(label + TEXT(": hover intensity"), out->Shapes[firstHover].Intensity, wantHover,
                HOLOKITTEST_VALUE_TOLERANCE);
            TestTrue(label + TEXT(": hover in Secondary"),
                out->Shapes[firstHover].Color == SOL::HudPalette::SECONDARY);
        }

        // No sweep at s = 0
        if (sClamped <= 0.0f)
        {
            TestEqual(label + TEXT(": no sweep at s = 0"), HoloKitCount(*out, ESOLHoloPart::Sweep), 0);
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitFrameSizesTest, "SOLTest.HoloMenuKit.FrameSizes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Line half widths, dot radii, the fill panel, the sweep line and the backing follow u and k
bool FSOLHoloMenuKitFrameSizesTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloFrameOutput> other = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloKitTestExpected> expected = MakeUnique<FSOLHoloKitTestExpected>();

    // One rect / scale case: rect, title, uiScale, the expected u, k and backing rect
    struct FSizeCase
    {
        FBox2f Rect;
        float Title;
        float UiScale;
        float U;
        float K;
        FBox2f Backing;
    };
    const FSizeCase cases[] = {
        { HoloKitRectR(), HOLOKITTEST_R_TITLE, 1.0f, 1.0f, 1.0f, HoloKitRect(2.0f, 2.0f, 398.0f, 298.0f) },
        { HoloKitRectR(), HOLOKITTEST_R_TITLE, 2.0f, 2.0f, 2.0f, HoloKitRect(4.0f, 4.0f, 396.0f, 296.0f) },
        { HoloKitRect(0.0f, 0.0f, 17.0f, 32.0f), 0.0f, 1.0f, 1.0f, 0.5f, HoloKitRect(1.0f, 1.0f, 16.0f, 31.0f) } };

    for (const FSizeCase& sizeCase : cases)
    {
        const FString label = FString::Printf(TEXT("%.0fx%.0f at uiScale %.0f"),
            sizeCase.Rect.Max.X - sizeCase.Rect.Min.X, sizeCase.Rect.Max.Y - sizeCase.Rect.Min.Y, sizeCase.UiScale);
        FSOLHoloFrameSpec spec = HoloKitOverlaySpec(sizeCase.UiScale);
        spec.Rect = sizeCase.Rect;
        spec.TitleStepWidthPx = sizeCase.Title;
        const FSOLHoloMenuLook look = HoloKitOpenLook(1.0f);
        SOLHoloFrameBuild::BuildFrame(spec, look, 1.0f, *out);
        HoloKitReferenceFrame(spec, look, 1.0f, *expected);
        HoloKitTestOutput(*this, label, *out, *expected);

        // Lines and dots
        int32 sizeFailures = 0;
        int32 gapDots = 0;
        const int32 count = FMath::Clamp(out->ShapeCount, 0, static_cast<int32>(FRAME_MAX_SHAPES));
        for (int32 i = 0; i < count; ++i)
        {
            const FSOLHoloShape& shape = out->Shapes[i];
            if (shape.Part == ESOLHoloPart::Fill)
            {
                continue;
            }
            float wantHalfWidth = FRAME_HALF_WIDTH_PX * sizeCase.U;
            bool bDot = false;
            if (shape.Part == ESOLHoloPart::GapDot)
            {
                wantHalfWidth = FRAME_DOT_RADIUS_PX * sizeCase.K;
                bDot = true;
                ++gapDots;
            }
            else if (shape.Part == ESOLHoloPart::TabDot || shape.Part == ESOLHoloPart::HoverDot)
            {
                wantHalfWidth = FRAME_DOT_RADIUS_PX * sizeCase.U;
                bDot = true;
            }
            const bool bOk = shape.Kind == ESOLHudShapeKind::Line && shape.Fill == 0.0f
                && FMath::Abs(shape.HalfWidthPx - wantHalfWidth) <= HOLOKITTEST_VALUE_TOLERANCE
                && (!bDot || shape.A == shape.B);
            if (!bOk && sizeFailures++ == 0)
            {
                AddError(FString::Printf(TEXT("%s: shape %d (part %d) half width %.4f, expected %.4f"), *label, i,
                    static_cast<int32>(shape.Part), shape.HalfWidthPx, wantHalfWidth));
            }
        }
        TestEqual(label + TEXT(": every line and dot size"), sizeFailures, 0);
        TestTrue(label + TEXT(": some gap dots"), gapDots > 0);

        // The fill: a panel over the backing rect
        const int32 fillIndex = HoloKitFirst(*out, ESOLHoloPart::Fill);
        if (TestEqual(label + TEXT(": fill is shape 0"), fillIndex, 0))
        {
            const FSOLHoloShape& fillShape = out->Shapes[0];
            TestTrue(label + TEXT(": fill kind Panel"), fillShape.Kind == ESOLHudShapeKind::Panel);
            TestEqual(label + TEXT(": fill centre x"), fillShape.A.X,
                0.5f * (sizeCase.Backing.Min.X + sizeCase.Backing.Max.X), HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": fill centre y"), fillShape.A.Y,
                0.5f * (sizeCase.Backing.Min.Y + sizeCase.Backing.Max.Y), HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": fill half x"), fillShape.B.X,
                0.5f * (sizeCase.Backing.Max.X - sizeCase.Backing.Min.X), HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": fill half y"), fillShape.B.Y,
                0.5f * (sizeCase.Backing.Max.Y - sizeCase.Backing.Min.Y), HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": fill intensity"), fillShape.Intensity, SOL::Hologram::HUD_PANEL_INTENSITY,
                HOLOKITTEST_VALUE_TOLERANCE);
            TestEqual(label + TEXT(": fill amount"), fillShape.Fill, SOL::Hologram::HUD_PANEL_FILL,
                HOLOKITTEST_VALUE_TOLERANCE);
            TestEqual(label + TEXT(": fill pad"), fillShape.PadPx, SOL::Hologram::HUD_PANEL_QUAD_PAD_PX,
                HOLOKITTEST_VALUE_TOLERANCE);
        }

        // The sweep: across the backing rect at Min.Y + SweepFraction x height
        const int32 sweepIndex = HoloKitFirst(*out, ESOLHoloPart::Sweep);
        if (TestTrue(label + TEXT(": sweep drawn"), sweepIndex != INDEX_NONE))
        {
            const FSOLHoloShape& sweep = out->Shapes[sweepIndex];
            const float y = sizeCase.Backing.Min.Y
                + HOLOKITTEST_SWEEP_FRACTION * (sizeCase.Backing.Max.Y - sizeCase.Backing.Min.Y);
            TestEqual(label + TEXT(": sweep A.y"), sweep.A.Y, y, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": sweep B.y"), sweep.B.Y, y, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": sweep left end"), FMath::Min(sweep.A.X, sweep.B.X), sizeCase.Backing.Min.X,
                HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": sweep right end"), FMath::Max(sweep.A.X, sweep.B.X), sizeCase.Backing.Max.X,
                HOLOKITTEST_PX_TOLERANCE);
        }

        // The backing
        if (TestTrue(label + TEXT(": backing"), out->bHasBacking))
        {
            TestEqual(label + TEXT(": backing min x"), out->Backing.Rect.Min.X, sizeCase.Backing.Min.X,
                HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": backing min y"), out->Backing.Rect.Min.Y, sizeCase.Backing.Min.Y,
                HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": backing max x"), out->Backing.Rect.Max.X, sizeCase.Backing.Max.X,
                HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": backing max y"), out->Backing.Rect.Max.Y, sizeCase.Backing.Max.Y,
                HOLOKITTEST_PX_TOLERANCE);
            TestEqual(label + TEXT(": backing corner"), out->Backing.CornerPx, 0.0f);
            TestEqual(label + TEXT(": backing feather"), out->Backing.FeatherPx,
                SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX * sizeCase.U, HOLOKITTEST_VALUE_TOLERANCE);
            TestEqual(label + TEXT(": backing alpha"), out->Backing.Alpha, look.BackdropAlpha,
                HOLOKITTEST_VALUE_TOLERANCE);
        }
    }

    // uiScale NaN and 0 read 1: the same output as uiScale 1
    SOLHoloFrameBuild::BuildFrame(HoloKitOverlaySpec(1.0f), HoloKitOpenLook(1.0f), 1.0f, *out);
    SOLHoloFrameBuild::BuildFrame(HoloKitOverlaySpec(HOLOKITTEST_NAN), HoloKitOpenLook(1.0f), 1.0f, *other);
    HoloKitTestSame(*this, TEXT("uiScale NaN equals uiScale 1"), *out, *other);
    SOLHoloFrameBuild::BuildFrame(HoloKitOverlaySpec(0.0f), HoloKitOpenLook(1.0f), 1.0f, *other);
    HoloKitTestSame(*this, TEXT("uiScale 0 equals uiScale 1"), *out, *other);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitStrengthZeroTest, "SOLTest.HoloMenuKit.FrameStrengthZero",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// D18 end to end: an anim opened at s = 0, advanced once and evaluated gives the whole flat frame at once on R
bool FSOLHoloMenuKitStrengthZeroTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const double startS = 200.0;
    FSOLHoloMenuAnim anim;
    SOLHoloMenu::BeginOpen(anim, startS, 0.0f);
    SOLHoloMenu::Advance(anim, startS, 0.0f);
    SOLHoloMenu::SetHovered(anim, 3, startS);
    const FSOLHoloMenuLook look = SOLHoloMenu::Evaluate(anim, startS, 0.0f);
    TestTrue(TEXT("visible"), look.bVisible);

    FSOLHoloFrameSpec spec = HoloKitPlainSpec(HoloKitRectR(), HOLOKITTEST_R_TITLE, 1.0f);
    spec.HoverItemRect = HoloKitRect(40.0f, 100.0f, 300.0f, 120.0f);
    spec.HoverMark = ESOLHoloHoverMark::RowPointer;
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloKitTestExpected> expected = MakeUnique<FSOLHoloKitTestExpected>();
    SOLHoloFrameBuild::BuildFrame(spec, look, 0.0f, *out);
    HoloKitReferenceFrame(spec, look, 0.0f, *expected);
    HoloKitTestOutput(*this, TEXT("s = 0"), *out, *expected);

    TestEqual(TEXT("19 Outline at once"), HoloKitCount(*out, ESOLHoloPart::Outline), HOLOKITTEST_R_OUTLINE_PIECES);
    TestEqual(TEXT("14 GapDot at once"), HoloKitCount(*out, ESOLHoloPart::GapDot), HOLOKITTEST_R_GAP_DOTS);
    TestEqual(TEXT("no Sweep"), HoloKitCount(*out, ESOLHoloPart::Sweep), 0);
    if (TestTrue(TEXT("backing"), out->bHasBacking))
    {
        TestEqual(TEXT("backing at FLAT_PANEL_ALPHA"), out->Backing.Alpha, FLAT_PANEL_ALPHA,
            HOLOKITTEST_VALUE_TOLERANCE);
        TestEqual(TEXT("its opacity is 1"), SOLHoloFrameBuild::BackingOpacity(out->Backing.Alpha), 1.0f,
            HOLOKITTEST_VALUE_TOLERANCE);
    }

    // Every line and dot is a thin flat line (the fill keeps its own pad, section 4.6 sizes row)
    int32 padFailures = 0;
    const int32 count = FMath::Clamp(out->ShapeCount, 0, static_cast<int32>(FRAME_MAX_SHAPES));
    for (int32 i = 0; i < count; ++i)
    {
        if (out->Shapes[i].Part != ESOLHoloPart::Fill)
        {
            padFailures += FMath::Abs(out->Shapes[i].PadPx - static_cast<float>(FRAME_FLAT_PAD_PX))
                > HOLOKITTEST_PAD_TOLERANCE ? 1 : 0;
        }
    }
    TestEqual(TEXT("every line and dot pad = FRAME_FLAT_PAD_PX"), padFailures, 0);

    // The hovered row: 8 pieces and 1 dot, Secondary, at FRAME_INTENSITY
    TestEqual(TEXT("8 Hover"), HoloKitCount(*out, ESOLHoloPart::Hover), HOLOKITTEST_ROW_POINTER_PIECES);
    TestEqual(TEXT("1 HoverDot"), HoloKitCount(*out, ESOLHoloPart::HoverDot), 1);
    int32 hoverFailures = 0;
    for (int32 i = 0; i < count; ++i)
    {
        const FSOLHoloShape& shape = out->Shapes[i];
        if (shape.Part == ESOLHoloPart::Hover || shape.Part == ESOLHoloPart::HoverDot)
        {
            hoverFailures += shape.Color != SOL::HudPalette::SECONDARY
                || FMath::Abs(shape.Intensity - FRAME_INTENSITY) > HOLOKITTEST_VALUE_TOLERANCE ? 1 : 0;
        }
    }
    TestEqual(TEXT("hover mark Secondary at FRAME_INTENSITY"), hoverFailures, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitCapacityTest, "SOLTest.HoloMenuKit.Capacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The worst case fits FRAME_MAX_SHAPES, TabCount above FRAME_MAX_TABS reads the maximum, and no input over a sweep of
// rects, fractions, scales and strengths writes past the output or above its capacity (frames and cards)
bool FSOLHoloMenuKitCapacityTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    TUniquePtr<FSOLHoloKitTestGuardedOutput> guarded = MakeUnique<FSOLHoloKitTestGuardedOutput>();
    TUniquePtr<FSOLHoloFrameOutput> other = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloKitTestExpected> expected = MakeUnique<FSOLHoloKitTestExpected>();

    // Constants: the section 4.6 worst case and the card's
    TestTrue(TEXT("FRAME_MAX_SHAPES holds the section 4.6 worst case"),
        static_cast<int32>(FRAME_MAX_SHAPES) >= HOLOKITTEST_WORST_CASE_FIXED + 4 * static_cast<int32>(FRAME_MAX_TABS));
    TestEqual(TEXT("CARD_MAX_SHAPES = 10 (fill + 8 ticks + sweep)"), static_cast<int32>(CARD_MAX_SHAPES),
        1 + HOLOKITTEST_CARD_TICKS + 1);
    TestEqual(TEXT("FRAME_MAX_TABS = MAX_PROFILES"), static_cast<int32>(FRAME_MAX_TABS),
        static_cast<int32>(SOL::Overlay::MAX_PROFILES));

    // The worst case: every group at its maximum
    {
        const FSOLHoloFrameSpec spec = HoloKitWorstCaseSpec();
        const FSOLHoloMenuLook look = HoloKitOpenLook(1.0f);
        HoloKitFillGuard(*guarded);
        const int32 count = SOLHoloFrameBuild::BuildFrame(spec, look, 1.0f, guarded->Out);
        HoloKitReferenceFrame(spec, look, 1.0f, *expected);
        HoloKitTestOutput(*this, TEXT("worst case"), guarded->Out, *expected);
        TestTrue(TEXT("worst case: within FRAME_MAX_SHAPES"), count >= 0 && count <= FRAME_MAX_SHAPES);
        TestTrue(TEXT("worst case: no write past the output"), HoloKitGuardIntact(*guarded));
        TestEqual(TEXT("worst case: 40 column divider dashes"), HoloKitCount(guarded->Out,
            ESOLHoloPart::ColumnDivider), static_cast<int32>(DIVIDER_MAX_DASHES));

        // TabCount above FRAME_MAX_TABS reads FRAME_MAX_TABS
        FSOLHoloFrameSpec overTabs = spec;
        overTabs.TabCount = FRAME_MAX_TABS + 4;
        SOLHoloFrameBuild::BuildFrame(overTabs, look, 1.0f, *other);
        HoloKitTestSame(*this, TEXT("TabCount above FRAME_MAX_TABS equals FRAME_MAX_TABS"), guarded->Out, *other);
    }

    // The sweep
    const FBox2f rects[] = {
        HoloKitRect(10.0f, 10.0f, 11.0f, 11.0f), HoloKitRect(0.0f, 0.0f, 17.0f, 32.0f),
        HoloKitRect(5.0f, 5.0f, 45.0f, 75.0f), HoloKitRectR(), HoloKitRect(0.0f, 0.0f, 1920.0f, 1080.0f),
        HoloKitRect(0.0f, 0.0f, 4000.0f, 2000.0f) };
    const float fractions[] = { -1.0f, 0.0f, HOLOKITTEST_NAN, 0.01f, 0.37f, 0.5f, 0.99f, 1.0f, 2.0f };
    const float uiScales[] = { 0.5f, 1.0f, 2.0f, HOLOKITTEST_NAN };
    const float strengths[] = { 0.0f, 1.0f, 2.0f };
    FSOLHoloKitTestSweepLog frameLog;
    FSOLHoloKitTestSweepLog cardLog;
    for (const FBox2f& rect : rects)
    {
        for (const float fraction : fractions)
        {
            for (const float uiScale : uiScales)
            {
                for (const float s : strengths)
                {
                    const FString label = FString::Printf(TEXT("%.0fx%.0f f %f u %f s %.0f"), rect.Max.X - rect.Min.X,
                        rect.Max.Y - rect.Min.Y, fraction, uiScale, s);
                    const float fill = FMath::IsNaN(fraction) ? 0.0f : FMath::Clamp(fraction, 0.0f, 1.0f);
                    const float hover = s > 0.0f ? HOVER_GLOW_INTENSITY : 0.0f;
                    const FSOLHoloMenuLook look = HoloKitLook(fraction, fill, s, hover, 0.5f);

                    // Frame
                    const FSOLHoloFrameSpec spec = HoloKitFullSpecFor(rect, uiScale);
                    HoloKitFillGuard(*guarded);
                    const int32 frameCount = SOLHoloFrameBuild::BuildFrame(spec, look, s, guarded->Out);
                    HoloKitReferenceFrame(spec, look, s, *expected);
                    FString why;
                    if (frameCount < 0 || frameCount > FRAME_MAX_SHAPES || frameCount != guarded->Out.ShapeCount)
                    {
                        frameLog.Fail(label + FString::Printf(TEXT(": count %d"), frameCount));
                    }
                    else if (!HoloKitGuardIntact(*guarded))
                    {
                        frameLog.Fail(label + TEXT(": wrote past the output"));
                    }
                    else if (!HoloKitOutputMatches(guarded->Out, *expected, why))
                    {
                        frameLog.Fail(label + TEXT(": ") + why);
                    }

                    // Card around the same rect
                    HoloKitFillGuard(*guarded);
                    const int32 cardCount = SOLHoloFrameBuild::BuildCard(rect, uiScale, look, s, guarded->Out);
                    HoloKitReferenceCard(rect, uiScale, look, s, *expected);
                    if (cardCount < 0 || cardCount > CARD_MAX_SHAPES || cardCount != guarded->Out.ShapeCount)
                    {
                        cardLog.Fail(label + FString::Printf(TEXT(": card count %d"), cardCount));
                    }
                    else if (!HoloKitGuardIntact(*guarded))
                    {
                        cardLog.Fail(label + TEXT(": card wrote past the output"));
                    }
                    else if (!HoloKitOutputMatches(guarded->Out, *expected, why))
                    {
                        cardLog.Fail(label + TEXT(": card: ") + why);
                    }
                }
            }
        }
    }
    if (!TestEqual(TEXT("frame sweep failures"), frameLog.Failures, 0))
    {
        AddError(TEXT("first frame failure: ") + frameLog.First);
    }
    if (!TestEqual(TEXT("card sweep failures"), cardLog.Failures, 0))
    {
        AddError(TEXT("first card failure: ") + cardLog.First);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitDeterminismTest, "SOLTest.HoloMenuKit.Determinism",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The same inputs give identical outputs on every call, whatever was built before and however dirty the output is;
// an Open menu that moves or retitles gets its new outline at once (an outline cache must not change results)
bool FSOLHoloMenuKitDeterminismTest::RunTest(const FString& /*parameters*/)
{
    TUniquePtr<FSOLHoloFrameOutput> first = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloFrameOutput> again = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloKitTestExpected> expected = MakeUnique<FSOLHoloKitTestExpected>();
    const FSOLHoloFrameSpec specA = HoloKitOverlaySpec(1.0f);
    const FSOLHoloMenuLook lookA = HoloKitOpenLook(1.0f);

    // Repeated calls
    SOLHoloFrameBuild::BuildFrame(specA, lookA, 1.0f, *first);
    for (int32 i = 0; i < 10; ++i)
    {
        SOLHoloFrameBuild::BuildFrame(specA, lookA, 1.0f, *again);
        HoloKitTestSame(*this, FString::Printf(TEXT("repeat %d"), i), *first, *again);
    }

    // Interleaved with a different spec and look into the same output
    {
        FSOLHoloFrameSpec specB = HoloKitFullSpecFor(HoloKitRect(30.0f, 40.0f, 900.0f, 700.0f), 2.0f);
        SOLHoloFrameBuild::BuildFrame(specB, HoloKitLook(0.3f, 0.2f, 2.0f, 0.4f, 0.8f), 2.0f, *again);
        SOLHoloFrameBuild::BuildFrame(specA, lookA, 1.0f, *again);
        HoloKitTestSame(*this, TEXT("after another frame"), *first, *again);
    }

    // A dirty output
    HoloKitDirty(*again);
    SOLHoloFrameBuild::BuildFrame(specA, lookA, 1.0f, *again);
    HoloKitTestSame(*this, TEXT("dirty output"), *first, *again);

    // An Open menu moved and retitled between calls: each result is that input's own frame
    {
        FSOLHoloFrameSpec moved = specA;
        moved.Rect = HoloKitRect(60.0f, 30.0f, 460.0f, 330.0f);
        SOLHoloFrameBuild::BuildFrame(moved, lookA, 1.0f, *again);
        HoloKitReferenceFrame(moved, lookA, 1.0f, *expected);
        HoloKitTestOutput(*this, TEXT("Open, moved"), *again, *expected);

        FSOLHoloFrameSpec retitled = moved;
        retitled.TitleStepWidthPx = 120.0f;
        SOLHoloFrameBuild::BuildFrame(retitled, lookA, 1.0f, *again);
        HoloKitReferenceFrame(retitled, lookA, 1.0f, *expected);
        HoloKitTestOutput(*this, TEXT("Open, retitled"), *again, *expected);

        FSOLHoloFrameSpec rescaled = retitled;
        rescaled.UiScale = 2.0f;
        SOLHoloFrameBuild::BuildFrame(rescaled, lookA, 1.0f, *again);
        HoloKitReferenceFrame(rescaled, lookA, 1.0f, *expected);
        HoloKitTestOutput(*this, TEXT("Open, rescaled"), *again, *expected);

        SOLHoloFrameBuild::BuildFrame(specA, lookA, 1.0f, *again);
        HoloKitTestSame(*this, TEXT("back to the first input"), *first, *again);
    }

    // Through an anim's open: the Open frames repeat exactly
    {
        FSOLHoloMenuAnim anim;
        SOLHoloMenu::BeginOpen(anim, 10.0, 1.0f);
        for (int32 step = 0; step <= 40; ++step)
        {
            SOLHoloMenu::Advance(anim, 10.0 + 0.01 * step, 1.0f);
            SOLHoloFrameBuild::BuildFrame(specA, SOLHoloMenu::Evaluate(anim, 10.0 + 0.01 * step, 1.0f), 1.0f,
                *again);
        }
        TestTrue(TEXT("anim reached Open"), anim.Phase == ESOLHoloMenuPhase::Open);
        const FSOLHoloMenuLook openLook = SOLHoloMenu::Evaluate(anim, 12.0, 1.0f);
        SOLHoloFrameBuild::BuildFrame(specA, openLook, 1.0f, *first);
        SOLHoloFrameBuild::BuildFrame(specA, openLook, 1.0f, *again);
        HoloKitTestSame(*this, TEXT("Open look built twice"), *first, *again);
        HoloKitReferenceFrame(specA, openLook, 1.0f, *expected);
        HoloKitTestOutput(*this, TEXT("Open look"), *first, *expected);
    }
    return true;
}

// --- Card builder (section 4.6, D15) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitCardTest, "SOLTest.HoloMenuKit.CardBuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The frameless card: fill, 8 corner ticks and the sweep around the padded content rect, its backing as AddHoloPanel's,
// never a frame part, at most CARD_MAX_SHAPES
bool FSOLHoloMenuKitCardTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    using SOLHoloFrameBuild::BuildCard;
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloFrameOutput> other = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloKitTestExpected> expected = MakeUnique<FSOLHoloKitTestExpected>();
    const FBox2f content = HoloKitRect(100.0f, 40.0f, 180.0f, 70.0f);

    // Open at s = 1, uiScale 1: card rect (92,32)-(188,78)
    {
        const FSOLHoloMenuLook look = HoloKitOpenLook(1.0f);
        HoloKitDirty(*out);
        const int32 count = BuildCard(content, 1.0f, look, 1.0f, *out);
        HoloKitReferenceCard(content, 1.0f, look, 1.0f, *expected);
        HoloKitTestOutput(*this, TEXT("card open, uiScale 1"), *out, *expected);
        TestEqual(TEXT("returns ShapeCount"), count, out->ShapeCount);
        TestEqual(TEXT("10 shapes"), out->ShapeCount, 1 + HOLOKITTEST_CARD_TICKS + 1);
        TestEqual(TEXT("Fill first"), HoloKitFirst(*out, ESOLHoloPart::Fill), 0);
        TestEqual(TEXT("ticks from 1"), HoloKitFirst(*out, ESOLHoloPart::CardTick), 1);
        TestEqual(TEXT("8 CardTick"), HoloKitCount(*out, ESOLHoloPart::CardTick), HOLOKITTEST_CARD_TICKS);
        TestEqual(TEXT("Sweep last"), HoloKitFirst(*out, ESOLHoloPart::Sweep), out->ShapeCount - 1);
        if (out->ShapeCount > 0)
        {
            const FSOLHoloShape& fillShape = out->Shapes[0];
            TestEqual(TEXT("fill centre x"), fillShape.A.X, 140.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("fill centre y"), fillShape.A.Y, 55.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("fill half x"), fillShape.B.X, 48.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("fill half y"), fillShape.B.Y, 23.0f, HOLOKITTEST_PX_TOLERANCE);
        }

        // Every tick: CARD_TICK_PX long, Primary at FRAME_INTENSITY, full glow pad
        int32 tickFailures = 0;
        for (int32 i = 0; i < FMath::Clamp(out->ShapeCount, 0, static_cast<int32>(FRAME_MAX_SHAPES)); ++i)
        {
            const FSOLHoloShape& shape = out->Shapes[i];
            if (shape.Part != ESOLHoloPart::CardTick)
            {
                continue;
            }
            const float length = FVector2f::Distance(shape.A, shape.B);
            const bool bOk = FMath::Abs(length - CARD_TICK_PX) <= HOLOKITTEST_PX_TOLERANCE
                && shape.Color == SOL::HudPalette::PRIMARY
                && FMath::Abs(shape.Intensity - FRAME_INTENSITY) <= HOLOKITTEST_VALUE_TOLERANCE
                && FMath::Abs(shape.PadPx - SOL::Hologram::HUD_GLOW_PAD_PX) <= HOLOKITTEST_PAD_TOLERANCE;
            tickFailures += bOk ? 0 : 1;
        }
        TestEqual(TEXT("every tick length, colour, intensity and pad"), tickFailures, 0);
        if (TestTrue(TEXT("backing"), out->bHasBacking))
        {
            TestEqual(TEXT("backing min x"), out->Backing.Rect.Min.X, 92.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("backing min y"), out->Backing.Rect.Min.Y, 32.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("backing max x"), out->Backing.Rect.Max.X, 188.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("backing max y"), out->Backing.Rect.Max.Y, 78.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("backing corner HUD_PANEL_CORNER_PX"), out->Backing.CornerPx,
                static_cast<float>(SOL::Hologram::HUD_PANEL_CORNER_PX), HOLOKITTEST_VALUE_TOLERANCE);
            TestEqual(TEXT("backing feather"), out->Backing.FeatherPx, SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX,
                HOLOKITTEST_VALUE_TOLERANCE);
            TestEqual(TEXT("backing alpha"), out->Backing.Alpha, look.BackdropAlpha, HOLOKITTEST_VALUE_TOLERANCE);
        }
    }

    // uiScale 2: card rect grown by 16, corner and feather doubled
    {
        const FSOLHoloMenuLook look = HoloKitOpenLook(1.0f);
        BuildCard(content, 2.0f, look, 1.0f, *out);
        HoloKitReferenceCard(content, 2.0f, look, 1.0f, *expected);
        HoloKitTestOutput(*this, TEXT("card open, uiScale 2"), *out, *expected);
        if (TestTrue(TEXT("uiScale 2: backing"), out->bHasBacking))
        {
            TestEqual(TEXT("uiScale 2: backing min x"), out->Backing.Rect.Min.X, 84.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("uiScale 2: backing max y"), out->Backing.Rect.Max.Y, 86.0f, HOLOKITTEST_PX_TOLERANCE);
            TestEqual(TEXT("uiScale 2: corner"), out->Backing.CornerPx,
                2.0f * static_cast<float>(SOL::Hologram::HUD_PANEL_CORNER_PX), HOLOKITTEST_VALUE_TOLERANCE);
            TestEqual(TEXT("uiScale 2: feather"), out->Backing.FeatherPx,
                2.0f * SOL::Hologram::HUD_PANEL_BACKING_FEATHER_PX, HOLOKITTEST_VALUE_TOLERANCE);
        }
    }

    // Mid draw-in, no fill yet: half-length ticks only, no backing
    {
        const FSOLHoloMenuLook look = HoloKitLook(0.5f, 0.0f, 1.0f, 0.0f, 0.25f);
        BuildCard(content, 1.0f, look, 1.0f, *out);
        HoloKitReferenceCard(content, 1.0f, look, 1.0f, *expected);
        HoloKitTestOutput(*this, TEXT("card draw-in 0.5"), *out, *expected);
        TestEqual(TEXT("draw-in 0.5: only the 8 ticks"), out->ShapeCount, HOLOKITTEST_CARD_TICKS);
        TestFalse(TEXT("draw-in 0.5: no backing"), out->bHasBacking);
        if (out->ShapeCount > 0)
        {
            TestEqual(TEXT("draw-in 0.5: half-length tick"), FVector2f::Distance(out->Shapes[0].A, out->Shapes[0].B),
                0.5f * CARD_TICK_PX, HOLOKITTEST_PX_TOLERANCE);
        }
    }

    // Strength 0: flat ticks, no sweep, the flat backdrop
    {
        const FSOLHoloMenuLook look = HoloKitOpenLook(0.0f);
        BuildCard(content, 1.0f, look, 0.0f, *out);
        HoloKitReferenceCard(content, 1.0f, look, 0.0f, *expected);
        HoloKitTestOutput(*this, TEXT("card s = 0"), *out, *expected);
        TestEqual(TEXT("s = 0: no sweep"), HoloKitCount(*out, ESOLHoloPart::Sweep), 0);
        TestEqual(TEXT("s = 0: 8 ticks"), HoloKitCount(*out, ESOLHoloPart::CardTick), HOLOKITTEST_CARD_TICKS);
        if (TestTrue(TEXT("s = 0: backing"), out->bHasBacking))
        {
            TestEqual(TEXT("s = 0: backing at FLAT_PANEL_ALPHA"), out->Backing.Alpha, FLAT_PANEL_ALPHA,
                HOLOKITTEST_VALUE_TOLERANCE);
        }
    }

    // Never a frame part
    {
        BuildCard(content, 1.0f, HoloKitOpenLook(2.0f), 2.0f, *out);
        int32 framePart = 0;
        for (int32 i = 0; i < FMath::Clamp(out->ShapeCount, 0, static_cast<int32>(FRAME_MAX_SHAPES)); ++i)
        {
            const ESOLHoloPart part = out->Shapes[i].Part;
            framePart += part == ESOLHoloPart::Fill || part == ESOLHoloPart::CardTick || part == ESOLHoloPart::Sweep
                ? 0 : 1;
        }
        TestEqual(TEXT("only Fill, CardTick and Sweep"), framePart, 0);
        TestTrue(TEXT("within CARD_MAX_SHAPES"), out->ShapeCount <= CARD_MAX_SHAPES);
    }

    // Hidden: invisible look or invalid content, into a dirty output
    {
        FSOLHoloMenuLook look = HoloKitOpenLook(1.0f);
        look.bVisible = false;
        HoloKitDirty(*out);
        TestEqual(TEXT("invisible card: returns 0"), BuildCard(content, 1.0f, look, 1.0f, *out), 0);
        TestEqual(TEXT("invisible card: ShapeCount 0"), out->ShapeCount, 0);
        TestFalse(TEXT("invisible card: no backing"), out->bHasBacking);
        const FBox2f invalidContent[] = { FBox2f(ForceInit), HoloKitRect(HOLOKITTEST_NAN, 40.0f, 180.0f, 70.0f),
            HoloKitRect(100.0f, 40.0f, 180.0f, HOLOKITTEST_INF) };
        for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(invalidContent)); ++i)
        {
            HoloKitDirty(*out);
            TestEqual(FString::Printf(TEXT("invalid content %d: returns 0"), i),
                BuildCard(invalidContent[i], 1.0f, HoloKitOpenLook(1.0f), 1.0f, *out), 0);
            TestEqual(FString::Printf(TEXT("invalid content %d: ShapeCount 0"), i), out->ShapeCount, 0);
            TestFalse(FString::Printf(TEXT("invalid content %d: no backing"), i), out->bHasBacking);
        }
    }

    // Deterministic, dirty output included
    {
        const FSOLHoloMenuLook look = HoloKitLook(0.8f, 0.6f, 1.5f, 0.0f, 0.7f);
        BuildCard(content, 1.5f, look, 1.5f, *out);
        HoloKitDirty(*other);
        BuildCard(content, 1.5f, look, 1.5f, *other);
        HoloKitTestSame(*this, TEXT("card twice, second into a dirty output"), *out, *other);
    }
    return true;
}

// --- AppendToBatch and BackingOpacity (section 4.6) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitAppendTest, "SOLTest.HoloMenuKit.AppendToBatch",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// AppendToBatch adds the shapes in order by AddLine / AddPanel, keeps what the batch held, returns how many it took
// and leaves the rest as the batch's dropped count; each quad decodes to its shape's kind
bool FSOLHoloMenuKitAppendTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    using SOLHoloFrameBuild::AppendToBatch;
    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    TUniquePtr<FSOLHoloFrameOutput> card = MakeUnique<FSOLHoloFrameOutput>();
    SOLHoloFrameBuild::BuildFrame(HoloKitOverlaySpec(1.0f), HoloKitOpenLook(1.0f), 1.0f, *out);
    SOLHoloFrameBuild::BuildCard(HoloKitRect(100.0f, 40.0f, 180.0f, 70.0f), 1.0f, HoloKitOpenLook(1.0f), 1.0f, *card);
    const int32 n = out->ShapeCount;
    if (!TestTrue(TEXT("the frame has shapes"), n > 0 && n <= FRAME_MAX_SHAPES))
    {
        return false;
    }

    // Two shapes already in the batch are kept
    const int32 capacity = FRAME_MAX_SHAPES + 2 * CARD_MAX_SHAPES + 8;
    FSOLHudShapeBatch batch;
    FSOLHudShapeBatch reference;
    batch.Initialize(capacity);
    reference.Initialize(capacity);
    const FLinearColor lineColor = SOL::HudPalette::GOOD;
    for (FSOLHudShapeBatch* target : { &batch, &reference })
    {
        target->AddLine(FVector2f(1.0f, 1.0f), FVector2f(50.0f, 1.0f), 0.5f, lineColor, 0.7f, 4.0f);
        target->AddLine(FVector2f(1.0f, 9.0f), FVector2f(50.0f, 9.0f), 0.5f, lineColor, 0.7f, 4.0f);
    }
    const int32 taken = AppendToBatch(*out, batch);
    HoloKitAddDirect(*out, reference);
    TestEqual(TEXT("returns every shape"), taken, n);
    TestEqual(TEXT("batch keeps its 2 shapes"), batch.GetShapeCount(), 2 + n);
    TestEqual(TEXT("nothing dropped"), batch.GetDroppedCount(), 0);
    HoloKitTestSameBatch(*this, TEXT("frame"), batch, reference);

    // Each new quad decodes to its shape's kind
    int32 kindFailures = 0;
    const TArray<FSlateVertex>& vertices = batch.GetVertices();
    for (int32 i = 0; i < n; ++i)
    {
        const int32 vertex = (2 + i) * SOL::Hologram::HUD_VERTICES_PER_SHAPE;
        if (vertex >= vertices.Num())
        {
            ++kindFailures;
            continue;
        }
        ESOLHudShapeKind kind = ESOLHudShapeKind::Arc;
        float param = 0.0f;
        SOLHudShapeMath::DecodeKind(vertices[vertex].TexCoords[3], kind, param);
        kindFailures += kind == out->Shapes[i].Kind ? 0 : 1;
    }
    TestEqual(TEXT("every quad's decoded kind"), kindFailures, 0);
    TestTrue(TEXT("the fill is a Panel quad"), out->Shapes[0].Kind == ESOLHudShapeKind::Panel);

    // A second append (a card) adds after the first, still no reset
    const int32 cardTaken = AppendToBatch(*card, batch);
    HoloKitAddDirect(*card, reference);
    TestEqual(TEXT("card: returns every shape"), cardTaken, card->ShapeCount);
    TestEqual(TEXT("card: appended after the frame"), batch.GetShapeCount(), 2 + n + card->ShapeCount);
    HoloKitTestSameBatch(*this, TEXT("frame then card"), batch, reference);

    // An empty output adds nothing
    TUniquePtr<FSOLHoloFrameOutput> empty = MakeUnique<FSOLHoloFrameOutput>();
    FSOLHoloMenuLook hidden = HoloKitOpenLook(1.0f);
    hidden.bVisible = false;
    SOLHoloFrameBuild::BuildFrame(HoloKitOverlaySpec(1.0f), hidden, 1.0f, *empty);
    const int32 before = batch.GetShapeCount();
    TestEqual(TEXT("empty: returns 0"), AppendToBatch(*empty, batch), 0);
    TestEqual(TEXT("empty: batch unchanged"), batch.GetShapeCount(), before);

    // A batch too small takes what fits; the rest are its dropped count
    FSOLHudShapeBatch small;
    const int32 smallCapacity = 5;
    small.Initialize(smallCapacity);
    TestEqual(TEXT("small batch: returns what fits"), AppendToBatch(*out, small), smallCapacity);
    TestEqual(TEXT("small batch: holds its capacity"), small.GetShapeCount(), smallCapacity);
    TestEqual(TEXT("small batch: the rest dropped"), small.GetDroppedCount(), n - smallCapacity);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitBatchNoGrowthTest, "SOLTest.HoloMenuKit.BatchNoGrowth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// After Initialize(FRAME_MAX_SHAPES), 200 rounds of Reset, BuildFrame and AppendToBatch (worst-case spec, a whole
// open / hover / close / reopen cycle) never grow the buffers and never drop a shape
bool FSOLHoloMenuKitBatchNoGrowthTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    FSOLHudShapeBatch batch;
    batch.Initialize(FRAME_MAX_SHAPES);
    const int32 vertexMax = batch.GetVertices().Max();
    const int32 indexMax = batch.GetIndices().Max();
    const SIZE_T vertexBytes = batch.GetVertices().GetAllocatedSize();
    const SIZE_T indexBytes = batch.GetIndices().GetAllocatedSize();

    TUniquePtr<FSOLHoloFrameOutput> out = MakeUnique<FSOLHoloFrameOutput>();
    const FSOLHoloFrameSpec spec = HoloKitWorstCaseSpec();
    FSOLHoloMenuAnim anim;
    const double startS = 300.0;
    SOLHoloMenu::BeginOpen(anim, startS, 1.0f);
    int32 dropRounds = 0;
    int32 shortRounds = 0;
    for (int32 round = 0; round < HOLOKITTEST_BATCH_ROUNDS; ++round)
    {
        // A whole menu life: hover at 50, close at 100, reopen at 150
        const double nowS = startS + 0.01 * round;
        if (round == 50)
        {
            SOLHoloMenu::SetHovered(anim, 2, nowS);
        }
        if (round == 100)
        {
            SOLHoloMenu::BeginClose(anim, nowS, 1.0f);
        }
        if (round == 150)
        {
            SOLHoloMenu::BeginOpen(anim, nowS, 1.0f);
            SOLHoloMenu::SetHovered(anim, 1, nowS);
        }
        SOLHoloMenu::Advance(anim, nowS, 1.0f);
        batch.Reset();
        const int32 count = SOLHoloFrameBuild::BuildFrame(spec, SOLHoloMenu::Evaluate(anim, nowS, 1.0f), 1.0f, *out);
        const int32 taken = SOLHoloFrameBuild::AppendToBatch(*out, batch);
        dropRounds += batch.GetDroppedCount() != 0 ? 1 : 0;
        shortRounds += taken != count ? 1 : 0;
    }
    TestEqual(TEXT("vertex capacity unchanged"), batch.GetVertices().Max(), vertexMax);
    TestEqual(TEXT("index capacity unchanged"), batch.GetIndices().Max(), indexMax);
    TestTrue(TEXT("vertex bytes unchanged"), batch.GetVertices().GetAllocatedSize() == vertexBytes);
    TestTrue(TEXT("index bytes unchanged"), batch.GetIndices().GetAllocatedSize() == indexBytes);
    TestEqual(TEXT("no round dropped a shape"), dropRounds, 0);
    TestEqual(TEXT("every round took every shape"), shortRounds, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitBackingOpacityTest, "SOLTest.HoloMenuKit.BackingOpacity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// BackingOpacity is clamp(alpha / HUD_PANEL_BACKING_COLOR.A, 0, 1), NaN 0; FLAT_PANEL_ALPHA gives 1
bool FSOLHoloMenuKitBackingOpacityTest::RunTest(const FString& /*parameters*/)
{
    using SOLHoloFrameBuild::BackingOpacity;
    const float backingA = SOL::Hologram::HUD_PANEL_BACKING_COLOR.A;
    TestEqual(TEXT("FLAT_PANEL_ALPHA gives 1"), BackingOpacity(SOL::HoloMenu::FLAT_PANEL_ALPHA), 1.0f,
        HOLOKITTEST_VALUE_TOLERANCE);
    TestEqual(TEXT("0 gives 0"), BackingOpacity(0.0f), 0.0f);
    TestEqual(TEXT("BACKDROP_ALPHA_PER_UNIT"), BackingOpacity(SOL::HoloMenu::BACKDROP_ALPHA_PER_UNIT),
        SOL::HoloMenu::BACKDROP_ALPHA_PER_UNIT / backingA, HOLOKITTEST_VALUE_TOLERANCE);
    TestEqual(TEXT("0.3"), BackingOpacity(0.3f), 0.3f / backingA, HOLOKITTEST_VALUE_TOLERANCE);
    TestEqual(TEXT("above the backing alpha clamps to 1"), BackingOpacity(0.9f), 1.0f);
    TestEqual(TEXT("negative clamps to 0"), BackingOpacity(-0.2f), 0.0f);
    TestEqual(TEXT("NaN gives 0"), BackingOpacity(HOLOKITTEST_NAN), 0.0f);
    TestEqual(TEXT("+inf gives 1"), BackingOpacity(HOLOKITTEST_INF), 1.0f);
    TestEqual(TEXT("-inf gives 0"), BackingOpacity(-HOLOKITTEST_INF), 0.0f);
    return true;
}

// --- Menu subsystem testable parts (section 4.6) ---

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitCueEnumTest, "SOLTest.HoloMenuKit.CueEnum",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// ESOLHoloCue values index SOL::HoloMenu::CUES like CUE_OPEN / CUE_HOVER / CUE_SELECT, Count is the table size
bool FSOLHoloMenuKitCueEnumTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    TestEqual(TEXT("Open = CUE_OPEN"), static_cast<int32>(ESOLHoloCue::Open), static_cast<int32>(CUE_OPEN));
    TestEqual(TEXT("Hover = CUE_HOVER"), static_cast<int32>(ESOLHoloCue::Hover), static_cast<int32>(CUE_HOVER));
    TestEqual(TEXT("Select = CUE_SELECT"), static_cast<int32>(ESOLHoloCue::Select), static_cast<int32>(CUE_SELECT));
    TestEqual(TEXT("Count = the CUES table size"), static_cast<int32>(ESOLHoloCue::Count),
        static_cast<int32>(UE_ARRAY_COUNT(CUES)));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitParamSanitizeTest, "SOLTest.HoloMenuKit.ParamCacheSanitize",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SanitizeStrength: NaN 0, else clamped to [HOLO_MENU_EFFECTS_MIN, HOLO_MENU_EFFECTS_MAX]
bool FSOLHoloMenuKitParamSanitizeTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Settings;
    TestEqual(TEXT("NaN gives 0"), FSOLHoloMenuParamCache::SanitizeStrength(HOLOKITTEST_NAN), 0.0f);
    TestEqual(TEXT("+inf gives 2"), FSOLHoloMenuParamCache::SanitizeStrength(HOLOKITTEST_INF), HOLO_MENU_EFFECTS_MAX);
    TestEqual(TEXT("-inf gives 0"), FSOLHoloMenuParamCache::SanitizeStrength(-HOLOKITTEST_INF), HOLO_MENU_EFFECTS_MIN);
    TestEqual(TEXT("-1 clamps to MIN"), FSOLHoloMenuParamCache::SanitizeStrength(-1.0f), HOLO_MENU_EFFECTS_MIN);
    TestEqual(TEXT("3 clamps to MAX"), FSOLHoloMenuParamCache::SanitizeStrength(3.0f), HOLO_MENU_EFFECTS_MAX);
    TestEqual(TEXT("0 stays"), FSOLHoloMenuParamCache::SanitizeStrength(0.0f), 0.0f);
    TestEqual(TEXT("1.35 stays"), FSOLHoloMenuParamCache::SanitizeStrength(1.35f), 1.35f);
    TestEqual(TEXT("2 stays"), FSOLHoloMenuParamCache::SanitizeStrength(2.0f), 2.0f);
    TestEqual(TEXT("MIN and MAX are 0 and 2"), HOLO_MENU_EFFECTS_MAX - HOLO_MENU_EFFECTS_MIN, 2.0f);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitParamUpdateTest, "SOLTest.HoloMenuKit.ParamCacheUpdate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Update asks for a push on the first call and on any exact change of the sanitized strength or a valid view size,
// counting each push; an invalid view size keeps the last pushed one
bool FSOLHoloMenuKitParamUpdateTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::Settings;
    const FVector2f sizeA(1920.0f, 1080.0f);
    const FVector2f sizeB(1280.0f, 720.0f);

    FSOLHoloMenuParamCache cache;
    TestEqual(TEXT("fresh: strength is the default"), cache.GetStrength(), HOLO_MENU_EFFECTS_DEFAULT);
    TestEqual(TEXT("fresh: no push"), static_cast<int32>(cache.GetPushCount()), 0);

    // The first call always pushes, even with the default strength
    TestTrue(TEXT("first call pushes"), cache.Update(HOLO_MENU_EFFECTS_DEFAULT, sizeA));
    TestEqual(TEXT("first call: 1 push"), static_cast<int32>(cache.GetPushCount()), 1);
    TestFalse(TEXT("same values: no push"), cache.Update(HOLO_MENU_EFFECTS_DEFAULT, sizeA));
    TestEqual(TEXT("same values: still 1 push"), static_cast<int32>(cache.GetPushCount()), 1);

    // Strength and size changes
    TestTrue(TEXT("strength 0.5 pushes"), cache.Update(0.5f, sizeA));
    TestEqual(TEXT("strength 0.5 stored"), cache.GetStrength(), 0.5f);
    TestTrue(TEXT("size B pushes"), cache.Update(0.5f, sizeB));
    TestFalse(TEXT("size B again: no push"), cache.Update(0.5f, sizeB));
    TestEqual(TEXT("3 pushes"), static_cast<int32>(cache.GetPushCount()), 3);

    // Exact comparison: one float step is a change
    const float nextUp = std::nextafter(0.5f, 1.0f);
    TestTrue(TEXT("one float step up pushes"), cache.Update(nextUp, sizeB));
    TestTrue(TEXT("and back pushes"), cache.Update(0.5f, sizeB));
    TestTrue(TEXT("a sub-pixel size change pushes"), cache.Update(0.5f, FVector2f(1280.0f, 720.5f)));
    TestTrue(TEXT("and back pushes"), cache.Update(0.5f, sizeB));
    TestEqual(TEXT("7 pushes"), static_cast<int32>(cache.GetPushCount()), 7);

    // An invalid view size keeps the last pushed size
    const FVector2f badSizes[] = {
        FVector2f(HOLOKITTEST_NAN, 720.0f), FVector2f(1280.0f, HOLOKITTEST_INF), FVector2f(0.0f, 720.0f),
        FVector2f(-5.0f, 720.0f), FVector2f(1280.0f, 0.0f), FVector2f(HOLOKITTEST_NAN, HOLOKITTEST_NAN) };
    for (int32 i = 0; i < static_cast<int32>(UE_ARRAY_COUNT(badSizes)); ++i)
    {
        TestFalse(FString::Printf(TEXT("invalid size %d: no push"), i), cache.Update(0.5f, badSizes[i]));
    }
    TestFalse(TEXT("size B after the invalid sizes: still the pushed size"), cache.Update(0.5f, sizeB));
    TestEqual(TEXT("still 7 pushes"), static_cast<int32>(cache.GetPushCount()), 7);
    TestTrue(TEXT("strength change with an invalid size pushes"), cache.Update(0.25f, badSizes[0]));
    TestFalse(TEXT("then size B: the kept size, no push"), cache.Update(0.25f, sizeB));
    TestEqual(TEXT("8 pushes"), static_cast<int32>(cache.GetPushCount()), 8);

    // Sanitized strengths compare after sanitizing
    TestTrue(TEXT("5 pushes (reads 2)"), cache.Update(5.0f, sizeB));
    TestEqual(TEXT("5 stored as 2"), cache.GetStrength(), HOLO_MENU_EFFECTS_MAX);
    TestFalse(TEXT("2 after 5: no push"), cache.Update(2.0f, sizeB));
    TestFalse(TEXT("+inf after 2: no push"), cache.Update(HOLOKITTEST_INF, sizeB));
    TestTrue(TEXT("NaN pushes (reads 0)"), cache.Update(HOLOKITTEST_NAN, sizeB));
    TestEqual(TEXT("NaN stored as 0"), cache.GetStrength(), 0.0f);
    TestFalse(TEXT("-3 after NaN: no push"), cache.Update(-3.0f, sizeB));
    TestFalse(TEXT("0 after NaN: no push"), cache.Update(0.0f, sizeB));
    TestFalse(TEXT("-inf after NaN: no push"), cache.Update(-HOLOKITTEST_INF, sizeB));
    TestEqual(TEXT("10 pushes"), static_cast<int32>(cache.GetPushCount()), 10);

    // A first call with an invalid size still pushes
    FSOLHoloMenuParamCache fresh;
    TestTrue(TEXT("first call with an invalid size pushes"), fresh.Update(1.0f, badSizes[0]));
    TestEqual(TEXT("its push counted"), static_cast<int32>(fresh.GetPushCount()), 1);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitCueGateTest, "SOLTest.HoloMenuKit.CueGate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Request counts every call and accepts a finite time on the first acceptance or CUES[cue].MinIntervalS after the
// last one; NaN, infinite and backwards times are rejected, out-of-range cues are ignored, cues are independent
bool FSOLHoloMenuKitCueGateTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const ESOLHoloCue cues[] = { ESOLHoloCue::Open, ESOLHoloCue::Hover, ESOLHoloCue::Select };

    // Every cue on its own interval
    for (const ESOLHoloCue cue : cues)
    {
        const double interval = CUES[static_cast<int32>(cue)].MinIntervalS;
        const FString label = FString::Printf(TEXT("cue %d"), static_cast<int32>(cue));
        FSOLHoloCueGate gate;
        TestEqual(label + TEXT(": fresh requests 0"), static_cast<int32>(gate.GetRequestCount(cue)), 0);
        TestEqual(label + TEXT(": fresh accepted 0"), static_cast<int32>(gate.GetAcceptedCount(cue)), 0);

        TestTrue(label + TEXT(": first request accepted"), gate.Request(cue, 0.0));
        TestFalse(label + TEXT(": half an interval later rejected"), gate.Request(cue, 0.5 * interval));
        TestTrue(label + TEXT(": one interval after the accepted one (the rejected one does not count)"),
            gate.Request(cue, interval));
        TestFalse(label + TEXT(": just under the next interval rejected"),
            gate.Request(cue, std::nextafter(2.0 * interval, 0.0)));
        TestTrue(label + TEXT(": exactly the next interval accepted"), gate.Request(cue, 2.0 * interval));
        TestEqual(label + TEXT(": 5 requests"), static_cast<int32>(gate.GetRequestCount(cue)), 5);
        TestEqual(label + TEXT(": 3 accepted"), static_cast<int32>(gate.GetAcceptedCount(cue)), 3);

        // NaN, infinite and backwards times
        TestFalse(label + TEXT(": NaN rejected"), gate.Request(cue, HOLOKITTEST_NAN_D));
        TestFalse(label + TEXT(": +inf rejected"), gate.Request(cue, HOLOKITTEST_INF_D));
        TestFalse(label + TEXT(": -inf rejected"), gate.Request(cue, -HOLOKITTEST_INF_D));
        TestFalse(label + TEXT(": a clock that went back rejected"), gate.Request(cue, 2.0 * interval - 5.0));
        TestEqual(label + TEXT(": 9 requests"), static_cast<int32>(gate.GetRequestCount(cue)), 9);
        TestEqual(label + TEXT(": still 3 accepted"), static_cast<int32>(gate.GetAcceptedCount(cue)), 3);
        TestTrue(label + TEXT(": a later time accepted again"), gate.Request(cue, 1000.0));
        TestEqual(label + TEXT(": 4 accepted"), static_cast<int32>(gate.GetAcceptedCount(cue)), 4);
    }

    // A first request at NaN or infinity is not an acceptance; a finite one after it is the first
    {
        FSOLHoloCueGate gate;
        TestFalse(TEXT("first request NaN rejected"), gate.Request(ESOLHoloCue::Hover, HOLOKITTEST_NAN_D));
        TestFalse(TEXT("first request +inf rejected"), gate.Request(ESOLHoloCue::Hover, HOLOKITTEST_INF_D));
        TestTrue(TEXT("then a finite time is the first acceptance"), gate.Request(ESOLHoloCue::Hover, 3.0));
        TestTrue(TEXT("a negative finite time can be a first acceptance"), gate.Request(ESOLHoloCue::Select, -50.0));
        TestEqual(TEXT("hover: 3 requests"), static_cast<int32>(gate.GetRequestCount(ESOLHoloCue::Hover)), 3);
        TestEqual(TEXT("hover: 1 accepted"), static_cast<int32>(gate.GetAcceptedCount(ESOLHoloCue::Hover)), 1);
    }

    // Cues are independent of each other
    {
        FSOLHoloCueGate gate;
        TestTrue(TEXT("open at 1"), gate.Request(ESOLHoloCue::Open, 1.0));
        TestTrue(TEXT("hover at 1 (its own first)"), gate.Request(ESOLHoloCue::Hover, 1.0));
        TestTrue(TEXT("select at 1 (its own first)"), gate.Request(ESOLHoloCue::Select, 1.0));
        TestFalse(TEXT("hover again at 1 rejected"), gate.Request(ESOLHoloCue::Hover, 1.0));
        TestEqual(TEXT("open: 1 request"), static_cast<int32>(gate.GetRequestCount(ESOLHoloCue::Open)), 1);
        TestEqual(TEXT("open: 1 accepted"), static_cast<int32>(gate.GetAcceptedCount(ESOLHoloCue::Open)), 1);
        TestEqual(TEXT("hover: 2 requests"), static_cast<int32>(gate.GetRequestCount(ESOLHoloCue::Hover)), 2);
        TestEqual(TEXT("select: 1 accepted"), static_cast<int32>(gate.GetAcceptedCount(ESOLHoloCue::Select)), 1);

        // Out-of-range cues: false, nothing counted
        TestFalse(TEXT("Count rejected"), gate.Request(ESOLHoloCue::Count, 10.0));
        TestFalse(TEXT("cue 200 rejected"), gate.Request(static_cast<ESOLHoloCue>(200), 10.0));
        TestEqual(TEXT("open requests unchanged"), static_cast<int32>(gate.GetRequestCount(ESOLHoloCue::Open)), 1);
        TestEqual(TEXT("hover requests unchanged"), static_cast<int32>(gate.GetRequestCount(ESOLHoloCue::Hover)), 2);
        TestEqual(TEXT("select requests unchanged"), static_cast<int32>(gate.GetRequestCount(ESOLHoloCue::Select)),
            1);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitSubsystemTest, "SOLTest.HoloMenuKit.SubsystemBare",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A bare, never-initialized subsystem (no world, settings, MID or sounds): the default strength, pushes counted
// without a MID, cues answered and counted by the gate, never a crash
bool FSOLHoloMenuKitSubsystemTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    USOLHoloMenuSubsystem* const subsystem = NewObject<USOLHoloMenuSubsystem>();
    if (!TestNotNull(TEXT("subsystem"), subsystem))
    {
        return false;
    }
    TestEqual(TEXT("strength: the default with no settings subsystem"), subsystem->GetStrength(),
        SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);
    TestNull(TEXT("no menu material before begin play"), subsystem->GetMenuMaterial());
    TestTrue(TEXT("no material view size yet"), subsystem->GetMaterialViewSizePx() == FVector2f::ZeroVector);

    // Parameter pushes are counted with no MID
    TestEqual(TEXT("no push yet"), static_cast<int32>(subsystem->GetParamPushCount()), 0);
    subsystem->UpdateMaterial(FVector2f(1280.0f, 720.0f));
    TestEqual(TEXT("first update pushes"), static_cast<int32>(subsystem->GetParamPushCount()), 1);
    subsystem->UpdateMaterial(FVector2f(1280.0f, 720.0f));
    TestEqual(TEXT("same size: no push"), static_cast<int32>(subsystem->GetParamPushCount()), 1);
    subsystem->UpdateMaterial(FVector2f(1920.0f, 1080.0f));
    TestEqual(TEXT("new size pushes"), static_cast<int32>(subsystem->GetParamPushCount()), 2);
    subsystem->UpdateMaterial(FVector2f(HOLOKITTEST_NAN, 1080.0f));
    TestEqual(TEXT("invalid size: no push"), static_cast<int32>(subsystem->GetParamPushCount()), 2);
    TestEqual(TEXT("strength unchanged"), subsystem->GetStrength(), SOL::Settings::HOLO_MENU_EFFECTS_DEFAULT);

    // Cues: the gate's answers and counters, with no world, audio device or loaded sound
    const double openInterval = CUES[CUE_OPEN].MinIntervalS;
    TestTrue(TEXT("open accepted"), subsystem->PlayCue(ESOLHoloCue::Open, 0.0));
    TestFalse(TEXT("open within its interval rejected"), subsystem->PlayCue(ESOLHoloCue::Open, 0.5 * openInterval));
    TestTrue(TEXT("hover accepted (independent)"), subsystem->PlayCue(ESOLHoloCue::Hover, 0.0));
    TestTrue(TEXT("select accepted (independent)"), subsystem->PlayCue(ESOLHoloCue::Select, 0.0));
    TestFalse(TEXT("open at NaN rejected"), subsystem->PlayCue(ESOLHoloCue::Open, HOLOKITTEST_NAN_D));
    TestFalse(TEXT("Count rejected"), subsystem->PlayCue(ESOLHoloCue::Count, 1.0));
    TestTrue(TEXT("open one interval later accepted"), subsystem->PlayCue(ESOLHoloCue::Open, openInterval));
    TestEqual(TEXT("open: 4 requests"), static_cast<int32>(subsystem->GetCueRequestCount(ESOLHoloCue::Open)), 4);
    TestEqual(TEXT("open: 2 accepted"), static_cast<int32>(subsystem->GetCueAcceptedCount(ESOLHoloCue::Open)), 2);
    TestEqual(TEXT("hover: 1 request"), static_cast<int32>(subsystem->GetCueRequestCount(ESOLHoloCue::Hover)), 1);
    TestEqual(TEXT("hover: 1 accepted"), static_cast<int32>(subsystem->GetCueAcceptedCount(ESOLHoloCue::Hover)), 1);
    TestEqual(TEXT("select: 1 accepted"), static_cast<int32>(subsystem->GetCueAcceptedCount(ESOLHoloCue::Select)), 1);

    // No world and no loaded sound: accepted cues never start a sound
    TestEqual(TEXT("open: none played"), subsystem->GetCuePlayedCount(ESOLHoloCue::Open), 0);
    TestEqual(TEXT("hover: none played"), subsystem->GetCuePlayedCount(ESOLHoloCue::Hover), 0);
    TestEqual(TEXT("select: none played"), subsystem->GetCuePlayedCount(ESOLHoloCue::Select), 0);
    TestEqual(TEXT("Count: 0 played"), subsystem->GetCuePlayedCount(ESOLHoloCue::Count), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloMenuKitLinePadTest, "SOLTest.HoloMenuKit.LinePadAndHelpers",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// LinePad is lerp(FRAME_FLAT_PAD_PX, HUD_GLOW_PAD_PX, GlowScale(s)) (flat at s <= 0 and NaN, full glow from 1);
// IsValidRect and UiScale are the geometry's own rect check and UI scale clamp
bool FSOLHoloMenuKitLinePadTest::RunTest(const FString& /*parameters*/)
{
    using namespace SOL::HoloMenu;
    const float flatPad = static_cast<float>(FRAME_FLAT_PAD_PX);
    const float glowPad = SOL::Hologram::HUD_GLOW_PAD_PX;
    TestEqual(TEXT("s = 0: flat"), SOLHoloFrameBuild::LinePad(0.0f), flatPad);
    TestEqual(TEXT("s < 0: flat"), SOLHoloFrameBuild::LinePad(-1.0f), flatPad);
    TestEqual(TEXT("NaN: flat"), SOLHoloFrameBuild::LinePad(HOLOKITTEST_NAN), flatPad);
    TestEqual(TEXT("s = 1: full glow"), SOLHoloFrameBuild::LinePad(1.0f), glowPad, HOLOKITTEST_PAD_TOLERANCE);
    TestEqual(TEXT("s = 2: full glow"), SOLHoloFrameBuild::LinePad(2.0f), glowPad, HOLOKITTEST_PAD_TOLERANCE);
    TestEqual(TEXT("s = 0.5: the GlowScale lerp"), SOLHoloFrameBuild::LinePad(0.5f),
        FMath::Lerp(flatPad, glowPad, SOLHoloMenu::GlowScale(0.5f)), HOLOKITTEST_PAD_TOLERANCE);

    TestTrue(TEXT("valid rect"), SOLHoloMenu::IsValidRect(HoloKitRect(0.0f, 0.0f, 10.0f, 5.0f)));
    TestFalse(TEXT("zero width"), SOLHoloMenu::IsValidRect(HoloKitRect(0.0f, 0.0f, 0.0f, 5.0f)));
    TestFalse(TEXT("NaN corner"), SOLHoloMenu::IsValidRect(HoloKitRect(HOLOKITTEST_NAN, 0.0f, 10.0f, 5.0f)));
    TestEqual(TEXT("uiScale 1.5 kept"), SOLHoloMenu::UiScale(1.5f), 1.5f);
    TestEqual(TEXT("uiScale NaN reads 1"), SOLHoloMenu::UiScale(HOLOKITTEST_NAN), 1.0f);
    TestEqual(TEXT("uiScale 0 reads 1"), SOLHoloMenu::UiScale(0.0f), 1.0f);
    TestEqual(TEXT("uiScale clamps to UI_SCALE_MAX"), SOLHoloMenu::UiScale(1000.0f), UI_SCALE_MAX);
    TestEqual(TEXT("uiScale clamps to UI_SCALE_MIN"), SOLHoloMenu::UiScale(0.01f), UI_SCALE_MIN);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLHoloKitCardRectTest, "SOLTest.HoloMenuKit.CardRect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// CardRect is the content grown by HUD_PANEL_PADDING_PX x the UI scale, the rect BuildCard's fill covers
bool FSOLHoloKitCardRectTest::RunTest(const FString& /*parameters*/)
{
    const FBox2f content = HoloKitRect(100.0f, 40.0f, 180.0f, 70.0f);
    const float scales[] = { 0.5f, 1.0f, 2.0f, HOLOKITTEST_NAN };
    for (const float uiScale : scales)
    {
        const FBox2f expected = HoloKitCardRect(content, uiScale);
        const FBox2f actual = SOLHoloFrameBuild::CardRect(content, uiScale);
        TestTrue(FString::Printf(TEXT("uiScale %.2f: min"), uiScale), actual.Min.Equals(expected.Min, 1.0e-4f));
        TestTrue(FString::Printf(TEXT("uiScale %.2f: max"), uiScale), actual.Max.Equals(expected.Max, 1.0e-4f));
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
