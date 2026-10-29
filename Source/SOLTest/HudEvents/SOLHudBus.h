/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlightMode.h"

#include "CoreMinimal.h"

class FSOLMapBodyOverlay;

// The pilot aids the ship pawn publishes for the HUD once per tick (one coalesced snapshot, the latest wins)
struct FSOLHudPilotSnapshot
{
    FVector2d StickOffsetPx = FVector2d::ZeroVector;    // Virtual-joystick offset, X right, Y up
    double StickRadiusPx = 0.0;                         // Virtual-joystick radius
    ESOLSteerIcon SteerIcon = ESOLSteerIcon::Free;      // Mouse-steer icon
    bool bIsSteering = false;                           // The mouse steers the ship
    bool bIsFreeLooking = false;                        // Free look is held
    bool bShowSteerHint = false;                        // Not steered yet this session: show the steer hint
};

// Commands sent to the HUD (input handled by the pawn, state owned by the HUD)
enum class ESOLHudCommand : uint8
{
    ToggleOrbitLines,
    RadarZoomIn,
    RadarZoomOut,
    RadarAuto,
};

DECLARE_MULTICAST_DELEGATE_OneParam(FSOLHudCommandSignal, ESOLHudCommand);

/**
 * The HUD event bus's state (SDD 21 decision D4), plain C++ so it is testable without a world: it breaks the
 * Ship/ <-> UI/ include cycle. The pawn publishes a pilot snapshot each tick (TG_PrePhysics) and sends commands; the
 * HUD reads the latest snapshot in DrawHUD (after the world tick) and handles commands as they are broadcast; the HUD
 * publishes its jump-map body overlay for the pawn's map picking. Nothing here allocates per frame.
 */
class SOLTEST_API FSOLHudBus
{
public:

    // Stores the pilot snapshot of a frame, replacing any earlier one (coalesced: the latest wins)
    void PublishPilot(const FSOLHudPilotSnapshot& snapshot, uint64 frameNumber);

    // Forgets the pilot (no pawn is flying: unpossessed, destroyed); the snapshot returns to its defaults
    void ClearPilot();

    // Returns true while a pilot snapshot is published and not cleared
    bool HasPilot() const { return mHasPilot; }

    // Returns the latest pilot snapshot (defaults when none)
    const FSOLHudPilotSnapshot& GetPilot() const { return mPilot; }

    // Returns the frame number of the latest published snapshot (0 when none)
    uint64 GetPilotFrame() const { return mPilotFrame; }

    // Returns how many snapshots were published since construction (verification)
    int32 GetPilotPublishCount() const { return mPilotPublishCount; }

    // Broadcasts a command to every subscriber, synchronously (UE multicast: no order between subscribers is promised;
    // the HUD is the only one)
    void SendCommand(ESOLHudCommand command);

    // Returns the command signal to subscribe to (the HUD binds in BeginPlay, unbinds in EndPlay)
    FSOLHudCommandSignal& OnCommand() { return mCommandSignal; }

    // Returns how many commands were sent since construction (verification)
    int32 GetCommandCount() const { return mCommandCount; }

    // Publishes (or with nullptr clears) the HUD's jump-map body overlay, owned by the HUD
    void SetMapBodyOverlay(const FSOLMapBodyOverlay* overlay) { mMapBodyOverlay = overlay; }

    // Returns the HUD's jump-map body overlay, or nullptr when no HUD published one
    const FSOLMapBodyOverlay* GetMapBodyOverlay() const { return mMapBodyOverlay; }

private:

    FSOLHudPilotSnapshot mPilot;                            // Latest pilot snapshot
    FSOLHudCommandSignal mCommandSignal;                    // Command subscribers
    const FSOLMapBodyOverlay* mMapBodyOverlay = nullptr;    // HUD-owned; cleared by the HUD in EndPlay
    uint64 mPilotFrame = 0;                                 // Frame of the latest snapshot
    int32 mPilotPublishCount = 0;                           // Snapshots published
    int32 mCommandCount = 0;                                // Commands sent
    bool mHasPilot = false;                                 // A snapshot is published and not cleared
};
