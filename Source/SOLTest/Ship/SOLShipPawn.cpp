/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipPawn.h"

#include "Game/SOLInputHelpers.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "UI/SOLSpeedPanelWidget.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
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

    // Shadowless fill "headlight" along the view so the ship's side facing away from the Sun is not black
    FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("FillLight"));
    FillLight->SetupAttachment(Camera);
    FillLight->SetRelativeRotation(FRotator(SOL::SHIP_FILL_LIGHT_PITCH_DEG, 0.0f, 0.0f));
    FillLight->SetMobility(EComponentMobility::Movable);
    FillLight->SetIntensity(SOL::SUN_ILLUMINANCE_LUX * SOL::SHIP_FILL_LIGHT_FRACTION);
    FillLight->SetCastShadows(false);

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
    if (Ships == nullptr || Targeting == nullptr || AnchorSubsystem == nullptr || SimClock == nullptr
        || !Ships->HasPlayerShip())
    {
        UE_LOG(LogSOL, Error, TEXT("ShipPawn %s: ship or universe subsystems missing; the pawn is inert"), *GetName());
        return;
    }

    // Start state per SDD 2: assist on, cap at the start value, stick centered
    mControl = Ships->GetControl();
    mControl.Thrust = FVector3d::ZeroVector;
    mControl.Rotation = FVector3d::ZeroVector;
    mControl.bBoost = false;
    mControl.bFlightAssist = true;
    mControl.SpeedCapMps = SOL::SHIP_START_SPEED_CAP_MPS;
    BuildShipMesh();

    // The pawn represents the observer: rebases move it, and it follows the ship after every universe update
    AnchorSubsystem->SetObserverActor(this);
    mUniverseUpdatedHandle = AnchorSubsystem->OnUniverseUpdated().AddUObject(this, &ASOLShipPawn::FollowShip);
    FollowShip();

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
    mUniverseUpdatedHandle.Reset();
    mSmokeInput.Reset();
    mSmokeHud.Reset();
    mSmokeMap.Reset();
    mSmokeMapPick.Reset();
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
        else
        {
            OpenMap();
        }
    }
    UpdateViewportFocus();
    UpdateMapCursor();

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
    const FVector2d stick = SOLFlight::JoystickToRotation(mStickOffsetPx / mStickRadiusPx, SOL::JOYSTICK_DEAD_ZONE,
        SOL::JOYSTICK_EXPONENT);
    mControl.Rotation = FVector3d(mRollInput, stick.Y, stick.X);
    if (!Ships->IsControlScripted())
    {
        Ships->SetControl(mControl);
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
    input->BindAction(BoostAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnBoostStarted);
    input->BindAction(BoostAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnBoostCompleted);
    input->BindAction(FreeLookAction, ETriggerEvent::Started, this, &ASOLShipPawn::OnFreeLookStarted);
    input->BindAction(FreeLookAction, ETriggerEvent::Completed, this, &ASOLShipPawn::OnFreeLookCompleted);
    input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnLookAction);
    input->BindAction(SpeedCapAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnSpeedCapAction);

    // One-shot keys trigger once per press
    input->BindAction(ToggleAssistAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnToggleAssistAction);
    input->BindAction(RecenterAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRecenterAction);
    input->BindAction(MatchLockAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMatchLockAction);
    input->BindAction(SelectTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnSelectTargetAction);
    input->BindAction(NextTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnNextTargetAction);
    input->BindAction(PreviousTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnPreviousTargetAction);
    input->BindAction(ClearTargetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnClearTargetAction);
    input->BindAction(WarpUpAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnWarpUpAction);
    input->BindAction(WarpDownAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnWarpDownAction);
    input->BindAction(WarpResetAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnWarpResetAction);
    input->BindAction(ToggleOrbitLinesAction, ETriggerEvent::Triggered, this,
        &ASOLShipPawn::OnToggleOrbitLinesAction);
    input->BindAction(TogglePredictedPathAction, ETriggerEvent::Triggered, this,
        &ASOLShipPawn::OnTogglePredictedPathAction);
    input->BindAction(SpeedPanelAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnSpeedPanelAction);
    input->BindAction(RadarZoomInAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRadarZoomInAction);
    input->BindAction(RadarZoomOutAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRadarZoomOutAction);
    input->BindAction(RadarAutoAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnRadarAutoAction);

    // Jump map: J toggles (mapped in both contexts); the rest only exist in the map's context while it is open
    input->BindAction(ToggleMapAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnToggleMapAction);
    input->BindAction(MapCloseAction, ETriggerEvent::Triggered, this, &ASOLShipPawn::OnMapCloseAction);
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
        || mIsSpeedPanelOpen || mIsMapOpen)
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
}

