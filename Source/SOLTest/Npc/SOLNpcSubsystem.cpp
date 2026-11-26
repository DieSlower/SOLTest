/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcSubsystem.h"

#include "Game/SOLGameMode.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "MinorBodies/SOLAsteroidBelt.h"
#include "Npc/SOLLaneNodeTable.h"
#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcFragments.h"
#include "Npc/SOLNpcFullAiProcessor.h"
#include "Npc/SOLNpcMidProcessor.h"
#include "Npc/SOLNpcStatsText.h"
#include "Npc/SOLNpcStatsWidget.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Ship/SOLShipSubsystem.h"
#include "Traffic/SOLLaneTrafficSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"
#include "Visuals/SOLThrusterMath.h"
#include "Visuals/SOLThrusterRibbons.h"

#include "Algo/BinarySearch.h"
#include "Algo/Count.h"
#include "Async/ParallelFor.h"
#include "Async/TaskGraphInterfaces.h"
#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "MassCommandBuffer.h"
#include "MassEntityManager.h"
#include "MassEntityQuery.h"
#include "MassEntityView.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "DynamicRHI.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "RenderTimer.h"

// Insights counters for the tier populations (SDD 11 section 3.7)
TRACE_DECLARE_INT_COUNTER(NpcFull, TEXT("SOL/Npc/Full"));
TRACE_DECLARE_INT_COUNTER(NpcMid, TEXT("SOL/Npc/Mid"));
TRACE_DECLARE_INT_COUNTER(NpcCandidates, TEXT("SOL/Npc/Candidates"));

namespace
{
    // Ships per ParallelFor block when building rails
    constexpr int32 NPC_SPAWN_BLOCK = 4096;

    // Bytes per megabyte, for the memory figures
    constexpr double NPC_BYTES_PER_MB = 1024.0 * 1024.0;

    // Parent-chain depth limit of the lane nominal length walk (Sun - planet - moon is 2)
    constexpr int32 NPC_LANE_CHAIN_MAX_DEPTH = 8;

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane's nominal (maximum) length (SDD 50 D16): the sum of the apoapsis distances a (1 + e) along each
    // body's parent chain up to their nearest common ancestor (the two aphelia for two planets, the moon's apoapsis for a
    // planet - moon lane), from the secular elements at the given centuries since J2000; 0 for bodies with no common
    // ancestor
    double NpcLaneNominalLengthM(const FSOLBodyRegistry& registry, const int32 bodyA, const int32 bodyB,
        const double centuries)
    {
        // A's ancestors (itself first) with the apoapsis sum from A up to each
        int32 chainBody[NPC_LANE_CHAIN_MAX_DEPTH + 1];
        double chainSumM[NPC_LANE_CHAIN_MAX_DEPTH + 1];
        int32 chainCount = 0;
        double sumM = 0.0;
        for (int32 body = bodyA; body != INDEX_NONE && chainCount <= NPC_LANE_CHAIN_MAX_DEPTH;
            body = registry.GetParent(body))
        {
            chainBody[chainCount] = body;
            chainSumM[chainCount] = sumM;
            ++chainCount;
            if (registry.GetParent(body) != INDEX_NONE)
            {
                const FSOLKeplerElements elements = registry.GetElements(body).AtCenturies(centuries);
                sumM += elements.SemiMajorAxisM * (1.0 + elements.Eccentricity);
            }
        }

        // Walk up from B until the chain is met
        sumM = 0.0;
        int32 body = bodyB;
        for (int32 depth = 0; body != INDEX_NONE && depth <= NPC_LANE_CHAIN_MAX_DEPTH; ++depth)
        {
            for (int32 link = 0; link < chainCount; ++link)
            {
                if (chainBody[link] == body)
                {
                    return sumM + chainSumM[link];
                }
            }
            if (registry.GetParent(body) == INDEX_NONE)
            {
                break;
            }
            const FSOLKeplerElements elements = registry.GetElements(body).AtCenturies(centuries);
            sumM += elements.SemiMajorAxisM * (1.0 + elements.Eccentricity);
            body = registry.GetParent(body);
        }
        return 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the NPC subsystem of a world, or nullptr
    USOLNpcSubsystem* FindNpcSubsystem(UWorld* world)
    {
        return world != nullptr ? world->GetSubsystem<USOLNpcSubsystem>() : nullptr;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the process's used physical memory in MB
    double UsedPhysicalMB()
    {
        return static_cast<double>(FPlatformMemory::GetStats().UsedPhysical) / NPC_BYTES_PER_MB;
    }

    // sol.Npc.Count <n>: (re)spawns n NPC ships ("250k", "1M" accepted)
    FAutoConsoleCommandWithWorldAndArgs GNpcCountCommand(TEXT("sol.Npc.Count"),
        TEXT("sol.Npc.Count <n>: spawn n NPC ships (250000, 250k, 1M)"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            USOLNpcSubsystem* npcs = FindNpcSubsystem(world);
            const int32 count = args.Num() > 0 ? SOLNpcScale::ParseNpcCount(args[0]) : INDEX_NONE;
            if (npcs != nullptr && count != INDEX_NONE)
            {
                npcs->RequestNpcCount(count);
            }
        }));

    // sol.Npc.Ramp [max]: runs the stress ramp up to max (default the maximum)
    FAutoConsoleCommandWithWorldAndArgs GNpcRampCommand(TEXT("sol.Npc.Ramp"),
        TEXT("sol.Npc.Ramp [max]: run the NPC stress ramp up to max ships"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            USOLNpcSubsystem* npcs = FindNpcSubsystem(world);
            const int32 parsed = args.Num() > 0 ? SOLNpcScale::ParseNpcCount(args[0]) : SOL::NPC_MAX_COUNT;
            if (npcs != nullptr && parsed != INDEX_NONE)
            {
                npcs->StartRamp(parsed, false);
            }
        }));

    // sol.Npc.FaceBattle [index]: turns the player ship toward a battle (verification screenshots)
    FAutoConsoleCommandWithWorldAndArgs GNpcFaceBattleCommand(TEXT("sol.Npc.FaceBattle"),
        TEXT("sol.Npc.FaceBattle [index]: turn the player ship toward an NPC battle"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            if (USOLNpcSubsystem* npcs = FindNpcSubsystem(world))
            {
                npcs->FaceBattle(args.Num() > 0 ? FCString::Atoi(*args[0]) : 0);
            }
        }));

    // sol.Npc.CombatModel: how NPC shots are resolved (SDD 11 section 3.6, open question 11)
    TAutoConsoleVariable<int32> CVarNpcCombatModel(TEXT("sol.Npc.CombatModel"), SOL::NPC_COMBAT_DEFAULT_MODEL,
        TEXT("NPC combat model: 0 bolts (every shot a swept bolt), 1 statistical (rolled hits, tracers near the ")
        TEXT("camera), 2 hybrid (bolts near the camera, statistical beyond)"));

    // sol.Npc.Combat: NPC-vs-NPC combat on or off (Full ships still steer)
    TAutoConsoleVariable<int32> CVarNpcCombat(TEXT("sol.Npc.Combat"), 1,
        TEXT("NPC-vs-NPC combat: 1 on, 0 off"));

    // sol.Npc.BattleShips <n>: sets the battle ship count and respawns the population
    FAutoConsoleCommandWithWorldAndArgs GNpcBattleShipsCommand(TEXT("sol.Npc.BattleShips"),
        TEXT("sol.Npc.BattleShips <n>: NPC battle ships near the player (respawns the population)"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            USOLNpcSubsystem* npcs = FindNpcSubsystem(world);
            const int32 count = args.Num() > 0 ? SOLNpcScale::ParseNpcCount(args[0]) : INDEX_NONE;
            if (npcs != nullptr && count != INDEX_NONE)
            {
                npcs->SetBattleShipCount(count);
            }
        }));

    // Console names of the demo options (SOLNpcScale::EDemoOption order), for sol.Npc.Option
    const TCHAR* const NPC_OPTION_NAMES[] =
    {
        TEXT("NpcCount"), TEXT("LaneFraction"), TEXT("BattleShipCount"), TEXT("BattleCount"), TEXT("BattleRadiusM"),
        TEXT("BattleMinDistanceM"), TEXT("BattleMaxDistanceM"), TEXT("FullAiRadiusM"), TEXT("MidAiRadiusM"),
        TEXT("FullAiBudget"), TEXT("MidAiBudget"), TEXT("AiHysteresis"), TEXT("MeshRadiusM"), TEXT("InstancedRadiusM"),
        TEXT("MeshBudget"), TEXT("InstancedBudget"), TEXT("RenderHysteresis"), TEXT("NpcCombat"), TEXT("ShowStats"),
        TEXT("RampSettleS"), TEXT("RampMeasureS"),
    };
    static_assert(UE_ARRAY_COUNT(NPC_OPTION_NAMES) == static_cast<int32>(SOLNpcScale::EDemoOption::Count),
        "one console name per demo option");

    // sol.Npc.Option <name> <value>: sets one demo option (as the Demo page would) and respawns a live population
    FAutoConsoleCommandWithWorldAndArgs GNpcOptionCommand(TEXT("sol.Npc.Option"),
        TEXT("sol.Npc.Option <name> <value>: set a demo option (e.g. BattleMinDistanceM 25000) and respawn"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            USOLNpcSubsystem* npcs = FindNpcSubsystem(world);
            if (npcs == nullptr || args.Num() < 2)
            {
                return;
            }
            for (int32 option = 0; option < UE_ARRAY_COUNT(NPC_OPTION_NAMES); ++option)
            {
                if (args[0].Equals(NPC_OPTION_NAMES[option], ESearchCase::IgnoreCase))
                {
                    npcs->SetOptionValue(static_cast<SOLNpcScale::EDemoOption>(option), FCString::Atod(*args[1]));
                    return;
                }
            }
            UE_LOG(LogSOL, Warning, TEXT("sol.Npc.Option: unknown option '%s'"), *args[0]);
        }));

    // sol.Npc.Stats [0|1]: shows or hides the stats overlay (no argument toggles it)
    FAutoConsoleCommandWithWorldAndArgs GNpcStatsCommand(TEXT("sol.Npc.Stats"),
        TEXT("sol.Npc.Stats [0|1]: show or hide the NPC stats overlay (no argument toggles it; also Ctrl+F2)"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& args, UWorld* world)
        {
            if (USOLNpcSubsystem* npcs = FindNpcSubsystem(world))
            {
                npcs->SetStatsVisible(args.Num() > 0 ? FCString::Atoi(*args[0]) != 0 : !npcs->IsStatsVisible());
            }
        }));

    // sol.Npc.Stop: stops the ramp and despawns every NPC
    FAutoConsoleCommandWithWorldAndArgs GNpcStopCommand(TEXT("sol.Npc.Stop"),
        TEXT("sol.Npc.Stop: stop the NPC ramp and despawn every NPC"),
        FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& /*args*/, UWorld* world)
        {
            if (USOLNpcSubsystem* npcs = FindNpcSubsystem(world))
            {
                npcs->Stop();
            }
        }));
}

//////////////////////////////////////////////////////////////////////////
// Declares its dependencies, prepares the processors, reads the command line and hooks the universe update
void USOLNpcSubsystem::Initialize(FSubsystemCollectionBase& collection)
{
    UMassEntitySubsystem* massEntities = collection.InitializeDependency<UMassEntitySubsystem>();
    SimClock = collection.InitializeDependency<USOLSimClockSubsystem>();
    BodyRegistry = collection.InitializeDependency<USOLBodyRegistrySubsystem>();
    Anchor = collection.InitializeDependency<USOLAnchorSubsystem>();
    Ship = collection.InitializeDependency<USOLShipSubsystem>();
    Super::Initialize(collection);

    if (massEntities == nullptr || SimClock == nullptr || BodyRegistry == nullptr || Anchor == nullptr
        || Ship == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("NpcSubsystem %s: Mass, universe or ship subsystems missing"), *GetName());
        return;
    }
    mEntityManager = massEntities->GetMutableEntityManager().AsShared();
    mCommandBuffer = MakeShared<FMassCommandBuffer>();
    TierProcessor = NewObject<USOLNpcTierProcessor>(this);
    TierProcessor->CallInitialize(this, mEntityManager.ToSharedRef());
    MidProcessor = NewObject<USOLNpcMidProcessor>(this);
    MidProcessor->CallInitialize(this, mEntityManager.ToSharedRef());
    FullAiProcessor = NewObject<USOLNpcFullAiProcessor>(this);
    FullAiProcessor->CallInitialize(this, mEntityManager.ToSharedRef());

    // Command line: -SOLNpcCount=<n>, -SOLNpcSeed=<n>, -SOLNpcRamp
    mOptions = SOLNpcScale::DefaultOptions();
    FString countText;
    int32 commandLineCount = 0;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::NPC_COUNT, countText))
    {
        commandLineCount = FMath::Max(SOLNpcScale::ParseNpcCount(countText), 0);
    }
    uint32 seed = 0;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::NPC_SEED, seed))
    {
        mOptions.Seed = seed;
    }
    FString battleShipsText;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::NPC_BATTLE_SHIPS, battleShipsText))
    {
        const int32 battleShips = SOLNpcScale::ParseNpcCount(battleShipsText);
        if (battleShips != INDEX_NONE)
        {
            mOptions = SOLNpcScale::SetOption(mOptions, SOLNpcScale::EDemoOption::BattleShipCount, battleShips);
        }
    }
    if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::NPC_RAMP))
    {
        StartRamp(commandLineCount > 0 ? commandLineCount : SOL::NPC_MAX_COUNT, true);
    }
    else if (commandLineCount > 0)
    {
        // Without a count the options keep their defaults for the Demo page (nothing spawns until a demo starts)
        RequestNpcCount(commandLineCount);
    }
    mOverlayFrameMs.Init(0.0, SOL::NPC_STATS_FRAME_WINDOW);
    mOverlayScratch.Reserve(SOL::NPC_STATS_FRAME_WINDOW);
    mLastSeenCVarCombatModel = CVarNpcCombatModel.GetValueOnGameThread();
    mCombatModel = mLastSeenCVarCombatModel;

    mUniverseUpdatedHandle = Anchor->OnUniverseUpdated().AddUObject(this, &USOLNpcSubsystem::Update);

    // The worker budget the measurements ran with (-corelimit=<n> caps both core counts, SDD 11 decision 7)
    UE_LOG(LogSOL, Log, TEXT("NpcSubsystem %s: cores=%d logical=%d taskGraphWorkers=%d"), *GetName(),
        FPlatformMisc::NumberOfCores(), FPlatformMisc::NumberOfCoresIncludingHyperthreads(),
        FTaskGraphInterface::Get().GetNumWorkerThreads());
}

