/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "UI/SOLLaneTunnelLabel.h"

#include "Menu/SOLMenuText.h"
#include "SOLConstants.h"
#include "UI/SOLHudFormat.h"

namespace
{
    // Candidate indices of a label's anchors
    constexpr int32 LANE_LABEL_TOP = 0;
    constexpr int32 LANE_LABEL_BOTTOM = 1;

    //////////////////////////////////////////////////////////////////////////
    // Returns true when the segment p0-p1 meets the closed box [boxMin, boxMax] (Liang-Barsky clipping; a zero-length
    // segment is tested as a point)
    bool LaneLabelSegmentMeetsBox(const FVector2D& p0, const FVector2D& p1, const FVector2D& boxMin,
        const FVector2D& boxMax)
    {
        double tEnter = 0.0;
        double tLeave = 1.0;
        const double origins[] = { p0.X, p0.Y };
        const double deltas[] = { p1.X - p0.X, p1.Y - p0.Y };
        const double mins[] = { boxMin.X, boxMin.Y };
        const double maxs[] = { boxMax.X, boxMax.Y };
        for (int32 axis = 0; axis < 2; ++axis)
        {
            // A segment parallel to this axis's slab must start inside it
            if (deltas[axis] == 0.0)
            {
                if (origins[axis] < mins[axis] || origins[axis] > maxs[axis])
                {
                    return false;
                }
                continue;
            }
            double tNear = (mins[axis] - origins[axis]) / deltas[axis];
            double tFar = (maxs[axis] - origins[axis]) / deltas[axis];
            if (tNear > tFar)
            {
                Swap(tNear, tFar);
            }
            tEnter = FMath::Max(tEnter, tNear);
            tLeave = FMath::Min(tLeave, tFar);
            if (tEnter > tLeave)
            {
                return false;
            }
        }
        return true;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns true when box a overlaps box b grown by the gap on every side (touching does not count)
    bool LaneLabelBoxesOverlap(const FBox2D& a, const FBox2D& b, const double gapPx)
    {
        return a.Min.X < b.Max.X + gapPx && b.Min.X - gapPx < a.Max.X && a.Min.Y < b.Max.Y + gapPx
            && b.Min.Y - gapPx < a.Max.Y;
    }

    //////////////////////////////////////////////////////////////////////////
    // Appends a non-negative count with a comma between each group of three digits ("62,400")
    void LaneLabelAppendGroupedCount(FString& outText, const int32 count)
    {
        TCHAR digits[16];
        int32 digitCount = 0;
        uint32 remaining = static_cast<uint32>(FMath::Max(count, 0));
        do
        {
            digits[digitCount++] = static_cast<TCHAR>(TEXT('0') + remaining % 10u);
            remaining /= 10u;
        }
        while (remaining != 0u);

        // Most significant digit first, a separator before every remaining group of three
        for (int32 index = digitCount - 1; index >= 0; --index)
        {
            outText.AppendChar(digits[index]);
            if (index > 0 && index % 3 == 0)
            {
                outText.AppendChar(SOLMenuText::LANE_LABEL_THOUSANDS_SEPARATOR);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true when no row segment meets the box grown by the margin
bool SOLLaneTunnelLabel::LaneLabelClearOfDots(const FBox2D& labelBox, const TConstArrayView<FVector2D> rowStarts,
    const TConstArrayView<FVector2D> rowEnds, const double marginPx)
{
    const FVector2D grow(marginPx, marginPx);
    const FVector2D boxMin = labelBox.Min - grow;
    const FVector2D boxMax = labelBox.Max + grow;
    const int32 rowCount = FMath::Min(rowStarts.Num(), rowEnds.Num());
    for (int32 row = 0; row < rowCount; ++row)
    {
        if (LaneLabelSegmentMeetsBox(rowStarts[row], rowEnds[row], boxMin, boxMax))
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Places the labels greedily in order: 12 o'clock, else 6 o'clock, else hidden
int32 SOLLaneTunnelLabel::PlaceLaneLabels(const TConstArrayView<FSOLLaneLabelInput> labels,
    const TConstArrayView<FVector2D> rowStarts, const TConstArrayView<FVector2D> rowEnds, const FVector2D& viewportPx,
    const double rowMarginPx, const double gapPx, const TArrayView<FSOLLaneLabelPlacement> outPlacements)
{
    const int32 labelCount = FMath::Min(labels.Num(), outPlacements.Num());
    const bool bViewportValid = FMath::IsFinite(viewportPx.X) && FMath::IsFinite(viewportPx.Y) && viewportPx.X > 0.0
        && viewportPx.Y > 0.0;
    int32 placedCount = 0;
    for (int32 index = 0; index < labelCount; ++index)
    {
        FSOLLaneLabelPlacement& placement = outPlacements[index];
        placement = FSOLLaneLabelPlacement();
        const FSOLLaneLabelInput& label = labels[index];
        const bool bSizeValid = FMath::IsFinite(label.SizePx.X) && FMath::IsFinite(label.SizePx.Y)
            && label.SizePx.X >= 0.0 && label.SizePx.Y >= 0.0;
        if (index >= SOL::NPC_TUNNEL_LABEL_MAX_COUNT || !bViewportValid || !bSizeValid)
        {
            continue;
        }

        // Try the 12 o'clock candidate, then the 6 o'clock one
        for (int32 candidate = LANE_LABEL_TOP; candidate <= LANE_LABEL_BOTTOM; ++candidate)
        {
            const bool bTop = candidate == LANE_LABEL_TOP;
            const bool bInFront = bTop ? label.bTopInFront : label.bBottomInFront;
            const bool bOccluded = bTop ? label.bTopOccluded : label.bBottomOccluded;
            const FVector2D anchor = bTop ? label.TopAnchorPx : label.BottomAnchorPx;
            if (!bInFront || bOccluded || anchor.ContainsNaN())
            {
                continue;
            }

            // The box centred on the anchor horizontally and outward from the tube (above the 12 o'clock anchor,
            // below the 6 o'clock one), on the viewport and clear of the rows and of earlier placed labels
            const double minY = bTop ? anchor.Y - label.SizePx.Y : anchor.Y;
            const FBox2D box(FVector2D(anchor.X - 0.5 * label.SizePx.X, minY),
                FVector2D(anchor.X + 0.5 * label.SizePx.X, minY + label.SizePx.Y));
            if (box.Min.X < 0.0 || box.Min.Y < 0.0 || box.Max.X > viewportPx.X || box.Max.Y > viewportPx.Y)
            {
                continue;
            }
            if (!LaneLabelClearOfDots(box, rowStarts, rowEnds, rowMarginPx))
            {
                continue;
            }
            bool bOverlaps = false;
            for (int32 earlier = 0; earlier < index && !bOverlaps; ++earlier)
            {
                bOverlaps = outPlacements[earlier].bPlaced
                    && LaneLabelBoxesOverlap(box, outPlacements[earlier].BoxPx, gapPx);
            }
            if (bOverlaps)
            {
                continue;
            }
            placement.bPlaced = true;
            placement.Candidate = candidate;
            placement.BoxPx = box;
            ++placedCount;
            break;
        }
    }
    return placedCount;
}

//////////////////////////////////////////////////////////////////////////
// Writes the label's name, end-distance and ship-count lines
void SOLLaneTunnelLabel::FormatLaneLabelLines(const FString& nameA, const FString& nameB, const double distanceAM,
    const double distanceBM, const int32 shipCount, FString& outLine1, FString& outLine2, FString& outLine3)
{
    outLine1.Reset();
    outLine1 += nameA;
    outLine1 += SOLMenuText::LANE_LABEL_NAME_SEPARATOR;
    outLine1 += nameB;

    outLine2.Reset();
    outLine2 += nameA;
    outLine2.AppendChar(TEXT(' '));
    SOLHudFormat::AppendDistanceM(outLine2, distanceAM);
    outLine2 += SOLMenuText::LANE_LABEL_DISTANCE_SEPARATOR;
    outLine2 += nameB;
    outLine2.AppendChar(TEXT(' '));
    SOLHudFormat::AppendDistanceM(outLine2, distanceBM);

    outLine3.Reset();
    LaneLabelAppendGroupedCount(outLine3, shipCount);
    outLine3 += shipCount == 1 ? SOLMenuText::LANE_LABEL_SHIP : SOLMenuText::LANE_LABEL_SHIPS;
}
