/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// One lane tunnel label to place (SDD 49 section 4.9): its two projected anchor candidates (12 and 6 o'clock on the
// tunnel wall), whether each is in front of the camera and not behind a planet, and the label's size in pixels
struct FSOLLaneLabelInput
{
    FVector2D TopAnchorPx = FVector2D::ZeroVector;      // 12 o'clock anchor on screen (candidate 0)
    FVector2D BottomAnchorPx = FVector2D::ZeroVector;   // 6 o'clock anchor on screen (candidate 1)
    FVector2D SizePx = FVector2D::ZeroVector;           // Label box size
    bool bTopInFront = false;                           // The 12 o'clock anchor is in front of the camera
    bool bTopOccluded = false;                          // ...and hidden behind a planet
    bool bBottomInFront = false;                        // The 6 o'clock anchor is in front of the camera
    bool bBottomOccluded = false;                       // ...and hidden behind a planet
};

// One label's placement: placed or hidden, the candidate it took and its box (centred on that anchor horizontally,
// above a 12 o'clock anchor and below a 6 o'clock one)
struct FSOLLaneLabelPlacement
{
    bool bPlaced = false;                               // A candidate was usable this frame
    int32 Candidate = INDEX_NONE;                       // 0 = 12 o'clock, 1 = 6 o'clock, INDEX_NONE when hidden
    FBox2D BoxPx = FBox2D(ForceInit);                   // The label's screen box when placed
};

// Pure layout and text of the lane tunnel wall labels (SDD 49 section 4.9, contract cases 18-20, Q4): the box clear of
// the projected guide-dot rows, the greedy placement strongest first and the three text lines (no side line)
namespace SOLLaneTunnelLabel
{
    // Returns true when the box, grown by the margin on every side, meets none of the row segments (rowStarts[i] to
    // rowEnds[i], the shorter list used; a point row is tested as a point); true for no rows
    SOLTEST_API bool LaneLabelClearOfDots(const FBox2D& labelBox, TConstArrayView<FVector2D> rowStarts,
        TConstArrayView<FVector2D> rowEnds, double marginPx);

    // Places the labels in the given order (strongest first), each at its 12 o'clock candidate when that box is usable,
    // else at 6 o'clock, else hidden. The box has the label's size, centred on the anchor horizontally, its bottom edge
    // on a 12 o'clock anchor and its top edge on a 6 o'clock one (outward from the tube). A candidate is usable when
    // its anchor is in front of the camera and not occluded, the box lies on the viewport, is clear of every row by
    // rowMarginPx and does not overlap an earlier placed box grown by gapPx. At most NPC_TUNNEL_LABEL_MAX_COUNT labels
    // (the first ones) are placed; nothing is placed for a non-finite or non-positive viewport or a label with a
    // non-finite or negative size. Every entry of outPlacements up to the shorter list is written. Returns the placed
    // count
    SOLTEST_API int32 PlaceLaneLabels(TConstArrayView<FSOLLaneLabelInput> labels, TConstArrayView<FVector2D> rowStarts,
        TConstArrayView<FVector2D> rowEnds, const FVector2D& viewportPx, double rowMarginPx, double gapPx,
        TArrayView<FSOLLaneLabelPlacement> outPlacements);

    // Writes a label's three lines: "A - B", "A <distance>   B <distance>" (SOLHudFormat's distance format) and the
    // ship count with thousands separators and "ships" ("1 ship"; a negative count reads as 0); the fixed words and
    // separators are SOLMenuText::LANE_LABEL_*
    SOLTEST_API void FormatLaneLabelLines(const FString& nameA, const FString& nameB, double distanceAM,
        double distanceBM, int32 shipCount, FString& outLine1, FString& outLine2, FString& outLine3);
}
