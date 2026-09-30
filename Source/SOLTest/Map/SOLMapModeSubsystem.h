/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapCamera.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLMapModeSubsystem.generated.h"

class ACameraActor;
class APlayerController;
class USOLAnchorSubsystem;

/**
 * Jump-map camera mode (SDD 3, sub-part 2b-2). Owns the orbit-camera state (FSOLOrbitCameraState, driven by the pure
 * SOLMapCamera math), a dedicated camera actor that becomes the player's view target while the map is open, and the
 * anchor subsystem's render-viewpoint override. The ship pawn owns the J/Esc keys, the map's input mapping and the
 * cursor (the same way it owns the F3 panel) and forwards mouse drags and wheel notches here.
 *
 * Frame order: pending input is applied on OnBodiesUpdated, and the camera's universe position becomes the render
 * viewpoint before the anchor rebases, so the render origin sits at the map camera and every body is placed (1:1 near,
 * depth-compressed far, angular size kept) as seen from it. The camera actor is then placed on OnUniverseUpdated from
 * the final render origin. The sim clock, the ship's Mass step and the anchor (still the ship) keep running.
 */
UCLASS()
class SOLTEST_API USOLMapModeSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:

    // Resolves the anchor subsystem and subscribes to its frame events
    virtual void Initialize(FSubsystemCollectionBase& collection) override;

    // Unsubscribes from the anchor subsystem and drops the camera
    virtual void Deinitialize() override;

    // Creates the subsystem only in game and PIE worlds that run ASOLGameMode
    virtual bool ShouldCreateSubsystem(UObject* outer) const override;

    // Opens the map for a controller: orbit state around the ship, map camera as the view target; false on failure
    bool OpenMap(APlayerController* playerController);

    // Closes the map: gives the view back to the controller's pawn and the render viewpoint back to the observer
    void CloseMap(APlayerController* playerController);

    // Returns true while the map is open
    bool IsMapOpen() const { return mIsOpen; }

    // Queues an orbit drag in mouse pixels (X right, Y up), applied at the next universe update
    void AddOrbitPixels(const FVector2d& deltaPixels);

    // Queues a pan drag in mouse pixels (X right, Y up), applied at the next universe update
    void AddPanPixels(const FVector2d& deltaPixels);

    // Queues wheel notches (positive = wheel forward = zoom in), applied at the next universe update
    void AddZoomNotches(int32 notches);

    // Returns the orbit-camera state (universe frame)
    const FSOLOrbitCameraState& GetCameraState() const { return mState; }

    // Returns the orbit-camera limits and zoom step
    const FSOLOrbitCameraParams& GetCameraParams() const { return mParams; }

    // Returns the map camera's universe position (meters, ecliptic) as of the last update
    const FVector3d& GetCameraPositionM() const { return mCameraPositionM; }

    // Returns the map camera actor, or nullptr before the map was first opened
    ACameraActor* GetCameraActor() const { return CameraActor; }

protected:

    // Limits the subsystem to game and PIE worlds so editor and automation worlds are unaffected
    virtual bool DoesSupportWorldType(const EWorldType::Type worldType) const override;

private:

    // Applies the queued input and makes the resulting camera position the render viewpoint (before the rebase)
    void HandleBodiesUpdated(float realDeltaSeconds);

    // Places the camera actor from the final render origin of the frame
    void HandleUniverseUpdated();

    // Spawns the map camera actor once
    bool EnsureCameraActor();

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<ACameraActor> CameraActor;

    TWeakObjectPtr<AActor> mPreviousViewTarget;                 // View target to restore when the map closes
    FSOLOrbitCameraState mState;                                // Orbit camera (focus, yaw, pitch, distance)
    FSOLOrbitCameraParams mParams;                              // Pitch and distance limits, zoom step
    FVector3d mCameraPositionM = FVector3d::ZeroVector;         // Camera universe position of the current frame
    FVector2d mPendingOrbitPx = FVector2d::ZeroVector;          // Orbit drag accumulated since the last update
    FVector2d mPendingPanPx = FVector2d::ZeroVector;            // Pan drag accumulated since the last update
    FDelegateHandle mBodiesUpdatedHandle;                       // Binding to OnBodiesUpdated
    FDelegateHandle mUniverseUpdatedHandle;                     // Binding to OnUniverseUpdated
    int32 mPendingZoomNotches = 0;                              // Wheel notches accumulated since the last update
    bool mIsOpen = false;                                       // True while the map is open
};
