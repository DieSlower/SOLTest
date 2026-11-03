/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Interaction/SOLChaseSmoke.h"

#include "Combat/SOLCombatSubsystem.h"
#include "Flight/SOLChase.h"
#include "Interaction/SOLInteractMenus.h"
#include "Interaction/SOLInteractionSubsystem.h"
#include "Map/SOLJumpSubsystem.h"
#include "Menu/SOLMenuSubsystem.h"
#include "Menu/SOLSettingsSubsystem.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Targeting/SOLTargetingSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace
{
    // The script's steps, in order
    enum class EChaseStep : int32
    {
        Setup,              // Ship at rest, the Moon selected, boost on
        Drop,               // G: a target ahead
        Place,              // The ship 20 km behind it, icons settle
        RadialOpen,         // Fire on the target's icon, pointer on Chase Down (screenshot)
        Chase,              // Release: Chase Down runs until the ship holds on the tail
        Hold,               // 10 s on the tail
        WCancel,            // W: back to ASSISTED
        EscEngage,          // Chase again
        EscFirst,           // Esc: chase cancelled, no pause menu
        EscSecond,          // Esc again: the pause menu
        EscResume,          // Resume
        MoveEngage,         // Chase again
        MoveTo,             // Move To the Moon replaces it
        MoonChase,          // Chase Down the Moon
        MoonCancel,         // C: back to ASSISTED
        MarginPrep,         // 30% margin applied, full meter, 20 km away and receding
        Margin,             // Chase at 30% to the tail
        DefaultMarginPrep,  // Default margin again, full meter, 20 km away and receding
        DefaultMargin,      // Chase at 25% to the tail
        StandoffPrep,       // Full meter, 20 km away
        Standoff,           // Chase the large stand-in: holds at 3 x radius
        WarpPrep,
        Warp100,            // Chase at 100x
        Warp1000,           // ... and at 1000x
        BoostOffPrep,
        BoostOff,           // Tab boost off: at or below the unboosted cap
        BoostOn,            // Boost on: bursts above it
        DestroyWait,        // Back on the tail
        Destroy,            // Bolts destroy the target: the chase ends
        LostNote,           // The target-lost note (screenshot)
        MoonWarpPrep,       // Full meter, just outside the Moon's tail point
        MoonWarp,           // Moon chase at 1000x: holds the floor, holds the tail
        JumpEngage,         // Chase Down the Moon again
        JumpTo,             // Jump To the Moon: the chase ends in ASSISTED as the jump starts; the jump arrives
        ProfileWarpChase,   // Profile flag only: cost of a Moon chase at 1 d/s
        ProfileWarpMove,    // Profile flag only: cost of a Move To Neptune at 1 d/s, for comparison
        Quit,
        Count,
    };

    constexpr double CHASESMOKE_SETUP_S = 3.0;
    constexpr double CHASESMOKE_STEP_S = 0.5;
    constexpr double CHASESMOKE_SHOT_S = 1.2;
    constexpr double CHASESMOKE_PLACE_S = 1.5;
    constexpr double CHASESMOKE_HOLD_S = 10.0;
    constexpr double CHASESMOKE_MOON_S = 2.0;
    constexpr double CHASESMOKE_WARP_S = 8.0;
    constexpr double CHASESMOKE_WARP_SETTLE_S = 4.0;           // Warp steps: hold distance measured after this
    constexpr double CHASESMOKE_BOOST_OFF_S = 8.0;
    constexpr double CHASESMOKE_BOOST_ON_S = 4.0;
    constexpr double CHASESMOKE_TAIL_TIMEOUT_S = 150.0;        // Longest wait for the tail
    constexpr double CHASESMOKE_HOLD_SETTLE_S = 1.0;           // On the tail this long before a chase step ends
    constexpr double CHASESMOKE_STANDOFF_SETTLE_S = 3.0;
    constexpr double CHASESMOKE_DESTROY_TIMEOUT_S = 10.0;
    constexpr double CHASESMOKE_JUMP_TIMEOUT_S = 10.0;          // The jump warp (3 s) must have arrived by then
    constexpr double CHASESMOKE_QUIT_S = 3.0;
    constexpr int32 CHASESMOKE_MIN_FRAMES = 5;
    constexpr double CHASESMOKE_DISTANCE_M = 20000.0;          // Start distance from the target (SDD 32 section 5)
    constexpr double CHASESMOKE_RECEDE_MPS = 2000.0;           // Margin chases: the ship starts flying away this fast
    constexpr double CHASESMOKE_FLOOR_REACHED = 0.2;           // Margin chases: the meter came this close to the floor
    constexpr double CHASESMOKE_MOON_OFFSET_M = 100000.0;      // Moon warp chase: start this far outside the tail point
    constexpr double CHASESMOKE_MOON_WARP_S = 20.0;
    constexpr double CHASESMOKE_MOON_WARP_SETTLE_S = 12.0;
    constexpr double CHASESMOKE_BURST_WITHIN_S = 2.0;
    constexpr double CHASESMOKE_INTERCEPT_SLACK = 1.25;        // Intercept within the prediction + 25%
    constexpr double CHASESMOKE_HOLD_FACTOR = 2.0;             // On the tail: within 2 x the trail distance
    constexpr double CHASESMOKE_FLOOR_EPS = 0.002;             // Meter rounding allowed under the floor
    constexpr double CHASESMOKE_BURST_MARGIN_G = 0.1;          // A burst: load this far above sustained
    constexpr double CHASESMOKE_BOOST_ABOVE_G = 0.5;           // Boost on: the limit this far above the unboosted cap
    constexpr double CHASESMOKE_CAP_EPS_G = 0.01;
    constexpr float CHASESMOKE_MARGIN_TEST = 0.30f;
    constexpr double CHASESMOKE_BIG_RADIUS_M = 1000.0;         // The stand-in's radius: trail 3 km
    constexpr double CHASESMOKE_STANDOFF_TOLERANCE = 0.1;      // Hold distance within 10% of 3 x radius
    constexpr double CHASESMOKE_BOLT_SPEED_MPS = 3000.0;
    constexpr double CHASESMOKE_MUZZLE_M = 20.0;
    constexpr int32 CHASESMOKE_BOLTS_PER_FRAME = 2;
    constexpr double CHASESMOKE_LOG_S = 1.0;
    constexpr int32 CHASESMOKE_PROFILE_FRAMES = 1000;          // Frames per dump (sum, then max)
    constexpr int32 CHASESMOKE_PROFILE_TAIL_FRAMES = 30;       // Frames left for the last dump to print
    constexpr int32 CHASESMOKE_PROFILE_WARP_STEPS = 5;         // Warp steps up from 1x to 1 d/s (86400x)
    constexpr double CHASESMOKE_PROFILE_SETTLE_S = 4.0;        // Warp profile steps: capture after this
    // Stat dumps of the SOL ship cycle stats (raw names STAT_SOL_*), no time cull
    const TCHAR* const CHASESMOKE_PROFILE_SUM_CMD = TEXT("stat dumpsum -num=1000 -ms=0 -root=STAT_SOL_");
    const TCHAR* const CHASESMOKE_PROFILE_MAX_CMD = TEXT("stat dumpmax -num=1000 -ms=0 -root=STAT_SOL_");

    // Returns the activity's HUD word for the log
    const TCHAR* ActivityName(const ESOLChaseActivity activity)
    {
        switch (activity)
        {
        case ESOLChaseActivity::Burst:
            return TEXT("BURST");
        case ESOLChaseActivity::Cool:
            return TEXT("COOL");
        case ESOLChaseActivity::BrakeBurst:
            return TEXT("BRAKE");
        case ESOLChaseActivity::BrakeCool:
            return TEXT("BRAKE COOL");
        case ESOLChaseActivity::Hold:
            return TEXT("HOLD");
        default:
            return TEXT("?");
        }
    }

    // Returns the flight mode's HUD word for the log
    const TCHAR* ModeName(const ESOLFlightMode mode)
    {
        return mode == ESOLFlightMode::Auto ? TEXT("AUTO") : mode == ESOLFlightMode::Assisted ? TEXT("ASSISTED")
            : TEXT("MANUAL");
    }
}

