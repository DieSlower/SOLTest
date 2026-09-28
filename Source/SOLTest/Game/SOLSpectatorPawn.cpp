/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Game/SOLSpectatorPawn.h"

#include "SOLConstants.h"
#include "SOLTest.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Camera/CameraComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    const TCHAR* const SPECTATOR_DEFAULT_START_BODY = SOL::BodyNames::EARTH;

    //////////////////////////////////////////////////////////////////////////
    // Maps a key to an action, optionally swizzling and negating the axis, and returns the mapping for triggers
    FEnhancedActionKeyMapping& MapAxisKey(UInputMappingContext* context, UInputAction* action, const FKey& key,
        UObject* outer, const bool bSwizzle, const EInputAxisSwizzle order, const bool bNegate)
    {
        FEnhancedActionKeyMapping& mapping = context->MapKey(action, key);
        if (bSwizzle)
        {
            UInputModifierSwizzleAxis* swizzle = NewObject<UInputModifierSwizzleAxis>(outer);
            swizzle->Order = order;
            mapping.Modifiers.Add(swizzle);
        }
        if (bNegate)
        {
            mapping.Modifiers.Add(NewObject<UInputModifierNegate>(outer));
        }
        return mapping;
    }

    //////////////////////////////////////////////////////////////////////////
    // Creates an input action of the given value type
    UInputAction* CreateAction(UObject* outer, const TCHAR* name, const EInputActionValueType valueType)
    {
        UInputAction* action = NewObject<UInputAction>(outer, name);
        action->ValueType = valueType;
        return action;
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the camera and enables ticking
ASOLSpectatorPawn::ASOLSpectatorPawn()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->bUsePawnControlRotation = true;
    RootComponent = Camera;

    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;
}

//////////////////////////////////////////////////////////////////////////
// Places the observer above the start body and registers this actor as the observer
void ASOLSpectatorPawn::BeginPlay()
{
    Super::BeginPlay();

    mSpeedStep = StartSpeedStep;
    UWorld* world = GetWorld();
    AnchorSubsystem = world->GetSubsystem<USOLAnchorSubsystem>();
    SimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    const USOLBodyRegistrySubsystem* registrySubsystem = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    if (AnchorSubsystem == nullptr || registrySubsystem == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("SpectatorPawn %s: universe subsystems missing"), *GetName());
        return;
    }

    // Start body, altitude and view target, overridable from the command line for verification runs
    FString startBody = SPECTATOR_DEFAULT_START_BODY;
    FParse::Value(FCommandLine::Get(), TEXT("SOLStart="), startBody);
    FString lookAtBody = startBody;
    FParse::Value(FCommandLine::Get(), TEXT("SOLLookAt="), lookAtBody);
    double altitudeKm = SOL::DEFAULT_SPAWN_ALTITUDE_M / SOL::METERS_PER_KM;
    FParse::Value(FCommandLine::Get(), TEXT("SOLAltitudeKm="), altitudeKm);

    const FSOLBodyRegistry& registry = registrySubsystem->GetRegistry();
    int32 bodyIndex = registry.FindByName(FName(*startBody));
    if (bodyIndex == INDEX_NONE)
    {
        UE_LOG(LogSOL, Warning, TEXT("SpectatorPawn %s: unknown start body '%s', using %s"), *GetName(), *startBody,
            SPECTATOR_DEFAULT_START_BODY);
        bodyIndex = registry.FindByName(FName(SPECTATOR_DEFAULT_START_BODY));
    }

    // Spawn on the body's Sun-facing side; by default look back at the body so its day side fills the view
    const FVector3d bodyPositionM = registry.GetPositionM(bodyIndex);
    FVector3d towardSun = (-bodyPositionM).GetSafeNormal();
    if (towardSun.IsNearlyZero())
    {
        towardSun = FVector3d::XAxisVector;
    }
    const FVector3d observerM = bodyPositionM + towardSun * (registry.GetRadiusM(bodyIndex)
        + altitudeKm * SOL::METERS_PER_KM);
    AnchorSubsystem->SetObserverPositionM(observerM);
    AnchorSubsystem->SetObserverActor(this);

    const int32 lookAtIndex = registry.FindByName(FName(*lookAtBody));
    const FVector3d lookDirection = lookAtIndex == INDEX_NONE || lookAtIndex == bodyIndex ? -towardSun
        : (registry.GetPositionM(lookAtIndex) - observerM).GetSafeNormal();
    const FRotator lookRotation = FVector(SOLRender::EclipticToUnreal(lookDirection)).Rotation();
    SetActorRotation(lookRotation);
    if (AController* controller = GetController())
    {
        controller->SetControlRotation(lookRotation);
    }
    UE_LOG(LogSOL, Log, TEXT("SpectatorPawn %s: spawned %.0f km above %s"), *GetName(), altitudeKm,
        *registry.GetName(bodyIndex).ToString());
}