//////////////////////////////////////////////////////////////////////////
// Destroys the NPC entities while the entity manager is still alive
void USOLNpcSubsystem::PreDeinitialize()
{
    Despawn();
    Super::PreDeinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Unhooks from the universe update and releases the Mass objects
void USOLNpcSubsystem::Deinitialize()
{
    if (Anchor != nullptr)
    {
        Anchor->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
    }
    mUniverseUpdatedHandle.Reset();
    if (StatsWidget != nullptr)
    {
        StatsWidget->RemoveFromParent();
        StatsWidget = nullptr;
    }
    mEntities.Empty();
    mCommandBuffer.Reset();
    mEntityManager.Reset();
    TierProcessor = nullptr;
    MidProcessor = nullptr;
    FullAiProcessor = nullptr;
    Super::Deinitialize();
}

//////////////////////////////////////////////////////////////////////////
// Creates the subsystem only in game and PIE worlds that run ASOLGameMode
bool USOLNpcSubsystem::ShouldCreateSubsystem(UObject* outer) const
{
    return Super::ShouldCreateSubsystem(outer) && ASOLGameMode::IsSOLGameWorld(Cast<UWorld>(outer));
}

//////////////////////////////////////////////////////////////////////////
// Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
bool USOLNpcSubsystem::DoesSupportWorldType(const EWorldType::Type worldType) const
{
    return worldType == EWorldType::Game || worldType == EWorldType::PIE;
}

//////////////////////////////////////////////////////////////////////////
// Requests a population size; it is (re)spawned at the next update once the player ship exists
void USOLNpcSubsystem::RequestNpcCount(const int32 count)
{
    mRequestedCount = FMath::Clamp(count, 0, SOL::NPC_MAX_COUNT);
    mOptions = SOLNpcScale::SetOption(mOptions, SOLNpcScale::EDemoOption::NpcCount, mRequestedCount);
}

//////////////////////////////////////////////////////////////////////////
// Starts the stress ramp up to maxCount; bQuitWhenDone quits the game after the last stage
void USOLNpcSubsystem::StartRamp(const int32 maxCount, const bool bQuitWhenDone)
{
    mRampStages = SOLNpcScale::BuildRampStages(maxCount);
    mRamp = SOLNpcScale::StartRamp(mRampStages.Num());
    mIsRampQuitWhenDone = bQuitWhenDone;
    mLastRampResult.Reset();
    mRampFrameMs.Reset();
    mRampNpcMs.Reset();
    UE_LOG(LogSOL, Log, TEXT("NpcSubsystem %s: ramp started, %d stages up to %d ships"), *GetName(),
        mRampStages.Num(), maxCount);
}

//////////////////////////////////////////////////////////////////////////
// Stops the ramp (if any) and despawns every NPC
void USOLNpcSubsystem::Stop()
{
    mRamp = SOLNpcScale::FRampState();
    mRequestedCount = 0;
    Despawn();

    // Nothing of the demo outlives it: the overlay hides and the combat model returns to the console's
    SetStatsVisible(false);
    mCombatModel = CVarNpcCombatModel.GetValueOnGameThread();
    mLastSeenCVarCombatModel = mCombatModel;
}

//////////////////////////////////////////////////////////////////////////
// Turns the player ship to face a battle centre (verification screenshots; sol.Npc.FaceBattle)
void USOLNpcSubsystem::FaceBattle(const int32 battleIndex)
{
    const int32 group = mGroups.LaneCount + mGroups.ClusterCount + battleIndex;
    if (!Ship->HasPlayerShip() || battleIndex < 0 || battleIndex >= mGroups.BattleCount
        || !mRailFrame.GroupPointAM.IsValidIndex(group))
    {
        return;
    }

    // The ship state is in the Unreal-handed universe frame: ecliptic (x, y, z) -> (x, -y, z)
    const FVector3d toBattleM = mRailFrame.GroupPointAM[group] - Anchor->GetViewpointM();
    const FVector3d direction(toBattleM.X, -toBattleM.Y, toBattleM.Z);
    FSOLShipState state = Ship->GetState();
    state.Orientation = FQuat4d(FRotationMatrix44d::MakeFromX(direction.GetSafeNormal()).ToQuat());
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    Ship->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Returns the angle one screen pixel spans, from the player camera (the Mid-to-Far snap gate, SDD 18 section 3.7)
double USOLNpcSubsystem::ViewPixelAngleRad() const
{
    double fovDeg = 0.0;
    FVector2D viewportPx;
    ViewCamera(fovDeg, viewportPx);

    // The camera's FOV is horizontal: the view width over the viewport's pixel width (square pixels)
    return 2.0 * FMath::Tan(FMath::DegreesToRadians(fovDeg) * 0.5) / viewportPx.X;
}

//////////////////////////////////////////////////////////////////////////
// Returns the distance beyond which no NPC plume can reach the cone's low pixel edge and no ribbon its vanish length
// (SDD 51 section 4.2, R4): the longest plume (the hull length) at the view's 1080p-reference focal length
double USOLNpcSubsystem::ViewPlumeGateM() const
{
    // The plume A/B switch (sol.NpcPlumes 0, owned by ASOLNpcVisuals) also skips the thrust, so its baseline Collect
    // pass is the pre-#51 one
    static const IConsoleVariable* const plumeCvar = IConsoleManager::Get().FindConsoleVariable(SOL::NPC_PLUME_CVAR);
    if (plumeCvar != nullptr && plumeCvar->GetInt() == 0)
    {
        return 0.0;
    }
    double fovDeg = 0.0;
    FVector2D viewportPx;
    ViewCamera(fovDeg, viewportPx);

    // The glow and gas-burst reach, widened to the flame reach at the preset's V while the flames are on (R4, R10-7);
    // sol.ThrusterRibbons 0 leaves the glow and bursts only (R9); Epic (3) without settings, as the flames' draw assumes
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(this);
    const int32 quality = settings != nullptr ? settings->GetSavedValues().QualityPreset : 3;
    return SOLThruster::NpcThrustGateM(SOL::Thruster::NPC_HULL_LENGTH_M, SOL::Thruster::NPC_NOZZLE_RADIUS_M,
        SOLThruster::FocalRefPx(fovDeg, viewportPx.X / viewportPx.Y), ASOLThrusterRibbons::AreRibbonsEnabled(),
        SOLThruster::RibbonVanishPx(quality));
}

//////////////////////////////////////////////////////////////////////////
// Writes the player camera's horizontal FOV and the viewport size (the reference 16:9 view without either)
void USOLNpcSubsystem::ViewCamera(double& outFovDeg, FVector2D& outViewportPx) const
{
    outFovDeg = SOL::SHIP_CAMERA_BASE_FOV_DEG;
    outViewportPx = FVector2D(SOL::NPC_REFERENCE_VIEWPORT_WIDTH_PX, SOL::NPC_LANE_REFERENCE_HEIGHT_PX);
    const APlayerController* controller = GetWorld()->GetFirstPlayerController();
    if (controller != nullptr && controller->PlayerCameraManager != nullptr)
    {
        outFovDeg = controller->PlayerCameraManager->GetFOVAngle();
    }
    if (GetWorld()->GetGameViewport() != nullptr)
    {
        FVector2D size;
        GetWorld()->GetGameViewport()->GetViewportSize(size);
        if (size.X > 0.0 && size.Y > 0.0)
        {
            outViewportPx = size;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// On a population's first spawn, puts the player ship at rest relative to the battles' body (battles stay around it)
void USOLNpcSubsystem::MatchShipToBattleBody()
{
    // An explicit exception to SDD 18 decision 1 (nothing writes velocity directly), for the demo / benchmark scene
    // only: a ship flying an autopilot order or a chase keeps its velocity
    const TConstArrayView<FVector3d> bodyVelocitiesMps = BodyRegistry->GetUnrealFrameCache().VelocitiesMps;
    if (mGroups.BattleCount <= 0 || !Ship->HasPlayerShip() || Ship->GetAutopilotBodyIndex() != INDEX_NONE
        || Ship->IsChasing() || !bodyVelocitiesMps.IsValidIndex(mGroups.BattleBody))
    {
        return;
    }
    FSOLShipState state = Ship->GetState();
    state.VelocityMps = bodyVelocitiesMps[mGroups.BattleBody];
    Ship->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Sets the battle ship count and respawns the population (sol.Npc.BattleShips, -SOLNpcBattleShips=)
void USOLNpcSubsystem::SetBattleShipCount(const int32 count)
{
    mOptions = SOLNpcScale::SetOption(mOptions, SOLNpcScale::EDemoOption::BattleShipCount, count);
    mIsRespawnRequested = true;
}

//////////////////////////////////////////////////////////////////////////
// Starts a demo with these options (the Demo page's Start / Apply)
void USOLNpcSubsystem::StartDemo(const SOLNpcScale::FDemoOptions& options, const int32 combatModel, const bool bRamp)
{
    mOptions = SOLNpcScale::SanitizeOptions(options).Options;
    mCombatModel = FMath::Clamp(combatModel, 0, static_cast<int32>(SOLNpcAi::ECombatModel::Hybrid));
    SetStatsVisible(mOptions.bShowStats);
    if (bRamp)
    {
        StartRamp(mOptions.NpcCount, false);
    }
    else
    {
        mRamp = SOLNpcScale::FRampState();
        mRequestedCount = mOptions.NpcCount;
    }
    mIsRespawnRequested = true;
    UE_LOG(LogSOL, Log, TEXT("NpcSubsystem %s: demo started, count=%d ramp=%d model=%d battleShips=%d seed=%u"),
        *GetName(), mOptions.NpcCount, bRamp ? 1 : 0, GetCombatModel(), mOptions.BattleShipCount, mOptions.Seed);
}

//////////////////////////////////////////////////////////////////////////
// Sets one demo option (sanitized as on the Demo page) and respawns a live population (sol.Npc.Option)
void USOLNpcSubsystem::SetOptionValue(const SOLNpcScale::EDemoOption option, const double value)
{
    mOptions = SOLNpcScale::SetOption(mOptions, option, value);
    if (option == SOLNpcScale::EDemoOption::NpcCount)
    {
        mRequestedCount = mOptions.NpcCount;
    }
    else if (option == SOLNpcScale::EDemoOption::ShowStats)
    {
        SetStatsVisible(mOptions.bShowStats);
    }
    mIsRespawnRequested = mSpawnedCount > 0;
}

//////////////////////////////////////////////////////////////////////////
// Shows or hides the stats overlay; the widget is created at the next update once a player controller exists
void USOLNpcSubsystem::SetStatsVisible(const bool bVisible)
{
    mIsStatsVisible = bVisible;
    mOverlayRefreshElapsedS = SOL::NPC_STATS_REFRESH_PERIOD_S;
    if (StatsWidget != nullptr)
    {
        StatsWidget->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
}

//////////////////////////////////////////////////////////////////////////
// Destroys every NPC entity and clears the population
void USOLNpcSubsystem::Despawn()
{
    if (mEntityManager.IsValid() && !mEntities.IsEmpty())
    {
        // Whole chunks of every NPC archetype at once, instead of looking up 1M handles one by one (Amendment 6)
        TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::Despawn);
        FMassEntityQuery npcQuery(mEntityManager.ToSharedRef());
        npcQuery.AddRequirement<FSOLNpcRailFragment>(EMassFragmentAccess::ReadOnly);
        npcQuery.GetNumMatchingEntities();
        TArray<FMassArchetypeEntityCollection> collections;
        for (const FMassArchetypeHandle& archetype : npcQuery.GetArchetypes())
        {
            collections.Emplace(archetype);
        }
        mEntityManager->BatchDestroyEntityChunks(collections);
    }
    mEntities.Empty();
    mSpawnOrder.Empty();
    mSpawnBucketStarts.Empty();
    mSpawnCreated = 0;
    mRails.Empty();
    mCpuShips.Reset();
    mBlockSlot.Empty();
    mBlockInWall.Empty();
    mBlockHalo.Empty();
    mBlockShips.Empty();
    mLaneEntities.Empty();
    mBlockSlotLanes.Reset();
    mBlockSlotInSet.Reset();
    mBlockMoveRanges.Reset();
    mColumnLaneShips.Reset();

    // The block step frequency of the population being cleared (a measure of the release retries, SDD 53 4.5)
    UE_CLOG(mHasLaneNetwork, LogSOL, Log, TEXT("NpcLaneBlocks: %d block steps ran past their early-out by frame %llu"),
        mBlockStepRuns, static_cast<uint64>(GFrameCounter));
    mHasLaneNetwork = false;
    mIsBlockStepPending = false;
    mIsBlockReshareRequested = false;
    mHasPendingRelease = false;
    mHasFadeHolds = false;
    mBlockStepRuns = 0;
    mGroupOpenStartRailS.Reset();
    mGroupCloseRailS.Reset();
    mLaneSlotWindows.Reset();
    mHasNodeEndpoints = false;
    mCombat.Reset(0);
    mHasPrevCombatFrameBody = false;
    mSpawnedCount = 0;
    mStats = FSOLNpcStats();
    ++mPopulationRevision;
    if (USOLLaneTrafficSubsystem* laneTraffic = GetWorld() != nullptr
        ? GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>() : nullptr)
    {
        laneTraffic->ClearLanes();
        laneTraffic->ClearNetwork();
    }
}

//////////////////////////////////////////////////////////////////////////
// Resolves the layout's body names and fills the group table (battles around the current anchor)
void USOLNpcSubsystem::BuildGroups(const SOLNpcScale::FLayout& layout, const SOLNpcScale::FBattleParams& battle)
{
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    mGroups = FSOLNpcGroupTable();
    mGroups.LaneCount = layout.Lanes.Num();
    mGroups.ClusterCount = layout.Clusters.Num();
    mGroups.BattleCount = FMath::Max(battle.BattleCount, 1);
    for (const SOLNpcScale::FLaneDef& lane : layout.Lanes)
    {
        mGroups.LaneBodyA.Add(registry.FindByName(lane.BodyA));
        mGroups.LaneBodyB.Add(registry.FindByName(lane.BodyB));
        mGroups.LaneRadiusM.Add(lane.RadiusM);
    }
    for (const SOLNpcScale::FClusterDef& cluster : layout.Clusters)
    {
        mGroups.ClusterBody.Add(registry.FindByName(cluster.Body));
        mGroups.ClusterInnerM.Add(cluster.InnerRadiusM);
        mGroups.ClusterOuterM.Add(cluster.OuterRadiusM);
    }

    // Battles co-move with the player's anchor body, centred at fixed offsets from where the player is now
    mGroups.BattleBody = Anchor->GetAnchorIndex();
    mGroups.BattleRadiusM = battle.RadiusM;
    const FVector3d viewFromBodyM = Anchor->GetViewpointM() - Anchor->GetAnchorPositionM();
    for (int32 index = 0; index < mGroups.BattleCount; ++index)
    {
        mGroups.BattleCenterFromBodyM.Add(viewFromBodyM
            + SOLNpcScale::BattleCenterOffsetM(mOptions.Seed, index, battle));
    }
}

//////////////////////////////////////////////////////////////////////////
// Configures the lane network (the 24-node table at the spawn epoch) and advances it to the spawn epoch
bool USOLNpcSubsystem::ConfigureLaneNetwork(const SOLNpcScale::FLayout& layout)
{
    mNodeTable = SOLLaneNodeTable::FTable();
    USOLLaneTrafficSubsystem* laneTraffic = GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>();
    if (laneTraffic == nullptr)
    {
        return false;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    mNodeTable = SOLLaneNodeTable::Build(registry, SOLAsteroidBelt::RealAsteroids(), layout.Lanes,
        mEpochSecondsSinceJ2000);
    if (!mNodeTable.bValid)
    {
        UE_LOG(LogSOL, Warning, TEXT("Lane network: node table incomplete, no lane network"));
        laneTraffic->ClearNetwork();
        return false;
    }

    // Node positions from the registry and the belt catalogue (SDD 53 4.2); the schedule is built here, at the spawn
    // epoch, so the slots exist before the rails (the next Update's advance at the same epoch does nothing)
    FSOLLaneNetworkConfig config;
    config.Nodes = mNodeTable.Nodes;
    config.NodeMaxSpeedMps = mNodeTable.MaxSpeedMps;
    config.BaseLanes = mNodeTable.BaseLanes;
    config.RootNode = SOLLaneNodeTable::EARTH_NODE;
    config.SlotCapacity = mGroups.LaneSlotCapacity();   // Slot columns: FSOLNpcGroupTable::LaneSlotColumn
    const FSOLBodyRegistry* registryPtr = &registry;
    laneTraffic->ConfigureNetwork(config, [registryPtr, table = mNodeTable](const double secondsSinceJ2000,
        TArrayView<FVector3d> outPositionsM)
    {
        SOLLaneNodeTable::PositionsAt(table, *registryPtr, secondsSinceJ2000, outPositionsM);
    });
    mSeenClockTimeSetCount = SimClock->GetTimeSetCount();
    laneTraffic->AdvanceNetwork(mEpochSecondsSinceJ2000);
    return laneTraffic->GetNetwork().IsBuilt();
}

//////////////////////////////////////////////////////////////////////////
// Fills the slot columns past the base lanes and writes each slot's weight over the spawn epoch's lane set
void USOLNpcSubsystem::BuildSlotColumns(const SOLNpcScale::FLayout& layout, const bool bNetwork,
    TArray<double>& outSlotWeights)
{
    outSlotWeights.Reset();
    mHasNodeEndpoints = false;
    if (!bNetwork)
    {
        // No network: the base lanes by the layout's weights
        for (const SOLNpcScale::FLaneDef& lane : layout.Lanes)
        {
            outSlotWeights.Add(FMath::Max(lane.Weight, 0.0));
        }
        return;
    }
    const FSOLLaneNetworkSchedule& network = GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>()->GetNetwork();
    const TConstArrayView<FSOLLanePair> slots = network.GetSlots();
    const TConstArrayView<FSOLLaneNode> nodes = mNodeTable.Nodes;
    const int32 slotCount = FMath::Min(slots.Num(), mGroups.LaneSlotCapacity());
    mGroups.ExtraSlotCount = FMath::Max(slotCount - mGroups.LaneCount, 0);
    const int32 columnEnd = mGroups.LaneColumnEnd();
    while (mGroups.LaneBodyA.Num() < columnEnd)
    {
        mGroups.LaneBodyA.Add(INDEX_NONE);
        mGroups.LaneBodyB.Add(INDEX_NONE);
        mGroups.LaneRadiusM.Add(0.0);
    }

    const int32 registryCount = BodyRegistry->GetRegistry().Num();
    const int64 epoch = network.GetCurrentEpoch();
    outSlotWeights.SetNumZeroed(slotCount);
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        const FSOLLanePair& lane = slots[slot];
        if (!nodes.IsValidIndex(lane.NodeA) || !nodes.IsValidIndex(lane.NodeB))
        {
            continue;
        }

        // A partner lane or island link: its nodes' end points and the larger region radius (O5); base lanes keep the
        // layout's bodies and radii (Earth-Moon its hand row)
        if (slot >= mGroups.LaneCount)
        {
            const int32 column = mGroups.LaneSlotColumn(slot);
            mGroups.LaneBodyA[column] = NodeEndpoint(lane.NodeA);
            mGroups.LaneBodyB[column] = NodeEndpoint(lane.NodeB);
            mGroups.LaneRadiusM[column] = FMath::Max(nodes[lane.NodeA].RegionRadiusM,
                nodes[lane.NodeB].RegionRadiusM);
            mHasNodeEndpoints |= mGroups.LaneBodyA[column] >= registryCount
                || mGroups.LaneBodyB[column] >= registryCount;
        }
        outSlotWeights[slot] = network.IsInSet(epoch, lane)
            ? SOLLaneNetwork::LaneWeight(nodes[lane.NodeA].PopulationTier, nodes[lane.NodeB].PopulationTier) : 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns this frame's node positions for end points past the registry: only the asteroid nodes are evaluated (the
// registry nodes' end points read the registry), nothing is copied
TConstArrayView<FVector3d> USOLNpcSubsystem::LaneEndpointPositions()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::LaneEndpointPositions);
    if (!mHasNodeEndpoints)
    {
        return TConstArrayView<FVector3d>();
    }
    if (mNodePositionsM.Num() != mNodeTable.Nodes.Num())
    {
        // The registry nodes' entries are never read (their end points are registry indices), so NaN once
        mNodePositionsM.Init(FVector3d(std::numeric_limits<double>::quiet_NaN()), mNodeTable.Nodes.Num());
    }
    SOLLaneNodeTable::AsteroidPositionsAt(mNodeTable, SimClock->GetClock().GetSecondsSinceJ2000(), mNodePositionsM);
    return mNodePositionsM;
}

//////////////////////////////////////////////////////////////////////////
// Writes each lane column's slot window in rail time (the tier rule) and each slot's row 6 inputs (window, active)
void USOLNpcSubsystem::UpdateLaneWindows(const double railTimeS, const FVector3d& viewpointM)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::UpdateLaneWindows);
    const USOLLaneTrafficSubsystem* laneTraffic = GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>();
    if (laneTraffic == nullptr || !laneTraffic->GetNetwork().IsBuilt())
    {
        mGroupOpenStartRailS.Reset();
        mGroupCloseRailS.Reset();
        mLaneSlotWindows.Reset();
        return;
    }
    const FSOLLaneNetworkSchedule& network = laneTraffic->GetNetwork();
    const int32 columnEnd = mGroups.LaneColumnEnd();
    const int32 slotCount = FMath::Min(network.GetSlots().Num(), mGroups.LaneSlotCapacity());

    // CPU-tier ships per column (a lane with one stays active, Q22)
    mColumnCpuShips.SetNumZeroed(columnEnd, EAllowShrinking::No);
    FMemory::Memzero(mColumnCpuShips.GetData(), sizeof(int32) * columnEnd);
    for (const FSOLNpcCpuShip& ship : mCpuShips)
    {
        const int32 group = mRails.IsValidIndex(ship.NpcIndex) ? mRails[ship.NpcIndex].Group : INDEX_NONE;
        if (mColumnCpuShips.IsValidIndex(group))
        {
            ++mColumnCpuShips[group];
        }
    }

    // The view's 1080p-reference focal and the QualityPreset's threshold scale (P5); the reference preset (Epic)
    // without settings
    double fovDeg = 0.0;
    FVector2D viewportPx;
    ViewCamera(fovDeg, viewportPx);
    const double focalRefPx = SOLThruster::FocalRefPx(fovDeg, viewportPx.X / viewportPx.Y);
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(this);
    const double thresholdScale = SOLLaneNetwork::LaneTunnelQualityScale(settings != nullptr
        ? settings->GetSavedValues().QualityPreset : SOL::NPC_TUNNEL_QUALITY_REFERENCE_PRESET);
    const double nanS = std::numeric_limits<double>::quiet_NaN();
    const double nowS = SimClock->GetClock().GetSecondsSinceJ2000();
    mGroupOpenStartRailS.Init(nanS, columnEnd);
    mGroupCloseRailS.Init(nanS, columnEnd);
    mLaneSlotWindows.SetNum(slotCount, EAllowShrinking::No);
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        FSOLNpcLaneSlotWindow& window = mLaneSlotWindows[slot];
        window.bHasWindow = network.GetSlotWindow(slot, window.OpenStartS, window.CloseS);
        const int32 column = mGroups.LaneSlotColumn(slot);
        window.bActive = false;
        window.bOpenNow = false;
        if (!mGroupOpenStartRailS.IsValidIndex(column))
        {
            continue;
        }
        if (window.bHasWindow)
        {
            SOLNpcRail::SetSlotWindowRailS(mGroups, slot, window.OpenStartS, window.CloseS, mEpochSecondsSinceJ2000,
                mGroupOpenStartRailS, mGroupCloseRailS);
        }
        window.bOpenNow = window.bHasWindow && SOLLaneNetwork::LaneOpenAt(nowS, window.OpenStartS, window.CloseS);
        const double radiusPx = SOLLaneNetwork::LaneTrafficRadiusPx(viewpointM, mRailFrame.GroupPointAM[column],
            mRailFrame.GroupPointBM[column], mGroups.LaneRadiusM[column], focalRefPx);
        window.bActive = SOLLaneNetwork::LaneTrafficActive(radiusPx, mColumnCpuShips[column], thresholdScale);
    }
}

//////////////////////////////////////////////////////////////////////////
// Stores the columns whose tunnel is still fading; a column that stopped fading re-checks the pending releases
void USOLNpcSubsystem::SetTunnelFadingColumns(const TConstArrayView<uint8> fadingColumns)
{
    mTunnelFadingFrame = GFrameCounter;
    if (mColumnTunnelFading.Num() != fadingColumns.Num())
    {
        mColumnTunnelFading.SetNumZeroed(fadingColumns.Num());
    }
    for (int32 column = 0; column < fadingColumns.Num(); ++column)
    {
        const uint8 fading = fadingColumns[column] != 0 ? 1 : 0;
        mHasPendingRelease |= mColumnTunnelFading[column] != 0 && fading == 0;
        mColumnTunnelFading[column] = fading;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns a lane node's end point index: its registry body, else registry count + node (LaneEndpointPositions)
int32 USOLNpcSubsystem::NodeEndpoint(const int32 node) const
{
    const TConstArrayView<FSOLLaneNode> nodes = mNodeTable.Nodes;
    if (!nodes.IsValidIndex(node))
    {
        return INDEX_NONE;
    }
    return nodes[node].BodyIndex != INDEX_NONE ? nodes[node].BodyIndex : BodyRegistry->GetRegistry().Num() + node;
}

//////////////////////////////////////////////////////////////////////////
// Writes each slot's lane, in-set flag and weight at the network's current epoch into the scratch arrays
int32 USOLNpcSubsystem::ReadSlotsNow()
{
    const USOLLaneTrafficSubsystem* laneTraffic = GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>();
    if (laneTraffic == nullptr || !laneTraffic->GetNetwork().IsBuilt())
    {
        mSlotLanesNow.Reset();
        mSlotInSetNow.Reset();
        mSlotWeightsNow.Reset();
        return 0;
    }
    const FSOLLaneNetworkSchedule& network = laneTraffic->GetNetwork();
    const TConstArrayView<FSOLLanePair> slots = network.GetSlots();
    const TConstArrayView<FSOLLaneNode> nodes = mNodeTable.Nodes;
    const int32 slotCount = FMath::Min(slots.Num(), mGroups.LaneSlotCapacity());
    const int64 epoch = network.GetCurrentEpoch();
    mSlotLanesNow.SetNum(slotCount, EAllowShrinking::No);
    mSlotInSetNow.SetNum(slotCount, EAllowShrinking::No);
    mSlotWeightsNow.SetNum(slotCount, EAllowShrinking::No);
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        // A slot's weight over the current set (P4 a): LaneWeight of its end tiers while in the set, else 0
        const FSOLLanePair& lane = slots[slot];
        const bool bInSet = nodes.IsValidIndex(lane.NodeA) && nodes.IsValidIndex(lane.NodeB)
            && network.IsInSet(epoch, lane);
        mSlotLanesNow[slot] = lane;
        mSlotInSetNow[slot] = bInSet ? 1 : 0;
        mSlotWeightsNow[slot] = bInSet
            ? SOLLaneNetwork::LaneWeight(nodes[lane.NodeA].PopulationTier, nodes[lane.NodeB].PopulationTier) : 0.0;
    }
    return slotCount;
}

//////////////////////////////////////////////////////////////////////////
// Runs the block step after the network's advance: releases, episode takes and holds, or a time jump's re-share
void USOLNpcSubsystem::StepLaneBlocks(const ESOLLaneNetworkAdvance advance)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::StepLaneBlocks);
    if (!mHasLaneNetwork || mBlockSlot.IsEmpty() || mSpawnedCount == 0)
    {
        return;
    }
    mIsBlockReshareRequested |= advance == ESOLLaneNetworkAdvance::Rebuilt;
    mIsBlockStepPending |= advance == ESOLLaneNetworkAdvance::Stepped;

    // A pending spawn buckets its entities by their spawn-time groups: the moves wait until every entity exists
    if (mSpawnCreated < mSpawnOrder.Num())
    {
        return;
    }

    // A Fade-only hold whose reports went stale (the tunnel redraw stopped) is re-checked once, so it cannot hold its
    // slot forever
    if (mHasFadeHolds && GFrameCounter - mTunnelFadingFrame > NPC_TUNNEL_FADING_MAX_AGE_FRAMES)
    {
        mHasPendingRelease = true;
    }
    if (!mIsBlockReshareRequested && !mIsBlockStepPending && !mHasPendingRelease)
    {
        return;
    }
    USOLLaneTrafficSubsystem* laneTraffic = GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>();
    const int32 slotCount = ReadSlotsNow();
    if (laneTraffic == nullptr || slotCount == 0)
    {
        return;
    }
    const double startS = FPlatformTime::Seconds();
    ++mBlockStepRuns;
    const bool bReshare = mIsBlockReshareRequested;
    mIsBlockReshareRequested = false;
    mIsBlockStepPending = false;
    if (bReshare)
    {
        // A time jump (P4 d): every block re-shared exactly over the new set, every slot's profile frozen again
        SOLLaneNetwork::AssignShipBlocks(mBlockSlot.Num(), mSlotWeightsNow, mBlockSlot);
        mBlockStep.MovedBlocks.SetNumUninitialized(mBlockSlot.Num());
        for (int32 block = 0; block < mBlockSlot.Num(); ++block)
        {
            mBlockStep.MovedBlocks[block] = block;
        }
        mBlockStep.SlotEpisodeOpens.Init(1, slotCount);
        mBlockStep.SlotOwnsBlocks.Init(0, slotCount);
        for (const int32 slot : mBlockSlot)
        {
            if (slot >= 0 && slot < slotCount)
            {
                mBlockStep.SlotOwnsBlocks[slot] = 1;
            }
        }
        mBlockStep.Released = 0;
        mBlockStep.Taken = mBlockSlot.Num();
        mBlockStep.Shortfall = 0;
    }
    else
    {
        // A step: CPU-tier ships per slot from the last Collect (a slot keeps its blocks while one flies, SDD 4.5)
        mSlotCpuShips.Init(0, slotCount);
        for (const FSOLNpcCpuShip& ship : mCpuShips)
        {
            const int32 slot = mRails.IsValidIndex(ship.NpcIndex) ? mGroups.ColumnSlot(mRails[ship.NpcIndex].Group)
                : INDEX_NONE;
            if (mSlotCpuShips.IsValidIndex(slot))
            {
                ++mSlotCpuShips[slot];
            }
        }
        SOLLaneNetwork::StepShipBlocks(mBlockSlotLanes, mBlockSlotInSet, mSlotLanesNow, mSlotInSetNow, mSlotCpuShips,
            mSlotWeightsNow, mBlockSlot, mBlockStep);
    }

    // The next step compares with this one; a slot owning blocks is held (never reused under them), a slot that
    // owns blocks outside the set is checked again every frame until its CPU-tier ships are gone
    mBlockSlotLanes = mSlotLanesNow;
    mBlockSlotInSet = mSlotInSetNow;
    mHasPendingRelease = false;
    mHasFadeHolds = false;
    bool bEndsCleared = false;

    // The tunnel's fading columns count only while the tunnels keep reporting them (a stopped redraw cannot hold a
    // slot forever)
    const bool bFadingFresh = GFrameCounter - mTunnelFadingFrame <= NPC_TUNNEL_FADING_MAX_AGE_FRAMES;
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        // A slot owning blocks, or whose tunnel is still fading out (SDD 53 4.5: the column is freed after the close
        // and the 3 s tunnel fade; a Fade-only hold yields to a new lane when no column is free), is held. Only a
        // block owner out of the set retries every frame; a Fade-only hold is re-checked once at its fade's end
        // (SetTunnelFadingColumns) or when the fade reports go stale
        const bool bOwns = mBlockStep.SlotOwnsBlocks[slot] != 0;
        const bool bInSet = mSlotInSetNow[slot] != 0;
        const int32 column = mGroups.LaneSlotColumn(slot);
        const bool bFading = bFadingFresh && mColumnTunnelFading.IsValidIndex(column)
            && mColumnTunnelFading[column] != 0;
        const ESOLLaneSlotHold hold = SOLLaneNetwork::LaneSlotHold(bOwns, bFading);
        laneTraffic->SetNetworkSlotHold(slot, hold);
        mHasPendingRelease |= SOLLaneNetwork::IsSlotReleasePending(bOwns, bInSet);
        mHasFadeHolds |= hold == ESOLLaneSlotHold::Fade && !bInSet;

        // A released network column outside the set (Gone) has no end points, so its tunnel and lane table entry go;
        // it gets them again when it opens (RefreshSlotColumn). Base lane columns keep the layout's bodies
        if (!bOwns && !bFading && mSlotInSetNow[slot] == 0 && slot >= mGroups.LaneCount
            && mGroups.LaneBodyA.IsValidIndex(column) && mGroups.LaneBodyB.IsValidIndex(column)
            && (mGroups.LaneBodyA[column] != INDEX_NONE || mGroups.LaneBodyB[column] != INDEX_NONE))
        {
            mGroups.LaneBodyA[column] = INDEX_NONE;
            mGroups.LaneBodyB[column] = INDEX_NONE;
            bEndsCleared = true;
        }
    }
    if (bEndsCleared)
    {
        laneTraffic->UpdateLaneEnds(mGroups.LaneBodyA, mGroups.LaneBodyB, mGroups.LaneProfile);
    }
    if (!mBlockStep.MovedBlocks.IsEmpty())
    {
        ApplyBlockMoves(mBlockStep.MovedBlocks, mBlockStep.SlotEpisodeOpens, bReshare, startS);
    }
}

//////////////////////////////////////////////////////////////////////////
// Refreshes an opening slot's column: end points and radius (network slots) and the profile frozen at the open
void USOLNpcSubsystem::RefreshSlotColumn(const int32 slot, const FSOLLanePair& lane, const int32 inWall,
    const int32 halo)
{
    const int32 column = mGroups.LaneSlotColumn(slot);
    if (!mGroups.LaneProfile.IsValidIndex(column) || !mGroups.LaneBodyA.IsValidIndex(column))
    {
        return;
    }
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const TConstArrayView<FSOLLaneNode> nodes = mNodeTable.Nodes;
    double nominalM = 0.0;
    double radiusAM = 0.0;
    double radiusBM = 0.0;
    if (slot < mGroups.LaneCount)
    {
        // A base lane keeps the layout's bodies and radius, its registry form (as at spawn, the spawn epoch's
        // elements)
        const int32 bodyA = mGroups.LaneBodyA[column];
        const int32 bodyB = mGroups.LaneBodyB[column];
        if (bodyA >= 0 && bodyA < registry.Num() && bodyB >= 0 && bodyB < registry.Num())
        {
            nominalM = NpcLaneNominalLengthM(registry, bodyA, bodyB,
                mEpochSecondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY);
            radiusAM = registry.GetRadiusM(bodyA);
            radiusBM = registry.GetRadiusM(bodyB);
        }
    }
    else
    {
        // A partner lane or island link: its nodes' end points, the larger region radius (O5), aphelia and clearance
        if (!nodes.IsValidIndex(lane.NodeA) || !nodes.IsValidIndex(lane.NodeB))
        {
            return;
        }
        mGroups.LaneBodyA[column] = NodeEndpoint(lane.NodeA);
        mGroups.LaneBodyB[column] = NodeEndpoint(lane.NodeB);
        mGroups.LaneRadiusM[column] = FMath::Max(nodes[lane.NodeA].RegionRadiusM, nodes[lane.NodeB].RegionRadiusM);
        nominalM = SOLLaneNetwork::LaneNominalLengthM(nodes, lane.NodeA, lane.NodeB);
        radiusAM = nodes[lane.NodeA].ClearanceRadiusM;
        radiusBM = nodes[lane.NodeB].ClearanceRadiusM;
    }
    mGroups.LaneProfile[column] = SOLNpcRail::SlotLaneProfile(mGroups.LaneRadiusM[column], inWall, nominalM,
        radiusAM, radiusBM);
    mGroups.LaneInWallCount[column] = inWall;
    mGroups.LaneHaloCount[column] = halo;
    mGroups.LaneMaxPeakMps[column] = 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Re-rails and re-buckets the moved blocks' ships after refreshing the opening slots' columns, and logs the cost
void USOLNpcSubsystem::ApplyBlockMoves(TConstArrayView<int32> movedBlocks, TConstArrayView<uint8> slotOpens,
    const bool bFullReshare, const double startS)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::ApplyBlockMoves);
    USOLLaneTrafficSubsystem* laneTraffic = GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>();
    const int32 slotCount = mSlotLanesNow.Num();

    // The opening slots' columns, each profile frozen from the in-wall ships of the blocks it holds now
    TArray<int32> slotInWall;
    TArray<int32> slotHalo;
    SOLLaneNetwork::SumBlockCounts(mBlockSlot, mBlockInWall, slotCount, slotInWall);
    SOLLaneNetwork::SumBlockCounts(mBlockSlot, mBlockHalo, slotCount, slotHalo);
    int32 opened = 0;
    for (int32 slot = 0; slot < slotCount && slot < slotOpens.Num(); ++slot)
    {
        if (slotOpens[slot] != 0)
        {
            RefreshSlotColumn(slot, mSlotLanesNow[slot], slotInWall[slot], slotHalo[slot]);
            ++opened;
        }
    }
    if (opened > 0)
    {
        // End points past the registry (asteroid nodes) and the HUD / AUTO lane table follow the columns
        const int32 registryCount = BodyRegistry->GetRegistry().Num();
        mHasNodeEndpoints = false;
        for (int32 column = 0; column < mGroups.LaneColumnEnd(); ++column)
        {
            mHasNodeEndpoints |= mGroups.IsLaneColumn(column)
                && (mGroups.LaneBodyA[column] >= registryCount || mGroups.LaneBodyB[column] >= registryCount);
        }
        if (laneTraffic != nullptr)
        {
            // In place: each entry keeps its slot and this frame's open flag (AdvanceNetwork already ran)
            laneTraffic->UpdateLaneEnds(mGroups.LaneBodyA, mGroups.LaneBodyB, mGroups.LaneProfile);
        }
    }

    // The moved ships' rails (CPU copy and Mass fragments) and buckets
    const int32 laneShips = mLaneSpawn.Counts.Lane;
    SOLLaneNetwork::ShipBlockDirtyRanges(movedBlocks, laneShips, mLaneSpawn.BlockSize, mBlockMoveRanges);
    SOLNpcBlockMoves::RebuildLaneRails(mLaneSpawn, mBlockSlot, mBlockMoveRanges, mGroups, mRails);
    const double railsS = FPlatformTime::Seconds();
    const SOLNpcBlockMoves::FRebucketStats stats = SOLNpcBlockMoves::RebucketLaneShips(*mEntityManager,
        mCommandBuffer, mLaneEntities, mRails, mLaneSpawn.Counts.Battle, mBlockMoveRanges, mGroups.Num(),
        mGroupWakeDirty);
    const double massS = FPlatformTime::Seconds();

    // Lane ships per column (the GPU layer's histogram and ribbon)
    TArray<int32> slotShips;
    SOLLaneNetwork::SumBlockCounts(mBlockSlot, mBlockShips, slotCount, slotShips);
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        const int32 column = mGroups.LaneSlotColumn(slot);
        if (mColumnLaneShips.IsValidIndex(column))
        {
            mColumnLaneShips[column] = slotShips[slot];
        }
    }
    if (bFullReshare)
    {
        ++mRailRevision;
    }
    else
    {
        ++mBlockMoveRevision;
    }
    const double endS = FPlatformTime::Seconds();
    const double totalMs = (endS - startS) * 1000.0;
    UE_LOG(LogSOL, Log, TEXT("NpcLaneBlockMove full=%d blocks=%d released=%d taken=%d shortfall=%d opened=%d ")
        TEXT("rebucketed=%d demoted=%d ms=%.3f perBlockMs=%.4f railsMs=%.3f massMs=%.3f"), bFullReshare ? 1 : 0,
        movedBlocks.Num(), mBlockStep.Released, mBlockStep.Taken, mBlockStep.Shortfall, opened, stats.Rebucketed,
        stats.Demoted, totalMs, movedBlocks.Num() > 0 ? totalMs / movedBlocks.Num() : 0.0,
        (railsS - startS) * 1000.0, (massS - railsS) * 1000.0);
}

