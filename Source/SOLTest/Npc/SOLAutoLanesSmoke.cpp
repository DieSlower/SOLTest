/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLAutoLanesSmoke.h"

#include "Flight/SOLNewtonianFlight.h"
#include "Game/SOLGameMode.h"
#include "Menu/SOLMenuText.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcVisuals.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Traffic/SOLLaneTrafficSubsystem.h"
#include "UI/SOLFlightHud.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"
#include "Universe/SOLSimClockSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "Widgets/SWindow.h"

namespace
{
    // Phase durations and limits (real seconds)
    constexpr double AUTOLANES_WAIT_TIMEOUT_S = 240.0;       // Population spawn and upload
    constexpr double AUTOLANES_READY_SETTLE_S = 2.0;         // The tunnels settle after the population
    constexpr double AUTOLANES_CASE_TIMEOUT_S = 180.0;       // A case that never ends fails
    constexpr double AUTOLANES_SHOT_S = 0.8;                 // A screenshot is written before the next case
    constexpr int32 AUTOLANES_HOLD_SETTLE_FRAMES = 30;       // Hold frames before the held speed is sampled
    constexpr int32 AUTOLANES_TAG_FRAMES = 3;                // The HUD draws the tag on a later frame

    // Script values
    constexpr int32 AUTOLANES_SHIPS = 250000;                // Population (any count gives every lane its table row)
    constexpr int32 AUTOLANES_WARP_HOUR = 4;                 // 3600x (SOLSimClock's factor table)
    constexpr int32 AUTOLANES_WARP_DAY = 5;                  // 86400x
    constexpr double AUTOLANES_HOLD_SIM_S = 1.0e4;           // The hold case runs at least this long (sim seconds)...
    constexpr double AUTOLANES_HOLD_MAX_SIM_S = 3.0e5;       // ...until settled on the track, at most this long
    constexpr double AUTOLANES_WAR_MAX_SIM_S = 6.0e5;        // The War power case must arrive within this
    constexpr double AUTOLANES_STOP_MAX_SIM_S = 8.0e5;       // The mid-lane stop must arrive within this
    constexpr double AUTOLANES_STOP_WAYPOINT_BEFORE_M = 2.0e9;   // The stop waypoint, this far before Mars's ring
    constexpr double AUTOLANES_STOP_START_BEFORE_M = 8.0e9;      // ...the ship starts holding this far before it
    constexpr double AUTOLANES_OVERSHOOT_BOUND_M = 1.0e6;    // The stop passes the waypoint by at most this
    constexpr double AUTOLANES_MIN_FPS = 20.0;               // One frame of sim travel allowed at this frame rate
    constexpr double AUTOLANES_APPROACH_MAX_SIM_S = 6.0e5;   // The approach must merge within this
    constexpr double AUTOLANES_LEG_MAX_SIM_S = 1.5e6;        // The whole leg must arrive within this
    constexpr double AUTOLANES_LEG_SLOW_FRACTION = 0.9;      // The leg case drops to 3600x past this leg fraction
    constexpr double AUTOLANES_TRACK_TOLERANCE_RHO = 0.1;    // The hold settles this close to the track's radius
    constexpr double AUTOLANES_LEFT_MIN_FRACTION = 0.95;     // The leg is left near Mars (its braking room)
    constexpr float AUTOLANES_MAX_WINDOW_PX = 16384.0f;      // Lifted window size limit for a -ResX / -ResY view

