/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "SOLGameMode.generated.h"

/** Game mode for the solar-system test map: spawns the body visuals and uses the debug spectator and debug HUD. */
UCLASS()
class SOLTEST_API ASOLGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:

    // Sets the default pawn and HUD classes
    ASOLGameMode();

    // Spawns the body visuals, arms the optional smoke-test screenshot, then starts play
    virtual void StartPlay() override;

    // Returns true for a Game or PIE world whose game mode (world override, else project default) is an ASOLGameMode
    static bool IsSOLGameWorld(const UWorld* world);

protected:

    // Clears the smoke-test timers
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

    // Keeps the pawn's initial look direction instead of the (absent) player start's rotation
    virtual void FinishRestartPlayer(AController* newPlayer, const FRotator& startRotation) override;

private:

    // Takes the smoke-test screenshot and schedules the quit
    void TakeSmokeScreenshot();

    // Quits the game after the smoke-test screenshot
    void QuitAfterSmokeTest();

    FTimerHandle mSmokeTimer; // Verification-only: -SOLSmokeShot=<seconds> screenshots, then quits
};
