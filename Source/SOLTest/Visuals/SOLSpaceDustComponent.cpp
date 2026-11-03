/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLSpaceDustComponent.h"

#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLOverlayModel.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "NiagaraUserRedirectionParameterStore.h"
#include "Scalability.h"

// The dust's game-thread cost (SDD 43 section 9): `stat dumpave -root=STAT_SOL_SpaceDustUpdate` or `stat SOLSpaceDust`
DECLARE_STATS_GROUP(TEXT("SOLSpaceDust"), STATGROUP_SOLSpaceDust, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SOL_SpaceDustUpdate"), STAT_SOL_SpaceDustUpdate, STATGROUP_SOLSpaceDust);

namespace
{
    // sol.SpaceDust: debug switch for the whole effect (session only, never persisted; SDD 43 section 4.3)
    TAutoConsoleVariable<int32> CVarSOLSpaceDust(SOL::SpaceDust::CVAR_ENABLED, 1,
        TEXT("Space dust speed cues (SDD 43): 1 = shown while the Overlay Menu's Space Dust row is checked (default), ")
        TEXT("0 = never shown (A/B profiling)"),
        ECVF_Default);

    // The constant Niagara user parameter names (set once through SetVariable*), hashed once (FName construction
    // looks the string up)
    struct FSOLSpaceDustParamNames
    {
        FName ParticleCount { SOL::SpaceDustParams::PARTICLE_COUNT };
        FName BoxSizeCm { SOL::SpaceDustParams::BOX_SIZE_CM };
        FName PointSizeCm { SOL::SpaceDustParams::POINT_SIZE_CM };
        FName RadialFadeStart { SOL::SpaceDustParams::RADIAL_FADE_START };
        FName NearFadeStartCm { SOL::SpaceDustParams::NEAR_FADE_START_CM };
        FName NearFadeEndCm { SOL::SpaceDustParams::NEAR_FADE_END_CM };
        FName Color { SOL::SpaceDustParams::COLOR };
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the parameter names, built on first use
    const FSOLSpaceDustParamNames& SOLSpaceDustNames()
    {
        static const FSOLSpaceDustParamNames names;
        return names;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a user parameter as its fully qualified (User.) Niagara variable of the given type
    FNiagaraVariable MakeDustUserVariable(const FNiagaraTypeDefinition& type, const TCHAR* name)
    {
        FNiagaraVariable variable(type, FName(name));
        FNiagaraUserRedirectionParameterStore::MakeUserVariable(variable);
        return variable;
    }

    // The per-frame user parameters as fully qualified variables, written straight into the component's override
    // store: the same store and write SetVariable* uses at runtime, without its editor-only override bookkeeping
    struct FSOLSpaceDustFrameVariables
    {
        FNiagaraVariable OffsetCm = MakeDustUserVariable(FNiagaraTypeDefinition::GetVec3Def(),
            SOL::SpaceDustParams::OFFSET_CM);
        FNiagaraVariable VelocityCmS = MakeDustUserVariable(FNiagaraTypeDefinition::GetVec3Def(),
            SOL::SpaceDustParams::VELOCITY_CM_S);
        FNiagaraVariable CameraLocalCm = MakeDustUserVariable(FNiagaraTypeDefinition::GetVec3Def(),
            SOL::SpaceDustParams::CAMERA_LOCAL_CM);
        FNiagaraVariable Opacity = MakeDustUserVariable(FNiagaraTypeDefinition::GetFloatDef(),
            SOL::SpaceDustParams::OPACITY);
        FNiagaraVariable StreakLengthCm = MakeDustUserVariable(FNiagaraTypeDefinition::GetFloatDef(),
            SOL::SpaceDustParams::STREAK_LENGTH_CM);
        FNiagaraVariable StreakAlpha = MakeDustUserVariable(FNiagaraTypeDefinition::GetFloatDef(),
            SOL::SpaceDustParams::STREAK_ALPHA);
    };

    //////////////////////////////////////////////////////////////////////////
    // Returns the per-frame variables, built on first use (after the Niagara module registered its types)
    const FSOLSpaceDustFrameVariables& SOLSpaceDustFrameVariables()
    {
        static const FSOLSpaceDustFrameVariables variables;
        return variables;
    }

    //////////////////////////////////////////////////////////////////////////
    // Writes one per-frame value into the override store unless it equals the last value written (exact compare of
    // the pushed float value, so the GPU sees the same values as an unconditional write); marks the store dirty
    template <typename TValue>
    void WriteDustParamIfChanged(FNiagaraUserRedirectionParameterStore& store, const FNiagaraVariable& variable,
        const TValue& value, TValue& lastPushed, const bool bForce)
    {
        if (!bForce && value == lastPushed)
        {
            return;
        }
        store.SetParameterValue(value, variable, true);
        lastPushed = value;
    }
}

//////////////////////////////////////////////////////////////////////////
// Disables ticking: all per-frame work runs in the universe-update handler
USOLSpaceDustComponent::USOLSpaceDustComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    PrimaryComponentTick.bStartWithTickEnabled = false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the space dust component of an actor (the player ship pawn), or nullptr
USOLSpaceDustComponent* USOLSpaceDustComponent::FindOn(const AActor* owner)
{
    return owner != nullptr ? owner->FindComponentByClass<USOLSpaceDustComponent>() : nullptr;
}

//////////////////////////////////////////////////////////////////////////
// Returns true when the Niagara component's desired state is paused (true without a component)
bool USOLSpaceDustComponent::IsNiagaraPaused() const
{
    return Niagara == nullptr || Niagara->IsPaused();
}

//////////////////////////////////////////////////////////////////////////
// Returns the Niagara component's world location (cm; zero without a component)
FVector USOLSpaceDustComponent::GetNiagaraWorldLocation() const
{
    return Niagara != nullptr ? Niagara->GetComponentLocation() : FVector::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Resolves the subsystems, creates and activates the Niagara component once (then hides and pauses it) and binds the
// universe and scalability updates
void USOLSpaceDustComponent::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    AActor* owner = GetOwner();
    if (world == nullptr || owner == nullptr)
    {
        return;
    }
    Ships = world->GetSubsystem<USOLShipSubsystem>();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    SimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    Jump = world->GetSubsystem<USOLJumpSubsystem>();
    MapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    Menu = world->GetSubsystem<USOLMenuSubsystem>();
    Settings = USOLSettingsSubsystem::Get(this);
    OwnerCamera = owner->FindComponentByClass<UCameraComponent>();
    OwnerRoot = owner->GetRootComponent();
    mIsDefaultRowEnabled = SOLOverlay::IsEnabled(SOLOverlay::MakeDefaultPrefs(), SOLOverlay::EOverlay::SpaceDust);
    if (Ships == nullptr || AnchorSubsystem == nullptr || SimClock == nullptr)
    {
        UE_LOG(LogSOL, Warning, TEXT("SpaceDust %s: ship or universe subsystems missing; the space dust stays off"),
            *GetName());
        return;
    }

    // The particle count follows the engine's effects quality (sg.EffectsQuality; Q5), set before the one activation
    mParticleCount = SOLSpaceDust::ParticleCountForQuality(Scalability::GetQualityLevels().EffectsQuality);
    if (!CreateNiagara())
    {
        UE_LOG(LogSOL, Warning, TEXT("SpaceDust %s: failed to load %s; the space dust stays off"), *GetName(),
            SOL::Paths::SPACE_DUST_NIAGARA_SYSTEM);
    }

    mNeedsReset = true;
    mBodiesUpdatedHandle = AnchorSubsystem->OnBodiesUpdated().AddUObject(this,
        &USOLSpaceDustComponent::HandleBodiesUpdated);
    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this,
        &USOLSpaceDustComponent::HandleUniverseUpdated);
    mScalabilityHandle = Scalability::OnScalabilitySettingsChanged.AddUObject(this,
        &USOLSpaceDustComponent::HandleScalabilityChanged);
}

//////////////////////////////////////////////////////////////////////////
// Unbinds every delegate and deactivates the Niagara component
void USOLSpaceDustComponent::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
        AnchorSubsystem->OnBodiesUpdated().Remove(mBodiesUpdatedHandle);
    }
    mUniverseUpdatedHandle.Reset();
    mBodiesUpdatedHandle.Reset();
    Scalability::OnScalabilitySettingsChanged.Remove(mScalabilityHandle);
    mScalabilityHandle.Reset();
    if (Niagara != nullptr)
    {
#if WITH_EDITOR
        Niagara->OnSynchronizedWithAssetParameters().Remove(mAssetSyncHandle);
#endif
        Niagara->DeactivateImmediate();
    }
    mAssetSyncHandle.Reset();
    mIsShown = false;
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Loads the system and creates the Niagara component with the constant user parameters; false if it failed
bool USOLSpaceDustComponent::CreateNiagara()
{
    UNiagaraSystem* system = LoadObject<UNiagaraSystem>(nullptr, SOL::Paths::SPACE_DUST_NIAGARA_SYSTEM);
    AActor* owner = GetOwner();
    if (system == nullptr || owner == nullptr)
    {
        return false;
    }

    // Absolute transform: placed at the ship's render location every shown frame with a world-aligned (identity)
    // rotation, so turning the ship never sweeps the dust; no auto-activation, the one Activate below runs after
    // every user parameter is set, so the burst reads them deterministically
    Niagara = NewObject<UNiagaraComponent>(owner, TEXT("SpaceDustNiagara"));
    Niagara->bAutoActivate = false;
    Niagara->SetUsingAbsoluteLocation(true);
    Niagara->SetUsingAbsoluteRotation(true);
    Niagara->SetUsingAbsoluteScale(true);
    Niagara->SetCastShadow(false);
    if (OwnerRoot != nullptr)
    {
        Niagara->SetupAttachment(OwnerRoot);
    }
    Niagara->RegisterComponent();
    Niagara->SetWorldLocationAndRotation(FVector::ZeroVector, FQuat::Identity);
    Niagara->SetWorldScale3D(FVector::OneVector);
    Niagara->SetAsset(system);

    // Constant user parameters (SDD 43 section 5.4)
    const FSOLSpaceDustParamNames& names = SOLSpaceDustNames();
    Niagara->SetVariableInt(names.ParticleCount, mParticleCount);
    Niagara->SetVariableFloat(names.BoxSizeCm, static_cast<float>(mParams.BoxSizeCm));
    Niagara->SetVariableFloat(names.PointSizeCm, static_cast<float>(mParams.PointSizeCm));
    Niagara->SetVariableFloat(names.RadialFadeStart, static_cast<float>(mParams.RadialFadeStartFraction));
    Niagara->SetVariableFloat(names.NearFadeStartCm, static_cast<float>(mParams.NearFadeStartCm));
    Niagara->SetVariableFloat(names.NearFadeEndCm, static_cast<float>(mParams.NearFadeEndCm));
    Niagara->SetVariableLinearColor(names.Color, SOL::SpaceDust::DUST_COLOR);

    // The per-frame parameters go through the override store only (never the editor's instance override map, which
    // an editor re-sync would re-apply over them); invisible until the first push
    float initialOpacity = 0.0f;
    WriteDustParamIfChanged(Niagara->GetOverrideParameters(), SOLSpaceDustFrameVariables().Opacity, 0.0f,
        initialOpacity, true);
    mIsPushCacheValid = false;
#if WITH_EDITOR
    // The system recompiling in the editor re-syncs the store to the asset's defaults: push every value again
    mAssetSyncHandle = Niagara->OnSynchronizedWithAssetParameters().AddUObject(this,
        &USOLSpaceDustComponent::InvalidatePushCache);
#endif

    // Activated once, then hidden and paused; shown and resumed only through ApplyShown
    Niagara->SetVisibility(false);
    Niagara->Activate(true);
    Niagara->SetPaused(true);
    mHasBeenActive = Niagara->IsActive();
    mIsShown = false;
    mIsPauseSynced = Niagara->GetExecutionState() == ENiagaraExecutionState::Active;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Stores the anchor's clamped real frame delta (OnBodiesUpdated) for this frame's update
void USOLSpaceDustComponent::HandleBodiesUpdated(const float realDeltaSeconds)
{
    mFrameDeltaS = realDeltaSeconds;
}

//////////////////////////////////////////////////////////////////////////
// Builds this frame's visibility gate from the subsystems, the overlay row and the cvar
FSOLSpaceDustGate USOLSpaceDustComponent::BuildGate() const
{
    FSOLSpaceDustGate gate;
    gate.bHasShip = Ships != nullptr && Ships->HasPlayerShip();

    // The row's own checkbox in the active profile (IsEnabled, not IsShown: the master O never affects the dust, Q2)
    gate.bRowEnabled = Settings != nullptr
        ? SOLOverlay::IsEnabled(Settings->GetOverlayPrefs(), SOLOverlay::EOverlay::SpaceDust)
        : mIsDefaultRowEnabled;
    gate.bCvarEnabled = CVarSOLSpaceDust.GetValueOnGameThread() != 0;
    gate.TimeWarpFactor = SimClock != nullptr ? SimClock->GetClock().GetWarpFactor() : 1.0;
    gate.bJumpWarping = Jump != nullptr && Jump->IsWarping();
    gate.bMapOpen = MapMode != nullptr && MapMode->IsMapOpen();

    // Paused as the HUD reads it: a main or pause menu showing (the simulation keeps running under it). A paused world
    // needs no check: the anchor does not tick then, so this handler never runs
    gate.bPaused = Menu != nullptr && Menu->IsMenuShowing();
    return gate;
}

//////////////////////////////////////////////////////////////////////////
// Per-frame update after the render origin is final: gate, map, smooth, place and push (SDD 43 sections 4 and 5)
void USOLSpaceDustComponent::HandleUniverseUpdated()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLSpaceDustComponent::HandleUniverseUpdated);
    SCOPE_CYCLE_COUNTER(STAT_SOL_SpaceDustUpdate);
    SyncPauseState();

    // Hard gate: hidden at once (no fade), and the smoothing restarts from scratch when the dust may run again
    const FSOLSpaceDustGate gate = BuildGate();
    if (!SOLSpaceDust::ShouldRun(gate, mParams))
    {
        ApplyShown(false);
        mNeedsReset = true;
        mTargetOpacity = 0.0;
        mSmoothedOpacity = 0.0;
        return;
    }

    // Speed mapping on the raw relative velocity (the HUD speed's exact expression; Unreal-handed m/s, whose axes are
    // the render axes, so the apparent velocity comes out in render-space cm/s)
    const FVector3d relativeVelocityMps = Ships->GetState().VelocityMps - Ships->GetReferenceVelocityMps();
    mTargetOpacity = SOLSpaceDust::Opacity(relativeVelocityMps.Size(), mParams);
    const FVector3d targetVelocityCmPerS = SOLSpaceDust::ApparentVelocityCmPerS(relativeVelocityMps, mParams);

    // After a hard gate the direction snaps to its target and the opacity fades in from 0, so no stale vector from
    // before a jump is ever replayed
    if (mNeedsReset)
    {
        mNeedsReset = false;
        mSmoothedVelocityCmPerS = targetVelocityCmPerS;
        mSmoothedOpacity = 0.0;
    }

    // Only the mapping's outputs are smoothed (SDD 43 section 5.3); streaks follow the smoothed apparent speed
    const double dtS = FMath::Max(mFrameDeltaS, 0.0);
    mSmoothedVelocityCmPerS = SOLSpaceDust::SmoothVector(mSmoothedVelocityCmPerS, targetVelocityCmPerS, dtS,
        mParams.SmoothingTauS);
    mSmoothedOpacity = SOLSpaceDust::SmoothScalar(mSmoothedOpacity, mTargetOpacity, dtS, mParams.SmoothingTauS);
    mStreakLengthCm = SOLSpaceDust::StreakLengthCm(mSmoothedVelocityCmPerS.Size() * SOL::CM_TO_METERS, mParams);
    mStreakAlpha = SOLSpaceDust::StreakAlpha(mStreakLengthCm, mParams);

    // Apparent travel, wrapped into one box length in double before any float cast; the GPU places each particle at
    // wrap(anchor - offset), so the dust streams opposite to the ship's relative motion
    mOffsetCm = SOLSpaceDust::AdvanceOffsetCm(mOffsetCm, mSmoothedVelocityCmPerS, dtS, mParams.BoxSizeCm);
    mAdvanceDeltaS = dtS;

    // Stopped: faded out and nothing left to fade in, so hidden and paused (no GPU cost)
    if (mSmoothedOpacity < SOL::SpaceDust::OPACITY_EPSILON && mTargetOpacity <= 0.0)
    {
        ApplyShown(false);
        return;
    }
    if (Niagara == nullptr)
    {
        return;
    }

    // The ship's render location from this frame's final render origin, in double (the map is closed, so the viewpoint
    // is the observer and the placement is 1:1); the camera offset from the pawn's camera and ship root, read together
    // so the ship's own motion through render space cancels
    mShipRenderCm = AnchorSubsystem->GetRenderOrigin().UniverseToRenderCm(AnchorSubsystem->GetObserverPositionM());
    mCameraLocalCm = OwnerCamera != nullptr && OwnerRoot != nullptr
        ? FVector3d(OwnerCamera->GetComponentLocation() - OwnerRoot->GetComponentLocation())
        : FVector3d::ZeroVector;
    ApplyShown(true);
    PushFrame();
}

