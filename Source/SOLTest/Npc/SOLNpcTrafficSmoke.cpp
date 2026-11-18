/*
* SOLTest
* Copyright © 2026 Acid Rain Studios LLC
*/

#include "Npc/SOLNpcTrafficSmoke.h"

#include "Game/SOLGameMode.h"
#include "Npc/SOLNpcAiMath.h"
#include "Npc/SOLNpcRail.h"
#include "Npc/SOLNpcScaleMath.h"
#include "Npc/SOLNpcSubsystem.h"
#include "Npc/SOLNpcTierProcessor.h"
#include "Npc/SOLNpcTrafficMirror.h"
#include "Npc/SOLNpcTunnelComponent.h"
#include "Npc/SOLNpcTunnelMath.h"
#include "Npc/SOLNpcVisuals.h"
#include "OrbitLines/SOLOrbitLineVisuals.h"
#include "Ship/SOLShipPawn.h"
#include "Ship/SOLShipSubsystem.h"
#include "SOLConstants.h"
#include "SOLTest.h"
#include "Traffic/SOLLaneTrafficMath.h"
#include "Universe/SOLAnchorSubsystem.h"
#include "Universe/SOLBodyRegistrySubsystem.h"
#include "Universe/SOLRenderPlacement.h"

#include "Algo/Sort.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

#include <cmath>
#include <limits>

namespace
{
    // Phase durations (real seconds)
    constexpr double NPCTRAFFIC_WAIT_TIMEOUT_S = 240.0;      // Population spawn and upload (1M ships)
    constexpr double NPCTRAFFIC_VIEW_SETUP_S = 2.5;          // Anchor, rebase and the far layer settle after a move
    constexpr double NPCTRAFFIC_SHOT_GAP_S = 0.5;            // V1: the two screenshots this far apart
    constexpr double NPCTRAFFIC_SEQUENCE_GAP_S = 0.35;       // V5: the screenshot sequence's spacing
    constexpr double NPCTRAFFIC_SHOT_S = 0.8;                // The screenshot is written before the next change
    constexpr double NPCTRAFFIC_V2_TRACK_S = 8.0;            // V2: the fly-by is tracked this long after the move
    constexpr int32 NPCTRAFFIC_MIN_PHASE_FRAMES = 5;

    // Script values
    constexpr int32 NPCTRAFFIC_SHIPS = 1000000;              // Population (SDD 50 section 8: 1M NPCs)
    constexpr int32 NPCTRAFFIC_SAMPLES = 100;                // V1: sampled rails per lane
    constexpr double NPCTRAFFIC_V1_FRACTION = 0.25;          // V1, V5: a quarter of the way from Earth
    constexpr double NPCTRAFFIC_V2_FRACTION = 0.5;           // V2: the lane's middle
    constexpr double NPCTRAFFIC_V3_SIDE_RADII = 8.0;         // V3: the camera this many wall radii to the side
    constexpr double NPCTRAFFIC_SIDE_MIN_RADII = 0.1;        // V1 on screen: ships at least this far off the axis
    constexpr double NPCTRAFFIC_LATERAL_TOLERANCE_M = 1.0;   // V1: a zero right offset read back in double
    constexpr double NPCTRAFFIC_MOTION_FRACTION = 0.9;       // V1: each leg ship moved at least 0.9 w dt
    constexpr double NPCTRAFFIC_MIN_SHOT_GAP_S = 0.3;        // ...over at least this much rail time
    constexpr int32 NPCTRAFFIC_MIN_MID_SHIPS = 20;           // V2: Mid-tier ships needed, else sampled rails
    constexpr int32 NPCTRAFFIC_SPEED_SAMPLES = 300;          // V2: sampled rails in the fallback
    constexpr double NPCTRAFFIC_MID_LANE_MIN = 0.35;         // ...between these fractions of the current leg
    constexpr double NPCTRAFFIC_MID_LANE_MAX = 0.65;
    constexpr double NPCTRAFFIC_ENVELOPE_TOLERANCE = 1.0e-3; // V2: effort envelope (float-rounded dv, the difference)
    constexpr double NPCTRAFFIC_RHO_LIMIT = -0.8;            // V2: Spearman rank correlation of speed with rho
    constexpr double NPCTRAFFIC_WALL_PEAK = 1.05;            // V2: wall ships have a peak of at most 1.05 w...
    constexpr double NPCTRAFFIC_WALL_TOLERANCE = 0.1;        // ...and are measured at w +- 10 %
    constexpr int32 NPCTRAFFIC_MIN_WALL_SHIPS = 5;
    constexpr double NPCTRAFFIC_MID_HEADING_DEG = 2.0;       // V2: a Mid hull's heading (deviation velocity included)
    constexpr double NPCTRAFFIC_RAIL_HEADING_DEG = 0.1;      // V2: the heading chain on a bare rail
    constexpr double NPCTRAFFIC_FULL_HEADING_DEG = 15.0;     // V2: a Full hull's nose off LaneHullForward (lag)
    constexpr double NPCTRAFFIC_HULL_DOT = 0.9999;           // V2: nose first, engine first, the held brake attitude
    constexpr double NPCTRAFFIC_V2_MISS_M = 5000.0;          // V2: the fly-by passes the tracked ship this close...
    constexpr double NPCTRAFFIC_V2_MIN_CLOSEST_S = 1.5;      // ...this long after the move at the earliest (2 Full
    constexpr double NPCTRAFFIC_V2_MAX_CLOSEST_S = 4.0;      // radii out at the relative speed) and at the latest
    constexpr double NPCTRAFFIC_HANDOFF_DEG = 3.0;           // V2: the tracked nose's change at promotion / demotion
    constexpr int32 NPCTRAFFIC_HEADING_SAMPLES = 4000;       // V2: sampled rails for the heading categories
    constexpr double NPCTRAFFIC_CLEARANCE_TOLERANCE_RADII = 1.0e-3;  // V3: rounding of the D14 clearance
    constexpr double NPCTRAFFIC_HALO_TOLERANCE = 0.02;       // V4: halo share +- 2 %
    constexpr int32 NPCTRAFFIC_HANDOVER_SAMPLES = 100;       // V5: ships within the band around the Mid radius...
    constexpr double NPCTRAFFIC_BAND_MIN = 0.5;              // ...of 0.5 - 2 Mid radii (and sampled ships crossing)
    constexpr double NPCTRAFFIC_BAND_MAX = 2.0;
    constexpr double NPCTRAFFIC_HANDOVER_PX = 0.5;           // V5: the drawn step off the far point's (px at Mid)
    constexpr int32 NPCTRAFFIC_SMOOTH_SAMPLES = 30;          // V5: end-zone and U-turn ships near the viewpoint
    constexpr int32 NPCTRAFFIC_SMOOTH_FRAMES = 300;          // ...sampled at 60 Hz for 5 s
    constexpr double NPCTRAFFIC_SMOOTH_HZ = 60.0;
    constexpr double NPCTRAFFIC_SMOOTH_STEP = 0.2;           // ...each step within 20 % of the CPU's...
    constexpr double NPCTRAFFIC_SMOOTH_ULPS = 8.0;           // ...plus 8 ulps of the nearer end's distance
    constexpr double NPCTRAFFIC_TURN_MIN_RADII = 0.3;        // U-turn ships swinging at least this far (no apex stop)
    constexpr double NPCTRAFFIC_PROJECT_CM = 1.0e5;          // Points projected along their direction from the camera
    constexpr double NPCTRAFFIC_PI = 3.14159265358979;       // The shader's SOLNTR_PI

