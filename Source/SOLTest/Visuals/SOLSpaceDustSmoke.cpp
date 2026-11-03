/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Visuals/SOLSpaceDustSmoke.h"

#include "Interaction/SOLInteractionSubsystem.h"
#include "Map/SOLJumpSubsystem.h"
#include "Map/SOLMapModeSubsystem.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLOverlayMenuWidget.h"
#include "UI/SOLOverlayModel.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLSimClockSubsystem.h"
#include "Visuals/SOLSpaceDustComponent.h"
#include "Visuals/SOLSpaceDustMath.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // The script's steps, in order
    enum class EDustStep : int32
    {
        Setup,              // Ship at rest in its frame, the HUD and universe settle
        Stopped,            // Step 1: relative velocity 0 -> hidden, paused, opacity 0
        Slow,               // Step 2: 3 m/s -> points, partial opacity (screenshot)
        Fast,               // Step 3: 50 km/s -> opaque, streaks near the cap, direction (screenshot)
        ProfileOn,          // Step 12 (profile flag only): ProfileGPU and stat dumpave with the dust shown
        CvarOff,            // sol.SpaceDust 0 hides the dust (profile flag: ProfileGPU and stat dumpave again)
        VeryFast,           // Step 4: 0.3c -> opaque, apparent speed at the cap
        MatchPrep,          // Step 5 setup: the ship moves with a body, the body is selected
        MatchPress,         // Step 5: M locks the frame to the body -> faded out, hidden and paused
        MatchRestore,       // M again: the frame follows the anchor
        WarpBase,           // Fast again
        WarpUp,             // Step 6: 10x -> hidden at once
        WarpDown,           // Step 6: back to 1x -> shown, fade-in reset
        JumpBase,           // Fast again
        Jump,               // Step 7: Jump To the Moon -> hidden for the whole warp
        MapBase,            // Fast again
        MapOpen,            // Step 8: the map opens -> hidden
        MapClose,           // Step 8: the map closes -> shown
        MenuOpen,           // Step 9: Ctrl+O (screenshot with the menu)
        MenuClickOff,       // Step 9: a Slate click on the Space Dust row (after the Menu shot rendered) -> hidden
        MenuClickOn,        // Step 9: click again -> shown, fade-in reset
        MenuClickPlanets,   // Step 9: another row, so the Esc write is owed
        MenuClose,          // Step 9: Esc -> menu closed, exactly one settings write
        MasterOff,          // Step 10: master O off -> dust unaffected
        MasterOn,           // Step 10: master O on -> dust unaffected
        PauseOpen,          // Step 10a: the pause menu -> hidden
        PauseResume,        // Step 10a: resume -> shown, fade-in reset
        Rebase,             // Step 11: 50 km/s through render-origin rebases
        Landed,             // Step 10a: resting on the Moon's surface -> no dust
        Quit,
        Count,
    };

    constexpr double DUSTSMOKE_SETUP_S = 3.0;
    constexpr double DUSTSMOKE_STEP_S = 1.5;                    // Default step: covers the fade (7 tau = 1.05 s)
    constexpr double DUSTSMOKE_SHORT_S = 0.8;                   // Hard-gate steps: the dust hides at once
    constexpr double DUSTSMOKE_WARP_UP_S = 0.5;
    constexpr double DUSTSMOKE_CVAR_S = 0.6;
    constexpr double DUSTSMOKE_PAUSE_S = 1.0;                   // Pause menu step
    constexpr double DUSTSMOKE_MATCH_MIN_S = 0.4;               // M step: lasts at least this once the dust is hidden
    constexpr double DUSTSMOKE_REBASE_S = 4.0;                  // 200 km at 50 km/s: many 10 km rebases
    constexpr double DUSTSMOKE_LANDED_S = 2.5;                  // Fade (1 s) plus time to rest on the surface
    constexpr double DUSTSMOKE_LANDED_LOCK_S = 0.5;             // L is pressed after this if surface-lock is not on
    constexpr double DUSTSMOKE_QUIT_S = 2.0;
    constexpr double DUSTSMOKE_MATCH_TIMEOUT_S = 2.0;
    constexpr double DUSTSMOKE_MATCH_BUDGET_S = 1.5;            // SDD 43 section 11 step 5: hidden within this
    constexpr double DUSTSMOKE_VISIBLE_BUDGET_S = 1.0;          // SDD 43 section 11 step 2: visible within this
    constexpr double DUSTSMOKE_MAP_SETTLE_S = 0.5;
    constexpr double DUSTSMOKE_MAP_TIMEOUT_S = 3.0;
    constexpr double DUSTSMOKE_JUMP_TIMEOUT_S = 15.0;
    constexpr double DUSTSMOKE_JUMP_MIN_S = 0.5;
    constexpr double DUSTSMOKE_LOG_S = 1.0;
    constexpr int32 DUSTSMOKE_MIN_FRAMES = 5;
    constexpr int32 DUSTSMOKE_WARP_GRACE_FRAMES = 2;            // Jump warp frames before the dust must be hidden
    constexpr int32 DUSTSMOKE_WARP_HIDE_FRAMES = 3;             // Time warp: hidden within this many frames
    constexpr int32 DUSTSMOKE_CLICK_DELAY_FRAMES = 2;           // Click-off waits this many frames after the Menu shot
    constexpr double DUSTSMOKE_SLOW_MPS = 3.0;
    constexpr double DUSTSMOKE_FAST_MPS = 50000.0;
    constexpr double DUSTSMOKE_VERY_FAST_MPS = 0.3 * SOL::SPEED_OF_LIGHT_MPS;
    constexpr double DUSTSMOKE_OPAQUE = 0.99;                   // Smoothed opacity counted as fully faded in
    constexpr double DUSTSMOKE_FADE_RESET = 0.5;                // First shown frame after a reset starts below this
    constexpr double DUSTSMOKE_STREAK_TOLERANCE = 0.02;         // Streak length within 2% of the model
    constexpr double DUSTSMOKE_DIRECTION_DOT = 0.999;
    constexpr double DUSTSMOKE_APPARENT_FRACTION = 0.99;        // SDD 43 section 11 step 4
    constexpr double DUSTSMOKE_APPARENT_CAP_SLACK = 1.001;      // Apparent speed may exceed the cap by this (rounding)
    constexpr double DUSTSMOKE_POINT_TOLERANCE_CM = 0.01;       // Slow: unstretched streak equals the point size
    constexpr double DUSTSMOKE_STEP_SLACK = 1.2;                // Rebase: offset step bound = |v| dt x this
    constexpr double DUSTSMOKE_STEP_FLOOR_CM = 1.0;
    constexpr double DUSTSMOKE_LOCATION_FRAMES = 3.0;           // Rebase: Niagara within this many frames of travel
    constexpr double DUSTSMOKE_LOCATION_FLOOR_CM = 100.0;
    constexpr double DUSTSMOKE_LOCATION_LIMIT = 1.5;            // Rebase: Niagara within this x the rebase distance
    constexpr int32 DUSTSMOKE_REBASE_MIN_FRAMES = 30;
    constexpr int32 DUSTSMOKE_PROFILE_FRAMES = 240;             // stat dumpave window
    constexpr int32 DUSTSMOKE_PROFILE_GPU_FRAME = 20;           // ProfileGPU inside the window
    constexpr int32 DUSTSMOKE_PROFILE_TAIL_FRAMES = 30;         // Frames left for the dump to print
    // The dust component's own cycle stat (STATGROUP_SOLSpaceDust), averaged over the window, uncut (-ms=0)
    const TCHAR* const DUSTSMOKE_PROFILE_AVE_CMD = TEXT("stat dumpave -num=240 -ms=0 -root=STAT_SOL_SpaceDustUpdate");
    const TCHAR* const DUSTSMOKE_PROFILE_GPU_CMD = TEXT("ProfileGPU");
    const TCHAR* const DUSTSMOKE_CVAR_OFF_CMD = TEXT("sol.SpaceDust 0");
    const TCHAR* const DUSTSMOKE_CVAR_ON_CMD = TEXT("sol.SpaceDust 1");

    //////////////////////////////////////////////////////////////////////////
    // Returns the fixed unit direction of the relative velocity (a diagonal, so every axis is exercised)
    FVector3d RelativeDirection()
    {
        return FVector3d(0.36, 0.48, 0.8);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns how long a step lasts at least (real seconds)
    double StepDurationS(const EDustStep step)
    {
        switch (step)
        {
        case EDustStep::Setup:
            return DUSTSMOKE_SETUP_S;
        case EDustStep::CvarOff:
            return DUSTSMOKE_CVAR_S;
        case EDustStep::MatchRestore:
            return DUSTSMOKE_CVAR_S;
        case EDustStep::WarpUp:
            return DUSTSMOKE_WARP_UP_S;
        case EDustStep::MenuOpen:
        case EDustStep::MenuClickOff:
        case EDustStep::MasterOff:
        case EDustStep::MasterOn:
            return DUSTSMOKE_SHORT_S;
        case EDustStep::MenuClickPlanets:
            return DUSTSMOKE_WARP_UP_S;
        case EDustStep::MenuClose:
            return DUSTSMOKE_CVAR_S;
        case EDustStep::PauseOpen:
            return DUSTSMOKE_PAUSE_S;
        case EDustStep::Rebase:
            return DUSTSMOKE_REBASE_S;
        case EDustStep::Landed:
            return DUSTSMOKE_LANDED_S;
        case EDustStep::Quit:
            return DUSTSMOKE_QUIT_S;
        default:
            return DUSTSMOKE_STEP_S;
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a body index by name, or INDEX_NONE
    int32 FindBodyIndex(const USOLBodyRegistrySubsystem* bodies, const TCHAR* name)
    {
        return bodies != nullptr ? bodies->GetRegistry().FindByName(FName(name)) : INDEX_NONE;
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, subsystems and the dust component and starts the first step
void FSOLSpaceDustSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mDust = pawn.GetSpaceDust();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    mClock = world->GetSubsystem<USOLSimClockSubsystem>();
    mJump = world->GetSubsystem<USOLJumpSubsystem>();
    mMapMode = world->GetSubsystem<USOLMapModeSubsystem>();
    mMenu = world->GetSubsystem<USOLMenuSubsystem>();
    mSettings = USOLSettingsSubsystem::Get(&pawn);
    mTargeting = world->GetSubsystem<USOLTargetingSubsystem>();
    mInteraction = world->GetSubsystem<USOLInteractionSubsystem>();
    mBodies = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mMoon.Kind = ESOLObjectKind::Body;
    mMoon.Index = FindBodyIndex(mBodies.Get(), SOL::BodyNames::MOON);
    mProfile = FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_DUST_PROFILE);

    // The cost captures need a standalone -game process: in the editor, ProfileGPU opens its visualizer window and the
    // editor's own work pollutes the numbers
    if (mProfile && GIsEditor)
    {
        UE_LOG(LogSOL, Warning, TEXT("SmokeDust: -%s needs -game (standalone); the cost captures are skipped"),
            SOL::CommandLine::SMOKE_DUST_PROFILE);
        mProfile = false;
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeDust: scripted space dust run starting (settings slot %s, Niagara system %s, "
        "profile %s)"), mSettings.IsValid() ? *mSettings->GetSlotName() : TEXT("none"),
        mDust.IsValid() && mDust->HasNiagaraSystem() ? TEXT("loaded") : TEXT("MISSING"), mProfile ? TEXT("on")
        : TEXT("off"));
    mStep = 0;
    EnterStep(mStep, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLSpaceDustSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mStep >= static_cast<int32>(EDustStep::Count))
    {
        return true;
    }
    if (!mController.IsValid() || !mDust.IsValid() || !mShips.IsValid() || !mAnchor.IsValid() || !mClock.IsValid()
        || !mJump.IsValid() || !mMapMode.IsValid() || !mMenu.IsValid() || !mSettings.IsValid()
        || !mTargeting.IsValid() || !mInteraction.IsValid() || !mBodies.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeDust: controller, dust component or subsystems missing; aborting"));
        mStep = static_cast<int32>(EDustStep::Count);
        return true;
    }

    mClockS += realDeltaSeconds;
    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;

    // The ship's velocity is re-set before the universe update that reads it, then the dust is observed
    ApplyHold();
    ObserveDust();

    // Jump warp frames: the dust must be hidden for the whole warp (a grace of a couple of frames for update order)
    if (mJump->IsWarping())
    {
        ++mWarpFrames;
        if (static_cast<EDustStep>(mStep) == EDustStep::Jump)
        {
            ++mWarpSeenFrames;
            mWarpShownFrames += mWarpFrames > DUSTSMOKE_WARP_GRACE_FRAMES && mDust->IsDustShown() ? 1 : 0;
        }
    }
    else
    {
        mWarpFrames = 0;
    }

    // A status line once a second
    if (mClockS - mLastLogS >= DUSTSMOKE_LOG_S)
    {
        mLastLogS = mClockS;
        UE_LOG(LogSOL, Log, TEXT("SmokeDust: t %.1f s step %d: |vRel| %.1f m/s, dust %s%s, opacity %.3f (target %.3f), "
            "streak %.0f cm alpha %.2f, warp %.0fx"), mClockS, mStep, GetRelativeVelocityMps().Size(),
            mDust->IsDustShown() ? TEXT("shown") : TEXT("hidden"), mDust->IsNiagaraPaused() ? TEXT(" (paused)")
            : TEXT(""), mDust->GetSmoothedOpacity(), mDust->GetTargetOpacity(), mDust->GetStreakLengthCm(),
            mDust->GetStreakAlpha(), mClock->GetClock().GetWarpFactor());
    }

    if (!TickStep(mStep, pawn) || mStepFrames < DUSTSMOKE_MIN_FRAMES)
    {
        return false;
    }
    ExitStep(mStep, pawn);
    ++mStep;
    mStepTimeS = 0.0;
    mStepFrames = 0;
    mProfileFrame = 0;
    if (mStep >= static_cast<int32>(EDustStep::Count))
    {
        return true;
    }
    EnterStep(mStep, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Holds the ship at a velocity relative to the reference frame (m/s, Unreal-handed)
void FSOLSpaceDustSmoke::HoldRelative(const FVector3d& relativeVelocityMps)
{
    mHold = EHold::Relative;
    mHoldVelocityMps = relativeVelocityMps;
}

//////////////////////////////////////////////////////////////////////////
// Holds the ship at the velocity of a body
void FSOLSpaceDustSmoke::HoldBody(const int32 bodyIndex)
{
    mHold = EHold::Body;
    mHoldBodyIndex = bodyIndex;
}

//////////////////////////////////////////////////////////////////////////
// Stops holding the ship's velocity
void FSOLSpaceDustSmoke::HoldNone()
{
    mHold = EHold::None;
}

//////////////////////////////////////////////////////////////////////////
// Sets the ship's velocity for this frame from the hold mode (not during a jump warp); the velocity only, never the
// position, so the ship flies on and the render origin rebases by its normal hysteresis (no forced snap per frame)
void FSOLSpaceDustSmoke::ApplyHold() const
{
    if (mHold == EHold::None || mJump->IsWarping())
    {
        return;
    }
    if (mHold == EHold::Relative)
    {
        mShips->SetVelocityMps(mShips->GetReferenceVelocityMps() + mHoldVelocityMps);
        return;
    }
    const FSOLBodyFrameCache& cache = mBodies->GetUnrealFrameCache();
    if (cache.VelocitiesMps.IsValidIndex(mHoldBodyIndex))
    {
        mShips->SetVelocityMps(cache.VelocitiesMps[mHoldBodyIndex]);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the velocity of the ship relative to its reference frame (m/s): the HUD speed's expression
FVector3d FSOLSpaceDustSmoke::GetRelativeVelocityMps() const
{
    return mShips->GetState().VelocityMps - mShips->GetReferenceVelocityMps();
}

//////////////////////////////////////////////////////////////////////////
// Records the first frame the dust is shown and the first frame it is hidden and paused, within the step
void FSOLSpaceDustSmoke::ObserveDust()
{
    const USOLSpaceDustComponent* dust = mDust.Get();
    if (dust->IsDustShown())
    {
        if (mShownFrame < 0)
        {
            mShownFrame = mStepFrames;
            mShownAtS = mStepTimeS;
            mShownOpacity = dust->GetSmoothedOpacity();
        }
    }
    else if (dust->IsNiagaraPaused() && mHiddenFrame < 0)
    {
        mHiddenFrame = mStepFrames;
        mHiddenAtS = mStepTimeS;
    }
}

//////////////////////////////////////////////////////////////////////////
// Moves the ship onto the Moon's surface (a ship radius above the centre), at rest in the Moon's frame
void FSOLSpaceDustSmoke::PlaceOnMoonSurface() const
{
    const FSOLBodyFrameCache& cache = mBodies->GetUnrealFrameCache();
    if (!cache.PositionsM.IsValidIndex(mMoon.Index) || !cache.VelocitiesMps.IsValidIndex(mMoon.Index)
        || !cache.RadiiM.IsValidIndex(mMoon.Index))
    {
        return;
    }
    FSOLShipState state = mShips->GetState();
    FVector3d radial = (state.PositionM - cache.PositionsM[mMoon.Index]).GetSafeNormal();
    if (radial.IsZero())
    {
        radial = FVector3d::ForwardVector;
    }
    state.PositionM = cache.PositionsM[mMoon.Index]
        + radial * (cache.RadiiM[mMoon.Index] + mShips->GetFlightParams().ShipRadiusM);
    state.VelocityMps = cache.VelocitiesMps[mMoon.Index];
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Sends a real Slate click to the centre of an Overlay Menu row; false when the menu is not there
bool FSOLSpaceDustSmoke::ClickRow(const ASOLShipPawn& pawn, const int32 row) const
{
    const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
    if (menu == nullptr || !FSlateApplication::IsInitialized())
    {
        return false;
    }

    // The pointer's hot spot is where the click lands
    FSlateApplication& slate = FSlateApplication::Get();
    const FVector2D at = menu->GetRowScreenCenter(row);
    slate.SetCursorPos(at);
    const FPointerEvent down(slate.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, at, at,
        TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
    slate.ProcessMouseButtonDownEvent(nullptr, down);
    const FPointerEvent up(slate.GetUserIndexForMouse(), FSlateApplicationBase::CursorPointerIndex, at, at,
        TSet<FKey>(), EKeys::LeftMouseButton, 0.0f, FModifierKeysState());
    slate.ProcessMouseButtonUpEvent(up);
    UE_LOG(LogSOL, Log, TEXT("SmokeDust: Slate click on row %d at (%.0f, %.0f)"), row, at.X, at.Y);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Sends Esc through Slate (closes the Overlay Menu)
void FSOLSpaceDustSmoke::PressEscape() const
{
    if (!FSlateApplication::IsInitialized())
    {
        return;
    }
    FSlateApplication& slate = FSlateApplication::Get();
    const FKeyEvent keyEvent(EKeys::Escape, FModifierKeysState(), slate.GetUserIndexForKeyboard(), false, 0, 0);
    slate.ProcessKeyDownEvent(keyEvent);
    slate.ProcessKeyUpEvent(keyEvent);
}

//////////////////////////////////////////////////////////////////////////
// Runs a console command in the game world
void FSOLSpaceDustSmoke::RunCommand(const TCHAR* command, UWorld* world) const
{
    if (GEngine != nullptr)
    {
        GEngine->Exec(world, command);
        UE_LOG(LogSOL, Log, TEXT("SmokeDust: console command '%s'"), command);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLSpaceDustSmoke::EnterStep(const int32 step, ASOLShipPawn& pawn)
{
    // Per-step observations restart; the action steps need to know the dust was up when they began
    mShownFrame = -1;
    mHiddenFrame = -1;
    mShownAtS = -1.0;
    mHiddenAtS = -1.0;
    mShownOpacity = -1.0;
    mWasShownAtEnter = mDust->IsDustShown();
    UWorld* world = pawn.GetWorld();
    switch (static_cast<EDustStep>(step))
    {
    case EDustStep::Setup:
    case EDustStep::Stopped:
    case EDustStep::MatchRestore:
        HoldRelative(FVector3d::ZeroVector);
        if (static_cast<EDustStep>(step) == EDustStep::MatchRestore)
        {
            // M again releases the lock: the frame follows the anchor
            pawn.HandleMatchLock();
        }
        break;
    case EDustStep::Slow:
        HoldRelative(RelativeDirection() * DUSTSMOKE_SLOW_MPS);
        break;
    case EDustStep::Fast:
    case EDustStep::WarpBase:
    case EDustStep::JumpBase:
    case EDustStep::MapBase:
    case EDustStep::Rebase:
        HoldRelative(RelativeDirection() * DUSTSMOKE_FAST_MPS);
        if (static_cast<EDustStep>(step) == EDustStep::Rebase)
        {
            mRebaseCount = 0;
            mContinuityFrames = 0;
            mContinuityViolations = 0;
            mLocationFrames = 0;
            mLocationViolations = 0;
            mMaxStepRatio = 0.0;
            mMaxLocationErrorCm = 0.0;
            mMaxLocationCm = 0.0;
            mForwardTravelCm = 0.0;
            mHasPrevOffset = false;
            mHasPrevOrigin = false;
        }
        break;
    case EDustStep::CvarOff:
        RunCommand(DUSTSMOKE_CVAR_OFF_CMD, world);
        break;
    case EDustStep::VeryFast:
        HoldRelative(RelativeDirection() * DUSTSMOKE_VERY_FAST_MPS);
        break;
    case EDustStep::MatchPrep:
    {
        // The ship moves with a body other than the anchor (the Moon, or Earth when the Moon is the anchor), which is
        // selected like a player would; the frame still follows the anchor, so the relative velocity is the body's
        const int32 earthIndex = FindBodyIndex(mBodies.Get(), SOL::BodyNames::EARTH);
        mMatchBodyIndex = mAnchor->GetAnchorIndex() == mMoon.Index ? earthIndex : mMoon.Index;
        if (mTargeting->IsFrameLocked())
        {
            mTargeting->ToggleFrameLock();
        }
        FSOLObjectRef body;
        body.Kind = ESOLObjectKind::Body;
        body.Index = mMatchBodyIndex;
        mInteraction->Execute(body, ESOLAction::Target);
        HoldBody(mMatchBodyIndex);
        break;
    }
    case EDustStep::MatchPress:
        pawn.HandleMatchLock();
        break;
    case EDustStep::WarpUp:
        pawn.HandleWarpUp();
        break;
    case EDustStep::WarpDown:
        pawn.HandleWarpReset();
        break;
    case EDustStep::Jump:
        mWarpFrames = 0;
        mWarpSeenFrames = 0;
        mWarpShownFrames = 0;
        mJumpsBefore = mJump->GetCompletedJumpCount();
        HoldNone();
        if (!mInteraction->Execute(mMoon, ESOLAction::JumpTo))
        {
            UE_LOG(LogSOL, Warning, TEXT("SmokeDust: Jump To the Moon was refused"));
        }
        break;
    case EDustStep::MapOpen:
        mMapOpenedAtS = -1.0;
        pawn.HandleOpenMap(false);
        break;
    case EDustStep::MapClose:
        pawn.HandleCloseMap();
        break;
    case EDustStep::MenuOpen:
        pawn.HandleToggleOverlayMenu();
        break;
    case EDustStep::MenuClickOff:
        // The click itself waits a couple of frames (TickStep), so the Menu screenshot renders the row still checked
        mSavesBefore = mSettings->GetSaveRequestCount();
        mIsRowClicked = false;
        break;
    case EDustStep::MenuClickOn:
        ClickRow(pawn, static_cast<int32>(SOLOverlay::EOverlay::SpaceDust));
        break;
    case EDustStep::MenuClickPlanets:
        ClickRow(pawn, static_cast<int32>(SOLOverlay::EOverlay::IconPlanets));
        break;
    case EDustStep::MenuClose:
        PressEscape();
        break;
    case EDustStep::MasterOff:
    case EDustStep::MasterOn:
        pawn.HandleToggleOverlayMaster();
        break;
    case EDustStep::PauseOpen:
        pawn.HandlePause();
        break;
    case EDustStep::PauseResume:
        mMenu->RequestResume();
        break;
    case EDustStep::Landed:
        mIsLevelPressed = false;
        HoldNone();
        PlaceOnMoonSurface();
        break;
    case EDustStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("SmokeDust: %d/%d checks passed"), mChecksPassed, mChecksRun);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's per-frame work; returns true once the step is done
bool FSOLSpaceDustSmoke::TickStep(const int32 step, ASOLShipPawn& pawn)
{
    const double t = mStepTimeS;
    const EDustStep dustStep = static_cast<EDustStep>(step);
    switch (dustStep)
    {
    case EDustStep::ProfileOn:
        return TickProfile(TEXT("dust shown at 50 km/s"), pawn.GetWorld());
    case EDustStep::CvarOff:
        return mProfile ? TickProfile(TEXT("sol.SpaceDust 0"), pawn.GetWorld()) : t >= DUSTSMOKE_CVAR_S;
    case EDustStep::MatchPress:
        return (mHiddenFrame >= 0 && t >= DUSTSMOKE_MATCH_MIN_S) || t >= DUSTSMOKE_MATCH_TIMEOUT_S;
    case EDustStep::MenuClickOff:
        // The screenshot requested as the menu step ended renders in that frame; the row is clicked only after it
        if (!mIsRowClicked && mStepFrames >= DUSTSMOKE_CLICK_DELAY_FRAMES)
        {
            mIsRowClicked = true;
            ClickRow(pawn, static_cast<int32>(SOLOverlay::EOverlay::SpaceDust));
        }
        return mIsRowClicked && t >= DUSTSMOKE_SHORT_S;
    case EDustStep::Jump:
        return (mJump->GetCompletedJumpCount() > mJumpsBefore && !mJump->IsWarping() && t >= DUSTSMOKE_JUMP_MIN_S)
            || t >= DUSTSMOKE_JUMP_TIMEOUT_S;
    case EDustStep::MapOpen:
        if (mMapOpenedAtS < 0.0 && mMapMode->IsMapOpen())
        {
            mMapOpenedAtS = t;
        }
        return (mMapOpenedAtS >= 0.0 && t - mMapOpenedAtS >= DUSTSMOKE_MAP_SETTLE_S) || t >= DUSTSMOKE_MAP_TIMEOUT_S;
    case EDustStep::Rebase:
        TrackRebase();
        return t >= DUSTSMOKE_REBASE_S;
    case EDustStep::Landed:
        // Auto-engage normally takes the ship; if surface-lock is not engaged yet, L asks for it once
        if (!mIsLevelPressed && t >= DUSTSMOKE_LANDED_LOCK_S && !mShips->GetSurfaceLockState().bEngaged)
        {
            mIsLevelPressed = true;
            pawn.HandleToggleLevel();
        }
        return t >= DUSTSMOKE_LANDED_S;
    default:
        return t >= StepDurationS(dustStep);
    }
}

//////////////////////////////////////////////////////////////////////////
// Follows the wrapped offset's continuity and the Niagara component's placement, frame by frame (the rebase step)
void FSOLSpaceDustSmoke::TrackRebase()
{
    const USOLSpaceDustComponent* dust = mDust.Get();
    const USOLAnchorSubsystem* anchor = mAnchor.Get();

    // A rebase is a change of the render origin between two frames
    const FVector3d origin = anchor->GetRenderOriginM();
    if (mHasPrevOrigin && origin != mPrevOriginM)
    {
        ++mRebaseCount;
    }
    mPrevOriginM = origin;
    mHasPrevOrigin = true;
    if (!dust->IsDustShown())
    {
        mHasPrevOffset = false;
        return;
    }

    // Continuity: the wrapped offset moves by at most |apparent v| dt (modulo the box length), whatever the rebase
    // does. The pawn ticks before the anchor, so one dust update lies between two observations; its dt is the
    // component's own (the anchor's clamped real delta of that update), not this tick's
    const double boxCm = SOL::SpaceDust::BOX_SIZE_CM;
    const FVector3d offsetCm = dust->GetWrappedOffsetCm();
    const double speedCmPerS = dust->GetSmoothedApparentVelocityCmPerS().Size();
    const double dustDeltaS = dust->GetLastAdvanceDeltaS();
    if (mHasPrevOffset)
    {
        FVector3d delta = offsetCm - mPrevOffsetCm;
        delta.X -= boxCm * FMath::RoundToDouble(delta.X / boxCm);
        delta.Y -= boxCm * FMath::RoundToDouble(delta.Y / boxCm);
        delta.Z -= boxCm * FMath::RoundToDouble(delta.Z / boxCm);
        const double boundCm = FMath::Max(mPrevSpeedCmPerS, speedCmPerS) * dustDeltaS * DUSTSMOKE_STEP_SLACK
            + DUSTSMOKE_STEP_FLOOR_CM;
        const double stepCm = delta.Size();
        ++mContinuityFrames;
        mContinuityViolations += stepCm > boundCm ? 1 : 0;
        mMaxStepRatio = FMath::Max(mMaxStepRatio, stepCm / boundCm);
        mForwardTravelCm += FVector3d::DotProduct(delta, RelativeDirection());
    }
    mPrevOffsetCm = offsetCm;
    mPrevSpeedCmPerS = speedCmPerS;
    mHasPrevOffset = true;

    // Placement: the Niagara component sits at the ship's render location (a couple of frames of travel allowed for the
    // listener order); a mistake of one rebase step would be 10 km
    const FVector3d expectedCm = anchor->GetRenderOrigin().UniverseToRenderCm(anchor->GetObserverPositionM());
    const FVector3d locationCm = FVector3d(dust->GetNiagaraWorldLocation());
    const double errorCm = FVector3d::Dist(locationCm, expectedCm);
    const double toleranceCm = DUSTSMOKE_LOCATION_FRAMES * mHoldVelocityMps.Size() * SOL::METERS_TO_CM * dustDeltaS
        + DUSTSMOKE_LOCATION_FLOOR_CM;
    const double limitCm = DUSTSMOKE_LOCATION_LIMIT * SOL::RENDER_REBASE_DISTANCE_M * SOL::METERS_TO_CM;
    ++mLocationFrames;
    mLocationViolations += errorCm > toleranceCm || locationCm.Size() > limitCm ? 1 : 0;
    mMaxLocationErrorCm = FMath::Max(mMaxLocationErrorCm, errorCm);
    mMaxLocationCm = FMath::Max(mMaxLocationCm, locationCm.Size());
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLSpaceDustSmoke::ExitStep(const int32 step, ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const USOLSpaceDustComponent* dust = mDust.Get();
    const FSOLSpaceDustParams params;
    const bool bShown = dust->IsDustShown();
    const bool bPaused = dust->IsNiagaraPaused();
    const double opacity = dust->GetSmoothedOpacity();
    const double relativeSpeedMps = GetRelativeVelocityMps().Size();
    const FVector3d apparentCmPerS = dust->GetSmoothedApparentVelocityCmPerS();
    const double apparentMps = apparentCmPerS.Size() * SOL::CM_TO_METERS;
    const double warpFactor = mClock->GetClock().GetWarpFactor();
    switch (static_cast<EDustStep>(step))
    {
    case EDustStep::Setup:
        Check(TEXT("0-system"), dust->HasNiagaraSystem() && mMoon.Index != INDEX_NONE,
            FString::Printf(TEXT("Niagara system %s, Moon index %d"), dust->HasNiagaraSystem() ? TEXT("loaded")
            : TEXT("MISSING"), mMoon.Index));
        break;
    case EDustStep::Stopped:
        Check(TEXT("1-stopped"), !bShown && bPaused && opacity < SOL::SpaceDust::OPACITY_EPSILON
            && dust->GetTargetOpacity() == 0.0, FString::Printf(TEXT("|vRel| %.4f m/s: dust %s, Niagara %s, opacity "
            "%.5f (target %.3f)"), relativeSpeedMps, bShown ? TEXT("SHOWN") : TEXT("hidden"), bPaused ? TEXT("paused")
            : TEXT("RUNNING"), opacity, dust->GetTargetOpacity()));
        break;
    case EDustStep::Slow:
        Check(TEXT("2-slow"), mShownFrame >= 0 && mShownAtS <= DUSTSMOKE_VISIBLE_BUDGET_S && bShown && opacity > 0.0
            && opacity < 1.0
            && FMath::IsNearlyEqual(dust->GetStreakLengthCm(), params.PointSizeCm, DUSTSMOKE_POINT_TOLERANCE_CM),
            FString::Printf(TEXT("%.1f m/s: shown after %.2f s (budget %.1f), opacity %.3f (target %.3f), streak %.2f "
            "cm (point %.1f cm), apparent speed %.3f m/s"), relativeSpeedMps, mShownAtS, DUSTSMOKE_VISIBLE_BUDGET_S,
            opacity, dust->GetTargetOpacity(), dust->GetStreakLengthCm(), params.PointSizeCm, apparentMps));
        TakeShot(TEXT("Slow"), false);
        break;
    case EDustStep::Fast:
    {
        // The velocity parameter points along vRel (the GPU shows its negative, particles stream the other way)
        const double expectedStreakCm = SOLSpaceDust::StreakLengthCm(params.ApparentMaxMps, params);
        const double dot = FVector3d::DotProduct(apparentCmPerS.GetSafeNormal(), RelativeDirection());
        Check(TEXT("3-fast"), bShown && opacity >= DUSTSMOKE_OPAQUE && FMath::Abs(dust->GetStreakLengthCm()
            - expectedStreakCm) <= DUSTSMOKE_STREAK_TOLERANCE * expectedStreakCm && dot >= DUSTSMOKE_DIRECTION_DOT,
            FString::Printf(TEXT("%.0f m/s: opacity %.4f, streak %.1f cm (model at the cap speed %.1f, cap %.0f), "
            "streak alpha %.3f, velocity parameter along vRel dot %.5f (|v| %.1f cm/s)"), relativeSpeedMps, opacity,
            dust->GetStreakLengthCm(), expectedStreakCm, params.StreakMaxCm, dust->GetStreakAlpha(), dot,
            apparentCmPerS.Size()));
        TakeShot(TEXT("Fast"), false);
        break;
    }
    case EDustStep::CvarOff:
        Check(TEXT("12-cvar-off"), !bShown && bPaused, FString::Printf(TEXT("sol.SpaceDust 0: dust %s, Niagara %s"),
            bShown ? TEXT("SHOWN") : TEXT("hidden"), bPaused ? TEXT("paused") : TEXT("RUNNING")));
        RunCommand(DUSTSMOKE_CVAR_ON_CMD, world);
        break;
    case EDustStep::VeryFast:
        Check(TEXT("4-very-fast"), bShown && opacity >= DUSTSMOKE_OPAQUE
            && apparentMps >= DUSTSMOKE_APPARENT_FRACTION * params.ApparentMaxMps
            && apparentMps <= DUSTSMOKE_APPARENT_CAP_SLACK * params.ApparentMaxMps,
            FString::Printf(TEXT("%.3g m/s (%.2f c): opacity %.4f, apparent speed %.2f m/s (cap %.0f, need >= %.1f), "
            "streak %.1f cm"), relativeSpeedMps, relativeSpeedMps / SOL::SPEED_OF_LIGHT_MPS, opacity, apparentMps,
            params.ApparentMaxMps, DUSTSMOKE_APPARENT_FRACTION * params.ApparentMaxMps, dust->GetStreakLengthCm()));
        break;
    case EDustStep::MatchPrep:
        Check(TEXT("5-match-setup"), bShown && opacity > DUSTSMOKE_FADE_RESET && !mTargeting->IsFrameLocked()
            && mMatchBodyIndex != INDEX_NONE, FString::Printf(TEXT("ship moves with body %d, frame %s: |vRel| %.1f "
            "m/s, dust %s, opacity %.3f"), mMatchBodyIndex, *mTargeting->GetFrameName().ToString(), relativeSpeedMps,
            bShown ? TEXT("shown") : TEXT("HIDDEN"), opacity));
        break;
    case EDustStep::MatchPress:
        Check(TEXT("5-match"), mWasShownAtEnter && mTargeting->IsFrameLocked() && mTargeting->GetLockedIndex()
            == mMatchBodyIndex && mHiddenFrame >= 0 && mHiddenAtS <= DUSTSMOKE_MATCH_BUDGET_S && !bShown && bPaused,
            FString::Printf(TEXT("M: frame locked to %s (index %d, want %d), hidden and paused after %.2f s (budget "
            "%.1f), |vRel| now %.4f m/s, opacity %.5f"), *mTargeting->GetFrameName().ToString(),
            mTargeting->GetLockedIndex(), mMatchBodyIndex, mHiddenAtS, DUSTSMOKE_MATCH_BUDGET_S, relativeSpeedMps,
            opacity));
        break;
    case EDustStep::WarpUp:
        Check(TEXT("6-warp-hide"), mWasShownAtEnter && warpFactor > 1.0 && mHiddenFrame >= 0 && mHiddenFrame
            <= DUSTSMOKE_WARP_HIDE_FRAMES && !bShown && bPaused && opacity == 0.0, FString::Printf(TEXT("time warp "
            "%.0fx: hidden on step frame %d (limit %d), dust %s, Niagara %s, opacity %.5f"), warpFactor, mHiddenFrame,
            DUSTSMOKE_WARP_HIDE_FRAMES, bShown ? TEXT("SHOWN") : TEXT("hidden"), bPaused ? TEXT("paused")
            : TEXT("RUNNING"), opacity));
        break;
    case EDustStep::WarpDown:
        Check(TEXT("6-warp-back"), warpFactor == 1.0 && mShownFrame >= 0 && mShownOpacity < DUSTSMOKE_FADE_RESET
            && bShown && opacity >= DUSTSMOKE_OPAQUE, FString::Printf(TEXT("back to %.0fx: shown on step frame %d with "
            "opacity %.3f (reset below %.1f), now %.4f"), warpFactor, mShownFrame, mShownOpacity,
            DUSTSMOKE_FADE_RESET, opacity));
        break;
    case EDustStep::Jump:
        Check(TEXT("7-jump"), mWasShownAtEnter && mJump->GetCompletedJumpCount() == mJumpsBefore + 1
            && mWarpSeenFrames > 0 && mWarpShownFrames == 0, FString::Printf(TEXT("Jump To the Moon: jumps %d -> %d, "
            "warp ran %d frames, dust shown on %d of them after %d grace frames (dust was up at the start: %s)"),
            mJumpsBefore, mJump->GetCompletedJumpCount(), mWarpSeenFrames, mWarpShownFrames,
            DUSTSMOKE_WARP_GRACE_FRAMES, mWasShownAtEnter ? TEXT("yes") : TEXT("NO")));
        break;
    case EDustStep::MapOpen:
        Check(TEXT("8-map-open"), mWasShownAtEnter && mMapMode->IsMapOpen() && !bShown && bPaused,
            FString::Printf(TEXT("map %s: dust %s, Niagara %s"), mMapMode->IsMapOpen() ? TEXT("open")
            : TEXT("NOT open"), bShown ? TEXT("SHOWN") : TEXT("hidden"), bPaused ? TEXT("paused") : TEXT("RUNNING")));
        break;
    case EDustStep::MapClose:
        Check(TEXT("8-map-close"), !mMapMode->IsMapOpen() && mShownFrame >= 0 && mShownOpacity < DUSTSMOKE_FADE_RESET
            && bShown && opacity >= DUSTSMOKE_OPAQUE, FString::Printf(TEXT("map %s: dust %s, first shown opacity "
            "%.3f, now %.4f"), mMapMode->IsMapOpen() ? TEXT("STILL open") : TEXT("closed"), bShown ? TEXT("shown")
            : TEXT("HIDDEN"), mShownOpacity, opacity));
        TakeShot(TEXT("AfterMap"), false);
        break;
    case EDustStep::MenuOpen:
    {
        const USOLOverlayMenuWidget* menu = pawn.GetOverlayMenu();
        const int32 rows = menu != nullptr ? menu->GetRowCount() : 0;
        Check(TEXT("9-menu-open"), pawn.IsOverlayMenuOpen() && rows == SOLOverlay::OVERLAY_COUNT && bShown,
            FString::Printf(TEXT("Ctrl+O: menu %s, %d rows (table %d), dust %s"), pawn.IsOverlayMenuOpen()
            ? TEXT("open") : TEXT("CLOSED"), rows, SOLOverlay::OVERLAY_COUNT, bShown ? TEXT("shown") : TEXT("HIDDEN")));
        TakeShot(TEXT("Menu"), true);
        break;
    }
    case EDustStep::MenuClickOff:
    {
        const bool bRow = SOLOverlay::IsEnabled(mSettings->GetOverlayPrefs(), SOLOverlay::EOverlay::SpaceDust);
        Check(TEXT("9-click-off"), mWasShownAtEnter && !bRow && !bShown && bPaused && opacity == 0.0,
            FString::Printf(TEXT("click on Space Dust: row %s, dust %s, Niagara %s, opacity %.5f"), bRow ? TEXT("ON")
            : TEXT("off"), bShown ? TEXT("SHOWN") : TEXT("hidden"), bPaused ? TEXT("paused") : TEXT("RUNNING"),
            opacity));
        break;
    }
    case EDustStep::MenuClickOn:
    {
        const bool bRow = SOLOverlay::IsEnabled(mSettings->GetOverlayPrefs(), SOLOverlay::EOverlay::SpaceDust);
        Check(TEXT("9-click-on"), bRow && mShownFrame >= 0 && mShownOpacity < DUSTSMOKE_FADE_RESET && bShown
            && opacity >= DUSTSMOKE_OPAQUE, FString::Printf(TEXT("click again: row %s, dust %s, first shown opacity "
            "%.3f, now %.4f"), bRow ? TEXT("on") : TEXT("OFF"), bShown ? TEXT("shown") : TEXT("HIDDEN"),
            mShownOpacity, opacity));
        break;
    }
    case EDustStep::MenuClickPlanets:
        Check(TEXT("9-other-row"), bShown && opacity >= DUSTSMOKE_OPAQUE, FString::Printf(TEXT("click on Planets: "
            "dust %s, opacity %.4f (another row must not touch the dust)"), bShown ? TEXT("shown") : TEXT("HIDDEN"),
            opacity));
        break;
    case EDustStep::MenuClose:
    {
        // The Space Dust row went off and on again (no net change), so the owed write comes from the Planets row
        const int32 saves = mSettings->GetSaveRequestCount() - mSavesBefore;
        Check(TEXT("9-menu-close"), !pawn.IsOverlayMenuOpen() && saves == 1 && bShown, FString::Printf(TEXT("Esc: menu "
            "%s, %d settings write(s) since the first click, dust %s"), pawn.IsOverlayMenuOpen() ? TEXT("OPEN")
            : TEXT("closed"), saves, bShown ? TEXT("shown") : TEXT("HIDDEN")));
        break;
    }
    case EDustStep::MasterOff:
        Check(TEXT("10-master-off"), !mSettings->IsOverlayMasterOn() && bShown && opacity >= DUSTSMOKE_OPAQUE,
            FString::Printf(TEXT("O: master %s, dust %s, opacity %.4f"), mSettings->IsOverlayMasterOn() ? TEXT("ON")
            : TEXT("off"), bShown ? TEXT("shown") : TEXT("HIDDEN"), opacity));
        break;
    case EDustStep::MasterOn:
        Check(TEXT("10-master-on"), mSettings->IsOverlayMasterOn() && bShown && opacity >= DUSTSMOKE_OPAQUE,
            FString::Printf(TEXT("O again: master %s, dust %s, opacity %.4f"), mSettings->IsOverlayMasterOn()
            ? TEXT("on") : TEXT("OFF"), bShown ? TEXT("shown") : TEXT("HIDDEN"), opacity));
        break;
    case EDustStep::PauseOpen:
        Check(TEXT("10a-pause"), mWasShownAtEnter && mMenu->IsMenuShowing() && !bShown && bPaused,
            FString::Printf(TEXT("Esc: pause menu %s, dust %s, Niagara %s"), mMenu->IsMenuShowing() ? TEXT("shown")
            : TEXT("NOT shown"), bShown ? TEXT("SHOWN") : TEXT("hidden"), bPaused ? TEXT("paused") : TEXT("RUNNING")));
        break;
    case EDustStep::PauseResume:
        Check(TEXT("10a-resume"), !mMenu->IsMenuShowing() && mShownFrame >= 0 && mShownOpacity < DUSTSMOKE_FADE_RESET
            && bShown && opacity >= DUSTSMOKE_OPAQUE, FString::Printf(TEXT("resume: pause menu %s, dust %s, first "
            "shown opacity %.3f, now %.4f"), mMenu->IsMenuShowing() ? TEXT("STILL shown") : TEXT("closed"),
            bShown ? TEXT("shown") : TEXT("HIDDEN"), mShownOpacity, opacity));
        break;
    case EDustStep::Rebase:
        // At least one rebase, and fewer rebases than frames: real 10 km hysteresis rebases, not a snap every frame
        Check(TEXT("11-rebase"), mRebaseCount >= 1 && mRebaseCount < mLocationFrames
            && mContinuityFrames >= DUSTSMOKE_REBASE_MIN_FRAMES
            && mContinuityViolations == 0 && mLocationFrames >= DUSTSMOKE_REBASE_MIN_FRAMES && mLocationViolations == 0
            && mForwardTravelCm > 0.0, FString::Printf(TEXT("%d rebases over %d shown frames: offset step violations "
            "%d (worst %.2f of the |v| dt bound), forward travel %.0f cm; Niagara location violations %d of %d (worst "
            "%.0f cm off the ship, farthest %.0f cm from the origin, limit %.0f cm)"), mRebaseCount, mContinuityFrames,
            mContinuityViolations, mMaxStepRatio, mForwardTravelCm, mLocationViolations, mLocationFrames,
            mMaxLocationErrorCm, mMaxLocationCm, DUSTSMOKE_LOCATION_LIMIT * SOL::RENDER_REBASE_DISTANCE_M
            * SOL::METERS_TO_CM));
        break;
    case EDustStep::Landed:
        Check(TEXT("10a-landed"), mWasShownAtEnter && !bShown && opacity < SOL::SpaceDust::OPACITY_EPSILON,
            FString::Printf(TEXT("resting on the Moon: surface-lock %s, frame %s, |vRel| %.3f m/s (dead zone %.2f), "
            "dust %s, opacity %.5f"), mShips->GetSurfaceLockState().bEngaged ? TEXT("engaged") : TEXT("NOT engaged"),
            *mTargeting->GetFrameName().ToString(), relativeSpeedMps, params.DeadZoneMps, bShown ? TEXT("SHOWN")
            : TEXT("hidden"), opacity));
        break;
    case EDustStep::Quit:
        RunCommand(DUSTSMOKE_CVAR_ON_CMD, world);
        UKismetSystemLibrary::QuitGame(world, nullptr, EQuitPreference::Quit, false);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one cost capture from the first frame it is called; true once done, or at once without the profile flag
bool FSOLSpaceDustSmoke::TickProfile(const TCHAR* label, UWorld* world)
{
    if (!mProfile || GEngine == nullptr)
    {
        return true;
    }

    // stat dumpave averages the next 240 frames; ProfileGPU captures one frame inside that window. The numbers are
    // read from the log (the dumps print on the stats thread, the GPU profile on the render thread)
    if (mProfileFrame == 0)
    {
        mProfileStartS = mClockS;
        UE_LOG(LogSOL, Log, TEXT("SmokeDust PROFILE %s: start (dust %s, |vRel| %.1f m/s)"), label,
            mDust->IsDustShown() ? TEXT("shown") : TEXT("hidden"), GetRelativeVelocityMps().Size());
        RunCommand(DUSTSMOKE_PROFILE_AVE_CMD, world);
    }
    else if (mProfileFrame == DUSTSMOKE_PROFILE_GPU_FRAME)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeDust PROFILE %s: ProfileGPU at frame %d"), label, mProfileFrame);
        RunCommand(DUSTSMOKE_PROFILE_GPU_CMD, world);
    }
    else if (mProfileFrame == DUSTSMOKE_PROFILE_FRAMES + DUSTSMOKE_PROFILE_TAIL_FRAMES)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeDust PROFILE %s: done after %d frames, %.2f s (dust %s). The numbers are in "
            "this log: the game-thread cost is the LogStats block '240 frames, average' with the "
            "STAT_SOL_SpaceDustUpdate line (ms per frame), the GPU cost is the LogRHI block 'GPU Profile for Frame' "
            "(the Niagara and translucency passes); compare with the sol.SpaceDust 0 capture"), label, mProfileFrame,
            mClockS - mProfileStartS, mDust->IsDustShown() ? TEXT("shown") : TEXT("hidden"));
    }
    ++mProfileFrame;
    return mProfileFrame > DUSTSMOKE_PROFILE_FRAMES + DUSTSMOKE_PROFILE_TAIL_FRAMES;
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot to Saved/Screenshots (at most one per frame)
void FSOLSpaceDustSmoke::TakeShot(const TCHAR* name, const bool bWithUi)
{
    if (mLastShotFrame == GFrameCounter)
    {
        return;
    }
    mLastShotFrame = GFrameCounter;
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("DustSmoke_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, bWithUi, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeDust: screenshot %s %s"), bWithUi ? TEXT("with UI") : TEXT("(scene)"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLSpaceDustSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeDust CHECK %-16s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