//////////////////////////////////////////////////////////////////////////
// Pushes this frame's location and per-frame user parameters to the Niagara component, skipping every value equal to
// the last one pushed (at steady speed only the offset and, while the ship moves through render space, the location
// change)
void USOLSpaceDustComponent::PushFrame()
{
    // Rotation is identity and the scale one for good (absolute, set at creation); only the location moves
    const bool bForce = !mIsPushCacheValid;
    const FVector locationCm(mShipRenderCm);
    if (bForce || locationCm != mPushedLocationCm)
    {
        Niagara->SetWorldLocation(locationCm);
        mPushedLocationCm = locationCm;
    }

    // Written straight into the override store, which marks it dirty; the instance copies it into its parameters and
    // the render thread receives them on the normal Niagara update, exactly as with SetVariable*
    FNiagaraUserRedirectionParameterStore& store = Niagara->GetOverrideParameters();
    const FSOLSpaceDustFrameVariables& variables = SOLSpaceDustFrameVariables();
    WriteDustParamIfChanged(store, variables.OffsetCm, FVector3f(mOffsetCm), mPushedOffsetCm, bForce);
    WriteDustParamIfChanged(store, variables.VelocityCmS, FVector3f(mSmoothedVelocityCmPerS), mPushedVelocityCmS,
        bForce);
    WriteDustParamIfChanged(store, variables.Opacity, static_cast<float>(mSmoothedOpacity), mPushedOpacity, bForce);
    WriteDustParamIfChanged(store, variables.StreakLengthCm, static_cast<float>(mStreakLengthCm),
        mPushedStreakLengthCm, bForce);
    WriteDustParamIfChanged(store, variables.StreakAlpha, static_cast<float>(mStreakAlpha), mPushedStreakAlpha,
        bForce);
    WriteDustParamIfChanged(store, variables.CameraLocalCm, FVector3f(mCameraLocalCm), mPushedCameraLocalCm, bForce);
    mIsPushCacheValid = true;
}