//////////////////////////////////////////////////////////////////////////
// Spawns a population of count ships around the current viewpoint
void USOLNpcSubsystem::Spawn(const int32 count)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::Spawn);
    const double startS = FPlatformTime::Seconds();
    Despawn();
    if (!mEntityManager.IsValid() || count <= 0)
    {
        return;
    }
    const double memoryBeforeMB = UsedPhysicalMB();

    // Distribution: counts per kind, the default layout, battles from the options
    const SOLNpcScale::FCounts counts = SOLNpcScale::SplitCounts(count, mOptions.BattleShipCount,
        mOptions.LaneFraction);
    const SOLNpcScale::FLayout layout = SOLNpcScale::DefaultLayout();
    SOLNpcScale::FBattleParams battle;
    battle.BattleCount = mOptions.BattleCount;
    battle.RadiusM = mOptions.BattleRadiusM;
    battle.MinDistanceM = mOptions.BattleMinDistanceM;
    battle.MaxDistanceM = mOptions.BattleMaxDistanceM;
    BuildGroups(layout, battle);
    mEpochSecondsSinceJ2000 = SimClock->GetClock().GetSecondsSinceJ2000();

    // Cluster GMs at the epoch
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    TArray<double> clusterGMs;
    for (int32 cluster = 0; cluster < mGroups.ClusterCount; ++cluster)
    {
        const int32 body = mGroups.ClusterBody[cluster];
        clusterGMs.Add(body != INDEX_NONE ? registry.GetGM(body) : 0.0);
    }

    // The lane network first (SDD 53 4.2-4.5, 53c2a): built at the spawn epoch so the lane ships go into ship blocks
    // shared over that epoch's slotted lane set before any rail exists. Without a network (node table incomplete)
    // the base lanes are the slots, shared by the layout's weights
    const uint32 seed = mOptions.Seed;
    const bool bNetwork = ConfigureLaneNetwork(layout);
    TArray<double> slotWeights;
    BuildSlotColumns(layout, bNetwork, slotWeights);
    const int32 slotCount = slotWeights.Num();
    const int32 columnEnd = mGroups.LaneColumnEnd();
    const int32 blockSize = SOL::NPC_LANE_SHIP_BLOCK_SIZE;
    SOLLaneNetwork::AssignShipBlocks(SOLLaneNetwork::ShipBlockCount(counts.Lane, blockSize), slotWeights, mBlockSlot);
    UE_LOG(LogSOL, Log, TEXT("NpcLaneBlocks network=%d slots=%d extraColumns=%d blocks=%d parked=%d"),
        bNetwork ? 1 : 0, slotCount, mGroups.ExtraSlotCount, mBlockSlot.Num(),
        static_cast<int32>(Algo::Count(mBlockSlot, INDEX_NONE)));

    // Lane profiles per slot column (SDD 50 4.7, SDD 53 4.5): the in-wall and halo ships of each slot's blocks (D13),
    // the nominal (maximum) length (D16) and the end radii; a base lane keeps its registry form, a network lane uses
    // its nodes' aphelia and clearance radii
    // (the block counts and the spawn inputs are kept for the block moves, 53c2b)
    SOLNpcScale::CountBlockSpawns(seed, counts, blockSize, mBlockInWall, mBlockHalo);
    mBlockShips.SetNumUninitialized(mBlockSlot.Num());
    for (int32 block = 0; block < mBlockSlot.Num(); ++block)
    {
        mBlockShips[block] = SOLLaneNetwork::ShipBlockSize(block, counts.Lane, blockSize);
    }
    mLaneSpawn.Seed = seed;
    mLaneSpawn.Counts = counts;
    mLaneSpawn.Layout = layout;
    mLaneSpawn.Battle = battle;
    mLaneSpawn.BlockSize = blockSize;
    mHasLaneNetwork = bNetwork;
    TArray<int32> slotInWall;
    TArray<int32> slotHalo;
    SOLLaneNetwork::SumBlockCounts(mBlockSlot, mBlockInWall, slotCount, slotInWall);
    SOLLaneNetwork::SumBlockCounts(mBlockSlot, mBlockHalo, slotCount, slotHalo);
    TArray<int32> laneInWall;
    TArray<int32> laneHalo;
    TArray<double> laneNominalM;
    TArray<double> laneRadiusAM;
    TArray<double> laneRadiusBM;
    laneInWall.SetNumZeroed(columnEnd);
    laneHalo.SetNumZeroed(columnEnd);
    laneNominalM.SetNumZeroed(columnEnd);
    laneRadiusAM.SetNumZeroed(columnEnd);
    laneRadiusBM.SetNumZeroed(columnEnd);
    TArray<int32> slotByColumn;
    slotByColumn.Init(INDEX_NONE, columnEnd);
    const double epochCenturies = mEpochSecondsSinceJ2000 / SOL::SECONDS_PER_JULIAN_CENTURY;
    const TConstArrayView<FSOLLanePair> slots = bNetwork ? GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>()
        ->GetNetwork().GetSlots() : TConstArrayView<FSOLLanePair>();
    for (int32 slot = 0; slot < slotCount; ++slot)
    {
        const int32 column = mGroups.LaneSlotColumn(slot);
        if (!slotByColumn.IsValidIndex(column))
        {
            continue;
        }
        slotByColumn[column] = slot;
        laneInWall[column] = slotInWall[slot];
        laneHalo[column] = slotHalo[slot];
        const int32 bodyA = mGroups.LaneBodyA[column];
        const int32 bodyB = mGroups.LaneBodyB[column];
        if (slot < mGroups.LaneCount && bodyA >= 0 && bodyA < registry.Num() && bodyB >= 0 && bodyB < registry.Num())
        {
            laneNominalM[column] = NpcLaneNominalLengthM(registry, bodyA, bodyB, epochCenturies);
            laneRadiusAM[column] = registry.GetRadiusM(bodyA);
            laneRadiusBM[column] = registry.GetRadiusM(bodyB);
        }
        else if (slot >= mGroups.LaneCount && slots.IsValidIndex(slot) && slots[slot].NodeA != INDEX_NONE)
        {
            const TConstArrayView<FSOLLaneNode> nodes = mNodeTable.Nodes;
            laneNominalM[column] = SOLLaneNetwork::LaneNominalLengthM(nodes, slots[slot].NodeA, slots[slot].NodeB);
            laneRadiusAM[column] = nodes[slots[slot].NodeA].ClearanceRadiusM;
            laneRadiusBM[column] = nodes[slots[slot].NodeB].ClearanceRadiusM;
        }
    }
    SOLNpcRail::BuildLaneProfiles(mGroups, laneInWall, laneHalo, laneNominalM, laneRadiusAM, laneRadiusBM);
    if (USOLLaneTrafficSubsystem* laneTraffic = GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>())
    {
        // The lane table for the HUD's merge readout and the AUTO lane rules (SDD 50 section 4.10), per column, with
        // each column's slot so its open flag follows the slot's window (Q30). A lane ending on a node without a
        // registry body (an asteroid) has no merge view yet
        laneTraffic->SetLanes(mGroups.LaneBodyA, mGroups.LaneBodyB, mGroups.LaneProfile);
        if (bNetwork)
        {
            laneTraffic->SetLaneSlots(slotByColumn);

            // The block steps start from the spawn epoch's slots (53c2b): a slot owning blocks is held, so it is never
            // reused under them, until it releases them (StepLaneBlocks)
            const int32 slotsNow = ReadSlotsNow();
            mBlockSlotLanes = mSlotLanesNow;
            mBlockSlotInSet = mSlotInSetNow;
            mHasPendingRelease = false;
            TArray<int32> slotShips;
            SOLLaneNetwork::SumBlockCounts(mBlockSlot, mBlockShips, slotsNow, slotShips);
            for (int32 slot = 0; slot < slotsNow; ++slot)
            {
                laneTraffic->SetNetworkSlotHold(slot, SOLLaneNetwork::LaneSlotHold(slotShips[slot] > 0, false));
            }
        }
    }
    mIsBlockStepPending = false;
    mIsBlockReshareRequested = false;
    mColumnLaneShips.Init(0, columnEnd);
    {
        // Lane ships per lane column (the GPU layer's lane histogram after a move)
        TArray<int32> slotShips;
        SOLLaneNetwork::SumBlockCounts(mBlockSlot, mBlockShips, slotCount, slotShips);
        for (int32 slot = 0; slot < slotCount; ++slot)
        {
            const int32 column = mGroups.LaneSlotColumn(slot);
            if (mColumnLaneShips.IsValidIndex(column))
            {
                mColumnLaneShips[column] = slotShips[slot];
            }
        }
    }
    mLaneEntities.Init(FMassEntityHandle(), counts.Lane);

    // Rails for every NPC index, in parallel blocks (stateless per-index hash), and each block's largest lane peak. A
    // lane ship's group is its block's slot column (LaneRailGroup), never the slot number; a ship of a free block is
    // parked (none at spawn: every block is assigned)
    const int32 blockCount = FMath::DivideAndRoundUp(count, NPC_SPAWN_BLOCK);
    const int32 clusterBase = mGroups.LaneCount;
    const int32 battleBase = mGroups.LaneCount + mGroups.ClusterCount;
    const FSOLLaneProfile noLaneProfile;
    TArray<double> blockMaxPeakMps;
    blockMaxPeakMps.SetNumZeroed(blockCount * columnEnd);
    mRails.SetNumUninitialized(count);
    ParallelFor(blockCount, [&](int32 block)
    {
        const int32 begin = block * NPC_SPAWN_BLOCK;
        const int32 end = FMath::Min((block + 1) * NPC_SPAWN_BLOCK, count);
        for (int32 index = begin; index < end; ++index)
        {
            const SOLNpcScale::FSpawn spawn = SOLNpcScale::MakeSpawn(seed, index, counts, layout, battle, mBlockSlot,
                blockSize);
            const int32 local = FMath::Max(spawn.GroupIndex, 0);
            switch (spawn.Kind)
            {
            case SOLNpcScale::EPlacementKind::Lane:
                mRails[index] = SOLNpcRail::LaneRailFromSpawn(spawn, mGroups);
                break;
            case SOLNpcScale::EPlacementKind::Cluster:
                mRails[index] = SOLNpcRail::FromSpawn(spawn, clusterBase, noLaneProfile,
                    clusterGMs.IsValidIndex(local) ? clusterGMs[local] : 0.0);
                break;
            case SOLNpcScale::EPlacementKind::Battle:
            default:
                mRails[index] = SOLNpcRail::FromSpawn(spawn, battleBase, noLaneProfile, 0.0);
                break;
            }
        }
        SOLNpcRail::AccumulateLaneMaxPeak(MakeArrayView(mRails).Slice(begin, end - begin),
            MakeArrayView(blockMaxPeakMps).Slice(block * columnEnd, columnEnd));
    });
    for (int32 lane = 0; lane < columnEnd; ++lane)
    {
        for (int32 block = 0; block < blockCount; ++block)
        {
            mGroups.LaneMaxPeakMps[lane] = FMath::Max(mGroups.LaneMaxPeakMps[lane],
                blockMaxPeakMps[block * columnEnd + lane]);
        }
    }
    const double railsS = FPlatformTime::Seconds();

    // Bucket the NPC indices by (group, class) (counting sort), so each bucket is one batch with its shared fragments;
    // parked lane ships (NPC_RAIL_PARKED_GROUP) get a trailing group of their own, never a candidate
    const int32 groupCount = mGroups.Num();
    const int32 classCount = SOLNpcAi::ClassCount();
    const int32 bucketCount = (groupCount + 1) * classCount;
    TArray<int32> bucketStarts;
    bucketStarts.SetNumZeroed(bucketCount + 1);
    TArray<int32> bucketOfIndex;
    bucketOfIndex.SetNumUninitialized(count);

    // Each index's bucket in parallel (the class hash is the costly part), then the counts serially
    ParallelFor(FMath::DivideAndRoundUp(count, NPC_SPAWN_BLOCK), [&](int32 block)
    {
        const int32 end = FMath::Min((block + 1) * NPC_SPAWN_BLOCK, count);
        for (int32 index = block * NPC_SPAWN_BLOCK; index < end; ++index)
        {
            const int32 shipClass = SOLNpcAi::PickClass(SOLNpcScale::HashUnit(seed, static_cast<uint32>(index),
                SOL::NPC_CLASS_HASH_STREAM));
            bucketOfIndex[index] = FMath::Min<int32>(mRails[index].Group, groupCount) * classCount + shipClass;
        }
    });
    for (int32 index = 0; index < count; ++index)
    {
        ++bucketStarts[bucketOfIndex[index] + 1];
    }
    for (int32 bucket = 0; bucket < bucketCount; ++bucket)
    {
        bucketStarts[bucket + 1] += bucketStarts[bucket];
    }
    TArray<int32> order;
    order.SetNumUninitialized(count);
    {
        TArray<int32> cursor(bucketStarts);
        for (int32 index = 0; index < count; ++index)
        {
            order[cursor[bucketOfIndex[index]]++] = index;
        }
    }

    // One archetype (Far: no tier tag); one batch per bucket with its const shared group and class fragments
    const UScriptStruct* const elements[] = {
        FSOLNpcRailFragment::StaticStruct(),
        FSOLNpcStateFragment::StaticStruct(),
        FSOLNpcDeviationFragment::StaticStruct(),
        FSOLNpcCombatFragment::StaticStruct(),
        FSOLNpcChunkWakeFragment::StaticStruct(),
    };

    // Large chunks: fewer chunks for the tier passes to walk (SDD 11 Amendment 6)
    FMassArchetypeCreationParams creationParams(FName(TEXT("SOLNpc")));
    creationParams.ChunkMemorySize = SOL::NPC_CHUNK_MEMORY_BYTES;
    const FMassArchetypeHandle archetype = mEntityManager->CreateArchetype(MakeArrayView(elements), creationParams);
    mEntities.Reserve(count);

    // The entities themselves are created in slices over the next frames (ContinueSpawn), so a 1M spawn does not stall
    // one frame for ~0.35 s (SDD 11 Amendment 6); the GPU far layer draws every rail from the first frame
    mSpawnOrder = MoveTemp(order);
    mSpawnBucketStarts = MoveTemp(bucketStarts);
    mSpawnArchetype = archetype;
    mSpawnCreated = 0;
    mSpawnStartS = startS;
    mSpawnMaxSliceMs = 0.0;
    mSpawnSlices = 0;
    mCombat.Reset(count);
    mHasPrevCombatFrameBody = false;

    // Temporal-cull bounds per group (relative to the group's anchor point): cluster orbits at most at the inner
    // circular speed, battle slots are fixed (lanes: the bound changes as the endpoints move, so it is recomputed every
    // frame from the lane's largest peak, current leg and endpoint speed, SOLNpcRail::LaneWakeSpeedBoundMps)
    mGroupMaxSpeedMps.SetNumZeroed(groupCount);
    for (int32 cluster = 0; cluster < mGroups.ClusterCount; ++cluster)
    {
        const double innerM = FMath::Max(FMath::Min(mGroups.ClusterInnerM[cluster], mGroups.ClusterOuterM[cluster]),
            1.0);
        mGroupMaxSpeedMps[clusterBase + cluster] = SOL::NPC_WAKE_SPEED_MARGIN
            * FMath::Sqrt(FMath::Max(clusterGMs[cluster], 0.0) / innerM);
    }
    mGroupTravelM.SetNumZeroed(groupCount);
    mGroupShipTravelM.SetNumZeroed(groupCount);
    mGroupPrevViewM.SetNumZeroed(groupCount);
    mGroupWakeClockM.SetNumZeroed(groupCount);
    mGroupWakeDirty.SetNumZeroed(groupCount);
    mGroupMidMarginM.SetNumZeroed(groupCount);
    mLastRailTimeS = 0.0;
    mHasGroupPrevView = false;
    mDistancesM.SetNumUninitialized(count);

    mSpawnedCount = count;
    ++mPopulationRevision;
    const double endS = FPlatformTime::Seconds();
    UE_LOG(LogSOL, Log, TEXT("NpcSpawn count=%d battle=%d lane=%d cluster=%d groups=%d despawnRailsMs=%.1f ")
        TEXT("setupMs=%.1f memBeforeMB=%.0f memMB=%.0f"), count, counts.Battle, counts.Lane, counts.Cluster,
        groupCount, (railsS - startS) * 1000.0, (endS - railsS) * 1000.0, memoryBeforeMB, UsedPhysicalMB());
    mSpawnMaxSliceMs = (endS - startS) * 1000.0;
    ContinueSpawn();
}

