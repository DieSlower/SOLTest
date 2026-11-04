/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Visuals/SOLSpaceDustMath.h"

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"

#include "SOLSpaceDustComponent.generated.h"

class AActor;
class UCameraComponent;
class UNiagaraComponent;
class USceneComponent;
class USOLAnchorSubsystem;
class USOLJumpSubsystem;
class USOLMapModeSubsystem;
class USOLMenuSubsystem;
class USOLSettingsSubsystem;
class USOLShipSubsystem;
class USOLSimClockSubsystem;

namespace Scalability
{
    struct FQualityLevels;
}

/**
 * Space dust speed cues (SDD 43): a world-aligned, camera-local box of GPU particles (NS_SOLSpaceDust) around the
 * player ship whose apparent motion shows the ship's speed relative to its reference frame.
 *
 * Lives on ASOLShipPawn and never ticks. Every frame, on USOLAnchorSubsystem::OnUniverseUpdated (render origin final),
 * it gates the effect (ship, Settings option, sol.SpaceDust, time warp, jump warp, map, pause), maps the relative
 * velocity through SOLSpaceDust (dead zone, saturating apparent speed, opacity, streaks), smooths the outputs, advances
 * the wrapped box offset in double on the CPU, places its Niagara component at the ship's render location (identity
 * rotation) and pushes a few small float user parameters. The Niagara component is activated once at BeginPlay and
 * only ever shown/hidden and resumed/paused after that (SDD 6 Amendment 15); the burst re-fires only when the effects
 * quality changes the particle count.
 */
UCLASS(ClassGroup = (SOL))
class SOLTEST_API USOLSpaceDustComponent : public UActorComponent
{
    GENERATED_BODY()

public:

    // Disables ticking: all per-frame work runs in the universe-update handler
    USOLSpaceDustComponent();

    // Returns the space dust component of an actor (the player ship pawn), or nullptr
    static USOLSpaceDustComponent* FindOn(const AActor* owner);

    // Returns true while the dust is shown (visible and simulating)
    bool IsDustShown() const { return mIsShown; }

    // Returns true when the Niagara component's desired state is paused (true without a component)
    bool IsNiagaraPaused() const;

    // Returns true when the Niagara system loaded and the component exists
    bool HasNiagaraSystem() const { return Niagara != nullptr; }

    // Returns the smoothed opacity [0, 1] pushed to the effect
    double GetSmoothedOpacity() const { return mSmoothedOpacity; }

    // Returns this frame's raw (unsmoothed) target opacity [0, 1]
    double GetTargetOpacity() const { return mTargetOpacity; }

    // Returns the smoothed apparent velocity of the dust (cm/s, render axes)
    const FVector3d& GetSmoothedApparentVelocityCmPerS() const { return mSmoothedVelocityCmPerS; }

    // Returns the streak length pushed to the effect (cm)
    double GetStreakLengthCm() const { return mStreakLengthCm; }

    // Returns the streak alpha pushed to the effect
    double GetStreakAlpha() const { return mStreakAlpha; }

    // Returns the wrapped box offset (cm, each axis in [0, box size))
    const FVector3d& GetWrappedOffsetCm() const { return mOffsetCm; }

    // Returns the delta (s) the last offset advance used (the anchor's clamped real delta of that update; verification)
    double GetLastAdvanceDeltaS() const { return mAdvanceDeltaS; }

    // Returns the Niagara component's world location (cm; zero without a component)
    FVector GetNiagaraWorldLocation() const;

    // Returns the particle count last set on the effect
    int32 GetParticleCount() const { return mParticleCount; }

    // Returns the number of times the burst was re-fired by an effects-quality change (verification)
    int32 GetReinitializeCount() const { return mReinitializeCount; }

protected:

    // Resolves the subsystems, creates and activates the Niagara component once (then hides and pauses it) and binds
    // the universe and scalability updates
    virtual void BeginPlay() override;

    // Unbinds every delegate and deactivates the Niagara component
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Loads the system and creates the Niagara component with the constant user parameters; false if it failed
    bool CreateNiagara();

    // Stores the anchor's clamped real frame delta (OnBodiesUpdated) for this frame's update
    void HandleBodiesUpdated(float realDeltaSeconds);