    //////////////////////////////////////////////////////////////////////////
    // Sets an integer console variable (sol.SpaceDust)
    void TrafficSmokeSetCvar(const TCHAR* name, const int32 value)
    {
        if (IConsoleVariable* cvar = IConsoleManager::Get().FindConsoleVariable(name))
        {
            cvar->Set(value, ECVF_SetByCode);
        }
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the float spacing (ulp) at a magnitude
    double TrafficSmokeUlp(const double magnitude)
    {
        const float value = static_cast<float>(FMath::Abs(magnitude));
        return static_cast<double>(std::nextafter(value, std::numeric_limits<float>::infinity()) - value);
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns a lane rail's radial position in wall radii
    double TrafficSmokeRho(const FSOLNpcRail& rail, const FSOLLaneProfile& profile)
    {
        return profile.WallRadiusM > 0.0 ? FMath::Sqrt(FMath::Square(static_cast<double>(rail.P[2]))
            + FMath::Square(static_cast<double>(rail.P[3]))) / profile.WallRadiusM : 0.0;
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns the Spearman rank correlation of two equally long series (ties ranked by order; 0 under 3 values)
    double TrafficSmokeSpearman(const TArray<double>& x, const TArray<double>& y)
    {
        const int32 count = x.Num();
        if (count < 3 || y.Num() != count)
        {
            return 0.0;
        }

        // Each series' ranks, then 1 - 6 sum d^2 / (n (n^2 - 1))
        const auto ranks = [count](const TArray<double>& values)
        {
            TArray<int32> order;
            order.SetNumUninitialized(count);
            for (int32 index = 0; index < count; ++index)
            {
                order[index] = index;
            }
            Algo::Sort(order, [&values](const int32 a, const int32 b) { return values[a] < values[b]; });
            TArray<double> rank;
            rank.SetNumUninitialized(count);
            for (int32 index = 0; index < count; ++index)
            {
                rank[order[index]] = static_cast<double>(index);
            }
            return rank;
        };
        const TArray<double> rankX = ranks(x);
        const TArray<double> rankY = ranks(y);
        double sum = 0.0;
        for (int32 index = 0; index < count; ++index)
        {
            sum += FMath::Square(rankX[index] - rankY[index]);
        }
        const double n = static_cast<double>(count);
        return 1.0 - 6.0 * sum / (n * (n * n - 1.0));
    }

    //////////////////////////////////////////////////////////////////////////
    // Returns whether a segment is a U-turn
    bool TrafficSmokeIsTurn(const ESOLLaneSegment segment)
    {
        return segment == ESOLLaneSegment::TurnAtA || segment == ESOLLaneSegment::TurnAtB;
    }
}

//////////////////////////////////////////////////////////////////////////
// Caches the subsystems and actors, sets up the overrides and the population, and starts the first phase
void FSOLNpcTrafficSmoke::Start(ASOLShipPawn& pawn)
{
    UWorld* world = pawn.GetWorld();
    const ASOLGameMode* gameMode = world->GetAuthGameMode<ASOLGameMode>();
    mVisuals = gameMode != nullptr ? gameMode->GetNpcVisuals() : nullptr;
    mOrbitLines = gameMode != nullptr ? gameMode->GetOrbitLineVisuals() : nullptr;
    mNpcs = world->GetSubsystem<USOLNpcSubsystem>();
    mShips = world->GetSubsystem<USOLShipSubsystem>();
    mRegistry = world->GetSubsystem<USOLBodyRegistrySubsystem>();
    mAnchor = world->GetSubsystem<USOLAnchorSubsystem>();
    mController = pawn.GetController<APlayerController>();
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTraffic: starting (NPC visuals %s, orbit lines %s)"),
        mVisuals.IsValid() ? *mVisuals->GetName() : TEXT("none"), mOrbitLines.IsValid() ? *mOrbitLines->GetName()
        : TEXT("none"));

    // Overrides: the lanes, the tunnels and the orbit lines leave the player's settings alone; orbit lines off; the
    // space dust off in memory (restored at the end)
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
    TrafficSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 0);
    if (mNpcs.IsValid() && mNpcs->GetRequestedCount() == 0)
    {
        mNpcs->RequestNpcCount(NPCTRAFFIC_SHIPS);
    }
    mPhase = ESOLTrafficPhase::WaitPopulation;
}

//////////////////////////////////////////////////////////////////////////
// Runs one frame of the script; returns true once it has finished
bool FSOLNpcTrafficSmoke::Update(ASOLShipPawn& pawn, const float realDeltaSeconds)
{
    if (mPhase == ESOLTrafficPhase::Done)
    {
        return true;
    }
    if (!mVisuals.IsValid() || !mNpcs.IsValid() || !mShips.IsValid() || !mRegistry.IsValid() || !mAnchor.IsValid()
        || mVisuals->GetTunnels() == nullptr)
    {
        UE_LOG(LogSOL, Error, TEXT("SmokeNpcTraffic: NPC visuals or subsystems missing; aborting"));
        mPhase = ESOLTrafficPhase::Done;
        Finish(pawn);
        return true;
    }
    mPhaseTimeS += realDeltaSeconds;
    mTotalTimeS += realDeltaSeconds;
    ++mPhaseFrames;

    // The last two rail frames (a ship's universe velocity for the fly-by); a few kilobytes copied per frame
    if (mNpcs->GetRailFrame().RailTimeS != mLastFrame.RailTimeS)
    {
        mPrevFrame = MoveTemp(mLastFrame);
        mLastFrame = mNpcs->GetRailFrame();
    }

    // A view moves only once the previous phase's screenshot is written (V1 follows the population, no shot)
    if (!mIsViewPlaced && mPhaseTimeS >= (mPhase == ESOLTrafficPhase::V1Setup ? 0.0 : NPCTRAFFIC_SHOT_S))
    {
        EnterPhase(mPhase);
        mIsViewPlaced = true;
    }
    else if (mIsViewPlaced && mPhase == ESOLTrafficPhase::V2Setup)
    {
        TrackFlyBy();
    }

    // The population wait ends once every rail is uploaded and the group rows are written (or times out)
    if (mPhase == ESOLTrafficPhase::WaitPopulation)
    {
        const bool bReady = mNpcs->GetRails().Num() >= NPCTRAFFIC_SHIPS && mVisuals->GetLaneMeshVertexCount() > 0
            && mVisuals->GetLastGroupTexels().Num() >= SOL::NPC_GROUP_TEXTURE_WIDTH * SOL::NPC_GROUP_TEXTURE_ROWS;
        if (!bReady && mPhaseTimeS < NPCTRAFFIC_WAIT_TIMEOUT_S)
        {
            return false;
        }
    }
    else if (mPhaseTimeS < GetPhaseDuration(mPhase) || mPhaseFrames < NPCTRAFFIC_MIN_PHASE_FRAMES)
    {
        return false;
    }

    ExitPhase(mPhase);
    mPhase = mLane == INDEX_NONE || mPhase == ESOLTrafficPhase::Tail ? ESOLTrafficPhase::Done
        : static_cast<ESOLTrafficPhase>(static_cast<uint8>(mPhase) + 1);
    mPhaseTimeS = 0.0;
    mPhaseFrames = 0;
    mIsViewPlaced = false;
    if (mPhase == ESOLTrafficPhase::Done)
    {
        Finish(pawn);
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
// Returns the minimum real duration of a phase in seconds
double FSOLNpcTrafficSmoke::GetPhaseDuration(const ESOLTrafficPhase phase)
{
    switch (phase)
    {
    case ESOLTrafficPhase::V1Setup:
        return NPCTRAFFIC_VIEW_SETUP_S;
    case ESOLTrafficPhase::V2Setup:
        return NPCTRAFFIC_SHOT_S + NPCTRAFFIC_V2_TRACK_S;       // The fly-by starts once the last shot is written
    case ESOLTrafficPhase::V3Setup:
        return NPCTRAFFIC_VIEW_SETUP_S + NPCTRAFFIC_SHOT_S;     // The last shot is written before the move
    case ESOLTrafficPhase::V1Hold:
        return NPCTRAFFIC_SHOT_GAP_S;
    case ESOLTrafficPhase::V5Check:
    case ESOLTrafficPhase::V5Shot2:
    case ESOLTrafficPhase::V5Shot3:
        return NPCTRAFFIC_SEQUENCE_GAP_S;
    default:
        return NPCTRAFFIC_SHOT_S;
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's entry actions: each view places the ship (called once the previous screenshot is written)
void FSOLNpcTrafficSmoke::EnterPhase(const ESOLTrafficPhase phase)
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    if (!frame.LaneFrame.IsValidIndex(mLane))
    {
        return;
    }
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[mLane];
    const FSOLLaneProfile& profile = frame.LaneProfile[mLane];
    const double lengthM = (laneFrame.BM - laneFrame.AM).Size();
    if (phase == ESOLTrafficPhase::V1Setup)
    {
        SetupShip(laneFrame.AM + laneFrame.Axis * (NPCTRAFFIC_V1_FRACTION * lengthM), laneFrame.Axis);
    }
    else if (phase == ESOLTrafficPhase::V2Setup)
    {
        SetupFlyBy();
    }
    else if (phase == ESOLTrafficPhase::V3Setup)
    {
        // Side view: beside the U-turn region past Earth's end ring, looking across the lane (Earth in frame)
        const FVector3d turnCentreM = laneFrame.AM + laneFrame.Axis * (profile.EndArcAM - 0.5 * profile.UTurnDepthM);
        SetupShip(turnCentreM + laneFrame.Right * (NPCTRAFFIC_V3_SIDE_RADII * profile.WallRadiusM), -laneFrame.Right);
    }
}

//////////////////////////////////////////////////////////////////////////
// Runs a phase's exit checks and screenshots
void FSOLNpcTrafficSmoke::ExitPhase(const ESOLTrafficPhase phase)
{
    switch (phase)
    {
    case ESOLTrafficPhase::WaitPopulation:
        if (BuildSamples())
        {
            CheckHalo();
        }
        else
        {
            Check(TEXT("population"), false, FString::Printf(TEXT("%d rails after %.0f s, Earth-Mars lane not found"),
                mNpcs->GetRails().Num(), mPhaseTimeS));
        }
        break;
    case ESOLTrafficPhase::V1Setup:
        CheckKeepRight();
        CheckTunnelRadius();
        TakeShot(TEXT("V1a"));
        break;
    case ESOLTrafficPhase::V1Hold:
        TakeShot(TEXT("V1b"));
        CheckMotion();
        break;
    case ESOLTrafficPhase::V5Check:
        CheckHandOver(TEXT("V5-quarter"), false);
        TakeShot(TEXT("V5a"));
        break;
    case ESOLTrafficPhase::V5Shot2:
        TakeShot(TEXT("V5b"));
        break;
    case ESOLTrafficPhase::V5Shot3:
        TakeShot(TEXT("V5c"));
        break;
    case ESOLTrafficPhase::V2Setup:
        CheckSpeeds();
        TakeShot(TEXT("V2"));
        break;
    case ESOLTrafficPhase::V3Setup:
        CheckUTurn();
        CheckHandOver(TEXT("V5-earth-end"), true);
        TakeShot(TEXT("V3"));
        break;
    default:
        break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Logs the SUMMARY line and takes the closing screenshot (which quits)
void FSOLNpcTrafficSmoke::Finish(ASOLShipPawn& pawn)
{
    TrafficSmokeSetCvar(SOL::SpaceDust::CVAR_ENABLED, 1);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTraffic SUMMARY: complete after %.1f s, %d/%d checks run passed%s"),
        mTotalTimeS, mChecksPassed, mChecksRun, mChecksPassed == mChecksRun && mChecksRun > 0 ? TEXT("")
        : TEXT(" (FAILURES)"));
    if (ASOLGameMode* gameMode = pawn.GetWorld()->GetAuthGameMode<ASOLGameMode>())
    {
        gameMode->TakeSmokeScreenshot();
    }
}

//////////////////////////////////////////////////////////////////////////
// Finds the Earth-Mars lane and fills the per-lane samples (spread evenly over each lane's rails) and its ship list
bool FSOLNpcTrafficSmoke::BuildSamples()
{
    const FSOLBodyRegistry& registry = mRegistry->GetRegistry();
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const int32 earth = registry.FindByName(FName(SOL::BodyNames::EARTH));
    const int32 mars = registry.FindByName(FName(SOL::BodyNames::MARS));
    const uint8 laneKind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
    mLane = INDEX_NONE;
    for (int32 lane = 0; lane < groups.LaneCount; ++lane)
    {
        if (groups.LaneBodyA.IsValidIndex(lane) && groups.LaneBodyB.IsValidIndex(lane)
            && groups.LaneBodyA[lane] == earth && groups.LaneBodyB[lane] == mars)
        {
            mLane = lane;
        }
    }

    // Count each lane's rails, then take every k-th so the samples spread over the lane
    TArray<int32> counts;
    counts.SetNumZeroed(groups.LaneCount);
    for (const FSOLNpcRail& rail : rails)
    {
        if (rail.Kind == laneKind && counts.IsValidIndex(rail.Group))
        {
            ++counts[rail.Group];
        }
    }
    TArray<int32> seen;
    seen.SetNumZeroed(groups.LaneCount);
    mSamples.SetNum(groups.LaneCount);
    mLaneShips.Reset(mLane != INDEX_NONE ? counts[mLane] : 0);
    for (int32 index = 0; index < rails.Num(); ++index)
    {
        const FSOLNpcRail& rail = rails[index];
        if (rail.Kind != laneKind || !counts.IsValidIndex(rail.Group))
        {
            continue;
        }
        const int32 lane = rail.Group;
        const int32 stride = FMath::Max(1, counts[lane] / NPCTRAFFIC_SAMPLES);
        if (seen[lane]++ % stride == 0 && mSamples[lane].Num() < NPCTRAFFIC_SAMPLES)
        {
            mSamples[lane].Add(index);
        }
        if (lane == mLane)
        {
            mLaneShips.Add(index);
        }
    }
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTraffic: %d rails, %d lanes, Earth-Mars lane %d with %d ships, Mid radius %.0f km"),
        rails.Num(), groups.LaneCount, mLane, mLaneShips.Num(), mNpcs->GetStats().MidRadiusM / 1000.0);
    return mLane != INDEX_NONE && mLaneShips.Num() > 0 && mNpcs->GetRailFrame().LaneFrame.IsValidIndex(mLane)
        && mNpcs->GetRailFrame().LaneFrame[mLane].bValid;
}

//////////////////////////////////////////////////////////////////////////
// Places the ship (ecliptic universe metres) facing a direction, at the reference velocity, not rotating
void FSOLNpcTrafficSmoke::SetupShip(const FVector3d& positionM, const FVector3d& forward) const
{
    FSOLShipState state = mShips->GetState();
    state.PositionM = SOLRender::EclipticToUnreal(positionM);
    state.VelocityMps = mShips->GetReferenceVelocityMps();
    state.AngularVelocityRadPerS = FVector3d::ZeroVector;
    if (!forward.IsNearlyZero())
    {
        state.Orientation = UE::Math::TRotationMatrix<double>::MakeFromX(SOLRender::EclipticToUnreal(forward)).ToQuat();
    }
    mShips->SetState(state);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTraffic: view at (%.6g, %.6g, %.6g) m ecliptic facing (%.3f, %.3f, %.3f)"),
        positionM.X, positionM.Y, positionM.Z, forward.X, forward.Y, forward.Z);
}

//////////////////////////////////////////////////////////////////////////
// V2: picks the sampled Earth-Mars ship coasting nearest the lane's middle with no flip or segment change during the
// fly-by and places the viewpoint (at rest in the reference frame) where that ship passes it at NPCTRAFFIC_V2_MISS_M,
// two Full radii of approach after the move (clamped), so it is drawn as a Mid hull, promoted to Full and demoted again
void FSOLNpcTrafficSmoke::SetupFlyBy()
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const FSOLLaneProfile& profile = frame.LaneProfile[mLane];
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[mLane];
    const double timeS = frame.RailTimeS;
    const double lengthM = (laneFrame.BM - laneFrame.AM).Size();
    mFlyBy = FFlyBy();

    // The candidate nearest the middle whose thrust state is the same now and after the fly-by
    double bestOffset = TNumericLimits<double>::Max();
    const int32 stride = FMath::Max(1, mLaneShips.Num() / NPCTRAFFIC_HEADING_SAMPLES);
    for (int32 sample = 0; sample < mLaneShips.Num(); sample += stride)
    {
        const FSOLNpcRail& rail = rails[mLaneShips[sample]];
        const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);
        const uint32 key = static_cast<uint32>(mLaneShips[sample]);
        const FSOLLaneThrustState now = SOLLaneTraffic::LaneThrustAt(profile, laneFrame, ship, key, timeS);
        const FSOLLaneThrustState after = SOLLaneTraffic::LaneThrustAt(profile, laneFrame, ship, key,
            timeS + NPCTRAFFIC_SHOT_S + NPCTRAFFIC_V2_TRACK_S);
        const double fraction = ((SOLNpcRail::EvaluateM(rail, frame) - laneFrame.AM) | laneFrame.Axis) / lengthM;
        const bool bSteady = !TrafficSmokeIsTurn(now.Segment) && now.Segment == after.Segment
            && now.Thrust == ESOLLaneThrust::Coast && after.Thrust == ESOLLaneThrust::Coast
            && now.FlipFraction == after.FlipFraction && (now.FlipFraction <= 0.0 || now.FlipFraction >= 1.0);
        if (bSteady && FMath::Abs(fraction - NPCTRAFFIC_V2_FRACTION) < bestOffset)
        {
            bestOffset = FMath::Abs(fraction - NPCTRAFFIC_V2_FRACTION);
            mFlyBy.Npc = mLaneShips[sample];
        }
    }
    if (mFlyBy.Npc == INDEX_NONE)
    {
        SetupShip(laneFrame.AM + laneFrame.Axis * (NPCTRAFFIC_V2_FRACTION * lengthM), laneFrame.Axis);
        return;
    }

    // Its universe velocity (the last two rail frames, so the lane's own motion is included) relative to the reference
    // frame the viewpoint rests in, and the point it passes in the closest-approach time
    const FSOLNpcRail& rail = rails[mFlyBy.Npc];
    const FVector3d positionM = SOLNpcRail::EvaluateM(rail, frame);
    const double stepS = mLastFrame.RailTimeS - mPrevFrame.RailTimeS;
    const bool bHasStep = stepS > 0.0 && mPrevFrame.GroupPointAM.IsValidIndex(rail.Group)
        && mPrevFrame.LaneFrame.IsValidIndex(rail.Group) && mLastFrame.LaneFrame.IsValidIndex(rail.Group);
    const FVector3d velocityMps = bHasStep ? (SOLNpcRail::EvaluateM(rail, mLastFrame)
        - SOLNpcRail::EvaluateM(rail, mPrevFrame)) / stepS : SOLNpcRail::Motion(rail, frame, mFlyBy.Npc).VelocityMps;
    const FVector3d relativeMps = velocityMps - SOLRender::EclipticToUnreal(mShips->GetReferenceVelocityMps());
    mFlyBy.RelativeSpeedMps = relativeMps.Size();
    const double closestS = FMath::Clamp(2.0 * mNpcs->GetStats().FullRadiusM / FMath::Max(mFlyBy.RelativeSpeedMps,
        1.0), NPCTRAFFIC_V2_MIN_CLOSEST_S, NPCTRAFFIC_V2_MAX_CLOSEST_S);
    SetupShip(positionM + relativeMps * closestS + laneFrame.Up * NPCTRAFFIC_V2_MISS_M, -relativeMps.GetSafeNormal());
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTraffic: V2 fly-by of ship %d, %.3f of the lane from its middle, %.1f km/s "
        "relative, closest in %.2f s (Full radius %.0f km)"), mFlyBy.Npc, bestOffset,
        mFlyBy.RelativeSpeedMps / 1000.0, closestS, mNpcs->GetStats().FullRadiusM / 1000.0);
}

