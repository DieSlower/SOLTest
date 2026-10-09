/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipSubsystem.h"

#include "Game/SOLGameMode.h"
#include "Ship/SOLShipFlightProcessor.h"
#include "Ship/SOLShipFragments.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

//////////////////////////////////////////////////////////////////////////
// Declares its dependencies and prepares the flight processor
void USOLShipSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    UMassEntitySubsystem* massEntities = collection.InitializeDependency<UMassEntitySubsystem>();
    SimClock = collection.InitializeDependency<USOLSimClockSubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Anchor = collection.InitializeDependency<USOLAnchorSubsystem>();
    Targeting = collection.InitializeDependency<USOLTargetingSubsystem>();
    Super::Initialize(collection);

    if (massEntities == nullptr || SimClock == nullptr || BodyRegistry == nullptr || Anchor == nullptr
        || Targeting == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("ShipSubsystem %s: Mass or universe subsystems missing"), *GetName());
        return;
    }
    mEntityManager = massEntities->GetMutableEntityManager().AsShared();
    mCommandBuffer = MakeShared<FMassCommandBuffer>();

    // Last frame's bodies are kept for the time-warp carry and the substep interpolation; sized once here
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    mPrevBodyPositionsM.SetNumZeroed(cache.Num());
    mPrevBodyVelocitiesMps.SetNumZeroed(cache.Num());
    mHasFrameHistory = false;

    FlightProcessor = NewObject<USOLShipFlightProcessor>(this);
    FlightProcessor->SetBodyCache(&cache);
    FlightProcessor->CallInitialize(this, mEntityManager.ToSharedRef());

    mBodiesUpdatedHandle = Anchor->OnBodiesUpdated().AddUObject(this, &USOLShipSubsystem::StepShips);
}

