/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Game/SOLGameMode.h"

#include "Combat/SOLCombatSubsystem.h"
#include "Combat/SOLCombatVisuals.h"
#include "Game/SOLSpectatorPawn.h"
#include "MinorBodies/SOLAsteroidBeltVisuals.h"
#include "MinorBodies/SOLPlanetRing.h"
#include "MinorBodies/SOLRingVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "StarField/SOLStarField.h"
#include "UI/SOLFlightHud.h"
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
}

//////////////////////////////////////////////////////////////////////////
// Sets the default pawn and HUD classes
ASOLGameMode::ASOLGameMode()
{
    DefaultPawnClass = ASOLShipPawn::StaticClass();
    HUDClass = ASOLFlightHud::StaticClass();
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship pawn, or the debug spectator when -SOLSpectator is on the command line
UClass* ASOLGameMode::GetDefaultPawnClassForController_Implementation(AController* controller)
{
    if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SPECTATOR))
    {
        return ASOLSpectatorPawn::StaticClass();
    }
    return Super::GetDefaultPawnClassForController_Implementation(controller);
}

//////////////////////////////////////////////////////////////////////////
// Spawns the body, star-field, asteroid-belt, per-ring and combat visuals, arms the optional smoke-test hooks, then
// starts play
void ASOLGameMode::StartPlay()
{
    FActorSpawnParameters params;
    params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GetWorld()->SpawnActor<ASOLBodyVisuals>(ASOLBodyVisuals::StaticClass(), FTransform::Identity, params);
    GetWorld()->SpawnActor<ASOLStarField>(ASOLStarField::StaticClass(), FTransform::Identity, params);
    GetWorld()->SpawnActor<ASOLAsteroidBeltVisuals>(ASOLAsteroidBeltVisuals::StaticClass(), FTransform::Identity,
        params);
    GetWorld()->SpawnActor<ASOLCombatVisuals>(ASOLCombatVisuals::StaticClass(), FTransform::Identity, params);

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

    // Verification hook for headless smoke runs: screenshot after the given delay, then quit
    float smokeDelaySeconds = 0.0f;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_SHOT, smokeDelaySeconds) && smokeDelaySeconds > 0.0f)
    {
        GetWorldTimerManager().SetTimer(mSmokeTimer, this, &ASOLGameMode::TakeSmokeScreenshot, smokeDelaySeconds);
    }

    // Verification hook for the combat visuals: drop a target ahead of the ship and shoot it until it dies, repeatedly
    if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::COMBAT_DEMO))
    {
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

    const double nowSeconds = GetWorld()->GetTimeSeconds();
    const double phaseSeconds = nowSeconds - mCombatDemoPhaseStartSeconds;
    switch (mCombatDemoPhase)
    {
    case ECombatDemoPhase::Drop:
        pawn->HandleDropTarget();
        mCombatDemoPhase = ECombatDemoPhase::Fire;
        mCombatDemoPhaseStartSeconds = nowSeconds;
        UE_LOG(LogSOL, Log, TEXT("CombatDemo: target dropped"));
        break;
    case ECombatDemoPhase::Fire:
        // Hold the trigger once the drop has landed; release it when no target is left (destroyed)
        if (phaseSeconds >= COMBAT_DEMO_DROP_SETTLE_SECONDS)
        {
            const bool bTargetLeft = combat->GetActiveTargetCount() > 0;
            pawn->HandleFire(bTargetLeft);
            if (mCombatDemoCycle == 0 && mCombatDemoShots == 0 && phaseSeconds >= COMBAT_DEMO_FIRE_SHOT_SECONDS)
            {
                CaptureScreenshot();
                ++mCombatDemoShots;
            }
            if (!bTargetLeft)
            {
                mCombatDemoPhase = ECombatDemoPhase::Rest;
                mCombatDemoPhaseStartSeconds = nowSeconds;
                UE_LOG(LogSOL, Log, TEXT("CombatDemo: target destroyed after %.2f s of fire"), phaseSeconds);
            }
        }
        break;
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
// Keeps the pawn's initial look direction instead of the (absent) player start's rotation
void ASOLGameMode::FinishRestartPlayer(AController* newPlayer, const FRotator& startRotation)
{
    const APawn* pawn = newPlayer != nullptr ? newPlayer->GetPawn() : nullptr;
    Super::FinishRestartPlayer(newPlayer, pawn != nullptr ? pawn->GetActorRotation() : startRotation);
}
