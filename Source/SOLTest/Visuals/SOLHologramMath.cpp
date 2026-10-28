/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLHologramMath.h"

#include "SOLConstants.h"

//////////////////////////////////////////////////////////////////////////
// Fills the grid level (two scales and the fine lines' fade) for a camera distance
bool SOLHologramMath::ComputeGridLevel(const double cameraDistanceM, FSOLHologramGridLevel& outLevel)
{
    outLevel = FSOLHologramGridLevel();
    if (!FMath::IsFinite(cameraDistanceM) || cameraDistanceM <= 0.0)
    {
        return false;
    }
    const double ratio = SOL::Hologram::GRID_SCALE_RATIO;
    const double logScale = FMath::Loge(cameraDistanceM / SOL::Hologram::GRID_MINOR_LINES_PER_VIEW)
        / FMath::Loge(ratio);
    double level = FMath::FloorToDouble(logScale);
    double fraction = logScale - level;

    // Guard the floor against rounding at exact powers (log ratios are not exact in floating point)
    const double nearest = FMath::RoundToDouble(logScale);
    if (FMath::Abs(logScale - nearest) < 1.0e-12)
    {
        level = nearest;
        fraction = 0.0;
    }
    outLevel.MinorSpacingM = FMath::Pow(ratio, level);
    outLevel.MajorSpacingM = outLevel.MinorSpacingM * ratio;
    outLevel.MinorAlpha = FMath::Clamp(1.0 - fraction, 0.0, 1.0);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the LT distance field exp(-2 d^2) + 0.2 exp(-(0.2 |d|)^0.75)
double SOLHologramMath::DistanceField(const double d)
{
    if (!FMath::IsFinite(d))
    {
        return 0.0;
    }
    const double distance = FMath::Abs(d);
    return FMath::Exp(-2.0 * distance * distance) + 0.2 * FMath::Exp(-FMath::Pow(0.2 * distance, 0.75));
}

//////////////////////////////////////////////////////////////////////////
// Returns the grid's fade at a distance from the focus in major spacings (distance field times the edge taper)
double SOLHologramMath::GridFade(const double distanceMajorCells)
{
    if (!FMath::IsFinite(distanceMajorCells))
    {
        return 0.0;
    }
    const double distance = FMath::Abs(distanceMajorCells);
    const double extent = SOL::Hologram::GRID_EXTENT_MAJOR_CELLS;
    const double taperStart = SOL::Hologram::GRID_EDGE_TAPER_START * extent;
    if (distance >= extent)
    {
        return 0.0;
    }
    const double taper = distance <= taperStart ? 1.0 : (extent - distance) / (extent - taperStart);
    return DistanceField(distance) * taper;
}

//////////////////////////////////////////////////////////////////////////
// Returns the point snapped down to the grid per axis
FVector2d SOLHologramMath::SnapToGrid(const FVector2d& pointM, const double spacingM)
{
    if (!FMath::IsFinite(spacingM) || spacingM <= 0.0)
    {
        return pointM;
    }
    return FVector2d(FMath::FloorToDouble(pointM.X / spacingM) * spacingM,
        FMath::FloorToDouble(pointM.Y / spacingM) * spacingM);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a stem is tall enough, relative to the camera distance, to be drawn
bool SOLHologramMath::IsStemVisible(const double heightAbovePlaneM, const double cameraDistanceM)
{
    if (!FMath::IsFinite(heightAbovePlaneM) || !FMath::IsFinite(cameraDistanceM) || cameraDistanceM <= 0.0)
    {
        return false;
    }
    return FMath::Abs(heightAbovePlaneM) >= SOL::Hologram::STEM_MIN_HEIGHT_FRACTION * cameraDistanceM;
}

//////////////////////////////////////////////////////////////////////////
// Returns a stem's alpha from its foot's distance to the focus in major spacings
double SOLHologramMath::StemAlpha(const double footDistanceMajorCells)
{
    const double fade = FMath::IsFinite(footDistanceMajorCells) ? GridFade(footDistanceMajorCells) : 0.0;
    return FMath::Min(1.0, SOL::Hologram::STEM_ALPHA * FMath::Max(SOL::Hologram::STEM_MIN_ALPHA, fade));
}

//////////////////////////////////////////////////////////////////////////
// Returns the transition from the current state to the wanted one (map open and the look enabled)
ESOLHologramTransition SOLHologramMath::NextTransition(const bool bShown, const bool bMapOpen, const bool bEnabled)
{
    const bool bWanted = bMapOpen && bEnabled;
    if (bWanted && !bShown)
    {
        return ESOLHologramTransition::Show;
    }
    if (!bWanted && bShown)
    {
        return ESOLHologramTransition::Hide;
    }
    return ESOLHologramTransition::None;
}

//////////////////////////////////////////////////////////////////////////
// Returns the post-process strength clamped into [0, POST_STRENGTH_MAX] (the default for non-finite input)
double SOLHologramMath::ClampPostStrength(const double strength)
{
    if (!FMath::IsFinite(strength))
    {
        return SOL::Hologram::POST_STRENGTH_DEFAULT;
    }
    return FMath::Clamp(strength, 0.0, SOL::Hologram::POST_STRENGTH_MAX);
}
