/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Ship/SOLShipSmokeInput.h"
#include "UI/SOLHudSmoke.h"

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "SOLShipPawn.generated.h"

class UCameraComponent;
class UDirectionalLightComponent;
class UInputAction;
class UInputMappingContext;
class ULocalPlayer;
class UMaterialInterface;
class USOLAnchorSubsystem;
class USOLShipSubsystem;
class USOLSpeedPanelWidget;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * Thin player proxy for the ship's Mass entity (SDD 2): owns the Enhanced Input bindings (created in C++), the
 * spring-arm chase camera and the placeholder ship mesh, composes FSOLShipControl every frame from the input state
 * and hands it to USOLShipSubsystem. The actor never carries universe coordinates: it sits at the observer's render
 * location (within the rebase distance of Unreal's origin) with the ship's orientation, set after each universe update.
 *
 * Mouse = virtual joystick: deltas move a stick offset (pixels) inside a circle of JOYSTICK_RADIUS_FRACTION of the
 * smaller viewport dimension; SOLFlight::JoystickToRotation turns it into yaw (X) and pitch (Y, mouse up = nose up).
 * Middle mouse recenters it. Holding Alt freezes the stick and orbits the camera instead. Every Enhanced Input
 * callback forwards to a plain Handle* function so scripts (-SOLSmokeInput) and future UI can drive the same paths.
 *
 * HUD keys (1c): O toggles the flight HUD's orbit ellipses, P is reserved for the predicted path (no-op), and F3 opens
 * the speed panel (USOLSpeedPanelWidget). While the panel is open the ship's own control input is suspended: the
 * mapping context is removed, held keys are flushed, thrust/roll/boost and the stick are zeroed and the panel has
 * UI-only keyboard focus. The ship's Mass simulation keeps running. Closing restores the mapping and game-only input.
 */
UCLASS()
class SOLTEST_API ASOLShipPawn : public APawn
{
    GENERATED_BODY()

public:

    // Creates the root, spring arm and camera, and enables ticking before physics
    ASOLShipPawn();

    // Composes and sends the ship control, updates the camera FOV and free-look, and runs the input smoke script
    virtual void Tick(float deltaSeconds) override;

    // Builds the input objects and binds the actions
    virtual void SetupPlayerInputComponent(UInputComponent* playerInputComponent) override;

    // Moves the mapping context to the new local controller and captures the mouse for the virtual joystick
    virtual void NotifyControllerChanged() override;

    // Removes the mapping context and releases the mouse before the controller lets go of this pawn
    virtual void UnPossessed() override;

    // Sets the translation input (local X forward, Y right, Z up, each in [-1, 1])
    void HandleThrust(const FVector3d& thrust);

    // Sets the roll input in [-1, 1] (positive = right wing down)
    void HandleRoll(double roll);

    // Sets whether boost is held
    void HandleBoost(bool bBoost);

    // Toggles flight-assist
    void HandleToggleAssist();

    // Sets whether the Alt free-look is held; releasing it returns the camera behind the ship
    void HandleFreeLook(bool bFreeLook);

    // Moves the virtual joystick (or, during free-look, orbits the camera) by a mouse delta in pixels (X right, Y up)
    void HandleMouseDelta(const FVector2d& deltaPixels);

    // Recenters the virtual joystick
    void HandleRecenterStick();

    // Steps the speed cap by whole wheel steps on the log scale
    void HandleSpeedCapSteps(int32 steps);

    // Toggles the reference-frame lock (M)
    void HandleMatchLock();

    // Selects the target under the forward reticle (T)
    void HandleSelectTarget();

    // Selects the next farther target (R)
    void HandleNextTarget();

    // Selects the next nearer target (F)
    void HandlePreviousTarget();

    // Clears the selected target (X)
    void HandleClearTarget();

    // Steps the time-warp up (])
    void HandleWarpUp();

    // Steps the time-warp down ([)
    void HandleWarpDown();

    // Resets the time-warp to 1x (Backspace)
    void HandleWarpReset();

    // Toggles the flight HUD's body orbit ellipses (O)
    void HandleToggleOrbitLines();

    // Reserved for the ship's predicted path (P); does nothing yet (SDD 2 section 6)
    void HandleTogglePredictedPath();

    // Steps the flight HUD's radar range one decade in ('-') or out ('='), switching it to MANUAL
    void HandleRadarZoom(bool bZoomIn);

    // Returns the flight HUD's radar to AUTO range (Home)
    void HandleRadarAuto();

    // Requests the F3 speed panel to open (done at the next tick, outside the input callback)
    void HandleToggleSpeedPanel();

    // Closes the speed panel and gives input back to the ship
    void CloseSpeedPanel();

    // Returns true while the speed panel is open (and the ship's own control input is suspended)
    bool IsSpeedPanelOpen() const { return mIsSpeedPanelOpen; }

    // Returns the speed panel widget, or nullptr before it was first opened
    USOLSpeedPanelWidget* GetSpeedPanel() const { return SpeedPanel; }

    // Sets the speed cap (clamped to the flight parameters' range) and returns the value set; the panel's apply path
    double ApplySpeedCapMps(double capMps);

    // Returns the control the pawn composed this frame
    const FSOLShipControl& GetComposedControl() const { return mControl; }

    // Returns the virtual joystick offset in pixels (X right, Y up)
    const FVector2d& GetStickOffsetPixels() const { return mStickOffsetPx; }

    // Returns the virtual joystick radius in pixels for the current viewport
    double GetStickRadiusPixels() const { return mStickRadiusPx; }

    // Returns true while Alt free-look is held
    bool IsFreeLooking() const { return mIsFreeLooking; }

protected:

    // Caches the subsystems, becomes the observer actor, builds the ship mesh and starts the input smoke script
    virtual void BeginPlay() override;

    // Removes the mapping context, releases the mouse and unhooks from the universe update
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Creates the input actions and the mapping context once
    void CreateInputObjects();

    // Removes the mapping context from the local player it was added to, if any
    void RemoveMappingContext();

    // Undoes the mouse capture: shows the cursor and restores game-and-UI input on the controller that had it
    void ReleaseMouse();

    // Detects the viewport losing focus (alt-tab, another window) and drops the held flight input once when it does
    void UpdateViewportFocus();

    // Recenters the virtual stick and zeroes thrust, roll and boost, so nothing stays held while the game is unfocused
    void HandleFocusLost();

    // Eases the camera arm toward the ship orientation (plus free-look) by an exponential slerp, after the ship moved
    void UpdateCameraRotation(float deltaSeconds);

    // Opens the speed panel: suspends the ship's control input and gives the panel UI-only keyboard focus
    void OpenSpeedPanel();

    // Removes the panel without restoring input (teardown)
    void DismissSpeedPanel();

    // Builds the placeholder ship from engine primitives with a light hull and a glowing engine
    void BuildShipMesh();

    // Adds one primitive part to the ship, relative to the root (location in cm, scale in primitive units)
    void AddShipPart(UStaticMesh* mesh, UMaterialInterface* material, const FVector& locationCm, const FRotator& rotation,
        const FVector& scale);

    // Places the actor at the observer's render location with the ship's orientation after the universe update
    void FollowShip();

    // Returns the ship's position and forward direction (Unreal-handed universe frame); false without a ship
    bool GetShipPose(FVector3d& outPositionM, FVector3d& outForwardDir) const;

    // Enhanced Input: thrust axes
    void OnThrustAction(const FInputActionValue& value);

    // Enhanced Input: thrust released
    void OnThrustCompleted(const FInputActionValue& value);

    // Enhanced Input: roll axis
    void OnRollAction(const FInputActionValue& value);

    // Enhanced Input: roll released
    void OnRollCompleted(const FInputActionValue& value);

    // Enhanced Input: boost pressed
    void OnBoostStarted(const FInputActionValue& value);

    // Enhanced Input: boost released
    void OnBoostCompleted(const FInputActionValue& value);

    // Enhanced Input: flight-assist toggle
    void OnToggleAssistAction(const FInputActionValue& value);

    // Enhanced Input: free-look pressed
    void OnFreeLookStarted(const FInputActionValue& value);

    // Enhanced Input: free-look released
    void OnFreeLookCompleted(const FInputActionValue& value);

    // Enhanced Input: mouse delta, converted back to pixels
    void OnLookAction(const FInputActionValue& value);

    // Enhanced Input: joystick recenter
    void OnRecenterAction(const FInputActionValue& value);

    // Enhanced Input: mouse wheel
    void OnSpeedCapAction(const FInputActionValue& value);

    // Enhanced Input: reference-frame lock
    void OnMatchLockAction(const FInputActionValue& value);

    // Enhanced Input: select under reticle
    void OnSelectTargetAction(const FInputActionValue& value);

    // Enhanced Input: next target
    void OnNextTargetAction(const FInputActionValue& value);

    // Enhanced Input: previous target
    void OnPreviousTargetAction(const FInputActionValue& value);

    // Enhanced Input: clear target
    void OnClearTargetAction(const FInputActionValue& value);

    // Enhanced Input: warp up
    void OnWarpUpAction(const FInputActionValue& value);

    // Enhanced Input: warp down
    void OnWarpDownAction(const FInputActionValue& value);

    // Enhanced Input: warp reset
    void OnWarpResetAction(const FInputActionValue& value);

    // Enhanced Input: orbit lines toggle
    void OnToggleOrbitLinesAction(const FInputActionValue& value);

    // Enhanced Input: predicted path toggle (reserved)
    void OnTogglePredictedPathAction(const FInputActionValue& value);

    // Enhanced Input: speed panel
    void OnSpeedPanelAction(const FInputActionValue& value);

    // Enhanced Input: radar zoom in
    void OnRadarZoomInAction(const FInputActionValue& value);

    // Enhanced Input: radar zoom out
    void OnRadarZoomOutAction(const FInputActionValue& value);

    // Enhanced Input: radar back to AUTO range
    void OnRadarAutoAction(const FInputActionValue& value);

    /** Root the ship parts and the camera arm hang from; carries the ship's orientation. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Ship")
    TObjectPtr<USceneComponent> ShipRoot;

    /** Chase-camera arm behind and above the ship, with rotation lag only. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Ship")
    TObjectPtr<USpringArmComponent> SpringArm;

    /** Chase camera; its FOV widens with speed. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Ship")
    TObjectPtr<UCameraComponent> Camera;

    /** Shadowless directional fill light along the view, lighting the ship's shadowed side. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Ship")
    TObjectPtr<UDirectionalLightComponent> FillLight;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> ShipParts;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> MappingContext;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ThrustAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> RollAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> BoostAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ToggleAssistAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> FreeLookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> LookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> RecenterAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SpeedCapAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MatchLockAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SelectTargetAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> NextTargetAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> PreviousTargetAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ClearTargetAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> WarpUpAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> WarpDownAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> WarpResetAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ToggleOrbitLinesAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> TogglePredictedPathAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SpeedPanelAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> RadarZoomInAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> RadarZoomOutAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> RadarAutoAction;

    UPROPERTY(Transient)
    TObjectPtr<USOLSpeedPanelWidget> SpeedPanel;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    TWeakObjectPtr<ULocalPlayer> mMappedLocalPlayer;        // Local player that currently has MappingContext added
    TUniquePtr<FSOLShipSmokeInput> mSmokeInput;             // Verification-only scripted input (-SOLSmokeInput)
    TUniquePtr<FSOLHudSmoke> mSmokeHud;                     // Verification-only HUD/panel script (-SOLSmokeHud)
    FSOLShipControl mControl;                               // Control composed from the input state
    FVector2d mStickOffsetPx = FVector2d::ZeroVector;       // Virtual joystick offset (X right, Y up), pixels
    FRotator mFreeLookRotation = FRotator::ZeroRotator;     // Camera orbit around the ship while Alt is held
    FQuat mCameraRotation = FQuat::Identity;                // Lagged world rotation of the camera arm
    FDelegateHandle mUniverseUpdatedHandle;                 // Binding to the anchor subsystem's OnUniverseUpdated
    double mRollInput = 0.0;                                // Q/E roll input
    double mStickRadiusPx = 1.0;                            // Virtual joystick radius for the current viewport
    double mMouseUnitsToPixels = 1.0;                       // Undoes the Mouse2D axis sensitivity of the input config
    bool mIsFreeLooking = false;                            // True while Alt is held
    bool mHasCameraRotation = false;                        // False until the camera rotation is first set
    bool mHadViewportFocus = false;                         // Viewport focus last frame (focus loss is an edge)
    bool mHasCapturedMouse = false;                         // True while this pawn hides the cursor (game-only input)
    bool mIsSpeedPanelOpen = false;                         // True while the F3 panel has focus
    bool mIsSpeedPanelRequested = false;                    // F3 pressed; the panel opens at the next tick
};
