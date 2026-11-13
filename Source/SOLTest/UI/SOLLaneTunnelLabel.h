/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// One lane tunnel label to place (SDD 49 section 4.9): its two projected anchor candidates (12 and 6 o'clock on the
// tunnel wall), whether each is in front of the camera and not behind a planet, and the sizes in pixels of its full
// three-line form and of its compact one-line fallback
struct FSOLLaneLabelInput
{
    FVector2D TopAnchorPx = FVector2D::ZeroVector;      // 12 o'clock anchor on screen (candidate 0)
    FVector2D BottomAnchorPx = FVector2D::ZeroVector;   // 6 o'clock anchor on screen (candidate 1)
    FVector2D SizePx = FVector2D::ZeroVector;           // Full (three-line) label box size
    FVector2D CompactSizePx = FVector2D::ZeroVector;    // Compact (one-line) box size; not tried unless both > 0
    bool bTopInFront = false;                           // The 12 o'clock anchor is in front of the camera
    bool bTopOccluded = false;                          // ...and hidden behind a planet
    bool bBottomInFront = false;                        // The 6 o'clock anchor is in front of the camera
    bool bBottomOccluded = false;                       // ...and hidden behind a planet
    int32 PreviousCandidate = INDEX_NONE;               // Last frame's candidate (0 / 1; anything else = none)
    bool bPreviousCompact = false;                      // ...placed in the compact form
};

// One label's placement: placed or hidden, the candidate and form it took and its box (centred on that anchor
// horizontally, above a 12 o'clock anchor and below a 6 o'clock one)
struct FSOLLaneLabelPlacement
{
    bool bPlaced = false;                               // A candidate was usable this frame
    bool bCompact = false;                              // Placed in the compact one-line form
    int32 Candidate = INDEX_NONE;                       // 0 = 12 o'clock, 1 = 6 o'clock, INDEX_NONE when hidden
    FBox2D BoxPx = FBox2D(ForceInit);                   // The label's screen box when placed
};

// The displayed values of a label's text (SDD 49 section 4.9): equal exactly when the text would read the same, so the
// HUD reformats a label only when this changes. A compact key holds the nearest end distance in DistanceA, 0 in
// DistanceB and Ships, and never equals a full key
struct FSOLLaneLabelKey
{
    int32 Lane = INDEX_NONE;                            // Lane (its body names)
    int64 DistanceA = 0;                                // LaneLabelDistanceKey of the distance to end ring A
    int64 DistanceB = 0;                                // ...and to end ring B
    int32 Ships = 0;                                    // Ship count (negative keys as 0)
    bool bCompact = false;                              // The key of the compact one-line form

    // Returns whether two keys read the same
    bool operator==(const FSOLLaneLabelKey& other) const
    {
        return Lane == other.Lane && DistanceA == other.DistanceA && DistanceB == other.DistanceB
            && Ships == other.Ships && bCompact == other.bCompact;
    }
};

// Pure layout and text of the lane tunnel wall labels (SDD 49 section 4.9, contract cases 18-20, Q4): the box clear of
// the projected guide-dot rows and the HUD panels, the greedy placement strongest first, the three text lines (no
// side line) and the compact one-line fallback
namespace SOLLaneTunnelLabel
{
    // Returns true when the box, grown by the margin on every side, meets none of the row segments (rowStarts[i] to
    // rowEnds[i], the shorter list used; a point row is tested as a point); true for no rows. Row i's margin is
    // marginPx plus rowExtraMarginsPx[i] when that list has an entry for it (its projected dot radius), else marginPx
    SOLTEST_API bool LaneLabelClearOfDots(const FBox2D& labelBox, TConstArrayView<FVector2D> rowStarts,
        TConstArrayView<FVector2D> rowEnds, double marginPx,
        TConstArrayView<double> rowExtraMarginsPx = TConstArrayView<double>());

    // Returns the projected radius in pixels of a guide dot at a view depth: half of max(floorDiameterPx,
    // dotDiameterCm x focalPx / depthCm), the shader's world-sized near dots over their pixel floor (49c); half the
    // floor for a non-finite or non-positive depth, diameter or focal length
    SOLTEST_API double LaneLabelDotRadiusPx(double depthCm, double dotDiameterCm, double focalPx,
        double floorDiameterPx);

