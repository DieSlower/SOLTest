/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "SOLConstants.h"

#include "CoreMinimal.h"

// One text line of the flight HUD (ASOLFlightHud keeps one cached FText per line). Namespace scope since SDD 33
// section 4.2, so the HUD glossary's coverage test can name every line; the values and their order are unchanged
enum class ESOLHudLine : uint8
{
    DateWarp,
    Anchor,
    Nearest,
    SurfaceLock,
    Hints,
    MapPickHints,
    Speed,
    Frame,
    AbsoluteSpeed,
    Cap,
    Assist,
    ControlsNotice,
    OverlayProfileTag,
    GMeter,
    GWarning,
    Autopilot,
    AutopilotHint,
    AutopilotCancel,
    ChaseActivity,
    Route,
    TargetName,
    TargetDistance,
    TargetRelative,
    TargetLabel,
    RadarSelected,
    RadarRange,
    SteerHint,
    InfoTitle,
    InfoType,
    InfoDistance,
    InfoDetail,
    InfoStatus,
    InteractHint,
    RadialTitle,
    RadialReason,
    RadialItem0,
    RadialItem1,
    RadialItem2,
    RadialItem3,
    RadialItem4,
    RadialItem5,
    RadialItem6,
    RadialItem7,
    RadialItem8,
    RadialItem9,
    Count
};
static_assert(static_cast<int32>(ESOLHudLine::RadialItem9) - static_cast<int32>(ESOLHudLine::RadialItem0) + 1
    == SOL::Interact::MAX_ACTIONS, "One radial label line per action");

// The flight HUD's four frameless hologram cards (SDD 42 D15), all drawn on the menu layer. Namespace scope like
// ESOLHudLine (SDD 33), so the HUD glossary names them without the HUD's header; the values and their order are
// unchanged
enum class ESOLHudCard : uint8
{
    Info,           // The info card of the object Info was chosen for (top right)
    Target,         // The selected target's readout (top right)
    Speed,          // The speed block and the G meter above it (bottom left)
    DateAnchor,     // The sim date and warp, anchor, nearest body and surface lock (top left)
    Count
};
