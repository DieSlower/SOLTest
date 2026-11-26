/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

struct FSOLShipControlFragment;
struct FSOLShipStateFragment;

// Pure writers of the autopilot order fields of a ship's control fragment (SDD 50 D22, section 4.9.1): every order goes
// through one of them, so the War power move's ignore-lanes flag holds for one command only and every other order
// (Move To, a route waypoint, a chase, a disengage) clears it
namespace SOLAutopilotOrder
{
    // Writes a body order (Move To, or the War power move with bWarPower): the body, the unit arrival side toward the
    // ship (+Z when degenerate), no point order, the ignore-lanes flag = bWarPower
    SOLTEST_API void WriteBodyOrder(FSOLShipControlFragment& control, int32 bodyIndex, const FVector3d& towardShipM,
        bool bWarPower);

    // Writes a route waypoint order: the body, the offset, the pass speed and capture radius floored at 0, a point
    // order, the ignore-lanes flag cleared
    SOLTEST_API void WritePointOrder(FSOLShipControlFragment& control, int32 bodyIndex, const FVector3d& offsetM,
        double passSpeedMps, double captureRadiusM);

    // Clears the order (a chase, a disengage, a reset): no body, no point order, the ignore-lanes flag cleared
    SOLTEST_API void ClearOrder(FSOLShipControlFragment& control);

    // Returns whether the HUD shows boost on: the boost switch, or a War power order (it flies boosted)
    SOLTEST_API bool IsBoostShown(bool bBoostSwitchOn, bool bWarPowerOrder);

    // Returns the AUTOPILOT marker's activity line: the War power token for a War power order, else empty
    SOLTEST_API const TCHAR* AutopilotActivityText(bool bWarPowerOrder);

    // Resets an order's transient lane state: the latched leg and mode, the War power pulse, the plan, the Sun detour
    SOLTEST_API void ResetLaneState(FSOLShipStateFragment& state);

    // Returns the HUD's Sun safety line: the SUN SAFETY OFF token while the override is on, else empty
    SOLTEST_API const TCHAR* SunSafetyLineText(bool bSunSafetyOverridden);

    // Appends the AUTOPILOT marker's VIA line for a node name ("VIA <node>")
    SOLTEST_API void AppendViaLine(FStringBuilderBase& out, FStringView nodeName);
}
