/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Game/SOLGameMode.h"

#include "Game/SOLSpectatorPawn.h"
#include "MinorBodies/SOLAsteroidBeltVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "SOLConstants.h"
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
    constexpr float SMOKE_QUIT_DELAY_SECONDS = 3.0f;                            // Time for the screenshot to be written
    const TCHAR* const SMOKE_SCREENSHOT_COMMAND = TEXT("HighResShot 1280x720"); // Written to Saved/Screenshots
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
// Spawns the body, star-field and asteroid-belt visuals, arms the optional smoke-test screenshot, then starts play
void ASOLGameMode::StartPlay()
{
    FActorSpawnParameters params;
    params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    GetWorld()->SpawnActor<ASOLBodyVisuals>(ASOLBodyVisuals::StaticClass(), FTransform::Identity, params);
    GetWorld()->SpawnActor<ASOLStarField>(ASOLStarField::StaticClass(), FTransform::Identity, params);
    GetWorld()->SpawnActor<ASOLAsteroidBeltVisuals>(ASOLAsteroidBeltVisuals::StaticClass(), FTransform::Identity,
        params);

    // Verification hook for headless smoke runs: screenshot after the given delay, then quit
    float smokeDelaySeconds = 0.0f;
    if (FParse::Value(FCommandLine::Get(), SOL::CommandLine::SMOKE_SHOT, smokeDelaySeconds) && smokeDelaySeconds > 0.0f)
    {
        GetWorldTimerManager().SetTimer(mSmokeTimer, this, &ASOLGameMode::TakeSmokeScreenshot, smokeDelaySeconds);
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
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Takes the smoke-test screenshot and schedules the quit (verification runs only)
void ASOLGameMode::TakeSmokeScreenshot()
{
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
// Keeps the pawn's initial look direction instead of the (absent) player start's rotation
void ASOLGameMode::FinishRestartPlayer(AController* newPlayer, const FRotator& startRotation)
{
    const APawn* pawn = newPlayer != nullptr ? newPlayer->GetPawn() : nullptr;
    Super::FinishRestartPlayer(newPlayer, pawn != nullptr ? pawn->GetActorRotation() : startRotation);
}
