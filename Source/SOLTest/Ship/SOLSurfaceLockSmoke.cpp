/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Ship/SOLSurfaceLockSmoke.h"

#include "Game/SOLGameMode.h"
#include "Level/SOLSurfaceLock.h"
#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapPicking.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLBodyFrameCache.h"
#include "Universe/SOLBodyRegistrySubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputKeyEventArgs.h"

namespace
{
    using ESOLHudLockLine = ASOLFlightHud::ESOLSurfaceLockHudLine;

    // Phase durations (real seconds) and frame counts
    constexpr double LEVEL_SMOKE_SETUP_S = 1.0;
    constexpr double LEVEL_SMOKE_STEP_S = 0.5;              // Long enough for a tap or a teleport to take effect
    constexpr double LEVEL_SMOKE_ALIGN_S = 6.0;             // Six alignment time constants
    constexpr double LEVEL_SMOKE_HOLD_S = 2.0;              // The released lock must stay released this long
    constexpr double LEVEL_SMOKE_NO_ALIGN_S = 1.5;          // A re-tilted, released ship must stay tilted this long
    constexpr double LEVEL_SMOKE_ARRIVE_MARGIN_S = 0.5;     // Past the warp's end before the arrival checks
    constexpr double LEVEL_SMOKE_FINISH_S = 1.0;
    constexpr int32 LEVEL_SMOKE_SETTLE_FRAMES = 5;          // Frames for the last injected event to be processed
    constexpr double LEVEL_SMOKE_SHOT_AT_S = 0.25;          // Screenshot this far into a phase (or before its end)

    // Scripted altitudes above Earth's surface (Earth's manual range is its radius, 6,371 km)
    constexpr double LEVEL_SMOKE_LOW_M = 5.0e3;             // Inside the 10 km auto range
    constexpr double LEVEL_SMOKE_WARN_M = 11.0e3;           // Past the auto warn threshold, below the release
    constexpr double LEVEL_SMOKE_CLEAR_M = 13.0e3;          // Past the 12.5 km auto release
    constexpr double LEVEL_SMOKE_ORBIT_M = 5.0e5;           // Beyond auto range, inside the manual range
    constexpr double LEVEL_SMOKE_MANUAL_WARN_FRACTION = 1.1;    // Of the manual range: warns, not yet released
    constexpr double LEVEL_SMOKE_MANUAL_RELEASE_FRACTION = 1.3; // Of the manual range: past 1.25x, released
    constexpr double LEVEL_SMOKE_JUMP_ALTITUDE_M = 5.0e5;   // Jump arrival above Earth: beyond auto range, no re-engage

    // The up error after six time constants from 90 degrees is ~0.2 degrees; allow the flight step's own motion
    constexpr double LEVEL_SMOKE_MIN_TILT_DEG = 80.0;
    constexpr double LEVEL_SMOKE_MAX_ALIGNED_DEG = 1.0;
    constexpr double LEVEL_SMOKE_MIN_UNALIGNED_DEG = 85.0;  // A released lock leaves a 90-degree tilt alone
    constexpr double LEVEL_SMOKE_TAU_TOLERANCE_DEG = 5.0;   // One time constant in: ~90 * e^-1 = 33.1 degrees