//////////////////////////////////////////////////////////////////////////
// Destroys the player ship entity while the entity manager is still alive (before any subsystem deinitializes)
void USOLShipSubsystem::PreDeinitialize()
{
    // Deinitialize is too late: UMassEntitySubsystem may already have torn the entity manager's storage down by then
    // (subsystem deinitialization order is not dependency-ordered), and touching it there asserts
    if (mEntityManager.IsValid() && mEntityManager->IsEntityValid(mPlayerShip))
    {
        mEntityManager->DestroyEntity(mPlayerShip);
    }
    mPlayerShip = FMassEntityHandle();
    Super::PreDeinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Unhooks from the universe update and releases the Mass objects
void USOLShipSubsystem::Deinitialize()
{
    if (Anchor != nullptr)
    {
        Anchor->OnBodiesUpdated().Remove(mBodiesUpdatedHandle);
    }
    mBodiesUpdatedHandle.Reset();
    mPlayerShip = FMassEntityHandle();
    mSmokeFlight.Reset();
    mCommandBuffer.Reset();
    mEntityManager.Reset();
    mPrevBodyPositionsM.Empty();
    mPrevBodyVelocitiesMps.Empty();
    mHasFrameHistory = false;
    FlightProcessor = nullptr;
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Spawns the player ship in orbit and starts the scripted smoke flight if requested
void USOLShipSubsystem::OnWorldBeginPlay(UWorld& world)
{
    Super::OnWorldBeginPlay(world);
    SpawnPlayerShip();

    // Verification hook: fly a fixed script without a pawn, log checkpoints, then quit
    if (HasPlayerShip() && FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_FLIGHT))
    {
        mSmokeFlight = MakeUnique<FSOLShipSmokeFlight>();
        mSmokeFlight->Start(*this, *Anchor, BodyRegistry->GetRegistry());
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLShipSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLShipSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Returns true once the player ship entity exists
bool USOLShipSubsystem::HasPlayerShip() const
{
    return mEntityManager.IsValid() && mEntityManager->IsEntityValid(mPlayerShip);
}

//////////////////////////////////////////////////////////////////////////
// Sets the player ship's control input; ReferenceVelocityMps is replaced each step by the reference body's velocity
void USOLShipSubsystem::SetControl(const FSOLShipControl& control)
{
    if (HasPlayerShip())
    {
        mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip).Control = control;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the player ship's current control input
FSOLShipControl USOLShipSubsystem::GetControl() const
{
    return HasPlayerShip() ? mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip).Control
        : FSOLShipControl();
}

//////////////////////////////////////////////////////////////////////////
// Returns a copy of the player ship's state (Unreal-handed universe frame)
FSOLShipState USOLShipSubsystem::GetState() const
{
    return HasPlayerShip() ? mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip).State
        : FSOLShipState();
}

//////////////////////////////////////////////////////////////////////////
// Teleports the player ship to a new state (Unreal-handed universe frame) and moves the observer with it
void USOLShipSubsystem::SetState(const FSOLShipState& state)
{
    if (!HasPlayerShip() || Targeting == nullptr)
    {
        return;
    }
    FSOLShipStateFragment& fragment = mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip);
    fragment.State = state;
    fragment.ContactBodyIndex = INDEX_NONE;
    Anchor->SetObserverPositionM(SOLRender::EclipticToUnreal(state.PositionM));

    // A teleport can change the anchor, and with it the reference frame; re-resolve it now, not at the next step
    Targeting->RefreshCandidates();
}

//////////////////////////////////////////////////////////////////////////
// Returns the player ship's universe position in the ecliptic frame (meters)
FVector3d USOLShipSubsystem::GetUniversePositionM() const
{
    // EclipticToUnreal only negates Y, so it is its own inverse
    return SOLRender::EclipticToUnreal(GetState().PositionM);
}

//////////////////////////////////////////////////////////////////////////
// Sets the player ship's flight-model parameters (swaps its const shared params value; a chunk move, rare)
void USOLShipSubsystem::SetFlightParams(const FSOLFlightParams& params)
{
    if (HasPlayerShip())
    {
        mEntityManager->SwapConstSharedFragmentForEntity(mPlayerShip,
            mEntityManager->GetOrCreateConstSharedFragment(FSOLShipParamsFragment::Make(params)));
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the player ship's flight-model parameters
FSOLFlightParams USOLShipSubsystem::GetFlightParams() const
{
    return HasPlayerShip()
        ? mEntityManager->GetConstSharedFragmentDataChecked<FSOLShipParamsFragment>(mPlayerShip).Params
        : FSOLFlightParams();
}

//////////////////////////////////////////////////////////////////////////
// Returns the body the ship rested on during the last step, or INDEX_NONE
int32 USOLShipSubsystem::GetContactBodyIndex() const
{
    return HasPlayerShip()
        ? mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip).ContactBodyIndex : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the active reference frame's velocity (Unreal-handed universe frame, m/s), from the targeting subsystem
FVector3d USOLShipSubsystem::GetReferenceVelocityMps() const
{
    return Targeting != nullptr ? Targeting->GetReferenceVelocityMps() : FVector3d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Returns the index of the body whose surface is nearest the ship, and the ship's altitude above it (meters)
int32 USOLShipSubsystem::FindNearestBody(double& outAltitudeM) const
{
    outAltitudeM = 0.0;
    if (BodyRegistry == nullptr || !HasPlayerShip())
    {
        return INDEX_NONE;
    }
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    const FVector3d shipM = GetState().PositionM;
    int32 nearest = INDEX_NONE;
    double bestAltitudeM = TNumericLimits<double>::Max();
    for (int32 index = 0; index < cache.Num(); ++index)
    {
        const double altitudeM = FVector3d::Dist(cache.PositionsM[index], shipM) - cache.RadiiM[index];
        if (altitudeM < bestAltitudeM)
        {
            bestAltitudeM = altitudeM;
            nearest = index;
        }
    }
    outAltitudeM = nearest == INDEX_NONE ? 0.0 : bestAltitudeM;
    return nearest;
}

//////////////////////////////////////////////////////////////////////////
// Creates the player ship entity on a circular orbit above the start body and makes it the observer
void USOLShipSubsystem::SpawnPlayerShip()
{
    if (!mEntityManager.IsValid() || HasPlayerShip())
    {
        return;
    }

    // Start body and altitude, overridable from the command line for verification runs
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    FString startBody = SOL::BodyNames::EARTH;
    FParse::Value(FCommandLine::Get(), SOL::CommandLine::START_BODY, startBody);
    double altitudeKm = SOL::DEFAULT_SPAWN_ALTITUDE_M / SOL::METERS_PER_KM;
    FParse::Value(FCommandLine::Get(), SOL::CommandLine::ALTITUDE_KM, altitudeKm);
    int32 bodyIndex = registry.FindByName(FName(*startBody));
    if (bodyIndex == INDEX_NONE)
    {
        UE_LOG(LogSOL, Warning, TEXT("ShipSubsystem %s: unknown start body '%s', using %s"), *GetName(), *startBody,
            SOL::BodyNames::EARTH);
        bodyIndex = registry.FindByName(FName(SOL::BodyNames::EARTH));
    }

    // Circular orbit on the body's Sun-facing side, in the Unreal-handed frame
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    const int32 sunIndex = registry.FindByName(FName(SOL::BodyNames::SUN));
    const FVector3d sunM = sunIndex == INDEX_NONE ? FVector3d::ZeroVector : cache.PositionsM[sunIndex];
    FVector3d spawnDirection = sunM - cache.PositionsM[bodyIndex];
    FVector3d spawnOffsetM = FVector3d::ZeroVector;
    FVector3d spawnTangent = FVector3d::ZeroVector;

    // Verification-only: -SOLRingStartKm=<r> starts inside the start body's ring plane, r km from its centre along the
    // ring's X axis and -SOLRingHeightKm=<h> above the plane, so the ring's dense rock layer can be screenshotted
    double ringStartKm = 0.0;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::RING_START_KM, ringStartKm) && ringStartKm > 0.0)
    {
        double ringHeightKm = SOL::RING_START_DEFAULT_HEIGHT_KM;
        FParse::Value(FCommandLine::Get(), SOL::CommandLine::RING_HEIGHT_KM, ringHeightKm);
        const FQuat4d orientation = registry.GetOrientation(bodyIndex);
        spawnDirection = SOLRender::EclipticToUnreal(orientation.RotateVector(FVector3d(1.0, 0.0, 0.0)));
        spawnOffsetM = SOLRender::EclipticToUnreal(orientation.RotateVector(FVector3d(0.0, 0.0, 1.0)))
            * (ringHeightKm * SOL::METERS_PER_KM);
        spawnTangent = SOLRender::EclipticToUnreal(orientation.RotateVector(FVector3d(0.0, 1.0, 0.0)));
        altitudeKm = ringStartKm - cache.RadiiM[bodyIndex] / SOL::METERS_PER_KM;
        if (altitudeKm <= 0.0)
        {
            UE_LOG(LogSOL, Warning, TEXT("ShipSubsystem %s: -SOLRingStartKm=%.0f is inside %s (radius %.0f km)"),
                *GetName(), ringStartKm, *registry.GetName(bodyIndex).ToString(),
                cache.RadiiM[bodyIndex] / SOL::METERS_PER_KM);
        }
    }
    FSOLShipState state = SOLFlight::MakeCircularOrbitState(cache.PositionsM[bodyIndex],
        cache.VelocitiesMps[bodyIndex], cache.GMs[bodyIndex], cache.RadiiM[bodyIndex],
        altitudeKm * SOL::METERS_PER_KM, spawnDirection);
    state.PositionM += spawnOffsetM;
    if (!spawnTangent.IsZero())
    {
        // In the ring plane the ship co-rotates with the ring (circular speed along the ring's Y axis), so it stays in
        // the ring instead of drifting out of the plane at the circular-orbit state's own inclination
        const double ringRadiusM = ringStartKm * SOL::METERS_PER_KM;
        state.VelocityMps = cache.VelocitiesMps[bodyIndex] + spawnTangent * FMath::Sqrt(cache.GMs[bodyIndex] / ringRadiusM);
    }

    // One archetype for ships; the player's carries the player tag, and its class's params are a const shared value
    const UScriptStruct* const elements[] = {
        FSOLShipStateFragment::StaticStruct(),
        FSOLShipControlFragment::StaticStruct(),
        FSOLPlayerShipTag::StaticStruct(),
    };
    const FMassArchetypeHandle archetype = mEntityManager->CreateArchetype(MakeArrayView(elements));
    FMassArchetypeSharedFragmentValues sharedValues;
    sharedValues.Add(mEntityManager->GetOrCreateConstSharedFragment(FSOLShipParamsFragment::Make(FSOLFlightParams())));
    sharedValues.Sort();
    mPlayerShip = mEntityManager->CreateEntity(archetype, sharedValues);

    FSOLShipControl control;
    control.bFlightAssist = true;
    control.SpeedCapMps = FMath::Clamp(SOL::SHIP_START_SPEED_CAP_MPS, SOL::MIN_SPEED_CAP_MPS, SOL::MAX_SPEED_CAP_MPS);
    mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip).State = state;
    mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip).Control = control;

    // The ship is the observer from now on; the frame (anchor) it sets is resolved for targeting right away
    Anchor->SetObserverPositionM(SOLRender::EclipticToUnreal(state.PositionM));
    Targeting->RefreshCandidates();
    UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: player ship spawned %.0f km above %s, orbital speed %.1f m/s"),
        *GetName(), altitudeKm, *registry.GetName(bodyIndex).ToString(),
        (state.VelocityMps - cache.VelocitiesMps[bodyIndex]).Size());
}

