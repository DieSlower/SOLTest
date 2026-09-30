/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Map/SOLMapCamera.h"
#include "Map/SOLMapPicking.h"
#include "Map/SOLMapScreenPick.h"

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "SOLMapModeSubsystem.generated.h"

class ACameraActor;
class APlayerController;
class FSOLMapBodyOverlay;
class USOLAnchorSubsystem;
class USOLBodyRegistrySubsystem;

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
 *
 * Destination picking (SDD 3 section 3, sub-part 2d) lives here too, as map-mode state: the FSOLMapPickState, the
 * reference's live position and the live preview. The pawn forwards the cursor and the left button / Shift / X /
 * Enter; mouse-down picks the body under the cursor on screen (SOLMapScreenPick over the body overlay's screen
 * positions) or else the ship; while held, the cursor ray meets the ecliptic plane through the reference's CURRENT
 * position (planar preview); mouse-up locks it; Shift + vertical mouse travel previews the height; Shift + click locks
 * it. The pick is recomputed on OnUniverseUpdated (positions final, before the HUD draws), so a moving reference
 * carries its locked offsets with it. Opening or closing the map clears the pick.
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

    // Returns the map camera's vertical field of view (radians) for a viewport aspect ratio (width / height); the
    // camera's own FOV is horizontal (MAP_CAMERA_FOV_DEG before the camera exists)
    double GetVerticalFovRad(double aspectRatio) const;

    // Sets the map cursor (pixels, top-left origin) and the size of the viewport it is measured in
    void SetPickCursorPx(const FVector2D& cursorPx, const FVector2D& viewportSizePx);

    // Left mouse down: with the height modifier held during an active height preview it locks the height at the
    // current cursor; otherwise it starts a new pick whose reference is the body under the cursor (screen-space pick
    // on the overlay) or the ship
    void PressPick(const FSOLMapBodyOverlay* overlay, bool bHeightModifierHeld);

    // Left mouse up: locks the planar offset of the drag
    void ReleasePick();

    // Shift held or released: starts or drops the height preview (only between the planar and the height lock, and
    // never during a camera drag, where Shift is the pan modifier)
    void SetHeightModifierHeld(bool bHeld);

    // Sets whether a camera drag (right-drag orbit, or middle / Shift+right pan) is active; one starting drops the
    // height preview, and none starts while it lasts (Shift then means pan). Releasing the drag does not start one
    void SetCameraDragActive(bool bActive);

    // Clears the whole pick (X): no reference, nothing locked, no preview
    void ClearPick();

    // Enter: logs the live destination a jump would go to (the jump itself is 2e); false when none is locked yet
    bool RequestJump();

    // Returns the destination-pick progress (reference, locks, locked offsets)
    const FSOLMapPickState& GetPickState() const { return mPick; }

    // Returns true while the left button drags the planar preview
    bool IsPickDragging() const { return mIsPickDragging; }

    // Returns true while Shift previews the height offset
    bool IsHeightPreviewing() const { return mIsHeightPreviewing; }

    // Returns the reference's live universe position (meters, ecliptic) as of the last update; valid with a reference
    const FVector3d& GetPickReferencePositionM() const { return mPickReferenceM; }

    // Returns the planar offset (dx, dy) in meters: the live drag preview, or the locked value after mouse-up
    FVector2D GetPickPlanarOffsetM() const;

    // Returns the height offset in meters: the locked value, else the live Shift preview, else 0
    double GetPickHeightOffsetM() const;

    // Returns the vertical-mouse scale of the current height preview (meters per pixel, frozen at its start)
    double GetHeightMetersPerPixel() const { return mHeightMetersPerPixel; }

    // Returns the live destination (reference's current position + locked offsets; height 0 until it is locked) and
    // true once the planar offset is locked; false (and outDestinationM untouched) before that. What 2e jumps to
    bool GetLiveDestinationM(FVector3d& outDestinationM) const;

    // Returns the number of jump requests (Enter with a locked destination) since the map opened (verification)
    int32 GetJumpRequestCount() const { return mJumpRequestCount; }

    // Returns the destination of the last jump request (verification)
    const FVector3d& GetLastJumpRequestM() const { return mLastJumpRequestM; }

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

    // Recomputes the reference's live position, the drag and height previews and the destination for this frame
    void UpdatePick();

    // Returns the unit ray (ecliptic) from the map camera through the cursor; false without a valid viewport
    bool ComputeCursorRay(FVector3d& outDirection) const;

    // Returns the reference's live universe position: the picked body's, or the ship's (the observer). A body index
    // that no longer resolves turns the reference into the ship
    FVector3d ResolvePickReferenceM();

    // Starts the Shift height preview at the current cursor, freezing its meters-per-pixel scale
    void StartHeightPreview();

    // Drops the height preview (back to 0); the next one starts afresh
    void StopHeightPreview();

    // Drops the pick state and every preview back to "no reference"
    void ResetPick();

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLBodyRegistrySubsystem> BodyRegistry;

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

    // Destination picking (2d)
    TArray<FSOLScreenPickCandidate> mPickCandidates;            // Reused per mouse-down, sized once
    FSOLMapPickState mPick;                                     // Reference, locks and locked offsets
    FVector3d mPickReferenceM = FVector3d::ZeroVector;          // Reference's live universe position this frame
    FVector3d mLastJumpRequestM = FVector3d::ZeroVector;        // Destination of the last Enter (verification)
    FVector2D mPickCursorPx = FVector2D::ZeroVector;            // Map cursor, pixels from the top-left
    FVector2D mViewportSizePx = FVector2D::ZeroVector;          // Viewport the cursor is measured in
    FVector2D mPreviewPlanarM = FVector2D::ZeroVector;          // Live (dx, dy) while dragging
    double mPreviewHeightM = 0.0;                               // Live dz while Shift previews
    double mHeightAnchorCursorY = 0.0;                          // Cursor Y where the height preview started
    double mHeightMetersPerPixel = 0.0;                         // Height preview scale, frozen at its start
    int32 mJumpRequestCount = 0;                                // Enter presses with a destination since opening
    bool mIsPickDragging = false;                               // Left button held after a mouse-down
    bool mIsHeightModifierHeld = false;                         // Shift held on the map
    bool mIsHeightPreviewing = false;                           // Shift previewing the height
    bool mIsCameraDragging = false;                             // Right or middle button drags the camera
};
