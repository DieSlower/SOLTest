/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipSubsystem.h"

#include "Flight/SOLChase.h"
#include "Flight/SOLNewtonianFlight.h"

#include "Game/SOLGameMode.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipFlightProcessor.h"
#include "Ship/SOLShipFragments.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLHudFormat.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Stats/Stats.h"

// Game-thread cycle stats of the ship step (`stat SOLShip`, `stat dumpsum -root=STAT_SOL_`; SDD 32 section 3.5 cost)
DECLARE_STATS_GROUP(TEXT("SOLShip"), STATGROUP_SOLShip, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("SOL_ChasePlan"), STAT_SOL_ChasePlan, STATGROUP_SOLShip);
DECLARE_CYCLE_STAT(TEXT("SOL_ChaseUpdate"), STAT_SOL_ChaseUpdate, STATGROUP_SOLShip);
DECLARE_CYCLE_STAT(TEXT("SOL_ShipFlightRun"), STAT_SOL_ShipFlightRun, STATGROUP_SOLShip);

namespace
{
    //////////////////////////////////////////////////////////////////////////
    // Returns the world's ship subsystem, or null
    USOLShipSubsystem* FindShipSubsystem(const UWorld* world)
    {
        return world != nullptr ? world->GetSubsystem<USOLShipSubsystem>() : nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Chase Down meter floor of the applied safety-margin setting (the default without settings)
    double ChaseFloorMeter(const UObject* worldContext)
    {
        const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(worldContext);
        return SOLChase::EffectiveMeterFloor(settings != nullptr ? settings->GetChaseSafetyMargin()
            : SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a chase sample's tail point (behind a target moving in its frame, else on the approach side)
    FVector3d ChaseSampleAimPointM(const FSOLChaseTargetSample& sample, const FVector3d& approachDir)
    {
        return SOLChase::ChaseAimPointM(sample.PositionM, sample.VelocityMps - sample.FrameVelocityMps, approachDir,
            sample.TrailM);
    }

    // sol.Autopilot <Body|Target|Off>: orders the player ship's autopilot (SDD 18; #19 adds the real controls)
    FAutoConsoleCommandWithWorldAndArgs GShipAutopilotCommand(TEXT("sol.Autopilot"),
        TEXT("sol.Autopilot <Body|Target|Off>: fly the player ship to a body (by name or the selected target), "
            "or stop"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            USOLShipSubsystem* ships = FindShipSubsystem(world);
            const USOLBodyRegistrySubsystem* registry = world != nullptr
                ? world->GetSubsystem<USOLBodyRegistrySubsystem>() : nullptr;
            const USOLTargetingSubsystem* targeting = world != nullptr
                ? world->GetSubsystem<USOLTargetingSubsystem>() : nullptr;
            if (ships == nullptr || registry == nullptr || args.Num() == 0)
            {
                UE_LOG(LogSOL, Warning, TEXT("sol.Autopilot: usage sol.Autopilot <Body|Target|Off>"));
                return;
            }
            if (args[0].Equals(TEXT("Off"), ESearchCase::IgnoreCase))
            {
                ships->DisengageAutopilot();
                return;
            }

            // A body name, or the selected target when it is a body (other targetables are not supported yet)
            int32 bodyIndex = INDEX_NONE;
            if (args[0].Equals(TEXT("Target"), ESearchCase::IgnoreCase))
            {
                const int32 selected = targeting != nullptr ? targeting->GetSelectedIndex() : INDEX_NONE;
                bodyIndex = selected >= 0 && selected < registry->GetUnrealFrameCache().Num() ? selected : INDEX_NONE;
            }
            else
            {
                bodyIndex = registry->GetRegistry().FindByName(FName(*args[0]));
            }
            if (!ships->EngageAutopilot(bodyIndex))
            {
                UE_LOG(LogSOL, Warning, TEXT("sol.Autopilot: no body for '%s'"), *args[0]);
            }
        }));
}

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

    // With the main menu up the ship waits for Play (USOLMenuSubsystem); verification runs start in flight at once
    if (!USOLMenuSubsystem::ShouldStartInMenu())
    {
        SpawnPlayerShip(GetCommandLineStartBody());
    }

    // Verification hook: fly a fixed script without a pawn, log checkpoints, then quit
    if (HasPlayerShip() && FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_FLIGHT))
    {
        // The script owns the controls from here: no pawn input may be latched by its autopilot phase
        mLastPilotInput = FSOLPilotInput();
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
// Returns the -SOLStart=<Body> value, or Earth
FString USOLShipSubsystem::GetCommandLineStartBody()
{
    FString startBody = SOL::BodyNames::EARTH;
    FParse::Value(FCommandLine::Get(), SOL::CommandLine::START_BODY, startBody);
    return startBody;
}

//////////////////////////////////////////////////////////////////////////
// Destroys the player ship entity (Quit to menu) and forgets its surface-lock and frame history
void USOLShipSubsystem::DespawnPlayerShip()
{
    if (!HasPlayerShip())
    {
        return;
    }
    ClearSurfaceLock();
    ResetPilotState();
    mEntityManager->DestroyEntity(mPlayerShip);
    mPlayerShip = FMassEntityHandle();
    mHasFrameHistory = false;
    mPrevReferenceIndex = INDEX_NONE;
    UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: player ship despawned"), *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Creates the player ship entity on a circular orbit above a body and makes it the observer
void USOLShipSubsystem::SpawnPlayerShip(const FString& startBody)
{
    if (!mEntityManager.IsValid() || HasPlayerShip())
    {
        return;
    }

    // Altitude overridable from the command line for verification runs
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
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
    bool bRingStart = false;

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
        bRingStart = true;
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

    // One archetype for ships; the player's carries the player tag and the Chase Down fragment (a player order), and
    // its class's params are a const shared value
    const UScriptStruct* const elements[] = {
        FSOLShipStateFragment::StaticStruct(),
        FSOLShipControlFragment::StaticStruct(),
        FSOLShipChaseFragment::StaticStruct(),
        FSOLPlayerShipTag::StaticStruct(),
    };
    const FMassArchetypeHandle archetype = mEntityManager->CreateArchetype(MakeArrayView(elements));
    FMassArchetypeSharedFragmentValues sharedValues;
    sharedValues.Add(mEntityManager->GetOrCreateConstSharedFragment(FSOLShipParamsFragment::Make(FSOLFlightParams())));
    sharedValues.Sort();
    mPlayerShip = mEntityManager->CreateEntity(archetype, sharedValues);

    FSOLShipControl control;
    // Flight assist starts as the settings say (SDD 8, on by default). A ring-plane verification start flies
    // ballistically so it keeps co-rotating with the ring instead of being braked toward the anchor's frame and
    // drifting out of the plane within seconds
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(this);
    control.bFlightAssist = !bRingStart && (settings == nullptr || settings->GetSavedValues().bDefaultFlightAssist);
    mFlightMode = SOLFlightMode::MakeInitialState(control.bFlightAssist);
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

    // A chase's target from this frame's bodies and targetables, and its plan, before the flight run (SDD 32)
    {
        SCOPE_CYCLE_COUNTER(STAT_SOL_ChaseUpdate);
        UpdateChase(realDeltaSeconds);
    }

    // Time-warp carry and substep inputs, then the flight processor, with the shared command buffer so the context
    // allocates none
    {
        SCOPE_CYCLE_COUNTER(STAT_SOL_ShipFlightRun);
        FSOLShipFrameInputs inputs;
        ComputeFrameInputs(realDeltaSeconds, inputs);
        FlightProcessor->SetFrameInputs(inputs);
        mLastWarpFactor = inputs.WarpFactor;
        UE::Mass::FProcessingContext context(*mEntityManager, realDeltaSeconds);
        context.SetCommandBuffer(mCommandBuffer);
        UE::Mass::Executor::Run(*FlightProcessor, context);
    }
    SaveFrameHistory();

    // The autopilot order ends at arrival; assisted flight resumes from there (SDD 19 section 3.2)
    // A route's fly-through waypoint (SDD 20) keeps AUTO and its order: the route listener (OnShipsStepped, below)
    // sees the arrival count change and orders the next waypoint this frame
    FSOLShipStateFragment& stateFragment = mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip);
    if (stateFragment.bAutopilotArrived)
    {
        ++mAutopilotArrivalCount;
        mLastArrivalOrderSerial = mAutopilotOrderSerial;
        if (mIsAutopilotPassThrough && IsAutopilotPointOrder())
        {
            stateFragment.bAutopilotArrived = false;
        }
        else
        {
            UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: autopilot arrived at %s"), *GetName(),
                *BodyRegistry->GetRegistry().GetName(GetAutopilotBodyIndex()).ToString());
            ClearAutopilotOrder();
            SOLFlightMode::NotifyAutopilotArrived(mFlightMode);
        }
    }

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
// Orders the autopilot (SDD 18) to fly to a point above a body on the ship's current side; false if invalid
bool USOLShipSubsystem::EngageAutopilot(const int32 bodyIndex)
{
    if (!HasPlayerShip() || BodyRegistry == nullptr || bodyIndex < 0
        || bodyIndex >= BodyRegistry->GetUnrealFrameCache().Num())
    {
        return false;
    }

    // Any other order ends a chase (SDD 32 section 3.7); the arrival side stays fixed in the body's (non-rotating)
    // frame: the side the ship is on now
    ClearChaseOrder();
    const FVector3d towardShip = GetState().PositionM - BodyRegistry->GetUnrealFrameCache().PositionsM[bodyIndex];
    FSOLShipControlFragment& control = mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip);
    control.AutopilotBodyIndex = bodyIndex;
    control.AutopilotOffsetDir = towardShip.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d(0.0, 0.0, 1.0));
    control.bAutopilotPoint = false;
    mIsAutopilotPassThrough = false;
    ++mAutopilotOrderSerial;
    SOLFlightMode::EngageAuto(mFlightMode, mLastPilotInput);
    UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: autopilot engaged to %s (%.0f km)%s"), *GetName(),
        *BodyRegistry->GetRegistry().GetName(bodyIndex).ToString(), towardShip.Size() / SOL::METERS_PER_KM,
        mFlightMode.LatchedChannels != 0 ? TEXT("; held input latched") : TEXT(""));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Orders the autopilot to a route waypoint; enters AUTO (latching held input) unless already in it