//////////////////////////////////////////////////////////////////////////
// Runs the ship step for one frame and syncs the observer; bound to the anchor subsystem's OnBodiesUpdated
void USOLShipSubsystem::StepShips(const float realDeltaSeconds)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLShipSubsystem::StepShips);
    if (!HasPlayerShip() || FlightProcessor == nullptr)
    {
        return;
    }

    // Targets first, from this frame's bodies, so the assist frame (M lock, else the anchor) has this frame's velocity
    Targeting->RefreshCandidates();
    mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip).Control.ReferenceVelocityMps =
        Targeting->GetReferenceVelocityMps();

    // Time-warp carry and substep inputs, then the flight processor, with the shared command buffer so the context
    // allocates none
    {
        FSOLShipFrameInputs inputs;
        ComputeFrameInputs(realDeltaSeconds, inputs);
        FlightProcessor->SetFrameInputs(inputs);
        UE::Mass::FProcessingContext context(*mEntityManager, realDeltaSeconds);
        context.SetCommandBuffer(mCommandBuffer);
        UE::Mass::Executor::Run(*FlightProcessor, context);
    }
    SaveFrameHistory();

    // Surface-lock after the flight run, when the ship and the bodies are both at this frame's positions (before the
    // run the ship is still where last frame's bodies left it, which misreads the altitude by up to the body's speed
    // times the frame time: ~500 m for Earth at 60 fps). Its decision applies from the next step on
    UpdateSurfaceLock();

    // The ship is the observer: hand its position (ecliptic) to the anchor update that follows
    Anchor->SyncObserverPositionM(GetUniversePositionM());
    mOnShipsStepped.Broadcast(realDeltaSeconds);

    if (mSmokeFlight.IsValid()
        && mSmokeFlight->Update(*this, *Anchor, BodyRegistry->GetRegistry(), realDeltaSeconds))
    {
        mSmokeFlight.Reset();
        UKismetSystemLibrary::QuitGame(GetWorld(), nullptr, EQuitPreference::Quit, false);
    }
}