//////////////////////////////////////////////////////////////////////////
// Creates the next slice of a pending spawn's entities (at most NPC_SPAWN_ENTITIES_PER_FRAME per call)
void USOLNpcSubsystem::ContinueSpawn()
{
    if (mSpawnCreated >= mSpawnOrder.Num() || !mEntityManager.IsValid())
    {
        return;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::ContinueSpawn);
    const double sliceStartS = FPlatformTime::Seconds();
    const int32 classCount = SOLNpcAi::ClassCount();
    const int32 bucketCount = mSpawnBucketStarts.Num() - 1;
    int32 budget = SOL::NPC_SPAWN_ENTITIES_PER_FRAME;
    int32 bucket = Algo::UpperBound(mSpawnBucketStarts, mSpawnCreated) - 1;
    while (budget > 0 && bucket < bucketCount)
    {
        // The rest of this bucket, or as much of it as the slice allows
        const int32 count = FMath::Min(mSpawnBucketStarts[bucket + 1] - mSpawnCreated, budget);
        if (count <= 0)
        {
            ++bucket;
            continue;
        }
        const int32 shipClass = bucket % classCount;
        const int32 group = bucket / classCount;
        const double maxHealth = SOLNpcAi::GetClass(shipClass).MaxHealth;
        FMassArchetypeSharedFragmentValues sharedValues;
        sharedValues.Add(mEntityManager->GetOrCreateConstSharedFragment(FSOLNpcGroupFragment::Make(group)));

        // New ships (wake key 0) may join a chunk whose minimum wake key was computed before they existed: the group's
        // chunks are visited in full by the next Evaluate that sees the group as a candidate
        if (mGroupWakeDirty.IsValidIndex(group))
        {
            mGroupWakeDirty[group] = 1;
        }
        sharedValues.Add(mEntityManager->GetOrCreateConstSharedFragment(FSOLNpcClassFragment::Make(shipClass)));
        sharedValues.Sort();
        const int32 firstEntity = mEntities.Num();
        {
            const TSharedRef<FMassEntityManager::FEntityCreationContext> creation =
                mEntityManager->BatchCreateEntities(mSpawnArchetype, sharedValues, count, mEntities);
            for (int32 local = 0; local < count; ++local)
            {
                // One entity lookup for both fragments; alive at full health
                const int32 npcIndex = mSpawnOrder[mSpawnCreated + local];
                const int32 laneLocal = npcIndex - mLaneSpawn.Counts.Battle;
                if (mLaneEntities.IsValidIndex(laneLocal))
                {
                    mLaneEntities[laneLocal] = mEntities[firstEntity + local];
                }
                const FMassEntityView view(*mEntityManager, mEntities[firstEntity + local]);
                view.GetFragmentData<FSOLNpcRailFragment>().Rail = mRails[npcIndex];
                FSOLNpcCombatFragment& combat = view.GetFragmentData<FSOLNpcCombatFragment>();
                combat.NpcIndex = npcIndex;
                combat.Health = static_cast<float>(maxHealth);
            }
        }
        mSpawnCreated += count;
        budget -= count;
    }
    const double endS = FPlatformTime::Seconds();
    ++mSpawnSlices;
    mSpawnMaxSliceMs = FMath::Max(mSpawnMaxSliceMs, (endS - sliceStartS) * 1000.0);
    if (mSpawnCreated >= mSpawnOrder.Num())
    {
        UE_LOG(LogSOL, Log, TEXT("NpcSpawnDone count=%d slices=%d totalMs=%.1f maxSliceMs=%.1f memMB=%.0f"),
            mSpawnCreated, mSpawnSlices, (endS - mSpawnStartS) * 1000.0, mSpawnMaxSliceMs, UsedPhysicalMB());
        mSpawnOrder.Empty();
        mSpawnBucketStarts.Empty();
        mSpawnCreated = 0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Per-frame update: respawn if requested, tiers, Mid motion, stats, ramp; bound to OnUniverseUpdated
void USOLNpcSubsystem::Update()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::Update);
    const double realDeltaS = FMath::Min(FApp::GetDeltaTime(), SOL::MAX_FRAME_DELTA_S);

    // A console change of sol.Npc.CombatModel overrides the Demo page's model
    const int32 cvarCombatModel = CVarNpcCombatModel.GetValueOnGameThread();
    if (cvarCombatModel != mLastSeenCVarCombatModel)
    {
        mLastSeenCVarCombatModel = cvarCombatModel;
        mCombatModel = cvarCombatModel;
    }
    StepRamp(realDeltaS);
    UpdateOverlay(realDeltaS);
    if ((mRequestedCount != mSpawnedCount || mIsRespawnRequested) && Ship->HasPlayerShip())
    {
        mIsRespawnRequested = false;
        const bool bInitialSpawn = mSpawnedCount == 0;
        Spawn(mRequestedCount);
        if (bInitialSpawn)
        {
            MatchShipToBattleBody();
        }
        FParse::Value(FCommandLine::Get(), SOL::CommandLine::NPC_FACE_BATTLE, mPendingFaceBattle);
    }
    ContinueSpawn();
    if (mSpawnedCount == 0 || TierProcessor == nullptr || MidProcessor == nullptr || FullAiProcessor == nullptr)
    {
        mOnNpcsUpdated.Broadcast();
        return;
    }
    const double startS = FPlatformTime::Seconds();

    // Rail frame and the group cull against the Mid keep radius
    const double aiKeepScale = 1.0 + FMath::Max(mOptions.AiHysteresis, 0.0);
    const FVector3d viewpointM = Anchor->GetViewpointM();
    const double railTimeS = SOLNpcRail::RailTimeS(SimClock->GetClock().GetSecondsSinceJ2000(),
        mEpochSecondsSinceJ2000);
    if (USOLLaneTrafficSubsystem* laneTraffic = GetWorld()->GetSubsystem<USOLLaneTrafficSubsystem>())
    {
        // The lane network's epoch (SDD 53 5.3): nothing while the epoch holds, a step per new epoch, a rebuild on
        // a jump (a clock set, even within the catch-up limit, or a move past it). Then the ship blocks follow it
        // (53c2b): releases and episode takes after a step, an exact re-share after a rebuild, before the frame and
        // the windows are built from the columns
        if (SimClock->GetTimeSetCount() != mSeenClockTimeSetCount)
        {
            mSeenClockTimeSetCount = SimClock->GetTimeSetCount();
            laneTraffic->MarkNetworkTimeJump();
        }
        StepLaneBlocks(laneTraffic->AdvanceNetwork(SimClock->GetClock().GetSecondsSinceJ2000()));
    }
    SOLNpcRail::BuildFrame(mGroups, BodyRegistry->GetRegistry().GetPositionsM(), railTimeS, mRailFrame,
        LaneEndpointPositions());
    UpdateLaneWindows(railTimeS, viewpointM);
    if (mPendingFaceBattle != INDEX_NONE)
    {
        FaceBattle(mPendingFaceBattle);
        mPendingFaceBattle = INDEX_NONE;
    }
    mGroupCandidate.SetNumUninitialized(mGroups.Num(), EAllowShrinking::No);
    mStats.CandidateGroups = 0;
    const double railStepS = FMath::Abs(railTimeS - mLastRailTimeS);
    mLastRailTimeS = railTimeS;
    const TConstArrayView<FVector3d> bodyVelocitiesMps = BodyRegistry->GetRegistry().GetVelocitiesMps();
    for (int32 group = 0; group < mGroups.Num(); ++group)
    {
        // Ship speed bound this frame: a lane's (SDD 50 section 4.7) grows with its current leg and its endpoints'
        // relative speed, from the lane's largest peak frozen at spawn
        double shipSpeedBoundMps = mGroupMaxSpeedMps[group];
        if (mGroups.IsLaneColumn(group))
        {
            // An end on a node without a registry body (an asteroid) is bounded by its node speed bound
            const int32 bodyA = mGroups.LaneBodyA[group];
            const int32 bodyB = mGroups.LaneBodyB[group];
            const auto speedBoundOf = [this, &bodyVelocitiesMps](const int32 body)
            {
                const int32 node = body - bodyVelocitiesMps.Num();
                return bodyVelocitiesMps.IsValidIndex(body) ? bodyVelocitiesMps[body].Size()
                    : (mNodeTable.MaxSpeedMps.IsValidIndex(node) ? mNodeTable.MaxSpeedMps[node] : 0.0);
            };
            const double endpointSpeedMps = bodyVelocitiesMps.IsValidIndex(bodyA)
                && bodyVelocitiesMps.IsValidIndex(bodyB) ? (bodyVelocitiesMps[bodyB] - bodyVelocitiesMps[bodyA]).Size()
                : speedBoundOf(bodyA) + speedBoundOf(bodyB);
            shipSpeedBoundMps = SOL::NPC_WAKE_SPEED_MARGIN * SOLNpcRail::LaneWakeSpeedBoundMps(mGroups, mRailFrame,
                group, endpointSpeedMps);
        }

        // Wake clock: the viewer's path relative to the group's anchor point plus the ships' possible travel; both
        // only grow, so a sleeping ship's key stays a valid lower bound on when it could reach the keep radius
        const FVector3d viewFromGroupM = viewpointM - mRailFrame.GroupPointAM[group];
        if (mHasGroupPrevView)
        {
            mGroupTravelM[group] += (viewFromGroupM - mGroupPrevViewM[group]).Size();
        }
        mGroupPrevViewM[group] = viewFromGroupM;
        mGroupShipTravelM[group] += shipSpeedBoundMps * railStepS;
        mGroupWakeClockM[group] = mGroupTravelM[group] + mGroupShipTravelM[group];

        // A lane's Mid radius grows by its hand-over margin (SDD 50 section 4.6): every ship the far layer's float
        // evaluation puts inside the Mid radius is then already a CPU ship, so the CPU point layer can draw it
        mGroupMidMarginM[group] = mGroups.IsLaneColumn(group)
            ? SOLNpcRail::LaneHandOverMarginM(mGroups, mRailFrame, group, mOptions.MidAiRadiusM) : 0.0;
        const bool bCandidate = SOLNpcRail::GroupDistanceM(mGroups, mRailFrame, group, viewpointM)
            < (mOptions.MidAiRadiusM + mGroupMidMarginM[group]) * aiKeepScale;
        mGroupCandidate[group] = bCandidate ? 1 : 0;
        mStats.CandidateGroups += bCandidate ? 1 : 0;
    }
    mHasGroupPrevView = true;

    USOLNpcTierProcessor::FFrameInputs inputs;
    inputs.RailFrame = &mRailFrame;
    inputs.GroupCandidate = mGroupCandidate;
    inputs.GroupWakeClockM = mGroupWakeClockM;
    inputs.GroupWakeDirty = mGroupWakeDirty;
    inputs.MidKeepRadiusM = mOptions.MidAiRadiusM * aiKeepScale;
    inputs.GroupMidMarginM = mGroupMidMarginM;
    inputs.ViewpointM = viewpointM;
    inputs.AiHysteresis = mOptions.AiHysteresis;
    inputs.RenderHysteresis = mOptions.RenderHysteresis;
    inputs.GroupOpenStartRailS = mGroupOpenStartRailS;
    inputs.GroupCloseRailS = mGroupCloseRailS;
    TierProcessor->SetOutputs(&mDistancesM, &mCpuShips);

    // Runs one tier-processor pass with its own processing context (commands flush when it ends)
    const auto runTierPass = [this, &inputs, realDeltaS](ESOLNpcTierPass pass)
    {
        TierProcessor->SetPass(pass, inputs);
        UE::Mass::FProcessingContext context(*mEntityManager, static_cast<float>(realDeltaS));
        context.SetCommandBuffer(mCommandBuffer);
        UE::Mass::Executor::Run(*TierProcessor, context);
    };

    // Evaluate rails and distances of awake candidates; the distances inside the Mid keep radius feed the budgets
    runTierPass(ESOLNpcTierPass::Evaluate);
    SOLNpcScale::ClearVisitedWakeDirty(mGroupWakeDirty, mGroupCandidate);
    const double evaluatedS = FPlatformTime::Seconds();
    const TConstArrayView<double> budgetDistancesM(mDistancesM.GetData(),
        FMath::Min(TierProcessor->GetBudgetDistanceCount(), mDistancesM.Num()));

    // Budgeted radii: each tier's radius shrinks so its budget holds (render radii never exceed the Mid radius).
    // Full and Mid, then mesh and instanced, are independent pairs: each pair runs on two threads (Amendment 6)
    ParallelFor(2, [this, &inputs, budgetDistancesM](const int32 tier)
    {
        if (tier == 0)
        {
            inputs.FullRadiusM = SOLNpcScale::BudgetedRadiusM(budgetDistancesM, mOptions.FullAiRadiusM,
                mOptions.AiHysteresis, mOptions.FullAiBudget, mBudgetScratch[0]);
            return;
        }
        inputs.MidRadiusM = SOLNpcScale::BudgetedRadiusM(budgetDistancesM, mOptions.MidAiRadiusM,
            mOptions.AiHysteresis, mOptions.MidAiBudget, mBudgetScratch[1]);
    });

    // A Mid budget that shrank the Mid radius below the Full radius also caps the Full radius (Full stays inside Mid)
    inputs.FullRadiusM = FMath::Min(inputs.FullRadiusM, inputs.MidRadiusM);
    inputs.bMidBudgetForced = inputs.MidRadiusM < mOptions.MidAiRadiusM;
    inputs.PixelAngleRad = ViewPixelAngleRad();
    inputs.PlumeGateM = ViewPlumeGateM();
    ParallelFor(2, [this, &inputs, budgetDistancesM](const int32 tier)
    {
        if (tier == 0)
        {
            inputs.MeshRadiusM = SOLNpcScale::BudgetedRadiusM(budgetDistancesM,
                FMath::Min(mOptions.MeshRadiusM, inputs.MidRadiusM), mOptions.RenderHysteresis, mOptions.MeshBudget,
                mBudgetScratch[0]);
            return;
        }
        inputs.InstancedRadiusM = SOLNpcScale::BudgetedRadiusM(budgetDistancesM,
            FMath::Min(mOptions.InstancedRadiusM, inputs.MidRadiusM), mOptions.RenderHysteresis,
            mOptions.InstancedBudget, mBudgetScratch[1]);
    });
    const double budgetedS = FPlatformTime::Seconds();

    // Tiers (archetype moves flushed at the end of the pass), Mid motion, then the renderer's list
    runTierPass(ESOLNpcTierPass::Select);
    const double selectedS = FPlatformTime::Seconds();
    {
        MidProcessor->SetRealDeltaSeconds(realDeltaS);
        UE::Mass::FProcessingContext context(*mEntityManager, static_cast<float>(realDeltaS));
        context.SetCommandBuffer(mCommandBuffer);
        UE::Mass::Executor::Run(*MidProcessor, context);
    }
    const double midS = FPlatformTime::Seconds();
    UpdateFullAi(realDeltaS);
    const double fullAiS = FPlatformTime::Seconds();
    runTierPass(ESOLNpcTierPass::Collect);
    const double endS = FPlatformTime::Seconds();

    // Statistics
    mStats.Total = mSpawnedCount;
    mStats.Full = TierProcessor->GetAiTierCount(0);
    mStats.Mid = TierProcessor->GetAiTierCount(1);
    mStats.Far = mSpawnedCount - mStats.Full - mStats.Mid;
    mStats.Mesh = TierProcessor->GetRenderTierCount(0);
    mStats.Instanced = TierProcessor->GetRenderTierCount(1);
    mStats.Point = mStats.Full + mStats.Mid - mStats.Mesh - mStats.Instanced;
    mStats.Candidates = TierProcessor->GetEvaluatedCount();
    mStats.TierChanges = TierProcessor->GetTierChanges();
    mStats.EvaluateMs = (evaluatedS - startS) * 1000.0;
    mStats.BudgetMs = (budgetedS - evaluatedS) * 1000.0;
    mStats.SelectMs = (selectedS - budgetedS) * 1000.0;
    mStats.MidMs = (midS - selectedS) * 1000.0;
    mStats.FullAiMs = (fullAiS - midS) * 1000.0;
    mStats.CollectMs = (endS - fullAiS) * 1000.0;
    mStats.Combat = mCombat.GetStats();
    mStats.TotalMs = (endS - startS) * 1000.0;
    mStats.FullRadiusM = inputs.FullRadiusM;
    mStats.MidRadiusM = inputs.MidRadiusM;
    mStats.MeshRadiusM = inputs.MeshRadiusM;
    mStats.InstancedRadiusM = inputs.InstancedRadiusM;
    TRACE_COUNTER_SET(NpcFull, mStats.Full);
    TRACE_COUNTER_SET(NpcMid, mStats.Mid);
    TRACE_COUNTER_SET(NpcCandidates, mStats.Candidates);

    mOnNpcsUpdated.Broadcast();
    LogStats(realDeltaS);
}