bool USOLShipSubsystem::EngageAutopilotPoint(const FSOLAutopilotPointOrder& order)
{
    if (!HasPlayerShip() || BodyRegistry == nullptr || order.BodyIndex < 0
        || order.BodyIndex >= BodyRegistry->GetUnrealFrameCache().Num() || !FMath::IsFinite(order.OffsetM.X)
        || !FMath::IsFinite(order.OffsetM.Y) || !FMath::IsFinite(order.OffsetM.Z)
        || !FMath::IsFinite(order.PassSpeedMps) || !FMath::IsFinite(order.CaptureRadiusM))
    {
        return false;
    }

    // A route start or Resume replaces a chase (SDD 32 section 3.7)
    ClearChaseOrder();
    FSOLShipControlFragment& control = mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip);
    control.AutopilotBodyIndex = order.BodyIndex;
    control.AutopilotPointOffsetM = order.OffsetM;
    control.AutopilotPassSpeedMps = FMath::Max(order.PassSpeedMps, 0.0);
    control.AutopilotCaptureRadiusM = FMath::Max(order.CaptureRadiusM, 0.0);
    control.bAutopilotPoint = true;
    mIsAutopilotPassThrough = order.bStayAutoOnArrival;
    ++mAutopilotOrderSerial;
    mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip).bAutopilotArrived = false;
    if (!SOLFlightMode::IsAuto(mFlightMode))
    {
        SOLFlightMode::EngageAuto(mFlightMode, mLastPilotInput);
    }
    UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: autopilot to a waypoint at %s (offset %.0f km, pass %.0f m/s, capture "
        "%.0f m)%s"), *GetName(), *BodyRegistry->GetRegistry().GetName(order.BodyIndex).ToString(),
        order.OffsetM.Size() / SOL::METERS_PER_KM, control.AutopilotPassSpeedMps, control.AutopilotCaptureRadiusM,
        mFlightMode.LatchedChannels != 0 ? TEXT("; held input latched") : TEXT(""));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the current autopilot order is a route waypoint (false without an order)
