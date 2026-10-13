/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Game/SOLGameMode.h"

#include "Combat/SOLCombatAudio.h"
#include "Combat/SOLCombatSubsystem.h"
#include "Combat/SOLCombatVisuals.h"
#include "GasGiants/SOLGasGiantVisuals.h"
#include "Game/SOLSpectatorPawn.h"
#include "Menu/SOLMenuCameraPawn.h"
#include "Menu/SOLMenuSubsystem.h"
#include "MinorBodies/SOLAsteroidBeltVisuals.h"
#include "MinorBodies/SOLPlanetRing.h"
#include "MinorBodies/SOLRingVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "StarField/SOLStarField.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"
#include "Visuals/SOLBodyVisuals.h"

#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "GameMapsSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
    constexpr float SMOKE_CONSOLE_SETTLE_SECONDS = 2.0f;                        // Time for console stats (stat unit/gpu) to fill
    constexpr float SMOKE_QUIT_DELAY_SECONDS = 3.0f;                            // Time for the screenshot to be written
    const TCHAR* const SMOKE_SCREENSHOT_COMMAND = TEXT("HighResShot 1280x720"); // Written to Saved/Screenshots
    constexpr float COMBAT_DEMO_START_DELAY_SECONDS = 8.0f;                     // -SOLCombatDemo: first drop once flight
                                                                                // assist has brought the spawned ship
                                                                                // (in orbit) nearly to rest
    constexpr float COMBAT_DEMO_STEP_SECONDS = 0.1f;                            // -SOLCombatDemo: script poll interval
    constexpr double COMBAT_DEMO_DROP_SETTLE_SECONDS = 0.3;                     // Time for a requested drop to land
    constexpr double COMBAT_DEMO_REST_SECONDS = 2.0;                            // Pause after a kill before the next drop
    constexpr double COMBAT_DEMO_FIRE_SHOT_SECONDS = 1.2;                       // First cycle: screenshot this far into fire
    constexpr double COMBAT_DEMO_KILL_SHOT_SECONDS = 0.2;                       // ...and these far after the kill
    constexpr double COMBAT_DEMO_DEBRIS_SHOT_SECONDS = 1.0;
    constexpr double COMBAT_DEMO_FIRE_TIMEOUT_SECONDS = 20.0;                   // Gives up on a target not killed by then
    constexpr double COMBAT_DEMO_HOLD_SHOT_SECONDS = 0.1;                      // -SOLCombatDemoHold: screenshot interval
    const TCHAR* const COMBAT_DEMO_HOLD_SHOT_NAME = TEXT("CombatHold");         // ...as presented frames (TSR and motion
                                                                                // blur included), unique-suffixed
    constexpr double COMBAT_STRESS_CONE_HALF_ANGLE_DEG = 25.0;                  // -SOLCombatStress: spread of the bolts
    constexpr float STAR_SMOKE_START_DELAY_SECONDS = 10.0f;                     // -SOLStarSmoke: first FOV once loaded
    constexpr float STAR_SMOKE_STEP_SECONDS = 1.5f;                             // ...then one FOV or shot per step
}

//////////////////////////////////////////////////////////////////////////
// Sets the default pawn and HUD classes
ASOLGameMode::ASOLGameMode()
{
    DefaultPawnClass = ASOLShipPawn::StaticClass();
    HUDClass = ASOLFlightHud::StaticClass();
}

//////////////////////////////////////////////////////////////////////////
// Returns the menu camera while the main menu shows, else the ship pawn (the debug spectator with -SOLSpectator)
UClass* ASOLGameMode::GetDefaultPawnClassForController_Implementation(AController* controller)
{
    // The main menu (SDD 8) has the menu camera; the ship pawn waits for Play
    const USOLMenuSubsystem* menu = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USOLMenuSubsystem>() : nullptr;
    if (menu != nullptr && menu->GetState().Mode == SOLMenuState::EMenuMode::MainMenu)
    {
        return ASOLMenuCameraPawn::StaticClass();
    }
    if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SPECTATOR))
    {
        return ASOLSpectatorPawn::StaticClass();
    }
    return Super::GetDefaultPawnClassForController_Implementation(controller);
}

