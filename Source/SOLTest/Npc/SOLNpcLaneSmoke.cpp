/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcLaneSmoke.h"

#include "Game/SOLGameMode.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Npc/SOLNpcLaneMath.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcVisuals.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "UI/SOLHudShapeMath.h"
#include "UI/SOLUiProfileCapture.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    // Phase durations (real seconds)
    constexpr double LANESMOKE_WAIT_TIMEOUT_S = 240.0;       // Population spawn and upload (1M ships)
    constexpr double LANESMOKE_VIEW_SETUP_S = 2.5;           // Anchor, rebase and the far layer settle after a move
    constexpr double LANESMOKE_VARIANT_S = 1.2;              // A lanes / glow change reaches the materials
    constexpr double LANESMOKE_SHOT_S = 0.8;                 // The screenshot is written before the next change
    constexpr double LANESMOKE_FLOW_S = 0.5;                 // Flow measurement (0.375 periods at 0.75 / s)
    constexpr double LANESMOKE_PAUSE_OPEN_S = 0.6;
    constexpr double LANESMOKE_PAUSE_HOLD_S = 1.0;
    constexpr double LANESMOKE_MAP_S = 3.0;
    constexpr double LANESMOKE_TAP_S = 0.6;
    constexpr int32 LANESMOKE_MIN_PHASE_FRAMES = 5;

    // Script values
    constexpr int32 LANESMOKE_SHIPS = 1000000;               // Population (SDD 38 section 8: 1M NPCs)
    const TCHAR* const LANESMOKE_VIEW_NAMES[] = {             // V4b: the near-axis regression view (38b review)
        TEXT("V1"), TEXT("V2"), TEXT("V3"), TEXT("V4"), TEXT("V4b"), TEXT("V5") };
    constexpr int32 LANESMOKE_VIEWS = UE_ARRAY_COUNT(LANESMOKE_VIEW_NAMES);
    constexpr int32 LANESMOKE_WARP_STEPS = 4;                // Time warp steps during the flow check
    constexpr double LANESMOKE_FLOW_TOLERANCE = 0.05;        // Flow phase vs real time, periods (frame jitter)
    constexpr float LANESMOKE_GLOW_TOLERANCE = 1.0e-5f;
    constexpr double LANESMOKE_VENUS_VIEW_AU = 0.15;         // V2: off Venus (SDD 11's converging-lanes view)
    constexpr double LANESMOKE_SUN_VIEW_AU = 3.0;            // V3: above the Sun (all 8 lanes)
    constexpr double LANESMOKE_EARTH_SIDE_M = 4.0e7;         // V5: beside Earth, across the Earth-Moon lane
    constexpr double LANESMOKE_EARTH_LOOK_M = 2.0e7;         // ...looking at this point of the lane (Earth in view)
    constexpr double LANESMOKE_LANE_YAW_DEG = 25.0;          // V4: look this far off the lane's axis toward Mars...
    constexpr double LANESMOKE_LANE_OFFSET_RADII = 0.5;      // ...from this far off its centre line (inside it)
    constexpr double LANESMOKE_AXIS_YAW_DEG = 1.5;           // V4b: look this little off the lane's axis toward Mars...
    constexpr double LANESMOKE_AXIS_OFFSET_RADII = 1.0;      // ...from one lane radius off its centre line
    constexpr double LANESMOKE_PROJECT_CM = 1.0e5;           // Lane endpoints projected along their direction

    // Profile flag (SDD 38 section 8 item 7): V2 and V3, lanes drawn then sol.NpcLanes 0
    constexpr int32 LANESMOKE_PROFILE_VIEWS[] = { 1, 2 };
    constexpr double LANESMOKE_PROFILE_SETTLE_S = 4.0;       // Anchor, far layer and GPU settle after the move
    constexpr int32 LANESMOKE_PROFILE_FRAMES = 240;          // stat dumpave window
    constexpr int32 LANESMOKE_PROFILE_GPU_FRAMES[] = { 20, 100, 180 };   // ProfileGPU inside the window
    constexpr int32 LANESMOKE_PROFILE_TAIL_FRAMES = 30;      // Frames left for the dump to print
    const TCHAR* const LANESMOKE_PROFILE_AVE_ROOT = TEXT("STAT_SOL_NpcRedraw");   // stat dumpave root
    constexpr float LANESMOKE_PROFILE_MAX_WINDOW_PX = 16384.0f;   // Lifted window size limit (4K and one-eye views)

    // The variants shot per view: lanes shown and the pending HUD glow
    struct FSOLLaneSmokeVariant
    {
        const TCHAR* Name;
        bool bShown;
        float HudGlow;
    };
    constexpr FSOLLaneSmokeVariant LANESMOKE_VARIANTS[] = {
        { TEXT("on_glow100"), true, SOL::Settings::HUD_GLOW_DEFAULT },
        { TEXT("on_glow0"), true, SOL::Settings::HUD_GLOW_MIN },
        { TEXT("on_glow200"), true, SOL::Settings::HUD_GLOW_MAX },
        { TEXT("off"), false, SOL::Settings::HUD_GLOW_DEFAULT },
    };
    constexpr int32 LANESMOKE_VARIANT_COUNT = UE_ARRAY_COUNT(LANESMOKE_VARIANTS);

    //////////////////////////////////////////////////////////////////////////
    // Returns the wrapped distance between two phases in [0, 1)
    double LaneSmokeWrapDistance(const double a, const double b)
    {
        const double d = FMath::Abs(a - b);
        return FMath::Min(d - FMath::FloorToDouble(d), 1.0 - (d - FMath::FloorToDouble(d)));
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets an integer console variable (sol.NpcLanes, sol.SpaceDust)
    void LaneSmokeSetCvar(const TCHAR* name, const int32 value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets the -ResX / -ResY view past the desktop size with r.SetRes (a windowed game is otherwise clamped to the
    // desktop's work area), as the menu profile smokes do; nothing without both values or before the window exists
    void LaneSmokeApplyCommandLineView(UWorld* world)
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
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(LANESMOKE_PROFILE_MAX_WINDOW_PX)
            .SetMaxHeight(LANESMOKE_PROFILE_MAX_WINDOW_PX));
        GEngine->Exec(world, *FString::Printf(TEXT("r.SetRes %dx%dw"), width, height));
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes: profile view at %dx%d (window size limit lifted)"), width, height);
    }

    //////////////////////////////////////////////////////////////////////////
    // Runs a console command in the world (the profile captures)
    void LaneSmokeRunCommand(const TCHAR* command, UWorld* world)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes: console command '%s'"), command);
        FSOLUiProfileCapture::RunCommand(command, world);
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
void FSOLNpcLaneSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const ASOLGameMode* gameMode = world->GetAuthGameMode<ASOLGameMode>();
    mVisuals = gameMode != nullptr ? gameMode->GetNpcVisuals() : nullptr;
    mOrbitLines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
    mNpcs = world->GetSubsystem<USOLNpcSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mMenu = world->GetSubsystem<USOLMenuSubsystem>();
    mSimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    mSettings = USOLSettingsSubsystem::Get(&pawn);

    // The cost captures need a standalone -game process (in the editor ProfileGPU opens its visualizer)
    mIsProfile = FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_NPC_LANES_PROFILE) && !GIsEditor;
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes: starting (NPC visuals %s, orbit lines %s, settings %s, profile %s)"),
        mVisuals.IsValid() ? *mVisuals->GetName() : TEXT("none"), mOrbitLines.IsValid() ? *mOrbitLines->GetName()
        : TEXT("none"), mSettings.IsValid() ? TEXT("yes") : TEXT("none"), mIsProfile ? TEXT("on") : TEXT("off"));

    // Overrides: the lanes and the orbit lines leave the player's settings alone; orbit lines off (D24 check)
    if (mVisuals.IsValid())
    {
        mVisuals->SetLaneVerificationOverride(true);
    }
    if (mOrbitLines.IsValid())
    {
        mOrbitLines->BeginVerificationOverride();
        mOrbitLines->SetLinesEnabled(false);
    }

    // The speed cue's streaks would cover the lanes after each teleport (in memory only, restored at the end)
    LaneSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 0);
    if (mNpcs.IsValid() && mNpcs->GetRequestedCount() == 0)
    {
        mNpcs->RequestNpcCount(LANESMOKE_SHIPS);
    }
    if (mShips.IsValid())
    {
        mStartPositionM = mShips->GetState().PositionM;
    }
    mPhase = ESOLLanePhase::WaitPopulation;
    EnterPhase(mPhase, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLNpcLaneSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLLanePhase::Done)
    {
        return true;
    }
    if (!mVisuals.IsValid() || !mNpcs.IsValid() || !mShips.IsValid() || !mRegistry.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeNpcLanes: NPC visuals or subsystems missing; aborting"));
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes SUMMARY: aborted, %d/%d checks passed (FAILURES)"), mChecksPassed,
            mChecksRun);
        mPhase = ESOLLanePhase::Done;
        if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
        {
            gameMode->TakeSmokeScreenshot();
        }
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;

    // The population wait ends as soon as lanes are drawn (or times out)
    if (mPhase == ESOLLanePhase::WaitPopulation)
    {
        const bool bReady = mNpcs->GetRails().Num() >= LANESMOKE_SHIPS && mVisuals->GetLaneMeshVertexCount() > 0
            && mVisuals->IsLaneComponentVisible();
        if (!bReady && mPhaseTimeS < LANESMOKE_WAIT_TIMEOUT_S)
        {
            return false;
        }
    }
    else if (mPhase == ESOLLanePhase::ProfileOn || mPhase == ESOLLanePhase::ProfileOff)
    {
        if (!TickProfile(mPhase == ESOLLanePhase::ProfileOn ? TEXT("lanes drawn") : TEXT("sol.NpcLanes 0"),
            pawn.GetWorld()))
        {
            return false;
        }
    }
    else if (mPhaseTimeS < GetPhaseDuration(mPhase) || mPhaseFrames < LANESMOKE_MIN_PHASE_FRAMES)
    {
        return false;
    }

    ExitPhase(mPhase, pawn);
    mPhase = NextPhase(mPhase);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    if (mPhase == ESOLLanePhase::Done)
    {
        return true;
    }
    EnterPhase(mPhase, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the next phase after one has ended (variants per view, then the next view, then the single checks)
FSOLNpcLaneSmoke::ESOLLanePhase FSOLNpcLaneSmoke::NextPhase(const ESOLLanePhase phase)
{
    // The profile flag skips the views and checks; each profile view runs its setup, lanes drawn, then lanes off
    if (phase == ESOLLanePhase::WaitPopulation && mIsProfile)
    {
        mProfileView = 0;
        mView = LANESMOKE_PROFILE_VIEWS[0];
        return ESOLLanePhase::ProfileSetup;
    }
    if (phase == ESOLLanePhase::GlowRevert)
    {
        return ESOLLanePhase::Done;
    }
    if (phase == ESOLLanePhase::ProfileOff)
    {
        if (mProfileView + 1 < static_cast<int32>(UE_ARRAY_COUNT(LANESMOKE_PROFILE_VIEWS)))
        {
            ++mProfileView;
            mView = LANESMOKE_PROFILE_VIEWS[mProfileView];
            return ESOLLanePhase::ProfileSetup;
        }
        return ESOLLanePhase::Done;
    }
    if (phase != ESOLLanePhase::VariantShot)
    {
        return static_cast<ESOLLanePhase>(static_cast<uint8>(phase) + 1);
    }
    if (mVariant + 1 < LANESMOKE_VARIANT_COUNT)
    {
        return ESOLLanePhase::VariantSet;
    }
    if (mView + 1 < LANESMOKE_VIEWS)
    {
        ++mView;
        return ESOLLanePhase::ViewSetup;
    }
    return ESOLLanePhase::Flow;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions
void FSOLNpcLaneSmoke::EnterPhase(const ESOLLanePhase phase, ASOLShipPawn& pawn)
{
    switch (phase)
    {
    case ESOLLanePhase::ViewSetup:
        // A new view (mView set by NextPhase): lanes on at 100% while the ship moves and the far layer settles
        mVariant = -1;
        SetupView();
        mVisuals->SetLaneVerificationOverride(true);
        SetPendingGlow(SOL::Settings::HUD_GLOW_DEFAULT);
        break;
    case ESOLLanePhase::VariantSet:
    {
        ++mVariant;
        const FSOLLaneSmokeVariant& variant = LANESMOKE_VARIANTS[mVariant];
        mVisuals->SetLaneVerificationOverride(variant.bShown);
        SetPendingGlow(variant.HudGlow);
        break;
    }
    case ESOLLanePhase::VariantShot:
        TakeShot(FString::Printf(TEXT("%s_%s"), GetViewName(), LANESMOKE_VARIANTS[mVariant].Name));
        break;
    case ESOLLanePhase::Flow:
        // Real-time flow under time warp (D15): the measurement starts here
        mVisuals->SetLaneVerificationOverride(true);
        SetPendingGlow(SOL::Settings::HUD_GLOW_DEFAULT);
        if (mSimClock.IsValid())
        {
            for (int32 step = 0; step < LANESMOKE_WARP_STEPS; ++step)
            {
                mSimClock->StepWarpUp();
            }
        }
        mFlowStartPhase = mVisuals->GetLaneFlowPhase();
        mFlowStartS = FPlatformTime::Seconds();
        break;
    case ESOLLanePhase::PauseOpen:
        if (mMenu.IsValid())
        {
            mMenu->RequestEscape();
        }
        break;
    case ESOLLanePhase::PauseHold:
        mFlowStartPhase = mVisuals->GetLaneFlowPhase();
        mFlowStartS = FPlatformTime::Seconds();
        break;
    case ESOLLanePhase::Resume:
        if (mMenu.IsValid())
        {
            mMenu->RequestResume();
        }
        if (mSimClock.IsValid())
        {
            mSimClock->ResetWarp();
        }
        mFlowStartPhase = mVisuals->GetLaneFlowPhase();
        mFlowStartS = FPlatformTime::Seconds();
        break;
    case ESOLLanePhase::CvarOff:
        LaneSmokeSetCvar(SOL::NPC_LANE_CVAR, 0);
        break;
    case ESOLLanePhase::CvarOn:
        LaneSmokeSetCvar(SOL::NPC_LANE_CVAR, 1);
        break;
    case ESOLLanePhase::MapOpen:
        pawn.HandleOpenMap(false);
        break;
    case ESOLLanePhase::MapClose:
        pawn.HandleCloseMap();
        break;
    case ESOLLanePhase::GlowRevert:
        if (mSettings.IsValid())
        {
            mSettings->RevertPending();
        }
        break;
    case ESOLLanePhase::ProfileSetup:
        // Lanes on at 100% glow at the profile view; the material draw events name the lane and point draws
        SetupView();
        mVisuals->SetLaneVerificationOverride(true);
        SetPendingGlow(SOL::Settings::HUD_GLOW_DEFAULT);
        LaneSmokeSetCvar(SOL::NPC_LANE_CVAR, 1);
        LaneSmokeRunCommand(TEXT("r.ShowMaterialDrawEvents 1"), pawn.GetWorld());
        LaneSmokeRunCommand(TEXT("r.VSync 0"), pawn.GetWorld());
        LaneSmokeApplyCommandLineView(pawn.GetWorld());
        break;
    case ESOLLanePhase::ProfileOn:
    case ESOLLanePhase::ProfileOff:
        LaneSmokeSetCvar(SOL::NPC_LANE_CVAR, phase == ESOLLanePhase::ProfileOn ? 1 : 0);
        mProfileFrame = 0;
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks
void FSOLNpcLaneSmoke::ExitPhase(const ESOLLanePhase phase, ASOLShipPawn& pawn)
{
    const double flow = SOL::NPC_LANE_DASH_FLOW_PERIODS_PER_S;
    switch (phase)
    {
    case ESOLLanePhase::WaitPopulation:
    {
        const int32 rails = mNpcs->GetRails().Num();
        Check(TEXT("a-population"), rails >= LANESMOKE_SHIPS && mVisuals->GetLaneMeshVertexCount() > 0,
            FString::Printf(TEXT("%d rails after %.1f s, lane ribbon %d vertices (%d expected for 8 lanes)"), rails,
                mPhaseTimeS, mVisuals->GetLaneMeshVertexCount(), SOLNpcLane::LaneRibbonVertexCount(8)));
        mOccluderWritesAtStart = mOrbitLines.IsValid() ? mOrbitLines->GetLaneOnlyOccluderWrites() : 0;

        // D15 amendment B: the shader receives the coarsest dash level (NpcLaneStyle2.z)
        FLinearColor style2 = FLinearColor::Black;
        const bool bStyle = mVisuals->GetPushedLaneStyle2(style2);
        Check(TEXT("a-dash-min-level"), bStyle && style2.B == static_cast<float>(SOL::NPC_LANE_DASH_MIN_LEVEL),
            FString::Printf(TEXT("NpcLaneStyle2.z %.1f (expected %d)"), style2.B, SOL::NPC_LANE_DASH_MIN_LEVEL));
        break;
    }
    case ESOLLanePhase::ViewSetup:
    case ESOLLanePhase::ProfileSetup:
    {
        // At rest relative to the reference body the move may have changed (the anchor follows the ship)
        FSOLShipState state = mShips->GetState();
        state.VelocityMps = mShips->GetReferenceVelocityMps();
        mShips->SetState(state);
        break;
    }
    case ESOLLanePhase::VariantShot:
        if (mVariant == 0)
        {
            LogLaneScreenSides(pawn);
        }
        break;
    case ESOLLanePhase::VariantSet:
    {
        const FSOLLaneSmokeVariant& variant = LANESMOKE_VARIANTS[mVariant];
        CheckLaneState(*FString::Printf(TEXT("b-%s-%s"), GetViewName(), variant.Name), variant.bShown, variant.HudGlow);
        if (!mIsTexelChecked)
        {
            CheckGroupTexels();
            mIsTexelChecked = true;

            // D24: the orbit lines are off, so only the lanes keep the occluders being written
            const int32 writes = mOrbitLines.IsValid() ? mOrbitLines->GetLaneOnlyOccluderWrites() : 0;
            Check(TEXT("c-occluders"), mOrbitLines.IsValid() && !mOrbitLines->AreLinesEnabled()
                && writes > mOccluderWritesAtStart, FString::Printf(TEXT("orbit lines %s, lane-only occluder writes "
                "%d -> %d"), mOrbitLines.IsValid() && mOrbitLines->AreLinesEnabled() ? TEXT("on") : TEXT("off"),
                mOccluderWritesAtStart, writes));
        }
        break;
    }
    case ESOLLanePhase::Flow:
    {
        const double phase1 = mVisuals->GetLaneFlowPhase();
        const double elapsedS = FPlatformTime::Seconds() - mFlowStartS;
        const double expected = mFlowStartPhase + flow * elapsedS;
        const double error = LaneSmokeWrapDistance(phase1, expected);
        const float warp = mSimClock.IsValid() ? static_cast<float>(mSimClock->GetClock().GetWarpFactor()) : 1.0f;
        Check(TEXT("d-flow-realtime"), phase1 != mFlowStartPhase && error <= LANESMOKE_FLOW_TOLERANCE && warp > 1.0f,
            FString::Printf(TEXT("phase %.4f -> %.4f over %.3f s at %.0fx warp (expected %.4f, error %.4f periods)"),
                mFlowStartPhase, phase1, elapsedS, warp, expected - FMath::FloorToDouble(expected), error));
        break;
    }
    case ESOLLanePhase::PauseHold:
    {
        // The dashes never freeze (D15 amended 2026-11-11): real-time flow with the pause menu open
        const bool bPaused = mMenu.IsValid() && mMenu->GetState().Mode == SOLMenuState::EMenuMode::Paused;
        const double phase1 = mVisuals->GetLaneFlowPhase();
        const double elapsedS = FPlatformTime::Seconds() - mFlowStartS;
        const double expected = mFlowStartPhase + flow * elapsedS;
        const double error = LaneSmokeWrapDistance(phase1, expected);
        Check(TEXT("e-flow-pause-menu"), bPaused && phase1 != mFlowStartPhase && error <= LANESMOKE_FLOW_TOLERANCE,
            FString::Printf(TEXT("pause menu %s, phase %.4f -> %.4f over %.3f s (expected %.4f, error %.4f periods)"),
                bPaused ? TEXT("open") : TEXT("closed"), mFlowStartPhase, phase1, elapsedS,
                expected - FMath::FloorToDouble(expected), error));
        break;
    }
    case ESOLLanePhase::Resume:
    {
        // After the resume, at warp 1: still the real-time rate
        const bool bPlaying = mMenu.IsValid() && mMenu->GetState().Mode == SOLMenuState::EMenuMode::Playing;
        const double phase1 = mVisuals->GetLaneFlowPhase();
        const double elapsedS = FPlatformTime::Seconds() - mFlowStartS;
        const double expected = mFlowStartPhase + flow * elapsedS;
        const double error = LaneSmokeWrapDistance(phase1, expected);
        const double warp = mSimClock.IsValid() ? mSimClock->GetClock().GetWarpFactor() : 1.0;
        Check(TEXT("e-flow-resumed"), bPlaying && warp == 1.0 && phase1 != mFlowStartPhase
            && error <= LANESMOKE_FLOW_TOLERANCE, FString::Printf(TEXT("playing %d, phase %.4f -> %.4f over %.3f s at "
            "%.0fx warp (expected %.4f, error %.4f periods)"), bPlaying ? 1 : 0, mFlowStartPhase, phase1, elapsedS,
            warp, expected - FMath::FloorToDouble(expected), error));
        break;
    }
    case ESOLLanePhase::CvarShotWait:
    {
        float glow = 0.0f;
        FLinearColor pointParams = FLinearColor::Black;
        const bool bParams = mVisuals->GetPushedLaneParams(glow, pointParams);
        Check(TEXT("f-cvar-off"), !mVisuals->IsLaneComponentVisible() && !mVisuals->AreLanesShown() && bParams
            && pointParams.R == 0.0f, FString::Printf(TEXT("sol.NpcLanes 0: lane component %s, points crossfade "
            "%.0f"), mVisuals->IsLaneComponentVisible() ? TEXT("visible") : TEXT("hidden"), pointParams.R));
        break;
    }
    case ESOLLanePhase::CvarOff:
        TakeShot(FString::Printf(TEXT("%s_cvar_off"), GetViewName()));
        break;
    case ESOLLanePhase::CvarOn:
        CheckLaneState(TEXT("f-cvar-on"), true, SOL::Settings::HUD_GLOW_DEFAULT);
        break;
    case ESOLLanePhase::MapOpen:
        TakeShot(TEXT("map"));
        break;
    case ESOLLanePhase::MapShotWait:
        Check(TEXT("g-map-lanes"), pawn.IsMapOpen() && mVisuals->IsLaneComponentVisible(), FString::Printf(
            TEXT("map %s, lane component %s (D18)"), pawn.IsMapOpen() ? TEXT("open") : TEXT("closed"),
            mVisuals->IsLaneComponentVisible() ? TEXT("visible") : TEXT("hidden")));
        break;
    case ESOLLanePhase::GlowRevert:
    {
        const float saved = mSettings.IsValid() ? mSettings->GetSavedValues().HudGlow
            : SOL::Settings::HUD_GLOW_DEFAULT;
        CheckLaneState(TEXT("h-glow-revert"), true, saved);
        Finish(pawn);
        break;
    }
    case ESOLLanePhase::ProfileOff:
        LaneSmokeSetCvar(SOL::NPC_LANE_CVAR, 1);
        if (mProfileView + 1 >= static_cast<int32>(UE_ARRAY_COUNT(LANESMOKE_PROFILE_VIEWS)))
        {
            Finish(pawn);
        }
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase in seconds
double FSOLNpcLaneSmoke::GetPhaseDuration(const ESOLLanePhase phase)
{
    switch (phase)
    {
    case ESOLLanePhase::ViewSetup:
        return LANESMOKE_VIEW_SETUP_S;
    case ESOLLanePhase::ProfileSetup:
        return LANESMOKE_PROFILE_SETTLE_S;
    case ESOLLanePhase::VariantSet:
    case ESOLLanePhase::CvarOff:
        return LANESMOKE_VARIANT_S;
    case ESOLLanePhase::VariantShot:
    case ESOLLanePhase::CvarShotWait:
    case ESOLLanePhase::MapShotWait:
        return LANESMOKE_SHOT_S;
    case ESOLLanePhase::Flow:
        return LANESMOKE_FLOW_S;
    case ESOLLanePhase::PauseOpen:
        return LANESMOKE_PAUSE_OPEN_S;
    case ESOLLanePhase::PauseHold:
        return LANESMOKE_PAUSE_HOLD_S;
    case ESOLLanePhase::MapOpen:
        return LANESMOKE_MAP_S;
    default:
        return LANESMOKE_TAP_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one cost capture from the first frame it is called; returns true once it is done
bool FSOLNpcLaneSmoke::TickProfile(const TCHAR* label, UWorld* world)
{
    // stat dumpave averages the next 240 frames; ProfileGPU captures three frames inside that window. The numbers
    // are read from the log (the dumps print on the stats thread, the GPU profiles on the render thread)
    if (mProfileFrame == 0)
    {
        mProfileStartS = mTotalTimeS;
        FVector2D size(0.0, 0.0);
        if (world->GetGameViewport() != nullptr)
        {
            world->GetGameViewport()->GetViewportSize(size);
        }
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes PROFILE %s %s: start (lane component %s, viewport %.0fx%.0f)"),
            GetViewName(), label, mVisuals->IsLaneComponentVisible() ? TEXT("visible") : TEXT("hidden"), size.X,
            size.Y);
        LaneSmokeRunCommand(*FString::Printf(TEXT("stat dumpave -num=%d -ms=0 -root=%s"), LANESMOKE_PROFILE_FRAMES,
            LANESMOKE_PROFILE_AVE_ROOT), world);
    }
    for (const int32 gpuFrame : LANESMOKE_PROFILE_GPU_FRAMES)
    {
        if (mProfileFrame == gpuFrame)
        {
            UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes PROFILE %s %s: ProfileGPU at frame %d"), GetViewName(), label,
                mProfileFrame);
            LaneSmokeRunCommand(TEXT("ProfileGPU"), world);
        }
    }
    if (mProfileFrame == LANESMOKE_PROFILE_FRAMES + LANESMOKE_PROFILE_TAIL_FRAMES)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes PROFILE %s %s: done after %d frames, %.2f s (lane component %s). "
            "The game-thread cost is the LogStats block '240 frames, average' (STAT_SOL_NpcRedraw and its "
            "STAT_SOL_NpcLanes line), the GPU cost the LogRHI 'GPU Profile for Frame' blocks (the translucency "
            "passes); compare with the sol.NpcLanes 0 capture"), GetViewName(), label, mProfileFrame,
            mTotalTimeS - mProfileStartS, mVisuals->IsLaneComponentVisible() ? TEXT("visible") : TEXT("hidden"));
    }
    ++mProfileFrame;
    return mProfileFrame > LANESMOKE_PROFILE_FRAMES + LANESMOKE_PROFILE_TAIL_FRAMES;
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, restores the space dust and takes the closing screenshot (which quits)
void FSOLNpcLaneSmoke::Finish(ASOLShipPawn& pawn) const
{
    LaneSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 1);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes SUMMARY: complete after %.1f s, %d/%d checks run passed%s"),
        mTotalTimeS, mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("") : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves the ship to a position facing a direction, at the reference velocity, not rotating
void FSOLNpcLaneSmoke::SetupShip(const FVector3d& positionM, const FVector3d& forwardDir) const
{
    FSOLShipState state = mShips->GetState();
    state.PositionM = positionM;
    state.VelocityMps = mShips->GetReferenceVelocityMps();
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    if (!forwardDir.IsNearlyZero())
    {
        state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(forwardDir).ToQuat();
    }
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Places the ship for the current view (positions in the Unreal-handed universe frame; Unreal +Z is ecliptic north)
void FSOLNpcLaneSmoke::SetupView()
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLBodyFrameCache& cache = mRegistry->GetUnrealFrameCache();
    // Returns a body's Unreal-handed position by name (zero when it is unknown)
    const auto bodyM = [&registry, &cache](const TCHAR* name)
    {
        const int32 index = registry.FindByName(FName(name));
        return cache.PositionsM.IsValidIndex(index) ? cache.PositionsM[index] : FVector3d::ZeroVector;
    };
    const FVector3d sunM = bodyM(SOL::BodyNames::SUN);
    const FVector3d earthM = bodyM(SOL::BodyNames::EARTH);
    const FVector3d moonDir = (bodyM(SOL::BodyNames::MOON) - earthM).GetSafeNormal();
    FVector3d positionM = mStartPositionM;
    FVector3d forward = FVector3d::UnitX();
    switch (mView)
    {
    case 0:
        // V1: the start, 10,000 km above Earth, facing the Moon (Earth-Moon and Earth-Mars lanes leave Earth)
        forward = (bodyM(SOL::BodyNames::MOON) - positionM).GetSafeNormal();
        break;
    case 1:
    {
        // V2: 0.15 AU off Venus, outward and above, facing Venus (Mercury-Venus and Venus-Earth converge on it)
        const FVector3d venusM = bodyM(SOL::BodyNames::VENUS);
        const FVector3d outward = ((venusM - sunM).GetSafeNormal() + FVector3d::UnitZ() * 0.5).GetSafeNormal();
        positionM = venusM + outward * (LANESMOKE_VENUS_VIEW_AU * SOL::AU_M);
        forward = (venusM - positionM).GetSafeNormal();
        break;
    }
    case 2:
        // V3: 3 AU above the Sun, looking down (all 8 lanes)
        positionM = sunM + FVector3d::UnitZ() * (LANESMOKE_SUN_VIEW_AU * SOL::AU_M);
        forward = -FVector3d::UnitZ();
        break;
    case 3:
    case 4:
    {
        // V4: halfway along the Earth-Mars lane, inside it just off its centre line, looking toward Mars a little off
        // its axis (the lane ships around the camera as points, the line toward Mars beyond them). V4b: one lane
        // radius off the centre line looking 1.5 degrees off the axis, so the ribbon's near end crosses the camera
        // plane close to the camera (the near-plane smear regression view)
        const FVector3d marsM = bodyM(SOL::BodyNames::MARS);
        const FVector3d laneDir = (marsM - earthM).GetSafeNormal();
        const FVector3d across = FVector3d::CrossProduct(laneDir, FVector3d::UnitZ()).GetSafeNormal();
        const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
        const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
        const int32 mars = registry.FindByName(FName(SOL::BodyNames::MARS));
        double radiusM = 0.0;
        for (int32 lane = 0; lane < groups.LaneCount && groups.LaneRadiusM.IsValidIndex(lane); ++lane)
        {
            if (groups.LaneBodyA.IsValidIndex(lane) && groups.LaneBodyB.IsValidIndex(lane)
                && groups.LaneBodyA[lane] == earth && groups.LaneBodyB[lane] == mars)
            {
                radiusM = groups.LaneRadiusM[lane];
            }
        }
        const bool bNearAxis = mView == 4;
        const double offsetRadii = bNearAxis ? LANESMOKE_AXIS_OFFSET_RADII : LANESMOKE_LANE_OFFSET_RADII;
        const double yawDeg = bNearAxis ? LANESMOKE_AXIS_YAW_DEG : LANESMOKE_LANE_YAW_DEG;
        positionM = 0.5 * (earthM + marsM) + across * (offsetRadii * radiusM);
        forward = FQuat4d(FVector3d::UnitZ(), FMath::DegreesToRadians(yawDeg)).RotateVector(laneDir);
        break;
    }
    default:
    {
        // V5: beside Earth across the Earth-Moon lane, which leaves from behind Earth's disc (the occlusion)
        const FVector3d across = FVector3d::CrossProduct(moonDir, FVector3d::UnitZ()).GetSafeNormal();
        positionM = earthM + across * LANESMOKE_EARTH_SIDE_M;
        forward = (earthM + moonDir * LANESMOKE_EARTH_LOOK_M - positionM).GetSafeNormal();
        break;
    }
    }
    SetupShip(positionM, forward);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes: view %s at (%.4g, %.4g, %.4g) m facing (%.2f, %.2f, %.2f)"),
        GetViewName(), positionM.X, positionM.Y, positionM.Z, forward.X, forward.Y, forward.Z);
}

//////////////////////////////////////////////////////////////////////////
// Returns the current view's name (V1 .. V5, V4b) for the checks, logs and screenshot names
const TCHAR* FSOLNpcLaneSmoke::GetViewName() const
{
    return LANESMOKE_VIEW_NAMES[FMath::Clamp(mView, 0, LANESMOKE_VIEWS - 1)];
}

//////////////////////////////////////////////////////////////////////////
// Sets the pending HUD glow (never applied); nothing without a settings subsystem
void FSOLNpcLaneSmoke::SetPendingGlow(const float hudGlow) const
{
    if (mSettings.IsValid())
    {
        SOLSettingsModel::FSettingsValues values = mSettings->GetLiveValues();
        values.HudGlow = hudGlow;
        mSettings->SetPendingValues(values);
    }
}

//////////////////////////////////////////////////////////////////////////
// Checks the lane component, the points' crossfade flag and the pushed glow against the expected lanes state
void FSOLNpcLaneSmoke::CheckLaneState(const TCHAR* label, const bool bExpectShown, const float expectedHudGlow)
{
    float glow = -1.0f;
    FLinearColor pointParams = FLinearColor::Black;
    const bool bParams = mVisuals->GetPushedLaneParams(glow, pointParams);
    const bool bVisible = mVisuals->IsLaneComponentVisible();
    const bool bExpectVisible = SOLNpcLane::ShouldDrawLaneLines(bExpectShown, mVisuals->GetLaneFrame());
    const float expectedGlow = SOLHudShapeMath::HudGlowScale(mSettings.IsValid() ? expectedHudGlow
        : SOL::Settings::HUD_GLOW_DEFAULT);

    // The glow is pushed only while the lines draw, so it is checked only then
    const bool bGlowOk = !bExpectShown || FMath::Abs(glow - expectedGlow) <= LANESMOKE_GLOW_TOLERANCE;
    const float expectedFlag = bExpectShown ? 1.0f : 0.0f;
    Check(label, bParams && mVisuals->AreLanesShown() == bExpectShown && bVisible == bExpectVisible
        && pointParams.R == expectedFlag && bGlowOk, FString::Printf(TEXT("lanes shown %d (expected %d), component "
        "%s (expected %s), points crossfade %.0f, glow scale %.4f (expected %.4f)"), mVisuals->AreLanesShown() ? 1 : 0,
        bExpectShown ? 1 : 0, bVisible ? TEXT("visible") : TEXT("hidden"), bExpectVisible ? TEXT("visible")
        : TEXT("hidden"), pointParams.R, glow, expectedGlow));
}

//////////////////////////////////////////////////////////////////////////
// Checks every lane's group-texel .w values against a fresh histogram and BuildLaneFrame
void FSOLNpcLaneSmoke::CheckGroupTexels()
{
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    TArray<int32> counts;
    SOLNpcLane::CountLaneShips(mNpcs->GetRails(), groups.LaneCount, counts);
    FSOLNpcLaneFrame expected;
    SOLNpcLane::BuildLaneFrame(groups, mNpcs->GetRailFrame(), counts, expected);
    const TConstArrayView<FVector4f> texels = mVisuals->GetLastGroupTexels();
    const FSOLNpcLaneFrame& frame = mVisuals->GetLaneFrame();
    constexpr int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    bool bOk = texels.Num() >= 2 * width && frame.Lanes.Num() == expected.Lanes.Num() && expected.Lanes.Num() > 0;
    int32 drawn = 0;
    FString detail;
    for (int32 lane = 0; bOk && lane < expected.Lanes.Num(); ++lane)
    {
        const FSOLNpcLaneEntry& entry = expected.Lanes[lane];
        const float weight = entry.bDrawn ? static_cast<float>(entry.TrafficWeight) : -1.0f;
        bOk &= frame.Lanes[lane].ShipCount == entry.ShipCount && texels[lane].W == static_cast<float>(entry.RadiusM)
            && texels[width + lane].W == weight;
        drawn += entry.bDrawn ? 1 : 0;
        detail += FString::Printf(TEXT(" [%d: %d ships, R %.3g m, w %.3f]"), lane, entry.ShipCount, entry.RadiusM,
            weight);
    }
    Check(TEXT("c-texels-w"), bOk && drawn > 0, FString::Printf(TEXT("%d lanes, %d drawn:%s"), expected.Lanes.Num(),
        drawn, *detail));
}

//////////////////////////////////////////////////////////////////////////
// Logs each drawn lane's endpoints on screen and the outbound line's side there ("keep right", D2), so a screenshot
// can be checked by eye: the teal-white line must lie on that side of the cyan one
void FSOLNpcLaneSmoke::LogLaneScreenSides(const ASOLShipPawn& pawn) const
{
    const APlayerController* controller = pawn.GetController<APlayerController>();
    const USOLAnchorSubsystem* anchor = pawn.GetWorld()->GetSubsystem<USOLAnchorSubsystem>();
    if (controller == nullptr || controller->PlayerCameraManager == nullptr || anchor == nullptr)
    {
        return;
    }
    const FVector cameraCm = controller->PlayerCameraManager->GetCameraLocation();
    const FSOLNpcLaneFrame& frame = mVisuals->GetLaneFrame();
    for (int32 lane = 0; lane < frame.Lanes.Num(); ++lane)
    {
        // Each endpoint's direction from the camera (placement keeps it), projected a short way out
        const FSOLNpcLaneEntry& entry = frame.Lanes[lane];
        FVector2D screen[2];
        bool bOnScreen[2] = { false, false };
        const FVector3d endsM[2] = { entry.A, entry.B };
        for (int32 end = 0; end < 2; ++end)
        {
            const FVector direction = (anchor->UniverseToRenderCm(endsM[end]) - cameraCm).GetSafeNormal();
            bOnScreen[end] = controller->ProjectWorldLocationToScreen(cameraCm + direction * LANESMOKE_PROJECT_CM,
                screen[end]);
        }
        const FVector2D side = SOLNpcLane::LaneOutboundSideNormal(screen[0], screen[1]);
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes: %s lane %d drawn %d: A (%.0f, %.0f)%s -> B (%.0f, %.0f)%s, "
            "outbound side (%.2f, %.2f)"), GetViewName(), lane, entry.bDrawn ? 1 : 0, screen[0].X, screen[0].Y,
            bOnScreen[0] ? TEXT("") : TEXT(" off"), screen[1].X, screen[1].Y, bOnScreen[1] ? TEXT("") : TEXT(" off"),
            side.X, side.Y);
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot (no UI) named NpcLanes_<width>x<height>_<name>.png
void FSOLNpcLaneSmoke::TakeShot(const FString& name) const
{
    FVector2D size(0.0, 0.0);
    if (mVisuals.IsValid() && mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("NpcLanes_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), *name));
    FScreenshotRequest::RequestScreenshot(path, false, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes: screenshot %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLNpcLaneSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcLanes CHECK %-20s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