//////////////////////////////////////////////////////////////////////////
// Runs the Full AI and NPC combat for this frame (gather, step, scatter)
void USOLNpcSubsystem::UpdateFullAi(const double realDeltaS)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(USOLNpcSubsystem::UpdateFullAi);

    // Everything is relative to the battles' body, so the AI and the bolts are unaffected by its orbital motion
    const TConstArrayView<FVector3d> bodyPositionsM = BodyRegistry->GetRegistry().GetPositionsM();
    mCombatFrameBodyM = bodyPositionsM.IsValidIndex(mGroups.BattleBody) ? bodyPositionsM[mGroups.BattleBody]
        : FVector3d::ZeroVector;
    USOLNpcFullAiProcessor::FRunInputs inputs;
    inputs.Combat = &mCombat;
    inputs.RailFrame = &mRailFrame;
    inputs.FrameBodyM = mCombatFrameBodyM;
    inputs.FrameBodyStepM = mCombatFrameBodyM - mPrevCombatFrameBodyM;
    inputs.bHasFrameBodyStep = mHasPrevCombatFrameBody;
    inputs.RealDeltaS = realDeltaS;
    mPrevCombatFrameBodyM = mCombatFrameBodyM;
    mHasPrevCombatFrameBody = true;

    // Runs one pass of the Full AI processor
    const auto runPass = [this, &inputs, realDeltaS](const ESOLNpcFullAiPass pass)
    {
        FullAiProcessor->SetPass(pass, inputs);
        UE::Mass::FProcessingContext context(*mEntityManager, static_cast<float>(realDeltaS));
        context.SetCommandBuffer(mCommandBuffer);
        UE::Mass::Executor::Run(*FullAiProcessor, context);
    };

    mCombat.BeginGather();
    runPass(ESOLNpcFullAiPass::Gather);
    FSOLNpcCombatInputs combatInputs;
    combatInputs.DeltaS = realDeltaS;
    combatInputs.CameraPositionM = Anchor->GetViewpointM() - mCombatFrameBodyM;
    combatInputs.Model = SOLNpcAi::CombatModelFromInt(mCombatModel);
    combatInputs.bCombatEnabled = mOptions.bNpcCombat && CVarNpcCombat.GetValueOnGameThread() != 0;
    combatInputs.Seed = mOptions.Seed;
    mCombat.Step(combatInputs);
    runPass(ESOLNpcFullAiPass::Scatter);
    mCombat.EndFrame();
}