bool USOLShipSubsystem::IsAutopilotPointOrder() const
{
    if (!HasPlayerShip())
    {
        return false;
    }
    const FSOLShipControlFragment& control =
        mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip);
    return control.AutopilotBodyIndex != INDEX_NONE && control.bAutopilotPoint;
}

//////////////////////////////////////////////////////////////////////////
// Starts a Chase Down on a body or a targetable, replacing any autopilot order; false (nothing changed) if invalid
bool USOLShipSubsystem::EngageChase(const FSOLChaseOrder& order)
{
    if (!HasPlayerShip() || BodyRegistry == nullptr)
    {
        return false;
    }
    FSOLChaseOrderState chase;
    chase.Order = order;
    if (!ResolveChaseTarget(0.0, chase))
    {
        UE_LOG(LogSOL, Warning, TEXT("ShipSubsystem %s: chase not started (no live target)"), *GetName());
        return false;
    }

    // The previous order (autopilot or chase) ends first; the tail side is the side the ship is on now, fixed in the
    // target's non-rotating frame (as Move To and Follow)
    ClearAutopilotOrder();
    const FSOLShipState ship = GetState();
    const FVector3d towardShip = ship.PositionM - chase.Sample.PositionM;
    chase.ApproachDir = towardShip.GetSafeNormal(UE_DOUBLE_SMALL_NUMBER, FVector3d(0.0, 0.0, 1.0));
    chase.bActive = true;
    mChase = chase;
    mIsChaseTargetLost = false;
    FSOLShipChaseFragment& chaseFragment = mEntityManager->GetFragmentDataChecked<FSOLShipChaseFragment>(mPlayerShip);
    chaseFragment.ChaseRun = FSOLChaseRunState();
    chaseFragment.ChaseLimit = FSOLChaseGLimit();
    mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip).bAutopilotArrived = false;

    // A new order serial (an active route pauses, SDD 20), AUTO with the held input latched, then the first plan
    ++mAutopilotOrderSerial;
    SOLFlightMode::EngageAuto(mFlightMode, mLastPilotInput);
    WriteChaseControl();
    ReplanChase();
    UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: chase engaged on %s (%.1f km, trail %.0f m): burst %.2f G, "
        "brake %.2f G, floor %.2f, predicted %.1f s%s"), *GetName(), *chase.Sample.Name.ToString(),
        towardShip.Size() / SOL::METERS_PER_KM, chase.Sample.TrailM, mChase.Plan.Policy.BurstG,
        mChase.Plan.Policy.BrakePlanG, mChase.Plan.Policy.FloorMeter, mChase.Plan.Prediction.TimeS,
        mFlightMode.LatchedChannels != 0 ? TEXT("; held input latched") : TEXT(""));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Ends a running chase in assisted flight; does nothing when no chase runs
