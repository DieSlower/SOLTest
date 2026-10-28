/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#pragma once

#include "Flight/SOLFlight.h"
#include "Flight/SOLFlightMode.h"
#include "Map/SOLJumpSmoke.h"
#include "Map/SOLMapAlignSmoke.h"
#include "Map/SOLMapModeState.h"
#include "Map/SOLMapPickSmoke.h"
#include "Map/SOLMapSmoke.h"
#include "Map/SOLRouteOverlaySmoke.h"
#include "Map/SOLRouteSmoke.h"
#include "Ship/SOLShipSmokeInput.h"
#include "Ship/SOLSurfaceLockSmoke.h"
#include "UI/SOLHudSmoke.h"

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "SOLShipPawn.generated.h"

class APlayerController;
class UCameraComponent;
class UPostProcessComponent;
class UDirectionalLightComponent;
class UInputAction;
class UInputMappingContext;
class ULocalPlayer;
class UMaterialInterface;
class USOLAnchorSubsystem;
class USOLCombatSubsystem;
class USOLJumpSubsystem;
class USOLMapModeSubsystem;
class USOLMenuSubsystem;
class USOLRouteSubsystem;
class USOLSettingsSubsystem;
class USOLShipSubsystem;
class USOLSpeedPanelWidget;
class USOLSimClockSubsystem;
class USOLTargetingSubsystem;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;
struct FInputActionValue;

