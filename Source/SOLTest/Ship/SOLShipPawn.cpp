/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipPawn.h"

#include "Combat/SOLCombatSubsystem.h"
#include "Flight/SOLNewtonianFlight.h"
#include "Game/SOLInputHelpers.h"
#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Map/SOLRouteSubsystem.h"
#include "Menu/SOLKeyBindings.h"
#include "Menu/SOLMenuState.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsRules.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLHudFormat.h"
#include "UI/SOLSpeedPanelWidget.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"
#include "Visuals/SOLBodyVisuals.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/InputSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Widgets/SViewport.h"

namespace
{
    // Placeholder ship look: light grey-blue hull, darker trim, orange engine glow (linear colors)
    const FLinearColor SHIP_HULL_COLOR(0.55f, 0.6f, 0.68f);
    const FLinearColor SHIP_TRIM_COLOR(0.12f, 0.15f, 0.2f);
    const FLinearColor SHIP_ENGINE_GLOW(8.0f, 3.2f, 0.8f);

    // Viewport layer of the F3 speed panel (above the HUD canvas)
    constexpr int32 SPEED_PANEL_Z_ORDER = 10;

    // Primitive orientation that turns the engine shapes' +Z axis (cylinder and cone) to the ship's +X (forward)
    const FRotator SHIP_ALONG_X(-90.0f, 0.0f, 0.0f);

    // Engine primitive a placeholder ship part uses (indexes the loaded meshes)
    enum class ESOLShipPartShape : uint8
    {
        Cube,
        Cone,
        Cylinder,
        Sphere,
        Count,
    };

    // Material a placeholder ship part uses (indexes the created materials)
    enum class ESOLShipPartLook : uint8
    {
        Hull,
        Trim,
        Glow,
        Count,
    };

    // One placeholder ship part: shape, look, location relative to the root (cm), rotation, scale (primitive units)
    struct FSOLShipPartSpec
    {
        ESOLShipPartShape Shape;
        ESOLShipPartLook Look;
        FVector LocationCm;
        FRotator Rotation;
        FVector Scale;
    };

    // The placeholder ship, about 21 m nose to exhaust with a 16 m wingspan (primitives are 100 cm across)
    const FSOLShipPartSpec SHIP_PARTS[] =
    {
        { ESOLShipPartShape::Cylinder, ESOLShipPartLook::Hull, FVector(0.0, 0.0, 0.0), SHIP_ALONG_X,
            FVector(3.0, 3.0, 14.0) },                                                             // Fuselage
        { ESOLShipPartShape::Cone, ESOLShipPartLook::Hull, FVector(950.0, 0.0, 0.0), SHIP_ALONG_X,
            FVector(3.0, 3.0, 5.0) },                                                              // Nose
        { ESOLShipPartShape::Sphere, ESOLShipPartLook::Trim, FVector(350.0, 0.0, 130.0), FRotator::ZeroRotator,
            FVector(3.5, 1.8, 1.4) },                                                              // Canopy
        { ESOLShipPartShape::Cube, ESOLShipPartLook::Hull, FVector(-250.0, 0.0, -30.0), FRotator::ZeroRotator,
            FVector(5.0, 16.0, 0.35) },                                                            // Wings
        { ESOLShipPartShape::Cube, ESOLShipPartLook::Trim, FVector(-550.0, 0.0, 260.0), FRotator::ZeroRotator,
            FVector(3.5, 0.3, 3.2) },                                                              // Tail fin
        { ESOLShipPartShape::Cylinder, ESOLShipPartLook::Trim, FVector(-600.0, 220.0, -20.0), SHIP_ALONG_X,
            FVector(1.6, 1.6, 4.0) },                                                              // Engine R
        { ESOLShipPartShape::Cylinder, ESOLShipPartLook::Trim, FVector(-600.0, -220.0, -20.0), SHIP_ALONG_X,
            FVector(1.6, 1.6, 4.0) },                                                              // Engine L
        { ESOLShipPartShape::Sphere, ESOLShipPartLook::Glow, FVector(-805.0, 220.0, -20.0), FRotator::ZeroRotator,
            FVector(0.5, 1.3, 1.3) },                                                              // Glow R
        { ESOLShipPartShape::Sphere, ESOLShipPartLook::Glow, FVector(-805.0, -220.0, -20.0), FRotator::ZeroRotator,
            FVector(0.5, 1.3, 1.3) },                                                              // Glow L
    };

    //////////////////////////////////////////////////////////////////////////
    // Creates a dynamic material instance of the ship hull material with one base color
    UMaterialInstanceDynamic* SOLShipPawnCreateHullMaterial(UMaterialInterface* base, UObject* outer,
        const FLinearColor& color)
    {
        UMaterialInstanceDynamic* material = UMaterialInstanceDynamic::Create(base, outer);
        material->SetVectorParameterValue(FName(SOL::SHIP_HULL_COLOR_PARAM), color);
        return material;
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the root, spring arm and camera, and enables ticking before physics
ASOLShipPawn::ASOLShipPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    ShipRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ShipRoot"));
    ShipRoot->SetMobility(EComponentMobility::Movable);
    RootComponent = ShipRoot;

    // Chase camera: behind and above the ship, no position lag so the camera never trails the ship at speed. The
    // rotation lag is applied by the pawn (UpdateCameraRotation) after the ship orientation update, not by the arm,
    // whose tick runs before that update and would step rigidly with the ship and then lag (a double step)
    SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
    SpringArm->SetupAttachment(ShipRoot);
    SpringArm->TargetArmLength = SOL::SHIP_CAMERA_ARM_LENGTH_CM;
    SpringArm->SocketOffset = FVector(0.0, 0.0, SOL::SHIP_CAMERA_HEIGHT_CM);
    SpringArm->bDoCollisionTest = false;
    SpringArm->bUsePawnControlRotation = false;
    SpringArm->bEnableCameraLag = false;
    SpringArm->bEnableCameraRotationLag = false;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;
    Camera->SetFieldOfView(static_cast<float>(SOL::SHIP_CAMERA_BASE_FOV_DEG));

    // Jump warp post-process (engine features, no material asset): the overrides are set once and blended in by the
    // warp's streak intensity through the blend weight, which stays 0 outside a jump
    FPostProcessSettings& warpPostProcess = Camera->PostProcessSettings;
    warpPostProcess.bOverride_SceneFringeIntensity = true;
    warpPostProcess.SceneFringeIntensity = SOL::WARP_FRINGE_INTENSITY;
    warpPostProcess.bOverride_VignetteIntensity = true;
    warpPostProcess.VignetteIntensity = SOL::WARP_VIGNETTE_INTENSITY;
    warpPostProcess.bOverride_SceneColorTint = true;
    warpPostProcess.SceneColorTint = SOL::WARP_SCENE_TINT;
    Camera->PostProcessBlendWeight = 0.0f;

    // Pilot G vision loss (SDD 18; engine features, no material asset): an unbound post-process whose overrides are
    // the full blackout look, blended in by SOLNewtonian::GVisionLoss, so it narrows, grays and darkens together
    GVisionPostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("GVisionPostProcess"));
    GVisionPostProcess->SetupAttachment(RootComponent);
    GVisionPostProcess->bUnbound = true;
    GVisionPostProcess->Priority = SOL::G_VISION_POST_PROCESS_PRIORITY;
    GVisionPostProcess->BlendWeight = 0.0f;
    FPostProcessSettings& visionPostProcess = GVisionPostProcess->Settings;
    visionPostProcess.bOverride_VignetteIntensity = true;
    visionPostProcess.VignetteIntensity = SOL::G_VISION_VIGNETTE_INTENSITY;
    visionPostProcess.bOverride_ColorSaturation = true;
    visionPostProcess.ColorSaturation = FVector4(SOL::G_VISION_SATURATION, SOL::G_VISION_SATURATION,
        SOL::G_VISION_SATURATION, 1.0);
    visionPostProcess.bOverride_SceneColorTint = true;
    visionPostProcess.SceneColorTint = SOL::G_VISION_SCENE_TINT;

    // Shadowless fill "headlight" along the view so the ship's side facing away from the Sun is not black
    FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
    FillLight->SetupAttachment(Camera);
    FillLight->SetRelativeRotation(FRotator(SOL::SHIP_FILL_LIGHT_PITCH_DEG, 0.0f, 0.0f));
    FillLight->SetMobility(EComponentMobility::Movable);
    FillLight->SetIntensity(SOL::SUN_ILLUMINANCE_LUX * SOL::SHIP_FILL_LIGHT_FRACTION);
    FillLight->SetCastShadows(false);
    FillLight->LightingChannels.bChannel1 = true;   // The ship is on channel 1 (SDD 16, 16d)

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems, becomes the observer actor, builds the ship mesh and starts the input smoke script
void ASOLShipPawn::BeginPlay()
{
    Super::BeginPlay();

    UWorld* world = GetWorld();
    Ships = world->GetSubsystem<USOLShipSubsystem>();
    Targeting = world->GetSubsystem<USOLTargetingSubsystem>();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    SimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    MapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    Routes = world->GetSubsystem<USOLRouteSubsystem>();
    Jump = world->GetSubsystem<USOLJumpSubsystem>();
    Combat = world->GetSubsystem<USOLCombatSubsystem>();
    Menu = world->GetSubsystem<USOLMenuSubsystem>();
    if (Ships == nullptr || Targeting == nullptr || AnchorSubsystem == nullptr || SimClock == nullptr
        || !Ships->HasPlayerShip())
    {
        UE_LOG(LogSOL, Error, TEXT("ShipPawn %s: ship or universe subsystems missing; the pawn is inert"), *GetName());
        return;
    }

    // Start state per SDD 2: cap at the start value, stick centered. Flight assist keeps what the ship's spawn set
    // (USOLShipSubsystem::SpawnPlayerShip: the settings' launch value, SDD 8, or off for a -SOLRingStartKm start)
    Settings = USOLSettingsSubsystem::Get(this);
    mControl = Ships->GetControl();
    mControl.Thrust = FVector3d::ZeroVector;
    mControl.Rotation = FVector3d::ZeroVector;
    mControl.bBoost = false;
    mControl.bStopSpin = false;
    mControl.SpeedCapMps = SOL::SHIP_START_SPEED_CAP_MPS;
    mControl.bFlightAssist = SOLFlightMode::FlightAssistFor(Ships->GetFlightMode());

    // Mouse steering starts on at every flight start (SDD 19 Q7), boost off; neither is saved. Every spawn or respawn
    // creates a new pawn, so this is the only reset of the steer toggle: a jump arrival (USOLShipSubsystem::
    // ResetPilotState) turns boost off but keeps the toggle (SDD 19 Amendment 3)
    mSteerGate = SOLFlightMode::MakeSteerGate(SOL::SHIP_STEER_TOGGLE_START_ON);
    SOLFlightMode::ResetBoost(mBoost);
    mSeenPilotResetCount = Ships->GetPilotResetCount();
    BuildShipMesh();

    // The pawn represents the observer: rebases move it, and it follows the ship after every universe update
    AnchorSubsystem->SetObserverActor(this);
    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this, &ASOLShipPawn::FollowShip);
    FollowShip();

    // Menus suspend and restore the ship's control as the map does
    if (Menu != nullptr)
    {
        mMenuStateHandle = Menu->OnStateChanged().AddUObject(this, &ASOLShipPawn::HandleMenuStateChanged);
    }

    // Applied rebinds map the ship's keys again
    if (Settings != nullptr)
    {
        mKeyBindingsHandle = Settings->OnKeyBindingsChanged().AddUObject(this, &ASOLShipPawn::HandleKeyBindingsChanged);
    }

    // Verification hook: drive the real input pipeline through a fixed script, log checkpoints, then quit
    if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_INPUT))
    {
        mSmokeInput = MakeUnique<FSOLShipSmokeInput>();
        mSmokeInput->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_HUD))
    {
        mSmokeHud = MakeUnique<FSOLHudSmoke>();
        mSmokeHud->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_MAP))
    {
        mSmokeMap = MakeUnique<FSOLMapSmoke>();
        mSmokeMap->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_MAP_PICK))
    {
        mSmokeMapPick = MakeUnique<FSOLMapPickSmoke>();
        mSmokeMapPick->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_MAP_ALIGN))
    {
        mSmokeMapAlign = MakeUnique<FSOLMapAlignSmoke>();
        mSmokeMapAlign->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_ROUTE))
    {
        mSmokeRoute = MakeUnique<FSOLRouteSmoke>();
        mSmokeRoute->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_ROUTE_OVERLAY))
    {
        mSmokeRouteOverlay = MakeUnique<FSOLRouteOverlaySmoke>();
        mSmokeRouteOverlay->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_HOLOGRAM))
    {
        mSmokeHologram = MakeUnique<FSOLHologramSmoke>();
        mSmokeHologram->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_JUMP))
    {
        mSmokeJump = MakeUnique<FSOLJumpSmoke>();
        mSmokeJump->Start(*this);
    }
    else if (FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_LEVEL))
    {
        mSmokeLevel = MakeUnique<FSOLSurfaceLockSmoke>();
        mSmokeLevel->Start(*this);
    }

    // A script's synthetic held keys (e.g. Shift through the map pick's height preview) must survive the OS window
    // losing focus, which otherwise flushes every pressed key; in memory only, restored at EndPlay
    if (mSmokeInput.IsValid() || mSmokeHud.IsValid() || mSmokeMap.IsValid() || mSmokeMapPick.IsValid()
        || mSmokeMapAlign.IsValid() || mSmokeRoute.IsValid() || mSmokeRouteOverlay.IsValid()
        || mSmokeHologram.IsValid() || mSmokeJump.IsValid()
        || mSmokeLevel.IsValid())
    {
        UInputSettings* inputSettings = GetMutableDefault<UInputSettings>();
        mSmokeSavedFocusFlush = inputSettings->bShouldFlushPressedKeysOnViewportFocusLost;
        inputSettings->bShouldFlushPressedKeysOnViewportFocusLost = false;
    }
}