//////////////////////////////////////////////////////////////////////////
// Removes the mapping context and unregisters this actor as the observer
void ASOLSpectatorPawn::EndPlay(const EEndPlayReason::Type endPlayReason)
{
    RemoveMappingContext();
    if (AnchorSubsystem != nullptr)
    {
        AnchorSubsystem->SetObserverActor(nullptr);
    }
    Super::EndPlay(endPlayReason);
}

//////////////////////////////////////////////////////////////////////////
// Moves the observer by the current input and mirrors it into render space
void ASOLSpectatorPawn::Tick(const float deltaSeconds)
{
    Super::Tick(deltaSeconds);
    if (AnchorSubsystem == nullptr)
    {
        return;
    }

    // Camera-relative direction in Unreal axes, converted to the ecliptic universe frame in double precision
    const FRotationMatrix view(GetControlRotation());
    const FVector input = mMoveInput.GetClampedToMaxSize(1.0);
    const FVector directionUnreal = view.GetUnitAxis(EAxis::X) * input.X + view.GetUnitAxis(EAxis::Y) * input.Y
        + view.GetUnitAxis(EAxis::Z) * input.Z;
    const double speedMps = GetSpeedCapMps();
    const FVector3d deltaM = SOLRender::EclipticToUnreal(FVector3d(directionUnreal)) * (speedMps * deltaSeconds);
    AnchorSubsystem->MoveObserverM(deltaM);
    mCurrentSpeedMps = deltaSeconds > 0.0f ? deltaM.Size() / deltaSeconds : 0.0;

    SetActorLocation(AnchorSubsystem->UniverseToRenderCm(AnchorSubsystem->GetObserverPositionM()));
}

//////////////////////////////////////////////////////////////////////////
// Returns the current speed cap in m/s
double ASOLSpectatorPawn::GetSpeedCapMps() const
{
    const double exponent = static_cast<double>(mSpeedStep) / static_cast<double>(FMath::Max(SpeedStepsPerDecade, 1));
    return FMath::Min(SOL::MIN_SPEED_CAP_MPS * FMath::Pow(10.0, exponent), SOL::MAX_SPEED_CAP_MPS);
}

//////////////////////////////////////////////////////////////////////////
// Builds the input objects and binds the actions
void ASOLSpectatorPawn::SetupPlayerInputComponent(UInputComponent* playerInputComponent)
{
    Super::SetupPlayerInputComponent(playerInputComponent);
    CreateInputObjects();

    UEnhancedInputComponent* input = Cast<UEnhancedInputComponent>(playerInputComponent);
    if (input == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("SpectatorPawn %s: Enhanced Input component missing"), *GetName());
        return;
    }
    input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ASOLSpectatorPawn::HandleMove);
    input->BindAction(MoveAction, ETriggerEvent::Completed, this, &ASOLSpectatorPawn::HandleMoveCompleted);
    input->BindAction(LookAction, ETriggerEvent::Triggered, this, &ASOLSpectatorPawn::HandleLook);
    input->BindAction(SpeedAction, ETriggerEvent::Triggered, this, &ASOLSpectatorPawn::HandleSpeedStep);
    input->BindAction(WarpUpAction, ETriggerEvent::Triggered, this, &ASOLSpectatorPawn::HandleWarpUp);
    input->BindAction(WarpDownAction, ETriggerEvent::Triggered, this, &ASOLSpectatorPawn::HandleWarpDown);
    input->BindAction(WarpResetAction, ETriggerEvent::Triggered, this, &ASOLSpectatorPawn::HandleWarpReset);
}

//////////////////////////////////////////////////////////////////////////
// Moves the mapping context from the previous local player (if any) to the new local controller
void ASOLSpectatorPawn::NotifyControllerChanged()
{
    Super::NotifyControllerChanged();
    CreateInputObjects();
    RemoveMappingContext();

    const APlayerController* playerController = Cast<APlayerController>(GetController());
    if (playerController == nullptr)
    {
        return;
    }
    if (UEnhancedInputLocalPlayerSubsystem* inputSubsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(playerController->GetLocalPlayer()))
    {
        inputSubsystem->AddMappingContext(MappingContext, 0);
        mMappedLocalPlayer = playerController->GetLocalPlayer();
    }
}

//////////////////////////////////////////////////////////////////////////
// Removes the mapping context before the controller lets go of this pawn
void ASOLSpectatorPawn::UnPossessed()
{
    RemoveMappingContext();
    Super::UnPossessed();
}

//////////////////////////////////////////////////////////////////////////
// Removes the mapping context from the local player it was added to, if any
void ASOLSpectatorPawn::RemoveMappingContext()
{
    ULocalPlayer* localPlayer = mMappedLocalPlayer.Get();
    mMappedLocalPlayer.Reset();
    if (localPlayer == nullptr || MappingContext == nullptr)
    {
        return;
    }
    if (UEnhancedInputLocalPlayerSubsystem* inputSubsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(localPlayer))
    {
        inputSubsystem->RemoveMappingContext(MappingContext);
    }
}

