/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Flight/SOLFlightMode.h"

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns whether one input component counts as manual input (finite and above the threshold)
    bool FlightModeIsActiveComponent(const double value)
    {
        return FMath::IsFinite(value) && FMath::Abs(value) > SOLFlightMode::MANUAL_INPUT_THRESHOLD;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the positive or negative channel a component sets, or none
    uint16 FlightModeSignedChannel(const double value, const uint16 positive, const uint16 negative)
    {
        if (!FlightModeIsActiveComponent(value))
        {
            return 0;
        }
        return value > 0.0 ? positive : negative;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the start state from the assist-at-launch setting
FSOLFlightModeState SOLFlightMode::MakeInitialState(const bool bAssistOn)
{
    FSOLFlightModeState state;
    state.Mode = bAssistOn ? ESOLFlightMode::Assisted : ESOLFlightMode::Manual;
    state.LatchedChannels = 0;
    return state;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the mode is Auto
bool SOLFlightMode::IsAuto(const FSOLFlightModeState& state)
{
    return state.Mode == ESOLFlightMode::Auto;
}

//////////////////////////////////////////////////////////////////////////
// Returns the flight processor's assist flag for a mode
bool SOLFlightMode::FlightAssistFor(const ESOLFlightMode mode)
{
    return mode != ESOLFlightMode::Manual;
}

//////////////////////////////////////////////////////////////////////////
// Returns the channels the input sets
uint16 SOLFlightMode::ActiveChannels(const FSOLPilotInput& input)
{
    return FlightModeSignedChannel(input.Thrust.X, CHANNEL_THRUST_FORWARD, CHANNEL_THRUST_BACK)
        | FlightModeSignedChannel(input.Thrust.Y, CHANNEL_THRUST_RIGHT, CHANNEL_THRUST_LEFT)
        | FlightModeSignedChannel(input.Thrust.Z, CHANNEL_THRUST_UP, CHANNEL_THRUST_DOWN)
        | FlightModeSignedChannel(input.Roll, CHANNEL_ROLL_RIGHT, CHANNEL_ROLL_LEFT)
        | FlightModeSignedChannel(input.Steer.X, CHANNEL_STEER_RIGHT, CHANNEL_STEER_LEFT)
        | FlightModeSignedChannel(input.Steer.Y, CHANNEL_STEER_UP, CHANNEL_STEER_DOWN)
        | (input.bStopSpin ? CHANNEL_STOP_SPIN : 0);
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the input sets any channel
bool SOLFlightMode::IsManualInput(const FSOLPilotInput& input)
{
    return ActiveChannels(input) != 0;
}

//////////////////////////////////////////////////////////////////////////
// Toggles Manual <-> Assisted, and Auto -> Assisted
bool SOLFlightMode::ToggleAssist(FSOLFlightModeState& state, const bool bPilotHasControl)
{
    if (!bPilotHasControl)
    {
        return false;
    }
    state.Mode = state.Mode == ESOLFlightMode::Assisted ? ESOLFlightMode::Manual : ESOLFlightMode::Assisted;
    state.LatchedChannels = 0;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Enters Auto and latches the held channels
bool SOLFlightMode::EngageAuto(FSOLFlightModeState& state, const FSOLPilotInput& heldInput)
{
    const bool bChanged = state.Mode != ESOLFlightMode::Auto;
    state.Mode = ESOLFlightMode::Auto;
    state.LatchedChannels = ActiveChannels(heldInput);
    return bChanged;
}

//////////////////////////////////////////////////////////////////////////
// In Auto, adds the held channels to the latch
void SOLFlightMode::RelatchInput(FSOLFlightModeState& state, const FSOLPilotInput& heldInput)
{
    if (state.Mode == ESOLFlightMode::Auto)
    {
        state.LatchedChannels |= ActiveChannels(heldInput);
    }
}

//////////////////////////////////////////////////////////////////////////
// Leaves Auto for Assisted
bool SOLFlightMode::DisengageAuto(FSOLFlightModeState& state)
{
    if (state.Mode != ESOLFlightMode::Auto)
    {
        return false;
    }
    state.Mode = ESOLFlightMode::Assisted;
    state.LatchedChannels = 0;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Autopilot arrival: hands back to Assisted
bool SOLFlightMode::NotifyAutopilotArrived(FSOLFlightModeState& state)
{
    return DisengageAuto(state);
}

//////////////////////////////////////////////////////////////////////////
// Applies the cancel-on-manual-input rule with the input latch
bool SOLFlightMode::ApplyPilotInput(FSOLFlightModeState& state, const FSOLPilotInput& input,
    const bool bPilotHasControl)
{
    if (state.Mode != ESOLFlightMode::Auto)
    {
        return false;
    }
    // Each latched channel clears the first frame it is released; any channel not latched cancels at once
    const uint16 active = ActiveChannels(input);
    state.LatchedChannels &= active;
    if (bPilotHasControl && (active & ~state.LatchedChannels) != 0)
    {
        state.Mode = ESOLFlightMode::Assisted;
        state.LatchedChannels = 0;
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns a steering gate with the given toggle state
FSOLSteerGateState SOLFlightMode::MakeSteerGate(const bool bToggleOn)
{
    FSOLSteerGateState gate;
    gate.bToggleOn = bToggleOn;
    gate.bHoldDown = false;
    gate.bWasActive = false;
    return gate;
}

//////////////////////////////////////////////////////////////////////////
// Sets whether the steer-hold key is down
void SOLFlightMode::SetSteerHold(FSOLSteerGateState& gate, const bool bDown)
{
    gate.bHoldDown = bDown;
}

//////////////////////////////////////////////////////////////////////////
// Flips the steering toggle latch
void SOLFlightMode::ToggleSteer(FSOLSteerGateState& gate)
{
    gate.bToggleOn = !gate.bToggleOn;
}

//////////////////////////////////////////////////////////////////////////
// Updates the gate and reports every edge (off-to-on and on-to-off) as a recenter
FSOLSteerGateUpdate SOLFlightMode::UpdateSteerGate(FSOLSteerGateState& gate, const bool bSuspended)
{
    FSOLSteerGateUpdate update;
    update.bActive = (gate.bToggleOn || gate.bHoldDown) && !bSuspended;
    update.bRecenter = update.bActive != gate.bWasActive;
    gate.bWasActive = update.bActive;
    return update;
}

//////////////////////////////////////////////////////////////////////////
// Returns the stick command when steering is active, else zero
FVector2d SOLFlightMode::GateSteer(const FVector2d& stickCommand, const bool bActive)
{
    return bActive ? stickCommand : FVector2d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Returns the steer icon for the gate
ESOLSteerIcon SOLFlightMode::SteerIconFor(const FSOLSteerGateState& gate)
{
    if (gate.bToggleOn)
    {
        return ESOLSteerIcon::Locked;
    }
    return gate.bHoldDown ? ESOLSteerIcon::Held : ESOLSteerIcon::Free;
}

//////////////////////////////////////////////////////////////////////////
// Flips boost with pilot control
bool SOLFlightMode::ToggleBoost(FSOLBoostToggle& boost, const bool bPilotHasControl)
{
    if (bPilotHasControl)
    {
        boost.bOn = !boost.bOn;
    }
    return boost.bOn;
}

//////////////////////////////////////////////////////////////////////////
// Turns boost off while the pilot has no control
void SOLFlightMode::UpdateBoost(FSOLBoostToggle& boost, const bool bPilotHasControl)
{
    if (!bPilotHasControl)
    {
        boost.bOn = false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Turns boost off
void SOLFlightMode::ResetBoost(FSOLBoostToggle& boost)
{
    boost.bOn = false;
}