void USOLShipSubsystem::DisengageChase()
{
    if (mChase.bActive)
    {
        DisengageAutopilot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the chase's HUD snapshot
FSOLChaseStatus USOLShipSubsystem::GetChaseStatus() const
{
    FSOLChaseStatus status;
    status.bTargetLost = mIsChaseTargetLost;
    status.TargetName = mChase.Sample.Name;
    if (!mChase.bActive || !HasPlayerShip())
    {
        return status;
    }
    const FSOLShipStateFragment& state = mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip);
    const FSOLShipChaseFragment& chase = mEntityManager->GetFragmentDataChecked<FSOLShipChaseFragment>(mPlayerShip);
    status.bActive = true;
    status.TargetDistanceM = FVector3d::Dist(state.State.PositionM, mChase.Sample.PositionM);
    status.AimDistanceM = state.Guidance.DistanceM;
    status.ClosingSpeedMps = state.Guidance.ClosingSpeedMps;
    status.bHolding = chase.ChaseLimit.Activity == ESOLChaseActivity::Hold;
    status.Activity = chase.ChaseLimit.Activity;
    status.LimitG = chase.ChaseLimit.LimitG;
    status.ResumeMeter = chase.ChaseLimit.ResumeMeter;
    status.bCooling = chase.ChaseRun.bCooling;
    status.FloorMeter = chase.ChasePolicy.FloorMeter;
    status.BurstG = chase.ChasePolicy.BurstG;
    status.BrakePlanG = chase.ChasePolicy.BrakePlanG;
    status.TrailM = mChase.Sample.TrailM;

    // The plan's prediction counts down in sim time until the next plan replaces it
    const FSOLChasePrediction& prediction = mChase.Plan.Prediction;
    status.TimeToInterceptS = status.bHolding ? 0.0 : (prediction.bIntercepts
        ? FMath::Max(prediction.TimeS - mChase.SinceReplanSimS, 0.0) : DBL_MAX);
    return status;
}

//////////////////////////////////////////////////////////////////////////
// Ends the chase order (fragment, run state and plan), without touching the flight mode or the autopilot order
void USOLShipSubsystem::ClearChaseOrder()
{
    const bool bWasActive = mChase.bActive;
    mChase.bActive = false;
    mChase.Plan = FSOLChasePlan();
    if (HasPlayerShip())
    {
        FSOLShipChaseFragment& chase = mEntityManager->GetFragmentDataChecked<FSOLShipChaseFragment>(mPlayerShip);
        chase.bChase = false;
        chase.ChaseFrameBodyIndex = INDEX_NONE;
        if (bWasActive)
        {
            UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: chase on %s ended (last %s %.2f G, meter %.2f)"), *GetName(),
                *mChase.Sample.Name.ToString(), SOLHudFormat::ChaseActivityName(chase.ChaseLimit.Activity),
                chase.ChaseLimit.LimitG, GetGMeter().Meter);
        }
        chase.ChaseRun = FSOLChaseRunState();
        chase.ChaseLimit = FSOLChaseGLimit();
    }
}