//////////////////////////////////////////////////////////////////////////
// Removes the mapping context, releases the mouse and unhooks from the universe update
void ASOLShipPawn::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    DismissSpeedPanel();
    DismissMap();
    RemoveMappingContext();
    ReleaseMouse();
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->OnUniverseUpdated().Remove(mUniverseUpdatedHandle);
        AnchorSubsystem->SetObserverActor(nullptr);
    }
    if (Combat != nullptr)
    {
        Combat->SetPlayerTriggerHeld(false);
    }
    if (Menu != nullptr)
    {
        Menu->OnStateChanged().Remove(mMenuStateHandle);
    }
    mMenuStateHandle.Reset();
    if (Settings != nullptr)
    {
        Settings->OnKeyBindingsChanged().Remove(mKeyBindingsHandle);
    }
    mKeyBindingsHandle.Reset();
    mIsFireHeld = false;
    mUniverseUpdatedHandle.Reset();
    mSmokeInput.Reset();
    mSmokeHud.Reset();
    mSmokeMap.Reset();
    mSmokeMapPick.Reset();
    mSmokeMapAlign.Reset();
    mSmokeRoute.Reset();
    mSmokeRouteOverlay.Reset();
    mSmokeHologram.Reset();
    mSmokeJump.Reset();
    mSmokeLevel.Reset();
    if (mSmokeSavedFocusFlush.IsSet())
    {
        GetMutableDefault<UInputSettings>()->bShouldFlushPressedKeysOnViewportFocusLost
            = mSmokeSavedFocusFlush.GetValue();
        mSmokeSavedFocusFlush.Reset();
    }
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Composes and sends the ship control, updates the camera FOV and free-look, and runs the input smoke script
void ASOLShipPawn::Tick(const float deltaSeconds)
{
    Super::Tick(deltaSeconds);
    if (Ships == nullptr || !Ships->HasPlayerShip())
    {
        return;
    }

    // The script injects key events now; the controller processes them at the start of the next frame
    if (mSmokeInput.IsValid() && mSmokeInput->Update(*this, deltaSeconds))
    {
        mSmokeInput.Reset();
    }
    if (mSmokeHud.IsValid() && mSmokeHud->Update(*this, deltaSeconds))
    {
        mSmokeHud.Reset();
    }
    if (mSmokeMap.IsValid() && mSmokeMap->Update(*this, deltaSeconds))
    {
        mSmokeMap.Reset();
    }
    if (mSmokeMapPick.IsValid() && mSmokeMapPick->Update(*this, deltaSeconds))
    {
        mSmokeMapPick.Reset();
    }
    if (mSmokeMapAlign.IsValid() && mSmokeMapAlign->Update(*this, deltaSeconds))
    {
        mSmokeMapAlign.Reset();
    }
    if (mSmokeRoute.IsValid() && mSmokeRoute->Update(*this, deltaSeconds))
    {
        mSmokeRoute.Reset();
    }
    if (mSmokeRouteOverlay.IsValid() && mSmokeRouteOverlay->Update(*this, deltaSeconds))
    {
        mSmokeRouteOverlay.Reset();
    }
    if (mSmokeHologram.IsValid() && mSmokeHologram->Update(*this, deltaSeconds))
    {
        mSmokeHologram.Reset();
    }
    if (mSmokeJump.IsValid() && mSmokeJump->Update(*this, deltaSeconds))
    {
        mSmokeJump.Reset();
    }
    if (mSmokeLevel.IsValid() && mSmokeLevel->Update(*this, deltaSeconds))
    {
        mSmokeLevel.Reset();
    }

    // F3 or J/Esc was pressed during input processing; the panel or map opens (or closes) here, outside the Enhanced
    // Input callback
    if (mIsSpeedPanelRequested)
    {
        mIsSpeedPanelRequested = false;
        OpenSpeedPanel();
    }
    if (mIsMapToggleRequested)
    {
        mIsMapToggleRequested = false;
        if (mIsMapOpen)
        {
            CloseMap();
        }
        else if (IsJumpWarping())
        {
            UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: jump map not opened during the jump warp"), *GetName());
        }
        else
        {
            OpenMap(mIsMapOpenForJump);
        }
    }

    // Esc was pressed in flight; the menu subsystem opens the pause menu at its next frame
    if (mIsPauseRequested)
    {
        mIsPauseRequested = false;
        if (Menu != nullptr && !mIsMapOpen)
        {
            Menu->RequestEscape();
        }
    }

    // Enter was pressed on the map; the jump starts (and the map closes) or the draft route starts here, outside the
    // Enhanced Input callback
    if (mIsJumpRequested)
    {
        mIsJumpRequested = false;
        if (MapMode != nullptr && MapMode->GetEnterAction() == ESOLMapEnterAction::StartRoute)
        {
            MapMode->StartDraftRoute();
        }
        else
        {
            StartMapJump();
        }
    }
    UpdateViewportFocus();
    UpdateMapCursor();
    UpdateSteering();

    // Virtual joystick: radius from the current viewport, offset mapped through the dead zone and response curve
    if (const APlayerController* playerController = Cast<APlayerController>(GetController()))
    {
        int32 viewportWidth = 0;
        int32 viewportHeight = 0;
        playerController->GetViewportSize(viewportWidth, viewportHeight);
        if (viewportWidth > 0 && viewportHeight > 0)
        {
            mStickRadiusPx = SOL::JOYSTICK_RADIUS_FRACTION * FMath::Min(viewportWidth, viewportHeight);
        }
    }
    mStickOffsetPx = mStickOffsetPx.GetClampedToMaxSize(mStickRadiusPx);
    const FVector2d stick = SOLFlightMode::GateSteer(SOLFlight::JoystickToRotation(mStickOffsetPx / mStickRadiusPx,
        SOL::JOYSTICK_DEAD_ZONE, SOL::JOYSTICK_EXPONENT), mIsSteering);
    mControl.Rotation = FVector3d(mRollInput, stick.Y, stick.X);

    // Boost: off after a jump arrival or respawn, and at a blackout (it never turns itself back on)
    if (Ships->GetPilotResetCount() != mSeenPilotResetCount)
    {
        mSeenPilotResetCount = Ships->GetPilotResetCount();
        SOLFlightMode::ResetBoost(mBoost);
    }
    SOLFlightMode::UpdateBoost(mBoost, Ships->PilotHasControl());
    mControl.bBoost = mBoost.bOn;

    // The gated pilot input goes through the autopilot's cancel rule first, then the mode sets the assist flag
    if (!Ships->IsControlScripted())
    {
        FSOLPilotInput pilotInput;
        pilotInput.Thrust = mControl.Thrust;
        pilotInput.Roll = mRollInput;
        pilotInput.Steer = stick;
        pilotInput.bStopSpin = mControl.bStopSpin;

        // Shift is both a flight key (thrust up by default) and the map chord (SDD 20): in AUTO, input held only on
        // Shift keys waits SHIFT_CHORD_GRACE_S before it counts, so Shift+M / Shift+J do not cancel the autopilot
        const APlayerController* playerController = Cast<APlayerController>(GetController());
        const uint16 channels = SOLFlightMode::ActiveChannels(pilotInput);
        const bool bShiftOnly = channels != 0 && playerController != nullptr
            && Ships->GetFlightMode() == ESOLFlightMode::Auto
            && (channels & ~SOLFlightMode::ActiveChannels(HeldKeyPilotInput(*playerController, true))) == 0;
        mShiftOnlyHeldS = bShiftOnly ? mShiftOnlyHeldS + deltaSeconds : 0.0;
        if (bShiftOnly && mShiftOnlyHeldS < SOL::Route::SHIFT_CHORD_GRACE_S)
        {
            pilotInput = FSOLPilotInput();
        }
        Ships->ApplyPilotInput(pilotInput);
        mControl.bFlightAssist = SOLFlightMode::FlightAssistFor(Ships->GetFlightMode());
        Ships->SetControl(mControl);
    }
    UpdateCombatInput();

    // Pilot G vision loss from the meter the flight step left (SDD 18)
    GVisionPostProcess->BlendWeight = Ships->HasPlayerShip()
        ? static_cast<float>(SOLNewtonian::GVisionLoss(Ships->GetGMeter(), Ships->GetFlightParams().PilotLimits))
        : 0.0f;

    UpdateCameraFov(deltaSeconds);
}

//////////////////////////////////////////////////////////////////////////
// Hands the gun trigger and the camera aim (ecliptic) to the combat subsystem
void ASOLShipPawn::UpdateCombatInput()
{
    if (Combat == nullptr)
    {
        return;
    }
    Combat->SetPlayerTriggerHeld(mIsFireHeld && !mIsMapOpen && !mIsSpeedPanelOpen && !mIsMenuSuspended
        && !IsJumpWarping());

    // Render space has Unreal-handed axes around the render origin, so the camera's offset from the actor (the ship)
    // and its forward are Unreal-handed universe vectors; EclipticToUnreal (a Y flip) turns them into ecliptic ones
    const FVector3d cameraOffsetM = FVector3d(Camera->GetComponentLocation() - GetActorLocation()) * SOL::CM_TO_METERS;
    Combat->SetPlayerAim(SOLRender::EclipticToUnreal(cameraOffsetM),
        SOLRender::EclipticToUnreal(FVector3d(Camera->GetForwardVector())));
}

//////////////////////////////////////////////////////////////////////////
// Sets the chase camera's FOV: the warp pulse and post-process during a jump, else the speed-based widening
void ASOLShipPawn::UpdateCameraFov(const float deltaSeconds)
{
    // Jump warp: the FOV pulses from the value it had when the warp began and the post-process fades in and out on
    // the streak curve; both follow the sequence's clock rather than easing
    if (IsJumpWarping())
    {
        if (!mIsWarpEffectActive)
        {
            mWarpBaseFovDeg = Camera->FieldOfView;
            mIsWarpEffectActive = true;
        }
        mWarpEffectElapsedS = Jump->GetElapsedS();
        Camera->SetFieldOfView(static_cast<float>(Jump->ComputeFovDeg(mWarpBaseFovDeg)));
        Camera->PostProcessBlendWeight = static_cast<float>(Jump->ComputeStreakIntensity());
        return;
    }
    if (mIsWarpEffectActive)
    {
        EndWarpEffect();
        return;
    }

    // FOV widens on a log scale of the speed relative to the active reference frame
    const double relativeSpeedMps = (Ships->GetState().VelocityMps - Ships->GetReferenceVelocityMps()).Size();
    const double speedFraction = FMath::Clamp(
        FMath::LogX(10.0, FMath::Max(relativeSpeedMps, SOL::SHIP_CAMERA_FOV_MIN_SPEED_MPS)
            / SOL::SHIP_CAMERA_FOV_MIN_SPEED_MPS)
        / FMath::LogX(10.0, SOL::SHIP_CAMERA_FOV_MAX_SPEED_MPS / SOL::SHIP_CAMERA_FOV_MIN_SPEED_MPS), 0.0, 1.0);
    const float targetFov = static_cast<float>(FMath::Lerp(SOL::SHIP_CAMERA_BASE_FOV_DEG, SOL::SHIP_CAMERA_MAX_FOV_DEG,
        speedFraction));
    Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, targetFov, deltaSeconds,
        SOL::SHIP_CAMERA_FOV_INTERP_SPEED));
}