    //////////////////////////////////////////////////////////////////////////
    // Returns the HUD's surface-lock line as text for the check details
    const TCHAR* LevelSmokeHudLineName(const ESOLHudLockLine line)
    {
        switch (line)
        {
        case ESOLHudLockLine::Hint:
            return TEXT("HINT");
        case ESOLHudLockLine::Engaged:
            return TEXT("ENGAGED");
        case ESOLHudLockLine::Warning:
            return TEXT("WARNING");
        default:
            return TEXT("NONE");
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller and subsystems and starts the first phase
void FSOLSurfaceLockSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mTargeting = world->GetSubsystem<USOLTargetingSubsystem>();
    mJump = world->GetSubsystem<USOLJumpSubsystem>();
    mEarthIndex = mRegistry.IsValid() ? mRegistry->GetRegistry().FindByName(FName(SOL::BodyNames::EARTH)) : INDEX_NONE;
    UE_LOG(LogSOL, Log, TEXT("SmokeLevel: scripted surface-lock input starting (controller %s, Earth index %d)"),
        mController.IsValid() ? *mController->GetName() : TEXT("none"), mEarthIndex);
    mPhase = ESOLLevelPhase::Setup;
    EnterPhase(mPhase);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script (from the pawn's tick); returns true once it has finished
bool FSOLSurfaceLockSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLLevelPhase::Done)
    {
        return true;
    }
    if (!mController.IsValid() || !mShips.IsValid() || !mRegistry.IsValid() || !mTargeting.IsValid()
        || !mJump.IsValid() || mEarthIndex == INDEX_NONE)
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeLevel: controller, subsystems or Earth missing; aborting"));
        mPhase = ESOLLevelPhase::Done;
        return true;
    }

    // Keys tapped last frame are released now, so each tap spans exactly one processed frame
    for (const FKey& key : mPendingReleases)
    {
        mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Released, 0.0f));
    }
    mPendingReleases.Reset();

    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;
    TickPhase(mPhase, pawn);
    if (!IsPhaseDone(mPhase))
    {
        return false;
    }

    // Phase over: checks, then the next phase
    ExitPhase(mPhase, pawn);
    mPhase = static_cast<ESOLLevelPhase>(static_cast<uint8>(mPhase) + 1);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    mIsShotTaken = false;
    if (mPhase == ESOLLevelPhase::Done)
    {
        return true;
    }
    EnterPhase(mPhase);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions (key taps, teleports)