//////////////////////////////////////////////////////////////////////////
// Resolves a chase's source into its Sample for this frame; false when the source is invalid or gone
bool USOLShipSubsystem::ResolveChaseTarget(const double simDeltaS, FSOLChaseOrderState& inOutChase) const
{
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    const FSOLFlightParams params = GetFlightParams();
    FSOLChaseTargetSample& sample = inOutChase.Sample;
    const FName lastName = sample.Name;
    sample = FSOLChaseTargetSample();
    sample.Name = lastName;

    // A body: the cache's position and velocity; it is its own frame (its acceleration, the other bodies' gravity at
    // its centre, is added per substep by the flight processor). The tail is holdable at the sustained limit
    const int32 bodyIndex = inOutChase.Order.BodyIndex;
    if (bodyIndex != INDEX_NONE)
    {
        if (!cache.PositionsM.IsValidIndex(bodyIndex))
        {
            return false;
        }
        const double radiusM = cache.RadiiM[bodyIndex];
        sample.Name = BodyRegistry->GetRegistry().GetName(bodyIndex);
        sample.PositionM = cache.PositionsM[bodyIndex];
        sample.VelocityMps = cache.VelocitiesMps[bodyIndex];
        sample.FrameVelocityMps = sample.VelocityMps;
        sample.FrameBodyIndex = bodyIndex;
        sample.RadiusM = radiusM;
        sample.TrailM = SOLChase::ChaseTrailM(radiusM, SOLFlight::AutopilotStandoffM(radiusM, cache.GMs[bodyIndex],
            SOLNewtonian::PilotSustainedAccelMps2(params.PilotLimits)), SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR);
        sample.bAlive = true;
        return true;
    }

    // A targetable: its snapshot, the velocity of the body it moves with (or its own when it has none)
    const ISOLTargetable* targetable = inOutChase.Order.Targetable.Get();
    if (targetable == nullptr || !targetable->IsTargetAlive())
    {
        return false;
    }
    const FSOLTargetInfo info = targetable->GetTargetInfo();
    const int32 frameBody = targetable->GetTargetFrameBodyIndex();
    sample.Name = info.Name;
    sample.PositionM = info.PositionM;
    sample.VelocityMps = info.VelocityMps;
    sample.FrameBodyIndex = cache.PositionsM.IsValidIndex(frameBody) ? frameBody : INDEX_NONE;
    sample.FrameVelocityMps = sample.FrameBodyIndex != INDEX_NONE ? cache.VelocitiesMps[frameBody] : info.VelocityMps;
    sample.RadiusM = info.RadiusM;
    sample.TrailM = SOLChase::ChaseTrailM(info.RadiusM, SOL::Chase::TARGET_TRAIL_M,
        SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR);

    // Own acceleration from the change of its velocity relative to its frame body (absolute without one), in sim
    // seconds, clamped to twice the boosted chase cap; a new frame body restarts the estimate
    const FVector3d ownVelocityMps = sample.FrameBodyIndex != INDEX_NONE ? info.VelocityMps - sample.FrameVelocityMps
        : info.VelocityMps;
    if (inOutChase.bHasPrevVelocity && inOutChase.PrevFrameBodyIndex == sample.FrameBodyIndex)
    {
        const double capMps2 = 2.0 * SOLChase::ChaseMaxG(params.Envelope, params.PilotLimits, true)
            * SOLNewtonian::STANDARD_GRAVITY_MPS2;
        inOutChase.AccelEstimateMps2 = SOLChase::EstimateTargetAccelMps2(inOutChase.PrevRelVelocityMps,
            ownVelocityMps, inOutChase.AccelEstimateMps2, simDeltaS, capMps2);
    }
    else
    {
        inOutChase.AccelEstimateMps2 = FVector3d::ZeroVector;
    }
    inOutChase.PrevRelVelocityMps = ownVelocityMps;
    inOutChase.PrevFrameBodyIndex = sample.FrameBodyIndex;
    inOutChase.bHasPrevVelocity = true;
    sample.AccelMps2 = inOutChase.AccelEstimateMps2;
    sample.bAlive = true;
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Per step before the flight run: resolves the target (a lost one ends the chase), writes the inputs and replans
void USOLShipSubsystem::UpdateChase(const double realDeltaS)
{
    if (!mChase.bActive || !HasPlayerShip())
    {
        return;
    }
    const double simDeltaS = FMath::Max(SimClock->GetClock().GetWarpFactor(), 1.0) * realDeltaS;
    if (!ResolveChaseTarget(simDeltaS, mChase))
    {
        // Target lost (destroyed, evicted, its slot reused, or the object gone): ASSISTED, and the HUD note
        UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: chase target %s lost; ASSISTED"), *GetName(),
            *mChase.Sample.Name.ToString());
        ClearAutopilotOrder();
        SOLFlightMode::DisengageAuto(mFlightMode);
        mIsChaseTargetLost = true;
        ++mChaseTargetLostCount;
        mChaseTargetLostRealS = GetWorld() != nullptr ? GetWorld()->GetRealTimeSeconds() : 0.0;
        return;
    }
    WriteChaseControl();

    // Replan triggers (SDD 32 sections 3.5 and 3.8): the real-time interval, the sim-time cap under warp, a new floor
    // or boost toggle, the meter leaving the plan's predicted band, and a target velocity change
    mChase.SinceReplanRealS += realDeltaS;
    mChase.SinceReplanSimS += simDeltaS;
    mChase.ShipLagS = simDeltaS;
    const FSOLShipChaseFragment& chase = mEntityManager->GetFragmentDataChecked<FSOLShipChaseFragment>(mPlayerShip);
    const bool bBoost = GetControl().bBoost;
    const double meter = GetGMeter().Meter;
    const FSOLChasePrediction& prediction = mChase.Plan.Prediction;
    const bool bMeterOff = prediction.bIntercepts && (meter < prediction.MinMeter - SOL::Chase::REPLAN_METER_DELTA
        || meter > FMath::Max(mChase.PlanStartMeter, prediction.EndMeter) + SOL::Chase::REPLAN_METER_DELTA);
    // The target's velocity relative to its frame (as the aim point uses it), so the frame's own orbit (the Sun's pull
    // on the Earth frame under warp) does not count as the target turning
    const FVector3d targetRelVelocityMps = mChase.Sample.VelocityMps - mChase.Sample.FrameVelocityMps;
    const double targetDvMps = FVector3d::Dist(targetRelVelocityMps, mChase.PlanTargetRelVelocityMps);
    const bool bTargetTurned = targetDvMps > FMath::Max(SOL::Chase::REPLAN_TARGET_DV_MIN_MPS,
        SOL::Chase::REPLAN_TARGET_DV_FRACTION * FMath::Abs(mChase.PlanClosingSpeedMps));
    if (mChase.SinceReplanRealS >= SOL::Chase::REPLAN_INTERVAL_S
        || mChase.SinceReplanSimS >= SOL::Chase::REPLAN_MAX_SIM_S
        || chase.ChasePolicy.FloorMeter != mChase.PlanFloorMeter || bBoost != mChase.bPlanBoost
        || bMeterOff || bTargetTurned)
    {
        ReplanChase();
    }
}