//////////////////////////////////////////////////////////////////////////
// Advances the ramp by one frame and logs finished stages
void USOLNpcSubsystem::StepRamp(const double realDeltaS)
{
    if (mRamp.Phase == SOLNpcScale::ERampPhase::Idle)
    {
        return;
    }
    if (mRamp.Phase == SOLNpcScale::ERampPhase::Done)
    {
        if (mIsRampQuitWhenDone)
        {
            mIsRampQuitWhenDone = false;
            UE_LOG(LogSOL, Log, TEXT("NpcRamp done; quitting"));
            FPlatformMisc::RequestExit(false, TEXT("SOLNpcRamp"));
        }
        return;
    }

    // Measured frames: the previous frame's real time, and the NPC update cost
    const SOLNpcScale::FRampStep step = SOLNpcScale::StepRamp(mRamp, mRampStages.Num(), mOptions.RampSettleS,
        mOptions.RampMeasureS, realDeltaS);
    if (step.bRecordFrame)
    {
        mRampFrameMs.Add(realDeltaS * 1000.0);
        mRampNpcMs.Add(mStats.TotalMs);
    }
    if (step.bStageFinished)
    {
        const SOLNpcScale::FFrameStats frameStats = SOLNpcScale::SummarizeFrameTimes(mRampFrameMs);
        const SOLNpcScale::FFrameStats npcStats = SOLNpcScale::SummarizeFrameTimes(mRampNpcMs);
        const int32 stageCount = mRampStages[step.FinishedStageIndex];
        const bool bPass = SOLNpcScale::MeetsFrameTarget(frameStats, SOL::NPC_TARGET_FPS);
        mLastRampResult = SOLNpcStatsText::FormatRampResult(stageCount, frameStats.P95Ms, bPass);
        UE_LOG(LogSOL, Display, TEXT("%s"), *SOLNpcScale::FormatRampStageLine(stageCount, frameStats,
            UsedPhysicalMB(), bPass));
        UE_LOG(LogSOL, Display, TEXT("NpcRampDetail stage=%d npcMeanMs=%.3f npcP95Ms=%.3f npcMaxMs=%.3f full=%d ")
            TEXT("mid=%d candidates=%d groups=%d gameMs=%.2f renderMs=%.2f gpuMs=%.2f"), stageCount, npcStats.MeanMs,
            npcStats.P95Ms, npcStats.MaxMs, mStats.Full, mStats.Mid, mStats.Candidates, mStats.CandidateGroups,
            FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime),
            FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
        mRampFrameMs.Reset();
        mRampNpcMs.Reset();
    }
    mRamp = step.State;
    const int32 stageCount = SOLNpcScale::StageNpcCount(mRamp, mRampStages);
    if (stageCount != INDEX_NONE)
    {
        mRequestedCount = stageCount;
    }
}