//////////////////////////////////////////////////////////////////////////
// V2, every frame of the fly-by: the drawn Mid and Full lane hulls against LaneHullForward, and the tracked ship's
// per-frame nose change (promotion and demotion frames apart from the others)
void FSOLNpcTrafficSmoke::TrackFlyBy()
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const FVector3d viewpointM = mAnchor->GetViewpointM();
    const uint8 laneKind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
    const uint8 mid = static_cast<uint8>(SOLNpcScale::EAiTier::Mid);
    const uint8 full = static_cast<uint8>(SOLNpcScale::EAiTier::Full);
    const FSOLNpcCpuShip* tracked = nullptr;
    for (const FSOLNpcCpuShip& ship : mNpcs->GetCpuShips())
    {
        if (ship.HeadingDirection.IsNearlyZero() || !rails.IsValidIndex(ship.NpcIndex)
            || rails[ship.NpcIndex].Kind != laneKind || (ship.AiTier != mid && ship.AiTier != full))
        {
            continue;
        }
        tracked = ship.NpcIndex == mFlyBy.Npc ? &ship : tracked;

        // Mid hulls within the deviation velocity's turn; Full hulls (the nose, aiming near a target) counted
        const FVector3d laneForward = SOLNpcRail::LaneHullForward(rails[ship.NpcIndex], frame, ship.NpcIndex,
            frame.RailTimeS);
        const double offDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            FVector3d(ship.HeadingDirection).GetSafeNormal() | laneForward, -1.0, 1.0)));
        if (ship.AiTier == mid)
        {
            ++mFlyBy.MidHulls;
            mFlyBy.MidWorstDeg = FMath::Max(mFlyBy.MidWorstDeg, offDeg);
            continue;
        }
        ++mFlyBy.FullHulls;
        mFlyBy.FullWorstDeg = FMath::Max(mFlyBy.FullWorstDeg, offDeg);
        mFlyBy.FullAligned += offDeg <= NPCTRAFFIC_FULL_HEADING_DEG ? 1 : 0;
        mFlyBy.FullReversed += offDeg > 90.0 ? 1 : 0;
    }

    // The tracked ship: its nose change since last frame, by the kind of frame
    if (tracked == nullptr)
    {
        mFlyBy.PrevTier = 0xFF;
        return;
    }
    ++mFlyBy.Frames;
    mFlyBy.ClosestM = FMath::Min(mFlyBy.ClosestM, (tracked->PositionM - viewpointM).Size());
    if (mFlyBy.PrevTier != 0xFF)
    {
        const double changeDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(static_cast<double>(
            tracked->HeadingDirection.GetSafeNormal() | mFlyBy.PrevHeading.GetSafeNormal()), -1.0, 1.0)));
        if (mFlyBy.PrevTier == mid && tracked->AiTier == full)
        {
            ++mFlyBy.Promotions;
            mFlyBy.PromoteDeg = FMath::Max(mFlyBy.PromoteDeg, changeDeg);
        }
        else if (mFlyBy.PrevTier == full && tracked->AiTier == mid)
        {
            ++mFlyBy.Demotions;
            mFlyBy.DemoteDeg = FMath::Max(mFlyBy.DemoteDeg, changeDeg);
        }
        else
        {
            mFlyBy.FrameDeg = FMath::Max(mFlyBy.FrameDeg, changeDeg);
        }
    }
    mFlyBy.PrevTier = tracked->AiTier;
    mFlyBy.PrevHeading = tracked->HeadingDirection;
}