//////////////////////////////////////////////////////////////////////////
// Creates the input actions and the mapping context once
void ASOLSpectatorPawn::CreateInputObjects()
{
    if (MappingContext != nullptr)
    {
        return;
    }
    MoveAction = CreateAction(this, TEXT("IA_SpectatorMove"), EInputActionValueType::Axis3D);
    LookAction = CreateAction(this, TEXT("IA_SpectatorLook"), EInputActionValueType::Axis2D);
    SpeedAction = CreateAction(this, TEXT("IA_SpectatorSpeed"), EInputActionValueType::Axis1D);
    WarpUpAction = CreateAction(this, TEXT("IA_WarpUp"), EInputActionValueType::Boolean);
    WarpDownAction = CreateAction(this, TEXT("IA_WarpDown"), EInputActionValueType::Boolean);
    WarpResetAction = CreateAction(this, TEXT("IA_WarpReset"), EInputActionValueType::Boolean);
    MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Spectator"));

    // Move: W/S forward/back (x), D/A right/left (y), Space/Ctrl up/down (z)
    MapAxisKey(MappingContext, MoveAction, EKeys::W, this, false, EInputAxisSwizzle::YXZ, false);
    MapAxisKey(MappingContext, MoveAction, EKeys::S, this, false, EInputAxisSwizzle::YXZ, true);
    MapAxisKey(MappingContext, MoveAction, EKeys::D, this, true, EInputAxisSwizzle::YXZ, false);
    MapAxisKey(MappingContext, MoveAction, EKeys::A, this, true, EInputAxisSwizzle::YXZ, true);
    MapAxisKey(MappingContext, MoveAction, EKeys::SpaceBar, this, true, EInputAxisSwizzle::ZYX, false);
    MapAxisKey(MappingContext, MoveAction, EKeys::LeftControl, this, true, EInputAxisSwizzle::ZYX, true);

    // Look: mouse with Y negated (mouse up looks up), as in the engine templates
    FEnhancedActionKeyMapping& lookMapping = MappingContext->MapKey(LookAction, EKeys::Mouse2D);
    UInputModifierNegate* negateY = NewObject<UInputModifierNegate>(this);
    negateY->bX = false;
    negateY->bZ = false;
    lookMapping.Modifiers.Add(negateY);

    // Speed: mouse wheel; time-warp: [ ] Backspace, once per press
    MappingContext->MapKey(SpeedAction, EKeys::MouseWheelAxis);
    MappingContext->MapKey(WarpDownAction, EKeys::LeftBracket).Triggers.Add(NewObject<UInputTriggerPressed>(this));
    MappingContext->MapKey(WarpUpAction, EKeys::RightBracket).Triggers.Add(NewObject<UInputTriggerPressed>(this));
    MappingContext->MapKey(WarpResetAction, EKeys::BackSpace).Triggers.Add(NewObject<UInputTriggerPressed>(this));
}

//////////////////////////////////////////////////////////////////////////
// Stores the move input (x forward, y right, z up)
void ASOLSpectatorPawn::HandleMove(const FInputActionValue& value)
{
    mMoveInput = value.Get<FVector>();
}

//////////////////////////////////////////////////////////////////////////
// Clears the move input when the keys are released
void ASOLSpectatorPawn::HandleMoveCompleted(const FInputActionValue& /*value*/)
{
    mMoveInput = FVector::ZeroVector;
}

//////////////////////////////////////////////////////////////////////////
// Applies mouse look to the control rotation
void ASOLSpectatorPawn::HandleLook(const FInputActionValue& value)
{
    const FVector2D look = value.Get<FVector2D>();
    AddControllerYawInput(look.X);
    AddControllerPitchInput(look.Y);
}

//////////////////////////////////////////////////////////////////////////
// Steps the speed cap up or down by the wheel direction
void ASOLSpectatorPawn::HandleSpeedStep(const FInputActionValue& value)
{
    const float wheel = value.Get<float>();
    const int32 steps = FMath::Max(SpeedStepsPerDecade, 1);
    const int32 maxStep = FMath::CeilToInt32(steps * FMath::LogX(10.0, SOL::MAX_SPEED_CAP_MPS / SOL::MIN_SPEED_CAP_MPS));
    mSpeedStep = FMath::Clamp(mSpeedStep + (wheel > 0.0f ? 1 : -1), 0, maxStep);
}

//////////////////////////////////////////////////////////////////////////
// Steps the time-warp up
void ASOLSpectatorPawn::HandleWarpUp(const FInputActionValue& /*value*/)
{
    if (SimClock != nullptr)
    {
        SimClock->StepWarpUp();
    }
}

//////////////////////////////////////////////////////////////////////////
// Steps the time-warp down
void ASOLSpectatorPawn::HandleWarpDown(const FInputActionValue& /*value*/)
{
    if (SimClock != nullptr)
    {
        SimClock->StepWarpDown();
    }
}

//////////////////////////////////////////////////////////////////////////
// Resets the time-warp to 1x
void ASOLSpectatorPawn::HandleWarpReset(const FInputActionValue& /*value*/)
{
    if (SimClock != nullptr)
    {
        SimClock->ResetWarp();
    }
}