//////////////////////////////////////////////////////////////////////////
// Releases any surface-lock and clears its auto-engage suppression and a pending L press (used by a jump arrival)
void USOLShipSubsystem::ClearSurfaceLock()
{
    if (mSurfaceLockState.bEngaged || mSurfaceLockState.bAutoSuppressed)
    {
        UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: surface-lock cleared (was %s)"), *GetName(),
            mSurfaceLockState.bEngaged ? TEXT("engaged") : TEXT("suppressed"));
    }
    mSurfaceLockState = FSOLSurfaceLockState();
    mSurfaceLockAltitudeM = 0.0;
    mIsSurfaceLockToggleRequested = false;
    if (HasPlayerShip())
    {
        mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip).AlignBodyIndex = INDEX_NONE;
    }
}

//////////////////////////////////////////////////////////////////////////
// Advances the surface-lock state one step (consuming a pending L press) and names the body to align to, if any
void USOLShipSubsystem::UpdateSurfaceLock()
{
    const bool bTogglePressed = mIsSurfaceLockToggleRequested;
    mIsSurfaceLockToggleRequested = false;

    // Nearest body (altitude above its surface) and, while engaged, the locked body's own altitude, all from this
    // frame's Unreal-handed bodies (the frame the ship's position is in)
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    const FVector3d shipM = GetState().PositionM;
    double nearestAltitudeM = 0.0;
    const int32 nearest = FindNearestBody(nearestAltitudeM);
    const double nearestRadiusM = cache.RadiiM.IsValidIndex(nearest) ? cache.RadiiM[nearest] : 0.0;
    FSOLSurfaceLockState prevState = mSurfaceLockState;
    double lockedAltitudeM = 0.0;
    double lockedRadiusM = 0.0;
    if (prevState.bEngaged)
    {
        // Registry indices are stable for the session; a stale one releases rather than reading another body
        if (cache.PositionsM.IsValidIndex(prevState.BodyIndex))
        {
            lockedRadiusM = cache.RadiiM[prevState.BodyIndex];
            lockedAltitudeM = FVector3d::Dist(shipM, cache.PositionsM[prevState.BodyIndex]) - lockedRadiusM;
        }
        else
        {
            UE_LOG(LogSOL, Warning, TEXT("ShipSubsystem %s: surface-locked body index %d is invalid; released"),
                *GetName(), prevState.BodyIndex);
            prevState = FSOLSurfaceLockState();
        }
    }
    mSurfaceLockState = SOLSurfaceLock::UpdateSurfaceLockState(prevState, nearest, nearestAltitudeM, nearestRadiusM,
        lockedAltitudeM, lockedRadiusM, bTogglePressed, mSurfaceLockParams);

    // Altitude above the body now locked (on the engaging step that is the nearest body)
    mSurfaceLockAltitudeM = 0.0;
    if (mSurfaceLockState.bEngaged)
    {
        mSurfaceLockAltitudeM = mSurfaceLockState.BodyIndex == prevState.BodyIndex && prevState.bEngaged
            ? lockedAltitudeM : nearestAltitudeM;
    }

    // Engaging matches the reference frame to the body (SDD 4 decision 2); releasing leaves the frame as it is
    if (mSurfaceLockState.bEngaged && !prevState.bEngaged)
    {
        Targeting->LockToBodyIndex(mSurfaceLockState.BodyIndex);
    }
    if (mSurfaceLockState.bEngaged != prevState.bEngaged || mSurfaceLockState.bWarning != prevState.bWarning
        || mSurfaceLockState.bAutoSuppressed != prevState.bAutoSuppressed)
    {
        // Only on a transition, so this logs a handful of lines per approach, not every frame
        const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
        const int32 bodyIndex = mSurfaceLockState.bEngaged ? mSurfaceLockState.BodyIndex
            : (prevState.bEngaged ? prevState.BodyIndex : prevState.SuppressedBodyIndex);
        const TCHAR* status = mSurfaceLockState.bEngaged
            ? (mSurfaceLockState.bWarning ? TEXT("WARNING") : (mSurfaceLockState.bManual ? TEXT("ENGAGED manual")
            : TEXT("ENGAGED auto")))
            : (mSurfaceLockState.bAutoSuppressed ? TEXT("RELEASED, auto-engage suppressed")
            : (prevState.bEngaged ? TEXT("RELEASED") : TEXT("auto-engage suppression cleared")));
        UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: surface-lock %s, body %s, altitude %.0f m, L %s"), *GetName(),
            status, bodyIndex >= 0 && bodyIndex < registry.Num() ? *registry.GetName(bodyIndex).ToString()
            : TEXT("none"), mSurfaceLockState.bEngaged ? mSurfaceLockAltitudeM : nearestAltitudeM, bTogglePressed
            ? TEXT("pressed") : TEXT("not pressed"));
    }

    // The flight processor aligns the ship's up to the locked body each substep, at its class's time constant
    // (FSOLFlightParams::AlignTimeConstantS)
    mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip).AlignBodyIndex =
        mSurfaceLockState.bEngaged ? mSurfaceLockState.BodyIndex : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Fills the frame's warp factor, last frame's bodies and the reference frame's time-warp carry