//////////////////////////////////////////////////////////////////////////
// V4: the share of all lane ships beyond their lane's wall radius (the halo, D6), per lane against the in-wall count
void FSOLNpcTrafficSmoke::CheckHalo()
{
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    const uint8 laneKind = static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane);
    TArray<int32> halo;
    TArray<int32> total;
    halo.SetNumZeroed(groups.LaneCount);
    total.SetNumZeroed(groups.LaneCount);
    for (const FSOLNpcRail& rail : mNpcs->GetRails())
    {
        if (rail.Kind == laneKind && groups.LaneProfile.IsValidIndex(rail.Group))
        {
            ++total[rail.Group];
            halo[rail.Group] += TrafficSmokeRho(rail, groups.LaneProfile[rail.Group]) > 1.0 ? 1 : 0;
        }
    }
    int32 haloSum = 0;
    int32 totalSum = 0;
    FString detail;
    for (int32 lane = 0; lane < groups.LaneCount; ++lane)
    {
        haloSum += halo[lane];
        totalSum += total[lane];
        const int32 inWall = groups.LaneInWallCount.IsValidIndex(lane) ? groups.LaneInWallCount[lane] : -1;
        detail += FString::Printf(TEXT(" [lane %d: %.1f %% of %d, in-wall %d vs table %d]"), lane, total[lane] > 0
            ? 100.0 * halo[lane] / total[lane] : 0.0, total[lane], total[lane] - halo[lane], inWall);
    }
    const double share = totalSum > 0 ? static_cast<double>(haloSum) / totalSum : 0.0;
    Check(TEXT("V4-halo-share"), totalSum > 0 && FMath::Abs(share - SOL::NPC_LANE_HALO_FRACTION)
        <= NPCTRAFFIC_HALO_TOLERANCE, FString::Printf(TEXT("%.2f %% of all %d lane ships beyond Rw (expected %.0f +- "
        "%.0f %%):%s"), 100.0 * share, totalSum, 100.0 * SOL::NPC_LANE_HALO_FRACTION, 100.0 * NPCTRAFFIC_HALO_TOLERANCE,
        *detail));
}

//////////////////////////////////////////////////////////////////////////
// V4: every shown tunnel's radius, in the feed and in its texel (row 0 .w), equals the lane's frozen wall radius
void FSOLNpcTrafficSmoke::CheckTunnelRadius()
{
    const USOLNpcTunnelComponent* tunnels = mVisuals->GetTunnels();
    const TConstArrayView<FVector4f> texels = tunnels->GetLastTexels();
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    int32 shown = 0;
    bool bOk = texels.Num() >= SOL::NPC_GROUP_TEXTURE_WIDTH;
    bool bEarthMars = false;
    FString detail;
    for (const FSOLNpcTunnelLaneData& data : tunnels->GetShownLanes())
    {
        if (!groups.LaneProfile.IsValidIndex(data.Lane) || !texels.IsValidIndex(data.Lane))
        {
            bOk = false;
            continue;
        }
        const double wallM = groups.LaneProfile[data.Lane].WallRadiusM;
        const float texelM = texels[data.Lane].W;
        bOk &= data.TunnelRadiusM == wallM && texelM == static_cast<float>(wallM);
        bEarthMars |= data.Lane == mLane;
        ++shown;
        detail += FString::Printf(TEXT(" [lane %d: Rw %.1f km, feed %.1f km, texel %.1f km]"), data.Lane, wallM / 1000.0,
            data.TunnelRadiusM / 1000.0, texelM / 1000.0);
    }
    Check(TEXT("V4-tunnel-radius"), bOk && bEarthMars, FString::Printf(TEXT("%d tunnels shown (Earth-Mars %s), each at "
        "its frozen wall radius:%s"), shown, bEarthMars ? TEXT("yes") : TEXT("NO"), *detail));
}

//////////////////////////////////////////////////////////////////////////
// V1: every sampled leg ship travels its leg's way on its side (outbound right of the axis, inbound left); the
// Earth-Mars ones ahead of the camera and clear of the axis are on that half of the screen
void FSOLNpcTrafficSmoke::CheckKeepRight()
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const double timeS = frame.RailTimeS;
    int32 legs = 0;
    int32 turns = 0;
    int32 wrong = 0;
    FString firstWrong;
    for (int32 lane = 0; lane < mSamples.Num(); ++lane)
    {
        if (!frame.LaneFrame.IsValidIndex(lane) || !frame.LaneFrame[lane].bValid || !frame.LaneProfile[lane].bDrivable)
        {
            continue;
        }
        const FSOLLaneFrame& laneFrame = frame.LaneFrame[lane];
        for (const int32 index : mSamples[lane])
        {
            const FSOLNpcRail& rail = rails[index];
            const FSOLLaneThrustState thrust = SOLLaneTraffic::LaneThrustAt(frame.LaneProfile[lane], laneFrame,
                SOLNpcRail::LaneShipFromRail(rail), static_cast<uint32>(index), timeS);
            if (TrafficSmokeIsTurn(thrust.Segment))
            {
                ++turns;
                continue;
            }
            ++legs;
            const double sign = thrust.Segment == ESOLLaneSegment::Outbound ? 1.0 : -1.0;
            const double lateralM = (SOLNpcRail::EvaluateAtTimeM(rail, frame, timeS) - laneFrame.AM)
                | laneFrame.Right;
            const double alongMps = SOLNpcRail::VelocityMps(rail, frame) | laneFrame.Axis;
            if (lateralM * sign < -NPCTRAFFIC_LATERAL_TOLERANCE_M || alongMps * sign <= 0.0)
            {
                if (wrong++ == 0)
                {
                    firstWrong = FString::Printf(TEXT("lane %d ship %d: lateral %.0f m, along %.0f m/s"), lane, index,
                        lateralM, alongMps);
                }
            }
        }
    }
    Check(TEXT("V1-keep-right"), legs > 0 && wrong == 0, FString::Printf(TEXT("%d sampled leg ships on %d lanes, %d on "
        "the wrong side or going the wrong way%s%s (%d in U-turns skipped)"), legs, mSamples.Num(), wrong,
        wrong > 0 ? TEXT("; first: ") : TEXT(""), *firstWrong, turns));

    // On screen: the sampled Earth-Mars ships ahead of the camera, projected a short way out along their direction
    FVector2D viewSize(0.0, 0.0);
    if (mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(viewSize);
    }
    if (!mController.IsValid() || mController->PlayerCameraManager == nullptr)
    {
        Check(TEXT("V1-screen-sides"), false, TEXT("no camera"));
        return;
    }
    const FVector cameraCm = mController->PlayerCameraManager->GetCameraLocation();
    const FVector3d cameraM = mAnchor->RenderToUniverseM(cameraCm);
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[mLane];
    const double minLateralM = NPCTRAFFIC_SIDE_MIN_RADII * frame.LaneProfile[mLane].WallRadiusM;
    int32 projected = 0;
    int32 rightSide = 0;
    int32 leftSide = 0;
    int32 wrongScreen = 0;
    mShotPositionsM.Reset(mSamples[mLane].Num());
    for (const int32 index : mSamples[mLane])
    {
        const FSOLNpcRail& rail = rails[index];
        const FVector3d positionM = SOLNpcRail::EvaluateAtTimeM(rail, frame, timeS);
        mShotPositionsM.Add(positionM);
        const FSOLLaneThrustState thrust = SOLLaneTraffic::LaneThrustAt(frame.LaneProfile[mLane], laneFrame,
            SOLNpcRail::LaneShipFromRail(rail), static_cast<uint32>(index), timeS);
        const double lateralM = (positionM - laneFrame.AM) | laneFrame.Right;
        FVector2D screen;
        const FVector direction = (mAnchor->UniverseToRenderCm(positionM) - cameraCm).GetSafeNormal();
        if (TrafficSmokeIsTurn(thrust.Segment) || FMath::Abs(lateralM) < minLateralM
            || ((positionM - cameraM) | laneFrame.Axis) <= 0.0
            || !mController->ProjectWorldLocationToScreen(cameraCm + direction * NPCTRAFFIC_PROJECT_CM, screen))
        {
            continue;
        }
        ++projected;
        const bool bRight = screen.X > 0.5 * viewSize.X;
        ++(bRight ? rightSide : leftSide);
        wrongScreen += bRight != (thrust.Segment == ESOLLaneSegment::Outbound) ? 1 : 0;
    }
    mShotRailTimeS = timeS;
    Check(TEXT("V1-screen-sides"), projected >= 5 && wrongScreen == 0 && rightSide > 0 && leftSide > 0,
        FString::Printf(TEXT("%d sampled Earth-Mars ships ahead and off the axis projected: %d right (outbound), %d "
        "left (inbound), %d on the wrong half"), projected, rightSide, leftSide, wrongScreen));
}

//////////////////////////////////////////////////////////////////////////
// V1: between the two screenshots every sampled Earth-Mars leg ship moved at least 0.9 w dt
void FSOLNpcTrafficSmoke::CheckMotion()
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const double dtS = frame.RailTimeS - mShotRailTimeS;
    int32 legs = 0;
    int32 still = 0;
    double minSpeedMps = TNumericLimits<double>::Max();
    double maxSpeedMps = 0.0;

    // The slowest a leg ship can move relative to the lane: w, times the metric map's mid-lane slope (l - Z) / (l0 - Z)
    // when the lane is shorter than nominal (SDD 50 section 4.5: the middle slows while the bodies are close)
    const FSOLLaneProfile& profile = frame.LaneProfile[mLane];
    const double l0 = profile.NominalLegM;
    const double z = profile.MetricZoneM;
    const double midSlope = l0 > z ? (frame.LaneFrame[mLane].CurrentLegM - z) / (l0 - z) : 1.0;
    const double minLaneSpeedMps = SOL::NPC_LANE_WALL_SPEED_MPS * FMath::Clamp(midSlope, 0.0, 1.0);
    for (int32 sample = 0; sample < mShotPositionsM.Num() && sample < mSamples[mLane].Num(); ++sample)
    {
        const FSOLNpcRail& rail = rails[mSamples[mLane][sample]];
        const FSOLLaneShip ship = SOLNpcRail::LaneShipFromRail(rail);
        const uint32 key = static_cast<uint32>(mSamples[mLane][sample]);
        if (TrafficSmokeIsTurn(SOLLaneTraffic::LaneThrustAt(frame.LaneProfile[mLane], frame.LaneFrame[mLane], ship,
            key, mShotRailTimeS).Segment) || TrafficSmokeIsTurn(SOLLaneTraffic::LaneThrustAt(
            frame.LaneProfile[mLane], frame.LaneFrame[mLane], ship, key, frame.RailTimeS).Segment))
        {
            continue;
        }
        ++legs;

        // Relative to the lane: both times evaluated on this frame's body positions (the bodies' own motion removed)
        const double speedMps = dtS > 0.0 ? (SOLNpcRail::EvaluateAtTimeM(rail, frame, frame.RailTimeS)
            - SOLNpcRail::EvaluateAtTimeM(rail, frame, mShotRailTimeS)).Size() / dtS : 0.0;
        minSpeedMps = FMath::Min(minSpeedMps, speedMps);
        maxSpeedMps = FMath::Max(maxSpeedMps, speedMps);
        still += speedMps < NPCTRAFFIC_MOTION_FRACTION * minLaneSpeedMps ? 1 : 0;
    }
    Check(TEXT("V1-motion"), dtS >= NPCTRAFFIC_MIN_SHOT_GAP_S && legs > 0 && still == 0, FString::Printf(TEXT("rail "
        "time %.2f s between the shots, %d sampled Earth-Mars leg ships moved along the lane at %.1f - %.1f km/s (%d "
        "below 0.9 x the slowest lane speed %.1f km/s, mid-lane slope %.3f)"), dtS, legs,
        legs > 0 ? minSpeedMps / 1000.0 : 0.0, maxSpeedMps / 1000.0, still, minLaneSpeedMps / 1000.0, midSlope));
}

