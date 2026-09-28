/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Targeting/SOLTargetingSubsystem.h"

#include "Game/SOLGameMode.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Engine/World.h"

//////////////////////////////////////////////////////////////////////////
// Declares the universe dependencies and sizes the candidate list for the bodies
void USOLTargetingSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Anchor = collection.InitializeDependency<USOLAnchorSubsystem>();
    Super::Initialize(collection);

    if (BodyRegistry == nullptr || Anchor == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("TargetingSubsystem %s: universe subsystems missing"), *GetName());
        return;
    }

    // Room for every body plus the expected targetables, so the per-frame rebuild never grows the arrays
    mBodyCount = BodyRegistry->GetUnrealFrameCache().Num();
    mTargetables.Reserve(SOL::TARGETING_RESERVED_TARGETABLES);
    mCandidates.Reserve(mBodyCount + SOL::TARGETING_RESERVED_TARGETABLES);
    RefreshCandidates();
}

//////////////////////////////////////////////////////////////////////////
// Drops the registered targetables and the selection
void USOLTargetingSubsystem::Deinitialize()
{
    mTargetables.Empty();
    mCandidates.Empty();
    mSelectedIndex = INDEX_NONE;
    mLockedIndex = INDEX_NONE;
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLTargetingSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLTargetingSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Rebuilds every candidate from this frame's bodies and targetables without allocating, and resolves the frame velocity
void USOLTargetingSubsystem::RefreshCandidates()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLTargetingSubsystem::RefreshCandidates);
    if (BodyRegistry == nullptr)
    {
        return;
    }

    // Drop targetables whose object is gone, fixing up the selection and lock like an explicit unregister
    for (int32 slot = mTargetables.Num() - 1; slot >= 0; --slot)
    {
        if (!mTargetables[slot].IsValid())
        {
            RemoveTargetableAt(slot);
        }
    }

    // Bodies first (candidate index == body index), from the Unreal-handed cache refreshed with the registry
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    mBodyCount = cache.Num();
    mCandidates.Reset();
    for (int32 body = 0; body < mBodyCount; ++body)
    {
        FSOLTargetInfo& info = mCandidates.AddDefaulted_GetRef();
        info.Name = registry.GetName(body);
        info.PositionM = cache.PositionsM[body];
        info.VelocityMps = cache.VelocitiesMps[body];
        info.RadiusM = cache.RadiiM[body];
    }

    // Then every registered targetable, in registration order
    for (const TWeakInterfacePtr<ISOLTargetable>& targetable : mTargetables)
    {
        mCandidates.Add(targetable->GetTargetInfo());
    }
    ResolveReferenceVelocity();
}

//////////////////////////////////////////////////////////////////////////
// Adds a targetable to the candidate list (no-op if already registered)
void USOLTargetingSubsystem::RegisterTargetable(const TScriptInterface<ISOLTargetable> targetable)
{
    if (targetable.GetObject() == nullptr || targetable.GetInterface() == nullptr)
    {
        return;
    }
    const TWeakInterfacePtr<ISOLTargetable> weakTargetable(targetable);
    if (!mTargetables.Contains(weakTargetable))
    {
        mTargetables.Add(weakTargetable);
    }
}

