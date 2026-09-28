/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLShipSmokeInput.h"

#include "Flight/SOLTargeting.h"
#include "Game/SOLGameMode.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"

namespace
{
    // Phase durations (real seconds) and frame counts
    constexpr double INPUT_TAP_PHASE_S = 0.3;               // Long enough for a tapped key to take effect
    constexpr double INPUT_SETUP_S = 1.0;
    constexpr double INPUT_THRUST_S = 3.0;
    constexpr double INPUT_BRAKE_S = 10.0;
    constexpr double INPUT_NEWTON_THRUST_S = 2.0;
    constexpr double INPUT_COAST_S = 2.0;
    constexpr double INPUT_TURN_S = 1.2;
    constexpr double INPUT_RECENTER_S = 1.0;
    constexpr double INPUT_AIM_S = 1.0;
    constexpr double INPUT_SEEK_S = 3.0;
    constexpr double INPUT_LOCK_RAMP_S = 6.0;
    constexpr double INPUT_SCREENSHOT_SETTLE_S = 3.0;
    constexpr int32 INPUT_MOUSE_FRAMES = 10;                // Frames of mouse motion per turn
    constexpr float INPUT_MOUSE_PIXELS_PER_FRAME = 20.0f;   // 200 px total, ~0.8 of the stick radius at 720 p
    constexpr int32 INPUT_WHEEL_SWEEP_FRAMES = 100;         // More than the ~82 steps from 1 m/s to 0.5c
    constexpr int32 INPUT_WHEEL_RESTORE_FRAMES = 30;        // 1 m/s * 10^3 = the 1000 m/s start cap
    constexpr int32 INPUT_WHEEL_SETTLE_FRAMES = 5;          // Frames for the last wheel event to be processed
    constexpr int32 INPUT_SEEK_TAP_INTERVAL_FRAMES = 6;     // Frames between R presses while looking for Mars

    // Verdict thresholds
    constexpr double INPUT_THRUST_MIN_CAP_FRACTION = 0.7;   // 3 s at tau 1.5 s reaches ~86% of the cap
    constexpr double INPUT_MIN_ALIGNMENT = 0.99;            // Relative velocity along forward
    constexpr double INPUT_BRAKE_MAX_REL_MPS = 2.0;         // 865 m/s * exp(-10/1.5) = ~1.1 m/s
    constexpr double INPUT_THRUST_GAIN_TOLERANCE_MPS = 15.0; // Newtonian gain vs 50 m/s^2 * t (gravity ~1.5 m/s^2)
    constexpr double INPUT_COAST_MAX_CHANGE = 0.08;         // Coasting keeps the speed (only gravity changes it)
    constexpr double INPUT_TURN_MIN_SIN = 0.2588190451;     // Forward turned at least 15 deg toward the mouse
    constexpr double INPUT_TURN_MAX_CROSS = 0.1;            // ...and less than ~6 deg along the other axis
    constexpr double INPUT_MAX_RESIDUAL_RATE_RAD_PER_S = 0.05; // Rotation stopped after the recenter
    constexpr double INPUT_CAP_RELATIVE_TOLERANCE = 1.0e-9;
    constexpr double INPUT_FRAME_VELOCITY_TOLERANCE_MPS = 1.0e-6;
    constexpr double INPUT_LOCK_MAX_REL_MPS = 1.0;          // Boosted ramp: tau 0.5 s over 6 s

    const TCHAR* const INPUT_MARS = TEXT("Mars");
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and starts the first phase
void FSOLShipSmokeInput::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mTargeting = world->GetSubsystem<USOLTargetingSubsystem>();
    mClock = world->GetSubsystem<USOLSimClockSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mEarthIndex = FindBody(SOL::BodyNames::EARTH);
    mMarsIndex = FindBody(INPUT_MARS);
    UE_LOG(LogSOL, Log, TEXT("SmokeInput: scripted input starting (controller %s)"),
        mController.IsValid() ? *mController->GetName() : TEXT("none"));
    mPhase = ESOLInputPhase::Setup;
    EnterPhase(mPhase, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLShipSmokeInput::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLInputPhase::Done)
    {
        return true;
    }
    if (!mController.IsValid() || !mShips.IsValid() || !mTargeting.IsValid() || !mClock.IsValid()
        || !mRegistry.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeInput: controller or subsystems missing; aborting"));
        mPhase = ESOLInputPhase::Done;
        return true;
    }