//////////////////////////////////////////////////////////////////////////
// Spawns the body, star-field, asteroid-belt, per-ring, per-gas-giant and combat visuals and the combat audio, arms
// the optional smoke-test hooks, then starts play
void ASOLGameMode::StartPlay()
{
    FActorSpawnParameters params;
    params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ASOLBodyVisuals* bodyVisuals = GetWorld()->SpawnActor<ASOLBodyVisuals>(ASOLBodyVisuals::StaticClass(),
        FTransform::Identity, params);
    GetWorld()->SpawnActor<ASOLStarField>(ASOLStarField::StaticClass(), FTransform::Identity, params);
    GetWorld()->SpawnActor<ASOLAsteroidBeltVisuals>(ASOLAsteroidBeltVisuals::StaticClass(), FTransform::Identity,
        params);
    GetWorld()->SpawnActor<ASOLCombatVisuals>(ASOLCombatVisuals::StaticClass(), FTransform::Identity, params);
    GetWorld()->SpawnActor<ASOLCombatAudio>(ASOLCombatAudio::StaticClass(), FTransform::Identity, params);

    // One ASOLRingVisuals per ringed planet (SDD 6 Amendment 11, 5e-iii); deferred so PlanetName is set before
    // BeginPlay resolves the ring definition and planet index
    for (const FSOLPlanetRingDef& ringDef : SOLPlanetRing::RealRings())
    {
        ASOLRingVisuals* ringVisuals = GetWorld()->SpawnActorDeferred<ASOLRingVisuals>(ASOLRingVisuals::StaticClass(),
            FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (ringVisuals != nullptr)
        {
            ringVisuals->PlanetName = ringDef.PlanetName;
            ringVisuals->FinishSpawning(FTransform::Identity);
        }
    }

    // One ASOLGasGiantVisuals per giant (SDD 14, 14c); deferred so PlanetName and BodyVisuals are set before BeginPlay
    for (const TCHAR* giantName : ASOLGasGiantVisuals::GetGasGiantBodyNames())
    {
        ASOLGasGiantVisuals* giantVisuals = GetWorld()->SpawnActorDeferred<ASOLGasGiantVisuals>(
            ASOLGasGiantVisuals::StaticClass(), FTransform::Identity, nullptr, nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (giantVisuals != nullptr)
        {
            giantVisuals->PlanetName = FName(giantName);
            giantVisuals->BodyVisuals = bodyVisuals;
            giantVisuals->FinishSpawning(FTransform::Identity);
        }
    }

    // Verification hook for headless smoke runs: screenshot after the given delay, then quit
    float smokeDelaySeconds = 0.0f;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_SHOT, smokeDelaySeconds) && smokeDelaySeconds > 0.0f)
    {
        GetWorldTimerManager().SetTimer(mSmokeTimer, this, &ASOLGameMode::TakeSmokeScreenshot, smokeDelaySeconds);
    }

    // Verification hook for animation under time warp (SDD 14): step the warp up this many times at start
    int32 smokeWarpSteps = 0;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_WARP_STEPS, smokeWarpSteps))
    {
        if (USOLSimClockSubsystem* simClock = GetWorld()->GetSubsystem<USOLSimClockSubsystem>())
        {
            for (int32 step = 0; step < smokeWarpSteps; ++step)
            {
                simClock->StepWarpUp();
            }
        }
    }

    // Verification hook for the star field (SDD 13): the same view at several FOVs, one screenshot each
    FString starSmokeFovs;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::STAR_SMOKE, starSmokeFovs, false))
    {
        TArray<FString> fovs;
        starSmokeFovs.ParseIntoArray(fovs, TEXT(","));
        for (const FString& fov : fovs)
        {
            mStarSmokeFovs.Add(FCString::Atof(*fov));
        }
        GetWorldTimerManager().SetTimer(mStarSmokeTimer, this, &ASOLGameMode::StepStarSmoke, STAR_SMOKE_STEP_SECONDS,
            true, STAR_SMOKE_START_DELAY_SECONDS);
    }

    // Verification hook for the combat visuals: drop a target ahead of the ship and shoot it until it dies, repeatedly
    if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::COMBAT_DEMO))
    {
        FParse::Value(FCommandLine::Get(), SOL::CommandLine::COMBAT_STRESS, mCombatStressBolts);
        FParse::Value(FCommandLine::Get(), SOL::CommandLine::COMBAT_DEMO_HOLD, mCombatDemoHoldSeconds);
        mCombatDemoHoldSeconds = FMath::Max(mCombatDemoHoldSeconds, 0.0);
        mCombatStressBolts = FMath::Clamp(mCombatStressBolts, 0, SOL::COMBAT_MAX_BOLTS);
        GetWorldTimerManager().SetTimer(mCombatDemoTimer, this, &ASOLGameMode::StepCombatDemo, COMBAT_DEMO_STEP_SECONDS,
            true, COMBAT_DEMO_START_DELAY_SECONDS);
    }

    Super::StartPlay();
}

