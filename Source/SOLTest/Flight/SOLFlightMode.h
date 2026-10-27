/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"

// Issue #19 flight modes (SDD 19 section 3.1): the manual / assisted / auto state machine with its cancel-on-manual-
// input rule and input latch, the mouse steering gate (hold OR toggle, suspension, recenter edge), the steer icon and
// the boost toggle. Pure logic: no UObjects, no world access, no allocation, no global mutable state.

// The player's flight mode
enum class ESOLFlightMode : uint8
{
    Manual,
    Assisted,
    Auto,
};

// The HUD's mouse-steer state icon
enum class ESOLSteerIcon : uint8
{
    Free,
    Held,
    Locked,
};

// The player's flight mode and the auto input latch
struct FSOLFlightModeState
{
    ESOLFlightMode Mode = ESOLFlightMode::Assisted;
    uint16 LatchedChannels = 0;     // Auto only: channels held at engage (or re-latch), ignored until each is released
};

// This frame's manual pilot input, after the steering gate
struct FSOLPilotInput
{
    FVector3d Thrust = FVector3d::ZeroVector;   // Local fractions: X forward, Y right, Z up
    double Roll = 0.0;                          // [-1, 1]
    FVector2d Steer = FVector2d::ZeroVector;    // Gated stick command (X yaw, Y pitch)
    bool bStopSpin = false;
};

// The mouse steering gate's inputs and its previous result
struct FSOLSteerGateState
{
    bool bToggleOn = false;         // SteerToggle latch
    bool bHoldDown = false;         // SteerHold held
    bool bWasActive = false;        // Gate result of the previous update (edge detection)
};

// One steering gate update's result
struct FSOLSteerGateUpdate
{
    bool bActive = false;           // The mouse steers this frame (cursor captured)
    bool bRecenter = false;         // Off-to-on edge: recenter the stick now
};

// The boost toggle (Tab)
struct FSOLBoostToggle
{
    bool bOn = false;
};

namespace SOLFlightMode
{
    // A component counts as manual input only when its magnitude is strictly greater than this
    inline constexpr double MANUAL_INPUT_THRESHOLD = 1.0e-3;

    // Signed input channels (bit flags); a component sets one only when finite and strictly beyond the threshold
    inline constexpr uint16 CHANNEL_THRUST_FORWARD = 1 << 0;   // Thrust.X > threshold
    inline constexpr uint16 CHANNEL_THRUST_BACK = 1 << 1;      // Thrust.X < -threshold
    inline constexpr uint16 CHANNEL_THRUST_RIGHT = 1 << 2;     // Thrust.Y > threshold
    inline constexpr uint16 CHANNEL_THRUST_LEFT = 1 << 3;      // Thrust.Y < -threshold
    inline constexpr uint16 CHANNEL_THRUST_UP = 1 << 4;        // Thrust.Z > threshold
    inline constexpr uint16 CHANNEL_THRUST_DOWN = 1 << 5;      // Thrust.Z < -threshold
    inline constexpr uint16 CHANNEL_ROLL_RIGHT = 1 << 6;       // Roll > threshold
    inline constexpr uint16 CHANNEL_ROLL_LEFT = 1 << 7;        // Roll < -threshold
    inline constexpr uint16 CHANNEL_STEER_RIGHT = 1 << 8;      // Steer.X > threshold
    inline constexpr uint16 CHANNEL_STEER_LEFT = 1 << 9;       // Steer.X < -threshold
    inline constexpr uint16 CHANNEL_STEER_UP = 1 << 10;        // Steer.Y > threshold
    inline constexpr uint16 CHANNEL_STEER_DOWN = 1 << 11;      // Steer.Y < -threshold
    inline constexpr uint16 CHANNEL_STOP_SPIN = 1 << 12;       // bStopSpin

    // Returns the channels the input sets (NaN and infinite components set none)
    SOLTEST_API uint16 ActiveChannels(const FSOLPilotInput& input);

    // Returns the start state: Assisted when assist is on at launch, else Manual; latch off
    SOLTEST_API FSOLFlightModeState MakeInitialState(bool bAssistOn);

    // Returns whether the mode is Auto
    SOLTEST_API bool IsAuto(const FSOLFlightModeState& state);

    // Returns the flight processor's assist flag for a mode (false only for Manual)
    SOLTEST_API bool FlightAssistFor(ESOLFlightMode mode);

    // Returns whether the input sets any channel
    SOLTEST_API bool IsManualInput(const FSOLPilotInput& input);

    // Toggles Manual <-> Assisted, and Auto -> Assisted (latch cleared); returns false without pilot control
    SOLTEST_API bool ToggleAssist(FSOLFlightModeState& state, bool bPilotHasControl);

    // Enters Auto and latches exactly the held channels (re-engaging replaces the latch); returns whether the mode
    // changed
    SOLTEST_API bool EngageAuto(FSOLFlightModeState& state, const FSOLPilotInput& heldInput);

    // In Auto, adds the held channels to the latch (input held when the map, a menu or the F3 panel closes); outside
    // Auto, does nothing
    SOLTEST_API void RelatchInput(FSOLFlightModeState& state, const FSOLPilotInput& heldInput);

    // Leaves Auto for Assisted (latch cleared); returns false (no change) outside Auto
    SOLTEST_API bool DisengageAuto(FSOLFlightModeState& state);

    // Autopilot arrival: same as DisengageAuto
    SOLTEST_API bool NotifyAutopilotArrived(FSOLFlightModeState& state);

    // Applies the cancel rule: in Auto, each released latched channel clears; any unlatched channel with control
    // cancels to Assisted at once. Returns whether the mode changed
    SOLTEST_API bool ApplyPilotInput(FSOLFlightModeState& state, const FSOLPilotInput& input, bool bPilotHasControl);

    // Returns a steering gate with the toggle as given, hold up and no previous activity (the first active update
    // recenters)
    SOLTEST_API FSOLSteerGateState MakeSteerGate(bool bToggleOn);

    // Sets whether the steer-hold key is down
    SOLTEST_API void SetSteerHold(FSOLSteerGateState& gate, bool bDown);

    // Flips the steering toggle latch
    SOLTEST_API void ToggleSteer(FSOLSteerGateState& gate);

    // Updates the gate: active = (toggle or hold) and not suspended; recenter on the off-to-on edge
    SOLTEST_API FSOLSteerGateUpdate UpdateSteerGate(FSOLSteerGateState& gate, bool bSuspended);

    // Returns the stick command when steering is active, else zero
    SOLTEST_API FVector2d GateSteer(const FVector2d& stickCommand, bool bActive);

    // Returns the steer icon: Locked with the toggle on, else Held while held, else Free (suspension ignored)
    SOLTEST_API ESOLSteerIcon SteerIconFor(const FSOLSteerGateState& gate);

    // Flips boost with pilot control (unchanged without); returns the resulting state
    SOLTEST_API bool ToggleBoost(FSOLBoostToggle& boost, bool bPilotHasControl);

    // Turns boost off while the pilot has no control (it never turns itself back on)
    SOLTEST_API void UpdateBoost(FSOLBoostToggle& boost, bool bPilotHasControl);

    // Turns boost off (jump arrival, respawn)
    SOLTEST_API void ResetBoost(FSOLBoostToggle& boost);
}