    // Keys tapped last frame are released now, so each tap spans exactly one processed frame
    for (const FKey& key : mPendingReleases)
    {
        ReleaseKey(key);
    }
    mPendingReleases.Reset();

    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    TickPhase(mPhase, pawn);
    if (mPhaseTimeS < GetPhaseDuration(mPhase) || mPhaseFrames < GetPhaseMinFrames(mPhase))
    {
        return false;
    }

    // Phase over: checks, then the next phase
    ExitPhase(mPhase, pawn);
    mPhase = static_cast<ESOLInputPhase>(static_cast<uint8>(mPhase) + 1);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    if (mPhase == ESOLInputPhase::Done)
    {
        return true;
    }
    EnterPhase(mPhase, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions (key presses, setup)
void FSOLShipSmokeInput::EnterPhase(const ESOLInputPhase phase, ASOLShipPawn& pawn)
{
    const FSOLShipState state = mShips->GetState();
    switch (phase)
    {
    case ESOLInputPhase::Setup:
        SetupShipAtRest(INDEX_NONE);
        break;
    case ESOLInputPhase::ThrustHold:
        mReference = state;
        PressKey(EKeys::W);
        break;
    case ESOLInputPhase::AssistOffThrust:
        mReference = state;
        mReferenceRelVelocityMps = GetRelativeVelocityMps();
        TapKey(EKeys::Tab);
        PressKey(EKeys::W);
        break;
    case ESOLInputPhase::YawRight:
    case ESOLInputPhase::PitchUp:
        mReference = state;
        break;
    case ESOLInputPhase::WheelOneUp:
        mReferenceValue = pawn.GetComposedControl().SpeedCapMps;
        InjectAxis(EKeys::MouseWheelAxis, 1.0f);
        break;
    case ESOLInputPhase::WheelOneDown:
        InjectAxis(EKeys::MouseWheelAxis, -1.0f);
        break;
    case ESOLInputPhase::AimEarth:
    case ESOLInputPhase::Screenshot:
        SetupShipAtRest(mEarthIndex);
        break;
    case ESOLInputPhase::WarpUp1:
        mReferenceIndex = mClock->GetClock().GetWarpIndex();
        TapKey(EKeys::RightBracket);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's per-frame actions (continuous mouse or wheel input)
void FSOLShipSmokeInput::TickPhase(const ESOLInputPhase phase, ASOLShipPawn& /*pawn*/)
{
    switch (phase)
    {
    case ESOLInputPhase::YawRight:
        if (mPhaseFrames <= INPUT_MOUSE_FRAMES)
        {
            InjectAxis(EKeys::MouseX, INPUT_MOUSE_PIXELS_PER_FRAME);
        }
        break;
    case ESOLInputPhase::PitchUp:
        if (mPhaseFrames <= INPUT_MOUSE_FRAMES)
        {
            InjectAxis(EKeys::MouseY, INPUT_MOUSE_PIXELS_PER_FRAME);
        }
        break;
    case ESOLInputPhase::WheelToMax:
    case ESOLInputPhase::WheelToMin:
        if (mPhaseFrames <= INPUT_WHEEL_SWEEP_FRAMES)
        {
            InjectAxis(EKeys::MouseWheelAxis, phase == ESOLInputPhase::WheelToMax ? 1.0f : -1.0f);
        }
        break;
    case ESOLInputPhase::WheelRestore:
        if (mPhaseFrames <= INPUT_WHEEL_RESTORE_FRAMES)
        {
            InjectAxis(EKeys::MouseWheelAxis, 1.0f);
        }
        break;
    case ESOLInputPhase::SeekMars:
        if (mPhaseFrames % INPUT_SEEK_TAP_INTERVAL_FRAMES == 0 && mTargeting->GetSelectedIndex() != mMarsIndex)
        {
            TapKey(EKeys::R);
        }
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks and releases or taps the keys for the next phase
void FSOLShipSmokeInput::ExitPhase(const ESOLInputPhase phase, ASOLShipPawn& pawn)
{
    const FSOLShipState state = mShips->GetState();
    const FVector3d forward = state.Orientation.GetForwardVector();
    const FVector3d relVelocity = GetRelativeVelocityMps();
    const double relSpeed = relVelocity.Size();
    const double cap = pawn.GetComposedControl().SpeedCapMps;
    const FSOLFlightParams params = mShips->GetFlightParams();
    const FVector2d stick = pawn.GetStickOffsetPixels();
    const TConstArrayView<FSOLTargetInfo> candidates = mTargeting->GetCandidates();
    const int32 selected = mTargeting->GetSelectedIndex();
    const int32 warp = mClock->GetClock().GetWarpIndex();

    // Each phase checks one expectation and queues the input of the next
    switch (phase)
    {
    case ESOLInputPhase::ThrustHold:
    {
        const double alignment = FVector3d::DotProduct(relVelocity.GetSafeNormal(), forward);
        Check(TEXT("a-thrust"), relSpeed >= INPUT_THRUST_MIN_CAP_FRACTION * cap && relSpeed <= cap + 1.0
            && alignment >= INPUT_MIN_ALIGNMENT, FString::Printf(TEXT("W held %.2f s: relative speed %.1f m/s "
            "(cap %.0f), alignment with forward %.4f"), mPhaseTimeS, relSpeed, cap, alignment));
        ReleaseKey(EKeys::W);
        break;
    }
    case ESOLInputPhase::Brake:
        Check(TEXT("b-brake"), relSpeed < INPUT_BRAKE_MAX_REL_MPS, FString::Printf(TEXT("W released %.2f s: "
            "relative speed %.3f m/s (limit %.1f)"), mPhaseTimeS, relSpeed, INPUT_BRAKE_MAX_REL_MPS));
        break;
    case ESOLInputPhase::AssistOffThrust:
    {
        const bool bAssist = mShips->GetControl().bFlightAssist;
        const double gain = FVector3d::DotProduct(relVelocity - mReferenceRelVelocityMps,
            mReference.Orientation.GetForwardVector());
        const double expected = params.NewtonianMainAccelMps2 * mPhaseTimeS;
        Check(TEXT("c-assist-off"), !bAssist && FMath::Abs(gain - expected) <= INPUT_THRUST_GAIN_TOLERANCE_MPS,
            FString::Printf(TEXT("Tab: assist %s; W held %.2f s: forward gain %.1f m/s (expected ~%.0f)"),
                bAssist ? TEXT("ON") : TEXT("OFF"), mPhaseTimeS, gain, expected));
        mReferenceValue = relSpeed;
        ReleaseKey(EKeys::W);
        break;
    }
    case ESOLInputPhase::Coast:
        Check(TEXT("c-coast"), FMath::Abs(relSpeed - mReferenceValue) <= INPUT_COAST_MAX_CHANGE * mReferenceValue,
            FString::Printf(TEXT("coasting %.2f s: relative speed %.2f -> %.2f m/s"), mPhaseTimeS, mReferenceValue,
                relSpeed));
        TapKey(EKeys::Tab);
        break;
    case ESOLInputPhase::AssistOn:
        Check(TEXT("c-assist-on"), mShips->GetControl().bFlightAssist, TEXT("Tab again: assist ON"));
        break;
    case ESOLInputPhase::YawRight:
    case ESOLInputPhase::PitchUp:
    {
        // Compare the forward vector against the axes at the start of the turn
        const bool bYaw = phase == ESOLInputPhase::YawRight;
        const FVector3d along = bYaw ? mReference.Orientation.GetRightVector() : mReference.Orientation.GetUpVector();
        const FVector3d across = bYaw ? mReference.Orientation.GetUpVector() : mReference.Orientation.GetRightVector();
        const double toward = FVector3d::DotProduct(forward, along);
        const double sideways = FVector3d::DotProduct(forward, across);
        const double stickAlong = bYaw ? stick.X : stick.Y;
        Check(bYaw ? TEXT("d-yaw-right") : TEXT("d-pitch-up"), toward >= INPUT_TURN_MIN_SIN
            && FMath::Abs(sideways) <= INPUT_TURN_MAX_CROSS && stickAlong > 0.0, FString::Printf(TEXT("mouse %s "
            "%.0f px: stick (%.1f, %.1f) of radius %.1f px; forward toward old %s %.3f (%.1f deg), across %.3f"),
            bYaw ? TEXT("right") : TEXT("up"), INPUT_MOUSE_FRAMES * INPUT_MOUSE_PIXELS_PER_FRAME, stick.X, stick.Y,
            pawn.GetStickRadiusPixels(), bYaw ? TEXT("right") : TEXT("up"), toward,
            FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(toward, -1.0, 1.0))), sideways));
        TapKey(EKeys::MiddleMouseButton);
        break;
    }
    case ESOLInputPhase::YawRecenter:
    case ESOLInputPhase::PitchRecenter:
    {
        const double rate = state.AngularVelocityRadPerS.Size();
        Check(TEXT("d-recenter"), stick.IsNearlyZero() && rate <= INPUT_MAX_RESIDUAL_RATE_RAD_PER_S,
            FString::Printf(TEXT("middle mouse: stick (%.1f, %.1f), residual turn rate %.4f rad/s"), stick.X, stick.Y,
                rate));
        break;
    }
    case ESOLInputPhase::WheelOneUp:
    {
        const double ratio = cap / mReferenceValue;
        const double expected = FMath::Pow(10.0, 1.0 / SOLFlight::SpeedCapStepsPerDecade);
        Check(TEXT("e-wheel-up"), FMath::Abs(ratio - expected) <= INPUT_CAP_RELATIVE_TOLERANCE * expected,
            FString::Printf(TEXT("one notch up: cap %.3f -> %.3f m/s, ratio %.9f (expected %.9f)"), mReferenceValue,
                cap, ratio, expected));
        break;
    }
    case ESOLInputPhase::WheelOneDown:
        Check(TEXT("e-wheel-down"), FMath::Abs(cap - mReferenceValue) <= INPUT_CAP_RELATIVE_TOLERANCE * mReferenceValue,
            FString::Printf(TEXT("one notch down: cap %.6f m/s (start %.6f)"), cap, mReferenceValue));
        break;
    case ESOLInputPhase::WheelToMax:
        Check(TEXT("e-wheel-max"), cap == params.MaxSpeedMps, FString::Printf(TEXT("%d notches up: cap %.1f m/s "
            "(clamp %.1f = 0.5c)"), INPUT_WHEEL_SWEEP_FRAMES, cap, params.MaxSpeedMps));
        break;
    case ESOLInputPhase::WheelToMin:
        Check(TEXT("e-wheel-min"), cap == params.MinSpeedCapMps, FString::Printf(TEXT("%d notches down: cap %.6f m/s "
            "(clamp %.1f)"), INPUT_WHEEL_SWEEP_FRAMES, cap, params.MinSpeedCapMps));
        break;
    case ESOLInputPhase::WheelRestore:
        Check(TEXT("e-wheel-restore"), FMath::Abs(cap - SOL::SHIP_START_SPEED_CAP_MPS)
            <= 1.0e-6 * SOL::SHIP_START_SPEED_CAP_MPS, FString::Printf(TEXT("%d notches up from 1 m/s: cap %.6f m/s"),
                INPUT_WHEEL_RESTORE_FRAMES, cap));
        break;
    case ESOLInputPhase::AimEarth:
        TapKey(EKeys::T);
        break;
    case ESOLInputPhase::SelectEarth:
        Check(TEXT("f-select"), selected == mEarthIndex, FString::Printf(TEXT("T facing Earth: selected %s"),
            *GetCandidateName(selected)));
        TapKey(EKeys::R);
        break;
    case ESOLInputPhase::CycleNext1:
    case ESOLInputPhase::CycleNext2:
    {
        // R steps to the next farther candidate, as the pure Cycle function orders them
        const int32 previous = phase == ESOLInputPhase::CycleNext1 ? mEarthIndex : mFirstCycled;
        const int32 expected = SOLTargeting::Cycle(candidates, state.PositionM, previous, 1);
        const double previousDistM = candidates.IsValidIndex(previous)
            ? FVector3d::Dist(candidates[previous].PositionM, state.PositionM) : 0.0;
        const double selectedDistM = candidates.IsValidIndex(selected)
            ? FVector3d::Dist(candidates[selected].PositionM, state.PositionM) : 0.0;
        Check(phase == ESOLInputPhase::CycleNext1 ? TEXT("f-next-1") : TEXT("f-next-2"), selected == expected
            && selected != previous && selectedDistM >= previousDistM, FString::Printf(TEXT("R: %s (%.0f km) -> %s "
            "(%.0f km), expected %s"), *GetCandidateName(previous), previousDistM / SOL::METERS_PER_KM,
            *GetCandidateName(selected), selectedDistM / SOL::METERS_PER_KM, *GetCandidateName(expected)));
        if (phase == ESOLInputPhase::CycleNext1)
        {
            mFirstCycled = selected;
            TapKey(EKeys::R);
        }
        else
        {
            TapKey(EKeys::F);
        }
        break;
    }
    case ESOLInputPhase::CyclePrevious1:
        Check(TEXT("f-previous-1"), selected == mFirstCycled, FString::Printf(TEXT("F: back to %s (expected %s)"),
            *GetCandidateName(selected), *GetCandidateName(mFirstCycled)));
        TapKey(EKeys::F);
        break;
    case ESOLInputPhase::CyclePrevious2:
        Check(TEXT("f-previous-2"), selected == mEarthIndex, FString::Printf(TEXT("F: back to %s (expected Earth)"),
            *GetCandidateName(selected)));
        TapKey(EKeys::X);
        break;
    case ESOLInputPhase::ClearTarget:
        Check(TEXT("f-clear"), selected == INDEX_NONE, FString::Printf(TEXT("X: selected %s"),
            *GetCandidateName(selected)));
        break;
    case ESOLInputPhase::SeekMars:
        Check(TEXT("g-seek-mars"), selected == mMarsIndex, FString::Printf(TEXT("R until Mars: selected %s"),
            *GetCandidateName(selected)));
        TapKey(EKeys::M);
        PressKey(EKeys::LeftShift);
        break;
    case ESOLInputPhase::LockMars:
    {
        const FVector3d marsVelocity = candidates.IsValidIndex(mMarsIndex) ? candidates[mMarsIndex].VelocityMps
            : FVector3d::ZeroVector;
        const double frameError = (mTargeting->GetReferenceVelocityMps() - marsVelocity).Size();
        const double earthRelSpeed = candidates.IsValidIndex(mEarthIndex)
            ? (state.VelocityMps - candidates[mEarthIndex].VelocityMps).Size() : 0.0;
        Check(TEXT("g-lock-mars"), mTargeting->GetLockedIndex() == mMarsIndex
            && mTargeting->GetFrameName() == FName(INPUT_MARS) && frameError <= INPUT_FRAME_VELOCITY_TOLERANCE_MPS
            && relSpeed <= INPUT_LOCK_MAX_REL_MPS, FString::Printf(TEXT("M (+Shift boost %.1f s): frame %s, frame "
            "velocity error %.3g m/s, speed rel frame %.3f m/s (rel Earth %.1f m/s)"), mPhaseTimeS,
            *mTargeting->GetFrameName().ToString(), frameError, relSpeed, earthRelSpeed));
        ReleaseKey(EKeys::LeftShift);
        TapKey(EKeys::M);
        break;
    }
    case ESOLInputPhase::UnlockMars:
    {
        const FVector3d earthVelocity = candidates.IsValidIndex(mEarthIndex) ? candidates[mEarthIndex].VelocityMps
            : FVector3d::ZeroVector;
        const double frameError = (mTargeting->GetReferenceVelocityMps() - earthVelocity).Size();
        Check(TEXT("g-unlock"), !mTargeting->IsFrameLocked() && frameError <= INPUT_FRAME_VELOCITY_TOLERANCE_MPS,
            FString::Printf(TEXT("M again: locked %s, frame %s, frame velocity error vs Earth %.3g m/s"),
                mTargeting->IsFrameLocked() ? TEXT("yes") : TEXT("no"), *mTargeting->GetFrameName().ToString(),
                frameError));
        TapKey(EKeys::X);
        break;
    }
    case ESOLInputPhase::ClearForNearest:
        TapKey(EKeys::M);
        break;
    case ESOLInputPhase::LockNearest:
        Check(TEXT("g-lock-nearest"), selected == INDEX_NONE && mTargeting->GetLockedIndex() == mEarthIndex,
            FString::Printf(TEXT("X then M: selected %s, frame locked to %s"), *GetCandidateName(selected),
                *GetCandidateName(mTargeting->GetLockedIndex())));
        TapKey(EKeys::M);
        break;
    case ESOLInputPhase::ReleaseNearest:
        Check(TEXT("g-release"), !mTargeting->IsFrameLocked(), FString::Printf(TEXT("M: frame follows %s"),
            *mTargeting->GetFrameName().ToString()));
        break;
    case ESOLInputPhase::WarpUp1:
    case ESOLInputPhase::WarpUp2:
    {
        const int32 expected = mReferenceIndex + (phase == ESOLInputPhase::WarpUp1 ? 1 : 2);
        Check(phase == ESOLInputPhase::WarpUp1 ? TEXT("h-warp-up-1") : TEXT("h-warp-up-2"), warp == expected,
            FString::Printf(TEXT("]: warp index %d (x%.0f), expected %d"), warp, mClock->GetClock().GetWarpFactor(),
                expected));
        TapKey(phase == ESOLInputPhase::WarpUp1 ? EKeys::RightBracket : EKeys::LeftBracket);
        break;
    }
    case ESOLInputPhase::WarpDown:
        Check(TEXT("h-warp-down"), warp == mReferenceIndex + 1, FString::Printf(TEXT("[: warp index %d, expected %d"),
            warp, mReferenceIndex + 1));
        TapKey(EKeys::BackSpace);
        break;
    case ESOLInputPhase::WarpReset:
        Check(TEXT("h-warp-reset"), warp == 0, FString::Printf(TEXT("Backspace: warp index %d (x%.0f)"), warp,
            mClock->GetClock().GetWarpFactor()));
        break;
    case ESOLInputPhase::Screenshot:
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeInput: complete after %.1f s, %d/%d checks passed%s"), mTotalTimeS,
            mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("") : TEXT(" (FAILURES)"));
        if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
        {
            gameMode->TakeSmokeScreenshot();
        }
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase in seconds
double FSOLShipSmokeInput::GetPhaseDuration(const ESOLInputPhase phase)
{
    switch (phase)
    {
    case ESOLInputPhase::Setup:
        return INPUT_SETUP_S;
    case ESOLInputPhase::ThrustHold:
        return INPUT_THRUST_S;
    case ESOLInputPhase::Brake:
        return INPUT_BRAKE_S;
    case ESOLInputPhase::AssistOffThrust:
        return INPUT_NEWTON_THRUST_S;
    case ESOLInputPhase::Coast:
        return INPUT_COAST_S;
    case ESOLInputPhase::YawRight:
    case ESOLInputPhase::PitchUp:
        return INPUT_TURN_S;
    case ESOLInputPhase::YawRecenter:
    case ESOLInputPhase::PitchRecenter:
        return INPUT_RECENTER_S;
    case ESOLInputPhase::WheelToMax:
    case ESOLInputPhase::WheelToMin:
    case ESOLInputPhase::WheelRestore:
        return 0.0;
    case ESOLInputPhase::AimEarth:
        return INPUT_AIM_S;
    case ESOLInputPhase::SeekMars:
        return INPUT_SEEK_S;
    case ESOLInputPhase::LockMars:
        return INPUT_LOCK_RAMP_S;
    case ESOLInputPhase::Screenshot:
        return INPUT_SCREENSHOT_SETTLE_S;
    default:
        return INPUT_TAP_PHASE_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum number of frames a phase lasts
int32 FSOLShipSmokeInput::GetPhaseMinFrames(const ESOLInputPhase phase)
{
    switch (phase)
    {
    case ESOLInputPhase::YawRight:
    case ESOLInputPhase::PitchUp:
        return INPUT_MOUSE_FRAMES + INPUT_WHEEL_SETTLE_FRAMES;
    case ESOLInputPhase::WheelToMax:
    case ESOLInputPhase::WheelToMin:
        return INPUT_WHEEL_SWEEP_FRAMES + INPUT_WHEEL_SETTLE_FRAMES;
    case ESOLInputPhase::WheelRestore:
        return INPUT_WHEEL_RESTORE_FRAMES + INPUT_WHEEL_SETTLE_FRAMES;
    default:
        return INPUT_WHEEL_SETTLE_FRAMES;
    }
}

//////////////////////////////////////////////////////////////////////////
// Sends a key press to the player controller
void FSOLShipSmokeInput::PressKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
}

//////////////////////////////////////////////////////////////////////////
// Sends a key release to the player controller
void FSOLShipSmokeInput::ReleaseKey(const FKey& key) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
}

//////////////////////////////////////////////////////////////////////////
// Presses a key now and releases it on the next frame
void FSOLShipSmokeInput::TapKey(const FKey& key)
{
    PressKey(key);
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Sends one analog axis sample (mouse delta in pixels, wheel notches) to the player controller
void FSOLShipSmokeInput::InjectAxis(const FKey& key, const float delta) const
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Axis, delta, 1));
}

//////////////////////////////////////////////////////////////////////////
// Teleports the ship to rest relative to the reference frame, optionally facing a body, with no rotation
void FSOLShipSmokeInput::SetupShipAtRest(const int32 faceBodyIndex) const
{
    FSOLShipState state = mShips->GetState();
    state.VelocityMps = mShips->GetReferenceVelocityMps();
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    const FSOLBodyFrameCache& cache = mRegistry->GetUnrealFrameCache();
    if (faceBodyIndex >= 0 && faceBodyIndex < cache.Num())
    {
        const FVector3d direction = (cache.PositionsM[faceBodyIndex] - state.PositionM).GetSafeNormal();
        state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(direction).ToQuat();
    }
    mShips->SetState(state);
    UE_LOG(LogSOL, Log, TEXT("SmokeInput: setup - ship at rest relative to the frame%s%s"),
        faceBodyIndex == INDEX_NONE ? TEXT("") : TEXT(", facing "),
        faceBodyIndex == INDEX_NONE ? TEXT("") : *mRegistry->GetRegistry().GetName(faceBodyIndex).ToString());
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship's velocity relative to the active reference frame
FVector3d FSOLShipSmokeInput::GetRelativeVelocityMps() const
{
    return mShips->GetState().VelocityMps - mShips->GetReferenceVelocityMps();
}

//////////////////////////////////////////////////////////////////////////
// Returns the registry index of a body by name
int32 FSOLShipSmokeInput::FindBody(const TCHAR* name) const
{
    return mRegistry.IsValid() ? mRegistry->GetRegistry().FindByName(FName(name)) : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Returns the display name of a candidate index, or "none"
FString FSOLShipSmokeInput::GetCandidateName(const int32 index) const
{
    const TConstArrayView<FSOLTargetInfo> candidates = mTargeting->GetCandidates();
    return candidates.IsValidIndex(index) ? candidates[index].Name.ToString() : FString(TEXT("none"));
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLShipSmokeInput::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeInput CHECK %-16s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
