/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "HudEvents/SOLHudBus.h"

//////////////////////////////////////////////////////////////////////////
// Stores the pilot snapshot of a frame, replacing any earlier one
void FSOLHudBus::PublishPilot(const FSOLHudPilotSnapshot& snapshot, const uint64 frameNumber)
{
    mPilot = snapshot;
    mPilotFrame = frameNumber;
    mHasPilot = true;
    ++mPilotPublishCount;
}

//////////////////////////////////////////////////////////////////////////
// Forgets the pilot; the snapshot returns to its defaults
void FSOLHudBus::ClearPilot()
{
    mPilot = FSOLHudPilotSnapshot();
    mHasPilot = false;
}

//////////////////////////////////////////////////////////////////////////
// Broadcasts a command to every subscriber, synchronously
void FSOLHudBus::SendCommand(const ESOLHudCommand command)
{
    ++mCommandCount;
    mCommandSignal.Broadcast(command);
}

//////////////////////////////////////////////////////////////////////////
// Stores the lane tunnel labels of a frame with the count clamped to the fixed capacity
void FSOLHudBus::PublishLaneTunnelLabels(const FSOLHudLaneTunnelLabels& labels, const uint64 frameNumber)
{
    mLaneTunnelLabels = labels;
    mLaneTunnelLabels.Count = FMath::Clamp(labels.Count, 0, static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_COUNT));
    mLaneTunnelLabels.RowCount = FMath::Clamp(labels.RowCount, 0, static_cast<int32>(SOL::NPC_TUNNEL_LABEL_MAX_ROWS));
    mLaneTunnelLabelsFrame = frameNumber;
}

//////////////////////////////////////////////////////////////////////////
// Forgets the lane tunnel labels and their frame
void FSOLHudBus::ClearLaneTunnelLabels()
{
    mLaneTunnelLabels = FSOLHudLaneTunnelLabels();
    mLaneTunnelLabelsFrame = 0;
}
