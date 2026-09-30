/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLTargeting.h"
#include "Targeting/SOLTargetable.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/WeakInterfacePtr.h"

#include "SOLTargetingSubsystem.generated.h"

class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;

/**
 * Owns the target candidates, the selected target and the reference-frame (M) lock (SDD 2 section 2).
 *
 * Candidates are rebuilt in place every frame by RefreshCandidates, which USOLShipSubsystem calls right before the ship
 * step: first every registry body (candidate index == body index), then every registered ISOLTargetable. The selection
 * and the lock are candidate indices, fixed up when a targetable unregisters. By default the reference frame is the
 * anchor body; ToggleFrameLock locks it to the selected target (or to the anchor when nothing is selected) until it is
 * toggled again, whatever happens to the anchor or the selection meanwhile; engaging surface-lock (SDD 4) sets the same
 * lock directly on its body with LockToBodyIndex. All values are in the Unreal-handed
 * universe frame (SOLRender::EclipticToUnreal applied, meters, m/s).
 */
UCLASS()
class SOLTEST_API USOLTargetingSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Declares the universe dependencies and sizes the candidate list for the bodies
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Drops the registered targetables and the selection
    virtual void Deinitialize() override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Rebuilds every candidate from this frame's bodies and targetables without allocating, and resolves the frame velocity
    void RefreshCandidates();

    // Returns this frame's candidates (bodies first, then registered targetables)
    TConstArrayView<FSOLTargetInfo> GetCandidates() const { return mCandidates; }

    // Adds a targetable to the candidate list (no-op if already registered)
    void RegisterTargetable(TScriptInterface<ISOLTargetable> targetable);

    // Removes a targetable; a selection or lock on it is cleared
    void UnregisterTargetable(TScriptInterface<ISOLTargetable> targetable);

    // Selects the candidate nearest the ship's forward reticle (default cone); keeps the selection if none is in the cone
    bool SelectUnderReticle(const FVector3d& shipPositionM, const FVector3d& forwardDir);

    // Selects the next farther (+1) or next nearer (-1) candidate by distance from the ship, wrapping
    void CycleTarget(const FVector3d& shipPositionM, int32 direction);

    // Clears the selected target (the frame lock, if any, stays)
    void ClearTarget();

    // Locks the reference frame to the selected target (else the anchor body), or releases an active lock
    void ToggleFrameLock();

    // Locks the reference frame to a body (candidate index == body index), replacing any lock; never toggles it off
    // (surface-lock's reference-frame auto-match, SDD 4 decision 2). An invalid body index is ignored
    void LockToBodyIndex(int32 bodyIndex);

    // Returns the selected candidate index, or INDEX_NONE
    int32 GetSelectedIndex() const { return mSelectedIndex; }

    // Returns the selected target's snapshot for this frame, or nullptr
    const FSOLTargetInfo* GetSelectedTarget() const;

    // Returns true while the reference frame is locked (M)
    bool IsFrameLocked() const { return mLockedIndex != INDEX_NONE; }

    // Returns the locked candidate index, or INDEX_NONE
    int32 GetLockedIndex() const { return mLockedIndex; }

    // Returns the active reference frame's name: the locked target, else the anchor body, else NAME_None
    FName GetFrameName() const;

    // Returns the active reference frame's velocity, resolved at the last refresh (lock, else anchor)
    const FVector3d& GetReferenceVelocityMps() const { return mReferenceVelocityMps; }

    // Returns the active reference frame's candidate index: the locked target, else the anchor body, else INDEX_NONE
    int32 GetReferenceIndex() const;

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Removes one registered targetable slot and its candidate, fixing up the selection and lock indices
    void RemoveTargetableAt(int32 slot);

    // Returns the anchor body index if it is a valid candidate, else INDEX_NONE
    int32 GetValidAnchorIndex() const;

    // Resolves the reference velocity from the lock state and the anchor body
    void ResolveReferenceVelocity();

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> Anchor;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    TArray<TWeakInterfacePtr<ISOLTargetable>> mTargetables;     // Registered non-body targets
    TArray<FSOLTargetInfo> mCandidates;                         // Bodies then targetables, rebuilt in place each frame
    FVector3d mReferenceVelocityMps = FVector3d::ZeroVector;    // Active frame velocity from the last refresh
    int32 mBodyCount = 0;                                       // Number of body candidates at the front
    int32 mSelectedIndex = INDEX_NONE;                          // Selected candidate
    int32 mLockedIndex = INDEX_NONE;                            // Candidate the reference frame is locked to
};
