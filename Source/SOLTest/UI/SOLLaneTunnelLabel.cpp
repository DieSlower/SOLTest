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
// Returns true when no row segment meets the box grown by the margin (plus the row's own extra margin)
bool SOLLaneTunnelLabel::LaneLabelClearOfDots(const FBox2D& labelBox, const TConstArrayView<FVector2D> rowStarts,
    const TConstArrayView<FVector2D> rowEnds, const double marginPx, const TConstArrayView<double> rowExtraMarginsPx)
{
    const int32 rowCount = FMath::Min(rowStarts.Num(), rowEnds.Num());
    for (int32 row = 0; row < rowCount; ++row)
    {
        const double rowMarginPx = marginPx + (row < rowExtraMarginsPx.Num() ? rowExtraMarginsPx[row] : 0.0);
        const FVector2D grow(rowMarginPx, rowMarginPx);
        if (LaneLabelSegmentMeetsBox(rowStarts[row], rowEnds[row], labelBox.Min - grow, labelBox.Max + grow))
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns half of max(floor, world diameter x focal / depth): a guide dot's projected radius in pixels
double SOLLaneTunnelLabel::LaneLabelDotRadiusPx(const double depthCm, const double dotDiameterCm, const double focalPx,
    const double floorDiameterPx)
{
    const double floorPx = FMath::IsFinite(floorDiameterPx) ? FMath::Max(floorDiameterPx, 0.0) : 0.0;
    if (!FMath::IsFinite(depthCm) || !FMath::IsFinite(dotDiameterCm) || !FMath::IsFinite(focalPx) || depthCm <= 0.0
        || dotDiameterCm <= 0.0 || focalPx <= 0.0)
    {
        return 0.5 * floorPx;
    }
    return 0.5 * FMath::Max(floorPx, dotDiameterCm * focalPx / depthCm);
}

//////////////////////////////////////////////////////////////////////////
// Places the labels greedily in order: last frame's spot, full form at 12 then 6 o'clock, compact, else hidden
int32 SOLLaneTunnelLabel::PlaceLaneLabels(const TConstArrayView<FSOLLaneLabelInput> labels,
    const TConstArrayView<FVector2D> rowStarts, const TConstArrayView<FVector2D> rowEnds,
    const TConstArrayView<FBox2D> avoidBoxes, const FVector2D& viewportPx, const double rowMarginPx,
    const double gapPx, const TArrayView<FSOLLaneLabelPlacement> outPlacements,
    const TConstArrayView<double> rowExtraMarginsPx)
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

        // Last frame's candidate and form first while still usable (hysteresis), then the full form at 12 then
        // 6 o'clock, then (when it has a size) the compact form at 12 then 6 o'clock; attempt a = candidate a % 2,
        // compact for a >= 2
        const bool bCompactValid = FMath::IsFinite(label.CompactSizePx.X) && FMath::IsFinite(label.CompactSizePx.Y)
            && label.CompactSizePx.X > 0.0 && label.CompactSizePx.Y > 0.0;
        int32 attempts[5];
        int32 attemptCount = 0;
        const bool bHasPrevious = (label.PreviousCandidate == LANE_LABEL_TOP
            || label.PreviousCandidate == LANE_LABEL_BOTTOM) && (!label.bPreviousCompact || bCompactValid);
        const int32 previousAttempt = bHasPrevious ? label.PreviousCandidate + (label.bPreviousCompact ? 2 : 0)
            : INDEX_NONE;
        if (bHasPrevious)
        {
            attempts[attemptCount++] = previousAttempt;
        }
        for (int32 attempt = 0; attempt < (bCompactValid ? 4 : 2); ++attempt)
        {
            if (attempt != previousAttempt)
            {
                attempts[attemptCount++] = attempt;
            }
        }
        for (int32 tried = 0; tried < attemptCount && !placement.bPlaced; ++tried)
        {
            const int32 candidate = attempts[tried] % 2 == 0 ? LANE_LABEL_TOP : LANE_LABEL_BOTTOM;
            const bool bCompact = attempts[tried] >= 2;
            const FVector2D size = bCompact ? label.CompactSizePx : label.SizePx;
            const bool bTop = candidate == LANE_LABEL_TOP;
            const bool bInFront = bTop ? label.bTopInFront : label.bBottomInFront;
            const bool bOccluded = bTop ? label.bTopOccluded : label.bBottomOccluded;
            const FVector2D anchor = bTop ? label.TopAnchorPx : label.BottomAnchorPx;
            if (!bInFront || bOccluded || anchor.ContainsNaN())
            {
                continue;
            }

            // The box centred on the anchor horizontally and outward from the tube (above the 12 o'clock anchor,
            // below the 6 o'clock one), on the viewport and clear of the rows, the HUD panels and earlier labels
            const double minY = bTop ? anchor.Y - size.Y : anchor.Y;
            const FBox2D box(FVector2D(anchor.X - 0.5 * size.X, minY),
                FVector2D(anchor.X + 0.5 * size.X, minY + size.Y));
            if (box.Min.X < 0.0 || box.Min.Y < 0.0 || box.Max.X > viewportPx.X || box.Max.Y > viewportPx.Y)
            {
                continue;
            }
            if (!LaneLabelClearOfDots(box, rowStarts, rowEnds, rowMarginPx, rowExtraMarginsPx))
            {
                continue;
            }
            bool bOverlaps = false;
            for (int32 avoid = 0; avoid < avoidBoxes.Num() && !bOverlaps; ++avoid)
            {
                bOverlaps = avoidBoxes[avoid].bIsValid && LaneLabelBoxesOverlap(box, avoidBoxes[avoid], gapPx);
            }
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
            placement.bCompact = bCompact;
            placement.Candidate = candidate;
            placement.BoxPx = box;
            ++placedCount;
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

//////////////////////////////////////////////////////////////////////////
// Writes the compact line: the lane name and the nearest end distance
void SOLLaneTunnelLabel::FormatLaneLabelCompactLine(const FString& nameA, const FString& nameB,
    const double distanceAM, const double distanceBM, FString& outLine)
{
    outLine.Reset();
    outLine += nameA;
    outLine += SOLMenuText::LANE_LABEL_NAME_SEPARATOR;
    outLine += nameB;
    outLine += SOLMenuText::LANE_LABEL_COMPACT_SEPARATOR;
    SOLHudFormat::AppendDistanceM(outLine, LaneLabelNearestDistanceM(distanceAM, distanceBM));
}

//////////////////////////////////////////////////////////////////////////
// Returns the smaller end distance, a NaN one ignored unless both are NaN
double SOLLaneTunnelLabel::LaneLabelNearestDistanceM(const double distanceAM, const double distanceBM)
{
    if (FMath::IsNaN(distanceAM))
    {
        return distanceBM;
    }
    if (FMath::IsNaN(distanceBM))
    {
        return distanceAM;
    }
    return FMath::Min(distanceAM, distanceBM);
}

//////////////////////////////////////////////////////////////////////////
// Returns the displayed distance as a number: the printed digits (formatted on the stack exactly as the HUD prints
// them), their sign and the unit packed together
int64 SOLLaneTunnelLabel::LaneLabelDistanceKey(const double distanceM)
{
    // Bad values and numbers too long for the packing get fixed keys of their own
    constexpr int64 KEY_NAN = TNumericLimits<int64>::Min();
    constexpr int64 KEY_POSITIVE_INFINITY = TNumericLimits<int64>::Max();
    constexpr int64 KEY_NEGATIVE_INFINITY = TNumericLimits<int64>::Min() + 1;
    constexpr double MAX_PACKED_VALUE = 1.0e12;
    if (FMath::IsNaN(distanceM))
    {
        return KEY_NAN;
    }
    if (!FMath::IsFinite(distanceM))
    {
        return distanceM > 0.0 ? KEY_POSITIVE_INFINITY : KEY_NEGATIVE_INFINITY;
    }
    double value = 0.0;
    const int32 unit = SOLHudFormat::DistanceUnitM(distanceM, value);
    if (FMath::Abs(value) > MAX_PACKED_VALUE)
    {
        return value > 0.0 ? KEY_POSITIVE_INFINITY - 1 : KEY_NEGATIVE_INFINITY + 1;
    }

    // The number exactly as printed, then its digits read back as one integer (the decimal point dropped)
    TCHAR buffer[64];
    if (unit == 2)
    {
        FCString::Snprintf(buffer, UE_ARRAY_COUNT(buffer), TEXT("%.4f"), value);
    }
    else
    {
        FCString::Snprintf(buffer, UE_ARRAY_COUNT(buffer), TEXT("%.1f"), value);
    }
    int64 digits = 0;
    int64 negative = 0;
    for (const TCHAR* character = buffer; *character != TEXT('\0'); ++character)
    {
        if (*character == TEXT('-'))
        {
            negative = 1;
        }
        else if (*character >= TEXT('0') && *character <= TEXT('9'))
        {
            digits = digits * 10 + static_cast<int64>(*character - TEXT('0'));
        }
    }
    return (digits * 2 + negative) * 3 + unit;
}

//////////////////////////////////////////////////////////////////////////
// Returns the key of a label's displayed values
FSOLLaneLabelKey SOLLaneTunnelLabel::MakeLaneLabelKey(const int32 lane, const double distanceAM,
    const double distanceBM, const int32 shipCount)
{
    FSOLLaneLabelKey key;
    key.Lane = lane;
    key.DistanceA = LaneLabelDistanceKey(distanceAM);
    key.DistanceB = LaneLabelDistanceKey(distanceBM);
    key.Ships = FMath::Max(shipCount, 0);
    return key;
}

//////////////////////////////////////////////////////////////////////////
// Returns the key of the compact form's displayed values
FSOLLaneLabelKey SOLLaneTunnelLabel::MakeLaneLabelCompactKey(const int32 lane, const double distanceAM,
    const double distanceBM)
{
    FSOLLaneLabelKey key;
    key.Lane = lane;
    key.DistanceA = LaneLabelDistanceKey(LaneLabelNearestDistanceM(distanceAM, distanceBM));
    key.bCompact = true;
    return key;
}

//////////////////////////////////////////////////////////////////////////
// Returns a label's displayed alpha moved linearly toward its clamped target without overshoot
double SOLLaneTunnelLabel::EaseLaneLabelAlpha(const double currentAlpha, const double targetAlpha,
    const double deltaS)
{
    const double current = FMath::IsNaN(currentAlpha) ? 0.0 : FMath::Clamp(currentAlpha, 0.0, 1.0);
    if (FMath::IsNaN(deltaS) || deltaS <= 0.0)
    {
        return current;
    }
    const double target = FMath::IsNaN(targetAlpha) ? 0.0 : FMath::Clamp(targetAlpha, 0.0, 1.0);
    const double step = deltaS / SOL::NPC_TUNNEL_LABEL_FADE_S;
    return target > current ? FMath::Min(current + step, target) : FMath::Max(current - step, target);
}