    // Places the labels in the given order (strongest first). Each takes last frame's candidate and form first when
    // PreviousCandidate is 0 or 1 (and, for bPreviousCompact, the compact size is valid) and that spot is usable
    // (hysteresis), else the first usable of: the full form at its 12 o'clock candidate, the full form at 6 o'clock,
    // the compact form at 12 o'clock, the compact form at 6 o'clock (the compact ones only when CompactSizePx is
    // finite with both sides > 0); else it is hidden. The box has the form's size, centred on the anchor
    // horizontally, its bottom edge on a 12 o'clock anchor and its top edge on a 6 o'clock one (outward from the
    // tube). A candidate is usable when its anchor is in front of the camera and not occluded, the box lies on the
    // viewport, is clear of every row by rowMarginPx (plus that row's rowExtraMarginsPx entry, LaneLabelClearOfDots),
    // does not overlap any avoid box (a HUD panel) grown by gapPx (invalid avoid boxes ignored) and does not overlap
    // an earlier placed box grown by gapPx. At most NPC_TUNNEL_LABEL_MAX_COUNT labels (the first ones) are placed;
    // nothing is placed for a non-finite or non-positive viewport or a label with a non-finite or negative full size.
    // Every entry of outPlacements up to the shorter list is written. Returns the placed count
    SOLTEST_API int32 PlaceLaneLabels(TConstArrayView<FSOLLaneLabelInput> labels, TConstArrayView<FVector2D> rowStarts,
        TConstArrayView<FVector2D> rowEnds, TConstArrayView<FBox2D> avoidBoxes, const FVector2D& viewportPx,
        double rowMarginPx, double gapPx, TArrayView<FSOLLaneLabelPlacement> outPlacements,
        TConstArrayView<double> rowExtraMarginsPx = TConstArrayView<double>());

    // Writes a label's three lines: "A - B", "A <distance>   B <distance>" (SOLHudFormat's distance format) and the
    // ship count with thousands separators and "ships" ("1 ship"; a negative count reads as 0); the fixed words and
    // separators are SOLMenuText::LANE_LABEL_*
    SOLTEST_API void FormatLaneLabelLines(const FString& nameA, const FString& nameB, double distanceAM,
        double distanceBM, int32 shipCount, FString& outLine1, FString& outLine2, FString& outLine3);

    // Writes the compact one-line fallback: "A - B", SOLMenuText::LANE_LABEL_COMPACT_SEPARATOR and the nearest end
    // distance (LaneLabelNearestDistanceM, SOLHudFormat's distance format)
    SOLTEST_API void FormatLaneLabelCompactLine(const FString& nameA, const FString& nameB, double distanceAM,
        double distanceBM, FString& outLine);

    // Returns the smaller of the two end distances; a NaN one is ignored unless both are NaN (then NaN)
    SOLTEST_API double LaneLabelNearestDistanceM(double distanceAM, double distanceBM);

    // Returns the displayed distance as a number: two distances share a key exactly when SOLHudFormat::FormatDistanceM
    // prints them the same (the unit is part of the key; formatted on the stack, no allocation); NaN and infinity have
    // stable keys of their own
    SOLTEST_API int64 LaneLabelDistanceKey(double distanceM);

    // Returns the key of a label's displayed values (a negative ship count keys as 0)
    SOLTEST_API FSOLLaneLabelKey MakeLaneLabelKey(int32 lane, double distanceAM, double distanceBM, int32 shipCount);

    // Returns the key of the compact form's displayed values: the lane, LaneLabelDistanceKey of the nearest end
    // distance, bCompact set (it changes exactly when FormatLaneLabelCompactLine's text would)
    SOLTEST_API FSOLLaneLabelKey MakeLaneLabelCompactKey(int32 lane, double distanceAM, double distanceBM);

    // Returns a label's displayed alpha moved linearly toward its target (clamped to [0, 1], NaN as 0) at
    // 1 / NPC_TUNNEL_LABEL_FADE_S per second, without overshoot; a NaN or non-positive step keeps the alpha (NaN as 0)
    SOLTEST_API double EaseLaneLabelAlpha(double currentAlpha, double targetAlpha, double deltaS);
}