//////////////////////////////////////////////////////////////////////////
// Forgets the last pushed values, so the next push writes every value and the location again
void USOLSpaceDustComponent::InvalidatePushCache()
{
    mIsPushCacheValid = false;
}

//////////////////////////////////////////////////////////////////////////
// Shows (resumes) or hides (pauses) the Niagara component; never deactivates it
void USOLSpaceDustComponent::ApplyShown(const bool bShown)
{
    if (bShown == mIsShown)
    {
        return;
    }
    mIsShown = bShown;

    // Nothing is pushed while hidden: the first shown frame writes every value again
    InvalidatePushCache();
    if (Niagara == nullptr)
    {
        return;
    }

    // The engine itself can deactivate a system (scalability or culling policy); bring it back on a show rather than
    // staying silently empty, at the price of one reshuffle (the only activation outside BeginPlay). Before the
    // instance was ever active, the BeginPlay activation is only deferred (the system was still compiling): no action
    if (bShown && mHasBeenActive && !Niagara->IsActive())
    {
        UE_LOG(LogSOL, Warning, TEXT("SpaceDust %s: the dust system was deactivated by the engine; reactivating"),
            *GetName());
        Niagara->Activate(true);
    }
    Niagara->SetVisibility(bShown);
    Niagara->SetPaused(!bShown);

    // SetPaused is ignored while the instance is not active yet (e.g. right after Activate); retried next frame
    mIsPauseSynced = Niagara->GetExecutionState() == ENiagaraExecutionState::Active;
}

