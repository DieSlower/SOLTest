/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "SOLMenuCameraPawn.generated.h"

class UCameraComponent;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;
class USOLMenuSubsystem;

/**
 * Main-menu camera (SDD 8, 8b): the pawn possessed while the main menu shows, before Play and after Quit to menu. It is
 * the observer: every OnBodiesUpdated (before the anchor update) it places the observer on the lit side of the menu's
 * start body, MENU_CAMERA_DISTANCE_RADII radii from its centre, slowly swaying around it in real time, and every
 * OnUniverseUpdated it moves to the observer's render location and looks at the body (turned so the body sits right of
 * the menu column). Changing the start body in the picker moves the camera to the new body. It has no input and does
 * not tick; the simulation (clock, warp, bodies) keeps running underneath.
 */
UCLASS()
class SOLTEST_API ASOLMenuCameraPawn : public APawn
{
    GENERATED_BODY()

public:

    // Creates the camera
    ASOLMenuCameraPawn();

protected:

    // Becomes the observer, places it at the start body and hooks the universe update
    virtual void BeginPlay() override;

    // Unhooks from the universe update and stops being the observer actor
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Returns the camera's universe position (ecliptic m) for this moment, and the body it looks at
    FVector3d ComputeObserverPositionM(int32& outBodyIndex) const;

    // Moves the observer along the camera path; bound to OnBodiesUpdated (before the anchor update)
    void UpdateObserver(float realDeltaSeconds);

    // Places the actor at the observer's render location looking at the body; bound to OnUniverseUpdated
    void FollowObserver();

    /** Camera looking at the start body. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Menu")
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    FDelegateHandle mBodiesUpdatedHandle;     // Binding to the anchor subsystem's OnBodiesUpdated
    FDelegateHandle mUniverseUpdatedHandle;   // Binding to the anchor subsystem's OnUniverseUpdated
    int32 mBodyIndex = INDEX_NONE;            // Body the camera looked at last (a change teleports the observer)
};
