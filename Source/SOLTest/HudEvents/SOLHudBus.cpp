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