//////////////////////////////////////////////////////////////////////////
// V2: Mid-tier ships (or sampled rails) mid-lane at the speed field within the effort spread, ranked against rho,
// wall ships at w, and the hull headings against LaneHullForward
void FSOLNpcTrafficSmoke::CheckSpeeds()
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const FSOLLaneProfile& profile = frame.LaneProfile[mLane];
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[mLane];
    const double timeS = frame.RailTimeS;
    const double w = SOL::NPC_LANE_WALL_SPEED_MPS;
    const double l0 = profile.NominalLegM;
    const double l = laneFrame.CurrentLegM;
    const double stepS = SOLNpcRail::RAIL_VELOCITY_STEP_S;

    // The leg speed of a rail (the central difference over the metric map's slope) and its arc on the nominal leg
    const auto legSpeed = [&](const FSOLNpcRail& rail, double& outArcM)
    {
        const FVector3d positionM = SOLNpcRail::EvaluateAtTimeM(rail, frame, timeS);
        const double fromRingM = ((positionM - laneFrame.AM) | laneFrame.Axis) - profile.EndArcAM;
        outArcM = SOLLaneTraffic::MetricMapInverseM(fromRingM, l0, l);
        const double measuredMps = (SOLNpcRail::EvaluateAtTimeM(rail, frame, timeS + stepS)
            - SOLNpcRail::EvaluateAtTimeM(rail, frame, timeS - stepS)).Size() / (2.0 * stepS);
        return measuredMps / SOLLaneTraffic::MetricMapSlope(outArcM, l0, l);
    };
    // Returns whether a rail's ship is on a leg (not in a U-turn) now; the segment does not depend on the ship's key
    const auto isLeg = [&](const FSOLNpcRail& rail)
    {
        return !TrafficSmokeIsTurn(SOLLaneTraffic::LaneThrustAt(profile, laneFrame, SOLNpcRail::LaneShipFromRail(rail),
            0u, timeS).Segment);
    };

    // The Mid-tier Earth-Mars leg ships, or sampled rails mid-lane when too few are near
    TArray<int32> candidates;
    for (const FSOLNpcCpuShip& ship : mNpcs->GetCpuShips())
    {
        if (ship.AiTier == static_cast<uint8>(SOLNpcScale::EAiTier::Mid) && rails.IsValidIndex(ship.NpcIndex)
            && rails[ship.NpcIndex].Kind == static_cast<uint8>(SOLNpcScale::EPlacementKind::Lane)
            && rails[ship.NpcIndex].Group == mLane && isLeg(rails[ship.NpcIndex]))
        {
            candidates.Add(ship.NpcIndex);
        }
    }
    const bool bMid = candidates.Num() >= NPCTRAFFIC_MIN_MID_SHIPS;
    const int32 midShips = candidates.Num();
    if (!bMid)
    {
        candidates.Reset();
        const int32 stride = FMath::Max(1, mLaneShips.Num() / (8 * NPCTRAFFIC_SPEED_SAMPLES));
        for (int32 index = 0; index < mLaneShips.Num() && candidates.Num() < NPCTRAFFIC_SPEED_SAMPLES; index += stride)
        {
            const FSOLNpcRail& rail = rails[mLaneShips[index]];
            const double fraction = (((SOLNpcRail::EvaluateAtTimeM(rail, frame, timeS) - laneFrame.AM)
                | laneFrame.Axis) - profile.EndArcAM) / l;
            if (fraction >= NPCTRAFFIC_MID_LANE_MIN && fraction <= NPCTRAFFIC_MID_LANE_MAX && isLeg(rail))
            {
                candidates.Add(mLaneShips[index]);
            }
        }
    }

    // Each candidate against its effort envelope; the in-wall ones ranked against rho and compared with the mean field
    TArray<double> rhos;
    TArray<double> speeds;
    int32 outside = 0;
    double meanDeviation = 0.0;
    FString firstOutside;
    for (const int32 index : candidates)
    {
        const FSOLNpcRail& rail = rails[index];
        const double rho = TrafficSmokeRho(rail, profile);
        double arcM = 0.0;
        const double speedMps = legSpeed(rail, arcM);
        const double kappa = SOLLaneTraffic::RadialFactor(rho);
        const double lowMps = SOLLaneTraffic::LegSpeedAtArcMps(SOLLaneTraffic::MakeLegProfile(kappa,
            SOL::NPC_LANE_EFFORT_MIN, l0), l0, arcM);
        const double highMps = SOLLaneTraffic::LegSpeedAtArcMps(SOLLaneTraffic::MakeLegProfile(kappa,
            SOL::NPC_LANE_EFFORT_MAX, l0), l0, arcM);
        if (speedMps < lowMps * (1.0 - NPCTRAFFIC_ENVELOPE_TOLERANCE)
            || speedMps > highMps * (1.0 + NPCTRAFFIC_ENVELOPE_TOLERANCE))
        {
            if (outside++ == 0)
            {
                firstOutside = FString::Printf(TEXT("; first: ship %d rho %.3f at %.3f l0: %.1f km/s, spread %.1f - %.1f"),
                    index, rho, arcM / l0, speedMps / 1000.0, lowMps / 1000.0, highMps / 1000.0);
            }
        }
        if (rho <= 1.0)
        {
            rhos.Add(rho);
            speeds.Add(speedMps);
            const double meanMps = SOLLaneTraffic::MeanSpeedMps(arcM, rho, l0);
            meanDeviation += FMath::Abs(speedMps - meanMps) / meanMps;
        }
    }
    const double spearman = TrafficSmokeSpearman(rhos, speeds);
    const FString source = bMid ? FString::Printf(TEXT("%d Mid-tier Earth-Mars leg ships"), midShips)
        : FString::Printf(TEXT("only %d Mid-tier Earth-Mars ships near, so %d sampled rails mid-lane evaluated directly"),
            midShips, candidates.Num());
    Check(TEXT("V2-speed-field"), candidates.Num() >= 10 && outside == 0, FString::Printf(TEXT("%s: %d outside their "
        "effort spread%s; in-wall mean |v - MeanSpeedMps| / MeanSpeedMps %.1f %%"), *source, outside, *firstOutside,
        rhos.Num() > 0 ? 100.0 * meanDeviation / rhos.Num() : 0.0));
    Check(TEXT("V2-rank-rho"), rhos.Num() >= 10 && spearman < NPCTRAFFIC_RHO_LIMIT, FString::Printf(TEXT("Spearman "
        "rank correlation of speed with rho %.3f over %d in-wall ships (limit %.1f)"), spearman, rhos.Num(),
        NPCTRAFFIC_RHO_LIMIT));

    // Wall ships (radial factor low enough for a peak of at most 1.05 w) at w +- 10 %
    const double c = SOL::NPC_LANE_SPEED_CAP_MPS;
    const double wallKappa = (FMath::Square(NPCTRAFFIC_WALL_PEAK) - 1.0) * w * w / (c * c - w * w);
    int32 wallShips = 0;
    int32 wallOff = 0;
    double wallMin = TNumericLimits<double>::Max();
    double wallMax = 0.0;
    for (const int32 index : mLaneShips)
    {
        const FSOLNpcRail& rail = rails[index];
        if (SOLLaneTraffic::RadialFactor(TrafficSmokeRho(rail, profile)) > wallKappa || !isLeg(rail))
        {
            continue;
        }
        double arcM = 0.0;
        const double speedMps = legSpeed(rail, arcM);
        ++wallShips;
        wallMin = FMath::Min(wallMin, speedMps);
        wallMax = FMath::Max(wallMax, speedMps);
        wallOff += FMath::Abs(speedMps - w) > NPCTRAFFIC_WALL_TOLERANCE * w ? 1 : 0;
    }
    Check(TEXT("V2-wall-speed"), wallShips >= NPCTRAFFIC_MIN_WALL_SHIPS && wallOff == 0, FString::Printf(TEXT("%d "
        "Earth-Mars wall ships (radial factor <= %.2e, rails evaluated directly) at %.2f - %.2f km/s, %d outside w +- "
        "10 %%"), wallShips, wallKappa, wallShips > 0 ? wallMin / 1000.0 : 0.0, wallMax / 1000.0, wallOff));

    // Hulls over the fly-by (TrackFlyBy, every frame): Mid-tier lane hulls face LaneHullForward (within the deviation
    // velocity's turn). The Full step steers the nose along the rail hull heading (D21), so a Full lane ship faces
    // LaneHullForward too (engine first while braking); ships near a target aim at it, so at least 80 % (not all) of
    // the Full hull frames must be within the limit. At least one hull of each tier must have been drawn
    const FFlyBy& flyBy = mFlyBy;
    Check(TEXT("V2-full-hull-heading"), flyBy.FullHulls > 0 && flyBy.FullAligned * 5 >= flyBy.FullHulls * 4,
        FString::Printf(TEXT("%d Full-tier lane hull frames over the fly-by: %d within %.0f deg of LaneHullForward, %d "
        "reversed (over 90 deg), worst %.1f deg"), flyBy.FullHulls, flyBy.FullAligned, NPCTRAFFIC_FULL_HEADING_DEG,
        flyBy.FullReversed, flyBy.FullWorstDeg));

    // The tracked ship's nose across its promotion and demotion: no turn in the hand-off frames
    Check(TEXT("V2-handoff-nose"), flyBy.Promotions > 0 && flyBy.Demotions > 0
        && flyBy.PromoteDeg <= NPCTRAFFIC_HANDOFF_DEG && flyBy.DemoteDeg <= NPCTRAFFIC_HANDOFF_DEG,
        FString::Printf(TEXT("ship %d flown past at %.1f km/s, closest %.1f km, %d frames drawn as a hull: %d promotions "
        "(nose change worst %.3f deg), %d demotions (worst %.3f deg), other frames worst %.3f deg (limit %.1f deg)"),
        flyBy.Npc, flyBy.RelativeSpeedMps / 1000.0, flyBy.Frames > 0 ? flyBy.ClosestM / 1000.0 : 0.0, flyBy.Frames,
        flyBy.Promotions, flyBy.PromoteDeg, flyBy.Demotions, flyBy.DemoteDeg, flyBy.FrameDeg,
        NPCTRAFFIC_HANDOFF_DEG));

    // Rails: the heading chain (HullHeadingVelocityMps -> HullForward) equals LaneHullForward; nose first before the
    // flip window, engine first after it (and while braking), the brake attitude held through the U-turns
    int32 chainBad = 0;
    int32 nose = 0;
    int32 engine = 0;
    int32 flipping = 0;
    int32 held = 0;
    int32 attitudeBad = 0;
    const double chainDot = FMath::Cos(FMath::DegreesToRadians(NPCTRAFFIC_RAIL_HEADING_DEG));
    const int32 stride = FMath::Max(1, mLaneShips.Num() / NPCTRAFFIC_HEADING_SAMPLES);
    for (int32 sample = 0; sample < mLaneShips.Num(); ++sample)
    {
        const FSOLNpcRail& rail = rails[mLaneShips[sample]];
        const int32 npcIndex = mLaneShips[sample];
        const FSOLLaneThrustState thrust = SOLLaneTraffic::LaneThrustAt(profile, laneFrame,
            SOLNpcRail::LaneShipFromRail(rail), static_cast<uint32>(npcIndex), timeS);
        const bool bTurn = TrafficSmokeIsTurn(thrust.Segment);
        if (!bTurn && sample % stride != 0)
        {
            continue;
        }
        const FVector3d laneForward = SOLNpcRail::LaneHullForward(rail, frame, npcIndex, timeS);
        const FVector3d hull = SOLNpcAi::HullForward(SOLNpcRail::HullHeadingVelocityMps(SOLNpcRail::Motion(rail,
            frame, npcIndex), FVector3d::ZeroVector),
            SOLNpcRail::EvaluateAtTimeM(rail, frame, timeS) - frame.GroupBodyM[mLane]);
        chainBad += (hull | laneForward) < chainDot ? 1 : 0;
        if (bTurn)
        {
            // A braking ship holds its brake attitude: -Axis at B, +Axis at A
            if (thrust.FlipFraction >= 1.0)
            {
                ++held;
                const FVector3d attitude = thrust.Segment == ESOLLaneSegment::TurnAtB ? -laneFrame.Axis : laneFrame.Axis;
                attitudeBad += (laneForward | attitude) < NPCTRAFFIC_HULL_DOT ? 1 : 0;
            }
            continue;
        }
        const FVector3d travel = thrust.Segment == ESOLLaneSegment::Outbound ? laneFrame.Axis : -laneFrame.Axis;
        if (thrust.FlipFraction <= 0.0)
        {
            ++nose;
            attitudeBad += (laneForward | travel) < NPCTRAFFIC_HULL_DOT || thrust.Thrust == ESOLLaneThrust::Brake ? 1 : 0;
        }
        else if (thrust.FlipFraction >= 1.0)
        {
            ++engine;
            attitudeBad += (laneForward | travel) > -NPCTRAFFIC_HULL_DOT
                || thrust.Thrust == ESOLLaneThrust::Accelerate ? 1 : 0;
        }
        else
        {
            ++flipping;
        }
    }
    Check(TEXT("V2-hull-heading"), flyBy.MidHulls > 0 && flyBy.MidWorstDeg <= NPCTRAFFIC_MID_HEADING_DEG
        && chainBad == 0 && attitudeBad == 0 && nose > 0 && engine > 0 && held > 0, FString::Printf(TEXT("%d Mid-tier "
        "lane hull frames over the fly-by, worst %.3f deg off LaneHullForward; sampled Earth-Mars rails (and every "
        "U-turn ship): heading chain off LaneHullForward on %d, nose first %d, engine first %d, flipping %d, brake "
        "attitude held in U-turns %d, %d with the wrong attitude"), flyBy.MidHulls, flyBy.MidWorstDeg, chainBad, nose,
        engine, flipping, held, attitudeBad));
}