//////////////////////////////////////////////////////////////////////////
// Logs the periodic NpcStats line
void USOLNpcSubsystem::LogStats(const double realDeltaS)
{
    mStatsLogElapsedS += realDeltaS;
    ++mStatsLogFrames;
    mStatsLogNpcMsSum += mStats.TotalMs;
    mStatsLogNpcMsMax = FMath::Max(mStatsLogNpcMsMax, mStats.TotalMs);
    mStatsLogFrameMsSum += realDeltaS * 1000.0;
    mStatsLogFrameMsMax = FMath::Max(mStatsLogFrameMsMax, realDeltaS * 1000.0);
    mStatsLogFullAiMsSum += mStats.FullAiMs;
    const double phaseMs[] = { mStats.EvaluateMs, mStats.BudgetMs, mStats.SelectMs, mStats.MidMs, mStats.FullAiMs,
        mStats.CollectMs, mStats.Combat.AiMs };
    for (int32 phase = 0; phase < UE_ARRAY_COUNT(phaseMs); ++phase)
    {
        mStatsLogPhaseMaxMs[phase] = FMath::Max(mStatsLogPhaseMaxMs[phase], phaseMs[phase]);
    }
    mStatsLogGpuMsSum += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
    mStatsLogGameMsSum += FPlatformTime::ToMilliseconds(GGameThreadTime);
    if (mStatsLogElapsedS < SOL::NPC_STATS_LOG_PERIOD_S)
    {
        return;
    }
    const double frames = static_cast<double>(mStatsLogFrames);
    UE_LOG(LogSOL, Log, TEXT("NpcStats total=%d full=%d mid=%d far=%d mesh=%d inst=%d point=%d cand=%d groups=%d ")
        TEXT("changes=%d npcMs=%.3f npcMaxMs=%.3f eval=%.3f budget=%.3f select=%.3f mid=%.3f fullAi=%.3f ")
        TEXT("fullAiMean=%.3f collect=%.3f frameMs=%.2f gameMs=%.2f renderMs=%.2f gpuMs=%.2f gpuMean=%.2f ")
        TEXT("gameMean=%.2f fullR=%.0f midR=%.0f meshR=%.0f instR=%.0f memMB=%.0f"),
        mStats.Total, mStats.Full, mStats.Mid, mStats.Far, mStats.Mesh, mStats.Instanced, mStats.Point,
        mStats.Candidates, mStats.CandidateGroups, mStats.TierChanges, mStatsLogNpcMsSum / frames, mStatsLogNpcMsMax,
        mStats.EvaluateMs, mStats.BudgetMs, mStats.SelectMs, mStats.MidMs, mStats.FullAiMs,
        mStatsLogFullAiMsSum / frames, mStats.CollectMs,
        mStatsLogFrameMsSum / frames, FPlatformTime::ToMilliseconds(GGameThreadTime),
        FPlatformTime::ToMilliseconds(GRenderThreadTime), FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()),
        mStatsLogGpuMsSum / frames, mStatsLogGameMsSum / frames, mStats.FullRadiusM, mStats.MidRadiusM,
        mStats.MeshRadiusM, mStats.InstancedRadiusM, UsedPhysicalMB());
    const FSOLNpcCombatStats& combat = mStats.Combat;
    UE_LOG(LogSOL, Log, TEXT("NpcPhaseMax frame=%.2f eval=%.3f budget=%.3f select=%.3f mid=%.3f fullAi=%.3f ")
        TEXT("collect=%.3f combatAi=%.3f"), mStatsLogFrameMsMax, mStatsLogPhaseMaxMs[0], mStatsLogPhaseMaxMs[1],
        mStatsLogPhaseMaxMs[2], mStatsLogPhaseMaxMs[3], mStatsLogPhaseMaxMs[4], mStatsLogPhaseMaxMs[5],
        mStatsLogPhaseMaxMs[6]);
    UE_LOG(LogSOL, Log, TEXT("NpcCombat model=%d ships=%d alive=%d bolts=%d real=%d shots=%d stat=%d hits=%d ")
        TEXT("kills=%d respawns=%d dropped=%d fullAiMs=%.3f aiMs=%.3f boltMs=%.3f fireMs=%.3f"),
        mCombatModel, combat.Ships, combat.Alive, combat.LiveBolts, combat.RealBolts,
        combat.Shots, combat.StatisticalShots, combat.Hits, combat.Kills, combat.Respawns, combat.DroppedBolts,
        mStats.FullAiMs, combat.AiMs, combat.BoltMs, combat.FireMs);
    mStatsLogElapsedS = 0.0;
    mStatsLogFrames = 0;
    mStatsLogNpcMsSum = 0.0;
    mStatsLogNpcMsMax = 0.0;
    mStatsLogFrameMsSum = 0.0;
    mStatsLogFrameMsMax = 0.0;
    mStatsLogFullAiMsSum = 0.0;
    for (double& phaseMaxMs : mStatsLogPhaseMaxMs)
    {
        phaseMaxMs = 0.0;
    }
    mStatsLogGpuMsSum = 0.0;
    mStatsLogGameMsSum = 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Records the frame time and refreshes the stats overlay's text at 4 Hz while it is on
