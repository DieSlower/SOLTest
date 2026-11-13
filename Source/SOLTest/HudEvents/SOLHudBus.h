/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlightMode.h"
#include "SOLConstants.h"

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
    ShowOverlayProfileTag,      // The profile cycle key was pressed: show the active profile's tag (SDD 34 section 8)
};

DECLARE_MULTICAST_DELEGATE_OneParam(FSOLHudCommandSignal, ESOLHudCommand);

// One lane tunnel's wall label as the NPC tunnel publishes it for the HUD (SDD 49 section 4.9, Q4): plain numbers, the
// label's target alpha and its two anchor candidates; the HUD eases the alpha, formats the text and places the box (so
// the HUD needs no NPC code)
struct FSOLHudLaneTunnelLabel
{
    int32 Lane = INDEX_NONE;                            // Lane (group) index; the HUD looks up its body names
    double Strength = 0.0;                              // The tunnel fade F (the label's ordering and alpha)
    bool bStrongest = false;                            // The strongest shown tunnel's label (the brightest)
    double Alpha = 0.0;                                 // Target alpha, SOLNpcTunnel::LaneLabelAlpha (the tunnel
                                                        // publishes only labels with IsLabelPlaced)
    double DistanceAM = 0.0;                            // Straight-line distance from the ship to end ring A
    double DistanceBM = 0.0;                            // ...and to end ring B
    int32 ShipCount = 0;                                // Ships in the lane
    int32 BodyA = INDEX_NONE;                           // Registry index of body A (the label's first name)
    int32 BodyB = INDEX_NONE;                           // Registry index of body B
    FVector3d TopAnchorM = FVector3d::ZeroVector;       // 12 o'clock anchor relative to the viewpoint (ecliptic m)
    FVector3d BottomAnchorM = FVector3d::ZeroVector;    // 6 o'clock anchor relative to the viewpoint (ecliptic m)
};

// The lane tunnel labels of a frame, strongest first (fixed capacity, no allocation)
struct FSOLHudLaneTunnelLabels
{
    int32 Count = 0;                                                        // Labels in use (0 = none)
    FSOLHudLaneTunnelLabel Labels[SOL::NPC_TUNNEL_LABEL_MAX_COUNT];         // Entries 0 .. Count - 1
    int32 RowCount = 0;                                                     // Dot-row segments in use
    FVector3d RowStartM[SOL::NPC_TUNNEL_LABEL_MAX_ROWS] = {};                 // Every shown tunnel's dot rows (the
    FVector3d RowEndM[SOL::NPC_TUNNEL_LABEL_MAX_ROWS] = {};                   // labels keep clear of them), relative
                                                                            // to the viewpoint (ecliptic m)
    double RowDotDiameterM[SOL::NPC_TUNNEL_LABEL_MAX_ROWS] = {};            // Each row's world dot diameter (m)
};

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

    // Stores the lane tunnel labels of a frame, replacing any earlier ones (the count clamped to
    // [0, NPC_TUNNEL_LABEL_MAX_COUNT], the row count to [0, NPC_TUNNEL_LABEL_MAX_ROWS]) with the frame number they
    // were made for
    void PublishLaneTunnelLabels(const FSOLHudLaneTunnelLabels& labels, uint64 frameNumber);

    // Forgets the lane tunnel labels (no tunnel shows, the NPC visuals ended); the frame returns to 0
    void ClearLaneTunnelLabels();

    // Returns the latest lane tunnel labels (count 0 when none)
    const FSOLHudLaneTunnelLabels& GetLaneTunnelLabels() const { return mLaneTunnelLabels; }

    // Returns the frame number of the latest lane tunnel labels (0 when none); the HUD drops a snapshot older than
    // the frame it draws, so labels never outlive the tunnels that made them
    uint64 GetLaneTunnelLabelsFrame() const { return mLaneTunnelLabelsFrame; }

private:

    FSOLHudPilotSnapshot mPilot;                            // Latest pilot snapshot
    FSOLHudLaneTunnelLabels mLaneTunnelLabels;              // Latest lane tunnel labels (SDD 49)
    FSOLHudCommandSignal mCommandSignal;                    // Command subscribers
    const FSOLMapBodyOverlay* mMapBodyOverlay = nullptr;    // HUD-owned; cleared by the HUD in EndPlay
    uint64 mPilotFrame = 0;                                 // Frame of the latest snapshot
    uint64 mLaneTunnelLabelsFrame = 0;                      // Frame of the latest lane tunnel labels
    int32 mPilotPublishCount = 0;                           // Snapshots published
    int32 mCommandCount = 0;                                // Commands sent
    bool mHasPilot = false;                                 // A snapshot is published and not cleared
};