    // Per-frame update after the render origin is final: gate, map, smooth, place and push (SDD 43 sections 4 and 5)
    void HandleUniverseUpdated();

    // Effects quality changed: sets the new particle count and re-fires the burst once if the count changed
    void HandleScalabilityChanged(const Scalability::FQualityLevels& levels);

    // Builds this frame's visibility gate from the subsystems, the Settings option and the cvar
    FSOLSpaceDustGate BuildGate() const;

    // Shows (resumes) or hides (pauses) the Niagara component; never deactivates it
    void ApplyShown(bool bShown);

    // Notes the instance's first activation, then re-applies the pause state when the last SetPaused could not take
    // (the instance was not active yet)
    void SyncPauseState();

    // Pushes this frame's location and per-frame user parameters to the Niagara component, skipping every value equal
    // to the last one pushed (at steady speed only the offset and, while the ship moves through render space, the
    // location change)
    void PushFrame();

    // Forgets the last pushed values, so the next push writes every value and the location again
    void InvalidatePushCache();

    /** The dust particle system; absolute transform, placed every shown frame. Created at BeginPlay. */
    UPROPERTY(Transient)
    TObjectPtr<UNiagaraComponent> Niagara;

    UPROPERTY(Transient)
    TObjectPtr<UCameraComponent> OwnerCamera;           // The pawn's chase camera (near fade offset)

    UPROPERTY(Transient)
    TObjectPtr<USceneComponent> OwnerRoot;              // The pawn's ship root (near fade offset)

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    UPROPERTY(Transient)
    TObjectPtr<USOLJumpSubsystem> Jump;

    UPROPERTY(Transient)
    TObjectPtr<USOLMapModeSubsystem> MapMode;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    FSOLSpaceDustParams mParams;                                      // Tunables (SOL::SpaceDust defaults)
    FDelegateHandle mUniverseUpdatedHandle;                           // Binding to OnUniverseUpdated
    FDelegateHandle mBodiesUpdatedHandle;                             // Binding to OnBodiesUpdated (frame delta)
    FDelegateHandle mScalabilityHandle;                               // Binding to Scalability's settings change
    FDelegateHandle mAssetSyncHandle;                                 // Editor: binding to the asset re-sync
    FVector mPushedLocationCm = FVector::ZeroVector;                  // Last location set on the Niagara component
    FVector3f mPushedOffsetCm = FVector3f::ZeroVector;                // Last values written to the user parameters
    FVector3f mPushedVelocityCmS = FVector3f::ZeroVector;
    FVector3f mPushedCameraLocalCm = FVector3f::ZeroVector;
    float mPushedOpacity = 0.0f;
    float mPushedStreakLengthCm = 0.0f;
    float mPushedStreakAlpha = 0.0f;
    FVector3d mOffsetCm = FVector3d::ZeroVector;                      // Wrapped apparent-travel offset (cm)
    FVector3d mSmoothedVelocityCmPerS = FVector3d::ZeroVector;        // Smoothed apparent velocity (cm/s)
    FVector3d mCameraLocalCm = FVector3d::ZeroVector;                 // Camera minus ship root (cm)
    FVector3d mShipRenderCm = FVector3d::ZeroVector;                  // Ship render location (cm, double)
    double mFrameDeltaS = 0.0;                                        // Anchor's clamped real delta this frame
    double mAdvanceDeltaS = 0.0;                                      // Delta of the last offset advance (s)
    double mSmoothedOpacity = 0.0;                                    // Smoothed opacity [0, 1]
    double mTargetOpacity = 0.0;                                      // Raw opacity from |vRel| [0, 1]
    double mStreakLengthCm = 0.0;                                     // Streak length from the smoothed speed
    double mStreakAlpha = 1.0;                                        // Streak dimming from the streak length
    int32 mParticleCount = 0;                                         // Particle count set on the effect
    int32 mReinitializeCount = 0;                                     // Burst re-fires by a quality change
    bool mIsShown = false;                                            // Visible and simulating
    bool mNeedsReset = true;                                          // Next runnable frame resets the smoothing
    bool mIsPauseSynced = true;                                       // Last SetPaused took effect on the instance
    bool mHasBeenActive = false;                                      // The Niagara instance was active once
    bool mIsPushCacheValid = false;                                   // False: the next push writes every value
};