void FSOLSurfaceLockSmoke::EnterPhase(const ESOLLevelPhase phase)
{
    const double manualRangeM = SOLSurfaceLock::ComputeManualRangeM(
        mRegistry.IsValid() && mEarthIndex != INDEX_NONE ? mRegistry->GetRegistry().GetRadiusM(mEarthIndex) : 0.0,
        mShips.IsValid() ? mShips->GetSurfaceLockParams() : FSOLSurfaceLockParams());
    switch (phase)
    {
    case ESOLLevelPhase::PressOutOfRange:
    case ESOLLevelPhase::PressRelease:
    case ESOLLevelPhase::PressManual:
        TapKey(EKeys::L);
        break;
    case ESOLLevelPhase::PlaceLow:
        // An M lock on something else first, which the auto-engage must replace with Earth
        mPreLockIndex = LockFrameToOtherCandidate();
        PlaceAboveEarth(LEVEL_SMOKE_LOW_M, ESOLLevelPlaceUp::Tilt);
        mStartUpErrorDeg = ComputeUpErrorDeg();
        mAlignStartS = -1.0;
        mTauElapsedS = -1.0;
        break;
    case ESOLLevelPhase::HoldSuppressed:
        // Re-tilted with the lock released: the alignment must not resume
        mEngagedFramesWhileHeld = 0;
        PlaceAboveEarth(LEVEL_SMOKE_LOW_M, ESOLLevelPlaceUp::Tilt);
        mRetiltUpErrorDeg = ComputeUpErrorDeg();
        break;
    case ESOLLevelPhase::ClimbClear:
        PlaceAboveEarth(LEVEL_SMOKE_CLEAR_M, ESOLLevelPlaceUp::Level);
        break;
    case ESOLLevelPhase::ClimbRelease:
        PlaceAboveEarth(LEVEL_SMOKE_CLEAR_M, ESOLLevelPlaceUp::Keep);
        break;
    case ESOLLevelPhase::HoldAfterAutoRelease:
        PlaceAboveEarth(LEVEL_SMOKE_CLEAR_M, ESOLLevelPlaceUp::Tilt);
        mRetiltUpErrorDeg = ComputeUpErrorDeg();
        break;
    case ESOLLevelPhase::Descend:
    case ESOLLevelPhase::JumpPlace:
        PlaceAboveEarth(LEVEL_SMOKE_LOW_M, ESOLLevelPlaceUp::Level);
        break;
    case ESOLLevelPhase::ClimbWarn:
        PlaceAboveEarth(LEVEL_SMOKE_WARN_M, ESOLLevelPlaceUp::Keep);
        break;
    case ESOLLevelPhase::PlaceOrbit:
        PlaceAboveEarth(LEVEL_SMOKE_ORBIT_M, ESOLLevelPlaceUp::Level);
        break;
    case ESOLLevelPhase::ManualWarn:
        PlaceAboveEarth(LEVEL_SMOKE_MANUAL_WARN_FRACTION * manualRangeM, ESOLLevelPlaceUp::Keep);
        break;
    case ESOLLevelPhase::ManualRelease:
        PlaceAboveEarth(LEVEL_SMOKE_MANUAL_RELEASE_FRACTION * manualRangeM, ESOLLevelPlaceUp::Keep);
        break;
    case ESOLLevelPhase::JumpWarpPress:
    {
        // A jump to 500 km above Earth (planar offset from its center in the ecliptic plane), then L during the warp
        FSOLMapPickState pick;
        pick.bHasReference = true;
        pick.bReferenceIsBody = true;
        pick.ReferenceBodyIndex = mEarthIndex;
        pick.bPlanarLocked = true;
        pick.PlanarOffsetXM = mRegistry->GetRegistry().GetRadiusM(mEarthIndex) + LEVEL_SMOKE_JUMP_ALTITUDE_M;
        mJumpCountBefore = mJump->GetCompletedJumpCount();
        mIsJumpStarted = mJump->StartJump(pick);
        TapKey(EKeys::L);
        break;
    }
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's per-frame work: the suppression hold's watch, and the screenshots before a phase ends
void FSOLSurfaceLockSmoke::TickPhase(const ESOLLevelPhase phase, const ASOLShipPawn& pawn)
{
    // Every frame of the hold counts: a single re-engaged frame is the bug the latch exists for
    if (phase == ESOLLevelPhase::HoldSuppressed && mShips->GetSurfaceLockState().bEngaged)
    {
        ++mEngagedFramesWhileHeld;
    }
    if (phase == ESOLLevelPhase::PlaceLow || phase == ESOLLevelPhase::Align)
    {
        TrackAlignment();
    }

    // The screenshot is captured a frame or more after the request, so it is taken well before the next teleport
    const TCHAR* shotName = nullptr;
    double shotAtS = LEVEL_SMOKE_SHOT_AT_S;
    switch (phase)
    {
    case ESOLLevelPhase::Align:
        shotName = TEXT("engaged");
        shotAtS = LEVEL_SMOKE_ALIGN_S - LEVEL_SMOKE_SHOT_AT_S;
        break;
    case ESOLLevelPhase::HoldSuppressed:
        shotName = TEXT("released-hint");
        break;
    case ESOLLevelPhase::ClimbWarn:
        shotName = TEXT("warning");
        break;
    case ESOLLevelPhase::PressManual:
        shotName = TEXT("manual");
        break;
    default:
        break;
    }
    if (shotName != nullptr && !mIsShotTaken && mPhaseTimeS >= shotAtS)
    {
        mIsShotTaken = true;
        Screenshot(pawn, shotName);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns true once a phase may end (its minimum time and frames)
bool FSOLSurfaceLockSmoke::IsPhaseDone(const ESOLLevelPhase phase) const
{
    double minimumS = LEVEL_SMOKE_STEP_S;
    switch (phase)
    {
    case ESOLLevelPhase::Setup:
        minimumS = LEVEL_SMOKE_SETUP_S;
        break;
    case ESOLLevelPhase::Align:
        minimumS = LEVEL_SMOKE_ALIGN_S;
        break;
    case ESOLLevelPhase::HoldSuppressed:
        minimumS = LEVEL_SMOKE_HOLD_S;
        break;
    case ESOLLevelPhase::HoldAfterAutoRelease:
        minimumS = LEVEL_SMOKE_NO_ALIGN_S;
        break;
    case ESOLLevelPhase::JumpArrive:
        minimumS = SOL::WARP_DURATION_S + LEVEL_SMOKE_ARRIVE_MARGIN_S;
        break;
    case ESOLLevelPhase::Finish:
        minimumS = LEVEL_SMOKE_FINISH_S;
        break;
    default:
        break;
    }
    return mPhaseTimeS >= minimumS && mPhaseFrames >= LEVEL_SMOKE_SETTLE_FRAMES;
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks
void FSOLSurfaceLockSmoke::ExitPhase(const ESOLLevelPhase phase, ASOLShipPawn& pawn)
{
    const FSOLSurfaceLockState& lock = mShips->GetSurfaceLockState();
    const ASOLFlightHud* hud = mController->GetHUD<ASOLFlightHud>();
    const ESOLHudLockLine hudLine = hud != nullptr ? hud->GetLastSurfaceLockLine() : ESOLHudLockLine::None;
    const bool bOnEarth = lock.BodyIndex == mEarthIndex;
    switch (phase)
    {
    case ESOLLevelPhase::PressOutOfRange:
        Check(TEXT("a-out-of-range"), !lock.bEngaged && hudLine == ESOLHudLockLine::None, FString::Printf(TEXT("L at "
            "the %.0f km spawn (beyond Earth's manual range): %s; HUD line %s"), SOL::DEFAULT_SPAWN_ALTITUDE_M
            / SOL::METERS_PER_KM, *DescribeLock(), LevelSmokeHudLineName(hudLine)));
        break;
    case ESOLLevelPhase::PlaceLow:
        Check(TEXT("b-auto-engage"), lock.bEngaged && !lock.bManual && bOnEarth && mStartUpErrorDeg
            >= LEVEL_SMOKE_MIN_TILT_DEG, FString::Printf(TEXT("placed at %.0f km, tilted %.1f deg, no key pressed: %s"),
            LEVEL_SMOKE_LOW_M / SOL::METERS_PER_KM, mStartUpErrorDeg, *DescribeLock()));
        Check(TEXT("c-frame-matched"), mTargeting->IsFrameLocked() && mTargeting->GetLockedIndex() == mEarthIndex,
            FString::Printf(TEXT("reference frame locked %s, to %s (candidate %d, Earth %d)"),
            mTargeting->IsFrameLocked() ? TEXT("yes") : TEXT("NO"), *mTargeting->GetFrameName().ToString(),
            mTargeting->GetLockedIndex(), mEarthIndex));
        Check(TEXT("c2-frame-replaced"), mPreLockIndex != INDEX_NONE && mPreLockIndex != mEarthIndex
            && mTargeting->GetLockedIndex() == mEarthIndex, FString::Printf(TEXT("M lock before the auto-engage on "
            "candidate %d (Earth %d), now on candidate %d"), mPreLockIndex, mEarthIndex, mTargeting->GetLockedIndex()));
        break;
    case ESOLLevelPhase::Align:
    {
        // One time constant in, the up error is the start error times e^-1 (the exact decay over the measured time)
        const double tauS = SOL::SURFACE_LOCK_ALIGN_TIME_CONSTANT_S;
        const double expectedTauDeg = mTauElapsedS >= 0.0 ? mAlignStartErrorDeg * FMath::Exp(-mTauElapsedS / tauS)
            : 0.0;
        Check(TEXT("d0-time-constant"), mTauElapsedS >= 0.0 && FMath::Abs(mTauErrorDeg - expectedTauDeg)
            <= LEVEL_SMOKE_TAU_TOLERANCE_DEG, FString::Printf(TEXT("%.3f s after the engage (tau %.2f s): up %.2f deg "
            "off, expected %.2f (from %.2f; 90 * e^-1 = %.2f), tolerance %.1f"), mTauElapsedS, tauS, mTauErrorDeg,
            expectedTauDeg, mAlignStartErrorDeg, 90.0 * FMath::Exp(-1.0), LEVEL_SMOKE_TAU_TOLERANCE_DEG));

        const double errorDeg = ComputeUpErrorDeg();
        Check(TEXT("d-aligned"), lock.bEngaged && errorDeg <= LEVEL_SMOKE_MAX_ALIGNED_DEG
            && hudLine == ESOLHudLockLine::Engaged, FString::Printf(TEXT("after %.1f s: up %.3f deg off the local "
            "vertical (from %.1f), %s, HUD line %s"), mPhaseTimeS, errorDeg, mStartUpErrorDeg, *DescribeLock(),
            LevelSmokeHudLineName(hudLine)));
        break;
    }
    case ESOLLevelPhase::PressRelease:
        Check(TEXT("e-manual-release"), !lock.bEngaged && lock.bAutoSuppressed && lock.SuppressedBodyIndex
            == mEarthIndex, FString::Printf(TEXT("L while engaged at %.0f km: %s"), LEVEL_SMOKE_LOW_M
            / SOL::METERS_PER_KM, *DescribeLock()));
        break;
    case ESOLLevelPhase::HoldSuppressed:
    {
        double altitudeM = 0.0;
        mShips->FindNearestBody(altitudeM);
        Check(TEXT("f-stays-released"), mEngagedFramesWhileHeld == 0 && !lock.bEngaged && lock.bAutoSuppressed
            && hudLine == ESOLHudLockLine::Hint, FString::Printf(TEXT("%.1f s (%d frames) inside the auto range after "
            "the release (now %.3f km up): engaged on %d frames; %s, HUD line %s"), mPhaseTimeS, mPhaseFrames,
            altitudeM / SOL::METERS_PER_KM, mEngagedFramesWhileHeld, *DescribeLock(), LevelSmokeHudLineName(hudLine)));
        const double errorDeg = ComputeUpErrorDeg();
        Check(TEXT("f2-no-align-manual"), mRetiltUpErrorDeg >= LEVEL_SMOKE_MIN_TILT_DEG
            && errorDeg >= LEVEL_SMOKE_MIN_UNALIGNED_DEG, FString::Printf(TEXT("re-tilted %.1f deg after the L "
            "release: after %.1f s up still %.2f deg off (minimum %.0f)"), mRetiltUpErrorDeg, mPhaseTimeS, errorDeg,
            LEVEL_SMOKE_MIN_UNALIGNED_DEG));
        break;
    }
    case ESOLLevelPhase::ClimbClear:
        Check(TEXT("g-latch-cleared"), !lock.bEngaged && !lock.bAutoSuppressed, FString::Printf(TEXT("climbed to "
            "%.0f km: %s"), LEVEL_SMOKE_CLEAR_M / SOL::METERS_PER_KM, *DescribeLock()));
        break;
    case ESOLLevelPhase::Descend:
        Check(TEXT("h-auto-reengage"), lock.bEngaged && !lock.bManual && bOnEarth, FString::Printf(TEXT("back down to "
            "%.0f km: %s"), LEVEL_SMOKE_LOW_M / SOL::METERS_PER_KM, *DescribeLock()));
        break;
    case ESOLLevelPhase::ClimbWarn:
        Check(TEXT("i-auto-warning"), lock.bEngaged && !lock.bManual && lock.bWarning
            && hudLine == ESOLHudLockLine::Warning, FString::Printf(TEXT("climbed to %.0f km: %s, HUD line %s"),
            LEVEL_SMOKE_WARN_M / SOL::METERS_PER_KM, *DescribeLock(), LevelSmokeHudLineName(hudLine)));
        break;
    case ESOLLevelPhase::ClimbRelease:
        Check(TEXT("j-auto-release"), !lock.bEngaged && !lock.bAutoSuppressed && mTargeting->GetLockedIndex()
            == mEarthIndex, FString::Printf(TEXT("climbed to %.0f km: %s; reference frame kept on %s (candidate %d)"),
            LEVEL_SMOKE_CLEAR_M / SOL::METERS_PER_KM, *DescribeLock(), *mTargeting->GetFrameName().ToString(),
            mTargeting->GetLockedIndex()));
        break;
    case ESOLLevelPhase::HoldAfterAutoRelease:
    {
        const double errorDeg = ComputeUpErrorDeg();
        Check(TEXT("j2-no-align-auto"), !lock.bEngaged && mRetiltUpErrorDeg >= LEVEL_SMOKE_MIN_TILT_DEG
            && errorDeg >= LEVEL_SMOKE_MIN_UNALIGNED_DEG, FString::Printf(TEXT("re-tilted %.1f deg after the auto "
            "release at %.0f km: after %.1f s up still %.2f deg off (minimum %.0f); %s"), mRetiltUpErrorDeg,
            LEVEL_SMOKE_CLEAR_M / SOL::METERS_PER_KM, mPhaseTimeS, errorDeg, LEVEL_SMOKE_MIN_UNALIGNED_DEG,
            *DescribeLock()));
        break;
    }
    case ESOLLevelPhase::PlaceOrbit:
        Check(TEXT("k-hint-no-auto"), !lock.bEngaged && hudLine == ESOLHudLockLine::Hint, FString::Printf(TEXT("at "
            "%.0f km: %s, HUD line %s"), LEVEL_SMOKE_ORBIT_M / SOL::METERS_PER_KM, *DescribeLock(),
            LevelSmokeHudLineName(hudLine)));
        break;
    case ESOLLevelPhase::PressManual:
        Check(TEXT("l-manual-engage"), lock.bEngaged && lock.bManual && !lock.bWarning && bOnEarth
            && hudLine == ESOLHudLockLine::Engaged, FString::Printf(TEXT("L at %.0f km: %s, HUD line %s"),
            LEVEL_SMOKE_ORBIT_M / SOL::METERS_PER_KM, *DescribeLock(), LevelSmokeHudLineName(hudLine)));
        break;
    case ESOLLevelPhase::ManualWarn:
        Check(TEXT("m-manual-warning"), lock.bEngaged && lock.bManual && lock.bWarning
            && hudLine == ESOLHudLockLine::Warning, FString::Printf(TEXT("climbed to %.0f km (%.2fx the manual range): "
            "%s, HUD line %s"), mShips->GetSurfaceLockAltitudeM() / SOL::METERS_PER_KM,
            LEVEL_SMOKE_MANUAL_WARN_FRACTION, *DescribeLock(), LevelSmokeHudLineName(hudLine)));
        break;
    case ESOLLevelPhase::ManualRelease:
        Check(TEXT("n-manual-release"), !lock.bEngaged && !lock.bAutoSuppressed, FString::Printf(TEXT("climbed to "
            "%.2fx the manual range: %s"), LEVEL_SMOKE_MANUAL_RELEASE_FRACTION, *DescribeLock()));
        break;
    case ESOLLevelPhase::JumpPlace:
        Check(TEXT("o-jump-setup"), lock.bEngaged && !lock.bManual && bOnEarth, FString::Printf(TEXT("placed at %.0f km "
            "before the jump: %s"), LEVEL_SMOKE_LOW_M / SOL::METERS_PER_KM, *DescribeLock()));
        break;
    case ESOLLevelPhase::JumpWarpPress:
        Check(TEXT("p-warp-L-dropped"), mIsJumpStarted && mJump->IsWarping() && lock.bEngaged && !lock.bAutoSuppressed
            && bOnEarth, FString::Printf(TEXT("jump started %s, %.2f s into the warp (warping %s), L pressed: %s"),
            mIsJumpStarted ? TEXT("yes") : TEXT("NO"), mJump->GetElapsedS(), mJump->IsWarping() ? TEXT("yes")
            : TEXT("NO"), *DescribeLock()));
        break;
    case ESOLLevelPhase::JumpArrive:
    {
        double altitudeM = 0.0;
        const int32 nearest = mShips->FindNearestBody(altitudeM);
        Check(TEXT("q-jump-clears-lock"), !mJump->IsWarping() && mJump->GetCompletedJumpCount() == mJumpCountBefore + 1
            && !lock.bEngaged && !lock.bAutoSuppressed && nearest == mEarthIndex, FString::Printf(TEXT("jumps "
            "completed %d -> %d, warping %s, arrived %.1f km above body %d (Earth %d): %s"), mJumpCountBefore,
            mJump->GetCompletedJumpCount(), mJump->IsWarping() ? TEXT("YES") : TEXT("no"), altitudeM
            / SOL::METERS_PER_KM, nearest, mEarthIndex, *DescribeLock()));
        break;
    }
    case ESOLLevelPhase::Finish:
        UE_LOG(LogSOL, Log, TEXT("SmokeLevel: complete after %.1f s, %d/%d checks passed%s"), mTotalTimeS,
            mChecksPassed, mChecksRun, mChecksPassed == mChecksRun ? TEXT("") : TEXT(" (FAILURES)"));
        if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
        {
            gameMode->TakeSmokeScreenshot();
        }
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Teleports the ship straight above Earth at an altitude (keeping its bearing), matched to Earth's velocity, with its
// up kept, tilted 90 degrees off the local vertical, or along it
void FSOLSurfaceLockSmoke::PlaceAboveEarth(const double altitudeM, const ESOLLevelPlaceUp up)
{
    // Ship state and the cache are both in the Unreal-handed frame
    const FSOLBodyFrameCache& cache = mRegistry->GetUnrealFrameCache();
    if (!cache.PositionsM.IsValidIndex(mEarthIndex))
    {
        return;
    }
    FSOLShipState state = mShips->GetState();
    const FVector3d earthM = cache.PositionsM[mEarthIndex];
    FVector3d radialDir = (state.PositionM - earthM).GetSafeNormal();
    if (radialDir.IsZero())
    {
        radialDir = FVector3d::ForwardVector;
    }
    state.PositionM = earthM + radialDir * (cache.RadiiM[mEarthIndex] + altitudeM);
    state.VelocityMps = cache.VelocitiesMps[mEarthIndex];
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;

    // Tilted: the nose points straight up, so the ship's up lies in the horizontal plane (90 degrees off); level: the
    // nose points along the horizon and the up along the local vertical
    const FVector3d helper = FMath::Abs(radialDir.Z) < 0.9 ? FVector3d::UpVector : FVector3d::ForwardVector;
    const FVector3d tangent = FVector3d::CrossProduct(radialDir, helper).GetSafeNormal();
    const TCHAR* upText = TEXT("");
    if (up == ESOLLevelPlaceUp::Tilt)
    {
        state.Orientation = FRotationMatrix::MakeFromXZ(radialDir, tangent).ToQuat();
        upText = TEXT(", up tilted 90 deg off the local vertical");
    }
    else if (up == ESOLLevelPlaceUp::Level)
    {
        state.Orientation = FRotationMatrix::MakeFromXZ(tangent, radialDir).ToQuat();
        upText = TEXT(", level");
    }
    mShips->SetState(state);
    UE_LOG(LogSOL, Log, TEXT("SmokeLevel: ship placed %.1f km above Earth%s"), altitudeM / SOL::METERS_PER_KM, upText);
}

//////////////////////////////////////////////////////////////////////////
// Locks the reference frame (as M does) to the nearest candidate that is not Earth; returns its index or INDEX_NONE
int32 FSOLSurfaceLockSmoke::LockFrameToOtherCandidate()
{
    // Release any lock, then cycle outward from the ship until the selection is not Earth
    if (mTargeting->IsFrameLocked())
    {
        mTargeting->ToggleFrameLock();
    }
    mTargeting->ClearTarget();
    const FVector3d shipM = mShips->GetState().PositionM;
    const int32 candidateCount = mTargeting->GetCandidates().Num();
    for (int32 attempt = 0; attempt < candidateCount; ++attempt)
    {
        mTargeting->CycleTarget(shipM, 1);
        const int32 selected = mTargeting->GetSelectedIndex();
        if (selected != INDEX_NONE && selected != mEarthIndex)
        {
            break;
        }
    }
    const int32 selected = mTargeting->GetSelectedIndex();
    if (selected == INDEX_NONE || selected == mEarthIndex)
    {
        mTargeting->ClearTarget();
        return INDEX_NONE;
    }

    // The lock stays on the candidate once the selection is cleared
    mTargeting->ToggleFrameLock();
    mTargeting->ClearTarget();
    UE_LOG(LogSOL, Log, TEXT("SmokeLevel: reference frame M-locked to %s (candidate %d) before the auto-engage"),
        *mTargeting->GetFrameName().ToString(), mTargeting->GetLockedIndex());
    return mTargeting->GetLockedIndex();
}

//////////////////////////////////////////////////////////////////////////
// Records when the alignment starts after the tilted placement and samples the up error one time constant later
void FSOLSurfaceLockSmoke::TrackAlignment()
{
    if (mAlignStartS < 0.0)
    {
        // The step that engages the lock names the body; the alignment runs from the next step on
        if (mShips->GetSurfaceLockState().bEngaged)
        {
            mAlignStartS = mTotalTimeS;
            mAlignStartErrorDeg = ComputeUpErrorDeg();
        }
        return;
    }
    if (mTauElapsedS < 0.0 && mTotalTimeS - mAlignStartS >= SOL::SURFACE_LOCK_ALIGN_TIME_CONSTANT_S)
    {
        mTauElapsedS = mTotalTimeS - mAlignStartS;
        mTauErrorDeg = ComputeUpErrorDeg();
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the angle between the ship's up and the local vertical above Earth (degrees)
double FSOLSurfaceLockSmoke::ComputeUpErrorDeg() const
{
    const FSOLBodyFrameCache& cache = mRegistry->GetUnrealFrameCache();
    if (!cache.PositionsM.IsValidIndex(mEarthIndex))
    {
        return 180.0;
    }
    const FSOLShipState state = mShips->GetState();
    const FVector3d up = state.Orientation.RotateVector(FVector3d::UpVector);
    const FVector3d vertical = SOLSurfaceLock::TargetUpDir(state.PositionM, cache.PositionsM[mEarthIndex]);
    return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector3d::DotProduct(up, vertical), -1.0, 1.0)));
}

//////////////////////////////////////////////////////////////////////////
// Returns a one-line description of the lock state for the check details
FString FSOLSurfaceLockSmoke::DescribeLock() const
{
    const FSOLSurfaceLockState& lock = mShips->GetSurfaceLockState();
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const int32 bodyIndex = lock.bEngaged ? lock.BodyIndex : lock.SuppressedBodyIndex;
    return FString::Printf(TEXT("engaged %s, manual %s, warning %s, auto-suppressed %s, body %s, lock altitude %.1f km"),
        lock.bEngaged ? TEXT("yes") : TEXT("no"), lock.bManual ? TEXT("yes") : TEXT("no"), lock.bWarning
        ? TEXT("yes") : TEXT("no"), lock.bAutoSuppressed ? TEXT("yes") : TEXT("no"), bodyIndex >= 0
        && bodyIndex < registry.Num() ? *registry.GetName(bodyIndex).ToString() : TEXT("none"),
        mShips->GetSurfaceLockAltitudeM() / SOL::METERS_PER_KM);
}

//////////////////////////////////////////////////////////////////////////
// Takes a verification screenshot and logs its name
void FSOLSurfaceLockSmoke::Screenshot(const ASOLShipPawn& pawn, const TCHAR* name) const
{
    UE_LOG(LogSOL, Log, TEXT("SmokeLevel: screenshot (%s)"), name);
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->CaptureScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Presses a key now and releases it on the next frame
void FSOLSurfaceLockSmoke::TapKey(const FKey& key)
{
    mController->InputKey(FInputKeyEventArgs::CreateSimulated(key, IE_Pressed, 1.0f));
    mPendingReleases.Add(key);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLSurfaceLockSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeLevel CHECK %-20s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