//////////////////////////////////////////////////////////////////////////
// Writes this frame's target, tail and planning gravity to the control fragment
void USOLShipSubsystem::WriteChaseControl()
{
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    const FSOLChaseTargetSample& sample = mChase.Sample;
    FSOLShipChaseFragment& chase = mEntityManager->GetFragmentDataChecked<FSOLShipChaseFragment>(mPlayerShip);
    chase.bChase = true;
    chase.ChaseFrameBodyIndex = sample.FrameBodyIndex;
    chase.ChaseTargetM = sample.PositionM;
    chase.ChaseTargetVelocityMps = sample.VelocityMps;
    chase.ChaseFrameVelocityMps = sample.FrameVelocityMps;
    chase.ChaseTargetAccelMps2 = sample.AccelMps2;
    chase.ChaseApproachDir = mChase.ApproachDir;
    chase.ChaseTrailM = sample.TrailM;
    chase.ChasePlanGravityMps2 = SOLFlight::GravityAcceleration(ChaseSampleAimPointM(sample, mChase.ApproachDir),
        cache.PositionsM, cache.GMs, cache.RadiiM).Size();

    // The safety clamp follows an applied margin at once (SDD 32 section 3.10); the levels change only at a replan
    chase.ChasePolicy = mChase.Plan.Policy;
    chase.ChasePolicy.FloorMeter = ChaseFloorMeter(this);
}