//////////////////////////////////////////////////////////////////////////
// Recenters the virtual stick and zeroes thrust, roll and boost, so nothing stays held while the game is unfocused
void ASOLShipPawn::HandleFocusLost()
{
    mStickOffsetPx = FVector2d::ZeroVector;
    mRollInput = 0.0;
    mControl.Thrust = FVector3d::ZeroVector;
    mControl.Rotation = FVector3d::ZeroVector;
    mControl.bBoost = false;
    if (Ships != nullptr && !Ships->IsControlScripted())
    {
        Ships->SetControl(mControl);
    }
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: viewport lost focus; stick recentered, thrust/roll/boost released"),
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
    ToggleAssistAction = CreateAction(this, TEXT("IA_ShipToggleAssist"), EInputActionValueType::Boolean);
    FreeLookAction = CreateAction(this, TEXT("IA_ShipFreeLook"), EInputActionValueType::Boolean);
    LookAction = CreateAction(this, TEXT("IA_ShipLook"), EInputActionValueType::Axis2D);
    RecenterAction = CreateAction(this, TEXT("IA_ShipRecenterStick"), EInputActionValueType::Boolean);
    SpeedCapAction = CreateAction(this, TEXT("IA_ShipSpeedCap"), EInputActionValueType::Axis1D);
    MatchLockAction = CreateAction(this, TEXT("IA_ShipMatchLock"), EInputActionValueType::Boolean);
    SelectTargetAction = CreateAction(this, TEXT("IA_ShipSelectTarget"), EInputActionValueType::Boolean);
    NextTargetAction = CreateAction(this, TEXT("IA_ShipNextTarget"), EInputActionValueType::Boolean);
    PreviousTargetAction = CreateAction(this, TEXT("IA_ShipPreviousTarget"), EInputActionValueType::Boolean);
    ClearTargetAction = CreateAction(this, TEXT("IA_ShipClearTarget"), EInputActionValueType::Boolean);
    WarpUpAction = CreateAction(this, TEXT("IA_ShipWarpUp"), EInputActionValueType::Boolean);
    WarpDownAction = CreateAction(this, TEXT("IA_ShipWarpDown"), EInputActionValueType::Boolean);
    WarpResetAction = CreateAction(this, TEXT("IA_ShipWarpReset"), EInputActionValueType::Boolean);
    ToggleOrbitLinesAction = CreateAction(this, TEXT("IA_ShipToggleOrbitLines"), EInputActionValueType::Boolean);
    TogglePredictedPathAction = CreateAction(this, TEXT("IA_ShipTogglePredictedPath"),
        EInputActionValueType::Boolean);
    SpeedPanelAction = CreateAction(this, TEXT("IA_ShipSpeedPanel"), EInputActionValueType::Boolean);
    RadarZoomInAction = CreateAction(this, TEXT("IA_ShipRadarZoomIn"), EInputActionValueType::Boolean);
    RadarZoomOutAction = CreateAction(this, TEXT("IA_ShipRadarZoomOut"), EInputActionValueType::Boolean);
    RadarAutoAction = CreateAction(this, TEXT("IA_ShipRadarAuto"), EInputActionValueType::Boolean);
    ToggleMapAction = CreateAction(this, TEXT("IA_MapToggle"), EInputActionValueType::Boolean);
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

    // Thrust: W/S forward/back (x), D/A right/left (y), Space/Ctrl up/down (z); roll: E right, Q left
    MapAxisKey(MappingContext, ThrustAction, EKeys::W, this, false, EInputAxisSwizzle::YXZ, false);
    MapAxisKey(MappingContext, ThrustAction, EKeys::S, this, false, EInputAxisSwizzle::YXZ, true);
    MapAxisKey(MappingContext, ThrustAction, EKeys::D, this, true, EInputAxisSwizzle::YXZ, false);
    MapAxisKey(MappingContext, ThrustAction, EKeys::A, this, true, EInputAxisSwizzle::YXZ, true);
    MapAxisKey(MappingContext, ThrustAction, EKeys::SpaceBar, this, true, EInputAxisSwizzle::ZYX, false);
    MapAxisKey(MappingContext, ThrustAction, EKeys::LeftControl, this, true, EInputAxisSwizzle::ZYX, true);
    MapAxisKey(MappingContext, RollAction, EKeys::E, this, false, EInputAxisSwizzle::YXZ, false);
    MapAxisKey(MappingContext, RollAction, EKeys::Q, this, false, EInputAxisSwizzle::YXZ, true);

    // Held keys and axes: boost, free-look, the mouse (virtual joystick) and the wheel (speed cap)
    MappingContext->MapKey(BoostAction, EKeys::LeftShift);
    MappingContext->MapKey(FreeLookAction, EKeys::LeftAlt);
    MappingContext->MapKey(LookAction, EKeys::Mouse2D);
    MappingContext->MapKey(SpeedCapAction, EKeys::MouseWheelAxis);

    // One-shot keys, including the HUD keys: O orbit lines, P predicted path (reserved), F3 speed panel, '-' / '='
    // radar zoom in/out, Home radar back to AUTO
    MapPressedKey(MappingContext, ToggleAssistAction, EKeys::Tab, this);
    MapPressedKey(MappingContext, RecenterAction, EKeys::MiddleMouseButton, this);
    MapPressedKey(MappingContext, MatchLockAction, EKeys::M, this);
    MapPressedKey(MappingContext, SelectTargetAction, EKeys::T, this);
    MapPressedKey(MappingContext, NextTargetAction, EKeys::R, this);
    MapPressedKey(MappingContext, PreviousTargetAction, EKeys::F, this);
    MapPressedKey(MappingContext, ClearTargetAction, EKeys::X, this);
    MapPressedKey(MappingContext, WarpDownAction, EKeys::LeftBracket, this);
    MapPressedKey(MappingContext, WarpUpAction, EKeys::RightBracket, this);
    MapPressedKey(MappingContext, WarpResetAction, EKeys::BackSpace, this);
    MapPressedKey(MappingContext, ToggleOrbitLinesAction, EKeys::O, this);
    MapPressedKey(MappingContext, TogglePredictedPathAction, EKeys::P, this);
    MapPressedKey(MappingContext, SpeedPanelAction, EKeys::F3, this);
    MapPressedKey(MappingContext, RadarZoomInAction, EKeys::Hyphen, this);
    MapPressedKey(MappingContext, RadarZoomOutAction, EKeys::Equals, this);
    MapPressedKey(MappingContext, RadarAutoAction, EKeys::Home, this);
    MapPressedKey(MappingContext, ToggleMapAction, EKeys::J, this);

    // Jump map: J or Esc close; right drag orbits, middle or Shift+right drag pans (held buttons gate the mouse
    // delta), the wheel zooms. Destination picking: left button (press and release), Shift (the same action as the
    // pan modifier) previews and locks the height, X clears the pick, Enter jumps. The ship's X (clear target) lives
    // in the ship's context, which is removed while the map is open, so the two never conflict
    MapPressedKey(MapMappingContext, ToggleMapAction, EKeys::J, this);
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
    part->SetupAttachment(ShipRoot);
    part->SetRelativeTransform(FTransform(rotation, locationCm, scale));
    part->RegisterComponent();
    ShipParts.Add(part);
}