//////////////////////////////////////////////////////////////////////////
// Returns true for a Game or PIE world whose game mode (world override, else project default) is an ASOLGameMode
bool ASOLGameMode::IsSOLGameWorld(const UWorld* world)
{
    if (world == nullptr || (world->WorldType != EWorldType::Game && world->WorldType != EWorldType::PIE))
    {
        return false;
    }

    // The map's own override wins; otherwise fall back to the project's global default game mode
    const AWorldSettings* worldSettings = world->GetWorldSettings(false, false);
    const UClass* gameModeClass = worldSettings != nullptr ? worldSettings->DefaultGameMode.Get() : nullptr;
    if (gameModeClass == nullptr)
    {
        gameModeClass = FSoftClassPath(UGameMapsSettings::GetGlobalDefaultGameMode()).TryLoadClass<AGameModeBase>();
    }
    return gameModeClass != nullptr && gameModeClass->IsChildOf(StaticClass());
}

//////////////////////////////////////////////////////////////////////////
// Clears the smoke-test timers
void ASOLGameMode::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    GetWorldTimerManager().ClearTimer(mSmokeTimer);
    GetWorldTimerManager().ClearTimer(mCombatDemoTimer);
    GetWorldTimerManager().ClearTimer(mStarSmokeTimer);
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Takes the smoke-test screenshot and schedules the quit (verification runs only)
void ASOLGameMode::TakeSmokeScreenshot()
{
    // Optional console commands (e.g. "ProfileGPU|memreport -full") run just before the screenshot, so a verification
    // run can capture GPU timings and memory in exactly the scene the screenshot shows
    FString smokeCommands;
    if (!mSmokeConsoleRun && FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_CONSOLE, smokeCommands, false))
    {
        mSmokeConsoleRun = true;
        TArray<FString> commands;
        smokeCommands.ParseIntoArray(commands, TEXT("|"));
        if (APlayerController* playerController = GetWorld()->GetFirstPlayerController())
        {
            for (const FString& command : commands)
            {
                playerController->ConsoleCommand(command);
            }
        }
        // Let on-screen stats settle for a couple of seconds, then come back for the screenshot
        GetWorldTimerManager().SetTimer(mSmokeTimer, this, &ASOLGameMode::TakeSmokeScreenshot, SMOKE_CONSOLE_SETTLE_SECONDS);
        return;
    }
    CaptureScreenshot();
    GetWorldTimerManager().SetTimer(mSmokeTimer, this, &ASOLGameMode::QuitAfterSmokeTest, SMOKE_QUIT_DELAY_SECONDS);
}

//////////////////////////////////////////////////////////////////////////
// Takes a verification screenshot (Saved/Screenshots) without quitting
void ASOLGameMode::CaptureScreenshot()
{
    // HighResShot is handled by the game viewport, so route it through the local player's console
    if (APlayerController* playerController = GetWorld()->GetFirstPlayerController())
    {
        playerController->ConsoleCommand(SMOKE_SCREENSHOT_COMMAND);
    }
}

