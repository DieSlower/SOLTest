/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcPixelSmoke.h"

#include "Game/SOLGameMode.h"
#include "Interaction/SOLActionTable.h"
#include "Interaction/SOLInteractionSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Npc/SOLNpcPixelMath.h"
#include "Npc/SOLNpcPointMirror.h"
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
#include "Traffic/SOLLaneNetworkMath.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Visuals/SOLThrusterMath.h"
#include "Visuals/SOLThrusterRibbons.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    // Phase durations (real seconds) and frame counts
    constexpr double NPCPIXEL_WAIT_TIMEOUT_S = 240.0;        // Population spawn and upload (1M ships)
    constexpr double NPCPIXEL_SETTLE_S = 3.0;                // After the population, before the scan
    constexpr int32 NPCPIXEL_SCAN_FRAMES = 90;               // Frames scanned at the spawn view
    constexpr double NPCPIXEL_SWITCH_S = 0.5;                // A FOV lock or unlock settles (several redraws)
    constexpr double NPCPIXEL_MAP_S = 3.0;                   // The map camera settles
    constexpr double NPCPIXEL_RIDE_S = 2.0;                  // A ride settles (anchor, tiers, camera follow)
    constexpr double NPCPIXEL_SHOT_HOLD_S = 0.8;             // Real time for a requested screenshot to be written
    constexpr double NPCPIXEL_FILE_TIMEOUT_S = 6.0;          // The measure phases wait this long for the files
    constexpr double NPCPIXEL_RESPAWN_MIN_S = 5.0;           // The respawn starts (the old population is despawned)
    constexpr int32 NPCPIXEL_MIN_PHASE_FRAMES = 5;

    // Script values
    constexpr int32 NPCPIXEL_SHIPS = 1000000;                // Population (SDD 54 section 5: 1M NPCs)
    constexpr double NPCPIXEL_TIER_SLACK = 0.02;             // (a), (b): relative slack (a frame of motion, focal lag)
    constexpr double NPCPIXEL_RADIUS_TOLERANCE = 1.0e-3;     // (a): unbudgeted radius against TierRadiusM
    constexpr float NPCPIXEL_LOCKED_FOV_DEG = 1.0f;          // (c): FarLayerCanDraw needs a FOV under about 1.9 deg
    constexpr double NPCPIXEL_HIDDEN_VIEW_M = 40000.0;       // (e): the viewpoint this far outside the battle
    constexpr int32 NPCPIXEL_SPARSE_BATTLE_COUNT = 1;        // (d): one battle of 500 ships in a 20 km ball, so the
    constexpr int32 NPCPIXEL_SPARSE_BATTLE_SHIPS = 500;      // ridden ship's pixel has no other ship near it
    constexpr double NPCPIXEL_SPARSE_BATTLE_RADIUS_M = 20000.0;
    constexpr double NPCPIXEL_SHOT_PX[3] = { 0.8, 1.5, 3.0 };   // (d): the ridden ship's true sizes
    constexpr double NPCPIXEL_AIM_DROP = 0.35;               // The aim this far (x distance) below the target
    constexpr double NPCPIXEL_SUN_MIN_COS = 0.5;             // The line of sight at least 60 deg from the Sun
    constexpr int32 NPCPIXEL_SPOT_RADIUS_PX = 4;             // Measured window half size (the camera lags a frame)
    constexpr int32 NPCPIXEL_RING_INNER_PX = 8;              // Background ring
    constexpr int32 NPCPIXEL_RING_OUTER_PX = 14;
    constexpr double NPCPIXEL_DARK_LEVELS = 3.0;             // (d): "background" is within this many 8-bit levels
    constexpr double NPCPIXEL_LIT_LEVELS = 4.0;              // (d): "lit" is at least this many levels above it
    constexpr double NPCPIXEL_FADE_RATIO_MIN = 0.5;          // (d): the 1.5 px dot's summed excess over the 3 px
    constexpr double NPCPIXEL_FADE_RATIO_MAX = 2.3;          // one, in units of alpha x area (linear 1; the
                                                             // tonemapper compresses the brighter 3 px dot more):
                                                             // 1.93 measured at 1080p with TSR, 2.73 with the dot's
                                                             // alpha removed from the shader (negative control)
    constexpr double NPCPIXEL_MAP_LIT_LEVELS = 8.0;          // Q9: an NPC pixel is this much brighter with the NPCs
    constexpr int32 NPCPIXEL_MAP_MIN_PIXELS = 1000;           // Q9: the NPCs brighten at least this many pixels...
    constexpr int32 NPCPIXEL_MAP_MIN_SITES = 3;              // ...in at least this many separate places
    constexpr int32 NPCPIXEL_MAP_SMALL_SPAN_PX = 5;          // A lone dot spans under this many pixels (reported)
    constexpr float NPCPIXEL_MAX_WINDOW_PX = 16384.0f;       // Lifted window size limit (r.SetRes past the desktop)
    const TCHAR* const NPCPIXEL_COMBAT_CVAR = TEXT("sol.Npc.Combat");
    const TCHAR* const NPCPIXEL_AA_CVAR = TEXT("r.AntiAliasingMethod");
    const TCHAR* const NPCPIXEL_SCREEN_PERCENTAGE_CVAR = TEXT("r.ScreenPercentage");
    const TCHAR* const NPCPIXEL_KEEP_SHOTS = TEXT("SOLNpcPixelKeepShots");   // Debugging: the measured shots are kept
    const TCHAR* const NPCPIXEL_AA_OFF = TEXT("SOLNpcPixelAaOff");   // Debugging: measured shots with AA off (crisp)

    //////////////////////////////////////////////////////////////////////////
    // Deletes one of the smoke's own screenshots unless -SOLNpcPixelKeepShots is given
    void NpcPixelDeleteShot(const FString& path)
    {
        if (!path.IsEmpty() && !FParse::Param(FCommandLine::Get(), NPCPIXEL_KEEP_SHOTS))
        {
            IFileManager::Get().Delete(*path, false, true, true);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a console variable's value as text ("?" when it does not exist)
    FString NpcPixelCvarText(const TCHAR* name)
    {
        const IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name);
        return cvar != nullptr ? cvar->GetString() : FString(TEXT("?"));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the brightest 8-bit channel of a pixel
    double NpcPixelLevel(const FColor& color)
    {
        return static_cast<double>(FMath::Max3(color.R, color.G, color.B));
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets the -ResX / -ResY view past the desktop size with r.SetRes (as the thruster profile does); nothing without
    // both
    void NpcPixelApplyCommandLineView(UWorld* world)
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
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(NPCPIXEL_MAX_WINDOW_PX)
            .SetMaxHeight(NPCPIXEL_MAX_WINDOW_PX));
        GEngine->Exec(world, *FString::Printf(TEXT("r.SetRes %dx%dw"), width, height));
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
void FSOLNpcPixelSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const ASOLGameMode* gameMode = world->GetAuthGameMode<ASOLGameMode>();
    mPawn = &pawn;
    mVisuals = gameMode != nullptr ? gameMode->GetNpcVisuals() : nullptr;
    mOrbitLines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
    mRibbons = gameMode != nullptr ? gameMode->GetThrusterRibbons() : nullptr;
    mNpcs = world->GetSubsystem<USOLNpcSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcPixel: starting (NPC visuals %s, ribbons %s)"),
        mVisuals.IsValid() ? *mVisuals->GetName() : TEXT("none"), mRibbons.IsValid() ? TEXT("found") : TEXT("none"));

    // Overrides in memory only: lanes, tunnels and orbit lines detached from the player's settings and hidden, no
    // space dust; combat, plumes and flames on for the scan
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
    SetCvar(SOL::SpaceDust::CVAR_ENABLED, TEXT("0"));
    SetCvar(SOL::NPC_PLUME_CVAR, TEXT("1"));
    SetCvar(SOL::THRUSTER_RIBBONS_CVAR, TEXT("1"));
    const IConsoleVariable* aa = IConsoleManager::Get().FindConsoleVariable(NPCPIXEL_AA_CVAR);
    const IConsoleVariable* percentage = IConsoleManager::Get().FindConsoleVariable(NPCPIXEL_SCREEN_PERCENTAGE_CVAR);
    mSavedAa = aa != nullptr ? aa->GetString() : FString();
    mSavedScreenPercentage = percentage != nullptr ? percentage->GetString() : FString();
    if (mNpcs.IsValid() && mNpcs->GetRequestedCount() == 0)
    {
        mNpcs->RequestNpcCount(NPCPIXEL_SHIPS);
    }
    mPhase = ESOLPixelPhase::WaitPopulation;
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLNpcPixelSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLPixelPhase::Done)
    {
        return true;
    }
    if (!mVisuals.IsValid() || !mNpcs.IsValid() || !mShips.IsValid() || !mRegistry.IsValid() || !mAnchor.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeNpcPixel: NPC visuals or subsystems missing; aborting"));
        mPhase = ESOLPixelPhase::Done;
        Finish(pawn);
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    if (mTarget != INDEX_NONE)
    {
        PinRide();
    }
    if (mPhase == ESOLPixelPhase::SpawnScan)
    {
        ScanFrame();
    }
    if (mShotHoldS > 0.0)
    {
        mShotHoldS -= realDeltaSeconds;
        if (mShotHoldS > 0.0)
        {
            return false;
        }
    }

    // Phase end: the populations once ready, the measure phases once their files exist, the rest by time and frames
    const int32 minFrames = FMath::Max(NPCPIXEL_MIN_PHASE_FRAMES, mPhase == ESOLPixelPhase::SpawnScan
        ? NPCPIXEL_SCAN_FRAMES : 0);
    const bool bMinimum = mPhaseTimeS >= GetPhaseDuration(mPhase) && mPhaseFrames >= minFrames;
    if (!bMinimum)
    {
        return false;
    }
    if (mPhase == ESOLPixelPhase::WaitPopulation || mPhase == ESOLPixelPhase::Respawn)
    {
        const int32 wanted = FMath::Max(mNpcs->GetRequestedCount(), 1);
        bool bReady = mNpcs->GetRails().Num() >= wanted && mNpcs->GetGroups().BattleCount > 0;
        if (mPhase == ESOLPixelPhase::Respawn)
        {
            bReady = bReady && mNpcs->GetGroups().BattleCount == NPCPIXEL_SPARSE_BATTLE_COUNT
                && PickBattleShip(AwayFromEarth()) != INDEX_NONE;
        }
        if (!bReady && mPhaseTimeS < NPCPIXEL_WAIT_TIMEOUT_S)
        {
            return false;
        }
    }
    if (mPhase == ESOLPixelPhase::MapMeasure || mPhase == ESOLPixelPhase::ShotMeasure)
    {
        bool bWritten = true;
        if (mPhase == ESOLPixelPhase::MapMeasure)
        {
            bWritten = IFileManager::Get().FileSize(*mMapShot) > 0 && IFileManager::Get().FileSize(*mMapEmptyShot) > 0;
        }
        for (int32 shot = 0; shot < 3 && mPhase == ESOLPixelPhase::ShotMeasure; ++shot)
        {
            bWritten = bWritten && IFileManager::Get().FileSize(*mShots[shot].Path) > 0;
        }
        if (!bWritten && mPhaseTimeS < NPCPIXEL_FILE_TIMEOUT_S)
        {
            return false;
        }
    }

    // A shot phase ends with its screenshot, once written
    const bool bShotPhase = mPhase == ESOLPixelPhase::MapOpen || mPhase == ESOLPixelPhase::MapEmpty
        || mPhase == ESOLPixelPhase::ShotFar
        || mPhase == ESOLPixelPhase::ShotFade || mPhase == ESOLPixelPhase::ShotFull;
    if (bShotPhase && !mIsShotTaken)
    {
        if (mPhase == ESOLPixelPhase::MapOpen)
        {
            CheckFarLayer(TEXT("c-far-layer-map"), true);
            mMapShot = TakeShot(TEXT("map"));
        }
        else if (mPhase == ESOLPixelPhase::MapEmpty)
        {
            Check(TEXT("map-despawned"), mNpcs->GetRails().IsEmpty() && mNpcs->GetCpuShips().IsEmpty(),
                FString::Printf(TEXT("%d rails, %d CPU ships left for the map shot without NPCs"),
                mNpcs->GetRails().Num(), mNpcs->GetCpuShips().Num()));
            mMapEmptyShot = TakeShot(TEXT("map_empty"));
        }
        else
        {
            const int32 shot = static_cast<int32>(mPhase) - static_cast<int32>(ESOLPixelPhase::ShotFar);
            FShot& record = mShots[shot];
            const int32 cpu = FindCpuShip(mTarget);
            if (cpu != INDEX_NONE)
            {
                const FSOLNpcCpuShip& ship = mNpcs->GetCpuShips()[cpu];
                record.SizePx = SOLNpcPixel::ShipSizePx(SOL::NPC_HULL_BOUNDING_DIAMETER_M,
                    (ship.PositionM - mAnchor->GetViewpointM()).Size(), LiveFocalRefPx());
                record.Alpha = SOLNpcPointMirror::DotAlpha(static_cast<float>(record.SizePx),
                    static_cast<float>(SOL::NPC_SHIP_HIDE_PX), static_cast<float>(SOL::NPC_SHIP_DOT_FULL_PX));
                record.bProjected = ProjectFromPawnCamera(ship.PositionM, record.ScreenPx);
                record.RenderTier = ship.RenderTier;
            }
            record.Path = TakeShot(*FString::Printf(TEXT("ride_%.1fpx"), NPCPIXEL_SHOT_PX[shot]));
            UE_LOG(LogSOL, Log, TEXT("SmokeNpcPixel: ride shot %d: true size %.3f px, alpha %.3f, render tier %d, "
                "screen (%.1f, %.1f)%s"), shot, record.SizePx, record.Alpha, record.RenderTier, record.ScreenPx.X,
                record.ScreenPx.Y, record.bProjected ? TEXT("") : TEXT(" (not projected)"));
        }
        mIsShotTaken = true;
        mShotHoldS = NPCPIXEL_SHOT_HOLD_S;
        return false;
    }

    ExitPhase(mPhase);
    mPhase = static_cast<ESOLPixelPhase>(static_cast<uint8>(mPhase) + 1);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    mIsShotTaken = false;
    if (mPhase == ESOLPixelPhase::Done)
    {
        Finish(pawn);
        return true;
    }
    EnterPhase(mPhase);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase in seconds
double FSOLNpcPixelSmoke::GetPhaseDuration(const ESOLPixelPhase phase)
{
    switch (phase)
    {
    case ESOLPixelPhase::WaitPopulation:
        return NPCPIXEL_SETTLE_S;
    case ESOLPixelPhase::FovLocked:
    case ESOLPixelPhase::FovUnlocked:
    case ESOLPixelPhase::MapClosed:
        return NPCPIXEL_SWITCH_S;
    case ESOLPixelPhase::MapOpen:
    case ESOLPixelPhase::MapEmpty:
        return NPCPIXEL_MAP_S;
    case ESOLPixelPhase::HiddenSetup:
    case ESOLPixelPhase::ShotFar:
    case ESOLPixelPhase::ShotFade:
    case ESOLPixelPhase::ShotFull:
        return NPCPIXEL_RIDE_S;
    case ESOLPixelPhase::Respawn:
        return NPCPIXEL_RESPAWN_MIN_S;
    default:
        return 0.0;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions
void FSOLNpcPixelSmoke::EnterPhase(const ESOLPixelPhase phase)
{
    APlayerController* controller = mVisuals->GetWorld()->GetFirstPlayerController();
    switch (phase)
    {
    case ESOLPixelPhase::FovLocked:
        if (controller != nullptr && controller->PlayerCameraManager != nullptr)
        {
            controller->PlayerCameraManager->SetFOV(NPCPIXEL_LOCKED_FOV_DEG);
        }
        break;
    case ESOLPixelPhase::FovUnlocked:
        if (controller != nullptr && controller->PlayerCameraManager != nullptr)
        {
            controller->PlayerCameraManager->UnlockFOV();
        }
        break;
    case ESOLPixelPhase::MapOpen:
        mTarget = INDEX_NONE;
        if (mPawn.IsValid())
        {
            mPawn->HandleOpenMap(false);
        }
        break;
    case ESOLPixelPhase::MapEmpty:
        mNpcs->RequestNpcCount(0);                          // The same view without NPCs: the pair's difference
        break;
    case ESOLPixelPhase::HiddenSetup:
    {
        // Outside the battle on its empty side, looking back at it: the nearest ships are all under 1 px
        SetCvar(NPCPIXEL_COMBAT_CVAR, TEXT("0"));
        const FVector3d away = AwayFromEarth();
        const int32 target = PickBattleShip(away);
        if (target == INDEX_NONE)
        {
            Check(TEXT("e-setup"), false, TEXT("no live battle ship found"));
            break;
        }
        StartRide(target, NPCPIXEL_HIDDEN_VIEW_M, away);
        break;
    }
    case ESOLPixelPhase::Respawn:
        // One sparse battle (a respawn: the options are session-only, never saved), combat, plumes and flames off
        mTarget = INDEX_NONE;
        // The measured shots use the game's AA (TSR) as the player sees it; crisp pixels only on request
        if (FParse::Param(FCommandLine::Get(), NPCPIXEL_AA_OFF))
        {
            SetCvar(NPCPIXEL_AA_CVAR, TEXT("0"));
            SetCvar(NPCPIXEL_SCREEN_PERCENTAGE_CVAR, TEXT("100"));
        }
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcPixel: measured shots with AA method %s, screen percentage %s"),
            *NpcPixelCvarText(NPCPIXEL_AA_CVAR), *NpcPixelCvarText(NPCPIXEL_SCREEN_PERCENTAGE_CVAR));
        SetCvar(SOL::NPC_PLUME_CVAR, TEXT("0"));
        SetCvar(SOL::THRUSTER_RIBBONS_CVAR, TEXT("0"));
        mNpcs->SetOptionValue(SOLNpcScale::EDemoOption::BattleCount, NPCPIXEL_SPARSE_BATTLE_COUNT);
        mNpcs->SetOptionValue(SOLNpcScale::EDemoOption::BattleShipCount, NPCPIXEL_SPARSE_BATTLE_SHIPS);
        mNpcs->SetOptionValue(SOLNpcScale::EDemoOption::BattleRadiusM, NPCPIXEL_SPARSE_BATTLE_RADIUS_M);
        break;
    case ESOLPixelPhase::ShotFar:
    {
        // Looking away from Earth at the battle ship farthest that way: nothing but space behind it
        mLineOfSight = AwayFromEarth();
        const int32 target = PickBattleShip(mLineOfSight);
        if (target == INDEX_NONE)
        {
            Check(TEXT("d-setup"), false, TEXT("no live battle ship after the respawn"));
            break;
        }
        StartRide(target, SOLNpcPixel::PixelRadiusM(SOL::NPC_HULL_BOUNDING_DIAMETER_M, LiveFocalRefPx(),
            NPCPIXEL_SHOT_PX[0]), -mLineOfSight);
        break;
    }
    case ESOLPixelPhase::ShotFade:
    case ESOLPixelPhase::ShotFull:
    {
        const int32 shot = static_cast<int32>(phase) - static_cast<int32>(ESOLPixelPhase::ShotFar);
        mRideDistanceM = SOLNpcPixel::PixelRadiusM(SOL::NPC_HULL_BOUNDING_DIAMETER_M, LiveFocalRefPx(),
            NPCPIXEL_SHOT_PX[shot]);
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's end checks
void FSOLNpcPixelSmoke::ExitPhase(const ESOLPixelPhase phase)
{
    switch (phase)
    {
    case ESOLPixelPhase::WaitPopulation:
        Check(TEXT("population"), mNpcs->GetRails().Num() >= FMath::Max(mNpcs->GetRequestedCount(), 1),
            FString::Printf(TEXT("%d rails after %.0f s"), mNpcs->GetRails().Num(), mPhaseTimeS));
        NpcPixelApplyCommandLineView(mVisuals->GetWorld());
        break;
    case ESOLPixelPhase::SpawnScan:
    {
        const FSOLNpcStats& stats = mNpcs->GetStats();
        Check(TEXT("a-tier-by-px"), mTierChecked > 0 && mTierWrong == 0, FString::Printf(TEXT("%d ship-frames over %d "
            "frames checked against the budgeted pixel radii (mesh %.0f m, hull %.0f m), %d wrong, %d in a hysteresis "
            "band (either tier)%s%s"), mTierChecked, mScanFrames, stats.MeshRadiusM, stats.InstancedRadiusM,
            mTierWrong, mTierBand, mTierWorst.IsEmpty() ? TEXT("") : TEXT("; first: "), *mTierWorst));
        Check(TEXT("a-radius-from-px"), mRadiusChecked > 0 && mRadiusWrong == 0, FString::Printf(TEXT("%d tier-frames "
            "with a free pool: radius equals TierRadiusM of the threshold x quality scale %.2f (worst relative error "
            "%.2e, %d off); pool bound (a reported limit, skipped): mesh %d, hull %d frames"), mRadiusChecked,
            QualityScale(), mRadiusWorstRel, mRadiusWrong, mMeshBound, mHullBound));
        Check(TEXT("b-no-small-hull"), mHullShips > 0 && mHullTooSmall == 0, FString::Printf(TEXT("%d mesh / instanced "
            "ship-frames (%d mesh), %d under the hull threshold %.2f px x %.2f / (1 + h), smallest %.2f px; %d dot "
            "ship-frames"), mHullShips, mMeshShips, mHullTooSmall, mNpcs->GetOptions().HullMinPx, QualityScale(),
            mHullMinPx, mDotShips));
        Check(TEXT("c-far-layer-flight"), mFarChecked > 0 && mFarWrong == 0, FString::Printf(TEXT("%d frames: the far "
            "layer shown exactly per FarLayerShown and dark in flight (Mid radius %.0f m), %d mismatched"), mFarChecked,
            stats.MidRadiusM, mFarWrong));
        Check(TEXT("f-dot-flame"), mBurningDotShips.Num() > 0 && mDrawnDotShips.Num() > 0, FString::Printf(TEXT("%d "
            "dot ship-frames with thrust and flame data (%d ships), %d with a drawn flame (%d ships)"), mBurningDots,
            mBurningDotShips.Num(), mBurningDotsDrawn, mDrawnDotShips.Num()));
        break;
    }
    case ESOLPixelPhase::FovLocked:
        CheckFarLayer(TEXT("c-far-layer-narrow-fov"), true);
        break;
    case ESOLPixelPhase::FovUnlocked:
        CheckFarLayer(TEXT("c-far-layer-fov-back"), false);
        break;
    case ESOLPixelPhase::MapMeasure:
        MeasureMapShots();
        if (mPawn.IsValid())
        {
            mPawn->HandleCloseMap();
        }
        break;
    case ESOLPixelPhase::MapClosed:
        // With the population despawned the Mid radius is 0, so the rule shows the far layer (nothing to draw)
        CheckFarLayer(TEXT("c-far-layer-map-closed"), SOLNpcPixel::FarLayerShown(false, LiveFocalRefPx(),
            mNpcs->GetStats().MidRadiusM));
        break;
    case ESOLPixelPhase::HiddenSetup:
        CheckHiddenSelectable();
        break;
    case ESOLPixelPhase::Respawn:
        Check(TEXT("respawn"), mNpcs->GetGroups().BattleCount == NPCPIXEL_SPARSE_BATTLE_COUNT
            && PickBattleShip(AwayFromEarth()) != INDEX_NONE, FString::Printf(TEXT("%d rails, %d battles after %.0f s"),
            mNpcs->GetRails().Num(), mNpcs->GetGroups().BattleCount, mPhaseTimeS));
        break;
    case ESOLPixelPhase::ShotMeasure:
        MeasureRideShots();
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, restores the cvars and the camera, and takes the closing screenshot (which quits)
void FSOLNpcPixelSmoke::Finish(ASOLShipPawn& pawn)
{
    SetCvar(SOL::SpaceDust::CVAR_ENABLED, TEXT("1"));
    SetCvar(NPCPIXEL_COMBAT_CVAR, TEXT("1"));
    SetCvar(SOL::NPC_PLUME_CVAR, TEXT("1"));
    SetCvar(SOL::THRUSTER_RIBBONS_CVAR, TEXT("1"));
    if (!mSavedAa.IsEmpty())
    {
        SetCvar(NPCPIXEL_AA_CVAR, *mSavedAa);
    }
    if (!mSavedScreenPercentage.IsEmpty())
    {
        SetCvar(NPCPIXEL_SCREEN_PERCENTAGE_CVAR, *mSavedScreenPercentage);
    }
    APlayerController* controller = pawn.GetWorld()->GetFirstPlayerController();
    if (controller != nullptr && controller->PlayerCameraManager != nullptr)
    {
        controller->PlayerCameraManager->UnlockFOV();
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcPixel SUMMARY: complete after %.1f s, %d/%d checks run passed%s"), mTotalTimeS,
        mChecksPassed, mChecksRun, mChecksPassed == mChecksRun && mChecksRun > 0 ? TEXT("") : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the camera manager's horizontal FOV in degrees (the locked one while locked)
double FSOLNpcPixelSmoke::LiveFovDeg() const
{
    const APlayerController* controller = mVisuals->GetWorld()->GetFirstPlayerController();
    return controller != nullptr && controller->PlayerCameraManager != nullptr
        ? controller->PlayerCameraManager->GetFOVAngle() : SOL::SHIP_CAMERA_BASE_FOV_DEG;
}

//////////////////////////////////////////////////////////////////////////
// Returns the live focal length in reference px (the camera manager's FOV, the viewport aspect)
double FSOLNpcPixelSmoke::LiveFocalRefPx() const
{
    const double fovDeg = LiveFovDeg();
    FVector2D size(SOL::NPC_REFERENCE_VIEWPORT_WIDTH_PX, SOL::NPC_LANE_REFERENCE_HEIGHT_PX);
    if (mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        FVector2D viewport;
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(viewport);
        if (viewport.X > 0.0 && viewport.Y > 0.0)
        {
            size = viewport;
        }
    }
    return SOLThruster::FocalRefPx(fovDeg, size.X / size.Y);
}

//////////////////////////////////////////////////////////////////////////
// Returns the render tier threshold scale of the saved quality preset (SDD 54 Q12)
double FSOLNpcPixelSmoke::QualityScale() const
{
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(mVisuals.Get());
    return SOLLaneNetwork::LaneTunnelQualityScale(settings != nullptr ? settings->GetSavedValues().QualityPreset
        : SOL::NPC_TUNNEL_QUALITY_REFERENCE_PRESET);
}

//////////////////////////////////////////////////////////////////////////
// (a), (b), (c), (f) for one frame
void FSOLNpcPixelSmoke::ScanFrame()
{
    using SOLNpcScale::ERenderTier;
    const FSOLNpcStats& stats = mNpcs->GetStats();
    const SOLNpcScale::FDemoOptions& options = mNpcs->GetOptions();
    const double focalRefPx = LiveFocalRefPx();
    const double quality = QualityScale();
    const double keep = 1.0 + options.RenderHysteresis;
    const FVector3d viewpointM = mAnchor->GetViewpointM();
    ++mScanFrames;

    // (a) the radii: without a binding pool each equals the pixel threshold's distance (capped by the Mid radius)
    const double radii[2] = { stats.MeshRadiusM, stats.InstancedRadiusM };
    const double thresholds[2] = { options.MeshMinPx, options.HullMinPx };
    const bool bBound[2] = { stats.bMeshBudgetBound, stats.bInstancedBudgetBound };
    for (int32 tier = 0; tier < 2; ++tier)
    {
        if (bBound[tier])
        {
            (tier == 0 ? mMeshBound : mHullBound) += 1;
            continue;
        }
        const double expectedM = SOLNpcPixel::TierRadiusM(SOL::NPC_HULL_BOUNDING_DIAMETER_M, focalRefPx,
            thresholds[tier], quality, stats.MidRadiusM);
        const double relative = FMath::Abs(radii[tier] - expectedM) / FMath::Max(expectedM, 1.0);
        mRadiusWorstRel = FMath::Max(mRadiusWorstRel, relative);
        ++mRadiusChecked;
        mRadiusWrong += relative > NPCPIXEL_RADIUS_TOLERANCE ? 1 : 0;
    }

    // (a) each ship's tier against the radii (the hysteresis band takes either tier), (b) no small hull, (f) flames
    TSet<int32> drawnTrails;
    if (mRibbons.IsValid())
    {
        TArray<FSOLRibbonDrawnTrail> trails;
        mRibbons->GetDrawnNpcTrails(trails);
        for (const FSOLRibbonDrawnTrail& trail : trails)
        {
            drawnTrails.Add(trail.Key);
        }
    }
    const double smallestHullPx = options.HullMinPx * quality / keep * (1.0 - NPCPIXEL_TIER_SLACK);
    for (const FSOLNpcCpuShip& ship : mNpcs->GetCpuShips())
    {
        if (ship.bHidden != 0)
        {
            continue;
        }
        const double distanceM = (ship.PositionM - viewpointM).Size();
        const double sizePx = SOLNpcPixel::ShipSizePx(SOL::NPC_HULL_BOUNDING_DIAMETER_M, distanceM, focalRefPx);
        const ERenderTier tier = static_cast<ERenderTier>(ship.RenderTier);
        bool bWrong = false;
        bool bBand = false;
        for (int32 radiusTier = 0; radiusTier < 2; ++radiusTier)
        {
            // Inside R: this tier or nearer; beyond R (1 + h): farther; in between, either
            const bool bNearer = static_cast<int32>(tier) <= radiusTier;
            if (distanceM < radii[radiusTier] * (1.0 - NPCPIXEL_TIER_SLACK))
            {
                bWrong = bWrong || !bNearer;
            }
            else if (distanceM > radii[radiusTier] * keep * (1.0 + NPCPIXEL_TIER_SLACK))
            {
                bWrong = bWrong || bNearer;
            }
            else
            {
                bBand = true;
            }
        }
        ++mTierChecked;
        mTierBand += bBand ? 1 : 0;
        if (bWrong)
        {
            ++mTierWrong;
            if (mTierWorst.IsEmpty())
            {
                mTierWorst = FString::Printf(TEXT("NPC %d tier %d at %.0f m (%.2f px)"), ship.NpcIndex,
                    ship.RenderTier, distanceM, sizePx);
            }
        }
        if (tier == ERenderTier::Point)
        {
            ++mDotShips;
            if (ship.bFlameData != 0 && !ship.ThrustLocalG.IsNearlyZero())
            {
                ++mBurningDots;
                mBurningDotShips.Add(ship.NpcIndex);
                if (drawnTrails.Contains(ship.NpcIndex))
                {
                    ++mBurningDotsDrawn;
                    mDrawnDotShips.Add(ship.NpcIndex);
                }
            }
            continue;
        }
        ++mHullShips;
        mMeshShips += tier == ERenderTier::Mesh ? 1 : 0;
        mHullMinPx = FMath::Min(mHullMinPx, sizePx);
        mHullTooSmall += sizePx < smallestHullPx ? 1 : 0;
    }

    // (c) the far layer in flight
    const bool bShown = mVisuals->IsFarLayerShown();
    ++mFarChecked;
    mFarWrong += bShown != SOLNpcPixel::FarLayerShown(false, focalRefPx, stats.MidRadiusM) || bShown ? 1 : 0;
}

//////////////////////////////////////////////////////////////////////////
// (c): the far layer's flag against SOLNpcPixel::FarLayerShown now and the expectation
void FSOLNpcPixelSmoke::CheckFarLayer(const TCHAR* label, const bool bExpected)
{
    const double focalRefPx = LiveFocalRefPx();
    const double midRadiusM = mNpcs->GetStats().MidRadiusM;
    const bool bMap = mPawn.IsValid() && mPawn->IsMapOpen();
    const bool bRule = SOLNpcPixel::FarLayerShown(bMap, focalRefPx, midRadiusM);
    const bool bShown = mVisuals->IsFarLayerShown();
    Check(label, bShown == bExpected && bRule == bExpected, FString::Printf(TEXT("far layer %s, FarLayerShown %s, "
        "expected %s (map %s, focal %.1f ref px, Mid radius %.0f m, 1 px distance %.0f m)"), bShown ? TEXT("on")
        : TEXT("off"), bRule ? TEXT("on") : TEXT("off"), bExpected ? TEXT("on") : TEXT("off"), bMap ? TEXT("open")
        : TEXT("closed"), focalRefPx, midRadiusM, SOLNpcPixel::PixelRadiusM(SOL::NPC_HULL_BOUNDING_DIAMETER_M,
        focalRefPx, SOL::NPC_SHIP_HIDE_PX)));
}

//////////////////////////////////////////////////////////////////////////
// Picks the live battle CPU ship farthest along a direction; INDEX_NONE if none
int32 FSOLNpcPixelSmoke::PickBattleShip(const FVector3d& direction) const
{
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const uint8 battle = static_cast<uint8>(SOLNpcScale::EPlacementKind::Battle);
    int32 best = INDEX_NONE;
    double bestAlong = -TNumericLimits<double>::Max();
    for (const FSOLNpcCpuShip& ship : mNpcs->GetCpuShips())
    {
        if (ship.bHidden != 0 || !rails.IsValidIndex(ship.NpcIndex) || rails[ship.NpcIndex].Kind != battle)
        {
            continue;
        }
        const double along = ship.PositionM | direction;
        if (along > bestAlong)
        {
            bestAlong = along;
            best = ship.NpcIndex;
        }
    }
    return best;
}

//////////////////////////////////////////////////////////////////////////
// Returns the CPU ship index of an NPC this frame, or INDEX_NONE
int32 FSOLNpcPixelSmoke::FindCpuShip(const int32 npcIndex) const
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
// Starts riding with an NPC at a distance along an offset direction (the camera looking back at it)
void FSOLNpcPixelSmoke::StartRide(const int32 npcIndex, const double distanceM, const FVector3d& offsetDir)
{
    mTarget = npcIndex;
    mRideDistanceM = distanceM;
    mRideOffsetDir = offsetDir.GetSafeNormal();
    const FVector3d pole = FMath::Abs(mRideOffsetDir.Z) < 0.9 ? FVector3d::UnitZ() : FVector3d::UnitX();
    mRideUpDir = (pole - mRideOffsetDir * (pole | mRideOffsetDir)).GetSafeNormal();
    mHasTargetPrev = false;
    PinRide();
}

//////////////////////////////////////////////////////////////////////////
// Pins the ship at the ride's offset from the target, at the target's velocity (its last step), facing it with the
// target above the player hull
void FSOLNpcPixelSmoke::PinRide()
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
    const FVector3d shipM = targetM + mRideOffsetDir * mRideDistanceM;
    const FVector3d toTarget = SOLRender::EclipticToUnreal(targetM - shipM);
    const FVector3d cameraUp = SOLRender::EclipticToUnreal(mRideUpDir);
    const FVector3d aim = toTarget - cameraUp * (NPCPIXEL_AIM_DROP * mRideDistanceM);
    FSOLShipState state = mShips->GetState();
    state.PositionM = SOLRender::EclipticToUnreal(shipM);
    state.VelocityMps = velocityUnrealMps;
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromXZ(aim, cameraUp).ToQuat();
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Returns the unit direction away from Earth at the viewpoint, turned at least 60 deg off the Sun
FVector3d FSOLNpcPixelSmoke::AwayFromEarth() const
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
    const int32 sun = registry.FindByName(FName(SOL::BodyNames::SUN));
    const FVector3d viewpointM = mAnchor->GetViewpointM();
    FVector3d away = earth != INDEX_NONE ? (viewpointM - registry.GetPositionM(earth)).GetSafeNormal()
        : FVector3d::UnitZ();
    if (sun != INDEX_NONE)
    {
        const FVector3d toSun = (registry.GetPositionM(sun) - viewpointM).GetSafeNormal();
        if ((away | toSun) > NPCPIXEL_SUN_MIN_COS)
        {
            away = (away - toSun * (away | toSun)).GetSafeNormal();
        }
    }
    return away.IsNearlyZero() ? FVector3d::UnitZ() : away;
}

//////////////////////////////////////////////////////////////////////////
// (e): the nearest ships under 1 px keep bHidden 0, resolve through the interaction subsystem and get overlay icons
void FSOLNpcPixelSmoke::CheckHiddenSelectable()
{
    const TConstArrayView<FSOLNpcCpuShip> ships = mNpcs->GetCpuShips();
    const FVector3d viewpointM = mAnchor->GetViewpointM();
    const double focalRefPx = LiveFocalRefPx();
    int32 nearest = INDEX_NONE;
    double nearestM = TNumericLimits<double>::Max();
    for (int32 index = 0; index < ships.Num(); ++index)
    {
        const double distanceM = (ships[index].PositionM - viewpointM).Size();
        if (distanceM < nearestM)
        {
            nearestM = distanceM;
            nearest = index;
        }
    }
    const double nearestPx = SOLNpcPixel::ShipSizePx(SOL::NPC_HULL_BOUNDING_DIAMETER_M, nearestM, focalRefPx);
    Check(TEXT("e-all-subpixel"), nearest != INDEX_NONE && nearestPx < SOL::NPC_SHIP_HIDE_PX,
        FString::Printf(TEXT("nearest CPU ship %.0f m away, %.3f px (a dot drawn only above %.1f px)"), nearestM,
        nearestPx, SOL::NPC_SHIP_HIDE_PX));
    if (nearest == INDEX_NONE)
    {
        return;
    }
    const FSOLNpcCpuShip& ship = ships[nearest];
    const USOLInteractionSubsystem* interaction = mVisuals->GetWorld()->GetSubsystem<USOLInteractionSubsystem>();
    FSOLObjectRef object;
    object.Kind = ESOLObjectKind::Ship;
    object.Index = ship.NpcIndex;
    FSOLObjectInfo info;
    FSOLActionList actions;
    const bool bResolved = interaction != nullptr && interaction->GetObjectInfo(object, info);
    const bool bActions = interaction != nullptr && interaction->BuildActions(object, actions);
    Check(TEXT("e-selectable"), ship.bHidden == 0 && bResolved && bActions, FString::Printf(TEXT("NPC %d (render tier "
        "%d): bHidden %d, object info %s, actions %s"), ship.NpcIndex, ship.RenderTier, ship.bHidden,
        bResolved ? TEXT("resolved") : TEXT("missing"), bActions ? TEXT("built") : TEXT("none")));

    // The overlay icons the HUD chose last frame (nearest first): sub-pixel ships among them
    const APlayerController* controller = mVisuals->GetWorld()->GetFirstPlayerController();
    const ASOLFlightHud* hud = controller != nullptr ? Cast<ASOLFlightHud>(controller->GetHUD()) : nullptr;
    // When the player's overlay settings show neither ship icons nor ships on the radar the HUD selects none; the same
    // selection is then run on a local icon layer (it reads only bHidden and the distance, never the pixel rule)
    FSOLOverlayIconLayer localLayer;
    const bool bHudSelected = hud != nullptr && !hud->GetIconLayer().GetSelectedShips().IsEmpty();
    if (!bHudSelected)
    {
        FSOLOverlayIconFrame frame;
        frame.Ships = ships;
        frame.ViewpointM = viewpointM;
        localLayer.SelectShips(frame);
    }
    const FSOLOverlayIconLayer& layer = bHudSelected ? hud->GetIconLayer() : localLayer;
    int32 icons = 0;
    int32 subPixelIcons = 0;
    bool bNearestIcon = false;
    {
        for (const int32 slot : layer.GetSelectedShips())
        {
            if (!ships.IsValidIndex(slot))
            {
                continue;
            }
            ++icons;
            subPixelIcons += SOLNpcPixel::ShipSizePx(SOL::NPC_HULL_BOUNDING_DIAMETER_M, (ships[slot].PositionM
                - viewpointM).Size(), focalRefPx) < SOL::NPC_SHIP_HIDE_PX ? 1 : 0;
            bNearestIcon = bNearestIcon || slot == nearest;
        }
    }
    Check(TEXT("e-icon"), subPixelIcons > 0 && bNearestIcon, FString::Printf(TEXT("%s: %d ship icons, %d of them on "
        "ships under 1 px, nearest ship %s"), bHudSelected ? TEXT("the HUD's selection") : TEXT("a local icon layer "
        "(the player's overlays hide ship icons and radar ships, so the HUD selected none)"), icons, subPixelIcons,
        bNearestIcon ? TEXT("has one") : TEXT("has none")));
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot without the HUD named NpcPixel_<width>x<height>_<name>.png; returns its path
FString FSOLNpcPixelSmoke::TakeShot(const TCHAR* name) const
{
    FVector2D size(0.0, 0.0);
    if (mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("NpcPixel_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), name));
    IFileManager::Get().Delete(*path, false, true, true);  // A stale file of an earlier run would pass the wait
    FScreenshotRequest::RequestScreenshot(path, false, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcPixel: screenshot %s"), *path);
    return path;
}

//////////////////////////////////////////////////////////////////////////
// Projects a universe position through the pawn's own camera component from the viewpoint-relative geometry (steady
// while riding: the camera manager's cached view is a frame old and the ride moves tens of km/s); false off screen
bool FSOLNpcPixelSmoke::ProjectFromPawnCamera(const FVector3d& positionM, FVector2D& outScreenPx) const
{
    const UCameraComponent* camera = mPawn.IsValid() ? mPawn->FindComponentByClass<UCameraComponent>() : nullptr;
    FVector2D size(0.0, 0.0);
    mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(size);
    if (camera == nullptr || !(size.X > 0.0) || !(size.Y > 0.0))
    {
        return false;
    }
    const FVector cameraFromPawnCm = camera->GetComponentLocation() - mPawn->GetActorLocation();
    const FVector toTargetCm = FVector(SOLRender::EclipticToUnreal(positionM - mAnchor->GetViewpointM())
        * SOL::METERS_TO_CM) - cameraFromPawnCm;
    const FVector local = camera->GetComponentRotation().UnrotateVector(toTargetCm);
    if (!(local.X > 0.0))
    {
        return false;
    }
    const double halfWidthPx = 0.5 * size.X;
    const double scale = halfWidthPx / FMath::Tan(FMath::DegreesToRadians(0.5 * LiveFovDeg()));
    outScreenPx = FVector2D(halfWidthPx + scale * local.Y / local.X, 0.5 * size.Y - scale * local.Z / local.X);
    const double margin = NPCPIXEL_RING_OUTER_PX + 2.0;
    return outScreenPx.X > margin && outScreenPx.Y > margin && outScreenPx.X < size.X - margin
        && outScreenPx.Y < size.Y - margin;
}

//////////////////////////////////////////////////////////////////////////
// Loads a screenshot as 8-bit BGRA; false if missing or unreadable
bool FSOLNpcPixelSmoke::LoadShot(const FString& path, FImage& outImage)
{
    if (path.IsEmpty() || !FImageUtils::LoadImage(*path, outImage))
    {
        return false;
    }
    outImage.ChangeFormat(ERawImageFormat::BGRA8, EGammaSpace::sRGB);
    return outImage.SizeX > 0 && outImage.SizeY > 0;
}

//////////////////////////////////////////////////////////////////////////
// Measures the brightest channel around a pixel: the window's peak, the median of a ring (the background), the summed
// excess over it and the pixels above half the peak's excess; false if the ring leaves the image
bool FSOLNpcPixelSmoke::MeasureSpot(const FImage& image, const FVector2D& centerPx, const int32 radius,
    double& outPeak, double& outBackground, double& outExcess, int32& outLitPixels)
{
    const int32 cx = FMath::FloorToInt32(centerPx.X);
    const int32 cy = FMath::FloorToInt32(centerPx.Y);
    if (cx - NPCPIXEL_RING_OUTER_PX < 0 || cy - NPCPIXEL_RING_OUTER_PX < 0 || cx + NPCPIXEL_RING_OUTER_PX
        >= image.SizeX || cy + NPCPIXEL_RING_OUTER_PX >= image.SizeY)
    {
        return false;
    }
    const TArrayView64<const FColor> pixels = const_cast<FImage&>(image).AsBGRA8();
    const auto level = [&pixels, &image](const int32 x, const int32 y)
    {
        return NpcPixelLevel(pixels[static_cast<int64>(y) * image.SizeX + x]);
    };
    TArray<double> ring;
    for (int32 y = -NPCPIXEL_RING_OUTER_PX; y <= NPCPIXEL_RING_OUTER_PX; ++y)
    {
        for (int32 x = -NPCPIXEL_RING_OUTER_PX; x <= NPCPIXEL_RING_OUTER_PX; ++x)
        {
            const int32 chebyshev = FMath::Max(FMath::Abs(x), FMath::Abs(y));
            if (chebyshev >= NPCPIXEL_RING_INNER_PX)
            {
                ring.Add(level(cx + x, cy + y));
            }
        }
    }
    ring.Sort();
    outBackground = ring[ring.Num() / 2];
    outPeak = 0.0;
    outExcess = 0.0;
    for (int32 y = -radius; y <= radius; ++y)
    {
        for (int32 x = -radius; x <= radius; ++x)
        {
            const double value = level(cx + x, cy + y);
            outPeak = FMath::Max(outPeak, value);
            outExcess += FMath::Max(value - outBackground, 0.0);
        }
    }
    outLitPixels = 0;
    const double half = outBackground + 0.5 * (outPeak - outBackground);
    for (int32 y = -radius; y <= radius; ++y)
    {
        for (int32 x = -radius; x <= radius; ++x)
        {
            outLitPixels += outPeak - outBackground >= 1.0 && level(cx + x, cy + y) >= half ? 1 : 0;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Q9 and SDD 54 4.8: the NPC dots on the jump map. The map screenshot is compared with the same view after the
// population was despawned; the pixels that lost brightness are the NPC dots. The small, isolated ones (outside the
// crowded inner lanes) are measured: a 2 device px dot covers a few pixels. Both files are then deleted
void FSOLNpcPixelSmoke::MeasureMapShots()
{
    FImage withNpcs;
    FImage without;
    const bool bLoaded = LoadShot(mMapShot, withNpcs) && LoadShot(mMapEmptyShot, without)
        && withNpcs.SizeX == without.SizeX && withNpcs.SizeY == without.SizeY;
    const int32 width = bLoaded ? static_cast<int32>(withNpcs.SizeX) : 0;
    const int32 height = bLoaded ? static_cast<int32>(withNpcs.SizeY) : 0;
    TArray<uint8> npc;
    npc.SetNumZeroed(width * height);
    int32 npcPixels = 0;
    if (bLoaded)
    {
        const TArrayView64<FColor> a = withNpcs.AsBGRA8();
        const TArrayView64<FColor> c = without.AsBGRA8();
        for (int32 index = 0; index < width * height; ++index)
        {
            const bool bDot = NpcPixelLevel(a[index]) - NpcPixelLevel(c[index]) >= NPCPIXEL_MAP_LIT_LEVELS;
            npc[index] = bDot ? 1 : 0;
            npcPixels += bDot ? 1 : 0;
        }
    }

    // 8-connected components of the NPC pixels: count, and the pixel count of the small isolated ones
    int32 components = 0;
    TArray<int32> smallSizes;
    TArray<int32> stack;
    for (int32 seed = 0; seed < width * height; ++seed)
    {
        if (npc[seed] != 1)
        {
            continue;
        }
        ++components;
        int32 size = 0;
        int32 minX = seed % width;
        int32 maxX = minX;
        int32 minY = seed / width;
        int32 maxY = minY;
        npc[seed] = 2;
        stack.Reset();
        stack.Add(seed);
        while (!stack.IsEmpty())
        {
            const int32 index = stack.Pop(EAllowShrinking::No);
            const int32 x = index % width;
            const int32 y = index / width;
            ++size;
            minX = FMath::Min(minX, x);
            maxX = FMath::Max(maxX, x);
            minY = FMath::Min(minY, y);
            maxY = FMath::Max(maxY, y);
            for (int32 dy = -1; dy <= 1; ++dy)
            {
                for (int32 dx = -1; dx <= 1; ++dx)
                {
                    const int32 nx = x + dx;
                    const int32 ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < width && ny < height && npc[ny * width + nx] == 1)
                    {
                        npc[ny * width + nx] = 2;
                        stack.Add(ny * width + nx);
                    }
                }
            }
        }
        if (maxX - minX < NPCPIXEL_MAP_SMALL_SPAN_PX && maxY - minY < NPCPIXEL_MAP_SMALL_SPAN_PX)
        {
            smallSizes.Add(size);
        }
    }
    smallSizes.Sort();
    const int32 median = smallSizes.IsEmpty() ? 0 : smallSizes[smallSizes.Num() / 2];
    // At map scale 1M dots overlap into solid crowds (the inner system, the outer planets' lanes and clusters), so the
    // check is that they draw at all; the dot's 2 device px size is the shader's map branch (SOLTest.NpcPointDot.*)
    Check(TEXT("q9-map-dots"), bLoaded && npcPixels >= NPCPIXEL_MAP_MIN_PIXELS && components >= NPCPIXEL_MAP_MIN_SITES,
        FString::Printf(TEXT("map pair %dx%d: %d pixels at least %.0f levels brighter with the NPCs in %d separate "
        "places (dots and crowds); %d lone dots (under %d px across), median %d lit pixels"), width, height, npcPixels,
        NPCPIXEL_MAP_LIT_LEVELS, components, smallSizes.Num(), NPCPIXEL_MAP_SMALL_SPAN_PX, median));
    NpcPixelDeleteShot(mMapShot);
    NpcPixelDeleteShot(mMapEmptyShot);
}

//////////////////////////////////////////////////////////////////////////
// (d): the three ride screenshots against the mirror's alpha (0, 0.5, 1); the files are then deleted
void FSOLNpcPixelSmoke::MeasureRideShots()
{
    double peak[3] = {};
    double background[3] = {};
    double excess[3] = {};
    int32 pixels[3] = {};
    bool bMeasured[3] = {};
    FString sizes;
    for (int32 shot = 0; shot < 3; ++shot)
    {
        FImage image;
        bMeasured[shot] = mShots[shot].bProjected && LoadShot(mShots[shot].Path, image)
            && MeasureSpot(image, mShots[shot].ScreenPx, NPCPIXEL_SPOT_RADIUS_PX, peak[shot], background[shot],
                excess[shot], pixels[shot]);
        sizes += FString::Printf(TEXT("%s%dx%d"), shot > 0 ? TEXT(", ") : TEXT(""), static_cast<int32>(image.SizeX),
            static_cast<int32>(image.SizeY));
        UE_LOG(LogSOL, Log, TEXT("SmokeNpcPixel: ride shot %d (%.3f px, alpha %.3f, tier %d): peak %.0f, background "
            "%.0f, excess %.1f, %d px above half%s"), shot, mShots[shot].SizePx, mShots[shot].Alpha,
            mShots[shot].RenderTier, peak[shot], background[shot], excess[shot], pixels[shot],
            bMeasured[shot] ? TEXT("") : TEXT(" (not measured)"));
    }
    const bool bDots = mShots[0].RenderTier == static_cast<uint8>(SOLNpcScale::ERenderTier::Point)
        && mShots[1].RenderTier == mShots[0].RenderTier && mShots[2].RenderTier == mShots[0].RenderTier;
    const bool bAlphas = mShots[0].Alpha == 0.0 && mShots[1].Alpha > 0.0 && mShots[1].Alpha < 1.0
        && mShots[2].Alpha == 1.0;
    Check(TEXT("d-setup-dots"), bMeasured[0] && bMeasured[1] && bMeasured[2] && bDots && bAlphas,
        FString::Printf(TEXT("shots %s, all dot tier %s, mirror alphas %.3f / %.3f / %.3f at %.3f / %.3f / %.3f px"),
        *sizes, bDots ? TEXT("yes") : TEXT("no"), mShots[0].Alpha, mShots[1].Alpha, mShots[2].Alpha, mShots[0].SizePx,
        mShots[1].SizePx, mShots[2].SizePx));
    const double rise[3] = { peak[0] - background[0], peak[1] - background[1], peak[2] - background[2] };
    Check(TEXT("d-gpu-hidden-0.8px"), bMeasured[0] && rise[0] <= NPCPIXEL_DARK_LEVELS, FString::Printf(TEXT("peak %.0f "
        "over a background of %.0f (at most %.0f levels: nothing drawn)"), peak[0], background[0],
        NPCPIXEL_DARK_LEVELS));
    // The faded dot's summed excess over the full one's against the mirror's alpha x area (about 0.125): a dot drawn
    // without its alpha (Shade.y x dotAlpha missing) is about twice as bright and fails the upper bound
    const double fadeRatio = excess[2] > 0.0 ? excess[1] / excess[2] : 0.0;
    const double alphaArea = mShots[2].Alpha > 0.0 && mShots[2].SizePx > 0.0 ? mShots[1].Alpha / mShots[2].Alpha
        * FMath::Square(mShots[1].SizePx / mShots[2].SizePx) : 0.0;
    const double fadeUnits = alphaArea > 0.0 ? fadeRatio / alphaArea : 0.0;
    Check(TEXT("d-gpu-fading-1.5px"), bMeasured[1] && rise[1] >= NPCPIXEL_LIT_LEVELS && rise[1] < rise[2] + 1.0
        && fadeUnits >= NPCPIXEL_FADE_RATIO_MIN && fadeUnits <= NPCPIXEL_FADE_RATIO_MAX, FString::Printf(TEXT("peak "
        "%.0f levels over the background (3 px: %.0f), excess %.1f against %.1f (ratio %.3f against alpha x area "
        "%.3f: %.2f of it, allowed %.2f - %.2f)"), rise[1], rise[2], excess[1], excess[2], fadeRatio, alphaArea,
        fadeUnits, NPCPIXEL_FADE_RATIO_MIN, NPCPIXEL_FADE_RATIO_MAX));
    Check(TEXT("d-gpu-full-3px"), bMeasured[2] && rise[2] >= 2.0 * NPCPIXEL_LIT_LEVELS, FString::Printf(TEXT("peak "
        "%.0f levels over the background, %d px above half the peak (the positive control)"), rise[2], pixels[2]));
    for (const FShot& shot : mShots)
    {
        NpcPixelDeleteShot(shot.Path);
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets a console variable from a string, logging a name that does not exist
void FSOLNpcPixelSmoke::SetCvar(const TCHAR* name, const TCHAR* value)
{
    if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
    {
        cvar->Set(value, ECVF_SetByCode);
        return;
    }
    UE_LOG(LogSOL, Warning, TEXT("SmokeNpcPixel: console variable %s not found"), name);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLNpcPixelSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcPixel %s: %s - %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), label, *detail);
}