//////////////////////////////////////////////////////////////////////////
// V3: every ship turning at Earth (and at Mars) lies beyond its end ring; the closest one at Earth is outside the
// planet by the D14 clearance m Rw - D_U
void FSOLNpcTrafficSmoke::CheckUTurn()
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const FSOLLaneProfile& profile = frame.LaneProfile[mLane];
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[mLane];
    const double earthRadiusM = mRegistry->GetRegistry().GetRadiusM(mNpcs->GetGroups().LaneBodyA[mLane]);
    const double rw = profile.WallRadiusM;
    const double expectedM = SOL::NPC_TUNNEL_END_MARGIN_RADII * rw - profile.UTurnDepthM;
    int32 atEarth = 0;
    int32 atMars = 0;
    int32 inside = 0;
    double minClearanceM = TNumericLimits<double>::Max();
    for (const int32 index : mLaneShips)
    {
        const FSOLNpcRail& rail = rails[index];
        const ESOLLaneSegment segment = SOLLaneTraffic::LaneThrustAt(profile, laneFrame,
            SOLNpcRail::LaneShipFromRail(rail), static_cast<uint32>(index), frame.RailTimeS).Segment;
        if (!TrafficSmokeIsTurn(segment))
        {
            continue;
        }
        const FVector3d positionM = SOLNpcRail::EvaluateAtTimeM(rail, frame, frame.RailTimeS);
        if (segment == ESOLLaneSegment::TurnAtA)
        {
            ++atEarth;
            inside += ((positionM - laneFrame.AM) | laneFrame.Axis) > profile.EndArcAM + 1.0 ? 1 : 0;
            minClearanceM = FMath::Min(minClearanceM, (positionM - laneFrame.AM).Size() - earthRadiusM);
        }
        else
        {
            ++atMars;
            inside += ((laneFrame.BM - positionM) | laneFrame.Axis) > profile.EndArcBM + laneFrame.EndShiftM + 1.0
                ? 1 : 0;
        }
    }
    const double toleranceM = NPCTRAFFIC_CLEARANCE_TOLERANCE_RADII * rw + 1.0;
    Check(TEXT("V3-u-turn"), atEarth > 0 && inside == 0 && minClearanceM >= expectedM - toleranceM,
        FString::Printf(TEXT("%d ships turning at Earth, %d at Mars, %d inside their end ring; closest turning ship "
        "%.0f km above Earth's surface = %.3f Rw (D14 clearance m Rw - D_U = %.0f km, Rw %.0f km)"), atEarth, atMars,
        inside, atEarth > 0 ? minClearanceM / 1000.0 : 0.0, atEarth > 0 ? minClearanceM / rw : 0.0,
        expectedM / 1000.0, rw / 1000.0));
}