//////////////////////////////////////////////////////////////////////////
// -SOLStarSmoke: sets the next FOV of the list, or screenshots the one set last step; quits after the last shot
void ASOLGameMode::StepStarSmoke()
{
    APlayerController* playerController = GetWorld()->GetFirstPlayerController();
    const int32 fovIndex = mStarSmokeStep / 2;
    if (playerController == nullptr || fovIndex >= mStarSmokeFovs.Num())
    {
        // Its own timer quits, so a pending -SOLSmokeShot screenshot on mSmokeTimer is left alone
        GetWorldTimerManager().SetTimer(mStarSmokeTimer, this, &ASOLGameMode::QuitAfterSmokeTest,
            SMOKE_QUIT_DELAY_SECONDS);
        return;
    }

    // The mouse must not turn the view between shots, so every FOV shows the same stars. SetIgnoreLookInput counts
    // calls, so lock once; only pawns that read look input through the controller obey it (-SOLSpectator does, the
    // ship pawn's Enhanced Input look does not)
    if (!mStarSmokeLookLocked)
    {
        playerController->SetIgnoreLookInput(true);
        mStarSmokeLookLocked = true;
    }
    if (mStarSmokeStep % 2 == 0)
    {
        playerController->ConsoleCommand(FString::Printf(TEXT("fov %g"), mStarSmokeFovs[fovIndex]));
    }
    else
    {
        UE_LOG(LogSOL, Log, TEXT("StarSmoke: screenshot at FOV %g"), mStarSmokeFovs[fovIndex]);
        CaptureScreenshot();
    }
    ++mStarSmokeStep;
}

//////////////////////////////////////////////////////////////////////////
// Quits the game after the smoke-test screenshot
void ASOLGameMode::QuitAfterSmokeTest()
{
    UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false);
}