//////////////////////////////////////////////////////////////////////////
// Plans the chase from the ship's current state; the new plan replaces the current one per ShouldReplacePlan
void USOLShipSubsystem::ReplanChase()
{
    const FSOLBodyFrameCache& cache = BodyRegistry->GetUnrealFrameCache();
    const FSOLShipState ship = GetState();
    const FSOLFlightParams params = GetFlightParams();
    FSOLShipChaseFragment& chase = mEntityManager->GetFragmentDataChecked<FSOLShipChaseFragment>(mPlayerShip);
    const FSOLChaseTargetSample& sample = mChase.Sample;

    // The target where it was at the ship's state time: before the flight run the ship is still where last frame's
    // bodies left it, and this frame's bodies (and the sample) moved on by ShipLagS (warp x the frame: 500 m for an
    // Earth-frame target at 60 fps, hundreds of km under warp). Its frame body's last position is exact; else its
    // velocity backs it off
    FVector3d targetM = sample.PositionM;
    const int32 frameBody = sample.FrameBodyIndex;
    if (mChase.ShipLagS > 0.0)
    {
        const FVector3d ownVelocityMps = sample.VelocityMps - sample.FrameVelocityMps;
        const bool bFrameHistory = mHasFrameHistory && mPrevBodyPositionsM.IsValidIndex(frameBody)
            && cache.PositionsM.IsValidIndex(frameBody);
        targetM -= bFrameHistory
            ? cache.PositionsM[frameBody] - mPrevBodyPositionsM[frameBody] + ownVelocityMps * mChase.ShipLagS
            : sample.VelocityMps * mChase.ShipLagS;
    }

    // The 1-D chase along the line of sight to the tail, in the target's frame (closing > 0); at the tail itself any
    // relative motion recedes
    const FVector3d toAimM = SOLChase::ChaseAimPointM(targetM, sample.VelocityMps - sample.FrameVelocityMps,
        mChase.ApproachDir, sample.TrailM) - ship.PositionM;
    const FVector3d relativeVelocityMps = ship.VelocityMps - sample.VelocityMps;
    const double distanceM = toAimM.Size();
    FSOLChasePlanInput input;
    input.DistanceM = distanceM;
    input.ClosingSpeedMps = distanceM > UE_DOUBLE_SMALL_NUMBER
        ? FVector3d::DotProduct(relativeVelocityMps, toAimM / distanceM) : -relativeVelocityMps.Size();
    input.GravityMps2 = FMath::Max(SOLFlight::GravityAcceleration(ship.PositionM, cache.PositionsM, cache.GMs,
        cache.RadiiM).Size(), chase.ChasePlanGravityMps2);
    input.Envelope = params.Envelope;
    input.Limits = params.PilotLimits;
    input.Meter = GetGMeter();
    input.FloorMeter = chase.ChasePolicy.FloorMeter;
    input.bBoost = GetControl().bBoost;
    // The plan alone under its cycle stat (the SDD 32 section 3.5 budget: 0.1 ms)
    const FSOLChasePlan candidate = [&input]()
    {
        SCOPE_CYCLE_COUNTER(STAT_SOL_ChasePlan);
        return SOLChase::PlanChase(input);
    }();

    // The current policy is re-predicted from now (its old prediction counted from its own plan time), and is no
    // longer feasible when it would go under the floor or bursts above a cap lowered by the boost toggle
    FSOLChasePlan current = mChase.Plan;
    if (current.bValid)
    {
        current.Policy.FloorMeter = input.FloorMeter;
        current.Prediction = SOLChase::PredictChase(input, current.Policy);
        current.bValid = current.Prediction.bIntercepts
            && current.Policy.BurstG <= SOLChase::ChaseMaxG(input.Envelope, input.Limits, input.bBoost)
                + SOL::Chase::REPLAN_FEASIBILITY_TOLERANCE
            && current.Prediction.MinMeter >= FMath::Min(input.FloorMeter, input.Meter.Meter)
                - SOL::Chase::REPLAN_FEASIBILITY_TOLERANCE;
    }
    const bool bReplace = SOLChase::ShouldReplacePlan(current, candidate) || !current.bValid;
    const bool bLevelsChanged = bReplace && (candidate.Policy.BurstG != mChase.Plan.Policy.BurstG
        || candidate.Policy.BrakePlanG != mChase.Plan.Policy.BrakePlanG);
    mChase.Plan = bReplace ? candidate : current;
    mChase.Plan.Policy.FloorMeter = input.FloorMeter;
    if (bLevelsChanged && mChase.PlanFloorMeter > 0.0)
    {
        UE_LOG(LogSOL, Verbose, TEXT("ShipSubsystem %s: chase replanned: burst %.2f G, brake %.2f G, predicted %.1f s "
            "(%.1f km, closing %.0f m/s, meter %.2f)"), *GetName(), mChase.Plan.Policy.BurstG,
            mChase.Plan.Policy.BrakePlanG, mChase.Plan.Prediction.TimeS, distanceM / SOL::METERS_PER_KM,
            input.ClosingSpeedMps, input.Meter.Meter);
    }

    // What the triggers compare against until the next plan
    mChase.PlanTargetRelVelocityMps = sample.VelocityMps - sample.FrameVelocityMps;
    mChase.PlanClosingSpeedMps = input.ClosingSpeedMps;
    mChase.PlanStartMeter = input.Meter.Meter;
    mChase.PlanFloorMeter = input.FloorMeter;
    mChase.bPlanBoost = input.bBoost;
    mChase.SinceReplanRealS = 0.0;
    mChase.SinceReplanSimS = 0.0;
    chase.ChasePolicy = mChase.Plan.Policy;
}

//////////////////////////////////////////////////////////////////////////
// Cancels the autopilot order, if any, and hands back to assisted flight
void USOLShipSubsystem::DisengageAutopilot()
{
    ClearAutopilotOrder();
    SOLFlightMode::DisengageAuto(mFlightMode);
}

//////////////////////////////////////////////////////////////////////////
// Toggles assisted / manual flight; in AUTO, takes control back in assisted flight
bool USOLShipSubsystem::ToggleAssist()
{
    const bool bWasAuto = SOLFlightMode::IsAuto(mFlightMode);
    if (!SOLFlightMode::ToggleAssist(mFlightMode, PilotHasControl()))
    {
        UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: assist toggle ignored (no pilot control)"), *GetName());
        return false;
    }
    if (bWasAuto)
    {
        ClearAutopilotOrder();
    }
    UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: flight mode %s"), *GetName(),
        SOLFlightMode::FlightAssistFor(mFlightMode.Mode) ? TEXT("ASSISTED") : TEXT("MANUAL"));
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Applies the cancel-on-manual-input rule to the frame's gated pilot input
bool USOLShipSubsystem::ApplyPilotInput(const FSOLPilotInput& input)
{
    mLastPilotInput = input;

    // Keys flushed at a close read as released for a frame or two before their OS repeat; the relatched channels are
    // re-applied meanwhile so that gap cannot clear them
    if (mRelatchFramesLeft > 0)
    {
        --mRelatchFramesLeft;
        SOLFlightMode::RelatchInput(mFlightMode, mRelatchInput);
    }
    if (!SOLFlightMode::ApplyPilotInput(mFlightMode, input, PilotHasControl()))
    {
        return false;
    }
    UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: manual input took control (channels 0x%04x); autopilot cancelled, "
        "ASSISTED"), *GetName(), SOLFlightMode::ActiveChannels(input));
    ClearAutopilotOrder();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// In AUTO, latches the channels of input held when the pawn's control comes back
