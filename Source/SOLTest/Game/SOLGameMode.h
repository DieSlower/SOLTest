/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "SOLGameMode.generated.h"

/**
 * Game mode for the solar-system test map: spawns the body visuals and the star field and uses the ship pawn and the
 * flight HUD.
 */
UCLASS()
class SOLTEST_API ASOLGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:

    // Sets the default pawn and HUD classes
    ASOLGameMode();

    // Spawns the body, star-field, asteroid-belt, per-ring and combat visuals, arms the optional smoke-test hooks,
    // then starts play
    virtual void StartPlay() override;

    // Returns true for a Game or PIE world whose game mode (world override, else project default) is an ASOLGameMode
    static bool IsSOLGameWorld(const UWorld* world);

    // Takes the smoke-test screenshot and schedules the quit (verification runs only)
    void TakeSmokeScreenshot();

    // Takes a verification screenshot (Saved/Screenshots) without quitting
    void CaptureScreenshot();

    // Returns the ship pawn, or the debug spectator when -SOLSpectator is on the command line
    virtual UClass* GetDefaultPawnClassForController_Implementation(AController* controller) override;

protected:

    // Clears the smoke-test timers
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

    // Keeps the pawn's initial look direction instead of the (absent) player start's rotation
    virtual void FinishRestartPlayer(AController* newPlayer, const FRotator& startRotation) override;

private:


    // Phases of the -SOLCombatDemo script
    enum class ECombatDemoPhase : uint8
    {
        Drop,
        Fire,
        Rest,
    };

    // Quits the game after the smoke-test screenshot
    void QuitAfterSmokeTest();

    // Advances the -SOLCombatDemo script: drop a target, hold the trigger until it is destroyed, rest, repeat
    void StepCombatDemo();

    bool mSmokeConsoleRun = false; // Verification-only: the -SOLSmokeConsole commands have been run
    FTimerHandle mSmokeTimer; // Verification-only: -SOLSmokeShot=<seconds> screenshots, then quits
    FTimerHandle mCombatDemoTimer; // Verification-only: -SOLCombatDemo script steps
    ECombatDemoPhase mCombatDemoPhase = ECombatDemoPhase::Drop;
    double mCombatDemoPhaseStartSeconds = 0.0;
    int32 mCombatDemoCycle = 0;    // Completed drop-and-kill cycles
    int32 mCombatDemoShots = 0;    // Screenshots taken in the first cycle
};
