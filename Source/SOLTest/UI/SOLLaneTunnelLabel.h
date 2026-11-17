/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Traffic/SOLLaneTrafficMath.h"

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
    FBox2D OwnBoxPx = FBox2D(ForceInit);                // Grown by a line (PlaceLaneLabelWithLine): where the form's
                                                        // own lines go (centred on the anchor, the line below them);
                                                        // invalid from PlaceLaneLabels
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

// The displayed values of the lane traffic readout line (SDD 50 section 4.8): equal exactly when
// SOLLaneTunnelLabel::FormatMergeReadoutLine's text would read the same, so the HUD rebuilds the line only on a change
struct FSOLLaneMergeKey
{
    int64 Traffic = 0;                                  // The traffic speed as printed (digits, sign and unit)
    int64 Player = 0;                                   // The player speed as printed
    ESOLLaneMergeVerdict Verdict = ESOLLaneMergeVerdict::TooSlow;
    bool bWarpNote = false;

    // Returns whether two keys read the same
    bool operator==(const FSOLLaneMergeKey& other) const
    {
        return Traffic == other.Traffic && Player == other.Player && Verdict == other.Verdict
            && bWarpNote == other.bWarpNote;
    }
};

// Where the lane traffic readout line goes this frame (D18)
enum class ESOLLaneMergeWhere : uint8
{
    Hidden,                                             // No readout
    Label,                                              // The last line of its lane's placed tunnel label
    Fallback                                            // The fixed line under the speed readout
};

// The readout's spot: where, the label entry when in a label, and what the next frame's choice needs (hysteresis)
struct FSOLLaneMergeSpot
{
    ESOLLaneMergeWhere Where = ESOLLaneMergeWhere::Hidden;
    int32 LabelIndex = INDEX_NONE;                      // The entry of the label lists when Where is Label
    int32 Lane = INDEX_NONE;                            // The readout's lane (INDEX_NONE when hidden)
    bool bNoFit = false;                                // On the fallback line although its lane's label is placed:
                                                        // the label grown by the line fits nowhere (Q10)
    double FitS = 0.0;                                  // On the fallback line: how long the grown label has fit
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

    // Part 50c, the lane traffic readout line (SDD 50 section 4.8, D18)

    // Returns the verdict's word: SOLMenuText::LANE_TRAFFIC_MATCHED, _TOO_FAST or _TOO_SLOW
    SOLTEST_API const TCHAR* MergeVerdictText(ESOLLaneMergeVerdict verdict);

    // Writes the readout line over outLine: "Traffic <speed>   You <speed>   <verdict>" (SOLHudFormat's speed format,
    // the SOLMenuText::LANE_TRAFFIC_* tokens), plus the warp note when bWarpNote; a speed below
    // NPC_LANE_MERGE_ZERO_SPEED_MPS in size prints as 0 (never "-0.0")
    SOLTEST_API void FormatMergeReadoutLine(double trafficSpeedMps, double playerSpeedMps,
        ESOLLaneMergeVerdict verdict, bool bWarpNote, FString& outLine);

    // Returns the key of the readout line's displayed values (each speed formatted on the stack, no allocation; the
    // zero rule of FormatMergeReadoutLine applied first)
    SOLTEST_API FSOLLaneMergeKey MakeMergeReadoutKey(double trafficSpeedMps, double playerSpeedMps,
        ESOLLaneMergeVerdict verdict, bool bWarpNote);

    // Returns the band's colour: SOL::HudPalette::GOOD, AMBER or BAD
    SOLTEST_API FLinearColor MergeBandColor(ESOLLaneMergeBand band);

    // Returns whether the readout is computed this frame: the HUD allows it (a view, no map), the Lanes row is on (no
    // settings read as on) or the verification override is set, a lane table exists and the player has a ship
    SOLTEST_API bool IsMergeReadoutEnabled(bool bHudAllowed, bool bVerificationOverride, bool bHasSettings,
        bool bLanesRowShown, bool bHasLanes, bool bHasPlayerShip);

    // Returns where the readout goes (D18, Q10) and the state the next frame's call takes as previous: Hidden when it
    // is hidden; Fallback when no entry of labelLanes (up to the shorter list) names the readout's lane and is placed;
    // Fallback with bNoFit when that label is placed but the label grown by the line fits nowhere (bGrownFits false);
    // else Label (that entry), except that after a no-fit fallback line for the same lane (previous bNoFit, or its
    // wait under way: FitS > 0) the grown label must have fit for NPC_LANE_MERGE_LABEL_RETURN_S (FitS adds deltaS, a
    // NaN or negative step as 0) before the readout returns to it. A label spot is left at once when the grown label
    // stops fitting (the line is never drawn where it does not fit); a label newly placed takes the readout at once
    SOLTEST_API FSOLLaneMergeSpot ChooseMergeReadoutSpot(bool bReadoutShown, int32 readoutLane,
        TConstArrayView<int32> labelLanes, TConstArrayView<FSOLLaneLabelPlacement> placements, bool bGrownFits,
        const FSOLLaneMergeSpot& previous, double deltaS);

    // Returns which of the fallback line's spots (in order of preference) the line takes: the first lying on the
    // viewport, off every label box grown by labelGapPx and off every HUD panel box (invalid boxes ignored, touching
    // counts as meeting); the last spot when none is clear; INDEX_NONE without spots or with a non-finite or
    // non-positive viewport
    SOLTEST_API int32 ChooseMergeFallbackSpot(TConstArrayView<FBox2D> spotBoxes, TConstArrayView<FBox2D> labelBoxes,
        TConstArrayView<FBox2D> hudBoxes, const FVector2D& viewportPx, double labelGapPx);

    // Returns a label box size grown by one line: (max(width, line width), height + line height); unchanged for a
    // non-finite or negative line width or height
    SOLTEST_API FVector2D LaneLabelSizeWithLine(const FVector2D& sizePx, double lineWidthPx, double lineHeightPx);

    // Places the readout lane's label grown by the readout line as its last line (D18): the full and compact sizes
    // grown by LaneLabelSizeWithLine (the compact one only when CompactSizePx is finite with both sides > 0), the full
    // form first (at the candidate the label took, placed, then the other), then the compact form in the same order
    // (an anchor behind the camera or occluded skipped). Vertically the grown box keeps PlaceLaneLabels' rule (bottom
    // edge on a 12 o'clock anchor, top edge on a 6 o'clock one); horizontally it may slide sideways as long as it
    // still covers the form's own box centred on the anchor (OwnBoxPx: those columns, the box's top rows), taking the
    // clear position nearest to centred. It may overlap the guide-dot rows (none are tested) but must lie on the
    // viewport and must not overlap any avoid box (the HUD panels and the other placed labels, invalid ones ignored)
    // grown by gapPx (touching is not an overlap). Not placed when placed is not, for a bad viewport or full size, or
    // when no spot is usable
    SOLTEST_API FSOLLaneLabelPlacement PlaceLaneLabelWithLine(const FSOLLaneLabelInput& label,
        const FSOLLaneLabelPlacement& placed, double lineWidthPx, double lineHeightPx,
        TConstArrayView<FBox2D> avoidBoxes, const FVector2D& viewportPx, double gapPx);
}