//////////////////////////////////////////////////////////////////////////
// Points the stand-in at a combat slot and the drop it holds now, with the radius it reports
void USOLChaseSmokeTarget::Bind(USOLCombatSubsystem* combat, const int32 slot, const uint32 spawnSerial,
    const double radiusM)
{
    mCombat = combat;
    mSlot = slot;
    mSpawnSerial = spawnSerial;
    mRadiusM = radiusM;
}

//////////////////////////////////////////////////////////////////////////
// Returns the slot's live position and velocity (Unreal-handed universe frame) with the stand-in radius
FSOLTargetInfo USOLChaseSmokeTarget::GetTargetInfo() const
{
    const USOLCombatSubsystem* combat = mCombat.Get();
    if (combat == nullptr || !IsTargetAlive())
    {
        return mLastInfo;
    }
    FVector3d positionM = FVector3d::ZeroVector;
    FVector3d velocityMps = FVector3d::ZeroVector;
    combat->GetTargetLivePositionM(mSlot, positionM);
    combat->GetTargetVelocityMps(mSlot, velocityMps);
    FSOLTargetInfo info;
    info.Name = FName(TEXT("Smoke large target"));
    info.PositionM = SOLRender::EclipticToUnreal(positionM);
    info.VelocityMps = SOLRender::EclipticToUnreal(velocityMps);
    info.RadiusM = mRadiusM;
    mLastInfo = info;
    return info;
}

//////////////////////////////////////////////////////////////////////////
// Returns true while the bound slot still holds the same drop
bool USOLChaseSmokeTarget::IsTargetAlive() const
{
    const USOLCombatSubsystem* combat = mCombat.Get();
    if (combat == nullptr)
    {
        return false;
    }
    const TConstArrayView<bool> active = combat->GetTargetActive();
    const TConstArrayView<uint32> serials = combat->GetTargetSpawnSerials();
    return active.IsValidIndex(mSlot) && active[mSlot] && serials.IsValidIndex(mSlot) && serials[mSlot] == mSpawnSerial;
}

//////////////////////////////////////////////////////////////////////////
// Returns the combat frame body the drop is held relative to
int32 USOLChaseSmokeTarget::GetTargetFrameBodyIndex() const
{
    const USOLCombatSubsystem* combat = mCombat.Get();
    return combat != nullptr && IsTargetAlive() ? combat->GetTargetFrameBodyIndex(mSlot) : INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Caches the controller, HUD and subsystems and starts the first step
void FSOLChaseSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    mController = pawn.GetController<APlayerController>();
    mHud = mController.IsValid() ? mController->GetHUD<ASOLFlightHud>() : nullptr;
    mInteraction = world->GetSubsystem<USOLInteractionSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mCombat = world->GetSubsystem<USOLCombatSubsystem>();
    mBigTarget.Reset(NewObject<USOLChaseSmokeTarget>());
    if (mShips.IsValid())
    {
        const FSOLFlightParams params = mShips->GetFlightParams();
        mSustainedG = params.PilotLimits.SustainedG;
        mUnboostedCapG = SOLChase::ChaseMaxG(params.Envelope, params.PilotLimits, false);
    }
    if (const USOLBodyRegistrySubsystem* bodies = world->GetSubsystem<USOLBodyRegistrySubsystem>())
    {
        mMoon.Kind = ESOLObjectKind::Body;
        mMoon.Index = bodies->GetRegistry().FindByName(FName(SOL::BodyNames::MOON));
    }
    const USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(&pawn);
    pawn.SetFlightCursorScripted(true);
    mProfile = FParse::Param(FCommandLine::Get(), SOL::CommandLine::SMOKE_CHASE_PROFILE);
    UE_LOG(LogSOL, Log, TEXT("SmokeChase: scripted Chase Down starting (settings slot %s, sustained %.2f G, unboosted "
        "cap %.2f G)"), settings != nullptr ? *settings->GetSlotName() : TEXT("none"), mSustainedG, mUnboostedCapG);
    mStep = 0;
    EnterStep(mStep, pawn);
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLChaseSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mStep >= static_cast<int32>(EChaseStep::Count))
    {
        return true;
    }
    if (!mController.IsValid() || !mInteraction.IsValid() || !mShips.IsValid() || !mCombat.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeChase: controller or subsystems missing; aborting"));
        mStep = static_cast<int32>(EChaseStep::Count);
        return true;
    }
    if (!mHud.IsValid())
    {
        mHud = mController->GetHUD<ASOLFlightHud>();
    }

    // The chase is watched every frame, whatever the step; each step then decides when it is done
    mFrameDeltaS = realDeltaSeconds;
    mClockS += realDeltaSeconds;
    mStepTimeS += realDeltaSeconds;
    ++mStepFrames;
    MonitorChase(pawn);
    if (!TickStep(mStep, pawn) || mStepFrames < CHASESMOKE_MIN_FRAMES)
    {
        return false;
    }
    ExitStep(mStep, pawn);
    ++mStep;
    mStepTimeS = 0.0;
    mStepFrames = 0;
    mProfileFrame = 0;
    if (mStep >= static_cast<int32>(EChaseStep::Count))
    {
        return true;
    }
    EnterStep(mStep, pawn);
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Resets the per-chase measurements at an engage
void FSOLChaseSmoke::BeginRun()
{
    mRun = FChaseRun();
    mRun.EngagedAtS = mClockS;
    mRun.ArrivalsAtEngage = mShips->GetAutopilotArrivalCount();
    mHoldStreakS = 0.0;
}