//////////////////////////////////////////////////////////////////////////
// Ends the warp effect: back to the FOV it started from, post-process off; the speed-based easing resumes from here
void ASOLShipPawn::EndWarpEffect()
{
    mIsWarpEffectActive = false;
    Camera->SetFieldOfView(static_cast<float>(mWarpBaseFovDeg));
    Camera->PostProcessBlendWeight = 0.0f;
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the jump warp sequence runs
bool ASOLShipPawn::IsJumpWarping() const
{
    return Jump != nullptr && Jump->IsWarping();
}

//////////////////////////////////////////////////////////////////////////
// Returns the chase camera's current horizontal field of view (degrees)
float ASOLShipPawn::GetCameraFovDeg() const
{
    return Camera->FieldOfView;
}

//////////////////////////////////////////////////////////////////////////
// Returns the blend weight of the camera's warp post-process (0 outside a jump warp)
float ASOLShipPawn::GetWarpPostProcessWeight() const
{
    return Camera->PostProcessBlendWeight;
}

//////////////////////////////////////////////////////////////////////////
// Returns the pilot G vision-loss post-process weight this frame (0 = clear, 1 = blacked out)
float ASOLShipPawn::GetGVisionWeight() const
{
    return GVisionPostProcess->BlendWeight;
}

//////////////////////////////////////////////////////////////////////////
// Hands the map's locked destination to the jump sequence and closes the map as J / Esc would
void ASOLShipPawn::StartMapJump()
{
    // RequestJump validates (at least the planar lock) and logs; without one the map stays open and unchanged
    if (!mIsMapOpen || MapMode == nullptr || Jump == nullptr || !MapMode->RequestJump())
    {
        return;
    }
    if (!Jump->StartJump(MapMode->GetPickState()))
    {
        UE_LOG(LogSOL, Warning, TEXT("ShipPawn %s: jump not started; the map stays open"), *GetName());
        return;
    }

    // The jump copied the pick, so the normal close (which clears the pick) is safe
    CloseMap();
}

//////////////////////////////////////////////////////////////////////////
// Builds the input objects and binds the actions
void ASOLShipPawn::SetupPlayerInputComponent(UInputComponent* playerInputComponent)
{
    Super::SetupPlayerInputComponent(playerInputComponent);
    CreateInputObjects();

    UEnhancedInputComponent* input = Cast<UEnhancedInputComponent>(playerInputComponent);
    if (input == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("ShipPawn %s: Enhanced Input component missing"), *GetName());
        return;
    }

    // Held inputs report every frame while actuated and once more on release
    input->BindAction(ThrustAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnThrustAction);
    input->BindAction(ThrustAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnThrustCompleted);
    input->BindAction(RollAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRollAction);
    input->BindAction(RollAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnRollCompleted);
    input->BindAction(SteerHoldAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnSteerHoldStarted);
    input->BindAction(SteerHoldAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnSteerHoldCompleted);
    input->BindAction(StopSpinAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnStopSpinStarted);
    input->BindAction(StopSpinAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnStopSpinCompleted);
    input->BindAction(FireAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnFireStarted);
    input->BindAction(FireAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnFireCompleted);
    input->BindAction(FreeLookAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnFreeLookStarted);
    input->BindAction(FreeLookAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnFreeLookCompleted);
    input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnLookAction);
    input->BindAction(SpeedCapAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnSpeedCapAction);

    // One-shot keys trigger once per press
    input->BindAction(ToggleAssistAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnToggleAssistAction);
    input->BindAction(BoostAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnBoostAction);
    input->BindAction(SteerToggleAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnSteerToggleAction);
    input->BindAction(RecenterAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRecenterAction);
    input->BindAction(MatchLockAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMatchLockAction);
    input->BindAction(ToggleLevelAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnToggleLevelAction);
    input->BindAction(DropTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnDropTargetAction);
    input->BindAction(SelectTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnSelectTargetAction);
    input->BindAction(NextTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnNextTargetAction);
    input->BindAction(PreviousTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnPreviousTargetAction);
    input->BindAction(ClearTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnClearTargetAction);
    input->BindAction(WarpUpAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnWarpUpAction);
    input->BindAction(WarpDownAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnWarpDownAction);
    input->BindAction(WarpResetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnWarpResetAction);
    input->BindAction(ToggleOrbitLinesAction, ETriggerEvent::Triggered, this,
        &ASOLShipPawn::OnToggleOrbitLinesAction);
    input->BindAction(ToggleRouteOverlayAction, ETriggerEvent::Triggered, this,
        &ASOLShipPawn::OnToggleRouteOverlayAction);
    input->BindAction(ResumeRouteAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnResumeRouteAction);
    input->BindAction(SpeedPanelAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnSpeedPanelAction);
    input->BindAction(RadarZoomInAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRadarZoomInAction);
    input->BindAction(RadarZoomOutAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRadarZoomOutAction);
    input->BindAction(RadarAutoAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRadarAutoAction);

    // Map: Shift+M / Shift+J open it (mapped in both contexts: on the map Shift+M closes and Shift+J is Jump mode);
    // the rest only exist in the map's context while it is open
    input->BindAction(OpenMapAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnOpenMapAction);
    input->BindAction(OpenMapJumpAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnOpenMapJumpAction);
    input->BindAction(MapModeJumpAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapModeJumpAction);
    input->BindAction(MapModeMoveAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapModeMoveAction);
    input->BindAction(MapModePatrolAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapModePatrolAction);
    input->BindAction(MapClearRouteAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapClearRouteAction);
    input->BindAction(MapCloseAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapCloseAction);
    input->BindAction(PauseAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnPauseAction);
    input->BindAction(MenuHotkeyAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMenuHotkeyAction);
    input->BindAction(StatsHotkeyAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnStatsHotkeyAction);
    input->BindAction(MapRightDragAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnMapRightStarted);
    input->BindAction(MapRightDragAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnMapRightCompleted);
    input->BindAction(MapPanAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnMapPanStarted);
    input->BindAction(MapPanAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnMapPanCompleted);
    input->BindAction(MapPanModifierAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnMapPanModifierStarted);
    input->BindAction(MapPanModifierAction, ETriggerEvent::Completed, this,
        &ASOLShipPawn::OnMapPanModifierCompleted);
    input->BindAction(MapLookAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapLookAction);
    input->BindAction(MapZoomAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapZoomAction);
    input->BindAction(MapPickAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnMapPickStarted);
    input->BindAction(MapPickAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnMapPickCompleted);
    input->BindAction(MapClearPickAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapClearPickAction);
    input->BindAction(MapJumpAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapJumpAction);
}

//////////////////////////////////////////////////////////////////////////
// Moves the mapping context to the new local controller and captures the mouse for the virtual joystick
void ASOLShipPawn::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();
    CreateInputObjects();
    RemoveMappingContext();

    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (playerController == nullptr)
    {
        return;
    }
    if (SOLInput::AddMappingContext(playerController->GetLocalPlayer(), MappingContext, 0))
    {
        mMappedLocalPlayer = playerController->GetLocalPlayer();
    }

    // The OS cursor is hidden and locked to the viewport; the mouse only moves the virtual stick (undone by ReleaseMouse)
    playerController->bShowMouseCursor = false;
    playerController->SetInputMode(FInputModeGameOnly());
    mHasCapturedMouse = true;

    // Mouse2D arrives scaled by the project's axis sensitivity (DefaultInput.ini); undo it to get pixels
    FInputAxisProperties mouseAxis;
    mMouseUnitsToPixels = 1.0;
    if (playerController->PlayerInput != nullptr
        && playerController->PlayerInput->GetAxisProperties(EKeys::Mouse2D, mouseAxis) && mouseAxis.Sensitivity > 0.0f)
    {
        mMouseUnitsToPixels = 1.0 / mouseAxis.Sensitivity;
    }
}

//////////////////////////////////////////////////////////////////////////
// Removes the mapping context and releases the mouse before the controller lets go of this pawn
void ASOLShipPawn::UnPossessed()
{
    DismissSpeedPanel();
    DismissMap();
    RemoveMappingContext();
    ReleaseMouse();
    Super::UnPossessed();
}

//////////////////////////////////////////////////////////////////////////
// Undoes the mouse capture: shows the cursor and restores game-and-UI input on the controller that had it
void ASOLShipPawn::ReleaseMouse()
{
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (!mHasCapturedMouse || playerController == nullptr)
    {
        return;
    }
    playerController->bShowMouseCursor = true;
    playerController->SetInputMode(FInputModeGameAndUI());
    mHasCapturedMouse = false;
}

//////////////////////////////////////////////////////////////////////////
// Detects the viewport losing focus (alt-tab, another window) and drops the held flight input once when it does
void ASOLShipPawn::UpdateViewportFocus()
{
    // The input scripts inject synthetic events whatever the OS focus is, so focus is ignored while they run; the
    // open speed panel and the open map have already released the flight input
    if (mSmokeInput.IsValid() || mSmokeHud.IsValid() || mSmokeMap.IsValid() || mSmokeMapPick.IsValid()
        || mSmokeMapAlign.IsValid() || mSmokeRoute.IsValid() || mSmokeRouteOverlay.IsValid()
        || mSmokeHologram.IsValid() || mSmokeJump.IsValid()
        || mSmokeLevel.IsValid())
    {
        mIsViewportFocused = true;
        return;
    }
    if (mIsSpeedPanelOpen || mIsMapOpen || mIsMenuSuspended)
    {
        return;
    }
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    const ULocalPlayer* localPlayer = playerController != nullptr ? playerController->GetLocalPlayer() : nullptr;
    const UGameViewportClient* viewportClient = localPlayer != nullptr ? localPlayer->ViewportClient.Get() : nullptr;
    const FViewport* viewport = viewportClient != nullptr ? viewportClient->Viewport : nullptr;
    const bool bHasFocus = viewport != nullptr && viewport->HasFocus() && FApp::HasFocus();
    if (mHadViewportFocus && !bHasFocus)
    {
        HandleFocusLost();
    }
    mHadViewportFocus = bHasFocus;
    mIsViewportFocused = bHasFocus;
}

