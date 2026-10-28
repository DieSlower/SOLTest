/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Issue #20 map hologram look (SDD 20 section 2.4): the ecliptic grid's level (spacing and fade between scales) and
// distance fade, stem culling and alpha, the proxy show/hide transitions and the post-process strength clamp.
// Pure logic: no UObjects, no world access, no allocation.

// The ecliptic grid's two scales for one camera distance
struct FSOLHologramGridLevel
{
    double MinorSpacingM = 0.0;     // Fine lines' spacing (a power of SOL::Hologram::GRID_SCALE_RATIO metres)
    double MajorSpacingM = 0.0;     // Coarse lines' spacing: MinorSpacingM x GRID_SCALE_RATIO
    double MinorAlpha = 0.0;        // Fine lines' fade in [0, 1]: 1 just after a level change, toward 0 before the next
};

// What the hologram look must do this frame
enum class ESOLHologramTransition : uint8
{
    None,   // Nothing changes
    Show,   // Hide the real bodies and rings, show the proxies, enable the post-process
    Hide,   // The reverse: real bodies back, proxies hidden, post-process off
};

namespace SOLHologramMath
{
    // Fills the grid level for a camera distance (meters): s = cameraDistanceM / GRID_MINOR_LINES_PER_VIEW,
    // level = floor(log_R(s)) with R = GRID_SCALE_RATIO, MinorSpacingM = R^level, MajorSpacingM = R^(level + 1),
    // MinorAlpha = 1 - frac(log_R(s)). Returns false (outLevel zeroed) for a non-positive or non-finite distance
    SOLTEST_API bool ComputeGridLevel(double cameraDistanceM, FSOLHologramGridLevel& outLevel);

    // Returns the LT distance field exp(-2 d^2) + 0.2 exp(-(0.2 |d|)^0.75) (d is used as |d|); 0 for non-finite d
    SOLTEST_API double DistanceField(double d);

    // Returns the grid's fade at a distance from the focus measured in major spacings: DistanceField(d) times an edge
    // taper that is 1 up to GRID_EDGE_TAPER_START x GRID_EXTENT_MAJOR_CELLS, falls linearly to 0 at
    // GRID_EXTENT_MAJOR_CELLS and stays 0 beyond; d is used as |d|; 0 for non-finite d
    SOLTEST_API double GridFade(double distanceMajorCells);

    // Returns the point snapped down to the grid: floor(x / spacing) x spacing per axis; the point unchanged for a
    // non-positive or non-finite spacing
    SOLTEST_API FVector2d SnapToGrid(const FVector2d& pointM, double spacingM);

    // Returns whether a stem is drawn: |height| >= STEM_MIN_HEIGHT_FRACTION x cameraDistanceM; false for a
    // non-positive or non-finite camera distance or a non-finite height
    SOLTEST_API bool IsStemVisible(double heightAbovePlaneM, double cameraDistanceM);

    // Returns a stem's alpha from its foot's distance to the focus in major spacings: STEM_ALPHA x
    // max(STEM_MIN_ALPHA, GridFade(d)), at most 1; STEM_ALPHA x STEM_MIN_ALPHA for non-finite d
    SOLTEST_API double StemAlpha(double footDistanceMajorCells);

    // Returns the transition from the current state (bShown) to the wanted one (map open and the look enabled):
    // Show when wanted and not shown, Hide when shown and not wanted, None otherwise
    SOLTEST_API ESOLHologramTransition NextTransition(bool bShown, bool bMapOpen, bool bEnabled);

    // Returns the post-process strength clamped into [0, POST_STRENGTH_MAX]; POST_STRENGTH_DEFAULT for non-finite
    SOLTEST_API double ClampPostStrength(double strength);
}