    //////////////////////////////////////////////////////////////////////////
    // Sets an integer console variable (sol.SpaceDust)
    void AutoLanesSetCvar(const TCHAR* name, const int32 value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Sets the -ResX / -ResY view past the desktop size with r.SetRes; nothing without both
    void AutoLanesApplyCommandLineView(UWorld* world)
    {
        int32 width = 0;
        int32 height = 0;
        const TSharedPtr<SWindow> window = GEngine != nullptr && GEngine->GameViewport != nullptr
            ? GEngine->GameViewport->GetWindow() : nullptr;
        if (!FParse::Value(FCommandLine::Get(), TEXT("ResX="), width) || !FParse::Value(FCommandLine::Get(),
            TEXT("ResY="), height) || width <= 0 || height <= 0 || !window.IsValid())
        {
            return;
        }
        window->SetSizeLimits(FWindowSizeLimits().SetMaxWidth(AUTOLANES_MAX_WINDOW_PX)
            .SetMaxHeight(AUTOLANES_MAX_WINDOW_PX));
        GEngine->Exec(world, *FString::Printf(TEXT("r.SetRes %dx%dw"), width, height));
        UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes: view at %dx%d (window size limit lifted)"), width, height);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane mode's name for the log
    const TCHAR* AutoLanesModeName(const ESOLLaneAutoMode mode)
    {
        return mode == ESOLLaneAutoMode::Hold ? TEXT("Hold") : mode == ESOLLaneAutoMode::Approach ? TEXT("Approach")
            : TEXT("None");
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a case's name (checks and screenshots)
    const TCHAR* AutoLanesCaseName(const int32 index)
    {
        static const TCHAR* NAMES[] = { TEXT("approach"), TEXT("hold"), TEXT("leg"), TEXT("midstop"),
            TEXT("warpower") };
        return index >= 0 && index < static_cast<int32>(UE_ARRAY_COUNT(NAMES)) ? NAMES[index] : TEXT("?");
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and actors, sets up the overrides, the view and the population
void FSOLAutoLanesSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const ASOLGameMode* gameMode = world->GetAuthGameMode<ASOLGameMode>();
    const APlayerController* controller = pawn.GetController<APlayerController>();
    mVisuals = gameMode != nullptr ? gameMode->GetNpcVisuals() : nullptr;
    mOrbitLines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
    mHud = controller != nullptr ? Cast<ASOLFlightHud>(controller->GetHUD()) : nullptr;
    mNpcs = world->GetSubsystem<USOLNpcSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mLaneTraffic = world->GetSubsystem<USOLLaneTrafficSubsystem>();
    mSimClock = world->GetSubsystem<USOLSimClockSubsystem>();
    UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes: starting (HUD %s, lane table %s)"),
        mHud.IsValid() ? *mHud->GetName() : TEXT("none"), mLaneTraffic.IsValid() ? TEXT("yes") : TEXT("none"));

    // Overrides: the lanes and tunnels leave the player's settings alone; orbit lines and space dust off
    if (mVisuals.IsValid())
    {
        mVisuals->SetLaneVerificationOverride(true);
        mVisuals->SetTunnelVerificationOverride(true, false);
    }
    if (mOrbitLines.IsValid())
    {
        mOrbitLines->BeginVerificationOverride();
        mOrbitLines->SetLinesEnabled(false);
    }
    if (mLaneTraffic.IsValid())
    {
        mLaneTraffic->SetReadoutVerificationOverride(true);
    }
    AutoLanesSetCvar(SOL::SpaceDust::CVAR_ENABLED, 0);
    AutoLanesApplyCommandLineView(world);
    if (mNpcs.IsValid() && mNpcs->GetRequestedCount() == 0)
    {
        mNpcs->RequestNpcCount(AUTOLANES_SHIPS);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLAutoLanesSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (!mHud.IsValid() || !mNpcs.IsValid() || !mShips.IsValid() || !mRegistry.IsValid() || !mLaneTraffic.IsValid()
        || !mSimClock.IsValid())
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeAutoLanes: HUD or subsystems missing; aborting"));
        Finish(pawn);
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;

    // The population and the lane table first (or the timeout), then a short settle and the crossing check (D20)
    if (mIsWaiting)
    {
        const bool bReady = mNpcs->GetRails().Num() >= mNpcs->GetRequestedCount()
            && !mLaneTraffic->GetLanes().IsEmpty();
        if ((!bReady && mPhaseTimeS < AUTOLANES_WAIT_TIMEOUT_S) || (bReady && mPhaseTimeS < AUTOLANES_READY_SETTLE_S))
        {
            return false;
        }
        mIsWaiting = false;
        mPhaseTimeS = 0.0;
        if (!FindLane())
        {
            Check(TEXT("population"), false, FString::Printf(TEXT("%d rails, %d lanes in the table, Earth-Mars lane "
                "not found"), mNpcs->GetRails().Num(), mLaneTraffic->GetLanes().Num()));
            Finish(pawn);
            return true;
        }
        const FSOLLaneMergeLane lane = LaneNow();
        const FSOLLaneMergeLane lanes[] = { lane };
        const FVector3d midM = lane.Frame.AM + lane.Frame.Axis * (lane.Profile.EndArcAM + 0.5 * lane.Frame.CurrentLegM);
        const FVector3d acrossM = lane.Frame.Up * (10.0 * lane.Profile.WallRadiusM);
        const FSOLLaneAutoLeg crossing = SOLLaneTraffic::FindLaneLeg(lanes, midM - acrossM, midM + acrossM);
        const FSOLLaneAutoLeg along = SOLLaneTraffic::FindLaneLeg(lanes, lane.Frame.AM, lane.Frame.BM);
        Check(TEXT("V7-crossing"), !crossing.bFound && along.bFound, FString::Printf(TEXT("square across the lane: %s, "
            "Earth to Mars: %s (wall radius %.0f km, leg %.4g km)"), crossing.bFound ? TEXT("a leg") : TEXT("no leg"),
            along.bFound ? TEXT("a leg") : TEXT("no leg"), lane.Profile.WallRadiusM / 1000.0,
            lane.Frame.CurrentLegM / 1000.0));
    }

    // Each case: start, run to its end, let its screenshot be written, next
    if (!mIsCaseStarted)
    {
        StartCase();
        mIsCaseStarted = true;
        mPhaseTimeS = 0.0;
        return false;
    }
    if (!UpdateCase(realDeltaSeconds))
    {
        return false;
    }
    if (mPhaseTimeS < AUTOLANES_SHOT_S)
    {
        return false;
    }
    mIsCaseStarted = false;
    mCase = static_cast<ECase>(static_cast<int32>(mCase) + 1);
    if (mCase != ECase::Count)
    {
        return false;
    }
    Finish(pawn);
    return true;
}

//////////////////////////////////////////////////////////////////////////
// Returns the Earth - Mars lane where its bodies are now (ecliptic)
FSOLLaneMergeLane FSOLAutoLanesSmoke::LaneNow() const
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLLaneTrafficEntry& entry = mLaneTraffic->GetLanes()[mLane];
    FSOLLaneMergeLane lane;
    lane.Profile = entry.Profile;
    lane.Frame = SOLLaneTraffic::MakeLaneFrame(entry.Profile, registry.GetPositionM(entry.BodyA),
        registry.GetPositionM(entry.BodyB));
    lane.AVelocityMps = registry.GetVelocityMps(entry.BodyA);
    lane.BVelocityMps = registry.GetVelocityMps(entry.BodyB);
    return lane;
}

//////////////////////////////////////////////////////////////////////////
// Returns the ship's lane sample this frame (the ship's state is in Unreal axes, mirrored into ecliptic)
FSOLAutoLanesSmoke::FLaneSample FSOLAutoLanesSmoke::SampleShip() const
{
    const FSOLLaneMergeLane lane = LaneNow();
    const FSOLShipState state = mShips->GetState();
    FLaneSample sample;
    sample.Point = SOLLaneTraffic::LanePointOf(lane.Profile, lane.Frame, SOLRender::EclipticToUnreal(state.PositionM));
    sample.TrafficMps = SOLLaneTraffic::TrafficSpeedMps(lane.Profile, lane.Frame, sample.Point.LegArcM,
        SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, 1);
    const FVector3d frameMps = lane.AVelocityMps + (lane.BVelocityMps - lane.AVelocityMps) * sample.Point.FootFraction;
    sample.AlongMps = (SOLRender::EclipticToUnreal(state.VelocityMps) - frameMps).Dot(lane.Frame.Axis);
    sample.Delta = sample.TrafficMps > 0.0 ? (sample.AlongMps - sample.TrafficMps) / sample.TrafficMps : 0.0;
    return sample;
}

//////////////////////////////////////////////////////////////////////////
// Places the ship on the lane moving along it and orders the autopilot (or the War power move) to Mars
void FSOLAutoLanesSmoke::PlaceAndEngage(const double legFraction, const double rightRho, const double upRho,
    const double speedFactor, const bool bWarPower)
{
    const FSOLLaneMergeLane lane = LaneNow();
    const FSOLLaneFrame& frame = lane.Frame;
    const double rw = lane.Profile.WallRadiusM;
    const double legArcM = legFraction * frame.CurrentLegM;
    const double alongM = lane.Profile.EndArcAM + legArcM;
    const double spanM = (frame.BM - frame.AM).Size();
    const double foot = spanM > 0.0 ? FMath::Clamp(alongM / spanM, 0.0, 1.0) : 0.0;
    const FVector3d positionM = frame.AM + frame.Axis * alongM + frame.Right * (rightRho * rw)
        + frame.Up * (upRho * rw);
    const FVector3d frameMps = lane.AVelocityMps + (lane.BVelocityMps - lane.AVelocityMps) * foot;
    const double trafficMps = SOLLaneTraffic::TrafficSpeedMps(lane.Profile, frame, legArcM,
        SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, 1);

    FSOLShipState state = mShips->GetState();
    state.PositionM = SOLRender::EclipticToUnreal(positionM);
    state.VelocityMps = SOLRender::EclipticToUnreal(frameMps + frame.Axis * (speedFactor * trafficMps));
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(SOLRender::EclipticToUnreal(frame.Axis)).ToQuat();
    mShips->DisengageAutopilot();
    mShips->SetState(state);
    const bool bEngaged = mShips->EngageAutopilot(mMars, bWarPower);
    UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes: %s placed at leg %.3f, right %.2f, up %.2f wall radii, %.2f x the "
        "traffic speed %.1f m/s, %s to Mars %s (wall radius %.0f km)"), AutoLanesCaseName(static_cast<int32>(mCase)),
        legFraction, rightRho, upRho, speedFactor, trafficMps, bWarPower ? TEXT("War power") : TEXT("Move To"),
        bEngaged ? TEXT("engaged") : TEXT("REFUSED"), rw / 1000.0);
}

//////////////////////////////////////////////////////////////////////////
// Starts a case: placement, order and warp, and clears its observations
void FSOLAutoLanesSmoke::StartCase()
{
    mLastMode = ESOLLaneAutoMode::None;
    mHasSeenApproach = false;
    mHasSeenHold = false;
    mHasSeenOtherMode = false;
    mHoldSample = FLaneSample();
    mMaxHoldDelta = 0.0;
    mHoldFrames = 0;
    mSettledFrames = 0;
    mLeftArcFraction = -1.0;
    mHasArrived = false;
    mMaxGLoadG = 0.0;
    mMinMeter = 1.0;
    mHasBlackedOut = false;
    mHasMarkerLane = false;
    mIsShotTaken = false;
    mCaseFrames = 0;
    mLeftRemainingM = -1.0;
    mLeftStopM = 0.0;
    mLeftSpeedMps = 0.0;
    mMaxOvershootM = 0.0;
    mHasReentered = false;
    switch (mCase)
    {
    case ECase::Approach:
        // Outside the halo on the right, too fast: it must slow and merge on the right half
        PlaceAndEngage(0.30, 2.2, 0.0, 1.25, false);
        SetWarpIndex(AUTOLANES_WARP_HOUR);
        break;
    case ECase::Hold:
    {
        // Inside on the wrong (left) half, too slow: hold at once, move to the right-hand track
        PlaceAndEngage(0.30, -0.6, 0.0, 0.8, false);
        const FSOLLaneMergeLane lane = LaneNow();
        const double sustainedMps2 = SOLNewtonian::GUIDANCE_ACCEL_MARGIN * SOL::PILOT_SUSTAINED_G
            * SOLNewtonian::STANDARD_GRAVITY_MPS2;
        mStraightEtaS = SOLNewtonian::EstimateTimeToArrivalS((lane.Frame.BM - SOLRender::EclipticToUnreal(
            mShips->GetState().PositionM)).Size(), 0.0, sustainedMps2, sustainedMps2, 0.0, SOL::MAX_SPEED_CAP_MPS);
        SetWarpIndex(AUTOLANES_WARP_HOUR);
        break;
    }
    case ECase::Leg:
        // On the track just past Earth's end ring at the traffic speed: the whole leg, leave and arrive
        PlaceAndEngage(0.005, SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, 0.0, 1.0, false);
        SetWarpIndex(AUTOLANES_WARP_DAY);
        break;
    case ECase::MidStop:
    {
        // Holding on the track at the traffic's cruise speed, then a route stop waypoint on the axis inside the lane,
        // a fixed offset from Mars close to it (a point fixed to a body drifts across the lane as the lane turns, so
        // it stays inside only near that body)
        const double legM = LaneNow().Frame.CurrentLegM;
        PlaceAndEngage(1.0 - AUTOLANES_STOP_START_BEFORE_M / legM, SOL::NPC_LANE_AUTO_TRACK_WALL_RADII, 0.0, 1.0,
            false);
        const FSOLLaneMergeLane lane = LaneNow();
        const FVector3d waypointM = lane.Frame.AM + lane.Frame.Axis * (lane.Profile.EndArcAM + lane.Frame.CurrentLegM
            - AUTOLANES_STOP_WAYPOINT_BEFORE_M);
        mWaypointOffsetM = SOLRender::EclipticToUnreal(waypointM)
            - SOLRender::EclipticToUnreal(mRegistry->GetRegistry().GetPositionM(mMars));
        FSOLAutopilotPointOrder order;
        order.BodyIndex = mMars;
        order.OffsetM = mWaypointOffsetM;
        const bool bEngaged = mShips->EngageAutopilotPoint(order);
        UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes: midstop waypoint %.3g m before Mars's end ring on the axis %s"),
            AUTOLANES_STOP_WAYPOINT_BEFORE_M, bEngaged ? TEXT("ordered") : TEXT("REFUSED"));
        SetWarpIndex(AUTOLANES_WARP_HOUR);
        break;
    }
    default:
    {
        // The War power move from the hold case's place: straight pulses of full boosted thrust, no lane; plain
        // AUTO's straight ETA from here at the normal plan is the bar it must beat
        PlaceAndEngage(0.30, -0.6, 0.0, 0.8, true);
        const FSOLLaneMergeLane lane = LaneNow();
        const FSOLShipState state = mShips->GetState();
        const FVector3d marsU = SOLRender::EclipticToUnreal(lane.Frame.BM);
        const FVector3d marsMpsU = SOLRender::EclipticToUnreal(lane.BVelocityMps);
        const FSOLFlightParams params;
        const FSOLGuidanceParams plan = SOLNewtonian::MakeGuidanceParams(params.Envelope,
            SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, FSOLGMeterState()), 0.0, true, false);
        mPlainEtaS = SOLNewtonian::EstimateTimeToArrivalS((marsU - state.PositionM).Size(),
            SOLLaneTraffic::LaneClosingSpeedMps(state.PositionM, state.VelocityMps, marsU, marsMpsU),
            plan.PlanAccelMps2, plan.BrakeAccelMps2, plan.FlipTimeS, SOL::MAX_SPEED_CAP_MPS);
        SetWarpIndex(AUTOLANES_WARP_HOUR);
        break;
    }
    }
    mCaseSimStartS = mSimClock->GetClock().GetSecondsSinceJ2000();
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the current case: observes the mode, speed, side, tag and G, and logs its checks at its end
bool FSOLAutoLanesSmoke::UpdateCase(const double /*realDeltaS*/)
{
    const TCHAR* name = AutoLanesCaseName(static_cast<int32>(mCase));
    if (mIsShotTaken)
    {
        return true;
    }
    ++mCaseFrames;
    const double simS = mSimClock->GetClock().GetSecondsSinceJ2000() - mCaseSimStartS;
    const ESOLLaneAutoMode mode = mShips->GetAutopilotLaneMode();
    const FLaneSample sample = SampleShip();
    const bool bOrder = mShips->GetAutopilotBodyIndex() != INDEX_NONE;
    const bool bTag = mHud->GetLastAutopilotMarkerText().Contains(SOLMenuText::AUTOPILOT_LANE_TAG,
        ESearchCase::CaseSensitive);
    if (mode != mLastMode)
    {
        UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes: %s mode %s -> %s at %.0f s sim, leg %.4f, rho %.3f (right %.3f), "
            "along %.1f m/s, traffic %.1f m/s (delta %+.4f)"), name, AutoLanesModeName(mLastMode),
            AutoLanesModeName(mode), simS, sample.Point.LegArcM / FMath::Max(LaneNow().Frame.CurrentLegM, 1.0),
            sample.Point.Rho, sample.Point.RightOffsetM / FMath::Max(LaneNow().Profile.WallRadiusM, 1.0),
            sample.AlongMps, sample.TrafficMps, sample.Delta);
        if (mode == ESOLLaneAutoMode::Left && mLeftArcFraction < 0.0)
        {
            mLeftArcFraction = sample.Point.LegArcM / FMath::Max(LaneNow().Frame.CurrentLegM, 1.0);
        }
        if (mode == ESOLLaneAutoMode::Hold && !mHasSeenHold)
        {
            mHoldSample = sample;
        }
    }
    mHasReentered |= mLastMode == ESOLLaneAutoMode::Left && (mode == ESOLLaneAutoMode::Approach
        || mode == ESOLLaneAutoMode::Hold);
    mHasSeenApproach |= mode == ESOLLaneAutoMode::Approach;
    mHasSeenHold |= mode == ESOLLaneAutoMode::Hold;
    mHoldFrames += mode == ESOLLaneAutoMode::Hold ? 1 : 0;
    if (mode == ESOLLaneAutoMode::Hold && mHoldFrames > AUTOLANES_HOLD_SETTLE_FRAMES)
    {
        mMaxHoldDelta = FMath::Max(mMaxHoldDelta, FMath::Abs(sample.Delta));
    }
    mHasMarkerLane |= bTag && mCaseFrames > AUTOLANES_TAG_FRAMES;
    mMaxGLoadG = FMath::Max(mMaxGLoadG, mShips->GetGLoadG());
    mMinMeter = FMath::Min(mMinMeter, mShips->GetGMeter().Meter);
    mHasBlackedOut |= mShips->GetGMeter().bBlackout;
    mLastMode = mode;
    const bool bTimedOut = mPhaseTimeS > AUTOLANES_CASE_TIMEOUT_S;
    const double rw = FMath::Max(LaneNow().Profile.WallRadiusM, 1.0);

    // Each case's end rule and checks
    bool bDone = false;
    switch (mCase)
    {
    case ECase::Approach:
        mHasSeenOtherMode |= mode == ESOLLaneAutoMode::None && mCaseFrames > 1;
        if ((mHasSeenHold && mHoldFrames > AUTOLANES_TAG_FRAMES + 1) || simS > AUTOLANES_APPROACH_MAX_SIM_S
            || bTimedOut)
        {
            // The side checked independently of the lane basis: the player's right of travel in Unreal axes (Up x
            // Forward); the speed by the HUD's own merge readout (direction outbound, MATCHED)
            const FSOLLaneMergeLane lane = LaneNow();
            const FVector3d forwardU = SOLRender::EclipticToUnreal(lane.Frame.Axis);
            const FVector3d rightU = FVector3d::CrossProduct(SOLRender::EclipticToUnreal(lane.Frame.Up), forwardU);
            const FVector3d shipU = mShips->GetState().PositionM;
            const FVector3d axisU = SOLRender::EclipticToUnreal(lane.Frame.AM)
                + forwardU * (shipU - SOLRender::EclipticToUnreal(lane.Frame.AM)).Dot(forwardU);
            const double rightRho = (shipU - axisU).Dot(rightU) / rw;
            const FSOLLaneMergeReadout& readout = mHud->GetLastMergeReadout();
            const bool bMatched = readout.bShown && readout.Lane == mLane && readout.DirectionSign == 1
                && readout.Verdict == ESOLLaneMergeVerdict::Matched;
            Check(TEXT("V7-approach-merge"), mHasSeenApproach && mHasSeenHold && rightRho > 0.0
                && rightRho <= SOL::NPC_LANE_AUTO_TRACK_WALL_RADII + SOL::NPC_LANE_AUTO_HOLD_RADIUS_WALL_RADII
                && bMatched, FString::Printf(TEXT("approach %s, hold %s after %.0f s sim; now %.3f wall radii on the "
                "Unreal right of travel; HUD readout %s lane %d direction %d delta %+.4f (%s); at the merge rho %.3f, "
                "delta %+.4f (started at +0.25)"), mHasSeenApproach ? TEXT("seen") : TEXT("NOT SEEN"),
                mHasSeenHold ? TEXT("reached") : TEXT("NOT REACHED"), simS, rightRho, readout.bShown ? TEXT("shown")
                : TEXT("HIDDEN"), readout.Lane, readout.DirectionSign, readout.Delta, bMatched ? TEXT("MATCHED")
                : TEXT("NOT MATCHED"), mHoldSample.Point.Rho, mHoldSample.Delta));
            Check(TEXT("V7-approach-tag"), mHasMarkerLane, FString::Printf(TEXT("marker '%s'"),
                *mHud->GetLastAutopilotMarkerText()));
            TakeShot(name);
            bDone = true;
        }
        break;
    case ECase::Hold:
        mHasSeenOtherMode |= mode != ESOLLaneAutoMode::Hold;
        mSettledFrames = sample.Point.RightOffsetM > 0.0
            && FMath::Abs(sample.Point.Rho - SOL::NPC_LANE_AUTO_TRACK_WALL_RADII) <= AUTOLANES_TRACK_TOLERANCE_RHO
            && FMath::Abs(sample.Delta) <= SOL::NPC_LANE_AUTO_HOLD_SPEED_FRACTION ? mSettledFrames + 1 : 0;
        if ((simS > AUTOLANES_HOLD_SIM_S && mSettledFrames > AUTOLANES_HOLD_SETTLE_FRAMES)
            || simS > AUTOLANES_HOLD_MAX_SIM_S || bTimedOut)
        {
            mHoldEtaS = mShips->GetAutopilotGuidance().EtaS;
            Check(TEXT("V7-hold-right"), !mHasSeenOtherMode && sample.Point.RightOffsetM > 0.0
                && FMath::Abs(sample.Point.Rho - SOL::NPC_LANE_AUTO_TRACK_WALL_RADII) <= AUTOLANES_TRACK_TOLERANCE_RHO
                && FMath::Abs(sample.Delta) <= SOL::NPC_LANE_AUTO_HOLD_SPEED_FRACTION, FString::Printf(TEXT(
                "hold throughout %s over %.0f s sim (engaged inside on the left at -0.60 wall radii, 0.8 x the "
                "traffic speed): rho %.3f, right %.3f wall radii, along %.1f m/s, traffic %.1f m/s (delta %+.4f)"),
                mHasSeenOtherMode ? TEXT("NO") : TEXT("yes"), simS, sample.Point.Rho, sample.Point.RightOffsetM / rw,
                sample.AlongMps, sample.TrafficMps, sample.Delta));
            Check(TEXT("V7-hold-tag"), mHasMarkerLane && bTag, FString::Printf(TEXT("marker '%s'"),
                *mHud->GetLastAutopilotMarkerText()));
            Check(TEXT("V7-hold-eta"), FMath::IsFinite(mHoldEtaS) && mHoldEtaS > mStraightEtaS, FString::Printf(TEXT(
                "ETA %.2f days with the lane, the straight no-lane estimate %.2f days"), mHoldEtaS / 86400.0,
                mStraightEtaS / 86400.0));
            TakeShot(name);
            bDone = true;
        }
        break;
    case ECase::Leg:
        // Slower warp for the leave and arrival near Mars
        if (sample.Point.LegArcM > AUTOLANES_LEG_SLOW_FRACTION * LaneNow().Frame.CurrentLegM
            && mSimClock->GetClock().GetWarpIndex() > AUTOLANES_WARP_HOUR)
        {
            SetWarpIndex(AUTOLANES_WARP_HOUR);
        }
        mHasArrived |= mHasSeenHold && !bOrder;
        if (mHasArrived || simS > AUTOLANES_LEG_MAX_SIM_S || bTimedOut)
        {
            Check(TEXT("V7-leg-held"), mHasSeenHold && mHoldFrames > AUTOLANES_HOLD_SETTLE_FRAMES
                && mMaxHoldDelta <= SOL::NPC_LANE_AUTO_HOLD_SPEED_FRACTION, FString::Printf(TEXT("held %d frames, the "
                "largest |delta| from the traffic speed %.4f"), mHoldFrames, mMaxHoldDelta));
            Check(TEXT("V7-leg-left-arrived"), mLeftArcFraction >= AUTOLANES_LEFT_MIN_FRACTION && mHasArrived,
                FString::Printf(TEXT("left the lane at leg %.4f, %s after %.2f days sim"), mLeftArcFraction,
                mHasArrived ? TEXT("arrived at Mars") : TEXT("NOT ARRIVED"), simS / 86400.0));
            bDone = true;
        }
        break;
    case ECase::MidStop:
    {
        // The braking room when it leaves, the overshoot after it, the arrival
        const FSOLLaneMergeLane lane = LaneNow();
        const FVector3d forwardU = SOLRender::EclipticToUnreal(lane.Frame.Axis);
        const FVector3d waypointU = SOLRender::EclipticToUnreal(mRegistry->GetRegistry().GetPositionM(mMars))
            + mWaypointOffsetM;
        const FSOLShipState state = mShips->GetState();
        if (mode == ESOLLaneAutoMode::Left && mLeftRemainingM < 0.0)
        {
            const FSOLFlightParams params;
            const FSOLGuidanceParams plan = SOLNewtonian::MakeGuidanceParams(params.Envelope,
                SOLNewtonian::AutopilotAccelLimitMps2(params.PilotLimits, mShips->GetGMeter()), 0.0, true, false);
            mLeftSpeedMps = (state.VelocityMps - SOLRender::EclipticToUnreal(lane.BVelocityMps)).Size();
            mLeftRemainingM = (waypointU - state.PositionM).Dot(forwardU);
            mLeftStopM = SOLNewtonian::StoppingDistanceM(mLeftSpeedMps, plan.BrakeAccelMps2, plan.FlipTimeS);
        }
        if (mLeftRemainingM >= 0.0)
        {
            mMaxOvershootM = FMath::Max(mMaxOvershootM, (state.PositionM - waypointU).Dot(forwardU));
        }
        mHasArrived |= mHasSeenHold && !bOrder;
        if (mHasArrived || simS > AUTOLANES_STOP_MAX_SIM_S || bTimedOut)
        {
            const double frameSimS = mSimClock->GetClock().GetWarpFactor() / AUTOLANES_MIN_FPS;
            // Left by the braking rule (within the leave distance's cap of 1.5 x the braking distance + 1e8 m), not
            // released early, at least the braking distance short
            Check(TEXT("V7-midstop-leave"), mHasSeenHold && mLeftRemainingM >= mLeftStopM - mLeftSpeedMps * frameSimS
                && mLeftRemainingM <= 1.5 * mLeftStopM + 1.0e8 + mLeftSpeedMps * frameSimS && !mHasReentered,
                FString::Printf(TEXT("held %s, left %.4g m before the waypoint at %.1f m/s "
                "relative (braking needs %.4g m), re-entered %s"), mHasSeenHold ? TEXT("yes") : TEXT("NO"),
                mLeftRemainingM, mLeftSpeedMps, mLeftStopM, mHasReentered ? TEXT("YES") : TEXT("no")));
            Check(TEXT("V7-midstop-arrived"), mHasArrived && mMaxOvershootM <= AUTOLANES_OVERSHOOT_BOUND_M,
                FString::Printf(TEXT("%s after %.2f days sim, overshoot %.4g m (bound %.0g)"), mHasArrived
                ? TEXT("arrived") : TEXT("NOT ARRIVED"), simS / 86400.0, mMaxOvershootM, AUTOLANES_OVERSHOOT_BOUND_M));
            bDone = true;
        }
        break;
    }
    default:
        mHasSeenOtherMode |= mode != ESOLLaneAutoMode::None;
        if (mCaseFrames == AUTOLANES_TAG_FRAMES + 2)
        {
            TakeShot(name);
        }
        mHasArrived |= !bOrder;
        if (mHasArrived || simS > AUTOLANES_WAR_MAX_SIM_S || bTimedOut)
        {
            Check(TEXT("V7-warpower-no-lane"), !mHasSeenOtherMode && !mHasMarkerLane, FString::Printf(TEXT("lane "
                "mode %s, tag %s"), mHasSeenOtherMode ? TEXT("SEEN") : TEXT("none"), mHasMarkerLane ? TEXT("SHOWN")
                : TEXT("none")));
            Check(TEXT("V7-warpower-thrust"), mMaxGLoadG > SOL::PILOT_SUSTAINED_G + 0.5 && !mHasBlackedOut
                && mMinMeter >= SOL::WAR_POWER_METER_FLOOR - 0.02, FString::Printf(TEXT("peak %.2f G (sustained "
                "%.1f), lowest meter %.3f (floor %.2f), blackout %s"), mMaxGLoadG, SOL::PILOT_SUSTAINED_G, mMinMeter,
                SOL::WAR_POWER_METER_FLOOR, mHasBlackedOut ? TEXT("YES") : TEXT("no")));
            Check(TEXT("V7-warpower-eta"), mHasArrived && simS < mPlainEtaS, FString::Printf(TEXT("%s after %.2f h "
                "sim, plain AUTO's straight estimate from the same start %.2f h (the lane's ETA was %.2f h)"),
                mHasArrived ? TEXT("arrived") : TEXT("NOT ARRIVED"), simS / 3600.0, mPlainEtaS / 3600.0,
                mHoldEtaS / 3600.0));
            bDone = true;
        }
        break;
    }
    if (bDone)
    {
        // The order and warp stay while the screenshot is written; the next case's placement disengages
        mIsShotTaken = true;
        mPhaseTimeS = 0.0;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Sets the time warp to a factor index (0 = 1x)
void FSOLAutoLanesSmoke::SetWarpIndex(const int32 index) const
{
    mSimClock->ResetWarp();
    for (int32 step = 0; step < index; ++step)
    {
        mSimClock->StepWarpUp();
    }
}

//////////////////////////////////////////////////////////////////////////
// Finds the Earth - Mars lane and the two bodies in the lane table
bool FSOLAutoLanesSmoke::FindLane()
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    mEarth = registry.FindByName(FName(SOL::BodyNames::EARTH));
    mMars = registry.FindByName(FName(SOL::BodyNames::MARS));
    const TConstArrayView<FSOLLaneTrafficEntry> lanes = mLaneTraffic->GetLanes();
    mLane = INDEX_NONE;
    for (int32 lane = 0; lane < lanes.Num(); ++lane)
    {
        if (lanes[lane].BodyA == mEarth && lanes[lane].BodyB == mMars && lanes[lane].Profile.bDrivable)
        {
            mLane = lane;
        }
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes: %d rails, %d lanes in the table, Earth-Mars lane %d"),
        mNpcs->GetRails().Num(), lanes.Num(), mLane);
    return mLane != INDEX_NONE && mMars != INDEX_NONE;
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line, restores the warp, overrides and space dust and takes the closing screenshot (which quits)
void FSOLAutoLanesSmoke::Finish(ASOLShipPawn& pawn)
{
    if (mShips.IsValid())
    {
        mShips->DisengageAutopilot();
    }
    if (mSimClock.IsValid())
    {
        mSimClock->ResetWarp();
    }
    if (mLaneTraffic.IsValid())
    {
        mLaneTraffic->SetReadoutVerificationOverride(false);
    }
    AutoLanesSetCvar(SOL::SpaceDust::CVAR_ENABLED, 1);
    UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes SUMMARY: complete after %.1f s, %d/%d checks run passed%s"), mTotalTimeS,
        mChecksPassed, mChecksRun, mChecksPassed == mChecksRun && mChecksRun > 0 ? TEXT("") : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot with the HUD named AutoLanes_<width>x<height>_<name>.png
void FSOLAutoLanesSmoke::TakeShot(const TCHAR* name) const
{
    FVector2D size(0.0, 0.0);
    if (mHud.IsValid() && mHud->GetWorld()->GetGameViewport() != nullptr)
    {
        mHud->GetWorld()->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("AutoLanes_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes: screenshot %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLAutoLanesSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeAutoLanes CHECK %-22s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
