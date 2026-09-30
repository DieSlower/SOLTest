/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapPicking.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLJumpSubsystem.generated.h"

class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLShipSubsystem;
class USOLTargetingSubsystem;

/**
 * Jump execution (SDD 3 Appendix I, sub-part 2e): owns the warp sequence that follows Enter on the jump map. StartJump
 * copies the map's pick (the reference, fixed from then on: a body index or the ship, and the locked offsets) and
 * starts a WARP_DURATION_S clock on real time, advanced at the end of every ship step (USOLShipSubsystem's
 * OnShipsStepped, inside the anchor subsystem's frame update, so a paused world pauses the warp too). The ship pawn
 * reads the clock to pulse its camera FOV and blend in the warp post-process, and the flight HUD reads it to draw the
 * radial streaks in place of the HUD.
 *
 * When the clock runs out, the destination is composed again from the reference's CURRENT position (it moved during
 * the warp), the arrival velocity is the reference body's current velocity (for a ship / empty-space reference, the
 * body a fresh anchor selection picks at the destination, the same rule the anchor uses), the selected target and the
 * M frame lock are cleared (they refer to the old location), and the ship is teleported through
 * USOLShipSubsystem::SetState with its orientation unchanged. SetState moves the observer through
 * USOLAnchorSubsystem::SetObserverPositionM, which re-picks the anchor without hysteresis and force-snaps the render
 * origin at once; the frame's anchor update and draw follow, so the ship is never drawn far from the origin. A frame
 * whose universe update ran without a ship step (the ship became invalid) aborts a running sequence.
 */
UCLASS()
class SOLTEST_API USOLJumpSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Resolves the anchor, registry and ship subsystems and subscribes to the end of the ship step
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Unsubscribes and drops any running sequence
    virtual void Deinitialize() override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Starts the warp sequence to a pick with at least the planar offset locked; false (nothing starts) otherwise or
    // while a sequence already runs
    bool StartJump(const FSOLMapPickState& pick);

    // Returns true while the warp sequence runs
    bool IsWarping() const { return mIsWarping; }

    // Returns the real seconds since the running sequence started (0 when none runs)
    double GetElapsedS() const { return mElapsedS; }

    // Returns the chase camera FOV for this point of the sequence, pulsing from baseFovDeg to WARP_PEAK_FOV_DEG
    double ComputeFovDeg(double baseFovDeg) const;

    // Returns the radial-streak intensity for this point of the sequence (0 when none runs)
    double ComputeStreakIntensity() const;

    // Returns the number of completed jumps (verification)
    int32 GetCompletedJumpCount() const { return mCompletedJumpCount; }

    // Returns the pick the running or last sequence was started with (verification)
    const FSOLMapPickState& GetJumpPick() const { return mPick; }

    // Returns the reference's universe position at the last arrival (meters, ecliptic; verification)
    const FVector3d& GetLastArrivalReferenceM() const { return mLastArrivalReferenceM; }

    // Returns the ship's universe position set at the last arrival (meters, ecliptic; verification)
    const FVector3d& GetLastArrivalPositionM() const { return mLastArrivalPositionM; }

    // Returns the body whose velocity the last arrival matched (verification)
    int32 GetLastArrivalVelocityBodyIndex() const { return mLastArrivalVelocityBodyIndex; }

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Advances the sequence on real time and completes it once WARP_DURATION_S has passed
    void HandleShipsStepped(float realDeltaSeconds);

    // Aborts a running sequence if this frame's universe update ran without a ship step (the ship became invalid)
    void HandleUniverseUpdated();

    // Teleports the ship to the destination recomposed from the reference's current position, matched to its velocity
    void CompleteJump();

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    FSOLMapPickState mPick;                                     // Reference and locked offsets, fixed at Enter
    FVector3d mLastArrivalReferenceM = FVector3d::ZeroVector;   // Reference position at the last arrival
    FVector3d mLastArrivalPositionM = FVector3d::ZeroVector;    // Ship position set at the last arrival
    FDelegateHandle mShipsSteppedHandle;                        // Binding to the ship subsystem's OnShipsStepped
    FDelegateHandle mUniverseUpdatedHandle;                     // Binding to the anchor subsystem's OnUniverseUpdated
    double mElapsedS = 0.0;                                     // Real seconds into the running sequence
    int32 mCompletedJumpCount = 0;                              // Jumps completed since the world started
    int32 mLastArrivalVelocityBodyIndex = INDEX_NONE;           // Body whose velocity the last arrival took
    bool mIsWarping = false;                                    // True while the sequence runs
    bool mWasSteppedThisFrame = false;                          // The ship step ran in this frame's universe update
};