//////////////////////////////////////////////////////////////////////////
// Places the actor at the observer's render location with the ship's orientation after the universe update
void ASOLShipPawn::FollowShip()
{
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
// Sets whether boost is held
void ASOLShipPawn::HandleBoost(const bool bBoost)
{
    mControl.bBoost = bBoost;
}

//////////////////////////////////////////////////////////////////////////
// Toggles flight-assist
void ASOLShipPawn::HandleToggleAssist()
{
    mControl.bFlightAssist = !mControl.bFlightAssist;
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: flight-assist %s"), *GetName(), mControl.bFlightAssist ? TEXT("ON")
        : TEXT("OFF"));
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
    if (Targeting != nullptr)
    {
        Targeting->ToggleFrameLock();
    }
}

//////////////////////////////////////////////////////////////////////////
// Selects the target under the forward reticle (T)
void ASOLShipPawn::HandleSelectTarget()
{
    FVector3d positionM;
    FVector3d forwardDir;
    if (Targeting != nullptr && GetShipPose(positionM, forwardDir))
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
    if (Targeting != nullptr && GetShipPose(positionM, forwardDir))
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
    if (Targeting != nullptr && GetShipPose(positionM, forwardDir))
    {
        Targeting->CycleTarget(positionM, -1);
    }
}

