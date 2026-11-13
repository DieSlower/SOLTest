/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLLaneTunnelLabel.h"
#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "UI/SOLHudFormat.h"

#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #49 lane tunnel wall labels (SOLLaneTunnelLabel, SDD 49 section 6 cases 18-20, decision Q4),
// written against that contract before the production code existed (part 49a, test first): a label box clear of the
// projected guide-dot rows, the greedy placement of up to NPC_TUNNEL_LABEL_MAX_COUNT labels strongest first (12
// o'clock, else 6 o'clock, else hidden; never over a row or another label) and the three text lines (no side line).
// Review amendment (2026-11-12): the box sits outward from the tube (above a 12 o'clock anchor, below a 6 o'clock one),
// the properties test checks the clearance with its own distance, and a bad size or viewport places nothing. Part 49b
// (test first): the quantised text key and the label alpha's ease. Contract change (2026-11-12, test first): the
// placement also keeps clear of HUD panel avoid boxes grown by the gap, falls back to a compact one-line form (full@12,
// full@6, compact@12, compact@6), and the compact line, nearest distance and compact key. Contract change (2026-11-12,
// test first): placement hysteresis, last frame's candidate and form are tried first and kept while usable.

// Helpers here carry names unique across the test files so unity builds do not collide
namespace
{
    constexpr double LANELABEL_TOL = 1.0e-9;
    constexpr int32 LANELABEL_TOP = 0;                       // Candidate index of the 12 o'clock anchor
    constexpr int32 LANELABEL_BOTTOM = 1;                    // Candidate index of the 6 o'clock anchor
    constexpr double LANELABEL_ROW_MARGIN_PX = 7.5;          // Dot margin plus dot radius used by the placement tests
    constexpr double LANELABEL_GAP_PX = 6.0;                 // Gap between placed labels used by the tests
    const FVector2D LANELABEL_VIEWPORT(1920.0, 1080.0);
    const FVector2D LANELABEL_SIZE(180.0, 54.0);