//////////////////////////////////////////////////////////////////////////
// Notes the instance's first activation, then re-applies the pause state when the last SetPaused could not take (the
// instance was not active yet)
void USOLSpaceDustComponent::SyncPauseState()
{
    if (!mHasBeenActive && Niagara != nullptr && Niagara->IsActive())
    {
        mHasBeenActive = true;
    }
    if (mIsPauseSynced || Niagara == nullptr || Niagara->GetExecutionState() != ENiagaraExecutionState::Active)
    {
        return;
    }
    Niagara->SetPaused(!mIsShown);
    mIsPauseSynced = true;
}

//////////////////////////////////////////////////////////////////////////
// Effects quality changed: sets the new particle count and re-fires the burst once if the count changed
void USOLSpaceDustComponent::HandleScalabilityChanged(const Scalability::FQualityLevels& levels)
{
    const int32 particleCount = SOLSpaceDust::ParticleCountForQuality(levels.EffectsQuality);
    if (particleCount == mParticleCount)
    {
        return;
    }
    mParticleCount = particleCount;
    if (Niagara == nullptr)
    {
        return;
    }

    // The only reshuffle of the anchors: the burst re-fires with the new count, then the shown state is re-applied
    Niagara->SetVariableInt(SOLSpaceDustNames().ParticleCount, mParticleCount);
    Niagara->ReinitializeSystem();
    ++mReinitializeCount;
    InvalidatePushCache();
    Niagara->SetVisibility(mIsShown);
    Niagara->SetPaused(!mIsShown);
    mIsPauseSynced = Niagara->GetExecutionState() == ENiagaraExecutionState::Active;
    UE_LOG(LogSOL, Log, TEXT("SpaceDust %s: effects quality %d, %d particles"), *GetName(), levels.EffectsQuality,
        mParticleCount);
}