//////////////////////////////////////////////////////////////////////////
// Watches the running chase every frame: the meter floor, the peaks, the intercept and the screenshots
void FSOLChaseSmoke::MonitorChase(ASOLShipPawn& pawn)
{
    if (!mShips->IsChasing())
    {
        mHoldStreakS = 0.0;
        return;
    }
    const FSOLChaseStatus status = mShips->GetChaseStatus();
    const double meter = mShips->GetGMeter().Meter;
    const double loadG = mShips->GetGLoadG();
    const double sinceS = mClockS - mRun.EngagedAtS;
    const USOLSimClockSubsystem* clock = pawn.GetWorld()->GetSubsystem<USOLSimClockSubsystem>();
    const int32 warpIndex = clock != nullptr ? clock->GetClock().GetWarpIndex() : 0;

    // The first frame fixes the chase's entry: its meter (a pilot already under the floor is only held there) and
    // the plan's prediction
    if (mRun.Frames == 0)
    {
        mRun.EntryMeter = meter;
        mRun.PredictedS = status.TimeToInterceptS;
    }
    else
    {
        // The meter never below the floor (or below where the chase found it, when that was already lower)
        const double allowed = FMath::Min(status.FloorMeter, mRun.EntryMeter) - CHASESMOKE_FLOOR_EPS;
        ++mFloorFrames;
        if (meter < allowed)
        {
            ++mFloorViolations;
            mWorstFloorDeficit = FMath::Max(mWorstFloorDeficit, allowed + CHASESMOKE_FLOOR_EPS - meter);
        }
    }
    ++mRun.Frames;
    mRun.MinMeter = FMath::Min(mRun.MinMeter, meter);
    mRun.MaxLoadG = FMath::Max(mRun.MaxLoadG, loadG);
    mRun.MaxLimitG = FMath::Max(mRun.MaxLimitG, status.LimitG);
    if (mRun.FirstBurstS < 0.0 && loadG > mSustainedG + CHASESMOKE_BURST_MARGIN_G)
    {
        mRun.FirstBurstS = sinceS;
    }
    if (status.bHolding && mRun.Frames > 3)
    {
        mHoldStreakS += mFrameDeltaS;
        if (mRun.InterceptS < 0.0)
        {
            mRun.InterceptS = sinceS;
        }
    }
    else
    {
        mHoldStreakS = 0.0;
    }
    if (mRun.bMeasureDistance)
    {
        mRun.MaxTargetDistanceM = FMath::Max(mRun.MaxTargetDistanceM, status.TargetDistanceM);
    }

    // Screenshots of the marker at 1x: a burst, a cool off, on hold
    if (warpIndex == 0)
    {
        if (!mHasShotBurst && status.Activity == ESOLChaseActivity::Burst && loadG > mSustainedG + 1.0 && sinceS > 0.7)
        {
            mHasShotBurst = true;
            TakeUiShot(TEXT("Burst"));
        }
        else if (!mHasShotCool && status.bCooling && (status.Activity == ESOLChaseActivity::Cool
            || status.Activity == ESOLChaseActivity::BrakeCool))
        {
            mHasShotCool = true;
            TakeUiShot(TEXT("Cool"));
        }
        else if (!mHasShotHold && mHoldStreakS > CHASESMOKE_HOLD_SETTLE_S)
        {
            mHasShotHold = true;
            TakeUiShot(TEXT("Hold"));
        }
    }

    // A status line once a second
    if (mClockS - mLastLogS >= CHASESMOKE_LOG_S)
    {
        mLastLogS = mClockS;
        UE_LOG(LogSOL, Log, TEXT("SmokeChase: t %.1f s warp %d %s %s: %s limit %.2f G load %.2f G meter %.3f (floor %.2f, "
            "resume %.2f) aim %.0f m target %.0f m (trail %.0f m) closing %.1f m/s T %.1f s plan %.2f/%.2f G"), sinceS,
            warpIndex, ModeName(mShips->GetFlightMode()), *status.TargetName.ToString(), ActivityName(status.Activity),
            status.LimitG, loadG, meter, status.FloorMeter, status.ResumeMeter, status.AimDistanceM,
            status.TargetDistanceM, status.TrailM, status.ClosingSpeedMps,
            status.TimeToInterceptS < 1.0e9 ? status.TimeToInterceptS : -1.0, status.BurstG, status.BrakePlanG);
    }
}

//////////////////////////////////////////////////////////////////////////
// Engages Chase Down on the dropped target through the interaction subsystem (the menus' Execute path)
bool FSOLChaseSmoke::ChaseTarget()
{
    BeginRun();
    return mInteraction->Execute(mTarget, ESOLAction::ChaseDown);
}