void USOLShipSubsystem::ComputeFrameInputs(const double realDeltaS, FSOLShipFrameInputs& outInputs) const
{
    outInputs.WarpFactor = FMath::Max(SimClock->GetClock().GetWarpFactor(), 1.0);
    if (mHasFrameHistory)
    {
        outInputs.PrevBodyPositionsM = mPrevBodyPositionsM;
    }

    // Previous state of the active frame: a body's from the per-body history (so a switch between bodies carries with
    // the new body at once), a targetable's only if it was already the frame last time (else no carry, re-seed)
    const int32 referenceIndex = Targeting->GetReferenceIndex();
    const TConstArrayView<FSOLTargetInfo> candidates = Targeting->GetCandidates();
    if (realDeltaS <= 0.0 || !candidates.IsValidIndex(referenceIndex))
    {
        return;
    }
    const FSOLTargetInfo& reference = candidates[referenceIndex];
    FVector3d prevPositionM;
    FVector3d prevVelocityMps;
    if (mHasFrameHistory && mPrevBodyPositionsM.IsValidIndex(referenceIndex)
        && referenceIndex < BodyRegistry->GetUnrealFrameCache().Num())
    {
        prevPositionM = mPrevBodyPositionsM[referenceIndex];
        prevVelocityMps = mPrevBodyVelocitiesMps[referenceIndex];
    }
    else if (referenceIndex == mPrevReferenceIndex)
    {
        prevPositionM = mPrevReferencePositionM;
        prevVelocityMps = mPrevReferenceVelocityMps;
    }
    else
    {
        return;
    }

    // Position: what the frame moved beyond its velocity * real dt. Velocity: the same with the frame's average
    // (sim-time) acceleration, so only the warp-scaled part is carried and real-time gravity supplies the rest
    const FVector3d averageAccelMps2 = (reference.VelocityMps - prevVelocityMps) / (outInputs.WarpFactor * realDeltaS);
    outInputs.CarryPositionM = SOLFlight::FrameCarryDisplacement(prevPositionM, reference.PositionM,
        reference.VelocityMps, realDeltaS);
    outInputs.CarryVelocityMps = SOLFlight::FrameCarryDisplacement(prevVelocityMps, reference.VelocityMps,
        averageAccelMps2, realDeltaS);
}

//////////////////////////////////////////////////////////////////////////
// Remembers this frame's bodies and reference frame as the previous state for the next frame's carry
void USOLShipSubsystem::SaveFrameHistory()
{
    // Copied in place: the arrays were sized for the bodies at initialization
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    const int32 bodyCount = FMath::Min(cache.Num(), mPrevBodyPositionsM.Num());
    for (int32 body = 0; body < bodyCount; ++body)
    {
        mPrevBodyPositionsM[body] = cache.PositionsM[body];
        mPrevBodyVelocitiesMps[body] = cache.VelocitiesMps[body];
    }
    mHasFrameHistory = true;

    // The active frame, for a targetable reference that has no per-body history
    const int32 referenceIndex = Targeting->GetReferenceIndex();
    const TConstArrayView<FSOLTargetInfo> candidates = Targeting->GetCandidates();
    mPrevReferenceIndex = candidates.IsValidIndex(referenceIndex) ? referenceIndex : INDEX_NONE;
    if (mPrevReferenceIndex != INDEX_NONE)
    {
        mPrevReferencePositionM = candidates[referenceIndex].PositionM;
        mPrevReferenceVelocityMps = candidates[referenceIndex].VelocityMps;
    }
}