//////////////////////////////////////////////////////////////////////////
// Advances the -SOLCombatDemo script: drop a target, hold the trigger until it is destroyed, rest, repeat; the first
// cycle takes screenshots while firing, just after the kill and of the debris
void ASOLGameMode::StepCombatDemo()
{
    ASOLShipPawn* pawn = Cast<ASOLShipPawn>(GetWorld()->GetFirstPlayerController() != nullptr
        ? GetWorld()->GetFirstPlayerController()->GetPawn() : nullptr);
    const USOLCombatSubsystem* combat = GetWorld()->GetSubsystem<USOLCombatSubsystem>();
    if (pawn == nullptr || combat == nullptr)
    {
        return;
    }
    StepCombatStress();

    const double nowSeconds = GetWorld()->GetTimeSeconds();
    const double phaseSeconds = nowSeconds - mCombatDemoPhaseStartSeconds;
    switch (mCombatDemoPhase)
    {
    case ECombatDemoPhase::Drop:
        // Remember the counters, so the fire phase can find this drop's slot and tell a kill from an eviction
        mCombatDemoDropsBefore = combat->GetTargetDropCount();
        mCombatDemoKillsBefore = combat->GetTargetsDestroyedCount();
        mCombatDemoSlot = INDEX_NONE;
        pawn->HandleDropTarget();
        mCombatDemoPhase = ECombatDemoPhase::Fire;
        mCombatDemoPhaseStartSeconds = nowSeconds;
        UE_LOG(LogSOL, Log, TEXT("CombatDemo: target drop requested"));
        break;
    case ECombatDemoPhase::Fire:
    {
        if (phaseSeconds < COMBAT_DEMO_DROP_SETTLE_SECONDS)
        {
            break;
        }

        // This cycle's own target, once its drop has landed
        if (mCombatDemoSlot == INDEX_NONE && combat->GetTargetDropCount() > mCombatDemoDropsBefore)
        {
            mCombatDemoSlot = combat->GetLastDroppedTargetSlot();
            UE_LOG(LogSOL, Log, TEXT("CombatDemo: target dropped in slot %d"), mCombatDemoSlot);
        }
        const TConstArrayView<bool> active = combat->GetTargetActive();
        const bool bTargetLeft = active.IsValidIndex(mCombatDemoSlot) && active[mCombatDemoSlot];

        // Hold the trigger while it lives; release it when it is gone or the demo gives up on it
        if (mCombatDemoSlot != INDEX_NONE && !bTargetLeft)
        {
            const bool bKilled = combat->GetTargetsDestroyedCount() > mCombatDemoKillsBefore;
            EndCombatDemoFire(pawn, nowSeconds, phaseSeconds, bKilled ? TEXT("destroyed") : TEXT("lost (not a kill)"));
            break;
        }
        // -SOLCombatDemoHold: the first target is left unshot for a while, with a presented-frame screenshot every
        // COMBAT_DEMO_HOLD_SHOT_SECONDS (how a freshly dropped target looks, issue #15); for the second half the ship
        // strafes right, so the target also moves across the screen
        const double holdSeconds = phaseSeconds - COMBAT_DEMO_DROP_SETTLE_SECONDS;
        if (mCombatDemoCycle == 0 && mCombatDemoHoldSeconds > 0.0)
        {
            StepCombatDemoHold(pawn, combat, holdSeconds);
            if (mCombatDemoHoldSeconds > 0.0)
            {
                break;
            }
        }
        if (phaseSeconds >= COMBAT_DEMO_FIRE_TIMEOUT_SECONDS)
        {
            EndCombatDemoFire(pawn, nowSeconds, phaseSeconds,
                mCombatDemoSlot == INDEX_NONE ? TEXT("never dropped (timed out)") : TEXT("still alive (timed out)"));
            break;
        }
        pawn->HandleFire(bTargetLeft);
        if (mCombatDemoCycle == 0 && mCombatDemoShots == 0 && phaseSeconds >= COMBAT_DEMO_FIRE_SHOT_SECONDS)
        {
            CaptureScreenshot();
            ++mCombatDemoShots;
        }
        break;
    }
    case ECombatDemoPhase::Rest:
        // First cycle: screenshots of the explosion and of the debris cloud
        if (mCombatDemoCycle == 0 && ((mCombatDemoShots == 1 && phaseSeconds >= COMBAT_DEMO_KILL_SHOT_SECONDS)
            || (mCombatDemoShots == 2 && phaseSeconds >= COMBAT_DEMO_DEBRIS_SHOT_SECONDS)))
        {
            CaptureScreenshot();
            ++mCombatDemoShots;
        }
        if (phaseSeconds >= COMBAT_DEMO_REST_SECONDS)
        {
            mCombatDemoPhase = ECombatDemoPhase::Drop;
            ++mCombatDemoCycle;
        }
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// -SOLCombatDemoHold: screenshots and strafes while the first target is held unshot, then puts the ship back where it
// was relative to the target, at rest in the reference frame, and ends the hold so the fire phase aims true
void ASOLGameMode::StepCombatDemoHold(ASOLShipPawn* pawn, const USOLCombatSubsystem* combat, const double holdSeconds)
{
    USOLShipSubsystem* ships = GetWorld()->GetSubsystem<USOLShipSubsystem>();
    const TConstArrayView<FVector3d> targetsM = combat->GetTargetPositionsM();
    const bool bHasTarget = targetsM.IsValidIndex(mCombatDemoSlot);

    // The ship's state and the target's position before the strafe, once the drop has landed
    if (!mHasCombatDemoHoldStart && bHasTarget && ships != nullptr)
    {
        mCombatDemoHoldShipPositionM = ships->GetState().PositionM;
        mCombatDemoHoldTargetM = targetsM[mCombatDemoSlot];
        mHasCombatDemoHoldStart = true;
    }

    if (holdSeconds < mCombatDemoHoldSeconds)
    {
        if (holdSeconds >= mCombatDemoHoldShots * COMBAT_DEMO_HOLD_SHOT_SECONDS)
        {
            FScreenshotRequest::RequestScreenshot(COMBAT_DEMO_HOLD_SHOT_NAME, false, true);
            ++mCombatDemoHoldShots;
        }
        pawn->HandleThrust(holdSeconds >= SOL::COMBAT_DEMO_HOLD_STRAFE_START_FRACTION * mCombatDemoHoldSeconds
            ? SOL::COMBAT_DEMO_HOLD_STRAFE_THRUST : FVector3d::ZeroVector);
        return;
    }

    // Hold over: stop strafing and cancel the drift (the frame's own motion since then is the target's displacement;
    // ship state is Unreal-handed, combat positions are ecliptic). The zero thrust also goes straight to the ship: the
    // pawn only hands its input over on its next tick, and one long frame of stale strafe would drift it again
    pawn->HandleThrust(FVector3d::ZeroVector);
    double driftMps = 0.0;
    if (mHasCombatDemoHoldStart && bHasTarget && ships != nullptr)
    {
        FSOLShipControl control = ships->GetControl();
        control.Thrust = FVector3d::ZeroVector;
        ships->SetControl(control);
        FSOLShipState state = ships->GetState();
        driftMps = (state.VelocityMps - ships->GetReferenceVelocityMps()).Size();
        state.PositionM = mCombatDemoHoldShipPositionM
            + SOLRender::EclipticToUnreal(targetsM[mCombatDemoSlot] - mCombatDemoHoldTargetM);
        state.VelocityMps = ships->GetReferenceVelocityMps();
        ships->SetState(state);
    }
    mCombatDemoHoldSeconds = 0.0;
    UE_LOG(LogSOL, Log, TEXT("CombatDemo: hold over after %d frame screenshots; ship put back in front of the target, "
        "at rest in the frame (was drifting at %.1f m/s)"), mCombatDemoHoldShots, driftMps);
}

//////////////////////////////////////////////////////////////////////////
// Ends the demo's fire phase: releases the trigger, logs the outcome and starts the rest
void ASOLGameMode::EndCombatDemoFire(ASOLShipPawn* pawn, const double nowSeconds, const double phaseSeconds,
    const TCHAR* outcome)
{
    pawn->HandleFire(false);
    mCombatDemoPhase = ECombatDemoPhase::Rest;
    mCombatDemoPhaseStartSeconds = nowSeconds;
    UE_LOG(LogSOL, Log, TEXT("CombatDemo: target %s after %.2f s of fire"), outcome, phaseSeconds);
}

//////////////////////////////////////////////////////////////////////////
// -SOLCombatStress: tops the live bolts up toward the requested count, spread over one bolt lifetime
void ASOLGameMode::StepCombatStress()
{
    USOLCombatSubsystem* combat = GetWorld()->GetSubsystem<USOLCombatSubsystem>();
    const USOLShipSubsystem* ships = GetWorld()->GetSubsystem<USOLShipSubsystem>();
    if (mCombatStressBolts <= 0 || combat == nullptr || ships == nullptr || !ships->HasPlayerShip())
    {
        return;
    }

    // At most one lifetime's share per step, so the population ramps up once and then holds steady
    const int32 perStep = FMath::CeilToInt32(mCombatStressBolts * COMBAT_DEMO_STEP_SECONDS / SOL::COMBAT_BOLT_LIFETIME_S);
    const int32 toFire = FMath::Min(perStep, mCombatStressBolts - combat->GetBoltCount());

    // Ship pose in the ecliptic frame (its state is Unreal-handed; the conversion is its own inverse)
    const FSOLShipState& state = ships->GetState();
    const FVector3d shipPositionM = ships->GetUniversePositionM();
    const FVector3d shipVelocityMps = SOLRender::EclipticToUnreal(state.VelocityMps);
    const FVector3d forward = SOLRender::EclipticToUnreal(state.Orientation.GetForwardVector());
    const float halfAngleRad = FMath::DegreesToRadians(static_cast<float>(COMBAT_STRESS_CONE_HALF_ANGLE_DEG));
    for (int32 bolt = 0; bolt < toFire; ++bolt)
    {
        const FVector3d direction(mCombatStressRandom.VRandCone(FVector(forward), halfAngleRad));
        combat->FireBolt(shipPositionM + direction * SOL::COMBAT_GUN_MUZZLE_OFFSET_M.X,
            shipVelocityMps + direction * SOL::COMBAT_MUZZLE_SPEED_MPS, SOLCombat::PLAYER_OWNER);
    }
}

//////////////////////////////////////////////////////////////////////////
// Keeps the pawn's initial look direction instead of the (absent) player start's rotation
void ASOLGameMode::FinishRestartPlayer(AController* newPlayer, const FRotator& startRotation)
{
    const APawn* pawn = newPlayer != nullptr ? newPlayer->GetPawn() : nullptr;
    Super::FinishRestartPlayer(newPlayer, pawn != nullptr ? pawn->GetActorRotation() : startRotation);
}
