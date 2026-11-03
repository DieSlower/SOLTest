/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLDroppedTargetTargetable.h"

#include "Combat/SOLCombatSubsystem.h"
#include "Universe/SOLRenderPlacement.h"

//////////////////////////////////////////////////////////////////////////
// Points the proxy at a combat slot and the drop it holds now, with the name it shows
void USOLDroppedTargetTargetable::Bind(USOLCombatSubsystem* combat, const int32 slot, const uint32 spawnSerial,
    const FName name)
{
    mCombat = combat;
    mSlot = slot;
    mSpawnSerial = spawnSerial;
    mName = name;
    mLastInfo = FSOLTargetInfo();
    mLastInfo.Name = name;
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the bound slot still holds the same drop
bool USOLDroppedTargetTargetable::IsBoundTargetAlive() const
{
    const USOLCombatSubsystem* combat = mCombat.Get();
    if (combat == nullptr)
    {
        return false;
    }
    const TConstArrayView<bool> active = combat->GetTargetActive();
    const TConstArrayView<uint32> serials = combat->GetTargetSpawnSerials();
    return active.IsValidIndex(mSlot) && serials.IsValidIndex(mSlot) && active[mSlot] && serials[mSlot] == mSpawnSerial;
}

//////////////////////////////////////////////////////////////////////////
// Returns the target's snapshot in the Unreal-handed universe frame (the combat subsystem keeps ecliptic values)
FSOLTargetInfo USOLDroppedTargetTargetable::GetTargetInfo() const
{
    const USOLCombatSubsystem* combat = mCombat.Get();
    if (combat == nullptr || !IsBoundTargetAlive())
    {
        return mLastInfo;
    }
    FSOLTargetInfo info;
    info.Name = mName;
    // Read during the ship step, before the combat update moves the target: its frame body is already at this frame's
    // position, so the live position is used (the stored one is a frame old, hundreds of metres at orbital speed)
    FVector3d positionM = FVector3d::ZeroVector;
    FVector3d velocityMps = FVector3d::ZeroVector;
    combat->GetTargetLivePositionM(mSlot, positionM);
    combat->GetTargetVelocityMps(mSlot, velocityMps);
    info.PositionM = SOLRender::EclipticToUnreal(positionM);
    info.VelocityMps = SOLRender::EclipticToUnreal(velocityMps);
    info.RadiusM = combat->GetTargetRadiusM();
    mLastInfo = info;
    return info;
}

//////////////////////////////////////////////////////////////////////////
// Returns the combat frame body the drop is held relative to, or INDEX_NONE once it is gone
int32 USOLDroppedTargetTargetable::GetTargetFrameBodyIndex() const
{
    const USOLCombatSubsystem* combat = mCombat.Get();
    return combat != nullptr && IsBoundTargetAlive() ? combat->GetTargetFrameBodyIndex(mSlot) : INDEX_NONE;
}