//////////////////////////////////////////////////////////////////////////
// Removes a targetable; a selection or lock on it is cleared
void USOLTargetingSubsystem::UnregisterTargetable(const TScriptInterface<ISOLTargetable> targetable)
{
    const UObject* object = targetable.GetObject();
    for (int32 slot = 0; object != nullptr && slot < mTargetables.Num(); ++slot)
    {
        if (mTargetables[slot].GetObject() == object)
        {
            RemoveTargetableAt(slot);
            return;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Removes one registered targetable slot and its candidate, fixing up the selection and lock indices
void USOLTargetingSubsystem::RemoveTargetableAt(const int32 slot)
{
    mTargetables.RemoveAt(slot, EAllowShrinking::No);

    // Candidates after the removed one move down by one; a selection or lock on the removed one ends
    const int32 candidate = mBodyCount + slot;

    // Shifts or clears one stored candidate index for the removal
    const auto fixUp = [candidate](int32& index)
    {
        if (index == candidate)
        {
            index = INDEX_NONE;
        }
        else if (index > candidate)
        {
            --index;
        }
    };
    fixUp(mSelectedIndex);
    fixUp(mLockedIndex);
    if (mCandidates.IsValidIndex(candidate))
    {
        mCandidates.RemoveAt(candidate, EAllowShrinking::No);
    }
}

//////////////////////////////////////////////////////////////////////////
// Selects the candidate nearest the ship's forward reticle (default cone); keeps the selection if none is in the cone
bool USOLTargetingSubsystem::SelectUnderReticle(const FVector3d& shipPositionM, const FVector3d& forwardDir)
{
    const int32 picked = SOLTargeting::PickUnderReticle(mCandidates, shipPositionM, forwardDir,
        SOLTargeting::DefaultConeHalfAngleRad);
    if (picked == INDEX_NONE)
    {
        return false;
    }
    mSelectedIndex = picked;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Selects the next farther (+1) or next nearer (-1) candidate by distance from the ship, wrapping
void USOLTargetingSubsystem::CycleTarget(const FVector3d& shipPositionM, const int32 direction)
{
    mSelectedIndex = SOLTargeting::Cycle(mCandidates, shipPositionM, mSelectedIndex, direction);
}

//////////////////////////////////////////////////////////////////////////
// Clears the selected target (the frame lock, if any, stays)
void USOLTargetingSubsystem::ClearTarget()
{
    mSelectedIndex = INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Locks the reference frame to the selected target (else the anchor body), or releases an active lock
void USOLTargetingSubsystem::ToggleFrameLock()
{
    if (mLockedIndex != INDEX_NONE)
    {
        mLockedIndex = INDEX_NONE;
    }
    else
    {
        mLockedIndex = mCandidates.IsValidIndex(mSelectedIndex) ? mSelectedIndex : GetValidAnchorIndex();
    }
    ResolveReferenceVelocity();
    UE_LOG(LogSOL, Log, TEXT("Targeting %s: reference frame %s %s"), *GetName(),
        mLockedIndex == INDEX_NONE ? TEXT("follows the anchor") : TEXT("locked to"), *GetFrameName().ToString());
}

//////////////////////////////////////////////////////////////////////////
// Returns the selected target's snapshot for this frame, or nullptr
const FSOLTargetInfo* USOLTargetingSubsystem::GetSelectedTarget() const
{
    return mCandidates.IsValidIndex(mSelectedIndex) ? &mCandidates[mSelectedIndex] : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns the active reference frame's name: the locked target, else the anchor body, else NAME_None
FName USOLTargetingSubsystem::GetFrameName() const
{
    const int32 frameIndex = GetReferenceIndex();
    return frameIndex == INDEX_NONE ? NAME_None : mCandidates[frameIndex].Name;
}

//////////////////////////////////////////////////////////////////////////
// Returns the active reference frame's candidate index: the locked target, else the anchor body, else INDEX_NONE
int32 USOLTargetingSubsystem::GetReferenceIndex() const
{
    return mCandidates.IsValidIndex(mLockedIndex) ? mLockedIndex : GetValidAnchorIndex();
}

//////////////////////////////////////////////////////////////////////////
// Returns the anchor body index if it is a valid candidate, else INDEX_NONE
int32 USOLTargetingSubsystem::GetValidAnchorIndex() const
{
    const int32 anchor = Anchor != nullptr ? Anchor->GetAnchorIndex() : INDEX_NONE;
    return anchor >= 0 && anchor < mBodyCount && anchor < mCandidates.Num() ? anchor : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Resolves the reference velocity from the lock state and the anchor body
void USOLTargetingSubsystem::ResolveReferenceVelocity()
{
    const int32 anchor = GetValidAnchorIndex();
    const FVector3d anchorVelocityMps = anchor == INDEX_NONE ? FVector3d::ZeroVector : mCandidates[anchor].VelocityMps;
    const FSOLTargetInfo* locked = mCandidates.IsValidIndex(mLockedIndex) ? &mCandidates[mLockedIndex] : nullptr;
    mReferenceVelocityMps = SOLTargeting::ResolveReferenceVelocity(mLockedIndex != INDEX_NONE, locked,
        anchorVelocityMps);
}