//////////////////////////////////////////////////////////////////////////
// V5: the uploaded group rows match the frame; for the ships nearest the Mid radius the float mirror of the point
// shader (the visuals' rows, the rail re-based to the GPU epoch, the shader's time) agrees with the CPU rail within case
// 10's bound and is drawn exactly once (by the tiers' Mid radius plus the lane's hand-over margin); with bSmooth the
// end-zone and U-turn ships nearest the viewpoint step smoothly at 60 Hz for 5 s, otherwise the hand-over ships' float
// steps are only reported
void FSOLNpcTrafficSmoke::CheckHandOver(const TCHAR* label, const bool bSmooth)
{
    const FSOLNpcRailFrame& frame = mNpcs->GetRailFrame();
    const TConstArrayView<FSOLNpcRail> rails = mNpcs->GetRails();
    const FSOLNpcGroupTable& groups = mNpcs->GetGroups();
    const FSOLLaneProfile& profile = frame.LaneProfile[mLane];
    const FSOLLaneFrame& laneFrame = frame.LaneFrame[mLane];
    const TConstArrayView<FVector4f> texels = mVisuals->GetLastGroupTexels();
    const int32 width = SOL::NPC_GROUP_TEXTURE_WIDTH;
    if (texels.Num() < width * SOL::NPC_GROUP_TEXTURE_ROWS)
    {
        Check(label, false, TEXT("no group rows uploaded"));
        return;
    }

    // The lane column as uploaded; rows 2-5 against WriteLaneTrafficTexels of this frame (bit for bit)
    FVector4f rows[SOL::NPC_GROUP_TEXTURE_ROWS];
    TArray<FVector4f> scratch;
    scratch.Init(FVector4f(0.0f, 0.0f, 0.0f, 0.0f), width * SOL::NPC_GROUP_TEXTURE_ROWS);
    SOLNpcRail::WriteLaneTrafficTexels(frame, scratch);
    bool bRowsMatch = true;
    for (int32 row = 0; row < SOL::NPC_GROUP_TEXTURE_ROWS; ++row)
    {
        rows[row] = texels[row * width + mLane];
        bRowsMatch &= row < 2 || rows[row] == scratch[row * width + mLane];
    }

    // The viewpoint the rows were written for: the anchor's when it reproduces row 0 exactly, else A - row 0
    const FVector3d anchorM = mAnchor->GetViewpointM();
    const FVector3f anchorRow(laneFrame.AM - anchorM);
    const bool bAnchorMatch = anchorRow.X == rows[0].X && anchorRow.Y == rows[0].Y && anchorRow.Z == rows[0].Z;
    const FVector3d viewpointM = bAnchorMatch ? anchorM
        : laneFrame.AM - FVector3d(rows[0].X, rows[0].Y, rows[0].Z);
    const double epochS = mVisuals->GetGpuRailEpochS();
    const float shaderTimeS = static_cast<float>(frame.RailTimeS - epochS);
    const double slope = FMath::Max(1.0, (laneFrame.CurrentLegM - profile.MetricZoneM)
        / (profile.NominalLegM - profile.MetricZoneM));
    const double midRadiusM = mNpcs->GetStats().MidRadiusM;
    const double hullStartM = mNpcs->GetStats().InstancedRadiusM;
    const double bandMinM = NPCTRAFFIC_BAND_MIN * midRadiusM;
    const double bandMaxM = NPCTRAFFIC_BAND_MAX * midRadiusM;

    // One pixel at the Mid radius (the chase camera's horizontal field of view over the viewport width)
    FVector2D viewportSize(1920.0, 1080.0);
    if (mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(viewportSize);
    }
    const double fovDeg = mController.IsValid() && mController->PlayerCameraManager != nullptr
        ? static_cast<double>(mController->PlayerCameraManager->GetFOVAngle()) : 90.0;
    const double pixelAtMidM = midRadiusM * 2.0 * FMath::Tan(FMath::DegreesToRadians(0.5 * fovDeg))
        / FMath::Max(viewportSize.X, 1.0);

    // The hand-over set: the Earth-Mars ships within the band around the Mid radius now (nearest the radius in log
    // distance first, the uploaded rows), then each sampled Earth-Mars ship seen from a viewpoint that makes it cross
    // the Mid radius half a second in, inward and outward in turn (rows 0 and 1 written for that viewpoint as the
    // visuals write them, rows 2-5 as uploaded)
    struct FHandOverCase
    {
        int32 Index;
        FVector3d ViewpointM;
        bool bReal;
    };
    TArray<TPair<double, int32>> byShell;
    byShell.Reserve(mLaneShips.Num());
    for (const int32 index : mLaneShips)
    {
        const double distanceM = (SOLNpcRail::EvaluateM(rails[index], frame) - viewpointM).Size();
        if (distanceM >= bandMinM && distanceM <= bandMaxM)
        {
            byShell.Emplace(FMath::Abs(FMath::Loge(distanceM / FMath::Max(midRadiusM, 1.0))), index);
        }
    }
    Algo::Sort(byShell, [](const TPair<double, int32>& a, const TPair<double, int32>& b) { return a.Key < b.Key; });
    TArray<FHandOverCase> cases;
    for (int32 sample = 0; sample < FMath::Min(byShell.Num(), NPCTRAFFIC_HANDOVER_SAMPLES); ++sample)
    {
        cases.Add({ byShell[sample].Value, viewpointM, true });
    }
    const int32 realCount = cases.Num();
    const float crossTimeS = static_cast<float>(static_cast<double>(shaderTimeS) + 0.5);
    for (int32 sample = 0; sample < mSamples[mLane].Num(); ++sample)
    {
        const FSOLNpcRail gpuRail = SOLNpcRail::AdvancedToEpoch(rails[mSamples[mLane][sample]], epochS,
            groups.LaneProfile);
        const FVector3d crossM = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(crossTimeS));
        const FVector3d direction = (SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, crossTimeS + 0.05)
            - SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, crossTimeS - 0.05)).GetSafeNormal();
        if (!direction.IsZero())
        {
            cases.Add({ mSamples[mLane][sample], crossM + direction * ((sample & 1) != 0 ? -midRadiusM : midRadiusM),
                false });
        }
    }

    // Each case: the mirror against the CPU rail now (case 10's bound), then 1 s at 60 Hz: the float steps (reported)
    // and the drawn point (the far point beyond the radius, the visuals' CPU point blend inside) against the far point
    double worstErrorM = 0.0;
    double worstRatio = 0.0;
    double worstEpochM = 0.0;
    double nearestM = TNumericLimits<double>::Max();
    double farthestM = 0.0;
    int32 failures = 0;
    int32 outOfBand = 0;
    int32 repeatFrames = 0;
    double worstStepDiffM = 0.0;
    double worstStepDistanceM = 0.0;
    int32 crossings = 0;
    double worstHandOverPx = 0.0;
    double minRawPopPx = TNumericLimits<double>::Max();
    double maxRawPopPx = 0.0;
    int32 floatOnlyFrames = 0;
    int32 worstFloatOnlyRun = 0;
    int32 handOverFrames = 0;

    // The tiers' view of the lane: a lane ship is a CPU ship within the Mid radius plus the lane's hand-over margin
    // (the subsystem's own margin this frame), with the AI hysteresis; Full stays inside Mid
    const TConstArrayView<double> margins = mNpcs->GetGroupMidMarginsM();
    const double marginM = margins.IsValidIndex(mLane) ? margins[mLane] : 0.0;
    const double fullRadiusM = FMath::Min(static_cast<double>(mNpcs->GetStats().FullRadiusM), midRadiusM);
    const double aiHysteresis = mNpcs->GetOptions().AiHysteresis;
    int32 missingFrames = 0;
    int32 doubledFrames = 0;
    const double realEndsUlpM = TrafficSmokeUlp(FMath::Max((laneFrame.AM - viewpointM).Size(),
        (laneFrame.BM - viewpointM).Size()));
    for (const FHandOverCase& handOverCase : cases)
    {
        const FSOLNpcRail& rail = rails[handOverCase.Index];
        const FVector3d& caseViewM = handOverCase.ViewpointM;
        FVector4f caseRows[SOL::NPC_GROUP_TEXTURE_ROWS];
        for (int32 row = 0; row < SOL::NPC_GROUP_TEXTURE_ROWS; ++row)
        {
            caseRows[row] = rows[row];
        }
        if (!handOverCase.bReal)
        {
            const FVector3f aRel(frame.GroupPointAM[mLane] - caseViewM);
            const FVector3f bRel(frame.GroupPointBM[mLane] - caseViewM);
            caseRows[0] = FVector4f(aRel, rows[0].W);
            caseRows[1] = FVector4f(bRel, rows[1].W);
        }
        const FSOLNpcRail gpuRail = SOLNpcRail::AdvancedToEpoch(rail, epochS, groups.LaneProfile);
        FVector4f texelA;
        FVector4f texelB;
        SOLNpcRail::GpuRailTexels(rail, epochS, groups.LaneProfile, texelA, texelB);
        const FVector3d cpuM = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(shaderTimeS));
        const FVector3f gpu = SOLNpcTrafficMirror::LaneShipRelM(texelA, texelB, caseRows, shaderTimeS);
        const double errorM = (FVector3d(gpu) - (cpuM - caseViewM)).Size();
        const double legS = SOLLaneTraffic::LaneShipLegS(profile, SOLNpcRail::LaneShipFromRail(rail));
        const double peakMps = SOL::NPC_LANE_WALL_SPEED_MPS + static_cast<double>(rail.P[1]);
        const double endsUlpM = TrafficSmokeUlp(FMath::Max((laneFrame.AM - caseViewM).Size(),
            (laneFrame.BM - caseViewM).Size()));
        const double boundM = peakMps * slope * TrafficSmokeUlp(legS) + 4.0 * TrafficSmokeUlp(profile.NominalLegM)
            + 4.0 * endsUlpM + 1.0;
        const double epochErrorM = (cpuM - SOLNpcRail::EvaluateM(rail, frame)).Size();
        worstErrorM = FMath::Max(worstErrorM, errorM);
        worstRatio = FMath::Max(worstRatio, errorM / boundM);
        worstEpochM = FMath::Max(worstEpochM, epochErrorM);
        failures += errorM <= boundM && epochErrorM <= boundM ? 0 : 1;

        // Frame by frame over one second; every sample must stay inside the band
        FVector3f previous = gpu;
        FVector3d previousCpuM = cpuM;
        FVector3f previousDrawn = gpu;
        bool bPreviousCpu = false;
        int32 floatOnlyRun = 0;
        SOLNpcScale::EAiTier tier = SOLNpcScale::EAiTier::Far;
        for (int32 frameIndex = 0; frameIndex <= static_cast<int32>(NPCTRAFFIC_SMOOTH_HZ); ++frameIndex)
        {
            const float timeS = static_cast<float>(static_cast<double>(shaderTimeS)
                + frameIndex / NPCTRAFFIC_SMOOTH_HZ);
            const FVector3f next = frameIndex == 0 ? gpu
                : SOLNpcTrafficMirror::LaneShipRelM(texelA, texelB, caseRows, timeS);
            const FVector3d nextCpuM = frameIndex == 0 ? cpuM
                : SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(timeS));
            const double distanceM = static_cast<double>(next.Size());
            nearestM = FMath::Min(nearestM, distanceM);
            farthestM = FMath::Max(farthestM, distanceM);
            outOfBand += distanceM >= bandMinM && distanceM <= bandMaxM ? 0 : 1;
            const SOLNpcTrafficMirror::FHandOver handOver = SOLNpcTrafficMirror::CpuPointHandOver(next,
                nextCpuM - caseViewM, static_cast<float>(midRadiusM), hullStartM);
            const FVector3f drawn = handOver.bCpuDraws ? handOver.RelM : next;

            // Drawn exactly once: the tiers decide by the double distance (the ship arriving from beyond as a Far ship,
            // the worst case), the CPU point layer draws a CPU ship the shader hides, the far layer every other ship.
            // Inside by the float test but outside the bare Mid radius by the double one is counted as well: the
            // margin must make those CPU ships
            const double doubleM = (nextCpuM - caseViewM).Size();
            tier = SOLNpcScale::SelectAiTier(tier, doubleM, fullRadiusM, midRadiusM + marginM, aiHysteresis);
            const bool bCpuPoint = tier != SOLNpcScale::EAiTier::Far && handOver.bCpuDraws;
            const bool bFarPoint = !handOver.bCpuDraws;
            missingFrames += !bCpuPoint && !bFarPoint ? 1 : 0;
            doubledFrames += bCpuPoint && bFarPoint ? 1 : 0;
            const bool bFloatOnly = handOver.bCpuDraws && doubleM >= midRadiusM;
            ++handOverFrames;
            floatOnlyFrames += bFloatOnly ? 1 : 0;
            floatOnlyRun = bFloatOnly ? floatOnlyRun + 1 : 0;
            worstFloatOnlyRun = FMath::Max(worstFloatOnlyRun, floatOnlyRun);
            if (frameIndex > 0)
            {
                // The float step against the CPU's (reported)
                const double stepM = FVector3d(next - previous).Size();
                const double diffM = FMath::Abs(stepM - (nextCpuM - previousCpuM).Size());
                repeatFrames += stepM > 0.0 ? 0 : 1;
                if (diffM > worstStepDiffM)
                {
                    worstStepDiffM = diffM;
                    worstStepDistanceM = distanceM;
                }

                // The drawn point's step off the far point's, in pixels at the Mid radius; at a crossing also the pop
                // the double position alone would make
                worstHandOverPx = FMath::Max(worstHandOverPx, FMath::Abs(FVector3d(drawn - previousDrawn).Size()
                    - stepM) / pixelAtMidM);
                if (handOver.bCpuDraws != bPreviousCpu)
                {
                    ++crossings;
                    const double rawPopPx = ((nextCpuM - caseViewM) - FVector3d(next)).Size() / pixelAtMidM;
                    minRawPopPx = FMath::Min(minRawPopPx, rawPopPx);
                    maxRawPopPx = FMath::Max(maxRawPopPx, rawPopPx);
                }
            }
            previous = next;
            previousCpuM = nextCpuM;
            previousDrawn = drawn;
            bPreviousCpu = handOver.bCpuDraws;
        }
    }
    const int32 handOverCount = cases.Num();
    Check(*FString::Printf(TEXT("%s-handover"), label), handOverCount > 0 && failures == 0 && outOfBand == 0
        && bRowsMatch, FString::Printf(TEXT("%d Earth-Mars ships (%d within %.1f - %.1f Mid radii now, %d made to "
        "cross it) at %.0f - %.0f km (Mid radius %.0f km, %d samples outside the band): float mirror vs CPU worst %.3f km, "
        "%.3f of the case-10 bound, %d over it; epoch re-base worst %.3f m; rows 2-5 as written %s, viewpoint %s "
        "(epoch %.1f s, shader time %.2f s)"), handOverCount, realCount, NPCTRAFFIC_BAND_MIN, NPCTRAFFIC_BAND_MAX,
        handOverCount - realCount, nearestM / 1000.0, farthestM / 1000.0, midRadiusM / 1000.0, outOfBand,
        worstErrorM / 1000.0, worstRatio, failures, worstEpochM, bRowsMatch ? TEXT("yes") : TEXT("NO"),
        bAnchorMatch ? TEXT("from the anchor") : TEXT("from row 0"), epochS, shaderTimeS));
    Check(*FString::Printf(TEXT("%s-handover-px"), label), crossings > 0 && outOfBand == 0
        && worstHandOverPx <= NPCTRAFFIC_HANDOVER_PX, FString::Printf(TEXT("%d Mid-radius crossings over 1 s at 60 Hz: "
        "the drawn point (CPU blend inside, far point beyond) steps within %.3f px of the far point at the Mid radius "
        "(limit %.1f px, 1 px = %.2f km there); the double position alone would pop %.1f - %.1f px"), crossings,
        worstHandOverPx, NPCTRAFFIC_HANDOVER_PX, pixelAtMidM / 1000.0, crossings > 0 ? minRawPopPx : 0.0,
        maxRawPopPx));
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTraffic INFO %s: hand-over ships over 1 s at 60 Hz: %d repeated float positions "
        "of %d frames, worst step difference to the CPU %.1f m at %.0f km from the viewpoint (end-distance ulp %.0f m)"),
        label, repeatFrames, handOverCount * static_cast<int32>(NPCTRAFFIC_SMOOTH_HZ), worstStepDiffM,
        worstStepDistanceM / 1000.0, realEndsUlpM);
    Check(*FString::Printf(TEXT("%s-handover-once"), label), handOverFrames > 0 && missingFrames == 0
        && doubledFrames == 0, FString::Printf(TEXT("%d hand-over frames, each ship drawn exactly once: %d missing, %d "
        "doubled; the lane's hand-over margin %.1f km (CPU ships within %.0f km); %d frames inside the Mid radius by "
        "the float test but outside it by the double distance (longest run %d), all CPU ships by the margin"),
        handOverFrames, missingFrames, doubledFrames, marginM / 1000.0, (midRadiusM + marginM) / 1000.0,
        floatOnlyFrames, worstFloatOnlyRun));
    if (!bSmooth)
    {
        return;
    }

    // Smoothness: the end-zone leg ships and the swinging U-turn ships nearest the viewpoint, 60 Hz for 5 s
    TArray<TPair<double, int32>> nearby;
    for (const int32 index : mLaneShips)
    {
        const FSOLNpcRail& rail = rails[index];
        const FSOLLaneThrustState thrust = SOLLaneTraffic::LaneThrustAt(profile, laneFrame,
            SOLNpcRail::LaneShipFromRail(rail), static_cast<uint32>(index), frame.RailTimeS);
        const FVector3d positionM = SOLNpcRail::EvaluateM(rail, frame);
        const double fromAM = ((positionM - laneFrame.AM) | laneFrame.Axis) - profile.EndArcAM;
        const bool bTurnOk = TrafficSmokeIsTurn(thrust.Segment)
            && static_cast<double>(rail.P[3]) >= NPCTRAFFIC_TURN_MIN_RADII * profile.WallRadiusM;
        const bool bEndZone = !TrafficSmokeIsTurn(thrust.Segment) && (fromAM < profile.MetricZoneM
            || laneFrame.CurrentLegM - fromAM < profile.MetricZoneM);
        if (bTurnOk || bEndZone)
        {
            nearby.Emplace((positionM - viewpointM).Size(), index);
        }
    }
    Algo::Sort(nearby, [](const TPair<double, int32>& a, const TPair<double, int32>& b) { return a.Key < b.Key; });
    const int32 smoothCount = FMath::Min(nearby.Num(), NPCTRAFFIC_SMOOTH_SAMPLES);
    int32 smoothFailures = 0;
    int32 turnShips = 0;
    double worstSmoothRatio = 0.0;
    FString firstFailure;
    for (int32 sample = 0; sample < smoothCount; ++sample)
    {
        const FSOLNpcRail& rail = rails[nearby[sample].Value];
        const FSOLNpcRail gpuRail = SOLNpcRail::AdvancedToEpoch(rail, epochS, groups.LaneProfile);
        FVector4f texelA;
        FVector4f texelB;
        SOLNpcRail::GpuRailTexels(rail, epochS, groups.LaneProfile, texelA, texelB);
        const FVector3d startM = SOLNpcRail::EvaluateM(rail, frame);
        const bool bNearA = (startM - laneFrame.AM).Size() <= (startM - laneFrame.BM).Size();
        const double nearEndUlpM = TrafficSmokeUlp(((bNearA ? laneFrame.AM : laneFrame.BM) - viewpointM).Size());
        turnShips += TrafficSmokeIsTurn(SOLLaneTraffic::LaneThrustAt(profile, laneFrame,
            SOLNpcRail::LaneShipFromRail(rail), static_cast<uint32>(nearby[sample].Value), frame.RailTimeS).Segment)
            ? 1 : 0;
        FVector3f previous = SOLNpcTrafficMirror::LaneShipRelM(texelA, texelB, rows, shaderTimeS);
        FVector3d previousCpuM = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(shaderTimeS));
        for (int32 frameIndex = 1; frameIndex <= NPCTRAFFIC_SMOOTH_FRAMES; ++frameIndex)
        {
            const float timeS = static_cast<float>(static_cast<double>(shaderTimeS)
                + frameIndex / NPCTRAFFIC_SMOOTH_HZ);
            const FVector3f next = SOLNpcTrafficMirror::LaneShipRelM(texelA, texelB, rows, timeS);
            const FVector3d nextCpuM = SOLNpcRail::EvaluateAtTimeM(gpuRail, frame, static_cast<double>(timeS));
            const double gpuStepM = FVector3d(next.X - previous.X, next.Y - previous.Y, next.Z - previous.Z).Size();
            const double cpuStepM = (nextCpuM - previousCpuM).Size();
            const double toleranceM = NPCTRAFFIC_SMOOTH_STEP * cpuStepM + NPCTRAFFIC_SMOOTH_ULPS * nearEndUlpM;
            worstSmoothRatio = FMath::Max(worstSmoothRatio, FMath::Abs(gpuStepM - cpuStepM) / toleranceM);
            if (!(gpuStepM > 0.0) || !(cpuStepM > 0.0) || !(FMath::Abs(gpuStepM - cpuStepM) <= toleranceM))
            {
                if (smoothFailures++ == 0)
                {
                    firstFailure = FString::Printf(TEXT("; first: ship %d frame %d: mirror step %.3f m, CPU %.3f m, "
                        "tolerance %.3f m"), nearby[sample].Value, frameIndex, gpuStepM, cpuStepM, toleranceM);
                }
            }
            previous = next;
            previousCpuM = nextCpuM;
        }
    }
    Check(*FString::Printf(TEXT("%s-smooth"), label), smoothCount >= 5 && smoothFailures == 0, FString::Printf(TEXT(
        "%d end-zone and U-turn ships nearest the viewpoint (%d turning, %.0f - %.0f km away), 60 Hz for 5 s: worst "
        "step difference %.3f of the tolerance (20 %% of the CPU step + 8 ulps of the nearer end), %d bad frames%s"),
        smoothCount, turnShips, smoothCount > 0 ? nearby[0].Key / 1000.0 : 0.0, smoothCount > 0
        ? nearby[smoothCount - 1].Key / 1000.0 : 0.0, worstSmoothRatio, smoothFailures, *firstFailure));
}

