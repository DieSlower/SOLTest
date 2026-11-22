/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLThrusterSmoke.h"

#include "Game/SOLGameMode.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcTierProcessor.h"
#include "Npc/SOLNpcVisuals.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Traffic/SOLLaneTrafficMath.h"
#include "UI/SOLUiProfileCapture.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Visuals/SOLThrusterMath.h"
#include "Visuals/SOLThrusterRibbons.h"

#include "Camera/PlayerCameraManager.h"
#include "DynamicRHI.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    // Phase durations (real seconds) and frame counts
    constexpr double THRSMOKE_WAIT_TIMEOUT_S = 240.0;        // Population spawn and upload (1M ships)
    constexpr double THRSMOKE_SETTLE_S = 3.0;                // Anchor, rebase and tiers settle after a ride starts
    constexpr int32 THRSMOKE_SCAN_FRAMES = 90;               // Frames scanned per window
    constexpr int32 THRSMOKE_SWITCH_FRAMES = 10;             // Frames with sol.NpcPlumes 0, then 1 again (>= 2 redraws)
    constexpr double THRSMOKE_SHOT_S = 0.8;                  // The screenshot is written before the next move
    constexpr int32 THRSMOKE_MIN_PHASE_FRAMES = 5;

    // Script values
    constexpr int32 THRSMOKE_SHIPS = 1000000;                // Population (SDD 51 section 4.12: 1M NPCs)
    constexpr double THRSMOKE_BURN_HOLD_S = 30.0;            // The burning target keeps its state this long
    constexpr double THRSMOKE_COAST_HOLD_S = 20.0;           // The coasting target likewise
    constexpr double THRSMOKE_PROFILE_HOLD_S = 120.0;        // The profile's braking target likewise
    constexpr double THRSMOKE_BURN_FRACTION = 0.02;          // Ties between equally hard burns go to the one nearest
                                                             // this fraction of the lane from Earth...
    constexpr double THRSMOKE_COAST_FRACTION = 0.5;          // ...and the coasting target is the one nearest this
    constexpr double THRSMOKE_BURN_VIEW_M = 120.0;           // Ride distance from the burning target (the screenshot)
    constexpr double THRSMOKE_PROFILE_VIEW_M = 1500.0;       // Ride distance in the profile's lane view
    constexpr double THRSMOKE_AIM_DROP = 0.35;               // The aim point this far (x distance) below the target, so
                                                             // it sits above the player hull on screen
    constexpr double THRSMOKE_MID_BAND_LOW = 1.25;           // P2: the coast ride beyond 1.25 Full radii...
    constexpr double THRSMOKE_MID_BAND_HIGH = 0.8;           // ...and within 0.8 of the hull radius and plume gate
    constexpr double THRSMOKE_STATE_STEP_S = 0.05;           // A ship whose thrust state changes within this is skipped
    constexpr double THRSMOKE_MIN_PX = 1.0;                  // P1: the rail G's plume at least this long (ref px)
    constexpr double THRSMOKE_MID_TOLERANCE = 0.02;          // P1: b against the rail G's brightness, Mid ships...
    constexpr double THRSMOKE_FULL_TOLERANCE = 0.10;         // ...and Full ships (their AI acceleration added)
    constexpr double THRSMOKE_EDGE_BAND = 0.1;               // P5: ships within 10 % of the 0.5 px cut are skipped
    constexpr float THRSMOKE_FLOAT_TOLERANCE = 1.0e-4f;      // P5: pushed c and b against the expected floats...
    constexpr float THRSMOKE_LENGTH_TOLERANCE = 1.0e-3f;     // ...and the plume length (relative)

    // The profile
    constexpr int32 THRSMOKE_PROFILE_PAIRS = 4;              // A/B pairs per view, ordered A B B A A B B A
    constexpr int32 THRSMOKE_PROFILE_SETTLE_FRAMES = 60;     // After each switch (mesh change hitch)
    constexpr int32 THRSMOKE_PROFILE_FRAMES = 240;           // The stat dumpave window
    constexpr int32 THRSMOKE_PROFILE_TAIL_FRAMES = 30;       // Frames left for the dump to print
    constexpr int32 THRSMOKE_PROFILE_GPU_FRAMES[] = { 40, 120, 200 };  // ProfileGPU frames inside the window
    constexpr int32 THRSMOKE_PROFILE_GPU_GUARD = 3;          // Frames around a ProfileGPU left out of the GPU mean
    const TCHAR* const THRSMOKE_PROFILE_ROOT = TEXT("STAT_SOL_NpcRedraw");
    constexpr float THRSMOKE_PROFILE_MAX_WINDOW_PX = 16384.0f;   // Lifted window size limit
    const TCHAR* const THRSMOKE_COMBAT_CVAR = TEXT("sol.Npc.Combat");
    // The ProfileGPU table settings the offline parser reads (as FSOLUiProfileCapture pins them)
    const TCHAR* const THRSMOKE_GPU_SETUP_CMDS[] = {
        TEXT("r.ProfileGPU.ShowUI 0"), TEXT("r.ProfileGPU.TableFormatting 1"), TEXT("r.ProfileGPU.UnicodeOutput 1"),
        TEXT("r.ProfileGPU.ShowStats 1"), TEXT("r.ProfileGPU.ShowPercentColumn 1"),
        TEXT("r.ProfileGPU.ShowInclusive 1"), TEXT("r.ProfileGPU.ShowExclusive 1"),
        TEXT("r.ProfileGPU.ThresholdPercent 0"), TEXT("r.ProfileGPU.Root *") };

    // The ribbon checks (-SOLSmokeThrusterRibbons)
    constexpr int32 THRSMOKE_RIBBON_PROFILE_PAIRS = 8;       // A/B pairs per view in the ribbon profile
    constexpr double THRSMOKE_RIBBON_SPAWN_S = 5.0;          // The spawn view settles (histories fill: 1 s each)
    constexpr double THRSMOKE_RIBBON_PLAYER_S = 2.5;         // The player thrusts while turning this long
    constexpr double THRSMOKE_RIBBON_RIDE_S = 4.0;           // A ride settles (the target's ribbon fills)
    constexpr double THRSMOKE_RIBBON_SHOT_HOLD_S = 0.6;      // Real time for a requested screenshot to be written
    constexpr double THRSMOKE_RIBBON_TURN_HOLD_S = 6.0;      // The U-turn target stays in its turn this long
    constexpr double THRSMOKE_RIBBON_TURN_VIEW_M = 400.0;    // Ride distance from the U-turn target
    constexpr double THRSMOKE_RIBBON_FAR_VIEW_M = 22000.0;   // Ride distance from the far burning target
    constexpr double THRSMOKE_RIBBON_FAR_LOG_M = 20000.0;    // Trails at least this far are logged
    constexpr double THRSMOKE_RIBBON_PLAYER_THRUST = 1.0;    // Forward input (3 G)...
    constexpr double THRSMOKE_RIBBON_PLAYER_YAW_RADPS = 0.8; // ...while yawing at this rate
    constexpr double THRSMOKE_RIBBON_SWING_YAW_PX = 500.0;   // Free-look swing onto the ship's side (as the 51d smoke)
    constexpr double THRSMOKE_RIBBON_SWING_PITCH_PX = -60.0;
    constexpr int32 THRSMOKE_RIBBON_LOG_TRAILS = 10;         // Longest pool / far trails logged per label
    constexpr float THRSMOKE_FROZEN_DILATION = 0.0001f;      // The world settings' minimum time dilation
    constexpr double THRSMOKE_POP_RELATIVE_TOL_CM = 1.0;   // Camera to the ridden ship across the pop shots
    constexpr int32 THRSMOKE_POOL_PX = 1000000000;           // sol.ThrusterRibbonNiagaraPx: every NPC ribbon to the pool
    constexpr int32 THRSMOKE_PRESET_PX = -1;                 // ...the preset's threshold
    constexpr int32 THRSMOKE_POP_SETTLE_FRAMES = 30;         // Frames after a switch (the pool's 3 upload buffers)...
    constexpr int32 THRSMOKE_POP_TSR_SETTLE_FRAMES = 90;     // ...and after a switch back to temporal AA
    const TCHAR* const THRSMOKE_AA_CVAR = TEXT("r.AntiAliasingMethod");

    // One shot of the no-pop sequence: anti-aliasing off (0) or the default (-1), the Niagara threshold, the ribbons
    struct FThrusterPopShot
    {
        const TCHAR* Name;
        int32 AntiAliasing;
        int32 NiagaraPx;
        int32 Ribbons;
    };
    // N = the preset's split, P = every NPC ribbon in the pool, twice each (the same-renderer baseline); last the ribbons
    // off (the mask; switching them off drops the histories, so it comes last)
    const FThrusterPopShot THRSMOKE_POP_SHOTS[] = {
        { TEXT("pop_aaoff_N1"), 0, THRSMOKE_PRESET_PX, 1 }, { TEXT("pop_aaoff_N2"), 0, THRSMOKE_PRESET_PX, 1 },
        { TEXT("pop_aaoff_P1"), 0, THRSMOKE_POOL_PX, 1 }, { TEXT("pop_aaoff_P2"), 0, THRSMOKE_POOL_PX, 1 },
        { TEXT("pop_tsr_N1"), -1, THRSMOKE_PRESET_PX, 1 }, { TEXT("pop_tsr_N2"), -1, THRSMOKE_PRESET_PX, 1 },
        { TEXT("pop_tsr_P1"), -1, THRSMOKE_POOL_PX, 1 }, { TEXT("pop_tsr_P2"), -1, THRSMOKE_POOL_PX, 1 },
        { TEXT("pop_aaoff_R0"), 0, THRSMOKE_PRESET_PX, 0 } };

    //////////////////////////////////////////////////////////////////////////
    // Sets an integer console variable, logging a name that does not exist
    void ThrusterSmokeSetCvar(const TCHAR* name, const int32 value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
            return;
        }
        UE_LOG(LogSOL, Warning, TEXT("SmokeThruster: console variable %s not found"), name);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs a console command in the world, logged
    void ThrusterSmokeRunCommand(const TCHAR* command, UWorld* world)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster: console command '%s'"), command);
        FSOLUiProfileCapture::RunCommand(command, world);
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets the -ResX / -ResY view past the desktop size with r.SetRes (as the tunnel profile does); nothing without
    // both
    void ThrusterSmokeApplyCommandLineView(UWorld* world)
    {
        int32 width = 0;
        int32 height = 0;
        const TSharedPtr<SWindow> window = GEngine != nullptr && GEngine->GameViewport != nullptr
            ? GEngine->GameViewport->GetWindow() : nullptr;
        if (!FParse::Value(FCommandLine::Get(), TEXT("ResX="), width) || !FParse::Value(FCommandLine::Get(),
            TEXT("ResY="), height) || width <= 0 || height <= 0 || !window.IsValid())
        {
            return;
        }
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(THRSMOKE_PROFILE_MAX_WINDOW_PX)
            .SetMaxHeight(THRSMOKE_PROFILE_MAX_WINDOW_PX));
        GEngine->Exec(world, *FString::Printf(TEXT("r.SetRes %dx%dw"), width, height));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a lane thrust is a burn (Accelerate or Brake)
    bool ThrusterSmokeIsBurn(const ESOLLaneThrust thrust)
    {
        return thrust == ESOLLaneThrust::Accelerate || thrust == ESOLLaneThrust::Brake;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a lane segment is a U-turn
    bool ThrusterSmokeIsTurn(const ESOLLaneSegment segment)
    {
        return segment == ESOLLaneSegment::TurnAtA || segment == ESOLLaneSegment::TurnAtB;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a rail is a lane rail with a valid lane frame and profile in this rail frame
    bool ThrusterSmokeHasLane(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame)
    {
        return rail.Kind == static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane)
            && frame.LaneFrame.IsValidIndex(rail.Group) && frame.LaneFrame[rail.Group].bValid
            && frame.LaneProfile.IsValidIndex(rail.Group);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane rail's thrust state at a rail time
    FSOLLaneThrustState ThrusterSmokeThrust(const FSOLNpcRail& rail, const FSOLNpcRailFrame& frame,
        const int32 npcIndex, const double railTimeS)
    {
        return SOLLaneTraffic::LaneThrustAt(frame.LaneProfile[rail.Group], frame.LaneFrame[rail.Group],
            SOLNpcRail::LaneShipFromRail(rail), static_cast<uint32>(npcIndex), railTimeS);
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
void FSOLThrusterSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const ASOLGameMode* gameMode = world->GetAuthGameMode<ASOLGameMode>();
    mVisuals = gameMode != nullptr ? gameMode->GetNpcVisuals() : nullptr;
    mOrbitLines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
    mNpcs = world->GetSubsystem<USOLNpcSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();

    // The cost captures need a standalone -game process (in the editor ProfileGPU opens its visualizer)
    mIsProfile = FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_THRUSTER_PROFILE) && !GIsEditor;
    mIsRibbons = FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_THRUSTER_RIBBONS);
    mRunNiagaraPx = THRSMOKE_PRESET_PX;
    FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_RIBBON_NIAGARA_PX, mRunNiagaraPx);
    mRibbons = gameMode != nullptr ? gameMode->GetThrusterRibbons() : nullptr;
    UE_LOG(LogSOL, Log, TEXT("SmokeThruster: starting (NPC visuals %s, profile %s, ribbons %s)"),
        mVisuals.IsValid() ? *mVisuals->GetName() : TEXT("none"), mIsProfile ? TEXT("on") : TEXT("off"),
        mIsRibbons ? (mRibbons.IsValid() ? TEXT("on") : TEXT("on, actor missing")) : TEXT("off"));

    // Overrides in memory only: lanes, tunnels and orbit lines detached from the player's settings and hidden, no
    // space dust, no NPC combat, the plumes on
    if (mVisuals.IsValid())
    {
        mVisuals->SetLaneVerificationOverride(false);
        mVisuals->SetTunnelVerificationOverride(false, false);
    }
    if (mOrbitLines.IsValid())
    {
        mOrbitLines->BeginVerificationOverride();
        mOrbitLines->SetLinesEnabled(false);
    }
    ThrusterSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 0);
    if (!mIsProfile && !mIsRibbons)
    {
        ThrusterSmokeSetCvar(THRSMOKE_COMBAT_CVAR, 0);      // The profile and the ribbons keep the default (combat on)
    }
    ThrusterSmokeSetCvar(SOL::NPC_PLUME_CVAR, 1);
    ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBONS_CVAR, 1);
    ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBON_NIAGARA_PX_CVAR, mRunNiagaraPx);
    if (mNpcs.IsValid() && mNpcs->GetRequestedCount() == 0)
    {
        mNpcs->RequestNpcCount(THRSMOKE_SHIPS);
    }
    mPhase = ESOLThrusterPhase::WaitPopulation;
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLThrusterSmoke::Update(ASOLShipPawn& pawn, const float deltaSeconds)
{
    // The ribbon checks freeze the game (the pawn's delta is dilated), so they time by the engine's real delta
    const float realDeltaSeconds = mIsRibbons ? static_cast<float>(FApp::GetDeltaTime()) : deltaSeconds;
    if (mPhase == ESOLThrusterPhase::Done)
    {
        return true;
    }
    if (!mVisuals.IsValid() || !mNpcs.IsValid() || !mShips.IsValid() || !mRegistry.IsValid() || !mAnchor.IsValid()
        || (mIsRibbons && !mRibbons.IsValid()))
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeThruster: NPC visuals or subsystems missing; aborting"));
        mPhase = ESOLThrusterPhase::Done;
        Finish(pawn);
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;

    // The ride-along is pinned every frame once it started; the scans run every frame of their phase
    if (mTarget != INDEX_NONE)
    {
        PinRide();
    }
    if (mPhase == ESOLThrusterPhase::SpawnScan || mPhase == ESOLThrusterPhase::BurnScan
        || mPhase == ESOLThrusterPhase::CoastScan)
    {
        ScanFrame();
    }

    // The player's held inputs (thrust forward while yawing) every frame of its phase, the screenshot hold included
    if (mPhase == ESOLThrusterPhase::RibbonPlayer)
    {
        if (mPhaseFrames == 1)
        {
            // Manual flight (no assist fighting the yaw) and the free-look swing onto the ship's side
            if (mShips->GetFlightMode() != ESOLFlightMode::Manual)
            {
                pawn.HandleToggleAssist();
            }
            pawn.HandleFreeLook(true);
            pawn.HandleMouseDelta(FVector2d(THRSMOKE_RIBBON_SWING_YAW_PX, THRSMOKE_RIBBON_SWING_PITCH_PX));
        }
        pawn.HandleThrust(FVector3d(THRSMOKE_RIBBON_PLAYER_THRUST, 0.0, 0.0));
        FSOLShipState state = mShips->GetState();
        state.AngularVelocityRadPerS = FVector3d(0.0, 0.0, THRSMOKE_RIBBON_PLAYER_YAW_RADPS);
        mShips->SetState(state);
    }
    else if (mPhase == ESOLThrusterPhase::RibbonTurnSettle && mPhaseFrames == 1)
    {
        pawn.HandleThrust(FVector3d::ZeroVector);           // The rides pin the ship; the camera back behind it
        pawn.HandleFreeLook(false);
    }

    // A requested screenshot holds the phase until it is written
    if (mShotHoldS > 0.0)
    {
        mShotHoldS -= realDeltaSeconds;
        if (mShotHoldS > 0.0)
        {
            return false;
        }
    }

    // Phase end: the population once ready; the profiles once every window is done; the rest by time and frames
    if (mPhase == ESOLThrusterPhase::WaitPopulation)
    {
        const int32 wanted = FMath::Max(mNpcs->GetRequestedCount(), 1);
        const bool bReady = mNpcs->GetRails().Num() >= wanted && mNpcs->GetGroups().LaneCount > 0
            && mPhaseTimeS > THRSMOKE_SETTLE_S;
        if (!bReady && mPhaseTimeS < THRSMOKE_WAIT_TIMEOUT_S)
        {
            return false;
        }
    }
    else if (mPhase == ESOLThrusterPhase::ProfileSpawn || mPhase == ESOLThrusterPhase::ProfileLane)
    {
        if (!TickProfile(mPhase == ESOLThrusterPhase::ProfileSpawn ? TEXT("spawn") : TEXT("lane"), pawn.GetWorld(),
            realDeltaSeconds))
        {
            return false;
        }
    }
    else if (mPhase == ESOLThrusterPhase::RibbonPop)
    {
        if (!TickPop(pawn.GetWorld()))
        {
            return false;
        }
    }
    else if (mPhaseTimeS < GetPhaseDuration(mPhase) || mPhaseFrames < FMath::Max(GetPhaseFrames(mPhase),
        THRSMOKE_MIN_PHASE_FRAMES))
    {
        return false;
    }

    // A ribbon shot phase ends with its screenshot, once written
    if (IsRibbonShotPhase(mPhase) && !mIsShotTaken)
    {
        const bool bFar = mPhase == ESOLThrusterPhase::RibbonFarSettle;
        const TCHAR* name = mPhase == ESOLThrusterPhase::RibbonSpawn ? TEXT("ribbon_spawn")
            : mPhase == ESOLThrusterPhase::RibbonPlayer ? TEXT("ribbon_player")
            : mPhase == ESOLThrusterPhase::RibbonTurnSettle ? TEXT("ribbon_turn") : TEXT("ribbon_far");
        LogRibbons(name, THRSMOKE_RIBBON_FAR_LOG_M);
        TakeShot(name, !bFar);
        mIsShotTaken = true;
        mShotHoldS = THRSMOKE_RIBBON_SHOT_HOLD_S;
        return false;
    }

    ExitPhase(mPhase);
    mPhase = NextPhase(mPhase);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    mIsShotTaken = false;
    if (mPhase == ESOLThrusterPhase::Done)
    {
        Finish(pawn);
        return true;
    }
    EnterPhase(mPhase);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the phase after this one (the profile flag skips the checks; no lane ends the script)
FSOLThrusterSmoke::ESOLThrusterPhase FSOLThrusterSmoke::NextPhase(const ESOLThrusterPhase phase) const
{
    if (phase == ESOLThrusterPhase::WaitPopulation)
    {
        if (mIsProfile)
        {
            return ESOLThrusterPhase::ProfileSpawn;
        }
        return mIsRibbons ? ESOLThrusterPhase::RibbonSpawn : ESOLThrusterPhase::SpawnScan;
    }
    if (phase == ESOLThrusterPhase::RibbonPlayer && mLane == INDEX_NONE)
    {
        return ESOLThrusterPhase::Done;
    }
    if (phase == ESOLThrusterPhase::SpawnScan && mLane == INDEX_NONE)
    {
        return ESOLThrusterPhase::Done;
    }
    if (phase == ESOLThrusterPhase::CoastScan || phase == ESOLThrusterPhase::ProfileLane)
    {
        return ESOLThrusterPhase::Done;
    }
    if (phase == ESOLThrusterPhase::ProfileSpawn && mLane == INDEX_NONE)
    {
        return ESOLThrusterPhase::Done;
    }
    return static_cast<ESOLThrusterPhase>(static_cast<uint8>(phase) + 1);
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase in seconds
double FSOLThrusterSmoke::GetPhaseDuration(const ESOLThrusterPhase phase)
{
    switch (phase)
    {
    case ESOLThrusterPhase::BurnSettle:
    case ESOLThrusterPhase::CoastSettle:
    case ESOLThrusterPhase::ProfileLaneSettle:
        return THRSMOKE_SETTLE_S;
    case ESOLThrusterPhase::Shot:
        return THRSMOKE_SHOT_S;
    case ESOLThrusterPhase::RibbonSpawn:
        return THRSMOKE_RIBBON_SPAWN_S;
    case ESOLThrusterPhase::RibbonPlayer:
        return THRSMOKE_RIBBON_PLAYER_S;
    case ESOLThrusterPhase::RibbonTurnSettle:
    case ESOLThrusterPhase::RibbonFarSettle:
        return THRSMOKE_RIBBON_RIDE_S;
    default:
        return 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether a phase ends with a ribbon screenshot
bool FSOLThrusterSmoke::IsRibbonShotPhase(const ESOLThrusterPhase phase)
{
    return phase == ESOLThrusterPhase::RibbonSpawn || phase == ESOLThrusterPhase::RibbonPlayer
        || phase == ESOLThrusterPhase::RibbonTurnSettle || phase == ESOLThrusterPhase::RibbonFarSettle;
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum frame count of a phase
int32 FSOLThrusterSmoke::GetPhaseFrames(const ESOLThrusterPhase phase)
{
    switch (phase)
    {
    case ESOLThrusterPhase::SpawnScan:
    case ESOLThrusterPhase::BurnScan:
    case ESOLThrusterPhase::CoastScan:
        return THRSMOKE_SCAN_FRAMES;
    case ESOLThrusterPhase::PlumesOff:
    case ESOLThrusterPhase::PlumesOn:
        return THRSMOKE_SWITCH_FRAMES;
    default:
        return 0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the actions of a phase's start
void FSOLThrusterSmoke::EnterPhase(const ESOLThrusterPhase phase)
{
    switch (phase)
    {
    case ESOLThrusterPhase::BurnSettle:
    {
        const int32 target = PickTarget(true, false, THRSMOKE_BURN_HOLD_S, THRSMOKE_BURN_FRACTION);
        if (target == INDEX_NONE)
        {
            Check(TEXT("burn-target"), false, TEXT("no Earth-Mars lane ship holds Accelerate or Brake"));
            break;
        }
        StartRide(target, THRSMOKE_BURN_VIEW_M);
        break;
    }
    case ESOLThrusterPhase::PlumesOff:
        ThrusterSmokeSetCvar(SOL::NPC_PLUME_CVAR, 0);
        break;
    case ESOLThrusterPhase::PlumesOn:
        ThrusterSmokeSetCvar(SOL::NPC_PLUME_CVAR, 1);
        break;
    case ESOLThrusterPhase::Shot:
        TakeShot(TEXT("lane"));
        break;
    case ESOLThrusterPhase::CoastSettle:
    {
        // A Mid-tier hull distance: beyond the Full radius, inside the hull radius and the glow's plume-float gate
        const FSOLNpcStats& stats = mNpcs->GetStats();
        const double gateM = SOLThruster::PlumeMaxDistanceM(SOL::Thruster::GLOW_DIAMETER_NOZZLE_RADII
            * SOL::Thruster::NPC_NOZZLE_RADIUS_M, mVisuals->GetPlumeFocalRefPx(), SOL::Thruster::GLOW_FADE_LOW_PX);
        const double lowM = THRSMOKE_MID_BAND_LOW * stats.FullRadiusM;
        const double highM = THRSMOKE_MID_BAND_HIGH * FMath::Min(stats.InstancedRadiusM, gateM);
        mIsMidBandEmpty = !(lowM < highM);
        const double distanceM = mIsMidBandEmpty ? 0.5 * FMath::Min(stats.InstancedRadiusM, gateM)
            : 0.5 * (lowM + highM);
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster: coast ride at %.0f m (Full radius %.0f m, hull radius %.0f m, plume "
            "gate %.0f m%s)"), distanceM, stats.FullRadiusM, stats.InstancedRadiusM, gateM,
            mIsMidBandEmpty ? TEXT(", no Mid band") : TEXT(""));
        const int32 target = PickTarget(false, false, THRSMOKE_COAST_HOLD_S, THRSMOKE_COAST_FRACTION);
        if (target == INDEX_NONE)
        {
            Check(TEXT("coast-target"), false, TEXT("no Earth-Mars lane ship holds Coast or Flip"));
            break;
        }
        StartRide(target, distanceM);
        break;
    }
    case ESOLThrusterPhase::ProfileSpawn:
    case ESOLThrusterPhase::ProfileLane:
        mProfileWindow = 0;
        mProfileFrame = 0;
        break;
    case ESOLThrusterPhase::RibbonPop:
    {
        const IConsoleVariable* aa = IConsoleManager::Get().FindConsoleVariable(THRSMOKE_AA_CVAR);
        mAntiAliasingDefault = aa != nullptr ? aa->GetInt() : INDEX_NONE;
        mPopShot = 0;
        mPopFrame = 0;
        ThrusterSmokeSetCvar(THRSMOKE_COMBAT_CVAR, 0);      // No bolts moving between the frozen shots
        SetFrozen(mVisuals->GetWorld(), true);
        break;
    }
    case ESOLThrusterPhase::RibbonTurnSettle:
    {
        const int32 target = PickTurnTarget(THRSMOKE_RIBBON_TURN_HOLD_S);
        if (target != INDEX_NONE)
        {
            StartRide(target, THRSMOKE_RIBBON_TURN_VIEW_M);
        }
        else
        {
            UE_LOG(LogSOL, Warning, TEXT("SmokeThruster RIBBON: no Earth-Mars lane ship in a U-turn; not ridden"));
        }
        break;
    }
    case ESOLThrusterPhase::RibbonFarSettle:
    {
        const int32 target = PickTarget(true, false, THRSMOKE_BURN_HOLD_S, THRSMOKE_BURN_FRACTION);
        if (target != INDEX_NONE)
        {
            StartRide(target, THRSMOKE_RIBBON_FAR_VIEW_M);
        }
        else
        {
            Check(TEXT("far-target"), false, TEXT("no Earth-Mars lane ship holds Accelerate or Brake"));
        }
        break;
    }
    case ESOLThrusterPhase::ProfileLaneSettle:
    {
        const int32 target = PickTarget(true, true, THRSMOKE_PROFILE_HOLD_S, THRSMOKE_BURN_FRACTION);
        if (target != INDEX_NONE)
        {
            StartRide(target, THRSMOKE_PROFILE_VIEW_M);
        }
        else
        {
            UE_LOG(LogSOL, Warning, TEXT("SmokeThruster PROFILE: no braking lane ship found; lane view not ridden"));
        }
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's end checks
void FSOLThrusterSmoke::ExitPhase(const ESOLThrusterPhase phase)
{
    switch (phase)
    {
    case ESOLThrusterPhase::WaitPopulation:
        if (!BuildLane())
        {
            Check(TEXT("population"), false, FString::Printf(TEXT("%d rails after %.0f s, Earth-Mars lane not found"),
                mNpcs->GetRails().Num(), mPhaseTimeS));
        }
        if (mIsProfile)
        {
            UWorld* world = mVisuals->GetWorld();
            ThrusterSmokeRunCommand(TEXT("r.VSync 0"), world);
            ThrusterSmokeApplyCommandLineView(world);
            for (const TCHAR* command : THRSMOKE_GPU_SETUP_CMDS)
            {
                ThrusterSmokeRunCommand(command, world);
            }
        }
        break;
    case ESOLThrusterPhase::PlumesOff:
    {
        // P4, first half: what the burning ship's component holds with the plain meshes
        FSOLPlumeInstance plume;
        const int32 cpu = FindCpuShip(mTarget);
        mOffReadable = cpu != INDEX_NONE && mVisuals->GetPushedPlume(cpu, plume);
        mOffBrightness = mOffReadable ? plume.Brightness : 0.0f;
        break;
    }
    case ESOLThrusterPhase::RibbonPop:
    {
        SetFrozen(mVisuals->GetWorld(), false);
        ThrusterSmokeSetCvar(THRSMOKE_COMBAT_CVAR, 1);
        ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBONS_CVAR, 1);
        ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBON_NIAGARA_PX_CVAR, mRunNiagaraPx);
        if (mAntiAliasingDefault != INDEX_NONE)
        {
            ThrusterSmokeSetCvar(THRSMOKE_AA_CVAR, mAntiAliasingDefault);
        }
        Check(TEXT("pop-not-vacuous"), !mIsPopVacuous, FString::Printf(TEXT("%d NPC trails drawn by Niagara, %d by the "
            "pool and %d live in the first shot"), mPopNiagaraNpc, mPopPool, mPopLive));
        Check(TEXT("pop-frozen"), mIsPopStable,
            TEXT("the ridden ship drawn and the camera fixed relative to it in every shot"));
        Check(TEXT("pop-switched"), mIsPopSwitched, TEXT("in every pool shot no NPC trail in Niagara and the pool "
            "holding the first shot's Niagara and pool trails"));
        break;
    }
    case ESOLThrusterPhase::PlumesOn:
    {
        FSOLPlumeInstance plume;
        const int32 cpu = FindCpuShip(mTarget);
        mBurnTarget = mTarget;
        mHasBurnTarget = cpu != INDEX_NONE && mVisuals->GetPushedPlume(cpu, plume);
        mOnBrightness = mHasBurnTarget ? plume.Brightness : 0.0f;
        mOnLengthM = mHasBurnTarget ? plume.LengthM : 0.0f;
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Logs the verdicts and the SUMMARY line, restores the cvars and takes the closing screenshot (which quits)
void FSOLThrusterSmoke::Finish(ASOLShipPawn& pawn)
{
    if (!mIsProfile && mScan.Frames > 0)
    {
        const FScan& s = mScan;
        const int32 burnSeen = s.BurnSeen[0] + s.BurnSeen[1];
        const int32 burnLit = s.BurnLit[0] + s.BurnLit[1];
        Check(TEXT("P1-burn-lit"), burnSeen > 0 && burnLit == burnSeen && s.BurnClose[0] + s.BurnClose[1] > 0
            && s.BurnClose[1] == s.BurnSeen[1], FString::Printf(TEXT("%d burning ship-frames (%d ships) checked over "
            "%d frames: Full %d (%d lit, %d within %.2f, worst b error %.4f), Mid %d (%d lit, %d within %.2f, worst b "
            "error %.4f); skipped %d in U-turns, %d at a state change, %d under %.0f px, %d unreadable"), burnSeen,
            s.BurnShips.Num(), s.Frames, s.BurnSeen[0], s.BurnLit[0], s.BurnClose[0], THRSMOKE_FULL_TOLERANCE,
            s.BurnWorstErr[0], s.BurnSeen[1], s.BurnLit[1], s.BurnClose[1], THRSMOKE_MID_TOLERANCE, s.BurnWorstErr[1],
            s.Turns, s.Transitions, s.Small, THRSMOKE_MIN_PX, s.Unreadable));
        const int32 coastSeen = s.CoastSeen[0] + s.CoastSeen[1];
        const int32 coastDark = coastSeen - s.CoastLit[0] - s.CoastLit[1];
        Check(TEXT("P2-coast-dark"), coastDark > 0 && s.CoastLit[1] == 0, FString::Printf(TEXT("%d coasting / "
            "flipping ship-frames (%d ships), %d dark: Full %d (%d with b > 0, max b %.4f), Mid %d (%d with b > 0, max "
            "b %.4f)%s"), coastSeen, s.CoastShips.Num(), coastDark, s.CoastSeen[0], s.CoastLit[0], s.CoastMaxB[0],
            s.CoastSeen[1], s.CoastLit[1], s.CoastMaxB[1], s.CoastSeen[1] == 0 ? (mIsMidBandEmpty
            ? TEXT("; Mid check vacuous: no Mid-tier hull distance") : TEXT("; Mid check vacuous: no Mid ship seen"))
            : TEXT("")));
        Check(TEXT("P3-dots-no-plume"), s.Dots > 0 && s.DotPlumes == 0, FString::Printf(TEXT("%d dot-drawn ship-frames,"
            " %d with pushed floats or plume thrust; %d hull-drawn ship-frames"), s.Dots, s.DotPlumes, s.Hulls));
        Check(TEXT("P5-pushed-floats"), s.FloatChecked[1] > 0 && s.FloatLit[1] > 0 && s.FloatMismatch[0] == 0
            && s.FloatMismatch[1] == 0, FString::Printf(TEXT("hull ship-frames against PlumeInstance of their own "
            "thrust: pool %d (%d lit, %d mismatched), instanced %d (%d lit, %d mismatched); worst b error %.6f, worst "
            "length error %.4f m; %d near the 0.5 px cut skipped"), s.FloatChecked[0], s.FloatLit[0],
            s.FloatMismatch[0], s.FloatChecked[1], s.FloatLit[1], s.FloatMismatch[1], s.FloatWorstB,
            s.FloatWorstLengthM, s.FloatEdge));
        Check(TEXT("P4-mesh-switch"), mHasBurnTarget && mOnBrightness > 0.0f && mOnLengthM > 0.0f,
            FString::Printf(TEXT("burning ship %d: sol.NpcPlumes 0 floats %s (b %.4f); back on after %d frames b %.4f, "
            "L %.2f m"), mBurnTarget, mOffReadable ? TEXT("readable") : TEXT("not readable"), mOffBrightness,
            THRSMOKE_SWITCH_FRAMES, mOnBrightness, mOnLengthM));
    }
    ThrusterSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 1);
    ThrusterSmokeSetCvar(THRSMOKE_COMBAT_CVAR, 1);
    ThrusterSmokeSetCvar(SOL::NPC_PLUME_CVAR, 1);
    ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBONS_CVAR, 1);
    ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBON_NIAGARA_PX_CVAR, THRSMOKE_PRESET_PX);
    if (mIsRibbons && mRibbons.IsValid())
    {
        SetFrozen(pawn.GetWorld(), false);
    }
    if (mIsProfile)
    {
        ThrusterSmokeRunCommand(TEXT("r.ProfileGPU.ShowUI 1"), pawn.GetWorld());
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeThruster SUMMARY: complete after %.1f s, %d/%d checks run passed%s"), mTotalTimeS,
        mChecksPassed, mChecksRun, mChecksPassed == mChecksRun && (mChecksRun > 0 || mIsProfile) ? TEXT("")
        : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Finds the Earth-Mars lane and its rail indices
bool FSOLThrusterSmoke::BuildLane()
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
    const int32 mars = registry.FindByName(FName(SOL::BodyNames::MARS));
    mLane = INDEX_NONE;
    for (int32 lane = 0; lane < groups.LaneCount; ++lane)
    {
        if (groups.LaneBodyA.IsValidIndex(lane) && groups.LaneBodyB.IsValidIndex(lane)
            && groups.LaneBodyA[lane] == earth && groups.LaneBodyB[lane] == mars)
        {
            mLane = lane;
        }
    }
    mLaneShips.Reset();
    const uint8 laneKind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
    for (int32 index = 0; index < rails.Num(); ++index)
    {
        if (rails[index].Kind == laneKind && rails[index].Group == mLane)
        {
            mLaneShips.Add(index);
        }
    }
    const FSOLNpcStats& stats = mNpcs->GetStats();
    UE_LOG(LogSOL, Log, TEXT("SmokeThruster: %d rails, Earth-Mars lane %d with %d ships; Full %.0f m, Mid %.0f m, mesh "
        "%.0f m, hull %.0f m radii"), rails.Num(), mLane, mLaneShips.Num(), stats.FullRadiusM, stats.MidRadiusM,
        stats.MeshRadiusM, stats.InstancedRadiusM);
    if (mLane == INDEX_NONE || mLaneShips.IsEmpty() || !mNpcs->GetRailFrame().LaneFrame.IsValidIndex(mLane)
        || !mNpcs->GetRailFrame().LaneFrame[mLane].bValid)
    {
        mLane = INDEX_NONE;
        return false;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Picks an Earth-Mars lane ship holding a wanted thrust state now and holdS later, off the U-turns: of the burns the
// hardest, else (and on ties) the one nearest to a fraction of the lane from Earth
int32 FSOLThrusterSmoke::PickTarget(const bool bBurn, const bool bBrakeOnly, const double holdS,
    const double laneFraction) const
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    if (mLane == INDEX_NONE || !frame.LaneFrame.IsValidIndex(mLane))
    {
        return INDEX_NONE;
    }
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[mLane];
    const double lengthM = (laneFrame.BM - laneFrame.AM).Size();
    const double timeS = frame.RailTimeS;

    // Returns whether a thrust state is one of the wanted ones
    const auto wanted = [bBurn, bBrakeOnly](const FSOLLaneThrustState& state)
    {
        if (ThrusterSmokeIsTurn(state.Segment))
        {
            return false;
        }
        if (bBurn)
        {
            return bBrakeOnly ? state.Thrust == ESOLLaneThrust::Brake : ThrusterSmokeIsBurn(state.Thrust);
        }
        return state.Thrust == ESOLLaneThrust::Coast || state.Thrust == ESOLLaneThrust::Flip;
    };
    int32 best = INDEX_NONE;
    double bestOffset = TNumericLimits<double>::Max();
    double bestAccelMps2 = 0.0;
    for (const int32 index : mLaneShips)
    {
        const FSOLNpcRail& rail = rails[index];
        const FSOLLaneThrustState now = ThrusterSmokeThrust(rail, frame, index, timeS);
        const FSOLLaneThrustState later = ThrusterSmokeThrust(rail, frame, index, timeS + holdS);
        if (!wanted(now) || !wanted(later) || now.Thrust != later.Thrust || now.Segment != later.Segment)
        {
            continue;
        }
        // A burn: the hardest one (the clearest plume), ties by the fraction; else the nearest to the fraction
        const double fraction = lengthM > 0.0 ? ((SOLNpcRail::EvaluateM(rail, frame) - laneFrame.AM) | laneFrame.Axis)
            / lengthM : 0.0;
        const double offset = FMath::Abs(fraction - laneFraction);
        const double accelMps2 = bBurn ? now.AccelMps2 : 0.0;
        if (accelMps2 > bestAccelMps2 || (accelMps2 == bestAccelMps2 && offset < bestOffset))
        {
            bestAccelMps2 = accelMps2;
            bestOffset = offset;
            best = index;
        }
    }
    if (best != INDEX_NONE)
    {
        const FSOLLaneThrustState state = ThrusterSmokeThrust(rails[best], frame, best, timeS);
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster: target ship %d, thrust %d at %.3f G, %.4f of the lane from its "
            "fraction %.2f"), best, static_cast<int32>(state.Thrust), state.AccelMps2 / SOL::STANDARD_GRAVITY_MPS2,
            bestOffset, laneFraction);
    }
    return best;
}

//////////////////////////////////////////////////////////////////////////
// Starts riding along with a lane ship at a distance on the lane's up side
void FSOLThrusterSmoke::StartRide(const int32 npcIndex, const double distanceM)
{
    mTarget = npcIndex;
    mRideDistanceM = distanceM;
    mRideOffsetDir = mNpcs->GetRailFrame().LaneFrame[mLane].Up.GetSafeNormal();
    mRideUpDir = mNpcs->GetRailFrame().LaneFrame[mLane].Right.GetSafeNormal();
    mHasTargetPrev = false;
    PinRide();
}

//////////////////////////////////////////////////////////////////////////
// Pins the ship at the ride's offset from the target, at the target's velocity (the last frame's step; the rail
// velocity plus the reference velocity at the start), facing it with the target above the player hull
void FSOLThrusterSmoke::PinRide()
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    if (!rails.IsValidIndex(mTarget))
    {
        return;
    }
    const int32 cpu = FindCpuShip(mTarget);
    const FVector3d targetM = cpu != INDEX_NONE ? mNpcs->GetCpuShips()[cpu].PositionM
        : SOLNpcRail::EvaluateM(rails[mTarget], frame);
    FVector3d velocityUnrealMps = mShips->GetReferenceVelocityMps()
        + SOLRender::EclipticToUnreal(SOLNpcRail::VelocityMps(rails[mTarget], frame));
    if (mHasTargetPrev && frame.RailTimeS > mTargetPrevRailS)
    {
        velocityUnrealMps = SOLRender::EclipticToUnreal((targetM - mTargetPrevM) / (frame.RailTimeS
            - mTargetPrevRailS));
    }
    if (!mHasTargetPrev || frame.RailTimeS > mTargetPrevRailS)
    {
        mTargetPrevM = targetM;
        mTargetPrevRailS = frame.RailTimeS;
        mHasTargetPrev = true;
    }

    // Facing the target, the aim dropped below it so it shows above the player hull
    const FVector3d shipM = targetM + mRideOffsetDir * mRideDistanceM;
    // (the screen's up is the lane's right, so a leg ship's plume, along the lane axis, lies across the view)
    const FVector3d toTarget = SOLRender::EclipticToUnreal(targetM - shipM);
    const FVector3d cameraUp = SOLRender::EclipticToUnreal(mRideUpDir);
    const FVector3d aim = toTarget - cameraUp * (THRSMOKE_AIM_DROP * mRideDistanceM);
    FSOLShipState state = mShips->GetState();
    state.PositionM = SOLRender::EclipticToUnreal(shipM);
    state.VelocityMps = velocityUnrealMps;
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromXZ(aim, cameraUp).ToQuat();
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Returns the CPU ship index of an NPC this frame, or INDEX_NONE
int32 FSOLThrusterSmoke::FindCpuShip(const int32 npcIndex) const
{
    const TConstArrayView<FSOLNpcCpuShip> ships = mNpcs->GetCpuShips();
    for (int32 index = 0; index < ships.Num(); ++index)
    {
        if (ships[index].NpcIndex == npcIndex)
        {
            return index;
        }
    }
    return INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Scans every CPU ship this frame: dot-drawn ships carry no plume (P3); hull-drawn lane ships inside the plume gate,
// off the U-turns and away from a state change, against their rail thrust (P1 burning, P2 coasting or flipping)
void FSOLThrusterSmoke::ScanFrame()
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const TConstArrayView<FSOLNpcCpuShip> ships = mNpcs->GetCpuShips();
    const FVector3d viewpointM = mAnchor->GetViewpointM();
    const double focalPx = mVisuals->GetPlumeFocalRefPx();
    const double gateM = SOLThruster::PlumeMaxDistanceM(SOL::Thruster::GLOW_DIAMETER_NOZZLE_RADII
        * SOL::Thruster::NPC_NOZZLE_RADIUS_M, focalPx, SOL::Thruster::GLOW_FADE_LOW_PX);
    const double timeS = frame.RailTimeS;
    FScan& s = mScan;
    ++s.Frames;
    for (int32 index = 0; index < ships.Num(); ++index)
    {
        const FSOLNpcCpuShip& ship = ships[index];
        if (ship.bHidden != 0)
        {
            continue;
        }
        FSOLPlumeInstance plume;
        if (ship.RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Point))
        {
            ++s.Dots;
            s.DotPlumes += mVisuals->GetPushedPlume(index, plume) || !ship.ThrustLocalG.IsZero() ? 1 : 0;
            continue;
        }
        ++s.Hulls;

        // P5, every hull-drawn ship (battles included): the pushed floats are PlumeInstance of its own plume thrust,
        // away from the 0.5 px cut (the renderer judges it at the placed distance)
        const double shipDistanceM = (ship.PositionM - viewpointM).Size();
        const double edgePx = SOLThruster::PixelLength(SOL::Thruster::GLOW_DIAMETER_NOZZLE_RADII
            * SOL::Thruster::NPC_NOZZLE_RADIUS_M, shipDistanceM, focalPx);
        if (FMath::Abs(edgePx - SOL::Thruster::GLOW_FADE_LOW_PX) < THRSMOKE_EDGE_BAND * SOL::Thruster::GLOW_FADE_LOW_PX)
        {
            ++s.FloatEdge;
        }
        else if (mVisuals->GetPushedPlume(index, plume))
        {
            const FSOLPlumeInstance expected = SOLThruster::PlumeInstance(ship.ThrustLocalG.X, false,
                SOL::Thruster::NPC_HULL_LENGTH_M, SOL::Thruster::NPC_NOZZLE_RADIUS_M, shipDistanceM, focalPx);
            const float lengthError = FMath::Abs(plume.LengthM - expected.LengthM);
            const bool bMatch = FMath::Abs(plume.Brightness - expected.Brightness) <= THRSMOKE_FLOAT_TOLERANCE
                && FMath::Abs(plume.Colour - expected.Colour) <= THRSMOKE_FLOAT_TOLERANCE
                && lengthError <= THRSMOKE_LENGTH_TOLERANCE * FMath::Max(expected.LengthM, 1.0f);
            const int32 layer = ship.RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Mesh) ? 0 : 1;
            ++s.FloatChecked[layer];
            s.FloatLit[layer] += expected.Brightness > 0.0f ? 1 : 0;
            s.FloatMismatch[layer] += bMatch ? 0 : 1;
            s.FloatWorstB = FMath::Max(s.FloatWorstB, FMath::Abs(plume.Brightness - expected.Brightness));
            s.FloatWorstLengthM = FMath::Max(s.FloatWorstLengthM, lengthError);
        }
        else
        {
            ++s.Unreadable;
        }
        if (!rails.IsValidIndex(ship.NpcIndex) || !ThrusterSmokeHasLane(rails[ship.NpcIndex], frame))
        {
            continue;
        }
        const double distanceM = (ship.PositionM - viewpointM).Size();
        if (distanceM >= gateM)
        {
            continue;
        }
        const FSOLNpcRail& rail = rails[ship.NpcIndex];
        const FSOLLaneThrustState now = ThrusterSmokeThrust(rail, frame, ship.NpcIndex, timeS);
        const double stepS = THRSMOKE_STATE_STEP_S;
        const FSOLLaneThrustState before = ThrusterSmokeThrust(rail, frame, ship.NpcIndex, timeS - stepS);
        const FSOLLaneThrustState after = ThrusterSmokeThrust(rail, frame, ship.NpcIndex, timeS + stepS);
        if (ThrusterSmokeIsTurn(now.Segment) || now.Thrust == ESOLLaneThrust::Turn)
        {
            ++s.Turns;
            continue;
        }
        if (before.Thrust != now.Thrust || after.Thrust != now.Thrust)
        {
            ++s.Transitions;
            continue;
        }
        const bool bBurn = ThrusterSmokeIsBurn(now.Thrust);
        const double railG = now.AccelMps2 / SOL::STANDARD_GRAVITY_MPS2;
        if (bBurn && SOLThruster::PixelLength(SOLThruster::PlumeLengthM(SOL::Thruster::NPC_HULL_LENGTH_M, railG),
            distanceM, focalPx) < THRSMOKE_MIN_PX)
        {
            ++s.Small;
            continue;
        }
        if (!mVisuals->GetPushedPlume(index, plume))
        {
            ++s.Unreadable;
            continue;
        }
        const int32 tier = ship.AiTier == static_cast<uint8>(SOLNpcScale::EAiTier::Mid) ? 1 : 0;
        if (bBurn)
        {
            const double error = FMath::Abs(static_cast<double>(plume.Brightness)
                - SOLThruster::PlumeBrightness(railG));
            ++s.BurnSeen[tier];
            s.BurnLit[tier] += plume.Brightness > 0.0f && plume.LengthM > 0.0f ? 1 : 0;
            s.BurnClose[tier] += error <= (tier == 1 ? THRSMOKE_MID_TOLERANCE : THRSMOKE_FULL_TOLERANCE) ? 1 : 0;
            s.BurnWorstErr[tier] = FMath::Max(s.BurnWorstErr[tier], error);
            s.BurnShips.Add(ship.NpcIndex);
        }
        else
        {
            ++s.CoastSeen[tier];
            s.CoastLit[tier] += plume.Brightness > 0.0f ? 1 : 0;
            s.CoastMaxB[tier] = FMath::Max(s.CoastMaxB[tier], static_cast<double>(plume.Brightness));
            s.CoastShips.Add(ship.NpcIndex);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of an interleaved profile (A = sol.NpcPlumes 1, B = 0, ordered A B B A per pair of pairs): the
// switch, a settle, then the stat dumpave window with its ProfileGPU frames and the window means; true once done
bool FSOLThrusterSmoke::TickProfile(const TCHAR* view, UWorld* world, const float realDeltaSeconds)
{
    const int32 windows = 2 * (mIsRibbons ? THRSMOKE_RIBBON_PROFILE_PAIRS : THRSMOKE_PROFILE_PAIRS);
    if (mProfileWindow >= windows)
    {
        return true;
    }
    const bool bPlumes = (mProfileWindow % 4 == 0) || (mProfileWindow % 4 == 3);
    const TCHAR* state = bPlumes ? TEXT("A") : TEXT("B");
    constexpr int32 start = THRSMOKE_PROFILE_SETTLE_FRAMES;
    constexpr int32 end = start + THRSMOKE_PROFILE_FRAMES;
    if (mProfileFrame == 0)
    {
        // The A/B switch: the plumes, or the ribbons (sol.ThrusterRibbons 0 also deactivates the Niagara set, so A - B
        // is its whole cost)
        if (mIsRibbons)
        {
            ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBONS_CVAR, bPlumes ? 1 : 0);
        }
        else
        {
            ThrusterSmokeSetCvar(SOL::NPC_PLUME_CVAR, bPlumes ? 1 : 0);
        }
        mSumCollectMs = mSumNpcMs = mSumGpuMs = mSumGameMs = mSumFrameMs = 0.0;
        mSumRibbonMs = mSumNiagaraTrails = mSumPoolTrails = mSumLiveTrails = mSumCulledTrails = 0.0;
        for (double& phaseMs : mSumRibbonPhaseMs)
        {
            phaseMs = 0.0;
        }
        mSumFrames = 0;
        mGpuFrames = 0;
    }
    if (mProfileFrame == start)
    {
        // The hull-drawn ships and their lit plumes now, then the window
        mWindowHulls = 0;
        mWindowLit = 0;
        const TConstArrayView<FSOLNpcCpuShip> ships = mNpcs->GetCpuShips();
        for (int32 index = 0; index < ships.Num(); ++index)
        {
            FSOLPlumeInstance plume;
            if (ships[index].bHidden == 0 && ships[index].RenderTier
                != static_cast<uint8>(SOLNpcScale::ERenderTier::Point))
            {
                ++mWindowHulls;
                mWindowLit += mVisuals->GetPushedPlume(index, plume) && plume.Brightness > 0.0f ? 1 : 0;
            }
        }
        FVector2D size(0.0, 0.0);
        if (world->GetGameViewport() != nullptr)
        {
            world->GetGameViewport()->GetViewportSize(size);
        }
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster PROFILE %s window %d %s: start (viewport %.0fx%.0f, %d hulls, %d lit)"),
            view, mProfileWindow, state, size.X, size.Y, mWindowHulls, mWindowLit);
        ThrusterSmokeRunCommand(*FString::Printf(TEXT("stat dumpave -num=%d -ms=0 -root=%s"), THRSMOKE_PROFILE_FRAMES,
            THRSMOKE_PROFILE_ROOT), world);
    }
    if (mProfileFrame > start && mProfileFrame <= end)
    {
        // Window means; the frames around a ProfileGPU are left out of the GPU frame mean
        const int32 windowFrame = mProfileFrame - start;
        bool bNearGpu = false;
        for (const int32 gpuFrame : THRSMOKE_PROFILE_GPU_FRAMES)
        {
            if (mIsRibbons)
            {
                break;                                      // The ribbon profile reads the GPU frame mean only
            }
            if (windowFrame == gpuFrame)
            {
                UE_LOG(LogSOL, Log, TEXT("SmokeThruster PROFILE %s window %d %s: ProfileGPU"), view, mProfileWindow,
                    state);
                ThrusterSmokeRunCommand(TEXT("ProfileGPU"), world);
            }
            bNearGpu |= FMath::Abs(windowFrame - gpuFrame) <= THRSMOKE_PROFILE_GPU_GUARD;
        }
        mSumCollectMs += mNpcs->GetStats().CollectMs;
        mSumNpcMs += mNpcs->GetStats().TotalMs;
        mSumGameMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
        mSumFrameMs += realDeltaSeconds * 1000.0;
        if (mIsRibbons)
        {
            mSumRibbonMs += mRibbons->GetLastTickMs();
            mSumNiagaraTrails += mRibbons->GetNiagaraTrailCount();
            mSumPoolTrails += mRibbons->GetPoolTrailCount();
            mSumLiveTrails += mRibbons->GetLiveTrailCount();
            mSumCulledTrails += mRibbons->GetCulledTrailCount();
            for (int32 phase = 0; phase < ASOLThrusterRibbons::RIBBON_PHASES; ++phase)
            {
                mSumRibbonPhaseMs[phase] += mRibbons->GetLastPhaseMs(phase);
            }
        }
        ++mSumFrames;
        if (!bNearGpu)
        {
            mSumGpuMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
            ++mGpuFrames;
        }
    }
    if (mProfileFrame == end + THRSMOKE_PROFILE_TAIL_FRAMES)
    {
        const double frames = FMath::Max(mSumFrames, 1);
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster PROFILE %s window %d %s: collectMs=%.4f npcMs=%.4f gpuMs=%.4f "
            "gameMs=%.3f frameMs=%.3f frames=%d gpuFrames=%d hulls=%d lit=%d"), view, mProfileWindow, state,
            mSumCollectMs / frames, mSumNpcMs / frames, mGpuFrames > 0 ? mSumGpuMs / mGpuFrames : 0.0,
            mSumGameMs / frames, mSumFrameMs / frames, mSumFrames, mGpuFrames, mWindowHulls, mWindowLit);
        if (mIsRibbons)
        {
            UE_LOG(LogSOL, Log, TEXT("SmokeThruster PROFILE %s window %d %s: ribbonMs=%.4f niagara=%.1f pool=%.1f "
                "live=%.1f culled=%.1f commitMs=%.4f storeMs=%.4f splitMs=%.4f niagaraMs=%.4f poolMs=%.4f "
                "nArraysMs=%.4f nMoveMs=%.4f setupMs=%.4f fillMs=%.4f nBuildMs=%.4f"), view, mProfileWindow, state,
                mSumRibbonMs / frames, mSumNiagaraTrails / frames, mSumPoolTrails / frames, mSumLiveTrails / frames,
                mSumCulledTrails / frames, mSumRibbonPhaseMs[0] / frames, mSumRibbonPhaseMs[1] / frames,
                mSumRibbonPhaseMs[2] / frames, mSumRibbonPhaseMs[3] / frames, mSumRibbonPhaseMs[4] / frames,
                mSumRibbonPhaseMs[5] / frames, mSumRibbonPhaseMs[6] / frames, mSumRibbonPhaseMs[7] / frames,
                mSumRibbonPhaseMs[8] / frames, mSumRibbonPhaseMs[9] / frames);
        }
        ++mProfileWindow;
        mProfileFrame = 0;
        return mProfileWindow >= windows;
    }
    ++mProfileFrame;
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot (with or without the HUD) named Thruster_<width>x<height>_<name>.png
void FSOLThrusterSmoke::TakeShot(const TCHAR* name, const bool bShowUi) const
{
    FVector2D size(0.0, 0.0);
    if (mVisuals.IsValid() && mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("Thruster_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), name));
    FScreenshotRequest::RequestScreenshot(path, bShowUi, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeThruster: screenshot %s"), *path);

    // Where the ridden ship is drawn relative to the player pawn (the camera's cached location is a frame old)
    const int32 cpu = FindCpuShip(mTarget);
    const APlayerController* controller = mVisuals->GetWorld()->GetFirstPlayerController();
    const APawn* pawn = controller != nullptr ? controller->GetPawn() : nullptr;
    if (cpu != INDEX_NONE && pawn != nullptr)
    {
        const FSOLNpcCpuShip& ship = mNpcs->GetCpuShips()[cpu];
        const FVector toTargetCm = mAnchor->UniverseToRenderCm(ship.PositionM) - pawn->GetActorLocation();
        const FVector toTarget = toTargetCm.GetSafeNormal();
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster: ridden ship %d (render tier %d, AI tier %d) %.0f m from the pawn, "
            "forward . to-ship %.3f, up . to-ship %.3f, thrust (%.3f, %.3f, %.3f) G"), mTarget, ship.RenderTier,
            ship.AiTier, toTargetCm.Size() / SOL::METERS_TO_CM, pawn->GetActorForwardVector() | toTarget,
            pawn->GetActorUpVector() | toTarget, ship.ThrustLocalG.X, ship.ThrustLocalG.Y, ship.ThrustLocalG.Z);
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster: ridden ship's nose . screen right %.3f (the plume leaves the stern, on "
            "the other side)"), FVector(SOLRender::EclipticToUnreal(FVector3d(ship.HeadingDirection)))
            | pawn->GetActorRightVector());
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the frozen-frame no-pop shots: each shot's switches, a settle, the invariants, the screenshot
// without the HUD (its write hold is the caller's); true once every shot is written
bool FSOLThrusterSmoke::TickPop(UWorld* world)
{
    constexpr int32 shots = UE_ARRAY_COUNT(THRSMOKE_POP_SHOTS);
    if (mPopShot >= shots)
    {
        return true;
    }
    const FThrusterPopShot& shot = THRSMOKE_POP_SHOTS[mPopShot];
    const bool bToTsr = shot.AntiAliasing != 0 && (mPopShot == 0 || THRSMOKE_POP_SHOTS[mPopShot - 1].AntiAliasing == 0);
    if (mPopFrame == 0)
    {
        const int32 antiAliasing = shot.AntiAliasing >= 0 ? shot.AntiAliasing : mAntiAliasingDefault;
        if (antiAliasing != INDEX_NONE)
        {
            ThrusterSmokeSetCvar(THRSMOKE_AA_CVAR, antiAliasing);
        }
        ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBON_NIAGARA_PX_CVAR, shot.NiagaraPx);
        ThrusterSmokeSetCvar(SOL::THRUSTER_RIBBONS_CVAR, shot.Ribbons);
    }
    if (++mPopFrame < (bToTsr ? THRSMOKE_POP_TSR_SETTLE_FRAMES : THRSMOKE_POP_SETTLE_FRAMES))
    {
        return false;
    }

    // The invariants against the first shot: the camera fixed relative to the ridden ship (the dilated game still
    // moves both a little in render space) and the ridden ship's trail drawn and on screen; a pool shot holds every
    // NPC trail in the pool. The live count is logged only (far ships keep being fed while frozen)
    const APlayerController* controller = world->GetFirstPlayerController();
    const FVector cameraCm = controller != nullptr && controller->PlayerCameraManager != nullptr
        ? controller->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
    const FVector3d anchorCm = mRibbons->GetLastAnchorCm();
    const int32 live = mRibbons->GetLiveTrailCount();
    const int32 niagaraNpc = LogRibbons(shot.Name, THRSMOKE_RIBBON_FAR_LOG_M);
    const int32 pool = mRibbons->GetPoolTrailCount();
    TArray<FSOLRibbonDrawnTrail> drawn;
    mRibbons->GetDrawnNpcTrails(drawn);
    const FSOLRibbonDrawnTrail* ridden = drawn.FindByPredicate([this](const FSOLRibbonDrawnTrail& trail)
    {
        return trail.Key == mTarget;
    });
    const FVector3d relativeCm = ridden != nullptr ? FVector3d(cameraCm) - ridden->ShipPositionM * SOL::METERS_TO_CM
        : FVector3d(BIG_NUMBER);
    if (mPopShot == 0)
    {
        mPopCameraCm = relativeCm;
        mPopAnchorCm = anchorCm;
        mPopLive = live;
        mPopNiagaraNpc = niagaraNpc;
        mPopPool = pool;
        mIsPopVacuous = niagaraNpc == 0 || ridden == nullptr || !ridden->bNiagara;
    }
    else
    {
        const bool bStable = (shot.Ribbons == 0 || ridden != nullptr)
            && (shot.Ribbons == 0 || relativeCm.Equals(mPopCameraCm, THRSMOKE_POP_RELATIVE_TOL_CM));
        const bool bSwitched = shot.NiagaraPx != THRSMOKE_POOL_PX || (niagaraNpc == 0
            && pool == mPopPool + mPopNiagaraNpc);
        mIsPopStable &= bStable;
        mIsPopSwitched &= bSwitched;
        if (!bStable || !bSwitched)
        {
            UE_LOG(LogSOL, Warning, TEXT("SmokeThruster RIBBON %s: camera moved %.4f cm, anchor moved %.4f cm, live %d "
                "(first %d), NPC Niagara %d, pool %d (first %d + %d)"), shot.Name, (relativeCm - mPopCameraCm).Size(),
                (anchorCm - mPopAnchorCm).Size(), live, mPopLive, niagaraNpc, pool, mPopPool, mPopNiagaraNpc);
        }
    }
    TakeShot(shot.Name, false);
    mShotHoldS = THRSMOKE_RIBBON_SHOT_HOLD_S;
    ++mPopShot;
    mPopFrame = 0;
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Freezes (time dilation at the world settings' minimum, the ribbons' clock fixed at now) or thaws the game
void FSOLThrusterSmoke::SetFrozen(UWorld* world, const bool bFrozen)
{
    UGameplayStatics::SetGlobalTimeDilation(world, bFrozen ? THRSMOKE_FROZEN_DILATION : 1.0f);
    if (mRibbons.IsValid())
    {
        mRibbons->SetVerificationClock(bFrozen ? world->GetTimeSeconds() : -1.0);
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeThruster RIBBON: game %s at %.4f s"), bFrozen ? TEXT("frozen") : TEXT("thawed"),
        world->GetTimeSeconds());
}

//////////////////////////////////////////////////////////////////////////
// Logs the ribbons' counts, camera and anchor under a label, and the drawn NPC trails with their screen places (every
// Niagara one, the longest pool ones, the longest at least farM away); returns the Niagara-drawn NPC trails
int32 FSOLThrusterSmoke::LogRibbons(const TCHAR* label, const double farM) const
{
    TArray<FSOLRibbonDrawnTrail> trails;
    mRibbons->GetDrawnNpcTrails(trails);
    const APlayerController* controller = mVisuals->GetWorld()->GetFirstPlayerController();
    const FVector cameraCm = controller != nullptr && controller->PlayerCameraManager != nullptr
        ? controller->PlayerCameraManager->GetCameraLocation() : FVector::ZeroVector;
    const FVector3d anchorCm = mRibbons->GetLastAnchorCm();
    int32 niagaraNpc = 0;
    for (const FSOLRibbonDrawnTrail& trail : trails)
    {
        niagaraNpc += trail.bNiagara ? 1 : 0;
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeThruster RIBBON %s: Niagara %d (NPC %d), pool %d, live %d, tick %.4f ms, camera "
        "(%.3f, %.3f, %.3f) cm, anchor (%.0f, %.0f, %.0f) cm, device focal %.2f px"), label,
        mRibbons->GetNiagaraTrailCount(), niagaraNpc, mRibbons->GetPoolTrailCount(), mRibbons->GetLiveTrailCount(),
        mRibbons->GetLastTickMs(), cameraCm.X, cameraCm.Y, cameraCm.Z, anchorCm.X, anchorCm.Y, anchorCm.Z,
        mRibbons->GetLastDeviceFocalPx());

    // Logs one trail with its ship's screen place
    const auto logTrail = [label, controller](const FSOLRibbonDrawnTrail& trail, const TCHAR* group)
    {
        FVector2D screen(-1.0, -1.0);
        const bool bOnScreen = controller != nullptr && controller->ProjectWorldLocationToScreen(
            FVector(trail.ShipPositionM * SOL::METERS_TO_CM), screen, true);
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster RIBBON %s %s key=%d px=%.2f d=%.0f screen=%s(%.1f, %.1f)"), label,
            group, trail.Key, trail.RibbonPx, trail.DistanceM, bOnScreen ? TEXT("") : TEXT("off "), screen.X,
            screen.Y);
    };
    TArray<const FSOLRibbonDrawnTrail*> pool;
    TArray<const FSOLRibbonDrawnTrail*> far;
    for (const FSOLRibbonDrawnTrail& trail : trails)
    {
        if (trail.bNiagara)
        {
            logTrail(trail, TEXT("niagara"));
        }
        else
        {
            pool.Add(&trail);
        }
        if (trail.DistanceM >= farM)
        {
            far.Add(&trail);
        }
    }
    // Orders trails by on-screen length, longest first
    const auto longer = [](const FSOLRibbonDrawnTrail& a, const FSOLRibbonDrawnTrail& b)
    {
        return a.RibbonPx > b.RibbonPx;
    };
    pool.Sort(longer);
    far.Sort(longer);
    for (int32 index = 0; index < FMath::Min(pool.Num(), THRSMOKE_RIBBON_LOG_TRAILS); ++index)
    {
        logTrail(*pool[index], TEXT("pool"));
    }
    for (int32 index = 0; index < FMath::Min(far.Num(), THRSMOKE_RIBBON_LOG_TRAILS); ++index)
    {
        logTrail(*far[index], TEXT("far"));
    }
    return niagaraNpc;
}

//////////////////////////////////////////////////////////////////////////
// Picks an Earth-Mars lane ship in a U-turn now and holdS later (the same turn), the hardest-thrusting one
int32 FSOLThrusterSmoke::PickTurnTarget(const double holdS) const
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    if (mLane == INDEX_NONE || !frame.LaneFrame.IsValidIndex(mLane))
    {
        return INDEX_NONE;
    }
    int32 best = INDEX_NONE;
    double bestAccelMps2 = -1.0;
    for (const int32 index : mLaneShips)
    {
        const FSOLLaneThrustState now = ThrusterSmokeThrust(rails[index], frame, index, frame.RailTimeS);
        const FSOLLaneThrustState later = ThrusterSmokeThrust(rails[index], frame, index, frame.RailTimeS + holdS);
        if (ThrusterSmokeIsTurn(now.Segment) && now.Segment == later.Segment && now.AccelMps2 > bestAccelMps2)
        {
            bestAccelMps2 = now.AccelMps2;
            best = index;
        }
    }
    if (best != INDEX_NONE)
    {
        const FSOLLaneThrustState state = ThrusterSmokeThrust(rails[best], frame, best, frame.RailTimeS);
        UE_LOG(LogSOL, Log, TEXT("SmokeThruster RIBBON: U-turn target ship %d, segment %d, thrust %d at %.3f G"), best,
            static_cast<int32>(state.Segment), static_cast<int32>(state.Thrust),
            state.AccelMps2 / SOL::STANDARD_GRAVITY_MPS2);
    }
    return best;
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLThrusterSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeThruster CHECK %-18s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