//////////////////////////////////////////////////////////////////////////
// Moves the ship (attitude kept) to a distance behind the dropped target along its own forward axis, so the target is
// straight ahead, flying away from it at recedeMps in the reference frame
void FSOLChaseSmoke::PlaceShipBehindTarget(const double distanceM, const double recedeMps) const
{
    const TConstArrayView<FVector3d> targets = mCombat->GetTargetPositionsM();
    if (!targets.IsValidIndex(mTarget.Index))
    {
        return;
    }
    FSOLShipState state = mShips->GetState();
    const FVector3d targetM = state.PositionM
        + SOLRender::EclipticToUnreal(targets[mTarget.Index] - mShips->GetUniversePositionM());
    state.PositionM = targetM - state.Orientation.GetForwardVector() * distanceM;
    state.VelocityMps = mShips->GetReferenceVelocityMps() - state.Orientation.GetForwardVector() * recedeMps;
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Wakes the pilot with a full meter (ends any order; the pawn turns boost off) and moves the ship 20 km away, flying
// away at recedeMps
void FSOLChaseSmoke::ResetAndPlace(const double recedeMps) const
{
    mShips->ResetPilotState();
    PlaceShipBehindTarget(CHASESMOKE_DISTANCE_M, recedeMps);
}

//////////////////////////////////////////////////////////////////////////
// Wakes the pilot with a full meter and moves the ship, at rest in the Moon's frame, just outside the Moon chase's
// tail point on the side it is on now
void FSOLChaseSmoke::PlaceShipNearMoon() const
{
    const USOLBodyRegistrySubsystem* bodies = mShips->GetWorld()->GetSubsystem<USOLBodyRegistrySubsystem>();
    if (bodies == nullptr || mMoon.Index == INDEX_NONE)
    {
        return;
    }
    const FSOLBodyRegistry& registry = bodies->GetRegistry();
    mShips->ResetPilotState();
    FSOLShipState state = mShips->GetState();
    const FVector3d moonM = state.PositionM
        + SOLRender::EclipticToUnreal(registry.GetPositionM(mMoon.Index) - mShips->GetUniversePositionM());
    const FVector3d outward = (state.PositionM - moonM).GetSafeNormal();
    const double trailM = SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR * registry.GetRadiusM(mMoon.Index);
    state.PositionM = moonM + outward * (trailM + CHASESMOKE_MOON_OFFSET_M);
    state.VelocityMps = SOLRender::EclipticToUnreal(registry.GetVelocityMps(mMoon.Index));
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    mShips->SetState(state);
}

//////////////////////////////////////////////////////////////////////////
// Turns the Tab boost toggle to the wanted state through the pawn
void FSOLChaseSmoke::SetBoost(ASOLShipPawn& pawn, const bool bOn)
{
    if (pawn.IsBoostOn() != bOn)
    {
        pawn.HandleToggleBoost();
    }
}

//////////////////////////////////////////////////////////////////////////
// Applies a Chase Down safety margin through the settings (the smoke settings slot)
void FSOLChaseSmoke::ApplyMargin(const float margin) const
{
    USOLSettingsSubsystem* settings = USOLSettingsSubsystem::Get(mShips.Get());
    if (settings == nullptr)
    {
        return;
    }
    SOLSettingsModel::FSettingsValues values = settings->GetLiveValues();
    values.ChaseSafetyMargin = margin;
    settings->SetPendingValues(values);
    settings->ApplyPending();
}

//////////////////////////////////////////////////////////////////////////
// Fires bolts from the ship at the dropped target
void FSOLChaseSmoke::FireAtTarget() const
{
    const TConstArrayView<FVector3d> targets = mCombat->GetTargetPositionsM();
    FVector3d targetVelocityMps = FVector3d::ZeroVector;
    if (!targets.IsValidIndex(mTarget.Index) || !mCombat->GetTargetVelocityMps(mTarget.Index, targetVelocityMps))
    {
        return;
    }
    const FVector3d shipM = mShips->GetUniversePositionM();
    const FVector3d direction = (targets[mTarget.Index] - shipM).GetSafeNormal();
    for (int32 bolt = 0; bolt < CHASESMOKE_BOLTS_PER_FRAME; ++bolt)
    {
        mCombat->FireBolt(shipM + direction * CHASESMOKE_MUZZLE_M,
            targetVelocityMps + direction * CHASESMOKE_BOLT_SPEED_MPS);
    }
}

//////////////////////////////////////////////////////////////////////////
// Returns the current screen centre of the dropped target's icon, false when it is not drawn
bool FSOLChaseSmoke::FindTargetIcon(FVector2D& outCenterPx) const
{
    for (const FSOLIconHit& hit : mInteraction->GetIconHits())
    {
        if (hit.Object == mTarget)
        {
            outCenterPx = hit.CenterPx;
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Moves the cursor onto an action's sector of the open radial menu (the middle of the sector ring)
bool FSOLChaseSmoke::DragToAction(ASOLShipPawn& pawn, const ESOLAction action) const
{
    const FSOLRadialMenuState& radial = mInteraction->GetRadial();
    const FSOLActionList& actions = mInteraction->GetRadialActions();
    const float uiScale = mHud.IsValid() ? mHud->GetUiScale() : 1.0f;
    for (int32 index = 0; radial.bOpen && index < actions.Num; ++index)
    {
        if (actions.Entries[index].Action == action)
        {
            const double angle = SOLRadialMenu::SectorMidAngleRad(index, radial.Count);
            const double radiusPx = 0.5 * (SOL::Interact::RADIAL_INNER_PX + SOL::Interact::RADIAL_OUTER_PX) * uiScale;
            pawn.HandleFlightCursorMoved(radial.CenterPx + FVector2D(FMath::Cos(angle), FMath::Sin(angle)) * radiusPx);
            mInteraction->MovePointer(pawn.GetFlightCursorPx());
            return true;
        }
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's action at its start
void FSOLChaseSmoke::EnterStep(const int32 step, ASOLShipPawn& pawn)
{
    switch (static_cast<EChaseStep>(step))
    {
    case EChaseStep::Drop:
        pawn.HandleDropTarget();
        break;
    case EChaseStep::RadialOpen:
    {
        // Fire on the target's icon opens the radial menu; the pointer moves onto Chase Down and the HUD highlights it
        FVector2D iconPx = FVector2D::ZeroVector;
        FindTargetIcon(iconPx);
        pawn.HandleFlightCursorMoved(iconPx);
        mInteraction->UpdateHover(pawn.GetFlightCursorPx(), true);
        pawn.HandleFire(true);
        DragToAction(pawn, ESOLAction::ChaseDown);
        TakeUiShot(TEXT("Radial"));
        break;
    }
    case EChaseStep::Chase:
        BeginRun();
        pawn.HandleFire(false);
        break;
    case EChaseStep::Hold:
        mRun.MaxTargetDistanceM = 0.0;
        mRun.bMeasureDistance = true;
        break;
    case EChaseStep::WCancel:
        pawn.HandleThrust(FVector3d(1.0, 0.0, 0.0));
        break;
    case EChaseStep::EscEngage:
    case EChaseStep::MoveEngage:
        ChaseTarget();
        break;
    case EChaseStep::EscFirst:
    case EChaseStep::EscSecond:
        pawn.HandlePause();
        break;
    case EChaseStep::EscResume:
        if (USOLMenuSubsystem* menu = pawn.GetWorld()->GetSubsystem<USOLMenuSubsystem>())
        {
            menu->RequestResume();
        }
        break;
    case EChaseStep::MoveTo:
        mInteraction->Execute(mMoon, ESOLAction::MoveTo);
        break;
    case EChaseStep::MoonChase:
        BeginRun();
        mInteraction->Execute(mMoon, ESOLAction::ChaseDown);
        break;
    case EChaseStep::MoonCancel:
        pawn.HandleToggleAssist();
        break;
    case EChaseStep::MarginPrep:
        ApplyMargin(CHASESMOKE_MARGIN_TEST);
        ResetAndPlace(CHASESMOKE_RECEDE_MPS);
        break;
    case EChaseStep::DefaultMarginPrep:
        ApplyMargin(SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT);
        ResetAndPlace(CHASESMOKE_RECEDE_MPS);
        break;
    case EChaseStep::StandoffPrep:
    case EChaseStep::WarpPrep:
    case EChaseStep::BoostOffPrep:
        ResetAndPlace(0.0);
        break;
    case EChaseStep::MoonWarpPrep:
        PlaceShipNearMoon();
        break;
    case EChaseStep::MoonWarp:
        SetBoost(pawn, true);
        BeginRun();
        mInteraction->Execute(mMoon, ESOLAction::ChaseDown);
        pawn.HandleWarpUp();
        pawn.HandleWarpUp();
        pawn.HandleWarpUp();
        break;
    case EChaseStep::Margin:
    case EChaseStep::DefaultMargin:
        SetBoost(pawn, true);
        ChaseTarget();
        break;
    case EChaseStep::Standoff:
    {
        SetBoost(pawn, true);
        const TConstArrayView<uint32> serials = mCombat->GetTargetSpawnSerials();
        mBigTarget->Bind(mCombat.Get(), mTarget.Index, serials.IsValidIndex(mTarget.Index) ? serials[mTarget.Index] : 0,
            CHASESMOKE_BIG_RADIUS_M);
        FSOLChaseOrder order;
        order.Targetable = TWeakInterfacePtr<ISOLTargetable>(mBigTarget.Get());
        BeginRun();
        mShips->EngageChase(order);
        break;
    }
    case EChaseStep::Warp100:
        SetBoost(pawn, true);
        ChaseTarget();
        pawn.HandleWarpUp();
        pawn.HandleWarpUp();
        break;
    case EChaseStep::Warp1000:
        mRun.MaxTargetDistanceM = 0.0;
        mRun.bMeasureDistance = false;
        mRun.FirstBurstS = -1.0;
        pawn.HandleWarpUp();
        break;
    case EChaseStep::BoostOff:
        SetBoost(pawn, false);
        ChaseTarget();
        break;
    case EChaseStep::BoostOn:
        mRun.MaxLimitG = 0.0;
        mRun.MaxLoadG = 0.0;
        pawn.HandleToggleBoost();
        break;
    case EChaseStep::Destroy:
        mDestroyedBefore = mCombat->GetTargetsDestroyedCount();
        break;
    case EChaseStep::LostNote:
        TakeUiShot(TEXT("TargetLost"));
        break;
    case EChaseStep::JumpEngage:
        mInteraction->Execute(mMoon, ESOLAction::ChaseDown);
        break;
    case EChaseStep::JumpTo:
    {
        // Jump To replaces the chase (SDD 32 section 3.7): it ends in ASSISTED as the warp starts
        mInteraction->Execute(mMoon, ESOLAction::JumpTo);
        const USOLJumpSubsystem* jump = pawn.GetWorld()->GetSubsystem<USOLJumpSubsystem>();
        const bool bWarping = jump != nullptr && jump->IsWarping();
        const ESOLFlightMode mode = mShips->GetFlightMode();
        Check(TEXT("z-jump-ends-chase"), bWarping && !mShips->IsChasing() && mode == ESOLFlightMode::Assisted,
            FString::Printf(TEXT("Jump To the Moon: warp %s, chasing %s, %s"), bWarping ? TEXT("started")
            : TEXT("NOT started"), mShips->IsChasing() ? TEXT("YES") : TEXT("no"), ModeName(mode)));
        break;
    }
    case EChaseStep::ProfileWarpChase:
        if (mProfile)
        {
            PlaceShipNearMoon();
            SetBoost(pawn, true);
            BeginRun();
            mInteraction->Execute(mMoon, ESOLAction::ChaseDown);
            for (int32 index = 0; index < CHASESMOKE_PROFILE_WARP_STEPS; ++index)
            {
                pawn.HandleWarpUp();
            }
        }
        break;
    case EChaseStep::ProfileWarpMove:
        if (mProfile)
        {
            // The same warp, the plain autopilot on a trip long enough not to arrive during the capture
            mShips->DisengageChase();
            FSOLObjectRef neptune;
            neptune.Kind = ESOLObjectKind::Body;
            if (const USOLBodyRegistrySubsystem* bodies = pawn.GetWorld()->GetSubsystem<USOLBodyRegistrySubsystem>())
            {
                neptune.Index = bodies->GetRegistry().FindByName(FName(SOL::BodyNames::NEPTUNE));
            }
            mInteraction->Execute(neptune, ESOLAction::MoveTo);
        }
        break;
    case EChaseStep::Quit:
        UE_LOG(LogSOL, Log, TEXT("SmokeChase: %d/%d checks passed"), mChecksPassed, mChecksRun);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's per-frame work; returns true once the step is done
bool FSOLChaseSmoke::TickStep(const int32 step, ASOLShipPawn& pawn)
{
    const double t = mStepTimeS;
    switch (static_cast<EChaseStep>(step))
    {
    case EChaseStep::Setup:
        return t >= CHASESMOKE_SETUP_S;
    case EChaseStep::Place:
        return t >= CHASESMOKE_PLACE_S;
    case EChaseStep::RadialOpen:
        return t >= CHASESMOKE_SHOT_S;
    case EChaseStep::Chase:
    {
        // The engage is checked on the first frame after the release
        if (mStepFrames == 1)
        {
            const UWorld* world = pawn.GetWorld();
            const USOLTargetingSubsystem* targeting = world->GetSubsystem<USOLTargetingSubsystem>();
            const int32 selected = targeting != nullptr ? targeting->GetSelectedIndex() : INDEX_NONE;
            Check(TEXT("c-engage"), mShips->IsChasing() && mShips->GetFlightMode() == ESOLFlightMode::Auto
                && mInteraction->GetLastExecutedAction() == ESOLAction::ChaseDown
                && mInteraction->GetLastExecutedObject() == mTarget
                && mInteraction->GetChasedDroppedTargetSlot() == mTarget.Index && !mInteraction->IsRadialOpen(),
                FString::Printf(TEXT("radial release: %s, chasing %s (slot %d of %d), last action %s, menu %s"),
                ModeName(mShips->GetFlightMode()), mShips->IsChasing() ? TEXT("yes") : TEXT("NO"),
                mInteraction->GetChasedDroppedTargetSlot(), mTarget.Index,
                SOLActionTable::ActionLabel(mInteraction->GetLastExecutedAction()), mInteraction->IsRadialOpen()
                ? TEXT("OPEN") : TEXT("closed")));
            Check(TEXT("d-target-kept"), selected == mSelectedAtStart && mSelectedAtStart != INDEX_NONE,
                FString::Printf(TEXT("selected candidate %d before, %d after the order"), mSelectedAtStart, selected));
        }
        TickProfile(TEXT("20 km chase"), pawn.GetWorld());
        const double timeoutS = FMath::Max(30.0, CHASESMOKE_INTERCEPT_SLACK * FMath::Min(mRun.PredictedS, 1.0e5) + 10.0);
        return (mRun.InterceptS >= 0.0 && t >= CHASESMOKE_STEP_S) || t >= timeoutS || !mShips->IsChasing();
    }
    case EChaseStep::Hold:
        return t >= CHASESMOKE_HOLD_S;
    case EChaseStep::MoonChase:
        return TickProfile(TEXT("Moon chase"), pawn.GetWorld()) && t >= CHASESMOKE_MOON_S;
    case EChaseStep::Margin:
    case EChaseStep::DefaultMargin:
        return mHoldStreakS >= CHASESMOKE_HOLD_SETTLE_S || t >= CHASESMOKE_TAIL_TIMEOUT_S || !mShips->IsChasing();
    case EChaseStep::Standoff:
        return mHoldStreakS >= CHASESMOKE_STANDOFF_SETTLE_S || t >= CHASESMOKE_TAIL_TIMEOUT_S || !mShips->IsChasing();
    case EChaseStep::Warp100:
    case EChaseStep::Warp1000:
        mRun.bMeasureDistance = t >= CHASESMOKE_WARP_SETTLE_S;
        return t >= CHASESMOKE_WARP_S;
    case EChaseStep::BoostOff:
        return t >= CHASESMOKE_BOOST_OFF_S;
    case EChaseStep::BoostOn:
        return t >= CHASESMOKE_BOOST_ON_S;
    case EChaseStep::DestroyWait:
        return mHoldStreakS >= CHASESMOKE_HOLD_SETTLE_S || t >= CHASESMOKE_TAIL_TIMEOUT_S || !mShips->IsChasing();
    case EChaseStep::Destroy:
        if (mShips->IsChasing())
        {
            FireAtTarget();
        }
        return (!mShips->IsChasing() && t >= CHASESMOKE_STEP_S) || t >= CHASESMOKE_DESTROY_TIMEOUT_S;
    case EChaseStep::LostNote:
        return t >= CHASESMOKE_SHOT_S;
    case EChaseStep::MoonWarp:
        // Over the last seconds: the hold distance and how often the status reads HOLD
        if (t >= CHASESMOKE_MOON_WARP_SETTLE_S && mShips->IsChasing())
        {
            const FSOLChaseStatus status = mShips->GetChaseStatus();
            mRun.bMeasureDistance = true;
            ++mMoonFrames;
            mMoonHoldFrames += status.bHolding ? 1 : 0;
            mMoonMaxAimM = FMath::Max(mMoonMaxAimM, status.AimDistanceM);
        }
        return t >= CHASESMOKE_MOON_WARP_S;
    case EChaseStep::JumpTo:
    {
        const USOLJumpSubsystem* jump = pawn.GetWorld()->GetSubsystem<USOLJumpSubsystem>();
        return ((jump == nullptr || !jump->IsWarping()) && t >= CHASESMOKE_STEP_S) || t >= CHASESMOKE_JUMP_TIMEOUT_S;
    }
    case EChaseStep::ProfileWarpChase:
        return !mProfile || (t >= CHASESMOKE_PROFILE_SETTLE_S
            && TickProfile(TEXT("Moon chase at 1 d/s"), pawn.GetWorld()));
    case EChaseStep::ProfileWarpMove:
        return !mProfile || (t >= CHASESMOKE_PROFILE_SETTLE_S
            && TickProfile(TEXT("Move To Neptune at 1 d/s"), pawn.GetWorld()));
    case EChaseStep::Quit:
        return t >= CHASESMOKE_QUIT_S;
    default:
        return t >= CHASESMOKE_STEP_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs the step's checks at its end
void FSOLChaseSmoke::ExitStep(const int32 step, ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const USOLTargetingSubsystem* targeting = world->GetSubsystem<USOLTargetingSubsystem>();
    const USOLMenuSubsystem* menu = world->GetSubsystem<USOLMenuSubsystem>();
    const FSOLChaseStatus status = mShips->GetChaseStatus();
    const ESOLFlightMode mode = mShips->GetFlightMode();
    const bool bChasing = mShips->IsChasing();
    switch (static_cast<EChaseStep>(step))
    {
    case EChaseStep::Setup:
    {
        // At rest in the reference frame (the target is dropped at rest in it), the Moon selected, boost on
        mShips->DisengageAutopilot();
        FSOLShipState state = mShips->GetState();
        state.VelocityMps = mShips->GetReferenceVelocityMps();
        state.AngularVelocityRadPerS = FVector3d::ZeroVector;
        mShips->SetState(state);
        mInteraction->Execute(mMoon, ESOLAction::Target);
        mSelectedAtStart = targeting != nullptr ? targeting->GetSelectedIndex() : INDEX_NONE;
        SetBoost(pawn, true);
        break;
    }
    case EChaseStep::Drop:
        mTarget.Kind = ESOLObjectKind::DroppedTarget;
        mTarget.Index = mCombat->GetLastDroppedTargetSlot();
        PlaceShipBehindTarget(CHASESMOKE_DISTANCE_M, 0.0);
        break;
    case EChaseStep::Place:
    {
        FVector2D iconPx = FVector2D::ZeroVector;
        const bool bIcon = FindTargetIcon(iconPx);
        const TConstArrayView<FVector3d> targets = mCombat->GetTargetPositionsM();
        const double distanceM = targets.IsValidIndex(mTarget.Index)
            ? FVector3d::Dist(targets[mTarget.Index], mShips->GetUniversePositionM()) : 0.0;
        Check(TEXT("a-setup"), mTarget.Index != INDEX_NONE && bIcon && pawn.IsBoostOn() && mSelectedAtStart == mMoon.Index
            && FMath::Abs(distanceM - CHASESMOKE_DISTANCE_M) < 0.05 * CHASESMOKE_DISTANCE_M, FString::Printf(TEXT(
            "target slot %d, icon %s at (%.0f, %.0f), %.2f km away, boost %s, selected %d (Moon %d)"), mTarget.Index,
            bIcon ? TEXT("drawn") : TEXT("NOT drawn"), iconPx.X, iconPx.Y, distanceM / 1000.0, pawn.IsBoostOn()
            ? TEXT("on") : TEXT("OFF"), mSelectedAtStart, mMoon.Index));
        break;
    }
    case EChaseStep::RadialOpen:
    {
        const FSOLRadialMenuState& radial = mInteraction->GetRadial();
        const FSOLActionList& actions = mInteraction->GetRadialActions();
        int32 chaseSector = INDEX_NONE;
        for (int32 index = 0; index < actions.Num; ++index)
        {
            chaseSector = actions.Entries[index].Action == ESOLAction::ChaseDown ? index : chaseSector;
        }
        Check(TEXT("b-radial"), radial.bOpen && radial.Object == mTarget && chaseSector != INDEX_NONE
            && radial.Highlighted == chaseSector, FString::Printf(TEXT("radial %s on %d:%d, %d sectors, highlighted %d "
            "(Chase Down is %d)"), radial.bOpen ? TEXT("open") : TEXT("CLOSED"), static_cast<int32>(radial.Object.Kind),
            radial.Object.Index, radial.Count, radial.Highlighted, chaseSector));
        break;
    }
    case EChaseStep::Chase:
        Check(TEXT("e-burst"), mRun.FirstBurstS >= 0.0 && mRun.FirstBurstS <= CHASESMOKE_BURST_WITHIN_S,
            FString::Printf(TEXT("first load above %.2f G after %.2f s (peak load %.2f G, peak limit %.2f G)"),
            mSustainedG + CHASESMOKE_BURST_MARGIN_G, mRun.FirstBurstS, mRun.MaxLoadG, mRun.MaxLimitG));
        Check(TEXT("f-intercept"), mRun.InterceptS >= 0.0 && mRun.InterceptS <= CHASESMOKE_INTERCEPT_SLACK
            * mRun.PredictedS, FString::Printf(TEXT("on the tail after %.2f s, predicted %.2f s (limit %.2f s); min "
            "meter %.3f (floor %.2f)"), mRun.InterceptS, mRun.PredictedS, CHASESMOKE_INTERCEPT_SLACK * mRun.PredictedS,
            mRun.MinMeter, status.FloorMeter));
        Check(TEXT("g-margin-25"), mRun.MinMeter >= SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT - CHASESMOKE_FLOOR_EPS
            && FMath::IsNearlyEqual(status.FloorMeter, SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT, 1.0e-4),
            FString::Printf(TEXT("default margin: floor %.3f, min meter %.3f"), status.FloorMeter, mRun.MinMeter));
        break;
    case EChaseStep::Hold:
        Check(TEXT("h-hold"), bChasing && mRun.MaxTargetDistanceM <= CHASESMOKE_HOLD_FACTOR * status.TrailM
            && mShips->GetAutopilotArrivalCount() == mRun.ArrivalsAtEngage && mode == ESOLFlightMode::Auto,
            FString::Printf(TEXT("10 s on the tail: max %.1f m from the target (trail %.0f m, limit %.0f m), %s, "
            "arrivals %u -> %u"), mRun.MaxTargetDistanceM, status.TrailM, CHASESMOKE_HOLD_FACTOR * status.TrailM,
            ModeName(mode), mRun.ArrivalsAtEngage, mShips->GetAutopilotArrivalCount()));
        break;
    case EChaseStep::WCancel:
        pawn.HandleThrust(FVector3d::ZeroVector);
        Check(TEXT("i-w-cancel"), !bChasing && mode == ESOLFlightMode::Assisted, FString::Printf(TEXT("W: %s, "
            "chasing %s"), ModeName(mode), bChasing ? TEXT("YES") : TEXT("no")));
        break;
    case EChaseStep::EscEngage:
    case EChaseStep::MoveEngage:
        if (!bChasing)
        {
            Check(TEXT("engage"), false, TEXT("Chase Down did not start"));
        }
        break;
    case EChaseStep::EscFirst:
        Check(TEXT("j-esc"), !bChasing && mode == ESOLFlightMode::Assisted && menu != nullptr && !menu->IsMenuShowing(),
            FString::Printf(TEXT("Esc: %s, chasing %s, pause menu %s"), ModeName(mode), bChasing ? TEXT("YES")
            : TEXT("no"), menu != nullptr && menu->IsMenuShowing() ? TEXT("SHOWN") : TEXT("not shown")));
        break;
    case EChaseStep::EscSecond:
        Check(TEXT("k-esc-second"), menu != nullptr && menu->IsMenuShowing(), FString::Printf(TEXT("second Esc: "
            "pause menu %s"), menu != nullptr && menu->IsMenuShowing() ? TEXT("shown") : TEXT("NOT shown")));
        break;
    case EChaseStep::EscResume:
        Check(TEXT("l-resume"), menu != nullptr && !menu->IsMenuShowing(), FString::Printf(TEXT("resume: pause menu "
            "%s"), menu != nullptr && menu->IsMenuShowing() ? TEXT("STILL SHOWN") : TEXT("closed")));
        break;
    case EChaseStep::MoveTo:
        Check(TEXT("m-move-to"), !bChasing && mode == ESOLFlightMode::Auto && mShips->GetAutopilotBodyIndex()
            == mMoon.Index, FString::Printf(TEXT("Move To the Moon: %s, chasing %s, autopilot body %d (Moon %d)"),
            ModeName(mode), bChasing ? TEXT("YES") : TEXT("no"), mShips->GetAutopilotBodyIndex(), mMoon.Index));
        mShips->DisengageAutopilot();
        break;
    case EChaseStep::MoonChase:
    {
        const USOLBodyRegistrySubsystem* bodies = world->GetSubsystem<USOLBodyRegistrySubsystem>();
        const double moonRadiusM = bodies != nullptr ? bodies->GetRegistry().GetRadiusM(mMoon.Index) : 0.0;
        const int32 selected = targeting != nullptr ? targeting->GetSelectedIndex() : INDEX_NONE;
        Check(TEXT("n-moon"), bChasing && mode == ESOLFlightMode::Auto && mShips->GetChaseOrder().BodyIndex
            == mMoon.Index && status.TrailM >= SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR * moonRadiusM - 1.0
            && selected == mSelectedAtStart, FString::Printf(TEXT("Moon chase: %s, chasing body %d, trail %.0f km "
            "(3 x radius %.0f km), %s %.2f G, selected %d"), ModeName(mode), mShips->GetChaseOrder().BodyIndex,
            status.TrailM / 1000.0, SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR * moonRadiusM / 1000.0,
            ActivityName(status.Activity), status.LimitG, selected));
        break;
    }
    case EChaseStep::MoonCancel:
        Check(TEXT("o-c-cancel"), !bChasing && mode == ESOLFlightMode::Assisted, FString::Printf(TEXT("C: %s, "
            "chasing %s"), ModeName(mode), bChasing ? TEXT("YES") : TEXT("no")));
        break;
    case EChaseStep::Margin:
    case EChaseStep::DefaultMargin:
    {
        // Receding at the start, so the bursts run the meter down to the floor and cool off
        const bool b30 = static_cast<EChaseStep>(step) == EChaseStep::Margin;
        const double floor = b30 ? CHASESMOKE_MARGIN_TEST : SOL::Settings::CHASE_SAFETY_MARGIN_DEFAULT;
        Check(b30 ? TEXT("p-margin-30") : TEXT("p-margin-25"), mRun.InterceptS >= 0.0 && mRun.MinMeter >= floor
            - CHASESMOKE_FLOOR_EPS && mRun.MinMeter <= floor + CHASESMOKE_FLOOR_REACHED
            && FMath::IsNearlyEqual(status.FloorMeter, floor, 1.0e-4), FString::Printf(TEXT("%.0f%% margin, receding "
            "%.0f m/s at the start: floor %.3f, min meter %.3f, on the tail after %.1f s (predicted %.1f s), peak load "
            "%.2f G, peak limit %.2f G"), 100.0 * floor, CHASESMOKE_RECEDE_MPS, status.FloorMeter, mRun.MinMeter,
            mRun.InterceptS, mRun.PredictedS, mRun.MaxLoadG, mRun.MaxLimitG));
        mShips->DisengageChase();
        break;
    }
    case EChaseStep::Standoff:
    {
        const double wantM = SOL::Chase::TARGET_TRAIL_RADIUS_FACTOR * CHASESMOKE_BIG_RADIUS_M;
        Check(TEXT("q-standoff"), bChasing && status.bHolding && FMath::IsNearlyEqual(status.TrailM, wantM, 1.0)
            && FMath::Abs(status.TargetDistanceM - wantM) <= CHASESMOKE_STANDOFF_TOLERANCE * wantM, FString::Printf(
            TEXT("radius %.0f m: trail %.0f m, holding %s at %.1f m from the centre (want %.0f m), on the tail after "
            "%.1f s"), CHASESMOKE_BIG_RADIUS_M, status.TrailM, status.bHolding ? TEXT("yes") : TEXT("NO"),
            status.TargetDistanceM, wantM, mRun.InterceptS));
        mShips->DisengageChase();
        break;
    }
    case EChaseStep::Warp100:
    case EChaseStep::Warp1000:
    {
        const bool b100 = static_cast<EChaseStep>(step) == EChaseStep::Warp100;
        const USOLSimClockSubsystem* clock = world->GetSubsystem<USOLSimClockSubsystem>();
        const double warp = clock != nullptr ? clock->GetClock().GetWarpFactor() : 0.0;
        Check(b100 ? TEXT("r-warp-100") : TEXT("s-warp-1000"), bChasing && mFloorViolations == 0
            && mRun.MaxTargetDistanceM <= CHASESMOKE_HOLD_FACTOR * status.TrailM && mRun.MaxTargetDistanceM > 0.0,
            FString::Printf(TEXT("%.0fx: chasing %s, max %.1f m from the target over the last %.0f s (trail %.0f m), "
            "now %s aim %.1f m closing %.2f m/s, min meter %.3f, floor dips %d, peak load %.2f G"), warp, bChasing
            ? TEXT("yes") : TEXT("NO"), mRun.MaxTargetDistanceM, CHASESMOKE_WARP_S - CHASESMOKE_WARP_SETTLE_S,
            status.TrailM, ActivityName(status.Activity), status.AimDistanceM, status.ClosingSpeedMps, mRun.MinMeter,
            mFloorViolations, mRun.MaxLoadG));
        if (!b100)
        {
            pawn.HandleWarpReset();
            mShips->DisengageChase();
        }
        break;
    }
    case EChaseStep::BoostOff:
        Check(TEXT("t-boost-off"), bChasing && mRun.MaxLimitG <= mUnboostedCapG + CHASESMOKE_CAP_EPS_G
            && mRun.MaxLoadG <= mUnboostedCapG + CHASESMOKE_CAP_EPS_G && mRun.MaxLimitG > mSustainedG
            && !pawn.IsBoostOn(), FString::Printf(TEXT("boost off: peak limit %.2f G, peak load %.2f G (cap %.2f G)"),
            mRun.MaxLimitG, mRun.MaxLoadG, mUnboostedCapG));
        break;
    case EChaseStep::BoostOn:
        Check(TEXT("u-boost-on"), bChasing && pawn.IsBoostOn() && mRun.MaxLimitG > mUnboostedCapG
            + CHASESMOKE_BOOST_ABOVE_G, FString::Printf(TEXT("boost on: chasing %s, peak limit %.2f G, peak load %.2f G "
            "(unboosted cap %.2f G)"), bChasing ? TEXT("yes") : TEXT("NO"), mRun.MaxLimitG, mRun.MaxLoadG,
            mUnboostedCapG));
        break;
    case EChaseStep::DestroyWait:
        if (!status.bHolding)
        {
            Check(TEXT("tail"), false, TEXT("not on the tail before the target was shot"));
        }
        break;
    case EChaseStep::Destroy:
        Check(TEXT("v-target-lost"), mCombat->GetTargetsDestroyedCount() == mDestroyedBefore + 1 && !bChasing
            && mode == ESOLFlightMode::Assisted && status.bTargetLost && mInteraction->IsChaseTargetLostNoteShown(),
            FString::Printf(TEXT("destroyed %d -> %d; %s, chasing %s, target lost %s, HUD note %s"), mDestroyedBefore,
            mCombat->GetTargetsDestroyedCount(), ModeName(mode), bChasing ? TEXT("YES") : TEXT("no"), status.bTargetLost
            ? TEXT("yes") : TEXT("NO"), mInteraction->IsChaseTargetLostNoteShown() ? TEXT("shown") : TEXT("NOT shown")));
        break;
    case EChaseStep::MoonWarp:
        Check(TEXT("x-moon-1000"), bChasing && mFloorViolations == 0 && mMoonFrames > 0
            && mRun.MaxTargetDistanceM <= CHASESMOKE_HOLD_FACTOR * status.TrailM, FString::Printf(TEXT("Moon at 1000x: "
            "chasing %s, max %.0f km from the centre over the last %.0f s (trail %.0f km), max aim %.1f m, HOLD on %d of "
            "%d frames, now %s %.2f G aim %.1f m closing %.2f m/s, min meter %.3f, peak load %.2f G"), bChasing
            ? TEXT("yes") : TEXT("NO"), mRun.MaxTargetDistanceM / 1000.0, CHASESMOKE_MOON_WARP_S
            - CHASESMOKE_MOON_WARP_SETTLE_S, status.TrailM / 1000.0, mMoonMaxAimM, mMoonHoldFrames, mMoonFrames,
            ActivityName(status.Activity), status.LimitG, status.AimDistanceM, status.ClosingSpeedMps, mRun.MinMeter,
            mRun.MaxLoadG));
        pawn.HandleWarpReset();
        mShips->DisengageChase();
        Check(TEXT("w-floor"), mFloorViolations == 0 && mFloorFrames > 0, FString::Printf(TEXT("meter checked on %d "
            "chase frames: %d below the floor (worst %.4f under)"), mFloorFrames, mFloorViolations,
            mWorstFloorDeficit));
        break;
    case EChaseStep::JumpEngage:
        if (!bChasing)
        {
            Check(TEXT("engage"), false, TEXT("Chase Down did not start"));
        }
        break;
    case EChaseStep::ProfileWarpMove:
        if (mProfile)
        {
            pawn.HandleWarpReset();
            mShips->DisengageAutopilot();
        }
        break;
    case EChaseStep::Quit:
        pawn.SetFlightCursorScripted(false);
        UKismetSystemLibrary::QuitGame(world, nullptr, EQuitPreference::Quit, false);
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one cost capture from the first frame it is called; true once both dumps are done, or at once without the flag
bool FSOLChaseSmoke::TickProfile(const TCHAR* label, UWorld* world)
{
    if (!mProfile || GEngine == nullptr)
    {
        return true;
    }

    // Each mark logs the window so far (frames, real seconds, warp, chase state); the dumps print on the stats thread
    const auto logMark = [this, label, world](const TCHAR* mark)
    {
        const USOLSimClockSubsystem* clock = world->GetSubsystem<USOLSimClockSubsystem>();
        const FSOLChaseStatus status = mShips->GetChaseStatus();
        UE_LOG(LogSOL, Log, TEXT("SmokeChase PROFILE %s %s: frame %d, %.2f s, warp %.0fx, %s, chasing %s %s, aim %.0f m"),
            label, mark, mProfileFrame, mClockS - mProfileStartS, clock != nullptr ? clock->GetClock().GetWarpFactor()
            : 0.0, ModeName(mShips->GetFlightMode()), mShips->IsChasing() ? TEXT("yes") : TEXT("no"),
            ActivityName(status.Activity), status.AimDistanceM);
    };
    if (mProfileFrame == 0)
    {
        mProfileStartS = mClockS;
        logMark(TEXT("sum start"));
        GEngine->Exec(world, CHASESMOKE_PROFILE_SUM_CMD);
    }
    else if (mProfileFrame == CHASESMOKE_PROFILE_FRAMES + CHASESMOKE_PROFILE_TAIL_FRAMES)
    {
        logMark(TEXT("max start"));
        GEngine->Exec(world, CHASESMOKE_PROFILE_MAX_CMD);
    }
    else if (mProfileFrame == 2 * (CHASESMOKE_PROFILE_FRAMES + CHASESMOKE_PROFILE_TAIL_FRAMES))
    {
        logMark(TEXT("done"));
    }
    ++mProfileFrame;
    return mProfileFrame > 2 * (CHASESMOKE_PROFILE_FRAMES + CHASESMOKE_PROFILE_TAIL_FRAMES);
}

//////////////////////////////////////////////////////////////////////////
// Requests a window screenshot with the UI to Saved/Screenshots (at most one per frame)
void FSOLChaseSmoke::TakeUiShot(const TCHAR* name)
{
    if (mLastShotFrame == GFrameCounter)
    {
        return;
    }
    mLastShotFrame = GFrameCounter;
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir()
        / FString::Printf(TEXT("ChaseSmoke_%s.png"), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeChase: screenshot with UI %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLChaseSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeChase CHECK %-16s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"), *detail);
}