void USOLShipSubsystem::RelatchPilotInput(const FSOLPilotInput& heldInput)
{
    const uint16 before = mFlightMode.LatchedChannels;
    SOLFlightMode::RelatchInput(mFlightMode, heldInput);
    mRelatchInput = heldInput;
    mRelatchFramesLeft = SOLFlightMode::IsAuto(mFlightMode) ? SOL::SHIP_RELATCH_GRACE_FRAMES : 0;
    if (mFlightMode.LatchedChannels != before)
    {
        UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: control restored in AUTO; held input latched (0x%04x)"),
            *GetName(), mFlightMode.LatchedChannels);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether the pilot has control (not blacked out) as of the last step
bool USOLShipSubsystem::PilotHasControl() const
{
    return !HasPlayerShip() || SOLNewtonian::PilotHasControl(GetGMeter());
}

//////////////////////////////////////////////////////////////////////////
// Clears the autopilot order fragment and guidance, and any chase, without touching the flight mode
void USOLShipSubsystem::ClearAutopilotOrder()
{
    ClearChaseOrder();
    if (!HasPlayerShip())
    {
        return;
    }
    FSOLShipControlFragment& control = mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip);
    if (control.AutopilotBodyIndex != INDEX_NONE)
    {
        UE_LOG(LogSOL, Log, TEXT("ShipSubsystem %s: autopilot disengaged"), *GetName());
    }
    control.AutopilotBodyIndex = INDEX_NONE;
    control.bAutopilotPoint = false;
    mIsAutopilotPassThrough = false;
    FSOLShipStateFragment& state = mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip);
    state.Guidance = FSOLGuidanceOutput();
    state.bAutopilotArrived = false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the body the autopilot flies to, or INDEX_NONE
int32 USOLShipSubsystem::GetAutopilotBodyIndex() const
{
    return HasPlayerShip()
        ? mEntityManager->GetFragmentDataChecked<FSOLShipControlFragment>(mPlayerShip).AutopilotBodyIndex : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the autopilot guidance of the last step (ETA, phase, distance); default when not engaged
FSOLGuidanceOutput USOLShipSubsystem::GetAutopilotGuidance() const
{
    return HasPlayerShip() ? mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip).Guidance
        : FSOLGuidanceOutput();
}

//////////////////////////////////////////////////////////////////////////
// Returns the pilot's G meter state as of the last step
FSOLGMeterState USOLShipSubsystem::GetGMeter() const
{
    return HasPlayerShip() ? mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip).GMeter
        : FSOLGMeterState();
}

//////////////////////////////////////////////////////////////////////////
// Returns the pilot's G-load (thrust plus surface contact) as of the last step
double USOLShipSubsystem::GetGLoadG() const
{
    return HasPlayerShip() ? mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip).GLoadG : 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the autopilot flies under a warp too high for precise gravity near a body (HUD hint)
bool USOLShipSubsystem::IsAutopilotPrecisionReduced() const
{
    if (GetAutopilotBodyIndex() == INDEX_NONE)
    {
        return false;
    }
    double altitudeM = 0.0;
    const int32 nearest = FindNearestBody(altitudeM);
    if (nearest == INDEX_NONE)
    {
        return false;
    }
    const double radiusM = BodyRegistry->GetUnrealFrameCache().RadiiM[nearest];
    return SOLFlight::IsAutopilotPrecisionReduced(mLastWarpFactor, altitudeM + radiusM, radiusM);
}

//////////////////////////////////////////////////////////////////////////
// Cancels the autopilot order and wakes the pilot with a full G meter (a jump arrival or a spawn: SDD 18 Amendment 7)
void USOLShipSubsystem::ResetPilotState()
{
    DisengageAutopilot();
    ++mPilotResetCount;
    if (HasPlayerShip())
    {
        FSOLShipStateFragment& state = mEntityManager->GetFragmentDataChecked<FSOLShipStateFragment>(mPlayerShip);
        state.GMeter = FSOLGMeterState();
        state.GLoadG = 0.0;
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

    // The flight processor flies the ship's up toward the locked body each substep, as an assisted attitude target
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