//////////////////////////////////////////////////////////////////////////
// Recenters the virtual stick and zeroes thrust and roll, so nothing stays held while the game is unfocused; the
// steer hold is released and the boost toggle survives (SDD 19 Q12)
void ASOLShipPawn::HandleFocusLost()
{
    mStickOffsetPx = FVector2d::ZeroVector;
    mRollInput = 0.0;
    SOLFlightMode::SetSteerHold(mSteerGate, false);
    mControl.Thrust = FVector3d::ZeroVector;
    mControl.Rotation = FVector3d::ZeroVector;
    mControl.bStopSpin = false;
    mIsFireHeld = false;
    if (Ships != nullptr && !Ships->IsControlScripted())
    {
        Ships->SetControl(mControl);
    }
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: viewport lost focus; stick recentered, thrust/roll/steer/trigger released"),
        *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Removes the mapping context from the local player it was added to, if any
void ASOLShipPawn::RemoveMappingContext()
{
    SOLInput::RemoveMappingContext(mMappedLocalPlayer.Get(), MappingContext);
    mMappedLocalPlayer.Reset();
}

//////////////////////////////////////////////////////////////////////////
// Creates the input actions and the mapping context once
void ASOLShipPawn::CreateInputObjects()
{
    if (MappingContext != nullptr)
    {
        return;
    }
    using namespace SOLInput;
    ThrustAction = CreateAction(this, TEXT("IA_ShipThrust"), EInputActionValueType::Axis3D);
    RollAction = CreateAction(this, TEXT("IA_ShipRoll"), EInputActionValueType::Axis1D);
    BoostAction = CreateAction(this, TEXT("IA_ShipBoost"), EInputActionValueType::Boolean);
    StopSpinAction = CreateAction(this, TEXT("IA_ShipStopSpin"), EInputActionValueType::Boolean);
    SteerHoldAction = CreateAction(this, TEXT("IA_ShipSteerHold"), EInputActionValueType::Boolean);
    SteerToggleAction = CreateAction(this, TEXT("IA_ShipSteerToggle"), EInputActionValueType::Boolean);
    ToggleAssistAction = CreateAction(this, TEXT("IA_ShipToggleAssist"), EInputActionValueType::Boolean);
    FreeLookAction = CreateAction(this, TEXT("IA_ShipFreeLook"), EInputActionValueType::Boolean);
    LookAction = CreateAction(this, TEXT("IA_ShipLook"), EInputActionValueType::Axis2D);
    RecenterAction = CreateAction(this, TEXT("IA_ShipRecenterStick"), EInputActionValueType::Boolean);
    SpeedCapAction = CreateAction(this, TEXT("IA_ShipSpeedCap"), EInputActionValueType::Axis1D);
    MatchLockAction = CreateAction(this, TEXT("IA_ShipMatchLock"), EInputActionValueType::Boolean);
    ToggleLevelAction = CreateAction(this, TEXT("IA_ShipLevel"), EInputActionValueType::Boolean);
    FireAction = CreateAction(this, TEXT("IA_ShipFire"), EInputActionValueType::Boolean);
    DropTargetAction = CreateAction(this, TEXT("IA_ShipDropTarget"), EInputActionValueType::Boolean);
    SelectTargetAction = CreateAction(this, TEXT("IA_ShipSelectTarget"), EInputActionValueType::Boolean);
    NextTargetAction = CreateAction(this, TEXT("IA_ShipNextTarget"), EInputActionValueType::Boolean);
    PreviousTargetAction = CreateAction(this, TEXT("IA_ShipPreviousTarget"), EInputActionValueType::Boolean);
    ClearTargetAction = CreateAction(this, TEXT("IA_ShipClearTarget"), EInputActionValueType::Boolean);
    WarpUpAction = CreateAction(this, TEXT("IA_ShipWarpUp"), EInputActionValueType::Boolean);
    WarpDownAction = CreateAction(this, TEXT("IA_ShipWarpDown"), EInputActionValueType::Boolean);
    WarpResetAction = CreateAction(this, TEXT("IA_ShipWarpReset"), EInputActionValueType::Boolean);
    ToggleOrbitLinesAction = CreateAction(this, TEXT("IA_ShipToggleOrbitLines"), EInputActionValueType::Boolean);
    ToggleRouteOverlayAction = CreateAction(this, TEXT("IA_ShipToggleRouteOverlay"), EInputActionValueType::Boolean);
    ResumeRouteAction = CreateAction(this, TEXT("IA_RouteResume"), EInputActionValueType::Boolean);
    ShiftChordAction = CreateAction(this, TEXT("IA_ShiftChord"), EInputActionValueType::Boolean);
    SpeedPanelAction = CreateAction(this, TEXT("IA_ShipSpeedPanel"), EInputActionValueType::Boolean);
    RadarZoomInAction = CreateAction(this, TEXT("IA_ShipRadarZoomIn"), EInputActionValueType::Boolean);
    RadarZoomOutAction = CreateAction(this, TEXT("IA_ShipRadarZoomOut"), EInputActionValueType::Boolean);
    RadarAutoAction = CreateAction(this, TEXT("IA_ShipRadarAuto"), EInputActionValueType::Boolean);
    OpenMapAction = CreateAction(this, TEXT("IA_MapOpen"), EInputActionValueType::Boolean);
    OpenMapJumpAction = CreateAction(this, TEXT("IA_MapOpenJump"), EInputActionValueType::Boolean);
    MapModeJumpAction = CreateAction(this, TEXT("IA_MapModeJump"), EInputActionValueType::Boolean);
    MapModeMoveAction = CreateAction(this, TEXT("IA_MapModeMove"), EInputActionValueType::Boolean);
    MapModePatrolAction = CreateAction(this, TEXT("IA_MapModePatrol"), EInputActionValueType::Boolean);
    MapClearRouteAction = CreateAction(this, TEXT("IA_MapClearRoute"), EInputActionValueType::Boolean);
    PauseAction = CreateAction(this, TEXT("IA_ShipPause"), EInputActionValueType::Boolean);
    MenuHotkeyAction = CreateAction(this, TEXT("IA_MenuHotkey"), EInputActionValueType::Boolean);
    StatsHotkeyAction = CreateAction(this, TEXT("IA_NpcStatsHotkey"), EInputActionValueType::Boolean);
    MapCloseAction = CreateAction(this, TEXT("IA_MapClose"), EInputActionValueType::Boolean);
    MapRightDragAction = CreateAction(this, TEXT("IA_MapRightDrag"), EInputActionValueType::Boolean);
    MapPanAction = CreateAction(this, TEXT("IA_MapPan"), EInputActionValueType::Boolean);
    MapPanModifierAction = CreateAction(this, TEXT("IA_MapPanModifier"), EInputActionValueType::Boolean);
    MapLookAction = CreateAction(this, TEXT("IA_MapLook"), EInputActionValueType::Axis2D);
    MapZoomAction = CreateAction(this, TEXT("IA_MapZoom"), EInputActionValueType::Axis1D);
    MapPickAction = CreateAction(this, TEXT("IA_MapPick"), EInputActionValueType::Boolean);
    MapClearPickAction = CreateAction(this, TEXT("IA_MapClearPick"), EInputActionValueType::Boolean);
    MapJumpAction = CreateAction(this, TEXT("IA_MapJump"), EInputActionValueType::Boolean);
    MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Ship"));
    MapMappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Map"));

    MapShipKeys();
}

//////////////////////////////////////////////////////////////////////////
// Maps every key of the ship and map contexts, the rebindable ones from the settings' applied keys
void ASOLShipPawn::MapShipKeys()
{
    using namespace SOLInput;
    using SOLKeyBindings::EBinding;
    if (MappingContext == nullptr || MapMappingContext == nullptr)
    {
        return;
    }

    // Rebindable keys from the settings (defaults without them); a key name the engine does not know falls back to
    // its default, and the bound names record what was really mapped (the Controls page shows them)
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(this);
    mBoundKeys = settings != nullptr ? settings->GetAppliedKeys() : SOLKeyBindings::DefaultKeys();
    mBoundKeys.SetNum(SOLKeyBindings::BINDING_COUNT);
    for (int32 binding = 0; binding < SOLKeyBindings::BINDING_COUNT; ++binding)
    {
        if (!FKey(*mBoundKeys[binding]).IsValid())
        {
            mBoundKeys[binding] = SOLKeyBindings::GetDef(binding).DefaultKey;
        }
    }

    // Returns the key mapped for a binding
    const auto key = [this](const EBinding binding)
    {
        return FKey(*mBoundKeys[static_cast<int32>(binding)]);
    };
    MappingContext->UnmapAll();
    MapMappingContext->UnmapAll();

    // Thrust: forward/back (x), right/left (y), up/down (z); roll: right, left
    MapAxisKey(MappingContext, ThrustAction, key(EBinding::ThrustForward), this, false, EInputAxisSwizzle::YXZ, false);
    MapAxisKey(MappingContext, ThrustAction, key(EBinding::ThrustBackward), this, false, EInputAxisSwizzle::YXZ, true);
    MapAxisKey(MappingContext, ThrustAction, key(EBinding::StrafeRight), this, true, EInputAxisSwizzle::YXZ, false);
    MapAxisKey(MappingContext, ThrustAction, key(EBinding::StrafeLeft), this, true, EInputAxisSwizzle::YXZ, true);
    MapAxisKey(MappingContext, ThrustAction, key(EBinding::ThrustUp), this, true, EInputAxisSwizzle::ZYX, false);
    MapAxisKey(MappingContext, ThrustAction, key(EBinding::ThrustDown), this, true, EInputAxisSwizzle::ZYX, true);
    MapAxisKey(MappingContext, RollAction, key(EBinding::RollRight), this, false, EInputAxisSwizzle::YXZ, false);
    MapAxisKey(MappingContext, RollAction, key(EBinding::RollLeft), this, false, EInputAxisSwizzle::YXZ, true);

    // Held keys and axes: stop spin, steer hold, the gun trigger (left mouse by default; the map's own context uses
    // the left mouse for picking, but the two contexts are never active together), free-look, the mouse (virtual
    // joystick, fixed) and the wheel (speed cap, fixed)
    MappingContext->MapKey(StopSpinAction, key(EBinding::StopSpin));
    MappingContext->MapKey(SteerHoldAction, key(EBinding::SteerHold));
    MappingContext->MapKey(FireAction, key(EBinding::Fire));
    MappingContext->MapKey(FreeLookAction, key(EBinding::FreeLook));
    MappingContext->MapKey(LookAction, EKeys::Mouse2D);
    MappingContext->MapKey(SpeedCapAction, EKeys::MouseWheelAxis);

    // One-shot keys: the assist, boost and steering toggles, and the HUD keys: orbit lines, the route overlay and
    // Resume (SDD 20), the speed panel, radar zoom in/out and back to AUTO
    MapPressedKey(MappingContext, ToggleAssistAction, key(EBinding::ToggleAssist), this);
    MapPressedKey(MappingContext, BoostAction, key(EBinding::Boost), this);
    MapPressedKey(MappingContext, SteerToggleAction, key(EBinding::SteerToggle), this);
    MapPressedKey(MappingContext, RecenterAction, key(EBinding::RecenterStick), this);
    MapPressedKey(MappingContext, MatchLockAction, key(EBinding::MatchLock), this);
    MapPressedKey(MappingContext, ToggleLevelAction, key(EBinding::SurfaceLock), this);
    MapPressedKey(MappingContext, DropTargetAction, key(EBinding::DropTarget), this);
    MapPressedKey(MappingContext, SelectTargetAction, key(EBinding::SelectTarget), this);
    MapPressedKey(MappingContext, NextTargetAction, key(EBinding::NextTarget), this);
    MapPressedKey(MappingContext, PreviousTargetAction, key(EBinding::PreviousTarget), this);
    MapPressedKey(MappingContext, ClearTargetAction, key(EBinding::ClearTarget), this);
    MapPressedKey(MappingContext, WarpDownAction, key(EBinding::WarpDown), this);
    MapPressedKey(MappingContext, WarpUpAction, key(EBinding::WarpUp), this);
    MapPressedKey(MappingContext, WarpResetAction, key(EBinding::WarpReset), this);
    MapPressedKey(MappingContext, ToggleOrbitLinesAction, key(EBinding::ToggleOrbitLines), this);
    MapPressedKey(MappingContext, ToggleRouteOverlayAction, key(EBinding::ToggleRouteOverlay), this);
    MapPressedKey(MappingContext, ResumeRouteAction, key(EBinding::Resume), this);
    MapPressedKey(MappingContext, SpeedPanelAction, key(EBinding::SpeedPanel), this);
    MapPressedKey(MappingContext, RadarZoomInAction, key(EBinding::RadarZoomIn), this);
    MapPressedKey(MappingContext, RadarZoomOutAction, key(EBinding::RadarZoomOut), this);
    MapPressedKey(MappingContext, RadarAutoAction, key(EBinding::RadarAuto), this);

    // Shift chords (SDD 20): the map keys fire only with Shift held, and Enhanced Input then blocks a plain mapping of
    // the same key in the same context (Shift+M opens the map, plain M stays the frame lock). Left Shift is also thrust
    // up in flight
    MappingContext->MapKey(ShiftChordAction, EKeys::LeftShift);
    MappingContext->MapKey(ShiftChordAction, EKeys::RightShift);
    MapChordedKey(MappingContext, OpenMapAction, key(EBinding::OpenMap), ShiftChordAction, this);
    MapChordedKey(MappingContext, OpenMapJumpAction, key(EBinding::OpenMapJump), ShiftChordAction, this);

    // Esc pauses (SDD 8): only in the ship's context, so while the map is open its own Esc closes the map instead
    MapPressedKey(MappingContext, PauseAction, EKeys::Escape, this);

    // Ctrl+F1 acts as Esc in both contexts (Esc stops Play-In-Editor first) and Ctrl+F2 toggles the NPC stats overlay;
    // F1 and F2 are reserved (SOLKeyBindings), and the handlers check Ctrl
    MapPressedKey(MappingContext, MenuHotkeyAction, EKeys::F1, this);
    MapPressedKey(MappingContext, StatsHotkeyAction, EKeys::F2, this);
    MapPressedKey(MapMappingContext, MenuHotkeyAction, EKeys::F1, this);

    // Map: Shift+M (the open key) or Esc closes, Shift+J switches to Jump mode, J / M / P pick the mode, Delete clears
    // the active route and Resume works here too; right drag orbits (Ctrl+right click undoes the last waypoint),
    // middle or Shift+right drag pans (held buttons gate the mouse delta), the wheel zooms. Picking: left button (press
    // and release), Shift (the same action as the pan modifier) previews and locks the height, X clears the pick and
    // the draft, Enter jumps or starts the route. The ship's X (clear target) and M (frame lock) live in the ship's
    // context, which is removed while the map is open, so the two never conflict
    MapMappingContext->MapKey(ShiftChordAction, EKeys::LeftShift);
    MapMappingContext->MapKey(ShiftChordAction, EKeys::RightShift);
    MapChordedKey(MapMappingContext, OpenMapAction, key(EBinding::OpenMap), ShiftChordAction, this);
    MapChordedKey(MapMappingContext, OpenMapJumpAction, key(EBinding::OpenMapJump), ShiftChordAction, this);
    MapPressedKey(MapMappingContext, MapModeJumpAction, EKeys::J, this);
    MapPressedKey(MapMappingContext, MapModeMoveAction, EKeys::M, this);
    MapPressedKey(MapMappingContext, MapModePatrolAction, EKeys::P, this);
    MapPressedKey(MapMappingContext, MapClearRouteAction, EKeys::Delete, this);
    MapPressedKey(MapMappingContext, ResumeRouteAction, key(EBinding::Resume), this);
    MapPressedKey(MapMappingContext, MapCloseAction, EKeys::Escape, this);
    MapPressedKey(MapMappingContext, MapClearPickAction, EKeys::X, this);
    MapPressedKey(MapMappingContext, MapJumpAction, EKeys::Enter, this);
    MapMappingContext->MapKey(MapPickAction, EKeys::LeftMouseButton);
    MapMappingContext->MapKey(MapRightDragAction, EKeys::RightMouseButton);
    MapMappingContext->MapKey(MapPanAction, EKeys::MiddleMouseButton);
    MapMappingContext->MapKey(MapPanModifierAction, EKeys::LeftShift);
    MapMappingContext->MapKey(MapPanModifierAction, EKeys::RightShift);
    MapMappingContext->MapKey(MapLookAction, EKeys::Mouse2D);
    MapMappingContext->MapKey(MapZoomAction, EKeys::MouseWheelAxis);
}

//////////////////////////////////////////////////////////////////////////
// The applied key bindings changed: maps the keys again and asks Enhanced Input to rebuild its mappings
void ASOLShipPawn::HandleKeyBindingsChanged()
{
    MapShipKeys();
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    const ULocalPlayer* localPlayer = playerController != nullptr ? playerController->GetLocalPlayer() : nullptr;
    UEnhancedInputLocalPlayerSubsystem* inputSubsystem = localPlayer != nullptr
        ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(localPlayer) : nullptr;
    if (inputSubsystem != nullptr)
    {
        inputSubsystem->RequestRebuildControlMappings();
    }
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: key bindings mapped again (fire = %s)"), *GetName(),
        *mBoundKeys[static_cast<int32>(SOLKeyBindings::EBinding::Fire)]);
}