//////////////////////////////////////////////////////////////////////////
// Clears the selected target (X)
void ASOLShipPawn::HandleClearTarget()
{
    if (Targeting != nullptr)
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
// Toggles the flight HUD's body orbit ellipses (O)
void ASOLShipPawn::HandleToggleOrbitLines()
{
    const APlayerController* playerController = Cast<APlayerController>(GetController());
    if (ASOLFlightHud* hud = playerController != nullptr ? playerController->GetHUD<ASOLFlightHud>() : nullptr)
    {
        hud->ToggleOrbitLines();
    }
}

//////////////////////////////////////////////////////////////////////////
// Reserved for the ship's predicted path (P); does nothing yet (SDD 2 section 6)
void ASOLShipPawn::HandleTogglePredictedPath()
{
    UE_LOG(LogSOL, Log, TEXT("ShipPawn %s: predicted path (P) is reserved and not built yet"), *GetName());
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
    if (mIsSpeedPanelOpen || mIsMapOpen || playerController == nullptr || Ships == nullptr)
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
    SpeedPanel->Open(this, mControl.SpeedCapMps);
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
    mControl.Thrust = FVector3d::ZeroVector;
    mControl.Rotation = FVector3d::ZeroVector;
    mControl.bBoost = false;
    mIsFreeLooking = false;
    mFreeLookRotation = FRotator::ZeroRotator;
}

//////////////////////////////////////////////////////////////////////////
// Restores the ship's mapping and the hidden, captured cursor of the virtual joystick
void ASOLShipPawn::RestoreShipControl()
{
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (playerController != nullptr)
    {
        if (SOLInput::AddMappingContext(playerController->GetLocalPlayer(), MappingContext, 0))
        {
            mMappedLocalPlayer = playerController->GetLocalPlayer();
        }
        playerController->bShowMouseCursor = false;
        playerController->SetInputMode(FInputModeGameOnly());
        mHasCapturedMouse = true;
    }
    mHadViewportFocus = false;
}

//////////////////////////////////////////////////////////////////////////
// Opens the jump map: adds the map mapping (rolling back on failure), suspends ship control and frees the OS cursor
void ASOLShipPawn::OpenMap()
{
    APlayerController* playerController = Cast<APlayerController>(GetController());
    if (mIsMapOpen || mIsSpeedPanelOpen || playerController == nullptr || MapMode == nullptr)
    {
        return;
    }
    if (!MapMode->OpenMap(playerController))
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
    if (APlayerController* playerController = Cast<APlayerController>(GetController()))
    {
        playerController->FlushPressedKeys();
    }
    DismissMap();
    RestoreShipControl();
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
    mIsMapOpen = false;
    mIsMapToggleRequested = false;
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
        MapMode->ClearPick();
    }
}

//////////////////////////////////////////////////////////////////////////
// Asks the map to jump to its locked destination (Enter; for now the map only logs it)
void ASOLShipPawn::HandleMapJump()
{
    if (mIsMapOpen && MapMode != nullptr)
    {
        MapMode->RequestJump();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests the jump map to open or close (J; done at the next tick, outside the input callback)
void ASOLShipPawn::HandleToggleMap()
{
    mIsMapToggleRequested = true;
}

//////////////////////////////////////////////////////////////////////////
// Requests the jump map to close (Esc; done at the next tick)
void ASOLShipPawn::HandleCloseMap()
{
    mIsMapToggleRequested = mIsMapOpen;
}

//////////////////////////////////////////////////////////////////////////
// Sets whether the map's right mouse button is held (orbit drag, or pan drag with Shift)
void ASOLShipPawn::HandleMapRightHeld(const bool bHeld)
{
    // A press first takes the OS position the viewport's capture pins the cursor at (and restores at the end)
    if (bHeld)
    {
        UpdateMapCursor();
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
// Enhanced Input: boost pressed
void ASOLShipPawn::OnBoostStarted(const FInputActionValue& /*value*/)
{
    HandleBoost(true);
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: boost released
void ASOLShipPawn::OnBoostCompleted(const FInputActionValue& /*value*/)
{
    HandleBoost(false);
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
    HandleMouseDelta(FVector2d(value.Get<FVector2D>()) * mMouseUnitsToPixels);
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
// Enhanced Input: predicted path toggle (reserved)
void ASOLShipPawn::OnTogglePredictedPathAction(const FInputActionValue& /*value*/)
{
    HandleTogglePredictedPath();
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
// Enhanced Input: jump map toggle (J, in both the ship and the map mapping)
void ASOLShipPawn::OnToggleMapAction(const FInputActionValue& /*value*/)
{
    HandleToggleMap();
}

//////////////////////////////////////////////////////////////////////////
// Enhanced Input: jump map close (Esc)
void ASOLShipPawn::OnMapCloseAction(const FInputActionValue& /*value*/)
{
    HandleCloseMap();
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
