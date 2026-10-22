/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcSubsystem.h"

#include "Game/SOLGameMode.h"
#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcFragments.h"
#include "Npc/SOLNpcFullAiProcessor.h"
#include "Npc/SOLNpcMidProcessor.h"
#include "Npc/SOLNpcStatsText.h"
#include "Npc/SOLNpcStatsWidget.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Ship/SOLShipSubsystem.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Algo/BinarySearch.h"
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
    double fovDeg = SOL::SHIP_CAMERA_BASE_FOV_DEG;
    double viewportWidth = SOL::NPC_REFERENCE_VIEWPORT_WIDTH_PX;
    const APlayerController* controller = GetWorld()->GetFirstPlayerController();
    if (controller != nullptr && controller->PlayerCameraManager != nullptr)
    {
        fovDeg = controller->PlayerCameraManager->GetFOVAngle();
    }
    if (GetWorld()->GetGameViewport() != nullptr)
    {
        FVector2D size;
        GetWorld()->GetGameViewport()->GetViewportSize(size);
        viewportWidth = size.X > 0.0 ? size.X : viewportWidth;
    }

    // The camera's FOV is horizontal: the view width over the viewport's pixel width (square pixels)
    return 2.0 * FMath::Tan(FMath::DegreesToRadians(fovDeg) * 0.5) / viewportWidth;
}

