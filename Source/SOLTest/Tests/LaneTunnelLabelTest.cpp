/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLLaneTunnelLabel.h"
#include "SOLConstants.h"
#include "UI/SOLHudFormat.h"

#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

// Tests for the issue #49 lane tunnel wall labels (SOLLaneTunnelLabel, SDD 49 section 6 cases 18-20, decision Q4),
// written against that contract before the production code existed (part 49a, test first): a label box clear of the
// projected guide-dot rows, the greedy placement of up to NPC_TUNNEL_LABEL_MAX_COUNT labels strongest first (12
// o'clock, else 6 o'clock, else hidden; never over a row or another label) and the three text lines (no side line).
// Review amendment (2026-11-12): the box sits outward from the tube (above a 12 o'clock anchor, below a 6 o'clock one),
// the properties test checks the clearance with its own distance, and a bad size or viewport places nothing.

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
    // Runs the placement with the tests' viewport and margins; the outputs are pre-filled with a placed garbage value
    // so every entry must be written
    int32 LaneLabelPlace(const TArray<FSOLLaneLabelInput>& labels, const TArray<FVector2D>& rowStarts,
        const TArray<FVector2D>& rowEnds, TArray<FSOLLaneLabelPlacement>& outPlacements)
    {
        FSOLLaneLabelPlacement garbage;
        garbage.bPlaced = true;
        garbage.Candidate = 7;
        garbage.BoxPx = FBox2D(FVector2D(-5.0, -5.0), FVector2D(-1.0, -1.0));
        outPlacements.Init(garbage, labels.Num());
        return SOLLaneTunnelLabel::PlaceLaneLabels(labels, rowStarts, rowEnds, LANELABEL_VIEWPORT,
            LANELABEL_ROW_MARGIN_PX, LANELABEL_GAP_PX, outPlacements);
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
    // Returns true when two placements agree (placed flag, candidate, and box when placed)
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
        return a.Candidate == b.Candidate && a.BoxPx.Min.Equals(b.BoxPx.Min, LANELABEL_TOL)
            && a.BoxPx.Max.Equals(b.BoxPx.Max, LANELABEL_TOL);
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
            LANELABEL_VIEWPORT, LANELABEL_ROW_MARGIN_PX, LANELABEL_GAP_PX, two), 2);
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
            SOLLaneTunnelLabel::PlaceLaneLabels(labels, starts, ends, badViewport, LANELABEL_ROW_MARGIN_PX,
                LANELABEL_GAP_PX, single), 0);
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
// horizontally and outward from it vertically; at most the cap are placed; removing later labels never changes an
// earlier one's placement; a label at 6 o'clock could not have taken 12 o'clock
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

#endif // WITH_DEV_AUTOMATION_TESTS