//////////////////////////////////////////////////////////////////////////
// Requests a screenshot with the HUD named NpcTraffic_<width>x<height>_<name>.png
void FSOLNpcTrafficSmoke::TakeShot(const TCHAR* name) const
{
    FVector2D size(0.0, 0.0);
    if (mVisuals.IsValid() && mVisuals->GetWorld()->GetGameViewport() != nullptr)
    {
        mVisuals->GetWorld()->GetGameViewport()->GetViewportSize(size);
    }
    const FString path = FPaths::ConvertRelativePathToFull(FPaths::ScreenShotDir() / FString::Printf(
        TEXT("NpcTraffic_%dx%d_%s.png"), FMath::RoundToInt32(size.X), FMath::RoundToInt32(size.Y), name));
    FScreenshotRequest::RequestScreenshot(path, true, false);
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTraffic: screenshot %s"), *path);
}

//////////////////////////////////////////////////////////////////////////
// Logs one check with its verdict and counts it
void FSOLNpcTrafficSmoke::Check(const TCHAR* label, const bool bPassed, const FString& detail)
{
    ++mChecksRun;
    mChecksPassed += bPassed ? 1 : 0;
    UE_LOG(LogSOL, Log, TEXT("SmokeNpcTraffic CHECK %-22s %s: %s"), label, bPassed ? TEXT("PASS") : TEXT("FAIL"),
        *detail);
}