//////////////////////////////////////////////////////////////////////////
// On a population's first spawn, puts the player ship at rest relative to the battles' body (battles stay around it)
void USOLNpcSubsystem::MatchShipToBattleBody()
{
    // An explicit exception to SDD 18 decision 1 (nothing writes velocity directly), for the demo / benchmark scene
    // only: a ship flying an autopilot order keeps its velocity
    const TConstArrayView<FVector3d> bodyVelocitiesMps = BodyRegistry->GetUnrealFrameCache().VelocitiesMps;
    if (mGroups.BattleCount <= 0 || !Ship->HasPlayerShip() || Ship->GetAutopilotBodyIndex() != INDEX_NONE
        || !bodyVelocitiesMps.IsValidIndex(mGroups.BattleBody))
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
    mCombat.Reset(0);
    mHasPrevCombatFrameBody = false;
    mSpawnedCount = 0;
    mStats = FSOLNpcStats();
    ++mPopulationRevision;
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

    // Nominal lane lengths and cluster GMs at the epoch
    const FSOLBodyRegistry& registry = BodyRegistry->GetRegistry();
    const TConstArrayView<FVector3d> bodyPositionsM = registry.GetPositionsM();
    TArray<double> laneLengthsM;
    TArray<double> clusterGMs;
    for (int32 lane = 0; lane < mGroups.LaneCount; ++lane)
    {
        const int32 bodyA = mGroups.LaneBodyA[lane];
        const int32 bodyB = mGroups.LaneBodyB[lane];
        laneLengthsM.Add(bodyPositionsM.IsValidIndex(bodyA) && bodyPositionsM.IsValidIndex(bodyB)
            ? (bodyPositionsM[bodyB] - bodyPositionsM[bodyA]).Size() : 0.0);
    }
    for (int32 cluster = 0; cluster < mGroups.ClusterCount; ++cluster)
    {
        const int32 body = mGroups.ClusterBody[cluster];
        clusterGMs.Add(body != INDEX_NONE ? registry.GetGM(body) : 0.0);
    }

    // Rails for every NPC index, in parallel blocks (stateless per-index hash)
    const uint32 seed = mOptions.Seed;
    const int32 clusterBase = mGroups.LaneCount;
    const int32 battleBase = mGroups.LaneCount + mGroups.ClusterCount;
    mRails.SetNumUninitialized(count);
    ParallelFor(FMath::DivideAndRoundUp(count, NPC_SPAWN_BLOCK), [&](int32 block)
    {
        const int32 end = FMath::Min((block + 1) * NPC_SPAWN_BLOCK, count);
        for (int32 index = block * NPC_SPAWN_BLOCK; index < end; ++index)
        {
            const SOLNpcScale::FSpawn spawn = SOLNpcScale::MakeSpawn(seed, index, counts, layout, battle);
            const int32 local = FMath::Max(spawn.GroupIndex, 0);
            switch (spawn.Kind)
            {
            case SOLNpcScale::EPlacementKind::Lane:
                mRails[index] = SOLNpcRail::FromSpawn(spawn, 0,
                    laneLengthsM.IsValidIndex(local) ? laneLengthsM[local] : 0.0, 0.0);
                break;
            case SOLNpcScale::EPlacementKind::Cluster:
                mRails[index] = SOLNpcRail::FromSpawn(spawn, clusterBase, 0.0,
                    clusterGMs.IsValidIndex(local) ? clusterGMs[local] : 0.0);
                break;
            case SOLNpcScale::EPlacementKind::Battle:
            default:
                mRails[index] = SOLNpcRail::FromSpawn(spawn, battleBase, 0.0, 0.0);
                break;
            }
        }
    });
    const double railsS = FPlatformTime::Seconds();

    // Bucket the NPC indices by (group, class) (counting sort), so each bucket is one batch with its shared fragments
    const int32 groupCount = mGroups.Num();
    const int32 classCount = SOLNpcAi::ClassCount();
    const int32 bucketCount = groupCount * classCount;
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
            bucketOfIndex[index] = FMath::Min<int32>(mRails[index].Group, groupCount - 1) * classCount + shipClass;
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

    // Temporal-cull bounds per group (relative to the group's anchor point): lanes move at the lane speed plus the
    // endpoints' relative speed, cluster orbits at most at the inner circular speed, battle slots are fixed
    // (lanes: the bound changes as the endpoints move, so it is recomputed every frame from their current distance and
    // relative velocity, SOLNpcScale::LaneShipSpeedBoundMps)
    mGroupMaxSpeedMps.SetNumZeroed(groupCount);
    mLaneNominalLengthM = laneLengthsM;
    mLaneMaxSpeedMps = FMath::Max(layout.LaneMinSpeedMps, layout.LaneMaxSpeedMps);
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
    const double railTimeS = SimClock->GetClock().GetSecondsSinceJ2000() - mEpochSecondsSinceJ2000;
    SOLNpcRail::BuildFrame(mGroups, BodyRegistry->GetRegistry().GetPositionsM(), railTimeS, mRailFrame);
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
        // Ship speed bound this frame: a lane's grows with its current length and its endpoints' relative speed
        double shipSpeedBoundMps = mGroupMaxSpeedMps[group];
        if (group < mGroups.LaneCount)
        {
            const int32 bodyA = mGroups.LaneBodyA[group];
            const int32 bodyB = mGroups.LaneBodyB[group];
            const double endpointSpeedMps = bodyVelocitiesMps.IsValidIndex(bodyA)
                && bodyVelocitiesMps.IsValidIndex(bodyB) ? (bodyVelocitiesMps[bodyB] - bodyVelocitiesMps[bodyA]).Size()
                : 0.0;
            shipSpeedBoundMps = SOL::NPC_WAKE_SPEED_MARGIN * SOLNpcScale::LaneShipSpeedBoundMps(mLaneMaxSpeedMps,
                mLaneNominalLengthM.IsValidIndex(group) ? mLaneNominalLengthM[group] : 0.0,
                (mRailFrame.GroupPointBM[group] - mRailFrame.GroupPointAM[group]).Size(), mGroups.LaneRadiusM[group],
                endpointSpeedMps);
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

        const bool bCandidate = SOLNpcRail::GroupDistanceM(mGroups, mRailFrame, group, viewpointM)
            < mOptions.MidAiRadiusM * aiKeepScale;
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
    inputs.ViewpointM = viewpointM;
    inputs.AiHysteresis = mOptions.AiHysteresis;
    inputs.RenderHysteresis = mOptions.RenderHysteresis;
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