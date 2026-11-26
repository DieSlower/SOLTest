/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLAutopilotOrder.h"

#include "Menu/SOLMenuText.h"
#include "Ship/SOLShipFragments.h"

//////////////////////////////////////////////////////////////////////////
// Writes a body order with its arrival side and the per-command ignore-lanes flag
void SOLAutopilotOrder::WriteBodyOrder(FSOLShipControlFragment& control, const int32 bodyIndex,
    const FVector3d& towardShipM, const bool bWarPower)
{
    control.AutopilotBodyIndex = bodyIndex;
    control.AutopilotOffsetDir = towardShipM.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d(0.0, 0.0, 1.0));
    control.bAutopilotPoint = false;
    control.bAutopilotIgnoreLanes = bWarPower;
}

//////////////////////////////////////////////////////////////////////////
// Writes a route waypoint order; it obeys the lanes
void SOLAutopilotOrder::WritePointOrder(FSOLShipControlFragment& control, const int32 bodyIndex,
    const FVector3d& offsetM, const double passSpeedMps, const double captureRadiusM)
{
    control.AutopilotBodyIndex = bodyIndex;
    control.AutopilotPointOffsetM = offsetM;
    control.AutopilotPassSpeedMps = FMath::Max(passSpeedMps, 0.0);
    control.AutopilotCaptureRadiusM = FMath::Max(captureRadiusM, 0.0);
    control.bAutopilotPoint = true;
    control.bAutopilotIgnoreLanes = false;
}

//////////////////////////////////////////////////////////////////////////
// Clears the order and the ignore-lanes flag
void SOLAutopilotOrder::ClearOrder(FSOLShipControlFragment& control)
{
    control.AutopilotBodyIndex = INDEX_NONE;
    control.bAutopilotPoint = false;
    control.bAutopilotIgnoreLanes = false;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the HUD shows boost on (the switch, or a War power order)
bool SOLAutopilotOrder::IsBoostShown(const bool bBoostSwitchOn, const bool bWarPowerOrder)
{
    return bBoostSwitchOn || bWarPowerOrder;
}

//////////////////////////////////////////////////////////////////////////
// Returns the AUTOPILOT marker's activity line for an order
const TCHAR* SOLAutopilotOrder::AutopilotActivityText(const bool bWarPowerOrder)
{
    return bWarPowerOrder ? SOLMenuText::AUTOPILOT_WAR_POWER_ACTIVITY : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Resets an order's transient lane state: the latched leg and mode, the War power pulse, the plan, the Sun detour and
// its notices
void SOLAutopilotOrder::ResetLaneState(FSOLShipStateFragment& state)
{
    state.LaneLeg = FSOLLaneAutoLeg();
    state.LaneMode = ESOLLaneAutoMode::None;
    state.WarPowerPulse = FSOLWarPowerPulseState();
    state.LanePlan = FSOLLaneRoutePlan();
    state.bSunDetour = false;
    state.bSunStoppedAtZone = false;
    state.bLaneLeftForSun = false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the HUD's Sun safety line: the SUN SAFETY OFF token while the override is on, else empty
const TCHAR* SOLAutopilotOrder::SunSafetyLineText(const bool bSunSafetyOverridden)
{
    return bSunSafetyOverridden ? SOLMenuText::AUTOPILOT_SUN_SAFETY_OFF : TEXT("");
}

//////////////////////////////////////////////////////////////////////////
// Appends the AUTOPILOT marker's VIA line for a node name ("VIA <node>")
void SOLAutopilotOrder::AppendViaLine(FStringBuilderBase& out, const FStringView nodeName)
{
    out << SOLMenuText::AUTOPILOT_VIA_PREFIX << nodeName;
}
