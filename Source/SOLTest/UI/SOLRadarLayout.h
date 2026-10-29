/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// One tracked object to plot on the radar, positioned relative to the ship (meters)
struct FSOLRadarContact
{
    FVector3d RelativePositionM = FVector3d::ZeroVector;
    FName Name;
    bool bSelected = false;
};

// A contact projected onto the radar scope (unit disc, ship-forward = +Y, ship-right = +X)
struct FSOLRadarPoint
{
    FVector2D ScreenOffsetUnit = FVector2D::ZeroVector;
    double HeightStalkUnit = 0.0;
    bool bBehind = false;
    FName Name;
    bool bSelected = false;
};

// Which side of the ship-horizontal plane (the ship's own forward/right plane) a contact is on (SDD 21 C)
enum class ESOLRadarSide : uint8
{
    Above,      // Ship-up height >= 0 (on the plane counts as above): LT Primary, solid stem, filled blip
    Below,      // Ship-up height < 0: LT Secondary, dashed stem, hollow blip
};

// How one contact is drawn on the hologram radar
struct FSOLRadarStyle
{
    ESOLRadarSide Side = ESOLRadarSide::Above;
    bool bDashedStem = false;   // The stem from the plane to the blip is dashed (below the plane)
    bool bHollowBlip = false;   // The blip is a ring instead of a filled dot (below the plane)
    bool bLarge = false;        // The selected target: a larger blip, a highlight ring and its name
};

namespace SOLRadar
{
    constexpr double MaxRangeM = 149597870700.0;   // 1 AU
    constexpr double DefaultFloorM = 10000000.0;   // 10,000 km: floor with no target selected
    constexpr double TargetedFloorM = 1000.0;      // 1 km: floor when a target is selected (close-combat detail)
    constexpr double ZoomStepFactor = 10.0;        // '-' / '=' step the manual range by this factor (one decade)
    constexpr double CollapseFraction = 0.01;

    // Hologram radar (SDD 21 phase C)
    constexpr int32 MaxDrawnContacts = 64;          // Contact budget per frame: the selected one, then the nearest
    constexpr double StemDashPx = 4.0;              // A dashed stem's dash length (before the UI scale; with the gap,
    constexpr double StemGapPx = 4.0;               // ...the glow shape's SOL::Hologram::HUD_DASH_FRACTION 0.5)
    constexpr double SonarBaseIntensity = 0.18;     // A range ring's glow between pulses
    constexpr double SonarPulseIntensity = 0.3;     // The sonar pulse's extra glow at strength 1
    constexpr double SonarRingsPerSecond = 0.5;     // Pulse crest speed outward, in range fractions per second
    constexpr double SonarSharpness = 8.0;          // Pulse shape exponent (LT minimap: |sin|^8)
    constexpr double SonarMaxStrength = 2.0;        // The effects strength is clamped to [0, this]

    // Returns the range floor for auto and manual range: TargetedFloorM if a target is selected, else DefaultFloorM
    SOLTEST_API double EffectiveFloorM(bool bHasSelectedTarget);

    // Steps a manual range one ZoomStepFactor decade (in = divide, out = multiply), clamped to [EffectiveFloorM, MaxRangeM]
    SOLTEST_API double StepManualRange(double currentRangeM, bool bZoomIn, bool bHasSelectedTarget);

    // Projects one ship-relative contact onto the scope at the given range (collapse law near center, bBehind if astern)
    SOLTEST_API FSOLRadarPoint ProjectContact(const FSOLRadarContact& contact, double rangeM);

    // A world-space offset (contact minus ship, meters) in the ship-horizontal frame: shipOrientation.UnrotateVector
    // of it (X forward, Y right, Z ship-up), so the radar plane follows the ship's pitch and roll
    SOLTEST_API FVector3d ToShipFrame(const FVector3d& contactWorldM, const FVector3d& shipWorldM,
        const FQuat4d& shipOrientation);

    // The side of the plane for a projected height (HeightStalkUnit): Above for >= 0 (and for NaN), Below for < 0
    SOLTEST_API ESOLRadarSide ClassifySide(double heightStalkUnit);

    // The draw style of a projected contact: its side (ClassifySide of HeightStalkUnit); below the plane a dashed stem
    // and a hollow blip, above a solid stem and a filled blip; bLarge = bSelected
    SOLTEST_API FSOLRadarStyle ContactStyle(const FSOLRadarPoint& point);

    // Number of dashes of a dashed stem of lengthPx: dash i spans [i (dash + gap), min(i (dash + gap) + dash, length)]
    // and exists while its start is < lengthPx; 0 for lengthPx <= 0 or dashPx <= 0 (a negative gap counts as 0)
    SOLTEST_API int32 DashCount(double lengthPx, double dashPx, double gapPx);

    // Dash index's span along the stem from the plane (0) toward the blip; false (outputs untouched) when index is
    // outside [0, DashCount)
    SOLTEST_API bool DashSegment(double lengthPx, double dashPx, double gapPx, int32 index, double& outStartPx,
        double& outEndPx);

    // A scope point (unit disc, +Y forward, +X right) on the screen: the plane drawn as an ellipse of radius radiusPx
    // squashed vertically by squash, forward up the screen: (center.X + u.X R, center.Y - u.Y R squash)
    SOLTEST_API FVector2D ScopeToScreen(const FVector2D& center, double radiusPx, double squash,
        const FVector2D& scopeUnit);

    // The top of a contact's stem above its foot on the plane: (foot.X, foot.Y - height R stalkScale), so a positive
    // height goes up the screen
    SOLTEST_API FVector2D StemTop(const FVector2D& foot, double heightStalkUnit, double radiusPx, double stalkScale);

    // A ship-frame azimuth (0 = forward, +pi/2 = right) as a scope unit vector: (sin a, cos a)
    SOLTEST_API FVector2D AzimuthToScope(double azimuthRad);

    // The azimuth of a ship-frame direction on the plane: atan2(Y, X) (0 forward, +pi/2 right); 0 with no lateral part
    SOLTEST_API double ShipAzimuth(const FVector3d& shipLocal);

    // Picks the contacts to draw within a budget: outOrder is Reset (capacity kept) and receives the indices of the
    // first min(contacts.Num(), max(budget, 0)) contacts in the order selected ones first (by index), then the rest
    // by ascending RelativePositionM length (ties by lower index); no allocation when outOrder's capacity is at least
    // contacts.Num(); returns outOrder.Num()
    SOLTEST_API int32 SelectContacts(TConstArrayView<FSOLRadarContact> contacts, int32 budget, TArray<int32>& outOrder);

    // A range ring's glow at a ring fraction (0 centre .. 1 the range ring) and time: SonarBaseIntensity +
    // SonarPulseIntensity s |sin(pi (timeS SonarRingsPerSecond - ringFraction))|^SonarSharpness, with s the effects
    // strength clamped to [0, SonarMaxStrength] (a crest travels outward; 0 strength = steady base glow)
    SOLTEST_API double SonarRingIntensity(double ringFraction, double timeS, double strength);

    // True when pointPx lies within radiusPx (inclusive) of any already drawn blip in drawnPx: the HUD skips such a
    // crowded blip (moons stacked on their planet at a long range would otherwise add up to a white blot); false for
    // radiusPx <= 0
    SOLTEST_API bool IsCrowded(TConstArrayView<FVector2D> drawnPx, const FVector2D& pointPx, double radiusPx);
}