//////////////////////////////////////////////////////////////////////////
// Returns the input action a rebindable binding drives (thrust and roll bindings share their axis action)
UInputAction* ASOLShipPawn::GetBindingAction(const int32 binding) const
{
    using SOLKeyBindings::EBinding;
    switch (static_cast<EBinding>(binding))
    {
    case EBinding::ThrustForward:
    case EBinding::ThrustBackward:
    case EBinding::StrafeLeft:
    case EBinding::StrafeRight:
    case EBinding::ThrustUp:
    case EBinding::ThrustDown:
        return ThrustAction;
    case EBinding::RollLeft:
    case EBinding::RollRight:
        return RollAction;
    case EBinding::Boost:
        return BoostAction;
    case EBinding::StopSpin:
        return StopSpinAction;
    case EBinding::SteerHold:
        return SteerHoldAction;
    case EBinding::SteerToggle:
        return SteerToggleAction;
    case EBinding::ToggleAssist:
        return ToggleAssistAction;
    case EBinding::FreeLook:
        return FreeLookAction;
    case EBinding::RecenterStick:
        return RecenterAction;
    case EBinding::SpeedPanel:
        return SpeedPanelAction;
    case EBinding::SelectTarget:
        return SelectTargetAction;
    case EBinding::NextTarget:
        return NextTargetAction;
    case EBinding::PreviousTarget:
        return PreviousTargetAction;
    case EBinding::ClearTarget:
        return ClearTargetAction;
    case EBinding::MatchLock:
        return MatchLockAction;
    case EBinding::SurfaceLock:
        return ToggleLevelAction;
    case EBinding::Fire:
        return FireAction;
    case EBinding::DropTarget:
        return DropTargetAction;
    case EBinding::WarpDown:
        return WarpDownAction;
    case EBinding::WarpUp:
        return WarpUpAction;
    case EBinding::WarpReset:
        return WarpResetAction;
    case EBinding::ToggleOrbitLines:
        return ToggleOrbitLinesAction;
    case EBinding::RadarZoomIn:
        return RadarZoomInAction;
    case EBinding::RadarZoomOut:
        return RadarZoomOutAction;
    case EBinding::RadarAuto:
        return RadarAutoAction;
    case EBinding::OpenMap:
        return OpenMapAction;
    case EBinding::OpenMapJump:
        return OpenMapJumpAction;
    case EBinding::Resume:
        return ResumeRouteAction;
    case EBinding::ToggleRouteOverlay:
        return ToggleRouteOverlayAction;
    default:
        return nullptr;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true if the ship mapping context maps this key to a binding's action
bool ASOLShipPawn::IsKeyMappedToBinding(const int32 binding, const FKey& key) const
{
    const UInputAction* action = GetBindingAction(binding);
    if (action == nullptr || MappingContext == nullptr)
    {
        return false;
    }
    for (const FEnhancedActionKeyMapping& mapping : MappingContext->GetMappings())
    {
        if (mapping.Action == action && mapping.Key == key)
        {
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Builds the placeholder ship from engine primitives with a light hull and a glowing engine
void ASOLShipPawn::BuildShipMesh()
{
    UStaticMesh* cube = LoadObject<UStaticMesh>(nullptr, SOL::Paths::SHIP_PART_CUBE);
    UStaticMesh* cone = LoadObject<UStaticMesh>(nullptr, SOL::Paths::SHIP_PART_CONE);
    UStaticMesh* cylinder = LoadObject<UStaticMesh>(nullptr, SOL::Paths::SHIP_PART_CYLINDER);
    UStaticMesh* sphere = LoadObject<UStaticMesh>(nullptr, SOL::Paths::SHIP_PART_SPHERE);
    UMaterialInterface* hullBase = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::SHIP_HULL_MATERIAL);
    UMaterialInterface* glowBase = LoadObject<UMaterialInterface>(nullptr, SOL::Paths::BODY_MATERIAL);
    if (cube == nullptr || cone == nullptr || cylinder == nullptr || sphere == nullptr || hullBase == nullptr
        || glowBase == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("ShipPawn %s: failed to load the placeholder ship meshes or materials"), *GetName());
        return;
    }

    // Lit hull and trim; the engine glow reuses the unlit body material with only its emissive term
    UMaterialInstanceDynamic* hull = SOLShipPawnCreateHullMaterial(hullBase, this, SHIP_HULL_COLOR);
    UMaterialInstanceDynamic* trim = SOLShipPawnCreateHullMaterial(hullBase, this, SHIP_TRIM_COLOR);
    UMaterialInstanceDynamic* glow = UMaterialInstanceDynamic::Create(glowBase, this);
    glow->SetVectorParameterValue(FName(SOL::BodyMaterialParams::COLOR_A), FLinearColor::Black);
    glow->SetVectorParameterValue(FName(SOL::BodyMaterialParams::COLOR_B), FLinearColor::Black);
    glow->SetVectorParameterValue(FName(SOL::BodyMaterialParams::POLAR_COLOR), FLinearColor::Black);
    glow->SetVectorParameterValue(FName(SOL::BodyMaterialParams::EMISSIVE_COLOR), SHIP_ENGINE_GLOW);

    // One component per entry of the parts table, indexed by shape and look
    UStaticMesh* const meshes[] = { cube, cone, cylinder, sphere };
    UMaterialInterface* const materials[] = { hull, trim, glow };
    static_assert(UE_ARRAY_COUNT(meshes) == static_cast<int32>(ESOLShipPartShape::Count), "One mesh per part shape");
    static_assert(UE_ARRAY_COUNT(materials) == static_cast<int32>(ESOLShipPartLook::Count), "One material per look");
    ShipParts.Reserve(UE_ARRAY_COUNT(SHIP_PARTS));
    for (const FSOLShipPartSpec& spec : SHIP_PARTS)
    {
        AddShipPart(meshes[static_cast<int32>(spec.Shape)], materials[static_cast<int32>(spec.Look)], spec.LocationCm,
            spec.Rotation, spec.Scale);
    }
}

//////////////////////////////////////////////////////////////////////////
// Adds one primitive part to the ship, relative to the root (location in cm, scale in primitive units)
void ASOLShipPawn::AddShipPart(UStaticMesh* mesh, UMaterialInterface* material, const FVector& locationCm,
    const FRotator& rotation, const FVector& scale)
{
    UStaticMeshComponent* part = NewObject<UStaticMeshComponent>(this);
    part->SetMobility(EComponentMobility::Movable);
    part->SetStaticMesh(mesh);
    part->SetMaterial(0, material);
    part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    part->SetCastShadow(false);
    SOLShipSunlight::PutOnShipLightingChannel(part);   // Lit by the shadowed ship sun light, not the rocks' (SDD 16)
    part->SetupAttachment(ShipRoot);
    part->SetRelativeTransform(FTransform(rotation, locationCm, scale));
    part->RegisterComponent();
    ShipParts.Add(part);
}

//////////////////////////////////////////////////////////////////////////
// Places the actor at the observer's render location with the ship's orientation after the universe update
void ASOLShipPawn::FollowShip()
{
    // A jump that completed (or aborted) in this frame's universe update: the resting FOV and post-process now, before
    // the camera update reads them, rather than one frame late from the next pre-physics tick
    if (mIsWarpEffectActive && !IsJumpWarping())
    {
        EndWarpEffect();
    }
    if (Ships == nullptr || !Ships->HasPlayerShip())
    {
        return;
    }
    // Through the body placement: the render origin itself normally, and still a bounded location when the jump map
    // draws from its own far viewpoint
    const FQuat orientation(Ships->GetState().Orientation);
    SetActorLocationAndRotation(AnchorSubsystem->ComputePointRenderLocationCm(AnchorSubsystem->GetObserverPositionM()),
        orientation);
    UpdateCameraRotation(GetWorld()->GetDeltaSeconds());
}

//////////////////////////////////////////////////////////////////////////
// Eases the camera arm toward the ship orientation (plus free-look) by an exponential slerp, after the ship moved
void ASOLShipPawn::UpdateCameraRotation(const float deltaSeconds)
{
    // Target: the orientation this frame is rendered with, orbited by the Alt free-look (identity when released)
    const FQuat freeLook = mIsFreeLooking ? mFreeLookRotation.Quaternion() : FQuat::Identity;
    const FQuat target = GetActorQuat() * freeLook;
    if (!mHasCameraRotation)
    {
        mCameraRotation = target;
        mHasCameraRotation = true;
    }
    else
    {
        const double alpha = 1.0 - FMath::Exp(-SOL::SHIP_CAMERA_ROTATION_LAG_SPEED * deltaSeconds);
        mCameraRotation = FQuat::Slerp(mCameraRotation, target, alpha).GetNormalized();
    }
    SpringArm->SetWorldRotation(mCameraRotation);
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship's position and forward direction (Unreal-handed universe frame); false without a ship
bool ASOLShipPawn::GetShipPose(FVector3d& outPositionM, FVector3d& outForwardDir) const
{
    if (Ships == nullptr || !Ships->HasPlayerShip())
    {
        return false;
    }
    const FSOLShipState state = Ships->GetState();
    outPositionM = state.PositionM;
    outForwardDir = state.Orientation.GetForwardVector();
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sets the translation input (local X forward, Y right, Z up, each in [-1, 1])
void ASOLShipPawn::HandleThrust(const FVector3d& thrust)
{
    mControl.Thrust = thrust;
}

//////////////////////////////////////////////////////////////////////////
// Sets the roll input in [-1, 1] (positive = right wing down)
void ASOLShipPawn::HandleRoll(const double roll)
{
    mRollInput = FMath::Clamp(roll, -1.0, 1.0);
}

//////////////////////////////////////////////////////////////////////////
// Toggles boost on / off; ignored while the pilot has no control
void ASOLShipPawn::HandleToggleBoost()
{
    const bool bHasControl = Ships == nullptr || Ships->PilotHasControl();
    SOLFlightMode::ToggleBoost(mBoost, bHasControl);
    mControl.bBoost = mBoost.bOn;
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: boost %s%s"), *GetName(), mBoost.bOn ? TEXT("ON") : TEXT("OFF"),
        bHasControl ? TEXT("") : TEXT(" (no pilot control)"));
}

//////////////////////////////////////////////////////////////////////////
// Sets whether the steer-hold key is down
void ASOLShipPawn::HandleSteerHold(const bool bDown)
{
    SOLFlightMode::SetSteerHold(mSteerGate, bDown);
}

//////////////////////////////////////////////////////////////////////////
// Toggles mouse steering on / off
void ASOLShipPawn::HandleToggleSteer()
{
    SOLFlightMode::ToggleSteer(mSteerGate);
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: mouse steering toggle %s"), *GetName(),
        mSteerGate.bToggleOn ? TEXT("ON") : TEXT("OFF"));
}

//////////////////////////////////////////////////////////////////////////
// Returns true while something else has the input or the view
bool ASOLShipPawn::IsSteerSuspended() const
{
    return mIsMapOpen || mIsSpeedPanelOpen || mIsMenuSuspended || IsJumpWarping() || !mIsViewportFocused;
}

//////////////////////////////////////////////////////////////////////////
// Updates the steering gate and the cursor capture that follows it
void ASOLShipPawn::UpdateSteering()
{
    // Suspension releases the hold (the key's release may never arrive); the toggle survives
    const bool bSuspended = IsSteerSuspended();
    if (bSuspended)
    {
        SOLFlightMode::SetSteerHold(mSteerGate, false);
    }
    const FSOLSteerGateUpdate update = SOLFlightMode::UpdateSteerGate(mSteerGate, bSuspended);
    if (update.bRecenter)
    {
        mStickOffsetPx = FVector2d::ZeroVector;
    }
    if (update.bActive != mIsSteering)
    {
        UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: mouse steering %s"), *GetName(),
            update.bActive ? TEXT("ON (stick recentered)") : TEXT("OFF (cursor shown, locked to the viewport)"));
    }
    mIsSteering = update.bActive;

    // The ship owns the cursor only while nothing else has the input: captured while steering or free-looking, else
    // shown and free over the game
    if (mIsMapOpen || mIsSpeedPanelOpen || mIsMenuSuspended || IsJumpWarping() || !mIsViewportFocused)
    {
        return;
    }
    const bool bCapture = mIsSteering || mIsFreeLooking;
    if (bCapture != mHasCapturedMouse)
    {
        ApplyShipCursor(bCapture);
    }
}

//////////////////////////////////////////////////////////////////////////
// Captures and hides the cursor, or shows it free over the game (the viewport hides it only while a button is held)
void ASOLShipPawn::ApplyShipCursor(const bool bCapture)
{
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (playerController == nullptr)
    {
        return;
    }
    if (bCapture)
    {
        playerController->bShowMouseCursor = false;
        playerController->SetInputMode(FInputModeGameOnly());
    }
    else
    {
        // The cursor is shown but stays locked inside the viewport, and keyboard focus stays on the viewport, so a
        // click never lands outside the game and Tab / Space never move Slate focus off it (SDD 19 Amendment 4)
        FInputModeGameAndUI inputMode;
        inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
        inputMode.SetHideCursorDuringCapture(false);
        const ULocalPlayer* localPlayer = playerController->GetLocalPlayer();
        const UGameViewportClient* viewportClient = localPlayer != nullptr ? localPlayer->ViewportClient.Get() : nullptr;
        if (viewportClient != nullptr && viewportClient->GetGameViewportWidget().IsValid())
        {
            inputMode.SetWidgetToFocus(viewportClient->GetGameViewportWidget());
        }
        playerController->SetInputMode(inputMode);
        playerController->bShowMouseCursor = true;

        // The hidden cursor may sit at the viewport edge after steering; it reappears at the centre, like the stick
        int32 sizeX = 0;
        int32 sizeY = 0;
        playerController->GetViewportSize(sizeX, sizeY);
        if (sizeX > 0 && sizeY > 0)
        {
            playerController->SetMouseLocation(sizeX / 2, sizeY / 2);
        }
    }
    mHasCapturedMouse = bCapture;
}

//////////////////////////////////////////////////////////////////////////
// Sets whether stop spin is held (SDD 18: cancels all rotation, in every flight mode)
void ASOLShipPawn::HandleStopSpin(const bool bStopSpin)
{
    mControl.bStopSpin = bStopSpin;
}

//////////////////////////////////////////////////////////////////////////
// Toggles assisted / manual flight; during the autopilot, takes control back in assisted flight
void ASOLShipPawn::HandleToggleAssist()
{
    if (Ships != nullptr && Ships->ToggleAssist())
    {
        mControl.bFlightAssist = SOLFlightMode::FlightAssistFor(Ships->GetFlightMode());
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets whether the Alt free-look is held; releasing it returns the camera behind the ship
void ASOLShipPawn::HandleFreeLook(const bool bFreeLook)
{
    mIsFreeLooking = bFreeLook;
    if (!bFreeLook)
    {
        mFreeLookRotation = FRotator::ZeroRotator;
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves the virtual joystick (or, during free-look, orbits the camera) by a mouse delta in pixels (X right, Y up)
void ASOLShipPawn::HandleMouseDelta(const FVector2d& deltaPixels)
{
    if (mIsFreeLooking)
    {
        const FVector2d orbitDeg = deltaPixels * SOL::SHIP_FREE_LOOK_DEG_PER_PIXEL;
        mFreeLookRotation.Yaw = FRotator::NormalizeAxis(mFreeLookRotation.Yaw + orbitDeg.X);
        mFreeLookRotation.Pitch = FMath::Clamp(mFreeLookRotation.Pitch + orbitDeg.Y, -SOL::SHIP_FREE_LOOK_MAX_PITCH_DEG,
            SOL::SHIP_FREE_LOOK_MAX_PITCH_DEG);
        return;
    }

    // With mouse steering off the mouse is a free cursor and the stick stays put
    if (!mIsSteering)
    {
        return;
    }
    mStickOffsetPx = (mStickOffsetPx + deltaPixels).GetClampedToMaxSize(mStickRadiusPx);
}

//////////////////////////////////////////////////////////////////////////
// Recenters the virtual joystick
void ASOLShipPawn::HandleRecenterStick()
{
    mStickOffsetPx = FVector2d::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Steps the speed cap by whole wheel steps on the log scale
void ASOLShipPawn::HandleSpeedCapSteps(const int32 steps)
{
    if (Ships != nullptr)
    {
        mControl.SpeedCapMps = SOLFlight::StepSpeedCap(mControl.SpeedCapMps, steps, Ships->GetFlightParams());
    }
}

//////////////////////////////////////////////////////////////////////////
// Toggles the reference-frame lock (M)
void ASOLShipPawn::HandleMatchLock()
{
    if (Targeting != nullptr && !IsJumpWarping())
    {
        Targeting->ToggleFrameLock();
    }
}

//////////////////////////////////////////////////////////////////////////
// Toggles surface-lock (L): hands the press to the ship subsystem, which consumes it in exactly its next step
void ASOLShipPawn::HandleToggleLevel()
{
    // The open map and speed panel remove the ship mapping, so L cannot arrive then; a jump warp is gated here
    if (Ships != nullptr && !IsJumpWarping() && !mIsMapOpen && !mIsSpeedPanelOpen)
    {
        Ships->RequestSurfaceLockToggle();
    }
}

//////////////////////////////////////////////////////////////////////////
// Sets whether the fire trigger (left mouse) is held; the combat subsystem fires at its cadence while it is
void ASOLShipPawn::HandleFire(const bool bHeld)
{
    mIsFireHeld = bHeld;
}

//////////////////////////////////////////////////////////////////////////
// Requests a target drop ahead of the ship (G); the combat subsystem drops it in its next update
void ASOLShipPawn::HandleDropTarget()
{
    if (Combat != nullptr && !IsJumpWarping() && !mIsMapOpen && !mIsSpeedPanelOpen)
    {
        Combat->RequestPlayerTargetDrop();
    }
}

//////////////////////////////////////////////////////////////////////////
// Selects the target under the forward reticle (T)
void ASOLShipPawn::HandleSelectTarget()
{
    FVector3d positionM;
    FVector3d forwardDir;
    if (Targeting != nullptr && !IsJumpWarping() && GetShipPose(positionM, forwardDir))
    {
        Targeting->SelectUnderReticle(positionM, forwardDir);
    }
}

//////////////////////////////////////////////////////////////////////////
// Selects the next farther target (R)
void ASOLShipPawn::HandleNextTarget()
{
    FVector3d positionM;
    FVector3d forwardDir;
    if (Targeting != nullptr && !IsJumpWarping() && GetShipPose(positionM, forwardDir))
    {
        Targeting->CycleTarget(positionM, 1);
    }
}

//////////////////////////////////////////////////////////////////////////
// Selects the next nearer target (F)
void ASOLShipPawn::HandlePreviousTarget()
{
    FVector3d positionM;
    FVector3d forwardDir;
    if (Targeting != nullptr && !IsJumpWarping() && GetShipPose(positionM, forwardDir))
    {
        Targeting->CycleTarget(positionM, -1);
    }
}

//////////////////////////////////////////////////////////////////////////
// Clears the selected target (X)
void ASOLShipPawn::HandleClearTarget()
{
    if (Targeting != nullptr && !IsJumpWarping())
    {
        Targeting->ClearTarget();
    }
}

//////////////////////////////////////////////////////////////////////////
// Steps the time-warp up (])
void ASOLShipPawn::HandleWarpUp()
{
    if (SimClock != nullptr)
    {
        SimClock->StepWarpUp();
    }
}

//////////////////////////////////////////////////////////////////////////
// Steps the time-warp down ([)
void ASOLShipPawn::HandleWarpDown()
{
    if (SimClock != nullptr)
    {
        SimClock->StepWarpDown();
    }
}

//////////////////////////////////////////////////////////////////////////
// Resets the time-warp to 1x (Backspace)
void ASOLShipPawn::HandleWarpReset()
{
    if (SimClock != nullptr)
    {
        SimClock->ResetWarp();
    }
}

//////////////////////////////////////////////////////////////////////////
// Toggles the world-space orbit lines (O) through the HUD
void ASOLShipPawn::HandleToggleOrbitLines()
{
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    if (ASOLFlightHud* hud = playerController != nullptr ? playerController->GetHUD<ASOLFlightHud>() : nullptr)
    {
        hud->ToggleOrbitLines();
    }
}

//////////////////////////////////////////////////////////////////////////
// Shows or hides the route overlay (P; SDD 20)
void ASOLShipPawn::HandleToggleRouteOverlay()
{
    if (Routes != nullptr)
    {
        Routes->ToggleOverlay();
    }
}

//////////////////////////////////////////////////////////////////////////
// Resumes a paused route (V; flight and map)
void ASOLShipPawn::HandleResumeRoute()
{
    if (Routes != nullptr)
    {
        Routes->Resume();
    }
}

//////////////////////////////////////////////////////////////////////////
// Steps the flight HUD's radar range one decade in ('-') or out ('='), switching it to MANUAL
void ASOLShipPawn::HandleRadarZoom(const bool bZoomIn)
{
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    if (ASOLFlightHud* hud = playerController != nullptr ? playerController->GetHUD<ASOLFlightHud>() : nullptr)
    {
        hud->StepRadarZoom(bZoomIn);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the flight HUD's radar to AUTO range (Home)
void ASOLShipPawn::HandleRadarAuto()
{
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    if (ASOLFlightHud* hud = playerController != nullptr ? playerController->GetHUD<ASOLFlightHud>() : nullptr)
    {
        hud->ResetRadarAuto();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests the F3 speed panel to open (done at the next tick, outside the input callback)
void ASOLShipPawn::HandleToggleSpeedPanel()
{
    mIsSpeedPanelRequested = !mIsSpeedPanelOpen;
}

//////////////////////////////////////////////////////////////////////////
// Opens the speed panel: suspends the ship's control input and gives the panel UI-only keyboard focus
void ASOLShipPawn::OpenSpeedPanel()
{
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (mIsSpeedPanelOpen || mIsMapOpen || IsJumpWarping() || playerController == nullptr || Ships == nullptr)
    {
        return;
    }
    if (SpeedPanel == nullptr)
    {
        SpeedPanel = CreateWidget<USOLSpeedPanelWidget>(playerController, USOLSpeedPanelWidget::StaticClass());
        if (SpeedPanel == nullptr)
        {
            UE_LOG(LogSOL, Error, TEXT("ShipPawn %s: failed to create the speed panel"), *GetName());
            return;
        }
    }

    SuspendShipControl(*playerController);

    // Show the panel with keyboard focus; the viewport stays visible and the Mass simulation keeps running
    // The title and the closing key name the speed-panel binding's current key
    const FString& toggleKeyName = mBoundKeys.IsValidIndex(static_cast<int32>(SOLKeyBindings::EBinding::SpeedPanel))
        ? mBoundKeys[static_cast<int32>(SOLKeyBindings::EBinding::SpeedPanel)] : FString();
    SpeedPanel->Open(this, mControl.SpeedCapMps, FKey(*toggleKeyName),
        SOLHudFormat::FormatHudKeyTexts(mBoundKeys).SpeedPanelTitle);
    SpeedPanel->AddToViewport(SPEED_PANEL_Z_ORDER);
    FInputModeUIOnly inputMode;
    inputMode.SetWidgetToFocus(SpeedPanel->TakeWidget());
    inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    playerController->SetInputMode(inputMode);
    playerController->bShowMouseCursor = true;
    mIsSpeedPanelOpen = true;
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: speed panel open; ship control input suspended (cap %.3f m/s)"),
        *GetName(), mControl.SpeedCapMps);
}

//////////////////////////////////////////////////////////////////////////
// Closes the speed panel and gives input back to the ship
void ASOLShipPawn::CloseSpeedPanel()
{
    if (!mIsSpeedPanelOpen)
    {
        return;
    }
    DismissSpeedPanel();
    RestoreShipControl();
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: speed panel closed; ship control input restored (cap %.3f m/s)"),
        *GetName(), mControl.SpeedCapMps);
}

//////////////////////////////////////////////////////////////////////////
// Suspends the ship's own control input: flushes held keys, drops the ship mapping and zeroes the flight input
void ASOLShipPawn::SuspendShipControl(APlayerController& playerController)
{
    // The Mass simulation keeps running; the assist holds the last speed cap and assist mode
    playerController.FlushPressedKeys();
    RemoveMappingContext();
    mStickOffsetPx = FVector2d::ZeroVector;
    mRollInput = 0.0;
    SOLFlightMode::SetSteerHold(mSteerGate, false);
    mControl.Thrust = FVector3d::ZeroVector;
    mControl.Rotation = FVector3d::ZeroVector;
    mControl.bStopSpin = false;
    mIsFireHeld = false;
    mIsFreeLooking = false;
    mFreeLookRotation = FRotator::ZeroRotator;
}

//////////////////////////////////////////////////////////////////////////
// Restores the ship's mapping and its cursor mode (captured while mouse steering is on, else free)
void ASOLShipPawn::RestoreShipControl()
{
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (playerController != nullptr)
    {
        if (SOLInput::AddMappingContext(playerController->GetLocalPlayer(), MappingContext, 0))
        {
            mMappedLocalPlayer = playerController->GetLocalPlayer();
        }
        ApplyShipCursor(mSteerGate.bToggleOn);

        // A key held across the close (e.g. Left Shift from the map) would otherwise cancel the autopilot at once
        if (Ships != nullptr && !Ships->IsControlScripted())
        {
            Ships->RelatchPilotInput(HeldKeyPilotInput(*playerController));
        }
    }
    mHadViewportFocus = false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the pilot input the ship's bound flight keys hold right now
FSOLPilotInput ASOLShipPawn::HeldKeyPilotInput(const APlayerController& playerController,
    const bool bShiftKeysOnly) const
{
    using SOLKeyBindings::EBinding;

    // Returns 1 when the binding's key is down (and, with bShiftKeysOnly, is a Shift key), else 0
    const auto held = [this, &playerController, bShiftKeysOnly](const EBinding binding)
    {
        const int32 index = static_cast<int32>(binding);
        if (!mBoundKeys.IsValidIndex(index))
        {
            return 0.0;
        }
        const FKey key(*mBoundKeys[index]);
        const bool bShiftKey = key == EKeys::LeftShift || key == EKeys::RightShift;
        return (!bShiftKeysOnly || bShiftKey) && playerController.IsInputKeyDown(key) ? 1.0 : 0.0;
    };
    FSOLPilotInput input;
    input.Thrust = FVector3d(held(EBinding::ThrustForward) - held(EBinding::ThrustBackward),
        held(EBinding::StrafeRight) - held(EBinding::StrafeLeft),
        held(EBinding::ThrustUp) - held(EBinding::ThrustDown));
    input.Roll = held(EBinding::RollRight) - held(EBinding::RollLeft);
    input.bStopSpin = held(EBinding::StopSpin) > 0.0;
    return input;
}

//////////////////////////////////////////////////////////////////////////
// Opens the jump map: adds the map mapping (rolling back on failure), suspends ship control and frees the OS cursor
void ASOLShipPawn::OpenMap(const bool bForJump)
{
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (mIsMapOpen || mIsSpeedPanelOpen || playerController == nullptr || MapMode == nullptr)
    {
        return;
    }
    if (!MapMode->OpenMap(playerController, bForJump))
    {
        return;
    }
    // The map mapping goes in before the ship's is removed; if it cannot be added the map is rolled back, so the
    // player is never left without a mapping that can close it
    if (!SOLInput::AddMappingContext(playerController->GetLocalPlayer(), MapMappingContext, 0))
    {
        MapMode->CloseMap(playerController);
        UE_LOG(LogSOL, Warning, TEXT("ShipPawn %s: jump map not opened; the map input mapping could not be added"),
            *GetName());
        return;
    }
    mMapMappedLocalPlayer = playerController->GetLocalPlayer();
    SuspendShipControl(*playerController);

    // A visible, unlocked cursor for dragging; the viewport captures (and hides) it only while a button is held
    FInputModeGameAndUI inputMode;
    inputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    inputMode.SetHideCursorDuringCapture(true);
    playerController->SetInputMode(inputMode);
    playerController->bShowMouseCursor = true;
    mHasCapturedMouse = false;
    mIsMapRightHeld = false;
    mIsMapPanHeld = false;
    mIsMapPanModifierHeld = false;
    mIsMapPickHeld = false;
    mHasOsMousePx = false;
    mIsMapOpen = true;
    if (Menu != nullptr)
    {
        Menu->NotifyMapOpen(true);
    }
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: jump map open; ship control input suspended, cursor released"), *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Closes the jump map and gives input and the view back to the ship
void ASOLShipPawn::CloseMap()
{
    if (!mIsMapOpen)
    {
        return;
    }
    // Flight keys still held (Left Shift is also the map's height key) are read before the flush forgets them and
    // latched in AUTO, so a key repeat after the close does not cancel the autopilot (SDD 19 Amendment 3)
    APlayerController* playerController = Cast<APlayerController>(GetController());
    FSOLPilotInput heldAtClose;
    if (playerController != nullptr)
    {
        heldAtClose = HeldKeyPilotInput(*playerController);
        playerController->FlushPressedKeys();
    }
    DismissMap();
    RestoreShipControl();
    if (Ships != nullptr && !Ships->IsControlScripted())
    {
        Ships->RelatchPilotInput(heldAtClose);
    }
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: jump map closed; ship control input restored"), *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Closes the map without restoring the ship's input (teardown)
void ASOLShipPawn::DismissMap()
{
    SOLInput::RemoveMappingContext(mMapMappedLocalPlayer.Get(), MapMappingContext);
    mMapMappedLocalPlayer.Reset();
    if (mIsMapOpen && MapMode != nullptr)
    {
        MapMode->CloseMap(Cast<APlayerController>(GetController()));
    }
    if (mIsMapOpen && Menu != nullptr)
    {
        Menu->NotifyMapOpen(false);
    }
    mIsMapOpen = false;
    mIsMapToggleRequested = false;
    mIsJumpRequested = false;
    mIsMapRightHeld = false;
    mIsMapPanHeld = false;
    mIsMapPanModifierHeld = false;
    mIsMapPickHeld = false;
}

//////////////////////////////////////////////////////////////////////////
// Follows the OS mouse position with the map cursor while no map button is held (and no script drives it)
void ASOLShipPawn::UpdateMapCursor()
{
    // During a drag the viewport captures and hides the cursor, so the drag moves the map cursor by the mouse deltas
    // instead (HandleMapMouseDelta); a reading is forwarded only when the OS position actually changed
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    if (!mIsMapOpen || mIsMapCursorScripted || mIsMapPickHeld || mIsMapRightHeld || mIsMapPanHeld
        || playerController == nullptr)
    {
        return;
    }
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    if (!playerController->GetMousePosition(mouseX, mouseY))
    {
        return;
    }
    const FVector2D osMousePx(mouseX, mouseY);
    if (!mHasOsMousePx || osMousePx != mLastOsMousePx)
    {
        mLastOsMousePx = osMousePx;
        mHasOsMousePx = true;
        HandleMapCursorMoved(osMousePx);
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves the map cursor to a pixel position (top-left origin) and forwards it to the map
void ASOLShipPawn::HandleMapCursorMoved(const FVector2D& cursorPx)
{
    mMapCursorPx = cursorPx;
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    if (MapMode == nullptr || playerController == nullptr)
    {
        return;
    }
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
    playerController->GetViewportSize(viewportWidth, viewportHeight);
    MapMode->SetPickCursorPx(mMapCursorPx, FVector2D(viewportWidth, viewportHeight));
}

//////////////////////////////////////////////////////////////////////////
// Left mouse pressed on the map: starts a destination pick, or with Shift during a height preview locks the height
void ASOLShipPawn::HandleMapPickPressed()
{
    if (!mIsMapOpen || MapMode == nullptr)
    {
        return;
    }

    // Take the latest OS cursor position before the button capture freezes it, then pick against the body icons the
    // HUD drew last frame
    UpdateMapCursor();
    mIsMapPickHeld = true;
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    const ASOLFlightHud* hud = playerController != nullptr ? playerController->GetHUD<ASOLFlightHud>() : nullptr;
    MapMode->PressPick(hud != nullptr ? &hud->GetMapBodyOverlay() : nullptr, mIsMapPanModifierHeld);
}

//////////////////////////////////////////////////////////////////////////
// Left mouse released on the map: locks the pick's planar offset
void ASOLShipPawn::HandleMapPickReleased()
{
    // The OS cursor is put back on the map cursor first, so the lock (and a height preview it starts with Shift held)
    // uses the position the player now sees
    mIsMapPickHeld = false;
    SyncOsCursorToMapCursor();
    if (mIsMapOpen && MapMode != nullptr)
    {
        MapMode->ReleasePick();
    }
}

//////////////////////////////////////////////////////////////////////////
// Once no map button is held (the viewport's mouse capture ended and it put the OS cursor back where the capture
// began), warps the OS cursor onto the map cursor and records that as the last OS reading, so neither jumps later
void ASOLShipPawn::SyncOsCursorToMapCursor()
{
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (!mIsMapOpen || mIsMapCursorScripted || mIsMapPickHeld || mIsMapRightHeld || mIsMapPanHeld
        || playerController == nullptr)
    {
        return;
    }
    int32 viewportWidth = 0;
    int32 viewportHeight = 0;
    playerController->GetViewportSize(viewportWidth, viewportHeight);
    if (viewportWidth <= 0 || viewportHeight <= 0)
    {
        return;
    }

    // The OS cursor only takes whole pixels inside the viewport; the map cursor snaps to the same pixel so the two
    // agree exactly (a drag may have carried it past the edge)
    const int32 cursorX = FMath::Clamp(FMath::RoundToInt32(mMapCursorPx.X), 0, viewportWidth - 1);
    const int32 cursorY = FMath::Clamp(FMath::RoundToInt32(mMapCursorPx.Y), 0, viewportHeight - 1);
    playerController->SetMouseLocation(cursorX, cursorY);
    mLastOsMousePx = FVector2D(cursorX, cursorY);
    mHasOsMousePx = true;
    HandleMapCursorMoved(mLastOsMousePx);
}

//////////////////////////////////////////////////////////////////////////
// Clears the map's destination pick (X)
void ASOLShipPawn::HandleMapClearPick()
{
    if (mIsMapOpen && MapMode != nullptr)
    {
        MapMode->ClearPickAndDraft();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a jump to the map's locked destination (Enter; started at the next tick, outside the input callback)
void ASOLShipPawn::HandleMapJump()
{
    mIsJumpRequested = mIsMapOpen;
}

//////////////////////////////////////////////////////////////////////////
// Shift+M / Shift+J: opens the map (next tick) in flight; on the map Shift+M closes it and Shift+J is Jump mode
void ASOLShipPawn::HandleOpenMap(const bool bForJump)
{
    if (mIsMapOpen && bForJump)
    {
        HandleMapModeKey(ESOLMapMode::Jump);
        return;
    }
    mIsMapToggleRequested = true;
    mIsMapOpenForJump = bForJump;
}

//////////////////////////////////////////////////////////////////////////
// An in-map mode key: J Jump, M Move, P Patrol (P again flips loop / ping-pong)
void ASOLShipPawn::HandleMapModeKey(const ESOLMapMode key)
{
    if (mIsMapOpen && MapMode != nullptr)
    {
        MapMode->PressModeKey(key);
    }
}

//////////////////////////////////////////////////////////////////////////
// Clears the active route (Delete on the map)
void ASOLShipPawn::HandleMapClearRoute()
{
    if (mIsMapOpen && Routes != nullptr)
    {
        Routes->ClearRoute();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests the jump map to close (Esc; done at the next tick)
void ASOLShipPawn::HandleCloseMap()
{
    mIsMapToggleRequested = mIsMapOpen;
    mIsMapOpenForJump = false;
}

//////////////////////////////////////////////////////////////////////////
// Requests the pause menu (Esc in flight; handed to the menu subsystem at the next tick)
void ASOLShipPawn::HandlePause()
{
    mIsPauseRequested = !mIsMapOpen && !mIsMenuSuspended;
}

//////////////////////////////////////////////////////////////////////////
// Menu state changed: suspends the ship's control while a menu shows and restores it on resume
void ASOLShipPawn::HandleMenuStateChanged(const SOLMenuState::FMenuState& previous,
    const SOLMenuState::FMenuState& next)
{
    const bool bSuspend = SOLMenuState::SuspendsFlightInput(next);
    if (bSuspend == mIsMenuSuspended)
    {
        return;
    }
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (bSuspend)
    {
        // The pause menu (UI-only input, set by the menu subsystem) replaces the ship's input; the sim keeps running
        if (playerController != nullptr)
        {
            SuspendShipControl(*playerController);
        }
        mIsMenuSuspended = true;
        UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: menu open; ship control input suspended"), *GetName());
        return;
    }
    mIsMenuSuspended = false;
    RestoreShipControl();
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: menu closed; ship control input restored"), *GetName());
}

//////////////////////////////////////////////////////////////////////////
// Sets whether the map's right mouse button is held (orbit drag, or pan drag with Shift)
void ASOLShipPawn::HandleMapRightHeld(const bool bHeld)
{
    // A press first takes the OS position the viewport's capture pins the cursor at (and restores at the end)
    if (bHeld)
    {
        UpdateMapCursor();
        mMapRightPressS = FPlatformTime::Seconds();
        mMapRightTravelPx = 0.0;
    }
    else if (mIsMapRightHeld && mIsMapOpen && MapMode != nullptr)
    {
        // Ctrl + a short, still right click undoes the last waypoint (SDD 20); anything longer was an orbit drag
        const APlayerController* playerController = Cast<APlayerController>(GetController());
        const bool bControlDown = playerController != nullptr && (playerController->IsInputKeyDown(EKeys::LeftControl)
            || playerController->IsInputKeyDown(EKeys::RightControl));
        if (SOLMapModeState::IsUndoClick(bControlDown, mMapRightTravelPx, FPlatformTime::Seconds() - mMapRightPressS))
        {
            MapMode->UndoWaypoint();
        }
    }
    mIsMapRightHeld = bHeld;
    HandleMapCameraDragChanged();
}

//////////////////////////////////////////////////////////////////////////
// Sets whether the map's middle mouse button is held (pan drag)
void ASOLShipPawn::HandleMapPanHeld(const bool bHeld)
{
    // A press first takes the OS position the viewport's capture pins the cursor at (and restores at the end)
    if (bHeld)
    {
        UpdateMapCursor();
    }
    mIsMapPanHeld = bHeld;
    HandleMapCameraDragChanged();
}

//////////////////////////////////////////////////////////////////////////
// Tells the map whether a camera drag (right or middle button) is active, and re-syncs the OS cursor once it ends
void ASOLShipPawn::HandleMapCameraDragChanged()
{
    if (mIsMapOpen && MapMode != nullptr)
    {
        MapMode->SetCameraDragActive(mIsMapRightHeld || mIsMapPanHeld);
    }
    SyncOsCursorToMapCursor();
}

//////////////////////////////////////////////////////////////////////////
// Sets whether Shift is held on the map (turns a right drag into a pan drag; previews and locks the pick height)
void ASOLShipPawn::HandleMapPanModifierHeld(const bool bHeld)
{
    mIsMapPanModifierHeld = bHeld;
    if (mIsMapOpen && MapMode != nullptr)
    {
        MapMode->SetHeightModifierHeld(bHeld);
    }
}

//////////////////////////////////////////////////////////////////////////
// Forwards a mouse delta in pixels (X right, Y up) to the map: middle or Shift+right drag pans, right drag orbits
void ASOLShipPawn::HandleMapMouseDelta(const FVector2d& deltaPixels)
{
    if (!mIsMapOpen || MapMode == nullptr)
    {
        return;
    }

    // A left drag moves the map cursor by the delta (screen Y grows downward, the delta's Y is up)
    if (mIsMapPickHeld)
    {
        HandleMapCursorMoved(mMapCursorPx + FVector2D(deltaPixels.X, -deltaPixels.Y));
    }
    if (mIsMapPanHeld || (mIsMapRightHeld && mIsMapPanModifierHeld))
    {
        MapMode->AddPanPixels(deltaPixels);
    }
    else if (mIsMapRightHeld)
    {
        MapMode->AddOrbitPixels(deltaPixels);
    }
    if (mIsMapRightHeld)
    {
        mMapRightTravelPx += deltaPixels.Size();
    }
}

//////////////////////////////////////////////////////////////////////////
// Forwards wheel notches to the map zoom (positive = wheel forward = zoom in)
void ASOLShipPawn::HandleMapZoom(const int32 notches)
{
    if (mIsMapOpen && MapMode != nullptr)
    {
        MapMode->AddZoomNotches(notches);
    }
}

//////////////////////////////////////////////////////////////////////////
// Removes the panel without restoring input (teardown)
void ASOLShipPawn::DismissSpeedPanel()
{
    if (SpeedPanel != nullptr)
    {
        SpeedPanel->RemoveFromParent();
    }
    mIsSpeedPanelOpen = false;
    mIsSpeedPanelRequested = false;
}

//////////////////////////////////////////////////////////////////////////
// Sets the speed cap (clamped to the flight parameters' range) and returns the value set; the panel's apply path
double ASOLShipPawn::ApplySpeedCapMps(const double capMps)
{
    if (Ships == nullptr)
    {
        return mControl.SpeedCapMps;
    }
    const FSOLFlightParams params = Ships->GetFlightParams();
    mControl.SpeedCapMps = FMath::Clamp(capMps, params.MinSpeedCapMps, params.MaxSpeedMps);
    return mControl.SpeedCapMps;
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: thrust axes
void ASOLShipPawn::OnThrustAction(const FInputActionValue& value)
{
    HandleThrust(FVector3d(value.Get<FVector>()));
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: thrust released
void ASOLShipPawn::OnThrustCompleted(const FInputActionValue& /*value*/)
{
    HandleThrust(FVector3d::ZeroVector);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: roll axis
void ASOLShipPawn::OnRollAction(const FInputActionValue& value)
{
    HandleRoll(value.Get<float>());
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: roll released
void ASOLShipPawn::OnRollCompleted(const FInputActionValue& /*value*/)
{
    HandleRoll(0.0);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: boost toggle pressed
void ASOLShipPawn::OnBoostAction(const FInputActionValue& /*value*/)
{
    HandleToggleBoost();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: steer hold pressed
void ASOLShipPawn::OnSteerHoldStarted(const FInputActionValue& /*value*/)
{
    HandleSteerHold(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: steer hold released
void ASOLShipPawn::OnSteerHoldCompleted(const FInputActionValue& /*value*/)
{
    HandleSteerHold(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: steering toggle pressed
void ASOLShipPawn::OnSteerToggleAction(const FInputActionValue& /*value*/)
{
    HandleToggleSteer();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: stop spin pressed
void ASOLShipPawn::OnStopSpinStarted(const FInputActionValue& /*value*/)
{
    HandleStopSpin(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: stop spin released
void ASOLShipPawn::OnStopSpinCompleted(const FInputActionValue& /*value*/)
{
    HandleStopSpin(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: flight-assist toggle
void ASOLShipPawn::OnToggleAssistAction(const FInputActionValue& /*value*/)
{
    HandleToggleAssist();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: free-look pressed
void ASOLShipPawn::OnFreeLookStarted(const FInputActionValue& /*value*/)
{
    HandleFreeLook(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: free-look released
void ASOLShipPawn::OnFreeLookCompleted(const FInputActionValue& /*value*/)
{
    HandleFreeLook(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: mouse delta, converted back to pixels
void ASOLShipPawn::OnLookAction(const FInputActionValue& value)
{
    // The player's mouse sensitivity and invert Y apply to real mouse input only (scripts call HandleMouseDelta)
    const FVector2d deltaPixels = FVector2d(value.Get<FVector2D>()) * mMouseUnitsToPixels;
    if (Settings != nullptr)
    {
        const SOLSettingsModel::FSettingsValues& values = Settings->GetLiveValues();
        HandleMouseDelta(SOLSettingsRules::ApplyLookSettings(deltaPixels, values.MouseSensitivity, values.bInvertY));
        return;
    }
    HandleMouseDelta(deltaPixels);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: joystick recenter
void ASOLShipPawn::OnRecenterAction(const FInputActionValue& /*value*/)
{
    HandleRecenterStick();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: mouse wheel (one step per notch; several notches in one frame step several times)
void ASOLShipPawn::OnSpeedCapAction(const FInputActionValue& value)
{
    const float wheel = value.Get<float>();
    int32 steps = FMath::RoundToInt32(wheel);
    if (steps == 0 && wheel != 0.0f)
    {
        steps = wheel > 0.0f ? 1 : -1;
    }
    HandleSpeedCapSteps(steps);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: reference-frame lock
void ASOLShipPawn::OnMatchLockAction(const FInputActionValue& /*value*/)
{
    HandleMatchLock();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: surface-lock toggle
void ASOLShipPawn::OnToggleLevelAction(const FInputActionValue& /*value*/)
{
    HandleToggleLevel();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: fire trigger pressed
void ASOLShipPawn::OnFireStarted(const FInputActionValue& /*value*/)
{
    HandleFire(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: fire trigger released
void ASOLShipPawn::OnFireCompleted(const FInputActionValue& /*value*/)
{
    HandleFire(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: drop a target
void ASOLShipPawn::OnDropTargetAction(const FInputActionValue& /*value*/)
{
    HandleDropTarget();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: select under reticle
void ASOLShipPawn::OnSelectTargetAction(const FInputActionValue& /*value*/)
{
    HandleSelectTarget();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: next target
void ASOLShipPawn::OnNextTargetAction(const FInputActionValue& /*value*/)
{
    HandleNextTarget();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: previous target
void ASOLShipPawn::OnPreviousTargetAction(const FInputActionValue& /*value*/)
{
    HandlePreviousTarget();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: clear target
void ASOLShipPawn::OnClearTargetAction(const FInputActionValue& /*value*/)
{
    HandleClearTarget();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: warp up
void ASOLShipPawn::OnWarpUpAction(const FInputActionValue& /*value*/)
{
    HandleWarpUp();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: warp down
void ASOLShipPawn::OnWarpDownAction(const FInputActionValue& /*value*/)
{
    HandleWarpDown();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: warp reset
void ASOLShipPawn::OnWarpResetAction(const FInputActionValue& /*value*/)
{
    HandleWarpReset();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: orbit lines toggle
void ASOLShipPawn::OnToggleOrbitLinesAction(const FInputActionValue& /*value*/)
{
    HandleToggleOrbitLines();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: route overlay toggle (P)
void ASOLShipPawn::OnToggleRouteOverlayAction(const FInputActionValue& /*value*/)
{
    HandleToggleRouteOverlay();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: resume a paused route (V)
void ASOLShipPawn::OnResumeRouteAction(const FInputActionValue& /*value*/)
{
    HandleResumeRoute();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: speed panel
void ASOLShipPawn::OnSpeedPanelAction(const FInputActionValue& /*value*/)
{
    HandleToggleSpeedPanel();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: radar zoom in
void ASOLShipPawn::OnRadarZoomInAction(const FInputActionValue& /*value*/)
{
    HandleRadarZoom(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: radar zoom out
void ASOLShipPawn::OnRadarZoomOutAction(const FInputActionValue& /*value*/)
{
    HandleRadarZoom(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: radar back to AUTO range
void ASOLShipPawn::OnRadarAutoAction(const FInputActionValue& /*value*/)
{
    HandleRadarAuto();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: Shift+M (open the map in the last mode / close it on the map)
void ASOLShipPawn::OnOpenMapAction(const FInputActionValue& /*value*/)
{
    HandleOpenMap(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: Shift+J (open the map in Jump mode / Jump mode on the map)
void ASOLShipPawn::OnOpenMapJumpAction(const FInputActionValue& /*value*/)
{
    HandleOpenMap(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: J on the map (Jump mode)
void ASOLShipPawn::OnMapModeJumpAction(const FInputActionValue& /*value*/)
{
    HandleMapModeKey(ESOLMapMode::Jump);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: M on the map (Move mode)
void ASOLShipPawn::OnMapModeMoveAction(const FInputActionValue& /*value*/)
{
    HandleMapModeKey(ESOLMapMode::Move);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: P on the map (Patrol mode; again flips loop / ping-pong)
void ASOLShipPawn::OnMapModePatrolAction(const FInputActionValue& /*value*/)
{
    HandleMapModeKey(ESOLMapMode::Patrol);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: Delete on the map (clear the active route)
void ASOLShipPawn::OnMapClearRouteAction(const FInputActionValue& /*value*/)
{
    HandleMapClearRoute();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: jump map close (Esc)
void ASOLShipPawn::OnMapCloseAction(const FInputActionValue& /*value*/)
{
    HandleCloseMap();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: pause menu (Esc in the ship mapping)
void ASOLShipPawn::OnPauseAction(const FInputActionValue& /*value*/)
{
    HandlePause();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map right mouse button pressed
void ASOLShipPawn::OnMapRightStarted(const FInputActionValue& /*value*/)
{
    HandleMapRightHeld(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map right mouse button released
void ASOLShipPawn::OnMapRightCompleted(const FInputActionValue& /*value*/)
{
    HandleMapRightHeld(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map middle mouse button (pan) pressed
void ASOLShipPawn::OnMapPanStarted(const FInputActionValue& /*value*/)
{
    HandleMapPanHeld(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map middle mouse button (pan) released
void ASOLShipPawn::OnMapPanCompleted(const FInputActionValue& /*value*/)
{
    HandleMapPanHeld(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map Shift (pan modifier) pressed
void ASOLShipPawn::OnMapPanModifierStarted(const FInputActionValue& /*value*/)
{
    HandleMapPanModifierHeld(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map Shift (pan modifier) released
void ASOLShipPawn::OnMapPanModifierCompleted(const FInputActionValue& /*value*/)
{
    HandleMapPanModifierHeld(false);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: mouse delta on the map, converted back to pixels
void ASOLShipPawn::OnMapLookAction(const FInputActionValue& value)
{
    HandleMapMouseDelta(FVector2d(value.Get<FVector2D>()) * mMouseUnitsToPixels);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: mouse wheel on the map (one step per notch; several notches in one frame step several times)
void ASOLShipPawn::OnMapZoomAction(const FInputActionValue& value)
{
    const float wheel = value.Get<float>();
    int32 notches = FMath::RoundToInt32(wheel);
    if (notches == 0 && wheel != 0.0f)
    {
        notches = wheel > 0.0f ? 1 : -1;
    }
    HandleMapZoom(notches);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map left mouse button (destination pick) pressed
void ASOLShipPawn::OnMapPickStarted(const FInputActionValue& /*value*/)
{
    HandleMapPickPressed();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map left mouse button (destination pick) released
void ASOLShipPawn::OnMapPickCompleted(const FInputActionValue& /*value*/)
{
    HandleMapPickReleased();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map X (clear the destination pick)
void ASOLShipPawn::OnMapClearPickAction(const FInputActionValue& /*value*/)
{
    HandleMapClearPick();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: map Enter (jump to the destination)
void ASOLShipPawn::OnMapJumpAction(const FInputActionValue& /*value*/)
{
    HandleMapJump();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: F1; with Ctrl held it acts as Esc (closes the map, else opens the pause menu)
void ASOLShipPawn::OnMenuHotkeyAction(const FInputActionValue& /*value*/)
{
    if (!SOLKeyBindings::IsMenuHotkey(EKeys::F1.GetFName().ToString(), IsControlHeld()))
    {
        return;
    }
    if (mIsMapOpen)
    {
        HandleCloseMap();
    }
    else
    {
        HandlePause();
    }
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: F2; with Ctrl held it shows or hides the NPC stats overlay
void ASOLShipPawn::OnStatsHotkeyAction(const FInputActionValue& /*value*/)
{
    USOLNpcSubsystem* npcs = GetWorld() != nullptr ? GetWorld()->GetSubsystem<USOLNpcSubsystem>() : nullptr;
    if (npcs != nullptr && SOLKeyBindings::IsStatsHotkey(EKeys::F2.GetFName().ToString(), IsControlHeld()))
    {
        npcs->SetStatsVisible(!npcs->IsStatsVisible());
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns whether Left or Right Ctrl is held
bool ASOLShipPawn::IsControlHeld() const
{
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    return playerController != nullptr && (playerController->IsInputKeyDown(EKeys::LeftControl)
        || playerController->IsInputKeyDown(EKeys::RightControl));
}