    //////////////////////////////////////////////////////////////////////////
    // Returns a label input with both anchors in front of the camera and not occluded
    FSOLLaneLabelInput LaneLabelInput(const FVector2D& topPx, const FVector2D& bottomPx,
        const FVector2D& sizePx = LANELABEL_SIZE)
    {
        FSOLLaneLabelInput input;
        input.TopAnchorPx = topPx;
        input.BottomAnchorPx = bottomPx;
        input.SizePx = sizePx;
        input.bTopInFront = true;
        input.bTopOccluded = false;
        input.bBottomInFront = true;
        input.bBottomOccluded = false;
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs the placement with the tests' viewport and margins (no avoid boxes unless given); the outputs are
    // pre-filled with a placed compact garbage value so every entry must be written
    int32 LaneLabelPlace(const TArray<FSOLLaneLabelInput>& labels, const TArray<FVector2D>& rowStarts,
        const TArray<FVector2D>& rowEnds, TArray<FSOLLaneLabelPlacement>& outPlacements,
        TConstArrayView<FBox2D> avoidBoxes = TConstArrayView<FBox2D>())
    {
        FSOLLaneLabelPlacement garbage;
        garbage.bPlaced = true;
        garbage.bCompact = true;
        garbage.Candidate = 7;
        garbage.BoxPx = FBox2D(FVector2D(-5.0, -5.0), FVector2D(-1.0, -1.0));
        outPlacements.Init(garbage, labels.Num());
        return SOLLaneTunnelLabel::PlaceLaneLabels(labels, rowStarts, rowEnds, avoidBoxes, LANELABEL_VIEWPORT,
            LANELABEL_ROW_MARGIN_PX, LANELABEL_GAP_PX, outPlacements);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a label input like LaneLabelInput with a compact size as well
    FSOLLaneLabelInput LaneLabelCompactInput(const FVector2D& topPx, const FVector2D& bottomPx,
        const FVector2D& sizePx, const FVector2D& compactSizePx)
    {
        FSOLLaneLabelInput input = LaneLabelInput(topPx, bottomPx, sizePx);
        input.CompactSizePx = compactSizePx;
        return input;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns an avoid box with the given corners but marked invalid (bIsValid false)
    FBox2D LaneLabelInvalidBox(const FVector2D& minPx, const FVector2D& maxPx)
    {
        FBox2D box(minPx, maxPx);
        box.bIsValid = false;
        return box;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when box a, grown by the gap on every side, overlaps box b (touching does not count)
    bool LaneLabelBoxesOverlap(const FBox2D& a, const FBox2D& b, const double gapPx)
    {
        constexpr double touch = 1.0e-9;
        return a.Min.X - gapPx < b.Max.X - touch && b.Min.X < a.Max.X + gapPx - touch
            && a.Min.Y - gapPx < b.Max.Y - touch && b.Min.Y < a.Max.Y + gapPx - touch;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when two placements agree (placed flag, and form, candidate and box when placed)
    bool LaneLabelSamePlacement(const FSOLLaneLabelPlacement& a, const FSOLLaneLabelPlacement& b)
    {
        if (a.bPlaced != b.bPlaced)
        {
            return false;
        }
        if (!a.bPlaced)
        {
            return true;
        }
        return a.bCompact == b.bCompact && a.Candidate == b.Candidate
            && a.BoxPx.Min.Equals(b.BoxPx.Min, LANELABEL_TOL) && a.BoxPx.Max.Equals(b.BoxPx.Max, LANELABEL_TOL);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance from a point to a segment (a zero-length segment is a point)
    double LaneLabelPointSegmentDistance(const FVector2D& point, const FVector2D& a, const FVector2D& b)
    {
        const FVector2D ab = b - a;
        const double lengthSq = ab.SizeSquared();
        const double t = lengthSq > 0.0 ? FMath::Clamp(FVector2D::DotProduct(point - a, ab) / lengthSq, 0.0, 1.0)
            : 0.0;
        return (point - (a + ab * t)).Size();
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the sign of the cross product (b - a) x (c - a)
    int32 LaneLabelOrientation(const FVector2D& a, const FVector2D& b, const FVector2D& c)
    {
        const double cross = FVector2D::CrossProduct(b - a, c - a);
        return cross > 0.0 ? 1 : (cross < 0.0 ? -1 : 0);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the distance between two segments (0 when they cross)
    double LaneLabelSegmentDistance(const FVector2D& p0, const FVector2D& p1, const FVector2D& q0, const FVector2D& q1)
    {
        const int32 o1 = LaneLabelOrientation(p0, p1, q0);
        const int32 o2 = LaneLabelOrientation(p0, p1, q1);
        const int32 o3 = LaneLabelOrientation(q0, q1, p0);
        const int32 o4 = LaneLabelOrientation(q0, q1, p1);
        if (o1 * o2 < 0 && o3 * o4 < 0)
        {
            return 0.0;
        }
        return FMath::Min(FMath::Min(LaneLabelPointSegmentDistance(p0, q0, q1), LaneLabelPointSegmentDistance(p1, q0,
            q1)), FMath::Min(LaneLabelPointSegmentDistance(q0, p0, p1), LaneLabelPointSegmentDistance(q1, p0, p1)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Euclidean distance from a segment to a box (0 when it touches or enters it), independent of the
    // production clipping test
    double LaneLabelSegmentBoxDistance(const FVector2D& a, const FVector2D& b, const FBox2D& box)
    {
        const auto inside = [&box](const FVector2D& point)
        {
            return point.X >= box.Min.X && point.X <= box.Max.X && point.Y >= box.Min.Y && point.Y <= box.Max.Y;
        };
        if (inside(a) || inside(b))
        {
            return 0.0;
        }
        const FVector2D corners[] = { box.Min, FVector2D(box.Max.X, box.Min.Y), box.Max, FVector2D(box.Min.X,
            box.Max.Y) };
        double distance = TNumericLimits<double>::Max();
        for (int32 edge = 0; edge < 4; ++edge)
        {
            distance = FMath::Min(distance, LaneLabelSegmentDistance(a, b, corners[edge], corners[(edge + 1) % 4]));
        }
        return distance;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a full-height vertical row at a screen x (crosses any box centred horizontally on that x)
    void LaneLabelAddVerticalRow(const double xPx, TArray<FVector2D>& rowStarts, TArray<FVector2D>& rowEnds)
    {
        rowStarts.Add(FVector2D(xPx, -10.0));
        rowEnds.Add(FVector2D(xPx, LANELABEL_VIEWPORT.Y + 10.0));
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelClearTest, "SOLTest.LaneTunnelLabel.ClearOfDots",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 18: a label box is clear of the dot rows when the box, grown by the margin, meets none of the row segments; a
// row crossing it, ending inside it or passing within the margin is not clear; a point row is tested as a point
bool FSOLLaneTunnelLabelClearTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::LaneLabelClearOfDots;
    const FBox2D box(FVector2D(100.0, 100.0), FVector2D(200.0, 140.0));
    constexpr double margin = 6.0;

    // Returns the clear result for a single row
    const auto clearOf = [&box](const FVector2D& start, const FVector2D& end)
    {
        const FVector2D starts[] = { start };
        const FVector2D ends[] = { end };
        return LaneLabelClearOfDots(box, starts, ends, margin);
    };

    const TArray<FVector2D> noRows;
    TestTrue(TEXT("no rows: clear"), LaneLabelClearOfDots(box, noRows, noRows, margin));
    TestFalse(TEXT("a row crossing the box"), clearOf(FVector2D(50.0, 120.0), FVector2D(250.0, 120.0)));
    TestFalse(TEXT("a vertical row crossing the box"), clearOf(FVector2D(150.0, 0.0), FVector2D(150.0, 500.0)));
    TestFalse(TEXT("a diagonal row through the middle"), clearOf(FVector2D(60.0, 60.0), FVector2D(240.0, 180.0)));
    TestFalse(TEXT("a row ending inside the box"), clearOf(FVector2D(150.0, 120.0), FVector2D(400.0, 400.0)));
    TestFalse(TEXT("a row wholly inside the box"), clearOf(FVector2D(120.0, 110.0), FVector2D(180.0, 130.0)));
    TestFalse(TEXT("a row 4 px below the box (within the margin)"),
        clearOf(FVector2D(50.0, 144.0), FVector2D(250.0, 144.0)));
    TestFalse(TEXT("a row 5 px left of the box (within the margin)"),
        clearOf(FVector2D(95.0, 0.0), FVector2D(95.0, 300.0)));
    TestTrue(TEXT("a row 7 px below the box (outside the margin)"),
        clearOf(FVector2D(50.0, 147.0), FVector2D(250.0, 147.0)));
    TestTrue(TEXT("a row 10 px above the box"), clearOf(FVector2D(0.0, 90.0), FVector2D(400.0, 90.0)));
    TestTrue(TEXT("a far row"), clearOf(FVector2D(300.0, 0.0), FVector2D(300.0, 500.0)));
    TestTrue(TEXT("a row stopping short of the box"), clearOf(FVector2D(0.0, 120.0), FVector2D(90.0, 120.0)));

    // Point rows
    TestFalse(TEXT("a point inside the box"), clearOf(FVector2D(150.0, 120.0), FVector2D(150.0, 120.0)));
    TestFalse(TEXT("a point 3 px right of the box"), clearOf(FVector2D(203.0, 120.0), FVector2D(203.0, 120.0)));
    TestTrue(TEXT("a point 9 px right of the box"), clearOf(FVector2D(209.0, 120.0), FVector2D(209.0, 120.0)));

    // Several rows: one crossing is enough
    const FVector2D starts[] = { FVector2D(300.0, 0.0), FVector2D(0.0, 90.0), FVector2D(150.0, 0.0) };
    const FVector2D ends[] = { FVector2D(300.0, 500.0), FVector2D(400.0, 90.0), FVector2D(150.0, 500.0) };
    TestFalse(TEXT("three rows, the last crossing"), LaneLabelClearOfDots(box, starts, ends, margin));
    TestTrue(TEXT("the first two rows only"), LaneLabelClearOfDots(box, TConstArrayView<FVector2D>(starts, 2),
        TConstArrayView<FVector2D>(ends, 2), margin));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelDotRadiusTest, "SOLTest.LaneTunnelLabel.DotRadiusMargin",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Review fix (49c): near guide dots are world-sized (tens of pixels at 1-2 tunnel radii), so the clearance grows by
// each row's projected dot radius: LaneLabelDotRadiusPx is half of max(floor, diameter x focal / depth), and a row's
// extra margin is added to the shared one for that row only (D11: a label never overlaps a dot)
bool FSOLLaneTunnelLabelDotRadiusTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::LaneLabelClearOfDots;
    using SOLLaneTunnelLabel::LaneLabelDotRadiusPx;

    // The radius: world-sized near, inverse to the depth, never below half the floor; bad inputs give half the floor
    constexpr double focalPx = 960.0;
    constexpr double floorPx = 3.0;
    constexpr double rtCm = 3.0e8;
    const double diameterCm = SOL::NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII * rtCm;
    TestEqual(TEXT("at 1 Rt: half of 0.034 x 960 px"), LaneLabelDotRadiusPx(rtCm, diameterCm, focalPx, floorPx),
        0.5 * SOL::NPC_TUNNEL_DOT_WORLD_DIAMETER_RADII * focalPx, LANELABEL_TOL);
    TestEqual(TEXT("half the depth, twice the radius"), LaneLabelDotRadiusPx(0.5 * rtCm, diameterCm, focalPx, floorPx),
        2.0 * LaneLabelDotRadiusPx(rtCm, diameterCm, focalPx, floorPx), LANELABEL_TOL);
    TestTrue(TEXT("at 2.5 Rt: well above the floor's 1.5 px"),
        LaneLabelDotRadiusPx(2.5 * rtCm, diameterCm, focalPx, floorPx) > 6.0);
    TestEqual(TEXT("far: the floor"), LaneLabelDotRadiusPx(1000.0 * rtCm, diameterCm, focalPx, floorPx), 0.5 * floorPx,
        LANELABEL_TOL);
    TestEqual(TEXT("zero depth: the floor"), LaneLabelDotRadiusPx(0.0, diameterCm, focalPx, floorPx), 0.5 * floorPx);
    TestEqual(TEXT("negative depth: the floor"), LaneLabelDotRadiusPx(-rtCm, diameterCm, focalPx, floorPx),
        0.5 * floorPx);
    TestEqual(TEXT("NaN depth: the floor"), LaneLabelDotRadiusPx(std::numeric_limits<double>::quiet_NaN(), diameterCm,
        focalPx, floorPx), 0.5 * floorPx);
    TestEqual(TEXT("no diameter: the floor"), LaneLabelDotRadiusPx(rtCm, 0.0, focalPx, floorPx), 0.5 * floorPx);
    TestEqual(TEXT("no focal length: the floor"), LaneLabelDotRadiusPx(rtCm, diameterCm, 0.0, floorPx), 0.5 * floorPx);

    // Per-row extra margins: a row 10 px below the box is clear by 6 px, not by 6 + 8; the extra counts for its own
    // row only, and a row without an entry takes none
    const FBox2D box(FVector2D(100.0, 100.0), FVector2D(200.0, 140.0));
    constexpr double margin = 6.0;
    const FVector2D starts[] = { FVector2D(50.0, 150.0), FVector2D(50.0, 400.0) };
    const FVector2D ends[] = { FVector2D(250.0, 150.0), FVector2D(250.0, 400.0) };
    const double nearExtra[] = { 8.0, 0.0 };
    const double farExtra[] = { 0.0, 8.0 };
    const double shortExtra[] = { 3.0 };
    TestTrue(TEXT("no extras: clear by 6"), LaneLabelClearOfDots(box, starts, ends, margin));
    TestFalse(TEXT("the near row's 8 px dot radius: not clear"), LaneLabelClearOfDots(box, starts, ends, margin,
        nearExtra));
    TestTrue(TEXT("the far row's extra does not reach the box"), LaneLabelClearOfDots(box, starts, ends, margin,
        farExtra));
    TestTrue(TEXT("3 px extra: 9 px still clear of 10"), LaneLabelClearOfDots(box, starts, ends, margin, shortExtra));

    // The placement takes the extras: the 12 o'clock box beside a near row with large dots moves to 6 o'clock
    const FVector2D top(960.0, 300.0);
    const FVector2D bottom(960.0, 800.0);
    const FVector2D rowStarts[] = { FVector2D(860.0, 315.0) };
    const FVector2D rowEnds[] = { FVector2D(1060.0, 315.0) };
    const double rowExtra[] = { 12.0 };
    const FSOLLaneLabelInput labels[] = { LaneLabelInput(top, bottom) };
    FSOLLaneLabelPlacement placements[1];
    TestEqual(TEXT("small dots: placed"), SOLLaneTunnelLabel::PlaceLaneLabels(labels, rowStarts, rowEnds,
        TConstArrayView<FBox2D>(), LANELABEL_VIEWPORT, LANELABEL_ROW_MARGIN_PX, LANELABEL_GAP_PX, placements), 1);
    TestEqual(TEXT("small dots: at 12 o'clock"), placements[0].Candidate, LANELABEL_TOP);
    TestEqual(TEXT("large dots: placed"), SOLLaneTunnelLabel::PlaceLaneLabels(labels, rowStarts, rowEnds,
        TConstArrayView<FBox2D>(), LANELABEL_VIEWPORT, LANELABEL_ROW_MARGIN_PX, LANELABEL_GAP_PX, placements,
        rowExtra), 1);
    TestEqual(TEXT("large dots: moved to 6 o'clock"), placements[0].Candidate, LANELABEL_BOTTOM);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelPlaceRulesTest, "SOLTest.LaneTunnelLabel.PlaceRules",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 19: each label takes its 12 o'clock candidate when that box is on screen, clear of every row and of every
// earlier placed label grown by the gap, else its 6 o'clock candidate, else it is hidden; a candidate behind the
// camera, behind a planet or off screen is skipped; labels are placed in the given order (the strongest first is never
// displaced), at most NPC_TUNNEL_LABEL_MAX_COUNT; a box has the label's size, is centred on its anchor horizontally and
// sits outward from the tube (its bottom edge on a 12 o'clock anchor, its top edge on a 6 o'clock one); a bad label
// size or viewport places nothing
bool FSOLLaneTunnelLabelPlaceRulesTest::RunTest(const FString& /*parameters*/)
{
    TArray<FSOLLaneLabelInput> labels;
    TArray<FVector2D> starts;
    TArray<FVector2D> ends;
    TArray<FSOLLaneLabelPlacement> out;
    const FVector2D top(960.0, 400.0);
    const FVector2D bottom(600.0, 700.0);

    // One label, nothing in the way: 12 o'clock, its size, centred horizontally on the anchor
    labels = { LaneLabelInput(top, bottom) };
    TestEqual(TEXT("free: one placed"), LaneLabelPlace(labels, starts, ends, out), 1);
    TestTrue(TEXT("free: placed"), out[0].bPlaced);
    TestEqual(TEXT("free: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    TestFalse(TEXT("free: full form (no compact size)"), out[0].bCompact);
    TestEqual(TEXT("free: width"), out[0].BoxPx.Max.X - out[0].BoxPx.Min.X, LANELABEL_SIZE.X, LANELABEL_TOL);
    TestEqual(TEXT("free: height"), out[0].BoxPx.Max.Y - out[0].BoxPx.Min.Y, LANELABEL_SIZE.Y, LANELABEL_TOL);
    TestEqual(TEXT("free: centred on the anchor"), 0.5 * (out[0].BoxPx.Min.X + out[0].BoxPx.Max.X), top.X,
        LANELABEL_TOL);
    TestEqual(TEXT("free: above the 12 o'clock anchor"), out[0].BoxPx.Max.Y, top.Y, LANELABEL_TOL);

    // A row through the 12 o'clock anchor: 6 o'clock
    LaneLabelAddVerticalRow(top.X, starts, ends);
    TestEqual(TEXT("top blocked: one placed"), LaneLabelPlace(labels, starts, ends, out), 1);
    TestEqual(TEXT("top blocked: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    TestEqual(TEXT("top blocked: centred on the 6 o'clock anchor"), 0.5 * (out[0].BoxPx.Min.X + out[0].BoxPx.Max.X),
        bottom.X, LANELABEL_TOL);
    TestEqual(TEXT("top blocked: below the 6 o'clock anchor"), out[0].BoxPx.Min.Y, bottom.Y, LANELABEL_TOL);

    // Rows through both anchors: hidden
    LaneLabelAddVerticalRow(bottom.X, starts, ends);
    TestEqual(TEXT("both blocked: none placed"), LaneLabelPlace(labels, starts, ends, out), 0);
    TestFalse(TEXT("both blocked: hidden"), out[0].bPlaced);
    TestEqual(TEXT("both blocked: no candidate"), out[0].Candidate, static_cast<int32>(INDEX_NONE));
    starts.Reset();
    ends.Reset();

    // A 12 o'clock anchor behind the camera, behind a planet or off screen is skipped
    {
        FSOLLaneLabelInput behind = LaneLabelInput(top, bottom);
        behind.bTopInFront = false;
        labels = { behind };
        LaneLabelPlace(labels, starts, ends, out);
        TestEqual(TEXT("top behind the camera: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);

        FSOLLaneLabelInput occluded = LaneLabelInput(top, bottom);
        occluded.bTopOccluded = true;
        labels = { occluded };
        LaneLabelPlace(labels, starts, ends, out);
        TestEqual(TEXT("top behind a planet: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);

        labels = { LaneLabelInput(FVector2D(-1000.0, 400.0), bottom) };
        LaneLabelPlace(labels, starts, ends, out);
        TestEqual(TEXT("top off screen: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);

        FSOLLaneLabelInput neither = LaneLabelInput(FVector2D(-1000.0, 400.0), bottom);
        neither.bBottomInFront = false;
        labels = { neither };
        TestEqual(TEXT("no usable candidate: none placed"), LaneLabelPlace(labels, starts, ends, out), 0);
        TestFalse(TEXT("no usable candidate: hidden"), out[0].bPlaced);
    }

    // Two labels at the same anchors: the first keeps 12 o'clock, the second falls back to its own 6 o'clock
    {
        const FVector2D secondBottom(1400.0, 800.0);
        labels = { LaneLabelInput(top, bottom), LaneLabelInput(top, secondBottom) };
        TestEqual(TEXT("shared top: two placed"), LaneLabelPlace(labels, starts, ends, out), 2);
        TestEqual(TEXT("shared top: first at 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
        TestEqual(TEXT("shared top: second at 6 o'clock"), out[1].Candidate, LANELABEL_BOTTOM);
        const FSOLLaneLabelPlacement firstAlone = [&]()
        {
            TArray<FSOLLaneLabelPlacement> single;
            const TArray<FSOLLaneLabelInput> first = { labels[0] };
            LaneLabelPlace(first, starts, ends, single);
            return single[0];
        }();
        TestTrue(TEXT("shared top: the first is not displaced"), LaneLabelSamePlacement(out[0], firstAlone));

        // The second's 12 o'clock taken by the first and its 6 o'clock behind the camera: hidden
        FSOLLaneLabelInput second = LaneLabelInput(top, secondBottom);
        second.bBottomInFront = false;
        labels = { LaneLabelInput(top, bottom), second };
        TestEqual(TEXT("top taken, bottom unusable: one placed"), LaneLabelPlace(labels, starts, ends, out), 1);
        TestFalse(TEXT("top taken, bottom unusable: second hidden"), out[1].bPlaced);
    }

    // The gap between labels: side by side 3 px apart overlap once grown by the gap, 20 px apart do not
    {
        const FVector2D closeAnchor(top.X + LANELABEL_SIZE.X + 3.0, top.Y);
        const FVector2D distantAnchor(top.X + LANELABEL_SIZE.X + 20.0, top.Y);
        labels = { LaneLabelInput(top, bottom), LaneLabelInput(closeAnchor, FVector2D(1500.0, 900.0)) };
        LaneLabelPlace(labels, starts, ends, out);
        TestEqual(TEXT("3 px apart: the second moves to 6 o'clock"), out[1].Candidate, LANELABEL_BOTTOM);
        labels = { LaneLabelInput(top, bottom), LaneLabelInput(distantAnchor, FVector2D(1500.0, 900.0)) };
        LaneLabelPlace(labels, starts, ends, out);
        TestEqual(TEXT("20 px apart: the second keeps 12 o'clock"), out[1].Candidate, LANELABEL_TOP);
    }

    // At most NPC_TUNNEL_LABEL_MAX_COUNT placed, in the given order
    {
        labels.Reset();
        for (int32 index = 0; index < 6; ++index)
        {
            const double x = 150.0 + 300.0 * static_cast<double>(index);
            labels.Add(LaneLabelInput(FVector2D(x, 300.0), FVector2D(x, 800.0)));
        }
        const int32 placed = LaneLabelPlace(labels, starts, ends, out);
        TestEqual(TEXT("six labels: the cap placed"), placed, static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_COUNT));
        for (int32 index = 0; index < labels.Num(); ++index)
        {
            TestEqual(FString::Printf(TEXT("six labels: label %d placed iff within the cap"), index),
                out[index].bPlaced, index < SOL::NPC_TUNNEL_LABEL_MAX_COUNT);
        }

        // An output shorter than the labels: only those entries
        TArray<FSOLLaneLabelPlacement> two;
        two.SetNum(2);
        TestEqual(TEXT("output of 2: two placed"), SOLLaneTunnelLabel::PlaceLaneLabels(labels, starts, ends,
            TConstArrayView<FBox2D>(), LANELABEL_VIEWPORT, LANELABEL_ROW_MARGIN_PX, LANELABEL_GAP_PX, two), 2);
    }

    // A box that would leave the top of the screen above its anchor moves to 6 o'clock
    labels = { LaneLabelInput(FVector2D(960.0, 20.0), bottom) };
    LaneLabelPlace(labels, starts, ends, out);
    TestEqual(TEXT("12 o'clock box above the screen: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);

    // A non-finite or negative label size places nothing
    for (const FVector2D& badSize : { FVector2D(NAN, 54.0), FVector2D(180.0, INFINITY), FVector2D(-10.0, 54.0) })
    {
        labels = { LaneLabelInput(top, bottom, badSize) };
        TestEqual(FString::Printf(TEXT("size (%f, %f): none placed"), badSize.X, badSize.Y),
            LaneLabelPlace(labels, starts, ends, out), 0);
        TestFalse(FString::Printf(TEXT("size (%f, %f): hidden"), badSize.X, badSize.Y), out[0].bPlaced);
    }

    // A non-finite or non-positive viewport places nothing
    labels = { LaneLabelInput(top, bottom) };
    for (const FVector2D& badViewport : { FVector2D(0.0, 0.0), FVector2D(-1920.0, 1080.0), FVector2D(NAN, 1080.0) })
    {
        TArray<FSOLLaneLabelPlacement> single;
        single.SetNum(1);
        TestEqual(FString::Printf(TEXT("viewport (%f, %f): none placed"), badViewport.X, badViewport.Y),
            SOLLaneTunnelLabel::PlaceLaneLabels(labels, starts, ends, TConstArrayView<FBox2D>(), badViewport,
                LANELABEL_ROW_MARGIN_PX, LANELABEL_GAP_PX, single), 0);
    }

    // No labels
    labels.Reset();
    TestEqual(TEXT("no labels: none placed"), LaneLabelPlace(labels, starts, ends, out), 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelPlacePropertiesTest, "SOLTest.LaneTunnelLabel.PlaceProperties",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 19, properties over 500 random views of 1-4 synthetic tubes (8 projected dot rows each, one label per tube):
// every placed box is on screen, at least the row margin from every row (by an independent Euclidean distance, not the
// production test), not overlapping another placed box grown by the gap, sized, centred on its chosen (usable) anchor
// horizontally and outward from it vertically, in the full form (no compact size given); at most the cap are placed;
// removing later labels never changes an earlier one's placement; a label at 6 o'clock could not have taken 12 o'clock
bool FSOLLaneTunnelLabelPlacePropertiesTest::RunTest(const FString& /*parameters*/)
{
    FRandomStream stream(4949);
    TArray<FSOLLaneLabelInput> labels;
    TArray<FVector2D> starts;
    TArray<FVector2D> ends;
    TArray<FSOLLaneLabelPlacement> out;
    TArray<FSOLLaneLabelPlacement> prefixOut;
    TArray<FSOLLaneLabelInput> prefix;
    int32 failures = 0;
    FString firstFailure;
    int32 totalPlaced = 0;
    int32 totalBottom = 0;

    // Counts a failure and keeps the first description
    const auto fail = [&failures, &firstFailure](const FString& description)
    {
        if (failures == 0)
        {
            firstFailure = description;
        }
        ++failures;
    };

    for (int32 view = 0; view < 500; ++view)
    {
        // The tubes' projected dot rows: 8 short rays from a vanishing point each
        const int32 tubes = stream.RandRange(1, 4);
        starts.Reset();
        ends.Reset();
        labels.Reset();
        for (int32 tube = 0; tube < tubes; ++tube)
        {
            const FVector2D vanishing(stream.FRandRange(0.0f, 1920.0f), stream.FRandRange(0.0f, 1080.0f));
            const double baseAngle = stream.FRandRange(0.0f, 2.0f * UE_PI);
            for (int32 row = 0; row < SOL::NPC_TUNNEL_DOT_ROWS; ++row)
            {
                const double angle = baseAngle + static_cast<double>(row) * 2.0 * UE_DOUBLE_PI
                    / static_cast<double>(SOL::NPC_TUNNEL_DOT_ROWS);
                const FVector2D direction(FMath::Cos(angle), FMath::Sin(angle));
                const FVector2D start = vanishing + direction * stream.FRandRange(20.0f, 80.0f);
                starts.Add(start);
                ends.Add(start + direction * stream.FRandRange(60.0f, 400.0f));
            }

            // One label per tube, anchors sometimes off screen, flags sometimes unusable
            FSOLLaneLabelInput input = LaneLabelInput(
                FVector2D(stream.FRandRange(-200.0f, 2120.0f), stream.FRandRange(-100.0f, 1180.0f)),
                FVector2D(stream.FRandRange(-200.0f, 2120.0f), stream.FRandRange(-100.0f, 1180.0f)),
                FVector2D(stream.FRandRange(120.0f, 260.0f), stream.FRandRange(36.0f, 70.0f)));
            input.bTopInFront = stream.FRand() < 0.9f;
            input.bTopOccluded = stream.FRand() < 0.1f;
            input.bBottomInFront = stream.FRand() < 0.9f;
            input.bBottomOccluded = stream.FRand() < 0.1f;
            labels.Add(input);
        }

        const int32 placed = LaneLabelPlace(labels, starts, ends, out);
        const FString viewLabel = FString::Printf(TEXT("view %d"), view);
        int32 placedSeen = 0;
        for (int32 index = 0; index < labels.Num(); ++index)
        {
            const FSOLLaneLabelPlacement& placement = out[index];
            if (!placement.bPlaced)
            {
                continue;
            }
            ++placedSeen;
            totalBottom += placement.Candidate == LANELABEL_BOTTOM ? 1 : 0;
            const FString label = viewLabel + FString::Printf(TEXT(" label %d"), index);
            const FSOLLaneLabelInput& input = labels[index];
            const bool bTop = placement.Candidate == LANELABEL_TOP;
            if (!bTop && placement.Candidate != LANELABEL_BOTTOM)
            {
                fail(label + TEXT(": placed with no candidate"));
                continue;
            }
            const FVector2D anchor = bTop ? input.TopAnchorPx : input.BottomAnchorPx;
            const bool bUsable = bTop ? (input.bTopInFront && !input.bTopOccluded)
                : (input.bBottomInFront && !input.bBottomOccluded);
            const FBox2D& box = placement.BoxPx;
            if (!bUsable)
            {
                fail(label + TEXT(": placed at an anchor behind the camera or a planet"));
            }
            if (placement.bCompact)
            {
                fail(label + TEXT(": compact with no compact size"));
            }
            if (!FMath::IsNearlyEqual(box.Max.X - box.Min.X, input.SizePx.X, 1.0e-6)
                || !FMath::IsNearlyEqual(box.Max.Y - box.Min.Y, input.SizePx.Y, 1.0e-6))
            {
                fail(label + TEXT(": box is not the label's size"));
            }
            if (!FMath::IsNearlyEqual(0.5 * (box.Min.X + box.Max.X), anchor.X, 1.0e-6))
            {
                fail(label + TEXT(": box not centred on its anchor horizontally"));
            }
            if (!FMath::IsNearlyEqual(bTop ? box.Max.Y : box.Min.Y, anchor.Y, 1.0e-6))
            {
                fail(label + TEXT(": box not outward from its anchor"));
            }
            if (box.Min.X < -1.0e-6 || box.Min.Y < -1.0e-6 || box.Max.X > LANELABEL_VIEWPORT.X + 1.0e-6
                || box.Max.Y > LANELABEL_VIEWPORT.Y + 1.0e-6)
            {
                fail(label + TEXT(": box off screen"));
            }
            for (int32 row = 0; row < starts.Num(); ++row)
            {
                if (LaneLabelSegmentBoxDistance(starts[row], ends[row], box) < LANELABEL_ROW_MARGIN_PX - 1.0e-9)
                {
                    fail(label + FString::Printf(TEXT(": box within the margin of row %d"), row));
                    break;
                }
            }
            for (int32 other = 0; other < index; ++other)
            {
                if (out[other].bPlaced && LaneLabelBoxesOverlap(out[other].BoxPx, box, LANELABEL_GAP_PX))
                {
                    fail(label + FString::Printf(TEXT(": overlaps label %d"), other));
                }
            }
        }
        if (placed != placedSeen || placed > SOL::NPC_TUNNEL_LABEL_MAX_COUNT)
        {
            fail(viewLabel + FString::Printf(TEXT(": placed count %d, %d entries placed"), placed, placedSeen));
        }
        totalPlaced += placedSeen;

        // Removing later labels never changes an earlier one's placement
        for (int32 count = 1; count < labels.Num(); ++count)
        {
            prefix = TArray<FSOLLaneLabelInput>(labels.GetData(), count);
            LaneLabelPlace(prefix, starts, ends, prefixOut);
            for (int32 index = 0; index < count; ++index)
            {
                if (!LaneLabelSamePlacement(prefixOut[index], out[index]))
                {
                    fail(viewLabel + FString::Printf(TEXT(": label %d changes when only %d labels are placed"), index,
                        count));
                }
            }
        }

        // A label at 6 o'clock: with its 6 o'clock unusable it is hidden (its 12 o'clock was not available)
        for (int32 index = 0; index < labels.Num(); ++index)
        {
            if (!out[index].bPlaced || out[index].Candidate != LANELABEL_BOTTOM)
            {
                continue;
            }
            prefix = TArray<FSOLLaneLabelInput>(labels.GetData(), index + 1);
            prefix[index].bBottomInFront = false;
            LaneLabelPlace(prefix, starts, ends, prefixOut);
            if (prefixOut[index].bPlaced)
            {
                fail(viewLabel + FString::Printf(TEXT(": label %d took 6 o'clock with 12 o'clock available"), index));
            }
        }
    }
    TestTrue(TEXT("some labels were placed"), totalPlaced > 0);
    AddInfo(FString::Printf(TEXT("%d labels placed over 500 views, %d at 6 o'clock"), totalPlaced, totalBottom));
    if (!TestEqual(TEXT("placement properties hold"), failures, 0))
    {
        AddInfo(TEXT("first: ") + firstFailure);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelTextTest, "SOLTest.LaneTunnelLabel.TextLines",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Case 20: line 1 is "A - B", line 2 has each body's name and its end distance in the HUD's distance format, line 3
// the ship count with thousands separators and "ships" ("1 ship" for one); no line mentions a side
bool FSOLLaneTunnelLabelTextTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::FormatLaneLabelLines;
    const double distanceAM = 0.42 * 1.495978707e11;
    const double distanceBM = 1.08 * 1.495978707e11;
    FString line1;
    FString line2;
    FString line3;
    FormatLaneLabelLines(TEXT("Earth"), TEXT("Mars"), distanceAM, distanceBM, 62400, line1, line2, line3);
    TestEqual(TEXT("line 1: A - B"), line1, FString(TEXT("Earth - Mars")));

    // Line 2: "Earth <distance A>   Mars <distance B>" in that order
    const FString distanceA = SOLHudFormat::FormatDistanceM(distanceAM);
    const FString distanceB = SOLHudFormat::FormatDistanceM(distanceBM);
    const int32 earthAt = line2.Find(TEXT("Earth"));
    const int32 distanceAAt = line2.Find(distanceA, ESearchCase::CaseSensitive, ESearchDir::FromStart, earthAt);
    const int32 marsAt = line2.Find(TEXT("Mars"), ESearchCase::CaseSensitive, ESearchDir::FromStart, distanceAAt);
    const int32 distanceBAt = line2.Find(distanceB, ESearchCase::CaseSensitive, ESearchDir::FromStart, marsAt);
    TestTrue(TEXT("line 2: names Earth"), earthAt != INDEX_NONE);
    TestTrue(TEXT("line 2: Earth's distance after it"), distanceAAt != INDEX_NONE && distanceAAt > earthAt);
    TestTrue(TEXT("line 2: names Mars after"), marsAt != INDEX_NONE && marsAt > distanceAAt);
    TestTrue(TEXT("line 2: Mars's distance after it"), distanceBAt != INDEX_NONE && distanceBAt > marsAt);
    TestEqual(TEXT("line 3: thousands separator"), line3, FString(TEXT("62,400 ships")));

    // Ship counts
    struct FCountCase
    {
        int32 Count;
        const TCHAR* Text;
    };
    const FCountCase counts[] = {
        { 0, TEXT("0 ships") },
        { 1, TEXT("1 ship") },
        { 2, TEXT("2 ships") },
        { 999, TEXT("999 ships") },
        { 1000, TEXT("1,000 ships") },
        { 1000000, TEXT("1,000,000 ships") },
    };
    for (const FCountCase& countCase : counts)
    {
        FString first;
        FString second;
        FString third;
        FormatLaneLabelLines(TEXT("Uranus"), TEXT("Neptune"), 1.0e12, 2.0e12, countCase.Count, first, second, third);
        TestEqual(FString::Printf(TEXT("%d ships"), countCase.Count), third, FString(countCase.Text));
        TestEqual(FString::Printf(TEXT("%d ships: line 1"), countCase.Count), first, FString(TEXT("Uranus - Neptune")));
    }

    // No side line
    for (const FString* line : { &line1, &line2, &line3 })
    {
        const FString lower = line->ToLower();
        TestFalse(FString::Printf(TEXT("no side word in \"%s\""), **line), lower.Contains(TEXT("left"))
            || lower.Contains(TEXT("right")) || lower.Contains(TEXT("your")));
    }
    return true;
}

// Part 49b (plan step 7b, test first): the quantised key a label's text is rebuilt on (SDD 49 section 4.9, "rebuilds a
// label's text only when one of its displayed (rounded) values changes", made exact) and the label alpha's ease toward
// its target over NPC_TUNNEL_LABEL_FADE_S
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the next value of a small deterministic sequence in [0, 1) (an LCG, so the sweep is repeatable)
    double LaneLabelKeyJitter(uint32& state)
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<double>(state >> 8) / static_cast<double>(1u << 24);
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelDistanceKeyTest, "SOLTest.LaneTunnelLabel.DistanceKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Step 7b: LaneLabelDistanceKey(d) is the displayed distance as a number: two finite, non-negative distances have the
// same key exactly when SOLHudFormat::FormatDistanceM prints them the same (metres, kilometres and AU, across the unit
// boundaries and the rounding steps), so the HUD reformats a label only when its text would change; NaN and infinity
// keep a stable key (a bad value does not reformat every frame)
bool FSOLLaneTunnelLabelDistanceKeyTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::LaneLabelDistanceKey;
    TMap<FString, int64> keyOfText;
    TMap<int64, FString> textOfKey;
    int32 failures = 0;
    FString firstFailure;

    // Checks one distance against everything seen so far: one text has one key and one key has one text
    const auto checkDistance = [&](const double distanceM)
    {
        const FString text = SOLHudFormat::FormatDistanceM(distanceM);
        const int64 key = LaneLabelDistanceKey(distanceM);
        const int64* seenKey = keyOfText.Find(text);
        const FString* seenText = textOfKey.Find(key);
        if ((seenKey != nullptr && *seenKey != key) || (seenText != nullptr && *seenText != text))
        {
            if (failures == 0)
            {
                firstFailure = FString::Printf(TEXT("%.17g m reads \"%s\" with key %lld, which another distance's "
                    "text or key already has"), distanceM, *text, static_cast<long long>(key));
            }
            ++failures;
            return;
        }
        keyOfText.Add(text, key);
        textOfKey.Add(key, text);
    };

    // A geometric sweep from 1 cm to 100 AU, each step jittered so no sample sits on an exact rounding tie
    uint32 state = 49u;
    for (double distanceM = 0.01; distanceM < 1.5e13; distanceM *= 1.0007)
    {
        checkDistance(distanceM * (1.0 + 1.0e-7 * LaneLabelKeyJitter(state)));
    }

    // Dense samples round the unit boundaries (1000 m, 0.01 AU) and inside a few rounding steps
    const double centres[] = { 0.0, 0.05, 12.34, 999.9, 1000.0, 1000.05, 23402.9e3, 0.01 * 1.495978707e11,
        0.42 * 1.495978707e11, 30.0 * 1.495978707e11 };
    for (const double centre : centres)
    {
        const double step = FMath::Max(1.0e-3, 2.0e-6 * centre);
        for (int32 index = -400; index <= 400; ++index)
        {
            const double distanceM = centre + step * (static_cast<double>(index) + 0.37 * LaneLabelKeyJitter(state));
            if (distanceM >= 0.0)
            {
                checkDistance(distanceM);
            }
        }
    }
    if (!TestEqual(TEXT("one key per displayed distance and one displayed distance per key"), failures, 0))
    {
        AddInfo(TEXT("first: ") + firstFailure);
    }
    TestTrue(TEXT("the sweep saw many displayed distances"), keyOfText.Num() > 1000);

    // Neighbours inside one rounding step share a key; across a step they do not
    TestEqual(TEXT("1.23401 AU and 1.23402 AU share a key"), LaneLabelDistanceKey(1.23401 * 1.495978707e11),
        LaneLabelDistanceKey(1.23402 * 1.495978707e11));
    TestNotEqual(TEXT("1.2340 AU and 1.2342 AU differ"), LaneLabelDistanceKey(1.2340 * 1.495978707e11),
        LaneLabelDistanceKey(1.2342 * 1.495978707e11));
    TestNotEqual(TEXT("12.3 m and 12.3 km differ (the unit is part of the key)"), LaneLabelDistanceKey(12.3),
        LaneLabelDistanceKey(12.3e3));

    // Bad values keep a stable key
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    TestEqual(TEXT("NaN: stable key"), LaneLabelDistanceKey(nan), LaneLabelDistanceKey(nan));
    TestEqual(TEXT("infinity: stable key"), LaneLabelDistanceKey(inf), LaneLabelDistanceKey(inf));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelTextKeyTest, "SOLTest.LaneTunnelLabel.TextKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Step 7b: MakeLaneLabelKey(lane, distance A, distance B, ships) changes exactly when the label's text would: a new
// lane, a displayed distance stepping, or the ship count; not when a distance moves inside its rounding step. A
// negative ship count keys as 0 (it reads "0 ships")
bool FSOLLaneTunnelLabelTextKeyTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::MakeLaneLabelKey;
    constexpr double au = 1.495978707e11;
    const FSOLLaneLabelKey base = MakeLaneLabelKey(3, 0.42 * au, 1.08 * au, 62400);
    TestTrue(TEXT("the same values: equal"), base == MakeLaneLabelKey(3, 0.42 * au, 1.08 * au, 62400));
    TestTrue(TEXT("a distance moving inside its rounding step: equal"),
        base == MakeLaneLabelKey(3, 0.42 * au + 10.0, 1.08 * au - 10.0, 62400));
    TestFalse(TEXT("another lane: differs"), base == MakeLaneLabelKey(4, 0.42 * au, 1.08 * au, 62400));
    TestFalse(TEXT("distance A stepping: differs"), base == MakeLaneLabelKey(3, 0.4201 * au, 1.08 * au, 62400));
    TestFalse(TEXT("distance B stepping: differs"), base == MakeLaneLabelKey(3, 0.42 * au, 1.0801 * au, 62400));
    TestFalse(TEXT("the ship count: differs"), base == MakeLaneLabelKey(3, 0.42 * au, 1.08 * au, 62401));
    TestFalse(TEXT("A and B swapped: differs"), base == MakeLaneLabelKey(3, 1.08 * au, 0.42 * au, 62400));
    TestTrue(TEXT("a negative ship count keys as 0"), MakeLaneLabelKey(1, 5.0e6, 6.0e6, -5)
        == MakeLaneLabelKey(1, 5.0e6, 6.0e6, 0));

    // Against the text itself: whenever the lines differ the keys differ, and whenever they match the keys match
    uint32 state = 7u;
    int32 failures = 0;
    FString first[3];
    FString second[3];
    for (int32 index = 0; index < 2000; ++index)
    {
        const double distanceAM = 1.0e6 * FMath::Pow(1.0e7, LaneLabelKeyJitter(state));
        const double distanceBM = 1.0e6 * FMath::Pow(1.0e7, LaneLabelKeyJitter(state));
        const int32 ships = static_cast<int32>(LaneLabelKeyJitter(state) * 3.0);
        const double nudge = 1.0 + 1.0e-5 * (LaneLabelKeyJitter(state) - 0.5);
        SOLLaneTunnelLabel::FormatLaneLabelLines(TEXT("Earth"), TEXT("Mars"), distanceAM, distanceBM, ships, first[0],
            first[1], first[2]);
        SOLLaneTunnelLabel::FormatLaneLabelLines(TEXT("Earth"), TEXT("Mars"), distanceAM * nudge, distanceBM, ships,
            second[0], second[1], second[2]);
        const bool bSameText = first[1] == second[1] && first[2] == second[2];
        const bool bSameKey = MakeLaneLabelKey(0, distanceAM, distanceBM, ships)
            == MakeLaneLabelKey(0, distanceAM * nudge, distanceBM, ships);
        failures += bSameText != bSameKey ? 1 : 0;
    }
    TestEqual(TEXT("the key follows the text over 2,000 nudged labels"), failures, 0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelAlphaEaseTest, "SOLTest.LaneTunnelLabel.AlphaEase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// SDD 49 section 4.9 (Q4): a label's displayed alpha moves linearly toward its target at 1 / NPC_TUNNEL_LABEL_FADE_S
// per second (a full swing takes NPC_TUNNEL_LABEL_FADE_S, 0.2 s), never overshoots, and stays in [0, 1]; a NaN or
// non-positive time step keeps the alpha, a NaN target reads as 0 and a NaN current alpha as 0
bool FSOLLaneTunnelLabelAlphaEaseTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::EaseLaneLabelAlpha;
    TestEqual(TEXT("fade time 0.2 s (proposed value)"), SOL::NPC_TUNNEL_LABEL_FADE_S, 0.2);
    const double fadeS = SOL::NPC_TUNNEL_LABEL_FADE_S;
    const double nan = std::numeric_limits<double>::quiet_NaN();

    // A full fade in and out, in quarter steps
    double alpha = 0.0;
    for (int32 step = 1; step <= 4; ++step)
    {
        alpha = EaseLaneLabelAlpha(alpha, 1.0, 0.25 * fadeS);
        TestEqual(FString::Printf(TEXT("fade in: step %d"), step), alpha, 0.25 * step, LANELABEL_TOL);
    }
    TestEqual(TEXT("fade in: stays at the target"), EaseLaneLabelAlpha(alpha, 1.0, fadeS), 1.0, LANELABEL_TOL);
    TestEqual(TEXT("fade out: half way after half the time"), EaseLaneLabelAlpha(1.0, 0.0, 0.5 * fadeS), 0.5,
        LANELABEL_TOL);
    TestEqual(TEXT("fade out: done after the fade time"), EaseLaneLabelAlpha(1.0, 0.0, fadeS), 0.0, LANELABEL_TOL);

    // No overshoot toward a partial target, from either side
    TestEqual(TEXT("up to 0.55: no overshoot"), EaseLaneLabelAlpha(0.0, 0.55, 10.0), 0.55, LANELABEL_TOL);
    TestEqual(TEXT("down to 0.3: no overshoot"), EaseLaneLabelAlpha(0.9, 0.3, 10.0), 0.3, LANELABEL_TOL);
    TestEqual(TEXT("a small step toward 0.55"), EaseLaneLabelAlpha(0.5, 0.55, 0.01 * fadeS), 0.51, LANELABEL_TOL);

    // Bad inputs
    TestEqual(TEXT("zero step keeps the alpha"), EaseLaneLabelAlpha(0.4, 1.0, 0.0), 0.4, LANELABEL_TOL);
    TestEqual(TEXT("negative step keeps the alpha"), EaseLaneLabelAlpha(0.4, 1.0, -0.1), 0.4, LANELABEL_TOL);
    TestEqual(TEXT("NaN step keeps the alpha"), EaseLaneLabelAlpha(0.4, 1.0, nan), 0.4, LANELABEL_TOL);
    TestEqual(TEXT("NaN target reads as 0"), EaseLaneLabelAlpha(0.4, nan, fadeS), 0.0, LANELABEL_TOL);
    TestEqual(TEXT("NaN current reads as 0"), EaseLaneLabelAlpha(nan, 1.0, 0.5 * fadeS), 0.5, LANELABEL_TOL);
    TestEqual(TEXT("a target above 1 is clamped"), EaseLaneLabelAlpha(0.9, 3.0, fadeS), 1.0, LANELABEL_TOL);
    TestEqual(TEXT("a target below 0 is clamped"), EaseLaneLabelAlpha(0.1, -2.0, fadeS), 0.0, LANELABEL_TOL);

    // Always in [0, 1] over a random walk of targets and steps
    uint32 state = 11u;
    bool bInRange = true;
    alpha = 0.0;
    for (int32 index = 0; index < 1000; ++index)
    {
        alpha = EaseLaneLabelAlpha(alpha, LaneLabelKeyJitter(state) * 1.4 - 0.2, LaneLabelKeyJitter(state) * 0.1);
        bInRange &= alpha >= 0.0 && alpha <= 1.0;
    }
    TestTrue(TEXT("always in [0, 1]"), bInRange);
    return true;
}

// Contract change (2026-11-12, test first): the placement keeps clear of HUD panel avoid boxes and falls back to a
// compact one-line form, whose text is "A - B" and the nearest end distance and whose key follows that text
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the full-size 12 o'clock box of a label anchored at a point (above it, centred horizontally)
    FBox2D LaneLabelTopBox(const FVector2D& anchorPx, const FVector2D& sizePx)
    {
        return FBox2D(FVector2D(anchorPx.X - 0.5 * sizePx.X, anchorPx.Y - sizePx.Y),
            FVector2D(anchorPx.X + 0.5 * sizePx.X, anchorPx.Y));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the compact line the contract expects: "A - B", the compact separator and the nearest distance's text
    FString LaneLabelExpectedCompactLine(const TCHAR* nameA, const TCHAR* nameB, const double nearestM)
    {
        FString text(nameA);
        text += SOLMenuText::LANE_LABEL_NAME_SEPARATOR;
        text += nameB;
        text += SOLMenuText::LANE_LABEL_COMPACT_SEPARATOR;
        text += SOLHudFormat::FormatDistanceM(nearestM);
        return text;
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelPlaceAvoidTest, "SOLTest.LaneTunnelLabel.PlaceAvoidBoxes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// A candidate box must not overlap any valid avoid box (a HUD panel) grown by the gap: a panel over the 12 o'clock box
// sends the label to 6 o'clock, panels over both hide it; touching the grown edge exactly is not an overlap; an invalid
// avoid box is ignored; the avoid boxes apply to every label
bool FSOLLaneTunnelLabelPlaceAvoidTest::RunTest(const FString& /*parameters*/)
{
    TArray<FSOLLaneLabelInput> labels;
    const TArray<FVector2D> noRows;
    TArray<FBox2D> avoid;
    TArray<FSOLLaneLabelPlacement> out;
    const FVector2D top(960.0, 400.0);
    const FVector2D bottom(600.0, 700.0);

    // The full 12 o'clock box is X [870, 1050] Y [346, 400]; the 6 o'clock box X [510, 690] Y [700, 754]
    const FBox2D topBox = LaneLabelTopBox(top, LANELABEL_SIZE);
    labels = { LaneLabelInput(top, bottom) };
    TestEqual(TEXT("no avoid boxes: one placed"), LaneLabelPlace(labels, noRows, noRows, out, avoid), 1);
    TestEqual(TEXT("no avoid boxes: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);

    // A panel over the 12 o'clock box: 6 o'clock, below its anchor
    avoid = { FBox2D(FVector2D(900.0, 300.0), FVector2D(1000.0, 380.0)) };
    TestEqual(TEXT("panel over 12 o'clock: one placed"), LaneLabelPlace(labels, noRows, noRows, out, avoid), 1);
    TestEqual(TEXT("panel over 12 o'clock: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    TestFalse(TEXT("panel over 12 o'clock: full form"), out[0].bCompact);
    TestEqual(TEXT("panel over 12 o'clock: centred on the 6 o'clock anchor"),
        0.5 * (out[0].BoxPx.Min.X + out[0].BoxPx.Max.X), bottom.X, LANELABEL_TOL);
    TestEqual(TEXT("panel over 12 o'clock: below the 6 o'clock anchor"), out[0].BoxPx.Min.Y, bottom.Y, LANELABEL_TOL);

    // Panels over both candidates: hidden
    avoid.Add(FBox2D(FVector2D(550.0, 720.0), FVector2D(650.0, 800.0)));
    TestEqual(TEXT("panels over both: none placed"), LaneLabelPlace(labels, noRows, noRows, out, avoid), 0);
    TestFalse(TEXT("panels over both: hidden"), out[0].bPlaced);
    TestEqual(TEXT("panels over both: no candidate"), out[0].Candidate, static_cast<int32>(INDEX_NONE));

    // A small panel wholly inside the 12 o'clock box blocks it too, and one covering the screen blocks everything
    avoid = { FBox2D(FVector2D(950.0, 360.0), FVector2D(970.0, 380.0)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel inside the 12 o'clock box: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    avoid = { FBox2D(FVector2D(0.0, 0.0), LANELABEL_VIEWPORT) };
    TestEqual(TEXT("panel over the whole screen: none placed"), LaneLabelPlace(labels, noRows, noRows, out, avoid), 0);

    // The gap: a panel 3 px right of the 12 o'clock box overlaps once grown by the gap, one 20 px right does not
    avoid = { FBox2D(FVector2D(topBox.Max.X + 3.0, 350.0), FVector2D(topBox.Max.X + 100.0, 390.0)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel 3 px right: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    avoid = { FBox2D(FVector2D(topBox.Max.X + 20.0, 350.0), FVector2D(topBox.Max.X + 100.0, 390.0)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel 20 px right: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);

    // Touching: a panel exactly the gap right of, left of or above the box does not overlap; 1 px closer does
    avoid = { FBox2D(FVector2D(topBox.Max.X + LANELABEL_GAP_PX, 350.0), FVector2D(topBox.Max.X + 100.0, 390.0)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel exactly the gap right: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    avoid = { FBox2D(FVector2D(topBox.Max.X + LANELABEL_GAP_PX - 1.0, 350.0),
        FVector2D(topBox.Max.X + 100.0, 390.0)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel 1 px inside the gap right: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    avoid = { FBox2D(FVector2D(topBox.Min.X - 100.0, 350.0), FVector2D(topBox.Min.X - LANELABEL_GAP_PX, 390.0)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel exactly the gap left: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    avoid = { FBox2D(FVector2D(900.0, 200.0), FVector2D(1000.0, topBox.Min.Y - LANELABEL_GAP_PX)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel exactly the gap above: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    avoid = { FBox2D(FVector2D(900.0, 200.0), FVector2D(1000.0, topBox.Min.Y - LANELABEL_GAP_PX + 1.0)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel 1 px inside the gap above: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);

    // An invalid avoid box is ignored, even one covering the whole screen; a valid one beside it still counts
    avoid = { LaneLabelInvalidBox(FVector2D(0.0, 0.0), LANELABEL_VIEWPORT) };
    TestEqual(TEXT("invalid panel: one placed"), LaneLabelPlace(labels, noRows, noRows, out, avoid), 1);
    TestEqual(TEXT("invalid panel: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    avoid.Add(FBox2D(FVector2D(900.0, 300.0), FVector2D(1000.0, 380.0)));
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("invalid panel and a valid one over 12 o'clock: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);

    // The avoid boxes apply to every label, not only the first
    labels = { LaneLabelInput(FVector2D(400.0, 300.0), FVector2D(400.0, 800.0)),
        LaneLabelInput(FVector2D(1400.0, 300.0), FVector2D(1400.0, 800.0)) };
    avoid = { FBox2D(FVector2D(1350.0, 200.0), FVector2D(1450.0, 280.0)) };
    TestEqual(TEXT("panel over the second's 12 o'clock: two placed"),
        LaneLabelPlace(labels, noRows, noRows, out, avoid), 2);
    TestEqual(TEXT("panel over the second's 12 o'clock: first at 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    TestEqual(TEXT("panel over the second's 12 o'clock: second at 6 o'clock"), out[1].Candidate, LANELABEL_BOTTOM);
    avoid = { FBox2D(FVector2D(1390.0, 0.0), FVector2D(1410.0, LANELABEL_VIEWPORT.Y)) };
    TestEqual(TEXT("panel over both of the second's: one placed"), LaneLabelPlace(labels, noRows, noRows, out, avoid),
        1);
    TestEqual(TEXT("panel over both of the second's: first at 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    TestFalse(TEXT("panel over both of the second's: second hidden"), out[1].bPlaced);
    avoid = { FBox2D(FVector2D(350.0, 200.0), FVector2D(450.0, 280.0)) };
    LaneLabelPlace(labels, noRows, noRows, out, avoid);
    TestEqual(TEXT("panel over the first's 12 o'clock: first at 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    TestEqual(TEXT("panel over the first's 12 o'clock: second at 12 o'clock"), out[1].Candidate, LANELABEL_TOP);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelPlaceCompactTest, "SOLTest.LaneTunnelLabel.PlaceCompactFallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Each label takes the first usable of full@12, full@6, compact@12, compact@6 (compact only with a finite compact size
// whose sides are both > 0), else it is hidden; bCompact reports the form and the box has that form's size, centred on
// the anchor and outward from it; compact boxes respect the rows, the avoid boxes, the viewport, the camera and planets
// and earlier labels (and block later ones); the cap and the bad-viewport and bad-full-size rules still hold
bool FSOLLaneTunnelLabelPlaceCompactTest::RunTest(const FString& /*parameters*/)
{
    TArray<FSOLLaneLabelInput> labels;
    TArray<FVector2D> starts;
    TArray<FVector2D> ends;
    TArray<FBox2D> avoid;
    TArray<FSOLLaneLabelPlacement> out;
    const FVector2D top(960.0, 400.0);
    const FVector2D bottom(600.0, 700.0);
    const FVector2D compact(120.0, 18.0);
    const FVector2D wide(3000.0, 54.0);                      // A valid full size that never fits on the viewport

    // Full boxes: 12 o'clock X [870, 1050] Y [346, 400], 6 o'clock X [510, 690] Y [700, 754]. Compact boxes:
    // 12 o'clock X [900, 1020] Y [382, 400], 6 o'clock X [540, 660] Y [700, 718]. Rows at x 880 and 520 cross the
    // full boxes and pass more than the margin outside the compact ones
    labels = { LaneLabelCompactInput(top, bottom, LANELABEL_SIZE, compact) };

    // The full form fits: full at 12 o'clock
    TestEqual(TEXT("full fits: one placed"), LaneLabelPlace(labels, starts, ends, out), 1);
    TestEqual(TEXT("full fits: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    TestFalse(TEXT("full fits: full form"), out[0].bCompact);
    TestEqual(TEXT("full fits: full height"), out[0].BoxPx.Max.Y - out[0].BoxPx.Min.Y, LANELABEL_SIZE.Y,
        LANELABEL_TOL);

    // The full 12 o'clock box blocked: the full 6 o'clock is preferred to the compact 12 o'clock
    LaneLabelAddVerticalRow(880.0, starts, ends);
    LaneLabelPlace(labels, starts, ends, out);
    TestEqual(TEXT("full 12 blocked: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    TestFalse(TEXT("full 12 blocked: full form"), out[0].bCompact);
    TestEqual(TEXT("full 12 blocked: full width"), out[0].BoxPx.Max.X - out[0].BoxPx.Min.X, LANELABEL_SIZE.X,
        LANELABEL_TOL);

    // Both full boxes blocked by rows: compact at 12 o'clock, its size, centred on and above the anchor
    LaneLabelAddVerticalRow(520.0, starts, ends);
    TestEqual(TEXT("full blocked by rows: one placed"), LaneLabelPlace(labels, starts, ends, out), 1);
    TestTrue(TEXT("full blocked by rows: placed"), out[0].bPlaced);
    TestTrue(TEXT("full blocked by rows: compact"), out[0].bCompact);
    TestEqual(TEXT("full blocked by rows: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    TestEqual(TEXT("compact 12: width"), out[0].BoxPx.Max.X - out[0].BoxPx.Min.X, compact.X, LANELABEL_TOL);
    TestEqual(TEXT("compact 12: height"), out[0].BoxPx.Max.Y - out[0].BoxPx.Min.Y, compact.Y, LANELABEL_TOL);
    TestEqual(TEXT("compact 12: centred on the anchor"), 0.5 * (out[0].BoxPx.Min.X + out[0].BoxPx.Max.X), top.X,
        LANELABEL_TOL);
    TestEqual(TEXT("compact 12: above the anchor"), out[0].BoxPx.Max.Y, top.Y, LANELABEL_TOL);

    // A row through the 12 o'clock anchor blocks the compact 12 o'clock too: compact at 6 o'clock, below the anchor
    LaneLabelAddVerticalRow(top.X, starts, ends);
    TestEqual(TEXT("compact 12 blocked: one placed"), LaneLabelPlace(labels, starts, ends, out), 1);
    TestTrue(TEXT("compact 12 blocked: compact"), out[0].bCompact);
    TestEqual(TEXT("compact 12 blocked: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    TestEqual(TEXT("compact 6: width"), out[0].BoxPx.Max.X - out[0].BoxPx.Min.X, compact.X, LANELABEL_TOL);
    TestEqual(TEXT("compact 6: height"), out[0].BoxPx.Max.Y - out[0].BoxPx.Min.Y, compact.Y, LANELABEL_TOL);
    TestEqual(TEXT("compact 6: centred on the anchor"), 0.5 * (out[0].BoxPx.Min.X + out[0].BoxPx.Max.X), bottom.X,
        LANELABEL_TOL);
    TestEqual(TEXT("compact 6: below the anchor"), out[0].BoxPx.Min.Y, bottom.Y, LANELABEL_TOL);

    // A row through the 6 o'clock anchor too: hidden
    LaneLabelAddVerticalRow(bottom.X, starts, ends);
    TestEqual(TEXT("everything blocked: none placed"), LaneLabelPlace(labels, starts, ends, out), 0);
    TestFalse(TEXT("everything blocked: hidden"), out[0].bPlaced);
    TestFalse(TEXT("everything blocked: not compact"), out[0].bCompact);
    TestEqual(TEXT("everything blocked: no candidate"), out[0].Candidate, static_cast<int32>(INDEX_NONE));
    starts.Reset();
    ends.Reset();

    // Both full boxes blocked by panels that miss the compact boxes: compact at 12 o'clock, then 6 o'clock once a
    // panel covers the compact 12 o'clock box too
    avoid = { FBox2D(FVector2D(870.0, 300.0), FVector2D(1050.0, 360.0)),
        FBox2D(FVector2D(510.0, 740.0), FVector2D(690.0, 800.0)) };
    TestEqual(TEXT("full blocked by panels: one placed"), LaneLabelPlace(labels, starts, ends, out, avoid), 1);
    TestTrue(TEXT("full blocked by panels: compact"), out[0].bCompact);
    TestEqual(TEXT("full blocked by panels: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
    avoid.Add(FBox2D(FVector2D(940.0, 385.0), FVector2D(980.0, 395.0)));
    TestEqual(TEXT("compact 12 behind a panel: one placed"), LaneLabelPlace(labels, starts, ends, out, avoid), 1);
    TestTrue(TEXT("compact 12 behind a panel: compact"), out[0].bCompact);
    TestEqual(TEXT("compact 12 behind a panel: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
    avoid.Add(FBox2D(FVector2D(580.0, 705.0), FVector2D(620.0, 712.0)));
    TestEqual(TEXT("every box behind a panel: none placed"), LaneLabelPlace(labels, starts, ends, out, avoid), 0);
    avoid.Reset();

    // The compact form is not tried with a zero, negative or non-finite compact size, but the full form still is
    const FVector2D badCompacts[] = { FVector2D(0.0, 0.0), FVector2D(120.0, 0.0), FVector2D(0.0, 18.0),
        FVector2D(-120.0, 18.0), FVector2D(120.0, -18.0), FVector2D(NAN, 18.0), FVector2D(120.0, INFINITY) };
    for (const FVector2D& badCompact : badCompacts)
    {
        const FString what = FString::Printf(TEXT("compact size (%f, %f)"), badCompact.X, badCompact.Y);
        labels = { LaneLabelCompactInput(top, bottom, LANELABEL_SIZE, badCompact) };
        TestEqual(what + TEXT(", full free: one placed"), LaneLabelPlace(labels, starts, ends, out), 1);
        TestFalse(what + TEXT(", full free: full form"), out[0].bCompact);
        LaneLabelAddVerticalRow(880.0, starts, ends);
        LaneLabelAddVerticalRow(520.0, starts, ends);
        TestEqual(what + TEXT(", full blocked: none placed"), LaneLabelPlace(labels, starts, ends, out), 0);
        TestFalse(what + TEXT(", full blocked: hidden"), out[0].bPlaced);
        starts.Reset();
        ends.Reset();
    }

    // A bad full size places nothing, even with a good compact size
    labels = { LaneLabelCompactInput(top, bottom, FVector2D(NAN, 54.0), compact) };
    TestEqual(TEXT("NaN full size with a compact size: none placed"), LaneLabelPlace(labels, starts, ends, out), 0);
    labels = { LaneLabelCompactInput(top, bottom, FVector2D(-10.0, 54.0), compact) };
    TestEqual(TEXT("negative full size with a compact size: none placed"), LaneLabelPlace(labels, starts, ends, out),
        0);

    // The compact form respects the viewport, the camera and planets (a full form wider than the screen forces it)
    {
        labels = { LaneLabelCompactInput(top, bottom, wide, compact) };
        LaneLabelPlace(labels, starts, ends, out);
        TestTrue(TEXT("wide full form: compact"), out[0].bCompact);
        TestEqual(TEXT("wide full form: 12 o'clock"), out[0].Candidate, LANELABEL_TOP);

        FSOLLaneLabelInput behind = LaneLabelCompactInput(top, bottom, wide, compact);
        behind.bTopInFront = false;
        labels = { behind };
        LaneLabelPlace(labels, starts, ends, out);
        TestTrue(TEXT("compact, top behind the camera: compact"), out[0].bCompact);
        TestEqual(TEXT("compact, top behind the camera: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);

        FSOLLaneLabelInput occluded = LaneLabelCompactInput(top, bottom, wide, compact);
        occluded.bTopOccluded = true;
        labels = { occluded };
        LaneLabelPlace(labels, starts, ends, out);
        TestEqual(TEXT("compact, top behind a planet: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
        occluded.bBottomOccluded = true;
        labels = { occluded };
        TestEqual(TEXT("compact, both behind a planet: none placed"), LaneLabelPlace(labels, starts, ends, out), 0);

        FSOLLaneLabelInput neither = LaneLabelCompactInput(top, bottom, wide, compact);
        neither.bTopInFront = false;
        neither.bBottomInFront = false;
        labels = { neither };
        TestEqual(TEXT("compact, both behind the camera: none placed"), LaneLabelPlace(labels, starts, ends, out), 0);

        labels = { LaneLabelCompactInput(FVector2D(960.0, 10.0), bottom, wide, compact) };
        LaneLabelPlace(labels, starts, ends, out);
        TestTrue(TEXT("compact 12 above the screen: compact"), out[0].bCompact);
        TestEqual(TEXT("compact 12 above the screen: 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);

        labels = { LaneLabelCompactInput(FVector2D(960.0, 10.0), FVector2D(600.0, 1075.0), wide, compact) };
        TestEqual(TEXT("compact off the top and the bottom: none placed"), LaneLabelPlace(labels, starts, ends, out),
            0);
    }

    // An earlier compact label blocks a later one, and an earlier full label blocks a later compact one
    {
        LaneLabelAddVerticalRow(880.0, starts, ends);
        LaneLabelAddVerticalRow(520.0, starts, ends);
        labels = { LaneLabelCompactInput(top, bottom, LANELABEL_SIZE, compact),
            LaneLabelCompactInput(top, bottom, LANELABEL_SIZE, compact) };
        TestEqual(TEXT("two compact at the same anchors: two placed"), LaneLabelPlace(labels, starts, ends, out), 2);
        TestTrue(TEXT("two compact: first compact"), out[0].bCompact);
        TestEqual(TEXT("two compact: first at 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
        TestTrue(TEXT("two compact: second compact"), out[1].bCompact);
        TestEqual(TEXT("two compact: second at 6 o'clock"), out[1].Candidate, LANELABEL_BOTTOM);
        TestFalse(TEXT("two compact: boxes apart by the gap"),
            LaneLabelBoxesOverlap(out[0].BoxPx, out[1].BoxPx, LANELABEL_GAP_PX));

        // The second's compact 6 o'clock unusable: hidden, the first unchanged
        const FSOLLaneLabelPlacement firstBefore = out[0];
        labels[1].bBottomInFront = false;
        TestEqual(TEXT("second's 6 o'clock unusable: one placed"), LaneLabelPlace(labels, starts, ends, out), 1);
        TestFalse(TEXT("second's 6 o'clock unusable: hidden"), out[1].bPlaced);
        TestTrue(TEXT("second's 6 o'clock unusable: first unchanged"), LaneLabelSamePlacement(out[0], firstBefore));

        // A later full label whose 12 o'clock box (X [1010, 1190]) meets the earlier compact box grown by the gap
        labels = { LaneLabelCompactInput(top, bottom, LANELABEL_SIZE, compact),
            LaneLabelInput(FVector2D(1100.0, 400.0), FVector2D(1400.0, 800.0)) };
        TestEqual(TEXT("full after compact: two placed"), LaneLabelPlace(labels, starts, ends, out), 2);
        TestTrue(TEXT("full after compact: first compact"), out[0].bCompact);
        TestFalse(TEXT("full after compact: second full"), out[1].bCompact);
        TestEqual(TEXT("full after compact: second at 6 o'clock"), out[1].Candidate, LANELABEL_BOTTOM);
        starts.Reset();
        ends.Reset();

        // A later compact label (its full form too wide) whose compact 12 o'clock box meets an earlier full box
        labels = { LaneLabelInput(top, bottom), LaneLabelCompactInput(top, bottom, wide, compact) };
        TestEqual(TEXT("compact after full: two placed"), LaneLabelPlace(labels, starts, ends, out), 2);
        TestFalse(TEXT("compact after full: first full"), out[0].bCompact);
        TestEqual(TEXT("compact after full: first at 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
        TestTrue(TEXT("compact after full: second compact"), out[1].bCompact);
        TestEqual(TEXT("compact after full: second at 6 o'clock"), out[1].Candidate, LANELABEL_BOTTOM);
    }

    // At most NPC_TUNNEL_LABEL_MAX_COUNT placed, compact ones included, in the given order
    {
        labels.Reset();
        for (int32 index = 0; index < 6; ++index)
        {
            const double x = 150.0 + 300.0 * static_cast<double>(index);
            labels.Add(LaneLabelCompactInput(FVector2D(x, 300.0), FVector2D(x, 800.0), wide, compact));
        }
        TestEqual(TEXT("six compact labels: the cap placed"), LaneLabelPlace(labels, starts, ends, out),
            static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_COUNT));
        for (int32 index = 0; index < labels.Num(); ++index)
        {
            const bool bWithinCap = index < SOL::NPC_TUNNEL_LABEL_MAX_COUNT;
            TestEqual(FString::Printf(TEXT("six compact labels: label %d placed iff within the cap"), index),
                out[index].bPlaced, bWithinCap);
            if (bWithinCap)
            {
                TestTrue(FString::Printf(TEXT("six compact labels: label %d compact"), index), out[index].bCompact);
                TestEqual(FString::Printf(TEXT("six compact labels: label %d at 12 o'clock"), index),
                    out[index].Candidate, LANELABEL_TOP);
            }
        }
    }

    // A non-finite or non-positive viewport places nothing, compact labels included
    labels = { LaneLabelCompactInput(top, bottom, wide, compact) };
    for (const FVector2D& badViewport : { FVector2D(0.0, 0.0), FVector2D(-1920.0, 1080.0), FVector2D(NAN, 1080.0) })
    {
        TArray<FSOLLaneLabelPlacement> single;
        single.SetNum(1);
        TestEqual(FString::Printf(TEXT("compact, viewport (%f, %f): none placed"), badViewport.X, badViewport.Y),
            SOLLaneTunnelLabel::PlaceLaneLabels(labels, starts, ends, TConstArrayView<FBox2D>(), badViewport,
                LANELABEL_ROW_MARGIN_PX, LANELABEL_GAP_PX, single), 0);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelCompactLineTest, "SOLTest.LaneTunnelLabel.CompactLine",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// The compact one-line fallback reads nameA + LANE_LABEL_NAME_SEPARATOR + nameB + LANE_LABEL_COMPACT_SEPARATOR + the
// nearest end distance in the HUD's distance format (a NaN end ignored unless both are NaN); the output is reset first
bool FSOLLaneTunnelLabelCompactLineTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::FormatLaneLabelCompactLine;
    constexpr double au = 1.495978707e11;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    // The worked example: Earth nearer
    FString line = TEXT("stale text from an earlier label");
    FormatLaneLabelCompactLine(TEXT("Earth"), TEXT("Mars"), 0.42 * au, 1.08 * au, line);
    TestEqual(TEXT("Earth nearer"), line, LaneLabelExpectedCompactLine(TEXT("Earth"), TEXT("Mars"), 0.42 * au));
    TestTrue(TEXT("starts with \"Earth - Mars\""), line.StartsWith(TEXT("Earth - Mars"), ESearchCase::CaseSensitive));
    TestFalse(TEXT("the farther distance is not shown"), line.Contains(SOLHudFormat::FormatDistanceM(1.08 * au)));
    TestFalse(TEXT("one line"), line.Contains(TEXT("\n")));
    TestFalse(TEXT("no ship count"), line.Contains(TEXT("ship")));

    // The nearest end, whichever it is, and the bad values
    struct FCompactCase
    {
        double DistanceAM;
        double DistanceBM;
        double NearestM;
        const TCHAR* What;
    };
    const FCompactCase cases[] = {
        { 5.0e9, 3.0e6, 3.0e6, TEXT("B nearer") },
        { 2.0e6, 2.0e6, 2.0e6, TEXT("equal ends") },
        { 0.0, 4.0e3, 0.0, TEXT("at end A") },
        { nan, 2.0e6, 2.0e6, TEXT("A NaN") },
        { 7.5e5, nan, 7.5e5, TEXT("B NaN") },
        { inf, 5.0e3, 5.0e3, TEXT("A infinite") },
        { 12.3, inf, 12.3, TEXT("B infinite") },
        { inf, inf, inf, TEXT("both infinite") },
        { nan, inf, inf, TEXT("A NaN, B infinite") },
        { nan, nan, nan, TEXT("both NaN") },
    };
    for (const FCompactCase& compactCase : cases)
    {
        FString text = TEXT("stale");
        FormatLaneLabelCompactLine(TEXT("Uranus"), TEXT("Neptune"), compactCase.DistanceAM, compactCase.DistanceBM,
            text);
        TestEqual(compactCase.What, text, LaneLabelExpectedCompactLine(TEXT("Uranus"), TEXT("Neptune"),
            compactCase.NearestM));
    }

    // Names are written as given, in order
    FString moons;
    FormatLaneLabelCompactLine(TEXT("Io"), TEXT("Europa"), 3.0e8, 2.0e8, moons);
    TestEqual(TEXT("Io - Europa, Europa nearer"), moons, LaneLabelExpectedCompactLine(TEXT("Io"), TEXT("Europa"),
        2.0e8));
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelNearestTest, "SOLTest.LaneTunnelLabel.NearestDistance",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// LaneLabelNearestDistanceM is the smaller end distance; a NaN one is ignored unless both are NaN (then NaN);
// infinities are ordinary values
bool FSOLLaneTunnelLabelNearestTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::LaneLabelNearestDistanceM;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    TestEqual(TEXT("A smaller"), LaneLabelNearestDistanceM(3.0e6, 5.0e9), 3.0e6, LANELABEL_TOL);
    TestEqual(TEXT("B smaller"), LaneLabelNearestDistanceM(5.0e9, 3.0e6), 3.0e6, LANELABEL_TOL);
    TestEqual(TEXT("equal"), LaneLabelNearestDistanceM(7.0, 7.0), 7.0, LANELABEL_TOL);
    TestEqual(TEXT("zero and far"), LaneLabelNearestDistanceM(0.0, 1.0e12), 0.0, LANELABEL_TOL);
    TestEqual(TEXT("A NaN: B"), LaneLabelNearestDistanceM(nan, 4.0e3), 4.0e3, LANELABEL_TOL);
    TestEqual(TEXT("B NaN: A"), LaneLabelNearestDistanceM(4.0e3, nan), 4.0e3, LANELABEL_TOL);
    TestTrue(TEXT("both NaN: NaN"), FMath::IsNaN(LaneLabelNearestDistanceM(nan, nan)));
    TestEqual(TEXT("A infinite: B"), LaneLabelNearestDistanceM(inf, 4.0e3), 4.0e3, LANELABEL_TOL);
    TestEqual(TEXT("B infinite: A"), LaneLabelNearestDistanceM(4.0e3, inf), 4.0e3, LANELABEL_TOL);
    TestTrue(TEXT("both infinite: infinity"), LaneLabelNearestDistanceM(inf, inf) == inf);
    TestTrue(TEXT("A NaN, B infinite: infinity"), LaneLabelNearestDistanceM(nan, inf) == inf);
    TestTrue(TEXT("A infinite, B NaN: infinity"), LaneLabelNearestDistanceM(inf, nan) == inf);
    TestTrue(TEXT("minus infinity is the smaller"), LaneLabelNearestDistanceM(-inf, 4.0e3) == -inf);
    return true;
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelCompactKeyTest, "SOLTest.LaneTunnelLabel.CompactKey",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// MakeLaneLabelCompactKey(lane, a, b) holds the lane, LaneLabelDistanceKey of the nearest distance, 0 for DistanceB and
// Ships, and bCompact (MakeLaneLabelKey keeps it false); it changes exactly when the compact text does: not for a
// change of the farther distance or a swap of the nearer end with the same printed nearest value, but for a new
// printed nearest value or lane; a compact key never equals the full key of the same inputs
bool FSOLLaneTunnelLabelCompactKeyTest::RunTest(const FString& /*parameters*/)
{
    using SOLLaneTunnelLabel::LaneLabelDistanceKey;
    using SOLLaneTunnelLabel::MakeLaneLabelCompactKey;
    using SOLLaneTunnelLabel::MakeLaneLabelKey;
    constexpr double au = 1.495978707e11;
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    // The fields
    const FSOLLaneLabelKey base = MakeLaneLabelCompactKey(3, 0.42 * au, 1.08 * au);
    TestEqual(TEXT("compact key: lane"), base.Lane, 3);
    TestEqual(TEXT("compact key: nearest distance in DistanceA"), base.DistanceA, LaneLabelDistanceKey(0.42 * au));
    TestEqual(TEXT("compact key: DistanceB 0"), base.DistanceB, static_cast<int64>(0));
    TestEqual(TEXT("compact key: Ships 0"), base.Ships, 0);
    TestTrue(TEXT("compact key: bCompact"), base.bCompact);
    TestEqual(TEXT("compact key, B nearer: DistanceA"), MakeLaneLabelCompactKey(5, 5.0e9, 3.0e6).DistanceA,
        LaneLabelDistanceKey(3.0e6));
    TestEqual(TEXT("compact key, A NaN: DistanceA is B's"), MakeLaneLabelCompactKey(1, nan, 2.0e6).DistanceA,
        LaneLabelDistanceKey(2.0e6));
    TestFalse(TEXT("full key: not compact"), MakeLaneLabelKey(3, 0.42 * au, 1.08 * au, 62400).bCompact);

    // Never equal to the full key of the same inputs, even where the full key's other fields would match
    struct FKeyInputs
    {
        int32 Lane;
        double DistanceAM;
        double DistanceBM;
    };
    const FKeyInputs inputs[] = { { 3, 0.42 * au, 1.08 * au }, { 0, 0.0, 0.0 }, { 2, 5.0e6, 5.0e6 },
        { 1, nan, nan }, { 4, inf, inf } };
    for (const FKeyInputs& keyInputs : inputs)
    {
        const FSOLLaneLabelKey compactKey = MakeLaneLabelCompactKey(keyInputs.Lane, keyInputs.DistanceAM,
            keyInputs.DistanceBM);
        for (const int32 ships : { 0, 1, 62400 })
        {
            TestFalse(FString::Printf(TEXT("lane %d, %g m and %g m, %d ships: compact differs from full"),
                keyInputs.Lane, keyInputs.DistanceAM, keyInputs.DistanceBM, ships), compactKey
                == MakeLaneLabelKey(keyInputs.Lane, keyInputs.DistanceAM, keyInputs.DistanceBM, ships));
        }
    }

    // The same printed nearest value: the same key
    TestTrue(TEXT("the same values: equal"), base == MakeLaneLabelCompactKey(3, 0.42 * au, 1.08 * au));
    TestTrue(TEXT("the nearest moving inside its rounding step: equal"),
        base == MakeLaneLabelCompactKey(3, 0.42 * au + 10.0, 1.08 * au));
    TestTrue(TEXT("the farther distance changing: equal"), base == MakeLaneLabelCompactKey(3, 0.42 * au, 1.5 * au));
    TestTrue(TEXT("the farther distance stepping its printed value: equal"),
        base == MakeLaneLabelCompactKey(3, 0.42 * au, 1.0801 * au));
    TestTrue(TEXT("the ends swapped: equal"), base == MakeLaneLabelCompactKey(3, 1.08 * au, 0.42 * au));
    TestTrue(TEXT("the other end nearer with the same printed value: equal"),
        base == MakeLaneLabelCompactKey(3, 2.0 * au, 0.42 * au + 10.0));

    // A new printed nearest value or lane: a new key
    TestFalse(TEXT("the nearest stepping: differs"), base == MakeLaneLabelCompactKey(3, 0.4201 * au, 1.08 * au));
    TestFalse(TEXT("the other end becoming nearer: differs"), base == MakeLaneLabelCompactKey(3, 0.42 * au, 0.3 * au));
    TestFalse(TEXT("another lane: differs"), base == MakeLaneLabelCompactKey(4, 0.42 * au, 1.08 * au));

    // Bad values keep stable keys; a NaN end is ignored
    TestTrue(TEXT("both NaN: stable"), MakeLaneLabelCompactKey(1, nan, nan) == MakeLaneLabelCompactKey(1, nan, nan));
    TestTrue(TEXT("both infinite: stable"), MakeLaneLabelCompactKey(1, inf, inf)
        == MakeLaneLabelCompactKey(1, inf, inf));
    TestTrue(TEXT("a NaN end either side: equal"), MakeLaneLabelCompactKey(1, nan, 2.0e6)
        == MakeLaneLabelCompactKey(1, 2.0e6, nan));
    TestTrue(TEXT("a NaN end is the other end alone"), MakeLaneLabelCompactKey(1, nan, 2.0e6)
        == MakeLaneLabelCompactKey(1, 2.0e6, 9.0e9));

    // Against the text itself: nudging the nearer end, swapping the ends or moving the farther one changes the key
    // exactly when the compact line changes
    uint32 state = 13u;
    int32 failures = 0;
    FString firstFailure;
    FString firstText;
    FString secondText;
    for (int32 index = 0; index < 3000; ++index)
    {
        const double distanceAM = 1.0e3 * FMath::Pow(1.0e10, LaneLabelKeyJitter(state));
        const double distanceBM = 1.0e3 * FMath::Pow(1.0e10, LaneLabelKeyJitter(state));
        const double nudge = 1.0 + 1.0e-5 * (LaneLabelKeyJitter(state) - 0.5);
        const int32 mode = static_cast<int32>(LaneLabelKeyJitter(state) * 3.0);
        const double otherAM = mode == 0 ? distanceAM * nudge : (mode == 1 ? distanceBM : distanceAM);
        const double otherBM = mode == 0 ? distanceBM : (mode == 1 ? distanceAM * nudge
            : distanceBM * (0.5 + 2.0 * LaneLabelKeyJitter(state)));
        SOLLaneTunnelLabel::FormatLaneLabelCompactLine(TEXT("Earth"), TEXT("Mars"), distanceAM, distanceBM, firstText);
        SOLLaneTunnelLabel::FormatLaneLabelCompactLine(TEXT("Earth"), TEXT("Mars"), otherAM, otherBM, secondText);
        const bool bSameText = firstText == secondText;
        const bool bSameKey = MakeLaneLabelCompactKey(0, distanceAM, distanceBM)
            == MakeLaneLabelCompactKey(0, otherAM, otherBM);
        if (bSameText != bSameKey)
        {
            if (failures == 0)
            {
                firstFailure = FString::Printf(TEXT("\"%s\" vs \"%s\": keys %s"), *firstText, *secondText,
                    bSameKey ? TEXT("equal") : TEXT("differ"));
            }
            ++failures;
        }
    }
    if (!TestEqual(TEXT("the compact key follows the compact text over 3,000 changed labels"), failures, 0))
    {
        AddInfo(TEXT("first: ") + firstFailure);
    }
    return true;
}

// Contract change (2026-11-12, test first): placement hysteresis, a label first tries last frame's candidate in last
// frame's form and keeps it while that attempt is usable, else the normal order applies unchanged
namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns a copy of a label input that remembers last frame's candidate and form
    FSOLLaneLabelInput LaneLabelWithPrevious(const FSOLLaneLabelInput& input, const int32 previousCandidate,
        const bool bPreviousCompact)
    {
        FSOLLaneLabelInput remembered = input;
        remembered.PreviousCandidate = previousCandidate;
        remembered.bPreviousCompact = bPreviousCompact;
        return remembered;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns one label's placement with no previous candidate (the normal order), the rows and avoid boxes as given
    FSOLLaneLabelPlacement LaneLabelPlaceFresh(const FSOLLaneLabelInput& input, const TArray<FVector2D>& rowStarts,
        const TArray<FVector2D>& rowEnds, TConstArrayView<FBox2D> avoidBoxes)
    {
        const TArray<FSOLLaneLabelInput> labels = { LaneLabelWithPrevious(input, INDEX_NONE, false) };
        TArray<FSOLLaneLabelPlacement> out;
        LaneLabelPlace(labels, rowStarts, rowEnds, out, avoidBoxes);
        return out[0];
    }
}

//////////////////////////////////////////////////////////////////////////
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSOLLaneTunnelLabelHysteresisTest, "SOLTest.LaneTunnelLabel.Hysteresis",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

//////////////////////////////////////////////////////////////////////////
// Hysteresis: a label whose PreviousCandidate is 0 or 1 first tries that candidate in last frame's form (the compact
// one only when bPreviousCompact is set and the compact size is available) and keeps it while it is usable (anchor in
// front and not occluded, on the viewport, clear of the rows, the avoid boxes and earlier labels), even when an
// earlier attempt of the normal order would fit too; otherwise, with no previous candidate (INDEX_NONE or out of
// range) or with a previous compact form and no compact size, the normal order full@12, full@6, compact@12, compact@6
// applies unchanged; the box geometry, the reported candidate and form and the placed count follow what was taken
bool FSOLLaneTunnelLabelHysteresisTest::RunTest(const FString& /*parameters*/)
{
    TArray<FSOLLaneLabelInput> labels;
    TArray<FVector2D> starts;
    TArray<FVector2D> ends;
    TArray<FBox2D> avoid;
    TArray<FSOLLaneLabelPlacement> out;
    const FVector2D top(960.0, 400.0);
    const FVector2D bottom(600.0, 700.0);
    const FVector2D compact(120.0, 18.0);

    // Places one label with the rows and avoid boxes as they stand; the placed count must match its placed flag
    const auto placeOne = [&](const FSOLLaneLabelInput& input, const FString& what)
    {
        labels = { input };
        const int32 placed = LaneLabelPlace(labels, starts, ends, out, avoid);
        TestEqual(what + TEXT(": placed count"), placed, out[0].bPlaced ? 1 : 0);
        return out[0];
    };

    // Places one label and checks it was placed with the given candidate and form
    const auto expectTaken = [&](const FSOLLaneLabelInput& input, const int32 candidate, const bool bCompact,
        const FString& what)
    {
        const FSOLLaneLabelPlacement placement = placeOne(input, what);
        TestTrue(what + TEXT(": placed"), placement.bPlaced);
        TestEqual(what + TEXT(": candidate"), placement.Candidate, candidate);
        TestEqual(what + TEXT(": compact"), placement.bCompact, bCompact);
        return placement;
    };

    // Places one label, checks its candidate and form and that it matches the no-previous (normal order) result
    const auto expectNormal = [&](const FSOLLaneLabelInput& input, const int32 candidate, const bool bCompact,
        const FString& what)
    {
        const FSOLLaneLabelPlacement placement = expectTaken(input, candidate, bCompact, what);
        TestTrue(what + TEXT(": the normal order's result"),
            LaneLabelSamePlacement(placement, LaneLabelPlaceFresh(input, starts, ends, avoid)));
    };

    // Checks a placement's box: the given size, centred on the anchor horizontally and outward from it vertically
    const auto expectBox = [this](const FSOLLaneLabelPlacement& placement, const FVector2D& anchor,
        const FVector2D& size, const FString& what)
    {
        const FBox2D& box = placement.BoxPx;
        TestEqual(what + TEXT(": width"), box.Max.X - box.Min.X, size.X, LANELABEL_TOL);
        TestEqual(what + TEXT(": height"), box.Max.Y - box.Min.Y, size.Y, LANELABEL_TOL);
        TestEqual(what + TEXT(": centred on the anchor"), 0.5 * (box.Min.X + box.Max.X), anchor.X, LANELABEL_TOL);
        TestEqual(what + TEXT(": outward from the anchor"),
            placement.Candidate == LANELABEL_TOP ? box.Max.Y : box.Min.Y, anchor.Y, LANELABEL_TOL);
    };

    // Full boxes: 12 o'clock X [870, 1050] Y [346, 400], 6 o'clock X [510, 690] Y [700, 754]. Compact boxes:
    // 12 o'clock X [900, 1020] Y [382, 400], 6 o'clock X [540, 660] Y [700, 718]
    const FSOLLaneLabelInput full = LaneLabelInput(top, bottom);
    const FSOLLaneLabelInput withCompact = LaneLabelCompactInput(top, bottom, LANELABEL_SIZE, compact);
    const FSOLLaneLabelInput previousBottom = LaneLabelWithPrevious(full, LANELABEL_BOTTOM, false);

    // Last frame at full 6 o'clock: kept although full 12 o'clock is usable too (the normal order would take that)
    TestEqual(TEXT("no previous: 12 o'clock"), LaneLabelPlaceFresh(full, starts, ends, avoid).Candidate,
        LANELABEL_TOP);
    expectBox(expectTaken(previousBottom, LANELABEL_BOTTOM, false, TEXT("previous full 6")), bottom, LANELABEL_SIZE,
        TEXT("previous full 6"));
    expectBox(expectTaken(LaneLabelWithPrevious(full, LANELABEL_TOP, false), LANELABEL_TOP, false,
        TEXT("previous full 12")), top, LANELABEL_SIZE, TEXT("previous full 12"));
    expectTaken(LaneLabelWithPrevious(withCompact, LANELABEL_BOTTOM, false), LANELABEL_BOTTOM, false,
        TEXT("previous full 6, compact size given"));

    // Last frame compact: kept although the full form fits at the same candidate
    expectBox(expectTaken(LaneLabelWithPrevious(withCompact, LANELABEL_TOP, true), LANELABEL_TOP, true,
        TEXT("previous compact 12")), top, compact, TEXT("previous compact 12"));
    expectBox(expectTaken(LaneLabelWithPrevious(withCompact, LANELABEL_BOTTOM, true), LANELABEL_BOTTOM, true,
        TEXT("previous compact 6")), bottom, compact, TEXT("previous compact 6"));

    // Both full boxes blocked by rows (x 880 and 520, clear of the compact boxes): the normal order takes compact 12,
    // a previous compact 6 is kept, and a previous full 6 (now blocked) takes the normal order's compact 12, not
    // compact 6
    LaneLabelAddVerticalRow(880.0, starts, ends);
    LaneLabelAddVerticalRow(520.0, starts, ends);
    expectNormal(withCompact, LANELABEL_TOP, true, TEXT("full blocked, no previous"));
    expectBox(expectTaken(LaneLabelWithPrevious(withCompact, LANELABEL_BOTTOM, true), LANELABEL_BOTTOM, true,
        TEXT("full blocked, previous compact 6")), bottom, compact, TEXT("full blocked, previous compact 6"));
    expectNormal(LaneLabelWithPrevious(withCompact, LANELABEL_BOTTOM, false), LANELABEL_TOP, true,
        TEXT("full blocked, previous full 6"));
    starts.Reset();
    ends.Reset();

    // A previous full candidate no longer usable falls back to the normal order
    {
        FSOLLaneLabelInput occluded = previousBottom;
        occluded.bBottomOccluded = true;
        expectNormal(occluded, LANELABEL_TOP, false, TEXT("previous 6 behind a planet"));

        FSOLLaneLabelInput behind = previousBottom;
        behind.bBottomInFront = false;
        expectNormal(behind, LANELABEL_TOP, false, TEXT("previous 6 behind the camera"));

        const FSOLLaneLabelInput offScreen = LaneLabelWithPrevious(LaneLabelInput(top, FVector2D(600.0, 1060.0)),
            LANELABEL_BOTTOM, false);
        expectNormal(offScreen, LANELABEL_TOP, false, TEXT("previous 6 off the viewport"));

        LaneLabelAddVerticalRow(bottom.X, starts, ends);
        expectNormal(previousBottom, LANELABEL_TOP, false, TEXT("previous 6 over a row"));
        starts.Reset();
        ends.Reset();

        avoid = { FBox2D(FVector2D(550.0, 720.0), FVector2D(650.0, 800.0)) };
        expectNormal(previousBottom, LANELABEL_TOP, false, TEXT("previous 6 behind a panel"));
        avoid = { FBox2D(FVector2D(bottom.X + 0.5 * LANELABEL_SIZE.X + 3.0, 710.0), FVector2D(800.0, 740.0)) };
        expectNormal(previousBottom, LANELABEL_TOP, false, TEXT("previous 6 within the gap of a panel"));
        avoid.Reset();

        FSOLLaneLabelInput topOccluded = LaneLabelWithPrevious(full, LANELABEL_TOP, false);
        topOccluded.bTopOccluded = true;
        expectNormal(topOccluded, LANELABEL_BOTTOM, false, TEXT("previous 12 behind a planet"));
    }

    // A previous compact attempt no longer usable falls back to the normal order (the full form first)
    {
        FSOLLaneLabelInput occluded = LaneLabelWithPrevious(withCompact, LANELABEL_TOP, true);
        occluded.bTopOccluded = true;
        expectNormal(occluded, LANELABEL_BOTTOM, false, TEXT("previous compact 12 behind a planet"));

        avoid = { FBox2D(FVector2D(940.0, 385.0), FVector2D(980.0, 395.0)) };
        expectNormal(LaneLabelWithPrevious(withCompact, LANELABEL_TOP, true), LANELABEL_BOTTOM, false,
            TEXT("previous compact 12 behind a panel"));
        avoid.Reset();

        LaneLabelAddVerticalRow(bottom.X, starts, ends);
        expectNormal(LaneLabelWithPrevious(withCompact, LANELABEL_BOTTOM, true), LANELABEL_TOP, false,
            TEXT("previous compact 6 over a row"));
        starts.Reset();
        ends.Reset();

        FSOLLaneLabelInput behind = LaneLabelWithPrevious(withCompact, LANELABEL_BOTTOM, true);
        behind.bBottomInFront = false;
        expectNormal(behind, LANELABEL_TOP, false, TEXT("previous compact 6 behind the camera"));

        const FSOLLaneLabelInput offScreen = LaneLabelWithPrevious(LaneLabelCompactInput(top,
            FVector2D(600.0, 1070.0), LANELABEL_SIZE, compact), LANELABEL_BOTTOM, true);
        expectNormal(offScreen, LANELABEL_TOP, false, TEXT("previous compact 6 off the viewport"));
    }

    // A previous compact form with no compact size (zero, a zero side or NaN): the normal order, full 12 o'clock
    for (const FVector2D& noCompact : { FVector2D(0.0, 0.0), FVector2D(120.0, 0.0), FVector2D(NAN, 18.0) })
    {
        const FString what = FString::Printf(TEXT("previous compact 6, compact size (%f, %f)"), noCompact.X,
            noCompact.Y);
        expectNormal(LaneLabelWithPrevious(LaneLabelCompactInput(top, bottom, LANELABEL_SIZE, noCompact),
            LANELABEL_BOTTOM, true), LANELABEL_TOP, false, what);
    }

    // ...and with both full boxes blocked it is hidden (no compact form to keep)
    LaneLabelAddVerticalRow(880.0, starts, ends);
    LaneLabelAddVerticalRow(520.0, starts, ends);
    const FSOLLaneLabelPlacement noCompactBlocked = placeOne(LaneLabelWithPrevious(full, LANELABEL_TOP, true),
        TEXT("previous compact 12, no compact size, full blocked"));
    TestFalse(TEXT("previous compact 12, no compact size, full blocked: hidden"), noCompactBlocked.bPlaced);
    starts.Reset();
    ends.Reset();

    // No previous candidate (INDEX_NONE) or one out of range, with either form: exactly the no-previous result, in a
    // free view (full 12), with full 12 blocked (full 6) and with both full boxes blocked (compact 12)
    for (int32 setup = 0; setup < 3; ++setup)
    {
        starts.Reset();
        ends.Reset();
        if (setup >= 1)
        {
            LaneLabelAddVerticalRow(880.0, starts, ends);
        }
        if (setup >= 2)
        {
            LaneLabelAddVerticalRow(520.0, starts, ends);
        }
        const int32 expectedCandidate = setup == 1 ? LANELABEL_BOTTOM : LANELABEL_TOP;
        for (const int32 previous : { static_cast<int32>(INDEX_NONE), 2, -5, 7 })
        {
            for (const bool bPreviousCompact : { false, true })
            {
                const FString what = FString::Printf(TEXT("setup %d, previous %d%s"), setup, previous,
                    bPreviousCompact ? TEXT(" compact") : TEXT(""));
                expectNormal(LaneLabelWithPrevious(withCompact, previous, bPreviousCompact), expectedCandidate,
                    setup == 2, what);
            }
        }
    }
    starts.Reset();
    ends.Reset();

    // Two labels at the same 12 o'clock anchor: the second's previous 12 o'clock (full or compact) would overlap the
    // first placed label, so it is rejected and the second takes the normal order's full 6 o'clock
    const FVector2D secondBottom(1400.0, 800.0);
    for (const bool bPreviousCompact : { false, true })
    {
        const FString what = bPreviousCompact ? TEXT("second's previous compact 12 under the first")
            : TEXT("second's previous full 12 under the first");
        labels = { full, LaneLabelWithPrevious(LaneLabelCompactInput(top, secondBottom, LANELABEL_SIZE, compact),
            LANELABEL_TOP, bPreviousCompact) };
        TestEqual(what + TEXT(": two placed"), LaneLabelPlace(labels, starts, ends, out, avoid), 2);
        TestEqual(what + TEXT(": first at 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
        TestEqual(what + TEXT(": second at 6 o'clock"), out[1].Candidate, LANELABEL_BOTTOM);
        TestFalse(what + TEXT(": second full"), out[1].bCompact);
        TestFalse(what + TEXT(": boxes apart by the gap"),
            LaneLabelBoxesOverlap(out[0].BoxPx, out[1].BoxPx, LANELABEL_GAP_PX));
    }

    // The first's kept previous 6 o'clock takes the box the second had last frame (a shared 6 o'clock anchor): the
    // second moves to its 12 o'clock; with no previous the first sits at 12 o'clock and the second keeps 6 o'clock
    {
        const FSOLLaneLabelInput secondAtBottom = LaneLabelWithPrevious(LaneLabelInput(FVector2D(1400.0, 300.0),
            bottom), LANELABEL_BOTTOM, false);
        labels = { full, secondAtBottom };
        TestEqual(TEXT("first fresh: two placed"), LaneLabelPlace(labels, starts, ends, out, avoid), 2);
        TestEqual(TEXT("first fresh: first at 12 o'clock"), out[0].Candidate, LANELABEL_TOP);
        TestEqual(TEXT("first fresh: second keeps 6 o'clock"), out[1].Candidate, LANELABEL_BOTTOM);

        labels = { previousBottom, secondAtBottom };
        TestEqual(TEXT("first kept at 6: two placed"), LaneLabelPlace(labels, starts, ends, out, avoid), 2);
        TestEqual(TEXT("first kept at 6: first at 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
        TestEqual(TEXT("first kept at 6: second pushed to 12 o'clock"), out[1].Candidate, LANELABEL_TOP);
        TestFalse(TEXT("first kept at 6: boxes apart by the gap"),
            LaneLabelBoxesOverlap(out[0].BoxPx, out[1].BoxPx, LANELABEL_GAP_PX));

        // The second's 12 o'clock behind the camera too: hidden, the first unchanged
        FSOLLaneLabelInput secondBlind = secondAtBottom;
        secondBlind.bTopInFront = false;
        labels = { previousBottom, secondBlind };
        TestEqual(TEXT("second pushed, no 12 o'clock: one placed"), LaneLabelPlace(labels, starts, ends, out, avoid),
            1);
        TestEqual(TEXT("second pushed, no 12 o'clock: first at 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
        TestFalse(TEXT("second pushed, no 12 o'clock: second hidden"), out[1].bPlaced);

        // A shared 12 o'clock anchor: the first kept at 6 o'clock leaves 12 o'clock to the second
        labels = { previousBottom, LaneLabelInput(top, secondBottom) };
        TestEqual(TEXT("first kept at 6, shared top: two placed"), LaneLabelPlace(labels, starts, ends, out, avoid),
            2);
        TestEqual(TEXT("first kept at 6, shared top: first at 6 o'clock"), out[0].Candidate, LANELABEL_BOTTOM);
        TestEqual(TEXT("first kept at 6, shared top: second at 12 o'clock"), out[1].Candidate, LANELABEL_TOP);
    }

    // At most NPC_TUNNEL_LABEL_MAX_COUNT placed with previous candidates too, each keeping its 6 o'clock
    {
        labels.Reset();
        for (int32 index = 0; index < 6; ++index)
        {
            const double x = 150.0 + 300.0 * static_cast<double>(index);
            labels.Add(LaneLabelWithPrevious(LaneLabelInput(FVector2D(x, 300.0), FVector2D(x, 800.0)),
                LANELABEL_BOTTOM, false));
        }
        TestEqual(TEXT("six previous-6 labels: the cap placed"), LaneLabelPlace(labels, starts, ends, out, avoid),
            static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_COUNT));
        for (int32 index = 0; index < labels.Num(); ++index)
        {
            const bool bWithinCap = index < SOL::NPC_TUNNEL_LABEL_MAX_COUNT;
            TestEqual(FString::Printf(TEXT("six previous-6 labels: label %d placed iff within the cap"), index),
                out[index].bPlaced, bWithinCap);
            if (bWithinCap)
            {
                TestEqual(FString::Printf(TEXT("six previous-6 labels: label %d at 6 o'clock"), index),
                    out[index].Candidate, LANELABEL_BOTTOM);
            }
        }
    }
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