void USOLNpcSubsystem::UpdateOverlay(const double realDeltaS)
{
    if (mOverlayFrameMs.IsEmpty())
    {
        return;
    }
    mOverlayFrameMs[mOverlayFrameCursor] = realDeltaS * 1000.0;
    mOverlayFrameCursor = (mOverlayFrameCursor + 1) % mOverlayFrameMs.Num();
    mOverlayRefreshElapsedS += realDeltaS;
    if (!mIsStatsVisible || mOverlayRefreshElapsedS < SOL::NPC_STATS_REFRESH_PERIOD_S)
    {
        return;
    }
    mOverlayRefreshElapsedS = 0.0;

    // The widget is made on first use, once the local player controller exists, below the menus
    if (StatsWidget == nullptr)
    {
        APlayerController* controller = GetWorld() != nullptr ? GetWorld()->GetFirstPlayerController() : nullptr;
        if (controller == nullptr)
        {
            return;
        }
        StatsWidget = CreateWidget<USOLNpcStatsWidget>(controller, USOLNpcStatsWidget::StaticClass());
        if (StatsWidget == nullptr)
        {
            return;
        }
        StatsWidget->AddToViewport(SOL::NPC_STATS_OVERLAY_Z_ORDER);
        StatsWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
    }

    // Frame-time window: mean and p95 of the last NPC_STATS_FRAME_WINDOW frames (zeros before it fills are dropped)
    mOverlayScratch.Reset();
    for (const double frameMs : mOverlayFrameMs)
    {
        if (frameMs > 0.0)
        {
            mOverlayScratch.Add(frameMs);
        }
    }
    const SOLNpcScale::FFrameStats frames = SOLNpcScale::SummarizeFrameTimes(mOverlayScratch);
    SOLNpcStatsText::FOverlayInput input;
    input.FrameMs = frames.MeanMs;
    input.FrameP95Ms = frames.P95Ms;
    input.GameMs = FPlatformTime::ToMilliseconds(GGameThreadTime);
    input.RenderMs = FPlatformTime::ToMilliseconds(GRenderThreadTime);
    input.GpuMs = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
    input.NpcMs = mStats.TotalMs;
    input.Total = mStats.Total;
    input.Far = mStats.Far;
    input.Mid = mStats.Mid;
    input.Full = mStats.Full;
    input.Point = mStats.Point;
    input.Instanced = mStats.Instanced;
    input.Mesh = mStats.Mesh;
    input.Awake = mStats.Candidates;
    input.TierChanges = mStats.TierChanges;
    input.bCombat = mOptions.bNpcCombat && CVarNpcCombat.GetValueOnGameThread() != 0;
    input.CombatModel = GetCombatModel();
    input.CombatShips = mStats.Combat.Ships;
    input.Alive = mStats.Combat.Alive;
    input.LiveBolts = mStats.Combat.LiveBolts;
    input.RealBolts = mStats.Combat.RealBolts;
    input.UsedMemoryMB = UsedPhysicalMB();
    input.RampPhase = mRamp.Phase;
    input.RampStageIndex = mRamp.StageIndex;
    input.RampStageCount = mRampStages.Num();
    input.RampStageNpcCount = mRampStages.IsValidIndex(mRamp.StageIndex) ? mRampStages[mRamp.StageIndex] : 0;
    input.RampPhaseElapsedS = mRamp.PhaseElapsedS;
    input.RampPhaseLengthS = mRamp.Phase == SOLNpcScale::ERampPhase::Settling ? mOptions.RampSettleS
        : mOptions.RampMeasureS;
    input.RampResult = mLastRampResult;
    StatsWidget->SetLines(SOLNpcStatsText::FormatOverlayLines(input));
}