namespace SOLMenuState
{
    struct FMenuState;
}

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
 * HUD keys (1c): O toggles the flight HUD's orbit ellipses, P the route overlay (issue #20), and F3 opens
 * the speed panel (USOLSpeedPanelWidget). While the panel is open the ship's own control input is suspended: the
 * mapping context is removed, held keys are flushed, thrust/roll/boost and the stick are zeroed and the panel has
 * UI-only keyboard focus. The ship's Mass simulation keeps running. Closing restores the mapping and game-only input.
 *
 * Jump map (2b-2): J opens the map through USOLMapModeSubsystem (which owns the orbit camera and the view). The ship's
 * control input is suspended exactly as for F3, the map's own mapping context is added, and the OS cursor is shown
 * and free (game-and-UI input, not locked): right-drag orbits, middle- or Shift+right-drag pans, the wheel zooms. J or
 * Esc closes it and restores the ship's mapping and captured cursor. Opening and closing happen at the next tick,
 * outside the Enhanced Input callback, like F3.
 *
 * Destination picking (2d): the pawn keeps a map cursor (the OS mouse position while no map button is held; while the
 * left button drags, the position at mouse-down plus the mouse deltas, since the viewport captures and hides the
 * cursor during a drag) and forwards it, the left button, Shift, X (clear) and Enter (jump) to USOLMapModeSubsystem,
 * which owns the pick state. When the last held map button is released, the viewport ends its capture and puts the OS
 * cursor back where the capture began; the pawn then warps the OS cursor onto the map cursor so the two never disagree.
 *
 * Jump (2e): Enter with a locked destination hands the pick to USOLJumpSubsystem (which owns the warp sequence and the
 * arrival teleport) and closes the map through the same path as J / Esc. While the warp runs the pawn drives the chase
 * camera's FOV from the sequence (the same Camera field the speed-based widening uses, which resumes from the restored
 * value afterwards) and blends in the camera's warp post-process by the streak intensity; J cannot open the map then.
 *
 * Surface-lock (3b, SDD 4): L hands a toggle request to USOLShipSubsystem (RequestSurfaceLockToggle), which owns the
 * lock state and consumes the press as the edge-triggered input of exactly its next ship step, so a press is never
 * lost or counted twice however the pawn tick and the ship step interleave. L is ignored during a jump warp; while the
 * map or the speed panel is open the ship mapping (and with it L) is removed anyway.
 *
 * Combat (7b, SDD 7): holding the left mouse button holds the gun trigger and G drops a target; both only hand state to
 * USOLCombatSubsystem, which owns the fire cadence, the muzzles and the drop rules and acts in its own update. Every
 * tick the pawn also hands it the chase camera's offset from the ship and its forward direction (ecliptic), the camera
 * ray the bolts converge on. The trigger reads as released while the map or the speed panel is open, during a jump
 * warp, and once the viewport loses focus.
 *
 * Menus (8b, SDD 8): Esc (IA_ShipPause, in the ship mapping only, so the open map's own Esc closes the map instead)
 * asks USOLMenuSubsystem to pause, at the next tick. While its state suspends flight input (the pause menu) the pawn
 * suspends its control exactly as for the map (SuspendShipControl; the simulation keeps running) and restores it on
 * resume. Opening and closing the map are reported to the menu subsystem.
 *
 * Settings (8d, SDD 8): the rebindable keys of the ship mapping (and J in the map mapping) come from
 * USOLSettingsSubsystem's applied keys (SOLKeyBindings order) and are mapped again when they change (Apply), with a
 * control-mapping rebuild; the fixed keys (mouse, wheel, Esc, P and the map's own keys) stay as they are. Mouse-look
 * deltas from Enhanced Input are scaled by the live mouse sensitivity and Y-flipped when inverted
 * (SOLSettingsRules::ApplyLookSettings; scripts calling HandleMouseDelta are unaffected), and flight assist starts in
 * the setting's state.
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

    // Toggles boost on / off (SDD 19 section 3.6); ignored while the pilot has no control
    void HandleToggleBoost();

    // Sets whether the steer-hold key (Space) is down: the mouse steers while it is (SDD 19 section 3.4)
    void HandleSteerHold(bool bDown);

    // Toggles mouse steering on / off (wheel click)
    void HandleToggleSteer();

    // Sets whether stop spin is held (SDD 18: cancels all rotation, in every flight mode)
    void HandleStopSpin(bool bStopSpin);

    // Toggles assisted / manual flight; during the autopilot, takes control back in assisted flight
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

    // Toggles surface-lock (L): hands the press to the ship subsystem, which consumes it in exactly its next step
    void HandleToggleLevel();

    // Sets whether the fire trigger (left mouse) is held
    void HandleFire(bool bHeld);

    // Requests a target drop ahead of the ship (G)
    void HandleDropTarget();

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

    // Shows or hides the route overlay (P; SDD 20)
    void HandleToggleRouteOverlay();

    // Resumes a paused route (V; flight and map)
    void HandleResumeRoute();

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

    // Shift+M (bForJump false) or Shift+J (true): in flight, requests the map to open in the last used mode or in Jump
    // mode (done at the next tick, outside the input callback); on the map, Shift+M closes it and Shift+J switches to
    // Jump mode
    void HandleOpenMap(bool bForJump);

    // An in-map mode key: J Jump, M Move, P Patrol (P again flips loop / ping-pong)
    void HandleMapModeKey(ESOLMapMode key);

    // Clears the active route (Delete on the map)
    void HandleMapClearRoute();

    // Requests the jump map to close (Esc; done at the next tick)
    void HandleCloseMap();

    // Requests the pause menu (Esc in flight; handed to the menu subsystem at the next tick)
    void HandlePause();

    // Returns true while a menu has the ship's control input suspended
    bool IsMenuSuspended() const { return mIsMenuSuspended; }

    // Sets whether the map's right mouse button is held (orbit drag, or pan drag with Shift)
    void HandleMapRightHeld(bool bHeld);

    // Sets whether the map's middle mouse button is held (pan drag)
    void HandleMapPanHeld(bool bHeld);

    // Sets whether Shift is held on the map (turns a right drag into a pan drag; previews and locks the pick height)
    void HandleMapPanModifierHeld(bool bHeld);

    // Left mouse pressed on the map: starts a destination pick, or with Shift during a height preview locks the height
    void HandleMapPickPressed();

    // Left mouse released on the map: locks the pick's planar offset
    void HandleMapPickReleased();

    // Clears the map's destination pick, and the draft route in Move / Patrol mode (X)
    void HandleMapClearPick();

    // Enter on the map: requests the jump (Jump mode) or the route start (Move / Patrol), done at the next tick
    void HandleMapJump();

    // Moves the map cursor to a pixel position (top-left origin) and forwards it to the map
    void HandleMapCursorMoved(const FVector2D& cursorPx);

    // Verification: while set, the map cursor ignores the OS mouse and only follows HandleMapCursorMoved and drags
    void SetMapCursorScripted(bool bScripted) { mIsMapCursorScripted = bScripted; }

    // Returns the map cursor (pixels, top-left origin)
    const FVector2D& GetMapCursorPx() const { return mMapCursorPx; }

    // Forwards a mouse delta in pixels (X right, Y up) to the map: middle or Shift+right drag pans, right drag orbits
    void HandleMapMouseDelta(const FVector2d& deltaPixels);

    // Forwards wheel notches to the map zoom (positive = wheel forward = zoom in)
    void HandleMapZoom(int32 notches);

    // Returns true while the jump map is open (and the ship's own control input is suspended)
    bool IsMapOpen() const { return mIsMapOpen; }

    // Returns true while the ship's own mapping context is active on the local player
    bool IsShipInputMapped() const { return mMappedLocalPlayer.IsValid(); }

    // Returns true while the map's mapping context is active on the local player
    bool IsMapInputMapped() const { return mMapMappedLocalPlayer.IsValid(); }

    // Returns the engine key names the ship mapping uses for each rebindable action (SOLKeyBindings order)
    const TArray<FString>& GetBoundKeyNames() const { return mBoundKeys; }

    // Returns true if the ship mapping context maps this key to a binding's action
    bool IsKeyMappedToBinding(int32 binding, const FKey& key) const;

    // Returns the control the pawn composed this frame
    const FSOLShipControl& GetComposedControl() const { return mControl; }

    // Returns the virtual joystick offset in pixels (X right, Y up)
    const FVector2d& GetStickOffsetPixels() const { return mStickOffsetPx; }

    // Returns the virtual joystick radius in pixels for the current viewport
    double GetStickRadiusPixels() const { return mStickRadiusPx; }

    // Returns true while Alt free-look is held
    bool IsFreeLooking() const { return mIsFreeLooking; }

    // Returns true while the mouse steers (the steering gate is active this frame)
    bool IsSteering() const { return mIsSteering; }

    // Returns the HUD's mouse-steer icon state (Free, Held, Locked)
    ESOLSteerIcon GetSteerIcon() const { return SOLFlightMode::SteerIconFor(mSteerGate); }

    // Returns true while boost is toggled on
    bool IsBoostOn() const { return mBoost.bOn; }

    // Returns the chase camera's current horizontal field of view (degrees)
    float GetCameraFovDeg() const;

    // Returns the blend weight of the camera's warp post-process (0 outside a jump warp)
    float GetWarpPostProcessWeight() const;

    // Returns the pilot G vision-loss post-process weight this frame (0 = clear, 1 = blacked out)
    float GetGVisionWeight() const;

    // Returns true while the pawn shows the warp effect (FOV pulse and post-process)
    bool IsWarpEffectActive() const { return mIsWarpEffectActive; }

    // Returns the FOV the warp pulse started from and returns to (degrees)
    double GetWarpBaseFovDeg() const { return mWarpBaseFovDeg; }

    // Returns the sequence time the warp FOV and post-process were last computed for (verification)
    double GetWarpEffectElapsedS() const { return mWarpEffectElapsedS; }

protected:

    // Caches the subsystems, becomes the observer actor, builds the ship mesh and starts the input smoke script
    virtual void BeginPlay() override;

    // Removes the mapping context, releases the mouse and unhooks from the universe update
    virtual void EndPlay(const EEndPlayReason::Type endPlayReason) override;

private:

    // Creates the input actions and the mapping context once
    void CreateInputObjects();

    // Maps every key of the ship and map contexts, the rebindable ones from the settings' applied keys
    void MapShipKeys();

    // The applied key bindings changed: maps the keys again and asks Enhanced Input to rebuild its mappings
    void HandleKeyBindingsChanged();

    // Returns the input action a rebindable binding drives (thrust and roll bindings share their axis action)
    UInputAction* GetBindingAction(int32 binding) const;

    // Removes the mapping context from the local player it was added to, if any
    void RemoveMappingContext();

    // Undoes the mouse capture: shows the cursor and restores game-and-UI input on the controller that had it
    void ReleaseMouse();

    // Updates the steering gate (suspension, recenter edge) and the cursor capture that follows it
    void UpdateSteering();

    // Returns true while something else has the input or the view (map, speed panel, menu, jump warp, focus loss)
    bool IsSteerSuspended() const;

    // Captures and hides the cursor (mouse steering or free-look) or shows it free over the game
    void ApplyShipCursor(bool bCapture);

    // Detects the viewport losing focus (alt-tab, another window) and drops the held flight input once when it does
    void UpdateViewportFocus();

    // Recenters the virtual stick and zeroes thrust and roll, so nothing stays held while the game is unfocused
    void HandleFocusLost();

    // Sets the chase camera's FOV: the warp pulse and post-process during a jump, else the speed-based widening
    void UpdateCameraFov(float deltaSeconds);

    // Ends the warp effect: back to the FOV it started from, post-process off; the speed-based easing resumes from here
    void EndWarpEffect();

    // Returns true while the jump warp sequence runs
    bool IsJumpWarping() const;

    // Hands the map's locked destination to the jump sequence and closes the map as J / Esc would
    void StartMapJump();

    // Eases the camera arm toward the ship orientation (plus free-look) by an exponential slerp, after the ship moved
    void UpdateCameraRotation(float deltaSeconds);

    // Opens the speed panel: suspends the ship's control input and gives the panel UI-only keyboard focus
    void OpenSpeedPanel();

    // Removes the panel without restoring input (teardown)
    void DismissSpeedPanel();

    // Suspends the ship's own control input: flushes held keys, drops the ship mapping and zeroes the flight input
    void SuspendShipControl(APlayerController& playerController);

    // Restores the ship's mapping and the hidden, captured cursor of the virtual joystick; in AUTO, latches the ship
    // keys still held so they do not cancel the autopilot until released
    void RestoreShipControl();

    // Returns the pilot input the ship's bound flight keys hold right now (no stick: it is zeroed while suspended);
    // with bShiftKeysOnly, only the bindings whose key is Left / Right Shift count
    FSOLPilotInput HeldKeyPilotInput(const APlayerController& playerController, bool bShiftKeysOnly = false) const;

    // Opens the map (in the last used mode, or Jump mode for Shift+J): adds the map mapping (rolling back on failure),
    // suspends ship control and frees the OS cursor
    void OpenMap(bool bForJump);

    // Closes the jump map and gives input and the view back to the ship
    void CloseMap();

    // Closes the map without restoring the ship's input (teardown)
    void DismissMap();

    // Follows the OS mouse position with the map cursor while no map button is held (and no script drives it)
    void UpdateMapCursor();

    // Once no map button is held (the viewport's mouse capture ended and it put the OS cursor back where the capture
    // began), warps the OS cursor onto the map cursor and records that as the last OS reading, so neither jumps later
    void SyncOsCursorToMapCursor();

    // Tells the map whether a camera drag (right or middle button) is active, and re-syncs the OS cursor once it ends
    void HandleMapCameraDragChanged();

    // Hands the gun trigger and the camera aim (ecliptic) to the combat subsystem
    void UpdateCombatInput();

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

    // Enhanced Input: boost toggle pressed
    void OnBoostAction(const FInputActionValue& value);

    // Enhanced Input: steer hold pressed
    void OnSteerHoldStarted(const FInputActionValue& value);

    // Enhanced Input: steer hold released
    void OnSteerHoldCompleted(const FInputActionValue& value);

    // Enhanced Input: steering toggle pressed
    void OnSteerToggleAction(const FInputActionValue& value);

    // Enhanced Input: stop spin pressed
    void OnStopSpinStarted(const FInputActionValue& value);

    // Enhanced Input: stop spin released
    void OnStopSpinCompleted(const FInputActionValue& value);

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

    // Enhanced Input: surface-lock toggle
    void OnToggleLevelAction(const FInputActionValue& value);

    // Enhanced Input: fire trigger pressed
    void OnFireStarted(const FInputActionValue& value);

    // Enhanced Input: fire trigger released
    void OnFireCompleted(const FInputActionValue& value);

    // Enhanced Input: drop a target
    void OnDropTargetAction(const FInputActionValue& value);

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

    // Enhanced Input: route overlay toggle (P)
    void OnToggleRouteOverlayAction(const FInputActionValue& value);

    // Enhanced Input: resume a paused route (V)
    void OnResumeRouteAction(const FInputActionValue& value);

    // Enhanced Input: speed panel
    void OnSpeedPanelAction(const FInputActionValue& value);

    // Enhanced Input: radar zoom in
    void OnRadarZoomInAction(const FInputActionValue& value);

    // Enhanced Input: radar zoom out
    void OnRadarZoomOutAction(const FInputActionValue& value);

    // Enhanced Input: radar back to AUTO range
    void OnRadarAutoAction(const FInputActionValue& value);

    // Enhanced Input: Shift+M (open the map in the last mode / close it on the map)
    void OnOpenMapAction(const FInputActionValue& value);

    // Enhanced Input: Shift+J (open the map in Jump mode / Jump mode on the map)
    void OnOpenMapJumpAction(const FInputActionValue& value);

    // Enhanced Input: J on the map (Jump mode)
    void OnMapModeJumpAction(const FInputActionValue& value);

    // Enhanced Input: M on the map (Move mode)
    void OnMapModeMoveAction(const FInputActionValue& value);

    // Enhanced Input: P on the map (Patrol mode; again flips loop / ping-pong)
    void OnMapModePatrolAction(const FInputActionValue& value);

    // Enhanced Input: Delete on the map (clear the active route)
    void OnMapClearRouteAction(const FInputActionValue& value);

    // Enhanced Input: jump map close (Esc)
    void OnMapCloseAction(const FInputActionValue& value);

    // Enhanced Input: pause menu (Esc in the ship mapping)
    void OnPauseAction(const FInputActionValue& value);

    // Enhanced Input: F1 in the ship and map mappings; with Ctrl held it acts as Esc (SDD 11 section 3.9)
    void OnMenuHotkeyAction(const FInputActionValue& value);

    // Enhanced Input: F2 in the ship mapping; with Ctrl held it shows or hides the NPC stats overlay
    void OnStatsHotkeyAction(const FInputActionValue& value);

    // Returns whether Left or Right Ctrl is held (the Ctrl+F1 / Ctrl+F2 hotkeys)
    bool IsControlHeld() const;

    // Menu state changed: suspends the ship's control while a menu shows and restores it on resume
    void HandleMenuStateChanged(const SOLMenuState::FMenuState& previous, const SOLMenuState::FMenuState& next);

    // Enhanced Input: map right mouse button pressed
    void OnMapRightStarted(const FInputActionValue& value);

    // Enhanced Input: map right mouse button released
    void OnMapRightCompleted(const FInputActionValue& value);

    // Enhanced Input: map middle mouse button (pan) pressed
    void OnMapPanStarted(const FInputActionValue& value);

    // Enhanced Input: map middle mouse button (pan) released
    void OnMapPanCompleted(const FInputActionValue& value);

    // Enhanced Input: map Shift (pan modifier) pressed
    void OnMapPanModifierStarted(const FInputActionValue& value);

    // Enhanced Input: map Shift (pan modifier) released
    void OnMapPanModifierCompleted(const FInputActionValue& value);

    // Enhanced Input: mouse delta on the map, converted back to pixels
    void OnMapLookAction(const FInputActionValue& value);

    // Enhanced Input: mouse wheel on the map
    void OnMapZoomAction(const FInputActionValue& value);

    // Enhanced Input: map left mouse button (destination pick) pressed
    void OnMapPickStarted(const FInputActionValue& value);

    // Enhanced Input: map left mouse button (destination pick) released
    void OnMapPickCompleted(const FInputActionValue& value);

    // Enhanced Input: map X (clear the destination pick)
    void OnMapClearPickAction(const FInputActionValue& value);

    // Enhanced Input: map Enter (jump to the destination)
    void OnMapJumpAction(const FInputActionValue& value);

    /** Root the ship parts and the camera arm hang from; carries the ship's orientation. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Ship")
    TObjectPtr<USceneComponent> ShipRoot;

    /** Chase-camera arm behind and above the ship, with rotation lag only. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Ship")
    TObjectPtr<USpringArmComponent> SpringArm;

    /** Chase camera; its FOV widens with speed. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Ship")
    TObjectPtr<UCameraComponent> Camera;

    /** Pilot G vision loss (SDD 18): tunnel vignette, gray-out and blackout, blended in by the G meter. */
    UPROPERTY(VisibleAnywhere, Category = "SOL|Ship")
    TObjectPtr<UPostProcessComponent> GVisionPostProcess;

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
    TObjectPtr<UInputAction> StopSpinAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SteerHoldAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SteerToggleAction;

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
    TObjectPtr<UInputAction> ToggleLevelAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> FireAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> DropTargetAction;

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
    TObjectPtr<UInputAction> ToggleRouteOverlayAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ResumeRouteAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> ShiftChordAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> SpeedPanelAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> RadarZoomInAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> RadarZoomOutAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> RadarAutoAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> OpenMapAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> OpenMapJumpAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapModeJumpAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapModeMoveAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapModePatrolAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapClearRouteAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> PauseAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MenuHotkeyAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> StatsHotkeyAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputMappingContext> MapMappingContext;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapCloseAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapRightDragAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapPanAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapPanModifierAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapLookAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapZoomAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapPickAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapClearPickAction;

    UPROPERTY(Transient)
    TObjectPtr<UInputAction> MapJumpAction;

    UPROPERTY(Transient)
    TObjectPtr<USOLSpeedPanelWidget> SpeedPanel;

    UPROPERTY(Transient)
    TObjectPtr<USOLMapModeSubsystem> MapMode;

    UPROPERTY(Transient)
    TObjectPtr<USOLRouteSubsystem> Routes;

    UPROPERTY(Transient)
    TObjectPtr<USOLMenuSubsystem> Menu;

    UPROPERTY(Transient)
    TObjectPtr<USOLSettingsSubsystem> Settings;

    UPROPERTY(Transient)
    TObjectPtr<USOLShipSubsystem> Ships;

    UPROPERTY(Transient)
    TObjectPtr<USOLJumpSubsystem> Jump;

    UPROPERTY(Transient)
    TObjectPtr<USOLTargetingSubsystem> Targeting;

    UPROPERTY(Transient)
    TObjectPtr<USOLAnchorSubsystem> AnchorSubsystem;

    UPROPERTY(Transient)
    TObjectPtr<USOLCombatSubsystem> Combat;

    UPROPERTY(Transient)
    TObjectPtr<USOLSimClockSubsystem> SimClock;

    TWeakObjectPtr<ULocalPlayer> mMappedLocalPlayer;        // Local player that currently has MappingContext added
    TWeakObjectPtr<ULocalPlayer> mMapMappedLocalPlayer;     // Local player that currently has MapMappingContext added
    TUniquePtr<FSOLShipSmokeInput> mSmokeInput;             // Verification-only scripted input (-SOLSmokeInput)
    TUniquePtr<FSOLHudSmoke> mSmokeHud;                     // Verification-only HUD/panel script (-SOLSmokeHud)
    TUniquePtr<FSOLMapSmoke> mSmokeMap;                     // Verification-only jump-map script (-SOLSmokeMap)
    TUniquePtr<FSOLMapPickSmoke> mSmokeMapPick;             // Verification-only map pick script (-SOLSmokeMapPick)
    TUniquePtr<FSOLMapAlignSmoke> mSmokeMapAlign;           // Verification-only map alignment (-SOLSmokeMapAlign)
    TUniquePtr<FSOLRouteSmoke> mSmokeRoute;                 // Verification-only map mode and route script
    TUniquePtr<FSOLRouteOverlaySmoke> mSmokeRouteOverlay;   // Verification-only route overlay script
    TUniquePtr<FSOLJumpSmoke> mSmokeJump;                   // Verification-only pick and jump script (-SOLSmokeJump)
    TUniquePtr<FSOLSurfaceLockSmoke> mSmokeLevel;           // Verification-only surface-lock script (-SOLSmokeLevel)
    TOptional<bool> mSmokeSavedFocusFlush;                  // Input setting a smoke script turned off, restored at EndPlay
    FSOLShipControl mControl;                               // Control composed from the input state
    FSOLSteerGateState mSteerGate;                          // Mouse steering gate: hold, toggle, last result (SDD 19)
    FSOLBoostToggle mBoost;                                 // Boost toggle (SDD 19)
    uint32 mSeenPilotResetCount = 0;                        // Ship subsystem pilot resets already applied to boost
    double mShiftOnlyHeldS = 0.0;                           // AUTO: how long only Shift keys have held flight input
    FVector2D mMapCursorPx = FVector2D::ZeroVector;         // Map cursor (pixels, top-left origin)
    FVector2D mLastOsMousePx = FVector2D::ZeroVector;       // OS mouse position last read for the map cursor
    FVector2d mStickOffsetPx = FVector2d::ZeroVector;       // Virtual joystick offset (X right, Y up), pixels
    FRotator mFreeLookRotation = FRotator::ZeroRotator;     // Camera orbit around the ship while Alt is held
    FQuat mCameraRotation = FQuat::Identity;                // Lagged world rotation of the camera arm
    FDelegateHandle mUniverseUpdatedHandle;                 // Binding to the anchor subsystem's OnUniverseUpdated
    FDelegateHandle mMenuStateHandle;                       // Binding to the menu subsystem's OnStateChanged
    FDelegateHandle mKeyBindingsHandle;                     // Binding to the settings' OnKeyBindingsChanged
    TArray<FString> mBoundKeys;                             // Key names mapped per rebindable action
    double mRollInput = 0.0;                                // Q/E roll input
    double mStickRadiusPx = 1.0;                            // Virtual joystick radius for the current viewport
    double mMouseUnitsToPixels = 1.0;                       // Undoes the Mouse2D axis sensitivity of the input config
    double mWarpBaseFovDeg = 0.0;                           // Camera FOV when the warp effect started
    double mWarpEffectElapsedS = 0.0;                       // Sequence time the warp effect was last computed for
    bool mIsFreeLooking = false;                            // True while Alt is held
    bool mIsFireHeld = false;                               // True while the left mouse (gun trigger) is held
    bool mHasCameraRotation = false;                        // False until the camera rotation is first set
    bool mHadViewportFocus = false;                         // Viewport focus last frame (focus loss is an edge)
    bool mHasCapturedMouse = false;                         // True while this pawn hides the cursor (game-only input)
    bool mIsSpeedPanelOpen = false;                         // True while the F3 panel has focus
    bool mIsSpeedPanelRequested = false;                    // F3 pressed; the panel opens at the next tick
    bool mIsMapOpen = false;                                // True while the jump map is open
    bool mIsMapToggleRequested = false;                     // Shift+M/Esc pressed; the map opens or closes next tick
    bool mIsMapOpenForJump = false;                         // The requested open is Shift+J (Jump mode)
    bool mIsJumpRequested = false;                          // Enter pressed on the map; the jump starts next tick
    bool mIsWarpEffectActive = false;                       // The warp FOV pulse and post-process are applied
    bool mIsMapRightHeld = false;                           // Right mouse held on the map (orbit, or pan with Shift)
    double mMapRightPressS = 0.0;                           // Real time the right button went down (undo click)
    double mMapRightTravelPx = 0.0;                         // Mouse travel since then (undo click vs orbit drag)
    bool mIsMapPanHeld = false;                             // Middle mouse held on the map (pan drag)
    bool mIsMapPanModifierHeld = false;                     // Shift held on the map (right drag pans instead)
    bool mIsMapPickHeld = false;                            // Left mouse held on the map (destination pick drag)
    bool mIsMapCursorScripted = false;                      // Verification: the OS mouse does not move the cursor
    bool mHasOsMousePx = false;                             // mLastOsMousePx holds a reading since the map opened
    bool mIsPauseRequested = false;                         // Esc pressed in flight; handed to the menu next tick
    bool mIsMenuSuspended = false;                          // A menu has the ship's control input suspended
    bool mIsSteering = false;                               // The mouse steers this frame (steering gate active)
    bool mIsViewportFocused = true;                         // The game viewport had focus at the last check
};
